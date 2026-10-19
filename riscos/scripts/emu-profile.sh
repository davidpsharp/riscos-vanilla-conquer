#!/usr/bin/env bash
# Profile the games on one of this project's RPCEmu machines ("VC SA RO371" by
# default), with the in-game sampler (VC_PROFILE) and, alongside it, profile.py's
# debugger sampling of the whole machine. Emulator figures: RPCEmu is much faster
# than a real Risc PC and doesn't model its memory, so use them for generic hotspots
# only (see rorth-profile.sh for the real machines).
#
#   emu-profile.sh <scenario>...        scenarios as in profile-scenarios.sh
#
# The games run from HostFS:$.vc.vcprof (../vcport-work/hostfs/vcprof, made by
# ../vcport-work/profile/setup-emu-apps.sh), so the shared deploy is left alone. Each
# run is an Obey file there that sets the scenario's variables, runs the game and
# unsets them again. Results: $OUT_DIR/emu371-<scenario>.{bin,log,txt,md,dbg.txt}.
#   MACHINE ("VC SA RO371"), CMDPORT (5921), OUT_DIR, TD_IMAGE, RA_IMAGE as in rorth-profile.sh
#   NO_DBG=1 skips the debugger sampling.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}
OUT_DIR=${OUT_DIR:-$WORK/profile/results}
MACHINE=${MACHINE:-VC SA RO371}
CMDPORT=${CMDPORT:-5921}
TAG=${TAG:-emu371}
TD_IMAGE=${TD_IMAGE:-$ROOT/build/riscos/vanillatd,ff8}
RA_IMAGE=${RA_IMAGE:-$ROOT/build/riscos-ra/vanillara,ff8}
DATADIR=${RPCEMU_DATADIR:-$HOME/rpcemu/rpcemu-extended}
SOCK="$DATADIR/machines/$MACHINE/rpcemu-debug.sock"
RUN=/Applications/RPCEmu.app/Contents/MacOS/rpcemu-run
. "$ROOT/riscos/scripts/profile-scenarios.sh"
[ $# -gt 0 ] || { echo $ALL | tr ' ' '\n'; exit 0; }
mkdir -p "$OUT_DIR"
H="$WORK/hostfs/vcprof"
[ -d "$H/!VanillaTD" ] || "$WORK/profile/setup-emu-apps.sh" "$WORK/hostfs"

for s in "$@"; do
    scenario "$s" || { echo "unknown scenario $s" >&2; exit 2; }
    if [ "$GAME" = td ]; then
        app="$H/!VanillaTD" image=$TD_IMAGE var=VanillaTD slot=4000K battle=$WORK/bench/SCG01EA-battle.INI rapp='HostFS:$.vc.vcprof.!VanillaTD'
    else
        app="$H/!VanillaRA" image=$RA_IMAGE var=VanillaRA slot=6000K battle=$WORK/bench/RA-SCG01EA-battle.INI rapp='HostFS:$.vc.vcprof.!VanillaRA'
    fi
    base="$OUT_DIR/$TAG-$s"
    rm -f "$app/ProfImage,ff8" "$app"/ProfData* "$app"/ProfErr* "$app"/ProfOut* "$app/INI/SCG01EA,fff"
    cp "$image" "$app/ProfImage,ff8"
    cp "${PROFOFF:-$ROOT/build/riscos-tests/profoff,ff8}" "$app/ProfOff,ff8"
    [ "$BATTLE" = 1 ] && cp "$battle" "$app/INI/SCG01EA,fff"
    {
        echo "| Profiling run (riscos/scripts/emu-profile.sh $s)"
        echo "Set $var\$Dir $rapp"
        echo "RMEnsure SharedUnixLibrary 1.16 IfThere System:Modules.SharedULib Then RMLoad System:Modules.SharedULib"
        echo "RMEnsure SharedUnixLibrary 1.16 RMLoad <$var\$Dir>.Modules.SharedULib"
        echo "RMEnsure DigitalRenderer 0.56 IfThere System:Modules.DRenderer Then RMLoad System:Modules.DRenderer"
        echo "RMEnsure DigitalRenderer 0.56 RMLoad <$var\$Dir>.Modules.DRenderer"
        [ "${NO_PROFILE:-0}" = 1 ] || echo "Set VC_PROFILE <$var\$Dir>.ProfData"
        for v in $VARS; do echo "Set ${v%%=*} ${v#*=}"; done
        echo "WimpSlot -min $slot -max $slot"
        echo "Run <$var\$Dir>.ProfImage ${MISSION:+-AUTOSTART=$MISSION} >/<$var\$Dir>/ProfOut 2>/<$var\$Dir>/ProfErr"
        echo "Unset VC_PROFILE"
        for v in $VARS; do echo "Unset ${v%%=*}"; done
    } >"$app/ProfRun,feb"
    echo "== $s on $MACHINE ($VARS)"
    # Start from a freshly booted machine (a crashed run leaves it waiting for a key).
    if [ "${NO_RESET:-0}" != 1 ]; then
        /Applications/RPCEmu.app/Contents/MacOS/rpcemu-debug --socket "$SOCK" reset >/dev/null
        sleep 3
        for _ in $(seq 1 60); do
            timeout 3 "$RUN" --tcp 127.0.0.1:$CMDPORT -- 'Echo up' 2>/dev/null | grep -q '^up' && break
            sleep 2
        done
        sleep 6
    fi
    started=$(date +%s)
    "$RUN" --tcp 127.0.0.1:$CMDPORT -- "Filer_Run $rapp.ProfRun" >/dev/null
    dbgpid=
    if [ "${NO_DBG:-0}" != 1 ] && [ -S "$SOCK" ]; then
        sleep 2
        ELF=${image%,ff8}
        python3 "$ROOT/riscos/scripts/profile.py" "$SOCK" "$ELF" "${DBG_SECS:-60}" --interval 0.02 --top 40 >"$base.dbg.txt" 2>&1 &
        dbgpid=$!
    fi
    result=timeout
    while [ $(($(date +%s) - started)) -lt "$WAIT" ]; do
        sleep 5
        if ! ls "$app"/ProfErr* >/dev/null 2>&1 && [ $(($(date +%s) - started)) -gt 90 ]; then result=did-not-start; break; fi
        if cat "$app"/ProfErr* 2>/dev/null | grep -q "^profile: .* written"; then result=ok; break; fi
        if cat "$app"/ProfErr* 2>/dev/null | grep -qE "^bench objects at the end|^movietest:"; then [ "${NO_PROFILE:-0}" = 1 ] && { result=ok; break; }; fi
        if cat "$app"/ProfErr* 2>/dev/null | grep -qE "Fatal signal|Uncaught|Abort"; then result=crashed; sleep 5; break; fi
        if cat "$app"/ProfErr* 2>/dev/null | grep -qE "^profile: (VC_PROFILE needs|can't|no dynamic|the handler)"; then result=no-profile; break; fi
    done
    [ -n "$dbgpid" ] && wait "$dbgpid" 2>/dev/null || true
    sleep 2
    cat "$app"/ProfErr* >"$base.log" 2>/dev/null || true
    echo "   $result after $(($(date +%s) - started)) s"
    grep -E "^bench:|^bench ms|^movie |^profile:" "$base.log" | sed 's/^/   /' || true
    data=$(ls "$app"/ProfData* 2>/dev/null | head -1)
    if [ -n "$data" ]; then
        cp "$data" "$base.bin"
        ELF=${image%,ff8}
        elfkeep="$OUT_DIR/elf/$(md5 -q "$ELF" 2>/dev/null || md5sum "$ELF" | cut -d' ' -f1).elf"
        mkdir -p "$OUT_DIR/elf"; [ -f "$elfkeep" ] || cp "$ELF" "$elfkeep"
        echo "$elfkeep" >"$base.elfpath" # the ELF these samples belong to, for vcprof.py later
        python3 "$ROOT/riscos/scripts/vcprof.py" "$base.bin" "$ELF" --phases --callers memcpy,memset,__divsi3,__udivsi3,__modsi3,__umodsi3,LCW_Uncompress,Phase_Now_Us >"$base.txt" || true
        python3 "$ROOT/riscos/scripts/vcprof.py" "$base.bin" "$ELF" --phases --markdown --callers memcpy,memset,__divsi3,__udivsi3,__modsi3,__umodsi3,LCW_Uncompress >"$base.md" || true
        head -30 "$base.txt" | sed 's/^/   /'
    fi
    rm -f "$app/ProfImage,ff8" "$app"/ProfData* "$app/INI/SCG01EA,fff"
done
