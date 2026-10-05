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
 *   The tempo knob is the BPM, as in Hydra / Spiral (the pedal's own number).
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
 *   OFF: nothing, the pattern free-runs.
 *   NOTE: a note that starts after a moment of silence, so every phrase you play
 *   begins on the downbeat of the pattern.
 *   PEDAL: the footswitch being turned on, so the pattern starts exactly when you step
 *   on the pedal. (While the effect is off the pedal still calls the effect; the input
 *   is untouched, but the pattern clock keeps running and follows Tempo flips, so with
 *   OFF or NOTE the pattern comes back in time with the bar.)
 *   Whatever Reset says, a SYNC RESET restarts it too: the Tempo knob runs 0..441 and
 *   holds every BPM twice (0..240 = the BPM, 241..441 = a twin copy, BPM = screen - 201,
 *   so past 240 the screen shows 40 again). Flipping between a BPM and its twin
 *   (120 <-> 321) restarts the pattern at step 1 without changing the tempo, so a host
 *   (iPhone, MIDI box) can send one knob edit on each downbeat. A plain tempo change
 *   does not restart the pattern.
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
 *   7 Tempo 0..441  BPM, 40..240 (below 40 reads as 40); 241..441 = the same BPMs again
 *                   (twin copy for the sync reset, see RESET), shown as the BPM
 *   8 Mix   0..100  dry/wet, DJ-style: dry full up to 50, wet full from 50
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define CH_DO_PRAGMA(x) _Pragma(#x)
#define CH_EXPAND_PRAGMA(x) CH_DO_PRAGMA(x)
#define CH_ALWAYS_INLINE(fn) CH_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define CH_CODE_SECTION(fn) CH_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define CH_ALWAYS_INLINE(fn)
#define CH_CODE_SECTION(fn)
#endif

#define CH_MAGIC        0x43483034u          /* "CH04" */
#define CH_BPM_MIN      40.0f
#define CH_BPM_MAX      240.0f
#define CH_TEMPO_MAX    441.0f               /* Tempo screen 0..441: the BPMs twice */
#define CH_TEMPO_TWIN   201.0f               /* twin copy = BPM + 201           */
#define CH_INC_PER_BPM  3.7793e-7f           /* 1 / (44100 * 60)                */
#define CH_QUIET_LEN    2000                 /* samples of silence before a new note counts */
#define CH_QUIET_LVL    0.002f               /* below this the input counts as silent */
#define CH_NOTE_LVL     0.01f                /* above this a new note has started */

typedef struct {
    unsigned int magic;
    float pp;              /* pair phase 0..2                        */
    unsigned int pos;      /* pattern position of the first step of the pair, 0..steps-1 */
    float g;               /* smoothed gate level                    */
    unsigned int quiet;    /* consecutive quiet samples              */
    unsigned int was_off;  /* set while the footswitch is off (for Reset = PEDAL) */
    int   twin;            /* Tempo on its twin copy (1) or not (0); -1 = not read yet */
} ChState;

typedef struct {
    float inc;             /* phase increment per sample (steps per sample)  */
    float swing;           /* 0..0.5                                          */
    float gap, c;          /* gap before a touching note, in steps; slew coefficient */
    float dryG, wetG;
    unsigned int steps, shift, sync;   /* pattern length 1..64, rotation (already < steps) */
    unsigned int lo, hi;           /* the pattern: bit j (lo = steps 0..31, hi = 32..63) is a note */
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

/* Tempo knob (screen 0..441): the pedal passes up to 4.41, so read raw x 100 up to 4.415
 * (ch_ui's 3.05 guess would take 4.41 for an on-screen 4). Returns the screen number. */
CH_ALWAYS_INLINE(ch_tempo_ui)
static inline float ch_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > CH_TEMPO_MAX) ui = CH_TEMPO_MAX;
    return ui;
}

/* Tempo screen number -> BPM: 0..240 as is (at least 40), 241..441 the twin copy
 * (screen - 201), so both copies give 40..240. */
CH_ALWAYS_INLINE(ch_tempo_bpm)
static inline float ch_tempo_bpm(float ui)
{
    if (ui > CH_BPM_MAX) ui -= CH_TEMPO_TWIN;
    if (ui < CH_BPM_MIN) ui = CH_BPM_MIN;
    if (ui > CH_BPM_MAX) ui = CH_BPM_MAX;
    return ui;
}

/* Sync reset: 1 when Tempo moved over to the other copy since the last block (the
 * first block after loading only takes note of the copy). */
CH_ALWAYS_INLINE(ch_twin_flip)
static inline int ch_twin_flip(ChState *s, float tempo_ui)
{
    int tw = (tempo_ui > CH_BPM_MAX) ? 1 : 0;
    int flip = (s->twin >= 0 && tw != s->twin);
    s->twin = tw;
    return flip;
}

CH_ALWAYS_INLINE(ch_init)
static inline void ch_init(ChState *s)
{
    s->pp = 0.0f; s->pos = 0u; s->g = 1.0f; s->quiet = 0u; s->was_off = 0u; s->twin = -1;
    s->magic = CH_MAGIC;
}

/* u[] = screen values in manifest order */
CH_ALWAYS_INLINE(ch_prepare)
static inline void ch_prepare(ChParams *P, const float *u)
{
    float bpm, k, e;
    unsigned int steps = (unsigned int)(int)(u[1] + 0.5f) + 1u;      /* 1..64 */
    unsigned int hits  = (unsigned int)(int)(u[0] + 0.5f) + 1u;      /* 1..64 */
    unsigned int shift = (unsigned int)(int)(u[2] + 0.5f);           /* 0..63 */
    unsigned int j, m = 0u, lo = 0u, hi = 0u;
    if (hits > steps) hits = steps;                      /* more notes than steps = every step */
    while (shift >= steps) shift -= steps;               /* rotation wraps inside the pattern */
    /* Euclid without a division: step j is a note when (j * hits) mod steps < hits; m runs
     * through (j * hits) mod steps by adding hits and taking steps away when it overflows. */
    for (j = 0u; j < steps; j++) {
        if (m < hits) { if (j < 32u) lo |= 1u << j; else hi |= 1u << (j - 32u); }
        m += hits;
        if (m >= steps) m -= steps;
    }
    P->steps = steps; P->shift = shift; P->lo = lo; P->hi = hi;
    bpm = ch_tempo_bpm(u[7]);                            /* both copies: 40..240 */
    P->inc   = bpm * 4.0f * CH_INC_PER_BPM;
    P->swing = u[3] * 0.005f;                            /* 0..0.5 */
    P->gap   = u[5] * 0.01f;                             /* Gap 0..50 % of a step */
    k = 1.0f - u[6] * 0.01f;                             /* 1 = hard, 0 = soft */
    e = 3.0f + 600.0f * k * k * k;                       /* slew rate in 1/steps */
    P->c = P->inc * e;
    if (P->c > 0.5f) P->c = 0.5f;
    P->sync = (unsigned int)(int)(u[4] + 0.5f);               /* 0 OFF, 1 NOTE, 2 PEDAL */
    P->dryG = 2.0f - 2.0f * (u[8] * 0.01f);            /* Mix: dry full up to 50, then fades out */
    if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * (u[8] * 0.01f);                    /* wet fades in up to 50, then full       */
    if (P->wetG > 1.0f) P->wetG = 1.0f;
}

CH_ALWAYS_INLINE(ch_process)
static inline void ch_process(ChState *s, const ChParams *P, float *buf, int n)
{
    int i;
    float pp = s->pp, g = s->g;
    unsigned int pos = s->pos, quiet = s->quiet;
    for (i = 0; i < n; i++) {
        float in = buf[i], a = in, target;
        unsigned int j, jn, hit, hitn;
        float end, len;
        if (a < 0.0f) a = -a;
        /* sync: a note after silence restarts the pattern */
        if (a < CH_QUIET_LVL) {
            if (quiet < (unsigned int)CH_QUIET_LEN) quiet++;
        } else {
            if (quiet >= (unsigned int)CH_QUIET_LEN && a > CH_NOTE_LVL && P->sync == 1u) { pp = 0.0f; pos = 0u; }
            if (a > CH_NOTE_LVL) quiet = 0u;
        }
        /* advance the pair phase */
        pp += P->inc;
        if (pp >= 2.0f) {                            /* next pair: two steps further in the pattern */
            pp -= 2.0f; pos += 2u;
            while (pos >= P->steps) pos -= P->steps;
        }
        j = pos;
        if (pp >= 1.0f + P->swing) { j += 1u; end = 2.0f; len = 1.0f - P->swing; }
        else                       { end = 1.0f + P->swing; len = end; }
        j += P->shift;                               /* rotated; pos, 1 and shift are each < steps + 1 */
        while (j >= P->steps) j -= P->steps;
        jn = j + 1u;
        if (jn >= P->steps) jn = 0u;
        hit  = (j  < 32u) ? ((P->lo >> j)  & 1u) : ((P->hi >> (j  - 32u)) & 1u);
        hitn = (jn < 32u) ? ((P->lo >> jn) & 1u) : ((P->hi >> (jn - 32u)) & 1u);
        if (hit) {
            target = 1.0f;
            /* GAP: when the next step is a note too, the gate closes for the last part of this
             * one (a share of the step, so it scales with the tempo), so the two are heard as
             * two notes. The next note's own start is untouched. */
            if (hitn && (end - pp) < P->gap * len) target = 0.0f;
        } else target = 0.0f;
        g += P->c * (target - g);
        buf[i] = P->dryG * in + P->wetG * (in * g);
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

/* knob 7 Tempo: screen 0..441 -> the BPM "40" .. "240"; the twin copy 241..441 shows
 * the same numbers again */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value > 441u) value = 441u;
    n = (int)ch_tempo_bpm((float)(int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
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
    float u[9];
    int i;

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
    u[7] = ch_tempo_ui(params[EUGATE_TEMPO_SLOT], (float)EUGATE_TEMPO_UI_DEFAULT);
    u[8] = ch_ui(params[EUGATE_MIX_SLOT],   (float)EUGATE_MIX_UI_DEFAULT,   100.0f);

    ch_prepare(&P, u);
    if (s->magic != CH_MAGIC) ch_init(s);
    if (params[0] < 0.5f) {                      /* effect switched off: input untouched */
        s->was_off = 1u;
        if (ch_twin_flip(s, u[7])) { s->pp = 0.0f; s->pos = 0u; }   /* still follows the bar */
        ch_idle(s, &P, 8);                       /* and the pattern clock keeps running */
        return;
    }
    if (s->was_off) {                            /* the pedal was just turned on */
        if (P.sync == 2u) { s->pp = 0.0f; s->pos = 0u; }
        s->was_off = 0u;
    }
    if (ch_twin_flip(s, u[7])) { s->pp = 0.0f; s->pos = 0u; }   /* sync reset: step 1 now */
    ch_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* EUGATE_HOST_TEST */
