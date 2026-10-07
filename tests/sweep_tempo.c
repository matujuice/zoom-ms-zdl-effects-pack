/* Sweep: Tempo knob (labels, FOLLW, twin copy), LFO speed, bar-locked flips, switched off */
#include "sweep_common.h"

static unsigned int rng = 12345u;
static int changed;
static void run(long blocks)
{
    long b; int j; float in[8];
    for (b = 0; b < blocks; b++) {
        for (j = 0; j < 8; j++) { rng = rng * 1664525u + 1013904223u; fx[j] = 0.3f * (float)(int)(rng >> 9) * 1.1920929e-7f - 0.3f; fx[j + 8] = fx[j]; in[j] = fx[j]; }
        Fx_DLY_Sweep(ctx);
        for (j = 0; j < 8; j++) if (fx[j] != in[j]) changed++;
    }
}
static float step(void) { float a = st->ph; run(1); return st->ph - a; }
static void tempo(float t) { params[SWEEP_TEMPO_SLOT] = t; }

int main(void)
{
    char b[8];
    unsigned tv[8] = {0, 39, 40, 120, 240, 241, 321, 441};
    const char *tw[8] = {"FOLLW", "FOLLW", "40", "120", "240", "40", "120", "240"};
    unsigned rv[16] = {0, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 114, 115};
    const char *rw[16] = {".05Hz", "8.0Hz", "8BAR", "7BAR", "6BAR", "5BAR", "4BAR", "3BAR", "2BAR", "1.5B", "1BAR", "3/4", "1/2", "1/4", "1/16", "1/32"};
    unsigned yv[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const char *yw[8] = {"PH 4", "PH 8", "FL +", "FL -", "LP", "BP", "HP", "NTCH"};
    const char *sw[6] = {"TRI", "SINE", "RISE", "FALL", "SQR", "RAND"};
    float d120, d240, d321, d441, dfol, before, after;
    int i;

    for (i = 0; i < 8; i++) { ZDL_GetLabel_7(tv[i], b); printf("T%u=%s ", tv[i], b); CHECK(strcmp(b, tw[i]) == 0, "tempo label %u: %s want %s", tv[i], b, tw[i]); }
    for (i = 0; i < 16; i++) { ZDL_GetLabel_1(rv[i], b); printf("R%u=%s ", rv[i], b); CHECK(strcmp(b, rw[i]) == 0, "rate label %u: %s want %s", rv[i], b, rw[i]); }
    for (i = 0; i < 8; i++) { ZDL_GetLabel_0(yv[i], b); CHECK(strcmp(b, yw[i]) == 0, "type label %u: %s want %s", yv[i], b, yw[i]); }
    for (i = 0; i < 6; i++) { ZDL_GetLabel_5((unsigned)i, b); CHECK(strcmp(b, sw[i]) == 0, "shape label %d: %s want %s", i, b, sw[i]); }
    for (i = 101; i <= 115; i++) { ZDL_GetLabel_1((unsigned)i, b); CHECK(strlen(b) <= 5, "rate label %d too long", i); }
    for (i = 0; i <= 100; i++) { ZDL_GetLabel_1((unsigned)i, b); CHECK(strlen(b) <= 5 && b[0], "free rate label %d: '%s'", i, b); }
    printf("\n");

    /* LFO speed follows the BPM: 8 bars, so the phase does not wrap in the test */
    setup(); SET(RATE, 101); tempo(1.20f); run(3000);
    d120 = step();
    tempo(0.0f); run(50); dfol = step();
    tempo(2.40f); run(50); d240 = step();
    tempo(4.41f); run(50); d441 = step();
    tempo(3.21f); run(50); d321 = step();
    printf("phase per block: 120 %.6g, FOLLW %.6g, 240 %.6g, twin 441 %.6g, twin 321 %.6g\n", d120, dfol, d240, d441, d321);
    CHECK(d120 > 0.0f && fabsf(dfol - d120) < 2e-8f, "FOLLW without a tag is not 120");
    CHECK(fabsf(d240 - 2.0f * d120) < 1e-8f, "240 BPM is not twice 120");
    CHECK(fabsf(d441 - d240) < 2e-8f && fabsf(d321 - d120) < 2e-8f, "twin copies run at another speed");
    CHECK(fabsf(d120 - 120.0f * SW_INC_PER_BPM * 0.03125f * 8.0f) < 2e-8f, "8 bars at 120 BPM is not 16 s");

    /* the synced Rates go faster with every step up: 8 bars (101) .. 1/32 (115) */
    {
        float prev = 0.0f; int r;
        setup(); tempo(1.20f);
        for (r = 101; r <= 115; r++) {
            float d; SET(RATE, r); run(50); d = step();
            CHECK(d > prev, "Rate %d is not faster than %d (%g <= %g)", r, r - 1, d, prev);
            prev = d;
        }
        printf("1/32 sweeps %.1f Hz at 120 BPM\n", prev * 44100.0f / 8.0f);
        CHECK(prev * 44100.0f / 8.0f > 15.0f && prev * 44100.0f / 8.0f < 17.0f, "1/32 at 120 BPM is not 16 Hz");
    }

    /* flips are bars: the 8 bar sweep keeps its place, (4 x bars mod 32) / 32 */
    setup(); SET(RATE, 101); tempo(1.20f); run(3000);
    for (i = 1; i <= 9; i++) {
        float want = (float)((4 * i) % 32) / 32.0f;
        tempo((i & 1) ? 3.21f : 1.20f); run(1);
        printf("flip %d: phase %.4f (want %.4f)\n", i, st->ph, want);
        CHECK(fabsf(st->ph - want - 8.0f * d120) < 1e-5f || fabsf(st->ph - want) < 1e-5f, "flip %d: phase %.4f, want %.4f", i, st->ph, want);
        run(500);
    }
    /* 1/4 note: every flip lands on 0 (4 beats = 4 whole sweeps) */
    setup(); SET(RATE, 112); tempo(1.20f); run(3000);
    before = st->ph; tempo(3.21f); run(1); after = st->ph;
    CHECK(after <= 2.0f * 8.0f * 120.0f * SW_INC_PER_BPM, "1/4 flip: phase %.4f -> %.4f", before, after);
    /* 3/4 (3 beats): a bar is 1 and 1/3 sweeps, so the phase is 1/3 */
    setup(); SET(RATE, 110); tempo(1.20f); run(3000);
    tempo(3.21f); run(1);
    CHECK(fabsf(st->ph - 0.3333f) < 0.003f, "3/4 flip: phase %.4f want 0.333", st->ph);
    /* free rate: a flip restarts the sweep */
    setup(); SET(RATE, 60); tempo(1.20f); run(3000);
    before = st->ph; tempo(3.21f); run(1); after = st->ph;
    CHECK(before > 0.0f && after < before && after < 0.01f, "free flip: phase %.4f -> %.4f", before, after);
    /* a plain tempo change restarts nothing */
    setup(); SET(RATE, 101); tempo(1.20f); run(3000);
    before = st->ph; tempo(1.21f); run(1);
    CHECK(st->ph > before, "plain tempo change moved the phase back");

    /* switched off: input untouched, the sweep runs on and follows a flip */
    setup(); SET(RATE, 101); tempo(1.20f); run(3000);
    params[0] = 0.0f; changed = 0; run(2000);
    d120 = step(); if (d120 < 0.0f) d120 = step();
    CHECK(changed == 0, "switched off but the input changed (%d samples)", changed);
    CHECK(fabsf(d120 - 120.0f * SW_INC_PER_BPM * 0.03125f * 8.0f) < 2e-8f, "off: sweep does not run");
    before = st->ph; tempo(3.21f); run(1);
    CHECK(fabsf(st->ph - 0.125f) < 1e-5f, "flip while off: phase %.4f want 0.125 (was %.4f)", st->ph, before);
    params[0] = 1.0f; run(100);

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
