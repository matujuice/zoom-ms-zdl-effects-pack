/* Shared set-up for the Sweep tests: the real pedal entry with an arena full of garbage. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../src/custom/sweep/sweep.c"

#define ARENA_BYTES (1u << 20)
static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *fx, *dry, *params;
static unsigned char *arena;
static SwState *st;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[SWEEP_##n##_SLOT] = SWEEP_##n##_UI_DEFAULT / 100.0f
#define SET(n, v) params[SWEEP_##n##_SLOT] = (float)(v) / 100.0f

static void defaults(void)
{
    D(TYPE); D(RATE); D(DEPTH); D(CNTR); D(RESO); D(SHAPE); D(TONE); D(TEMPO); D(MIX);
    params[SWEEP_TEMPO_SLOT] = SWEEP_TEMPO_UI_DEFAULT / 100.0f;
}

static void setup(void)
{
    m = mmap(0, 2u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768); dry = (float *)(m + 1024);
    arena = m + 4096;
    memset(arena, 0xC9, ARENA_BYTES);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc;
    ctx[4] = (unsigned)(uintptr_t)dry; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + ARENA_BYTES);
    desc[2] = ARENA_BYTES;
    memset(dry, 0, 64);
    params[0] = 1.0f;
    defaults();
    st = (SwState *)(((uintptr_t)arena + 3u) & ~(uintptr_t)3u);
}
