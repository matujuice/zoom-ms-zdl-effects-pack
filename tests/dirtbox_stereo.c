/* DirtBox: the box hears (L + R) / 2, the dry keeps L and R apart, DJ Mix (stereo up to 50, mono at 100). */
#include "stereo_entry.h"
#include "../src/custom/dirtbox/dirtbox.c"
#include "stereo_dry.h"
#define D(n) params[DIRTBOX_##n##_SLOT] = DIRTBOX_##n##_UI_DEFAULT / 100.0f
int main(void)
{
    se_init(1u << 20);
    D(MODEL); D(DRIVE); D(TONE); D(LOW); D(MID); D(HIGH); D(ZNR); D(LEVEL); D(MIX);
    params[DIRTBOX_ZNR_SLOT] = 0.0f;                 /* ZNR would gate a steady test tone */
    sd_test("DirtBox", Fx_DLY_DirtBox, DIRTBOX_MIX_SLOT, 0.4f);
    return sd_fails;
}
