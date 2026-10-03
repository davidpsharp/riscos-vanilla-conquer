#include "phasetime.h"
#include "riscos_fs.h"

#ifdef USE_SHORT_FILENAMES
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <map>
#include <set>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

bool RISCOS_Short_Path(const char* path, std::string& out)
{
    const char* slash = strrchr(path, '/');
    const char* leaf = slash != nullptr ? slash + 1 : path;
    const char* dot = strrchr(leaf, '.');

    if (dot == nullptr || dot == leaf || dot[1] == '\0') {
        return false;
    }

    out.assign(path, leaf - path);
    out.append(dot + 1);
    out.push_back('/');
    out.append(leaf, dot - leaf);
    return true;
}

static FILE* Traced(FILE* fp, const char* path, const char* mode)
{
    // Set VC_FSLOG=1 to trace file opens when debugging the name mapping.
    static const bool trace = getenv("VC_FSLOG") != nullptr;
    if (trace) {
        fprintf(stderr, "fopen(%s, %s) -> %s\n", path, mode, fp != nullptr ? "ok" : "fail");
    }
    return fp;
}

static FILE* Short_Fopen(const char* path, const char* mode);

/*
** Paths that failed to open for reading. The game probes for the same missing
** files over and over (every search path, both name forms, for each Open and
** Is_Available), and every probe costs several filing system calls. Remember
** failures until the game itself creates or deletes something.
*/
static std::set<std::string> Missing;

/*
** Most probes are for different paths, though: loose files that would override the
** MIX files, each looked for under both name forms in every search directory. So
** each directory is listed once, and a file it doesn't have is missing without
** asking the filing system. Names are compared ignoring case and UnixLib's swap of
** '.' and '/'. RISC OS 3 filing systems may truncate a leaf to 10 characters, so
** a longer one is only missing if its first 10 aren't listed either. Cleared with
** Missing.
*/
static std::map<std::string, std::set<std::string>> Listings; // directory -> leaves, normalised; absent dirs empty
static std::set<std::string> Absent_Dirs;

static std::string Normal_Leaf(const char* leaf)
{
    std::string out;
    for (; *leaf != '\0' && *leaf != ','; ++leaf) { // ",xxx" is a filetype suffix
        char c = *leaf == '/' ? '.' : char(tolower((unsigned char)*leaf));
        out.push_back(c);
    }
    return out;
}

static bool Listed_Missing(const char* path)
{
    const char* slash = strrchr(path, '/');
    std::string dir = slash != nullptr ? std::string(path, slash - path) : std::string(".");
    const char* leaf = slash != nullptr ? slash + 1 : path;
    if (dir.empty()) {
        return false;
    }

    auto it = Listings.find(dir);
    if (it == Listings.end()) {
        if (Absent_Dirs.count(dir) != 0) {
            return true;
        }
        DIR* d = opendir(dir.c_str());
        if (d == nullptr) {
            if (errno == ENOENT || errno == ENOTDIR) {
                Absent_Dirs.insert(dir);
                return true;
            }
            return false;
        }
        std::set<std::string> leaves;
        while (struct dirent* e = readdir(d)) {
            leaves.insert(Normal_Leaf(e->d_name));
        }
        closedir(d);
        it = Listings.insert(std::make_pair(dir, std::move(leaves))).first;
    }
    std::string name = Normal_Leaf(leaf);
    return it->second.count(name) == 0 && (name.size() <= 10 || it->second.count(name.substr(0, 10)) == 0);
}

static void Forget_Missing()
{
    Missing.clear();
    Listings.clear();
    Absent_Dirs.clear();
}

static FILE* Read_Fopen(const char* path, const char* mode)
{
    static const bool use_listings = getenv("VC_NODIRCACHE") == nullptr;
    if (Missing.count(path) != 0 || (use_listings && Listed_Missing(path))) {
        errno = ENOENT;
        return nullptr;
    }
    FILE* fp = Traced(fopen(path, mode), path, mode);
    if (fp == nullptr && errno == ENOENT) {
        Missing.insert(path);
    }
    return fp;
}

/*
** Opening a file through UnixLib costs a few milliseconds on a Risc PC (it checks
** every directory on the path for symlinks), and the game opens its MIX files
** again for every file it reads from them: 1631 opens, 5.2 of the 10.5 seconds
** from start-up to the first game frame. So files opened for reading are kept
** open when the game closes them, up to MAX_IDLE of them, and handed back,
** rewound, when the same file is opened the same way. Before anything is opened
** for writing or deleted, they're all closed: RISC OS won't open a file for
** writing while it's open, and the game spells some names more than one way
** (savegame.001 and SAVEGAME.001). If an open fails, they're closed and it's tried
** again, in case some other spelling of the file was being kept open.
*/
namespace {
struct IdleFile
{
    FILE* Handle;
    std::string Path;
    std::string Mode;
};
const size_t MAX_IDLE = 8;
std::vector<IdleFile> Idle_Files;              // oldest first
std::map<FILE*, std::pair<std::string, std::string>> Read_Files; // open for reading: path, mode

bool Read_Only(const char* mode)
{
    return mode[0] == 'r' && strchr(mode, '+') == nullptr;
}

void Close_Idle()
{
    for (size_t i = 0; i < Idle_Files.size(); ++i) {
        fclose(Idle_Files[i].Handle);
    }
    Idle_Files.clear();
}

FILE* Open(const char* path, const char* mode)
{
    if (Read_Only(mode)) {
        for (size_t i = Idle_Files.size(); i-- > 0;) {
            if (Idle_Files[i].Path == path && Idle_Files[i].Mode == mode) {
                FILE* fp = Idle_Files[i].Handle;
                Idle_Files.erase(Idle_Files.begin() + i);
                rewind(fp);
                Read_Files[fp] = std::make_pair(std::string(path), std::string(mode));
                return fp;
            }
        }
    } else {
        Close_Idle();
    }

    FILE* fp = Short_Fopen(path, mode);
    if (fp == nullptr && errno != ENOENT && !Idle_Files.empty()) {
        Close_Idle(); // perhaps "file already open": try again with none kept
        fp = Short_Fopen(path, mode);
    }
    if (fp != nullptr && Read_Only(mode)) {
        Read_Files[fp] = std::make_pair(std::string(path), std::string(mode));
    }
    return fp;
}
} // namespace

FILE* RISCOS_Fopen(const char* path, const char* mode)
{
    if (!Phase_Timing) {
        return Open(path, mode);
    }
    unsigned started = Phase_Now_Us();
    FILE* fp = Open(path, mode);
    ++Phase_Counts[COUNT_FOPENS];
    Phase_Counts[COUNT_FOPEN_US] += Phase_Now_Us() - started;
    return fp;
}

int RISCOS_Fclose(FILE* fp)
{
    auto it = Read_Files.find(fp);
    if (it == Read_Files.end()) {
        return fclose(fp);
    }
    IdleFile idle = {fp, it->second.first, it->second.second};
    Read_Files.erase(it);
    if (Idle_Files.size() >= MAX_IDLE) {
        fclose(Idle_Files.front().Handle);
        Idle_Files.erase(Idle_Files.begin());
    }
    Idle_Files.push_back(idle);
    return 0;
}

static FILE* Short_Fopen(const char* path, const char* mode)
{
    std::string short_path;

    bool writing = strchr(mode, 'w') != nullptr || strchr(mode, 'a') != nullptr || strchr(mode, '+') != nullptr;

    if (writing) {
        Forget_Missing();
    }

    if (!RISCOS_Short_Path(path, short_path)) {
        return writing ? Traced(fopen(path, mode), path, mode) : Read_Fopen(path, mode);
    }

    if (writing) {
        // Create the extension directory on demand.
        std::string dir = short_path.substr(0, short_path.rfind('/'));
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            return Traced(fopen(path, mode), path, mode);
        }
        return Traced(fopen(short_path.c_str(), mode), short_path.c_str(), mode);
    }

    FILE* fp = Read_Fopen(short_path.c_str(), mode);
    if (fp == nullptr) {
        fp = Read_Fopen(path, mode);
    }
    return fp;
}

int RISCOS_Unlink(const char* path)
{
    std::string short_path;
    Forget_Missing();
    Close_Idle();

    if (RISCOS_Short_Path(path, short_path) && unlink(short_path.c_str()) == 0) {
        return 0;
    }
    return unlink(path);
}
/*
** VC_FSTEST: check the kept-open files and directory listings against writes,
** different spellings of a name and deletes, in the current directory.
*/
void RISCOS_Fs_Self_Test()
{
    int fails = 0;
    auto check = [&fails](bool ok, const char* what) {
        fprintf(stderr, "fstest: %s %s\n", ok ? "ok  " : "FAIL", what);
        fails += ok ? 0 : 1;
    };
    auto write = [](const char* name, const char* text) {
        FILE* fp = RISCOS_Fopen(name, "wb");
        if (fp == nullptr) {
            return false;
        }
        fputs(text, fp);
        return RISCOS_Fclose(fp) == 0;
    };
    auto read = [](const char* name) {
        std::string text;
        FILE* fp = RISCOS_Fopen(name, "rb");
        if (fp == nullptr) {
            return std::string("<missing>");
        }
        int c;
        while ((c = fgetc(fp)) != EOF) {
            text.push_back(char(c));
        }
        RISCOS_Fclose(fp);
        return text;
    };

    RISCOS_Unlink("fstest.001");
    RISCOS_Unlink("fstest2.001");
    check(read("fstest.001") == "<missing>", "missing before it's written");
    check(write("fstest.001", "first"), "write");
    check(read("fstest.001") == "first", "read back");
    check(read("fstest.001") == "first", "read back again (kept open)");
    check(write("FSTEST.001", "second, longer"), "write with other case while kept open");
    check(read("fstest.001") == "second, longer", "read the new contents");
    check(read("fstest2.001") == "<missing>", "a new name in a listed directory is missing");
    FILE* fp = fopen("fstest2.001", "wb"); // behind the cache's back, as another program might
    if (fp != nullptr) {
        fputs("x", fp);
        fclose(fp);
    }
    check(write("fstest.001", "third"), "write");
    check(read("fstest2.001") == "x", "found after the cache was cleared by a write");
    check(RISCOS_Unlink("fstest.001") == 0, "delete while kept open");
    check(read("fstest.001") == "<missing>", "missing after delete");
    RISCOS_Unlink("fstest2.001");
    fprintf(stderr, "fstest: %d failures\n", fails);
}
#endif // USE_SHORT_FILENAMES
