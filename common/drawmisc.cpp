#include "drawmisc.h"
#include "gbuffer.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
HWND MainWindow; // Handle to programs main window
#endif

unsigned char CurrentPalette[768] = {255};
unsigned char PaletteTable[1024] = {0};

bool OverlappedVideoBlits = true; // Can video driver blit overlapped regions?
bool AllowHardwareBlitFills = true;

/*
** Function to call if we detect focus loss
*/
void (*Misc_Focus_Loss_Function)(void) = nullptr;
void (*Misc_Focus_Restore_Function)(void) = nullptr;

void Fat_Put_Pixel(int x, int y, int value, int size, GraphicViewPortClass& gvp)
{
    char* buf;
    int w;
    int pitch;

    if (size == 0) {
        return;
    }

    if ((unsigned)y >= (unsigned)gvp.Get_Height() || (unsigned)x >= (unsigned)gvp.Get_Width()) {
        return;
    }

    Mark_Written(gvp, x, y, size, size);
    buf = reinterpret_cast<char*>(x + (gvp.Get_Pitch() + gvp.Get_XAdd() + gvp.Get_Width()) * y + gvp.Get_Offset());
    w = size;
    pitch = gvp.Get_Pitch() + gvp.Get_XAdd() + gvp.Get_Width();

    while (size--) {
        memset(buf, value, w);
        buf += pitch;
    }
}

bool Radar_Tile_Blip(GraphicViewPortClass& gvp, const unsigned char* icon, int x, int y, int size)
{
    if (size <= 0 || x < 0 || y < 0 || x + size > gvp.Get_Width() || y + size > gvp.Get_Height()) {
        return false;
    }
    Mark_Written(gvp, x, y, size, size);
    const int ratio = ((24 << 16) / size) + 1; // as Linear_Scale_To_Linear's
    const int pitch = gvp.Get_Pitch() + gvp.Get_XAdd() + gvp.Get_Width();
    unsigned char* dst = reinterpret_cast<unsigned char*>(gvp.Get_Offset()) + y * pitch + x;
    for (int i = 0; i < size; ++i, dst += pitch) {
        const unsigned char* src = icon + ((i * ratio) >> 16) * 24;
        int xrat = 0;
        for (int j = 0; j < size; ++j, xrat += ratio) {
            unsigned char c = src[xrat >> 16];
            if (c != 0) {
                dst[j] = c;
            }
        }
    }
    return true;
}
