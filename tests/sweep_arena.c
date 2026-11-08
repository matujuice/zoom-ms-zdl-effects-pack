/* SWEEP leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/sweep/sweep.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(SwState) + 64);
    return as_run(Fx_DLY_Sweep, sizeof(SwState), 12, 120000, "sweep");
}
