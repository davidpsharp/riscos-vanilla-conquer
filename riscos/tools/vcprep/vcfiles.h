/*
** vcfiles.h: the files Tiberian Dawn and Red Alert need from each freeware CD image, with
** their sizes and CRC-32s, and where they go. Shared by Utils.vcprep, which installs and checks
** them, and the games' desktop code (common/riscos_desktop.cpp), which works out from it what a
** dropped disc would add and which discs are installed.
**
** Everything goes in the gamedata directory inside the application (!VanillaTD.gamedata,
** !VanillaRA.gamedata), with the layout the game's short filename scheme expects:
** MIX.<name>, gdi.MIX.GENERAL, allied.MIX.MAIN and so on. Deleting gamedata uninstalls it.
**
** C, header-only, so it can be included by C and C++ alike. SEP is the host's directory
** separator ("." on RISC OS).
*/
#ifndef VCFILES_H
#define VCFILES_H

#ifndef SEP
#ifdef __riscos__
#define SEP "."
#else
#define SEP "/"
#endif
#endif

/* ---- Known files ---- */

struct Known
{
    const char* name;  /* On the disc or in SETUP.Z. */
    const char* dest;  /* Relative to gamedata; "@" is replaced by gdi or nod. */
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

/*
** Red Alert (the 1996 Windows 95 discs, freeware since 2008). REDALERT.MIX is the
** same on both; MAIN.MIX differs only in its movies. With movies, each disc's
** MAIN.MIX goes in allied or soviet, where the game looks for a disc's files.
** Without, a copy of MAIN.MIX leaving the movies out (85 MB, the same from either
** disc) goes in MIX, made by Utils.ramix, which can decrypt its index.
*/
static const struct Known RA_Redalert = {"REDALERT.MIX", "MIX" SEP "REDALERT", 25046328, 0xd011d267UL};
static const struct Known RA_No_Movies = {"MAIN.MIX", "MIX" SEP "MAIN", 85242860, 0x9820857bUL};

struct RA_Disc
{
    const char* name;
    struct Known main;
};

static const struct RA_Disc RA_Discs[] = {
    {"Allied", {"MAIN.MIX", "allied" SEP "MIX" SEP "MAIN", 454605294, 0x07a47814UL}},
    {"Soviet", {"MAIN.MIX", "soviet" SEP "MIX" SEP "MAIN", 500577414, 0x4d4140cbUL}},
};

#define DISC_FILES_COUNT (sizeof(Disc_Files) / sizeof(Disc_Files[0]))
#define ARCHIVE_FILES_COUNT (sizeof(Archive_Files) / sizeof(Archive_Files[0]))
#define SIDES_COUNT (sizeof(Sides) / sizeof(Sides[0]))
#define RA_DISCS_COUNT (sizeof(RA_Discs) / sizeof(RA_Discs[0]))

#endif
