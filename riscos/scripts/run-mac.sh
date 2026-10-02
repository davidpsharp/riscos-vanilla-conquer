#!/usr/bin/env bash
# Run a native macOS build of VanillaTD in portable mode from a work
# directory outside the Synology-synced tree, logging UBSan reports.
# Usage: run-mac.sh [build dir] [game args...]   e.g. run-mac.sh build/mac-ubsan -AUTOSTART=G1
# For LAN play against RISC OS, build build/mac-net (see riscos/README.md) and run
# run-mac.sh build/mac-net.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build/mac-ubsan}
shift || true
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
RUN="$WORK/mac-run"
[ -d "$WORK/tddata" ] || { echo "no game data in $WORK/tddata (see prepare-td-data.sh)" >&2; exit 1; }
mkdir -p "$RUN"
for f in "$WORK"/tddata/*.MIX; do ln -sf "$f" "$RUN/"; done
if [ ! -f "$RUN/CONQUER.INI" ]; then
    cp "$WORK/tddata/CONQUER.INI" "$RUN/"
    printf '\r\n[Video]\r\nWindowed=yes\r\n' >> "$RUN/CONQUER.INI"
fi
# Release builds are an app bundle; Debug builds a plain binary.
if [ -x "$BUILD/vanillatd.app/Contents/MacOS/vanillatd" ]; then
    cp "$BUILD/vanillatd.app/Contents/MacOS/vanillatd" "$RUN/vanillatd"
else
    cp "$BUILD/vanillatd" "$RUN/vanillatd"
fi
rm -f "$WORK"/ubsan.*
cd "$RUN"
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:log_path="$WORK/ubsan" exec ./vanillatd "$@"
