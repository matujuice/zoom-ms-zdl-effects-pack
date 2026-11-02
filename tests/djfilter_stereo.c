/* DJFilter is linked stereo (one set of coefficients) but L and R stay independent signals: a tone
 * only on L leaves R silent, only on R leaves L silent, two different tones stay different. With
 * the DJ filter and Kill on. Also: the all-on state is finite on noise. Host test only. */
#include "djfilter_common.h"

static double rms(const float *p, long n) { double a = 0; long i; for (i = 0; i < n; i++) a += (double)p[i] * p[i]; return sqrt(a / (double)n); }

int main(void)
{
    int mode;
    for (mode = 0; mode < 3; mode++) {            /* 0: L only, 1: R only, 2: different tones both sides */
        static float oL[8 * 3000], oR[8 * 3000]; long b, k = 0; int j; double xc = 0, eL, eR;
        setup(); SET(KILL, 60); SET(DJ, 50); SET(RESO, 60);
        for (b = 0; b < 3000; b++) {
            float l[8], r[8];
            for (j = 0; j < 8; j++, k++) {
                double a = 0.4 * sin(0.07 * (double)k * 3.0), c = 0.3 * sin(0.19 * (double)k * 0.5 + 1.0);
                l[j] = (mode == 1) ? 0.0f : (float)a; r[j] = (mode == 0) ? 0.0f : (float)c;
            }
            block(l, r);
            memcpy(oL + b * 8, fx, 32); memcpy(oR + b * 8, fx + 8, 32);
        }
        eL = rms(oL + 8 * 500, 8 * 2500); eR = rms(oR + 8 * 500, 8 * 2500);
        for (k = 8 * 500; k < 8 * 3000; k++) xc += (double)oL[k] * oR[k];
        xc /= (8.0 * 2500.0) * (eL * eR + 1e-30);
        printf("mode %d: rms L %.4f R %.4f corr %.3f\n", mode, eL, eR, xc);
        CHECK(mode != 0 || (eL > 0.005 && eR < 1e-9), "L only leaked to R (R %.3g)", eR);
        CHECK(mode != 1 || (eR > 0.005 && eL < 1e-9), "R only leaked to L (L %.3g)", eL);
        CHECK(mode != 2 || (eL > 0.005 && eR > 0.005 && fabs(xc) < 0.5), "L and R not apart (corr %.3f)", xc);
    }
    {   /* noise, everything on, extremes: finite and bounded */
        long b; int j; double peak = 0; unsigned int rng = 99u;
        setup(); SET(DJ, 200); SET(KILL, 281); SET(RESO, 100); SET(LEVEL, 36);
        for (b = 0; b < 20000; b++) {
            float l[8], r[8];
            for (j = 0; j < 8; j++) {
                rng = rng * 1664525u + 1013904223u; l[j] = (float)(rng >> 8) * 1.1920929e-7f * 2.0f - 1.0f;
                rng = rng * 1664525u + 1013904223u; r[j] = (float)(rng >> 8) * 1.1920929e-7f * 2.0f - 1.0f;
            }
            block(l, r);
            for (j = 0; j < 16; j++) { if (!(fabs(fx[j]) < 1e6)) peak = 1e30; else if (fabs(fx[j]) > peak) peak = fabs(fx[j]); }
        }
        printf("noise, all on: peak %.3f\n", peak);
        CHECK(peak < 50.0, "unstable or too loud (%.3g)", peak);
    }
    return fails;
}
