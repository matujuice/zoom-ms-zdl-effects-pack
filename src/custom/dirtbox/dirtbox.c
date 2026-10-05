/*
 * dirtbox.c - "DirtBox": three distortion models in one effect, plus an automatic
 * ZNR-style noise reducer that leaves kick tails alone. Mono.
 *
 * MODELS (one Model knob)
 *   ACID  the built-in distortion of the Behringer TD-3 (the TB-303 clone; the
 *         original 303 has none), which is a copy of the Boss DS-1: a transistor
 *         booster (all frequencies, up to 5.4x here), an op-amp stage whose gain
 *         only applies above 72 Hz (4.7k / 0.47u leg, up to 22x), a 7.2 kHz
 *         low-pass (2.2k / 0.01u) into two silicon diodes to ground (hard,
 *         symmetric), then the DS-1 tone: a pot blending a 234 Hz low-pass
 *         (6.8k / 0.1u) with a 1.06 kHz high-pass, so the middle of the knob
 *         scoops the mids. Values from the DS-1 circuit as widely documented
 *         (not measured on a TD-3).
 *   RAT   after the ProCo RAT: the op-amp gain only boosts above about 70 Hz (the
 *         560R/4.7u and 1k/2.2u legs), the LM308 loses treble as the gain goes up
 *         (bandwidth about 800 kHz / gain), hard symmetric diode clip, then the
 *         RAT's Filter: a one-pole low-pass 475 Hz .. 16 kHz (Tone up = brighter,
 *         the reverse of the real Filter knob).
 *   METAL after the Boss MT-2 Metal Zone: two clipping stages (60x then 6x), lows
 *         under 100 Hz kept out of the gain so it stays tight, a fixed -9 dB mid
 *         scoop at 750 Hz after the clip, Tone = low-pass 1.2 .. 12 kHz.
 *   Each is a sketch of the circuit's character, not a component-level model.
 *   No oversampling (too heavy for the pedal): the smooth clip curve and the
 *   models' own low-passes keep the aliasing down, but high notes at full Drive
 *   alias a little.
 *
 * SIGNAL PATH (per sample)
 *   lpH = one-pole low-pass of the input (corner fh), h = in - lpH
 *   v   = lowG * lpH + G * h            (bass gets less gain than the rest)
 *   v   -> one-pole low-pass (fl)       (RAT: fl follows the gain)
 *   y   = clip(v)   METAL: y = clip(6 y) again
 *         clip(v) = c * S(v / c), S(u) = u / (1 + u^4)^(1/4): a diode-like knee,
 *         ceiling cp above zero and cn below
 *   y   -> DC blocker -> mid peak/scoop (METAL) -> Tone
 *   wet = y * makeup * Level * ZNR gain     out = DJ crossfade of in and wet
 *   Makeup = REF / min(G * REF, ceiling): with an input peaking at REF (0.2, about
 *   -14 dBFS) the wet peak stays near REF whatever the Drive, so turning Drive up
 *   adds dirt, not mostly volume. Level 50 = that level.
 *   Turning Model mutes the wet for a moment and fades it back in over ~6 ms.
 *
 * ZNR (automatic noise reducer, wet path only; the dry path is never touched)
 *   - The detector listens to the clean input, not the distorted signal, so it
 *     sees the real decay of a kick, not the squashed one.
 *   - Noise floor: the input envelope (instant attack, 50 ms release) is tracked
 *     by a minimum follower that drops quickly in quiet moments and creeps up by
 *     about 2.4 dB/s otherwise. It is capped at -60 dBFS: in a track that never
 *     goes quiet the floor sits at the cap, so the threshold can never climb into
 *     the music.
 *   - Threshold = floor x margin, margin 2^(1 + 3 z): +6 dB at ZNR 1 .. +24 dB at
 *     ZNR 100, +15 dB at the default 50 (so at most -45 dBFS by default).
 *   - Above the threshold the wet passes untouched; 50 ms after the input last
 *     crossed it, the gain eases down as a 1:3 downward expander (gain =
 *     (env / threshold)^2), released over ~150 ms. A kick tail therefore fades
 *     the way it does on the input instead of being cut at a fixed point, and
 *     anything that comes back above the threshold opens it in under 1 ms.
 *   - ZNR 0 = off.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no switch,
 * no float or integer division, no libm, no double, every helper forced inline.
 *
 * KNOBS (screen values)
 *   0 Model 0..2    ACID / RAT / METAL
 *   1 Drive 0..100  gain 0.5 x (2 Gmax)^(n/100): 0.5x .. Gmax (ACID 120, RAT 300,
 *                   METAL 60 into the 6x second stage)
 *   2 Tone  0..100  per model, see MODELS; 100 = brightest on all three
 *   3 ZNR   0..100  noise reducer margin above the measured noise floor, 0 = off
 *   4 Level 0..100  wet level 0 .. 2x, 50 = about the input level (see Makeup)
 *   5 Mix   0..100  dry/wet, DJ-style: dry full up to 50, wet full from 50
 *                   (50 = parallel distortion: the clean kick stays under the dirt)
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define DB_DO_PRAGMA(x) _Pragma(#x)
#define DB_EXPAND_PRAGMA(x) DB_DO_PRAGMA(x)
#define DB_ALWAYS_INLINE(fn) DB_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define DB_CODE_SECTION(fn) DB_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define DB_ALWAYS_INLINE(fn)
#define DB_CODE_SECTION(fn)
#endif

#define DB_MAGIC        0x44423032u          /* "DB02" */
#define DB_W            0.00014247585f       /* 2 pi / 44100: Hz -> radians per sample */
#define DB_DC_R         0.9993f              /* DC blocker pole                  */
#define DB_REF          0.2f                 /* makeup reference input peak      */
#define DB_FADE         0.004f               /* wet fade-in after a Model change (~6 ms) */
#define DB_ENV_REL      0.99954658f          /* ZNR envelope release, 50 ms      */
#define DB_NF_DROP      0.05f                /* noise floor follows quiet moments, per block */
#define DB_NF_RISE      1.00005f             /* .. and creeps up 2.4 dB/s otherwise */
#define DB_NF_MIN       0.00001f             /* -100 dBFS                        */
#define DB_NF_MAX       0.001f               /* -60 dBFS: the threshold never climbs into music */
#define DB_HOLD         2205                 /* 50 ms before the expander starts */
#define DB_G_ATT        0.05f                /* ZNR gain opens in under 1 ms     */
#define DB_G_REL        0.000151f            /* .. and closes over ~150 ms       */
/* METAL's mid scoop: Chamberlin state-variable filter, 750 Hz, Q 0.7, -9 dB */
#define DB_MID_F        0.106806f            /* 2 sin(pi 750 / 44100)            */
#define DB_MID_Q        1.428571f            /* 1 / Q                            */
#define DB_MID_K        (-0.921718f)         /* (10^(-9/20) - 1) / Q             */

typedef struct {
    unsigned int magic;
    int model;             /* last model, to fade in after a change  */
    float lpH, lpA;        /* drive split, pre-clip low-pass          */
    float dcx, dcy;        /* DC blocker                              */
    float lpS, bpS;        /* mid filter                              */
    float lpT, lpU;        /* tone: low-pass and the high-pass's pole */
    float fade;            /* wet fade-in after a Model change        */
    float env, nf, gz;     /* ZNR: input envelope, noise floor, gain  */
    int hold;              /* ZNR hold counter, samples               */
} DbState;

typedef struct {
    int model, stage2, znr;
    float aH, lowG, G, aL;     /* drive split and pre-clip low-pass      */
    float cp, icp, cn, icn;    /* clip ceilings and their inverses       */
    float midK;                /* mid peak (0 = flat)                    */
    float aT, aU, wl, wh;      /* tone: wl x LP(aT) + wh x HP(aU)        */
    float wetScale;            /* makeup x Level                         */
    float margin;              /* ZNR threshold / noise floor            */
    float dryG, wetG;
} DbParams;

DB_ALWAYS_INLINE(db_clamp01)
static inline float db_clamp01(float x)
{
    if (!(x >= 0.0f)) x = 0.0f;                  /* also catches NaN */
    if (x > 1.0f) x = 1.0f;
    return x;
}

/* 1 / sqrt(x) for x > 0, no libm, no divide: exponent trick + three Newton steps. */
DB_ALWAYS_INLINE(db_rsqrt)
static inline float db_rsqrt(float x)
{
    union { float f; unsigned int u; } c;
    float y, h = 0.5f * x;
    c.f = x;
    c.u = 0x5f3759dfu - (c.u >> 1);
    y = c.f;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return y;
}

/* 1 / x for x > 0, as rsqrt squared (no divide). */
DB_ALWAYS_INLINE(db_inv)
static inline float db_inv(float x)
{
    float r = db_rsqrt(x);
    return r * r;
}

/* 2^x for x in 0..10: whole octaves by doubling, the fraction by a Taylor series
 * of e^(f ln2) (error < 2e-4). */
DB_ALWAYS_INLINE(db_exp2)
static inline float db_exp2(float x)
{
    int n;
    float f, e, r;
    if (!(x > 0.0f)) x = 0.0f;
    if (x > 10.0f) x = 10.0f;
    n = (int)x;
    f = x - (float)n;
    e = f * 0.69314718f;
    r = 1.0f + e * (1.0f + e * (0.5f + e * (0.16666667f + e * (0.041666668f + e * 0.008333334f))));
    for (; n > 0; n--) r += r;
    return r;
}

/* One-pole coefficient 1 - e^(-2 pi fc / fs), fc up to ~16 kHz: e^(-w/8) by a
 * Taylor series, then squared three times. */
DB_ALWAYS_INLINE(db_pole)
static inline float db_pole(float hz)
{
    float w = hz * DB_W * 0.125f, e;
    e = 1.0f - w * (1.0f - w * (0.5f - w * (0.16666667f - w * 0.041666668f)));
    e = e * e; e = e * e; e = e * e;
    return 1.0f - e;
}

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer, then scale by 1/max. */
DB_ALWAYS_INLINE(db_knob)
static inline float db_knob(float raw, float def_ui, float inv_max)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.05f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    return db_clamp01(ui * inv_max);
}

/* Diode-like clip: c * S(v / c), S(u) = u (1 + u^4)^(-1/4). Slope 1 at zero, flat
 * at c, a knee between soft and hard. */
DB_ALWAYS_INLINE(db_clip)
static inline float db_clip(float v, float c, float ic)
{
    float u = v * ic, u2 = u * u, r;
    r = db_rsqrt(1.0f + u2 * u2);                /* (1 + u^4)^(-1/2) */
    return c * u * (r * db_rsqrt(r));            /* x sqrt(r)        */
}

DB_ALWAYS_INLINE(db_init)
static inline void db_init(DbState *s, const DbParams *P)
{
    s->model = P->model;
    s->lpH = 0.0f; s->lpA = 0.0f; s->dcx = 0.0f; s->dcy = 0.0f;
    s->lpS = 0.0f; s->bpS = 0.0f; s->lpT = 0.0f; s->lpU = 0.0f;
    s->fade = 0.0f;
    s->env = 0.0f; s->nf = 0.0001f; s->gz = 1.0f; s->hold = 0;
    s->magic = DB_MAGIC;
}

/* k[] = 0..1 by each knob's own maximum, in manifest order */
DB_ALWAYS_INLINE(db_prepare)
static inline void db_prepare(DbParams *P, const float *k)
{
    float l2, gt, trim, t = k[2];
    int m = (int)(k[0] * 2.0f + 0.5f);
    P->model = m;
    P->stage2 = 0;
    P->midK = 0.0f;
    P->wl = 1.0f; P->wh = 0.0f; P->aU = 0.1f;
    trim = 1.0f;
    if (m == 0) {                                /* ACID: TD-3 = DS-1 */
        l2 = 7.9069f;                            /* log2(2 x 120): booster 5.4 x op-amp 22 */
        P->aH = db_pole(72.0f);
        P->cp = 0.6f; P->cn = 0.6f;
        P->aL = db_pole(7200.0f);
        P->aT = db_pole(234.0f);                 /* tone pot: LP 234 Hz <-> HP 1.06 kHz */
        P->aU = db_pole(1060.0f);
        P->wl = 1.0f - t; P->wh = t;
        trim = 2.0f;                             /* the tone stack's loss, measured in tests */
    } else if (m == 1) {                         /* RAT */
        l2 = 9.2288f;                            /* log2(2 x 300) */
        P->aH = db_pole(70.0f);
        P->cp = 0.55f; P->cn = 0.55f;
        P->aT = db_pole(475.0f * db_exp2(t * 5.07f));      /* 475 Hz .. 16 kHz */
    } else {                                     /* METAL */
        l2 = 6.9069f;                            /* log2(2 x 60)  */
        P->stage2 = 1;
        P->aH = db_pole(100.0f);
        P->cp = 0.5f; P->cn = 0.5f;
        P->aL = db_pole(6000.0f);
        P->midK = DB_MID_K;
        P->aT = db_pole(1200.0f * db_exp2(t * 3.32f));     /* 1.2 .. 12 kHz */
    }
    P->icp = db_inv(P->cp); P->icn = db_inv(P->cn);
    P->G = 0.5f * db_exp2(k[1] * l2);            /* 0.5x .. Gmax */
    if (m == 1) {
        float fl = 800000.0f * db_inv(P->G);     /* LM308: bandwidth = GBW / gain */
        if (fl > 16000.0f) fl = 16000.0f;
        P->aL = db_pole(fl);
        P->lowG = 1.0f;                          /* below the legs' corner the gain is 1 */
    } else {
        P->lowG = P->G * ((m == 0) ? 0.045f : 0.1f);   /* ACID: booster gain only (1 / 22) */
    }
    gt = P->G * (P->stage2 ? 6.0f : 1.0f) * DB_REF;
    if (gt > 0.5f * (P->cp + P->cn)) gt = 0.5f * (P->cp + P->cn);   /* expected clip peak */
    P->wetScale = DB_REF * db_inv(gt) * (k[4] + k[4]) * trim;
    P->znr = (k[3] > 0.0f);
    P->margin = db_exp2(1.0f + 3.0f * k[3]);
    P->dryG = 2.0f - 2.0f * k[5]; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * k[5];        if (P->wetG > 1.0f) P->wetG = 1.0f;
}

DB_ALWAYS_INLINE(db_process)
static inline void db_process(DbState *s, const DbParams *P, float *buf, int n)
{
    int i, hold = s->hold;
    float lpH = s->lpH, lpA = s->lpA, dcx = s->dcx, dcy = s->dcy;
    float lpS = s->lpS, bpS = s->bpS, lpT = s->lpT, lpU = s->lpU, fade = s->fade;
    float env = s->env, nf = s->nf, gz = s->gz, invThr;

    if (P->model != s->model) { s->model = P->model; fade = 0.0f; }
    invThr = db_inv(nf * P->margin);

    for (i = 0; i < n; i++) {
        float x = buf[i], ax = (x < 0.0f) ? -x : x, h, v, y, d;
        /* ZNR detector on the clean input */
        env *= DB_ENV_REL;
        if (ax > env) env = ax;
        /* drive: the bass under the corner gets lowG, the rest G */
        lpH += P->aH * (x - lpH);
        h = x - lpH;
        v = P->lowG * lpH + P->G * h;
        lpA += P->aL * (v - lpA);
        if (lpA >= 0.0f) y = db_clip(lpA, P->cp, P->icp);
        else             y = db_clip(lpA, P->cn, P->icn);
        if (P->stage2) y = db_clip(6.0f * y, P->cp, P->icp);
        d = y - dcx + DB_DC_R * dcy;                     /* DC blocker */
        dcx = y; dcy = d;
        if (P->stage2) {                                 /* METAL mid scoop */
            float hp;
            lpS += DB_MID_F * bpS;
            hp = d - lpS - DB_MID_Q * bpS;
            bpS += DB_MID_F * hp;
            d += P->midK * bpS;
        }
        lpT += P->aT * (d - lpT);                        /* Tone */
        lpU += P->aU * (d - lpU);
        d = P->wl * lpT + P->wh * (d - lpU);
        if (P->znr) {
            float r = env * invThr, tgt;
            if (r >= 1.0f) { hold = DB_HOLD; tgt = 1.0f; }
            else if (hold > 0) { hold--; tgt = 1.0f; }
            else tgt = r * r;                            /* 1:3 downward expander */
            gz += ((tgt > gz) ? DB_G_ATT : DB_G_REL) * (tgt - gz);
        } else {
            gz = 1.0f;
        }
        fade += DB_FADE * (1.0f - fade);
        buf[i] = P->dryG * x + P->wetG * (d * P->wetScale * gz * fade);
    }

    /* noise floor: follow the envelope down quickly, creep up slowly */
    if (env < nf) nf += DB_NF_DROP * (env - nf);
    else nf *= DB_NF_RISE;
    if (nf < DB_NF_MIN) nf = DB_NF_MIN;
    if (nf > DB_NF_MAX) nf = DB_NF_MAX;

    if (lpH < 1e-15f && lpH > -1e-15f) lpH = 0.0f;       /* denormals */
    if (lpA < 1e-15f && lpA > -1e-15f) lpA = 0.0f;
    if (dcy < 1e-15f && dcy > -1e-15f) dcy = 0.0f;
    if (lpS < 1e-15f && lpS > -1e-15f) lpS = 0.0f;
    if (bpS < 1e-15f && bpS > -1e-15f) bpS = 0.0f;
    if (lpT < 1e-15f && lpT > -1e-15f) lpT = 0.0f;
    if (lpU < 1e-15f && lpU > -1e-15f) lpU = 0.0f;
    if (env < 1e-15f) env = 0.0f;
    if (gz < 1e-15f) gz = 0.0f;
    s->lpH = lpH; s->lpA = lpA; s->dcx = dcx; s->dcy = dcy;
    s->lpS = lpS; s->bpS = bpS; s->lpT = lpT; s->lpU = lpU; s->fade = fade;
    s->env = env; s->nf = nf; s->gz = gz; s->hold = hold;
}

/* ---- on-screen text ------------------------------------------------------ */
/* knob index 0 = Model: 0 ACID, 1 RAT, 2 METAL */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 2u) { out[0] = 'M'; out[1] = 'E'; out[2] = 'T'; out[3] = 'A'; out[4] = 'L'; out[5] = 0; return 5; }
    if (value == 1u) { out[0] = 'R'; out[1] = 'A'; out[2] = 'T'; out[3] = 0; return 3; }
    out[0] = 'A'; out[1] = 'C'; out[2] = 'I'; out[3] = 'D'; out[4] = 0;
    return 4;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef DIRTBOX_HOST_TEST

#include "dirtbox_params.h"

#ifndef DIRTBOX_AUDIO_FUNC
#define DIRTBOX_AUDIO_FUNC Fx_DLY_DirtBox
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

DB_CODE_SECTION(DIRTBOX_AUDIO_FUNC)
void DIRTBOX_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    DbState *s;
    DbParams P;
    float k[6];
    int i;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    if (params[0] < 0.5f) return;                /* effect bypassed            */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(DbState) || span < (end - base)) return;
    if (stateBase + sizeof(DbState) > end) return;

    s = (DbState *)stateBase;

    k[0] = db_knob(params[DIRTBOX_MODEL_SLOT], (float)DIRTBOX_MODEL_UI_DEFAULT, 0.5f);
    k[1] = db_knob(params[DIRTBOX_DRIVE_SLOT], (float)DIRTBOX_DRIVE_UI_DEFAULT, 0.01f);
    k[2] = db_knob(params[DIRTBOX_TONE_SLOT],  (float)DIRTBOX_TONE_UI_DEFAULT,  0.01f);
    k[3] = db_knob(params[DIRTBOX_ZNR_SLOT],   (float)DIRTBOX_ZNR_UI_DEFAULT,   0.01f);
    k[4] = db_knob(params[DIRTBOX_LEVEL_SLOT], (float)DIRTBOX_LEVEL_UI_DEFAULT, 0.01f);
    k[5] = db_knob(params[DIRTBOX_MIX_SLOT],   (float)DIRTBOX_MIX_UI_DEFAULT,   0.01f);

    db_prepare(&P, k);
    if (s->magic != DB_MAGIC) db_init(s, &P);
    db_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* DIRTBOX_HOST_TEST */
