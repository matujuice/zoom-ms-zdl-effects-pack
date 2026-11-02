/* Shared by the *_stereo.c tests: a fake pedal context for one effect's audio function.
 * se_init(arena) fills ctx[], params[], the 16-float chain buffer fx[] (L = fx[0..7], R = fx[8..15]),
 * the magic shuttle and the effect's state arena (desc), all below 4 GB like on the pedal. */
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
static unsigned char *se_m; static unsigned int *ctx, *se_magic, *se_desc; static float *fx, *params;
static void se_init(unsigned int arena)
{
    se_m = mmap(0, arena + (1u << 16), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ctx = (unsigned int *)se_m; params = (float *)(se_m + 256); fx = (float *)(se_m + 512);
    se_magic = (unsigned int *)(se_m + 640); se_desc = (unsigned int *)(se_m + 768);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)se_desc; ctx[5] = (unsigned)(uintptr_t)fx;
    se_magic[2] = (unsigned)(uintptr_t)&se_magic[1];
    ctx[11] = (unsigned)(uintptr_t)&se_magic[2]; ctx[12] = (unsigned)(uintptr_t)&se_magic[0];
    se_desc[0] = (unsigned)(uintptr_t)(se_m + 4096); se_desc[1] = se_desc[0] + arena; se_desc[2] = arena;
    params[0] = 1.0f;
}
