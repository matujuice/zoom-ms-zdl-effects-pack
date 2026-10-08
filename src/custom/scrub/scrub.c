/*
 * scrub.c - "Scrub": scrub through the recent past, freeze the grain where you stop, mono
 *
 * Everything that goes in is recorded into a 7.9 second buffer. A read head sits somewhere
 * in that buffer and plays a short GRAIN from there, over and over, each repeat
 * crossfading into the next so the loop has no seam. Position moves the head along the
 * last 6 seconds like a timeline, in 10 ms steps: 0 = 6 s ago, 600 = now. Turning Position
 * slides the head and the grains follow it through the audio, always at the original
 * pitch (as in Clouds: no tape bend); stop turning and the grain under the head keeps
 * looping: a freeze.
 *
 * REC (what the buffer does)
 *   LIVE   it keeps recording. Position is how far back the head reads, so the head moves
 *          with the music: a delay you can sweep (the pitch never bends), and with Spray a
 *          granular cloud of the last few seconds.
 *   HOLD   recording stops: the last 7.9 seconds are kept and Position scrubs through them.
 *          Stop on a note and it rings forever. Back to LIVE and recording carries on.
 *   STOMP  the footswitch does it: while the effect is OFF it records (you hear your
 *          sound untouched), the moment you switch it ON the buffer freezes and you
 *          scrub what you just played. Untested: it needs the pedal to send the sound
 *          through a switched-off effect, which the stock delays rely on but has not been
 *          checked on this pedal. If ON only gives silence, use LIVE and HOLD.
 *
 * THE HEAD
 *   The head's distance from "now" is D = (600 - Position) x 10 ms. D glides toward the
 *   knob with a one-pole (Glide), once per 8-sample block, which also hides the knob's
 *   10 ms steps. Each grain starts 1.25 x Grain + D (+- Spray jitter) back and reads forward
 *   at normal speed, so the pitch stays put: a move of the head is heard as each new grain
 *   starts where the head now is. (An earlier version also pushed the running grains along
 *   with the head, which bent the pitch with the turning speed like tape; Luca preferred
 *   constant pitch after hearing both, 2026-10-03.)
 *   In LIVE the age of what a grain reads stays fixed while D does not move (it is a
 *   delay of 1.25 x Grain + D); in HOLD it drops by one per sample (the grain plays forward
 *   through frozen audio) and every new grain jumps back to the head.
 *
 * GRAINS (a crossfaded loop, as in a sampler, not a 50 % overlap cloud)
 *   A phase p runs 0..1 once per Grain. When it wraps, a new voice starts at the head and
 *   the voice that was playing becomes the old one: for the first quarter of the cycle
 *   the new voice fades in while the old one fades out (raised cosine, the two gains add
 *   up to 1), then the new one plays alone. So a voice lives 1.25 x Grain and the loop
 *   is heard as one piece with a short seam, instead of two copies always overlapping
 *   (which on a held tone beats at the overlap whenever the copies are out of phase).
 *   The fade is sin(pi q)^2 with sin(pi q) from a small polynomial in q(1-q) (max error
 *   0.1 %). Reads use linear interpolation (D, and so the ages, are fractional).
 *   Spray moves each new grain a random amount either side of the head (up to +-250 ms,
 *   never ahead of now; Luca asked for a +-ms readout, 2026-10-03), which turns a static
 *   loop into a moving cloud and breaks the buzz of very short frozen grains.
 *
 * DIR (which way each voice plays)
 *   FWD   every voice plays forward: it starts 1.25 x Grain + D back and ends at D.
 *   REV   every voice plays backward: it starts 0.25 x Grain + D back and ends at
 *         1.25 x Grain + D, the same slice turned round.
 *   PING  the voices take turns: forward, backward, forward... A backward voice starts
 *         exactly where the forward one is at the seam (and the other way round), so
 *         with a still head each turn begins on the very sample the last voice was
 *         playing: no jump at the seam, a smoother, more tonal loop. Its crossfade is
 *         only 1 ms (a longer one would mix the turn with its own mirror image, which
 *         cancels on held tones); a moving head or Spray can still make a small jump.
 *   RAND  each new voice flips a coin: forward or backward. Where it turns round it gets
 *         PING's 1 ms seam, where it keeps going the usual quarter-grain one
 *         (Luca, 2026-10-03).
 *   A backward voice reads older audio by one sample per sample (by two in LIVE, where
 *   the buffer moves on under it).
 *
 * MEMORY
 *   The buffer is 16-bit (scale 16384, so +-2.0 fits), 7.9 s = 348000 samples = 696 KB of
 *   the at least 705 KB arena (Luca asked for the most the arena holds, 2026-10-03; it was
 *   6 s with Pos reaching 4 s). It is cleared 2048 samples per block after loading (about
 *   31 ms, the effect is dry meanwhile), so stale arena data is never played.
 *   A voice reads back to D + Spray plus 1.25 x Grain (forward), 1.5 x Grain (backward in
 *   HOLD) or 2.75 x Grain (backward in LIVE, where the buffer moves on under it). The
 *   free range fits with the head 6 s back forward or in HOLD (1.25 x 1 s + 6 s + 0.25 s
 *   = 7.5 s); backward in LIVE a grain over about 0.6 s pulls the head in a little; a long
 *   synced grain pulls the head in, and one that cannot fit even at now is shortened.
 *   No read reaches the end of the 7.9 s. The 1.9 s beyond Pos's 6 s are this headroom.
 *
 * KNOBS (screen values)
 *   0 Pos    0..600  where the head reads, 10 ms per step: 0 = 6 s ago, 600 = now (in HOLD
 *                    the freeze moment); shown as the time back, "0ms", "990ms", "6.00s".
 *                    600 steps instead of 100 so no Range knob is needed (Luca, 2026-10-03).
 *                    Read as raw x 100 always (the pedal passes screen / 100, so 4.00 here
 *                    means 400; the 3.05 guess in sc_ui would read it as 4)
 *   1 Grain  0..112  0..100 = grain length, 10 ms .. 1 s (log), shown in ms / s;
 *                    101..112 = a note value at the pedal's tempo (Luca, 2026-10-03): 1/64
 *                    1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2, so the freeze loops
 *                    in time. Grain carries the tap flag (manifest flags 40) so the pedal
 *                    shows its tap button.
 *                    A synced grain can reach 4 s (1/2 at 30 BPM); then the head cannot go
 *                    as far back as Pos asks (it stops where the grain still fits),
 *                    and a backward one in LIVE may be shortened (see MEMORY)
 *   2 Rec    0..2    LIVE / HOLD / STOMP (see REC)
 *   3 Glide  0..100  how slowly the head follows Position: 0 = jumps, 40 = about 0.1 s,
 *                    70 = about 1 s, 100 = about 3 s
 *   4 Dir    0..3    FWD / REV / PING / RAND: which way the grain plays (see DIR)
 *   5 Spray  0..100  random offset of each grain either side of the head, up to +-250 ms
 *                    (squared curve); shown as "OFF", "+-1" .. "+-250" (ms; 5 characters)
 *   6 Mix    0..100  dry/wet crossfade, DJ style: dry full up to 50, wet full from 50,
 *                    both full at 50
 *
 * TEMPO AND MIDI TRANSPORT (MOD firmware 0.4 and later; src/custom/common/zmt.h)
 *   No Tempo knob: the BPM is the pedal's (MIDI clock, tap or patch TEMPO), read from the
 *   MOD firmware's transport block; 120 BPM without it. Only a synced Grain uses it. While
 *   the MIDI transport runs, a synced grain's phase p is placed on the clock count (beats
 *   since Start / the note value), and a wrap of p starts a new grain as at the end of any
 *   cycle, so the loop stays on the grid; MIDI Start starts a new grain at once (crossfaded
 *   like any seam) on the downbeat. After Stop it runs on at the last tempo. Free grains
 *   ignore the clock. While the effect is switched off the grains do not run (they start
 *   afresh when it is switched on, then land on the clock). A synced grain shortened to fit
 *   the buffer (see MEMORY) runs free.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline, no calls. 2^x is built from a polynomial and the float exponent.
 */

#include <stdint.h>
#include "../common/zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define SC_DO_PRAGMA(x) _Pragma(#x)
#define SC_EXPAND_PRAGMA(x) SC_DO_PRAGMA(x)
#define SC_ALWAYS_INLINE(fn) SC_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SC_CODE_SECTION(fn) SC_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SC_ALWAYS_INLINE(fn)
#define SC_CODE_SECTION(fn)
#endif

#define SC_MAGIC      0x53435237u        /* "SCR7": change whenever ScState changes */
#define SC_N          348000             /* buffer length: 7.9 s at 44.1 kHz            */
#define SC_AGE_MAX    347997.0f          /* oldest age a read may use (SC_N - 3)        */
#define SC_POS_MAX    600                /* Pos steps: 10 ms each, 6 s                  */
#define SC_CLEAR_BLK  2048               /* samples cleared per block after loading     */
#define SC_TO16       16384.0f
#define SC_FROM16     6.1035156e-5f      /* 1 / 16384 */
#define SC_FIN_STEP   0.0022675737f      /* wet fade-in after switching on: 10 ms       */
#define SC_XF_SCALE   2.0f               /* fade over the first 1/4 cycle: q = 2p, 0..0.5 */

#define SC_MODE_LIVE  0
#define SC_MODE_HOLD  1
#define SC_MODE_STOMP 2

#define SC_DIR_FWD    0
#define SC_DIR_REV    1
#define SC_DIR_PING   2
#define SC_DIR_RAND   3

typedef struct {
    unsigned int magic;
    unsigned int rng;      /* LCG for Spray                                   */
    int   clr;             /* samples of the buffer cleared so far            */
    int   wp;              /* next write index; age a = buf[wp - a]           */
    int   started;         /* 0 until the first block set the head            */
    int   was_off;         /* the effect was switched off (for STOMP, fade-in) */
    float D;               /* smoothed head distance from now, samples        */
    float p;               /* grain phase 0..1                                */
    float aC, aO;          /* ages read by the current and the old voice      */
    int   bC, bO;          /* 1 = that voice plays backward                   */
    float fin;             /* wet fade-in 0..1                                */
    ZtSync zt;             /* MIDI transport (zmt.h)                          */
    short buf[SC_N];
} ScState;

typedef struct {
    float inc;             /* grain phase increment per sample (1 / length)   */
    float mult;            /* synced grain: cycles per beat; 0 = free (no clock lock) */
    float len;             /* grain length, samples                           */
    float Dt;              /* head distance target, samples                   */
    float Dmax;            /* furthest the head may be so every read fits     */
    float c;               /* Glide: one-pole coefficient per block          */
    float spray;           /* Spray: grains land up to this far either side   */
    float dryG, wetG;      /* Mix: DJ crossfade gains                         */
    int   mode;            /* SC_MODE_*                                       */
    int   dir;             /* SC_DIR_*                                        */
    float xf;              /* seam: crossfade over the first 1/(2 xf) of a cycle */
    float xft;             /* the same where the voice turns round: 1 ms         */
} ScParams;

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer. */
SC_ALWAYS_INLINE(sc_ui)
static inline float sc_ui(float raw, float def_ui, float max_ui)
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

/* 2^x for x in about -30..30: the fraction by a polynomial, the whole part in the exponent */
SC_ALWAYS_INLINE(sc_exp2)
static inline float sc_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int   n = (int)x;
    float f, r;
    if ((float)n > x) n--;                       /* floor for negative x */
    f = x - (float)n;
    r = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    c.u = ((unsigned int)(n + 127)) << 23;
    return r * c.f;
}

/* 1 / x for x > 0: a first guess from the float bits, then three Newton steps (no divide) */
SC_ALWAYS_INLINE(sc_recip)
static inline float sc_recip(float x)
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

/* synced Grain (knob 101..112): the grain as a note value, in beats (quarter notes):
 * 1/64 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 */
SC_ALWAYS_INLINE(sc_note_beats)
static inline float sc_note_beats(int d)
{
    return (d == 0) ? 0.0625f : (d == 1) ? 0.125f : (d == 2) ? 0.16666667f : (d == 3) ? 0.25f
         : (d == 4) ? 0.33333334f : (d == 5) ? 0.375f : (d == 6) ? 0.5f : (d == 7) ? 0.6666667f
         : (d == 8) ? 0.75f : (d == 9) ? 1.0f : (d == 10) ? 1.5f : 2.0f;
}

/* Hann window sin(pi p)^2 for p in 0..1, no libm */
SC_ALWAYS_INLINE(sc_hann)
static inline float sc_hann(float p)
{
    float x = p * (1.0f - p);                    /* 0..0.25 */
    float s = x * (3.1f + 3.6f * x);             /* ~ sin(pi p) */
    return s * s;
}

/* buffer sample at a (fractional) age >= 1, linear interpolation */
SC_ALWAYS_INLINE(sc_read)
static inline float sc_read(const short *b, int wp, float age)
{
    int a0 = (int)age, j, j1;
    float fr = age - (float)a0, x0, x1;
    j = wp - a0;
    if (j < 0) j += SC_N;
    j1 = j - 1;
    if (j1 < 0) j1 += SC_N;
    x0 = (float)b[j];
    x1 = (float)b[j1];
    return (x0 + fr * (x1 - x0)) * SC_FROM16;
}

SC_ALWAYS_INLINE(sc_write)
static inline void sc_write(ScState *s, float in)
{
    float v = in * SC_TO16;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    s->buf[s->wp] = (short)(int)v;
    s->wp++;
    if (s->wp >= SC_N) s->wp = 0;
}

/* random number 0..1 */
SC_ALWAYS_INLINE(sc_rand)
static inline float sc_rand(ScState *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(int)(s->rng >> 8) * 5.9604645e-8f;      /* / 2^24 */
}

SC_ALWAYS_INLINE(sc_clamp_age)
static inline float sc_clamp_age(float a)
{
    if (a < 1.0f) a = 1.0f;
    if (a > SC_AGE_MAX) a = SC_AGE_MAX;
    return a;
}

/* Grain 0..100 -> length in samples, 10 ms * 100^(g/100) */
SC_ALWAYS_INLINE(sc_grain_len)
static inline float sc_grain_len(float g)
{
    return 441.0f * sc_exp2(g * 0.06643856f);              /* 6.643856 = log2(100) */
}

/* Pos knob: the pedal passes screen / 100 (0..6.00); always scale by 100, no guessing */
SC_ALWAYS_INLINE(sc_ui_pos)
static inline float sc_ui_pos(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 6.005f)) ui = def_ui;
    else ui = (float)(int)(raw * 100.0f + 0.5f);
    if (ui > (float)SC_POS_MAX) ui = (float)SC_POS_MAX;
    return ui;
}

SC_ALWAYS_INLINE(sc_init)
static inline void sc_init(ScState *s)
{
    s->rng = 0x5C2B1A37u;
    s->clr = 0; s->wp = 0; s->started = 0; s->was_off = 0;
    s->D = 0.0f; s->p = 0.0f; s->aC = 1.0f; s->aO = 1.0f; s->fin = 1.0f;
    s->bC = 0; s->bO = 0;
    zt_init(&s->zt);
    s->magic = SC_MAGIC;
}

/* u[] = screen values in manifest order, u[7] = the pedal's BPM (zt_update) */
SC_ALWAYS_INLINE(sc_prepare)
static inline void sc_prepare(ScParams *P, const float *u)
{
    float k, e, sp, m, bpm, lim, reach, rinv;
    int g = (int)(u[1] + 0.5f);
    P->mode = (int)(u[2] + 0.5f);
    P->dir = (int)(u[4] + 0.5f);
    P->mult = 0.0f;
    if (g > 100) {                               /* synced: the grain is a note at the BPM */
        bpm = u[7];                              /* 30..300 from zt_update */
        P->len = sc_note_beats(g - 101) * 2646000.0f * sc_recip(bpm);   /* 60 x 44100 */
        P->inc = sc_recip(P->len);
        P->mult = sc_recip(sc_note_beats(g - 101));
    } else {
        P->len = sc_grain_len(u[1]);
        P->inc = 0.0022675737f * sc_exp2(-u[1] * 0.06643856f);  /* 1 / len */
    }
    sp = u[5] * 0.01f;
    P->spray = sp * sp * 11025.0f;               /* up to +-250 ms */
    /* How far back (beyond D + Spray) a voice reads during its 1.25 x Grain life: a forward
     * voice 1.25 x Grain; a backward one starts 0.25 x Grain back and ages by 1 per sample
     * in HOLD (1.5 x Grain) or by 2 in LIVE, where the buffer moves on under it (2.75 x
     * Grain). A grain too long for the buffer even with the head at now (only synced ones,
     * e.g. 1/2 below about 55 BPM with REV in LIVE) is shortened to fit; otherwise the
     * head is pulled in. Either way no read ever reaches the end of the buffer. */
    reach = 1.25f; rinv = 0.8f;
    if (P->dir != SC_DIR_FWD) {
        if (P->mode == SC_MODE_LIVE) { reach = 2.75f; rinv = 0.36363637f; }
        else { reach = 1.5f; rinv = 0.6666667f; }
    }
    lim = SC_AGE_MAX - 4.0f - P->spray;
    if (reach * P->len > lim) { P->len = lim * rinv; P->inc = sc_recip(P->len); P->mult = 0.0f; }
    lim -= reach * P->len;
    if (lim < 0.0f) lim = 0.0f;
    P->Dmax = lim;
    P->Dt  = ((float)SC_POS_MAX - u[0]) * 441.0f;           /* 10 ms per step back from now */
    if (P->Dt > lim) P->Dt = lim;
    k = u[3] * 0.01f;
    e = 14.0f * k * (2.0f - k);                  /* 0..14: per-block coefficient 1 .. 2^-14 */
    P->c = sc_exp2(-e);
    /* Two voices playing the same way crossfade over the first quarter of the cycle. Where
     * the new voice turns round (PING always, RAND when the coin flips) it starts on the
     * sample the last voice was playing, so a long crossfade would only mix the sound with
     * its own mirror image (which cancels on held tones): 1 ms is enough there. */
    P->xf = SC_XF_SCALE;
    P->xft = P->len * 0.0113379f;                /* len / (2 x 44.1): 1 ms */
    if (P->xft < SC_XF_SCALE) P->xft = SC_XF_SCALE;
    m = u[6] * 0.01f;
    P->dryG = 2.0f - 2.0f * m; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * m;        if (P->wetG > 1.0f) P->wetG = 1.0f;
}

/* a new voice: picks its direction (Dir), then its start: forward 1.25 x Grain + D back,
 * backward 0.25 x Grain + D back, plus or minus Spray. Sets s->bC, returns the start age. */
SC_ALWAYS_INLINE(sc_new_voice)
static inline float sc_new_voice(ScState *s, const ScParams *P)
{
    int b = 0;
    float off = P->spray * (2.0f * sc_rand(s) - 1.0f);  /* Spray: either side of the head */
    if (off < -s->D) off = -s->D;                /* but never ahead of now        */
    if (P->dir == SC_DIR_REV) b = 1;
    if (P->dir == SC_DIR_PING) b = !s->bC;
    if (P->dir == SC_DIR_RAND) b = (sc_rand(s) >= 0.5f);
    s->bC = b;
    return sc_clamp_age((b ? 0.25f : 1.25f) * P->len + s->D + 1.0f + off);
}

/* clear part of the buffer after loading; returns 1 while still clearing */
SC_ALWAYS_INLINE(sc_clearing)
static inline int sc_clearing(ScState *s)
{
    int i, n = s->clr;
    if (n >= SC_N) return 0;
    for (i = 0; i < SC_CLEAR_BLK && n < SC_N; i++) { s->buf[n] = 0; n++; }
    s->clr = n;
    return 1;
}

/* effect switched off: keep recording (LIVE, STOMP), the sound passes untouched */
SC_ALWAYS_INLINE(sc_bypassed)
static inline void sc_bypassed(ScState *s, int mode, const float *buf, int n)
{
    int i;
    if (mode == SC_MODE_HOLD) return;
    for (i = 0; i < n; i++) sc_write(s, buf[i]);
}

SC_ALWAYS_INLINE(sc_process)
static inline void sc_process(ScState *s, const ScParams *P, float *buf, int n)
{
    int i, rec = (P->mode == SC_MODE_LIVE);
    float daF, daB, p = s->p, aC = s->aC, aO = s->aO, fin = s->fin;

    if (!s->started) { s->D = P->Dt; s->started = 1; s->bC = 0; aC = sc_new_voice(s, P); aO = aC; s->bO = s->bC; }
    s->D += P->c * (P->Dt - s->D);               /* the head glides; new voices start there */
    if (s->D > P->Dmax) s->D = P->Dmax;          /* a longer grain or Spray: pull it in now  */
    daF = rec ? 0.0f : -1.0f;                    /* age change of a forward voice  */
    daB = rec ? 2.0f : 1.0f;                     /* and of a backward one          */

    for (i = 0; i < n; i++) {
        float in = buf[i], q, g, wet;
        if (rec) sc_write(s, in);
        p += P->inc;
        aO = sc_clamp_age(aO + (s->bO ? daB : daF));
        if (p >= 1.0f) { p -= 1.0f; aO = aC; s->bO = s->bC; aC = sc_new_voice(s, P); }
        else aC = sc_clamp_age(aC + (s->bC ? daB : daF));
        q = p * ((s->bC != s->bO) ? P->xft : P->xf);
        wet = sc_read(s->buf, s->wp, aC);
        if (q < 0.5f) {                          /* seam: old voice fades out */
            g = sc_hann(q);
            wet = g * wet + (1.0f - g) * sc_read(s->buf, s->wp, aO);
        }
        fin += SC_FIN_STEP;
        if (fin > 1.0f) fin = 1.0f;
        buf[i] = P->dryG * in + P->wetG * fin * wet;
    }
    s->p = p; s->aC = aC; s->aO = aO; s->fin = fin;
}

/* switched back on: in STOMP the buffer now holds what was played while off; start a
 * voice at the head and fade the wet sound in */
SC_ALWAYS_INLINE(sc_switched_on)
static inline void sc_switched_on(ScState *s, const ScParams *P)
{
    s->D = P->Dt;
    s->p = 0.25f;                                /* past the seam: one voice plays */
    s->aC = sc_new_voice(s, P);
    s->aO = s->aC; s->bO = s->bC;
    s->fin = 0.0f;
}

/* MIDI Start: start a new grain now, exactly as at the end of a cycle (the old voice
 * fades out over the seam), so the loop restarts on the downbeat */
SC_ALWAYS_INLINE(sc_restart)
static inline void sc_restart(ScState *s, const ScParams *P)
{
    s->p = 0.0f;
    s->aO = s->aC; s->bO = s->bC;
    s->aC = sc_new_voice(s, P);
}

/* transport running: a synced grain's phase = frac(beats x cycles per beat) (96 beats hold
 * whole cycles of every note value); a wrap starts a new voice as at the end of a cycle */
SC_ALWAYS_INLINE(sc_lock)
static inline void sc_lock(ScState *s, const ScParams *P, float beats)
{
    float t;
    if (P->mult <= 0.0f) return;                 /* free grain: runs free */
    t = beats * P->mult;
    t -= (float)(int)t;
    if (t < s->p - 0.5f && s->started) { s->aO = s->aC; s->bO = s->bC; s->aC = sc_new_voice(s, P); }
    s->p = t;
}

/* ---- on-screen text ------------------------------------------------------ */
SC_ALWAYS_INLINE(sc_put_int)
static inline int sc_put_int(int n, char *out)
{
    int h = 0, t = 0, len = 0;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    return len;
}

/* a time in ms as at most five characters: "10ms".."999ms", then "1.0s".."9.9s" */
SC_ALWAYS_INLINE(sc_put_time)
static inline int sc_put_time(int ms, char *out)
{
    int len, t, d = 0;
    if (ms < 1000) {
        len = sc_put_int(ms, out);
        out[len] = 'm'; out[len + 1] = 's'; out[len + 2] = 0;
        return len + 2;
    }
    t = (int)((float)ms * 0.01f + 0.5f);          /* tenths of a second */
    while (t >= 10) { t -= 10; d++; }
    out[0] = (char)('0' + d); out[1] = '.'; out[2] = (char)('0' + t); out[3] = 's'; out[4] = 0;
    return 4;
}

/* one note value as text, five characters at most */
SC_ALWAYS_INLINE(sc_note_text)
static inline int sc_note_text(char *out, int c2, int c3, int c4)
{
    int n = 3;
    out[0] = '1'; out[1] = '/'; out[2] = (char)c2;
    if (c3) { out[3] = (char)c3; n = 4; }
    if (c4) { out[4] = (char)c4; n = 5; }
    out[n] = 0;
    return n;
}

/* knob 1 Grain: screen 0..100 -> "10ms".."1.0s", then 101..112 = note values synced to
 * the pedal's tempo: 1/64 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int n;
    if (value > 112u) value = 112u;
    if (value > 100u) {
        n = (int)value - 101;
        if (n == 0)  return sc_note_text(out, '6', '4', 0);
        if (n == 1)  return sc_note_text(out, '3', '2', 0);
        if (n == 2)  return sc_note_text(out, '1', '6', 'T');
        if (n == 3)  return sc_note_text(out, '1', '6', 0);
        if (n == 4)  return sc_note_text(out, '8', 'T', 0);
        if (n == 5)  return sc_note_text(out, '1', '6', '.');
        if (n == 6)  return sc_note_text(out, '8', 0, 0);
        if (n == 7)  return sc_note_text(out, '4', 'T', 0);
        if (n == 8)  return sc_note_text(out, '8', '.', 0);
        if (n == 9)  return sc_note_text(out, '4', 0, 0);
        if (n == 10) return sc_note_text(out, '4', '.', 0);
        return sc_note_text(out, '2', 0, 0);
    }
    return sc_put_time((int)(sc_grain_len((float)(int)value) * 0.022675737f + 0.5f), out);
}

/* knob 2 Rec: 0 "LIVE", 1 "HOLD", 2 "STOMP" */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value >= 2u) {
        out[0] = 'S'; out[1] = 'T'; out[2] = 'O'; out[3] = 'M'; out[4] = 'P'; out[5] = 0;
        return 5;
    }
    if (value == 1u) { out[0] = 'H'; out[1] = 'O'; out[2] = 'L'; out[3] = 'D'; out[4] = 0; return 4; }
    out[0] = 'L'; out[1] = 'I'; out[2] = 'V'; out[3] = 'E'; out[4] = 0;
    return 4;
}

/* knob 0 Pos: screen 0..600 -> how far back the head is: "6.00s".."1.00s", "990ms".."0ms"
 * (0ms, not NOW: Luca, 2026-10-03) */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    int cs, len, w = 0, t = 0;
    if (value > (unsigned)SC_POS_MAX) value = (unsigned)SC_POS_MAX;
    cs = SC_POS_MAX - (int)value;                       /* hundredths of a second back */
    if (cs < 100) {
        len = sc_put_int(cs * 10, out);
        out[len] = 'm'; out[len + 1] = 's'; out[len + 2] = 0;
        return len + 2;
    }
    while (cs >= 100) { cs -= 100; w++; }
    while (cs >= 10)  { cs -= 10;  t++; }
    out[0] = (char)('0' + w); out[1] = '.'; out[2] = (char)('0' + t); out[3] = (char)('0' + cs);
    out[4] = 's'; out[5] = 0;
    return 5;
}

/* knob 4 Dir: 0 "FWD", 1 "REV", 2 "PING", 3 "RAND" */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value >= 3u) { out[0] = 'R'; out[1] = 'A'; out[2] = 'N'; out[3] = 'D'; out[4] = 0; return 4; }
    if (value >= 2u) { out[0] = 'P'; out[1] = 'I'; out[2] = 'N'; out[3] = 'G'; out[4] = 0; return 4; }
    if (value == 1u) { out[0] = 'R'; out[1] = 'E'; out[2] = 'V'; out[3] = 0; return 3; }
    out[0] = 'F'; out[1] = 'W'; out[2] = 'D'; out[3] = 0;
    return 3;
}

/* knob 5 Spray: 0 "OFF", else how far either side of the head a grain may land, in ms:
 * "+-1" .. "+-250" (the unit does not fit in 5 characters) */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    int ms;
    if (value > 100u) value = 100u;
    if (value == 0u) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return 3; }
    ms = (int)((float)(int)(value * value) * 0.025f + 0.5f);     /* 250 x (v/100)^2 */
    if (ms < 1) ms = 1;
    out[0] = '+'; out[1] = '-';
    ms = 2 + sc_put_int(ms, out + 2);
    out[ms] = 0;
    return ms;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef SCRUB_HOST_TEST

#include "scrub_params.h"

#ifndef SCRUB_AUDIO_FUNC
#define SCRUB_AUDIO_FUNC Fx_DLY_Scrub
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SC_CODE_SECTION(SCRUB_AUDIO_FUNC)
void SCRUB_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    ScState *s;
    ScParams P;
    float u[8], beats;
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
    if ((end - base) < sizeof(ScState) || span < (end - base)) return;
    if (stateBase + sizeof(ScState) > end) return;

    s = (ScState *)stateBase;

    u[0] = sc_ui_pos(params[SCRUB_POS_SLOT], (float)SCRUB_POS_UI_DEFAULT);
    u[1] = sc_ui(params[SCRUB_GRAIN_SLOT],  (float)SCRUB_GRAIN_UI_DEFAULT,  112.0f);
    u[2] = sc_ui(params[SCRUB_REC_SLOT],    (float)SCRUB_REC_UI_DEFAULT,    2.0f);
    u[3] = sc_ui(params[SCRUB_GLIDE_SLOT], (float)SCRUB_GLIDE_UI_DEFAULT, 100.0f);
    u[4] = sc_ui(params[SCRUB_DIR_SLOT],    (float)SCRUB_DIR_UI_DEFAULT,    3.0f);
    u[5] = sc_ui(params[SCRUB_SPRAY_SLOT],  (float)SCRUB_SPRAY_UI_DEFAULT,  100.0f);
    u[6] = sc_ui(params[SCRUB_MIX_SLOT],    (float)SCRUB_MIX_UI_DEFAULT,    100.0f);

    if (s->magic != SC_MAGIC) sc_init(s);
    u[7] = zt_update(&s->zt, &run, &beats, &start);   /* the pedal's BPM, MIDI transport */
    sc_prepare(&P, u);
    if (sc_clearing(s)) {                        /* first ~31 ms after loading: dry */
        if (run) sc_lock(s, &P, beats);          /* but a mid-song load lands on the clock */
        return;
    }
    if (params[0] < 0.5f) {                      /* effect switched off */
        s->was_off = 1;
        sc_bypassed(s, P.mode, fxBuf, 8);
        return;
    }
    if (s->was_off) { sc_switched_on(s, &P); s->was_off = 0; }
    else if (start && s->started) sc_restart(s, &P);  /* MIDI Start: new grain now */
    if (run) sc_lock(s, &P, beats);              /* on the clock (synced grain only) */
    sc_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* SCRUB_HOST_TEST */
