// Times SDL audio initialisation on the target, step by step.
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <time.h>

static void SDLCALL Fill(void*, Uint8* stream, int len)
{
    std::memset(stream, 0, len);
}

static double Now()
{
    return clock() / (double)CLOCKS_PER_SEC;
}

int main()
{
    double t0 = Now();
    SDL_Init(SDL_INIT_TIMER);
    std::printf("SDL_Init(TIMER)          %.2fs\n", Now() - t0);
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    std::printf("SDL_InitSubSystem(AUDIO) %.2fs\n", Now() - t0);
    SDL_AudioSpec want;
    std::memset(&want, 0, sizeof(want));
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = Fill;
    int rc = SDL_OpenAudio(&want, nullptr);
    std::printf("SDL_OpenAudio -> %d      %.2fs\n", rc, Now() - t0);
    SDL_PauseAudio(0);
    std::printf("SDL_PauseAudio(0)        %.2fs\n", Now() - t0);
    SDL_Delay(500);
    SDL_CloseAudio();
    std::printf("SDL_CloseAudio           %.2fs\n", Now() - t0);
    SDL_Quit();
    return 0;
}
