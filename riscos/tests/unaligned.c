/* Probes what unaligned loads and stores do on this CPU, in each instruction
 * form GCC emits. ARMv4 (StrongARM) does not fault: LDR returns the aligned
 * word rotated, STR writes the aligned word, and halfword accesses at odd
 * addresses are UNPREDICTABLE. ARMv7 with alignment checking on (RISC OS 5 on
 * a Pi) raises a data abort. Run it on an emulator and on real hardware: any
 * difference means the emulator can hide alignment bugs. */
#include <stdio.h>
#include <string.h>

static unsigned char buf[16] __attribute__((aligned(4)));

static void reset(void)
{
    int i;
    for (i = 0; i < 16; ++i) {
        buf[i] = (unsigned char)(0x11 * (i + 1));
    }
}

static void dump(const char* what, unsigned v)
{
    int i;
    printf("%-26s 0x%08x  buf:", what, v);
    for (i = 0; i < 12; ++i) {
        printf(" %02x", buf[i]);
    }
    printf("\n");
}

int main(void)
{
    unsigned v, w;
    unsigned char* p;
    int off;

    for (off = 1; off <= 3; ++off) {
        printf("-- offset +%d\n", off);

        reset();
        p = buf + off;
        __asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(p));
        dump("ldr [rn]", v);

        reset();
        p = buf;
        __asm__ volatile("ldr %0, [%1, %2]" : "=r"(v) : "r"(p), "r"(off));
        dump("ldr [rn, rm]", v);

        reset();
        p = buf + off;
        __asm__ volatile("ldr %0, [%1], #4" : "=r"(v), "+r"(p));
        dump("ldr [rn], #4 (post)", v);

        reset();
        p = buf + off - 4 + 4;
        __asm__ volatile("ldr %0, [%1, #4]!" : "=r"(v), "+r"(p));
        dump("ldr [rn, #4]! (pre, +4)", v);

        reset();
        p = buf + off;
        __asm__ volatile("ldrh %0, [%1]" : "=r"(v) : "r"(p));
        dump("ldrh [rn]", v);

        reset();
        p = buf + off;
        __asm__ volatile("ldrsh %0, [%1]" : "=r"(v) : "r"(p));
        dump("ldrsh [rn]", v);

        reset();
        p = buf + off;
        v = 0xA1B2C3D4;
        __asm__ volatile("str %0, [%1]" : : "r"(v), "r"(p) : "memory");
        dump("str [rn] of a1b2c3d4", v);

        reset();
        p = buf + off;
        v = 0xA1B2;
        __asm__ volatile("strh %0, [%1]" : : "r"(v), "r"(p) : "memory");
        dump("strh [rn] of a1b2", v);

        reset();
        p = buf + off;
        __asm__ volatile("ldmia %2, {%0, %1}" : "=&r"(v), "=&r"(w) : "r"(p));
        dump("ldmia {r,r} first", v);
        dump("ldmia {r,r} second", w);
    }

    reset();
    memcpy(&v, buf + 1, 4);
    dump("memcpy at +1 (reference)", v);
    return 0;
}
