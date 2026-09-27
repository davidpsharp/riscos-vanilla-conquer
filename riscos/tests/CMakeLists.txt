# Phase 0 smoke tests for the RISC OS toolchain. Build with riscos/scripts/build-tests.sh.
cmake_minimum_required(VERSION 3.25)
project(RiscosSmokeTests C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello hello.cpp)
add_executable(unaligned unaligned.c)

find_package(SDL REQUIRED)
add_executable(sdltest sdltest.cpp)
target_include_directories(sdltest PRIVATE ${SDL_INCLUDE_DIR})
target_link_libraries(sdltest PRIVATE ${SDL_LIBRARY})

if(RISCOS)
    foreach(t hello unaligned sdltest)
        riscos_make_aif(${t})
    endforeach()
endif()
