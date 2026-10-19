/*
** scaletest: checks the faster pixel doubling (VC_Double_Row, common/riscos_memory.h, used
** for 320x200 on a screen with no mode that size) and movie interpolation (common/winasm.cpp)
** against the code they replace, copied below as it was, and times both.
** - Doubling: every source alignment 0-3 x destination alignment 0-3 (both rows) x widths
**   0-80 and 320, with guard bytes round every row.
** - Interpolation: Interpolate_X_Axis and Interpolate_Y_Axis at every alignment and widths
**   0-40, 320, 400 and 800, then the four interpolating scalers (modes 0-2 and the band of
**   rows) over whole frames of movie-like pictures (runs of a colour, rows like the one
**   above) and noise, with random interpolation tables. Each frame's output is hashed.
** - The movie clock: VQA_Ticks_From_Cs (common/vqaclock.h) against 60 * cs * 10 / 1000 in
**   64 bits, over the whole range of the centisecond counter.
**
**   scaletest [-quick]       prints PASS or the first failures, then the timings
*/
#define VC_MEM_PREFIX "vc_"
#include "../../common/riscos_memory.cpp"
#include "../../common/riscos_memory.h"
#include "../../common/vqaclock.h"
#include "../../common/winasm.cpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#endif

/*
** The code before the speed-ups, as it was (winasm.cpp and video_sdl1.cpp's Scale_Rect).
*/
static void Old_Interpolate_X_Axis(void* src, void* dst, int src_width)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* sptr = (unsigned char*)(src);

    unsigned char* wptr = dptr;

    for (int i = 0; i < src_width - 1; ++i) {
        unsigned char a = sptr[0], b = sptr[1];
        *wptr++ = a;
        *wptr++ = a == b ? a : InterpolationTable->PaletteInterpolationTable[a][b];
        ++sptr;
    }

    *wptr++ = *sptr;
    *wptr = 0;
}

static void Old_Interpolate_Y_Axis(void* top_line, void* bottom_line, void* middle_line, int src_width)
{
    unsigned char* tlp = (unsigned char*)(top_line);
    unsigned char* mlp = (unsigned char*)(middle_line);
    unsigned char* blp = (unsigned char*)(bottom_line);
    int dst_width = 2 * src_width;

    for (int i = 0; i < dst_width; ++i) {
        unsigned char a = *tlp++, b = *blp++;
        *mlp++ = a == b ? a : InterpolationTable->PaletteInterpolationTable[a][b];
    }
}

static void Old_Asm_Interpolate(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* sptr = (unsigned char*)(src);

    while (src_height--) {
        Old_Interpolate_X_Axis(sptr, InterpolationTable->LineBuffer, src_width);
        memcpy(dptr, InterpolationTable->LineBuffer, 2 * src_width);
        sptr += src_width;
        dptr += dst_pitch;
    }
}

static void Old_Asm_Interpolate_Line_Double(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* sptr = (unsigned char*)(src);

    while (src_height--) {
        Old_Interpolate_X_Axis(sptr, InterpolationTable->LineBuffer, src_width);
        memcpy(dptr, InterpolationTable->LineBuffer, 2 * src_width);
        memcpy(dptr + dst_pitch / 2, InterpolationTable->LineBuffer, 2 * src_width);
        sptr += src_width;
        dptr += dst_pitch;
    }
}

static void Old_Asm_Interpolate_Line_Interpolate(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* buff_offset1 = InterpolationTable->TopLine;
    unsigned char* buff_offset2 = InterpolationTable->BottomLine;

    int pitch = dst_pitch / 2;
    Old_Interpolate_X_Axis(src, buff_offset1, src_width);

    unsigned char* tmp = buff_offset1;
    buff_offset1 = buff_offset2;
    buff_offset2 = tmp;

    unsigned char* current_line = (unsigned char*)(src) + src_width;
    int lines = src_height - 1;

    while (lines--) {
        Old_Interpolate_X_Axis(current_line, buff_offset1, src_width);
        Old_Interpolate_Y_Axis(buff_offset2, buff_offset1, InterpolationTable->LineBuffer, src_width);
        memcpy(dptr, buff_offset2, 2 * src_width);
        dptr += pitch;
        memcpy(dptr, InterpolationTable->LineBuffer, 2 * src_width);
        current_line += src_width;
        dptr += pitch;

        unsigned char* tmp = buff_offset1;
        buff_offset1 = buff_offset2;
        buff_offset2 = tmp;
    }

    memcpy(dptr, buff_offset2, 2 * src_width);
    dptr += pitch;
}

static void Old_Asm_Interpolate_Line_Interpolate_Rows(
    void* src, void* dst, int src_height, int src_width, int dst_pitch, int first, int last)
{
    unsigned char* top = InterpolationTable->TopLine;
    unsigned char* bottom = InterpolationTable->BottomLine;
    unsigned char* sptr = (unsigned char*)(src);
    int pitch = dst_pitch / 2;

    Old_Interpolate_X_Axis(sptr + first * src_width, top, src_width);

    for (int i = first; i <= last; ++i) {
        unsigned char* dptr = (unsigned char*)(dst) + 2 * i * pitch;
        memcpy(dptr, top, 2 * src_width);
        if (i + 1 < src_height) {
            Old_Interpolate_X_Axis(sptr + (i + 1) * src_width, bottom, src_width);
            Old_Interpolate_Y_Axis(top, bottom, InterpolationTable->LineBuffer, src_width);
            memcpy(dptr + pitch, InterpolationTable->LineBuffer, 2 * src_width);
            unsigned char* tmp = top;
            top = bottom;
            bottom = tmp;
        }
    }
}

// Scale_Rect's inner loop: one source row to two destination rows.
static void Old_Double_Row(unsigned char* to, int pitch, const unsigned char* from, int w)
{
    for (int x = 0; x < w; ++x) {
        to[2 * x] = to[2 * x + 1] = from[x];
    }
    memcpy(to + pitch, to, size_t(2 * w));
}

static void New_Double_Row(unsigned char* to, int pitch, const unsigned char* from, int w)
{
#if defined(__riscos__) && defined(__arm__)
    VC_Double_Row(to, to + pitch, from, w);
#else
    Old_Double_Row(to, pitch, from, w);
#endif
}

static unsigned Seed = 12345;
static unsigned Rand()
{
    Seed = Seed * 1103515245u + 12345u;
    return Seed >> 8;
}

static unsigned long Cases = 0, Failures = 0;
static const char* Section = "";

static void Fail(const char* what, int a, int b, int n, long at)
{
    ++Failures;
    if (Failures <= 20) {
        printf("FAIL %s %s: alignments %d/%d, width %d, first difference at byte %ld\n", Section, what, a, b, n, at);
    }
}

static long First_Difference(const unsigned char* x, const unsigned char* y, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) {
            return long(i);
        }
    }
    return -1;
}

static unsigned Hash_Of(const unsigned char* p, size_t n, unsigned h = 2166136261u)
{
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static void Random_Table()
{
    unsigned char* t = &InterpolationTable->PaletteInterpolationTable[0][0];
    for (int i = 0; i < 65536; ++i) {
        t[i] = (unsigned char)Rand();
    }
}

/*
** A movie-like picture: runs of a colour 1-24 long, each row mostly the row above (as most
** of a movie frame is), some all one colour; or noise ("noise" true).
*/
static void Make_Picture(unsigned char* p, int w, int h, bool noise)
{
    for (int y = 0; y < h; ++y) {
        unsigned char* row = p + size_t(y) * w;
        if (noise) {
            for (int x = 0; x < w; ++x) {
                row[x] = (unsigned char)Rand();
            }
            continue;
        }
        if (y > 0 && Rand() % 3 != 0) {
            memcpy(row, row - w, w);
            for (int k = Rand() % 6; k > 0; --k) {
                int x = int(Rand() % unsigned(w));
                int n = 1 + int(Rand() % 12);
                unsigned char c = (unsigned char)Rand();
                for (; n > 0 && x < w; --n, ++x) {
                    row[x] = c;
                }
            }
            continue;
        }
        if (Rand() % 8 == 0) {
            memset(row, int(Rand() & 255), w);
            continue;
        }
        for (int x = 0; x < w;) {
            int n = 1 + int(Rand() % 24);
            unsigned char c = (unsigned char)(Rand() % 32); // few colours, as in a dark scene
            for (; n > 0 && x < w; --n, ++x) {
                row[x] = c;
            }
        }
    }
}

enum
{
    GUARD = 16
};

static void Test_Doubling(bool quick)
{
    Section = "doubling";
    int widths[100], nw = 0;
    for (int w = 0; w <= 80; ++w) {
        widths[nw++] = w;
    }
    widths[nw++] = 319;
    widths[nw++] = 320;
    const int pitch = 2 * 336 + 8; // a multiple of 4, like a screen's line length
    std::vector<unsigned char> src(400 + 2 * GUARD), want(2 * pitch + 2 * GUARD), got(2 * pitch + 2 * GUARD);
    for (int i = 0; i < nw; ++i) {
        int w = widths[i];
        for (int sa = 0; sa < 4; ++sa) {
            for (int da = 0; da < 4; ++da) {
                if (quick && w > 40 && w < 300 && (sa | da) != 0) {
                    continue;
                }
                for (size_t k = 0; k < src.size(); ++k) {
                    src[k] = (unsigned char)Rand();
                }
                for (size_t k = 0; k < want.size(); ++k) {
                    want[k] = got[k] = (unsigned char)Rand();
                }
                // Aligned buffers from vector are at least 8-byte aligned.
                unsigned char* s = &src[GUARD + sa];
                Old_Double_Row(&want[GUARD + da], pitch, s, w);
                New_Double_Row(&got[GUARD + da], pitch, s, w);
                ++Cases;
                long at = First_Difference(&want[0], &got[0], want.size());
                if (at >= 0) {
                    Fail("row", sa, da, w, at - GUARD - da);
                }
            }
        }
    }
    // Rows at different alignments from each other (a pitch that isn't a multiple of 4).
    for (int odd = 1; odd < 4; ++odd) {
        for (int w = 0; w < 40; ++w) {
            for (size_t k = 0; k < want.size(); ++k) {
                want[k] = got[k] = (unsigned char)Rand();
            }
            Old_Double_Row(&want[GUARD], pitch + odd, &src[GUARD], w);
            New_Double_Row(&got[GUARD], pitch + odd, &src[GUARD], w);
            ++Cases;
            long at = First_Difference(&want[0], &got[0], want.size());
            if (at >= 0) {
                Fail("odd pitch", 0, odd, w, at - GUARD);
            }
        }
    }
}

static void Test_Axes(bool quick)
{
    Section = "axes";
    int widths[64], nw = 0;
    for (int w = 0; w <= 40; ++w) {
        widths[nw++] = w;
    }
    widths[nw++] = 320;
    widths[nw++] = 400;
    widths[nw++] = 799;
    widths[nw++] = 800;
    std::vector<unsigned char> src(1700), top(1700), bottom(1700), want(1700), got(1700);
    for (int i = 0; i < nw; ++i) {
        int w = widths[i];
        for (int pass = 0; pass < (quick ? 1 : 3); ++pass) {
            Make_Picture(&src[GUARD], w > 0 ? w : 1, 1, pass == 2);
            if (pass == 1) {
                memset(&src[GUARD], 7, w + 4); // all one colour: the fast path, and its edges
                if (w > 5) {
                    src[GUARD + w - 1] = 9;
                }
            }
            for (int sa = 0; sa < 4; ++sa) {
                for (int da = 0; da < 4; ++da) {
                    // X axis: dst 2w bytes
                    for (size_t k = 0; k < want.size(); ++k) {
                        want[k] = got[k] = (unsigned char)Rand();
                    }
                    Old_Interpolate_X_Axis(&src[GUARD + sa], &want[GUARD + da], w);
                    Interpolate_X_Axis(&src[GUARD + sa], &got[GUARD + da], w);
                    ++Cases;
                    long at = First_Difference(&want[0], &got[0], want.size());
                    if (at >= 0) {
                        Fail("X", sa, da, w, at - GUARD - da);
                    }
                    // Y axis: lines of 2w bytes; top at sa, bottom at da, middle at (sa + da) & 3
                    int ma = (sa + da + pass) & 3;
                    for (int k = 0; k < 2 * w; ++k) {
                        unsigned char c = (unsigned char)Rand();
                        top[GUARD + sa + k] = c;
                        bottom[GUARD + da + k] = Rand() % 3 == 0 ? (unsigned char)Rand() : c;
                    }
                    for (size_t k = 0; k < want.size(); ++k) {
                        want[k] = got[k] = (unsigned char)Rand();
                    }
                    Old_Interpolate_Y_Axis(&top[GUARD + sa], &bottom[GUARD + da], &want[GUARD + ma], w);
                    Interpolate_Y_Axis(&top[GUARD + sa], &bottom[GUARD + da], &got[GUARD + ma], w);
                    ++Cases;
                    at = First_Difference(&want[0], &got[0], want.size());
                    if (at >= 0) {
                        Fail("Y", sa, da, w, at - GUARD - ma);
                    }
                }
            }
        }
    }
}

/*
** Whole frames through each scaler, old and new, into separate destinations; prints a hash
** of each mode's output over all frames (the same for old and new when it passes).
*/
static void Test_Frames(bool quick)
{
    Section = "frames";
    static const int sizes[][2] = {{320, 200}, {320, 156}, {316, 100}, {33, 17}, {800, 3}, {2, 2}, {1, 5}};
    unsigned hashes[5] = {2166136261u, 2166136261u, 2166136261u, 2166136261u, 2166136261u};
    int frames = quick ? 4 : 24;
    for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
        int w = sizes[si][0], h = sizes[si][1];
        int pitch = 2 * 2 * w + 2 * 16; // dst_pitch as interpal.cpp passes it: two rows' worth
        std::vector<unsigned char> src(size_t(w) * h + 8), a(size_t(pitch) * h + 64), b(a.size());
        for (int f = 0; f < frames; ++f) {
            if (f % 8 == 0) {
                Random_Table();
            }
            Make_Picture(&src[0], w, h, f % 6 == 5);
            for (int mode = 0; mode < 4; ++mode) {
                memset(&a[0], 0x5A, a.size());
                memset(&b[0], 0x5A, b.size());
                int first = 0, last = h - 1;
                switch (mode) {
                case 0:
                    Old_Asm_Interpolate(&src[0], &a[0], h, w, pitch);
                    Asm_Interpolate(&src[0], &b[0], h, w, pitch);
                    break;
                case 1:
                    Old_Asm_Interpolate_Line_Double(&src[0], &a[0], h, w, pitch);
                    Asm_Interpolate_Line_Double(&src[0], &b[0], h, w, pitch);
                    break;
                case 2:
                    Old_Asm_Interpolate_Line_Interpolate(&src[0], &a[0], h, w, pitch);
                    Asm_Interpolate_Line_Interpolate(&src[0], &b[0], h, w, pitch);
                    break;
                case 3:
                    first = int(Rand() % unsigned(h));
                    last = first + int(Rand() % unsigned(h - first));
                    Old_Asm_Interpolate_Line_Interpolate_Rows(&src[0], &a[0], h, w, pitch, first, last);
                    Asm_Interpolate_Line_Interpolate_Rows(&src[0], &b[0], h, w, pitch, first, last);
                    break;
                }
                ++Cases;
                long at = First_Difference(&a[0], &b[0], a.size());
                if (at >= 0) {
                    char what[32];
                    snprintf(what, sizeof(what), "mode %d", mode == 3 ? 4 : mode);
                    Fail(what, f, int(si), w, at);
                }
                hashes[mode] = Hash_Of(&b[0], b.size(), hashes[mode]);
            }
        }
    }
    printf("scaletest: frame hashes: mode 0 %08x, mode 1 %08x, mode 2 %08x, rows %08x\n",
           hashes[0], hashes[1], hashes[2], hashes[3]);
}

static void Test_Clock()
{
    Section = "clock";
    // Every value near the edges and multiples of 5, and a sweep over the whole range.
    for (unsigned long long cs = 0; cs <= 0xFFFFFFFFull; cs += (cs < 100000 ? 1 : 99991)) {
        unsigned want = unsigned(60ull * (cs * 10) / 1000);
        unsigned got = VQA_Ticks_From_Cs(unsigned(cs));
        ++Cases;
        if (want != got) {
            Fail("ticks", 0, 0, int(cs), long(got));
        }
    }
    for (unsigned d = 0; d < 1000; ++d) {
        unsigned cs = 0xFFFFFFFFu - d;
        unsigned want = unsigned(60ull * (cs * 10ull) / 1000);
        ++Cases;
        if (VQA_Ticks_From_Cs(cs) != want) {
            Fail("ticks top", 0, 0, int(d), long(VQA_Ticks_From_Cs(cs)));
        }
    }
}

static unsigned Now_Cs()
{
#ifdef __riscos__
    _kernel_swi_regs regs;
    _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
    return unsigned(regs.r[0]);
#else
    return unsigned(clock() / (CLOCKS_PER_SEC / 100));
#endif
}

// Microseconds per call of fn, run for about a second.
template <typename F> static double Time_It(F fn)
{
    unsigned n = 0, start = Now_Cs(), now;
    while (Now_Cs() == start) {
    }
    start = Now_Cs();
    do {
        fn();
        ++n;
        now = Now_Cs();
    } while (now - start < 100);
    return (now - start) * 10000.0 / n;
}

static void Timings()
{
    const int w = 320, h = 200, pitch = 4 * w;
    std::vector<unsigned char> src(size_t(w) * h), dst(size_t(pitch) * h + 64), screen(size_t(2 * w) * 2 * h + 64);
    Random_Table();
    Make_Picture(&src[0], w, h, false);
    unsigned char* s = &src[0];
    unsigned char* d = &dst[0];
    printf("scaletest: timings, a 320x200 movie-like frame, ms per frame (old -> new):\n");
    double o, n;
    o = Time_It([&] { Old_Asm_Interpolate_Line_Double(s, d, h, w, pitch); });
    n = Time_It([&] { Asm_Interpolate_Line_Double(s, d, h, w, pitch); });
    printf("  mode 1 (blend across, double lines): %.2f -> %.2f\n", o / 1000, n / 1000);
    o = Time_It([&] { Old_Asm_Interpolate_Line_Interpolate(s, d, h, w, pitch); });
    n = Time_It([&] { Asm_Interpolate_Line_Interpolate(s, d, h, w, pitch); });
    printf("  mode 2 (blend both ways):            %.2f -> %.2f\n", o / 1000, n / 1000);
    Make_Picture(&src[0], w, h, true);
    o = Time_It([&] { Old_Asm_Interpolate_Line_Interpolate(s, d, h, w, pitch); });
    n = Time_It([&] { Asm_Interpolate_Line_Interpolate(s, d, h, w, pitch); });
    printf("  mode 2, noise (every pixel looked up): %.2f -> %.2f\n", o / 1000, n / 1000);
    unsigned char* sc = &screen[0];
    o = Time_It([&] {
        for (int y = 0; y < h; ++y) {
            Old_Double_Row(sc + size_t(2 * y) * 2 * w, 2 * w, s + size_t(y) * w, w);
        }
    });
    n = Time_It([&] {
        for (int y = 0; y < h; ++y) {
            New_Double_Row(sc + size_t(2 * y) * 2 * w, 2 * w, s + size_t(y) * w, w);
        }
    });
    printf("  320x200 doubled to 640x400 in memory: %.2f -> %.2f\n", o / 1000, n / 1000);
}

int main(int argc, char** argv)
{
    bool quick = argc > 1 && strcmp(argv[1], "-quick") == 0;
    InterpolationTable = new struct InterpolationTable();
    Random_Table();
    Test_Doubling(quick);
    Test_Axes(quick);
    Test_Frames(quick);
    Test_Clock();
    printf("scaletest: %lu cases, %lu failures\n", Cases, Failures);
    printf("scaletest: %s\n", Failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    if (!quick) {
        Timings();
    }
    printf("scaletest: done\n");
    return Failures == 0 ? 0 : 1;
}
