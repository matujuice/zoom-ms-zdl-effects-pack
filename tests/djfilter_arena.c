/* DJFilter leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/djfilter/djfilter.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(DfState) + 64);
    return as_run(Fx_DLY_DJFilter, sizeof(DfState), 11, 120000, "djfilter");
}
