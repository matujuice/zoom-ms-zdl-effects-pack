/* TAKEOFF leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/takeoff/takeoff.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(TkState) + 64);
    return as_run(Fx_DLY_Takeoff, sizeof(TkState), 13, 120000, "takeoff");
}
