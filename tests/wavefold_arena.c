/* WAVEFOLD leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/wavefold/wavefold.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(WfState) + 64);
    return as_run(Fx_DLY_Wavefold, sizeof(WfState), 9, 120000, "wavefold");
}
