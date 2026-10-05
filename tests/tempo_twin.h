/* Tempo-knob sync reset, shared by the <effect>_tempo.c tests. Runs the real pedal entry
 * (ctx, params, arena as in dubsiren_entry.c) and checks:
 *   - the Tempo label shows the BPM on both copies (0..39 -> 40, 241 -> 40, 441 -> 240)
 *   - the twin copy runs at the same speed as its BPM (also read from raw 4.41)
 *   - flipping a BPM to its twin (1.20 -> 3.21) restarts the phase, and only once
 *   - a plain tempo change restarts it only where PLAIN_RESETS is 1 (DualShft, Choral)
 * The including file defines: STATE, PHASE(s), ENTRY, TEMPO_SLOT, PLAIN_RESETS,
 * defaults(), label(). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#define ARENA_BYTES (1u << 20)
static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *fx;
static unsigned char *arena;
static STATE *st;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

static void setup(void)
{
    m = mmap(0, 2u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768); arena = m + 4096;
    memset(arena, 0xC9, ARENA_BYTES);                        /* garbage arena */
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + ARENA_BYTES);
    desc[2] = ARENA_BYTES;
    params[0] = 1.0f;                                         /* effect on */
    defaults();
    st = (STATE *)(((uintptr_t)arena + 3u) & ~(uintptr_t)3u);
}

static unsigned int rng = 12345u;
static void run(long blocks)
{
    long b; int j;
    for (b = 0; b < blocks; b++) {
        for (j = 0; j < 8; j++) { rng = rng * 1664525u + 1013904223u; fx[j] = 0.3f * (float)(int)(rng >> 9) * 1.1920929e-7f - 0.3f; fx[j + 8] = fx[j]; }
        ENTRY(ctx);
    }
}

static float step(void) { float a = PHASE(st); run(1); return PHASE(st) - a; }

int main(void)
{
    char b[8];
    unsigned v[8] = {0, 39, 40, 120, 240, 241, 321, 441};
    const char *want[8] = {"40", "40", "40", "120", "240", "40", "120", "240"};
    float d120, d321, d441, d240, before, after, p1, p2;
    int i;

    for (i = 0; i < 8; i++) {
        label(v[i], b);
        printf("%u=%s ", v[i], b);
        CHECK(strcmp(b, want[i]) == 0, "label %u shows %s, want %s", v[i], b, want[i]);
    }
    printf("\n");

    setup();
    params[TEMPO_SLOT] = 1.20f; run(20000);                 /* past any clearing */
    d120 = step();
    params[TEMPO_SLOT] = 2.40f; run(50); d240 = step();
    params[TEMPO_SLOT] = 4.41f; run(50); d441 = step();
    params[TEMPO_SLOT] = 1.20f; run(20000);
    before = PHASE(st);
    params[TEMPO_SLOT] = 3.21f; run(1);
    after = PHASE(st);
    p1 = PHASE(st); run(1); p2 = PHASE(st);
    d321 = p2 - p1;
    printf("phase per block: 120 %.6g, twin 321 %.6g; 240 %.6g, twin 441 %.6g\n", d120, d321, d240, d441);
    printf("flip 120 -> 321: phase %.4f -> %.4f\n", before, after);
    CHECK(fabsf(d120 - d321) < 1e-6f && d120 > 0.0f, "twin 321 runs at a different speed");
    CHECK(fabsf(d240 - d441) < 1e-6f && d240 > 0.0f, "twin 441 (raw 4.41) runs at a different speed");
    CHECK(before > 2.0f * d120, "test setup: phase too small to see a reset (%g)", before);
    CHECK(after <= 1.5f * d120, "flip did not restart the phase (%g)", after);
    CHECK(p2 > p1, "phase restarted again on the next block");

    params[TEMPO_SLOT] = 3.21f; run(3000);                  /* back on the base copy */
    params[TEMPO_SLOT] = 1.20f; before = PHASE(st); run(1); after = PHASE(st);
    printf("flip 321 -> 120: phase %.4f -> %.4f\n", before, after);
    CHECK(after <= 1.5f * d120 && before > 2.0f * d120, "flip back did not restart the phase");

    run(3000);
    params[TEMPO_SLOT] = 1.21f; before = PHASE(st); run(1); after = PHASE(st);
    printf("plain change 120 -> 121: phase %.4f -> %.4f (restart expected: %d)\n", before, after, PLAIN_RESETS);
    if (PLAIN_RESETS) CHECK(after <= 1.5f * d120, "plain tempo change did not restart");
    else              CHECK(after > before, "plain tempo change restarted the phase");

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
