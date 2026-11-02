/* Gooey meter on the cover (src/custom/common/zdl_meter.h): every frame the firmware would draw
 * equals an independent render of the approved v16 look and the reference RLE encoder, stays in
 * ZM_CAP (worst case 1536 bytes), the pointer flips A/B, at most 15 redraws a second, columns at
 * 32 a beat (patch TEMPO and MIDISTOMP clock), nothing on stock firmware or off screen, the level
 * is the output (bypass too), and preview PNGs of a few histories. */
#define GOOEY_HOST_TEST
#define ZM_EFFECT_C "../src/custom/gooey/gooey.c"
#include "zm_test.h"

#ifndef GO_PREVIEW_DIR
#define GO_PREVIEW_DIR "C:/Users/lucam/OneDrive/Documenti/CLAUDE/gooey-meter-preview"
#endif
static const char *EFF_NAME = "Gooey";
static const char *EFF_PREVIEW_DIR = GO_PREVIEW_DIR;

static GoState S;
static void eff_init(void) { memset(&S, 0x5A, sizeof S); go_init(&S); }
static ZmState *eff_zm(void) { return &S.zm; }

static float last_out;                               /* loudest output sample of the last block */
static void eff_block(const float *u, float amp, int b)
{
    float buf[16];
    GoParams P;
    unsigned int on;
    int i;
    for (i = 0; i < 8; i++) {
        float t = (float)(b * 8 + i), a = amp;
        if (amp < 0.0f) { float ph = fmodf(t, 22050.0f); a = -amp * expf(-ph / 3000.0f); }
        buf[i] = a * sinf(0.05f * t); buf[i + 8] = buf[i];
    }
    go_prepare(&P, &S, u);
    on = zm_begin(&S.zm, (void *)&S);
    go_process(&S, &P, buf);
    if (on) go_meter(&S, u);
    last_out = 0.0f;
    for (i = 0; i < 16; i++) last_out = fabsf(buf[i]) > last_out ? fabsf(buf[i]) : last_out;
}

static void eff_geom(const float *u, int *by, RefTag *tags, int *nt)
{
    int thr = (int)u[0];
    *by = (int)ZM_MID;
    snprintf(tags[0].text, sizeof tags[0].text, "THRSH %.1f", (thr - 400) / 10.0);
    tags[0].xr = 126; tags[0].yb = *by - 2; *nt = 1;
}
static void eff_randk(float *u) { u[0] = (float)(rand() % 401); u[2] = (float)(rand() % 16); }
static void eff_line(const float *u, double *line, double *rpd) { *line = (u[0] - 400.0) / 10.0; *rpd = (u[2] + 1.0) / 2.0; }

int main(void)
{
    static const float udef[EFF_NK] = { 400.0f, 2.0f, 1.0f, 4.0f, 4.0f, 1.0f, 100.0f, 0.0f };
    float u[EFF_NK], pk;
    int g, b, r, w;

    zmt_run(udef);

    /* the shown level is the output after the gain; in bypass (Mix 0) the dry signal */
    for (g = 0; g < 2; g++) {
        zmt_reset(1u, 1u);
        memcpy(u, udef, sizeof u); u[0] = 200.0f;
        if (g) u[6] = 0.0f;
        for (b = 0; b < 5512; b++) eff_block(u, 0.5f, b);
        pk = 0.0f;
        for (b = 5512; b < 5512 + 2000; b++) { eff_block(u, 0.5f, b); pk = last_out > pk ? last_out : pk; }
        r = (int)zm_row(&S.zm, S.zm.lv[(S.zm.head - 1u) & 255u]);
        w = ref_row(u, 20.0 * log10(pk));
        CHECK(r >= w - 1 && r <= w + 1, "%s: row %d, output peak %.3f wants %d", g ? "Mix 0" : "Thrsh -20", r, pk, w);
    }

    for (g = 0; g < 4; g++) {
        static const char *nm[4] = { "gooey-meter-0-idle.png", "gooey-meter-1-drum-hits.png",
                                     "gooey-meter-2-thrsh-20.png", "gooey-meter-3-thrsh-32.png" };
        static const float amp[4] = { 0.0f, -0.9f, 0.4f, 0.9f };
        zmt_reset(1u, 1u);
        memcpy(u, udef, sizeof u);
        if (g == 2) u[0] = 200.0f;
        if (g == 3) { u[0] = 80.0f; u[2] = 3.0f; }
        run_blocks(u, 5512 * 9, amp[g]);
        CHECK(frame_matches(u, 0), "preview %d frame differs", g);
        preview(nm[g]);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
