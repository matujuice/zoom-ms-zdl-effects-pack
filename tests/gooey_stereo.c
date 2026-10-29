/* Gooey through the real pedal entry (arena, knob conversion, defaults): the defaults (Thrsh OFF) leave
 * the buffer bit-exact, switched off it leaves the buffer alone, and with Thrsh down both sides are
 * reduced by the same gain (linked) and the quiet side follows the loud one. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
/* the live meter writes its band into a scratch cover; no firmware here, so no Event_post */
unsigned char _Fx_DLY_Gooey_Pic[2048];   /* the cover the linker would resolve */
#define ZM_FW_OK() 0u
#define ZM_POST() ((void)0)
#include "../src/custom/gooey/gooey.c"
#define D(n) params[GOOEY_##n##_SLOT] = GOOEY_##n##_UI_DEFAULT / 100.0f

int main(void)
{
    int fails = 0, b, i;
    float diff = 0, off = 0, gl = 0, gr = 0;
    se_init(65536);
    D(THRSH); D(RATIO); D(ATTCK); D(RELS); D(MKUP); D(MIX); D(SC_HP);
    for (b = 0; b < 500; b++) {
        float in[16];
        for (i = 0; i < 8; i++) {
            int k = b * 8 + i;
            in[i] = 0.5f * sinf(0.05f * (float)k); in[i + 8] = 0.2f * sinf(0.13f * (float)k + 1.0f);
            fx[i] = in[i]; fx[i + 8] = in[i + 8];
        }
        Fx_DLY_Gooey(ctx);
        for (i = 0; i < 16; i++) diff += fabsf(fx[i] - in[i]);
    }
    printf("defaults: difference to the input %.2e\n", diff);
    if (diff != 0.0f) { puts("FAIL: defaults not bit-exact"); fails++; }

    params[0] = 0.0f;                                                  /* effect off: untouched */
    for (i = 0; i < 16; i++) fx[i] = 0.5f * (float)(i + 1);
    Fx_DLY_Gooey(ctx);
    for (i = 0; i < 16; i++) off += fabsf(fx[i] - 0.5f * (float)(i + 1));
    if (off != 0.0f) { puts("FAIL: switched off but the buffer changed"); fails++; }
    params[0] = 1.0f;

    params[GOOEY_THRSH_SLOT] = 0.10f;                                  /* -30 dB */
    params[GOOEY_ATTCK_SLOT] = 0.02f;                                  /* 1 ms */
    for (b = 0; b < 2000; b++) {
        float in[16];
        for (i = 0; i < 8; i++) {
            int k = b * 8 + i;
            in[i] = 0.5f * sinf(0.05f * (float)k); in[i + 8] = 0.05f * sinf(0.05f * (float)k);
            fx[i] = in[i]; fx[i + 8] = in[i + 8];
        }
        Fx_DLY_Gooey(ctx);
        if (b >= 1900) for (i = 0; i < 16; i++) {
            float g;
            if (fabsf(in[i]) < 0.2f * ((i < 8) ? 1.0f : 0.1f)) continue;
            g = fx[i] / in[i];
            if (i < 8) gl = g; else gr = g;
        }
    }
    printf("Thrsh -30: gain L %.4f R %.4f (linked, under 1)\n", gl, gr);
    if (!(gl < 0.5f && fabsf(gl - gr) < 0.01f)) { puts("FAIL: stereo link through the entry"); fails++; }
    return fails;
}
