/*
** The game on the RISC OS desktop: an icon on the icon bar that starts the game, and a key (Shift+F12)
** that leaves it for the desktop, paused, until the icon is clicked again. The icon's menu has
** the standard Info and Quit. Elsewhere these do nothing.
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
};

#ifdef __riscos__
void RISCOS_Desktop_Set_App(const RISCOSDesktopApp& app);

// False to go straight into the game at the start (e.g. -AUTOSTART), without waiting for a
// click on the icon; the icon is still put on the icon bar.
extern bool RISCOS_Desktop_Click_To_Start;

// Called once the game's Wimp task is running (SDL's video), before the game's screen mode is
// set: puts the icon on the icon bar and, unless told not to, waits for a click on it.
void RISCOS_Desktop_Start(void);

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
