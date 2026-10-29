/* LOUDER through the real pedal entry (arena, knob conversion, defaults): the defaults are
 * transparent (the input 64 samples late, both sides), switched off it leaves the buffer alone, and
 * with Gain up the output stays under Ceil on both sides. */
#include <stdio.h>
#include <math.h>
#include "stereo_entry.h"
/* the live meter writes its band into a scratch cover; no firmware here, so no Event_post */
unsigned char _Fx_DLY_LOUDER_Pic[2048];   /* the cover the linker would resolve */
#define ZM_FW_OK() 0u
#define ZM_POST() ((void)0)
#include "../src/custom/louder/louder.c"
#define D(n) params[LOUDER_##n##_SLOT] = LOUDER_##n##_UI_DEFAULT / 100.0f

int main(void)
{
    int fails = 0, b, i;
    float errL = 0, errR = 0, pk = 0, off = 0;
    static float inL[8 * 2000], inR[8 * 2000];
    se_init(65536);
    D(GAIN); D(CEIL); D(RELS); D(STYLE); D(MIX);
    for (b = 0; b < 2000; b++) {
        for (i = 0; i < 8; i++) {
            int k = b * 8 + i;
            inL[k] = 0.3f * sinf(0.05f * (float)k); inR[k] = 0.2f * sinf(0.13f * (float)k + 1.0f);
            fx[i] = inL[k]; fx[i + 8] = inR[k];
        }
        Fx_DLY_LOUDER(ctx);
        for (i = 0; i < 8; i++) {
            int k = b * 8 + i;
            if (k >= 64) { float e;
                e = fabsf(fx[i] - inL[k - 64]); if (e > errL) errL = e;
                e = fabsf(fx[i + 8] - inR[k - 64]); if (e > errR) errR = e; }
        }
    }
    printf("defaults: difference to the delayed input L %.2e R %.2e\n", errL, errR);
    if (errL > 1e-6f || errR > 1e-6f) { puts("FAIL: defaults not transparent"); fails++; }

    params[0] = 0.0f;                                                  /* effect off: untouched */
    for (i = 0; i < 16; i++) fx[i] = 0.5f * (float)(i + 1);
    Fx_DLY_LOUDER(ctx);
    for (i = 0; i < 16; i++) off += fabsf(fx[i] - 0.5f * (float)(i + 1));
    if (off != 0.0f) { puts("FAIL: switched off but the buffer changed"); fails++; }
    params[0] = 1.0f;

    params[LOUDER_GAIN_SLOT] = 1.8f;                                   /* +18 dB */
    params[LOUDER_CEIL_SLOT] = 0.5f;                                   /* -1.0 dB */
    for (b = 0; b < 2000; b++) {
        for (i = 0; i < 8; i++) {
            int k = b * 8 + i;
            fx[i] = (b > 800 && b < 900 ? 0.95f : 0.1f) * sinf(0.2f * (float)k); fx[i + 8] = 0.3f * sinf(0.07f * (float)k);
        }
        Fx_DLY_LOUDER(ctx);
        for (i = 0; i < 16; i++) if (fabsf(fx[i]) > pk) pk = fabsf(fx[i]);
    }
    printf("Gain +18, Ceil -1: peak out %.5f (ceiling 0.89125)\n", pk);
    if (pk > 0.89126f) { puts("FAIL: ceiling passed through the entry"); fails++; }
    return fails;
}
