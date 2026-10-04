/*
 * metro.c - "Metro": a test tool that clicks on Segue's beat grid, mono
 *
 * Not part of the pack. It answers one question on the pedal: does Segue's kick tracker
 * lock to the sequencer (Luca's Digitakt)? The input passes through and a metronome click
 * is mixed in on every beat the tracker counts, so you hear at once whether the clicks sit
 * on the kicks, drift off them, or put bar 1 in the wrong place.
 *
 * TRACKER
 *   The kick detector and the bar clock come from segue.c (Segue, PR #8, commit 5518a31):
 *   the functions keep their se_ names and the knobs LoBPM, HiBPM and Listn mean what they
 *   mean there. Everything Segue's header says under KICK TRACKING holds: the first kick
 *   after loading is bar 1, the tempo locks after four agreeing kicks, and every beat sits
 *   15 ms before the kick is detected, a few ms before the kick starts. So a click lands a
 *   hair before the kick, where Segue cuts.
 *   Four fixes found with Metro (2026-10-04), to be ported into segue.c:
 *   - AUTO threshold hunts. A boomy kick on every beat (long tail, as Digitakt kicks
 *     often are) is still loud when the next one hits, so the next kick only jumps about
 *     1.5..1.9 x over the band's recent level, under the fixed 2 x: after the first kick
 *     nothing counted. AUTO now starts at 2 x and, while nothing has locked, steps down
 *     every 4 s (1.7, 1.5, 1.3 x, then 2 x again); once locked the step stays.
 *   - A stale reference while finding the tempo. A gap between kicks longer than two
 *     beats that is not 1, 2, 4.. beats was skipped as "off the beat" but kept as the
 *     reference, so every later kick was measured from that stale kick and nothing ever
 *     locked (with LoBPM = HiBPM always, after any missed stretch). Such a gap now starts
 *     the measuring again.
 *   - The beat count while finding the tempo. Halving only counts 1, 2, 4.. beats between
 *     kicks; once two intervals agree, the beats since bar 1 are counted from the time.
 *   - The Thrsh knob: 1..100 = 1.02..3 x (was 1.3..20 x: above about 10 nothing counted).
 *
 * WHAT YOU HEAR
 *   Nothing is clicked until the tempo is locked: the first click means "locked".
 *   Click BEAT: a click on every beat. Bar starts are higher (1.6 kHz) than beats (800 Hz)
 *     and phrase starts (bar 1 of every Bars bars, where Segue starts a recording) higher
 *     still (2.4 kHz).
 *   Click BAR: only the bar and phrase clicks.
 *   Click KICK: no grid; a 1 kHz click on every onset the detector accepts as a kick, about
 *     20 ms after it starts (that is when Segue judges it), locked or not. Use it to see
 *     what the detector hears: missing clicks are kicks it does not see, extra clicks are
 *     bass notes or toms it takes for kicks (raise Thrsh or lower Listn).
 *   A click is a sine burst with a fast decay (8 ms), at most 0.5 x full scale.
 *
 * FOOTSWITCH
 *   Any press (on or off, the LED means nothing; the audio runs either way) resets bar 1:
 *   the next kick is bar 1 again, with the locked tempo kept. Press on the sequencer's
 *   bar 1 if the bar clicks land on the wrong beat. (Segue needs three quick presses for
 *   this; here one is enough because there is nothing to arm.) The beat line of that new
 *   bar 1 lies before the kick that set it, so its phrase click comes when the kick is
 *   judged, about 20 ms late, to confirm the reset; from beat 2 on the clicks are on time.
 *
 * KNOBS (screen values; pages of three)
 *   0 Mix   0..100  DJ-style crossfade: input gain min(1, 2 - 2m), click gain min(1, 2m);
 *                   both full at 50. 100 = clicks only.
 *   1 Click 0..2    BEAT / BAR / KICK
 *   2 Bars  0..7    phrase length for the phrase click, shown as 1..8 bars
 *   3 LoBPM 0..240  as Segue: slowest tempo the tracker may lock to (below 40 reads 40)
 *   4 HiBPM 0..240  as Segue: fastest; LoBPM = HiBPM fixes the tempo
 *   5 Thrsh 0..100  0 AUTO (hunts 2, 1.7, 1.5, 1.3 x until it locks), else how far the
 *                   kick band must jump over its recent level: 1 = 1.02 x (anything) ..
 *                   50 = 2 x .. 100 = 3 x (only hard kicks)
 *   6 Listn 0..101  as Segue: 0 AUTO (100 Hz), 1..101 = 50..150 Hz
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline, no calls.
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

#define MT_MAGIC      0x4D455432u        /* "MET1": change whenever SeState changes      */
#define SE_LEAD       662.0f             /* beats sit 15 ms before the detected kick     */
#define SE_JUDGE      110                /* blocks: an onset is judged 20 ms later       */
#define MT_HUNT       22050              /* AUTO: 4 s (in blocks) per threshold step     */
#define SE_SPB        2646000.0f         /* samples per beat x BPM (60 x 44100)          */

#define MT_BEAT       0                  /* Click knob                                   */
#define MT_BAR        1
#define MT_KICK       2

#define MT_LEN        1323               /* click length, samples (30 ms)                */
#define MT_DECAY      0.99716955f        /* 8 ms decay per sample                        */
#define MT_AMP        0.5f               /* loudest click                                */
#define MT_W_BEAT     0.11391900f        /* 2 sin(pi f / fs): 800 Hz                     */
#define MT_W_KICK     0.14235538f        /* 1 kHz                                        */
#define MT_W_BAR      0.22746810f        /* 1.6 kHz                                      */
#define MT_W_PHRASE   0.34027860f        /* 2.4 kHz                                      */

#define SE_WAIT       0                  /* no bar 1 yet (after loading or a reset)      */
#define SE_ACQ        1                  /* bar 1 known, finding the tempo               */
#define SE_LOCK       2                  /* tempo locked, tracking                       */

typedef struct {
    unsigned int magic;
    int   on_prev;         /* last on/off state seen                          */
    /* kick detector (as Segue) */
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
    /* bar clock (as Segue) */
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
    /* metronome */
    int   kicked;          /* a kick was accepted in the last block (2: bar 1) */
    int   left;            /* samples of the click left                       */
    float w;               /* its oscillator coefficient                      */
    float amp;             /* its envelope                                    */
    float bs, bc;          /* its oscillator                                  */
} SeState;

typedef struct {
    float gd, gc;          /* Mix gains: input, click (DJ law)                */
    int   click;           /* MT_BEAT / MT_BAR / MT_KICK                      */
    int   bars;            /* 1..8                                            */
    float Tmin, Tmax;      /* beat length range from HiBPM, LoBPM             */
    int   fixed;           /* LoBPM = HiBPM                                   */
    float R;               /* kick: jump over the average                     */
    int   autoR;           /* Thrsh AUTO: the detector picks R itself         */
    float lpc;             /* kick band low-pass coefficient                  */
    int   refr;            /* blocks between kicks at least                   */
} SeParams;

/* ---- copied from segue.c ------------------------------------------------- */

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

/* kick detector, once per block, after the samples (Segue's, unchanged; pk = the kick
 * band's peak over the block). Sets s->kicked when it accepts a kick: 2 if that kick
 * is bar 1 after a reset (the tempo already locked). */
SE_ALWAYS_INLINE(se_detect)
static inline void se_detect(SeState *s, const SeParams *P, float pk)
{
    float th;
    if (pk > s->env) s->env = pk; else s->env *= 0.98202f;    /* 10 ms release    */
    if (++s->kn >= 5512) { s->kn = 0; s->kp = s->kw; s->kw = 0.0f; }
    if (s->pend >= 0) {
        if (s->env > s->pstr) s->pstr = s->env;
        s->pend++;
        if (s->pend >= SE_JUDGE) {
            if (s->pstr >= 0.7f * s->kw && s->pstr >= 0.7f * s->kp) {
                int was_wait = (s->cs == SE_WAIT);
                se_kick(s, P, 8 * s->pend);
                s->kicked = (was_wait && s->cs == SE_LOCK) ? 2 : 1;   /* 2: bar 1 again */
            }
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
        else if (++s->hunt >= MT_HUNT) { s->hunt = 0; s->ri = (s->ri + 1) & 3; }
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

/* ---- the metronome ------------------------------------------------------- */

SE_ALWAYS_INLINE(se_init)
static inline void se_init(SeState *s, float T0)
{
    s->on_prev = -1;
    s->d1 = 0.0f; s->d2 = 0.0f; s->dc = 0.0f;
    s->env = 0.0f; s->slow = 0.0f; s->refr = 0; s->ready = 1;
    s->pend = -1; s->pstr = 0.0f; s->kw = 0.0f; s->kp = 0.0f; s->kn = 0;
    s->ri = 0; s->hunt = 0;
    s->cs = SE_WAIT; s->tl = 0; s->T = T0; s->ph = 0.0f; s->beat = 0; s->kb = 0; s->sk = 0; s->s1 = 0;
    s->hits = 0; s->misses = 0; s->tm = T0;
    s->kicked = 0; s->left = 0; s->w = MT_W_BEAT; s->amp = 0.0f; s->bs = 0.0f; s->bc = 1.0f;
    s->magic = MT_MAGIC;
}

/* u[] = screen values in manifest order */
SE_ALWAYS_INLINE(se_prepare)
static inline void se_prepare(SeParams *P, const float *u)
{
    float m, lo, hi, t;
    int th;
    m = u[0] * 0.01f;
    P->gd = 2.0f - 2.0f * m; if (P->gd > 1.0f) P->gd = 1.0f;
    P->gc = 2.0f * m;        if (P->gc > 1.0f) P->gc = 1.0f;
    P->click = (int)(u[1] + 0.5f);
    P->bars = (int)(u[2] + 0.5f) + 1;
    lo = u[3]; hi = u[4];                        /* the rest as Segue's se_prepare */
    if (lo < 40.0f) lo = 40.0f;
    if (hi < 40.0f) hi = 40.0f;
    if (lo > hi) { t = lo; lo = hi; hi = t; }
    P->fixed = (hi - lo < 0.5f);
    P->Tmin = SE_SPB * se_recip(hi);
    P->Tmax = SE_SPB * se_recip(lo);
    th = (int)(u[5] + 0.5f);
    P->autoR = (th == 0);
    P->R = 1.0f + 0.02f * (float)th;                         /* 1..100: 1.02 .. 3 x */
    t = (u[6] >= 0.5f) ? (49.0f + u[6]) : 100.0f;           /* kick band, Hz */
    P->lpc = t * 1.4247585e-4f;                              /* ~ 2 pi f / fs */
    P->refr = (int)(P->Tmin * 0.2f * 0.125f);
}

/* start a click: oscillator coefficient and level */
SE_ALWAYS_INLINE(mt_click)
static inline void mt_click(SeState *s, float w, float amp)
{
    s->w = w;
    s->amp = amp;
    s->bs = 0.0f;                                /* the sine starts at zero: no pop */
    s->bc = 1.0f;
    s->left = MT_LEN;
}

/* a beat line has just passed (s->beat is the new beat) */
SE_ALWAYS_INLINE(mt_on_beat)
static inline void mt_on_beat(SeState *s, const SeParams *P)
{
    int bar = ((s->beat & 3) == 0);
    if (s->cs != SE_LOCK || P->click == MT_KICK) return;
    if (bar && se_mod(s->beat >> 2, P->bars) == 0) mt_click(s, MT_W_PHRASE, MT_AMP);
    else if (bar) mt_click(s, MT_W_BAR, MT_AMP);
    else if (P->click == MT_BEAT) mt_click(s, MT_W_BEAT, 0.6f * MT_AMP);
}

/* once per block: footswitch (any press resets bar 1), a kick click */
SE_ALWAYS_INLINE(se_block)
static inline void se_block(SeState *s, const SeParams *P, float onoff)
{
    int on = s->on_prev ? (onoff > 0.3f) : (onoff > 0.7f);
    if (s->on_prev < 0) s->on_prev = on;         /* the first block: not a press */
    if (on != s->on_prev) {
        s->on_prev = on;
        s->cs = SE_WAIT;                         /* as Segue's se_reset           */
        s->hits = 0; s->misses = 0;
    }
    if (s->T < P->Tmin) s->T = P->Tmin;
    if (s->T > P->Tmax) s->T = P->Tmax;
    if (s->kicked && P->click == MT_KICK) mt_click(s, MT_W_KICK, MT_AMP);
    else if (s->kicked == 2) mt_click(s, MT_W_PHRASE, MT_AMP);   /* late, see the header */
    s->kicked = 0;
}

SE_ALWAYS_INLINE(se_process)
static inline void se_process(SeState *s, const SeParams *P, float *buf, int n)
{
    int i;
    float pk = 0.0f, d1 = s->d1, d2 = s->d2, dc = s->dc;
    for (i = 0; i < n; i++) {
        float in = buf[i], x, c = 0.0f;

        /* kick band (as Segue) */
        d1 += P->lpc * (in - d1);
        d2 += P->lpc * (d1 - d2);
        dc += 0.0042742f * (d2 - dc);            /* 30 Hz */
        x = d2 - dc;
        if (x < 0.0f) x = -x;
        if (x > pk) pk = x;

        /* bar clock (as Segue); a click starts on the sample the beat line passes */
        s->ph += 1.0f;
        if (s->sk < 0x10000000) s->sk++;
        if (s->s1 < 0x10000000) s->s1++;
        if (s->ph >= s->T) { s->ph -= s->T; s->beat++; if (s->cs != SE_WAIT) mt_on_beat(s, P); }

        if (s->left > 0) {
            s->bs += s->w * s->bc;
            s->bc -= s->w * s->bs;
            c = s->amp * s->bs;
            s->amp *= MT_DECAY;
            s->left--;
        }
        buf[i] = P->gd * in + P->gc * c;
    }
    s->d1 = d1; s->d2 = d2; s->dc = dc;
    se_detect(s, P, pk);
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

/* knob 1 Click: BEAT, BAR, KICK */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value >= 2u) return se_put4(out, 'K', 'I', 'C', 'K');
    if (value == 1u) return se_put4(out, 'B', 'A', 'R', 0);
    return se_put4(out, 'B', 'E', 'A', 'T');
}

/* knob 2 Bars: screen 0..7 shown as 1..8 */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value > 7u) value = 7u;
    return se_put_int((int)value + 1, out);
}

/* knobs 3, 4 LoBPM, HiBPM: below 40 reads as 40 */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value < 40u) value = 40u;
    return se_put_int((int)value, out);
}

int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value < 40u) value = 40u;
    return se_put_int((int)value, out);
}

/* knob 5 Thrsh: "AUTO", 1..100 */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    if (value == 0u) return se_put4(out, 'A', 'U', 'T', 'O');
    return se_put_int((int)value, out);
}

/* knob 6 Listn: "AUTO", "50Hz".."150Hz" */
int ZDL_GetLabel_6(unsigned int value, char *out)
{
    int n;
    if (value == 0u) return se_put4(out, 'A', 'U', 'T', 'O');
    if (value > 101u) value = 101u;
    n = se_put_int(49 + (int)value, out);
    out[n] = 'H'; out[n + 1] = 'z'; out[n + 2] = 0;
    return n + 2;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef METRO_HOST_TEST

#include "metro_params.h"

#ifndef METRO_AUDIO_FUNC
#define METRO_AUDIO_FUNC Fx_DLY_Metro
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SE_CODE_SECTION(METRO_AUDIO_FUNC)
void METRO_AUDIO_FUNC(unsigned int *ctx)
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
    float u[7];
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

    u[0] = se_ui(params[METRO_MIX_SLOT],   (float)METRO_MIX_UI_DEFAULT,   100.0f);
    u[1] = se_ui(params[METRO_CLICK_SLOT], (float)METRO_CLICK_UI_DEFAULT, 2.0f);
    u[2] = se_ui(params[METRO_BARS_SLOT],  (float)METRO_BARS_UI_DEFAULT,  7.0f);
    u[3] = se_ui(params[METRO_LOBPM_SLOT], (float)METRO_LOBPM_UI_DEFAULT, 240.0f);
    u[4] = se_ui(params[METRO_HIBPM_SLOT], (float)METRO_HIBPM_UI_DEFAULT, 240.0f);
    u[5] = se_ui(params[METRO_THRSH_SLOT], (float)METRO_THRSH_UI_DEFAULT, 100.0f);
    u[6] = se_ui(params[METRO_LISTN_SLOT], (float)METRO_LISTN_UI_DEFAULT, 101.0f);

    se_prepare(&P, u);
    if (s->magic != MT_MAGIC) se_init(s, 0.5f * (P.Tmin + P.Tmax));
    se_block(s, &P, params[0]);                  /* on/off only counts presses */
    se_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* METRO_HOST_TEST */
