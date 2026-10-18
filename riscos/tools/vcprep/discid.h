/*
** discid.h: tells which freeware Westwood CD image a file is, from a few KB of it: the
** ISO 9660 primary volume descriptor and the root directory. Each disc is known by a file
** in its root and that file's exact size; the volume IDs are kept only to show.
**
** Shared by the Vanilla Conquer RISC OS port (Tiberian Dawn and Red Alert: the game and
** vcprep) and the OpenTS RISC OS port (Tiberian Sun: the game and tsprep), so that every
** game recognises every disc and can say which game a wrong one is for. The twins are
** Vanilla Conquer's riscos/tools/vcprep/discid.h and OpenTS's port/riscos/tools/discid.h;
** keep the two the same.
**
** Only the directory is read. Whether the files themselves are whole is found when they
** are installed, each against its own CRC-32; a CRC of the whole image could be added to
** Disc_Types (image_crc) and checked here, at the cost of reading all of it.
**
** C, header-only (static functions), so it can be included by C and C++ alike.
*/
#ifndef DISCID_H
#define DISCID_H

#include <stdio.h>
#include <string.h>

struct DiscType
{
    const char* id;    /* short name, e.g. "td-gdi" */
    const char* name;  /* for messages: "the C&C95 GDI disc" */
    const char* app;   /* the application that installs it, or NULL for none */
    const char* key;   /* a file in the root of the disc ... */
    unsigned long size; /* ... and its size, which tells the disc */
    unsigned long image_crc; /* not used yet (0): see above */
};

static const struct DiscType Disc_Types[] = {
    {"td-gdi", "the C&C95 GDI disc", "!VanillaTD", "GENERAL.MIX", 2584388UL, 0},
    {"td-nod", "the C&C95 Nod disc", "!VanillaTD", "GENERAL.MIX", 2549589UL, 0},
    {"td-dos-gdi", "the MS-DOS C&C GDI disc", NULL, "GENERAL.MIX", 3409175UL, 0},
    {"td-dos-nod", "the MS-DOS C&C Nod disc", NULL, "GENERAL.MIX", 3374373UL, 0},
    {"ra-allied", "the Red Alert Allied disc", "!VanillaRA", "MAIN.MIX", 454605294UL, 0},
    {"ra-soviet", "the Red Alert Soviet disc", "!VanillaRA", "MAIN.MIX", 500577414UL, 0},
    {"ts-gdi", "the Tiberian Sun GDI disc", "!OpenTS", "MAPS01.MIX", 3668236UL, 0},
    {"ts-nod", "the Tiberian Sun Nod disc", "!OpenTS", "MAPS02.MIX", 3503740UL, 0},
    {"ts-firestorm", "the Tiberian Sun Firestorm disc", "!OpenTS", "MAPS03.MIX", 3714132UL, 0},
};
#define DISC_TYPES ((int)(sizeof(Disc_Types) / sizeof(Disc_Types[0])))

/* Disc_Identify's results that aren't an index into Disc_Types. */
#define DISC_UNKNOWN (-1)   /* a CD image, but not one of these discs */
#define DISC_NOT_ISO (-2)   /* not a CD image (or can't be read) */

static unsigned long Disc_Le32(const unsigned char* p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* Reads bytes at a byte offset; 0 if they aren't all there. */
static int Disc_Read(FILE* f, unsigned long offset, unsigned char* buf, size_t len)
{
    return fseek(f, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

/*
** Which disc the file at path is: an index into Disc_Types, DISC_UNKNOWN or DISC_NOT_ISO.
** *truncated is set if the image is shorter than the volume it holds, or than the file that
** told the disc (a download or copy that didn't finish). volume, if not NULL, gets the
** volume ID (up to 32 characters).
*/
static int Disc_Identify(const char* path, int* truncated, char* volume, size_t volume_len)
{
    unsigned char pvd[2048];
    unsigned char dir[8192];
    unsigned long blocks, root_lba, root_len, file_size, off;
    int i, found = DISC_UNKNOWN, cut = 0;
    FILE* f = fopen(path, "rb");
    *truncated = 0;
    if (volume != NULL && volume_len > 0) {
        volume[0] = '\0';
    }
    if (f == NULL) {
        return DISC_NOT_ISO;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (long)(file_size = (unsigned long)ftell(f)) < 0
        || !Disc_Read(f, 16UL * 2048, pvd, sizeof(pvd))) {
        fclose(f);
        return DISC_NOT_ISO;
    }
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        fclose(f);
        return DISC_NOT_ISO;
    }
    if (volume != NULL && volume_len > 0) {
        size_t n = 32 < volume_len - 1 ? 32 : volume_len - 1;
        memcpy(volume, pvd + 40, n);
        volume[n] = '\0';
        while (n > 0 && volume[n - 1] == ' ') {
            volume[--n] = '\0';
        }
    }
    blocks = Disc_Le32(pvd + 80);
    root_lba = Disc_Le32(pvd + 156 + 2);
    root_len = Disc_Le32(pvd + 156 + 10);
    if (blocks * 2048UL > file_size) {
        cut = 1; /* the image holds less than the volume says it has */
    }
    if (root_len > sizeof(dir)) {
        root_len = sizeof(dir);
    }
    if (!Disc_Read(f, root_lba * 2048UL, dir, root_len)) {
        fclose(f);
        *truncated = 1;
        return DISC_UNKNOWN;
    }
    for (off = 0; off < root_len && found == DISC_UNKNOWN;) {
        unsigned len = dir[off], name_len;
        char name[40];
        size_t n;
        if (len == 0) { /* records don't cross sectors: on to the next */
            off = (off / 2048 + 1) * 2048;
            continue;
        }
        if (off + len > root_len || len < 34) {
            break;
        }
        name_len = dir[off + 32];
        n = name_len < sizeof(name) - 1 ? name_len : sizeof(name) - 1;
        memcpy(name, dir + off + 33, n);
        name[n] = '\0';
        if (strchr(name, ';') != NULL) {
            *strchr(name, ';') = '\0';
        }
        for (i = 0; i < DISC_TYPES; ++i) {
            if (strcmp(name, Disc_Types[i].key) == 0 && Disc_Le32(dir + off + 10) == Disc_Types[i].size) {
                unsigned long end = Disc_Le32(dir + off + 2) * 2048UL + Disc_Types[i].size;
                found = i;
                if (end > file_size) {
                    cut = 1;
                }
                break;
            }
        }
        off += len;
    }
    fclose(f);
    *truncated = cut;
    return found;
}

#endif
