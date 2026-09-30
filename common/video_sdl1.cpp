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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern WWKeyboardClass* Keyboard;
static SDL_Surface* window;
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
    SDL_Init(SDL_INIT_VIDEO);
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

void Get_Video_Mouse(int& x, int& y)
{
    if (Keyboard->Is_Gamepad_Active() || (Settings.Mouse.RawInput && (hwcursor.Clip || !Settings.Video.Windowed))) {
        x = hwcursor.X;
        y = hwcursor.Y;
    } else {
        SDL_GetMouseState(&x, &y);
    }
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

// Copies the overlapping part of two 8 bit surfaces byte for byte (SDL would remap
// colours between 8 bit palettes).
static void Raw_Copy(SDL_Surface* from, SDL_Surface* to)
{
    int w = from->w < to->w ? from->w : to->w;
    int h = from->h < to->h ? from->h : to->h;
    SDL_LockSurface(from);
    SDL_LockSurface(to);
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

        if (flags & GBC_VISIBLE) {
            frontSurface = this;
        }
    }

    virtual ~VideoSurfaceSDL1()
    {
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
        return (SDL_LockSurface(surface) == 0);
    }

    virtual bool Unlock()
    {
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
        SDL_BlitSurface(((VideoSurfaceSDL1*)src)->surface, &srcRectSDL, surface, &destRectSDL);
    }

    virtual void FillRect(const Rect& rect, unsigned char color)
    {
        SDL_Rect rectSDL = Make_SDL_Rect(rect.X, rect.Y, rect.Width + 1, rect.Height + 1);
        Touch();
        changed = true;
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
        if (always_full || changed || !presented || (mirror != nullptr && !mirror_shown)) {
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
        if (from == nullptr) {
            return;
        }
        if (!mirror_shown) {
            Raw_Copy(from->surface, surface);
        } else if (window != nullptr) {
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
            Get_Video_Mouse(x, y);
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
        bool full = changed || always_full || !presented;
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
                Raw_Copy(frame, window);
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
        }

        if (full) {
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
