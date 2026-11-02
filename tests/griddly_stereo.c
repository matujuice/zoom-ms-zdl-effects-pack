/* GridDly stereo: ST keeps L and R apart (own ring each), PING alternates the repeats left / right from
 * one ring, MONO gives the same repeats on both sides; the dry keeps L and R apart in every mode.
 * Footswitch with a stereo signal (the v2.0 beta1 report: audio cut for about a second): switched off the
 * input of BOTH sides passes untouched (Tail OFF: bit exact; Tail ON: the dry plus the decaying tail), and
 * toggling on / off / on in every mode, engine and Tail leaves no hole in the dry of either side.
 * Reproduced on the old code (host, effect function only): with Tail ON, or REVRS, at Mix 100 switching back on
 * is silent for one Time (nothing went into the ring while off): 0.5 s at Time 496 ms, about 1 s in PING or at
 * Time 1 s. The port holds the dry for that Time (dry hold). The old code also threw the right input away. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/griddly/griddly.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SET(n, v) params[GRIDDLY_##n##_SLOT] = (float)(v) / 100.0f
#define ARENA 705536u

static void fresh(int type, int time, int fdbk, int mix, int tail, int stereo)
{
    memset(se_m + 4096, 0, ARENA);
    params[0] = 1.0f;
    SET(TYPE, type); SET(TIME, time); SET(FDBK, fdbk); SET(TONE, 100); SET(CHAR, 0); SET(DUCK, 0);
    SET(MIX, mix); SET(TAIL, tail); SET(STEREO, stereo);
}
static unsigned rng = 5u;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 9) * 2.3841858e-7f - 1.0f; }

/* process n samples of (inL, inR) returned in oL / oR */
static void feed(long n, const float *inL, const float *inR, float *oL, float *oR)
{
    long b; int j;
    for (b = 0; b < n / 8; b++) {
        for (j = 0; j < 8; j++) { fx[j] = inL ? inL[b * 8 + j] : 0.0f; fx[j + 8] = inR ? inR[b * 8 + j] : 0.0f; }
        Fx_DLY_GridDly(ctx);
        for (j = 0; j < 8; j++) { if (oL) oL[b * 8 + j] = fx[j]; if (oR) oR[b * 8 + j] = fx[j + 8]; }
    }
}
static double rms(const float *p, long a, long b) { double s = 0; long i; for (i = a; i < b; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)(b - a)); }
#define SEC 44100
static float inL[SEC * 6], inR[SEC * 6], oL[SEC * 6], oR[SEC * 6];

int main(void)
{
    long i; int mode, type, tail, mix;
    se_init(ARENA);

    /* 1. ST: a burst only on L comes back only on L, only on R only on R, both different stay apart */
    for (mode = 0; mode < 3; mode++) {
        fresh(0, 70, 50, 100, 1, 1);
        feed(SEC, 0, 0, 0, 0);
        for (i = 0; i < SEC * 4; i++) { inL[i] = (mode == 1) ? 0 : ((i < 4410) ? 0.5f * noise() : 0); inR[i] = (mode == 0) ? 0 : ((i < 4410) ? 0.5f * noise() : 0); }
        feed(SEC * 4, inL, inR, oL, oR);
        { double eL = rms(oL, 8000, SEC * 4), eR = rms(oR, 8000, SEC * 4);
          double xc = 0; for (i = 8000; i < SEC * 4; i++) xc += (double)oL[i] * oR[i];
          xc /= (SEC * 4 - 8000) * (eL * eR + 1e-30);
          printf("ST burst %s: wet rms L %.4f R %.4f corr %.3f\n", mode == 0 ? "L only" : mode == 1 ? "R only" : "both", eL, eR, xc);
          CHECK(mode != 0 || (eL > 0.001 && eR < 1e-9), "ST: L leaked to R");
          CHECK(mode != 1 || (eR > 0.001 && eL < 1e-9), "ST: R leaked to L");
          CHECK(mode != 2 || (eL > 0.001 && eR > 0.001 && fabs(xc) < 0.2), "ST: sides not apart"); }
    }

    /* 2. PING: a click on both sides, Time 496 ms (free 70), Fdbk 70: repeat 1 left, 2 right, 3 left, 4 right */
    fresh(0, 70, 70, 100, 1, 2);
    feed(SEC, 0, 0, 0, 0);
    for (i = 0; i < SEC * 4; i++) { inL[i] = (i == 0) ? 0.8f : 0; inR[i] = inL[i]; }
    feed(SEC * 4, inL, inR, oL, oR);
    { long D = 21874; double e[4][2]; int r;
      for (r = 0; r < 4; r++) { e[r][0] = rms(oL, D * (r + 1) - 400, D * (r + 1) + 400); e[r][1] = rms(oR, D * (r + 1) - 400, D * (r + 1) + 400); }
      printf("PING repeats (L,R rms): 1 %.3f %.3f | 2 %.3f %.3f | 3 %.3f %.3f | 4 %.3f %.3f\n", e[0][0], e[0][1], e[1][0], e[1][1], e[2][0], e[2][1], e[3][0], e[3][1]);
      CHECK(e[0][0] > 0.01 && e[0][1] < 0.1 * e[0][0], "PING: repeat 1 not on the left");
      CHECK(e[1][1] > 0.01 && e[1][0] < 0.1 * e[1][1], "PING: repeat 2 not on the right");
      CHECK(e[2][0] > 0.01 && e[2][1] < 0.1 * e[2][0], "PING: repeat 3 not on the left");
      CHECK(e[3][1] > 0.005 && e[3][0] < 0.1 * e[3][1], "PING: repeat 4 not on the right"); }

    /* 3. MONO: the same repeats on both sides, dry still apart (Mix 50 = dry and wet both full) */
    fresh(0, 70, 50, 50, 1, 0);
    feed(SEC, 0, 0, 0, 0);
    for (i = 0; i < SEC * 2; i++) { inL[i] = 0.4f * sinf(0.05f * (float)i); inR[i] = 0.3f * sinf(0.21f * (float)i); }
    feed(SEC * 2, inL, inR, oL, oR);
    { double dl = 0, dr = 0, wdiff = 0;
      for (i = 30000; i < SEC * 2; i++) { dl = oL[i] - inL[i]; dr = oR[i] - inR[i]; if (fabs(dl - dr) > wdiff) wdiff = fabs(dl - dr); }
      printf("MONO Mix 50: wet differs between sides by %.2e\n", wdiff);
      CHECK(wdiff < 1e-5, "MONO: wet not the same on both sides"); }

    /* 4. footswitch with a stereo signal, every mode / engine / Tail / Mix: toggle off at 2 s, on at 4 s */
    { int worst = 0, bad = 0;
      for (mode = 0; mode < 3; mode++) for (type = 0; type < 6; type++) for (tail = 0; tail < 2; tail++) for (mix = 50; mix <= 100; mix += 50) {
          long b; int j, cur[2] = {0, 0}, holeMax = 0, exact = 1;
          double eiL = 0, eiR = 0, eoL = 0, eoR = 0; int w = 0;
          fresh(type, 70, 45, mix, tail, mode);
          for (b = 0; b < SEC * 7 / 8; b++) {
              long t0 = b * 8; int on = !(t0 >= SEC * 3 && t0 < SEC * 5); float aL[8], aR[8];
              params[0] = on ? 1.0f : 0.0f;
              for (j = 0; j < 8; j++) { aL[j] = fx[j] = 0.3f * noise(); aR[j] = fx[j + 8] = 0.25f * noise(); }
              Fx_DLY_GridDly(ctx);
              for (j = 0; j < 8; j++) {
                  eiL += aL[j] * aL[j]; eiR += aR[j] * aR[j]; eoL += fx[j] * fx[j]; eoR += fx[j + 8] * fx[j + 8];
                  if (!on && !tail && (fx[j] != aL[j] || fx[j + 8] != aR[j])) exact = 0;   /* off, Tail OFF: bit exact, both sides */
              }
              if (++w == 64) {                           /* 512-sample window */
                  if (t0 >= SEC * 3) {                   /* only after the first toggle (the very first Time has nothing to echo) */
                      int hL = (eiL > 1.0 && eoL < 0.2 * eiL), hR = (eiR > 1.0 && eoR < 0.2 * eiR);
                      cur[0] = hL ? cur[0] + 1 : 0; cur[1] = hR ? cur[1] + 1 : 0;
                      if (cur[0] > holeMax) holeMax = cur[0]; if (cur[1] > holeMax) holeMax = cur[1];
                  }
                  eiL = eiR = eoL = eoR = 0; w = 0;
              }
          }
          if (!exact) { printf("  not exact off: mode %d type %d\n", mode, type); bad++; }
          if (holeMax > 5) {                              /* > 58 ms: a real cut, not the 50 ms dry hand-over */ printf("  hole %.0f ms: mode %d type %d tail %d mix %d\n", holeMax * 512 / 44.1, mode, type, tail, mix); bad++; }
          if (holeMax > worst) worst = holeMax;
      }
      printf("footswitch with stereo input, 72 combinations: longest hole %.1f ms, %d problems\n", worst * 512 / 44.1, bad);
      CHECK(bad == 0, "footswitch toggling with stereo input left holes or changed the input"); }

    printf("state %u bytes\n", (unsigned)sizeof(GdState));
    CHECK(sizeof(GdState) < ARENA, "state too big");
    return fails;
}
