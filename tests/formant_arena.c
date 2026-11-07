/* formant leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
#include "../src/custom/formant/formant.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(ChState) + 64);
    return as_run(Fx_DLY_Formant, sizeof(ChState), 13, 120000, "formant");
}
