/* Shared by the djfilter_*.c tests: the real pedal entry (arena, knob conversion, defaults) with a
 * fake context, and a measure helper. Host test only. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/djfilter/djfilter.c"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[DJFILTER_##n##_SLOT] = DJFILTER_##n##_UI_DEFAULT / 100.0f
#define SET(n, v) params[DJFILTER_##n##_SLOT] = (float)(v) / 100.0f

static void setup(void)
{
    se_init(65536);                       /* a fresh, zeroed arena: the state starts from nothing */
    D(LP); D(HP); D(DJ); D(RESO); D(KILL); D(SLOPE); D(GLIDE); D(LEVEL); D(MIX);
}

/* one block: in L / in R (both 8 samples) -> fx[0..7], fx[8..15] after the effect */
static void block(const float *l, const float *r)
{
    memcpy(fx, l, 32); memcpy(fx + 8, r, 32);
    Fx_DLY_DJFilter(ctx);
}

/* gain (rms out / rms in) of a sine at hz, the same on both sides, after a warm-up */
static double gain_at(double hz, int warm, int meas)
{
    long b, k = 0; int j; double si = 0, so = 0;
    for (b = 0; b < warm + meas; b++) {
        float l[8];
        for (j = 0; j < 8; j++, k++) l[j] = (float)(0.2 * sin(6.283185307179586 * hz * (double)k / 44100.0));
        block(l, l);
        if (b >= warm) for (j = 0; j < 8; j++) { si += (double)l[j] * l[j]; so += (double)fx[j] * fx[j]; }
    }
    return sqrt(so / si);
}
