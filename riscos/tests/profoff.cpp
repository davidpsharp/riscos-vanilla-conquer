// profoff: remove the profiler's interrupt handler (VC_PROFILE, common/riscos_profile.cpp)
// if a profiled run of the game died without removing it. Safe to run at any time.
#include "riscos_profile.h"
#include <stdio.h>

volatile unsigned Profile_Game_Frames = 0; // the game's is in phasetime.cpp

int main()
{
    int n = Profile_Release_Stale();
    printf("profoff: %d handler(s) removed\n", n);
    return 0;
}
