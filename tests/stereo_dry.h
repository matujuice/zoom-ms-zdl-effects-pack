/* Shared check for the "stereo dry, mono effect" ports (WaveFold, DirtBox, DualShft): the effect hears (L + R) / 2,
 * the dry keeps L and R apart, and both sides get the same wet.
 *   Mix 0   : output == input on both sides (L and R untouched, different from each other)
 *   Mix 50  : (out - in) is the same wet signal on L and on R, and the sides are still apart
 *   Mix 100 : L == R (all wet, mono)
 * Needs stereo_entry.h and the effect's .c included first; `call` is the audio function. */
#include <stdio.h>
#include <math.h>
static int sd_fails = 0;
#define SD_CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); sd_fails++; } } while (0)

static void sd_test(const char *name, void (*call)(unsigned int *), int mix_slot, float amp)
{
    int mixes[3] = {0, 50, 100}, m;
    for (m = 0; m < 3; m++) {
        long b, k = 0; int j;
        double eW = 0, eL = 0, eR = 0, xc = 0, errIn = 0, errLR = 0, errWet = 0;
        params[mix_slot] = (float)mixes[m] / 100.0f;
        for (b = 0; b < 4000; b++) {
            float inL[8], inR[8];
            for (j = 0; j < 8; j++, k++) {
                inL[j] = fx[j] = amp * sinf(0.045f * (float)k);
                inR[j] = fx[j + 8] = 0.6f * amp * sinf(0.13f * (float)k + 1.0f);
            }
            call(ctx);
            if (b < 1000) continue;
            for (j = 0; j < 8; j++) {
                double dL = fx[j] - inL[j], dR = fx[j + 8] - inR[j];
                if (fabs(dL) > errIn && mixes[m] == 0) errIn = fabs(dL);
                if (fabs(fx[j + 8] - inR[j]) > errIn && mixes[m] == 0) errIn = fabs(fx[j + 8] - inR[j]);
                if (fabs(fx[j] - fx[j + 8]) > errLR) errLR = fabs(fx[j] - fx[j + 8]);
                if (fabs(dL - dR) > errWet) errWet = fabs(dL - dR);
                eW += dL * dL; eL += (double)fx[j] * fx[j]; eR += (double)fx[j + 8] * fx[j + 8];
                xc += (double)fx[j] * fx[j + 8];
            }
        }
        xc /= sqrt(eL * eR) + 1e-30;
        printf("%s Mix %3d: untouched error %.2e, L-R max %.2e, wet differs L/R %.2e, wet rms %.3f, corr %.3f\n",
               name, mixes[m], errIn, errLR, errWet, sqrt(eW / (8.0 * 3000.0)), xc);
        if (mixes[m] == 0)   SD_CHECK(errIn < 1e-5, "%s: Mix 0 changed the dry", name);
        if (mixes[m] == 50)  SD_CHECK(errWet < 1e-4 && eW > 1.0 && fabs(xc) < 0.9, "%s: Mix 50 not stereo dry + shared wet", name);
        if (mixes[m] == 100) SD_CHECK(errLR < 1e-4 && eL > 1.0, "%s: Mix 100 not mono wet", name);
    }
}
