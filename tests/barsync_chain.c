/* Bar tag across slots: two real EuGate entries share one Dry buffer, like slot 1 and slot 5.
 * Slot 1 gets Mozaic's twin flips; slot 5 never sees a knob edit. On FOLLOW it must restart
 * on the same blocks and take the BPM; on a BPM it must ignore the tag and run on its own; a
 * sender that disappears is dropped. Also: a slot without Mozaic flips writes nothing, and an
 * effect that does not send leaves the tag untouched (pass-through). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mman.h>
#include "../src/custom/eugate/eugate.c"

#define ARENA_BYTES (1u << 16)
typedef struct { unsigned int *ctx, *magic, *desc; float *params, *fx; unsigned char *arena; ChState *st; } Slot;
static float *dry;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(sl, n) (sl)->params[EUGATE_##n##_SLOT] = EUGATE_##n##_UI_DEFAULT / 100.0f

static void make(Slot *sl, unsigned char *m)
{
    sl->ctx = (unsigned int *)m; sl->params = (float *)(m + 256); sl->fx = (float *)(m + 512);
    sl->magic = (unsigned int *)(m + 640); sl->desc = (unsigned int *)(m + 768); sl->arena = m + 4096;
    memset(sl->arena, 0xC9, ARENA_BYTES);
    sl->ctx[1] = (unsigned)(uintptr_t)sl->params; sl->ctx[3] = (unsigned)(uintptr_t)sl->desc;
    sl->ctx[4] = (unsigned)(uintptr_t)dry; sl->ctx[5] = (unsigned)(uintptr_t)sl->fx;
    sl->magic[0] = 0xABCD1234u; sl->magic[2] = (unsigned)(uintptr_t)&sl->magic[1];
    sl->ctx[11] = (unsigned)(uintptr_t)&sl->magic[2]; sl->ctx[12] = (unsigned)(uintptr_t)&sl->magic[0];
    sl->desc[0] = (unsigned)(uintptr_t)sl->arena; sl->desc[1] = (unsigned)(uintptr_t)(sl->arena + ARENA_BYTES);
    sl->desc[2] = ARENA_BYTES;
    sl->params[0] = 1.0f;
    D(sl, NOTES); D(sl, STEPS); D(sl, SHIFT); D(sl, SWING); D(sl, RESET); D(sl, GAP); D(sl, SOFT); D(sl, TEMPO); D(sl, MIX);
    sl->params[EUGATE_STEPS_SLOT] = 0.63f;                   /* 64 steps: pos only restarts on a reset */
    sl->st = (ChState *)(((uintptr_t)sl->arena + 3u) & ~(uintptr_t)3u);
}

static float phase(Slot *sl) { return (float)sl->st->pos + sl->st->pp; }
static void tempo(Slot *sl, int screen) { sl->params[EUGATE_TEMPO_SLOT] = (float)screen / 100.0f; }
/* Mozaic's flip: the same BPM on the other copy */
static void flip(Slot *sl, int bpm) { tempo(sl, sl->st->sync.own_twin ? bpm : bpm + 201); }

/* one block through slot 1 then slot 5 (the pedal runs the slots in order on the same block) */
static void block(Slot *a, Slot *b, int run_a)
{
    int j;
    for (j = 0; j < 8; j++) { a->fx[j] = 0.2f; b->fx[j] = 0.2f; }
    if (run_a) Fx_DLY_EuGate(a->ctx);
    Fx_DLY_EuGate(b->ctx);
}

int main(void)
{
    unsigned char *m = mmap(0, 4u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    Slot A, B;
    long i;
    int bars = 0, hits = 0, early = 0, j;
    float pa, pb, inc120, inc150, p0, p1;
    DtTag t;

    dry = (float *)(m + 2048);
    for (j = 0; j < 16; j++) dry[j] = 0.0f;
    make(&A, m + (1u << 20)); make(&B, m + (2u << 20));

    /* 1. no flips yet: slot 1 is not LIVE, so it writes nothing */
    tempo(&A, 120); tempo(&B, 120);
    for (i = 0; i < 3000; i++) block(&A, &B, 1);
    for (j = 0; j < 16; j++) CHECK(dry[j] == 0.0f, "Dry buffer written without any flip");

    /* 2. Mozaic flips slot 1 every 1000 blocks; slot 5 on FOLLOW restarts on the same block */
    tempo(&B, 0);
    for (i = 0; i < 8000; i++) {
        if (i % 1000 == 500) { flip(&A, 120); bars++; }
        pb = phase(&B);
        block(&A, &B, 1);
        if (i % 1000 == 500) { if (phase(&B) < pb && phase(&B) < 0.01f) hits++; }
        else if (phase(&B) < pb) early++;
    }
    printf("slot 5 restarts on slot 1 flips: %d of %d, other restarts %d\n", hits, bars, early);
    CHECK(hits == bars && early == 0, "slot 5 not restarting on exactly the bars");
    CHECK(fabsf(phase(&A) - phase(&B)) < 1e-4f, "slot 1 and slot 5 out of step (%g vs %g)", phase(&A), phase(&B));
    CHECK(dt_read(dry + 8, &t) && t.live && t.bpm16 == 1920u, "no LIVE tag at 120 BPM");

    /* 3. FOLLOW: slot 5 takes slot 1's BPM, and changes with it */
    p0 = phase(&B); block(&A, &B, 1); inc120 = phase(&B) - p0;
    flip(&A, 150); bars++;                                    /* host tempo 150, on a flip */
    block(&A, &B, 1);
    p0 = phase(&B); block(&A, &B, 1); p1 = phase(&B); inc150 = p1 - p0;
    pa = phase(&A) - p1;
    printf("FOLLOW: slot 5 per block %.6g at 120, %.6g at 150 (ratio %.4f)\n", inc120, inc150, inc150 / inc120);
    CHECK(fabsf(inc150 / inc120 - 1.25f) < 1e-3f, "FOLLOW did not take 150 BPM from slot 1");
    CHECK(fabsf(pa) < 1e-4f, "FOLLOW slot out of step with slot 1");

    /* 4. On a BPM, slot 5 keeps its own tempo and ignores the bars */
    tempo(&B, 100);
    for (i = 0; i < 20; i++) block(&A, &B, 1);
    p0 = phase(&B); block(&A, &B, 1);
    CHECK(fabsf((phase(&B) - p0) / inc120 - 100.0f / 120.0f) < 1e-3f, "own BPM not used when not on FOLLOW");
    for (i = 0; i < 500; i++) block(&A, &B, 1);
    pb = phase(&B);
    flip(&A, 150); bars++;
    block(&A, &B, 1);
    CHECK(phase(&B) > pb, "slot 5 on its own BPM restarted on an upstream bar");

    /* 5. slot 1 removed: its last tag stays in the buffer but stops counting -> stale after 1 s */
    tempo(&B, 0);
    for (i = 0; i < 6000; i++) block(&A, &B, 0);
    p0 = phase(&B); block(&A, &B, 0);
    printf("sender gone: slot 5 FOLLOW per block %.6g (120 = %.6g), ok %u\n", phase(&B) - p0, inc120, B.st->sync.rx.ok);
    CHECK(!B.st->sync.rx.ok && fabsf((phase(&B) - p0) - inc120) < 1e-6f, "stale tag still followed");

    /* 6. slot 5 a LIVE sender itself (Mozaic flips it too, slightly later): upstream bars ignored */
    tempo(&B, 150);
    for (i = 0; i < 100; i++) block(&A, &B, 1);
    early = 0; hits = 0;
    for (i = 0; i < 4000; i++) {
        if (i % 1000 == 100) { flip(&A, 150); bars++; }
        if (i % 1000 == 103) flip(&B, 150);
        pb = phase(&B);
        block(&A, &B, 1);
        if (phase(&B) < pb) { if (i % 1000 == 103) hits++; else early++; }
    }
    printf("slot 5 flipped by Mozaic too: restarts on its own flips %d, on others %d\n", hits, early);
    CHECK(hits == 4 && early == 0, "slot 5 on a BPM followed upstream bars too (double restarts)");

    /* 7. pass-through: a slot that is not sending leaves a tag as it is */
    {
        float keep[8];
        dt_write(dry + 8, 0x123u, 7u, 1920u, 55u, 1u);
        memcpy(keep, dry + 8, sizeof keep);
        tempo(&B, 120);
        for (i = 0; i < 10; i++) { memcpy(dry + 8, keep, sizeof keep); B.st->sync.tx.flipped = 0u; block(&A, &B, 0); }
        CHECK(memcmp(keep, dry + 8, sizeof keep) == 0, "a non-sending slot changed the tag");
    }

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
