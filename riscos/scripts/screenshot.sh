#!/usr/bin/env bash
# Save a PNG of an emulated machine's screen via its VNC server.
# Usage: screenshot.sh <vnc port> <out.png>
# Reuses the VNC client in RPCEmu Extended's MCP server.
set -euo pipefail
MCP_DIR=${RPCEMU_MCP_DIR:-/Applications/RPCEmu.app/Contents/Resources/tools/mcp}
PY=${RPCEMU_MCP_PYTHON:-$HOME/.rpcemu-mcp/bin/python}
RPCEMU_VNC_PORT=$1 "$PY" -c "
import sys; sys.path.insert(0, '$MCP_DIR')
import rpcemu_mcp
open(sys.argv[1], 'wb').write(rpcemu_mcp._vnc_screenshot_png())
" "$2"
