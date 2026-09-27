// SDL 1.2 implementation of the sound backend used by soundio_common.cpp,
// built on the software mixer in mixer_sdl1.cpp.
#include "soundio_imp.h"
#include "mixer_sdl1.h"
#include <stdio.h>
#include <stdlib.h>

// With VC_FPSLOG set, log each sound's start and stop, with bytes queued and played.
static bool Sound_Log()
{
    static const bool enabled = getenv("VC_FPSLOG") != nullptr;
    return enabled;
}

struct SampleTrackerTypeImp
{
    MixerChannel* Channel;
};

void SoundImp_Buffer_Sample_Data(SampleTrackerTypeImp* st, const void* data, size_t datalen)
{
    Mixer_Queue(st->Channel, data, datalen);
}

int SoundImp_Get_Sample_Free_Buffer_Count(SampleTrackerTypeImp* st)
{
    return Mixer_Free_Buffers(st->Channel);
}

bool SoundImp_Init(int bits_per_sample, bool stereo, int rate, bool reverse_channels)
{
    (void)bits_per_sample;
    (void)stereo;
    return Mixer_Init(rate, reverse_channels);
}

void SoundImp_PauseSound()
{
    Mixer_Pause(true);
}

bool SoundImp_ResumeSound()
{
    if (!Mixer_Is_Open()) {
        return false;
    }
    Mixer_Pause(false);
    return true;
}

SampleTrackerTypeImp* SoundImp_Init_Sample(int bits_per_sample, bool stereo, int rate)
{
    // More queue than OpenAL's 2 buffers: on a real Risc PC the SDL audio thread drains
    // the queue in bursts, and 2 x 8 KB ran dry between refills, cutting speech short.
    MixerChannel* ch = Mixer_Create_Channel(bits_per_sample, stereo ? 2 : 1, rate, 6);
    if (ch == nullptr) {
        return nullptr;
    }

    SampleTrackerTypeImp* st = new SampleTrackerTypeImp;
    st->Channel = ch;
    return st;
}

bool SoundImp_Sample_Status(SampleTrackerTypeImp* st)
{
    return Mixer_Is_Playing(st->Channel);
}

void SoundImp_Set_Sample_Attributes(SampleTrackerTypeImp* st, int bits_per_sample, bool stereo, int rate)
{
    Mixer_Set_Format(st->Channel, bits_per_sample, stereo ? 2 : 1, rate);
}

void SoundImp_Set_Sample_Volume(SampleTrackerTypeImp* st, unsigned int volume)
{
    // volume is 0-65535 (sound volume * sample volume), as for OpenAL's AL_GAIN / 65536.
    Mixer_Set_Gain(st->Channel, volume);
}

void SoundImp_Shutdown()
{
    Mixer_Shutdown();
}

void SoundImp_Shutdown_Sample(SampleTrackerTypeImp* st)
{
    Mixer_Destroy_Channel(st->Channel);
    delete st;
}

void SoundImp_Start_Sample(SampleTrackerTypeImp* st)
{
    if (Sound_Log()) {
        unsigned queued, played;
        Mixer_Get_Channel_Counts(st->Channel, queued, played);
        fprintf(stderr, "sound: start %p, %u bytes queued\n", (void*)st, queued);
    }
    Mixer_Play(st->Channel);
}

void SoundImp_Stop_Sample(SampleTrackerTypeImp* st)
{
    if (Sound_Log()) {
        unsigned queued, played;
        Mixer_Get_Channel_Counts(st->Channel, queued, played);
        if (queued != 0) {
            fprintf(stderr,
                    "sound: stop %p, %u bytes queued, %u played%s\n",
                    (void*)st,
                    queued,
                    played,
                    played < queued ? " - CUT SHORT" : "");
        }
    }
    Mixer_Stop(st->Channel);
}
