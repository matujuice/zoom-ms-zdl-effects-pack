/* DualShft leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/dualshft/dualshft.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(DualShift) + 64);
    return as_run(Fx_DLY_DualShft, sizeof(DualShift), 12, 120000, "dualshft");
}
