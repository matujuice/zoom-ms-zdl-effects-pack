/*
 * djfilter.c - "DJFilter": a DJ mixer filter (Xone / DJM style) with a kick kill, linked stereo
 *
 * The static filter you ride by hand, by CC or in Perform. Spec: docs/NEW-EFFECTS.md, DJFILTER.
 *
 * KNOBS (screen values)
 *   0 DJ    0..200   shown -100 .. 0 .. +100: left = low-pass, right = high-pass, -3..+3 flat
 *   1 Reso  0..100   resonance at the far end of the sweep (default 30)
 *   2 Kill  0..281   OFF, then 20 .. 300 Hz: an independent 24 dB/oct low cut (default OFF)
 *   3 Slope 0..1     12 / 24 dB per octave for the DJ filter (default 24)
 *   4 Glide 0..100   0 = instant, 1..100 = 1 .. 1000 ms (log), smooths DJ (default 44 = 20 ms)
 *   5 Level 0..36    -12 .. +6 dB in 0.5 dB steps (default 24 = 0 dB)
 *   6 Mix   0..100   DJ crossfade (dry min(1,2-2m), wet min(1,2m)), default 100
 *
 * HOW IT WORKS (all coefficient maths once per 8-sample block, then plain sample loops)
 *   Amount: DJ counts from the edge of its flat zone, (|DJ| - 3) / 97, signed (below 0 low-pass,
 *   above 0 high-pass), smoothed once per block by Glide (a one-pole, so a jump sweeps; a jump
 *   from left to right sweeps through open).
 *   Sweep: LP cutoff 19 kHz * 2^(-8.89 a) (a = 1 gives 40 Hz), HP cutoff 20 Hz * 500^a (a = 1
 *   gives 10 kHz): exponential, turning away from the middle is always more filtering.
 *   Filters: zero-delay-feedback state-variable stages (as in Sweep). 24 dB/oct is two stages,
 *   12 dB/oct one (the second passes through). Resonance sits in the first stage only: Q from
 *   0.71 (open) up to 5 at the far end, k = 1.414 * 2^(-2.82 * Reso * a), so it is about none
 *   when open and Reso sets what it reaches. k stays above 0.28, so the filter cannot
 *   self-oscillate. The second stage is Q 0.71, so with no resonance 24 dB/oct is a flat
 *   Linkwitz-Riley (-6 dB at the cutoff). Kill: two Q 0.71 high-pass stages at its own frequency.
 *   CPU: each pair of stages runs both sides in one loop (four chains side by side keep the DSP's
 *   pipeline full). The DJ filter at 0 and Kill OFF are skipped (zero cost); a restarted stage
 *   (or LP <-> HP) has its memory fitted to the signal first, so it never clicks.
 *   Footswitch: the switch-over is a gain ramp over 128 samples (2.9 ms). While it runs the
 *   effect keeps processing, so both on and off fade. Fully off, the input passes untouched.
 *   Output = Level * (dry * input + wet * filtered), then the footswitch ramp. Nothing active,
 *   Level 0 dB and Mix 100: the whole block is skipped.
 *   Pedal-safe rules (docs/SAFE-DSP-RULES.md): no libm, no division, no switch, no static
 *   arrays, no double. The reciprocals are Newton steps, tan() a short series on the half angle.
 *
 * State: 4 stages x 2 sides x 2 memories, the smoothed amount, the ramp. In the arena,
 * validated by a magic number (change it when DfState changes).
 */

#include <stdint.h>
#include "../common/zarena.h"

#ifdef __TI_COMPILER_VERSION__
#define DF_DO_PRAGMA(x) _Pragma(#x)
#define DF_EXPAND_PRAGMA(x) DF_DO_PRAGMA(x)
#define DF_ALWAYS_INLINE(fn) DF_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define DF_CODE_SECTION(fn) DF_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define DF_ONE_COPY(fn) DF_EXPAND_PRAGMA(FUNC_CANNOT_INLINE(fn)) DF_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define DF_BLOCK_LOOP() DF_DO_PRAGMA(MUST_ITERATE(8, 8, 8)) DF_DO_PRAGMA(UNROLL(1))
#else
#define DF_ALWAYS_INLINE(fn)
#define DF_CODE_SECTION(fn)
#define DF_BLOCK_LOOP()
#define DF_ONE_COPY(fn)
#endif

#define DF_MAGIC   0x44463034u              /* "DF04": change when DfState changes */
#define DF_K0      1.4142136f               /* 1/Q for Q = 0.7071 (Butterworth)    */
#define DF_LP_TOP  19000.0f
#define DF_LP_OCT  8.89f                    /* octaves from 19 kHz down to 40 Hz   */
#define DF_HP_LOW  20.0f
#define DF_HP_OCT  8.9658f                  /* log2(500): 20 Hz up to 10 kHz       */
#define DF_FADE    0.0625f                  /* footswitch ramp per block: 128 samples */

typedef struct {
    float a1, a2, a3;      /* state-variable stage update                         */
    float co, cb, cl;      /* output = co * x + cb * v1 + cl * v2                 */
    float fresh0;          /* 1.0: a restarted stage's low-pass memory starts at the input, 0.0: at 0 */
} DfC;

typedef struct {
    DfC c[3];              /* DJ filter stage 1, stage 2 (passes through at 12 dB), Kill */
    unsigned int on;       /* bit 0 DJ filter, 1 its second stage, 2 Kill, 4 the DJ filter is a high-pass */
    float dg, wg;          /* dry and wet gain (with Level)                       */
} DfParams;

/* The knob maths runs only when a knob or the glided amount moved: on the pedal every byte of code a
 * block runs costs about a cycle (it comes from slow memory each block), so still knobs cost nothing. */
typedef struct {
    unsigned int magic;
    float a;               /* smoothed DJ amount: -1 (LP closed) .. 0 (open) .. +1 (HP closed) */
    float e;               /* footswitch ramp 0 (off) .. 1 (on)                   */
    unsigned int act;      /* last block's DfParams.on                            */
    float ic[16];          /* [pair * 8 + stage * 4 + side * 2 + 0/1]: pair 0 the DJ filter, 1 Kill */
    float rk[7];           /* the knobs' raw values u and gl come from            */
    float u[7];            /* screen values: DJ, Reso, Kill, Slope, Glide, Level, Mix */
    float gl;              /* Glide's per-block step                              */
    float pa;              /* the amount P was worked out for                     */
    DfParams P;
    unsigned int seal[ZA_NSEAL(64)]; /* every word above, bits 30, 29 between blocks (zarena.h) */
} DfState;
ZA_CHECK(DfState);

/* knobs arrive as (screen value) / 100; convert back like the rest of the pack */
DF_ALWAYS_INLINE(df_ui)
static inline float df_ui(float raw, float def_ui, float max_ui)
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

/* 1 / x for x > 0: a first guess from the float bits, then three Newton steps (no divide) */
DF_ALWAYS_INLINE(df_recip)
static inline float df_recip(float x)
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

/* 2^x for x in -20..20: whole part from the exponent bits, the rest by a polynomial (error ~2e-6) */
DF_ALWAYS_INLINE(df_exp2)
static inline float df_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int n;
    float f, e, r;
    if (x > 20.0f) x = 20.0f;
    if (x < -20.0f) x = -20.0f;
    n = (int)(x + 32.5f) - 32;
    f = x - (float)n;
    e = f * 0.6931472f;
    r = 1.0f + e * (1.0f + e * (0.5f + e * (0.16666667f + e * (0.041666668f + e * 0.008333334f))));
    c.u = (unsigned int)(n + 127) << 23;
    return r * c.f;
}

/* g = tan(pi f / 44100) for f up to 19 kHz, from t = tan(pi f / 88200) (series to w^7, w <= 0.68)
 * and tan(2a) = 2 t / (1 - t^2). Error under 0.5 %. */
DF_ALWAYS_INLINE(df_g)
static inline float df_g(float f)
{
    float w = f * 3.5617e-5f, w2 = w * w, t;
    t = w * (1.0f + w2 * (0.33333334f + w2 * (0.13333334f + w2 * 0.053968254f)));
    return 2.0f * t * df_recip(1.0f - t * t);
}

/* One stage's coefficients from g and k. hp = 0: low-pass (output v2), 1: high-pass (x - k v1 - v2). */
DF_ALWAYS_INLINE(df_coef)
static inline void df_coef(DfC *c, float g, float k, int hp)
{
    float a1 = df_recip(1.0f + g * (g + k));
    c->a1 = a1; c->a2 = g * a1; c->a3 = g * c->a2;
    c->co = hp ? 1.0f : 0.0f; c->cb = hp ? -k : 0.0f; c->cl = hp ? -1.0f : 1.0f;
    c->fresh0 = hp ? 0.0f : 1.0f;
}

/* Two stages in series (A then B) over both sides' 8 samples, in place, in one loop. fa / fb: that
 * stage was not running, so fit its memory to the signal first (a low-pass settled on the input, a
 * high-pass with nothing in its low-pass). ic: A L, A R, B L, B R, two memories each. */
#define DF_SVF(c, m1, m2, in, out) { float xx = in, v3 = xx - m2, v1 = c##1 * m1 + c##2 * v3, \
    v2 = m2 + c##2 * m1 + c##3 * v3; m1 = 2.0f * v1 - m1; m2 = 2.0f * v2 - m2; out = c##o * xx + c##b * v1 + c##l * v2; }
#define DF_FLUSH(v) if (v < 1e-15f && v > -1e-15f) v = 0.0f;
/* One copy for both filters (not inlined twice): code a block runs costs ~1 cycle per byte on the pedal. */
DF_ONE_COPY(df_pair)
static void df_pair(float *restrict bf, float *restrict ic, const DfC *A, const DfC *B,
                           unsigned int fa, unsigned int fb)
{
    int i;
    const float a1 = A->a1, a2 = A->a2, a3 = A->a3, ao = A->co, ab = A->cb, al = A->cl;
    const float b1 = B->a1, b2 = B->a2, b3 = B->a3, bo = B->co, bb = B->cb, bl = B->cl;
    float p1 = ic[0], p2 = ic[1], q1 = ic[2], q2 = ic[3], r1 = ic[4], r2 = ic[5], s1 = ic[6], s2 = ic[7];
    if (fa) { p1 = 0.0f; p2 = A->fresh0 * bf[0]; q1 = 0.0f; q2 = A->fresh0 * bf[8]; }
    if (fb) { r1 = 0.0f; r2 = B->fresh0 * bf[0]; s1 = 0.0f; s2 = B->fresh0 * bf[8]; }
    DF_BLOCK_LOOP()
    for (i = 0; i < 8; i++) {
        float yl, yr, zl, zr;
        DF_SVF(a, p1, p2, bf[i], yl) DF_SVF(a, q1, q2, bf[i + 8], yr)
        DF_SVF(b, r1, r2, yl, zl)    DF_SVF(b, s1, s2, yr, zr)
        bf[i] = zl; bf[i + 8] = zr;
    }
    DF_FLUSH(p1) DF_FLUSH(p2) DF_FLUSH(q1) DF_FLUSH(q2) DF_FLUSH(r1) DF_FLUSH(r2) DF_FLUSH(s1) DF_FLUSH(s2)
    ic[0] = p1; ic[1] = p2; ic[2] = q1; ic[3] = q2; ic[4] = r1; ic[5] = r2; ic[6] = s1; ic[7] = s2;
}
#undef DF_SVF
#undef DF_FLUSH

DF_ALWAYS_INLINE(df_init)
static inline void df_init(DfState *s)
{
    unsigned int i;
    s->a = 0.0f; s->e = 0.0f; s->act = 0u;
    for (i = 0u; i < 16u; i++) s->ic[i] = 0.0f;
    for (i = 0u; i < 7u; i++) s->rk[i] = -1.0f;   /* no knob is -1: the first block works everything out */
    s->pa = 2.0f;
    s->magic = DF_MAGIC;
}

/* Glide: move the smoothed amount towards the knob. Every block (cheap), even while the effect is
 * off, so the amount is where the knob is when it is switched on. */
DF_ALWAYS_INLINE(df_smooth)
static inline void df_smooth(DfState *s)
{
    float dj = s->u[0] - 100.0f, t = 0.0f;
    if (dj < -3.0f) t = (dj + 3.0f) * 0.010309278f;
    else if (dj > 3.0f) t = (dj - 3.0f) * 0.010309278f;
    s->a += s->gl * (t - s->a);
    if (s->a - t < 0.0001f && t - s->a < 0.0001f) s->a = t;   /* land: a still amount skips the filter maths */
}

DF_ALWAYS_INLINE(df_prepare)
static inline void df_prepare(DfParams *P, const float *u, float a)
{
    float reso = u[1] * 0.01f, m = u[6] * 0.01f, lvl, dryG, wetG, g;
    int s24 = (u[3] > 0.5f), hp = (a > 0.0f);
    unsigned int mask = 0u;

    if (a != 0.0f) {
        if (!hp) a = -a;
        g = hp ? df_g(DF_HP_LOW * df_exp2(DF_HP_OCT * a)) : df_g(DF_LP_TOP * df_exp2(-DF_LP_OCT * a));
        df_coef(&P->c[0], g, DF_K0 * df_exp2(-2.82f * reso * a), hp);
        mask = hp ? 17u : 1u;
        if (s24) { df_coef(&P->c[1], g, DF_K0, hp); mask |= 2u; }
        else {                                   /* 12 dB: the second stage passes through */
            P->c[1].a1 = 0.0f; P->c[1].a2 = 0.0f; P->c[1].a3 = 0.0f;
            P->c[1].co = 1.0f; P->c[1].cb = 0.0f; P->c[1].cl = 0.0f; P->c[1].fresh0 = 0.0f;
        }
    }
    if (u[2] > 0.5f) {
        df_coef(&P->c[2], df_g(19.0f + u[2]), DF_K0, 1);   /* 1 -> 20 Hz ... 281 -> 300 Hz */
        mask |= 4u;
    }
    P->on = mask;

    lvl = df_exp2((u[5] - 24.0f) * 0.5f * 0.16609640f);
    dryG = 2.0f - 2.0f * m; if (dryG > 1.0f) dryG = 1.0f;
    wetG = 2.0f * m;        if (wetG > 1.0f) wetG = 1.0f;
    P->dg = lvl * dryG; P->wg = lvl * wetG;
}

DF_ALWAYS_INLINE(df_process)
static inline void df_process(DfState *s, const DfParams *P, float *buf, float e, float de)
{
    int i;
    unsigned int fresh = P->on & ~s->act;
    float d[16];
    const float dg = P->dg, wg = P->wg;
    const int plain = (dg == 0.0f && wg == 1.0f && e >= 1.0f && de == 0.0f);   /* Mix 100, 0 dB, on: no dry pass */
    if ((P->on ^ s->act) & 16u) fresh |= P->on & 3u;      /* LP <-> HP: the filter restarts */
    if (!plain) for (i = 0; i < 16; i++) d[i] = buf[i];
    if (P->on & 1u) df_pair(buf, s->ic, &P->c[0], &P->c[1], fresh & 1u, fresh & 2u);
    if (P->on & 4u) df_pair(buf, s->ic + 8, &P->c[2], &P->c[2], fresh & 4u, fresh & 4u);
    s->act = P->on;
    if (plain) return;
    DF_BLOCK_LOOP()
    for (i = 0; i < 8; i++) {
        float ol = dg * d[i] + wg * buf[i], orr = dg * d[i + 8] + wg * buf[i + 8];
        buf[i] = d[i] + e * (ol - d[i]);
        buf[i + 8] = d[i + 8] + e * (orr - d[i + 8]);
        e += de;
    }
}

/* ---- labels (the pedal screen) -------------------------------------------------------------- */
/* n in 0..9999 as text at out[len], no leading zeros; returns the new length */
static int df_num(unsigned int n, char *out, int len)
{
    int th = 0, h = 0, t = 0;
    while (n >= 1000u) { n -= 1000u; th++; }
    while (n >= 100u) { n -= 100u; h++; }
    while (n >= 10u) { n -= 10u; t++; }
    if (th > 0) { out[len] = (char)('0' + th); len++; }
    if (th > 0 || h > 0) { out[len] = (char)('0' + h); len++; }
    if (th > 0 || h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + (int)n); len++;
    out[len] = 0;
    return len;
}

/* knob 0 DJ: 0..200 -> "-100" .. "0" .. "+100" */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value > 200u) value = 200u;
    if (value < 100u) { out[0] = '-'; return df_num(100u - value, out, 1); }
    if (value == 100u) return df_num(0u, out, 0);
    out[0] = '+';
    return df_num(value - 100u, out, 1);
}

/* knob 2 Kill: 0 "OFF", 1..281 -> "20" .. "300" Hz */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return 3; }
    if (value > 281u) value = 281u;
    return df_num(19u + value, out, 0);
}

/* knob 3 Slope: 12 / 24 */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = '1'; out[1] = '2'; }
    else { out[0] = '2'; out[1] = '4'; }
    out[2] = 0;
    return 2;
}

/* knob 4 Glide: 0 "0", 1..100 -> 1 .. 1000 ms (log) */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value == 0u) return df_num(0u, out, 0);
    if (value > 100u) value = 100u;
    return df_num((unsigned int)(int)(df_exp2(((float)(int)value - 1.0f) * 0.1006693f) + 0.5f), out, 0);
}

/* knob 5 Level: 0..36 -> "-12.0" .. "0.0" .. "+6.0" dB */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    unsigned int t;
    int h = 0, len = 0;
    if (value > 36u) value = 36u;
    if (value < 24u) { t = (24u - value) * 5u; out[0] = '-'; len = 1; }
    else { t = (value - 24u) * 5u; if (t > 0u) { out[0] = '+'; len = 1; } }
    while (t >= 10u) { t -= 10u; h++; }
    len = df_num((unsigned int)h, out, len);
    out[len] = '.'; out[len + 1] = (char)('0' + (int)t); out[len + 2] = 0;
    return len + 2;
}

#ifndef DJFILTER_HOST_TEST

#include "djfilter_params.h"

#ifndef DJFILTER_AUDIO_FUNC
#define DJFILTER_AUDIO_FUNC Fx_DLY_DJFilter
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

DF_CODE_SECTION(DJFILTER_AUDIO_FUNC)
void DJFILTER_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    DfState *s;
    float r[7], e0, de;
    int on, k, chg = 0;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(DfState) || span < (end - base)) return;
    if (stateBase + sizeof(DfState) > end) return;

    s = (DfState *)stateBase;
    za_unseal(s, ZA_HDR(DfState), s->seal);
    if (s->magic != DF_MAGIC) df_init(s);

    r[0] = params[DJFILTER_DJ_SLOT];    r[1] = params[DJFILTER_RESO_SLOT];
    r[2] = params[DJFILTER_KILL_SLOT];  r[3] = params[DJFILTER_SLOPE_SLOT];
    r[4] = params[DJFILTER_GLIDE_SLOT]; r[5] = params[DJFILTER_LEVEL_SLOT];
    r[6] = params[DJFILTER_MIX_SLOT];
    for (k = 0; k < 7; k++) if (r[k] != s->rk[k]) chg = 1;
    if (chg) {
        float *u = s->u;
        u[0] = df_ui(r[0], (float)DJFILTER_DJ_UI_DEFAULT,    200.0f);
        u[1] = df_ui(r[1], (float)DJFILTER_RESO_UI_DEFAULT,  100.0f);
        u[2] = df_ui(r[2], (float)DJFILTER_KILL_UI_DEFAULT,  281.0f);
        u[3] = df_ui(r[3], (float)DJFILTER_SLOPE_UI_DEFAULT, 1.0f);
        u[4] = df_ui(r[4], (float)DJFILTER_GLIDE_UI_DEFAULT, 100.0f);
        u[5] = df_ui(r[5], (float)DJFILTER_LEVEL_UI_DEFAULT, 36.0f);
        u[6] = df_ui(r[6], (float)DJFILTER_MIX_UI_DEFAULT,   100.0f);
        s->gl = (u[4] < 0.5f) ? 1.0f
              : 1.0f - df_exp2(-0.2617f * df_recip(df_exp2((u[4] - 1.0f) * 0.1006693f)));
        for (k = 0; k < 7; k++) s->rk[k] = r[k];
    }

    on = (params[0] >= 0.5f);
    df_smooth(s);
    if (!on && s->e <= 0.0f) {                   /* fully off: input untouched */
        s->act = 0u;
        goto done;
    }
    e0 = s->e;                                   /* footswitch ramp: this block's start and step */
    if (on) { s->e += DF_FADE; if (s->e > 1.0f) s->e = 1.0f; }
    else    { s->e -= DF_FADE; if (s->e < 0.0f) s->e = 0.0f; }
    de = (s->e - e0) * 0.125f;
    if (chg || s->a != s->pa) { df_prepare(&s->P, s->u, s->a); s->pa = s->a; }
    if (s->P.on == 0u && s->act == 0u && e0 >= 1.0f && de == 0.0f &&
        s->P.dg + s->P.wg > 0.9999f && s->P.dg + s->P.wg < 1.0001f)
        goto done;                               /* nothing to do: skipped entirely */
    df_process(s, &s->P, fxBuf, e0, de);
done:
    za_seal(s, ZA_HDR(DfState), s->seal);        /* leave the arena safe (MIDISTOMP #60) */
}

#endif /* DJFILTER_HOST_TEST */
