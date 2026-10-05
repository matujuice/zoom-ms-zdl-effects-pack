/* TempoPrb host test: fake host memory (state blocks + the two state[31] tables), then
 * check that BLIP beeps only on changes, CLICK follows the value, the state[24] call
 * only happens when asked and with a plausible pointer, nothing outside the tables is
 * read, and a DUMP frame survives a round trip through decode_dump.py (44.1 kHz and a
 * noisy 48 kHz "recording"). Says nothing about what the pedal really keeps there. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#define TAB_END 0xc00a2c00u
static unsigned int fake_state[6 * 53 + 8];
static unsigned int fake_tab[(TAB_END - 0xc009c1a0u) / 4u];
static unsigned int dummy, bad_reads, calls, last_a, last_b, ret24 = 25000u;
static volatile unsigned int *tp_fake(uintptr_t a)
{
    if (a >= 0x11f03000u && a < 0x11f03000u + 6u * 0xD4u) return &fake_state[(a - 0x11f03000u) / 4u];
    if (a >= 0xc009c1a0u && a < TAB_END) return &fake_tab[(a - 0xc009c1a0u) / 4u];
    bad_reads++;
    return &dummy;
}
static unsigned int fake24(unsigned int fn, unsigned int a, unsigned int b)
{
    (void)fn; calls++; last_a = a; last_b = b; return ret24;
}
#define TP_MEM(a) (*tp_fake((uintptr_t)(a)))
#define TP_CALL24(fn, a, b) fake24((fn), (a), (b))
#include "../src/probes/tempoprb/tempoprb.c"

#define ST(slot, word) fake_state[(slot) * 53u + (word)]
#define T1(row, word) fake_tab[((row) * 44u + 4u * (word)) / 4u]
#define T2(row, word) fake_tab[(0xc009fe90u - 0xc009c1a0u + (row) * 44u + 4u * (word)) / 4u]

static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *params, *fx;
static unsigned char *arena;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void setup(void)
{
    if (!m) m = mmap(0, 1u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768); arena = m + 4096;
    memset(arena, 0xC9, 1u << 19);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + (1u << 19)); desc[2] = 1u << 19;
    params[0] = 1.0f;
    memset(fake_state, 0, sizeof fake_state);
    for (unsigned i = 0; i < sizeof fake_tab / 4; i++) fake_tab[i] = i * 2654435761u;   /* junk */
    /* our effect in slot 3, state[0] = 4, state[24] = the template helper */
    for (unsigned k = 0; k < 6; k++) { ST(k, 1) = 0x10000000u + k; ST(k, 24) = 0xc00d4b40u; ST(k, 0) = k + 1; }
    ST(3, 1) = ctx[1];
    T1(4, 6) = 0; T2(3, 0) = 500;                 /* sync word, x */
    bad_reads = 0; calls = 0;
}
static void knobs(int mode, int watch, int level, int sync, int call)
{
    params[TEMPOPRB_MODE_SLOT] = mode / 100.f; params[TEMPOPRB_WATCH_SLOT] = watch / 100.f;
    params[TEMPOPRB_TIME_SLOT] = 4.9f; params[TEMPOPRB_LEVEL_SLOT] = level / 100.f;
    params[TEMPOPRB_SYNC_SLOT] = sync / 100.f; params[TEMPOPRB_CALL_SLOT] = call / 100.f;
}
#define NMAX (44100 * 16)
static float out[NMAX];
static void run(int n, float in)
{
    for (int b = 0; b < n / 8; b++) {
        for (int j = 0; j < 8; j++) { fx[j] = in; fx[j + 8] = in; }
        Fx_DLY_TempoPrb(ctx);
        for (int j = 0; j < 8; j++) out[b * 8 + j] = fx[j];
    }
}
static float energy(int a, int e, float in)
{
    double s = 0; for (int i = a; i < e; i++) s += (out[i] - in) * (out[i] - in); return (float)s;
}
static int onsets(int n, int *first, int *gap)
{
    int c = 0, last = -100000, prev = -1; *first = -1; *gap = 0;
    for (int i = 0; i < n; i++) if (out[i] != 0.0f) { if (i - last > 400) { if (*first < 0) *first = i; if (prev >= 0) *gap = i - prev; prev = i; c++; } last = i; }
    return c;
}
static void wav(const char *path, int n, int sr48, int noise)
{
    FILE *f = fopen(path, "wb"); int sr = sr48 ? 48000 : 44100; int len = sr48 ? (int)((double)n * 48000 / 44100) : n;
    unsigned int d = (unsigned)len * 2, r = 36 + d; unsigned short s;
    fwrite("RIFF", 1, 4, f); fwrite(&r, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    unsigned int v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    v = sr; fwrite(&v, 4, 1, f); v = sr * 2; fwrite(&v, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    srand(7);
    for (int i = 0; i < len; i++) {
        double t = sr48 ? i * 44100.0 / 48000.0 : i; int k = (int)t; double fr = t - k;
        double x = k + 1 < n ? out[k] * (1 - fr) + out[k + 1] * fr : out[k];
        if (noise) x = 0.6 * x + 0.02 * ((rand() / (double)RAND_MAX) * 2 - 1);
        short q = (short)(x * 30000); fwrite(&q, 2, 1, f);
    }
    fclose(f);
}
static int decode_has(const char *path, const char *const *want, int nwant)
{
    char cmd[512], line[256]; int got = 0, frames = 0;
    snprintf(cmd, sizeof cmd, "python3 src/probes/tempoprb/decode_dump.py %s", path);
    FILE *p = popen(cmd, "r");
    while (fgets(line, sizeof line, p)) {
        if (strstr(line, "(ok)")) frames++;
        for (int i = 0; i < nwant; i++) if (strstr(line, want[i])) got |= 1 << i;
    }
    pclose(p);
    printf("  %s: %d good frame(s), %d/%d expected words\n", path, frames, __builtin_popcount(got), nwant);
    return frames >= 1 && got == (1 << nwant) - 1;
}

int main(void)
{
    int first, gap, c;
    char b[8];

    /* BLIP / SYNC: silent while nothing changes, beeps when the host's Sync word does */
    setup(); knobs(0, 2, 100, 0, 0); run(44100, 0.25f);
    CHECK(energy(0, 44096, 0.25f) == 0.0f, "BLIP beeps with nothing changing");   /* run() does whole blocks */
    CHECK(out[100] == 0.25f, "input not passed through");
    T1(4, 6) = 3; run(44100, 0.0f);
    c = onsets(44100, &first, &gap);
    printf("BLIP/SYNC after a change: %d beep(s), first at %d samples\n", c, first);
    CHECK(c == 1 && first >= 0 && first < 64, "expected one beep right after the change");
    CHECK(calls == 0, "state[24] called with Call OFF");

    /* BLIP / X and TABLE */
    knobs(0, 1, 100, 0, 0); run(8000, 0.0f); T2(3, 0) = 501; run(44100, 0.0f);
    CHECK(onsets(44100, &first, &gap) == 1, "X change gave no single beep");
    knobs(0, 4, 100, 0, 0); run(44100, 0.0f);
    CHECK(energy(0, 44100, 0.0f) == 0.0f, "TABLE beeps while priming");
    T2(7, 5) ^= 1u; run(44100, 0.0f);
    CHECK(onsets(44100, &first, &gap) == 1, "TABLE missed a second-table change");
    T1(2, 3) ^= 1u; run(44100, 0.0f);
    CHECK(onsets(44100, &first, &gap) == 1, "TABLE missed a first-table change");
    printf("BLIP/X and TABLE: ok\n");

    /* CLICK / X: x = 500 -> a click every 500 ms */
    setup(); knobs(1, 1, 100, 0, 0); run(44100 * 3, 0.0f);
    c = onsets(44100 * 3, &first, &gap);
    printf("CLICK/X x=500: %d clicks, gap %d samples (want 22050)\n", c, gap);
    CHECK(gap == 22050, "click gap");
    T2(3, 0) = 5; run(44100, 0.0f);                /* 5 ms: not a delay -> hum */
    CHECK(energy(0, 44100, 0.0f) > 100.0f && onsets(44100, &first, &gap) <= 1, "no hum for an implausible value");

    /* CLICK / RCPE with Call ON: y = 25000 -> 250 ms, called as state[24](x, 100) */
    setup(); knobs(1, 0, 100, 4, 1); run(44100 * 2, 0.0f);
    c = onsets(44100 * 2, &first, &gap);
    printf("CLICK/RCPE y=25000: %d clicks, gap %d (want 11025), %u calls, args %u, %u\n", c, gap, calls, last_a, last_b);
    CHECK(gap == 11025 && calls > 0 && last_a == 500u && last_b == 100u, "recipe call");
    /* a float result is used as a float: 250.0f * 100 */
    { TpBits fb; fb.f = 25000.0f; ret24 = fb.u; } run(44100 * 2, 0.0f);
    onsets(44100 * 2, &first, &gap);
    CHECK(gap == 11025, "float y");
    ret24 = 25000u;
    /* implausible pointer: never called */
    setup(); for (unsigned k = 0; k < 6; k++) ST(k, 24) = 0x12345678u;
    knobs(1, 0, 100, 4, 1); run(44100, 0.0f);
    CHECK(calls == 0, "called a non-firmware pointer");

    /* no slot found: warble once a second */
    setup(); ST(3, 1) = 0; knobs(0, 2, 100, 0, 0); run(44100 * 2, 0.0f);
    c = onsets(44100 * 2, &first, &gap);
    printf("no slot: %d warble(s), gap %d\n", c, gap);
    CHECK(c == 2 && gap == 44100, "no-slot warble");

    /* state[0] = 0 -> row 255 of the second table, as the firmware would read it */
    setup(); for (unsigned k = 0; k < 6; k++) ST(k, 0) = 0; knobs(0, 4, 100, 0, 0); run(44100, 0.0f);
    CHECK(bad_reads == 0, "read outside the tables");

    /* DUMP round trip */
    setup(); T2(3, 0) = 0x000001F4u; T2(3, 4) = 0xDEADBEEFu; T1(4, 6) = 7;
    knobs(2, 0, 100, 4, 1); run(44100 * 15, 0.0f);
    CHECK(bad_reads == 0, "dump read outside the tables");
    wav("tests/_gen/tempoprb_dump.wav", 44100 * 15, 0, 0);
    wav("tests/_gen/tempoprb_dump48.wav", 44100 * 15, 1, 1);
    {
        const char *want[] = { "magic      = 0x54505231", "slot       = 0x00000003", "state[0]   = 0x00000004",
                               "x          = 0x000001F4", "y          = 0x000061A8", "tab2 w4    = 0xDEADBEEF",
                               "tab1 w6    = 0x00000007", "state[24]  = 0xC00D4B40" };
        CHECK(decode_has("tests/_gen/tempoprb_dump.wav", want, 8), "dump decode 44.1k");
        CHECK(decode_has("tests/_gen/tempoprb_dump48.wav", want, 8), "dump decode 48k + noise");
    }

    /* labels */
    for (unsigned v = 0; v < 3; v++) { ZDL_GetLabel_0(v, b); printf("Mode%u=%s ", v, b); }
    for (unsigned v = 0; v < 5; v++) { ZDL_GetLabel_1(v, b); printf("Watch%u=%s ", v, b); }
    printf("\n");
    for (unsigned v = 0; v < 16; v += 5) { ZDL_GetLabel_4(v, b); printf("Sync%u=%s ", v, b); }
    ZDL_GetLabel_4(15, b); printf("Sync15=%s ", b); CHECK(!strcmp(b, "S15"), "Sync label");
    ZDL_GetLabel_5(1, b); printf("Call1=%s\n", b);
    printf("sizeof(TpState) = %u bytes\n", (unsigned)sizeof(TpState));

    printf(fails ? "tempoprb: %d FAILED\n" : "tempoprb: all ok\n", fails);
    return fails ? 1 : 0;
}
