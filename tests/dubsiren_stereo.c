/* DubSiren: the input passes through in stereo (L stays L, R stays R, never processed); the siren and its
 * echo are one mono sound placed by Pan with equal power: centre = same on both sides, hard L = nothing in R,
 * hard R = nothing in L, and left^2 + right^2 stays constant. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/dubsiren/dubsiren.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

int main(void)
{
    int pans[5] = {0, 25, 50, 75, 100}, q;
    se_init(1u << 19);
    for (q = 0; q < 5; q++) {
        long b, k = 0; int j; double eL = 0, eR = 0, inErr = 0;
        memset(se_m + 4096, 0, 1u << 19);
        params[0] = 1.0f;                                  /* footswitch on: Trig Hold sounds */
        params[DUBSIREN_TRIG_SLOT] = 0.0f; params[DUBSIREN_MODE_SLOT] = 0.01f; params[DUBSIREN_PITCH_SLOT] = 0.48f;
        params[DUBSIREN_RATE_SLOT] = 0.67f; params[DUBSIREN_DEPTH_SLOT] = 0.34f; params[DUBSIREN_VOL_SLOT] = 0.5f;
        params[DUBSIREN_TIME_SLOT] = 0.4f; params[DUBSIREN_FDBK_SLOT] = 0.5f;
        params[DUBSIREN_PAN_SLOT] = (float)pans[q] / 100.0f;
        for (b = 0; b < 3000; b++) {
            float inL[8], inR[8];
            for (j = 0; j < 8; j++, k++) {
                inL[j] = fx[j] = 0.3f * sinf(0.05f * (float)k);
                inR[j] = fx[j + 8] = 0.2f * sinf(0.21f * (float)k + 0.5f);
            }
            Fx_DLY_DubSiren(ctx);
            if (b < 500) continue;
            for (j = 0; j < 8; j++) {
                double dL = fx[j] - inL[j], dR = fx[j + 8] - inR[j];
                eL += dL * dL; eR += dR * dR;
            }
        }
        printf("Pan %3d: siren energy L %.3f R %.3f  total %.3f\n", pans[q], eL, eR, eL + eR);
        if (q == 0) CHECK(eL > 1.0 && eR < 1e-9, "hard L leaks to R");
        if (q == 4) CHECK(eR > 1.0 && eL < 1e-9, "hard R leaks to L");
        if (q == 2) CHECK(eL > 1.0 && fabs(eL - eR) < 1e-6 * eL, "centre not equal");
        { static double tot; if (q == 0) tot = eL + eR; else CHECK(fabs(eL + eR - tot) < 0.02 * tot, "pan %d not equal power", pans[q]); }
        (void)inErr;
    }
    return fails;
}
