/* Tempo-knob sync reset, shared by the <effect>_tempo.c tests. Runs the real pedal entry
 * (ctx, params, arena as in dubsiren_entry.c) and checks:
 *   - the Tempo label shows the BPM on both copies (241 -> 40, 441 -> 240), FOLLW on 0..39
 *   - FOLLOW (0..39) with no bar tag upstream runs at 120 BPM
 *     (both FOLLOW checks only where HAS_FOLLOW is 1; effects without the bar tag show 40)
 *   - the twin copy runs at the same speed as its BPM (also read from raw 4.41)
 *   - flipping a BPM to its twin (1.20 -> 3.21) restarts the phase, and only once
 *   - a plain tempo change restarts it only where PLAIN_RESETS is 1 (none since 2026-10-07: all restart only on a flip)
 *   - switched off, the input passes untouched while the phase keeps running at the same
 *     speed and a flip still restarts it (skipped where OFF_RUNS is 0: Scrub; the input
 *     check is skipped where OFF_DRY is 0: DubSiren, whose echo rings out)
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

#ifndef OFF_RUNS
#define OFF_RUNS 1
#endif
#ifndef OFF_DRY
#define OFF_DRY 1
#endif

static unsigned int rng = 12345u;
static int changed;                    /* output samples that differ from the input */
static void run(long blocks)
{
    long b; int j;
    float in[8];
    for (b = 0; b < blocks; b++) {
        for (j = 0; j < 8; j++) { rng = rng * 1664525u + 1013904223u; fx[j] = 0.3f * (float)(int)(rng >> 9) * 1.1920929e-7f - 0.3f; fx[j + 8] = fx[j]; in[j] = fx[j]; }
        ENTRY(ctx);
        for (j = 0; j < 8; j++) if (fx[j] != in[j]) changed++;
    }
}

static float step(void) { float a = PHASE(st); run(1); return PHASE(st) - a; }

#ifndef HAS_FOLLOW
#define HAS_FOLLOW 0
#endif

int main(void)
{
    char b[8];
    unsigned v[8] = {0, 39, 40, 120, 240, 241, 321, 441};
#if HAS_FOLLOW
    const char *want[8] = {"FOLLW", "FOLLW", "40", "120", "240", "40", "120", "240"};
#else
    const char *want[8] = {"40", "40", "40", "120", "240", "40", "120", "240"};
#endif
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
#if HAS_FOLLOW
    params[TEMPO_SLOT] = 0.0f; run(50);
    {
        float df = step();
        printf("FOLLOW, no tag: phase per block %.6g (120: %.6g)\n", df, d120);
        CHECK(fabsf(df - d120) < 1e-6f, "FOLLOW without a tag does not run at 120 BPM");
    }
#endif
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

    if (OFF_RUNS) {                                          /* switched off */
        float doff;
        params[TEMPO_SLOT] = 1.20f; run(3000);
        params[0] = 0.0f; changed = 0;
        run(3000);
        doff = step(); if (doff < 0.0f) doff = step();
        printf("switched off: phase per block %.6g (on: %.6g), changed samples %d\n", doff, d120, changed);
        if (OFF_DRY) CHECK(changed == 0, "switched off but the input was changed");
        CHECK(fabsf(doff - d120) < 1e-5f, "switched off, the phase does not keep running");
        run(3000);
        params[TEMPO_SLOT] = 3.21f; before = PHASE(st); run(1); after = PHASE(st);
        printf("flip while off: phase %.4f -> %.4f\n", before, after);
        CHECK(after <= 1.5f * d120 && before > 2.0f * d120, "flip while off did not restart the phase");
        params[0] = 1.0f;
    }

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
