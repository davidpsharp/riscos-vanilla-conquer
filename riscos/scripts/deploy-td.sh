#!/usr/bin/env bash
# Cross-build VanillaTD and assemble the !VanillaTD application in
# ~/vcport-work/hostfs (seen by the test machines as HostFS:$.vc).
# Game data is hard-linked from ~/vcport-work/tddata (see prepare-td-data.sh).
# Usage: [VC_DEBUG=1] deploy-td.sh [--no-build]
#   VC_DEBUG=1 builds with -D_DEBUG so the game's debug log goes to stdout.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$HOME/vcport-work}
APP="$WORK/hostfs/!VanillaTD"

if [ "${VC_DEBUG:-0}" = 1 ]; then
    BDIR=build/riscos-dbg EXTRA=-D_DEBUG
else
    BDIR=build/riscos EXTRA=
fi

# CMAKE_<LANG>_FLAGS_INIT from the toolchain file only applies to a fresh
# cache, so start over whenever the toolchain file has changed.
if [ -f "$ROOT/$BDIR/CMakeCache.txt" ] && [ "$ROOT/riscos/cmake/riscos-gccsdk.cmake" -nt "$ROOT/$BDIR/CMakeCache.txt" ]; then
    rm -rf "${ROOT:?}/$BDIR"
fi

if [ "${1:-}" != --no-build ]; then
    "$ROOT/riscos/scripts/sdk.sh" bash -c "
        cmake -S . -B $BDIR -G Ninja -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake \
            -DCMAKE_BUILD_TYPE=Release -DRISCOS_EXTRA_FLAGS=$EXTRA \
            -DBUILD_VANILLARA=OFF -DSDL1=ON -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF >/dev/null &&
        cmake --build $BDIR &&
        elf2aif $BDIR/vanillatd $BDIR/vanillatd,ff8"
fi

[ -d "$WORK/hostfs/modules" ] || "$ROOT/riscos/scripts/fetch-runtime.sh"
mkdir -p "$APP/Modules"
cp "$ROOT"/riscos/app/\!VanillaTD/* "$APP/"
cp "$ROOT/$BDIR/vanillatd,ff8" "$APP/!RunImage,ff8"
cp "$WORK/hostfs/modules/SharedULib,ffa" "$APP/Modules/"
# Lay the data out as RISC OS 3.x FileCore needs it: leaf names of at most
# 10 characters, with NAME.EXT stored as EXT.NAME (see common/riscos_fs.h).
mkdir -p "$APP/MIX" "$APP/INI"
for f in "$WORK"/tddata/*.MIX; do
    name=$(basename "$f" .MIX)
    ln -f "$f" "$APP/MIX/$name"
    rm -f "$APP/$name.MIX" # older long-name layout
done
if [ ! -f "$APP/INI/CONQUER" ]; then
    if [ -f "$APP/CONQUER.INI" ]; then
        mv "$APP/CONQUER.INI" "$APP/INI/CONQUER"
    else
        cp "$WORK/tddata/CONQUER.INI" "$APP/INI/CONQUER"
    fi
fi

# Every leaf name in the application must fit a 10 character filing system.
too_long=$(cd "$APP" && find . -mindepth 1 | sed 's|,[0-9a-f][0-9a-f][0-9a-f]$||' | awk -F/ 'length($NF) > 10 {print}')
if [ -n "$too_long" ]; then
    echo "warning: names longer than 10 characters in !VanillaTD:" >&2
    echo "$too_long" >&2
fi
echo "deployed to $APP"
