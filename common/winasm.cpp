#include "winasm.h"
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

static void Interpolate_X_Axis(void* src, void* dst, int src_width)
{
    unsigned char* dptr = (unsigned char*)(dst);
    unsigned char* sptr = (unsigned char*)(src);

    unsigned char* wptr = dptr;

    for (int i = 0; i < src_width - 1; ++i) {
        unsigned char a = sptr[0], b = sptr[1];
        *wptr++ = a;
        // Neighbours are often the same colour; skip the table (and a likely cache miss).
        *wptr++ = a == b ? a : InterpolationTable->PaletteInterpolationTable[a][b];
        ++sptr;
    }

    *wptr++ = *sptr;
    *wptr = 0;
}

static void Interpolate_Y_Axis(void* top_line, void* bottom_line, void* middle_line, int src_width)
{
    unsigned char* tlp = (unsigned char*)(top_line);
    unsigned char* mlp = (unsigned char*)(middle_line);
    unsigned char* blp = (unsigned char*)(bottom_line);
    int dst_width = 2 * src_width;

    for (int i = 0; i < dst_width; ++i) {
        unsigned char a = *tlp++, b = *blp++;
        *mlp++ = a == b ? a : InterpolationTable->PaletteInterpolationTable[a][b];
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
