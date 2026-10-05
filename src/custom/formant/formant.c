/*
 * Choral - vowel filter / choir (Zoom MultiStomp MS-70CDR, custom ZDL, mono).
 * (Source folder and audio function keep the working name "Formant".)
 *
 * SIGNAL FLOW
 *                 +--> BPF F1 x2 (cascade) --+
 *   in --+--------+--> BPF F2 x2 (cascade) --+--> weights * makeup --> soft clip --+
 *        |        +--> BPF F3 x2 (cascade) --+                                    |
 *        +------------------------------ dry ---------------------------------- mix --> out
 *
 * Three parallel, highly resonant band-pass filters (RBJ cookbook biquad,
 * constant 0 dB peak gain), each one run twice in series (4th order: much
 * steeper skirts, a far stronger vowel colour), tuned to the first three
 * formants of a vowel.
 *
 * VOWEL TABLE (centre frequencies in Hz, adult male; Latin vowels as in
 * Italian / Spanish: A "ah", E "eh", I "ee", O "oh", U "oo")
 *                 A      E      I      O      U
 *     F1        800    450    280    430    290
 *     F2       1250   2000   2400    800    750
 *     F3       2800   2750   3100   2600   2300
 *   Band levels (relative, F1 = 1):
 *     F2        0.6    0.8   0.75    0.4    0.25
 *     F3        0.3    0.3    0.3    0.2    0.1
 *   The filters' own gain is also scaled by F/500 Hz: a guitar spectrum falls
 *   with frequency, so without that tilt the high formants of E and especially
 *   I (the "ee" lives in F2 = 2.4 kHz) came out far too weak.
 *
 * VOWEL MORPH
 *   v  = Vowel (0..4) + Depth * LFO            (LFO = sine, -1..+1), clamped 0..4
 *   i  = floor(v)   (0..3),  t = v - i         (0..1)
 *   Fn = Tab[n][i] + t * (Tab[n][i+1] - Tab[n][i])       n = 1..3 (same for levels)
 *   v is smoothed with a one-pole every 8-sample block (no zipper noise).
 *
 * BIQUAD (per filter, recomputed once per 8-sample block)
 *   w0    = 2*pi*F / fs            (F <= ~3.1 kHz, so w0 < 0.45 rad)
 *   alpha = sin(w0) / (2Q) = sin(w0) * invQ / 2
 *   b0 =  alpha      b1 = 0     b2 = -alpha
 *   a0 =  1 + alpha  a1 = -2cos(w0)   a2 = 1 - alpha          (all / a0)
 *   Transposed direct form II:
 *     y  = b0*x + z1
 *     z1 = b1*x - a1*y + z2
 *     z2 = b2*x - a2*y
 *   sin/cos are short Taylor series (w0 is small), 1/a0 is three Newton steps:
 *   the ZDL build has no libm and no divide, see the safe-DSP rules.
 *
 * TEMPO SYNC
 *   A custom ZDL cannot read the pedal's global BPM or tap button, so, like
 *   DualShft, the BPM comes from the Tempo knob (screen number = BPM, 40..240)
 *   and Div picks the note value of one LFO cycle. The LFO restarts whenever
 *   Tempo moves, so you can line it up with the beat.
 *   SYNC RESET: the knob runs 0..441 and holds every BPM twice: 0..240 is the
 *   BPM, 241..441 a twin copy (BPM = screen - 201, so past 240 the screen
 *   shows 40 again). Flipping between a BPM and its twin (120 <-> 321)
 *   restarts the LFO without changing the tempo, so a host can send one knob
 *   edit on each downbeat to keep the sweep on the bar.
 *
 * FOOTSWITCH: off = untouched input, but the LFO keeps running and follows
 *   Tempo flips, so the sweep comes back on the bar.
 *
 * CONTROLS (screen values), in pedal order: page 1 = cover, LFO and Mix last
 *   0 Vowel  0.0..4.0 in tenths: A E I O U with morphs in between. The screen
 *            shows five letters blending one vowel into the next
 *            (AAAAA AAAAE AAAEE AAEEE AEEEE EEEEE ...)
 *   1 Reso   Q from ~2 (soft) to ~33 (very vocal). The level is lifted towards the
 *            top of the knob (up to x3.4 at 100) so it stays as loud as it gets narrower
 *   2 Chord  OFF, then 0.01 .. 1.00 = a close detune in semitones (42 steps), then the 35 chords
 *            of a classic chord-machine set (minor, Major, sus2, ... 5ths;
 *            see ZDL_GetLabel_2 for the names and chord_t1..3 for the notes). FIVE
 *            voices: the same offsets move the pitch AND (partly) the formants of the
 *            four side voices. No in-between settings.
 *   3 Param  a parameter whose meaning follows the shape (0..100):
 *              Sine   Lag: the side voices run the sweep ahead of / behind the main
 *                     voice (up to 0.15 cycle for the outer pair) and sit on slightly
 *                     different vowels (+-0.3)
 *              Step   Glide: 0 = hard steps .. 100 = smooth glide between vowels
 *              Rand   Independence: 0 = all voices change on the same beat, 100 = each
 *                     voice on its own clock (new speed of 0.6x..1.6x every cycle)
 *              Solo   Soloists: 1 .. 4 voices move together each cycle, in turn
 *              Some   Chance: 10% .. 90% that a voice changes each cycle
 *              Canon  Lag: the singers' entries are smeared in time
 *              Ripl   Ripple speed: the delay from one voice to the next
 *              Fan    Glide: how fast the voices open out and gather
 *              Walk   Step size: from small blends to whole-vowel leaps
 *              Swell  Attack: how much of the cycle the rise takes
 *              Spot   Width of the spotlight
 *   4 Div    length of one LFO cycle: 4bar 3bar 2bar 1.5b 1bar 1/2. 1/2 1/4. 1/4 1/8.
 *            1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64 (bar = 4 beats = 16 steps, so 4bar =
 *            64 steps, 3bar = 48, 2bar = 32, 1.5b = 24, 1bar = 16, 1/2. = 12 ...)
 *   5 Shape  what the LFO does (Depth and Param apply to all):
 *              Sine   smooth sweep each side of Vowel
 *              Step   hold a whole vowel, glide to the next: A E I O U O I E A ...
 *              Rand   every voice draws its own random vowels (own clocks via Param)
 *              Solo   a few voices change per cycle, in turn; the others hold
 *              Some   each voice changes on its own chance, the others hold
 *              Canon  all voices sing the same random melody, one cycle apart
 *              Ripl   one new vowel ripples through the voices, one after another
 *              Fan    the voices open out into a vowel chord and gather again
 *              Walk   every voice steps a vowel up or down, on its own
 *              Swell  slow eased rise, quick fall
 *              Spot   a spotlight of vowel travels through the voices
 *   6 Depth  0..100 = how far the shape reaches from Vowel, 0..4 vowels (0 = still)
 *   7 Tempo  BPM for the LFO, 40..240; screen 0..441, the second half (241..441) is the
 *            twin copy of the same BPMs (see TEMPO SYNC); shown as the BPM on both; the 8th knob, as on every
 *            twin-Tempo effect of the pack
 *   8 Mix    dry / wet, DJ-style: dry full up to 50, wet full from 50
 *
 * SINGER'S FORMANT: the main (middle) voice has a 4th, fixed band at 3 kHz (x section scale),
 * level 0.06 (no vowel trim), Q half of the others: the bright "ring" of a
 * trained choir. The side voices do not have it.
 *
 * MAIN VOICE, human touch: it is not a copy of the input. It reads the input through a short
 * delay that swings +-17 cents at 5.2 Hz and wanders at random (a live vibrato), breathes
 * a little in loudness with it, has narrower vowel peaks (1/Q x 0.62) and the same breath
 * as the side voices. All of it is always on, in Chord OFF too.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define SR_DO_PRAGMA(x) _Pragma(#x)
#define SR_EXPAND_PRAGMA(x) SR_DO_PRAGMA(x)
#define SR_ALWAYS_INLINE(fn) SR_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SR_CODE_SECTION(fn) SR_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
/* Keep every loop as one copy of its body. The compiler otherwise unrolls the 5-voice and
 * 3-formant loops, which made the object about twice as large as the pedal will take. */
#define SR_NOUNROLL _Pragma("UNROLL(1)")
#else
#define SR_ALWAYS_INLINE(fn)
#define SR_CODE_SECTION(fn)
#define SR_NOUNROLL
#endif

#define FM_MAGIC        0x464D3231u          /* "FM21"                        */
#define FM_W0_PER_HZ    1.4247585e-4f        /* 2*pi / 44100                  */
#define FM_BPM_BLOCK    3.0234e-6f           /* 8 / (60 * 44100): LFO phase per block per (BPM*mult) */
#define FM_VSLEW        0.22f                /* vowel smoothing per block     */
#define FM_BPM_MIN      40
#define FM_BPM_MAX      240
#define FM_TEMPO_MAX_F  441.0f               /* Tempo screen 0..441: the BPMs twice */
#define FM_TEMPO_TWIN   201                  /* twin copy = BPM + 201           */
#define FM_MAKEUP_A     2.6f                 /* wet gain = A + B*Reso, tuned by measurement */
#define FM_MAKEUP_B     3.6f
#define FM_RESO_LIFT    2.4f                 /* extra gain at Reso 100: x3.4              */
#define FM_CHOIR_NORM_A 0.45f                /* level compensation of the five voices, tuned by measurement */
#define FM_CHOIR_NORM_B 0.0f

#define FM_RING         2048                 /* shifter ring, samples (power of 2) */
#define FM_RMASK        2047
#define FM_SHW          1024.0f              /* shifter window: 23 ms             */
#define FM_SHINV        9.765625e-4f         /* 1 / 1024                          */
#define FM_FORMANT_FOLLOW 0.4f               /* formants follow 40% of a voice's pitch shift: a soprano is not a chipmunk */
#define FM_MAIN_VOICE   0.45f                /* the unshifted main voice sounds like the dry input: keep it well under the four others */
#define FM_ENV          0.0003f              /* loudness meters (~70 ms)            */
#define FM_STEP         0.0004f              /* auto-level step per sample (a fixed fraction) */
#define FM_MAIN_DLY     48.0f                /* main voice: base delay of its vibrato tap (1.1 ms)          */
#define FM_MAIN_VIB     13.0f                /* vibrato swing in samples: about +-17 cents at 5.2 Hz        */
#define FM_MAIN_JIT     36.0f                /* slow random wander of that delay, samples per unit of drift */
#define FM_MAIN_Q       0.62f                /* main voice 1/Q scale: narrower, stronger vowel peaks        */
#define FM_BREATH       0.05f                /* aspiration noise into the formants, follows the input level */
#define FM_OUT_LP       0.7f                 /* one-pole warmth filter on the wet signal (about 8.5 kHz) */

typedef struct {
    unsigned int magic;
    int   wr;              /* write index of the shifter ring                 */
    float sph[5];          /* pitch shifter phase per voice, 0..1             */
    float ring[FM_RING];   /* input history for the detuned voices            */
    float vph[5];          /* per-voice vibrato phase                         */
    float drift[5];        /* per-voice slow random pitch / formant drift     */
    unsigned int nz[5];    /* per-voice breath-noise generators               */
    float env;             /* input level follower (breath gate)              */
    float lp;              /* wet output warmth filter state                  */
    float dmain;           /* main voice vibrato-tap delay now (samples)      */
    float ein, eout, comp; /* loudness meters (input / output) and the auto-level gain */
    float lfo_ph;          /* LFO phase, 0..1                                 */
    float last_tempo;      /* Tempo screen number at the last LFO restart     */
    unsigned int rng;      /* random vowel generator (LCG)                    */
    float rcur[5], rnext[5], rph[5], rmul[5]; /* per voice: vowel now / next; Rand: own cycle phase and speed */
    float pprev[5];        /* per voice: last cycle phase (cycle-event detector) */
    int   vsel[5];         /* per voice: cycle counter, 0..59                 */
    float hist[5];         /* Canon: the melody of the last five cycles       */
    float shared;          /* Ripple: the vowel travelling through the voices */
    int   last_shape;      /* shape in use, to restart the voices on a change */
    float isc[4];          /* smoothed semitone offsets of the four side voices (low outer, low inner, high inner, high outer) */
    float vsm[5];          /* smoothed vowel position per voice, <0 = not set  */
    float z1[35], z2[35];  /* biquad states: 5 voices x (3 formants x 2 stages + singer band) */
} FmState;

typedef struct {
    float vowel;           /* target vowel position before LFO, 0..4          */
    float lfo_inc;         /* LFO phase increment per 8-sample block          */
    float depth;           /* LFO swing in vowel units                        */
    float invq;            /* 1/Q                                             */
    float dryG, wetG;      /* Mix gains, 0..1, both 1 at Mix 50               */
    float makeup;          /* wet gain compensation                           */
    float bpm;
    float tempo;           /* Tempo screen number 0..441 (twin copy above 240) */
    int   shape;           /* LFO shape 0..13                                 */
    float hold;            /* steps: fraction of each step that is held       */
    float invg;            /* steps: 1 / glide fraction                       */
    float sec;             /* chord on = 1                                    */
    int   side_on;         /* side voices running (a chord is selected)   */
    int   shift_on;        /* detuned voices active                           */
    float sinc[5];         /* per-voice shifter phase step per sample         */
    float stag;            /* Param 0..1 (its meaning depends on the shape)   */
    int   nsolo;           /* Solo: voices that move per cycle                */
    float prob;            /* Some: chance that a voice changes               */
    float wstep;           /* Walk: step size in vowels                       */
    float ripl;            /* Ripl: stagger per voice in cycles               */
    float swr, swri, swfi; /* Swell: rise share, 1/rise, 1/fall               */
    float spw, spinv;      /* Spot: width and 1/width                         */
    float vibamt;          /* vibrato / drift amount, 1 when a chord or detune is on */
    float vg[5];           /* voice gains (normalised)                        */
    float sscale[5];       /* per-voice formant scale                         */
} FmParams;

SR_ALWAYS_INLINE(clamp01)
static inline float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

/* The pedal hands over every knob as (screen number)/100 whatever its max.
 * Convert back to the screen integer, scale by 1/max. Garbage -> default. */
SR_ALWAYS_INLINE(sr_knob)
static inline float sr_knob(float raw, float def_ui, float inv_max)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.05f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    return clamp01(ui * inv_max);
}

/* Tempo knob (screen 0..441): the pedal passes up to 4.41, so read raw x 100 up to
 * 4.415 (sr_knob's 3.05 guess would take 4.41 for an on-screen 4). Returns the
 * screen number. */
SR_ALWAYS_INLINE(fm_tempo_ui)
static inline float fm_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > FM_TEMPO_MAX_F) ui = FM_TEMPO_MAX_F;
    return ui;
}

/* Tempo screen number -> BPM: 0..240 as is (at least 40), 241..441 the twin copy
 * (screen - 201), so both copies give 40..240. */
SR_ALWAYS_INLINE(fm_tempo_bpm)
static inline int fm_tempo_bpm(int ui)
{
    if (ui > FM_BPM_MAX) ui -= FM_TEMPO_TWIN;
    if (ui < FM_BPM_MIN) ui = FM_BPM_MIN;
    return ui;
}

/* 2^(semis/12) for +-24 st: 5th-order Taylor of half the interval, squared
 * (error well under 1 cent). No libm, no divide. */
SR_ALWAYS_INLINE(semis_to_ratio)
static inline float semis_to_ratio(float semis)
{
    float e  = semis * 0.02888114f;
    float e2 = e * e, r;
    r = 1.0f + e + e2 * (0.5f + e * (0.16666667f
                 + e * (0.041666668f + e * 0.008333334f)));
    return r * r;
}

/* Triangle starting at 0 and rising: ph in [0,1) -> [-1,+1] */
SR_ALWAYS_INLINE(tri_bi)
static inline float tri_bi(float ph)
{
    float q = ph + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
}

/* sin(2*pi*ph) via a triangle and an odd polynomial, error < 1e-4 */
SR_ALWAYS_INLINE(sine_of)
static inline float sine_of(float ph)
{
    float q = ph + 0.25f;
    float t, t2;
    if (q >= 1.0f) q -= 1.0f;
    t  = (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
    t2 = t * t;
    return t * (1.5706268f + t2 * (-0.6432292f + t2 * 0.0727102f));
}

/* ---- vowel table: formant n (0..2) of vowel v (0..4), in Hz ------------- */
/* Compiled as selects on literals: no array, nothing in a data section.    */
SR_ALWAYS_INLINE(vowel_hz)
static inline float vowel_hz(int n, int v)
{
    float r;
    if (n == 0)
        r = (v == 0) ? 800.0f  : (v == 1) ? 450.0f  : (v == 2) ? 280.0f  : (v == 3) ? 430.0f : 290.0f;
    else if (n == 1)
        r = (v == 0) ? 1250.0f : (v == 1) ? 2000.0f : (v == 2) ? 2400.0f : (v == 3) ? 800.0f : 750.0f;
    else
        r = (v == 0) ? 2800.0f : (v == 1) ? 2750.0f : (v == 2) ? 3100.0f : (v == 3) ? 2600.0f : 2300.0f;
    return r;
}

/* relative level of formant n (0..2) for vowel v */
SR_ALWAYS_INLINE(vowel_amp)
static inline float vowel_amp(int n, int v)
{
    float r;
    if (n == 0)      r = 1.0f;
    else if (n == 1) r = (v == 0) ? 0.6f : (v == 1) ? 0.8f : (v == 2) ? 0.75f : (v == 3) ? 0.4f : 0.25f;
    else             r = (v == 0) ? 0.3f : (v == 1) ? 0.3f : (v == 2) ? 0.3f : (v == 3) ? 0.2f : 0.1f;
    return r;
}

/* overall loudness trim per vowel, so A E I O U sound equally loud */
SR_ALWAYS_INLINE(vowel_trim)
static inline float vowel_trim(int v)
{
    return (v == 0) ? 0.7f : (v == 1) ? 0.6f : (v == 2) ? 0.55f : (v == 3) ? 1.3f : 2.3f;
}

/* ---- band-pass biquad coefficients, constant 0 dB peak gain ------------- */
typedef struct { float b0, a1, a2; } FmBq;   /* b1 = 0, b2 = -b0 */

SR_ALWAYS_INLINE(bp_coefs)
static inline FmBq bp_coefs(float hz, float invq)
{
    FmBq c;
    float w0 = hz * FM_W0_PER_HZ;
    float w2, sn, cs, alpha, a0, y;
    if (w0 > 1.55f) w0 = 1.55f;
    w2 = w0 * w0;
    sn = w0 * (1.0f - w2 * (0.16666667f - w2 * (0.008333334f - w2 * 0.00019841270f)));
    cs = 1.0f - w2 * (0.5f - w2 * (0.041666668f - w2 * 0.0013888889f));
    alpha = sn * 0.5f * invq;
    a0 = 1.0f + alpha;                        /* 1.0 .. ~1.2               */
    y  = 1.44f - 0.5f * a0;                   /* 1/a0 by Newton: y*(2-a0*y) */
    y  = y * (2.0f - a0 * y);
    y  = y * (2.0f - a0 * y);
    y  = y * (2.0f - a0 * y);
    c.b0 = alpha * y;
    c.a1 = -2.0f * cs * y;
    c.a2 = (1.0f - alpha) * y;
    return c;
}

SR_ALWAYS_INLINE(soft_clip)
static inline float soft_clip(float x)
{
    if (x > 1.5f) x = 1.5f;
    else if (x < -1.5f) x = -1.5f;
    return x - 0.14814815f * x * x * x;
}

SR_ALWAYS_INLINE(fm_init)
static inline void fm_init(FmState *s)
{
    int i;
    s->lfo_ph = 0.0f; s->last_tempo = -1.0f; s->wr = 0; s->env = 0.0f; s->lp = 0.0f; s->dmain = FM_MAIN_DLY;
    s->ein = 0.0f; s->eout = 0.0f; s->comp = 0.3f;
    SR_NOUNROLL
    for (i = 0; i < 5; i++) { s->vph[i] = 0.21f * (float)i; s->drift[i] = 0.0f; s->nz[i] = 0x9E3779B9u * (unsigned int)(i + 1); }
    s->isc[0] = -1000.0f;
    s->rng = 0x1234567u; s->shared = 0.0f; s->last_shape = -1;
    SR_NOUNROLL
    for (i = 0; i < 5; i++) { s->pprev[i] = 0.0f; s->vsel[i] = 0; s->hist[i] = 0.0f; }
    SR_NOUNROLL
    for (i = 0; i < 5; i++) { s->rcur[i] = -1.0f; s->rnext[i] = 0.0f; s->rph[i] = 0.0f; s->rmul[i] = 1.0f; }
    SR_NOUNROLL
    for (i = 0; i < 5; i++) { s->vsm[i] = -1.0f; s->sph[i] = 0.1f * (float)i; }
    SR_NOUNROLL
    for (i = 0; i < FM_RING; i++) s->ring[i] = 0.0f;
    SR_NOUNROLL
    for (i = 0; i < 35; i++) { s->z1[i] = 0.0f; s->z2[i] = 0.0f; }
    s->magic = FM_MAGIC;
}

/* Div index 0..16 -> LFO cycles per beat (4/4: one bar = 4 beats, 16 steps = 1 bar):
 *   0 4 bars (64 steps) 0.0625    6 1/2   (8 steps)   0.5      12 1/16  4
 *   1 3 bars (48 steps) 0.08333   7 1/4.  (6 steps)   0.6667   13 1/16T 6
 *   2 2 bars (32 steps) 0.125     8 1/4   (4 steps)   1        14 1/32  8
 *   3 1.5 bars (24)     0.16667   9 1/8.              1.3333   15 1/32T 12
 *   4 1 bar  (16 steps) 0.25     10 1/8               2        16 1/64  16
 *   5 1/2.  (12 steps)  0.3333   11 1/8T              3                      */
SR_ALWAYS_INLINE(subdiv_mult)
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

/* 1/x for x in 0.15..0.7 (Newton, no divide) */
SR_ALWAYS_INLINE(recip_g)
static inline float recip_g(float g)
{
    float y = 2.5f;
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    return y;
}

/* 1/x for x in 0.15..0.85 (Newton from 1.5, no divide) */
SR_ALWAYS_INLINE(recip_w)
static inline float recip_w(float g)
{
    float y = 1.5f;
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    y = y * (2.0f - g * y);
    return y;
}

/* linear-interpolated read, d samples behind the newest sample (2 <= d < 2040) */
SR_ALWAYS_INLINE(ring_tap)
static inline float ring_tap(const float *ring, int wr, float d)
{
    int   di = (int)d;
    float fr = d - (float)di;
    float a = ring[(wr - di) & FM_RMASK];
    float b = ring[(wr - di - 1) & FM_RMASK];
    return a + fr * (b - a);
}

/* Chords of a classic chord-machine set, by their names (Chord knob 49..83 =
 * chord 1..35). Each is the root plus three tones, intervals in semitones above the
 * root (tones above 12 are the 9th / 11th...). The four side voices take them as
 * low outer = t1 - 12, low inner = t2 - 12, high inner = t1, high outer = t3, so every
 * tone is present and the chord is spread over both sides of the main voice. */
SR_ALWAYS_INLINE(chord_word)
static inline unsigned int chord_word(int c)
{
    /* one packed word per chord: bits 0..2 t1, 3..6 t2, 7..11 t3, 12..19 level trim
     * (trim = 0.4 + 0.001 * value, measured on noise and saws so every chord is about
     * as loud as the single voice) */
    return (c == 1) ? 0x8F63Bu
         : (c == 2) ? 0x8A63Cu
         : (c == 3) ? 0x9163Au
         : (c == 4) ? 0x7B63Du
         : (c == 5) ? 0x9253Bu
         : (c == 6) ? 0x8B53Cu
         : (c == 7) ? 0x8B5BBu
         : (c == 8) ? 0x855BCu
         : (c == 9) ? 0x7D53Du
         : (c == 10) ? 0xAB4B3u
         : (c == 11) ? 0x8A73Bu
         : (c == 12) ? 0x8473Cu
         : (c == 13) ? 0xA14BBu
         : (c == 14) ? 0x9A4BCu
         : (c == 15) ? 0x97633u
         : (c == 16) ? 0x91634u
         : (c == 17) ? 0x9B533u
         : (c == 18) ? 0x93534u
         : (c == 19) ? 0x7F644u
         : (c == 20) ? 0x86543u
         : (c == 21) ? 0x81544u
         : (c == 22) ? 0x82643u
         : (c == 23) ? 0x7F753u
         : (c == 24) ? 0x79754u
         : (c == 25) ? 0x8A734u
         : (c == 26) ? 0x8C5B4u
         : (c == 27) ? 0x836D4u
         : (c == 28) ? 0x706C5u
         : (c == 29) ? 0x8E43Du
         : (c == 30) ? 0x9B3B4u
         : (c == 31) ? 0xA84ACu
         : (c == 32) ? 0x775CCu
         : (c == 33) ? 0x8275Cu
         : (c == 34) ? 0x517D5u
         : 0x5FAF7u;
}

/* Level trim for the side voices, measured on noise and saws so every setting is
 * about as loud as the single voice. ci = Chord knob 0..83: 0 = off (1.0); 1..48 = detune / semitones;
 * close detune (its hundredths, knots at 1 3 10 25 50 75 100, linear in between);
 * 49..83 = one value per chord. */
/* Detune steps: Chord knob 1..42 -> hundredths of a semitone. 1..20 = 0.01 .. 0.20 in
 * steps of 0.01, 21..30 = 0.22 .. 0.40 in steps of 0.02, 31..42 = 0.45 .. 1.00 in steps
 * of 0.05. Fine where a little detune matters, coarser where it does not. */
SR_ALWAYS_INLINE(dn_h)
static inline int dn_h(int ci)
{
    if (ci <= 20) return ci;
    if (ci <= 30) return 20 + 2 * (ci - 20);
    return 40 + 5 * (ci - 30);
}

SR_ALWAYS_INLINE(fm_trim)
static inline float fm_trim(int ci)
{
    int   seg;
    float x0, inv, y0, y1;
    if (ci <= 0) return 1.0f;
    ci = dn_h(ci);
    seg = (ci < 3) ? 0 : (ci < 10) ? 1 : (ci < 25) ? 2 : (ci < 50) ? 3 : (ci < 75) ? 4 : 5;
    x0  = (seg == 0) ? 1.0f : (seg == 1) ? 3.0f : (seg == 2) ? 10.0f : (seg == 3) ? 25.0f : (seg == 4) ? 50.0f : 75.0f;
    inv = (seg == 0) ? 0.50000000f : (seg == 1) ? 0.14285714f : (seg == 2) ? 0.06666667f : (seg == 3) ? 0.04000000f : (seg == 4) ? 0.04000000f : 0.04000000f;
    y0  = (seg == 0) ? 0.655f : (seg == 1) ? 0.667f : (seg == 2) ? 0.646f : (seg == 3) ? 0.626f : (seg == 4) ? 0.626f : 0.615f;
    y1  = (seg == 0) ? 0.667f : (seg == 1) ? 0.646f : (seg == 2) ? 0.626f : (seg == 3) ? 0.626f : (seg == 4) ? 0.615f : 0.624f;
    return y0 + ((float)ci - x0) * inv * (y1 - y0);
}

/* Random mode: pick the next whole vowel for one voice, somewhere within
 * +-depth vowels of the Vowel knob (reflected at the ends of A..U). The same vowel may come
 * twice in a row. LCG, no divide. */
SR_ALWAYS_INLINE(rnd01)
static inline float rnd01(FmState *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(s->rng >> 8) * 5.9604645e-8f;               /* 0 .. 1 */
}

SR_ALWAYS_INLINE(rnd_vowel)
static inline float rnd_vowel(FmState *s, float vowel, float depth)
{
    float u, t;
    u = 2.0f * rnd01(s) - 1.0f;                                /* -1 .. +1 */
    if (depth >= 4.0f) {                                       /* full range: any of the five, equally likely */
        t = (float)(int)(rnd01(s) * 5.0f);
    } else {
        t = vowel + depth * u;
        if (t < 0.0f) t = -t;
        if (t > 4.0f) t = 8.0f - t;
        if (t < 0.0f) t = 0.0f;
        if (t > 4.0f) t = 4.0f;
        if (depth >= 1.5f) t = (float)(int)(t + 0.5f);         /* wide: whole vowels; narrow: any blend */
    }
    if (t > 4.0f) t = 4.0f;
    return t;
}

/* k[] = 0..1 by each knob's own max:
 *   Vowel, Tempo, Div, Depth, Reso, Mix, Chord, Param (internal order) */
SR_ALWAYS_INLINE(fm_prepare)
static inline void fm_prepare(FmState *s, FmParams *P, const float *k)
{
    int tempo_i = (int)(k[1] * FM_TEMPO_MAX_F + 0.5f);  /* screen 0..441 */
    int idx   = (int)(k[2] * 16.0f + 0.5f);
    int   nd    = (int)(k[3] * 100.0f + 0.5f);          /* Depth 0..100 = 0..4 vowels */
    float gl;
    int m;
    P->tempo   = (float)tempo_i;
    P->bpm     = (float)fm_tempo_bpm(tempo_i);
    P->vowel   = 4.0f * k[0];
    P->lfo_inc = P->bpm * subdiv_mult(idx) * FM_BPM_BLOCK;
    P->shape   = (int)(k[8] * 10.0f + 0.5f);
    P->depth   = 0.04f * (float)nd;                   /* 0..4 vowels                  */
    /* Param (k[7]) means something different in every shape, see the header.
     * Glide share of each step: Step and Fan take it from Param (hard steps .. smooth);
     * the other stepping shapes have a fixed glide: small = rhythmic, large = smooth */
    gl = (P->shape == 1 || P->shape == 7) ? 0.15f + 0.55f * k[7]
       : (P->shape == 2) ? 0.35f : (P->shape == 3 || P->shape == 6) ? 0.50f
       : (P->shape == 4) ? 0.40f : (P->shape == 5) ? 0.45f : 0.30f;
    P->hold    = 1.0f - gl;
    P->invg    = recip_g(gl);
    P->nsolo   = 1 + (int)(k[7] * 3.99f);
    P->prob    = 0.1f + 0.8f * k[7];
    P->wstep   = 0.2f + 1.8f * k[7];
    P->ripl    = 0.02f + 0.28f * k[7];
    P->swr     = 0.2f + 0.6f * k[7];
    P->swri    = recip_w(P->swr);
    P->swfi    = recip_w(1.0f - P->swr);
    P->spw     = 0.2f + 0.5f * k[7];
    P->spinv   = recip_w(P->spw);
    P->invq    = 0.5f - 0.47f * k[4];                 /* Q 2 .. 33                    */
    P->dryG = 2.0f - 2.0f * (k[5]);            /* Mix: dry full up to 50, then fades out */
    if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * (k[5]);                    /* wet fades in up to 50, then full       */
    if (P->wetG > 1.0f) P->wetG = 1.0f;
    {   /* more Reso = narrower bands = less level: lift the top of the range */
        float k2 = k[4] * k[4], k6 = k2 * k2 * k2;
        P->makeup = (FM_MAKEUP_A + FM_MAKEUP_B * k[4]) * (1.0f + FM_RESO_LIFT * k6);
    }
    {   /* Chord: 0 = off, 1..42 = a close detune of 0.01 .. 1.00 semitone (see dn_h; outer
         * pair -+n, inner pair -+n/2), 44..48 = 3..7 semitones (43 = 2), 49..83 = the 35 chords of the chord
         * machine (see chord_t1..3). The same offsets move the formants AND the pitch
         * of the four side voices. No in-between settings. The offsets glide a little
         * so a change of chord does not click. */
        int   ci = (int)(k[6] * 83.0f + 0.5f);
        float tg[4], st[5], y, nn, r, t1, t2, t3;
        unsigned int cw = 0u;
        if (ci <= 48) {
            nn = (ci <= 42) ? 0.01f * (float)dn_h(ci) : (float)(ci - 41);   /* 43..48 = 2 .. 7 semitones */
            tg[0] = -nn; tg[1] = -0.5f * nn; tg[2] = 0.5f * nn; tg[3] = nn;
        } else {
            int cc = ci - 48;
            cw = chord_word(cc);
            t1 = (float)(cw & 7u); t2 = (float)((cw >> 3) & 15u); t3 = (float)((cw >> 7) & 31u);
            tg[0] = t1 - 12.0f; tg[1] = t2 - 12.0f; tg[2] = t1; tg[3] = t3;
        }
        if (s->isc[0] < -500.0f) { s->isc[0] = tg[0]; s->isc[1] = tg[1]; s->isc[2] = tg[2]; s->isc[3] = tg[3]; }
        SR_NOUNROLL
        for (m = 0; m < 4; m++) s->isc[m] += 0.06f * (tg[m] - s->isc[m]);
        st[0] = s->isc[0]; st[1] = s->isc[1]; st[2] = 0.0f; st[3] = s->isc[2]; st[4] = s->isc[3];
        SR_NOUNROLL
        for (m = 0; m < 5; m++) {
            r = semis_to_ratio(st[m]);
            P->sinc[m]   = (1.0f - r) * FM_SHINV;
            P->sscale[m] = semis_to_ratio(st[m] * FM_FORMANT_FOLLOW);     /* a sharp voice reads its input with a shrinking delay */
        }
        P->sec      = (ci > 0) ? 1.0f : 0.0f;
        P->shift_on = (ci > 0);
        P->side_on  = (ci > 0);
        P->stag     = k[7];
        P->vibamt   = 1.0f;                           /* the main voice always sings with a live vibrato */
        P->vg[0] = 0.7f; P->vg[1] = 0.9f; P->vg[2] = FM_MAIN_VOICE; P->vg[3] = 0.9f; P->vg[4] = 0.7f;
        y = (ci > 48) ? 0.4f + 0.001f * (float)(cw >> 12) : fm_trim((ci > 42) ? 42 : ci);
        SR_NOUNROLL
        for (m = 0; m < 5; m++) P->vg[m] *= y;
    }
    if (P->tempo != s->last_tempo) {                  /* Tempo moved or flipped to its twin */
        s->lfo_ph = 0.0f; s->last_tempo = P->tempo;
        SR_NOUNROLL
        for (m = 0; m < 5; m++) { s->rph[m] = 0.0f; s->rmul[m] = 1.0f; }   /* random mode restarts with the beat too */
    }
}

SR_ALWAYS_INLINE(fm_process)
static inline void fm_process(FmState *s, const FmParams *P, float *buf, int n)
{
    FmBq c[5][4];
    float g[5][4];
    float sincv[5];
    float v = 0.0f, t, f, ph, tr, slew, fsc, vc, dr, gph;
    int   i, j, m, iv, e;
    float trv[2];
    float dmain = FM_MAIN_DLY, dstep = 0.0f, amain = 1.0f;

    /* LFO, once per block */
    s->lfo_ph += P->lfo_inc;
    if (s->lfo_ph >= 1.0f) s->lfo_ph -= 1.0f;
    slew = (P->shape == 0 || P->shape >= 9) ? FM_VSLEW : 0.3f;
    if (s->last_shape != P->shape) {                  /* a new shape starts the voices afresh */
        s->last_shape = P->shape;
        SR_NOUNROLL
        for (m = 0; m < 5; m++) s->rcur[m] = -1.0f;
    }

    SR_NOUNROLL
    for (m = 0; m < 5; m++) sincv[m] = P->sinc[m];
    SR_NOUNROLL
    for (m = 0; m < 5; m++) {
        float off = (m == 0) ? -1.0f : (m == 1) ? -0.5f : (m == 2) ? 0.0f : (m == 3) ? 0.5f : 1.0f;
        if (m != 2 && (!P->side_on || P->vg[m] <= 0.0f)) {   /* side voice off: keep clean */
            SR_NOUNROLL
            for (j = 0; j < 7; j++) { s->z1[m * 7 + j] = 0.0f; s->z2[m * 7 + j] = 0.0f; }
            continue;
        }
        /* Human touch: every voice has its own vibrato (4.9 .. 6.0 Hz, +-14 cents) and a
         * slow random drift (a few cents), on the pitch of the shifted voices and on
         * the formant frequencies (a tenth of that), so the voices never move together */
        s->vph[m] += (m == 0) ? 8.889e-4f : (m == 1) ? 1.0159e-3f : (m == 2) ? 9.433e-4f : (m == 3) ? 1.0885e-3f : 9.796e-4f;
        if (s->vph[m] >= 1.0f) s->vph[m] -= 1.0f;
        s->drift[m] = s->drift[m] * 0.999f + (rnd01(s) - 0.5f) * 0.01f;
        vc  = P->vibamt * (14.0f * sine_of(s->vph[m]) + 60.0f * s->drift[m]);
        fsc = P->sscale[m] * (1.0f + 0.0002888f * vc);
        sincv[m] = P->sinc[m] - vc * 5.776e-4f * FM_SHINV;
        if (m == 2) {
            /* main voice: a vibrato tap, not a copy. The input is read through a delay that
             * swings +-13 samples at 5.2 Hz (a +-17 cent vibrato) and wanders a little at
             * random, so its pitch is never exactly the source's pitch. It also breathes a
             * little in loudness, in step with the vibrato, as a real voice does. */
            dmain = FM_MAIN_DLY + FM_MAIN_VIB * sine_of(s->vph[2]) + FM_MAIN_JIT * s->drift[2];
            dstep = (dmain - s->dmain) * 0.125f;
            amain = 1.0f + 0.07f * sine_of(s->vph[2] + 0.2f);
        }
        /* Param, by shape: Sine, Canon, Saw: Lag = the voices run the cycle ahead of /
         * behind the main voice (+-0.15 cycle at 100 for the outer pair); Pair: the same,
         * doubled, between the two members of a pair; Ripl: stagger per voice. */
        ph = s->lfo_ph + ((P->shape == 0 || P->shape == 5) ? off * 0.15f * P->stag
                        : (P->shape == 6) ? -P->ripl * (float)m : 0.0f);
        while (ph >= 1.0f) ph -= 1.0f;
        while (ph < 0.0f) ph += 1.0f;
        dr = (P->shape >= 2 && P->shape <= 6) ? rnd_vowel(s, P->vowel, P->depth) : 0.0f;   /* one fresh random vowel for this voice, used when it changes */
        gph = -1.0f;
        if (P->shape == 2) {
            /* RAND: Lag sets how independent the voices are. Lag 0: all voices share
             * the Tempo/Div clock and change on the same beat (each to its own vowel).
             * Lag 100: every voice runs on its OWN clock: its own start phase and,
             * every cycle, a new speed of 0.6x .. 1.6x the Tempo/Div rate. A voice
             * holds its vowel for most of the cycle and glides to the next at the end. */
            if (s->rcur[m] < 0.0f) {
                s->rph[m]   = P->stag * rnd01(s);
                s->rmul[m]  = 1.0f;
                s->rcur[m]  = dr;
                s->rnext[m] = P->vowel;
            }
            s->rph[m] += P->lfo_inc * s->rmul[m];
            if (s->rph[m] >= 1.0f) {                  /* this voice starts a new cycle */
                s->rph[m] -= 1.0f;
                if (s->rph[m] >= 1.0f) s->rph[m] = 0.0f;
                s->rmul[m]  = 1.0f + P->stag * (rnd01(s) - 0.4f);
                s->rcur[m]  = s->rnext[m];
                s->rnext[m] = dr;
            }
            gph = s->rph[m];
        } else if (P->shape >= 3 && P->shape <= 8) {
            /* Voices that remember a vowel, hold it, and glide to a new one at the end
             * of the cycle. WHICH voices change, and to what, is the shape:
             *  3 SOLO  a few voices per cycle, in turn: the others hold
             *  4 SOME  each voice on its own chance every cycle
             *  5 CANON all voices sing the same random melody, each one cycle later
             *  6 RIPL  one new vowel ripples through the voices, one after another
             *  7 FAN   the voices open out into a vowel chord and gather again
             *  8 WALK  every voice steps a vowel up or down, on its own */
            float cc = P->vowel, dd = P->depth, nx, lo, hi, stp;
            if (s->rcur[m] < 0.0f) {
                s->rcur[m] = cc; s->rnext[m] = cc; s->pprev[m] = ph; s->vsel[m] = 0; s->hist[m] = cc;
                if (m == 0) s->shared = cc;
            }
            if (ph < s->pprev[m]) {                   /* this voice starts a new cycle */
                int r5, sel;
                s->rcur[m] = s->rnext[m];
                s->vsel[m] += 1;
                if (s->vsel[m] >= 10) s->vsel[m] = 0;
                sel = s->vsel[m];
                r5 = (sel >= 5) ? sel - 5 : sel;
                nx = s->rnext[m];
                if (P->shape == 3) {
                    stp = (float)(m - r5); if (stp < 0.0f) stp += 5.0f;     /* distance from the soloist in turn */
                    if (stp < (float)P->nsolo) nx = dr;
                } else if (P->shape == 4) {
                    if (rnd01(s) < P->prob) nx = dr;
                } else if (P->shape == 5) {
                    if (m == 0) {
                        s->hist[4] = s->hist[3]; s->hist[3] = s->hist[2]; s->hist[2] = s->hist[1]; s->hist[1] = s->hist[0];
                        s->hist[0] = dr;
                    }
                    nx = s->hist[m];
                } else if (P->shape == 6) {
                    if (m == 0) s->shared = dr;
                    nx = s->shared;
                } else if (P->shape == 7) {
                    if (sel & 1) {
                        nx = cc + off * dd;                   /* outer voices reach +-depth, inner +-depth/2 */
                        if (nx < 0.0f) nx = 0.0f;
                        if (nx > 4.0f) nx = 4.0f;
                    } else {
                        nx = cc;
                    }
                } else {
                    lo = cc - dd; if (lo < 0.0f) lo = 0.0f;
                    hi = cc + dd; if (hi > 4.0f) hi = 4.0f;
                    stp = P->wstep;
                    nx = s->rcur[m] + ((rnd01(s) < 0.5f) ? -stp : stp);
                    if (nx < lo) nx = s->rcur[m] + stp;
                    if (nx > hi) nx = s->rcur[m] - stp;
                    if (nx < lo) nx = lo;
                    if (nx > hi) nx = hi;
                }
                s->rnext[m] = nx;
            }
            s->pprev[m] = ph;
            gph = ph;
        } else {
            /* Shapes that are a function of the cycle phase:
             *  0 SINE  smooth sweep each side of Vowel
             *  1 STEP  hold a whole vowel, glide to the next: A E I O U O I E A ...
             *  9 SWELL slow rise, quick fall, eased: a crescendo (Param = how long the rise)
             * 10 SPOT  a spotlight: a bump of vowel travels through the voices */
            float cc = P->vowel, dd = P->depth, q;
            if (P->shape == 0) {
                v = cc + dd * sine_of(ph);
            } else if (P->shape == 1) {
                v = cc + dd * tri_bi(ph);
            } else if (P->shape == 9) {
                q = (ph < P->swr) ? ph * P->swri : (1.0f - ph) * P->swfi;
                q = q * q * (3.0f - 2.0f * q);
                v = cc + dd * (2.0f * q - 1.0f);
            } else {
                q = ph - 0.2f * (float)m;
                if (q < 0.0f) q += 1.0f;
                q = (q < P->spw) ? 0.5f - 0.5f * sine_of(q * P->spinv + 0.25f) : 0.0f;
                v = (cc + dd > 4.0f) ? cc - dd * q : cc + dd * q;
            }
            if (v < 0.0f) v = 0.0f;
            if (v > 4.0f) v = 4.0f;
            if (P->shape == 1) {                          /* vowel steps: hold a whole vowel, then glide on */
                int   nv = (int)v;
                float fr = v - (float)nv, u;
                u = (fr - P->hold) * P->invg;             /* the last part of each step = the glide */
                if (u < 0.0f) u = 0.0f;
                if (u > 1.0f) u = 1.0f;
                v = (float)nv + u * u * (3.0f - 2.0f * u);/* smoothstep */
            }
            if (P->shape == 0) v += off * 0.3f * P->stag;   /* Param: the voices also sit on slightly different vowels */
        }
        if (gph >= 0.0f) {                            /* a remembered vowel: hold it, glide on at the end of the cycle */
            float u = (gph - P->hold) * P->invg;
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            v = s->rcur[m] + (s->rnext[m] - s->rcur[m]) * (u * u * (3.0f - 2.0f * u));
        }
        if (v < 0.0f) v = 0.0f;
        if (v > 4.0f) v = 4.0f;
        if (s->vsm[m] < 0.0f) s->vsm[m] = v;
        s->vsm[m] += slew * (v - s->vsm[m]);
        v = s->vsm[m];
        iv = (int)v;
        if (iv > 3) iv = 3;
        t = v - (float)iv;
        SR_NOUNROLL
        for (e = 0; e < 2; e++) trv[e] = vowel_trim(iv + e);
        tr = trv[0] + t * (trv[1] - trv[0]);

        /* three vowel formants, plus (main voice only) the singer's band at 3 kHz */
        SR_NOUNROLL
        for (j = 0; j < ((m == 2) ? 4 : 3); j++) {
            float qi, gg;
            if (j < 3) {
                float hz[2], am[2];
                SR_NOUNROLL
                for (e = 0; e < 2; e++) { hz[e] = vowel_hz(j, iv + e); am[e] = vowel_amp(j, iv + e); }
                f  = (hz[0] + t * (hz[1] - hz[0])) * fsc;   /* cutoff interpolation */
                qi = (m == 2) ? P->invq * FM_MAIN_Q : P->invq;
                gg = P->vg[m] * P->makeup * tr * (am[0] + t * (am[1] - am[0]));
                if (m == 2) gg *= amain;
            } else {                                    /* singer's formant: fixed band near 3 kHz */
                f  = 3000.0f * fsc;
                qi = P->invq + P->invq;
                gg = P->vg[m] * P->makeup * 0.06f;
            }
            c[m][j] = bp_coefs(f, qi);
            g[m][j] = gg * (f * 0.002f);
        }
    }
    SR_NOUNROLL
    for (j = 0; j < 35; j++) {                             /* denormals */
        if (s->z1[j] < 1e-15f && s->z1[j] > -1e-15f) s->z1[j] = 0.0f;
        if (s->z2[j] < 1e-15f && s->z2[j] > -1e-15f) s->z2[j] = 0.0f;
    }

    SR_NOUNROLL
    for (i = 0; i < n; i++) {
        float x0 = buf[i], wet = 0.0f, x, y, xin, vo;
        s->wr = (s->wr + 1) & FM_RMASK;
        s->ring[s->wr] = x0;
        s->env += 0.003f * (((x0 < 0.0f) ? -x0 : x0) - s->env);
        SR_NOUNROLL
        for (m = 0; m < 5; m++) {
            float *z1 = &s->z1[m * 7], *z2 = &s->z2[m * 7];
            if (m != 2 && (!P->side_on || P->vg[m] <= 0.0f)) continue;
            xin = x0;
            if (m == 2) {                                     /* vibrato tap (see above) */
                s->dmain += dstep;
                xin = ring_tap(s->ring, s->wr, s->dmain);
            } else if (P->shift_on) {                      /* detuned voice: two crossfaded taps */
                float p = s->sph[m] + sincv[m], wa, xa, xb, pb, dly;
                if (p >= 1.0f) p -= 1.0f;
                if (p < 0.0f) p += 1.0f;
                s->sph[m] = p;
                pb = p + 0.5f;
                if (pb >= 1.0f) pb -= 1.0f;
                wa = sine_of(p * 0.5f);
                wa = wa * wa;                                 /* sin^2(pi p); tap b gets 1 - wa */
                dly = (m == 0) ? 410.0f : (m == 1) ? 230.0f : (m == 3) ? 330.0f : 570.0f;   /* each singer a few ms apart */
                xa = ring_tap(s->ring, s->wr, p * FM_SHW + 2.0f + dly);
                xb = ring_tap(s->ring, s->wr, pb * FM_SHW + 2.0f + dly);
                xin = wa * xa + (1.0f - wa) * xb;
            }
            s->nz[m] = s->nz[m] * 1664525u + 1013904223u;       /* breath: noise that follows the input level */
            xin += FM_BREATH * s->env * ((float)(s->nz[m] >> 8) * 1.1920929e-7f - 1.0f);
            vo = 0.0f;
            SR_NOUNROLL
            for (j = 0; j < 3; j++) {
                x = xin;
                y = c[m][j].b0 * x + z1[j];                   /* stage 1 */
                z1[j] = -c[m][j].a1 * y + z2[j];
                z2[j] = -c[m][j].b0 * x - c[m][j].a2 * y;
                x = y;
                y = c[m][j].b0 * x + z1[j + 3];               /* stage 2 */
                z1[j + 3] = -c[m][j].a1 * y + z2[j + 3];
                z2[j + 3] = -c[m][j].b0 * x - c[m][j].a2 * y;
                vo += g[m][j] * y;
            }
            if (m == 2) {
                y = c[m][3].b0 * xin + z1[6];                 /* singer band, main voice only */
                z1[6] = -c[m][3].a1 * y + z2[6];
                z2[6] = -c[m][3].b0 * xin - c[m][3].a2 * y;
                vo += g[m][3] * y;
            }
            wet += vo;
        }
        wet = soft_clip(wet);
        s->lp += FM_OUT_LP * (wet - s->lp);
        wet = s->lp;
        /* auto-level: the wet signal keeps the loudness of the input, whatever the vowel,
         * Reso, Chord or source (no sqrt, no divide: it only compares and steps) */
        wet *= s->comp;
        if (wet > 1.5f) wet = 1.0f;                       /* soft ceiling at +-1 (cubic) */
        else if (wet < -1.5f) wet = -1.0f;
        else wet = wet - 0.148148f * wet * wet * wet;
        s->ein  += FM_ENV * (x0 * x0 - s->ein);
        s->eout += FM_ENV * (wet * wet - s->eout);
        if (s->ein > 1e-9f) {                             /* hold the gain in silence */
            if (s->eout > s->ein) s->comp -= FM_STEP * s->comp;
            else                  s->comp += FM_STEP * s->comp;
            if (s->comp < 0.02f) s->comp = 0.02f;
            if (s->comp > 8.0f)  s->comp = 8.0f;
        }
        buf[i] = P->dryG * x0 + P->wetG * wet;
    }
}

/* ---- on-screen text (knob index = ZDL_GetLabel_<index>) ------------------ */
/* 0 Vowel: five letters that blend from one vowel into the next:
 * AAAAA, AAAAE, AAAEE, AAEEE, AEEEE, EEEEE ... (one letter per 2 tenths) */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    int n = (int)value, w = 0, y = 0, i;
    char a, b;
    if (n > 40) n = 40;
    while (n >= 10) { n -= 10; w++; }
    while (n >= 2)  { n -= 2;  y++; }                   /* 0..4 letters of the next vowel */
    a = (w == 0) ? 'A' : (w == 1) ? 'E' : (w == 2) ? 'I' : (w == 3) ? 'O' : 'U';
    b = (w == 0) ? 'E' : (w == 1) ? 'I' : (w == 2) ? 'O' : 'U';
    SR_NOUNROLL
    for (i = 0; i < 5; i++) out[i] = (i < 5 - y) ? a : b;
    out[5] = 0;
    return 5;
}

/* 2 Chord: OFF, 1..42 = close detune of the side voices, 0.01 .. 1.00 semitone
 * (shown as that number), 44..48 = 3..7 semitones (43 = 2), 49..83 = the 35 chords of a classic chord
 * machine (without its Unison entries, which the detune covers), in its order and with
 * its names (M = major, m = minor, add = added tone, no5 = no fifth). Names longer
 * than the 5 characters of the screen are shortened the same way: Madd9b5 = M9b5,
 * Maj7b5 = Mj7b5, M7b9no5 = M7b9, sus4#5b9 = s4#5, sus4add#5 = s4a#5, Maddb5 = Madb5,
 * M6add4no5 = M6a4, Maj7/6no5 = Mj7/6, Maj9no5 = Mj9, Fourths = 4ths, Fifths = 5ths.
 * The main voice is the root. */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    int n = (int)value, len = 0, t = 0;
    if (n > 83) n = 83;
    if (n == 0) { out[0]='O'; out[1]='F'; out[2]='F'; out[3] = 0; return 3; }
    if (n <= 42) {                                    /* close detune in semitones: 0.01 .. 1.00 */
        if (n > 20) n = (n <= 30) ? 20 + 2 * (n - 20) : 40 + 5 * (n - 30);
        if (n == 100) { out[0]='1'; out[1]='.'; out[2]='0'; out[3]='0'; out[4] = 0; return 4; }
        while (n >= 10) { n -= 10; t++; }
        out[0]='0'; out[1]='.'; out[2]=(char)('0' + t); out[3]=(char)('0' + n); out[4] = 0;
        return 4;
    }
    if (n <= 48) {                                    /* wider dissonance: 2.00 .. 7.00 semitones */
        out[0] = (char)('0' + (n - 41)); out[1]='.'; out[2]='0'; out[3]='0'; out[4] = 0;
        return 4;
    }
    n -= 48;                                          /* chord 1..35 */
    if (n == 1) { out[0]='m'; out[1]='i'; out[2]='n'; out[3]='o'; out[4]='r'; len = 5; }
    else if (n == 2) { out[0]='M'; out[1]='a'; out[2]='j'; out[3]='o'; out[4]='r'; len = 5; }
    else if (n == 3) { out[0]='s'; out[1]='u'; out[2]='s'; out[3]='2'; len = 4; }
    else if (n == 4) { out[0]='s'; out[1]='u'; out[2]='s'; out[3]='4'; len = 4; }
    else if (n == 5) { out[0]='m'; out[1]='7'; len = 2; }
    else if (n == 6) { out[0]='M'; out[1]='7'; len = 2; }
    else if (n == 7) { out[0]='m'; out[1]='M'; out[2]='a'; out[3]='j'; out[4]='7'; len = 5; }
    else if (n == 8) { out[0]='M'; out[1]='a'; out[2]='j'; out[3]='7'; len = 4; }
    else if (n == 9) { out[0]='7'; out[1]='s'; out[2]='u'; out[3]='s'; out[4]='4'; len = 5; }
    else if (n == 10) { out[0]='d'; out[1]='i'; out[2]='m'; out[3]='7'; len = 4; }
    else if (n == 11) { out[0]='m'; out[1]='a'; out[2]='d'; out[3]='d'; out[4]='9'; len = 5; }
    else if (n == 12) { out[0]='M'; out[1]='a'; out[2]='d'; out[3]='d'; out[4]='9'; len = 5; }
    else if (n == 13) { out[0]='m'; out[1]='6'; len = 2; }
    else if (n == 14) { out[0]='M'; out[1]='6'; len = 2; }
    else if (n == 15) { out[0]='m'; out[1]='b'; out[2]='5'; len = 3; }
    else if (n == 16) { out[0]='M'; out[1]='b'; out[2]='5'; len = 3; }
    else if (n == 17) { out[0]='m'; out[1]='7'; out[2]='b'; out[3]='5'; len = 4; }
    else if (n == 18) { out[0]='M'; out[1]='7'; out[2]='b'; out[3]='5'; len = 4; }
    else if (n == 19) { out[0]='M'; out[1]='#'; out[2]='5'; len = 3; }
    else if (n == 20) { out[0]='m'; out[1]='7'; out[2]='#'; out[3]='5'; len = 4; }
    else if (n == 21) { out[0]='M'; out[1]='7'; out[2]='#'; out[3]='5'; len = 4; }
    else if (n == 22) { out[0]='m'; out[1]='b'; out[2]='6'; len = 3; }
    else if (n == 23) { out[0]='m'; out[1]='9'; out[2]='n'; out[3]='o'; out[4]='5'; len = 5; }
    else if (n == 24) { out[0]='M'; out[1]='9'; out[2]='n'; out[3]='o'; out[4]='5'; len = 5; }
    else if (n == 25) { out[0]='M'; out[1]='9'; out[2]='b'; out[3]='5'; len = 4; }
    else if (n == 26) { out[0]='M'; out[1]='j'; out[2]='7'; out[3]='b'; out[4]='5'; len = 5; }
    else if (n == 27) { out[0]='M'; out[1]='7'; out[2]='b'; out[3]='9'; len = 4; }
    else if (n == 28) { out[0]='s'; out[1]='4'; out[2]='#'; out[3]='5'; len = 4; }
    else if (n == 29) { out[0]='s'; out[1]='4'; out[2]='a'; out[3]='#'; out[4]='5'; len = 5; }
    else if (n == 30) { out[0]='M'; out[1]='a'; out[2]='d'; out[3]='b'; out[4]='5'; len = 5; }
    else if (n == 31) { out[0]='M'; out[1]='6'; out[2]='a'; out[3]='4'; len = 4; }
    else if (n == 32) { out[0]='M'; out[1]='j'; out[2]='7'; out[3]='/'; out[4]='6'; len = 5; }
    else if (n == 33) { out[0]='M'; out[1]='j'; out[2]='9'; len = 3; }
    else if (n == 34) { out[0]='4'; out[1]='t'; out[2]='h'; out[3]='s'; len = 4; }
    else { out[0]='5'; out[1]='t'; out[2]='h'; out[3]='s'; len = 4; }
    out[len] = 0;
    return len;
}

/* 4 Tempo: screen 0..441 -> the BPM "40" .. "240"; the twin copy 241..441 shows the
 * same numbers again (fm_tempo_bpm, as in fm_prepare) */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value > 441u) value = 441u;
    n = fm_tempo_bpm((int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* 5 Div: length of one LFO cycle, 4bar 3bar 2bar 1.5b 1bar 1/2. 1/2 1/4. 1/4 1/8. 1/8
 * 1/8T 1/16 1/16T 1/32 1/32T 1/64 (bar = 4 beats = 16 steps: 4bar = 64 steps, 3bar = 48,
 * 2bar = 32, 1.5b = 24 = 3/4 of 2 bars, 1/2. = 12 = 3/4 of a bar ...) */
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

/* 6 Shape: Sine Step Rand Solo Some Canon Ripl Fan Walk Swell Spot */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    int n = (int)value, len = 0;
    if (n > 10) n = 10;
    if (n == 0) { out[0]='S'; out[1]='i'; out[2]='n'; out[3]='e'; len = 4; }
    else if (n == 1) { out[0]='S'; out[1]='t'; out[2]='e'; out[3]='p'; len = 4; }
    else if (n == 2) { out[0]='R'; out[1]='a'; out[2]='n'; out[3]='d'; len = 4; }
    else if (n == 3) { out[0]='S'; out[1]='o'; out[2]='l'; out[3]='o'; len = 4; }
    else if (n == 4) { out[0]='S'; out[1]='o'; out[2]='m'; out[3]='e'; len = 4; }
    else if (n == 5) { out[0]='C'; out[1]='a'; out[2]='n'; out[3]='o'; out[4]='n'; len = 5; }
    else if (n == 6) { out[0]='R'; out[1]='i'; out[2]='p'; out[3]='l'; len = 4; }
    else if (n == 7) { out[0]='F'; out[1]='a'; out[2]='n'; len = 3; }
    else if (n == 8) { out[0]='W'; out[1]='a'; out[2]='l'; out[3]='k'; len = 4; }
    else if (n == 9) { out[0]='S'; out[1]='w'; out[2]='e'; out[3]='l'; out[4]='l'; len = 5; }
    else { out[0]='S'; out[1]='p'; out[2]='o'; out[3]='t'; len = 4; }
    out[len] = 0;
    return len;
}


/* ---- pedal entry point (same structure as DualShft / DubSiren) ---------- */
#ifndef FORMANT_HOST_TEST

#include "formant_params.h"

#ifndef FORMANT_AUDIO_FUNC
#define FORMANT_AUDIO_FUNC Fx_DLY_Formant
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SR_CODE_SECTION(FORMANT_AUDIO_FUNC)
void FORMANT_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    FmState *s;
    FmParams P;
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
    if ((end - base) < sizeof(FmState) || span < (end - base)) return;
    if (stateBase + sizeof(FmState) > end) return;

    s = (FmState *)stateBase;

    k[0] = sr_knob(params[FORMANT_VOWEL_SLOT], (float)FORMANT_VOWEL_UI_DEFAULT, 0.025f);
    k[1] = fm_tempo_ui(params[FORMANT_TEMPO_SLOT], (float)FORMANT_TEMPO_UI_DEFAULT) * 0.0022675737f;   /* 1/441 */
    k[2] = sr_knob(params[FORMANT_DIV_SLOT],   (float)FORMANT_DIV_UI_DEFAULT,   0.0625f);
    k[3] = sr_knob(params[FORMANT_DEPTH_SLOT], (float)FORMANT_DEPTH_UI_DEFAULT, 0.01f);
    k[4] = sr_knob(params[FORMANT_RESO_SLOT],  (float)FORMANT_RESO_UI_DEFAULT,  0.01f);
    k[5] = sr_knob(params[FORMANT_MIX_SLOT],   (float)FORMANT_MIX_UI_DEFAULT,   0.01f);
    k[6] = sr_knob(params[FORMANT_CHORD_SLOT], (float)FORMANT_CHORD_UI_DEFAULT, 0.012048193f);
    k[7] = sr_knob(params[FORMANT_PARAM_SLOT],    (float)FORMANT_PARAM_UI_DEFAULT,    0.01f);
    k[8] = sr_knob(params[FORMANT_SHAPE_SLOT],  (float)FORMANT_SHAPE_UI_DEFAULT,  0.1f);

    if (s->magic != FM_MAGIC) fm_init(s);
    fm_prepare(s, &P, k);                        /* restarts the LFO on a Tempo flip */
    if (params[0] < 0.5f) {                      /* effect switched off: input untouched, */
        s->lfo_ph += P.lfo_inc;                  /* but the LFO keeps time with the bar   */
        if (s->lfo_ph >= 1.0f) s->lfo_ph -= 1.0f;
        return;
    }
    fm_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    SR_NOUNROLL
    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* FORMANT_HOST_TEST */
