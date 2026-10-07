/*
 * DryPrb: can one effect leave a marker for later effects in a buffer the output ignores?
 *
 * A hardware probe, not a release effect (build only when named: py build_all.py dryprb).
 * Background: ELynx's Div0 writes the Dry buffer (ctx[4]) and effects after it read it,
 * so a Dry buffer half can carry data from slot to slot (docs/TEMPO-SYNC.md). If that holds
 * on the MS-60B, a synced effect in slots 1..3 could mark each bar and effects in slots 4..6
 * could follow it. This probe only asks the yes/no questions.
 *
 * Knobs: Role OFF / SEND / READ, Where DRYR / DRYL / FXR, Shape DC / ALT / PULSE, Amp, Level.
 * Buffer layout is 8 left samples then 8 right samples (like the Fx buffer: left [0..7],
 * right [8..15]); that is assumed to hold for the Dry buffer too.
 *
 *  - OFF: the input passes through, nothing else happens.
 *  - SEND: writes the marker into the chosen half every block. The input is not touched.
 *    (The Fx right half is copied from left first, as every effect of the pack does, then
 *    the marker overwrites it when Where = FXR.)
 *  - READ: sums |marker| over the 8 samples of the chosen half. Found = above 0.0025 per
 *    sample on average. Found plays a steady 1378 Hz tone, not found a 40 ms 86 Hz blip
 *    once a second, both added to the left channel at Level. Shape PULSE makes the tone
 *    follow the on/off pulse.
 *
 * Tests (see README): SEND and READ side by side, with a stock effect between, across the
 * 3-slot edit limit, and SEND alone with Amp 100 to listen for leaks into the output.
 *
 * Plain float math, no division, no switch, no static arrays, no libm.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define DP_DO_PRAGMA(x) _Pragma(#x)
#define DP_EXPAND_PRAGMA(x) DP_DO_PRAGMA(x)
#define DP_ALWAYS_INLINE(fn) DP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define DP_CODE_SECTION(fn) DP_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define DP_ALWAYS_INLINE(fn)
#define DP_CODE_SECTION(fn)
#endif

#define DP_MAGIC        0x44505231u          /* "DPR1" */
#define DP_FOUND_SUM    0.02f                /* 8 samples x 0.0025 */
#define DP_BLIP_LEN     1764u                /* 40 ms */
#define DP_BLIP_EVERY   44100u
#define DP_SH_MID       5u                   /* 1378 Hz */
#define DP_SH_HUM       9u                   /*   86 Hz */
#define DP_PULSE_SH     15u                  /* 32768 samples = 0.74 s per half */

typedef struct {
    unsigned int magic;
    unsigned int ph;                         /* sample counter (tone phase, pulse clock) */
    unsigned int since;                      /* samples since the last not-found blip   */
} DpState;

typedef union { unsigned int u; float f; } DpBits;

/* The pedal hands every knob over as (screen number) / 100, whatever its maximum. */
DP_ALWAYS_INLINE(dp_ui)
static inline unsigned int dp_ui(float raw, unsigned int def_ui, unsigned int max_ui)
{
    unsigned int ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) return def_ui;
    if (raw <= 3.05f) raw = raw * 100.0f;
    ui = (unsigned int)(int)(raw + 0.5f);
    if (ui > max_ui) ui = max_ui;
    return ui;
}

/* triangle, period 1 << sh samples, -1..1 */
DP_ALWAYS_INLINE(dp_tri)
static inline float dp_tri(unsigned int ph, unsigned int sh)
{
    DpBits inv;
    unsigned int p = ph & ((1u << sh) - 1u);
    float v;
    inv.u = (127u + 2u - sh) << 23;          /* 4 / 2^sh, built from the exponent */
    v = (float)(int)p * inv.f;
    if (v > 2.0f) v = 4.0f - v;
    return v - 1.0f;
}

/* marker value for sample j of a block: 0 when the pulse is off */
DP_ALWAYS_INLINE(dp_marker)
static inline float dp_marker(unsigned int shape, unsigned int ph, unsigned int j, float amp)
{
    if (shape == 2u && ((ph >> DP_PULSE_SH) & 1u)) return 0.0f;
    if (shape == 1u && (j & 1u)) return -amp;
    return amp;
}

/* One 8-sample block. fx = Fx buffer, dry = Dry buffer (both 16 floats: L then R). */
DP_ALWAYS_INLINE(dp_block)
static inline void dp_block(DpState *s, unsigned int role, unsigned int where,
                            unsigned int shape, float amp, float lvl,
                            float *fx, float *dry)
{
    float *half;
    float sum = 0.0f;
    int found;
    int j;

    if (role == 1u) {                                        /* SEND */
        for (j = 0; j < 8; j++) fx[j + 8] = fx[j];
        half = (where == 2u) ? fx + 8 : (where == 1u) ? dry : dry + 8;
        for (j = 0; j < 8; j++) half[j] = dp_marker(shape, s->ph, (unsigned int)j, amp);
        s->ph += 8u;
        return;
    }
    if (role == 2u) {                                        /* READ */
        half = (where == 2u) ? fx + 8 : (where == 1u) ? dry : dry + 8;
        for (j = 0; j < 8; j++) {
            float v = half[j];
            if (v < 0.0f) v = -v;
            sum += v;
        }
        found = (sum > DP_FOUND_SUM);
        for (j = 0; j < 8; j++) {
            float v = 0.0f;
            if (found) v = dp_tri(s->ph, DP_SH_MID);
            else if (s->since < DP_BLIP_LEN) v = 0.3f * dp_tri(s->ph, DP_SH_HUM);
            s->since++;
            if (s->since >= DP_BLIP_EVERY) s->since = 0u;
            s->ph++;
            fx[j] += lvl * v;
        }
    }
    for (j = 0; j < 8; j++) fx[j + 8] = fx[j];               /* same signal to R */
}

/* ---- on-screen text ------------------------------------------------------ */
DP_ALWAYS_INLINE(dp_text)
static inline int dp_text(char *out, int a, int b, int c, int d, int e)
{
    int n = 0;
    if (a) { out[n] = (char)a; n++; }
    if (b) { out[n] = (char)b; n++; }
    if (c) { out[n] = (char)c; n++; }
    if (d) { out[n] = (char)d; n++; }
    if (e) { out[n] = (char)e; n++; }
    out[n] = 0;
    return n;
}

/* knob 0 Role */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 2u) return dp_text(out, 'R', 'E', 'A', 'D', 0);
    if (value == 1u) return dp_text(out, 'S', 'E', 'N', 'D', 0);
    return dp_text(out, 'O', 'F', 'F', 0, 0);
}

/* knob 1 Where */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value >= 2u) return dp_text(out, 'F', 'X', 'R', 0, 0);
    if (value == 1u) return dp_text(out, 'D', 'R', 'Y', 'L', 0);
    return dp_text(out, 'D', 'R', 'Y', 'R', 0);
}

/* knob 2 Shape */
int ZDL_GetLabel_2(unsigned int value, char *out)
{
    if (value >= 2u) return dp_text(out, 'P', 'U', 'L', 'S', 'E');
    if (value == 1u) return dp_text(out, 'A', 'L', 'T', 0, 0);
    return dp_text(out, 'D', 'C', 0, 0, 0);
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef DRYPRB_HOST_TEST

#include "dryprb_params.h"

#ifndef DRYPRB_AUDIO_FUNC
#define DRYPRB_AUDIO_FUNC Fx_DLY_DryPrb
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

DP_CODE_SECTION(DRYPRB_AUDIO_FUNC)
void DRYPRB_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *dryBuf = ZDL_PTR(float *, ctx[4]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span, role, where, shape;
    float amp, lvl;
    DpState *s;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(DpState) || span < (end - base)) return;
    if (stateBase + sizeof(DpState) > end) return;

    s = (DpState *)stateBase;
    if (params[0] < 0.5f) return;                /* effect bypassed */

    role  = dp_ui(params[DRYPRB_ROLE_SLOT],  DRYPRB_ROLE_UI_DEFAULT,  2u);
    where = dp_ui(params[DRYPRB_WHERE_SLOT], DRYPRB_WHERE_UI_DEFAULT, 2u);
    shape = dp_ui(params[DRYPRB_SHAPE_SLOT], DRYPRB_SHAPE_UI_DEFAULT, 2u);
    amp   = 0.01f  * (float)(int)dp_ui(params[DRYPRB_AMP_SLOT],   DRYPRB_AMP_UI_DEFAULT,   100u);
    lvl   = 0.005f * (float)(int)dp_ui(params[DRYPRB_LEVEL_SLOT], DRYPRB_LEVEL_UI_DEFAULT, 100u);

    if (s->magic != DP_MAGIC) { s->magic = DP_MAGIC; s->ph = 0u; s->since = 0u; }
    dp_block(s, role, where, shape, amp, lvl, fxBuf, dryBuf);
}

#endif /* DRYPRB_HOST_TEST */
