/* gooey leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
#include "stereo_entry.h"
unsigned char _Fx_DLY_Gooey_Pic[2048];
#define ZM_FW_OK() 0u
#define ZM_POST() ((void)0)
#include "../src/custom/gooey/gooey.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(GoState) + 64);
    return as_run(Fx_DLY_Gooey, sizeof(GoState), 12, 120000, "gooey");
}
