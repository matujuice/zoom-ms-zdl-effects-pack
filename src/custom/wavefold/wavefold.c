/*
 * wavefold.c - "WaveFold": Buchla 259 style wavefolder, mono
 *
 * Based on the model in: F. Esqueda, H. Pontynen, V. Valimaki, J. D. Parker,
 * "Virtual Analog Buchla 259 Wavefolder", DAFx-2017 (Edinburgh).
 *
 * The 259 folder is five op-amp folding stages in PARALLEL, plus the direct
 * signal, added together. Each stage does nothing until the input passes its own
 * threshold, then it adds a straight line with its own slope (eqs. 13-17 of the
 * paper). The five thresholds are 0.6, 1.8, 2.994, 4.08 and 5.46 (volts). The sum
 * (eq. 18) is a transfer curve that rises, folds back, folds again, ... five
 * times, so a louder input is bent more and more, and the harmonics grow.
 * The paper then low-passes the result (first order, about 1.3 kHz).
 *
 * What is the same as the paper: the five stages, their thresholds, slopes,
 * weights and the summing; the direct path; the one-pole low-pass.
 * What is different: no oversampling or BLAMP anti-aliasing (too heavy for the
 * pedal), so high notes at high Drive alias. The Tone low-pass takes the edge off.
 * Added for the pedal: Drive (input volts), Symmetry (an offset, like the 259's
 * symmetry control), a DC blocker, an automatic level match, Level and Mix.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float
 * division, no libm, no switch, every helper forced inline, no calls.
 *
 * SIGNAL PATH (per sample), mono
 *   v = in * Drive + offset                 (clamped to +-8 V, like the op-amp rails)
 *   y = 5 v + sum of the five stages        (the transfer curve, see wf_fold)
 *   y -> DC blocker (y = x - x' + 0.9993 y') -> one-pole low-pass (Tone)
 *   wet = soft-ceiling(y * AutoLevel) * Level   out = dry/wet Mix
 *   (the ceiling keeps the matched wet signal inside +-1; the wet fades out when the
 *   input is silent so turning Symm never thumps)
 *
 * AUTO-LEVEL
 *   Two running mean-square meters (about 70 ms) measure the input and the wet
 *   signal after the filter. A gain `comp` is nudged up or down by a fixed
 *   fraction per sample until comp^2 * wet == input, so the wet loudness equals
 *   the input loudness whatever Drive, Symm or Tone are set to. No sqrt and no
 *   division: it only compares and steps. The gain holds when the input is silent.
 *   Level 50 = matched, below / above = quieter / louder.
 *
 * KNOBS (screen values)
 *   0 Drive 0..100  input gain 0.2 + 15 * (n/100)^2   (0.2x .. 15.2x, in volts)
 *   1 Symm  0..100  shown -50..+50, offset = +-2 V before the folder
 *   2 Tone  0..100  low-pass coefficient 0.03 + 0.97 * (n/100)^2 (about 200 Hz .. open)
 *   3 Level 0..100  0 .. 2x, 50 = 1x = output loudness equal to input loudness
 *   4 Mix   0..100  dry/wet
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define WF_DO_PRAGMA(x) _Pragma(#x)
#define WF_EXPAND_PRAGMA(x) WF_DO_PRAGMA(x)
#define WF_ALWAYS_INLINE(fn) WF_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define WF_CODE_SECTION(fn) WF_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define WF_ALWAYS_INLINE(fn)
#define WF_CODE_SECTION(fn)
#endif

#define WF_MAGIC        0x57463034u          /* "WF04" */
#define WF_DC_R         0.9993f              /* DC blocker pole                  */
#define WF_SMOOTH       0.003f               /* gain / offset smoothing per sample */
#define WF_ENV          0.0003f              /* loudness meters (~70 ms)         */
#define WF_LVL          0.003f               /* auto-level smoothing per sample (~7 ms) */
#define WF_VMAX         8.0f                 /* op-amp rails, in volts           */
#define WF_BIAS_V       2.0f                 /* Symm range, in volts             */

/* Fallback knob values when the whole parameter table reads 0. */
#define DEF_DRIVE       0.40f
#define DEF_SYMM        0.50f
#define DEF_TONE        0.60f
#define DEF_LEVEL       0.50f
#define DEF_MIX         1.0f

typedef struct {
    unsigned int magic;
    float g_s, b_s;        /* smoothed drive gain and offset          */
    float lp;              /* the low-pass pole                       */
    float dcx, dcy;        /* DC blocker memory                       */
    float ein, elp;        /* input / un-levelled wet loudness meters (mean square) */
    float comp;            /* auto-level gain                         */
    float gate;            /* fades the wet signal out when the input is silent */
} WfState;

typedef struct {
    float gain, bias, a, level, mix;
} WfParams;

WF_ALWAYS_INLINE(clamp01)
static inline float clamp01(float x)
{
    if (!(x >= 0.0f)) x = 0.0f;                  /* also catches NaN */
    if (x > 1.0f) x = 1.0f;
    return x;
}

/* 1 / sqrt(x) without libm and without a divide (x > 0): the classic exponent trick for a
 * first guess, then three Newton steps  y = y * (1.5 - 0.5 * x * y * y). */
WF_ALWAYS_INLINE(wf_rsqrt)
static inline float wf_rsqrt(float x)
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

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer, then scale by 1/max. */
WF_ALWAYS_INLINE(wf_knob)
static inline float wf_knob(float raw, float def_ui, float inv_max)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) ui = def_ui;
    else if (raw <= 3.05f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    return clamp01(ui * inv_max);
}

/* The Buchla 259 transfer curve (paper eqs. 13-18), input in volts.
 * Stage k adds  w_k * (m_k * v - c_k * sgn(v))  once |v| > t_k.  The numbers below
 * are w_k * m_k and w_k * c_k, already multiplied out:
 *   stage   t_k     w*m        w*c
 *     1     0.6     -9.9996    -6.0000
 *     4     1.8     10.1347    18.2435
 *     2     2.994  -10.4664   -31.3352
 *     5     4.08     9.7198    39.6611
 *     3     5.46    -6.0620   -33.0977
 * and the direct path adds 5 * v. */
WF_ALWAYS_INLINE(wf_fold)
static inline float wf_fold(float v)
{
    float a = v, s = 1.0f, y;
    if (v < 0.0f) { a = -v; s = -1.0f; }
    y = 5.0f * v;
    if (a > 0.6f)   y += -9.9996f  * v + 6.0000f  * s;
    if (a > 1.8f)   y +=  10.1347f * v - 18.2435f * s;
    if (a > 2.994f) y += -10.4664f * v + 31.3352f * s;
    if (a > 4.08f)  y +=  9.7198f  * v - 39.6611f * s;
    if (a > 5.46f)  y += -6.0620f  * v + 33.0977f * s;
    return y;
}

WF_ALWAYS_INLINE(wf_init)
static inline void wf_init(WfState *s, const WfParams *P)
{
    s->g_s = P->gain; s->b_s = P->bias;
    s->lp = 0.0f; s->dcy = 0.0f;
    s->dcx = wf_fold(P->bias);                           /* no start-up thump from the offset */
    s->ein = 0.0f; s->elp = 0.0f; s->comp = 0.1f; s->gate = 0.0f;
    s->magic = WF_MAGIC;
}

/* k[] = 0..1 by each knob's own maximum, in manifest order */
WF_ALWAYS_INLINE(wf_prepare)
static inline void wf_prepare(WfParams *P, const float *k)
{
    P->gain  = 0.2f + 15.0f * k[0] * k[0];
    P->bias  = (k[1] + k[1] - 1.0f) * WF_BIAS_V;
    P->a     = 0.03f + 0.97f * k[2] * k[2];
    P->level = k[3] + k[3];
    P->mix   = k[4];
}

WF_ALWAYS_INLINE(wf_process)
static inline void wf_process(WfState *s, const WfParams *P, float *buf, int n)
{
    int i;
    float g = s->g_s, b = s->b_s, lp = s->lp, dcx = s->dcx, dcy = s->dcy;
    float ein = s->ein, elp = s->elp, comp = s->comp, gate = s->gate;
    for (i = 0; i < n; i++) {
        float in = buf[i], v, y, d, w;
        g += WF_SMOOTH * (P->gain - g);
        b += WF_SMOOTH * (P->bias - b);
        v = in * g + b;
        if (v > WF_VMAX) v = WF_VMAX;
        if (v < -WF_VMAX) v = -WF_VMAX;
        y = wf_fold(v);
        d = y - dcx + WF_DC_R * dcy;                     /* DC blocker */
        dcx = y; dcy = d;
        lp += P->a * (d - lp);                           /* one-pole low-pass */
        /* auto-level: the gain is the ratio of the two loudness meters, so it
         * follows the input at once instead of creeping toward it note after note */
        ein += WF_ENV * (in * in - ein);
        elp += WF_ENV * (lp * lp - elp);
        if (ein > 1e-9f && elp > 1e-12f) {               /* hold the gain in silence */
            float t = ein * wf_rsqrt(ein * elp);        /* = sqrt(ein / elp), no divide */
            if (t < 0.002f) t = 0.002f;
            if (t > 8.0f)   t = 8.0f;
            comp += WF_LVL * (t - comp);
        }
        w = lp * comp;
        if (w > 1.5f) w = 1.0f;                          /* soft ceiling at +-1 (cubic) */
        else if (w < -1.5f) w = -1.0f;
        else w = w - 0.148148f * w * w * w;
        gate += 0.01f * ((ein > 1e-7f ? 1.0f : 0.0f) - gate);
        buf[i] = in + P->mix * (w * gate * P->level - in);
    }
    if (lp < 1e-15f && lp > -1e-15f) lp = 0.0f;          /* denormals */
    if (dcy < 1e-15f && dcy > -1e-15f) dcy = 0.0f;
    if (elp < 1e-15f) elp = 0.0f;
    if (ein < 1e-15f) ein = 0.0f;
    if (gate < 1e-15f) gate = 0.0f;
    s->g_s = g; s->b_s = b; s->lp = lp; s->dcx = dcx; s->dcy = dcy;
    s->ein = ein; s->elp = elp; s->comp = comp; s->gate = gate;
}

/* ---- on-screen text ------------------------------------------------------ */
/* knob index 1 = Symm: screen 0..100 -> "-50" .. "0" .. "+50" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    int n, t = 0, len = 0;
    if (value > 100u) value = 100u;
    n = (int)value - 50;
    if (n < 0) { out[len] = '-'; len++; n = -n; }
    else if (n > 0) { out[len] = '+'; len++; }
    while (n >= 10) { n -= 10; t++; }
    if (t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef WAVEFOLD_HOST_TEST

#include "wavefold_params.h"

#ifndef WAVEFOLD_AUDIO_FUNC
#define WAVEFOLD_AUDIO_FUNC Fx_DLY_Wavefold
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

WF_CODE_SECTION(WAVEFOLD_AUDIO_FUNC)
void WAVEFOLD_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    WfState *s;
    WfParams P;
    float k[5];
    int i;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    if (params[0] < 0.5f) return;                /* effect bypassed            */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(WfState) || span < (end - base)) return;
    if (stateBase + sizeof(WfState) > end) return;

    s = (WfState *)stateBase;

    k[0] = wf_knob(params[WAVEFOLD_DRIVE_SLOT], (float)WAVEFOLD_DRIVE_UI_DEFAULT, 0.01f);
    k[1] = wf_knob(params[WAVEFOLD_SYMM_SLOT],  (float)WAVEFOLD_SYMM_UI_DEFAULT,  0.01f);
    k[2] = wf_knob(params[WAVEFOLD_TONE_SLOT],  (float)WAVEFOLD_TONE_UI_DEFAULT,  0.01f);
    k[3] = wf_knob(params[WAVEFOLD_LEVEL_SLOT], (float)WAVEFOLD_LEVEL_UI_DEFAULT, 0.01f);
    k[4] = wf_knob(params[WAVEFOLD_MIX_SLOT],   (float)WAVEFOLD_MIX_UI_DEFAULT,   0.01f);

    wf_prepare(&P, k);
    if (s->magic != WF_MAGIC) wf_init(s, &P);
    wf_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* WAVEFOLD_HOST_TEST */
