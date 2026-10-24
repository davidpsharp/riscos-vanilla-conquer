#!/usr/bin/env bash
# Cross-build VanillaRA and assemble the !VanillaRA application in
# work/hostfs in the checkout (seen by the test machines as HostFS:$.vc).
# Game data is hard-linked from work/ra: the Allied and Soviet discs,
# extracted to ra/CD1_ALLIES and ra/CD2_Soviet.
# Usage: [VC_DEBUG=1] deploy-ra.sh [--no-build]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
# VC_VERSION_* : the version this build says it is (see version.sh).
# shellcheck source=riscos/scripts/version.sh
source "$(dirname "$0")/version.sh"
WORK=${VC_WORK:-$ROOT/work}
APP="$WORK/hostfs/!VanillaRA"

if [ "${VC_DEBUG:-0}" = 1 ]; then
    BDIR=build/riscos-ra-dbg EXTRA=-D_DEBUG
else
    BDIR=build/riscos-ra EXTRA=
fi

if [ -f "$ROOT/$BDIR/CMakeCache.txt" ] && [ "$ROOT/riscos/cmake/riscos-gccsdk.cmake" -nt "$ROOT/$BDIR/CMakeCache.txt" ]; then
    rm -rf "${ROOT:?}/$BDIR"
fi

# The build vcprep names in its install log (git can't see the repo from inside the container).
TOOLS_BUILD=$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || date +%Y%m%d)
if [ "${1:-}" != --no-build ]; then
    "$ROOT/riscos/scripts/sdk.sh" bash -c "
        cmake -S . -B $BDIR -G Ninja -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake \
            -DCMAKE_BUILD_TYPE=Release -DRISCOS_EXTRA_FLAGS=$EXTRA \
            -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=ON -DSDL1=ON -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=ON $VC_CMAKE_VERSION >/dev/null &&
        cmake --build $BDIR &&
        elf2aif $BDIR/vanillara $BDIR/vanillara,ff8 &&
        cmake -S riscos/tools -B build/riscos-tools -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=/work/riscos/cmake/riscos-gccsdk.cmake -DCMAKE_BUILD_TYPE=Release \
            -DTOOLS_BUILD=$TOOLS_BUILD >/dev/null &&
        cmake --build build/riscos-tools"
fi

[ -d "$WORK/hostfs/modules" ] || "$ROOT/riscos/scripts/fetch-runtime.sh"
mkdir -p "$APP/Modules"
cp "$ROOT"/riscos/app/\!VanillaRA/* "$APP/"
vc_stamp_help "$APP/!Help,fff"
cp "$ROOT/$BDIR/vanillara,ff8" "$APP/!RunImage,ff8"
mkdir -p "$APP/Utils"
cp "$ROOT/build/riscos-tools/vcprep,ff8" "$APP/Utils/"
cp "$ROOT/$BDIR/ramix,ff8" "$APP/Utils/"
cp "$WORK/hostfs/modules/SharedULib,ffa" "$APP/Modules/"
cp "$WORK/hostfs/modules/DRenderer,ffa" "$APP/Modules/"

# Lay the data out as RISC OS 3.x FileCore needs it (NAME.EXT as EXT.NAME, see
# common/riscos_fs.h). REDALERT.MIX is the same on both discs; each disc's
# MAIN.MIX goes in a folder named for it, where the game looks for it.
RA="$WORK/ra"
# The data goes in gamedata, as installing from the CD images puts it.
DATA="$APP/gamedata"
mkdir -p "$DATA/MIX" "$DATA/INI" "$DATA/allied/MIX" "$DATA/soviet/MIX"
ln -f "$RA/CD1_ALLIES/INSTALL/REDALERT.MIX" "$DATA/MIX/REDALERT"
ln -f "$RA/CD1_ALLIES/MAIN.MIX" "$DATA/allied/MIX/MAIN"
ln -f "$RA/CD2_Soviet/MAIN.MIX" "$DATA/soviet/MIX/MAIN"

# Every leaf name in the application must fit a 10 character filing system.
too_long=$(cd "$APP" && find . -mindepth 1 | sed 's|,[0-9a-f][0-9a-f][0-9a-f]$||' | awk -F/ 'length($NF) > 10 {print}')
if [ -n "$too_long" ]; then
    echo "warning: names longer than 10 characters in !VanillaRA:" >&2
    echo "$too_long" >&2
fi
echo "deployed to $APP"
