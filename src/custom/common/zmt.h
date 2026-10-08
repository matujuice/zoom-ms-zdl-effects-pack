/*
 * zmt.h - the MOD firmware's MIDI transport block, read by every tempo effect of the pack.
 *
 * The MOD firmware (repo zoom-ms-modding, 0.4 and later, MS-50G 3.10 OS; spec in its
 * docs/transport-block.md) keeps 8 words at 0x1181FF00 in L2 RAM:
 *   0 magic 0x5A4D5401   1 sequence (+1 before and after each update, odd = mid-write)
 *   2 running (1 after MIDI Start, 0 after Stop or 0.5 s without clock)
 *   3 Start counter      4 MIDI clocks since Start (24 per beat; the first clock is 1 = the downbeat)
 *   5 tempo, BPM x 100: the clock's tempo while it runs, else the pedal's TEMPO (tap, setting, patch)
 * The effects take their tempo only from here (no Tempo knob; Luca, 2026-10-08). While the
 * transport runs they place themselves on the clock count, so MIDI Start restarts them on the
 * downbeat, an effect loaded mid-song lands in the right place and nothing drifts. After Stop
 * they run on at the last tempo. Without the block (stock firmware) they run at 120 BPM.
 *
 * Usage, once per block before anything reads the tempo:
 *   float beats; int run, start;
 *   bpm = zt_update(&s->zt, &run, &beats, &start);
 *   if (run) { ...place the effect at beats... }
 * bpm = the block's tempo, held until it moves by 0.3 BPM or more (the clock's tempo moves in
 * 0.1 BPM steps; a tempo-synced delay time should not move with that), 120 without the block.
 * run = 1 while the transport runs. beats = beats since Start, wrapped at 96 (24 bars of 4/4, a
 * whole number of every note value the pack uses: 2304 clocks = 1536 x 1/64), between clocks
 * moved on at the tempo but never past the next clock. start = 1 on the first block that sees
 * a new Start (restart what restarts on the downbeat). s->zt.clk = clocks since Start (EuGate).
 * Pedal rules (docs/SAFE-DSP-RULES.md): plain loads, no division, inline.
 */
#ifndef ZMT_H
#define ZMT_H

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define ZT_DO_PRAGMA(x) _Pragma(#x)
#define ZT_EXPAND_PRAGMA(x) ZT_DO_PRAGMA(x)
#define ZT_ALWAYS_INLINE(fn) ZT_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#else
#define ZT_ALWAYS_INLINE(fn)
#endif

#ifndef ZT_ADDR
#ifdef __TI_COMPILER_VERSION__
#define ZT_ADDR       0x1181FF00u
#else
static unsigned int zt_host[8];          /* host tests: no firmware; tests fill it in */
#define ZT_ADDR       ((uintptr_t)zt_host)
#endif
#endif
#define ZT_MAGIC      0x5A4D5401u
#define ZT_WRAP       2304u              /* clocks: 96 beats                         */
#define ZT_CLK_BLOCK  7.2562358e-5f      /* clocks per 8-sample block per BPM: 8 * 24 / (60 * 44100) */
#define ZT_BEAT       0.041666668f       /* 1 / 24                                   */

/* Per-effect state (in the effect's arena state) */
typedef struct {
    unsigned int clk, starts;  /* clock count and Start counter seen last block */
    float sub;                 /* share of the next clock already played, 0..1   */
    float bpm;                 /* tempo in use (held, see the top); 0 = none yet */
} ZtSync;

ZT_ALWAYS_INLINE(zt_init)
static inline void zt_init(ZtSync *z)
{
    z->clk = 0u; z->starts = 0u; z->sub = 0.0f; z->bpm = 0.0f;
}

/* Once per block (also while the effect is switched off). Reads the block (retried while
 * the sequence word is odd or changes: a guard only, the firmware writes it with interrupts
 * off) and returns the tempo; see the top for run, beats and start. */
ZT_ALWAYS_INLINE(zt_update)
static inline float zt_update(ZtSync *z, int *run, float *beats, int *start)
{
    volatile const unsigned int *w = (volatile const unsigned int *)ZT_ADDR;
    unsigned int t, q, ok = 0u, rn = 0u, starts = 0u, clocks = 0u, r, mk, k;
    float bpm = 120.0f, d;
    if (w[0] == ZT_MAGIC) {
        for (t = 0u; t < 4u && !ok; t++) {
            q = w[1];
            if (q & 1u) continue;
            rn = w[2]; starts = w[3]; clocks = w[4];
            bpm = (float)(int)w[5] * 0.01f;
            if (w[1] == q) ok = 1u;
        }
    }
    if (!ok) { rn = 0u; bpm = 120.0f; }
    if (!(bpm >= 30.0f)) bpm = 30.0f;            /* also catches NaN */
    if (bpm > 300.0f) bpm = 300.0f;
    d = bpm - z->bpm;
    if (d >= 0.3f || d <= -0.3f) z->bpm = bpm;   /* hold small clock wobble */
    *start = (ok && starts != z->starts) ? 1 : 0;
    if (ok) z->starts = starts;
    *beats = 0.0f;
    *run = (ok && rn) ? 1 : 0;
    if (!*run) { z->clk = clocks; return z->bpm; }
    if (clocks == 0u) { z->clk = 0u; z->sub = 0.0f; return z->bpm; }   /* Start, waiting for clock 1 */
    if (clocks != z->clk || *start) z->sub = 0.0f;
    else {
        z->sub += z->bpm * ZT_CLK_BLOCK;
        if (z->sub > 0.999f) z->sub = 0.999f;    /* never past the next clock */
    }
    z->clk = clocks;
    r = clocks - 1u;
    for (k = 20u; k-- > 0u; ) {                  /* r mod 2304 by shifted subtraction */
        mk = ZT_WRAP << k;
        while (r >= mk) r -= mk;                 /* at most 3 times at the top, once below */
    }
    *beats = ((float)(int)r + z->sub) * ZT_BEAT;
    return z->bpm;
}

#endif /* ZMT_H */
