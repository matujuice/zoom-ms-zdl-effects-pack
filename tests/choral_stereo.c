/* Choral stereo: the tracker hears (L + R) / 2 (a note on R only still makes the choir sing), Width 0 gives the
 * same wet on both sides, Width 100 spreads the sections of the choir (L and R differ). Mix 100 = wet only. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/formant/formant.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SR 44100

static void knobs(int choir, int size, int width, int mix)
{
    params[FORMANT_CHOIR_SLOT] = choir / 100.f; params[FORMANT_SIZE_SLOT] = size / 100.f; params[FORMANT_CHORD_SLOT] = 0;
    params[FORMANT_SING_SLOT] = 6 / 100.f; params[FORMANT_PACE_SLOT] = 4 / 100.f; params[FORMANT_FEEL_SLOT] = 40 / 100.f;
    params[FORMANT_GLIDE_SLOT] = 20 / 100.f; params[FORMANT_MIX_SLOT] = mix / 100.f; params[FORMANT_WIDTH_SLOT] = width / 100.f;
}

/* 1.2 s of a 220 Hz saw; mode 0 on both sides, 1 on L only, 2 on R only; returns energies and the largest L-R difference */
static void run(int mode, double *eL, double *eR, double *xc, double *maxd)
{
    long b; int j; double ph = 0; *eL = *eR = *xc = *maxd = 0;
    for (b = 0; b < SR * 12 / 10 / 8; b++) {
        for (j = 0; j < 8; j++) { float x; ph += 220.0 / SR; ph -= floor(ph); x = 0.4f * (float)(2 * ph - 1);
            fx[j] = (mode == 2) ? 0.0f : x; fx[j + 8] = (mode == 1) ? 0.0f : x; }
        Fx_DLY_Formant(ctx);
        if (b * 8 > SR / 2) for (j = 0; j < 8; j++) {
            *eL += (double)fx[j] * fx[j]; *eR += (double)fx[j + 8] * fx[j + 8]; *xc += (double)fx[j] * fx[j + 8];
            if (fabs(fx[j] - fx[j + 8]) > *maxd) *maxd = fabs(fx[j] - fx[j + 8]);
        }
    }
    *xc /= sqrt(*eL * *eR) + 1e-30;
}

int main(void)
{
    double eL, eR, xc, md, eLo, eRo; int w;
    se_init(1u << 20);
    ctx[4] = (unsigned)(uintptr_t)(se_m + 1024);
    for (w = 0; w < 2; w++) {
        memset(se_m + 4096, 0, 1u << 20);
        knobs(14, 100, w ? 100 : 0, 100);            /* ALL four sections, 6 singers */
        run(0, &eL, &eR, &xc, &md);
        printf("Width %3d: energy L %.2f R %.2f corr %.3f max |L-R| %.4f\n", w * 100, eL, eR, xc, md);
        if (!w) CHECK(eL > 1.0 && md < 1e-4, "Width 0: L and R not the same");
        else    CHECK(eL > 1.0 && eR > 1.0 && md > 0.01 && xc < 0.99, "Width 100: the choir is not spread");
    }
    /* the tracker hears (L + R) / 2: a note on one side only still gets sung */
    memset(se_m + 4096, 0, 1u << 20); knobs(0, 20, 0, 100); run(1, &eL, &eR, &xc, &md);
    memset(se_m + 4096, 0, 1u << 20); knobs(0, 20, 0, 100); run(2, &eLo, &eRo, &xc, &md);
    printf("note on L only: choir energy %.2f / %.2f ; on R only: %.2f / %.2f\n", eL, eR, eLo, eRo);
    CHECK(eL > 1.0 && eRo > 1.0 && eLo > 1.0 && eR > 1.0, "a note on one side only is not sung");
    CHECK(fabs(eL - eRo) < 0.2 * eL, "the choir level depends on the side of the note");
    printf("state %u bytes\n", (unsigned)sizeof(ChState));
    CHECK(sizeof(ChState) < 705536u, "state too big");
    return fails;
}
