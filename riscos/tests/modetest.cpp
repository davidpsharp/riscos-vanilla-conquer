// Screen mode and palette check: sets the game's 640x400x8 mode the way
// video_sdl1.cpp does, logs what RISC OS actually selected, then shows a
// colour chart for 20 seconds (or until a key is pressed). Top to bottom the
// chart should be: red ramp, green ramp, blue ramp, grey ramp, each dark on
// the left to bright on the right. Log goes to modetest.log in the current dir.
// Usage: modetest [fullscreen: 1 (default) or 0]
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#endif

static std::FILE* Log;

#ifdef __riscos__
static int Mode_Var(int var)
{
    int value = -1;
    if (_swix(OS_ReadModeVariable, _INR(0, 1) | _OUT(2), -1, var, &value) != nullptr) {
        return -1;
    }
    return value;
}

static void Log_Mode()
{
    static const struct
    {
        int var;
        const char* name;
    } vars[] = {{0, "ModeFlags"},
                {3, "NColour"},
                {4, "XEigFactor"},
                {5, "YEigFactor"},
                {6, "LineLength"},
                {7, "ScreenSize"},
                {9, "Log2BPP"},
                {11, "XWindLimit"},
                {12, "YWindLimit"}};
    for (const auto& v : vars) {
        std::fprintf(Log, "  %-10s %d\n", v.name, Mode_Var(v.var));
    }
    int mode = -1;
    _swix(OS_Byte, _IN(0) | _OUT(2), 135, &mode);
    std::fprintf(Log, "  mode number/selector %#x\n", unsigned(mode));
    int flags = Mode_Var(0);
    if (Mode_Var(9) == 3) {
        std::fprintf(Log,
                     "  full palette (ModeFlags bit 7): %s\n",
                     flags >= 0 && (flags & 0x80) ? "yes" : "NO - 256 colours will be wrong");
    }
}
#endif

int main(int argc, char** argv)
{
    bool fullscreen = argc < 2 || std::atoi(argv[1]) != 0;
    Log = std::fopen("modetest.log", "w");
    if (Log == nullptr) {
        Log = stderr;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(Log, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#ifdef __riscos__
    std::fprintf(Log, "desktop mode:\n");
    Log_Mode();
#endif

    int flags = SDL_HWSURFACE | SDL_HWPALETTE | (fullscreen ? SDL_FULLSCREEN : 0);
    SDL_Surface* s = SDL_SetVideoMode(640, 400, 8, flags);
    if (s == nullptr) {
        std::fprintf(Log, "SDL_SetVideoMode(640, 400, 8) failed: %s\n", SDL_GetError());
        std::fclose(Log);
        SDL_Quit();
        return 1;
    }

    char driver[64] = "?";
    SDL_VideoDriverName(driver, sizeof(driver));
    std::fprintf(Log,
                 "SDL driver %s surface %dx%d pitch %d bpp %d flags %08x\n",
                 driver,
                 s->w,
                 s->h,
                 s->pitch,
                 s->format->BitsPerPixel,
                 unsigned(s->flags));
#ifdef __riscos__
    std::fprintf(Log, "game mode:\n");
    Log_Mode();
#endif
    std::fflush(Log);

    // Rows of 64 entries: red, green, blue and grey ramps.
    SDL_Color pal[256];
    for (int i = 0; i < 256; ++i) {
        Uint8 level = Uint8((i % 64) * 255 / 63);
        pal[i].r = (i < 64 || i >= 192) ? level : 0;
        pal[i].g = ((i >= 64 && i < 128) || i >= 192) ? level : 0;
        pal[i].b = (i >= 128) ? level : 0;
        pal[i].unused = 0;
    }
    SDL_SetPalette(s, SDL_LOGPAL, pal, 0, 256);
    SDL_SetPalette(s, SDL_PHYSPAL, pal, 0, 256);

    // 4 bands of 64 columns, each column 10 pixels wide.
    SDL_LockSurface(s);
    for (int y = 0; y < s->h; ++y) {
        Uint8* row = static_cast<Uint8*>(s->pixels) + y * s->pitch;
        int band = y * 4 / s->h;
        for (int x = 0; x < s->w; ++x) {
            row[x] = Uint8(band * 64 + x * 64 / s->w);
        }
    }
    SDL_UnlockSurface(s);
    SDL_Flip(s);

    Uint32 end = SDL_GetTicks() + 20000;
    bool quit = false;
    while (!quit && SDL_GetTicks() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_QUIT) {
                quit = true;
            }
        }
        SDL_Delay(50);
    }

    std::fprintf(Log, "done\n");
    std::fclose(Log);
    SDL_Quit();
    return 0;
}
