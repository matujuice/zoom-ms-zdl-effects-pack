/* DJFilter filters: flat at 0 (and DJ -3..+3), the DJ low-pass and high-pass cutoffs and slopes, resonance following the sweep and staying stable, Kill at 24 dB/oct, Level, Mix and the
 * labels. Everything through the real entry. Host test only. */
#include "djfilter_common.h"

static double lp_fc(double a) { return 19000.0 * pow(2.0, -8.89 * a); }
static double hp_fc(double a) { return 20.0 * pow(500.0, a); }

/* DJ knob in -100..+100 -> the screen value 0..200; amount of a DJ value (|v| - 3) / 97 */
#define DJV(v) ((v) + 100)
#define AMT(v) (((v) - 3) / 97.0)

static void flat_case(const char *name, int dj)
{
    long b; int j; double err = 0; unsigned int rng = 12345u;
    setup(); SET(DJ, DJV(dj));
    for (b = 0; b < 400; b++) {
        float l[8], r[8], il[8], ir[8];
        for (j = 0; j < 8; j++) {
            rng = rng * 1664525u + 1013904223u; l[j] = il[j] = (float)(rng >> 8) * 1.1920929e-7f - 0.5f;
            rng = rng * 1664525u + 1013904223u; r[j] = ir[j] = (float)(rng >> 8) * 1.1920929e-7f - 0.5f;
        }
        block(l, r);
        for (j = 0; j < 8; j++) err += fabs(fx[j] - il[j]) + fabs(fx[j + 8] - ir[j]);
    }
    printf("%s: summed error %.3g\n", name, err);
    if (dj >= -3 && dj <= 3) CHECK(err == 0.0, "%s not flat", name);
    else CHECK(err > 0.0, "%s did nothing", name);
}

/* reference gain: filter knobs given, no resonance, no glide */
int main(void)
{
    int i, s;
    double a, g;
    static const int djs[] = { -3, -2, 0, 1, 3 };

    flat_case("defaults", 0);
    for (i = 0; i < 5; i++) flat_case("DJ flat zone", djs[i]);
    flat_case("DJ -4 (just outside)", -4);
    flat_case("DJ +4 (just outside)", 4);

    /* DJ low-pass (left) and high-pass (right) cutoffs, 24 and 12 dB/oct, no resonance, no glide */
    for (s = 0; s < 2; s++) {
        static const int amt[] = { 30, 50, 70, 100 };
        for (i = 0; i < 4; i++) {
            double fc;
            a = AMT(amt[i]);
            fc = lp_fc(a);
            setup(); SET(DJ, DJV(-amt[i])); SET(RESO, 0); SET(GLIDE, 0); SET(SLOPE, s);
            g = gain_at(fc, 1500, 3000);
            printf("LP %3d slope %d: fc %.0f Hz gain at fc %.3f\n", amt[i], s ? 24 : 12, fc, g);
            CHECK(fabs(g - (s ? 0.5 : 0.7071)) < 0.06, "LP %d slope %d gain at fc %.3f", amt[i], s ? 24 : 12, g);
            if (amt[i] < 100) {                                   /* 2 x fc: 24 dB 0.059, 12 dB 0.243 */
                g = gain_at(2.0 * fc, 1500, 3000);
                CHECK(fabs(g - (s ? 0.059 : 0.243)) < (s ? 0.02 : 0.05), "LP %d slope %d gain at 2 fc %.3f", amt[i], s ? 24 : 12, g);
            }
            fc = hp_fc(a);
            setup(); SET(DJ, DJV(amt[i])); SET(RESO, 0); SET(GLIDE, 0); SET(SLOPE, s);
            g = gain_at(fc, 1500, 3000);
            printf("HP %3d slope %d: fc %.0f Hz gain at fc %.3f\n", amt[i], s ? 24 : 12, fc, g);
            CHECK(fabs(g - (s ? 0.5 : 0.7071)) < 0.06, "HP %d slope %d gain at fc %.3f", amt[i], s ? 24 : 12, g);
        }
    }
    CHECK(fabs(lp_fc(1.0) - 40.0) < 1.0 && fabs(hp_fc(1.0) - 10000.0) < 100.0, "end points");

    /* resonance follows the sweep: the peak near the cutoff grows as the filter closes, is about
     * none when open, and stays bounded; a burst rings out (no self-oscillation) */
    {
        static const int amt[] = { 8, 22, 61, 100 };
        double prev = 0.0, pk[4];
        for (i = 0; i < 4; i++) {
            double fc = lp_fc(AMT(amt[i])), f, best = 0.0;
            for (s = 0; s < 14; s++) {
                f = fc * 0.6 * pow(1.1, (double)s);
                setup(); SET(DJ, DJV(-amt[i])); SET(RESO, 100); SET(GLIDE, 0);
                g = gain_at(f, 1500, 3000);
                if (g > best) best = g;
            }
            pk[i] = best;
            printf("LP %3d Reso 100: peak gain near fc %.2f (%.1f dB)\n", amt[i], best, 20.0 * log10(best));
        }
        CHECK(pk[0] < 1.1, "resonance not gone when open (%.2f)", pk[0]);
        for (i = 1; i < 4; i++) { CHECK(pk[i] > prev, "resonance does not rise as the filter closes"); prev = pk[i]; }
        CHECK(pk[3] > 1.8 && pk[3] < 8.0, "resonance at the far end out of range (%.2f)", pk[3]);
        for (s = 0; s < 4; s++) {                               /* LP / HP 100, Reso 100, both slopes: a burst dies away */
            long b; int j; double tail = 0;
            setup(); SET(DJ, (s & 1) ? 200 : 0); SET(RESO, 100); SET(GLIDE, 0); SET(SLOPE, s >> 1);
            for (b = 0; b < 30000; b++) {
                float l[8], r[8];
                for (j = 0; j < 8; j++) l[j] = r[j] = (b < 20) ? 0.8f * (float)sin(0.3 * (double)(b * 8 + j)) : 0.0f;
                block(l, r);
                if (b >= 29000) for (j = 0; j < 8; j++) tail += fabs(fx[j]);
            }
            printf("burst, %s 100 slope %d: tail %.3g\n", (s & 1) ? "HP" : "LP", (s >> 1) ? 24 : 12, tail);
            CHECK(tail < 1e-4, "burst still rings (self-oscillation?) tail %.3g", tail);
        }
        setup(); SET(DJ, 0); SET(GLIDE, 0);                    /* default Reso 30: a mild lift only */
        g = gain_at(45.0, 1500, 3000);
        CHECK(g < 2.0, "default resonance too strong (%.2f)", g);
    }

    /* Kill: 24 dB/oct low cut, independent of the filter */
    {
        double g100, g50, g25;
        setup(); SET(KILL, 81);                                 /* 100 Hz */
        g100 = gain_at(100.0, 1500, 3000); g50 = gain_at(50.0, 1500, 3000); g25 = gain_at(25.0, 1500, 3000);
        g = gain_at(1000.0, 1500, 3000);
        printf("Kill 100 Hz: 100 Hz %.3f, 50 Hz %.4f, 25 Hz %.4f, 1 kHz %.3f (slope %.1f dB/oct)\n",
               g100, g50, g25, g, 20.0 * log10(g50 / g25));
        CHECK(fabs(g100 - 0.5) < 0.06, "Kill gain at its frequency %.3f", g100);
        CHECK(g > 0.99, "Kill touches the highs %.3f", g);
        CHECK(g50 / g25 > 12.0 && g50 / g25 < 18.0, "Kill slope is not 24 dB/oct (%.1f dB)", 20.0 * log10(g50 / g25));
        CHECK(g25 < 0.01, "Kill leaves content below it (%.4f)", g25);
        setup(); SET(KILL, 1); g = gain_at(20.0, 1500, 6000);
        CHECK(fabs(g - 0.5) < 0.08, "Kill 20 Hz gain at fc %.3f", g);
        setup(); SET(KILL, 281); g = gain_at(300.0, 1500, 3000);
        CHECK(fabs(g - 0.5) < 0.06, "Kill 300 Hz gain at fc %.3f", g);
        setup(); SET(KILL, 81); SET(SLOPE, 0); SET(DJ, DJV(-32)); SET(RESO, 0); SET(GLIDE, 0);   /* with a 12 dB filter on: Kill is still 24 dB */
        g50 = gain_at(50.0, 1500, 3000); g25 = gain_at(25.0, 1500, 3000);
        CHECK(g50 / g25 > 12.0 && g50 / g25 < 18.0, "Kill slope changes with the filter (%.1f dB)", 20.0 * log10(g50 / g25));
    }

    /* Level and Mix */
    setup(); SET(LEVEL, 0);  g = gain_at(500.0, 100, 200); CHECK(fabs(g - 0.2512) < 0.002, "Level -12 dB gain %.4f", g);
    setup(); SET(LEVEL, 36); g = gain_at(500.0, 100, 200); CHECK(fabs(g - 1.9953) < 0.01, "Level +6 dB gain %.4f", g);
    setup(); SET(DJ, 0); SET(MIX, 0); SET(RESO, 0); g = gain_at(500.0, 500, 200); CHECK(fabs(g - 1.0) < 1e-6, "Mix 0 not dry (%.4f)", g);
    setup(); SET(DJ, 0); SET(MIX, 100); SET(RESO, 0); g = gain_at(500.0, 1500, 200); CHECK(g < 0.01, "Mix 100 not wet (%.4f)", g);
    setup(); SET(DJ, 0); SET(MIX, 50); SET(RESO, 0); g = gain_at(500.0, 1500, 200); CHECK(fabs(g - 1.0) < 0.02, "Mix 50 dry + wet (%.4f)", g);

    /* labels */
    {
        char t[16]; int n;
        n = ZDL_GetLabel_0(0, t);   CHECK(!strcmp(t, "-100") && n == 4, "DJ label 0 [%s]", t);
        n = ZDL_GetLabel_0(100, t); CHECK(!strcmp(t, "0") && n == 1, "DJ label 100 [%s]", t);
        n = ZDL_GetLabel_0(200, t); CHECK(!strcmp(t, "+100") && n == 4, "DJ label 200 [%s]", t);
        n = ZDL_GetLabel_2(0, t);   CHECK(!strcmp(t, "OFF"), "Kill label 0 [%s]", t);
        n = ZDL_GetLabel_2(1, t);   CHECK(!strcmp(t, "20"), "Kill label 1 [%s]", t);
        n = ZDL_GetLabel_2(281, t); CHECK(!strcmp(t, "300"), "Kill label 281 [%s]", t);
        n = ZDL_GetLabel_3(0, t);   CHECK(!strcmp(t, "12"), "Slope label 0 [%s]", t);
        n = ZDL_GetLabel_3(1, t);   CHECK(!strcmp(t, "24"), "Slope label 1 [%s]", t);
        n = ZDL_GetLabel_4(0, t);   CHECK(!strcmp(t, "0"), "Glide label 0 [%s]", t);
        n = ZDL_GetLabel_4(44, t);  CHECK(!strcmp(t, "20"), "Glide label 44 [%s]", t);
        n = ZDL_GetLabel_4(100, t); CHECK(!strcmp(t, "1000"), "Glide label 100 [%s]", t);
        n = ZDL_GetLabel_5(0, t);   CHECK(!strcmp(t, "-12.0"), "Level label 0 [%s]", t);
        n = ZDL_GetLabel_5(23, t);  CHECK(!strcmp(t, "-0.5"), "Level label 23 [%s]", t);
        n = ZDL_GetLabel_5(24, t);  CHECK(!strcmp(t, "0.0"), "Level label 24 [%s]", t);
        n = ZDL_GetLabel_5(36, t);  CHECK(!strcmp(t, "+6.0") && n == 4, "Level label 36 [%s]", t);
        (void)n;
    }
    return fails;
}
