/*
** The low resolution (320x200, DOSMode) interface from the Windows 95 data.
**
** C&C95's LOCAL.MIX is an empty placeholder: it has none of the 320x200
** interface (sidebar strips, build icons, tabs, power bar, radar frame). Their high
** resolution versions are there, at exactly twice the size, so when one of those
** is asked for and isn't found, make it here: decode each frame of the high
** resolution shape (H<name>, or <x>ICNH.<ext> for a build icon <x>ICON.<ext>, which
** C&C95 has for each theater, .TEM, .WIN and .DES), halve
** it, and keep the result as a shape of LCW keyframes. The game world's art is
** the same in both resolutions, so only the interface is involved.
**
** Halving takes, for each 2x2 block, its most common colour, transparency (0)
** included, and the top left one on a tie.
**
** C&C95's 6POINT.FNT and GRAD6FNT.FNT are high resolution too (16 rows against the
** DOS 8): low resolution versions are made from 8POINT.FNT (see Low_Res_Font).
*/
#include "function.h"
#include "common/endianness.h"
#include "common/keyframe.h"

#include <ctype.h>
#include <map>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

#pragma pack(push, 1)
struct ShapeHeader
{
    uint16_t Frames, X, Y, Width, Height, LargestFrame;
    int16_t Flags;
};
#pragma pack(pop)

// Name -> the shape made for it. Never freed: the game keeps pointers to them.
std::map<std::string, std::vector<char>>* Made = nullptr;

bool High_Res_Name(const char* name, std::string& out)
{
    std::string n(name);
    for (size_t i = 0; i < n.size(); ++i) {
        n[i] = char(toupper((unsigned char)n[i]));
    }
    size_t dot = n.rfind('.');
    if (dot != std::string::npos && dot >= 4 && n.compare(dot - 4, 4, "ICON") == 0) {
        out = n.substr(0, dot - 4) + "ICNH" + n.substr(dot); // build icons: E1ICON.TEM -> E1ICNH.TEM
        return true;
    }
    static const char* const interface[] = {"STRIP.SHP",
                                            "STRIPUP.SHP",
                                            "STRIPDN.SHP",
                                            "SIDE1.SHP",
                                            "SIDE2.SHP",
                                            "SIDE3.SHP",
                                            "TABS.SHP",
                                            "PWRBAR.SHP",
                                            "CLOCK.SHP",
                                            "PIPS.SHP",
                                            "PIPS_F.SHP",
                                            "PIPS_G.SHP",
                                            "RADAR.GDI",
                                            "RADAR.NOD",
                                            "RADAR.JP",
                                            "REPAIR.SHP",
                                            "SELL.SHP",
                                            "MAP.SHP",
                                            "POWER.SHP"};
    for (size_t i = 0; i < sizeof(interface) / sizeof(interface[0]); ++i) {
        if (n == interface[i]) {
            out = "H" + n;
            return true;
        }
    }
    return false;
}

unsigned char Most_Common(unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{
    unsigned char v[4] = {a, b, c, d};
    int best = 0, best_count = 0;
    for (int i = 0; i < 4; ++i) {
        int count = 0;
        for (int j = 0; j < 4; ++j) {
            count += v[j] == v[i];
        }
        if (count > best_count) {
            best = i;
            best_count = count;
        }
    }
    return v[best];
}

/*
** LCW data that only uses "copy the next n bytes" commands (0x80 | n, n up to 63),
** ending with 0x80: always valid, and only 1.6% bigger than the data. LCW_Comp's
** output didn't always come back the same through LCW_Uncompress.
*/
std::vector<char> LCW_Literal(const unsigned char* data, size_t size)
{
    std::vector<char> out;
    out.reserve(size + size / 63 + 2);
    for (size_t at = 0; at < size;) {
        size_t n = size - at < 63 ? size - at : 63;
        out.push_back(char(0x80 | n));
        out.insert(out.end(), data + at, data + at + n);
        at += n;
    }
    out.push_back(char(0x80));
    return out;
}

void const* Make_Low_Res(char const* filename)
{
    if (Made == nullptr) {
        Made = new std::map<std::string, std::vector<char>>;
    }
    auto found = Made->find(filename);
    if (found != Made->end()) {
        return &found->second[0];
    }
    std::string high_name;
    if (!High_Res_Name(filename, high_name)) {
        return nullptr;
    }
    /*
    ** From a cached mixfile if it's in one, else read it (the build icons are in
    ** mixfiles that are only cached in high resolution). No recursion meanwhile.
    */
    Mix_Missing_Hook = nullptr;
    void const* high = MFCD::Retrieve(high_name.c_str());
    void* loaded = nullptr;
    if (high == nullptr) {
        CCFileClass file(high_name.c_str());
        if (file.Is_Available()) {
            high = loaded = Load_Alloc_Data(file);
        }
    }
    Mix_Missing_Hook = Make_Low_Res;
    if (high == nullptr) {
        return nullptr;
    }

    int frames = Get_Build_Frame_Count(high);
    int w = Get_Build_Frame_Width(high), h = Get_Build_Frame_Height(high);
    int w2 = (w + 1) / 2, h2 = (h + 1) / 2;
    if (frames <= 0 || w <= 0 || h <= 0) {
        if (loaded != nullptr) {
            Free(loaded);
        }
        return nullptr;
    }

    std::vector<unsigned char> big(size_t(w) * h), small(size_t(w2) * h2);
    std::vector<std::vector<char>> packed(frames);
    unsigned saved = UseBigShapeBuffer; // so that Build_Frame fills our buffer
    UseBigShapeBuffer = false;
    for (int f = 0; f < frames; ++f) {
        memset(&big[0], 0, big.size());
        Build_Frame(high, f, &big[0]);
        for (int y = 0; y < h2; ++y) {
            int y0 = 2 * y, y1 = 2 * y + 1 < h ? 2 * y + 1 : 2 * y;
            for (int x = 0; x < w2; ++x) {
                int x0 = 2 * x, x1 = 2 * x + 1 < w ? 2 * x + 1 : 2 * x;
                small[y * w2 + x] = Most_Common(big[y0 * w + x0], big[y0 * w + x1], big[y1 * w + x0], big[y1 * w + x1]);
            }
        }
        packed[f] = LCW_Literal(&small[0], small.size());
    }
    UseBigShapeBuffer = saved;
    if (loaded != nullptr) {
        Free(loaded);
    }

    // The header, then (frames + 2) pairs of offsets, then the frames.
    size_t table = sizeof(ShapeHeader) + size_t(frames + 2) * 8;
    size_t total = table;
    for (int f = 0; f < frames; ++f) {
        total += packed[f].size();
    }
    std::vector<char>& out = (*Made)[filename];
    out.assign(total, 0);
    ShapeHeader header = {htole16(uint16_t(frames)),
                          0,
                          0,
                          htole16(uint16_t(w2)),
                          htole16(uint16_t(h2)),
                          htole16(uint16_t(w2 * h2)),
                          0};
    memcpy(&out[0], &header, sizeof(header));
    uint32_t at = uint32_t(table);
    for (int f = 0; f <= frames; ++f) {
        uint32_t entry[2] = {htole32(at | (f < frames ? uint32_t(KF_KEYFRAME) << 24 : 0)), 0};
        memcpy(&out[sizeof(ShapeHeader) + f * 8], entry, sizeof(entry));
        if (f < frames) {
            memcpy(&out[at], &packed[f][0], packed[f].size());
            at += uint32_t(packed[f].size());
        }
    }
    return &out[0];
}

#pragma pack(push, 1)
struct FontHeader
{
    uint16_t FontLength;
    uint8_t FontCompress, FontDataBlocks;
    uint16_t InfoBlockOffset, OffsetBlockOffset, WidthBlockOffset, DataBlockOffset, HeightOffset, UnknownConst;
    uint8_t Pad, CharCount, MaxHeight, MaxWidth;
};
#pragma pack(pop)


} // namespace

/*
** The low resolution 6 point fonts, made from 8POINT.FNT (the same in both releases)
** when C&C95 only has high resolution ones. Its 11 rows become 8 by dropping rows 3,
** 6 and 9: that turns its capitals into the DOS 6 point ones almost exactly (R is
** identical). For the gradient font, the face colour (1) becomes the gradient's
** colours, 0xB to 0xF down the rows, as in the DOS GRAD6FNT.FNT.
*/
void const* Low_Res_Font(void const* font, void const* font8, bool gradient)
{
    if (font == nullptr || font8 == nullptr) {
        return font;
    }
    FontHeader h, h8;
    memcpy(&h, font, sizeof(h));
    memcpy(&h8, font8, sizeof(h8));
    if (h.MaxHeight <= 8 || h8.MaxHeight != 11) {
        return font; // already a low resolution font, or not an 8POINT we know
    }
    const unsigned char* src = static_cast<const unsigned char*>(font8);
    int chars = h8.CharCount + 1;
    int offsets = le16toh(h8.OffsetBlockOffset), widths = le16toh(h8.WidthBlockOffset), heights = le16toh(h8.HeightOffset);
    auto dropped = [](int row) { return row == 3 || row == 6 || row == 9; };
    auto kept_before = [&dropped](int row) {
        int n = 0;
        for (int r = 0; r < row; ++r) {
            n += !dropped(r);
        }
        return n;
    };

    std::vector<unsigned char> out(sizeof(FontHeader) + chars * 5);
    FontHeader nh = h8;
    // InfoBlockOffset stays: it points into the header itself, at MaxHeight and MaxWidth (Set_Font).
    nh.OffsetBlockOffset = htole16(uint16_t(sizeof(FontHeader)));
    nh.WidthBlockOffset = htole16(uint16_t(sizeof(FontHeader) + chars * 2));
    nh.HeightOffset = htole16(uint16_t(sizeof(FontHeader) + chars * 3));
    nh.DataBlockOffset = htole16(uint16_t(sizeof(FontHeader) + chars * 5));
    nh.MaxHeight = 8;
    for (int c = 0; c < chars; ++c) {
        uint16_t data_at, lle;
        memcpy(&data_at, src + offsets + c * 2, 2);
        memcpy(&lle, src + heights + c * 2, 2);
        data_at = le16toh(data_at);
        lle = le16toh(lle);
        int w = src[widths + c], ypos = lle & 0xFF, lines = lle >> 8, pitch = (w + 1) / 2;
        int top = kept_before(ypos), lines2 = kept_before(ypos + lines) - top;

        uint16_t at = htole16(uint16_t(out.size()));
        uint16_t nlle = htole16(uint16_t(top | (lines2 << 8)));
        memcpy(&out[sizeof(FontHeader) + c * 2], &at, 2);
        out[sizeof(FontHeader) + chars * 2 + c] = uint8_t(w);
        memcpy(&out[sizeof(FontHeader) + chars * 3 + c * 2], &nlle, 2);
        int row_out = 0;
        for (int y = 0; y < lines; ++y) {
            if (dropped(ypos + y)) {
                continue;
            }
            const unsigned char* row = src + data_at + y * pitch;
            for (int x = 0; x < pitch; ++x) {
                unsigned char b = row[x];
                if (gradient) {
                    int shade = 0xB + (top + row_out - 1 < 0 ? 0 : top + row_out - 1);
                    shade = shade > 0xF ? 0xF : shade;
                    if ((b & 0x0F) == 1) {
                        b = (b & 0xF0) | shade;
                    }
                    if ((b >> 4) == 1) {
                        b = (b & 0x0F) | (shade << 4);
                    }
                }
                out.push_back(b);
            }
            ++row_out;
        }
    }
    nh.FontLength = htole16(uint16_t(out.size()));
    memcpy(&out[0], &nh, sizeof(nh));
    void* made = new unsigned char[out.size()];
    memcpy(made, &out[0], out.size());
    return made;
}

// At start up: in low resolution, make missing interface shapes (above).
void Init_Low_Res_Interface()
{
    if (Get_Resolution_Factor() == 0) {
        Mix_Missing_Hook = Make_Low_Res;
    }
}
