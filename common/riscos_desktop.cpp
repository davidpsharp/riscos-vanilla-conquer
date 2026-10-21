/*
** The game on the RISC OS desktop (see riscos_desktop.h).
**
** SDL's RISC OS port already makes the game a Wimp task (at SDL_Init's video start-up), and in
** full screen it stores the desktop's mode and stops polling the Wimp, which freezes the desktop.
** Here the game puts an icon on the icon bar with that task, and polls the Wimp itself while the
** game is not on the screen: at the start until the icon is clicked, and after Shift+F12, when the
** desktop's mode is restored (RISCOS_RestoreWimpMode, SDL's own) and the game waits, paused.
**
** The icon's menu and Info window follow the standard ones (Edit's, for instance): the menu has
** "Info" leading to "About this program" with Name, Purpose, Author and Version, then "Quit".
** Between them, "Screen size" leads to the sizes the game can use on this monitor. At the start
** a choice is only saved, as the game reads it after the click; once the game is running the
** screen can't change size under it, so a different size restarts the game, after asking.
** "Help" opens !Help.
**
** The game data comes from the freeware CD images (ISO files), dragged onto the icon. !Run checks
** the installed files (Utils.vcprep -check) and leaves any problem in <App>$DataProblem; while
** there is one, a click on the icon says how to install the data and opens !Help, instead of
** starting the game. A dragged file is identified here from its directory (discid.h): this game's
** disc is installed by Utils.vcprep in a task window, which closes by itself when all went well;
** another game's disc, an MS-DOS one, a cut-short image or something else is turned away, saying
** why. The paths the game set up at the start were for an app without its data, so once it is
** installed a click restarts the game rather than going on.
*/
#ifdef __riscos__

#include "riscos_desktop.h"
#include "riscos_autosize.h"
#include "ccfile.h"
#include "../riscos/tools/vcprep/discid.h"
#include "../riscos/tools/vcprep/vcfiles.h"
#undef SEP // vcfiles.h's, for its tables only
#include "gitinfo.h"
#include "ini.h"
#include "mixer_sdl1.h"
#include "settings.h"

#include <SDL.h>
#include <kernel.h>
#include <pthread.h>
#include <swis.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// SDL's RISC OS port, which keeps the task and the desktop's mode.
extern "C" int RISCOS_GetTaskHandle(void);

// The game's own (winstub.cpp): stop and restart its sound and music.
void Focus_Loss(void);
void Focus_Restore(void);

// The game's own shutdown (startup.cpp), as its Exit Game does: the sound first.
void Prog_End(const char* why, bool fatal);

// video_sdl1.cpp: the desktop's screen mode, and back to the game's.
void Video_Leave_For_Desktop(void);
void Video_Return_From_Desktop(void);

bool RISCOS_Desktop_Click_To_Start = true;

static RISCOSDesktopApp App = {"!vanillatd", "Vanilla", "", "", "CONQUER.INI", "Vanilla"};
static int IconHandle = -1;
static int InfoWindow = -1;
static bool AudioPaused = false; // by us, while on the desktop
static bool InGame = false;      // past the click that starts the game

// The screen the game runs at (RISCOS_Desktop_Set_Screen).
static int ScreenW = 0, ScreenH = 0;
static bool ForcedLowRes = false;

void RISCOS_Desktop_Set_App(const RISCOSDesktopApp& app)
{
    App = app;
}

static bool Swi(int swi, _kernel_swi_regs& regs)
{
    _kernel_oserror* error = _kernel_swi(swi, &regs, &regs);
    if (error != nullptr) {
        fprintf(stderr, "desktop: SWI &%X: %s\n", swi, error->errmess);
        return false;
    }
    return true;
}

/*
** Icon blocks, as in a window or Wimp_CreateIcon: the bounding box, flags and 12 bytes of data.
*/
struct Icon
{
    int x0, y0, x1, y1;
    unsigned flags;
    union
    {
        char text[12];
        struct
        {
            const char* buffer;
            const char* validation;
            int length;
        } ind;
    } data;
};

static void Plain_Text(Icon& icon, int x0, int y0, int x1, int y1, unsigned flags, const char* text)
{
    icon.x0 = x0;
    icon.y0 = y0;
    icon.x1 = x1;
    icon.y1 = y1;
    icon.flags = flags;
    memset(icon.data.text, 0, sizeof(icon.data.text));
    strncpy(icon.data.text, text, sizeof(icon.data.text));
}

static void Indirected(Icon& icon, int x0, int y0, int x1, int y1, unsigned flags, const char* text, const char* valid)
{
    icon.x0 = x0;
    icon.y0 = y0;
    icon.x1 = x1;
    icon.y1 = y1;
    icon.flags = flags;
    icon.data.ind.buffer = text;
    icon.data.ind.validation = valid;
    icon.data.ind.length = int(strlen(text)) + 1;
}

/*
** "About this program", laid out as Edit's progInfo template: right-aligned labels (Name,
** Purpose, Author, Ported by, Version) beside sunken display fields.
*/
static void Create_Info_Window(void)
{
    // "0.1.0 (20 Oct 2026)"; a dev build says 0.1.0-dev and what git describes (the date is
    // the commit's, so a rebuild of the same commit says the same).
    static char version[96];
    snprintf(version, sizeof(version), "%s", VC_Version_Full);

    static struct
    {
        int visible[4];
        int scroll[2];
        int behind;
        unsigned flags;
        unsigned char colours[8];
        int extent[4];
        unsigned title_flags;
        unsigned button_type;
        int sprite_area;
        short min_w, min_h;
        struct
        {
            const char* buffer;
            const char* validation;
            int length;
        } title;
        int icon_count;
        Icon icons[10];
    } block;

    static const char title[] = "About this program";
    static const char field_valid[] = "R2";

    memset(&block, 0, sizeof(block));
    block.visible[0] = 392;
    block.visible[1] = 628;
    block.visible[2] = 392 + 668;
    block.visible[3] = 628 + 308;
    block.behind = -1;
    // Moveable, auto-redraw, title bar, new format (as Edit's), but kept on the screen: opened
    // from the icon bar's right-hand end, it would otherwise run off it.
    block.flags = 0x84000012;
    const unsigned char colours[8] = {7, 2, 7, 1, 12, 14, 12, 0};
    memcpy(block.colours, colours, sizeof(colours));
    block.extent[0] = 0;
    block.extent[1] = -308;
    block.extent[2] = 668;
    block.extent[3] = 0;
    block.title_flags = 0x0000013D; // text, border, centred, filled, indirected
    block.sprite_area = 1;
    block.title.buffer = title;
    block.title.validation = reinterpret_cast<const char*>(-1);
    block.title.length = sizeof(title);

    // Edit's rows and fields, with a wider label column for "Ported by".
    const char* labels[5] = {"Name", "Purpose", "Author", "Ported by", "Version"};
    const char* values[5] = {App.name, App.purpose, App.author, "David Sharp", version};
    for (int row = 0; row < 5; row++) {
        int const top = -4 - 60 * row;
        Indirected(block.icons[row], 184, top - 52, 660, top, 0x1700613D, values[row], field_valid);
        Plain_Text(block.icons[5 + row], 8, top - 48, 184, top - 8, 0x17000211, labels[row]);
    }
    block.icon_count = 10;

    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(&block);
    if (Swi(Wimp_CreateWindow, regs)) {
        InfoWindow = regs.r[0];
    }
}

/*
** Screen sizes. A choice is a string: "" for the automatic size (the largest this machine keeps
** smooth, see riscos_autosize.cpp), "320x200" for low resolution, or "WxH".
*/
static const char LowResChoice[] = "320x200";

static std::string App_Variable(const char* name)
{
    return std::string(App.prefix) + "$" + name;
}

// A system variable's value ("" if unset), read from the OS: not every one reaches getenv.
static std::string Os_Variable(const std::string& name)
{
    char buf[512];
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(name.c_str());
    regs.r[1] = reinterpret_cast<int>(buf);
    regs.r[2] = sizeof(buf) - 1;
    regs.r[3] = 0;
    regs.r[4] = 3; // expanded to a string
    if (_kernel_swi(OS_ReadVarVal, &regs, &regs) != nullptr) {
        return "";
    }
    buf[regs.r[2]] = '\0';
    return buf;
}

static void Set_Os_Variable(const std::string& name, const char* value)
{
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(name.c_str());
    regs.r[1] = reinterpret_cast<int>(value);
    regs.r[2] = value != nullptr ? int(strlen(value)) : -1; // -1 deletes it
    regs.r[3] = 0;
    regs.r[4] = 4; // literal: the value isn't GSTrans'd
    _kernel_swi(OS_SetVarVal, &regs, &regs);
}

void RISCOS_Desktop_Pixel_Size(int& width, int& height)
{
    RISCOS_Desktop_Size(width, height);
}

const std::string& RISCOS_Desktop_Auto_Resolution(void)
{
    return RISCOS_Auto_Resolution(App.ini);
}

// The low resolution asked for by !Run's variable, which the menu can't override.
static bool Low_Res_Variable(void)
{
    const char* value = getenv(App_Variable("LowRes").c_str());
    return value != nullptr && *value != '\0' && strcmp(value, "0") != 0;
}

static std::string Saved_Choice(void)
{
    if (Settings.Video.LowRes || Settings.Video.DOSMode) {
        return LowResChoice;
    }
    if (Settings.Video.Resolution == "auto") {
        return "";
    }
    return Settings.Video.Resolution;
}

static void Choice_Size(const std::string& choice, int& w, int& h)
{
    std::string size = choice.empty() ? RISCOS_Desktop_Auto_Resolution() : choice;
    w = h = 0;
    sscanf(size.c_str(), "%dx%d", &w, &h);
}

static void Save_Choice(const std::string& choice)
{
    Settings.Video.LowRes = choice == LowResChoice;
    if (!Settings.Video.LowRes) {
        Settings.Video.DOSMode = false; // which is 320x200 too
        Settings.Video.Resolution = choice.empty() ? "auto" : choice;
    }
    CCFileClass file(App.ini);
    INIClass ini;
    ini.Load(file);
    Settings.Save(ini);
    ini.Save(file);
    fprintf(stderr, "desktop: screen size %s saved\n", choice.empty() ? "(automatic)" : choice.c_str());
}

void RISCOS_Desktop_Forget_Size(void)
{
    fprintf(stderr, "desktop: the screen size in the settings could not be used: back to automatic\n");
    Save_Choice("");
}

/*
** Menus as the Wimp takes them: a title, colours and sizes, then the items. Item text that fits
** in 12 characters is held in the item; longer text is indirected.
*/
struct MenuItem
{
    unsigned flags;
    int submenu;
    unsigned icon_flags;
    union
    {
        char text[12];
        struct
        {
            const char* buffer;
            int validation;
            int length;
        } ind;
    } data;
};

static const unsigned ItemTicked = 1;
static const unsigned ItemLast = 0x80;
static const unsigned ItemText = 0x07000021;   // text, filled, black on white
static const unsigned ItemIndirected = 0x100;
static const unsigned ItemShaded = 0x400000;

static const int MaxSizes = 12;

template <int N> struct MenuBlock
{
    char title[12];
    unsigned char colours[4];
    int width, height, gap;
    MenuItem items[N];
};

static MenuBlock<4> Menu;
static MenuBlock<MaxSizes> SizeMenu;
static std::string SizeChoices[MaxSizes];
static char SizeText[MaxSizes][32];
static int SizeCount = 0;

static void Menu_Header(char* title, unsigned char* colours, int& width, int& height, int& gap, const char* text)
{
    strncpy(title, text, 12);
    colours[0] = 7;
    colours[1] = 2;
    colours[2] = 7;
    colours[3] = 0;
    width = (int(strlen(text)) + 2) * 16;
    if (width < 128) {
        width = 128;
    }
    height = 44;
    gap = 0;
}

static void Plain_Item(MenuItem& item, const char* text, int submenu)
{
    item.flags = 0;
    item.submenu = submenu;
    item.icon_flags = ItemText;
    strncpy(item.data.text, text, sizeof(item.data.text));
}

// Whether SDL (so the screen) can use a mode of this size at 256 colours.
static bool Have_Mode(SDL_Rect** modes, int w, int h)
{
    if (modes == nullptr || modes == reinterpret_cast<SDL_Rect**>(-1)) {
        return true; // no list: offer them all
    }
    for (int i = 0; modes[i] != nullptr; ++i) {
        if (modes[i]->w == w && modes[i]->h == h) {
            return true;
        }
    }
    return false;
}

static void Add_Size(const std::string& choice, const char* text)
{
    if (SizeCount == MaxSizes) {
        return;
    }
    SizeChoices[SizeCount] = choice;
    snprintf(SizeText[SizeCount], sizeof(SizeText[SizeCount]), "%s", text);
    ++SizeCount;
}

static void Add_Size(int w, int h)
{
    char text[32];
    snprintf(text, sizeof(text), "%d\xD7%d", w, h); // a multiplication sign, in Latin-1
    Add_Size(std::to_string(w) + "x" + std::to_string(h), text);
}

/*
** The sizes: the automatic one first, then those of the usual ones the monitor has, then low
** resolution. A size set in the settings file by hand is kept in the list too.
*/
static void Build_Size_List(void)
{
    SizeCount = 0;
    int dw, dh;
    Choice_Size("", dw, dh);
    char text[32];
    snprintf(text, sizeof(text), "Automatic (%d\xD7%d)", dw, dh);
    Add_Size("", text);

    SDL_PixelFormat format;
    memset(&format, 0, sizeof(format));
    format.BitsPerPixel = 8;
    format.BytesPerPixel = 1;
    SDL_Rect** modes = SDL_ListModes(&format, SDL_FULLSCREEN | SDL_HWSURFACE);

    static const int Presets[][2] = {{640, 400}, {640, 480}, {800, 600}, {1024, 768}, {1280, 800}, {1280, 1024}};
    std::string saved = Saved_Choice();
    bool saved_listed = saved.empty() || saved == LowResChoice;
    for (const auto& size : Presets) {
        std::string choice = std::to_string(size[0]) + "x" + std::to_string(size[1]);
        if ((size[0] == dw && size[1] == dh) || !Have_Mode(modes, size[0], size[1])) {
            continue;
        }
        Add_Size(size[0], size[1]);
        saved_listed = saved_listed || choice == saved;
    }
    int sw, sh;
    Choice_Size(saved, sw, sh);
    if (!saved_listed && sw > 0 && sh > 0) {
        Add_Size(sw, sh);
    }
    Add_Size(LowResChoice, "320\xD7" "200 (low res)");
}

// The size menu's ticks and shading, for the size in use now.
static void Update_Size_Menu(void)
{
    std::string current = Saved_Choice();
    bool forced = ForcedLowRes || Low_Res_Variable();
    if (forced) {
        current = LowResChoice;
    }
    for (int i = 0; i < SizeCount; ++i) {
        MenuItem& item = SizeMenu.items[i];
        item.flags = (SizeChoices[i] == current ? ItemTicked : 0) | (i == SizeCount - 1 ? ItemLast : 0);
        item.icon_flags = ItemText | ItemIndirected | (forced && SizeChoices[i] != LowResChoice ? ItemShaded : 0);
    }
}

static void Build_Size_Menu(void)
{
    Build_Size_List();
    memset(&SizeMenu, 0, sizeof(SizeMenu));
    Menu_Header(SizeMenu.title, SizeMenu.colours, SizeMenu.width, SizeMenu.height, SizeMenu.gap, "Screen size");
    for (int i = 0; i < SizeCount; ++i) {
        MenuItem& item = SizeMenu.items[i];
        item.submenu = -1;
        item.data.ind.buffer = SizeText[i];
        item.data.ind.validation = -1;
        item.data.ind.length = int(strlen(SizeText[i])) + 1;
        int width = (int(strlen(SizeText[i])) + 2) * 16;
        if (width > SizeMenu.width) {
            SizeMenu.width = width;
        }
    }
    Update_Size_Menu();
}

/*
** The icon bar menu: Info (leading to the Info window), Help, Screen size and Quit.
*/
enum
{
    MenuInfo,
    MenuHelp,
    MenuSize,
    MenuQuit,
};

static void Build_Menu(void)
{
    memset(&Menu, 0, sizeof(Menu));
    Menu_Header(Menu.title, Menu.colours, Menu.width, Menu.height, Menu.gap, App.name);
    Plain_Item(Menu.items[MenuInfo], "Info", InfoWindow);
    Plain_Item(Menu.items[MenuHelp], "Help", -1);
    Plain_Item(Menu.items[MenuSize], "Screen size", reinterpret_cast<int>(&SizeMenu));
    Plain_Item(Menu.items[MenuQuit], "Quit", -1);
    Menu.items[MenuQuit].flags = ItemLast;
    int width = (int(strlen("Screen size")) + 3) * 16; // and the submenu arrow
    if (width > Menu.width) {
        Menu.width = width;
    }
    Build_Size_Menu();
}

static void Open_Menu(int mouse_x)
{
    Update_Size_Menu();
    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(&Menu);
    regs.r[2] = mouse_x - 64;
    regs.r[3] = 96 + 4 * 44; // an icon bar menu sits just above the icon bar
    Swi(Wimp_CreateMenu, regs);
}

static void Put_Icon_On_Bar(void)
{
    struct
    {
        int window;
        Icon icon;
    } block;
    block.window = -1; // the right of the icon bar, with the applications
    Plain_Text(block.icon, 0, 0, 68, 68, 0x1700301A, App.sprite); // sprite, centred, click
    _kernel_swi_regs regs;
    regs.r[0] = 0;
    regs.r[1] = reinterpret_cast<int>(&block);
    if (Swi(Wimp_CreateIcon, regs)) {
        IconHandle = regs.r[0];
    }
}

static void Quit_Game(void)
{
    fprintf(stderr, "desktop: quit from the icon bar\n");
    if (AudioPaused) {
        Mixer_Pause(false);
    }
    Prog_End(nullptr, false); // exiting with the sound still open fails in SDL_Quit's audio
    exit(0); // SDL_Quit (atexit) closes the task, which takes the icon away
}

/*
** Quits, and has the Filer start the game again. Filer_Run only queues the run: the Filer
** starts it when it next gets control, after this task has gone, so the two copies are never
** in memory together. The new one goes straight into the game (RISCOS_Desktop_Start).
*/
static void Restart_Game(void)
{
    const char* dir = getenv(App_Variable("Dir").c_str());
    if (dir == nullptr) {
        fprintf(stderr, "desktop: no %s to restart from\n", App_Variable("Dir").c_str());
        Quit_Game();
    }
    std::string command = std::string("Filer_Run ") + dir;
    fprintf(stderr, "desktop: restarting (%s)\n", command.c_str());
    _kernel_swi_regs regs;
    std::string restart = App_Variable("Restart");
    regs.r[0] = reinterpret_cast<int>(restart.c_str());
    regs.r[1] = reinterpret_cast<int>("1");
    regs.r[2] = 1;
    regs.r[3] = 0;
    regs.r[4] = 0; // a string
    Swi(OS_SetVarVal, regs);
    regs.r[0] = reinterpret_cast<int>(command.c_str());
    Swi(OS_CLI, regs);
    Quit_Game();
}

/*
** While the game waits on the desktop its memory is paged out, but the handlers UnixLib
** installed stay. An error raised by another task (seen: the Filer's) or an enabled event (a key
** or mouse button press) then ran whatever that task had at the game's addresses, in SVC mode,
** and could corrupt the system heap. Around Wimp_Poll the handlers are the OS's defaults: the
** exceptions, error, callback, escape and event. The game's are put back after.
*/
struct HandlerType
{
    int Address;
    int Workspace;
    int Buffer;
};

// OS_ChangeEnvironment numbers: undefined instruction, prefetch abort, data abort, address
// exception, error, callback, escape, event.
static const int Swapped_Handlers[] = {1, 2, 3, 4, 6, 7, 9, 10};
static const int Swapped_Count = sizeof(Swapped_Handlers) / sizeof(Swapped_Handlers[0]);

static HandlerType Set_Handler(int number, const HandlerType& handler)
{
    _kernel_swi_regs regs;
    regs.r[0] = number;
    regs.r[1] = handler.Address;
    regs.r[2] = handler.Workspace;
    regs.r[3] = handler.Buffer;
    _kernel_swi(OS_ChangeEnvironment, &regs, &regs);
    HandlerType previous = {regs.r[1], regs.r[2], regs.r[3]};
    return previous;
}

// Puts the OS's default handlers in, keeping the game's in saved.
static void Use_Default_Handlers(HandlerType saved[])
{
    for (int i = 0; i < Swapped_Count; i++) {
        _kernel_swi_regs regs;
        regs.r[0] = Swapped_Handlers[i];
        _kernel_swi(OS_ReadDefaultHandler, &regs, &regs);
        HandlerType handler = {regs.r[1], regs.r[2], regs.r[3]};
        saved[i] = Set_Handler(Swapped_Handlers[i], handler);
    }
}

static void Use_Game_Handlers(const HandlerType saved[])
{
    for (int i = 0; i < Swapped_Count; i++) {
        Set_Handler(Swapped_Handlers[i], saved[i]);
    }
}

/*
** The way out of the game, whatever the cause (menu Quit, Message_Quit, the game's own exit, an error): runs
** first of the exit handlers. UnixLib's thread ticker (an OS_CallEvery that sets a callback every 2 cs, running
** __pthread_callback) was still running while SDL_Quit closed the Wimp task, and rarely a callback then ran with
** the task's memory gone (prefetch abort in SVC mode at the game's address, in another task's time: seen on a
** Pi 4 after menu-click Quit of RA, about 2 in 45). So: the audio and timer threads are stopped and joined (they
** need the ticker to be joined), then the ticker is stopped, then the video is closed.
*/
void RISCOS_Exit_Cleanly(void)
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    fprintf(stderr, "exit: closing the audio and timer threads\n");
    SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
    fprintf(stderr, "exit: stopping UnixLib's thread ticker\n");
    __pthread_stop_ticker();
    // Every handler UnixLib put in is handed back to the OS's (as around Wimp_Poll), so that nothing the OS or the
    // Wimp does after the task's memory is gone (a pending callback, an event) can run at the game's addresses.
    HandlerType ours[Swapped_Count];
    Use_Default_Handlers(ours);
    fprintf(stderr, "exit: handlers handed back; closing the task (SDL_Quit)\n");
    SDL_Quit();
    fprintf(stderr, "exit: done\n");
}

/*
** Messages go in a small window of the game's own rather than Wimp_ReportError. An error box opened
** by the game while it waited on the desktop left the desktop unredrawn after it closed (the box
** stayed on the screen and the icon bar went blank), on RISC OS 4.39 and 5.30 alike and even once
** the game had quit; a BASIC task's boxes were fine. A window also leaves the desktop working,
** which suits an install running meanwhile. Return picks the first button, Escape the last.
*/
enum MessageAction
{
    ActionNone,
    ActionRestart,         // the first button saves PendingChoice as the screen size and restarts
    ActionInstall,         // the first installs with the movies, the second without
    ActionInstallDataOnly, // the first installs without the movies (there's no room for them)
    ActionInstallMovies,   // the first installs the movies (the game data is already there)
};

static const int MessageWidth = 1120;
static const int MaxLines = 14;
static int MessageWindow = -1;
static int MessageButtons = 0;   // icons 0 .. MessageButtons - 1
static MessageAction PendingAction = ActionNone;
static std::string PendingChoice;
static char MessageLines[MaxLines][100];
static const int MaxButtons = 3;
static char ButtonText[MaxButtons][28];
static char SpriteName[16];
static int ProgressIcon = -1; // the line that shows how an install is going, or -1

static void Close_Message(void)
{
    if (MessageWindow < 0) {
        return;
    }
    int block[1] = {MessageWindow};
    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(block);
    Swi(Wimp_DeleteWindow, regs);
    MessageWindow = -1;
}

// Splits text into lines of at most width characters, at spaces.
static int Wrap(const std::string& text, size_t width)
{
    int lines = 0;
    size_t pos = 0;
    while (pos < text.size() && lines < MaxLines) {
        size_t end = text.size();
        if (end - pos > width) {
            end = text.rfind(' ', pos + width);
            if (end == std::string::npos || end <= pos) {
                end = pos + width;
            }
        }
        snprintf(MessageLines[lines++], sizeof(MessageLines[0]), "%s", text.substr(pos, end - pos).c_str());
        pos = end;
        while (pos < text.size() && text[pos] == ' ') {
            pos++;
        }
    }
    return lines;
}

static bool Wimp_Has_Sprite(const char* name)
{
    _kernel_swi_regs regs;
    regs.r[0] = 40; // read sprite information
    regs.r[2] = reinterpret_cast<int>(name);
    return _kernel_swi(Wimp_SpriteOp, &regs, &regs) == nullptr;
}

/*
** Opens the message window, in the middle of the screen and in front: the text, the Wimp's
** "error" or "information" sprite if it has one, and one or two buttons (the first is the default).
*/
static void Show_Message(const std::string& text, bool error, const std::vector<std::string>& buttons,
                         bool progress = false)
{
    Close_Message();
    fprintf(stderr, "desktop: message: %s\n", text.c_str());
    int lines = Wrap(text, 58);
    ProgressIcon = -1;
    if (progress && lines < MaxLines) {
        MessageLines[lines++][0] = '\0'; // filled in by Show_Progress
    }
    snprintf(SpriteName, sizeof(SpriteName), "%s", error ? "error" : "information");
    bool sprite = Wimp_Has_Sprite(SpriteName);
    int text_x = sprite ? 128 : 32;
    int buttons_y = -(32 + 44 * (lines > 2 ? lines : 2) + 28);
    int height = -buttons_y + 68 + 28;

    static struct
    {
        int visible[4];
        int scroll[2];
        int behind;
        unsigned flags;
        unsigned char colours[8];
        int extent[4];
        unsigned title_flags;
        unsigned button_type;
        int sprite_area;
        short min_w, min_h;
        struct
        {
            const char* buffer;
            const char* validation;
            int length;
        } title;
        int icon_count;
        Icon icons[1 + MaxButtons + MaxLines];
    } block;
    memset(&block, 0, sizeof(block));
    block.behind = -1;
    block.flags = 0x84000012; // moveable, auto-redraw, title bar, new format; kept on the screen
    const unsigned char colours[8] = {7, 2, 7, 1, 12, 14, 12, 0};
    memcpy(block.colours, colours, sizeof(colours));
    block.extent[1] = -height;
    block.extent[2] = MessageWidth;
    block.title_flags = 0x0000013D;
    block.sprite_area = 1;
    block.title.buffer = App.name;
    block.title.validation = reinterpret_cast<const char*>(-1);
    block.title.length = int(strlen(App.name)) + 1;

    int n = 0;
    MessageButtons = buttons.size() < size_t(MaxButtons) ? int(buttons.size()) : MaxButtons;
    static const char default_valid[] = "R6,3";
    static const char action_valid[] = "R5,3";
    // Left to right, ending at the right edge; the first is the default (Return).
    int widths[MaxButtons], total = 0;
    for (int i = 0; i < MessageButtons; i++) {
        snprintf(ButtonText[i], sizeof(ButtonText[i]), "%s", buttons[i].c_str());
        widths[i] = int(strlen(ButtonText[i])) * 16 + 64;
        if (widths[i] < 216) {
            widths[i] = 216;
        }
        total += widths[i] + (i > 0 ? 16 : 0);
    }
    int x0 = MessageWidth - 32 - total;
    for (int i = 0; i < MessageButtons; i++) {
        Indirected(block.icons[n++], x0, buttons_y - 68, x0 + widths[i], buttons_y, 0x1700313D, ButtonText[i],
                   i == 0 ? default_valid : action_valid);
        x0 += widths[i] + 16;
    }
    for (int i = 0; i < lines; i++) {
        int top = -32 - 44 * i;
        if (progress && i == lines - 1) {
            ProgressIcon = n;
        }
        Indirected(block.icons[n++], text_x, top - 44, MessageWidth - 32, top, 0x07000111, MessageLines[i],
                   reinterpret_cast<const char*>(-1));
        block.icons[n - 1].data.ind.length = sizeof(MessageLines[i]);
    }
    if (sprite) {
        Indirected(block.icons[n++], 32, -32 - 76, 32 + 76, -32, 0x1700011A, SpriteName,
                   reinterpret_cast<const char*>(1)); // sprite name, in the Wimp's pool
        block.icons[n - 1].data.ind.length = int(strlen(SpriteName)) + 1;
    }
    block.icon_count = n;

    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(&block);
    if (!Swi(Wimp_CreateWindow, regs)) {
        return;
    }
    MessageWindow = regs.r[0];

    // In the middle of the screen, in front, with the (invisible) caret so it gets the keys.
    int size[4];
    static const int vars[] = {4, 5, 11, 12, -1};
    regs.r[0] = reinterpret_cast<int>(vars);
    regs.r[1] = reinterpret_cast<int>(size);
    int screen_w = 1600, screen_h = 1200;
    if (_kernel_swi(OS_ReadVduVariables, &regs, &regs) == nullptr) {
        screen_w = (size[2] + 1) << size[0];
        screen_h = (size[3] + 1) << size[1];
    }
    int open[8];
    open[0] = MessageWindow;
    open[1] = (screen_w - MessageWidth) / 2;
    open[2] = (screen_h - height) / 2;
    open[3] = open[1] + MessageWidth;
    open[4] = open[2] + height;
    open[5] = 0;
    open[6] = 0;
    open[7] = -1;
    regs.r[1] = reinterpret_cast<int>(open);
    Swi(Wimp_OpenWindow, regs);
    regs.r[0] = MessageWindow;
    regs.r[1] = -1;
    regs.r[2] = 0;
    regs.r[3] = 0;
    regs.r[4] = 1 << 25; // invisible
    regs.r[5] = -1;
    Swi(Wimp_SetCaretPosition, regs);
}

// Shows how the install is going on the message window's progress line, if it's open.
static void Show_Progress(const std::string& text)
{
    if (MessageWindow < 0 || ProgressIcon < 0) {
        return;
    }
    // The icon's own buffer is one of MessageLines: change it, then have the icon redrawn.
    struct
    {
        int window, icon;
        Icon info;
    } state;
    state.window = MessageWindow;
    state.icon = ProgressIcon;
    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(&state);
    if (!Swi(Wimp_GetIconState, regs)) {
        return;
    }
    char* buffer = const_cast<char*>(state.info.data.ind.buffer);
    if (strcmp(buffer, text.c_str()) == 0) {
        return;
    }
    snprintf(buffer, sizeof(MessageLines[0]), "%s", text.c_str());
    int set[4] = {MessageWindow, ProgressIcon, 0, 0}; // no change to its flags: just redraw it
    regs.r[1] = reinterpret_cast<int>(set);
    Swi(Wimp_SetIconState, regs);
}

static void Install_Choice(MessageAction action, int button);

// A button of the message window (0 the first), or Escape or its close icon (the last).
static void Message_Choice(int button)
{
    Close_Message();
    MessageAction action = PendingAction;
    PendingAction = ActionNone;
    if (action == ActionRestart && button == 0) {
        Save_Choice(PendingChoice);
        Restart_Game();
    }
    if (action == ActionInstall || action == ActionInstallDataOnly || action == ActionInstallMovies) {
        Install_Choice(action, button);
    }
}

// A message with OK, saying something went wrong (error) or how things are.
static void Tell(const std::string& text, bool error)
{
    PendingAction = ActionNone;
    Show_Message(text, error, {"OK"});
}

// Asks before restarting for a new screen size; Restart saves it and restarts the game.
static void Ask_Restart(const std::string& choice)
{
    Show_Message("Changing the screen size restarts the game, and the game in progress will be lost. "
                 "Save it first if you want to keep it.",
                 false, {"Restart", "Cancel"});
    PendingAction = ActionRestart;
    PendingChoice = choice;
}

// A size chosen from the menu: saved at the start; once in the game, a restart if it differs.
static void Choose_Size(int index)
{
    if (index < 0 || index >= SizeCount || ForcedLowRes || Low_Res_Variable()) {
        return;
    }
    const std::string& choice = SizeChoices[index];
    if (!InGame) {
        Save_Choice(choice);
        return;
    }
    int w, h;
    Choice_Size(choice, w, h);
    if (w == ScreenW && h == ScreenH) {
        Save_Choice(choice); // the same screen, e.g. the default chosen by its size
        return;
    }
    Ask_Restart(choice);
}

/*
** The game data: whether it's installed, and installing it from CD images dragged onto the icon.
*/
static bool Installed_Now = false; // installed while this copy waited: a click restarts the game
static bool Installing = false;
static int InstallTask = 0;         // the task window's task, once seen
static unsigned InstallStarted = 0; // when it was started, in centiseconds
static std::vector<std::string> Pending; // images to install, once the question about the movies is answered
static std::vector<std::string> ToLook;  // files dropped, to be looked at once the drag has all arrived
static unsigned LookSince = 0;
static int InstallDiscs = 0;             // the discs being installed (1 GDI or Allied, 2 Nod or Soviet)
static bool InstallMovies = true;

static unsigned Now(void)
{
    _kernel_swi_regs regs;
    _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
    return unsigned(regs.r[0]);
}

// The problem with the installed data that !Run found (or vcprep, after installing), or "".
static std::string Data_Problem(void)
{
    if (!Os_Variable(App_Variable("NoDataCheck")).empty()) {
        return "";
    }
    return Os_Variable(App_Variable("DataProblem"));
}

static void Open_Help(void)
{
    std::string command = "Filer_Run " + Os_Variable(App_Variable("Dir")) + ".!Help";
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(command.c_str());
    Swi(OS_CLI, regs);
}

static std::string Own_App(void)
{
    return std::string("!") + App.prefix;
}

// The task window's name, which the Task Manager lists it by.
static std::string Install_Task_Name(void)
{
    return std::string(App.name) + " install";
}

static std::string Data_Dir(void);

// A click on the icon while the data isn't there. !Help isn't opened: most people won't need it.
static void Explain_No_Data(void)
{
    if (Installing) {
        Tell("The game data is being installed. Click the icon when that has finished.", false);
        return;
    }
    std::string problem = Data_Problem();
    std::string data = Data_Dir();
    _kernel_swi_regs regs;
    regs.r[0] = 17;
    regs.r[1] = reinterpret_cast<int>(data.c_str());
    bool nothing = _kernel_swi(OS_File, &regs, &regs) != nullptr || regs.r[0] != 2; // no gamedata yet
    Tell(std::string(App.purpose) + (nothing ? "'s game data isn't installed yet." : "'s game data is incomplete (" + problem + ").")
             + " Drag the CD images (ISO files) onto this icon to install it. !Help says where to get them: choose "
               "Help on this icon's menu, or open !Help in the " + Own_App() + " directory.",
         false);
}


static bool Is_RA(void)
{
    return strcmp(App.prefix, "VanillaRA") == 0;
}

// Where the installed data goes: the gamedata directory in the application. Deleting it uninstalls it.
static std::string Data_Dir(void)
{
    return Os_Variable(App_Variable("Dir")) + ".gamedata";
}

// An installed file's size, by where it goes in gamedata (MIX.SCORES, gdi.MIX.GENERAL), or -1.
static long Installed_Size(const std::string& dest)
{
    std::string path = Data_Dir() + "." + dest;
    _kernel_swi_regs regs;
    regs.r[0] = 17;
    regs.r[1] = reinterpret_cast<int>(path.c_str());
    if (_kernel_swi(OS_File, &regs, &regs) != nullptr || regs.r[0] != 1) {
        return -1;
    }
    return long(unsigned(regs.r[4]));
}

static std::string Known_Dest(const Known& k, const char* side)
{
    return k.dest[0] == '@' ? std::string(side) + (k.dest + 1) : std::string(k.dest);
}

// Whether vcprep would copy a file: not if it's there at its size (CONQUER.INI: if it's there).
static bool Will_Copy(const Known& k, const char* side)
{
    long size = Installed_Size(Known_Dest(k, side));
    return strcmp(k.name, "CONQUER.INI") == 0 ? size < 0 : size != long(k.size);
}

static const int GameDiscs = 3; // both bits: GDI and Nod, or Allied and Soviet

/*
** The bytes installing these discs would add, as vcprep would copy them (a file both discs hold,
** once). Tiberian Dawn: the files both discs share, and each disc's GENERAL, then its MOVIES.
** Red Alert: REDALERT and, without the movies, a MAIN with them left out that does for both
** discs; with them, each disc's own MAIN.
*/
static double Install_Bytes(int discs, bool movies)
{
    std::vector<std::string> planned;
    double bytes = 0;
    auto add = [&](const Known& k, const char* side) {
        std::string dest = Known_Dest(k, side);
        for (const std::string& p : planned) {
            if (p == dest) {
                return;
            }
        }
        planned.push_back(dest);
        if (Will_Copy(k, side)) {
            bytes += k.size;
        }
    };
    for (int d = 0; d < 2; d++) {
        if (!(discs & (1 << d))) {
            continue;
        }
        if (Is_RA()) {
            add(RA_Redalert, "");
            add(movies ? RA_Discs[d].main : RA_No_Movies, "");
        } else {
            for (size_t k = 0; k < DISC_FILES_COUNT; k++) {
                add(Disc_Files[k], "");
            }
            for (size_t k = 0; k < ARCHIVE_FILES_COUNT; k++) {
                add(Archive_Files[k], "");
            }
            add(Sides[d].general, Sides[d].dir);
            if (movies) {
                add(Sides[d].movies, Sides[d].dir);
            }
        }
    }
    return bytes;
}

// A disc's game data is installed (without movies, one Red Alert disc does for both).
static bool Disc_Installed(int d)
{
    if (Is_RA()) {
        return Installed_Size(RA_Redalert.dest) == long(RA_Redalert.size)
               && (Installed_Size(RA_No_Movies.dest) == long(RA_No_Movies.size)
                   || Installed_Size(RA_Discs[d].main.dest) == long(RA_Discs[d].main.size));
    }
    for (size_t k = 0; k < DISC_FILES_COUNT; k++) {
        if (Installed_Size(Disc_Files[k].dest) != long(Disc_Files[k].size)) {
            return false;
        }
    }
    for (size_t k = 0; k < ARCHIVE_FILES_COUNT; k++) {
        if (strcmp(Archive_Files[k].name, "CONQUER.INI") != 0
            && Installed_Size(Archive_Files[k].dest) != long(Archive_Files[k].size)) {
            return false;
        }
    }
    return Installed_Size(Known_Dest(Sides[d].general, Sides[d].dir)) == long(Sides[d].general.size);
}

static bool Movies_Installed(int d)
{
    if (Is_RA()) {
        return Installed_Size(RA_Discs[d].main.dest) == long(RA_Discs[d].main.size);
    }
    return Installed_Size(Known_Dest(Sides[d].movies, Sides[d].dir)) == long(Sides[d].movies.size);
}

// Free space where the data goes, in bytes; <App>$TestFree (in MB) pretends, for testing.
static double Free_Space(void)
{
    std::string test = Os_Variable(App_Variable("TestFree"));
    if (!test.empty()) {
        return atof(test.c_str()) * 1024 * 1024;
    }
    std::string where = Data_Dir();
    _kernel_swi_regs regs;
    regs.r[0] = 17;
    regs.r[1] = reinterpret_cast<int>(where.c_str());
    if (_kernel_swi(OS_File, &regs, &regs) != nullptr || regs.r[0] != 2) {
        where = Os_Variable(App_Variable("Dir")); // no gamedata yet
    }
    regs.r[0] = 55; // FreeSpace64: discs over 4 GB
    regs.r[1] = reinterpret_cast<int>(where.c_str());
    if (_kernel_swi(OS_FSControl, &regs, &regs) == nullptr) {
        return double(unsigned(regs.r[0])) + double(unsigned(regs.r[1])) * 4294967296.0;
    }
    regs.r[0] = 49; // FreeSpace: older filing systems (RISC OS 3.7 and 4)
    regs.r[1] = reinterpret_cast<int>(where.c_str());
    if (_kernel_swi(OS_FSControl, &regs, &regs) == nullptr) {
        return double(unsigned(regs.r[0]));
    }
    return -1;
}

// The disc the application is on, for messages: SCSI::HardDisc0, say.
static std::string Disc_Name(void)
{
    std::string dir = Os_Variable(App_Variable("Dir"));
    char buffer[512];
    _kernel_swi_regs regs;
    regs.r[0] = 37; // canonicalise
    regs.r[1] = reinterpret_cast<int>(dir.c_str());
    regs.r[2] = reinterpret_cast<int>(buffer);
    regs.r[3] = 0;
    regs.r[4] = 0;
    regs.r[5] = sizeof(buffer);
    if (_kernel_swi(OS_FSControl, &regs, &regs) == nullptr) {
        dir = buffer;
    }
    size_t dollar = dir.find(".$");
    return dollar != std::string::npos ? dir.substr(0, dollar) : dir;
}

// A size for a message, with a hard space so that the window never splits "144 MB".
static std::string Megabytes(double bytes, bool round_up = false)
{
    char text[32];
    double mb = bytes / (1024 * 1024);
    if (mb >= 10240 && !round_up) {
        snprintf(text, sizeof(text), "%.1f\xA0GB", mb / 1024);
    } else {
        snprintf(text, sizeof(text), "%.0f\xA0MB", round_up ? ceil(mb) : mb < 1 ? 1 : floor(mb + 0.5));
    }
    return text;
}

// "the C&C95 GDI disc", "the C&C95 GDI and Nod discs", "the Red Alert Allied disc".
static std::string Disc_List(int discs)
{
    const char* names[2] = {Is_RA() ? "Allied" : "GDI", Is_RA() ? "Soviet" : "Nod"};
    std::string text = std::string("the ") + (Is_RA() ? "Red Alert " : "C&C95 ");
    if (discs == GameDiscs) {
        return text + names[0] + " and " + names[1] + " discs";
    }
    return text + names[discs & 1 ? 0 : 1] + " disc";
}

static int Disc_Bit(int type)
{
    const char* id = Disc_Types[type].id;
    return strcmp(id, "td-gdi") == 0 || strcmp(id, "ra-allied") == 0 ? 1
           : strcmp(id, "td-nod") == 0 || strcmp(id, "ra-soviet") == 0 ? 2 : 0;
}

/*
** Message_DataLoad on the icon: a file dragged onto it from the Filer. Nothing is read here: the
** window says it's being looked at straight away, and the files are read on a null event once
** the drag has all arrived (the Filer sends one message per file) and the window is on the
** screen (a disc that has to spin up takes a moment).
*/
static void Data_Load(int* block)
{
    std::string file = reinterpret_cast<const char*>(block) + 44;
    int filetype = block[10];
    std::string leaf = file.substr(file.find_last_of('.') + 1);

    // DataLoadAck, so the Filer knows it arrived (the file is only read, never moved).
    block[3] = block[2];
    block[4] = 4;
    _kernel_swi_regs regs;
    regs.r[0] = 17;
    regs.r[1] = reinterpret_cast<int>(block);
    regs.r[2] = block[1];
    Swi(Wimp_SendMessage, regs);

    fprintf(stderr, "desktop: %s dragged to the icon\n", file.c_str());
    if (filetype == 0x1000 || filetype == 0x2000) {
        Tell(leaf + " is a directory. Drag the CD image files (ISO files) themselves onto the icon.", true);
        return;
    }
    if (Installing) {
        Tell("The game data is being installed. Drag more CD images here when that has finished.", false);
        return;
    }
    // Any filetype is taken: a disc is known by its contents. Zipped images (as downloaded)
    // aren't read yet, so they get their own message.
    if (filetype == 0xDDC) {
        Tell(leaf + " is a zip file. Unzip it first, then drag the CD image (ISO file) inside it onto the icon.", true);
        return;
    }
    if (ToLook.empty()) {
        PendingAction = ActionNone;
        Show_Message("Looking at " + leaf + "...", false, {"Cancel"});
    }
    ToLook.push_back(file);
    LookSince = Now();
}

// The question asked about the discs in Pending, with the buttons there's room for.
static void Ask_About_Movies(const std::string& intro)
{
    double data = Install_Bytes(InstallDiscs, false);
    double with = Install_Bytes(InstallDiscs, true);
    double movies = with > data ? with - data : 0;
    double free = Free_Space();
    const double margin = 2.0 * 1024 * 1024;
    std::string where = Disc_Name();
    std::string free_text = free >= 0 ? Megabytes(free) : std::string("unknown");
    fprintf(stderr, "desktop: to install: data %.0f, with the movies %.0f, free %.0f bytes\n", data, with, free);
    if (data <= 0 && movies <= 0) {
        Pending.clear();
        Tell(intro + " Its game data and movies are already installed.", false);
        return;
    }
    if (data <= 0) {
        if (free >= 0 && free < with + margin) {
            Pending.clear();
            Tell(intro + " Its game data is already installed. Its movies take up " + Megabytes(movies) + ", and "
                     + where + " has only " + free_text + " free.",
                 true);
            return;
        }
        Show_Message(intro + " Its game data is already installed; its movies take up " + Megabytes(movies)
                         + ". Free on " + where + ": " + free_text + ". Install the movies?",
                     false, {"Install the movies", "Cancel"});
        PendingAction = ActionInstallMovies;
        return;
    }
    if (free >= 0 && free < data + margin) {
        Pending.clear();
        Tell(intro + " There isn't room for it on " + where + ": its game data needs " + Megabytes(data, true)
                 + ", and only " + free_text + " is free. Free some space, then drag the CD image onto this icon again.",
             true);
        return;
    }
    std::string sizes = "Game data: " + Megabytes(data) + ". Movies: " + Megabytes(movies) + ". Free on " + where
                        + ": " + free_text + ".";
    if (movies <= 0) {
        Show_Message(intro + " " + sizes + " Its movies are already installed.", false, {"Install", "Cancel"});
        PendingAction = ActionInstallDataOnly;
        return;
    }
    if (free >= 0 && free < with + margin) {
        double short_by = with + margin - free; // the movies, less what's left after the game data
        Show_Message(intro + " " + sizes + " After installing the levels, you need to free up "
                         + Megabytes(short_by, true) + " if you want to install the movies: they take up "
                         + Megabytes(movies) + ".",
                     true, {"Game data only", "Cancel"});
        PendingAction = ActionInstallDataOnly;
        return;
    }
    Show_Message(intro + " " + sizes + " Install the game data only, or the movies too? Without the movies the game "
                     "skips them.",
                 false, {"Game data and movies", "Game data only", "Cancel"});
    PendingAction = ActionInstall;
}

// Once the drag has all arrived: which discs the dropped files are, and what to do with them.
static void Look_At_Drops(void)
{
    std::vector<std::string> files;
    files.swap(ToLook);
    std::string problems, intro;
    int discs = 0;
    for (const std::string& file : files) {
        std::string leaf = file.substr(file.find_last_of('.') + 1);
        int cut = 0;
        char volume[40];
        int disc = Disc_Identify(file.c_str(), &cut, volume, sizeof(volume));
        fprintf(stderr, "desktop: %s is %s%s (volume %s)\n", leaf.c_str(),
                disc >= 0 ? Disc_Types[disc].id : disc == DISC_NOT_ISO ? "not a CD image" : "an unknown disc",
                cut ? ", cut short" : "", volume);
        std::string problem;
        if (disc == DISC_NOT_ISO) {
            problem = leaf + " isn't a CD image (ISO file). !Help says which ones to drag here.";
        } else if (cut) {
            problem = leaf + " is " + (disc >= 0 ? Disc_Types[disc].name : std::string("a CD image"))
                      + ", but cut short: its download or copy didn't finish. Get it again.";
        } else if (disc == DISC_UNKNOWN) {
            problem = leaf + " isn't one of the freeware discs this game installs from. !Help says which ones to get.";
        } else if (Disc_Types[disc].app == nullptr) {
            problem = leaf + " is " + Disc_Types[disc].name
                      + ", which won't work: this game needs the Windows 95 version (see !Help).";
        } else if (Own_App() != Disc_Types[disc].app) {
            problem = leaf + " is " + Disc_Types[disc].name + ": drag it onto the " + Disc_Types[disc].app + " icon instead.";
        } else if (discs & Disc_Bit(disc)) {
            continue; // the same disc twice
        } else {
            discs |= Disc_Bit(disc);
            Pending.push_back(file);
            intro += (intro.empty() ? "" : " ") + leaf + " is " + Disc_Types[disc].name + ".";
        }
        if (!problem.empty()) {
            problems += (problems.empty() ? "" : " ") + problem;
        }
    }
    Close_Message();
    if (discs == 0) {
        Tell(problems.empty() ? std::string("Nothing to install.") : problems, true);
        return;
    }
    InstallDiscs = discs;
    Ask_About_Movies(problems.empty() ? intro : problems + " " + intro);
}

/*
** Installs the images in Pending with Utils.vcprep in a task window, so the desktop goes on
** working meanwhile. The list goes in <App>$Install, one per line, as it may be too long for a
** command line. vcprep puts the data in the application's gamedata directory.
*/
static void Start_Install(bool movies)
{
    std::string list;
    for (size_t i = 0; i < Pending.size(); i++) {
        list += Pending[i] + "\n";
    }
    Pending.clear();
    std::string var = App_Variable("Install");
    Set_Os_Variable(var, list.c_str());
    std::string dir = Os_Variable(App_Variable("Dir"));
    std::string prepare = dir + ".Utils.vcprep -window -images " + var + (Is_RA() ? " -raapp " : " -app ") + dir;
    InstallMovies = movies;
    if (!movies) {
        prepare += " -nomovies";
    }
    std::string command = "TaskWindow \"" + prepare + "\" -wimpslot 6000K -name \"" + Install_Task_Name() + "\" -quit";
    fprintf(stderr, "desktop: %s\n", command.c_str());
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(command.c_str());
    _kernel_oserror* error = _kernel_swi(Wimp_StartTask, &regs, &regs);
    if (error != nullptr) {
        Tell(std::string("The installer couldn't be started in a task window: ") + error->errmess, true);
        return;
    }
    Installing = true;
    InstallTask = 0;
    InstallStarted = Now();
    PendingAction = ActionNone;
    Show_Message("Installing " + Disc_List(InstallDiscs) + (movies ? ", with the movies" : ", without the movies")
                     + ". This takes a few minutes per disc; the desktop carries on meanwhile.",
                 false, {"Hide"}, true);
    Show_Progress("Starting");
}

// The answer to the question about the movies (the button, 0 the first).
static void Install_Choice(MessageAction action, int button)
{
    bool go = button == 0 || (action == ActionInstall && button == 1);
    if (!go || Pending.empty()) {
        Pending.clear();
        ToLook.clear();
        return;
    }
    Start_Install(action == ActionInstallMovies || (action == ActionInstall && button == 0));
}

// The task window's task if it's running, or 0. Also spots it before its messages would.
static int Find_Install_Task(void)
{
    std::string name = Install_Task_Name();
    int buffer[16 * 4];
    _kernel_swi_regs regs;
    regs.r[0] = 0;
    do {
        regs.r[1] = reinterpret_cast<int>(buffer);
        regs.r[2] = sizeof(buffer);
        if (_kernel_swi(TaskManager_EnumerateTasks, &regs, &regs) != nullptr) {
            return 0;
        }
        int entries = (reinterpret_cast<int*>(regs.r[1]) - buffer) / 4;
        for (int i = 0; i < entries; i++) {
            const char* task = reinterpret_cast<const char*>(buffer[i * 4 + 1]);
            if (task != nullptr && strncmp(task, name.c_str(), name.size()) == 0
                && (task[name.size()] < ' ' || task[name.size()] == '\0')) {
                return buffer[i * 4];
            }
        }
    } while (regs.r[0] >= 0);
    return 0;
}

// The task window has gone: what does the data look like now?
static void Install_Finished(void)
{
    Installing = false;
    InstallTask = 0;
    std::string problem = Data_Problem();
    fprintf(stderr, "desktop: install finished: %s\n", problem.empty() ? "all there" : problem.c_str());
    int done = 0, failed = 0, missing = 0, without_movies = 0;
    for (int d = 0; d < 2; d++) {
        int bit = 1 << d;
        if (InstallDiscs & bit) {
            bool ok = Disc_Installed(d) && (!InstallMovies || Movies_Installed(d));
            (ok ? done : failed) |= bit;
        }
        if (!Disc_Installed(d)) {
            missing |= bit;
        } else if (!Movies_Installed(d)) {
            without_movies |= bit;
        }
    }
    std::string text;
    if (done != 0) {
        std::string list = Disc_List(done);
        list[0] = 'T';
        text = list + (done == GameDiscs ? "' data is installed" : "'s data is installed")
               + (InstallMovies ? ", with the movies." : ", without the movies.");
    }
    if (failed != 0) {
        text += (text.empty() ? "" : " ") + std::string("The data from ") + Disc_List(failed)
                + " didn't install completely" + (problem.empty() ? "" : " (" + problem + ")")
                + ". The install window said why, and InstallLog in the game's gamedata directory keeps the details. "
                  "Drag the CD image onto this icon to try again: what's already "
                  "installed is kept.";
    } else if (missing != 0) {
        text += " You can also install " + Disc_List(missing) + ": drag its CD image onto this icon.";
    } else if (Is_RA() && without_movies != 0) {
        text += " Without the movies, one disc is enough for both campaigns. To add a disc's movies, drag its CD "
                "image onto this icon again.";
    } else {
        text += std::string(" Both discs are installed") + (without_movies != 0 ? ", without all of the movies." : ".");
    }
    if (problem.empty()) {
        Installed_Now = true;
        text += " Click the icon to play.";
    }
    Tell(text, failed != 0);
}

// On a null event while installing, or while a drag is still arriving.
static void Install_Tick(void)
{
    if (!ToLook.empty() && !Installing && Now() - LookSince >= 30) {
        Look_At_Drops();
        return;
    }
    if (Installing) {
        // vcprep says the file it's copying, how far it has got and which file of how many. A
        // file Utils.ramix writes comes with its full size and where it's going, to watch it grow.
        std::string now = Os_Variable(App_Variable("Installing"));
        char name[40], path[512];
        int percent = 0, file = 0, files = 0;
        unsigned long size = 0;
        int got = sscanf(now.c_str(), "%39s %d %d %d %lu %511s", name, &percent, &file, &files, &size, path);
        if (got >= 4) {
            if (percent < 0 && got == 6 && size > 0) {
                _kernel_swi_regs regs;
                regs.r[0] = 17;
                regs.r[1] = reinterpret_cast<int>(path);
                if (_kernel_swi(OS_File, &regs, &regs) == nullptr && regs.r[0] == 1) {
                    percent = int(double(unsigned(regs.r[4])) * 100 / size);
                }
            }
            for (char* p = name; *p != '\0'; p++) {
                if (*p == '.') {
                    *p = '/';
                }
            }
            char text[100];
            snprintf(text, sizeof(text), "Copying %s: %d%% (file %d of %d)", name, percent < 0 ? 0 : percent, file,
                     files);
            Show_Progress(text);
        }
        int task = Find_Install_Task();
        if (task != 0) {
            InstallTask = task;
        } else if (InstallTask != 0 || Now() - InstallStarted > 3000) {
            Install_Finished(); // gone, or never seen after 30 s
        }
    }
}


/*
** Polls the Wimp until the icon is clicked with Select or Adjust (true), handling the icon's
** menu, the Info window and the desktop quitting.
*/
static void Wait_For_Icon_Click(void)
{
    int block[64];
    for (;;) {
        __pthread_stop_ticker(); // the game's threads mustn't run while other tasks are paged in
        _kernel_swi_regs regs;
        bool ticking = Installing || !ToLook.empty();
        regs.r[0] = ticking ? 0 : 1; // null events only to watch an install or a drop: else sleep
        regs.r[1] = reinterpret_cast<int>(block);
        regs.r[2] = int(Now() + (Installing ? 20 : 10)); // the progress, five times a second
        HandlerType game_handlers[Swapped_Count];
        Use_Default_Handlers(game_handlers);
        _kernel_oserror* error = _kernel_swi(ticking ? Wimp_PollIdle : Wimp_Poll, &regs, &regs);
        Use_Game_Handlers(game_handlers);
        __pthread_start_ticker();
        if (error != nullptr) {
            fprintf(stderr, "desktop: Wimp_Poll: %s\n", error->errmess);
            return;
        }

        switch (regs.r[0]) {
        case 0: // Null_Reason
            Install_Tick();
            break;

        case 1: { // Redraw_Window_Request: a window of the task's (the Info window) to redraw
            regs.r[1] = reinterpret_cast<int>(block);
            int more = 0;
            if (Swi(Wimp_RedrawWindow, regs)) {
                more = regs.r[0];
            }
            while (more) { // the Wimp draws its icons; nothing else to draw
                regs.r[1] = reinterpret_cast<int>(block);
                if (!Swi(Wimp_GetRectangle, regs)) {
                    break;
                }
                more = regs.r[0];
            }
            break;
        }

        case 2: // Open_Window_Request
            regs.r[1] = reinterpret_cast<int>(block);
            Swi(Wimp_OpenWindow, regs);
            break;

        case 3: // Close_Window_Request
            if (block[0] == MessageWindow) {
                Message_Choice(MessageButtons - 1);
                break;
            }
            regs.r[1] = reinterpret_cast<int>(block);
            Swi(Wimp_CloseWindow, regs);
            break;

        case 8: // Key_Pressed: the message window has the caret
            if (block[0] == MessageWindow && (block[6] == 13 || block[6] == 27)) {
                Message_Choice(block[6] == 13 ? 0 : MessageButtons - 1);
                break;
            }
            regs.r[0] = block[6];
            Swi(Wimp_ProcessKey, regs);
            break;

        case 6: // Mouse_Click: x, y, buttons, window, icon
            if (block[3] == MessageWindow && block[4] >= 0 && block[4] < MessageButtons && (block[2] & (4 | 1))) {
                Message_Choice(block[4]);
                break;
            }
            if (block[3] == -2 && block[4] == IconHandle) {
                if (block[2] & 2) {
                    Open_Menu(block[0]);
                } else if (block[2] & (4 | 1)) {
                    if (Installed_Now) {
                        Restart_Game(); // with the paths found now the data is there
                    }
                    if (Installing || !Data_Problem().empty()) {
                        Explain_No_Data();
                        break;
                    }
                    return;
                }
            }
            break;

        case 9: // Menu_Selection: the item, then the submenu's
            if (block[0] == MenuQuit) {
                Quit_Game();
            }
            if (block[0] == MenuHelp) {
                Open_Help();
            }
            if (block[0] == MenuSize) {
                Choose_Size(block[1]);
            }
            {
                // Adjust keeps the menu open.
                int pointer[5];
                regs.r[1] = reinterpret_cast<int>(pointer);
                if (Swi(Wimp_GetPointerInfo, regs) && (pointer[2] & 1)) {
                    Open_Menu(pointer[0] + 64);
                }
            }
            break;

        case 17: // User_Message
        case 18: // User_Message_Recorded
            if (block[4] == 0) { // Message_Quit
                Quit_Game();
            }
            if (block[4] == 3 && block[5] == -2 && block[6] == IconHandle) { // Message_DataLoad
                Data_Load(block);
            }
            if (block[4] == 0x400C3 && Installing && InstallTask != 0 && block[1] == InstallTask) {
                Install_Finished(); // Message_TaskCloseDown from the task window
            }
            break;

        default:
            break;
        }
    }
}

void RISCOS_Desktop_Start(void)
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    if (Settings.Video.Windowed) {
        InGame = true;
        return;
    }
    // SDL's video makes the game a Wimp task (the game's Set_Video_Mode does this again later).
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
    if (RISCOS_GetTaskHandle() == 0) {
        InGame = true;
        return;
    }
    std::string moved = Os_Variable(App_Variable("Moved"));
    if (!moved.empty()) {
        fprintf(stderr, "desktop: moved %s of game data from an earlier install into gamedata\n", moved.c_str());
        Set_Os_Variable(App_Variable("Moved"), nullptr);
    }
    Create_Info_Window();
    Build_Menu();
    Put_Icon_On_Bar();

    // A restart for a new screen size goes straight back into the game.
    std::string restart = App_Variable("Restart");
    bool restarted = getenv(restart.c_str()) != nullptr;
    if (restarted) {
        _kernel_swi_regs regs;
        regs.r[0] = reinterpret_cast<int>(restart.c_str());
        regs.r[1] = 0;
        regs.r[2] = -1; // delete it
        regs.r[3] = 0;
        regs.r[4] = 0;
        _kernel_swi(OS_SetVarVal, &regs, &regs);
    }

    // Files dragged to the icon, and task windows closing (a Wimp task gets only the messages it
    // asks for, as well as Message_Quit).
    static const int messages[] = {3, 0x400C3, 0};
    _kernel_swi_regs mregs;
    mregs.r[0] = reinterpret_cast<int>(messages);
    _kernel_swi(Wimp_AddMessages, &mregs, &mregs);

    // Without its data the game can't start: it waits for the CD images, whatever else was asked.
    bool no_data = !Data_Problem().empty();
    if (no_data) {
        fprintf(stderr, "desktop: no game data (%s)\n", Data_Problem().c_str());
    }
    // VC_NODESKTOP (for tests) skips the wait, but never when there is no data to play with.
    if (IconHandle >= 0
        && (no_data || (RISCOS_Desktop_Click_To_Start && !restarted && getenv("VC_NODESKTOP") == nullptr))) {
        fprintf(stderr, "desktop: waiting for a click on the icon bar\n");
        AudioPaused = Mixer_Is_Running(); // in case the sound is set up first
        if (AudioPaused) {
            Mixer_Pause(true);
        }
        Wait_For_Icon_Click();
        if (AudioPaused) {
            Mixer_Pause(false);
            AudioPaused = false;
        }
    }
    InGame = true;
}

void RISCOS_Desktop_Set_Screen(int width, int height, bool forced_low_res)
{
    ScreenW = width;
    ScreenH = height;
    ForcedLowRes = forced_low_res;
}

bool RISCOS_Desktop_Is_Leave_Key(int sdl_key, int sdl_mod)
{
    return sdl_key == SDLK_F12 && (sdl_mod & KMOD_SHIFT) && IconHandle >= 0;
}

void RISCOS_Desktop_Suspend(void)
{
    if (IconHandle < 0) {
        return;
    }
    fprintf(stderr, "desktop: left for the desktop\n");
    Focus_Loss();
    AudioPaused = Mixer_Is_Running();
    if (AudioPaused) {
        Mixer_Pause(true);
    }
    Video_Leave_For_Desktop();
    Wait_For_Icon_Click();
    Video_Return_From_Desktop();
    if (AudioPaused) {
        Mixer_Pause(false);
        AudioPaused = false;
    }
    Focus_Restore();
    fprintf(stderr, "desktop: back to the game\n");
}

#endif
