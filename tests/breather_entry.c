/* Breather: the real pedal entry on a garbage arena. Checks: state set up, no NaN; switching
 * on restarts on beat 1 while the MIDI transport is stopped, but not while it runs; the reverb
 * starts clean after switching on again (no old tail). */
#include "../src/custom/breather/breather.c"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mman.h>

#define ARENA_BYTES (1u << 20)
static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *fx, *params;
static unsigned char *arena; static PuState *st; static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[BREATHER_##n##_SLOT] = BREATHER_##n##_UI_DEFAULT / 100.0f

static float level = 0.5f, peak;
static int nan_seen;
static void run(long blocks)
{
    long b; int j;
    for (b = 0; b < blocks; b++) {
        for (j = 0; j < 8; j++) { fx[j] = level; fx[j + 8] = level; }
        Fx_DLY_Breather(ctx);
        for (j = 0; j < 16; j++) { if (!(fx[j] == fx[j])) nan_seen = 1; if (fabsf(fx[j]) > peak) peak = fabsf(fx[j]); }
    }
}

int main(void)
{
    float before, after;
    m = mmap(0, 2u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768); arena = m + 4096;
    memset(arena, 0xC9, ARENA_BYTES);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + ARENA_BYTES); desc[2] = ARENA_BYTES;
    params[0] = 1.0f;
    D(TARGT); D(SHAPE); D(DEPTH); D(DIV); D(SHIFT); D(CURVE); D(VERB); D(SIZE);
    st = (PuState *)(((uintptr_t)arena + 3u) & ~(uintptr_t)3u);
    printf("state %u bytes (arena >= 705,536)\n", (unsigned)sizeof(PuState));
    CHECK(sizeof(PuState) < 705536u, "state too big");

    run(20000);
    CHECK(st->magic == PU_MAGIC, "state not set up");
    CHECK(!nan_seen && peak < 4.0f, "NaN or peak %.3f with defaults", peak);
    printf("defaults: peak %.3f\n", peak);

    /* no host: off, then on restarts on beat 1 */
    run(3000); params[0] = 0.0f; run(3000);
    before = st->bp; params[0] = 1.0f; run(1); after = st->bp;
    printf("on, no host: beat %.4f -> %.4f\n", before, after);
    CHECK(after < 0.01f && before > 0.1f, "switching on without a host did not restart on beat 1");

    /* transport running (clock 50 = beat 2 + 1/24): off/on keeps the clock's place */
    zt_host[0] = ZT_MAGIC; zt_host[2] = 1u; zt_host[3] = 1u; zt_host[4] = 50u; zt_host[5] = 16000u;
    params[0] = 0.0f; run(3000);
    before = st->bp; params[0] = 1.0f; run(1); after = st->bp;
    printf("on, transport running: beat %.4f -> %.4f\n", before, after);
    CHECK(after > 2.0f && before > 2.0f, "switching on restarted although the transport runs");

    /* Stop: switching on restarts on beat 1 again */
    zt_host[2] = 0u; run(100); params[0] = 0.0f; run(100);
    before = st->bp; params[0] = 1.0f; run(1); after = st->bp;
    printf("on, transport stopped: beat %.4f -> %.4f\n", before, after);
    CHECK(after < 0.01f && before > 0.1f, "transport stopped but switching on did not restart");

    /* reverb clean after switching on: VERB only, Depth 0, Verb 100, Size 100: after the
     * input stops the tail rings; switch off and on: the tail is gone */
    params[BREATHER_TARGT_SLOT] = 0.01f; params[BREATHER_DEPTH_SLOT] = 0.0f;
    params[BREATHER_VERB_SLOT] = 1.0f; params[BREATHER_SIZE_SLOT] = 1.0f;
    level = 0.5f; run(5000); level = 0.0f; peak = 0; run(10);
    printf("tail right after the input stops: %.4f\n", peak);
    CHECK(peak > 0.01f, "no reverb tail to test with");
    params[0] = 0.0f; run(20); params[0] = 1.0f; peak = 0; run(200);
    printf("tail after switching off and on: %.6f\n", peak);
    CHECK(peak < 1e-6f, "old reverb tail came back after switching on");
    CHECK(!nan_seen, "NaN seen");

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
