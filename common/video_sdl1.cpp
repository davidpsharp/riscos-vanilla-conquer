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
#include "wwkeyboard.h"
#include "wwmouse.h"
#include "settings.h"
#include "debugstring.h"

#include <SDL.h>
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
    if (window == nullptr || window->offset == 0 || window->pitch <= 0) {
        return false;
    }
    offx = window->offset % window->pitch;
    offy = window->offset / window->pitch;
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
    x = (regs.r[0] >> Mode_Variable(4)) - offx;                     // XEigFactor
    y = Mode_Variable(12) - (regs.r[1] >> Mode_Variable(5)) - offy; // YWindLimit, YEigFactor
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
    int x0 = offx + (area ? MouseArea.X : 0), y0 = offy + (area ? MouseArea.Y : 0);
    int w = area ? MouseArea.W : window->w, h = area ? MouseArea.H : window->h;
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

    Video_Settle_Front();
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
                window->pitch ? window->offset % window->pitch : 0,
                window->pitch ? window->offset / window->pitch : 0);
    }
#endif

    SDL_SetPalette(window, SDL_LOGPAL, logpal, 0, 256);
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

    SDL_SetPalette(window, SDL_PHYSPAL, physpal, 0, 256);

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
** Copies "bytes" (a multiple of 32) between word-aligned buffers, 32 bytes per
** LDM/STM pair. ARM (ARMv3 and later) only; r9-r11 are left alone for the APCS.
*/
static void Copy_32(void* dst, const void* src, unsigned bytes)
{
    register unsigned r0 asm("r0") = reinterpret_cast<unsigned>(dst);
    register unsigned r1 asm("r1") = reinterpret_cast<unsigned>(src);
    register unsigned r2 asm("r2") = bytes;
    asm volatile("1:\n"
                 "ldmia r1!, {r3, r4, r5, r6, r7, r8, r12, lr}\n"
                 "stmia r0!, {r3, r4, r5, r6, r7, r8, r12, lr}\n"
                 "subs r2, r2, #32\n"
                 "bgt 1b\n"
                 : "+r"(r0), "+r"(r1), "+r"(r2)
                 :
                 : "r3", "r4", "r5", "r6", "r7", "r8", "r12", "lr", "cc", "memory");
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

static void Raw_Copy(SDL_Surface* from, SDL_Surface* to)
{
    int w = from->w < to->w ? from->w : to->w;
    int h = from->h < to->h ? from->h : to->h;
    SDL_LockSurface(from);
    SDL_LockSurface(to);
#if defined(__riscos__) && defined(__arm__)
    // The LDM/STM loop copied a frame to the screen in 10.9 ms on a StrongARM Risc PC,
    // against 15.2 ms with memcpy. VC_PRESENTMEMCPY switches back, for comparison.
    static const bool use_asm = getenv("VC_PRESENTMEMCPY") == nullptr;
    if (use_asm && (w & 31) == 0 && ((reinterpret_cast<uintptr_t>(to->pixels) | reinterpret_cast<uintptr_t>(from->pixels)
                                      | unsigned(to->pitch) | unsigned(from->pitch))
                                     & 3)
                                        == 0) {
        for (int row = 0; row < h; ++row) {
            Copy_32(static_cast<Uint8*>(to->pixels) + row * to->pitch,
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
        static const bool partial = getenv("VC_PARTIALPRESENT") != nullptr;
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
            // The frame is on the screen, with the cursor drawn over it.
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
        if (Shake_Count > 0) {
            Present_Shaken();
            return;
        }
        bool force_full = always_full || !presented || Shake_Undo; // copy all of it, not just what changed
        bool full = changed || force_full;
        Shake_Undo = false;
        if (!full && cursor == last_cursor && Cursor_Generation == last_generation
            && (!cursor || (area.x == last_area.x && area.y == last_area.y))) {
            ++Phase_Counts[COUNT_PRESENT_SKIPPED];
            return;
        }
        ++Phase_Counts[full ? COUNT_PRESENT_FULL : COUNT_PRESENT_PARTIAL];

        // The frame to show: the hidden surface if this one mirrors it.
        SDL_Surface* frame = mirror != nullptr ? mirror->surface : surface;
        SDL_Rect drawn = area;
        SDL_Rect dirty = {0, 0, 0, 0};

        if (!full && mirror != nullptr && mirror_shown) {
            /*
            ** The frame is only on the screen now (the hidden surface may already hold
            ** part of the next one), so move the cursor on the screen itself: put back
            ** what was under it, then save what's under the new place and draw it there.
            */
            if (last_cursor) {
                Put_Under(window, last_drawn);
            }
            if (cursor) {
                cursor = Save_Area(window, drawn);
                if (cursor) {
                    Draw_Cursor(window, area);
                }
            }
            dirty = Union(last_cursor ? last_drawn : dirty, cursor ? drawn : dirty);
        } else {
            /*
            ** Draw the software cursor into the frame before copying it to the screen,
            ** then put back what was under it. Drawing it on the screen after the copy
            ** leaves it missing for part of every frame, which flickers badly on real
            ** hardware.
            */
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
                if (!Copy_Changed(frame, drawn, cursor, force_full)) {
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

            if (cursor) {
                Put_Under(frame, drawn);
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
            SDL_UpdateRects(window, int(spans.size()), &spans[0]);
        } else if (full) {
            SDL_Flip(window);
        } else if (dirty.w > 0 && dirty.h > 0) {
            SDL_UpdateRects(window, 1, &dirty);
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
        SDL_Flip(window);
        if (mirror != nullptr && mirror_shown) {
            // The frame was only on the screen, which is now shifted: keep a copy first.
            Raw_Copy(mirror->surface, surface);
            mirror = nullptr;
        }
        Shake_Undo = true;
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
    ** For now it's only on with VC_PARTIALPRESENT=1, until it has been played with
    ** more widely. In the battle benchmark on the StrongARM Risc PC (same image, runs
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
                for (int row = 0; row < span.h; ++row) {
                    Uint8* to = static_cast<Uint8*>(window->pixels) + (span.y + row) * window->pitch + span.x;
                    const Uint8* from = static_cast<const Uint8*>(frame->pixels) + (span.y + row) * frame->pitch + span.x;
#if defined(__riscos__) && defined(__arm__)
                    if ((span.w & 31) == 0 && ((reinterpret_cast<uintptr_t>(to) | reinterpret_cast<uintptr_t>(from)) & 3) == 0) {
                        Copy_32(to, from, unsigned(span.w));
                        continue;
                    }
#endif
                    memcpy(to, from, span.w);
                }
                spans.push_back(span);
                Dirty_Tiles_Copied += tx - start;
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
