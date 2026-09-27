#!/usr/bin/env bash
# Measure the running game's logic frame rate by sampling its global Frame
# counter through the RPCEmu debugger. TD runs 15 logic frames/s at normal speed.
# Usage: game-fps.sh <371|371-1mb|530> [seconds] [elf]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DATADIR=${RPCEMU_DATADIR:-$HOME/rpcemu/rpcemu-extended}
case "$1" in
    371) NAME="VC SA RO371" ;;
    371-1mb) NAME="VC SA RO371 1MB" ;;
    530) NAME="VC SA RO530" ;;
    *) echo "unknown machine $1" >&2; exit 2 ;;
esac
SECS=${2:-20}
ELF=${3:-build/riscos/vanillatd}
SOCK="$DATADIR/machines/$NAME/rpcemu-debug.sock"
ADDR=$("$ROOT/riscos/scripts/sdk.sh" bash -c "arm-unknown-riscos-nm $ELF" | awk '$3=="Frame"{print $1}')
read_frame() {
    /Applications/RPCEmu.app/Contents/MacOS/rpcemu-debug --socket "$SOCK" mem "$ADDR" 4 | python3 -c 'import sys,json; print(int.from_bytes(bytes.fromhex(json.load(sys.stdin)["data"]),"little"))'
}
f0=$(read_frame); t0=$(python3 -c 'import time; print(time.time())')
sleep "$SECS"
f1=$(read_frame); t1=$(python3 -c 'import time; print(time.time())')
python3 -c "print('%d frames in %.1fs = %.1f logic fps (target 15)' % ($f1-$f0, $t1-$t0, ($f1-$f0)/($t1-$t0)))"
