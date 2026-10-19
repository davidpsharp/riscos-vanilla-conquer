// A sampling profiler for real RISC OS hardware (VC_PROFILE), where there's no
// debugger to pause the machine (riscos/scripts/profile.py does that on RPCEmu).
//
// VC_PROFILE=<file> starts a thread that, at each of UnixLib's context switches
// (every other centisecond), records where the game's other threads were: their
// PC and LR and the APCS frame-pointer chain. VC_PROFILE_SECS=<n> writes the
// file after n seconds; otherwise it's written at exit, or when the buffer
// (VC_PROFILE_MAX samples, 40000 by default) is full. riscos/scripts/vcprof.py
// turns it into flat and call-site profiles.
//
// Measurement only: it doesn't change what the game does.
#ifndef RISCOS_PROFILE_H
#define RISCOS_PROFILE_H

#ifdef __riscos__
void Profile_Start();                  // from the main thread, early in main()
void Profile_Register_Thread(int tag); // from another thread the profile should cover
int Profile_Release_Stale();           // remove handlers left by runs that crashed
#else
inline void Profile_Start()
{
}
inline void Profile_Register_Thread(int)
{
}
#endif

#endif
