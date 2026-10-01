/*
 * DubSiren - a dub siren in the style of classic analogue sirens,
 * with a tape-style dub echo (Zoom MultiStomp MS-70CDR, custom ZDL, mono).
 *
 * WHAT THE REAL SIRENS DO (from public circuit write-ups of analogue sirens;
 * exact values are not published, so the numbers here are my own tuning of
 * the same behaviour):
 *   - The audio oscillator is a transistor / 555 astable: a FULL-AMPLITUDE
 *     SQUARE wave whose pulse width changes while it is modulated.
 *   - The harsh highs are rounded off by cascaded RC low-pass stages, so the
 *     sound is a dull, buzzy, rounded square, not a sine.
 *   - The LFO is a slow square plus a "shaped" (RC-rounded, uneven) triangle;
 *     range about 0.15 Hz .. 15 Hz.
 *   - The LFO drives the oscillator's control voltage, which is LINEAR in
 *     frequency (f = f0 * (1 + depth*lfo)), not in semitones. Sweeps therefore
 *     feel quick at the bottom and stretched at the top, like the real thing.
 *   - Trigger is a latching button (light press = momentary, hard = latched).
 *   - A classic analogue siren echo: Time 50 ms..1 s, Feedback from a single repeat up
 *     to continuous oscillation, a combined HP/LP filter, echo volume.
 *
 * ROUTING
 *   input -----------------------------------------------------+
 *     (never touched: no filter, no delay, no gain)            |
 *                                                              v
 *   square osc --> 2 RC low-passes --> x envelope --> x Volume = siren --> (+) --> out
 *        ^                                                     |           ^
 *   pitch: LFO or manual envelope                              v           |
 *                                                 dub echo: delay (wow + flutter)
 *                                                 -> HP 120 Hz -> LP -> LP -> x Fdbk
 *                                                 -> soft clip -> write back
 *                                                      +--> x 0.6 (fixed) --+
 *
 *   out = in + siren + echo * 0.6 (Fdbk 0 = echo off). The echo only ever sees the siren.
 *
 * CONTROLS (9 knobs)
 *   0 Trig   the FOOT PEDAL (the effect's on/off footswitch) is the trigger;
 *            this knob picks how it behaves:
 *              Hold   siren sounds while the effect is ON (footswitch latched
 *                     on), releases when you switch it OFF; the echo rings out
 *              Pulse  each press (OFF -> ON only) fires one ~0.6 s burst, however
 *                     long the effect stays on
 *            The effect only sees the on/off state. The footswitch latches
 *            (each press toggles), so "held down" is not visible; Hold means
 *            "while switched on".
 *            HARDWARE NOTE: both modes need the pedal to keep running the
 *            effect while it is switched off so the echo can ring out, and
 *            the trigger needs the pedal to pass the on/off flag while off.
 *            Unconfirmed on the MS-70CDR.
 *   1 Mode   Wail   shaped (uneven RC) triangle LFO: the classic siren rise and fall
 *                   (15 semitones: 415 Hz up to 988 Hz at the default Pitch)
 *            Fast   square LFO, two tones, 2x the Rate speed
 *            Slow   square LFO, two tones, 0.5x the Rate speed
 *            Laser  falling ramp (RC curved): a pitch drop that repeats
 *   2 Pitch  0..100, base frequency 110 Hz .. 1760 Hz (4 octaves, exponential)
 *   3 Rate   LFO speed 1..100 = 0.15 Hz .. 15 Hz, exponential. 0 = Man: the
 *            LFO stops and the trigger fires a pitch envelope instead
 *            (Pulse = drops from 3x the pitch down to the note over ~1 s,
 *            Hold = climbs to 3x and stays up while held)
 *   4 Depth  FM depth, f = f0*(1+4*Depth/100*LFO): 0 = no sweep, 34 = classic two-tone
 *            (15 semitones), 100 = up to 5x the base pitch. All modes.
 *   5 Vol    siren level
 *   6 Time   echo time, ms = 50 + 0.095*screen^2 (50 ms .. 1 s), shown on screen
 *   7 Fdbk   echo feedback in percent; 100 = unity; above that it self-oscillates
 *   (echo tone is fixed: two low-pass poles at ~1.5 kHz, warm tape)
 *   8 Tempo  BPM, 40..240 (the pedal's own number). Only used when Rate is set to a note value.
 *
 * TEMPO SYNC (no clock comes from the pedal, so it follows the BPM you dial in Tempo):
 *   Rate 101..112 = one whole LFO cycle lasts a note value: 4bar 2bar 1bar 1/2. 1/2 1/4.
 *            1/4 1/8. 1/8 1/8T 1/16 1/32 (Fast = 2x, Slow = 0.5x, as in free mode).
 *   Rate 1..100 is the free (Hz) range, as before. The echo Time is never synced.
 *   Fdbk 0 = echo off; the echo level is fixed (it used to be the Echo knob).
 *
 * DEFAULTS = A CLASSIC DANCEHALL TWO-TONE SIREN: Trig Pulse (one short
 * burst), Mode Fast (square LFO, two tones at 2x Rate), Pitch 48 (G#4, 415 Hz),
 * Rate 67 (3.4 Hz, so the two tones alternate about 6.7 times a second),
 * Depth 34 (upper tone = B5, about 980 Hz), Vol 50, Time 40 (202 ms),
 * Fdbk 70 (echo level 0.6). The echo is tape-like: the
 * loop has a high-pass (keeps the repeats from getting boomy), two low-pass
 * poles (each repeat is darker than the last, like tape), a slow wow and a
 * fast flutter on the delay time (the repeats drift in pitch like a tape echo),
 * and a gentle soft clip that only engages near full scale.
 *
 * Safe-DSP rules of this repo are kept: no static/const tables in the audio
 * path, no float divide, no integer / or %, no libm, no switch, no memset,
 * everything always-inline. NOT TESTED ON HARDWARE YET.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define SR_DO_PRAGMA(x) _Pragma(#x)
#define SR_EXPAND_PRAGMA(x) SR_DO_PRAGMA(x)
#define SR_ALWAYS_INLINE(fn) SR_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SR_CODE_SECTION(fn) SR_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SR_ALWAYS_INLINE(fn)
#define SR_CODE_SECTION(fn)
#endif

#define RING_SIZE        65536               /* 256 KB, 1 s = 44100 used      */
#define CLEAR_CHUNK      1024
#define SR_MAGIC         0x53523036u         /* "SR06"                        */
#define HZ_TO_INC        2.2675737e-5f       /* 1 / 44100                     */
#define MS_TO_SAMPLES    44.1f
#define ENV_ATTACK       0.012f              /* per sample: 0 -> 1 in ~2 ms   */
#define ENV_RELEASE      0.0007f             /* per sample: 1 -> 0 in ~32 ms  */
#define PULSE_SAMPLES    26460               /* momentary trigger: 0.6 s      */
#define MAN_STEP         0.0000252f          /* manual pitch envelope: ~0.9 s */
#define DLY_SLEW         0.0008f             /* echo-time glide, tau ~28 ms   */
#define OSC_LP_A         0.25f               /* RC stages after the oscillator */
#define HP_A             0.017f              /* echo loop high-pass, ~120 Hz  */
#define WOW_INC          2.4975e-5f          /* 0.55 Hz                       */
#define FLUT_INC         1.8821e-4f          /* 8.3 Hz                        */
#define WOW_DEPTH        7.0f                /* samples (+-0.16 ms)           */
#define FLUT_DEPTH       1.3f                /* samples                       */

#define SYNC_INC_PER_BPM 3.7793e-7f          /* 1 / (44100 * 60): beats per sample per BPM */

typedef struct {
    unsigned int magic;
    int   w;               /* index of the newest sample in ring              */
    int   clear_pos;
    int   prev_foot;       /* footswitch state in the previous block          */
    int   prev_mode;       /* Trig knob position in the previous block        */
    int   prev_gate;       /* gate in the previous block                      */
    int   pulse_left;      /* samples left of a momentary burst               */
    float osc_ph;          /* oscillator phase accumulator, 0..1              */
    float lfo_ph;          /* LFO phase accumulator, 0..1                     */
    float env;             /* siren volume envelope, 0..1                     */
    float menv;            /* manual pitch envelope, 0..1                     */
    float y1, y2;          /* the two RC low-passes after the oscillator      */
    float wow_ph, flut_ph; /* tape wow / flutter phases                       */
    float dly;             /* smoothed echo time in samples, <0 = not set     */
    float hpl;             /* echo high-pass state                            */
    float lp1, lp2;        /* echo low-pass poles                             */
    float ring[RING_SIZE]; /* siren-only echo line                            */
} SirenState;

typedef struct {
    int   gate;            /* siren sounding                                  */
    int   retrig;          /* Off->ON edge (or Trig-mode change)              */
    int   mode;            /* 0 Wail, 1 Fast, 2 Slow, 3 Laser                 */
    int   manual;          /* Rate knob at 0 (Man)                            */
    int   mdir;            /* manual envelope: -1 drop (Pulse), +1 rise       */
    float f0_inc;          /* base pitch, cycles per sample                   */
    float lfo_inc;         /* LFO cycles per sample                           */
    float depth;           /* FM depth: f = f0 * (1 + depth * u)              */
    float volume, echoLvl;
    float dlySamp;         /* echo time target, samples                       */
    float fb;              /* feedback gain, 0..1.25                          */
    float lpA;             /* echo low-pass coefficient                       */
} SirenParams;

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
    else if (raw <= 3.0f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    return clamp01(ui * inv_max);
}

/* 2^(semis/12), 5th-order Taylor, +-12 st, ~0.3 cent. No libm, no divide. */
SR_ALWAYS_INLINE(semis_to_ratio)
static inline float semis_to_ratio(float semis)
{
    float e  = semis * 0.05776227f;
    float e2 = e * e;
    return 1.0f + e + e2 * (0.5f + e * (0.16666667f
                 + e * (0.041666668f + e * 0.008333334f)));
}

/* 2^oct for oct in 0..7: whole octaves by doubling, the fraction by the
 * polynomial (2^f = 2^(f-0.5) * sqrt2, so it never sees more than +-6 st). */
SR_ALWAYS_INLINE(exp2_oct)
static inline float exp2_oct(float oct)
{
    int   n = (int)oct;
    float f = oct - (float)n;
    float r = semis_to_ratio(f * 12.0f - 6.0f) * 1.41421356f;
    for (; n > 0; n--) r += r;
    return r;
}

/* Triangle starting at 0 and rising: ph in [0,1) -> [-1,+1]. */
SR_ALWAYS_INLINE(tri_bi)
static inline float tri_bi(float ph)
{
    float q = ph + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
}

/* sin(2*pi*ph) = sin(pi/2 * triangle): odd polynomial, error < 1e-4. */
SR_ALWAYS_INLINE(sine_of)
static inline float sine_of(float ph)
{
    float t = tri_bi(ph);
    float t2 = t * t;
    return t * (1.5706268f + t2 * (-0.6432292f + t2 * 0.0727102f));
}

/* Cubic soft clip: flat at +-1.5 -> +-1.0, almost linear below ~0.6. */
SR_ALWAYS_INLINE(soft_clip)
static inline float soft_clip(float x)
{
    if (x > 1.5f) x = 1.5f;
    else if (x < -1.5f) x = -1.5f;
    return x - 0.14814815f * x * x * x;
}

/* Rate, synced (Rate knob 101..112): LFO cycles per beat. The division is the length of
 * one whole LFO cycle: 4 bars, 2 bars, 1 bar, dotted 1/2, 1/2, dotted 1/4, 1/4, dotted 1/8,
 * 1/8, 1/8 triplet, 1/16, 1/32 */
SR_ALWAYS_INLINE(rate_cpb)
static inline float rate_cpb(int d)
{
    return (d == 0) ? 0.0625f : (d == 1) ? 0.125f : (d == 2) ? 0.25f : (d == 3) ? 0.33333334f
         : (d == 4) ? 0.5f : (d == 5) ? 0.6666667f : (d == 6) ? 1.0f : (d == 7) ? 1.3333334f
         : (d == 8) ? 2.0f : (d == 9) ? 3.0f : (d == 10) ? 4.0f : 8.0f;
}

SR_ALWAYS_INLINE(sr_init)
static inline void sr_init(SirenState *s)
{
    s->w = 0; s->clear_pos = 0; s->prev_foot = 0; s->prev_mode = 0; s->prev_gate = 0;
    s->pulse_left = 0;
    s->osc_ph = 0.0f; s->lfo_ph = 0.0f; s->env = 0.0f; s->menv = 0.0f;
    s->y1 = 0.0f; s->y2 = 0.0f; s->wow_ph = 0.0f; s->flut_ph = 0.37f;
    s->dly = -1.0f; s->hpl = 0.0f; s->lp1 = 0.0f; s->lp2 = 0.0f;
    s->magic = SR_MAGIC;
}

/* ---- once per 8-sample block -------------------------------------------
 * k[] are 0..1 (by each knob's own max). foot = 1 while the footswitch has the
 * effect ON. Trigger state tracking: Hold follows the footswitch, Pulse arms a 0.6 s counter on each OFF -> ON press (ON -> OFF does nothing). A gate
 * rising edge (or a change of Trig mode) sets retrig. */
SR_ALWAYS_INLINE(sr_prepare)
static inline void sr_prepare(SirenState *s, SirenParams *P, const float *k, int foot)
{
    int   fm   = (int)(k[0] + 0.5f);                 /* 0 Hold, 1 Pulse       */
    int   np   = (int)(k[2] * 100.0f + 0.5f);        /* pitch                 */
    int   nr   = (int)(k[3] * 112.0f + 0.5f);        /* rate: 0 Man, 1..100 Hz, 101..112 synced */
    int   nt   = (int)(k[6] * 100.0f + 0.5f);
    int   nf   = (int)(k[7] * 125.0f + 0.5f);
    float lfo_hz, bpm;

    if (fm == 1 && foot && !s->prev_foot) s->pulse_left = PULSE_SAMPLES;
    if (fm != 1) s->pulse_left = 0;
    if (fm == 0) P->gate = foot;
    else         P->gate = (s->pulse_left > 0);
    if (s->pulse_left > 0) s->pulse_left -= 8;
    P->retrig = P->gate && (!s->prev_gate || fm != s->prev_mode);
    s->prev_gate = P->gate;
    s->prev_foot = foot;
    s->prev_mode = fm;

    P->mode   = (int)(k[1] * 3.0f + 0.5f);
    P->manual = (nr == 0);
    P->mdir   = (fm == 1) ? -1 : 1;

    /* 110 Hz * 2^(n/25): 110 .. 1760 Hz */
    P->f0_inc = 110.0f * exp2_oct((float)np * 0.04f) * HZ_TO_INC;

    /* 0.15 Hz * 2^(n*0.0664): 0.15 .. 15 Hz; Fast x2, Slow x0.5 */
    bpm = k[8] * 240.0f;                             /* Tempo knob = the BPM (40..240) */
    if (bpm < 40.0f) bpm = 40.0f;
    if (bpm > 240.0f) bpm = 240.0f;
    if (nr > 100) {                                  /* synced: one LFO cycle = the division; Fast x2 and Slow x0.5 as in free mode */
        lfo_hz = bpm * SYNC_INC_PER_BPM * rate_cpb(nr - 101);
        if (P->mode == 1) lfo_hz += lfo_hz;
        else if (P->mode == 2) lfo_hz *= 0.5f;
        P->lfo_inc = lfo_hz;
    } else {
        lfo_hz = 0.15f * exp2_oct((float)nr * 0.0664f);
        if (P->mode == 1) lfo_hz += lfo_hz;
        else if (P->mode == 2) lfo_hz *= 0.5f;
        P->lfo_inc = P->manual ? 0.0f : lfo_hz * HZ_TO_INC;
    }

    /* FM depth: Wail +138% (15 semitones, G#4 up to B5 like a classic two-tone
     * siren), two-tone +50% (a fifth),
     * Laser +300% (two octaves), manual envelope +200% */
    P->depth  = 4.0f * k[4];                         /* knob: 0..4 (top = 5x f0) */

    P->volume  = k[5];
    P->echoLvl = (nf > 0) ? 0.6f : 0.0f;              /* fixed echo level; Fdbk 0 = echo off */
    P->dlySamp = (50.0f + 0.095f * (float)(nt * nt)) * MS_TO_SAMPLES;
    if (P->dlySamp > 44000.0f) P->dlySamp = 44000.0f;
    P->fb      = (float)nf * 0.01f;                   /* 0 .. 1.25 */
    P->lpA     = 0.22f;                               /* constant warm tape tone, ~1.5 kHz per pole */
}

/* ---- the unified loop --------------------------------------------------- */
SR_ALWAYS_INLINE(sr_process)
static inline void sr_process(SirenState *s, const SirenParams *P, float *buf, int n)
{
    int   i, w = s->w;
    float osc_ph = s->osc_ph, lfo_ph = s->lfo_ph, env = s->env, menv = s->menv;
    float y1 = s->y1, y2 = s->y2, wow_ph = s->wow_ph, flut_ph = s->flut_ph;
    float dly = s->dly, hpl = s->hpl, lp1 = s->lp1, lp2 = s->lp2;
    float *ring = s->ring;

    if (s->magic != SR_MAGIC) sr_init(s);

    /* zero the ring lazily; the dry input passes untouched meanwhile */
    if (s->clear_pos < RING_SIZE) {
        volatile float *rp = ring;
        int end = s->clear_pos + CLEAR_CHUNK;
        for (i = s->clear_pos; i < end; i++) rp[i] = 0.0f;
        s->clear_pos = end;
        s->prev_gate = 0;          /* a trigger edge seen now must still fire */
        return;
    }

    if (dly < 0.0f) dly = P->dlySamp;
    if (P->retrig) {                               /* trigger edge            */
        lfo_ph = 0.0f;                             /* LFO phase -> 0          */
        menv = (P->mdir < 0) ? 1.0f : 0.0f;        /* manual envelope start   */
    }

    for (i = 0; i < n; i++) {
        float in = buf[i];                         /* clean input, never modified */
        float u, inc, duty, p2, sq, siren, rd, dl, hp, e, x, dm;
        int   wn, i0, i1;

        /* --- pitch control value u (0..1): shaped LFO, square LFO, ramp or manual */
        if (P->manual) {
            menv += (float)P->mdir * MAN_STEP;
            if (menv < 0.0f) menv = 0.0f;
            if (menv > 1.0f) menv = 1.0f;
            u = menv;
        } else {
            lfo_ph += P->lfo_inc;
            if (lfo_ph >= 1.0f) lfo_ph -= 1.0f;
            if (P->mode == 0) {                    /* uneven RC triangle: fast rise, slow fall */
                if (lfo_ph < 0.35f) {
                    u = 1.0f - lfo_ph * 2.857143f;       /* 1 - ph/0.35 */
                    u = 1.0f - u * u;                    /* concave rise */
                } else {
                    u = 1.0f - (lfo_ph - 0.35f) * 1.538462f;   /* 1 - (ph-.35)/.65 */
                    u = u * (0.3f + 0.7f * u);           /* convex fall */
                }
            }
            else if (P->mode == 3) { u = 1.0f - lfo_ph; u = u * u; }   /* RC curved falling ramp */
            else u = (lfo_ph < 0.5f) ? 0.0f : 1.0f;                     /* two-tone square */
        }

        /* --- oscillator: linear FM (control voltage), square with changing pulse width */
        inc = P->f0_inc * (1.0f + P->depth * u);
        osc_ph += inc;
        if (osc_ph >= 1.0f) osc_ph -= 1.0f;
        duty = 0.5f - 0.12f * (u > 1.0f ? 1.0f : u);
        p2 = osc_ph - 0.5f * inc;                   /* half-sample tap: 2x box filter */
        if (p2 < 0.0f) p2 += 1.0f;
        sq = 0.5f * (((osc_ph < duty) ? 1.0f : -1.0f) + ((p2 < duty) ? 1.0f : -1.0f));

        /* --- the RC low-pass stages that round the square off */
        y1 += OSC_LP_A * (sq - y1);
        y2 += OSC_LP_A * (y1 - y2);

        /* --- volume envelope: attack while the gate is on, release after */
        if (P->gate) { env += ENV_ATTACK;  if (env > 1.0f) env = 1.0f; }
        else         { env -= ENV_RELEASE; if (env < 0.0f) env = 0.0f; }

        siren = 0.3f * y2 * env * P->volume;

        /* --- tape-style dub echo, fed by the siren only */
        wow_ph += WOW_INC;  if (wow_ph >= 1.0f)  wow_ph -= 1.0f;
        flut_ph += FLUT_INC; if (flut_ph >= 1.0f) flut_ph -= 1.0f;
        dly += (P->dlySamp - dly) * DLY_SLEW;      /* time changes glide      */
        dm = dly + WOW_DEPTH * sine_of(wow_ph) + FLUT_DEPTH * sine_of(flut_ph);
        wn = w + 1; if (wn >= RING_SIZE) wn = 0;
        rd = (float)wn - dm;
        if (rd < 0.0f) rd += (float)RING_SIZE;
        i0 = (int)rd;
        i1 = i0 + 1; if (i1 >= RING_SIZE) i1 = 0;
        dl = ring[i0] + (rd - (float)i0) * (ring[i1] - ring[i0]);

        hpl += HP_A * (dl - hpl);                  /* high-pass: dl - low part */
        hp = dl - hpl;
        lp1 += P->lpA * (hp - lp1);                /* two low-pass poles       */
        lp2 += P->lpA * (lp1 - lp2);
        e = lp2;
        x = siren * 0.9f + e * P->fb;              /* feedback may exceed 1.0  */
        ring[wn] = soft_clip(x);
        w = wn;

        /* --- parallel mix */
        buf[i] = in + siren + e * P->echoLvl;
    }

    s->w = w; s->osc_ph = osc_ph; s->lfo_ph = lfo_ph; s->env = env; s->menv = menv;
    s->y1 = y1; s->y2 = y2; s->wow_ph = wow_ph; s->flut_ph = flut_ph;
    s->dly = dly; s->hpl = hpl; s->lp1 = lp1; s->lp2 = lp2;
}

/* ---- on-screen text (knob index = ZDL_GetLabel_<index>) ----------------- */
/* 0 Trig: Hold Pulse */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    char c0, c1, c2, c3, c4 = 0;
    int len = 4;
    if (value == 0u) { c0 = 'H'; c1 = 'o'; c2 = 'l'; c3 = 'd'; }
    else             { c0 = 'P'; c1 = 'u'; c2 = 'l'; c3 = 's'; c4 = 'e'; len = 5; }
    out[0] = c0; out[1] = c1; out[2] = c2; out[3] = c3; out[4] = c4; out[len] = 0;
    return len;
}

/* 1 Mode: Wail Fast Slow Laser */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    char c0, c1, c2, c3, c4 = 0;
    int len = 4;
    if (value == 0u)      { c0 = 'W'; c1 = 'a'; c2 = 'i'; c3 = 'l'; }
    else if (value == 1u) { c0 = 'F'; c1 = 'a'; c2 = 's'; c3 = 't'; }
    else if (value == 2u) { c0 = 'S'; c1 = 'l'; c2 = 'o'; c3 = 'w'; }
    else                  { c0 = 'L'; c1 = 'a'; c2 = 's'; c3 = 'e'; c4 = 'r'; len = 5; }
    out[0] = c0; out[1] = c1; out[2] = c2; out[3] = c3; out[4] = c4; out[len] = 0;
    return len;
}

/* One synced note value as text: five characters at most. Rate 101..112 */
SR_ALWAYS_INLINE(note_text)
static inline int note_text(char *out, int c0, int c1, int c2, int c3, int c4)
{
    int len = 3;
    out[0] = (char)c0; out[1] = (char)c1; out[2] = (char)c2; out[3] = (char)c3; out[4] = (char)c4;
    if (c3 != 0) len = 4;
    if (c4 != 0) len = 5;
    out[len] = 0;
    return len;
}

/* 3 Rate: "Man" at 0, the number 1..100 (free, Hz), then 101..112 = the length of one LFO
 * cycle as a note value: 4bar 2bar 1bar 1/2. 1/2 1/4. 1/4 1/8. 1/8 1/8T 1/16 1/32 */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value == 0u) { out[0] = 'M'; out[1] = 'a'; out[2] = 'n'; out[3] = 0; return 3; }
    if (value > 112u) value = 112u;
    if (value > 100u) {
        n = (int)value - 101;
        if (n == 0) return note_text(out, '4', 'b', 'a', 'r', 0);
        if (n == 1) return note_text(out, '2', 'b', 'a', 'r', 0);
        if (n == 2) return note_text(out, '1', 'b', 'a', 'r', 0);
        if (n == 3) return note_text(out, '1', '/', '2', '.', 0);
        if (n == 4) return note_text(out, '1', '/', '2', 0, 0);
        if (n == 5) return note_text(out, '1', '/', '4', '.', 0);
        if (n == 6) return note_text(out, '1', '/', '4', 0, 0);
        if (n == 7) return note_text(out, '1', '/', '8', '.', 0);
        if (n == 8) return note_text(out, '1', '/', '8', 0, 0);
        if (n == 9) return note_text(out, '1', '/', '8', 'T', 0);
        if (n == 10) return note_text(out, '1', '/', '1', '6', 0);
        return note_text(out, '1', '/', '3', '2', 0);
    }
    n = (int)value;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* 6 Time: real echo time, "50ms" .. "999ms", "1.00s" (same law as sr_prepare) */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    int v, ms, h = 0, t = 0, len = 0;
    if (value > 100u) value = 100u;
    v = (int)value;
    ms = (int)(50.0f + 0.095f * (float)(v * v) + 0.5f);
    if (ms >= 1000) {
        out[0] = '1'; out[1] = '.'; out[2] = '0'; out[3] = '0';
        out[4] = 's'; out[5] = 0;
        return 5;
    }
    while (ms >= 100) { ms -= 100; h++; }
    while (ms >= 10)  { ms -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + ms); len++;
    out[len] = 'm'; len++;
    out[len] = 's'; len++;
    out[len] = 0;
    return len;
}

/* 7 Fdbk: "Off" at 0 (no echo at all), then the percent 1..125 */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value == 0u) { out[0] = 'O'; out[1] = 'f'; out[2] = 'f'; out[3] = 0; return 3; }
    if (value > 125u) value = 125u;
    n = (int)value;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* ---- pedal entry point (same structure as DualShft, hardware-proven) ---- */
#ifndef DUBSIREN_HOST_TEST

#include "dubsiren_params.h"

#ifndef DUBSIREN_AUDIO_FUNC
#define DUBSIREN_AUDIO_FUNC Fx_DLY_DubSiren
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SR_CODE_SECTION(DUBSIREN_AUDIO_FUNC)
void DUBSIREN_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    SirenState *s;
    SirenParams P;
    float k[9];
    int i, foot;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    foot = (params[0] >= 0.5f);                  /* footswitch: effect ON     */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(SirenState) || span < (end - base)) return;
    if (stateBase + sizeof(SirenState) > end) return;

    s = (SirenState *)stateBase;

    k[0] = sr_knob(params[DUBSIREN_TRIG_SLOT],   (float)DUBSIREN_TRIG_UI_DEFAULT,   1.0f);
    k[1] = sr_knob(params[DUBSIREN_MODE_SLOT],   (float)DUBSIREN_MODE_UI_DEFAULT,   0.3333333f);
    k[2] = sr_knob(params[DUBSIREN_PITCH_SLOT],  (float)DUBSIREN_PITCH_UI_DEFAULT,  0.01f);
    k[3] = sr_knob(params[DUBSIREN_RATE_SLOT],   (float)DUBSIREN_RATE_UI_DEFAULT,   0.008928571f);
    k[4] = sr_knob(params[DUBSIREN_DEPTH_SLOT],  (float)DUBSIREN_DEPTH_UI_DEFAULT,  0.01f);
    k[5] = sr_knob(params[DUBSIREN_VOL_SLOT],    (float)DUBSIREN_VOL_UI_DEFAULT,    0.01f);
    k[6] = sr_knob(params[DUBSIREN_TIME_SLOT],   (float)DUBSIREN_TIME_UI_DEFAULT,   0.01f);
    k[7] = sr_knob(params[DUBSIREN_FDBK_SLOT],   (float)DUBSIREN_FDBK_UI_DEFAULT,   0.008f);
    k[8] = sr_knob(params[DUBSIREN_TEMPO_SLOT],  (float)DUBSIREN_TEMPO_UI_DEFAULT,  0.004166667f);

    if (s->magic != SR_MAGIC) sr_init(s);        /* prev_* must be valid       */
    sr_prepare(s, &P, k, foot);
    sr_process(s, &P, fxBuf, 8);

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];
}

#endif /* DUBSIREN_HOST_TEST */
