/*
 * kickkill.c - "KickKill": a DJ-style kick kill (24 dB/oct low cut), linked stereo
 *
 * The Kill knob that used to sit in DJFilter, as its own effect, so it can go on another signal path.
 *
 * KNOBS (screen values)
 *   0 Kill  0..281   OFF, then 20 .. 300 Hz in 1 Hz steps (default 81 = 100 Hz)
 *   1 Mix   0..100   DJ crossfade (dry min(1,2-2m), wet min(1,2m)), default 100
 *
 * HOW IT WORKS
 *   Two Q 0.71 zero-delay-feedback state-variable high-pass stages (24 dB/oct, -6 dB at the cutoff),
 *   both sides in one loop, the same stages as DJFilter. The coefficients are worked out only when
 *   a knob moves. Kill OFF works like the footswitch: a 128-sample (2.9 ms) gain ramp, then the
 *   input passes untouched at zero cost; switched back on, the filter memory is fitted to the signal
 *   first. Mix 100, on: no dry pass. Pedal-safe rules (docs/SAFE-DSP-RULES.md): no libm, no
 *   division, no switch, no static arrays, no double.
 *
 * State: 2 stages x 2 sides x 2 memories, the ramp. In the arena, validated by a magic number
 * (change it when KkState changes).
 */

#include <stdint.h>
#include "../common/zarena.h"

#ifdef __TI_COMPILER_VERSION__
#define KK_DO_PRAGMA(x) _Pragma(#x)
#define KK_EXPAND_PRAGMA(x) KK_DO_PRAGMA(x)
#define KK_ALWAYS_INLINE(fn) KK_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define KK_CODE_SECTION(fn) KK_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define KK_BLOCK_LOOP() KK_DO_PRAGMA(MUST_ITERATE(8, 8, 8)) KK_DO_PRAGMA(UNROLL(1))
#else
#define KK_ALWAYS_INLINE(fn)
#define KK_CODE_SECTION(fn)
#define KK_BLOCK_LOOP()
#endif

#define KK_MAGIC   0x4B4B3031u              /* "KK01": change when KkState changes */
#define KK_K0      1.4142136f               /* 1/Q for Q = 0.7071 (Butterworth)    */
#define KK_FADE    0.0625f                  /* on/off ramp per block: 128 samples  */

typedef struct {
    unsigned int magic;
    float e;               /* on/off ramp 0 (off) .. 1 (on)                       */
    unsigned int act;      /* 1: the filter ran last block (its memory is live)   */
    float ic[8];           /* stage 1 L, R, stage 2 L, R: two memories each       */
    float rk[2];           /* the knobs' raw values u comes from                  */
    float u[2];            /* screen values: Kill, Mix                            */
    float a1, a2, a3, cb;  /* high-pass stage: v1, v2 update, out = x + cb v1 - v2 */
    float dg, wg;          /* Mix: dry and wet gain                               */
    unsigned int seal[ZA_NSEAL(21)]; /* every word above, bits 30, 29 between blocks (zarena.h) */
} KkState;
ZA_CHECK(KkState);

/* knobs arrive as (screen value) / 100; convert back like the rest of the pack */
KK_ALWAYS_INLINE(kk_ui)
static inline float kk_ui(float raw, float def_ui, float max_ui)
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
KK_ALWAYS_INLINE(kk_recip)
static inline float kk_recip(float x)
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

/* g = tan(pi f / 44100) for f up to 19 kHz (the series DJFilter uses; error under 0.5 %) */
KK_ALWAYS_INLINE(kk_g)
static inline float kk_g(float f)
{
    float w = f * 3.5617e-5f, w2 = w * w, t;
    t = w * (1.0f + w2 * (0.33333334f + w2 * (0.13333334f + w2 * 0.053968254f)));
    return 2.0f * t * kk_recip(1.0f - t * t);
}

/* Both stages over both sides' 8 samples, in place, in one loop (four chains side by side keep the
 * DSP's pipeline full). fr: the filter was not running, so its memories start at 0 (a high-pass with
 * nothing in its low-pass). v1 = a1 m1 + a2 (x - m2), v2 = m2 + a2 m1 + a3 (x - m2),
 * out = x + cb v1 - v2, split into the part the memories give (k1, k2) and the input times cG. */
#define KK_SVF(m1, m2, in, out) { float xx = in, k1 = a1 * m1 - a2 * m2, k2 = m2 + a2 * m1 - a3 * m2, \
    v1 = k1 + a2 * xx, v2 = k2 + a3 * xx; out = (cb * k1 - k2) + cG * xx; \
    m1 = (v1 + v1) - m1; m2 = (v2 + v2) - m2; }
#define KK_FLUSH(v) if (v < 1e-15f && v > -1e-15f) v = 0.0f;
KK_ALWAYS_INLINE(kk_filter)
static inline void kk_filter(KkState *s, float *restrict bf, unsigned int fr)
{
    int i;
    float *restrict ic = s->ic;
    const float a1 = s->a1, a2 = s->a2, a3 = s->a3, cb = s->cb;
    const float cG = 1.0f + cb * a2 - a3;
    float p1 = ic[0], p2 = ic[1], q1 = ic[2], q2 = ic[3], r1 = ic[4], r2 = ic[5], t1 = ic[6], t2 = ic[7];
    if (fr) { p1 = 0.0f; p2 = 0.0f; q1 = 0.0f; q2 = 0.0f; r1 = 0.0f; r2 = 0.0f; t1 = 0.0f; t2 = 0.0f; }
    KK_BLOCK_LOOP()
    for (i = 0; i < 8; i++) {
        float yl, yr, zl, zr;
        KK_SVF(p1, p2, bf[i], yl) KK_SVF(q1, q2, bf[i + 8], yr)
        KK_SVF(r1, r2, yl, zl)    KK_SVF(t1, t2, yr, zr)
        bf[i] = zl; bf[i + 8] = zr;
    }
    KK_FLUSH(p1) KK_FLUSH(p2) KK_FLUSH(q1) KK_FLUSH(q2) KK_FLUSH(r1) KK_FLUSH(r2) KK_FLUSH(t1) KK_FLUSH(t2)
    ic[0] = p1; ic[1] = p2; ic[2] = q1; ic[3] = q2; ic[4] = r1; ic[5] = r2; ic[6] = t1; ic[7] = t2;
}
#undef KK_SVF
#undef KK_FLUSH

/* ---- labels (the pedal screen) -------------------------------------------------------------- */
/* n in 0..999 as text, no leading zeros; returns the length */
static int kk_num(unsigned int n, char *out)
{
    int h = 0, t = 0, len = 0;
    while (n >= 100u) { n -= 100u; h++; }
    while (n >= 10u) { n -= 10u; t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + (int)n); len++;
    out[len] = 0;
    return len;
}

/* knob 0 Kill: 0 "OFF", 1..281 -> "20" .. "300" Hz */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return 3; }
    if (value > 281u) value = 281u;
    return kk_num(19u + value, out);
}

#ifndef KICKKILL_HOST_TEST

#include "kickkill_params.h"

#ifndef KICKKILL_AUDIO_FUNC
#define KICKKILL_AUDIO_FUNC Fx_DLY_KickKill
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

KK_CODE_SECTION(KICKKILL_AUDIO_FUNC)
void KICKKILL_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *buf    = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    KkState *s;
    float r0, r1, e0, de;
    int on, i;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(KkState) || span < (end - base)) return;
    if (stateBase + sizeof(KkState) > end) return;

    s = (KkState *)stateBase;
    za_unseal(s, ZA_HDR(KkState), s->seal);
    if (s->magic != KK_MAGIC) {
        s->e = 0.0f; s->act = 0u;
        for (i = 0; i < 8; i++) s->ic[i] = 0.0f;
        s->rk[0] = -1.0f; s->rk[1] = -1.0f;      /* no knob is -1: the first block works everything out */
        s->a1 = 0.0f; s->a2 = 0.0f; s->a3 = 0.0f; s->cb = 0.0f;
        s->magic = KK_MAGIC;
    }

    r0 = params[KICKKILL_KILL_SLOT]; r1 = params[KICKKILL_MIX_SLOT];
    if (r0 != s->rk[0]) {                        /* Kill moved: new coefficients (OFF keeps the last for the ramp-out) */
        s->u[0] = kk_ui(r0, (float)KICKKILL_KILL_UI_DEFAULT, 281.0f);
        s->rk[0] = r0;
        if (s->u[0] > 0.5f) {
            const float g = kk_g(19.0f + s->u[0]);   /* 1 -> 20 Hz ... 281 -> 300 Hz */
            const float a1 = kk_recip(1.0f + g * (g + KK_K0));
            s->a1 = a1; s->a2 = g * a1; s->a3 = g * s->a2; s->cb = -KK_K0;
        }
    }
    if (r1 != s->rk[1]) {
        const float m = (s->u[1] = kk_ui(r1, (float)KICKKILL_MIX_UI_DEFAULT, 100.0f)) * 0.01f;
        s->dg = 2.0f - 2.0f * m; if (s->dg > 1.0f) s->dg = 1.0f;
        s->wg = 2.0f * m;        if (s->wg > 1.0f) s->wg = 1.0f;
        s->rk[1] = r1;
    }

    on = (params[0] >= 0.5f) && (s->u[0] > 0.5f);
    if (!on && s->e <= 0.0f) { s->act = 0u; goto done; }   /* off or Kill OFF: input untouched */
    e0 = s->e;
    if (on) { s->e += KK_FADE; if (s->e > 1.0f) s->e = 1.0f; }
    else    { s->e -= KK_FADE; if (s->e < 0.0f) s->e = 0.0f; }
    de = (s->e - e0) * 0.125f;
    {
        const float dg = s->dg, wg = s->wg;
        float d[16];
        if (dg == 0.0f && wg == 1.0f && e0 >= 1.0f && de == 0.0f) {   /* Mix 100, on: no dry pass */
            kk_filter(s, buf, !s->act);
        } else {
            /* d + e (dg d + wg wet - d) as two gains that ramp with e; kept as d + (...) like DJFilter */
            float ga = e0 * (dg - 1.0f), gb = e0 * wg;
            const float dga = de * (dg - 1.0f), dgb = de * wg;
            for (i = 0; i < 16; i++) d[i] = buf[i];
            kk_filter(s, buf, !s->act);
            KK_BLOCK_LOOP()
            for (i = 0; i < 8; i++) {
                buf[i] = d[i] + (ga * d[i] + gb * buf[i]);
                buf[i + 8] = d[i + 8] + (ga * d[i + 8] + gb * buf[i + 8]);
                ga += dga; gb += dgb;
            }
        }
    }
    s->act = 1u;
done:
    za_seal(s, ZA_HDR(KkState), s->seal);        /* leave the arena safe (MIDISTOMP #60) */
}

#endif /* KICKKILL_HOST_TEST */
