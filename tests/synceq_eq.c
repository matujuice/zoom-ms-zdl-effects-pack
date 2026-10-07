/* SyncEQ through the real pedal entry: knob text, flat at defaults, EQ gains at test
 * frequencies, drive and level, switched off, and the bar tag it sends into Dry right. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mman.h>
#include "../src/custom/synceq/synceq.c"

#define ARENA_BYTES (1u << 16)
static unsigned char *m; static unsigned int *ctx, *magic, *desc; static float *fx, *dry, *params;
static unsigned char *arena;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)
#define D(n) params[SYNCEQ_##n##_SLOT] = SYNCEQ_##n##_UI_DEFAULT / 100.0f

static void defaults(void) { D(LOCUT); D(LOW); D(MID); D(MIDF); D(HIGH); D(HICUT); D(DRIVE); D(TEMPO); D(LEVEL); }

static void setup(void)
{
    m = mmap(0, 1u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)m; params = (float *)(m + 256); fx = (float *)(m + 512); dry = (float *)(m + 1024);
    magic = (unsigned int *)(m + 640); desc = (unsigned int *)(m + 768); arena = m + 4096;
    memset(arena, 0xC9, ARENA_BYTES);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc;
    ctx[4] = (unsigned)(uintptr_t)dry; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + ARENA_BYTES);
    desc[2] = ARENA_BYTES;
    params[0] = 1.0f;
    defaults();
}

static void knob(int slot, int screen) { params[slot] = (float)screen / 100.0f; }

/* gain in dB of a sine at hz (after 0.5 s to settle) */
static float gain_db(float hz, float amp)
{
    double ph = 0.0, ein = 0.0, eout = 0.0;
    long n, b; int j;
    for (b = 0; b < 8000; b++) {
        for (j = 0; j < 8; j++) {
            float x = amp * (float)sin(ph);
            ph += 2.0 * 3.14159265358979 * hz / 44100.0;
            fx[j] = x; fx[j + 8] = x;
            if (b >= 2756) ein += (double)x * x;
        }
        Fx_DLY_SyncEQ(ctx);
        if (b >= 2756) for (j = 0; j < 8; j++) eout += (double)fx[j] * fx[j];
        for (j = 0; j < 8; j++) if (!(fx[j] == fx[j])) eout = -1e30;
    }
    (void)n;
    if (eout <= 0.0) return -999.0f;
    return (float)(10.0 * log10(eout / ein));
}

static void expect(const char *what, float hz, float lo, float hi)
{
    float g = gain_db(hz, 0.1f);
    printf("%-28s %6.0f Hz: %+6.2f dB\n", what, hz, g);
    CHECK(g >= lo && g <= hi, "%s at %.0f Hz: %.2f dB, want %.1f..%.1f", what, hz, g, lo, hi);
}

static void label_is(int (*fn)(unsigned int, char *), unsigned v, const char *want)
{
    char b[8];
    fn(v, b);
    CHECK(strcmp(b, want) == 0, "label %u shows %s, want %s", v, b, want);
}

int main(void)
{
    long b, changed = 0; int j;
    DtTag t;
    unsigned int c0;

    label_is(ZDL_GetLabel_0, 0, "OFF");  label_is(ZDL_GetLabel_0, 1, "20");  label_is(ZDL_GetLabel_0, 50, "500");
    label_is(ZDL_GetLabel_1, 0, "-12");  label_is(ZDL_GetLabel_1, 12, "0");  label_is(ZDL_GetLabel_1, 24, "+12");
    label_is(ZDL_GetLabel_3, 0, "200");  label_is(ZDL_GetLabel_3, 25, "1.0k"); label_is(ZDL_GetLabel_3, 50, "5.0k");
    label_is(ZDL_GetLabel_5, 0, "1.0k"); label_is(ZDL_GetLabel_5, 49, "20k"); label_is(ZDL_GetLabel_5, 50, "OFF");
    label_is(ZDL_GetLabel_7, 0, "40");   label_is(ZDL_GetLabel_7, 120, "120"); label_is(ZDL_GetLabel_7, 321, "120");
    label_is(ZDL_GetLabel_7, 441, "240"); label_is(ZDL_GetLabel_8, 24, "+12");
    {
        char bb[8]; unsigned v;
        printf("LoCut:"); for (v = 1; v <= 50; v += 7) { ZDL_GetLabel_0(v, bb); printf(" %s", bb); }
        printf("\nMidF: "); for (v = 0; v <= 50; v += 5) { ZDL_GetLabel_3(v, bb); printf(" %s", bb); }
        printf("\nHiCut:"); for (v = 0; v <= 50; v += 7) { ZDL_GetLabel_5(v, bb); printf(" %s", bb); }
        printf("\n");
        for (v = 0; v <= 50; v++) { ZDL_GetLabel_0(v, bb); CHECK(strlen(bb) <= 5, "LoCut label too long"); ZDL_GetLabel_3(v, bb); CHECK(strlen(bb) <= 5, "MidF label too long"); ZDL_GetLabel_5(v, bb); CHECK(strlen(bb) <= 5, "HiCut label too long"); }
    }

    setup();
    /* defaults: the sound passes unchanged */
    for (b = 0; b < 5000; b++) {
        float in[8];
        for (j = 0; j < 8; j++) { in[j] = 0.5f * (float)sin(0.01 * (double)(b * 8 + j)); fx[j] = in[j]; fx[j + 8] = 0.0f; }
        Fx_DLY_SyncEQ(ctx);
        for (j = 0; j < 8; j++) if (fx[j] != in[j] || fx[j + 8] != in[j]) changed++;
    }
    printf("defaults: %ld samples changed\n", changed);
    CHECK(changed == 0, "defaults change the sound");

    knob(SYNCEQ_LOW_SLOT, 24);  expect("Low +12", 30, 10.5f, 12.5f); expect("Low +12", 5000, -0.5f, 0.5f);
    knob(SYNCEQ_LOW_SLOT, 0);   expect("Low -12", 30, -12.5f, -10.5f);
    knob(SYNCEQ_LOW_SLOT, 18);  expect("Low +6", 30, 5.0f, 6.5f);
    knob(SYNCEQ_LOW_SLOT, 12);
    knob(SYNCEQ_MID_SLOT, 24);  expect("Mid +12 at 1k", 1000, 11.5f, 12.5f); expect("Mid +12 at 1k", 100, -0.1f, 1.5f);
    knob(SYNCEQ_MIDF_SLOT, 0);  expect("Mid +12 at 200", 200, 11.5f, 12.5f);
    knob(SYNCEQ_MIDF_SLOT, 50); expect("Mid +12 at 5k", 5000, 11.5f, 12.5f);
    knob(SYNCEQ_MID_SLOT, 0);   expect("Mid -12 at 5k", 5000, -12.5f, -11.5f);
    knob(SYNCEQ_MID_SLOT, 12);  knob(SYNCEQ_MIDF_SLOT, 25);
    knob(SYNCEQ_HIGH_SLOT, 24); expect("High +12", 18000, 10.5f, 12.5f); expect("High +12", 200, -0.3f, 0.3f);
    knob(SYNCEQ_HIGH_SLOT, 12);
    knob(SYNCEQ_LOCUT_SLOT, 50); expect("LoCut 500", 500, -3.8f, -2.2f); expect("LoCut 500", 50, -60.0f, -35.0f); expect("LoCut 500", 5000, -0.3f, 0.3f);
    knob(SYNCEQ_LOCUT_SLOT, 1);  expect("LoCut 20", 20, -3.8f, -2.2f); expect("LoCut 20", 1000, -0.1f, 0.1f);
    knob(SYNCEQ_LOCUT_SLOT, 0);
    knob(SYNCEQ_HICUT_SLOT, 0);  expect("HiCut 1k", 1000, -3.8f, -2.2f); expect("HiCut 1k", 10000, -60.0f, -35.0f);
    knob(SYNCEQ_HICUT_SLOT, 49); expect("HiCut 20k", 1000, -0.1f, 0.1f); expect("HiCut 20k", 20000, -4.5f, -1.5f);
    knob(SYNCEQ_HICUT_SLOT, 50);
    knob(SYNCEQ_LEVEL_SLOT, 24); expect("Level +12", 1000, 11.9f, 12.1f);
    knob(SYNCEQ_LEVEL_SLOT, 0);  expect("Level -12", 1000, -12.1f, -11.9f);
    knob(SYNCEQ_LEVEL_SLOT, 12);

    /* drive: bounded, no NaN, small signals a bit louder */
    knob(SYNCEQ_DRIVE_SLOT, 100);
    expect("Drive 100, quiet", 1000, 3.0f, 10.0f);
    {
        float peak = 0.0f; double ph = 0.0;
        for (b = 0; b < 4000; b++) {
            for (j = 0; j < 8; j++) { fx[j] = 4.0f * (float)sin(ph); ph += 0.05; }
            Fx_DLY_SyncEQ(ctx);
            for (j = 0; j < 8; j++) { float v = fabsf(fx[j]); if (!(v == v)) v = 99.0f; if (v > peak) peak = v; }
        }
        printf("Drive 100, input 4.0: peak out %.3f\n", peak);
        CHECK(peak <= 0.36f, "drive output not bounded (%.3f)", peak);
    }
    knob(SYNCEQ_DRIVE_SLOT, 0);

    /* every knob at its extremes at once: finite */
    knob(SYNCEQ_LOCUT_SLOT, 50); knob(SYNCEQ_LOW_SLOT, 24); knob(SYNCEQ_MID_SLOT, 24); knob(SYNCEQ_MIDF_SLOT, 50);
    knob(SYNCEQ_HIGH_SLOT, 24); knob(SYNCEQ_HICUT_SLOT, 0); knob(SYNCEQ_DRIVE_SLOT, 100); knob(SYNCEQ_LEVEL_SLOT, 24);
    CHECK(gain_db(700, 1.0f) > -100.0f, "NaN or silence with every knob at the top");
    defaults();

    /* bar tag: nothing until Mozaic flips Tempo, then counter +1 per twin flip, BPM x 16 */
    knob(SYNCEQ_TEMPO_SLOT, 120);
    for (j = 0; j < 16; j++) dry[j] = 0.0f;
    gain_db(1000, 0.1f);
    CHECK(!dt_read(dry + 8, &t), "tag sent before any flip");
    for (j = 0; j < 16; j++) CHECK(dry[j] == 0.0f, "Dry buffer written before any flip");
    c0 = 0u;
    knob(SYNCEQ_TEMPO_SLOT, 321); Fx_DLY_SyncEQ(ctx);
    CHECK(dt_read(dry + 8, &t) && t.count == ((c0 + 1u) & 0xFFFu) && t.live && t.age == 0u && t.bpm16 == 1920u,
          "twin flip 120 -> 321 did not count a bar");
    Fx_DLY_SyncEQ(ctx);
    CHECK(dt_read(dry + 8, &t) && t.count == ((c0 + 1u) & 0xFFFu) && t.age == 1u, "counted twice for one flip");
    knob(SYNCEQ_TEMPO_SLOT, 150); Fx_DLY_SyncEQ(ctx);
    CHECK(dt_read(dry + 8, &t) && t.count == ((c0 + 2u) & 0xFFFu) && t.bpm16 == 2400u, "flip back to 150 BPM");
    knob(SYNCEQ_TEMPO_SLOT, 441); Fx_DLY_SyncEQ(ctx);              /* raw 4.41 */
    CHECK(dt_read(dry + 8, &t) && t.bpm16 == 3840u, "Tempo 441 should send 240 BPM (%u)", t.bpm16);
    for (j = 0; j < 8; j++) CHECK(dry[j] == 0.0f, "Dry left touched");

    /* switched off: input untouched, the tag still goes out and still counts */
    params[0] = 0.0f; changed = 0;
    knob(SYNCEQ_LOW_SLOT, 24);
    for (b = 0; b < 1000; b++) {
        float in[8];
        for (j = 0; j < 8; j++) { in[j] = 0.3f * (float)sin(0.02 * (double)(b * 8 + j)); fx[j] = in[j]; }
        if (b == 500) knob(SYNCEQ_TEMPO_SLOT, 240);
        Fx_DLY_SyncEQ(ctx);
        for (j = 0; j < 8; j++) if (fx[j] != in[j]) changed++;
    }
    CHECK(changed == 0, "switched off but the input was changed");
    CHECK(dt_read(dry + 8, &t) && t.count == ((c0 + 4u) & 0xFFFu) && t.age == 499u,
          "switched off: tag missing or flip not counted");
    printf("switched off: %ld samples changed, tag count %u age %u\n", changed, t.count, t.age);

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
