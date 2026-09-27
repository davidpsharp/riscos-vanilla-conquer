#!/usr/bin/env bash
# Assemble Tiberian Dawn game data from a C&C Gold (C&C95) CD image into
# ~/vcport-work/tddata. Local testing only; the data is not redistributable.
# Usage: prepare-td-data.sh <CNC95_GDI.iso> [--movies] [--nod <CNC95_Nod.iso>]
#
# The GDI and Nod discs differ only in GENERAL.MIX and MOVIES.MIX. Those are
# also stored per disc in tddata/cd/gdi and tddata/cd/nod, which the game
# picks from depending on the side being played (see Force_CD_Available).
# Needs unshieldv3 (https://github.com/wfr/unshieldv3), built into
# build/unshieldv3-src if it isn't on PATH.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ISO=$1
shift
MOVIES=
NOD_ISO=
while [ $# -gt 0 ]; do
    case "$1" in
        --movies) MOVIES=--movies ;;
        --nod) NOD_ISO=$2; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
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

# Per-disc files.
copy_disc_files() { # <mountpoint> <side>
    mkdir -p "$OUT/cd/$2"
    cp "$1/GENERAL.MIX" "$OUT/cd/$2/"
    if [ "$MOVIES" = --movies ]; then
        cp "$1/MOVIES.MIX" "$OUT/cd/$2/"
    fi
}
copy_disc_files "$MNT" gdi
if [ -n "$NOD_ISO" ]; then
    NMNT=$(mktemp -d)
    hdiutil attach -readonly -nobrowse -mountpoint "$NMNT" "$NOD_ISO" >/dev/null
    copy_disc_files "$NMNT" nod
    hdiutil detach -quiet "$NMNT"
    rmdir "$NMNT" 2>/dev/null || true
fi
ls "$OUT" "$OUT"/cd/*
