/* DubSiren: Tempo-knob sync reset (see tempo_twin.h); Trig Hold, Rate 1/4 synced */
#include "../src/custom/dubsiren/dubsiren.c"
typedef SirenState STATE;
#define PHASE(s) ((s)->lfo_ph)
#define ENTRY Fx_DLY_DubSiren
#define TEMPO_SLOT DUBSIREN_TEMPO_SLOT
#define PLAIN_RESETS 0
#define OFF_DRY 0
#define D(n) params[DUBSIREN_##n##_SLOT] = DUBSIREN_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TRIG); D(MODE); D(PITCH); D(RATE); D(DEPTH); D(VOL); D(TIME); D(FDBK); D(TEMPO);
                             params[DUBSIREN_TRIG_SLOT] = 0.0f; params[DUBSIREN_RATE_SLOT] = 1.07f; }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#define HAS_FOLLOW 1
#include "tempo_twin.h"
