// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection
#include "vqapalette.h"
#include "interpal.h"
#include "palette.h"
#include "winasm.h"
#include <string.h>

uint8_t* VQPalette;
int VQNumBytes;
bool VQSlowpal;
bool VQPaletteChange = false;

/*
** Movies are scaled up with a precomputed interpolation table per palette
** change, selected by PaletteCounter in the order the changes occur. Changes
** that are never applied (skipped frames, or a newer change flagged before the
** old one was set) still have to advance the counter, or every later frame is
** interpolated with the wrong table.
*/
static unsigned VQPalettesSkipped = 0;

void VQA_Palette_Skipped()
{
    ++VQPalettesSkipped;
}

void VQA_Reset_Palette_Tracking()
{
    VQPalettesSkipped = 0;
}

extern unsigned char* InterpolatedPalettes[100];
extern bool PalettesRead;
extern unsigned PaletteCounter;

/**
 * Flags a VQA Palette change.
 */
void VQA_Flag_To_Set_Palette(uint8_t* palette, int numbytes, bool slowpal)
{
    if (VQPaletteChange) {
        VQA_Palette_Skipped(); // The pending change is replaced before it was applied.
    }
    VQPalette = palette;
    VQNumBytes = numbytes;
    VQSlowpal = slowpal;
    VQPaletteChange = true;
}

/**
 * Changes the VQA Palette.
 */
void VQA_SetPalette(uint8_t* palette, int numbytes, bool slowpal)
{
    for (int i = 0; i < 768; ++i) {
        palette[i] &= 0x3Fu;
    }

    Increase_Palette_Luminance(palette, 15, 15, 15, 63);

    PaletteCounter += VQPalettesSkipped;
    VQPalettesSkipped = 0;

    if (PalettesRead && InterpolationTable) {
        /*
        ** A movie without its own .VQP gets another movie's (Red Alert uses AAGUN's),
        ** which can have fewer palettes than it has palette changes. Past the last one,
        ** keep the table already in use rather than copying from a null pointer (Red
        ** Alert's ENGLISH movie crashed here).
        */
        if (PaletteCounter < sizeof(InterpolatedPalettes) / sizeof(InterpolatedPalettes[0])
            && InterpolatedPalettes[PaletteCounter] != nullptr) {
            void* paletteinterpol = (void*)&InterpolationTable->PaletteInterpolationTable;
            size_t table_size = sizeof(InterpolationTable->PaletteInterpolationTable);
            memcpy(paletteinterpol, InterpolatedPalettes[PaletteCounter], table_size);
        }
        ++PaletteCounter;
    }

    Set_Palette(palette);
}

/**
 * Changes the VQA Palette after a call to VQA_Flag_To_Set_Palette.
 */
void Check_VQ_Palette_Set()
{
    if (VQPaletteChange) {
        VQA_SetPalette(VQPalette, VQNumBytes, VQSlowpal);
        VQPaletteChange = false;
    }
}
