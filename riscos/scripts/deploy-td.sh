#!/usr/bin/env bash
# Cross-build VanillaTD and assemble the !VanillaTD application in
# ../vcport-work/hostfs next to the repo (seen by the test machines as HostFS:$.vc).
# Game data is hard-linked from ../vcport-work/tddata (see prepare-td-data.sh).
# Usage: [VC_DEBUG=1] deploy-td.sh [--no-build]
#   VC_DEBUG=1 builds with -D_DEBUG so the game's debug log goes to stdout.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
APP="$WORK/hostfs/!VanillaTD"

if [ "${VC_DEBUG:-0}" = 1 ]; then
    BDIR=build/riscos-dbg EXTRA=-D_DEBUG
else
    BDIR=build/riscos EXTRA=
fi

# CMAKE_<LANG>_FLAGS_INIT from the toolchain file only applies to a fresh
# cache, so start over whenever the toolchain file has changed.
for dir in "$BDIR" build/riscos-tools; do
    if [ -f "$ROOT/$dir/CMakeCache.txt" ] && [ "$ROOT/riscos/cmake/riscos-gccsdk.cmake" -nt "$ROOT/$dir/CMakeCache.txt" ]; then
        rm -rf "${ROOT:?}/$dir"
    fi
done

if [ "${1:-}" != --no-build ]; then
    "$ROOT/riscos/scripts/sdk.sh" bash -c "
        cmake -S . -B $BDIR -G Ninja -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake \
            -DCMAKE_BUILD_TYPE=Release -DRISCOS_EXTRA_FLAGS=$EXTRA \
            -DBUILD_VANILLARA=OFF -DSDL1=ON -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=ON >/dev/null &&
        cmake --build $BDIR &&
        elf2aif $BDIR/vanillatd $BDIR/vanillatd,ff8 &&
        cmake -S riscos/tools -B build/riscos-tools -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null &&
        cmake --build build/riscos-tools"
fi

[ -d "$WORK/hostfs/modules" ] || "$ROOT/riscos/scripts/fetch-runtime.sh"
mkdir -p "$APP/Modules"
cp "$ROOT"/riscos/app/\!VanillaTD/* "$APP/"
# Prepare sits next to !VanillaTD, as in the release zip.
rm -f "$APP/Prepare,feb"
cp "$ROOT/riscos/app/Prepare,feb" "$APP/../"
cp "$ROOT/$BDIR/vanillatd,ff8" "$APP/!RunImage,ff8"
mkdir -p "$APP/Utils"
cp "$ROOT/build/riscos-tools/vcprep,ff8" "$APP/Utils/"
cp "$WORK/hostfs/modules/SharedULib,ffa" "$APP/Modules/"
cp "$WORK/hostfs/modules/DRenderer,ffa" "$APP/Modules/"
# Lay the data out as RISC OS 3.x FileCore needs it: leaf names of at most
# 10 characters, with NAME.EXT stored as EXT.NAME (see common/riscos_fs.h).
mkdir -p "$APP/MIX" "$APP/INI"
HAVE_DISCS=
[ -d "$WORK/tddata/cd" ] && HAVE_DISCS=1
for f in "$WORK"/tddata/*.MIX; do
    name=$(basename "$f" .MIX)
    rm -f "$APP/$name.MIX" # older long-name layout
    # With per-disc data, GENERAL and MOVIES come from the gdi/nod directories
    # only, or the top-level copies would shadow the other side's.
    if [ -n "$HAVE_DISCS" ] && { [ "$name" = GENERAL ] || [ "$name" = MOVIES ]; }; then
        rm -f "$APP/MIX/$name"
        continue
    fi
    ln -f "$f" "$APP/MIX/$name"
done
if [ -n "$HAVE_DISCS" ]; then
    for disc in "$WORK"/tddata/cd/*; do
        side=$(basename "$disc")
        mkdir -p "$APP/$side/MIX"
        for f in "$disc"/*.MIX; do
            ln -f "$f" "$APP/$side/MIX/$(basename "$f" .MIX)"
        done
    done
fi
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
