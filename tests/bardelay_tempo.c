/* BarDelay: Tempo-knob sync reset (see tempo_twin.h); Time 1/4 synced, phase = REVRS chunk phase */
#include "../src/custom/bardelay/bardelay.c"
typedef BdState STATE;
#define PHASE(s) ((s)->ph)
#define ENTRY Fx_DLY_BarDelay
#define TEMPO_SLOT BARDELAY_TEMPO_SLOT
#define PLAIN_RESETS 0
#define D(n) params[BARDELAY_##n##_SLOT] = BARDELAY_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TYPE); D(TIME); D(FDBK); D(TONE); D(CHAR); D(DUCK); D(MIX); D(TEMPO); D(TAIL);
                             params[BARDELAY_TYPE_SLOT] = 0.03f;      /* REVRS */
                             params[BARDELAY_TIME_SLOT] = 1.09f;      /* 1/4 */
                             params[BARDELAY_TAIL_SLOT] = 0.0f; }     /* Tail OFF: off = untouched */
static int label(unsigned v, char *o) { return ZDL_GetLabel_7(v, o); }
#define HAS_FOLLOW 1
#include "tempo_twin.h"
