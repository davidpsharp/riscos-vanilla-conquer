#!/usr/bin/env bash
# Reset a test machine, wait for it to boot, then launch !VanillaTD as a
# desktop task with the given arguments. Output goes to !VanillaTD.stdout/stderr.
# Usage: run-td.sh <371|530> [game args...]    e.g. run-td.sh 371 -AUTOSTART=G1
set -euo pipefail
WORK=${VC_WORK:-$HOME/vcport-work}
DATADIR=${RPCEMU_DATADIR:-$HOME/rpcemu/rpcemu-extended}
M=/Applications/RPCEmu.app/Contents/MacOS
case "$1" in
    371) NAME="VC SA RO371" PORT=5921 ;;
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
printf 'Run HostFS:$.vc.!VanillaTD %s\n' "$*" > "$WORK/hostfs/runtd,feb"
"$M/rpcemu-run" --tcp 127.0.0.1:$PORT -- 'Filer_Run HostFS:$.vc.runtd' >/dev/null
echo "launched on $NAME: $*"
