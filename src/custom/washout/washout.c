/*
 * washout.c - "Washout": a tempo-synced stereo feedback delay whose loop smears the sound
 * into a reverb-like cloud (the Octatrack-style washout for transitions).
 *
 * SIGNAL (per side, L and R each have their own ring and their own diffusers)
 *   in --Send--> + --> soft clip --> ring (16-bit, 3.8 s) --> read at Time --+--> wet out
 *                ^                                                           |
 *                +-- Fdbk x LoCut <- HiCut <- 4 all-pass diffusers (Wash) <--+
 *   The wet output is the diffused read, so even the first repeat is smeared. Every pass
 *   through the loop smears it again, so the repeats melt into a cloud.
 *
 * DIFFUSERS: 4 Schroeder all-passes in series (unity gain at every frequency, so they lose no
 *   level). Wash = their coefficient g, 0..0.7; 0 = skipped (no work). Lengths L 113 163 241
 *   335, R 131 157 251 313 samples (both sum to about 853: the read delay is shortened by that
 *   much so a synced Time still lands on the beat). A slow LFO per stage (0.31 .. 0.71 Hz,
 *   the right side half a cycle apart) moves each length by up to 20 samples (Mod 100). The
 *   fractional read is a first-order all-pass interpolator, so modulating loses no level either.
 *   The ring read is whole samples (the Time is rounded), so the loop has no interpolation loss.
 * LOOP FILTERS: HiCut = one-pole low-pass 11.6 kHz (1) .. 375 Hz (100); LoCut = one-pole
 *   high-pass 25 Hz (1) .. 800 Hz (100). 0 = off and skipped.
 * FDBK 0..120: gain into the loop = screen / 100, so 100 = 1:1. The diffusers lose no level, so
 *   Fdbk 100 + Send 0 + both cuts off holds the cloud. Above 100 it grows until the soft clip
 *   (transparent below 0.7, flat at 1.0) holds it, so it stays bounded.
 * SEND 0..100: a crossfade-send (no Mix knob), smoothed (about 7 ms): turning it up sends more
 *   input into the wash and fades the dry down, equal power (send = sin, dry = cos of Send x 90
 *   degrees). 0 = dry only at full level, the tail keeps ringing; 100 = wash only, dry silent.
 * WIDTH 0..100: mid/side on the wet output only, 0 = mono, 100 = as is.
 *
 * FOOTSWITCH: plain on/off with trails. OFF: Send ramps to 0, so nothing new goes in and the
 *   dry comes back to full level (as a stock effect that is off); the tail rings out (Fdbk capped
 *   at 95 so it dies).
 * IDLE: when nothing goes in (off, or Send 0) and the ring has been silent (under 2 LSB of the
 *   16-bit ring) for longer than the delay, the effect does no work: the dry passes as is.
 *
 * TIME (as GRDELAY): 0..100 free, ms = 12 + 0.0988 x screen^2 (12 ms .. 1 s);
 *   101..113 synced: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar 2bar at the pedal's
 *   tempo (zmt.h; the stock patch tempo without the MOD firmware). A synced time longer than the ring (3.8 s,
 *   168000 samples) is halved until it fits. Changing Time crossfades read points over 23 ms.
 *   With Wash on, a free Time under about 20 ms is held at the diffusers' own latency.
 *
 * MEMORY: 2 x 170000 16-bit samples (680 KB, scale 16384) + 16 KB of diffuser lines + state,
 *   under the 705 KB arena. Cleared 2048 samples per block after loading (dry meanwhile).
 *
 * KNOBS (screen values; 8, 3 pages; Time carries the tap flag, manifest flags 40)
 *   0 Time 0..113   1 Wash 0..100 (70)   2 Fdbk 0..120 (60)   3 HiCut 0..100 (0)
 *   4 LoCut 0..100 (0)   5 Mod 0..100 (20)   6 Width 0..100 (100)   7 Send 0..100 (100).
 *   Inside the code u[9] is the pedal's BPM.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline. Host tests only; never run on a pedal, CPU never measured.
 */

#include <stdint.h>
#include "../common/zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define WD_DO_PRAGMA(x) _Pragma(#x)
#define WD_EXPAND_PRAGMA(x) WD_DO_PRAGMA(x)
#define WD_ALWAYS_INLINE(fn) WD_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define WD_EIGHT WD_DO_PRAGMA(MUST_ITERATE(8, 8, 8)) WD_DO_PRAGMA(UNROLL(1))
#define WD_CODE_SECTION(fn) WD_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define WD_ALWAYS_INLINE(fn)
#define WD_EIGHT
#define WD_CODE_SECTION(fn)
#endif

#define WD_MAGIC      0x57444F35u        /* "WDO5": change whenever WdState changes      */
#define WD_N          170000             /* ring length per side                        */
#define WD_MAXD       168000.0f          /* longest delay (samples)                     */
#define WD_CLEAR_BLK  2048
#define WD_TO16       16384.0f
#define WD_FROM16     6.1035156e-5f
#define WD_MS         44.1f
#define WD_XF_STEP    9.765625e-4f       /* Time crossfade: 1024 samples = 23 ms */
#define WD_W2C        2.0555e-4f
#define WD_SG_RATE    0.003f             /* Send smoothing, about 7.5 ms */
#define WD_APN        512                /* all-pass line length (power of 2)           */
#define WD_APCOMP     853.0f             /* mean total all-pass delay (samples)         */
#define WD_QUIET      2.0f               /* ring "silent" below this (16384 scale)      */

typedef struct {
    int   hc, lc, ap;
    float dT, fb, cl, ch, g, amp, W, inT;
} WdParams;

/* Knob maths, diffuser lengths and Send gains are worked out only when they change: on the pedal every
 * byte of code a block runs costs about a cycle (it comes from slow memory each block). */
typedef struct {
    unsigned int magic;
    int   clr;             /* samples of the rings cleared so far                */
    int   wp;              /* next write index; age a = buf[wp - a]              */
    int   apw;             /* all-pass write position (all lines)                */
    int   apOn;            /* diffusers were running last block                  */
    int   quiet;           /* samples the ring has been silent (capped)          */
    float dCur, dOld, xf;  /* read delay now / before a Time change; crossfade 0..1 */
    float lp[2], hs[2];    /* loop low-pass state, loop high-pass's low part      */
    float sg;              /* Send gain, smoothed                                */
    float lfo[4];          /* diffuser LFO phases 0..1                           */
    float ip[2][4];        /* all-pass interpolator state per side and stage       */
    float rk[10];          /* raw knobs, on, BPM that P comes from                  */
    WdParams P;
    int   ia[2][4];        /* all-pass lengths: whole samples ...                  */
    float eta[2][4];       /* ... and the interpolator coefficient                 */
    float pamp;            /* Mod depth ia/eta were worked out for (-1: not yet)   */
    float esg, ea, eb;     /* Send the steady wash and dry gains are for (-1: not yet) */
    ZtSync zt;
    float ap[2][4][WD_APN];
    short buf[2 * WD_N];
} WdState;

WD_ALWAYS_INLINE(wd_ui)
static inline float wd_ui(float raw, float def_ui, float max_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.05f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > max_ui) ui = max_ui;
    if (ui < 0.0f) ui = 0.0f;
    return ui;
}

WD_ALWAYS_INLINE(wd_exp2)
static inline float wd_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int   n = (int)x;
    float f, r;
    if (x < 0.0f && (float)n != x) n--;
    f = x - (float)n;
    r = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    c.u = ((unsigned int)(n + 127)) << 23;
    return r * c.f;
}

WD_ALWAYS_INLINE(wd_recip)
static inline float wd_recip(float x)
{
    union { float f; unsigned int u; } c;
    float y;
    c.f = x;
    c.u = 0x7EF311C7u - c.u;
    y = c.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y;
}

WD_ALWAYS_INLINE(wd_coef)
static inline float wd_coef(float hz)
{
    return 1.0f - wd_exp2(-hz * WD_W2C);
}

/* Synced Time 101..113 in beats: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar 2bar */
WD_ALWAYS_INLINE(wd_note_beats)
static inline float wd_note_beats(int d)
{
    if (d <= 0) return 0.125f;
    if (d == 1) return 0.16666667f;
    if (d == 2) return 0.25f;
    if (d == 3) return 0.33333334f;
    if (d == 4) return 0.375f;
    if (d == 5) return 0.5f;
    if (d == 6) return 0.6666667f;
    if (d == 7) return 0.75f;
    if (d == 8) return 1.0f;
    if (d == 9) return 1.5f;
    if (d == 10) return 2.0f;
    if (d == 11) return 4.0f;
    return 8.0f;
}

/* bipolar sine-like LFO from a phase 0..1 (parabola, within 6 %) */
WD_ALWAYS_INLINE(wd_lfo)
static inline float wd_lfo(float p)
{
    float t = p + p - 1.0f, a = (t < 0.0f) ? -t : t;
    return -4.0f * t * (1.0f - a);
}

/* sin(x * 90 degrees) for x in 0..1 (odd polynomial, exactly 0 at 0 and 1 at 1, within 0.02 %) */
WD_ALWAYS_INLINE(wd_eqp)
static inline float wd_eqp(float x)
{
    float q = x * x;
    return x * (1.570796f + q * (-0.645964f + q * (0.079693f - 0.004525f * q)));
}

/* soft clip: transparent up to 0.7, flat at 1.0 from 1.3 */
WD_ALWAYS_INLINE(wd_sat)
static inline float wd_sat(float x)
{
    float a = (x < 0.0f) ? -x : x, u = (a - 0.7f) * 1.6666667f, y;
    u = (u < 0.0f) ? 0.0f : u;
    u = (u > 1.0f) ? 1.0f : u;
    y = (a < 0.7f) ? a : 0.7f + 0.3f * u * (2.0f - u);
    return (x < 0.0f) ? -y : y;
}

/* all-pass base lengths: stage k, side c (0 L, 1 R) */
WD_ALWAYS_INLINE(wd_base)
static inline float wd_base(int c, int k)
{
    if (k == 0) return c ? 131.0f : 113.0f;
    if (k == 1) return c ? 157.0f : 163.0f;
    if (k == 2) return c ? 251.0f : 241.0f;
    return c ? 313.0f : 335.0f;
}

WD_ALWAYS_INLINE(wd_read)
static inline float wd_read(const short *b, int wp, int age)
{
    int j = wp - age;
    j = (j < 0) ? j + WD_N : j;
    return (float)b[j] * WD_FROM16;
}

WD_ALWAYS_INLINE(wd_init)
static inline void wd_init(WdState *s)
{
    int k;
    s->clr = 0; s->wp = 0; s->apw = 0; s->apOn = 0; s->quiet = 0;
    s->dCur = 0.0f; s->dOld = 0.0f; s->xf = 1.0f;
    s->lp[0] = s->lp[1] = 0.0f; s->hs[0] = s->hs[1] = 0.0f;
    s->sg = 0.0f;
    for (k = 0; k < 8; k++) s->ip[k >> 2][k & 3] = 0.0f;
    for (k = 0; k < 4; k++) s->lfo[k] = 0.25f * (float)k;
    for (k = 0; k < 10; k++) s->rk[k] = -1.0f;  /* no knob is -1: the first block works everything out */
    s->pamp = -1.0f; s->esg = -1.0f;
    zt_init(&s->zt);
    s->magic = WD_MAGIC;
}

WD_ALWAYS_INLINE(wd_clearing)
static inline int wd_clearing(WdState *s)
{
    int i, n = s->clr;
    if (n >= 2 * WD_N) return 0;
    for (i = 0; i < WD_CLEAR_BLK && n < 2 * WD_N; i++) { s->buf[n] = 0; n++; }
    s->clr = n;
    return 1;
}

/* Knobs (u[] = screen values) -> per-block numbers */
WD_ALWAYS_INLINE(wd_prepare)
static inline void wd_prepare(WdParams *P, const float *u, int on)
{
    int n = (int)(u[0] + 0.5f);
    float d, fbG = u[2] * 0.01f, nb;
    P->ap = (u[1] > 0.0f);
    P->g = 0.007f * u[1];
    P->amp = 0.1f * u[5];
    P->hc = (u[3] > 0.0f);
    P->lc = (u[4] > 0.0f);
    P->cl = wd_coef(12000.0f * wd_exp2(-0.05f * u[3]));
    P->ch = wd_coef(25.0f * wd_exp2(0.05f * u[4]));
    P->W = u[6] * 0.01f;
    if (n > 100) {
        nb = wd_note_beats(n - 101);
        d = nb * 2646000.0f * wd_recip(u[9]);    /* u[9] = BPM from zt_update */
        while (d > WD_MAXD) d *= 0.5f;
    } else {
        d = (float)(int)(12.0f + 0.0988f * (float)(n * n) + 0.5f) * WD_MS;
    }
    if (P->ap) { d -= WD_APCOMP; if (d < 16.0f) d = 16.0f; }
    P->dT = (float)(int)(d + 0.5f);            /* whole samples: no interpolation loss in the loop */
    if (!on && fbG > 0.95f) fbG = 0.95f;         /* switched off: the tail dies */
    P->fb = fbG;
    P->inT = on ? u[7] * 0.01f : 0.0f;
}

WD_ALWAYS_INLINE(wd_process)
static inline void wd_process(WdState *s, const WdParams *P, float *buf)
{
    int i, k, c, wp0 = s->wp, apw = s->apw;
    float dCur = s->dCur, dOld, xf = s->xf, sg = s->sg, bigm = 0.0f, inT = P->inT;
    float xs[2][8], xe[8], sgv[8], dgv[8], yl[8], yr[8], wo[2][8], md[2][4], cv[16];
    const float fb = P->fb, cl = P->cl, ch_ = P->ch, g = P->g, W = P->W;
    const int hc = P->hc, lc = P->lc;
    short * restrict sb = s->buf;

    /* nothing goes in and the ring has been silent for longer than the delay: the dry passes as is */
    if (inT == 0.0f && sg == 0.0f && xf >= 1.0f) {
        float md0 = (dCur > P->dT) ? dCur : P->dT;
        if (s->quiet > (int)(md0 + 4096.0f)) {
            s->dCur = P->dT;
            return;
        }
    }

    if (dCur <= 0.0f) dCur = P->dT;
    /* Time changed by more than 0.5 %: crossfade to it (a wobbling synced BPM must not restart the crossfade) */
    if (xf >= 1.0f && (P->dT - dCur > 0.005f * dCur + 0.5f || dCur - P->dT > 0.005f * dCur + 0.5f)) {
        s->dOld = dCur; dCur = P->dT; xf = 0.0f;
    }
    dOld = s->dOld;

    if (P->ap && !s->apOn) {                      /* diffusers switch on: start from silence */
        for (c = 0; c < 2; c++) for (k = 0; k < 4; k++) for (i = 0; i < WD_APN; i++) s->ap[c][k][i] = 0.0f;
    }
    s->apOn = P->ap;

    /* LFO phases once per block; all-pass lengths per side and stage (Mod 0: fixed, kept from before) */
    if (P->ap && (P->amp != 0.0f || s->pamp != 0.0f)) {
    s->pamp = P->amp;
    for (k = 0; k < 4; k++) {
        float p = s->lfo[k] + 1.8143e-4f * (0.31f + 0.133f * (float)k), q;
        p = (p >= 1.0f) ? p - 1.0f : p;
        s->lfo[k] = p;
        q = p + 0.5f; q = (q >= 1.0f) ? q - 1.0f : q;
        md[0][k] = wd_base(0, k) + P->amp * (1.0f + wd_lfo(p));
        md[1][k] = wd_base(1, k) + P->amp * (1.0f + wd_lfo(q));
    }
    /* each length = whole samples + a fraction 0.5..1.5 for the all-pass interpolator: its coefficient stays
     * in -0.2..0.33, so a whole-sample step is a small change (a fraction near 0 put the pole near -1: clicks) */
    for (c = 0; c < 2; c++) for (k = 0; k < 4; k++) {
        float fr;
        s->ia[c][k] = (int)(md[c][k] - 0.5f);
        fr = md[c][k] - (float)s->ia[c][k];
        fr = 0.5f * (fr - 1.0f);               /* (1 - fr) / (1 + fr) = -h / (1 + h), h in -0.25..0.25: series */
        s->eta[c][k] = -fr * (1.0f + fr * (-1.0f + fr * (1.0f + fr * (-1.0f + fr))));
    }
    }

    /* per-sample ramps: Send (wash and dry gains, equal-power crossfade; worked out once while Send is
     * steady), Time crossfade */
    if (sg == inT) {
        float a, b;
        if (sg != s->esg) { s->ea = wd_eqp(sg); s->eb = (sg == 0.0f) ? 1.0f : wd_eqp(1.0f - sg); s->esg = sg; }
        a = s->ea; b = s->eb;
        for (i = 0; i < 8; i++) { sgv[i] = a; dgv[i] = b; }
    } else {
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            sg += (inT - sg) * WD_SG_RATE;
            sg = (sg - inT < 1e-5f && inT - sg < 1e-5f) ? inT : sg;   /* land exactly: Send 100 = dry silent */
            sgv[i] = wd_eqp(sg); dgv[i] = (sg == 0.0f) ? 1.0f : wd_eqp(1.0f - sg);
        }
    }
    WD_EIGHT
    for (i = 0; i < 8; i++) { xs[0][i] = buf[i]; xs[1][i] = buf[i + 8]; }
    xe[0] = xf;   /* 1: no Time crossfade running, the reads below take one tap */
    if (xf < 1.0f) for (i = 0; i < 8; i++) {
        xe[i] = xf;
        xf = (xf + WD_XF_STEP > 1.0f) ? 1.0f : xf + WD_XF_STEP;
    }

    /* read both sides (crossfaded between the old and new Time; one tap when no crossfade runs) */
    if (xe[0] >= 1.0f) {
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            int wpk = wp0 + i;
            wpk = (wpk >= WD_N) ? wpk - WD_N : wpk;
            yl[i] = wd_read(sb, wpk, (int)dCur);
            yr[i] = wd_read(sb + WD_N, wpk, (int)dCur);
        }
    } else {
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            int wpk = wp0 + i;
            wpk = (wpk >= WD_N) ? wpk - WD_N : wpk;
            yl[i] = xe[i] * wd_read(sb, wpk, (int)dCur) + (1.0f - xe[i]) * wd_read(sb, wpk, (int)dOld);
            yr[i] = xe[i] * wd_read(sb + WD_N, wpk, (int)dCur) + (1.0f - xe[i]) * wd_read(sb + WD_N, wpk, (int)dOld);
        }
    }

    /* diffusers: all eight lines (four stages a side) in one pass over the block. Each line's fractional
     * all-pass chains sample to sample; eight of them side by side keep the DSP's pipeline full */
    if (P->ap) {
        float t0[9], t1[9], t2[9], t3[9], t4[9], t5[9], t6[9], t7[9];
        float e0, e1, e2, e3, e4, e5, e6, e7, p0, p1, p2, p3, p4, p5, p6, p7;
        int r0, r1, r2, r3, r4, r5, r6, r7;
#define WD_TAPS(j, c, k) { r##j = apw - s->ia[c][k] - 1; e##j = s->eta[c][k]; p##j = s->ip[c][k]; }
#define WD_TAP(j, c, k) t##j[i] = s->ap[c][k][(r##j + i) & (WD_APN - 1)];
#define WD_AP(v, j, c, k) { float d = t##j[i] + e##j * t##j[i + 1] - e##j * p##j, wv = v + g * d; p##j = d; \
        s->ap[c][k][w] = wv; v = d - g * wv; }
        WD_TAPS(0, 0, 0) WD_TAPS(1, 0, 1) WD_TAPS(2, 0, 2) WD_TAPS(3, 0, 3)
        WD_TAPS(4, 1, 0) WD_TAPS(5, 1, 1) WD_TAPS(6, 1, 2) WD_TAPS(7, 1, 3)
        /* the 9 taps the block reads first: lengths >= 112 never reach this block's 8 writes */
        for (i = 0; i < 9; i++) {
            WD_TAP(0, 0, 0) WD_TAP(1, 0, 1) WD_TAP(2, 0, 2) WD_TAP(3, 0, 3)
            WD_TAP(4, 1, 0) WD_TAP(5, 1, 1) WD_TAP(6, 1, 2) WD_TAP(7, 1, 3)
        }
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            int w = (apw + i) & (WD_APN - 1);
            float vl = yl[i], vr = yr[i];
            WD_AP(vl, 0, 0, 0) WD_AP(vr, 4, 1, 0)
            WD_AP(vl, 1, 0, 1) WD_AP(vr, 5, 1, 1)
            WD_AP(vl, 2, 0, 2) WD_AP(vr, 6, 1, 2)
            WD_AP(vl, 3, 0, 3) WD_AP(vr, 7, 1, 3)
            yl[i] = vl; yr[i] = vr;
        }
#undef WD_TAPS
#undef WD_TAP
#undef WD_AP
        s->ip[0][0] = p0; s->ip[0][1] = p1; s->ip[0][2] = p2; s->ip[0][3] = p3;
        s->ip[1][0] = p4; s->ip[1][1] = p5; s->ip[1][2] = p6; s->ip[1][3] = p7;
    }

    /* the wet out is the diffused read; then loop filters (always run; off = pass through), Send, soft clip,
     * both sides in one loop */
    {
        const float bl = hc ? cl : 1.0f, al = 1.0f - bl, bh = lc ? ch_ : 0.0f, ah = 1.0f - bh;
        float l0 = s->lp[0], l1 = s->lp[1], h0 = lc ? s->hs[0] : 0.0f, h1 = lc ? s->hs[1] : 0.0f;
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            float v0, v1, a0, a1;
            wo[0][i] = yl[i]; wo[1][i] = yr[i];
            l0 = al * l0 + bl * yl[i]; l1 = al * l1 + bl * yr[i];
            h0 = ah * h0 + bh * l0;    h1 = ah * h1 + bh * l1;
            v0 = wd_sat(sgv[i] * xs[0][i] + fb * (l0 - h0)) * WD_TO16;
            v1 = wd_sat(sgv[i] * xs[1][i] + fb * (l1 - h1)) * WD_TO16;
            cv[i] = v0; cv[i + 8] = v1;
            a0 = (v0 < 0.0f) ? -v0 : v0; a1 = (v1 < 0.0f) ? -v1 : v1;
            a0 = (a1 > a0) ? a1 : a0;
            bigm = (a0 > bigm) ? a0 : bigm;
        }
        s->lp[0] = l0; s->lp[1] = l1; s->hs[0] = h0; s->hs[1] = h1;
    }
    /* the ring stores in a loop of their own: in the loop above they chain every sample to the last */
    WD_EIGHT
    for (i = 0; i < 8; i++) {
        int wpk = wp0 + i;
        wpk = (wpk >= WD_N) ? wpk - WD_N : wpk;
        sb[wpk] = (short)(int)cv[i];
        sb[WD_N + wpk] = (short)(int)cv[i + 8];
    }
    s->quiet = (bigm >= WD_QUIET || sg > 0.0f) ? 0 : ((s->quiet < 100000000) ? s->quiet + 8 : s->quiet);

    {
        const float wa = 0.5f + 0.5f * W, wb = 0.5f - 0.5f * W;   /* width: Width 100 = 1 and 0, as is */
        float * restrict ob = buf;
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            float wL = wo[0][i], wR = wo[1][i];
            ob[i] = dgv[i] * xs[0][i] + wa * wL + wb * wR;
            ob[i + 8] = dgv[i] * xs[1][i] + wa * wR + wb * wL;
        }
    }
    wp0 += 8;
    s->wp = (wp0 >= WD_N) ? wp0 - WD_N : wp0;
    s->apw = (apw + 8) & (WD_APN - 1);
    s->sg = sg; s->dCur = dCur; s->xf = xf;
}

/* ------------------------------------------------------------------ */
/* On-screen text: ZDL_GetLabel_<knob index>(screen value, 8-byte out),  */
/* characters one by one (no string literals, no tables), at most 5.     */
/* ------------------------------------------------------------------ */

WD_ALWAYS_INLINE(wd_put4)
static inline int wd_put4(char *out, int a, int b, int c, int d, int e)
{
    int n = 3;
    out[0] = (char)a; out[1] = (char)b; out[2] = (char)c;
    if (d) { out[3] = (char)d; n = 4; }
    if (e) { out[4] = (char)e; n = 5; }
    out[n] = 0;
    return n;
}

/* Time: 12ms .. 1.00s, then 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar 2bar */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    int v, ms, h = 0, t = 0, len = 0;
    if (value > 113u) value = 113u;
    if (value > 100u) {
        v = (int)value - 101;
        if (v == 12) return wd_put4(out, '2', 'b', 'a', 'r', 0);
        if (v == 11) return wd_put4(out, '1', 'b', 'a', 'r', 0);
        if (v == 0)  return wd_put4(out, '1', '/', '3', '2', 0);
        if (v == 1)  return wd_put4(out, '1', '/', '1', '6', 'T');
        if (v == 2)  return wd_put4(out, '1', '/', '1', '6', 0);
        if (v == 3)  return wd_put4(out, '1', '/', '8', 'T', 0);
        if (v == 4)  return wd_put4(out, '1', '/', '1', '6', '.');
        if (v == 5)  return wd_put4(out, '1', '/', '8', 0, 0);
        if (v == 6)  return wd_put4(out, '1', '/', '4', 'T', 0);
        if (v == 7)  return wd_put4(out, '1', '/', '8', '.', 0);
        if (v == 8)  return wd_put4(out, '1', '/', '4', 0, 0);
        if (v == 9)  return wd_put4(out, '1', '/', '4', '.', 0);
        return wd_put4(out, '1', '/', '2', 0, 0);
    }
    v = (int)value;
    ms = (int)(12.0f + 0.0988f * (float)(v * v) + 0.5f);
    if (ms >= 1000) return wd_put4(out, '1', '.', '0', '0', 's');
    while (ms >= 100) { ms -= 100; h++; }
    while (ms >= 10)  { ms -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + ms); len++;
    out[len] = 'm'; len++;
    out[len] = 's'; len++;
    out[len] = 0;
    return len;
}

#ifndef WASHOUT_HOST_TEST

#include "washout_params.h"

#ifndef WASHOUT_AUDIO_FUNC
#define WASHOUT_AUDIO_FUNC Fx_DLY_Washout
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

WD_CODE_SECTION(WASHOUT_AUDIO_FUNC)
void WASHOUT_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    WdState *s;
    float u[10], r[10], beats;
    int run, start, k, chg = 0;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(WdState) || span < (end - base)) return;
    if (stateBase + sizeof(WdState) > end) return;

    s = (WdState *)stateBase;

    if (s->magic != WD_MAGIC) wd_init(s);
    r[0] = params[WASHOUT_TIME_SLOT];  r[1] = params[WASHOUT_WASH_SLOT];
    r[2] = params[WASHOUT_FDBK_SLOT];  r[3] = params[WASHOUT_HICUT_SLOT];
    r[4] = params[WASHOUT_LOCUT_SLOT]; r[5] = params[WASHOUT_MOD_SLOT];
    r[6] = params[WASHOUT_WIDTH_SLOT]; r[7] = params[WASHOUT_SEND_SLOT];
    r[8] = (params[0] >= 0.5f) ? 1.0f : 0.0f;
    r[9] = zt_update(&s->zt, &run, &beats, &start);   /* the pedal's BPM */
    for (k = 0; k < 10; k++) if (r[k] != s->rk[k]) chg = 1;
    if (chg) {
        u[0] = wd_ui(r[0], (float)WASHOUT_TIME_UI_DEFAULT,  113.0f);
        u[1] = wd_ui(r[1], (float)WASHOUT_WASH_UI_DEFAULT,  100.0f);
        u[2] = wd_ui(r[2], (float)WASHOUT_FDBK_UI_DEFAULT,  120.0f);
        u[3] = wd_ui(r[3], (float)WASHOUT_HICUT_UI_DEFAULT, 100.0f);
        u[4] = wd_ui(r[4], (float)WASHOUT_LOCUT_UI_DEFAULT, 100.0f);
        u[5] = wd_ui(r[5], (float)WASHOUT_MOD_UI_DEFAULT,   100.0f);
        u[6] = wd_ui(r[6], (float)WASHOUT_WIDTH_UI_DEFAULT, 100.0f);
        u[7] = wd_ui(r[7], (float)WASHOUT_SEND_UI_DEFAULT,  100.0f);
        u[9] = r[9];
        wd_prepare(&s->P, u, r[8] != 0.0f);
        for (k = 0; k < 10; k++) s->rk[k] = r[k];
    }
    if (wd_clearing(s)) return;                  /* first ~31 ms after loading: dry */
    wd_process(s, &s->P, fxBuf);                 /* L and R in place */
}

#endif /* WASHOUT_HOST_TEST */
