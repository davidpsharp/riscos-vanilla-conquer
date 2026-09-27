#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "file.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <fnmatch.h>
#include <ctype.h>

/*
** Case-insensitive wildcard match. FNM_CASEFOLD is a GNU extension that some C
** libraries (e.g. RISC OS UnixLib) accept but ignore, so fold case ourselves.
*/
static bool Match_No_Case(const char* pattern, const char* name)
{
    char lpattern[PATH_MAX];
    char lname[PATH_MAX];
    size_t i;

    for (i = 0; pattern[i] != '\0' && i < sizeof(lpattern) - 1; ++i) {
        lpattern[i] = (char)tolower((unsigned char)pattern[i]);
    }
    lpattern[i] = '\0';

    for (i = 0; name[i] != '\0' && i < sizeof(lname) - 1; ++i) {
        lname[i] = (char)tolower((unsigned char)name[i]);
    }
    lname[i] = '\0';

    return fnmatch(lpattern, lname, FNM_PATHNAME) == 0;
}

class Find_File_Data_Posix : public Find_File_Data
{
public:
    Find_File_Data_Posix();
    virtual ~Find_File_Data_Posix();

    virtual const char* GetName() const;
    virtual const char* GetFullName() const
    {
        return DirEntry != nullptr ? FullName : nullptr;
    }
    virtual unsigned int GetTime() const;

    virtual bool FindFirst(const char* fname);
    virtual bool FindNext();
    virtual void Close();

private:
    DIR* Directory;
    struct dirent* DirEntry;
    const char* FileFilter;
    char FullName[PATH_MAX];
    char DirName[PATH_MAX];

    bool FindNextWithFilter();
};

Find_File_Data_Posix::Find_File_Data_Posix()
    : Directory(nullptr)
    , DirEntry(nullptr)
{
}

Find_File_Data_Posix::~Find_File_Data_Posix()
{
    Close();
}

const char* Find_File_Data_Posix::GetName() const
{
    if (DirEntry == nullptr) {
        return nullptr;
    }
    return DirEntry->d_name;
}

unsigned int Find_File_Data_Posix::GetTime() const
{
    if (DirEntry == nullptr) {
        return 0;
    }
    struct stat buf = {0};
    if (stat(FullName, &buf) != 0) {
        return false;
    }
    return buf.st_mtime;
}

bool Find_File_Data_Posix::FindNextWithFilter()
{
    while (true) {
        DirEntry = readdir(Directory);
        if (DirEntry == nullptr) {
            return false;
        }
        if (Match_No_Case(FileFilter, DirEntry->d_name)) {
            strcpy(FullName, DirName);
            strcat(FullName, DirEntry->d_name);
            break;
        }
    }
    return true;
}

bool Find_File_Data_Posix::FindFirst(const char* fname)
{
    Close();
    FullName[0] = '\0';
    DirName[0] = '\0';

    // split directory and file from the path
    char* fdir = strrchr((char*)fname, '/');
    if (fdir != nullptr) {
        strncat(DirName, fname, (fdir - fname + 1));
        FileFilter = fdir + 1;
        Directory = opendir(DirName);
    } else {
        FileFilter = fname;
        Directory = opendir(".");
    }

    if (Directory == nullptr) {
        return false;
    }

    return FindNextWithFilter();
}

bool Find_File_Data_Posix::FindNext()
{
    if (Directory == nullptr) {
        return false;
    }
    return FindNextWithFilter();
}

void Find_File_Data_Posix::Close()
{
    if (Directory != nullptr) {
        closedir(Directory);
        Directory = nullptr;
    }
}

Find_File_Data* Find_File_Data::CreateFindData()
{
    return new Find_File_Data_Posix();
}
