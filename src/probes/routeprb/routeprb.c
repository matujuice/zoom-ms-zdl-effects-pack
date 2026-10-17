/*
 * RoutePrb: the bus probe for MIDISTOMP Mixer Mode (firmware zoom-ms-modding, M2).
 *
 * A hardware probe, not a release effect (build only when named: py build_all.py routeprb).
 * The mixer runs each slot on a bus (PRE, A, B, POST) taken from a mailbox in its data block:
 * one byte per slot at 0xC00F6088, written 0x80 | bus by this effect every block and cleared
 * by the mixer after it reads it, so a removed probe falls back to PRE. The slot is found
 * from ctx: the mixer passes slot state 0x11F03000 + 212 x slot.
 *
 * Knobs: Bus PRE / A / B / POST, Src OFF / SAW / NOISE, Tone, Delay, Level.
 *  - Src adds a test sound to the input at Level (SAW 110 Hz; NOISE 0.1 s burst each second).
 *  - Tone: one-pole low-pass on the whole signal (100 = open).
 *  - Delay: 0..100 ms, mixed in at half level, no feedback (0 = off).
 * P8 stays 0 (the mixer keeps its own bytes in the high bits of P8).
 *
 * Plain float math, no division, no switch, no static arrays, no libm.
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define RP_DO_PRAGMA(x) _Pragma(#x)
#define RP_EXPAND_PRAGMA(x) RP_DO_PRAGMA(x)
#define RP_ALWAYS_INLINE(fn) RP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define RP_CODE_SECTION(fn) RP_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define RP_ALWAYS_INLINE(fn)
#define RP_CODE_SECTION(fn)
#endif

#define RP_MAGIC        0x52505231u          /* "RPR1" */
#define RP_MAIL         0xC00F6088u          /* mixer.S MAIL[6]                       */
#define RP_MIXER_MAGIC  0xC00F60F7u          /* mixer.S DEFAULTS[11], 0x4D on the mixer */
#define RP_STATE0       0x11F03000u          /* slot 0 state, 212 B per slot           */
#define RP_SLOT_SIZE    212u
#define RP_DLY_N        4416u                /* ring: 100 ms at 44.1 kHz              */
#define RP_SAW_STEP     (110.0f * 2.0f / 44100.0f)
#define RP_BURST_LEN    4410u
#define RP_BURST_EVERY  44100u

typedef struct {
    unsigned int magic;
    unsigned int since;                      /* samples since the last noise burst */
    unsigned int seed;                       /* noise LCG                          */
    unsigned int w;                          /* delay write position               */
    float saw;                               /* -1..1                              */
    float lp;                                /* filter state                       */
    float ring[RP_DLY_N];
} RpState;

/* The pedal hands every knob over as (screen number) / 100, whatever its maximum. */
RP_ALWAYS_INLINE(rp_ui)
static inline unsigned int rp_ui(float raw, unsigned int def_ui, unsigned int max_ui)
{
    unsigned int ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) return def_ui;
    if (raw <= 3.05f) raw = raw * 100.0f;
    ui = (unsigned int)(int)(raw + 0.5f);
    if (ui > max_ui) ui = max_ui;
    return ui;
}

/* Tell the mixer this slot's bus. Only on the mixer firmware, only for a real slot. */
RP_ALWAYS_INLINE(rp_mail)
static inline void rp_mail(uintptr_t ctx, unsigned int bus)
{
    unsigned int slot;
    uintptr_t at = RP_STATE0;
    if (*(volatile uint8_t *)RP_MIXER_MAGIC != 0x4Du) return;
    for (slot = 0u; slot < 6u; slot++, at += RP_SLOT_SIZE) {
        if (at == ctx) {
            *(volatile uint8_t *)(RP_MAIL + slot) = (uint8_t)(0x80u | bus);
            return;
        }
    }
}

/* One 8-sample block on the left half of fx, then copied to the right. */
RP_ALWAYS_INLINE(rp_block)
static inline void rp_block(RpState *s, unsigned int src, unsigned int tone,
                            unsigned int dly, float lvl, float *fx)
{
    float t = 0.01f * (float)(int)tone;
    float a = 0.02f + 0.98f * t * t;                 /* low-pass coefficient */
    unsigned int d = dly * 44u;                      /* 0..4400 samples       */
    int j;

    for (j = 0; j < 8; j++) {
        float x = fx[j], v = 0.0f;
        if (src == 1u) {
            s->saw += RP_SAW_STEP;
            if (s->saw >= 1.0f) s->saw -= 2.0f;
            v = s->saw;
        } else if (src == 2u && s->since < RP_BURST_LEN) {
            s->seed = s->seed * 1664525u + 1013904223u;
            v = (float)(int)(s->seed >> 9) * (1.0f / 4194304.0f) - 1.0f;
        }
        s->since++;
        if (s->since >= RP_BURST_EVERY) s->since = 0u;
        x += lvl * v;
        s->lp += a * (x - s->lp);
        x = s->lp;
        s->ring[s->w] = x;
        if (d) {
            unsigned int r = (s->w >= d) ? s->w - d : s->w + RP_DLY_N - d;
            x += 0.5f * s->ring[r];
        }
        s->w++;
        if (s->w >= RP_DLY_N) s->w = 0u;
        fx[j] = x;
    }
    for (j = 0; j < 8; j++) fx[j + 8] = fx[j];       /* same signal to R */
}

/* ---- on-screen text ------------------------------------------------------ */
RP_ALWAYS_INLINE(rp_text)
static inline int rp_text(char *out, int a, int b, int c, int d, int e)
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

/* knob 0 Bus */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 3u) return rp_text(out, 'P', 'O', 'S', 'T', 0);
    if (value == 2u) return rp_text(out, 'B', 'U', 'S', ' ', 'B');
    if (value == 1u) return rp_text(out, 'B', 'U', 'S', ' ', 'A');
    return rp_text(out, 'P', 'R', 'E', 0, 0);
}

/* knob 1 Src */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value >= 2u) return rp_text(out, 'N', 'O', 'I', 'S', 'E');
    if (value == 1u) return rp_text(out, 'S', 'A', 'W', 0, 0);
    return rp_text(out, 'O', 'F', 'F', 0, 0);
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef ROUTEPRB_HOST_TEST

#include "routeprb_params.h"

#ifndef ROUTEPRB_AUDIO_FUNC
#define ROUTEPRB_AUDIO_FUNC Fx_DLY_RoutePrb
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

RP_CODE_SECTION(ROUTEPRB_AUDIO_FUNC)
void ROUTEPRB_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span, i;
    RpState *s;

    *magicDst = *magicSrc;                       /* preserve the magic shuttle */

    /* the bus holds even when bypassed or short of memory: the slot stays where it was put */
    rp_mail((uintptr_t)ctx, rp_ui(params[ROUTEPRB_BUS_SLOT], ROUTEPRB_BUS_UI_DEFAULT, 3u));

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    base = (uintptr_t)desc[0];
    end  = (uintptr_t)desc[1];
    span = desc[2];
    stateBase = (base + 3u) & ~(uintptr_t)3u;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if ((end - base) < sizeof(RpState) || span < (end - base)) return;
    if (stateBase + sizeof(RpState) > end) return;

    s = (RpState *)stateBase;
    if (params[0] < 0.5f) return;                /* effect bypassed */

    if (s->magic != RP_MAGIC) {
        s->since = 0u; s->seed = 1u; s->w = 0u; s->saw = 0.0f; s->lp = 0.0f;
        for (i = 0u; i < RP_DLY_N; i++) s->ring[i] = 0.0f;
        s->magic = RP_MAGIC;
    }
    rp_block(s,
             rp_ui(params[ROUTEPRB_SRC_SLOT],   ROUTEPRB_SRC_UI_DEFAULT,   2u),
             rp_ui(params[ROUTEPRB_TONE_SLOT],  ROUTEPRB_TONE_UI_DEFAULT,  100u),
             rp_ui(params[ROUTEPRB_DELAY_SLOT], ROUTEPRB_DELAY_UI_DEFAULT, 100u),
             0.005f * (float)(int)rp_ui(params[ROUTEPRB_LEVEL_SLOT], ROUTEPRB_LEVEL_UI_DEFAULT, 100u),
             fxBuf);
}

#endif /* ROUTEPRB_HOST_TEST */
