# Writes buildstamp.cpp with the current UTC date and time. Run on every build
# (see common/CMakeLists.txt) so the binary always records when it was built.
string(TIMESTAMP BUILD_STAMP "%Y-%m-%d %H:%M UTC" UTC)
set(CONTENT "#include \"gitinfo.h\"\n\nconst char BuildStamp[] = \"${BUILD_STAMP}\";\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" OLD)
endif()
if(NOT "${OLD}" STREQUAL "${CONTENT}")
    file(WRITE "${OUT}" "${CONTENT}")
endif()
