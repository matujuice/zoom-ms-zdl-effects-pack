/* KickKill clicks: Kill on / off / jumps and the footswitch must not step. The measure is the
 * largest sample-to-sample step of the output against the input's own, plus a small allowance.
 * Host test only. */
#include "kickkill_common.h"

typedef void (*change_fn)(void);
static double run_steps(double hz, int blocks, int at, change_fn change, double *in_step)
{
    long b, k = 0; int j; double worst = 0, istep = 0, pl = 0, pr = 0, pil = 0;
    for (b = 0; b < blocks; b++) {
        float l[8], r[8];
        if (b == at && change) change();
        for (j = 0; j < 8; j++, k++) l[j] = r[j] = (float)(0.5 * sin(6.283185307179586 * hz * (double)k / 44100.0));
        block(l, r);
        for (j = 0; j < 8; j++) {
            double dl = fabs(fx[j] - pl), dr = fabs(fx[j + 8] - pr), di = fabs(l[j] - pil);
            if (b >= 50) { if (dl > worst) worst = dl; if (dr > worst) worst = dr; if (di > istep) istep = di; }
            pl = fx[j]; pr = fx[j + 8]; pil = l[j];
        }
    }
    *in_step = istep;
    return worst;
}

static void k_off(void)  { SET(KILL, 0); }
static void k_20(void)   { SET(KILL, 1); }
static void k_300(void)  { SET(KILL, 281); }
static void sw_off(void) { params[0] = 0.0f; }
static void sw_on(void)  { params[0] = 1.0f; }

/* an instant Kill jump (preset recall) changes the filter at once, no glide: a small step is
 * expected there (the old DJFilter's Kill did the same) */
static double slack = 0.01;

static void check(const char *name, double hz, change_fn pre, change_fn change)
{
    double istep, w;
    setup();
    if (pre) pre();
    w = run_steps(hz, 1500, 600, change, &istep);
    printf("%s: largest output step %.4f (input %.4f)\n", name, w, istep);
    CHECK(w < istep * 1.5 + slack, "%s clicks: output step %.4f, input step %.4f", name, w, istep);
}

int main(void)
{
    long b, k; int j; double worst = 0, pl = 0, c;

    check("Kill OFF -> 300 Hz", 100.0, k_off, k_300);
    check("Kill 300 Hz -> OFF", 100.0, k_300, k_off);
    slack = 0.06;
    check("Kill 20 -> 300 Hz", 150.0, k_20, k_300);
    slack = 0.01;
    check("Kill 300 -> 20 Hz", 150.0, k_300, k_20);
    /* a fast hand turn, one step a block (20 -> 300 Hz in 51 ms) */
    {
        double istep = 0.5 * 6.283185307179586 * 150.0 / 44100.0, pr = 0; int v = 1;
        setup(); SET(KILL, 1); worst = 0;
        for (b = 0, k = 0; b < 1500; b++) {
            float l[8];
            if (b >= 600 && v < 281) SET(KILL, ++v);
            for (j = 0; j < 8; j++, k++) l[j] = (float)(0.5 * sin(6.283185307179586 * 150.0 * (double)k / 44100.0));
            block(l, l);
            for (j = 0; j < 8; j++) { double d = fabs(fx[j] - pr); if (b > 50 && d > worst) worst = d; pr = fx[j]; }
        }
        printf("Kill 20 -> 300 Hz by hand: largest step %.4f (input %.4f)\n", worst, istep);
        CHECK(worst < istep * 1.5 + 0.01, "Kill hand turn clicks (%.4f)", worst);
        worst = 0;
    }
    check("footswitch off -> on (Kill 300 Hz)", 100.0, k_300, sw_on);   /* on already: no change */
    {
        double istep, w;
        setup(); SET(KILL, 281); params[0] = 0.0f;
        w = run_steps(100.0, 1500, 600, sw_on, &istep);
        printf("footswitch on from off: largest step %.4f (input %.4f)\n", w, istep);
        CHECK(w < istep * 1.5 + 0.01, "footswitch on clicks");
    }
    check("footswitch on -> off (Kill 300 Hz)", 100.0, k_300, sw_off);
    /* on -> off -> on with a rest between: the filters restart from nothing, no pop */
    setup(); SET(KILL, 281);
    for (b = 0, k = 0; b < 3000; b++) {
        float l[8];
        if (b == 500) params[0] = 0.0f;
        if (b == 1500) params[0] = 1.0f;
        for (j = 0; j < 8; j++, k++) l[j] = (float)(0.5 * sin(6.283185307179586 * 700.0 * (double)k / 44100.0));
        block(l, l);
        for (j = 0; j < 8; j++) { double d = fabs(fx[j] - pl); if (b > 50 && d > worst) worst = d; pl = fx[j]; }
    }
    c = 0.5 * 6.283185307179586 * 700.0 / 44100.0;
    printf("on/off/on: largest step %.4f (input %.4f)\n", worst, c);
    CHECK(worst < c * 1.5 + 0.01, "on/off/on clicks (%.4f)", worst);
    return fails;
}
