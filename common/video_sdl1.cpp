//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/***************************************************************************
 **   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
 ***************************************************************************
 *                                                                         *
 *                 Project Name : Westwood Win32 Library                   *
 *                                                                         *
 *                    File Name : DDRAW.CPP                                *
 *                                                                         *
 *                   Programmer : Philip W. Gorrow                         *
 *                                                                         *
 *                   Start Date : October 10, 1995                         *
 *                                                                         *
 *                  Last Update : October 10, 1995   []                    *
 *                                                                         *
 *-------------------------------------------------------------------------*
 * Functions:                                                              *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/*=========================================================================*/
/* The following PRIVATE functions are in this file:                       */
/*=========================================================================*/

/*= = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =*/

#include "phasetime.h"
#include "framelimit.h"
#ifdef SDL1AUDIO_BUILD
#include "mixer_sdl1.h"
#endif
#include "gbuffer.h"
#include "palette.h"
#include "video.h"
#include "riscos_desktop.h"
#include "pngwrite.h"
#include "paths.h"
#include "wwkeyboard.h"
#include "wwmouse.h"
#include "settings.h"
#include "debugstring.h"
#ifdef __riscos__
#include "riscos_memory.h"
#endif

#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#endif
#ifdef __APPLE__
#include <execinfo.h> // VC_DIRTYCHECK=2
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern WWKeyboardClass* Keyboard;
static SDL_Surface* window;
static unsigned Window_Generation; // a new window holds nothing presented

/*
** Low resolution on a machine with no screen mode near 320x200 (a Pi, whose 8 bpp modes start
** around 640x400): the game's surface (window) is then a plain one in memory, and what's shown of
** it is copied, each pixel doubled, to a mode twice its size (Screen), which fills the screen as
** 640x400 does. Scale is 2 then; otherwise 1, and window is the screen.
*/
static SDL_Surface* Screen = nullptr;
static int Scale = 1;

// The surface that is the screen.
static SDL_Surface* Shown(void)
{
    return Screen != nullptr ? Screen : window;
}

// Copies part of the game's surface to the screen, each pixel doubled.
static void Scale_Rect(SDL_Rect r)
{
    if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (r.x + r.w > window->w) {
        r.w = window->w - r.x;
    }
    if (r.y + r.h > window->h) {
        r.h = window->h - r.y;
    }
    SDL_LockSurface(window);
    SDL_LockSurface(Screen);
    for (int y = r.y; y < r.y + r.h; ++y) {
        const Uint8* from = static_cast<const Uint8*>(window->pixels) + y * window->pitch + r.x;
        Uint8* to = static_cast<Uint8*>(Screen->pixels) + (2 * y) * Screen->pitch + 2 * r.x;
#if defined(__riscos__) && defined(__arm__)
        // Both rows from registers, a word at a time: the screen isn't read back.
        VC_Double_Row(to, to + Screen->pitch, from, r.w);
#else
        for (int x = 0; x < r.w; ++x) {
            to[2 * x] = to[2 * x + 1] = from[x];
        }
        memcpy(to + Screen->pitch, to, size_t(2 * r.w));
#endif
    }
    SDL_UnlockSurface(Screen);
    SDL_UnlockSurface(window);
}

static bool Counter_Overlay(SDL_Rect& changed); // the frame counter (below)

// SDL_UpdateRects and SDL_Flip for the game's surface, through the doubling if there is any.
// The frame counter, if there is one, is drawn on the screen once the frame is there, and its
// place is updated too.
static void Show_Rects(int count, SDL_Rect* rects)
{
    SDL_Rect counter;
    if (Screen == nullptr) {
        if (Counter_Overlay(counter)) {
            std::vector<SDL_Rect> all(rects, rects + count);
            all.push_back(counter);
            SDL_UpdateRects(window, int(all.size()), all.data());
        } else if (count > 0) {
            SDL_UpdateRects(window, count, rects);
        }
        return;
    }
    std::vector<SDL_Rect> big(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        Scale_Rect(rects[i]);
        big[i].x = Sint16(rects[i].x * 2);
        big[i].y = Sint16(rects[i].y * 2);
        big[i].w = Uint16(rects[i].w * 2);
        big[i].h = Uint16(rects[i].h * 2);
    }
    if (Counter_Overlay(counter)) {
        big.push_back(counter);
    }
    if (!big.empty()) {
        SDL_UpdateRects(Screen, int(big.size()), big.data());
    }
}

static void Show_All(void)
{
    SDL_Rect counter;
    if (Screen == nullptr) {
        Counter_Overlay(counter); // (the flip shows all of it)
        SDL_Flip(window);
        return;
    }
    SDL_Rect all = {0, 0, Uint16(window->w), Uint16(window->h)};
    Scale_Rect(all);
    Counter_Overlay(counter);
    SDL_Flip(Screen);
}

// SDL_SetPalette for the game's surface, and the screen behind it if that's another.
static void Set_Palette_Both(int flags, SDL_Color* colours)
{
    SDL_SetPalette(window, flags, colours, 0, 256);
    if (Screen != nullptr) {
        SDL_SetPalette(Screen, flags, colours, 0, 256);
    }
}

/*
** The area of the screen the pointer is reported in (see Set_Video_Mouse_Area).
*/
static struct
{
    int X, Y, W, H;
} MouseArea = {0, 0, 0, 0};

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>

/*
** SDL 1.2's RISC OS port works out full screen pointer positions as if the screen
** were the size of the surface. When there's no mode that size (320x200, say) it
** uses a bigger one and centres the surface in it, and then the positions are
** off by the centring, with y clamped to 0: the game sees the pointer stuck on the
** top edge and scrolls the map north for ever. So for a centred surface, work the
** position out from the OS pointer and the mode instead, and keep the pointer
** inside the surface.
*/
static bool Centred_Surface(int& offx, int& offy)
{
    SDL_Surface* const shown = Shown();
    if (shown == nullptr || shown->offset == 0 || shown->pitch <= 0) {
        return false;
    }
    offx = shown->offset % shown->pitch;
    offy = shown->offset / shown->pitch;
    return true;
}

static int Mode_Variable(int var)
{
    _kernel_swi_regs regs;
    regs.r[0] = -1;
    regs.r[1] = var;
    _kernel_swi(OS_ReadModeVariable, &regs, &regs);
    return regs.r[2];
}

bool RISCOS_Pointer_Position(int& x, int& y)
{
    // Always from the OS pointer, as SDL's positions are off on a centred surface, and to
    // be the same whether it is centred or not.
    int offx = 0, offy = 0;
    if (window == nullptr) {
        return false;
    }
    Centred_Surface(offx, offy);
    _kernel_swi_regs regs;
    _kernel_swi(OS_Mouse, &regs, &regs);
    x = ((regs.r[0] >> Mode_Variable(4)) - offx) / Scale;                     // XEigFactor
    y = (Mode_Variable(12) - (regs.r[1] >> Mode_Variable(5)) - offy) / Scale; // YWindLimit, YEigFactor
    x = x < 0 ? 0 : (x >= window->w ? window->w - 1 : x);
    y = y < 0 ? 0 : (y >= window->h ? window->h - 1 : y);
    return true;
}

// Keep the pointer inside the surface, or the menu area on it (OS_Word 21,1 sets the
// bounding box; OS_Word 21,3 moves the pointer).
static void Confine_Pointer()
{
    int offx = 0, offy = 0;
    bool const centred = Centred_Surface(offx, offy);
    bool const area = MouseArea.W > 0 && MouseArea.H > 0;
    if (window == nullptr) {
        return;
    }
    // Without a centred surface or an area this still sets the box, to the whole surface,
    // to undo an area's box (the game layout after a menu).
    // Within the surface, the menu area if there is one, or else the whole surface.
    int x0 = offx + (area ? MouseArea.X : 0) * Scale, y0 = offy + (area ? MouseArea.Y : 0) * Scale;
    int w = (area ? MouseArea.W : window->w) * Scale, h = (area ? MouseArea.H : window->h) * Scale;
    int xeig = Mode_Variable(4), yeig = Mode_Variable(5), ymax = Mode_Variable(12);
    int left = x0 << xeig, right = (x0 + w - 1) << xeig;
    int bottom = (ymax - (y0 + h - 1)) << yeig, top = (ymax - y0) << yeig;
    unsigned char block[9] = {1,
                              Uint8(left),
                              Uint8(left >> 8),
                              Uint8(bottom),
                              Uint8(bottom >> 8),
                              Uint8(right),
                              Uint8(right >> 8),
                              Uint8(top),
                              Uint8(top >> 8)};
    _kernel_osword(21, reinterpret_cast<int*>(block));

    // Keep the pointer where it is, if that's inside the box (so a layout change, such as the
    // end of a movie, doesn't take it away from where the player had it), or else bring it
    // to the nearest point inside. OS_Word 21,4 reads where it is.
    unsigned char pos[5] = {4, 0, 0, 0, 0};
    _kernel_osword(21, reinterpret_cast<int*>(pos));
    int cx = Sint16(pos[1] | (pos[2] << 8)), cy = Sint16(pos[3] | (pos[4] << 8));
    cx = cx < left ? left : (cx > right ? right : cx);
    cy = cy < bottom ? bottom : (cy > top ? top : cy);
    unsigned char move[5] = {3, Uint8(cx), Uint8(cx >> 8), Uint8(cy), Uint8(cy >> 8)};
    _kernel_osword(21, reinterpret_cast<int*>(move));
}
#endif
static SDL_Color logpal[256], physpal[256];
void Video_Settle_Front();

static struct
{
    int GameW;
    int GameH;
    bool Clip;
    void* Raw;
    int W;
    int H;
    int HotX;
    int HotY;
    float X;
    float Y;
    SDL_Surface* Surface;
} hwcursor;

static void Update_HWCursor();

static SDL_Rect Make_SDL_Rect(int x, int y, int w, int h)
{
    SDL_Rect r = {Sint16(x), Sint16(y), Uint16(w), Uint16(h)};
    return r;
}

static SDL_Rect Union_Rects(const SDL_Rect& a, const SDL_Rect& b)
{
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return Make_SDL_Rect(x0, y0, x1 - x0, y1 - y0);
}

/*
** The frame counter (Shift+F8; wwkeyboard_sdl1.cpp): a small bright green figure in the bottom
** right corner of the screen, with a thin black outline: the game's own frames a second, then
** how busy it is, as in "15 40%" (the share of the time it isn't waiting for its next frame;
** framelimit.h and ratemeter.h). Out of a game frame (a menu, a dialog) it is a "-". It is off
** until Shift+F8 turns it on.
**
** It exists only on the screen, never in a game surface, so the game can't see it: it is
** drawn on the screen (Shown) after the frame, and the cursor, have reached it, and what it
** covered is saved first, as the software pointer's is. The screen is written live, so it is
** left alone as long as it can be: it stays where it is through partial presents that don't
** touch its place and while its figures don't change (they are looked at a few times a
** second), and only a present that copies over its place, or its own change, rewrites it.
** A full present gets what the counter covered from the frame itself, not from a copy put
** back first, so the figures are never off the screen between the copy and being drawn again.
** Anything that reads the screen back (Settle) takes it off first. Turning it off puts the screen
** back once. Screenshots are of the game's own surface, so don't have it.
*/
static struct
{
    bool on = false;
    Uint32 t0 = 0;          // when it was turned on
    Uint32 changed_at = 0;  // when the figures to show last changed
    char text[16] = "";     // the figures to show; empty for none yet
    // What is on the screen.
    bool shown = false;
    SDL_Surface* surface = nullptr;
    unsigned generation = 0;
    SDL_Rect rect = {0, 0, 0, 0};
    std::vector<Uint8> under; // the screen's pixels in rect, before the counter
    std::vector<Uint8> mask;  // 0 clear, 1 outline, 2 digit: rect.w x rect.h
    Uint8 fg = 0, bg = 0;     // the palette entries used
    char shown_text[16] = "";
    bool update = false; // the screen under the counter changed since the last update of its place
    // Palette: the nearest to green, and the darkest.
    unsigned palette_generation = 1, colours_generation = 0;
    Uint8 green = 0, dark = 0;
} Ctr;

// 5x7 glyphs, a row to a byte (bit 4 is the left column): 0 to 9, %, -, space.
static const Uint8 Counter_Font[13][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

static const Uint8* Counter_Glyph(char c)
{
    return Counter_Font[c >= '0' && c <= '9' ? c - '0' : c == '%' ? 10 : c == '-' ? 11 : 12];
}

void Video_Toggle_FPS_Counter(void)
{
    Ctr.on = !Ctr.on;
    Ctr.t0 = SDL_GetTicks();
    Ctr.changed_at = 0;
    Ctr.text[0] = '\0';
    Rate_Meter_Enable(Ctr.on);
    fprintf(stderr, "Frame counter %s\n", Ctr.on ? "on" : "off");
}

/*
** Works out the figures to show, at every present. The first as soon as there is a reading (a
** rough one, from the first frames), then changes at most four times a second.
*/
static void Counter_Tick(void)
{
    /*
    ** VC_COUNTER=1 (test only): the counter is on from the first present, without the key.
    ** VC_COUNTER=2: and toggled off and on again every 50 presents, to test taking it away.
    */
    static const char* test = getenv("VC_COUNTER");
    static unsigned presents = 0;
    if (test != nullptr && (presents == 0 || (test[0] == '2' && presents % 50 == 0))) {
        Video_Toggle_FPS_Counter();
    }
    ++presents;
    if (!Ctr.on) {
        return;
    }
    Uint32 now = SDL_GetTicks();
    if (Ctr.text[0] != '\0' && now - Ctr.changed_at < 250) {
        return;
    }
    char text[16];
    int fps, busy;
    if (Rate_Meter_Read(fps, busy)) {
        snprintf(text, sizeof(text), "%d %d%%", fps > 999 ? 999 : fps, busy);
    } else if (now - Ctr.t0 >= 400) {
        snprintf(text, sizeof(text), "-"); // no game frames: a menu or a dialog
    } else {
        return; // still waiting for the first
    }
    if (strcmp(text, Ctr.text) != 0) {
        memcpy(Ctr.text, text, sizeof(text));
        Ctr.changed_at = now;
    }
}

// Does the screen need a present to bring the counter up to date?
static bool Counter_Stale(void)
{
    if (!Ctr.on) {
        return Ctr.shown;
    }
    return Ctr.text[0] != '\0'
           && (!Ctr.shown || strcmp(Ctr.text, Ctr.shown_text) != 0 || Ctr.colours_generation != Ctr.palette_generation);
}

// Is the counter on the screen as it was drawn, and where the screen still is?
static bool Counter_On_Screen(void)
{
    SDL_Surface* on = Shown();
    return Ctr.shown && on != nullptr && SDL_WasInit(SDL_INIT_VIDEO) != 0 && on == Ctr.surface
           && Ctr.generation == Window_Generation && Ctr.rect.x + Ctr.rect.w <= on->w && Ctr.rect.y + Ctr.rect.h <= on->h;
}

// Does the counter's place (on the screen) meet "r" (in the game's pixels)?
static bool Counter_Meets(const SDL_Rect& r)
{
    if (r.w <= 0 || r.h <= 0) {
        return false;
    }
    int x0 = r.x * Scale, y0 = r.y * Scale, x1 = (r.x + r.w) * Scale, y1 = (r.y + r.h) * Scale;
    return x0 < Ctr.rect.x + Ctr.rect.w && x1 > Ctr.rect.x && y0 < Ctr.rect.y + Ctr.rect.h && y1 > Ctr.rect.y;
}

// Puts back what the counter covered on the screen.
static void Counter_Remove(void)
{
    if (!Ctr.shown) {
        return;
    }
    bool there = Counter_On_Screen();
    Ctr.shown = false;
    if (!there) {
        return; // the screen is a new one, which holds nothing of ours
    }
    SDL_Surface* on = Shown();
    SDL_LockSurface(on);
    for (int row = 0; row < Ctr.rect.h; ++row) {
        memcpy(static_cast<Uint8*>(on->pixels) + (Ctr.rect.y + row) * on->pitch + Ctr.rect.x,
               &Ctr.under[size_t(row) * Ctr.rect.w],
               Ctr.rect.w);
    }
    SDL_UnlockSurface(on);
    Ctr.update = true;
}

/*
** After a full present, in place of Counter_Remove before it: the copy has left the screen
** holding the frame in some places and the counter in others, so the counter's place is given the
** frame's pixels (the game's surface "frame", which the doubling copy, Scale_Rect, takes from the
** window), ready to be drawn on again. Nothing of the frame's is lost in between, as it would be
** by putting the saved pixels back and copying over them.
*/
static void Counter_Refresh(SDL_Surface* frame)
{
    if (!Ctr.shown) {
        return;
    }
    bool there = Counter_On_Screen();
    Ctr.shown = false;
    Ctr.update = true;
    if (!there) {
        return;
    }
    SDL_Surface* on = Shown();
    if (Screen != nullptr) {
        SDL_Rect r = Make_SDL_Rect(Ctr.rect.x / Scale, Ctr.rect.y / Scale, (Ctr.rect.x + Ctr.rect.w + Scale - 1) / Scale - Ctr.rect.x / Scale,
                                   (Ctr.rect.y + Ctr.rect.h + Scale - 1) / Scale - Ctr.rect.y / Scale);
        Scale_Rect(r); // (clipped to the window)
        return;
    }
    if (frame->w < Ctr.rect.x + Ctr.rect.w || frame->h < Ctr.rect.y + Ctr.rect.h) {
        return;
    }
    SDL_LockSurface(frame);
    SDL_LockSurface(on);
    for (int row = 0; row < Ctr.rect.h; ++row) {
        memcpy(static_cast<Uint8*>(on->pixels) + (Ctr.rect.y + row) * on->pitch + Ctr.rect.x,
               static_cast<const Uint8*>(frame->pixels) + (Ctr.rect.y + row) * frame->pitch + Ctr.rect.x,
               Ctr.rect.w);
    }
    SDL_UnlockSurface(on);
    SDL_UnlockSurface(frame);
}

// How big to draw it: 1 up to 600 pixels of screen height, 2 up to 1500, then 3.
static int Counter_Scale(int screen_h)
{
    return screen_h <= 600 ? 1 : (screen_h < 1500 ? 2 : 3);
}

/*
** Draws the counter on the screen, if it is on and not there as it should be, and says (true)
** which part of it to update: the counter's place, or the place it just left. Called with the
** frame on the screen.
*/
static bool Counter_Overlay(SDL_Rect& changed)
{
    if (!Ctr.on || Ctr.text[0] == '\0') {
        if (Ctr.update) {
            Ctr.update = false;
            changed = Ctr.rect; // what it covered, put back
            return true;
        }
        return false;
    }
    SDL_Surface* on = Shown();
    if (on == nullptr) {
        return false;
    }
    if (Ctr.shown) {
        if (strcmp(Ctr.text, Ctr.shown_text) == 0 && Ctr.colours_generation == Ctr.palette_generation
            && Counter_On_Screen()) {
            return false; // still there, untouched by this present
        }
        Counter_Remove(); // (to change it, or else a double draw would save the digits as the screen)
    }

    // The figures, as a mask: 5x7 glyphs a pixel apart, scaled, and a one pixel outline.
    char text[sizeof(Ctr.text)];
    memcpy(text, Ctr.text, sizeof(text));
    int digits = int(strlen(text));
    int scale = Counter_Scale(on->h);
    int w = (digits * 6 - 1) * scale + 2, h = 7 * scale + 2;
    int margin = 2 * scale;
    SDL_Rect rect = Make_SDL_Rect(on->w - w - margin, on->h - h - margin, w, h);
    if (rect.x < 0 || rect.y < 0) {
        return false; // a screen too small for it
    }
    Ctr.mask.assign(size_t(w) * h, 0);
    for (int d = 0; d < digits; ++d) {
        const Uint8* glyph = Counter_Glyph(text[d]);
        for (int row = 0; row < 7 * scale; ++row) {
            for (int col = 0; col < 5 * scale; ++col) {
                if (glyph[row / scale] & (0x10 >> (col / scale))) {
                    Ctr.mask[size_t(1 + row) * w + 1 + d * 6 * scale + col] = 2;
                }
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (Ctr.mask[size_t(y) * w + x] != 0) {
                continue;
            }
            for (int dy = -1; dy <= 1 && Ctr.mask[size_t(y) * w + x] == 0; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && Ctr.mask[size_t(ny) * w + nx] == 2) {
                        Ctr.mask[size_t(y) * w + x] = 1;
                        break;
                    }
                }
            }
        }
    }

    // The palette entries: nearest to (0,255,0) and the darkest, again after a palette change.
    if (Ctr.colours_generation != Ctr.palette_generation) {
        // The brightest clearly green entry: most green, least red and blue (4g - (r+b)), and
        // green must dominate (g > 1.5 max(r,b)). A palette with none (a movie's, say) gets the
        // entry with the most of that score anyway: a bright yellow or white, still legible
        // inside the dark outline.
        int best_green = -(1 << 30), best_any = -(1 << 30), best_dark = 1 << 30;
        Uint8 any = 0;
        for (int i = 0; i < 256; ++i) {
            int r = physpal[i].r, g = physpal[i].g, b = physpal[i].b;
            int score = 4 * g - (r + b);
            if (g >= 100 && 2 * g > 3 * (r > b ? r : b) && score > best_green) {
                best_green = score;
                Ctr.green = Uint8(i);
            }
            if (score > best_any) {
                best_any = score;
                any = Uint8(i);
            }
            int k = r * 3 + g * 6 + b;
            if (k < best_dark) {
                best_dark = k;
                Ctr.dark = Uint8(i);
            }
        }
        if (best_green == -(1 << 30)) {
            Ctr.green = any;
        }
        Ctr.colours_generation = Ctr.palette_generation;
    }
    Ctr.fg = Ctr.green;
    Ctr.bg = Ctr.dark;

    // Save what it covers, and draw it.
    Ctr.under.resize(size_t(w) * h);
    SDL_LockSurface(on);
    for (int y = 0; y < h; ++y) {
        Uint8* row = static_cast<Uint8*>(on->pixels) + (rect.y + y) * on->pitch + rect.x;
        memcpy(&Ctr.under[size_t(y) * w], row, w);
        for (int x = 0; x < w; ++x) {
            Uint8 m = Ctr.mask[size_t(y) * w + x];
            if (m != 0) {
                row[x] = m == 2 ? Ctr.fg : Ctr.bg;
            }
        }
    }
    SDL_UnlockSurface(on);

    // The place it left, if it moved or got smaller, was put back by Counter_Remove.
    changed = Union_Rects(Ctr.update ? Ctr.rect : rect, rect);
    Ctr.rect = rect;
    Ctr.surface = on;
    Ctr.generation = Window_Generation;
    Ctr.shown = true;
    memcpy(Ctr.shown_text, text, sizeof(text));
    Ctr.update = false;
    return true;
}

class SurfaceMonitorClassDummy : public SurfaceMonitorClass
{

public:
    SurfaceMonitorClassDummy()
    {
    }

    virtual void Restore_Surfaces()
    {
    }

    virtual void Set_Surface_Focus(bool in_focus)
    {
    }

    virtual void Release()
    {
    }
};

SurfaceMonitorClassDummy AllSurfacesDummy;           // List of all direct draw surfaces
SurfaceMonitorClass& AllSurfaces = AllSurfacesDummy; // List of all direct draw surfaces

/***********************************************************************************************
 * Set_Video_Mode -- Initializes Direct Draw and sets the required Video Mode                  *
 *                                                                                             *
 * INPUT:           int width           - the width of the video mode in pixels                *
 *                  int height          - the height of the video mode in pixels               *
 *                  int bits_per_pixel  - the number of bits per pixel the video mode supports *
 *                                                                                             *
 * OUTPUT:     none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/26/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
#ifdef __riscos__
/*
** True if no 8 bpp screen mode is near w x h, the nearest that holds it being at least twice as
** big each way (a Pi has nothing like 320x200; a Risc PC has 320x256). VC_NODOUBLE turns it off.
*/
// VC_NODOUBLE: never double. VC_DOUBLE: double even where a mode near the size exists (for
// testing and timing the doubling on a machine that has one).
static bool Wants_Doubling(int w, int h)
{
    if (getenv("VC_NODOUBLE") != nullptr || w > 400 || h > 300) {
        return false;
    }
    if (getenv("VC_DOUBLE") != nullptr) {
        return true;
    }
    SDL_PixelFormat format;
    memset(&format, 0, sizeof(format));
    format.BitsPerPixel = 8;
    format.BytesPerPixel = 1;
    SDL_Rect** modes = SDL_ListModes(&format, SDL_FULLSCREEN | SDL_HWSURFACE);
    if (modes == nullptr || modes == reinterpret_cast<SDL_Rect**>(-1)) {
        return false;
    }
    for (int i = 0; modes[i] != nullptr; ++i) {
        if (modes[i]->w >= w && modes[i]->h >= h && modes[i]->w < 2 * w && modes[i]->h < 2 * h) {
            return false;
        }
    }
    return true;
}
#endif

bool Set_Video_Mode(int w, int h, int bits_per_pixel)
{
    /*
    ** No parachute: SDL's handler for fatal signals calls SDL_Quit inside the signal
    ** handler, which on RISC OS waits for the audio thread and so dies with a UnixLib
    ** "pthread_yield called with context switching disabled" error, hiding the real
    ** fault. UnixLib's own handler prints a backtrace instead.
    */
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
    SDL_ShowCursor(SDL_DISABLE);
    atexit(SDL_Quit);

    int win_w = w;
    int win_h = h;
    int win_flags = SDL_HWSURFACE | SDL_HWPALETTE;

    if (!Settings.Video.Windowed) {
        win_flags |= SDL_FULLSCREEN;
    }

#ifdef __riscos__
    // The icon on the icon bar and the wait for a click, if the game hasn't done it already
    // (it does, before choosing its screen size).
    if (win_flags & SDL_FULLSCREEN) {
        RISCOS_Desktop_Start();
    }
#endif

    Video_Settle_Front();
#ifdef __riscos__
    if ((win_flags & SDL_FULLSCREEN) && Wants_Doubling(w, h)) {
        Screen = SDL_SetVideoMode(w * 2, h * 2, 8, win_flags);
        window = Screen != nullptr ? SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 8, 0, 0, 0, 0) : nullptr;
        Scale = 2;
        fprintf(stderr, "video: %dx%d doubled to fill the screen\n", w, h);
    } else
#endif
        window = SDL_SetVideoMode(w, h, 8, win_flags);
    ++Window_Generation;
    if (window == nullptr) {
        DBG_ERROR("SDL_SetVideoMode failed: %s", SDL_GetError());
        Reset_Video_Mode();
        return false;
    }

#ifdef __riscos__
    /*
    ** The RISC OS SDL port switches to relative mouse mode (re-centring the pointer
    ** every poll) when the cursor is hidden and input is grabbed, which fullscreen
    ** always does. That fights any absolute pointer source, e.g. an emulator
    ** following the host mouse. Keep the cursor "visible" but fully transparent so
    ** SDL reports the absolute OS pointer position instead.
    */
    static Uint8 blank_cursor_bits[8] = {0};
    static SDL_Cursor* blank_cursor = SDL_CreateCursor(blank_cursor_bits, blank_cursor_bits, 8, 8, 0, 0);
    if (blank_cursor != nullptr) {
        SDL_SetCursor(blank_cursor);
        SDL_ShowCursor(SDL_ENABLE);
    }
    Confine_Pointer();
    {
        // Which mode SDL chose matters for speed: the screen's refresh shares the memory bus.
        _kernel_swi_regs regs;
        regs.r[0] = 1; // OS_ScreenMode 1: read the current mode
        _kernel_swi(OS_ScreenMode, &regs, &regs);
        int rate = -1;
        if (unsigned(regs.r[1]) >= 256) {
            rate = reinterpret_cast<const int*>(regs.r[1])[4]; // mode selector: flags, x, y, log2bpp, rate
        }
        fprintf(stderr,
                "video: %dx%d surface in a %dx%d mode (%d bpp, %d Hz, mode %s), offset %d,%d\n",
                window->w,
                window->h,
                Mode_Variable(11) + 1,
                Mode_Variable(12) + 1,
                1 << Mode_Variable(9),
                rate,
                unsigned(regs.r[1]) >= 256 ? "selector" : "number",
                Shown()->pitch ? Shown()->offset % Shown()->pitch : 0,
                Shown()->pitch ? Shown()->offset / Shown()->pitch : 0);
    }
#endif

    Set_Palette_Both(SDL_LOGPAL, logpal);
    SDL_WM_SetCaption("Vanilla Conquer", NULL);

    DBG_INFO("Created SDL1 %s window in %dx%d@%dbpp",
             (window->flags & SDL_FULLSCREEN ? "fullscreen" : "windowed"),
             window->w,
             window->h,
             window->format->BitsPerPixel);

    /*
    ** Set mouse scaling options.
    */
    hwcursor.GameW = w;
    hwcursor.GameH = h;
    hwcursor.X = w / 2;
    hwcursor.Y = h / 2;

    /*
    ** Ensure cursor clip is in the desired state.
    */
    Set_Video_Cursor_Clip(hwcursor.Clip);

    /*
    ** Update visible cursor scaling.
    */
    Update_HWCursor();

    return true;
}

#ifdef __riscos__
extern "C" void RISCOS_RestoreWimpMode(void);
extern "C" void WIMP_RestoreWimpCursor(void);

/*
** For the desktop while the game is paused (riscos_desktop.cpp): its screen mode back (SDL keeps
** it) with the Wimp's pointer; then the game's mode again, which SDL sets up afresh, with the
** palette, pointer and pointer box, and a full present of the frame the game still holds.
*/
void Video_Leave_For_Desktop(void)
{
    if (window == nullptr || !(Shown()->flags & SDL_FULLSCREEN)) {
        return;
    }
    RISCOS_RestoreWimpMode();
    WIMP_RestoreWimpCursor();
}

void Video_Return_From_Desktop(void)
{
    if (window == nullptr || !(Shown()->flags & SDL_FULLSCREEN)) {
        return;
    }
    Video_Settle_Front();
    SDL_Surface* const shown = Shown();
    SDL_Surface* new_shown = SDL_SetVideoMode(shown->w, shown->h, 8, SDL_HWSURFACE | SDL_HWPALETTE | SDL_FULLSCREEN);
    if (new_shown == nullptr) {
        DBG_ERROR("SDL_SetVideoMode back from the desktop failed: %s", SDL_GetError());
        exit(1);
    }
    if (Screen != nullptr) {
        Screen = new_shown; // the game's own surface stays as it is
    } else {
        window = new_shown;
    }
    ++Window_Generation;
    Set_Palette_Both(SDL_LOGPAL, logpal);
    Set_Palette_Both(SDL_PHYSPAL, physpal);
    SDL_SetCursor(SDL_GetCursor()); // define the (transparent) pointer shape again
    Confine_Pointer();
}
#endif

void Toggle_Video_Fullscreen()
{
    Settings.Video.Windowed = !Settings.Video.Windowed;

    if (window) {
        int win_flags = SDL_HWSURFACE | SDL_HWPALETTE;
        SDL_Surface* new_window;

        if (!Settings.Video.Windowed) {
            win_flags |= SDL_FULLSCREEN;
        }

        Video_Settle_Front();
        new_window = SDL_SetVideoMode(window->w, window->h, 8, win_flags);
        if (new_window) {
            window = new_window;
            ++Window_Generation;

            /* The palette needs to be restored when switching to a new video mode */
            SDL_SetPalette(window, SDL_LOGPAL, logpal, 0, 256);
            SDL_SetPalette(window, SDL_PHYSPAL, physpal, 0, 256);
        }
    }
}

void Get_Video_Scale(float& x, float& y)
{
    x = 1.0f;
    y = 1.0f;
}

void Set_Video_Cursor_Clip(bool clipped)
{
    hwcursor.Clip = clipped;

    if (window) {
        int relative = -1;

        if (relative < 0) {
            // DBG_ERROR("Raw input not supported, disabling.");
            Settings.Mouse.RawInput = false;
        }
    }
}

void Move_Video_Mouse(float xrel, float yrel)
{
    if (Keyboard->Is_Gamepad_Active() || hwcursor.Clip || !Settings.Video.Windowed) {
        hwcursor.X += xrel * (Settings.Mouse.Sensitivity / 100.0f);
        hwcursor.Y += yrel * (Settings.Mouse.Sensitivity / 100.0f);
    }

    if (hwcursor.X >= hwcursor.GameW) {
        hwcursor.X = hwcursor.GameW - 1;
    } else if (hwcursor.X < 0) {
        hwcursor.X = 0;
    }

    if (hwcursor.Y >= hwcursor.GameH) {
        hwcursor.Y = hwcursor.GameH - 1;
    } else if (hwcursor.Y < 0) {
        hwcursor.Y = 0;
    }
}

static void Apply_Mouse_Area(int& x, int& y)
{
    if (MouseArea.W <= 0 || MouseArea.H <= 0) {
        return;
    }
    x -= MouseArea.X;
    y -= MouseArea.Y;
    x = x < 0 ? 0 : (x >= MouseArea.W ? MouseArea.W - 1 : x);
    y = y < 0 ? 0 : (y >= MouseArea.H ? MouseArea.H - 1 : y);
}

static void Get_Screen_Mouse(int& x, int& y)
{
#ifdef __riscos__
    // A centred surface: SDL's positions are off (see RISCOS_Pointer_Position), raw or not.
    if (!Keyboard->Is_Gamepad_Active() && RISCOS_Pointer_Position(x, y)) {
        return;
    }
#endif
    if (Keyboard->Is_Gamepad_Active() || (Settings.Mouse.RawInput && (hwcursor.Clip || !Settings.Video.Windowed))) {
        x = hwcursor.X;
        y = hwcursor.Y;
    } else {
        SDL_GetMouseState(&x, &y);
    }
}

void Get_Video_Mouse(int& x, int& y)
{
    Get_Screen_Mouse(x, y);
    Apply_Mouse_Area(x, y);
}

// Where the cursor goes on the screen: the game's position, back inside the mouse area.
static void Get_Cursor_Position(int& x, int& y)
{
    Get_Video_Mouse(x, y);
    if (MouseArea.W > 0 && MouseArea.H > 0) {
        x += MouseArea.X;
        y += MouseArea.Y;
    }
}

void Set_Video_Mouse_Area(int x, int y, int w, int h)
{
    MouseArea.X = x;
    MouseArea.Y = y;
    MouseArea.W = w;
    MouseArea.H = h;
#ifdef __riscos__
    Confine_Pointer();
#endif
}

/***********************************************************************************************
 * Reset_Video_Mode -- Resets video mode and deletes Direct Draw Object                        *
 *                                                                                             *
 * INPUT:		none                                                                            *
 *                                                                                             *
 * OUTPUT:     none                                                                            *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/26/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
void Reset_Video_Mode(void)
{
    if (hwcursor.Surface) {
        SDL_FreeSurface(hwcursor.Surface);
        hwcursor.Surface = nullptr;
    }

    if (window) {
        SDL_FreeSurface(window);
        window = nullptr;
    }
    Screen = nullptr; // SDL's own, like a window that is the screen
    Scale = 1;
}

static void Update_HWCursor()
{
    /*
    ** Allocate or reallocate surface if it has the wrong size.
    */
    if (hwcursor.Surface == nullptr || hwcursor.Surface->w != hwcursor.W || hwcursor.Surface->h != hwcursor.H) {
        if (hwcursor.Surface) {
            SDL_FreeSurface(hwcursor.Surface);
        }

        /*
        ** Real HW cursor needs to be scaled up. Emulated can use original cursor data.
        */
        hwcursor.Surface = SDL_CreateRGBSurfaceFrom(hwcursor.Raw, hwcursor.W, hwcursor.H, 8, hwcursor.W, 0, 0, 0, 0);

        SDL_SetColorKey(hwcursor.Surface, SDL_SRCCOLORKEY, 0);
    }
}

// Bumped whenever the cursor shape may have changed, so a frame is presented.
static unsigned Cursor_Generation = 0;

void Set_Video_Cursor(void* cursor, int w, int h, int hotx, int hoty)
{
    ++Cursor_Generation;
    hwcursor.Raw = cursor;
    hwcursor.W = w;
    hwcursor.H = h;
    hwcursor.HotX = hotx;
    hwcursor.HotY = hoty;

    Update_HWCursor();
}

/***********************************************************************************************
 * Get_Free_Video_Memory -- returns amount of free video memory                                *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   bytes of available video RAM                                                      *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/29/95 12:52PM ST : Created                                                            *
 *=============================================================================================*/
unsigned int Get_Free_Video_Memory(void)
{
    return 1000000000;
}

/***********************************************************************************************
 * Get_Video_Hardware_Caps -- returns bitmask of direct draw video hardware support            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   hardware flags                                                                    *
 *                                                                                             *
 * WARNINGS: Must call Set_Video_Mode 1st to create the direct draw object                     *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    1/12/96 9:14AM ST : Created                                                              *
 *=============================================================================================*/
unsigned Get_Video_Hardware_Capabilities(void)
{
    return VIDEO_BLITTER;
}

/***********************************************************************************************
 * Wait_Vert_Blank -- Waits for the start (leading edge) of a vertical blank                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *=============================================================================================*/
void Wait_Vert_Blank(void)
{
}

/***********************************************************************************************
 * Set_Palette -- set a direct draw palette                                                    *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    ptr to 768 rgb palette bytes                                                      *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    10/11/95 3:33PM ST : Created                                                             *
 *=============================================================================================*/
void Set_DD_Palette(void* rpalette)
{
    unsigned char* rcolors = (unsigned char*)rpalette;
    for (int i = 0; i < 256; i++) {
        physpal[i].r = (unsigned char)rcolors[i * 3] << 2;
        physpal[i].g = (unsigned char)rcolors[i * 3 + 1] << 2;
        physpal[i].b = (unsigned char)rcolors[i * 3 + 2] << 2;
    }

    Set_Palette_Both(SDL_PHYSPAL, physpal);
    ++Ctr.palette_generation; // the frame counter's colours

    /*
    ** Cursor needs to be updated when palette changes.
    */
    Update_HWCursor();
}

/***********************************************************************************************
 * Wait_Blit -- waits for the DirectDraw blitter to become idle                                *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07-25-95 03:53pm ST : Created                                                             *
 *=============================================================================================*/

void Wait_Blit(void)
{
}

/***********************************************************************************************
 * SMC::SurfaceMonitorClass -- constructor for surface monitor class                           *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/3/95 3:23PM ST : Created                                                              *
 *=============================================================================================*/

SurfaceMonitorClass::SurfaceMonitorClass()
{
    SurfacesRestored = false;
}
/*
** VideoSurfaceDDraw
*/
class VideoSurfaceSDL1;
static VideoSurfaceSDL1* frontSurface = nullptr;

/*
** Screen shakes (Shake_The_Screen): vertical offsets for the next presents to show.
*/
static int Shake_Queue[16];
static int Shake_Count = 0;
static bool Shake_Undo = false; // the screen is shifted; the next present must be a normal full one

void Video_Queue_Shake(int dy)
{
    if (Shake_Count < 16) {
        Shake_Queue[Shake_Count++] = dy;
    }
}


// Copies the overlapping part of two 8 bit surfaces byte for byte (SDL would remap
// colours between 8 bit palettes).
#if defined(__riscos__) && defined(__arm__)
/*
** Presents copy each row (or each span of changed tiles) with VC_Copy_Span
** (riscos_memory.h): LDM/STM of 32 bytes at a time, then 16, 8, 4 and the last bytes, for
** any width (the Pi's 1680, the spans at the right edge). VC_PRESENTMEMCPY uses memcpy
** instead, for comparison (now the program's own, riscos_memory.cpp).
*/
static bool Present_With_Memcpy()
{
    static const bool use_memcpy = getenv("VC_PRESENTMEMCPY") != nullptr;
    return use_memcpy;
}
#endif

/*
** VC_DIRTYSTAT: for each full present, count how much of the frame differs from the
** last one, to judge whether presenting only what changed would pay. Measured in
** 16x16 tiles, in full-width bands of 16 rows, and as one bounding rectangle.
*/
extern unsigned long long Dirty_Partial, Dirty_Full_Unknown, Dirty_Full_Other, Dirty_Tiles_Copied, Dirty_Missed,
    Dirty_Unknown_Locks;
extern unsigned long long Dirty_Frames, Dirty_Tiles, Dirty_Tiles_Total, Dirty_Bands, Dirty_Bands_Total, Dirty_Box_Pixels,
    Dirty_Pixels_Total;

static void Dirty_Stat(SDL_Surface* frame)
{
    static unsigned char* last = nullptr;
    int w = frame->w, h = frame->h, pitch = frame->pitch;
    const unsigned char* now = static_cast<const unsigned char*>(frame->pixels);
    if (last == nullptr) {
        last = static_cast<unsigned char*>(malloc(w * h));
        for (int y = 0; y < h; ++y) {
            memcpy(last + y * w, now + y * pitch, w);
        }
        return;
    }
    int tw = (w + 15) / 16, th = (h + 15) / 16;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int ty = 0; ty < th; ++ty) {
        bool band = false;
        for (int tx = 0; tx < tw; ++tx) {
            bool tile = false;
            for (int y = ty * 16; y < ty * 16 + 16 && y < h; ++y) {
                int xs = tx * 16, n = xs + 16 <= w ? 16 : w - xs;
                if (memcmp(now + y * pitch + xs, last + y * w + xs, n) != 0) {
                    tile = true;
                    break;
                }
            }
            if (tile) {
                ++Dirty_Tiles;
                band = true;
                x0 = tx * 16 < x0 ? tx * 16 : x0;
                y0 = ty * 16 < y0 ? ty * 16 : y0;
                x1 = tx * 16 + 15 > x1 ? tx * 16 + 15 : x1;
                y1 = ty * 16 + 15 > y1 ? ty * 16 + 15 : y1;
            }
        }
        Dirty_Bands += band ? 1 : 0;
    }
    Dirty_Tiles_Total += tw * th;
    Dirty_Bands_Total += th;
    Dirty_Box_Pixels += x1 >= 0 ? (unsigned long long)(x1 - x0 + 1) * (y1 - y0 + 1) : 0;
    Dirty_Pixels_Total += (unsigned long long)w * h;
    ++Dirty_Frames;
    for (int y = 0; y < h; ++y) {
        memcpy(last + y * w, now + y * pitch, w);
    }
}

/*
** VC_PARTIALVERIFY: after every present, compare what is really on the screen (the
** display memory, read back) with the whole frame it should show, cursor and all,
** and report any difference: which present, the area and how many pixels. Slow (a
** full read of the screen each present), for testing partial presents.
** VC_PARTIALVERIFY=2 also saves the first few mismatching screens as PPM files.
*/
static struct
{
    std::vector<Uint8> expected; // what the screen should show now, in game pixels
    std::vector<Uint8> ref;      // the last frame presented, without the cursor
    int w = 0, h = 0;            // of both
    bool ref_valid = false;
    unsigned checked = 0, bad = 0, skipped = 0, dumps = 0;
    unsigned long long bad_pixels = 0;
    unsigned kind_checked[4] = {0, 0, 0, 0}, kind_bad[4] = {0, 0, 0, 0};
} PV;

static const char* const PV_Kinds[4] = {"full", "partial", "cursor", "cursor-on-screen"};

static bool PV_Enabled()
{
    static const bool on = getenv("VC_PARTIALVERIFY") != nullptr;
    return on;
}

static void PV_Totals(void)
{
    fprintf(stderr,
            "partialverify: totals: %u presents checked, %u mismatched (%llu pixels), %u skipped; "
            "full %u/%u, partial %u/%u, cursor %u/%u, cursor-on-screen %u/%u (bad/checked)\n",
            PV.checked,
            PV.bad,
            PV.bad_pixels,
            PV.skipped,
            PV.kind_bad[0],
            PV.kind_checked[0],
            PV.kind_bad[1],
            PV.kind_checked[1],
            PV.kind_bad[2],
            PV.kind_checked[2],
            PV.kind_bad[3],
            PV.kind_checked[3]);
}

// Copies a surface's pixels (top left w x h) into "to", raw.
static void PV_Grab(SDL_Surface* from, std::vector<Uint8>& to, int w, int h)
{
    to.resize(size_t(w) * h);
    SDL_LockSurface(from);
    for (int y = 0; y < h; ++y) {
        memcpy(&to[size_t(y) * w], static_cast<const Uint8*>(from->pixels) + y * from->pitch, w);
    }
    SDL_UnlockSurface(from);
}

/*
** Where the game's top left pixel is in the memory being displayed, and its line length.
** On RISC OS that's read from the VDU (DisplayStart), not from SDL's surface, so a back
** buffer that SDL failed to copy would show up too.
*/
static const Uint8* PV_Display(SDL_Surface* shown, int& pitch)
{
#ifdef __riscos__
    int vars[3] = {149, 6, -1}; // DisplayStart, LineLength
    int out[2] = {0, 0};
    _kernel_swi_regs regs;
    regs.r[0] = reinterpret_cast<int>(vars);
    regs.r[1] = reinterpret_cast<int>(out);
    _kernel_swi(OS_ReadVduVariables, &regs, &regs);
    static bool told = false;
    if (!told) {
        told = true;
        fprintf(stderr,
                "partialverify: display at %p, line %d; SDL screen surface pixels %p, pitch %d, offset %d, flags %x\n",
                reinterpret_cast<void*>(out[0]),
                out[1],
                shown->pixels,
                shown->pitch,
                shown->offset,
                unsigned(shown->flags));
        atexit(PV_Totals);
    }
    pitch = out[1];
    return reinterpret_cast<const Uint8*>(out[0]) + shown->offset;
#else
    static bool told = false;
    if (!told) {
        told = true;
        atexit(PV_Totals);
    }
    pitch = shown->pitch;
    return static_cast<const Uint8*>(shown->pixels) + shown->offset;
#endif
}

static void PV_Dump(const char* name, const Uint8* pixels, int w, int h, int pitch, int step, SDL_Color* pal)
{
    FILE* f = fopen(name, "wb");
    if (f == nullptr) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const SDL_Color& c = pal[pixels[size_t(y) * step * pitch + size_t(x) * step]];
            fputc(c.r, f);
            fputc(c.g, f);
            fputc(c.b, f);
        }
    }
    fclose(f);
}

// Compares the screen with PV.expected, after a present of the given kind.
static void PV_Compare(SDL_Surface* shown, int scale, int kind)
{
    int pitch = 0;
    int w = PV.w, h = PV.h;
    if (shown->w < w * scale || shown->h < h * scale) {
        ++PV.skipped;
        return;
    }
    SDL_LockSurface(shown);
    const Uint8* screen = PV_Display(shown, pitch);
    unsigned long long n = 0;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    // The frame counter is on the screen over the frame: its place is checked below, against
    // the frame with the counter drawn on it.
    const bool counter = Ctr.shown && Ctr.surface == shown && Ctr.generation == Window_Generation;
    for (int y = 0; y < h; ++y) {
        const Uint8* want = &PV.expected[size_t(y) * w];
        const Uint8* row = screen + size_t(y) * scale * pitch;
        const bool counter_row = counter && y * scale < Ctr.rect.y + Ctr.rect.h && (y + 1) * scale > Ctr.rect.y;
        if (scale == 1 && !counter_row && memcmp(row, want, w) == 0) {
            continue;
        }
        for (int x = 0; x < w; ++x) {
            if (counter_row && x * scale < Ctr.rect.x + Ctr.rect.w && (x + 1) * scale > Ctr.rect.x) {
                continue;
            }
            bool same = row[x * scale] == want[x];
            if (scale == 2) {
                same = same && row[x * 2 + 1] == want[x] && row[pitch + x * 2] == want[x] && row[pitch + x * 2 + 1] == want[x];
            }
            if (!same) {
                ++n;
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    for (int sy = counter ? Ctr.rect.y : 0; counter && sy < Ctr.rect.y + Ctr.rect.h && sy < h * scale; ++sy) {
        for (int sx = Ctr.rect.x; sx < Ctr.rect.x + Ctr.rect.w && sx < w * scale; ++sx) {
            Uint8 m = Ctr.mask[size_t(sy - Ctr.rect.y) * Ctr.rect.w + (sx - Ctr.rect.x)];
            Uint8 want = m == 2 ? Ctr.fg : m == 1 ? Ctr.bg : PV.expected[size_t(sy / scale) * w + sx / scale];
            if (screen[size_t(sy) * pitch + sx] != want) {
                int x = sx / scale, y = sy / scale;
                ++n;
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    ++PV.checked;
    ++PV.kind_checked[kind];
    if (n != 0) {
        ++PV.bad;
        ++PV.kind_bad[kind];
        PV.bad_pixels += n;
        if (PV.bad <= 50 || (PV.bad % 100) == 0) {
            fprintf(stderr,
                    "partialverify: MISMATCH at present %u (game frame %u, %s): %llu pixels in %d,%d %dx%d\n",
                    PV.checked,
                    Logic_Frame_Count,
                    PV_Kinds[kind],
                    n,
                    x0,
                    y0,
                    x1 - x0 + 1,
                    y1 - y0 + 1);
        }
        static const bool dump = getenv("VC_PARTIALVERIFY") != nullptr && atoi(getenv("VC_PARTIALVERIFY")) == 2;
        if (dump && PV.dumps < 4) {
            char name[64];
            snprintf(name, sizeof(name), "pvscreen%u", PV.dumps);
            PV_Dump(name, screen, w, h, pitch, scale, physpal);
            snprintf(name, sizeof(name), "pvwant%u", PV.dumps);
            PV_Dump(name, &PV.expected[0], w, h, w, 1, physpal);
            ++PV.dumps;
        }
    }
    SDL_UnlockSurface(shown);
    if ((PV.checked % 2000) == 0) {
        PV_Totals();
    }
}

/*
** Screenshots. Insert (wwkeyboard_sdl1.cpp) asks for one; the next present saves the
** game's own frame, as the game drew it (without the pointer, and without any border or
** the doubling or scaling to the display), as an 8 bit PNG with the game's palette.
** The files go in the Screenshots directory of the application, as SCRN0001, SCRN0002
** and so on, always the first number above those already there, so nothing is overwritten.
*/
static bool Shot_Pending = false;

static Uint32 Shot_Asked_At = 0;

void Video_Request_Screenshot(bool key_down)
{
    // (The game doesn't turn on key repeat, so one key down is one request; asking again
    // before the frame is saved is the same screenshot.)
    if (key_down && !Shot_Pending) {
        Shot_Pending = true;
        Shot_Asked_At = SDL_GetTicks();
    }
}

// The next number to use: one above the highest SCRNnnnn there (a ",xxx" type suffix is ignored).
static int Next_Screenshot_Number(const std::string& dir)
{
    int highest = 0;
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
        return 1;
    }
    while (struct dirent* e = readdir(d)) {
        const char* n = e->d_name;
        if (strncasecmp(n, "SCRN", 4) != 0) {
            continue;
        }
        int value = 0, digits = 0;
        for (n += 4; *n >= '0' && *n <= '9' && digits < 6; ++n, ++digits) {
            value = value * 10 + (*n - '0');
        }
        if (digits > 0 && value > highest) {
            highest = value;
        }
    }
    closedir(d);
    return highest + 1;
}

static void Save_Screenshot(SDL_Surface* from, int w, int h)
{
    if (w > from->w) {
        w = from->w;
    }
    if (h > from->h) {
        h = from->h;
    }
    std::string dir = std::string(Paths.Program_Path()) + "/Screenshots";
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Screenshot: can't make %s: %s\n", dir.c_str(), strerror(errno));
        return;
    }

    unsigned char palette[768];
    for (int i = 0; i < 256; ++i) {
        palette[i * 3] = physpal[i].r;
        palette[i * 3 + 1] = physpal[i].g;
        palette[i * 3 + 2] = physpal[i].b;
    }

    int number = Next_Screenshot_Number(dir);
    std::string path;
    for (int tries = 0; tries < 20; ++tries, ++number) { // (a name that turns up since the listing is skipped)
        char leaf[16];
        snprintf(leaf, sizeof(leaf), "/SCRN%04d", number);
        path = dir + leaf;
        FILE* exists = fopen(path.c_str(), "rb");
        if (exists == nullptr) {
            break;
        }
        fclose(exists);
        path.clear();
    }
    if (path.empty()) {
        fprintf(stderr, "Screenshot: no free name in %s\n", dir.c_str());
        return;
    }

    FILE* fp = fopen(path.c_str(), "wb");
    if (fp == nullptr) {
        fprintf(stderr, "Screenshot: can't write %s: %s\n", path.c_str(), strerror(errno));
        return;
    }
    SDL_LockSurface(from);
    bool ok = PNG_Write_Paletted(fp, w, h, static_cast<const unsigned char*>(from->pixels), from->pitch, palette);
    SDL_UnlockSurface(from);
    if (fclose(fp) != 0) {
        ok = false;
    }
    if (!ok) {
        fprintf(stderr, "Screenshot: couldn't write %s (disc full?)\n", path.c_str());
        remove(path.c_str());
        return;
    }
#ifdef __riscos__
    // The RISC OS name (<VanillaTD$Dir>.Screenshots.SCRN0001) and its type, PNG.
    std::string native = path.substr(path.size() > 0 && path[0] == '/' ? 1 : 0);
    for (size_t i = 0; i < native.size(); ++i) {
        if (native[i] == '/') {
            native[i] = '.';
        }
    }
    _kernel_swi_regs regs;
    regs.r[0] = 18;
    regs.r[1] = reinterpret_cast<int>(native.c_str());
    regs.r[2] = 0xB60;
    if (_kernel_swi(OS_File, &regs, &regs) != nullptr) {
        fprintf(stderr, "Screenshot: couldn't set the type of %s\n", native.c_str());
    }
#endif
    fprintf(stderr, "Screenshot saved: %s (%dx%d)\n", path.c_str(), w, h);
    Screenshot_Message_Due = true; // the game's main loop says so on the screen
}

static void Raw_Copy(SDL_Surface* from, SDL_Surface* to)
{
    int w = from->w < to->w ? from->w : to->w;
    int h = from->h < to->h ? from->h : to->h;
    SDL_LockSurface(from);
    SDL_LockSurface(to);
#if defined(__riscos__) && defined(__arm__)
    // An LDM/STM loop copied a frame to the screen in 10.9 ms on a StrongARM Risc PC,
    // against 15.2 ms with UnixLib's memcpy.
    if (!Present_With_Memcpy()) {
        for (int row = 0; row < h; ++row) {
            VC_Copy_Span(static_cast<Uint8*>(to->pixels) + row * to->pitch,
                         static_cast<const Uint8*>(from->pixels) + row * from->pitch,
                         unsigned(w));
        }
        SDL_UnlockSurface(to);
        SDL_UnlockSurface(from);
        return;
    }
#endif
    for (int row = 0; row < h; ++row) {
        memcpy(static_cast<Uint8*>(to->pixels) + row * to->pitch,
               static_cast<const Uint8*>(from->pixels) + row * from->pitch,
               w);
    }
    SDL_UnlockSurface(to);
    SDL_UnlockSurface(from);
}

/*
** The visible surface (SeenBuff) normally gets each frame by a full copy from the
** hidden one (HidPage) in Blit_Display, and then presenting copies it again to the
** screen. On a Risc PC each of those copies costs 15-20 ms. So instead Blit_Display
** can call Show_Hidden: the visible surface then "mirrors" the hidden one, and the
** next present copies the hidden surface straight to the screen. After that the
** frame lives on the screen, and the game can draw its next frame on the hidden
** surface. Whenever anything touches the visible surface (a dialog drawing on it,
** say), Settle first fills it in with what it would have held: the hidden surface
** if the frame hasn't been presented yet, else the screen. Touching the hidden
** surface before its frame is presented settles too. VC_NODEFERBLIT turns it off.
*/
class VideoSurfaceSDL1 : public VideoSurface
{
public:
    VideoSurfaceSDL1(int w, int h, GBC_Enum flags)
        : flags(flags)
    {
        surface = SDL_CreateRGBSurface(SDL_HWSURFACE, w, h, 8, 0, 0, 0, 0);
        SDL_SetPalette(surface, SDL_LOGPAL, logpal, 0, 256);
        tiles_w = (w + TILE - 1) / TILE;
        tiles_h = (h + TILE - 1) / TILE;
        tiles.assign(size_t(tiles_w) * tiles_h, 0);
        All_Surfaces->push_back(this);
        // On unless VC_FULLPRESENT is set (VC_PARTIALPRESENT, which turned it on, is still accepted).
        static const bool partial = getenv("VC_FULLPRESENT") == nullptr;
        if (partial) {
            Video_Written_Hook = Written;
        }

        if (flags & GBC_VISIBLE) {
            frontSurface = this;
        }
    }

    virtual ~VideoSurfaceSDL1()
    {
        for (size_t i = 0; i < All_Surfaces->size(); ++i) {
            if ((*All_Surfaces)[i] == this) {
                All_Surfaces->erase(All_Surfaces->begin() + i);
                break;
            }
        }
        if (frontSurface != nullptr && frontSurface->screen_holds == this) {
            frontSurface->screen_holds = nullptr;
        }
        if (frontSurface != nullptr && frontSurface->mirror == this) {
            frontSurface->Settle();
        }
        if (frontSurface == this) {
            mirror = nullptr;
            frontSurface = nullptr;
        }

        SDL_FreeSurface(surface);
    }

    virtual void* GetData() const
    {
        return surface->pixels;
    }
    virtual int GetPitch() const
    {
        return surface->pitch;
    }
    virtual bool IsAllocated() const
    {
        return false;
    }

    virtual void AddAttachedSurface(VideoSurface* surface)
    {
    }

    virtual bool IsReadyToBlit()
    {
        return true;
    }

    virtual bool LockWait()
    {
        Touch();
        changed = true; // Anything that draws on the surface locks it first.
        marks_at_lock = Video_Mark_Count;
        return (SDL_LockSurface(surface) == 0);
    }

    virtual bool Unlock()
    {
        static const bool strict = getenv("VC_DIRTYCHECK") != nullptr && atoi(getenv("VC_DIRTYCHECK")) == 2;
        if (strict && shadow_of == this) {
            fprintf(stderr, "dirtycheck: (at unlock)\n");
            Check_Unmarked_Now();
        }
        if (Video_Mark_Count == marks_at_lock) {
            all_dirty = true; // locked by something that doesn't say where it draws
            ++Dirty_Unknown_Locks;
#ifdef __APPLE__
            // VC_DIRTYCHECK=3: the first few unmarked locks, with where they came from.
            static const bool trace = getenv("VC_DIRTYCHECK") != nullptr && atoi(getenv("VC_DIRTYCHECK")) == 3;
            static int traced = 0;
            if (trace && traced < 6) {
                void* frames[12];
                fprintf(stderr, "dirtycheck: lock with nothing marked (surface %p):\n", static_cast<void*>(this));
                backtrace_symbols_fd(frames, backtrace(frames, 12), 2);
                ++traced;
            }
#endif
        }
        SDL_UnlockSurface(surface);
        return true;
    }

    virtual void Blt(const Rect& destRect, VideoSurface* src, const Rect& srcRect, bool mask)
    {
        SDL_Rect srcRectSDL = Make_SDL_Rect(srcRect.X, srcRect.Y, srcRect.Width, srcRect.Height);
        SDL_Rect destRectSDL = Make_SDL_Rect(destRect.X, destRect.Y, destRect.Width, destRect.Height);
        ((VideoSurfaceSDL1*)src)->Touch();
        Touch();
        changed = true;
        Mark(destRectSDL.x, destRectSDL.y, destRectSDL.w, destRectSDL.h);
        SDL_BlitSurface(((VideoSurfaceSDL1*)src)->surface, &srcRectSDL, surface, &destRectSDL);
    }

    virtual void FillRect(const Rect& rect, unsigned char color)
    {
        SDL_Rect rectSDL = Make_SDL_Rect(rect.X, rect.Y, rect.Width + 1, rect.Height + 1);
        Touch();
        changed = true;
        Mark(rectSDL.x, rectSDL.y, rectSDL.w, rectSDL.h);
        SDL_FillRect(surface, &rectSDL, color);
    }

    // Blit_Display: make this (the visible surface) show "hidden" without copying it.
    bool Show_Hidden(VideoSurfaceSDL1* hidden)
    {
        if (hidden == this || hidden->surface->w != surface->w || hidden->surface->h != surface->h) {
            return false;
        }
        if (mirror != nullptr && mirror != hidden) {
            Settle();
        }
        mirror = hidden;
        mirror_shown = false;
        changed = true;
        return true;
    }

    /*
    ** Between presents: move the cursor if nothing else has changed. A new frame
    ** waits for the next proper present, to keep those to the frame limit.
    */
    void Update_Cursor()
    {
        static const bool always_full = getenv("VC_FULLPRESENT") != nullptr;
        static const bool off = getenv("VC_NOPOINTERUPDATE") != nullptr;
        if (off || always_full || changed || !presented || (mirror != nullptr && !mirror_shown) || Shake_Count > 0
            || Shake_Undo) {
            return;
        }
        /*
        ** Pick up pointer movement since the game last read its input. In fullscreen
        ** (RawInput) the pointer is moved by the motion events, so apply those, but
        ** leave everything else queued for the game; and if a click is waiting, leave
        ** the motion too, so the click still lands where the pointer was.
        */
        SDL_PumpEvents();
        SDL_Event events[32];
        if (SDL_PeepEvents(events, 32, SDL_PEEKEVENT, SDL_MOUSEBUTTONDOWNMASK | SDL_MOUSEBUTTONUPMASK) == 0) {
            int n;
            while ((n = SDL_PeepEvents(events, 32, SDL_GETEVENT, SDL_MOUSEMOTIONMASK)) > 0) {
                for (int i = 0; i < n; ++i) {
                    ++Phase_Counts[COUNT_MOTION_BETWEEN_FRAMES];
                    Move_Video_Mouse(float(events[i].motion.xrel), float(events[i].motion.yrel));
                }
            }
        }
        RenderSurface();
    }

    // The surface that holds the frame as it stands, for a screenshot of a still screen.
    SDL_Surface* Frame_Now()
    {
        return mirror != nullptr ? mirror->surface : surface;
    }

    // Fill in the surface with the frame it stands for, before anything uses it.
    void Settle()
    {
        VideoSurfaceSDL1* from = mirror;
        mirror = nullptr;
        /*
        ** At exit, SDL_Quit (registered with atexit after the static surfaces were
        ** made) has already freed the screen by the time their destructors get here.
        ** Copying from it then crashed on RISC OS 5.
        */
        if (from == nullptr || SDL_WasInit(SDL_INIT_VIDEO) == 0) {
            return;
        }
        if (!mirror_shown) {
            Raw_Copy(from->surface, surface);
        } else if (window != nullptr && window->pixels != nullptr) {
            // The frame is on the screen, with the cursor drawn over it (and the frame counter, off first).
            if (Screen == nullptr) {
                Counter_Remove();
            }
            Raw_Copy(window, surface);
            if (last_cursor) {
                Put_Under(surface, last_drawn);
            }
        }
    }

    void RenderSurface()
    {
        SDL_Rect area = {0, 0, 0, 0};
        bool cursor = !Get_Mouse_State() && hwcursor.Surface != nullptr;

        if (cursor) {
            int x, y;
            Get_Cursor_Position(x, y);
            area = Make_SDL_Rect(x - hwcursor.HotX, y - hwcursor.HotY, hwcursor.Surface->w, hwcursor.Surface->h);
        }

        /*
        ** The game presents about twice for every frame it draws, and a full copy to
        ** the screen is the most expensive thing a Risc PC does all frame. If nothing
        ** has drawn on the surface since the last present, copy only where the cursor
        ** was and now is, or nothing at all if it hasn't changed. VC_FULLPRESENT turns
        ** this off, for comparison.
        */
        static const bool always_full = getenv("VC_FULLPRESENT") != nullptr;
        Counter_Tick();
        if (Shake_Count > 0) {
            Present_Shaken();
            return;
        }
        bool force_full = always_full || !presented || Shake_Undo; // copy all of it, not just what changed
        bool full = changed || force_full;
        Shake_Undo = false;
        if (!full && !Shot_Pending && cursor == last_cursor && Cursor_Generation == last_generation
            && (!cursor || (area.x == last_area.x && area.y == last_area.y)) && !Counter_Stale()) {
            ++Phase_Counts[COUNT_PRESENT_SKIPPED];
            return;
        }
        ++Phase_Counts[full ? COUNT_PRESENT_FULL : COUNT_PRESENT_PARTIAL];
        /*
        ** The counter is left on the screen if this present doesn't touch its place (a partial
        ** one, with the pointer elsewhere): it would otherwise be missing for the length of the
        ** present, over and over. A full present copies the frame over it, in some places: it is
        ** given the frame's pixels afterwards instead (Counter_Refresh). Otherwise the screen is
        ** put back as the frame left it, for the copying below.
        */
        bool counter_refresh = false;
        if (Ctr.shown) {
            bool meets = Shot_Pending || Counter_Meets(Union(last_cursor ? last_drawn : SDL_Rect{0, 0, 0, 0},
                                                              cursor ? area : SDL_Rect{0, 0, 0, 0}));
            if (!meets && full) {
                counter_refresh = true;
            } else if (meets || full) {
                Counter_Remove();
            }
        }

        // The frame to show: the hidden surface if this one mirrors it.
        SDL_Surface* frame = mirror != nullptr ? mirror->surface : surface;
        SDL_Rect drawn = area;
        SDL_Rect dirty = {0, 0, 0, 0};
        int pv_kind = -1; // VC_PARTIALVERIFY: what kind of present to check, if any
        bool copied_changed = false;

        if (!full && mirror != nullptr && mirror_shown) {
            /*
            ** The frame is only on the screen now (the hidden surface may already hold
            ** part of the next one), so move the cursor on the screen itself: put back
            ** what was under it, then save what's under the new place and draw it there.
            */
            if (last_cursor) {
                Put_Under(window, last_drawn);
            }
            if (Shot_Pending) {
                Shot_Pending = false; // the screen holds the frame, without the pointer now
                Save_Screenshot(window, frame->w, frame->h);
            }
            if (cursor) {
                cursor = Save_Area(window, drawn);
                if (cursor) {
                    Draw_Cursor(window, area);
                }
            }
            dirty = Union(last_cursor ? last_drawn : dirty, cursor ? drawn : dirty);
            if (PV_Enabled()) {
                if (PV.ref_valid) {
                    PV.expected = PV.ref;
                    if (cursor) {
                        SDL_Surface* want = SDL_CreateRGBSurfaceFrom(&PV.expected[0], PV.w, PV.h, 8, PV.w, 0, 0, 0, 0);
                        if (want != nullptr) {
                            Draw_Cursor(want, area);
                            SDL_FreeSurface(want);
                        }
                    }
                    pv_kind = 3;
                } else {
                    ++PV.skipped;
                }
            }
        } else {
            /*
            ** Draw the software cursor into the frame before copying it to the screen,
            ** then put back what was under it. Drawing it on the screen after the copy
            ** leaves it missing for part of every frame, which flickers badly on real
            ** hardware.
            */
            if (Shot_Pending) {
                Shot_Pending = false; // the whole frame, before the pointer goes on it
                Save_Screenshot(frame, frame->w, frame->h);
            }
            if (cursor) {
                cursor = Save_Area(frame, drawn); // clips "drawn" to the surface
                if (cursor) {
                    Draw_Cursor(frame, area);
                }
            }

            if (full) {
                static const bool dirty_stat = getenv("VC_DIRTYSTAT") != nullptr;
                if (dirty_stat) {
                    Dirty_Stat(frame);
                }
                copied_changed = Copy_Changed(frame, drawn, cursor, force_full);
                if (!copied_changed) {
                    static const bool check = getenv("VC_DIRTYCHECK") != nullptr;
                    if (check && mirror != nullptr) {
                        Check_Marks(frame, drawn, cursor, false); // just take a copy
                        ++Presents;
                    }
                    Raw_Copy(frame, window);
                }
            } else {
                dirty = Union(last_cursor ? last_drawn : dirty, cursor ? drawn : dirty);
                if (dirty.w > 0 && dirty.h > 0) {
                    SDL_Rect src = dirty, dst = dirty;
                    SDL_BlitSurface(frame, &src, window, &dst);
                }
            }

            if (PV_Enabled()) {
                PV.w = frame->w < window->w ? frame->w : window->w;
                PV.h = frame->h < window->h ? frame->h : window->h;
                PV_Grab(frame, PV.expected, PV.w, PV.h);
                pv_kind = !full ? 2 : copied_changed ? 1 : 0;
            }
            if (cursor) {
                Put_Under(frame, drawn);
            }
            if (counter_refresh) {
                Counter_Refresh(frame);
            }
            if (PV_Enabled()) {
                PV_Grab(frame, PV.ref, PV.w, PV.h);
                PV.ref_valid = true;
            }
            if (mirror != nullptr) {
                mirror_shown = true;
            }
            if (full) {
                // What the screen now holds, for the next present to copy only changes to it.
                screen_holds = mirror;
                screen_generation = Window_Generation;
                if (mirror != nullptr) {
                    mirror->Clear_Marks();
                }
            }
        }

        if (full && !spans.empty()) {
            Show_Rects(int(spans.size()), &spans[0]);
        } else if (full) {
            Show_All();
        } else if (dirty.w > 0 && dirty.h > 0) {
            Show_Rects(1, &dirty);
        } else if (Ctr.on || Ctr.update) {
            Show_Rects(0, nullptr); // only the frame counter's place
        }
        if (pv_kind >= 0) {
            PV_Compare(Shown(), Scale, pv_kind);
        }

        changed = false;
        presented = true;
        last_cursor = cursor;
        last_area = area;
        last_drawn = drawn;
        last_generation = Cursor_Generation;
    }

private:
    /*
    ** Shows the frame moved up or down by the next queued shake offset, without the
    ** cursor (as the original hid it). The rows it uncovers keep what was there.
    ** The screen then isn't the frame, so the mirror isn't marked shown, and the
    ** next ordinary present is a full one.
    */
    void Present_Shaken()
    {
        int dy = Shake_Queue[0];
        for (int i = 1; i < Shake_Count; ++i) {
            Shake_Queue[i - 1] = Shake_Queue[i];
        }
        --Shake_Count;
        ++Phase_Counts[COUNT_PRESENT_FULL];

        Counter_Remove(); // the rows the shake leaves alone must not keep it
        SDL_Surface* frame = mirror != nullptr ? mirror->surface : surface;
        int h = (frame->h < window->h ? frame->h : window->h) - (dy < 0 ? -dy : dy);
        int w = frame->w < window->w ? frame->w : window->w;
        SDL_LockSurface(frame);
        SDL_LockSurface(window);
        for (int row = 0; row < h; ++row) {
            int from = dy < 0 ? row - dy : row;
            int to = dy < 0 ? row : row + dy;
            memcpy(static_cast<Uint8*>(window->pixels) + to * window->pitch,
                   static_cast<const Uint8*>(frame->pixels) + from * frame->pitch,
                   w);
        }
        SDL_UnlockSurface(window);
        SDL_UnlockSurface(frame);
        Show_All();
        if (mirror != nullptr && mirror_shown) {
            // The frame was only on the screen, which is now shifted: keep a copy first.
            Raw_Copy(mirror->surface, surface);
            mirror = nullptr;
        }
        Shake_Undo = true;
        PV.ref_valid = false;
        last_cursor = false;
        presented = true;
        screen_holds = nullptr;
    }

    // About to be read or written: make sure it holds what it should.
    void Touch()
    {
        if (frontSurface == nullptr) {
            return;
        }
        if (this == frontSurface) {
            Settle();
        } else if (frontSurface->mirror == this && !frontSurface->mirror_shown) {
            frontSurface->Settle();
        }
    }

    static SDL_Rect Union(const SDL_Rect& a, const SDL_Rect& b)
    {
        if (a.w <= 0 || a.h <= 0) {
            return b;
        }
        if (b.w <= 0 || b.h <= 0) {
            return a;
        }
        int x0 = a.x < b.x ? a.x : b.x;
        int y0 = a.y < b.y ? a.y : b.y;
        int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
        int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
        return Make_SDL_Rect(x0, y0, x1 - x0, y1 - y0);
    }

    /*
    ** Copies the part of "area" inside the surface to a side buffer, clipping "area" to
    ** match. Raw copies, as SDL would remap colours between 8 bit palettes.
    */
    bool Save_Area(SDL_Surface* on, SDL_Rect& area)
    {
        int x0 = area.x < 0 ? 0 : area.x;
        int y0 = area.y < 0 ? 0 : area.y;
        int x1 = area.x + area.w > on->w ? on->w : area.x + area.w;
        int y1 = area.y + area.h > on->h ? on->h : area.y + area.h;

        if (x1 <= x0 || y1 <= y0) {
            return false;
        }
        area = Make_SDL_Rect(x0, y0, x1 - x0, y1 - y0);
        under.resize(size_t(area.w) * area.h);

        SDL_LockSurface(on);
        for (int row = 0; row < area.h; ++row) {
            memcpy(&under[size_t(row) * area.w],
                   static_cast<Uint8*>(on->pixels) + (area.y + row) * on->pitch + area.x,
                   area.w);
        }
        SDL_UnlockSurface(on);
        return true;
    }

    // Puts back what Save_Area saved.
    void Put_Under(SDL_Surface* on, const SDL_Rect& area)
    {
        if (under.size() < size_t(area.w) * area.h) {
            return;
        }
        SDL_LockSurface(on);
        for (int row = 0; row < area.h; ++row) {
            memcpy(static_cast<Uint8*>(on->pixels) + (area.y + row) * on->pitch + area.x,
                   &under[size_t(row) * area.w],
                   area.w);
        }
        SDL_UnlockSurface(on);
    }

    // Draws the cursor with its top left at "at", colour 0 transparent, clipped.
    void Draw_Cursor(SDL_Surface* on, const SDL_Rect& at)
    {
        const Uint8* shape = static_cast<const Uint8*>(hwcursor.Surface->pixels);
        int pitch = hwcursor.Surface->pitch;
        SDL_LockSurface(on);
        for (int row = 0; row < at.h; ++row) {
            int y = at.y + row;
            if (y < 0 || y >= on->h) {
                continue;
            }
            Uint8* dst = static_cast<Uint8*>(on->pixels) + y * on->pitch;
            const Uint8* src = shape + row * pitch;
            for (int col = 0; col < at.w; ++col) {
                int x = at.x + col;
                if (src[col] != 0 && x >= 0 && x < on->w) {
                    dst[x] = src[col];
                }
            }
        }
        SDL_UnlockSurface(on);
    }

    /*
    ** Partial presents. Each surface keeps a grid of 16x16 tiles, marked as the
    ** drawing routines report what they write (Mark_Written, via Written). When the
    ** screen already holds this surface's last frame, a present copies only the tiles
    ** written since, plus the cursor's old and new places: in a busy battle about 6%
    ** of the screen changes per frame. A lock that ends with nothing marked means
    ** something drew without saying where, so then the whole surface is copied.
    **
    ** It's on unless VC_FULLPRESENT is set, which also leaves out the marking. (It was
    ** once on only with VC_PARTIALPRESENT=1, which is still accepted and does nothing.)
    ** In the battle benchmark on the StrongARM Risc PC (same image, runs
    ** alternated) frames went from 54.4 to 48.1 ms: presenting 10.8 to 4.1 ms,
    ** drawing 17.0 to 18.0 ms for the marking.
    ** VC_DIRTYCHECK=1 compares each partial present with the whole frame and reports
    ** tiles that changed without being marked; VC_DIRTYCHECK=2 (slow) checks at
    ** every drawing call and lock, with a backtrace on the Mac, to find the culprit.
    */
    enum
    {
        TILE_SHIFT = 4,
        TILE = 1 << TILE_SHIFT
    };

    void Mark(int x, int y, int w, int h)
    {
        ++Video_Mark_Count;
        if (w <= 0 || h <= 0 || x + w <= 0 || y + h <= 0) {
            return;
        }
        int x0 = x < 0 ? 0 : x >> TILE_SHIFT;
        int y0 = y < 0 ? 0 : y >> TILE_SHIFT;
        int x1 = (x + w - 1) >> TILE_SHIFT;
        int y1 = (y + h - 1) >> TILE_SHIFT;
        if (x0 >= tiles_w || y0 >= tiles_h) {
            return;
        }
        x1 = x1 < tiles_w ? x1 : tiles_w - 1;
        y1 = y1 < tiles_h ? y1 : tiles_h - 1;
        for (int ty = y0; ty <= y1; ++ty) {
            memset(&tiles[size_t(ty) * tiles_w + x0], 1, x1 - x0 + 1 > 0 ? x1 - x0 + 1 : 0);
        }
    }

    void Clear_Marks()
    {
        memset(&tiles[0], 0, tiles.size());
        all_dirty = false;
    }

    // Video_Written_Hook.
    static void Written(VideoSurface* where, int x, int y, int w, int h)
    {
        static const bool strict = getenv("VC_DIRTYCHECK") != nullptr && atoi(getenv("VC_DIRTYCHECK")) == 2;
        if (strict && shadow_of != nullptr) {
            shadow_of->Check_Unmarked_Now();
            Last_Presents = Presents;
#ifdef __APPLE__
            Last_Depth = backtrace(Last_Frames, 10);
            Last_Rect[0] = x;
            Last_Rect[1] = y;
            Last_Rect[2] = w;
            Last_Rect[3] = h;
#endif
        }
        static_cast<VideoSurfaceSDL1*>(where)->Mark(x, y, w, h);
    }

    /*
    ** Copies the changed tiles of "frame" (the mirrored hidden surface, cursor drawn
    ** in) to the screen, as spans of adjacent tiles, and lists them in "spans".
    ** Returns false, copying nothing, if it all has to be copied.
    */
    bool Copy_Changed(SDL_Surface* frame, const SDL_Rect& drawn, bool cursor, bool force_full)
    {
        spans.clear();
        if (Video_Written_Hook == nullptr || mirror == nullptr || force_full || mirror->all_dirty || screen_holds != mirror
            || screen_generation != Window_Generation || frame->w != window->w || frame->h != window->h
            || mirror->tiles_w != (window->w + TILE - 1) / TILE) {
            if (mirror != nullptr) {
                ++(mirror->all_dirty ? Dirty_Full_Unknown : Dirty_Full_Other);
            }
            return false;
        }
        std::vector<Uint8>& t = mirror->tiles;
        if (last_cursor) {
            mirror->Mark(last_drawn.x, last_drawn.y, last_drawn.w, last_drawn.h);
        }
        if (cursor) {
            mirror->Mark(drawn.x, drawn.y, drawn.w, drawn.h);
        }

        static const bool check = getenv("VC_DIRTYCHECK") != nullptr;
        if (check) {
            Check_Marks(frame, drawn, cursor, true);
            ++Presents;
        }

        // VC_PARTIALBREAK=<n> (to test VC_PARTIALVERIFY): every n-th partial present leaves out its first span.
        static const int break_every = getenv("VC_PARTIALBREAK") != nullptr ? atoi(getenv("VC_PARTIALBREAK")) : 0;
        static unsigned break_count = 0;
        bool leave_out = break_every > 0 && (++break_count % unsigned(break_every)) == 0;

        SDL_LockSurface(frame);
        SDL_LockSurface(window);
        int tw = mirror->tiles_w;
        for (int ty = 0; ty < mirror->tiles_h; ++ty) {
            for (int tx = 0; tx < tw;) {
                if (!t[size_t(ty) * tw + tx]) {
                    ++tx;
                    continue;
                }
                int start = tx;
                while (tx < tw && t[size_t(ty) * tw + tx]) {
                    ++tx;
                }
                SDL_Rect span = Make_SDL_Rect(start * TILE, ty * TILE, (tx - start) * TILE, TILE);
                span.w = span.x + span.w > frame->w ? frame->w - span.x : span.w;
                span.h = span.y + span.h > frame->h ? frame->h - span.y : span.h;
                for (int row = 0; row < span.h && !leave_out; ++row) {
                    Uint8* to = static_cast<Uint8*>(window->pixels) + (span.y + row) * window->pitch + span.x;
                    const Uint8* from = static_cast<const Uint8*>(frame->pixels) + (span.y + row) * frame->pitch + span.x;
#if defined(__riscos__) && defined(__arm__)
                    if (!Present_With_Memcpy()) {
                        VC_Copy_Span(to, from, unsigned(span.w));
                        continue;
                    }
#endif
                    memcpy(to, from, span.w);
                }
                spans.push_back(span);
                Dirty_Tiles_Copied += tx - start;
                leave_out = false;
            }
        }
        SDL_UnlockSurface(window);
        SDL_UnlockSurface(frame);
        ++Dirty_Partial;
        return true;
    }

    // VC_DIRTYCHECK: compare the frame with the last one presented, outside the marked tiles.
    void Check_Marks(SDL_Surface* frame, const SDL_Rect& drawn, bool cursor, bool compare)
    {
        int w = frame->w, h = frame->h, tw = mirror->tiles_w;
        if (compare && shadow_of == mirror && shadow.size() == size_t(w) * h) {
            int missed = 0;
            for (int ty = 0; ty < mirror->tiles_h; ++ty) {
                for (int tx = 0; tx < tw; ++tx) {
                    if (mirror->tiles[size_t(ty) * tw + tx]) {
                        continue;
                    }
                    for (int y = ty * TILE; y < ty * TILE + TILE && y < h; ++y) {
                        int x = tx * TILE, n = x + TILE <= w ? TILE : w - x;
                        if (memcmp(static_cast<const Uint8*>(frame->pixels) + y * frame->pitch + x, &shadow[size_t(y) * w + x], n)
                            != 0) {
                            if (missed < 4) {
                                fprintf(stderr, "dirtycheck: tile at %d,%d changed without being marked\n", x, ty * TILE);
                            }
                            ++missed;
                            break;
                        }
                    }
                }
            }
            Dirty_Missed += missed;
        }
        // Keep the frame without the cursor (put back what it covers in the copy).
        shadow.resize(size_t(w) * h);
        for (int y = 0; y < h; ++y) {
            memcpy(&shadow[size_t(y) * w], static_cast<const Uint8*>(frame->pixels) + y * frame->pitch, w);
        }
        if (cursor && under.size() >= size_t(drawn.w) * drawn.h) {
            for (int row = 0; row < drawn.h; ++row) {
                memcpy(&shadow[size_t(drawn.y + row) * w + drawn.x], &under[size_t(row) * drawn.w], drawn.w);
            }
        }
        shadow_of = mirror;
    }

    // VC_DIRTYCHECK=2 (slow): has an unmarked tile changed since the last present?
    void Check_Unmarked_Now()
    {
        int w = surface->w, h = surface->h;
        if (shadow.size() != size_t(w) * h) {
            return;
        }
        for (int ty = 0; ty < tiles_h; ++ty) {
            for (int tx = 0; tx < tiles_w; ++tx) {
                if (tiles[size_t(ty) * tiles_w + tx]) {
                    continue;
                }
                for (int y = ty * TILE; y < ty * TILE + TILE && y < h; ++y) {
                    int x = tx * TILE, n = x + TILE <= w ? TILE : w - x;
                    if (memcmp(static_cast<const Uint8*>(surface->pixels) + y * surface->pitch + x, &shadow[size_t(y) * w + x], n) != 0) {
                        fprintf(stderr,
                                "dirtycheck: unmarked write to tile at %d,%d, %u presents since the previous report, "
                                "found at:\n",
                                x,
                                ty * TILE,
                                Presents - Last_Presents);
#ifdef __APPLE__
                        void* frames[10];
                        backtrace_symbols_fd(frames, backtrace(frames, 10), 2);
                        fprintf(stderr,
                                "dirtycheck: the previous report was %d,%d %dx%d from:\n",
                                Last_Rect[0],
                                Last_Rect[1],
                                Last_Rect[2],
                                Last_Rect[3]);
                        backtrace_symbols_fd(Last_Frames, Last_Depth, 2);
#endif
                        tiles[size_t(ty) * tiles_w + tx] = 1;
                        break;
                    }
                }
            }
        }
    }

    static std::vector<VideoSurfaceSDL1*>* All_Surfaces; // never freed: surfaces outlive static destructors
    static std::vector<Uint8> shadow;                    // VC_DIRTYCHECK: the last frame presented
    static void* Last_Frames[10];
    static int Last_Depth, Last_Rect[4];
    static unsigned Presents, Last_Presents;
    static VideoSurfaceSDL1* shadow_of;
    std::vector<Uint8> tiles;
    int tiles_w = 0, tiles_h = 0;
    bool all_dirty = true;
    unsigned marks_at_lock = 0;
    VideoSurfaceSDL1* screen_holds = nullptr; // whose frame the screen shows, if any
    unsigned screen_generation = 0;
    std::vector<SDL_Rect> spans;

    // What was presented last, so an unchanged frame needn't be copied again.
    bool changed = true;
    bool presented = false;
    bool last_cursor = false;
    SDL_Rect last_area = {0, 0, 0, 0};
    SDL_Rect last_drawn = {0, 0, 0, 0};
    unsigned last_generation = 0;

    // The hidden surface this one stands for, and whether that frame is on the screen yet.
    VideoSurfaceSDL1* mirror = nullptr;
    bool mirror_shown = false;

    SDL_Surface* surface;
    GBC_Enum flags;
    std::vector<Uint8> under; // What the software cursor covers while it is drawn.

    friend bool Video_Show_Hidden(VideoSurface* hidden);
    friend void Video_Settle_Front();
};

std::vector<VideoSurfaceSDL1*>* VideoSurfaceSDL1::All_Surfaces = new std::vector<VideoSurfaceSDL1*>;
std::vector<Uint8> VideoSurfaceSDL1::shadow;
void* VideoSurfaceSDL1::Last_Frames[10];
unsigned VideoSurfaceSDL1::Presents, VideoSurfaceSDL1::Last_Presents;
int VideoSurfaceSDL1::Last_Depth, VideoSurfaceSDL1::Last_Rect[4];
VideoSurfaceSDL1* VideoSurfaceSDL1::shadow_of = nullptr;

/*
** Blit_Display calls this instead of copying the hidden page to the visible one;
** false means copy as usual.
*/
bool Video_Show_Hidden(VideoSurface* hidden)
{
    static const bool off = getenv("VC_NODEFERBLIT") != nullptr;
    if (off || frontSurface == nullptr || hidden == nullptr) {
        return false;
    }
    return frontSurface->Show_Hidden(static_cast<VideoSurfaceSDL1*>(hidden));
}

void Video_Update_Cursor();

// After the keyboard has been read: a screenshot asked for while the screen isn't changing (a
// dialog waiting for a click, say) would otherwise wait for the next present, which is a mouse move.
void Video_Service_Screenshot()
{
    if (!Shot_Pending) {
        return;
    }
    Video_Update_Cursor();
    /*
    ** If that couldn't present (a dialog drawing on the visible page, say) and nothing has
    ** by a tenth of a second, which a running game would have, the screen is standing still
    ** and its frame is what to save.
    */
    if (Shot_Pending && frontSurface != nullptr && SDL_GetTicks() - Shot_Asked_At > 100) {
        Shot_Pending = false;
        SDL_Surface* frame = frontSurface->Frame_Now();
        if (frame != nullptr) {
            Save_Screenshot(frame, frame->w, frame->h);
        }
    }
}

// Moves the cursor between presents (see Frame_Limiter).
void Video_Update_Cursor()
{
    if (frontSurface != nullptr && window != nullptr) {
        frontSurface->Update_Cursor();
    }
}

// Before the screen changes under the visible surface (a new video mode).
void Video_Settle_Front()
{
    if (frontSurface != nullptr) {
        frontSurface->Settle();
        frontSurface->presented = false;
    }
}

// Mouse button events seen by the SDL1 keyboard code, for the VC_FPSLOG report.
extern unsigned SDL1_Mouse_Button_Events;

/*
** With VC_FPSLOG set, print frames presented per second and the time spent copying
** them to the screen every 5 seconds, to measure speed on real hardware.
*/
static void Log_Frame_Rate(Uint32 render_ms)
{
    static const bool enabled = getenv("VC_FPSLOG") != nullptr;
    static Uint32 start = 0;
    static unsigned frames = 0;
    static Uint32 render_total = 0;
    static unsigned last_buttons = 0;
    static unsigned last_logic = 0;

    if (!enabled) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    if (start == 0) {
        start = now;
    }
    ++frames;
    render_total += render_ms;

    if (now - start >= 5000) {
        fprintf(stderr,
                "fps: %.1f over %ums (game logic %.1f), copy to screen %.1fms/frame, mouse button events %u\n",
                frames * 1000.0 / (now - start),
                now - start,
                (Logic_Frame_Count - last_logic) * 1000.0 / (now - start),
                double(render_total) / frames,
                SDL1_Mouse_Button_Events - last_buttons);
#ifdef SDL1AUDIO_BUILD
        MixerStats audio;
        static MixerStats last_audio;
        Mixer_Get_Stats(audio);
        fprintf(stderr,
                "audio: %u callbacks, %.0f frames/s mixed (%d Hz), longest gap %ums, %u dry, peak level %d, up to %d "
                "channels\n",
                audio.Callbacks - last_audio.Callbacks,
                (audio.FramesMixed - last_audio.FramesMixed) * 1000.0 / (now - start),
                audio.OutputRate,
                audio.MaxGapMs,
                audio.DryCount - last_audio.DryCount,
                audio.Peak,
                audio.MaxChannels);
        if (audio.OutputQueued != 0) {
            fprintf(stderr,
                    "audio: SharedSoundBuffer queue up to %u bytes, found empty %u times\n",
                    audio.OutputQueued,
                    audio.OutputDry);
        }
        for (int i = 0; i < audio.StallCount; ++i) {
            fprintf(stderr,
                    "stall: audio thread didn't run for %u ms at %u.%03u s\n",
                    audio.Stalls[i].GapMs,
                    audio.Stalls[i].AtMs / 1000,
                    audio.Stalls[i].AtMs % 1000);
        }
        last_audio = audio;
#endif
        Phase_Report(now - start);
        last_buttons = SDL1_Mouse_Button_Events;
        last_logic = Logic_Frame_Count;
        start = now;
        frames = 0;
        render_total = 0;
    }
}

void Video_Render_Frame()
{
    if (frontSurface) {
        Uint32 before = SDL_GetTicks();
        frontSurface->RenderSurface();
        Log_Frame_Rate(SDL_GetTicks() - before);
    }
}

/*
** Video
*/

Video::Video()
{
}

Video::~Video()
{
}

Video& Video::Shared()
{
    static Video video;
    return video;
}

VideoSurface* Video::CreateSurface(int w, int h, GBC_Enum flags)
{
    return new VideoSurfaceSDL1(w, h, flags);
}
