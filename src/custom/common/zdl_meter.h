/*
 * zdl_meter.h - live gain-reduction meter drawn by the effect on its own cover (LOUDER, Gooey)
 *
 * How it works (hardware-tested by themanro's Blink probe on MS-70CDR 2.10; plan in
 * docs/NEW-EFFECTS.md "Readouts"): the cover RLE blob lives in RAM and the firmware decodes it on
 * every redraw. The cover has a band (one 8-row block, COVER_BAND_LEN columns, bytes that never
 * repeat so the RLE stores them literally; build.py checks this and writes <effect>_layout.h).
 * The effect writes the bar and the dB number into the band, then asks the UI task for a redraw
 * with Event_post(*(void**)0xC00EF9D8, 0x01). That call is made ONLY if the first 20 bytes of
 * Event_post match MS-70CDR 2.10 and the event handle is in RAM (zm_fw_ok, checked once at state
 * init by the effect). Any other firmware: the band is still written, nothing is called.
 *
 * Band byte = one column, bit z = pixel row 24 + z. Bit 7 is a dotted baseline (set on even
 * columns) and is NEVER left to the drawing: two equal neighbours would read as an RLE run.
 *
 * The includer defines ZM_PIC (the cover base: extern _<audio>_Pic[] on the pedal) and includes
 * <effect>_layout.h first. Host tests may define ZM_FW_OK() and ZM_POST() before including.
 * No libm, no divide, no switch, no static data.
 */
#ifndef ZDL_METER_H
#define ZDL_METER_H

#include <stdint.h>

#ifdef __TI_COMPILER_VERSION__
#define ZM_DO_PRAGMA(x) _Pragma(#x)
#define ZM_INLINE(fn) ZM_DO_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#else
#define ZM_INLINE(fn)
#endif

#define ZM_BW      119u                     /* band columns: must equal COVER_BAND_LEN */
#define ZM_BARX    18u                      /* first bar column (number area before it) */
#define ZM_BARW    (ZM_BW - ZM_BARX)        /* 101: frame at both ends, fill inside      */
#define ZM_FULL_TENTHS 180.0f               /* bar full scale: 18.0 dB */

typedef char zm_band_len_check[(COVER_BAND_LEN == ZM_BW) ? 1 : -1];

#ifndef ZM_FW_OK
#define ZM_FW_OK() zm_fw_ok()
#endif
#ifndef ZM_POST
#define ZM_POST() zm_post()
#endif

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

/* one column (bits 1..5) of a 3 x 5 digit: font = 15 bits, top row first, left pixel = high bit */
ZM_INLINE(zm_col)
static inline unsigned int zm_col(unsigned int d, unsigned int c)
{
    unsigned int f = (d == 0u) ? 0x7B6Fu : (d == 1u) ? 0x2C97u : (d == 2u) ? 0x73E7u : (d == 3u) ? 0x73CFu :
                     (d == 4u) ? 0x5BC9u : (d == 5u) ? 0x79CFu : (d == 6u) ? 0x79EFu : (d == 7u) ? 0x7249u :
                     (d == 8u) ? 0x7BEFu : 0x7BCFu;
    unsigned int r, b = 0u;
    for (r = 0u; r < 5u; r++)
        if ((f >> (14u - r * 3u - c)) & 1u) b |= 2u << r;
    return b;
}

/* Draw "-12.3" and the bar (t = tenths of a dB, 0..999) into the band. */
ZM_INLINE(zm_band_write)
static inline void zm_band_write(volatile unsigned char *band, unsigned int t)
{
    unsigned int d1 = 0u, d2 = 0u, d3, i, mx;
    int n = (int)t, w;
    while (n >= 100) { n -= 100; d1++; }
    while (n >= 10) { n -= 10; d2++; }
    d3 = (unsigned int)n;
    w = (int)((float)t * ((float)(ZM_BARW - 2u) * (1.0f / ZM_FULL_TENTHS)) + 0.5f);
    if (w > (int)(ZM_BARW - 2u)) w = (int)(ZM_BARW - 2u);
    mx = d1 ? 0u : 4u;                                  /* minus sign, right before the first digit */
    for (i = 0u; i < ZM_BW; i++) {
        unsigned int b = (i & 1u) ? 0u : 0x80u;
        if (i >= ZM_BARX) {
            unsigned int j = i - ZM_BARX;
            b |= (j == 0u || j == ZM_BARW - 1u) ? 0x7Fu : 0x41u;       /* frame: ends, top and bottom rows */
            if (j >= 1u && (int)j <= w) b |= 0x1Cu;                    /* fill: three middle rows */
        } else {
            if (i == mx || i == mx + 1u) b |= 0x08u;
            if (d1 && i >= 3u && i <= 5u) b |= zm_col(d1, i - 3u);
            if (i >= 7u && i <= 9u) b |= zm_col(d2, i - 7u);
            if (i == 11u) b |= 0x20u;
            if (i >= 13u && i <= 15u) b |= zm_col(d3, i - 13u);
        }
        band[i] = (unsigned char)b;
    }
}

/* the shown value changed: redraw the band, then (firmware checked) ask for a screen redraw */
ZM_INLINE(zm_show)
static inline void zm_show(unsigned int tenths, unsigned int fw_ok)
{
    zm_band_write((volatile unsigned char *)ZM_PIC + COVER_BAND_OFF, tenths);
    if (fw_ok) ZM_POST();
}

#endif /* ZDL_METER_H */
