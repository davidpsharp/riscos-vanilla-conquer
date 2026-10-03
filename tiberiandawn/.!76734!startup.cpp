//
// Copyright 2020 Electronic Arts Inc.
//
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

/* $Header:   F:\projects\c&c\vcs\code\startup.cpv   2.17   16 Oct 1995 16:48:12   JOE_BOSTIC  $ */
/***********************************************************************************************
 ***             C O N F I D E N T I A L  ---  W E S T W O O D   S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : STARTUP.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : October 3, 1994                                              *
 *                                                                                             *
 *                  Last Update : August 27, 1995 [JLB]                                        *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Prog_End -- Cleans up library systems in prep for game exit.                              *
 *   main -- Initial startup routine (preps library systems).                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "function.h"
#include "common/gitinfo.h"
#include "common/ini.h"
#include "common/paths.h"
#include "common/utfargs.h"
#include "settings.h"

bool Read_Private_Config_Struct(FileClass& file, NewConfigType* config);
void Print_Error_End_Exit(char* string);
void Print_Error_Exit(char* string);
#ifdef _WIN32
#include <direct.h>
#include "common/utf.h"
extern void Create_Main_Window(HANDLE instance, int width, int height);
HINSTANCE ProgramInstance;
#else
#include <unistd.h>
#endif

extern int ReadyToQuit;
void Read_Setup_Options(RawFileClass* config_file);

bool VideoBackBufferAllowed = true;
void Check_From_WChat(char* wchat_name);
bool ProgEndCalled = false;

/***********************************************************************************************
 * main -- Initial startup routine (preps library systems).                                    *
 *                                                                                             *
 *    This is the routine that is first called when the program starts up. It basically        *
 *    handles the command line parsing and setting up library systems.                         *
 *                                                                                             *
 * INPUT:   argc  -- Number of command line arguments.                                         *
 *                                                                                             *
 *          argv  -- Pointer to array of comman line argument strings.                         *
 *                                                                                             *
 * OUTPUT:  Returns with execution failure code (if any).                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/20/1995 JLB : Created.                                                                 *
 *=============================================================================================*/

void Move_Point(short& x, short& y, register DirType dir, unsigned short distance);

void Check_Use_Compressed_Shapes(void);
extern void DLL_Shutdown(void);

#if defined REMASTER_BUILD && defined _WIN32
BOOL WINAPI DllMain(HINSTANCE instance, unsigned int fdwReason, void* lpvReserved)
{
    lpvReserved;

    switch (fdwReason) {

    case DLL_PROCESS_ATTACH:
        ProgramInstance = instance;
        break;

    case DLL_PROCESS_DETACH:
        DLL_Shutdown();

        MFCD::Free_All();

        Uninit_Game();

        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return true;
}
#endif

#ifdef REMASTER_BUILD
int main(int, char*[]);

int DLL_Startup(const char* command_line_in)
{
    /* Construct argc and argv from command_line_in. Remaster build requires
    ** that first argument is a full path to DLL, not executable. Furthermore, it
    ** seems to override the argv to include extra parameters which the DLL
    ** expects. Getting the argc and argv from executable will result in
    ** a crash trying to read the font files. */

    RunningAsDLL = true;
    HINSTANCE instance = ProgramInstance;
    char command_line[1024];
    int argc = 0;
    unsigned command_scan;
    char command_char;
    char* argv[20];
    char path_to_exe[280];

    strcpy(command_line, command_line_in);
    ProgramInstance = instance;

    /*
    ** Get the full path to the .DLL
    */
    DWORD readed = GetModuleFileNameA(instance, &path_to_exe[0], 280);
    if (readed >= 280 - 1) {
        MessageBoxA(NULL, "Path to remaster is too large.", "Command & Conquer", MB_ICONEXCLAMATION | MB_OK);
        return -1;
    }

    /*
    ** First argument is supposed to be a pointer to the .EXE that is running
    ** - False. Must be a pointer to the DLL - giulianob 07/11/2021
    **
    */
    argc = 1;                  // Set argument count to 1
    argv[0] = &path_to_exe[0]; // Set 1st command line argument to point to full path

    /*
    ** Get pointers to command line arguments just like if we were in DOS
    **
    ** The command line we get is cr/zero? terminated.
    **
    */

    command_scan = 0;

    /* This certainly can be improved, but worse than this is not working :)*/

    do {
        /*
        ** Scan for non-space character on command line
        */
        do {
            command_char = *(command_line + command_scan++);
        } while (command_char == ' ');

        if (command_char != 0 && command_char != 13) {
            argv[argc++] = command_line + command_scan - 1;

            /*
            ** Scan for space character on command line
            */
            bool in_quotes = false;
            do {
                command_char = *(command_line + command_scan++);
                if (command_char == '"') {
                    in_quotes = !in_quotes;
                }
            } while ((in_quotes || command_char != ' ') && command_char != 0 && command_char != 13);

            *(command_line + command_scan - 1) = 0;
        }

    } while (command_char != 0 && command_char != 13 && argc < 20);

    if (argc >= 20) {
        MessageBoxA(NULL, "Too many arguments on command line.", "Command & Conquer", MB_ICONEXCLAMATION | MB_OK);
        return -1;
    }

    return main(argc, argv);
}
#endif // REMASTER_BUILD

int main(int argc, char** argv)
{
#ifdef __riscos__
    // Output is usually redirected to a file by !Run; don't lose it on a crash.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (getenv("VC_FPSLOG") != nullptr) {
        /*
        ** Except when logging timings: then every line was a disc write, interleaved
        ** with the music stream's reads from the same disc. Flushed every 5 seconds
        ** with the report (Phase_Report), so a crash loses at most that.
        */
        static char stderr_buffer[16384];
        setvbuf(stderr, stderr_buffer, _IOFBF, sizeof(stderr_buffer));
    }
#ifdef USE_SHORT_FILENAMES
    if (getenv("VC_FSTEST") != nullptr) {
        void RISCOS_Fs_Self_Test();
        RISCOS_Fs_Self_Test();
        return 0;
    }
#endif
    if (getenv("VC_BLITTEST") != nullptr) {
        void Blit_Self_Test();
        Blit_Self_Test();
        return 0;
    }
    // Record which build wrote this log.
    fprintf(stderr, "Vanilla Conquer TD %s%s built %s\n", GitUncommittedChanges ? "~" : "", GitShortSHA1, BuildStamp);
#endif
    UtfArgs args(argc, argv);
    CCDebugString("C&C95 - Starting up.\n");

    if (Ram_Free(MEM_NORMAL) < 5000000) {
#ifdef GERMAN
        printf("Zuwenig Hauptspeicher verf?gbar.\n");
#else
#ifdef FRENCH
