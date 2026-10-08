/* Sweep: the LFO on the pedal's tempo and MIDI transport (see transport.h) */
#include "../src/custom/sweep/sweep.c"
typedef SwState STATE;
#define PHASE(s) ((s)->ph)
#define ENTRY Fx_DLY_Sweep
#define MULT 0.25                                 /* Rate default 109 = 1 bar: a quarter cycle per beat */
#define D(n) params[SWEEP_##n##_SLOT] = SWEEP_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TYPE); D(RATE); D(DEPTH); D(CNTR); D(RESO); D(SHAPE); D(TONE); D(MIX); }
#include "transport.h"
