#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
class INIClass;

class SettingsClass
{
public:
    SettingsClass();

    void Load(INIClass& ini);
    void Save(INIClass& ini);

    struct
    {
        bool RawInput;
        int Sensitivity;
        bool ControllerEnabled;
        int ControllerPointerSpeed;
    } Mouse;

    struct
    {
        int WindowWidth;
        int WindowHeight;
        bool Windowed;
        bool Boxing;
        std::string BoxingAspectRatio;
        // The game screen, e.g. "1024x768"; menus and movies stay 640x400, centred in it.
        std::string Resolution;
        int Width;
        int Height;
        int FrameLimit;
        int InterpolationMode;
        bool HardwareCursor;
        bool DOSMode;
        int ButtonStyle;
        std::string Scaler;
        std::string Driver;
        std::string PixelFormat;
    } Video;

    struct
    {
        bool MouseWheelScrolling;
    } Options;
};

extern SettingsClass Settings;

#endif
