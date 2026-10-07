/* Bar tag with several senders (Luca, 2026-10-06: a FOLLW EuGate in slot 4 ran free while
 * Mozaic pads were on for more than one slot). Four EuGate slots share the Dry buffer; Mozaic
 * flips slots 1-3 when their pad is on (a few blocks apart), slot 4 is on FOLLW. Every mix of
 * pads, FOLLW / BPM in slots 2-3, a patch reload, and one pad turned off mid-run, through three
 * guesses at how the pedal keeps the Dry buffer between blocks: kept, two buffers swapped every
 * block, cleared. Slot 4 must restart on every bar (from about 2 bars after a pad goes off)
 * whenever any pad is on. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../src/custom/eugate/eugate.c"

#define ARENA (1u << 16)
#define BAR   1103L                      /* blocks between Mozaic flips (short, to test fast) */
typedef struct { unsigned int *ctx, *magic, *desc; float *params, *fx; unsigned char *arena; } Slot;
static unsigned char *M;
static int twin[4];

static void reload(Slot *s) { memset(s->arena, 0xC9, ARENA); }
static void make(Slot *s, int k)
{
    unsigned char *m = M + ((unsigned)k << 20);
    int i;
    s->ctx = (unsigned *)m; s->params = (float *)(m + 256); s->fx = (float *)(m + 512);
    s->magic = (unsigned *)(m + 640); s->desc = (unsigned *)(m + 768); s->arena = m + 4096;
    reload(s);
    s->ctx[1] = (unsigned)(uintptr_t)s->params; s->ctx[3] = (unsigned)(uintptr_t)s->desc;
    s->ctx[5] = (unsigned)(uintptr_t)s->fx;
    s->magic[0] = 0xABCD1234u; s->magic[2] = (unsigned)(uintptr_t)&s->magic[1];
    s->ctx[11] = (unsigned)(uintptr_t)&s->magic[2]; s->ctx[12] = (unsigned)(uintptr_t)&s->magic[0];
    s->desc[0] = (unsigned)(uintptr_t)s->arena; s->desc[1] = (unsigned)(uintptr_t)(s->arena + ARENA);
    s->desc[2] = ARENA;
    for (i = 0; i < 16; i++) s->params[i] = 0.0f;
    s->params[0] = 1.0f;
    s->params[EUGATE_NOTES_SLOT] = 0.03f; s->params[EUGATE_STEPS_SLOT] = 0.63f;   /* 64 steps */
    s->params[EUGATE_MIX_SLOT] = 1.0f;
}
static void tempo(Slot *s, int screen) { s->params[EUGATE_TEMPO_SLOT] = (float)screen / 100.0f; }
static float phase(Slot *s) { ChState *c = (ChState *)(((uintptr_t)s->arena + 3u) & ~(uintptr_t)3u); return (float)c->pos + c->pp; }

/* returns 1 when slot 4 behaved */
static int run(int model, const int *pad0, const int *bpm, int seed, int padoff, int doreload)
{
    float *bufs = (float *)(M + 2048);
    Slot s[5];
    int pad[4], off[4], k, j, hits = 0, stray = 0, bars = 0, any;
    long start[4], i, reloadat;
    float pc;
    srand((unsigned)seed);
    memset(bufs, 0, 32 * sizeof(float));
    for (k = 1; k <= 4; k++) make(&s[k], k);
    for (k = 1; k <= 3; k++) { pad[k] = pad0[k]; off[k] = rand() % 7 - 3; start[k] = rand() % 3000; twin[k] = 0; }
    tempo(&s[1], 120); tempo(&s[2], bpm[2] ? 120 : 0); tempo(&s[3], bpm[3] ? 120 : 0); tempo(&s[4], 0);
    reloadat = doreload ? 4000 + rand() % 2000 : -1;
    for (i = 0; i < 60000; i++) {
        float *d = (model == 1) ? bufs + 16 * (i & 1) : bufs;
        if (model == 2) for (j = 8; j < 16; j++) d[j] = 0.0f;
        for (k = 1; k <= 4; k++) s[k].ctx[4] = (unsigned)(uintptr_t)d;
        if (i == reloadat) for (k = 1; k <= 4; k++) reload(&s[k]);
        if (padoff && i == 20000) pad[padoff] = 0;
        for (k = 1; k <= 3; k++) if (pad[k] && i >= start[k]) {
            if (i == start[k]) tempo(&s[k], 150);
            if ((i - off[k]) % BAR == 500) { twin[k] ^= 1; tempo(&s[k], twin[k] ? 351 : 150); }
        }
        for (k = 1; k <= 4; k++) for (j = 0; j < 8; j++) s[k].fx[j] = 0.2f;
        pc = phase(&s[4]);
        for (k = 1; k <= 4; k++) Fx_DLY_EuGate(s[k].ctx);
        if (i > (padoff ? 22000 : 12000)) {
            long ph = i % BAR - 500;
            if (ph == 0) bars++;
            if (phase(&s[4]) < pc) { if (ph >= -4 && ph <= 4) hits++; else stray++; }
        }
    }
    any = pad[1] || pad[2] || pad[3];
    if (any && (hits != bars || stray != 0)) {
        printf("FAIL: buffer %s, pads %d%d%d (pad %d off mid-run), slot 2 %s, slot 3 %s, reload %d: slot 4 bar restarts %d of %d, stray %d\n",
               model == 0 ? "kept" : model == 1 ? "swapped" : "cleared", pad0[1], pad0[2], pad0[3], padoff,
               bpm[2] ? "BPM" : "FOLLW", bpm[3] ? "BPM" : "FOLLW", doreload, hits, bars, stray);
        return 0;
    }
    return 1;
}

int main(void)
{
    int model, p, t, po, sd, n = 0, fails = 0;
    M = mmap(0, 8u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    for (model = 0; model < 3; model++)
        for (p = 1; p < 8; p++)
            for (t = 0; t < 4; t++)
                for (po = 0; po <= 3; po++)
                    for (sd = 1; sd <= 2; sd++) {
                        int pad[4] = {0, p & 1, (p >> 1) & 1, (p >> 2) & 1}, bpm[4] = {0, 1, t & 1, (t >> 1) & 1};
                        if (po && !pad[po]) continue;
                        n++;
                        if (!run(model, pad, bpm, sd, po, sd == 2)) fails++;
                    }
    printf("%d runs, %d failed\n", n, fails);
    return fails ? 1 : 0;
}
