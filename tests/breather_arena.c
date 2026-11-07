/* Breather leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/breather/breather.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(PuState) + 64);
    return as_run(Fx_DLY_Breather, sizeof(PuState), 12, 120000, "breather");
}
