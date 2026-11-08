/* SCRUB leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/scrub/scrub.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(ScState) + 64);
    return as_run(Fx_DLY_Scrub, sizeof(ScState), 9, 120000, "scrub");
}
