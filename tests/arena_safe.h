/* Shared by the *_arena.c tests (MIDISTOMP #60): run an effect with random audio, knobs and
 * on/off, then scan its state. A stock delay or reverb loaded into the slot later reads every
 * word as a float, so each word must have bits 30 and 29 clear (|x| < 2^-63) or be a finite
 * float of audio size. Include after stereo_entry.h and the effect's .c. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../src/custom/common/zarena.h"
static unsigned int as_rng = 12345u;
static float as_rand(void) { as_rng = as_rng * 1664525u + 1013904223u; return (float)(as_rng >> 8) * (1.0f / 16777216.0f); }

/* words of the state that a stock effect would read as noise or NaN */
static long as_bad(size_t bytes, long *first)
{
    const unsigned int *w = (const unsigned int *)(uintptr_t)((se_desc[0] + 3u) & ~3u);
    long i, n = (long)(bytes >> 2), bad = 0;
    for (i = 0; i < n; i++) {
        union { unsigned int u; float f; } c; c.u = w[i];
        if ((c.u & 0x60000000u) == 0u) continue;
        if (c.f == c.f && fabsf(c.f) <= 16.0f) continue;
        if (!bad) *first = i;
        bad++;
    }
    return bad;
}

/* blocks of 8 frames; knob slots 1..nslots random every ~0.5 s, switched off now and then */
static int as_run(void (*fx_fn)(unsigned int *), size_t bytes, int nslots, long blocks, const char *name)
{
    long b, first = -1, bad;
    int j, fails = 0;
    float amp = 0.5f;
    /* the slot holds the last effect's leftovers, not zeros */
    memset((void *)(uintptr_t)se_desc[0], 0xC9, se_desc[2]);
    for (b = 0; b < blocks; b++) {
        if ((b & 2047) == 0) {
            for (j = 1; j <= nslots; j++) params[j] = as_rand() * 1.2f;
            params[0] = (as_rand() < 0.2f) ? 0.0f : 1.0f;
            amp = (as_rand() < 0.3f) ? 0.0f : 2.0f * as_rand();
        }
        for (j = 0; j < 16; j++) fx[j] = amp * (2.0f * as_rand() - 1.0f);
        fx_fn(ctx);
        if ((b & 4095) == 4095 && (bad = as_bad(bytes, &first)) != 0) {
            printf("FAIL: %s: %ld unsafe words after %ld blocks, first at word %ld (0x%08x)\n", name, bad, b + 1,
                   first, ((unsigned int *)(uintptr_t)((se_desc[0] + 3u) & ~3u))[first]);
            return 1;
        }
    }
    {   /* the 14-bit ring codes round-trip */
        int v;
        for (v = -8191; v <= 8191; v++)
            if (za_u14(za_s14(v)) != v || (za_s14(v) & 0x6000) != 0) { printf("FAIL: za_s14 %d\n", v); fails++; break; }
    }
    {   /* seal, unseal gives the header back (the other tests build with ZA_NO_SEAL to read state) */
        unsigned int h[64], c[64], sl[5];
        int n, i;
        for (n = 1; n <= 64 && !fails; n++) {
            for (i = 0; i < n; i++) h[i] = c[i] = (unsigned int)(as_rand() * 16777216.0f) * 0x9E3779B1u;
            za_seal(h, n, sl);
            for (i = 0; i < n; i++) if (h[i] & 0x60000000u) { printf("FAIL: sealed word with bit 30 or 29 (n %d)\n", n); fails++; break; }
            za_unseal(h, n, sl);
            for (i = 0; i < n; i++) if (h[i] != c[i]) { printf("FAIL: seal round trip (n %d, word %d)\n", n, i); fails++; break; }
        }
    }
    if (!fails) printf("%s: arena safe after %ld blocks\n", name, blocks);
    return fails;
}
