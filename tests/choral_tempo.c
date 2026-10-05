/* Choral: Tempo-knob sync reset (see tempo_twin.h) */
#include "../src/custom/formant/formant.c"
typedef FmState STATE;
#define PHASE(s) ((s)->lfo_ph)
#define ENTRY Fx_DLY_Formant
#define TEMPO_SLOT FORMANT_TEMPO_SLOT
#define PLAIN_RESETS 1
#define D(n) params[FORMANT_##n##_SLOT] = FORMANT_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(VOWEL); D(RESO); D(CHORD); D(PARAM); D(TEMPO); D(DIV); D(SHAPE); D(DEPTH); D(MIX); }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#include "tempo_twin.h"
