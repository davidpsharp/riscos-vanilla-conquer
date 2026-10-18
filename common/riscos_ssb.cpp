/*
** Sound output through SharedSoundBuffer and StreamManager (see riscos_ssb.h).
**
** SharedSoundBuffer plays a stream of 16 bit stereo blocks through SharedSound, mixed with every
** other program's stream and resampled to whatever rate the sound system runs at. StreamManager
** copies each block into its own memory, so nothing here has to stay paged in, and the sound keeps
** going from what is queued while other tasks run. A thread of ours keeps about queue_ms queued:
** it fills a block whenever the queue is below that, and sleeps otherwise. The same interface as
** RDPClient's, and as the SDL 2 driver in riscos-openttd.
**
** If the program ended with the stream open, SharedSoundBuffer would keep it (silent, once the queue
** has run out), so it's closed at exit and from the fatal signals too.
*/
#ifdef __riscos__

#include "riscos_ssb.h"

#include <SDL.h>
#include <SDL_thread.h>
#include <kernel.h>
#include <swis.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Looked up by name, as StreamManager's own manual numbers its SWIs inconsistently.
struct SSBSwis
{
    int Open, Close, Volume, Rate, Pause, Stream;
    int Add, SetBuffer, Stats;
};
SSBSwis Swis;

volatile int Stream = 0;   // SharedSoundBuffer handle, 0 when closed
int StreamManagerHandle;   // the StreamManager stream under it
int BlockBytes;
int BytesPerMs;
int TargetBytes;
int StartBytes;            // queued before playback starts, so it doesn't start by running dry
SSBFillFunc Fill;
unsigned char* Block;
SDL_Thread* Feeder;
volatile bool Quit;
volatile bool Feeding;       // the thread is running
volatile bool Paused = true; // by the game (SSB_Pause)
bool Started;                // playback started once enough was queued
unsigned DryCount;
unsigned MaxQueued;

int Swi_Number(const char* name)
{
    _kernel_swi_regs regs;
    regs.r[1] = reinterpret_cast<int>(name);
    if (_kernel_swi(OS_SWINumberFromString, &regs, &regs) != nullptr) {
        return 0;
    }
    return regs.r[0] | 0x20000; // the X form
}

bool Call(int swi, _kernel_swi_regs& regs, const char* what)
{
    _kernel_oserror* error = _kernel_swi(swi, &regs, &regs);
    if (error != nullptr) {
        fprintf(stderr, "audio: %s: %s\n", what, error->errmess);
        return false;
    }
    return true;
}

// Bytes queued that the sound system hasn't played yet, or -1. BufferStats gives the bytes added
// and the bytes played so far (its manual says only the first, the unplayed bytes).
int Queued()
{
    _kernel_swi_regs regs;
    regs.r[0] = StreamManagerHandle;
    if (_kernel_swi(Swis.Stats, &regs, &regs) != nullptr) {
        return -1;
    }
    return regs.r[0] - regs.r[1];
}

void Set_Playing(bool play)
{
    _kernel_swi_regs regs;
    regs.r[0] = Stream;
    regs.r[1] = play ? 1 : 0; // bit 0: resume
    _kernel_swi(Swis.Pause, &regs, &regs);
}

int Feed(void*)
{
    Feeding = true;
    while (!Quit) {
        if (Paused) {
            SDL_Delay(10);
            continue;
        }
        int queued = Queued();
        if (queued < 0) {
            SDL_Delay(10);
            continue;
        }
        if (Started && queued == 0) {
            ++DryCount;
        }
        if (unsigned(queued) > MaxQueued) {
            MaxQueued = unsigned(queued);
        }
        if (queued >= TargetBytes) {
            // Sleep for about half of what's over the target.
            int ms = (queued - TargetBytes) / BytesPerMs / 2 + 1;
            SDL_Delay(ms > 20 ? 20 : ms);
            continue;
        }
        Fill(Block, BlockBytes);
        _kernel_swi_regs regs;
        regs.r[0] = StreamManagerHandle;
        regs.r[1] = reinterpret_cast<int>(Block);
        regs.r[2] = BlockBytes;
        if (_kernel_swi(Swis.Add, &regs, &regs) != nullptr) {
            SDL_Delay(5); // full: let some play (the block is lost, which shouldn't happen at our limit)
            continue;
        }
        if (!Started && queued + BlockBytes >= StartBytes && !Paused) {
            Set_Playing(true);
            Started = true;
        }
    }
    Feeding = false;
    return 0;
}

// From a fatal signal: close the stream, without waiting for the thread.
void Close_Stream()
{
    int stream = Stream;
    Stream = 0;
    if (stream != 0) {
        _kernel_swi_regs regs;
        regs.r[0] = stream;
        _kernel_swi(Swis.Close, &regs, &regs);
    }
}

// At exit (if the game didn't shut the sound down): let the thread stop, briefly, so it isn't
// adding a block to the stream as it's closed; it can't be joined from an atexit handler.
void Close_At_Exit()
{
    Quit = true;
    for (int i = 0; i < 20 && Feeding; ++i) {
        SDL_Delay(5);
    }
    Close_Stream();
}

void (*Old_Handlers[NSIG])(int);
const int Fatal_Signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM};

void On_Fatal_Signal(int sig)
{
    Quit = true;
    Close_Stream();
    signal(sig, Old_Handlers[sig] == SIG_ERR ? SIG_DFL : Old_Handlers[sig]);
    raise(sig);
}

void Install_Exit_Handlers()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    atexit(Close_At_Exit);
    for (int sig : Fatal_Signals) {
        Old_Handlers[sig] = signal(sig, On_Fatal_Signal);
    }
}

} // namespace

bool SSB_Available(void)
{
    Swis.Open = Swi_Number("SharedSoundBuffer_OpenStream");
    Swis.Close = Swi_Number("SharedSoundBuffer_CloseStream");
    Swis.Volume = Swi_Number("SharedSoundBuffer_Volume");
    Swis.Rate = Swi_Number("SharedSoundBuffer_SampleRate");
    Swis.Pause = Swi_Number("SharedSoundBuffer_Pause");
    Swis.Stream = Swi_Number("SharedSoundBuffer_ReturnStreamHandle");
    Swis.Add = Swi_Number("StreamManager_AddBlock");
    Swis.SetBuffer = Swi_Number("StreamManager_SetBuffer");
    Swis.Stats = Swi_Number("StreamManager_BufferStats");
    return Swis.Open && Swis.Close && Swis.Volume && Swis.Rate && Swis.Pause && Swis.Stream && Swis.Add
           && Swis.SetBuffer && Swis.Stats;
}

bool SSB_Open(int rate, int block_frames, int queue_ms, SSBFillFunc fill)
{
    if (Stream != 0 || !SSB_Available()) {
        return false;
    }

    BlockBytes = block_frames * 4;
    BytesPerMs = rate * 4 / 1000;
    TargetBytes = BytesPerMs * queue_ms;
    if (TargetBytes < BlockBytes * 2) {
        TargetBytes = BlockBytes * 2;
    }
    StartBytes = BlockBytes * 2;
    Block = static_cast<unsigned char*>(malloc(BlockBytes));
    if (Block == nullptr) {
        return false;
    }

    // No usual block size (flag bit 1): on RISC OS 4, SharedSoundBuffer 0.07 then has room for only
    // one queued block of that size ("Not enough memory in dynamic area" for the next).
    _kernel_swi_regs regs;
    regs.r[0] = 0;
    regs.r[1] = reinterpret_cast<int>("Vanilla Conquer");
    regs.r[2] = 0;
    if (!Call(Swis.Open, regs, "SharedSoundBuffer_OpenStream")) {
        free(Block);
        Block = nullptr;
        return false;
    }
    Stream = regs.r[0];
    Install_Exit_Handlers();

    regs.r[0] = Stream;
    if (!Call(Swis.Stream, regs, "SharedSoundBuffer_ReturnStreamHandle")) {
        SSB_Close();
        return false;
    }
    StreamManagerHandle = regs.r[0];

    // Room for well over the target, so AddBlock never has to refuse.
    regs.r[0] = StreamManagerHandle;
    regs.r[1] = TargetBytes * 4 + BlockBytes * 4;
    Call(Swis.SetBuffer, regs, "StreamManager_SetBuffer");

    regs.r[0] = Stream;
    regs.r[1] = rate * 1024; // in 1/1024 Hz
    Call(Swis.Rate, regs, "SharedSoundBuffer_SampleRate");

    regs.r[0] = Stream;
    regs.r[1] = int(0xFFFFFFFF); // full volume, left and right: the desktop's volume still applies
    Call(Swis.Volume, regs, "SharedSoundBuffer_Volume");

    Set_Playing(false);
    Started = false;
    Paused = true;
    Fill = fill;
    Quit = false;
    Feeder = SDL_CreateThread(Feed, nullptr);
    if (Feeder == nullptr) {
        SSB_Close();
        return false;
    }
    return true;
}

void SSB_Close(void)
{
    Quit = true;
    if (Feeder != nullptr) {
        SDL_WaitThread(Feeder, nullptr);
        Feeder = nullptr;
    }
    Close_Stream();
    free(Block);
    Block = nullptr;
}

void SSB_Pause(bool pause)
{
    if (Stream == 0 || pause == Paused) {
        return;
    }
    Paused = pause;
    if (pause) {
        Set_Playing(false);
    } else if (Started) {
        Set_Playing(true);
    }
}

bool SSB_Is_Paused(void)
{
    return Stream == 0 || Paused;
}

void SSB_Get_Stats(unsigned& dry, unsigned& max_queued)
{
    dry = DryCount;
    max_queued = MaxQueued;
    DryCount = 0;
    MaxQueued = 0;
}

#endif // __riscos__
