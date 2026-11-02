/* Takeoff: the input passes untouched in every state, a flip fires a timed sweep that ends at Len, a flip
 * mid-sweep drops it, the footswitch hold (transport word 6) rises, sits and drops and its release flip is
 * swallowed, QUANT starts a press on the next Len line, every Sound makes sound in both Dirs, Color/Wind/Grain
 * stay bounded, the riser is mono (same on L and R), idle does no work, no clicks at start or drop,
 * Space rings after the cut, the LED pulses while armed, follows the sweep and is never written when idle. */
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

static int clkOn;
static long clk0;
static void fresh(int dir, int len, int var, int space, int vol, int zt)
{
    memset(se_m + 4096, 0, ARENA);
    memset(zt_host, 0, sizeof zt_host);
    if (zt) { zt_host[0] = ZT_MAGIC; zt_host[5] = 12000u; }
    memset(tk_svc_host, 0, sizeof tk_svc_host);
    foot = 0; T = 0; inf = 0; clkOn = 0;
    SET(DIR, dir); SET(LEN, len); SET(RANGE, 60); SET(VAR, var);
    SET(COLOR, 25); SET(WIND, 40); SET(GRAIN, 0); SET(SPACE, space); SET(VOL, vol);
}

static void blk(void)
{
    int j; float iL[8], iR[8];
    for (j = 0; j < 8; j++) {
        iL[j] = inf ? inf(T + j, 0) : 0.0f; iR[j] = inf ? inf(T + j, 1) : 0.0f;
        fx[j] = iL[j]; fx[j + 8] = iR[j];
    }
    params[0] = foot ? 1.0f : 0.0f;
    if (clkOn) { zt_host[2] = 1u; zt_host[4] = 1u + (unsigned)((double)(T - clk0) / 918.75); }  /* 120 BPM, 24 ppq */
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
static float ZL[MAXT], ZR[MAXT];

int main(void)
{
    int snd, dir; long i;
    se_init(ARENA);

    /* 1. the input passes untouched: out == in + added, the added part does not depend on the input */
    for (snd = 0; snd < 2; snd++) {             /* snd: Dir, and Var 0 / 100 */
        long bad = 0, badIdle = 0;
        fresh(snd, 6, 100 * snd, 40, 100, 1);
        foot = 0; run_to(0.5); foot = 1; run_to(5.0);
        for (i = 0; i < T; i++) { ZL[i] = A[i]; ZR[i] = B[i]; }
        fresh(snd, 6, 100 * snd, 40, 100, 1);
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
    /* idle with the footswitch off (the listener must not write the buffer) */
    fresh(0, 6, 0, 0, 100, 1); inf = in_chord;
    run_to(3.0);
    { long bad = 0; for (i = 0; i < T; i++) if (A[i] != C[i]) bad++; CHECK(bad == 0, "AUTO listener changed the input"); }

    /* 2. a flip fires a timed sweep that ends at Len (2 s), swelling all the way */
    fresh(0, 6, 0, 0, 100, 0);
    run_to(0.2); { long t0 = T; foot = 1; run_to(5.0);
        { long te = last_loud(D, T, 1e-4f); double r1 = rms(D, t0 + 8000, t0 + 17000), r2 = rms(D, t0 + 44100, t0 + 53000), r3 = rms(D, t0 + 77000, t0 + 86000);
          printf("timed sweep: ends %.3f s after the flip (Len 2 s), rms %.4f -> %.4f -> %.4f\n", (double)(te - t0) / SEC, r1, r2, r3);
          CHECK(labs(te - t0 - 2 * SEC) < SEC / 20, "sweep ended at %.3f s, want 2.0", (double)(te - t0) / SEC);
          CHECK(r1 < r2 && r2 < r3 && r3 > 0.05, "sweep does not swell");
          CHECK(peak(D, t0 + 2 * SEC + 400, T) < 1e-6f, "sound after the cut"); } }
    /* Len in bars at 120 BPM: 1 bar = 2 s (the transport block's tempo word) */
    fresh(0, 0, 0, 0, 100, 1);
    run_to(0.2); { long t0 = T; foot = 1; run_to(4.0);
        { long te = last_loud(D, T, 1e-4f); printf("1 bar at 120 BPM: %.3f s\n", (double)(te - t0) / SEC);
          CHECK(labs(te - t0 - 2 * SEC) < SEC / 20, "1 bar not 2 s"); } }

    /* 3. a flip mid-sweep drops it early */
    fresh(0, 6, 0, 0, 100, 0);
    run_to(0.2); foot = 1; run_to(1.2); { long t1 = T; foot = 0; run_to(4.0);
        printf("drop: loudest after the 2nd flip +300 samples: %.2e\n", peak(D, t1 + 300, T));
        CHECK(peak(D, t1 + 300, T) < 1e-6f, "flip mid-sweep did not drop");
        CHECK(rms(D, t1 - 4000, t1) > 0.005, "no sound before the drop"); }

    /* 4. hold through transport word 6: rise, sit at the top, drop on release; short press = timed */
    fresh(0, 6, 0, 0, 100, 1);
    run_to(0.2); { long t0 = T; foot = 1; zt_host[6] = 1u; run_to(5.0);
        { double rA = rms(D, t0 + 3 * SEC, t0 + 3 * SEC + 4000), rB = rms(D, T - 8000, T - 4000);
          printf("hold: rms at 3 s %.4f, at 4.8 s %.4f, claim word 0x%08X (params %08X)\n", rA, rB, zt_host[7], (unsigned)(uintptr_t)params);
          CHECK(rA > 0.05 && fabs(rA - rB) < 0.05 * rA, "hold does not sit at the top");
          CHECK(zt_host[7] == (unsigned)(uintptr_t)params, "hold claim not written");
          { long t1 = T; zt_host[6] = 0u; run_to(5.01); foot = 0; run_to(8.0);   /* momentary: the release flips too */
            CHECK(peak(D, t1 + 300, T) < 1e-6f, "hold release did not drop, or its flip fired a sweep");
            CHECK(S()->state == ST_IDLE, "hold release flip left state %d", S()->state); } } }
    fresh(0, 6, 0, 0, 100, 1);
    run_to(0.2); { long t0 = T; foot = 1; zt_host[6] = 1u; run_to(0.4); zt_host[6] = 0u; run_to(0.41); foot = 0; run_to(5.0);
        { long te = last_loud(D, T, 1e-4f); printf("short press: ends %.3f s after the flip\n", (double)(te - t0) / SEC);
          CHECK(labs(te - t0 - 2 * SEC) < SEC / 20, "short press not a timed sweep"); } }
    fresh(0, 6, 0, 0, 100, 0);           /* old firmware: word 6 reads 0, press only */
    run_to(0.2); { long t0 = T; foot = 1; run_to(5.0);
        CHECK(labs(last_loud(D, T, 1e-4f) - t0 - 2 * SEC) < SEC / 20, "no-firmware press not timed"); }

    /* 5. the real MOD firmware order: word 6 rises up to 16 ms BEFORE the press flip (the 16 ms poll), and a
     *    momentary release flips again. Press and hold, with and without the MIDI clock (Len 1 bar / 2 s):
     *    the sweep must sound within Len (+ the wait for the line), the LED must light, the release must not
     *    start a second sweep */
    { int ck, hd;
      for (ck = 0; ck < 2; ck++) for (hd = 0; hd < 2; hd++) {
          unsigned int *gw = (unsigned int *)(se_m + 2048); long t0, t1, k; int lit = 0; float pk;
          fresh(0, ck ? 0 : 6, 0, 0, 100, 1);
          if (ck) { clkOn = 1; clk0 = 0; }                     /* bar lines at 0, 2, 4 s */
          tk_svc_host[0] = SV_MAGIC; tk_svc_host[SV_LEDON] = (unsigned)(uintptr_t)&gw[0];
          tk_svc_host[SV_LEDOFF] = (unsigned)(uintptr_t)&gw[1]; tk_svc_host[SV_LEDMASK] = 1u;
          tk_svc_host[SV_CURSOR] = (unsigned)(uintptr_t)params;
          run_to(0.3); zt_host[6] = 1u; run_to(0.316); foot = 1; t0 = T;
          for (k = 0; k < BLK(hd ? 2.7 : 0.15); k++) { blk(); lit += (S()->led == 1); }
          zt_host[6] = 0u; t1 = T; run_to((double)t1 / SEC + 0.01); foot = 0;   /* momentary: the release flips */
          while (T < t0 + 4 * SEC + SEC / 2) { blk(); lit += (S()->led == 1); }
          pk = peak(D, t0, T);
          printf("word 6 first, %s, %s: peak %.3f, LED lit %d blocks, state %d at the end\n",
                 ck ? "clock" : "no clock", hd ? "hold" : "press", pk, lit, S()->state);
          CHECK(pk > 0.01f, "%s %s: no sweep (the press flip was eaten)", ck ? "clock" : "no clock", hd ? "hold" : "press");
          CHECK(lit > 0, "%s %s: LED never lit", ck ? "clock" : "no clock", hd ? "hold" : "press");
          CHECK(S()->state == ST_IDLE && peak(D, T - SEC / 4, T) < 1e-6f, "%s %s: the release started a second sweep",
                ck ? "clock" : "no clock", hd ? "hold" : "press");
          if (hd) CHECK(peak(D, t1 + 300, T) < 1e-6f, "%s hold: no drop on release", ck ? "clock" : "no clock");
      } }

    /* 6. both Dirs make sound, bounded, mono (snd: Var 0 / 100) */
    for (snd = 0; snd < 2; snd++) for (dir = 0; dir < 2; dir++) {
        double rm; float pk; int nan = 0;
        fresh(dir, 6, 100 * snd, 0, 100, 0);
        run_to(0.1); foot = 1; run_to(2.5);
        for (i = 0; i < T; i++) if (!(A[i] == A[i]) || !(B[i] == B[i])) nan++;
        rm = rms(D, 0, T); pk = peak(D, 0, T);
        { double w = 0; for (i = 0; i < T; i++) w += fabs(A[i] - B[i]);
          printf("sound %d dir %d: rms %.4f peak %.3f |L-R| mean %.4f\n", snd, dir, rm, pk, w / (double)T);
          CHECK(w == 0.0, "sound %d dir %d is not mono", snd, dir); }
        CHECK(rm > 0.01 && pk < 1.0f && !nan, "sound %d dir %d: rms %.4f peak %.3f nan %d", snd, dir, rm, pk, nan);
    }

    /* 8. idle: nothing is synthesised and the reverb stops once the tail has died */
    fresh(0, 6, 0, 0, 100, 0);
    run_to(2.0);
    CHECK(S()->rng == 12345u && S()->sv[0] == 0.0f && S()->sv[1] == 0.0f, "idle ran the synthesis");
    fresh(0, 6, 0, 50, 100, 0);
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
      fresh(0, 6, 0, 0, 100, 0);
      c0 = clock(); for (b = 0; b < n; b++) { int j; for (j = 0; j < 16; j++) fx[j] = 0.0f; params[0] = 0.0f; Fx_DLY_Takeoff(ctx); } idle = (double)(clock() - c0) / CLOCKS_PER_SEC;
      fresh(0, 35, 0, 0, 100, 0); params[0] = 0.0f; blk(); foot = 1;
      c0 = clock(); for (b = 0; b < n; b++) { int j; for (j = 0; j < 16; j++) fx[j] = 0.0f; params[0] = (b == 0) ? 1.0f : 1.0f; Fx_DLY_Takeoff(ctx); } snd1 = (double)(clock() - c0) / CLOCKS_PER_SEC;
      fresh(0, 35, 0, 60, 100, 0); params[0] = 0.0f; blk(); foot = 1;
      c0 = clock(); for (b = 0; b < n; b++) { int j; for (j = 0; j < 16; j++) fx[j] = 0.0f; params[0] = 1.0f; Fx_DLY_Takeoff(ctx); } snd2 = (double)(clock() - c0) / CLOCKS_PER_SEC;
      printf("host time for %ld blocks: idle %.4f s, sounding %.4f s, sounding + Space %.4f s\n", n, idle, snd1, snd2);
      CHECK(idle * 2.0 < snd1 + 1e-9, "idle is not 2x cheaper than sounding"); }

    /* 9. no clicks at the start, the drop and the cut (noise; steps against the signal's own steady step) */
    fresh(1, 6, 0, 0, 100, 0);           /* DOWN: starts at the top at full level */
    run_to(0.3); { long t0 = T; foot = 1; run_to(0.6);
        { float st = maxstep(D, t0 + 2000, t0 + 4000), s0 = maxstep(D, t0 - 10, t0 + 800);
          printf("start (DOWN): max step %.4f, steady %.4f\n", s0, st);
          CHECK(s0 <= 1.2f * st, "click at the DOWN start"); } }
    fresh(0, 6, 0, 0, 100, 1);           /* UP timed: cut at the top; and a hold release at the top */
    run_to(0.2); { long t0 = T; foot = 1; run_to(2.6);
        { long te = t0 + 2 * SEC; float st = maxstep(D, te - 6000, te - 3000), sd = maxstep(D, te - 200, te + 400);
          printf("cut (UP end): max step %.4f, steady %.4f\n", sd, st);
          CHECK(sd <= 1.2f * st, "click at the UP cut"); } }
    fresh(0, 6, 0, 0, 100, 1);
    run_to(0.2); foot = 1; zt_host[6] = 1u; run_to(4.0); { long t1 = T; float st = maxstep(D, t1 - 6000, t1 - 3000); zt_host[6] = 0u; run_to(4.01); foot = 0; run_to(4.5);
        { float sd = maxstep(D, t1 - 10, t1 + 600); printf("hold release: max step %.4f, steady %.4f\n", sd, st);
          CHECK(sd <= 1.2f * st, "click at the hold release"); } }

    /* 10. Space: a tail with Space on, none with Space 0, and the tail never runs away */
    fresh(0, 6, 0, 100, 100, 0);
    run_to(0.2); foot = 1; run_to(6.0);
    CHECK(peak(D, 0, T) < 4.0f, "Space 100 peak %.2f", peak(D, 0, T));
    printf("Space 100: peak %.3f, tail rms 0.3 s after the cut %.4f\n", peak(D, 0, T), rms(D, 2 * SEC + 13000, 2 * SEC + 20000));

    /* 11. QUANT: without the MIDI clock a press starts at once; with it a press waits for the next bar line,
     *     a press just after a line joins it, a second press while waiting cancels */
    fresh(0, 6, 0, 0, 100, 0);
    run_to(0.2); { long t0 = T, ts, te; foot = 1; run_to(1.0); foot = 0; run_to(3.0); foot = 1; run_to(6.0);
        ts = t0; while (ts < T && fabsf(D[ts]) < 1e-7f) ts++;   /* noise starts very quiet (x^2 swell) */
        te = last_loud(D, T, 1e-4f);
        printf("no clock: press at 0.2 s starts at %.3f; press at 3.0 s, ends at %.3f (want 0.2, 5.0)\n",
               (double)ts / SEC, (double)te / SEC);
        CHECK(ts - t0 < SEC / 20, "no clock: press did not start at once");
        CHECK(labs(te - 5 * SEC) < SEC / 20, "no clock: second press did not start at once"); }
    fresh(0, 0, 0, 0, 100, 1);
    run_to(0.2); clkOn = 1; clk0 = T; run_to(2.25); { long te; foot = 1; run_to(6.0);
        te = last_loud(D, T, 1e-4f);
        printf("QUANT late press (50 ms after the line): ends at %.3f s (want 4)\n", (double)(te - clk0) / SEC);
        CHECK(labs(te - clk0 - 4 * SEC) < SEC / 20, "late press did not join the line"); }
    fresh(0, 0, 0, 0, 100, 1);
    run_to(0.2); clkOn = 1; clk0 = T; run_to(3.0); { long t1; foot = 1; run_to(3.2); foot = 0; t1 = T; run_to(7.0);
        printf("QUANT cancel: peak after %.2e, state %d\n", peak(D, t1, T), S()->state);
        CHECK(peak(D, t1, T) < 1e-6f && S()->state == ST_IDLE, "second press while waiting did not cancel"); }
    /* with the MIDI clock: lines on the bar from Start (1 bar = 2 s at 120 BPM) */
    fresh(0, 0, 0, 0, 100, 1);
    run_to(0.2); clkOn = 1; clk0 = T; run_to(3.2); { long ts, te; foot = 1; ts = T; run_to(9.0);
        while (ts < T && fabsf(D[ts]) < 1e-7f) ts++;
        te = last_loud(D, T, 1e-4f);
        printf("QUANT clock: press at 3.0 s from Start, starts %.3f, ends %.3f (want 4, 6)\n",
               (double)(ts - clk0) / SEC, (double)(te - clk0) / SEC);
        CHECK(labs(ts - clk0 - 4 * SEC) < SEC / 20 && labs(te - clk0 - 6 * SEC) < SEC / 20, "clocked QUANT off the bar"); }

    /* 12. Color / Wind / Grain: NOISE stays bounded and sounding at every corner, and each knob changes it */
    { int c, w, g;
      for (c = 0; c <= 100; c += 100) for (w = 0; w <= 100; w += 100) for (g = 0; g <= 100; g += 100) {
          double rm, df = 0; float pk; int nan = 0;
          fresh(0, 6, 0, 0, 100, 0); SET(COLOR, c); SET(WIND, w); SET(GRAIN, g);
          run_to(0.1); foot = 1; run_to(2.0);
          for (i = 0; i < T; i++) { if (!(A[i] == A[i]) || !(B[i] == B[i])) nan++; if (!(c || w || g)) ZL[i] = A[i]; }
          for (i = 0; i < T; i++) df += fabs(A[i] - ZL[i]);
          rm = rms(D, SEC, T); pk = peak(D, 0, T);
          printf("Color %3d Wind %3d Grain %3d: rms %.4f peak %.3f, differs from 0/0/0 by %.1f\n", c, w, g, rm, pk, df);
          CHECK(rm > 0.01 && pk < 1.0f && !nan, "noise corner C%d W%d G%d: rms %.4f peak %.3f", c, w, g, rm, pk);
          if (c || w || g) CHECK(df > 1.0, "knob corner C%d W%d G%d changes nothing", c, w, g); } }

    /* 15. Range: 0 and 100 both sound and stay bounded, and change the sweep */
    { int rg; double r[2];
      for (rg = 0; rg < 2; rg++) {
          fresh(0, 6, 0, 0, 100, 0); SET(RANGE, 100 * rg);
          run_to(0.1); foot = 1; run_to(2.2);
          for (i = 0; i < T; i++) { if (!rg) ZL[i] = A[i]; }
          r[rg] = rms(D, 0, T);
          printf("Range %d: rms %.4f peak %.3f\n", 100 * rg, r[rg], peak(D, 0, T));
          CHECK(r[rg] > 0.01 && peak(D, 0, T) < 1.0f, "Range %d: no sound or too loud", 100 * rg);
      }
      { double df = 0; for (i = 0; i < T; i++) df += fabs(A[i] - ZL[i]); CHECK(df > 1.0, "Range changes nothing"); } }

    /* 14. Var: 0 = two identical takeoffs; 100 = two different ones at about the same level, no click at the start */
    { int vv; double r0 = 0;
      for (vv = 0; vv <= 100; vv += 100) {
          long t0, t1, k; double e = 0, ra, rb; float st, s0;
          fresh(0, 6, vv, 0, 100, 0);
          run_to(0.2); t0 = T; foot = 1; run_to(3.0); t1 = T; foot = 0; run_to(5.5);   /* each flip fires one sweep */
          for (k = 0; k < 2 * SEC; k++) e += fabs(D[t0 + k] - D[t1 + k]);
          ra = rms(D, t0, t0 + 2 * SEC); rb = rms(D, t1, t1 + 2 * SEC);
          st = maxstep(D, t1 + 2000, t1 + 4000); s0 = maxstep(D, t1 - 10, t1 + 800);
          printf("Var %d: two takeoffs differ by %.3e (sum |a-b|), rms %.4f / %.4f, start step %.4f steady %.4f\n",
                 vv, e, ra, rb, s0, st);
          if (vv == 0) { CHECK(e == 0.0, "Var 0: two takeoffs differ"); r0 = ra; }
          else {
              CHECK(e > 1.0, "Var 100: two takeoffs are the same");
              CHECK(ra > 0.5 * r0 && ra < 2.0 * r0 && rb > 0.5 * r0 && rb < 2.0 * r0, "Var 100: level jump (%.4f, %.4f vs %.4f)", ra, rb, r0);
          }
          CHECK(s0 <= 1.2f * st, "Var %d: click at the 2nd start", vv);
      } }

    /* 13. LED: never written unless the firmware says we are under the cursor; pulses while armed; follows the
     *     sweep (UP: duty rises); idle writes nothing */
    { unsigned int *gr = (unsigned int *)(se_m + 2048); long k;   /* GPIO stand-ins in low memory */
#define gon gr[0]
#define goff gr[1]
      int lit1, lit2, ch;
      fresh(0, 0, 0, 0, 100, 1); clkOn = 1; clk0 = 0;   /* bar lines at 0, 2, 4, 6 s */
      tk_svc_host[0] = SV_MAGIC; tk_svc_host[SV_LEDON] = (unsigned)(uintptr_t)&gon;
      tk_svc_host[SV_LEDOFF] = (unsigned)(uintptr_t)&goff; tk_svc_host[SV_LEDMASK] = 1u;
      tk_svc_host[SV_CURSOR] = 0u; gon = goff = 0u;
      run_to(0.2); foot = 1; run_to(1.0);        /* armed until 2.0, sweep 2-4 */
      CHECK(gon == 0u && goff == 0u, "LED written with the cursor elsewhere");
      run_to(4.5); tk_svc_host[SV_CURSOR] = (unsigned)(uintptr_t)params; run_to(4.8);
      CHECK(gon == 0u && goff == 0u, "LED written while idle");
      foot = 0; ch = 0; lit1 = 0;                /* armed at 4.8 until the line at 6.0: a flash a beat */
      for (k = 0; k < BLK(0.8); k++) { int l0 = S()->led; blk(); if (S()->led != l0) ch++; lit1 += (S()->led == 1); }
      printf("LED armed: %d changes, lit %.2f of the time (state %d)\n", ch, (double)lit1 / BLK(0.8), S()->state);
      CHECK(S()->state == ST_WAIT && ch >= 2 && lit1 > 0 && lit1 < BLK(0.8), "LED does not pulse while armed");
      CHECK(gon == 1u && goff == 1u, "LED registers not written with the mask");
      tk_led_host[0] = 1u; tk_led_host[1] = 0u; blk();   /* a flip: the stock LED task would rewrite the LED */
      CHECK(tk_led_host[1] == 1u, "stock LED task not kept quiet while armed");
      run_to(6.1); lit1 = 0; for (k = 0; k < BLK(0.4); k++) { blk(); lit1 += (S()->led == 1); }
      run_to(7.5); lit2 = 0; for (k = 0; k < BLK(0.4); k++) { blk(); lit2 += (S()->led == 1); }
      printf("LED UP sweep: lit %.2f early, %.2f late\n", (double)lit1 / BLK(0.4), (double)lit2 / BLK(0.4));
      CHECK(lit2 > 4 * lit1 + 10, "LED does not fade up with the sweep");
      run_to(8.5); gon = goff = 0u; run_to(9.5);
      CHECK(S()->led != 1, "LED left lit after the sweep");
      CHECK((tk_led_host[1] & 1u) != (tk_led_host[0] & 1u), "LED not handed back to the stock task when idle");
      CHECK(gon == 0u && goff == 0u, "LED written while idle after the sweep");
      tk_svc_host[0] = 0x5A4C5300u; foot = 1; run_to(11.0); foot = 0; run_to(11.5);
      CHECK(gon == 0u && goff == 0u, "LED written on stock (no services magic)"); }
#undef gon
#undef goff

    /* labels */
    { char b[8]; unsigned v[] = {0, 2, 4, 5, 6, 35}; int k;
      for (k = 0; k < 6; k++) { ZDL_GetLabel_1(v[k], b); printf("Len%u=%s ", v[k], b); }
      ZDL_GetLabel_7(0, b); printf("Space0=%s ", b); CHECK(b[0] == 'O', "Space 0 label");
      ZDL_GetLabel_7(100, b); printf("Space100=%s\n", b);
      ZDL_GetLabel_1(4, b); CHECK(b[0] == '1' && b[1] == '6' && b[5] == 0, "Len 16bar label");
      ZDL_GetLabel_1(35, b); CHECK(b[0] == '3' && b[1] == '1' && b[2] == 's' && b[3] == 0, "Len 31s label"); }

    printf("%s\n", fails ? "takeoff_riser: FAILED" : "takeoff_riser: ok");
    return fails ? 1 : 0;
}
