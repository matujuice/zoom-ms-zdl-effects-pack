/* S.GN_L is true stereo with one loss decision: with Fill GAP both sides drop in the very same samples,
 * the kept packets keep L and R apart (different DC, a tone only on L leaves R silent), and a different
 * noise on each side stays uncorrelated through the codec. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/sgnl/sgnl.c"
#define SET(n, v) params[SGNL_##n##_SLOT] = (float)(v) / 100.0f
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

static void knobs(int loss, int codec, int fill, int line)
{
    SET(LOSS, loss); SET(SIZE, 30); SET(CODEC, codec); SET(FILL, fill); SET(BURST, 30); SET(JUMP, 0);
    SET(LINE, line); SET(EDGE, 0); SET(MIX, 100);
}

int main(void)
{
    long b; int j, mode;
    se_init(1u << 20);
    /* 1. shared loss: DC 0.3 on L, 0.2 on R, Fill GAP; zero on one side only when zero on the other */
    knobs(50, 0, 0, 0);
    { long both = 0, onlyL = 0, onlyR = 0, kept = 0, lastL = 0, lastR = 0;
      for (b = 0; b < 44100 * 6 / 8; b++) {
          for (j = 0; j < 8; j++) { fx[j] = 0.3f; fx[j + 8] = 0.2f; }
          Fx_DLY_SGNL(ctx);
          if (b < 200) continue;
          for (j = 0; j < 8; j++) {
              int zl = fabsf(fx[j]) < 1e-6f, zr = fabsf(fx[j + 8]) < 1e-6f;
              if (zl && zr) both++; else if (zl) onlyL++; else if (zr) onlyR++; else kept++;
              if (!zl) { if (fabsf(fx[j] - 0.3f) > 1e-4f) lastL++; }
              if (!zr) { if (fabsf(fx[j + 8] - 0.2f) > 1e-4f) lastR++; }
          }
      }
      printf("GAP: both lost %ld, only L %ld, only R %ld, kept %ld, level errors L %ld R %ld\n", both, onlyL, onlyR, kept, lastL, lastR);
      CHECK(both > 1000 && kept > 1000, "no losses or no kept packets");
      CHECK(onlyL == 0 && onlyR == 0, "losses are not shared");
      CHECK(lastL == 0 && lastR == 0, "kept packets changed level"); }
    /* 2. L only / R only / different noise, every fill, codec on */
    for (mode = 0; mode < 3; mode++) {
        int fill;
        for (fill = 0; fill < 8; fill++) {
            unsigned r = 777u; double eL = 0, eR = 0, xc = 0;
            memset(se_m + 4096, 0, 1u << 20);
            knobs(30, 40, fill, 1);
            for (b = 0; b < 44100 * 3 / 8; b++) {
                for (j = 0; j < 16; j++) { r = r * 1664525u + 1013904223u; fx[j] = ((float)(int)(r >> 9) * 2.3841858e-7f - 1.0f) * 0.4f; }
                if (mode == 0) memset(fx + 8, 0, 32);
                if (mode == 1) memset(fx, 0, 32);
                Fx_DLY_SGNL(ctx);
                for (j = 0; j < 8; j++) { eL += fx[j] * fx[j]; eR += fx[j + 8] * fx[j + 8]; xc += (double)fx[j] * fx[j + 8]; }
            }
            xc /= sqrt(eL * eR) + 1e-30;
            if (fill == 3 || fill == 5) { /* NOISE / GARBL make sound out of nothing: only check the sides differ */ }
            else {
                CHECK(mode != 0 || (eL > 1.0 && eR < 1e-9), "fill %d: L only leaked to R (%g)", fill, eR);
                CHECK(mode != 1 || (eR > 1.0 && eL < 1e-9), "fill %d: R only leaked to L (%g)", fill, eL);
            }
            CHECK(mode != 2 || (eL > 1.0 && eR > 1.0 && fabs(xc) < 0.3), "fill %d: L and R not apart (corr %.3f)", fill, xc);
            if (fill == 1) printf("mode %d fill %d: energy L %.2f R %.2f corr %.3f\n", mode, fill, eL, eR, xc);
        }
    }
    printf("state %u bytes\n", (unsigned)sizeof(SgState));
    CHECK(sizeof(SgState) < 705536u, "state too big");
    return fails;
}
