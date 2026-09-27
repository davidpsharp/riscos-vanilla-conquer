#include "riscos_fs.h"

#ifdef USE_SHORT_FILENAMES
#include <errno.h>
#include <set>
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

static FILE* Read_Fopen(const char* path, const char* mode)
{
    if (Missing.count(path) != 0) {
        errno = ENOENT;
        return nullptr;
    }
    FILE* fp = Traced(fopen(path, mode), path, mode);
    if (fp == nullptr && errno == ENOENT) {
        Missing.insert(path);
    }
    return fp;
}

FILE* RISCOS_Fopen(const char* path, const char* mode)
{
    return Short_Fopen(path, mode);
}

static FILE* Short_Fopen(const char* path, const char* mode)
{
    std::string short_path;

    bool writing = strchr(mode, 'w') != nullptr || strchr(mode, 'a') != nullptr || strchr(mode, '+') != nullptr;

    if (writing) {
        Missing.clear();
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
    Missing.clear();

    if (RISCOS_Short_Path(path, short_path) && unlink(short_path.c_str()) == 0) {
        return 0;
    }
    return unlink(path);
}
#endif // USE_SHORT_FILENAMES
