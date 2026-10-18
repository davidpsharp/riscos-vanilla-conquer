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
*/
#ifdef __riscos__

#include "riscos_desktop.h"
#include "riscos_autosize.h"
#include "ccfile.h"
#include "gitinfo.h"
#include "ini.h"
#include "mixer_sdl1.h"
#include "settings.h"

#include <SDL.h>
#include <kernel.h>
#include <pthread.h>
#include <swis.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    static char version[64];
    snprintf(version, sizeof(version), "r%d %s (%.10s)", GitRevision, GitShortSHA1, BuildStamp);

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

static MenuBlock<3> Menu;
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
** The icon bar menu: Info (leading to the Info window), Screen size and Quit.
*/
static void Build_Menu(void)
{
    memset(&Menu, 0, sizeof(Menu));
    Menu_Header(Menu.title, Menu.colours, Menu.width, Menu.height, Menu.gap, App.name);
    Plain_Item(Menu.items[0], "Info", InfoWindow);
    Plain_Item(Menu.items[1], "Screen size", reinterpret_cast<int>(&SizeMenu));
    Plain_Item(Menu.items[2], "Quit", -1);
    Menu.items[2].flags = ItemLast;
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
    regs.r[3] = 96 + 3 * 44; // an icon bar menu sits just above the icon bar
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

// Asks before restarting for a new screen size: true for Restart.
static bool Ask_Restart(void)
{
    struct
    {
        int number;
        char message[252];
    } error;
    error.number = 0;
    snprintf(error.message, sizeof(error.message),
             "Changing the screen size restarts the game, and the game in progress will be lost. "
             "Save it first if you want to keep it.");
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(&error);
    regs.r[1] = 2 | (1 << 8) | (4 << 9); // Cancel, new style, a question
    regs.r[2] = reinterpret_cast<int>(App.name);
    regs.r[3] = reinterpret_cast<int>(App.sprite);
    regs.r[4] = 1; // the Wimp's sprite area
    regs.r[5] = reinterpret_cast<int>("Restart");
    if (!Swi(Wimp_ReportError, regs)) {
        return false;
    }
    return regs.r[1] == 3; // the first extra button
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
    if (Ask_Restart()) {
        Save_Choice(choice);
        Restart_Game();
    }
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
** Polls the Wimp until the icon is clicked with Select or Adjust (true), handling the icon's
** menu, the Info window and the desktop quitting.
*/
static void Wait_For_Icon_Click(void)
{
    int block[64];
    for (;;) {
        __pthread_stop_ticker(); // the game's threads mustn't run while other tasks are paged in
        _kernel_swi_regs regs;
        regs.r[0] = 1; // no null events: sleep until something happens
        regs.r[1] = reinterpret_cast<int>(block);
        HandlerType game_handlers[Swapped_Count];
        Use_Default_Handlers(game_handlers);
        _kernel_oserror* error = _kernel_swi(Wimp_Poll, &regs, &regs);
        Use_Game_Handlers(game_handlers);
        __pthread_start_ticker();
        if (error != nullptr) {
            fprintf(stderr, "desktop: Wimp_Poll: %s\n", error->errmess);
            return;
        }

        switch (regs.r[0]) {
        case 2: // Open_Window_Request
            regs.r[1] = reinterpret_cast<int>(block);
            Swi(Wimp_OpenWindow, regs);
            break;

        case 3: // Close_Window_Request
            regs.r[1] = reinterpret_cast<int>(block);
            Swi(Wimp_CloseWindow, regs);
            break;

        case 6: // Mouse_Click: x, y, buttons, window, icon
            if (block[3] == -2 && block[4] == IconHandle) {
                if (block[2] & 2) {
                    Open_Menu(block[0]);
                } else if (block[2] & (4 | 1)) {
                    return;
                }
            }
            break;

        case 9: // Menu_Selection: the item, then the submenu's
            if (block[0] == 2) {
                Quit_Game();
            }
            if (block[0] == 1) {
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

    if (IconHandle >= 0 && RISCOS_Desktop_Click_To_Start && !restarted && getenv("VC_NODESKTOP") == nullptr) {
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
