#!/usr/bin/env bash
# Run the battle benchmark on a Rorth target (normally the real Risc PC).
#
#   rorth-bench.sh [frames] [seed]       e.g. rorth-bench.sh 1200 1
#
# Sends build/riscos/vanillatd,ff8 as !RunImage, installs the benchmark battle
# as a loose INI.SCG01EA (it overrides GDI mission 1), sets VC_BENCH and
# VC_SEED, launches GDI 1, waits for the game to finish and exit, and prints its
# "bench" lines. Afterwards it removes the battle and unsets the variables, so
# the installed game is as it was, and releases the lease.
#
# The battle, ../vcport-work/bench/SCG01EA-battle.INI, is made from the GDI disc's
# GENERAL.MIX by riscos/bench/make-battle.py when it's missing: GDI 1 with 57
# vehicles and 80 infantry set to Hunt and no win or lose triggers.
#
# RO_TARGET (default riscpc) and VC_APP (the !VanillaTD path there) override;
# BENCH_IMAGE picks the program to test (default build/riscos/vanillatd,ff8), and
# BENCH_VARS="NAME=value ..." sets more variables for the run (unset afterwards).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
RO=${RO_RUN:-/Users/david/code/RORTH/tools/ro-run}
T=${RO_TARGET:-riscpc}
APP=${VC_APP:-'IDEFS::HardDisc4b.$.VanillaConquer.!VanillaTD'}
FRAMES=${1:-1200}
SEED=${2:-1}
OUT=${BENCH_OUT:-$WORK/bench/last-$T.txt}

cd "$ROOT" # so the lease is this project's (.ro-run/)
BATTLE=$WORK/bench/SCG01EA-battle.INI
if [ ! -f "$BATTLE" ]; then
    mkdir -p "$WORK/bench"
    python3 "$ROOT/riscos/bench/make-battle.py" "$WORK/tddata/cd/gdi/GENERAL.MIX" "$BATTLE"
fi
"$RO" -t "$T" lease take --idle 900 --max 3600 >/dev/null
cleanup()
{
    "$RO" -t "$T" cmd 'Unset VC_BENCH' >/dev/null 2>&1 || true
    "$RO" -t "$T" cmd 'Unset VC_SEED' >/dev/null 2>&1 || true
    for v in ${BENCH_VARS:-}; do
        "$RO" -t "$T" cmd "Unset ${v%%=*}" >/dev/null 2>&1 || true
    done
    "$RO" -t "$T" rm "$APP.INI.SCG01EA" >/dev/null 2>&1 || true
    "$RO" -t "$T" lease release >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$RO" -t "$T" put "${BENCH_IMAGE:-build/riscos/vanillatd,ff8}" "$APP.!RunImage" --type ff8 >/dev/null
"$RO" -t "$T" put "$BATTLE" "$APP.INI.SCG01EA" --type fff >/dev/null
"$RO" -t "$T" cmd "Set VC_BENCH $FRAMES" >/dev/null
"$RO" -t "$T" cmd "Set VC_SEED $SEED" >/dev/null
for v in ${BENCH_VARS:-}; do
    "$RO" -t "$T" cmd "Set ${v%%=*} ${v#*=}" >/dev/null
done
"$RO" -t "$T" launch "Run $APP -AUTOSTART=G1" >/dev/null

# The game holds stderr open until it exits; fetching it succeeds once it has.
for _ in $(seq 1 120); do
    sleep 10
    if "$RO" -t "$T" get "$APP.stderr" "$OUT" >/dev/null 2>&1 && grep -q "^bench objects" "$OUT"; then
        break
    fi
done
"$RO" -t "$T" key space >/dev/null 2>&1 || true # in case the C library is waiting
grep -E "^Vanilla Conquer|^bench|^stall: game|Fatal" "$OUT" || { echo "no bench result; see $OUT" >&2; exit 1; }
