/*
 * Takeoff - risers and downlifters (Zoom MS, custom ZDL). It only ADDS sound on top, like DubSiren:
 * the input passes through in stereo, never touched (no Mix knob, Vol instead).
 *
 *   input ------------------------------------------------------------------+
 *   noise --> 2 resonant filters (L, R, sweep) --+                          v
 *   3 detuned saws (L / R spread) ---------------+--> x swell x Vol --> (+) --> out
 *   sine ----------------------------------------+        |                ^
 *                                                          +--> Space: 4 all-pass diffusers
 *                                                               in a feedback ring (reverb)
 *   One position x runs 0..1 (UP) or 1..0 (DOWN). The frequency is f0 * 2^(Range * x) (exponential),
 *   the volume swells as x^2, the stereo width grows with x (0.2 .. 1).
 *   f0 = the key note in the octave C1..B1 (FREE: 50 Hz). Saws play an octave up, the noise
 *   filter sits at 8 x f0, so the sweep starts and ends on the note. SINE + DOWN + Range 1 = sub drop.
 *   UP cuts at the top (a 1.5 ms fade, no click); DOWN falls to silence by itself.
 *
 * KNOBS (screen values, param order)
 *   0 Dir    UP (default) / DOWN
 *   1 Len    0..4 = 1/2/4/8/16 bars at the pedal's tempo (zmt.h), 5..35 = FREE, 1..31 seconds. Default 2 = 4 bars
 *   2 Trig   PLAY (default) / MAN
 *   3 Sound  NOISE / SAW / N+SAW / SINE / N+SIN
 *   4 Range  0..3 = 1..4 octaves (default 2 = 3 octaves)
 *   5 Key    AUTO (default) / FREE / C .. B
 *   6 Space  reverb tail on the riser's own sound, 0 = off (default)
 *   7 Pos    manual position 0..100 (MAN only), smoothed (about 10 ms)
 *   8 Vol    level of the added sound (default 50)
 *
 * TRIGGER (PLAY). The effect sees the on/off flag (passed while OFF, as DubSiren) and the firmware's
 *   "footswitch down" word 6 of the transport block (zmt.h). Any on/off flip starts a timed sweep over Len
 *   (UP cuts at the end); a flip while a sweep runs drops it early (a fast fade). If word 6 is still 1
 *   0.5 s after the flip it is a hold: the sweep goes on at Len speed, sits at its end if it gets there and
 *   drops when word 6 goes to 0. Every block the effect writes its params pointer (ctx[1]) into word 7
 *   (the hold claim; only while the transport block's magic is there, so stock firmware is never touched).
 *   Older MIDISTOMP (word 6 is 0): with HOLD FOR = MOMENTARY a hold still works, because the firmware flips
 *   the effect back on release and that flip drops the sweep; only "sit at the top" needs word 6. Stock
 *   firmware: holding opens the tuner, so press only. MIDI: an OFF during a sweep drops it (a momentary CC = hold).
 *   MAN: no automatic sweep; the on/off flag is ignored, Pos is the position.
 *
 * KEY AUTO: a listener (only while Key is AUTO) low-passes the input, decimates it by 8 (5512 Hz), runs 24
 *   Goertzel bins (C3..B4, 1024-sample windows = 186 ms) and folds them into a 12-note chroma that decays
 *   with a time constant of about 2.5 s (a few bars). The strongest note wins if it holds 30% of the
 *   chroma energy and the input is not silent; otherwise FREE. The note is latched when a sweep starts.
 *   Cost: about 24 x 3 flops per block plus two one-pole filters. Aliasing of bright harmonics is not
 *   removed beyond the two poles: AUTO is a guess, not a tuner.
 *
 * IDLE: nothing sounding and the Space tail died (32 blocks under -120 dB) = no synthesis, no reverb,
 *   nothing but the AUTO listener.
 * SPACE: reverb state is zeroed (1024 floats a block, riser still dry) when Space comes up from 0; 0 = off
 *   (turning it to 0 cuts a ringing tail).
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static arrays, no libm, no float or integer division, no
 * switch, no double. Host tests only; CPU never measured.
 */

#include <stdint.h>
#include "../common/zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define TK_DO_PRAGMA(x) _Pragma(#x)
#define TK_EXPAND_PRAGMA(x) TK_DO_PRAGMA(x)
#define TK_ALWAYS_INLINE(fn) TK_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define TK_CODE_SECTION(fn) TK_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define TK_ITER8 _Pragma("MUST_ITERATE(8,8,8)") _Pragma("UNROLL(1)")
#else
#define TK_ITER8
#define TK_ALWAYS_INLINE(fn)
#define TK_CODE_SECTION(fn)
#endif

#define TK_MAGIC     0x544B4F32u         /* "TKO2": change whenever TkState changes */
#define HZ_TO_INC    2.2675737e-5f       /* 1 / 44100 */
#define RV_N         2048                /* reverb ring per side */
#define RV_M         2047
#define AP_N         512
#define AP_M         511
#define RV_TOTAL     (2 * RV_N + 8 * AP_N)
#define CLR_CHUNK    1024
#define HOLD_SAMPLES 22050               /* 0.5 s */
#define LG_N         1024                /* listener window, in blocks */
#define NBIN         24
#define QUIET_BLOCKS 32
#define NOISE_GAIN   0.25f               /* bandpass output -> about +-0.5 at the top (tuned in tests/takeoff_sounds.c) */

enum { ST_IDLE = 0, ST_SWEEP, ST_HOLD, ST_DROP, ST_MAN };

typedef struct {
    unsigned int magic;
    int   rvClr, inited, prev_foot, state, holdT, chk, note, autoNote, quiet, apw, rw, lgn;
    unsigned int rngL, rngR;
    float x, g;
    float ph[3], sph;
    float svL[2], svR[2], lpv[2];
    float ls[3];                   /* listener: two low-passes, high-pass state (kick) */
    float gc[NBIN], gs1[NBIN], gs2[NBIN], chroma[12];
    ZtSync zt;
    float rv[RV_TOTAL];            /* 2 rings, then 2 x 4 all-pass lines */
} TkState;

typedef struct {
    int   act, noise, saw, sine, rv;
    float lvl, gT, w, ng, tg, svf;
    float inc0, inc1, inc2, sinc;
    float fb, wetG;
} TkParams;

TK_ALWAYS_INLINE(tk_ui)
static inline int tk_ui(float raw, int def)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = (float)def;
    else if (raw <= 3.0f) ui = raw * 100.0f;
    else ui = raw;
    return (int)(ui + 0.5f);
}

TK_ALWAYS_INLINE(tk_recip)
static inline float tk_recip(float x)
{
    union { float f; unsigned int u; } c;
    float y;
    c.f = x; c.u = 0x7EF311C7u - c.u; y = c.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y;
}

/* 2^-n for n 0..4 */
TK_ALWAYS_INLINE(tk_inv2n)
static inline float tk_inv2n(int n)
{
    union { float f; unsigned int u; } c;
    c.u = ((unsigned int)(127 - n)) << 23;
    return c.f;
}

/* 2^(semis/12), 5th-order Taylor, +-12 st */
TK_ALWAYS_INLINE(semis_to_ratio)
static inline float semis_to_ratio(float semis)
{
    float e  = semis * 0.05776227f;
    float e2 = e * e;
    return 1.0f + e + e2 * (0.5f + e * (0.16666667f
                 + e * (0.041666668f + e * 0.008333334f)));
}

/* 2^oct for oct in 0..7 */
TK_ALWAYS_INLINE(exp2_oct)
static inline float exp2_oct(float oct)
{
    int   n = (int)oct;
    float f = oct - (float)n;
    float r = semis_to_ratio(f * 12.0f - 6.0f) * 1.41421356f;
    for (; n > 0; n--) r += r;
    return r;
}

TK_ALWAYS_INLINE(tri_bi)
static inline float tri_bi(float ph)
{
    float q = ph + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
}

/* sin(2*pi*ph), ph in [0,1) */
TK_ALWAYS_INLINE(sine_of)
static inline float sine_of(float ph)
{
    float t = tri_bi(ph);
    float t2 = t * t;
    return t * (1.5706268f + t2 * (-0.6432292f + t2 * 0.0727102f));
}

/* all-pass lengths: L 113 163 241 335, R 131 157 251 313 (as Washout, no modulation) */
TK_ALWAYS_INLINE(ap_len)
static inline int ap_len(int c, int k)
{
    return c ? ((k == 0) ? 131 : (k == 1) ? 157 : (k == 2) ? 251 : 313)
             : ((k == 0) ? 113 : (k == 1) ? 163 : (k == 2) ? 241 : 335);
}

TK_ALWAYS_INLINE(tk_init)
static inline void tk_init(TkState *s)
{
    int k;
    s->rvClr = 0; s->inited = 0; s->prev_foot = 0; s->state = ST_IDLE; s->holdT = 0; s->chk = 0;
    s->note = -1; s->autoNote = -1; s->quiet = QUIET_BLOCKS; s->apw = 0; s->rw = 0; s->lgn = 0;
    s->rngL = 12345u; s->rngR = 987654321u;
    s->x = 0.0f; s->g = 0.0f;
    s->ph[0] = 0.0f; s->ph[1] = 0.33f; s->ph[2] = 0.66f; s->sph = 0.0f;
    for (k = 0; k < 2; k++) { s->svL[k] = 0.0f; s->svR[k] = 0.0f; s->lpv[k] = 0.0f; s->ls[k] = 0.0f; }
    s->ls[2] = 0.0f;
    for (k = 0; k < 12; k++) s->chroma[k] = 0.0f;
    for (k = 0; k < NBIN; k++) {                 /* Goertzel at C3 * 2^(k/12), fs 5512.5 Hz */
        int   m = (k >= 12) ? k - 12 : k;
        float hz = 130.8128f * semis_to_ratio((float)m);
        float th;
        if (k >= 12) hz += hz;
        th = hz * (8.0f * HZ_TO_INC);
        s->gc[k] = 2.0f * sine_of(0.25f + th);   /* 2 cos(2 pi th) */
        s->gs1[k] = 0.0f; s->gs2[k] = 0.0f;
    }
    zt_init(&s->zt);
    s->magic = TK_MAGIC;
}

/* once per block while Key is AUTO; buf = the input, L = [0..7], R = [8..15] */
TK_ALWAYS_INLINE(tk_listen)
static inline void tk_listen(TkState *s, const float *buf)
{
    int i, k;
    float a = s->ls[0], b = s->ls[1], h = s->ls[2], x;
    for (i = 0; i < 8; i++) {
        float m = buf[i] + buf[i + 8];
        h += 0.02f * (m - h);                    /* high-pass 140 Hz: a kick is not a note */
        a += 0.2f * (m - h - a);
        b += 0.2f * (a - b);
    }
    s->ls[0] = a; s->ls[1] = b; s->ls[2] = h;
    x = b * 0.5f;
    for (k = 0; k < NBIN; k++) {
        float s0 = x + s->gc[k] * s->gs1[k] - s->gs2[k];
        s->gs2[k] = s->gs1[k]; s->gs1[k] = s0;
    }
    if (++s->lgn >= LG_N) {
        float mx = 0.0f, tot = 0.0f;
        int   best = 0;
        for (k = 0; k < 12; k++) {
            int   j = k + 12;
            float e = (s->gs1[k] * s->gs1[k] + s->gs2[k] * s->gs2[k] - s->gc[k] * s->gs1[k] * s->gs2[k]
                     + s->gs1[j] * s->gs1[j] + s->gs2[j] * s->gs2[j] - s->gc[j] * s->gs1[j] * s->gs2[j]) * 3.8147e-6f;
            s->chroma[k] += (e - s->chroma[k]) * 0.07f;
            tot += s->chroma[k];
            if (s->chroma[k] > mx) { mx = s->chroma[k]; best = k; }
        }
        for (k = 0; k < NBIN; k++) { s->gs1[k] = 0.0f; s->gs2[k] = 0.0f; }
        s->lgn = 0;
        s->autoNote = (tot > 2.0e-6f && mx > 0.3f * tot) ? best : -1;
    }
}

/* once per block: knobs u[] (screen ints), foot = on/off flag, hold = footswitch down (word 6), bpm */
TK_ALWAYS_INLINE(tk_prepare)
static inline void tk_prepare(TkState *s, TkParams *P, const int *u, int foot, int hold, float bpm)
{
    int   up = (u[0] == 0), man = (u[2] == 1), snd = u[3], flip, noteNow, both;
    float dx, vol, fz, f, fc;

    if (!s->inited) { s->prev_foot = foot; s->inited = 1; }
    flip = (foot != s->prev_foot);
    s->prev_foot = foot;
    noteNow = (u[5] == 0) ? s->autoNote : (u[5] == 1) ? -1 : u[5] - 2;

    dx = (u[1] < 5) ? bpm * 7.5586e-7f * tk_inv2n(u[1]) : 1.8141e-4f * tk_recip((float)(u[1] - 4));

    if (man) {
        s->state = ST_MAN; s->chk = 0;
        s->x += ((float)u[7] * 0.01f - s->x) * 0.03f;
    } else {
        if (s->state == ST_MAN) s->state = ST_DROP;
        if (flip) {
            if (s->state == ST_SWEEP || s->state == ST_HOLD) { s->state = ST_DROP; s->chk = 0; }
            else { s->state = ST_SWEEP; s->x = up ? 0.0f : 1.0f; s->holdT = 0; s->chk = 1; s->note = noteNow; }
        }
        if (s->chk && s->state == ST_SWEEP) {
            s->holdT += 8;
            if (s->holdT >= HOLD_SAMPLES) { s->chk = 0; if (hold) s->state = ST_HOLD; }
        }
        if (s->state == ST_SWEEP || s->state == ST_HOLD) {
            s->x += up ? dx : -dx;
            if (up && s->x >= 1.0f) { s->x = 1.0f; if (s->state == ST_SWEEP) { s->state = ST_DROP; s->chk = 0; } }
            if (!up && s->x <= 0.0f) {
                s->x = 0.0f;
                if (s->state == ST_SWEEP) { s->state = ST_IDLE; s->g = 0.0f; s->chk = 0; }
            }
            if (s->state == ST_HOLD && !hold) s->state = ST_DROP;
        }
        if (s->state == ST_DROP && s->g <= 0.0f) { s->state = ST_IDLE; s->x = 0.0f; }
    }
    if (s->state == ST_IDLE || s->state == ST_MAN) s->note = noteNow;

    P->gT = (s->state == ST_SWEEP || s->state == ST_HOLD || s->state == ST_MAN) ? 1.0f : 0.0f;
    vol = 0.7f * (float)u[8] * 0.01f;
    P->lvl = s->x * s->x * vol;
    P->act = ((s->state != ST_IDLE || s->g > 0.0f) && P->lvl > 1.0e-7f);
    if (!P->act) s->g = P->gT;                   /* silent anyway: no ramp needed */

    P->noise = (snd == 0 || snd == 2 || snd == 4);
    P->saw   = (snd == 1 || snd == 2);
    P->sine  = (snd == 3 || snd == 4);
    both = (snd == 2 || snd == 4);
    P->ng = NOISE_GAIN * (both ? 0.7f : 1.0f);
    P->tg = (P->sine ? 0.7f : 0.5f) * (both ? 0.7f : 1.0f);

    if (P->act) {
        fz = (s->note >= 0) ? 32.703f * semis_to_ratio((float)s->note) : 50.0f;
        f  = fz * exp2_oct((float)(u[4] + 1) * s->x);
        P->sinc = f * HZ_TO_INC;
        P->inc1 = 2.0f * P->sinc; P->inc0 = P->inc1 * 0.994f; P->inc2 = P->inc1 * 1.006f;
        fc = 8.0f * f; if (fc > 6000.0f) fc = 6000.0f;
        P->svf = 2.0f * sine_of(0.5f * fc * HZ_TO_INC);
        P->w = 0.2f + 0.8f * s->x;
    }
    P->rv = (u[6] > 0);
    P->fb = 0.55f + 0.4f * (float)u[6] * 0.01f;
    P->wetG = 0.6f * (float)u[6] * 0.01f;
}

/* once per block: the riser (zero when not active), then the reverb, added on the input. n = 8 */
TK_ALWAYS_INLINE(tk_process)
static inline void tk_process(TkState *s, TkParams *P, float *restrict buf)
{
    int   i, c, k;
    float rl[8], rr[8], mb[8], sb[8], peak = 0.0f;

    for (i = 0; i < 8; i++) { rl[i] = 0.0f; rr[i] = 0.0f; }

    if (P->act) {
        float g = s->g, gs = P->gT > g ? 0.0078125f : -0.015625f;   /* up 3 ms, down 1.5 ms */
        for (i = 0; i < 8; i++) { mb[i] = 0.0f; sb[i] = 0.0f; }
        if (P->noise) {
            float f = P->svf, lL = s->svL[0], bL = s->svL[1], lR = s->svR[0], bR = s->svR[1];
            unsigned int rL = s->rngL, rR = s->rngR;
            TK_ITER8
            for (i = 0; i < 8; i++) {
                float nl, nr, hl, hr;
                rL = rL * 1664525u + 1013904223u; rR = rR * 22695477u + 1u;
                nl = (float)(int)rL * 4.656613e-10f; nr = (float)(int)rR * 4.656613e-10f;
                lL += f * bL; hl = nl - lL - 0.12f * bL; bL += f * hl;
                lR += f * bR; hr = nr - lR - 0.12f * bR; bR += f * hr;
                mb[i] = (bL + bR) * (0.5f * P->ng); sb[i] = (bL - bR) * (0.5f * P->ng);
            }
            s->svL[0] = lL; s->svL[1] = bL; s->svR[0] = lR; s->svR[1] = bR; s->rngL = rL; s->rngR = rR;
        }
        if (P->saw) {
            float p0 = s->ph[0], p1 = s->ph[1], p2 = s->ph[2];
            TK_ITER8
            for (i = 0; i < 8; i++) {
                float v0, v1, v2;
                p0 += P->inc0; p0 -= (p0 >= 1.0f) ? 1.0f : 0.0f;
                p1 += P->inc1; p1 -= (p1 >= 1.0f) ? 1.0f : 0.0f;
                p2 += P->inc2; p2 -= (p2 >= 1.0f) ? 1.0f : 0.0f;
                v0 = p0 + p0 - 1.0f; v1 = p1 + p1 - 1.0f; v2 = p2 + p2 - 1.0f;
                mb[i] += (v0 + v1 + v2) * (0.3333333f * P->tg);
                sb[i] += (v2 - v0) * (0.5f * P->tg);
            }
            s->ph[0] = p0; s->ph[1] = p1; s->ph[2] = p2;
        }
        if (P->sine) {
            float p = s->sph;
            TK_ITER8
            for (i = 0; i < 8; i++) {
                p += P->sinc; p -= (p >= 1.0f) ? 1.0f : 0.0f;
                mb[i] += P->tg * sine_of(p);
            }
            s->sph = p;
        }
        TK_ITER8
        for (i = 0; i < 8; i++) {
            float a;
            g += gs; g = (g > 1.0f) ? 1.0f : g; g = (g < 0.0f) ? 0.0f : g;
            a = g * P->lvl;
            rl[i] = a * (mb[i] + P->w * sb[i]);
            rr[i] = a * (mb[i] - P->w * sb[i]);
        }
        s->g = g;
    }

    if (!P->rv) s->rvClr = 0;                    /* reverb off: its state goes stale */
    else if (s->rvClr < RV_TOTAL) {
        volatile float *rp = s->rv;
        int e = s->rvClr + CLR_CHUNK;
        for (i = s->rvClr; i < e; i++) rp[i] = 0.0f;
        s->rvClr = e;
    } else if (P->act || s->state != ST_IDLE || s->quiet < QUIET_BLOCKS) {
        int w0 = s->rw, a0 = s->apw;
        for (c = 0; c < 2; c++) {
            float *rg = s->rv + c * RV_N, *in = c ? rr : rl, y[8], lp = s->lpv[c];
            int   D = c ? 1877 : 1693;
            for (i = 0; i < 8; i++) y[i] = rg[(w0 + i - D) & RV_M];
            for (k = 0; k < 4; k++) {
                float *ab = s->rv + 2 * RV_N + (c * 4 + k) * AP_N;
                int   L = ap_len(c, k);
                for (i = 0; i < 8; i++) {
                    int   w = (a0 + i) & AP_M;
                    float d = ab[(w - L) & AP_M], wv = y[i] + 0.6f * d;
                    ab[w] = wv;
                    y[i] = d - 0.6f * wv;
                }
            }
            for (i = 0; i < 8; i++) {
                float ay = y[i] < 0.0f ? -y[i] : y[i];
                lp += 0.5f * (y[i] - lp);
                rg[(w0 + i) & RV_M] = in[i] + P->fb * lp;
                in[i] += P->wetG * y[i];         /* in[] = the riser, plus its own tail */
                peak = (ay > peak) ? ay : peak;
            }
            s->lpv[c] = lp;
        }
        s->rw = (w0 + 8) & RV_M; s->apw = (a0 + 8) & AP_M;
        s->quiet = (P->act || s->state != ST_IDLE || peak > 1.0e-6f) ? 0 : s->quiet + 1;
    }

    if (P->act || P->rv) {
        for (i = 0; i < 8; i++) { buf[i] = buf[i] + rl[i]; buf[i + 8] = buf[i + 8] + rr[i]; }
    }
}

/* ---- on-screen text (knob index = ZDL_GetLabel_<index>) ----------------- */
TK_ALWAYS_INLINE(tk_put)
static inline int tk_put(char *out, int c0, int c1, int c2, int c3, int c4)
{
    int len = 1;
    out[0] = (char)c0; out[1] = (char)c1; out[2] = (char)c2; out[3] = (char)c3; out[4] = (char)c4;
    if (c1) len = 2;
    if (c2) len = 3;
    if (c3) len = 4;
    if (c4) len = 5;
    out[len] = 0;
    return len;
}

TK_ALWAYS_INLINE(tk_num)
static inline int tk_num(char *out, int n, int len)
{
    int h = 0, t = 0;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* 0 Dir: UP DOWN */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    return value == 0u ? tk_put(out, 'U', 'P', 0, 0, 0) : tk_put(out, 'D', 'O', 'W', 'N', 0);
}

/* 1 Len: 1bar 2bar 4bar 8bar 16bar, then 1s .. 31s */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int len;
    if (value > 35u) value = 35u;
    if (value == 0u) return tk_put(out, '1', 'b', 'a', 'r', 0);
    if (value == 1u) return tk_put(out, '2', 'b', 'a', 'r', 0);
    if (value == 2u) return tk_put(out, '4', 'b', 'a', 'r', 0);
    if (value == 3u) return tk_put(out, '8', 'b', 'a', 'r', 0);
    if (value == 4u) return tk_put(out, '1', '6', 'b', 'a', 'r');
    len = tk_num(out, (int)value - 4, 0);
    out[len] = 's'; len++; out[len] = 0;
    return len;
}

/* 2 Trig: PLAY MAN */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    return value == 0u ? tk_put(out, 'P', 'L', 'A', 'Y', 0) : tk_put(out, 'M', 'A', 'N', 0, 0);
}

/* 3 Sound: NOISE SAW N+SAW SINE N+SIN */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value == 0u) return tk_put(out, 'N', 'O', 'I', 'S', 'E');
    if (value == 1u) return tk_put(out, 'S', 'A', 'W', 0, 0);
    if (value == 2u) return tk_put(out, 'N', '+', 'S', 'A', 'W');
    if (value == 3u) return tk_put(out, 'S', 'I', 'N', 'E', 0);
    return tk_put(out, 'N', '+', 'S', 'I', 'N');
}

/* 4 Range: 1oct .. 4oct */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value > 3u) value = 3u;
    return tk_put(out, '1' + (int)value, 'o', 'c', 't', 0);
}

/* 5 Key: AUTO FREE C C# D D# E F F# G G# A A# B */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    int n;
    if (value == 0u) return tk_put(out, 'A', 'U', 'T', 'O', 0);
    if (value == 1u) return tk_put(out, 'F', 'R', 'E', 'E', 0);
    n = (int)value - 2;
    if (n > 11) n = 11;
    return tk_put(out,
        (n == 0 || n == 1) ? 'C' : (n == 2 || n == 3) ? 'D' : (n == 4) ? 'E' : (n == 5 || n == 6) ? 'F'
        : (n == 7 || n == 8) ? 'G' : (n == 9 || n == 10) ? 'A' : 'B',
        (n == 1 || n == 3 || n == 6 || n == 8 || n == 10) ? '#' : 0, 0, 0, 0);
}

/* 6 Space: Off, then 1..100 */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    if (value == 0u) return tk_put(out, 'O', 'f', 'f', 0, 0);
    if (value > 100u) value = 100u;
    return tk_num(out, (int)value, 0);
}

/* ---- pedal entry point (same structure as DubSiren) --------------------- */
#ifndef TAKEOFF_HOST_TEST

#include "takeoff_params.h"

#ifndef TAKEOFF_AUDIO_FUNC
#define TAKEOFF_AUDIO_FUNC Fx_DLY_Takeoff
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

TK_CODE_SECTION(TAKEOFF_AUDIO_FUNC)
void TAKEOFF_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc, *zw;
    uintptr_t base, end, stateBase;
    unsigned int span;
    TkState *s;
    TkParams P;
    int u[9], foot, hold = 0, run, start;
    float beats, bpm;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    foot = (params[0] >= 0.5f);                  /* the on/off flag (passed while OFF) */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(TkState) || span < (end - base)) return;
    if (stateBase + sizeof(TkState) > end) return;

    s = (TkState *)stateBase;
    if (s->magic != TK_MAGIC) tk_init(s);

    u[0] = tk_ui(params[TAKEOFF_DIR_SLOT],   TAKEOFF_DIR_UI_DEFAULT);
    u[1] = tk_ui(params[TAKEOFF_LEN_SLOT],   TAKEOFF_LEN_UI_DEFAULT);
    u[2] = tk_ui(params[TAKEOFF_TRIG_SLOT],  TAKEOFF_TRIG_UI_DEFAULT);
    u[3] = tk_ui(params[TAKEOFF_SOUND_SLOT], TAKEOFF_SOUND_UI_DEFAULT);
    u[4] = tk_ui(params[TAKEOFF_RANGE_SLOT], TAKEOFF_RANGE_UI_DEFAULT);
    u[5] = tk_ui(params[TAKEOFF_KEY_SLOT],   TAKEOFF_KEY_UI_DEFAULT);
    u[6] = tk_ui(params[TAKEOFF_SPACE_SLOT], TAKEOFF_SPACE_UI_DEFAULT);
    u[7] = tk_ui(params[TAKEOFF_POS_SLOT],   TAKEOFF_POS_UI_DEFAULT);
    u[8] = tk_ui(params[TAKEOFF_VOL_SLOT],   TAKEOFF_VOL_UI_DEFAULT);
    if (u[0] > 1) u[0] = 1;
    if (u[1] > 35) u[1] = 35;
    if (u[2] > 1) u[2] = 1;
    if (u[3] > 4) u[3] = 4;
    if (u[4] > 3) u[4] = 3;
    if (u[5] > 13) u[5] = 13;
    if (u[6] > 100) u[6] = 100;
    if (u[7] > 100) u[7] = 100;
    if (u[8] > 100) u[8] = 100;

    bpm = zt_update(&s->zt, &run, &beats, &start);
    zw = (volatile unsigned int *)ZT_ADDR;
    if (zw[0] == ZT_MAGIC) {                     /* MOD firmware: footswitch down (6), our hold claim (7) */
        hold = (zw[6] == 1u);
        zw[7] = ctx[1];
    }

    if (u[5] == 0) tk_listen(s, fxBuf);          /* before the riser is added */
    tk_prepare(s, &P, u, foot, hold, bpm);
    tk_process(s, &P, fxBuf);
}

#endif /* TAKEOFF_HOST_TEST */
