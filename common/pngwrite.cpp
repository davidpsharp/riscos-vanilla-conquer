#include "pngwrite.h"

#include <stddef.h>

namespace
{
/*
** CRC-32 (the PNG polynomial) a byte at a time, from a 256 entry table made on first use.
*/
unsigned int Crc_Table[256];
bool Crc_Ready = false;

void Make_Crc_Table()
{
    for (unsigned int n = 0; n < 256; ++n) {
        unsigned int c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        Crc_Table[n] = c;
    }
    Crc_Ready = true;
}

unsigned int Crc_Update(unsigned int crc, const unsigned char* p, size_t n)
{
    while (n-- > 0) {
        crc = Crc_Table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

/*
** Output with a running CRC for the chunk being written, buffered so that the many small
** pieces don't each cost an fwrite.
*/
class Sink
{
public:
    explicit Sink(FILE* f)
        : fp(f)
        , used(0)
        , crc(0)
        , ok(true)
    {
    }

    void Put(const unsigned char* p, size_t n)
    {
        crc = Crc_Update(crc, p, n);
        while (n > 0) {
            size_t room = sizeof(buf) - used;
            size_t take = n < room ? n : room;
            for (size_t i = 0; i < take; ++i) {
                buf[used + i] = p[i];
            }
            used += take;
            p += take;
            n -= take;
            if (used == sizeof(buf)) {
                Flush();
            }
        }
    }

    void Put_Byte(unsigned int b)
    {
        unsigned char c = (unsigned char)b;
        Put(&c, 1);
    }

    void Flush()
    {
        if (used > 0) {
            if (fwrite(buf, 1, used, fp) != used) {
                ok = false;
            }
            used = 0;
        }
    }

    // Big endian 32 bit value (everything in the PNG headers).
    void Put_U32(unsigned int v)
    {
        unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v};
        Put(b, 4);
    }

    // Starts a chunk: its length, then (in the CRC) its type.
    void Begin_Chunk(unsigned int length, const char* type)
    {
        Put_U32(length);
        crc = 0xFFFFFFFFu;
        Put((const unsigned char*)type, 4);
    }

    void End_Chunk()
    {
        unsigned int value = ~crc;
        Put_U32(value); // (this also runs the CRC over itself, which nothing then reads)
    }

    bool Finish()
    {
        Flush();
        return ok && fflush(fp) == 0;
    }

private:
    FILE* fp;
    unsigned char buf[4096];
    size_t used;
    unsigned int crc;
    bool ok;
};

// Little endian 16 bit, for the stored block headers.
void Put_U16_LE(Sink& s, unsigned int v)
{
    s.Put_Byte(v & 0xFF);
    s.Put_Byte((v >> 8) & 0xFF);
}
} // namespace

bool PNG_Write_Paletted(FILE* fp, int w, int h, const unsigned char* pixels, int pitch, const unsigned char* palette)
{
    if (fp == nullptr || w <= 0 || h <= 0 || pixels == nullptr || palette == nullptr || pitch < w) {
        return false;
    }
    if (!Crc_Ready) {
        Make_Crc_Table();
    }

    // The raw image is each row preceded by a filter byte (0, none).
    unsigned long long raw_size_64 = (unsigned long long)h * ((unsigned long long)w + 1);
    if (raw_size_64 > 0x7FFF0000ull / 2) { // the chunk length must fit comfortably in 31 bits
        return false;
    }
    unsigned int raw_size = (unsigned int)raw_size_64;
    unsigned int blocks = (raw_size + 65534) / 65535;
    // zlib header (2) + a 5 byte header for every stored block + the data + Adler-32 (4).
    unsigned int idat_size = 2 + blocks * 5 + raw_size + 4;

    Sink s(fp);
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    s.Put(signature, 8);

    s.Begin_Chunk(13, "IHDR");
    s.Put_U32((unsigned int)w);
    s.Put_U32((unsigned int)h);
    s.Put_Byte(8); // bit depth
    s.Put_Byte(3); // colour type: palette
    s.Put_Byte(0); // compression
    s.Put_Byte(0); // filter
    s.Put_Byte(0); // no interlace
    s.End_Chunk();

    s.Begin_Chunk(768, "PLTE");
    s.Put(palette, 768);
    s.End_Chunk();

    s.Begin_Chunk(idat_size, "IDAT");
    s.Put_Byte(0x78); // zlib: deflate, 32K window
    s.Put_Byte(0x01); // no compression; (0x78 * 256 + 0x01) is a multiple of 31

    unsigned int a = 1, b = 0;   // Adler-32
    int row = 0;
    int col = -1;                // -1: the row's filter byte is next
    unsigned int left = raw_size;
    unsigned int pending = 0;    // bytes summed since the last reduction modulo 65521
    while (left > 0) {
        unsigned int n = left < 65535 ? left : 65535;
        s.Put_Byte(n == left ? 1 : 0); // BFINAL, BTYPE 00 (stored)
        Put_U16_LE(s, n);
        Put_U16_LE(s, ~n & 0xFFFF);
        left -= n;
        while (n > 0) {
            if (col < 0) {
                s.Put_Byte(0);
                b += a; // a is unchanged by a zero byte
                ++pending;
                col = 0;
                --n;
                continue;
            }
            unsigned int run = (unsigned int)(w - col);
            if (run > n) {
                run = n;
            }
            if (run > 2000) { // keeps the sums within 32 bits between reductions
                run = 2000;
            }
            const unsigned char* p = pixels + (size_t)row * (size_t)pitch + (size_t)col;
            s.Put(p, run);
            for (unsigned int i = 0; i < run; ++i) {
                a += p[i];
                b += a;
            }
            pending += run;
            if (pending >= 2000) {
                a %= 65521u;
                b %= 65521u;
                pending = 0;
            }
            col += (int)run;
            n -= run;
            if (col == w) {
                col = -1;
                ++row;
            }
        }
    }
    a %= 65521u;
    b %= 65521u;
    s.Put_U32((b << 16) | a);
    s.End_Chunk();

    s.Begin_Chunk(0, "IEND");
    s.End_Chunk();
    return s.Finish();
}
