// Opening the game's screen at the size chosen (screensize.h), falling back if the machine can't.
#ifndef SCREENSIZE_VIDEO_H
#define SCREENSIZE_VIDEO_H

#include "riscos_desktop.h"
#include "screensize.h"
#include "settings.h"
#include "video.h"

#ifdef __riscos__
#include <SDL.h>
#endif

/*
** Opens the screen at w x h. If that can't be opened (not expected: a size bigger than the desktop is not asked for; on RISC OS
** SDL_SetVideoMode fails for one bigger than the desktop, and the game used to end with a fatal
** signal as it quit), tries the automatic size, then 640x400, and says what happened on stderr. On
** RISC OS a [Video] Resolution that failed, or was not usable at all ("unusable"), is put back to
** "auto" in the settings file, so the next start doesn't do the same. w and h are what was opened.
*/
inline bool Set_Video_Mode_With_Fallback(int& w, int& h, bool unusable)
{
    const int asked_w = w, asked_h = h;
    bool opened = Set_Video_Mode(w, h, 8);
    if (!opened) {
        std::string candidates[2] = {"640x400", "640x400"};
#ifdef __riscos__
        candidates[0] = RISCOS_Desktop_Auto_Resolution();
#endif
        for (int i = 0; i < 2 && !opened; ++i) {
            int cw = 0, ch = 0;
            std::string why;
            if (Parse_Screen_Size(candidates[i], cw, ch, why) && (cw != w || ch != h)) {
                fprintf(stderr, "video: can't open a %dx%d screen: trying %dx%d\n", w, h, cw, ch);
                w = cw;
                h = ch;
#ifdef __riscos__
                /*
                ** SDL's video is shut down and started again first: after a failed mode the same session
                ** crashed in its event handling (SDL_PumpEvents) on a Pi 4. This is only the safety net: a size
                ** bigger than the desktop is not asked for at all (Choose_Screen_Size).
                */
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                SDL_InitSubSystem(SDL_INIT_VIDEO);
#endif
                opened = Set_Video_Mode(w, h, 8);
            }
        }
        if (!opened) {
            fprintf(stderr, "video: can't open a screen at %dx%d or any smaller size\n", asked_w, asked_h);
            return false;
        }
        unusable = true;
    }
#ifdef __riscos__
    if (unusable) {
        RISCOS_Desktop_Set_Screen(w, h, false);
        if (Settings.Video.Resolution != "auto") {
            RISCOS_Desktop_Forget_Size(); // not left in the settings to be tried again
        }
    }
#endif
    return true;
}

#endif
