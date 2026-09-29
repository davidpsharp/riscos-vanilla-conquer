#include "mixer_sdl1.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum SlotState
{
    SLOT_FREE,
    SLOT_QUEUED,
    SLOT_DONE,
};

struct MixerSlot
{
    unsigned char* Data;
    size_t Length;
    size_t Capacity;
    SlotState State;
};

struct MixerChannel
{
    MixerSlot Slots[MIXER_MAX_BUFFERS];
    int SlotCount;
    int Head; // Slot being played.
    int Tail; // Next slot to fill.

    int BitsPerSample;
    int Channels;
    int Rate;
    unsigned Step; // Source frames per output frame, 16.16 fixed point.

    unsigned Frame; // Current frame within the head slot.
    unsigned Frac;  // Fractional part of the position, 16 bits.

    unsigned Gain; // 16.16 fixed point.
    bool Playing;
    bool Paused;
    bool Starved; // Stopped because it ran out of data; resumes when more is queued.

    unsigned BytesQueued; // Since the last Mixer_Stop, for diagnostics.
    unsigned BytesPlayed;
};

enum
{
    MAX_MIXER_CHANNELS = 32,
};

static MixerChannel* Channels[MAX_MIXER_CHANNELS];

// Total output frames mixed; read by debugging tools to check playback pacing.
volatile unsigned Mixer_Frames_Mixed = 0;
static unsigned Callbacks = 0;
static unsigned LastCallbackMs = 0;
static unsigned MaxGapMs = 0;
static unsigned DryCount = 0;
static int PeakLevel = 0;
static int MaxPlaying = 0;
static int OutputSamples = 0;
static bool AudioOpen = false;
static bool ReverseChannels = false;
static int OutputRate = 22050;

static void Update_Step(MixerChannel* ch)
{
    ch->Step = (unsigned)(((unsigned long long)ch->Rate << 16) / (unsigned)OutputRate);
}

/*
** Reads one frame from the head slot as signed 16 bit left/right values.
*/
static void Read_Frame(const MixerChannel* ch, const MixerSlot* slot, unsigned frame, int& left, int& right)
{
    if (ch->BitsPerSample == 16) {
        const unsigned char* p = slot->Data + frame * 2 * ch->Channels;
        left = (short)(p[0] | (p[1] << 8));
        right = ch->Channels > 1 ? (short)(p[2] | (p[3] << 8)) : left;
    } else {
        const unsigned char* p = slot->Data + frame * ch->Channels;
        left = (p[0] - 128) << 8;
        right = ch->Channels > 1 ? (p[1] - 128) << 8 : left;
    }
}

static void Mix_Channel(MixerChannel* ch, int* mix, int frames)
{
    int bytes_per_frame = (ch->BitsPerSample / 8) * ch->Channels;

    for (int i = 0; i < frames && ch->Playing; ++i) {
        MixerSlot* slot = &ch->Slots[ch->Head];

        // Move on to the next queued slot when this one runs out.
        while (slot->State == SLOT_QUEUED && ch->Frame >= slot->Length / bytes_per_frame) {
            ch->Frame -= unsigned(slot->Length / bytes_per_frame);
            ch->BytesPlayed += unsigned(slot->Length);
            slot->State = SLOT_DONE;
            ch->Head = (ch->Head + 1) % ch->SlotCount;
            slot = &ch->Slots[ch->Head];
        }

        if (slot->State != SLOT_QUEUED) {
            // Out of data: stop until more is queued.
            ch->Playing = false;
            ch->Starved = true;
            ++DryCount;
            ch->Frame = 0;
            ch->Frac = 0;
            break;
        }

        int left, right;
        Read_Frame(ch, slot, ch->Frame, left, right);
        mix[i * 2] += (int)(((long long)left * ch->Gain) >> 16);
        mix[i * 2 + 1] += (int)(((long long)right * ch->Gain) >> 16);

        unsigned pos = ch->Frac + ch->Step;
        ch->Frame += pos >> 16;
        ch->Frac = pos & 0xFFFF;
    }
}

static void SDLCALL Mixer_Callback(void* userdata, Uint8* stream, int len)
{
    (void)userdata;
    int frames = len / 4; // 16 bit stereo output.
    static int* mix = nullptr;
    static int mix_frames = 0;

    if (frames > mix_frames) {
        free(mix);
        mix = (int*)malloc(sizeof(int) * 2 * frames);
        mix_frames = mix ? frames : 0;
        if (!mix) {
            memset(stream, 0, len);
            return;
        }
    }

    memset(mix, 0, sizeof(int) * 2 * frames);
    Mixer_Frames_Mixed += frames;

    unsigned now = SDL_GetTicks();
    if (Callbacks++ != 0 && now - LastCallbackMs > MaxGapMs) {
        MaxGapMs = now - LastCallbackMs;
    }
    LastCallbackMs = now;

    int playing = 0;
    for (int c = 0; c < MAX_MIXER_CHANNELS; ++c) {
        MixerChannel* ch = Channels[c];
        if (ch != nullptr && ch->Playing && !ch->Paused) {
            Mix_Channel(ch, mix, frames);
            ++playing;
        }
    }
    if (playing > MaxPlaying) {
        MaxPlaying = playing;
    }

    Sint16* out = (Sint16*)stream;
    for (int i = 0; i < frames; ++i) {
        int l = mix[i * 2];
        int r = mix[i * 2 + 1];
        if (ReverseChannels) {
            int t = l;
            l = r;
            r = t;
        }
        out[i * 2] = (Sint16)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
        out[i * 2 + 1] = (Sint16)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        int al = l < 0 ? -l : l;
        int ar = r < 0 ? -r : r;
        int level = al > ar ? al : ar;
        if (level > PeakLevel) {
            PeakLevel = level;
        }
    }
}

bool Mixer_Init(int rate, bool reverse_channels)
{
    if (AudioOpen) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return false;
    }

    SDL_AudioSpec desired;
    memset(&desired, 0, sizeof(desired));
    desired.freq = rate > 0 ? rate : 22050;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
#ifdef __riscos__
    desired.samples = 2048; // Larger buffer to ride out slow frames on older machines.
#else
    desired.samples = 1024;
#endif
    desired.callback = Mixer_Callback;

    // Passing no "obtained" spec makes SDL convert to the device format for us.
    if (SDL_OpenAudio(&desired, nullptr) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    OutputRate = desired.freq;
    OutputSamples = desired.samples;

    if (getenv("VC_FPSLOG") != nullptr) {
        char driver[32] = "?";
        SDL_AudioDriverName(driver, sizeof(driver));
        fprintf(stderr,
                "audio: driver %s, %d Hz, %d frames per callback (%u bytes)\n",
                driver,
                desired.freq,
                desired.samples,
                unsigned(desired.size));
    }
    ReverseChannels = reverse_channels;
    AudioOpen = true;
    SDL_PauseAudio(0);
    return true;
}

void Mixer_Shutdown()
{
    if (!AudioOpen) {
        return;
    }
    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    AudioOpen = false;
}

void Mixer_Pause(bool pause)
{
    if (AudioOpen) {
        SDL_PauseAudio(pause ? 1 : 0);
    }
}

void Mixer_Get_Stats(MixerStats& stats)
{
    SDL_LockAudio();
    stats.Callbacks = Callbacks;
    stats.FramesMixed = Mixer_Frames_Mixed;
    stats.MaxGapMs = MaxGapMs;
    stats.DryCount = DryCount;
    stats.OutputRate = OutputRate;
    stats.OutputSamples = OutputSamples;
    stats.Peak = PeakLevel > 32767 ? 32767 : PeakLevel;
    stats.MaxChannels = MaxPlaying;
    MaxGapMs = 0;
    PeakLevel = 0;
    MaxPlaying = 0;
    SDL_UnlockAudio();
}

bool Mixer_Is_Open()
{
    return AudioOpen;
}

MixerChannel* Mixer_Create_Channel(int bits_per_sample, int channels, int rate, int buffers)
{
    MixerChannel* ch = (MixerChannel*)calloc(1, sizeof(MixerChannel));
    if (ch == nullptr) {
        return nullptr;
    }

    ch->BitsPerSample = bits_per_sample == 16 ? 16 : 8;
    ch->Channels = channels > 1 ? 2 : 1;
    ch->Rate = rate > 0 ? rate : 22050;
    ch->Gain = 0x10000;
    ch->SlotCount = buffers < 1 ? 1 : (buffers > MIXER_MAX_BUFFERS ? MIXER_MAX_BUFFERS : buffers);
    Update_Step(ch);

    SDL_LockAudio();
    for (int c = 0; c < MAX_MIXER_CHANNELS; ++c) {
        if (Channels[c] == nullptr) {
            Channels[c] = ch;
            SDL_UnlockAudio();
            return ch;
        }
    }
    SDL_UnlockAudio();

    free(ch);
    return nullptr;
}

void Mixer_Destroy_Channel(MixerChannel* ch)
{
    if (ch == nullptr) {
        return;
    }

    SDL_LockAudio();
    for (int c = 0; c < MAX_MIXER_CHANNELS; ++c) {
        if (Channels[c] == ch) {
            Channels[c] = nullptr;
        }
    }
    SDL_UnlockAudio();

    for (int i = 0; i < MIXER_MAX_BUFFERS; ++i) {
        free(ch->Slots[i].Data);
    }
    free(ch);
}

void Mixer_Set_Format(MixerChannel* ch, int bits_per_sample, int channels, int rate)
{
    SDL_LockAudio();
    ch->BitsPerSample = bits_per_sample == 16 ? 16 : 8;
    ch->Channels = channels > 1 ? 2 : 1;
    ch->Rate = rate > 0 ? rate : 22050;
    Update_Step(ch);
    SDL_UnlockAudio();
}

void Mixer_Set_Gain(MixerChannel* ch, unsigned gain)
{
    SDL_LockAudio();
    ch->Gain = gain;
    SDL_UnlockAudio();
}

bool Mixer_Queue(MixerChannel* ch, const void* data, size_t len)
{
    if (len == 0) {
        return true;
    }

    SDL_LockAudio();
    MixerSlot* slot = &ch->Slots[ch->Tail];
    bool free_slot = slot->State != SLOT_QUEUED;
    SDL_UnlockAudio();

    if (!free_slot) {
        return false;
    }

    // The callback never touches slots that aren't queued, so fill it unlocked.
    if (slot->Capacity < len) {
        unsigned char* data_copy = (unsigned char*)realloc(slot->Data, len);
        if (data_copy == nullptr) {
            return false;
        }
        slot->Data = data_copy;
        slot->Capacity = len;
    }
    memcpy(slot->Data, data, len);
    slot->Length = len;

    SDL_LockAudio();
    slot->State = SLOT_QUEUED;
    ch->BytesQueued += unsigned(len);
    ch->Tail = (ch->Tail + 1) % ch->SlotCount;
    if (ch->Starved) {
        ch->Starved = false;
        ch->Playing = true;
    }
    SDL_UnlockAudio();
    return true;
}

int Mixer_Free_Buffers(MixerChannel* ch)
{
    int count = 0;
    SDL_LockAudio();
    for (int i = 0; i < ch->SlotCount; ++i) {
        if (ch->Slots[i].State != SLOT_QUEUED) {
            ++count;
        }
    }
    SDL_UnlockAudio();
    return count;
}

void Mixer_Play(MixerChannel* ch)
{
    SDL_LockAudio();
    ch->Paused = false;
    ch->Playing = ch->Slots[ch->Head].State == SLOT_QUEUED;
    ch->Starved = !ch->Playing;
    SDL_UnlockAudio();
}

void Mixer_Pause_Channel(MixerChannel* ch, bool pause)
{
    SDL_LockAudio();
    ch->Paused = pause;
    SDL_UnlockAudio();
}

void Mixer_Stop(MixerChannel* ch)
{
    SDL_LockAudio();
    ch->Playing = false;
    ch->Paused = false;
    ch->Starved = false;
    ch->BytesQueued = 0;
    ch->BytesPlayed = 0;
    ch->Head = 0;
    ch->Tail = 0;
    ch->Frame = 0;
    ch->Frac = 0;
    for (int i = 0; i < MIXER_MAX_BUFFERS; ++i) {
        ch->Slots[i].State = SLOT_FREE;
    }
    SDL_UnlockAudio();
}

void Mixer_Get_Channel_Counts(MixerChannel* ch, unsigned& queued, unsigned& played)
{
    SDL_LockAudio();
    queued = ch->BytesQueued;
    played = ch->BytesPlayed;
    SDL_UnlockAudio();
}

bool Mixer_Is_Playing(MixerChannel* ch)
{
    SDL_LockAudio();
    bool playing = ch->Playing && ch->Slots[ch->Head].State == SLOT_QUEUED;
    SDL_UnlockAudio();
    return playing;
}
