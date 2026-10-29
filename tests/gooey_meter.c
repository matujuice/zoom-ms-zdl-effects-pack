/* Gooey meter on the cover: the band writer, the RLE (decoded again after every write: length and
 * bytes outside the band never change), the firmware check (no Event_post without it), the redraw
 * throttle, and preview PNGs of the decoded cover at a few gain-reduction levels. */
#define GOOEY_HOST_TEST
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#define ZM_WANT_BLOB
static unsigned char zm_t_blob[2048];                 /* the cover as the pedal's RAM holds it */
static unsigned int zm_t_fw, zm_t_posts;
#define ZM_PIC zm_t_blob
#define ZM_FW_OK() zm_t_fw
#define ZM_POST() (zm_t_posts++)
#include "../src/custom/gooey/gooey.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#ifndef GO_PREVIEW_DIR
#define GO_PREVIEW_DIR "C:/Users/lucam/OneDrive/Documenti/CLAUDE/gooey-meter-preview"
#endif

/* ---- a tiny PNG writer (8-bit grey, stored deflate blocks, no zlib needed) ---- */
static unsigned int crc_tab[256];
static void put32(unsigned char *p, unsigned int v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static unsigned int crc32b(unsigned int c, const unsigned char *d, size_t n)
{
    size_t i; int k;
    if (!crc_tab[1]) for (i = 0; i < 256; i++) { unsigned int x = (unsigned int)i; for (k = 0; k < 8; k++) x = (x & 1) ? 0xEDB88320u ^ (x >> 1) : x >> 1; crc_tab[i] = x; }
    c = ~c; for (i = 0; i < n; i++) c = crc_tab[(c ^ d[i]) & 255] ^ (c >> 8); return ~c;
}
static void chunk(FILE *f, const char *type, const unsigned char *d, unsigned int n)
{
    unsigned char h[8]; unsigned int c;
    put32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    c = crc32b(0, (const unsigned char *)type, 4); if (n) c = crc32b(c, d, n);
    put32(h, c); fwrite(h, 1, 4, f);
}
static int write_png(const char *path, const unsigned char *grey, int w, int h)
{
    FILE *f = fopen(path, "wb");
    size_t raw = (size_t)(w + 1) * h, i, pos = 0, z = 0;
    unsigned char *r, *zb, hdr[13];
    unsigned int a = 1, b = 0;
    if (!f) return 0;
    r = malloc(raw); zb = malloc(raw + raw / 65535 * 5 + 64);
    for (i = 0; i < (size_t)h; i++) { r[i * (w + 1)] = 0; memcpy(r + i * (w + 1) + 1, grey + (size_t)i * w, w); }
    for (i = 0; i < raw; i++) { a = (a + r[i]) % 65521u; b = (b + a) % 65521u; }
    zb[z++] = 0x78; zb[z++] = 0x01;
    while (pos < raw) {
        size_t n = raw - pos > 65535 ? 65535 : raw - pos;
        zb[z++] = (pos + n == raw); zb[z++] = n & 255; zb[z++] = n >> 8; zb[z++] = ~n & 255; zb[z++] = (~n >> 8) & 255;
        memcpy(zb + z, r + pos, n); z += n; pos += n;
    }
    put32(zb + z, (b << 16) | a); z += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    put32(hdr, w); put32(hdr + 4, h); hdr[8] = 8; hdr[9] = 0; hdr[10] = 0; hdr[11] = 0; hdr[12] = 0;
    chunk(f, "IHDR", hdr, 13); chunk(f, "IDAT", zb, (unsigned int)z); chunk(f, "IEND", 0, 0);
    fclose(f); free(r); free(zb);
    return 1;
}

/* decode the Zoom RLE: a byte, and if the next one is equal, a count (run = count + 2). 1024 bytes
 * out = 8 row blocks of 128 columns; returns how many bytes were produced */
static int rle_decode(const unsigned char *in, int n, unsigned char *raw)
{
    int i = 0, o = 0;
    while (o < 1024 && i < n) {
        unsigned char b = in[i];
        if (i + 2 < n && in[i + 1] == b) { int c = in[i + 2] + 2; while (c-- && o < 1024) raw[o++] = b; i += 3; }
        else { raw[o++] = b; i++; }
    }
    return o;
}

#define BAND_RAW0 (3u * 128u + 4u)                  /* block 3, column 4: where the band lands once decoded */
static unsigned char ORIG_RAW[1024];
static void reset_blob(void) { memset(zm_t_blob, 0, sizeof zm_t_blob); memcpy(zm_t_blob, COVER_BLOB, COVER_BLOB_LEN); }

/* the blob as it is now must decode to the original picture everywhere but the band, and to exactly
 * the bytes the writer stored inside it; the padding after the blob must be untouched */
static int blob_ok(void)
{
    unsigned char raw[1024];
    unsigned int i;
    if (rle_decode(zm_t_blob, COVER_BLOB_LEN, raw) != 1024) return 0;
    for (i = 0; i < 1024; i++)
        if ((i < BAND_RAW0 || i >= BAND_RAW0 + COVER_BAND_LEN) && raw[i] != ORIG_RAW[i]) return 0;
    for (i = 0; i < COVER_BAND_LEN; i++)
        if (raw[BAND_RAW0 + i] != zm_t_blob[COVER_BAND_OFF + i]) return 0;      /* stored literally */
    for (i = COVER_BLOB_LEN; i < sizeof zm_t_blob; i++) if (zm_t_blob[i]) return 0;
    return 1;
}

/* the cover drawn 6x as big, black on white like the LCD */
static void preview(unsigned int tenths, const char *name)
{
    static unsigned char raw[1024], img[768 * 384];
    char path[512];
    int x, y, d, k = 0;
    reset_blob();
    zm_band_write(zm_t_blob + COVER_BAND_OFF, tenths);
    rle_decode(zm_t_blob, COVER_BLOB_LEN, raw);
    for (y = 0; y < 64; y++) for (x = 0; x < 128; x++) {
        unsigned char v = ((raw[(y >> 3) * 128 + x] >> (y & 7)) & 1) ? 0 : 255;
        for (d = 0; d < 36; d++) img[(y * 6 + d / 6) * 768 + x * 6 + d % 6] = v;
    }
    snprintf(path, sizeof path, "%s/%s", GO_PREVIEW_DIR, name);
    if (!write_png(path, img, 768, 384)) {
        snprintf(path, sizeof path, "tests/_gen/%s", name);
        k = write_png(path, img, 768, 384);
        printf("(could not write to %s, wrote %s: %s)\n", GO_PREVIEW_DIR, path, k ? "ok" : "failed");
    } else printf("wrote %s\n", path);
}

static GoState S;
/* thr -30, ratio 4: a 1 kHz sine at -12 dB is 18 dB over, so 13.5 dB of reduction. amp picks the level. */
static void drive(int blocks, float amp)
{
    float u[7] = { 10, 2, 2, 3, 1, 100, 0 }; GoParams P; int b, i;
    for (b = 0; b < blocks; b++) {
        float buf[16];
        for (i = 0; i < 8; i++) { buf[i] = amp * sinf(0.1425f * (float)(b * 8 + i)); buf[i + 8] = buf[i]; }
        go_prepare(&P, &S, u);
        go_process(&S, &P, buf);
    }
}

int main(void)
{
    static const unsigned int lv[6] = { 0, 15, 30, 60, 100, 180 };
    char name[64];
    unsigned int t, i;
    int g;

    CHECK(sizeof(COVER_BLOB) <= sizeof zm_t_blob, "blob too big for the test buffer");
    CHECK(rle_decode(COVER_BLOB, COVER_BLOB_LEN, ORIG_RAW) == 1024, "the cover does not decode to 1024 bytes");

    /* ---- the band writer: for EVERY value 0.0 .. 99.9 dB the blob still decodes to the same picture
     * outside the band, the band is stored literally, and the bar width follows the value ---- */
    for (t = 0; t < 1000; t++) {
        unsigned int cols = 0;
        reset_blob();
        zm_band_write(zm_t_blob + COVER_BAND_OFF, t);
        if (!blob_ok()) { CHECK(0, "blob changed beyond the band at %u tenths", t); break; }
        for (i = ZM_BARX + 1u; i < ZM_BW - 1u; i++) cols += (zm_t_blob[COVER_BAND_OFF + i] & 0x1Cu) == 0x1Cu;
        if (t <= 180u) {
            unsigned int want = (unsigned int)((float)t * (99.0f / 180.0f) + 0.5f);
            if (cols != want) { CHECK(0, "%u tenths: %u bar columns, want %u", t, cols, want); break; }
        }
    }

    /* ---- the firmware check: no Event_post without it; the band is still drawn ---- */
    for (g = 0; g < 2; g++) {
        zm_t_fw = (unsigned int)g; zm_t_posts = 0;
        reset_blob(); go_init(&S);
        CHECK(S.m_fw == (unsigned int)g, "state kept check result %u, want %d", S.m_fw, g);
        drive(5512, 0.25f);                   /* one second, 13.5 dB of reduction */
        printf("fw check %d: posts %u, held reduction %.3f, shown %u\n", g, zm_t_posts, S.m_peak, S.m_shown);
        CHECK(blob_ok(), "blob damaged by the writes (fw %d)", g);
        CHECK(memcmp(zm_t_blob, COVER_BLOB, COVER_BLOB_LEN) != 0, "band not drawn (fw %d)", g);
        CHECK(S.m_shown >= 125u && S.m_shown <= 145u, "shown %u tenths, want 125..145", S.m_shown);
        if (g == 0) CHECK(zm_t_posts == 0u, "Event_post called %u times with the firmware check failed", zm_t_posts);
        else CHECK(zm_t_posts >= 1u && zm_t_posts <= 15u, "%u redraw posts in one second (1..15 allowed)", zm_t_posts);
    }

    /* ---- hold: the peak stays 300 ms after the reduction drops, then follows the value ---- */
    zm_t_fw = 0; reset_blob(); go_init(&S);
    go_meter_step(&S, 0.5f);
    {
        int held = 0;
        while (S.m_peak >= 0.5f && held < 4000) { go_meter_step(&S, 0.1f); held++; }
        printf("hold: peak held %d blocks (%.0f ms)\n", held - 1, (held - 1) * 8.0f / 44.1f);
        CHECK(held - 1 >= 1650 && held - 1 <= 1660, "hold time %d blocks", held - 1);
        CHECK(S.m_peak == 0.1f, "peak does not follow after the hold");
    }

    /* ---- a steady value: no band writes and no posts (the blob is byte for byte the same) ---- */
    zm_t_fw = 1; reset_blob(); go_init(&S);
    drive(5512, 0.25f);                   /* one second, 13.5 dB of reduction */
    {
        static unsigned char snap[sizeof zm_t_blob];
        unsigned int p0 = zm_t_posts;
        memcpy(snap, zm_t_blob, sizeof snap);
        S.m_tick = 0u; S.m_hold = 0u; S.m_peak = 0.0f; S.m_shown = 0u;
        for (i = 0; i < 4000u; i++) go_meter_step(&S, 0.0f);   /* 0.0 dB, nothing shown yet: no change */
        CHECK(zm_t_posts == p0 && memcmp(snap, zm_t_blob, sizeof snap) == 0, "writes or posts with a steady value");
        for (i = 0; i < 4000u; i++) go_meter_step(&S, 0.25f);  /* one change, then steady */
        CHECK(zm_t_posts == p0 + 1u, "%u posts for one change", zm_t_posts - p0);
        memcpy(snap, zm_t_blob, sizeof snap);
        for (i = 0; i < 4000u; i++) go_meter_step(&S, 0.25f);
        CHECK(zm_t_posts == p0 + 1u && memcmp(snap, zm_t_blob, sizeof snap) == 0, "writes or posts with a steady value");
    }
    /* and the cap: a value that changes every tick still posts at most 15 times a second */
    zm_t_fw = 1; zm_t_posts = 0; reset_blob(); go_init(&S);
    for (i = 0; i < 5512u; i++) go_meter_step(&S, 0.01f * (float)((i / 367u) % 40u + 1u));
    printf("changing value: %u posts in one second\n", zm_t_posts);
    CHECK(zm_t_posts >= 1u && zm_t_posts <= 15u, "%u posts in one second with a changing value", zm_t_posts);

    /* ---- preview pictures of the decoded cover ---- */
    for (i = 0; i < 6; i++) {
        snprintf(name, sizeof name, "gooey-meter-%02u.%udB.png", lv[i] / 10, lv[i] % 10);
        preview(lv[i], name);
    }
    /* the number's own conversion: 3 dB = factor 0.7079 */
    CHECK(go_tenths(1.0f - 0.70795f) >= 29u && go_tenths(1.0f - 0.70795f) <= 31u, "go_tenths(3 dB) = %u", go_tenths(1.0f - 0.70795f));
    CHECK(go_tenths(0.0f) == 0u, "go_tenths(0) = %u", go_tenths(0.0f));
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
