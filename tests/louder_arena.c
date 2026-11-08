/* LOUDER leaves its slot's arena safe for a stock delay or reverb (MIDISTOMP #60) */
/* no firmware here: the meter writes into a scratch cover */
#include <stdio.h>
unsigned char _Fx_DLY_LOUDER_Pic[2048];
#define ZM_FW_OK() 0u
#define ZM_POST() ((void)0)
#include "stereo_entry.h"
#include "../src/custom/louder/louder.c"
#include "arena_safe.h"
int main(void)
{
    se_init(sizeof(LoState) + 64);
    return as_run(Fx_DLY_LOUDER, sizeof(LoState), 10, 120000, "louder");
}
