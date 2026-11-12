/* Washout (one Wash macro, Tone, Size) through the real pedal entry: riding Wash and Size never clicks, Wash 100
 * stays bounded and rings far longer than a low Wash, Wash 0 with an empty loop is the bit-exact dry and does no
 * work, the tail dies and goes idle, the footswitch keeps the tail and gives back the full dry, L stays on L. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/washout/washout.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SET(n, v) params[WASHOUT_##n##_SLOT] = (float)(v) / 100.0f
#define ARENA 705536u
#define SEC 44100
#define LEN (SEC * 10)

static float inL[LEN], inR[LEN], oL[LEN], oR[LEN];
static unsigned rng = 7u;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 9) * 2.3841858e-7f - 1.0f; }
static double rms(const float *p, long a, long b) { double s = 0; long i; for (i = a; i < b; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)(b - a)); }
static void fresh(int wash, int tone, int size)
{
    memset(se_m + 4096, 0, ARENA);
    params[0] = 1.0f; SET(WASH, wash); SET(TONE, tone); SET(SIZE, size);
}
static void blk(long t, int on)
{
    int j;
    params[0] = on ? 1.0f : 0.0f;
    for (j = 0; j < 8; j++) { fx[j] = inL[t + j]; fx[j + 8] = inR[t + j]; }
    Fx_DLY_Washout(ctx);
    for (j = 0; j < 8; j++) { oL[t + j] = fx[j]; oR[t + j] = fx[j + 8]; }
}
static float peak(const float *p, long a, long b) { float m = 0; long i; for (i = a; i < b; i++) m = fabsf(p[i]) > m ? fabsf(p[i]) : m; return m; }
/* largest second difference: a click shows as a spike there */
static float d2(const float *p, long a, long b)
{
    float m = 0; long i;
    for (i = a + 2; i < b; i++) { float d = fabsf(p[i] - 2.0f * p[i - 1] + p[i - 2]); m = d > m ? d : m; }
    return m;
}

int main(void)
{
    long i, b, t;
    se_init(ARENA);

    /* 1. a 330 Hz sine; Wash rides 0 -> 100 -> 0 and Size moves; the second difference stays near a steady run's */
    { float ride, steady;
      for (i = 0; i < LEN; i++) { inL[i] = 0.3f * sinf(0.047018f * (float)i); inR[i] = inL[i]; }
      fresh(60, 50, 50);
      for (b = 0; b < LEN / 8; b++) blk(b * 8, 1);
      steady = d2(oL, SEC, LEN);
      fresh(0, 50, 50);
      for (b = 0; b < LEN / 8; b++) {
          t = b * 8;
          if (t % 2205 == 0) { long k = t / 2205; SET(WASH, k < 100 ? k : (k < 200 ? 200 - k : 0)); }
          if (t >= SEC * 3 && t < SEC * 3 + 8) SET(SIZE, 90);
          if (t >= SEC * 6 && t < SEC * 6 + 8) SET(SIZE, 10);
          blk(t, 1);
      }
      ride = d2(oL, 4096, LEN);
      printf("Wash ride + Size moves: largest 2nd difference %.5f (steady Wash 60: %.5f)\n", ride, steady);
      CHECK(ride < 2.0f * steady, "riding Wash or Size clicks"); }

    /* 2. Wash 100, loud noise: bounded; Wash 100 rings far longer than Wash 20 */
    { double t100, t20; float pk;
      for (i = 0; i < LEN; i++) { inL[i] = (i < SEC * 3) ? 0.9f * noise() : 0; inR[i] = (i < SEC * 3) ? 0.9f * noise() : 0; }
      fresh(100, 50, 50);
      for (b = 0; b < LEN / 8; b++) blk(b * 8, 1);
      pk = peak(oL, 0, LEN); t100 = rms(oL, SEC * 5, SEC * 6);
      fresh(20, 50, 50);
      for (b = 0; b < LEN / 8; b++) blk(b * 8, 1);
      t20 = rms(oL, SEC * 5, SEC * 6);
      printf("Wash 100: peak %.3f, tail 2 s after %.4f; Wash 20 tail %.2e\n", pk, t100, t20);
      CHECK(pk < 1.6f, "Wash 100 not bounded");
      CHECK(t100 > 0.02 && t20 < 1e-3, "Wash does not set the decay"); }

    /* 3. Wash 0 from the start: the bit-exact dry; after a burst at Wash 50 then Wash 0 the tail dies and the dry is exact again */
    { double e = 0;
      for (i = 0; i < LEN; i++) { inL[i] = 0.3f * noise(); inR[i] = 0.3f * noise(); }
      fresh(0, 50, 50);
      for (b = 0; b < SEC * 2 / 8; b++) blk(b * 8, 1);
      for (i = 0; i < SEC * 2; i++) e += fabs(oL[i] - inL[i]) + fabs(oR[i] - inR[i]);
      printf("Wash 0, empty: |out - dry| = %.2e\n", e);
      CHECK(e == 0.0, "Wash 0 is not the dry");
      for (i = 0; i < LEN; i++) { inL[i] = (i < SEC) ? 0.3f * noise() : 0; inR[i] = inL[i]; }
      fresh(50, 50, 50);
      for (b = 0; b < LEN / 8; b++) { t = b * 8; if (t >= SEC && t < SEC + 8) SET(WASH, 0); blk(t, 1); }
      e = 0; for (i = SEC * 8; i < LEN; i++) e += fabs(oL[i]) + fabs(oR[i]);
      printf("burst at Wash 50, then Wash 0: output 7 s later %.2e, quiet %d\n", e, ((WdState *)(se_m + 4096))->quiet);
      CHECK(e == 0.0, "the tail does not die to the exact dry"); }

    /* 4. footswitch: off keeps the tail ringing; off at Wash 100 gives back the full dry within ~50 ms */
    { double tail, e = 0;
      for (i = 0; i < LEN; i++) { inL[i] = (i < SEC) ? 0.3f * noise() : 0; inR[i] = inL[i]; }
      fresh(80, 50, 50);
      for (b = 0; b < SEC * 3 / 8; b++) { t = b * 8; blk(t, t < SEC); }
      tail = rms(oL, SEC + 11025, SEC + 22050);
      printf("off, input silent: tail rms %.4f a quarter second after off\n", tail);
      CHECK(tail > 0.01, "tail does not ring after off");
      for (i = 0; i < LEN; i++) { inL[i] = (i < SEC / 2) ? 0 : 0.3f * noise(); inR[i] = inL[i]; }
      fresh(100, 50, 50);
      for (b = 0; b < SEC / 8; b++) { t = b * 8; blk(t, t < SEC / 4); }   /* off at 0.25 s, the input starts at 0.5 s */
      for (i = SEC / 2; i < SEC / 8 * 8; i++) { double d = fabs(oL[i] - inL[i]); e = d > e ? d : e; }
      printf("off at Wash 100: largest |out - dry| once the send has ramped down %.2e\n", e);
      CHECK(e == 0.0, "dry does not come back when off"); }

    /* 5. a burst only on L stays on L; a mono input comes out decorrelated */
    { double eL, eR, xc = 0;
      for (i = 0; i < LEN; i++) { inL[i] = (i < 4410) ? 0.5f * noise() : 0; inR[i] = 0; }
      fresh(70, 50, 50);
      for (b = 0; b < SEC * 2 / 8; b++) blk(b * 8, 1);
      eL = rms(oL, 8000, SEC * 2); eR = rms(oR, 8000, SEC * 2);
      printf("burst on L only: rms L %.4f R %.2e\n", eL, eR);
      CHECK(eL > 0.001 && eR < 1e-9, "L leaked to R");
      for (i = 0; i < LEN; i++) inR[i] = inL[i];
      fresh(70, 50, 50);
      for (b = 0; b < SEC * 2 / 8; b++) blk(b * 8, 1);
      for (i = 8000; i < SEC * 2; i++) xc += (double)oL[i] * oR[i];
      xc /= (SEC * 2 - 8000) * (rms(oL, 8000, SEC * 2) * rms(oR, 8000, SEC * 2) + 1e-30);
      printf("mono in: corr L/R %.3f\n", xc);
      CHECK(fabs(xc) < 0.5, "sides not decorrelated"); }

    printf("state %u bytes\n", (unsigned)sizeof(WdState));
    return fails;
}
