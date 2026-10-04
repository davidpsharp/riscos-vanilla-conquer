/*
** ramix: list Red Alert mixfiles, and copy one leaving entries out.
**
**   ramix list <mixfile>
**   ramix copy <mixfile> <new mixfile> [name to leave out ...]
**   ramix extract <mixfile> <name> <file>
**
** Red Alert's MAIN.MIX (455-500 MB) is mostly its movies (MOVIES1.MIX or
** MOVIES2.MIX inside it); without them it is a few tens of MB. copy writes a plain
** (unencrypted) mixfile, which the game reads as well as the encrypted kind. Names
** are matched the way the game does (uppercase, CRCEngine). Entries whose names
** aren't known are listed by their hash.
*/
#include "common/crc.h"
#include "common/ini.h"
#include "common/pk.h"
#include "common/pkstraw.h"
#include "common/ramfile.h"
#include "common/rawfile.h"
#include "common/rndstraw.h"
#include "common/xstraw.h"
#include "common/endianness.h"

#include <algorithm>
#include <ctype.h>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// Red Alert's public key (redalert/const.cpp), which decrypts mixfile headers.
static const char Keys[] = "[PublicKey]\n"
                           "1=AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V\n"
                           "\n";

struct Entry
{
    int32_t CRC;
    uint32_t Offset, Size;
};

static int32_t Name_CRC(const char* name)
{
    std::string upper(name);
    for (size_t i = 0; i < upper.size(); ++i) {
        upper[i] = char(toupper((unsigned char)upper[i]));
    }
    return Calculate_CRC<CRCEngine>(upper.c_str(), unsigned(upper.size()));
}

// Names that may be in Red Alert's mixfiles, to show in listings.
static const char* const Known[] = {"CONQUER.MIX", "GENERAL.MIX", "MOVIES1.MIX", "MOVIES2.MIX", "SCORES.MIX",
                                    "SOUNDS.MIX",  "RUSSIAN.MIX", "ALLIES.MIX",  "INTERIOR.MIX", "SNOW.MIX",
                                    "TEMPERAT.MIX", "LOCAL.MIX",  "HIRES.MIX",   "LORES.MIX",   "NCHIRES.MIX",
                                    "SPEECH.MIX",  "EXPAND.MIX",  "EXPAND2.MIX", "HIRES1.MIX",  "REDALERT.INI",
                                    "RULES.INI",   "AUD.MIX",     "SETUP.MIX",   "DESEICNH.MIX", "TEMPICNH.MIX"};

static bool Read_Index(const char* path, std::vector<Entry>& entries, uint32_t& data_start)
{
    RawFileClass file(path);
    if (!file.Is_Available()) {
        fprintf(stderr, "ramix: can't open %s\n", path);
        return false;
    }
    RAMFileClass keyfile((void*)Keys, int(strlen(Keys)));
    INIClass ini;
    ini.Load(keyfile);
    PKey key = ini.Get_PKey(true);

    FileStraw fstraw(file);
    RandomStraw fakernd;
    PKStraw pstraw(PKStraw::DECRYPT, fakernd);
    Straw* straw = &fstraw;

    int16_t alternate[2];
    straw->Get(alternate, sizeof(alternate));
    unsigned char header[6];
    if (le16toh(alternate[0]) == 0) {
        if (le16toh(alternate[1]) & 2) { // encrypted
            pstraw.Key(&key);
            pstraw.Get_From(&fstraw);
            straw = &pstraw;
        }
        straw->Get(header, sizeof(header));
    } else {
        memcpy(header, alternate, sizeof(alternate));
        straw->Get(header + sizeof(alternate), sizeof(header) - sizeof(alternate));
    }
    uint16_t count = uint16_t(header[0] | header[1] << 8);
    entries.resize(count);
    for (int i = 0; i < count; ++i) {
        uint32_t raw[3];
        straw->Get(raw, sizeof(raw));
        entries[i].CRC = int32_t(le32toh(raw[0]));
        entries[i].Offset = le32toh(raw[1]);
        entries[i].Size = le32toh(raw[2]);
    }
    data_start = uint32_t(file.Seek(0, SEEK_CUR));
    return true;
}

int main(int argc, char** argv)
{
    bool list = argc >= 3 && strcmp(argv[1], "list") == 0;
    bool copy = argc >= 4 && strcmp(argv[1], "copy") == 0;
    bool extract = argc == 5 && strcmp(argv[1], "extract") == 0;
    if (!list && !copy && !extract) {
        fprintf(stderr,
                "usage: ramix list <mixfile>\n       ramix copy <mixfile> <new> [name to leave out ...]\n"
                "       ramix extract <mixfile> <name> <file>\n");
        return 2;
    }
    std::vector<Entry> entries;
    uint32_t data_start;
    if (!Read_Index(argv[2], entries, data_start)) {
        return 1;
    }
    std::map<int32_t, std::string> names;
    for (size_t i = 0; i < sizeof(Known) / sizeof(Known[0]); ++i) {
        names[Name_CRC(Known[i])] = Known[i];
    }

    if (strcmp(argv[1], "list") == 0) {
        uint64_t total = 0;
        for (size_t i = 0; i < entries.size(); ++i) {
            auto n = names.find(entries[i].CRC);
            printf("%10u  %s\n", entries[i].Size, n != names.end() ? n->second.c_str() : "?");
            if (n == names.end()) {
                printf("            (hash %08x)\n", unsigned(entries[i].CRC));
            }
            total += entries[i].Size;
        }
        printf("%10llu  in %u entries\n", (unsigned long long)total, unsigned(entries.size()));
        return 0;
    }

    if (extract) {
        int32_t want = Name_CRC(argv[3]);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].CRC != want) {
                continue;
            }
            FILE* in = fopen(argv[2], "rb");
            FILE* out = fopen(argv[4], "wb");
            if (in == nullptr || out == nullptr) {
                fprintf(stderr, "ramix: can't open %s\n", in == nullptr ? argv[2] : argv[4]);
                return 1;
            }
            std::vector<char> data(entries[i].Size);
            fseek(in, long(data_start + entries[i].Offset), SEEK_SET);
            bool ok = fread(&data[0], 1, data.size(), in) == data.size() && fwrite(&data[0], 1, data.size(), out) == data.size();
            fclose(in);
            ok = fclose(out) == 0 && ok;
            if (!ok) {
                fprintf(stderr, "ramix: read or write failed\n");
                return 1;
            }
            return 0;
        }
        fprintf(stderr, "ramix: %s isn't in %s\n", argv[3], argv[2]);
        return 1;
    }

    // copy: keep the entries not named on the command line, in the same (sorted) order.
    std::vector<int32_t> drop;
    for (int i = 4; i < argc; ++i) {
        drop.push_back(Name_CRC(argv[i]));
    }
    std::vector<Entry> kept;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (std::find(drop.begin(), drop.end(), entries[i].CRC) == drop.end()) {
            kept.push_back(entries[i]);
        }
    }
    FILE* in = fopen(argv[2], "rb");
    FILE* out = fopen(argv[3], "wb");
    if (in == nullptr || out == nullptr) {
        fprintf(stderr, "ramix: can't open %s\n", in == nullptr ? argv[2] : argv[3]);
        return 1;
    }
    uint32_t size = 0;
    for (size_t i = 0; i < kept.size(); ++i) {
        size += kept[i].Size;
    }
    unsigned char header[6] = {uint8_t(kept.size()), uint8_t(kept.size() >> 8), uint8_t(size), uint8_t(size >> 8),
                               uint8_t(size >> 16), uint8_t(size >> 24)};
    fwrite(header, 1, sizeof(header), out);
    uint32_t at = 0;
    for (size_t i = 0; i < kept.size(); ++i) {
        uint32_t raw[3] = {htole32(uint32_t(kept[i].CRC)), htole32(at), htole32(kept[i].Size)};
        fwrite(raw, 1, sizeof(raw), out);
        at += kept[i].Size;
    }
    std::vector<char> buffer(1 << 20);
    for (size_t i = 0; i < kept.size(); ++i) {
        fseek(in, long(data_start + kept[i].Offset), SEEK_SET);
        for (uint32_t left = kept[i].Size; left > 0;) {
            size_t n = left < buffer.size() ? left : buffer.size();
            if (fread(&buffer[0], 1, n, in) != n || fwrite(&buffer[0], 1, n, out) != n) {
                fprintf(stderr, "ramix: read or write failed\n");
                return 1;
            }
            left -= uint32_t(n);
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        fprintf(stderr, "ramix: write failed\n");
        return 1;
    }
    printf("%s: %u of %u entries, %u bytes\n", argv[3], unsigned(kept.size()), unsigned(entries.size()), size);
    return 0;
}
