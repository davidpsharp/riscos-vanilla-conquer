/*
** mixtest: runs the sound mixer (common/mixer_sdl1.cpp, included here, with SDL's audio and
** SharedSoundBuffer stubbed out) over fixed inputs and hashes what it produces: the output
** samples, the peak level and each channel's position after every callback. The mixed sound
** must be the same, bit for bit, whatever the mixer's code does to go faster, so the hashes
** are compared with those of the mixer before the speed-ups (commit c764394), listed below.
**
** The cases cover what the game feeds it: 16 bit mono at the output rate (effects and speech),
** 8 bit, stereo and other rates (resampled), every gain from silent to full, several loud
** channels at once (the soft limiter), queues running dry and refilled, swapped channels, odd
** block sizes and output buffers at every alignment (two bytes off, as SDL might hand one).
**
**   mixtest            prints a hash per case and PASS or FAIL; exit status 0 on PASS
**   mixtest -print     prints the hashes only (to make the expected list from another build)
**
** Build with -DMIXER_SOURCE='"path/to/mixer_sdl1.cpp"' to test another copy of the mixer.
*/
#ifndef MIXER_SOURCE
#define MIXER_SOURCE "../../common/mixer_sdl1.cpp"
#endif
#include MIXER_SOURCE
#undef main // SDL may rename it

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Stand-ins for what the mixer calls outside itself.
bool Phase_Timing = false;
unsigned Phase_Now_Us()
{
    return 0;
}
void Phase_Add(PhaseId, unsigned)
{
}
static Uint32 Ticks = 0;
extern "C" {
Uint32 SDL_GetTicks(void)
{
    return Ticks += 23;
}
void SDL_LockAudio(void)
{
}
void SDL_UnlockAudio(void)
{
}
int SDL_mutexP(SDL_mutex*)
{
    return 0;
}
int SDL_mutexV(SDL_mutex*)
{
    return 0;
}
SDL_mutex* SDL_CreateMutex(void)
{
    return nullptr;
}
void SDL_DestroyMutex(SDL_mutex*)
{
}
int SDL_InitSubSystem(Uint32)
{
    return -1;
}
void SDL_QuitSubSystem(Uint32)
{
}
int SDL_OpenAudio(SDL_AudioSpec*, SDL_AudioSpec*)
{
    return -1;
}
void SDL_PauseAudio(int)
{
}
void SDL_CloseAudio(void)
{
}
SDL_audiostatus SDL_GetAudioStatus(void)
{
    return SDL_AUDIO_STOPPED;
}
char* SDL_AudioDriverName(char* buf, int)
{
    return buf;
}
}
#ifdef __riscos__
bool SSB_Available(void)
{
    return false;
}
bool SSB_Open(int, int, int, SSBFillFunc)
{
    return false;
}
void SSB_Close(void)
{
}
void SSB_Pause(bool)
{
}
bool SSB_Is_Paused(void)
{
    return false;
}
void SSB_Get_Stats(unsigned& dry, unsigned& max_queued)
{
    dry = max_queued = 0;
}
#endif

static unsigned Seed;
static unsigned Rand()
{
    Seed = Seed * 1103515245u + 12345u;
    return Seed >> 8;
}

static unsigned Hash;
static void Hash_Bytes(const void* p, size_t n)
{
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) {
        Hash = (Hash ^ b[i]) * 16777619u; // FNV-1a
    }
}
static void Hash_Int(unsigned v)
{
    Hash_Bytes(&v, sizeof(v));
}

/*
** Sample data: "kind" 0 is random at the given amplitude, 1 a sawtooth, 2 a square wave of
** +-amplitude, 3 full scale square waves (for the limiter), 4 a constant -amplitude, 5 a constant
** +amplitude.
*/
static void Make_Data(unsigned char* buf, size_t len, int bits, int kind, int amp)
{
    if (bits == 8) {
        for (size_t i = 0; i < len; ++i) {
            int v;
            switch (kind) {
            case 0:
                v = int(Rand() % unsigned(2 * amp + 1)) - amp;
                break;
            case 1:
                v = int((i * 3) % 256) - 128;
                break;
            case 2:
                v = (i / 37) & 1 ? amp : -amp;
                break;
            case 4:
                v = -amp;
                break;
            case 5:
                v = amp;
                break;
            default:
                v = (i / 11) & 1 ? 127 : -128;
                break;
            }
            v = v < -128 ? -128 : (v > 127 ? 127 : v);
            buf[i] = (unsigned char)(v + 128);
        }
        return;
    }
    for (size_t i = 0; i + 1 < len; i += 2) {
        int v;
        switch (kind) {
        case 0:
            v = int(Rand() % unsigned(2 * amp + 1)) - amp;
            break;
        case 1:
            v = int((i * 97) % 65536) - 32768;
            break;
        case 2:
            v = (i / 74) & 1 ? amp : -amp;
            break;
        case 4:
            v = -amp;
            break;
        case 5:
            v = amp;
            break;
        default:
            v = (i / 22) & 1 ? 32767 : -32768;
            break;
        }
        v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        buf[i] = (unsigned char)(v & 0xFF);
        buf[i + 1] = (unsigned char)((v >> 8) & 0xFF);
    }
    if (len & 1) {
        buf[len - 1] = (unsigned char)Rand();
    }
}

struct Source
{
    int Bits, Chans, Rate;
    unsigned Gain;
    int Kind, Amp;
    int Buffers;
    size_t BlockLen; // bytes per queued block
    int Blocks;      // how many to queue over the case (0: keep it fed)
};

struct Case
{
    const char* Name;
    int Frames;     // per callback
    int Callbacks;
    int Misalign;   // bytes the output buffer is off a word
    bool Reverse;
    int Count;
    Source Sources[8];
};

static const Case Cases[] = {
    {"mono16-full", 1024, 40, 0, false, 1, {{16, 1, 22050, 0x10000, 0, 8000, 2, 4096, 0}}},
    {"mono16-half", 1024, 40, 0, false, 1, {{16, 1, 22050, 0x8000, 0, 30000, 2, 4096, 0}}},
    {"mono16-odd-gain", 512, 60, 0, false, 1, {{16, 1, 22050, 0x1234, 0, 32767, 3, 3000, 0}}},
    {"mono16-ffff", 512, 60, 0, false, 1, {{16, 1, 22050, 0xFFFF, 1, 0, 2, 2048, 0}}},
    {"mono16-silent-gain", 512, 20, 0, false, 1, {{16, 1, 22050, 0, 0, 32767, 2, 2048, 0}}},
    {"mono16-quiet-runs", 1024, 30, 0, false, 1, {{16, 1, 22050, 0x10000, 2, 300, 2, 4096, 0}}},
    {"mono16-odd-blocks", 333, 80, 0, false, 1, {{16, 1, 22050, 0x10000, 0, 20000, 4, 1001, 0}}},
    {"mono16-dry", 1024, 40, 0, false, 1, {{16, 1, 22050, 0x10000, 0, 20000, 2, 1500, 9}}},
    {"stereo16", 1024, 40, 0, false, 1, {{16, 2, 22050, 0xC000, 0, 32767, 2, 4096, 0}}},
    {"mono8", 1024, 40, 0, false, 1, {{8, 1, 22050, 0x10000, 0, 127, 2, 2048, 0}}},
    {"stereo8", 1024, 40, 0, false, 1, {{8, 2, 22050, 0x9000, 1, 0, 2, 2048, 0}}},
    {"mono16-11k", 1024, 40, 0, false, 1, {{16, 1, 11025, 0x10000, 0, 20000, 2, 2048, 0}}},
    {"mono16-44k", 1024, 40, 0, false, 1, {{16, 1, 44100, 0x10000, 0, 20000, 2, 8192, 0}}},
    {"mono8-22254", 1024, 40, 0, false, 1, {{8, 1, 22254, 0xE000, 0, 100, 3, 2222, 0}}},
    {"loud-four", 1024, 40, 0, false, 4,
     {{16, 1, 22050, 0x10000, 3, 0, 2, 4096, 0},
      {16, 1, 22050, 0x10000, 0, 32767, 2, 4096, 0},
      {16, 1, 22050, 0xF000, 1, 0, 2, 2048, 0},
      {16, 2, 22050, 0x10000, 3, 0, 2, 4096, 0}}},
    {"limit-edge", 512, 60, 0, false, 2,
     {{16, 1, 22050, 0x10000, 2, 12288, 2, 2048, 0}, {16, 1, 22050, 0x10000, 2, 12289, 2, 2048, 0}}},
    {"game-mix", 1024, 120, 0, false, 8,
     {{16, 1, 22050, 0x10000, 0, 9000, 2, 4096, 0},
      {16, 1, 22050, 0x7FFF, 0, 30000, 2, 3000, 25},
      {16, 1, 22050, 0x4000, 2, 20000, 2, 2048, 0},
      {8, 1, 22050, 0xC000, 0, 90, 2, 2048, 14},
      {16, 2, 22050, 0x10000, 0, 16000, 2, 4096, 0},
      {16, 1, 11025, 0xA000, 1, 0, 3, 1024, 0},
      {16, 1, 22050, 0x10000, 3, 0, 2, 777, 31},
      {8, 2, 44100, 0x3000, 0, 120, 2, 4096, 0}}},
    {"game-mix-reversed", 1024, 60, 0, true, 3,
     {{16, 1, 22050, 0x10000, 0, 30000, 2, 4096, 0},
      {16, 2, 22050, 0x10000, 1, 0, 2, 4096, 0},
      {8, 1, 22050, 0x10000, 3, 0, 2, 2048, 0}}},
    {"knee", 512, 20, 0, false, 1, {{16, 1, 22050, 0x10000, 2, 24576, 2, 2048, 0}}},
    {"knee-plus-1", 512, 20, 0, false, 1, {{16, 1, 22050, 0x10000, 2, 24577, 2, 2048, 0}}},
    {"knee-low", 512, 20, 0, false, 1, {{16, 1, 22050, 0x10000, 4, 24576, 2, 2048, 0}}},
    {"knee-low-minus-1", 512, 20, 0, false, 1, {{16, 1, 22050, 0x10000, 4, 24577, 2, 2048, 0}}},
    {"knee-high-only", 512, 20, 0, false, 1, {{16, 1, 22050, 0x10000, 5, 24577, 2, 2048, 0}}},
    {"reversed-quiet", 1024, 30, 0, true, 2,
     {{16, 2, 22050, 0x10000, 0, 9000, 2, 4096, 0}, {16, 1, 22050, 0x8000, 1, 0, 2, 2048, 0}}},
    {"misaligned-out", 1024, 40, 2, false, 2,
     {{16, 1, 22050, 0x10000, 0, 20000, 2, 4096, 0}, {16, 1, 22050, 0x10000, 3, 0, 2, 4096, 0}}},
    {"misaligned-quiet", 512, 40, 2, true, 1, {{16, 1, 22050, 0x10000, 0, 5000, 2, 4096, 0}}},
    {"tiny-callbacks", 1, 3000, 0, false, 2,
     {{16, 1, 22050, 0x10000, 0, 20000, 2, 512, 0}, {8, 1, 22050, 0x10000, 0, 127, 2, 300, 0}}},
    {"odd-callbacks", 7, 1500, 0, false, 2,
     {{16, 1, 22050, 0x10000, 0, 30000, 2, 600, 0}, {16, 1, 22050, 0x10000, 3, 0, 2, 600, 0}}},
};

static unsigned Run_Case(const Case& c)
{
    Seed = 0x5EED0000u ^ unsigned(c.Frames * 131 + c.Callbacks);
    Hash = 2166136261u;
    for (int i = 0; i < MAX_MIXER_CHANNELS; ++i) {
        Channels[i] = nullptr;
    }
    PeakLevel = 0;
    DryCount = 0;
    ReverseChannels = c.Reverse;
    OutputRate = 22050;

    MixerChannel* ch[8];
    int queued[8];
    std::vector<unsigned char> data;
    for (int s = 0; s < c.Count; ++s) {
        const Source& src = c.Sources[s];
        ch[s] = Mixer_Create_Channel(src.Bits, src.Chans, src.Rate, src.Buffers);
        Mixer_Set_Gain(ch[s], src.Gain);
        queued[s] = 0;
    }
    size_t bytes = size_t(c.Frames) * 4;
    std::vector<unsigned char> out(bytes + 8);
    unsigned char* stream = &out[0] + c.Misalign;
    for (int cb = 0; cb < c.Callbacks; ++cb) {
        // Keep each source fed, as the game does before each callback (up to its block limit).
        for (int s = 0; s < c.Count; ++s) {
            const Source& src = c.Sources[s];
            while ((src.Blocks == 0 || queued[s] < src.Blocks) && Mixer_Free_Buffers(ch[s]) > 0) {
                size_t len = src.BlockLen + size_t(Rand() % 3) * (src.Bits / 8) * src.Chans;
                data.resize(len);
                Make_Data(&data[0], len, src.Bits, src.Kind, src.Amp);
                Mixer_Queue(ch[s], &data[0], len);
                ++queued[s];
                if (queued[s] == 1) {
                    Mixer_Play(ch[s]);
                }
            }
        }
        memset(stream, 0xA5, bytes);
        Mixer_Callback(nullptr, stream, int(bytes));
        Hash_Bytes(stream, bytes);
        Hash_Int(unsigned(PeakLevel));
        PeakLevel = 0;
        for (int s = 0; s < c.Count; ++s) {
            Hash_Int(ch[s]->Frame);
            Hash_Int(ch[s]->Frac);
            Hash_Int(unsigned(ch[s]->Head));
            Hash_Int(ch[s]->BytesPlayed);
            Hash_Int(ch[s]->Playing ? 1 : 0);
        }
    }
    Hash_Int(DryCount);
    for (int s = 0; s < c.Count; ++s) {
        Mixer_Destroy_Channel(ch[s]);
    }
    return Hash;
}

// The hashes from the mixer before the speed-ups (commit c764394), in the order of Cases.
static const unsigned Expected[] = {
    0x0559ad9b, // mono16-full
    0xdb85c03b, // mono16-half
    0xa6bf4b62, // mono16-odd-gain
    0x83260205, // mono16-ffff
    0xbc9b9085, // mono16-silent-gain
    0xe3c14cd5, // mono16-quiet-runs
    0xf1d6f72a, // mono16-odd-blocks
    0xc7d320cd, // mono16-dry
    0x8bf6906f, // stereo16
    0x2a5a8a45, // mono8
    0x5e0d0c2d, // stereo8
    0x80518eb1, // mono16-11k
    0x2459a0d2, // mono16-44k
    0x763e5265, // mono8-22254
    0x17630b95, // loud-four
    0x4bb77c9d, // limit-edge
    0x0e0897b0, // game-mix
    0x0c3fc27e, // game-mix-reversed
    0x703d94b5, // knee
    0x703d94b5, // knee-plus-1
    0xe5a27eb5, // knee-low
    0xe5a27eb5, // knee-low-minus-1
    0x51ba7eb5, // knee-high-only
    0x77d8b377, // reversed-quiet
    0xe6ed7b53, // misaligned-out
    0x9f1dab26, // misaligned-quiet
    0xd85ce605, // tiny-callbacks
    0x32c9778a, // odd-callbacks
};

int main(int argc, char** argv)
{
    bool print = argc > 1 && strcmp(argv[1], "-print") == 0;
    Init_Limit_Table();
    int n = int(sizeof(Cases) / sizeof(Cases[0]));
    int bad = 0;
    bool have_expected = sizeof(Expected) / sizeof(Expected[0]) == size_t(n);
    unsigned all = 2166136261u;
    for (int i = 0; i < n; ++i) {
        unsigned h = Run_Case(Cases[i]);
        all = (all ^ h) * 16777619u;
        if (print) {
            printf("    0x%08x, // %s\n", h, Cases[i].Name);
            continue;
        }
        bool ok = have_expected && h == Expected[i];
        if (!ok) {
            ++bad;
        }
        printf("mixtest: %-20s %08x %s\n", Cases[i].Name, h, have_expected ? (ok ? "same" : "DIFFERENT") : "?");
    }
    printf("mixtest: all cases %08x\n", all);
    if (print) {
        return 0;
    }
    printf("mixtest: %s\n", bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
}
