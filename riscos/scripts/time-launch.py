#!/usr/bin/env python3
"""Time from launching !VanillaTD to the game's screen mode and to the first
non-black movie frame. Run after the machine has booted to the desktop.
Usage: time-launch.py <hostcmd port> <vnc port> [app path]"""
import os, subprocess, sys, time
sys.path.insert(0, "/Applications/RPCEmu.app/Contents/Resources/tools/mcp")
os.environ["RPCEMU_VNC_PORT"] = sys.argv[2]
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

app = sys.argv[3] if len(sys.argv) > 3 else "HostFS:$.vc.!VanillaTD"
work = os.path.join(os.environ.get("VC_WORK") or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "vcport-work"), "hostfs")
pre = os.environ.get("VC_PRE", "")  # extra *commands before launching, ';'-separated
open(os.path.join(work, "runtd,feb"), "w").write("".join(c + "\n" for c in pre.split(";") if c) + "Run %s\n" % app)
# Make sure the desktop (not a leftover game screen) is showing first.
while (grab()["w"], grab()["h"]) == (640, 400):
    time.sleep(0.5)
subprocess.run(["/Applications/RPCEmu.app/Contents/MacOS/rpcemu-run", "--tcp", "127.0.0.1:" + sys.argv[1], "--",
                "Filer_Run HostFS:$.vc.runtd"], capture_output=True)
t0 = time.time(); mode = None
while time.time() - t0 < 120:
    g = grab()
    if mode is None and (g["w"], g["h"]) == (640, 400):
        mode = time.time() - t0
        print("game screen mode after %.1fs" % mode, flush=True)
    if mode is not None:
        fb = g["fb"]
        lit = sum(1 for i in range(0, len(fb), 4 * 97) if fb[i] + fb[i + 1] + fb[i + 2] > 60)
        if lit > 20:
            print("first movie frame after %.1fs" % (time.time() - t0))
            break
    time.sleep(0.1)
