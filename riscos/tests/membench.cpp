/*
** membench: times the kinds of inner loop the games spend their time in, on the
** machine it runs on, to judge what hand-written ARM code could gain there:
** memory write and copy bandwidth by store width (STRB, STR, STM), UnixLib's
** memcpy and memset against LDM/STM loops, 24x24 tile copies, table look-ups in
** small and 64 KB tables, pixel doubling, screen memory writes, divides, long
** multiplies, function calls with and without an APCS frame, and LCW decompression.
**
**   membench [seconds per test, default 1] [-noscreen]
**
** Run it twice on a Risc PC, the second time after *Cache Off (and *Cache On after),
** to see the machine with every fetch going to main memory, the nearest thing to an
** ARM610's small cache. Measurement only: the screen is written with what it held.
*/
#include "../../common/lcw.h"

#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <swis.h>
#include "poll_once.h"

static unsigned Cs()
{
    _kernel_swi_regs r;
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    return unsigned(r.r[0]);
}

static double Secs = 1.0;

// Repeats f until Secs have passed; returns the time per call in microseconds.
template <typename F> static double Time(F f)
{
    unsigned calls = 0, start = Cs(), now;
    do {
        f();
        ++calls;
        now = Cs();
    } while (now - start < unsigned(Secs * 100));
    return (now - start) * 10000.0 / calls;
}

static void Report(const char* what, double us, double bytes)
{
    if (bytes > 0) {
        printf("%-58s %10.2f us  %8.1f MB/s\n", what, us, bytes / us);
    } else {
        printf("%-58s %10.3f us\n", what, us);
    }
    fflush(stdout);
}

extern "C" {
// Stores: r0 dst, r1 bytes (a multiple of 32).
void Store_Bytes(void* dst, unsigned bytes);
void Store_Words(void* dst, unsigned bytes);
void Store_Stm4(void* dst, unsigned bytes);
void Store_Stm8(void* dst, unsigned bytes);
// Copies: r0 dst, r1 src, r2 bytes (a multiple of 32).
void Copy_Words(void* dst, const void* src, unsigned bytes);
void Copy_Ldm8(void* dst, const void* src, unsigned bytes);
// Reads: r0 src, r1 bytes; returns a checksum.
unsigned Read_Words(const void* src, unsigned bytes);
unsigned Read_Ldm8(const void* src, unsigned bytes);
}

asm(".text\n.align 2\n"
    ".global Store_Bytes\nStore_Bytes:\n"
    "mov r2, #0\n"
    "1: strb r2, [r0], #1\nstrb r2, [r0], #1\nstrb r2, [r0], #1\nstrb r2, [r0], #1\n"
    "subs r1, r1, #4\nbgt 1b\nmov pc, lr\n"
    ".global Store_Words\nStore_Words:\n"
    "mov r2, #0\n"
    "1: str r2, [r0], #4\nstr r2, [r0], #4\nstr r2, [r0], #4\nstr r2, [r0], #4\n"
    "subs r1, r1, #16\nbgt 1b\nmov pc, lr\n"
    ".global Store_Stm4\nStore_Stm4:\n"
    "stmfd sp!, {r4, lr}\nmov r2, #0\nmov r3, #0\nmov r4, #0\nmov r12, #0\n"
    "1: stmia r0!, {r2, r3, r4, r12}\nstmia r0!, {r2, r3, r4, r12}\n"
    "subs r1, r1, #32\nbgt 1b\nldmfd sp!, {r4, pc}\n"
    ".global Store_Stm8\nStore_Stm8:\n"
    "stmfd sp!, {r4-r8, lr}\nmov r2, #0\nmov r3, #0\nmov r4, #0\nmov r5, #0\nmov r6, #0\nmov r7, #0\nmov r8, #0\nmov r12, #0\n"
    "1: stmia r0!, {r2-r8, r12}\n"
    "subs r1, r1, #32\nbgt 1b\nldmfd sp!, {r4-r8, pc}\n"
    ".global Copy_Words\nCopy_Words:\n"
    "1: ldr r3, [r1], #4\nstr r3, [r0], #4\nldr r3, [r1], #4\nstr r3, [r0], #4\n"
    "subs r2, r2, #8\nbgt 1b\nmov pc, lr\n"
    ".global Copy_Ldm8\nCopy_Ldm8:\n"
    "stmfd sp!, {r4-r8, lr}\n"
    "1: ldmia r1!, {r3-r8, r12, lr}\nstmia r0!, {r3-r8, r12, lr}\n"
    "subs r2, r2, #32\nbgt 1b\nldmfd sp!, {r4-r8, pc}\n"
    ".global Read_Words\nRead_Words:\n"
    "mov r2, #0\n"
    "1: ldr r3, [r0], #4\nadd r2, r2, r3\nldr r3, [r0], #4\nadd r2, r2, r3\n"
    "subs r1, r1, #8\nbgt 1b\nmov r0, r2\nmov pc, lr\n"
    ".global Read_Ldm8\nRead_Ldm8:\n"
    "stmfd sp!, {r4-r8, lr}\nmov r2, #0\n"
    "1: ldmia r0!, {r3-r8, r12, lr}\nadd r2, r2, r3\n"
    "subs r1, r1, #32\nbgt 1b\nmov r0, r2\nldmfd sp!, {r4-r8, pc}\n");

// A function with an APCS frame and stack check, and a leaf without, as GCC makes them.
__attribute__((noinline)) int Framed(int a, int b);
__attribute__((noinline)) int Leaf(int a, int b)
{
    return a + b;
}
static volatile int Sink;
__attribute__((noinline)) int Framed(int a, int b)
{
    char pad[8];
    pad[0] = char(a);
    Sink = pad[0];
    return Leaf(a, b) + 1; // a call, so it needs a frame
}

int main(int argc, char** argv)
{
    Poll_Once_At_Start(); // see poll_once.h: uncached until the first Wimp_Poll on the Risc PC
    bool screen = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-noscreen") == 0) {
            screen = false;
        } else {
            Secs = atof(argv[i]);
        }
    }
    const unsigned BIG = 1024 * 1024; // well beyond any cache
    unsigned char* a = static_cast<unsigned char*>(malloc(BIG + 64));
    unsigned char* b = static_cast<unsigned char*>(malloc(BIG + 64));
    unsigned char* small = static_cast<unsigned char*>(malloc(8192 + 64));
    if (!a || !b || !small) {
        printf("no memory\n");
        return 1;
    }
    a = reinterpret_cast<unsigned char*>((reinterpret_cast<unsigned>(a) + 31) & ~31u);
    b = reinterpret_cast<unsigned char*>((reinterpret_cast<unsigned>(b) + 31) & ~31u);
    small = reinterpret_cast<unsigned char*>((reinterpret_cast<unsigned>(small) + 31) & ~31u);
    for (unsigned i = 0; i < BIG; ++i) {
        b[i] = (unsigned char)(i * 7 + (i >> 8));
    }
    printf("membench: %.1f s per test\n", Secs);

    printf("-- writes, 1 MB (main memory)\n");
    Report("STRB, 1 byte a store", Time([&] { Store_Bytes(a, BIG); }), BIG);
    Report("STR, 1 word a store", Time([&] { Store_Words(a, BIG); }), BIG);
    Report("STM of 4 registers", Time([&] { Store_Stm4(a, BIG); }), BIG);
    Report("STM of 8 registers", Time([&] { Store_Stm8(a, BIG); }), BIG);
    Report("memset (UnixLib)", Time([&] { memset(a, 0, BIG); }), BIG);
    printf("-- writes, 4 KB (fits the caches)\n");
    Report("STRB, 4 KB", Time([&] { Store_Bytes(small, 4096); }), 4096);
    Report("STR, 4 KB", Time([&] { Store_Words(small, 4096); }), 4096);
    Report("STM of 8 registers, 4 KB", Time([&] { Store_Stm8(small, 4096); }), 4096);

    printf("-- reads, 1 MB\n");
    Report("LDR words", Time([&] { Sink = int(Read_Words(b, BIG)); }), BIG);
    Report("LDM of 8 registers", Time([&] { Sink = int(Read_Ldm8(b, BIG)); }), BIG);

    printf("-- copies, 1 MB to 1 MB\n");
    Report("LDR/STR words", Time([&] { Copy_Words(a, b, BIG); }), BIG);
    Report("LDM/STM 8 registers", Time([&] { Copy_Ldm8(a, b, BIG); }), BIG);
    Report("memcpy (UnixLib), aligned", Time([&] { memcpy(a, b, BIG); }), BIG);
    Report("memcpy (UnixLib), source 1 byte off", Time([&] { memcpy(a, b + 1, BIG - 64); }), BIG);
    Report("memcpy (UnixLib), source 2 bytes off", Time([&] { memcpy(a, b + 2, BIG - 64); }), BIG);
    Report("memmove (UnixLib), overlapping upwards", Time([&] { memmove(a + 32, a, BIG - 64); }), BIG);
    Report("byte loop", Time([&] {
               volatile unsigned char* d = a;
               for (unsigned i = 0; i < BIG; ++i) {
                   d[i] = b[i];
               }
           }),
           BIG);

    printf("-- 24x24 tiles into a 640-wide page (2 KB of tiles, from a 64 KB tile set)\n");
    {
        unsigned tile = 0;
        Report("memcpy per row (as Buffer_Draw_Stamp)", Time([&] {
                   for (int t = 0; t < 100; ++t) {
                       const unsigned char* s = b + ((tile++ * 576) & 0xFFFF);
                       unsigned char* d = a + (t % 26) * 24 + (t / 26) * 24 * 640;
                       for (int y = 0; y < 24; ++y) {
                           memcpy(d + y * 640, s + y * 24, 24);
                       }
                   }
               }) / 100,
               576);
        Report("LDM/STM per row, 6 words", Time([&] {
                   for (int t = 0; t < 100; ++t) {
                       const unsigned char* s = b + ((tile++ * 576) & 0xFFFF);
                       unsigned char* d = a + (t % 26) * 24 + (t / 26) * 24 * 640;
                       for (int y = 0; y < 24; ++y) {
                           register unsigned* dd asm("r0") = reinterpret_cast<unsigned*>(d + y * 640);
                           register const unsigned* ss asm("r1") = reinterpret_cast<const unsigned*>(s + y * 24);
                           asm volatile("ldmia r1, {r2-r7}\nstmia r0, {r2-r7}\n"
                                        :
                                        : "r"(dd), "r"(ss)
                                        : "r2", "r3", "r4", "r5", "r6", "r7", "memory");
                       }
                   }
               }) / 100,
               576);
        Report("transparent byte loop, half the pixels 0", Time([&] {
                   for (int t = 0; t < 100; ++t) {
                       const unsigned char* s = b + ((tile++ * 576) & 0xFFFF);
                       unsigned char* d = a + (t % 26) * 24 + (t / 26) * 24 * 640;
                       for (int y = 0; y < 24; ++y) {
                           for (int x = 0; x < 24; ++x) {
                               unsigned char c = s[y * 24 + x];
                               if (c & 1) {
                                   d[y * 640 + x] = c;
                               }
                           }
                       }
                   }
               }) / 100,
               576);
    }

    printf("-- table look-ups, 1 MB of source bytes\n");
    {
        unsigned char* table = static_cast<unsigned char*>(malloc(65536));
        for (int i = 0; i < 65536; ++i) {
            table[i] = (unsigned char)(i ^ (i >> 8));
        }
        Report("256-byte table (remap, fading)", Time([&] {
                   for (unsigned i = 0; i < BIG; ++i) {
                       a[i] = table[b[i]];
                   }
               }),
               BIG);
        Report("64 KB table, indexed by source and destination (ghost, interpolation)", Time([&] {
                   for (unsigned i = 0; i < BIG; ++i) {
                       a[i] = table[b[i] | (a[i] << 8)];
                   }
               }),
               BIG);
        free(table);
    }

    printf("-- pixel doubling, 320x200 to 640x400 in main memory\n");
    Report("byte loop, then memcpy of the row (as Scale_Rect)", Time([&] {
               for (int y = 0; y < 200; ++y) {
                   const unsigned char* s = b + y * 320;
                   unsigned char* d = a + y * 2 * 640;
                   for (int x = 0; x < 320; ++x) {
                       d[2 * x] = d[2 * x + 1] = s[x];
                   }
                   memcpy(d + 640, d, 640);
               }
           }),
           256000);
    Report("words: 4 source pixels to 2 words, both rows stored", Time([&] {
               for (int y = 0; y < 200; ++y) {
                   const unsigned* s = reinterpret_cast<const unsigned*>(b + y * 320);
                   unsigned* d = reinterpret_cast<unsigned*>(a + y * 2 * 640);
                   for (int x = 0; x < 80; ++x) {
                       unsigned p = s[x];
                       unsigned lo = (p & 0xFF) | (p & 0xFF) << 8 | (p & 0xFF00) << 8 | (p & 0xFF00) << 16;
                       unsigned hi = (p >> 16 & 0xFF) | (p >> 8 & 0xFF00) | (p & 0xFF000000) >> 8 | (p & 0xFF000000);
                       d[2 * x] = lo;
                       d[2 * x + 1] = hi;
                       d[160 + 2 * x] = lo;
                       d[160 + 2 * x + 1] = hi;
                   }
               }
           }),
           256000);

    if (screen) {
        _kernel_swi_regs r;
        int in[3] = {148, 150, -1}; // ScreenStart, TotalScreenSize
        int out[2];
        r.r[0] = reinterpret_cast<int>(in);
        r.r[1] = reinterpret_cast<int>(out);
        _kernel_swi(OS_ReadVduVariables, &r, &r);
        unsigned char* scr = reinterpret_cast<unsigned char*>(out[0]);
        unsigned n = unsigned(out[1]) < 256 * 1024 ? unsigned(out[1]) & ~31u : 256 * 1024;
        memcpy(b, scr, n); // the screen as it is, written back by every test
        printf("-- screen memory, %u KB of the desktop, written with what it shows\n", n / 1024);
        Report("LDM/STM 8 registers, main memory to screen", Time([&] { Copy_Ldm8(scr, b, n); }), n);
        Report("LDR/STR words, main memory to screen", Time([&] { Copy_Words(scr, b, n); }), n);
        Report("memcpy (UnixLib), main memory to screen", Time([&] { memcpy(scr, b, n); }), n);
        Report("byte stores, main memory to screen", Time([&] {
                   volatile unsigned char* d = scr;
                   for (unsigned i = 0; i < n; ++i) {
                       d[i] = b[i];
                   }
               }),
               n);
        Report("LDM 8 registers, reading the screen", Time([&] { Sink = int(Read_Ldm8(scr, n)); }), n);
        memcpy(scr, b, n);
    }

    printf("-- arithmetic and calls (per operation)\n");
    {
        volatile int x = 123456789, y = 37;
        volatile long long p = 0;
        Report("int divide (__divsi3)", Time([&] {
                   for (int i = 0; i < 1000; ++i) {
                       Sink = x / (y + (i & 7));
                   }
               }) / 1000,
               0);
        Report("int multiply", Time([&] {
                   for (int i = 0; i < 1000; ++i) {
                       Sink = x * (y + (i & 7));
                   }
               }) / 1000,
               0);
        Report("long long multiply (SMULL on ARMv3M)", Time([&] {
                   for (int i = 0; i < 1000; ++i) {
                       p = (long long)x * (y + (i & 7));
                   }
               }) / 1000,
               0);
        Report("call of a leaf function (no frame)", Time([&] {
                   for (int i = 0; i < 1000; ++i) {
                       Sink = Leaf(i, 1);
                   }
               }) / 1000,
               0);
        Report("call of a function with an APCS frame and stack check", Time([&] {
                   for (int i = 0; i < 1000; ++i) {
                       Sink = Framed(i, 1);
                   }
               }) / 1000,
               0);
    }

    printf("-- LCW (Format80), 64 KB of shape-like data\n");
    {
        const unsigned N = 65536;
        unsigned char* raw = b;
        for (unsigned i = 0; i < N; ++i) {
            unsigned x = i % 64, row = i / 64;
            raw[i] = ((x + row) % 23 < 9) ? 0 : (unsigned char)((x * 3 + row) & 0x1F | 0x60);
        }
        unsigned char* packed = a;
        int packed_len = LCW_Comp(raw, packed, N);
        unsigned char* out = static_cast<unsigned char*>(malloc(N + 64));
        Report("LCW_Uncompress (output bytes)", Time([&] { LCW_Uncompress(packed, out, N); }), N);
        printf("   (packed %d bytes, %s)\n", packed_len, memcmp(out, raw, N) == 0 ? "round trip ok" : "ROUND TRIP FAILED");
        Report("memcpy of the same 64 KB, for comparison", Time([&] { memcpy(out, raw, N); }), N);
        free(out);
    }
    return 0;
}
