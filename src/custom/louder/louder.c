/*
 * louder.c - "LOUDER": transparent look-ahead mastering limiter, linked stereo
 *
 * For the end of a live set's chain (after [MIX]): pushes the level up and holds it under a
 * ceiling, like the Pro-L / Ozone Maximizer class of limiter. Spec: docs/NEW-EFFECTS.md, LIMITER.
 *
 * KNOBS (screen values)
 *   0 Gain  0..180  0.0 .. +18.0 dB of push into the limiter (default 0)
 *   1 Ceil  0..60   -6.0 .. 0.0 dB, 0.1 dB steps (default 59 = -0.1 dB)
 *   2 Rels  0..60   AUTO (default), then 1 .. 1000 ms on a log scale
 *   3 Style 0..2    CLEAN (default) / PUNCH / LOUD
 *   4 Mix   0..100  DJ crossfade (dry min(1,2-2m), wet min(1,2m)), default 100
 *
 * HOW IT WORKS (everything is decided once per 8-sample block; the sample loops are plain)
 *   Linked stereo: one gain for both sides, from the louder side.
 *   Look-ahead: the audio is delayed 64 samples (8 blocks, 1.45 ms). Each block the loudest
 *   sample of the pushed signal gives the gain that block needs (target / peak). The gain
 *   used is the mean of 8 "lowest need over the last 9 blocks" values, so it is already
 *   down when the peak leaves the delay, and it gets there in a ramp 64 samples long (no
 *   click). With a 9-block minimum and an 8-block mean, the gain at both ends of a block
 *   (the gain moves in a straight line across the block) is never above what the delayed
 *   block needs: the output stays under the ceiling by construction. The hard clip at Ceil
 *   (after Mix) is the safety net, and what lets PUNCH through.
 *   Release: on top of that the gain comes back by a one-pole release (down at once, since
 *   the look-ahead ramp already shapes the attack). AUTO is program-dependent with two
 *   stages: a slow one (250 ms) that follows the sustained reduction, and a fast one (30 ms)
 *   that takes only the rest, so a single peak lets go quickly and a loud passage lets go
 *   slowly, and it doesn't pump. Rels = a number: one stage with that time.
 *   Style (saturation before the limiter and transient let-through, never the release):
 *   CLEAN none; PUNCH a little saturation, and the limiter aims 1.2 dB over the ceiling so
 *   the clip trims the rest (transients get through); LOUD the most saturation (densest).
 *   The dry path is delayed by the same 64 samples, so a part Mix never comb-filters.
 *
 * METER (gain reduction bar + dB number, peak held 300 ms; docs/NEW-EFFECTS.md "Readouts")
 *   Drawn by the effect on its own cover, no firmware hook (src/custom/common/zdl_meter.h). The
 *   value, hold and throttle run every block (a few cycles). When the shown value (0.1 dB)
 *   changes, at most LO_M_TICK blocks apart (15 a second), the band in the cover is redrawn and
 *   the UI task is asked for a redraw (Event_post), but only if the firmware check done once at
 *   state init says this is MS-70CDR 2.10. Steady value or effect off: no writes, no calls.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static arrays in .audio, no divide, no libm,
 * no switch, no double, helpers forced inline, state in the arena with a magic. The one call
 * (Event_post) is made only after the firmware check.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define LO_DO_PRAGMA(x) _Pragma(#x)
#define LO_EXPAND_PRAGMA(x) LO_DO_PRAGMA(x)
#define LO_ALWAYS_INLINE(fn) LO_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define LO_CODE_SECTION(fn) LO_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define LO_BLOCK_LOOP() LO_DO_PRAGMA(MUST_ITERATE(8, 8, 8)) LO_DO_PRAGMA(UNROLL(1))
#else
#define LO_ALWAYS_INLINE(fn)
#define LO_CODE_SECTION(fn)
#define LO_BLOCK_LOOP()
#endif

#define LO_MAGIC     0x4C4F5531u            /* "LOU1": change when LoState changes */
#define LO_DELAY     64u                    /* look-ahead in samples = 8 blocks */
#define LO_NMIN      9                      /* blocks in the lowest-need window */
#define LO_NAVG      8                      /* blocks in the mean               */
#define LO_CF_AUTO   0.006f                 /* AUTO fast stage, 8 / (44100 * 0.030)  */
#define LO_CS_AUTO   0.00073f               /* AUTO slow stage, 8 / (44100 * 0.250)  */
#define LO_KEY_NONE  0xFFFFFFFFu

/* ---- meter: cover band (zdl_meter.h) ------------------------------------------------- */
#ifndef LOUDER_HOST_TEST
#ifndef LOUDER_AUDIO_FUNC
#define LOUDER_AUDIO_FUNC Fx_DLY_LOUDER
#endif
#define LO_PIC_X(f) _##f##_Pic
#define LO_PIC_(f) LO_PIC_X(f)
extern unsigned char LO_PIC_(LOUDER_AUDIO_FUNC)[];       /* the cover blob; the linker resolves it */
#define ZM_PIC LO_PIC_(LOUDER_AUDIO_FUNC)
#endif
#include "louder_layout.h"                  /* COVER_BAND_OFF / COVER_BAND_LEN, from build.py */
#include "../common/zdl_meter.h"
#define LO_M_HOLD    1654u                  /* blocks: 300 ms */
#define LO_M_TICK    367u                   /* blocks between redraws: 15 per second at most */

typedef struct {
    unsigned int magic;
    unsigned int pos;                       /* ring position, multiple of 8, 0..56 */
    unsigned int bi, mi;                    /* ring indices of bm[] and mn[]       */
    unsigned int was_off;
    unsigned int k_gain, k_ceil, k_rels;    /* knob values the cached numbers are for */
    float gainLin, ceilLin, cF;
    float gprev, rF, rS;
    float bm[LO_NMIN], mn[LO_NAVG];
    float dl[LO_DELAY], dr[LO_DELAY];
    float m_peak;                           /* meter: held reduction (linear, 0..1) */
    unsigned int m_hold, m_tick, m_shown;   /* hold blocks left, redraw throttle, last shown (0.1 dB) */
    unsigned int m_fw;                      /* zm_fw_ok() at init: may we call Event_post */
} LoState;

typedef struct {
    float gain, ceil, thr;                  /* push, ceiling, limiter target (ceiling or a bit above) */
    float a, xm;                            /* saturation: x - a x^3 up to |x| = xm  */
    float cF, cS;
    float dryG, wetG;
    unsigned int sat, autor;
} LoParams;

/* knobs arrive as (screen value) / 100; convert back like the rest of the pack */
LO_ALWAYS_INLINE(lo_ui)
static inline float lo_ui(float raw, float def_ui, float max_ui)
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

/* 2^x for x in -20..20 (error about 2e-6) */
LO_ALWAYS_INLINE(lo_exp2)
static inline float lo_exp2(float x)
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

/* 1 / x for x > 0: exponent trick + three Newton steps (error < 2e-7) */
LO_ALWAYS_INLINE(lo_inv)
static inline float lo_inv(float x)
{
    union { float f; unsigned int u; } c;
    float y;
    c.f = x;
    c.u = 0x7ef311c3u - c.u;
    y = c.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y;
}

LO_ALWAYS_INLINE(lo_init)
static inline void lo_init(LoState *s)
{
    unsigned int i;
    for (i = 0u; i < LO_DELAY; i++) { s->dl[i] = 0.0f; s->dr[i] = 0.0f; }
    for (i = 0u; i < (unsigned int)LO_NMIN; i++) s->bm[i] = 1.0f;
    for (i = 0u; i < (unsigned int)LO_NAVG; i++) s->mn[i] = 1.0f;
    s->pos = 0u; s->bi = 0u; s->mi = 0u; s->was_off = 0u;
    s->k_gain = LO_KEY_NONE; s->k_ceil = LO_KEY_NONE; s->k_rels = LO_KEY_NONE;
    s->gainLin = 1.0f; s->ceilLin = 1.0f; s->cF = LO_CF_AUTO;
    s->gprev = 1.0f; s->rF = 0.0f; s->rS = 0.0f;
    s->m_peak = 0.0f; s->m_hold = 0u; s->m_tick = 0u; s->m_shown = 0u;
    s->m_fw = ZM_FW_OK();
    s->magic = LO_MAGIC;
}

/* u[] = screen values in manifest order: Gain, Ceil, Rels, Style, Mix. The cached numbers
 * (a few 2^x) are only recomputed when their knob moved. */
LO_ALWAYS_INLINE(lo_prepare)
static inline void lo_prepare(LoParams *P, LoState *s, const float *u)
{
    unsigned int kg = (unsigned int)(int)u[0], kc = (unsigned int)(int)u[1], kr = (unsigned int)(int)u[2];
    unsigned int style = (unsigned int)(int)(u[3] + 0.5f);
    float m = u[4] * 0.01f;
    if (kg != s->k_gain) { s->k_gain = kg; s->gainLin = lo_exp2(u[0] * 0.016609640f); }  /* dB / 6.0206 */
    if (kc != s->k_ceil) { s->k_ceil = kc; s->ceilLin = lo_exp2((u[1] - 60.0f) * 0.016609640f); }
    if (kr != s->k_rels) {                      /* 1 .. 1000 ms over 59 steps; c = 1 - 2^(-0.26171 / ms) */
        s->k_rels = kr;
        s->cF = 1.0f - lo_exp2(-0.26171f * lo_exp2(-(u[2] - 1.0f) * 0.16891f));
    }
    P->gain = s->gainLin; P->ceil = s->ceilLin;
    P->thr = (style == 1u) ? s->ceilLin * 1.15f : s->ceilLin;
    P->sat = (style != 0u);
    P->a  = (style == 2u) ? 0.2f : 0.06f;
    P->xm = (style == 2u) ? 1.29f : 2.36f;
    P->autor = (kr == 0u);
    P->cF = P->autor ? LO_CF_AUTO : s->cF;
    P->cS = LO_CS_AUTO;
    P->dryG = 2.0f - 2.0f * m; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * m;        if (P->wetG > 1.0f) P->wetG = 1.0f;
}

/* soft saturation: x - a x^3, flat at the top of the curve (|x| = xm) */
LO_ALWAYS_INLINE(lo_sat)
static inline float lo_sat(float x, float a, float xm)
{
    x = (x > xm) ? xm : x;
    x = (x < -xm) ? -xm : x;
    return x - a * x * x * x;
}

/* ---- meter value: held reduction, throttle, redraw request ---------------------------- */
/* -20 log10(1 - r) in 0.1 dB steps: ln(m), m in 1..2, from a polynomial (error < 1e-4), plus the
 * exponent times ln 2; r in 0..0.999 */
LO_ALWAYS_INLINE(lo_tenths)
static inline unsigned int lo_tenths(float r)
{
    union { float f; unsigned int u; } c;
    float m, ln;
    int e, t;
    c.f = 1.0f - r;
    e = (int)(c.u >> 23) - 127;
    c.u = (c.u & 0x007FFFFFu) | 0x3F800000u;                      /* mantissa 1..2 */
    m = c.f;
    ln = (float)e * 0.6931472f + (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
    t = (int)(-86.85890f * ln + 0.5f);                            /* 0.1 dB */
    return (unsigned int)((t < 0) ? 0 : (t > 999 ? 999 : t));
}

#ifndef LO_METER_REDRAW
#define LO_METER_REDRAW(s) zm_show((s)->m_shown, (s)->m_fw)
#endif

/* r = the block's total reduction. Redraws the cover band when the shown value changes. */
LO_ALWAYS_INLINE(lo_meter_step)
static inline void lo_meter_step(LoState *s, float r)
{
    if (r >= s->m_peak) { s->m_peak = r; s->m_hold = LO_M_HOLD; }
    else if (s->m_hold > 0u) s->m_hold--;
    else s->m_peak = r;                                           /* hold over: follow the value */
    s->m_tick++;
    if (s->m_tick >= LO_M_TICK) {
        unsigned int t = lo_tenths(s->m_peak);
        s->m_tick = 0u;
        if (t != s->m_shown) { s->m_shown = t; LO_METER_REDRAW(s); }
    }
}

/* ---- the limiter: one 8-sample block, L = buf[0..7], R = buf[8..15], in place ---------- */
LO_ALWAYS_INLINE(lo_process)
static inline void lo_process(LoState *s, const LoParams *P, float *restrict buf)
{
    int i;
    unsigned int p = s->pos, k;
    const float G = P->gain, C = P->ceil, a = P->a, xm = P->xm, dryG = P->dryG, wetG = P->wetG;
    const unsigned int sat = P->sat;
    float pk = 0.0f, gr, lo, sum, gb, red, need, rS = s->rS, rF = s->rF, r, gf, g, gs;

    /* 1. the loudest sample of the pushed signal, both sides */
    LO_BLOCK_LOOP()
    for (i = 0; i < 8; i++) {
        float xl = buf[i] * G, xr = buf[i + 8] * G, al, ar;
        if (sat) { xl = lo_sat(xl, a, xm); xr = lo_sat(xr, a, xm); }
        al = (xl < 0.0f) ? -xl : xl;
        ar = (xr < 0.0f) ? -xr : xr;
        al = (ar > al) ? ar : al;
        pk = (al > pk) ? al : pk;
    }

    /* 2. the gain this block needs, then the look-ahead smoothing */
    gr = (pk > P->thr) ? P->thr * lo_inv(pk) : 1.0f;
    s->bm[s->bi] = gr;
    s->bi = (s->bi + 1u >= (unsigned int)LO_NMIN) ? 0u : s->bi + 1u;
    lo = s->bm[0];
    for (k = 1u; k < (unsigned int)LO_NMIN; k++) lo = (s->bm[k] < lo) ? s->bm[k] : lo;
    s->mn[s->mi] = lo;
    s->mi = (s->mi + 1u >= (unsigned int)LO_NAVG) ? 0u : s->mi + 1u;
    sum = 0.0f;
    for (k = 0u; k < (unsigned int)LO_NAVG; k++) sum += s->mn[k];
    gb = sum * 0.125f;

    /* 3. release: the reduction never drops below what the look-ahead asks for */
    red = 1.0f - gb;
    if (P->autor) {
        rS += P->cS * (red - rS);                                 /* slow stage: the sustained part */
        need = red - rS;
        need = (need > 0.0f) ? need : 0.0f;
    } else {
        rS = 0.0f;
        need = red;
    }
    rF = rF - P->cF * rF;                                         /* fast stage lets go */
    rF = (need > rF) ? need : rF;
    if (rF < 1e-9f) rF = 0.0f;
    if (rS < 1e-9f) rS = 0.0f;
    r = rS + rF;
    r = (r > 0.999f) ? 0.999f : r;
    gf = 1.0f - r;
    s->rS = rS; s->rF = rF;
    g = s->gprev;
    gs = (gf - g) * 0.125f;
    s->gprev = gf;

    /* 4. the delayed audio through the gain, Mix, then the hard clip at the ceiling */
    LO_BLOCK_LOOP()
    for (i = 0; i < 8; i++) {
        float dL = s->dl[p + i], dR = s->dr[p + i], xl, xr, ol, orr;
        s->dl[p + i] = buf[i]; s->dr[p + i] = buf[i + 8];
        g += gs;
        xl = dL * G; xr = dR * G;
        if (sat) { xl = lo_sat(xl, a, xm); xr = lo_sat(xr, a, xm); }
        ol = dryG * dL + wetG * (xl * g);
        orr = dryG * dR + wetG * (xr * g);
#ifdef LOUDER_HOST_TEST
        { extern unsigned int lo_t_clips; if (ol > C * 1.0001f || ol < -C * 1.0001f || orr > C * 1.0001f || orr < -C * 1.0001f) lo_t_clips++; }
#endif
        buf[i]     = (ol > C) ? C : ((ol < -C) ? -C : ol);
        buf[i + 8] = (orr > C) ? C : ((orr < -C) ? -C : orr);
    }
    s->pos = (p + 8u >= LO_DELAY) ? 0u : p + 8u;
    lo_meter_step(s, r);
}

/* ---- on-screen text ------------------------------------------------------ */
/* tenths as "12.3": whole part, '.', one decimal; the caller puts a sign in front */
LO_ALWAYS_INLINE(lo_tenths_text)
static inline int lo_tenths_text(unsigned int v, char *out, int len)
{
    int t = 0, n = (int)v;
    while (n >= 100) { n -= 100; t++; }
    if (t > 0) { out[len] = (char)('0' + t); len++; }
    t = 0;
    while (n >= 10) { n -= 10; t++; }
    out[len] = (char)('0' + t); len++;
    out[len] = '.'; len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* knob 0 Gain: 0..180 -> "0.0".."18.0" */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value > 180u) value = 180u;
    return lo_tenths_text(value, out, 0);
}

/* knob 1 Ceil: 0..60 -> "-6.0".."0.0" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    unsigned int d;
    if (value > 60u) value = 60u;
    d = 60u - value;
    if (d == 0u) return lo_tenths_text(0u, out, 0);
    out[0] = '-';
    return lo_tenths_text(d, out, 1);
}

/* knob 2 Rels: 0 "AUTO", 1..60 -> 1..1000 ms (log) */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    int ms, len = 0, th = 0, h = 0, t = 0;
    if (value == 0u) { out[0] = 'A'; out[1] = 'U'; out[2] = 'T'; out[3] = 'O'; out[4] = 0; return 4; }
    if (value > 60u) value = 60u;
    ms = (int)(lo_exp2(((float)(int)value - 1.0f) * 0.16891f) + 0.5f);
    while (ms >= 1000) { ms -= 1000; th++; }
    while (ms >= 100) { ms -= 100; h++; }
    while (ms >= 10) { ms -= 10; t++; }
    if (th > 0) { out[len] = (char)('0' + th); len++; }
    if (th > 0 || h > 0) { out[len] = (char)('0' + h); len++; }
    if (th > 0 || h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + ms); len++;
    out[len] = 0;
    return len;
}

/* knob 3 Style: CLEAN / PUNCH / LOUD */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'C'; out[1] = 'L'; out[2] = 'E'; out[3] = 'A'; out[4] = 'N'; out[5] = 0; return 5; }
    if (value == 1u) { out[0] = 'P'; out[1] = 'U'; out[2] = 'N'; out[3] = 'C'; out[4] = 'H'; out[5] = 0; return 5; }
    out[0] = 'L'; out[1] = 'O'; out[2] = 'U'; out[3] = 'D'; out[4] = 0;
    return 4;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef LOUDER_HOST_TEST

#include "louder_params.h"

#ifndef LOUDER_AUDIO_FUNC
#define LOUDER_AUDIO_FUNC Fx_DLY_LOUDER
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

LO_CODE_SECTION(LOUDER_AUDIO_FUNC)
void LOUDER_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    LoState *s;
    LoParams P;
    float u[5];

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(LoState) || span < (end - base)) return;
    if (stateBase + sizeof(LoState) > end) return;

    s = (LoState *)stateBase;

    if (params[0] < 0.5f) {                      /* effect switched off: input untouched */
        if (s->magic == LO_MAGIC) s->was_off = 1u;
        return;
    }
    if (s->magic != LO_MAGIC) lo_init(s);
    if (s->was_off) {                            /* just switched on: no old audio from the delay */
        unsigned int i;
        for (i = 0u; i < LO_DELAY; i++) { s->dl[i] = 0.0f; s->dr[i] = 0.0f; }
        s->gprev = 1.0f; s->rF = 0.0f; s->rS = 0.0f;
        s->was_off = 0u;
    }

    u[0] = lo_ui(params[LOUDER_GAIN_SLOT],  (float)LOUDER_GAIN_UI_DEFAULT,  180.0f);
    u[1] = lo_ui(params[LOUDER_CEIL_SLOT],  (float)LOUDER_CEIL_UI_DEFAULT,  60.0f);
    u[2] = lo_ui(params[LOUDER_RELS_SLOT],  (float)LOUDER_RELS_UI_DEFAULT,  60.0f);
    u[3] = lo_ui(params[LOUDER_STYLE_SLOT], (float)LOUDER_STYLE_UI_DEFAULT, 2.0f);
    u[4] = lo_ui(params[LOUDER_MIX_SLOT],   (float)LOUDER_MIX_UI_DEFAULT,   100.0f);

    lo_prepare(&P, s, u);
    lo_process(s, &P, fxBuf);
}

#endif /* LOUDER_HOST_TEST */
