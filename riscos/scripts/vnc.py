#!/usr/bin/env python3
"""Drive an RPCEmu machine's screen, mouse and keyboard over VNC.

Reuses the VNC client in RPCEmu Extended's MCP server. Run it with that
server's Python (see screenshot.sh).

Usage: vnc.py <port> <command> [args] [<command> [args]] ...
  shot <out.png>                 save a screenshot
  move <x> <y>                   move the pointer
  click <x> <y> [select|menu|adjust]
  drag <x1> <y1> <x2> <y2>       drag with select (box-select units)
  key <name|0xsym> ...           press keys: esc, ret, space, tab, f1..f12, up/down/left/right, a-z
  slowkey <hold s> <name|0xsym> ...  like key, but holds each key down for <hold> seconds
  wait <seconds>
  save <x> <y> <w> <h> <out.raw>  save a screen region (raw RGBX) as a reference
  until <x> <y> <w> <h> <ref.raw> <timeout>  wait until the region matches the reference
  changes <seconds>              count how many times the screen changes over a period
Commands can be chained, e.g. vnc.py 5920 click 300 200 wait 1 shot /tmp/a.png
"""
import os
import sys
import time

sys.path.insert(0, os.environ.get("RPCEMU_MCP_DIR", "/Applications/RPCEmu.app/Contents/Resources/tools/mcp"))
os.environ["RPCEMU_VNC_PORT"] = sys.argv[1]
import rpcemu_mcp as r  # noqa: E402

def grab():
    """Return (w, h, fb) with fb as RGBX bytes, via the MCP server's VNC client."""
    got = {}
    orig = r._encode_png
    r._encode_png = lambda w, h, fb: got.update(w=w, h=h, fb=bytes(fb)) or b""
    try:
        r._vnc_screenshot_png()
    finally:
        r._encode_png = orig
    return got["w"], got["h"], got["fb"]


def region(x, y, w, h):
    sw, sh, fb = grab()
    if x + w > sw or y + h > sh:
        return None
    return b"".join(fb[((y + row) * sw + x) * 4:((y + row) * sw + x + w) * 4] for row in range(h))


KEYS = {"esc": 0xFF1B, "ret": 0xFF0D, "space": 0x20, "tab": 0xFF09, "bs": 0xFF08,
        "left": 0xFF51, "up": 0xFF52, "right": 0xFF53, "down": 0xFF54, "home": 0xFF50,
        "ctrl": 0xFFE3, "shift": 0xFFE1, "alt": 0xFFE9}
KEYS.update({"f%d" % i: 0xFFBD + i for i in range(1, 13)})

args = sys.argv[2:]
while args:
    c = args.pop(0)
    if c == "shot":
        open(args.pop(0), "wb").write(r._vnc_screenshot_png())
    elif c == "move":
        r._vnc_move(int(args.pop(0)), int(args.pop(0)))
    elif c == "click":
        x, y = int(args.pop(0)), int(args.pop(0))
        button = args.pop(0) if args and args[0] in ("select", "menu", "adjust") else "select"
        r._vnc_click(x, y, button)
    elif c == "drag":
        r._vnc_drag(*(int(args.pop(0)) for _ in range(4)))
    elif c == "key":
        syms = []
        while args and args[0] not in ("shot", "move", "click", "drag", "key", "slowkey", "wait", "save", "until", "changes"):
            k = args.pop(0)
            syms.append(int(k, 16) if k.startswith("0x") else KEYS.get(k, ord(k[0])))
        r._vnc_send_keys(syms)
    elif c == "slowkey":
        hold = float(args.pop(0))
        sock, _, _ = r._vnc_connect()
        try:
            while args and args[0] not in ("shot", "move", "click", "drag", "key", "slowkey", "wait", "save", "until", "changes"):
                k = args.pop(0)
                sym = int(k, 16) if k.startswith("0x") else KEYS.get(k, ord(k[0]))
                sock.sendall(r.struct.pack(">BBHI", 4, 1, 0, sym))
                time.sleep(hold)
                sock.sendall(r.struct.pack(">BBHI", 4, 0, 0, sym))
                time.sleep(hold)
        finally:
            sock.close()
    elif c == "wait":
        time.sleep(float(args.pop(0)))
    elif c == "save":
        x, y, w, h = (int(args.pop(0)) for _ in range(4))
        open(args.pop(0), "wb").write(region(x, y, w, h))
    elif c == "until":
        x, y, w, h = (int(args.pop(0)) for _ in range(4))
        ref = open(args.pop(0), "rb").read()
        deadline = time.time() + float(args.pop(0))
        start = time.time()
        while region(x, y, w, h) != ref:
            if time.time() > deadline:
                raise SystemExit("timeout waiting for screen region")
            time.sleep(1)
        print("matched after %.0fs" % (time.time() - start))
    elif c == "changes":
        period = float(args.pop(0))
        end = time.time() + period
        last, n, grabs = None, 0, 0
        while time.time() < end:
            fb = grab()[2]
            grabs += 1
            if last is not None and fb != last:
                n += 1
            last = fb
        print("%d changes in %d grabs over %.0fs" % (n, grabs, period))
    else:
        raise SystemExit("unknown command " + c)
