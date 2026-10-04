#!/usr/bin/env bash
# Build the release zip: !VanillaTD (or with RELEASE_GAME=ra, !VanillaRA) with the
# game, the runtime modules and licences, and Prepare next to it, but no game data.
# Filetypes are kept (see tools/riscos-zip.py).
# Usage: [RELEASE_GAME=ra] make-release.sh <version> [--no-build]
#   e.g. make-release.sh preview  ->  build/release/VanillaTD-riscos-preview.zip
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
VERSION=${1:?usage: make-release.sh <version> [--no-build]}
OUT=$ROOT/build/release
PKG=$OUT/pkg
if [ "${RELEASE_GAME:-td}" = ra ]; then
    NAME=VanillaRA IMAGE=build/riscos-ra/vanillara,ff8 DEPLOY=deploy-ra.sh
else
    NAME=VanillaTD IMAGE=build/riscos/vanillatd,ff8 DEPLOY=deploy-td.sh
fi
APP=$PKG/!$NAME

if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]; then
    echo "warning: uncommitted changes; the title screen will show a ~ version" >&2
fi
if [ "${2:-}" != --no-build ]; then
    # deploy-td.sh / deploy-ra.sh build the game and the tools (and refresh the HostFS
    # test copy).
    "$ROOT/riscos/scripts/$DEPLOY"
fi
[ -f "$WORK/hostfs/modules/DRenderer-Copyright" ] || "$ROOT/riscos/scripts/fetch-runtime.sh"

rm -rf "$PKG"
mkdir -p "$APP/Utils" "$APP/Modules" "$APP/Licences"
cp "$ROOT"/riscos/app/\!$NAME/* "$APP/"
cp "$ROOT/riscos/app/Prepare,feb" "$PKG/"
cp "$ROOT/$IMAGE" "$APP/!RunImage,ff8"
cp "$ROOT/build/riscos-tools/vcprep,ff8" "$APP/Utils/"
if [ "$NAME" = VanillaRA ]; then
    cp "$ROOT/build/riscos-ra/ramix,ff8" "$APP/Utils/"
fi
cp "$WORK/hostfs/modules/SharedULib,ffa" "$WORK/hostfs/modules/DRenderer,ffa" "$APP/Modules/"
cp "$ROOT/License.txt" "$APP/Licences/GPL-3,fff"
sed -n '/^\/\* blast.h/,/\*\//p' "$ROOT/riscos/tools/vcprep/blast.h" > "$APP/Licences/Blast,fff"
cp "$WORK/hostfs/modules/SharedULib-Copyright" "$APP/Licences/UnixLib,fff"
cp "$WORK/hostfs/modules/DRenderer-Copyright" "$APP/Licences/DRenderer,fff"

# RISC OS 3.x FileCore truncates leaf names longer than 10 characters.
too_long=$(cd "$PKG" && find . -mindepth 1 | sed 's|,[0-9a-f][0-9a-f][0-9a-f]$||' | awk -F/ 'length($NF) > 10')
if [ -n "$too_long" ]; then
    echo "error: leaf names over 10 characters:" >&2
    echo "$too_long" >&2
    exit 1
fi

ZIP=$OUT/$NAME-riscos-$VERSION.zip
rm -f "$ZIP"
python3 "$ROOT/riscos/tools/riscos-zip.py" "$ZIP" "$APP" "$PKG/Prepare,feb" >/dev/null
unzip -l "$ZIP"
shasum -a 256 "$ZIP"
