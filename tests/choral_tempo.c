#include "../src/custom/formant/formant.c"
typedef ChState STATE;
#define PHASE(s) ((s)->lfo_ph)
#define ENTRY Fx_DLY_Formant
#define TEMPO_SLOT FORMANT_TEMPO_SLOT
#define PLAIN_RESETS 0
#define D(n) params[FORMANT_##n##_SLOT] = FORMANT_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(CHOIR); D(SIZE); D(CHORD); D(SING); D(PACE); D(FEEL); D(GLIDE); D(TEMPO); D(MIX); }
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#define HAS_FOLLOW 1
#include "tempo_twin.h"
