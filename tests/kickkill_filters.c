/* KickKill: the 24 dB/oct low cut at 20 / 100 / 300 Hz, the highs untouched, OFF and Mix 0
 * bit-exact, Mix 50 and the labels. Everything through the real entry. Host test only. */
#include "kickkill_common.h"

/* summed |out - in| of noise over 400 blocks, both sides */
static double noise_err(void)
{
    long b; int j; double err = 0; unsigned int rng = 12345u;
    for (b = 0; b < 400; b++) {
        float l[8], r[8], il[8], ir[8];
        for (j = 0; j < 8; j++) {
            rng = rng * 1664525u + 1013904223u; l[j] = il[j] = (float)(rng >> 8) * 1.1920929e-7f - 0.5f;
            rng = rng * 1664525u + 1013904223u; r[j] = ir[j] = (float)(rng >> 8) * 1.1920929e-7f - 0.5f;
        }
        block(l, r);
        for (j = 0; j < 8; j++) err += fabs(fx[j] - il[j]) + fabs(fx[j + 8] - ir[j]);
    }
    return err;
}

int main(void)
{
    double g, g100, g50, g25, e;
    char t[16]; int n;

    {
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
    }
    /* OFF and Mix 0 leave the signal bit-exact */
    setup(); SET(KILL, 0); e = noise_err(); printf("Kill OFF: summed error %.3g\n", e);
    CHECK(e == 0.0, "Kill OFF not bit-exact (%g)", e);
    setup(); SET(KILL, 81); SET(MIX, 0); e = noise_err(); printf("Mix 0: summed error %.3g\n", e);
    CHECK(e == 0.0, "Mix 0 not bit-exact (%g)", e);
    setup(); SET(KILL, 81); e = noise_err(); CHECK(e > 0.0, "Kill 100 Hz did nothing");
    /* Mix 50: dry 1 + wet 1, so the cut part comes back at full level */
    setup(); SET(KILL, 281); SET(MIX, 50); g = gain_at(25.0, 1500, 3000);
    CHECK(fabs(g - 1.0) < 0.02, "Mix 50 below the cut (%.4f)", g);
    setup(); SET(KILL, 281); SET(MIX, 50); g = gain_at(5000.0, 1500, 3000);
    CHECK(fabs(g - 2.0) < 0.02, "Mix 50 above the cut (%.4f)", g);

    n = ZDL_GetLabel_0(0, t);   CHECK(!strcmp(t, "OFF") && n == 3, "Kill label 0 [%s]", t);
    n = ZDL_GetLabel_0(1, t);   CHECK(!strcmp(t, "20") && n == 2, "Kill label 1 [%s]", t);
    n = ZDL_GetLabel_0(81, t);  CHECK(!strcmp(t, "100") && n == 3, "Kill label 81 [%s]", t);
    n = ZDL_GetLabel_0(281, t); CHECK(!strcmp(t, "300") && n == 3, "Kill label 281 [%s]", t);
    return fails;
}
