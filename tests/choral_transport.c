/* Choral: the movement (Pace) on the pedal's tempo and MIDI transport (see transport.h) */
#include "../src/custom/formant/formant.c"
typedef ChState STATE;
#define PHASE(s) ((s)->lfo_ph)
#define ENTRY Fx_DLY_Formant
#define MULT 0.25                                 /* Pace default 1bar: a quarter cycle per beat */
#define D(n) params[FORMANT_##n##_SLOT] = FORMANT_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(CHOIR); D(SIZE); D(CHORD); D(SING); D(PACE); D(FEEL); D(GLIDE); D(MIX); }
#include "transport.h"
