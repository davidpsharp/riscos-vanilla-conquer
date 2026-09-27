#include "riscos_fs.h"

#ifdef USE_SHORT_FILENAMES
#include <errno.h>
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

FILE* RISCOS_Fopen(const char* path, const char* mode)
{
    return Short_Fopen(path, mode);
}

static FILE* Short_Fopen(const char* path, const char* mode)
{
    std::string short_path;

    if (!RISCOS_Short_Path(path, short_path)) {
        return Traced(fopen(path, mode), path, mode);
    }

    bool writing = strchr(mode, 'w') != nullptr || strchr(mode, 'a') != nullptr;

    if (writing) {
        // Create the extension directory on demand.
        std::string dir = short_path.substr(0, short_path.rfind('/'));
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            return Traced(fopen(path, mode), path, mode);
        }
        return Traced(fopen(short_path.c_str(), mode), short_path.c_str(), mode);
    }

    FILE* fp = Traced(fopen(short_path.c_str(), mode), short_path.c_str(), mode);
    if (fp == nullptr) {
        fp = Traced(fopen(path, mode), path, mode);
    }
    return fp;
}

int RISCOS_Unlink(const char* path)
{
    std::string short_path;

    if (RISCOS_Short_Path(path, short_path) && unlink(short_path.c_str()) == 0) {
        return 0;
    }
    return unlink(path);
}
#endif // USE_SHORT_FILENAMES
