#include "wwstd.h"
#include "settings.h"
#include "ini.h"
#include "miscasm.h"

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>

/*
** Whether this is a machine newer than the Risc PC and A7000. OS_ReadSysInfo 8 gives the
** platform class: 1 to 3 are their IOMD hardware (and RPCEmu's), 4 and above the Iyonix,
** the Raspberry Pi and other ARMv7 boards. An OS without the call is old enough to be slow.
*/
static bool RISCOS_Is_Fast_Machine()
{
    _kernel_swi_regs regs;
    regs.r[0] = 8;
    if (_kernel_swi(OS_ReadSysInfo, &regs, &regs) != NULL) {
        return false;
    }
    return regs.r[0] >= 4;
}
#endif

SettingsClass Settings;

SettingsClass::SettingsClass()
{
    /*
    ** Mouse settings
    */
    Mouse.RawInput = true;
    Mouse.Sensitivity = 100;
    Mouse.ControllerEnabled = false;
    Mouse.ControllerPointerSpeed = 10;
    Options.MouseWheelScrolling = true;

    /*
    ** Video settings
    */
    Video.WindowWidth = 640;
    Video.WindowHeight = 400;
    Video.Windowed = false;
    Video.Width = 0;
    Video.Height = 0;
    Video.Boxing = true;
    Video.BoxingAspectRatio = "16:10";
    Video.Resolution = "640x400";
#ifdef __riscos__
    if (RISCOS_Is_Fast_Machine()) {
        // A Pi or similar copies a frame in well under a millisecond, so present at 60
        // for a smoother pointer and scrolling (game logic still runs at 15 fps), and
        // blend movies both ways.
        Video.FrameLimit = 60;
        Video.InterpolationMode = 2;
    } else {
        // Each presented frame is a full 640x400 copy to screen memory, which a Risc PC's
        // memory bus can't sustain at 120 per second. Game logic runs at 15 fps anyway.
        Video.FrameLimit = 30;
        // Blend horizontally, double lines: a third of mode 2's table lookups. On a real
        // StrongARM Risc PC mode 2 couldn't keep up with detailed movies such as the GDI 1
        // briefing (the 64 KB table is 4x the data cache); mode 1 plays them smoothly.
        Video.InterpolationMode = 1;
    }
#else
    Video.FrameLimit = 120;
    Video.InterpolationMode = 2;
#endif
    Video.HardwareCursor = false;
    Video.DOSMode = false;
    Video.Scaler = "nearest";
    Video.Driver = "default";
    Video.PixelFormat = "default";
}

void SettingsClass::Load(INIClass& ini)
{
    char buf[128];

    /*
    ** Mouse settings
    */
    Mouse.RawInput = ini.Get_Bool("Mouse", "RawInput", Mouse.RawInput);
    Mouse.Sensitivity = ini.Get_Int("Mouse", "Sensitivity", Mouse.Sensitivity);
    Mouse.ControllerEnabled = ini.Get_Bool("Mouse", "ControllerEnabled", Mouse.ControllerEnabled);
    Mouse.ControllerPointerSpeed = ini.Get_Int("Mouse", "ControllerPointerSpeed", Mouse.ControllerPointerSpeed);
    /*
    ** Compatibility with CNCNet configuration for this feature
    */
    Options.MouseWheelScrolling = ini.Get_Bool("Options", "MouseWheelScrolling", Options.MouseWheelScrolling);
    Options.MouseWheelScrolling = ini.Get_Bool("Mouse", "MouseWheelScrolling", Options.MouseWheelScrolling);

    /*
    ** Video settings
    */
    Video.WindowWidth = ini.Get_Int("Video", "WindowWidth", Video.WindowWidth);
    Video.WindowHeight = ini.Get_Int("Video", "WindowHeight", Video.WindowHeight);
    Video.Windowed = ini.Get_Bool("Video", "Windowed", Video.Windowed);
    Video.Boxing = ini.Get_Bool("Video", "Boxing", Video.Boxing);
    Video.BoxingAspectRatio = ini.Get_String("Video", "BoxingAspectRatio", Video.BoxingAspectRatio);
    Video.Resolution = ini.Get_String("Video", "Resolution", Video.Resolution);
    Video.Width = ini.Get_Int("Video", "Width", Video.Width);
    Video.Height = ini.Get_Int("Video", "Height", Video.Height);
    Video.FrameLimit = ini.Get_Int("Video", "FrameLimit", Video.FrameLimit);
    Video.HardwareCursor = ini.Get_Bool("Video", "HardwareCursor", Video.HardwareCursor);
    Video.DOSMode = ini.Get_Bool("Video", "DOSMode", Video.DOSMode);
    Video.Scaler = ini.Get_String("Video", "Scaler", Video.Scaler);
    Video.Driver = ini.Get_String("Video", "Driver", Video.Driver);
    Video.PixelFormat = ini.Get_String("Video", "PixelFormat", Video.PixelFormat);

    /*
    ** VQA and WSA interpolation mode 0 = scanlines, 1 = vertical doubling, 2 = linear
    */
    Video.InterpolationMode = Bound(ini.Get_Int("Video", "InterpolationMode", Video.InterpolationMode), 0, 3);

    /*
    ** Boxing and raw input require software cursor.
    */
    if (Video.Boxing || Mouse.RawInput || Mouse.ControllerEnabled) {
        Video.HardwareCursor = false;
    }

    ini.Get_String("Video", "ButtonStyle", "Default", buf, sizeof(buf));
    if (!stricmp(buf, "Gold")) {
        Video.ButtonStyle = 1;
    } else if (!stricmp(buf, "Classic") || !stricmp(buf, "DOS")) {
        Video.ButtonStyle = 0;
    } else {
        Video.ButtonStyle = -1;
    }
}

void SettingsClass::Save(INIClass& ini)
{
    const SettingsClass Defaults;
    /*
    ** Mouse settings
    */
    ini.Put_Bool("Mouse", "RawInput", Mouse.RawInput);
    ini.Put_Int("Mouse", "Sensitivity", Mouse.Sensitivity);
    ini.Put_Bool("Mouse", "ControllerEnabled", Mouse.ControllerEnabled);
    ini.Put_Int("Mouse", "ControllerPointerSpeed", Mouse.ControllerPointerSpeed);
    ini.Put_Bool("Mouse", "MouseWheelScrolling", Options.MouseWheelScrolling);

    /*
    ** Video settings
    */
    ini.Put_Int("Video", "WindowWidth", Video.WindowWidth);
    ini.Put_Int("Video", "WindowHeight", Video.WindowHeight);
    ini.Put_Bool("Video", "Windowed", Video.Windowed);
    ini.Put_Bool("Video", "Boxing", Video.Boxing);
    ini.Put_String("Video", "BoxingAspectRatio", Video.BoxingAspectRatio);
    ini.Put_Int("Video", "Width", Video.Width);
    ini.Put_Int("Video", "Height", Video.Height);
    // Left out when it is this machine's default, so that a copy moved between a Risc PC
    // and a faster machine takes the right one there.
    if (Video.FrameLimit == Defaults.Video.FrameLimit) {
        ini.Clear("Video", "FrameLimit");
    } else {
        ini.Put_Int("Video", "FrameLimit", Video.FrameLimit);
    }
    ini.Put_Bool("Video", "HardwareCursor", Video.HardwareCursor);
    ini.Put_Bool("Video", "DOSMode", Video.DOSMode);
    ini.Put_String("Video", "Scaler", Video.Scaler);
    ini.Put_String("Video", "Resolution", Video.Resolution);
    ini.Put_String("Video", "Driver", Video.Driver);
    ini.Put_String("Video", "PixelFormat", Video.PixelFormat);

    /*
    ** VQA and WSA interpolation mode 0 = scanlines, 1 = vertical doubling, 2 = linear
    */
    // Left out when it is this machine's default, so that a copy moved between a Risc PC
    // and a faster machine takes the right one there.
    if (Video.InterpolationMode == Defaults.Video.InterpolationMode) {
        ini.Clear("Video", "InterpolationMode");
    } else {
        ini.Put_Int("Video", "InterpolationMode", Video.InterpolationMode);
    }

    ini.Put_String(
        "Video", "ButtonStyle", Video.ButtonStyle == -1 ? "Default" : (Video.ButtonStyle == 1 ? "Gold" : "Classic"));
}
