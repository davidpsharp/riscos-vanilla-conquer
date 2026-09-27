#!/usr/bin/env bash
# Fetch the RISC OS runtime modules our binaries need into ~/vcport-work/hostfs/modules:
#   SharedULib - SharedUnixLibrary, required by every GCCSDK program
#   DRenderer  - digital sound renderer, used by SDL audio
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="${VC_WORK:-$HOME/vcport-work}/hostfs/modules"
BASE=https://www.riscos.info/packages/arm/Develop/gcc
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for pkg in SharedUnixLibrary_1.16-1:SharedULib DRenderer_0.56-r-1b:DRenderer; do
    zip=${pkg%%:*} mod=${pkg##*:}
    curl -fsSL -o "$tmp/$zip.zip" "$BASE/$zip.zip"
    unzip -q -o -j "$tmp/$zip.zip" "System/310/Modules/$mod" -d "$tmp"
    cp "$tmp/$mod" "$OUT/$mod,ffa"
    echo "$OUT/$mod,ffa"
done
