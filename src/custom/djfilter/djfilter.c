/*
 * djfilter.c - "DJFilter": a DJ mixer filter (Xone / DJM style) with a kick kill, linked stereo
 *
 * The static filter you ride by hand, by CC or in Perform. Spec: docs/NEW-EFFECTS.md, DJFILTER.
 *
 * KNOBS (screen values)
 *   0 LP    0..100   low-pass amount, 0 = off (skipped), 100 = closes down to about 40 Hz
 *   1 HP    0..100   high-pass amount, 0 = off (skipped), 100 = closes up to about 10 kHz
 *   2 DJ    0..200   shown -100 .. 0 .. +100: left = low-pass, right = high-pass, -3..+3 flat
 *   3 Reso  0..100   resonance at the far end of the sweep (default 30)
 *   4 Kill  0..281   OFF, then 20 .. 300 Hz: an independent 24 dB/oct low cut (default OFF)
 *   5 Slope 0..1     12 / 24 dB per octave for LP and HP (default 24)
 *   6 Glide 0..100   0 = instant, 1..100 = 1 .. 1000 ms (log), smooths LP / HP / DJ (default 44 = 20 ms)
 *   7 Level 0..36    -12 .. +6 dB in 0.5 dB steps (default 24 = 0 dB)
 *   8 Mix   0..100   DJ crossfade (dry min(1,2-2m), wet min(1,2m)), default 100
 *
 * HOW IT WORKS (all coefficient maths once per 8-sample block, then plain sample loops)
 *   Amounts: LP amount = max(LP, DJ below 0), HP amount = max(HP, DJ above 0), each 0..1. DJ
 *   counts from the edge of its flat zone, (|DJ| - 3) / 97, so it starts smoothly. The amounts
 *   are smoothed once per block by Glide (a one-pole, in octaves, so a jump sweeps).
 *   Sweep: LP cutoff 19 kHz * 2^(-8.89 a) (a = 1 gives 40 Hz), HP cutoff 20 Hz * 500^a (a = 1
 *   gives 10 kHz): exponential, turning right is always more filtering.
 *   Filters: zero-delay-feedback state-variable stages (as in Sweep). 24 dB/oct is two stages,
 *   12 dB/oct one. Resonance sits in the first stage only: Q from 0.71 (open) up to 5 at the
 *   far end, k = 1.414 * 2^(-2.82 * Reso * a), so it is about none when open and Reso sets what
 *   it reaches. k stays above 0.28, so the filter cannot self-oscillate. The second stage is
 *   Q 0.71, so with no resonance 24 dB/oct is a flat Linkwitz-Riley (-6 dB at the cutoff).
 *   A stage whose amount is 0 is skipped (zero cost); when it starts again its memory is fitted
 *   to the signal, so it never clicks. Kill: two Q 0.71 high-pass stages (24 dB/oct) at its own
 *   frequency.
 *   Footswitch: the switch-over is a gain ramp over 128 samples (2.9 ms). While it runs the
 *   effect keeps processing, so both on and off fade. Fully off, the input passes untouched.
 *   Output = Level * (dry * input + wet * filtered), then the footswitch ramp. Nothing active,
 *   Level 0 dB and Mix 100: the whole block is skipped.
 *   Pedal-safe rules (docs/SAFE-DSP-RULES.md): no libm, no division, no switch, no static
 *   arrays, no double. The reciprocals are Newton steps, tan() a short series on the half angle.
 *
 * State: 6 stages x 2 sides x 2 memories, the smoothed amounts, the ramp. In the arena,
 * validated by a magic number (change it when DfState changes).
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define DF_DO_PRAGMA(x) _Pragma(#x)
#define DF_EXPAND_PRAGMA(x) DF_DO_PRAGMA(x)
#define DF_ALWAYS_INLINE(fn) DF_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define DF_CODE_SECTION(fn) DF_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define DF_BLOCK_LOOP() DF_DO_PRAGMA(MUST_ITERATE(8, 8, 8)) DF_DO_PRAGMA(UNROLL(1))
#else
#define DF_ALWAYS_INLINE(fn)
#define DF_CODE_SECTION(fn)
#define DF_BLOCK_LOOP()
#endif

#define DF_MAGIC   0x44463031u              /* "DF01": change when DfState changes */
#define DF_K0      1.4142136f               /* 1/Q for Q = 0.7071 (Butterworth)    */
#define DF_LP_TOP  19000.0f
#define DF_LP_OCT  8.89f                    /* octaves from 19 kHz down to 40 Hz   */
#define DF_HP_LOW  20.0f
#define DF_HP_OCT  8.9658f                  /* log2(500): 20 Hz up to 10 kHz       */
#define DF_FADE    0.0625f                  /* footswitch ramp per block: 128 samples */

typedef struct {
    unsigned int magic;
    float aL, aH;          /* smoothed LP / HP amount, 0..1                       */
    float e;               /* footswitch ramp 0 (off) .. 1 (on)                   */
    unsigned int act;      /* bit per stage: was it running last block            */
    float ic[24];          /* [side * 12 + stage * 2 + 0/1]: stage 0 LP1, 1 LP2, 2 HP1, 3 HP2, 4 K1, 5 K2 */
} DfState;

typedef struct {
    float a1, a2, a3;      /* state-variable stage update                         */
    float cx, cb, cl;      /* output = cx * x + cb * v1 + cl * v2                 */
    float fresh0;          /* 1.0: a restarted stage's low-pass memory starts at the input, 0.0: at 0 */
} DfC;

typedef struct {
    DfC c[6];
    unsigned int on;       /* bit per stage that runs this block                  */
    float dg, wg, e, de;   /* dry and wet gain (with Level), ramp start and step per sample */
} DfParams;

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
    c->cx = hp ? 1.0f : 0.0f; c->cb = hp ? -k : 0.0f; c->cl = hp ? -1.0f : 1.0f;
    c->fresh0 = hp ? 0.0f : 1.0f;
}

/* One stage over one side's 8 samples, in place. fresh: it was not running, so fit its memory to
 * the signal first (a low-pass settled on the input, a high-pass with nothing in its low-pass). */
DF_ALWAYS_INLINE(df_stage)
static inline void df_stage(float *restrict bf, float *restrict ic, const DfC *c, int fresh)
{
    int i;
    const float a1 = c->a1, a2 = c->a2, a3 = c->a3, cx = c->cx, cb = c->cb, cl = c->cl;
    float ic1 = ic[0], ic2 = ic[1];
    if (fresh) { ic1 = 0.0f; ic2 = c->fresh0 * bf[0]; }
    DF_BLOCK_LOOP()
    for (i = 0; i < 8; i++) {
        float x = bf[i], v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        bf[i] = cx * x + cb * v1 + cl * v2;
    }
    if (ic1 < 1e-15f && ic1 > -1e-15f) ic1 = 0.0f;
    if (ic2 < 1e-15f && ic2 > -1e-15f) ic2 = 0.0f;
    ic[0] = ic1; ic[1] = ic2;
}

DF_ALWAYS_INLINE(df_init)
static inline void df_init(DfState *s)
{
    unsigned int i;
    s->aL = 0.0f; s->aH = 0.0f; s->e = 0.0f; s->act = 0u;
    for (i = 0u; i < 24u; i++) s->ic[i] = 0.0f;
    s->magic = DF_MAGIC;
}

/* Glide: move the smoothed LP / HP amounts towards the knobs. Every block (cheap), even while the
 * effect is off, so the amounts are where the knobs are when it is switched on.
 * u: LP, HP, DJ, Reso, Kill, Slope, Glide, Level, Mix */
DF_ALWAYS_INLINE(df_smooth)
static inline void df_smooth(DfState *s, const float *u)
{
    float dj = u[2] - 100.0f, lpT = u[0] * 0.01f, hpT = u[1] * 0.01f, g, d;
    if (dj < 0.0f) { d = (-dj - 3.0f) * 0.010309278f; if (d > lpT) lpT = d; }
    else           { d = ( dj - 3.0f) * 0.010309278f; if (d > hpT) hpT = d; }
    if (u[6] < 0.5f) g = 1.0f;
    else g = 1.0f - df_exp2(-0.2617f * df_recip(df_exp2((u[6] - 1.0f) * 0.1006693f)));
    s->aL += g * (lpT - s->aL);
    s->aH += g * (hpT - s->aH);
    if (lpT == 0.0f && s->aL < 0.001f) s->aL = 0.0f;
    if (hpT == 0.0f && s->aH < 0.001f) s->aH = 0.0f;
}

DF_ALWAYS_INLINE(df_prepare)
static inline void df_prepare(DfState *s, DfParams *P, const float *u, int on)
{
    float reso = u[3] * 0.01f, m = u[8] * 0.01f, lvl, dryG, wetG, g;
    int s24 = (u[5] > 0.5f);
    unsigned int mask = 0u;

    P->e = s->e;                                 /* footswitch ramp: this block's start and step */
    if (on) { s->e += DF_FADE; if (s->e > 1.0f) s->e = 1.0f; }
    else    { s->e -= DF_FADE; if (s->e < 0.0f) s->e = 0.0f; }
    P->de = (s->e - P->e) * 0.125f;

    if (s->aL > 0.0f) {
        g = df_g(DF_LP_TOP * df_exp2(-DF_LP_OCT * s->aL));
        df_coef(&P->c[0], g, DF_K0 * df_exp2(-2.82f * reso * s->aL), 0);
        mask |= 1u;
        if (s24) { df_coef(&P->c[1], g, DF_K0, 0); mask |= 2u; }
    }
    if (s->aH > 0.0f) {
        g = df_g(DF_HP_LOW * df_exp2(DF_HP_OCT * s->aH));
        df_coef(&P->c[2], g, DF_K0 * df_exp2(-2.82f * reso * s->aH), 1);
        mask |= 4u;
        if (s24) { df_coef(&P->c[3], g, DF_K0, 1); mask |= 8u; }
    }
    if (u[4] > 0.5f) {
        g = df_g(19.0f + u[4]);                  /* 1 -> 20 Hz ... 281 -> 300 Hz */
        df_coef(&P->c[4], g, DF_K0, 1);
        P->c[5] = P->c[4];
        mask |= 48u;
    }
    P->on = mask;

    lvl = df_exp2((u[7] - 24.0f) * 0.5f * 0.16609640f);
    dryG = 2.0f - 2.0f * m; if (dryG > 1.0f) dryG = 1.0f;
    wetG = 2.0f * m;        if (wetG > 1.0f) wetG = 1.0f;
    P->dg = lvl * dryG; P->wg = lvl * wetG;
}

DF_ALWAYS_INLINE(df_process)
static inline void df_process(DfState *s, const DfParams *P, float *buf)
{
    int c, i, k;
    unsigned int fresh = P->on & ~s->act;
    for (c = 0; c < 2; c++) {                    /* L, then R: own memory, same coefficients */
        float *restrict bf = buf + 8 * c;
        float d[8], e = P->e;
        for (i = 0; i < 8; i++) d[i] = bf[i];
        for (k = 0; k < 6; k++)
            if ((P->on >> k) & 1u)
                df_stage(bf, s->ic + c * 12 + k * 2, &P->c[k], (int)((fresh >> k) & 1u));
        DF_BLOCK_LOOP()
        for (i = 0; i < 8; i++) {
            float o = P->dg * d[i] + P->wg * bf[i];
            bf[i] = d[i] + e * (o - d[i]);
            e += P->de;
        }
    }
    s->act = P->on;
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

/* knob 2 DJ: 0..200 -> "-100" .. "0" .. "+100" */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value > 200u) value = 200u;
    if (value < 100u) { out[0] = '-'; return df_num(100u - value, out, 1); }
    if (value == 100u) return df_num(0u, out, 0);
    out[0] = '+';
    return df_num(value - 100u, out, 1);
}

/* knob 4 Kill: 0 "OFF", 1..281 -> "20" .. "300" Hz */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return 3; }
    if (value > 281u) value = 281u;
    return df_num(19u + value, out, 0);
}

/* knob 5 Slope: 12 / 24 */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = '1'; out[1] = '2'; }
    else { out[0] = '2'; out[1] = '4'; }
    out[2] = 0;
    return 2;
}

/* knob 6 Glide: 0 "0", 1..100 -> 1 .. 1000 ms (log) */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    if (value == 0u) return df_num(0u, out, 0);
    if (value > 100u) value = 100u;
    return df_num((unsigned int)(int)(df_exp2(((float)(int)value - 1.0f) * 0.1006693f) + 0.5f), out, 0);
}

/* knob 7 Level: 0..36 -> "-12.0" .. "0.0" .. "+6.0" dB */
int ZDL_GetLabel_7(unsigned int value, char *out)
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
    DfParams P;
    float u[9];
    int on;

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
    if (s->magic != DF_MAGIC) df_init(s);

    u[0] = df_ui(params[DJFILTER_LP_SLOT],    (float)DJFILTER_LP_UI_DEFAULT,    100.0f);
    u[1] = df_ui(params[DJFILTER_HP_SLOT],    (float)DJFILTER_HP_UI_DEFAULT,    100.0f);
    u[2] = df_ui(params[DJFILTER_DJ_SLOT],    (float)DJFILTER_DJ_UI_DEFAULT,    200.0f);
    u[3] = df_ui(params[DJFILTER_RESO_SLOT],  (float)DJFILTER_RESO_UI_DEFAULT,  100.0f);
    u[4] = df_ui(params[DJFILTER_KILL_SLOT],  (float)DJFILTER_KILL_UI_DEFAULT,  281.0f);
    u[5] = df_ui(params[DJFILTER_SLOPE_SLOT], (float)DJFILTER_SLOPE_UI_DEFAULT, 1.0f);
    u[6] = df_ui(params[DJFILTER_GLIDE_SLOT], (float)DJFILTER_GLIDE_UI_DEFAULT, 100.0f);
    u[7] = df_ui(params[DJFILTER_LEVEL_SLOT], (float)DJFILTER_LEVEL_UI_DEFAULT, 36.0f);
    u[8] = df_ui(params[DJFILTER_MIX_SLOT],   (float)DJFILTER_MIX_UI_DEFAULT,   100.0f);

    on = (params[0] >= 0.5f);
    df_smooth(s, u);
    if (!on && s->e <= 0.0f) {                   /* fully off: input untouched */
        s->act = 0u;
        return;
    }
    df_prepare(s, &P, u, on);
    if (P.on == 0u && s->act == 0u && P.e >= 1.0f && P.de == 0.0f &&
        P.dg + P.wg > 0.9999f && P.dg + P.wg < 1.0001f)
        return;                                  /* nothing to do: skipped entirely */
    df_process(s, &P, fxBuf);
}

#endif /* DJFILTER_HOST_TEST */
