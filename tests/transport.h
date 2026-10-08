/* The pedal's tempo and MIDI transport (src/custom/common/zmt.h), shared by the
 * <effect>_transport.c tests. Runs the real pedal entry (ctx, params, arena as in
 * dubsiren_entry.c) with the host copy of the transport block (zt_host) and checks:
 *   - no block (stock firmware): 120 BPM; transport stopped: the tempo word's BPM
 *   - running on a clock at 121 BPM while the word says 120: at every clock the phase is
 *     where the clock count puts it (beats since Start x MULT cycles), and it never jumps
 *     back between clocks by more than a hair
 *   - MIDI Start puts the phase back on 0 and holds it there until the first clock
 *   - loaded mid-song (fresh state at clock 1000) it lands on the clock's place
 *   - after Stop it runs on at the tempo word
 *   - switched off it keeps following the clock (skipped where OFF_RUNS is 0) and the input
 *     passes untouched (skipped where OFF_DRY is 0)
 * The including file defines: STATE, PHASE(s) (0..1), ENTRY, MULT (cycles per beat at the
 * defaults), defaults(); optionally OFF_RUNS, OFF_DRY, TOL (phase tolerance, default 0.002). */
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
static double t = 0.0;                                       /* samples */
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define zt zt_host

#ifndef OFF_RUNS
#define OFF_RUNS 1
#endif
#ifndef OFF_DRY
#define OFF_DRY 1
#endif
#ifndef TOL
#define TOL 0.002
#endif

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
static int changed;                    /* output samples that differ from the input */
static void block(void)
{
    int j; float in[8];
    for (j = 0; j < 8; j++) { rng = rng * 1664525u + 1013904223u; fx[j] = 0.3f * (float)(int)(rng >> 9) * 1.1920929e-7f - 0.3f; fx[j + 8] = fx[j]; in[j] = fx[j]; }
    ENTRY(ctx);
    for (j = 0; j < 8; j++) if (fx[j] != in[j]) changed++;
    t += 8.0;
}
static double pdist(double a, double b) { double d = fabs(a - b); d -= floor(d); return d > 0.5 ? 1.0 - d : d; }
static double expect(unsigned int clocks) { double c = (double)((clocks - 1u) % 2304u) / 24.0 * MULT; return c - floor(c); }

/* phase moved in n blocks, in cycles (unwrapped) */
static double speed(long n)
{
    double sum = 0.0, d; float a; long i;
    for (i = 0; i < n; i++) { a = PHASE(st); block(); d = PHASE(st) - a; if (d < -0.5) d += 1.0; sum += d; }
    return sum;
}
#define CYCLES(bpm, n) ((bpm) / 60.0 * MULT * (n) * 8.0 / 44100.0)

/* clocked run from t0, one clock per per samples; returns the worst phase error at a clock */
static double clocked(double t0, double per, double until, double *back)
{
    double worst = 0.0, d; unsigned int last = zt[4]; float a;
    while (t < until) {
        zt[1] += 2u;
        zt[4] = (t >= t0) ? (unsigned int)((t - t0) / per) + 1u : 0u;
        a = PHASE(st); block();
        if (zt[4] && zt[4] != last) { d = pdist(PHASE(st), expect(zt[4])); if (d > worst) worst = d; }
        else if (zt[4] && back) { d = a - PHASE(st); if (d > 0.5) d -= 1.0; if (d > *back) *back = d; }
        last = zt[4];
    }
    return worst;
}

int main(void)
{
    double per = 44100.0 * 60.0 / (121.0 * 24.0), t0, w, back = 0.0, s;
    setup();

    /* 1: no block: 120 BPM (after 1 s of start-up: some effects clear their buffers first) */
    for (s = 0.0; s < 44100.0; s += 8.0) block();
    s = speed(2000);
    printf("no block: %.4f cycles in 2000 blocks (120 BPM: %.4f)\n", s, CYCLES(120.0, 2000));
    CHECK(fabs(s - CYCLES(120.0, 2000)) < 0.01 * CYCLES(120.0, 2000) + 0.002, "no block: not 120 BPM");

    /* 2: block present, stopped: the tempo word (90 BPM) */
    zt[0] = ZT_MAGIC; zt[1] = 0u; zt[2] = 0u; zt[3] = 0u; zt[4] = 0u; zt[5] = 9000u;
    block();
    s = speed(2000);
    CHECK(fabs(s - CYCLES(90.0, 2000)) < 0.01 * CYCLES(90.0, 2000) + 0.002, "stopped: not the tempo word's 90 BPM (%.4f)", s);

    /* 3: Start, then 20 s on a 121 BPM clock (word 120): on the clock count at every clock */
    zt[5] = 12000u; zt[2] = 1u; zt[3]++; t0 = t + 300.0;
    w = clocked(t0, per, t0 - 1.0, 0);
    CHECK(pdist(PHASE(st), 0.0) < TOL, "Start: phase %.4f, not 0 before the first clock", PHASE(st));
    w = clocked(t0, per, t + 44100.0 * 20.0, &back);
    printf("clocked 20 s: worst phase error at a clock %.5f, largest step back %.5f\n", w, back);
    CHECK(w < TOL, "phase left the clock (%.5f)", w);
    CHECK(back < TOL, "phase jumped back between clocks (%.5f)", back);

    /* 4: loaded mid-song: fresh state at clock 1000 */
    memset(arena, 0xC9, 65536);
    zt[1] += 2u; zt[4] = 1000u; block();
    CHECK(pdist(PHASE(st), expect(1000u)) < TOL, "mid-song load: phase %.4f, want %.4f", PHASE(st), expect(1000u));

    /* 5: Stop: runs on at the tempo word (100 BPM) */
    zt[2] = 0u; zt[5] = 10000u;
    for (s = 0.0; s < 44100.0; s += 8.0) block();
    s = speed(2000);
    CHECK(fabs(s - CYCLES(100.0, 2000)) < 0.01 * CYCLES(100.0, 2000) + 0.002, "after Stop: not 100 BPM (%.4f)", s);

#if OFF_RUNS
    /* 6: switched off: the input passes untouched, the phase still follows the clock */
    params[0] = 0.0f; changed = 0;
    zt[2] = 1u; zt[3]++; t0 = t + 100.0;
    w = clocked(t0, per, t + 44100.0 * 5.0, 0);
    printf("off, clocked 5 s: worst phase error %.5f, changed samples %d\n", w, changed);
    CHECK(w < TOL, "switched off: phase left the clock (%.5f)", w);
#if OFF_DRY
    CHECK(changed == 0, "switched off: input changed");
#endif
#endif

    printf("%s\n", fails ? "transport: FAILED" : "transport: ok");
    return fails ? 1 : 0;
}
