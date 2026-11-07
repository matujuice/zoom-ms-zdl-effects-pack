/*
 * zarena.h - leave the slot's arena safe for the next effect (pack issues #41, MIDISTOMP #60)
 *
 * The arena outlives the effect: there is no unload hook, and a stock delay or reverb loaded
 * into the same slot later reads what we left there as float audio. One NaN or Inf in its
 * delay line mutes it; a huge value is a long burst of noise. So every word an effect leaves
 * must read as a tiny float or as audio:
 *
 *   - The state's header (ints, the magic, the RNG, tempos, sample counts, cached numbers) is
 *     SEALED at the end of every block: bits 30 and 29 of each header word move into seal
 *     words (14 header words per seal word, bits 0..27), so every header word, and every seal
 *     word, has bits 30 and 29 clear: as a float |x| < 2^-63. The next block unseals first.
 *     A stale arena unseals to garbage and fails the magic, as before.
 *   - 16-bit sample rings store 14-bit codes (za_s14): bits 14 and 13 of every short are clear,
 *     so the upper short of any word never sets float bits 30 and 29. -84 dB noise floor.
 *   - Float arrays holding audio (delay lines, filter states) stay as they are: audio range.
 *
 * Layout: put the header first, then `unsigned int seal[ZA_NSEAL(words)]`, then the audio
 * arrays. ZA_HDR(Type) is the header's word count (everything before `seal`).
 * Same rules as the effects: no static data, no divide, no calls (always inline).
 */
#ifndef ZARENA_H
#define ZARENA_H
#include <stddef.h>

#ifdef __TI_COMPILER_VERSION__
#define ZA_DO_PRAGMA(x) _Pragma(#x)
#define ZA_INLINE(fn) ZA_DO_PRAGMA(FUNC_ALWAYS_INLINE(fn))
#else
#define ZA_INLINE(fn)
#endif

#define ZA_NSEAL(words)  (((words) + 13) / 14)
#define ZA_HDR(T)        ((int)(offsetof(T, seal) >> 2))
/* compile-time check that seal[] covers the header: ZA_CHECK(ScState); */
#define ZA_CHECK(T) \
    typedef char za_seal_fits_##T[(offsetof(T, seal) <= 4u * 14u * (sizeof(((T *)0)->seal) >> 2)) ? 1 : -1]

/* in the effect's init: seal[] may be longer than the header needs, and za_seal never
 * writes the spare words, so they would keep the last effect's leftovers */
#define ZA_ZERO_SEAL(s) do { int za_i_; \
    for (za_i_ = 0; za_i_ < (int)(sizeof((s)->seal) >> 2); za_i_++) (s)->seal[za_i_] = 0u; } while (0)
/* same when seal[] is the state's last field: also the padding after it */
#define ZA_ZERO_TAIL(s, T) do { int za_i_; \
    for (za_i_ = ZA_HDR(T); za_i_ < (int)(sizeof(T) >> 2); za_i_++) ((unsigned int *)(s))[za_i_] = 0u; } while (0)

/* end of a block: bits 30, 29 of n header words into seal[].
 * Full groups of 14 unroll (cl6x: about 1.1 cycles a word); only the tail loops. */
ZA_INLINE(za_seal)
static inline void za_seal(void *hdr, int n, unsigned int *seal)
{
#ifdef ZA_NO_SEAL
    return;
#endif
    unsigned int *w = (unsigned int *)hdr, c, x;
    int i, k;
    for (i = 0; i + 14 <= n; i += 14) {
        c = 0u;
        for (k = 0; k < 14; k++) {
            x = w[i + k];
            c |= (x & 0x60000000u) >> (29 - k - k);
            w[i + k] = x & 0x9FFFFFFFu;
        }
        *seal++ = c;
    }
    c = 0u;
    for (k = 0; i < n; i++, k += 2) {
        x = w[i];
        c |= ((x >> 29) & 3u) << k;
        w[i] = x & 0x9FFFFFFFu;
    }
    if (k) *seal = c;
}

/* start of a block: put them back */
ZA_INLINE(za_unseal)
static inline void za_unseal(void *hdr, int n, const unsigned int *seal)
{
#ifdef ZA_NO_SEAL
    return;
#endif
    unsigned int *w = (unsigned int *)hdr, c;
    int i, k;
    for (i = 0; i + 14 <= n; i += 14) {
        c = *seal++;
        for (k = 0; k < 14; k++)
            w[i + k] = (w[i + k] & 0x9FFFFFFFu) | ((c << (29 - k - k)) & 0x60000000u);
    }
    if (i < n) {
        c = *seal;
        for (k = 0; i < n; i++, k += 2)
            w[i] = (w[i] & 0x9FFFFFFFu) | (((c >> k) & 3u) << 29);
    }
}

/* 16-bit rings: v is the sample already scaled to -8192..8191 (callers clamp to +-8191).
 * Bit 15 keeps the sign, bits 14 and 13 (copies of it) are dropped. */
#define ZA_S14_MAX 8191.0f
ZA_INLINE(za_s14)
static inline short za_s14(int v) { return (short)(v & 0x9FFF); }

ZA_INLINE(za_u14)
static inline int za_u14(short s) { int x = s; return x | ((x >> 2) & 0x6000); }

#endif /* ZARENA_H */
