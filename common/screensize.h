// Choosing the screen size from the [Video] Resolution setting (TD and RA startup), kept apart from
// the games so that it can be tested (tests/screensize.cpp).
#ifndef SCREENSIZE_H
#define SCREENSIZE_H

#include <cstdio>
#include <string>

// The sizes the game can draw at: it must hold the 640x400 menus, and 2048 is the most its layout code allows.
static const int SCREEN_MIN_W = 640, SCREEN_MIN_H = 400, SCREEN_MAX = 2048;

/*
** Reads "WxH" from "setting". True if it is a usable size (w, h set). Otherwise "why" says what is
** wrong with it, and w, h are left alone.
*/
inline bool Parse_Screen_Size(const std::string& setting, int& w, int& h, std::string& why)
{
    int pw = 0, ph = 0;
    char extra = 0;
    if (sscanf(setting.c_str(), "%dx%d%c", &pw, &ph, &extra) < 2) {
        why = "it is not a size like 1280x800";
        return false;
    }
    if (pw < SCREEN_MIN_W || ph < SCREEN_MIN_H) {
        why = "it is smaller than 640x400";
        return false;
    }
    if (pw > SCREEN_MAX || ph > SCREEN_MAX) {
        why = "it is bigger than 2048x2048";
        return false;
    }
    w = pw;
    h = ph;
    return true;
}

/*
** The size to start at: the setting if it is usable (true), else the fallback (the automatic size, or
** the default) with "note" saying what was wrong. "w", "h" hold the default; they are set to the
** fallback if that is usable too.
*/
inline bool Choose_Screen_Size(const std::string& setting,
                               const std::string& fallback,
                               int& w,
                               int& h,
                               std::string& note,
                               int max_w = 0,
                               int max_h = 0)
{
    std::string why;
    int pw = w, ph = h;
    if (Parse_Screen_Size(setting, pw, ph, why)) {
        if ((max_w > 0 && pw > max_w) || (max_h > 0 && ph > max_h)) {
            // A display can't show a surface bigger than its own screen: asking SDL for one fails, and on RISC OS
            // a failed mode leaves SDL's event handling in a state that crashed the retry. Never asked for.
            char text[64];
            snprintf(text, sizeof(text), "it is bigger than the screen (%dx%d)", max_w, max_h);
            why = text;
        } else {
            w = pw;
            h = ph;
            return true;
        }
    }
    note = "The screen size '" + setting + "' can't be used (" + why + ")";
    std::string ignored;
    int fw = w, fh = h;
    if (Parse_Screen_Size(fallback, fw, fh, ignored)) {
        w = fw;
        h = fh;
    }
    return false;
}

#endif
