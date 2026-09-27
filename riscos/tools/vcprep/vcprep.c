/*
** vcprep: install Tiberian Dawn data into !VanillaTD from a C&C95 (C&C Gold)
** CD image, the freeware GDI or Nod disc. Reads the ISO directly (no CDFS
** needed), unpacks the files held in the InstallShield archive INSTALL/SETUP.Z
** and checks every file it writes against a known CRC-32.
**
** Usage: vcprep <iso image> <app dir> [-nomovies] [-force]
**   -nomovies  leave out MOVIES.MIX (about 430 MB per disc)
**   -force     rewrite files that are already present with the right size
**
** Run it once per disc: the shared files are identical on both, GENERAL and
** MOVIES go into the gdi or nod directory for the disc given.
**
** Paths are native to the host: RISC OS paths on RISC OS, where the layout is
** MIX.<name>, gdi.MIX.GENERAL etc. as the game's short filename scheme expects.
**
** Licensing: this file is part of the Vanilla Conquer RISC OS port and is under
** the GNU GPL v3, like the rest of the project. blast.c and blast.h (the PKWARE
** DCL decompressor) are Copyright (C) 2003, 2012, 2013 Mark Adler, under the
** zlib licence, and are included unmodified. The InstallShield 3 archive layout
** follows unshieldv3 by Wolfgang Frisch (Apache License 2.0), used as a format
** reference; no code was taken from it.
*/
#include "blast.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>
/* Take file names as native RISC OS paths, untranslated. */
int __riscosify_control = __RISCOSIFY_NO_PROCESS;
#define SEP "."
#else
#define SEP "/"
#endif

#define SECTOR 2048
#define FILETYPE_DATA 0xFFD
#define FILETYPE_TEXT 0xFFF

/* ---- CRC-32 (the zlib polynomial, so zlib.crc32 gives the same values) ---- */

static unsigned long Crc_Table[256];

static void Crc_Init(void)
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        }
        Crc_Table[n] = c;
    }
}

static unsigned long Crc_Update(unsigned long crc, const unsigned char* buf, size_t len)
{
    crc ^= 0xFFFFFFFFUL;
    while (len--) {
        crc = Crc_Table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ---- Known files ---- */

struct Known
{
    const char* name;  /* On the disc or in SETUP.Z. */
    const char* dest;  /* Relative to the app dir; "@" is replaced by gdi or nod. */
    unsigned long size;
    unsigned long crc;
};

/* Plain files in the root of the disc, identical on both discs. */
static const struct Known Disc_Files[] = {
    {"AUD.MIX", "MIX" SEP "AUD", 1890646, 0x930cf805UL},
    {"CONQUER.MIX", "MIX" SEP "CONQUER", 2039435, 0x1cf22830UL},
    {"DESERT.MIX", "MIX" SEP "DESERT", 677061, 0x1f2f49f4UL},
    {"SCORES.MIX", "MIX" SEP "SCORES", 39114329, 0xad4bd953UL},
    {"SOUNDS.MIX", "MIX" SEP "SOUNDS", 1228794, 0xe4e52377UL},
    {"TEMPERAT.MIX", "MIX" SEP "TEMPERAT", 639607, 0xa1112d4eUL},
    {"WINTER.MIX", "MIX" SEP "WINTER", 667385, 0x4af93813UL},
};

/* Files in INSTALL/SETUP.Z (under C&C95\), identical on both discs. */
static const struct Known Archive_Files[] = {
    {"CCLOCAL.MIX", "MIX" SEP "CCLOCAL", 121305, 0xcc71829fUL},
    {"DESEICNH.MIX", "MIX" SEP "DESEICNH", 120296, 0xfaab9715UL},
    {"LOCAL.MIX", "MIX" SEP "LOCAL", 4, 0x8bc21544UL},
    {"SPEECH.MIX", "MIX" SEP "SPEECH", 594297, 0x872fa2b3UL},
    {"TEMPICNH.MIX", "MIX" SEP "TEMPICNH", 119935, 0x1772c942UL},
    {"TRANSIT.MIX", "MIX" SEP "TRANSIT", 4105646, 0xce4d7722UL},
    {"UPDATE.MIX", "MIX" SEP "UPDATE", 10233756, 0x025b22c8UL},
    {"UPDATEC.MIX", "MIX" SEP "UPDATEC", 990901, 0x473893aeUL},
    {"WINTICNH.MIX", "MIX" SEP "WINTICNH", 119935, 0xceb5e4f4UL},
    {"CONQUER.INI", "INI" SEP "CONQUER", 310, 0x2fc12471UL},
};

/* The files that differ between the discs. */
struct Side
{
    const char* dir;
    struct Known general;
    struct Known movies;
};

static const struct Side Sides[] = {
    {"gdi",
     {"GENERAL.MIX", "@" SEP "MIX" SEP "GENERAL", 2584388, 0xce92c64fUL},
     {"MOVIES.MIX", "@" SEP "MIX" SEP "MOVIES", 449080410, 0xa907244fUL}},
    {"nod",
     {"GENERAL.MIX", "@" SEP "MIX" SEP "GENERAL", 2549589, 0xccee57e6UL},
     {"MOVIES.MIX", "@" SEP "MIX" SEP "MOVIES", 432535694, 0x7b65d73cUL}},
};

/* ---- Options and state ---- */

static FILE* Iso;
static const char* App_Dir;
static const char* Side_Dir;
static int Force = 0;
static int Problems = 0;
static unsigned char Buffer[256 * 1024];

static int Read_At(unsigned long offset, void* buf, size_t len)
{
    return fseek(Iso, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, Iso) == len;
}

static unsigned long Get32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned Get16(const unsigned char* p)
{
    return p[0] | (p[1] << 8);
}

/* ---- Output files ---- */

static void Dest_Path(const struct Known* k, char* out, size_t outlen)
{
    const char* d = k->dest;
    if (d[0] == '@') {
        snprintf(out, outlen, "%s" SEP "%s%s", App_Dir, Side_Dir, d + 1);
    } else {
        snprintf(out, outlen, "%s" SEP "%s", App_Dir, d);
    }
}

/* Creates each directory along the path of a file. */
static void Make_Dirs(const char* path)
{
    char dir[1024];
    const char* p = path + strlen(App_Dir) + 1;
    while ((p = strstr(p, SEP)) != NULL) {
        size_t len = (size_t)(p - path);
        if (len >= sizeof(dir)) {
            return;
        }
        memcpy(dir, path, len);
        dir[len] = '\0';
        mkdir(dir, 0755);
        p++;
    }
}

static void Set_Type(const char* path, int type)
{
#ifdef __riscos__
    _swix(OS_File, _INR(0, 2), 18, path, type);
#else
    (void)path;
    (void)type;
#endif
}

static long File_Size(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

/* Returns 1 to skip a file that is already there. */
static int Already_Present(const struct Known* k, const char* path)
{
    if (!Force && File_Size(path) == (long)k->size) {
        printf("  %-12s already present, skipped\n", k->name);
        return 1;
    }
    return 0;
}

static void Check(const struct Known* k, unsigned long size, unsigned long crc)
{
    if (size != k->size || crc != k->crc) {
        printf("  %-12s CHECKSUM MISMATCH: got %lu bytes crc %08lx, expected %lu bytes crc %08lx\n",
               k->name,
               size,
               crc,
               k->size,
               k->crc);
        ++Problems;
    } else {
        printf("  %-12s ok\n", k->name);
    }
}

/* ---- ISO 9660 ---- */

struct Entry
{
    unsigned long lba;
    unsigned long size;
    int is_dir;
};

/* Compares a directory record name ("NAME.EXT;1") with a plain name. */
static int Name_Matches(const unsigned char* rec_name, int len, const char* name)
{
    int i;
    for (i = 0; i < len && rec_name[i] != ';'; ++i) {
        char c = (char)rec_name[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (name[i] == '\0' || name[i] != c) {
            return 0;
        }
    }
    return name[i] == '\0';
}

static int Find_In_Dir(const struct Entry* dir, const char* name, struct Entry* out)
{
    unsigned long pos = 0;
    while (pos < dir->size) {
        unsigned long sector = pos / SECTOR;
        unsigned long end = (sector + 1) * SECTOR;
        unsigned char sec[SECTOR];
        unsigned long off;

        if (!Read_At((dir->lba + sector) * SECTOR, sec, SECTOR)) {
            return 0;
        }
        off = pos % SECTOR;
        while (off < SECTOR) {
            unsigned char* rec = sec + off;
            int len = rec[0];
            if (len == 0) {
                break; /* The rest of this sector is padding. */
            }
            if (Name_Matches(rec + 33, rec[32], name)) {
                out->lba = Get32(rec + 2);
                out->size = Get32(rec + 10);
                out->is_dir = (rec[25] & 2) != 0;
                return 1;
            }
            off += (unsigned long)len;
        }
        pos = end;
    }
    return 0;
}

static int Open_Root(struct Entry* root)
{
    unsigned char pvd[SECTOR];
    if (!Read_At(16 * SECTOR, pvd, SECTOR) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        return 0;
    }
    root->lba = Get32(pvd + 156 + 2);
    root->size = Get32(pvd + 156 + 10);
    root->is_dir = 1;
    return 1;
}

/* ---- Copying plain files ---- */

static void Copy_Plain(const struct Entry* root, const struct Known* k)
{
    char path[1024];
    struct Entry e;
    FILE* out;
    unsigned long left, crc = 0, done = 0, next_dot = 0;

    Dest_Path(k, path, sizeof(path));
    if (Already_Present(k, path)) {
        return;
    }
    if (!Find_In_Dir(root, k->name, &e) || e.is_dir) {
        printf("  %-12s NOT FOUND on this disc\n", k->name);
        ++Problems;
        return;
    }

    Make_Dirs(path);
    out = fopen(path, "wb");
    if (out == NULL) {
        printf("  %-12s can't create %s: %s\n", k->name, path, strerror(errno));
        ++Problems;
        return;
    }

    if (fseek(Iso, (long)(e.lba * SECTOR), SEEK_SET) != 0) {
        e.size = 0;
    }
    for (left = e.size; left > 0;) {
        size_t chunk = left < sizeof(Buffer) ? (size_t)left : sizeof(Buffer);
        if (fread(Buffer, 1, chunk, Iso) != chunk) {
            printf("  %-12s read error\n", k->name);
            break;
        }
        if (fwrite(Buffer, 1, chunk, out) != chunk) {
            printf("  %-12s write error (disc full?)\n", k->name);
            break;
        }
        crc = Crc_Update(crc, Buffer, chunk);
        left -= chunk;
        done += chunk;
        if (e.size > 20000000UL && done >= next_dot) {
            printf("  %-12s %lu%%\r", k->name, (unsigned long)(done / (e.size / 100 + 1)));
            fflush(stdout);
            next_dot = done + 10000000UL;
        }
    }
    fclose(out);
    Set_Type(path, FILETYPE_DATA);
    Check(k, done, crc);
}

/* ---- InstallShield 3 archive (SETUP.Z) ---- */

struct Blast_Io
{
    unsigned long in_left;
    FILE* out;
    unsigned long out_size;
    unsigned long crc;
    int write_error;
};

static unsigned Blast_In(void* how, unsigned char** buf)
{
    struct Blast_Io* io = (struct Blast_Io*)how;
    size_t chunk = io->in_left < sizeof(Buffer) ? (size_t)io->in_left : sizeof(Buffer);
    if (chunk == 0) {
        return 0;
    }
    chunk = fread(Buffer, 1, chunk, Iso);
    io->in_left -= chunk;
    *buf = Buffer;
    return (unsigned)chunk;
}

static int Blast_Out(void* how, unsigned char* buf, unsigned len)
{
    struct Blast_Io* io = (struct Blast_Io*)how;
    io->crc = Crc_Update(io->crc, buf, len);
    io->out_size += len;
    if (fwrite(buf, 1, len, io->out) != len) {
        io->write_error = 1;
        return 1;
    }
    return 0;
}

static void Extract_Archive(const struct Entry* root)
{
    struct Entry install, setup;
    unsigned char hdr[59];
    unsigned long base, pos;
    unsigned dirs, d, i;
    int found[sizeof(Archive_Files) / sizeof(Archive_Files[0])] = {0};
    size_t k;

    if (!Find_In_Dir(root, "INSTALL", &install) || !install.is_dir || !Find_In_Dir(&install, "SETUP.Z", &setup)) {
        printf("  INSTALL/SETUP.Z NOT FOUND on this disc\n");
        ++Problems;
        return;
    }
    base = setup.lba * SECTOR;
    if (!Read_At(base, hdr, sizeof(hdr)) || Get32(hdr) != 0x8C655D13UL || Get32(hdr + 4) != 0x02013AUL) {
        printf("  SETUP.Z is not an InstallShield 3 archive\n");
        ++Problems;
        return;
    }
    dirs = Get16(hdr + 49);
    pos = base + Get32(hdr + 41);

    /* Directory table: file count per directory. */
    {
        unsigned counts[64];
        unsigned char rec[6];
        if (dirs > 64) {
            dirs = 64;
        }
        for (d = 0; d < dirs; ++d) {
            if (!Read_At(pos, rec, sizeof(rec))) {
                return;
            }
            counts[d] = Get16(rec);
            pos += Get16(rec + 2);
        }

        /* File table. */
        for (d = 0; d < dirs; ++d) {
            for (i = 0; i < counts[d]; ++i) {
                unsigned char f[30 + 256];
                char name[256];
                unsigned long csize, offset;
                int attrib, namelen;

                if (!Read_At(pos, f, 30)) {
                    return;
                }
                csize = Get32(f + 7);
                offset = Get32(f + 11);
                attrib = f[25];
                namelen = f[29];
                if (!Read_At(pos + 30, name, (size_t)namelen)) {
                    return;
                }
                name[namelen] = '\0';
                pos += Get16(f + 23);

                for (k = 0; k < sizeof(Archive_Files) / sizeof(Archive_Files[0]); ++k) {
                    const struct Known* kn = &Archive_Files[k];
                    char path[1024];
                    struct Blast_Io io;
                    FILE* out;

                    if (found[k] || strcmp(name, kn->name) != 0) {
                        continue;
                    }
                    found[k] = 1;
                    Dest_Path(kn, path, sizeof(path));

                    /* Keep an existing INI: it holds the player's settings. */
                    if (strcmp(kn->name, "CONQUER.INI") == 0 && File_Size(path) >= 0) {
                        printf("  %-12s already present, kept\n", kn->name);
                        continue;
                    }
                    if (Already_Present(kn, path)) {
                        continue;
                    }

                    Make_Dirs(path);
                    out = fopen(path, "wb");
                    if (out == NULL) {
                        printf("  %-12s can't create %s: %s\n", kn->name, path, strerror(errno));
                        ++Problems;
                        continue;
                    }
                    memset(&io, 0, sizeof(io));
                    io.in_left = csize;
                    io.out = out;
                    fseek(Iso, (long)(base + offset), SEEK_SET);
                    if (attrib & 0x10) {
                        /* Stored uncompressed. */
                        unsigned char* buf;
                        unsigned n;
                        while ((n = Blast_In(&io, &buf)) > 0 && !Blast_Out(&io, buf, n)) {
                        }
                    } else if (blast(Blast_In, &io, Blast_Out, &io, NULL, NULL) != 0 && !io.write_error) {
                        printf("  %-12s decompression failed\n", kn->name);
                    }
                    if (io.write_error) {
                        printf("  %-12s write error (disc full?)\n", kn->name);
                    }
                    fclose(out);
                    Set_Type(path, strcmp(kn->name, "CONQUER.INI") == 0 ? FILETYPE_TEXT : FILETYPE_DATA);
                    Check(kn, io.out_size, io.crc);
                }
            }
        }
    }

    for (k = 0; k < sizeof(Archive_Files) / sizeof(Archive_Files[0]); ++k) {
        if (!found[k]) {
            printf("  %-12s NOT FOUND in SETUP.Z\n", Archive_Files[k].name);
            ++Problems;
        }
    }
}

int main(int argc, char** argv)
{
    const char* iso_name = NULL;
    struct Entry root, general;
    const struct Side* side = NULL;
    int movies = 1;
    int i;
    size_t k;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-nomovies") == 0) {
            movies = 0;
        } else if (strcmp(argv[i], "-force") == 0) {
            Force = 1;
        } else if (iso_name == NULL) {
            iso_name = argv[i];
        } else if (App_Dir == NULL) {
            App_Dir = argv[i];
        } else {
            iso_name = NULL;
            break;
        }
    }
    if (iso_name == NULL || App_Dir == NULL) {
        fprintf(stderr, "usage: vcprep <C&C95 CD image> <!VanillaTD dir> [-nomovies] [-force]\n");
        return 2;
    }

    Crc_Init();
    Iso = fopen(iso_name, "rb");
    if (Iso == NULL) {
        fprintf(stderr, "can't open %s: %s\n", iso_name, strerror(errno));
        return 1;
    }
    if (!Open_Root(&root)) {
        fprintf(stderr, "%s is not an ISO 9660 CD image\n", iso_name);
        return 1;
    }

    /* Tell the discs apart by the size of GENERAL.MIX, then check it properly below. */
    if (Find_In_Dir(&root, "GENERAL.MIX", &general)) {
        for (k = 0; k < sizeof(Sides) / sizeof(Sides[0]); ++k) {
            if (general.size == Sides[k].general.size) {
                side = &Sides[k];
            }
        }
    }
    if (side == NULL) {
        fprintf(stderr, "%s is not the C&C95 (C&C Gold) GDI or Nod disc this was made for\n", iso_name);
        return 1;
    }
    Side_Dir = side->dir;
    printf("%s disc, installing into %s\n", strcmp(side->dir, "gdi") == 0 ? "GDI" : "Nod", App_Dir);

    for (k = 0; k < sizeof(Disc_Files) / sizeof(Disc_Files[0]); ++k) {
        Copy_Plain(&root, &Disc_Files[k]);
    }
    Extract_Archive(&root);
    Copy_Plain(&root, &side->general);
    if (movies) {
        Copy_Plain(&root, &side->movies);
    } else {
        printf("  %-12s left out (-nomovies)\n", side->movies.name);
    }

    fclose(Iso);
    if (Problems) {
        printf("%d problem(s): the disc image may be a different version or damaged.\n", Problems);
        return 1;
    }
    printf("Done.\n");
    return 0;
}
