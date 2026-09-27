// Minimal software mixer on top of SDL 1.2 audio.
//
// SDL 1.2 only offers a single audio callback, so this provides OpenAL-like
// streaming channels for the sound and VQA movie backends: each channel has a
// small queue of buffers that are played in order, reports how many have
// finished, and stops when it runs out of data (like an OpenAL source).
#ifndef COMMON_MIXER_SDL1_H
#define COMMON_MIXER_SDL1_H

#include <stddef.h>

enum
{
    MIXER_BUFFERS_PER_CHANNEL = 2, // Default queue length, as for the OpenAL backend.
    MIXER_MAX_BUFFERS = 8,
};

struct MixerChannel;

bool Mixer_Init(int rate, bool reverse_channels);
void Mixer_Shutdown();
void Mixer_Pause(bool pause);
bool Mixer_Is_Open();

// buffers: queue length, up to MIXER_MAX_BUFFERS. A channel that runs out of queued
// data stops, but resumes by itself when more is queued (unlike an OpenAL source),
// so a late refill causes a gap rather than cutting the sound off.
MixerChannel* Mixer_Create_Channel(int bits_per_sample, int channels, int rate, int buffers = MIXER_BUFFERS_PER_CHANNEL);
void Mixer_Destroy_Channel(MixerChannel* ch);

void Mixer_Set_Format(MixerChannel* ch, int bits_per_sample, int channels, int rate);
// gain is 16.16 fixed point, 0x10000 = unity.
void Mixer_Set_Gain(MixerChannel* ch, unsigned gain);

// Copies data into a free buffer slot. Returns false if no slot was free.
bool Mixer_Queue(MixerChannel* ch, const void* data, size_t len);
// Number of slots that are unused or whose data has finished playing.
int Mixer_Free_Buffers(MixerChannel* ch);

void Mixer_Play(MixerChannel* ch);
void Mixer_Pause_Channel(MixerChannel* ch, bool pause);
// Stops playback and discards all queued data.
void Mixer_Stop(MixerChannel* ch);
// True while the channel is playing and still has queued data.
bool Mixer_Is_Playing(MixerChannel* ch);

// Running totals for diagnosing playback pacing.
struct MixerStats
{
    unsigned Callbacks;    // Audio callbacks so far.
    unsigned FramesMixed;  // Output frames mixed so far.
    unsigned MaxGapMs;     // Longest gap between callbacks since the last call.
    unsigned DryCount;     // Times a channel ran out of queued data.
    int OutputRate;
    int OutputSamples;     // Callback size in frames.
};
void Mixer_Get_Stats(MixerStats& stats);
// Bytes queued and played since the channel was last stopped.
void Mixer_Get_Channel_Counts(MixerChannel* ch, unsigned& queued, unsigned& played);

#endif // COMMON_MIXER_SDL1_H
