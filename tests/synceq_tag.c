/* Bar tag (src/custom/common/drytag.h): round trip, nothing else reads as a tag, sender rules.
 * Host logic only: whether the Dry buffer keeps every bit on the pedal is the first pedal test. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/custom/common/drytag.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

static unsigned int rng = 777u;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return (float)(int)(rng >> 8) * 5.9604645e-8f - 0.5f; }

int main(void)
{
    float r[8];
    DtTag t;
    DtSend a, b;
    long i, bad = 0;
    int j, wrote;
    float peak = 0.0f;

    /* round trip, including the largest values */
    dt_write(r, 0xFFFu, 4095u, 3840u, 0xFFFFFFu, 1u);
    CHECK(dt_read(r, &t) && t.id == 0xFFFu && t.count == 4095u && t.bpm16 == 3840u
          && t.age == 0xFFFFFFu && t.live == 1u, "round trip (max values)");
    dt_write(r, 1u, 0u, 640u, 0u, 0u);
    CHECK(dt_read(r, &t) && t.id == 1u && t.count == 0u && t.bpm16 == 640u && t.age == 0u
          && t.live == 0u, "round trip (min values)");
    for (j = 0; j < 8; j++) { float v = fabsf(r[j]); if (v > peak) peak = v; }
    printf("largest tag value %.3g (%.1f dBFS)\n", peak, 20.0f * log10f(peak));
    CHECK(peak < 0.00025f, "tag louder than -72 dBFS");

    /* any changed bit, a gain or an offset breaks it */
    dt_write(r, 77u, 12u, 1920u, 345u, 1u);
    r[3] *= 1.0001f;                    CHECK(!dt_read(r, &t), "scaled value still valid");
    dt_write(r, 77u, 12u, 1920u, 345u, 1u);
    r[2] = dt_put(13u);                 CHECK(!dt_read(r, &t), "changed counter still valid (checksum)");
    dt_write(r, 77u, 12u, 1920u, 345u, 1u);
    for (j = 0; j < 8; j++) r[j] *= 0.5f; CHECK(!dt_read(r, &t), "halved tag still valid");

    /* silence, DC, noise, quiet noise and NaN never read as a tag */
    memset(r, 0, sizeof r);             CHECK(!dt_read(r, &t), "silence reads as a tag");
    for (j = 0; j < 8; j++) r[j] = 0.25f; CHECK(!dt_read(r, &t), "DC reads as a tag");
    for (j = 0; j < 8; j++) r[j] = NAN; CHECK(!dt_read(r, &t), "NaN reads as a tag");
    for (i = 0; i < 2000000; i++) {
        float g = (i & 1) ? 1.0f : 0.0002f;           /* loud audio and noise at tag level */
        for (j = 0; j < 8; j++) r[j] = g * rnd();
        if (dt_read(r, &t)) bad++;
    }
    for (i = 0; i < 2000000; i++) {                   /* valid-looking values, random */
        for (j = 0; j < 8; j++) { rng = rng * 1664525u + 1013904223u; r[j] = dt_put(rng >> 20); }
        r[0] = dt_put(DT_SIG); r[6] = dt_put(DT_VERSION);
        if (dt_read(r, &t)) bad++;
    }
    printf("false tags in 4M random blocks: %ld\n", bad);
    CHECK(bad < 1500, "too many random blocks pass (checksum only should let ~1/4096 of the second set through)");

    /* sender: nothing before a flip, counter +1 per flip, age counts blocks, quiet 60 s after */
    memset(r, 0, sizeof r);
    dt_send_init(&a);
    wrote = dt_send(&a, r, 0, 1920u, 100u);
    CHECK(!wrote && !dt_read(r, &t), "sender wrote a tag before any flip (Mozaic not flipping it)");
    dt_send(&a, r, 1, 1920u, 100u); dt_read(r, &t);
    CHECK(t.id == 100u && t.bpm16 == 1920u && t.count == 1u && t.age == 0u && t.live,
          "flip: counter 1, age 0, LIVE");
    for (i = 0; i < 10; i++) dt_send(&a, r, 0, 1920u, 100u);
    dt_read(r, &t);
    CHECK(t.count == 1u && t.age == 10u, "age should count blocks (%u)", t.age);
    memset(r, 0, sizeof r);
    for (i = 0; i < (long)DT_LIVE_BLOCKS; i++) wrote = dt_send(&a, r, 0, 1920u, 100u);
    CHECK(!wrote, "still sending 60 s after the last flip");
    dt_send(&a, r, 1, 1920u, 100u); dt_read(r, &t);
    CHECK(t.count == 2u && t.live, "second flip");

    /* two LIVE senders in one block: the upstream tag stays */
    dt_send_init(&b);
    memset(r, 0, sizeof r);
    dt_send(&a, r, 0, 1920u, 100u);                      /* slot 1, LIVE */
    wrote = dt_send(&b, r, 1, 2000u, 200u);              /* slot 2, also LIVE */
    dt_read(r, &t);
    CHECK(!wrote && t.id == 100u, "second LIVE sender replaced the upstream LIVE tag");
    /* a tag that is not LIVE (left over from a sender that stopped) is replaced */
    dt_write(r, 100u, 5u, 1920u, 400000u, 0u);
    wrote = dt_send(&b, r, 0, 2000u, 200u);
    dt_read(r, &t);
    CHECK(wrote && t.id == 200u && t.live, "LIVE sender did not replace a tag that is not LIVE");
    /* its own tag left over from the last block is replaced */
    wrote = dt_send(&b, r, 0, 2000u, 200u);
    CHECK(wrote, "sender did not refresh its own tag");

    /* ids: 12 bits, never 0, different for arenas 64 KB apart */
    CHECK(dt_id(0) != 0u && dt_id(0x10000u) != dt_id(0x20000u) && dt_id(0x80123450u) <= 0xFFFu, "ids");

    printf("%d failed checks\n", fails);
    return fails ? 1 : 0;
}
