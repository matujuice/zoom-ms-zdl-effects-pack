/* DualShft: Tempo-knob sync reset (see tempo_twin.h) */
#include "../src/custom/dualshft/dualshft.c"
typedef DualShift STATE;
#define PHASE(s) ((s)->lfo_phase)
#define ENTRY Fx_DLY_DualShft
#define TEMPO_SLOT DUALSHFT_TEMPO_SLOT
#define PLAIN_RESETS 1
#define D(n) params[DUALSHFT_##n##_SLOT] = DUALSHFT_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(PTCH1); D(PTCH2); D(DLY1); D(DLY2); D(TEMPO); D(DIV); D(DEPTH); D(SHAPE); D(MIX); }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#include "tempo_twin.h"
