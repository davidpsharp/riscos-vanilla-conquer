/*
** The screen size for "auto" (see riscos_autosize.h): the speed test's results are kept in the
** settings file, and the choice is made again from them for the desktop as it is.
*/
#ifdef __riscos__

#include "riscos_autosize.h"
#include "riscos_speedtest.h"
#include "ccfile.h"
#include "ini.h"
#include "settings.h"

#include <kernel.h>
#include <swis.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The version of the speed test; results from another are measured again.
static const int TestVersion = 1;

static int Mode_Variable(int variable)
{
    _kernel_swi_regs regs;
    regs.r[0] = -1;
    regs.r[1] = variable;
    _kernel_swi(OS_ReadModeVariable, &regs, &regs);
    return regs.r[2];
}

static unsigned Monotonic_Cs(void)
{
    _kernel_swi_regs regs;
    _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
    return unsigned(regs.r[0]);
}

const std::string& RISCOS_Auto_Resolution(const char* ini_name)
{
    static std::string resolution;
    if (!resolution.empty()) {
        return resolution;
    }

    CCFileClass file(ini_name);
    INIClass ini;
    ini.Load(file);
    RISCOSSpeeds speeds;
    speeds.cpu = ini.Get_Int("RISCOS", "SpeedCPU", 0);
    speeds.memory = ini.Get_Int("RISCOS", "SpeedMemory", 0);
    speeds.screen = ini.Get_Int("RISCOS", "SpeedScreen", 0);
    const char* retune = getenv("VC_RETUNE");
    bool measure = ini.Get_Int("RISCOS", "SpeedTest", 0) != TestVersion || speeds.cpu <= 0 || speeds.memory <= 0
                   || speeds.screen <= 0 || (retune != nullptr && *retune != '\0' && strcmp(retune, "0") != 0);
    if (!measure) {
        // A tenth of a second of the processor test, in case the game has been copied to
        // another machine with its settings: the speeds are measured again if it differs.
        double cpu = RISCOS_Measure_Cpu(10);
        if (cpu < speeds.cpu * 0.7 || cpu > speeds.cpu * 1.4) {
            fprintf(stderr, "autosize: cpu now %.0f, not %.0f: another machine?\n", cpu, speeds.cpu);
            measure = true;
        }
    }
    if (measure) {
        unsigned start = Monotonic_Cs();
        RISCOS_Measure_Speeds(speeds);
        fprintf(stderr,
                "autosize: speed test (%u cs): cpu %.0f, memory %.0f KB/s, screen %.0f KB/s\n",
                Monotonic_Cs() - start,
                speeds.cpu,
                speeds.memory,
                speeds.screen);
    } else {
        fprintf(stderr,
                "autosize: speeds from %s: cpu %.0f, memory %.0f KB/s, screen %.0f KB/s\n",
                ini_name,
                speeds.cpu,
                speeds.memory,
                speeds.screen);
    }

    int dw = Mode_Variable(11) + 1; // XWindLimit, YWindLimit: the desktop's size, less one
    int dh = Mode_Variable(12) + 1;
    int xeig = Mode_Variable(4), yeig = Mode_Variable(5);
    bool square = xeig == yeig;
    if (yeig > xeig) {
        dh <<= yeig - xeig; // rectangular pixels: the shape as seen, in square pixels
    }
    if (speeds.cpu <= 0 || speeds.memory <= 0 || speeds.screen <= 0) {
        resolution = "640x400";
        fprintf(stderr, "autosize: no speeds, so %s\n", resolution.c_str());
    } else {
        int presents = Settings.Video.FrameLimit > 0 && Settings.Video.FrameLimit < 60 ? Settings.Video.FrameLimit : 60;
        resolution = RISCOS_Pick_Resolution(speeds, presents, dw, dh, square);
        fprintf(stderr, "autosize: desktop %dx%d, so %s\n", dw, dh, resolution.c_str());
    }

    std::string desktop = RISCOS_Size_Text(dw, dh);
    if (measure || ini.Get_String("RISCOS", "AutoResolution", "") != resolution
        || ini.Get_String("RISCOS", "AutoDesktop", "") != desktop) {
        ini.Put_Int("RISCOS", "SpeedTest", TestVersion);
        ini.Put_Int("RISCOS", "SpeedCPU", int(speeds.cpu + 0.5));
        ini.Put_Int("RISCOS", "SpeedMemory", int(speeds.memory + 0.5));
        ini.Put_Int("RISCOS", "SpeedScreen", int(speeds.screen + 0.5));
        ini.Put_String("RISCOS", "AutoDesktop", desktop);
        ini.Put_String("RISCOS", "AutoResolution", resolution);
        ini.Save(file);
    }
    return resolution;
}

#endif
