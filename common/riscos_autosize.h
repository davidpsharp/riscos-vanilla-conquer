/*
** The game's screen size when the player hasn't chosen one ("auto", the RISC OS default): the
** largest that this machine can keep smooth, in the desktop's shape. A short speed test the first
** time measures the processor, memory and screen memory; its results are kept in the settings
** file ([RISCOS] section), so later starts only redo the choice, for the desktop as it is then.
** VC_RETUNE=1 (a system variable) runs the test again.
*/
#ifndef RISCOS_AUTOSIZE_H
#define RISCOS_AUTOSIZE_H

#ifdef __riscos__
#include <string>

// The size, as "WxH", reading and updating the speed test's results in the settings file ini_name.
// Worked out once per run; call it in the desktop's mode, before the game's screen opens.
const std::string& RISCOS_Auto_Resolution(const char* ini_name);

// The desktop's size in square pixels (as autosize sees it), read once, in the desktop's mode, before the game's
// screen opens. No screen size bigger than this can be opened.
void RISCOS_Desktop_Size(int& width, int& height);
#endif

#endif
