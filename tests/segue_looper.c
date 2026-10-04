/* Segue: kick tracking on a synthetic drum-machine mix (drift, breakdown, rolling bass),
 * the transition workflow (arm, record, jump, pickup, fade back), the loop matching what
 * was recorded, seams without clicks, reset taps, the Bars-do-not-fit beep, Roll, LoCut,
 * the loop format, labels, and a long random-knob run (no NaN, bounded, indices in range). */
#define SEGUE_HOST_TEST
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../src/custom/segue/segue.c"

static SeState S;          /* 704 KB: keep it off the stack */
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define FS 44100.0f
#define PI2 6.2831853f

/* ---- a synthetic mix ------------------------------------------------------ */
typedef struct {
    double bpm, ppm;       /* tempo; the machine's clock error against the pedal   */
    double t0;             /* first kick, s                                          */
    int    pattern;        /* 0 = A (440 Hz stab), 1 = B (660 Hz stab)               */
    int    bass;           /* rolling 16th bass in the kick band                     */
    double kick_off_from, kick_off_to;   /* breakdown without kicks, s               */
    unsigned int rng;
} Mix;

static float noise(Mix *m) { m->rng = m->rng * 1664525u + 1013904223u; return (float)(int)(m->rng >> 8) * 1.1920929e-7f - 1.0f; }

static double beat_len(const Mix *m) { return 60.0 / m->bpm * (1.0 + m->ppm * 1e-6); }

/* sample at time t (s) */
static float mix_at(Mix *m, double t)
{
    double T = beat_len(m), b, x;
    float out = 0.0f;
    long k;
    if (t < m->t0) return 0.0f;
    b = (t - m->t0) / T;
    k = (long)floor(b);
    x = (b - (double)k) * T;                        /* s since the beat */
    if (!(t >= m->kick_off_from && t < m->kick_off_to)) {
        double ph = 2 * M_PI * (50.0 * x + 100.0 * 0.03 * (1 - exp(-x / 0.03)));  /* 150 -> 50 Hz */
        out += 0.8f * (float)(sin(ph) * exp(-x / 0.15));
    }
    {   /* hats on 8ths (offbeats loud) */
        double h = fmod(b * 2.0, 1.0) * T * 0.5;
        float n = noise(m);
        static float last;
        float hp = n - last; last = n;
        out += 0.12f * hp * (float)exp(-h / 0.02);
    }
    if ((k & 1) == 1) out += 0.3f * noise(m) * (float)exp(-x / 0.08);     /* snare on 2, 4 */
    if (m->bass) {                                                       /* 16ths, not on the beat */
        double s16 = fmod(b * 4.0, 1.0) * T * 0.25;
        int q = (int)(fmod(b * 4.0, 4.0));
        if (q != 0) out += (m->bass == 2 ? 0.45f : 0.32f) * (float)(sin(2 * M_PI * 55.0 * s16) * exp(-s16 / 0.05));
    }
    out += 0.15f * (float)sin(2 * M_PI * (m->pattern ? 660.0 : 440.0) * t) * (float)exp(-fmod(b, 4.0) * T / 0.6);
    return out;
}

/* true time of beat k, s */
static double beat_time(const Mix *m, long k) { return m->t0 + k * beat_len(m); }

/* ---- the pedal entry, minus the ctx plumbing; u = XFade LoCut Roll Bars LoBPM HiBPM Thrsh Listn */
static void block(const float *u, float onoff, float *b)
{
    SeParams P;
    se_prepare(&P, u);
    if (S.magic != SE_MAGIC) se_init(&S, 0.5f * (P.Tmin + P.Tmax));
    se_block(&S, &P, onoff);
    se_process(&S, &P, b, 8);
}

static void fresh(void) { memset(&S, 0x7f, sizeof S); }

static int bad(float x) { return !(x == x) || x > 4.0f || x < -4.0f; }

/* goertzel power of frequency f over n samples */
static float tone(const float *x, int n, float f)
{
    float w = PI2 * f / FS, c = 2 * cosf(w), s0, s1 = 0, s2 = 0;
    int i;
    for (i = 0; i < n; i++) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((float)n * n);
}

/* run a mix through, tracking how the clock's beats sit against the true kicks */
static void track(Mix *m, const float *u, double secs, double *T_err, double *worst_ms, double *mean_ms,
                  long *bar_err, double check_from)
{
    long t, nb = 0, last_beat = -1;
    double sum = 0, worst = -12.0;
    float b[8];
    int i;
    *bar_err = 0;
    for (t = 0; t < (long)(secs * FS); t += 8) {
        for (i = 0; i < 8; i++) b[i] = mix_at(m, (t + i) / FS);
        block(u, 1.0f, b);
        if (S.cs != SE_WAIT && S.beat != last_beat) {
            /* the clock just started beat S.beat: compare with the true kick of that beat
             * (bar 1 = the first kick, beat 0) */
            double now = (t + 8) / FS - S.ph / FS;        /* when that beat began */
            double d = (now - beat_time(m, S.beat)) * 1000.0;
            if (now > check_from && !(now > m->kick_off_from && now < m->kick_off_to + 4)) {
                sum += d; nb++;
                if (fabs(d - (-12.0)) > fabs(worst - (-12.0))) worst = d;
                if (d < -0.5 * beat_len(m) * 1000 || d > 0.5 * beat_len(m) * 1000) (*bar_err)++;
            }
            last_beat = S.beat;
        }
    }
    *T_err = (S.T / FS - beat_len(m)) / beat_len(m);
    *worst_ms = worst;
    *mean_ms = nb ? sum / nb : 0;
}

int main(void)
{
    float b[8];
    int i;
    long t;
    printf("state %u bytes (arena >= 705536)\n", (unsigned)sizeof(SeState));
    CHECK(sizeof(SeState) < 705536u, "state too big");

    /* 1. the loop format (block float + pre-emphasis; mu-law gave 37.8 / 24.8 dB on this
     *    signal): a mix-like signal (saw bass, chords,
     *    noisy hats) round trip at three levels, including one far over full scale; then a
     *    voice that starts mid-buffer (warm-up) matches one that played from the start */
    {
        static float x[44100];
        float lv[3] = { 1.0f, 0.03f, 4.0f };
        int li, k;
        unsigned int r = 12345u;
        for (k = 0; k < 44100; k++) {
            float t = (float)k / FS, h;
            r = r * 1664525u + 1013904223u;
            h = ((float)(r >> 9) / 8388608.0f - 0.5f) * ((k % 5512) < 800 ? 0.5f : 0.0f);
            x[k] = 0.4f * (2.0f * (55.0f * t - floorf(55.0f * t)) - 1.0f)
                 + 0.15f * sinf(6.2832f * 440.0f * t) + 0.15f * sinf(6.2832f * 554.4f * t)
                 + 0.1f * sinf(6.2832f * 3520.0f * t) + h;
        }
        for (li = 0; li < 3; li++) {
            double se = 0, ss = 0, sh = 0, eh = 0, ep = 0, xp = 0;
            float z = 0, worst = 0;
            se_init(&S, 16537.5f);
            S.rec = 1;
            for (k = 0; k < 44100; k++) { se_wr(&S, lv[li] * x[k]); S.xp = lv[li] * x[k]; }
            se_flush(&S, 44100 & ~31, 44100 & 31);
            S.rec = 0;
            for (k = 0; k < 44100; k++) {
                double e, d;
                z = se_rd(&S, k) + SE_PRE * z;
                e = z - lv[li] * x[k];
                se += e * e; ss += (double)lv[li] * x[k] * lv[li] * x[k];
                d = e - ep; sh += (lv[li] * x[k] - xp) * (lv[li] * x[k] - xp); eh += d * d;
                ep = e; xp = lv[li] * x[k];
            }
            for (k = 1000; k < 44000; k += 977) {   /* warm-up from mid-buffer */
                float w = se_warm(&S, k), zc = 0, ww;
                int j;
                for (j = 0; j <= k; j++) zc = se_rd(&S, j) + SE_PRE * zc;
                ww = fabsf(se_rd(&S, k) + SE_PRE * w - zc);
                if (ww > worst) worst = ww;
            }
            printf("loop format at level %.2f: SNR %.1f dB, highs (first difference) %.1f dB, warm-up error %.1e\n",
                   lv[li], 10 * log10(ss / se), 10 * log10(sh / eh), worst);
            CHECK(10 * log10(ss / se) > 44.0, "loop format SNR");
            CHECK(10 * log10(sh / eh) > 39.0, "loop format SNR in the highs");
            CHECK(worst < 0.002f * lv[li], "warm-up differs from continuous playback");
        }
        S.magic = 0;
    }

    /* 2. tracking: 160 BPM four on the floor, machine clock 200 ppm slow (twice the worst
     *    real case), 10 minutes with a 40 s kickless breakdown in the middle */
    {
        float u[9] = {0, 0, 0, 7, 140, 170, 0, 0};
        Mix m = {160.0, 200.0, 0.5, 0, 0, 300.0, 340.0, 1};
        double Te, worst, mean; long be;
        fresh();
        track(&m, u, 600.0, &Te, &worst, &mean, &be, 10.0);
        printf("track 160 BPM +200 ppm, breakdown: state %d, tempo error %.0f ppm, beats lead the kick by %.1f ms"
               " (worst %.1f), %ld beats off by > 1/2\n", S.cs, Te * 1e6, -mean, -worst, be);
        CHECK(S.cs == SE_LOCK, "not locked");
        CHECK(fabs(Te) < 1e-4, "tempo off");
        CHECK(mean < -3.0 && mean > -20.0 && worst < 2.0 && worst > -30.0, "beats not just before the kicks");
        CHECK(be == 0, "lost the beat count");
    }

    /* 3. a rolling 16th bass in the kick band (the 4th 16th lands 3/4 beat after the
     *    kick, inside a wide tempo range): about 4 dB under the kick with range 40..240
     *    (told apart by level), and 1 dB under it with 120..170 (told apart by the range) */
    {
        float u[9] = {0, 0, 0, 7, 40, 240, 0, 0};
        Mix m = {142.0, -80.0, 1.2, 0, 1, 1e9, 1e9, 7};
        double Te, worst, mean; long be;
        int c;
        for (c = 0; c < 2; c++) {
            m.bass = c ? 2 : 1;
            u[4] = c ? 120 : 40; u[5] = c ? 170 : 240;
            fresh();
            track(&m, u, 120.0, &Te, &worst, &mean, &be, 15.0);
            printf("track 142 BPM rolling bass %s, range %.0f..%.0f: state %d, tempo error %.0f ppm, lead %.1f ms (worst %.1f), %ld off\n",
                   c ? "-1 dB" : "-4 dB", u[4], u[5], S.cs, Te * 1e6, -mean, -worst, be);
            CHECK(S.cs == SE_LOCK && fabs(Te) < 1e-4 && be == 0 && mean < -3.0 && mean > -20.0, "bass fooled the tracker");
        }
    }

    /* 4. fixed tempo (Lo = Hi = 160) and a slow ramp 150 -> 165 over 3 minutes */
    {
        float u[9] = {0, 0, 0, 7, 160, 160, 0, 0};
        Mix m = {160.0, 50.0, 0.2, 0, 0, 1e9, 1e9, 3};
        double Te, worst, mean; long be;
        fresh();
        track(&m, u, 120.0, &Te, &worst, &mean, &be, 10.0);
        printf("fixed 160 BPM, machine +50 ppm: lead %.1f ms (worst %.1f), %ld off\n", -mean, -worst, be);
        CHECK(S.cs == SE_LOCK && be == 0 && worst > -30.0 && worst < 2.0, "fixed tempo tracking");
    }
    {
        float u[9] = {0, 0, 0, 7, 140, 170, 0, 0};
        Mix m = {150.0, 0.0, 0.3, 0, 0, 1e9, 1e9, 5};
        long last_beat = -1, offs = 0;
        double kt = m.t0, worstd = -12;     /* the ramp: kick times by integrating the tempo */
        long k = 0;
        fresh();
        /* build the kicks by hand: tempo ramps 150 -> 165 over 180 s */
        for (t = 0; t < (long)(200 * FS); t += 8) {
            for (i = 0; i < 8; i++) {
                double tt = (t + i) / FS, bpm = 150.0 + 15.0 * (tt > 180 ? 1.0 : tt / 180.0);
                m.bpm = bpm;
                while (tt >= kt + 60.0 / bpm) { kt += 60.0 / bpm; k++; }
                m.t0 = kt - k * beat_len(&m);    /* so mix_at puts beat k at kt */
                b[i] = mix_at(&m, tt);
            }
            block(u, 1.0f, b);
            if (S.cs != SE_WAIT && S.beat != last_beat) {
                double now = (t + 8) / FS - S.ph / FS, d = (now - kt) * 1000.0;
                if (d > 30000.0 / m.bpm) d -= 60000.0 / m.bpm;   /* the kick that is about to come */
                if (now > 20) { if (fabs(d + 12) > fabs(worstd + 12)) worstd = d; if (S.beat != k && S.beat != k + 1) offs++; }
                last_beat = S.beat;
            }
        }
        printf("ramp 150 -> 165 BPM: tracked %.2f BPM at the end, worst beat %.1f ms from the kick, %ld beat counts off\n",
               SE_SPB / S.T, worstd, offs);
        CHECK(fabs(SE_SPB / S.T - 165.0) < 0.2 && worstd > -30 && worstd < 5 && offs == 0, "tempo ramp");
    }

    /* 5. the workflow: pattern A, press in bar 5, record bars 9..16, jump at bar 17 (pattern B
     *    from there), output = A's loop; XFade to LOOP picks up; XFade to LIVE gives B */
    {
        static float in[44100 * 60], out[44100 * 60];
        float u[9] = {0, 0, 0, 7, 140, 170, 0, 0};
        Mix m = {160.0, 30.0, 0.25, 0, 0, 1e9, 1e9, 9};
        long N = (long)(55 * FS), jump_t = -1, rec_t = -1, rec_beat = -1;
        double bar = 4 * beat_len(&m);
        float onoff = 1.0f;
        fresh();
        for (t = 0; t < N; t += 8) {
            double tt = t / FS;
            m.pattern = (tt >= m.t0 + 16 * bar - 0.002) ? 1 : 0;     /* B from bar 17 */
            if (tt > m.t0 + 4.5 * bar && tt < m.t0 + 4.5 * bar + 0.01) onoff = 0.0f;  /* press: off */
            u[0] = 0;
            if (tt > m.t0 + 19 * bar) u[0] = 100;                     /* pick up        */
            if (tt > m.t0 + 21 * bar) u[0] = 0;                       /* fade back      */
            for (i = 0; i < 8; i++) { in[t + i] = mix_at(&m, (t + i) / FS); b[i] = in[t + i]; }
            {
                int was_rec = S.rec, had = S.magic == SE_MAGIC ? S.has_loop : 0;
                block(u, onoff, b);
                if (!was_rec && S.rec && rec_t < 0) { rec_t = t; rec_beat = S.beat; }
                if (!had && S.has_loop) jump_t = t;
            }
            for (i = 0; i < 8; i++) out[t + i] = b[i];
        }
        printf("workflow: recording began on beat %ld (%.3f s, bar 9 is at %.3f s), jump at %.3f s (bar 17 at %.3f s),"
               " loop %d samples = %d bars of %.0f\n", rec_beat, rec_t / FS, m.t0 + 8 * bar, jump_t / FS,
               m.t0 + 16 * bar, S.L, S.lbars, bar * FS);
        CHECK(rec_beat == 32, "recording did not start on bar 9");
        CHECK(fabs(rec_t / FS - (m.t0 + 8 * bar)) < 0.03 && rec_t / FS < m.t0 + 8 * bar, "recording start time");
        CHECK(jump_t > 0 && fabs(jump_t / FS - (m.t0 + 16 * bar)) < 0.03 && jump_t / FS < m.t0 + 16 * bar, "jump time");
        CHECK(fabs(S.L - 8 * bar * FS) < 0.002 * 8 * bar * FS, "loop length");
        {
            long a = (long)((m.t0 + 17 * bar) * FS), n = (long)(2 * bar * FS);
            float pa = tone(out + a, n, 440), pb = tone(out + a, n, 660);
            float ia = tone(in + a, n, 440), ib = tone(in + a, n, 660);
            printf("  after the jump (XFade at LIVE, not picked up): out 440 Hz %.2e, 660 Hz %.2e (live: %.2e, %.2e)\n", pa, pb, ia, ib);
            CHECK(pa > 20 * pb && ib > 20 * ia, "the loop is not what plays after the jump");
        }
        {   /* the loop is the recording: out = in delayed by L (bars 17..19, past the seam) */
            long a = (long)((m.t0 + 17 * bar) * FS), n = (long)(1.5 * bar * FS), k;
            float err = 0, pk = 0;
            for (k = a; k < a + n; k++) {
                float e = fabsf(out[k] - in[k - S.L]);
                if (e > err) err = e;
                if (fabsf(in[k - S.L]) > pk) pk = fabsf(in[k - S.L]);
            }
            printf("  loop vs the recorded input: max error %.4f (peak %.2f, 8-bit steps)\n", err, pk);
            CHECK(err < 0.04f * pk + 0.002f, "loop differs from the recording");
        }
        {
            long a = (long)((m.t0 + 22.5 * bar) * FS), n = (long)(1 * bar * FS);
            float pa = tone(out + a, n, 440), pb = tone(out + a, n, 660);
            printf("  picked up, then XFade LIVE: out 440 Hz %.2e, 660 Hz %.2e\n", pa, pb);
            CHECK(pb > 20 * pa, "XFade did not bring the live mix back");
        }
        {   /* clicks: the biggest sample-to-sample step of the output around the jump and the
             * loop's own seam, against that of the input */
            long k, a = (long)((m.t0 + 15.5 * bar) * FS), z = (long)((m.t0 + 18.5 * bar) * FS);
            float so = 0, si = 0;
            for (k = a; k < z; k++) {
                if (fabsf(out[k] - out[k - 1]) > so) so = fabsf(out[k] - out[k - 1]);
                if (fabsf(in[k] - in[k - 1]) > si) si = fabsf(in[k] - in[k - 1]);
            }
            printf("  steps around the jump and the seam: out %.3f, in %.3f\n", so, si);
            CHECK(so < 1.3f * si, "a click at a seam");
        }
    }

    /* 5b. a new recording while the loop is fully up: the old loop keeps playing, unchanged,
     *     while it is overwritten, and the new one takes over at the jump */
    {
        static float out[44100 * 75];
        float u[9] = {0, 0, 0, 7, 140, 170, 0, 0}, onoff = 1.0f;
        Mix m = {160.0, 0.0, 0.25, 0, 0, 1e9, 1e9, 21};
        double bar = 4 * beat_len(&m);
        long N = (long)(36 * bar * FS);
        float a1, b1, a2, b2;
        fresh();
        for (t = 0; t < N; t += 8) {
            double tt = t / FS;
            m.pattern = (tt >= m.t0 + 16 * bar - 0.002) ? 1 : 0;           /* B from bar 17 */
            if (tt > m.t0 + 4.5 * bar && tt < m.t0 + 4.5 * bar + 0.01) onoff = 0.0f;   /* 1st press */
            if (tt > m.t0 + 19.5 * bar && tt < m.t0 + 19.5 * bar + 0.01) onoff = 1.0f; /* 2nd press */
            u[0] = (tt > m.t0 + 18 * bar) ? 100.0f : 0.0f;                  /* loop full up */
            for (i = 0; i < 8; i++) b[i] = mix_at(&m, (t + i) / FS);
            block(u, onoff, b);
            for (i = 0; i < 8; i++) out[t + i] = b[i];
        }
        a1 = tone(out + (long)((m.t0 + 26 * bar) * FS), (long)(4 * bar * FS), 440);
        b1 = tone(out + (long)((m.t0 + 26 * bar) * FS), (long)(4 * bar * FS), 660);
        a2 = tone(out + (long)((m.t0 + 33.2 * bar) * FS), (long)(2 * bar * FS), 440);
        b2 = tone(out + (long)((m.t0 + 33.2 * bar) * FS), (long)(2 * bar * FS), 660);
        printf("re-record with the loop up: during it 440 %.1e / 660 %.1e (old loop A), after the jump 440 %.1e / 660 %.1e\n",
               a1, b1, a2, b2);
        CHECK(a1 > 20 * b1 && b2 > 20 * a2, "re-recording with the loop up");
    }

    /* 5c. Mode AUTO: 100 % loop for one loop, then a fade back to live over the next one,
     *     XFade untouched at LIVE; Mode MANUAL: XFade at 30 is obeyed from the jump on;
     *     AUTO with XFade at 50: the knob takes over when the fade meets it */
    {
        static float out[44100 * 50], in[44100 * 50];
        int mode;
        for (mode = 0; mode < 3; mode++) {
            float u[9] = {0, 0, 0, 3, 160, 160, 0, 0, 1}, onoff = 1.0f;   /* Bars 4, fixed 160 */
            Mix m = {160.0, 0.0, 0.25, 0, 0, 1e9, 1e9, 31};
            double bar = 4 * beat_len(&m);
            long N = (long)(22 * bar * FS);
            float ra, rb, rc, rd;
            if (mode == 1) u[8] = 2, u[0] = 30;
            if (mode == 2) u[0] = 50;
            fresh();
            for (t = 0; t < N; t += 8) {
                double tt = t / FS;
                m.pattern = (tt >= m.t0 + 8 * bar - 0.002) ? 1 : 0;           /* B from bar 9 */
                if (tt > m.t0 + 2.5 * bar && tt < m.t0 + 2.5 * bar + 0.01) onoff = 0.0f;
                for (i = 0; i < 8; i++) { in[t + i] = mix_at(&m, (t + i) / FS); b[i] = in[t + i]; }
                block(u, onoff, b);
                for (i = 0; i < 8; i++) out[t + i] = b[i];
            }
            /* share of the loop (A, 440 Hz) against live (B, 660 Hz) per stretch */
#define SHARE(from, len) (tone(out + (long)((m.t0 + (from) * bar) * FS), (long)((len) * bar * FS), 440) / \
                          (tone(out + (long)((m.t0 + (from) * bar) * FS), (long)((len) * bar * FS), 660) + 1e-12f))
            ra = SHARE(9, 3);        /* first loop after the jump        */
            rb = SHARE(13.2, 0.6);   /* early in AUTO's fade             */
            rc = SHARE(15.4, 0.6);   /* late in the fade                 */
            rd = SHARE(18, 3);       /* after it                         */
#undef SHARE
            if (mode == 0) {
                printf("AUTO, XFade LIVE: loop/live power %.0f (held), %.2f, %.3f (fading), %.4f (after), pickup %d\n", ra, rb, rc, rd, S.pickup);
                CHECK(ra > 1000 && rb > 0.3f && rb < 300 && rc < 0.3f && rd < 0.001f && !S.pickup, "Mode AUTO");
            } else if (mode == 1) {
                printf("MANUAL, XFade 30: loop/live power %.2f right after the jump (XFade 30 = loop at -4.4 dB under full live: ~0.36)\n", ra);
                CHECK(ra > 0.2f && ra < 0.6f && !S.pickup, "Mode MANUAL");
            } else {
                printf("AUTO, XFade 50: %.0f (held), %.2f (fade above 50), after: %.2f (both full: ~1), pickup %d\n", ra, rb, rd, S.pickup);
                CHECK(ra > 1000 && rd > 0.5f && rd < 2.0f && !S.pickup, "AUTO takeover at XFade 50");
            }
        }
    }

    /* 6. presses: a second press while armed does nothing; three quick ones reset bar 1 */
    {
        float u[9] = {0, 0, 0, 7, 140, 170, 0, 0};
        Mix m = {160.0, 0, 0.25, 0, 0, 1e9, 1e9, 11};
        float onoff = 1.0f;
        int armed_after_one, armed_after_two, state_after_three, beat_after;
        double bar = 4 * beat_len(&m);
        long tp[5] = {(long)(3.1 * bar * FS), (long)(3.6 * bar * FS), (long)(6.10 * bar * FS),
                      (long)(6.10 * bar * FS + 0.25 * FS), (long)(6.10 * bar * FS + 0.5 * FS)};
        int p = 0;
        fresh();
        armed_after_one = armed_after_two = state_after_three = beat_after = -1;
        for (t = 0; t < (long)(8 * bar * FS); t += 8) {
            if (p < 5 && t >= tp[p]) { onoff = 1.0f - onoff; p++; }
            for (i = 0; i < 8; i++) b[i] = mix_at(&m, (t + i) / FS);
            block(u, onoff, b);
            if (p == 1 && armed_after_one < 0) armed_after_one = S.armed;
            if (p == 2 && armed_after_two < 0) armed_after_two = S.armed;
            if (p == 5 && state_after_three < 0) state_after_three = S.cs;
            if (p == 5 && S.cs != SE_WAIT && beat_after < 0) beat_after = (int)((t / FS - m.t0) / beat_len(&m) + 0.5);
        }
        printf("presses: armed after 1: %d, after a slow 2nd: %d (rd %d); 3 quick taps -> state %d, then bar 1 on true beat %d\n",
               armed_after_one, armed_after_two, S.rd, state_after_three, beat_after);
        CHECK(armed_after_one == 1 && armed_after_two == 1, "arming");
        CHECK(state_after_three == SE_WAIT && !S.armed && !S.rec, "reset");
        CHECK(beat_after == 26 || beat_after == 27, "reset did not take the next kick as bar 1");
    }

    /* 7. Bars do not fit: fixed 100 BPM, 8 bars (19.2 s) -> 4 bars and a double beep */
    {
        float u[9] = {0, 0, 0, 7, 100, 100, 0, 0};
        Mix m = {100.0, 0, 0.25, 0, 0, 1e9, 1e9, 13};
        float onoff = 1.0f, beep = 0;
        long rec_beat = -1;
        double bar = 4 * beat_len(&m);
        fresh();
        for (t = 0; t < (long)(26 * bar * FS); t += 8) {
            double tt = t / FS;
            float in8[8];
            if (tt > 2.5 * bar && tt < 2.5 * bar + 0.01) onoff = 0.0f;
            for (i = 0; i < 8; i++) { in8[i] = b[i] = mix_at(&m, (t + i) / FS); }
            {
                int was = S.rec;
                block(u, onoff, b);
                if (!was && S.rec && rec_beat < 0) rec_beat = S.beat;
            }
            if (tt > 2.5 * bar && tt < 2.5 * bar + 0.25)
                for (i = 0; i < 8; i++) if (fabsf(b[i] - in8[i]) > beep) beep = fabsf(b[i] - in8[i]);
        }
        printf("100 BPM, Bars 8: records %d bars from beat %ld (bar %ld), beep peak %.2f, loop %.1f s\n",
               S.rd, rec_beat, rec_beat / 4 + 1, beep, S.L / FS);
        CHECK(S.rd == 4 && rec_beat == 16 && beep > 0.15f && S.has_loop && S.lbars == 4, "Bars fallback");
    }

    /* 8. Roll 1BAR, 2BEAT, 1BEAT, 1/2BT: while on, the output repeats every slice (the slices are the
     *    loop's own grid); after Roll OFF the output is exactly what it is in a run without
     *    Roll (the loop never moved) */
    {
        static float out[44100 * 50], ref[44100 * 50];
        static int pmv[44100 * 50], piv[44100 * 50];
        float rv[4] = {1, 2, 3, 4}, frac[4] = {4, 2, 1, 0.5f};
        int ri;
        for (ri = -1; ri < 4; ri++) {
            Mix m = {160.0, 0, 0.25, 0, 0, 1e9, 1e9, 17};
            double bar = 4 * beat_len(&m);
            float u[9] = {100, 0, 0, 1, 160, 160, 0, 0};      /* Bars 2, fixed 160, XFade LOOP */
            float onoff = 1.0f, err = 0, pk = 0, e2 = 0, len;
            float *o = (ri < 0) ? ref : out;
            long k, bl;
            fresh();
            for (t = 0; t < (long)(15 * bar * FS); t += 8) {
                double tt = t / FS;
                if (tt > 1.5 * bar && tt < 1.5 * bar + 0.01) onoff = 0.0f;
                u[2] = (ri >= 0 && tt > 8.3 * bar && tt < 12.37 * bar) ? rv[ri] : 0.0f;
                for (i = 0; i < 8; i++) b[i] = mix_at(&m, (t + i) / FS);
                block(u, onoff, b);
                for (i = 0; i < 8; i++) { o[t + i] = b[i]; pmv[t + i] = S.pm - 7 + i; piv[t + i] = S.pi - 7 + i; }
            }
            if (ri < 0) continue;
            len = frac[ri] * S.L / (4.0f * S.lbars);
            bl = (long)(len + 0.5f);
            for (k = (long)(9.5 * bar * FS); k < (long)(12 * bar * FS); k++) {
                float ph = (float)pmv[k] - len * floorf((float)pmv[k] / len), e;
                long d;
                if (pmv[k] < 0 || ph < SE_X + 16 || ph > len - 16) continue;      /* seams */
                /* the roll plays loop sample piv[k]: what the run without Roll played when
                 * its playhead was there, and that sample lies in the slice */
                d = pmv[k] - piv[k];
                if (d < 0) d += S.L;                 /* the loop wrapped since */
                e = fabsf(out[k] - ref[k - d]);
                if (piv[k] < S.rs || piv[k] > S.rs + bl + 1) e = 9.0f;
                if (e > err) err = e;
                if (fabsf(out[k]) > pk) pk = fabsf(out[k]);
            }
            for (k = (long)(12.6 * bar * FS); k < (long)(14.5 * bar * FS); k++) {
                float e = fabsf(out[k] - ref[k]);
                if (e > e2) e2 = e;
            }
            printf("Roll %s: repeats the %ld-sample slice from %d, max diff %.4f (peak %.2f); after OFF vs no Roll: %.5f\n",
                   ri == 0 ? "1BAR" : ri == 1 ? "2BEAT" : ri == 2 ? "1BEAT" : "1/2BT", bl, S.rs, err, pk, e2);
            CHECK(err < 0.02f && pk > 0.2f, "Roll does not repeat the slice");
            CHECK(e2 < 1e-4f, "Roll moved the loop");
        }
    }

    /* 9. LoCut: a loop of 50 Hz + 3 kHz; LoCut 50 (200 Hz) takes the 50 Hz down > 40 dB */
    {
        static float out[44100 * 16];
        float u[9] = {100, 0, 0, 0, 160, 160, 0, 0};          /* Bars 1 */
        float onoff = 1.0f;
        Mix m = {160.0, 0, 0.25, 0, 0, 1e9, 1e9, 19};
        double bar = 4 * beat_len(&m);
        long N = (long)(10 * bar * FS), a = (long)(7 * bar * FS), n = (long)(bar * FS);
        float lo0, hi0, lo1, hi1;
        fresh();
        for (t = 0; t < N; t += 8) {
            double tt = t / FS;
            if (tt > 0.6 * bar && tt < 0.6 * bar + 0.01) onoff = 0.0f;
            u[1] = (tt > 6.0 * bar) ? 50.0f : 0.0f;
            for (i = 0; i < 8; i++) {
                double x = (t + i) / FS;
                b[i] = mix_at(&m, x);
                if (x > 1.2 * bar) b[i] = 0.4f * sinf(PI2 * 50.0f * x) + 0.2f * sinf(PI2 * 3000.0f * x);
                if (x > 4.0 * bar) b[i] = 0;              /* live silent: only the loop is heard */
            }
            block(u, onoff, b);
            for (i = 0; i < 8; i++) out[t + i] = b[i];
        }
        lo0 = tone(out + (long)(5 * bar * FS), n, 50); hi0 = tone(out + (long)(5 * bar * FS), n, 3000);
        lo1 = tone(out + a, n, 50); hi1 = tone(out + a, n, 3000);
        printf("LoCut 200 Hz on the loop: 50 Hz %.1f dB, 3 kHz %.1f dB\n",
               10 * log10f(lo1 / lo0), 10 * log10f(hi1 / hi0));
        CHECK(10 * log10f(lo1 / lo0) < -40 && fabsf(10 * log10f(hi1 / hi0)) < 0.5f, "LoCut");
    }

    /* 10. labels */
    {
        char o[8];
        int n;
        struct { int k; unsigned v; const char *want; } L[] = {
            {0, 0, "LIVE"}, {0, 37, "37"}, {0, 100, "LOOP"}, {1, 0, "OFF"}, {1, 50, "200Hz"},
            {1, 100, "2.0k"}, {1, 90, "1.3k"}, {2, 0, "OFF"}, {2, 5, "1/2BT"},
            {2, 1, "1BAR"}, {2, 2, "2BEAT"}, {2, 3, "1BEAT"}, {2, 4, "1/2BT"}, {3, 0, "1"}, {3, 7, "8"}, {4, 12, "40"}, {5, 170, "170"},
            {6, 0, "AUTO"}, {6, 100, "100"}, {7, 0, "AUTO"}, {7, 1, "50Hz"}, {7, 101, "150Hz"},
            {8, 0, "JUMP"}, {8, 1, "AUTO"}, {8, 2, "MANU"}};
        for (i = 0; i < (int)(sizeof L / sizeof L[0]); i++) {
            memset(o, 0, sizeof o);
            if (L[i].k == 0) n = ZDL_GetLabel_0(L[i].v, o);
            else if (L[i].k == 1) n = ZDL_GetLabel_1(L[i].v, o);
            else if (L[i].k == 2) n = ZDL_GetLabel_2(L[i].v, o);
            else if (L[i].k == 3) n = ZDL_GetLabel_3(L[i].v, o);
            else if (L[i].k == 4) n = ZDL_GetLabel_4(L[i].v, o);
            else if (L[i].k == 5) n = ZDL_GetLabel_5(L[i].v, o);
            else if (L[i].k == 6) n = ZDL_GetLabel_6(L[i].v, o);
            else if (L[i].k == 7) n = ZDL_GetLabel_7(L[i].v, o);
            else n = ZDL_GetLabel_8(L[i].v, o);
            CHECK(strcmp(o, L[i].want) == 0 && n == (int)strlen(o) && n <= 5, "label %d/%u: '%s' want '%s'", L[i].k, L[i].v, o, L[i].want);
        }
        for (i = 0; i <= 100; i++) { n = ZDL_GetLabel_1((unsigned)i, o); CHECK(n <= 5, "LoCut label too long at %d", i); }
        printf("labels checked\n");
    }

    /* 11. 20 minutes of random knobs, presses, tempo jumps and silences: bounded, no NaN,
     *     every index inside the buffer */
    {
        float u[9] = {0, 0, 0, 7, 140, 170, 0, 0}, onoff = 1.0f, mx = 0;
        Mix m = {160.0, 0, 0.1, 0, 1, 1e9, 1e9, 23};
        unsigned int r = 99;
        long nbad = 0, oob = 0;
        fresh();
        for (t = 0; t < (long)(1200 * FS); t += 8) {
            r = r * 1664525u + 1013904223u;
            if ((r >> 8) % 20000 == 0) { int k = (r >> 4) % 9; float mx8[9] = {100, 100, 4, 7, 240, 240, 100, 101, 2};
                u[k] = (float)((r >> 12) % (unsigned)(mx8[k] + 1)); }
            if ((r >> 9) % 30000 == 0) onoff = 1.0f - onoff;
            if ((r >> 10) % 400000 == 0) m.bpm = 120 + (r >> 14) % 60;
            if ((r >> 11) % 300000 == 0) { m.kick_off_from = t / FS; m.kick_off_to = t / FS + 20; }
            for (i = 0; i < 8; i++) b[i] = mix_at(&m, (t + i) / FS);
            block(u, onoff, b);
            for (i = 0; i < 8; i++) { if (bad(b[i])) nbad++; if (fabsf(b[i]) > mx) mx = fabsf(b[i]); }
            if (S.pi < 0 || S.pi >= SE_N || S.po < 0 || S.po >= SE_N || S.wi < 0 || S.wi >= SE_N
                || (S.has_loop && (S.nj > S.L || S.nj <= 0 || S.rs < 0 || S.L > SE_WMAX
                                  || S.pi > S.L || S.pm < 0 || S.pm >= S.L || (S.fade > 0 && S.po > S.L + SE_X)))) oob++;
        }
        printf("random run: %ld bad samples, peak %.2f, %ld index problems, tempo %.1f BPM, state %d\n",
               nbad, mx, oob, SE_SPB / S.T, S.cs);
        CHECK(nbad == 0 && oob == 0, "random run");
    }

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
