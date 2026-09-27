#!/usr/bin/env bash
# Time TD start-up on a test machine: launch -> title screen -> main menu.
# Usage: time-menu.sh <371|371-1mb|530> <vnc port>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
P=${RPCEMU_MCP_PYTHON:-$HOME/.rpcemu-mcp/bin/python}
"$ROOT/riscos/scripts/run-td.sh" "$1" >/dev/null
"$P" - "$2" <<'PY'
import os, sys, time
sys.path.insert(0, "/Applications/RPCEmu.app/Contents/Resources/tools/mcp")
os.environ["RPCEMU_VNC_PORT"] = sys.argv[1]
import rpcemu_mcp as r
def grab():
    got = {}
    orig = r._encode_png
    r._encode_png = lambda w, h, fb: got.update(w=w, h=h, fb=bytes(fb)) or b""
    try:
        r._vnc_screenshot_png()
    finally:
        r._encode_png = orig
    return got
def px(g, x, y):
    i = (y * g["w"] + x) * 4
    return g["fb"][i:i + 3].hex()
t0 = time.time(); title = None
while time.time() - t0 < 240:
    g = grab()
    if g["w"] == 640 and g["h"] == 400:
        if title is None and px(g, 100, 100) == "d47810":
            title = time.time() - t0
            print("title screen at %.1fs" % title, flush=True)
        # (320,148) is orange in the title art and grey on the "Intro & Sneak Peek" button.
        if title is not None and px(g, 320, 148) == "383838":
            menu = time.time() - t0
            print("main menu at %.1fs (%.1fs after title)" % (menu, menu - title))
            break
    time.sleep(0.1)
PY
