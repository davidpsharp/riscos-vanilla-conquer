// Palette switch test: fills the screen with colour index 1, then changes
// palette entry 1 every 3 seconds (red, green, blue) via SDL_SetPalette with
// SDL_PHYSPAL, the way the game does. Watch whether the display follows.
// Usage: paltest [mode]  mode 0 = PHYSPAL only (game), 1 = PHYSPAL|LOGPAL, 2 = SetColors
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    int mode = argc > 1 ? std::atoi(argv[1]) : 0;
    std::FILE* log = std::fopen("paltest.log", "w");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Surface* s = SDL_SetVideoMode(640, 400, 8, SDL_HWSURFACE | SDL_HWPALETTE | SDL_FULLSCREEN);
    if (!s) {
        std::fprintf(log, "no mode: %s\n", SDL_GetError());
        return 1;
    }
    std::fprintf(log, "flags %08x\n", unsigned(s->flags));
    SDL_Color base[256];
    std::memset(base, 0, sizeof(base));
    SDL_SetPalette(s, SDL_LOGPAL, base, 0, 256);

    SDL_LockSurface(s);
    for (int y = 0; y < s->h; ++y) {
        std::memset(static_cast<Uint8*>(s->pixels) + y * s->pitch, y < s->h / 2 ? 1 : 2, s->w);
    }
    SDL_UnlockSurface(s);
    SDL_Flip(s);

    static const SDL_Color seq[3] = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}};
    for (int i = 0; i < 3; ++i) {
        SDL_Color pal[256];
        std::memset(pal, 0, sizeof(pal));
        pal[1] = seq[i];
        pal[2] = seq[(i + 1) % 3];
        int ok;
        if (mode == 0) {
            ok = SDL_SetPalette(s, SDL_PHYSPAL, pal, 0, 256);
        } else if (mode == 1) {
            ok = SDL_SetPalette(s, SDL_PHYSPAL | SDL_LOGPAL, pal, 0, 256);
        } else {
            ok = SDL_SetColors(s, pal, 0, 256);
        }
        std::fprintf(log, "step %d set ok=%d at %u\n", i, ok, unsigned(SDL_GetTicks()));
        std::fflush(log);
        SDL_Delay(3000);
    }
    SDL_Quit();
    std::fclose(log);
    return 0;
}
