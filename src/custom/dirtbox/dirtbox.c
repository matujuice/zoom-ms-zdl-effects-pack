/*
 * dirtbox.c - "DirtBox": three distortion models in one effect, plus an automatic
 * ZNR-style noise reducer that leaves kick tails alone. Mono.
 *
 * MODELS (one Model knob). Each follows a published circuit analysis or digital
 * recreation of the pedal, stage by stage; component values are in the comments
 * next to the numbers. 1.0 in the pedal's audio = 1 V into the circuit.
 *   DS-1  the built-in distortion of the Behringer TD-3 (the TB-303 clone; the
 *         original 303 has none), which is a copy of the Boss DS-1. Stages from the
 *         ElectroSmash DS-1 analysis and the DS1.lv2 nodal model (LiamLombard):
 *         transistor booster, 35 dB (56x) above a 33 Hz high-pass, running out of
 *         swing around 4 V; op-amp stage, gain 1 + Dist / 4.7k (Dist = 100k pot)
 *         above 72 Hz (4.7k / 0.47u), treble trimmed by the 100p feedback cap,
 *         output stuck inside the 9 V rails; 2.2k / 0.01u low-pass (7.2 kHz) into
 *         two 1N4148s to ground; the Big Muff style tone: a pot blending a 234 Hz
 *         low-pass (6.8k / 0.1u) with a 1.06 kHz high-pass, so noon scoops ~500 Hz.
 *   RAT   the ProCo RAT, after the nodal model by Rudro085 (Proco-Rat, values as
 *         in the ElectroSmash RAT analysis): op-amp gain 1 + Dist / 560R (4.7u,
 *         from 60 Hz) + Dist / 47R (2.2u, from 1.54 kHz), Dist = 100k audio-taper
 *         pot, 100p across it; the LM308's ~1 MHz gain-bandwidth cuts the treble as
 *         the gain rises (corner = 1 MHz / gain, ~450 Hz at full); 9 V rails;
 *         1k into two 1N914s (Is 2.52 nA, Rudro's sinh law); the Filter is
 *         1.5k + 100k audio pot into 3.3n (475 Hz .. 32 kHz). Tone up = brighter
 *         (the real Filter knob turns the other way).
 *   METAL the Boss MT-2 Metal Zone, from guitarix's MetalTone (DK-method model of
 *         the MT-2 schematic), its filters evaluated at 44.1 kHz: a fixed
 *         pre-filter peaking +25 dB around 900 Hz (stages p0/p1), the Dist stage
 *         (1.2x .. 55x on the pot's log taper, treble shelved -11 dB between 629 Hz
 *         and 2.28 kHz, plus a low-pass that closes to ~4.8 kHz at full Dist), the
 *         clipper's transfer curve (guitarix's table, fitted, 0.575 ceiling), then
 *         the fixed post filter (p3: +5 dB at 100 Hz, +11 dB at 4.6 kHz). Low,
 *         Middle and Freq sit at noon (flat in the model); Tone = the High knob,
 *         a shelf from -20 to +20 dB above ~2 kHz, flat at 50.
 *   Not oversampled (too heavy for the pedal): the models' own low-passes keep the
 *   aliasing down, but high notes at full Drive alias a little. The diode curves
 *   are fitted to the circuit equations (within 2 % of full scale; MT-2 within 3 %).
 *
 * DIODE CURVES: y = c S(g v / c) + m v / (1 + h |v|), S(u) = u / (1 + u^4)^(1/4):
 *   a knee into a slow log-like rise, as a diode pair does. Fitted per model.
 *
 * LEVEL: Makeup = REF / min(gain x REF, ceiling) x trim: with an input peaking at
 *   REF (0.2, about -14 dBFS) the wet level stays near the input level whatever the
 *   Drive, so turning Drive up adds dirt, not mostly volume (trim per model, set
 *   in the host test). Level 50 = that level.
 *   Turning Model clears the filters, mutes the wet and fades it in over ~6 ms.
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
 *   0 Model 0..2    DS-1 / RAT / METAL
 *   1 Drive 0..100  the pedal's own Dist / Distortion pot, 0 = fully left. RAT at 0
 *                   is nearly clean; DS-1 and METAL still distort at 0, as the real
 *                   pedals do (DS-1's booster and MT-2's fixed gain come first)
 *   2 Tone  0..100  DS-1 Tone, RAT Filter, METAL High; 100 = brightest on all three
 *   3 ZNR   0..100  noise reducer margin above the measured noise floor, 0 = off
 *   4 Level 0..100  wet level 0 .. 2x, 50 = about the input level (see LEVEL)
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

#define DB_MAGIC        0x44423033u          /* "DB03" */
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
#define DB_RAIL         4.5f                 /* op-amp swing on 9 V (DS-1, RAT)  */
#define DB_FMAX         16000.0f             /* highest corner db_pole handles   */

/* MT-2 filters from guitarix MetalTone, evaluated at 44.1 kHz and split into
 * biquads (b0 b1 b2 / a1 a2). Pre-filter p1 (DC block p0 is the 33 Hz stage's job): */
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

typedef struct {
    unsigned int magic;
    int model;             /* last model, to clear the filters after a change */
    float h1, h2, fb, lc;  /* one-pole states: high-pass legs, gain roll-off, pre-clip */
    float dcx, dcy;        /* DC blocker                              */
    float t1, t2;          /* tone                                    */
    float qa1, qa2, qb1, qb2, qc1, qc2, qd1, qd2, qe1, qe2;   /* MT-2 biquads */
    float fade;            /* wet fade-in after a Model change        */
    float env, nf, gz;     /* ZNR: input envelope, noise floor, gain  */
    int hold;              /* ZNR hold counter, samples               */
} DbState;

typedef struct {
    int model, znr;
    float pre, a1, a2;         /* DS-1 booster gain; high-pass legs' poles      */
    float gA, gB, aF, aC;      /* leg gains, gain roll-off pole, pre-clip pole  */
    float c, ic, cg, cm, ch, cap;   /* diode curve                              */
    float aT, aU, wl, wh;      /* tone: wl x LP(aT) + wh x HP(aU)               */
    float sk;                  /* METAL High shelf: x + sk x HP(aT)             */
    float wetScale;            /* makeup x Level                                */
    float margin;              /* ZNR threshold / noise floor                   */
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

/* One-pole coefficient 1 - e^(-2 pi fc / fs), fc up to 16 kHz (clamped): e^(-w/8)
 * by a Taylor series, then squared three times. */
DB_ALWAYS_INLINE(db_pole)
static inline float db_pole(float hz)
{
    float w, e;
    if (hz > DB_FMAX) hz = DB_FMAX;
    w = hz * DB_W * 0.125f;
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

/* Knee: c * S(v / c), S(u) = u (1 + u^4)^(-1/4). Slope 1 at zero, flat at c. */
DB_ALWAYS_INLINE(db_clip)
static inline float db_clip(float v, float c, float ic)
{
    float u = v * ic, u2 = u * u, r;
    r = db_rsqrt(1.0f + u2 * u2);                /* (1 + u^4)^(-1/2) */
    return c * u * (r * db_rsqrt(r));            /* x sqrt(r)        */
}

/* Diode pair: knee plus a slow rise, y = c S(g v / c) + m v / (1 + h |v|), held
 * inside +-cap. */
DB_ALWAYS_INLINE(db_diode)
static inline float db_diode(float v, const DbParams *P)
{
    float av = (v < 0.0f) ? -v : v, y;
    y = db_clip(P->cg * v, P->c, P->ic) + P->cm * v * db_inv(1.0f + P->ch * av);
    if (y > P->cap) y = P->cap;
    if (y < -P->cap) y = -P->cap;
    return y;
}

/* Biquad, transposed direct form II. */
DB_ALWAYS_INLINE(db_bq)
static inline float db_bq(float x, float *z1, float *z2,
                          float b0, float b1, float b2, float a1, float a2)
{
    float y = b0 * x + *z1;
    *z1 = b1 * x - a1 * y + *z2;
    *z2 = b2 * x - a2 * y;
    return y;
}

DB_ALWAYS_INLINE(db_clear)
static inline void db_clear(DbState *s)
{
    s->h1 = 0.0f; s->h2 = 0.0f; s->fb = 0.0f; s->lc = 0.0f;
    s->dcx = 0.0f; s->dcy = 0.0f; s->t1 = 0.0f; s->t2 = 0.0f;
    s->qa1 = 0.0f; s->qa2 = 0.0f; s->qb1 = 0.0f; s->qb2 = 0.0f; s->qc1 = 0.0f;
    s->qc2 = 0.0f; s->qd1 = 0.0f; s->qd2 = 0.0f; s->qe1 = 0.0f; s->qe2 = 0.0f;
    s->fade = 0.0f;
}

DB_ALWAYS_INLINE(db_init)
static inline void db_init(DbState *s, const DbParams *P)
{
    s->model = P->model;
    db_clear(s);
    s->env = 0.0f; s->nf = 0.0001f; s->gz = 1.0f; s->hold = 0;
    s->magic = DB_MAGIC;
}

/* k[] = 0..1 by each knob's own maximum, in manifest order */
DB_ALWAYS_INLINE(db_prepare)
static inline void db_prepare(DbParams *P, const float *k)
{
    float d = k[1], t = k[2], gpk, ceil, trim, fl, rd;
    int m = (int)(k[0] * 2.0f + 0.5f);
    P->model = m;
    P->pre = 1.0f; P->gA = 0.0f; P->gB = 0.0f;
    P->a1 = 0.1f; P->a2 = 0.1f; P->aC = 1.0f; P->aU = 0.1f;
    P->wl = 1.0f; P->wh = 0.0f; P->sk = 0.0f;
    if (m == 0) {                                /* DS-1: TD-3 = DS-1 */
        rd = 100000.0f * d;                      /* Dist, 100k linear */
        P->pre = 56.0f;                          /* booster, 35 dB    */
        P->a1 = db_pole(33.0f);                  /* booster's input high-pass */
        P->a2 = db_pole(72.0f);                  /* 4.7k / 0.47u leg  */
        P->gA = rd * 0.00021276596f;             /* Dist / 4.7k       */
        fl = DB_FMAX;
        if (rd > 1000.0f) fl = 1591549431.0f * db_inv(rd);   /* 1 / (2 pi Dist 100p) */
        P->aF = db_pole(fl);
        P->aC = db_pole(7234.0f);                /* 2.2k / 0.01u      */
        P->c = 0.428f; P->cg = 0.8931f; P->cm = 0.1514f; P->ch = 0.6116f; P->cap = 1.0f;
        P->aT = db_pole(234.0f);                 /* tone: LP 6.8k / 0.1u */
        P->aU = db_pole(1063.0f);                /*       HP 1.06 kHz    */
        P->wl = 1.0f - t; P->wh = t;
        gpk = 56.0f * (1.0f + P->gA); ceil = 0.6f; trim = 2.0f;
    } else if (m == 1) {                         /* RAT */
        rd = 1010.101f * (db_exp2(6.643856f * d) - 1.0f);    /* 100k audio taper: 100k (10^(2d) - 1) / 99 */
        P->a1 = db_pole(60.5f);                  /* 560R / 4.7u  */
        P->a2 = db_pole(1539.0f);                /* 47R / 2.2u   */
        P->gA = rd * 0.0017857143f;              /* Dist / 560   */
        P->gB = rd * 0.021276596f;               /* Dist / 47    */
        fl = 1000000.0f * db_inv(1.0f + rd * 0.023049645f);  /* LM308: 1 MHz / (1 + Dist / (47 || 560)) */
        if (rd > 1000.0f) {
            float fc = 1591549431.0f * db_inv(rd);           /* 100p across Dist */
            if (fc < fl) fl = fc;
        }
        P->aF = db_pole(fl);
        P->c = 0.6076f; P->cg = 0.9516f; P->cm = 0.0935f; P->ch = 0.3322f; P->cap = 1.0f;
        {   /* Filter: 1.5k + 100k audio pot into 3.3n, Tone 100 = pot at 0 */
            float rt = 11111.11f * (db_exp2(3.321928f * (1.0f - t)) - 1.0f);
            P->aT = db_pole(48228770.0f * db_inv(1500.0f + rt));
        }
        gpk = 1.0f + P->gA + 0.5f * P->gB; ceil = 0.75f; trim = 1.0f;
    } else {                                     /* METAL */
        float D = (db_exp2(4.328085f * d) - 1.0f) * 0.052396f;   /* guitarix LogPot(3, d) */
        float G = 1.21f + 53.8f * D, gdb, g;
        P->pre = G;
        P->a1 = db_pole(629.2f);                 /* Dist stage treble shelf */
        fl = DB_FMAX;
        if (D > 0.3f) fl = 4800.0f * db_inv(D);  /* .. and its closing low-pass */
        P->aF = db_pole(fl);
        P->c = 0.0621f; P->cg = 100.0f; P->cm = 7.8705f; P->ch = 14.2186f; P->cap = 0.5745f;
        gdb = 2.0f * t - 1.0f; gdb = gdb * gdb * gdb;            /* High: +-20 dB, cubic */
        if (gdb >= 0.0f) {
            g = db_exp2(3.321928f * gdb);                        /* 10^(gdb) */
            P->sk = g - 1.0f; P->aT = db_pole(7200.0f);
        } else {
            g = db_exp2(-3.321928f * gdb);
            P->sk = db_inv(g) - 1.0f; P->aT = db_pole(7200.0f * db_inv(g));
        }
        gpk = 18.0f * G; ceil = 0.5745f; trim = 0.42f;
    }
    P->ic = db_inv(P->c);
    gpk *= DB_REF;
    if (gpk > ceil) gpk = ceil;                  /* expected clip peak */
    P->wetScale = DB_REF * db_inv(gpk) * (k[4] + k[4]) * trim;
    P->znr = (k[3] > 0.0f);
    P->margin = db_exp2(1.0f + 3.0f * k[3]);
    P->dryG = 2.0f - 2.0f * k[5]; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * k[5];        if (P->wetG > 1.0f) P->wetG = 1.0f;
}

#define DB_FLUSH(v) if ((v) < 1e-15f && (v) > -1e-15f) (v) = 0.0f

DB_ALWAYS_INLINE(db_process)
static inline void db_process(DbState *s, const DbParams *P, float *buf, int n)
{
    int i, hold, m = P->model;
    float h1, h2, fb, lc, dcx, dcy, t1, t2, fade, env, nf, gz, invThr;

    if (m != s->model) { s->model = m; db_clear(s); }
    h1 = s->h1; h2 = s->h2; fb = s->fb; lc = s->lc; dcx = s->dcx; dcy = s->dcy;
    t1 = s->t1; t2 = s->t2; fade = s->fade;
    env = s->env; nf = s->nf; gz = s->gz; hold = s->hold;
    invThr = db_inv(nf * P->margin);

    for (i = 0; i < n; i++) {
        float x = buf[i], ax = (x < 0.0f) ? -x : x, v, g, y, d;
        /* ZNR detector on the clean input */
        env *= DB_ENV_REL;
        if (ax > env) env = ax;
        if (m == 2) {                                    /* METAL */
            v = db_bq(x, &s->qa1, &s->qa2, MT_A_B0, MT_A_B1, MT_A_B2, MT_A_A1, 0.0f);
            v = db_bq(v, &s->qb1, &s->qb2, 1.0f, MT_B_B1, 0.0f, MT_B_A1, MT_B_A2);
            v = db_bq(v, &s->qc1, &s->qc2, 1.0f, MT_C_B1, MT_C_B2, MT_C_A1, MT_C_A2);
            v *= P->pre;                                 /* Dist stage gain */
            h1 += P->a1 * (v - h1);
            v += (MT_SHELF - 1.0f) * (v - h1);           /* .. its treble shelf */
            fb += P->aF * (v - fb);                      /* .. and low-pass */
            y = db_diode(fb, P);
        } else {
            if (m == 0) {                                /* DS-1 booster */
                h1 += P->a1 * (x - h1);
                v = db_clip(P->pre * (x - h1), 4.0f, 0.25f);
                h2 += P->a2 * (v - h2);                  /* op-amp leg */
                g = P->gA * (v - h2);
            } else {                                     /* RAT: two legs */
                v = x;
                h1 += P->a1 * (x - h1);
                h2 += P->a2 * (x - h2);
                g = P->gA * (x - h1) + P->gB * (x - h2);
            }
            fb += P->aF * (g - fb);                      /* gain rolls off in the treble */
            v += fb;
            if (v > DB_RAIL) v = DB_RAIL;                /* op-amp rails */
            if (v < -DB_RAIL) v = -DB_RAIL;
            lc += P->aC * (v - lc);                      /* DS-1: 2.2k / 0.01u (aC = 1 on RAT) */
            y = db_diode(lc, P);
        }
        d = y - dcx + DB_DC_R * dcy;                     /* DC blocker */
        dcx = y; dcy = d;
        t1 += P->aT * (d - t1);                          /* Tone */
        if (m == 0) {                                    /* DS-1: LP <-> HP blend */
            t2 += P->aU * (d - t2);
            d = P->wl * t1 + P->wh * (d - t2);
        } else if (m == 1) {                             /* RAT: Filter low-pass */
            d = t1;
        } else {                                         /* METAL: post filter, High shelf */
            d = db_bq(d, &s->qd1, &s->qd2, MT_D_B0, MT_D_B1, MT_D_B2, MT_D_A1, MT_D_A2);
            d = db_bq(d, &s->qe1, &s->qe2, 1.0f, MT_E_B1, MT_E_B2, MT_E_A1, MT_E_A2);
            t2 += P->aT * (d - t2);
            d += P->sk * (d - t2);
        }
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

    DB_FLUSH(h1); DB_FLUSH(h2); DB_FLUSH(fb); DB_FLUSH(lc);    /* denormals */
    DB_FLUSH(dcy); DB_FLUSH(t1); DB_FLUSH(t2);
    DB_FLUSH(s->qa1); DB_FLUSH(s->qa2); DB_FLUSH(s->qb1); DB_FLUSH(s->qb2);
    DB_FLUSH(s->qc1); DB_FLUSH(s->qc2); DB_FLUSH(s->qd1); DB_FLUSH(s->qd2);
    DB_FLUSH(s->qe1); DB_FLUSH(s->qe2);
    if (env < 1e-15f) env = 0.0f;
    if (gz < 1e-15f) gz = 0.0f;
    s->h1 = h1; s->h2 = h2; s->fb = fb; s->lc = lc; s->dcx = dcx; s->dcy = dcy;
    s->t1 = t1; s->t2 = t2; s->fade = fade;
    s->env = env; s->nf = nf; s->gz = gz; s->hold = hold;
}

/* ---- on-screen text ------------------------------------------------------ */
/* knob index 0 = Model: 0 DS-1, 1 RAT, 2 METAL */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 2u) { out[0] = 'M'; out[1] = 'E'; out[2] = 'T'; out[3] = 'A'; out[4] = 'L'; out[5] = 0; return 5; }
    if (value == 1u) { out[0] = 'R'; out[1] = 'A'; out[2] = 'T'; out[3] = 0; return 3; }
    out[0] = 'D'; out[1] = 'S'; out[2] = '-'; out[3] = '1'; out[4] = 0;
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
