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
** Every install (not -check) records what happened in the game's gamedata.InstallLog, for
** when it goes wrong (the install log, below).
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
#include <time.h>
#include <unistd.h>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>
/* Take file names as native RISC OS paths, untranslated. */
int __riscosify_control = __RISCOSIFY_NO_PROCESS;
#define SEP "."
#else
#include <sys/statvfs.h>
#include <sys/time.h>
#define SEP "/"
#endif

/* Which build this is, for the install log: git describe, passed in by the deploy scripts. */
#ifndef TOOLS_BUILD
#define TOOLS_BUILD "unknown"
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

/*
** ---- The install log ----
** Every install (not -check) writes what happened to gamedata.InstallLog (a Text file),
** replacing the last one, so that an install that went wrong where nobody saw the window can
** still be looked into: the machine, the images and what each was found to be, and a line per
** file with its result. Each line is flushed as it's written, so a crash or a power cut leaves
** what was done so far. Nothing here can make an install fail: if the log can't be written,
** it simply isn't (or it goes in the scrap directory if gamedata can't take it).
*/
#define LOG_LEAF "InstallLog"
#define LOG_MAX 2 /* run from the command line, vcprep may install both games */
static FILE* Logs[LOG_MAX];
static char Log_Paths[LOG_MAX][1100];
static const char* Log_Data_Dirs[LOG_MAX]; /* the gamedata each log is for */
static int Log_Count = 0;
static int Log_In_Scrap = 0; /* gamedata couldn't take the log: it's in the scrap directory */

static void Log(const char* format, ...)
{
    int i;
    for (i = 0; i < Log_Count; ++i) {
        va_list args;
        if (Logs[i] == NULL) {
            continue;
        }
        va_start(args, format);
        vfprintf(Logs[i], format, args);
        va_end(args);
        fputc('\n', Logs[i]);
        fflush(Logs[i]);
    }
}

/* The time in centiseconds, for how long things took. */
static unsigned long Now_Cs(void)
{
#ifdef __riscos__
    unsigned t = 0;
    _swix(OS_ReadMonotonicTime, _OUT(0), &t);
    return t;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)tv.tv_sec * 100 + (unsigned long)tv.tv_usec / 10000;
#endif
}

/* The last error a file operation got, as the OS put it, for the log. */
static const char* Error_Text(void)
{
    static char text[300];
#ifdef __riscos__
    const _kernel_oserror* e = _kernel_last_oserror();
    if (e != NULL) {
        snprintf(text, sizeof(text), "%s (error &%X)", e->errmess, (unsigned)e->errnum);
        return text;
    }
#endif
    snprintf(text, sizeof(text), "%s", errno != 0 ? strerror(errno) : "no error given");
    return text;
}

/* Forgets the last error, before an operation whose error is to be logged. */
static void Clear_Error(void)
{
    errno = 0;
#ifdef __riscos__
    (void)_kernel_last_oserror();
#endif
}

/* Free space on the disc holding path, in bytes, or -1. */
static double Free_Space(const char* path)
{
#ifdef __riscos__
    unsigned lo = 0, hi = 0;
    if (_swix(OS_FSControl, _INR(0, 1) | _OUTR(0, 1), 55, path, &lo, &hi) == NULL) { /* FreeSpace64 */
        return (double)lo + (double)hi * 4294967296.0;
    }
    if (_swix(OS_FSControl, _INR(0, 1) | _OUT(0), 49, path, &lo) == NULL) { /* FreeSpace (RISC OS 3, 4) */
        return (double)lo;
    }
    return -1;
#else
    struct statvfs s;
    return statvfs(path, &s) == 0 ? (double)s.f_bavail * (double)s.f_frsize : -1;
#endif
}

static void Log_Free_Space(const char* when, const char* path)
{
    double free = Free_Space(path);
    if (free >= 0) {
        Log("free space %s: %.1f MB", when, free / (1024 * 1024));
    } else {
        Log("free space %s: unknown", when);
    }
}

static void Set_Type(const char* path, int type);
static char* Read_Var(const char* name);

/* Opens the log in a gamedata directory (made if need be), or else in the scrap directory. */
static void Log_Open(const char* data_dir, const char* app_name)
{
    FILE* f;
    char* path;
    if (Log_Count == LOG_MAX) {
        return;
    }
    path = Log_Paths[Log_Count];
    mkdir(data_dir, 0755);
    snprintf(path, sizeof(Log_Paths[0]), "%s" SEP LOG_LEAF, data_dir);
    f = fopen(path, "w");
    if (f == NULL) {
        const char* scrap = Read_Var("Wimp$ScrapDir");
#ifndef __riscos__
        if (scrap == NULL) {
            scrap = "/tmp";
        }
#endif
        if (scrap == NULL || *scrap == '\0') {
            return;
        }
        snprintf(path, sizeof(Log_Paths[0]), "%s" SEP "%s" LOG_LEAF, scrap, app_name);
        f = fopen(path, "w");
        if (f == NULL) {
            return;
        }
        Log_In_Scrap = 1;
    }
    Set_Type(path, FILETYPE_TEXT);
    Log_Data_Dirs[Log_Count] = data_dir;
    Logs[Log_Count++] = f;
}

static void Log_Close(void)
{
    int i;
    for (i = 0; i < Log_Count; ++i) {
        if (Logs[i] != NULL) {
            fclose(Logs[i]);
            Logs[i] = NULL;
            Set_Type(Log_Paths[i], FILETYPE_TEXT);
        }
    }
}

/* Where the log is, for the window: !VanillaTD.gamedata.InstallLog, or its whole path. */
static void Say_Where_Log_Is(void)
{
    const char* path = Log_Paths[0];
    const char* leaf = path;
    const char* p;
    int dots = 0;
    if (Log_Count == 0) {
        printf("No install log could be written.\n");
        return;
    }
    if (Log_In_Scrap) {
        printf("Details are in %s (gamedata couldn't take them).\n", path);
        return;
    }
    /* The application's own name onwards. */
    for (p = path + strlen(path); p > path; --p) {
        if (strncmp(p - 1, SEP, 1) == 0 && ++dots == 3) {
            leaf = p;
            break;
        }
    }
    printf("Details are in %s.\n", leaf);
}

/* The machine, for the log's header. */
static void Log_System(void)
{
#ifdef __riscos__
    char version[200];
    const _kernel_oserror* e = _swix(OS_Byte, _INR(0, 1), 0, 0); /* the version, as an error */
    int slot = 0, next = 0, free = 0;
    size_t len;
    snprintf(version, sizeof(version), "%s", e != NULL ? e->errmess : "RISC OS (version unknown)");
    for (len = strlen(version); len > 0 && (unsigned char)version[len - 1] < ' '; --len) {
        version[len - 1] = '\0';
    }
    Log("system: %s", version);
    if (_swix(Wimp_SlotSize, _INR(0, 1) | _OUTR(0, 2), -1, -1, &slot, &next, &free) == NULL) {
        Log("memory: wimpslot %dK, free %dK", slot / 1024, free / 1024);
    }
#else
    Log("system: not RISC OS (a test build)");
#endif
}

static void Log_Header(int argc, char** argv)
{
    char when[40];
    time_t now = time(NULL);
    int i;
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
    Log("vcprep install log");
    Log("build: %s, compiled %s %s", TOOLS_BUILD, __DATE__, __TIME__);
    Log("started: %s", when);
    Log_System();
    for (i = 0; i < Log_Count; ++i) {
        if (Logs[i] != NULL) {
            int a;
            fputs("command:", Logs[i]);
            for (a = 0; a < argc; ++a) {
                fprintf(Logs[i], " %s", argv[a]);
            }
            fputc('\n', Logs[i]);
            fflush(Logs[i]);
        }
    }
}

/*
** -check: a line at the end of the last install's log, if there is one (it isn't made, and the
** log isn't replaced): "check 2026-10-19 21:04:11: all there". !Run checks on every start, so
** the lines stop once the log reaches a few KB.
*/
static void Log_Check(const char* data_dir, const char* problem)
{
    char path[1100], when[40];
    struct stat st;
    time_t now = time(NULL);
    FILE* f;
    snprintf(path, sizeof(path), "%s" SEP LOG_LEAF, data_dir);
    if (stat(path, &st) != 0 || st.st_size > 16 * 1024 || (f = fopen(path, "a")) == NULL) {
        return;
    }
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "check %s: %s\n", when, problem[0] != '\0' ? problem : "all there");
    fclose(f);
    Set_Type(path, FILETYPE_TEXT);
}

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

/*
** A file's line in the log: where it goes in gamedata, the size it should be, and what happened,
** with how long it took if it was copied: "  MIX.CONQUER  2345678  copied, CRC ok, 3.1 s".
*/
static unsigned long File_Started = 0; /* when the file being copied was started, or 0 */

static void Log_File(const struct Known* k, const char* format, ...)
{
    char name[64], result[400];
    va_list args;
    const char* d = k->dest;
    if (d[0] == '@') {
        snprintf(name, sizeof(name), "%s%s", Side_Dir, d + 1);
    } else {
        snprintf(name, sizeof(name), "%s", d);
    }
    va_start(args, format);
    vsnprintf(result, sizeof(result), format, args);
    va_end(args);
    if (File_Started != 0) {
        Log("  %-16s %10lu  %s, %.1f s", name, k->size, result, (Now_Cs() - File_Started) / 100.0);
        File_Started = 0;
    } else {
        Log("  %-16s %10lu  %s", name, k->size, result);
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
        Log_File(k, "already present, skipped");
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
        Clear_Error();
        if (rename(temp, path) == 0) {
            Set_Type(path, type);
            printf("  %-12s ok   \n", k->name); /* spaces cover the progress percentage */
            Log_File(k, "copied, CRC ok");
            return;
        }
        Log_File(k, "RENAME ERROR, from %s: %s", temp, Error_Text());
        printf("  %-12s can't be given its name: %s\n", k->name, strerror(errno));
    } else if (!failed) {
        printf("  %-12s CHECKSUM MISMATCH: got %lu bytes crc %08lx, expected %lu bytes crc %08lx;"
               " not installed\n",
               k->name,
               size,
               crc,
               k->size,
               k->crc);
        Log_File(k, "CRC MISMATCH: expected %lu bytes crc %08lx, got %lu bytes crc %08lx", k->size, k->crc, size, crc);
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
        Log_File(k, "NOT FOUND on the disc");
        ++Problems;
        return;
    }
    if (k->size > 100000000UL) {
        printf("  %-12s %lu MB, this takes a while\n", k->name, k->size >> 20);
    }
    ++Copying;
    Progress(k, 0);
    File_Started = Now_Cs();

    Make_Dirs(path);
    Temp_Path(path, temp, sizeof(temp));
    Clear_Error();
    out = fopen(temp, "wb");
    if (out == NULL) {
        Log_File(k, "CREATE ERROR, %s: %s", temp, Error_Text());
        printf("  %-12s can't create %s: %s\n", k->name, temp, strerror(errno));
        ++Problems;
        return;
    }

    for (left = e.size; left > 0;) {
        size_t chunk = left < sizeof(Buffer) ? (size_t)left : sizeof(Buffer);
        Clear_Error(); /* so that an error logged is this chunk's */
        if (!Source_Read(&e, done, Buffer, chunk)) {
            printf("  %-12s read error; not installed\n", k->name);
            {
                const char* why = Error_Text();
                Log_File(k, "READ ERROR at byte %lu of %lu: %s", done, e.size,
                         strcmp(why, "no error given") == 0 ? "the image ends there (cut short?)" : why);
            }
            failed = 1;
            break;
        }
        if (fwrite(Buffer, 1, chunk, out) != chunk) {
            printf("  %-12s write error (disc full?); not installed\n", k->name);
            Log_File(k, "WRITE ERROR at byte %lu: %s", done, Error_Text());
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
        Log_File(k, "WRITE ERROR on closing: %s", Error_Text());
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
        Log("  INSTALL/SETUP.Z NOT FOUND on the disc");
        ++Problems;
        return;
    }
    base = 0; /* offsets below are in SETUP.Z */
    if (!Source_Read(&setup, base, hdr, sizeof(hdr)) || Get32(hdr) != 0x8C655D13UL || Get32(hdr + 4) != 0x02013AUL) {
        printf("  SETUP.Z is not an InstallShield 3 archive\n");
        Log("  INSTALL/SETUP.Z is not an InstallShield 3 archive (or can't be read)");
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
                        Log_File(kn, "already present, kept (the player's settings)");
                        continue;
                    }
                    if (Already_Present(kn, path)) {
                        continue;
                    }

                    Make_Dirs(path);
                    ++Copying;
                    Progress(kn, 0);
                    File_Started = Now_Cs();
                    Temp_Path(path, temp, sizeof(temp));
                    Clear_Error();
                    out = fopen(temp, "wb");
                    if (out == NULL) {
                        Log_File(kn, "CREATE ERROR, %s: %s", temp, Error_Text());
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
                        Log_File(kn, "UNPACKING FAILED (SETUP.Z damaged or cut short?)");
                        failed = 1;
                    }
                    if (fclose(out) != 0) {
                        io.write_error = 1;
                    }
                    if (io.write_error && !failed) {
                        printf("  %-12s write error (disc full?); not installed\n", kn->name);
                        Log_File(kn, "WRITE ERROR: %s", Error_Text());
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
            Log_File(&Archive_Files[k], "NOT FOUND in INSTALL/SETUP.Z");
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
        Log_File(k, "NOT MADE by Utils.ramix");
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
        Log("  not installed: a Red Alert disc, and this run is for Tiberian Dawn");
        ++Problems;
        return 0;
    }
    App_Dir = RA_Dir;
    mkdir(App_Dir, 0755); /* gamedata, the first time */
    printf("%s: Red Alert %s disc, installing into %s\n", iso_name, disc->name, App_Dir);
    Log("  Red Alert %s disc, installing into %s", disc->name, App_Dir);
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
        int status;
        Dest_Path(&RA_No_Movies, path, sizeof(path));
        Temp_Path(path, temp, sizeof(temp));
        if (!Already_Present(&RA_No_Movies, path)) {
            Make_Dirs(path);
            printf("  %-12s without its movies (-nomovies)\n", RA_No_Movies.name);
            fflush(stdout);
            ++Copying;
            Progress_Growing(&RA_No_Movies, temp);
            File_Started = Now_Cs();
            snprintf(command,
                     sizeof(command),
                     "%s" SEP "Utils" SEP "ramix copy %s@%ld %s MOVIES1.MIX MOVIES2.MIX",
                     RA_App,
                     iso_name,
                     Source_Offset(main_mix),
                     temp);
            if (Source_Offset(main_mix) < 0) {
                printf("  %-12s can't be made from this kind of source; not installed\n", RA_No_Movies.name);
                Log_File(&RA_No_Movies, "can't be made from this kind of source");
                ++Problems;
            } else if ((status = system(command)) != 0) {
                printf("  %-12s Utils.ramix failed; not installed\n", RA_No_Movies.name);
                Log_File(&RA_No_Movies, "Utils.ramix FAILED (it returned %d): %s", status, command);
                remove(temp);
                ++Problems;
            } else {
                Check_File(&RA_No_Movies, temp, path);
            }
        }
    }
    return 1;
}

/* An image's line in the log: its size, and the disc it is (discid.h). */
static void Log_Image(const char* path)
{
    int cut = 0;
    char volume[40];
    int d = Disc_Identify(path, &cut, volume, sizeof(volume));
    Log("image: %s, %ld bytes: %s%s%s%s",
        path,
        File_Size(path),
        d >= 0 ? Disc_Types[d].name : d == DISC_NOT_ISO ? "not a CD image (or can't be read)" : "a CD image of no disc known",
        volume[0] != '\0' ? ", volume " : "",
        volume,
        cut ? ", CUT SHORT (the download or copy didn't finish?)" : "");
}

static int Install_From(const char* iso_name, int movies, int quiet_if_not_cnc)
{
    struct Entry root, general;
    const struct Side* side = NULL;
    size_t k;

    Log_Image(iso_name);
    Clear_Error();
    if (!Source_Open(iso_name)) {
        Log("  not installed: it can't be opened: %s", Error_Text());
        printf("can't open %s: %s\n", iso_name, strerror(errno));
        ++Problems;
        return 0;
    }
    if (!Source_Root(&root)) {
        Source_Close();
        Log("  not installed: not an ISO 9660 CD image");
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
        if (!done && RA_Dir != NULL) {
            Log("  not installed: not the Red Alert Allied or Soviet disc this was made for");
        }
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
            Log("  not installed: a C&C95 disc, and this run is for Red Alert");
            printf("%s: C&C95 disc, but !VanillaTD hasn't been seen (double-click it first)\n", iso_name);
            ++Problems;
            return 0;
        }
        Log("  not installed: not a C&C95 GDI or Nod disc, nor a Red Alert one");
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
    Log("  C&C95 %s disc, installing into %s", strcmp(side->dir, "gdi") == 0 ? "GDI" : "Nod", App_Dir);

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
        Log_File(&side->movies, "left out (no movies)");
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

/* The log's last lines: the free space left, the time taken and how it went. */
static unsigned long Started = 0;

static void Log_Finish(const char* failure)
{
    if (Log_Count > 0) {
        Log_Free_Space("after", Log_Data_Dirs[0]);
    }
    Log("time taken: %.1f s", (Now_Cs() - Started) / 100.0);
    if (failure != NULL) {
        Log("result: FAILED: %s", failure);
    } else if (Problems != 0 || Problem[0] != '\0') {
        Log("result: FAILED: %d problem(s)%s%s",
            Problems,
            Problem[0] != '\0' ? "; the game's data is still incomplete: " : "",
            Problem);
    } else {
        Log("result: OK");
    }
    Log_Close();
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
    Log_Finish(NULL);
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
    Say_Where_Log_Is();
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
        Log_Check(td ? TD_Dir : RA_Dir, Problem);
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
    Started = Now_Cs();
    {
        /* Run from the command line with both games seen, the log goes only to the games the
        ** images are for (both if that can't be told). */
        int td = TD_Dir != NULL, ra = RA_Dir != NULL;
        if (td && ra && num_images > 0) {
            int any_td = 0, any_ra = 0;
            for (i = 0; i < num_images; ++i) {
                int cut;
                int d = Disc_Identify(images[i], &cut, NULL, 0);
                any_td |= d >= 0 && strncmp(Disc_Types[d].id, "td-", 3) == 0;
                any_ra |= d >= 0 && strncmp(Disc_Types[d].id, "ra-", 3) == 0;
            }
            if (any_td || any_ra) {
                td = any_td;
                ra = any_ra;
            }
        }
        if (td) {
            Log_Open(TD_Dir, "VanillaTD");
        }
        if (ra) {
            Log_Open(RA_Dir, "VanillaRA");
        }
    }
    Log_Header(argc, argv);
    for (i = 0; i < num_images; ++i) {
        Log("image given: %s", images[i]);
    }
    if (search != NULL) {
        Log("images from: %s", search);
    }
    Log("movies: %s%s", movies ? "included" : "left out (-nomovies)", Force ? "; -force: files rewritten" : "");
    Log_Free_Space("before", TD_Dir != NULL ? TD_Dir : RA_Dir);
    if (TD_App != NULL) {
        Migrate(1);
    }
    if (RA_App != NULL) {
        Migrate(0);
    }
    Log("moved from an earlier layout: %d file(s)", Moved);
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
            Log_Finish("no C&C95 or Red Alert CD images found");
            Say_Where_Log_Is();
            return 1;
        }
    }

    Log_Finish(NULL);
    if (Problems) {
        printf("%d problem(s): the disc image may be a different version or damaged.\n", Problems);
        Say_Where_Log_Is();
        return 1;
    }
    printf("Done.\n");
    return 0;
}
