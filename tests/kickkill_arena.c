/* KickKill leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/kickkill/kickkill.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(KkState) + 64);
    return as_run(Fx_DLY_KickKill, sizeof(KkState), 7, 120000, "kickkill");
}
