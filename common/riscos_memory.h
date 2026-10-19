/*
** Copies for RISC OS on ARM: the block copies the presents and the map tiles use. The
** program's memcpy, memmove and memset are in riscos_memory.cpp too, replacing UnixLib's.
*/
#ifndef RISCOS_MEMORY_H
#define RISCOS_MEMORY_H

#if defined(__riscos__) && defined(__arm__)

#include <stdint.h>
#include <string.h>

extern "C" {
/*
** A 24-byte-wide block (a map tile's rows): "rows" rows (at least 1) from a packed
** source to a destination whose pitch is a multiple of 4, either of any alignment.
*/
void VC_Stamp_Rows_24(void* dst, const void* src, int dst_pitch, int rows);
}

/*
** Copies n bytes between areas that don't overlap, inline: when both have the same
** alignment within a word, LDM/STM of 8 registers for each 32 bytes, then 16, 8 and 4,
** then the last bytes. Otherwise (or under 16 bytes) memcpy, which shifts and merges.
** For presents: a row of the frame to the screen, or a span of changed tiles.
*/
static inline void VC_Copy_Span(void* dst, const void* src, unsigned n)
{
    unsigned char* d = static_cast<unsigned char*>(dst);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    if (n < 16 || ((reinterpret_cast<uintptr_t>(d) ^ reinterpret_cast<uintptr_t>(s)) & 3) != 0) {
        memcpy(d, s, n);
        return;
    }
    while ((reinterpret_cast<uintptr_t>(d) & 3) != 0) {
        *d++ = *s++;
        --n;
    }
    register unsigned char* r0 asm("r0") = d;
    register const unsigned char* r1 asm("r1") = s;
    register unsigned r2 asm("r2") = n;
    asm volatile("subs r2, r2, #32\n"
                 "blo 2f\n"
                 "1: ldmia r1!, {r3, r4, r5, r6, r7, r8, r12, lr}\n"
                 "stmia r0!, {r3, r4, r5, r6, r7, r8, r12, lr}\n"
                 "subs r2, r2, #32\n"
                 "bhs 1b\n"
                 "2: tst r2, #16\n"
                 "ldmneia r1!, {r3, r4, r5, r6}\n"
                 "stmneia r0!, {r3, r4, r5, r6}\n"
                 "tst r2, #8\n"
                 "ldmneia r1!, {r3, r4}\n"
                 "stmneia r0!, {r3, r4}\n"
                 "tst r2, #4\n"
                 "ldrne r3, [r1], #4\n"
                 "strne r3, [r0], #4\n"
                 "ands r2, r2, #3\n"
                 "beq 3f\n"
                 "cmp r2, #2\n"
                 "ldrb r3, [r1], #1\n"
                 "strb r3, [r0], #1\n"
                 "ldrhsb r3, [r1], #1\n"
                 "strhsb r3, [r0], #1\n"
                 "ldrhib r3, [r1], #1\n"
                 "strhib r3, [r0], #1\n"
                 "3:\n"
                 : "+r"(r0), "+r"(r1), "+r"(r2)
                 :
                 : "r3", "r4", "r5", "r6", "r7", "r8", "r12", "lr", "cc", "memory");
}

#endif
#endif
