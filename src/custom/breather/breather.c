/*
 * breather.c - "Breather": tempo-synced pump with a small built-in reverb, mono
 *
 * For synth pads, basses, percussion and drum machines (tekno at ~160 BPM). On every
 * Div (a 16th up to a bar) the pump moves a level: your sound, the reverb, both, or
 * what goes into the reverb. Two knobs pick it:
 *
 *   Targt (what moves)
 *     DRY   your sound; the reverb (if Verb is up) stays steady
 *     VERB  only the reverb output; your sound passes untouched
 *     BOTH  your sound and the reverb output together
 *     SEND  what goes into the reverb; your sound is untouched and the tail rings freely
 *           (SEND + GATE = the hits stay dry, only the gaps feed the reverb)
 *
 *   Shape (how it moves, once per Div, starting on the downbeat)
 *     DUCK  drops on the beat by Depth, recovers over Curve (classic sidechain pump)
 *     GATE  cuts by Depth on the beat, stays cut for Curve, then opens (a hard duck)
 *     RISE  quiet after the beat, grows over Curve into the next beat, drops on it
 *
 * Depth is how far the level moves (0 = nothing, 100 = to silence). Curve is the length
 * of the move as a share of the Div (5 %..100 %). Shift moves the whole pump later by up
 * to one beat (0 = on the beat, 25 = +1/16, 50 = +1/8, the offbeat, 100 = +1/4). Verb is
 * the reverb level (0 = no reverb: a pure pump), Size the reverb length (bigger is also
 * darker). There is no Mix knob: Depth and Verb already set the amounts.
 *
 * Pedal-safe rules (docs/SAFE-DSP-RULES.md): no static/const arrays, no float division,
 * no libm, no switch or long if/else chains in the audio code (Targt and Shape become
 * 0/1 weights once per block), every helper forced inline, no calls.
 *
 * CLOCK (no division)
 *   bp counts beats into the bar, 0..4, advancing by BPM / (44100 * 60) per sample.
 *   The pump position inside the Div is p = frac((bp - shift + 4) / div), where 1/div is
 *   a power of two built with shifts and 4 beats is a whole number of every Div, so the
 *   wrap is exact. 1/Curve-length comes from a Newton reciprocal once per block.
 *
 * LEVELS
 *   The pump gives an envelope e (1 = untouched). Each of the three gains (dry, reverb
 *   out, reverb in) follows 1 + m * (e - 1), m = 1 when Targt names it, through a ~2 ms
 *   one-pole slew, so no edge clicks.
 *
 * REVERB
 *   Mono Freeverb-style: 4 damped combs (1557, 1617, 1491, 1422 samples) into 2 allpasses
 *   (556, 441). Lengths are literals and offsets into one array in the state (no const
 *   arrays), 7084 floats = 28 KB of the arena. Size sets the comb feedback 0.70..0.97 and
 *   the damping 0.15..0.55; the input gain follows (1 - feedback), so the reverb
 *   level (about 2x the input, rms, at Verb 100) does not change with Size. The array is cleared 1024 floats per block after load and
 *   after the effect is switched on again (the reverb is silent for those 56 samples),
 *   so an old tail never comes back.
 *
 * SYNC (iPhone / Mozaic bar sync, docs/IPHONE-SYNC.md, docs/TEMPO-SYNC.md)
 *   The Tempo knob runs 0..441 and holds every BPM twice (0..240 = the BPM, 241..441 = a
 *   twin copy, BPM = screen - 201, so past 240 the screen shows 40 again). Flipping
 *   between a BPM and its twin (160 <-> 361) restarts the pump on beat 1 without
 *   changing the tempo; the Mozaic script sends that on each downbeat. A plain tempo
 *   change does not restart it. Switched off, the input passes untouched but the clock
 *   keeps running and follows flips, so the pump comes back on the bar. Switching the
 *   effect on restarts it on beat 1 only when no twin flip has arrived for ~8 s (no host
 *   running), so you can stomp on the one by hand without knocking a synced pump off.
 *
 * KNOBS (screen values)
 *   0 Targt 0..3    shown DRY / VERB / BOTH / SEND
 *   1 Shape 0..2    shown DUCK / GATE / RISE
 *   2 Depth 0..100  how far the level moves
 *   3 Div   0..4    shown 1/16 / 1/8 / 1/4 / 1/2 / BAR: how often the pump fires
 *   4 Shift 0..100  later by % of a beat; shown +1/16 at 25, +1/8 at 50, +3/16 at 75,
 *                   +1/4 at 100, the number elsewhere
 *   5 Curve 0..100  length of the move, 5 %..100 % of the Div
 *   6 Verb  0..100  reverb level
 *   7 Tempo 0..441  BPM 40..240 (0..39 = FOLLOW, see BAR TAG); 241..441 = the twin copy
 *   8 Size  0..100  reverb length, short room to long wash (bigger = darker)
 *
 * TESTED: host tests only (tests/breather_*.c). Not heard on the pedal, CPU never measured.
 *
 * BAR TAG (src/custom/common/drytag.h, docs/TEMPO-SYNC.md "Bar tag")
 *   Mozaic can only edit slots 1-3. While Mozaic flips this effect's Tempo, it writes a bar
 *   tag into the Dry buffer's right half for the slots after it. With Tempo on 0..39 = FOLLOW
 *   (shown FOLLW) it follows a tag from earlier slots: their BPM (120 when there is none),
 *   and each new bar restarts it as a twin flip of its own knob would. On any BPM it ignores
 *   the tag and runs on its own. In slots 1-3, FOLLOW needs that slot's Mozaic pad OFF.
 */

#include <stdint.h>
#include "../common/drytag.h"

#ifdef __TI_COMPILER_VERSION__
#define PU_DO_PRAGMA(x) _Pragma(#x)
#define PU_EXPAND_PRAGMA(x) PU_DO_PRAGMA(x)
#define PU_ALWAYS_INLINE(fn) PU_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#define PU_CODE_SECTION(fn) PU_EXPAND_PRAGMA(CODE_SECTION(fn, ".audio"))
#else
#define PU_ALWAYS_INLINE(fn)
#define PU_CODE_SECTION(fn)
#endif

#define PU_MAGIC        0x50553032u          /* "PU02" */
#define PU_BPM_MIN      40.0f
#define PU_BPM_MAX      240.0f
#define PU_TEMPO_MAX    441.0f               /* Tempo screen 0..441: the BPMs twice */
#define PU_TEMPO_TWIN   201.0f               /* twin copy = BPM + 201           */
#define PU_INC_PER_BPM  3.7793e-7f           /* 1 / (44100 * 60)                */
#define PU_BAR          4.0f                 /* beats per bar                    */
#define PU_SLEW         0.0113f              /* gain slew per sample, ~2 ms      */
#define PU_HOST_BLOCKS  44100u               /* ~8 s of 8-sample blocks          */
#define PU_REV_IN       0.932f               /* reverb input gain x (1 - feedback): Verb 100
                                             * gives ~2x the input level (rms), any Size */
#define PU_REV_OUT      0.6f                 /* reverb output gain at Verb 100   */
#define PU_AP_G         0.5f                 /* allpass coefficient              */
#define PU_CLEAR_CHUNK  1024

/* reverb lines inside one array: 4 combs then 2 allpasses */
#define PU_C0_LEN 1557
#define PU_C1_LEN 1617
#define PU_C2_LEN 1491
#define PU_C3_LEN 1422
#define PU_A0_LEN 556
#define PU_A1_LEN 441
#define PU_C0 0
#define PU_C1 (PU_C0 + PU_C0_LEN)
#define PU_C2 (PU_C1 + PU_C1_LEN)
#define PU_C3 (PU_C2 + PU_C2_LEN)
#define PU_A0 (PU_C3 + PU_C3_LEN)
#define PU_A1 (PU_A0 + PU_A0_LEN)
#define PU_REV_LEN (PU_A1 + PU_A1_LEN)       /* 7084 floats */

typedef struct {
    unsigned int magic;
    float bp;              /* beats into the bar, 0..4                       */
    float gD, gT, gS;      /* smoothed gains: dry, reverb out, reverb in     */
    float f0, f1, f2, f3;  /* comb damping filter states                     */
    int   i0, i1, i2, i3;  /* comb read/write positions                      */
    int   j0, j1;          /* allpass positions                              */
    int   clear_pos;       /* lazy clear progress, PU_REV_LEN when done      */
    unsigned int since_flip; /* blocks since the last twin flip (saturates)  */
    unsigned int was_off;  /* set while the footswitch is off                */
    int   twin;            /* Tempo on its twin copy (1) or not (0); -1 = not read yet */
    DtSync sync;           /* bar tag to and from other slots (drytag.h)     */
    float rev[PU_REV_LEN]; /* reverb delay lines                             */
} PuState;

typedef struct {
    float inc;             /* beats per sample                               */
    float invDiv;          /* Divs per beat                                  */
    float shift;           /* beats, 0..1                                    */
    float L, invL;         /* move length as a share of the Div, and 1/L     */
    float depth, flo;      /* depth and the level it moves to (1 - depth)    */
    float wDuck, wGate, wRise;   /* Shape as 0/1 weights                     */
    float mD, mT, mS;      /* Targt as 0/1 weights: dry, reverb out, reverb in */
    float fb, damp, damp1; /* comb feedback, damping, 1 - damping            */
    float revIn;           /* reverb input gain, follows Size                */
    float verbG;           /* reverb output gain                             */
} PuParams;

/* The pedal hands every knob over as (screen number) / 100, whatever the knob's
 * maximum. Convert back to the screen integer. */
PU_ALWAYS_INLINE(pu_ui)
static inline float pu_ui(float raw, float def_ui, float max_ui)
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

/* Tempo knob (screen 0..441): the pedal passes up to 4.41, so read raw x 100 up to 4.415
 * (pu_ui's 3.05 guess would take 4.41 for an on-screen 4). Returns the screen number. */
PU_ALWAYS_INLINE(pu_tempo_ui)
static inline float pu_tempo_ui(float raw, float def_ui)
{
    float ui;
    if (!(raw >= 0.0f && raw <= 441.5f)) ui = def_ui;
    else if (raw <= 4.415f) ui = raw * 100.0f;
    else ui = raw;
    ui = (float)(int)(ui + 0.5f);
    if (ui > PU_TEMPO_MAX) ui = PU_TEMPO_MAX;
    return ui;
}

/* Tempo screen number -> BPM: 0..240 as is (at least 40), 241..441 the twin copy. */
PU_ALWAYS_INLINE(pu_tempo_bpm)
static inline float pu_tempo_bpm(float ui)
{
    if (ui > PU_BPM_MAX) ui -= PU_TEMPO_TWIN;
    if (ui < PU_BPM_MIN) ui = PU_BPM_MIN;
    if (ui > PU_BPM_MAX) ui = PU_BPM_MAX;
    return ui;
}

/* Sync reset: 1 when Tempo moved over to the other copy since the last block (the
 * first block after loading only takes note of the copy). */
PU_ALWAYS_INLINE(pu_twin_flip)
static inline int pu_twin_flip(PuState *s, float tempo_ui)
{
    int tw = (tempo_ui > PU_BPM_MAX) ? 1 : 0;
    int flip = (s->twin >= 0 && tw != s->twin);
    s->twin = tw;
    return flip;
}

/* 1/x for x in 0.05..1 without a division: bit-trick guess and three Newton steps. */
PU_ALWAYS_INLINE(pu_rcp)
static inline float pu_rcp(float x)
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

PU_ALWAYS_INLINE(pu_init)
static inline void pu_init(PuState *s)
{
    s->bp = 0.0f; s->gD = 1.0f; s->gT = 1.0f; s->gS = 1.0f;
    s->f0 = 0.0f; s->f1 = 0.0f; s->f2 = 0.0f; s->f3 = 0.0f;
    s->i0 = 0; s->i1 = 0; s->i2 = 0; s->i3 = 0; s->j0 = 0; s->j1 = 0;
    s->clear_pos = 0;
    s->since_flip = PU_HOST_BLOCKS;          /* no host seen yet */
    s->was_off = 0u; s->twin = -1;
    dt_sync_init(&s->sync);
    s->magic = PU_MAGIC;
}

/* Clear up to one chunk of the reverb lines (after load or switching on). */
PU_ALWAYS_INLINE(pu_clear_step)
static inline void pu_clear_step(PuState *s)
{
    int i, end;
    if (s->clear_pos >= PU_REV_LEN) return;
    end = s->clear_pos + PU_CLEAR_CHUNK;
    if (end > PU_REV_LEN) end = PU_REV_LEN;
    for (i = s->clear_pos; i < end; i++) s->rev[i] = 0.0f;
    s->clear_pos = end;
    if (end >= PU_REV_LEN) {
        s->f0 = 0.0f; s->f1 = 0.0f; s->f2 = 0.0f; s->f3 = 0.0f;
    }
}

/* u[] = screen values in manifest order */
PU_ALWAYS_INLINE(pu_prepare)
static inline void pu_prepare(PuParams *P, const float *u)
{
    int targ = (int)(u[0] + 0.5f), shape = (int)(u[1] + 0.5f), div = (int)(u[3] + 0.5f);
    float size = u[8] * 0.01f;
    if (div < 0) div = 0;
    if (div > 4) div = 4;
    P->inc    = pu_tempo_bpm(u[7]) * PU_INC_PER_BPM;
    P->invDiv = (float)(16 >> div) * 0.25f;          /* 1/16 -> 4 .. BAR -> 0.25 Divs per beat */
    P->shift  = u[4] * 0.01f;                        /* 0..1 beat */
    P->L      = 0.05f + 0.95f * (u[5] * 0.01f);
    P->invL   = pu_rcp(P->L);
    P->depth  = u[2] * 0.01f;
    P->flo    = 1.0f - P->depth;
    P->wDuck  = (shape == 0) ? 1.0f : 0.0f;
    P->wGate  = (shape == 1) ? 1.0f : 0.0f;
    P->wRise  = (shape >= 2) ? 1.0f : 0.0f;
    P->mD     = (targ == 0 || targ == 2) ? 1.0f : 0.0f;
    P->mT     = (targ == 1 || targ == 2) ? 1.0f : 0.0f;
    P->mS     = (targ >= 3) ? 1.0f : 0.0f;
    P->fb     = 0.70f + 0.27f * size;
    P->damp   = 0.15f + 0.40f * size;
    P->damp1  = 1.0f - P->damp;
    P->revIn  = PU_REV_IN * (1.0f - P->fb);       /* comb loudness grows as 1/(1 - fb) */
    P->verbG  = u[6] * 0.01f * PU_REV_OUT;
}

/* Pump envelope at position p (0..1 through the Div): 1 = untouched. */
PU_ALWAYS_INLINE(pu_env)
static inline float pu_env(const PuParams *P, float p)
{
    float eD = 1.0f, eG = 1.0f, eR = P->flo, t;
    if (p < P->L) {
        t = 1.0f - p * P->invL;                      /* 1 at the beat .. 0 at the end of Curve */
        eD = 1.0f - P->depth * t * t;
        eG = P->flo;
    }
    t = p - (1.0f - P->L);
    if (t > 0.0f) {
        t *= P->invL;                                /* 0 .. 1 into the next beat */
        eR = P->flo + P->depth * t * t;
    }
    return P->wDuck * eD + P->wGate * eG + P->wRise * eR;
}

PU_ALWAYS_INLINE(pu_process)
static inline void pu_process(PuState *s, const PuParams *P, float *buf, int n)
{
    int i;
    float bp = s->bp, gD = s->gD, gT = s->gT, gS = s->gS;
    float f0 = s->f0, f1 = s->f1, f2 = s->f2, f3 = s->f3;
    int i0 = s->i0, i1 = s->i1, i2 = s->i2, i3 = s->i3, j0 = s->j0, j1 = s->j1;
    int ready = (s->clear_pos >= PU_REV_LEN);
    float *r = s->rev;
    for (i = 0; i < n; i++) {
        float in = buf[i], x, p, e, rv = 0.0f;
        bp += P->inc;
        if (bp >= PU_BAR) bp -= PU_BAR;
        x = (bp - P->shift + PU_BAR) * P->invDiv;
        p = x - (float)(int)x;
        e = pu_env(P, p) - 1.0f;
        gD += PU_SLEW * (1.0f + P->mD * e - gD);
        gT += PU_SLEW * (1.0f + P->mT * e - gT);
        gS += PU_SLEW * (1.0f + P->mS * e - gS);
        if (ready) {
            float ri = in * gS * P->revIn, y, b, o;
            y = r[PU_C0 + i0]; f0 = y * P->damp1 + f0 * P->damp; r[PU_C0 + i0] = ri + f0 * P->fb;
            if (++i0 >= PU_C0_LEN) i0 = 0;
            rv = y;
            y = r[PU_C1 + i1]; f1 = y * P->damp1 + f1 * P->damp; r[PU_C1 + i1] = ri + f1 * P->fb;
            if (++i1 >= PU_C1_LEN) i1 = 0;
            rv += y;
            y = r[PU_C2 + i2]; f2 = y * P->damp1 + f2 * P->damp; r[PU_C2 + i2] = ri + f2 * P->fb;
            if (++i2 >= PU_C2_LEN) i2 = 0;
            rv += y;
            y = r[PU_C3 + i3]; f3 = y * P->damp1 + f3 * P->damp; r[PU_C3 + i3] = ri + f3 * P->fb;
            if (++i3 >= PU_C3_LEN) i3 = 0;
            rv += y;
            b = r[PU_A0 + j0]; o = b - rv; r[PU_A0 + j0] = rv + b * PU_AP_G;
            if (++j0 >= PU_A0_LEN) j0 = 0;
            rv = o;
            b = r[PU_A1 + j1]; o = b - rv; r[PU_A1 + j1] = rv + b * PU_AP_G;
            if (++j1 >= PU_A1_LEN) j1 = 0;
            rv = o;
        }
        buf[i] = in * gD + rv * P->verbG * gT;
    }
    if (f0 < 1e-15f && f0 > -1e-15f) f0 = 0.0f;
    if (f1 < 1e-15f && f1 > -1e-15f) f1 = 0.0f;
    if (f2 < 1e-15f && f2 > -1e-15f) f2 = 0.0f;
    if (f3 < 1e-15f && f3 > -1e-15f) f3 = 0.0f;
    s->bp = bp; s->gD = gD; s->gT = gT; s->gS = gS;
    s->f0 = f0; s->f1 = f1; s->f2 = f2; s->f3 = f3;
    s->i0 = i0; s->i1 = i1; s->i2 = i2; s->i3 = i3; s->j0 = j0; s->j1 = j1;
}

/* ---- on-screen text ------------------------------------------------------ */
/* knob 0 Targt: 0 "DRY", 1 "VERB", 2 "BOTH", 3 "SEND" */
int ZDL_GetLabel_0(unsigned int value, char *out)
{
    if (value >= 3u) { out[0] = 'S'; out[1] = 'E'; out[2] = 'N'; out[3] = 'D'; out[4] = 0; return 4; }
    if (value == 2u) { out[0] = 'B'; out[1] = 'O'; out[2] = 'T'; out[3] = 'H'; out[4] = 0; return 4; }
    if (value == 1u) { out[0] = 'V'; out[1] = 'E'; out[2] = 'R'; out[3] = 'B'; out[4] = 0; return 4; }
    out[0] = 'D'; out[1] = 'R'; out[2] = 'Y'; out[3] = 0;
    return 3;
}

/* knob 1 Shape: 0 "DUCK", 1 "GATE", 2 "RISE" */
int ZDL_GetLabel_1(unsigned int value, char *out)
{
    if (value >= 2u) { out[0] = 'R'; out[1] = 'I'; out[2] = 'S'; out[3] = 'E'; out[4] = 0; return 4; }
    if (value == 1u) { out[0] = 'G'; out[1] = 'A'; out[2] = 'T'; out[3] = 'E'; out[4] = 0; return 4; }
    out[0] = 'D'; out[1] = 'U'; out[2] = 'C'; out[3] = 'K'; out[4] = 0;
    return 4;
}

/* knob 3 Div: 0 "1/16", 1 "1/8", 2 "1/4", 3 "1/2", 4 "BAR" */
int ZDL_GetLabel_3(unsigned int value, char *out)
{
    if (value >= 4u) { out[0] = 'B'; out[1] = 'A'; out[2] = 'R'; out[3] = 0; return 3; }
    out[0] = '1'; out[1] = '/';
    if (value == 0u) { out[2] = '1'; out[3] = '6'; out[4] = 0; return 4; }
    out[2] = (char)('0' + (16 >> value));        /* 1 -> 8, 2 -> 4, 3 -> 2 */
    out[3] = 0;
    return 3;
}

/* knob 4 Shift: 25 "+1/16", 50 "+1/8", 75 "+3/16", 100 "+1/4", otherwise the number */
int ZDL_GetLabel_4(unsigned int value, char *out)
{
    int n, t = 0, len = 0;
    if (value >= 100u) { out[0] = '+'; out[1] = '1'; out[2] = '/'; out[3] = '4'; out[4] = 0; return 4; }
    if (value == 50u)  { out[0] = '+'; out[1] = '1'; out[2] = '/'; out[3] = '8'; out[4] = 0; return 4; }
    if (value == 25u || value == 75u) {
        out[0] = '+'; out[1] = (value == 25u) ? '1' : '3';
        out[2] = '/'; out[3] = '1'; out[4] = '6'; out[5] = 0;
        return 5;
    }
    n = (int)value;
    while (n >= 10) { n -= 10; t++; }
    if (t > 0) { out[len] = (char)('0' + t); len++; }
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* knob 7 Tempo: screen 0..441 -> the BPM "40" .. "240"; the twin copy 241..441 shows
 * the same numbers again */
int ZDL_GetLabel_7(unsigned int value, char *out)
{
    int n, h = 0, t = 0, len = 0;
    if (value <= 39u) return dt_follow_text(out);
    if (value > 441u) value = 441u;
    n = (int)pu_tempo_bpm((float)(int)value);
    while (n >= 100) { n -= 100; h++; }
    while (n >= 10)  { n -= 10;  t++; }
    if (h > 0) { out[len] = (char)('0' + h); len++; }
    out[len] = (char)('0' + t); len++;
    out[len] = (char)('0' + n); len++;
    out[len] = 0;
    return len;
}

/* ---- pedal entry point ---------------------------------------------------- */
#ifndef BREATHER_HOST_TEST

#include "breather_params.h"

#ifndef BREATHER_AUDIO_FUNC
#define BREATHER_AUDIO_FUNC Fx_DLY_Breather
#endif

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

PU_CODE_SECTION(BREATHER_AUDIO_FUNC)
void BREATHER_AUDIO_FUNC(unsigned int *ctx)
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
    PuState *s;
    PuParams P;
    float u[9];
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
    if ((end - base) < sizeof(PuState) || span < (end - base)) return;
    if (stateBase + sizeof(PuState) > end) return;

    s = (PuState *)stateBase;

    u[0] = pu_ui(params[BREATHER_TARGT_SLOT], (float)BREATHER_TARGT_UI_DEFAULT, 3.0f);
    u[1] = pu_ui(params[BREATHER_SHAPE_SLOT], (float)BREATHER_SHAPE_UI_DEFAULT, 2.0f);
    u[2] = pu_ui(params[BREATHER_DEPTH_SLOT], (float)BREATHER_DEPTH_UI_DEFAULT, 100.0f);
    u[3] = pu_ui(params[BREATHER_DIV_SLOT],   (float)BREATHER_DIV_UI_DEFAULT,   4.0f);
    u[4] = pu_ui(params[BREATHER_SHIFT_SLOT], (float)BREATHER_SHIFT_UI_DEFAULT, 100.0f);
    u[5] = pu_ui(params[BREATHER_CURVE_SLOT], (float)BREATHER_CURVE_UI_DEFAULT, 100.0f);
    u[6] = pu_ui(params[BREATHER_VERB_SLOT],  (float)BREATHER_VERB_UI_DEFAULT,  100.0f);
    u[7] = pu_tempo_ui(params[BREATHER_TEMPO_SLOT], (float)BREATHER_TEMPO_UI_DEFAULT);
    u[8] = pu_ui(params[BREATHER_SIZE_SLOT],  (float)BREATHER_SIZE_UI_DEFAULT,  100.0f);

    if (s->magic != PU_MAGIC) pu_init(s);
    /* bar tag: bars from earlier slots flip the Tempo copy too, FOLLOW takes their BPM */
    u[7] = dt_tempo(&s->sync, dryBuf ? dryBuf + 8 : 0, u[7], dt_id(stateBase));
    pu_prepare(&P, u);
    flip = pu_twin_flip(s, u[7]);
    if (flip) { s->bp = 0.0f; s->since_flip = 0u; }  /* sync reset: beat 1 now */
    else if (s->since_flip < PU_HOST_BLOCKS) s->since_flip++;

    if (params[0] < 0.5f) {                      /* effect switched off: input untouched */
        if (!s->was_off) s->clear_pos = 0;       /* the reverb starts clean when back on */
        s->was_off = 1u;
        pu_clear_step(s);
        s->bp += P.inc * 8.0f;                   /* the clock keeps running */
        if (s->bp >= PU_BAR) s->bp -= PU_BAR;
        return;
    }
    if (s->was_off) {                            /* the pedal was just turned on */
        if (s->since_flip >= PU_HOST_BLOCKS) s->bp = 0.0f;   /* no host: stomp on the one */
        s->was_off = 0u;
    }
    pu_clear_step(s);
    pu_process(s, &P, fxBuf, 8);                 /* mono: left half in place   */

    for (i = 0; i < 8; i++) fxBuf[i + 8] = fxBuf[i];   /* same signal to R     */
}

#endif /* BREATHER_HOST_TEST */
