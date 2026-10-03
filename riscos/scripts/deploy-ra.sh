#!/usr/bin/env bash
# Cross-build VanillaRA and assemble the !VanillaRA application in
# ../vcport-work/hostfs next to the repo (seen by the test machines as HostFS:$.vc).
# Game data is hard-linked from ../vcport-work/ra: the Allied and Soviet discs,
# extracted to ra/CD1_ALLIES and ra/CD2_Soviet.
# Usage: [VC_DEBUG=1] deploy-ra.sh [--no-build]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
APP="$WORK/hostfs/!VanillaRA"

if [ "${VC_DEBUG:-0}" = 1 ]; then
    BDIR=build/riscos-ra-dbg EXTRA=-D_DEBUG
else
    BDIR=build/riscos-ra EXTRA=
fi

if [ -f "$ROOT/$BDIR/CMakeCache.txt" ] && [ "$ROOT/riscos/cmake/riscos-gccsdk.cmake" -nt "$ROOT/$BDIR/CMakeCache.txt" ]; then
    rm -rf "${ROOT:?}/$BDIR"
fi

if [ "${1:-}" != --no-build ]; then
    "$ROOT/riscos/scripts/sdk.sh" bash -c "
        cmake -S . -B $BDIR -G Ninja -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake \
            -DCMAKE_BUILD_TYPE=Release -DRISCOS_EXTRA_FLAGS=$EXTRA \
            -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=ON -DSDL1=ON -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=ON >/dev/null &&
        cmake --build $BDIR &&
        elf2aif $BDIR/vanillara $BDIR/vanillara,ff8"
fi

[ -d "$WORK/hostfs/modules" ] || "$ROOT/riscos/scripts/fetch-runtime.sh"
mkdir -p "$APP/Modules"
cp "$ROOT"/riscos/app/\!VanillaRA/* "$APP/"
cp "$ROOT/$BDIR/vanillara,ff8" "$APP/!RunImage,ff8"
cp "$WORK/hostfs/modules/SharedULib,ffa" "$APP/Modules/"
cp "$WORK/hostfs/modules/DRenderer,ffa" "$APP/Modules/"

# Lay the data out as RISC OS 3.x FileCore needs it (NAME.EXT as EXT.NAME, see
# common/riscos_fs.h). REDALERT.MIX is the same on both discs; each disc's
# MAIN.MIX goes in a folder named for it, where the game looks for it.
RA="$WORK/ra"
mkdir -p "$APP/MIX" "$APP/INI" "$APP/allied/MIX" "$APP/soviet/MIX"
ln -f "$RA/CD1_ALLIES/INSTALL/REDALERT.MIX" "$APP/MIX/REDALERT"
ln -f "$RA/CD1_ALLIES/MAIN.MIX" "$APP/allied/MIX/MAIN"
ln -f "$RA/CD2_Soviet/MAIN.MIX" "$APP/soviet/MIX/MAIN"

# Every leaf name in the application must fit a 10 character filing system.
too_long=$(cd "$APP" && find . -mindepth 1 | sed 's|,[0-9a-f][0-9a-f][0-9a-f]$||' | awk -F/ 'length($NF) > 10 {print}')
if [ -n "$too_long" ]; then
    echo "warning: names longer than 10 characters in !VanillaRA:" >&2
    echo "$too_long" >&2
fi
echo "deployed to $APP"
