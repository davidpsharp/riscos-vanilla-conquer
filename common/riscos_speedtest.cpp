/*
** The speed test and the screen size it leads to (see riscos_speedtest.h).
**
** What limits the game is per second of play: 15 game frames, each with its logic (independent
** of the screen size) and drawing (in proportion to the screen's area, into memory), and up to
** FrameLimit presents, each a copy of the whole screen to screen memory while the map scrolls.
** On a Risc PC the copies dominate: its screen memory takes about 20 MB/s. The speed test
** measures three things, each for a quarter of a second:
**   cpu     a loop like the game's logic: arithmetic, branches and small-table lookups;
**   memory  copying 512 KB blocks, as drawing the map does;
**   screen  copying to screen memory, as presenting does. It writes back what the desktop's
**           screen already holds, so nothing changes on it.
** Each is taken relative to the StrongARM Risc PC, whose times at 640x400 are known (see
** riscos/README.md), to project the load at each candidate size.
*/
#ifdef __riscos__

#include "riscos_speedtest.h"

#include <kernel.h>
#include <swis.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/*
** The StrongARM Risc PC (RISC OS 4.39, 2 MB VRAM): the speed test's results there (two runs,
** within 4%), and the game's own times at 640x400 in a busy mission (Red Alert's Allied 2: 28.5
** ms a frame, of which about 10 ms is the copy to the screen). Logic and drawing are split by
** the battle benchmark's proportions. Its screen memory took 18 MB/s.
*/
static const double RefCpu = 10500;     // thousand loops a second
static const double RefMemory = 14000;  // KB/s
static const double RefLogicMs = 12.0;
static const double RefDrawMs = 6.5;    // at 640x400
static const double RefPixels = 640.0 * 400.0;

// Of every second, what may go on the game: the rest is headroom for battles and the sound.
static const double BudgetMs = 600;

static unsigned Monotonic_Cs(void)
{
    _kernel_swi_regs regs;
    _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
    return unsigned(regs.r[0]);
}

// Waits for the next centisecond tick, to start a measurement on one.
static unsigned Next_Tick(void)
{
    unsigned start = Monotonic_Cs();
    unsigned now;
    while ((now = Monotonic_Cs()) == start) {
    }
    return now;
}

static const unsigned TestCs = 25;

/*
** The logic-like loop: a pseudo-random walk over a 16 KB table, with a data-dependent branch.
** The result is used, so the compiler keeps the work.
*/
static volatile unsigned Cpu_Sink;

double RISCOS_Measure_Cpu(unsigned cs)
{
    std::vector<unsigned char> table(16384);
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = (unsigned char)(i * 7 + (i >> 5));
    }
    unsigned x = 1, sum = 0, loops = 0;
    unsigned start = Next_Tick();
    unsigned elapsed;
    do {
        for (int i = 0; i < 1000; ++i) {
            x = x * 1103515245u + 12345u;
            sum += table[(x >> 16) & 16383];
            if (sum & 1) {
                sum ^= x >> 3;
            }
        }
        ++loops;
        elapsed = Monotonic_Cs() - start;
    } while (elapsed < cs);
    Cpu_Sink = sum;
    return loops * 100.0 / elapsed; // thousands a second
}

static double Measure_Memory(void)
{
    const size_t size = 512 * 1024;
    char* a = static_cast<char*>(malloc(size));
    char* b = static_cast<char*>(malloc(size));
    if (a == nullptr || b == nullptr) {
        free(a);
        free(b);
        return 0;
    }
    memset(a, 1, size);
    memset(b, 2, size);
    unsigned copies = 0;
    unsigned start = Next_Tick();
    unsigned elapsed;
    do {
        memcpy(copies & 1 ? a : b, copies & 1 ? b : a, size);
        ++copies;
        elapsed = Monotonic_Cs() - start;
    } while (elapsed < TestCs);
    free(a);
    free(b);
    return copies * (size / 1024.0) * 100.0 / elapsed; // KB/s
}

static int Mode_Variable(int variable)
{
    _kernel_swi_regs regs;
    regs.r[0] = -1;
    regs.r[1] = variable;
    _kernel_swi(OS_ReadModeVariable, &regs, &regs);
    return regs.r[2];
}

/*
** Copies the start of the screen to itself through a buffer: read once, then written back
** repeatedly, so only the writes are timed (as in a present) and the screen doesn't change.
*/
static double Measure_Screen(void)
{
    int vars[2] = {149, -1}; // DisplayStart
    int values[1] = {0};
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(vars);
    regs.r[1] = reinterpret_cast<int>(values);
    if (_kernel_swi(OS_ReadVduVariables, &regs, &regs) != nullptr || values[0] == 0) {
        return 0;
    }
    size_t screen_bytes = size_t(Mode_Variable(6)) * size_t(Mode_Variable(12) + 1); // LineLength x rows
    size_t size = screen_bytes < 256 * 1024 ? screen_bytes : 256 * 1024;
    size &= ~size_t(3);
    if (size < 16 * 1024) {
        return 0;
    }
    char* screen = reinterpret_cast<char*>(values[0]);
    char* copy = static_cast<char*>(malloc(size));
    if (copy == nullptr) {
        return 0;
    }
    memcpy(copy, screen, size);
    unsigned copies = 0;
    unsigned start = Next_Tick();
    unsigned elapsed;
    do {
        memcpy(screen, copy, size);
        ++copies;
        elapsed = Monotonic_Cs() - start;
    } while (elapsed < TestCs);
    free(copy);
    return copies * (size / 1024.0) * 100.0 / elapsed; // KB/s
}

void RISCOS_Measure_Speeds(RISCOSSpeeds& speeds)
{
    speeds.cpu = RISCOS_Measure_Cpu(TestCs);
    speeds.memory = Measure_Memory();
    speeds.screen = Measure_Screen();
}

// Milliseconds of every second that the game would take at w x h.
static double Load_Ms(const RISCOSSpeeds& speeds, int presents, int w, int h)
{
    double pixels = double(w) * h;
    double logic = RefLogicMs * RefCpu / speeds.cpu;
    double draw = RefDrawMs * (pixels / RefPixels) * RefMemory / speeds.memory;
    double present = presents * pixels / (speeds.screen * 1024.0) * 1000.0;
    return 15 * (logic + draw) + present;
}

// Whether the screen can show w x h at 256 colours. OS_CheckModeValid sets C for a mode it
// can't show, with R0 -1, or -2 for one that needs more screen memory than there is now
// (which the mode change itself makes room for where the screen memory can grow).
static bool Mode_Valid(int w, int h)
{
    int selector[] = {1, w, h, 3, -1, -1}; // a mode selector: 8 bits a pixel, any frame rate
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(selector);
    int carry = 0;
    if (_kernel_swi_c(OS_CheckModeValid, &regs, &regs, &carry) != nullptr) {
        return false;
    }
    return !carry || regs.r[0] == -2;
}

struct Candidate
{
    int w, h;
    bool fraction; // an exact fraction of the desktop: the monitor scales it evenly
};

std::string RISCOS_Size_Text(int w, int h)
{
    return std::to_string(w) + "x" + std::to_string(h);
}

std::string RISCOS_Pick_Resolution(const RISCOSSpeeds& speeds, int presents, int dw, int dh, bool square)
{
    std::vector<Candidate> candidates;
    auto add = [&](int w, int h, bool fraction) {
        if (w < 640 || h < 400 || w > dw || h > dh || w > 2048 || h > 2048 || (w & 1) || (h & 1)) {
            return;
        }
        for (const Candidate& c : candidates) {
            if (c.w == w && c.h == h) {
                return;
            }
        }
        if (Mode_Valid(w, h)) {
            candidates.push_back({w, h, fraction});
        }
    };
    if (square) {
        for (int k = 1; k <= 3; ++k) {
            if (dw % k == 0 && dh % k == 0) {
                add(dw / k, dh / k, true);
            }
        }
    }
    static const int Widths[] = {640, 720, 768, 800, 864, 960, 1024, 1152, 1280, 1360, 1440, 1600, 1680, 1920, 2048};
    for (int w : Widths) {
        add(w, (int(double(w) * dh / dw + 0.5)) & ~1, false);
    }

    const Candidate* best = nullptr;
    for (const Candidate& c : candidates) {
        double load = Load_Ms(speeds, presents, c.w, c.h);
        fprintf(stderr, "autosize:   %dx%d%s: %.0f ms/s\n", c.w, c.h, c.fraction ? " (fraction)" : "", load);
        if (load <= BudgetMs && (best == nullptr || c.w * c.h > best->w * best->h)) {
            best = &c;
        }
    }
    if (best == nullptr) {
        return "640x400"; // the menus' size: the smallest the game has (but for low resolution)
    }
    // A fraction of the desktop nearly as big looks sharper.
    const Candidate* pick = best;
    for (const Candidate& c : candidates) {
        if (c.fraction && Load_Ms(speeds, presents, c.w, c.h) <= BudgetMs && c.w * c.h >= best->w * best->h * 3 / 4
            && (!pick->fraction || c.w * c.h > pick->w * pick->h)) {
            pick = &c;
        }
    }
    return RISCOS_Size_Text(pick->w, pick->h);
}

#endif
