#!/usr/bin/env bash
# Cross-build the smoke tests into build/riscos-tests.
set -euo pipefail
exec "$(dirname "$0")/sdk.sh" bash -c '
    cmake -S riscos/tests -B build/riscos-tests -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake -DCMAKE_BUILD_TYPE=Release &&
    cmake --build build/riscos-tests'
