/* DubSiren: the synced LFO on the pedal's tempo and MIDI transport (see transport.h).
 * Trig Hold, Rate 1/4 synced; it runs while switched off (the echo rings out), so the
 * output is not the plain input then (OFF_DRY 0). */
#include "../src/custom/dubsiren/dubsiren.c"
typedef SirenState STATE;
#define PHASE(s) ((s)->lfo_ph)
#define ENTRY Fx_DLY_DubSiren
#define MULT 2.0                                  /* Rate 1/4, Mode Fast (x2): two cycles per beat */
#define OFF_DRY 0
#define D(n) params[DUBSIREN_##n##_SLOT] = DUBSIREN_##n##_UI_DEFAULT / 100.0f
static float *params;
static void defaults(void) { D(TRIG); D(MODE); D(PITCH); D(RATE); D(DEPTH); D(VOL); D(TIME); D(FDBK);
                             params[DUBSIREN_TRIG_SLOT] = 0.0f; params[DUBSIREN_RATE_SLOT] = 1.07f; }
#include "transport.h"
