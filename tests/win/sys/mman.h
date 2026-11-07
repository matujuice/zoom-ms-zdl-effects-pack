/* Windows stand-in for the one mmap use in the host tests: zeroed memory below 4 GB
 * (the pedal context holds 32-bit pointers). run.py adds this folder on Windows only. */
#ifndef WIN_MMAN_H
#define WIN_MMAN_H
#include <windows.h>
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT 0x40
static void *mmap(void *a, size_t n, int p, int f, int fd, long o)
{
    uintptr_t at;
    void *m;
    (void)a; (void)p; (void)f; (void)fd; (void)o;
    for (at = 0x10000000u; at < 0xF0000000u; at += 0x01000000u)
        if ((m = VirtualAlloc((void *)at, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) return m;
    return 0;
}
#endif
