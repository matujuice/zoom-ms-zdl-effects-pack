/* Looper: the song grid on the pedal's tempo and MIDI transport (see transport.h). PHASE is
 * the beat phase of the grid. Switched off (= a press) it keeps following the clock, and the
 * input passes untouched: with Mix 50 the live part is full and nothing plays yet. */
#include "../src/custom/looper/looper.c"
typedef LpState STATE;
/* the state is sealed between blocks (#41): read an unsealed copy */
static float lt_phase(const LpState *s) { LpState c = *s; lp_unseal(&c); return ((float)(c.gu & 7) + c.gf) * 0.125f; }
#define PHASE(s) lt_phase(s)
#define ENTRY Fx_DLY_Looper
#define MULT 1.0
#define D(n) params[LOOPER_##n##_SLOT] = LOOPER_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(MIX); D(ROLL); D(SYNC); D(NEXT); D(CYCLE); D(STOP); }
#include "transport.h"
