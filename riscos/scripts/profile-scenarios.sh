# Profiling scenarios, shared by rorth-profile.sh (real machines, through Rorth) and
# emu-profile.sh (an RPCEmu machine on this computer). Sourced, not run.
# Scenarios: game, -AUTOSTART mission (which also skips the wait for a click; the movie
# tests play their movies first and quit), whether the benchmark battle replaces
# mission 1, the variables, and how long to wait for it at most (s).
scenario()
{
    BATTLE=0 VARS="" WAIT=900
    case "$1" in
    td-battle) GAME=td MISSION=G1 BATTLE=1 VARS="VC_BENCH=1200 VC_SEED=1" ;;
    td-battle-hash) GAME=td MISSION=G1 BATTLE=1 VARS="VC_BENCH=1200 VC_SEED=1 VC_FRAMEHASH=1 VC_NOPROFILE=1" ;;
    td-battle-pp) GAME=td MISSION=G1 BATTLE=1 VARS="VC_BENCH=1200 VC_SEED=1 VC_PARTIALPRESENT=1" ;;
    td-battle-nosound) GAME=td MISSION=G1 BATTLE=1 VARS="VC_BENCH=1200 VC_SEED=1 VC_NOSOUND=1" ;;
    td-g1-scroll) GAME=td MISSION=G1 VARS="VC_BENCH=1500 VC_SEED=1 VC_BENCHSCROLL=128" ;;
    td-g1-scroll-pp) GAME=td MISSION=G1 VARS="VC_BENCH=1500 VC_SEED=1 VC_BENCHSCROLL=128 VC_PARTIALPRESENT=1" ;;
    td-g1-pp) GAME=td MISSION=G1 VARS="VC_BENCH=1500 VC_SEED=1 VC_PARTIALPRESENT=1" ;;
    td-n1-scroll) GAME=td MISSION=N1 VARS="VC_BENCH=1500 VC_SEED=1 VC_BENCHSCROLL=128" ;;
    td-movie) GAME=td MISSION=G1 VARS="VC_MOVIETEST=40 VC_MOVIETEST_FROM=GDI1 VC_MOVIETEST_COUNT=1" ;;
    td-movie-nosound) GAME=td MISSION=G1 VARS="VC_MOVIETEST=40 VC_MOVIETEST_FROM=GDI1 VC_MOVIETEST_COUNT=1 VC_NOSOUND=1" ;;
    td-startup) GAME=td MISSION=G1 VARS="VC_BENCH=30 VC_SEED=1" ;;
    ra-battle) GAME=ra MISSION=A1 BATTLE=1 VARS="VC_BENCH=600 VC_SEED=1" ;;
    ra-battle-hash) GAME=ra MISSION=A1 BATTLE=1 VARS="VC_BENCH=600 VC_SEED=1 VC_FRAMEHASH=1 VC_NOPROFILE=1" ;;
    ra-battle-pp) GAME=ra MISSION=A1 BATTLE=1 VARS="VC_BENCH=600 VC_SEED=1 VC_PARTIALPRESENT=1" ;;
    ra-battle-nosound) GAME=ra MISSION=A1 BATTLE=1 VARS="VC_BENCH=600 VC_SEED=1 VC_NOSOUND=1" ;;
    ra-a1-scroll) GAME=ra MISSION=A1 VARS="VC_BENCH=2000 VC_SEED=1 VC_BENCHSCROLL=128" ;;
    ra-a1-scroll-pp) GAME=ra MISSION=A1 VARS="VC_BENCH=2000 VC_SEED=1 VC_BENCHSCROLL=128 VC_PARTIALPRESENT=1" ;;
    ra-a1-pp) GAME=ra MISSION=A1 VARS="VC_BENCH=2000 VC_SEED=1 VC_PARTIALPRESENT=1" ;;
    ra-a2-scroll) GAME=ra MISSION=A2 VARS="VC_BENCH=2000 VC_SEED=1 VC_BENCHSCROLL=128" ;;
    ra-a2-scroll-pp) GAME=ra MISSION=A2 VARS="VC_BENCH=2000 VC_SEED=1 VC_BENCHSCROLL=128 VC_PARTIALPRESENT=1" ;;
    ra-movie) GAME=ra MISSION=A1 VARS="VC_MOVIETEST=40 VC_MOVIETEST_FROM=ENGLISH VC_MOVIETEST_COUNT=1" ;;
    ra-movie-nosound) GAME=ra MISSION=A1 VARS="VC_MOVIETEST=40 VC_MOVIETEST_FROM=ENGLISH VC_MOVIETEST_COUNT=1 VC_NOSOUND=1" ;;
    ra-startup) GAME=ra MISSION=A1 VARS="VC_BENCH=30 VC_SEED=1" ;;
    *) return 1 ;;
    esac
}
ALL="td-battle td-battle-hash td-battle-pp td-battle-nosound td-g1-scroll td-g1-scroll-pp td-g1-pp td-n1-scroll td-movie td-movie-nosound td-startup
ra-battle ra-battle-hash ra-battle-pp ra-battle-nosound ra-a1-scroll ra-a1-scroll-pp ra-a1-pp ra-a2-scroll ra-a2-scroll-pp ra-movie ra-movie-nosound ra-startup"
