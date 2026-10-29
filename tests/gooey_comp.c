/* Gooey compressor: static reduction vs ratio / threshold (soft knee), attack and release timing,
 * AUTO release, sidechain high-pass (detector only), makeup AUTO and fixed, Thrsh 0 = transparent,
 * linked stereo, Mix 50 parallel, no clicks. */
#define GOOEY_HOST_TEST
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
/* the live meter writes its band into a scratch cover; no firmware here, so no Event_post */
#define ZM_WANT_BLOB
static unsigned char zm_t_blob[2048];
#define ZM_PIC zm_t_blob
#define ZM_FW_OK() 0u
#define ZM_POST() ((void)0)
#include "../src/custom/gooey/gooey.c"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define SR 44096
#define MAXN (SR * 10)
static float inL[MAXN], inR[MAXN], outL[MAXN], outR[MAXN], tr[MAXN / 8];
static GoState S;

/* knobs in screen units: Thrsh 0..40, Ratio 0..3, Attck 0..5, Rels 0..4, MkUp 0..41, Mix 0..100, SC HP 0..20 */
typedef struct { float thr, rat, att, rel, mk, mix, hp; } K;
static K kd(void) { K k = { 40, 2, 4, 4, 1, 100, 0 }; return k; }

static void run(int n, K k)
{
    float u[7]; GoParams P; int b, i;
    u[0] = k.thr; u[1] = k.rat; u[2] = k.att; u[3] = k.rel; u[4] = k.mk; u[5] = k.mix; u[6] = k.hp;
    go_init(&S);
    for (b = 0; b < n / 8; b++) {
        float buf[16];
        for (i = 0; i < 8; i++) { buf[i] = inL[b * 8 + i]; buf[i + 8] = inR[b * 8 + i]; }
        go_prepare(&P, &S, u);
        go_process(&S, &P, buf);
        tr[b] = S.rS + S.rF;
        for (i = 0; i < 8; i++) { outL[b * 8 + i] = buf[i]; outR[b * 8 + i] = buf[i + 8]; }
    }
}
static void sine(float *x, int n, float f, float a, int from) { int i; for (i = from; i < n; i++) x[i] = a * sinf(6.2831853f * f * (float)i / 44100.0f); }
static float peak(const float *x, int a, int b) { float m = 0; int i; for (i = a; i < b; i++) if (fabsf(x[i]) > m) m = fabsf(x[i]); return m; }
static float db(float x) { return 20.0f * log10f(x); }
static float lin(float d) { return powf(10.0f, d / 20.0f); }
static unsigned int rng = 777;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }

/* steady reduction in dB of a 1 kHz sine at level_db with Thrsh thr_ui, measured on the output */
static float steady_gr(float thr_ui, float rat, float level_db, float mk)
{
    K k = kd(); float a = lin(level_db);
    k.thr = thr_ui; k.rat = rat; k.att = 2; k.rel = 3; k.mk = mk;
    sine(inL, SR, 1000.0f, a, 0); memcpy(inR, inL, sizeof(float) * SR);
    run(SR, k);
    return -db(peak(outL, SR - 2000, SR) / peak(inL, SR - 2000, SR));
}

int main(void)
{
    int i, n, b;
    static const float slope[4] = { 1.0f / 3.0f, 0.5f, 0.75f, 0.9f };

    /* ---- 1. static reduction: ratio and threshold (thr -30 dB, level -6 dB: 24 dB over) ---- */
    for (i = 0; i < 4; i++) {
        float gr = steady_gr(10.0f, (float)i, -6.0f, 1.0f), want = slope[i] * 24.0f;
        printf("ratio %d: GR %.2f dB, wanted %.2f\n", i, gr, want);
        CHECK(fabsf(gr - want) < 0.05f * want + 0.4f, "static GR ratio %d: %.2f, wanted %.2f", i, gr, want);
    }
    /* threshold: -20 dB, ratio 4, level -6: 14 dB over -> 10.5 */
    { float gr = steady_gr(20.0f, 2.0f, -6.0f, 1.0f); printf("thr -20: GR %.2f (10.5)\n", gr); CHECK(fabsf(gr - 10.5f) < 0.7f, "thr -20 GR %.2f", gr); }
    /* soft knee: at the threshold 0.56 dB (not 0, not a hard knee), 4 dB under it nothing, 4 dB over s*o */
    { float g0 = steady_gr(20.0f, 2.0f, -20.0f, 1.0f), gm = steady_gr(20.0f, 2.0f, -24.0f, 1.0f), gp = steady_gr(20.0f, 2.0f, -16.0f, 1.0f);
      printf("knee: at thr %.2f (0.56), 4 under %.2f (0), 4 over %.2f (3.0)\n", g0, gm, gp);
      CHECK(g0 > 0.2f && g0 < 1.0f, "GR at the threshold %.2f", g0);
      CHECK(gm < 0.15f, "GR 4 dB under the threshold %.2f", gm);
      CHECK(fabsf(gp - 3.0f) < 0.4f, "GR 4 dB over the threshold %.2f", gp); }

    /* ---- 2. attack / release timing (manual): time to 63 % of the final reduction ---- */
    {
        static const float attms[6] = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
        for (i = 2; i <= 5; i++) {
            K k = kd(); int step = SR / 4, bs = step / 8; float fin = 0, t63 = 0;
            k.thr = 10; k.rat = 2; k.att = (float)i; k.rel = 3;
            sine(inL, SR, 1000.0f, 0.005f, 0); sine(inL, SR * 2, 1000.0f, 0.5f, step);
            memcpy(inR, inL, sizeof(float) * SR * 2);
            run(SR * 2, k);
            for (b = SR * 2 / 8 - 20; b < SR * 2 / 8; b++) fin += tr[b] / 20.0f;
            for (b = bs; b < SR * 2 / 8; b++) if (tr[b] >= 0.632f * fin) { t63 = (float)(b - bs) * 8.0f / 44.1f; break; }
            printf("attack %.1f ms: t63 %.2f ms\n", attms[i], t63);
            if (i == 2) CHECK(t63 >= 0.5f && t63 <= 5.0f, "attack 1 ms gives %.2f", t63);
            else CHECK(t63 >= 0.55f * attms[i] && t63 <= 1.6f * attms[i], "attack %.0f ms gives %.2f", attms[i], t63);
        }
        { float rl[2] = { 0.3f, 1.2f }, tt[2]; int ri[2] = { 1, 3 }, r;
          for (r = 0; r < 2; r++) {
            K k = kd(); int stop = SR * 2, bs = stop / 8; float start;
            k.thr = 10; k.rat = 2; k.att = 2; k.rel = (float)ri[r];
            sine(inL, stop, 1000.0f, 0.5f, 0); for (i = stop; i < SR * 5; i++) inL[i] = 0.0f;
            memcpy(inR, inL, sizeof(float) * SR * 5);
            run(SR * 5, k);
            start = tr[bs - 1]; tt[r] = 0;
            for (b = bs; b < SR * 5 / 8; b++) if (tr[b] <= 0.368f * start) { tt[r] = (float)(b - bs) * 8.0f / 44100.0f; break; }
            printf("release %.1f s: t37 %.3f s\n", rl[r], tt[r]);
            CHECK(tt[r] >= 0.7f * rl[r] && tt[r] <= 1.5f * rl[r], "release %.1f s gives %.3f", rl[r], tt[r]);
          }
          CHECK(tt[1] > 3.0f * tt[0] && tt[1] < 5.0f * tt[0], "1.2 s vs 0.3 s ratio %.2f", tt[1] / tt[0]); }
    }

    /* ---- 3. AUTO release: a short burst lets go fast, a long loud passage lets go slowly ---- */
    {
        int len[2] = { SR / 16, SR * 4 }, r; float tt[2];
        for (r = 0; r < 2; r++) {
            K k = kd(); int stop = len[r], bs = stop / 8; float start;
            k.thr = 10; k.rat = 2; k.att = 2; k.rel = 4;
            sine(inL, stop, 1000.0f, 0.5f, 0); for (i = stop; i < stop + SR * 4; i++) inL[i] = 0.0f;
            memcpy(inR, inL, sizeof(float) * (stop + SR * 4));
            run(stop + SR * 4, k);
            start = tr[bs - 1]; tt[r] = 0;
            for (b = bs; b < (stop + SR * 4) / 8; b++) if (tr[b] <= 0.368f * start) { tt[r] = (float)(b - bs) * 8.0f / 44100.0f; break; }
            printf("AUTO release after %s: t37 %.3f s\n", r ? "4 s" : "60 ms", tt[r]);
        }
        CHECK(tt[0] < 0.25f, "AUTO after a short burst: %.3f s", tt[0]);
        CHECK(tt[1] > 3.0f * tt[0] && tt[1] > 0.4f, "AUTO after a long passage %.3f s vs burst %.3f", tt[1], tt[0]);
    }

    /* ---- 4. sidechain high-pass: a kick (50 Hz) pumps less, the audio is untouched ---- */
    {
        float gr_off, gr_on, gr_mid, d = 0;
        K k = kd(); k.thr = 20; k.rat = 2; k.att = 2; k.rel = 3;
        sine(inL, SR, 50.0f, 0.8f, 0); memcpy(inR, inL, sizeof(float) * SR);
        run(SR, k); gr_off = tr[SR / 8 - 1] * 6.0206f;
        k.hp = 20; run(SR, k); gr_on = tr[SR / 8 - 1] * 6.0206f;
        printf("kick 50 Hz: GR %.2f dB with SC HP off, %.2f with 300 Hz\n", gr_off, gr_on);
        CHECK(gr_off > 8.0f && gr_on < 0.2f * gr_off, "SC HP on the kick: %.2f -> %.2f", gr_off, gr_on);
        /* the same kick under the threshold with SC HP on: output == input (no filter in the audio path) */
        k.thr = 38; run(SR, k);
        for (i = 0; i < SR; i++) d += fabsf(outL[i] - inL[i]);
        CHECK(d == 0.0f, "audio changed with the detector under the threshold (%g)", d);
        /* a 2 kHz tone is not touched by the 300 Hz high-pass in the detector */
        k.thr = 20; k.hp = 0; sine(inL, SR, 2000.0f, 0.8f, 0); memcpy(inR, inL, sizeof(float) * SR); run(SR, k); gr_mid = tr[SR / 8 - 1];
        k.hp = 20; run(SR, k);
        printf("2 kHz: GR %.2f dB off, %.2f dB with 300 Hz\n", gr_mid * 6.0206f, tr[SR / 8 - 1] * 6.0206f);
        CHECK(fabsf(tr[SR / 8 - 1] - gr_mid) * 6.0206f < 0.5f, "SC HP 300 Hz moves the 2 kHz reduction");
        /* labels / frequency range: knob 1 = 20 Hz, 20 = 300 Hz */
        { char t[16]; ZDL_GetLabel_6(1, t); printf("SC HP 1 = %s, ", t); CHECK(!strcmp(t, "20Hz"), "label 1: %s", t);
          ZDL_GetLabel_6(20, t); printf("20 = %s\n", t); CHECK(!strcmp(t, "300Hz"), "label 20: %s", t);
          ZDL_GetLabel_6(0, t); CHECK(!strcmp(t, "OFF"), "label 0: %s", t); }
    }

    /* ---- 5. makeup: AUTO restores about the input level, fixed is a plain gain ---- */
    {
        float lvl, base, gr;
        K k = kd(); k.thr = 10; k.rat = 2; k.att = 2; k.rel = 3;
        sine(inL, SR * 10, 1000.0f, 0.5f, 0); memcpy(inR, inL, sizeof(float) * SR * 10);
        k.mk = 0; run(SR * 10, k);
        lvl = peak(outL, SR * 10 - 2000, SR * 10) / 0.5f;
        printf("MkUp AUTO after 10 s: output/input %.3f (%.2f dB)\n", lvl, db(lvl));
        CHECK(fabsf(db(lvl)) < 1.0f, "MkUp AUTO off by %.2f dB", db(lvl));
        k.mk = 1; run(SR * 2, k); base = peak(outL, SR * 2 - 2000, SR * 2); gr = -db(base / 0.5f);
        k.mk = 14; run(SR * 2, k);                                   /* +6.5 dB */
        printf("MkUp +6.5: gain over OFF %.2f dB (GR %.1f)\n", db(peak(outL, SR * 2 - 2000, SR * 2) / base), gr);
        CHECK(fabsf(db(peak(outL, SR * 2 - 2000, SR * 2) / base) - 6.5f) < 0.1f, "fixed makeup");
        { char t[16]; ZDL_GetLabel_4(2, t); CHECK(!strcmp(t, "+0.5"), "MkUp 2 label %s", t);
          ZDL_GetLabel_4(41, t); CHECK(!strcmp(t, "+20.0"), "MkUp 41 label %s", t);
          ZDL_GetLabel_4(1, t); CHECK(!strcmp(t, "OFF"), "MkUp 1 label %s", t); }
    }

    /* ---- 6. Thrsh 0 = transparent (bit exact, whatever Mix and MkUp say), gain ramps out cleanly ---- */
    {
        K k = kd(); float d = 0, mx = 0, gprev = 1.0f, gs;
        for (i = 0; i < SR; i++) { inL[i] = 0.9f * rnd(); inR[i] = 0.9f * rnd(); }
        run(SR, k);
        for (i = 0; i < SR; i++) d += fabsf(outL[i] - inL[i]) + fabsf(outR[i] - inR[i]);
        CHECK(d == 0.0f, "defaults not transparent (%g)", d);
        k.mk = 41; k.mix = 50; k.hp = 7; run(SR, k);
        d = 0; for (i = 0; i < SR; i++) d += fabsf(outL[i] - inL[i]) + fabsf(outR[i] - inR[i]);
        CHECK(d == 0.0f, "Thrsh 0 with MkUp and Mix 50 not transparent (%g)", d);
        printf("Thrsh 0: bit-exact bypass\n");
        /* compress, then Thrsh -> 0 in the middle: the gain returns to 1 in a ramp, no jump */
        sine(inL, SR, 1000.0f, 0.7f, 0); memcpy(inR, inL, sizeof(float) * SR);
        { float u[7] = { 10, 2, 2, 3, 1, 100, 0 }; GoParams P; go_init(&S);
          for (b = 0; b < SR / 8; b++) {
              float buf[16];
              u[0] = (b < SR / 16) ? 10.0f : 40.0f;
              for (i = 0; i < 8; i++) { buf[i] = inL[b * 8 + i]; buf[i + 8] = inR[b * 8 + i]; }
              go_prepare(&P, &S, u); go_process(&S, &P, buf);
              for (i = 0; i < 8; i++) { outL[b * 8 + i] = buf[i]; outR[b * 8 + i] = buf[i + 8]; }
          } }
        gprev = -1.0f;
        for (i = SR / 2 - 40; i < SR - 100; i++) {
            if (fabsf(inL[i]) > 0.3f) { gs = outL[i] / inL[i]; if (gprev > 0.0f && fabsf(gs - gprev) > mx) mx = fabsf(gs - gprev); gprev = gs; }
            else gprev = -1.0f;
        }
        gprev = outL[SR - 200] / inL[SR - 200] > 0 ? outL[SR - 1000] / inL[SR - 1000] : 1.0f;
        printf("Thrsh -> 0 mid-stream: largest gain step per sample %.3f, last gain %.4f\n", mx, gprev);
        CHECK(mx < 0.2f && fabsf(gprev - 1.0f) < 1e-4f, "bypass switch: step %.3f end %.4f", mx, gprev);
    }

    /* ---- 7. linked stereo: both sides get the one gain, from the louder side ---- */
    {
        K k = kd(); float gl, gr, gm;
        k.thr = 10; k.rat = 2; k.att = 2; k.rel = 3;
        sine(inL, SR, 1000.0f, 0.5f, 0); sine(inR, SR, 1300.0f, 0.01f, 0);
        run(SR, k);
        gl = peak(outL, SR - 2000, SR) / 0.5f; gr = peak(outR, SR - 2000, SR) / 0.01f;
        for (i = 0; i < SR; i++) inR[i] = inL[i];
        run(SR, k); gm = peak(outL, SR - 2000, SR) / 0.5f;
        printf("linked: gain L %.4f, R %.4f, with both loud %.4f\n", gl, gr, gm);
        CHECK(fabsf(gl - gr) < 0.01f, "L gain %.4f vs R gain %.4f", gl, gr);
        CHECK(fabsf(gl - gm) < 0.01f, "gain with R quiet %.4f vs both loud %.4f", gl, gm);
        CHECK(gl < 0.5f, "no reduction at all");
    }

    /* ---- 8. Mix 50 = parallel: linear dry/wet blend ---- */
    {
        K k = kd(); float g, o50, o25;
        k.thr = 10; k.rat = 2; k.att = 2; k.rel = 3;
        sine(inL, SR, 1000.0f, 0.5f, 0); memcpy(inR, inL, sizeof(float) * SR);
        run(SR, k); g = peak(outL, SR - 2000, SR) / 0.5f;
        k.mix = 50; run(SR, k); o50 = peak(outL, SR - 2000, SR) / 0.5f;
        k.mix = 25; run(SR, k); o25 = peak(outL, SR - 2000, SR) / 0.5f;
        printf("Mix 50: %.3f (0.5 + 0.5g = %.3f); Mix 25: %.3f (%.3f)\n", o50, 0.5f + 0.5f * g, o25, 0.75f + 0.25f * g);
        CHECK(fabsf(o50 - (0.5f + 0.5f * g)) < 0.01f, "Mix 50 %.3f vs %.3f", o50, 0.5f + 0.5f * g);
        CHECK(fabsf(o25 - (0.75f + 0.25f * g)) < 0.01f, "Mix 25 %.3f vs %.3f", o25, 0.75f + 0.25f * g);
        k.mix = 0; run(SR, k); n = 0; for (i = 0; i < SR; i++) if (outL[i] != inL[i]) n++;
        CHECK(n == 0, "Mix 0 changed %d samples", n);
    }

    /* ---- 9. no clicks: the gain moves in ramps even at the fastest attack, no NaN ---- */
    {
        K k = kd(); float mx = 0, gp = 1.0f, gs; int bad = 0;
        k.thr = 0; k.rat = 3; k.att = 0; k.rel = 0;
        for (i = 0; i < SR * 2; i++) {
            float a = ((i / 6000) % 2) ? 0.9f : 0.02f;
            inL[i] = a * sinf(0.09f * (float)i); inR[i] = 0.5f * inL[i];
        }
        run(SR * 2, k);
        for (i = 100; i < SR * 2; i++) {
            if (!(fabsf(outL[i]) < 4.0f) || !(fabsf(outR[i]) < 4.0f)) bad++;
            if (fabsf(inL[i]) > 0.2f) { gs = outL[i] / inL[i]; if (gp > 0.0f && fabsf(gs - gp) > mx) mx = fabsf(gs - gp); gp = gs; }
            else gp = -1.0f;
        }
        printf("fastest attack, 10:1: largest gain step per sample %.3f, bad samples %d\n", mx, bad);
        CHECK(bad == 0, "%d NaN or huge samples", bad);
        CHECK(mx < 0.16f, "gain step %.3f per sample (a click)", mx);
    }

    /* ---- labels ---- */
    {
        char t[16];
        ZDL_GetLabel_0(0, t);  CHECK(!strcmp(t, "-40"), "Thrsh 0 label %s", t);
        ZDL_GetLabel_0(40, t); CHECK(!strcmp(t, "0"), "Thrsh 40 label %s", t);
        ZDL_GetLabel_1(0, t);  CHECK(!strcmp(t, "1.5:1"), "Ratio 0 label %s", t);
        ZDL_GetLabel_1(3, t);  CHECK(!strcmp(t, "10:1"), "Ratio 3 label %s", t);
        ZDL_GetLabel_2(0, t);  CHECK(!strcmp(t, "0.1ms"), "Attck 0 label %s", t);
        ZDL_GetLabel_2(5, t);  CHECK(!strcmp(t, "30ms"), "Attck 5 label %s", t);
        ZDL_GetLabel_3(3, t);  CHECK(!strcmp(t, "1.2s"), "Rels 3 label %s", t);
        ZDL_GetLabel_3(4, t);  CHECK(!strcmp(t, "AUTO"), "Rels 4 label %s", t);
        ZDL_GetLabel_3(2, t);  CHECK(!strcmp(t, "0.6s"), "Rels 2 label %s", t);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
