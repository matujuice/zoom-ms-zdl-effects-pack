/*
 * tempoprb.c - "TempoPrb": hardware probe, NOT a release effect.
 *
 * QUESTION
 *   Can a custom ZDL see the pedal's own patch tempo (the BPM that tap tempo and the
 *   patch-tempo SysEx F0 52 00 58 31 03 08 LSB MSB F7 set)? Stock delays follow it, but
 *   they never read a BPM number: docs/TEMPO-SYNC.md sections 4 and 8 trace TAPEECH3's
 *   DLY_EP3_Calc_DelayTime, which asks the host for a delay value instead:
 *
 *     sync = state[31](slot, 6)              read the SYNC knob (descriptor entry 6)
 *     if sync == 0:  time = state[31](slot, 4) + 10
 *     else:          x = state[31]((slot - 1) & 0xff, 0x0f3c)   (a second table)
 *                    y = state[24](x, 100);  delay_ms = y / 100
 *
 *   and the SYNC knob carries pedal_flags 0x28 in the descriptor. Nobody has run this
 *   chain from a custom effect yet. This probe does, and makes the results audible.
 *
 * WHAT IT READS (all plain memory loads, except the optional Call)
 *   - Its own 212-byte per-slot state block: the host's six blocks sit at
 *     0x11f03000 + slot * 0xD4 (build/ABI.md 5.3); ours is the one whose word 1
 *     (state[1], the parameter table) equals ctx[1]. state[0] is the "slot" argument
 *     TAPEECH3 passes to state[31]; state[24] is the math helper (template c00d4b40).
 *   - state[31] is only a table read (TEMPO-SYNC.md 3): word B4 of row A4 of a table of
 *     44-byte rows at 0xc009c1a0. So the probe reads that memory directly instead of
 *     calling it: sync = tab1[s0][6], x = word 0 of row (s0 - 1) & 0xff of the table
 *     at 0xc009fe90 (= 0xc009c1a0 + 4 * 0x0f3c).
 *   - Call ON also calls state[24](x, 100) exactly as TAPEECH3 does, once every 32
 *     blocks, from the audio function. This is the riskiest part: it is a firmware
 *     function called from a custom effect (only when the pointer looks like firmware
 *     code, 0xc0000000..0xc03fffff). Call OFF never calls anything.
 *
 * WHAT YOU HEAR (the input passes through untouched; the probe sound is added on top)
 *   Mode BLIP: a short beep each time the watched value(s) change. Second-table changes
 *     beep high, first-table changes beep low, single values in the middle.
 *     "No slot" (our state block not found): a two-tone warble once a second.
 *   Mode CLICK: a click every N ms, N taken from the watched value as a delay time
 *     (RCPE: y / 100 like TAPEECH3; the others: the value itself as ms). A value
 *     that is not a plausible delay (under 20 ms or over 6 s) gives a low hum instead.
 *   Mode DUMP: a data tone you record and decode (decode_dump.py): every 7.4 s,
 *     0.3 s silence, 0.2 s 689 Hz start tone, then 32 words x 32 bits, MSB first,
 *     256 samples per bit, 1378 Hz = 0, 2756 Hz = 1, then 1 s silence. Frame:
 *       0 'TPR1'  1 slot (0..5, or 0xFFFFFFFF = not found)  2 state[0]
 *       3 ctx[1]  4 state[24] pointer  5 Sync knob raw bits  6 Time knob raw bits
 *       7 x  8 y (0xFFFFFFFF = not called)  9..19 second-table row (s0 - 1) & 0xff
 *       20..30 first-table row s0  31 blocks since load
 *
 *   Watch picks what BLIP and CLICK follow:
 *     RCPE  y, the TAPEECH3 result (needs Call ON; with Call OFF it follows x)
 *     X     x, the second-table word TAPEECH3 hands to state[24]
 *     SYNC  our Sync knob as the host's table holds it: turning Sync must beep, which
 *           proves the probe found its own row (a calibration, not a tempo test)
 *     ROW   all 11 words of our second-table row (CLICK uses word 0 = x)
 *     TABLE rows 0..15 of both tables (CLICK uses x)
 *
 * KNOBS (manifest order; Time sits at descriptor entry 4 and Sync at entry 6, the
 * positions TAPEECH3's B4 = 4 and B4 = 6 read)
 *   0 Mode  0..2   BLIP / CLICK / DUMP
 *   1 Watch 0..4   RCPE / X / SYNC / ROW / TABLE
 *   2 Time  0..990 dummy free time (TAPEECH3 shape); only shows up in the dump
 *   3 Level 0..100 probe sound level
 *   4 Sync  0..15  OFF, S1..S15, descriptor pedal_flags 0x28 like TAPEECH3's SYNC
 *   5 Call  0..1   OFF / ON: call state[24] (see above)
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float division,
 * no integer division or modulo, no libm, no switch, every helper forced inline. The
 * only call is the optional state[24] one, through a pointer read at run time from the
 * host's own state block (a real firmware address, not a link-time one).
 */

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define TP_DO_PRAGMA(x) _Pragma(#x)
#define TP_EXPAND_PRAGMA(x) TP_DO_PRAGMA(x)
#define TP_ALWAYS_INLINE(fn) TP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define TP_CODE_SECTION(fn) TP_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define TP_ALWAYS_INLINE(fn)
#define TP_CODE_SECTION(fn)
#endif

#define TP_MAGIC        0x54505231u          /* "TPR1"; also word 0 of a dump frame */
#define TP_NONE         0xFFFFFFFFu
#define TP_STATE_BASE   0x11f03000u          /* host per-slot state blocks           */
#define TP_STATE_STRIDE 0xD4u
#define TP_SLOTS        6u
#define TP_TAB1         0xc009c1a0u          /* state[31] table                      */
#define TP_TAB2         0xc009fe90u          /* = TP_TAB1 + 4 * 0x0f3c               */
#define TP_ROW          44u                  /* bytes per row, 11 words              */
#define TP_FN_LO        0xc0000000u          /* plausible firmware code for state[24] */
#define TP_FN_HI        0xc0400000u

/* Watched words, by index into prev[]: rows are 16 words apart so row = i >> 4,
 * word = i & 15 (words 11..15 unused). */
#define TP_W_TAB2       0u                   /* 0..255: second table rows 0..15     */
#define TP_W_TAB1       256u                 /* 256..511: first table rows 0..15    */
#define TP_W_X          512u
#define TP_W_ROW        513u                 /* 513..523: our second-table row      */
#define TP_W_SYNC       524u
#define TP_W_Y          525u
#define TP_NWATCH       526u
#define TP_SCAN_PER_BLK 32u

/* tone periods are powers of two: 1 << sh samples */
#define TP_SH_HI        4u                   /* 2756 Hz */
#define TP_SH_MID       5u                   /* 1378 Hz */
#define TP_SH_LO        6u                   /*  689 Hz */
#define TP_SH_HUM       9u                   /*   86 Hz */

#define TP_BLIP_LEN     1764u                /* 40 ms */
#define TP_CLICK_LEN    220u                 /*  5 ms */
#define TP_WARBLE_EVERY 44100u

/* dump frame timing, samples */
#define TP_D_SIL1       13230u
#define TP_D_PRE        8820u
#define TP_D_BIT        256u
#define TP_D_WORDS      32u
#define TP_D_BITS0      (TP_D_SIL1 + TP_D_PRE)
#define TP_D_BITSN      (TP_D_BITS0 + TP_D_WORDS * 32u * TP_D_BIT)
#define TP_D_LEN        (TP_D_BITSN + 44100u)

#ifndef TP_MEM
#define TP_MEM(a) (*(volatile unsigned int *)(uintptr_t)(a))
#endif
typedef unsigned int (*TpFn24)(unsigned int, unsigned int);
#ifndef TP_CALL24
#define TP_CALL24(fn, a, b) (((TpFn24)(uintptr_t)(fn))((a), (b)))
#endif

typedef struct {
    unsigned int magic;
    unsigned int blocks;                 /* blocks since load (wraps)               */
    unsigned int slot, s0, fn24;         /* our slot, state[0], state[24]           */
    unsigned int x, y, sync;
    unsigned int watch;                  /* Watch the prev[] values were primed for */
    unsigned int scan, primed;           /* next index to compare; one pass done    */
    unsigned int ph;                     /* tone phase counter                      */
    unsigned int blip, blipSh;           /* samples of beep left, its pitch         */
    unsigned int since;                  /* samples since the last click / warble   */
    unsigned int dpos;                   /* position in the dump frame              */
    unsigned int frame[32];              /* dump snapshot                           */
    unsigned int prev[526];              /* TP_NWATCH last seen values              */
} TpState;

typedef struct {
    unsigned int mode, watch, call;
    float amp;
} TpParams;

typedef union { unsigned int u; float f; } TpBits;

/* The pedal hands every knob over as (screen number) / 100, whatever its maximum. */
TP_ALWAYS_INLINE(tp_ui)
static inline unsigned int tp_ui(float raw, unsigned int def_ui, unsigned int max_ui)
{
    unsigned int ui;
    if (!(raw >= 0.0f && raw <= 300.0f)) return def_ui;
    if (raw <= 3.05f) raw = raw * 100.0f;
    ui = (unsigned int)(int)(raw + 0.5f);
    if (ui > max_ui) ui = max_ui;
    return ui;
}

TP_ALWAYS_INLINE(tp_bits)
static inline unsigned int tp_bits(float f)
{
    TpBits b;
    b.f = f;
    return b.u;
}

/* a word as a number: small integers as they are, floats 1.0 .. 2^23 as floats,
 * anything else -1 (not a number we can use) */
TP_ALWAYS_INLINE(tp_num)
static inline float tp_num(unsigned int w)
{
    TpBits b;
    if (w < 0x00800000u) return (float)(int)w;
    if (w >= 0x3F800000u && w <= 0x4B000000u) { b.u = w; return b.f; }
    return -1.0f;
}

/* triangle, period 1 << sh samples, -1..1 */
TP_ALWAYS_INLINE(tp_tri)
static inline float tp_tri(unsigned int ph, unsigned int sh)
{
    TpBits inv;
    unsigned int p = ph & ((1u << sh) - 1u);
    float v;
    inv.u = (127u + 2u - sh) << 23;          /* 4 / 2^sh, built from the exponent */
    v = (float)(int)p * inv.f;               /* 0..4 */
    if (v > 2.0f) v = 4.0f - v;              /* 0..2..0 */
    return v - 1.0f;
}

/* read watched word i straight from the host's memory (or from our own copies) */
TP_ALWAYS_INLINE(tp_read)
static inline unsigned int tp_read(const TpState *s, unsigned int i, unsigned int r2)
{
    if (i < TP_W_TAB1) return TP_MEM(TP_TAB2 + TP_ROW * (i >> 4) + 4u * (i & 15u));
    if (i < TP_W_X)    return TP_MEM(TP_TAB1 + TP_ROW * ((i - TP_W_TAB1) >> 4) + 4u * (i & 15u));
    if (i == TP_W_X)   return s->x;
    if (i < TP_W_SYNC) return TP_MEM(TP_TAB2 + TP_ROW * r2 + 4u * (i - TP_W_ROW));
    if (i == TP_W_SYNC) return s->sync;
    return s->y;
}

TP_ALWAYS_INLINE(tp_init)
static inline void tp_init(TpState *s)
{
    s->blocks = 0u; s->slot = TP_NONE; s->s0 = 0u; s->fn24 = 0u;
    s->x = 0u; s->y = TP_NONE; s->sync = 0u;
    s->watch = TP_NONE; s->scan = 0u; s->primed = 0u;
    s->ph = 0u; s->blip = 0u; s->blipSh = TP_SH_MID; s->since = 0u; s->dpos = 0u;
    s->magic = TP_MAGIC;
}

/* Find our state block, read the TAPEECH3 values, compare the watched ones. Once per
 * block, before the sample loop. */
TP_ALWAYS_INLINE(tp_prepare)
static inline void tp_prepare(TpState *s, const TpParams *P, unsigned int params_ptr)
{
    unsigned int k, lo, hi, n, r1, r2, i, w;
    uintptr_t blk;

    if ((s->blocks & 511u) == 0u) {              /* look for our slot now and then */
        s->slot = TP_NONE;
        for (k = 0u; k < TP_SLOTS; k++)
            if (TP_MEM(TP_STATE_BASE + TP_STATE_STRIDE * k + 4u) == params_ptr) s->slot = k;
    }
    blk = TP_STATE_BASE + TP_STATE_STRIDE * (s->slot == TP_NONE ? 0u : s->slot);
    s->s0   = TP_MEM(blk);
    s->fn24 = TP_MEM(blk + 24u * 4u);

    r1 = s->s0;                                  /* TAPEECH3: state[31](state[0], B4) */
    if (r1 > 255u) r1 = (s->slot == TP_NONE) ? 0u : s->slot;   /* not a row: stay sane */
    r2 = (s->s0 - 1u) & 0xffu;                   /* TAPEECH3: state[31]((s0-1)&0xff, 0xf3c) */
    s->sync = TP_MEM(TP_TAB1 + TP_ROW * r1 + 6u * 4u);
    s->x    = TP_MEM(TP_TAB2 + TP_ROW * r2);
    if (!P->call) s->y = TP_NONE;
    else if ((s->blocks & 31u) == 0u && s->fn24 >= TP_FN_LO && s->fn24 < TP_FN_HI
             && (s->fn24 & 1u) == 0u)
        s->y = TP_CALL24(s->fn24, s->x, 100u);

    /* range of watched words */
    lo = TP_W_TAB2; hi = TP_W_X;                                 /* TABLE */
    if (P->watch == 0u) { lo = P->call ? TP_W_Y : TP_W_X; hi = lo + 1u; }   /* RCPE */
    if (P->watch == 1u) { lo = TP_W_X; hi = TP_W_X + 1u; }
    if (P->watch == 2u) { lo = TP_W_SYNC; hi = TP_W_SYNC + 1u; }
    if (P->watch == 3u) { lo = TP_W_ROW; hi = TP_W_SYNC; }
    k = P->watch + (P->call ? 8u : 0u);
    if (s->watch != k) { s->watch = k; s->scan = lo; s->primed = 0u; }
    if (s->scan < lo || s->scan >= hi) s->scan = lo;

    for (n = 0u; n < TP_SCAN_PER_BLK; n++) {
        i = s->scan;
        if (i >= TP_W_X || (i & 15u) < 11u) {       /* table rows use words 0..10 */
            w = tp_read(s, i, r2);
            if (w != s->prev[i]) {
                if (s->primed) {
                    s->blip = TP_BLIP_LEN;
                    s->blipSh = (i < TP_W_TAB1) ? TP_SH_HI : (i < TP_W_X ? TP_SH_LO : TP_SH_MID);
                }
                s->prev[i] = w;
            }
        }
        i++;
        if (i >= hi) { i = lo; s->primed = 1u; }
        s->scan = i;
    }
    s->blocks++;
}

/* the watched value as a delay in ms (CLICK), or -1 */
TP_ALWAYS_INLINE(tp_click_ms)
static inline float tp_click_ms(const TpState *s, const TpParams *P)
{
    if (P->watch == 0u && P->call) return tp_num(s->y) * 0.01f;
    if (P->watch == 2u) return tp_num(s->sync);
    return tp_num(s->x);
}

TP_ALWAYS_INLINE(tp_snapshot)
static inline void tp_snapshot(TpState *s, unsigned int params_ptr, unsigned int sync_raw,
                               unsigned int time_raw)
{
    unsigned int k, r1, r2;
    r1 = s->s0;
    if (r1 > 255u) r1 = (s->slot == TP_NONE) ? 0u : s->slot;
    r2 = (s->s0 - 1u) & 0xffu;
    s->frame[0] = TP_MAGIC;   s->frame[1] = s->slot;  s->frame[2] = s->s0;
    s->frame[3] = params_ptr; s->frame[4] = s->fn24;  s->frame[5] = sync_raw;
    s->frame[6] = time_raw;   s->frame[7] = s->x;     s->frame[8] = s->y;
    for (k = 0u; k < 11u; k++) {
        s->frame[9 + k]  = TP_MEM(TP_TAB2 + TP_ROW * r2 + 4u * k);
        s->frame[20 + k] = TP_MEM(TP_TAB1 + TP_ROW * r1 + 4u * k);
    }
    s->frame[31] = s->blocks;
}

/* one sample of the dump tone at frame position d */
TP_ALWAYS_INLINE(tp_dump_sample)
static inline float tp_dump_sample(const TpState *s, unsigned int d)
{
    unsigned int b, wd, bit;
    if (d < TP_D_SIL1) return 0.0f;
    if (d < TP_D_BITS0) return tp_tri(d - TP_D_SIL1, TP_SH_LO);
    if (d >= TP_D_BITSN) return 0.0f;
    d -= TP_D_BITS0;
    b = d >> 8;                                  /* bit number, 256 samples each */
    wd = b >> 5;
    bit = (s->frame[wd] >> (31u - (b & 31u))) & 1u;
    return tp_tri(d & 255u, bit ? TP_SH_HI : TP_SH_MID);
}

TP_ALWAYS_INLINE(tp_process)
static inline void tp_process(TpState *s, const TpParams *P, float *buf, int n,
                              unsigned int params_ptr, unsigned int sync_raw,
                              unsigned int time_raw)
{
    int i;
    float ms = -1.0f, period = 0.0f, v;
    unsigned int per = 0u;
    if (P->mode == 1u) {
        ms = tp_click_ms(s, P);
        if (ms >= 20.0f && ms <= 6000.0f) { period = ms * 44.1f; per = (unsigned int)(int)period; }
    }
    for (i = 0; i < n; i++) {
        v = 0.0f;
        if (P->mode == 2u) {                                     /* DUMP */
            if (s->dpos == 0u) tp_snapshot(s, params_ptr, sync_raw, time_raw);
            v = tp_dump_sample(s, s->dpos);
            s->dpos++;
            if (s->dpos >= TP_D_LEN) s->dpos = 0u;
        } else if (P->mode == 1u) {                              /* CLICK */
            if (per) {
                if (s->since >= per) s->since = 0u;
                if (s->since < TP_CLICK_LEN) v = tp_tri(s->since, TP_SH_HI);
            } else v = 0.3f * tp_tri(s->ph, TP_SH_HUM);
            s->since++;
        } else {                                                 /* BLIP */
            if (s->blip) { v = tp_tri(s->ph, s->blipSh); s->blip--; }
            if (s->slot == TP_NONE) {                            /* warble: no slot */
                if (s->since < 4410u) v = tp_tri(s->ph, TP_SH_MID);
                else if (s->since < 8820u) v = tp_tri(s->ph, TP_SH_LO);
                s->since++;
                if (s->since >= TP_WARBLE_EVERY) s->since = 0u;
            }
        }
        if (P->mode != 2u) s->dpos = 0u;                         /* dumps start fresh */
        s->ph++;
        buf[i] += P->amp * v;
    }
}

/* ---- on-screen text ------------------------------------------------------ */
TP_ALWAYS_INLINE(tp_text)
static inline int tp_text(char *out, int a, int b, int c, int d, int e)
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

/* knob 0 Mode: BLIP / CLICK / DUMP */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 2u) return tp_text(out, 'D', 'U', 'M', 'P', 0);
    if (value == 1u) return tp_text(out, 'C', 'L', 'I', 'C', 'K');
    return tp_text(out, 'B', 'L', 'I', 'P', 0);
}

/* knob 1 Watch: RCPE / X / SYNC / ROW / TABLE */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value >= 4u) return tp_text(out, 'T', 'A', 'B', 'L', 'E');
    if (value == 3u) return tp_text(out, 'R', 'O', 'W', 0, 0);
    if (value == 2u) return tp_text(out, 'S', 'Y', 'N', 'C', 0);
    if (value == 1u) return tp_text(out, 'X', 0, 0, 0, 0);
    return tp_text(out, 'R', 'C', 'P', 'E', 0);
}

/* knob 4 Sync: 0 "OFF", 1..15 "S1".."S15" (TAPEECH3's division index, unlabelled here) */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    if (value > 15u) value = 15u;
    if (value == 0u) return tp_text(out, 'O', 'F', 'F', 0, 0);
    if (value >= 10u) return tp_text(out, 'S', '1', '0' + (int)value - 10, 0, 0);
    return tp_text(out, 'S', '0' + (int)value, 0, 0, 0);
}

/* knob 5 Call: OFF / ON */
int ZDL_GetLabel_5(unsigned int value, char *out)
{
    if (value) return tp_text(out, 'O', 'N', 0, 0, 0);
    return tp_text(out, 'O', 'F', 'F', 0, 0);
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef TEMPOPRB_HOST_TEST

#include "tempoprb_params.h"

#ifndef TEMPOPRB_AUDIO_FUNC
#define TEMPOPRB_AUDIO_FUNC Fx_DLY_TempoPrb
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

TP_CODE_SECTION(TEMPOPRB_AUDIO_FUNC)
void TEMPOPRB_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *,
                                     *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile unsigned int *desc;
    uintptr_t base, end, stateBase;
    unsigned int span;
    TpState *s;
    TpParams P;
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
    if ((end - base) < sizeof(TpState) || span < (end - base)) return;
    if (stateBase + sizeof(TpState) > end) return;

    s = (TpState *)stateBase;
    if (params[0] < 0.5f) return;                /* effect bypassed */

    P.mode  = tp_ui(params[TEMPOPRB_MODE_SLOT],  TEMPOPRB_MODE_UI_DEFAULT,  2u);
    P.watch = tp_ui(params[TEMPOPRB_WATCH_SLOT], TEMPOPRB_WATCH_UI_DEFAULT, 4u);
    P.call  = tp_ui(params[TEMPOPRB_CALL_SLOT],  TEMPOPRB_CALL_UI_DEFAULT,  1u);
    P.amp   = 0.005f * (float)(int)tp_ui(params[TEMPOPRB_LEVEL_SLOT], TEMPOPRB_LEVEL_UI_DEFAULT, 100u);

    if (s->magic != TP_MAGIC) tp_init(s);
    tp_prepare(s, &P, ctx[1]);
    tp_process(s, &P, fxBuf, 8, ctx[1], tp_bits(params[TEMPOPRB_SYNC_SLOT]),
               tp_bits(params[TEMPOPRB_TIME_SLOT]));

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R */
}

#endif /* TEMPOPRB_HOST_TEST */
