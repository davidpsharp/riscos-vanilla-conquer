// A minimal PNG writer for screenshots: 8-bit paletted, no compression (stored deflate blocks).
// No libpng or zlib, and nothing here needs more than ARMv3 (bytes and 32-bit words only).
#ifndef COMMON_PNGWRITE_H
#define COMMON_PNGWRITE_H

#include <stdio.h>

// Writes a w x h image of 8-bit pixels (rows "pitch" bytes apart) to fp, with the 256 colour
// palette as 768 bytes of red, green, blue (0 to 255 each). Returns false if anything
// failed to write (the caller closes fp and removes a bad file).
bool PNG_Write_Paletted(FILE* fp, int w, int h, const unsigned char* pixels, int pitch, const unsigned char* palette);

#endif // COMMON_PNGWRITE_H
