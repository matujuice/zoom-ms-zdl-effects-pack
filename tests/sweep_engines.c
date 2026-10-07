/* Sweep: what each engine does to a sine at set frequencies (Depth 0 = parked), Mix, level,
 * stability at the extremes with noise, and that a sweep really moves. Host test only. */
#include "sweep_common.h"

static float gain_at(float hz, float amp)
{
    long b; int j; double si = 0, so = 0; long n = 0; unsigned long k = 0;
    for (b = 0; b < 6000; b++) {
        for (j = 0; j < 8; j++, k++) { fx[j] = amp * sinf(6.2831853f * hz * (float)(k % 441000) / 44100.0f); fx[j + 8] = fx[j]; }
        {   float in[8]; memcpy(in, fx, sizeof in);
            Fx_DLY_Sweep(ctx);
            if (b >= 3000) for (j = 0; j < 8; j++) { si += in[j] * in[j]; so += fx[j] * fx[j]; n++; } }
    }
    return (float)sqrt(so / si);
}

static void engine(int type)
{
    setup(); SET(TYPE, type); SET(DEPTH, 0); SET(RESO, 0); SET(TONE, 0); SET(MIX, 100); SET(CNTR, 52);
}

static int minima(float lo, float hi, int pts, float *deepest, float *top)
{
    int i, count = 0; float g[200], prev;
    *deepest = 9.0f; *top = 0.0f;
    for (i = 0; i < pts; i++) {
        float hz = lo * powf(hi / lo, (float)i / (float)(pts - 1));
        g[i] = gain_at(hz, 0.2f);
        if (g[i] < *deepest) *deepest = g[i];
        if (g[i] > *top) *top = g[i];
    }
    prev = g[0];
    for (i = 1; i < pts - 1; i++) { if (g[i] < g[i - 1] && g[i] <= g[i + 1] && g[i] < 0.35f) count++; }
    (void)prev;
    return count;
}

static unsigned int rng = 777u;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 9) * 2.3841858e-7f - 1.0f; }

int main(void)
{
    float f0 = 80.0f * exp2f(52 * 0.0697f), g1, g2, g3, deepest, top;
    int t, sh, n;

    /* filters at Cntr 52 (cutoff about 990 Hz) */
    engine(4); g1 = gain_at(100, 0.2f); g2 = gain_at(8000, 0.2f);
    printf("LP cutoff %.0f Hz: 100 Hz %.3f, 8 kHz %.4f\n", f0, g1, g2);
    CHECK(g1 > 0.95f && g1 < 1.05f && g2 < 0.05f, "LP response");
    engine(6); g1 = gain_at(100, 0.2f); g2 = gain_at(8000, 0.2f);
    printf("HP: 100 Hz %.4f, 8 kHz %.3f\n", g1, g2);
    CHECK(g2 > 0.95f && g2 < 1.05f && g1 < 0.05f, "HP response");
    engine(5); SET(RESO, 60); g1 = gain_at(f0, 0.2f); g2 = gain_at(100, 0.2f); g3 = gain_at(9000, 0.2f);
    printf("BP: centre %.3f, 100 Hz %.3f, 9 kHz %.3f\n", g1, g2, g3);
    CHECK(g1 > 0.9f && g1 < 1.3f && g2 < 0.25f && g3 < 0.25f, "BP response");
    engine(7); SET(RESO, 100); g1 = gain_at(f0, 0.2f); g2 = gain_at(100, 0.2f); g3 = gain_at(9000, 0.2f);
    printf("NTCH: centre %.3f, 100 Hz %.3f, 9 kHz %.3f\n", g1, g2, g3);
    CHECK(g1 < 0.2f && g2 > 0.9f && g3 > 0.9f, "notch response");
    engine(4); SET(RESO, 100); g1 = gain_at(f0, 0.05f);
    printf("LP at full Reso, peak %.2f (%.1f dB)\n", g1, 20 * log10f(g1));
    CHECK(g1 > 2.0f && g1 < 8.0f, "LP resonance peak out of range");

    /* phaser: notches between 100 Hz and 10 kHz, 2 for 4 stages, 4 for 8 stages */
    engine(0); n = minima(100, 10000, 80, &deepest, &top);
    printf("PH 4: %d notches, deepest %.3f, top %.3f\n", n, deepest, top);
    CHECK(n == 2 && deepest < 0.1f && top > 0.95f && top < 1.05f, "PH 4 notches");
    engine(1); n = minima(100, 10000, 160, &deepest, &top);
    printf("PH 8: %d notches, deepest %.3f, top %.3f\n", n, deepest, top);
    CHECK(n == 4 && deepest < 0.1f && top > 0.95f && top < 1.05f, "PH 8 notches");

    /* flanger: delay 352.8 x 2^(-0.0474 x 52) samples, first notch at fs / (2 d) */
    {
        float d = 352.8f * exp2f(-0.0474f * 52), nh = 44100.0f / (2.0f * d);
        engine(2); g1 = gain_at(nh, 0.2f); g2 = gain_at(2.0f * nh, 0.2f);
        printf("FL +: delay %.1f samples, notch %.0f Hz gain %.3f, comb peak %.3f\n", d, nh, g1, g2);
        CHECK(g1 < 0.1f && g2 > 0.9f, "flanger comb");
    }

    /* Mix 0 is the untouched input (every type), and Mix 100 has a sane level on noise */
    for (t = 0; t < 8; t++) {
        long b; int j; int same = 1;
        setup(); SET(TYPE, t); SET(MIX, 0);
        for (b = 0; b < 400; b++) {
            float in[8];
            for (j = 0; j < 8; j++) { fx[j] = 0.3f * noise(); fx[j + 8] = fx[j]; in[j] = fx[j]; }
            Fx_DLY_Sweep(ctx);
            for (j = 0; j < 8; j++) if (fx[j] != in[j]) same = 0;
        }
        CHECK(same, "type %d: Mix 0 is not the input", t);
    }
    for (t = 0; t < 8; t++) {
        long b; int j; double si = 0, so = 0;
        setup(); SET(TYPE, t); SET(MIX, 100);
        for (b = 0; b < 8000; b++) {
            float in[8];
            for (j = 0; j < 8; j++) { fx[j] = 0.3f * noise(); fx[j + 8] = fx[j]; in[j] = fx[j]; }
            Fx_DLY_Sweep(ctx);
            if (b >= 1000) for (j = 0; j < 8; j++) { si += in[j] * in[j]; so += fx[j] * fx[j]; }
        }
        printf("type %d defaults on noise: level %.2f x\n", t, sqrt(so / si));
        CHECK(sqrt(so / si) > 0.25 && sqrt(so / si) < 3.0, "type %d level %.2f x", t, sqrt(so / si));
    }

    /* every type x shape, everything up, loud noise, free and synced rate: finite and bounded */
    for (t = 0; t < 8; t++) for (sh = 0; sh < 6; sh++) {
        long b; int j, bad = 0; float peak = 0.0f;
        setup(); SET(TYPE, t); SET(SHAPE, sh); SET(DEPTH, 100); SET(RESO, 100); SET(TONE, 100);
        SET(RATE, (sh & 1) ? 100 : 101); SET(CNTR, (sh * 20) % 101); SET(MIX, 50 + 10 * (sh & 1));
        for (b = 0; b < 30000; b++) {
            for (j = 0; j < 8; j++) { fx[j] = 0.9f * noise(); fx[j + 8] = fx[j]; }
            Fx_DLY_Sweep(ctx);
            for (j = 0; j < 16; j++) { if (!(fx[j] == fx[j]) || fabsf(fx[j]) > 20.0f) bad = 1; if (fabsf(fx[j]) > peak) peak = fabsf(fx[j]); }
        }
        CHECK(!bad, "type %d shape %d: not finite or above 20 (peak %.1f)", t, sh, peak);
        if (sh == 0) printf("type %d worst-case peak %.2f\n", t, peak);
    }

    /* a sweep moves: LP at Depth 100, fast free rate: the gain at 1 kHz changes a lot */
    {
        long b; int j; float lo = 9, hi = 0;
        setup(); SET(TYPE, 4); SET(RATE, 60); SET(DEPTH, 100); SET(RESO, 0); SET(TONE, 0); SET(MIX, 100);
        for (b = 0; b < 44100; b++) {
            double si = 0, so = 0; float in[8];
            for (j = 0; j < 8; j++) { fx[j] = 0.2f * sinf(6.2831853f * 1000.0f * (float)((b * 8 + j) % 44100) / 44100.0f); fx[j + 8] = fx[j]; in[j] = fx[j]; }
            Fx_DLY_Sweep(ctx);
            for (j = 0; j < 8; j++) { si += in[j] * in[j]; so += fx[j] * fx[j]; }
            if (b % 16 == 0 && b > 5000) { float g = (float)sqrt(so / si); if (g < lo) lo = g; if (g > hi) hi = g; }
        }
        printf("LP swept past a 1 kHz tone: gain %.2f .. %.2f\n", lo, hi);
        CHECK(hi > 0.8f && lo < 0.3f, "the sweep does not move (%.2f .. %.2f)", lo, hi);
    }

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
