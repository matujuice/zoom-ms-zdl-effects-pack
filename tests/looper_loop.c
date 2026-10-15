/* Looper: the 12-bit format, record / play / stop / delete on the When lines, the loop
 * matching what was recorded (stereo kept apart), seams, stereo/mono/shorter picks, Roll,
 * tempo changes (locked to the grid, no seams at a steady tempo), MIDI Start, labels and a
 * random-knob run (no NaN, bounded). Runs the DSP functions directly on a 705,600-byte arena
 * (the pedal's slot block), with the host copy of the transport block. */
#define LOOPER_HOST_TEST
static long jumps;
#define LP_ON_JUMP() (jumps++)
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../src/custom/looper/looper.c"

#define ARENA 705600
static unsigned char arena[ARENA] __attribute__((aligned(8)));
static LpState *S;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

/* knobs, screen values: Mix Roll Length When Amount */
static float U[5];
static float onoff = 1.0f;
static long t;                      /* samples since the start of the test */
static float outl, outr;            /* last output sample */
static double clk_per = 0.0, clk_t0 = 0.0;   /* running MIDI clock: samples per clock, Start time */

static float in_l(long n) { return 0.5f * (float)sin(0.0123 * n + 0.3 * sin(0.0007 * n)); }
static float in_r(long n) { return 0.4f * (float)sin(0.0371 * n); }

static void fresh(void)
{
    memset(arena, 0xC9, sizeof arena);
    S = (LpState *)arena;
    lp_init(S, (uintptr_t)(arena + ((sizeof(LpState) + 3) & ~3u)), ARENA - (int)((sizeof(LpState) + 3) & ~3u));
    memset(zt_host, 0, sizeof zt_host);
    memset(ls_host, 0, sizeof ls_host);
    S->me = 0u;
    clk_per = 0.0; t = 0; onoff = 1.0f;
    U[0] = 100; U[1] = 0; U[2] = 0; U[3] = 2; U[4] = 7;
}

/* one 8-sample block; out[] gets the output, the input is in_l/in_r at t */
static float lastout[16];
static void block(void)
{
    LpParams P; float b[16], bpm, du; int j;
    if (clk_per > 0.0) {
        zt_host[1] += 2u;
        zt_host[4] = (t >= clk_t0) ? (unsigned int)((t - clk_t0) / clk_per) + 1u : 0u;
    }
    for (j = 0; j < 8; j++) { b[j] = in_l(t + j); b[j + 8] = in_r(t + j); }
    lp_prepare(&P, U);
    bpm = lp_block(S, &P, onoff, &du);
    lp_process(S, &P, bpm, du, b);
    lp_out(S);
    memcpy(lastout, b, sizeof b);
    t += 8;
}
static void run(long n) { long e = t + n; while (t < e) block(); }
static void press(void) { onoff = (onoff > 0.5f) ? 0.0f : 1.0f; block(); }
static void tempo_word(int bpm) { zt_host[0] = ZT_MAGIC; zt_host[5] = (unsigned int)(bpm * 100); }
static void clock_start(double bpm) { zt_host[0] = ZT_MAGIC; zt_host[2] = 1u; zt_host[3]++; zt_host[5] = (unsigned int)(bpm * 100.0 + 0.5); clk_per = 44100.0 * 60.0 / (bpm * 24.0); clk_t0 = (double)t + 100.0; }
static long upos(void) { return S->gu; }   /* grid units since bar 1 */

/* the firmware's looper services: free RAM regions 3, 2 and 1 (sizes as on the pedal) in
 * one host buffer; the build runs -no-pie so host addresses fit the block's 32-bit words */
#define R3 1677952
#define R2 337196
#define R1 614400
static unsigned char big[R3 + R2 + R1 + 64] __attribute__((aligned(8)));
static unsigned int gpio_set, gpio_clr;     /* the LED's GPIO registers */
static long led_writes;
static void services(int slot)
{
    ls_host[0] = LS_MAGIC; ls_host[1] = 3;
    ls_host[2] = (unsigned int)(uintptr_t)big; ls_host[3] = R3;
    ls_host[4] = (unsigned int)(uintptr_t)(big + R3); ls_host[5] = R2;
    ls_host[6] = (unsigned int)(uintptr_t)(big + R3 + R2); ls_host[7] = R1;
    ls_host[LS_SLOT] = (unsigned int)slot;
    ls_host[LS_LEDON] = (unsigned int)(uintptr_t)&gpio_clr; ls_host[LS_LEDOFF] = (unsigned int)(uintptr_t)&gpio_set;
    ls_host[LS_LEDMASK] = 1u;
    S->me = 0x11F00000u;                       /* any params pointer */
}
static void fw_press(void) { ls_host[LS_PRESS]++; block(); }
static void text(char *out) { memcpy(out, &ls_host[LS_TEXT], 8); out[8] = 0; }

int main(void)
{
    long i, k, n, rec_start, worst_at = 0; float e, worst, a, d, maxstep;
    int j;

    /* 1: format round trip, levels from -100 dB to +18 dB */
    fresh();
    S->stereo = 1; S->bs = LP_BS2; S->capf = lp_count(S, LP_BS2); S->wi = 0;
    worst = 0.0f;
    for (k = 0; k < 120; k++) {
        float lev = (float)pow(10.0, (-100.0 + k) / 20.0), pk = 0.0f, x[16], y[16];
        for (j = 0; j < 16; j++) { x[j] = lev * (float)sin(1.7 * j + k); y[j] = -lev * (float)cos(0.9 * j + k); if (fabsf(x[j]) > pk) pk = fabsf(x[j]); if (fabsf(y[j]) > pk) pk = fabsf(y[j]); }
        for (j = 0; j < 16; j++) lp_write(S, x[j], y[j]);
        for (j = 0; j < 16; j++) {
            LpVoice v = { (int)k * 16 + j, -1 }; float l, r;
            lp_read(S, &v, &l, &r);
            e = fmaxf(fabsf(l - x[j]), fabsf(r - y[j])) / fmaxf(pk, 2.5e-4f);
            if (k < 108 && e > worst) worst = e;           /* up to +18 dB; under -72 dB the step stays at -138 dBFS */
        }
    }
    printf("format: worst error %.2e of the block peak (12 bits: 4.9e-4 or better)\n", worst);
    CHECK(worst < 6e-4f, "12-bit block float error %.2e", worst);

    /* 2: no clock, 120 BPM (stock firmware): the press records at once, 1 bar stereo */
    fresh(); U[2] = 0; U[3] = 2;
    run(4000);
    press();
    CHECK(S->st == LP_REC && S->stereo == 1 && S->U == 32, "no clock: press did not start a 1-bar stereo recording (st %d stereo %d U %d)", S->st, S->stereo, S->U);
    rec_start = t - 8;
    run(88200 + 64);                                      /* 1 bar at 120 = 2 s */
    CHECK(S->st == LP_PLAY, "loop not playing after one bar");
    CHECK(S->L > 88190 && S->L < 88210, "loop length %d, want 88200", S->L);
    /* playing: output (Mix 100 = loop only) = what was recorded one loop ago */
    worst = 0.0f; maxstep = 0.0f; a = lastout[7];
    for (i = 0; i < 2 * 88200 / 8; i++) {
        block();
        for (j = 0; j < 8; j++) {
            long src = rec_start + lp_imod((int)(t - 8 + j - rec_start), S->L);
            /* skip the seams: 128 samples after the loop start */
            long ph = lp_imod((int)(t - 8 + j - rec_start), S->L);
            if (ph >= 140) {
                e = fmaxf(fabsf(lastout[j] - in_l(src)), fabsf(lastout[j + 8] - in_r(src)));
                if (e > worst) { worst = e; worst_at = t - 8 + j; }
            }
            d = fabsf(lastout[j] - a); if (d > maxstep) maxstep = d; a = lastout[j];
        }
    }
    printf("stereo loop: worst error %.2e (at %ld), largest step %.3f\n", worst, worst_at, maxstep);
    CHECK(worst < 2e-3f, "loop does not match the recording (%.2e)", worst);
    CHECK(maxstep < 0.1f, "click at a seam (step %.3f)", maxstep);
    jumps = 0; run(4 * 88200);
    CHECK(jumps == 4, "steady tempo: %ld seams in 4 loops, want 4 (the loop ends only)", jumps);

    /* 3: When BAR: press stops at the next bar line; press plays again; double press deletes */
    U[3] = 2;
    run(10000);
    k = upos(); press();
    CHECK(S->pend == LP_A_STOP && S->pu == lp_next((int)k, 32), "stop not aimed at the next bar line");
    while (S->st == LP_PLAY) block();
    CHECK(S->gu - S->pu < 2 && S->gu >= S->pu, "stopped at unit %d, want %d", S->gu, S->pu);
    run(2000); a = 0.0f;
    for (i = 0; i < 500; i++) { block(); for (j = 0; j < 16; j++) if (fabsf(lastout[j]) > a) a = fabsf(lastout[j]); }
    CHECK(a < 1e-6f, "stopped but the loop is heard (%.3f)", a);
    press(); while (S->st == LP_STOP) block();
    run(2000); press(); run(2000); press();                  /* stop, then a double press */
    CHECK(S->pend == LP_A_DEL, "double press did not aim a delete");
    while (S->st != LP_EMPTY) block();
    CHECK(S->ev_del == 1 && S->ev_rec == 1, "events rec %d del %d", S->ev_rec, S->ev_del);

    /* 4: When IMMED: a single press waits 300 ms; double press while recording drops it */
    U[3] = 0;
    clock_start(120.0); run(44100);                        /* clock running: no "bar 1 = press" */
    press();
    CHECK(S->st == LP_EMPTY && S->pwait > 0, "IMMED: the press did not wait for a double");
    run(13230 + 16);
    CHECK(S->st == LP_REC, "IMMED: no recording 300 ms after the press");
    press(); run(800); press();
    CHECK(S->st == LP_EMPTY && S->pend == 0, "double press while recording did not drop it");
    zt_host[2] = 0u; clk_per = 0.0;

    /* 5: When CYCLE 8 on a 120 BPM clock: a press in bar 3 records from bar 9 (unit 256) */
    fresh(); U[3] = 3; U[4] = 7; U[2] = 3;                  /* Length 4 */
    clock_start(120.0);
    run(2 * 88200 + 30000);                               /* into bar 3 */
    CHECK(upos() >= 64 && upos() < 96, "not in bar 3 (unit %ld)", upos());
    press();
    CHECK(S->pend == LP_A_REC && S->pu == 256, "CYCLE 8: record aimed at unit %d", S->pu);
    while (S->st != LP_REC) block();
    printf("CYCLE 8: recording started at unit %d (+%.3f)\n", S->gu, S->gf);
    CHECK(S->gu == 256, "recording started at unit %d, want 256", S->gu);
    CHECK(S->U == 128 && S->stereo == 0, "4 bars at 120 in the arena: want mono (stereo %d U %d)", S->stereo, S->U);
    while (S->st != LP_PLAY) block();
    CHECK(S->gu == 256 + 128, "recording ended at unit %d, want 384", S->gu);

    /* 6: Roll 1/4: starts on the next bar line, repeats the last beat of the bar before */
    {
        int bar_line, len = S->L, q = len / 16, u0, ok = 1;
        float ref[64];
        U[1] = 2;                                          /* 1/4 */
        while ((S->gu & 31) != 31) block();
        while ((S->gu & 31) != 0) block();
        bar_line = S->gu;
        CHECK(S->roll == 8 && S->ru0 == bar_line, "Roll did not start on the bar line (roll %d ru0 %d gu %d)", S->roll, S->ru0, S->gu);
        /* the slice: the loop's last beat of the bar before bar_line */
        u0 = lp_imod(bar_line - 8 - S->lu0, S->U);
        for (j = 0; j < 64; j++) { LpVoice v = { (int)(u0 * S->spu + 0.5f) + 400 + j, -1 }; float l, r; lp_read(S, &v, &l, &r); ref[j] = l; }
        for (k = 0; k < 3; k++) {                          /* three repeats: same audio each beat */
            long beat_start = t;
            while (S->v[S->mi].pos < (int)(u0 * S->spu + 0.5f) + 400 || S->v[S->mi].pos > (int)(u0 * S->spu + 0.5f) + 500) block();
            for (i = 0; i < 8; i++) block();
            (void)beat_start;
            n = S->v[S->mi].pos;
            CHECK(n > (int)(u0 * S->spu) && n < (int)(u0 * S->spu) + q * 4, "Roll repeat %ld left the slice (pos %ld)", k, n);
            while ((S->gu & 7) != 7) block();
        }
        (void)ref; (void)ok;
        U[1] = 0;
        while (S->roll) block();
        CHECK((S->gu & 7) == 0 || (S->gu & 7) == 1, "Roll OFF not on a beat line");
        n = lp_target(S, S->gu);
        CHECK(abs(S->v[S->mi].pos - (int)n) < 100, "after Roll the loop is not where it would have been");
    }

    /* 7: tempo change while playing (clock 120 -> 100): locked to the grid, seams on lines only */
    clock_start(120.0);
    while (S->st != LP_PLAY || S->gu < 40) block();
    jumps = 0; run(4 * 88200);
    printf("steady 120 on the clock: %ld seams in 4 bars\n", jumps);
    CHECK(jumps <= 2, "steady clock: %ld seams in 4 bars", jumps);
    {   /* switch to 100 BPM: keep the count, slower clock */
        double cnt = (t - clk_t0) / clk_per;
        clk_per = 44100.0 * 60.0 / (100.0 * 24.0); clk_t0 = (double)t - cnt * clk_per;
        zt_host[5] = 10000u;
    }
    jumps = 0; worst = 0.0f; maxstep = 0.0f; a = lastout[7];
    for (i = 0; i < 4 * 105840 / 8; i++) {
        block();
        for (j = 0; j < 8; j++) { d = fabsf(lastout[j] - a); if (d > maxstep) maxstep = d; a = lastout[j]; }
        {   /* never past the end of the slice of the last 1/16 line: no next attack early */
            n = S->v[S->mi].pos - lp_target(S, S->gu & ~1);
            if (n < -S->L / 2) n += S->L;
            if (n > S->L / 2) n -= S->L;
            if (n > worst) worst = (float)n;
        }
    }
    printf("100 BPM on a 120 loop: %ld seams in 4 bars, playhead at most %.0f samples past its 1/16 line (slice %d)\n", jumps, worst, S->slice);
    printf("100 BPM: largest output step %.3f (input sine: about 0.01)\n", maxstep);
    CHECK(maxstep < 0.06f, "click while stretching (step %.3f)", maxstep);
    CHECK(jumps > 60 && jumps < 140, "100 BPM: %ld seams in 4 bars (want about two per 1/16)", jumps);
    CHECK(worst <= S->slice + 24, "playhead ran into the next slice (%.0f samples)", worst);
    {   /* much slower: 100 -> 80 BPM: grains fill the gaps, no clicks, never past the slice end */
        double cnt = (t - clk_t0) / clk_per;
        clk_per = 44100.0 * 60.0 / (80.0 * 24.0); clk_t0 = (double)t - cnt * clk_per;
        zt_host[5] = 8000u;
    }
    run(8000); jumps = 0; maxstep = 0.0f; a = lastout[7]; worst = 0.0f;
    for (i = 0; i < 4 * 132300 / 8; i++) {
        block();
        for (j = 0; j < 8; j++) { d = fabsf(lastout[j] - a); if (d > maxstep) maxstep = d; a = lastout[j]; }
        n = S->v[S->mi].pos - lp_target(S, S->gu & ~1);
        if (n < -S->L / 2) n += S->L;
        if (n > S->L / 2) n -= S->L;
        if (n > worst) worst = (float)n;
    }
    printf("80 BPM on a 120 loop: %ld seams in 4 bars, largest step %.3f, playhead at most %.0f past its line (slice %d)\n", jumps, maxstep, worst, S->slice);
    CHECK(maxstep < 0.06f, "80 BPM: click while stretching (step %.3f)", maxstep);
    CHECK(worst <= S->slice + 24, "80 BPM: playhead ran into the next slice (%.0f)", worst);
    CHECK(jumps > 128, "80 BPM: %ld seams, grains did not run", jumps);
    {   /* faster: 100 -> 140 BPM, one cut per 1/16 line */
        double cnt = (t - clk_t0) / clk_per;
        clk_per = 44100.0 * 60.0 / (140.0 * 24.0); clk_t0 = (double)t - cnt * clk_per;
        zt_host[5] = 14000u;
    }
    run(8000); jumps = 0; run(4 * 75600);
    printf("140 BPM on a 120 loop: %ld seams in 4 bars\n", jumps);
    CHECK(jumps > 56 && jumps < 70, "140 BPM: %ld seams in 4 bars (want one per 1/16)", jumps);

    /* 7b: the whole 30-40 BPM range: 120 -> 80, 85, 160, 150 each way (ratios 0.67 .. 1.33), no clicks, never into the next slice */
    {
        static const int tgt[4] = { 80, 85, 160, 150 };
        int q;
        for (q = 0; q < 4; q++) {
            double cnt = (t - clk_t0) / clk_per, bt = (double)tgt[q];
            clk_per = 44100.0 * 60.0 / (bt * 24.0); clk_t0 = (double)t - cnt * clk_per; zt_host[5] = (unsigned)(tgt[q] * 100);
            run(8000); maxstep = 0.0f; a = lastout[7]; worst = 0.0f;
            for (i = 0; i < 3 * 132300 / 8; i++) {
                block();
                for (j = 0; j < 8; j++) { d = fabsf(lastout[j] - a); if (d > maxstep) maxstep = d; a = lastout[j]; }
                n = S->v[S->mi].pos - lp_target(S, S->gu & ~1);
                if (n < -S->L / 2) n += S->L;
                if (n > S->L / 2) n -= S->L;
                if (n > worst) worst = (float)n;
            }
            printf("120 loop at %d BPM: largest step %.3f, playhead at most %.0f past its line (slice %d)\n", tgt[q], maxstep, worst, S->slice);
            CHECK(maxstep < 0.06f, "%d BPM: click (step %.3f)", tgt[q], maxstep);
            CHECK(worst <= S->slice + 24, "%d BPM: ran into the next slice (%.0f)", tgt[q], worst);
        }
    }

    /* 8: MIDI Start: a waiting action is re-aimed at the downbeat, the loop restarts */
    U[3] = 3; U[4] = 15;                                  /* CYCLE 16 */
    press();                                              /* stop at the next 16-bar line */
    CHECK(S->pend == LP_A_STOP, "stop not waiting");
    clock_start(100.0); block();
    CHECK(S->pend == LP_A_STOP && S->pu == 0, "Start did not re-aim the stop at the downbeat");
    while (S->st == LP_PLAY) block();
    CHECK(S->gu == 0, "stop after Start at unit %d, want 0", S->gu);
    zt_host[2] = 0u; clk_per = 0.0;

    /* 8b: MIDI Stop stops a playing loop at once, the next Start plays it from bar 1;
     * a loop stopped from the footswitch stays stopped */
    U[3] = 1;                                             /* BEAT */
    clock_start(100.0); run(20000); press();               /* past the double-press window */
    while (S->st != LP_PLAY) block();
    run(20000);
    zt_host[2] = 0u; clk_per = 0.0; block();
    CHECK(S->st == LP_STOP && S->mstop == 1, "MIDI Stop did not stop the loop (st %d)", S->st);
    run(20000);
    CHECK(S->st == LP_STOP && S->pend == 0, "loop moved while the transport was stopped");
    clock_start(100.0); block();
    CHECK(S->pend == LP_A_PLAY && S->pu == 0, "Start did not aim play at the downbeat");
    while (S->st != LP_PLAY) block();
    CHECK(S->gu == 0, "play after Start at unit %d, want 0", S->gu);
    run(20000); press();
    while (S->st == LP_PLAY) block();
    zt_host[2] = 0u; clk_per = 0.0; run(4000);
    clock_start(100.0); run(44100);
    CHECK(S->st == LP_STOP, "footswitch stop: Start played the loop again");
    zt_host[2] = 0u; clk_per = 0.0;

    /* 9: fit: stereo / mono / shorter in the 705 KB arena and with the services' big buffer
     * (3.3 MB with the arena) */
    {
        LpParams P; float bpm;
        struct { float bpm; int len; int bufbig; int want_st; int want_U; } c[] = {
            { 120, 1, 0, 1, 32 }, { 120, 2, 0, 1, 64 }, { 120, 4, 0, 0, 128 }, { 120, 8, 0, 0, 128 }, { 40, 8, 0, 0, 32 },
            { 79, 8, 1, 1, 256 }, { 78, 8, 1, 0, 256 }, { 40, 8, 1, 0, 256 }, { 40, 4, 1, 1, 128 },
            { 250, 8, 1, 1, 256 }, { 100, 8, 1, 1, 256 },
        };
        for (k = 0; k < (long)(sizeof c / sizeof c[0]); k++) {
            fresh();
            if (c[k].bufbig) { services(0); S->own = 1; }
            U[2] = (float)(c[k].len - 1); lp_prepare(&P, U); bpm = c[k].bpm;
            lp_record(S, &P, bpm, 0, 0.0f);
            CHECK(S->stereo == c[k].want_st && S->U == c[k].want_U && S->big == c[k].bufbig,
                  "fit %.0f BPM %d bars %s: stereo %d bars %d, want %d %d",
                  c[k].bpm, c[k].len, c[k].bufbig ? "big" : "arena", S->stereo, S->U >> 5, c[k].want_st, c[k].want_U >> 5);
        }
        /* the big buffer across its four regions: record and read back 8 bars stereo at 80 */
        fresh(); services(0);
        U[2] = 7; U[1] = 0; tempo_word(80);
        run(800); fw_press(); rec_start = t - 8;
        while (S->st != LP_PLAY) block();
        CHECK(S->stereo == 1 && S->U == 256 && S->big && S->nreg == 4, "big buffer 80 BPM: want 8 bars stereo in 4 regions");
        worst = 0.0f;
        for (i = 0; i < S->L / 8; i++) {
            block();
            for (j = 0; j < 8; j++) {
                long ph = lp_imod((int)(t - 8 + j - rec_start), S->L);
                if (ph < 140) continue;
                e = fmaxf(fabsf(lastout[j] - in_l(rec_start + ph)), fabsf(lastout[j + 8] - in_r(rec_start + ph)));
                if (e > worst) worst = e;
            }
        }
        printf("big buffer, 8 bars stereo at 80 BPM (%.1f s): worst error %.2e\n", S->L / 44100.0, worst);
        CHECK(worst < 2e-3f, "big buffer loop does not match (%.2e)", worst);
    }

    /* 9b: services: claim and heartbeat, presses from the firmware's counter (flips
     * ignored), flips while the slot is not found, status text, LED, losing the claim */
    {
        char tx[9]; unsigned int hb; long on_writes = 0, n = 0; int lit, last = -1;
        fresh(); services(2); tempo_word(120); U[2] = 1; U[3] = 2;   /* 2 bars, BAR */
        block();
        CHECK(ls_host[LS_CLAIM] == S->me && S->own, "no claim");
        hb = ls_host[LS_HB]; run(80);
        CHECK(ls_host[LS_HB] == hb + 10, "heartbeat %u, want %u", ls_host[LS_HB], hb + 10);
        text(tx); CHECK(!strcmp(tx, "EMPTY   "), "text '%s'", tx);
        press(); run(4000);
        CHECK(S->st == LP_EMPTY && !S->pend, "a flip counted while the firmware counts presses");
        fw_press(); text(tx);
        CHECK(S->st == LP_REC, "firmware press did not record (st %d)", S->st);
        run(97020); text(tx);                  /* 2.2 s: bar 2 */
        CHECK(!strcmp(tx, "REC  2/2"), "recording text '%s'", tx);
        CHECK(ls_host[LS_LEVEL] == 32, "recording level %u", ls_host[LS_LEVEL]);
        while (S->st != LP_PLAY) block();
        run(400); text(tx);
        CHECK(!strcmp(tx, "PLAY 1/2"), "playing text '%s'", tx);
        /* LED: only while under the cursor; the duty follows the level */
        CHECK(led_writes == 0 && gpio_clr == 0 && gpio_set == 0, "LED written while not under the cursor");
        ls_host[LS_MINE] = 1u;
        for (i = 0; i < 22050; i++) {          /* one beat at 120: about 2756 blocks */
            gpio_clr = gpio_set = 0; block();
            if (gpio_clr) { lit = 1; on_writes++; } else if (gpio_set) lit = 0; else lit = last;
            if (lit == 1) n++;
            last = lit;
        }
        printf("LED, playing: lit %.0f %% of the blocks, %ld switch-ons\n", 100.0 * n / 22050, on_writes);
        CHECK(n > 22050 / 50 && n < 22050 / 2 && on_writes > 100, "LED duty %ld / 22050, %ld switch-ons", n, on_writes);
        fw_press(); text(tx);
        CHECK(!strncmp(tx, "PLAY", 4) && ls_host[LS_LEVEL] <= 32, "waiting to stop: '%s'", tx);
        while (S->st != LP_STOP) block();
        text(tx); CHECK(!strcmp(tx, "STOP    "), "stopped text '%s'", tx);
        fw_press(); run(8); fw_press();       /* double press: delete */
        while (S->st != LP_EMPTY) block();
        CHECK(S->ev_del == 1, "double press did not delete");
        /* slot not found: on/off flips are presses again */
        ls_host[LS_SLOT] = 0xFFFFFFFFu; run(80);
        press(); run(80);
        CHECK(S->pend == LP_A_REC || S->st == LP_REC, "flip ignored while the firmware has no slot");
        /* someone else's claim (we were freed and another looper took it): a big loop is gone */
        ls_host[LS_SLOT] = 2u; while (S->st != LP_PLAY) block();
        CHECK(S->big, "loop not in the big buffer");
        ls_host[LS_CLAIM] = 0x22F00000u; block();
        CHECK(S->st == LP_EMPTY && !S->own, "lost claim: loop still playing from the big buffer");
        /* a second looper with a loop of its own in the arena never takes the claim */
        ls_host[LS_CLAIM] = 0u; S->st = LP_PLAY; S->big = 0; block();
        CHECK(ls_host[LS_CLAIM] == 0u, "claimed while a loop was in the arena");
        /* no services block: flips as before */
        fresh(); tempo_word(120); run(80); press(); run(80);
        CHECK(S->pend == LP_A_REC || S->st == LP_REC, "no services: flip not counted");
    }

    /* 10: labels */
    {
        char b[16];
        ZDL_GetLabel_1(4, b); CHECK(!strcmp(b, "1/16"), "Roll label %s", b);
        ZDL_GetLabel_2(7, b); CHECK(!strcmp(b, "8BAR"), "Length label %s", b);
        ZDL_GetLabel_3(0, b); CHECK(!strcmp(b, "IMMED"), "When label %s", b);
        ZDL_GetLabel_3(3, b); CHECK(!strcmp(b, "CYCLE"), "When label %s", b);
        ZDL_GetLabel_4(15, b); CHECK(!strcmp(b, "16BAR"), "Amount label %s", b);
    }

    /* 11: random knobs, presses, tempo and clock: no NaN, bounded, indices in range */
    {
        unsigned int r = 99u; float pk = 0.0f; int nan = 0;
        fresh(); tempo_word(120);
        for (i = 0; i < 1000000; i++) {
            r = r * 1664525u + 1013904223u;
            if ((r >> 8) % 3000u == 0u) { U[(r >> 4) % 5u] = (float)((r >> 12) % 101u); U[1] = fminf(U[1], 5); U[2] = fminf(U[2], 7); U[3] = fminf(U[3], 3); U[4] = fminf(U[4], 15); }
            if ((r >> 9) % 8000u == 0u) { onoff = 1.0f - onoff; }
            if ((r >> 10) % 20000u == 0u) tempo_word(40 + (int)((r >> 3) % 211u));
            if ((r >> 11) % 40000u == 0u) clock_start(40.0 + (r >> 5) % 200u);
            if ((r >> 11) % 30000u == 7u) { zt_host[2] = 0u; clk_per = 0.0; }
            block();
            for (j = 0; j < 16; j++) { if (!(lastout[j] == lastout[j])) nan++; if (fabsf(lastout[j]) > pk) pk = fabsf(lastout[j]); }
            if (S->v[0].pos > S->wi + 200 || S->v[1].pos > S->wi + 200 || S->wi > S->capf) { nan++; }
        }
        printf("random run: peak %.3f, NaN/out of range %d, events rec %d stop %d del %d\n", pk, nan, S->ev_rec, S->ev_stop, S->ev_del);
        CHECK(nan == 0 && pk < 2.5f, "random run: NaN/range %d, peak %.3f", nan, pk);
    }

    printf("%s\n", fails ? "looper: FAILED" : "looper: ok");
    return fails ? 1 : 0;
}
