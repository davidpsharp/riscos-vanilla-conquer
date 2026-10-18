/*
** vcprep: install Tiberian Dawn data into !VanillaTD from a C&C95 (C&C Gold)
** CD image, the freeware GDI or Nod disc. Reads the ISO directly (no CDFS
** needed), unpacks the files held in the InstallShield archive INSTALL/SETUP.Z
** and checks every file it writes against a known CRC-32.
**
** Usage: vcprep [-nomovies] [-force] [-app <app dir>] [-search <dir>] [<iso image>...]
**        vcprep -check td -app <!VanillaTD dir> | -check ra -raapp <!VanillaRA dir>
**        vcprep -identify <file>
**   -nomovies  leave out MOVIES.MIX (about 430 MB per disc)
**   -force     rewrite files that are already present with the right size
**   -app       the !VanillaTD directory; defaults to <VanillaTD$Dir>
**   -search    with no images named, install from every C&C95 CD image here
**   -check     only check that a game's files are all there at their sizes (no
**              checksums, so it takes a moment): !Run does this before every game.
**              The first problem goes in VanillaTD$DataProblem or VanillaRA$DataProblem,
**              which is unset when all is well; the exit code is 0 or 1.
**   -window    as the game runs it, in a task window, when CD images are dragged to its
**              icon: the images are listed, one per line, in the variable named by
**              -images; afterwards the game's files are checked as by -check, and if
**              anything went wrong it waits for a key, so the window stays to be read
**              (it closes by itself when all went well)
**   -identify  says which disc a file is (discid.h), in VCPrep$Disc ("td-gdi", ...,
**              "unknown", "not-iso") and VCPrep$DiscCut (set if the image is cut short)
**
** Each file is written under a temporary name and only given its own once its size
** and CRC are right, so a file under its real name is always a whole, checked copy:
** an interrupted run (Escape, a full disc, a power cut) leaves nothing that looks
** installed, and running Prepare again redoes that file.
**
** The shared files are identical on both discs; GENERAL and MOVIES go into
** the gdi or nod directory for each disc.
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
#include "discid.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

/*
** Messages. In -window mode they are held here rather than printed: a task window opens at the
** first character printed, and when all goes well there's nothing to see (Window_Install).
** stdio's own buffering doesn't hold them back there, so printf is routed through this.
*/
#include <stdarg.h>
static int Holding = 0;
static char Held[64 * 1024];
static size_t Held_Len = 0;

static int Say(const char* format, ...)
{
    va_list args;
    int n;
    va_start(args, format);
    if (Holding) {
        n = vsnprintf(Held + Held_Len, sizeof(Held) - Held_Len, format, args);
        if (n > 0) {
            Held_Len += (size_t)n < sizeof(Held) - Held_Len ? (size_t)n : sizeof(Held) - Held_Len - 1;
        }
    } else {
        n = vprintf(format, args);
    }
    va_end(args);
    return n;
}

/* Prints what was held. */
static void Release_Held(void)
{
    Holding = 0;
    fwrite(Held, 1, Held_Len, stdout);
    Held_Len = 0;
}
#define printf Say

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

#include "vcfiles.h"


/* ---- Options and state ---- */

static const char* App_Dir;  /* where the data being installed or checked goes: TD_Dir or RA_Dir */
static const char* TD_Dir;   /* !VanillaTD.gamedata */
static const char* RA_Dir;   /* !VanillaRA.gamedata */
static const char* TD_App;   /* !VanillaTD itself */
static const char* RA_App;
static const char* Side_Dir;
static int Force = 0;
static int Problems = 0;
/* -window: what is being copied goes in this variable for the game to show, not to the screen */
static const char* Progress_Var = NULL;
static unsigned char Buffer[256 * 1024];

static void Set_Var(const char* name, const char* value);

/*
** In -window mode, says which file is being installed, how far it has got in per cent, and which
** file of how many, for the game's window: "MIX.SCORES 45 3 18". Another program writing a file
** (Utils.ramix) can't say how far it has got, so then the percentage is -1 and the variable also
** gives the file's full size and where it's being written, for the game to watch it grow.
*/
static int Copying = 0, Copy_Count = 0;

static void Progress(const struct Known* k, int percent)
{
    char text[64];
    if (Progress_Var == NULL) {
        return;
    }
    snprintf(text, sizeof(text), "%s %d %d %d", k->name, percent, Copying, Copy_Count);
    Set_Var(Progress_Var, text);
}

static void Progress_Growing(const struct Known* k, const char* path)
{
    char text[1200];
    if (Progress_Var == NULL) {
        return;
    }
    snprintf(text, sizeof(text), "%s -1 %d %d %lu %s", k->name, Copying, Copy_Count, k->size, path);
    Set_Var(Progress_Var, text);
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

/* Creates each directory along the path of a file, below base (which must exist). */
static void Make_Dirs_Below(const char* base, const char* path)
{
    char dir[1024];
    const char* p = path + strlen(base) + 1;
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

static void Make_Dirs(const char* path)
{
    Make_Dirs_Below(App_Dir, path);
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

/* The temporary name a file is written under: its leaf with "_" added, still within the
** 10 characters older filing systems allow, in the same directory so it can be renamed. */
static void Temp_Path(const char* path, char* out, size_t outlen)
{
    snprintf(out, outlen, "%s_", path);
}

/* Removes what an interrupted run may have left under a file's temporary name. */
static void Remove_Temp(const struct Known* k)
{
    char path[1024], temp[1040];
    Dest_Path(k, path, sizeof(path));
    Temp_Path(path, temp, sizeof(temp));
    remove(temp);
}

/*
** Gives a file written under its temporary name its real one, if it came out at the
** right size and CRC; otherwise deletes it, so that nothing is left that looks
** installed. failed says the copy already went wrong (and said so).
*/
static void Commit(const struct Known* k, const char* temp, const char* path, unsigned long size,
                   unsigned long crc, int failed, int type)
{
    if (!failed && size == k->size && crc == k->crc) {
        remove(path); /* RISC OS can't rename over an existing file */
        if (rename(temp, path) == 0) {
            Set_Type(path, type);
            printf("  %-12s ok   \n", k->name); /* spaces cover the progress percentage */
            return;
        }
        printf("  %-12s can't be given its name: %s\n", k->name, strerror(errno));
    } else if (!failed) {
        printf("  %-12s CHECKSUM MISMATCH: got %lu bytes crc %08lx, expected %lu bytes crc %08lx;"
               " not installed\n",
               k->name,
               size,
               crc,
               k->size,
               k->crc);
    }
    remove(temp);
    ++Problems;
}

/* ---- ISO 9660 ---- */

struct Entry
{
    unsigned long lba; /* where it starts on the disc, in sectors */
    unsigned long size;
    int is_dir;
};

/*
** ---- Where the disc's files come from ----
** Everything else reads the disc through these few calls, so that another kind of source can
** be added beside the ISO image file: a zipped ISO, as downloaded.
**   Source_Open    opens a source; 0 if it can't be read as one
**   Source_Root    its root directory (0 if it isn't a CD image)
**   Source_Find    a file or directory in a directory, by its name on the disc
**   Source_Read    bytes of a file, at an offset in it (1 if they were all read)
**   Source_Offset  where a file starts in the image, for Utils.ramix (-1 if it has none)
**   Source_Close
** Telling which disc it is goes by the same reads (discid.h, for -identify and the games).
*/
struct Source
{
    FILE* iso;
};

static struct Source Src;

static int Iso_Read_At(unsigned long offset, void* buf, size_t len)
{
    return fseek(Src.iso, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, Src.iso) == len;
}

static int Source_Open(const char* path)
{
    Src.iso = fopen(path, "rb");
    return Src.iso != NULL;
}

static void Source_Close(void)
{
    if (Src.iso != NULL) {
        fclose(Src.iso);
        Src.iso = NULL;
    }
}

static int Source_Read(const struct Entry* file, unsigned long offset, void* buf, size_t len)
{
    if (offset + len > file->size) {
        return 0;
    }
    return Iso_Read_At(file->lba * SECTOR + offset, buf, len);
}

static long Source_Offset(const struct Entry* file)
{
    return (long)(file->lba * SECTOR);
}

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

static int Source_Find(const struct Entry* dir, const char* name, struct Entry* out)
{
    unsigned long pos = 0;
    while (pos < dir->size) {
        unsigned long sector = pos / SECTOR;
        unsigned long end = (sector + 1) * SECTOR;
        unsigned char sec[SECTOR];
        unsigned long off;

        if (!Iso_Read_At((dir->lba + sector) * SECTOR, sec, SECTOR)) {
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

static int Source_Root(struct Entry* root)
{
    unsigned char pvd[SECTOR];
    if (!Iso_Read_At(16 * SECTOR, pvd, SECTOR) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
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
    char path[1024], temp[1040];
    struct Entry e;
    FILE* out;
    unsigned long left, crc = 0, done = 0, next_dot = 0;
    int failed = 0, shown = 0;

    Dest_Path(k, path, sizeof(path));
    if (Already_Present(k, path)) {
        return;
    }
    if (!Source_Find(root, k->name, &e) || e.is_dir) {
        printf("  %-12s NOT FOUND on this disc\n", k->name);
        ++Problems;
        return;
    }
    if (k->size > 100000000UL) {
        printf("  %-12s %lu MB, this takes a while\n", k->name, k->size >> 20);
    }
    ++Copying;
    Progress(k, 0);

    Make_Dirs(path);
    Temp_Path(path, temp, sizeof(temp));
    out = fopen(temp, "wb");
    if (out == NULL) {
        printf("  %-12s can't create %s: %s\n", k->name, temp, strerror(errno));
        ++Problems;
        return;
    }

    for (left = e.size; left > 0;) {
        size_t chunk = left < sizeof(Buffer) ? (size_t)left : sizeof(Buffer);
        if (!Source_Read(&e, done, Buffer, chunk)) {
            printf("  %-12s read error; not installed\n", k->name);
            failed = 1;
            break;
        }
        if (fwrite(Buffer, 1, chunk, out) != chunk) {
            printf("  %-12s write error (disc full?); not installed\n", k->name);
            failed = 1;
            break;
        }
        crc = Crc_Update(crc, Buffer, chunk);
        left -= chunk;
        done += chunk;
        if (Progress_Var != NULL) {
            int percent = (int)((double)done * 100 / e.size);
            if (percent != shown) {
                Progress(k, percent);
                shown = percent;
            }
        } else if (e.size > 20000000UL && done >= next_dot) {
            printf("  %-12s %lu%%\r", k->name, (unsigned long)(done / (e.size / 100 + 1)));
            fflush(stdout);
            next_dot = done + 10000000UL;
        }
    }
    if (fclose(out) != 0 && !failed) {
        printf("  %-12s write error (disc full?); not installed\n", k->name);
        failed = 1;
    }
    Commit(k, temp, path, done, crc, failed, FILETYPE_DATA);
}

/* ---- InstallShield 3 archive (SETUP.Z) ---- */

struct Blast_Io
{
    const struct Known* known; /* the file being unpacked, for the progress */
    int shown;
    const struct Entry* in;  /* SETUP.Z */
    unsigned long in_pos;    /* where the next read comes from, in it */
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
    if (!Source_Read(io->in, io->in_pos, Buffer, chunk)) {
        return 0;
    }
    io->in_pos += chunk;
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
    if (io->known != NULL && io->known->size > 0) {
        int percent = (int)((double)io->out_size * 100 / io->known->size);
        if (percent != io->shown && percent <= 100) {
            Progress(io->known, percent);
            io->shown = percent;
        }
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

    if (!Source_Find(root, "INSTALL", &install) || !install.is_dir || !Source_Find(&install, "SETUP.Z", &setup)) {
        printf("  INSTALL/SETUP.Z NOT FOUND on this disc\n");
        ++Problems;
        return;
    }
    base = 0; /* offsets below are in SETUP.Z */
    if (!Source_Read(&setup, base, hdr, sizeof(hdr)) || Get32(hdr) != 0x8C655D13UL || Get32(hdr + 4) != 0x02013AUL) {
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
            if (!Source_Read(&setup, pos, rec, sizeof(rec))) {
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

                if (!Source_Read(&setup, pos, f, 30)) {
                    return;
                }
                csize = Get32(f + 7);
                offset = Get32(f + 11);
                attrib = f[25];
                namelen = f[29];
                if (!Source_Read(&setup, pos + 30, name, (size_t)namelen)) {
                    return;
                }
                name[namelen] = '\0';
                pos += Get16(f + 23);

                for (k = 0; k < sizeof(Archive_Files) / sizeof(Archive_Files[0]); ++k) {
                    const struct Known* kn = &Archive_Files[k];
                    char path[1024], temp[1040];
                    struct Blast_Io io;
                    FILE* out;
                    int failed = 0;

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
                    ++Copying;
                    Progress(kn, 0);
                    Temp_Path(path, temp, sizeof(temp));
                    out = fopen(temp, "wb");
                    if (out == NULL) {
                        printf("  %-12s can't create %s: %s\n", kn->name, temp, strerror(errno));
                        ++Problems;
                        continue;
                    }
                    memset(&io, 0, sizeof(io));
                    io.known = kn;
                    io.in = &setup;
                    io.in_pos = base + offset;
                    io.in_left = csize;
                    io.out = out;
                    if (attrib & 0x10) {
                        /* Stored uncompressed. */
                        unsigned char* buf;
                        unsigned n;
                        while ((n = Blast_In(&io, &buf)) > 0 && !Blast_Out(&io, buf, n)) {
                        }
                    } else if (blast(Blast_In, &io, Blast_Out, &io, NULL, NULL) != 0 && !io.write_error) {
                        printf("  %-12s decompression failed; not installed\n", kn->name);
                        failed = 1;
                    }
                    if (fclose(out) != 0) {
                        io.write_error = 1;
                    }
                    if (io.write_error && !failed) {
                        printf("  %-12s write error (disc full?); not installed\n", kn->name);
                        failed = 1;
                    }
                    Commit(kn,
                           temp,
                           path,
                           io.out_size,
                           io.crc,
                           failed,
                           FILETYPE_DATA); /* the INI too: every installed file is Data */
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

/* Checks a file another program wrote under its temporary name, by size and CRC, and
** gives it its real name if it's right. */
static void Check_File(const struct Known* k, const char* temp, const char* path)
{
    FILE* f = fopen(temp, "rb");
    unsigned long crc = 0, size = 0;
    size_t n;
    if (f == NULL) {
        printf("  %-12s wasn't made\n", k->name);
        ++Problems;
        return;
    }
    while ((n = fread(Buffer, 1, sizeof(Buffer), f)) > 0) {
        crc = Crc_Update(crc, Buffer, n);
        size += n;
    }
    fclose(f);
    Commit(k, temp, path, size, crc, 0, FILETYPE_DATA);
}

static int Install_RA(const char* iso_name, const struct Entry* root, const struct Entry* main_mix, int movies)
{
    const struct RA_Disc* disc = NULL;
    struct Entry install;
    size_t k;

    for (k = 0; k < sizeof(RA_Discs) / sizeof(RA_Discs[0]); ++k) {
        if (main_mix->size == RA_Discs[k].main.size) {
            disc = &RA_Discs[k];
        }
    }
    if (disc == NULL || !Source_Find(root, "INSTALL", &install) || !install.is_dir) {
        return 0;
    }
    if (RA_Dir == NULL) {
        printf("%s: Red Alert %s disc, but !VanillaRA hasn't been seen (double-click it first)\n",
               iso_name,
               disc->name);
        ++Problems;
        return 0;
    }
    App_Dir = RA_Dir;
    mkdir(App_Dir, 0755); /* gamedata, the first time */
    printf("%s: Red Alert %s disc, installing into %s\n", iso_name, disc->name, App_Dir);
    Remove_Temp(&RA_Redalert);
    Remove_Temp(&RA_No_Movies);
    for (k = 0; k < sizeof(RA_Discs) / sizeof(RA_Discs[0]); ++k) {
        Remove_Temp(&RA_Discs[k].main);
    }
    Copy_Plain(&install, &RA_Redalert);
    if (movies) {
        Copy_Plain(root, &disc->main);
    } else {
        /* MAIN.MIX without its movies, straight from the image. */
        char path[1024], temp[1040], command[3072];
        Dest_Path(&RA_No_Movies, path, sizeof(path));
        Temp_Path(path, temp, sizeof(temp));
        if (!Already_Present(&RA_No_Movies, path)) {
            Make_Dirs(path);
            printf("  %-12s without its movies (-nomovies)\n", RA_No_Movies.name);
            fflush(stdout);
            ++Copying;
            Progress_Growing(&RA_No_Movies, temp);
            snprintf(command,
                     sizeof(command),
                     "%s" SEP "Utils" SEP "ramix copy %s@%ld %s MOVIES1.MIX MOVIES2.MIX",
                     RA_App,
                     iso_name,
                     Source_Offset(main_mix),
                     temp);
            if (Source_Offset(main_mix) < 0) {
                printf("  %-12s can't be made from this kind of source; not installed\n", RA_No_Movies.name);
                ++Problems;
            } else if (system(command) != 0) {
                printf("  %-12s Utils.ramix failed; not installed\n", RA_No_Movies.name);
                remove(temp);
                ++Problems;
            } else {
                Check_File(&RA_No_Movies, temp, path);
            }
        }
    }
    return 1;
}

static int Install_From(const char* iso_name, int movies, int quiet_if_not_cnc)
{
    struct Entry root, general;
    const struct Side* side = NULL;
    size_t k;

    if (!Source_Open(iso_name)) {
        printf("can't open %s: %s\n", iso_name, strerror(errno));
        ++Problems;
        return 0;
    }
    if (!Source_Root(&root)) {
        Source_Close();
        if (!quiet_if_not_cnc) {
            printf("%s is not an ISO 9660 CD image\n", iso_name);
            ++Problems;
        }
        return 0;
    }

    /* A Red Alert disc has MAIN.MIX in its root. */
    if (Source_Find(&root, "MAIN.MIX", &general) && !general.is_dir) {
        int done = Install_RA(iso_name, &root, &general, movies);
        Source_Close();
        if (!done && !quiet_if_not_cnc) {
            printf("%s is not a Red Alert Allied or Soviet disc this was made for\n", iso_name);
            ++Problems;
        }
        return done;
    }

    /* Tell the discs apart by the size of GENERAL.MIX, then check it properly below. */
    if (Source_Find(&root, "GENERAL.MIX", &general)) {
        for (k = 0; k < sizeof(Sides) / sizeof(Sides[0]); ++k) {
            if (general.size == Sides[k].general.size) {
                side = &Sides[k];
            }
        }
    }
    if (side == NULL || TD_Dir == NULL) {
        Source_Close();
        if (side != NULL) {
            printf("%s: C&C95 disc, but !VanillaTD hasn't been seen (double-click it first)\n", iso_name);
            ++Problems;
            return 0;
        }
        printf("%s is not the C&C95 (C&C Gold) GDI or Nod disc this was made for%s\n",
               iso_name,
               quiet_if_not_cnc ? ", skipped" : "");
        if (!quiet_if_not_cnc) {
            ++Problems;
        }
        return 0;
    }
    Side_Dir = side->dir;
    App_Dir = TD_Dir;
    mkdir(App_Dir, 0755); /* gamedata, the first time */
    printf("%s: %s disc, installing into %s\n", iso_name, strcmp(side->dir, "gdi") == 0 ? "GDI" : "Nod", App_Dir);

    for (k = 0; k < sizeof(Disc_Files) / sizeof(Disc_Files[0]); ++k) {
        Remove_Temp(&Disc_Files[k]);
    }
    for (k = 0; k < sizeof(Archive_Files) / sizeof(Archive_Files[0]); ++k) {
        Remove_Temp(&Archive_Files[k]);
    }
    Remove_Temp(&side->general);
    Remove_Temp(&side->movies);

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
    Source_Close();
    return 1;
}

/*
** Installs from every CD image in a directory. The images are recognised by
** their contents, not their names: RISC OS 3.x truncates leaf names to 10
** characters, so CNC95_GDI/iso may well have lost its extension.
*/
#define MIN_IMAGE_SIZE (100L * 1024 * 1024)

static int Install_From_Dir(const char* dir, int movies)
{
    char path[1024];
    int found = 0;
#ifdef __riscos__
    static unsigned char entries[4096];
    int offset = 0;
    while (offset != -1) {
        int count = 0, next = -1, i;
        const unsigned char* e = entries;
        if (_swix(OS_GBPB, _INR(0, 6) | _OUT(3) | _OUT(4), 10, dir, entries, 64, offset, sizeof(entries), "*", &count,
                  &next)
            != NULL) {
            printf("can't read the directory %s\n", dir);
            ++Problems;
            return found;
        }
        for (i = 0; i < count; ++i) {
            /* load, exec, length, attributes, object type, then the name, word aligned */
            unsigned long length = Get32(e + 8);
            unsigned long type = Get32(e + 16);
            const char* name = (const char*)e + 20;
            if ((type == 1 || type == 3) && length >= (unsigned long)MIN_IMAGE_SIZE) {
                snprintf(path, sizeof(path), "%s" SEP "%s", dir, name);
                found += Install_From(path, movies, 1);
            }
            e += (20 + strlen(name) + 1 + 3) & ~3u;
        }
        offset = next;
    }
#else
    DIR* d = opendir(dir);
    struct dirent* de;
    if (d == NULL) {
        printf("can't read the directory %s: %s\n", dir, strerror(errno));
        ++Problems;
        return 0;
    }
    while ((de = readdir(d)) != NULL) {
        snprintf(path, sizeof(path), "%s" SEP "%s", dir, de->d_name);
        if (de->d_name[0] != '.' && File_Size(path) >= MIN_IMAGE_SIZE) {
            found += Install_From(path, movies, 1);
        }
    }
    closedir(d);
#endif
    return found;
}

#ifdef __riscos__
/* Prepare passes <VanillaTD$Dir> and <Obey$Dir>; turn them into real paths for the messages. */
static const char* Canonical(const char* path)
{
    char buf[1024];
    char* copy;
    if (_swix(OS_FSControl, _INR(0, 5), 37, path, buf, 0, 0, sizeof(buf)) != NULL || (copy = strdup(buf)) == NULL) {
        return path;
    }
    return copy;
}
#else
#define Canonical(path) (path)
#endif

/* ---- -check: is a game's data all there? ---- */

static char Problem[200];

/* The first problem found is the one reported. */
static void Note_Problem(const struct Known* k, const char* what)
{
    const char* d = k->dest;
    if (Problem[0] != '\0') {
        return;
    }
    if (d[0] == '@') {
        snprintf(Problem, sizeof(Problem), "%s%s %s", Side_Dir, d + 1, what);
    } else {
        snprintf(Problem, sizeof(Problem), "%s %s", d, what);
    }
}

/* 1 if the file is there at its size; 0 if it's missing (a problem if required); -1, always a
** problem, if it's there at the wrong size: a damaged or partly copied file. */
static int Check_Present(const struct Known* k, int required)
{
    char path[1024];
    long size;
    Dest_Path(k, path, sizeof(path));
    size = File_Size(path);
    if (size < 0) {
        if (required) {
            Note_Problem(k, "is missing");
        }
        return 0;
    }
    if (size != (long)k->size) {
        Note_Problem(k, "is the wrong size");
        return -1;
    }
    return 1;
}

/* Tiberian Dawn: the files both discs share, and at least one disc's GENERAL. The movies
** are optional, and so is CONQUER.INI (the game makes one; its size changes with the
** settings in it). */
static void Check_TD(void)
{
    size_t k;
    int generals = 0;
    App_Dir = TD_Dir;
    for (k = 0; k < sizeof(Disc_Files) / sizeof(Disc_Files[0]); ++k) {
        Check_Present(&Disc_Files[k], 1);
    }
    for (k = 0; k < sizeof(Archive_Files) / sizeof(Archive_Files[0]); ++k) {
        if (strcmp(Archive_Files[k].name, "CONQUER.INI") != 0) {
            Check_Present(&Archive_Files[k], 1);
        }
    }
    for (k = 0; k < sizeof(Sides) / sizeof(Sides[0]); ++k) {
        Side_Dir = Sides[k].dir;
        generals += Check_Present(&Sides[k].general, 0) > 0;
        Check_Present(&Sides[k].movies, 0);
    }
    if (generals == 0 && Problem[0] == '\0') {
        snprintf(Problem, sizeof(Problem), "gdi" SEP "MIX" SEP "GENERAL or nod" SEP "MIX" SEP "GENERAL is missing");
    }
}

/* Red Alert: REDALERT, and a MAIN, without movies or either disc's own. */
static void Check_RA(void)
{
    size_t k;
    int mains;
    App_Dir = RA_Dir;
    Check_Present(&RA_Redalert, 1);
    mains = Check_Present(&RA_No_Movies, 0) > 0;
    for (k = 0; k < sizeof(RA_Discs) / sizeof(RA_Discs[0]); ++k) {
        mains += Check_Present(&RA_Discs[k].main, 0) > 0;
    }
    if (mains == 0 && Problem[0] == '\0') {
        snprintf(Problem, sizeof(Problem), "MIX" SEP "MAIN is missing");
    }
}

/* Puts the problem (or nothing) in the game's variable, for its !Run. */
static void Report_Check(const char* var)
{
#ifdef __riscos__
    if (Problem[0] != '\0') {
        _swix(OS_SetVarVal, _INR(0, 4), var, Problem, (int)strlen(Problem), 0, 4);
    } else {
        _swix(OS_SetVarVal, _INR(0, 4), var, NULL, -1, 0, 0);
    }
#else
    /* Nothing goes to the screen on RISC OS: !Run runs this in the desktop. */
    printf("%s: %s\n", var, Problem[0] != '\0' ? Problem : "all there");
#endif
}

#ifdef __riscos__
/* The value of a system variable, or NULL. UnixLib's getenv doesn't see every variable. */
static char* Read_Var(const char* name)
{
    static char buf[4096];
    int len = 0;
    if (_swix(OS_ReadVarVal, _INR(0, 4) | _OUT(2), name, buf, sizeof(buf) - 1, 0, 0, &len) != NULL) {
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static void Set_Var(const char* name, const char* value)
{
    if (value != NULL) {
        _swix(OS_SetVarVal, _INR(0, 4), name, value, (int)strlen(value), 0, 4);
    } else {
        _swix(OS_SetVarVal, _INR(0, 4), name, NULL, -1, 0, 0);
    }
}
#else
static char* Read_Var(const char* name)
{
    return getenv(name);
}

static void Set_Var(const char* name, const char* value)
{
    fprintf(stderr, "%s: %s\n", name, value != NULL ? value : "(unset)"); /* not held back */
}
#endif

/* A known file's path below base, with the disc's directory for "@". */
static void Known_Path(const char* base, const struct Known* k, const char* side, char* out, size_t outlen)
{
    if (k->dest[0] == '@') {
        snprintf(out, outlen, "%s" SEP "%s%s", base, side, k->dest + 1);
    } else {
        snprintf(out, outlen, "%s" SEP "%s", base, k->dest);
    }
}

/*
** Data installed by an earlier version went straight into the application directory; it moves
** into gamedata, file by file and only where gamedata hasn't one of its own: renamed, so nothing
** is copied and it takes no time, and if it's stopped part-way the next run carries on. The
** directories left empty are removed. The number moved goes in <App>$Moved, for the game's log.
*/
static int Moved = 0;

static void Migrate_One(const char* app, const char* data, const struct Known* k, const char* side)
{
    char from[1024], to[1024];
    Known_Path(app, k, side, from, sizeof(from));
    Known_Path(data, k, side, to, sizeof(to));
    if (File_Size(from) < 0 || File_Size(to) >= 0) {
        return;
    }
    mkdir(data, 0755);
    Make_Dirs_Below(data, to);
    if (rename(from, to) == 0) {
        Set_Type(to, FILETYPE_DATA);
        ++Moved;
    }
}

static void Migrate(int td)
{
    const char* app = td ? TD_App : RA_App;
    const char* data = td ? TD_Dir : RA_Dir;
    static const char* const td_dirs[] = {"gdi" SEP "MIX", "gdi", "nod" SEP "MIX", "nod", "INI", "MIX"};
    static const char* const ra_dirs[] = {"allied" SEP "MIX", "allied", "soviet" SEP "MIX", "soviet", "MIX"};
    char path[1024], text[32];
    size_t k;
    if (app == NULL) {
        return;
    }
    if (td) {
        for (k = 0; k < DISC_FILES_COUNT; ++k) {
            Migrate_One(app, data, &Disc_Files[k], "");
        }
        for (k = 0; k < ARCHIVE_FILES_COUNT; ++k) {
            Migrate_One(app, data, &Archive_Files[k], "");
        }
        for (k = 0; k < SIDES_COUNT; ++k) {
            Migrate_One(app, data, &Sides[k].general, Sides[k].dir);
            Migrate_One(app, data, &Sides[k].movies, Sides[k].dir);
        }
    } else {
        Migrate_One(app, data, &RA_Redalert, "");
        Migrate_One(app, data, &RA_No_Movies, "");
        for (k = 0; k < RA_DISCS_COUNT; ++k) {
            Migrate_One(app, data, &RA_Discs[k].main, "");
        }
    }
    if (Moved == 0) {
        return;
    }
    for (k = 0; k < (td ? sizeof(td_dirs) : sizeof(ra_dirs)) / sizeof(td_dirs[0]); ++k) {
        snprintf(path, sizeof(path), "%s" SEP "%s", app, td ? td_dirs[k] : ra_dirs[k]);
        rmdir(path); /* only if it's empty */
    }
    snprintf(text, sizeof(text), "%d files", Moved);
    Set_Var(td ? "VanillaTD$Moved" : "VanillaRA$Moved", text);
}

/* How many files installing these images will copy, for the progress ("file 3 of 18"): those
** not there yet, each once (both C&C95 discs hold the shared files; the second is skipped). */
static int Plan_Count(const char** images, int num_images, int movies)
{
    static char planned[64][1024];
    int count = 0, i;
    size_t k;
    for (i = 0; i < num_images; ++i) {
        int cut = 0;
        char volume[40];
        int d = Disc_Identify(images[i], &cut, volume, sizeof(volume));
        const struct Known* files[32];
        const char* sides[32];
        int n = 0, j, m;
        if (d < 0) {
            continue;
        }
        if (strcmp(Disc_Types[d].id, "td-gdi") == 0 || strcmp(Disc_Types[d].id, "td-nod") == 0) {
            const struct Side* side = &Sides[strcmp(Disc_Types[d].id, "td-gdi") == 0 ? 0 : 1];
            if (TD_Dir == NULL) continue;
            for (k = 0; k < DISC_FILES_COUNT; ++k) { files[n] = &Disc_Files[k]; sides[n++] = ""; }
            for (k = 0; k < ARCHIVE_FILES_COUNT; ++k) { files[n] = &Archive_Files[k]; sides[n++] = ""; }
            files[n] = &side->general; sides[n++] = side->dir;
            if (movies) { files[n] = &side->movies; sides[n++] = side->dir; }
        } else if (strcmp(Disc_Types[d].id, "ra-allied") == 0 || strcmp(Disc_Types[d].id, "ra-soviet") == 0) {
            if (RA_Dir == NULL) continue;
            files[n] = &RA_Redalert; sides[n++] = "";
            files[n] = movies ? &RA_Discs[strcmp(Disc_Types[d].id, "ra-allied") == 0 ? 0 : 1].main : &RA_No_Movies;
            sides[n++] = "";
        }
        for (j = 0; j < n; ++j) {
            char path[1024];
            int dup = 0;
            long size;
            Known_Path(strncmp(Disc_Types[d].id, "td", 2) == 0 ? TD_Dir : RA_Dir, files[j], sides[j], path, sizeof(path));
            size = File_Size(path);
            if (strcmp(files[j]->name, "CONQUER.INI") == 0 ? size >= 0 : (!Force && size == (long)files[j]->size)) {
                continue;
            }
            for (m = 0; m < count; ++m) {
                dup |= strcmp(planned[m], path) == 0;
            }
            if (!dup && count < 64) {
                snprintf(planned[count++], sizeof(planned[0]), "%s", path);
            }
        }
    }
    return count;
}

/* -identify: which disc a file is. */
static int Identify(const char* path)
{
    int cut;
    char volume[40];
    int d = Disc_Identify(path, &cut, volume, sizeof(volume));
    const char* id = d >= 0 ? Disc_Types[d].id : d == DISC_NOT_ISO ? "not-iso" : "unknown";
    Set_Var("VCPrep$Disc", id);
    Set_Var("VCPrep$DiscCut", cut ? "1" : NULL);
    printf("%s: %s%s%s\n", path, d >= 0 ? Disc_Types[d].name : id, volume[0] ? ", volume " : "", volume);
    return d >= 0 && !cut ? 0 : 1;
}

static int Usage(void)
{
    fprintf(stderr,
            "usage: vcprep [-nomovies] [-force] [-app <!VanillaTD dir>] [-raapp <!VanillaRA dir>] [-search <dir>]\n"
            "              [<CD image>...]\n"
            "       vcprep -check td -app <!VanillaTD dir> | -check ra -raapp <!VanillaRA dir>\n"
            "       vcprep -window -images <variable> -app <!VanillaTD dir> | -raapp <!VanillaRA dir>\n"
            "       vcprep -identify <file>\n"
            "  Installs from the C&C95 and Red Alert CD images given, or else every one in the -search\n"
            "  directory. The application directories default to <VanillaTD$Dir> and <VanillaRA$Dir>.\n");
    return 2;
}

/*
** -window: installs the images the game was given, then checks the game's files as -check does,
** for the game to read. Run in a task window, which only opens once something is printed: so the
** messages are held back (Say), and when all went well the program ends without them, and the
** window never appears; the game shows the progress from Progress_Var instead.
** Otherwise they're printed, and stay until a key is pressed.
*/
static int Window_Install(const char** images, int num_images, int movies)
{
    int i;
    Progress_Var = TD_Dir != NULL ? "VanillaTD$Installing" : "VanillaRA$Installing";
    Set_Var("RAMix$Quiet", "1"); /* Utils.ramix prints nothing either */
    Holding = 1;
    Crc_Init();
    Copy_Count = Plan_Count(images, num_images, movies);
    printf("Installing the game data.\n\n");
    for (i = 0; i < num_images; ++i) {
        if (Install_From(images[i], movies, 0) == 0 && Problems == 0) {
            printf("%s isn't a disc this game can install from.\n", images[i]);
            ++Problems;
        }
    }
    Set_Var(Progress_Var, NULL);
    Set_Var("RAMix$Quiet", NULL);
    Problem[0] = '\0';
    if (TD_Dir != NULL) {
        Check_TD();
        Report_Check("VanillaTD$DataProblem");
    } else {
        Check_RA();
        Report_Check("VanillaRA$DataProblem");
    }
    if (Problems == 0 && Problem[0] == '\0') {
        return 0; /* the held messages aren't printed: nothing to see */
    }
    Release_Held();
    if (Problems) {
        printf("\n%d problem(s): the disc image may be a different version or damaged.\n", Problems);
    }
    if (Problem[0] != '\0') {
        printf("The game's data is still incomplete: %s.\n", Problem);
    }
    printf("\nPress a key when you have read this, then close this window.\n");
    fflush(stdout);
#ifdef __riscos__
    _swix(OS_ReadC, 0); /* any key: getchar would wait for a whole line */
#else
    getchar();
#endif
    return 1;
}

int main(int argc, char** argv)
{
    const char* search = NULL;
    const char* images[8];
    int num_images = 0, installed = 0;
    int movies = 1;
    const char* check = NULL;
    const char* images_var = NULL;
    int window = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-nomovies") == 0) {
            movies = 0;
        } else if (strcmp(argv[i], "-force") == 0) {
            Force = 1;
        } else if (strcmp(argv[i], "-app") == 0 && i + 1 < argc) {
            TD_Dir = argv[++i];
        } else if (strcmp(argv[i], "-raapp") == 0 && i + 1 < argc) {
            RA_Dir = argv[++i];
        } else if (strcmp(argv[i], "-search") == 0 && i + 1 < argc) {
            search = argv[++i];
        } else if (strcmp(argv[i], "-check") == 0 && i + 1 < argc) {
            check = argv[++i];
        } else if (strcmp(argv[i], "-identify") == 0 && i + 1 < argc) {
            return Identify(argv[i + 1]);
        } else if (strcmp(argv[i], "-window") == 0) {
            window = 1;
        } else if (strcmp(argv[i], "-images") == 0 && i + 1 < argc) {
            images_var = argv[++i];
        } else if (argv[i][0] == '-' || num_images == (int)(sizeof(images) / sizeof(images[0]))) {
            return Usage();
        } else {
            images[num_images++] = argv[i];
        }
    }
    /* The other game's directory from its variable, but not when the game runs this (-window
    ** or -check): then only the one it names is installed or checked, whatever else has run. */
    if (TD_Dir == NULL && !window && check == NULL) {
        TD_Dir = getenv("VanillaTD$Dir");
    }
    if (RA_Dir == NULL && !window && check == NULL) {
        RA_Dir = getenv("VanillaRA$Dir");
    }
    TD_App = TD_Dir != NULL && *TD_Dir != '\0' ? Canonical(TD_Dir) : NULL;
    RA_App = RA_Dir != NULL && *RA_Dir != '\0' ? Canonical(RA_Dir) : NULL;
    /* The data goes in gamedata inside each application. */
    {
        static char td[1024], ra[1024];
        snprintf(td, sizeof(td), "%s" SEP "gamedata", TD_App != NULL ? TD_App : "");
        snprintf(ra, sizeof(ra), "%s" SEP "gamedata", RA_App != NULL ? RA_App : "");
        TD_Dir = TD_App != NULL ? td : NULL;
        RA_Dir = RA_App != NULL ? ra : NULL;
    }
    if (check != NULL) {
        int td = strcmp(check, "td") == 0;
        if ((!td && strcmp(check, "ra") != 0) || (td ? TD_Dir : RA_Dir) == NULL) {
            return Usage();
        }
        Migrate(td);
        if (td) {
            Check_TD();
        } else {
            Check_RA();
        }
        Report_Check(td ? "VanillaTD$DataProblem" : "VanillaRA$DataProblem");
        return Problem[0] != '\0';
    }
    if (images_var != NULL) {
        /* One image per line: a list too long for a command line. */
        static char list[4096];
        char* line;
        const char* value = Read_Var(images_var);
        if (value != NULL) {
            strncpy(list, value, sizeof(list) - 1);
            for (line = strtok(list, "\n"); line != NULL; line = strtok(NULL, "\n")) {
                if (*line != '\0' && num_images < (int)(sizeof(images) / sizeof(images[0]))) {
                    images[num_images++] = line;
                }
            }
        }
    }
    if ((TD_Dir == NULL && RA_Dir == NULL) || (num_images == 0 && search == NULL)) {
        return Usage();
    }
    if (TD_App != NULL) {
        Migrate(1);
    }
    if (RA_App != NULL) {
        Migrate(0);
    }
    if (window) {
        return Window_Install(images, num_images, movies);
    }

    Crc_Init();
    if (num_images > 0) {
        for (i = 0; i < num_images; ++i) {
            /* A bare name that isn't in the current directory may be next to Prepare. */
            char path[1024];
            const char* image = images[i];
            if (File_Size(image) < 0 && search != NULL) {
                snprintf(path, sizeof(path), "%s" SEP "%s", Canonical(search), image);
                if (File_Size(path) >= 0) {
                    image = path;
                }
            }
            installed += Install_From(image, movies, 0);
        }
    } else {
        search = Canonical(search);
        installed = Install_From_Dir(search, movies);
        if (installed == 0 && Problems == 0) {
            printf("No C&C95 or Red Alert CD images found in %s.\n"
                   "Put the Windows 95 (C&C Gold) GDI and/or Nod disc image (.iso) there, or the\n"
                   "Red Alert Allied and/or Soviet disc image, or give its name. The MS-DOS and\n"
                   "Mac discs won't work. !VanillaTD.!Help and !VanillaRA.!Help say where to\n"
                   "download them.\n",
                   search);
            return 1;
        }
    }

    if (Problems) {
        printf("%d problem(s): the disc image may be a different version or damaged.\n", Problems);
        return 1;
    }
    printf("Done.\n");
    return 0;
}
