/* DualShft: the shifters hear (L + R) / 2, the dry keeps L and R apart, DJ Mix (stereo up to 50, mono at 100). */
#include "stereo_entry.h"
#include "../src/custom/dualshft/dualshft.c"
#include "stereo_dry.h"
#define D(n) params[DUALSHFT_##n##_SLOT] = DUALSHFT_##n##_UI_DEFAULT / 100.0f
int main(void)
{
    se_init(705536u);
    D(PTCH1); D(PTCH2); D(DLY1); D(DLY2); D(DIV); D(DEPTH); D(SHAPE); D(MIX);
    sd_test("DualShft", Fx_DLY_DualShft, DUALSHFT_MIX_SLOT, 0.4f);
    return sd_fails;
}
