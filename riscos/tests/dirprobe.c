/* Lists a directory the way common/file_posix.cpp does and reports which
 * entries match a pattern, to check UnixLib's readdir name translation.
 * Usage: dirprobe <dir> <pattern> */
#define _GNU_SOURCE
#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : ".";
    const char* pat = argc > 2 ? argv[2] : "*";
    DIR* d = opendir(dir);
    struct dirent* e;
    if (!d) {
        printf("opendir(%s) failed\n", dir);
        return 1;
    }
    while ((e = readdir(d)) != NULL) {
        printf("'%s' match=%d\n", e->d_name, fnmatch(pat, e->d_name, FNM_PATHNAME | FNM_CASEFOLD) == 0);
    }
    closedir(d);
    return 0;
}
