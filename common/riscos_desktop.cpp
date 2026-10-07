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
*/
#ifdef __riscos__

#include "riscos_desktop.h"
#include "gitinfo.h"

#include <SDL.h>
#include <kernel.h>
#include <pthread.h>
#include <swis.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static RISCOSDesktopApp App = {"!vanillatd", "Vanilla", "", ""};
static int IconHandle = -1;
static int InfoWindow = -1;
static bool AudioPaused = false; // by us, while on the desktop

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
** The icon bar menu: Info (leading to the Info window) and Quit.
*/
static struct
{
    char title[12];
    unsigned char colours[4];
    int width, height, gap;
    struct
    {
        unsigned flags;
        int submenu;
        unsigned icon_flags;
        char text[12];
    } items[2];
} Menu;

static void Build_Menu(void)
{
    memset(&Menu, 0, sizeof(Menu));
    strncpy(Menu.title, App.name, sizeof(Menu.title));
    Menu.colours[0] = 7;
    Menu.colours[1] = 2;
    Menu.colours[2] = 7;
    Menu.colours[3] = 0;
    Menu.width = (int(strlen(App.name)) + 2) * 16;
    if (Menu.width < 128) {
        Menu.width = 128;
    }
    Menu.height = 44;
    Menu.gap = 0;

    Menu.items[0].flags = 0;
    Menu.items[0].submenu = InfoWindow;
    Menu.items[0].icon_flags = 0x07000021; // text, filled, black on white
    strncpy(Menu.items[0].text, "Info", sizeof(Menu.items[0].text));

    Menu.items[1].flags = 0x80; // the last item
    Menu.items[1].submenu = -1;
    Menu.items[1].icon_flags = 0x07000021;
    strncpy(Menu.items[1].text, "Quit", sizeof(Menu.items[1].text));
}

static void Open_Menu(int mouse_x)
{
    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(&Menu);
    regs.r[2] = mouse_x - 64;
    regs.r[3] = 96 + 2 * 44; // an icon bar menu sits just above the icon bar
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
        SDL_PauseAudio(0);
    }
    Prog_End(nullptr, false); // exiting with the sound still open fails in SDL_Quit's audio
    exit(0); // SDL_Quit (atexit) closes the task, which takes the icon away
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
        _kernel_oserror* error = _kernel_swi(Wimp_Poll, &regs, &regs);
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

        case 9: // Menu_Selection
            if (block[0] == 1) {
                Quit_Game();
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
    if (started || RISCOS_GetTaskHandle() == 0) {
        return;
    }
    started = true;
    Create_Info_Window();
    Build_Menu();
    Put_Icon_On_Bar();
    if (IconHandle >= 0 && RISCOS_Desktop_Click_To_Start && getenv("VC_NODESKTOP") == nullptr) {
        fprintf(stderr, "desktop: waiting for a click on the icon bar\n");
        AudioPaused = SDL_GetAudioStatus() == SDL_AUDIO_PLAYING; // the sound is set up first
        if (AudioPaused) {
            SDL_PauseAudio(1);
        }
        Wait_For_Icon_Click();
        if (AudioPaused) {
            SDL_PauseAudio(0);
            AudioPaused = false;
        }
    }
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
    AudioPaused = SDL_GetAudioStatus() == SDL_AUDIO_PLAYING;
    if (AudioPaused) {
        SDL_PauseAudio(1);
    }
    Video_Leave_For_Desktop();
    Wait_For_Icon_Click();
    Video_Return_From_Desktop();
    if (AudioPaused) {
        SDL_PauseAudio(0);
        AudioPaused = false;
    }
    Focus_Restore();
    fprintf(stderr, "desktop: back to the game\n");
}

#endif
