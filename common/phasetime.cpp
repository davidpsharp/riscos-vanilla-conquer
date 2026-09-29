#include "phasetime.h"

#include <chrono>
#include <stdio.h>
#include <stdlib.h>

bool Phase_Timing = getenv("VC_FPSLOG") != nullptr;

static volatile unsigned Phase_Us[PHASE_COUNT];

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
    Phase_Us[id] += us; // Only the audio thread writes PHASE_MIXER.
}

void Phase_Report(unsigned elapsed_ms)
{
    if (!Phase_Timing || elapsed_ms == 0) {
        return;
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
            "callback %.0f present %.0f sleep %.0f other %.0f | audio mixing %.0f\n",
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
            elapsed_ms > accounted ? (elapsed_ms - accounted) * scale : 0.0,
            ms[PHASE_MIXER] * scale);
}
