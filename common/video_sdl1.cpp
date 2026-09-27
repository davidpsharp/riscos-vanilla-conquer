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

#include "framelimit.h"
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

void Set_Video_Cursor(void* cursor, int w, int h, int hotx, int hoty)
{
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
        if (frontSurface == this) {
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
        SDL_BlitSurface(((VideoSurfaceSDL1*)src)->surface, &srcRectSDL, surface, &destRectSDL);
    }

    virtual void FillRect(const Rect& rect, unsigned char color)
    {
        SDL_Rect rectSDL = Make_SDL_Rect(rect.X, rect.Y, rect.Width + 1, rect.Height + 1);
        SDL_FillRect(surface, &rectSDL, color);
    }

    void RenderSurface()
    {
        /*
        ** Draw the software cursor into the frame before copying it to the screen, then
        ** put back what was under it. Drawing it on the screen after the copy leaves it
        ** missing for part of every frame, which flickers badly on real hardware.
        */
        SDL_Rect area = {0, 0, 0, 0};
        bool cursor = !Get_Mouse_State() && hwcursor.Surface != nullptr;

        if (cursor) {
            int x, y;
            Get_Video_Mouse(x, y);
            SDL_Rect dst =
                Make_SDL_Rect(x - hwcursor.HotX, y - hwcursor.HotY, hwcursor.Surface->w, hwcursor.Surface->h);
            area = dst;
            cursor = Save_Area(area);
            if (cursor) {
                SDL_BlitSurface(hwcursor.Surface, nullptr, surface, &dst);
            }
        }

        SDL_BlitSurface(surface, NULL, window, NULL);

        if (cursor) {
            Restore_Area(area);
        }

        SDL_Flip(window);
    }

private:
    /*
    ** Copies the part of "area" inside the surface to a side buffer, clipping "area" to
    ** match. Raw copies, as SDL would remap colours between 8 bit palettes.
    */
    bool Save_Area(SDL_Rect& area)
    {
        int x0 = area.x < 0 ? 0 : area.x;
        int y0 = area.y < 0 ? 0 : area.y;
        int x1 = area.x + area.w > surface->w ? surface->w : area.x + area.w;
        int y1 = area.y + area.h > surface->h ? surface->h : area.y + area.h;

        if (x1 <= x0 || y1 <= y0) {
            return false;
        }
        area = Make_SDL_Rect(x0, y0, x1 - x0, y1 - y0);
        under.resize(size_t(area.w) * area.h);

        SDL_LockSurface(surface);
        for (int row = 0; row < area.h; ++row) {
            memcpy(&under[size_t(row) * area.w],
                   static_cast<Uint8*>(surface->pixels) + (area.y + row) * surface->pitch + area.x,
                   area.w);
        }
        SDL_UnlockSurface(surface);
        return true;
    }

    void Restore_Area(const SDL_Rect& area)
    {
        SDL_LockSurface(surface);
        for (int row = 0; row < area.h; ++row) {
            memcpy(static_cast<Uint8*>(surface->pixels) + (area.y + row) * surface->pitch + area.x,
                   &under[size_t(row) * area.w],
                   area.w);
        }
        SDL_UnlockSurface(surface);
    }

    SDL_Surface* surface;
    GBC_Enum flags;
    std::vector<Uint8> under; // What the software cursor covers while it is drawn.
};

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
