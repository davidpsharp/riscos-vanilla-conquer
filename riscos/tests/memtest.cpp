/*
** memtest: checks the program's memcpy, memmove, memset, bcopy and bzero
** (common/riscos_memory.cpp, built here as vc_memcpy and so on, beside UnixLib's) and
** the copies in common/riscos_memory.h, against byte loops:
** - every source alignment 0-3 x destination alignment 0-3 x length 0-300, then 64 KB
**   and 1 MB (and a few lengths round them);
** - memmove of overlapping areas, both directions, at every distance 1-68, every
**   alignment, lengths 0-300, and large ones;
** - memset with every fill value (also passed with junk in the upper bits), every
**   alignment, lengths 0-300, and large ones;
** - random cases up to 4100 bytes, also compared with UnixLib's routines;
** - VC_Copy_Span and VC_Stamp_Rows_24.
** Every case has guard bytes before and after (and checks the source is unchanged), and
** checks the return value.
**
**   memtest [-quick]      prints PASS or the first failures; exit status 0 if all pass
*/
#define VC_MEM_PREFIX "vc_"
#include "../../common/riscos_memory.cpp"
#include "../../common/riscos_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#endif

extern "C" {
void* vc_memcpy(void* dst, const void* src, size_t n);
void* vc_memmove(void* dst, const void* src, size_t n);
void* vc_memset(void* dst, int c, size_t n);
void vc_bcopy(const void* src, void* dst, size_t n);
void vc_bzero(void* dst, size_t n);
}

static unsigned Seed = 12345;
static unsigned Rand()
{
    Seed = Seed * 1103515245u + 12345u;
    return Seed >> 8;
}

static unsigned long Cases = 0, Failures = 0;
static const char* Section = "";

static void Fail(const char* what, unsigned sa, unsigned da, unsigned n, long at, const char* how)
{
    ++Failures;
    if (Failures <= 20) {
        printf("FAIL %s: %s src align %u dst align %u length %u: %s at %ld\n", Section, what, sa, da, n, how, at);
        fflush(stdout);
    }
}

// Byte loops (volatile, so the compiler can't turn them into library calls).
static void Ref_Copy(unsigned char* d, const unsigned char* s, size_t n)
{
    volatile unsigned char* vd = d;
    for (size_t i = 0; i < n; ++i) {
        vd[i] = s[i];
    }
}

static void Ref_Fill(unsigned char* d, int c, size_t n)
{
    volatile unsigned char* vd = d;
    for (size_t i = 0; i < n; ++i) {
        vd[i] = (unsigned char)c;
    }
}

static void Random_Fill(unsigned char* d, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        d[i] = (unsigned char)Rand();
    }
}

// Compares n bytes; returns the first difference or -1.
static long Diff(const unsigned char* a, const unsigned char* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return long(i);
        }
    }
    return -1;
}

static unsigned Now_Cs()
{
#ifdef __riscos__
    _kernel_swi_regs r;
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    return unsigned(r.r[0]);
#else
    return unsigned(clock() * 100 / CLOCKS_PER_SEC);
#endif
}

enum
{
    GUARD = 64
};

typedef void* (*Copy_Fn)(void*, const void*, size_t);

static void* Bcopy_As_Copy(void* d, const void* s, size_t n)
{
    vc_bcopy(s, d, n);
    return d;
}

static void* Span_As_Copy(void* d, const void* s, size_t n)
{
    VC_Copy_Span(d, s, unsigned(n));
    return d;
}

/*
** One non-overlapping copy: src and dst are separate buffers of at least
** n + 2 * GUARD + 4 bytes; the copy goes to offset GUARD + da from GUARD + sa.
*/
static void Copy_Case(const char* what,
                      Copy_Fn fn,
                      bool returns,
                      unsigned char* src,
                      unsigned char* dst,
                      unsigned char* expect,
                      unsigned char* src_copy,
                      unsigned sa,
                      unsigned da,
                      size_t n)
{
    size_t total = n + 2 * GUARD + 4;
    Random_Fill(src, total);
    Random_Fill(dst, total);
    Ref_Copy(src_copy, src, total);
    Ref_Copy(expect, dst, total);
    Ref_Copy(expect + GUARD + da, src + GUARD + sa, n);
    void* r = fn(dst + GUARD + da, src + GUARD + sa, n);
    ++Cases;
    if (returns && r != dst + GUARD + da) {
        Fail(what, sa, da, unsigned(n), 0, "wrong return value");
    }
    long at = Diff(dst, expect, total);
    if (at >= 0) {
        const char* how = at < long(GUARD + da) ? "wrote before the destination"
                          : at >= long(GUARD + da + n) ? "wrote after the destination"
                                                       : "wrong byte";
        Fail(what, sa, da, unsigned(n), at - long(GUARD + da), how);
    }
    at = Diff(src, src_copy, total);
    if (at >= 0) {
        Fail(what, sa, da, unsigned(n), at, "changed the source");
    }
}

/*
** One overlapping memmove inside buf: from offset base to base + delta (delta may be
** negative, base >= GUARD - delta), n bytes; everything up to GUARD bytes past the
** furthest byte either could touch is checked.
*/
static void Move_Case(const char* what,
                      Copy_Fn fn,
                      unsigned char* buf,
                      unsigned char* expect,
                      unsigned char* temp,
                      unsigned base,
                      long delta,
                      size_t n)
{
    size_t total = base + size_t(delta < 0 ? -delta : delta) + n + GUARD;
    Random_Fill(buf, total);
    Ref_Copy(expect, buf, total);
    Ref_Copy(temp, buf + base, n);
    Ref_Copy(expect + base + delta, temp, n);
    void* r = fn(buf + base + delta, buf + base, n);
    ++Cases;
    unsigned sa = base & 3, da = unsigned(base + delta) & 3;
    if (r != buf + base + delta) {
        Fail(what, sa, da, unsigned(n), delta, "wrong return value (at = distance)");
    }
    long at = Diff(buf, expect, total);
    if (at >= 0) {
        char how[64];
        snprintf(how, sizeof(how), "distance %ld: wrong byte", delta);
        Fail(what, sa, da, unsigned(n), at - long(base + delta), how);
    }
}

static void Fill_Case(const char* what,
                      bool zero,
                      unsigned char* dst,
                      unsigned char* expect,
                      unsigned da,
                      int c,
                      size_t n,
                      bool fresh)
{
    size_t total = n + 2 * GUARD + 4;
    if (fresh) {
        // enough for the cases that follow without a refill, too (up to 300 bytes)
        size_t fill = total > 512 ? total : 512;
        Random_Fill(dst, fill);
        Ref_Copy(expect, dst, fill);
    }
    Ref_Fill(expect + GUARD + da, zero ? 0 : c, n);
    void* r;
    if (zero) {
        vc_bzero(dst + GUARD + da, n);
        r = dst + GUARD + da;
    } else {
        r = vc_memset(dst + GUARD + da, c, n);
    }
    ++Cases;
    if (r != dst + GUARD + da) {
        Fail(what, 0, da, unsigned(n), c, "wrong return value (at = value)");
    }
    long at = Diff(dst, expect, total);
    if (at >= 0) {
        char how[64];
        snprintf(how, sizeof(how), "value %d (0x%x): %s", c, unsigned(c), at < long(GUARD + da) ? "wrote before"
                                                                         : at >= long(GUARD + da + n) ? "wrote after"
                                                                                                       : "wrong byte");
        Fail(what, 0, da, unsigned(n), at - long(GUARD + da), how);
    }
}

static void Report(unsigned start)
{
    printf("  %-44s %8lu cases, %4lu failures so far, %5.1f s\n",
           Section,
           Cases,
           Failures,
           (Now_Cs() - start) / 100.0);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    bool quick = argc > 1 && strcmp(argv[1], "-quick") == 0;
    const size_t BIG = 1024 * 1024;
    const size_t SIZE = BIG + 4 * 1024;
    unsigned char* a = static_cast<unsigned char*>(malloc(SIZE));
    unsigned char* b = static_cast<unsigned char*>(malloc(SIZE));
    unsigned char* e = static_cast<unsigned char*>(malloc(SIZE));
    unsigned char* t = static_cast<unsigned char*>(malloc(SIZE));
    if (!a || !b || !e || !t) {
        printf("memtest: not enough memory (needs about 4.2 MB)\n");
        return 2;
    }
    // Word-align the buffers, so "alignment" below is the real alignment.
    a = reinterpret_cast<unsigned char*>((reinterpret_cast<uintptr_t>(a) + 31) & ~uintptr_t(31));
    b = reinterpret_cast<unsigned char*>((reinterpret_cast<uintptr_t>(b) + 31) & ~uintptr_t(31));
    e = reinterpret_cast<unsigned char*>((reinterpret_cast<uintptr_t>(e) + 31) & ~uintptr_t(31));
    t = reinterpret_cast<unsigned char*>((reinterpret_cast<uintptr_t>(t) + 31) & ~uintptr_t(31));
    unsigned all = Now_Cs(), start;
    printf("memtest%s\n", quick ? " -quick" : "");

    struct
    {
        const char* name;
        Copy_Fn fn;
        bool returns;
    } copies[] = {{"memcpy", vc_memcpy, true},
                  {"memmove", vc_memmove, true},
                  {"bcopy", Bcopy_As_Copy, false},
                  {"VC_Copy_Span", Span_As_Copy, false}};
    const size_t SMALL = 300;
    for (unsigned f = 0; f < sizeof(copies) / sizeof(copies[0]); ++f) {
        Section = copies[f].name;
        start = Now_Cs();
        if (quick && f == 2) {
            continue;
        }
        for (unsigned sa = 0; sa < 4; ++sa) {
            for (unsigned da = 0; da < 4; ++da) {
                for (size_t n = 0; n <= SMALL; ++n) {
                    Copy_Case(copies[f].name, copies[f].fn, copies[f].returns, a, b, e, t, sa, da, n);
                }
            }
        }
        Report(start);
    }

    Section = "memcpy and memmove, large";
    start = Now_Cs();
    {
        const size_t larges[] = {4095, 4096, 4097, 65535, 65536, 65537, 65536 + 63, BIG - 3, BIG};
        for (unsigned l = 0; l < sizeof(larges) / sizeof(larges[0]); ++l) {
            for (unsigned sa = 0; sa < 4; ++sa) {
                for (unsigned da = 0; da < 4; ++da) {
                    if (quick && larges[l] >= BIG - 3 && (sa != da || sa != 0) && sa != 1) {
                        continue;
                    }
                    Copy_Case("memcpy", vc_memcpy, true, a, b, e, t, sa, da, larges[l]);
                    if (larges[l] < BIG - 3) {
                        Copy_Case("memmove", vc_memmove, true, a, b, e, t, sa, da, larges[l]);
                        Copy_Case("VC_Copy_Span", Span_As_Copy, false, a, b, e, t, sa, da, larges[l]);
                    }
                }
            }
        }
    }
    Report(start);

    Section = "memmove, overlapping, lengths 0-300";
    start = Now_Cs();
    {
        const long MAXD = quick ? 12 : 68;
        for (unsigned base4 = 0; base4 < 4; ++base4) {
            unsigned base = GUARD + 72 + base4; // room below for a negative distance
            for (long d = 1; d <= MAXD; ++d) {
                for (size_t n = 0; n <= SMALL; ++n) {
                    Move_Case("memmove up", vc_memmove, a, e, t, base, d, n);
                    Move_Case("memmove down", vc_memmove, a, e, t, base, -d, n);
                    if (d <= 8 && n % 7 == 0) {
                        Move_Case("memcpy up (as memmove)", vc_memcpy, a, e, t, base, d, n);
                        Move_Case("memcpy down (as memmove)", vc_memcpy, a, e, t, base, -d, n);
                    }
                }
            }
        }
    }
    Report(start);

    Section = "memmove, overlapping, large";
    start = Now_Cs();
    {
        const size_t larges[] = {1000, 4096 + 3, 65536, 65536 + 5};
        const long ds[] = {1, 2, 3, 4, 5, 7, 31, 32, 33, 63, 64, 65, 999, 4096, 40000};
        for (unsigned l = 0; l < sizeof(larges) / sizeof(larges[0]); ++l) {
            for (unsigned i = 0; i < sizeof(ds) / sizeof(ds[0]); ++i) {
                for (unsigned base4 = 0; base4 < 4; ++base4) {
                    unsigned base = GUARD + 40000 + base4;
                    Move_Case("memmove up", vc_memmove, a, e, t, base, ds[i], larges[l]);
                    Move_Case("memmove down", vc_memmove, a, e, t, base, -ds[i], larges[l]);
                }
            }
        }
        // And 1 MB, a few distances.
        const long bigd[] = {1, 3, 4, 32, 1001};
        for (unsigned i = 0; i < sizeof(bigd) / sizeof(bigd[0]); ++i) {
            Move_Case("memmove up", vc_memmove, a, e, t, GUARD + 1024 + 1, bigd[i], BIG - 4096);
            Move_Case("memmove down", vc_memmove, a, e, t, GUARD + 1024 + 2, -bigd[i], BIG - 4096);
        }
    }
    Report(start);

    Section = "memset, every value, lengths 0-300";
    start = Now_Cs();
    {
        // The buffer is refilled at random every so often; between, each case starts from
        // what the last left, which "expect" tracks.
        for (int v = 0; v < 256; v += quick ? 17 : 1) {
            for (unsigned da = 0; da < 4; ++da) {
                for (size_t n = 0; n <= SMALL; ++n) {
                    // the value as given, and with junk in the bits memset must ignore
                    int c = (n % 3 == 0) ? v : (n % 3 == 1) ? (v | 0x5A00) : (v - 256 * 3);
                    Fill_Case("memset", false, b, e, da, c, n, n % 32 == 0);
                }
            }
        }
        for (unsigned da = 0; da < 4; ++da) {
            for (size_t n = 0; n <= SMALL; ++n) {
                Fill_Case("bzero", true, b, e, da, 0, n, true);
            }
        }
    }
    Report(start);

    Section = "memset, large";
    start = Now_Cs();
    {
        const size_t larges[] = {4095, 4096, 65536, 65536 + 7, BIG - 3, BIG};
        const int vals[] = {0, 0xFF, 0x80, 0x5A, -1, 0x1234};
        for (unsigned l = 0; l < sizeof(larges) / sizeof(larges[0]); ++l) {
            for (unsigned da = 0; da < 4; ++da) {
                for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i) {
                    if (quick && i > 1) {
                        continue;
                    }
                    Fill_Case("memset", false, b, e, da, vals[i], larges[l], true);
                }
            }
        }
    }
    Report(start);

    Section = "random, up to 4100 bytes, against UnixLib";
    start = Now_Cs();
    {
        const unsigned N = quick ? 2000 : 20000;
        unsigned char* u = t; // UnixLib's result
        for (unsigned i = 0; i < N; ++i) {
            size_t n = Rand() % 4101;
            unsigned sa = Rand() % 64, da = Rand() % 64;
            unsigned kind = Rand() % 3;
            size_t total = n + 256;
            Random_Fill(a, 2 * total + 256);
            Ref_Copy(e, a, 2 * total + 256);
            unsigned char *rs, *rd, *us, *ud;
            if (kind == 2) { // memmove, overlapping by a random amount either way
                long d = long(Rand() % 200) - 100;
                rs = a + 128 + sa;
                rd = rs + d;
                us = e + 128 + sa;
                ud = us + d;
                void* r1 = vc_memmove(rd, rs, n);
                void* r2 = memmove(ud, us, n);
                ++Cases;
                if (r1 != rd || r2 != ud || Diff(a, e, 2 * total + 256) >= 0) {
                    Fail("random memmove", sa & 3, unsigned(128 + sa + d) & 3, unsigned(n), d, "differs from UnixLib");
                }
            } else if (kind == 1) {
                int c = int(Rand());
                rd = a + 128 + da;
                ud = e + 128 + da;
                void* r1 = vc_memset(rd, c, n);
                void* r2 = memset(ud, c, n);
                ++Cases;
                if (r1 != rd || r2 != ud || Diff(a, e, 2 * total + 256) >= 0) {
                    Fail("random memset", 0, da & 3, unsigned(n), c, "differs from UnixLib");
                }
            } else {
                rs = a + 64 + sa;
                rd = a + total + 64 + da;
                us = e + 64 + sa;
                ud = e + total + 64 + da;
                void* r1 = vc_memcpy(rd, rs, n);
                void* r2 = memcpy(ud, us, n);
                ++Cases;
                if (r1 != rd || r2 != ud || Diff(a, e, 2 * total + 256) >= 0) {
                    Fail("random memcpy", sa & 3, da & 3, unsigned(n), 0, "differs from UnixLib");
                }
            }
            (void)u;
        }
    }
    Report(start);

    Section = "VC_Stamp_Rows_24";
    start = Now_Cs();
    {
        const int pitches[] = {24, 28, 640, 1680};
        for (unsigned p = 0; p < sizeof(pitches) / sizeof(pitches[0]); ++p) {
            int pitch = pitches[p];
            for (unsigned sa = 0; sa < 4; ++sa) {
                for (unsigned dofs = 0; dofs < 12; ++dofs) { // every destination alignment
                    for (int rows = 1; rows <= 24; ++rows) {
                        size_t total = size_t(rows) * pitch + 2 * GUARD + 32;
                        Random_Fill(a, 24 * 24 + 2 * GUARD);
                        Random_Fill(b, total);
                        Ref_Copy(t, a, 24 * 24 + 2 * GUARD);
                        Ref_Copy(e, b, total);
                        const unsigned char* s = a + GUARD + sa;
                        for (int r = 0; r < rows; ++r) {
                            Ref_Copy(e + GUARD + dofs + r * pitch, s + r * 24, 24);
                        }
                        VC_Stamp_Rows_24(b + GUARD + dofs, s, pitch, rows);
                        ++Cases;
                        long at = Diff(b, e, total);
                        if (at >= 0) {
                            char how[64];
                            snprintf(how, sizeof(how), "pitch %d rows %d: wrong byte", pitch, rows);
                            Fail("VC_Stamp_Rows_24", sa, dofs, 24, at - long(GUARD + dofs), how);
                        }
                        if (Diff(a, t, 24 * 24 + 2 * GUARD) >= 0) {
                            Fail("VC_Stamp_Rows_24", sa, dofs, 24, 0, "changed the source");
                        }
                    }
                }
            }
        }
    }
    Report(start);

    printf("memtest: %s: %lu cases, %lu failures, %.1f s\n",
           Failures == 0 ? "PASS" : "FAIL",
           Cases,
           Failures,
           (Now_Cs() - all) / 100.0);
    return Failures == 0 ? 0 : 1;
}
