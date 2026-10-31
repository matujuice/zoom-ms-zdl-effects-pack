/* Washout clicks: a steady sine through the wash (Send 100, wet only) must stay smooth. The largest second
 * difference of the output (a click shows as a spike there) stays within what the sine itself gives, with
 * Mod 0, 20 and 100, and with a synced Time whose BPM wobbles by +-0.3. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
#include "../src/custom/washout/washout.c"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define SET(n, v) params[WASHOUT_##n##_SLOT] = (float)(v) / 100.0f
#define ARENA 705536u
#define SEC 44100
#define LEN (SEC * 8)

static float oL[LEN];
static void fresh(int time, int wash, int fdbk, int mod)
{
    memset(se_m + 4096, 0, ARENA);
    params[0] = 1.0f;
    SET(TIME, time); SET(WASH, wash); SET(FDBK, fdbk); SET(HICUT, 0); SET(LOCUT, 0);
    SET(MOD, mod); SET(WIDTH, 100); SET(SEND, 100);
}
/* 330 Hz sine, 0.2; wobble != 0 moves the zmt BPM 120 +-0.3 every 50 ms */
static void run(int wobble)
{
    long b; int j;
    for (b = 0; b < LEN / 8; b++) {
        if (wobble) zt_host[5] = (unsigned)(12000 + ((b / 275) % 3 - 1) * 30);
        for (j = 0; j < 8; j++) { float x = 0.2f * sinf(0.047018f * (float)(b * 8 + j)); fx[j] = x; fx[j + 8] = x; }
        Fx_DLY_Washout(ctx);
        for (j = 0; j < 8; j++) oL[b * 8 + j] = fx[j];
    }
}
/* click score: largest second difference over what a clean 330 Hz sine of the output's peak can give
 * (w^2 * peak). The clean wash scores about 2 (the 16-bit ring adds a little noise); a click scores far above. */
static float score(long a, long b)
{
    float m = 0, pk = 0; long i;
    for (i = a + 2; i < b; i++) {
        float d = fabsf(oL[i] - 2.0f * oL[i - 1] + oL[i - 2]), x = fabsf(oL[i]);
        m = (d > m) ? d : m; pk = (x > pk) ? x : pk;
    }
    return m / (0.0022107f * pk);
}

int main(void)
{
    float c;
    se_init(ARENA);
    fresh(60, 70, 60, 0);   run(0); c = score(SEC * 2, LEN);
    printf("Mod 0: click score %.3f\n", c);
    CHECK(c < 3.0f, "Mod 0 clicks");
    fresh(60, 70, 60, 20);  run(0); c = score(SEC * 2, LEN);
    printf("Mod 20: click score %.3f\n", c);
    CHECK(c < 3.0f, "Mod 20 clicks");
    fresh(60, 100, 90, 100); run(0); c = score(SEC * 2, LEN);
    printf("Mod 100, Wash 100, Fdbk 90: click score %.3f\n", c);
    CHECK(c < 3.0f, "Mod 100 clicks");

    zt_host[0] = ZT_MAGIC; zt_host[1] = 2u; zt_host[2] = 0u; zt_host[3] = 0u; zt_host[4] = 0u; zt_host[5] = 12000u;
    fresh(107, 70, 60, 0); run(1); c = score(SEC * 2, LEN);
    printf("synced 1/8., BPM 120 +-0.3: click score %.3f\n", c);
    CHECK(c < 3.0f, "a wobbling BPM clicks");
    zt_host[0] = 0u;
    return fails;
}
