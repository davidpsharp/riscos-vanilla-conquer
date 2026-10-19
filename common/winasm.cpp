#include "winasm.h"
#include <stdint.h>
#include <string.h>

struct InterpolationTable* InterpolationTable = NULL;

static void Interpolate_X_Axis(void* src, void* dst, int src_width);

/**
 * Interpolates in X axis, leaves additional lines blank in the Y axis.
 * Each line is built in a cached buffer and copied out in one go: writing the
 * destination a byte at a time was much slower on a Risc PC.
 */
void Asm_Interpolate(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* sptr = (unsigned char*)(src);

    while (src_height--) {
        Interpolate_X_Axis(sptr, InterpolationTable->LineBuffer, src_width);
        memcpy(dptr, InterpolationTable->LineBuffer, 2 * src_width);
        sptr += src_width;
        dptr += dst_pitch;
    }
}

/**
 * Interpolates in X axis, duplicates lines in the Y axis.
 */
void Asm_Interpolate_Line_Double(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* sptr = (unsigned char*)(src);

    while (src_height--) {
        Interpolate_X_Axis(sptr, InterpolationTable->LineBuffer, src_width);
        memcpy(dptr, InterpolationTable->LineBuffer, 2 * src_width);
        memcpy(dptr + dst_pitch / 2, InterpolationTable->LineBuffer, 2 * src_width);
        sptr += src_width;
        dptr += dst_pitch;
    }
}

/*
** A word at a time where the byte order allows (little-endian: the first pixel is the low byte),
** with the same output as a byte at a time:
** - the table's base is held in a local, where the old byte loop reloaded it through the global
**   after every byte store (which might have changed it, as far as the compiler knew);
** - 4 source pixels are read as a word and the 8 output bytes are stored as 2 words;
** - 4 pixels all one colour, with the next one the same too (common in movies), are copied
**   without any table look-up.
** It was 8-12 instructions a pixel with two byte stores, and is now about 9 per pixel where the
** neighbours differ and 2 where they don't.
*/
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define INTERPOLATE_BY_WORD 1
typedef unsigned int __attribute__((may_alias)) InterpolateWord;
#endif

// The colour between a and b: a if they're the same, else from the table.
#define INTERPOLATE_BLEND(table, a, b) ((a) == (b) ? (a) : (table)[((a) << 8) | (b)])

static void Interpolate_X_Axis(void* src, void* dst, int src_width)
{
    const unsigned char* s = (const unsigned char*)(src);
    unsigned char* d = (unsigned char*)(dst);
    const unsigned char* table = &InterpolationTable->PaletteInterpolationTable[0][0];
    int n = src_width - 1; // pixels followed by a neighbour

#ifdef INTERPOLATE_BY_WORD
    // Each source pixel makes two output bytes, so once the source is word-aligned the output is
    // too, if it started at twice the source's alignment (the line buffers and frames do).
    if ((((uintptr_t)d - 2 * (uintptr_t)s) & 3) == 0) {
        for (; n > 0 && ((uintptr_t)s & 3) != 0; --n, ++s, d += 2) {
            unsigned a = s[0], b = s[1];
            d[0] = (unsigned char)a;
            d[1] = INTERPOLATE_BLEND(table, a, b);
        }
        for (; n >= 4; n -= 4, s += 4, d += 8) {
            unsigned w = *(const InterpolateWord*)s;
            unsigned p0 = w & 0xff, p4 = s[4];
            if (w == ((w << 8) | (w >> 24)) && p4 == p0) { // all 5 the same
                ((InterpolateWord*)d)[0] = w;
                ((InterpolateWord*)d)[1] = w;
                continue;
            }
            unsigned p1 = (w >> 8) & 0xff, p2 = (w >> 16) & 0xff, p3 = w >> 24;
            ((InterpolateWord*)d)[0] =
                p0 | (INTERPOLATE_BLEND(table, p0, p1) << 8) | (p1 << 16) | (INTERPOLATE_BLEND(table, p1, p2) << 24);
            ((InterpolateWord*)d)[1] =
                p2 | (INTERPOLATE_BLEND(table, p2, p3) << 8) | (p3 << 16) | (INTERPOLATE_BLEND(table, p3, p4) << 24);
        }
    }
#endif
    for (; n > 0; --n, ++s, d += 2) {
        unsigned a = s[0], b = s[1];
        d[0] = (unsigned char)a;
        // Neighbours are often the same colour; skip the table (and a likely cache miss).
        d[1] = INTERPOLATE_BLEND(table, a, b);
    }

    d[0] = *s;
    d[1] = 0;
}

static void Interpolate_Y_Axis(void* top_line, void* bottom_line, void* middle_line, int src_width)
{
    const unsigned char* tlp = (const unsigned char*)(top_line);
    const unsigned char* blp = (const unsigned char*)(bottom_line);
    unsigned char* mlp = (unsigned char*)(middle_line);
    const unsigned char* table = &InterpolationTable->PaletteInterpolationTable[0][0];
    int n = 2 * src_width;

#ifdef INTERPOLATE_BY_WORD
    if ((((uintptr_t)tlp | (uintptr_t)blp | (uintptr_t)mlp) & 3) == 0) {
        for (; n >= 4; n -= 4, tlp += 4, blp += 4, mlp += 4) {
            unsigned x = *(const InterpolateWord*)tlp, y = *(const InterpolateWord*)blp;
            if (x != y) { // else all 4 are copied as they are
                unsigned a = x & 0xff, b = y & 0xff;
                unsigned r = INTERPOLATE_BLEND(table, a, b);
                a = (x >> 8) & 0xff, b = (y >> 8) & 0xff;
                r |= INTERPOLATE_BLEND(table, a, b) << 8;
                a = (x >> 16) & 0xff, b = (y >> 16) & 0xff;
                r |= INTERPOLATE_BLEND(table, a, b) << 16;
                a = x >> 24, b = y >> 24;
                x = r | (INTERPOLATE_BLEND(table, a, b) << 24);
            }
            *(InterpolateWord*)mlp = x;
        }
    }
#endif
    for (; n > 0; --n) {
        unsigned a = *tlp++, b = *blp++;
        *mlp++ = INTERPOLATE_BLEND(table, a, b);
    }
}

/**
 * @brief Interpolates in both the X and Y axis.
 */
void Asm_Interpolate_Line_Interpolate(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* buff_offset1 = InterpolationTable->TopLine;
    unsigned char* buff_offset2 = InterpolationTable->BottomLine;

    int pitch = dst_pitch / 2;
    Interpolate_X_Axis(src, buff_offset1, src_width);

    unsigned char* tmp = buff_offset1;
    buff_offset1 = buff_offset2;
    buff_offset2 = tmp;

    unsigned char* current_line = (unsigned char*)(src) + src_width;
    int lines = src_height - 1;

    while (lines--) {
        Interpolate_X_Axis(current_line, buff_offset1, src_width);
        Interpolate_Y_Axis(buff_offset2, buff_offset1, InterpolationTable->LineBuffer, src_width);
        memcpy(dptr, buff_offset2, 2 * src_width);
        dptr += pitch;
        memcpy(dptr, InterpolationTable->LineBuffer, 2 * src_width);
        current_line += src_width;
        dptr += pitch;

        unsigned char* tmp = buff_offset1;
        buff_offset1 = buff_offset2;
        buff_offset2 = tmp;
    }

    memcpy(dptr, buff_offset2, 2 * src_width);
    dptr += pitch;
    // memcpy(dptr, buff_offset1, 2 * src_width);
}

void Asm_Interpolate_Line_Interpolate_Rows(
    void* src, void* dst, int src_height, int src_width, int dst_pitch, int first, int last)
{
    unsigned char* top = InterpolationTable->TopLine;
    unsigned char* bottom = InterpolationTable->BottomLine;
    unsigned char* sptr = (unsigned char*)(src);
    int pitch = dst_pitch / 2;

    Interpolate_X_Axis(sptr + first * src_width, top, src_width);

    for (int i = first; i <= last; ++i) {
        unsigned char* dptr = (unsigned char*)(dst) + 2 * i * pitch;
        memcpy(dptr, top, 2 * src_width);

        // The row below is blended with the next source row; the last row has none.
        if (i + 1 < src_height) {
            Interpolate_X_Axis(sptr + (i + 1) * src_width, bottom, src_width);
            Interpolate_Y_Axis(top, bottom, InterpolationTable->LineBuffer, src_width);
            memcpy(dptr + pitch, InterpolationTable->LineBuffer, 2 * src_width);

            unsigned char* tmp = top;
            top = bottom;
            bottom = tmp;
        }
    }
}

void Asm_Pixel_Double(void* src, void* dst, int src_height, int src_width, int dst_pitch)
{
    // Build each doubled line in a small buffer that stays in the cache, then copy it
    // to both destination rows in bulk.
    static unsigned char line[2 * 1024];
    unsigned char* sptr = (unsigned char*)(src);
    unsigned char* dptr = (unsigned char*)(dst);
    int pitch = dst_pitch / 2;

    if (src_width > 1024) {
        src_width = 1024;
    }

    while (src_height--) {
        for (int i = 0; i < src_width; ++i) {
            line[2 * i] = line[2 * i + 1] = sptr[i];
        }
        memcpy(dptr, line, 2 * src_width);
        memcpy(dptr + pitch, line, 2 * src_width);
        sptr += src_width;
        dptr += dst_pitch;
    }
}
