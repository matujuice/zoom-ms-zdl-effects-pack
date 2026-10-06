/*
 * drytag.h - the bar tag that carries Mozaic's bar sync from slots 1-3 to later slots.
 *
 * Why: the pedal only accepts knob edits from outside on slots 1-3, so Mozaic can't flip the
 * Tempo of an effect in slots 4-6. But the Dry buffer (ctx[4], 16 floats: 8 left then 8
 * right) travels through every slot, and its right half reaches later slots without being
 * heard (DryPrb probe on the MS-60B, 2026-10-06; see docs/TEMPO-SYNC.md, "Bar tag").
 *
 * The tag: a sender writes 8 numbers into Dry right (ctx[4] + 8) on every block. Each one is
 * a whole number n (0..4095) times 2^-24, so the largest value is 0.00024 (-72 dBFS) and
 * every bit can be checked by the reader.
 *   0 signature 0xA5C
 *   1 sender id: 12 bits of the sender's arena address (different in every slot)
 *   2 bar counter: +1 on every Tempo twin flip of the sender (wraps at 4096)
 *   3 BPM x 16 (40..240 -> 640..3840)
 *   4 blocks since the last flip, low 12 bits
 *   5 blocks since the last flip, high 12 bits (stops at 2^24 - 1, about 50 minutes)
 *   6 format: 0x10 = version 1, plus 1 when LIVE (a twin flip in the last 60 s)
 *   7 checksum: (sum of 0..6) xor 0x5A5, low 12 bits
 * Valid = all 8 are exact whole numbers times 2^-24 in 0..4095 and the signature, version and
 * checksum match. Audio, silence or noise can't pass that.
 *
 * Who writes: a sender writes its tag unless a valid tag from another sender is already
 * there, in which case the upstream one stays (same Mozaic, same bar), except that a LIVE
 * sender replaces a tag that is not live. SyncEQ always sends; a tempo effect sends only
 * while LIVE (Mozaic is flipping its Tempo). An effect that is not sending never touches the
 * Dry buffer, so the tag passes through it unchanged.
 *
 * Receiving (dt_tempo, used by every tempo effect): the effect hands over its Tempo screen
 * number and gets back the one its existing code should use. A bar from upstream (the
 * counter changed) toggles that number to the other twin copy, exactly as a Mozaic flip of
 * its own knob would, so each effect restarts the way it already does on a flip. With the
 * knob on FOLLOW (screen 0..39) the BPM comes from the tag (120 when there is none). A LIVE
 * sender ignores upstream bars: it gets the same bars from Mozaic directly. A tag whose
 * "blocks since flip" stops moving for 1 s is stale (sender removed) and is ignored.
 *
 * Pedal rules (docs/SAFE-DSP-RULES.md): no division, no tables, no libm, helpers inline.
 * The including file defines nothing; everything here is prefixed dt_ / DT_.
 */
#ifndef DRYTAG_H
#define DRYTAG_H

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define DT_DO_PRAGMA(x) _Pragma(#x)
#define DT_EXPAND_PRAGMA(x) DT_DO_PRAGMA(x)
#define DT_ALWAYS_INLINE(fn) DT_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#else
#define DT_ALWAYS_INLINE(fn)
#endif

#define DT_SIG          0xA5Cu
#define DT_VERSION      0x10u
#define DT_LIVE         0x01u
#define DT_XOR          0x5A5u
#define DT_SCALE        5.9604645e-8f        /* 2^-24 */
#define DT_UNSCALE      16777216.0f          /* 2^24  */
#define DT_AGE_MAX      0xFFFFFFu
#define DT_LIVE_BLOCKS  330750u              /* 60 s of 8-sample blocks: 8 bars down to 32 BPM */
#define DT_STALE_BLOCKS 5513u                /* 1 s: a tag that stops counting is gone          */
#define DT_FOLLOW_MAX   39.0f                /* Tempo screen 0..39 = FOLLOW                     */

/* What a reader gets out of a valid tag. */
typedef struct {
    unsigned int id, count, bpm16, age, live;
} DtTag;

/* Sender state: lives in the effect's arena state. */
typedef struct {
    unsigned int count;    /* bar counter                            */
    unsigned int age;      /* blocks since the last flip (or since loading) */
    unsigned int flipped;  /* 1 once a twin flip has been seen        */
} DtSend;

/* Receiver state */
typedef struct {
    unsigned int id, count, age;   /* last tag seen (id 0 = none yet) */
    unsigned int quiet;            /* blocks its age has not moved    */
    unsigned int ok;               /* a fresh tag from another sender */
    unsigned int bpm16;            /* its BPM x 16                    */
} DtRecv;

/* Everything a tempo effect keeps for the tag (put it in the arena state). */
typedef struct {
    DtSend tx;
    DtRecv rx;
    int    own_twin;               /* the knob's own copy; -1 = not read yet */
    unsigned int vtwin;            /* copy handed to the effect (toggled by bars) */
} DtSync;

/* 12-bit id from the state address: each slot has its own arena. Never 0. */
DT_ALWAYS_INLINE(dt_id)
static inline unsigned int dt_id(uintptr_t state_addr)
{
    unsigned int a = (unsigned int)state_addr;
    unsigned int id = ((a >> 4) ^ (a >> 16) ^ (a >> 28)) & 0xFFFu;
    if (id == 0u) id = 1u;
    return id;
}

DT_ALWAYS_INLINE(dt_send_init)
static inline void dt_send_init(DtSend *t)
{
    t->count = 0u; t->age = 0u; t->flipped = 0u;
}

/* One value of the tag: n in 0..4095 -> -1 when it is not an exact whole number there. */
DT_ALWAYS_INLINE(dt_get)
static inline int dt_get(float x)
{
    float v = x * DT_UNSCALE;
    int n;
    if (!(v >= 0.0f && v <= 4095.0f)) return -1;       /* also rejects NaN */
    n = (int)v;
    if ((float)n != v) return -1;
    return n;
}

/* Read the tag in r[0..7] (Dry right). Returns 1 and fills *t when valid. */
DT_ALWAYS_INLINE(dt_read)
static inline int dt_read(const float *r, DtTag *t)
{
    int n0 = dt_get(r[0]), n1 = dt_get(r[1]), n2 = dt_get(r[2]), n3 = dt_get(r[3]);
    int n4 = dt_get(r[4]), n5 = dt_get(r[5]), n6 = dt_get(r[6]), n7 = dt_get(r[7]);
    unsigned int sum;
    if ((n0 | n1 | n2 | n3 | n4 | n5 | n6 | n7) < 0) return 0;
    if ((unsigned int)n0 != DT_SIG) return 0;
    if (((unsigned int)n6 & ~DT_LIVE) != DT_VERSION) return 0;
    sum = (unsigned int)(n0 + n1 + n2 + n3 + n4 + n5 + n6);
    if (((sum ^ DT_XOR) & 0xFFFu) != (unsigned int)n7) return 0;
    t->id = (unsigned int)n1;
    t->count = (unsigned int)n2;
    t->bpm16 = (unsigned int)n3;
    t->age = (unsigned int)n4 | ((unsigned int)n5 << 12);
    t->live = (unsigned int)n6 & DT_LIVE;
    return 1;
}

DT_ALWAYS_INLINE(dt_write)
static inline void dt_write(float *r, unsigned int id, unsigned int count,
                            unsigned int bpm16, unsigned int age, unsigned int live)
{
    unsigned int n1 = id & 0xFFFu, n2 = count & 0xFFFu, n3 = bpm16 & 0xFFFu;
    unsigned int n4 = age & 0xFFFu, n5 = (age >> 12) & 0xFFFu;
    unsigned int n6 = DT_VERSION | (live ? DT_LIVE : 0u);
    unsigned int n7 = ((DT_SIG + n1 + n2 + n3 + n4 + n5 + n6) ^ DT_XOR) & 0xFFFu;
    r[0] = (float)(int)DT_SIG * DT_SCALE;
    r[1] = (float)(int)n1 * DT_SCALE;
    r[2] = (float)(int)n2 * DT_SCALE;
    r[3] = (float)(int)n3 * DT_SCALE;
    r[4] = (float)(int)n4 * DT_SCALE;
    r[5] = (float)(int)n5 * DT_SCALE;
    r[6] = (float)(int)n6 * DT_SCALE;
    r[7] = (float)(int)n7 * DT_SCALE;
}

/* Sender, once per block. r = Dry right (ctx[4] + 8), flip = this block's Tempo twin flip,
 * bpm16 = BPM x 16, id = dt_id(state), always = send even while not LIVE (SyncEQ). Returns 1
 * when it wrote its own tag, 0 when it wrote nothing. */
DT_ALWAYS_INLINE(dt_send)
static inline int dt_send(DtSend *t, float *r, int flip, unsigned int bpm16, unsigned int id,
                          int always)
{
    DtTag up;
    unsigned int live;
    if (flip) { t->count = (t->count + 1u) & 0xFFFu; t->age = 0u; t->flipped = 1u; }
    else if (t->age < DT_AGE_MAX) t->age++;
    live = (t->flipped && t->age < DT_LIVE_BLOCKS) ? 1u : 0u;
    if (!live && !always) return 0;
    if (dt_read(r, &up) && up.id != id && (up.live || !live)) return 0;
    dt_write(r, id, t->count, bpm16, t->age, live);
    return 1;
}

/* Receiver, once per block, before the effect's own send. Returns 1 on a new bar. */
DT_ALWAYS_INLINE(dt_recv)
static inline int dt_recv(DtRecv *v, const float *r, unsigned int id)
{
    DtTag t;
    int bar;
    if (!dt_read(r, &t) || t.id == id) { v->ok = 0u; return 0; }
    if (t.id == v->id && t.age == v->age) { if (v->quiet < DT_STALE_BLOCKS) v->quiet++; }
    else v->quiet = 0u;
    v->ok = (v->quiet < DT_STALE_BLOCKS) ? 1u : 0u;
    /* a new bar: the counter moved, or a sender has just gone LIVE with its first flip */
    bar = (v->ok && ((t.id == v->id && t.count != v->count) || (t.id != v->id && t.live && t.age == 0u))) ? 1 : 0;
    v->id = t.id; v->count = t.count; v->age = t.age; v->bpm16 = t.bpm16;
    return bar;
}

DT_ALWAYS_INLINE(dt_sync_init)
static inline void dt_sync_init(DtSync *y)
{
    dt_send_init(&y->tx);
    y->rx.id = 0u; y->rx.count = 0u; y->rx.age = 0u; y->rx.quiet = 0u; y->rx.ok = 0u;
    y->rx.bpm16 = 1920u;
    y->own_twin = -1; y->vtwin = 0u;
}

/* For a tempo effect, once per block and before anything reads Tempo (also while switched
 * off). ui = the Tempo knob's screen number 0..441, r = Dry right (ctx[4] + 8) or 0, id =
 * dt_id(state). Receives, sends while LIVE, and returns the screen number the effect's own
 * code should use: the BPM (own, or the tag's on FOLLOW) on the copy the bars point at. */
DT_ALWAYS_INLINE(dt_tempo)
static inline float dt_tempo(DtSync *y, float *r, float ui, unsigned int id)
{
    int tw = (ui > 240.0f) ? 1 : 0;
    int flip = (y->own_twin >= 0 && tw != y->own_twin);
    int bar = 0;
    float bpm;
    y->own_twin = tw;
    if (r) bar = dt_recv(&y->rx, r, id);
    if (ui <= DT_FOLLOW_MAX) {                               /* FOLLOW */
        bpm = 120.0f;
        if (y->rx.ok) bpm = (float)(int)((y->rx.bpm16 + 8u) >> 4);
    } else {
        bpm = (ui > 240.0f) ? ui - 201.0f : ui;
    }
    if (bpm < 40.0f) bpm = 40.0f;
    if (bpm > 240.0f) bpm = 240.0f;
    if (r) dt_send(&y->tx, r, flip, (unsigned int)(int)(bpm * 16.0f), id, 0);
    if (flip || (bar && !(y->tx.flipped && y->tx.age < DT_LIVE_BLOCKS))) y->vtwin ^= 1u;
    return y->vtwin ? bpm + 201.0f : bpm;
}

/* Tempo knob text for screen 0..39 (labels hold 5 characters) */
DT_ALWAYS_INLINE(dt_follow_text)
static inline int dt_follow_text(char *out)
{
    out[0] = 'F'; out[1] = 'O'; out[2] = 'L'; out[3] = 'L'; out[4] = 'W'; out[5] = 0;
    return 5;
}

#endif /* DRYTAG_H */
