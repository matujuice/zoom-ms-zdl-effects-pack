/*
 * segue.c - "Segue": a bar-synced transition looper for a whole drum-machine mix, mono
 *
 * Made for playing a set with one sequencer (Luca's Digitakt at about 160 BPM). The whole
 * mix goes through the pedal. Segue follows the kick to know where every bar and phrase
 * starts; one press records the next phrase, and at its end the output jumps to the loop,
 * so the sequencer can change pattern underneath without anyone hearing it. The XFade
 * knob then fades the live mix back in and the loop out.
 *
 * WORKFLOW
 *   1. Load the patch at the start of the set and leave it on. The first kick it hears is
 *      bar 1. From then on it counts bars and phrases (Bars long) and follows every kick.
 *   2. Press the footswitch any time before the phrase ends: it is ARMED.
 *   3. On the next phrase start it RECORDS one phrase (Bars bars) of the mix, while you
 *      queue the next pattern on the sequencer.
 *   4. At the phrase end the loop starts and the output JUMPS to 100 % loop: the pattern
 *      change underneath is not heard.
 *   5. PICKUP: the output stays 100 % loop, whatever XFade says, until XFade is turned fully
 *      right (LOOP). From then on XFade works normally: turn it left to fade the mix back in
 *      and the loop out.
 *   That is Mode JUMP. Mode AUTO does steps 4..5 by itself: 100 % loop for one loop length,
 *   then the output fades back to live over the next one (an automatic XFade from LOOP to
 *   LIVE, DJ law). Mode MANUAL never moves on its own: XFade always does what it shows.
 *   In JUMP and AUTO, turning XFade takes over as soon as it meets the automatic position
 *   (crosses it, or is within 1 of it), like a soft-takeover knob, so there is no jump.
 *   6. The loop keeps playing (inaudible at XFade LIVE) and tracking goes on. The next press
 *      arms the next transition; the new recording overwrites the loop in place.
 *   Until the first loop is recorded the output is the live mix, whatever XFade says.
 *
 * FOOTSWITCH (any press, the LED means nothing)
 *   The audio code runs whether the effect is on or off (the pedal only passes the on/off
 *   state, build/ABI.md); Segue ignores it and counts every flip, either way, as a press.
 *   One press arms. Three presses within half a second of each other RESET: the arm (and a
 *   recording just started) is cancelled and the next kick becomes bar 1 again; the loop
 *   stays as it is. Use it if bar 1 slipped (it was set on a stray sound, a long tempo
 *   jump). Unverified on the pedal: that the firmware adds no fade or mute of its own when
 *   the effect is switched off.
 *
 * KICK TRACKING (no effect on the sound)
 *   The input goes through a kick band: two one-pole low-passes at Listn (AUTO = 100 Hz)
 *   minus a 30 Hz one-pole (no rumble or DC). Once per 8-sample block the band's peak feeds
 *   an envelope (instant attack, 10 ms release) and a 40 ms average of it. An onset is the
 *   envelope jumping above Thrsh x the average (AUTO: 2 x, stepping down every 4 s to
 *   1.7, 1.5, 1.3 x while nothing locks, for boomy kicks whose tail is still loud at the
 *   next one; the step stays once locked), at least 0.2 beat (at HiBPM)
 *   after the last one and only after the envelope fell back below the threshold (no double
 *   triggers on one long kick). 20 ms later the onset is judged by the peak it reached: a
 *   kick if at least 70 % of the loudest onset in the last 1..2 s, so bass notes and toms
 *   clearly quieter than the kick do not count.
 *   The bar clock is a phase-locked loop. It counts beats of T samples; every kick that
 *   lands within 12 % of a beat nudges the phase by a quarter of the error and the tempo by
 *   2 % of it, so it follows the sequencer through the up to ~100 ppm the two crystals
 *   differ (about a beat per hour at 160) and through slow tempo moves; in a breakdown with
 *   no kicks it free-runs on the last tempo. Kicks off the grid (syncopated ones) are
 *   ignored; four in a row agreeing on a new tempo re-lock it there (a tempo jump), keeping
 *   the bar count as well as it can (a reset puts bar 1 right if not).
 *   Finding the tempo: after bar 1 the time between kicks is halved until it fits LoBPM..
 *   HiBPM (kicks every 2 beats count as a beat, off-beat ones are skipped); four agreeing
 *   in a row lock the tempo. LoBPM = HiBPM fixes it (only the phase is tracked). The range
 *   is what stops hats, double time or half time from fooling it. A gap of more than two
 *   beats that does not halve into the range starts the measuring again from that kick;
 *   once two gaps agree, the beats since bar 1 are counted from the time since it.
 *   Every beat (and so every recording start and jump) sits SE_LEAD = 15 ms before the
 *   kick is detected, which is a few ms before the kick starts: the loop's cut lands in
 *   the quiet before the kick, never on it.
 *
 * LOOP
 *   One buffer of 682656 8-bit samples (15.48 s) plus one scale byte per 32 samples, 704 KB
 *   of the at least 705536-byte arena: block floating point (as in NICAM) of the signal
 *   with a pre-emphasis, de-emphasised on playback (about 48 dB signal to noise at any
 *   level, ~57 dB in the highs where hiss is heard; never clips). The live input is never
 *   stored or changed: only the loop goes through this format. 8 bars fit down to about
 *   127 BPM (with 2 % room for drift). If Bars do not fit at the tempo being
 *   tracked when you press, it records the longest part of the phrase that does and still
 *   divides it (8 -> 4 -> 2 -> 1 bars: the last ones of the phrase, so the jump still lands
 *   on the phrase end) and plays a double beep. Nothing is ever read from the buffer before
 *   it was recorded, so it needs no clearing after loading.
 *   The loop plays at the tempo it was recorded at; a tempo ramp while it is up drifts.
 *   Every seam (the loop's end, a Roll jump, the start of a new recording) is a 128-sample
 *   (2.9 ms) linear crossfade from where the playhead was going to where it jumps. At the
 *   loop end the old voice reads on into 128 samples recorded just after the phrase (the
 *   tail), so the end flows into the start like a tape splice.
 *   A new recording overwrites the loop in place and starts with the loop's playhead at its
 *   start: the playhead reads each sample just before it is overwritten, so a loop still
 *   up during the recording keeps playing unchanged until the jump.
 *
 * KNOBS (screen values; pages of three: Perform, Loop, Tracking + Mode)
 *   0 XFade 0..100  crossfade, DJ style: 0 LIVE (the mix) .. 100 LOOP; dry gain
 *                   min(1, 2 - 2m), loop gain min(1, 2m), both full at 50. After the jump
 *                   (JUMP, AUTO): ignored until it meets the automatic position (pickup).
 *                   Gains glide ~5 ms.
 *   1 LoCut 0..100  high-pass on the loop only (24 dB/octave), to take its kick and bass out
 *                   under the live mix: 0 OFF, 1..100 = 21 Hz..2 kHz (log); shown in Hz
 *   2 Roll  0..4    OFF / 1BAR / 2BEAT (half a bar) / 1BEAT / 1/2BT (half a beat):
 *                   repeats the slice of the loop the playhead is in (slices counted from the
 *                   loop start), in time, like a DJ beat roll. The loop itself runs on
 *                   underneath, so turning Roll OFF lands where the loop would have been:
 *                   the bars and the phrase never move.
 *   3 Bars  0..7    phrase and loop length, shown as 1..8 bars
 *   4 LoBPM 0..240  slowest tempo the tracker may lock to (below 40 reads as 40)
 *   5 HiBPM 0..240  fastest; LoBPM = HiBPM fixes the tempo (swapped if Lo > Hi)
 *   6 Thrsh 0..100  0 AUTO (hunts 2, 1.7, 1.5, 1.3 x until it locks), else how far the
 *                   kick band must jump over its recent level: 1 = 1.02 x (anything) ..
 *                   50 = 2 x .. 100 = 3 x (only hard kicks)
 *   7 Listn 0..101  0 AUTO (100 Hz), else the kick band's low-pass: 1..101 = 50..150 Hz
 *   8 Mode  0..2    JUMP / AUTO / MANU(AL): what the output does when a loop is ready
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline, no calls. 2^x from a polynomial and the float exponent, 1/x by
 * Newton steps, a remainder by a reciprocal and a correction.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define SE_DO_PRAGMA(x) _Pragma(#x)
#define SE_EXPAND_PRAGMA(x) SE_DO_PRAGMA(x)
#define SE_ALWAYS_INLINE(fn) SE_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SE_CODE_SECTION(fn) SE_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SE_ALWAYS_INLINE(fn)
#define SE_CODE_SECTION(fn)
#endif

#define SE_MAGIC      0x53455135u        /* "SEQ5": change whenever SeState changes */
#define SE_N          682656             /* loop buffer, 8-bit samples: 15.48 s           */
#define SE_NB         21333              /* one scale byte per 32 samples (SE_N / 32)     */
#define SE_PRE        0.85f              /* pre-emphasis: stored d = x - 0.85 x[-1]       */
#define SE_WARM       64                 /* samples decoded to settle a voice at a jump   */
#define SE_X          128                /* seam crossfade, samples (2.9 ms)             */
#define SE_XINV       0.0078125f         /* 1 / SE_X                                     */
#define SE_WMAX       (SE_N - SE_X - 2)  /* a recording stops here at the latest         */
#define SE_LEAD       662.0f             /* beats sit 15 ms before the detected kick     */
#define SE_JUDGE      110                /* blocks: an onset is judged 20 ms later       */
#define SE_HUNT       22050              /* AUTO: 4 s (in blocks) per threshold step     */
#define SE_TAPWIN     2756               /* blocks: 0.5 s between the taps of a reset    */
#define SE_SPB        2646000.0f         /* samples per beat x BPM (60 x 44100)          */
#define SE_GLIDE      0.005f             /* gain glide per sample: ~5 ms                 */
#define SE_BEEP_LEN   3528               /* one beep or gap: 80 ms                       */
#define SE_BEEP_W     0.14247585f        /* 2 pi 1000 / 44100: a 1 kHz beep              */
#define SE_BEEP_AMP   0.2f

#define SE_MODE_JUMP   0                 /* Mode knob                                    */
#define SE_MODE_AUTO   1
#define SE_MODE_MANUAL 2

#define SE_WAIT       0                  /* no bar 1 yet (after loading or a reset)      */
#define SE_ACQ        1                  /* bar 1 known, finding the tempo               */
#define SE_LOCK       2                  /* tempo locked, tracking                       */

typedef struct {
    unsigned int magic;
    /* footswitch */
    int   on_prev;         /* last on/off state seen                          */
    int   tap_age;         /* blocks since the last press                     */
    int   taps;            /* presses in the current quick burst              */
    /* kick detector */
    float d1, d2, dc;      /* kick band: two low-passes, minus a 30 Hz one   */
    float env, slow;       /* envelope, its 40 ms average                     */
    int   refr;            /* blocks before the next onset may count          */
    int   ready;           /* the envelope fell below the threshold again     */
    int   pend;            /* blocks since an onset waiting to be judged, -1  */
    float pstr;            /* its peak so far                                 */
    float kw, kp;          /* loudest onset this second and the last one      */
    int   kn;              /* blocks into this second                         */
    int   ri;              /* AUTO: threshold step 0..3 (2, 1.7, 1.5, 1.3 x)  */
    int   hunt;            /* AUTO: blocks without a lock at this step        */
    /* bar clock */
    int   cs;              /* SE_WAIT / SE_ACQ / SE_LOCK                      */
    int   tl;              /* the tempo has been locked once                  */
    float T;               /* beat length, samples                            */
    float ph;              /* samples since the last beat                     */
    int   beat;            /* beats since bar 1 (beat 0 = bar 1, beat 1)      */
    int   kb;              /* beat of the last kick accepted                  */
    int   sk;              /* samples since the last kick accepted            */
    int   s1;              /* samples since bar 1                             */
    int   hits, misses;    /* agreeing kicks (finding), off-grid in a row     */
    float tm;              /* tempo the last off-grid kick suggested          */
    /* looper */
    int   armed;           /* waiting for the phrase start                    */
    int   rb, rd;          /* phrase bars, bars to record (rd divides rb)     */
    int   rec;             /* recording                                       */
    int   rec_end;         /* beat at which the recording ends                */
    int   wi;              /* write index                                     */
    int   tail;            /* samples still to record after the end           */
    int   has_loop;
    int   L;               /* loop length, samples                            */
    int   lbars;           /* bars in the loop                                */
    int   pi;              /* playhead (what is heard)                        */
    int   pm;              /* the loop's own position: always runs on, so Roll
                              never moves the loop or its bars              */
    int   po, fade;        /* the voice fading out at a seam, samples left    */
    int   rs;              /* Roll: start of the slice being repeated         */
    int   rk;              /* Roll: slice of the grid pm is in                */
    int   nj;              /* next grid line of pm (L: the loop end)          */
    float rlen;            /* Roll slice length, samples (0 = off)            */
    int   roll;            /* Roll setting the slice was taken for            */
    int   pickup;          /* 1 = the output follows pa until XFade meets it  */
    float pa;              /* that automatic XFade position, 0..1 (1 = LOOP)  */
    int   ahold;           /* AUTO: samples left at 100 % loop, -1 = no ramp  */
    float astep;           /* AUTO: pa step per sample (one loop to fade out) */
    int   dsign;           /* side of pa the knob is on (0 = not known yet)   */
    int   mode;            /* SE_MODE_* (from the Mode knob)                  */
    float gd, gw;          /* dry and loop gains (gliding)                    */
    float s1b, s1l, s2b, s2l;   /* LoCut: two state-variable filters          */
    float F;               /* LoCut: tan(pi f / fs), gliding                  */
    int   beep;            /* samples of the double beep left                 */
    float bs, bc;          /* beep oscillator                                 */
    float zm, zo;          /* de-emphasis state of the playhead and fade voice */
    float xp;              /* previous input (pre-emphasis)                   */
    float stg[32];         /* the block being recorded, before it is scaled   */
    unsigned char buf[SE_N];   /* 8-bit mantissas                             */
    signed char   bex[SE_NB];  /* each 32-sample block's scale: 2^bex         */
} SeState;

typedef struct {
    float tgd, tgw;        /* XFade gains (DJ law)                            */
    float xf;              /* XFade position 0..1                             */
    int   mode;            /* SE_MODE_JUMP / AUTO / MANUAL                    */
    int   cut;             /* LoCut on                                        */
    float F;               /* LoCut: tan(pi f / fs)                           */
    int   roll;            /* 0..4                                            */
    int   bars;            /* 1..8                                            */
    float Tmin, Tmax;      /* beat length range from HiBPM, LoBPM             */
    int   fixed;           /* LoBPM = HiBPM                                   */
    float R;               /* kick: jump over the average                     */
    int   autoR;           /* Thrsh AUTO: the detector picks R itself         */
    float lpc;             /* kick band low-pass coefficient                  */
    int   refr;            /* blocks between kicks at least                   */
} SeParams;

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer. */
SE_ALWAYS_INLINE(se_ui)
static inline float se_ui(float raw, float def_ui, float max_ui)
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
SE_ALWAYS_INLINE(se_exp2)
static inline float se_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int   n = (int)x;
    float f, r;
    if ((float)n > x) n--;
    f = x - (float)n;
    r = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    c.u = ((unsigned int)(n + 127)) << 23;
    return r * c.f;
}

/* 1 / x for x > 0: a first guess from the float bits, then three Newton steps (no divide) */
SE_ALWAYS_INLINE(se_recip)
static inline float se_recip(float x)
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

/* x mod m for x >= 0, 1 <= m <= 8, by a reciprocal and a correction (no integer divide) */
SE_ALWAYS_INLINE(se_mod)
static inline int se_mod(int x, int m)
{
    int r = x - (int)((float)x * se_recip((float)m)) * m;
    while (r < 0) r += m;
    while (r >= m) r -= m;
    return r;
}

/* 2^n as a float, n in -100..100, from the exponent bits */
SE_ALWAYS_INLINE(se_pow2i)
static inline float se_pow2i(int n)
{
    union { float f; unsigned int u; } c;
    c.u = ((unsigned int)(n + 127)) << 23;
    return c.f;
}

/* the loop's format: 8-bit samples with one scale per 32 (block floating point, as in
 * NICAM), of the pre-emphasised signal d = x - 0.85 x[-1]. Each block is scaled so its
 * peak sits in the top octave of the 8 bits: about 48 dB signal to noise whatever the
 * level, and the de-emphasis on playback (y = d + 0.85 y[-1]) takes the noise down
 * another ~10 dB in the highs, where hiss is heard. Nothing clips: the scale covers any
 * level. Samples are staged as floats until their block is complete, then scaled. */
SE_ALWAYS_INLINE(se_flush)
static inline void se_flush(SeState *s, int base, int n)
{
    union { float f; unsigned int u; } c;
    float m = 0.0f, a, sc;
    int k, e, q;
    for (k = 0; k < n; k++) { a = s->stg[k]; if (a < 0.0f) a = -a; if (a > m) m = a; }
    c.f = m;
    e = -(int)((c.u >> 23) & 255u) + 126;        /* m x 2^e in 0.5..1 */
    if (m < 1e-12f || e > 60) e = 60;
    if (e < -60) e = -60;
    sc = 127.0f * se_pow2i(e);
    for (k = 0; k < n; k++) {
        a = s->stg[k] * sc;
        q = (int)(a + ((a >= 0.0f) ? 0.5f : -0.5f));
        if (q > 127) q = 127;
        if (q < -127) q = -127;
        s->buf[base + k] = (unsigned char)(q & 255);
    }
    s->bex[base >> 5] = (signed char)e;
}

/* the stored (pre-emphasised) value at idx: from the staged block if it was recorded
 * there and not yet scaled, else from the buffer */
SE_ALWAYS_INLINE(se_rd)
static inline float se_rd(const SeState *s, int idx)
{
    int q;
    if ((s->rec || s->tail > 0) && (idx & ~31) == (s->wi & ~31) && idx < s->wi)
        return s->stg[idx & 31];
    q = (int)s->buf[idx];
    if (q > 127) q -= 256;
    return (float)q * 0.007874016f * se_pow2i(-(int)s->bex[idx >> 5]);
}

/* record one input sample */
SE_ALWAYS_INLINE(se_wr)
static inline void se_wr(SeState *s, float in)
{
    s->stg[s->wi & 31] = in - SE_PRE * s->xp;
    s->wi++;
    if ((s->wi & 31) == 0) se_flush(s, s->wi - 32, 32);
}

/* de-emphasis state for a voice about to read idx: decode the samples just before it */
SE_ALWAYS_INLINE(se_warm)
static inline float se_warm(const SeState *s, int idx)
{
    int j = idx - SE_WARM;
    float z = 0.0f;
    if (j < 0) j = 0;
    for (; j < idx; j++) z = se_rd(s, j) + SE_PRE * z;
    return z;
}

SE_ALWAYS_INLINE(se_init)
static inline void se_init(SeState *s, float T0)
{
    s->on_prev = -1; s->tap_age = SE_TAPWIN; s->taps = 0;
    s->d1 = 0.0f; s->d2 = 0.0f; s->dc = 0.0f;
    s->env = 0.0f; s->slow = 0.0f; s->refr = 0; s->ready = 1;
    s->pend = -1; s->pstr = 0.0f; s->kw = 0.0f; s->kp = 0.0f; s->kn = 0;
    s->ri = 0; s->hunt = 0; s->s1 = 0;
    s->cs = SE_WAIT; s->tl = 0; s->T = T0; s->ph = 0.0f; s->beat = 0; s->kb = 0; s->sk = 0;
    s->hits = 0; s->misses = 0; s->tm = T0;
    s->armed = 0; s->rb = 8; s->rd = 8; s->rec = 0; s->rec_end = 0; s->wi = 0; s->tail = 0;
    s->has_loop = 0; s->L = 0; s->lbars = 1; s->pi = 0; s->po = 0; s->fade = 0;
    s->pm = 0; s->rs = 0; s->rk = 0; s->nj = 0; s->rlen = 0.0f; s->roll = 0; s->pickup = 0;
    s->pa = 0.0f; s->ahold = -1; s->astep = 0.0f; s->dsign = 0; s->mode = 0;
    s->gd = 1.0f; s->gw = 0.0f;
    s->s1b = 0.0f; s->s1l = 0.0f; s->s2b = 0.0f; s->s2l = 0.0f; s->F = 0.0014248f;
    s->beep = 0; s->bs = 0.0f; s->bc = 1.0f;
    s->zm = 0.0f; s->zo = 0.0f; s->xp = 0.0f;
    s->magic = SE_MAGIC;
}

/* u[] = screen values in manifest order */
SE_ALWAYS_INLINE(se_prepare)
static inline void se_prepare(SeParams *P, const float *u)
{
    float m, lo, hi, t;
    int th;
    m = u[0] * 0.01f;
    P->tgd = 2.0f - 2.0f * m; if (P->tgd > 1.0f) P->tgd = 1.0f;
    P->tgw = 2.0f * m;        if (P->tgw > 1.0f) P->tgw = 1.0f;
    P->xf = m;
    P->mode = (int)(u[8] + 0.5f);
    P->cut = (u[1] >= 0.5f);
    /* f = 20 Hz x 100^(v/100); the filters want tan(pi f / fs), here at most tan(0.143) */
    t = 20.0f * se_exp2(u[1] * 0.06643856f) * 7.1237928e-5f;  /* pi f / fs */
    if (!P->cut) t = 0.0014248f;                             /* 20 Hz while off */
    m = t * t;
    P->F = t * (1.0f + m * (0.33333334f + m * 0.13333334f));
    P->roll = (int)(u[2] + 0.5f);
    P->bars = (int)(u[3] + 0.5f) + 1;
    lo = u[4]; hi = u[5];
    if (lo < 40.0f) lo = 40.0f;
    if (hi < 40.0f) hi = 40.0f;
    if (lo > hi) { t = lo; lo = hi; hi = t; }
    P->fixed = (hi - lo < 0.5f);
    P->Tmin = SE_SPB * se_recip(hi);
    P->Tmax = SE_SPB * se_recip(lo);
    th = (int)(u[6] + 0.5f);
    P->autoR = (th == 0);
    P->R = 1.0f + 0.02f * (float)th;                         /* 1..100: 1.02 .. 3 x */
    t = (u[7] >= 0.5f) ? (49.0f + u[7]) : 100.0f;           /* kick band, Hz */
    P->lpc = t * 1.4247585e-4f;                              /* ~ 2 pi f / fs */
    P->refr = (int)(P->Tmin * 0.2f * 0.125f);
}

/* jump the playhead to 'to'; the voice at 'from' fades out over the seam */
SE_ALWAYS_INLINE(se_jump)
static inline void se_jump(SeState *s, int from, int to)
{
    s->po = from;
    s->fade = SE_X;
    s->pi = to;
    s->zo = se_warm(s, from);
    s->zm = se_warm(s, to);
}

/* Roll: take the slice of the loop's grid that pm is in (OFF: none). The playhead stays
 * where it is (on pm), so the slice starts playing in time; at every grid line pm passes
 * it jumps back to the slice start (se_process). */
SE_ALWAYS_INLINE(se_window)
static inline void se_window(SeState *s)
{
    float beat = (float)s->L * se_recip((float)(4 * s->lbars)), len = 0.0f;
    int k, nj;
    if (s->roll == 1) len = 4.0f * beat;
    if (s->roll == 2) len = 2.0f * beat;
    if (s->roll == 3) len = beat;
    if (s->roll >= 4) len = 0.5f * beat;
    if (len < 64.0f || len > (float)s->L - 32.0f) { s->rlen = 0.0f; s->nj = s->L; return; }
    k = (int)((float)s->pm * se_recip(len));
    while (k > 0 && (int)((float)k * len + 0.5f) > s->pm) k--;
    while ((int)((float)(k + 1) * len + 0.5f) <= s->pm) k++;
    nj = (int)((float)(k + 1) * len + 0.5f);
    if (nj > s->L) nj = s->L;
    s->rlen = len; s->rk = k; s->nj = nj;
    s->rs = (int)((float)k * len + 0.5f);
}

/* the recording is complete: the loop starts and the output jumps to it */
SE_ALWAYS_INLINE(se_finish)
static inline void se_finish(SeState *s)
{
    s->rec = 0;
    s->L = s->wi;
    s->lbars = s->rd;
    s->tail = SE_X;                              /* record on into the tail       */
    s->has_loop = 1;
    s->pickup = (s->mode != SE_MODE_MANUAL);     /* JUMP, AUTO: 100 % loop now */
    s->pa = 1.0f;
    s->ahold = (s->mode == SE_MODE_AUTO) ? s->L : -1;
    s->astep = se_recip((float)s->L);
    s->dsign = 0;
    s->pm = 0;
    se_jump(s, s->L, 0);                         /* fade from the live tail in    */
    se_window(s);
}

/* how many bars to record: Bars, or if they do not fit at this tempo the longest part
 * that does and divides the phrase (8 -> 4 -> 2 -> 1). Returns 1 if shortened. */
SE_ALWAYS_INLINE(se_arm)
static inline int se_arm(SeState *s, const SeParams *P)
{
    int d = P->bars, k, divides;
    float room = (float)(SE_WMAX - 64);
    s->armed = 1;
    s->rb = P->bars;
    while (d > 1) {
        divides = 0;
        for (k = d; k <= P->bars; k += d) if (k == P->bars) divides = 1;
        if (divides && 4.08f * (float)d * s->T <= room) break;   /* 2 % for drift */
        d--;
    }
    s->rd = d;
    return d < P->bars;
}

/* three quick presses: forget bar 1, cancel the arm and a recording just started */
SE_ALWAYS_INLINE(se_reset)
static inline void se_reset(SeState *s)
{
    s->armed = 0;
    s->rec = 0;
    s->cs = SE_WAIT;
    s->hits = 0; s->misses = 0;
}

/* a beat line has just passed (s->beat is the new beat) */
SE_ALWAYS_INLINE(se_on_beat)
static inline void se_on_beat(SeState *s)
{
    int bars;
    if (s->cs == SE_WAIT || (s->beat & 3) != 0) return;
    bars = s->beat >> 2;
    if (s->rec && s->beat >= s->rec_end) se_finish(s);
    if (s->armed && !s->rec && se_mod(bars + s->rd, s->rb) == 0) {
        s->armed = 0;
        s->rec = 1;
        s->wi = 0;
        s->tail = 0;
        s->rec_end = s->beat + 4 * s->rd;
        if (s->has_loop) { s->pm = 0; se_jump(s, s->pi, 0); se_window(s); }
    }
}

/* fold a time between kicks into the tempo range by halving: returns the beats it spans
 * (1, 2, 4..) and the beat length in *T, or 0 if it does not fit (an off-beat kick) */
SE_ALWAYS_INLINE(se_fold)
static inline int se_fold(float ioi, const SeParams *P, float *T)
{
    int n = 1;
    while (ioi > P->Tmax * 1.03f && n < 64) { ioi *= 0.5f; n += n; }
    if (ioi < P->Tmin * 0.97f) return 0;
    *T = ioi;
    return n;
}

/* a kick started 'back' samples ago (it is judged a little after its onset) */
SE_ALWAYS_INLINE(se_kick)
static inline void se_kick(SeState *s, const SeParams *P, int back)
{
    float T = s->T, e, f = T, fb = (float)back;
    int nb, n;
    if (s->cs == SE_WAIT) {                      /* bar 1 */
        s->beat = 0; s->ph = SE_LEAD + fb; s->kb = 0; s->sk = back; s->s1 = back;
        s->hits = 0; s->misses = 0;
        s->cs = s->tl ? SE_LOCK : SE_ACQ;
        return;
    }
    e = s->ph - fb - SE_LEAD;                    /* kick relative to the grid    */
    nb = s->beat;
    if (e > 0.5f * T) { e -= T; nb++; }
    if (e < -0.5f * T) { e += T; nb--; }
    if (s->cs == SE_LOCK) {
        if (e < 0.12f * T && e > -0.12f * T) {   /* on the grid: follow it      */
            s->ph -= 0.25f * e;
            if (!P->fixed) s->T = T + 0.02f * e;
            s->kb = nb; s->misses = 0; s->sk = back;
        } else if (!P->fixed && se_fold((float)(s->sk - back), P, &f)) {
            /* off the grid a beat or two after the last kick: the tempo may have jumped
             * (a syncopated kick or a bass note between beats does not fold into range) */
            s->sk = back;
            if (s->misses > 0 && f - s->tm < 0.04f * f && s->tm - f < 0.04f * f) s->misses++;
            else s->misses = 1;
            s->tm = f;
            if (s->misses >= 4) {                /* four agree: re-lock there    */
                s->T = f; s->ph = SE_LEAD + fb; s->beat = nb; s->kb = nb; s->misses = 0;
            }
        }
    } else {                                     /* SE_ACQ: finding the tempo    */
        n = se_fold((float)(s->sk - back), P, &f);
        if (n == 0) {
            /* off the beat: wait for the next kick, measured from the same one. But a
             * gap longer than two beats that is no 1, 2, 4.. beats would leave every
             * later kick measured from that stale kick, and nothing would ever lock:
             * measure from this one instead. */
            if ((float)(s->sk - back) > 2.06f * P->Tmax) s->sk = back;
            return;
        }
        if (P->fixed) {
            if (f - T < 0.06f * T && T - f < 0.06f * T) s->hits++; else s->hits = 0;
        } else if (s->hits > 0 && f - T < 0.04f * T && T - f < 0.04f * T) {
            s->T = 0.7f * T + 0.3f * f; s->hits++;
        } else {
            s->T = f; s->hits = 1;
        }
        s->beat = s->kb + n; s->kb = s->beat; s->ph = SE_LEAD + fb; s->sk = back;
        if (s->hits >= 2) {
            /* the tempo is known well enough: count the beats since bar 1 from the time
             * (halving above only counts 1, 2, 4.. beats, wrong after a longer gap) */
            s->beat = (int)((float)(s->s1 - back) * se_recip(s->T) + 0.5f);
            s->kb = s->beat;
        }
        if (s->hits >= 4) { s->cs = SE_LOCK; s->tl = 1; s->misses = 0; }
    }
    if (s->T < P->Tmin) s->T = P->Tmin;
    if (s->T > P->Tmax) s->T = P->Tmax;
}

/* once per block: footswitch, knobs that act on the state */
SE_ALWAYS_INLINE(se_block)
static inline void se_block(SeState *s, const SeParams *P, float onoff)
{
    int on = s->on_prev ? (onoff > 0.3f) : (onoff > 0.7f);
    if (s->on_prev < 0) s->on_prev = on;         /* the first block: not a press */
    if (s->tap_age < SE_TAPWIN) s->tap_age++;
    if (on != s->on_prev) {
        s->on_prev = on;
        if (s->tap_age < SE_TAPWIN) s->taps++; else s->taps = 1;
        s->tap_age = 0;
        if (s->taps == 1 && !s->armed && !s->rec) {
            if (se_arm(s, P)) s->beep = 3 * SE_BEEP_LEN;
        } else if (s->taps == 3) se_reset(s);
    }
    if (s->T < P->Tmin) s->T = P->Tmin;
    if (s->T > P->Tmax) s->T = P->Tmax;
    if (P->roll != s->roll) {                    /* a new slice, or back to the loop */
        s->roll = P->roll;
        if (s->has_loop) { se_window(s); if (s->pi != s->pm) se_jump(s, s->pi, s->pm); }
    }
    s->mode = P->mode;
    if (s->pickup) {                             /* XFade takes over where it meets pa */
        float d = 100.0f * (P->xf - s->pa);
        int sg = (d > 0.0f) ? 1 : -1;
        if ((d < 1.0f && d > -1.0f) || (s->dsign != 0 && sg != s->dsign)) s->pickup = 0;
        else s->dsign = sg;
    }
}

SE_ALWAYS_INLINE(se_process)
static inline void se_process(SeState *s, const SeParams *P, float *buf, int n)
{
    int i;
    float pk = 0.0f, tgd, tgw, th;
    float d1 = s->d1, d2 = s->d2, dc = s->dc, gd = s->gd, gw = s->gw;
    float s1b = s->s1b, s1l = s->s1l, s2b = s->s2b, s2l = s->s2l, g, a1, a2, a3, b1, b2, b3;

    /* LoCut: two TPT state-variable high-passes (stable at any frequency), dampings
     * 2 / Q of a 4th-order Butterworth; the frequency glides between blocks */
    s->F += 0.1f * (P->F - s->F);
    g = s->F;
    a1 = se_recip(1.0f + g * (g + 1.847759f)); a2 = g * a1; a3 = g * a2;
    b1 = se_recip(1.0f + g * (g + 0.765367f)); b2 = g * b1; b3 = g * b2;
    for (i = 0; i < n; i++) {
        float in = buf[i], x, wet = 0.0f, h, out;

        /* kick band */
        d1 += P->lpc * (in - d1);
        d2 += P->lpc * (d1 - d2);
        dc += 0.0042742f * (d2 - dc);            /* 30 Hz */
        x = d2 - dc;
        if (x < 0.0f) x = -x;
        if (x > pk) pk = x;

        /* bar clock */
        s->ph += 1.0f;
        if (s->sk < 0x10000000) s->sk++;
        if (s->s1 < 0x10000000) s->s1++;
        if (s->ph >= s->T) { s->ph -= s->T; s->beat++; se_on_beat(s); }

        /* loop: read the playhead, record, then the voice fading out (it may read the
         * sample just recorded: the tail at the loop's first seam) */
        if (s->has_loop) { s->zm = se_rd(s, s->pi) + SE_PRE * s->zm; wet = s->zm; }
        if (s->rec || s->tail > 0) {
            se_wr(s, in);
            if (s->tail > 0) {
                s->tail--;
                if (s->tail == 0 && (s->wi & 31) != 0)          /* scale the last block */
                    se_flush(s, s->wi & ~31, s->wi & 31);
            } else if (s->wi >= SE_WMAX) se_finish(s);
        }
        s->xp = in;
        if (s->has_loop) {
            if (s->fade > 0) {
                float g = (float)s->fade * SE_XINV;
                s->zo = se_rd(s, s->po) + SE_PRE * s->zo;
                wet += g * (s->zo - wet);
                s->po++;
                s->fade--;
            }
            s->pi++;
            s->pm++;
            if (s->pm >= s->nj) {                /* a grid line, or the loop end */
                int nj;
                if (s->pm >= s->L) { s->pm = 0; s->rk = 0; } else s->rk++;
                if (s->rlen > 0.0f) {            /* Roll: back to the slice start */
                    nj = (int)((float)(s->rk + 1) * s->rlen + 0.5f);
                    s->nj = (nj > s->L) ? s->L : nj;
                    se_jump(s, s->pi, s->rs);
                } else {
                    s->nj = s->L;
                    se_jump(s, s->pi, s->pm);
                }
            } else if (s->pi >= s->L) se_jump(s, s->pi, s->rs);   /* a slice past the end */
        }

        /* LoCut (s?b, s?l: the band and low-pass integrator states) */
        {
            float v1, v2, v3;
            v3 = wet - s1l;
            v1 = a1 * s1b + a2 * v3;
            v2 = s1l + a2 * s1b + a3 * v3;
            s1b = 2.0f * v1 - s1b; s1l = 2.0f * v2 - s1l;
            h = wet - 1.847759f * v1 - v2;
            v3 = h - s2l;
            v1 = b1 * s2b + b2 * v3;
            v2 = s2l + b2 * s2b + b3 * v3;
            s2b = 2.0f * v1 - s2b; s2l = 2.0f * v2 - s2l;
            h = h - 0.765367f * v1 - v2;
        }
        if (P->cut) wet = h;

        /* gains: live only before the first loop, 100 % loop until picked up */
        if (!s->has_loop) { tgd = 1.0f; tgw = 0.0f; }
        else if (s->pickup) {                    /* JUMP, AUTO: the automatic XFade */
            if (s->ahold > 0) s->ahold--;
            else if (s->ahold == 0 && s->pa > 0.0f) { s->pa -= s->astep; if (s->pa < 0.0f) s->pa = 0.0f; }
            tgd = 2.0f - 2.0f * s->pa; if (tgd > 1.0f) tgd = 1.0f;
            tgw = 2.0f * s->pa;        if (tgw > 1.0f) tgw = 1.0f;
        }
        else { tgd = P->tgd; tgw = P->tgw; }
        gd += SE_GLIDE * (tgd - gd);
        gw += SE_GLIDE * (tgw - gw);
        out = gd * in + gw * wet;

        /* double beep: Bars did not fit, a shorter loop is armed */
        if (s->beep > 0) {
            if (s->beep > 2 * SE_BEEP_LEN || s->beep <= SE_BEEP_LEN) {
                s->bs += SE_BEEP_W * s->bc;
                s->bc -= SE_BEEP_W * s->bs;
                out += SE_BEEP_AMP * s->bs;
            } else { s->bs = 0.0f; s->bc = 1.0f; }
            s->beep--;
        }
        buf[i] = out;
    }
    s->d1 = d1; s->d2 = d2; s->dc = dc; s->gd = gd; s->gw = gw;
    s->s1b = s1b; s->s1l = s1l; s->s2b = s2b; s->s2l = s2l;

    /* kick detector, once per block. An ONSET is the band's envelope jumping above R x
     * its own recent level (a 40 ms average, which a kick's tail and a held bass note
     * keep up with, so only a fresh hit stands out). 20 ms later it is judged by the peak
     * it reached: a kick if at least 70 % of the loudest onset in the last 1..2 s (so bass
     * notes and toms clearly quieter than the kick do not count once a kick was heard). */
    if (pk > s->env) s->env = pk; else s->env *= 0.98202f;    /* 10 ms release    */
    if (++s->kn >= 5512) { s->kn = 0; s->kp = s->kw; s->kw = 0.0f; }
    if (s->pend >= 0) {
        if (s->env > s->pstr) s->pstr = s->env;
        s->pend++;
        if (s->pend >= SE_JUDGE) {
            if (s->pstr >= 0.7f * s->kw && s->pstr >= 0.7f * s->kp) se_kick(s, P, 8 * s->pend);
            if (s->pstr > s->kw) s->kw = s->pstr;
            s->pend = -1;
        }
    }
    th = P->R * s->slow;
    if (P->autoR) {
        /* AUTO: start strict (2 x); while nothing has locked, step down every 4 s
         * (1.7, 1.5, 1.3 x, then back to 2 x): a boomy kick whose tail is still loud
         * at the next beat only jumps ~1.5..1.9 x, while bass and snares are better
         * kept out by the strict setting when it works. Once locked, the step stays. */
        float r = 2.0f;
        if (s->ri == 1) r = 1.7f;
        if (s->ri == 2) r = 1.5f;
        if (s->ri == 3) r = 1.3f;
        th = r * s->slow;
        if (s->cs == SE_LOCK) s->hunt = 0;
        else if (++s->hunt >= SE_HUNT) { s->hunt = 0; s->ri = (s->ri + 1) & 3; }
    }
    if (s->refr > 0) s->refr--;
    if (!s->ready && s->env < th) s->ready = 1;
    if (s->ready && s->refr == 0 && s->env > th && s->env > 0.002f && s->pend < 0) {
        s->ready = 0;
        s->refr = P->refr;
        s->pend = 0;
        s->pstr = s->env;
    }
    s->slow += 0.0045f * (s->env - s->slow);                  /* 40 ms            */
}

/* ---- on-screen text ------------------------------------------------------ */
SE_ALWAYS_INLINE(se_put_int)
static inline int se_put_int(int n, char *out)
{
    int k = 0, h = 0, t = 0, len = 0;
    while (n >= 1000) { n -= 1000; k++; }
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (k > 0) { out[len] = (char)('0' + k); len++; }
    if (k > 0 || h > 0) { out[len] = (char)('0' + h); len++; }
    if (k > 0 || h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

SE_ALWAYS_INLINE(se_put4)
static inline int se_put4(char *out, int a, int b, int c, int d)
{
    int n = 0;
    out[0] = (char)a; n = 1;
    if (b) { out[1] = (char)b; n = 2; }
    if (c) { out[2] = (char)c; n = 3; }
    if (d) { out[3] = (char)d; n = 4; }
    out[n] = 0;
    return n;
}

/* number followed by "Hz" (at most 5 characters for 0..999) */
SE_ALWAYS_INLINE(se_put_hz)
static inline int se_put_hz(int f, char *out)
{
    int n = se_put_int(f, out);
    out[n] = 'H'; out[n + 1] = 'z'; out[n + 2] = 0;
    return n + 2;
}

/* knob 0 XFade: "LIVE", 1..99, "LOOP" */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value == 0u) return se_put4(out, 'L', 'I', 'V', 'E');
    if (value >= 100u) return se_put4(out, 'L', 'O', 'O', 'P');
    return se_put_int((int)value, out);
}

/* knob 1 LoCut: "OFF", "21Hz".."999Hz", "1.0k".."2.0k" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int f, d = 0, t;
    if (value == 0u) return se_put4(out, 'O', 'F', 'F', 0);
    if (value > 100u) value = 100u;
    f = (int)(20.0f * se_exp2((float)(int)value * 0.06643856f) + 0.5f);
    if (f < 1000) return se_put_hz(f, out);
    f += 50;                                     /* round to 0.1 kHz */
    while (f >= 1000) { f -= 1000; d++; }
    t = 0;
    while (f >= 100) { f -= 100; t++; }
    out[0] = (char)('0' + d); out[1] = '.'; out[2] = (char)('0' + t); out[3] = 'k'; out[4] = 0;
    return 4;
}

/* knob 2 Roll: OFF, 1BAR, 2BEAT, 1BEAT, 1/2BT */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value == 0u) return se_put4(out, 'O', 'F', 'F', 0);
    if (value == 1u) return se_put4(out, '1', 'B', 'A', 'R');
    if (value >= 4u) {
        out[0] = '1'; out[1] = '/'; out[2] = '2'; out[3] = 'B'; out[4] = 'T'; out[5] = 0;
        return 5;
    }
    out[0] = (value == 2u) ? '2' : '1'; out[1] = 'B'; out[2] = 'E'; out[3] = 'A'; out[4] = 'T';
    out[5] = 0;
    return 5;
}

/* knob 3 Bars: screen 0..7 shown as 1..8 */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value > 7u) value = 7u;
    return se_put_int((int)value + 1, out);
}

/* knobs 4, 5 LoBPM, HiBPM: below 40 reads as 40 */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value < 40u) value = 40u;
    return se_put_int((int)value, out);
}

int ZDL_GetLabel_5(unsigned int value, char *out)
{
    if (value < 40u) value = 40u;
    return se_put_int((int)value, out);
}

/* knob 6 Thrsh: "AUTO", 1..100 */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    if (value == 0u) return se_put4(out, 'A', 'U', 'T', 'O');
    return se_put_int((int)value, out);
}

/* knob 7 Listn: "AUTO", "50Hz".."150Hz" */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    if (value == 0u) return se_put4(out, 'A', 'U', 'T', 'O');
    if (value > 101u) value = 101u;
    return se_put_hz(49 + (int)value, out);
}

/* knob 8 Mode: JUMP, AUTO, MANU */
int ZDL_GetLabel_8(unsigned int value, char *out)
{
    if (value >= 2u) return se_put4(out, 'M', 'A', 'N', 'U');
    if (value == 1u) return se_put4(out, 'A', 'U', 'T', 'O');
    return se_put4(out, 'J', 'U', 'M', 'P');
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef SEGUE_HOST_TEST

#include "segue_params.h"

#ifndef SEGUE_AUDIO_FUNC
#define SEGUE_AUDIO_FUNC Fx_DLY_Segue
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SE_CODE_SECTION(SEGUE_AUDIO_FUNC)
void SEGUE_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    SeState *s;
    SeParams P;
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
    if ((end - base) < sizeof(SeState) || span < (end - base)) return;
    if (stateBase + sizeof(SeState) > end) return;

    s = (SeState *)stateBase;

    u[0] = se_ui(params[SEGUE_XFADE_SLOT], (float)SEGUE_XFADE_UI_DEFAULT, 100.0f);
    u[1] = se_ui(params[SEGUE_LOCUT_SLOT], (float)SEGUE_LOCUT_UI_DEFAULT, 100.0f);
    u[2] = se_ui(params[SEGUE_ROLL_SLOT],  (float)SEGUE_ROLL_UI_DEFAULT,  4.0f);
    u[3] = se_ui(params[SEGUE_BARS_SLOT],  (float)SEGUE_BARS_UI_DEFAULT,  7.0f);
    u[4] = se_ui(params[SEGUE_LOBPM_SLOT], (float)SEGUE_LOBPM_UI_DEFAULT, 240.0f);
    u[5] = se_ui(params[SEGUE_HIBPM_SLOT], (float)SEGUE_HIBPM_UI_DEFAULT, 240.0f);
    u[6] = se_ui(params[SEGUE_THRSH_SLOT], (float)SEGUE_THRSH_UI_DEFAULT, 100.0f);
    u[7] = se_ui(params[SEGUE_LISTN_SLOT], (float)SEGUE_LISTN_UI_DEFAULT, 101.0f);
    u[8] = se_ui(params[SEGUE_MODE_SLOT],  (float)SEGUE_MODE_UI_DEFAULT,  2.0f);

    se_prepare(&P, u);
    if (s->magic != SE_MAGIC) se_init(s, 0.5f * (P.Tmin + P.Tmax));
    se_block(s, &P, params[0]);                  /* on/off only counts presses */
    se_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* SEGUE_HOST_TEST */
