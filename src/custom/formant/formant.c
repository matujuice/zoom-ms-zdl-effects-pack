/*
 * Choral - a choir that sings the note your synth plays (Zoom MultiStomp, custom ZDL, mono).
 * (Source folder and audio function keep the working name "Formant".)
 * Rewritten from the ground up on 2026-10-07 (Luca): the old Choral only filtered the input,
 * so it sounded different on every synth and its auto-level pumped and clicked. This one
 * listens to the pitch and sings it with its own voices, so every synth gets the same choir.
 * For synths, one note at a time (leads, basses, arps). Never meant for drums or guitar.
 *
 * SIGNAL FLOW
 *   in --> pitch tracker (note + "is there a note?") ---------------------+
 *     \--> loudness follower ---------------------------------------+    |
 *                                                                   v    v
 *   singers (1..6): band-limited saw at the note, in their section's register, each a few
 *   cents apart with its own vibrato --> summed per section --> 3 vowel formants per section
 *   --> wet (one soft clipper) --> Mix with the dry synth
 *
 * PITCH TRACKER (once per 8-sample block)
 *   The input is DC-blocked and low-passed at 1 kHz (2-pole), and the 8 samples of a block are
 *   averaged into one sample at 5512.5 Hz. For every lag t = 1..138 (40 Hz .. 5.5 kHz) a
 *   running mean of (x[n] - x[n-t])^2 is kept, over about two periods of that lag (so high
 *   notes are found fast and low notes are still measured over whole cycles). The note is
 *   the first lag (>= 5, below 1.1 kHz) whose mean, times t, drops under 0.15 x the sum of
 *   the means up to t (YIN's normalised difference, without a divide), then the bottom of
 *   that dip, refined by a parabola and then by a cosine model of the dip (sub-sample
 *   period). A new note is taken once it has stayed within 40 cents for 24 blocks (4 ms).
 *   After an attack in the input (onset: fast envelope > 4 x slow envelope) the means forget
 *   the old note twice as fast. A legato note change has no attack: once the old note's
 *   period no longer fits the input at all, it counts as one. At Glide 0 the voice then
 *   hushes (~6 ms) and the new note comes in with its own attack, so fast arps never sing
 *   the old note over the new one; at any other Glide it keeps singing and slides (legato).
 *   Detuned oscillators that beat (or start out of phase) cancel the fundamental for a while,
 *   and the wave then really reads an octave or a fifth up (or the octave below). So with no
 *   attack, a new reading that the old note's period still fits as well waits 3 s; this
 *   also holds after the note ran out of readings, as long as the input stayed loud. The one
 *   real jump the old period still fits is an octave up: it is taken fast when the odd
 *   harmonics of the old note vanished at once (not in the 0.2 s after an attack), which
 *   beating never does. Above ~610 Hz the dips are too coarse to compare: an octave there
 *   waits 0.3 s (up: unless the odd harmonics vanished).
 *   While the input stays loud the note is kept through bad readings (up to 0.4 s); after
 *   that, or once the input fades, the singers keep singing for 60 ms after the last good
 *   reading, so a short glitch does not cut the voice. Host test (tests/choral_voice.c): within
 *   10 cents on saw, square, sine, filtered saw and two detuned saws from 41 Hz to 1 kHz; a
 *   re-attacked note is found in at most ~30 ms, a legato note in ~55 ms, a legato octave
 *   (saw, square, sine, filtered saw) in at most ~70 ms; a 160 BPM 16th arp at 50% gate
 *   misses no note (each found within 20 ms); 2 and 3 detuned saws (0.15..1.2%) held 6 s on
 *   C2..A4 never slip to another octave or harmonic.
 *
 * VOICES
 *   Every singer is a band-limited saw (PolyBLEP) at 2^(note + section octave + Chord octave
 *   + detune + vibrato). Its level follows the input's loudness (a ~10 ms RMS follower, then
 *   Feel's attack and release), so the synth's own envelope still shapes the phrasing, and is
 *   scaled by (220 Hz / its pitch)^0.75 so high and low notes come out about as loud through
 *   the formants. The voice glides to a new note at the Glide speed (in octaves, so every
 *   interval takes the same time); after a silence it starts on the new note directly.
 *   Sections (Choir knob): GIANT one octave under your note, MEN on it, WOMEN one above,
 *   KIDS two above. Each has its own throat size (formants x0.78 / 1.0 / 1.17 / 1.35), width,
 *   darkness, breath and vibrato rate (4.6 / 5.2 / 5.6 / 6.0 Hz). Singers are dealt to the
 *   sections in turn. Detune per singer: 0, +8, -7, +12, -11, +4 cents (more spread in a
 *   bigger choir), plus a slow random drift; the extra singers follow the loudness a little
 *   late, so a big choir is looser than a solo.
 *   Breath: noise per section, low-passed at twice the note and scaled by the singers' own
 *   level (pitch scaling included), so it stays ~24..34 dB under the voice on every note and
 *   section (more at Feel 0, less at 100). It used to follow the input instead, which made
 *   high notes and the higher sections hiss.
 *
 * FORMANTS (per section, coefficients once per block)
 *   Three band-passes (Simper's state-variable filter, which stays clean while the vowel
 *   moves) at the vowel's F1 F2 F3 (adult male table, x the section's throat size), widths
 *   70 / 90 / 120 Hz x the section's width, peak levels F1 1, F2 and F3 from the vowel table.
 *   Each band's gain is also x F / 500 Hz, because a saw falls 6 dB per octave and the
 *   table gives the levels of a real voice. The vowel glides over ~10 ms.
 *     vowel    A     E     I     O     U          F2 level, F3 level
 *     F1      800   450   280   430   290         A 0.6 0.3   E 0.8 0.3   I 0.75 0.3
 *     F2     1250  2000  2400   800   750         O 0.4 0.2   U 0.25 0.1
 *     F3     2800  2750  3100  2600  2300
 *   SVF: g = tan(pi F / fs) (Taylor, F <= 5 kHz), k = width / F, a1 = 1/(1 + g(g + k)),
 *   a2 = g a1, a3 = g a2; per sample v3 = x - ic2, v1 = a1 ic1 + a2 v3,
 *   v2 = ic2 + a2 ic1 + a3 v3, ic1 = 2 v1 - ic1, ic2 = 2 v2 - ic2; band = k v1 (0 dB peak).
 *
 * LEVELS: the singers' level follows the input's loudness. A voice gain then corrects for
 *   what the formants do to our own saw at this pitch and vowel: the choir's power over the
 *   power the singers were asked for (8 x level^2), both smoothed over ~50 ms, gives the gain
 *   (limited to x0.2..x5, moving 1% per block, held while the choir is silent). Both powers
 *   follow the input's envelope together, so unlike the old auto-level it does not pump with
 *   the synth's dynamics. One soft clipper (x - x^3 / 6.75, flat from 1.5) on the wet signal
 *   only. Host test: choir within -1.0..+1.9 dB of the input over every Choir x Size x Sing
 *   and notes 55..660 Hz; the same note on five sources within 2.1 dB.
 *
 * CONTROLS (screen values), in pedal order
 *   0 Choir  who sings: MEN WOMEN KIDS GIANT M+W M+KID W+KID G+M ALL
 *   1 Size   how many: SOLO DUO TRIO QUART QUINT SEXT (1..6 singers)
 *   2 Chord  how they sing together: UNIS (all on the note), OCT (every second singer an
 *            octave up). More to come (5th, triads, drone).
 *   3 Sing   what they sing: AAH, OOH, OOAH (opens from "oo" to "ah" and closes again, once
 *            per Pace cycle). More to come (hum, la, doo, canon, swell).
 *   4 Pace   length of one cycle of the movement: 4bar 3bar 2bar 1.5b 1bar 1/2. 1/2 1/4. 1/4
 *            1/8. 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64 (bar = 4 beats)
 *   5 Feel   0 = soft and legato (slow attack and release, wide late vibrato, breathy, dark)
 *            .. 100 = punchy and staccato (fast attack and release, narrow vibrato, bright,
 *            and OOAH snaps between its two vowels)
 *   6 Glide  0 = every note starts fresh (a legato change hushes the old note and
 *            re-attacks, for fast arps); 1..100 = legato, the voice slides to a new note
 *            in ~3 ms .. ~0.6 s
 *   7 Tempo  BPM for Pace, 40..240; screen 0..441, 241..441 is the twin copy (see TEMPO SYNC);
 *            0..39 = FOLLW (bar tag). The 8th knob, as on every synced effect of the pack.
 *   8 Mix    dry synth / choir, DJ-style: dry full up to 50, choir full from 50
 *
 * TEMPO SYNC
 *   A custom ZDL cannot read the pedal's BPM, so it comes from the Tempo knob and Pace picks
 *   the note value of one cycle. A new BPM only changes the speed. Flipping between a BPM and
 *   its twin (120 <-> 321) marks a downbeat: the movement goes to where it should be that
 *   many bars after the last downbeat (counted from the time between them at the current
 *   BPM), so a 2-bar or dotted Pace is never cut in the middle. The first flip, or one more
 *   than 512 bars after the last, starts the cycle from 0.
 * BAR TAG (src/custom/common/drytag.h, docs/TEMPO-SYNC.md "Bar tag"): with Tempo on FOLLW it
 *   takes the BPM and the bars from an effect in an earlier slot; on any BPM it ignores them.
 *   In slots 1-3, FOLLOW needs Mozaic's Send knob on another slot.
 *
 * FOOTSWITCH: off = untouched input; the movement keeps time and follows flips. On again,
 *   the filters, singers and tracker start clean and the choir fades in on the next note.
 *
 * SIZE: must stay well under the ~31 KB the pedal boots with (docs/SAFE-DSP-RULES.md, Size);
 *   not measured yet. Helpers are inlined, so code shared by several paths runs through one
 *   loop (singers, sections, bands). CPU not measured yet either.
 */

#include <stdint.h>
#include "../common/drytag.h"

#ifdef __TI_COMPILER_VERSION__
#define SR_DO_PRAGMA(x) _Pragma(#x)
#define SR_EXPAND_PRAGMA(x) SR_DO_PRAGMA(x)
#define SR_ALWAYS_INLINE(fn) SR_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SR_CODE_SECTION(fn) SR_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#define SR_NOUNROLL _Pragma("UNROLL(1)")
#else
#define SR_ALWAYS_INLINE(fn)
#define SR_CODE_SECTION(fn)
#define SR_NOUNROLL
#endif

#define CH_MAGIC      0x43483035u          /* "CH05" */
#define CH_NS         6                    /* singers                                 */
#define CH_RING       256                  /* tracker history at fs/8 (power of 2)    */
#define CH_RMASK      255
#define CH_MAXLAG     138                  /* 5512.5 / 40 Hz                          */
#define CH_MINLAG     5                    /* 1.1 kHz                                 */
#define CH_THR        0.15f                /* YIN threshold                           */
#define CH_LOG2_FD    12.428491f           /* log2(5512.5)                            */
#define CH_STABLE     24                   /* blocks a new note must hold (4 ms)      */
#define CH_STABLE_HI  1650                 /* ... the same above ~610 Hz (0.3 s)      */
#define CH_STABLE_OCT 16500                /* ... a harmonic of the note being sung, no attack (3 s) */
#define CH_FITS       0.2f                 /* old note's dip under the new one's + this: it still fits */
#define CH_GONE       0.6f                 /* sung note's dip above this x mean: no longer there */
#define CH_YOUNG      1100                 /* blocks after an attack before an octave up can be fast (0.2 s) */
#define CH_ODD_MIN    0.8f                 /* odd share (x mean difference) that was clearly there ... */
                                           /* ... and fell under 0.35 x its ~60 ms peak: a real octave up */
#define CH_LOST       2200                 /* blocks the note is kept without a reading while loud (0.4 s) */
#define CH_ONSET      220                  /* blocks an attack counts as a new note (40 ms) */
#define CH_HOLD       330                  /* blocks the voice holds without a reading (60 ms) */
#define CH_INC_HZ     2.2675737e-5f        /* 1 / 44100                               */
#define CH_BLK_HZ     1.8140590e-4f        /* 8 / 44100: a block's phase per Hz       */
#define CH_BPM_BLOCK  3.0234e-6f           /* 8 / (60 * 44100): phase per block per (BPM x mult) */
#define CH_BARS_PER_BLK 7.5586e-7f         /* bars per block per BPM: 8 / (4 * 60 * 44100) */
#define CH_PI_FS      7.1237928e-5f        /* pi / 44100                              */
#define CH_LEVEL      6.2f                 /* overall choir level, set by measurement */
#define CH_TILT       0.75f                /* level slope vs pitch: (220 Hz / f)^0.75, measured */
#define CH_BREATH     0.06f                /* breath noise, ~-24..-34 dB under the voice at Feel 40 */

typedef struct {
    unsigned int magic;
    /* tracker */
    float hpx, hpy, l1, l2, m1, m2;        /* DC blocker, 2-pole low-pass (TDF-II)    */
    float e2, ef;                          /* input power followers, ~9 ms and ~0.5 ms */
    int   onset;                           /* blocks left since a fast attack         */
    int   wp;
    float ring[CH_RING];
    float dm[CH_MAXLAG + 2];               /* running mean of the squared difference per lag */
    float cc[CH_MAXLAG + 2];               /* its coefficient, 1 / (2 lag)            */
    float cand;                            /* candidate note, octaves                 */
    int   stable, hold, lost;              /* blocks the candidate held; left to sing; since a reading */
    float lref;                            /* input power at the last good reading    */
    float rf, rs;                          /* odd-harmonic share of the sung note, ~3 ms; its recent peak */
    int   odrop;                           /* blocks left since the odd share fell at once */
    int   age;                             /* blocks since the last attack (capped)   */
    int   gone;                            /* the sung note no longer fits the input  */
    int   kept;                            /* no attack or fade since the last note: it still counts */
    float note;                            /* accepted note, octaves (log2 Hz)        */
    float base;                            /* note after Glide                        */
    float vamp;                            /* voice level (after Feel)                */
    float vibon;                           /* vibrato fade-in after a note start, 0..1 */
    /* singers */
    float sph[CH_NS], vph[CH_NS], drift[CH_NS], amp[CH_NS];
    unsigned int rng;
    /* sections: GIANT MEN WOMEN KIDS */
    float tilt[4];
    float nlp[4];                          /* breath noise low-pass per section       */
    float ic1[12], ic2[12];                /* 4 sections x 3 formants                 */
    float wsm;                             /* smoothed vowel blend                    */
    float pw, pa, gn;                      /* choir power, asked power, voice gain    */
    /* movement and sync */
    float lfo_ph, ref_ph;                  /* phase now; phase at the last downbeat   */
    unsigned int since;                    /* blocks since the last downbeat          */
    int   flipped;                         /* a downbeat has been seen                */
    float last_twin;                       /* Tempo copy at the last block (0, 1)     */
    DtSync sync;
    int   was_off;
} ChState;

typedef struct {
    float bpm, tempo, lfo_inc, cpb;        /* cpb = cycles per bar                    */
    int   choir, size, chord, sing;
    int legato; float feel, glide_c, att, rel, vibc, breath, bright;
    float dryG, wetG;
} ChParams;

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

/* Tempo knob (screen 0..441): read raw x 100 up to 4.415 (sr_knob's 3.05 guess would take
 * 4.41 for an on-screen 4). Returns the screen number. */
SR_ALWAYS_INLINE(ch_tempo_ui)
static inline float ch_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > 441.0f) ui = 441.0f;
    return ui;
}

/* Tempo screen number -> BPM: 0..240 as is (at least 40), 241..441 the twin copy */
SR_ALWAYS_INLINE(ch_tempo_bpm)
static inline int ch_tempo_bpm(int ui)
{
    if (ui > 240) ui -= 201;
    if (ui < 40) ui = 40;
    return ui;
}

/* 1/x for x > 0: exponent trick, then three Newton steps (no divide) */
SR_ALWAYS_INLINE(ch_recip)
static inline float ch_recip(float x)
{
    union { float f; unsigned int u; } c;
    float y;
    c.f = x;
    c.u = 0x7EF311C3u - c.u;
    y = c.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y;
}

SR_ALWAYS_INLINE(ch_rsqrt)
static inline float ch_rsqrt(float x)
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

/* log2(x) for x > 0: exponent bits + a polynomial of the mantissa (error < 0.0003 oct) */
SR_ALWAYS_INLINE(ch_log2)
static inline float ch_log2(float x)
{
    union { float f; unsigned int u; } c;
    float m, e;
    c.f = x;
    e = (float)((int)((c.u >> 23) & 255u) - 127);
    c.u = (c.u & 0x007FFFFFu) | 0x3F800000u;           /* mantissa 1..2 */
    m = c.f;
    return e - 2.4968461f + m * (4.0285479f + m * (-2.0812142f + m * (0.62887362f + m * -0.07915816f)));
}

/* 2^x for -100 < x < 100: exponent bits + a polynomial of the fraction (error < 3e-7) */
SR_ALWAYS_INLINE(ch_exp2)
static inline float ch_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int   n;
    float f;
    n = (int)(x + 128.0f) - 128;                       /* floor */
    f = x - (float)n;
    c.u = (unsigned int)(n + 127) << 23;
    return c.f * (0.99999977f + f * (0.69315676f + f * (0.24013177f + f * (0.05587644f
                 + f * (0.00894063f + f * 0.00189439f)))));
}

/* Triangle starting at 0 and rising: ph in [0,1) -> [-1,+1] */
SR_ALWAYS_INLINE(tri_bi)
static inline float tri_bi(float ph)
{
    float q = ph + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
}

/* sin(2 pi ph), from the triangle by an odd polynomial, error < 1e-4 */
SR_ALWAYS_INLINE(sine_of)
static inline float sine_of(float ph)
{
    float t = tri_bi(ph), t2 = t * t;
    return t * (1.5706268f + t2 * (-0.6432292f + t2 * 0.0727102f));
}

/* Vowel table: formant n (0..2) of vowel v (0..4 = A E I O U), Hz; selects on literals */
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

/* level of formant n for vowel v, times the vowel's loudness trim (measured) */
SR_ALWAYS_INLINE(vowel_amp)
static inline float vowel_amp(int n, int v)
{
    float r, tr;
    if (n == 0)      r = 1.0f;
    else if (n == 1) r = (v == 0) ? 0.6f : (v == 1) ? 0.8f : (v == 2) ? 0.75f : (v == 3) ? 0.4f : 0.25f;
    else             r = (v == 0) ? 0.3f : (v == 1) ? 0.3f : (v == 2) ? 0.3f : (v == 3) ? 0.2f : 0.1f;
    tr = (v == 0) ? 1.0f : (v == 1) ? 1.0f : (v == 2) ? 1.0f : (v == 3) ? 1.0f : 1.0f;
    return r * tr;
}

/* Pace index 0..16 -> cycles per beat (bar = 4 beats) */
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

SR_ALWAYS_INLINE(rnd01)
static inline float rnd01(ChState *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(s->rng >> 8) * 5.9604645e-8f;
}

/* Sections that sing in each Choir setting: 2 bits per entry (0 GIANT, 1 MEN, 2 WOMEN,
 * 3 KIDS) in the order singers are dealt, entry count in bits 12..14 */
SR_ALWAYS_INLINE(choir_word)
static inline unsigned int choir_word(int c)
{
    return (c == 0) ? 0x1001u          /* MEN            */
         : (c == 1) ? 0x1002u          /* WOMEN          */
         : (c == 2) ? 0x1003u          /* KIDS           */
         : (c == 3) ? 0x1000u          /* GIANT          */
         : (c == 4) ? 0x2009u          /* M+W:   M W     */
         : (c == 5) ? 0x200Du          /* M+KID: M K     */
         : (c == 6) ? 0x200Eu          /* W+KID: W K     */
         : (c == 7) ? 0x2001u          /* G+M:   M G     */
         : 0x40C9u;                    /* ALL:   M W G K */
}

/* Clear what sounds: filters, singers, tracker. The movement keeps its phase. */
SR_ALWAYS_INLINE(ch_clear)
static inline void ch_clear(ChState *s)
{
    int i;
    s->hpx = 0.0f; s->hpy = 0.0f; s->l1 = 0.0f; s->l2 = 0.0f; s->m1 = 0.0f; s->m2 = 0.0f;
    s->e2 = 0.0f; s->ef = 0.0f; s->onset = 0; s->wp = 0; s->stable = 0; s->hold = 0; s->lost = 0; s->lref = 0.0f; s->rf = 0.0f; s->rs = 0.0f; s->odrop = 0; s->age = 0; s->gone = 0; s->kept = 0; s->cand = 0.0f; s->vamp = 0.0f;
    s->vibon = 0.0f; s->wsm = -1.0f; s->pw = 0.0f; s->pa = 0.0f; s->gn = 1.0f;
    SR_NOUNROLL
    for (i = 0; i < CH_RING; i++) s->ring[i] = 0.0f;
    SR_NOUNROLL
    for (i = 0; i < CH_MAXLAG + 2; i++) s->dm[i] = 0.0f;
    SR_NOUNROLL
    for (i = 0; i < 12; i++) { s->ic1[i] = 0.0f; s->ic2[i] = 0.0f; }
    SR_NOUNROLL
    for (i = 0; i < 4; i++) { s->tilt[i] = 0.0f; s->nlp[i] = 0.0f; }
    SR_NOUNROLL
    for (i = 0; i < CH_NS; i++) s->amp[i] = 0.0f;
}

SR_ALWAYS_INLINE(ch_init)
static inline void ch_init(ChState *s)
{
    int i;
    ch_clear(s);
    s->note = 7.78f; s->base = 7.78f;                  /* 220 Hz until a note is heard */
    SR_NOUNROLL
    for (i = 0; i < CH_MAXLAG + 2; i++) {
        float w = 2.0f * (float)i;
        if (w < 32.0f) w = 32.0f;
        s->cc[i] = ch_recip(w);
    }
    SR_NOUNROLL
    for (i = 0; i < CH_NS; i++) { s->sph[i] = 0.17f * (float)i; s->vph[i] = 0.23f * (float)i; s->drift[i] = 0.0f; }
    s->rng = 0x1234567u;
    s->lfo_ph = 0.0f; s->ref_ph = 0.0f; s->since = 0u; s->flipped = 0; s->last_twin = -1.0f;
    s->was_off = 0;
    dt_sync_init(&s->sync);
    s->magic = CH_MAGIC;
}

/* k[] = 0..1 by each knob's own max, in pedal order (Tempo as screen/441) */
SR_ALWAYS_INLINE(ch_prepare)
static inline void ch_prepare(ChState *s, ChParams *P, const float *k)
{
    int   tempo_i = (int)(k[7] * 441.0f + 0.5f);
    float twin, f, m;
    P->tempo = (float)tempo_i;
    P->bpm   = (float)ch_tempo_bpm(tempo_i);
    P->choir = (int)(k[0] * 8.0f + 0.5f);
    P->size  = 1 + (int)(k[1] * 5.0f + 0.5f);
    P->chord = (int)(k[2] + 0.5f);
    P->sing  = (int)(k[3] * 2.0f + 0.5f);
    m        = subdiv_mult((int)(k[4] * 16.0f + 0.5f));
    P->cpb   = 4.0f * m;
    P->lfo_inc = P->bpm * m * CH_BPM_BLOCK;
    f = k[5];
    P->feel  = f;
    /* Feel: attack 60 .. 2 ms, release 300 .. 30 ms (per-block coefficients), vibrato 28 .. 6
     * cents, breath 1.4 .. 0.6, brightness of the source 0.55 .. 1 */
    P->att    = 0.0030f * ch_exp2(4.9f * f);          /* 0.003 .. 0.09 per block */
    P->rel    = 0.0006f * ch_exp2(3.3f * f);          /* 0.0006 .. 0.006 per block */
    P->vibc   = (28.0f - 22.0f * f) * 0.00083333f;    /* cents -> octaves */
    P->breath = CH_BREATH * (1.4f - 0.8f * f);
    P->bright = 0.55f + 0.45f * f;
    P->glide_c = 0.06f * ch_exp2(-7.6f * k[6]);       /* ~3 ms .. ~0.6 s */
    P->legato  = (k[6] > 0.005f);                     /* Glide 0: every note starts fresh */
    P->dryG = 2.0f - 2.0f * k[8];
    if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * k[8];
    if (P->wetG > 1.0f) P->wetG = 1.0f;

    /* sync: a flip of the Tempo copy is a downbeat. Go to where the cycle should be that
     * many bars after the last one (bar-aware, so long and dotted Paces are not cut). */
    twin = (P->tempo > 240.0f) ? 1.0f : 0.0f;
    if (s->last_twin >= 0.0f && twin != s->last_twin) {
        float bars = (float)s->since * P->bpm * CH_BARS_PER_BLK;
        float ph = 0.0f;
        if (s->flipped && bars < 512.0f) {
            ph = s->ref_ph + (float)(int)(bars + 0.5f) * P->cpb;
            ph -= (float)(int)ph;
        }
        s->lfo_ph = ph; s->ref_ph = ph; s->since = 0u; s->flipped = 1;
    }
    s->last_twin = twin;
}

/* the movement, once per block, also while switched off */
SR_ALWAYS_INLINE(ch_tick)
static inline void ch_tick(ChState *s, const ChParams *P)
{
    s->lfo_ph += P->lfo_inc;
    if (s->lfo_ph >= 1.0f) s->lfo_ph -= 1.0f;
    if (s->since < 0x7FFFFFFFu) s->since++;
}

/* bottom of a dip y1 between y0 and y2, by a parabola (y1 if they don't curve up) */
SR_ALWAYS_INLINE(ch_dip)
static inline float ch_dip(float y0, float y1, float y2)
{
    float den = y0 - y1 - y1 + y2;
    float m = (den > 1e-20f) ? y1 - 0.125f * (y0 - y2) * (y0 - y2) * ch_recip(den) : y1;
    return (m < y1) ? m : y1;
}

SR_ALWAYS_INLINE(ch_process)
static inline void ch_process(ChState *s, const ChParams *P, float *buf)
{
    float inc[CH_NS], iinc[CH_NS], sg[CH_NS], ka[CH_NS];
    int   sec[CH_NS];
    float a1[12], a2[12], a3[12], gk[12];
    float tl[4], ns[4], nc[4];
    int   act[4];
    unsigned int cw;
    int   i, j, n, b, nsec, best, ln, lh;
    float v, h, y, cum, cn, qb, tgt, w, wv, nrm, lvl, p1, p2, pw = 0.0f, pa;

    /* ---- tracker: this block's input, low-passed and averaged to one sample ---- */
    v = 0.0f;
    SR_NOUNROLL
    for (i = 0; i < 8; i++) {
        float in = buf[i];
        h = in - s->hpx + 0.995f * s->hpy;           /* DC blocker */
        s->hpx = in; s->hpy = h;
        y = 0.004603998f * h + s->l1;                /* 2-pole low-pass at 1 kHz (TDF-II) */
        s->l1 = 0.009207997f * h + 1.7990964f * y + s->l2;
        s->l2 = 0.004603998f * h - 0.8175124f * y;
        v += y;
        s->e2 += 0.0025f * (in * in - s->e2);         /* ~9 ms power follower */
        s->ef += 0.05f * (in * in - s->ef);           /* ~0.5 ms */
    }
    if (s->age < CH_YOUNG) s->age++;
    if (s->ef > 4.0f * s->e2 && s->ef > 1e-6f) { s->onset = CH_ONSET; s->age = 0; s->kept = 0; }   /* a fast attack: a new note */
    else if (s->onset > 0) s->onset--;
    if (s->hpy < 1e-20f && s->hpy > -1e-20f) s->hpy = 0.0f;
    if (s->l1 < 1e-20f && s->l1 > -1e-20f) s->l1 = 0.0f;
    if (s->l2 < 1e-20f && s->l2 > -1e-20f) s->l2 = 0.0f;
    v *= 0.125f;
    s->wp = (s->wp + 1) & CH_RMASK;
    s->ring[s->wp] = v;
    cum = 0.0f; best = 0; p1 = 1e30f; p2 = 1e30f; cn = 0.0f; qb = 1.0f;
    ln = (int)(ch_exp2(CH_LOG2_FD - s->note) + 0.5f);    /* lag of the note being sung */
    SR_NOUNROLL
    for (n = 1; n <= CH_MAXLAG; n++) {
        float e = v - s->ring[(s->wp - n) & CH_RMASK], d, c = s->cc[n];
        if (s->onset > 0) c += c;                    /* a new note: forget the old one twice as fast */
        d = s->dm[n] + c * (e * e - s->dm[n]);
        s->dm[n] = d;
        /* lag n-1 is a dip: is its bottom (by a parabola) under the threshold? */
        if (best == 0 && n > CH_MINLAG && p1 <= p2 && p1 <= d) {
            h = ch_dip(p2, p1, d);
            if (h * (float)(n - 1) < CH_THR * cum) { best = n - 1; qb = h * (float)best * ch_recip(cum); }
        }
        cum += d;
        if (n == ln) cn = cum;
        p2 = p1; p1 = d;
    }
    /* the note being sung no longer fits the input at all: a new note played legato (no
     * attack in the level). Treat it as an attack, and hush the voice until the new note is
     * found instead of singing the old one over it (fast arps). */
    s->gone = 0;
    if (s->hold > 0 && ln >= 2 && ln < CH_MAXLAG && cn > 1e-20f
        && ch_dip(s->dm[ln - 1], s->dm[ln], s->dm[ln + 1]) * (float)ln > CH_GONE * cn) {
        s->gone = 1;
        if (s->onset < CH_STABLE) s->onset = CH_STABLE + CH_STABLE;
    }
    /* odd harmonics of the note being sung: x(t) - x(t + T/2) keeps only them. A real jump
     * an octave up removes them at once; two beating oscillators fade them out slowly */
    lh = (ln + 1) >> 1;
    if (lh >= 1 && lh <= CH_MAXLAG && cum > 1e-20f) {
        h = s->dm[lh] * (float)CH_MAXLAG * ch_recip(cum);
        s->rf += 0.06f * (h - s->rf);
    }
    s->rs = (s->rf > s->rs || s->onset > 0) ? s->rf : s->rs * 0.997f;   /* peak, falls over ~60 ms; */
    if (s->age >= CH_YOUNG && s->rs > CH_ODD_MIN && s->rf < 0.35f * s->rs) s->odrop = CH_ONSET;   /* not the attack's */
    else if (s->odrop > 0) s->odrop--;
    if (best > 0) {
        float y0, y1, y2, den, off, x, z;
        y0 = s->dm[best - 1]; y1 = s->dm[best]; y2 = s->dm[best + 1];
        den = y0 - y1 - y1 + y2;
        /* fine position of the bottom: a sine's difference function is a cosine of the lag,
         * so off = atan(r tan(pi/T)) T / (2 pi), r = (y0 - y2) / den (a parabola for long lags) */
        off = (den > 1e-20f) ? (y0 - y2) * ch_recip(den) : 0.0f;
        if (off > 1.0f) off = 1.0f;
        if (off < -1.0f) off = -1.0f;
        x = 3.1415927f * ch_recip((float)best);
        z = x * x;
        z = off * x * (1.0f + z * (0.33333334f + z * (0.13333334f + z * 0.053968254f)));   /* r tan(pi/T) */
        x = z * z;
        off = z * (0.9998222f + x * (-0.32898255f + x * (0.17032765f + x * -0.05977583f)))
            * (float)best * 0.15915494f;                                                /* atan(.) T / 2 pi */
        if (off > 0.5f) off = 0.5f;
        if (off < -0.5f) off = -0.5f;
        tgt = CH_LOG2_FD - ch_log2((float)best + off);  /* octaves (log2 Hz) */
        h = tgt - s->cand;
        if (h < 0.0333f && h > -0.0333f) { if (s->stable < CH_STABLE_OCT) s->stable++; }
        else s->stable = 0;
        s->cand = tgt;
        /* A change of note while singing, with no new attack in the input. When the input
         * is a real new note, the old note's period no longer fits it. When it still fits as
         * well as the new reading, the reading is a harmonic of the same note: detuned
         * oscillators that beat or start out of phase cancel the fundamental for a while
         * (an octave or a fifth up, or the octave below when the next dip wins). Those wait
         * 3 s. An octave up is the one real jump the old period still fits; it is taken
         * fast when the odd harmonics fell at once (odrop, not in the 0.2 s after an attack),
         * which beating never does. Above ~610 Hz (lag under 9) the dips are too coarse to
         * compare and beating is fast: a reading up there waits 0.3 s, and from a note up
         * there an octave jump waits 0.3 s (up: unless odrop). */
        h = tgt - s->note;
        n = CH_STABLE;
        if ((s->hold > 0 || s->kept) && s->onset <= 0 && (h > 0.0333f || h < -0.0333f)) {
            if (ln >= 9 && ln < CH_MAXLAG
                && ch_dip(s->dm[ln - 1], s->dm[ln], s->dm[ln + 1]) * (float)ln * ch_recip(cn) < qb + CH_FITS)
                n = (h > 0.95f && s->odrop > 0) ? CH_STABLE        /* octave, 2 octaves up */
                  : (best < 9) ? CH_STABLE_HI : CH_STABLE_OCT;
            else if (ln < 9 && ((h > 0.95f && h < 1.05f) || (h < -0.95f && h > -1.05f)))
                n = (h > 0.0f && s->odrop > 0) ? CH_STABLE : CH_STABLE_HI;   /* above ~610 Hz */
        }
        if (s->stable >= n) {
            if (s->hold <= 0 || s->vamp < 1e-5f) { s->note = tgt; s->base = tgt; s->vibon = 0.0f; }
            else s->note = tgt;
            s->hold = CH_HOLD; s->lost = 0; s->lref = s->e2;
        }
    } else {
        s->stable = 0;
    }
    /* no good reading: keep singing the note while the input stays loud (up to 0.4 s, e.g.
     * two beating oscillators that lose their pitch for a moment); once it fades, 60 ms more */
    if (s->lost < CH_LOST) s->lost++;
    if (s->hold > 0 && (s->e2 < 0.25f * s->lref || s->lost >= CH_LOST)) s->hold--;
    /* a note that ran out of readings while the input stayed loud (a long beat null, a
     * chord) still counts for the rule above: no new attack and no fade since */
    if (s->hold > 0) s->kept = 1;
    else if (s->e2 < 0.25f * s->lref) s->kept = 0;

    /* ---- voice level: the input's loudness while there is a note, with Feel's envelope ---- */
    lvl = (s->e2 > 1e-9f) ? s->e2 * ch_rsqrt(s->e2) : 0.0f;
    i = s->gone && !P->legato;                         /* Glide 0: hush the old note */
    tgt = (s->hold > 0 && !i) ? lvl : 0.0f;
    s->vamp += ((tgt > s->vamp) ? P->att : i ? 0.03f : P->rel) * (tgt - s->vamp);   /* hush: ~6 ms */
    if (s->vamp < 1e-9f) s->vamp = 0.0f;
    s->base += P->glide_c * (s->note - s->base);
    s->vibon += 0.0007f * (1.0f - s->vibon);           /* vibrato fades in over ~0.3 s */

    /* ---- singers: pitch, level and section, once per block ---- */
    cw   = choir_word(P->choir);
    nsec = (int)((cw >> 12) & 7u);
    nrm  = ch_rsqrt((float)P->size);
    j = 0;
    SR_NOUNROLL
    for (i = 0; i < CH_NS; i++) {
        float det, oc, vib, hz, sp;
        int   q = (int)((cw >> (j + j)) & 3u);
        j++; if (j >= nsec) j = 0;
        sec[i] = (i < P->size) ? q : -1;
        if (i >= P->size) { s->amp[i] = 0.0f; continue; }
        sp  = 0.5f + 0.1f * (float)P->size;          /* a bigger choir spreads wider */
        det = (i == 0) ? 0.0f : (i == 1) ? 8.0f : (i == 2) ? -7.0f : (i == 3) ? 12.0f : (i == 4) ? -11.0f : 4.0f;
        s->drift[i] = s->drift[i] * 0.9995f + (rnd01(s) - 0.5f) * 0.004f;
        oc  = (float)(q - 1) + ((P->chord == 1 && (i & 1)) ? 1.0f : 0.0f);
        s->vph[i] += ((q == 0) ? 4.6f : (q == 1) ? 5.2f : (q == 2) ? 5.6f : 6.0f) * (1.0f + 0.03f * (float)i) * CH_BLK_HZ;
        if (s->vph[i] >= 1.0f) s->vph[i] -= 1.0f;
        vib = P->vibc * s->vibon * sine_of(s->vph[i]);
        w   = s->base + oc + (det * sp + 3.0f * s->drift[i]) * 0.00083333f + vib;
        if (w > 12.1f) w = 12.1f;                    /* 4.4 kHz */
        hz  = ch_exp2(w);
        inc[i]  = hz * CH_INC_HZ;
        iinc[i] = ch_recip(inc[i]);
        sg[i] = ch_exp2(CH_TILT * (7.78136f - w));      /* equal loudness over the range */
        ka[i] = (i == 0) ? 1.0f : 0.25f - 0.03f * (float)i;   /* the extra singers come in a little late */
        s->amp[i] += ka[i] * (s->vamp - s->amp[i]);
    }

    /* ---- vowel: blend of two vowels, glided ---- */
    {
        float u = 0.0f;
        if (P->sing == 2) {                          /* OOAH: oo -> ah -> oo over one cycle */
            u = 0.5f - 0.5f * sine_of(s->lfo_ph + 0.25f);
            u = u + P->feel * (u * u * (3.0f - u - u) - u);   /* punchy: snaps between them */
        }
        if (s->wsm < 0.0f) s->wsm = u;
        s->wsm += 0.02f * (u - s->wsm);
        wv = s->wsm;
    }

    /* ---- formants, once per block for the sections that sing ---- */
    SR_NOUNROLL
    for (b = 0; b < 4; b++) {
        float th, wd, f0, lo;
        act[b] = 0; ns[b] = 0.0f;
        SR_NOUNROLL
        for (i = 0; i < P->size; i++) if (sec[i] == b) { act[b]++; ns[b] += sg[i] * s->amp[i]; }
        if (!act[b]) {
            SR_NOUNROLL
            for (j = 0; j < 3; j++) { s->ic1[b * 3 + j] = 0.0f; s->ic2[b * 3 + j] = 0.0f; }
            s->tilt[b] = 0.0f; s->nlp[b] = 0.0f;
            continue;
        }
        /* breath: follows the singers' own level (pitch scaling included), so it sits at the
         * same depth under the voice on every note; summed singers grow by sqrt(count) */
        ns[b] *= P->breath * ch_rsqrt((float)act[b]) * ((b == 0) ? 0.6f : (b == 1) ? 0.8f : (b == 2) ? 1.0f : 1.1f);
        nc[b] = 2.0f * 6.2831853f * CH_INC_HZ * ch_exp2(s->base + (float)(b - 1));   /* 2 x f0 */
        if (nc[b] > 0.9f) nc[b] = 0.9f;
        ns[b] *= ch_rsqrt(nc[b]);                    /* the low-pass takes ~sqrt(nc) of the power */
        th = (b == 0) ? 0.78f : (b == 1) ? 1.0f : (b == 2) ? 1.17f : 1.35f;
        wd = (b == 0) ? 0.8f : (b == 1) ? 1.0f : (b == 2) ? 1.15f : 1.3f;
        tl[b] = P->bright * ((b == 0) ? 0.3f : (b == 1) ? 0.4f : (b == 2) ? 0.5f : 0.55f);
        f0 = ch_exp2(s->base + (float)(b - 1));          /* the section's fundamental */
        lo = 1.1f * f0;
        SR_NOUNROLL
        for (j = 0; j < 3; j++) {
            int   va = (P->sing == 0) ? 0 : 4;       /* AAH = A; OOH and OOAH start on U */
            int   vb = (P->sing == 1) ? 4 : 0;       /* OOAH ends on A                    */
            float fa = vowel_hz(j, va), fb = vowel_hz(j, vb);
            float ga = vowel_amp(j, va), gb = vowel_amp(j, vb);
            float hz = (fa + wv * (fb - fa)) * th, kk, g, t2, aa;
            float bw = ((j == 0) ? 70.0f : (j == 1) ? 90.0f : 120.0f) * wd;
            /* a high voice lifts its formants above its note and opens them up, as a soprano
             * does: otherwise the bands fall between its few harmonics and the voice fades */
            if (hz < lo) hz = lo;
            lo = 1.4f * hz;
            if (bw < 0.5f * f0) bw = 0.5f * f0;
            if (hz > 5000.0f) hz = 5000.0f;
            kk = bw * ch_recip(hz);
            g  = hz * CH_PI_FS;
            t2 = g * g;
            g  = g * (1.0f + t2 * (0.33333334f + t2 * (0.13333334f + t2 * 0.053968254f)));
            aa = ch_recip(1.0f + g * (g + kk));
            a1[b * 3 + j] = aa;
            a2[b * 3 + j] = g * aa;
            a3[b * 3 + j] = g * g * aa;
            gk[b * 3 + j] = kk * (ga + wv * (gb - ga)) * hz * 0.002f * CH_LEVEL * nrm;
        }
    }

    /* ---- the sample loop ---- */
    SR_NOUNROLL
    for (n = 0; n < 8; n++) {
        float in = buf[n], wet = 0.0f, sum[4], nz;
        sum[0] = 0.0f; sum[1] = 0.0f; sum[2] = 0.0f; sum[3] = 0.0f;
        s->rng = s->rng * 1664525u + 1013904223u;
        nz = (float)(int)(s->rng >> 9) * 2.3841858e-7f - 1.0f;   /* white noise -1..1 */
        SR_NOUNROLL
        for (i = 0; i < CH_NS; i++) {
            float p, sw, t;
            if (sec[i] < 0) continue;
            p = s->sph[i] + inc[i];
            if (p >= 1.0f) p -= 1.0f;
            s->sph[i] = p;
            sw = p + p - 1.0f;                       /* saw, minus PolyBLEP at the wrap */
            if (p < inc[i]) { t = p * iinc[i]; sw -= t + t - t * t - 1.0f; }
            else if (p > 1.0f - inc[i]) { t = (p - 1.0f) * iinc[i]; sw -= t * t + t + t + 1.0f; }
            sum[sec[i]] += sw * sg[i] * s->amp[i];
        }
        SR_NOUNROLL
        for (b = 0; b < 4; b++) {
            float u;
            if (!act[b]) continue;
            s->nlp[b] += nc[b] * (nz - s->nlp[b]);    /* noise falling like a saw above 2 f0 */
            u = sum[b] + s->nlp[b] * ns[b];
            s->tilt[b] += tl[b] * (u - s->tilt[b]);  /* darker for the big singers */
            u = s->tilt[b];
            SR_NOUNROLL
            for (j = 0; j < 3; j++) {
                int   q = b * 3 + j;
                float v3 = u - s->ic2[q];
                float v1 = a1[q] * s->ic1[q] + a2[q] * v3;
                float v2 = s->ic2[q] + a2[q] * s->ic1[q] + a3[q] * v3;
                s->ic1[q] = v1 + v1 - s->ic1[q];
                s->ic2[q] = v2 + v2 - s->ic2[q];
                wet += gk[q] * v1;
            }
        }
        pw += wet * wet;
        wet *= s->gn;
        if (wet > 1.5f) wet = 1.0f;                    /* soft ceiling at +-1 (cubic) */
        else if (wet < -1.5f) wet = -1.0f;
        else wet = wet - 0.14814815f * wet * wet * wet;
        buf[n] = P->dryG * in + P->wetG * wet;
    }
    /* Voice gain: how loud the formants make our own saw at this pitch and vowel, measured as
     * choir power over the power the singers were asked for. Both follow the input's envelope
     * together, so the ratio moves only with pitch and vowel (no pumping on the synth's
     * dynamics). ~50 ms, held while the choir is silent, limited to +-14 dB. */
    pa = 8.0f * s->vamp * s->vamp;
    if (pa > 1e-10f) {
        s->pw += 0.0036f * (pw - s->pw);
        s->pa += 0.0036f * (pa - s->pa);
        if (s->pw > 1e-20f) {
            h = ch_rsqrt(s->pw * ch_recip(s->pa));
            if (h > 5.0f) h = 5.0f;
            if (h < 0.2f) h = 0.2f;
            s->gn += 0.01f * (h - s->gn);
        }
    }
    SR_NOUNROLL
    for (j = 0; j < 12; j++) {                         /* denormals */
        if (s->ic1[j] < 1e-20f && s->ic1[j] > -1e-20f) s->ic1[j] = 0.0f;
        if (s->ic2[j] < 1e-20f && s->ic2[j] > -1e-20f) s->ic2[j] = 0.0f;
    }
}

/* ---- on-screen text (knob index = ZDL_GetLabel_<index>) ------------------ */
/* Upper-case words packed 6 bits per letter, first letter in the low bits, letter = value
 * + 32 (so digits, '+', '/', '.' and A..Z), 0 = end. */
SR_ALWAYS_INLINE(ch_word_text)
static inline int ch_word_text(unsigned int w, char *out)
{
    int len;
    SR_NOUNROLL
    for (len = 0; w != 0u; len++) {
        out[len] = (char)((int)(w & 63u) + 32);
        w >>= 6;
    }
    out[len] = 0;
    return len;
}

/* Pace words, packed as in the rest of the pack: 6 bits per letter, 1..26 = a..z,
 * 27..62 = '#'..'F', 63 = 'T' */
SR_ALWAYS_INLINE(fm_word_text)
static inline int fm_word_text(unsigned int w, char *out)
{
    int len, v;
    SR_NOUNROLL
    for (len = 0; w != 0u; len++) {
        v = (int)(w & 63u);
        out[len] = (v == 63) ? 'T' : (char)((v > 26) ? v + 8 : v + 96);
        w >>= 6;
    }
    out[len] = 0;
    return len;
}

/* 0 Choir */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    int n = (int)value;
    if (n > 8) n = 8;
    return ch_word_text(
          (n == 0) ? 0x2E96Du      /* MEN   */
        : (n == 1) ? 0x2E96DBF7u   /* WOMEN */
        : (n == 2) ? 0xCE4A6Bu     /* KIDS  */
        : (n == 3) ? 0x34BA1A67u   /* GIANT */
        : (n == 4) ? 0x372EDu      /* M+W   */
        : (n == 5) ? 0x24A6B2EDu   /* M+KID */
        : (n == 6) ? 0x24A6B2F7u   /* W+KID */
        : (n == 7) ? 0x2D2E7u      /* G+M   */
        : 0x2CB21u                 /* ALL   */
        , out);
}

/* 1 Size */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int n = (int)value;
    if (n > 5) n = 5;
    return ch_word_text(
          (n == 0) ? 0xBECBF3u     /* SOLO  */
        : (n == 1) ? 0x2FD64u      /* DUO   */
        : (n == 2) ? 0xBE9CB4u     /* TRIO  */
        : (n == 3) ? 0x34CA1D71u   /* QUART */
        : (n == 4) ? 0x34BA9D71u   /* QUINT */
        : 0xD38973u                /* SEXT  */
        , out);
}

/* 2 Chord */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    return ch_word_text((value == 0u) ? 0xCE9BB5u /* UNIS */ : 0x348EFu /* OCT */, out);
}

/* 3 Sing */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    return ch_word_text((value == 0u) ? 0x28861u      /* AAH  */
                      : (value == 1u) ? 0x28BEFu      /* OOH  */
                      : 0xA21BEFu                     /* OOAH */
                      , out);
}

/* 4 Pace: 4bar 3bar 2bar 1.5b 1bar 1/2. 1/2 1/4. 1/4 1/8. 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64 */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    int n = (int)value;
    if (n > 16) n = 16;
    return fm_word_text(
          (n == 0) ? 0x004810ACu  /* 4bar */
        : (n == 1) ? 0x004810ABu  /* 3bar */
        : (n == 2) ? 0x004810AAu  /* 2bar */
        : (n == 3) ? 0x000AD9A9u  /* 1.5b */
        : (n == 4) ? 0x004810A9u  /* 1bar */
        : (n == 5) ? 0x009AA9E9u  /* 1/2. */
        : (n == 6) ? 0x0002A9E9u  /* 1/2 */
        : (n == 7) ? 0x009AC9E9u  /* 1/4. */
        : (n == 8) ? 0x0002C9E9u  /* 1/4 */
        : (n == 9) ? 0x009B09E9u  /* 1/8. */
        : (n == 10) ? 0x000309E9u  /* 1/8 */
        : (n == 11) ? 0x00FF09E9u  /* 1/8T */
        : (n == 12) ? 0x00BA99E9u  /* 1/16 */
        : (n == 13) ? 0x3FBA99E9u  /* 1/16T */
        : (n == 14) ? 0x00AAB9E9u  /* 1/32 */
        : (n == 15) ? 0x3FAAB9E9u  /* 1/32T */
        : 0x00B2E9E9u  /* 1/64 */
        , out);
}

/* 7 Tempo: FOLLW on 0..39, else the BPM "40" .. "240" (twin copy 241..441 shows it too) */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value <= 39u) return dt_follow_text(out);
    if (value > 441u) value = 441u;
    n = ch_tempo_bpm((int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}


/* ---- pedal entry point (same structure as the rest of the pack) ---------- */
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
    float *dryBuf = ZDL_PTR(float *, ctx[4]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    ChState *s;
    ChParams P;
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
    if ((end - base) < sizeof(ChState) || span < (end - base)) return;
    if (stateBase + sizeof(ChState) > end) return;

    s = (ChState *)stateBase;

    k[0] = sr_knob(params[FORMANT_CHOIR_SLOT], (float)FORMANT_CHOIR_UI_DEFAULT, 0.125f);
    k[1] = sr_knob(params[FORMANT_SIZE_SLOT],  (float)FORMANT_SIZE_UI_DEFAULT,  0.2f);
    k[2] = sr_knob(params[FORMANT_CHORD_SLOT], (float)FORMANT_CHORD_UI_DEFAULT, 1.0f);
    k[3] = sr_knob(params[FORMANT_SING_SLOT],  (float)FORMANT_SING_UI_DEFAULT,  0.5f);
    k[4] = sr_knob(params[FORMANT_PACE_SLOT],  (float)FORMANT_PACE_UI_DEFAULT,  0.0625f);
    k[5] = sr_knob(params[FORMANT_FEEL_SLOT],  (float)FORMANT_FEEL_UI_DEFAULT,  0.01f);
    k[6] = sr_knob(params[FORMANT_GLIDE_SLOT], (float)FORMANT_GLIDE_UI_DEFAULT, 0.01f);
    k[8] = sr_knob(params[FORMANT_MIX_SLOT],   (float)FORMANT_MIX_UI_DEFAULT,   0.01f);

    if (s->magic != CH_MAGIC) ch_init(s);
    /* bar tag: bars from earlier slots flip the Tempo copy too, FOLLOW takes their BPM */
    k[7] = dt_tempo(&s->sync, dryBuf ? dryBuf + 8 : 0,
                    ch_tempo_ui(params[FORMANT_TEMPO_SLOT], (float)FORMANT_TEMPO_UI_DEFAULT),
                    dt_id(stateBase)) * 0.0022675737f;   /* 1/441 */
    ch_prepare(s, &P, k);                        /* a Tempo flip is a downbeat */
    ch_tick(s, &P);
    if (params[0] < 0.5f) {                      /* switched off: input untouched, the */
        s->was_off = 1;                          /* movement keeps time with the bar   */
        return;
    }
    if (s->was_off) { ch_clear(s); s->was_off = 0; }
    ch_process(s, &P, fxBuf);                    /* mono: left half in place */

    SR_NOUNROLL
    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R */
}

#endif /* FORMANT_HOST_TEST */
