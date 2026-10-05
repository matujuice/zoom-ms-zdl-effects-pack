/* Pump: Tempo-knob sync reset (see tempo_twin.h); phase = beats into the bar */
#include "../src/custom/pump/pump.c"
typedef PuState STATE;
#define PHASE(s) ((s)->bp)
#define ENTRY Fx_DLY_Pump
#define TEMPO_SLOT PUMP_TEMPO_SLOT
#define PLAIN_RESETS 0
#define D(n) params[PUMP_##n##_SLOT] = PUMP_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TARGT); D(SHAPE); D(DEPTH); D(DIV); D(SHIFT); D(CURVE); D(VERB); D(TEMPO); D(SIZE); }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#include "tempo_twin.h"
