/*
** A short test of this machine's speed (about three quarters of a second), and the largest
** screen size in the desktop's shape that the game keeps smooth at that speed. Used for the
** "auto" screen size (riscos_autosize.h); separate so that a test program can run it alone.
*/
#ifndef RISCOS_SPEEDTEST_H
#define RISCOS_SPEEDTEST_H

#ifdef __riscos__
#include <string>

struct RISCOSSpeeds
{
    double cpu;    // thousands of logic-like loops a second
    double memory; // KB/s copied in memory
    double screen; // KB/s written to screen memory
};

// Measures; call it in the desktop's mode, as it writes the desktop's screen (back to itself).
void RISCOS_Measure_Speeds(RISCOSSpeeds& speeds);

// Just the processor's speed, as RISCOSSpeeds::cpu, measured for cs centiseconds.
double RISCOS_Measure_Cpu(unsigned cs);

// The size, as "WxH", for a desktop of dw x dh (square pixels unless !square), with presents a
// second as the game's FrameLimit. Logs each candidate's projected load to stderr.
std::string RISCOS_Pick_Resolution(const RISCOSSpeeds& speeds, int presents, int dw, int dh, bool square);

std::string RISCOS_Size_Text(int w, int h);
#endif

#endif
