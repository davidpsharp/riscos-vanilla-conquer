/*
** The game on the RISC OS desktop: an icon on the icon bar that starts the game, and a key (Shift+F12)
** that leaves it for the desktop, paused, until the icon is clicked again. The icon's menu has
** the standard Info and Quit, and Screen size between them. Elsewhere these do nothing.
*/
#ifndef RISCOS_DESKTOP_H
#define RISCOS_DESKTOP_H

// What the icon bar icon and the Info window show; set by the game before the screen opens.
struct RISCOSDesktopApp
{
    const char* sprite;  // the application's sprite, e.g. "!vanillatd"
    const char* name;    // menu title and Info's Name, e.g. "Vanilla TD"
    const char* purpose; // Info's Purpose
    const char* author;  // Info's Author
    const char* ini;     // the settings file the screen size is saved in, e.g. "CONQUER.INI"
    const char* prefix;  // of the application's variables, e.g. "VanillaTD" for VanillaTD$Dir
};

#ifdef __riscos__
#include <string>

void RISCOS_Desktop_Set_App(const RISCOSDesktopApp& app);

// False to go straight into the game at the start (e.g. -AUTOSTART), without waiting for a
// click on the icon; the icon is still put on the icon bar.
extern bool RISCOS_Desktop_Click_To_Start;

// Called by the game after reading its settings and before choosing its screen size: makes the
// game a Wimp task (SDL's video), puts the icon on the icon bar and, unless told not to, waits
// for a click on it. A screen size chosen from the icon's menu meanwhile is saved in the settings
// (Settings.Video.Resolution and LowRes), so the game then starts at it.
void RISCOS_Desktop_Start(void);

// The screen size the game chose, and whether it was forced to 320x200 (a demo, the DOS files, or
// the LowRes variable), for the menu while the game is on the desktop: choosing another size then
// restarts the game, after asking.
void RISCOS_Desktop_Set_Screen(int width, int height, bool forced_low_res);

// The size Settings.Video.Resolution "auto" stands for (riscos_autosize.h), as "WxH".
const std::string& RISCOS_Desktop_Auto_Resolution(void);

// A size that could not be opened is put back to automatic in the settings file (so the game doesn't fail
// the same way at every start).
void RISCOS_Desktop_Forget_Size(void);

// True for the key that leaves for the desktop: Shift+F12 (as on the desktop, where it brings
// the icon bar to the front). F12 alone stays the game's (a map bookmark).
bool RISCOS_Desktop_Is_Leave_Key(int sdl_key, int sdl_mod);

// Leaves the game's screen for the desktop and waits there, paused, until the icon is clicked
// (or the game is quit from its menu).
void RISCOS_Desktop_Suspend(void);
#else
inline void RISCOS_Desktop_Set_App(const RISCOSDesktopApp&)
{
}
#endif

#endif
