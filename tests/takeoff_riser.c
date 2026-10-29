/* Takeoff: the input passes untouched in every state, a flip fires a timed sweep that ends at Len, a flip
 * mid-sweep drops it, the footswitch hold (transport word 6) rises, sits and drops, MAN follows Pos, every
 * Sound makes sound in both Dirs, AUTO finds the strongest note (and falls back to FREE on drums), idle
 * does no work, no clicks at start or drop, Space rings after the cut. */
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "stereo_entry.h"
#include "../src/custom/takeoff/takeoff.c"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SET(n, v) params[TAKEOFF_##n##_SLOT] = (float)(v) / 100.0f
#define SEC 44100
#define MAXT (SEC * 26)
#define ARENA (1u << 17)
#define BLK(sec) ((long)((sec) * SEC / 8))

static float A[MAXT], B[MAXT], C[MAXT], D[MAXT];   /* out L, out R, in L, out - in (L) */
static int foot;
static long T;
static float (*inf)(long t, int side);

static TkState *S(void) { return (TkState *)(((uintptr_t)se_desc[0] + 3u) & ~(uintptr_t)3u); }

static void fresh(int dir, int len, int trig, int snd, int rng, int key, int space, int pos, int vol, int zt)
{
    memset(se_m + 4096, 0, ARENA);
    memset(zt_host, 0, sizeof zt_host);
    if (zt) { zt_host[0] = ZT_MAGIC; zt_host[5] = 12000u; }
    foot = 0; T = 0; inf = 0;
    SET(DIR, dir); SET(LEN, len); SET(TRIG, trig); SET(SOUND, snd); SET(RANGE, rng); SET(KEY, key);
    SET(SPACE, space); SET(POS, pos); SET(VOL, vol);
}

static void blk(void)
{
    int j; float iL[8], iR[8];
    for (j = 0; j < 8; j++) {
        iL[j] = inf ? inf(T + j, 0) : 0.0f; iR[j] = inf ? inf(T + j, 1) : 0.0f;
        fx[j] = iL[j]; fx[j + 8] = iR[j];
    }
    params[0] = foot ? 1.0f : 0.0f;
    Fx_DLY_Takeoff(ctx);
    for (j = 0; j < 8; j++) { A[T + j] = fx[j]; B[T + j] = fx[j + 8]; C[T + j] = iL[j]; D[T + j] = fx[j] - iL[j]; }
    T += 8;
}
static void run_to(double sec) { while (T < (long)(sec * SEC)) blk(); }

static double rms(const float *p, long a, long b) { double s = 0; long i; for (i = a; i < b; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)(b - a)); }
static float peak(const float *p, long a, long b) { float m = 0; long i; for (i = a; i < b; i++) if (fabsf(p[i]) > m) m = fabsf(p[i]); return m; }
static float maxstep(const float *p, long a, long b) { float m = 0; long i; for (i = a + 1; i < b; i++) { float d = fabsf(p[i] - p[i - 1]); if (d > m) m = d; } return m; }
static long last_loud(const float *p, long n, float th) { long i; for (i = n - 1; i >= 0; i--) if (fabsf(p[i]) > th) return i; return -1; }

static unsigned rs = 77u;
static float noise(void) { rs = rs * 1664525u + 1013904223u; return (float)(int)(rs >> 9) * 2.3841858e-7f - 1.0f; }
static float in_noise(long t, int side) { (void)t; (void)side; return 0.3f * noise(); }
static float in_chord(long t, int side)
{
    double x = (double)t / SEC * 6.283185307;
    (void)side;
    return (float)(0.2 * sin(220.0 * x) + 0.1 * sin(146.83 * x) + 0.1 * sin(185.0 * x));
}
static float in_triad(long t, int side)            /* C3 E3 G3 equal */
{
    double x = (double)t / SEC * 6.283185307;
    (void)side;
    return (float)(0.1 * sin(130.81 * x) + 0.1 * sin(164.81 * x) + 0.1 * sin(196.0 * x));
}
static float in_drums(long t, int side)            /* kick every 0.5 s, noisy snare/hat bursts */
{
    double tm = (double)(t % (SEC / 2)) / SEC, tn = (double)(t % (SEC / 4)) / SEC;
    (void)side;
    return (float)(0.6 * exp(-tm * 14.0) * sin(6.283185307 * (45.0 + 60.0 * exp(-tm * 30.0)) * tm)) + 0.3f * (float)exp(-tn * 25.0) * noise();
}

static float ZL[MAXT], ZR[MAXT];

int main(void)
{
    int snd, dir; long i;
    se_init(ARENA);

    /* 1. the input passes untouched: out == in + added, the added part does not depend on the input */
    for (snd = 0; snd < 5; snd++) {
        long bad = 0, badIdle = 0;
        fresh(snd & 1, 6, 0, snd, 2, 2, 40, 0, 100, 1);
        foot = 0; run_to(0.5); foot = 1; run_to(5.0);
        for (i = 0; i < T; i++) { ZL[i] = A[i]; ZR[i] = B[i]; }
        fresh(snd & 1, 6, 0, snd, 2, 2, 40, 0, 100, 1);
        inf = in_noise;
        foot = 0; run_to(0.5); foot = 1; run_to(5.0);
        for (i = 0; i < T; i++) {
            if (!(A[i] == C[i] + ZL[i])) bad++;
            { float inR = A[i]; (void)inR; }
            if (i < 22000 && A[i] != C[i]) badIdle++;
        }
        printf("sound %d: samples not in + added %ld, changed while idle %ld\n", snd, bad, badIdle);
        CHECK(bad == 0, "sound %d: input not passed bit exact", snd);
        CHECK(badIdle == 0, "sound %d: input changed while idle", snd);
    }
    /* right side too, and idle with the footswitch off, key AUTO (listener must not write the buffer) */
    fresh(0, 6, 0, 1, 2, 0, 0, 0, 100, 1); inf = in_chord;
    run_to(3.0);
    { long bad = 0; for (i = 0; i < T; i++) if (A[i] != C[i]) bad++; CHECK(bad == 0, "AUTO listener changed the input"); }

    /* 2. a flip fires a timed sweep that ends at Len (2 s), swelling all the way */
    fresh(0, 6, 0, 1, 2, 2, 0, 0, 100, 0);
    run_to(0.2); { long t0 = T; foot = 1; run_to(5.0);
        { long te = last_loud(D, T, 1e-4f); double r1 = rms(D, t0 + 8000, t0 + 17000), r2 = rms(D, t0 + 44100, t0 + 53000), r3 = rms(D, t0 + 77000, t0 + 86000);
          printf("timed sweep: ends %.3f s after the flip (Len 2 s), rms %.4f -> %.4f -> %.4f\n", (double)(te - t0) / SEC, r1, r2, r3);
          CHECK(labs(te - t0 - 2 * SEC) < SEC / 20, "sweep ended at %.3f s, want 2.0", (double)(te - t0) / SEC);
          CHECK(r1 < r2 && r2 < r3 && r3 > 0.05, "sweep does not swell");
          CHECK(peak(D, t0 + 2 * SEC + 400, T) < 1e-6f, "sound after the cut"); } }
    /* Len in bars at 120 BPM: 1 bar = 2 s (the transport block's tempo word) */
    fresh(0, 0, 0, 1, 2, 2, 0, 0, 100, 1);
    run_to(0.2); { long t0 = T; foot = 1; run_to(4.0);
        { long te = last_loud(D, T, 1e-4f); printf("1 bar at 120 BPM: %.3f s\n", (double)(te - t0) / SEC);
          CHECK(labs(te - t0 - 2 * SEC) < SEC / 20, "1 bar not 2 s"); } }

    /* 3. a flip mid-sweep drops it early */
    fresh(0, 6, 0, 1, 2, 2, 0, 0, 100, 0);
    run_to(0.2); foot = 1; run_to(1.2); { long t1 = T; foot = 0; run_to(4.0);
        printf("drop: loudest after the 2nd flip +300 samples: %.2e\n", peak(D, t1 + 300, T));
        CHECK(peak(D, t1 + 300, T) < 1e-6f, "flip mid-sweep did not drop");
        CHECK(rms(D, t1 - 4000, t1) > 0.02, "no sound before the drop"); }

    /* 4. hold through transport word 6: rise, sit at the top, drop on release; short press = timed */
    fresh(0, 6, 0, 3, 2, 2, 0, 0, 100, 1);
    run_to(0.2); { long t0 = T; foot = 1; zt_host[6] = 1u; run_to(5.0);
        { double rA = rms(D, t0 + 3 * SEC, t0 + 3 * SEC + 4000), rB = rms(D, T - 8000, T - 4000);
          printf("hold: rms at 3 s %.4f, at 4.8 s %.4f, claim word 0x%08X (params %08X)\n", rA, rB, zt_host[7], (unsigned)(uintptr_t)params);
          CHECK(rA > 0.05 && fabs(rA - rB) < 0.05 * rA, "hold does not sit at the top");
          CHECK(zt_host[7] == (unsigned)(uintptr_t)params, "hold claim not written");
          { long t1 = T; zt_host[6] = 0u; run_to(6.0);
            CHECK(peak(D, t1 + 300, T) < 1e-6f, "hold release did not drop"); } } }
    fresh(0, 6, 0, 3, 2, 2, 0, 0, 100, 1);
    run_to(0.2); { long t0 = T; foot = 1; zt_host[6] = 1u; run_to(0.4); zt_host[6] = 0u; run_to(5.0);
        { long te = last_loud(D, T, 1e-4f); printf("short press: ends %.3f s after the flip\n", (double)(te - t0) / SEC);
          CHECK(labs(te - t0 - 2 * SEC) < SEC / 20, "short press not a timed sweep"); } }
    fresh(0, 6, 0, 3, 2, 2, 0, 0, 100, 0);           /* old firmware: word 6 reads 0, press only */
    run_to(0.2); { long t0 = T; foot = 1; run_to(5.0);
        CHECK(labs(last_loud(D, T, 1e-4f) - t0 - 2 * SEC) < SEC / 20, "no-firmware press not timed"); }

    /* 5. MAN follows Pos (SINE: amplitude = 0.343 x Vol x x^2) */
    fresh(0, 6, 1, 3, 2, 2, 0, 0, 100, 0);
    run_to(0.5); { double r0 = rms(D, 4000, T - 100), r50, r100; long a;
        SET(POS, 100); run_to(1.5); a = T; run_to(2.0); r100 = rms(D, a, T);
        SET(POS, 50); run_to(3.0); a = T; run_to(3.5); r50 = rms(D, a, T);
        printf("MAN: rms at Pos 0 %.2e, 100 %.4f, 50 %.4f (ratio %.2f, want 4)\n", r0, r100, r50, r100 / r50);
        CHECK(r0 < 1e-6, "MAN Pos 0 not silent");
        CHECK(fabs(r100 / r50 - 4.0) < 0.3, "MAN does not follow Pos");
        { long t1; SET(POS, 0); run_to(4.5); t1 = T; run_to(5.0); CHECK(rms(D, t1, T) < 1e-4, "MAN Pos back to 0 not silent"); } }

    /* 6. every Sound makes sound in both Dirs, bounded, with width */
    for (snd = 0; snd < 5; snd++) for (dir = 0; dir < 2; dir++) {
        double rm; float pk; int nan = 0;
        fresh(dir, 6, 0, snd, 3, 2, 0, 0, 100, 0);
        run_to(0.1); foot = 1; run_to(2.5);
        for (i = 0; i < T; i++) if (!(A[i] == A[i]) || !(B[i] == B[i])) nan++;
        rm = rms(D, 0, T); pk = peak(D, 0, T);
        { double w = 0; for (i = 0; i < T; i++) w += fabs(A[i] - B[i]);
          printf("sound %d dir %d: rms %.4f peak %.3f |L-R| mean %.4f\n", snd, dir, rm, pk, w / (double)T);
          if (snd != 3) CHECK(w > 0.0, "sound %d dir %d has no width", snd, dir); }
        CHECK(rm > 0.01 && pk < 1.0f && !nan, "sound %d dir %d: rms %.4f peak %.3f nan %d", snd, dir, rm, pk, nan);
    }

    /* 7. AUTO key */
    fresh(1, 6, 0, 3, 0, 0, 0, 0, 100, 0); inf = in_chord;
    run_to(8.0);
    printf("AUTO chord (A strongest): note %d\n", S()->autoNote);
    CHECK(S()->autoNote == 9, "AUTO chose %d, want 9 (A)", S()->autoNote);
    { long t0 = T, n = 0; foot = 1; run_to(9.0);
      for (i = t0 + 200; i < t0 + 4410; i++) if (D[i - 1] < 0.0f && D[i] >= 0.0f) n++;
      printf("DOWN sine on AUTO key: %.1f Hz at the start (A: 110)\n", (double)n * SEC / 4210.0);
      CHECK(fabs((double)n * SEC / 4210.0 - 110.0) < 14.0, "sweep does not start on A");
      CHECK(S()->note == 9, "note not latched"); }
    fresh(1, 6, 0, 3, 0, 4, 0, 0, 100, 0);           /* fixed D: 73.4 Hz */
    run_to(0.2); { long t0 = T, n = 0; foot = 1; run_to(1.0);
      for (i = t0 + 200; i < t0 + 4410; i++) if (D[i - 1] < 0.0f && D[i] >= 0.0f) n++;
      printf("DOWN sine on D: %.1f Hz at the start (73.4)\n", (double)n * SEC / 4210.0);
      CHECK(fabs((double)n * SEC / 4210.0 - 73.4) < 10.0, "fixed key D wrong"); }
    fresh(1, 6, 0, 3, 0, 0, 0, 0, 100, 0); inf = in_triad; run_to(8.0);
    { int k; float t = 0, m = 0; for (k = 0; k < 12; k++) { t += S()->chroma[k]; if (S()->chroma[k] > m) m = S()->chroma[k]; }
      printf("AUTO triad: peak share %.3f of chroma %.2e\n", m / t, t); }
    printf("AUTO equal triad C E G: note %d (0 C, 4 E, 7 G)\n", S()->autoNote);
    CHECK(S()->autoNote == 0 || S()->autoNote == 4 || S()->autoNote == 7, "AUTO on a triad chose %d", S()->autoNote);
    fresh(1, 6, 0, 3, 0, 0, 0, 0, 100, 0); inf = in_drums; run_to(10.0);
    { int k; float t = 0, m = 0; for (k = 0; k < 12; k++) { t += S()->chroma[k]; if (S()->chroma[k] > m) m = S()->chroma[k]; }
      printf("AUTO drums: peak share %.3f of chroma %.2e\n", m / t, t); }
    printf("AUTO drums: note %d (want -1)\n", S()->autoNote);
    CHECK(S()->autoNote == -1, "AUTO found a note %d in drums", S()->autoNote);
    fresh(1, 6, 0, 3, 0, 0, 0, 0, 100, 0); inf = in_noise; run_to(10.0);
    { int k; float t = 0, m = 0; for (k = 0; k < 12; k++) { t += S()->chroma[k]; if (S()->chroma[k] > m) m = S()->chroma[k]; }
      printf("AUTO noise: peak share %.3f of chroma %.2e\n", m / t, t); }
    printf("AUTO white noise: note %d (want -1)\n", S()->autoNote);
    CHECK(S()->autoNote == -1, "AUTO found a note %d in noise", S()->autoNote);
    fresh(1, 6, 0, 3, 0, 0, 0, 0, 100, 0); run_to(3.0);
    CHECK(S()->autoNote == -1, "AUTO found a note in silence");
    fresh(0, 6, 0, 3, 1, 0, 0, 0, 100, 0); inf = in_noise; run_to(10.0);   /* no clear note: FREE */
    { long t0 = T; foot = 1; run_to(2.0); CHECK(S()->note == -1, "no clear note should be FREE"); (void)t0; }

    /* 8. idle: nothing is synthesised and the reverb stops once the tail has died */
    fresh(0, 6, 0, 2, 2, 2, 0, 0, 100, 0);
    run_to(2.0);
    CHECK(S()->rngL == 12345u && S()->ph[1] == 0.33f && S()->svL[0] == 0.0f && S()->svL[1] == 0.0f, "idle ran the synthesis");
    fresh(0, 6, 0, 3, 2, 2, 50, 0, 100, 0);
    run_to(0.2); foot = 1; run_to(2.5);
    { int rw0; double tail;
      tail = rms(D, 2 * SEC + 4000, 2 * SEC + 12000); printf("Space 50: tail rms %.4f just after the cut\n", tail);
      CHECK(tail > 1e-3, "Space tail does not ring after the cut");
      run_to(22.0); rw0 = S()->rw;
      printf("tail died: quiet %d (idle at %d)\n", S()->quiet, QUIET_BLOCKS);
      CHECK(S()->quiet >= QUIET_BLOCKS, "tail never died");
      { long t1 = T; run_to(23.0); CHECK(S()->rw == rw0 && peak(D, t1, T) == 0.0f, "idle after the tail still runs the reverb"); } }
    { /* cost: blocks per second idle vs sounding (host speed, for comparison only) */
      clock_t c0; double idle, snd1, snd2; long n = 200000, b;
      fresh(0, 6, 0, 2, 2, 2, 0, 0, 100, 0);
      c0 = clock(); for (b = 0; b < n; b++) { int j; for (j = 0; j < 16; j++) fx[j] = 0.0f; params[0] = 0.0f; Fx_DLY_Takeoff(ctx); } idle = (double)(clock() - c0) / CLOCKS_PER_SEC;
      fresh(0, 35, 0, 2, 2, 2, 0, 0, 100, 0); params[0] = 0.0f; blk(); foot = 1;
      c0 = clock(); for (b = 0; b < n; b++) { int j; for (j = 0; j < 16; j++) fx[j] = 0.0f; params[0] = (b == 0) ? 1.0f : 1.0f; Fx_DLY_Takeoff(ctx); } snd1 = (double)(clock() - c0) / CLOCKS_PER_SEC;
      fresh(0, 35, 0, 2, 2, 2, 60, 0, 100, 0); params[0] = 0.0f; blk(); foot = 1;
      c0 = clock(); for (b = 0; b < n; b++) { int j; for (j = 0; j < 16; j++) fx[j] = 0.0f; params[0] = 1.0f; Fx_DLY_Takeoff(ctx); } snd2 = (double)(clock() - c0) / CLOCKS_PER_SEC;
      printf("host time for %ld blocks: idle %.4f s, sounding %.4f s, sounding + Space %.4f s\n", n, idle, snd1, snd2);
      CHECK(idle * 2.0 < snd1 + 1e-9, "idle is not much cheaper than sounding"); }

    /* 9. no clicks at the start, the drop and the cut (SINE; steps against the signal's own steady step) */
    fresh(1, 6, 0, 3, 3, 2, 0, 0, 100, 0);           /* DOWN: starts at the top at full level */
    run_to(0.3); { long t0 = T; foot = 1; run_to(0.6);
        { float st = maxstep(D, t0 + 2000, t0 + 4000), s0 = maxstep(D, t0 - 10, t0 + 800);
          printf("start (DOWN): max step %.4f, steady %.4f\n", s0, st);
          CHECK(s0 <= 1.2f * st, "click at the DOWN start"); } }
    fresh(0, 6, 0, 3, 3, 2, 0, 0, 100, 1);           /* UP timed: cut at the top; and a hold release at the top */
    run_to(0.2); { long t0 = T; foot = 1; run_to(2.6);
        { long te = t0 + 2 * SEC; float st = maxstep(D, te - 6000, te - 3000), sd = maxstep(D, te - 200, te + 400);
          printf("cut (UP end): max step %.4f, steady %.4f\n", sd, st);
          CHECK(sd <= 1.2f * st, "click at the UP cut"); } }
    fresh(0, 6, 0, 3, 3, 2, 0, 0, 100, 1);
    run_to(0.2); foot = 1; zt_host[6] = 1u; run_to(4.0); { long t1 = T; float st = maxstep(D, t1 - 6000, t1 - 3000); zt_host[6] = 0u; run_to(4.5);
        { float sd = maxstep(D, t1 - 10, t1 + 600); printf("hold release: max step %.4f, steady %.4f\n", sd, st);
          CHECK(sd <= 1.2f * st, "click at the hold release"); } }

    /* 10. Space: a tail with Space on, none with Space 0, and the tail never runs away */
    fresh(0, 6, 0, 1, 2, 2, 100, 0, 100, 0);
    run_to(0.2); foot = 1; run_to(6.0);
    CHECK(peak(D, 0, T) < 4.0f, "Space 100 peak %.2f", peak(D, 0, T));
    printf("Space 100: peak %.3f, tail rms 0.3 s after the cut %.4f\n", peak(D, 0, T), rms(D, 2 * SEC + 13000, 2 * SEC + 20000));

    /* labels */
    { char b[8]; unsigned v[] = {0, 2, 4, 5, 6, 35}; int k;
      for (k = 0; k < 6; k++) { ZDL_GetLabel_1(v[k], b); printf("Len%u=%s ", v[k], b); }
      for (k = 0; k < 14; k++) { ZDL_GetLabel_5((unsigned)k, b); printf("Key%d=%s ", k, b); }
      ZDL_GetLabel_6(0, b); printf("Space0=%s ", b); ZDL_GetLabel_6(100, b); printf("Space100=%s ", b);
      ZDL_GetLabel_3(2, b); printf("Sound2=%s ", b); ZDL_GetLabel_4(3, b); printf("Range3=%s\n", b);
      ZDL_GetLabel_1(4, b); CHECK(b[0] == '1' && b[1] == '6' && b[5] == 0, "Len 16bar label");
      ZDL_GetLabel_1(35, b); CHECK(b[0] == '3' && b[1] == '1' && b[2] == 's' && b[3] == 0, "Len 31s label"); }

    printf("%s\n", fails ? "takeoff_riser: FAILED" : "takeoff_riser: ok");
    return fails ? 1 : 0;
}
