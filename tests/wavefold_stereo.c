/* WaveFold: the fold hears (L + R) / 2, the dry keeps L and R apart, DJ Mix (stereo up to 50, mono at 100). */
#include "stereo_entry.h"
#include "../src/custom/wavefold/wavefold.c"
#include "stereo_dry.h"
#define D(n) params[WAVEFOLD_##n##_SLOT] = WAVEFOLD_##n##_UI_DEFAULT / 100.0f
int main(void)
{
    se_init(1u << 16);
    D(DRIVE); D(SYMM); D(TONE); D(LEVEL); D(MIX);
    sd_test("WaveFold", Fx_DLY_Wavefold, WAVEFOLD_MIX_SLOT, 0.4f);
    return sd_fails;
}
