/* Sweep is true stereo: in every engine L and R keep their own sound. A tone only on L leaves R silent,
 * a tone only on R leaves L silent (the R-jack-only case), and two different tones stay different. */
#include "sweep_common.h"

static double rms(float *p, long n) { double a = 0; long i; for (i = 0; i < n; i++) a += (double)p[i] * p[i]; return sqrt(a / (double)n); }

int main(void)
{
    int type, mode;
    for (type = 0; type < 8; type++) {
        for (mode = 0; mode < 3; mode++) {            /* 0: L only, 1: R only, 2: different tones both sides */
            static float oL[8 * 3000], oR[8 * 3000]; long b, k = 0; int j; double xc = 0, eL, eR;
            setup(); SET(TYPE, type); SET(RATE, 60); SET(DEPTH, 60); SET(RESO, 50); SET(MIX, 100);
            for (b = 0; b < 3000; b++) {
                for (j = 0; j < 8; j++, k++) {
                    float a = 0.4f * sinf(0.07f * (float)k), c = 0.3f * sinf(0.19f * (float)k + 1.0f);
                    fx[j] = (mode == 1) ? 0.0f : a; fx[j + 8] = (mode == 0) ? 0.0f : c;
                }
                Fx_DLY_Sweep(ctx);
                memcpy(oL + b * 8, fx, 32); memcpy(oR + b * 8, fx + 8, 32);
            }
            eL = rms(oL + 8 * 500, 8 * 2500); eR = rms(oR + 8 * 500, 8 * 2500);
            for (k = 8 * 500; k < 8 * 3000; k++) xc += (double)oL[k] * oR[k];
            xc /= (8.0 * 2500.0) * (eL * eR + 1e-30);
            printf("type %d mode %d: rms L %.4f R %.4f corr %.3f\n", type, mode, eL, eR, xc);
            CHECK(mode != 0 || (eL > 0.01 && eR < 1e-6), "L only leaked to R (type %d)", type);
            CHECK(mode != 1 || (eR > 0.01 && eL < 1e-6), "R only leaked to L (type %d)", type);
            CHECK(mode != 2 || (eL > 0.01 && eR > 0.01 && fabs(xc) < 0.5), "L and R not apart (type %d)", type);
        }
    }
    return fails;
}
