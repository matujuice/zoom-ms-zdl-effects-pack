/* LOUDER meter on the cover (src/custom/common/zdl_meter.h): every frame the firmware would draw
 * equals an independent render of the approved v12 look and the reference RLE encoder, stays in
 * ZM_CAP (worst case 1536 bytes), the pointer flips A/B, at most 15 redraws a second, columns at
 * 8 a beat (patch TEMPO and MIDISTOMP clock), nothing on stock firmware or off screen, and
 * preview PNGs of a few histories. */
#define LOUDER_HOST_TEST
#define ZM_EFFECT_C "../src/custom/louder/louder.c"
#include "zm_test.h"
unsigned int lo_t_clips;

#ifndef LO_PREVIEW_DIR
#define LO_PREVIEW_DIR "C:/Users/lucam/OneDrive/Documenti/CLAUDE/louder-meter-preview"
#endif
static const char *EFF_NAME = "LOUDER";
static const char *EFF_PREVIEW_DIR = LO_PREVIEW_DIR;

static LoState S;
static void eff_init(void) { memset(&S, 0x5A, sizeof S); lo_init(&S); }
static ZmState *eff_zm(void) { return &S.zm; }

/* the entry's order: prepare, zm_begin, process, meter. amp < 0: drum hits */
static void eff_block(const float *u, float amp, int b)
{
    float buf[16];
    LoParams P;
    unsigned int on;
    int i;
    for (i = 0; i < 8; i++) {
        float t = (float)(b * 8 + i), a = amp;
        if (amp < 0.0f) { float ph = fmodf(t, 22050.0f); a = -amp * expf(-ph / 3000.0f); }
        buf[i] = a * sinf(0.05f * t); buf[i + 8] = buf[i];
    }
    lo_prepare(&P, &S, u);
    on = zm_begin(&S.zm, (void *)&S);
    lo_process(&S, &P, buf);
    if (on) lo_meter(&S, u);
}

static void eff_geom(const float *u, int *by, RefTag *tags, int *nt)
{
    int gain = (int)u[0], ceil = (int)u[1];
    *by = py_round((int)ZM_TOP + (3.0 - (ceil - 60) / 10.0) * 3.0);
    *nt = 0;
    snprintf(tags[*nt].text, sizeof tags[0].text, "CEIL %.1f", (ceil - 60) / 10.0);
    tags[*nt].xr = 126; tags[*nt].yb = *by - 2 > (int)ZM_TOP + 7 ? *by - 2 : (int)ZM_TOP + 7; (*nt)++;
    snprintf(tags[*nt].text, sizeof tags[0].text, "GAIN %+.1f", gain / 10.0);
    tags[*nt].xr = 126; tags[*nt].yb = 62; (*nt)++;
}
static void eff_randk(float *u) { u[0] = (float)(rand() % 181); u[1] = (float)(rand() % 61); }
static int eff_row(double db)
{
    double r = db >= -9.0 ? (int)ZM_TOP + (3.0 - db) * 3 : (int)ZM_TOP + 36 + (-9.0 - db) / 4;
    return (int)floor(r + 0.5);
}
static unsigned int eff_row_c(float a) { return lo_row(a); }

int main(void)
{
    static const float udef[EFF_NK] = { 0.0f, 59.0f, 0.0f, 0.0f, 100.0f };
    float u[EFF_NK];
    int g, b;

    zmt_run(udef);

    /* the shown level is the input after Gain: +6 dB of Gain lifts a -12 dB sine by 6 dB */
    zmt_reset(1u, 1u);
    memcpy(u, udef, sizeof u); u[0] = 60.0f;
    run_blocks(u, 5512, 0.25f);
    b = (int)S.zm.wy[(S.zm.head - 1u) & 255u];
    CHECK(b == eff_row(-6.0) || b == eff_row(-6.0) + 1, "row of a -12 dB sine at +6 dB Gain: %d, want %d", b, eff_row(-6.0));

    /* previews: a full history of a few shapes at a few knob settings */
    for (g = 0; g < 4; g++) {
        static const char *nm[4] = { "louder-meter-0-idle.png", "louder-meter-1-drum-hits.png",
                                      "louder-meter-2-loud-gain-6dB.png", "louder-meter-3-ceil-minus3.png" };
        static const float amp[4] = { 0.0f, -0.9f, 0.6f, 0.9f };
        zmt_reset(1u, 1u);
        memcpy(u, udef, sizeof u);
        if (g == 2) u[0] = 60.0f;
        if (g == 3) { u[1] = 30.0f; u[0] = 120.0f; }
        run_blocks(u, 5512 * 9, amp[g]);
        CHECK(frame_matches(u, 0), "preview %d frame differs", g);
        preview(nm[g]);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
