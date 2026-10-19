#!/usr/bin/env bash
# Run riscos/tests/membench on a Rorth target, with the caches on and then (where
# *Cache works: the Risc PC, RISC OS 3.7-4.x) off, and fetch the results.
#
#   rorth-membench.sh <target> <directory on the target>    e.g. rorth-membench.sh riscpc 'IDEFS::HardDisc4b.$.VanillaConquer'
#
# Each run is an Obey file launched as a task that writes to a file, as a long *command
# would drop Rorth's connection. Results: $OUT_DIR/<target>-membench[-cacheoff].txt.
# NO_LEASE=1: the caller holds the lease (from $RORTH/host). NO_CACHEOFF=1 skips the second run.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
RORTH=${RORTH:-$HOME/code/RORTH}
OUT_DIR=${OUT_DIR:-$WORK/profile/results}
T=${1:?target}
D=${2:?directory on the target}
mkdir -p "$OUT_DIR"
cd "$RORTH/host"
ro() { PYTHONPATH="$RORTH/host" python3 -m ro_run -t "$T" "$@"; }
[ "${NO_LEASE:-0}" = 1 ] || ro lease take --queue --timeout "${LEASE_WAIT:-7200}" --idle 900 --max 3600 --label "membench (vc-profile)" >/dev/null
cleanup()
{
    set +e
    ro cmd "Cache On" >/dev/null 2>&1
    for f in membench MBRun MBRunOff MBOut MBOutOff; do ro rm "$D.$f" >/dev/null 2>&1; done
    [ "${NO_LEASE:-0}" = 1 ] || ro lease release >/dev/null 2>&1
}
trap cleanup EXIT
ro mouse 400 300 >/dev/null 2>&1 || true
ro mouse 404 302 >/dev/null 2>&1 || true
ro put "$ROOT/build/riscos-tests/membench,ff8" "$D.membench" --type ff8 >/dev/null
tmp=$(mktemp -d)
printf 'WimpSlot -min 3000K -max 3000K\nRun %s.membench %s { > %s.MBOut }\n' "$D" "${SECS:-1}" "$D" >"$tmp/run,feb"
printf 'WimpSlot -min 3000K -max 3000K\nCache Off\nRun %s.membench %s -noscreen { > %s.MBOutOff }\nCache On\n' "$D" "${SECS:-1}" "$D" >"$tmp/off,feb"
ro put "$tmp/run,feb" "$D.MBRun" --type feb >/dev/null
ro put "$tmp/off,feb" "$D.MBRunOff" --type feb >/dev/null
run_one()
{ # obey file, output file, local name
    ro rm "$D.$2" >/dev/null 2>&1 || true
    ro launch "Obey $D.$1" >/dev/null 2>&1 || true
    for _ in $(seq 1 90); do
        sleep 10
        if ro get "$D.$2" "$OUT_DIR/$3" >/dev/null 2>&1 && grep -q "memcpy of the same 64 KB" "$OUT_DIR/$3"; then
            echo "$3:"
            cat "$OUT_DIR/$3"
            return 0
        fi
    done
    echo "$3: no result (see $OUT_DIR/$3)" >&2
}
run_one MBRun MBOut "$T-membench.txt"
[ "${NO_CACHEOFF:-0}" = 1 ] || run_one MBRunOff MBOutOff "$T-membench-cacheoff.txt"
