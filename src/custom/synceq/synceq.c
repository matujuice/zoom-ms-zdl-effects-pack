/*
 * synceq.c - "SyncEQ": EQ and tone shaping that also sends Mozaic's bar sync to later slots, mono
 *
 * WHY IT EXISTS
 *   Mozaic's bar sync can only edit slots 1-3 (the pedal ignores outside knob edits on slots
 *   4-6). An effect with a Tempo knob in slots 1-3 that Mozaic flips can pass the bar on to
 *   later slots through the Dry buffer (the bar tag, src/custom/common/drytag.h and
 *   docs/TEMPO-SYNC.md). SyncEQ is the sender for patches whose first three slots hold no
 *   tempo effect: put it in slot 1-3, turn that slot's pad on in Mozaic, and it marks every
 *   bar for the effects after it. The audio side is a plain EQ with a little drive, so the
 *   slot isn't wasted. The tempo effects of the pack send the same tag, so SyncEQ is only
 *   needed when none of them sits in slots 1-3.
 *
 * SENDING
 *   The Tempo knob runs 0..441 like every synced effect of the pack: 0..240 = the BPM (below
 *   40 reads as 40), 241..441 = a twin copy (BPM = screen - 201). Mozaic flips it between a
 *   BPM and its twin on each downbeat; each flip adds 1 to the bar counter in the tag. The
 *   tag also carries the BPM. Like the tempo effects, SyncEQ sends only while Mozaic flips it
 *   (LIVE: a flip in the last 60 s; Luca, 2026-10-06), also while switched off (then the
 *   input passes untouched, the tag still goes out). A LIVE tag from an earlier sender is
 *   left alone.
 *
 * AUDIO (in this order, every stage skipped while it is neutral)
 *   LoCut  high-pass 12 dB/oct, Q 0.707
 *   Low    shelf at 100 Hz       (RBJ cookbook, slope 1)
 *   Mid    bell at MidF, Q 0.9
 *   High   shelf at 8 kHz        (slope 1)
 *   HiCut  low-pass 12 dB/oct, Q 0.707
 *   Drive  soft clip x - 0.148 x^3 (flat top at 1.5), gain 1..8 in, 1/sqrt(gain) back out
 *   Level  -12..+12 dB
 *   With every knob at its default the sound passes unchanged. Coefficients are worked out
 *   only when a knob moves (sin/cos and 2^x by polynomial, 1/a0 by rsqrt squared).
 *
 * KNOBS (screen values)
 *   0 LoCut 0..50   0 = OFF, 1..50 = 20..500 Hz (log)
 *   1 Low   0..24   -12..+12 dB, 12 = flat
 *   2 Mid   0..24   -12..+12 dB, 12 = flat
 *   3 MidF  0..50   200 Hz..5 kHz (log), 25 = 1 kHz
 *   4 High  0..24   -12..+12 dB, 12 = flat
 *   5 HiCut 0..50   0..49 = 1..20 kHz (log), 50 = OFF
 *   6 Drive 0..100  0 = clean
 *   7 Tempo 0..441  BPM 40..240, twice (twin copy for the bar sync)
 *   8 Level 0..24   -12..+12 dB, 12 = 0 dB
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no division, no libm,
 * no switch, no double, every helper forced inline.
 */

#include <stdint.h>
#include "../common/drytag.h"

#ifdef __TI_COMPILER_VERSION__
#define SQ_DO_PRAGMA(x) _Pragma(#x)
#define SQ_EXPAND_PRAGMA(x) SQ_DO_PRAGMA(x)
#define SQ_ALWAYS_INLINE(fn) SQ_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define SQ_CODE_SECTION(fn) SQ_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define SQ_ALWAYS_INLINE(fn)
#define SQ_CODE_SECTION(fn)
#endif

#define SQ_MAGIC        0x53455132u          /* "SEQ2": change whenever SqState changes */
#define SQ_BPM_MIN      40.0f
#define SQ_BPM_MAX      240.0f
#define SQ_TEMPO_MAX    441.0f
#define SQ_TEMPO_TWIN   201.0f
#define SQ_W_PER_HZ     1.4247585e-4f        /* 2 pi / 44100 */
#define SQ_PI           3.14159265f
#define SQ_HALF_PI      1.57079633f
#define SQ_LOG2_25      4.64385619f          /* 20..500 Hz and 200..5000 Hz span 25x */
#define SQ_LOG2_20      4.32192809f          /* 1..20 kHz */
#define SQ_DB_A         0.08304820f          /* log2(10) / 40: 2^(dB * this) = 10^(dB/40) */
#define SQ_DB_V         0.16609640f          /* log2(10) / 20: amplitude */

typedef struct {
    float b0, b1, b2, a1, a2;                /* a0 folded in */
    float z1, z2;                            /* transposed direct form II */
} SqBq;

typedef struct {
    unsigned int magic;
    unsigned int k0, k1, k2, k3, k4, k5, k6, k8;   /* knob screen values the coefficients are for */
    unsigned int on;                         /* bit per stage: 1 LoCut, 2 Low, 4 Mid, 8 High, 16 HiCut, 32 Drive */
    SqBq hp, ls, pk, hs, lp;
    float drive, makeup, level;
    int   twin;                              /* Tempo on its twin copy; -1 = not read yet */
    DtSync tag;                              /* bar tag sender (drytag.h)               */
} SqState;

/* ---- helpers ----------------------------------------------------------------- */

/* The pedal hands every knob over as (screen number) / 100. Back to the screen integer. */
SQ_ALWAYS_INLINE(sq_ui)
static inline unsigned int sq_ui(float raw, unsigned int def_ui, unsigned int max_ui)
{
    float ui;
    unsigned int n;
    if (!(raw >= 0.0f && raw <= 300.0f)) return def_ui;
    ui = (raw <= 3.05f) ? raw * 100.0f : raw;
    n = (unsigned int)(int)(ui + 0.5f);
    if (n > max_ui) n = max_ui;
    return n;
}

/* Tempo: raw x 100 up to 4.415 (sq_ui's 3.05 guess would misread 4.41). Screen number. */
SQ_ALWAYS_INLINE(sq_tempo_ui)
static inline float sq_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > SQ_TEMPO_MAX) ui = SQ_TEMPO_MAX;
    return ui;
}

SQ_ALWAYS_INLINE(sq_tempo_bpm)
static inline float sq_tempo_bpm(float ui)
{
    if (ui > SQ_BPM_MAX) ui -= SQ_TEMPO_TWIN;
    if (ui < SQ_BPM_MIN) ui = SQ_BPM_MIN;
    if (ui > SQ_BPM_MAX) ui = SQ_BPM_MAX;
    return ui;
}

SQ_ALWAYS_INLINE(sq_twin_flip)
static inline int sq_twin_flip(SqState *s, float tempo_ui)
{
    int tw = (tempo_ui > SQ_BPM_MAX) ? 1 : 0;
    int flip = (s->twin >= 0 && tw != s->twin);
    s->twin = tw;
    return flip;
}

/* 2^x for x in about -10..20: whole part into the exponent, fraction by polynomial. */
SQ_ALWAYS_INLINE(sq_exp2)
static inline float sq_exp2(float x)
{
    union { float f; unsigned int u; } c;
    int i = (int)x;
    float f, p;
    if (x < (float)i) i--;
    f = x - (float)i;
    p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f
          + f * (0.00961813f + f * 0.00133336f))));
    c.u = (unsigned int)(i + 127) << 23;
    return p * c.f;
}

SQ_ALWAYS_INLINE(sq_rsqrt)
static inline float sq_rsqrt(float x)
{
    union { float f; unsigned int u; } c;
    float y, h = 0.5f * x;
    c.f = x;
    c.u = 0x5f3759dfu - (c.u >> 1);
    y = c.f;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return y;
}

/* 1/x for x > 0 */
SQ_ALWAYS_INLINE(sq_recip)
static inline float sq_recip(float x)
{
    float r = sq_rsqrt(x);
    return r * r;
}

/* sin(x) for x in -pi/2..pi/2 (Taylor to x^11, error < 1e-7) */
SQ_ALWAYS_INLINE(sq_sin_hp)
static inline float sq_sin_hp(float x)
{
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.008333333f + x2 * (-1.9841270e-4f
             + x2 * (2.7557319e-6f + x2 * -2.5052108e-8f)))));
}

/* sin and cos of w in 0..pi */
SQ_ALWAYS_INLINE(sq_sincos)
static inline void sq_sincos(float w, float *sn, float *cs)
{
    *sn = sq_sin_hp(w > SQ_HALF_PI ? SQ_PI - w : w);
    *cs = sq_sin_hp(SQ_HALF_PI - w);
}

/* Frequencies from the screen numbers (also used by the labels). */
SQ_ALWAYS_INLINE(sq_locut_hz)
static inline float sq_locut_hz(unsigned int n)        /* 1..50 -> 20..500 */
{
    return 20.0f * sq_exp2((float)(int)(n - 1u) * (SQ_LOG2_25 * 0.020408163f));
}

SQ_ALWAYS_INLINE(sq_midf_hz)
static inline float sq_midf_hz(unsigned int n)         /* 0..50 -> 200..5000 */
{
    return 200.0f * sq_exp2((float)(int)n * (SQ_LOG2_25 * 0.02f));
}

SQ_ALWAYS_INLINE(sq_hicut_hz)
static inline float sq_hicut_hz(unsigned int n)        /* 0..49 -> 1000..20000 */
{
    return 1000.0f * sq_exp2((float)(int)n * (SQ_LOG2_20 * 0.020408163f));
}

/* One biquad, RBJ cookbook, a0 folded in (1/a0 = rsqrt squared). One copy of this code:
 * sq_prepare redesigns at most one stage per block.
 *   t 0 = high-pass, 4 = low-pass (Q 0.707), 1 = low shelf, 3 = high shelf (slope 1),
 *   2 = bell (Q 0.9); db = -12..12 for 1..3. */
SQ_ALWAYS_INLINE(sq_design)
static inline void sq_design(SqBq *q, unsigned int t, float hz, float db)
{
    float sn, cs, A, sA, iA, al, b, am, ap, cam, cap, ta;
    float b0, b1, b2, a0, a1, a2, r;
    sq_sincos(hz * SQ_W_PER_HZ, &sn, &cs);
    A  = sq_exp2(db * SQ_DB_A);
    sA = sq_exp2(db * (0.5f * SQ_DB_A));
    iA = sq_recip(A);
    am = A - 1.0f; ap = A + 1.0f;
    cam = am * cs; cap = ap * cs;
    ta = 2.0f * sA * sn * 0.70710678f;           /* shelves: 2 sqrt(A) alpha */
    if (t == 0u || t == 4u) {                    /* high-pass / low-pass */
        al = sn * 0.70710678f;
        b = (t == 0u) ? 0.5f * (1.0f + cs) : 0.5f * (1.0f - cs);
        b0 = b; b1 = (t == 0u) ? -(b + b) : b + b; b2 = b;
        a0 = 1.0f + al; a1 = -2.0f * cs; a2 = 1.0f - al;
    } else if (t == 1u) {                        /* low shelf */
        b0 = A * (ap - cam + ta); b1 = 2.0f * A * (am - cap); b2 = A * (ap - cam - ta);
        a0 = ap + cam + ta; a1 = -2.0f * (am + cap); a2 = ap + cam - ta;
    } else if (t == 3u) {                        /* high shelf */
        b0 = A * (ap + cam + ta); b1 = -2.0f * A * (am + cap); b2 = A * (ap + cam - ta);
        a0 = ap - cam + ta; a1 = 2.0f * (am - cap); a2 = ap - cam - ta;
    } else {                                     /* bell */
        al = sn * 0.55555556f;
        b0 = 1.0f + al * A; b1 = -2.0f * cs; b2 = 1.0f - al * A;
        a0 = 1.0f + al * iA; a1 = -2.0f * cs; a2 = 1.0f - al * iA;
    }
    r = sq_recip(a0);
    q->b0 = b0 * r; q->b1 = b1 * r; q->b2 = b2 * r; q->a1 = a1 * r; q->a2 = a2 * r;
}

SQ_ALWAYS_INLINE(sq_bq)
static inline float sq_bq(SqBq *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

SQ_ALWAYS_INLINE(sq_flush)
static inline void sq_flush(SqBq *q)
{
    if (q->z1 < 1e-20f && q->z1 > -1e-20f) q->z1 = 0.0f;
    if (q->z2 < 1e-20f && q->z2 > -1e-20f) q->z2 = 0.0f;
}

SQ_ALWAYS_INLINE(sq_clear)
static inline void sq_clear(SqBq *q)
{
    q->b0 = 1.0f; q->b1 = 0.0f; q->b2 = 0.0f; q->a1 = 0.0f; q->a2 = 0.0f;
    q->z1 = 0.0f; q->z2 = 0.0f;
}

SQ_ALWAYS_INLINE(sq_init)
static inline void sq_init(SqState *s)
{
    s->k0 = s->k1 = s->k2 = s->k3 = s->k4 = s->k5 = s->k6 = s->k8 = 0xFFFFFFFFu;
    s->on = 0u;
    sq_clear(&s->hp); sq_clear(&s->ls); sq_clear(&s->pk); sq_clear(&s->hs); sq_clear(&s->lp);
    s->drive = 1.0f; s->makeup = 1.0f; s->level = 1.0f;
    s->twin = -1;
    dt_sync_init(&s->tag);
    s->magic = SQ_MAGIC;
}

/* Follow the knobs. k[] = screen values in knob order (k[7], Tempo, is not used here).
 * Drive and Level are cheap and follow at once; of the five filters only the first one whose
 * knob moved is redesigned in this block, the next one in the next block (8 samples later),
 * so the coefficient code exists once. */
SQ_ALWAYS_INLINE(sq_prepare)
static inline void sq_prepare(SqState *s, const unsigned int *k)
{
    unsigned int on = 0u, t = 9u;
    float base = 100.0f, oct = 0.0f, db = 0.0f;
    SqBq *q = &s->hp;
    if (k[6] != s->k6) {
        float d = (float)(int)k[6] * 0.01f;
        s->drive = 1.0f + 7.0f * d * d;
        s->makeup = sq_rsqrt(s->drive);
        s->k6 = k[6];
    }
    if (k[8] != s->k8) { s->level = sq_exp2(((float)(int)k[8] - 12.0f) * SQ_DB_V); s->k8 = k[8]; }

    if (k[0] != s->k0) {
        t = 0u; q = &s->hp; base = 20.0f;
        oct = (k[0] > 0u) ? (float)(int)(k[0] - 1u) * (SQ_LOG2_25 * 0.020408163f) : 0.0f;
        s->k0 = k[0];
    } else if (k[1] != s->k1) {
        t = 1u; q = &s->ls; db = (float)(int)k[1] - 12.0f; s->k1 = k[1];
    } else if (k[2] != s->k2 || k[3] != s->k3) {
        t = 2u; q = &s->pk; base = 200.0f; oct = (float)(int)k[3] * (SQ_LOG2_25 * 0.02f);
        db = (float)(int)k[2] - 12.0f; s->k2 = k[2]; s->k3 = k[3];
    } else if (k[4] != s->k4) {
        t = 3u; q = &s->hs; base = 8000.0f; db = (float)(int)k[4] - 12.0f; s->k4 = k[4];
    } else if (k[5] != s->k5) {
        t = 4u; q = &s->lp; base = 1000.0f;
        oct = (k[5] < 50u) ? (float)(int)k[5] * (SQ_LOG2_20 * 0.020408163f) : 0.0f;
        s->k5 = k[5];
    }
    if (t < 9u) sq_design(q, t, base * sq_exp2(oct), db);

    /* a stage runs only once its coefficients match its knob, and while it isn't neutral */
    if (k[0] > 0u   && s->k0 == k[0]) on |= 1u;
    if (k[1] != 12u && s->k1 == k[1]) on |= 2u;
    if (k[2] != 12u && s->k2 == k[2] && s->k3 == k[3]) on |= 4u;
    if (k[4] != 12u && s->k4 == k[4]) on |= 8u;
    if (k[5] < 50u  && s->k5 == k[5]) on |= 16u;
    if (k[6] > 0u)   on |= 32u;
    /* a stage coming back on starts from silence, not from old state */
    if ((on & 1u)  && !(s->on & 1u))  { s->hp.z1 = 0.0f; s->hp.z2 = 0.0f; }
    if ((on & 2u)  && !(s->on & 2u))  { s->ls.z1 = 0.0f; s->ls.z2 = 0.0f; }
    if ((on & 4u)  && !(s->on & 4u))  { s->pk.z1 = 0.0f; s->pk.z2 = 0.0f; }
    if ((on & 8u)  && !(s->on & 8u))  { s->hs.z1 = 0.0f; s->hs.z2 = 0.0f; }
    if ((on & 16u) && !(s->on & 16u)) { s->lp.z1 = 0.0f; s->lp.z2 = 0.0f; }
    s->on = on;
}

SQ_ALWAYS_INLINE(sq_process)
static inline void sq_process(SqState *s, float *buf, int n)
{
    unsigned int on = s->on;
    float g = s->drive, mk = s->makeup, lv = s->level;
    int i;
    for (i = 0; i < n; i++) {
        float x = buf[i];
        if (on & 1u)  x = sq_bq(&s->hp, x);
        if (on & 2u)  x = sq_bq(&s->ls, x);
        if (on & 4u)  x = sq_bq(&s->pk, x);
        if (on & 8u)  x = sq_bq(&s->hs, x);
        if (on & 16u) x = sq_bq(&s->lp, x);
        if (on & 32u) {
            float v = x * g;
            if (v > 1.5f) v = 1.0f;
            else if (v < -1.5f) v = -1.0f;
            else v = v - 0.14814815f * v * v * v;
            x = v * mk;
        }
        buf[i] = x * lv;
    }
    sq_flush(&s->hp); sq_flush(&s->ls); sq_flush(&s->pk); sq_flush(&s->hs); sq_flush(&s->lp);
}

/* ---- on-screen text (value = screen number, at most 5 characters) ---------- */

/* whole number 0..99999 */
SQ_ALWAYS_INLINE(sq_num)
static inline int sq_num(char *out, int len, int n)
{
    int d4 = 0, d3 = 0, d2 = 0, d1 = 0, started = 0;
    while (n >= 10000) { n -= 10000; d4++; }
    while (n >= 1000)  { n -= 1000;  d3++; }
    while (n >= 100)   { n -= 100;   d2++; }
    while (n >= 10)    { n -= 10;    d1++; }
    if (d4) { out[len] = (char)('0' + d4); len++; started = 1; }
    if (started || d3) { out[len] = (char)('0' + d3); len++; started = 1; }
    if (started || d2) { out[len] = (char)('0' + d2); len++; started = 1; }
    if (started || d1) { out[len] = (char)('0' + d1); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* Hz: "20".."999", "1.0k".."9.9k", "10k".."20k" */
SQ_ALWAYS_INLINE(sq_hz_text)
static inline int sq_hz_text(char *out, float hz)
{
    int n = (int)(hz + 0.5f), len;
    if (n < 1000) return sq_num(out, 0, n);
    if (n < 9950) {
        int t = (int)(hz * 0.01f + 0.5f), w = 0;     /* tenths of a kHz */
        while (t >= 10) { t -= 10; w++; }
        out[0] = (char)('0' + w); out[1] = '.'; out[2] = (char)('0' + t);
        out[3] = 'k'; out[4] = 0;
        return 4;
    }
    len = sq_num(out, 0, (int)(hz * 0.001f + 0.5f));
    out[len] = 'k'; len++; out[len] = 0;
    return len;
}

/* dB: screen 0..24 -> "-12".."0".."+12" */
SQ_ALWAYS_INLINE(sq_db_text)
static inline int sq_db_text(char *out, unsigned int value)
{
    int n;
    if (value > 24u) value = 24u;
    n = (int)value - 12;
    if (n < 0) { out[0] = '-'; return sq_num(out, 1, -n); }
    if (n > 0) { out[0] = '+'; return sq_num(out, 1, n); }
    return sq_num(out, 0, 0);
}

SQ_ALWAYS_INLINE(sq_off_text)
static inline int sq_off_text(char *out)
{
    out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0;
    return 3;
}

int ZDL_GetLabel_0(unsigned int value, char *out)      /* LoCut */
{
    if (value == 0u) return sq_off_text(out);
    if (value > 50u) value = 50u;
    return sq_hz_text(out, sq_locut_hz(value));
}

int ZDL_GetLabel_1(unsigned int value, char *out) { return sq_db_text(out, value); }   /* Low  */
int ZDL_GetLabel_2(unsigned int value, char *out) { return sq_db_text(out, value); }   /* Mid  */

int ZDL_GetLabel_3(unsigned int value, char *out)      /* MidF */
{
    if (value > 50u) value = 50u;
    return sq_hz_text(out, sq_midf_hz(value));
}

int ZDL_GetLabel_4(unsigned int value, char *out) { return sq_db_text(out, value); }   /* High */

int ZDL_GetLabel_5(unsigned int value, char *out)      /* HiCut */
{
    if (value >= 50u) return sq_off_text(out);
    return sq_hz_text(out, sq_hicut_hz(value));
}

int ZDL_GetLabel_7(unsigned int value, char *out)      /* Tempo: the BPM on both copies */
{
    if (value > 441u) value = 441u;
    return sq_num(out, 0, (int)sq_tempo_bpm((float)(int)value));
}

int ZDL_GetLabel_8(unsigned int value, char *out) { return sq_db_text(out, value); }   /* Level */

/* ---- pedal entry point ------------------------------------------------------- */
#ifndef SYNCEQ_HOST_TEST

#include "synceq_params.h"

#ifndef SYNCEQ_AUDIO_FUNC
#define SYNCEQ_AUDIO_FUNC Fx_DLY_SyncEQ
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

SQ_CODE_SECTION(SYNCEQ_AUDIO_FUNC)
void SYNCEQ_AUDIO_FUNC(unsigned int *ctx)
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
    SqState *s;
    unsigned int k[9];
    float tempo;
    int flip, i;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(SqState) || span < (end - base)) return;
    if (stateBase + sizeof(SqState) > end) return;

    s = (SqState *)stateBase;
    if (s->magic != SQ_MAGIC) sq_init(s);

    /* the bar tag goes out while Mozaic flips Tempo, on or off */
    tempo = sq_tempo_ui(params[SYNCEQ_TEMPO_SLOT], (float)SYNCEQ_TEMPO_UI_DEFAULT);
    flip = sq_twin_flip(s, tempo);
    if (dryBuf)
        dt_send(&s->tag, dryBuf + 8, flip,
                (unsigned int)(int)(sq_tempo_bpm(tempo) * 16.0f), dt_id(stateBase));

    if (params[0] < 0.5f) return;                /* switched off: input untouched */

    k[0] = sq_ui(params[SYNCEQ_LOCUT_SLOT], SYNCEQ_LOCUT_UI_DEFAULT, 50u);
    k[1] = sq_ui(params[SYNCEQ_LOW_SLOT],   SYNCEQ_LOW_UI_DEFAULT,   24u);
    k[2] = sq_ui(params[SYNCEQ_MID_SLOT],   SYNCEQ_MID_UI_DEFAULT,   24u);
    k[3] = sq_ui(params[SYNCEQ_MIDF_SLOT],  SYNCEQ_MIDF_UI_DEFAULT,  50u);
    k[4] = sq_ui(params[SYNCEQ_HIGH_SLOT],  SYNCEQ_HIGH_UI_DEFAULT,  24u);
    k[5] = sq_ui(params[SYNCEQ_HICUT_SLOT], SYNCEQ_HICUT_UI_DEFAULT, 50u);
    k[6] = sq_ui(params[SYNCEQ_DRIVE_SLOT], SYNCEQ_DRIVE_UI_DEFAULT, 100u);
    k[7] = 0u;
    k[8] = sq_ui(params[SYNCEQ_LEVEL_SLOT], SYNCEQ_LEVEL_UI_DEFAULT, 24u);

    sq_prepare(s, k);
    sq_process(s, fxBuf, 8);                     /* mono: left half in place */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R */
}

#endif /* SYNCEQ_HOST_TEST */
