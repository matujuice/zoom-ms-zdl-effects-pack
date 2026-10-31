/* zm_test.h - shared host-test kit for the cover meter (src/custom/common/zdl_meter.h): a fake
 * firmware (imageInfo, on-screen switch, Event_post counter), the Zoom RLE decoder and encoder,
 * an independent pixel renderer of the approved look (make_v12.btag, custom_covers.meter_frame)
 * and a PNG writer. The effect test defines ZM_EFFECT_C (its .c) and includes this, then defines
 * the eff_* hooks and calls zmt_run(). */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define ZM_WANT_BLOB
static unsigned char zm_t_blob[4096];                 /* the cover data as the pedal's RAM holds it */
static unsigned int zm_t_fw, zm_t_posts, zm_t_screen;
static uintptr_t zm_t_info[8];                        /* the firmware's imageInfo of our slot */
#define ZM_PIC zm_t_blob
#define ZM_WORD uintptr_t
#define ZM_FW_OK() zm_t_fw
#define ZM_POST() (zm_t_posts++)
#define ZM_FIND(params) (zm_t_screen ? zm_t_info : (uintptr_t *)0)
#include ZM_EFFECT_C                                  /* the effect under test, set by the includer */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

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

/* the firmware's decoder (0xC00AA3D8): a byte equal to the previous one is followed by a count n,
 * n + 1 more copies, then no previous byte; stops at 1024 bytes out. Returns the bytes produced. */
static int rle_decode(const unsigned char *in, int n, unsigned char *raw)
{
    int i = 0, o = 0, prev = -1;
    while (o < 1024 && i < n) {
        int b = in[i++];
        if (b == prev && i < n) { int c = in[i++] + 1; while (c-- && o < 1024) raw[o++] = (unsigned char)b; prev = -1; }
        else { raw[o++] = (unsigned char)b; prev = b; }
    }
    return o;
}

/* build/screen_image.encode_zoom_rle (greedy runs of 2..257) */
static int rle_encode(const unsigned char *raw, unsigned char *out)
{
    int i = 0, o = 0;
    while (i < 1024) {
        if (i + 1 < 1024 && raw[i] == raw[i + 1]) {
            int j = i + 2;
            while (j < 1024 && raw[j] == raw[i]) j++;
            if (j > i + 257) j = i + 257;
            out[o++] = raw[i]; out[o++] = raw[i]; out[o++] = (unsigned char)(j - i - 2); i = j;
        } else out[o++] = raw[i++];
    }
    return o;
}

/* ---- reference renderer, written from the mockup (make_v8.SMALL glyph rows, make_v12.btag) ---- */
static const char *ref_glyph(char ch)
{
    switch (ch) {
    case 'T': return "###/.#./.#./.#./.#.";  case 'H': return "#.#/#.#/###/#.#/#.#";
    case 'R': return "##./#.#/##./#.#/#.#";  case 'S': return ".##/#../.#./..#/##.";
    case 'C': return ".##/#../#../#../.##";  case 'E': return "###/#../##./#../###";
    case 'I': return "###/.#./.#./.#./###";  case 'L': return "#../#../#../#../###";
    case 'G': return ".##/#../#.#/#.#/.##";  case 'A': return ".#./#.#/###/#.#/#.#";
    case 'N': return "##./#.#/#.#/#.#/#.#";  case '-': return "../../##/../..";
    case '+': return ".../.#./###/.#./...";  case '.': return "././././#";
    case ' ': return "./././././";
    case '0': return "###/#.#/#.#/#.#/###";  case '1': return ".#./##./.#./.#./###";
    case '2': return "###/..#/###/#../###";  case '3': return "###/..#/###/..#/###";
    case '4': return "#.#/#.#/###/..#/..#";  case '5': return "###/#../###/..#/###";
    case '6': return "###/#../###/#.#/###";  case '7': return "###/..#/..#/..#/..#";
    case '8': return "###/#.#/###/#.#/###";  case '9': return "###/#.#/###/..#/###";
    }
    return 0;
}
static int ref_gw(char ch) { const char *g = ref_glyph(ch); int w = 0; while (g[w] != '/') w++; return w; }

typedef struct { char text[16]; int xr, yb; } RefTag;
static unsigned char ref_px[64][128];
static void ref_set(int x, int y, int v) { if (x >= 0 && x < 128 && y >= 0 && y < 64) ref_px[y][x] = (unsigned char)v; }

/* the picture: title raw (from the cover data) rows 0..top-1, box rows top..by-1 XOR the wave
 * (rows max(rows[x], top)..63), then each tag: black rect, white rect, black text */
static void ref_frame(unsigned char *raw, const unsigned char *title, int top, int by, const int *rows,
                      const RefTag *tags, int ntags)
{
    int x, y, k;
    for (y = 0; y < 64; y++) for (x = 0; x < 128; x++) {
        if (y < top) ref_px[y][x] = (unsigned char)((title[(y >> 3) * 128 + x] >> (y & 7)) & 1);
        else { int r = rows[x] < top ? top : rows[x]; ref_px[y][x] = (unsigned char)((y < by) ^ (y >= r)); }
    }
    for (k = 0; k < ntags; k++) {
        const char *s = tags[k].text;
        int w = -1, xr = tags[k].xr, yb = tags[k].yb, i, cx;
        for (i = 0; s[i]; i++) w += ref_gw(s[i]) + 1;
        for (x = xr - w - 2; x <= xr + 1; x++) for (y = yb - 7; y <= yb + 1; y++) ref_set(x, y, 1);
        for (x = xr - w - 1; x <= xr; x++) for (y = yb - 6; y <= yb; y++) ref_set(x, y, 0);
        cx = xr - w;
        for (i = 0; s[i]; i++) {
            const char *g = ref_glyph(s[i]);
            int gw = ref_gw(s[i]), r, c;
            for (r = 0; r < 5; r++) for (c = 0; c < gw; c++) if (g[r * (gw + 1) + c] == '#') ref_set(cx + c, yb - 5 + r, 1);
            cx += gw + 1;
        }
    }
    for (k = 0; k < 1024; k++) {
        int b = 0, z;
        for (z = 0; z < 8; z++) b |= ref_px[(k >> 7) * 8 + z][k & 127] << z;
        raw[k] = (unsigned char)b;
    }
}

/* Python's round(): half to even */
static int py_round(double v) { return (int)nearbyint(v); }

/* ---- the effect side, defined by the test file ---- */
static void eff_init(void);                           /* fresh state (magic, zm_init) */
static void eff_block(const float *u, float amp, int b); /* one block: begin, DSP, meter */
static ZmState *eff_zm(void);
static void eff_geom(const float *u, int *by, RefTag *tags, int *ntags);  /* custom_covers.meter_geometry */
static void eff_randk(float *u);                      /* random box/tag knobs within their ranges */
static void eff_line(const float *u, double *line_db, double *rpd);  /* the line's level, rows a dB */
static const char *EFF_NAME;
static const char *EFF_PREVIEW_DIR;
#ifndef EFF_NK
#define EFF_NK 8                                      /* knobs in the manifest */
#endif

/* reference row of a level: the line mid-meter, rpd rows a dB around it, clamped to the meter */
static int ref_row(const float *u, double db)
{
    double line, rpd;
    int r;
    eff_line(u, &line, &rpd);
    r = (int)floor((double)ZM_MID - (db - line) * rpd + 0.5);
    return r < (int)ZM_TOP ? (int)ZM_TOP : (r > 64 ? 64 : r);
}

static void zmt_reset(unsigned int fw, unsigned int screen)
{
    memset(zm_t_blob, 0xEE, sizeof zm_t_blob);
    memcpy(zm_t_blob, COVER_BLOB, COVER_BLOB_LEN);
    memset(zm_t_info, 0, sizeof zm_t_info);
    zm_t_info[5] = (uintptr_t)zm_t_blob;
    zm_t_fw = fw; zm_t_screen = screen; zm_t_posts = 0u;
    zt_tempo_host = 120; memset(zt_host, 0, sizeof zt_host);
    eff_init();
}

/* the picture the firmware would draw now, and its RLE length (0 = bad pointer) */
static int fw_raw(unsigned char *raw)
{
    const unsigned char *p = (const unsigned char *)zm_t_info[5];
    if (p != zm_t_blob && p != zm_t_blob + ZM_CAP) return 0;
    return rle_decode(p, ZM_CAP, raw) == 1024;
}

/* the engine's last frame vs the reference (by, tags from the knobs; rows from the ring snapshot) */
static int frame_matches(const float *u, int *len_out)
{
    unsigned char raw[1024], want[1024], enc[1600];
    int rows[128], x, by, nt, n;
    RefTag tags[2];
    ZmState *z = eff_zm();
    if (!fw_raw(raw)) return 0;
    for (x = 0; x < 128; x++) rows[x] = (int)zm_row(z, z->lv[(z->rh + (unsigned int)x) & 255u]);
    eff_geom(u, &by, tags, &nt);
    ref_frame(want, zm_t_blob + 2u * ZM_CAP, (int)ZM_TOP, by, rows, tags, nt);
    if (memcmp(raw, want, 1024) != 0) {
        for (x = 0; x < 1024 && raw[x] == want[x]; x++) ;
        printf("  first difference at raw byte %d (block %d, column %d): got %02X want %02X\n", x, x >> 7, x & 127, raw[x], want[x]);
        printf("  engine by %u, tags n %u/%u yb %u/%u x0 %u/%u; reference by %d, %d tags\n", z->by, z->tag[0].n, z->tag[1].n,
               z->tag[0].yb, z->tag[1].yb, z->tag[0].x0, z->tag[1].x0, by, nt);
        return 0;
    }
    n = rle_encode(want, enc);
    if (len_out) *len_out = (int)z->o;
    return n == (int)z->o && memcmp(enc, z->out, (size_t)n) == 0;
}

static void preview(const char *name)
{
    static unsigned char raw[1024], img[768 * 384];
    char path[512];
    int x, y, d, k;
    if (!fw_raw(raw)) { CHECK(0, "preview %s: no picture", name); return; }
    for (y = 0; y < 64; y++) for (x = 0; x < 128; x++) {
        unsigned char v = ((raw[(y >> 3) * 128 + x] >> (y & 7)) & 1) ? 0 : 255;
        for (d = 0; d < 36; d++) img[(y * 6 + d / 6) * 768 + x * 6 + d % 6] = v;
    }
    snprintf(path, sizeof path, "%s/%s", EFF_PREVIEW_DIR, name);
    if (!write_png(path, img, 768, 384)) {
        snprintf(path, sizeof path, "tests/_gen/%s", name);
        k = write_png(path, img, 768, 384);
        printf("(could not write to %s, wrote %s: %s)\n", EFF_PREVIEW_DIR, path, k ? "ok" : "failed");
    } else printf("wrote %s\n", path);
}

static int blk;
static void run_blocks(const float *u, int n, float amp)
{
    int b;
    for (b = 0; b < n; b++) eff_block(u, amp, blk++);
}

static void zmt_run(const float *udef)
{
    unsigned char raw[1024], want[1024], enc[1600];
    int rows[128], x, by, nt, len, maxlen = 0, i, k;
    RefTag tags[2];
    float u[EFF_NK];
    unsigned int posts, head;
    ZmState *z;

    /* ---- frame A in the cover data = the reference at the default knobs, empty history ---- */
    CHECK(COVER_BLOB_LEN == 2u * ZM_CAP + 384u, "cover data %u bytes", COVER_BLOB_LEN);
    CHECK(COVER_BLOB_LEN <= sizeof zm_t_blob, "test buffer too small");
    for (x = 0; x < 128; x++) rows[x] = 64;
    eff_geom(udef, &by, tags, &nt);
    ref_frame(want, COVER_BLOB + 2u * ZM_CAP, (int)ZM_TOP, by, rows, tags, nt);
    CHECK(rle_decode(COVER_BLOB, ZM_CAP, raw) == 1024 && memcmp(raw, want, 1024) == 0, "frame A is not the reference picture");
    for (x = 0; x < 1024 && raw[x] == want[x]; x++) ;
    if (x < 1024) printf("  frame A: first difference at raw byte %d: got %02X want %02X\n", x, raw[x], want[x]);
    len = rle_encode(want, enc);
    CHECK(memcmp(enc, COVER_BLOB, (size_t)len) == 0 && memcmp(COVER_BLOB, COVER_BLOB + ZM_CAP, ZM_CAP) == 0, "frame A/B bytes");
    printf("%s: title %u rows, frame A %d RLE bytes, cover data %u bytes\n", EFF_NAME, ZM_TOP, len, COVER_BLOB_LEN);

    /* ---- the RLE worst case: every byte in an equal pair = 3 bytes per 2 = 1536 = ZM_CAP ---- */
    for (i = 0; i < 1024; i++) raw[i] = (unsigned char)(((i >> 1) & 1) ? 0x55 : 0xAA);
    CHECK(rle_encode(raw, enc) == 1536 && 1536 <= (int)ZM_CAP, "worst case %d bytes", rle_encode(raw, enc));
    for (i = 0; i < 20000; i++) {                    /* random pictures never beat it */
        for (k = 0; k < 1024; k++) raw[k] = (unsigned char)((rand() & 3) ? (rand() & 1) : (rand() & 255));
        if (rle_encode(raw, enc) > 1536) { CHECK(0, "random picture over 1536"); break; }
    }

    /* ---- stock firmware (check failed): no posts, cover data and pointer untouched ---- */
    zmt_reset(0u, 1u);
    run_blocks(udef, 5512 * 3, 0.5f);
    CHECK(zm_t_posts == 0u && memcmp(zm_t_blob, COVER_BLOB, COVER_BLOB_LEN) == 0 && zm_t_info[5] == (uintptr_t)zm_t_blob,
          "fw check failed: %u posts or writes", zm_t_posts);

    /* ---- off screen: no posts, no writes, nothing measured ---- */
    zmt_reset(1u, 0u);
    run_blocks(udef, 5512 * 3, 0.5f);
    z = eff_zm();
    CHECK(zm_t_posts == 0u && memcmp(zm_t_blob, COVER_BLOB, COVER_BLOB_LEN) == 0 && z->head == 0u && !z->on,
          "off screen: %u posts, head %u", zm_t_posts, z->head);

    /* ---- on screen at 120 BPM (patch TEMPO): 64 columns a second, at most 15 posts a second,
     * every frame equals the reference and the reference encoder, pointer flips A/B ---- */
    zmt_reset(1u, 1u);
    run_blocks(udef, 64, 0.5f);
    z = eff_zm();
    head = z->head; posts = zm_t_posts;
    for (i = 0; i < 5512 * 4; i++) {
        uintptr_t before = zm_t_info[5];
        unsigned int p0 = zm_t_posts;
        run_blocks(udef, 1, 0.5f);
        if (zm_t_posts != p0) {
            if (zm_t_info[5] == before) { CHECK(0, "post without a pointer swap"); break; }
            if (!frame_matches(udef, &len)) { CHECK(0, "frame %u differs from the reference", zm_t_posts); break; }
            if (len > maxlen) maxlen = len;
        }
    }
    printf("120 BPM: %u columns, %u posts in 4 s\n", z->head - head, zm_t_posts - posts);
    CHECK(z->head - head >= 255u && z->head - head <= 257u, "columns in 4 s at 120 BPM: %u, want 256", z->head - head);
    CHECK(zm_t_posts - posts >= 56u && zm_t_posts - posts <= 60u, "posts in 4 s: %u, want 56..60", zm_t_posts - posts);

    /* ---- 300 BPM from the MIDISTOMP transport block: 160 columns a second, still 15 posts ---- */
    zt_host[0] = 0x5A4D5401u; zt_host[1] = 2u; zt_host[2] = 0u; zt_host[5] = 30000u;
    run_blocks(udef, 64, 0.5f);
    head = z->head; posts = zm_t_posts;
    run_blocks(udef, 5512 * 4, 0.5f);
    printf("300 BPM: %u columns, %u posts in 4 s\n", z->head - head, zm_t_posts - posts);
    CHECK(z->head - head >= 639u && z->head - head <= 641u, "columns in 4 s at 300 BPM: %u, want 640", z->head - head);
    CHECK(zm_t_posts - posts <= 60u, "posts in 4 s at 300 BPM: %u", zm_t_posts - posts);
    CHECK(frame_matches(udef, 0), "300 BPM frame differs");
    zt_host[0] = 0u;

    /* ---- silence: the history scrolls out (128 columns = 4 beats), then no more posts ---- */
    run_blocks(udef, 5512 * 9, 0.0f);
    posts = zm_t_posts;
    run_blocks(udef, 5512 * 3, 0.0f);
    CHECK(zm_t_posts == posts && z->live == 0u, "idle: %u posts in 3 s of silence", zm_t_posts - posts);
    CHECK(frame_matches(udef, 0), "idle frame differs");

    /* ---- every knob value and random histories: frames match, RLE within ZM_CAP ---- */
    srand(7);
    for (i = 0; i < 3000; i++) {
        unsigned int t;
        memcpy(u, udef, sizeof u);
        eff_randk(u);
        for (t = 0; t < 256u; t++) {
            int r = rand() % 6;                          /* levels: empty, extremes, anything in +24..-100 dB */
            z->lv[t] = (unsigned short)(r == 0 ? ZM_NOLV : r == 1 ? rand() % 3 : r == 2 ? 65534 : rand() % 2000);
        }
        if (i & 1) for (t = 0; t < 256u; t++) z->lv[t] = z->lv[t & ~1u];   /* pairs: long RLE */
        z->live = 128u; z->dirty = 1u; z->since = ZM_GAP;
        posts = zm_t_posts;
        for (k = 0; k < 40 && zm_t_posts == posts; k++) eff_block(u, 0.0f, 0);
        if (zm_t_posts == posts) { CHECK(0, "no frame after a knob change"); break; }
        if (!frame_matches(u, &len)) { CHECK(0, "random frame %d differs (u0 %g u1 %g)", i, u[0], u[1]); break; }
        if (len > maxlen) maxlen = len;
        for (k = -600; k <= 240; k += 37) {          /* the effect's line and zoom vs the reference */
            double db = k * 0.1 + 0.013;
            int got = (int)zm_row(z, zm_level((float)pow(10.0, db / 20.0))), want = ref_row(u, db);
            if (got < want - 1 || got > want + 1) { CHECK(0, "row of %.3f dB at u0 %g u2 %g: %d, want %d", db, u[0], u[2], got, want); i = 3000; break; }
        }
    }
    /* adversarial: the title in pairs too, history in pairs, so every byte sits in a 2-run */
    {
        unsigned int t;
        double line, rpd, l63, l3;
        for (t = 0; t < 384u; t++) zm_t_blob[2u * ZM_CAP + t] = (unsigned char)(((t >> 1) & 1) ? 0x5A : 0xA5);
        eff_line(udef, &line, &rpd);                     /* levels of rows 63 and ZM_TOP + 3 at the default knobs */
        l63 = (ZM_DB0 - (line - (63.0 - ZM_MID) / rpd)) * 16.0; l3 = (ZM_DB0 - (line - ((double)ZM_TOP + 3.0 - ZM_MID) / rpd)) * 16.0;
        for (t = 0; t < 256u; t++) z->lv[t] = (unsigned short)floor((((t >> 1) & 1) ? l63 : l3) + 0.5);
        z->dirty = 1u; z->since = ZM_GAP; posts = zm_t_posts;
        for (k = 0; k < 40 && zm_t_posts == posts; k++) eff_block(udef, 0.0f, 0);
        CHECK(frame_matches(udef, &len), "adversarial frame differs");
        printf("adversarial frame: %d RLE bytes (cap %u)\n", len, ZM_CAP);
        if (len > maxlen) maxlen = len;
        memcpy(zm_t_blob + 2u * ZM_CAP, COVER_BLOB + 2u * ZM_CAP, 384u);
    }
    printf("largest frame: %d RLE bytes of %u\n", maxlen, ZM_CAP);
    CHECK(maxlen <= (int)ZM_CAP, "frame over the cap");

    /* ---- leaving the screen drops the frame, coming back clears the history ---- */
    run_blocks(udef, 5512, 0.5f);
    zm_t_screen = 0u; posts = zm_t_posts;
    run_blocks(udef, 5512, 0.5f);
    CHECK(zm_t_posts - posts <= 1u && !z->on, "off screen: %u posts", zm_t_posts - posts);
    zm_t_screen = 1u;
    run_blocks(udef, 20, 0.0f);
    CHECK(z->on && z->live == 0u, "back on screen: history not cleared");
}
