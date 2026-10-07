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
 * Who writes: SyncEQ and the tempo effects send only while LIVE (Mozaic is flipping their
 * Tempo; Luca, 2026-10-06): a flip within 2.5 times the gap between their last two flips, so
 * a pad turned off stops sending after about 2 bars (60 s after a first flip, until there is
 * a gap). A sender writes its tag unless a LIVE tag from another sender is already there and
 * still moving, in which case that one stays (same Mozaic, same bar); a frozen one (sender
 * stopped or removed) is taken over. An effect that is not sending never touches the Dry
 * buffer, so the tag passes through it unchanged.
 *
 * Receiving (dt_tempo, used by every tempo effect): the effect hands over its Tempo screen
 * number and gets back the one its existing code should use. Only with the knob on FOLLOW
 * (screen 0..39, shown FOLLW) does it listen: the BPM comes from the tag (120 until a tag
 * is heard; the last BPM is kept when the sender goes away) and a bar from upstream (the counter changed) toggles the number to the other twin
 * copy, exactly as a Mozaic flip of its own knob would, so each effect restarts the way it
 * already does on a flip. On any BPM the effect ignores the tag and runs on its own (free,
 * or its own resets) (Luca, 2026-10-06). In slots 1-3 a FOLLOW effect needs Mozaic's Send
 * knob on another slot, or Mozaic overwrites the knob with a BPM. A tag whose "blocks since flip" stops moving
 * for 1 s is stale (sender removed) and is ignored.
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
#define DT_NOUNROLL _Pragma("UNROLL(1)")
#else
#define DT_ALWAYS_INLINE(fn)
#define DT_NOUNROLL
#endif

#define DT_SIG          0xA5Cu
#define DT_VERSION      0x10u
#define DT_LIVE         0x01u
#define DT_XOR          0x5A5u
#define DT_SCALE        5.9604645e-8f        /* 2^-24 */
#define DT_UNSCALE      16777216.0f          /* 2^24  */
#define DT_AGE_MAX      0xFFFFFFu
#define DT_LIVE_BLOCKS  330750u              /* 60 s: LIVE after the first flip, until a second one */
#define DT_DEFER_QUIET  8u                   /* another sender's tag counts only while its age moves */
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
    unsigned int gap;      /* blocks between the last two flips (0 = one flip so far) */
} DtSend;

/* Receiver state */
typedef struct {
    unsigned int id, count, age;   /* last tag seen (id 0 = none yet), its highest age */
    unsigned int quiet;            /* blocks its age has not moved    */
    unsigned int ok;               /* a fresh tag from another sender */
    unsigned int bpm16;            /* its BPM x 16; kept when the tag goes away */
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
    t->count = 0u; t->age = 0u; t->flipped = 0u; t->gap = 0u;
}

/* LIVE = Mozaic is flipping this knob: a flip within 2.5 times the gap between the last two
 * (so a pad turned off stops the sending after about 2 bars, and another sender takes over),
 * or within 60 s of the first flip. Luca, 2026-10-06: a pad turned off kept its slot sending
 * as if live for 60 s, and the next sender waited behind it. */
DT_ALWAYS_INLINE(dt_live)
static inline int dt_live(const DtSend *t)
{
    unsigned int lim = t->gap ? t->gap + t->gap + (t->gap >> 1) + 64u : DT_LIVE_BLOCKS;
    return (t->flipped && t->age < lim) ? 1 : 0;
}

/* Once per block: count a flip (bar counter +1, remember the gap) or age by one block. */
DT_ALWAYS_INLINE(dt_flip)
static inline void dt_flip(DtSend *t, int flip)
{
    if (flip) {
        t->gap = t->flipped ? t->age + 1u : 0u;
        t->count = (t->count + 1u) & 0xFFFu; t->age = 0u; t->flipped = 1u;
    } else if (t->age < DT_AGE_MAX) t->age++;
}

/* One value of the tag: n (0..4095) times 2^-24. */
DT_ALWAYS_INLINE(dt_put)
static inline float dt_put(unsigned int n)
{
    return (float)(int)(n & 0xFFFu) * DT_SCALE;
}

/* Read the tag in r[0..7] (Dry right). Returns 1 and fills *t when valid. */
DT_ALWAYS_INLINE(dt_read)
static inline int dt_read(const float *r, DtTag *t)
{
    unsigned int n[8], sum = 0u;
    float v;
    int j, k;
    DT_NOUNROLL
    for (j = 0; j < 8; j++) {
        v = r[j] * DT_UNSCALE;
        if (!(v >= 0.0f && v <= 4095.0f)) return 0;        /* also rejects NaN */
        k = (int)v;
        if ((float)k != v) return 0;                       /* not an exact whole number */
        n[j] = (unsigned int)k;
        if (j < 7) sum += n[j];
    }
    if (n[0] != DT_SIG || (n[6] & ~DT_LIVE) != DT_VERSION) return 0;
    if (((sum ^ DT_XOR) & 0xFFFu) != n[7]) return 0;
    t->id = n[1];
    t->count = n[2];
    t->bpm16 = n[3];
    t->age = n[4] | (n[5] << 12);
    t->live = n[6] & DT_LIVE;
    return 1;
}

DT_ALWAYS_INLINE(dt_write)
static inline void dt_write(float *r, unsigned int id, unsigned int count,
                            unsigned int bpm16, unsigned int age, unsigned int live)
{
    unsigned int n[8], sum = 0u;
    int j;
    n[0] = DT_SIG; n[1] = id & 0xFFFu; n[2] = count & 0xFFFu; n[3] = bpm16 & 0xFFFu;
    n[4] = age & 0xFFFu; n[5] = (age >> 12) & 0xFFFu; n[6] = DT_VERSION | (live ? DT_LIVE : 0u);
    DT_NOUNROLL
    for (j = 0; j < 7; j++) sum += n[j];
    n[7] = (sum ^ DT_XOR) & 0xFFFu;
    DT_NOUNROLL
    for (j = 0; j < 8; j++) r[j] = dt_put(n[j]);
}

/* Receiver, once per block, given this block's read of Dry right (valid = dt_read's result).
 * Returns 1 on a new bar. */
DT_ALWAYS_INLINE(dt_recv)
static inline int dt_recv(DtRecv *v, int valid, const DtTag *t, unsigned int id)
{
    int bar, moving;
    if (!valid || t->id == id) { v->ok = 0u; return 0; }
    /* moving = a new sender, a new bar, or an age past the highest seen since (a tag that
     * stops, even one that alternates between two old copies, is not moving) */
    if (t->id == v->id && t->count == v->count && t->age <= v->age) {
        if (v->quiet < DT_STALE_BLOCKS) v->quiet++;
        moving = 0;
    } else {
        v->quiet = 0u;
        moving = 1;
    }
    v->ok = (v->quiet < DT_STALE_BLOCKS) ? 1u : 0u;
    /* a new bar: the counter moved, or a sender has just gone LIVE with its first flip */
    bar = (v->ok && ((t->id == v->id && t->count != v->count) || (t->id != v->id && t->live && t->age == 0u))) ? 1 : 0;
    if (moving) v->age = t->age;                             /* highest age seen of this bar */
    v->id = t->id; v->count = t->count; v->bpm16 = t->bpm16;
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

/* Sender side for SyncEQ and dt_tempo, once per block after dt_recv on the same read.
 * Writes while LIVE, unless a LIVE tag from a sender with a lower id is there and still
 * moving. The lower id wins whatever the slot order, so with two senders every slot agrees
 * on one, however the pedal keeps the buffer between blocks (both are flipped on the same
 * bar by the same Mozaic). A tag that froze belongs to a sender that stopped or was removed
 * and is taken over. */
DT_ALWAYS_INLINE(dt_tx)
static inline int dt_tx(DtSync *y, float *r, int valid, const DtTag *up, unsigned int bpm16,
                        unsigned int id)
{
    if (!r || !dt_live(&y->tx)) return 0;
    if (valid && up->id < id && up->live && y->rx.quiet < DT_DEFER_QUIET) return 0;
    dt_write(r, id, y->tx.count, bpm16, y->tx.age, 1u);
    return 1;
}

/* Sender only (SyncEQ), once per block. r = Dry right (ctx[4] + 8) or 0, flip = this
 * block's Tempo twin flip, bpm16 = BPM x 16, id = dt_id(state). Sends only while LIVE
 * (Mozaic flips its Tempo). Returns 1 when it wrote its own tag, 0 when it wrote nothing. */
DT_ALWAYS_INLINE(dt_send)
static inline int dt_send(DtSync *y, float *r, int flip, unsigned int bpm16, unsigned int id)
{
    DtTag up;
    int valid = 0;
    if (r) valid = dt_read(r, &up);
    dt_recv(&y->rx, valid, &up, id);                         /* only to see whether up is moving */
    dt_flip(&y->tx, flip);
    return dt_tx(y, r, valid, &up, bpm16, id);
}

/* For a tempo effect, once per block and before anything reads Tempo (also while switched
 * off). ui = the Tempo knob's screen number 0..441, r = Dry right (ctx[4] + 8) or 0, id =
 * dt_id(state). Receives, sends while LIVE, and returns the screen number the effect's own
 * code should use: on a BPM the knob as it is (own flips still toggle the copy); on FOLLOW
 * the tag's BPM on the copy its bars point at. */
DT_ALWAYS_INLINE(dt_tempo)
static inline float dt_tempo(DtSync *y, float *r, float ui, unsigned int id)
{
    DtTag up;
    int tw = (ui > 240.0f) ? 1 : 0;
    int flip = (y->own_twin >= 0 && tw != y->own_twin);
    int follow = (ui <= DT_FOLLOW_MAX) ? 1 : 0;
    int valid = 0, bar;
    float bpm;
    y->own_twin = tw;
    if (r) valid = dt_read(r, &up);                         /* one read serves both sides */
    bar = dt_recv(&y->rx, valid, &up, id);
    if (follow) bpm = (float)(int)((y->rx.bpm16 + 8u) >> 4); /* last BPM heard, 120 before any */
    else bpm = (ui > 240.0f) ? ui - 201.0f : ui;
    if (bpm < 40.0f) bpm = 40.0f;
    if (bpm > 240.0f) bpm = 240.0f;
    /* send while LIVE (Mozaic flips this knob), unless a moving LIVE tag from another slot is there */
    dt_flip(&y->tx, flip);
    dt_tx(y, r, valid, &up, (unsigned int)(int)(bpm * 16.0f), id);
    if (flip || (bar && follow)) y->vtwin ^= 1u;           /* bars count only on FOLLOW */
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
