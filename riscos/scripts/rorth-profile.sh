#!/usr/bin/env bash
# Profile the games on a Rorth target with the in-game sampler (VC_PROFILE,
# common/riscos_profile.cpp), one scenario after another, under one lease.
#
#   rorth-profile.sh <target> <scenario>...      e.g. rorth-profile.sh riscpc td-battle td-g1-scroll
#   rorth-profile.sh <target> list               the scenarios
#
# For each scenario it sends nothing but variables: the profiling build is sent
# once per run, as ProfImage next to the installed game's !RunImage (which is
# left alone), with ProfRun, an Obey file like !Run that runs ProfImage with its
# output in ProfOut and ProfErr. Afterwards it fetches the samples and the log,
# removes everything it put there, and turns the samples into profiles with
# vcprof.py. Results go in $OUT_DIR/<target>-<scenario>.{bin,log,txt,md}.
#
# The lease: taken (queueing) from $RORTH/host, where every ro-run here runs,
# and released at the end. Variables set during it are put back by Rorth too.
#
# Environment:
#   RORTH       the Rorth checkout (~/code/RORTH)
#   OUT_DIR     where results go (../vcport-work/profile/results)
#   TD_IMAGE, RA_IMAGE   the profiling builds (build/riscos/vanillatd,ff8, build/riscos-ra/vanillara,ff8)
#   TD_APP, RA_APP       the installed games on the target (defaults per target below)
#   LEASE_WAIT  seconds to wait for the lease (7200)
#   NO_LEASE=1  the caller holds the lease already (from $RORTH/host)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
RORTH=${RORTH:-$HOME/code/RORTH}
OUT_DIR=${OUT_DIR:-$WORK/profile/results}
T=${1:?target}
shift
TD_IMAGE=${TD_IMAGE:-$ROOT/build/riscos/vanillatd,ff8}
RA_IMAGE=${RA_IMAGE:-$ROOT/build/riscos-ra/vanillara,ff8}
TD_ELF=${TD_IMAGE%,ff8}
RA_ELF=${RA_IMAGE%,ff8}

case "$T" in
riscpc)
    TD_APP=${TD_APP:-'IDEFS::HardDisc4b.$.VanillaConquer.!VanillaTD'}
    RA_APP=${RA_APP:-'IDEFS::HardDisc4b.$.VanillaConquer.!VanillaRA'}
    ;;
emu371 | emu530 | emu439)
    # Made by vcport-work/profile/setup-emu-apps.sh in the machine's HostFS Work directory.
    TD_APP=${TD_APP:-'HostFS::HostFS.$.Work.vcprof.!VanillaTD'}
    RA_APP=${RA_APP:-'HostFS::HostFS.$.Work.vcprof.!VanillaRA'}
    ;;
*)
    TD_APP=${TD_APP:?set TD_APP for target $T}
    RA_APP=${RA_APP:?set RA_APP for target $T}
    ;;
esac

. "$ROOT/riscos/scripts/profile-scenarios.sh"
if [ "${1:-}" = list ] || [ $# -eq 0 ]; then
    echo $ALL | tr ' ' '\n'
    exit 0
fi
for s in "$@"; do scenario "$s" || { echo "unknown scenario $s" >&2; exit 2; }; done

mkdir -p "$OUT_DIR"
cd "$RORTH/host" # the lease belongs to the directory ro-run runs from
ro() { PYTHONPATH="$RORTH/host" python3 -m ro_run -t "$T" "$@"; }

if [ "${NO_LEASE:-0}" != 1 ]; then
    echo "taking the $T lease (queueing up to ${LEASE_WAIT:-7200} s)..."
    ro lease take --queue --timeout "${LEASE_WAIT:-7200}" --idle 900 --max 7200 --label "profile (vc-profile)" >/dev/null
fi
PUT_TD=0 PUT_RA=0
cleanup()
{
    set +e
    if [ "$PUT_TD" = 1 ]; then ro cmd "Run $TD_APP.ProfOff" >/dev/null 2>&1; for f in ProfImage ProfRun ProfOff ProfData ProfErr ProfOut INI.SCG01EA; do ro rm "$TD_APP.$f" >/dev/null 2>&1; done; fi
    if [ "$PUT_RA" = 1 ]; then ro cmd "Run $RA_APP.ProfOff" >/dev/null 2>&1; for f in ProfImage ProfRun ProfOff ProfData ProfErr ProfOut INI.SCG01EA; do ro rm "$RA_APP.$f" >/dev/null 2>&1; done; fi
    for v in VC_BENCH VC_SEED VC_PARTIALPRESENT VC_NOSOUND VC_BENCHSCROLL VC_MOVIETEST VC_MOVIETEST_FROM VC_MOVIETEST_COUNT VC_PROFILE VC_PROFILE_SECS VC_FPSLOG; do
        ro cmd "Unset $v" >/dev/null 2>&1
    done
    [ "${NO_LEASE:-0}" != 1 ] && ro lease release >/dev/null 2>&1
}
trap cleanup EXIT

# The riscpc's screensaver (and a "not correctly shut down" box) starve the desktop: nudge the mouse.
ro mouse 400 300 >/dev/null 2>&1 || true # (and below: a failing ro-run mustn't end the run early)
ro mouse 404 302 >/dev/null 2>&1 || true
ro screenshot --small -o "$OUT_DIR/$T-before.png" >/dev/null 2>&1 || true
echo "screenshot of the desktop before: $OUT_DIR/$T-before.png (check for an error box if a run hangs)"

prepare_app()
{ # game: send the profiling build and ProfRun once
    local game=$1 app image var slot
    if [ "$game" = td ]; then app=$TD_APP image=$TD_IMAGE var=VanillaTD slot=4000K; else app=$RA_APP image=$RA_IMAGE var=VanillaRA slot=6000K; fi
    ro stat "$app.!RunImage" >/dev/null || { echo "no game at $app on $T (set TD_APP or RA_APP)" >&2; exit 1; }
    local obey="$OUT_DIR/ProfRun-$game,feb"
    cat >"$obey" <<EOF
| Profiling run (riscos/scripts/rorth-profile.sh): like !Run, with ProfImage and ProfErr.
Set $var\$Dir <Obey\$Dir>
RMEnsure SharedUnixLibrary 1.16 IfThere System:Modules.SharedULib Then RMLoad System:Modules.SharedULib
RMEnsure SharedUnixLibrary 1.16 RMLoad <$var\$Dir>.Modules.SharedULib
RMEnsure DigitalRenderer 0.56 IfThere System:Modules.DRenderer Then RMLoad System:Modules.DRenderer
RMEnsure DigitalRenderer 0.56 RMLoad <$var\$Dir>.Modules.DRenderer
Set VC_PROFILE <$var\$Dir>.ProfData
WimpSlot -min $slot -max $slot
Run <$var\$Dir>.ProfImage %*0 >/<$var\$Dir>/ProfOut 2>/<$var\$Dir>/ProfErr
EOF
    echo "sending $(basename "$image") to $app.ProfImage..."
    ro put "$image" "$app.ProfImage" --type ff8 >/dev/null
    ro put "$obey" "$app.ProfRun" --type feb >/dev/null
    # profoff removes the profiler's interrupt handler if a run dies without doing it.
    ro put "${PROFOFF:-$ROOT/build/riscos-tests/profoff,ff8}" "$app.ProfOff" --type ff8 >/dev/null
}

for s in "$@"; do
    scenario "$s"
    if [ "$GAME" = td ]; then
        APP=$TD_APP ELF=$TD_ELF BATTLE_INI=$WORK/bench/SCG01EA-battle.INI
        [ "$PUT_TD" = 1 ] || { prepare_app td; PUT_TD=1; }
    else
        APP=$RA_APP ELF=$RA_ELF BATTLE_INI=$WORK/bench/RA-SCG01EA-battle.INI
        [ "$PUT_RA" = 1 ] || { prepare_app ra; PUT_RA=1; }
    fi
    base="$OUT_DIR/$T-$s"
    echo "== $s on $T ($VARS)"
    if [ "$BATTLE" = 1 ]; then
        ro put "$BATTLE_INI" "$APP.INI.SCG01EA" --type fff >/dev/null
    else
        ro rm "$APP.INI.SCG01EA" >/dev/null 2>&1 || true
    fi
    ro rm "$APP.ProfData" >/dev/null 2>&1 || true
    ro rm "$APP.ProfErr" >/dev/null 2>&1 || true
    for v in $VARS; do ro cmd "Set ${v%%=*} ${v#*=}" >/dev/null; done
    started=$(date +%s)
    # A full-screen game keeps Rorth's helper busy, so launch may report an error once it has started.
    ro launch "Run $APP.ProfRun ${MISSION:+-AUTOSTART=$MISSION}" >/dev/null 2>&1 || true
    # The game holds ProfErr open until it exits; ProfData appears when it has written it.
    result=timeout
    while [ $(($(date +%s) - started)) -lt "$WAIT" ]; do
        sleep 10
        if ro get "$APP.ProfErr" "$base.log" >/dev/null 2>&1; then
            if grep -q "^profile: .* written" "$base.log"; then result=ok; break; fi
            if grep -qE "Fatal|Abort|crash|SIGSEGV|exception" "$base.log"; then result=crashed; break; fi
            if grep -qE "^profile: (VC_PROFILE needs|can't|no dynamic|the handler)" "$base.log"; then result=no-profile; break; fi
        fi
    done
    sleep 3
    ro get "$APP.ProfErr" "$base.log" >/dev/null 2>&1 || true
    ro key space >/dev/null 2>&1 || true # in case the C library is waiting
    sleep 2
    ro cmd "Run $APP.ProfOff" 2>&1 | grep -v "^profoff: 0 " | sed 's/^/   /' || true
    for v in $VARS; do ro cmd "Unset ${v%%=*}" >/dev/null 2>&1 || true; done
    echo "   $result after $(($(date +%s) - started)) s"
    grep -E "^bench:|^bench ms|^movie |^profile:" "$base.log" | sed 's/^/   /' || true
    if [ "$result" = ok ] && ro get "$APP.ProfData" "$base.bin" >/dev/null 2>&1; then
        elfkeep="$OUT_DIR/elf/$(md5 -q "$ELF" 2>/dev/null || md5sum "$ELF" | cut -d' ' -f1).elf"
        mkdir -p "$OUT_DIR/elf"; [ -f "$elfkeep" ] || cp "$ELF" "$elfkeep"
        echo "$elfkeep" >"$base.elfpath" # the ELF these samples belong to, for vcprof.py later
        python3 "$ROOT/riscos/scripts/vcprof.py" "$base.bin" "$ELF" --phases --callers memcpy,memset,__divsi3,__udivsi3,__modsi3,__umodsi3,LCW_Uncompress,Phase_Now_Us >"$base.txt" || true
        python3 "$ROOT/riscos/scripts/vcprof.py" "$base.bin" "$ELF" --phases --markdown --callers memcpy,memset,__divsi3,__udivsi3,__modsi3,__umodsi3,LCW_Uncompress >"$base.md" || true
        head -40 "$base.txt" | sed 's/^/   /'
    fi
    ro rm "$APP.INI.SCG01EA" >/dev/null 2>&1 || true
    ro rm "$APP.ProfData" >/dev/null 2>&1 || true
done
