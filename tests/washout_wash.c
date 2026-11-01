/* Washout through the real pedal entry: the tail is diffuse (echo density rises vs a plain delay), stays
 * bounded at Fdbk 120, Fdbk 100 + Send 0 + both cuts off holds the level, tempo sync follows the zmt BPM,
 * the idle path costs nothing and gives the same output as the full path. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/washout/washout.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SET(n, v) params[WASHOUT_##n##_SLOT] = (float)(v) / 100.0f
#define ARENA 705536u
#define SEC 44100

static void fresh(int time, int wash, int fdbk, int hi, int lo, int width, int send, int mix)
{
    memset(se_m + 4096, 0, ARENA);
    params[0] = 1.0f;
    SET(TIME, time); SET(WASH, wash); SET(FDBK, fdbk); SET(HICUT, hi); SET(LOCUT, lo);
    SET(WIDTH, width); SET(SEND, send);
}
static unsigned rng = 5u;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 9) * 2.3841858e-7f - 1.0f; }
static void feed(long n, const float *inL, const float *inR, float *oL, float *oR)
{
    long b; int j;
    for (b = 0; b < n / 8; b++) {
        for (j = 0; j < 8; j++) { fx[j] = inL ? inL[b * 8 + j] : 0.0f; fx[j + 8] = inR ? inR[b * 8 + j] : 0.0f; }
        Fx_DLY_Washout(ctx);
        for (j = 0; j < 8; j++) { if (oL) oL[b * 8 + j] = fx[j]; if (oR) oR[b * 8 + j] = fx[j + 8]; }
    }
}
static double rms(const float *p, long a, long b) { double s = 0; long i; for (i = a; i < b; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)(b - a)); }
static float inL[SEC * 16], inR[SEC * 16], oL[SEC * 16], oR[SEC * 16];

static long count_active(const float *p, long a, long b) { long i, n = 0; for (i = a; i < b; i++) if (fabsf(p[i]) > 1e-4f) n++; return n; }

int main(void)
{
    long i; int k;
    se_init(ARENA);

    /* 1. diffuse tail: an impulse, Time 1/4 at 120 BPM (0.5 s), Fdbk 70. Active samples in 0.6..1.4 s: a plain
     *    delay has none or a few spikes, Washout has hundreds, and more in the later window than the earlier. */
    { long plain, wash, w1, w2;
      for (i = 0; i < SEC * 3; i++) { inL[i] = (i == 4000) ? 0.8f : 0; inR[i] = inL[i]; }
      fresh(109, 0, 70, 0, 0, 100, 100, 100);
      feed(SEC * 3, inL, inR, oL, oR);
      plain = count_active(oL, 4000 + SEC * 6 / 10, 4000 + SEC * 14 / 10);
      fresh(109, 70, 70, 0, 0, 100, 100, 100);
      feed(SEC * 3, inL, inR, oL, oR);
      wash = count_active(oL, 4000 + SEC * 6 / 10, 4000 + SEC * 14 / 10);
      w1 = count_active(oL, 4000 + SEC * 5 / 10, 4000 + SEC * 10 / 10); w2 = count_active(oL, 4000 + SEC * 20 / 10, 4000 + SEC * 25 / 10);
      printf("echo density 0.6-1.4 s: plain delay %ld samples, Washout %ld; Washout 0.5-1.0 s %ld, 2.0-2.5 s %ld\n", plain, wash, w1, w2);
      CHECK(plain < 20 && wash > 50 * (plain + 1), "tail not diffuse");
      CHECK(w2 > w1, "density does not rise over time"); }

    /* 2. bounded: Fdbk 120, Wash 100, noise for 14 s */
    { float pk = 0; int bad = 0;
      fresh(109, 100, 120, 0, 0, 100, 100, 100);
      for (i = 0; i < SEC * 14; i++) { inL[i] = 0.7f * noise(); inR[i] = 0.7f * noise(); }
      feed(SEC * 14, inL, inR, oL, oR);
      for (i = 0; i < SEC * 14; i++) { float a = fabsf(oL[i]), b = fabsf(oR[i]); if (a > pk) pk = a; if (b > pk) pk = b; if (!(a < 100) || !(b < 100)) bad++; }
      printf("Fdbk 120, 14 s of noise: peak %.3f, rms last 2 s %.3f\n", pk, rms(oL, SEC * 12, SEC * 14));
      CHECK(!bad && pk < 4.0f, "not bounded at Fdbk 120"); }

    /* 3. hold: Fdbk 100, Send 0 (after a burst), both cuts off: the level stays roughly steady */
    {
        double r1, r2, r3;
        fresh(109, 70, 100, 0, 0, 100, 100, 100);
        for (i = 0; i < SEC * 2; i++) { inL[i] = (i < SEC / 2) ? 0.2f * noise() : 0; inR[i] = (i < SEC / 2) ? 0.2f * noise() : 0; }
        feed(SEC * 2, inL, inR, oL, oR);
        SET(SEND, 0);
        for (i = 0; i < SEC * 14; i++) { inL[i] = inR[i] = 0; }
        feed(SEC * 14, inL, inR, oL, oR);
        r1 = rms(oL, SEC * 1, SEC * 3); r2 = rms(oL, SEC * 6, SEC * 8); r3 = rms(oL, SEC * 12, SEC * 14);
        printf("hold: rms 1-3 s %.4f, 6-8 s %.4f, 12-14 s %.4f\n", r1, r2, r3);
        CHECK(r1 > 0.01 && r3 > 0.6 * r1 && r3 < 1.5 * r1, "level not held");
    }
    /* ... and the cuts act in the loop: a 6 kHz tone dies through HiCut 80 but holds with it off; a 100 Hz tone dies
     * through LoCut 100 but holds with it off */
    { int cut, hi; double a, b, ra[2][2];
      for (hi = 0; hi < 2; hi++) for (cut = 0; cut < 2; cut++) {
          fresh(109, 70, 100, hi ? (cut ? 80 : 0) : 0, hi ? 0 : (cut ? 100 : 0), 100, 100, 100);
          for (i = 0; i < SEC * 2; i++) { float ph = (float)i * (hi ? 0.8546f : 0.01425f); inL[i] = (i < SEC / 2) ? 0.2f * sinf(ph) : 0; inR[i] = inL[i]; }
          feed(SEC * 2, inL, inR, oL, oR);
          SET(SEND, 0); for (i = 0; i < SEC * 6; i++) inL[i] = inR[i] = 0;
          feed(SEC * 6, inL, inR, oL, oR);
          a = rms(oL, SEC * 1, SEC * 2); b = rms(oL, SEC * 5, SEC * 6);
          ra[hi][cut] = b / (a + 1e-30);
      }
      printf("rms 5-6 s / 1-2 s: HiCut off %.3f, HiCut 80 %.3f; LoCut off %.3f, LoCut 100 %.3f\n", ra[1][0], ra[1][1], ra[0][0], ra[0][1]);
      CHECK(ra[1][0] > 0.6 && ra[1][1] < 0.2, "HiCut does not act in the loop");
      CHECK(ra[0][0] > 0.6 && ra[0][1] < 0.2, "LoCut does not act in the loop"); }

    /* 4. tempo sync: an impulse with Wash 0, Time 1/4: echo at 60/BPM s, from the zmt BPM word */
    { int bpm; long expect, got, j;
      for (bpm = 90; bpm <= 150; bpm += 60) {
          zt_host[0] = ZT_MAGIC; zt_host[1] = 2u; zt_host[2] = 0u; zt_host[3] = 0u; zt_host[4] = 0u; zt_host[5] = (unsigned)(bpm * 100);
          fresh(109, 0, 50, 0, 0, 100, 100, 100);
          for (i = 0; i < SEC * 2; i++) inL[i] = inR[i] = (i == 8000) ? 0.9f : 0;
          feed(SEC * 2, inL, inR, oL, oR);
          expect = (long)(44100.0 * 60.0 / bpm + 0.5) + 8000; got = 0;
          for (j = 9000; j < SEC * 2; j++) if (fabsf(oL[j]) > 0.1f) { got = j; break; }
          printf("BPM %d: first echo at %ld (expected %ld)\n", bpm, got, expect);
          CHECK(got >= expect - 8 && got <= expect + 8, "echo off the beat at %d BPM", bpm);
      }
      zt_host[0] = 0u; }

    /* 5. with Wash on the synced echo still lands on the beat (the diffusers' latency is taken off Time) */
    { long j, pk = 0; float m = 0;
      fresh(109, 70, 50, 0, 0, 100, 100, 100);
      for (i = 0; i < SEC * 2; i++) inL[i] = inR[i] = (i == 8000) ? 0.9f : 0;
      feed(SEC * 2, inL, inR, oL, oR);
      for (j = 9000; j < SEC * 2; j++) { float a = fabsf(oL[j]); if (a > m) { m = a; pk = j; } }
      printf("Wash 70, 120 BPM: loudest wet sample at %ld (the beat is %ld)\n", pk, (long)(22050 + 8000));
      CHECK(pk > 22050 + 8000 - 400 && pk < 22050 + 8000 + 4000, "wash echo far from the beat"); }

    /* 6. idle: Send 0 and a dead tail: the buffer is left alone, and the state is untouched */
    { WdState *st = (WdState *)(((uintptr_t)(se_m + 4096) + 3u) & ~(uintptr_t)3u); int wp0, wp1; float err = 0;
      fresh(109, 70, 60, 0, 0, 100, 100, 50);
      for (i = 0; i < SEC / 2; i++) { inL[i] = 0.3f * noise(); inR[i] = 0.3f * noise(); }
      feed(SEC / 2, inL, inR, oL, oR);
      SET(SEND, 0);
      for (i = 0; i < SEC * 10; i++) { inL[i] = 0.2f * sinf(0.03f * (float)i); inR[i] = 0.1f * sinf(0.07f * (float)i); }
      feed(SEC * 10, inL, inR, oL, oR);
      wp0 = st->wp;
      feed(SEC, inL, inR, oL, oR);
      wp1 = st->wp;
      for (i = 0; i < SEC - 8; i++) err += fabsf(oL[i] - inL[i]) + fabsf(oR[i] - inR[i]);
      printf("idle (Send 0, tail dead): ring pointer %d -> %d, output differs from the dry by %.2e\n", wp0, wp1, err);
      CHECK(wp0 == wp1 && err == 0.0f, "idle path does work or changes the dry"); }

    printf("state %u bytes (arena %u)\n", (unsigned)sizeof(WdState), ARENA);
    CHECK(sizeof(WdState) < ARENA, "state too big");
    return fails;
}
