/* GRIDDLY leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/griddly/griddly.c"
#include "arena_safe.h"
#define ARENA 705536u
int main(void)
{
    se_init(ARENA);
    return as_run(Fx_DLY_GridDly, sizeof(GdState), 13, 120000, "griddly");
}
