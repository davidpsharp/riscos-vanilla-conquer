// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection
#ifndef COMMON_GITINFO_H
#define COMMON_GITINFO_H

#include <time.h>

extern const char GitSHA1[];
extern const char GitShortSHA1[];
extern const char GitCommitDate[];
extern const char GitCommitAuthorName[];
extern const char GitTag[];
extern time_t GitCommitTimeStamp;
extern bool GitUncommittedChanges;
extern bool GitHaveInfo;
extern int GitRevision;

// When this binary was built, e.g. "2026-09-27 21:05 UTC".
extern const char BuildStamp[];

// The version of this build: "0.1.0" for the release, "0.1.0-dev" for anything else (VERSION, riscos/scripts/version.sh).
extern const char VC_Version[];
extern const char VC_Version_Date[];     // the commit's date, e.g. "20 Oct 2026"
extern const char VC_Version_Describe[]; // git describe for a dev build, "" for the release
extern const char VC_Version_Full[];     // "0.1.0 (20 Oct 2026)", or "0.1.0-dev v0.1.0-3-gabc1234 (20 Oct 2026)"
extern const char VC_Version_Title[];    // "0.1.0", or "0.1.0-dev v0.1.0-3-gabc1234": for the title screen

#endif
