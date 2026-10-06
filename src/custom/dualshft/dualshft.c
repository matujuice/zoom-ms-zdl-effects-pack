/*
 * dualshift.c - Tempo-synced modulated dual pitch shifter (mono DSP core)
 *
 * Written for the themanro/ZoomMultistompZDL toolchain (TI C6000, plain C),
 * following the pedal-safe rules in docs/SAFE-DSP-RULES.md:
 *   - no static/const arrays touched by the audio path (they force
 *     code->data relocations and freeze the pedal)
 *   - no float division, no integer / or %, no libm calls (sinf, powf...)
 *     -> ratios come from a polynomial, note values from if-chains
 *        (NOT a switch: a switch can compile to a jump table in .const),
 *        and 1/BPM from a Newton-iteration reciprocal
 *   - ring indexing wraps with a compare, never % or a division
 *
 * Signal flow (mono)
 *   in -> one delay ring (172,032 samples, about 3.9 s)
 *   Voice 1: pitch-shifted echo, delay 1  (12 ms .. 1 s free, or a synced note value)
 *   Voice 2: pitch-shifted echo, delay 2  (12 ms .. 1 s free, or a synced note value)
 *   Each voice reads two taps half a grain apart with a triangle crossfade
 *   (the overlapping-crossfade granular shifter, no clicks).
 *   wet = 0.5 * (A + B), then dry/wet Mix (DJ-style: dry full up to 50,
 *   wet full from 50, both full at 50). No feedback.
 *   One tempo-synced LFO bends the pitch of both voices by up to +-12
 *   semitones (Depth knob), voice B with the opposite sign (180 degrees out
 *   of phase). Depth goes one way only: screen 0 = no LFO, then a curve
 *   with very fine steps at the bottom, and from 3 semitones up it snaps to
 *   whole semitones (screen 71..80 = 3 .. 12 st).
 *
 * DELAY 1 / DELAY 2 (free time, or synced at the top of the knob)
 *   Screen 101..112 = synced to the Tempo knob as a note value: 1/32 1/16T 1/16
 *   1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar. A synced time longer than the
 *   ring (3.9 s) is halved until it fits, so 1bar below about 62 BPM plays a 1/2.
 *   Screen 0..100 free. Milliseconds = 12 + 0.0988 * screen^2, rounded, so the
 *   bottom of the knob is fine-grained (12 ms) and the top is 1000 ms.
 *   The pedal shows the exact time ("12ms" .. "999ms", "1.00s") through the
 *   ZDL_GetLabel_2 / _3 callbacks at the bottom of the DSP section.
 *   12 ms is the shortest possible: the pitch shifter itself needs half a
 *   grain (512 samples, 11.6 ms) of look-behind.

 * LFO shapes (Shape knob, 7 positions; 0..2 keep their old meaning)
 *   0 Tri   triangle   starts at 0 and rises at the phase restart
 *   1 Sqr   square     +1 for the first half of the note, -1 for the second
 *   2 Rand  smooth random: a sine-like glide between random peaks and
 *                      valleys. Every half cycle it heads for a new extreme of
 *                      random height (30..100% of Depth) that alternates in
 *                      sign; voice B mirrors it. Never hops.
 *   3 Step  random sample and hold: one new random value in [-1,1) per LFO
 *                      cycle (and on every restart), held until the next cycle,
 *                      so the pitch hops to a new random offset each cycle.
 *   4 Sine  smooth sine, starts at 0 and rises (vibrato)
 *   5 Rise  sawtooth ramp from -1 up to +1 over each note, then jumps back
 *   6 Fall  sawtooth ramp from +1 down to -1 over each note, then jumps back
 *   Voice B always gets the opposite sign.
 *
 * TEMPO (LFO only; the delays no longer use it)
 *   A custom ZDL cannot read the pedal's global BPM or its tap button
 *   (docs/TEMPO-SYNC.md), so the BPM comes from the Tempo knob, like the
 *   shipped Hydra / Spiral / Spool. The LFO phase restarts whenever the
 *   Tempo knob moves.
 *   SYNC RESET: the knob runs 0..441 and holds every BPM twice. 0..240 is
 *   the BPM (0..39 = FOLLOW, see BAR TAG); 241..441 is a twin copy, BPM = screen -
 *   201, so past 240 the screen shows 40 again. Both copies show the same
 *   number. Flipping between a BPM and its twin (120 <-> 321) restarts the
 *   LFO without changing the tempo, so a host (iPhone, MIDI box) can send
 *   one knob edit on each downbeat to keep the LFO on the bar.
 *   While the effect is switched off the input is untouched, but the LFO
 *   keeps running and follows Tempo flips, so it comes back on the bar.
 *
 * ON-SCREEN TEXT (ZDL_GetLabel_<knob index>, value = screen number)
 *   Ptch1/2    -24 .. -1, -0.9 .. -0.1, 0, +0.1 .. +0.9, +1 .. +24
 *   Dly1/Dly2  12ms .. 1.00s, then 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar
 *   Depth      0 .. 2.00 st in fine steps, then whole semitones 3 .. 12
 *   Div        4bar 3bar 2bar 1.5b 1bar 1/2. 1/2 1/4. 1/4 1/8. 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64
 *   Shape      Tri Sqr Rand Step Sine Rise Fall   Tempo: the BPM, 40 .. 240, on both copies
 *   Mix: plain number
 *   The functions write characters one by one (no string literals, no
 *   tables, no calls), so they need no data relocations.
 *
 * KNOBS (9 = the maximum, 3 pages x 3), each scaled to 0..1 by ds_knob()
 *   On the pedal: page 1 Ptch1 Ptch2 Dly1, page 2 Dly2 Div Depth, page 3 Shape
 *   Tempo Mix. Tempo is the 8th knob on every twin-Tempo effect of the pack, so a
 *   sync host sends one knob number whatever the effect (Luca, 2026-10-05).
 *   Inside the code k[] keeps its own order:
 *     k[0] Ptch1  k[1] Ptch2  k[2] Dly1  k[3] Dly2  k[4] Tempo  k[5] Div
 *     k[6] Depth  k[7] Shape  k[8] Mix   (default 50)
 *
 * DEFAULTS: the value the pedal shows at load lives in the effect's
 * manifest (manifest_pedal.json), not in this file. Because the repo notes
 * say params can read 0 until a knob is touched, ds_prepare substitutes the
 * DEF_* values below when the WHOLE table reads exactly 0. Keep the
 * manifest defaults equal to these.
 *
 * FILES: this file + manifest.json + build.py go in src/custom/dualshft/.
 * The pedal entry point Fx_DLY_DualShft is at the bottom; define
 * DUALSHIFT_HOST_TEST to compile only the DSP core on a PC.
 *
 * CONFIRMED ON HARDWARE (MS-70CDR, tested by the user 2026-09-30):
 *   - the on-screen knob text (ZDL_GetLabel_ callbacks)
 *   - delay changes while playing
 *   - patch reload behaviour
 * NOT CHECKED ON HARDWARE YET: the 44.1 kHz assumption written down as
 * exact delay times, long-term DSP load, behaviour across bypass toggling.
 *
 * BAR TAG (src/custom/common/drytag.h, docs/TEMPO-SYNC.md "Bar tag")
 *   Mozaic can only edit slots 1-3. While Mozaic flips this effect's Tempo, it writes a bar
 *   tag into the Dry buffer's right half for the slots after it. In any slot it reads a tag
 *   from earlier slots, and each new bar there restarts it as a twin flip of its own knob
 *   would (ignored while it is getting flips itself). Tempo 0..39 = FOLLOW (shown FOLLW):
 *   the BPM comes from the tag too, 120 when there is none.
 */

#include <stdint.h>
#include "../common/drytag.h"

/* TI toolchain pragmas, same pattern as TapeEcho4: every helper is forced
 * inline so the object carries no .text and no local call relocations.
 * Plain gcc (host tests) ignores them. */
#ifdef __TI_COMPILER_VERSION__
#define DS_DO_PRAGMA(x) _Pragma(#x)
#define DS_EXPAND_PRAGMA(x) DS_DO_PRAGMA(x)
#define DS_ALWAYS_INLINE(fn) DS_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define DS_CODE_SECTION(fn) DS_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define DS_ALWAYS_INLINE(fn)
#define DS_CODE_SECTION(fn)
#endif

#define FS_HZ            44100.0f
#define RING_SIZE        172032              /* 168 * 1024 floats = 688,128 B, 3.9 s (arena >= 705,536 B) */
#define GRAIN_LEN        1024.0f             /* crossfade window W, ~23 ms     */
#define HALF_GRAIN       512.0f
#define INV_GRAIN_LEN    0.0009765625f       /* 1/1024, written as a literal   */
#define MAX_DLY_SAMPLES  44200.0f            /* safety clamp (1 s = 44100)     */
#define MAX_SYNC_DLY     171000.0f           /* synced delays: ring minus a grain */
#define MS_TO_SAMPLES    44.1f               /* samples per millisecond        */
#define BPM_TO_INC       3.77929e-7f         /* 1 / (60 * 44100)               */
#define FADE_STEP        0.005f              /* wet fade per sample, ~4.5 ms   */
#define DLY_DEADBAND     1.0f                /* target move (samples) that counts */
#define CLEAR_CHUNK      1024                /* lazy ring clear per call       */

/* Tempo knob: range 0..441 in the manifest, every BPM twice (sync reset).
 * Screen 40..240 IS the BPM (0..39 = FOLLOW, see BAR TAG); 241..441 is the twin copy,
 * BPM = screen - 201. The pedal hands over screen/100 (up to 4.41);
 * ds_tempo_ui() turns that back into the screen number and ds_tempo_bpm()
 * into the BPM. */
#define BPM_MIN          40
#define BPM_MAX          240
#define TEMPO_MAX_F      441.0f
#define TEMPO_TWIN       201                 /* twin copy = BPM + 201          */

#define DS_MAGIC         0x44533037u         /* "DS07": arena holds valid state */

#define SHAPE_TRI        0
#define SHAPE_SQUARE     1
#define SHAPE_SH         2
#define SHAPE_STEP       3
#define SHAPE_SINE       4
#define SHAPE_RISE       5
#define SHAPE_FALL       6

/* Fallback knob values when the whole parameter table reads 0. */
#define DEF_PITCH_A      0.7424242f          /* screen 49 = +7 st              */
#define DEF_PITCH_B      0.2878788f          /* screen 19 = -5 st              */
#define DEF_DELAY_A      0.625f              /* screen 70 = 496 ms (70/112)    */
#define DEF_DELAY_B      0.44642857f         /* screen 50 = 259 ms (50/112)    */
#define DEF_TEMPO        0.27210884f         /* 120 BPM (120/441)              */
#define DEF_DIV          0.5f                /* screen 8 = quarter note LFO    */
#define DEF_DEPTH        0.3625f             /* screen 29 = 0.43 semitone      */
#define DEF_SHAPE        0.0f                /* triangle                       */
#define DEF_MIX          0.5f                /* 50                             */

/* ------------------------------------------------------------------ */
/* State: lives at the base of the per-instance arena (ctx[3][0]).     */
/* about 262 KB total; the arena is proven to be at least 705,536 B.   */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned int magic;    /* DS_MAGIC once initialised (see below)     */
    int   w;               /* index of the newest sample in ring        */
    int   clear_pos;       /* lazy-clear progress, RING_SIZE when done  */
    float p1, p2;          /* grain sweep phase 0..1, voice A / B       */
    float lfo_phase;       /* 0..1                                      */
    float sh;              /* current sample&hold value, -1..+1         */
    unsigned int rng;      /* xorshift32 state (never 0)                */
    float rfrom;           /* smooth random: level the glide starts from */
    float rto;             /* smooth random: peak/valley it glides to    */
    int   rseg;            /* smooth random: which half cycle we are in  */
    int   rsign;           /* smooth random: sign of the current target  */
    float dlyA, dlyB;      /* delays in use, samples, <0 = not set      */
    float gA, gB;          /* wet fade gain per voice, 0..1             */
    float last_tempo;      /* Tempo screen number at the last LFO restart */
    DtSync sync;           /* bar tag to and from other slots (drytag.h) */
    float ring[RING_SIZE]; /* input history                             */
} DualShift;

/* Per-block derived parameters, computed once per 8-sample block. */
typedef struct {
    float ratioA, ratioB;  /* base pitch ratios                         */
    float dlyA, dlyB;      /* target delays in samples                  */
    float lfo_inc;         /* LFO phase increment per sample            */
    float depth;           /* LFO swing in semitones, -12..+12 (signed) */
    float dryG, wetG;      /* Mix gains, 0..1, both 1 at Mix 50         */
    int   shape;           /* SHAPE_* 0..6 (see the list at the top)    */
    int   retrig;          /* 1 = restart LFO phase at the top of block */
} DualShiftParams;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
DS_ALWAYS_INLINE(clamp01)
static inline float clamp01(float x)
{
    if (!(x >= 0.0f)) x = 0.0f;              /* also catches NaN */
    if (x > 1.0f) x = 1.0f;
    return x;
}

/* 2^(semis/12) via 5th-order Taylor of e^(semis * ln2/12).
 * Worst case (+-12 st) error is ~0.3 cent. No libm, no division. */
/* The pedal stores every knob as (on-screen number) / 100, whatever the knob's
 * own maximum (Stasis and Rooms6 rely on this). So Delay 3 arrives as 0.03 and
 * Tempo 120 arrives as 1.2. Convert back to the on-screen integer, then scale
 * to 0..1 by the knob's real maximum (inv_max = 1 / max, a literal). Anything
 * out of range (NaN, garbage) falls back to the manifest default. */
DS_ALWAYS_INLINE(ds_knob)
static inline float ds_knob(float raw, float def_ui, float inv_max)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.0f) ui = raw * 100.0f;
    else ui = raw;                           /* already an on-screen number */
    ui = (float)(int)(ui + 0.5f);
    return clamp01(ui * inv_max);
}

/* Tempo knob (screen 0..441): the pedal passes up to 4.41, so read raw x 100
 * up to 4.415 (ds_knob's 3.0 guess would take 4.41 for an on-screen 4).
 * Returns the screen number. */
DS_ALWAYS_INLINE(ds_tempo_ui)
static inline float ds_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;                           /* already an on-screen number */
    ui = (float)(int)(ui + 0.5f);
    if (ui > TEMPO_MAX_F) ui = TEMPO_MAX_F;
    return ui;
}

/* Tempo screen number -> BPM: 0..240 as is (at least 40), 241..441 the twin
 * copy (screen - 201), so both copies give 40..240. */
DS_ALWAYS_INLINE(ds_tempo_bpm)
static inline int ds_tempo_bpm(int ui)
{
    if (ui > BPM_MAX) ui -= TEMPO_TWIN;
    if (ui < BPM_MIN) ui = BPM_MIN;
    return ui;
}

DS_ALWAYS_INLINE(semis_to_ratio)
static inline float semis_to_ratio(float semis)
{
    float e  = semis * 0.05776227f;          /* ln(2)/12 */
    float e2 = e * e;
    return 1.0f + e + e2 * (0.5f + e * (0.16666667f
                 + e * (0.041666668f + e * 0.008333334f)));
}

/* Base pitch for -24..+24 semitones: one octave is split off exactly
 * (x2 or x0.5), so the polynomial above never sees more than +-12. */
DS_ALWAYS_INLINE(semis_to_ratio_wide)
static inline float semis_to_ratio_wide(float semis)
{
    float oct = 1.0f;
    if (semis > 12.0f)       { semis -= 12.0f; oct = 2.0f; }
    else if (semis < -12.0f) { semis += 12.0f; oct = 0.5f; }
    return oct * semis_to_ratio(semis);
}

/* Div index 0..16 -> LFO cycles per beat (4/4: one bar = 4 beats = 16 steps).
 *   0 4 bars (64 steps) 0.0625    6 1/2   (8 steps)   0.5      12 1/16  4
 *   1 3 bars (48 steps) 0.08333   7 1/4.  (6 steps)   0.6667   13 1/16T 6
 *   2 2 bars (32 steps) 0.125     8 1/4   (4 steps)   1        14 1/32  8
 *   3 1.5 bars (24)     0.16667   9 1/8.              1.3333   15 1/32T 12
 *   4 1 bar  (16 steps) 0.25     10 1/8               2        16 1/64  16
 *   5 1/2.  (12 steps)  0.3333   11 1/8T              3                      */
DS_ALWAYS_INLINE(subdiv_mult)
static inline float subdiv_mult(int idx)
{
    if (idx <= 0) return 0.0625f;
    if (idx == 1) return 0.08333334f;
    if (idx == 2) return 0.125f;
    if (idx == 3) return 0.16666667f;
    if (idx == 4) return 0.25f;
    if (idx == 5) return 0.33333334f;
    if (idx == 6) return 0.5f;
    if (idx == 7) return 0.6666667f;
    if (idx == 8) return 1.0f;
    if (idx == 9) return 1.3333334f;
    if (idx == 10) return 2.0f;
    if (idx == 11) return 3.0f;
    if (idx == 12) return 4.0f;
    if (idx == 13) return 6.0f;
    if (idx == 14) return 8.0f;
    if (idx == 15) return 12.0f;
    return 16.0f;
}

/* Synced delay (Dly knob 101..112): the delay as a note value, in beats (quarter notes):
 * 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar */
DS_ALWAYS_INLINE(dly_note_beats)
static inline float dly_note_beats(int d)
{
    if (d <= 0) return 0.125f;
    if (d == 1) return 0.16666667f;
    if (d == 2) return 0.25f;
    if (d == 3) return 0.33333334f;
    if (d == 4) return 0.375f;
    if (d == 5) return 0.5f;
    if (d == 6) return 0.6666667f;
    if (d == 7) return 0.75f;
    if (d == 8) return 1.0f;
    if (d == 9) return 1.5f;
    if (d == 10) return 2.0f;
    return 4.0f;
}

/* 1 / x for x > 0: a first guess from the float bits, then three Newton steps (no divide) */
DS_ALWAYS_INLINE(ds_recip)
static inline float ds_recip(float x)
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

/* Delay knob screen 0..112 -> delay in samples. 0..100 free: whole milliseconds,
 * 12 + 0.0988 * screen^2 (the label callbacks use the same expression); 101..112 a
 * note value at the Tempo BPM, halved until it fits the ring (1 bar below ~62 BPM). */
DS_ALWAYS_INLINE(dly_samples)
static inline float dly_samples(int n, float spb)
{
    float d;
    int   ms;
    if (n > 100) {
        d = dly_note_beats(n - 101) * spb;
        while (d > MAX_SYNC_DLY) d *= 0.5f;
        return d;
    }
    ms = (int)(12.0f + 0.0988f * (float)(n * n) + 0.5f);
    d  = (float)ms * MS_TO_SAMPLES;
    if (d > MAX_DLY_SAMPLES) d = MAX_DLY_SAMPLES;
    return d;
}

/* Pitch knob, screen 0..66 -> tenths of a semitone (-240 .. +240). Whole semitones
 * everywhere except between -1 and +1, where there are tenths (-0.9 .. +0.9):
 *   0..23  -24 .. -1     24..32  -0.9 .. -0.1     33  0
 *   34..42 +0.1 .. +0.9  43..66  +1 .. +24                                   */
DS_ALWAYS_INLINE(pitch_tenths)
static inline int pitch_tenths(int n)
{
    if (n <= 23) return (n - 24) * 10;
    if (n <= 42) return n - 33;
    return (n - 42) * 10;
}

/* Depth knob, screen 0..80 -> semitones of swing (one direction):
 *   0 = none; 1..70 = a gentle curve from 0.005 to 2.00, with exactly 1.00 at 48
 *   and exactly 2.00 at 70; 71..80 = whole semitones 3 .. 12. */
DS_ALWAYS_INLINE(depth_st)
static inline float depth_st(int n)
{
    if (n == 48) return 1.0f;
    if (n == 70) return 2.0f;
    if (n <= 70) return (float)n * (0.005f + 0.00033673f * (float)n);
    return (float)(n - 68);
}

/* Triangle, ph in [0,1) -> [-1,+1]. Starts at 0 and rises, so a phase
 * restart gives neutral pitch exactly at the restart. */
DS_ALWAYS_INLINE(lfo_tri)
static inline float lfo_tri(float ph)
{
    float q = ph + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
}

/* Random value in [-1,1). xorshift32 (shifts and xors only: no multiply, no
 * division). The earlier LCG had a visible pattern in consecutive draws
 * (every third value nearly repeated), which is very audible with Step. */
DS_ALWAYS_INLINE(next_rand)
static inline float next_rand(unsigned int *rng)
{
    unsigned int x = *rng;
    if (x == 0u) x = 0x1234ABCDu;              /* xorshift must never sit at 0 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return (float)(int)(x >> 8) * 1.1920929e-7f - 1.0f;      /* 2^-23 */
}

/* Linear-interpolated read, d samples behind the newest sample.
 * Requires 0 <= d < RING_SIZE - 1. (int) cast of a float is a native
 * truncate on C6000; verify it produces no __c6xabi_ helper in the map. */
DS_ALWAYS_INLINE(ring_read)
static inline float ring_read(const float *ring, int w, float d)
{
    int   di = (int)d;
    float fr = d - (float)di;
    int   i1 = w - di;
    int   i2;
    float a, b;
    if (i1 < 0) i1 += RING_SIZE;
    i2 = i1 - 1;
    if (i2 < 0) i2 += RING_SIZE;
    a = ring[i1];
    b = ring[i2];
    return a + fr * (b - a);
}

/* One pitch-shift voice, one sample. `dly` is the delay this voice
 * should have ON AVERAGE.
 * The sweep phase p in [0,1) maps to a tap delay of (dly - W/2) + p*W, so
 * the two taps average to `dly` whatever p is; with ratio == 1 the sweep
 * stands still and the delay is exactly `dly`.
 * Reading faster than real time (ratio > 1) makes the delay shrink, so p
 * moves by (1 - ratio)/W per sample. Tap 2 sits half a grain away. Each
 * tap is silent exactly when it wraps (triangle window), and the two
 * windows sum to 1, which is what removes the click.                    */
DS_ALWAYS_INLINE(voice_step)
static inline float voice_step(const float *ring, int w, float *pp,
                        float ratio, float dly)
{
    float p = *pp + (1.0f - ratio) * INV_GRAIN_LEN;
    float base = dly - HALF_GRAIN;
    float pb, wa, ya, yb;

    if (p >= 1.0f) p -= 1.0f;
    if (p <  0.0f) p += 1.0f;
    *pp = p;

    pb = p + 0.5f;
    if (pb >= 1.0f) pb -= 1.0f;

    wa = (p < 0.5f) ? (p + p) : (2.0f - (p + p));   /* tap 1 gain; tap 2 = 1-wa */

    ya = ring_read(ring, w, base + p  * GRAIN_LEN);
    yb = ring_read(ring, w, base + pb * GRAIN_LEN);
    return wa * ya + (1.0f - wa) * yb;
}

/* Follow a delay target without a pitch swoop. Sliding the read point
 * across tens of thousands of samples would play the ring at several
 * times real speed (even backwards), so instead: when the target moves,
 * fade this voice out, jump the read point while it is silent, fade it
 * back in (~4.5 ms each way). While the target stays put the voice is
 * at full level. Returns the wet gain for this sample.                  */
DS_ALWAYS_INLINE(dly_follow)
static inline float dly_follow(float *cur, float *g, float target)
{
    float d = target - *cur;
    if (d < 0.0f) d = -d;
    if (d > DLY_DEADBAND) {
        *g -= FADE_STEP;
        if (*g <= 0.0f) { *g = 0.0f; *cur = target; }
    } else {
        *g += FADE_STEP;
        if (*g > 1.0f) *g = 1.0f;
    }
    return *g;
}

/* ------------------------------------------------------------------ */
/* Public functions                                                    */
/* ------------------------------------------------------------------ */

DS_ALWAYS_INLINE(ds_init)
static inline void ds_init(DualShift *s)
{
    s->w = 0;
    s->clear_pos = 0;
    s->p1 = 0.0f;
    s->p2 = 0.0f;
    s->lfo_phase = 0.0f;
    s->rng = 0x1234ABCDu;      /* any non-zero seed */
    s->sh = next_rand(&s->rng);
    s->rfrom = 0.0f;
    s->rto = 0.7f;
    s->rseg = 0;
    s->rsign = 1;
    s->dlyA = -1.0f;           /* snap to the target on the first block */
    s->dlyB = -1.0f;
    s->gA = 0.0f;              /* wet fades in after load */
    s->gB = 0.0f;
    s->last_tempo = -1.0f;     /* forces one restart on the first block */
    dt_sync_init(&s->sync);
    s->magic = DS_MAGIC;
}

/* Fresh-instance detection. The repo documents no init-time hook that is
 * safe for this (calling stock handlers from a custom _init froze the
 * pedal on boot), and its stateful effects (StereoChorus, ToTape9) clear
 * their ctx[3] state lazily from the audio function. So the arena is
 * treated as possibly holding garbage: it is only trusted when the marker
 * matches, and is re-initialised otherwise. TODO(HW): confirm across
 * bypass, preset switch and reload. */
DS_ALWAYS_INLINE(ds_ensure_init)
static inline void ds_ensure_init(DualShift *s)
{
    if (s->magic != DS_MAGIC) ds_init(s);
}

/* Once per block. kraw[] are the 9 knob values scaled to 0..1 by each knob's own maximum (ds_knob) (layout in
 * the header comment).
 *
 * Timing, no division anywhere:
 *   tempo      = round(Tempo * 441)                (= the screen number)
 *   bpm        = tempo, or tempo - 201 on the twin copy; at least 40
 *   LFO inc    = bpm * div_mult * (1 / (60 * FS))              [per sample]
 *   delay      = round(12 + 0.0988 * screen^2) ms * 44.1  [samples]
 *
 * "Tap" replacement: the pedal's tap button is invisible to a ZDL, so the
 * LFO phase restarts whenever the Tempo knob moves. Set the tempo, stop
 * turning, and the cycle starts from zero at that moment. Flipping to the
 * twin copy of the same BPM restarts it too, with no change of tempo.   */
DS_ALWAYS_INLINE(ds_prepare)
static inline void ds_prepare(DualShift *s, DualShiftParams *P, const float *kraw)
{
    float k[9];
    int   i, allzero = 1;
    int   sa, sb, ia, ib, idx;
    float bpm, spb;
    int   tempo_i;

    ds_ensure_init(s);

    for (i = 0; i < 9; i++) {
        k[i] = clamp01(kraw[i]);
        if (i != 4 && kraw[i] != 0.0f) allzero = 0;   /* Tempo comes through dt_tempo, never 0 */
    }
    if (allzero) {                       /* table not materialised yet */
        k[0] = DEF_PITCH_A;  k[1] = DEF_PITCH_B;
        k[2] = DEF_DELAY_A;  k[3] = DEF_DELAY_B;
        k[4] = DEF_TEMPO;    k[5] = DEF_DIV;
        k[6] = DEF_DEPTH;    k[7] = DEF_SHAPE;
        k[8] = DEF_MIX;
    }

    sa  = pitch_tenths((int)(k[0] * 66.0f + 0.5f));   /* screen 0..66 -> tenths of a semitone */
    sb  = pitch_tenths((int)(k[1] * 66.0f + 0.5f));
    ia  = (int)(k[2] * 112.0f + 0.5f);            /* screen 0..112: free ms, then note values */
    ib  = (int)(k[3] * 112.0f + 0.5f);
    idx = (int)(k[5] * 16.0f + 0.5f);            /* screen 0..16 */

    tempo_i = (int)(k[4] * TEMPO_MAX_F + 0.5f);   /* screen number 0..441     */
    bpm = (float)ds_tempo_bpm(tempo_i);

    if ((float)tempo_i != s->last_tempo) {        /* Tempo moved (or flipped to its twin): restart LFO */
        P->retrig = 1;
        s->last_tempo = (float)tempo_i;
    } else {
        P->retrig = 0;
    }
    spb = 2646000.0f * ds_recip(bpm);             /* samples per beat (60 x 44100 / BPM) */

    P->ratioA  = semis_to_ratio_wide(0.1f * (float)sa);
    P->ratioB  = semis_to_ratio_wide(0.1f * (float)sb);
    P->dlyA    = dly_samples(ia, spb);   /* average delay of the voice */
    P->dlyB    = dly_samples(ib, spb);
    P->lfo_inc = bpm * subdiv_mult(idx) * BPM_TO_INC;
    P->depth = depth_st((int)(k[6] * 80.0f + 0.5f));   /* see depth_st */
    P->shape   = (int)(k[7] * 6.0f + 0.5f);         /* screen 0..6 */
    P->dryG = 2.0f - 2.0f * (k[8]);            /* Mix: dry full up to 50, then fades out */
    if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * (k[8]);                    /* wet fades in up to 50, then full       */
    if (P->wetG > 1.0f) P->wetG = 1.0f;
}

/* Main loop. buf points at the 8 samples of the effect buffer and is
 * processed in place. The restart is applied at the top of the block
 * (<= 8 samples of jitter, ~0.2 ms). */
DS_ALWAYS_INLINE(ds_process)
static inline void ds_process(DualShift *s, const DualShiftParams *P,
                       float *buf, int n)
{
    int   i;
    int   w;
    float p1, p2, ph, sh, dA, dB, gA, gB;
    unsigned int rng;
    float rfrom, rto;
    int rseg, rsign;
    float *ring = s->ring;

    ds_ensure_init(s);

    /* Zero the ring lazily; dry passes through untouched meanwhile.
     * 65536 / 1024 = 64 blocks, about 12 ms. */
    if (s->clear_pos < RING_SIZE) {
        /* volatile keeps the compiler from turning this zero loop into a
         * memset() call, which the loader could not resolve. */
        volatile float *rp = ring;
        int end = s->clear_pos + CLEAR_CHUNK;
        for (i = s->clear_pos; i < end; i++) rp[i] = 0.0f;
        s->clear_pos = end;
        return;
    }

    w   = s->w;
    p1  = s->p1;  p2 = s->p2;
    ph  = s->lfo_phase;
    sh  = s->sh;
    rng = s->rng;
    rfrom = s->rfrom; rto = s->rto; rseg = s->rseg; rsign = s->rsign;
    dA  = s->dlyA; dB = s->dlyB;
    gA  = s->gA;   gB = s->gB;
    if (dA < 0.0f) { dA = P->dlyA; dB = P->dlyB; }   /* first block: snap */

    if (P->retrig) {
        ph = 0.0f;
        sh = next_rand(&rng);
        rseg = 0; rfrom = 0.0f; rsign = 1;
        rto = 0.3f + 0.35f * (next_rand(&rng) + 1.0f);
    }

    for (i = 0; i < n; i++) {
        float dry = buf[i];
        float lfo, m, yA, yB, gainA, gainB;

        /* write newest sample */
        w = w + 1;
        if (w >= RING_SIZE) w = 0;
        ring[w] = dry;

        /* follow the synced delay times (fade / jump / fade, see above) */
        gainA = dly_follow(&dA, &gA, P->dlyA);
        gainB = dly_follow(&dB, &gB, P->dlyB);

        /* tempo-synced LFO, updated every sample */
        ph += P->lfo_inc;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            sh = next_rand(&rng);          /* new S&H step each cycle */
        }
        {   /* smooth random: every half cycle glide on to a new peak or
             * valley of random height, sign alternating */
            int seg = (ph >= 0.5f);
            if (seg != rseg) {
                rseg = seg;
                rfrom = rto;
                rsign = -rsign;
                rto = (float)rsign * (0.3f + 0.35f * (next_rand(&rng) + 1.0f));
            }
        }
        if (P->shape == SHAPE_TRI)         lfo = lfo_tri(ph);
        else if (P->shape == SHAPE_SQUARE) lfo = (ph < 0.5f) ? 1.0f : -1.0f;
        else if (P->shape == SHAPE_SINE) {
            /* sin(2*pi*ph) = sin(pi/2 * triangle): odd minimax polynomial,
             * error < 1e-4, no libm */
            float t = lfo_tri(ph);
            float t2 = t * t;
            lfo = t * (1.5706268f + t2 * (-0.6432292f + t2 * 0.0727102f));
        }
        else if (P->shape == SHAPE_RISE)   lfo = ph + ph - 1.0f;
        else if (P->shape == SHAPE_FALL)   lfo = 1.0f - (ph + ph);
        else if (P->shape == SHAPE_SH) {
            /* sine-shaped (smoothstep) glide from the last extreme to the next */
            float t = (ph < 0.5f) ? (ph + ph) : (ph + ph - 1.0f);
            float e = t * t * (3.0f - 2.0f * t);
            lfo = rfrom + (rto - rfrom) * e;
        }
        else                               lfo = sh;   /* Step */
        m = P->depth * lfo;                /* LFO pitch offset in semitones */

        /* Voice A is bent by +m semitones, voice B by -m (180 deg apart).
         * The LFO factor is its own exact 2^(m/12)
         * polynomial (+-12 st, ~0.3 cent), multiplied onto the base
         * ratio, so the two errors never add up across a +-24 range. */
        yA = voice_step(ring, w, &p1, P->ratioA * semis_to_ratio(m),  dA);
        yB = voice_step(ring, w, &p2, P->ratioB * semis_to_ratio(-m), dB);

        /* mono: average the two voices, then dry/wet */
        buf[i] = P->dryG * dry + P->wetG * (0.5f * (yA * gainA + yB * gainB));
    }

    s->w = w;
    s->p1 = p1;
    s->p2 = p2;
    s->lfo_phase = ph;
    s->sh = sh;
    s->rng = rng;
    s->rfrom = rfrom; s->rto = rto; s->rseg = rseg; s->rsign = rsign;
    s->dlyA = dA;
    s->dlyB = dB;
    s->gA = gA;
    s->gB = gB;
}

/* ------------------------------------------------------------------ */
/* On-screen knob text. The pedal calls ZDL_GetLabel_<knob index>(value,  */
/* out) with the knob's screen number and an 8-byte buffer; the function  */
/* writes the text and returns its length (same contract as the stock     */
/* RndmFLTR GetString, used by Stasis / Rooms / Hydra in this repo).      */
/* Each function is self-contained: characters are stored one by one (no  */
/* string literals, no tables), no division, no calls. At most 5 chars    */
/* are used so the text fits the pedal column. Knob indices: 0 Ptch1,    */
/* 1 Ptch2, 2 Dly1, 3 Dly2, 4 Div, 5 Depth, 6 Shape, 7 Tempo, 8 Mix.      */
/* Mix has no callback and shows a plain number.                          */
/* ------------------------------------------------------------------ */

/* Pitch text: screen 0..66 -> "-24" .. "-1", "-0.9" .. "-0.1", "0", "+0.1" .. "+0.9",
 * "+1" .. "+24" (same table as pitch_tenths; v = tenths of a semitone). */
#define DS_PITCH_LABEL(fn)                                                 \
int fn(unsigned int value, char *out)                                      \
{                                                                          \
    int n, v, t = 0, len = 0;                                              \
    if (value > 66u) value = 66u;                                          \
    n = (int)value;                                                        \
    if (n <= 23) v = (n - 24) * 10;                                        \
    else if (n <= 42) v = n - 33;                                          \
    else v = (n - 42) * 10;                                                \
    if (v < 0) { out[len] = '-'; len++; v = -v; }                          \
    else if (v > 0) { out[len] = '+'; len++; }                             \
    if (v >= 10 || v == 0) {                      /* whole semitones */    \
        while (v >= 100) { v -= 100; t++; }                                \
        if (t > 0) { out[len] = (char)('0' + t); len++; }                  \
        t = 0;                                                             \
        while (v >= 10) { v -= 10; t++; }                                  \
        out[len] = (char)('0' + t); len++;                                 \
    } else {                                      /* tenths */             \
        out[len] = '0'; len++; out[len] = '.'; len++;                      \
        out[len] = (char)('0' + v); len++;                                 \
    }                                                                      \
    out[len] = 0;                                                          \
    return len;                                                            \
}

/* Delay: screen 0..100 -> "12ms" .. "999ms", "1.00s". Same millisecond
 * expression as dly_samples(), so the text is the real delay time.
 * 101..112 -> the note value: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar */
#define DS_DELAY_LABEL(fn)                                                 \
int fn(unsigned int value, char *out)                                      \
{                                                                          \
    int v, ms, h = 0, t = 0, len = 0;                                      \
    if (value > 112u) value = 112u;                                        \
    if (value > 100u) {                                                    \
        v = (int)value - 101;                                              \
        if (v == 11) {                                                     \
            out[0] = '1'; out[1] = 'b'; out[2] = 'a'; out[3] = 'r';        \
            out[4] = 0; return 4;                                          \
        }                                                                  \
        out[0] = '1'; out[1] = '/'; len = 2;                               \
        if (v <= 1)      { out[2] = '3'; out[3] = '2'; len = 4; }          \
        else if (v <= 4) { out[2] = '1'; out[3] = '6'; len = 4; }          \
        else if (v <= 7) { out[2] = '8'; len = 3; }                        \
        else if (v <= 9) { out[2] = '4'; len = 3; }                        \
        else             { out[2] = '2'; len = 3; }                        \
        if (v == 1) { out[2] = '1'; out[3] = '6'; out[4] = 'T'; len = 5; } \
        if (v == 3) { out[2] = '8'; out[3] = 'T'; len = 4; }               \
        if (v == 4) { out[4] = '.'; len = 5; }                             \
        if (v == 6) { out[2] = '4'; out[3] = 'T'; len = 4; }               \
        if (v == 7 || v == 9) { out[3] = '.'; len = 4; }                   \
        out[len] = 0;                                                      \
        return len;                                                        \
    }                                                                      \
    v = (int)value;                                                        \
    ms = (int)(12.0f + 0.0988f * (float)(v * v) + 0.5f);                   \
    if (ms >= 1000) {                                                      \
        out[0] = '1'; out[1] = '.'; out[2] = '0'; out[3] = '0';            \
        out[4] = 's'; out[5] = 0;                                          \
        return 5;                                                          \
    }                                                                      \
    while (ms >= 100) { ms -= 100; h++; }                                  \
    while (ms >= 10)  { ms -= 10;  t++; }                                  \
    if (h > 0) { out[len] = (char)('0' + h); len++; }                      \
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }             \
    out[len] = (char)('0' + ms); len++;                                    \
    out[len] = 'm'; len++;                                                 \
    out[len] = 's'; len++;                                                 \
    out[len] = 0;                                                          \
    return len;                                                            \
}

DS_PITCH_LABEL(ZDL_GetLabel_0)   /* Ptch1 -24..+24 with tenths near 0 */
DS_PITCH_LABEL(ZDL_GetLabel_1)   /* Ptch2 */
DS_DELAY_LABEL(ZDL_GetLabel_2)                 /* Dly1 12ms .. 1.00s, 1/32 .. 1bar */
DS_DELAY_LABEL(ZDL_GetLabel_3)                 /* Dly2 12ms .. 1.00s, 1/32 .. 1bar */
/* Tempo: screen 0..441 -> the BPM "40" .. "240"; the twin copy 241..441 shows
 * the same numbers again (ds_tempo_bpm, as in ds_prepare). */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value > 441u) value = 441u;
    if (value <= 39u) return dt_follow_text(out);
    n = ds_tempo_bpm((int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}
/* Depth: screen 0..80 (law in ds_prepare). Text: "0"; "0.005" .. "0.999" with
 * three decimals below 1 st; "1.00" .. "2.00" with two decimals; then the
 * whole semitones "3" .. "12" (screen 71..80). The value is computed with the
 * same expression as the DSP, so the text is the real swing. */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    int n, a = 0, b = 0, c = 0, len = 0;
    if (value > 80u) value = 80u;
    n = (int)value;
    if (n == 0) { out[0] = '0'; out[1] = 0; return 1; }
    if (n > 70) {                                /* whole semitones */
        int w = n - 68;                          /* 3 .. 12 */
        if (w >= 10) { out[len] = '1'; len++; w -= 10; }
        out[len] = (char)('0' + w); len++;
        out[len] = 0;
        return len;
    } else {
        float st = (float)n * (0.005f + 0.00033673f * (float)n);
        if (n == 48) st = 1.0f;
        if (n == 70) st = 2.0f;
        if (st < 0.9995f) {                      /* 0.005 .. 0.999 */
            int t = (int)(st * 1000.0f + 0.5f);
            while (t >= 100) { t -= 100; a++; }
            while (t >= 10)  { t -= 10;  b++; }
            c = t;
            out[0] = '0'; out[1] = '.';
            out[2] = (char)('0' + a); out[3] = (char)('0' + b); out[4] = (char)('0' + c);
            out[5] = 0;
            return 5;
        } else {                                 /* 1.00 .. 2.00 */
            int h = (int)(st * 100.0f + 0.5f);
            int ip = 0;
            while (h >= 100) { h -= 100; ip++; }
            while (h >= 10)  { h -= 10;  a++; }
            b = h;
            out[0] = (char)('0' + ip); out[1] = '.';
            out[2] = (char)('0' + a); out[3] = (char)('0' + b);
            out[4] = 0;
            return 4;
        }
    }
}

/* Div: screen 0..16 -> length of one LFO cycle: 4bar 3bar 2bar 1.5b 1bar 1/2. 1/2 1/4. 1/4
 * 1/8. 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64 */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    int n = (int)value, len = 0;
    if (n > 16) n = 16;
    if (n == 0) { out[0]='4'; out[1]='b'; out[2]='a'; out[3]='r'; len = 4; }
    else if (n == 1) { out[0]='3'; out[1]='b'; out[2]='a'; out[3]='r'; len = 4; }
    else if (n == 2) { out[0]='2'; out[1]='b'; out[2]='a'; out[3]='r'; len = 4; }
    else if (n == 3) { out[0]='1'; out[1]='.'; out[2]='5'; out[3]='b'; len = 4; }
    else if (n == 4) { out[0]='1'; out[1]='b'; out[2]='a'; out[3]='r'; len = 4; }
    else if (n == 5) { out[0]='1'; out[1]='/'; out[2]='2'; out[3]='.'; len = 4; }
    else if (n == 6) { out[0]='1'; out[1]='/'; out[2]='2'; len = 3; }
    else if (n == 7) { out[0]='1'; out[1]='/'; out[2]='4'; out[3]='.'; len = 4; }
    else if (n == 8) { out[0]='1'; out[1]='/'; out[2]='4'; len = 3; }
    else if (n == 9) { out[0]='1'; out[1]='/'; out[2]='8'; out[3]='.'; len = 4; }
    else if (n == 10) { out[0]='1'; out[1]='/'; out[2]='8'; len = 3; }
    else if (n == 11) { out[0]='1'; out[1]='/'; out[2]='8'; out[3]='T'; len = 4; }
    else if (n == 12) { out[0]='1'; out[1]='/'; out[2]='1'; out[3]='6'; len = 4; }
    else if (n == 13) { out[0]='1'; out[1]='/'; out[2]='1'; out[3]='6'; out[4]='T'; len = 5; }
    else if (n == 14) { out[0]='1'; out[1]='/'; out[2]='3'; out[3]='2'; len = 4; }
    else if (n == 15) { out[0]='1'; out[1]='/'; out[2]='3'; out[3]='2'; out[4]='T'; len = 5; }
    else { out[0]='1'; out[1]='/'; out[2]='6'; out[3]='4'; len = 4; }
    out[len] = 0;
    return len;
}

/* Shape: screen 0..6 -> Tri / Sqr / Rand / Step / Sine / Rise / Fall */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    char c0, c1, c2, c3 = 0;
    int len = 4;
    if (value == 0u)      { c0 = 'T'; c1 = 'r'; c2 = 'i'; len = 3; }
    else if (value == 1u) { c0 = 'S'; c1 = 'q'; c2 = 'r'; len = 3; }
    else if (value == 2u) { c0 = 'R'; c1 = 'a'; c2 = 'n'; c3 = 'd'; }
    else if (value == 3u) { c0 = 'S'; c1 = 't'; c2 = 'e'; c3 = 'p'; }
    else if (value == 4u) { c0 = 'S'; c1 = 'i'; c2 = 'n'; c3 = 'e'; }
    else if (value == 5u) { c0 = 'R'; c1 = 'i'; c2 = 's'; c3 = 'e'; }
    else                  { c0 = 'F'; c1 = 'a'; c2 = 'l'; c3 = 'l'; }
    out[0] = c0; out[1] = c1; out[2] = c2; out[3] = c3; out[len] = 0;
    return len;
}

/* ------------------------------------------------------------------ */
/* Pedal entry point. Follows TapeEcho4's audio function line by line:  */
/* magic shuttle first, on/off check, strict bounds checks on the       */
/* ctx[3] arena, then the effect. Not compiled for host tests.          */
/* ------------------------------------------------------------------ */
#ifndef DUALSHIFT_HOST_TEST

#include "dualshft_params.h"          /* generated by build.py            */

#ifndef DUALSHFT_AUDIO_FUNC
#define DUALSHFT_AUDIO_FUNC Fx_DLY_DualShft
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

DS_CODE_SECTION(DUALSHFT_AUDIO_FUNC)
void DUALSHFT_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *dryBuf = ZDL_PTR(float *, ctx[4]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    DualShift *s;
    DualShiftParams P;
    float k[9];
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
    if ((end - base) < sizeof(DualShift) || span < (end - base)) return;
    if (stateBase + sizeof(DualShift) > end) return;

    s = (DualShift *)stateBase;

    k[0] = ds_knob(params[DUALSHFT_PTCH1_SLOT], (float)DUALSHFT_PTCH1_UI_DEFAULT, 0.0151515152f);
    k[1] = ds_knob(params[DUALSHFT_PTCH2_SLOT], (float)DUALSHFT_PTCH2_UI_DEFAULT, 0.0151515152f);
    k[2] = ds_knob(params[DUALSHFT_DLY1_SLOT],   (float)DUALSHFT_DLY1_UI_DEFAULT,   0.008928571f);
    k[3] = ds_knob(params[DUALSHFT_DLY2_SLOT],   (float)DUALSHFT_DLY2_UI_DEFAULT,   0.008928571f);
    ds_ensure_init(s);
    /* bar tag: bars from earlier slots flip the Tempo copy too, FOLLOW takes their BPM */
    k[4] = dt_tempo(&s->sync, dryBuf ? dryBuf + 8 : 0,
                    ds_tempo_ui(params[DUALSHFT_TEMPO_SLOT], (float)DUALSHFT_TEMPO_UI_DEFAULT),
                    dt_id(stateBase)) * 0.0022675737f;   /* 1/441 */
    k[5] = ds_knob(params[DUALSHFT_DIV_SLOT],    (float)DUALSHFT_DIV_UI_DEFAULT,    0.0625f);
    k[6] = ds_knob(params[DUALSHFT_DEPTH_SLOT],  (float)DUALSHFT_DEPTH_UI_DEFAULT,  0.0125f);
    k[7] = ds_knob(params[DUALSHFT_SHAPE_SLOT],  (float)DUALSHFT_SHAPE_UI_DEFAULT,  0.1666667f);
    k[8] = ds_knob(params[DUALSHFT_MIX_SLOT],    (float)DUALSHFT_MIX_UI_DEFAULT,    0.01f);

    ds_prepare(s, &P, k);
    if (params[0] < 0.5f) {                      /* effect switched off: input untouched, */
        if (P.retrig) s->lfo_phase = 0.0f;       /* but the LFO keeps time with the bar   */
        s->lfo_phase += P.lfo_inc * 8.0f;
        if (s->lfo_phase >= 1.0f) s->lfo_phase -= 1.0f;
        return;
    }
    ds_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* DUALSHIFT_HOST_TEST */
