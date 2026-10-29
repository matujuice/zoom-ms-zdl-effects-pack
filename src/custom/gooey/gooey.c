/*
 * gooey.c - "Gooey": SSL G-bus style VCA mix-bus compressor, linked stereo
 *
 * Glue for a whole mix. One character, done well. Spec: docs/NEW-EFFECTS.md, COMP.
 *
 * KNOBS (screen values)
 *   0 Thrsh 0..40   -40 .. 0 dB, 1 dB steps (default 40 = 0 dB = OFF: the effect is bypassed)
 *   1 Ratio 0..3    1.5 / 2 / 4 / 10 (default 2 = 4:1)
 *   2 Attck 0..5    0.1 / 0.3 / 1 / 3 / 10 / 30 ms (default 4 = 10 ms)
 *   3 Rels  0..4    0.1 / 0.3 / 0.6 / 1.2 s / AUTO (default 4 = AUTO)
 *   4 MkUp  0..41   AUTO / OFF (default 1) / +0.5 .. +20.0 dB in 0.5 dB steps (value 2 .. 41)
 *   5 Mix   0..100  linear dry/wet (dry 1-m, wet m, like the Digitakt compressor), default 100 (parallel compression)
 *   6 SC HP 0..20   OFF (default), then 20 .. 300 Hz on a log scale (12 dB/oct, detector only)
 *
 * HOW IT WORKS (everything but the plain sample loops runs once per 8-sample block)
 *   Detector: linked stereo, one value from the louder side of each sample (after the sidechain
 *   high-pass, which only the detector sees: the audio is never filtered). Per block: the peak,
 *   and the mean square. A peak follower (50 ms fall) and a mean-square follower (5 ms) give a
 *   level that is half peak, half RMS. Feed-forward: the detector sees the input.
 *   Gain computer: level in log2 units, soft knee 6 dB wide, slope 1 - 1/ratio.
 *   Ballistics on the reduction (log domain), at block rate: attack and release are one-pole.
 *   AUTO release (program-dependent, like the G-bus): a slow stage (1.2 s) follows the
 *   sustained part of the reduction, a fast stage (0.1 s release) takes only the rest. A
 *   single peak lets go in 0.1 s, a long loud passage in about 1.2 s, and it does not pump.
 *   MkUp AUTO adds back the average gain reduction (1 s average). Fixed MkUp is a plain gain.
 *   Output gain per block = dryG + wetG * 2^(makeup - reduction), moved in a straight line over
 *   the block (no zipper). Thrsh at 0 (or Mix 0) bypasses everything: the gain ramps to 1, then
 *   the block is skipped entirely (the cost is a compare). SC HP is skipped when OFF.
 *
 * METER (gain reduction bar + dB number, peak held 300 ms; docs/NEW-EFFECTS.md "Readouts")
 *   Drawn by the effect on its own cover, no firmware hook (src/custom/common/zdl_meter.h). The
 *   value, hold and throttle run every block (a few cycles). When the shown value (0.1 dB)
 *   changes, at most GO_M_TICK blocks apart (15 a second), the band in the cover is redrawn and
 *   the UI task is asked for a redraw (Event_post), but only if the firmware check done once at
 *   state init says this is MS-70CDR 2.10. Steady value or effect off: no writes, no calls.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static arrays in .audio, no divide, no libm,
 * no switch, no double, helpers forced inline, state in the arena with a magic. The one call
 * (Event_post) is made only after the firmware check.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define GO_DO_PRAGMA(x) _Pragma(#x)
#define GO_EXPAND_PRAGMA(x) GO_DO_PRAGMA(x)
#define GO_ALWAYS_INLINE(fn) GO_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define GO_CODE_SECTION(fn) GO_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define GO_BLOCK_LOOP() GO_DO_PRAGMA(MUST_ITERATE(8, 8, 8)) GO_DO_PRAGMA(UNROLL(1))
#else
#define GO_ALWAYS_INLINE(fn)
#define GO_CODE_SECTION(fn)
#define GO_BLOCK_LOOP()
#endif

#define GO_MAGIC     0x474F4F31u            /* "GOO1": change when GoState changes */
#define GO_KEY_NONE  0xFFFFFFFFu
#define GO_DPK       0.99638f               /* peak follower fall per block: 50 ms         */
#define GO_CMS       0.03563f               /* mean-square follower per block: 5 ms        */
#define GO_CAV       0.0001814f             /* AUTO makeup average per block: 1 s          */
#define GO_CSLOW     0.00015117f            /* AUTO slow stage per block: 1.2 s            */
#define GO_KNEE_H    0.49829f               /* half the 6 dB knee, in log2 units (1 = 6.02 dB) */
#define GO_KNEE_K    0.50172f               /* 1 / (2 x knee width)                        */
#define GO_DB2LOG    0.16609640f            /* dB -> log2 units                            */

/* ---- meter: cover band (zdl_meter.h) ------------------------------------------------- */
#ifndef GOOEY_HOST_TEST
#ifndef GOOEY_AUDIO_FUNC
#define GOOEY_AUDIO_FUNC Fx_DLY_Gooey
#endif
#define GO_PIC_X(f) _##f##_Pic
#define GO_PIC_(f) GO_PIC_X(f)
extern unsigned char GO_PIC_(GOOEY_AUDIO_FUNC)[];       /* the cover blob; the linker resolves it */
#define ZM_PIC GO_PIC_(GOOEY_AUDIO_FUNC)
#endif
#include "gooey_layout.h"                  /* COVER_BAND_OFF / COVER_BAND_LEN, from build.py */
#include "../common/zdl_meter.h"
#define GO_M_HOLD    1654u                  /* blocks: 300 ms */
#define GO_M_TICK    367u                   /* blocks between redraws: 15 per second at most */

typedef struct {
    unsigned int magic;
    unsigned int was_off;
    unsigned int k_att, k_rel, k_hp;        /* knob values the cached numbers are for */
    float cA, cR, hpa;
    float pkenv, ms, rS, rF, avg, gm;
    float a1l, a2l, a1r, a2r;               /* sidechain high-pass: the two low-pass states per side */
    float m_peak;                           /* meter: held reduction (linear, 0..1) */
    unsigned int m_hold, m_tick, m_shown;   /* hold blocks left, redraw throttle, last shown (0.1 dB) */
    unsigned int m_fw;                      /* zm_fw_ok() at init: may we call Event_post */
} GoState;

typedef struct {
    float t2, slope, mk, dryG, wetG, hpa, cA, cR;
    unsigned int off, autor, automk, hp;
} GoParams;

/* knobs arrive as (screen value) / 100; convert back like the rest of the pack */
GO_ALWAYS_INLINE(go_ui)
static inline float go_ui(float raw, float def_ui, float max_ui)
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
GO_ALWAYS_INLINE(go_exp2)
static inline float go_exp2(float x)
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

/* log2 x for x > 0 normal (error < 2e-4): exponent plus a polynomial for the mantissa */
GO_ALWAYS_INLINE(go_log2)
static inline float go_log2(float x)
{
    union { float f; unsigned int u; } c;
    float m;
    int e;
    c.f = x;
    e = (int)(c.u >> 23) - 127;
    c.u = (c.u & 0x007FFFFFu) | 0x3F800000u;
    m = c.f;
    return (float)e + 1.442695f * (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

/* sqrt x for x >= 0: inverse square root by the exponent trick and three Newton steps */
GO_ALWAYS_INLINE(go_sqrt)
static inline float go_sqrt(float x)
{
    union { float f; unsigned int u; } c;
    float y;
    if (x < 1e-20f) return 0.0f;
    c.f = x;
    c.u = 0x5f3759dfu - (c.u >> 1);
    y = c.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

GO_ALWAYS_INLINE(go_reset)
static inline void go_reset(GoState *s)
{
    s->pkenv = 0.0f; s->ms = 0.0f; s->rS = 0.0f; s->rF = 0.0f; s->avg = 0.0f;
    s->a1l = 0.0f; s->a2l = 0.0f; s->a1r = 0.0f; s->a2r = 0.0f;
}

GO_ALWAYS_INLINE(go_init)
static inline void go_init(GoState *s)
{
    go_reset(s);
    s->k_att = GO_KEY_NONE; s->k_rel = GO_KEY_NONE; s->k_hp = GO_KEY_NONE;
    s->cA = 0.0f; s->cR = 0.0f; s->hpa = 0.0f; s->gm = 1.0f; s->was_off = 0u;
    s->m_peak = 0.0f; s->m_hold = 0u; s->m_tick = 0u; s->m_shown = 0u;
    s->m_fw = ZM_FW_OK();
    s->magic = GO_MAGIC;
}

/* 1 / ms of the attack and release choices (no tables in .audio) */
GO_ALWAYS_INLINE(go_att_inv)
static inline float go_att_inv(unsigned int i)
{
    return (i == 0u) ? 10.0f : (i == 1u) ? 3.3333333f : (i == 2u) ? 1.0f : (i == 3u) ? 0.33333333f : (i == 4u) ? 0.1f : 0.033333333f;
}
GO_ALWAYS_INLINE(go_rel_inv)
static inline float go_rel_inv(unsigned int i)
{
    return (i == 0u) ? 0.01f : (i == 1u) ? 0.0033333333f : (i == 2u) ? 0.0016666667f : (i == 3u) ? 0.00083333333f : 0.01f;
}

/* u[] = screen values in manifest order: Thrsh, Ratio, Attck, Rels, MkUp, Mix, SC HP. The cached
 * numbers (a few 2^x) are only recomputed when their knob moved. */
GO_ALWAYS_INLINE(go_prepare)
static inline void go_prepare(GoParams *P, GoState *s, const float *u)
{
    unsigned int thr = (unsigned int)(int)u[0], rat = (unsigned int)(int)u[1], ka = (unsigned int)(int)u[2];
    unsigned int kr = (unsigned int)(int)u[3], mk = (unsigned int)(int)u[4], kh = (unsigned int)(int)u[6];
    float m = u[5] * 0.01f;
    if (ka != s->k_att) { s->k_att = ka; s->cA = 1.0f - go_exp2(-0.26171f * go_att_inv(ka)); }
    if (kr != s->k_rel) { s->k_rel = kr; s->cR = 1.0f - go_exp2(-0.26171f * go_rel_inv(kr)); }
    if (kh != s->k_hp) {                        /* f = 20 .. 300 Hz; a = 1 - exp(-2 pi f / 44100) */
        s->k_hp = kh;
        s->hpa = 1.0f - go_exp2(-2.05555e-4f * 20.0f * go_exp2(((float)(int)kh - 1.0f) * 0.205627f));
    }
    P->dryG = 1.0f - m;
    P->wetG = m;
    P->off = (thr >= 40u) || (P->wetG <= 0.0f);
    P->t2 = ((float)(int)thr - 40.0f) * GO_DB2LOG;
    P->slope = (rat == 0u) ? 0.33333333f : (rat == 1u) ? 0.5f : (rat == 2u) ? 0.75f : 0.9f;
    P->cA = s->cA; P->cR = s->cR; P->hpa = s->hpa;
    P->autor = (kr >= 4u);
    P->automk = (mk == 0u);
    P->mk = (mk >= 2u) ? ((float)(int)mk - 1.0f) * (0.5f * GO_DB2LOG) : 0.0f;
    P->hp = (kh != 0u);
}

/* ---- meter value: held reduction, throttle, redraw request ---------------------------- */
/* -20 log10(1 - r) in 0.1 dB steps (same maths as LOUDER's), r in 0..0.999 */
GO_ALWAYS_INLINE(go_tenths)
static inline unsigned int go_tenths(float r)
{
    int t = (int)(-60.205999f * go_log2(1.0f - r) + 0.5f);
    return (unsigned int)((t < 0) ? 0 : (t > 999 ? 999 : t));
}

#ifndef GO_METER_REDRAW
#define GO_METER_REDRAW(s) zm_show((s)->m_shown, (s)->m_fw)
#endif

/* r = the block's reduction (1 - gain). Redraws the cover band when the shown value changes. */
GO_ALWAYS_INLINE(go_meter_step)
static inline void go_meter_step(GoState *s, float r)
{
    if (r >= s->m_peak) { s->m_peak = r; s->m_hold = GO_M_HOLD; }
    else if (s->m_hold > 0u) s->m_hold--;
    else s->m_peak = r;                                           /* hold over: follow the value */
    s->m_tick++;
    if (s->m_tick >= GO_M_TICK) {
        unsigned int t = go_tenths(s->m_peak);
        s->m_tick = 0u;
        if (t != s->m_shown) { s->m_shown = t; GO_METER_REDRAW(s); }
    }
}

/* ---- the compressor: one 8-sample block, L = buf[0..7], R = buf[8..15], in place ------- */
GO_ALWAYS_INLINE(go_process)
static inline void go_process(GoState *s, const GoParams *P, float *restrict buf)
{
    int i;
    float pk = 0.0f, sq = 0.0f, g, gt, gs, lvl, lx, o, gr, rS, rF, need, tot, mk;

    if (P->off) {                               /* bypass: ramp the gain to 1, then do nothing */
        go_reset(s);
        go_meter_step(s, 0.0f);
        if (s->gm == 1.0f) return;
        g = s->gm; gs = (1.0f - g) * 0.125f; s->gm = 1.0f;
        GO_BLOCK_LOOP()
        for (i = 0; i < 8; i++) { g += gs; buf[i] *= g; buf[i + 8] *= g; }
        return;
    }

    /* 1. the detector: the louder side of each sample (high-passed when SC HP is on) */
    if (P->hp) {
        const float a = P->hpa;
        float a1l = s->a1l, a2l = s->a2l, a1r = s->a1r, a2r = s->a2r;
        GO_BLOCK_LOOP()
        for (i = 0; i < 8; i++) {
            float xl = buf[i], xr = buf[i + 8], p, q;
            a1l += a * (xl - a1l); xl -= a1l; a2l += a * (xl - a2l); xl -= a2l;
            a1r += a * (xr - a1r); xr -= a1r; a2r += a * (xr - a2r); xr -= a2r;
            p = (xl < 0.0f) ? -xl : xl;
            q = (xr < 0.0f) ? -xr : xr;
            p = (q > p) ? q : p;
            pk = (p > pk) ? p : pk;
            sq += p * p;
        }
        s->a1l = a1l; s->a2l = a2l; s->a1r = a1r; s->a2r = a2r;
    } else {
        GO_BLOCK_LOOP()
        for (i = 0; i < 8; i++) {
            float p = buf[i], q = buf[i + 8];
            p = (p < 0.0f) ? -p : p;
            q = (q < 0.0f) ? -q : q;
            p = (q > p) ? q : p;
            pk = (p > pk) ? p : pk;
            sq += p * p;
        }
    }

    /* 2. level: half peak, half RMS (both in sine-peak units) */
    s->pkenv = (pk > s->pkenv * GO_DPK) ? pk : s->pkenv * GO_DPK;
    s->ms += GO_CMS * (sq * 0.125f - s->ms);
    lvl = 0.5f * (s->pkenv + go_sqrt(2.0f * s->ms)) + 1e-6f;
    lx = go_log2(lvl);

    /* 3. gain computer: soft knee, reduction in log2 units */
    o = lx - P->t2;
    if (o <= -GO_KNEE_H) gr = 0.0f;
    else if (o >= GO_KNEE_H) gr = P->slope * o;
    else { float t = o + GO_KNEE_H; gr = P->slope * t * t * GO_KNEE_K; }

    /* 4. ballistics: AUTO has a slow stage for the sustained part and a fast one for the rest */
    rS = s->rS; rF = s->rF;
    if (P->autor) { rS += GO_CSLOW * (gr - rS); need = gr - rS; need = (need > 0.0f) ? need : 0.0f; }
    else { rS = 0.0f; need = gr; }
    rF += ((need > rF) ? P->cA : P->cR) * (need - rF);
    if (rF < 1e-7f) rF = 0.0f;
    if (rS < 1e-7f) rS = 0.0f;
    s->rS = rS; s->rF = rF;
    tot = rS + rF;

    /* 5. makeup, then the output gain for the block (dry + wet), moved in a line over the block */
    if (P->automk) { s->avg += GO_CAV * (tot - s->avg); mk = s->avg; }
    else mk = P->mk;
    gt = P->dryG + P->wetG * go_exp2(mk - tot);
    g = s->gm; gs = (gt - g) * 0.125f; s->gm = gt;
    GO_BLOCK_LOOP()
    for (i = 0; i < 8; i++) { g += gs; buf[i] *= g; buf[i + 8] *= g; }

    go_meter_step(s, 1.0f - go_exp2(-tot));
}

/* ---- on-screen text ------------------------------------------------------ */
/* tenths as "12.3": whole part, '.', one decimal; the caller puts a sign in front */
GO_ALWAYS_INLINE(go_tenths_text)
static inline int go_tenths_text(unsigned int v, char *out, int len)
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

/* whole number 0..999 as text */
GO_ALWAYS_INLINE(go_int_text)
static inline int go_int_text(int n, char *out, int len)
{
    int h = 0, t = 0;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10) { n -= 10; t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* knob 0 Thrsh: 0..40 -> "-40".."0" (dB) */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value > 40u) value = 40u;
    if (value == 40u) { out[0] = '0'; out[1] = 0; return 1; }
    out[0] = '-';
    return go_int_text((int)(40u - value), out, 1);
}

/* knob 1 Ratio: 1.5:1 / 2:1 / 4:1 / 10:1 */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int n = 0;
    if (value == 0u) { out[n++] = '1'; out[n++] = '.'; out[n++] = '5'; }
    else { n = go_int_text((value == 1u) ? 2 : (value == 2u) ? 4 : 10, out, 0); }
    out[n++] = ':'; out[n++] = '1'; out[n] = 0;
    return n;
}

/* knob 2 Attck: 0.1 / 0.3 / 1 / 3 / 10 / 30 ms */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    int n;
    if (value == 0u || value == 1u) { out[0] = '0'; out[1] = '.'; out[2] = (char)('1' + 2 * (int)value); n = 3; }
    else n = go_int_text((value == 2u) ? 1 : (value == 3u) ? 3 : (value == 4u) ? 10 : 30, out, 0);
    out[n++] = 'm'; out[n++] = 's'; out[n] = 0;
    return n;
}

/* knob 3 Rels: 0.1s / 0.3s / 0.6s / 1.2s / AUTO */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value >= 4u) { out[0] = 'A'; out[1] = 'U'; out[2] = 'T'; out[3] = 'O'; out[4] = 0; return 4; }
    out[0] = (value == 3u) ? '1' : '0'; out[1] = '.';
    out[2] = (value == 0u) ? '1' : (value == 1u) ? '3' : (value == 2u) ? '6' : '2';
    out[3] = 's'; out[4] = 0;
    return 4;
}

/* knob 4 MkUp: AUTO / OFF / +0.5 .. +20.0 dB */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'A'; out[1] = 'U'; out[2] = 'T'; out[3] = 'O'; out[4] = 0; return 4; }
    if (value == 1u) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return 3; }
    if (value > 41u) value = 41u;
    out[0] = '+';
    return go_tenths_text((value - 1u) * 5u, out, 1);
}

/* knob 6 SC HP: OFF, then 20 .. 300 Hz (log) */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    int n;
    if (value == 0u) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return 3; }
    if (value > 20u) value = 20u;
    n = go_int_text((int)(20.0f * go_exp2(((float)(int)value - 1.0f) * 0.205627f) + 0.5f), out, 0);
    out[n++] = 'H'; out[n++] = 'z'; out[n] = 0;
    return n;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef GOOEY_HOST_TEST

#include "gooey_params.h"

#ifndef GOOEY_AUDIO_FUNC
#define GOOEY_AUDIO_FUNC Fx_DLY_Gooey
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

GO_CODE_SECTION(GOOEY_AUDIO_FUNC)
void GOOEY_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    GoState *s;
    GoParams P;
    float u[7];

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(GoState) || span < (end - base)) return;
    if (stateBase + sizeof(GoState) > end) return;

    s = (GoState *)stateBase;

    if (params[0] < 0.5f) {                      /* effect switched off: input untouched */
        if (s->magic == GO_MAGIC) s->was_off = 1u;
        return;
    }
    if (s->magic != GO_MAGIC) go_init(s);
    if (s->was_off) { go_reset(s); s->gm = 1.0f; s->was_off = 0u; }

    u[0] = go_ui(params[GOOEY_THRSH_SLOT], (float)GOOEY_THRSH_UI_DEFAULT, 40.0f);
    u[1] = go_ui(params[GOOEY_RATIO_SLOT], (float)GOOEY_RATIO_UI_DEFAULT, 3.0f);
    u[2] = go_ui(params[GOOEY_ATTCK_SLOT], (float)GOOEY_ATTCK_UI_DEFAULT, 5.0f);
    u[3] = go_ui(params[GOOEY_RELS_SLOT],  (float)GOOEY_RELS_UI_DEFAULT,  4.0f);
    u[4] = go_ui(params[GOOEY_MKUP_SLOT],  (float)GOOEY_MKUP_UI_DEFAULT,  41.0f);
    u[5] = go_ui(params[GOOEY_MIX_SLOT],   (float)GOOEY_MIX_UI_DEFAULT,   100.0f);
    u[6] = go_ui(params[GOOEY_SC_HP_SLOT], (float)GOOEY_SC_HP_UI_DEFAULT, 20.0f);

    go_prepare(&P, s, u);
    go_process(s, &P, fxBuf);
}

#endif /* GOOEY_HOST_TEST */
