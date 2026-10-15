/*
 * looper.c - "Looper": a bar-synced stereo looper on the song grid of the MOD firmware (v2.0 step 1)
 *
 * Scope: matujuice/zoom-ms-modding docs/v2.0-brief.md "Looper" (decided 2026-10-15) and the
 * project's plan/v2.0-looper-zdl-scope.md. Started from the Segue branch (seams, tape-splice
 * loop end, beat roll); Segue's kick tracker, Mode, XFade pickup and 8-bit format are gone.
 *
 * SONG GRID
 *   Bars and beats come from the MOD firmware's transport block (src/custom/common/zmt.h).
 *   With a MIDI clock running, bar 1 is MIDI Start; the grid follows the clock count. Without
 *   one it runs on at the pedal's tempo (tap, patch, last clock tempo after Stop), and a
 *   press that records (loop empty, clock not running) makes that moment bar 1, so the
 *   first recording starts right away. The grid counts in units of 1/32 note (1/8 beat).
 *
 * FOOTSWITCH: with the MOD firmware's looper services (v2.0 step 2) the firmware counts
 * presses (footswitch with the looper under the cursor, which then never flips it; the
 * looper slot's on/off CC, values 64-127) in its services block. Without them (older
 * firmware, a second looper, the firmware has not found our slot yet) every on/off flip is
 * a press, from the footswitch or the CC, and the LED means nothing.
 *   Empty   press: record Length bars at the When line, then play
 *   Playing press: stop at the When line (the loop is kept, the loop part goes silent)
 *   Stopped press: play again at the When line, in its place on the grid
 *   Recording: press does nothing
 *   Double press (two flips within 300 ms): delete the loop at the When line (IMMED: at
 *   once); while recording it throws the recording away at once.
 *   With When IMMED a single press waits out the 300 ms, so it can still become a double.
 *   A press while an action waits is ignored, except the second press of a double.
 *   MIDI Start re-aims a waiting action at the downbeat; a recording under way starts again
 *   on the downbeat.
 *   MIDI Stop (or 0.5 s without clock) stops a playing loop at once and drops a waiting
 *   action and a recording under way; the next MIDI Start plays that loop again from bar 1.
 *   A loop stopped from the footswitch stays stopped.
 *
 * KNOBS (screen values; page 1 Mix, Roll, Length; page 2 When, Amount)
 *   0 Mix    0..100  DJ crossfade live <-> loop: live min(1, 2 - 2m), loop min(1, 2m), both
 *                    full at 50. With no loop (empty, recording, stopped) only the live part
 *                    is heard. Gains glide ~5 ms.
 *   1 Roll   0..5    OFF, 1/2, 1/4, 1/8, 1/16, 1/32 (of a bar). Latched beat repeat: it starts
 *                    on the next bar line and repeats the first 1/2 .. 1/32 of that bar; a
 *                    new rate takes over on that rate's next line (same start point: 1/4 ->
 *                    1/8 doubles the repeats); OFF lands on the next line of the
 *                    running rate, where the loop would have been (the loop never moves).
 *   2 Length 0..7    1..8 bars recorded. If they don't fit at the tempo when recording starts,
 *                    stereo falls back to mono, then the length halves (8 -> 4 -> 2 -> 1).
 *   3 When   0..3    IMMED, BEAT, BAR, CYCLE: the line record, stop, play and delete wait for
 *                    (Perform mode's When). CYCLE = the next Amount-bar line counted from bar 1.
 *   4 Amount 0..15   1..16 bars, used by CYCLE. Length 4 with CYCLE 8 records bars 1-4 of the
 *                    next 8-bar phrase.
 *
 * LOOP FORMAT
 *   12-bit block floating point, 16 frames per block: one 4-bit shift for the block (shared
 *   by L and R, as Luca's A/B), then 12-bit samples packed two in three bytes. 49 bytes per
 *   stereo block (3.06 B per frame), 25 per mono block. A mono loop stores the left input.
 *   Stereo or mono is picked when recording starts: stereo whenever Length fits.
 *   Memory: the firmware's big buffer (free RAM regions 3, 2, 1 from the services block)
 *   plus the effect's own arena, 3.3 MB: 8 bars stereo from 79 BPM, mono at every tempo.
 *   The looper that holds the services claim gets it, picked when recording starts; a
 *   second looper, or older firmware, has the arena alone (705 KB: about 10 s mono, 5 s
 *   stereo). Recorded audio is never read before it is written, so a new
 *   arena needs no clearing; a new arena (patch change, reload) means an empty looper.
 *
 * PLAYBACK ON THE GRID (tempo changes)
 *   The loop's place is tied to the song grid: on every 1/16 line the playhead is checked
 *   against where the grid says it should be, and only if it is more than LP_SNAP samples
 *   (1.45 ms) off it jumps there. At the recording tempo nothing ever jumps. Faster, each
 *   1/16 slice is cut short at the next line. Slower, a slice that runs out before its line
 *   is filled by overlapping grains of its own tail (a granular stretch: 15 ms crossfades,
 *   one every 30 ms, LP_GX) until the line, so the next slice's attack is never heard twice
 *   (no flams) and there are no gaps. Pitch never changes. Every other jump, the loop end,
 *   start and stop are a 128-sample (2.9 ms) crossfade; at the loop end the old voice reads on into 128 samples
 *   recorded just after the loop (the tail), so the end flows into the start like a splice.
 *
 * LOOPER SERVICES (zoom-ms-midistomp docs/looper-services.md, block at 0xC00F3000): the
 *   looper claims the block with its params pointer (ctx[1]) while it has no loop of its
 *   own in the arena, beats a heartbeat every block (the firmware frees a silent claim after
 *   ~48 ms), and writes the firmware's status box: 8 characters (EMPTY, REC WAIT, REC or
 *   REC MONO, PLAY, STOP, DEL WAIT; STOP or PLAY while waiting to stop or play), the loop
 *   progress 0..256 (recording: time recorded / Length; playing or stopped: place in the
 *   loop; REC WAIT: the wait for the line), which blinks while the box itself never inverts
 *   (Luca): off an 1/8 every 2 beats while recording, the countdown blink while waiting. While the firmware says the looper is under the cursor it drives the LED
 *   (PWM, 32 steps of one block: 172 Hz), as the v2.0 brief's LED table: off when empty or
 *   stopped, a fade every 2 beats while recording, on while playing or rolling, and while
 *   an action waits a countdown blink whose rate doubles as the time left halves (one per
 *   2 bars 8 bars out, down to 1/8 notes). Losing the claim with a loop in the big buffer
 *   empties the looper. A double press while stopped deletes at once, whatever When says.
 *   Counters ev_rec, ev_stop, ev_del are kept for Perform mode's events.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline.
 */

#include <stdint.h>
#include "../common/zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define LP_DO_PRAGMA(x) _Pragma(#x)
#define LP_EXPAND_PRAGMA(x) LP_DO_PRAGMA(x)
#define LP_ALWAYS_INLINE(fn) LP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define LP_CODE_SECTION(fn) LP_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define LP_ALWAYS_INLINE(fn)
#define LP_CODE_SECTION(fn)
#endif

#define LP_MAGIC   0x4C4F4F33u           /* "LOO3": change whenever LpState changes        */
#define LP_X       128                   /* seam crossfade, samples (2.9 ms)               */
#define LP_XINV    0.0078125f
#define LP_GX      661                   /* grain crossfade, samples (15 ms): tune by ear   */
#define LP_GXINV   0.0015128593f         /* 1 / LP_GX                                       */
#define LP_SNAP    64                    /* re-sync only when this many samples off        */
#define LP_DBL     13230                 /* double press window: 300 ms                    */
#define LP_GLIDE   0.005f                /* gain glide per sample: ~5 ms                   */
#define LP_UNIT    3.0234014e-6f         /* grid units (1/8 beat) per sample per BPM       */
#define LP_SPB     2646000.0f            /* samples per beat x BPM                         */
#define LP_BS2     49                    /* bytes per stereo block                         */
#define LP_BS1     25                    /* bytes per mono block                           */
#define LP_NREG    4                     /* buffer regions: the big buffer's 3 + the arena */
#ifndef LP_ON_JUMP
#define LP_ON_JUMP()                     /* host tests count the seams */
#endif

/* The MOD firmware's looper services block (zoom-ms-midistomp docs/looper-services.md):
 * plain loads and stores, nothing calls into the firmware. Word indexes. */
#ifndef LS_ADDR
#ifdef __TI_COMPILER_VERSION__
#define LS_ADDR    0xC00F3000u
#else
static unsigned int ls_host[32];         /* host tests: no firmware; tests fill it in */
#define LS_ADDR    ((uintptr_t)ls_host)
#endif
#endif
#define LS         ((volatile unsigned int *)LS_ADDR)
#define LS_MAGIC   0x5A4C5301u           /* "ZLS", version 1                               */
#define LS_NREG    1                     /* regions, then base / bytes pairs from word 2   */
#define LS_CLAIM   8                     /* ours: the params pointer of the looper using it */
#define LS_HB      9                     /* ours: +1 every block while we hold the claim   */
#define LS_SLOT    10                    /* firmware: our slot, -1 until it has found it   */
#define LS_PRESS   11                    /* firmware: footswitch / CC presses              */
#define LS_MINE    12                    /* firmware: we are under the cursor: drive the LED */
#define LS_LEDON   13                    /* firmware: GPIO register that lights the LED    */
#define LS_LEDOFF  14                    /* firmware: GPIO register that darkens it        */
#define LS_LEDMASK 15
#define LS_LEVEL   16                    /* ours: LED level 0..32 (PWM duty)                */
#define LS_TEXT    17                    /* ours: 8 status characters (words 17, 18)       */
#define LS_PROG    19                    /* ours: loop progress 0..256 (status box fill)    */
#define LS_INV     20                    /* ours: 1 = status box inverted (pulse, blink)    */
#define LP_PWM     32                    /* LED PWM steps (one per block: 172 Hz)          */

#define LP_EMPTY 0                       /* st */
#define LP_REC   1
#define LP_PLAY  2
#define LP_STOP  3

#define LP_A_REC  1                      /* actions */
#define LP_A_STOP 2
#define LP_A_PLAY 3
#define LP_A_DEL  4

typedef struct {
    int   pos;             /* frame being read, -1 = silent                      */
    int   blk;             /* block decoded into l, r (-1 = none)                */
    float l[16], r[16];
} LpVoice;

typedef struct {
    unsigned int magic;
    ZtSync zt;
    /* song grid */
    int   gu;              /* units (1/8 beat) since bar 1 of the last line passed */
    float gf;              /* fraction of the next unit already played           */
    int   wraps;           /* the transport's 96-beat wraps seen since Start      */
    float lastb;           /* the transport's beats last block                   */
    int   run;             /* the MIDI transport runs                            */
    int   mstop;           /* MIDI Stop stopped the loop: Start plays it again   */
    /* footswitch and the action waiting for its line */
    int   on_prev;         /* last on/off seen, -1 = none yet                    */
    int   age;             /* samples since the last press                       */
    int   first;           /* the last press may start a double                  */
    int   pend;            /* LP_A_*, 0 = none                                   */
    int   pu;              /* its line (unit)                                     */
    int   pm;              /* its grid step in units (to re-aim it at Start)     */
    int   pwait;           /* IMMED: samples left; -1 = waits for pu             */
    /* loop */
    int   st;              /* LP_EMPTY / REC / PLAY / STOP                       */
    int   stereo, bs;      /* format of this loop, bytes per block               */
    int   U;               /* loop length in units                               */
    int   lu0;             /* where the loop starts on the grid: unit ...        */
    float lu0f;            /* ... and fraction (IMMED starts between lines)       */
    int   eu; float ef;    /* the recording ends here (lu0 + U)                  */
    int   wi;              /* frames recorded                                    */
    int   capf;            /* frames that fit in this format                     */
    int   L;               /* loop length, frames                                */
    float spu;             /* frames per unit                                    */
    int   tail;            /* frames still to record after the loop end          */
    float stl[16], str[16];/* the block being recorded                           */
    LpVoice v[2];
    int   mi;              /* v[mi] is heard, v[mi ^ 1] fades out                */
    int   fade;            /* samples of the fade left                           */
    float fi;              /* 1 / the length of this fade                        */
    int   slice;           /* frames from one 1/16 line to the next in the loop  */
    int   nrun;            /* frames played since the last 1/16 line             */
    int   roll;            /* Roll slice in units (0 = off)                      */
    int   ru0;             /* the bar line Roll started on                       */
    float gd, gw;          /* live and loop gains (gliding)                      */
    /* state out */
    int   ev_rec, ev_stop, ev_del;
    /* looper services */
    unsigned int me;       /* our params pointer: the claim                      */
    int   own;             /* we hold the services block's claim                 */
    unsigned int npress;   /* its press counter, as far as we have counted      */
    int   big;             /* this loop is in the big buffer                     */
    int   want;            /* Length asked for when this loop was recorded       */
    int   pwm, led;        /* LED PWM step, last LED write (-1 = none)           */
    int   tick;            /* unused (was the stopped loop's breathing)          */
    /* buffer */
    uintptr_t ab; int asz; /* the arena (always the last region)                 */
    int   nreg;
    uintptr_t rb[LP_NREG]; /* region start                                       */
    int   rsz[LP_NREG];    /* bytes                                              */
    int   rcnt[LP_NREG];   /* blocks in this format                              */
} LpState;

typedef struct {
    float tgd, tgw;        /* Mix gains                                          */
    int   roll;            /* slice in units: 0, 16, 8, 4, 2, 1                  */
    int   len;             /* 1..8 bars                                          */
    int   when;            /* 0 IMMED, 1 BEAT, 2 BAR, 3 CYCLE                    */
    int   amt;             /* 1..16 bars                                         */
} LpParams;

/* The pedal hands every knob over as (screen number) / 100. Convert back. */
LP_ALWAYS_INLINE(lp_ui)
static inline float lp_ui(float raw, float def_ui, float max_ui)
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

/* 1 / x for x > 0: a guess from the float bits, then three Newton steps */
LP_ALWAYS_INLINE(lp_recip)
static inline float lp_recip(float x)
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

/* 2^n as a float, n in -100..100 */
LP_ALWAYS_INLINE(lp_pow2i)
static inline float lp_pow2i(int n)
{
    union { float f; unsigned int u; } c;
    c.u = ((unsigned int)(n + 127)) << 23;
    return c.f;
}

/* x mod m (result 0..m-1) for any x, m >= 1, |x| < 2^24: reciprocal and a correction */
LP_ALWAYS_INLINE(lp_imod)
static inline int lp_imod(int x, int m)
{
    int r = x - (int)((float)x * lp_recip((float)m)) * m;
    while (r < 0) r += m;
    while (r >= m) r -= m;
    return r;
}

/* x mod m for floats, m > 0 */
LP_ALWAYS_INLINE(lp_fmod)
static inline float lp_fmod(float x, float m)
{
    float r = x - (float)(int)(x * lp_recip(m)) * m;
    while (r < 0.0f) r += m;
    while (r >= m) r -= m;
    return r;
}

/* the next line of a grid of m units after unit u */
LP_ALWAYS_INLINE(lp_next)
static inline int lp_next(int u, int m)
{
    return u - lp_imod(u, m) + m;
}

/* ---- the buffer ----------------------------------------------------------- */

/* blocks of bs bytes in each region; returns the frames that fit */
LP_ALWAYS_INLINE(lp_count)
static inline int lp_count(LpState *s, int bs)
{
    int k, n, all = 0;
    for (k = 0; k < s->nreg; k++) {
        n = (int)((float)s->rsz[k] * lp_recip((float)bs));
        while (n > 0 && n * bs > s->rsz[k]) n--;
        while ((n + 1) * bs <= s->rsz[k]) n++;
        s->rcnt[k] = n;
        all += n;
    }
    return all << 4;
}

LP_ALWAYS_INLINE(lp_addr)
static inline unsigned char *lp_addr(const LpState *s, int n)
{
    int k;
    for (k = 0; k < s->nreg; k++) {
        if (n < s->rcnt[k]) return (unsigned char *)(s->rb[k] + (uintptr_t)(n * s->bs));
        n -= s->rcnt[k];
    }
    return 0;
}

LP_ALWAYS_INLINE(lp_q12)
static inline int lp_q12(float x, float sc)
{
    float a = x * sc;
    int q = (int)(a + ((a >= 0.0f) ? 0.5f : -0.5f));
    if (q > 2047) q = 2047;
    if (q < -2047) q = -2047;
    return q & 4095;
}

/* 16 samples, two per three bytes */
LP_ALWAYS_INLINE(lp_pack)
static inline void lp_pack(unsigned char *p, const float *x, float sc)
{
    int j, a, b;
    for (j = 0; j < 16; j += 2) {
        a = lp_q12(x[j], sc); b = lp_q12(x[j + 1], sc);
        p[0] = (unsigned char)(a & 255);
        p[1] = (unsigned char)((a >> 8) | ((b & 15) << 4));
        p[2] = (unsigned char)(b >> 4);
        p += 3;
    }
}

LP_ALWAYS_INLINE(lp_unpack)
static inline void lp_unpack(const unsigned char *p, float *x, float g)
{
    int j, a, b;
    for (j = 0; j < 16; j += 2) {
        a = (int)p[0] | (((int)p[1] & 15) << 8);
        b = ((int)p[1] >> 4) | ((int)p[2] << 4);
        if (a & 2048) a -= 4096;
        if (b & 2048) b -= 4096;
        x[j] = (float)a * g; x[j + 1] = (float)b * g;
        p += 3;
    }
}

/* store the staged block (n frames, the rest silent). The shift e (0..15) is the largest
 * that keeps the block's peak under full scale; a sample is q / 2047 x 2^(3 - e), so peaks
 * up to 8 fit; blocks under -72 dB keep the finest step (-138 dBFS). */
LP_ALWAYS_INLINE(lp_flush)
static inline void lp_flush(LpState *s, int n)
{
    union { float f; unsigned int u; } c;
    unsigned char *p = lp_addr(s, (s->wi - 1) >> 4);
    float m = 0.0f, a, sc;
    int k, e;
    for (k = n; k < 16; k++) { s->stl[k] = 0.0f; s->str[k] = 0.0f; }
    for (k = 0; k < 16; k++) {
        a = s->stl[k]; if (a < 0.0f) a = -a; if (a > m) m = a;
        if (s->stereo) { a = s->str[k]; if (a < 0.0f) a = -a; if (a > m) m = a; }
    }
    c.f = m;
    e = 2 - ((int)((c.u >> 23) & 255u) - 127);   /* m < 2^(k+1): m x 2^(e-3) < 1 */
    if (m < 1e-30f || e > 15) e = 15;
    if (e < 0) e = 0;
    if (!p) return;
    sc = 2047.0f * lp_pow2i(e - 3);
    p[0] = (unsigned char)e;
    lp_pack(p + 1, s->stl, sc);
    if (s->stereo) lp_pack(p + 25, s->str, sc);
}

/* record one frame */
LP_ALWAYS_INLINE(lp_write)
static inline void lp_write(LpState *s, float l, float r)
{
    s->stl[s->wi & 15] = l;
    s->str[s->wi & 15] = r;
    s->wi++;
    if ((s->wi & 15) == 0) lp_flush(s, 16);
}

/* read the voice's frame and move it on */
LP_ALWAYS_INLINE(lp_read)
static inline void lp_read(const LpState *s, LpVoice *v, float *l, float *r)
{
    const unsigned char *p;
    int b, k;
    float g;
    *l = 0.0f; *r = 0.0f;
    if (v->pos < 0) return;
    if (v->pos < s->wi) {
        k = v->pos & 15;
        if (v->pos >= (s->wi & ~15)) {           /* in the block still being recorded */
            *l = s->stl[k]; *r = s->stereo ? s->str[k] : s->stl[k];
        } else {
            b = v->pos >> 4;
            if (b != v->blk) {
                p = lp_addr(s, b);
                v->blk = b;
                if (p) {
                    g = 4.8851979e-4f * lp_pow2i(3 - (int)(p[0] & 15));   /* 2^(3-e) / 2047 */
                    lp_unpack(p + 1, v->l, g);
                    if (s->stereo) lp_unpack(p + 25, v->r, g);
                }
            }
            *l = v->l[k]; *r = s->stereo ? v->r[k] : v->l[k];
        }
    }
    v->pos++;
}

/* ---- the loop ---------------------------------------------------------------- */

/* the heard voice jumps to 'to' (-1 = silence); the voice it leaves fades out */
LP_ALWAYS_INLINE(lp_jumpx)
static inline void lp_jumpx(LpState *s, int to, int len, float inv)
{
    LP_ON_JUMP();
    s->mi ^= 1;
    s->v[s->mi].pos = to;
    s->v[s->mi].blk = -1;
    s->fade = len; s->fi = inv;
}

LP_ALWAYS_INLINE(lp_jump)
static inline void lp_jump(LpState *s, int to)
{
    lp_jumpx(s, to, LP_X, LP_XINV);
}

/* where the loop should be (frame) at grid unit u, Roll included */
LP_ALWAYS_INLINE(lp_target)
static inline int lp_target(const LpState *s, int u)
{
    float x = (float)(u - s->lu0) - s->lu0f;
    int t;
    if (s->roll > 0)
        x = (float)(s->ru0 - s->lu0) - s->lu0f + (float)((u - s->ru0) & (s->roll - 1));   /* the bar's first slice (Luca) */
    x = lp_fmod(x, (float)s->U);
    t = (int)(x * s->spu + 0.5f);
    if (t >= s->L) t = s->L - 1;
    return t;
}

LP_ALWAYS_INLINE(lp_init)
static inline void lp_init(LpState *s, uintptr_t buf, int bytes)
{
    int k;
    s->gu = 0; s->gf = 0.0f; s->wraps = 0; s->lastb = 0.0f; s->run = 0; s->mstop = 0;
    s->on_prev = -1; s->age = LP_DBL; s->first = 0;
    s->pend = 0; s->pu = 0; s->pm = 32; s->pwait = -1;
    s->st = LP_EMPTY; s->stereo = 1; s->bs = LP_BS2; s->U = 32; s->lu0 = 0; s->lu0f = 0.0f;
    s->eu = 0; s->ef = 0.0f; s->wi = 0; s->capf = 0; s->L = 1; s->spu = 1.0f; s->tail = 0;
    for (k = 0; k < 16; k++) { s->stl[k] = 0.0f; s->str[k] = 0.0f; }
    for (k = 0; k < 2; k++) { s->v[k].pos = -1; s->v[k].blk = -1; }
    s->mi = 0; s->fade = 0; s->fi = LP_XINV; s->roll = 0; s->ru0 = 0; s->slice = 0; s->nrun = 0;
    s->gd = 1.0f; s->gw = 1.0f;
    s->ev_rec = 0; s->ev_stop = 0; s->ev_del = 0;
    s->own = 0; s->npress = 0u; s->big = 0; s->want = 8; s->pwm = 0; s->led = -1; s->tick = 0;
    s->ab = buf; s->asz = bytes;
    s->nreg = 1; s->rb[0] = buf; s->rsz[0] = bytes;
    for (k = 1; k < LP_NREG; k++) { s->rb[k] = 0u; s->rsz[k] = 0; }
    zt_init(&s->zt);
    s->magic = LP_MAGIC;
}

/* u[] = screen values in manifest order */
LP_ALWAYS_INLINE(lp_prepare)
static inline void lp_prepare(LpParams *P, const float *u)
{
    float m = u[0] * 0.01f;
    int r = (int)(u[1] + 0.5f);
    P->tgd = 2.0f - 2.0f * m; if (P->tgd > 1.0f) P->tgd = 1.0f;
    P->tgw = 2.0f * m;        if (P->tgw > 1.0f) P->tgw = 1.0f;
    P->roll = (r > 0) ? (32 >> r) : 0;
    P->len = (int)(u[2] + 0.5f) + 1;
    P->when = (int)(u[3] + 0.5f);
    P->amt = (int)(u[4] + 0.5f) + 1;
}

/* the buffer for a new recording: the firmware's big buffer and our arena while we hold
 * the services claim, else the arena alone */
LP_ALWAYS_INLINE(lp_regions)
static inline void lp_regions(LpState *s)
{
    volatile unsigned int *b = LS;
    int k, n = 0, m;
    if (s->own) {
        m = (int)b[LS_NREG];
        if (m > LP_NREG - 1) m = LP_NREG - 1;
        for (k = 0; k < m; k++) {
            s->rb[n] = (uintptr_t)b[2 + 2 * k];
            s->rsz[n] = (int)b[3 + 2 * k];
            if (s->rb[n] != 0u && s->rsz[n] > 0) n++;
        }
    }
    s->big = (n > 0);
    s->rb[n] = s->ab; s->rsz[n] = s->asz;
    s->nreg = n + 1;
}

/* start recording at the grid position (u, f): pick stereo or mono and the length that fits */
LP_ALWAYS_INLINE(lp_record)
static inline void lp_record(LpState *s, const LpParams *P, float bpm, int u, float f)
{
    float beat = LP_SPB * lp_recip(bpm), need;
    int len = P->len, cs, cm;
    lp_regions(s);
    cs = lp_count(s, LP_BS2); cm = lp_count(s, LP_BS1);
    s->want = len;
    for (;;) {
        need = 4.02f * (float)len * beat + (float)(LP_X + 32);   /* 0.5 % for a tempo drift */
        if (need <= (float)cs) { s->stereo = 1; break; }
        if (need <= (float)cm || len == 1) { s->stereo = 0; break; }
        len >>= 1;
    }
    s->bs = s->stereo ? LP_BS2 : LP_BS1;
    s->capf = lp_count(s, s->bs);
    s->U = len << 5;
    s->lu0 = u; s->lu0f = f;
    s->eu = u + s->U; s->ef = f;
    s->wi = 0; s->tail = 0;
    s->roll = 0;
    s->st = LP_REC;
}

/* the recording is complete: the loop plays from its start */
LP_ALWAYS_INLINE(lp_finish)
static inline void lp_finish(LpState *s)
{
    s->st = LP_PLAY;
    s->L = s->wi;
    s->spu = (float)s->L * lp_recip((float)s->U);
    s->tail = LP_X;
    s->ev_rec++;
    s->slice = 0; s->nrun = 0;
    lp_jump(s, 0);                               /* fade in from silence */
}

/* do an action now; line = 1 when on a grid line (then the loop starts exactly on it) */
LP_ALWAYS_INLINE(lp_do)
static inline void lp_do(LpState *s, const LpParams *P, float bpm, int act, int line)
{
    s->pend = 0; s->pwait = -1; s->mstop = 0;
    if (act == LP_A_REC && s->st == LP_EMPTY) lp_record(s, P, bpm, s->gu, line ? 0.0f : s->gf);
    if (act == LP_A_STOP && s->st == LP_PLAY) { lp_jump(s, -1); s->st = LP_STOP; s->roll = 0; s->ev_stop++; }
    if (act == LP_A_PLAY && s->st == LP_STOP) { s->st = LP_PLAY; s->slice = 0; lp_jump(s, lp_target(s, s->gu)); }
    if (act == LP_A_DEL) {
        if (s->st == LP_PLAY || s->st == LP_STOP) { lp_jump(s, -1); s->ev_del++; }
        s->st = LP_EMPTY; s->roll = 0; s->tail = 0;
    }
}

/* the grid passed line u */
LP_ALWAYS_INLINE(lp_line)
static inline void lp_line(LpState *s, const LpParams *P, float bpm, int u)
{
    int t, d;
    if (s->pend && s->pwait < 0 && u >= s->pu) lp_do(s, P, bpm, s->pend, 1);
    if (s->st != LP_PLAY) return;
    /* Roll: start on a bar line, a new rate on its own line, OFF on the running rate's line */
    if (P->roll != s->roll) {
        if (s->roll == 0) { if ((u & 31) == 0) { s->roll = P->roll; s->ru0 = u; } }
        else if (P->roll == 0) { if ((u & (s->roll - 1)) == 0) s->roll = 0; }
        else if ((u & (P->roll - 1)) == 0) s->roll = P->roll;
    }
    /* the playhead on the grid, on every 1/16 line (1/32 while Roll 1/32 runs) */
    if (u & ((s->roll == 1) ? 0 : 1)) return;
    t = lp_target(s, u);
    d = s->v[s->mi].pos - t;
    if (d + d > s->L) d -= s->L;
    if (d + d < -s->L) d += s->L;
    if (d > LP_SNAP || d < -LP_SNAP) lp_jump(s, t);
    s->slice = (int)(s->spu * ((s->roll == 1) ? 1.0f : 2.0f));
    s->nrun = 0;
}

/* one footswitch press: record, stop or play at the When line; two within 300 ms delete */
LP_ALWAYS_INLINE(lp_press)
static inline void lp_press(LpState *s, const LpParams *P, int run, float *du)
{
    int act, m;
    act = 0;
    if (s->age < LP_DBL && s->first) { s->first = 0; act = LP_A_DEL; }
    else {
        s->first = 1;
        if (s->st == LP_EMPTY) act = LP_A_REC;
        if (s->st == LP_PLAY) act = LP_A_STOP;
        if (s->st == LP_STOP) act = LP_A_PLAY;
    }
    s->age = 0;
    if (act == LP_A_DEL && s->st == LP_EMPTY) s->pend = 0;    /* the record never started */
    else if (act && (!s->pend || act == LP_A_DEL)) {
        s->pend = act;
        m = 8;
        if (P->when == 2) m = 32;
        if (P->when >= 3) m = P->amt << 5;
        s->pm = m;
        s->pu = lp_next(s->gu, m);
        s->pwait = -1;
        if (P->when == 0) s->pwait = (act == LP_A_DEL) ? 1 : LP_DBL;
        if (act == LP_A_DEL && s->st == LP_REC) s->pwait = 1;   /* nothing heard yet: at once */
        if (act == LP_A_DEL && s->st == LP_STOP) s->pwait = 1;  /* silent anyway: at once */
        if (act == LP_A_REC && !run) {       /* no clock: this press is bar 1, record now */
            s->gu = 0; s->gf = -*du;         /* the first sample lands on 0 */
            s->pwait = 1;
        }
    }
}

/* the services block: claim it while nobody has (only with no loop of our own in the
 * arena), beat the heartbeat, and return the presses since the last block; -1 = count
 * on/off flips instead (no services, someone else's claim, slot not found yet) */
LP_ALWAYS_INLINE(lp_services)
static inline int lp_services(LpState *s)
{
    volatile unsigned int *b = LS;
    unsigned int c, p;
    if (b[0] != LS_MAGIC || s->me == 0u) { s->own = 0; return -1; }
    c = b[LS_CLAIM];
    if (c == 0u && s->st == LP_EMPTY && !s->pend) { b[LS_CLAIM] = s->me; c = s->me; }
    if (c != s->me) {
        if (s->big) { s->st = LP_EMPTY; s->pend = 0; s->roll = 0; s->tail = 0; s->big = 0;   /* lost it */
                      s->v[0].pos = -1; s->v[1].pos = -1; s->fade = 0; }
        s->own = 0; s->led = -1;
        return -1;
    }
    p = b[LS_PRESS];
    if (!s->own) { s->own = 1; s->npress = p; }
    b[LS_HB] = b[LS_HB] + 1u;
    if ((int)b[LS_SLOT] < 0) { s->npress = p; return -1; }
    c = p - s->npress;
    s->npress = p;
    return (int)c;
}

/* once per block: the song grid from the transport, the footswitch */
LP_ALWAYS_INLINE(lp_block)
static inline float lp_block(LpState *s, const LpParams *P, float onoff, float *du)
{
    float beats, err, bpm;
    int run, start, on, n, m, h;

    bpm = zt_update(&s->zt, &run, &beats, &start);
    *du = bpm * LP_UNIT;
    if (s->run && !run) {                        /* MIDI Stop: stop with the song */
        if (s->st == LP_PLAY) { lp_do(s, P, bpm, LP_A_STOP, 0); s->mstop = 1; }
        if (s->st == LP_REC) s->st = LP_EMPTY;
        s->pend = 0; s->pwait = -1;
    }
    s->run = run;
    if (start) {                                 /* MIDI Start: the downbeat is bar 1 */
        s->wraps = 0; s->lastb = 0.0f;
        s->gu = -1; s->gf = 0.999f;
        s->lu0 = 0; s->lu0f = 0.0f;              /* the loop starts again on it */
        s->roll = 0;
        if (s->st == LP_REC) { s->st = LP_EMPTY; s->pend = LP_A_REC; s->pwait = -1; }
        if (s->mstop && s->st == LP_STOP) s->pend = LP_A_PLAY;
        if (s->pend) { s->pu = 0; s->pwait = -1; }
    }
    if (run) {
        if (s->zt.clk == 0u) { *du = 0.0f; s->gf = 0.999f; }   /* Start, waiting for clock 1 */
        else {
            if (beats < s->lastb - 48.0f) s->wraps++;
            s->lastb = beats;
            err = beats * 8.0f - ((float)(s->gu - 768 * s->wraps) + s->gf);
            if (err >= 4.0f || err <= -4.0f) {   /* loaded mid-song, a jump: take its place */
                h = (int)(beats * 8.0f);
                s->gu = h + 768 * s->wraps;
                s->gf = beats * 8.0f - (float)h;
            } else s->gf += err;
        }
    }

    /* footswitch: the firmware's press counter while it has found our slot, else every
     * on/off flip is a press (no services, a second looper, older firmware) */
    on = s->on_prev ? (onoff > 0.3f) : (onoff > 0.7f);
    if (s->on_prev < 0) s->on_prev = on;         /* the first block: not a press */
    n = (on != s->on_prev);
    s->on_prev = on;
    m = lp_services(s);
    if (m >= 0) n = m;
    if (n > 2) n = 2;                            /* a burst (a CC flood) is a double press at most */
    for (; n > 0; n--) lp_press(s, P, run, du);
    if (s->age < LP_DBL) s->age += 8;
    if (s->st != LP_PLAY) s->roll = 0;
    return bpm;
}

LP_ALWAYS_INLINE(lp_process)
static inline void lp_process(LpState *s, const LpParams *P, float bpm, float du, float *buf)
{
    int i;
    float gd = s->gd, gw = s->gw, inl, inr, ml, mr, fl, fr, g, left;
    int gx, hop;
    float idu = (du > 1e-6f) ? lp_recip(du) : 1e9f;   /* samples per grid unit */
    for (i = 0; i < 8; i++) {
        inl = buf[i]; inr = buf[i + 8];

        s->gf += du;
        while (s->gf >= 1.0f) { s->gf -= 1.0f; s->gu++; lp_line(s, P, bpm, s->gu); }
        if (s->pend && s->pwait > 0 && --s->pwait == 0) lp_do(s, P, bpm, s->pend, s->gf == 0.0f);

        if (s->st == LP_REC && (s->gu > s->eu || (s->gu == s->eu && s->gf >= s->ef)))
            lp_finish(s);
        if (s->st == LP_REC || s->tail > 0) {
            lp_write(s, inl, inr);
            if (s->st != LP_REC) {
                if (--s->tail == 0 && (s->wi & 15) != 0) lp_flush(s, s->wi & 15);
            } else if (s->wi >= s->capf - LP_X - 16) lp_finish(s);   /* memory full */
        }

        lp_read(s, &s->v[s->mi], &ml, &mr);
        if (s->fade > 0) {
            g = (float)s->fade * s->fi;
            lp_read(s, &s->v[s->mi ^ 1], &fl, &fr);
            ml += g * (fl - ml); mr += g * (fr - mr);
            if (--s->fade == 0) s->v[s->mi ^ 1].pos = -1;
        }
        if (s->st == LP_PLAY) {
            /* slower than recorded: the slice will run out before its line comes. A slice of
             * 4 grains or more is filled by grains: the last LP_GX samples before its end
             * crossfade back by two grains, again and again until the line (a granular
             * stretch of the slice's tail; the next slice's attack is never heard early, its
             * line cuts in on time). Shorter slices (1/32 Roll at a fast tempo) just repeat
             * their second half with the short crossfade. */
            gx = (s->slice >= 4 * LP_GX);
            if (s->slice > 0 && ++s->nrun >= s->slice - (gx ? LP_GX : LP_X)) {
                left = (float)((s->roll == 1 ? 1 : 2) - (s->gu & (s->roll == 1 ? 0 : 1))) - s->gf;
                if (left * idu > (float)(s->slice - s->nrun + LP_SNAP)) {   /* the line is late */
                    hop = gx ? 2 * LP_GX : (s->slice >> 1);
                    lp_jumpx(s, s->v[s->mi].pos - hop, gx ? LP_GX : LP_X, gx ? LP_GXINV : LP_XINV);
                    s->nrun -= hop;
                }
            }
            if (s->v[s->mi].pos >= s->L) lp_jump(s, s->v[s->mi].pos - s->L);
        }

        gd += LP_GLIDE * (P->tgd - gd);
        gw += LP_GLIDE * (P->tgw - gw);
        buf[i] = gd * inl + gw * ml;
        buf[i + 8] = gd * inr + gw * mr;
    }
    s->gd = gd; s->gw = gw;
}

/* ---- state out: the status text, the pulse level and the LED ------------------- */

/* "b/L" into t[k..k+2] (bars 1..8) */
/* once per block while we hold the claim: 8 characters, the loop progress and the box's
 * (blinking) loop progress for the firmware's status box, and a level 0..32 that the LED shows (PWM)
 * while the firmware says the looper is under the cursor */
LP_ALWAYS_INLINE(lp_out)
static inline void lp_out(LpState *s)
{
    volatile unsigned int *b = LS;
    char t[8];
    int k, lev = 0, u, lit, hide = 0, prog = 0;
    float p;
    unsigned int w0, w1;
    if (!s->own) return;
    for (k = 0; k < 8; k++) t[k] = ' ';
    if (s->st == LP_EMPTY) {
        t[0] = 'E'; t[1] = 'M'; t[2] = 'P'; t[3] = 'T'; t[4] = 'Y';
        if (s->pend) { t[0] = 'R'; t[1] = 'E'; t[2] = 'C'; t[3] = ' '; t[4] = 'W'; t[5] = 'A'; t[6] = 'I'; t[7] = 'T';
            prog = (int)(((float)(s->pm - s->pu + s->gu) + s->gf) * 256.0f * lp_recip((float)s->pm)); }  /* the wait for the line */
    } else if (s->st == LP_REC) {
        t[0] = 'R'; t[1] = 'E'; t[2] = 'C';
        if (!s->stereo) { t[4] = 'M'; t[5] = 'O'; t[6] = 'N'; t[7] = 'O'; }
        prog = (int)(((float)(s->gu - s->lu0) + s->gf) * 256.0f * lp_recip((float)s->U));
        p = ((float)(s->gu & 15) + s->gf) * 0.125f;   /* a fade every 2 beats, on the grid */
        if (p > 1.0f) p = 2.0f - p;
        lev = 4 + (int)(28.0f * p * p);
        hide = (s->gu & 15) < 4;                        /* the bar: off an 1/8 every 2 beats */
    } else {
        u = lp_imod(s->gu - s->lu0, s->U);              /* place in the loop, stopped too */
        prog = (int)(((float)u + s->gf) * 256.0f * lp_recip((float)s->U));
        if (s->st == LP_PLAY) {
            t[0] = 'P'; t[1] = 'L'; t[2] = 'A'; t[3] = 'Y';
            if (s->pend == LP_A_STOP) { t[0] = 'S'; t[1] = 'T'; t[2] = 'O'; t[3] = 'P'; }
            lev = LP_PWM;                               /* on (rolling too) */
        } else {
            t[0] = 'S'; t[1] = 'T'; t[2] = 'O'; t[3] = 'P';  /* off */
            if (s->pend == LP_A_PLAY) { t[0] = 'P'; t[1] = 'L'; t[2] = 'A'; t[3] = 'Y'; }
        }
    }
    if (s->pend == LP_A_DEL) {
        t[0] = 'D'; t[1] = 'E'; t[2] = 'L'; t[3] = ' '; t[4] = 'W'; t[5] = 'A'; t[6] = 'I'; t[7] = 'T';
    }
    if (s->pend) {
        /* countdown blink: the period (1/8-beat units) is the remaining time / 4 as a power
         * of 2, from 2 bars down to an 1/8 note (Luca), so it doubles as the time halves */
        u = s->pu - s->gu;
        k = 4;
        while (k < 64 && k + k <= (u >> 2)) k += k;
        if (s->pwait >= 0) k = 4;                       /* IMMED: about to happen */
        lev = ((float)(s->gu & (k - 1)) + s->gf < 0.5f * (float)k) ? LP_PWM : 0;
        hide = (lev == 0);
    }
    if (hide) prog = 0;                                 /* only the bar blinks (Luca) */
    if (prog < 0) prog = 0;
    if (prog > 256) prog = 256;
    w0 = (unsigned int)(unsigned char)t[0] | ((unsigned int)(unsigned char)t[1] << 8)
       | ((unsigned int)(unsigned char)t[2] << 16) | ((unsigned int)(unsigned char)t[3] << 24);
    w1 = (unsigned int)(unsigned char)t[4] | ((unsigned int)(unsigned char)t[5] << 8)
       | ((unsigned int)(unsigned char)t[6] << 16) | ((unsigned int)(unsigned char)t[7] << 24);
    b[LS_TEXT] = w0; b[LS_TEXT + 1] = w1;
    b[LS_LEVEL] = (unsigned int)lev;
    b[LS_PROG] = (unsigned int)prog;
    b[LS_INV] = 0u;                                     /* the box never inverts (Luca) */
    if (!b[LS_MINE] || b[LS_LEDON] == 0u || b[LS_LEDOFF] == 0u) { s->led = -1; return; }
    s->pwm++;
    if (s->pwm >= LP_PWM) s->pwm = 0;
    lit = (s->pwm < lev);
    if (lit != s->led) {
        *(volatile unsigned int *)(uintptr_t)(lit ? b[LS_LEDON] : b[LS_LEDOFF]) = b[LS_LEDMASK];
        s->led = lit;
    }
}

/* ---- on-screen text ------------------------------------------------------ */
LP_ALWAYS_INLINE(lp_put_int)
static inline int lp_put_int(int n, char *out)
{
    int h = 0, t = 0, len = 0;
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    if (h > 0 || t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

LP_ALWAYS_INLINE(lp_put5)
static inline int lp_put5(char *out, int a, int b, int c, int d, int e)
{
    int n = 1;
    out[0] = (char)a;
    if (b) { out[1] = (char)b; n = 2; }
    if (c) { out[2] = (char)c; n = 3; }
    if (d) { out[3] = (char)d; n = 4; }
    if (e) { out[4] = (char)e; n = 5; }
    out[n] = 0;
    return n;
}

/* "1BAR".."16BAR" */
LP_ALWAYS_INLINE(lp_put_bars)
static inline int lp_put_bars(int n, char *out)
{
    int k = lp_put_int(n, out);
    out[k] = 'B'; out[k + 1] = 'A'; out[k + 2] = 'R'; out[k + 3] = 0;
    return k + 3;
}

/* knob 0 Mix: 0..100 */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value > 100u) value = 100u;
    return lp_put_int((int)value, out);
}

/* knob 1 Roll: OFF, 1/2, 1/4, 1/8, 1/16, 1/32 */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value == 0u) return lp_put5(out, 'O', 'F', 'F', 0, 0);
    if (value == 1u) return lp_put5(out, '1', '/', '2', 0, 0);
    if (value == 2u) return lp_put5(out, '1', '/', '4', 0, 0);
    if (value == 3u) return lp_put5(out, '1', '/', '8', 0, 0);
    if (value == 4u) return lp_put5(out, '1', '/', '1', '6', 0);
    return lp_put5(out, '1', '/', '3', '2', 0);
}

/* knob 2 Length: 0..7 shown as 1BAR..8BAR */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value > 7u) value = 7u;
    return lp_put_bars((int)value + 1, out);
}

/* knob 3 When: IMMED, BEAT, BAR, CYCLE */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value == 0u) return lp_put5(out, 'I', 'M', 'M', 'E', 'D');
    if (value == 1u) return lp_put5(out, 'B', 'E', 'A', 'T', 0);
    if (value == 2u) return lp_put5(out, 'B', 'A', 'R', 0, 0);
    return lp_put5(out, 'C', 'Y', 'C', 'L', 'E');
}

/* knob 4 Amount: 0..15 shown as 1BAR..16BAR */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value > 15u) value = 15u;
    return lp_put_bars((int)value + 1, out);
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef LOOPER_HOST_TEST

#include "looper_params.h"

#ifndef LOOPER_AUDIO_FUNC
#define LOOPER_AUDIO_FUNC Fx_DLY_Looper
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

LP_CODE_SECTION(LOOPER_AUDIO_FUNC)
void LOOPER_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase, bufBase;
    unsigned int span;
    LpState *s;
    LpParams P;
    float u[5], bpm, du;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;
    bufBase = (stateBase + sizeof(LpState) + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(LpState) || span < (end - base)) return;
    if (bufBase + 1024u > end) return;

    s = (LpState *)stateBase;

    u[0] = lp_ui(params[LOOPER_MIX_SLOT],    (float)LOOPER_MIX_UI_DEFAULT,    100.0f);
    u[1] = lp_ui(params[LOOPER_ROLL_SLOT],   (float)LOOPER_ROLL_UI_DEFAULT,   5.0f);
    u[2] = lp_ui(params[LOOPER_LENGTH_SLOT], (float)LOOPER_LENGTH_UI_DEFAULT, 7.0f);
    u[3] = lp_ui(params[LOOPER_WHEN_SLOT],   (float)LOOPER_WHEN_UI_DEFAULT,   3.0f);
    u[4] = lp_ui(params[LOOPER_AMOUNT_SLOT], (float)LOOPER_AMOUNT_UI_DEFAULT, 15.0f);

    lp_prepare(&P, u);
    if (s->magic != LP_MAGIC) lp_init(s, bufBase, (int)(end - bufBase));
    s->me = ctx[1];                              /* our claim on the looper services */
    bpm = lp_block(s, &P, params[0], &du);       /* on/off only counts presses */
    lp_process(s, &P, bpm, du, fxBuf);
    lp_out(s);
}

#endif /* LOOPER_HOST_TEST */
