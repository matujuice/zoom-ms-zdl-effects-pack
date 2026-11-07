/* eugate leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/eugate/eugate.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(ChState) + 64);
    return as_run(Fx_DLY_EuGate, sizeof(ChState), 12, 120000, "eugate");
}
