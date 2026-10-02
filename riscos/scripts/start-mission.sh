#!/usr/bin/env bash
# Launch TD straight into a mission on a test machine, skipping movies with Esc,
# and wait until the in-game tab bar is showing.
# Usage: start-mission.sh <371|371-1mb|530> <vnc port> <G1|N1|...> [extra args]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
P=${RPCEMU_MCP_PYTHON:-$HOME/.rpcemu-mcp/bin/python}
M=$1 VNC=$2 MISSION=$3
shift 3
"$ROOT/riscos/scripts/run-td.sh" "$M" -AUTOSTART=$MISSION "$@" >/dev/null
sleep 8
for i in $(seq 1 12); do
    "$P" "$ROOT/riscos/scripts/vnc.py" "$VNC" slowkey 0.15 esc
    sleep 3
    if "$P" "$ROOT/riscos/scripts/vnc.py" "$VNC" until 30 0 120 12 "${VC_WORK:-$(cd "$ROOT/.." && pwd)/vcport-work}/ref-options-tab.raw" 2 >/dev/null 2>&1; then
        echo "in mission $MISSION"
        exit 0
    fi
done
echo "mission $MISSION did not start" >&2
exit 1
