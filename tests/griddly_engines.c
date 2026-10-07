/* GridDly engines: runs the real pedal entry on every Type and checks
 *   - labels (Type, Time, Tail)
 *   - DIGI: a click comes back at the set Time (free 500 ms and synced 1/4 at 120), Fdbk decays
 *   - every engine stays finite and under +-2.5 with noise in, Fdbk 120, Char 0 and 100
 *   - Fdbk 100 = 1:1: every engine fades at 100 and drones at 120 (DUB with dark Tone excepted);
 *     DIGI at Fdbk 45 dies away
 *   - REVRS plays something back (reversed chunks), TAPS has three echoes before the 1/4
 *   - Duck 100 lowers the repeats while the input plays
 *   - switched off: Tail OFF = output equals input; Tail ON = input plus decaying repeats */
#include "../src/custom/griddly/griddly.c"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mman.h>

#define ARENA_BYTES (1u << 20)
static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *fx, *params;
static unsigned char *arena;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[GRIDDLY_##n##_SLOT] = GRIDDLY_##n##_UI_DEFAULT / 100.0f
#define SET(n, v) params[GRIDDLY_##n##_SLOT] = (v) / 100.0f

static void setup(void)
{
    if (!m) m = mmap(0, 2u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768); arena = m + 4096;
    memset(arena, 0xC9, ARENA_BYTES);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + ARENA_BYTES);
    desc[2] = ARENA_BYTES;
    params[0] = 1.0f;
    D(TYPE); D(TIME); D(FDBK); D(TONE); D(CHAR); D(DUCK); D(MIX); D(TEMPO); D(TAIL);
}

#define MAXS (44100 * 6)
static float out[MAXS];
static unsigned int rng = 1u;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 9) * 1.1920929e-7f - 1.0f; }

/* in: 0 = silence, 1 = click at sample 0, 2 = noise at amp */
static void run(int n, int kind, float amp)
{
    int b, j, k = 0;
    for (b = 0; b < n / 8; b++) {
        for (j = 0; j < 8; j++, k++) {
            float x = 0.0f;
            if (kind == 1 && k == 0) x = amp;
            if (kind == 2) x = amp * noise();
            fx[j] = x; fx[j + 8] = x;
        }
        Fx_DLY_GridDly(ctx);
        for (j = 0; j < 8; j++) out[b * 8 + j] = fx[j];
    }
}
static void settle(void) { run(44100, 0, 0.0f); }         /* past the ring clearing */
static float peak(int a, int b) { float p = 0; int i; for (i = a; i < b; i++) { float v = fabsf(out[i]); if (!(v == v)) return 1e9f; if (v > p) p = v; } return p; }
static float area(int a, int b) { float s = 0; int i; for (i = a; i < b; i++) s += fabsf(out[i]); return s; }
static int argmax(int a, int b) { int i, k = a; for (i = a; i < b; i++) if (fabsf(out[i]) > fabsf(out[k])) k = i; return k; }
static float rms(int a, int b) { double s = 0; int i; for (i = a; i < b; i++) s += out[i] * out[i]; return (float)sqrt(s / (b - a)); }

int main(void)
{
    char t[8];
    int i, k, ty;
    const char *types[6] = {"DIGI", "TAPE", "DUB", "REVRS", "TAPS", "LOFI"};

    for (i = 0; i < 6; i++) { ZDL_GetLabel_0(i, t); CHECK(strcmp(t, types[i]) == 0, "Type %d label %s", i, t); }
    ZDL_GetLabel_1(70, t);  CHECK(strcmp(t, "496ms") == 0, "Time 70 label %s", t);
    ZDL_GetLabel_1(100, t); CHECK(strcmp(t, "1.00s") == 0, "Time 100 label %s", t);
    ZDL_GetLabel_1(108, t); CHECK(strcmp(t, "1/8.") == 0, "Time 108 label %s", t);
    ZDL_GetLabel_1(102, t); CHECK(strcmp(t, "1/16T") == 0, "Time 102 label %s", t);
    ZDL_GetLabel_1(113, t); CHECK(strcmp(t, "2bar") == 0, "Time 113 label %s", t);
    ZDL_GetLabel_8(0, t);   CHECK(strcmp(t, "OFF") == 0, "Tail 0 label %s", t);
    ZDL_GetLabel_8(1, t);   CHECK(strcmp(t, "ON") == 0, "Tail 1 label %s", t);

    /* DIGI click: free 70 = 496 ms = 21873.6 samples; Mix 100 = wet only */
    setup(); SET(MIX, 100); SET(TIME, 70); SET(CHAR, 0); SET(FDBK, 50); SET(TONE, 100); settle();
    run(44100 * 2, 1, 0.5f);
    k = argmax(100, 30000);
    printf("DIGI 496ms click back at %d samples (want ~21874), level %.3f\n", k, out[k]);
    CHECK(k >= 21872 && k <= 21876, "DIGI echo at %d", k);
    CHECK(fabsf(out[k]) + fabsf(out[k - 1]) + fabsf(out[k + 1]) > 0.45f, "DIGI first echo too quiet %.3f", out[k]); /* split by the fractional delay */
    {   int k2 = argmax(k + 100, k + 30000);
        printf("second echo at %d, level %.3f\n", k2, out[k2]);
        CHECK(k2 - k >= 21872 && k2 - k <= 21876 && fabsf(out[k2]) < fabsf(out[k]), "DIGI second echo wrong"); }

    /* synced 1/4 at 120 = 22050 */
    setup(); SET(MIX, 100); SET(TIME, 109); SET(CHAR, 0); SET(TONE, 100); settle();
    run(44100, 1, 0.5f);
    k = argmax(100, 40000);
    printf("DIGI 1/4 at 120: %d (want 22050)\n", k);
    CHECK(k >= 22049 && k <= 22052, "synced echo at %d", k);

    /* TAPS: three echoes before the 1/4 (pattern 0 = 1/4 1/2 1 of the time) */
    setup(); SET(TYPE, 4); SET(MIX, 100); SET(TIME, 109); SET(CHAR, 0); SET(FDBK, 0); SET(TONE, 100); settle();
    run(44100, 1, 0.5f);
    printf("TAPS (sum around each tap): %.3f @5513 %.3f @11025 %.3f @22050, between %.4f\n",
           area(5505, 5520), area(11018, 11032), area(22043, 22057), area(12000, 21000));
    CHECK(area(5505, 5520) > 0.38f && area(11018, 11032) > 0.28f && area(22043, 22057) > 0.2f, "TAPS taps missing");
    CHECK(area(5505, 5520) > area(11018, 11032) && area(11018, 11032) > area(22043, 22057), "TAPS levels do not fall");

    /* every engine: noise, Fdbk 120, Char 0 and 100, Tone 0 and 100 */
    for (ty = 0; ty < 6; ty++) for (i = 0; i < 4; i++) {
        float p;
        setup(); SET(TYPE, ty); SET(FDBK, 120); SET(CHAR, (i & 1) ? 100 : 0); SET(TONE, (i & 2) ? 100 : 0);
        SET(MIX, 100); SET(TIME, 103); settle();
        run(44100 * 3, 2, 0.9f);
        p = peak(0, 44100 * 3);
        CHECK(p < 2.5f, "%s Char %d Tone %d: peak %.3f (or NaN)", types[ty], (i & 1) * 100, (i >> 1) * 100, p);
    }

    /* Fdbk: 100 = 1:1 into the loop, the filters still take their share; 120 drones.
     * Change in dB from 1..2 s to 4..5 s after a noise burst, Tone 0 and 100, Char 0. */
    {
        static const int fd[4] = {90, 100, 110, 120};
        unsigned int rng0 = rng;
        float db[4][6][2];
        int f, tn;
        for (f = 0; f < 4; f++) {
            printf("Fdbk %3d dB from 1..2 s to 4..5 s:", fd[f]);
            for (ty = 0; ty < 6; ty++) for (tn = 0; tn < 2; tn++) {
                float a, b;
                setup(); SET(TYPE, ty); SET(FDBK, fd[f]); SET(CHAR, 0); SET(TONE, tn * 100);
                SET(MIX, 100); SET(TIME, 106); settle();
                run(22048, 2, 0.5f); run(44100 * 5, 0, 0.0f);
                a = rms(44100, 88200) + 1e-9f; b = rms(44100 * 4, 44100 * 5) + 1e-9f;
                db[f][ty][tn] = 20.0f * log10f(b / a);
                printf(" %s/%d %+.1f", types[ty], tn * 100, db[f][ty][tn]);
                if (fd[f] <= 100) CHECK(db[f][ty][tn] < -2.0f, "%s Tone %d Fdbk %d does not die away", types[ty], tn * 100, fd[f]);
                if (fd[f] == 120 && !(ty == 2 && tn == 0))
                    CHECK(db[f][ty][tn] > 0.0f && b < 2.5f, "%s Tone %d Fdbk 120 does not grow into a drone (rms %.3f)", types[ty], tn * 100, b);
            }
            puts("");
        }
        rng = rng0;
    }
    setup(); SET(MIX, 100); SET(TIME, 106); settle();
    run(44100, 2, 0.5f); run(44100 * 5, 0, 0.0f);
    printf("DIGI Fdbk 45, 4..5 s after: rms %.6f\n", rms(44100 * 4, 44100 * 5));
    CHECK(rms(44100 * 4, 44100 * 5) < 1e-3f, "DIGI at Fdbk 45 does not decay");

    /* REVRS: something comes back, and it is not the plain delay */
    setup(); SET(TYPE, 3); SET(MIX, 100); SET(TIME, 109); SET(FDBK, 0); settle();
    run(44100 * 2, 2, 0.5f);
    printf("REVRS wet rms %.3f\n", rms(44100, 88200));
    CHECK(rms(44100, 88200) > 0.05f, "REVRS silent");

    /* LOFI at Char 100: the output only moves every 8 samples or so */
    setup(); SET(TYPE, 5); SET(MIX, 100); SET(TIME, 103); SET(CHAR, 100); SET(FDBK, 0); SET(TONE, 100); settle();
    run(44100, 2, 0.5f);
    { int ch = 0; for (i = 20000; i < 30000; i++) if (out[i] != out[i - 1]) ch++;
      printf("LOFI Char 100: %d changes in 10000 samples\n", ch);
      CHECK(ch < 2000, "LOFI hold not working (%d changes)", ch); }

    /* Duck: wet level with the input playing, Duck 0 vs 100 */
    {   float r0, r1;
        setup(); SET(MIX, 100); SET(TIME, 103); settle(); run(44100, 2, 0.5f); r0 = rms(22050, 44100);
        setup(); SET(MIX, 100); SET(TIME, 103); SET(DUCK, 100); settle(); run(44100, 2, 0.5f); r1 = rms(22050, 44100);
        printf("Duck 0 wet rms %.3f, Duck 100 %.3f\n", r0, r1);
        CHECK(r1 < 0.2f * r0, "Duck 100 does not duck"); }

    /* switched off */
    {   int same = 1, b, j;
        setup(); SET(TAIL, 0); SET(TIME, 103); settle(); run(44100, 2, 0.5f);
        params[0] = 0.0f;
        for (b = 0; b < 2000; b++) {
            float in[8];
            for (j = 0; j < 8; j++) { in[j] = 0.5f * noise(); fx[j] = in[j]; fx[j + 8] = in[j]; }
            Fx_DLY_GridDly(ctx);
            for (j = 0; j < 8; j++) if (fx[j] != in[j]) same = 0;
        }
        CHECK(same, "Tail OFF: switched off but the input was changed");
        setup(); SET(TAIL, 100); SET(TIME, 103); SET(FDBK, 60); settle(); run(44100, 2, 0.5f);
        params[0] = 0.0f;
        run(44100 * 3, 0, 0.0f);
        printf("Tail ON, off, silence in: rms 0..0.5 s %.4f, 2.5..3 s %.6f\n", rms(0, 22050), rms(44100 * 5 / 2, 44100 * 3));
        CHECK(rms(0, 22050) > 0.02f, "Tail ON: no repeats after switching off");
        CHECK(rms(44100 * 5 / 2, 44100 * 3) < 0.25f * rms(0, 22050), "Tail ON: repeats do not decay");
        setup(); SET(TAIL, 0); SET(TIME, 103); SET(FDBK, 60); settle(); run(44100, 2, 0.5f);
        params[0] = 0.0f;
        run(44100, 0, 0.0f);
        printf("Tail OFF, off, silence in: peak %g at %d\n", peak(0, 44096), argmax(0, 44096));
        CHECK(peak(0, 44096) == 0.0f, "Tail OFF: repeats after switching off");
    }

    /* DIGI 2bar at 120 latches a 176400-sample chunk; switched to REVRS mid-chunk the
     * backward read must stay inside the 348000-sample ring (it read up to 352800 before) */
    {
        GdState *st;
        setup(); SET(TYPE, 0); SET(TIME, 113); SET(TEMPO, 120); settle();
        run(44100 * 2, 2, 0.3f);
        SET(TYPE, 3); run(8, 2, 0.3f);
        st = (GdState *)(((uintptr_t)arena + 3u) & ~(uintptr_t)3u);
        printf("REVRS after DIGI 2bar: chunk %.0f samples (max %.0f)\n", st->L, GD_MAXD * 0.5f);
        CHECK(st->L <= GD_MAXD * 0.5f, "REVRS: chunk latched on DIGI reads past the ring");
        run(44100 * 3, 2, 0.3f);
        CHECK(peak(0, 44100 * 3) < 2.5f, "REVRS after DIGI 2bar: output not finite or too loud");
    }

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
