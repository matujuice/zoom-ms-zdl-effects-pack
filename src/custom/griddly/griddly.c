/*
 * griddly.c - "GridDly": one tempo-synced delay with six engines on a Type knob, mono
 *
 * Shown on the pedal as GridDly (Luca picked the name 2026-10-07; first built as BarDelay).
 * Scoped with Luca 2026-10-06/07 (/mnt/project-files/ideas/delay-scope.md): six engines,
 * Time as note values like DualShft, Tempo on knob 8 with FOLLW, Tail toggle (decay or no
 * decay when switched off, like the factory delays), no Hold.
 *
 * SIGNAL
 *   in -> ring (16-bit, 7.9 s) -> read at the delay time -> wet out
 *                 ^                         |
 *                 +-- saturate <- HP <- LP <- feedback --+
 *   What goes into the ring is in + Fdbk x (filtered repeat), soft-clipped, so every repeat
 *   passes the loop filters and the saturator again. The Duck gain only touches the wet
 *   output, never the loop.
 *
 * ENGINES (Type knob; Char means something different on each)
 *   0 DIGI   clean repeats. Tone = low-pass on the loop. Char = slow modulation of the read
 *            point (0.5 Hz, up to 1.5 ms: a slight chorus on the repeats; 0 = none).
 *   1 TAPE   darker loop, wow (0.6 Hz) and flutter (6.5 Hz), mild saturation; a Time or
 *            Tempo change glides the read head over about 0.2 s, so the pitch bends like tape.
 *            Char = wow and flutter depth (100 = seasick, about +-40 cents of wow).
 *   2 DUB    high-pass and low-pass in the loop (Tone moves both: 100..400 Hz and 0.5..4 kHz),
 *            harder saturation; Fdbk near 100 self-oscillates and the saturator holds it.
 *            Char = drive: the higher, the lower the loop's ceiling and the dirtier the
 *            repeats (a wet make-up gain keeps them about as loud).
 *   3 REVRS  each chunk of Time is played backwards: the chunk that ended at a chunk start
 *            plays reversed until the next one. Chunks restart on a Tempo twin flip (or a bar
 *            from the bar tag on FOLLW), so with bar sync they start on the beat. A Time
 *            change moves the clock at once, but the read keeps the running chunk's length
 *            until the next chunk (that chunk plays a little slower or faster, no click). Char = the fade at each chunk's edges, 1 % of
 *            the chunk (choppy) to 25 % (smooth). Reads reach back 2 x Time, so synced times
 *            are halved until twice the time fits (1bar below about 61 BPM, 2bar below 122).
 *   4 TAPS   three taps inside Time, levels falling (0.85, 0.65, 0.5); only the last one
 *            feeds back. Char picks the pattern (fractions of Time):
 *              0..24 1/4 1/2 1   25..49 3/8 3/4 1   50..74 1/3 2/3 1   75..100 1/2 3/4 1
 *   5 LOFI   each pass is quantised and sample-held, so the repeats crumble more every time.
 *            Char = crush: 12 bits and full rate at 0, 4 bits and 1/8 rate at 100.
 *   Loop gain at Fdbk 100: DIGI 0.95, TAPE 1.08, DUB 1.25, REVRS 0.9, TAPS 0.85, LOFI 1.0
 *   (before the loop filters' losses), so only TAPE, DUB and LOFI can run on forever.
 *   All engines share one ring, one loop filter pair and one saturator; the per-engine
 *   numbers are blended from 0/1 flags in gd_prepare, so the sample loop has no branch on
 *   Type except the REVRS / TAPS / LOFI read and write paths.
 *
 * TIME
 *   Screen 0..100 free: ms = 12 + 0.0988 x screen^2 (12 ms .. 1 s, as DualShft).
 *   101..113 synced to Tempo: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar 2bar.
 *   A synced time longer than the ring is halved until it fits (2bar below about 61 BPM;
 *   for REVRS see above). Away from TAPE a Time change crossfades from the old read point
 *   to the new one over 23 ms (no pitch bend, no click).
 *
 * TAIL (knob 9) and switching off
 *   Off, the input always passes untouched. Tail ON: the input stops entering the ring and
 *   the repeats ring out and decay with the feedback, as the factory delays do with Tail on.
 *   Tail OFF: the repeats stop at once; the ring keeps recording the dry input (no feedback),
 *   so switching back on starts echoing what you just played. The beat clock keeps running
 *   and follows twin flips while off. Tail ON needs the pedal to call a switched-off effect,
 *   which the synced effects already rely on for their clocks; it passed on the pedal
 *   (2026-10-07).
 *
 * DUCK
 *   A peak follower on the input (instant attack, about 0.23 s release) pulls the wet level
 *   down while you play, by up to Duck % (full depth from about -10 dBFS), and the repeats
 *   swell back in the gaps. Duck 0 = off.
 *
 * MEMORY
 *   16-bit ring (scale 16384, so +-2.0 fits) of 348000 samples = 7.9 s = 696 KB of the at
 *   least 705 KB arena, as Scrub. Cleared 2048 samples per block after loading (about 31 ms,
 *   dry meanwhile), so stale arena data is never played. The longest read is 346000 samples
 *   plus the TAPE modulation (under 450 samples).
 *
 * KNOBS (screen values; 9 = the maximum, 3 pages x 3)
 *   0 Type   0..5    DIGI TAPE DUB REVRS TAPS LOFI
 *   1 Time   0..113  12ms .. 1.00s, then 1/32 .. 1bar, 2bar (above)
 *   2 Fdbk   0..100  repeats; 100 = the engine's loop gain above
 *   3 Tone   0..100  dark to bright (LP 0.8..15.7 kHz; TAPE 0.6..7.8 kHz; DUB the band)
 *   4 Char   0..100  per engine (above); the label is a plain number: a label can't see Type
 *   5 Duck   0..100  how far the repeats dip while you play
 *   6 Mix    0..100  dry/wet crossfade, DJ style: dry full up to 50, wet full from 50
 *   7 Tempo  0..441  BPM 40..240 (0..39 = FOLLOW, see BAR TAG); 241..441 is a twin copy
 *                    (BPM = screen - 201). Flipping between a BPM and its twin restarts the
 *                    REVRS chunk without changing the tempo. A plain tempo change does not.
 *                    Read as raw x 100 (gd_tempo_ui), never through the 3.05 guess.
 *   8 Tail   0..1    OFF / ON (above)
 *
 * BAR TAG (src/custom/common/drytag.h, docs/TEMPO-SYNC.md "Bar tag")
 *   While Mozaic flips this effect's Tempo, it writes a bar tag into the Dry buffer's right
 *   half for the slots after it. With Tempo on 0..39 = FOLLOW (shown FOLLW) it follows a tag
 *   from earlier slots: their BPM (120 until one is heard, kept if the sender goes away),
 *   and each new bar restarts it as a twin flip of its own knob would. On any BPM it ignores
 *   the tag. In slots 1-3, FOLLOW needs Mozaic's Send knob on another slot.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch or if-chains on Type in the audio path, no double / long long,
 * no float-to-unsigned casts, every helper forced inline. Built and passed on the MS-60B
 * (2026-10-07, every engine and Tail); CPU never measured.
 */

#include <stdint.h>
#include "../common/drytag.h"

#ifdef __TI_COMPILER_VERSION__
#define GD_DO_PRAGMA(x) _Pragma(#x)
#define GD_EXPAND_PRAGMA(x) GD_DO_PRAGMA(x)
#define GD_ALWAYS_INLINE(fn) GD_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define GD_CODE_SECTION(fn) GD_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define GD_ALWAYS_INLINE(fn)
#define GD_CODE_SECTION(fn)
#endif

#define GD_MAGIC      0x47444C31u        /* "GDL1": change whenever GdState changes      */
#define GD_N          348000             /* ring length: 7.9 s at 44.1 kHz              */
#define GD_MAXD       346000.0f          /* longest delay (samples), room for modulation */
#define GD_TEMPO_MAX  441.0f
#define GD_TEMPO_TWIN 201.0f
#define GD_CLEAR_BLK  2048
#define GD_TO16       16384.0f
#define GD_FROM16     6.1035156e-5f      /* 1 / 16384 */
#define GD_MS         44.1f              /* samples per ms */
#define GD_XF_STEP    9.765625e-4f       /* Time crossfade: 1024 samples = 23 ms */
#define GD_W2C        2.0555e-4f         /* 2 pi / 44100 / ln 2: one-pole coef from Hz */
#define GD_ENV_REL    0.9999f            /* duck follower release, about 0.23 s */
#define GD_DG_RATE    0.003f             /* duck gain smoothing, about 7.5 ms */
#define GD_WL_RATE    0.01f              /* wet level ramp on switch-on, about 2 ms */
#define GD_GLIDE      1.134e-4f          /* TAPE head glide, about 0.2 s */

typedef struct {
    unsigned int magic;
    int   clr;             /* samples of the ring cleared so far              */
    int   wp;              /* next write index; age a = buf[wp - a]           */
    int   twin;            /* Tempo on its twin copy (1) or not (0); -1 = not read yet */
    float dCur, dOld;      /* read delay now and before a Time change (samples) */
    float xf;              /* Time crossfade 0..1 (1 = done)                  */
    float ph;              /* chunk phase 0..1 (REVRS chunks, runs on every engine) */
    float L;               /* chunk length the REVRS read uses, latched at each chunk start */
    float lp, hs;          /* loop low-pass state, loop high-pass's low part  */
    float wow, flt;        /* TAPE / DIGI LFO phases 0..1                     */
    float env, dg;         /* duck follower and duck gain                     */
    float wl;              /* wet level: ramps in on switch-on                */
    float hold, hacc;      /* LOFI sample-hold value and its clock            */
    DtSync sync;           /* bar tag to and from other slots (drytag.h)      */
    short buf[GD_N];
} GdState;

typedef struct {
    int   rev, taps, lofi, tape, on;
    float dT;              /* delay time the knobs ask for (samples)          */
    float L;               /* REVRS chunk length = dT                         */
    float inG, fb, cl, ch, drive, dinv, mk;
    float modA, modInc, fltA, fltInc;
    float f1, f2;          /* TAPS fractions of Time                          */
    float fadeInv;         /* REVRS: 1 / edge fade (fraction of a chunk)      */
    float qk, qinv, hinc;  /* LOFI quantiser scale and its inverse, hold clock */
    float duck, dryG, wetG;
} GdParams;

GD_ALWAYS_INLINE(gd_ui)
static inline float gd_ui(float raw, float def_ui, float max_ui)
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

GD_ALWAYS_INLINE(gd_tempo_ui)
static inline float gd_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > GD_TEMPO_MAX) ui = GD_TEMPO_MAX;
    return ui;
}

GD_ALWAYS_INLINE(gd_tempo_bpm)
static inline float gd_tempo_bpm(float ui)
{
    if (ui > 240.0f) ui -= GD_TEMPO_TWIN;
    if (ui < 40.0f) ui = 40.0f;
    if (ui > 240.0f) ui = 240.0f;
    return ui;
}

GD_ALWAYS_INLINE(gd_exp2)
static inline float gd_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int   n = (int)x;
    float f, r;
    if (x < 0.0f && (float)n != x) n--;          /* floor for negative x */
    f = x - (float)n;
    r = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    c.u = ((unsigned int)(n + 127)) << 23;
    return r * c.f;
}

/* 1 / x for x > 0: a first guess from the float bits, then three Newton steps */
GD_ALWAYS_INLINE(gd_recip)
static inline float gd_recip(float x)
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

/* one-pole coefficient 1 - e^(-2 pi fc / fs) */
GD_ALWAYS_INLINE(gd_coef)
static inline float gd_coef(float hz)
{
    return 1.0f - gd_exp2(-hz * GD_W2C);
}

/* Synced Time 101..113 in beats: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar 2bar */
GD_ALWAYS_INLINE(gd_note_beats)
static inline float gd_note_beats(int d)
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
    if (d == 11) return 4.0f;
    return 8.0f;
}

/* bipolar sine-like LFO from a phase 0..1 (parabola, within 6 %) */
GD_ALWAYS_INLINE(gd_lfo)
static inline float gd_lfo(float p)
{
    float t = p + p - 1.0f, a = (t < 0.0f) ? -t : t;
    return -4.0f * t * (1.0f - a);
}

/* soft clip, flat at +-1 from |x| = 1.5 */
GD_ALWAYS_INLINE(gd_sat)
static inline float gd_sat(float x)
{
    if (x > 1.5f) x = 1.5f;
    if (x < -1.5f) x = -1.5f;
    return x - 0.14814815f * x * x * x;
}

GD_ALWAYS_INLINE(gd_read)
static inline float gd_read(const short *b, int wp, float age)
{
    int a0 = (int)age, j, j1;
    float fr = age - (float)a0, x0, x1;
    j = wp - a0;
    if (j < 0) j += GD_N;
    j1 = j - 1;
    if (j1 < 0) j1 += GD_N;
    x0 = (float)b[j];
    x1 = (float)b[j1];
    return (x0 + fr * (x1 - x0)) * GD_FROM16;
}

/* read at the new delay, crossfaded from the old one while a Time change settles */
GD_ALWAYS_INLINE(gd_xread)
static inline float gd_xread(const short *b, int wp, float aN, float aO, float xf)
{
    float y = gd_read(b, wp, aN);
    if (xf < 1.0f) y = xf * y + (1.0f - xf) * gd_read(b, wp, aO);
    return y;
}

GD_ALWAYS_INLINE(gd_write)
static inline void gd_write(GdState *s, float v)
{
    v *= GD_TO16;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    s->buf[s->wp] = (short)(int)v;
    s->wp++;
    if (s->wp >= GD_N) s->wp = 0;
}

GD_ALWAYS_INLINE(gd_init)
static inline void gd_init(GdState *s)
{
    s->clr = 0; s->wp = 0; s->twin = -1;
    s->dCur = 0.0f; s->dOld = 0.0f; s->xf = 1.0f;
    s->ph = 0.0f; s->L = 0.0f;
    s->lp = 0.0f; s->hs = 0.0f; s->wow = 0.0f; s->flt = 0.25f;
    s->env = 0.0f; s->dg = 1.0f; s->wl = 0.0f; s->hold = 0.0f; s->hacc = 0.0f;
    dt_sync_init(&s->sync);
    s->magic = GD_MAGIC;
}

GD_ALWAYS_INLINE(gd_clearing)
static inline int gd_clearing(GdState *s)
{
    int i, n = s->clr;
    if (n >= GD_N) return 0;
    for (i = 0; i < GD_CLEAR_BLK && n < GD_N; i++) { s->buf[n] = 0; n++; }
    s->clr = n;
    return 1;
}

/* Knobs (u[] = screen values) -> per-block numbers. Engine numbers are blended from 0/1
 * flags so no branch on Type is needed (a dense if/else on an int can become a jump table). */
GD_ALWAYS_INLINE(gd_prepare)
static inline void gd_prepare(GdParams *P, const float *u, int on)
{
    int type = (int)(u[0] + 0.5f), n = (int)(u[1] + 0.5f), z, bits;
    float eD = (float)(type == 0), eT = (float)(type == 1), eB = (float)(type == 2);
    float eR = (float)(type == 3), eP = (float)(type == 4), eL = (float)(type == 5);
    float d, lim, tone = u[3] * 0.01f, ch = u[4] * 0.01f, m, fbMax, lpHz, hpHz, tail;

    P->rev = (type == 3); P->taps = (type == 4); P->lofi = (type == 5); P->tape = (type == 1);
    P->on = on;

    /* Time */
    lim = P->rev ? GD_MAXD * 0.5f : GD_MAXD;
    if (n > 100) {
        d = gd_note_beats(n - 101) * 2646000.0f * gd_recip(gd_tempo_bpm(u[7]));
        while (d > lim) d *= 0.5f;
    } else {
        d = (float)(int)(12.0f + 0.0988f * (float)(n * n) + 0.5f) * GD_MS;
    }
    P->dT = d;
    P->L = d;

    /* loop */
    fbMax = 0.95f * eD + 1.08f * eT + 1.25f * eB + 0.9f * eR + 0.85f * eP + 1.0f * eL;
    P->fb = u[2] * 0.01f * fbMax;
    lpHz = (eD + eR + eP + eL) * 800.0f * gd_exp2(4.3f * tone)
         + eT * 600.0f * gd_exp2(3.7f * tone) + eB * 500.0f * gd_exp2(3.0f * tone);
    hpHz = (eD + eR + eP + eL) * 25.0f + eT * 50.0f + eB * 100.0f * gd_exp2(2.0f * tone);
    P->cl = gd_coef(lpHz);
    P->ch = gd_coef(hpHz);
    P->drive = 0.5f * (eD + eR + eP) + 1.2f * eT + (1.0f + 3.0f * ch) * eB + 0.8f * eL;
    P->dinv = gd_recip(P->drive);
    P->mk = 1.0f + 1.5f * ch * eB;

    /* modulation: DIGI slow and slight, TAPE wow + flutter */
    P->modA = eD * 66.0f * ch + eT * (2.0f + 200.0f * ch);
    P->modInc = eD * 1.1338e-5f + eT * 1.3605e-5f;          /* 0.5 Hz, 0.6 Hz */
    P->fltA = eT * (0.5f + 8.0f * ch);
    P->fltInc = 1.4739e-4f;                                  /* 6.5 Hz */

    /* TAPS pattern */
    z = (int)(ch * 3.999f);
    P->f1 = 0.25f * (float)(z == 0) + 0.375f * (float)(z == 1) + 0.33333334f * (float)(z == 2) + 0.5f * (float)(z == 3);
    P->f2 = 0.5f * (float)(z == 0) + 0.75f * (float)(z == 1) + 0.6666667f * (float)(z == 2) + 0.75f * (float)(z == 3);

    /* REVRS edge fade 1 % .. 25 % of the chunk */
    P->fadeInv = gd_recip(0.01f + 0.24f * ch);

    /* LOFI: 12 .. 4 bits, hold 1 .. 8 samples */
    bits = 12 - (int)(8.0f * ch + 0.5f);
    P->qk = (float)(1 << (bits - 1));
    P->qinv = gd_recip(P->qk);
    P->hinc = gd_recip(1.0f + 7.0f * ch);

    /* Duck, Mix, Tail */
    P->duck = u[5] * 0.01f;
    m = u[6] * 0.01f;
    P->dryG = 2.0f - 2.0f * m; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * m;        if (P->wetG > 1.0f) P->wetG = 1.0f;
    P->wetG *= P->mk;
    P->inG = 1.0f;
    if (!on) {                                   /* switched off: input passes untouched */
        tail = (u[8] >= 0.5f) ? 1.0f : 0.0f;
        P->dryG = 1.0f;
        P->inG = 1.0f - tail;                    /* Tail ON: nothing new goes in   */
        P->fb *= tail;                           /* Tail OFF: ring records the dry input */
        P->wetG *= tail;
    }
}

/* restart the beat clock (REVRS chunk) and latch the chunk length */
GD_ALWAYS_INLINE(gd_restart)
static inline void gd_restart(GdState *s, const GdParams *P)
{
    s->ph = 0.0f;
    s->L = P->L;
}

GD_ALWAYS_INLINE(gd_process)
static inline void gd_process(GdState *s, const GdParams *P, float *buf, int n)
{
    int i, wp;
    float ph = s->ph, L = s->L, invL = gd_recip(P->L), lp = s->lp, hs = s->hs;
    float wow = s->wow, flt = s->flt, env = s->env, dg = s->dg, wl = s->wl;
    float dCur = s->dCur, xf = s->xf, wlT = P->wetG;

    if (L <= 0.0f) L = P->L;
    if (P->rev && L > GD_MAXD * 0.5f) L = P->L;   /* chunk latched on another Type: too long to read backwards */
    if (dCur <= 0.0f) dCur = P->dT;
    if (P->tape) xf = 1.0f;
    else if (xf >= 1.0f && (P->dT - dCur > 0.5f || dCur - P->dT > 0.5f)) {
        s->dOld = dCur; dCur = P->dT; xf = 0.0f;  /* Time changed: crossfade to it */
    }
    if (!P->on && wlT == 0.0f) wl = 0.0f;         /* Tail OFF: repeats stop at once */

    for (i = 0; i < n; i++) {
        float x = buf[i], ax = (x < 0.0f) ? -x : x, y, v, mod, tg;

        /* duck: peak follower on the input */
        env = (ax > env) ? ax : env * GD_ENV_REL;
        tg = env * 3.0f; if (tg > 1.0f) tg = 1.0f;
        dg += (1.0f - P->duck * tg - dg) * GD_DG_RATE;
        wl += (wlT - wl) * GD_WL_RATE;

        /* beat clock: runs at the current Time; the REVRS read keeps the chunk length it
         * started with, so a Time change mid-chunk slows or speeds that chunk, never clicks */
        ph += invL;
        if (ph >= 1.0f) { ph -= 1.0f; L = P->L; }

        /* LFOs (offsets are >= 0, so the read never gets nearer than the set time) */
        wow += P->modInc; if (wow >= 1.0f) wow -= 1.0f;
        flt += P->fltInc; if (flt >= 1.0f) flt -= 1.0f;
        mod = P->modA * (1.0f + gd_lfo(wow)) + P->fltA * (1.0f + gd_lfo(flt));
        if (P->tape) dCur += (P->dT - dCur) * GD_GLIDE;

        wp = s->wp;
        if (P->rev) {
            float w = ph * P->fadeInv, w2 = (1.0f - ph) * P->fadeInv;
            if (w2 < w) w = w2;
            if (w > 1.0f) w = 1.0f;
            y = w * gd_read(s->buf, wp, (ph + ph) * L + 1.0f);
            v = y;
        } else {
            y = gd_xread(s->buf, wp, dCur + mod, s->dOld + mod, xf);
            v = y;
            if (P->taps)
                y = 0.5f * y + 0.85f * gd_xread(s->buf, wp, P->f1 * dCur + 1.0f, P->f1 * s->dOld + 1.0f, xf)
                             + 0.65f * gd_xread(s->buf, wp, P->f2 * dCur + 1.0f, P->f2 * s->dOld + 1.0f, xf);
            if (xf < 1.0f) { xf += GD_XF_STEP; if (xf > 1.0f) xf = 1.0f; }
        }

        /* feedback loop: low-pass, high-pass, saturate (LOFI: crush and hold) */
        lp += P->cl * (v - lp);
        hs += P->ch * (lp - hs);
        v = P->inG * x + P->fb * (lp - hs);
        v = gd_sat(v * P->drive) * P->dinv;
        if (P->lofi) {
            s->hacc += P->hinc;
            if (s->hacc >= 1.0f) {
                s->hacc -= 1.0f;
                s->hold = (float)(int)(v * P->qk + ((v < 0.0f) ? -0.5f : 0.5f)) * P->qinv;
            }
            v = s->hold;
        }
        gd_write(s, v);

        buf[i] = P->dryG * x + wl * dg * y;
    }
    s->ph = ph; s->L = L; s->lp = lp; s->hs = hs;
    s->wow = wow; s->flt = flt; s->env = env; s->dg = dg; s->wl = wl;
    s->dCur = dCur; s->xf = xf;
}

/* ------------------------------------------------------------------ */
/* On-screen text: ZDL_GetLabel_<knob index>(screen value, 8-byte out),  */
/* characters one by one (no string literals, no tables), at most 5.     */
/* Fdbk, Tone, Char, Duck and Mix show plain numbers.                    */
/* ------------------------------------------------------------------ */

GD_ALWAYS_INLINE(gd_put4)
static inline int gd_put4(char *out, int a, int b, int c, int d, int e)
{
    int n = 3;
    out[0] = (char)a; out[1] = (char)b; out[2] = (char)c;
    if (d) { out[3] = (char)d; n = 4; }
    if (e) { out[4] = (char)e; n = 5; }
    out[n] = 0;
    return n;
}

int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 5u) return gd_put4(out, 'L', 'O', 'F', 'I', 0);
    if (value == 4u) return gd_put4(out, 'T', 'A', 'P', 'S', 0);
    if (value == 3u) return gd_put4(out, 'R', 'E', 'V', 'R', 'S');
    if (value == 2u) return gd_put4(out, 'D', 'U', 'B', 0, 0);
    if (value == 1u) return gd_put4(out, 'T', 'A', 'P', 'E', 0);
    return gd_put4(out, 'D', 'I', 'G', 'I', 0);
}

/* Time: 12ms .. 1.00s, then 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2 1bar 2bar */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int v, ms, h = 0, t = 0, len = 0;
    if (value > 113u) value = 113u;
    if (value > 100u) {
        v = (int)value - 101;
        if (v == 12) return gd_put4(out, '2', 'b', 'a', 'r', 0);
        if (v == 11) return gd_put4(out, '1', 'b', 'a', 'r', 0);
        if (v == 0)  return gd_put4(out, '1', '/', '3', '2', 0);
        if (v == 1)  return gd_put4(out, '1', '/', '1', '6', 'T');
        if (v == 2)  return gd_put4(out, '1', '/', '1', '6', 0);
        if (v == 3)  return gd_put4(out, '1', '/', '8', 'T', 0);
        if (v == 4)  return gd_put4(out, '1', '/', '1', '6', '.');
        if (v == 5)  return gd_put4(out, '1', '/', '8', 0, 0);
        if (v == 6)  return gd_put4(out, '1', '/', '4', 'T', 0);
        if (v == 7)  return gd_put4(out, '1', '/', '8', '.', 0);
        if (v == 8)  return gd_put4(out, '1', '/', '4', 0, 0);
        if (v == 9)  return gd_put4(out, '1', '/', '4', '.', 0);
        return gd_put4(out, '1', '/', '2', 0, 0);
    }
    v = (int)value;
    ms = (int)(12.0f + 0.0988f * (float)(v * v) + 0.5f);
    if (ms >= 1000) return gd_put4(out, '1', '.', '0', '0', 's');
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

/* Tempo: FOLLW on 0..39, else the BPM on both copies */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value <= 39u) return dt_follow_text(out);
    if (value > 441u) value = 441u;
    n = (int)gd_tempo_bpm((float)(int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

int ZDL_GetLabel_8(unsigned int value, char *out)
{
    if (value >= 1u) { out[0] = 'O'; out[1] = 'N'; out[2] = 0; return 2; }
    return gd_put4(out, 'O', 'F', 'F', 0, 0);
}

#ifndef GRIDDLY_HOST_TEST

#include "griddly_params.h"

#ifndef GRIDDLY_AUDIO_FUNC
#define GRIDDLY_AUDIO_FUNC Fx_DLY_GridDly
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

GD_CODE_SECTION(GRIDDLY_AUDIO_FUNC)
void GRIDDLY_AUDIO_FUNC(unsigned int *ctx)
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
    GdState *s;
    GdParams P;
    float u[9];
    int i, tw;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(GdState) || span < (end - base)) return;
    if (stateBase + sizeof(GdState) > end) return;

    s = (GdState *)stateBase;

    u[0] = gd_ui(params[GRIDDLY_TYPE_SLOT],  (float)GRIDDLY_TYPE_UI_DEFAULT,  5.0f);
    u[1] = gd_ui(params[GRIDDLY_TIME_SLOT],  (float)GRIDDLY_TIME_UI_DEFAULT,  113.0f);
    u[2] = gd_ui(params[GRIDDLY_FDBK_SLOT],  (float)GRIDDLY_FDBK_UI_DEFAULT,  100.0f);
    u[3] = gd_ui(params[GRIDDLY_TONE_SLOT],  (float)GRIDDLY_TONE_UI_DEFAULT,  100.0f);
    u[4] = gd_ui(params[GRIDDLY_CHAR_SLOT],  (float)GRIDDLY_CHAR_UI_DEFAULT,  100.0f);
    u[5] = gd_ui(params[GRIDDLY_DUCK_SLOT],  (float)GRIDDLY_DUCK_UI_DEFAULT,  100.0f);
    u[6] = gd_ui(params[GRIDDLY_MIX_SLOT],   (float)GRIDDLY_MIX_UI_DEFAULT,   100.0f);
    u[7] = gd_tempo_ui(params[GRIDDLY_TEMPO_SLOT], (float)GRIDDLY_TEMPO_UI_DEFAULT);
    u[8] = gd_ui(params[GRIDDLY_TAIL_SLOT],  (float)GRIDDLY_TAIL_UI_DEFAULT,  1.0f);

    if (s->magic != GD_MAGIC) gd_init(s);
    /* bar tag: bars from earlier slots flip the Tempo copy too, FOLLOW takes their BPM */
    u[7] = dt_tempo(&s->sync, dryBuf ? dryBuf + 8 : 0, u[7], dt_id(stateBase));
    if (gd_clearing(s)) return;                  /* first ~31 ms after loading: dry */

    gd_prepare(&P, u, params[0] >= 0.5f);
    tw = (u[7] > 240.0f) ? 1 : 0;                /* sync reset, followed while off too */
    if (s->twin >= 0 && tw != s->twin) gd_restart(s, &P);
    s->twin = tw;
    gd_process(s, &P, fxBuf, 8);                 /* mono: left half in place */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R */
}

#endif /* GRIDDLY_HOST_TEST */
