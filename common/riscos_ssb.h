/*
** Sound output through SharedSoundBuffer and StreamManager (John Duffell's freeware modules, over
** SharedSound), so the game's sound mixes with other programs' instead of taking DigitalRenderer
** for itself. Optional: the mixer uses it only when asked (mixer_sdl1.h, Mixer_Prefer_Output) and
** the modules are loaded; otherwise SDL plays the sound as before. Elsewhere these do nothing.
*/
#ifndef RISCOS_SSB_H
#define RISCOS_SSB_H

// Fills "len" bytes of 16 bit stereo; called on the output's own thread.
typedef void (*SSBFillFunc)(unsigned char* stream, int len);

#ifdef __riscos__
bool SSB_Available(void);
// rate in Hz; block_frames per fill; queue_ms kept queued ahead of the sound system.
bool SSB_Open(int rate, int block_frames, int queue_ms, SSBFillFunc fill);
void SSB_Close(void);
void SSB_Pause(bool pause);
bool SSB_Is_Paused(void);
// Times the feeding thread found nothing left to play (the sound broke up), and the most queued.
void SSB_Get_Stats(unsigned& dry, unsigned& max_queued);
#else
static inline bool SSB_Available(void)
{
    return false;
}
static inline bool SSB_Open(int, int, int, SSBFillFunc)
{
    return false;
}
static inline void SSB_Close(void)
{
}
static inline void SSB_Pause(bool)
{
}
static inline bool SSB_Is_Paused(void)
{
    return true;
}
static inline void SSB_Get_Stats(unsigned& dry, unsigned& max_queued)
{
    dry = max_queued = 0;
}
#endif

#endif // RISCOS_SSB_H
