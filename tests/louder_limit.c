/* LOUDER limiter: the output never passes Ceil (the look-ahead does it, not the clip), no clicks,
 * linked stereo, transparent at Gain 0, no comb filter at Mix 50, release behaviour, labels. */
#define LOUDER_HOST_TEST
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
/* the live meter writes its band into a scratch cover; no firmware here, so no Event_post */
#define ZM_WANT_BLOB
static unsigned char zm_t_blob[2048];
#define ZM_PIC zm_t_blob
#define ZM_FW_OK() 0u
#define ZM_POST() ((void)0)
#include "../src/custom/louder/louder.c"
unsigned int lo_t_clips;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define SR 44096                       /* a whole number of 8-sample blocks */
#define MAXN (SR * 3)
static float inL[MAXN], inR[MAXN], outL[MAXN], outR[MAXN];
static LoState S;

/* knobs in screen units: Gain 0..180, Ceil 0..60, Rels 0..60, Style 0..2, Mix 0..100 */
static void run(int n, float gain, float ceil, float rels, float style, float mix)
{
    float u[5];
    LoParams P; int b, i;
    u[0] = gain; u[1] = ceil; u[2] = rels; u[3] = style; u[4] = mix;
    lo_init(&S);
    lo_t_clips = 0;
    for (b = 0; b < n / 8; b++) {
        float buf[16];
        for (i = 0; i < 8; i++) { buf[i] = inL[b * 8 + i]; buf[i + 8] = inR[b * 8 + i]; }
        lo_prepare(&P, &S, u);
        lo_process(&S, &P, buf);
        for (i = 0; i < 8; i++) { outL[b * 8 + i] = buf[i]; outR[b * 8 + i] = buf[i + 8]; }
    }
}
static float ceil_lin(float ui) { return powf(10.0f, (ui - 60.0f) * 0.1f / 20.0f); }
static float peak(const float *x, int n) { float m = 0; int i; for (i = 0; i < n; i++) if (fabsf(x[i]) > m) m = fabsf(x[i]); return m; }

static unsigned int rng = 12345;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }

/* samples (after 'from') where the gain is still under 99 %: the last one = the recovery time */
static int last_low(int from, int n, float gainlin)
{
    int i, last = from;
    for (i = from + 100; i < n; i++) {
        float x = inL[i - 64];
        if (fabsf(x) > 0.01f) { float g = outL[i] / (x * gainlin); if (g < 0.99f) last = i; }
    }
    return last - from;
}

int main(void)
{
    int n = SR, t, i, sty, r;

    /* ---- 1. the ceiling holds, and it is the look-ahead, not the clip, that holds it ---- */
    {
        const char *names[6] = { "burst", "impulse", "noise", "square", "L-only loud", "ramp" };
        float rels[3] = { 0, 1, 30 };
        for (t = 0; t < 6; t++) {
            for (i = 0; i < n; i++) {
                switch (t) {
                case 0: inL[i] = (i > 20000 && i < 22000 ? 0.95f : 0.1f) * sinf(0.2f * (float)i); inR[i] = inL[i]; break;
                case 1: inL[i] = (i == 30001) ? 0.99f : 0.02f * sinf(0.3f * (float)i); inR[i] = 0.01f * rnd(); break;
                case 2: inL[i] = 0.9f * rnd(); inR[i] = 0.9f * rnd(); break;
                case 3: inL[i] = ((i >> 6) & 1) ? 0.98f : -0.98f; inR[i] = -inL[i]; break;
                case 4: inL[i] = 0.9f * sinf(0.05f * (float)i); inR[i] = 0.001f * rnd(); break;
                default: inL[i] = 0.9f * (float)i / (float)n; inR[i] = 0.5f * inL[i]; break;
                }
            }
            for (sty = 0; sty < 3; sty++) {
                for (r = 0; r < 3; r++) {
                    float c = ceil_lin(50.0f), pk;                         /* -1.0 dB */
                    run(n, 180.0f * (t == 1 ? 0.5f : 0.67f), 50.0f, rels[r], (float)sty, 100.0f);
                    pk = peak(outL, n); if (peak(outR, n) > pk) pk = peak(outR, n);
                    CHECK(pk <= c * 1.00001f, "%s style %d rels %g: peak %.5f over ceiling %.5f", names[t], sty, rels[r], pk, c);
                    /* CLEAN and LOUD aim at the ceiling itself: the safety clip must not have been needed */
                    CHECK(sty == 1 || lo_t_clips == 0, "%s style %d rels %g: clip engaged %u times (look-ahead too late)", names[t], sty, rels[r], lo_t_clips);
                }
            }
        }
        printf("ceiling: 6 signals x 3 styles x 3 releases ok\n");
    }

    /* ---- 2. no clicks: no sample-to-sample jump above a sane bound ---- */
    for (t = 0; t < 2; t++) {
        float worst = 0, base = 0, w = t ? 0.02f : 0.1425f;
        int i0 = 20000, i1;
        while (sinf(w * (float)i0) > 0.0f || sinf(w * (float)(i0 + 1)) < 0.0f) i0++;   /* the burst starts at a zero crossing */
        i1 = i0 + (t ? 6000 : 3000);
        while (sinf(w * (float)i1) > 0.0f || sinf(w * (float)(i1 + 1)) < 0.0f) i1++;   /* and ends at one */
        for (i = 0; i < n; i++) {
            float a = (i > i0 && i <= i1) ? 0.95f : 0.2f;
            inL[i] = a * sinf(w * (float)i); inR[i] = inL[i];
        }
        for (i = 1; i < n; i++) { float d = fabsf(inL[i] - inL[i - 1]); if (d > base) base = d; }
        run(n, 150.0f, 59.0f, 0.0f, 0.0f, 100.0f);
        for (i = 65; i < n; i++) { float d = fabsf(outL[i] - outL[i - 1]); if (d > worst) worst = d; }
        printf("clicks (%s): biggest step %.4f (the input's own biggest step %.4f)\n", t ? "low sine" : "high sine", worst, base);
        /* the gain ramp (about 15 dB in 72 samples) adds a little slope on top of the signal's own; a click is 0.3 or more */
        CHECK(worst < base * 1.3f + 0.04f, "jump %.4f in the output", worst);
    }

    /* ---- 3. linked stereo: a loud left makes the right quieter by the same gain ---- */
    {
        float gmax = 0; int bad = 0;
        for (i = 0; i < n; i++) { inL[i] = (i > 15000 && i < 25000 ? 0.9f : 0.05f) * sinf(0.07f * (float)i); inR[i] = 0.05f * sinf(0.11f * (float)i + 0.5f); }
        run(n, 60.0f, 50.0f, 0.0f, 0.0f, 100.0f);                     /* +6 dB */
        for (i = 64; i < n; i++) {
            float xl = inL[i - 64], xr = inR[i - 64], gl, gr;
            if (fabsf(xl) < 0.02f || fabsf(xr) < 0.02f) continue;
            gl = outL[i] / (xl * 1.99526f); gr = outR[i] / (xr * 1.99526f);
            if (fabsf(gl - gr) > 1e-3f) bad++;
            if (1.0f - gr > gmax) gmax = 1.0f - gr;
        }
        printf("linked: right side reduced by up to %.2f (as a gain), L/R gain mismatches: %d\n", gmax, bad);
        CHECK(gmax > 0.3f, "R was not reduced by the L-only peak");
        CHECK(bad == 0, "L and R gains differ in %d samples", bad);
    }

    /* ---- 4. transparent: Gain 0, Mix 100, quiet signal = the input 64 samples later ---- */
    {
        float err = 0;
        for (i = 0; i < n; i++) { inL[i] = 0.1f * rnd(); inR[i] = 0.1f * rnd(); }
        run(n, 0.0f, 59.0f, 0.0f, 0.0f, 100.0f);
        for (i = 64; i < n; i++) { float e = fabsf(outL[i] - inL[i - 64]) + fabsf(outR[i] - inR[i - 64]); if (e > err) err = e; }
        for (i = 0; i < 64; i++) CHECK(outL[i] == 0.0f, "not silent before the look-ahead delay");
        printf("transparent: biggest difference to the delayed input %.2e\n", err);
        CHECK(err < 1e-6f, "not transparent: %.2e", err);
    }

    /* ---- 5. Mix 50: dry and wet are time-aligned (no comb); Mix 0 is the delayed dry ---- */
    {
        float err = 0, err0 = 0;
        for (i = 0; i < n; i++) { inL[i] = 0.01f * rnd(); inR[i] = 0.01f * rnd(); }
        run(n, 120.0f, 59.0f, 0.0f, 0.0f, 50.0f);                     /* +12 dB wet (3.98x), too quiet to limit */
        for (i = 64; i < n; i++) { float e = fabsf(outL[i] - inL[i - 64] * (1.0f + 3.98107f)); if (e > err) err = e; }
        printf("mix 50: biggest difference to dry + wet aligned %.2e\n", err);
        CHECK(err < 1e-5f, "dry and wet not aligned: %.2e", err);
        run(n, 120.0f, 59.0f, 0.0f, 0.0f, 0.0f);
        for (i = 64; i < n; i++) { float e = fabsf(outL[i] - inL[i - 64]); if (e > err0) err0 = e; }
        CHECK(err0 < 1e-6f, "Mix 0 is not the delayed dry: %.2e", err0);
        for (i = 0; i < n; i++) { inL[i] = 0.9f * sinf(0.05f * (float)i); inR[i] = inL[i]; }
        run(n, 120.0f, 50.0f, 0.0f, 0.0f, 50.0f);                     /* the ceiling clip holds at a part Mix */
        CHECK(peak(outL, n) <= ceil_lin(50.0f) * 1.00001f, "Mix 50: ceiling passed");
    }

    /* ---- 6. release: AUTO lets go quickly after one peak and slowly after a loud passage ---- */
    {
        int fast, slow, man;
        for (i = 0; i < MAXN; i++) { inL[i] = 0.02f * sinf(0.1f * (float)i); inR[i] = inL[i]; }
        inL[5000] = 0.95f; inR[5000] = 0.95f;                         /* one peak */
        run(SR * 2, 150.0f, 59.0f, 0.0f, 0.0f, 100.0f);
        fast = last_low(5000, SR * 2, 5.62341f);
        for (i = 0; i < MAXN; i++) { inL[i] = (i < SR ? 0.9f : 0.02f) * sinf(0.1f * (float)i); inR[i] = inL[i]; }
        run(SR * 3, 150.0f, 59.0f, 0.0f, 0.0f, 100.0f);
        slow = last_low(SR, SR * 3, 5.62341f);
        run(SR * 3, 150.0f, 59.0f, 1.0f, 0.0f, 100.0f);            /* Rels = 1 ms */
        man = last_low(SR, SR * 3, 5.62341f);
        printf("release to 99%%: AUTO after one peak %d samples (%.0f ms), AUTO after a loud second %d (%.0f ms), Rels 1 ms %d (%.1f ms)\n",
               fast, fast / 44.1f, slow, slow / 44.1f, man, man / 44.1f);
        CHECK(fast > 0 && slow > fast * 3, "AUTO is not program-dependent");
        CHECK(man < SR / 20, "Rels 1 ms too slow");
    }

    /* ---- 7. labels ---- */
    {
        char b[16];
        ZDL_GetLabel_0(123u, b); CHECK(!strcmp(b, "12.3"), "Gain label %s", b);
        ZDL_GetLabel_0(0u, b);   CHECK(!strcmp(b, "0.0"), "Gain label %s", b);
        ZDL_GetLabel_1(59u, b);  CHECK(!strcmp(b, "-0.1"), "Ceil label %s", b);
        ZDL_GetLabel_1(60u, b);  CHECK(!strcmp(b, "0.0"), "Ceil label %s", b);
        ZDL_GetLabel_1(0u, b);   CHECK(!strcmp(b, "-6.0"), "Ceil label %s", b);
        ZDL_GetLabel_2(0u, b);   CHECK(!strcmp(b, "AUTO"), "Rels label %s", b);
        ZDL_GetLabel_2(1u, b);   CHECK(!strcmp(b, "1"), "Rels label %s", b);
        ZDL_GetLabel_2(60u, b);  CHECK(!strcmp(b, "1000"), "Rels label %s", b);
        ZDL_GetLabel_3(1u, b);   CHECK(!strcmp(b, "PUNCH"), "Style label %s", b);
        printf("labels ok\n");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
