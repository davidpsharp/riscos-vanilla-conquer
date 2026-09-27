// RISC OS filename mapping for 10 character filing systems.
//
// FileCore on RISC OS 3.x limits leaf names to 10 characters, and UnixLib maps
// "NAME.EXT" to "NAME/EXT", so 8.3 names such as TEMPICNH.MIX don't fit. Store
// them the traditional RISC OS way instead, with the extension as a directory:
// "dir/TEMPICNH.MIX" is kept as "dir/MIX/TEMPICNH" (RISC OS "dir.MIX.TEMPICNH").
// Reads fall back to the plain name, so data on long filename systems or HostFS
// still works as copied.
#ifndef COMMON_RISCOS_FS_H
#define COMMON_RISCOS_FS_H

// Always on for RISC OS. Other POSIX builds can enable it with -DSHORT_FILENAMES=ON
// to test the scheme natively.
#if defined(__riscos__) || defined(SHORT_FILENAMES)
#define USE_SHORT_FILENAMES 1
#endif

#ifdef USE_SHORT_FILENAMES
#include <stdio.h>
#include <string>

// Fill out with the extension directory form of path and return true, or
// return false if the leaf name has no extension.
bool RISCOS_Short_Path(const char* path, std::string& out);

// fopen/unlink replacements that use the short form (see above).
FILE* RISCOS_Fopen(const char* path, const char* mode);
int RISCOS_Unlink(const char* path);
#endif

#endif // COMMON_RISCOS_FS_H
