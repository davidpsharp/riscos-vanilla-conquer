/*
** The movie player's clock on RISC OS (vqaaudio_sdl1.cpp, VQA_GetTime): its ticks are 1/60 s,
** and RISC OS counts centiseconds (OS_Word 14's real-time clock).
*/
#ifndef VQACLOCK_H
#define VQACLOCK_H

/*
** 60 ticks a second from a count of centiseconds: cs * 60 / 100 rounded down, as the clock
** it replaced gave (60 * ms / 1000 of a millisecond count that moved in centiseconds), in
** 32 bits without overflow for any count: cs * 3 / 5 is (cs / 5) * 3 plus (cs % 5) * 3 / 5.
** No 64-bit divide (a library call on ARM, which has no divide instruction).
*/
static inline unsigned VQA_Ticks_From_Cs(unsigned cs)
{
    unsigned fifths = cs / 5; // a divide by a constant: a multiply, no library call
    return fifths * 3 + (cs - fifths * 5) * 3 / 5;
}

#endif
