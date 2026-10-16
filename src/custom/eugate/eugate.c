/*
 * eugate.c - "EuGate": tempo-locked rhythmic gate (trance gate), mono
 *
 * Chops whatever goes in (synth pads, vocals, samples, drum machines) into a
 * rhythm. The rhythm is a EUCLIDEAN pattern of Steps (1..64) steps, every step a 16th
 * note. Notes spreads that many notes as evenly as possible across the steps (16 steps:
 * 5 = a clave-like shape, 4 = four on the floor, 16 = every step; notes at or above the
 * number of steps count as "every step"), and Shift turns the pattern around so it starts
 * on a different step. The pattern repeats after Steps steps, whatever the length, so a
 * pattern of 12 or 20 steps against the beat gives POLYMETERS: it drifts against the bar
 * and only lines up again after a common multiple. Steps that are not notes are silent
 * (Mix blends the original back in), and Gap separates notes that
 * touch: when a note is followed straight away by another, the gate closes for the last
 * part of the first one (a share of the step, so it scales with the tempo).
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float
 * division, no libm, no switch, every helper forced inline, no calls.
 * The pattern needs no table and no division: it is built into two 32-bit words once per
 * block. Step j is a note when ((j * Notes) mod Steps) < Notes; the running value is kept
 * by adding Notes and taking Steps away on overflow.
 *
 * TIMING (no division needed)
 *   The tempo is the pedal's (see TEMPO AND MIDI TRANSPORT).
 *   Steps run in PAIRS of 16ths: (1st, 2nd) and (3rd, 4th) of every beat. A phase pp
 *   runs 0..2 and advances by inc = BPM * 4 / (44100 * 60) per sample (a step is a
 *   16th note), so the first step of a pair is pp < 1 + swing and the second is the
 *   rest. Swing therefore delays ONLY the weak 16ths (the 2nd and 4th of each beat, the
 *   "e" and the "a"); the 16ths on the beat and on the "and" never move.
 *   Swing 0 = straight, 100 = the first step lasts 75% of the pair. The swing
 *   grid keeps running in pairs of 16ths whatever Steps is, so it stays on the beat.
 *
 * EDGES
 *   The gate level moves toward its target with a one-pole slew whose time is a
 *   fraction of the step length, so Soft feels the same at every tempo: 0 = hard
 *   (a fraction of a millisecond, a trance chop) .. 100 = very soft (about a third
 *   of a step, a smooth tremolo-like swell).
 *
 * RESET (what restarts the pattern at step 1)
 *   OFF: nothing, the pattern free-runs (and follows the MIDI transport, below).
 *   NOTE: a note that starts after a moment of silence, so every phrase you play
 *   begins on the downbeat of the pattern.
 *   PEDAL: the footswitch being turned on, so the pattern starts exactly when you step
 *   on the pedal. (While the effect is off the pedal still calls the effect; the input
 *   is untouched, but the pattern clock keeps running, so with OFF or NOTE the pattern
 *   comes back in time.)
 *   While the MIDI transport runs, the clock decides where the pattern is and NOTE / PEDAL
 *   do nothing.
 *
 * KNOBS (screen values; the names are kept short and plain on purpose)
 *   0 Notes 0..63   shown 1..64: how many notes play (at or above the step count = every step)
 *   1 Steps 0..63   shown 1..64: how many 16th-note steps the pattern has before it repeats
 *   2 Shift 0..63   starts the pattern later by this many steps
 *   3 Swing 0..100  shuffle: delays only the weak 16ths (see TIMING)
 *   4 Reset 0..2    shown OFF / NOTE / PEDAL: what restarts the pattern (see RESET)
 *   5 Gap   0..50   small silence at the end of a note that is followed by another note,
 *                   in percent of a step (0 = touching notes join, as before)
 *   6 Soft  0..100  how soft the edges of each note are
 *   7 Mix   0..100  dry/wet, DJ-style: dry full up to 50, wet full from 50
 *
 * TEMPO AND MIDI TRANSPORT (MOD firmware 0.4 and later; src/custom/common/zmt.h)
 *   There is no Tempo knob: the tempo is the pedal's (MIDI clock, tap tempo or the patch
 *   TEMPO), read from the MOD firmware's transport block. While the MIDI transport runs the
 *   pattern steps on the clock count: step = (clocks - 1) / 6 (a 16th is 6 clocks) inside
 *   the pattern, so MIDI Start restarts it at step 1, an EuGate loaded mid-song lands on the
 *   right step, and it can't drift. Between two clocks the phase moves on at the tempo but
 *   never past the next clock. After Stop (or no clock for 0.5 s) it runs free at the tempo
 *   from where it was. Without the MOD firmware it runs at 120 BPM.
 *   The Steps knob carries pedal_flags 0x28 (the flag every stock tempo effect has on a knob),
 *   for the pedal's tap tempo button.
 */

#include <stdint.h>
#include "../common/zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define CH_DO_PRAGMA(x) _Pragma(#x)
#define CH_EXPAND_PRAGMA(x) CH_DO_PRAGMA(x)
#define CH_ALWAYS_INLINE(fn) CH_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define CH_CODE_SECTION(fn) CH_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define CH_BLOCK_LOOP() CH_DO_PRAGMA(MUST_ITERATE(8, 8, 8))
#else
#define CH_ALWAYS_INLINE(fn)
#define CH_CODE_SECTION(fn)
#define CH_BLOCK_LOOP()
#endif

#define CH_MAGIC        0x43483039u          /* "CH09" */
#define CH_INC_PER_BPM  3.7793e-7f           /* 1 / (44100 * 60)                */
#define CH_QUIET_LEN    2000                 /* samples of silence before a new note counts */
#define CH_QUIET_LVL    0.002f               /* below this the input counts as silent */
#define CH_NOTE_LVL     0.01f                /* above this a new note has started */
#define CH_FREE_MAX     4.0f                 /* pp limit when not on the clock: none    */

typedef struct {
    unsigned int magic;
    float pp;              /* pair phase 0..2                        */
    unsigned int pos;      /* pattern position of the first step of the pair, 0..steps-1 */
    float g;               /* smoothed gate level                    */
    unsigned int quiet;    /* consecutive quiet samples              */
    unsigned int was_off;  /* set while the footswitch is off (for Reset = PEDAL) */
    unsigned int clk;      /* transport clocks seen last block */
    unsigned int cr, cm;   /* (clk - 1) mod cm, cm = 12 * steps; cm = 0: not valid */
    unsigned int pk_steps, pk_hits;   /* the pattern in lo / hi was built for these (0 = none) */
    unsigned int lo, hi;   /* the Euclid pattern: bit j (lo = steps 0..31, hi = 32..63) is a note */
    ZtSync zt;             /* MIDI transport (zmt.h)          */
} ChState;

typedef struct {
    float inc;             /* phase increment per sample (steps per sample)  */
    float swing;           /* 0..0.5                                          */
    float gap, c, e;       /* gap before a touching note, in steps; slew coefficient, slew rate */
    float ppmax;           /* pp never passes this (the next clock); CH_FREE_MAX = free */
    float dryG, wetG;
    unsigned int steps, shift, sync;   /* pattern length 1..64, rotation (steps - Shift, already < steps) */
    unsigned int lo, hi;           /* the pattern: bit j (lo = steps 0..31, hi = 32..63) is a note */
    float gapA, gapB;              /* gap * length of the first / second step of a pair */
    unsigned int hits;             /* Notes, already <= steps */
} ChParams;

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer. */
CH_ALWAYS_INLINE(ch_ui)
static inline float ch_ui(float raw, float def_ui, float max_ui)
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

CH_ALWAYS_INLINE(ch_init)
static inline void ch_init(ChState *s)
{
    s->pp = 0.0f; s->pos = 0u; s->g = 1.0f; s->quiet = 0u; s->was_off = 0u; s->clk = 0u; s->cm = 0u;
    s->pk_steps = 0u; s->pk_hits = 0u; s->lo = 0u; s->hi = 0u;
    zt_init(&s->zt);
    s->magic = CH_MAGIC;
}

/* u[] = screen values in manifest order */
CH_ALWAYS_INLINE(ch_prepare_core)
static inline void ch_prepare_core(ChParams *P, const float *u)
{
    float bpm, k, e;
    unsigned int steps = (unsigned int)(int)(u[1] + 0.5f) + 1u;      /* 1..64 */
    unsigned int hits  = (unsigned int)(int)(u[0] + 0.5f) + 1u;      /* 1..64 */
    unsigned int shift = (unsigned int)(int)(u[2] + 0.5f);           /* 0..63 */
    if (hits > steps) hits = steps;                      /* more notes than steps = every step */
    while (shift >= steps) shift -= steps;               /* rotation wraps inside the pattern */
    if (shift > 0u) shift = steps - shift;               /* played step = time step - Shift, so the
                                                          * pattern starts LATER by Shift steps */
    P->steps = steps; P->hits = hits; P->shift = shift;
    bpm = u[7];                                          /* the tempo (zmt.h)      */
    P->inc   = bpm * 4.0f * CH_INC_PER_BPM;
    P->swing = u[3] * 0.005f;                            /* 0..0.5 */
    P->gap   = u[5] * 0.01f;                             /* Gap 0..50 % of a step */
    P->gapA  = P->gap * (1.0f + P->swing);               /* x the length of the 1st step of a pair */
    P->gapB  = P->gap * (1.0f - P->swing);               /* ... and of the 2nd */
    k = 1.0f - u[6] * 0.01f;                             /* 1 = hard, 0 = soft */
    e = 3.0f + 600.0f * k * k * k;                       /* slew rate in 1/steps */
    P->e = e;
    P->ppmax = CH_FREE_MAX;
    P->c = P->inc * e;
    if (P->c > 0.5f) P->c = 0.5f;
    P->sync = (unsigned int)(int)(u[4] + 0.5f);               /* 0 OFF, 1 NOTE, 2 PEDAL */
    P->dryG = 2.0f - 2.0f * (u[8] * 0.01f);            /* Mix: dry full up to 50, then fades out */
    if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * (u[8] * 0.01f);                    /* wet fades in up to 50, then full       */
    if (P->wetG > 1.0f) P->wetG = 1.0f;
}

/* The Euclid pattern into P->lo / P->hi, without a division: step j is a note when
 * (j * hits) mod steps < hits; m runs through (j * hits) mod steps by adding hits and taking
 * steps away when it overflows. */
CH_ALWAYS_INLINE(ch_pattern)
static inline void ch_pattern(ChParams *P)
{
    unsigned int j, m = 0u, lo = 0u, hi = 0u;
    for (j = 0u; j < P->steps; j++) {
        if (m < P->hits) { if (j < 32u) lo |= 1u << j; else hi |= 1u << (j - 32u); }
        m += P->hits;
        if (m >= P->steps) m -= P->steps;
    }
    P->lo = lo; P->hi = hi;
}

/* Everything for the block. (The tests call this one.) */
CH_ALWAYS_INLINE(ch_prepare)
static inline void ch_prepare(ChParams *P, const float *u)
{
    ch_prepare_core(P, u);
    ch_pattern(P);
}

/* While the transport runs: put the pattern where the clock count says. 12 clocks = one
 * pair of 16ths. pos and the clock inside the pair come from (clocks - 1) mod (12 * steps)
 * by subtracting shifted copies (no division), so any count works, also mid-song. pp jumps
 * to the clock's place only when a new clock (or Start) arrived; between clocks it moves on
 * at the tempo up to just before the next clock. */
CH_ALWAYS_INLINE(ch_clock_apply)
static inline void ch_clock_apply(ChState *s, ChParams *P, unsigned int clocks, int start)
{
    unsigned int r, m, mk, p = 0u, b, pos;
    P->sync = 0u;                                    /* the clock decides where the pattern is */
    if (clocks == 0u) {                              /* Start seen, first clock not yet: wait on step 1 */
        s->pos = 0u; s->pp = 0.0f; P->ppmax = 0.0f; s->clk = 0u; s->cm = 0u;
        return;
    }
    r = clocks - 1u;
    m = 12u * P->steps;
    if (s->cm == m && clocks >= s->clk && clocks - s->clk <= 24u && s->clk != 0u) {
        r = s->cr + (clocks - s->clk);               /* a few clocks since last block: one or two */
        while (r >= m) r -= m;                       /* subtractions instead of the long loop    */
    } else {
        for (b = 28u; b-- > 0u; ) {
            mk = m << b;
            if ((mk >> b) == m && r >= mk) r -= mk;  /* r = (clocks - 1) mod (12 * steps) */
        }
    }
    s->cr = r; s->cm = m;
    for (b = 7u; b-- > 0u; ) {
        if (r >= (12u << b)) { r -= 12u << b; p += 1u << b; }   /* p = pairs, r = clock 0..11 */
    }
    pos = p + p;
    while (pos >= P->steps) pos -= P->steps;
    s->pos = pos;
    if (clocks != s->clk || start) s->pp = (float)(int)r * 0.16666667f;
    P->ppmax = (float)(int)r * 0.16666667f + 0.1666f;
    if (s->pp > P->ppmax) s->pp = P->ppmax;
    s->clk = clocks;
}

CH_ALWAYS_INLINE(ch_process)
static inline void ch_process(ChState *s, const ChParams *P, float *restrict buf, int n)
{
    int i;
    float pp = s->pp, g = s->g;
    unsigned int pos = s->pos, quiet = s->quiet;
    /* the block's constants in locals (P-> can't stay in registers across stores to buf) */
    const float inc = P->inc, swing = P->swing, ppmax = P->ppmax, c = P->c;
    const float dryG = P->dryG, wetG = P->wetG, split = 1.0f + swing;
    const float gapA = P->gapA, gapB = P->gapB;
    const unsigned int steps = P->steps, shift = P->shift, lo = P->lo, hi = P->hi;
    const unsigned int sync1 = (P->sync == 1u);
    while (pos >= steps) pos -= steps;               /* a shorter Steps than last block: wrap once here, */
    CH_BLOCK_LOOP()
    for (i = 0; i < n; i++) {
        float in = buf[i], a = in, target, end, gl;
        unsigned int j, jn, hit, hitn, low, note, rst;
        a = (a < 0.0f) ? -a : a;
        /* sync: a note after silence restarts the pattern */
        low  = (a < CH_QUIET_LVL);
        note = (a > CH_NOTE_LVL);
        rst  = (quiet >= (unsigned int)CH_QUIET_LEN) & note & sync1;
        quiet = low ? ((quiet < (unsigned int)CH_QUIET_LEN) ? quiet + 1u : quiet) : (note ? 0u : quiet);
        pp  = rst ? 0.0f : pp;
        pos = rst ? 0u : pos;
        /* advance the pair phase (on the clock: never past the next clock) */
        pp += inc;
        pp = (pp > ppmax) ? ppmax : pp;
        rst = (pp >= 2.0f);                          /* next pair: two steps further in the pattern */
        pp  = rst ? pp - 2.0f : pp;
        pos = rst ? pos + 2u : pos;
        pos = (pos >= steps) ? pos - steps : pos;    /* pos < steps in the loop: + 2 needs twice at Steps = 1 */
        pos = (pos >= steps) ? pos - steps : pos;
        rst = (pp >= split);                         /* second step of the pair */
        j   = pos + rst + shift;                     /* pos, 1 and shift are each < steps + 1 */
        end = rst ? 2.0f : split;
        gl  = rst ? gapB : gapA;
        j   = (j >= steps) ? j - steps : j;
        jn  = j + 1u;
        jn  = (jn >= steps) ? 0u : jn;
        hit  = ((((j  < 32u) ? lo : hi) >> (j  & 31u)) & 1u);
        hitn = ((((jn < 32u) ? lo : hi) >> (jn & 31u)) & 1u);
        /* GAP: when the next step is a note too, the gate closes for the last part of this
         * one (a share of the step, so it scales with the tempo), so the two are heard as
         * two notes. The next note's own start is untouched. */
        hitn &= ((end - pp) < gl);
        target = (hit && !hitn) ? 1.0f : 0.0f;
        g += c * (target - g);
        buf[i] = dryG * in + wetG * (in * g);
    }
    if (g < 1e-15f && g > -1e-15f) g = 0.0f;
    s->pp = pp; s->g = g; s->pos = pos; s->quiet = quiet;
}

/* Effect switched off: nothing is processed, but the pattern clock keeps running (n samples
 * on), so the pattern comes back in time with the host's bar flips. */
CH_ALWAYS_INLINE(ch_idle)
static inline void ch_idle(ChState *s, const ChParams *P, int n)
{
    s->pp += P->inc * (float)n;
    if (s->pp > P->ppmax) s->pp = P->ppmax;
    if (s->pp >= 2.0f) {
        s->pp -= 2.0f; s->pos += 2u;
        while (s->pos >= P->steps) s->pos -= P->steps;
    }
}

/* ---- on-screen text ------------------------------------------------------ */
/* knob 0 Notes: screen 0..63 -> "1".."64" */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    int n, t = 0, len = 0;
    if (value > 63u) value = 63u;
    n = (int)value + 1;
    while (n >= 10) { n -= 10; t++; }
    if (t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* knob 1 Steps: screen 0..63 -> "1".."64" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    return ZDL_GetLabel_0(value, out);
}

/* knob 4 Reset: 0 "OFF", 1 "NOTE", 2 "PEDAL" */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value >= 2u) {
        out[0] = 'P'; out[1] = 'E'; out[2] = 'D'; out[3] = 'A'; out[4] = 'L'; out[5] = 0;
        return 5;
    }
    if (value == 1u) { out[0] = 'N'; out[1] = 'O'; out[2] = 'T'; out[3] = 'E'; out[4] = 0; return 4; }
    out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0;
    return 3;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef EUGATE_HOST_TEST

#include "eugate_params.h"

#ifndef EUGATE_AUDIO_FUNC
#define EUGATE_AUDIO_FUNC Fx_DLY_EuGate
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

CH_CODE_SECTION(EUGATE_AUDIO_FUNC)
void EUGATE_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    ChState *s;
    ChParams P;
    float u[9], beats;
    int i, run, start;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(ChState) || span < (end - base)) return;
    if (stateBase + sizeof(ChState) > end) return;

    s = (ChState *)stateBase;

    u[0] = ch_ui(params[EUGATE_NOTES_SLOT],  (float)EUGATE_NOTES_UI_DEFAULT,  63.0f);
    u[1] = ch_ui(params[EUGATE_STEPS_SLOT], (float)EUGATE_STEPS_UI_DEFAULT, 63.0f);
    u[2] = ch_ui(params[EUGATE_SHIFT_SLOT], (float)EUGATE_SHIFT_UI_DEFAULT, 63.0f);
    u[3] = ch_ui(params[EUGATE_SWING_SLOT], (float)EUGATE_SWING_UI_DEFAULT, 100.0f);
    u[4] = ch_ui(params[EUGATE_RESET_SLOT],  (float)EUGATE_RESET_UI_DEFAULT,  2.0f);
    u[5] = ch_ui(params[EUGATE_GAP_SLOT], (float)EUGATE_GAP_UI_DEFAULT, 50.0f);
    u[6] = ch_ui(params[EUGATE_SOFT_SLOT],  (float)EUGATE_SOFT_UI_DEFAULT,  100.0f);
    u[8] = ch_ui(params[EUGATE_MIX_SLOT],   (float)EUGATE_MIX_UI_DEFAULT,   100.0f);

    if (s->magic != CH_MAGIC) ch_init(s);
    u[7] = zt_update(&s->zt, &run, &beats, &start);  /* the pedal's tempo, MIDI transport */
    ch_prepare_core(&P, u);
    if (s->pk_steps != P.steps || s->pk_hits != P.hits) {      /* the pattern only depends on Steps and */
        ch_pattern(&P);                                        /* Notes: rebuilt only when one changes  */
        s->lo = P.lo; s->hi = P.hi; s->pk_steps = P.steps; s->pk_hits = P.hits;
    } else { P.lo = s->lo; P.hi = s->hi; }
    if (run) ch_clock_apply(s, &P, s->zt.clk, start);
    if (params[0] < 0.5f) {                      /* effect switched off: input untouched */
        s->was_off = 1u;
        ch_idle(s, &P, 8);                       /* and the pattern clock keeps running */
        return;
    }
    if (s->was_off) {                            /* the pedal was just turned on */
        if (P.sync == 2u) { s->pp = 0.0f; s->pos = 0u; }
        s->was_off = 0u;
    }
    ch_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* EUGATE_HOST_TEST */
