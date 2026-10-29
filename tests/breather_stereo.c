/* Breather is true stereo: the dry sides keep their own sound and the reverb of one side never shows on the
 * other (own comb/allpass set). Noise only on L leaves R silent, noise only on R leaves L silent, and two
 * different noises stay apart (corr near 0) with the reverb up. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/breather/breather.c"
#define D(n) params[BREATHER_##n##_SLOT] = BREATHER_##n##_UI_DEFAULT / 100.0f
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

int main(void)
{
    int mode;
    se_init(1u << 20);
    for (mode = 0; mode < 3; mode++) {                  /* 0: L only, 1: R only, 2: different noise both sides */
        unsigned r = 12345u; long b; int j; double eL = 0, eR = 0, xc = 0;
        memset(se_m + 4096, 0, 1u << 20);
        D(TARGT); D(SHAPE); D(DEPTH); D(DIV); D(SHIFT); D(CURVE); D(VERB); D(SIZE);
        params[BREATHER_VERB_SLOT] = 1.0f; params[BREATHER_DEPTH_SLOT] = 0.0f;   /* Verb 100, no pump */
        for (b = 0; b < 2000; b++) {
            for (j = 0; j < 16; j++) { r = r * 1664525u + 1013904223u; fx[j] = ((float)(int)(r >> 9) * 2.3841858e-7f - 1.0f) * 0.4f; }
            if (mode == 0) memset(fx + 8, 0, 32);
            if (mode == 1) memset(fx, 0, 32);
            Fx_DLY_Breather(ctx);
            if (b >= 1000) for (j = 0; j < 8; j++) { eL += fx[j] * fx[j]; eR += fx[j + 8] * fx[j + 8]; xc += (double)fx[j] * fx[j + 8]; }
        }
        xc /= sqrt(eL * eR) + 1e-30;
        printf("mode %d: energy L %.3f R %.3f corr %.4f\n", mode, eL, eR, xc);
        CHECK(mode != 0 || (eL > 1.0 && eR < 1e-9), "L only leaked to R");
        CHECK(mode != 1 || (eR > 1.0 && eL < 1e-9), "R only leaked to L");
        CHECK(mode != 2 || (eL > 1.0 && eR > 1.0 && fabs(xc) < 0.1), "L and R not apart");
    }
    printf("state %u bytes\n", (unsigned)sizeof(PuState));
    CHECK(sizeof(PuState) < 705536u, "state too big");
    return fails;
}
