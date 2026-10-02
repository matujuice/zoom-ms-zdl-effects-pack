/* S.GN_L host tests: clean passthrough, loss share and bursts, REPT pitch, edges, no NaN. */
#define SGNL_HOST_TEST
#include <stdio.h>
#include <math.h>
#include "../src/custom/sgnl/sgnl.c"

static SgState S;
static float knobs_def[9] = {30, 40, 30, 1, 30, 20, 0, 30, 100};

static void setup(SgParams *P, const float *u) { sg_prepare(P, u); sg_init(&S); }

/* run n blocks of a test signal; returns 1 on NaN */
static float sig(long t) { return 0.4f * sinf(6.2831853f * 220.0f * (float)t / 44100.0f); }

int main(void)
{
    int fails = 0;
    SgParams P;
    float b[8];

    /* 1. clean line: Loss 0, Codec 0, Jump 0, HIFI, Mix 100 -> output equals input */
    {
        float u[9] = {0, 40, 0, 1, 30, 0, 0, 30, 100};
        float err = 0.0f; long t = 0;
        setup(&P, u);
        for (int k = 0; k < 44100 / 8; k++) {
            for (int i = 0; i < 8; i++) b[i] = sig(t + i);
            sg_process(&S, &P, b, 8);
            for (int i = 0; i < 8; i++) { float d = fabsf(b[i] - sig(t + i)); if (d > err) err = d; }
            t += 8;
        }
        printf("clean line: max difference %.2e\n", err);
        if (err > 1e-5f) { puts("  FAIL: clean settings change the sound"); fails++; }
    }

    /* 2. lost share follows Loss and does not move much with Burst; Burst lengthens runs */
    for (int L = 0; L <= 100; L += 50) {
        for (int B = 0; B <= 100; B += 100) {
            float u[9] = {(float)L, 0, 0, 0, (float)B, 0, 0, 0, 100};
            long packets = 0, lost = 0, runs = 0; int prev = 0;
            setup(&P, u);
            for (int k = 0; k < 200000; k++) {
                for (int i = 0; i < 8; i++) b[i] = 0.3f;
                if (S.left <= 0) {         /* a packet starts in this block */
                    sg_process(&S, &P, b, 8);
                    packets++; lost += S.lost; if (S.lost && !prev) runs++; prev = S.lost;
                } else sg_process(&S, &P, b, 8);
            }
            float share = (float)lost / (float)packets;
            float want = 0.01f * L * (0.25f + 0.006f * L);
            float run = runs ? (float)lost / (float)runs : 0.0f;
            printf("Loss %3d Burst %3d: lost %5.1f %% (aim %4.1f %%), mean outage %.1f packets\n",
                   L, B, 100.0f * share, 100.0f * want, run);
            if (fabsf(share - want) > 0.03f) { puts("  FAIL: lost share off"); fails++; }
            if (L == 50 && B == 100 && run < 6.0f) { puts("  FAIL: Burst 100 should give long outages"); fails++; }
            if (L == 50 && B == 0 && run > 2.0f) { puts("  FAIL: Burst 0 should give short outages"); fails++; }
        }
    }

    /* 3. REPT at about 5 ms: during an outage the output repeats with the packet period */
    {
        float u[9] = {100, 23, 0, 1, 100, 0, 0, 0, 100};   /* Size 23 -> ~5 ms */
        static float out[44100];
        int pkb, per, ok = 0, checked = 0; long t = 0;
        setup(&P, u);
        pkb = P.pkb; per = pkb * 8;
        for (int k = 0; k < 44100 / 8; k++) {
            for (int i = 0; i < 8; i++) b[i] = sig(t + i) + 0.2f * sinf(0.0123f * (float)(t + i) * (float)(t + i) * 0.0001f);
            sg_process(&S, &P, b, 8);
            for (int i = 0; i < 8; i++) out[t + i] = S.lost ? b[i] : 1e9f;
            t += 8;
        }
        for (long i = 0; i + per < 44100; i++)
            if (out[i] < 1e8f && out[i + per] < 1e8f) { checked++; if (fabsf(out[i] - out[i + per]) < 1e-6f) ok++; }
        printf("REPT: packet %d samples = %.1f ms -> buzz at %.0f Hz; %d of %d samples repeat\n",
               per, per / 44.1f, 44100.0f / per, ok, checked);
        if (checked == 0 || ok < checked * 9 / 10) { puts("  FAIL: replay is not periodic"); fails++; }
    }

    /* 4. Edge 100 keeps the steps small at the packet edges (GAP, steady input) */
    {
        float u[9] = {60, 30, 0, 0, 30, 0, 0, 100, 100};
        float prev = 0.0f, big = 0.0f; long t = 0;
        setup(&P, u);
        for (int k = 0; k < 44100 / 8; k++) {
            for (int i = 0; i < 8; i++) b[i] = 0.5f;
            sg_process(&S, &P, b, 8);
            for (int i = 0; i < 8; i++) { if (t > 1000 && fabsf(b[i] - prev) > big) big = fabsf(b[i] - prev); prev = b[i]; t++; }
        }
        printf("Edge 100: largest step %.4f (input 0.5)\n", big);
        if (big > 0.01f) { puts("  FAIL: Edge 100 still clicks"); fails++; }
    }

    /* 4b. Edge 100 fades scale with Size: rise time (10% -> 90%) after a lost packet */
    for (int sz = 40; sz <= 100; sz += 60) {
        float u[9] = {60, (float)sz, 0, 0, 30, 0, 0, 100, 100};
        float prev = 0.0f; long t = 0, t10 = -1, best = 0;
        setup(&P, u);
        for (int k = 0; k < 4 * 44100 / 8; k++) {
            for (int i = 0; i < 8; i++) b[i] = 0.5f;
            sg_process(&S, &P, b, 8);
            for (int i = 0; i < 8; i++, t++) {
                if (b[i] < 0.05f) t10 = -1;
                else if (prev < 0.05f) t10 = t;
                if (t10 >= 0 && prev < 0.45f && b[i] >= 0.45f && t - t10 > best) best = t - t10;
                prev = b[i];
            }
        }
        printf("Edge 100 Size %3d: longest fade-in %.1f ms\n", sz, best / 44.1f);
        if (sz == 100 && best < 44 * 20) { puts("  FAIL: Edge 100 fades too short at Size 100"); fails++; }
    }

    /* 5. every mode, extreme knobs, noise input: no NaN, level bounded */
    {
        unsigned int r = 1;
        float worst = 0.0f;
        for (int fill = 0; fill <= 3; fill++)
        for (int line = 0; line <= 3; line++)
        for (int ex = 0; ex < 2; ex++) {
            float u[9] = {ex ? 100.0f : 30.0f, ex ? 0.0f : 100.0f, ex ? 100.0f : 60.0f, (float)fill,
                          ex ? 100.0f : 0.0f, 100, (float)line, ex ? 0.0f : 100.0f, 100};
            setup(&P, u);
            for (int k = 0; k < 20000; k++) {
                for (int i = 0; i < 8; i++) { r = r * 1103515245u + 12345u; b[i] = ((float)(int)(r >> 9) - 4194304.0f) * (0.9f / 4194304.0f); }
                if ((k & 4095) < 600) for (int i = 0; i < 8; i++) b[i] = 0.0f;   /* silences: clip path */
                sg_process(&S, &P, b, 8);
                for (int i = 0; i < 8; i++) {
                    if (!(b[i] == b[i])) { printf("  FAIL: NaN fill %d line %d\n", fill, line); return 1; }
                    if (fabsf(b[i]) > worst) worst = fabsf(b[i]);
                }
            }
        }
        printf("all modes, extreme knobs: peak %.2f (input peak 0.9)\n", worst);
        if (worst > 2.0f) { puts("  FAIL: level runs away"); fails++; }
    }

    /* 6. level: Codec and Line change the tone, not the loudness too much (Loss 0) */
    for (int line = 0; line <= 3; line++)
    for (int q = 0; q <= 100; q += 50) {
        float u[9] = {0, 40, (float)q, 1, 30, 0, (float)line, 30, 100};
        double ei = 0, eo = 0; long t = 0; unsigned int r = 7;
        setup(&P, u);
        for (int k = 0; k < 44100 / 8; k++) {
            for (int i = 0; i < 8; i++) {   /* guitar-ish: 220 Hz + harmonics + a little noise */
                float x = sig(t + i) + 0.5f * sig(2 * (t + i)) + 0.25f * sig(4 * (t + i));
                r = r * 1103515245u + 12345u; x += 0.02f * ((float)(int)(r >> 9) - 4194304.0f) * (1.0f / 4194304.0f);
                b[i] = x;
            }
            for (int i = 0; i < 8; i++) ei += b[i] * b[i];
            sg_process(&S, &P, b, 8);
            if (k > 400) for (int i = 0; i < 8; i++) eo += b[i] * b[i];
            t += 8;
        }
        float db = 10.0f * log10f((float)(eo / ei * 5512.0 / (5512.0 - 401.0)));
        printf("Line %d Codec %3d: level %+5.1f dB\n", line, q, db);
        if (db < -12.0f || db > 6.0f) { puts("  FAIL: level"); fails++; }
    }

    /* 7. labels */
    {
        char s[8]; int sz[] = {0, 23, 40, 100};
        for (int i = 0; i < 4; i++) { ZDL_GetLabel_1((unsigned)sz[i], s); printf("Size %3d -> %s\n", sz[i], s); }
        for (unsigned v = 0; v < 4; v++) { char a[8], c[8]; ZDL_GetLabel_3(v, a); ZDL_GetLabel_6(v, c); printf("%u: Fill %s, Line %s\n", v, a, c); }
    }
    (void)knobs_def;
    return fails ? 1 : 0;
}
