/* Pump: labels, the three shapes, Targt routing, Shift, Div, and the reverb (host test).
 * Runs pu_prepare / pu_process directly on a heap state; checks levels, not sound. */
#define PUMP_HOST_TEST
#include "../src/custom/pump/pump.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

static PuState *s;
static float u[9];
static void knobs(float targ, float shape, float depth, float div, float shift, float curve, float verb, float size)
{
    u[0] = targ; u[1] = shape; u[2] = depth; u[3] = div; u[4] = shift; u[5] = curve; u[6] = verb;
    u[7] = 120.0f; u[8] = size;
}
static void reset(void) { memset(s, 0, sizeof *s); pu_init(s); while (s->clear_pos < PU_REV_LEN) pu_clear_step(s); }

#define BEAT 22050                       /* samples per beat at 120 BPM */
/* gain curve over n beats with a constant 0.5 input: g[k] = out / in at sample k */
static float *gain(int beats)
{
    static float g[BEAT * 8];
    PuParams P; float b[8]; int k, j;
    pu_prepare(&P, u);
    for (k = 0; k < BEAT * beats; k += 8) {
        for (j = 0; j < 8; j++) b[j] = 0.5f;
        pu_process(s, &P, b, 8);
        for (j = 0; j < 8; j++) g[k + j] = b[j] * 2.0f;
    }
    return g;
}
/* gain at a position in beats (bp counts from the first sample after reset) */
static float at(const float *g, float beat) { return g[(int)(beat * BEAT)]; }
/* lowest gain between two positions */
static float lo(const float *g, float b0, float b1) { int k; float m = 9; for (k = (int)(b0 * BEAT); k < (int)(b1 * BEAT); k++) if (g[k] < m) m = g[k]; return m; }

static void lab(int (*f)(unsigned, char *), unsigned v, const char *want)
{
    char b[8]; f(v, b);
    CHECK(strcmp(b, want) == 0, "label %u = %s, want %s", v, b, want);
}

int main(void)
{
    float *g, mx;
    int k, n;
    s = malloc(sizeof *s);

    lab(ZDL_GetLabel_0, 0, "DRY"); lab(ZDL_GetLabel_0, 1, "VERB"); lab(ZDL_GetLabel_0, 2, "BOTH"); lab(ZDL_GetLabel_0, 3, "SEND");
    lab(ZDL_GetLabel_1, 0, "DUCK"); lab(ZDL_GetLabel_1, 1, "GATE"); lab(ZDL_GetLabel_1, 2, "RISE");
    lab(ZDL_GetLabel_3, 0, "1/16"); lab(ZDL_GetLabel_3, 1, "1/8"); lab(ZDL_GetLabel_3, 2, "1/4");
    lab(ZDL_GetLabel_3, 3, "1/2"); lab(ZDL_GetLabel_3, 4, "BAR");
    lab(ZDL_GetLabel_4, 0, "0"); lab(ZDL_GetLabel_4, 7, "7"); lab(ZDL_GetLabel_4, 24, "24"); lab(ZDL_GetLabel_4, 25, "+1/16");
    lab(ZDL_GetLabel_4, 50, "+1/8"); lab(ZDL_GetLabel_4, 75, "+3/16"); lab(ZDL_GetLabel_4, 99, "99"); lab(ZDL_GetLabel_4, 100, "+1/4");

    /* DRY DUCK, depth 100, curve 50, no reverb: silent just after the beat, back by the end */
    reset(); knobs(0, 0, 100, 2, 0, 50, 0, 50); g = gain(4);
    printf("DUCK: low %.3f  +0.25 %.3f  +0.6 %.3f  +0.95 %.3f\n", lo(g, 1.0f, 1.1f), at(g, 1.25f), at(g, 1.6f), at(g, 1.95f));
    CHECK(lo(g, 1.0f, 1.1f) < 0.1f, "DUCK does not drop on the beat");
    CHECK(at(g, 1.25f) > 0.3f && at(g, 1.25f) < 0.9f, "DUCK not recovering");
    CHECK(at(g, 1.6f) > 0.99f && at(g, 1.95f) > 0.99f, "DUCK not back after Curve");
    /* depth 70 bottoms at 0.3 */
    reset(); knobs(0, 0, 70, 2, 0, 50, 0, 50); g = gain(4);
    CHECK(fabsf(lo(g, 2.0f, 2.1f) - 0.3f) < 0.06f, "DUCK depth 70 bottom %.3f, want 0.3", lo(g, 2.0f, 2.1f));

    /* GATE: open just after the beat, closed after Curve */
    reset(); knobs(0, 1, 100, 2, 0, 30, 0, 50); g = gain(4);
    printf("GATE: +0.05 %.3f  +0.25 %.3f  +0.5 %.3f  +0.95 %.3f\n", at(g, 1.05f), at(g, 1.25f), at(g, 1.5f), at(g, 1.95f));
    CHECK(at(g, 1.05f) > 0.99f && at(g, 1.25f) > 0.99f, "GATE not open after the beat");
    CHECK(at(g, 1.5f) < 0.01f && at(g, 1.95f) < 0.01f, "GATE not closed after Curve");

    /* RISE: quiet after the beat, loud just before the next */
    reset(); knobs(0, 2, 100, 2, 0, 50, 0, 50); g = gain(4);
    printf("RISE: +0.05 %.3f  +0.4 %.3f  +0.75 %.3f  +0.99 %.3f\n", at(g, 1.05f), at(g, 1.4f), at(g, 1.75f), at(g, 1.99f));
    CHECK(at(g, 1.05f) < 0.01f && at(g, 1.4f) < 0.01f, "RISE not quiet after the beat");
    CHECK(at(g, 1.75f) > 0.15f && at(g, 1.75f) < 0.4f, "RISE not growing");
    CHECK(at(g, 1.99f) > 0.9f, "RISE not up before the beat");

    /* Shift 50: the duck lands on the offbeat */
    reset(); knobs(0, 0, 100, 2, 50, 30, 0, 50); g = gain(4);
    printf("Shift 50: beat %.3f  offbeat %.3f\n", lo(g, 2.0f, 2.1f), lo(g, 2.5f, 2.6f));
    CHECK(lo(g, 2.0f, 2.1f) > 0.99f && lo(g, 2.5f, 2.6f) < 0.1f && lo(g, 2.4f, 2.5f) > 0.99f, "Shift 50 does not move the duck to the offbeat");
    /* Shift 25 = one 16th later */
    reset(); knobs(0, 0, 100, 2, 25, 20, 0, 50); g = gain(4);
    printf("Shift 25: 2.0-2.2 %.3f  2.25-2.35 %.3f\n", lo(g, 2.0f, 2.2f), lo(g, 2.25f, 2.35f));
    CHECK(lo(g, 2.0f, 2.2f) > 0.99f && lo(g, 2.25f, 2.35f) < 0.2f, "Shift 25 does not move the duck one 16th later");

    /* Div BAR: one duck per 4 beats; Div 1/16: four per beat */
    reset(); knobs(0, 0, 100, 4, 0, 5, 0, 50); g = gain(8);
    for (n = 0, k = 1; k < BEAT * 8; k++) if (g[k] < 0.5f && g[k - 1] >= 0.5f) n++;
    printf("Div BAR: %d ducks in 8 beats\n", n);
    CHECK(n == 2, "Div BAR gave %d ducks in 8 beats, want 2", n);
    reset(); knobs(0, 0, 100, 0, 0, 50, 0, 50); g = gain(2);
    for (n = 0, k = 1; k < BEAT * 2; k++) if (g[k] < 0.5f && g[k - 1] >= 0.5f) n++;
    CHECK(n == 8, "Div 1/16 gave %d ducks in 2 beats, want 8", n);

    /* Targt VERB and SEND leave the dry sound alone (Verb 0) */
    reset(); knobs(1, 0, 100, 2, 0, 50, 0, 50); g = gain(4);
    for (mx = 0, k = 0; k < BEAT * 4; k++) if (fabsf(g[k] - 1.0f) > mx) mx = fabsf(g[k] - 1.0f);
    CHECK(mx < 1e-5f, "VERB target changed the dry sound (%g)", mx);
    reset(); knobs(3, 1, 100, 2, 0, 50, 0, 50); g = gain(4);
    for (mx = 0, k = 0; k < BEAT * 4; k++) if (fabsf(g[k] - 1.0f) > mx) mx = fabsf(g[k] - 1.0f);
    CHECK(mx < 1e-5f, "SEND target changed the dry sound (%g)", mx);
    /* Depth 0: untouched for every shape */
    for (k = 0; k < 3; k++) {
        int j; reset(); knobs(2, (float)k, 0, 2, 0, 50, 0, 50); g = gain(2);
        for (mx = 0, j = 0; j < BEAT * 2; j++) if (fabsf(g[j] - 1.0f) > mx) mx = fabsf(g[j] - 1.0f);
        CHECK(mx < 1e-5f, "Depth 0 shape %d changed the sound (%g)", k, mx);
    }

    /* Reverb: noise in, Verb 100, Size 0 / 50 / 100: finite, bounded, a tail after the input stops */
    for (k = 0; k <= 2; k++) {
        PuParams P; float b[8], ein = 0, eout = 0, peak = 0, tail = 0; int i, j; unsigned r = 1;
        reset(); knobs(1, 0, 0, 2, 0, 50, 100, (float)(k * 50)); pu_prepare(&P, u);
        for (i = 0; i < 44100 * 20; i += 8) {
            for (j = 0; j < 8; j++) { r = r * 1664525u + 1013904223u; b[j] = (i < 44100 * 10) ? ((float)(int)(r >> 9) * 1.1920929e-7f - 1.0f) * 0.5f : 0.0f; }
            for (j = 0; j < 8; j++) ein += b[j] * b[j];
            pu_process(s, &P, b, 8);
            for (j = 0; j < 8; j++) {
                float y = b[j];
                if (!(y == y) || fabsf(y) > 100.0f) { peak = 1e9f; break; }
                if (fabsf(y) > peak) peak = fabsf(y);
                if (i < 44100 * 10) eout += y * y; else if (i < 44100 * 10 + 4410) tail += y * y;
            }
        }
        printf("reverb Size %d: out/in rms %.3f (dry included), peak %.3f, tail rms %.4f\n", k * 50, sqrtf(eout / ein), peak, sqrtf(tail / 4410));
        CHECK(peak < 4.0f, "reverb Size %d peak %.3f", k * 50, peak);
        CHECK(tail > 1e-4f, "reverb Size %d has no tail", k * 50);
    }
    /* Size 100 tail lasts longer than Size 0: energy 1 s after the input stops */
    {
        float t[2]; int q;
        for (q = 0; q < 2; q++) {
            PuParams P; float b[8]; int i, j; unsigned r = 7; t[q] = 0;
            reset(); knobs(1, 0, 0, 2, 0, 50, 100, (float)(q * 100)); pu_prepare(&P, u);
            for (i = 0; i < 44100 * 3; i += 8) {
                for (j = 0; j < 8; j++) { r = r * 1664525u + 1013904223u; b[j] = (i < 44100) ? ((float)(int)(r >> 9) * 1.1920929e-7f - 1.0f) * 0.5f : 0.0f; }
                pu_process(s, &P, b, 8);
                if (i >= 44100 * 2) for (j = 0; j < 8; j++) t[q] += b[j] * b[j];
            }
        }
        printf("tail energy 1..2 s after the input stops: Size 0 %.3g, Size 100 %.3g\n", t[0], t[1]);
        CHECK(t[1] > t[0] * 10.0f, "Size 100 tail not longer than Size 0");
    }

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
