/* DualShft: the LFO on the pedal's tempo and MIDI transport (see transport.h) */
#include "../src/custom/dualshft/dualshft.c"
typedef DualShift STATE;
#define PHASE(s) ((s)->lfo_phase)
#define ENTRY Fx_DLY_DualShft
#define MULT 1.0                                  /* Div default 1/4: one cycle per beat */
#define D(n) params[DUALSHFT_##n##_SLOT] = DUALSHFT_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(PTCH1); D(PTCH2); D(DLY1); D(DLY2); D(DIV); D(DEPTH); D(SHAPE); D(MIX); }
#include "transport.h"
