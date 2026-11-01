/* Washout stereo and switching: L and R stay apart, Width 0 is mono, the footswitch keeps the tail and gives
 * back the full dry with no clicks on or off, Send is an equal-power crossfade-send and click-free. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/washout/washout.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SET(n, v) params[WASHOUT_##n##_SLOT] = (float)(v) / 100.0f
#define ARENA 705536u
#define SEC 44100

static void fresh(int time, int wash, int fdbk, int width, int send, int mix)
{
    memset(se_m + 4096, 0, ARENA);
    params[0] = 1.0f;
    SET(TIME, time); SET(WASH, wash); SET(FDBK, fdbk); SET(HICUT, 0); SET(LOCUT, 0);
    SET(WIDTH, width); SET(SEND, send);
}
static unsigned rng = 9u;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 9) * 2.3841858e-7f - 1.0f; }
static double rms(const float *p, long a, long b) { double s = 0; long i; for (i = a; i < b; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)(b - a)); }
static float inL[SEC * 8], inR[SEC * 8], oL[SEC * 8], oR[SEC * 8];
/* one block with the effect on or off */
static void blk(long t0, int on)
{
    int j;
    params[0] = on ? 1.0f : 0.0f;
    for (j = 0; j < 8; j++) { fx[j] = inL[t0 + j]; fx[j + 8] = inR[t0 + j]; }
    Fx_DLY_Washout(ctx);
    for (j = 0; j < 8; j++) { oL[t0 + j] = fx[j]; oR[t0 + j] = fx[j + 8]; }
}
static float maxstep(const float *p, long a, long b) { float m = 0; long i; for (i = a + 1; i < b; i++) { float d = fabsf(p[i] - p[i - 1]); if (d > m) m = d; } return m; }

int main(void)
{
    long i, b; int mix;
    se_init(ARENA);

    /* 1. L and R stay apart: a burst only on L stays on L (Width 100); only on R stays on R */
    for (mix = 0; mix < 2; mix++) {
        double eL, eR;
        fresh(109, 70, 60, 100, 100, 100);
        for (i = 0; i < SEC * 4; i++) { inL[i] = (!mix && i < 4410) ? 0.5f * noise() : 0; inR[i] = (mix && i < 4410) ? 0.5f * noise() : 0; }
        for (b = 0; b < SEC * 4 / 8; b++) blk(b * 8, 1);
        eL = rms(oL, 8000, SEC * 4); eR = rms(oR, 8000, SEC * 4);
        printf("burst on %s only: wet rms L %.4f R %.4f\n", mix ? "R" : "L", eL, eR);
        CHECK(mix || (eL > 0.001 && eR < 1e-9), "L leaked to R");
        CHECK(!mix || (eR > 0.001 && eL < 1e-9), "R leaked to L");
    }

    /* 2. the two sides decorrelate even from a mono input (different diffuser lengths), and Width 0 makes them equal */
    { double xc = 0, el, er, diff = 0; int wd;
      for (wd = 100; wd >= 0; wd -= 100) {
          fresh(109, 70, 70, wd, 100, 100);
          for (i = 0; i < SEC * 3; i++) { inL[i] = (i < 4410) ? 0.5f * noise() : 0; inR[i] = inL[i]; }
          for (b = 0; b < SEC * 3 / 8; b++) blk(b * 8, 1);
          xc = 0; diff = 0;
          for (i = 30000; i < SEC * 3 - 8; i++) { xc += (double)oL[i] * oR[i]; if (fabs(oL[i] - oR[i]) > diff) diff = fabs(oL[i] - oR[i]); }
          el = rms(oL, 30000, SEC * 3); er = rms(oR, 30000, SEC * 3);
          xc /= (SEC * 3 - 30000) * (el * er + 1e-30);
          printf("mono in, Width %d: corr L/R %.3f, max |L-R| %.3e\n", wd, xc, diff);
          if (wd == 100) CHECK(fabs(xc) < 0.5, "sides not decorrelated at Width 100");
          else CHECK(diff < 1e-5, "Width 0 is not mono");
      } }

    /* 3. footswitch: noise for 1 s with the effect on, then off with the input silent: the tail keeps ringing
     *    (and dies, Fdbk is capped at 95 off); no clicks at either switch with a steady tone on the input */
    { double tail, dryerr = 0; float st; long t;
      fresh(109, 70, 60, 100, 100, 100);
      for (i = 0; i < SEC * 8; i++) { inL[i] = (i < SEC) ? 0.3f * noise() : 0; inR[i] = (i < SEC) ? 0.3f * noise() : 0; }
      for (b = 0; b < SEC * 4 / 8; b++) { t = b * 8; blk(t, t < SEC); }
      tail = rms(oL, SEC + 22050, SEC + 22050 + SEC / 2);
      printf("off, input silent: tail rms %.4f half a second after the off point\n", tail);
      CHECK(tail > 0.02, "tail does not ring after off");
      fresh(109, 70, 60, 100, 100, 100);
      for (i = 0; i < SEC * 8; i++) { inL[i] = 0.2f * sinf(0.02f * (float)i); inR[i] = 0.2f * sinf(0.031f * (float)i); }
      for (b = 0; b < SEC * 8 / 8; b++) { t = b * 8; blk(t, !(t >= SEC * 3 && t < SEC * 6)); }
      st = maxstep(oL, SEC * 3 - 400, SEC * 3 + 800);
      printf("largest step around switch off %.4f, at switch on %.4f (input step max %.4f)\n", st, maxstep(oL, SEC * 6 - 400, SEC * 6 + 800), maxstep(inL, 0, SEC));
      CHECK(st < 0.05f, "click at switch off");
      CHECK(maxstep(oL, SEC * 6 - 400, SEC * 6 + 800) < 0.05f, "click at switch on");
      /* dry untouched: same run again with a silent-after-burst send: output - dry must equal the tail alone;
       * easiest exact check: Send 0 from the start, nothing in the ring, off = bit exact */
      fresh(109, 70, 60, 100, 0, 50);
      for (b = 0; b < SEC * 3 / 8; b++) { t = b * 8; blk(t, t < SEC); }
      for (i = SEC + 1000; i < SEC * 3 - 8; i++) dryerr += fabs(oL[i] - inL[i]) + fabs(oR[i] - inR[i]);
      printf("off with an empty ring: output - dry = %.2e\n", dryerr);
      CHECK(dryerr == 0.0, "dry changed when off"); }

    /* 4. Send 0: the dry passes at full level on and off; run with no tail to see the dry alone */
    { double e; long t;
      fresh(109, 70, 60, 100, 0, 100);
      for (i = 0; i < SEC * 3; i++) { inL[i] = 0.3f * noise(); inR[i] = 0.3f * noise(); }
      for (b = 0; b < SEC * 3 / 8; b++) { t = b * 8; blk(t, t < SEC); }
      e = 0; for (i = 0; i < SEC * 3 - 8; i++) e += fabs(oL[i] - inL[i]);
      printf("empty tail, on then off: |out - dry| = %.2e\n", e);
      CHECK(e == 0.0, "dry not at full level, on or off"); }

    /* 5. Send click-free: a loud tone, the ring fed, Send jumps 100 -> 0 -> 100: the largest sample step stays at the
     *    tone's own, no spike (compare against a Send-constant run) */
    { float s0, s1; long t;
      fresh(109, 0, 0, 100, 100, 100);
      for (i = 0; i < SEC * 2; i++) { inL[i] = 0.5f * sinf(0.05f * (float)i); inR[i] = inL[i]; }
      /* Time is 1/4 = 0.5 s: the echo of the sine shows the Send gain directly */
      for (b = 0; b < SEC * 2 / 8; b++) { t = b * 8; if (t == SEC / 4) SET(SEND, 0); if (t == SEC * 3 / 4) SET(SEND, 100); blk(t, 1); }
      s0 = maxstep(oL, SEC / 2, SEC * 2);
      fresh(109, 0, 0, 100, 100, 100);
      for (b = 0; b < SEC * 2 / 8; b++) blk(b * 8, 1);
      s1 = maxstep(oL, SEC / 2, SEC * 2);
      printf("Send 100->0->100: largest step %.4f (constant Send %.4f)\n", s0, s1);
      CHECK(s0 < s1 * 1.2f + 0.002f, "Send change clicks"); }

    /* 6. crossfade-send: Fdbk 0, Wash 0, Time 1/4 (0.5 s) so dry and wash are apart in time; a burst in the
     *    first 0.25 s. Dry part = output in the burst, wash part = the echo. Send 100: no dry; Send 50: both at
     *    sin/cos 45 degrees (0.707); off: full dry */
    { int sd; static const int sends[3] = { 100, 50, 0 };
      for (sd = 0; sd < 3; sd++) {
          double dry, wet, ref; long t;
          fresh(109, 0, 0, 100, sends[sd], 100);
          for (i = 0; i < SEC * 2; i++) { inL[i] = (i < SEC / 4) ? 0.3f * noise() : 0; inR[i] = inL[i]; }
          for (b = 0; b < SEC * 2 / 8; b++) { t = b * 8; blk(t, 1); }
          ref = rms(inL, 8000, SEC / 4);                 /* after the ring clear (~30 ms) and the Send ramp */
          dry = rms(oL, 8000, SEC / 4) / ref;
          wet = rms(oL, SEC / 2 + 2000, SEC * 3 / 4) / ref;
          printf("Send %d: dry gain %.3f, wash gain %.3f (want %s)\n", sends[sd], dry, wet,
                 sd == 0 ? "0, 1" : sd == 1 ? "0.707, 0.707" : "1, 0");
          if (sd == 0) CHECK(dry < 1e-6 && fabs(wet - 1.0) < 0.02, "Send 100 is not wash only");
          if (sd == 1) CHECK(fabs(dry - 0.7071) < 0.02 && fabs(wet - 0.7071) < 0.02, "Send 50 is not equal power");
          if (sd == 2) CHECK(fabs(dry - 1.0) < 1e-9 && wet < 1e-9, "Send 0 is not dry only");
      }
      /* switched off at Send 100: the dry comes back to full level within ~50 ms */
      { double e = 0; long t;
        fresh(109, 0, 0, 100, 100, 100);
        for (i = 0; i < SEC * 2; i++) { inL[i] = (i < SEC / 4) ? 0 : 0.3f * noise(); inR[i] = inL[i]; }
        for (b = 0; b < SEC * 2 / 8; b++) { t = b * 8; blk(t, t < SEC / 4); }   /* on, silent; off when the noise starts */
        for (i = SEC / 4 + 2205; i < SEC * 3 / 4 - 200; i++) { double d = fabs(oL[i] - inL[i]); e = d > e ? d : e; }
        printf("off at Send 100: largest |out - dry| 50 ms after off, before the echo %.2e\n", e);
        CHECK(e < 1e-4, "dry does not come back to full level when off"); } }

    printf("state %u bytes\n", (unsigned)sizeof(WdState));
    return fails;
}
