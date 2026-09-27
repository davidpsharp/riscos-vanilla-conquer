/* Probes what an unaligned 32-bit load does on this CPU.
 * ARMv4 (StrongARM) returns the aligned word rotated; ARMv7 with alignment
 * checking on (RISC OS 5 on a Pi) raises a data abort. */
#include <stdio.h>
#include <string.h>

static unsigned int load_word(const unsigned char* p)
{
    unsigned int v;
    /* A real LDR, not a compiler-split load. */
    __asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(p));
    return v;
}

int main(void)
{
    static unsigned char buf[8] __attribute__((aligned(4))) = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    unsigned int expected;
    unsigned int got;

    memcpy(&expected, buf + 1, 4);
    got = load_word(buf + 1);
    printf("unaligned LDR at +1: got 0x%08x, bytewise 0x%08x -> %s\n",
           got, expected, got == expected ? "unaligned access supported" : "ROTATED (silent corruption)");
    return 0;
}
