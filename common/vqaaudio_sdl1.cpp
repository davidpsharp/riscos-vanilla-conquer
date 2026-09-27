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
#include "vqaaudio.h"
#include "audio.h"
#include "mixer_sdl1.h"
#include "vqafile.h"
#include "vqaloader.h"
#include "vqatask.h"
#include <algorithm>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SDL 1.2 port of vqaaudio_openal.cpp, streaming movie audio through the
// software mixer in mixer_sdl1.cpp instead of an OpenAL source.

int AudioFlags;
int TimerIntCount;
int TimerMethod;
int VQATickCount;
int TickOffset;
unsigned VQAAudioPaused;
VQAHandle* AudioVQAHandle;

static bool Queue_Audio()
{
    VQAConfig* config = &AudioVQAHandle->Config;
    VQAData* data = AudioVQAHandle->VQABuf;
    VQAAudio* audio = &data->Audio;

    if (audio->MixerChan == nullptr) {
        return false;
    }

    Mixer_Queue(audio->MixerChan, &audio->Buffer[audio->PlayPosition], config->HMIBufSize);
    audio->field_B8 = audio->field_B0;
    audio->field_B0 += config->HMIBufSize;

    if (audio->field_B0 >= audio->BuffBytes) {
        audio->field_B0 = 0;
    }

    audio->field_14 = audio->field_10 + 1;

    if (audio->field_14 >= audio->NumAudBlocks) {
        audio->field_14 = 0;
    }

    if (audio->IsLoaded[audio->field_14] != 1) {
        if (VQAMovieDone) {
            ++audio->field_B4;
        }

        ++audio->NumSkipped;
        config->DrawFlags &= 0xFB;

        return false;
    }

    audio->IsLoaded[audio->field_10] = 0;
    audio->PlayPosition += config->HMIBufSize;
    ++audio->field_10;

    if (audio->PlayPosition >= config->AudioBufSize) {
        audio->PlayPosition = 0;
        audio->field_10 = 0;
    }

    ++audio->field_B4;
    return true;
}

/*
** With VC_AUDIOLOG set, print movie audio pacing to stderr once a second: how
** often the mixer callback runs and how much it mixes, against how far the
** loader and drawer get. The video waits on the audio, so a slow or stalled
** callback shows up here as few frames per second.
*/
static unsigned CopyFullCount = 0;

static void Log_Audio_Pacing(VQAHandle* handle)
{
    static const bool enabled = getenv("VC_AUDIOLOG") != nullptr;
    static unsigned last_ms = 0;
    static MixerStats last;

    if (!enabled) {
        return;
    }

    unsigned now = unsigned(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (last_ms != 0 && now - last_ms < 1000) {
        return;
    }

    MixerStats stats;
    Mixer_Get_Stats(stats);
    if (last_ms != 0) {
        VQAData* data = handle->VQABuf;
        fprintf(stderr,
                "vqa audio: %ums callbacks %u (max gap %ums) mixed %u frames (%dHz, %d/callback) dry %u | "
                "loaded %d drawn %d skipped %u full %u\n",
                now - last_ms,
                stats.Callbacks - last.Callbacks,
                stats.MaxGapMs,
                stats.FramesMixed - last.FramesMixed,
                stats.OutputRate,
                stats.OutputSamples,
                stats.DryCount - last.DryCount,
                data->Loader.CurFrameNum,
                data->Drawer.LastFrameNum,
                data->Audio.NumSkipped,
                CopyFullCount);
    }
    last = stats;
    last_ms = now;
}

void VQA_AudioCallback()
{
    if (!VQAAudioPaused && AudioVQAHandle) {
        VQAAudio* audio = &AudioVQAHandle->VQABuf->Audio;

        Log_Audio_Pacing(AudioVQAHandle);

        if (audio->MixerChan != nullptr) {
            // Work out if we have any space to buffer more data right now.
            if (Mixer_Free_Buffers(audio->MixerChan) > 0) {
                Queue_Audio();
            }

            // If playback ran dry (e.g. a slow frame), restart it once data is queued.
            if (!Mixer_Is_Playing(audio->MixerChan) && Mixer_Free_Buffers(audio->MixerChan) < MIXER_BUFFERS_PER_CHANNEL) {
                Mixer_Play(audio->MixerChan);
            }
        }
    }
}

int VQA_StartTimerInt(VQAHandle* handle, int a2)
{
    VQAData* data = handle->VQABuf;
    VQAAudio* audio = &data->Audio;

    if (!(AudioFlags & VQA_AUDIO_FLAG_INTERRUPT_TIMER)) {
        AudioFlags |= VQA_AUDIO_FLAG_UNKNOWN016;
    }

    audio->Flags |= VQA_AUDIO_FLAG_UNKNOWN016;
    ++TimerIntCount;
    return 0;
}

void VQA_StopTimerInt(VQAHandle* handle)
{
    if (TimerIntCount > 0) {
        --TimerIntCount;
    }

    AudioFlags &= ~VQA_AUDIO_FLAG_INTERRUPT_TIMER;
}
int VQA_OpenAudio(VQAHandle* handle, void* hwnd)
{
    VQAConfig* config = &handle->Config;
    VQAData* data = handle->VQABuf;
    VQAHeader* header = &handle->Header;
    VQAAudio* audio = &data->Audio;

    // We are assuming here that the main game already created an OpenAL device context.
    Start_Primary_Sound_Buffer(true);

    audio->field_10 = 0;
    audio->field_BC = 1;

    // Sampling rate (blocks per second)
    if (config->AudioRate == -1) {
        int rate = 0;

        if (header->FPS == config->FrameRate) {
            rate = audio->SampleRate;
        } else {
            rate = config->FrameRate * audio->SampleRate / header->FPS;
        }

        config->AudioRate = rate;
    }

    audio->field_C0 = 1;

    if (audio->field_BC) {
        audio->field_BC = 0;
    }

    audio->Flags |= VQA_AUDIO_FLAG_UNKNOWN001;
    AudioFlags |= VQA_AUDIO_FLAG_UNKNOWN001;
    return 0;
}

void VQA_CloseAudio(VQAHandle* handle)
{
    VQAConfig* config = &handle->Config;
    VQAData* data = handle->VQABuf;
    VQAAudio* audio = &data->Audio;

    VQA_StopAudio(handle);
    AudioFlags &= ~(VQA_AUDIO_FLAG_UNKNOWN004 | VQA_AUDIO_FLAG_UNKNOWN008);
    audio->Flags &= ~(VQA_AUDIO_FLAG_UNKNOWN004 | VQA_AUDIO_FLAG_UNKNOWN008);

    if (audio->field_C0) {
        audio->field_C0 = 0;
    }

    if (audio->field_BC) {
        audio->field_BC = 0;
    }

    audio->Flags &= ~(VQA_AUDIO_FLAG_UNKNOWN001 | VQA_AUDIO_FLAG_UNKNOWN002);
    AudioFlags &= ~(VQA_AUDIO_FLAG_UNKNOWN001 | VQA_AUDIO_FLAG_UNKNOWN002 | VQA_AUDIO_FLAG_AUDIO_DMA_TIMER);
}

int VQA_StartAudio(VQAHandle* handle)
{
    VQAConfig* config = &handle->Config;
    VQAData* data = handle->VQABuf;
    VQAAudio* audio = &data->Audio;

    AudioVQAHandle = handle;

    // Audio already started, abort.
    if (AudioFlags & VQA_AUDIO_FLAG_AUDIO_DMA_TIMER) {
        return -1;
    }

    if (audio->MixerChan != nullptr) {
        Mixer_Destroy_Channel(audio->MixerChan);
        audio->MixerChan = nullptr;
    }

    audio->MixerChan = Mixer_Create_Channel(audio->BitsPerSample, audio->Channels, audio->SampleRate);
    if (audio->MixerChan == nullptr) {
        return -1;
    }

    audio->BuffBytes = config->HMIBufSize * 4;

    audio->field_B0 = 0;
    audio->field_B4 = 0;

    for (unsigned i = 0; i < MIXER_BUFFERS_PER_CHANNEL; ++i) {
        Queue_Audio();
    }

    Mixer_Set_Gain(audio->MixerChan, unsigned(config->Volume) << 8);
    Mixer_Play(audio->MixerChan);

    audio->Flags |= VQA_AUDIO_FLAG_AUDIO_DMA_TIMER;
    AudioFlags |= VQA_AUDIO_FLAG_AUDIO_DMA_TIMER;
    return 0;
}

void VQA_StopAudio(VQAHandle* handle)
{
    VQAData* data = handle->VQABuf;
    VQAAudio* audio = &data->Audio;

    if (AudioFlags & VQA_AUDIO_FLAG_AUDIO_DMA_TIMER && audio->MixerChan != nullptr) {
        Mixer_Destroy_Channel(audio->MixerChan);
        audio->MixerChan = nullptr;

        audio->Flags &= ~VQA_AUDIO_FLAG_AUDIO_DMA_TIMER;
        AudioFlags &= ~VQA_AUDIO_FLAG_AUDIO_DMA_TIMER;
    }

    AudioVQAHandle = nullptr;
}

void VQA_PauseAudio()
{
    if (AudioVQAHandle) {
        VQAData* data = AudioVQAHandle->VQABuf;

        if (data != nullptr && data->Audio.MixerChan != nullptr && (AudioFlags & 0x40) && !VQAAudioPaused) {
            Mixer_Pause_Channel(data->Audio.MixerChan, true);
            VQAAudioPaused = VQA_GetTime(AudioVQAHandle);
        }
    }
}

void VQA_ResumeAudio()
{
    if (AudioVQAHandle) {
        VQAData* data = AudioVQAHandle->VQABuf;

        if (data != nullptr && data->Audio.MixerChan != nullptr && (AudioFlags & 0x40) && VQAAudioPaused) {
            Mixer_Pause_Channel(data->Audio.MixerChan, false);
            TickOffset -= VQA_GetTime(AudioVQAHandle) - VQAAudioPaused;
            VQAAudioPaused = 0;
        }
    }
}

int VQA_CopyAudio(VQAHandle* handle)
{
    VQAConfig* config = &handle->Config;
    VQAData* data = handle->VQABuf;
    VQAAudio* audio = &data->Audio;

    VQA_AudioCallback();

    if (config->OptionFlags & 1) {
        if (audio->Buffer != nullptr) {
            if (audio->TempBufSize > 0) {
                int current_block = audio->AudBufPos / config->HMIBufSize;
                int next_block = (audio->TempBufSize + audio->AudBufPos) / config->HMIBufSize;

                if ((unsigned)next_block >= audio->NumAudBlocks) {
                    next_block -= audio->NumAudBlocks;
                }

                if (audio->IsLoaded[next_block] == 1) {
                    ++CopyFullCount;
                    return -10;
                }

                // Need to loop back and treat like circular buffer?
                if (next_block < current_block) {
                    int end_space = config->AudioBufSize - audio->AudBufPos;
                    int remaining = audio->TempBufSize - end_space;
                    memcpy(&audio->Buffer[audio->AudBufPos], audio->TempBuf, end_space);
                    memcpy(audio->Buffer, &audio->TempBuf[end_space], remaining);
                    audio->AudBufPos = remaining;
                    audio->TempBufSize = 0;

                    for (unsigned i = current_block; i < audio->NumAudBlocks; ++i) {
                        audio->IsLoaded[i] = 1;
                    }

                    for (int i = 0; i < next_block; ++i) {
                        audio->IsLoaded[i] = 1;
                    }
                } else {
                    memcpy(&audio->Buffer[audio->AudBufPos], audio->TempBuf, audio->TempBufSize);
                    audio->AudBufPos += audio->TempBufSize;
                    audio->TempBufSize = 0;

                    for (int i = current_block; i < next_block; ++i) {
                        audio->IsLoaded[i] = 1;
                    }
                }
            }
        }
    }
    return 0;
}

void VQA_SetTimer(VQAHandle* handle, int time, int method)
{
    if (method == -1) {
        if (AudioFlags & VQA_AUDIO_FLAG_AUDIO_DMA_TIMER) {
            method = VQA_AUDIO_TIMER_METHOD_DMA;
        } else if (AudioFlags & (VQA_AUDIO_FLAG_UNKNOWN016 | VQA_AUDIO_FLAG_UNKNOWN032)) {
            method = VQA_AUDIO_TIMER_METHOD_INTERRUPT;
        } else {
            method = VQA_AUDIO_TIMER_METHOD_DOS;
        }
    } else {
        if (!(AudioFlags & VQA_AUDIO_FLAG_AUDIO_DMA_TIMER) && method == 3) {
            method = VQA_AUDIO_TIMER_METHOD_INTERRUPT;
        }

        if (!(AudioFlags & (VQA_AUDIO_FLAG_UNKNOWN016 | VQA_AUDIO_FLAG_UNKNOWN032))
            && method == VQA_AUDIO_TIMER_METHOD_INTERRUPT) {
            method = VQA_AUDIO_TIMER_METHOD_DOS;
        }
    }

    TimerMethod = method;
    TickOffset = 0;
    TickOffset = time - VQA_GetTime(handle);
}

unsigned VQA_GetTime(VQAHandle* handle)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    unsigned result_time =
        unsigned(TickOffset + 60 * (std::chrono::duration_cast<std::chrono::milliseconds>(now).count()) / 1000);

    return result_time;
}

int VQA_TimerMethod()
{
    return TimerMethod;
}
