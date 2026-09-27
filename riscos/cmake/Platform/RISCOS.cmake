# Minimal platform description for RISC OS via GCCSDK/UnixLib, which presents
# a POSIX-like environment. Treat it as UNIX so posix code paths are picked.
set(UNIX 1)
set(RISCOS 1)
# Standard / , /usr and /usr/local prefixes, re-rooted under CMAKE_FIND_ROOT_PATH.
include(Platform/UnixPaths)
set(CMAKE_EXECUTABLE_SUFFIX "")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
