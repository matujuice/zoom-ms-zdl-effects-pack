/* SGNL leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/sgnl/sgnl.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(SgState) + 64);
    return as_run(Fx_DLY_SGNL, sizeof(SgState), 13, 120000, "sgnl");
}
