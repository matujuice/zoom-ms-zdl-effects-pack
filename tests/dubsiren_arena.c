/* dubsiren leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/dubsiren/dubsiren.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(SirenState) + 64);
    return as_run(Fx_DLY_DubSiren, sizeof(SirenState), 13, 120000, "dubsiren");
}
