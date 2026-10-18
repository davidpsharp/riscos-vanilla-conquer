// The game's speed test on its own (common/riscos_speedtest.cpp): prints this machine's speeds
// and the screen size the game would pick for the desktop, at 30 and 60 presents a second.
// For calibrating the test on real machines; run it from the desktop (a task window will do).
#include "riscos_speedtest.h"

#include <kernel.h>
#include <swis.h>

#include <cstdio>

static int Mode_Variable(int variable)
{
    _kernel_swi_regs regs;
    regs.r[0] = -1;
    regs.r[1] = variable;
    _kernel_swi(OS_ReadModeVariable, &regs, &regs);
    return regs.r[2];
}

int main()
{
    RISCOSSpeeds speeds;
    RISCOS_Measure_Speeds(speeds);
    int dw = Mode_Variable(11) + 1, dh = Mode_Variable(12) + 1;
    int xeig = Mode_Variable(4), yeig = Mode_Variable(5);
    if (yeig > xeig) {
        dh <<= yeig - xeig;
    }
    printf("speeds: cpu %.0f, memory %.0f KB/s, screen %.0f KB/s; desktop %dx%d, %d bpp\n",
           speeds.cpu, speeds.memory, speeds.screen, dw, dh, 1 << Mode_Variable(9));
    for (int presents : {30, 60}) {
        fprintf(stderr, "at %d presents a second:\n", presents);
        std::string pick = RISCOS_Pick_Resolution(speeds, presents, dw, dh, xeig == yeig);
        printf("pick at %d presents a second: %s\n", presents, pick.c_str());
    }
    return 0;
}
