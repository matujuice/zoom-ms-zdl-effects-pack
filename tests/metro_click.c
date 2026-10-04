/* Metro: on a synthetic drum-machine mix (the one in segue_looper.c), the clicks start
 * only once locked, land just before the true kicks with the right accents (beat, bar,
 * phrase), KICK clicks follow the detector, the footswitch resets bar 1, Mix is the DJ
 * law, labels, a boomy kick on every beat (AUTO hunts), the Thrsh scale, and a
 * random-knob run (no NaN, bounded). */
#define METRO_HOST_TEST
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../src/custom/metro/metro.c"

static SeState S;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define FS 44100.0f

/* ---- the synthetic mix from segue_looper.c --------------------------------- */
typedef struct {
    double bpm, ppm, t0;
    int    bass;
    unsigned int rng;
} Mix;

static float noise(Mix *m) { m->rng = m->rng * 1664525u + 1013904223u; return (float)(int)(m->rng >> 8) * 1.1920929e-7f - 1.0f; }
static double beat_len(const Mix *m) { return 60.0 / m->bpm * (1.0 + m->ppm * 1e-6); }
static double beat_time(const Mix *m, long k) { return m->t0 + k * beat_len(m); }

static float mix_at(Mix *m, double t)
{
    double T = beat_len(m), b, x;
    float out = 0.0f;
    long k;
    if (t < m->t0) return 0.0f;
    b = (t - m->t0) / T;
    k = (long)floor(b);
    x = (b - (double)k) * T;
    {
        double ph = 2 * M_PI * (50.0 * x + 100.0 * 0.03 * (1 - exp(-x / 0.03)));  /* 150 -> 50 Hz */
        out += 0.8f * (float)(sin(ph) * exp(-x / 0.15));
    }
    {
        double h = fmod(b * 2.0, 1.0) * T * 0.5;
        float n = noise(m);
        static float last;
        float hp = n - last; last = n;
        out += 0.12f * hp * (float)exp(-h / 0.02);
    }
    if ((k & 1) == 1) out += 0.3f * noise(m) * (float)exp(-x / 0.08);
    if (m->bass) {
        double s16 = fmod(b * 4.0, 1.0) * T * 0.25;
        int q = (int)(fmod(b * 4.0, 4.0));
        if (q != 0) out += 0.32f * (float)(sin(2 * M_PI * 55.0 * s16) * exp(-s16 / 0.05));
    }
    out += 0.15f * (float)sin(2 * M_PI * 440.0 * t) * (float)exp(-fmod(b, 4.0) * T / 0.6);
    return out;
}

/* u = Mix Click Bars LoBPM HiBPM Thrsh Listn */
static void block(const float *u, float onoff, float *b)
{
    SeParams P;
    se_prepare(&P, u);
    if (S.magic != MT_MAGIC) se_init(&S, 0.5f * (P.Tmin + P.Tmax));
    se_block(&S, &P, onoff);
    se_process(&S, &P, b, 8);
}

static void fresh(void) { memset(&S, 0x7f, sizeof S); }

/* a click that started: when (s) and which (its oscillator coefficient) */
typedef struct { double t; float w; } Click;
static Click clicks[4096];
static int nclicks;

/* run the mix; records every click start. onoff_flip_at: time of a footswitch press, or < 0 */
static void run(Mix *m, const float *u, double secs, double flip_at, float *peak_out)
{
    long t;
    int i, prev_left = 0;
    float b[8], onoff = 1.0f, pk = 0.0f;
    nclicks = 0;
    for (t = 0; t < (long)(secs * FS); t += 8) {
        if (flip_at >= 0 && t / FS >= flip_at) { onoff = 0.0f; flip_at = -1; }
        for (i = 0; i < 8; i++) b[i] = mix_at(m, (t + i) / FS);
        block(u, onoff, b);
        for (i = 0; i < 8; i++) {
            if (!(b[i] == b[i])) pk = 1e9f;
            if (fabsf(b[i]) > pk) pk = fabsf(b[i]);
        }
        /* a click started in this block if 'left' jumped up */
        if (S.left > prev_left && nclicks < 4096) {
            clicks[nclicks].t = (t + 8 - (MT_LEN - S.left)) / FS;
            clicks[nclicks].w = S.w;
            nclicks++;
        }
        prev_left = S.left;
    }
    if (peak_out) *peak_out = pk;
}

/* the beat (from bar 1 = the first kick) nearest to time t, and the offset in ms */
static long nearest_beat(const Mix *m, double t, double *ms)
{
    long k = (long)floor((t - m->t0) / beat_len(m) + 0.5);
    *ms = (t - beat_time(m, k)) * 1000.0;
    return k;
}

int main(void)
{
    int i;
    printf("state %u bytes\n", (unsigned)sizeof(SeState));

    /* 1. BEAT at 160 BPM, machine 200 ppm slow, 3 minutes: clicks only after lock, every
     *    beat once, a little before the kick, accents on bars (1.6 kHz) and every 8th bar
     *    (2.4 kHz) counted from the first kick */
    {
        float u[7] = {50, 0, 7, 140, 170, 0, 0}, pk;
        Mix m = {160.0, 200.0, 0.5, 0, 1};
        double ms, sum = 0, worst = 0, first;
        long k, prev = -1, missing = 0, dup = 0, wrong = 0, n = 0;
        fresh();
        run(&m, u, 180.0, -1, &pk);
        first = nclicks ? clicks[0].t : 1e9;
        for (i = 0; i < nclicks; i++) {
            float want;
            k = nearest_beat(&m, clicks[i].t, &ms);
            if (prev >= 0 && k > prev + 1) missing += k - prev - 1;
            if (k == prev) dup++;
            prev = k;
            want = (k % 32 == 0) ? MT_W_PHRASE : (k % 4 == 0) ? MT_W_BAR : MT_W_BEAT;
            if (clicks[i].w != want) wrong++;
            if (clicks[i].t > 10.0) { sum += ms; n++; if (fabs(ms) > fabs(worst)) worst = ms; }
        }
        printf("BEAT 160 BPM +200 ppm: %d clicks, first at %.2f s (kick 1 at 0.50 s), lead %.1f ms (worst %.1f),"
               " %ld missing, %ld doubled, %ld wrong accent, peak %.2f\n",
               nclicks, first, -sum / n, -worst, missing, dup, wrong, pk);
        CHECK(S.cs == SE_LOCK, "not locked");
        CHECK(first > 0.5 + 3 * beat_len(&m) && first < 5.0, "first click not right after lock");
        CHECK(nclicks > 400, "too few clicks");
        CHECK(missing == 0 && dup == 0 && wrong == 0, "beats missing, doubled or mis-accented");
        CHECK(sum / n < -3.0 && sum / n > -20.0 && worst > -30.0 && worst < 2.0, "clicks not just before the kicks");
        CHECK(pk < 1.6f, "too loud");
    }

    /* 2. BAR: only bar and phrase clicks; Bars 4 -> phrase every 16 beats */
    {
        float u[7] = {50, 1, 3, 140, 170, 0, 0};
        Mix m = {150.0, -80.0, 0.3, 0, 2};
        double ms;
        long k, bad = 0, phr = 0;
        fresh();
        run(&m, u, 60.0, -1, NULL);
        for (i = 0; i < nclicks; i++) {
            k = nearest_beat(&m, clicks[i].t, &ms);
            if (k % 4 != 0) bad++;
            if (k % 16 == 0) { phr++; if (clicks[i].w != MT_W_PHRASE) bad++; }
            else if (clicks[i].w != MT_W_BAR) bad++;
        }
        printf("BAR, Bars 4, 150 BPM: %d clicks, %ld phrase, %ld wrong\n", nclicks, phr, bad);
        CHECK(nclicks > 30 && bad == 0 && phr * 4 >= nclicks - 4 && phr * 4 <= nclicks + 4, "BAR clicks");
    }

    /* 3. KICK: a 1 kHz click per kick, 20..40 ms after it, from the very first kick */
    {
        float u[7] = {50, 2, 7, 140, 170, 0, 0};
        Mix m = {160.0, 0.0, 0.5, 0, 3};
        double ms, lo = 1e9, hi = -1e9;
        long k, prev = -1, missing = 0;
        fresh();
        run(&m, u, 30.0, -1, NULL);
        for (i = 0; i < nclicks; i++) {
            k = nearest_beat(&m, clicks[i].t - 0.025, &ms);
            ms += 25.0;
            if (prev >= 0 && k > prev + 1) missing += k - prev - 1;
            prev = k;
            if (ms < lo) lo = ms;
            if (ms > hi) hi = ms;
            if (clicks[i].w != MT_W_KICK) missing += 1000;
        }
        printf("KICK 160 BPM: %d clicks (%ld kicks), first at %.2f s, %.1f..%.1f ms after the kick, %ld missing\n",
               nclicks, (long)((30.0 - 0.5) / beat_len(&m)) + 1, nclicks ? clicks[0].t : -1.0, lo, hi, missing);
        CHECK(nclicks > 0 && clicks[0].t < 0.56, "first kick not clicked");
        CHECK(missing == 0 && lo > 15.0 && hi < 40.0, "kick clicks");
    }

    /* 4. the rolling bass in the kick band, range 40..240: still every beat, no extras */
    {
        float u[7] = {50, 0, 7, 40, 240, 0, 0};
        Mix m = {142.0, -80.0, 1.2, 1, 7};
        double ms;
        long k, prev = -1, missing = 0, dup = 0;
        fresh();
        run(&m, u, 90.0, -1, NULL);
        for (i = 0; i < nclicks; i++) {
            k = nearest_beat(&m, clicks[i].t, &ms);
            if (clicks[i].t > 15.0) {
                if (prev >= 0 && k > prev + 1) missing += k - prev - 1;
                if (k == prev || fabs(ms) > 40.0) dup++;
            }
            prev = k;
        }
        printf("BEAT 142 BPM rolling bass: %d clicks, %ld missing, %ld extra or off\n", nclicks, missing, dup);
        CHECK(nclicks > 150 && missing == 0 && dup == 0, "bass fooled the clicks");
    }

    /* 5. footswitch: a press at 20.1 s makes the next kick (beat 53, 20.375 s) bar 1: its
 *    phrase click comes when that kick is judged (late), then beat 2 on time, bar 2 on the
 *    5th beat, the next phrase 8 bars on */
    {
        float u[7] = {50, 0, 7, 140, 170, 0, 0};
        Mix m = {160.0, 0.0, 0.5, 0, 9};
        double ms, late = -1;
        long k = -1;
        int j = -1;
        fresh();
        run(&m, u, 40.0, 20.1, NULL);
        for (i = 0; i < nclicks; i++) if (clicks[i].t > 20.1) { j = i; break; }
        if (j >= 0) { late = (clicks[j].t - beat_time(&m, 53)) * 1000.0; k = nearest_beat(&m, clicks[j + 1].t, &ms); }
        printf("reset at 20.1 s: bar 1 click %.1f ms after its kick (%s), next click on beat %ld (%.1f ms)\n",
               late, j >= 0 && clicks[j].w == MT_W_PHRASE ? "phrase" : "NOT phrase", k, ms);
        CHECK(j >= 0 && clicks[j].w == MT_W_PHRASE && late > 15.0 && late < 50.0, "no late bar 1 click after the reset");
        CHECK(k == 54 && clicks[j + 1].w == MT_W_BEAT && clicks[j + 4].w == MT_W_BAR && clicks[j + 32].w == MT_W_PHRASE
              && nearest_beat(&m, clicks[j + 32].t, &ms) == 53 + 32, "count after the reset");
    }

    /* 6. Mix: 0 = the input untouched; 100 = clicks only */
    {
        float u[7] = {0, 0, 7, 140, 170, 0, 0}, b[8], x[8];
        Mix m = {160.0, 0.0, 0.5, 0, 11};
        long t;
        int same = 1, clicks_heard = 0;
        fresh();
        for (t = 0; t < (long)(20 * FS); t += 8) {
            for (i = 0; i < 8; i++) x[i] = b[i] = mix_at(&m, (t + i) / FS);
            block(u, 1.0f, b);
            for (i = 0; i < 8; i++) if (b[i] != x[i]) same = 0;
        }
        u[0] = 100;
        for (t = 0; t < (long)(5 * FS); t += 8) {
            for (i = 0; i < 8; i++) b[i] = mix_at(&m, (t + i) / FS);
            block(u, 1.0f, b);
            for (i = 0; i < 8; i++) if (fabsf(b[i]) > 0.05f) clicks_heard++;
        }
        printf("Mix 0: input %s; Mix 100: %d loud samples in 5 s (clicks only)\n", same ? "untouched" : "CHANGED", clicks_heard);
        CHECK(same, "Mix 0 changed the input");
        CHECK(clicks_heard > 100 && clicks_heard < 5 * 9 * 1323, "Mix 100");
    }

    /* 7. labels */
    {
        char s[16];
        ZDL_GetLabel_1(0, s); CHECK(!strcmp(s, "BEAT"), "label BEAT");
        ZDL_GetLabel_1(1, s); CHECK(!strcmp(s, "BAR"), "label BAR");
        ZDL_GetLabel_1(2, s); CHECK(!strcmp(s, "KICK"), "label KICK");
        ZDL_GetLabel_2(7, s); CHECK(!strcmp(s, "8"), "label Bars");
        ZDL_GetLabel_3(10, s); CHECK(!strcmp(s, "40"), "label LoBPM");
        ZDL_GetLabel_4(170, s); CHECK(!strcmp(s, "170"), "label HiBPM");
        ZDL_GetLabel_5(0, s); CHECK(!strcmp(s, "AUTO"), "label Thrsh");
        ZDL_GetLabel_6(51, s); CHECK(!strcmp(s, "100Hz"), "label Listn");
        printf("labels ok\n");
    }

    /* 8. random knobs and presses on noise and drums: no NaN, bounded */
    {
        float u[7], b[8], pk = 0;
        Mix m = {128.0, 0.0, 0.1, 1, 13};
        long t;
        unsigned int r = 99;
        fresh();
        for (t = 0; t < (long)(120 * FS); t += 8) {
            if ((t & 32767) == 0) {
                int j;
                float mx[7] = {100, 2, 7, 240, 240, 100, 101};
                for (j = 0; j < 7; j++) { r = r * 1664525u + 1013904223u; u[j] = (float)((r >> 8) % (unsigned)(mx[j] + 1)); }
            }
            for (i = 0; i < 8; i++) b[i] = mix_at(&m, (t + i) / FS) * ((t >> 16) & 1 ? 1.0f : 0.1f);
            block(u, (float)((t >> 15) & 1), b);
            for (i = 0; i < 8; i++) {
                if (!(b[i] == b[i])) pk = 1e9f;
                if (fabsf(b[i]) > pk) pk = fabsf(b[i]);
            }
        }
        printf("random knobs: peak %.2f\n", pk);
        CHECK(pk < 2.5f, "random run blew up");
    }

    /* 9. a boomy kick (45 Hz, 0.6 s decay) on every beat, with a snare on 2 and 4: its
     *    tail is still loud at the next kick, which jumps under 2 x. AUTO must hunt down
     *    and lock, with the tempo free (120..170) and fixed (Lo = Hi; this one also needs
     *    the stale-kick fix: the kicks missed at 2 x leave gaps of no 1, 2, 4.. beats) */
    {
        double bpms[2] = {150.0, 160.0};
        int c, f;
        for (f = 0; f < 2; f++) for (c = 0; c < 2; c++) {
            float u[7] = {50, 0, 7, 120, 170, 0, 0}, bb[8];
            double T = 60.0 / bpms[c], t0 = 0.4, first = -1, ms;
            long t, k;
            int j, prev_left = 0, on = 0, n = 0, barok = 0, barn = 0;
            unsigned int r = 5;
            if (f) { u[3] = (float)bpms[c]; u[4] = (float)bpms[c]; }
            fresh();
            for (t = 0; t < (long)(60 * FS); t += 8) {
                for (j = 0; j < 8; j++) {
                    double tt = (t + j) / FS, uu = tt - t0, x, o = 0;
                    if (uu >= 0) {
                        k = (long)floor(uu / T); x = uu - k * T;
                        o += 0.8 * sin(2 * M_PI * (45 * x + 70 * 0.025 * (1 - exp(-x / 0.025)))) * exp(-x / 0.6);
                        r = r * 1664525u + 1013904223u;
                        if (k & 1) o += 0.5 * (0.6 * sin(2 * M_PI * 185 * x) * exp(-x / 0.06)
                                              + 0.6 * ((double)(int)(r >> 8) * 1.1920929e-7 - 1.0) * exp(-x / 0.09));
                    }
                    bb[j] = (float)o;
                }
                block(u, 1.0f, bb);
                if (S.left > prev_left) {
                    double tc = (t + 8 - (MT_LEN - S.left)) / FS;
                    if (first < 0) first = tc;
                    k = (long)floor((tc - t0) / T + 0.5); ms = (tc - t0 - k * T) * 1000.0;
                    if (tc > 30.0) {
                        n++; if (ms > -30.0 && ms < 5.0) on++;
                        if (S.w != MT_W_BEAT) { barn++; if (k % 4 == 0) barok++; }
                    }
                }
                prev_left = S.left;
            }
            printf("boomy kick %.0f BPM, %s: first click %.1f s, %d/%d clicks on the beat, %d/%d bar clicks on bar 1\n",
                   bpms[c], f ? "fixed tempo" : "120..170", first, on, n, barok, barn);
            CHECK(n > 60 && on == n && barok == barn, "boomy kick not tracked");
        }
    }

    /* 10. Thrsh: 1 is the loosest, 50 = 2 x (old AUTO), 100 = 3 x */
    {
        float u[7] = {50, 0, 7, 140, 170, 1, 0};
        SeParams P;
        se_prepare(&P, u); CHECK(P.R > 1.01f && P.R < 1.03f && !P.autoR, "Thrsh 1");
        u[5] = 50; se_prepare(&P, u); CHECK(P.R > 1.98f && P.R < 2.02f, "Thrsh 50");
        u[5] = 100; se_prepare(&P, u); CHECK(P.R > 2.98f && P.R < 3.02f, "Thrsh 100");
        u[5] = 0; se_prepare(&P, u); CHECK(P.autoR, "Thrsh AUTO");
        printf("Thrsh scale ok\n");
    }

    /* 11. tempo changes with LoBPM..HiBPM as the range (140..175), a kick on every beat
     *     and a snare on 2 and 4: 160 BPM, a glide to 168 over 10 s, a jump to 150, then
     *     a loop whose kicks are pushed 60 ms late (the snare stays on the beat). Clicks
     *     must sit just before the kicks in each stretch, the pushed ones included. */
    {
        static double kt[1024];
        float u[7] = {50, 0, 7, 140, 175, 0, 0}, bb[8];
        double pos = 0.0, tt;
        double from[4] = {14.0, 34.0, 52.0, 68.0}, to[4] = {20.0, 45.0, 60.0, 76.0};
        const char *what[4] = {"160 BPM", "after the glide to 168", "after the jump to 150", "kicks pushed 60 ms"};
        int nk = 0, j, w, prev_left = 0, on[4] = {0, 0, 0, 0}, n[4] = {0, 0, 0, 0};
        long t;
        unsigned int r = 9;
        /* the kick times: the beat position advances by bpm(t) / 60 per second */
        for (t = 0; t < (long)(76 * FS) && nk < 1024; t++) {
            double bpm, p0 = pos;
            tt = t / FS;
            bpm = tt < 20 ? 160 : tt < 30 ? 160 + 0.8 * (tt - 20) : tt < 45 ? 168 : 150;
            pos += bpm / 60.0 / FS;
            if (tt >= 0.4 && floor(pos) > floor(p0)) kt[nk++] = tt + (tt >= 60.0 ? 0.060 : 0.0);
        }
        fresh();
        for (t = 0, w = 0; t < (long)(76 * FS); t += 8) {
            for (j = 0; j < 8; j++) {
                double x, o = 0, y;
                int i;
                tt = (t + j) / FS;
                while (w < nk && kt[w] <= tt) w++;
                if (w > 0) {
                    x = tt - kt[w - 1];
                    o += 0.8 * sin(2 * M_PI * (50 * x + 70 * 0.025 * (1 - exp(-x / 0.025)))) * exp(-x / 0.3);
                    /* the snare on every other beat, on the beat itself */
                    i = w - 1;
                    y = tt - (kt[i] - (kt[i] >= 60.06 ? 0.060 : 0.0));
                    r = r * 1664525u + 1013904223u;
                    if ((i & 1) && y >= 0) o += 0.4 * (0.6 * sin(2 * M_PI * 185 * y) * exp(-y / 0.06)
                                                      + 0.6 * ((double)(int)(r >> 8) * 1.1920929e-7 - 1.0) * exp(-y / 0.09));
                }
                bb[j] = (float)o;
            }
            block(u, 1.0f, bb);
            if (S.left > prev_left) {
                double tc = (t + 8 - (MT_LEN - S.left)) / FS, best = 1e9, d;
                int i, q;
                for (i = 0; i < nk; i++) { d = (tc - kt[i]) * 1000.0; if (fabs(d) < fabs(best)) best = d; }
                for (q = 0; q < 4; q++) if (tc >= from[q] && tc < to[q]) { n[q]++; if (best > -30.0 && best < 5.0) on[q]++; }
            }
            prev_left = S.left;
        }
        for (j = 0; j < 4; j++) {
            printf("tempo changes, %s: %d/%d clicks on the kick\n", what[j], on[j], n[j]);
            CHECK(n[j] > 10 && on[j] >= n[j] - 1, "tempo change not followed (%s)", what[j]);
        }
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
