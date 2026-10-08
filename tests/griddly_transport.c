/* GridDly: the beat clock (REVRS chunk phase) on the pedal's tempo and MIDI transport (see
 * transport.h). Time 1/8. synced; Tail OFF so switched off the input passes untouched. */
#include "../src/custom/griddly/griddly.c"
typedef GdState STATE;
#define PHASE(s) ((s)->ph)
#define ENTRY Fx_DLY_GridDly
#define MULT (1.0 / 0.75)                         /* Time default 1/8. = 3/4 beat: 4/3 chunks per beat */
#define D(n) params[GRIDDLY_##n##_SLOT] = GRIDDLY_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TYPE); D(TIME); D(FDBK); D(TONE); D(CHAR); D(DUCK); D(MIX);
                             params[GRIDDLY_TAIL_SLOT] = 0.0f; }
#include "transport.h"
