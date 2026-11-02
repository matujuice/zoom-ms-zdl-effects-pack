/* DJFilter clicks: fast knob jumps (DJ, Kill, Slope) and footswitch on / off must not step.
 * The measure is the largest sample-to-sample step of the output against the largest step the
 * input itself has, plus a small allowance. Host test only. */
#include "djfilter_common.h"

/* run `blocks` blocks of a sine (hz, amp); at block `at` call change(). Returns the largest
 * output step after the first 50 blocks (state settled), both sides. */
typedef void (*change_fn)(void);
static double run_steps(double hz, double amp, int blocks, int at, change_fn change, double *in_step)
{
    long b, k = 0; int j; double worst = 0, istep = 0, pl = 0, pr = 0, pil = 0;
    for (b = 0; b < blocks; b++) {
        float l[8], r[8];
        if (b == at && change) change();
        for (j = 0; j < 8; j++, k++) l[j] = r[j] = (float)(amp * sin(6.283185307179586 * hz * (double)k / 44100.0));
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

static void dj_left(void)   { SET(DJ, 0); }
static void dj_right(void)  { SET(DJ, 200); }
static void kill_on(void)   { SET(KILL, 281); }
static void slope_12(void)  { SET(SLOPE, 0); }
static void dj_back(void)   { SET(DJ, 100); }
static void sw_off(void)    { params[0] = 0.0f; }
static void lp_sw_off(void) { SET(DJ, 0); params[0] = 0.0f; }
static void sw_on(void)     { params[0] = 1.0f; }

static void check(const char *name, double hz, double amp, int blocks, int at, change_fn pre, change_fn change)
{
    double istep, w;
    setup();
    if (pre) pre();
    w = run_steps(hz, amp, blocks, at, change, &istep);
    printf("%s: largest output step %.4f (input %.4f)\n", name, w, istep);
    CHECK(w < istep * 1.5 + 0.01, "%s clicks: output step %.4f, input step %.4f", name, w, istep);
}

static void kill_dj(void)   { SET(KILL, 100); SET(DJ, 150); }

int main(void)
{
    double istep, w, c;
    long b, k;
    int j;

    check("DJ 0 -> -100 (100 Hz)", 100.0, 0.5, 1500, 400, 0, dj_left);
    check("DJ 0 -> -100 (1 kHz)", 1000.0, 0.5, 1500, 400, 0, dj_left);
    check("DJ 0 -> +100 (1 kHz)", 1000.0, 0.5, 1500, 400, 0, dj_right);
    check("DJ 0 -> +100 (100 Hz)", 100.0, 0.5, 1500, 400, 0, dj_right);
    check("DJ 0 -> -100", 500.0, 0.5, 1500, 400, 0, dj_left);
    check("DJ 0 -> +100", 500.0, 0.5, 1500, 400, 0, dj_right);
    check("DJ -100 -> +100", 500.0, 0.5, 1500, 600, dj_left, dj_right);
    check("DJ -100 -> 0", 100.0, 0.5, 1500, 600, dj_left, dj_back);
    check("Kill OFF -> 300 Hz", 200.0, 0.5, 1500, 400, 0, kill_on);
    check("Slope 24 -> 12 with DJ -100", 800.0, 0.5, 1500, 400, dj_left, slope_12);
    check("Kill + DJ jump together", 400.0, 0.5, 1500, 400, 0, kill_dj);

    /* footswitch: off -> on and on -> off with the filter closed, so the sound changes a lot;
     * the ramp is 128 samples, so the step stays a small multiple of the input's own step */
    check("footswitch off -> on (DJ -100)", 300.0, 0.5, 1000, 400, lp_sw_off, sw_on);
    {
        setup(); SET(DJ, 0); params[0] = 0.0f;                  /* off from the start, then on */
        w = run_steps(300.0, 0.5, 1000, 400, sw_on, &istep);
        printf("footswitch on from the start: largest step %.4f (input %.4f)\n", w, istep);
        CHECK(w < istep * 1.5 + 0.01, "footswitch on clicks");
    }
    {
        setup(); SET(DJ, 0); SET(KILL, 50);
        w = run_steps(300.0, 0.5, 1000, 400, sw_off, &istep);
        printf("footswitch on -> off: largest step %.4f (input %.4f)\n", w, istep);
        CHECK(w < istep * 1.5 + 0.01, "footswitch off clicks");
        /* once off the buffer is the input, untouched */
        {
            float l[8], r[8], err = 0;
            for (j = 0; j < 8; j++) l[j] = r[j] = 0.1f * (float)(j + 1);
            block(l, r);
            for (j = 0; j < 8; j++) err += fabsf(fx[j] - l[j]) + fabsf(fx[j + 8] - r[j]);
            CHECK(err == 0.0f, "off but the buffer changed (%g)", err);
        }
    }
    /* on -> off -> on again with a rest between: the filters restart from the signal, no pop */
    {
        double worst = 0, pl = 0; int ph = 0;
        setup(); SET(DJ, 40); SET(KILL, 60);
        for (b = 0, k = 0; b < 3000; b++) {
            float l[8];
            if (b == 500) params[0] = 0.0f;
            if (b == 1500) params[0] = 1.0f;
            for (j = 0; j < 8; j++, k++) l[j] = (float)(0.5 * sin(6.283185307179586 * 700.0 * (double)k / 44100.0));
            block(l, l);
            for (j = 0; j < 8; j++) { double d = fabs(fx[j] - pl); if (b > 50 && d > worst) worst = d; pl = fx[j]; }
            (void)ph;
        }
        c = 0.5 * 6.283185307179586 * 700.0 / 44100.0;
        printf("on/off/on: largest step %.4f (input %.4f)\n", worst, c);
        CHECK(worst < c * 1.5 + 0.01, "on/off/on clicks (%.4f)", worst);
    }
    return fails;
}
