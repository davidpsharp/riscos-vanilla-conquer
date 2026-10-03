#include "phasetime.h"

#include <chrono>
#ifdef SDL_BUILD
#include <SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif

bool Phase_Timing = getenv("VC_FPSLOG") != nullptr;

static volatile unsigned Phase_Us[PHASE_COUNT];
static unsigned Frame_Us[PHASE_COUNT]; // the current game frame's share
static unsigned Frame_Started = 0;
static intptr_t Frame_Heap = 0;

// The top of the heap, to see whether a stall came with the heap growing.
static intptr_t Heap_Top()
{
#if defined(_WIN32) || defined(__APPLE__)
    return 0;
#else
    return reinterpret_cast<intptr_t>(sbrk(0));
#endif
}
unsigned Phase_Counts[COUNT_MAX];

#ifdef __riscos__
/*
** RISC OS's clocks only tick every centisecond, and the frame limiter's sleep
** always ends on a tick, so the work after it would hardly ever span one and
** would be measured as nothing. On a Risc PC under RISC OS 3.5 to 4.x, read
** IOMD timer 0 instead: the 2 MHz counter behind the centisecond tick, which
** RISC OS reloads with 19999 every tick. That needs supervisor mode.
*/
static unsigned Iomd_Timer0()
{
    register unsigned count asm("r0");
    asm volatile("swi 0x16\n"                // OS_EnterOS: to SVC26
                 "mov r1, #0x03200000\n"     // IOMD
                 "strb r1, [r1, #0x4c]\n"    // T0LATCH: latch the count
                 "ldrb r0, [r1, #0x40]\n"    // T0LOW
                 "ldrb r2, [r1, #0x44]\n"    // T0HIGH
                 "orr r0, r0, r2, lsl #8\n"
                 "teqp pc, #0\n"             // back to USR26
                 "mov r0, r0\n"
                 : "=r"(count)
                 :
                 : "r1", "r2", "lr", "cc", "memory");
    return count;
}

static unsigned Monotonic_Cs()
{
    register unsigned cs asm("r0");
    asm volatile("swi 0x42" : "=r"(cs) : : "lr", "cc"); // OS_ReadMonotonicTime
    return cs;
}

static bool Use_Iomd()
{
    // OS_Byte 129,0,255 gives the OS version: &A5 (3.5) to &A9 (4.3x) are
    // 26-bit IOMD machines. RISC OS 5 maps the hardware elsewhere.
    register int r0 asm("r0") = 129;
    register int r1 asm("r1") = 0;
    register int r2 asm("r2") = 255;
    asm volatile("swi 0x06" : "+r"(r0), "+r"(r1), "+r"(r2) : : "lr", "cc"); // OS_Byte
    return r1 >= 0xA5 && r1 <= 0xA9;
}

unsigned Phase_Now_Us()
{
    static const bool iomd = Use_Iomd();
    if (iomd) {
        // Retry if the tick happened between the readings.
        for (int tries = 0; tries < 4; ++tries) {
            unsigned cs = Monotonic_Cs();
            unsigned count = Iomd_Timer0();
            if (Monotonic_Cs() == cs && count < 20000) {
                return cs * 10000 + (19999 - count) / 2;
            }
        }
    }
    using namespace std::chrono;
    return unsigned(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
#else
unsigned Phase_Now_Us()
{
    using namespace std::chrono;
    return unsigned(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
#endif

void Phase_Add(PhaseId id, unsigned us)
{
    Phase_Us[id] += us;
    Frame_Us[id] += us;
}

static PhaseId Seq_Phase;
static unsigned Seq_Start;
static bool Seq_Active = false;

void Phase_Timer_Begin(PhaseId id)
{
    if (Phase_Timing) {
        Seq_Phase = id;
        Seq_Start = Phase_Now_Us();
        Seq_Active = true;
    }
}

void Phase_Timer_Switch(PhaseId id)
{
    if (Seq_Active) {
        unsigned now = Phase_Now_Us();
        Phase_Add(Seq_Phase, now - Seq_Start);
        Seq_Phase = id;
        Seq_Start = now;
    }
}

void Phase_Timer_End()
{
    if (Seq_Active) {
        Phase_Add(Seq_Phase, Phase_Now_Us() - Seq_Start);
        Seq_Active = false;
    }
}

void Phase_Frame_Begin()
{
    if (!Phase_Timing) {
        return;
    }
    for (int i = 0; i < PHASE_COUNT; ++i) {
        Frame_Us[i] = 0;
    }
    Frame_Started = Phase_Now_Us();
    Frame_Heap = Heap_Top();
}

/*
** A pause the player notices is a single long frame, which the 5 second averages
** hide, so log each one with where its time went.
*/
void Phase_Frame_End()
{
    if (!Phase_Timing || Frame_Started == 0) {
        return;
    }
    static const unsigned threshold = getenv("VC_STALLMS") ? unsigned(atoi(getenv("VC_STALLMS"))) : 150;
    unsigned took = (Phase_Now_Us() - Frame_Started) / 1000;
    if (took < threshold) {
        return;
    }
    unsigned accounted = Frame_Us[PHASE_RENDER] + Frame_Us[PHASE_LOGIC] + Frame_Us[PHASE_QUEUE] + Frame_Us[PHASE_CALLBACK]
                + Frame_Us[PHASE_PRESENT] + Frame_Us[PHASE_SLEEP];
    unsigned other = took * 1000 > accounted ? took * 1000 - accounted : 0;
    fprintf(stderr,
            "stall: game frame took %u ms at %u.%03u s: draw %u (map %u) logic %u queue %u callback %u present %u sleep %u "
            "other %u [logic: teams %u objects %u map %u factories %u houses %u] [callback: theme %u speech %u "
            "stream file %u audio lock %u] heap %+ld KB\n",
            took,
#ifdef SDL_BUILD
            (SDL_GetTicks() - took) / 1000,
            (SDL_GetTicks() - took) % 1000,
#else
            0u,
            0u,
#endif
            Frame_Us[PHASE_RENDER] / 1000,
            Frame_Us[PHASE_TACTICAL] / 1000,
            Frame_Us[PHASE_LOGIC] / 1000,
            Frame_Us[PHASE_QUEUE] / 1000,
            Frame_Us[PHASE_CALLBACK] / 1000,
            Frame_Us[PHASE_PRESENT] / 1000,
            Frame_Us[PHASE_SLEEP] / 1000,
            other / 1000,
            Frame_Us[PHASE_LOGIC_TEAMS] / 1000,
            Frame_Us[PHASE_LOGIC_OBJECTS] / 1000,
            Frame_Us[PHASE_LOGIC_MAP] / 1000,
            Frame_Us[PHASE_LOGIC_FACTORIES] / 1000,
            Frame_Us[PHASE_LOGIC_HOUSES] / 1000,
            Frame_Us[PHASE_CB_THEME] / 1000,
            Frame_Us[PHASE_CB_SPEAK] / 1000,
            Frame_Us[PHASE_CB_STREAM_FILE] / 1000,
            Frame_Us[PHASE_CB_AUDIO_LOCK] / 1000,
            long((Heap_Top() - Frame_Heap) / 1024));
}

void Phase_Report(unsigned elapsed_ms)
{
    if (!Phase_Timing || elapsed_ms == 0) {
        return;
    }
    fprintf(stderr,
            "frames/s: game drew %.1f, presents full %.1f partial %.1f skipped %.1f\n",
            Phase_Counts[COUNT_BLIT_DISPLAY] * 1000.0 / elapsed_ms,
            Phase_Counts[COUNT_PRESENT_FULL] * 1000.0 / elapsed_ms,
            Phase_Counts[COUNT_PRESENT_PARTIAL] * 1000.0 / elapsed_ms,
            Phase_Counts[COUNT_PRESENT_SKIPPED] * 1000.0 / elapsed_ms);
    if (Phase_Counts[COUNT_MOTION_BETWEEN_FRAMES] + Phase_Counts[COUNT_MOTION_BY_GAME] != 0) {
        fprintf(stderr,
                "pointer: moves picked up between frames %u, by the game %u\n",
                Phase_Counts[COUNT_MOTION_BETWEEN_FRAMES],
                Phase_Counts[COUNT_MOTION_BY_GAME]);
    }
    if (Phase_Counts[COUNT_DELAY_ASKED_MS] != 0) {
        fprintf(stderr,
                "delays: asked %u ms, took %u ms\n",
                Phase_Counts[COUNT_DELAY_ASKED_MS],
                Phase_Counts[COUNT_DELAY_TOOK_MS]);
    }
    for (int i = 0; i < COUNT_MAX; ++i) {
        Phase_Counts[i] = 0;
    }
    unsigned ms[PHASE_COUNT];
    for (int i = 0; i < PHASE_COUNT; ++i) {
        ms[i] = Phase_Us[i] / 1000;
        Phase_Us[i] = 0;
    }

    // The Draw_It layers each draw the one below first, so subtract to get each layer's own time.
    auto own = [](unsigned outer, unsigned inner) { return outer > inner ? outer - inner : 0; };
    unsigned tactical = ms[PHASE_TACTICAL];
    unsigned radar = own(ms[PHASE_RADAR], ms[PHASE_TACTICAL]);
    unsigned power = own(ms[PHASE_POWER], ms[PHASE_RADAR]);
    unsigned sidebar = own(ms[PHASE_SIDEBAR], ms[PHASE_POWER]);
    unsigned render_rest = own(ms[PHASE_RENDER], ms[PHASE_SIDEBAR]);
    unsigned accounted = ms[PHASE_RENDER] + ms[PHASE_LOGIC] + ms[PHASE_QUEUE] + ms[PHASE_CALLBACK]
                         + ms[PHASE_PRESENT] + ms[PHASE_SLEEP];

    // Milliseconds per second of wall time, so the main thread's phases add up to about 1000.
    double scale = 1000.0 / elapsed_ms;
    fprintf(stderr,
            "time ms/s: map %.0f radar %.0f power %.0f sidebar %.0f other-draw %.0f | logic %.0f queue %.0f "
            "callback %.0f present %.0f sleep %.0f other %.0f\n",
            tactical * scale,
            radar * scale,
            power * scale,
            sidebar * scale,
            render_rest * scale,
            ms[PHASE_LOGIC] * scale,
            ms[PHASE_QUEUE] * scale,
            ms[PHASE_CALLBACK] * scale,
            ms[PHASE_PRESENT] * scale,
            ms[PHASE_SLEEP] * scale,
            elapsed_ms > accounted ? (elapsed_ms - accounted) * scale : 0.0);
}
