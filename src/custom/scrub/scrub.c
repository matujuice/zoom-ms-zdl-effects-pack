/*
 * scrub.c - "Scrub": scrub through the recent past, freeze the grain where you stop, mono
 *
 * Everything that goes in is recorded into a 6 second buffer. A read head sits somewhere
 * in that buffer and plays a short GRAIN from there, over and over, each repeat
 * crossfading into the next so the loop has no seam. Position moves the head along the
 * last 4 seconds like a timeline, in 10 ms steps: 0 = 4 s ago, 400 = now. Turning Position slides the head and you hear the audio go by
 * (faster turns = faster, like dragging tape: forward plays higher, backward plays
 * reversed); stop turning and the grain under the head keeps looping: a freeze.
 *
 * REC (what the buffer does)
 *   LIVE   it keeps recording. Position is how far back the head reads, so the head moves
 *          with the music: a delay you can sweep (pitch bends while it moves, like a tape
 *          echo), and with Spray a granular cloud of the last few seconds.
 *   HOLD   recording stops: the last 6 seconds are kept and Position scrubs through them.
 *          Stop on a note and it rings forever. Back to LIVE and recording carries on.
 *   STOMP  the footswitch does it: while the effect is OFF it records (you hear your
 *          sound untouched), the moment you switch it ON the buffer freezes and you
 *          scrub what you just played. Untested: it needs the pedal to send the sound
 *          through a switched-off effect, which the stock delays rely on but has not been
 *          checked on this pedal. If ON only gives silence, use LIVE and HOLD.
 *
 * THE HEAD
 *   The head's distance from "now" is D = (400 - Position) x 10 ms. D glides toward the
 *   knob with a one-pole (Glide), once per 8-sample block, which also hides the knob's
 *   10 ms steps. Each grain starts 1.25 x Grain + D (+ Spray jitter) back and reads forward;
 *   while D glides, each running grain is pushed along with it, so a move is heard at once
 *   and not only when the next grain starts. The push is limited to 3 samples per sample.
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
 *   0.1 %). Reads use linear interpolation (the push makes ages fractional).
 *   Spray adds a random extra age to each new grain (up to 0.5 s), which turns a static
 *   loop into a moving cloud and breaks the buzz of very short frozen grains.
 *
 * MEMORY
 *   The buffer is 16-bit (scale 16384, so +-2.0 fits), 6 s = 264600 samples = 529 KB of
 *   the at least 705 KB arena. It is cleared 2048 samples per block after loading (about
 *   12 ms, the effect is dry meanwhile), so stale arena data is never played.
 *   The longest reach is 1.25 x Grain 1 s + Position 4 s + Spray 0.5 s = 5.75 s, inside the 6 s.
 *
 * KNOBS (screen values)
 *   0 Pos    0..400  where the head reads, 10 ms per step: 0 = 4 s ago, 400 = now (in HOLD
 *                    the freeze moment); shown as the time back, "NOW", "990ms", "4.00s".
 *                    400 steps instead of 100 so no Range knob is needed (Luca, 2026-10-03).
 *                    Read as raw x 100 always (the pedal passes screen / 100, so 4.00 here
 *                    means 400; the 3.05 guess in sc_ui would read it as 4)
 *   1 Grain  0..100  grain length, 10 ms .. 1 s (log); shown in ms / s
 *   2 Rec    0..2    LIVE / HOLD / STOMP (see REC)
 *   3 Glide  0..100  how slowly the head follows Position: 0 = jumps, 40 = about 0.1 s,
 *                    70 = about 1 s, 100 = about 3 s
 *   4 Spray  0..100  random offset of each grain, up to 0.5 s (squared curve)
 *   5 Mix    0..100  dry/wet crossfade, DJ style: dry full up to 50, wet full from 50,
 *                    both full at 50
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float or integer
 * division, no libm, no switch, no double / long long, no float-to-unsigned casts, every
 * helper forced inline, no calls. 2^x is built from a polynomial and the float exponent.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define SC_DO_PRAGMA(x) _Pragma(#x)
#define SC_EXPAND_PRAGMA(x) SC_DO_PRAGMA(x)
#define SC_ALWAYS_INLINE(fn) SC_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SC_CODE_SECTION(fn) SC_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SC_ALWAYS_INLINE(fn)
#define SC_CODE_SECTION(fn)
#endif

#define SC_MAGIC      0x53435231u        /* "SCR1": change whenever ScState changes */
#define SC_N          264600             /* buffer length: 6 s at 44.1 kHz              */
#define SC_AGE_MAX    264597.0f          /* oldest age a read may use (SC_N - 3)        */
#define SC_CLEAR_BLK  2048               /* samples cleared per block after loading     */
#define SC_TO16       16384.0f
#define SC_FROM16     6.1035156e-5f      /* 1 / 16384 */
#define SC_PUSH_MAX   3.0f               /* max head push, samples per sample           */
#define SC_FIN_STEP   0.0022675737f      /* wet fade-in after switching on: 10 ms       */
#define SC_XF_SCALE   2.0f               /* fade over the first 1/4 cycle: q = 2p, 0..0.5 */

#define SC_MODE_LIVE  0
#define SC_MODE_HOLD  1
#define SC_MODE_STOMP 2

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
    float fin;             /* wet fade-in 0..1                                */
    short buf[SC_N];
} ScState;

typedef struct {
    float inc;             /* grain phase increment per sample (1 / length)   */
    float len;             /* grain length, samples                           */
    float Dt;              /* head distance target, samples                   */
    float c;               /* Glide: one-pole coefficient per block          */
    float spray;           /* max random extra age, samples                   */
    float dryG, wetG;      /* Mix: DJ crossfade gains                         */
    int   mode;            /* SC_MODE_*                                       */
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

/* Pos knob: the pedal passes screen / 100 (0..4.00); always scale by 100, no guessing */
SC_ALWAYS_INLINE(sc_ui_pos)
static inline float sc_ui_pos(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 4.005f)) ui = def_ui;
    else ui = (float)(int)(raw * 100.0f + 0.5f);
    if (ui > 400.0f) ui = 400.0f;
    return ui;
}

SC_ALWAYS_INLINE(sc_init)
static inline void sc_init(ScState *s)
{
    s->rng = 0x5C2B1A37u;
    s->clr = 0; s->wp = 0; s->started = 0; s->was_off = 0;
    s->D = 0.0f; s->p = 0.0f; s->aC = 1.0f; s->aO = 1.0f; s->fin = 1.0f;
    s->magic = SC_MAGIC;
}

/* u[] = screen values in manifest order */
SC_ALWAYS_INLINE(sc_prepare)
static inline void sc_prepare(ScParams *P, const float *u)
{
    float k, e, sp, m;
    P->len = sc_grain_len(u[1]);
    P->inc = 0.0022675737f * sc_exp2(-u[1] * 0.06643856f);  /* 1 / len */
    P->Dt  = (400.0f - u[0]) * 441.0f;           /* 10 ms per step back from now */
    P->mode = (int)(u[2] + 0.5f);
    k = u[3] * 0.01f;
    e = 14.0f * k * (2.0f - k);                  /* 0..14: per-block coefficient 1 .. 2^-14 */
    P->c = sc_exp2(-e);
    sp = u[4] * 0.01f;
    P->spray = sp * sp * 22050.0f;
    m = u[5] * 0.01f;
    P->dryG = 2.0f - 2.0f * m; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * m;        if (P->wetG > 1.0f) P->wetG = 1.0f;
}

/* a new voice: 1.25 x Grain + D back from now, plus Spray */
SC_ALWAYS_INLINE(sc_start_age)
static inline float sc_start_age(ScState *s, const ScParams *P)
{
    return sc_clamp_age(1.25f * P->len + s->D + 1.0f + P->spray * sc_rand(s));
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
    float Dold, push, da, p = s->p, aC = s->aC, aO = s->aO, fin = s->fin;

    if (!s->started) { s->D = P->Dt; s->started = 1; aC = sc_start_age(s, P); aO = aC; }
    Dold = s->D;
    s->D += P->c * (P->Dt - s->D);
    push = (s->D - Dold) * 0.125f;               /* head movement per sample */
    if (push > SC_PUSH_MAX) push = SC_PUSH_MAX;
    if (push < -SC_PUSH_MAX) push = -SC_PUSH_MAX;
    da = rec ? push : push - 1.0f;               /* frozen: the voices read forward */

    for (i = 0; i < n; i++) {
        float in = buf[i], q, g, wet;
        if (rec) sc_write(s, in);
        p += P->inc;
        aO = sc_clamp_age(aO + da);
        if (p >= 1.0f) { p -= 1.0f; aO = aC; aC = sc_start_age(s, P); }
        else aC = sc_clamp_age(aC + da);
        q = p * SC_XF_SCALE;
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
    s->aC = sc_start_age(s, P);
    s->aO = s->aC;
    s->fin = 0.0f;
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

/* knob 1 Grain: screen 0..100 -> "10ms".."1.0s" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value > 100u) value = 100u;
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

/* knob 0 Pos: screen 0..400 -> how far back the head is: "4.00s".."1.00s", "990ms".."10ms",
 * "NOW" */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    int cs, len, w = 0, t = 0;
    if (value > 400u) value = 400u;
    cs = 400 - (int)value;                       /* hundredths of a second back */
    if (cs == 0) { out[0] = 'N'; out[1] = 'O'; out[2] = 'W'; out[3] = 0; return 3; }
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
    float u[6];
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
    if ((end - base) < sizeof(ScState) || span < (end - base)) return;
    if (stateBase + sizeof(ScState) > end) return;

    s = (ScState *)stateBase;

    u[0] = sc_ui_pos(params[SCRUB_POS_SLOT], (float)SCRUB_POS_UI_DEFAULT);
    u[1] = sc_ui(params[SCRUB_GRAIN_SLOT],  (float)SCRUB_GRAIN_UI_DEFAULT,  100.0f);
    u[2] = sc_ui(params[SCRUB_REC_SLOT],    (float)SCRUB_REC_UI_DEFAULT,    2.0f);
    u[3] = sc_ui(params[SCRUB_GLIDE_SLOT], (float)SCRUB_GLIDE_UI_DEFAULT, 100.0f);
    u[4] = sc_ui(params[SCRUB_SPRAY_SLOT],  (float)SCRUB_SPRAY_UI_DEFAULT,  100.0f);
    u[5] = sc_ui(params[SCRUB_MIX_SLOT],    (float)SCRUB_MIX_UI_DEFAULT,    100.0f);

    if (s->magic != SC_MAGIC) sc_init(s);
    if (sc_clearing(s)) return;                  /* first ~12 ms after loading: dry */

    sc_prepare(&P, u);
    if (params[0] < 0.5f) {                      /* effect switched off */
        s->was_off = 1;
        sc_bypassed(s, P.mode, fxBuf, 8);
        return;
    }
    if (s->was_off) { sc_switched_on(s, &P); s->was_off = 0; }
    sc_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* SCRUB_HOST_TEST */
