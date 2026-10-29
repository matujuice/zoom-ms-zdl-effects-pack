/* Scrub through the pedal entry: MONO keeps the dry L/R apart and freezes (L + R) / 2; ST freezes both sides
 * apart (a tone on L only gives a wet only on L, a tone on R only a wet only on R); switching the Stereo
 * knob starts over. HOLD with Pos at "now" freezes the last second. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/scrub/scrub.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[SCRUB_##n##_SLOT] = SCRUB_##n##_UI_DEFAULT / 100.0f
static long T;

static void run(long blocks, int mode, double *eL, double *eR, double *dryErr)
{
    long b; int j;
    for (b = 0; b < blocks; b++) {
        float inL[8], inR[8];
        for (j = 0; j < 8; j++, T++) {
            inL[j] = fx[j] = (mode == 1) ? 0.0f : 0.4f * sinf(0.061f * (float)T);          /* mode 1: R only */
            inR[j] = fx[j + 8] = (mode == 0) ? 0.0f : 0.3f * sinf(0.173f * (float)T + 1.0f); /* mode 0: L only */
        }
        Fx_DLY_Scrub(ctx);
        if (eL) for (j = 0; j < 8; j++) {
            *eL += (double)(fx[j] - 0 * inL[j]) * fx[j]; *eR += (double)fx[j + 8] * fx[j + 8];
        }
        if (dryErr) for (j = 0; j < 8; j++) { double d = fabs(fx[j] - inL[j]); if (d > *dryErr) *dryErr = d; }
    }
}

int main(void)
{
    int stereo, mode;
    se_init(705536u);
    for (stereo = 0; stereo < 2; stereo++) {
        for (mode = 0; mode < 3; mode++) {      /* 0 L only, 1 R only, 2 both */
            double eL = 0, eR = 0, xc = 0;
            memset(se_m + 4096, 0, 705536u); T = 0;
            D(POS); D(GRAIN); D(REC); D(GLIDE); D(DIR); D(SPRAY); D(MIX);
            params[SCRUB_STEREO_SLOT] = (float)stereo / 100.0f;
            params[SCRUB_MIX_SLOT] = 1.0f;                                   /* wet only */
            params[SCRUB_POS_SLOT] = 6.0f; params[SCRUB_GRAIN_SLOT] = 0.40f; params[SCRUB_REC_SLOT] = 0.0f;   /* LIVE delay */
            run(44100 * 2 / 8, mode, 0, 0, 0);                               /* record, clearing, settle */
            { long b; int j;
              for (b = 0; b < 44100 / 8; b++) {
                  for (j = 0; j < 8; j++, T++) {
                      fx[j] = (mode == 1) ? 0.0f : 0.4f * sinf(0.061f * (float)T);
                      fx[j + 8] = (mode == 0) ? 0.0f : 0.3f * sinf(0.173f * (float)T + 1.0f);
                  }
                  Fx_DLY_Scrub(ctx);
                  for (j = 0; j < 8; j++) { eL += (double)fx[j] * fx[j]; eR += (double)fx[j + 8] * fx[j + 8]; xc += (double)fx[j] * fx[j + 8]; }
              } }
            xc /= sqrt(eL * eR) + 1e-30;
            printf("%s mode %d: wet energy L %.2f R %.2f corr %.3f\n", stereo ? "ST  " : "MONO", mode, eL, eR, xc);
            if (stereo) {
                CHECK(mode != 0 || (eL > 1.0 && eR < 1e-9), "ST: L only leaked to R");
                CHECK(mode != 1 || (eR > 1.0 && eL < 1e-9), "ST: R only leaked to L");
                CHECK(mode != 2 || (eL > 1.0 && eR > 1.0 && fabs(xc) < 0.3), "ST: sides not apart");
            } else {
                CHECK(eL > 1.0 && fabs(eL - eR) < 1e-6 * eL, "MONO: wet not the same on both sides");
            }
        }
    }
    /* the dry keeps L and R apart in MONO with Mix 0 */
    { double dry = 0; memset(se_m + 4096, 0, 705536u); T = 0; D(POS); D(GRAIN); D(REC); D(GLIDE); D(DIR); D(SPRAY);
      params[SCRUB_STEREO_SLOT] = 0.0f; params[SCRUB_MIX_SLOT] = 0.0f;
      run(2000, 2, 0, 0, &dry);
      printf("MONO Mix 0: dry L error %.2e\n", dry);
      CHECK(dry < 1e-5, "MONO Mix 0 changed the dry"); }
    /* flipping the knob restarts the buffer without a crash: wp inside the new ring */
    { ScState *s = (ScState *)(((uintptr_t)se_desc[0] + 3u) & ~(uintptr_t)3u);
      params[SCRUB_STEREO_SLOT] = 0.01f; params[SCRUB_MIX_SLOT] = 1.0f; run(5000, 2, 0, 0, 0);
      CHECK(s->nn == SC_NS && s->wp < SC_NS, "ring not switched to ST (nn %d wp %d)", s->nn, s->wp);
      params[SCRUB_STEREO_SLOT] = 0.0f; run(5000, 2, 0, 0, 0);
      CHECK(s->nn == SC_N, "ring not switched back to MONO"); }
    printf("state %u bytes\n", (unsigned)sizeof(ScState));
    CHECK(sizeof(ScState) < 705536u, "state too big");
    return fails;
}
