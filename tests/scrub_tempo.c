/* Scrub: Tempo-knob sync reset (see tempo_twin.h); Grain 1/8 synced, phase = grain phase */
#include "../src/custom/scrub/scrub.c"
typedef ScState STATE;
#define PHASE(s) ((s)->p)
#define ENTRY Fx_DLY_Scrub
#define TEMPO_SLOT SCRUB_TEMPO_SLOT
#define PLAIN_RESETS 0
#define OFF_RUNS 0        /* Scrub: grains start afresh when switched on */
#define D(n) params[SCRUB_##n##_SLOT] = SCRUB_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(POS); D(GRAIN); D(REC); D(GLIDE); D(DIR); D(SPRAY); D(MIX); D(TEMPO);
                             params[SCRUB_GRAIN_SLOT] = 1.07f; }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#define HAS_FOLLOW 1
#include "tempo_twin.h"
