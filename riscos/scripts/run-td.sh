#!/usr/bin/env bash
# Reset a test machine, wait for it to boot, then launch !VanillaTD as a
# desktop task with the given arguments. Output goes to !VanillaTD.stdout/stderr.
# Usage: run-td.sh <371|371-1mb|530> [game args...]    e.g. run-td.sh 371 -AUTOSTART=G1
# VC_APP overrides the application path, e.g. VC_APP='ADFS::HardDisc4.$.!VanillaTD'.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
DATADIR=${RPCEMU_DATADIR:-$HOME/rpcemu/rpcemu-extended}
M=/Applications/RPCEmu.app/Contents/MacOS
case "$1" in
    371) NAME="VC SA RO371" PORT=5921 ;;
    371-1mb) NAME="VC SA RO371 1MB" PORT=5941 ;;
    530) NAME="VC SA RO530" PORT=5931 ;;
    *) echo "unknown machine $1" >&2; exit 2 ;;
esac
shift
"$M/rpcemu-debug" --socket "$DATADIR/machines/$NAME/rpcemu-debug.sock" reset >/dev/null
sleep 3
for _ in $(seq 1 60); do
    if timeout 3 "$M/rpcemu-run" --tcp 127.0.0.1:$PORT -- 'Echo up' 2>/dev/null | grep -q '^up'; then
        break
    fi
    sleep 2
done
sleep 4 # let the desktop finish starting
rm -f "$WORK/hostfs/!VanillaTD/stdout" "$WORK/hostfs/!VanillaTD/stderr"
APP=${VC_APP:-'HostFS:$.vc.!VanillaTD'}
# VC_RUN_PRE: extra *commands (';'-separated) to run first, e.g. "Set VC_KEYLOG 1".
( IFS=';'; for c in ${VC_RUN_PRE:-}; do printf '%s\n' "$c"; done ) > "$WORK/hostfs/runtd,feb"
printf 'Run %s %s\n' "$APP" "$*" >> "$WORK/hostfs/runtd,feb"
"$M/rpcemu-run" --tcp 127.0.0.1:$PORT -- 'Filer_Run HostFS:$.vc.runtd' >/dev/null
echo "launched on $NAME: $*"
