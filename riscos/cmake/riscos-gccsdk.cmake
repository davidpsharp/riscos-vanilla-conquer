# CMake toolchain file for GCCSDK 4.7.4 (arm-unknown-riscos), used inside the
# vc-riscos-sdk container (see riscos/docker/Dockerfile).
#
# RISCOS_CPU selects the code generation target. The default is StrongARM
# (ARMv4), the fastest CPU in a Risc PC.
set(CMAKE_SYSTEM_NAME RISCOS)
set(CMAKE_SYSTEM_PROCESSOR arm)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(GCCSDK_CROSSBIN /home/riscos/cross/bin)
set(GCCSDK_ENV /home/riscos/env)

set(CMAKE_C_COMPILER ${GCCSDK_CROSSBIN}/arm-unknown-riscos-gcc)
set(CMAKE_CXX_COMPILER ${GCCSDK_CROSSBIN}/arm-unknown-riscos-g++)
set(RISCOS_ELF2AIF ${GCCSDK_CROSSBIN}/elf2aif)

set(RISCOS_CPU strongarm CACHE STRING "Target CPU passed to -mcpu")
# Extra C/C++ flags, e.g. -Wcast-align. Use this rather than CMAKE_CXX_FLAGS,
# which would replace the flags below.
set(RISCOS_EXTRA_FLAGS "" CACHE STRING "Extra compiler flags for C and C++")

# -fsigned-char: the game code was written for x86, where plain char is signed.
# -mstructure-size-boundary=8: GCCSDK defaults to 32 (APCS), which pads every
#   struct to a multiple of 4 bytes, so struct {char[13]} is 16 bytes and
#   one-byte bitfield structs and short/char unions grow to 4. The game relies
#   on x86 sizes. Library structs containing an int or pointer lay out the same
#   either way, so this stays compatible with UnixLib and SDL.
set(RISCOS_ABI_FLAGS "-mcpu=${RISCOS_CPU} -fsigned-char -mstructure-size-boundary=8")
set(CMAKE_C_FLAGS_INIT "${RISCOS_ABI_FLAGS} ${RISCOS_EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT
    "${RISCOS_ABI_FLAGS} -include ${CMAKE_CURRENT_LIST_DIR}/../compat/cxx11_compat.h ${RISCOS_EXTRA_FLAGS}")
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
