/* Scrub: delay in LIVE, steady freeze in HOLD, Position lands where it should, STOMP,
 * clearing after load, labels, and a long random-knob run (no NaN, bounded). */
#define SCRUB_HOST_TEST
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../src/custom/scrub/scrub.c"

static ScState S;          /* 529 KB: keep it off the stack */

/* the pedal entry's logic, minus the ctx plumbing; u = Pos Grain Rec Glide Spray Mix */
static void block(const float *u, int on, float *b)
{
    ScParams P;
    if (S.magic != SC_MAGIC) sc_init(&S);
    if (sc_clearing(&S)) return;
    sc_prepare(&P, u);
    if (!on) { S.was_off = 1; sc_bypassed(&S, P.mode, b, 8); return; }
    if (S.was_off) { sc_switched_on(&S, &P); S.was_off = 0; }
    sc_process(&S, &P, b, 8);
}

static int bad(float x) { return !(x == x) || x > 4.0f || x < -4.0f; }

static void fresh(void) { memset(&S, 0x7f, sizeof S); }     /* garbage arena */

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

int main(void)
{
    long t;
    int i;
    float b[8];
    printf("state %u bytes (arena >= 705536)\n", (unsigned)sizeof(ScState));
    CHECK(sizeof(ScState) < 705536u, "state too big");

    /* 1. clearing: dry while clearing, then a HOLD with nothing recorded is silent */
    {
        float u[6] = {400, 50, 1, 0, 0, 100};
        long dry_blocks = 0; float mx = 0;
        fresh();
        for (t = 0; t < 44100; t += 8) {
            for (i = 0; i < 8; i++) b[i] = 0.25f;
            block(u, 1, b);
            if (S.clr < SC_N || b[0] == 0.25f) dry_blocks++;
            else for (i = 0; i < 8; i++) if (fabsf(b[i]) > mx) mx = fabsf(b[i]);
        }
        printf("clearing: %ld dry blocks (%.1f ms), then HOLD on a cleared buffer peaks at %g\n",
               dry_blocks, dry_blocks * 8 / 44.1, mx);
        CHECK(mx == 0.0f, "stale arena data played");
    }

    /* 2. LIVE, Pos 100, Spray 0, Mix 100: a delay of exactly one grain length */
    {
        float u[6] = {400, 50, 0, 0, 0, 100};
        static float in[44100 * 3 + 8];
        float len = 1.25f * sc_grain_len(50), err = 0;
        int L = (int)len;
        fresh();
        for (t = 0; t < 44100 * 3; t += 8) {
            for (i = 0; i < 8; i++) { in[t + i] = 0.5f * sinf(2 * 3.14159265f * 440.0f * (t + i) / 44100.0f); b[i] = in[t + i]; }
            block(u, 1, b);
            for (i = 0; i < 8; i++) {
                long k = t + i;
                if (k > 44100) {
                    float fr = len - (float)L;
                    float want = in[k - L] + fr * (in[k - L - 1] - in[k - L]);
                    float e = fabsf(b[i] - want);
                    if (e > err) err = e;
                }
            }
        }
        printf("LIVE: 1.25 x grain = %.1f ms, wet = input delayed by it, max error %.4f\n", len / 44.1f, err);
        CHECK(err < 0.01f, "LIVE is not a clean delay");
    }

    /* 3. HOLD: freeze a 220 Hz tone, then 10 s of silence in: the tone rings on, steady */
    {
        float u[6] = {400, 50, 0, 40, 0, 100};
        float lo = 9, hi = 0, step = 0, prev = 0;
        fresh();
        for (t = 0; t < 44100 * 2; t += 8) {
            for (i = 0; i < 8; i++) b[i] = 0.5f * sinf(2 * 3.14159265f * 220.0f * (t + i) / 44100.0f);
            block(u, 1, b);
        }
        u[2] = 1;                                         /* HOLD */
        for (int w = 0; w < 20; w++) {
            double e = 0;
            for (t = 0; t < 22050; t += 8) {
                for (i = 0; i < 8; i++) b[i] = 0.0f;
                block(u, 1, b);
                for (i = 0; i < 8; i++) {
                    float d = fabsf(b[i] - prev);
                    if (w > 0 && d > step) step = d;
                    prev = b[i];
                    e += b[i] * b[i];
                }
            }
            e = sqrt(e / 22050);
            if (e < lo) lo = (float)e;
            if (e > hi) hi = (float)e;
        }
        printf("HOLD: 10 s after the input stopped, RMS per 0.5 s stays in %.3f..%.3f (tone 0.354), "
               "largest sample step %.4f (the tone's own 0.0157)\n", lo, hi, step);
        CHECK(lo > 0.25f && hi < 0.45f, "freeze level not steady");
        CHECK(step < 0.06f, "click in the freeze loop");
    }

    /* 3b. the seam on a tone whose period does not fit the loop (347 Hz in 100 ms): how
     * deep the level dips while the old and new voice cross, 5 ms RMS windows */
    {
        float u[6] = {400, 50, 0, 40, 0, 100};
        float lo = 9, hi = 0;
        fresh();
        for (t = 0; t < 44100 * 2; t += 8) {
            for (i = 0; i < 8; i++) b[i] = 0.5f * sinf(2 * 3.14159265f * 347.0f * (t + i) / 44100.0f);
            block(u, 1, b);
        }
        u[2] = 1;
        for (int w = 0; w < 400; w++) {
            double e = 0;
            for (t = 0; t < 224; t += 8) {
                for (i = 0; i < 8; i++) b[i] = 0.0f;
                block(u, 1, b);
                for (i = 0; i < 8; i++) e += b[i] * b[i];
            }
            e = sqrt(e / 224);
            if (w > 40) { if (e < lo) lo = (float)e; if (e > hi) hi = (float)e; }
        }
        printf("seam, 347 Hz frozen in a 100 ms loop: 5 ms RMS between %.3f and %.3f (tone 0.354)\n", lo, hi);
        CHECK(hi < 0.40f, "seam boosts the level");
    }

    /* 4. Position: 4 s of levels 0.0, 0.1 .. 0.7 (0.5 s each), HOLD, then park the head */
    {
        float pos[5] = {400, 320, 240, 160, 40};
        for (int k = 0; k < 5; k++) {
            float u[6] = {400, 0, 0, 0, 0, 100};     /* 10 ms grain, no smoothing */
            double acc = 0; long n = 0;
            fresh();
            for (t = 0; t < 44100 * 4 + 4400; t += 8) {
                for (i = 0; i < 8; i++) {
                    long q = t + i - 4400;                   /* the first 0.1 s is clearing + lead-in */
                    int seg = q < 0 ? 0 : (int)(q / 22050);
                    b[i] = 0.1f * (float)(seg > 7 ? 7 : seg);
                }
                block(u, 1, b);
            }
            u[2] = 1; u[0] = pos[k];
            for (t = 0; t < 44100; t += 8) {
                for (i = 0; i < 8; i++) b[i] = 0.0f;
                block(u, 1, b);
                if (t > 4410) for (i = 0; i < 8; i++) { acc += b[i]; n++; }
            }
            /* head is (100 - Pos)% of 4 s back from the freeze; the loop plays the 12.5 ms before it */
            {
                float back = (400.0f - pos[k]) * 0.01f + 0.006f;
                int seg = (int)((4.0f - back) * 2.0f);
                float want = 0.1f * (float)seg, got = (float)(acc / n);
                printf("Pos %3.0f: level %.3f, expected %.1f (%.2f s back)\n", pos[k], got, want, back);
                CHECK(fabsf(got - want) < 0.02f, "Position lands in the wrong place");
            }
        }
    }

    /* 5. STOMP: play while switched off (recorded, untouched), switch on: frozen sound */
    {
        float u[6] = {400, 70, 2, 40, 0, 100};
        double e = 0; int untouched = 1;
        fresh();
        for (t = 0; t < 44100 * 2; t += 8) {
            for (i = 0; i < 8; i++) b[i] = 0.4f * sinf(2 * 3.14159265f * 330.0f * (t + i) / 44100.0f);
            float c0 = b[3];
            block(u, 0, b);
            if (b[3] != c0) untouched = 0;
        }
        for (t = 0; t < 44100 * 2; t += 8) {
            for (i = 0; i < 8; i++) b[i] = 0.0f;
            block(u, 1, b);
            if (t > 4410) for (i = 0; i < 8; i++) e += b[i] * b[i];
        }
        e = sqrt(e / (44100 * 2 - 4410));
        printf("STOMP: off = untouched %s; on with silence in: RMS %.3f (tone 0.283)\n",
               untouched ? "yes" : "NO", e);
        CHECK(untouched, "switched-off effect changed the sound");
        CHECK(e > 0.18f && e < 0.36f, "STOMP did not freeze");
    }

    /* 6. scrubbing: sweep Pos 100 -> 0 -> 100 with Glide, Spray, all modes; then random knobs */
    {
        float u[6] = {400, 40, 0, 50, 30, 100};
        unsigned int r = 12345u;
        float mx = 0;
        fresh();
        for (t = 0; t < 44100 * 60; t += 8) {
            if (t % 4410 == 0) {
                if (t < 44100 * 10) { u[2] = 1; u[0] = (float)(int)(200 + 200 * cosf(t * 1e-5f)); }
                else for (int k = 0; k < 6; k++) {
                    r = r * 1664525u + 1013904223u;
                    if ((r >> 28) < 4) {
                        float mxk = (k == 2) ? 2.0f : (k == 0) ? 400.0f : 100.0f;
                        r = r * 1664525u + 1013904223u;
                        u[k] = (float)(int)((r >> 8) % (unsigned)(mxk + 1));
                    }
                }
            }
            for (i = 0; i < 8; i++) { r = r * 1664525u + 1013904223u; b[i] = ((int)(r >> 16) - 32768) * 2.4e-5f; }
            block(u, (t / 44100) % 7 != 3, b);
            for (i = 0; i < 8; i++) {
                if (bad(b[i])) { printf("FAIL: bad sample %g at %ld\n", b[i], t); return 1; }
                if (fabsf(b[i]) > mx) mx = fabsf(b[i]);
            }
        }
        printf("60 s of sweeps and random knobs/switching: no NaN, peak %.3f (input peak 0.79)\n", mx);
    }

    /* 7. labels */
    {
        char o[8];
        const unsigned int gv[3] = {0, 50, 100}, rv[4] = {0, 275, 399, 400};
        printf("Grain:");
        for (i = 0; i < 3; i++) { ZDL_GetLabel_1(gv[i], o); printf(" %u=%s", gv[i], o); }
        printf("   Pos:");
        for (i = 0; i < 4; i++) { ZDL_GetLabel_0(rv[i], o); printf(" %u=%s", rv[i], o); }
        printf("   Rec:");
        for (i = 0; i < 3; i++) { ZDL_GetLabel_2((unsigned)i, o); printf(" %s", o); }
        puts("");
        for (unsigned v = 0; v <= 100; v++) {
            int n1 = ZDL_GetLabel_1(v, o);
            CHECK(n1 <= 5 && (int)strlen(o) == n1, "Grain label %u too long: %s", v, o);
        }
        for (unsigned v = 0; v <= 400; v++) {
            int n0 = ZDL_GetLabel_0(v, o);
            CHECK(n0 <= 5 && (int)strlen(o) == n0, "Pos label %u too long: %s", v, o);
        }
        ZDL_GetLabel_1(0, o);   CHECK(!strcmp(o, "10ms"), "Grain 0 = %s", o);
        ZDL_GetLabel_1(100, o); CHECK(!strcmp(o, "1.0s"), "Grain 100 = %s", o);
        ZDL_GetLabel_0(0, o);   CHECK(!strcmp(o, "4.00s"), "Pos 0 = %s", o);
        ZDL_GetLabel_0(275, o); CHECK(!strcmp(o, "1.25s"), "Pos 275 = %s", o);
        ZDL_GetLabel_0(399, o); CHECK(!strcmp(o, "10ms"), "Pos 399 = %s", o);
        ZDL_GetLabel_0(400, o); CHECK(!strcmp(o, "0ms"), "Pos 400 = %s", o);
        /* the knob arrives as screen / 100: 2.75 must read as 275, not 3 */
        CHECK(sc_ui_pos(2.75f, 400.0f) == 275.0f, "Pos 2.75 read as %g", sc_ui_pos(2.75f, 400.0f));
        CHECK(sc_ui_pos(4.0f, 0.0f) == 400.0f, "Pos 4.00 read as %g", sc_ui_pos(4.0f, 0.0f));
    }

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
