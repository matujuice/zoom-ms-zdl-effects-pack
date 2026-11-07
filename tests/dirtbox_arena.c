/* DirtBox leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/dirtbox/dirtbox.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(AbState) + 64);
    return as_run(Fx_DLY_DirtBox, sizeof(AbState), 13, 120000, "dirtbox");
}
