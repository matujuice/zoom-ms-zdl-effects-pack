/* EuGate: Tempo-knob sync reset (see tempo_twin.h); phase = steps into the pattern */
#include "../src/custom/eugate/eugate.c"
typedef ChState STATE;
#define PHASE(s) ((float)(s)->pos + (s)->pp)
#define ENTRY Fx_DLY_EuGate
#define TEMPO_SLOT EUGATE_TEMPO_SLOT
#define PLAIN_RESETS 0
#define D(n) params[EUGATE_##n##_SLOT] = EUGATE_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(NOTES); D(STEPS); params[EUGATE_STEPS_SLOT] = 0.63f; D(SHIFT); D(SWING); D(RESET); D(GAP); D(SOFT); D(TEMPO); D(MIX); }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#define HAS_FOLLOW 1
#include "tempo_twin.h"
