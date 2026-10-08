/* DubSiren: Trig SHold / SPuls wait for the next beat (on the MIDI clock's beats while the
 * transport runs, restarted by MIDI Start) and keep the LFO running; Hold / Pulse still start
 * at the press (with a synced Rate on a running clock the LFO stays on the clock). */
#include <stdio.h>
#include <stdlib.h>
#define DUBSIREN_HOST_TEST
#include "../src/custom/dubsiren/dubsiren.c"

static SirenState *s;
static float k[9], buf[8];
static long t;                         /* samples since the start */
static int gate;
static int run, start;           /* transport, as zt_update gives it */
static long flip;                 /* sample of the MIDI Start */
#define BEAT 22050L                                  /* 120 BPM */

static void block(int foot)
{
    SirenParams P;
    int i;
    for (i = 0; i < 8; i++) buf[i] = 0.0f;
    sr_prepare(s, &P, k, foot, run, run ? (float)(t - flip) / (float)BEAT : 0.0f, start);
    start = 0;
    sr_process(s, &P, buf, 8);
    gate = P.gate;
    t += 8;
}

/* run until sample `end`, return the first sample where the gate changed to `want` (-1 = never) */
static long until(long end, int foot, int want)
{
    long at = -1;
    while (t < end) { long t0 = t; block(foot); if (at < 0 && gate == want) at = t0; }
    return at;
}

static void setup(int trig)
{
    sr_init(s);
    k[0] = trig * 0.3333333f; k[1] = 0.0f; k[2] = 0.48f; k[3] = 103 * 0.008928571f;  /* Wail, Rate 1bar */
    k[4] = 0.34f; k[5] = 0.5f; k[6] = 0.4f; k[7] = 0.56f; k[8] = 120.0f;    /* 120 BPM */
    t = 0; run = 0; start = 0;
    until(200000, 0, 99);                           /* ring cleared, clock running */
}

static int fails;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

int main(void)
{
    long on, off;
    char b[8];
    int v;
    s = (SirenState *)calloc(1, sizeof(SirenState));

    /* SPuls: MIDI Start = downbeat, press 5000 samples later -> burst on the next beat */
    setup(3);
    run = 1; start = 1; flip = t; until(flip + 5000, 0, 99);
    on = until(flip + 2 * BEAT, 1, 1);
    printf("SPuls: Start at %ld, pressed at %ld, siren on at %ld (beat at %ld)\n", flip, flip + 5000, on, flip + BEAT);
    check("SPuls starts on the next beat", on >= flip + BEAT - 8 && on <= flip + BEAT + 8);
    printf("SPuls: LFO phase %.4f at %ld samples after Start (one bar = 1.0)\n", s->lfo_ph, t - flip);
    check("SPuls keeps the LFO on the bar (2 beats after Start = 0.5)", t - flip - 2 * BEAT < 8 && s->lfo_ph > 0.49f && s->lfo_ph < 0.51f);

    /* SHold: on and off on the beat */
    setup(2);
    run = 1; start = 1; flip = t; until(flip + 7000, 0, 99);
    on = until(flip + 2 * BEAT, 1, 1);
    off = until(flip + 2 * BEAT + 3000, 1, 0);
    off = until(flip + 4 * BEAT, 0, 0);
    printf("SHold: on at %ld, off at %ld\n", on, off);
    check("SHold starts on the next beat", on >= flip + BEAT - 8 && on <= flip + BEAT + 8);
    check("SHold stops on the beat after release", off >= flip + 3 * BEAT - 8 && off <= flip + 3 * BEAT + 8);

    /* Pulse, transport stopped: unchanged, starts at the press and restarts the LFO */
    setup(1);
    flip = t; until(flip + 5000, 0, 99);
    on = until(flip + 2 * BEAT, 1, 1);
    check("Pulse starts at the press", on >= flip + 5000 - 8 && on <= flip + 5000 + 8);
    printf("Pulse: LFO phase %.4f (restarted at the press: expect %.4f)\n", s->lfo_ph, (float)(t - on) / (4.0f * BEAT));
    check("Pulse restarts the LFO at the press", s->lfo_ph > (float)(t - on) / (4.0f * BEAT) - 0.002f
                                                && s->lfo_ph < (float)(t - on) / (4.0f * BEAT) + 0.002f);

    /* Pulse, transport running, Rate synced: starts at the press, the LFO stays on the clock */
    setup(1);
    run = 1; start = 1; flip = t; until(flip + 5000, 0, 99);
    on = until(flip + 2 * BEAT, 1, 1);
    check("Pulse on the clock starts at the press", on >= flip + 5000 - 8 && on <= flip + 5000 + 8);
    printf("Pulse on the clock: LFO phase %.4f (on the clock: expect %.4f)\n", s->lfo_ph, (float)(t - flip) / (4.0f * BEAT));
    check("Pulse on the clock keeps the LFO on the clock", s->lfo_ph > (float)(t - flip) / (4.0f * BEAT) - 0.002f
                                                        && s->lfo_ph < (float)(t - flip) / (4.0f * BEAT) + 0.002f);

    /* Hold: unchanged */
    setup(0);
    on = until(t + 4000, 1, 1);
    check("Hold starts at the press", on == 200000 || (on >= 199992 && on <= 200008));

    for (v = 0; v < 4; v++) { ZDL_GetLabel_0((unsigned)v, b); printf("Trig %d = %s\n", v, b); }
    ZDL_GetLabel_0(2u, b); check("label SHold", b[0] == 'S' && b[1] == 'H' && b[5] == 0);
    ZDL_GetLabel_0(3u, b); check("label SPuls", b[0] == 'S' && b[1] == 'P' && b[5] == 0);
    return fails ? 1 : 0;
}
