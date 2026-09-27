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

#include "macros.h"
#include "wwkeyboard_sdl1.h"
#include "video.h"
#include "settings.h"
#include <SDL.h>
#include <string.h>

void Focus_Loss();
void Focus_Restore();
void Process_Network();

WWKeyboardClassSDL1::~WWKeyboardClassSDL1()
{
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
            if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                /* Switching to full screen is handled in the key up event */
            } else {
                Put_Key_Message(event.key.keysym.sym, false);
            }
            break;
        case SDL_KEYUP:
            if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                Toggle_Video_Fullscreen();
            } else {
                Put_Key_Message(event.key.keysym.sym, true);
            }
            break;
        case SDL_MOUSEMOTION:
            Move_Video_Mouse(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int x, y;

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

            if (Settings.Mouse.RawInput || Is_Gamepad_Active()) {
                Get_Video_Mouse(x, y);
            } else {
                float scale_x = 1.0f, scale_y = 1.0f;
                Get_Video_Scale(scale_x, scale_y);
                x = event.button.x / scale_x;
                y = event.button.y / scale_y;
            }

            Put_Mouse_Message(key, x, y, event.type == SDL_MOUSEBUTTONDOWN ? false : true);
        } break;

        case SDL_ACTIVEEVENT:
            if (event.active.state & SDL_APPINPUTFOCUS) {
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
    default:
        break;
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
