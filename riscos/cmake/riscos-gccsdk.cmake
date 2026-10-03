# CMake toolchain file for GCCSDK 4.7.4 (arm-unknown-riscos), used inside the
# vc-riscos-sdk container (see riscos/docker/Dockerfile).
#
# RISCOS_ARCH and RISCOS_TUNE select code generation: ARMv3M instructions,
# scheduled for the StrongARM, the fastest CPU in a Risc PC.
set(CMAKE_SYSTEM_NAME RISCOS)
set(CMAKE_SYSTEM_PROCESSOR arm)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(GCCSDK_CROSSBIN /home/riscos/cross/bin)
set(GCCSDK_ENV /home/riscos/env)

set(CMAKE_C_COMPILER ${GCCSDK_CROSSBIN}/arm-unknown-riscos-gcc)
set(CMAKE_CXX_COMPILER ${GCCSDK_CROSSBIN}/arm-unknown-riscos-g++)
set(RISCOS_ELF2AIF ${GCCSDK_CROSSBIN}/elf2aif)

# -march=armv3m: no halfword loads/stores (LDRH/STRH, ARMv4). The Risc PC's
#   memory system predates them: on a real StrongARM Risc PC, STRH to heap
#   memory left the upper byte unwritten, corrupting 16-bit data (the keyboard
#   queue returned garbage mouse clicks). RPCEmu emulates halfwords correctly,
#   so only real hardware showed it. GCCSDK's own libraries are built the same way.
# -mtune=strongarm still schedules for the StrongARM.
set(RISCOS_ARCH armv3m CACHE STRING "Target architecture passed to -march")
set(RISCOS_TUNE strongarm CACHE STRING "CPU to tune for, passed to -mtune")
# Extra C/C++ flags, e.g. -Wcast-align. Use this rather than CMAKE_CXX_FLAGS,
# which would replace the flags below.
set(RISCOS_EXTRA_FLAGS "" CACHE STRING "Extra compiler flags for C and C++")

# -fsigned-char: the game code was written for x86, where plain char is signed.
# -mstructure-size-boundary=8: GCCSDK defaults to 32 (APCS), which pads every
#   struct to a multiple of 4 bytes, so struct {char[13]} is 16 bytes and
#   one-byte bitfield structs and short/char unions grow to 4. The game relies
#   on x86 sizes. Library structs containing an int or pointer lay out the same
#   either way, so this stays compatible with UnixLib and SDL.
set(RISCOS_ABI_FLAGS "-march=${RISCOS_ARCH} -mtune=${RISCOS_TUNE} -fsigned-char -mstructure-size-boundary=8")
set(CMAKE_C_FLAGS_INIT "${RISCOS_ABI_FLAGS} ${RISCOS_EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT
    "${RISCOS_ABI_FLAGS} -include ${CMAKE_CURRENT_LIST_DIR}/../compat/cxx11_compat.h ${RISCOS_EXTRA_FLAGS}")
# -O2, not CMake's Release default of -O3: on a StrongARM Risc PC the -O3 build
#   was 25% slower (77.2 against 61.7 ms per frame in riscos/bench's battle),
#   presumably as its larger code fits the 16 KB instruction cache worse. -O1
#   (65.0 ms) and -O2 with -flto (61.6 ms) were no better; -Os crashes GCC 4.7.
#   (The _INIT variables would get CMake's -O3 appended, so set the cache.)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C flags for Release builds")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C++ flags for Release builds")
# Static link so the binary can be converted to an AIF absolute and run on
# RISC OS 3.7 without SharedLibs.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH ${GCCSDK_ENV})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Convert an ELF executable target into a RISC OS absolute (<name>,ff8) next to it.
function(riscos_make_aif target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${RISCOS_ELF2AIF} $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${target},ff8
        COMMENT "elf2aif ${target}")
endfunction()
