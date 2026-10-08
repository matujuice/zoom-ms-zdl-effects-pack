/* Breather: the pump's bar clock on the pedal's tempo and MIDI transport (see transport.h);
 * phase = beats into the bar / 4 */
#include "../src/custom/breather/breather.c"
typedef PuState STATE;
#define PHASE(s) ((s)->bp * 0.25f)
#define ENTRY Fx_DLY_Breather
#define MULT 0.25                                 /* the bar clock: one cycle per 4 beats */
#define D(n) params[BREATHER_##n##_SLOT] = BREATHER_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TARGT); D(SHAPE); D(DEPTH); D(DIV); D(SHIFT); D(CURVE); D(VERB); D(SIZE); }
#include "transport.h"
