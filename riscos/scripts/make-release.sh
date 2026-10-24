#!/usr/bin/env bash
# Build the release zip: !VanillaTD (or with RELEASE_GAME=ra, !VanillaRA) with the
# game, its tools (Utils.vcprep installs the data from CD images dragged onto the
# icon), the runtime modules and licences, but no game data.
# Filetypes are kept (see tools/riscos-zip.py).
# Usage: [RELEASE_GAME=ra] make-release.sh <version> [--no-build]
#   e.g. make-release.sh 0.1.0  ->  build/release/VanillaTD-riscos-0.1.0.zip
#
# A version like 0.1.0 asks for a release, and only a release build can have that name: the
# tree must be clean and HEAD exactly the commit tagged riscos-v0.1.0 (an annotated tag), and the
# VERSION file must say 0.1.0 (see version.sh). Then the game, its Info window, !Help and log
# say exactly 0.1.0. Otherwise this stops, unless VC_DEV_BUILD=1 is set: that builds a dev
# build (0.1.0-dev and git describe, in the zip named <version>-dev) that can't be mistaken for
# the release. Any other word (preview, test) is a dev build and is named as given.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$ROOT/work}
VERSION=${1:?usage: make-release.sh <version> [--no-build]}
# shellcheck source=riscos/scripts/version.sh
source "$ROOT/riscos/scripts/version.sh"
ZIPVERSION=$VERSION
if [[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [ "$VERSION" != "$VC_VERSION_BASE" ]; then
        echo "error: asked for release $VERSION but the VERSION file says $VC_VERSION_BASE (bump VERSION, commit and tag first)" >&2
        exit 1
    fi
    if [ "$VC_RELEASE" != 1 ]; then
        if [ "${VC_DEV_BUILD:-0}" != 1 ]; then
            echo "error: can't build release $VERSION: $VC_RELEASE_WHY." >&2
            echo "       A release is built from a clean tree at the commit tagged riscos-v$VERSION. To build a dev build" >&2
            echo "       ($VC_VERSION_FULL) anyway, set VC_DEV_BUILD=1." >&2
            exit 1
        fi
        ZIPVERSION=$VERSION-dev
        echo "note: dev build, not release $VERSION ($VC_RELEASE_WHY)" >&2
    fi
fi
OUT=$ROOT/build/release
PKG=$OUT/pkg
if [ "${RELEASE_GAME:-td}" = ra ]; then
    NAME=VanillaRA IMAGE=build/riscos-ra/vanillara,ff8 DEPLOY=deploy-ra.sh
else
    NAME=VanillaTD IMAGE=build/riscos/vanillatd,ff8 DEPLOY=deploy-td.sh
fi
APP=$PKG/!$NAME

if [ "${2:-}" != --no-build ]; then
    # deploy-td.sh / deploy-ra.sh build the game and the tools (and refresh the HostFS
    # test copy).
    "$ROOT/riscos/scripts/$DEPLOY"
fi
[ -f "$WORK/hostfs/modules/SDL-Copyright" ] || "$ROOT/riscos/scripts/fetch-runtime.sh"
# The game has to say what this script thinks it is (a --no-build of an older build would not).
if ! grep -qaF "$VC_VERSION_FULL" "$ROOT/${IMAGE%,ff8}"; then
    echo "error: ${IMAGE%,ff8} doesn't contain the version text '$VC_VERSION_FULL'; rebuild without --no-build" >&2
    exit 1
fi
if [ "$VC_RELEASE" = 1 ] && grep -qaF "$VC_VERSION_BASE-dev" "$ROOT/${IMAGE%,ff8}"; then
    echo "error: the release build contains a $VC_VERSION_BASE-dev version string" >&2
    exit 1
fi
# Everything shipped must run on an ARM610/710/7500 as well as a StrongARM.
ELVES="$ROOT/${IMAGE%,ff8} $ROOT/build/riscos-tools/vcprep"
[ "$NAME" = VanillaRA ] && ELVES="$ELVES $ROOT/build/riscos-ra/ramix"
# shellcheck disable=SC2086
"$ROOT/riscos/scripts/check-armv3.sh" $ELVES

rm -rf "$PKG"
mkdir -p "$APP/Utils" "$APP/Modules" "$APP/Licences"
cp "$ROOT"/riscos/app/\!$NAME/* "$APP/"
vc_stamp_help "$APP/!Help,fff"
cp "$ROOT/$IMAGE" "$APP/!RunImage,ff8"
cp "$ROOT/build/riscos-tools/vcprep,ff8" "$APP/Utils/"
if [ "$NAME" = VanillaRA ]; then
    cp "$ROOT/build/riscos-ra/ramix,ff8" "$APP/Utils/"
fi
cp "$WORK/hostfs/modules/SharedULib,ffa" "$WORK/hostfs/modules/DRenderer,ffa" "$APP/Modules/"
cp "$ROOT/License.txt" "$APP/Licences/GPL-3,fff"
sed -n '/^\/\* blast.h/,/\*\//p' "$ROOT/riscos/tools/vcprep/blast.h" > "$APP/Licences/Blast,fff"
{
    cat "$WORK/hostfs/modules/SharedULib-Copyright"
    cat <<'NOTE'

Note on the C library in !RunImage
----------------------------------

The C library linked into !RunImage is GCCSDK's UnixLib (the licence above) with one
change: the thread-ticker patch from https://github.com/adyoull/riscos-unixlib, an
unofficial fork of UnixLib, which stops the game crashing when it quits. It replaces
seven objects of the library (_context, _syslib, atexit, context, exec, pthinit and the
new ticker). The patch (a diff against jhamby's riscos-gccsdk, commit
64c6f814eab25486685677b1c5b5ce3d8214d82f) and the build that applies it are in the
riscos/unixlib-patch directory of the source of this program,
https://github.com/davidpsharp/riscos-vanilla-conquer (the riscos branch). The patched
parts are under UnixLib's licence, in which the LGPL overrides; the source of the whole
program is available there, so it can be relinked with another UnixLib.
NOTE
} > "$APP/Licences/UnixLib,fff"
cp "$WORK/hostfs/modules/DRenderer-Copyright" "$APP/Licences/DRenderer,fff"
cp "$WORK/hostfs/modules/SDL-Copyright" "$APP/Licences/SDL,fff"

# RISC OS 3.x FileCore truncates leaf names longer than 10 characters.
too_long=$(cd "$PKG" && find . -mindepth 1 | sed 's|,[0-9a-f][0-9a-f][0-9a-f]$||' | awk -F/ 'length($NF) > 10')
if [ -n "$too_long" ]; then
    echo "error: leaf names over 10 characters:" >&2
    echo "$too_long" >&2
    exit 1
fi

ZIP=$OUT/$NAME-riscos-$ZIPVERSION.zip
rm -f "$ZIP"
python3 "$ROOT/riscos/tools/riscos-zip.py" "$ZIP" "$APP" >/dev/null
unzip -l "$ZIP"
shasum -a 256 "$ZIP"
