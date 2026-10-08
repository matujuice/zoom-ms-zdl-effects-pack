/* EuGate on the MOD firmware transport block: steps on the clock count, restarts on Start,
 * lands right when loaded mid-song, runs free after Stop at the tempo word, runs at 120 BPM
 * when the block is missing or always mid-write. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "../src/custom/eugate/eugate.c"
#define zmt zt_host

static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *fx, *params;
static ChState *st; static int fails = 0; static double t = 0.0;   /* samples */
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[EUGATE_##n##_SLOT] = EUGATE_##n##_UI_DEFAULT / 100.0f

static void setup(void)
{
    m = mmap(0, 2u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)(m + 4096); desc[1] = desc[0] + 65536u; desc[2] = 65536u;
    params[0] = 1.0f;
    D(NOTES); D(STEPS); D(SHIFT); D(SWING); D(RESET); D(GAP); D(SOFT); D(MIX);
    params[EUGATE_STEPS_SLOT] = 0.63f;                       /* 64 steps: long counts */
    st = (ChState *)(uintptr_t)desc[0];
}
static unsigned int idx(void)                                 /* 16th being played */
{
    unsigned int j = st->pos + (st->pp >= 1.0f ? 1u : 0u);
    return j >= 64u ? j - 64u : j;
}
static void block(void) { int i; for (i = 0; i < 8; i++) fx[i] = 0.3f; Fx_DLY_EuGate(ctx); t += 8.0; }
static void block_on_clock(double t0, double per)             /* clocks from t0, one per per samples */
{
    zmt[1] += 2u;
    zmt[4] = (t >= t0) ? (unsigned int)((t - t0) / per) + 1u : 0u;
    block();
}

int main(void)
{
    double per = 44100.0 * 60.0 / (121.0 * 24.0), t0;        /* clock at 121 BPM, word 5 says 120 */
    unsigned int bad = 0u, e;
    float lastp = 0.0f, back = 0.0f, a; unsigned int lastpos = 0u;
    setup();
    zmt[0] = ZT_MAGIC; zmt[1] = 0u; zmt[2] = 1u; zmt[3] = 1u; zmt[4] = 0u; zmt[5] = 12000u;

    /* 1: 30 s on the clock: the 16th always equals (clocks - 1) / 6 mod 64, never moves back */
    t0 = 500.0;
    while (t < 44100.0 * 30.0) {
        block_on_clock(t0, per);
        e = zmt[4] ? ((zmt[4] - 1u) / 6u) % 64u : 0u;
        if (idx() != e) bad++;
        if (st->pos == lastpos && st->pp < lastp) { a = lastp - st->pp; if (a > back) back = a; }
        lastp = st->pp; lastpos = st->pos;
    }
    printf("clocked 30 s: %u blocks off the clock's 16th, largest step back %g\n", bad, back);
    CHECK(bad == 0u, "pattern left the clock");
    CHECK(back == 0.0f, "phase moved backwards");

    /* 2: Start restarts at step 1, waiting there until the first clock */
    zmt[3]++; t0 = t + 300.0;
    block_on_clock(t0, per);
    CHECK(zmt[4] == 0u && idx() == 0u && st->pp == 0.0f, "Start: not on step 1 before the first clock");
    while (t < t0 + 3.0 * per) block_on_clock(t0, per);
    CHECK(idx() == 0u, "Start: not on step 1 after 3 clocks (got %u)", idx());
    while (t < t0 + 13.0 * per) block_on_clock(t0, per);
    CHECK(idx() == 2u, "Start: not on step 3 after 13 clocks (got %u)", idx());

    /* 3: Stop: runs free at word 5's tempo (90 BPM = 6 16ths per second) */
    zmt[2] = 0u; zmt[5] = 9000u;
    { unsigned int i0 = idx(); long n; for (n = 0; n < 44100 / 8; n++) block();
      e = idx() >= i0 ? idx() - i0 : idx() + 64u - i0;
      printf("after Stop: %u 16ths in 1 s\n", e);
      CHECK(e == 6u, "Stop: expected 6 16ths in 1 s, got %u", e); }

    /* 4: loaded mid-song: fresh state, clock count 1000 -> 16th 999 / 6 = 166 -> 166 mod 64 = 38 */
    memset(st, 0, sizeof(ChState));
    zmt[2] = 1u; zmt[4] = 1000u; zmt[1] += 2u; block();
    CHECK(idx() == 38u, "mid-song load: expected 16th 38, got %u", idx());

    /* 5: block always mid-write (odd sequence) -> 120 BPM = 8 16ths per second */
    zmt[1] = 1u; zmt[5] = 6000u;
    { unsigned int i0; long n; block(); i0 = idx(); for (n = 0; n < 44100 / 8; n++) block();
      e = idx() >= i0 ? idx() - i0 : idx() + 64u - i0;
      CHECK(e == 8u, "torn block: expected 8 16ths in 1 s, got %u", e); }

    /* 6: no block (stock firmware) -> 120 BPM */
    zmt[0] = 0u; zmt[1] = 0u;
    { unsigned int i0; long n; block(); i0 = idx(); for (n = 0; n < 44100 / 8; n++) block();
      e = idx() >= i0 ? idx() - i0 : idx() + 64u - i0;
      CHECK(e == 8u, "no block: expected 8 16ths in 1 s, got %u", e); }

    printf("%s\n", fails ? "eugate_clock: FAILED" : "eugate_clock: ok");
    return fails ? 1 : 0;
}
