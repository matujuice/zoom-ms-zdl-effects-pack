/* GRDLOOP loop bar on its own cover (looper.c lp_bar): every frame equals the static cover with
 * MIDISTOMP's old lbox.c bar(0,17,127,34,2,4) drawn on top (a straight port of it below, on a made-up
 * FONT7), stays in ZM_CAP, the pointer flips A/B, one frame per change, at most 15 a second, and
 * nothing on stock firmware, without the services claim or off screen. */
#define LOOPER_HOST_TEST
#define ZM_WANT_BLOB
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
static unsigned char t_blob[4096];
static unsigned int t_fw, t_posts, t_screen;
static uintptr_t t_info[8];
static unsigned char t_font[256 * 7];
#define ZM_PIC t_blob
#define ZM_WORD uintptr_t
#define ZM_FW_OK() t_fw
#define ZM_POST() (t_posts++)
#define ZM_FIND(params) (t_screen ? t_info : (uintptr_t *)0)
#define LP_FONT7 t_font
#include "../src/custom/looper/looper.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- lbox.c bar(), ported as it was (px on a page-format framebuffer) ---- */
static unsigned char FB[1024];
static void px(int x, int y, int op)
{
    unsigned char *p = FB + ((y >> 3) << 7) + x, b = (unsigned char)(1u << (y & 7));
    if (op == 0) *p &= (unsigned char)~b; else if (op == 1) *p |= b; else *p ^= b;
}
static int put(char *t, int k, const char *s) { while (*s) t[k++] = *s++; return k; }
static int put_n(char *t, int k, int n) { int d = 0; while (n >= 10) { n -= 10; d++; } if (d) t[k++] = (char)('0' + d); t[k++] = (char)('0' + n); return k; }
static int left_text(char *t, unsigned c)
{
    unsigned kind = c & 15, n = c >> 4 & 63, m = c >> 10 & 63;
    int k = 0;
    if (kind == 1) return put(t, 0, "EMPTY");
    if (kind == 8) return put(t, 0, "STOPPED");
    if (kind >= 2 && kind <= 4) {
        k = put(t, 0, kind == 2 ? "REC IN " : kind == 3 ? "STOP IN " : "PLAY IN ");
        k = put_n(t, k, n);
        k = put(t, k, c >> 16 & 1 ? " BEAT" : " BAR");
        if (n > 1) t[k++] = 'S';
        return k;
    }
    if (kind == 7) { k = put_n(t, put(t, 0, "PLAY "), n); k = put(t, k, " BAR"); if (n > 1) t[k++] = 'S'; return k; }
    k = put_n(t, put(t, 0, "REC "), n);
    if (kind == 6) { t[k++] = '/'; k = put_n(t, k, m); }
    if (c >> 17 & 1) k = put(t, k, kind == 6 ? " M" : " MONO");
    return k;
}
static int text(const char *t, int n, int x, int y, int y0, int xmax, unsigned *ink, int draw)
{
    int i, j, r, w = 0;
    for (i = 0; i < n; i++) {
        const unsigned char *f = t_font + 7 * (unsigned char)t[i];
        int any = 0;
        for (j = 0; j < 7; j++) {
            if (!f[j]) continue;
            any = 1;
            if (draw && x + w <= xmax)
                for (r = 0; r < 7; r++)
                    if (f[j] >> r & 1) { px(x + w, y + r, 2); ink[x + w] |= 1u << (y + r - y0); }
            w++;
        }
        w += any ? 1 : 2;
    }
    return w - 1;
}
static void bar(unsigned left, unsigned prog, int x0, int y0, int x1, int y1, int s, int margin)
{
    unsigned ink[128];
    int xi0 = x0 + s, xi1 = x1 - s, yi0 = y0 + s, yi1 = y1 - s, wi = xi1 - xi0 + 1;
    int fill = (int)((unsigned)wi * (prog & 0xFFFF) >> 8), cyc = prog >> 16 & 63;
    int ty = yi0 + ((yi1 - yi0 + 1 - 7) >> 1), x, y, i, q, r, n, w;
    char t[24];
    if (fill > wi) fill = wi;
    for (x = x0; x <= x1; x++) {
        ink[x] = 0;
        for (y = y0; y <= y1; y++)
            px(x, y, x < x0 + s || x > x1 - s || y < y0 + s || y > y1 - s || x - xi0 < fill);
    }
    n = left_text(t, left);
    text(t, n, xi0 + margin, ty, y0, xi1, ink, 1);
    if (cyc) {
        if (prog >> 30 & 1) n = put(t, 0, "NO CLK");
        else { n = put_n(t, 0, prog >> 22 & 63); t[n++] = '.'; n = put_n(t, n, (prog >> 28 & 3) + 1); t[n++] = '/'; n = put_n(t, n, cyc); }
        w = text(t, n, 0, 0, 0, 0, ink, 0);
        text(t, n, xi1 - margin + 1 - w, ty, y0, xi1, ink, 1);
    }
    if (cyc > 1 && wi >= 3 * cyc)
        for (i = 1, q = 0, r = 0; i < cyc; i++) {
            r += wi;
            while (r >= cyc) { r -= cyc; q++; }
            x = xi0 + q;
            for (y = y0; y < yi0; y++) { px(x, y, 0); px(x, y1 - (y - y0), 0); }
            for (y = yi0 + (s == 1); y < yi1; y += 2) {
                unsigned nb = (ink[x - 1] | ink[x] | ink[x + 1]) >> (y - y0 - 1) & 7;
                if (!nb) px(x, y, 2);
            }
        }
    {
        static const signed char c[6] = { 0, 0, 1, 0, 0, 1 };
        for (i = 0; i < 6; i += 2) {
            px(x0 + c[i], y0 + c[i + 1], 0); px(x1 - c[i], y0 + c[i + 1], 0);
            px(x0 + c[i], y1 - c[i + 1], 0); px(x1 - c[i], y1 - c[i + 1], 0);
        }
        px(x0 + 1, y0 + 1, 1); px(x1 - 1, y0 + 1, 1); px(x0 + 1, y1 - 1, 1); px(x1 - 1, y1 - 1, 1);
    }
}

/* ---- the fake firmware ---- */
static int rle_decode(const unsigned char *in, unsigned int cap, unsigned char *out)
{
    unsigned int i = 0, o = 0;
    while (o < 1024 && i < cap) {
        if (i + 1 < cap && in[i] == in[i + 1]) { unsigned int n = in[i + 2] + 2u; while (n-- && o < 1024) out[o++] = in[i]; i += 3; }
        else out[o++] = in[i++];
    }
    return o == 1024 ? (int)i : -1;
}

static LpState S;
static unsigned char arena[4096];
static void fresh(unsigned int fw)
{
    unsigned i;
    memcpy(t_blob, COVER_BLOB, COVER_BLOB_LEN);
    memset(t_info, 0, sizeof t_info);
    t_info[5] = (uintptr_t)t_blob;
    t_fw = fw; t_posts = 0; t_screen = 1;
    memset(&S, 0x5A, sizeof S);
    lp_init(&S, (uintptr_t)arena, (int)sizeof arena);
    S.own = 1; S.me = 0x1234u;
    memset(ls_host, 0, sizeof ls_host);
    ls_host[0] = LS_MAGIC;
    for (i = 0; i < 256 * 7; i++) {                     /* a made-up FONT7: 3..6 inked columns, gaps, bit 7 junk */
        unsigned h = (i / 7) * 2654435761u ^ (i % 7) * 40503u;
        t_font[i] = (i % 7 < 3 + (i / 7) % 4) ? (unsigned char)((h >> 9) | 1u) : 0u;
    }
    memset(t_font + 7 * ' ', 0, 7);
}
static void blocks(int n) { while (n-- > 0) lp_bar(&S); }

/* the shown picture equals the static cover with lbox's bar on top */
static int shown_matches(unsigned left, unsigned prog)
{
    unsigned char got[1024];
    const unsigned char *pic = (const unsigned char *)t_info[5];
    int i, n;
    if (pic != t_blob && pic != t_blob + ZM_CAP) { printf("pointer off our buffers\n"); return 0; }
    n = rle_decode(pic, ZM_CAP, got);
    if (n < 0) { printf("RLE does not decode\n"); return 0; }
    memcpy(FB, COVER_BLOB + 2 * ZM_CAP, 1024);
    bar(left, prog, 0, 17, 127, 34, 2, 4);
    for (i = 0; i < 1024; i++)
        if (got[i] != FB[i]) { printf("left %08X prog %08X: byte %d (x %d, rows %d-%d) %02X want %02X\n", left, prog, i, i & 127, (i >> 7) * 8, (i >> 7) * 8 + 7, got[i], FB[i]); return 0; }
    return 1;
}

/* set the state, wait out the gap, then one whole frame */
static int show(unsigned left, unsigned prog)
{
    unsigned p0 = t_posts;
    ls_host[LS_LEFT] = left; ls_host[LS_PROG] = prog;
    blocks((int)ZM_GAP + 40);
    return t_posts == p0 + 1u && shown_matches(left, prog);
}

int main(void)
{
    static const unsigned lefts[] = { 1, 8, 7 | 1 << 4, 7 | 4 << 4, 5 | 3 << 4, 5 | 12 << 4 | 1 << 17,
        6 | 14 << 4 | 16 << 10 | 1 << 17, 6 | 4 << 4 | 8 << 10, 2 | 2 << 4, 3 | 1 << 4 | 1 << 16, 4 | 63 << 4 | 1 << 16 };
    static const unsigned progs[] = { 0, 1u << 30 | 1u << 16, 128 | 4u << 16 | 3u << 22 | 2u << 28, 256 | 32u << 16 | 32u << 22 | 3u << 28,
        77 | 8u << 16 | 1u << 22, 200 | 63u << 16 | 63u << 22 | 3u << 28, 255 | 41u << 16 | 9u << 22 | 1u << 28, 1u << 30 | 2u << 16 | 64 };
    unsigned i, j, p0, a0;
    unsigned char a[ZM_CAP];

    /* lbox's look, for every kind of text and bar */
    fresh(1);
    for (i = 0; i < sizeof lefts / sizeof lefts[0]; i++)
        for (j = 0; j < sizeof progs / sizeof progs[0]; j++)
            CHECK(show(lefts[i], progs[j]), "frame for left %08X prog %08X", lefts[i], progs[j]);
    srand(7);
    for (i = 0; i < 300; i++) {
        unsigned l = (unsigned)(1 + rand() % 8) | (unsigned)(rand() % 64) << 4 | (unsigned)(rand() % 64) << 10 | (unsigned)(rand() % 4) << 16;
        unsigned p = (unsigned)(rand() % 257) | (unsigned)(rand() % 64) << 16 | (unsigned)(rand() % 64) << 22 | (unsigned)(rand() % 4) << 28 | (unsigned)(rand() % 8 == 0) << 30;
        CHECK(show(l, p), "random frame %u", i);
    }
    CHECK(t_info[5] == (uintptr_t)t_blob || t_info[5] == (uintptr_t)(t_blob + ZM_CAP), "pointer");

    /* flips A/B; nothing new while the bar stays the same (a fill change under 1 px is the same) */
    fresh(1);
    ls_host[LS_LEFT] = 7 | 2 << 4; ls_host[LS_PROG] = 11 | 4u << 16 | 1u << 22;   /* fill 5 px */
    blocks(2000);
    CHECK(t_posts == 1 && t_info[5] == (uintptr_t)(t_blob + ZM_CAP), "first frame into B: posts %u", t_posts);
    ls_host[LS_PROG] = 12 | 4u << 16 | 1u << 22;   /* still 5 px */
    blocks(2000);
    CHECK(t_posts == 1, "under 1 px: no new frame (posts %u)", t_posts);
    ls_host[LS_PROG] = 14 | 4u << 16 | 1u << 22;   /* 6 px */
    blocks(2000);
    CHECK(t_posts == 2 && t_info[5] == (uintptr_t)t_blob, "second frame back into A: posts %u", t_posts);

    /* at most 15 frames a second while it changes every block */
    fresh(1);
    for (i = 0; i < 5512; i++) { ls_host[LS_PROG] = (i & 255) | 4u << 16 | (i & 7) << 22; lp_bar(&S); }
    CHECK(t_posts >= 14 && t_posts <= 16, "frames in a second: %u", t_posts);

    /* stock firmware, no claim, off screen: the cover stays frame A, nothing posted */
    fresh(0);
    memcpy(a, t_blob, ZM_CAP);
    ls_host[LS_LEFT] = 1; ls_host[LS_PROG] = 100 | 4u << 16;
    blocks(5000);
    CHECK(t_posts == 0 && t_info[5] == (uintptr_t)t_blob && !memcmp(a, t_blob, ZM_CAP), "stock firmware: frame A untouched");
    fresh(1); S.own = 0;
    blocks(5000);
    CHECK(t_posts == 0 && S.bar.pos == ZM_RAW, "no claim: nothing");
    fresh(1); t_screen = 0;
    blocks(5000);
    CHECK(t_posts == 0 && S.bar.on == 0 && S.bar.pos == ZM_RAW, "off screen: nothing");
    /* leaving the screen mid-frame drops it; coming back redraws it */
    fresh(1);
    blocks(10);                                         /* a frame under way */
    t_screen = 0; blocks(40);
    p0 = t_posts; a0 = S.bar.pos;
    CHECK(p0 == 0 && a0 == ZM_RAW, "frame dropped off screen (posts %u)", p0);
    t_screen = 1; blocks((int)ZM_GAP + 60);
    CHECK(t_posts == 1 && shown_matches(ls_host[LS_LEFT], ls_host[LS_PROG]), "back on screen: redrawn");

    /* the static cover itself: frame A decodes to the raw copy */
    {
        unsigned char got[1024];
        CHECK(rle_decode(COVER_BLOB, ZM_CAP, got) > 0 && !memcmp(got, COVER_BLOB + 2 * ZM_CAP, 1024), "frame A = the static raw");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
