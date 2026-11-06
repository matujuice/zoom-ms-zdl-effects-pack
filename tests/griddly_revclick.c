/* GridDly #36: REVRS with a synced Time on a running MIDI clock must not click. The clock
 * (121 BPM while the tempo word says 120) corrects the chunk phase at every clock; the
 * reversed read must glide over that, not jump. Input: a 110 Hz sine; Mix 100, Fdbk 0, so
 * the output is the reversed sine alone. Check: the biggest sample-to-sample step of the
 * output stays near the sine's own (a jump of a few samples would be many times bigger). */
#include "../src/custom/griddly/griddly.c"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mman.h>

#define ARENA_BYTES (1u << 20)
#define D(n) params[GRIDDLY_##n##_SLOT] = GRIDDLY_##n##_UI_DEFAULT / 100.0f

int main(void)
{
    unsigned char *m = mmap(0, 2u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    unsigned int *ctx = (unsigned int *)m, *magic = (unsigned int *)(m + 640), *desc = (unsigned int *)(m + 768);
    float *params = (float *)(m + 256), *fx = (float *)(m + 512), y, prev = 0.0f, worst = 0.0f;
    unsigned char *arena = m + 4096;
    double t = 0.0, per = 44100.0 * 60.0 / (121.0 * 24.0), ph = 0.0, own = 0.3 * 2.0 * 3.14159265 * 110.0 / 44100.0;
    long n, at = 0; int j;

    memset(arena, 0xC9, ARENA_BYTES);
    ctx[1] = (unsigned)(uintptr_t)params; ctx[3] = (unsigned)(uintptr_t)desc; ctx[5] = (unsigned)(uintptr_t)fx;
    magic[0] = 0xABCD1234u; magic[2] = (unsigned)(uintptr_t)&magic[1];
    ctx[11] = (unsigned)(uintptr_t)&magic[2]; ctx[12] = (unsigned)(uintptr_t)&magic[0];
    desc[0] = (unsigned)(uintptr_t)arena; desc[1] = (unsigned)(uintptr_t)(arena + ARENA_BYTES); desc[2] = ARENA_BYTES;
    params[0] = 1.0f;
    D(TIME); D(TONE); D(CHAR);
    params[GRIDDLY_TYPE_SLOT] = 0.03f;                 /* REVRS */
    params[GRIDDLY_FDBK_SLOT] = 0.0f; params[GRIDDLY_DUCK_SLOT] = 0.0f;
    params[GRIDDLY_MIX_SLOT] = 1.0f; params[GRIDDLY_TAIL_SLOT] = 0.0f;
    zt_host[0] = ZT_MAGIC; zt_host[2] = 1u; zt_host[3] = 1u; zt_host[5] = 12000u;   /* Start, word 120 BPM */

    for (n = 0; n < 44100L * 12 / 8; n++, t += 8.0) {
        zt_host[1] += 2u;
        zt_host[4] = (unsigned int)(t / per) + 1u;     /* a clock every per samples */
        for (j = 0; j < 8; j++) { fx[j] = fx[j + 8] = (float)(0.3 * sin(ph)); ph += 2.0 * 3.14159265 * 110.0 / 44100.0; }
        Fx_DLY_GridDly(ctx);
        for (j = 0; j < 8; j++) {
            y = fx[j];
            if (t > 44100.0 * 2.0 && fabsf(y - prev) > worst) { worst = fabsf(y - prev); at = (long)t + j; }
            prev = y;
        }
    }
    printf("REVRS on a 121 BPM clock: biggest step %.4f at sample %ld (the sine's own %.4f)\n", worst, at, own);
    if (worst > 3.0 * own) { printf("FAIL: REVRS clicks on the clock\n"); return 1; }
    printf("griddly_revclick: ok\n");
    return 0;
}
