#!/usr/bin/env bash
# Assemble Tiberian Dawn game data from a C&C Gold (C&C95) CD image into
# build/tddata. Local testing only; the data is not redistributable.
# Usage: prepare-td-data.sh <CNC95_GDI.iso> [--movies]
# Needs unshieldv3 (https://github.com/wfr/unshieldv3), built into
# build/unshieldv3-src if it isn't on PATH.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ISO=$1
MOVIES=${2:-}
OUT="${VC_WORK:-$HOME/vcport-work}/tddata"
MNT=$(mktemp -d)

UNSHIELD=$(command -v unshieldv3 || true)
if [ -z "$UNSHIELD" ]; then
    SRC="$ROOT/build/unshieldv3-src"
    [ -d "$SRC" ] || git clone -q --depth 1 https://github.com/wfr/unshieldv3 "$SRC"
    cmake -S "$SRC" -B "$SRC/b" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$SRC/b" >/dev/null
    UNSHIELD="$SRC/b/unshieldv3"
fi

hdiutil attach -readonly -nobrowse -mountpoint "$MNT" "$ISO" >/dev/null
trap 'hdiutil detach -quiet "$MNT"; rmdir "$MNT" 2>/dev/null || true' EXIT

mkdir -p "$OUT"
for f in "$MNT"/*.MIX; do
    name=$(basename "$f")
    if [ "$name" = MOVIES.MIX ] && [ "$MOVIES" != --movies ]; then
        continue
    fi
    cp "$f" "$OUT/"
done

# The installer archive holds the remaining MIX files and CONQUER.INI.
tmp=$(mktemp -d)
"$UNSHIELD" extract "$MNT/INSTALL/SETUP.Z" "$tmp" >/dev/null
find "$tmp" -type f \( -iname '*.MIX' -o -iname 'CONQUER.INI' \) -exec cp {} "$OUT/" \;
rm -rf "$tmp"
ls "$OUT"
