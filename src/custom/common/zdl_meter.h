/*
 * zdl_meter.h - live level meter drawn by the effect on its own cover (LOUDER, Gooey)
 *
 * The whole cover is the meter (docs/NEW-EFFECTS.md "Readouts"): the fixed title on top, then a
 * scrolling level history (one column = 1/8 beat, 16 beats across), a black box down to the
 * effect's line (ceiling, threshold) and small tags. Approved look: the v12 (LOUDER) and v16
 * (Gooey) mockups.
 *
 * How it gets on screen (MS-70CDR 2.10): the cover draw (0xC00AA3D8) reads imageInfo word 5 (the
 * RLE pointer) on every redraw and decodes it with a bounded decoder (never past 1024 bytes). The
 * cover data holds two RLE buffers of ZM_CAP bytes (the Zoom RLE worst case) and the title raw
 * (custom_covers.meter_layout). While the cover is on screen and the picture changed, the effect
 * renders the next frame into the buffer the firmware is NOT reading, ZM_CHUNK raw bytes per
 * audio block (32 blocks a frame), then swaps word 5 and posts a redraw:
 * Event_post(*(void**)0xC00EF9D8, 0x01). At most one frame start every ZM_GAP blocks (15 a second).
 *
 * Firmware reads and the call happen ONLY after zm_fw_ok() (the first 20 bytes of Event_post
 * match 2.10, the event handle is in RAM). Any other firmware: the cover stays frame A (built at
 * default knobs, empty history) and the meter costs one compare per block. Off screen it costs a
 * counter per block and a few loads every ZM_CHECK blocks: no level, no tempo, no rendering.
 * Coming back on screen clears the history (nothing was measured meanwhile).
 *
 * The includer defines ZM_PIC (the cover data: extern _<audio>_Pic[] on the pedal) and includes
 * <effect>_layout.h (ZM_TOP, ZM_CAP) first. Host tests may define ZM_FW_OK(), ZM_POST(),
 * ZM_FIND(params) and ZM_WORD. No libm, no divide, no switch, no static data, no string literals.
 */
#ifndef ZDL_METER_H
#define ZDL_METER_H

#include <stdint.h>
#include "zmt.h"

#ifdef __TI_COMPILER_VERSION__
#define ZM_DO_PRAGMA(x) _Pragma(#x)
#define ZM_INLINE(fn) ZM_DO_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#else
#define ZM_INLINE(fn)
#endif

#define ZM_RAW     1024u                    /* decoded picture: 8 row blocks x 128 columns    */
#define ZM_CHUNK   32u                      /* raw bytes rendered per audio block            */
#define ZM_GAP     367u                     /* blocks between frame starts: 15 a second      */
#define ZM_CHECK   16u                      /* blocks between on-screen checks               */
#define ZM_EMPTY   64u                      /* wave row of an empty column                   */
#define ZM_TAGW    40u                      /* widest tag text, pixels                       */
#define ZM_NONE    0xFFFFFFFFu
#define ZM_COL     2.41874527e-5f           /* columns per 8-sample block per BPM: 8 a beat  */

typedef char zm_cap_check[(ZM_CAP >= 1536u && ZM_TOP <= 24u) ? 1 : -1];

#ifndef ZM_WORD
#define ZM_WORD unsigned int                /* a firmware pointer word (host tests: uintptr_t) */
#endif
#ifndef ZM_FW_OK
#define ZM_FW_OK() zm_fw_ok()
#endif
#ifndef ZM_POST
#define ZM_POST() zm_post()
#endif
#ifndef ZM_FIND
#define ZM_FIND(params) zm_find(params)
#endif

typedef struct {                            /* make_v12.btag: black border, white box, black text */
    unsigned int x0, x1, ix0, tx0, xr, yb;  /* border x0..x1, white ix0..xr, text tx0..xr-1   */
    unsigned int n;                         /* text columns; 0 = no tag                       */
    unsigned char col[ZM_TAGW];             /* text columns, bit r = row yb-5+r               */
} ZmTag;

typedef struct {
    unsigned int fw;                        /* zm_fw_ok() at init */
    unsigned int on, chk, since, dirty;     /* on screen; blocks to the next check; blocks since the last frame; picture changed */
    unsigned int head, live;                /* history ring: next write; pushes until it is empty again */
    float phase, lvl;                       /* column phase; loudest level since the last column */
    unsigned int key, by;                   /* knobs the box and tags are drawn for; first row under the box */
    unsigned int pos, o, rc, rn, rh;        /* frame: next raw byte (ZM_RAW = none), out bytes, RLE byte and run, ring start */
    unsigned char *out;                     /* frame: the buffer being written */
    volatile ZM_WORD *info;                 /* the firmware's imageInfo of our slot */
    ZtSync zt;
    ZmTag tag[2];
    unsigned char wy[256];                  /* history ring: first wave row per column */
} ZmState;

/* Pure reads: is this MS-70CDR 2.10 with a live UI event? Call once, keep the result. */
ZM_INLINE(zm_fw_ok)
static inline unsigned int zm_fw_ok(void)
{
    const volatile unsigned int *code = (const volatile unsigned int *)0xC00CBF40u;
    unsigned int ev;
    if (code[0] != 0x27773577u || code[1] != 0x85778677u || code[2] != 0xB64631F7u ||
        code[3] != 0x02E7442Bu || code[4] != 0x01E74228u) return 0u;
    ev = *(const volatile unsigned int *)0xC00EF9D8u;
    return (((ev >= 0xC0000000u && ev < 0xC8000000u) || (ev >= 0x11800000u && ev < 0x11840000u)) &&
            (ev & 3u) == 0u) ? 1u : 0u;
}

/* Event_post(ui_event, EV_REDRAW): redraws the page and the cover. Only after zm_fw_ok. */
ZM_INLINE(zm_post)
static inline void zm_post(void)
{
    void *ev = (void *)(uintptr_t)*(const volatile unsigned int *)0xC00EF9D8u;
    ((void (*)(void *, unsigned int))(uintptr_t)0xC00CBF40u)(ev, 0x01u);
}

/* The imageInfo the firmware draws our cover from, or 0 when our cover is not on screen: effect
 * screen (mode 0), cover page (not the knob page), the cursor's slot runs this instance (its params
 * pointer), and the slot's imageInfo is an RLE picture pointing at one of our buffers. 2.10 only. */
ZM_INLINE(zm_find)
static inline volatile unsigned int *zm_find(const void *params)
{
    unsigned int view, slot, a = (unsigned int)(uintptr_t)ZM_PIC, ip;
    int cur;
    volatile unsigned int *info;
    if (*(const volatile unsigned int *)0xC00EF888u != 0u) return 0;          /* screen mode */
    if (*(const volatile unsigned int *)0xC009D398u != 0u) return 0;          /* knob page shown */
    view = *(const volatile unsigned int *)0xC009D8F8u;
    cur = (int)*(const volatile signed char *)0xC009C064u;
    if (view > 5u || cur < 0 || cur > 5) return 0;
    slot = *(const volatile unsigned int *)(uintptr_t)(0xC00EE748u + 4u * (6u * view + (unsigned int)cur));
    if (slot > 5u) return 0;
    if (*(const volatile unsigned int *)(uintptr_t)(0x11F03004u + 212u * slot) != (unsigned int)(uintptr_t)params) return 0;
    ip = *(const volatile unsigned int *)(uintptr_t)(0xC0064E08u + 28u * slot);
    if ((ip & 3u) != 0u || !((ip >= 0xC0000000u && ip < 0xC8000000u) ||          /* DDR, shared, L2 RAM */
        (ip >= 0x80000000u && ip < 0x80020000u) || (ip >= 0x11800000u && ip < 0x11840000u))) return 0;
    info = (volatile unsigned int *)(uintptr_t)ip;
    if (*((const volatile unsigned char *)info + 4) != 1u) return 0;         /* RLE picture */
    if (info[5] != a && info[5] != a + ZM_CAP) return 0;                    /* ours */
    return info;
}

/* rows lo..hi-1 inside row block p8..p8+7 as a column byte (bit z = row p8 + z) */
ZM_INLINE(zm_span)
static inline unsigned int zm_span(int p8, int lo, int hi)
{
    int a = lo - p8, b = hi - p8;
    a = (a < 0) ? 0 : (a > 8 ? 8 : a);
    b = (b < 0) ? 0 : (b > 8 ? 8 : b);
    return ((1u << b) - 1u) & ~((1u << a) - 1u);
}

/* make_v8 SMALL font: 15 bits, top row first, left pixel = high bit; '-' is 2 wide, '.' 1 */
ZM_INLINE(zm_glyph)
static inline unsigned int zm_glyph(char ch)
{
    return (ch == 'T') ? 0x7492u : (ch == 'H') ? 0x5BEDu : (ch == 'R') ? 0x6BADu : (ch == 'S') ? 0x388Eu :
           (ch == 'C') ? 0x3923u : (ch == 'E') ? 0x79A7u : (ch == 'I') ? 0x7497u : (ch == 'L') ? 0x4927u :
           (ch == 'G') ? 0x396Bu : (ch == 'A') ? 0x2BEDu : (ch == 'N') ? 0x6B6Du : (ch == '-') ? 0x0180u :
           (ch == '+') ? 0x05D0u : (ch == '.') ? 0x0004u : (ch == '0') ? 0x7B6Fu : (ch == '1') ? 0x2C97u :
           (ch == '2') ? 0x73E7u : (ch == '3') ? 0x73CFu : (ch == '4') ? 0x5BC9u : (ch == '5') ? 0x79CFu :
           (ch == '6') ? 0x79EFu : (ch == '7') ? 0x7249u : (ch == '8') ? 0x7BEFu : (ch == '9') ? 0x7BCFu : 0u;
}

/* tag with text s[0..len-1], white box right edge xr, bottom row yb (make_v12.btag) */
ZM_INLINE(zm_tag_set)
static inline void zm_tag_set(ZmTag *t, const char *s, unsigned int len, unsigned int xr, unsigned int yb)
{
    unsigned int n = 0u, i, c, r, w, f;
    for (i = 0u; i < len; i++) {
        f = zm_glyph(s[i]);
        w = (s[i] == '-') ? 2u : (s[i] == '.' || s[i] == ' ') ? 1u : 3u;
        if (i > 0u && n < ZM_TAGW) t->col[n++] = 0u;
        for (c = 0u; c < w && n < ZM_TAGW; c++) {
            unsigned int b = 0u;
            for (r = 0u; r < 5u; r++) b |= ((f >> (14u - r * 3u - c)) & 1u) << r;
            t->col[n++] = (unsigned char)b;
        }
    }
    t->n = n; t->xr = xr; t->yb = yb;
    t->tx0 = xr - n; t->ix0 = xr - n - 1u; t->x0 = xr - n - 3u;
    t->x1 = (xr + 2u > 127u) ? 127u : xr + 2u;
}

/* "12.3" from tenths (0..999) at out[len]; returns the new length */
ZM_INLINE(zm_tenths)
static inline unsigned int zm_tenths(unsigned int v, char *out, unsigned int len)
{
    unsigned int h = 0u, t = 0u;
    while (v >= 100u) { v -= 100u; h++; }
    while (v >= 10u) { v -= 10u; t++; }
    if (h) out[len++] = (char)('0' + h);
    out[len++] = (char)('0' + t);
    out[len++] = '.';
    out[len++] = (char)('0' + v);
    return len;
}

/* 20 log10(a), a > 0: ln of the mantissa (1..2) from a polynomial (error < 1e-4) plus the exponent */
ZM_INLINE(zm_db)
static inline float zm_db(float a)
{
    union { float f; unsigned int u; } c;
    float m;
    int e;
    c.f = a;
    e = (int)(c.u >> 23) - 127;
    c.u = (c.u & 0x007FFFFFu) | 0x3F800000u;
    m = c.f;
    return 8.6858896f * ((float)e * 0.6931472f +
           (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m));
}

ZM_INLINE(zm_clear)
static inline void zm_clear(ZmState *z)
{
    unsigned int i;
    for (i = 0u; i < 256u; i++) z->wy[i] = (unsigned char)ZM_EMPTY;
    z->live = 0u; z->lvl = 0.0f; z->phase = 0.0f;
}

ZM_INLINE(zm_init)
static inline void zm_init(ZmState *z)
{
    zm_clear(z);
    zt_init(&z->zt);
    z->fw = ZM_FW_OK();
    z->on = 0u; z->chk = 1u; z->since = ZM_GAP; z->dirty = 0u; z->head = 0u;
    z->key = ZM_NONE; z->by = ZM_TOP; z->pos = ZM_RAW; z->o = 0u; z->rc = 0u; z->rn = 0u; z->rh = 0u;
    z->out = 0; z->info = 0;
    z->tag[0].n = 0u; z->tag[1].n = 0u;
}

/* Once per block, first. 1 = our cover is on screen: measure into zm.lvl, then zm_column / zm_push,
 * the tags (only while z->pos >= ZM_RAW) and zm_frame. 0 = nothing more to do this block. */
ZM_INLINE(zm_begin)
static inline unsigned int zm_begin(ZmState *z, const void *params)
{
    volatile ZM_WORD *info;
    if (!z->fw) return 0u;
    if (z->since < ZM_GAP) z->since++;
    if (--z->chk != 0u) return z->on;
    z->chk = ZM_CHECK;
    info = (volatile ZM_WORD *)ZM_FIND(params);
    if (!info) { z->on = 0u; z->pos = ZM_RAW; return 0u; }        /* off screen: drop any frame */
    if (!z->on) { z->on = 1u; zm_clear(z); z->key = ZM_NONE; z->dirty = 1u; }
    z->info = info;
    return 1u;
}

/* 1 when a new history column is due (8 a beat at the held tempo: MIDI clock with MIDISTOMP,
 * else the patch TEMPO); then zm_push the row of zm.lvl */
ZM_INLINE(zm_column)
static inline unsigned int zm_column(ZmState *z)
{
    float beats;
    int run, start;
    z->phase += zt_update(&z->zt, &run, &beats, &start) * ZM_COL;
    if (z->phase < 1.0f) return 0u;
    z->phase -= 1.0f;
    if (z->phase >= 1.0f) z->phase = 0.0f;
    return 1u;
}

/* newest column: first wave row (ZM_TOP..ZM_EMPTY); resets the level */
ZM_INLINE(zm_push)
static inline void zm_push(ZmState *z, unsigned int row)
{
    if (row < ZM_TOP) row = ZM_TOP;
    if (row > ZM_EMPTY) row = ZM_EMPTY;
    if (z->live || row < ZM_EMPTY) z->dirty = 1u;                  /* else empty into empty: same picture */
    z->wy[z->head & 255u] = (unsigned char)row;
    z->head++;
    z->live = (row < ZM_EMPTY) ? 128u : z->live - (z->live != 0u);
    z->lvl = 0.0f;
}

/* one RLE output byte; the guard never trips (ZM_CAP is the worst case) */
ZM_INLINE(zm_emit)
static inline void zm_emit(ZmState *z, unsigned int b)
{
    if (z->o < ZM_CAP) z->out[z->o] = (unsigned char)b;
    z->o++;
}

/* close the pending run: one byte literal, else "X X n" = n + 2 copies (encode_zoom_rle) */
ZM_INLINE(zm_flush)
static inline void zm_flush(ZmState *z)
{
    if (z->rn == 1u) zm_emit(z, z->rc);
    else if (z->rn > 1u) { zm_emit(z, z->rc); zm_emit(z, z->rc); zm_emit(z, z->rn - 2u); }
    z->rn = 0u;
}

/* ZM_CHUNK raw bytes of the frame (all in one row block), RLE-encoded on the fly */
ZM_INLINE(zm_render)
static inline void zm_render(ZmState *z)
{
    unsigned int pos = z->pos, end = pos + ZM_CHUNK, p = pos >> 7, tm, boxm, k;
    int p8 = (int)(p << 3);
    const unsigned char *ttl = (const unsigned char *)ZM_PIC + 2u * ZM_CAP + ((p < 3u ? p : 0u) << 7);
    unsigned int om[2], im[2];
    int sh[2];
    tm = (p < 3u) ? zm_span(p8, 0, (int)ZM_TOP) : 0u;
    boxm = zm_span(p8, (int)ZM_TOP, (int)z->by);
    for (k = 0u; k < 2u; k++) {
        int yb = (int)z->tag[k].yb;
        om[k] = zm_span(p8, yb - 8, yb + 3);
        im[k] = zm_span(p8, yb - 6, yb + 1);
        sh[k] = yb - 5 - p8;
        sh[k] = (sh[k] > 8) ? 8 : (sh[k] < -8 ? -8 : sh[k]);         /* text outside this block: shifts out */
    }
    for (; pos < end; pos++) {
        unsigned int x = pos & 127u, b = boxm ^ zm_span(p8, (int)z->wy[(z->rh + x) & 255u], 64);
        b |= ttl[x] & tm;
        for (k = 0u; k < 2u; k++) {
            const ZmTag *t = &z->tag[k];
            if (t->n && x >= t->x0 && x <= t->x1) {
                b |= om[k];
                if (x >= t->ix0 && x <= t->xr) {
                    b &= ~im[k];
                    if (x >= t->tx0 && x < t->xr) {
                        unsigned int c = t->col[x - t->tx0];
                        b |= ((sh[k] >= 0) ? (c << sh[k]) : (c >> -sh[k])) & 0xFFu;
                    }
                }
            }
        }
        if (z->rn && b == z->rc && z->rn < 257u) z->rn++;
        else { zm_flush(z); z->rc = b; z->rn = 1u; }
    }
    z->pos = pos;
}

/* Start a frame when the picture changed and the last one is ZM_GAP blocks old; render a chunk;
 * at the end point imageInfo word 5 at the new buffer and post the redraw. */
ZM_INLINE(zm_frame)
static inline void zm_frame(ZmState *z)
{
    ZM_WORD a = (ZM_WORD)(uintptr_t)ZM_PIC, b = a + ZM_CAP, front;
    if (z->pos >= ZM_RAW) {
        if (!z->dirty || z->since < ZM_GAP) return;
        front = z->info[5];
        z->out = (unsigned char *)ZM_PIC + ((front == a) ? ZM_CAP : 0u);
        z->pos = 0u; z->o = 0u; z->rn = 0u; z->rh = z->head - 128u;
        z->dirty = 0u; z->since = 0u;
    }
    zm_render(z);
    if (z->pos < ZM_RAW) return;
    zm_flush(z);
    front = z->info[5];
    if (front == a || front == b) {
        z->info[5] = (ZM_WORD)(uintptr_t)z->out;
        ZM_POST();
    }
}

#endif /* ZDL_METER_H */
