/*
 * sweep.c - "Sweep": one bar-synced swept effect, three engines on one LFO, mono
 *
 * A phaser, a flanger and a resonant filter in one effect, picked by the Type knob. All
 * three are swept by the same tempo-locked LFO, so a slow sweep can cover 1/4 note up to
 * 8 bars and stay on the bar. For synths and drum machines (it was never meant for guitar).
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float division, no
 * libm, no switch, every helper forced inline, no calls. The two reciprocals each block
 * need are Newton iterations, tan() is a short series, 2^x is built from the exponent bits.
 * Knob reads and coefficient set-up happen once per block of 8 samples, then the sample loop.
 *
 * ENGINES (Type)
 *   PH 4   phaser, 4 first-order all-pass stages (2 notches), dry + all-pass chain
 *   PH 8   phaser, 8 stages (4 notches, deeper and more vocal)
 *   FL +   flanger: short delay (0.1 .. 12 ms) added to the dry sound, positive feedback
 *   FL -   flanger with negative feedback: hollow, metallic, the odd harmonics ring
 *   LP     resonant low-pass (zero-delay-feedback state variable filter)
 *   BP     band-pass, the same filter, peak gain 1
 *   HP     high-pass
 *   NTCH   notch (band-reject); Reso narrows the notch
 *   The phaser and flanger wet signal is already 0.5 x (dry + processed), so Mix 100 is the
 *   whole phaser/flanger sound (notches included) and Mix 50 adds the dry on top, as the
 *   other effects do (DJ crossfade). The filters are 100 % wet at Mix 100.
 *
 * KNOBS (screen values)
 *   0 Type  0..7    PH 4 / PH 8 / FL + / FL - / LP / BP / HP / NTCH
 *   1 Rate  0..112  0..100 free, about 0.05 Hz to 8 Hz (shown in Hz); 101..112 synced to
 *                   Tempo, one full sweep (up and down) lasts 1/4 note (101), 1/2 (102),
 *                   3/4 (103), 1 bar (104), 1.5 bars (105), 2, 3, 4, 5, 6, 7, 8 bars (112)
 *   2 Depth 0..100  how far the sweep travels each way around Cntr, 0 = parked, 100 = +-2.5
 *                   octaves (phaser/filter frequency, flanger delay)
 *   3 Cntr  0..100  centre of the sweep, low to high: phaser notch / filter cutoff 80 Hz to
 *                   10 kHz (log); flanger delay 8 ms down to 0.3 ms (higher = shorter =
 *                   brighter, the same direction as the others)
 *   4 Reso  0..100  phaser feedback 0..85 %, flanger feedback 0..90 % (sign from Type),
 *                   filter resonance (Q 0.7 up to about 10; LP gets a gain make-up)
 *   5 Shape 0..5    TRI / SINE / RISE / FALL / SQR / RAND. TRI and SINE start at the centre
 *                   and rise, RISE = saw up, FALL = saw down, SQR = jumps, RAND = a glide to
 *                   a new random height twice per sweep
 *   6 Tone  0..100  phaser/flanger: how bright the feedback is (0 dark, 100 open);
 *                   filters: drive into the filter (0 clean, 100 hot, level kept about equal)
 *   7 Tempo 0..441  BPM 40..240 (0..39 = FOLLOW, see BAR TAG); 241..441 = the same BPMs again
 *                   (twin copy for the sync reset), shown as the BPM. Read raw x 100.
 *   8 Mix   0..100  DJ crossfade: dry full up to 50, wet full from 50
 *
 * TEMPO / SYNC RESET
 *   Same scheme as EuGate / DualShft (docs/TEMPO-SYNC.md). Flipping between a BPM and its twin
 *   (120 <-> 321) happens once per bar when a host sends it. With a synced Rate the LFO jumps
 *   to where it would be after that many bars, (4 x bars mod length) / length, so a 4 bar sweep
 *   keeps going across the flips instead of restarting each bar. With a free Rate a flip
 *   restarts the sweep at its centre. A plain tempo change restarts nothing. Switched off, the
 *   input passes untouched but the LFO keeps running and follows flips.
 *
 * BAR TAG (src/custom/common/drytag.h)
 *   Like the other tempo effects: Tempo on 0..39 = FOLLOW takes BPM and bars from the tag of an
 *   earlier slot, on any BPM the effect runs on its own. In slots 1-3, FOLLOW needs its Mozaic
 *   pad off.
 *
 * State: 1024-sample ring for the flanger (4 KB), 8 all-pass states, the SVF, the LFO. It lives
 * in the arena, validated by a magic number (change it when the struct changes).
 */

#include <stdint.h>
#include "../common/drytag.h"

#ifdef __TI_COMPILER_VERSION__
#define SW_DO_PRAGMA(x) _Pragma(#x)
#define SW_EXPAND_PRAGMA(x) SW_DO_PRAGMA(x)
#define SW_ALWAYS_INLINE(fn) SW_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SW_CODE_SECTION(fn) SW_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SW_ALWAYS_INLINE(fn)
#define SW_CODE_SECTION(fn)
#endif

#define SW_MAGIC        0x53573031u          /* "SW01" */
#define SW_BPM_MIN      40.0f
#define SW_BPM_MAX      240.0f
#define SW_TEMPO_MAX    441.0f
#define SW_TEMPO_TWIN   201.0f
#define SW_INC_PER_BPM  3.7793e-7f           /* 1 / (44100 * 60)                */
#define SW_HZ_TO_INC    2.2675737e-5f        /* 1 / 44100                       */
#define SW_RING         1024u                /* flanger ring, a power of two    */
#define SW_RING_MASK    1023u
#define SW_FLG_MAX      520.0f               /* longest flanger delay, samples  */
#define SW_FLG_MIN      2.0f

typedef struct {
    unsigned int magic;
    float ph;              /* LFO phase 0..1                                         */
    float rp;              /* second-cycle phase of the last block (RAND)            */
    float r0, r1;          /* RAND: the height it glides from and to                 */
    float lfo;             /* smoothed LFO value, -1..1                              */
    unsigned int rng;
    unsigned int bt;       /* beats into the sweep at the last synced flip           */
    int twin;              /* Tempo on its twin copy (1) or not (0); -1 = not read yet */
    unsigned int was_off;
    unsigned int w;        /* ring write index                                       */
    float d;               /* current flanger delay, samples                          */
    float fbs;             /* damped feedback signal                                  */
    float ic1, ic2;        /* state variable filter memory                            */
    float ap[8];           /* all-pass memory                                         */
    DtSync sync;           /* bar tag to and from other slots (drytag.h)              */
    float ring[SW_RING];
} SwState;

typedef struct {
    float inc;             /* LFO phase per sample                                    */
    float G;               /* all-pass coefficient g / (1 + g)                         */
    float fb, cd;          /* feedback gain (signed), feedback damping coefficient    */
    float d_new;           /* flanger delay at the end of the block, samples          */
    float a1, a2, a3, k;   /* state variable filter                                    */
    float cx, cb, cl;      /* filter output mix: cx * in + cb * k * bp + cl * lp       */
    float dg, mk;          /* filter drive and its level make-up                       */
    float gain;            /* phaser/flanger wet gain at high feedback                 */
    float dryG, wetG;
    unsigned int stages, flip_sync, bt_len;
    float inv_len;
    unsigned int type;
} SwParams;

/* The pedal hands every knob over as (screen number) / 100. Convert back. */
SW_ALWAYS_INLINE(sw_ui)
static inline float sw_ui(float raw, float def_ui, float max_ui)
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

/* Tempo knob (screen 0..441): raw x 100 up to 4.415, never through the 3.05 guess. */
SW_ALWAYS_INLINE(sw_tempo_ui)
static inline float sw_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > SW_TEMPO_MAX) ui = SW_TEMPO_MAX;
    return ui;
}

SW_ALWAYS_INLINE(sw_tempo_bpm)
static inline float sw_tempo_bpm(float ui)
{
    if (ui > SW_BPM_MAX) ui -= SW_TEMPO_TWIN;
    if (ui < SW_BPM_MIN) ui = SW_BPM_MIN;
    if (ui > SW_BPM_MAX) ui = SW_BPM_MAX;
    return ui;
}

SW_ALWAYS_INLINE(sw_twin_flip)
static inline int sw_twin_flip(SwState *s, float tempo_ui)
{
    int tw = (tempo_ui > SW_BPM_MAX) ? 1 : 0;
    int flip = (s->twin >= 0 && tw != s->twin);
    s->twin = tw;
    return flip;
}

/* 1 / x for x > 0: a first guess from the float bits, then three Newton steps (no divide) */
SW_ALWAYS_INLINE(sw_recip)
static inline float sw_recip(float x)
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

/* 2^x for x in -20..20: the whole part from the exponent bits, the rest by a polynomial
 * on -0.5..0.5 (error about 2e-6). */
SW_ALWAYS_INLINE(sw_exp2)
static inline float sw_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int n;
    float f, e, r;
    if (x > 20.0f) x = 20.0f;
    if (x < -20.0f) x = -20.0f;
    n = (int)(x + 32.5f) - 32;                  /* x rounded to the nearest whole number */
    f = x - (float)n;
    e = f * 0.6931472f;
    r = 1.0f + e * (1.0f + e * (0.5f + e * (0.16666667f + e * (0.041666668f + e * 0.008333334f))));
    c.u = (unsigned int)(n + 127) << 23;
    return r * c.f;
}

/* tan(pi * f / 44100) for f up to 10 kHz: the series to w^7, error under 0.2 % */
SW_ALWAYS_INLINE(sw_tan)
static inline float sw_tan(float f)
{
    float w = f * 7.1233e-5f;
    float w2 = w * w;
    return w * (1.0f + w2 * (0.33333334f + w2 * (0.13333334f + w2 * 0.053968254f)));
}

/* Cubic soft clip: flat at +-1.5 -> +-1.0, almost linear below ~0.6. */
SW_ALWAYS_INLINE(sw_clip)
static inline float sw_clip(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - x * x * x * 0.14814815f;
}

/* Triangle starting at 0 and rising: ph in [0,1) -> [-1,+1]. */
SW_ALWAYS_INLINE(sw_tri)
static inline float sw_tri(float ph)
{
    float q = ph + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return (q < 0.5f) ? (4.0f * q - 1.0f) : (3.0f - 4.0f * q);
}

/* sin(2*pi*ph) = sin(pi/2 * triangle): odd polynomial, error < 1e-4. */
SW_ALWAYS_INLINE(sw_sine)
static inline float sw_sine(float ph)
{
    float t = sw_tri(ph);
    float t2 = t * t;
    return t * (1.5706268f + t2 * (-0.6432292f + t2 * 0.0727102f));
}

/* Rate screen 101..112 -> the sweep length in beats (1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 28,
 * 32) and its reciprocal. An if-chain, never a switch. */
SW_ALWAYS_INLINE(sw_beats)
static inline unsigned int sw_beats(unsigned int r, float *inv)
{
    if (r <= 101u) { *inv = 1.0f;         return 1u; }
    if (r == 102u) { *inv = 0.5f;         return 2u; }
    if (r == 103u) { *inv = 0.33333334f;  return 3u; }
    if (r == 104u) { *inv = 0.25f;        return 4u; }
    if (r == 105u) { *inv = 0.16666667f;  return 6u; }
    if (r == 106u) { *inv = 0.125f;       return 8u; }
    if (r == 107u) { *inv = 0.083333336f; return 12u; }
    if (r == 108u) { *inv = 0.0625f;      return 16u; }
    if (r == 109u) { *inv = 0.05f;        return 20u; }
    if (r == 110u) { *inv = 0.041666668f; return 24u; }
    if (r == 111u) { *inv = 0.035714287f; return 28u; }
    *inv = 0.03125f;                      return 32u;
}

SW_ALWAYS_INLINE(sw_init)
static inline void sw_init(SwState *s)
{
    unsigned int i;
    s->ph = 0.0f; s->rp = 0.0f; s->r0 = 0.0f; s->r1 = 0.7f; s->lfo = 0.0f;
    s->rng = 0x2545F491u; s->bt = 0u; s->twin = -1; s->was_off = 0u;
    s->w = 0u; s->d = 100.0f; s->fbs = 0.0f; s->ic1 = 0.0f; s->ic2 = 0.0f;
    for (i = 0u; i < 8u; i++) s->ap[i] = 0.0f;
    for (i = 0u; i < SW_RING; i++) s->ring[i] = 0.0f;
    dt_sync_init(&s->sync);
    s->magic = SW_MAGIC;
}

/* Clear the filter memory when the effect is switched on again (the ring may keep old audio:
 * the longest read is 12 ms). */
SW_ALWAYS_INLINE(sw_wake)
static inline void sw_wake(SwState *s)
{
    unsigned int i;
    s->fbs = 0.0f; s->ic1 = 0.0f; s->ic2 = 0.0f;
    for (i = 0u; i < 8u; i++) s->ap[i] = 0.0f;
    s->was_off = 0u;
}

/* The LFO value for the block that starts at the current phase. */
SW_ALWAYS_INLINE(sw_lfo)
static inline float sw_lfo(SwState *s, unsigned int shape)
{
    float v, ph = s->ph;
    if (shape == 0u) v = sw_tri(ph);
    else if (shape == 1u) v = sw_sine(ph);
    else if (shape == 2u) v = 2.0f * ph - 1.0f;
    else if (shape == 3u) v = 1.0f - 2.0f * ph;
    else if (shape == 4u) v = (ph < 0.5f) ? 1.0f : -1.0f;
    else {
        float p2 = ph + ph, f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        if (p2 < s->rp) {                       /* half a sweep passed: a new random height */
            s->r0 = s->r1;
            s->rng = s->rng * 1664525u + 1013904223u;
            s->r1 = (float)(int)(s->rng >> 9) * 2.3841858e-7f - 1.0f;
        }
        s->rp = p2;
        f = p2 * p2 * (3.0f - 2.0f * p2);
        v = s->r0 + (s->r1 - s->r0) * f;
    }
    s->lfo += 0.3f * (v - s->lfo);              /* a few blocks of glide: jumps never click */
    return s->lfo;
}

SW_ALWAYS_INLINE(sw_prepare)
static inline void sw_prepare(SwState *s, SwParams *P, const float *u, float bpm)
{
    unsigned int type = (unsigned int)(int)u[0], rate = (unsigned int)(int)u[1];
    float depth = u[2] * 0.025f, cntr = u[3];
    float r = u[4] * 0.01f, t = u[6] * 0.01f, m = u[8] * 0.01f;
    float lfo, f, g;

    P->type = type;
    P->flip_sync = (rate >= 101u) ? 1u : 0u;
    P->bt_len = 1u; P->inv_len = 1.0f;
    if (rate >= 101u) {
        P->bt_len = sw_beats(rate, &P->inv_len);
        P->inc = bpm * SW_INC_PER_BPM * P->inv_len;
    } else {
        P->inc = 0.05f * sw_exp2((float)rate * 0.0732f) * SW_HZ_TO_INC;
    }

    lfo = sw_lfo(s, (unsigned int)(int)u[5]);

    P->dryG = 2.0f - 2.0f * m; if (P->dryG > 1.0f) P->dryG = 1.0f;
    P->wetG = 2.0f * m;        if (P->wetG > 1.0f) P->wetG = 1.0f;
    P->cd = 0.05f + 0.95f * t * t;
    P->gain = 1.0f;

    /* phaser / filter share the cutoff: 80 Hz at Cntr 0, 10 kHz at 100, swept in octaves */
    f = 80.0f * sw_exp2(cntr * 0.0697f + depth * lfo);
    if (f < 30.0f) f = 30.0f;
    if (f > 10000.0f) f = 10000.0f;
    g = sw_tan(f);

    if (type < 2u) {                            /* phaser */
        P->stages = (type == 0u) ? 4u : 8u;
        P->G = g * sw_recip(1.0f + g);
        P->fb = 0.85f * r;
        P->gain = 1.0f - 0.5f * P->fb;
    } else if (type < 4u) {                     /* flanger */
        float d = 352.8f * sw_exp2(-0.0474f * cntr - depth * lfo);
        if (d < SW_FLG_MIN) d = SW_FLG_MIN;
        if (d > SW_FLG_MAX) d = SW_FLG_MAX;
        P->d_new = d;
        P->fb = (type == 2u) ? 0.9f * r : -0.9f * r;
        P->gain = 1.0f - 0.5f * ((P->fb < 0.0f) ? -P->fb : P->fb);
    } else {                                    /* filter */
        P->k = 1.4f - 1.3f * r;
        P->a1 = sw_recip(1.0f + g * (g + P->k));
        P->a2 = g * P->a1;
        P->a3 = g * P->a2;
        P->dg = 1.0f + 5.0f * t * t;
        P->mk = sw_recip(1.0f + 0.4f * (P->dg - 1.0f));
        if (type == 4u)      { P->cx = 0.0f; P->cb = 0.0f;  P->cl = 0.3f + 0.7f * P->k * 0.71428573f; }
        else if (type == 5u) { P->cx = 0.0f; P->cb = 1.0f;  P->cl = 0.0f; }
        else if (type == 6u) { P->cx = 1.0f; P->cb = -1.0f; P->cl = -1.0f; }
        else                 { P->cx = 1.0f; P->cb = -1.0f; P->cl = 0.0f; }
    }
}

SW_ALWAYS_INLINE(sw_phaser)
static inline void sw_phaser(SwState *s, const SwParams *P, float *buf, int n)
{
    int i;
    unsigned int k;
    float fbs = s->fbs;
    for (i = 0; i < n; i++) {
        float in = buf[i], y = in + P->fb * fbs, v, lp;
        for (k = 0u; k < P->stages; k++) {
            v = (y - s->ap[k]) * P->G;
            lp = v + s->ap[k];
            s->ap[k] = lp + v;
            y = 2.0f * lp - y;
        }
        fbs += P->cd * (sw_clip(y) - fbs);
        buf[i] = P->dryG * in + P->wetG * (0.5f * P->gain * (in + y));
    }
    s->fbs = fbs;
}

SW_ALWAYS_INLINE(sw_flanger)
static inline void sw_flanger(SwState *s, const SwParams *P, float *buf, int n)
{
    int i;
    float fbs = s->fbs, d = s->d, dd = (P->d_new - s->d) * 0.125f;
    unsigned int w = s->w;
    for (i = 0; i < n; i++) {
        float in = buf[i], pos, fr, a, b, dl;
        unsigned int i0, i1;
        d += dd;
        pos = (float)w + 1024.0f - d;
        i0 = (unsigned int)(int)pos;
        fr = pos - (float)(int)i0;
        i0 &= SW_RING_MASK;
        i1 = (i0 + 1u) & SW_RING_MASK;
        a = s->ring[i0]; b = s->ring[i1];
        dl = a + fr * (b - a);
        fbs += P->cd * (sw_clip(dl) - fbs);
        s->ring[w] = in + P->fb * fbs;
        w = (w + 1u) & SW_RING_MASK;
        buf[i] = P->dryG * in + P->wetG * (0.5f * P->gain * (in + dl));
    }
    s->fbs = fbs; s->d = d; s->w = w;
}

SW_ALWAYS_INLINE(sw_filter)
static inline void sw_filter(SwState *s, const SwParams *P, float *buf, int n)
{
    int i;
    float ic1 = s->ic1, ic2 = s->ic2;
    for (i = 0; i < n; i++) {
        float in = buf[i], x, v1, v2, y;
        x = sw_clip(in * P->dg) * P->mk;
        v1 = P->a1 * ic1 + P->a2 * (x - ic2);
        v2 = ic2 + P->a2 * ic1 + P->a3 * (x - ic2);
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        y = P->cx * x + P->cb * (P->k * v1) + P->cl * v2;
        buf[i] = P->dryG * in + P->wetG * sw_clip(y);
    }
    if (ic1 < 1e-15f && ic1 > -1e-15f) ic1 = 0.0f;
    if (ic2 < 1e-15f && ic2 > -1e-15f) ic2 = 0.0f;
    s->ic1 = ic1; s->ic2 = ic2;
}

/* The LFO moves on by one block; a flip first puts it where the bar says it is. */
SW_ALWAYS_INLINE(sw_advance)
static inline void sw_advance(SwState *s, const SwParams *P, int flip)
{
    if (flip) {
        if (P->flip_sync) {
            s->bt += 4u;
            while (s->bt >= P->bt_len) s->bt -= P->bt_len;
            s->ph = (float)s->bt * P->inv_len;
        } else {
            s->ph = 0.0f;
        }
        s->rp = 0.0f;
    } else {
        s->ph += P->inc * 8.0f;
        if (s->ph >= 1.0f) s->ph -= 1.0f;
    }
}

int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'P'; out[1] = 'H'; out[2] = ' '; out[3] = '4'; out[4] = 0; return 4; }
    if (value == 1u) { out[0] = 'P'; out[1] = 'H'; out[2] = ' '; out[3] = '8'; out[4] = 0; return 4; }
    if (value == 2u) { out[0] = 'F'; out[1] = 'L'; out[2] = ' '; out[3] = '+'; out[4] = 0; return 4; }
    if (value == 3u) { out[0] = 'F'; out[1] = 'L'; out[2] = ' '; out[3] = '-'; out[4] = 0; return 4; }
    if (value == 4u) { out[0] = 'L'; out[1] = 'P'; out[2] = 0; return 2; }
    if (value == 5u) { out[0] = 'B'; out[1] = 'P'; out[2] = 0; return 2; }
    if (value == 6u) { out[0] = 'H'; out[1] = 'P'; out[2] = 0; return 2; }
    out[0] = 'N'; out[1] = 'T'; out[2] = 'C'; out[3] = 'H'; out[4] = 0;
    return 4;
}

/* Rate: free 0..100 as Hz (".05Hz" .. "8.0Hz"), 101..112 as the sweep length */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value >= 101u) {
        unsigned int v = value;
        if (v > 112u) v = 112u;
        if (v == 101u) { out[0] = '1'; out[1] = '/'; out[2] = '4'; out[3] = 0; return 3; }
        if (v == 102u) { out[0] = '1'; out[1] = '/'; out[2] = '2'; out[3] = 0; return 3; }
        if (v == 103u) { out[0] = '3'; out[1] = '/'; out[2] = '4'; out[3] = 0; return 3; }
        if (v == 105u) { out[0] = '1'; out[1] = '.'; out[2] = '5'; out[3] = 'B'; out[4] = 0; return 4; }
        {
            int bars = 1;                       /* 104 = 1 bar, 106 = 2, 107 = 3, ... 112 = 8 */
            if (v >= 106u) bars = (int)v - 104;
            out[0] = (char)('0' + bars); out[1] = 'B'; out[2] = 'A'; out[3] = 'R'; out[4] = 0;
            return 4;
        }
    } else {
        int hz = (int)(0.05f * sw_exp2((float)(int)value * 0.0732f) * 100.0f + 0.5f);
        int a = 0, b = 0, len = 0;
        if (hz < 100) {
            while (hz >= 10) { hz -= 10; a++; }
            out[0] = '.'; out[1] = (char)('0' + a); out[2] = (char)('0' + hz);
            len = 3;
        } else {
            hz += 5;                            /* round to tenths */
            while (hz >= 100) { hz -= 100; a++; }
            while (hz >= 10) { hz -= 10; b++; }
            out[0] = (char)('0' + a); out[1] = '.'; out[2] = (char)('0' + b);
            len = 3;
        }
        out[len] = 'H'; out[len + 1] = 'z'; out[len + 2] = 0;
        return len + 2;
    }
}

int ZDL_GetLabel_5(unsigned int value, char *out)
{
    if (value == 0u) { out[0] = 'T'; out[1] = 'R'; out[2] = 'I'; out[3] = 0; return 3; }
    if (value == 1u) { out[0] = 'S'; out[1] = 'I'; out[2] = 'N'; out[3] = 'E'; out[4] = 0; return 4; }
    if (value == 2u) { out[0] = 'R'; out[1] = 'I'; out[2] = 'S'; out[3] = 'E'; out[4] = 0; return 4; }
    if (value == 3u) { out[0] = 'F'; out[1] = 'A'; out[2] = 'L'; out[3] = 'L'; out[4] = 0; return 4; }
    if (value == 4u) { out[0] = 'S'; out[1] = 'Q'; out[2] = 'R'; out[3] = 0; return 3; }
    out[0] = 'R'; out[1] = 'A'; out[2] = 'N'; out[3] = 'D'; out[4] = 0;
    return 4;
}

int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value <= 39u) return dt_follow_text(out);
    if (value > 441u) value = 441u;
    n = (int)sw_tempo_bpm((float)(int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

#ifndef SWEEP_HOST_TEST

#include "sweep_params.h"

#ifndef SWEEP_AUDIO_FUNC
#define SWEEP_AUDIO_FUNC Fx_DLY_Sweep
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SW_CODE_SECTION(SWEEP_AUDIO_FUNC)
void SWEEP_AUDIO_FUNC(unsigned int *ctx)
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
    SwState *s;
    SwParams P;
    float u[9], bpm;
    int i, flip;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(SwState) || span < (end - base)) return;
    if (stateBase + sizeof(SwState) > end) return;

    s = (SwState *)stateBase;

    u[0] = sw_ui(params[SWEEP_TYPE_SLOT],  (float)SWEEP_TYPE_UI_DEFAULT,  7.0f);
    u[1] = sw_ui(params[SWEEP_RATE_SLOT],  (float)SWEEP_RATE_UI_DEFAULT,  112.0f);
    u[2] = sw_ui(params[SWEEP_DEPTH_SLOT], (float)SWEEP_DEPTH_UI_DEFAULT, 100.0f);
    u[3] = sw_ui(params[SWEEP_CNTR_SLOT],  (float)SWEEP_CNTR_UI_DEFAULT,  100.0f);
    u[4] = sw_ui(params[SWEEP_RESO_SLOT],  (float)SWEEP_RESO_UI_DEFAULT,  100.0f);
    u[5] = sw_ui(params[SWEEP_SHAPE_SLOT], (float)SWEEP_SHAPE_UI_DEFAULT, 5.0f);
    u[6] = sw_ui(params[SWEEP_TONE_SLOT],  (float)SWEEP_TONE_UI_DEFAULT,  100.0f);
    u[7] = sw_tempo_ui(params[SWEEP_TEMPO_SLOT], (float)SWEEP_TEMPO_UI_DEFAULT);
    u[8] = sw_ui(params[SWEEP_MIX_SLOT],   (float)SWEEP_MIX_UI_DEFAULT,   100.0f);

    if (s->magic != SW_MAGIC) sw_init(s);
    /* bar tag: bars from earlier slots flip the Tempo copy too, FOLLOW takes their BPM */
    u[7] = dt_tempo(&s->sync, dryBuf ? dryBuf + 8 : 0, u[7], dt_id(stateBase));
    bpm = sw_tempo_bpm(u[7]);
    flip = sw_twin_flip(s, u[7]);
    sw_prepare(s, &P, u, bpm);
    if (params[0] < 0.5f) {                      /* effect switched off: input untouched */
        s->was_off = 1u;
        sw_advance(s, &P, flip);                 /* the sweep keeps running, follows the bar */
        return;
    }
    if (s->was_off) sw_wake(s);
    if (P.type < 2u)      sw_phaser(s, &P, fxBuf, 8);
    else if (P.type < 4u) sw_flanger(s, &P, fxBuf, 8);
    else                  sw_filter(s, &P, fxBuf, 8);
    sw_advance(s, &P, flip);

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* SWEEP_HOST_TEST */
