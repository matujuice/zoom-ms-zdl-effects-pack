/*
 * Takeoff - risers and downlifters (Zoom MS, custom ZDL). It only ADDS sound on top, like DubSiren:
 * the input passes through in stereo, never touched (no Mix knob, Vol instead).
 *
 *   input ------------------------------------------------------------+
 *   noise --> resonant filter (sweep) --> x swell x Vol --> (+) --> out (mono, same on L and R)
 *                                              |                ^
 *                                              +--> Space: 4 all-pass diffusers
 *                                                   in a feedback ring (mono reverb)
 *   One position x runs 0..1 (UP) or 1..0 (DOWN). The frequency is f0 * 2^(range * curve(x)) (exponential),
 *   the volume swells as x^2. f0 = 50 Hz (moved by Var's pitch draw); the noise filter sits at 8 x f0.
 *   UP cuts at the top (a 1.5 ms fade, no click); DOWN falls to silence.
 *
 * KNOBS (screen values, param order)
 *   0 Dir    UP (default) / DOWN
 *   1 Len    0..4 = 1/2/4/8/16 bars at the pedal's tempo (zmt.h), 5..35 = FREE, 1..31 seconds. Default 2 = 4 bars
 *   2 Color  noise band width: 0 = a narrow whistle, 100 = wide air (level compensated). Default 25
 *   3 Wind   slow random gusts on the noise's level and filter (organic). Default 40
 *   4 Grain  noise grit: random sample-and-hold, smooth (0) to crackle (100). Default 0
 *   5 Range  how far the filter travels, 0..100 = 0..5 octaves: small = a subtle lift, 100 = rumble to air.
 *            Default 60 = 3 octaves
 *   6 Var    how much each takeoff differs from the last (default 35). Drawn once when a sweep starts, scaled
 *            by Var: Range x (1 +-0.35), start pitch +-5 semitones, curve bend +-0.6, Color / Wind / Grain +-30.
 *            Never Len, Dir or Vol. Var 0 = the same takeoff every time
 *            (noise, gusts and filters restart from the same seeds).
 *   7 Space  reverb tail on the riser's own sound, 0 = off (default)
 *   8 Vol    level of the added sound (default 50)
 *
 * TRIGGER. The effect sees the on/off flag (passed while OFF, as DubSiren) and the firmware's "footswitch
 *   down" word 6 of the transport block (zmt.h). Any on/off flip arms a sweep (QUANT): it starts on the next
 *   line of the Len cycle, so it ends on the following one. Only while the MIDI clock runs (bars Len): the
 *   lines are Len-bar lines from bar 1; without a clock every sweep starts at once. A flip up to 150 ms after a
 *   line counts as on it (the sweep joins in sync). A flip while armed
 *   cancels; a flip during a sweep drops it (a fast fade). UP cuts at the end. If word 6 is still 1 0.5 s
 *   after the flip it is a hold: the sweep goes on at Len speed, sits at its end if it gets there and drops
 *   when word 6 goes to 0 (released before the line: cancelled). Word 6 rises up to 16 ms before the press flip;
 *   the first flip of a press always counts. Its second flip, the one a momentary HOLD FOR makes on
 *   release (with or up to 40 ms after word 6 falls, or while word 6 is still 1), is ignored: a release
 *   never starts a new sweep. Every block the effect writes its params pointer (ctx[1]) into word 7 (the hold
 *   claim; only while the transport block's magic is there, so stock firmware is never touched). Stock
 *   firmware: holding opens the tuner, so press only. MIDI: an OFF during a sweep drops it.
 *
 * LED (MOD firmware services block 0xC00F3000, magic 0x5A4C5301): while word 22 = our params pointer (we
 *   are the effect under the cursor in play mode) the effect drives the footswitch LED by PWM, one step a
 *   block, 32 steps (172 Hz), through the firmware's GPIO words 13 (on) / 14 (off) / 15 (mask): armed = a
 *   pulse on every beat (FREE: every second), sweeping = brightness x^2 (fades up on UP, down on DOWN).
 *   While armed or sweeping it writes the LED every block and tells the stock LED task its state is already
 *   written (0xC00EEB10 bit 0 = 0xC00EEB08 bit 0), so a footswitch flip cannot overwrite it; when it goes idle
 *   it flips 0xC00EEB10 bit 0 once and the stock LED (on/off) comes back. Not ours: nothing is written (the
 *   firmware hands the LED back when the cursor owner changes).
 *
 *
 * IDLE: nothing sounding and the Space tail died (32 blocks under -120 dB) = no synthesis, no reverb,
 *   nothing but the LED while ours.
 * SPACE: reverb state is zeroed (1024 floats a block, riser still dry) when Space comes up from 0; 0 = off
 *   (turning it to 0 cuts a ringing tail).
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static arrays, no libm, no float or integer division, no
 * switch, no double. CPU: the knob maths run only when a knob, the BPM or a sweep start changes them.
 */

#include <stdint.h>
#include "../common/zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define TK_DO_PRAGMA(x) _Pragma(#x)
#define TK_EXPAND_PRAGMA(x) TK_DO_PRAGMA(x)
#define TK_ALWAYS_INLINE(fn) TK_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define TK_CODE_SECTION(fn) TK_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define TK_ITER8 _Pragma("MUST_ITERATE(8,8,8)") _Pragma("UNROLL(1)")
#define TK_ONE_COPY(fn) TK_EXPAND_PRAGMA(FUNC_CANNOT_INLINE(fn)) TK_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define TK_ONE_COPY(fn)
#define TK_ITER8
#define TK_ALWAYS_INLINE(fn)
#define TK_CODE_SECTION(fn)
#endif

#define TK_MAGIC     0x544B4F38u         /* "TKO8": change whenever TkState changes */
#define HZ_TO_INC    2.2675737e-5f       /* 1 / 44100 */
#define RV_N         2048                /* reverb ring */
#define RV_M         2047
#define AP_N         512
#define AP_M         511
#define RV_TOTAL     (RV_N + 4 * AP_N)
#define CLR_CHUNK    1024
#define HOLD_SAMPLES 22050               /* 0.5 s */
#define QUIET_BLOCKS 32
#define HOLD_GUARD   220                 /* blocks (40 ms): a release flip after word 6 falls */
#define LATE_BLOCKS  827                 /* 150 ms: a flip this late after a cycle line joins it */
#define TK_PWM       32                  /* LED PWM steps, one per block */
#define NOISE_GAIN   0.25f               /* bandpass output -> about +-0.5 at the top (tuned in tests/takeoff_sounds.c) */

enum { ST_IDLE = 0, ST_SWEEP, ST_HOLD, ST_DROP, ST_WAIT };

/* The MOD firmware's services block (zoom-ms-midistomp docs/looper-services.md), word indexes */
#ifndef TK_SVC
#ifdef __TI_COMPILER_VERSION__
#define TK_SVC       0xC00F3000u
#else
static unsigned int tk_svc_host[32];     /* host tests: no firmware; tests fill it in */
#define TK_SVC       ((uintptr_t)tk_svc_host)
#endif
#endif
#define SV_MAGIC     0x5A4C5301u
#define SV_LEDON     13
#define SV_LEDOFF    14
#define SV_LEDMASK   15
#define SV_CURSOR    22                  /* params pointer of the effect under the cursor (play mode), or 0 */
/* The stock LED task (every 16 ms) rewrites the LED when its wanted bit 0 differs from the bit it wrote last,
 * which a footswitch flip causes. While we drive the LED we set "written" = "wanted" so it stays quiet. */
#ifdef __TI_COMPILER_VERSION__
#define TK_LEDWANT   0xC00EEB08u
#define TK_LEDDONE   0xC00EEB10u
#else
static unsigned int tk_led_host[2];      /* host tests: [0] wanted, [1] written */
#define TK_LEDWANT   ((uintptr_t)&tk_led_host[0])
#define TK_LEDDONE   ((uintptr_t)&tk_led_host[1])
#endif

typedef struct {
    int   act, rv;
    unsigned int gth;              /* Grain: refresh threshold of 65536 */
    float lvl, gT, ng, f, a, dmp;  /* f, a: filter coefficient and level with Wind */
    float fb, wetG;
} TkParams;

/* On the pedal every byte of code a block runs costs about a cycle (it comes from slow memory each block):
 * the knob maths run only when a knob or the BPM changes, or a sweep starts (kd). */
typedef struct {
    unsigned int magic;
    int   rvClr, inited, prev_foot, state, holdT, chk, quiet, apw, rw;
    int   prev_hold, rel, hl, ref, late, pwm, led, wt, pf, ff;   /* pf: this press already flipped, ff: blocks since a flip */
    unsigned int rng, rngW, rngV;
    float x, g, cph;               /* position, gain, Len cycle phase 0..1 */
    float nh, nq, gw[2], gt;       /* held noise (Grain), noise low-pass (darker hiss), gust smoothers, gust target */
    float vR, vP, vC, vCo, vWi, vGr;   /* Var draws: Range factor, pitch st, curve, Color/Wind/Grain */
    float sv[2], lp;               /* filter state, reverb damping */
    float rk[10];                  /* raw knobs and BPM that u[] and the knob maths come from */
    int   u[9], kd;                /* screen values; kd: knob maths to redo */
    float dx, fz, vol, wd;         /* knob maths: step a block, root Hz, level, Wind amount */
    TkParams P;
    ZtSync zt;
    float rv[RV_TOTAL];            /* the ring, then 4 all-pass lines */
} TkState;

TK_ONE_COPY(tk_ui)
static int tk_ui(float raw, int def)
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

/* all-pass lengths 113 163 241 335 (Washout's left side) */
TK_ALWAYS_INLINE(ap_len)
static inline int ap_len(int k)
{
    return (k == 0) ? 113 : (k == 1) ? 163 : (k == 2) ? 241 : 335;
}

TK_ALWAYS_INLINE(tk_init)
static inline void tk_init(TkState *s)
{
    int k;
    s->rvClr = 0; s->inited = 0; s->prev_foot = 0; s->state = ST_IDLE; s->holdT = 0; s->chk = 0;
    s->quiet = QUIET_BLOCKS; s->apw = 0; s->rw = 0;
    s->prev_hold = 0; s->rel = 0; s->hl = 0; s->ref = 0; s->late = 0; s->pwm = 0; s->led = -1; s->wt = 0; s->pf = 0; s->ff = HOLD_GUARD;
    s->rng = 12345u; s->rngW = 4242u; s->rngV = 2718281u;
    s->vR = 1.0f; s->vP = 0.0f; s->vC = 0.0f; s->vCo = 0.0f; s->vWi = 0.0f; s->vGr = 0.0f;
    s->x = 0.0f; s->g = 0.0f; s->cph = 0.0f;
    s->nh = 0.0f; s->nq = 0.0f; s->gt = 0.0f; s->gw[0] = 0.0f; s->gw[1] = 0.0f;
    s->sv[0] = 0.0f; s->sv[1] = 0.0f; s->lp = 0.0f;
    for (k = 0; k < 10; k++) s->rk[k] = -1.0f;  /* no knob is -1: the first block works everything out */
    s->kd = 1;
    zt_init(&s->zt);
    s->magic = TK_MAGIC;
}

TK_ALWAYS_INLINE(tk_rnd)
static inline float tk_rnd(unsigned int *r)      /* -1..1 */
{
    *r = *r * 1664525u + 1013904223u;
    return (float)(int)*r * 4.656613e-10f;
}

TK_ALWAYS_INLINE(tk_clampf)
static inline float tk_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* once per sweep start: this takeoff's offsets around the knobs, scaled by Var (0..100) */
TK_ALWAYS_INLINE(tk_vary)
static inline void tk_vary(TkState *s, int var)
{
    float v = 0.01f * (float)var;
    unsigned int seed;
    s->vR  = 1.0f + 0.35f * v * tk_rnd(&s->rngV);
    s->vP  = 5.0f * v * tk_rnd(&s->rngV);
    s->vC  = 0.6f * v * tk_rnd(&s->rngV);
    s->vCo = 30.0f * v * tk_rnd(&s->rngV);
    s->vWi = 30.0f * v * tk_rnd(&s->rngV);
    s->vGr = 30.0f * v * tk_rnd(&s->rngV);
    seed = var ? s->rngV : 0u;
    s->kd = 1;                                   /* new offsets: redo the knob maths */
    s->wt = 0;                                   /* and the filter maths on the first sounding block */
    if (s->g <= 0.0f) {                          /* a fresh start: Var 0 = the same noise every time */
        s->rng = 12345u ^ seed; s->rngW = 4242u ^ seed;
        s->nh = 0.0f; s->nq = 0.0f; s->gt = 0.0f; s->gw[0] = 0.0f; s->gw[1] = 0.0f;
        s->sv[0] = 0.0f; s->sv[1] = 0.0f;
    }
}

/* once per block: knobs u[] (screen ints), foot = on/off flag, hold = footswitch down (word 6),
 * run = the MIDI clock runs (zmt.h), bpm */
TK_ALWAYS_INLINE(tk_prepare)
static inline void tk_prepare(TkState *s, TkParams *P, const int *u, int foot, int hold, int run, float bpm)
{
    int   up = (u[0] == 0), n = u[1], flip, wrap, go = 0;
    float dx, f, fc, np, q, gr, col, grn, xc, svf;

    if (!s->inited) { s->prev_foot = foot; s->prev_hold = hold; s->inited = 1; }
    flip = (foot != s->prev_foot);
    s->prev_foot = foot;
    if (!s->prev_hold && hold) s->pf = (s->ff < HOLD_GUARD);        /* the switch went down (a flip just before = this press) */
    if (s->prev_hold && !hold) s->rel = HOLD_GUARD;                 /* the switch came up */
    if (flip && s->pf && (hold || s->rel > 0)) { flip = 0; s->rel = 0; s->pf = 0; }   /* this press's 2nd flip: the release */
    else if (flip && hold) s->pf = 1;                               /* word 6 rises before the press flip: that one counts */
    if (s->rel > 0) { s->rel--; if (s->rel == 0 && !hold) s->pf = 0; }
    s->ff = flip ? 0 : (s->ff < HOLD_GUARD ? s->ff + 1 : HOLD_GUARD);
    s->prev_hold = hold;

    if (s->kd) s->dx = (n < 5) ? bpm * 7.5586e-7f * tk_inv2n(n) : 1.8141e-4f * tk_recip((float)(n - 4));
    dx = s->dx;

    /* the Len cycle: Len-bar lines from bar 1 while the clock runs, else free at the Len rate */
    if (run && n < 5 && s->zt.clk > 0u) {
        unsigned int r = s->zt.clk - 1u, cyc = 96u << n, k, mk;
        for (k = 26u - (unsigned int)n; k-- > 0u; ) { mk = cyc << k; while (r >= mk) r -= mk; }
        np = ((float)(int)r + s->zt.sub) * 0.010416667f * tk_inv2n(n);
        s->ref = 1;
    } else {
        s->ref = 0;
        np = s->cph + dx;
        if (np >= 1.0f) np -= 1.0f;
    }
    wrap = (np < s->cph);
    s->cph = np;
    s->late = wrap ? 0 : (s->late < LATE_BLOCKS ? s->late + 1 : LATE_BLOCKS);

    if (flip) {
        if (s->state == ST_SWEEP || s->state == ST_HOLD) { s->state = ST_DROP; s->chk = 0; }
        else if (s->state == ST_WAIT) { s->state = ST_IDLE; s->chk = 0; }
        else {
            s->state = ST_WAIT; s->holdT = 0; s->chk = 1; s->hl = 0;
            if (!s->ref) { s->cph = 0.0f; s->late = 0; go = 1; }   /* no clock: start now */
            else if (s->late < LATE_BLOCKS) go = 1;                          /* just after a line: join it */
        }
    }
    if (s->chk && (s->state == ST_WAIT || s->state == ST_SWEEP)) {
        s->holdT += 8;
        if (s->holdT >= HOLD_SAMPLES) {
            s->chk = 0;
            if (hold) { s->hl = 1; if (s->state == ST_SWEEP) s->state = ST_HOLD; }
        }
    }
    if (s->state == ST_WAIT) {
        if (s->hl && !hold) { s->state = ST_IDLE; }                 /* hold let go before the line */
        else if (go || wrap) {
            s->state = s->hl ? ST_HOLD : ST_SWEEP;
            s->x = up ? s->cph - dx : 1.0f - s->cph + dx;           /* on the cycle: ends on the next line */
            tk_vary(s, u[6]);
        }
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

    if (s->kd) {                                 /* knobs, BPM or the Var draws changed */
        s->vol = 0.7f * (float)u[8] * 0.01f;
        col = tk_clampf((float)u[2] + s->vCo, 0.0f, 100.0f);
        grn = tk_clampf((float)u[4] + s->vGr, 0.0f, 100.0f);
        q = 0.12f + 0.0088f * col;               /* Color: damping q^2, level kept by q (noise power ~ 1/damping) */
        P->dmp = q * q;
        P->ng = NOISE_GAIN * 2.887f * q * (1.0f + 0.0001f * grn * (100.0f - col));   /* Grain loses ~6 dB at dark Color */
        gr = 1.0f - 0.01f * grn;                 /* Grain: chance a sample takes a new random value */
        P->gth = (unsigned int)(int)((0.03f + 0.97f * gr * gr) * 65536.0f);
        s->fz = 50.0f * semis_to_ratio(s->vP);
        s->wd = 0.01f * tk_clampf((float)u[3] + s->vWi, 0.0f, 100.0f);
        P->rv = (u[7] > 0);
        P->fb = 0.55f + 0.4f * (float)u[7] * 0.01f;
        P->wetG = 0.6f * (float)u[7] * 0.01f;
        s->kd = 0;
    }

    P->gT = (s->state == ST_SWEEP || s->state == ST_HOLD) ? 1.0f : 0.0f;
    P->lvl = s->x * s->x * s->vol;
    P->act = ((P->gT > 0.0f || s->g > 0.0f) && P->lvl > 1.0e-7f);
    if (!P->act) s->g = P->gT;                   /* silent anyway: no ramp needed */

    if (P->act && (++s->wt & 3) == 1) {         /* filter and Wind every 4th block (0.7 ms), first one at a start */
        float wd = s->wd;
        xc = s->x * (1.0f + s->vC * (1.0f - s->x));  /* curve bend, still 0..1 and rising */
        f  = s->fz * exp2_oct(0.05f * (float)u[5] * s->vR * xc);   /* Range: 0..5 octaves */
        fc = 8.0f * f; if (fc > 6000.0f) fc = 6000.0f;
        svf = 2.0f * sine_of(0.5f * fc * HZ_TO_INC);
        if ((s->wt & 1023) == 1 && s->wt > 1)    /* Wind: a new random gust target every 0.75 s, two smoothers */
            s->gt = tk_rnd(&s->rngW);
        s->gw[0] += 0.006f * (s->gt - s->gw[0]); s->gw[1] += 0.006f * (s->gw[0] - s->gw[1]);
        P->f = svf * (1.0f + 0.45f * wd * s->gw[1]); if (P->f > 0.95f) P->f = 0.95f;
        P->a = 1.0f + 0.6f * wd * s->gw[1];
    }
}

/* once per block, after tk_prepare: the footswitch LED while the firmware says we are under the cursor */
TK_ALWAYS_INLINE(tk_led)
static inline void tk_led(TkState *s, const int *u, unsigned int me)
{
    volatile unsigned int *b = (volatile unsigned int *)TK_SVC;
    int   lev = -1, lit;
    float bt;
    if (b[0] != SV_MAGIC || b[SV_CURSOR] != me || b[SV_LEDON] == 0u || b[SV_LEDOFF] == 0u) { s->led = -1; return; }
    s->pwm = (s->pwm + 1) & (TK_PWM - 1);
    if (s->state == ST_WAIT) {                   /* armed: a flash on every beat (FREE: every second) */
        bt = s->cph * (float)((u[1] < 5) ? (4 << u[1]) : (u[1] - 4));
        bt -= (float)(int)bt;
        lev = (bt < 0.25f) ? TK_PWM : 0;
    } else if (s->state == ST_SWEEP || s->state == ST_HOLD) {
        lev = (int)(s->x * s->x * (float)TK_PWM + 0.5f);
    }
    if (lev < 0) {                                       /* idle: hand the LED back to the stock task once */
        if (s->led >= 0) { *(volatile unsigned int *)TK_LEDDONE ^= 1u; s->led = -1; }
        return;
    }
    lit = (s->pwm < lev);
    *(volatile unsigned int *)TK_LEDDONE = (*(volatile unsigned int *)TK_LEDDONE & ~1u) | (*(volatile unsigned int *)TK_LEDWANT & 1u);
    *(volatile unsigned int *)(uintptr_t)(lit ? b[SV_LEDON] : b[SV_LEDOFF]) = b[SV_LEDMASK];   /* every block: a flip's stock write is undone at once */
    s->led = lit;
}

/* once per block: the riser (zero when not active), then the reverb, added on both sides. n = 8 */
TK_ALWAYS_INLINE(tk_process)
static inline void tk_process(TkState *s, TkParams *P, float *restrict buf)
{
    int   i, k;
    float o[8], peak = 0.0f;

    for (i = 0; i < 8; i++) o[i] = 0.0f;

    if (P->act) {
        float g = s->g, gs = P->gT > g ? 0.0078125f : -0.015625f;   /* up 3 ms, down 1.5 ms */
        float f = P->f, d = P->dmp, a = P->ng * P->a * P->lvl;
        float lo = s->sv[0], b = s->sv[1], nh = s->nh, q = s->nq;
        unsigned int r = s->rng, gth = P->gth;
        TK_ITER8
        for (i = 0; i < 8; i++) {
            float h;
            r = r * 1664525u + 1013904223u;
            nh = (((r >> 8) & 0xFFFFu) < gth) ? (float)(int)r * 4.656613e-10f : nh;
            q += 0.5f * (nh - q);                /* about 5 kHz: a darker hiss */
            lo += f * b; h = q - lo - d * b; b += f * h;
            g += gs; g = (g > 1.0f) ? 1.0f : g; g = (g < 0.0f) ? 0.0f : g;
            o[i] = g * a * b;
        }
        s->sv[0] = lo; s->sv[1] = b; s->rng = r; s->nh = nh; s->nq = q; s->g = g;
    }

    if (!P->rv) s->rvClr = 0;                    /* reverb off: its state goes stale */
    else if (s->rvClr < RV_TOTAL) {
        volatile float *rp = s->rv;
        int e = s->rvClr + CLR_CHUNK;
        for (i = s->rvClr; i < e; i++) rp[i] = 0.0f;
        s->rvClr = e;
    } else if (P->act || s->quiet < QUIET_BLOCKS) {
        int   w0 = s->rw, a0 = s->apw;
        float *rg = s->rv, y[8], lp = s->lp;
        for (i = 0; i < 8; i++) y[i] = rg[(w0 + i - 1693) & RV_M];
        for (k = 0; k < 4; k++) {
            float *ab = s->rv + RV_N + k * AP_N;
            int   L = ap_len(k);
            for (i = 0; i < 8; i++) {
                int   w = (a0 + i) & AP_M;
                float d = ab[(w - L) & AP_M], wv = y[i] + 0.6f * d;
                ab[w] = wv;
                y[i] = d - 0.6f * wv;
            }
        }
        for (i = 0; i < 8; i++) {
            float ay = y[i] < 0.0f ? -y[i] : y[i];
            lp += 0.4f * (y[i] - lp);            /* tail damping (darker) */
            rg[(w0 + i) & RV_M] = o[i] + P->fb * lp;
            o[i] += P->wetG * y[i];              /* the riser, plus its own tail */
            peak = (ay > peak) ? ay : peak;
        }
        s->lp = lp;
        s->rw = (w0 + 8) & RV_M; s->apw = (a0 + 8) & AP_M;
        s->quiet = (P->act || peak > 1.0e-6f) ? 0 : s->quiet + 1;
    }

    if (P->act || P->rv) {
        for (i = 0; i < 8; i++) { buf[i] = buf[i] + o[i]; buf[i + 8] = buf[i + 8] + o[i]; }
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

/* 7 Space: Off, then 1..100 */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    if (value == 0u) return tk_put(out, 'O', 'f', 'f', 0, 0);
    if (value > 100u) value = 100u;
    return tk_num(out, (int)value, 0);
}

/* ---- pedal entry point (same structure as DubSiren) --------------------- */
#ifndef TAKEOFF_HOST_TEST

#include "takeoff_params.h"
#if TAKEOFF_VOL_SLOT != TAKEOFF_DIR_SLOT + 8
#error "Takeoff reads its 9 knobs as one run of slots"
#endif

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
    int k, foot, hold = 0, run, start, chg = 0;
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

    bpm = zt_update(&s->zt, &run, &beats, &start);
    for (k = 0; k < 9; k++) if (params[TAKEOFF_DIR_SLOT + k] != s->rk[k]) chg = 1;
    if (bpm != s->rk[9]) chg = 1;
    if (chg) {
        int *u = s->u;
        u[0] = tk_ui(params[TAKEOFF_DIR_SLOT],   TAKEOFF_DIR_UI_DEFAULT);
        u[1] = tk_ui(params[TAKEOFF_LEN_SLOT],   TAKEOFF_LEN_UI_DEFAULT);
        u[2] = tk_ui(params[TAKEOFF_COLOR_SLOT], TAKEOFF_COLOR_UI_DEFAULT);
        u[3] = tk_ui(params[TAKEOFF_WIND_SLOT],  TAKEOFF_WIND_UI_DEFAULT);
        u[4] = tk_ui(params[TAKEOFF_GRAIN_SLOT], TAKEOFF_GRAIN_UI_DEFAULT);
        u[5] = tk_ui(params[TAKEOFF_RANGE_SLOT], TAKEOFF_RANGE_UI_DEFAULT);
        u[6] = tk_ui(params[TAKEOFF_VAR_SLOT],   TAKEOFF_VAR_UI_DEFAULT);
        u[7] = tk_ui(params[TAKEOFF_SPACE_SLOT], TAKEOFF_SPACE_UI_DEFAULT);
        u[8] = tk_ui(params[TAKEOFF_VOL_SLOT],   TAKEOFF_VOL_UI_DEFAULT);
        if (u[0] > 1) u[0] = 1;
        if (u[1] > 35) u[1] = 35;
        for (k = 2; k < 9; k++) if (u[k] > 100) u[k] = 100;
        for (k = 0; k < 9; k++) s->rk[k] = params[TAKEOFF_DIR_SLOT + k];
        s->rk[9] = bpm;
        s->kd = 1;
    }
    zw = (volatile unsigned int *)ZT_ADDR;
    if (zw[0] == ZT_MAGIC) {                     /* MOD firmware: footswitch down (6), our hold claim (7) */
        hold = (zw[6] == 1u);
        zw[7] = ctx[1];
    }

    tk_prepare(s, &s->P, s->u, foot, hold, run, bpm);
    tk_led(s, s->u, ctx[1]);
    tk_process(s, &s->P, fxBuf);
}

#endif /* TAKEOFF_HOST_TEST */
