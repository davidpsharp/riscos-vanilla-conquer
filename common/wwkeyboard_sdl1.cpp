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

#include "phasetime.h"
#include "macros.h"
#include "wwkeyboard_sdl1.h"
#include "video.h"
#include "settings.h"
#include <SDL.h>
#ifdef __riscos__
#include <kernel.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Focus_Loss();
void Focus_Restore();
void Process_Network();

/*
** Converts an SDL 1.2 key symbol to this backend's key code (see SDL1_VK in
** wwkeyboard.h), or 0 for keys the game has no use for.
*/
static unsigned short SDL1_Key(SDLKey sym)
{
    if (sym >= SDLK_WORLD_0 && sym <= SDLK_WORLD_95) {
        return 0; // Their codes are reused for keys above 255.
    }
    return (unsigned short)SDL1_VK(sym);
}

WWKeyboardClassSDL1::~WWKeyboardClassSDL1()
{
}

unsigned SDL1_Mouse_Button_Events = 0;

/*
** With VC_FPSLOG set, print every mouse button event: the position SDL gave it,
** the position passed to the game, SDL's current mouse state (where the cursor
** is drawn) and, on RISC OS, where the OS pointer really is.
*/
#ifdef __riscos__
bool RISCOS_Pointer_Position(int& x, int& y); // video_sdl1.cpp
#endif

static void Log_Mouse_Button(const SDL_Event& event, int x, int y, bool stored)
{
    static const bool enabled = getenv("VC_FPSLOG") != nullptr;
    if (!enabled) {
        return;
    }
    int sx, sy;
    SDL_GetMouseState(&sx, &sy);
    fprintf(stderr,
            "mouse: %s button %d at %d,%d -> game %d,%d; cursor at %d,%d",
            event.type == SDL_MOUSEBUTTONDOWN ? "down" : "up",
            event.button.button,
            event.button.x,
            event.button.y,
            x,
            y,
            sx,
            sy);
#ifdef __riscos__
    // OS_Word 21,4 reads the unbuffered pointer position, in OS units from the bottom left.
    unsigned char block[5] = {4, 0, 0, 0, 0};
    _kernel_osword(21, (int*)block);
    int os_x = Sint16(block[1] | (block[2] << 8));
    int os_y = Sint16(block[3] | (block[4] << 8));
    fprintf(stderr, "; OS pointer %d,%d (OS units)", os_x, os_y);
#endif
    fprintf(stderr, "%s\n", stored ? "" : " - DROPPED, keyboard buffer full");
}

// Also with VC_FPSLOG: print key events, to spot keys the OS reports as stuck down.
static void Log_Key(const SDL_Event& event)
{
    static const bool enabled = getenv("VC_FPSLOG") != nullptr;
    if (enabled) {
        fprintf(stderr,
                "key: %s sym %d (%s)\n",
                event.type == SDL_KEYDOWN ? "down" : "up",
                int(event.key.keysym.sym),
                SDL_GetKeyName(event.key.keysym.sym));
    }
}

void WWKeyboardClassSDL1::Fill_Buffer_From_System(void)
{
#ifdef NETWORKING
    Process_Network();
#endif
    SDL_Event event;

    /*
    ** Pump once, then drain only the events already queued. SDL_PollEvent pumps on
    ** every call, and some SDL 1.2 ports (e.g. RISC OS) queue a fresh mouse motion
    ** event on each pump, which would keep this loop from ever finishing.
    */
    SDL_PumpEvents();
    while (!Is_Buffer_Full() && SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_ALLEVENTS) > 0) {
        unsigned short key;
        switch (event.type) {
        case SDL_QUIT:
            exit(0);
            break;
        case SDL_KEYDOWN:
            Log_Key(event);
            if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                /* Switching to full screen is handled in the key up event */
            } else if (unsigned short key = SDL1_Key(event.key.keysym.sym)) {
                Put_Key_Message(key, false);
            }
            break;
        case SDL_KEYUP:
            Log_Key(event);
            if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                Toggle_Video_Fullscreen();
            } else if (unsigned short key = SDL1_Key(event.key.keysym.sym)) {
                Put_Key_Message(key, true);
            }
            break;
        case SDL_MOUSEMOTION:
            ++Phase_Counts[COUNT_MOTION_BY_GAME];
            Move_Video_Mouse(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int x, y;
            ++SDL1_Mouse_Button_Events;

            switch (event.button.button) {
            case SDL_BUTTON_LEFT:
            default:
                key = VK_LBUTTON;
                break;
            case SDL_BUTTON_RIGHT:
                key = VK_RBUTTON;
                break;
            case SDL_BUTTON_MIDDLE:
                key = VK_MBUTTON;
                break;
            case SDL_BUTTON_WHEELUP:
                key = VK_MOUSEWHEEL_UP;
                break;
            case SDL_BUTTON_WHEELDOWN:
                key = VK_MOUSEWHEEL_DOWN;
                break;
            }

#ifdef __riscos__
            if (!Is_Gamepad_Active()) {
                // From the OS pointer, as SDL's event position can be off (see video_sdl1.cpp),
                // and relative to the menu area, as the game's other mouse positions are.
                Get_Video_Mouse(x, y);
            } else
#endif
                if (Settings.Mouse.RawInput || Is_Gamepad_Active()) {
                Get_Video_Mouse(x, y);
            } else {
                float scale_x = 1.0f, scale_y = 1.0f;
                Get_Video_Scale(scale_x, scale_y);
                x = event.button.x / scale_x;
                y = event.button.y / scale_y;
            }

            bool stored = Put_Mouse_Message(key, x, y, event.type == SDL_MOUSEBUTTONDOWN ? false : true);
            Log_Mouse_Button(event, x, y, stored);
        } break;

        case SDL_ACTIVEEVENT:
#ifdef __APPLE__
            // On the Mac it keeps running (and playing) behind other windows, so it can be
            // watched while using something else; minimising it still pauses it.
            if (event.active.state & SDL_APPACTIVE) {
#else
            if (event.active.state & SDL_APPINPUTFOCUS) {
#endif
                if (event.active.gain) {
                    Focus_Restore();
                } else {
                    Focus_Loss();
                }
            }
            break;
        }
    }
}

KeyASCIIType WWKeyboardClassSDL1::To_ASCII(unsigned short key)
{
    /*
    ** SDL 1.2 key symbols for printable keys are their unshifted ASCII values
    ** (unlike the SDL2 scancodes sdl_keymap.h is indexed by), as are Return,
    ** Escape, Backspace and Tab. Apply a US layout shift map on top.
    */
    static const char shift_from[] = "`1234567890-=[]\\;',./";
    static const char shift_to[] = "~!@#$%^&*()_+{}|:\"<>?";

    if (key & WWKEY_RLS_BIT) {
        return KA_NONE;
    }

    bool shift = (key & WWKEY_SHIFT_BIT) || (SDL_GetModState() & KMOD_SHIFT);
    key &= 0xFF; // drop all mods

    switch (key) {
    case SDLK_RETURN:
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
    case SDLK_TAB:
        return KeyASCIIType(key);
    case SDL1_VK(SDLK_KP_ENTER):
        return KA_RETURN;
    case SDL1_VK(SDLK_KP_PERIOD):
        return KA_PERIOD;
    case SDL1_VK(SDLK_KP_DIVIDE):
        return KA_SLASH;
    case SDL1_VK(SDLK_KP_MULTIPLY):
        return KA_ASTERISK;
    case SDL1_VK(SDLK_KP_MINUS):
        return KA_MINUS;
    case SDL1_VK(SDLK_KP_PLUS):
        return KA_PLUS;
    default:
        break;
    }

    if (key >= SDL1_VK(SDLK_KP0) && key <= SDL1_VK(SDLK_KP9)) {
        return KeyASCIIType('0' + key - SDL1_VK(SDLK_KP0));
    }

    if (key < ' ' || key > '~') {
        return KA_NONE;
    }

    if (shift) {
        if (key >= 'a' && key <= 'z') {
            return KeyASCIIType(key - 'a' + 'A');
        }
        const char* pos = strchr(shift_from, key);
        if (pos != nullptr) {
            return KeyASCIIType(shift_to[pos - shift_from]);
        }
    }

    return KeyASCIIType(key);
}

WWKeyboardClass* CreateWWKeyboardClass(void)
{
    return new WWKeyboardClassSDL1;
}
