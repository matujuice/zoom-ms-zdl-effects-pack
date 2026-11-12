/*
 * washout.c - "Washout": one Wash knob turns the sound into a cloud (a Transit-style macro).
 *
 * SIGNAL (per side, L and R each have their own loop)
 *   in --send--> + --> soft clip --> HiCut --> LoCut --> 4 all-pass diffusers --+--> wet out
 *                ^                                                             |
 *                +---- fb x <---- ring (Size: 27..136 ms, R 13 % longer) <-----+
 *   No discrete repeats: the loop is short and every pass is diffused again, so it builds a cloud.
 *   The filters sit at the loop's input, so the first sound is already shaped and each pass again.
 *
 * WASH 0..100 moves everything at once (w = Wash / 100, smoothed per block, about 10 ms):
 *   send = sin(w x 90 deg) into the loop, dry = cos(w x 90 deg): equal power, 100 = wash only
 *   decay fb = 0.40 .. 0.98 (about 1 s .. almost endless)
 *   diffusion g = 0.50 .. 0.70
 *   HiCut 12 kHz .. 3 kHz (x Tone), LoCut 25 Hz .. 300 Hz
 *   Wash 0: nothing goes in and the tail rings out with a short decay; once it has died the effect
 *   does no work (the dry passes as is). Nothing is ever cleared in one block.
 * TONE 0..100: the HiCut range x 0.5 (dark) .. x 2 (bright), 50 = as above.
 * SIZE 0..100: the loop length, 27 ms .. 136 ms (L), crossfaded over 23 ms when it changes.
 * FOOTSWITCH: plain on/off with trails. OFF: send ramps to 0, the dry comes back to full level and
 *   the tail rings out (decay capped at 0.9 so it dies).
 *
 * MEMORY: 2 x 7000 16-bit samples (14-bit codes, zarena.h) + 16 KB of diffuser lines + state.
 *   Cleared 512 values per block after loading (dry meanwhile, about 7 ms).
 *
 * KNOBS (screen values; 3, one page): 0 Wash 0..100 (50)   1 Tone 0..100 (50)   2 Size 0..100 (50).
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline. Host tests only; CPU not measured on the pedal yet.
 */

#include <stdint.h>
#include "../common/zarena.h"

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

#define WD_MAGIC      0x57444F38u        /* "WDO8": change whenever WdState changes      */
#define WD_N          7000               /* ring length per side (a multiple of 8)      */
#define WD_APN        512                /* all-pass line length (power of 2)           */
#define WD_CLEAR_N    (2 * WD_N + 2 * 4 * WD_APN)
#define WD_CLEAR_BLK  512
#define WD_TO16       4096.0f            /* 14-bit codes in the ring (zarena.h) */
#define WD_FROM16     2.4414062e-4f      /* 1 / 4096 */
#define WD_XF_STEP    9.765625e-4f       /* Size crossfade: 1024 samples = 23 ms */
#define WD_W2C        2.0555e-4f
#define WD_SG_RATE    0.003f             /* send smoothing, about 7.5 ms */
#define WD_W_RATE     0.1f               /* Wash smoothing per block, about 10 ms */
#define WD_QUIET      0.5f               /* ring "silent" below this (4096 scale)       */

typedef struct {
    float dT, fb, cl, ch, g, inT;
} WdParams;

/* Knob maths and gains are worked out only when they change: on the pedal every
 * byte of code a block runs costs about a cycle (it comes from slow memory each block). */
typedef struct {
    unsigned int magic;
    int   clr;             /* values cleared so far (rings, then diffuser lines) */
    int   wp;              /* next write index; age a = buf[wp - a]              */
    int   apw;             /* all-pass write position (all lines)                */
    int   quiet;           /* samples the ring has been silent (capped)          */
    float dCur, dOld, xf;  /* loop length now / before a Size change; crossfade 0..1 */
    float lp[2], hs[2];    /* loop low-pass state, loop high-pass's low part      */
    float sg;              /* send, smoothed                                     */
    float ws;              /* Wash, smoothed (0..1)                              */
    float rk[4];           /* raw knobs and on that P comes from                 */
    WdParams P;
    float esg, ea, eb;     /* send the steady wash and dry gains are for (-1: not yet) */
    unsigned int seal[ZA_NSEAL(28)]; /* header bits 30, 29 between blocks (zarena.h) */
    float ap[2][4][WD_APN];
    short buf[2 * WD_N];   /* 14-bit codes: za_s14 / za_u14 */
} WdState;
ZA_CHECK(WdState);

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

WD_ALWAYS_INLINE(wd_coef)
static inline float wd_coef(float hz)
{
    return 1.0f - wd_exp2(-hz * WD_W2C);
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

/* all-pass lengths: stage k, side c (0 L, 1 R) */
WD_ALWAYS_INLINE(wd_len)
static inline int wd_len(int c, int k)
{
    if (k == 0) return c ? 131 : 113;
    if (k == 1) return c ? 157 : 163;
    if (k == 2) return c ? 251 : 241;
    return c ? 313 : 335;
}

WD_ALWAYS_INLINE(wd_read)
static inline float wd_read(const short *b, int wp, int age)
{
    int j = wp - age;
    j = (j < 0) ? j + WD_N : j;
    return (float)za_u14(b[j]) * WD_FROM16;
}

WD_ALWAYS_INLINE(wd_init)
static inline void wd_init(WdState *s)
{
    int k;
    s->clr = 0; s->wp = 0; s->apw = 0; s->quiet = 0;
    s->dCur = 0.0f; s->dOld = 0.0f; s->xf = 1.0f;
    s->lp[0] = s->lp[1] = 0.0f; s->hs[0] = s->hs[1] = 0.0f;
    s->sg = 0.0f; s->ws = -1.0f;
    for (k = 0; k < 4; k++) s->rk[k] = -1.0f;  /* no knob is -1: the first block works everything out */
    s->esg = -1.0f;
    ZA_ZERO_SEAL(s);
    s->magic = WD_MAGIC;
}

/* the rings, then the diffuser lines, a little each block (no spike) */
WD_ALWAYS_INLINE(wd_clearing)
static inline int wd_clearing(WdState *s)
{
    int i, n = s->clr;
    float *a = &s->ap[0][0][0];
    if (n >= WD_CLEAR_N) return 0;
    for (i = 0; i < WD_CLEAR_BLK && n < WD_CLEAR_N; i++) {
        if (n < 2 * WD_N) s->buf[n] = 0; else a[n - 2 * WD_N] = 0.0f;
        n++;
    }
    s->clr = n;
    return 1;
}

/* w = smoothed Wash 0..1, t = Tone, z = Size (screen values) -> per-block numbers */
WD_ALWAYS_INLINE(wd_prepare)
static inline void wd_prepare(WdParams *P, float w, float t, float z, int on)
{
    float fb = 0.40f + 0.65f * w;   /* above 1 near 100: the loop filters lose about 9 % a pass */
    P->g = 0.50f + 0.20f * w;
    P->cl = wd_coef(12000.0f * wd_exp2(-2.0f * w + 0.02f * (t - 50.0f)));
    P->ch = wd_coef(25.0f * wd_exp2(3.585f * w));
    P->dT = (float)(int)(1200.0f + 48.0f * z + 0.5f);   /* whole samples: no interpolation loss in the loop */
    if (!on && fb > 0.9f) fb = 0.9f;                       /* switched off: the tail dies */
    P->fb = fb;
    P->inT = on ? w : 0.0f;
}

WD_ALWAYS_INLINE(wd_process)
static inline void wd_process(WdState *s, const WdParams *P, float *buf)
{
    int i, wp0 = s->wp, apw = s->apw, aL, aR, oL, oR;
    float dCur = s->dCur, dOld, xf = s->xf, sg = s->sg, bigm = 0.0f, inT = P->inT;
    float xs[2][8], xe[8], sgv[8], dgv[8], yl[8], yr[8], cv[16];
    const float fb = P->fb, cl = P->cl, ch_ = P->ch, g = P->g;
    short * restrict sb = s->buf;

    /* nothing goes in and the ring has been silent for longer than the loop: the dry passes as is */
    if (inT == 0.0f && sg == 0.0f && xf >= 1.0f && s->quiet > (int)(P->dT * 1.13f + 4096.0f)) {
        s->dCur = P->dT;
        return;
    }

    if (dCur <= 0.0f) dCur = P->dT;
    if (xf >= 1.0f && dCur != P->dT) { s->dOld = dCur; dCur = P->dT; xf = 0.0f; }   /* Size moved: crossfade */
    dOld = s->dOld;
    aL = (int)dCur; aR = (int)(dCur * 1.13f); oL = (int)dOld; oR = (int)(dOld * 1.13f);

    /* per-sample ramps: send and dry gains (equal power; worked out once while steady), Size crossfade */
    if (sg == inT) {
        float a, b;
        if (sg != s->esg) { s->ea = wd_eqp(sg); s->eb = (sg == 0.0f) ? 1.0f : wd_eqp(1.0f - sg); s->esg = sg; }
        a = s->ea; b = s->eb;
        for (i = 0; i < 8; i++) { sgv[i] = a; dgv[i] = b; }
    } else {
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            sg += (inT - sg) * WD_SG_RATE;
            sg = (sg - inT < 1e-5f && inT - sg < 1e-5f) ? inT : sg;   /* land exactly: Wash 100 = dry silent */
            sgv[i] = wd_eqp(sg); dgv[i] = (sg == 0.0f) ? 1.0f : wd_eqp(1.0f - sg);
        }
    }
    WD_EIGHT
    for (i = 0; i < 8; i++) { xs[0][i] = buf[i]; xs[1][i] = buf[i + 8]; }
    xe[0] = xf;   /* 1: no crossfade running, the reads below take one tap */
    if (xf < 1.0f) for (i = 0; i < 8; i++) {
        xe[i] = xf;
        xf = (xf + WD_XF_STEP > 1.0f) ? 1.0f : xf + WD_XF_STEP;
    }

    /* read the loop's return, both sides */
    if (xe[0] >= 1.0f) {
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            int wpk = wp0 + i;
            wpk = (wpk >= WD_N) ? wpk - WD_N : wpk;
            yl[i] = wd_read(sb, wpk, aL);
            yr[i] = wd_read(sb + WD_N, wpk, aR);
        }
    } else {
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            int wpk = wp0 + i;
            wpk = (wpk >= WD_N) ? wpk - WD_N : wpk;
            yl[i] = xe[i] * wd_read(sb, wpk, aL) + (1.0f - xe[i]) * wd_read(sb, wpk, oL);
            yr[i] = xe[i] * wd_read(sb + WD_N, wpk, aR) + (1.0f - xe[i]) * wd_read(sb + WD_N, wpk, oR);
        }
    }

    /* loop input: send + feedback, soft clip, HiCut, LoCut (both sides in one loop) */
    {
        const float al = 1.0f - cl, ah = 1.0f - ch_;
        float l0 = s->lp[0], l1 = s->lp[1], h0 = s->hs[0], h1 = s->hs[1];
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            float v0 = wd_sat(sgv[i] * xs[0][i] + fb * yl[i]), v1 = wd_sat(sgv[i] * xs[1][i] + fb * yr[i]);
            l0 = al * l0 + cl * v0;  l1 = al * l1 + cl * v1;
            h0 = ah * h0 + ch_ * l0; h1 = ah * h1 + ch_ * l1;
            yl[i] = l0 - h0; yr[i] = l1 - h1;
        }
        s->lp[0] = l0; s->lp[1] = l1; s->hs[0] = h0; s->hs[1] = h1;
    }

    /* diffusers: all eight lines (four stages a side) in one pass over the block. Each all-pass chains
     * sample to sample; eight of them side by side keep the DSP's pipeline full */
    {
        float t0[8], t1[8], t2[8], t3[8], t4[8], t5[8], t6[8], t7[8];
#define WD_TAP(j, c, k) t##j[i] = s->ap[c][k][(apw - wd_len(c, k) + i) & (WD_APN - 1)];
#define WD_AP(v, j, c, k) { float d = t##j[i], wv = v + g * d; s->ap[c][k][w] = wv; v = d - g * wv; }
        /* the 8 taps the block reads first: lengths >= 113 never reach this block's 8 writes */
        for (i = 0; i < 8; i++) {
            WD_TAP(0, 0, 0) WD_TAP(1, 0, 1) WD_TAP(2, 0, 2) WD_TAP(3, 0, 3)
            WD_TAP(4, 1, 0) WD_TAP(5, 1, 1) WD_TAP(6, 1, 2) WD_TAP(7, 1, 3)
        }
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            int w = (apw + i) & (WD_APN - 1);
            float vl = yl[i], vr = yr[i], a0, a1;
            WD_AP(vl, 0, 0, 0) WD_AP(vr, 4, 1, 0)
            WD_AP(vl, 1, 0, 1) WD_AP(vr, 5, 1, 1)
            WD_AP(vl, 2, 0, 2) WD_AP(vr, 6, 1, 2)
            WD_AP(vl, 3, 0, 3) WD_AP(vr, 7, 1, 3)
            yl[i] = vl; yr[i] = vr;
            cv[i] = vl * WD_TO16; cv[i + 8] = vr * WD_TO16;
            a0 = (vl < 0.0f) ? -vl : vl; a1 = (vr < 0.0f) ? -vr : vr;
            a0 = (a1 > a0) ? a1 : a0;
            bigm = (a0 > bigm) ? a0 : bigm;
        }
#undef WD_TAP
#undef WD_AP
    }
    /* the ring stores in a loop of their own: above they chain every sample to the last */
    WD_EIGHT
    for (i = 0; i < 8; i++) {
        int wpk = wp0 + i;
        wpk = (wpk >= WD_N) ? wpk - WD_N : wpk;
        sb[wpk] = za_s14((int)cv[i]);
        sb[WD_N + wpk] = za_s14((int)cv[i + 8]);
    }
    bigm *= WD_TO16;
    s->quiet = (bigm >= WD_QUIET || sg > 0.0f) ? 0 : ((s->quiet < 100000000) ? s->quiet + 8 : s->quiet);

    {
        float * restrict ob = buf;
        WD_EIGHT
        for (i = 0; i < 8; i++) {
            ob[i] = dgv[i] * xs[0][i] + yl[i];
            ob[i + 8] = dgv[i] * xs[1][i] + yr[i];
        }
    }
    wp0 += 8;
    s->wp = (wp0 >= WD_N) ? wp0 - WD_N : wp0;
    s->apw = (apw + 8) & (WD_APN - 1);
    s->sg = sg; s->dCur = dCur; s->xf = xf;
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
    float r[4], wt;
    int k, chg = 0;

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

    za_unseal(s, ZA_HDR(WdState), s->seal);
    if (s->magic != WD_MAGIC) wd_init(s);
    r[0] = params[WASHOUT_WASH_SLOT]; r[1] = params[WASHOUT_TONE_SLOT]; r[2] = params[WASHOUT_SIZE_SLOT];
    r[3] = (params[0] >= 0.5f) ? 1.0f : 0.0f;
    for (k = 0; k < 4; k++) if (r[k] != s->rk[k]) { chg = 1; s->rk[k] = r[k]; }
    wt = wd_ui(r[0], (float)WASHOUT_WASH_UI_DEFAULT, 100.0f) * 0.01f;
    if (s->ws < 0.0f) s->ws = wt;
    if (s->ws != wt) {                           /* the macro glides: no zipper, no jumps in the loop */
        s->ws += (wt - s->ws) * WD_W_RATE;
        if (s->ws - wt < 1e-4f && wt - s->ws < 1e-4f) s->ws = wt;
        chg = 1;
    }
    if (chg) wd_prepare(&s->P, s->ws, wd_ui(r[1], (float)WASHOUT_TONE_UI_DEFAULT, 100.0f),
                        wd_ui(r[2], (float)WASHOUT_SIZE_UI_DEFAULT, 100.0f), r[3] != 0.0f);
    if (wd_clearing(s)) goto done;               /* first ~7 ms after loading: dry */
    wd_process(s, &s->P, fxBuf);                 /* L and R in place */
done:
    za_seal(s, ZA_HDR(WdState), s->seal);        /* leave the arena safe (MIDISTOMP #60) */
}

#endif /* WASHOUT_HOST_TEST */
