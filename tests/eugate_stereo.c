/* EuGate on both sides: a different tone on L and R stays apart, both are gated by the same pattern,
 * and the pattern is the same on both. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/eugate/eugate.c"
#define D(n) params[EUGATE_##n##_SLOT] = EUGATE_##n##_UI_DEFAULT / 100.0f

int main(void)
{
    int fails = 0, b, i, gl_on = 0, gr_on = 0, mism = 0; double eL = 0, eR = 0, xc = 0; long n = 0;
    se_init(65536);
    D(NOTES); D(STEPS); D(SHIFT); D(SWING); D(RESET); D(GAP); D(SOFT); D(MIX);
    params[EUGATE_MIX_SLOT] = 1.0f;                                  /* wet only */
    for (b = 0; b < 44100 * 4 / 8; b++) {
        for (i = 0; i < 8; i++) {
            long k = (long)b * 8 + i;
            fx[i] = 0.4f * sinf(0.05f * (float)k);                   /* L: one tone */
            fx[i + 8] = (k & 64) ? 0.3f : -0.3f;                     /* R: a square, nothing like L */
        }
        { float inL[8], inR[8]; memcpy(inL, fx, 32); memcpy(inR, fx + 8, 32);
          Fx_DLY_EuGate(ctx);
          for (i = 0; i < 8; i++) {
              float gl = (fabsf(inL[i]) > 0.01f) ? fx[i] / inL[i] : -1.0f, gr = fx[i + 8] / inR[i];
              if (gl >= 0.0f && fabsf(gl - gr) > 1e-3f) mism++;      /* the same gain on both sides */
              if (gl > 0.5f) gl_on++;
              if (gr > 0.5f) gr_on++;
              eL += fx[i] * fx[i]; eR += fx[i + 8] * fx[i + 8]; xc += (double)fx[i] * fx[i + 8]; n++;
          } }
    }
    printf("gain differs between L and R in %d samples; gate open L %d R %d samples; corr %.3f\n",
           mism, gl_on, gr_on, xc / sqrt(eL * eR));
    if (mism) { puts("FAIL: L and R gated differently"); fails++; }
    if (gl_on < 1000 || fabs(xc / sqrt(eL * eR)) > 0.3) { puts("FAIL: gate never opened or L/R not apart"); fails++; }
    return fails;
}
