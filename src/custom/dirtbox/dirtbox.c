/*
 * dirtbox.c - "DIRTBOX": a distortion box for an acid machine (TB-303 / TD-3 lines
 * into the pedal). Seven pedal circuits on one Model knob, mildest first, a 3-band
 * EQ tuned for acid, and an automatic ZNR-style noise reducer. Mono.
 *
 * MODELS (Model knob, least to most aggressive). Each follows the real circuit
 * stage by stage, from a published analysis or a circuit-level digital model; the
 * component values are in the comments next to the numbers. 1.0 in the pedal's
 * audio = 1 V into the circuit. Drive is always the pedal's own gain pot.
 *   TS9   Ibanez TS9 Tube Screamer, after guitarix's ts9sim (Kenez's analysis of the
 *         TS9): op-amp gain 1 + Drive / 4.7k above 720 Hz (4.7k / 0.047u), Drive =
 *         51k + 500k audio pot, 51p across it, 1N914 pair in the feedback (soft clip
 *         added to the clean signal; guitarix's measured curve, fitted); tone: the
 *         723 Hz low-pass (1k / 0.22u) with the treble control blending the top back.
 *   DIST+ MXR Distortion+, after the ElectroSmash analysis and guitarix's mxrdist:
 *         741 gain 1 + 1M / (Dist + 4.7k), 0.047u in that leg (corner 3 Hz .. 720 Hz),
 *         the 741's ~1 MHz bandwidth, then 10k into two germanium diodes (1N270,
 *         Is 0.25 uA, n 1.3) with 1n across them. The pedal has no tone knob: Tone is
 *         a gentle low-pass, 800 Hz .. 16 kHz.
 *   DS-1  the built-in distortion of the Behringer TD-3, a copy of the Boss DS-1
 *         (ElectroSmash analysis, DS1.lv2): 35 dB booster above 33 Hz running out of
 *         swing near 4 V, op-amp 1 + Dist / 4.7k above 72 Hz with 100p across Dist,
 *         9 V rails, 2.2k / 0.01u into two 1N4148s, and the DS-1 tone (234 Hz
 *         low-pass blended with a 1.06 kHz high-pass; noon scoops ~500 Hz).
 *   RAT2  ProCo RAT 2 (same gain stage and clipper as the RAT), after Rudro085's
 *         Proco-Rat nodal model: 1 + Dist / 560R (4.7u) + Dist / 47R (2.2u), Dist =
 *         100k audio pot, 100p across it, the LM308's ~1 MHz bandwidth, 9 V rails,
 *         1k into two 1N914s, Filter 1.5k + 100k audio into 3.3n (Tone up = brighter).
 *   MUFF  Electro-Harmonix Big Muff Pi, guitarix's bmp (DK-method model of the
 *         circuit), filters evaluated at 44.1 kHz: input stage, Sustain stage (its
 *         filter computed from the pot each block), the transistor clipper curve,
 *         second gain stage, the same clipper, the Big Muff tone stack (Tone 0 = the
 *         low-pass side, 100 = the high-pass side, noon = the famous mid scoop) and
 *         the circuit's output roll-off.
 *   SFUZZ Univox Super-Fuzz, from the schematic (GGG layout): high-gain preamp
 *         (Expander = Drive), phase splitter into the push-push doubler (full-wave
 *         rectifier: octave up, some fundamental left), coupling cap, two germanium
 *         diodes. Tone = the Tone switch made continuous: 0 = the 1 kHz scoop fully
 *         in (fat, bassy), 100 = switch off. No circuit-level digital model was
 *         found for this one: stages and values follow the schematic.
 *   MT-2  Boss MT-2 Metal Zone, guitarix's MetalTone (DK-method model): +25 dB
 *         pre-filter at ~900 Hz, Dist 1.2x .. 55x with its treble shelf and closing
 *         low-pass, the clipper curve (0.575 ceiling), the post filter (+5 dB at
 *         100 Hz, +11 dB at 4.6 kHz). Tone = the High knob, -20..+20 dB above ~2 kHz.
 *   Diode and transistor curves: y = c S(g v / c) + m v / (1 + h |v|), S(u) = u /
 *   (1 + u^4)^(1/4), fitted per circuit (within 2 % of full scale; MT-2 3 %, MXR
 *   op-amp knee replaced by the 741's rails). Not oversampled (too heavy for the
 *   pedal), so high notes at full Drive alias a little.
 *
 * EQ (after the pedal, acid voicing): Low shelf 110 Hz (the 303's body), Mid peak
 *   1 kHz Q 0.8 (the squelch of the resonance), High shelf 4.5 kHz (the fizz),
 *   each -12 .. +12 dB, 0 = flat (RBJ biquads).
 *
 * LEVEL: Makeup = REF / min(gain x REF, ceiling) x trim: an input peaking at REF
 *   (0.2, about -14 dBFS) comes out near the same level at any Drive (trim per
 *   model, set in the host test). Level 50 = that level.
 *   Turning Model clears the filters, mutes the wet and fades it in over ~6 ms.
 *
 * ZNR (automatic noise reducer, wet path only; the dry path is never touched),
 * tuned for 303 lines: 16th notes, accents, slides, rests between steps.
 *   - The detector listens to the clean input, so it follows the real decay of a
 *     note, not the squashed one.
 *   - Noise floor: the input envelope (instant attack, 50 ms release) is tracked by
 *     a minimum follower that drops quickly in quiet moments and creeps up by about
 *     2.4 dB/s otherwise, capped at -60 dBFS so the threshold never climbs into the
 *     music.
 *   - Threshold = floor x margin, margin 2^(1 + 3 z): +6 dB at ZNR 1 .. +24 dB at
 *     ZNR 100, +15 dB at the default 50.
 *   - Above the threshold the wet passes untouched; 30 ms after the input last
 *     crossed it, the gain eases down as a 1:3 downward expander (gain =
 *     (env / threshold)^2) over ~80 ms: fast enough to clear the hiss in a rest
 *     between 16ths at 140 BPM, while a note's tail still fades as on the input.
 *     Anything back above the threshold opens it in under 1 ms. ZNR 0 = off.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no switch,
 * no float or integer division, no libm, no double, every helper forced inline.
 *
 * SIZE. The first build of the seven models came to 36 KB and the pedal hung on
 * the boot screen; it booted again as soon as the file was removed (Luca,
 * 2026-10-06). Choral, 30.9 KB, is the largest file that boots. So the code is
 * written to stay small as well as fast: the pedal's seven models share one copy
 * of every expensive helper rather than inlining their own. The corner
 * frequencies are kept in Hz through ab_prepare and converted by one group of
 * ab_pole calls at the end; the two 2^x each model needs are computed once
 * before the model branches; the three EQ bands run through one loop; and the
 * nine knobs are read in one loop over consecutive slots. See docs/SAFE-DSP-RULES.md.
 *
 * KNOBS (screen values)
 *   0 Model 0..6    TS9 / DIST+ / DS-1 / RAT2 / MUFF / SFUZZ / MT-2, default RAT2
 *   1 Drive 0..100  the pedal's own gain pot (Drive, Distortion, Dist, Sustain,
 *                   Expander), 0 = fully left. Only RAT2 is nearly clean at 0.
 *   2 Tone  0..100  the pedal's tone control (see MODELS); 100 = brightest. Default 80
 *   3 Low   0..24   -12 .. +12 dB shelf at 110 Hz, 12 = flat
 *   4 Mid   0..24   -12 .. +12 dB peak at 1 kHz, 12 = flat
 *   5 High  0..24   -12 .. +12 dB shelf at 4.5 kHz, 12 = flat
 *   6 ZNR   0..100  noise reducer margin above the measured noise floor, 0 = off
 *   7 Level 0..100  wet level 0 .. 2x, 50 = about the input level (see LEVEL)
 *   8 Mix   0..100  dry/wet, DJ-style: dry full up to 50, wet full from 50
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define AB_DO_PRAGMA(x) _Pragma(#x)
#define AB_EXPAND_PRAGMA(x) AB_DO_PRAGMA(x)
#define AB_ALWAYS_INLINE(fn) AB_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define AB_CODE_SECTION(fn) AB_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define AB_NO_UNROLL AB_DO_PRAGMA(UNROLL(1))
#else
#define AB_ALWAYS_INLINE(fn)
#define AB_CODE_SECTION(fn)
#define AB_NO_UNROLL
#endif

#define AB_MAGIC        0x41423031u          /* "AB01" */
#define AB_W            0.00014247585f       /* 2 pi / 44100: Hz -> radians per sample */
#define AB_DC_R         0.9993f              /* DC blocker pole                  */
#define AB_REF          0.2f                 /* makeup reference input peak      */
#define AB_FADE         0.004f               /* wet fade-in after a Model change (~6 ms) */
#define AB_ENV_REL      0.99954658f          /* ZNR envelope release, 50 ms      */
#define AB_NF_DROP      0.05f                /* noise floor follows quiet moments, per block */
#define AB_NF_RISE      1.00005f             /* .. and creeps up 2.4 dB/s otherwise */
#define AB_NF_MIN       0.00001f             /* -100 dBFS                        */
#define AB_NF_MAX       0.001f               /* -60 dBFS: the threshold never climbs into music */
#define AB_HOLD         1323                 /* 30 ms before the expander starts */
#define AB_G_ATT        0.05f                /* ZNR gain opens in under 1 ms     */
#define AB_G_REL        0.000283f            /* .. and closes over ~80 ms        */
#define AB_FMAX         16000.0f             /* highest corner ab_pole handles   */

#define M_TS9   0
#define M_DIST  1
#define M_DS1   2
#define M_RAT   3
#define M_MUFF  4
#define M_SFUZZ 5
#define M_MT2   6

/* MT-2 filters from guitarix MetalTone, evaluated at 44.1 kHz and split into
 * biquads (b0 b1 b2 / a1 a2). Pre-filter p1 (DC block p0 is our DC blocker's job): */
#define MT_A_B0  0.08033098f
#define MT_A_B1  0.11920252f
#define MT_A_B2  0.03887154f
#define MT_A_A1 (-0.31981578f)
#define MT_B_B1 (-0.4018711f)
#define MT_B_A1 (-1.91582123f)
#define MT_B_A2  0.93307778f
#define MT_C_B1 (-1.99623994f)
#define MT_C_B2  0.99623994f
#define MT_C_A1 (-1.89294488f)
#define MT_C_A2  0.894326f
/* .. post filter p3 (its third section is a 1.6 Hz DC blocker: our DC blocker) */
#define MT_D_B0  1.49892177f
#define MT_D_B1 (-1.56357181f)
#define MT_D_B2  0.42906255f
#define MT_D_A1 (-1.36107743f)
#define MT_D_A2  0.71247088f
#define MT_E_B1 (-1.99045965f)
#define MT_E_B2  0.99065458f
#define MT_E_A1 (-1.99658585f)
#define MT_E_A2  0.99678836f
#define MT_SHELF 0.2763f                     /* Dist stage treble: 629 Hz / 2277 Hz */

/* Big Muff from guitarix bmp at 44.1 kHz. Input stage: -15 dB, flat in band. */
#define BM_IN    0.1786f
/* second gain stage (bpmamp2) */
#define BM_A_B0 (-2.051823554f)
#define BM_A_B1 (-0.035029699f)
#define BM_A_B2  2.086853253f
#define BM_A_A1 (-1.790005285f)
#define BM_A_A2  0.792306471f
/* output roll-off (its other section cancels at Nyquist) */
#define BM_O_B0  0.183016397f
#define BM_O_B1 (-0.009880496f)
#define BM_O_B2 (-0.173135901f)
#define BM_O_A1 (-1.593095615f)
#define BM_O_A2  0.595572816f

/* Super-Fuzz Tone switch: 1 kHz scoop, Chamberlin state-variable filter, Q 0.7 */
#define SF_F     0.142364f                   /* 2 sin(pi 1000 / 44100)          */
#define SF_Q     1.428571f                   /* 1 / Q                           */
#define SF_K    (-1.25f)                     /* (10^(-18/20) - 1) / Q: -18 dB   */

/* EQ: cos and sin of each corner at 44.1 kHz (110 Hz shelf, 1 kHz Q 0.8, 4.5 kHz) */
#define EQ_LO_C  0.999877191f
#define EQ_LO_AL 0.011081567f                /* sin / 2 x sqrt 2 (shelf slope 1) */
#define EQ_MI_C  0.989867473f
#define EQ_MI_AL 0.088746449f                /* sin / (2 Q)                      */
#define EQ_HI_C  0.801413622f
#define EQ_HI_AL 0.422928012f

typedef struct {
    unsigned int magic;
    int model;             /* last model, to clear the filters after a change */
    float h1, h2, fb, lc;  /* one-pole states: high-pass legs, gain roll-off, pre-clip */
    float dcx, dcy;        /* DC blocker                              */
    float t1, t2;          /* tone                                    */
    float qa1, qa2, qb1, qb2, qc1, qc2, qd1, qd2, qe1, qe2;   /* model biquads */
    float el1, el2, em1, em2, eh1, eh2;                       /* EQ biquads    */
    float fade;            /* wet fade-in after a Model change        */
    float env, nf, gz;     /* ZNR: input envelope, noise floor, gain  */
    int hold;              /* ZNR hold counter, samples               */
} AbState;

typedef struct {
    int model, znr;
    float pre, a1, a2;         /* input gain; high-pass legs' poles             */
    float gA, gB, aF, aC, rail;   /* leg gains, gain roll-off pole, pre-clip pole, rails */
    float c, ic, cg, cm, ch, cap; /* clipper curve                              */
    float u0, u1, u2, u3, u4;  /* Muff: Sustain stage biquad                    */
    float v0, v1, v2, v3, v4;  /* Muff: tone stack biquad                       */
    float aT, aU, wl, wh, sk;  /* tone                                          */
    float eq[15];              /* EQ biquads: low, mid, high; b0 b1 b2 a1 a2 each */
    float wetScale;            /* makeup x Level                                */
    float margin;              /* ZNR threshold / noise floor                   */
    float dryG, wetG;
} AbParams;

AB_ALWAYS_INLINE(ab_clamp01)
static inline float ab_clamp01(float x)
{
    if (!(x >= 0.0f)) x = 0.0f;                  /* also catches NaN */
    if (x > 1.0f) x = 1.0f;
    return x;
}

/* 1 / sqrt(x) for x > 0, no libm, no divide: exponent trick + three Newton steps
 * (filter coefficients need them: a shelf's DC gain hangs on tiny differences). */
AB_ALWAYS_INLINE(ab_rsqrt)
static inline float ab_rsqrt(float x)
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

/* The same with two steps (error < 5e-6), for the clipper curves in the sample
 * loop: shorter code, and the shape error is far below hearing. */
AB_ALWAYS_INLINE(ab_rsqrt2)
static inline float ab_rsqrt2(float x)
{
    union { float f; unsigned int u; } c;
    float y, h = 0.5f * x;
    c.f = x;
    c.u = 0x5f3759dfu - (c.u >> 1);
    y = c.f;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return y;
}

/* 1 / x for x > 0, no divide: exponent trick + three Newton steps (rel. error
 * < 2e-7). Shorter than squaring an rsqrt, and this file is called 15 times. */
AB_ALWAYS_INLINE(ab_inv)
static inline float ab_inv(float x)
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

/* 2^x for x in 0..10: whole octaves straight into the exponent field, the
 * fraction by a Taylor series of e^(f ln2) (error < 2e-4). */
AB_ALWAYS_INLINE(ab_exp2)
static inline float ab_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int n;
    float f, e;
    if (!(x > 0.0f)) x = 0.0f;
    if (x > 10.0f) x = 10.0f;
    n = (int)x;
    f = x - (float)n;
    e = f * 0.69314718f;
    c.u = ((unsigned int)(n + 127)) << 23;       /* 2^n, exponent field */
    return c.f * (1.0f + e * (1.0f + e * (0.5f + e * (0.16666667f + e * (0.041666668f + e * 0.008333334f)))));
}

/* One-pole coefficient 1 - e^(-2 pi fc / fs), fc up to 16 kHz (clamped): e^(-w/8)
 * by a Taylor series, then squared three times. */
AB_ALWAYS_INLINE(ab_pole)
static inline float ab_pole(float hz)
{
    float w, e;
    if (hz > AB_FMAX) hz = AB_FMAX;
    w = hz * AB_W * 0.125f;
    e = 1.0f - w * (1.0f - w * (0.5f - w * (0.16666667f - w * 0.041666668f)));
    e = e * e; e = e * e; e = e * e;
    return 1.0f - e;
}

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer, then scale by 1/max. */
AB_ALWAYS_INLINE(ab_knob)
static inline float ab_knob(float raw, float def_ui, float inv_max)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.05f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    return ab_clamp01(ui * inv_max);
}

/* Knee: c * S(v / c), S(u) = u (1 + u^4)^(-1/4). Slope 1 at zero, flat at c. */
AB_ALWAYS_INLINE(ab_clip)
static inline float ab_clip(float v, float c, float ic)
{
    float u = v * ic, u2 = u * u, r;
    r = ab_rsqrt2(1.0f + u2 * u2);               /* (1 + u^4)^(-1/2) */
    return c * u * (r * ab_rsqrt2(r));           /* x sqrt(r)        */
}

/* Clipper curve: y = c S(g v / c) + m v / (1 + h |v|), held inside +-cap. */
AB_ALWAYS_INLINE(ab_diode)
static inline float ab_diode(float v, const AbParams *P)
{
    float av = (v < 0.0f) ? -v : v, y;
    float r = ab_rsqrt2(1.0f + P->ch * av);
    y = ab_clip(P->cg * v, P->c, P->ic) + P->cm * v * (r * r);
    if (y > P->cap) y = P->cap;
    if (y < -P->cap) y = -P->cap;
    return y;
}

AB_ALWAYS_INLINE(ab_rail)
static inline float ab_rail(float v, float r)
{
    if (v > r) v = r;
    if (v < -r) v = -r;
    return v;
}

/* Biquad, transposed direct form II. */
AB_ALWAYS_INLINE(ab_bq)
static inline float ab_bq(float x, float *z1, float *z2,
                          float b0, float b1, float b2, float a1, float a2)
{
    float y = b0 * x + *z1;
    *z1 = b1 * x - a1 * y + *z2;
    *z2 = b2 * x - a2 * y;
    return y;
}

/* 10^(dB / 40) for dB in -12..+12 (the RBJ "A") */
AB_ALWAYS_INLINE(ab_eqA)
static inline float ab_eqA(float db)
{
    float A = ab_exp2(((db < 0.0f) ? -db : db) * 0.08304820f);
    return (db < 0.0f) ? ab_inv(A) : A;
}

/* RBJ shelf (slope 1): hi = 0 low shelf, 1 high shelf; writes b0 b1 b2 a1 a2 / a0 */
AB_ALWAYS_INLINE(ab_shelf)
static inline void ab_shelf(float A, float cw, float al, int hi,
                            float *b0, float *b1, float *b2, float *a1, float *a2)
{
    float sa = 2.0f * al * ab_rsqrt(A) * A;      /* 2 sqrt(A) alpha */
    float ap = A + 1.0f, am = A - 1.0f, s = hi ? -1.0f : 1.0f, ia0;
    ia0 = ab_inv(ap + s * am * cw + sa);
    *b0 = A * (ap - s * am * cw + sa) * ia0;
    *b1 = s * 2.0f * A * (am - s * ap * cw) * ia0;
    *b2 = A * (ap - s * am * cw - sa) * ia0;
    *a1 = -s * 2.0f * (am + s * ap * cw) * ia0;
    *a2 = (ap + s * am * cw - sa) * ia0;
}

AB_ALWAYS_INLINE(ab_clear)
static inline void ab_clear(AbState *s)
{
    s->h1 = 0.0f; s->h2 = 0.0f; s->fb = 0.0f; s->lc = 0.0f;
    s->dcx = 0.0f; s->dcy = 0.0f; s->t1 = 0.0f; s->t2 = 0.0f;
    s->qa1 = 0.0f; s->qa2 = 0.0f; s->qb1 = 0.0f; s->qb2 = 0.0f; s->qc1 = 0.0f;
    s->qc2 = 0.0f; s->qd1 = 0.0f; s->qd2 = 0.0f; s->qe1 = 0.0f; s->qe2 = 0.0f;
    s->fade = 0.0f;
}

AB_ALWAYS_INLINE(ab_init)
static inline void ab_init(AbState *s, const AbParams *P)
{
    s->model = P->model;
    ab_clear(s);
    s->el1 = 0.0f; s->el2 = 0.0f; s->em1 = 0.0f; s->em2 = 0.0f; s->eh1 = 0.0f; s->eh2 = 0.0f;
    s->env = 0.0f; s->nf = 0.0001f; s->gz = 1.0f; s->hold = 0;
    s->magic = AB_MAGIC;
}

/* k[] = 0..1 by each knob's own maximum, in manifest order */
AB_ALWAYS_INLINE(ab_prepare)
static inline void ab_prepare(AbParams *P, const float *k)
{
    float d = k[1], t = k[2], gpk, ceil, trim, fl, rd, ea = 0.0f, eb = 0.0f, e1, e2, au, gdb;
    int b;
    int m = (int)(k[0] * 6.0f + 0.5f);
    P->model = m;
    /* a1 a2 aF aC aT aU hold corner frequencies (Hz) until the end, where one
     * ab_pole each turns them into coefficients (one copy of the code, not one
     * per model: the pedal file must stay small). aC 0 = no RC (coefficient 1). */
    P->pre = 1.0f; P->gA = 0.0f; P->gB = 0.0f; P->rail = 4.5f;
    P->a1 = 700.0f; P->a2 = 700.0f; P->aF = AB_FMAX; P->aC = 0.0f; P->aT = 700.0f; P->aU = 700.0f;
    P->wl = 1.0f; P->wh = 0.0f; P->sk = 0.0f;
    P->u0 = 0.0f; P->u1 = 0.0f; P->u2 = 0.0f; P->u3 = 0.0f; P->u4 = 0.0f;
    P->v0 = 0.0f; P->v1 = 0.0f; P->v2 = 0.0f; P->v3 = 0.0f; P->v4 = 0.0f;
    /* every 2^x a model needs comes from these two ab_exp2 (one copy each) */
    gdb = 2.0f * t - 1.0f; gdb = gdb * gdb * gdb;               /* MT-2 High: +-20 dB, cubic */
    if (m == M_TS9 || m == M_RAT) ea = 6.643856f * d;           /* audio-taper Drive */
    if (m == M_DIST) { ea = 6.643856f * (1.0f - d); eb = 4.321928f * t; }
    if (m == M_RAT) eb = 3.321928f * (1.0f - t);
    if (m == M_SFUZZ) ea = 5.0f * d;
    if (m >= M_MT2) { ea = 4.328085f * d; eb = 3.321928f * ((gdb < 0.0f) ? -gdb : gdb); }
    e1 = ab_exp2(ea); e2 = ab_exp2(eb);
    au = (e1 - 1.0f) * 0.01010101f;                             /* audio taper (10^(2x) - 1) / 99 */
    if (m == M_TS9) {
        rd = 51000.0f + 500000.0f * au;          /* 51k + Drive (500k A) */
        P->a1 = 720.5f;                 /* 4.7k / 0.047u     */
        P->gA = rd * 0.00021276596f;             /* Drive / 4.7k      */
        P->aF = 3120700000.0f * ab_inv(rd);   /* 1 / (2 pi Drive 51p) */
        P->c = 0.32379f; P->cg = 0.96065f; P->cm = 0.02424f; P->ch = 0.13193f; P->cap = 0.501f;
        P->aT = 723.0f;                 /* 1k / 0.22u        */
        P->sk = 0.1f + 1.3f * t;                 /* treble control    */
        gpk = 1.0f + 0.7f * P->gA; ceil = 0.7f; trim = 2.0f;
    } else if (m == M_DIST) {
        float rg = 4700.0f + 1000000.0f * au;    /* Dist pot (1M, audio, reversed) + 4.7k */
        float ir = ab_inv(rg);
        P->a1 = 3386275.0f * ir;        /* 1 / (2 pi Rg 0.047u) */
        P->gA = 1000000.0f * ir;                 /* 1M / Rg           */
        P->aF = 1000000.0f * ab_inv(1.0f + P->gA);   /* 741: 1 MHz / gain */
        P->rail = 3.5f;
        P->aC = 15915.0f;               /* 10k / 1n          */
        P->c = 0.09437f; P->cg = 0.55241f; P->cm = 0.22582f; P->ch = 1.27299f; P->cap = 0.25f;
        P->aT = 800.0f * e2;      /* 800 Hz .. 16 kHz */
        gpk = 1.0f + P->gA; ceil = 0.24f; trim = 1.0f;
    } else if (m == M_DS1) {                     /* DS-1: TD-3 = DS-1 */
        rd = 100000.0f * d;                      /* Dist, 100k linear */
        P->pre = 56.0f;                          /* booster, 35 dB    */
        P->a1 = 33.0f;                  /* booster's input high-pass */
        P->a2 = 72.0f;                  /* 4.7k / 0.47u leg  */
        P->gA = rd * 0.00021276596f;             /* Dist / 4.7k       */
        fl = AB_FMAX;
        if (rd > 1000.0f) fl = 1591549431.0f * ab_inv(rd);   /* 1 / (2 pi Dist 100p) */
        P->aF = fl;
        P->aC = 7234.0f;                /* 2.2k / 0.01u      */
        P->c = 0.428f; P->cg = 0.8931f; P->cm = 0.1514f; P->ch = 0.6116f; P->cap = 1.0f;
        P->aT = 234.0f;                 /* tone: LP 6.8k / 0.1u */
        P->aU = 1063.0f;                /*       HP 1.06 kHz    */
        P->wl = 1.0f - t; P->wh = t;
        gpk = 56.0f * (1.0f + P->gA); ceil = 0.6f; trim = 2.8f;
    } else if (m == M_RAT) {                     /* RAT 2 */
        rd = 100000.0f * au;                     /* Dist, 100k audio  */
        P->a1 = 60.5f;                  /* 560R / 4.7u  */
        P->a2 = 1539.0f;                /* 47R / 2.2u   */
        P->gA = rd * 0.0017857143f;              /* Dist / 560   */
        P->gB = rd * 0.021276596f;               /* Dist / 47    */
        fl = 1000000.0f * ab_inv(1.0f + rd * 0.023049645f);  /* LM308: 1 MHz / (1 + Dist / (47 || 560)) */
        if (rd > 1000.0f) {
            float fc = 1591549431.0f * ab_inv(rd);           /* 100p across Dist */
            if (fc < fl) fl = fc;
        }
        P->aF = fl;
        P->c = 0.6076f; P->cg = 0.9516f; P->cm = 0.0935f; P->ch = 0.3322f; P->cap = 1.0f;
        {   /* Filter: 1.5k + 100k audio pot into 3.3n, Tone 100 = pot at 0 */
            float rt = 11111.11f * (e2 - 1.0f);   /* 100k (10^(1-t) - 1) / 9 */
            P->aT = 48228770.0f * ab_inv(1500.0f + rt);
        }
        gpk = 1.0f + P->gA + 0.5f * P->gB; ceil = 0.75f; trim = 1.0f;
    } else if (m == M_MUFF) {                    /* Big Muff Pi */
        float a0;
        a0 = ab_inv((-2.33381713f * d + 2.31047896f) * d + 0.277122037f);   /* Sustain */
        P->u0 = (-0.515564073f * d - 0.00515564073f) * a0;
        P->u1 = (-0.00880195285f * d - 0.0000880195285f) * a0;
        P->u2 = (0.524366026f * d + 0.00524366026f) * a0;
        P->u3 = ((4.41263852f * d - 4.36851214f) * d - 0.498400847f) * a0;
        P->u4 = ((-2.07882139f * d + 2.05803318f) * d + 0.221862813f) * a0;
        P->v0 = (0.713868866f * t + 0.0422535386f) * 1.16858433f;  /* tone, / a0 0.855734712 */
        P->v1 = (-1.44038138f * t + 0.00802871828f) * 1.16858433f;
        P->v2 = (0.713868866f * t - 0.0342248203f) * 1.16858433f;
        P->v3 = -1.42432395f * 1.16858433f;
        P->v4 = 0.588060456f * 1.16858433f;
        P->c = 0.44241f; P->cg = 0.10949f; P->cm = 0.30544f; P->ch = 0.85402f; P->cap = 0.795f;
        {   /* small-signal gain: Sustain stage at 300 Hz x the rest of the chain (~0.57) */
            float nr = P->u0 + P->u1 * 0.999086667f + P->u2 * 0.996348338f, ni = P->u1 * 0.042729744f + P->u2 * 0.085381434f;
            float dr = 1.0f + P->u3 * 0.999086667f + P->u4 * 0.996348338f, di = P->u3 * 0.042729744f + P->u4 * 0.085381434f;
            float n2 = nr * nr + ni * ni + 1e-12f;
            gpk = 0.57f * n2 * ab_rsqrt(n2) * ab_rsqrt(dr * dr + di * di + 1e-12f);
        }
        ceil = 0.795f; trim = 6.7f;
    } else if (m == M_SFUZZ) {                   /* Univox Super-Fuzz */
        P->pre = 10.0f * e1;      /* preamp, Expander: 10x .. 320x */
        P->a1 = 80.0f;                  /* input coupling    */
        P->a2 = 50.0f;                  /* doubler's coupling cap */
        P->c = 0.09437f; P->cg = 0.55241f; P->cm = 0.22582f; P->ch = 1.27299f; P->cap = 0.25f;
        P->sk = SF_K * (1.0f - t);               /* Tone switch, made continuous */
        gpk = 100.0f; ceil = 0.24f; trim = 1.0f;
    } else {                                     /* MT-2 */
        float D = (e1 - 1.0f) * 0.052396f;      /* guitarix LogPot(3, d) */
        float G = 1.21f + 53.8f * D, ig;
        P->pre = G;
        P->a1 = 629.2f;                 /* Dist stage treble shelf */
        fl = AB_FMAX;
        if (D > 0.3f) fl = 4800.0f * ab_inv(D);  /* .. and its closing low-pass */
        P->aF = fl;
        P->c = 0.0621f; P->cg = 100.0f; P->cm = 7.8705f; P->ch = 14.2186f; P->cap = 0.5745f;
        if (gdb >= 0.0f) {                                       /* e2 = 10^|gdb| */
            P->sk = e2 - 1.0f; P->aT = 7200.0f;
        } else {
            ig = ab_inv(e2);
            P->sk = ig - 1.0f; P->aT = 7200.0f * ig;
        }
        gpk = 18.0f * G; ceil = 0.5745f; trim = 0.62f;
    }
    P->a1 = ab_pole(P->a1); P->a2 = ab_pole(P->a2); P->aF = ab_pole(P->aF);
    P->aC = (P->aC > 0.0f) ? ab_pole(P->aC) : 1.0f;
    P->aT = ab_pole(P->aT); P->aU = ab_pole(P->aU);
    P->ic = ab_inv(P->c);
    gpk *= AB_REF;
    if (gpk > ceil) gpk = ceil;                  /* expected clip peak */
    P->wetScale = AB_REF * ab_inv(gpk) * (k[7] + k[7]) * trim;
    /* EQ: knob 0..24 -> -12..+12 dB. One loop over the bands keeps one copy of
     * the coefficient code (0 low shelf, 1 mid peak, 2 high shelf). */
    AB_NO_UNROLL
    for (b = 0; b < 3; b++) {
        float A = ab_eqA(24.0f * k[3 + b] - 12.0f), *q = P->eq + 5 * b;
        if (b == 1) {
            float iA = ab_inv(A), ia0 = ab_inv(1.0f + EQ_MI_AL * iA);
            q[0] = (1.0f + EQ_MI_AL * A) * ia0;
            q[1] = -2.0f * EQ_MI_C * ia0;
            q[2] = (1.0f - EQ_MI_AL * A) * ia0;
            q[3] = q[1];
            q[4] = (1.0f - EQ_MI_AL * iA) * ia0;
        } else {
            ab_shelf(A, b ? EQ_HI_C : EQ_LO_C, b ? EQ_HI_AL : EQ_LO_AL, b,
                     q, q + 1, q + 2, q + 3, q + 4);
        }
    }
    P->znr = (k[6] > 0.0f);
    P->margin = ab_exp2(1.0f + 3.0f * k[6]);
    P->dryG = 2.0f - 2.0f * k[8]; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * k[8];        if (P->wetG > 1.0f) P->wetG = 1.0f;
}

#define AB_FLUSH(v) if ((v) < 1e-15f && (v) > -1e-15f) (v) = 0.0f

AB_ALWAYS_INLINE(ab_process)
static inline void ab_process(AbState *s, const AbParams *P, float *buf, int n)
{
    int i, hold, m = P->model;
    float h1, h2, fb, lc, dcx, dcy, t1, t2, fade, env, nf, gz, invThr;

    if (m != s->model) { s->model = m; ab_clear(s); }
    h1 = s->h1; h2 = s->h2; fb = s->fb; lc = s->lc; dcx = s->dcx; dcy = s->dcy;
    t1 = s->t1; t2 = s->t2; fade = s->fade;
    env = s->env; nf = s->nf; gz = s->gz; hold = s->hold;
    invThr = ab_inv(nf * P->margin);

    for (i = 0; i < n; i++) {
        float x = buf[i], ax = (x < 0.0f) ? -x : x, v, g, y, d, z, add = 0.0f;
        /* ZNR detector on the clean input */
        env *= AB_ENV_REL;
        if (ax > env) env = ax;
        if (m == M_TS9) {                                /* feedback clipper: x + clip(gain part) */
            h1 += P->a1 * (x - h1);
            fb += P->aF * (P->gA * (x - h1) - fb);
            z = fb; add = x;
        } else if (m == M_MUFF) {
            v = ab_bq(BM_IN * x, &s->qa1, &s->qa2, P->u0, P->u1, P->u2, P->u3, P->u4);
            v = ab_diode(v, P);
            z = ab_bq(v, &s->qb1, &s->qb2, BM_A_B0, BM_A_B1, BM_A_B2, BM_A_A1, BM_A_A2);
        } else if (m == M_SFUZZ) {
            h1 += P->a1 * (x - h1);
            v = ab_clip(P->pre * (x - h1), 2.0f, 0.5f);  /* preamp swing ~2 V */
            v = 0.8f * ((v < 0.0f) ? -v : v) + 0.2f * v; /* push-push doubler */
            h2 += P->a2 * (v - h2);
            z = v - h2;                                  /* into the germanium pair */
        } else if (m == M_MT2) {
            v = ab_bq(x, &s->qa1, &s->qa2, MT_A_B0, MT_A_B1, MT_A_B2, MT_A_A1, 0.0f);
            v = ab_bq(v, &s->qb1, &s->qb2, 1.0f, MT_B_B1, 0.0f, MT_B_A1, MT_B_A2);
            v = ab_bq(v, &s->qc1, &s->qc2, 1.0f, MT_C_B1, MT_C_B2, MT_C_A1, MT_C_A2);
            v *= P->pre;                                 /* Dist stage gain */
            h1 += P->a1 * (v - h1);
            v += (MT_SHELF - 1.0f) * (v - h1);           /* .. its treble shelf */
            fb += P->aF * (v - fb);                      /* .. and low-pass */
            z = fb;
        } else {                                         /* DIST+, DS-1, RAT2: op-amp stage */
            if (m == M_DS1) {                            /* DS-1 booster */
                h1 += P->a1 * (x - h1);
                v = ab_clip(P->pre * (x - h1), 4.0f, 0.25f);
                h2 += P->a2 * (v - h2);                  /* op-amp leg */
                g = P->gA * (v - h2);
            } else {                                     /* DIST+: one leg; RAT2: two */
                v = x;
                h1 += P->a1 * (x - h1);
                h2 += P->a2 * (x - h2);
                g = P->gA * (x - h1) + P->gB * (x - h2);
            }
            fb += P->aF * (g - fb);                      /* gain rolls off in the treble */
            v = ab_rail(v + fb, P->rail);                /* op-amp rails */
            lc += P->aC * (v - lc);                      /* RC before the diodes (aC = 1: none) */
            z = lc;
        }
        y = add + ab_diode(z, P);                        /* the clipper, one copy for all models */
        d = y - dcx + AB_DC_R * dcy;                     /* DC blocker */
        dcx = y; dcy = d;
        /* the pedal's tone control */
        if (m == M_TS9) {                                /* 723 Hz LP + treble */
            t1 += P->aT * (d - t1);
            d = t1 + P->sk * (d - t1);
        } else if (m == M_DS1) {                         /* LP <-> HP blend */
            t1 += P->aT * (d - t1);
            t2 += P->aU * (d - t2);
            d = P->wl * t1 + P->wh * (d - t2);
        } else if (m == M_MUFF) {                        /* tone stack + output roll-off */
            d = ab_bq(d, &s->qc1, &s->qc2, P->v0, P->v1, P->v2, P->v3, P->v4);
            d = ab_bq(d, &s->qd1, &s->qd2, BM_O_B0, BM_O_B1, BM_O_B2, BM_O_A1, BM_O_A2);
        } else if (m == M_SFUZZ) {                       /* 1 kHz scoop */
            float hp;
            t1 += SF_F * t2;
            hp = d - t1 - SF_Q * t2;
            t2 += SF_F * hp;
            d += P->sk * t2;
        } else if (m == M_MT2) {                         /* post filter, High shelf */
            d = ab_bq(d, &s->qd1, &s->qd2, MT_D_B0, MT_D_B1, MT_D_B2, MT_D_A1, MT_D_A2);
            d = ab_bq(d, &s->qe1, &s->qe2, 1.0f, MT_E_B1, MT_E_B2, MT_E_A1, MT_E_A2);
            t2 += P->aT * (d - t2);
            d += P->sk * (d - t2);
        } else {                                         /* DIST+, RAT2: low-pass */
            t1 += P->aT * (d - t1);
            d = t1;
        }
        /* acid EQ */
        d = ab_bq(d, &s->el1, &s->el2, P->eq[0], P->eq[1], P->eq[2], P->eq[3], P->eq[4]);
        d = ab_bq(d, &s->em1, &s->em2, P->eq[5], P->eq[6], P->eq[7], P->eq[8], P->eq[9]);
        d = ab_bq(d, &s->eh1, &s->eh2, P->eq[10], P->eq[11], P->eq[12], P->eq[13], P->eq[14]);
        if (P->znr) {
            float r = env * invThr, tgt;
            if (r >= 1.0f) { hold = AB_HOLD; tgt = 1.0f; }
            else if (hold > 0) { hold--; tgt = 1.0f; }
            else tgt = r * r;                            /* 1:3 downward expander */
            gz += ((tgt > gz) ? AB_G_ATT : AB_G_REL) * (tgt - gz);
        } else {
            gz = 1.0f;
        }
        fade += AB_FADE * (1.0f - fade);
        buf[i] = P->dryG * x + P->wetG * (d * P->wetScale * gz * fade);
    }

    /* noise floor: follow the envelope down quickly, creep up slowly */
    if (env < nf) nf += AB_NF_DROP * (env - nf);
    else nf *= AB_NF_RISE;
    if (nf < AB_NF_MIN) nf = AB_NF_MIN;
    if (nf > AB_NF_MAX) nf = AB_NF_MAX;

    AB_FLUSH(h1); AB_FLUSH(h2); AB_FLUSH(fb); AB_FLUSH(lc);    /* denormals */
    AB_FLUSH(dcy); AB_FLUSH(t1); AB_FLUSH(t2);
    AB_FLUSH(s->qa1); AB_FLUSH(s->qa2); AB_FLUSH(s->qb1); AB_FLUSH(s->qb2);
    AB_FLUSH(s->qc1); AB_FLUSH(s->qc2); AB_FLUSH(s->qd1); AB_FLUSH(s->qd2);
    AB_FLUSH(s->qe1); AB_FLUSH(s->qe2);
    AB_FLUSH(s->el1); AB_FLUSH(s->el2); AB_FLUSH(s->em1); AB_FLUSH(s->em2);
    AB_FLUSH(s->eh1); AB_FLUSH(s->eh2);
    if (env < 1e-15f) env = 0.0f;
    if (gz < 1e-15f) gz = 0.0f;
    s->h1 = h1; s->h2 = h2; s->fb = fb; s->lc = lc; s->dcx = dcx; s->dcy = dcy;
    s->t1 = t1; s->t2 = t2; s->fade = fade;
    s->env = env; s->nf = nf; s->gz = gz; s->hold = hold;
}

/* ---- on-screen text ------------------------------------------------------ */
/* knob 0 = Model: TS9, DIST+, DS-1, RAT2, MUFF, SFUZZ, MT-2 */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'T'; out[1] = 'S'; out[2] = '9'; out[3] = 0; return 3; }
    if (value == 1u) { out[0] = 'D'; out[1] = 'I'; out[2] = 'S'; out[3] = 'T'; out[4] = '+'; out[5] = 0; return 5; }
    if (value == 2u) { out[0] = 'D'; out[1] = 'S'; out[2] = '-'; out[3] = '1'; out[4] = 0; return 4; }
    if (value == 3u) { out[0] = 'R'; out[1] = 'A'; out[2] = 'T'; out[3] = '2'; out[4] = 0; return 4; }
    if (value == 4u) { out[0] = 'M'; out[1] = 'U'; out[2] = 'F'; out[3] = 'F'; out[4] = 0; return 4; }
    if (value == 5u) { out[0] = 'S'; out[1] = 'F'; out[2] = 'U'; out[3] = 'Z'; out[4] = 'Z'; out[5] = 0; return 5; }
    out[0] = 'M'; out[1] = 'T'; out[2] = '-'; out[3] = '2'; out[4] = 0;
    return 4;
}

/* EQ knobs: screen 0..24 -> "-12" .. "0" .. "+12" (dB) */
#define AB_DB_LABEL(fn)                                                    \
int fn(unsigned int value, char *out)                                      \
{                                                                          \
    int v, len = 0;                                                        \
    if (value > 24u) value = 24u;                                          \
    v = (int)value - 12;                                                   \
    if (v < 0) { out[len] = '-'; len++; v = -v; }                          \
    else if (v > 0) { out[len] = '+'; len++; }                             \
    if (v >= 10) { out[len] = '1'; len++; v -= 10; }                       \
    out[len] = (char)('0' + v); len++;                                     \
    out[len] = 0;                                                          \
    return len;                                                            \
}
AB_DB_LABEL(ZDL_GetLabel_3)   /* Low  */
AB_DB_LABEL(ZDL_GetLabel_4)   /* Mid  */
AB_DB_LABEL(ZDL_GetLabel_5)   /* High */

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef DIRTBOX_HOST_TEST

#include "dirtbox_params.h"

#ifndef DIRTBOX_AUDIO_FUNC
#define DIRTBOX_AUDIO_FUNC Fx_DLY_DirtBox
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

/* the knob loop in the entry point needs Model..Mix in consecutive slots */
typedef char ab_slots_consecutive[
    (DIRTBOX_DRIVE_SLOT == DIRTBOX_MODEL_SLOT + 1 && DIRTBOX_TONE_SLOT == DIRTBOX_MODEL_SLOT + 2 &&
     DIRTBOX_LOW_SLOT == DIRTBOX_MODEL_SLOT + 3 && DIRTBOX_MID_SLOT == DIRTBOX_MODEL_SLOT + 4 &&
     DIRTBOX_HIGH_SLOT == DIRTBOX_MODEL_SLOT + 5 && DIRTBOX_ZNR_SLOT == DIRTBOX_MODEL_SLOT + 6 &&
     DIRTBOX_LEVEL_SLOT == DIRTBOX_MODEL_SLOT + 7 && DIRTBOX_MIX_SLOT == DIRTBOX_MODEL_SLOT + 8) ? 1 : -1];

AB_CODE_SECTION(DIRTBOX_AUDIO_FUNC)
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
    AbState *s;
    AbParams P;
    float k[9];
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
    if ((end - base) < sizeof(AbState) || span < (end - base)) return;
    if (stateBase + sizeof(AbState) > end) return;

    s = (AbState *)stateBase;

    /* the nine knobs sit in consecutive slots (checked below at compile time);
     * one loop keeps one copy of ab_knob. k[] starts at each knob's default. */
    k[0] = (float)DIRTBOX_MODEL_UI_DEFAULT; k[1] = (float)DIRTBOX_DRIVE_UI_DEFAULT;
    k[2] = (float)DIRTBOX_TONE_UI_DEFAULT;  k[3] = (float)DIRTBOX_LOW_UI_DEFAULT;
    k[4] = (float)DIRTBOX_MID_UI_DEFAULT;   k[5] = (float)DIRTBOX_HIGH_UI_DEFAULT;
    k[6] = (float)DIRTBOX_ZNR_UI_DEFAULT;   k[7] = (float)DIRTBOX_LEVEL_UI_DEFAULT;
    k[8] = (float)DIRTBOX_MIX_UI_DEFAULT;
    AB_NO_UNROLL
    for (i = 0; i < 9; i++) {
        float im = 0.01f;                                    /* 0..100 knobs   */
        if (i == 0) im = 0.16666667f;                        /* Model 0..6     */
        if (i >= 3 && i <= 5) im = 0.041666668f;             /* EQ 0..24       */
        k[i] = ab_knob(params[DIRTBOX_MODEL_SLOT + i], k[i], im);
    }

    ab_prepare(&P, k);
    if (s->magic != AB_MAGIC) ab_init(s, &P);
    ab_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* DIRTBOX_HOST_TEST */
