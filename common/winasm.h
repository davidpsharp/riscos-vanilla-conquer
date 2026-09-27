#ifndef WINASM_H
#define WINASM_H

#define SIZE_OF_PALETTE 256

struct InterpolationTable
{
    unsigned char PaletteInterpolationTable[SIZE_OF_PALETTE][SIZE_OF_PALETTE];
    unsigned char LineBuffer[1600];
    unsigned char TopLine[1600];
    unsigned char BottomLine[1600];
};

extern struct InterpolationTable* InterpolationTable;

void Asm_Interpolate(void* src, void* dst, int src_height, int src_width, int dst_pitch);
void Asm_Interpolate_Line_Double(void* src, void* dst, int src_height, int src_width, int dst_pitch);
void Asm_Interpolate_Line_Interpolate(void* src, void* dst, int src_height, int src_width, int dst_pitch);
// As Asm_Interpolate_Line_Interpolate for source rows first..last only; src and dst are
// the whole buffers. Gives the same output rows as scaling the whole buffer.
void Asm_Interpolate_Line_Interpolate_Rows(
    void* src, void* dst, int src_height, int src_width, int dst_pitch, int first, int last);

#endif
