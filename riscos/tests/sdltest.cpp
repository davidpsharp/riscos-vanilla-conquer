// SDL 1.2 smoke test: opens an 8bpp palettised screen (the mode Vanilla Conquer
// uses), draws a palette gradient and logs input events to sdltest/log.
// Usage: sdltest [seconds] [w] [h]
#include <SDL.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    int seconds = argc > 1 ? std::atoi(argv[1]) : 10;
    int w = argc > 2 ? std::atoi(argv[2]) : 640;
    int h = argc > 3 ? std::atoi(argv[3]) : 480;

    std::FILE* log = std::fopen("sdltest.log", "w");
    if (!log) {
        log = stderr;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(log, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Surface* s = SDL_SetVideoMode(w, h, 8, SDL_SWSURFACE | SDL_HWPALETTE | SDL_FULLSCREEN);
    if (!s) {
        std::fprintf(log, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    std::fprintf(log, "mode %dx%dx%d pitch=%d\n", s->w, s->h, s->format->BitsPerPixel, s->pitch);

    SDL_Color pal[256];
    for (int i = 0; i < 256; ++i) {
        pal[i].r = (i & 0xE0);
        pal[i].g = (i & 0x1C) << 3;
        pal[i].b = (i & 0x03) << 6;
    }
    SDL_SetColors(s, pal, 0, 256);

    Uint32 start = SDL_GetTicks();
    int frames = 0;
    bool quit = false;
    while (!quit && SDL_GetTicks() - start < Uint32(seconds * 1000)) {
        SDL_LockSurface(s);
        for (int y = 0; y < s->h; ++y) {
            Uint8* row = static_cast<Uint8*>(s->pixels) + y * s->pitch;
            for (int x = 0; x < s->w; ++x) {
                row[x] = Uint8((x * 256 / s->w + frames) ^ (y * 8 / s->h));
            }
        }
        SDL_UnlockSurface(s);
        SDL_Flip(s);
        ++frames;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYDOWN) {
                std::fprintf(log, "key %d\n", e.key.keysym.sym);
                quit = e.key.keysym.sym == SDLK_ESCAPE;
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                std::fprintf(log, "click %d at %d,%d\n", e.button.button, e.button.x, e.button.y);
            } else if (e.type == SDL_QUIT) {
                quit = true;
            }
        }
    }
    Uint32 ms = SDL_GetTicks() - start;
    std::fprintf(log, "frames=%d ms=%u fps=%.1f\n", frames, ms, ms ? frames * 1000.0 / ms : 0.0);
    SDL_Quit();
    if (log != stderr) {
        std::fclose(log);
    }
    return 0;
}
