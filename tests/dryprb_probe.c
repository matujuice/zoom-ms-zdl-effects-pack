/* DryPrb host test: SEND writes the marker where it should and leaves the audio alone,
 * READ finds it only when present, tone/blip behave, bypass and OFF pass through. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mman.h>
#define DRYPRB_HOST_TEST
#include "../src/probes/dryprb/dryprb.c"
#include "dryprb_params.h"

static float fx[16], dry[16];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static DpState st;
static void reset(void) { memset(&st, 0, sizeof st); st.magic = DP_MAGIC; }
static void blk(unsigned role, unsigned where, unsigned shape, float amp, float lvl, float in)
{
    for (int j = 0; j < 8; j++) { fx[j] = in; fx[j + 8] = 0.0f; }
    dp_block(&st, role, where, shape, amp, lvl, fx, dry);
}

int main(void)
{
    char lab[8];
    /* knob units: raw is screen/100 */
    CHECK(dp_ui(0.02f, 0, 2) == 2, "dp_ui screen 2");
    CHECK(dp_ui(1.0f, 50, 100) == 100, "dp_ui screen 100");

    /* OFF: passthrough, buffers untouched */
    reset(); memset(dry, 0, sizeof dry);
    blk(0, 0, 0, 0.5f, 0.5f, 0.3f);
    for (int j = 0; j < 8; j++) CHECK(fx[j] == 0.3f && fx[j + 8] == 0.3f, "OFF passthrough");
    for (int j = 0; j < 16; j++) CHECK(dry[j] == 0.0f, "OFF leaves Dry alone");

    /* SEND DRYR DC: Dry R = amp, Dry L and audio untouched */
    reset(); memset(dry, 0, sizeof dry);
    blk(1, 0, 0, 0.5f, 0.5f, 0.3f);
    for (int j = 0; j < 8; j++) {
        CHECK(dry[j + 8] == 0.5f, "SEND DRYR marker");
        CHECK(dry[j] == 0.0f, "SEND DRYR leaves Dry L");
        CHECK(fx[j] == 0.3f && fx[j + 8] == 0.3f, "SEND leaves audio");
    }
    /* SEND DRYL */
    reset(); memset(dry, 0, sizeof dry);
    blk(1, 1, 0, 0.5f, 0.5f, 0.3f);
    CHECK(dry[0] == 0.5f && dry[8] == 0.0f, "SEND DRYL");
    /* SEND FXR overwrites Fx R only */
    reset(); memset(dry, 0, sizeof dry);
    blk(1, 2, 0, 0.5f, 0.5f, 0.3f);
    CHECK(fx[8] == 0.5f && fx[0] == 0.3f && dry[8] == 0.0f, "SEND FXR");
    /* ALT alternates sign */
    reset(); memset(dry, 0, sizeof dry);
    blk(1, 0, 1, 0.5f, 0.5f, 0.0f);
    CHECK(dry[8] == 0.5f && dry[9] == -0.5f, "ALT sign");
    /* PULSE: on first half, off second half */
    reset(); memset(dry, 0, sizeof dry);
    blk(1, 0, 2, 0.5f, 0.5f, 0.0f);
    CHECK(dry[8] == 0.5f, "PULSE on at start");
    st.ph = 1u << DP_PULSE_SH;
    blk(1, 0, 2, 0.5f, 0.5f, 0.0f);
    CHECK(dry[8] == 0.0f, "PULSE off in second half");

    /* READ: no marker -> blip (86 Hz) only in the first 40 ms, no 1378 Hz tone */
    reset(); memset(dry, 0, sizeof dry);
    double e_first = 0, e_late = 0;
    for (int b = 0; b < 44100 / 8; b++) {
        blk(2, 0, 0, 0.0f, 1.0f, 0.0f);
        for (int j = 0; j < 8; j++) { double v = fx[j]; if (b * 8 + j < 1764) e_first += v * v; else e_late += v * v; }
        for (int j = 0; j < 8; j++) CHECK(fx[j] == fx[j + 8], "READ copies L to R");
    }
    CHECK(e_first > 10.0 && e_late == 0.0, "READ not found: blip then silence (%g %g)", e_first, e_late);

    /* READ: marker present -> steady tone */
    reset(); for (int j = 0; j < 16; j++) dry[j] = (j >= 8) ? 0.5f : 0.0f;
    double e_all = 0;
    for (int b = 0; b < 4410 / 8; b++) { blk(2, 0, 0, 0.0f, 1.0f, 0.0f); for (int j = 0; j < 8; j++) e_all += fx[j] * fx[j]; }
    CHECK(e_all > 400.0, "READ found: steady tone (%g)", e_all);

    /* READ: marker below threshold is not found */
    reset(); for (int j = 8; j < 16; j++) dry[j] = 0.001f;
    e_all = 0;
    for (int b = 0; b < 1000 / 8; b++) { blk(2, 0, 0, 0.0f, 1.0f, 0.0f); for (int j = 0; j < 8; j++) e_all += fx[j] * fx[j]; }
    CHECK(e_all < 400.0, "READ small marker is not 'found'");

    /* READ adds to the input and respects Level 0 */
    reset(); for (int j = 8; j < 16; j++) dry[j] = 0.5f;
    blk(2, 0, 0, 0.0f, 0.0f, 0.3f);
    for (int j = 0; j < 8; j++) CHECK(fx[j] == 0.3f, "READ at Level 0 passes input");

    /* SEND then READ through the same buffer, same where */
    for (unsigned w = 0; w < 3; w++) {
        reset(); memset(dry, 0, sizeof dry);
        for (int j = 0; j < 8; j++) { fx[j] = 0.0f; fx[j + 8] = 0.0f; }
        dp_block(&st, 1u, w, 0u, 0.5f, 0.5f, fx, dry);
        /* the next slot sees the same Dry, and the Fx R the sender left */
        dp_block(&st, 2u, w, 0u, 0.5f, 0.5f, fx, dry);
        double e = 0; for (int j = 0; j < 8; j++) e += fx[j] * fx[j];
        CHECK(e > 0.0, "SEND then READ where=%u: found", w);
    }

    /* labels */
    dp_text(lab, 'a', 0, 0, 0, 0);
    CHECK(ZDL_GetLabel_0(2, lab) == 4 && lab[0] == 'R', "label 0");
    CHECK(ZDL_GetLabel_1(2, lab) == 3 && lab[0] == 'F', "label 1");
    CHECK(ZDL_GetLabel_2(2, lab) == 5 && lab[0] == 'P', "label 2");

    printf(fails ? "dryprb: %d failure(s)\n" : "dryprb: ok\n", fails);
    return fails != 0;
}
