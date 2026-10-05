/* DualShft: synced delay times (Dly 101..112) land on the beat, free ones keep their ms,
 * labels, and a 1bar too long for the ring is halved. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#define DUALSHIFT_HOST_TEST
#include "../src/custom/dualshft/dualshft.c"

static int fails;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

/* knob values 0..1 like the entry passes: pitch 0 (screen 33), Depth 0, Mix 100 */
static void knobs(float *k, int dly, int bpm)
{
    k[0] = 33.0f / 66.0f; k[1] = 33.0f / 66.0f;
    k[2] = (float)dly / 112.0f; k[3] = (float)dly / 112.0f;
    k[4] = (float)bpm / 441.0f; k[5] = 0.5f; k[6] = 0.0f; k[7] = 0.0f; k[8] = 1.0f;
}

/* impulse response: sample index of the loudest output after the impulse */
static long echo_at(int dly, int bpm)
{
    static float buf[8];
    DualShift *s = (DualShift *)calloc(1, sizeof(DualShift));
    DualShiftParams P;
    float k[9], best = 0.0f;
    long t, at = -1, t0 = 0;
    int i;
    knobs(k, dly, bpm);
    ds_init(s);
    for (t = 0; t < 200000 + 200000; t += 8) {
        ds_prepare(s, &P, k);
        for (i = 0; i < 8; i++) buf[i] = 0.0f;
        if (s->clear_pos >= RING_SIZE && t0 == 0 && s->gA >= 1.0f) { t0 = t; buf[0] = 1.0f; }
        ds_process(s, &P, buf, 8);
        if (t0) for (i = 0; i < 8; i++) if (fabsf(buf[i]) > best && t + i > t0) { best = fabsf(buf[i]); at = t + i - t0; }
    }
    free(s);
    return at;
}

int main(void)
{
    char b[8];
    unsigned v;
    const char *want[12] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/4.", "1/2", "1bar"};
    for (v = 101; v <= 112; v++) {
        ZDL_GetLabel_2(v, b);
        printf("%u=%s ", v, b);
        if (strcmp(b, want[v - 101]) != 0) { printf("(want %s) ", want[v - 101]); fails++; }
    }
    printf("\n");
    ZDL_GetLabel_3(100u, b); check("Dly 100 still shows 1.00s", strcmp(b, "1.00s") == 0);
    ZDL_GetLabel_3(70u, b);  check("Dly 70 still shows 496ms", strcmp(b, "496ms") == 0);

    {
        long a = echo_at(109, 120), q = echo_at(101, 160), bar = echo_at(112, 160), f = echo_at(70, 120), lo = echo_at(112, 50);
        printf("1/4 @120: %ld (want 22050)  1/32 @160: %ld (want 2067)  1bar @160: %ld (want 66150)\n", a, q, bar);
        printf("free 70 (496ms): %ld (want 21874)  1bar @50 halved: %ld (want 105840)\n", f, lo);
        check("1/4 at 120 BPM = 22050 samples", labs(a - 22050) <= 1);
        check("1/32 at 160 BPM", labs(q - 2067) <= 1);
        check("1bar at 160 BPM", labs(bar - 66150) <= 1);
        check("free Dly 70 unchanged", labs(f - 21874) <= 1);
        check("1bar at 50 BPM halved to fit", labs(lo - 105840) <= 1);
    }
    return fails ? 1 : 0;
}
