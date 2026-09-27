#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "file.h"
#include "riscos_fs.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <fnmatch.h>
#include <ctype.h>
#include <string>
#include <strings.h>
#include <vector>

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

#ifdef USE_SHORT_FILENAMES
/*
** RISC OS keeps "NAME.EXT" files in an EXT directory to fit 10 character
** filing systems (see riscos_fs.h). Search both plain directory entries and
** extension directories, and report matches as "NAME.EXT".
*/
class Find_File_Data_RISCOS : public Find_File_Data
{
public:
    Find_File_Data_RISCOS()
        : Index(0)
    {
    }

    virtual const char* GetName() const
    {
        return Index < Names.size() ? Names[Index].c_str() : nullptr;
    }
    virtual const char* GetFullName() const
    {
        return Index < FullNames.size() ? FullNames[Index].c_str() : nullptr;
    }
    virtual unsigned int GetTime() const
    {
        struct stat buf = {0};
        if (Index >= StoredNames.size() || stat(StoredNames[Index].c_str(), &buf) != 0) {
            return 0;
        }
        return buf.st_mtime;
    }

    virtual bool FindFirst(const char* fname);
    virtual bool FindNext()
    {
        if (Index < Names.size()) {
            ++Index;
        }
        return Index < Names.size();
    }
    virtual void Close()
    {
        Names.clear();
        FullNames.clear();
        StoredNames.clear();
        Index = 0;
    }

private:
    // name: logical NAME.EXT; dir: directory it was searched in; stored: where it actually is.
    void Add(const std::string& name, const std::string& dir, const std::string& stored);
    static bool Is_Dir(const std::string& path)
    {
        struct stat buf = {0};
        return stat(path.c_str(), &buf) == 0 && S_ISDIR(buf.st_mode);
    }

    std::vector<std::string> Names;
    std::vector<std::string> FullNames;   // Logical path, dir + NAME.EXT (used by Resolve_File).
    std::vector<std::string> StoredNames; // Path on disc, possibly dir + EXT/NAME.
    size_t Index;
};

void Find_File_Data_RISCOS::Add(const std::string& name, const std::string& dir, const std::string& stored)
{
    for (size_t i = 0; i < Names.size(); ++i) {
        if (strcasecmp(Names[i].c_str(), name.c_str()) == 0) {
            return;
        }
    }
    Names.push_back(name);
    FullNames.push_back(dir + name);
    StoredNames.push_back(stored);
}

bool Find_File_Data_RISCOS::FindFirst(const char* fname)
{
    Close();

    std::string dir;
    const char* filter = fname;
    const char* slash = strrchr(fname, '/');
    if (slash != nullptr) {
        dir.assign(fname, slash - fname + 1);
        filter = slash + 1;
    }
    std::string open_dir = dir.empty() ? std::string(".") : dir;

    const char* dot = strrchr(filter, '.');
    std::string name_filter = dot != nullptr ? std::string(filter, dot - filter) : std::string(filter);
    std::string ext_filter = dot != nullptr ? std::string(dot + 1) : std::string();

    DIR* d = opendir(open_dir.c_str());
    if (d == nullptr) {
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string full = dir + entry->d_name;

        // Plain entries, e.g. NAME/EXT on HostFS or a long filename disc.
        if (Match_No_Case(filter, entry->d_name) && !Is_Dir(full)) {
            Add(entry->d_name, dir, full);
        }

        // Extension directories holding NAME files.
        if (dot != nullptr && Match_No_Case(ext_filter.c_str(), entry->d_name) && Is_Dir(full)) {
            DIR* sub = opendir(full.c_str());
            if (sub == nullptr) {
                continue;
            }
            struct dirent* leaf;
            while ((leaf = readdir(sub)) != nullptr) {
                std::string leaf_full = full + "/" + leaf->d_name;
                if (strchr(leaf->d_name, '.') == nullptr && Match_No_Case(name_filter.c_str(), leaf->d_name)
                    && !Is_Dir(leaf_full)) {
                    Add(std::string(leaf->d_name) + "." + entry->d_name, dir, leaf_full);
                }
            }
            closedir(sub);
        }
    }
    closedir(d);

    return !Names.empty();
}

Find_File_Data* Find_File_Data::CreateFindData()
{
    return new Find_File_Data_RISCOS();
}
#else
Find_File_Data* Find_File_Data::CreateFindData()
{
    return new Find_File_Data_Posix();
}
#endif
