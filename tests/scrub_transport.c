/* Scrub: a synced grain (1/4) on the pedal's tempo and MIDI transport (see transport.h);
 * phase = grain phase p. Free grains ignore the clock (checked at the end). */
#include "../src/custom/scrub/scrub.c"
typedef ScState STATE;
#define PHASE(s) ((s)->p)
#define ENTRY Fx_DLY_Scrub
#define MULT 1.0                                  /* Grain 110 = 1/4: one grain per beat */
#define OFF_RUNS 0        /* Scrub: grains start afresh when switched on */
#define D(n) params[SCRUB_##n##_SLOT] = SCRUB_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(POS); D(GRAIN); D(REC); D(GLIDE); D(DIR); D(SPRAY); D(MIX);
                             params[SCRUB_GRAIN_SLOT] = 1.10f; }
#define main transport_main
#include "transport.h"
#undef main

int main(void)
{
    float a; int r = transport_main(), j;
    /* free grain (default 100 ms) while the clock runs: p keeps its own speed, no lock */
    params[0] = 1.0f; params[SCRUB_GRAIN_SLOT] = SCRUB_GRAIN_UI_DEFAULT / 100.0f;
    zt[2] = 1u; zt[3]++; zt[4] = 1u; zt[1] += 2u; block();
    for (j = 0; j < 50; j++) {
        a = PHASE(st); zt[1] += 2u; zt[4] += 1u; block();
        if (fabs(pdist(PHASE(st), a) - 8.0 / 4410.0) > 1e-4) { printf("FAIL: free grain moved by the clock\n"); return 1; }
    }
    printf("free grain ignores the clock: ok\n");
    return r;
}
