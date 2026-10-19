/*
** memspeed: times UnixLib's memcpy, memmove and memset against the program's own
** (common/riscos_memory.cpp, built here as vc_memcpy and so on), and the present and
** tile copies of common/riscos_memory.h, on the kinds of copy the games make:
** large copies at each alignment, overlapping moves, fills, 24-byte tile rows, the
** rows and partial spans of a present to the screen.
**
**   memspeed [seconds per test, default 1] [-noscreen]
**
** Screen tests write back what the screen already shows. Measurement only.
*/
#define VC_MEM_PREFIX "vc_"
#include "../../common/riscos_memory.cpp"
#include "../../common/riscos_memory.h"

#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <swis.h>

extern "C" {
void* vc_memcpy(void* dst, const void* src, size_t n);
void* vc_memmove(void* dst, const void* src, size_t n);
void* vc_memset(void* dst, int c, size_t n);
}

static unsigned Cs()
{
    _kernel_swi_regs r;
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    return unsigned(r.r[0]);
}

static double Secs = 1.0;

template <typename F> static double Time(F f)
{
    f(); // once to warm the caches (as in a game, where the same areas recur)
    unsigned calls = 0, start = Cs(), now;
    do {
        f();
        ++calls;
        now = Cs();
    } while (now - start < unsigned(Secs * 100));
    return (now - start) * 10000.0 / calls;
}

// Prints UnixLib's time, ours, and the ratio.
static void Pair(const char* what, double old_us, double new_us, double bytes)
{
    printf("%-50s %9.2f %9.2f us  %7.1f %7.1f MB/s  x%.2f\n",
           what,
           old_us,
           new_us,
           bytes / old_us,
           bytes / new_us,
           old_us / new_us);
    fflush(stdout);
}

static void One(const char* what, double us, double bytes)
{
    printf("%-50s %9s %9.2f us  %7s %7.1f MB/s\n", what, "", us, "", bytes / us);
    fflush(stdout);
}

// The 32-byte-multiple loop presents used before (video_sdl1.cpp's Copy_32).
static void Copy_32(void* dst, const void* src, unsigned bytes)
{
    register unsigned r0 asm("r0") = reinterpret_cast<unsigned>(dst);
    register unsigned r1 asm("r1") = reinterpret_cast<unsigned>(src);
    register unsigned r2 asm("r2") = bytes;
    asm volatile("1:\n"
                 "ldmia r1!, {r3, r4, r5, r6, r7, r8, r12, lr}\n"
                 "stmia r0!, {r3, r4, r5, r6, r7, r8, r12, lr}\n"
                 "subs r2, r2, #32\n"
                 "bgt 1b\n"
                 : "+r"(r0), "+r"(r1), "+r"(r2)
                 :
                 : "r3", "r4", "r5", "r6", "r7", "r8", "r12", "lr", "cc", "memory");
}

int main(int argc, char** argv)
{
    bool screen = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-noscreen") == 0) {
            screen = false;
        } else {
            Secs = atof(argv[i]);
        }
    }
    const unsigned BIG = 1024 * 1024;
    unsigned char* a = static_cast<unsigned char*>(malloc(BIG + 4096));
    unsigned char* b = static_cast<unsigned char*>(malloc(BIG + 4096));
    if (!a || !b) {
        printf("no memory\n");
        return 1;
    }
    a = reinterpret_cast<unsigned char*>((reinterpret_cast<unsigned>(a) + 31) & ~31u);
    b = reinterpret_cast<unsigned char*>((reinterpret_cast<unsigned>(b) + 31) & ~31u);
    for (unsigned i = 0; i < BIG + 2048; ++i) {
        b[i] = (unsigned char)(i * 7 + (i >> 8));
        a[i] = 0;
    }
    printf("memspeed: %.1f s per test; columns: UnixLib, ours (time per call, then MB/s), speed-up\n", Secs);

    printf("-- 1 MB copies (beyond the caches)\n");
    const unsigned M = BIG - 64;
    for (int so = 0; so < 4; ++so) {
        for (int dof = 0; dof < 4; dof += 3) {
            char what[64];
            snprintf(what, sizeof(what), "memcpy, source +%d, destination +%d", so, dof);
            Pair(what,
                 Time([&] { memcpy(a + dof, b + so, M); }),
                 Time([&] { vc_memcpy(a + dof, b + so, M); }),
                 M);
        }
    }
    Pair("memmove, overlapping upwards by 32",
         Time([&] { memmove(a + 32, a, M - 32); }),
         Time([&] { vc_memmove(a + 32, a, M - 32); }),
         M);
    Pair("memmove, overlapping upwards by 1",
         Time([&] { memmove(a + 1, a, M - 32); }),
         Time([&] { vc_memmove(a + 1, a, M - 32); }),
         M);
    Pair("memmove, overlapping downwards by 3",
         Time([&] { memmove(a, a + 3, M - 32); }),
         Time([&] { vc_memmove(a, a + 3, M - 32); }),
         M);
    Pair("memset, 1 MB", Time([&] { memset(a, 0x5A, BIG); }), Time([&] { vc_memset(a, 0x5A, BIG); }), BIG);
    Pair("memset, 1 MB, destination +1",
         Time([&] { memset(a + 1, 0x5A, M); }),
         Time([&] { vc_memset(a + 1, 0x5A, M); }),
         M);

    printf("-- in the caches: 4 KB copies, fills\n");
    Pair("memcpy 4 KB, aligned", Time([&] { memcpy(a, b, 4096); }), Time([&] { vc_memcpy(a, b, 4096); }), 4096);
    Pair("memcpy 4 KB, source +2", Time([&] { memcpy(a, b + 2, 4096); }), Time([&] { vc_memcpy(a, b + 2, 4096); }), 4096);
    Pair("memset 4 KB", Time([&] { memset(a, 1, 4096); }), Time([&] { vc_memset(a, 1, 4096); }), 4096);

    printf("-- small copies, 1000 calls (aligned unless said)\n");
    {
        const unsigned sizes[] = {4, 8, 16, 24, 64, 256};
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            unsigned n = sizes[i];
            char what[64];
            snprintf(what, sizeof(what), "memcpy of %u bytes", n);
            Pair(what,
                 Time([&] {
                     for (int k = 0; k < 1000; ++k) {
                         memcpy(a + (k & 63) * 64, b + (k & 31) * 64, n);
                     }
                 }) / 1000,
                 Time([&] {
                     for (int k = 0; k < 1000; ++k) {
                         vc_memcpy(a + (k & 63) * 64, b + (k & 31) * 64, n);
                     }
                 }) / 1000,
                 n);
            snprintf(what, sizeof(what), "memcpy of %u bytes, source +1", n);
            Pair(what,
                 Time([&] {
                     for (int k = 0; k < 1000; ++k) {
                         memcpy(a + (k & 63) * 64, b + (k & 31) * 64 + 1, n);
                     }
                 }) / 1000,
                 Time([&] {
                     for (int k = 0; k < 1000; ++k) {
                         vc_memcpy(a + (k & 63) * 64, b + (k & 31) * 64 + 1, n);
                     }
                 }) / 1000,
                 n);
            snprintf(what, sizeof(what), "memset of %u bytes", n);
            Pair(what,
                 Time([&] {
                     for (int k = 0; k < 1000; ++k) {
                         memset(a + (k & 63) * 64, k, n);
                     }
                 }) / 1000,
                 Time([&] {
                     for (int k = 0; k < 1000; ++k) {
                         vc_memset(a + (k & 63) * 64, k, n);
                     }
                 }) / 1000,
                 n);
        }
    }

    printf("-- 24x24 tiles into a 640-wide page, 100 tiles from a 64 KB tile set (per tile)\n");
    for (int so = 0; so < 4; so += 1) {
        unsigned tile = 0;
        char what[80];
        snprintf(what, sizeof(what), "memcpy per row, tile set +%d", so);
        auto rows = [&](void* (*fn)(void*, const void*, size_t)) {
            return Time([&] {
                       for (int t = 0; t < 100; ++t) {
                           const unsigned char* s = b + so + ((tile++ * 576) & 0xFFFF);
                           unsigned char* d = a + (t % 26) * 24 + (t / 26) * 24 * 640;
                           for (int y = 0; y < 24; ++y) {
                               fn(d + y * 640, s + y * 24, 24);
                           }
                       }
                   })
                   / 100;
        };
        Pair(what, rows(memcpy), rows(vc_memcpy), 576);
        snprintf(what, sizeof(what), "VC_Stamp_Rows_24, tile set +%d", so);
        One(what,
            Time([&] {
                for (int t = 0; t < 100; ++t) {
                    const unsigned char* s = b + so + ((tile++ * 576) & 0xFFFF);
                    unsigned char* d = a + (t % 26) * 24 + (t / 26) * 24 * 640;
                    VC_Stamp_Rows_24(d, s, 640, 24);
                }
            }) / 100,
            576);
    }
    {
        unsigned tile = 0;
        Pair("memcpy per row, destination +1 (scrolled by a pixel)",
             Time([&] {
                 for (int t = 0; t < 100; ++t) {
                     const unsigned char* s = b + ((tile++ * 576) & 0xFFFF);
                     unsigned char* d = a + 1 + (t % 26) * 24 + (t / 26) * 24 * 640;
                     for (int y = 0; y < 24; ++y) {
                         memcpy(d + y * 640, s + y * 24, 24);
                     }
                 }
             }) / 100,
             Time([&] {
                 for (int t = 0; t < 100; ++t) {
                     const unsigned char* s = b + ((tile++ * 576) & 0xFFFF);
                     unsigned char* d = a + 1 + (t % 26) * 24 + (t / 26) * 24 * 640;
                     for (int y = 0; y < 24; ++y) {
                         vc_memcpy(d + y * 640, s + y * 24, 24);
                     }
                 }
             }) / 100,
             576);
        for (int dof = 1; dof < 4; ++dof) {
            char what[80];
            snprintf(what, sizeof(what), "VC_Stamp_Rows_24, destination +%d, tile set +1", dof);
            One(what,
                Time([&] {
                    for (int t = 0; t < 100; ++t) {
                        const unsigned char* s = b + 1 + ((tile++ * 576) & 0xFFFF);
                        unsigned char* d = a + dof + (t % 26) * 24 + (t / 26) * 24 * 640;
                        VC_Stamp_Rows_24(d, s, 640, 24);
                    }
                }) / 100,
                576);
        }
    }

    printf("-- a frame's rows in main memory, 1050 rows (per frame)\n");
    {
        const unsigned widths[] = {640, 1024, 1680};
        for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
            unsigned w = widths[i], rows = BIG / w < 1050 ? BIG / w : 1050;
            char what[80];
            snprintf(what, sizeof(what), "rows of %u: memcpy | VC_Copy_Span (%u rows)", w, rows);
            Pair(what,
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         memcpy(a + y * w, b + y * w, w);
                     }
                 }),
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         VC_Copy_Span(a + y * w, b + y * w, w);
                     }
                 }),
                 double(rows) * w);
            snprintf(what, sizeof(what), "rows of %u: our memcpy | VC_Copy_Span", w);
            Pair(what,
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         vc_memcpy(a + y * w, b + y * w, w);
                     }
                 }),
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         VC_Copy_Span(a + y * w, b + y * w, w);
                     }
                 }),
                 double(rows) * w);
        }
    }

    if (screen) {
        _kernel_swi_regs r;
        int in[4] = {148, 150, 6, -1}; // ScreenStart, TotalScreenSize, LineLength
        int out[3];
        r.r[0] = reinterpret_cast<int>(in);
        r.r[1] = reinterpret_cast<int>(out);
        _kernel_swi(OS_ReadVduVariables, &r, &r);
        unsigned char* scr = reinterpret_cast<unsigned char*>(out[0]);
        unsigned size = unsigned(out[1]), line = unsigned(out[2]);
        unsigned n = size < BIG ? size & ~31u : BIG;
        memcpy(b, scr, n); // the screen as it is, written back by every test
        printf("-- screen memory: %u KB of the desktop (line %u bytes), written with what it shows\n", n / 1024, line);
        Pair("memcpy, main memory to the screen, in one", Time([&] { memcpy(scr, b, n); }), Time([&] {
                 vc_memcpy(scr, b, n);
             }),
             n);
        // A present: rows of w bytes at the screen's line length, as many as fit.
        const unsigned widths[] = {640, 1024, 1680, 1664};
        for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
            unsigned w = widths[i];
            if (w > line) {
                continue;
            }
            unsigned rows = n / line;
            char what[80];
            snprintf(what, sizeof(what), "present of %u x %u: UnixLib memcpy | VC_Copy_Span", w, rows);
            Pair(what,
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         memcpy(scr + y * line, b + y * line, w);
                     }
                 }),
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         VC_Copy_Span(scr + y * line, b + y * line, w);
                     }
                 }),
                 double(rows) * w);
            snprintf(what, sizeof(what), "present of %u x %u: our memcpy | VC_Copy_Span", w, rows);
            Pair(what,
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         vc_memcpy(scr + y * line, b + y * line, w);
                     }
                 }),
                 Time([&] {
                     for (unsigned y = 0; y < rows; ++y) {
                         VC_Copy_Span(scr + y * line, b + y * line, w);
                     }
                 }),
                 double(rows) * w);
            if ((w & 31) == 0) {
                snprintf(what, sizeof(what), "present of %u x %u: old Copy_32 | VC_Copy_Span", w, rows);
                Pair(what,
                     Time([&] {
                         for (unsigned y = 0; y < rows; ++y) {
                             Copy_32(scr + y * line, b + y * line, w);
                         }
                     }),
                     Time([&] {
                         for (unsigned y = 0; y < rows; ++y) {
                             VC_Copy_Span(scr + y * line, b + y * line, w);
                         }
                     }),
                     double(rows) * w);
            }
        }
        // Partial presents: spans of 16-pixel tiles, 16 rows each, across the screen.
        const unsigned spans[] = {16, 48, 80, 208};
        for (unsigned i = 0; i < sizeof(spans) / sizeof(spans[0]); ++i) {
            unsigned w = spans[i], rows = n / line, count = 0;
            char what[80];
            snprintf(what, sizeof(what), "partial spans %u wide x 16 rows: UnixLib | Span", w);
            auto run = [&](int kind) {
                return Time([&] {
                    count = 0;
                    for (unsigned y = 0; y + 16 <= rows; y += 32) {
                        for (unsigned x = 0; x + w <= line && x + w <= 1680; x += 2 * w + 16) {
                            for (unsigned k = 0; k < 16; ++k) {
                                unsigned o = (y + k) * line + x;
                                if (kind == 0) {
                                    memcpy(scr + o, b + o, w);
                                } else if (kind == 1) {
                                    vc_memcpy(scr + o, b + o, w);
                                } else {
                                    VC_Copy_Span(scr + o, b + o, w);
                                }
                            }
                            ++count;
                        }
                    }
                });
            };
            double t0 = run(0), t1 = run(1), t2 = run(2);
            Pair(what, t0, t2, double(count) * w * 16);
            snprintf(what, sizeof(what), "partial spans %u wide x 16 rows: ours | Span", w);
            Pair(what, t1, t2, double(count) * w * 16);
        }
        memcpy(scr, b, n);
    }
    return 0;
}
