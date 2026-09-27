#!/usr/bin/env python3
"""Heuristic backtrace of a running RISC OS program under RPCEmu Extended.

Pauses the machine, samples until the CPU is in user code (mode 0 = USR26,
0x10 = USR32), then scans the user stack for words that look like return
addresses (they point just past a BL into the program's code), and resolves
them with addr2line from the cross-build container.

Usage: guest-stack.py <debug socket> <elf> [--samples N] [--depth WORDS]
"""
import json
import os
import socket
import subprocess
import sys
import time

sock_path, elf = sys.argv[1], sys.argv[2]
samples = int(sys.argv[sys.argv.index("--samples") + 1]) if "--samples" in sys.argv else 1
depth = int(sys.argv[sys.argv.index("--depth") + 1]) if "--depth" in sys.argv else 1024
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def cmd(c):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    s.sendall((c + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf)


def text_range():
    out = subprocess.run(
        [ROOT + "/riscos/scripts/sdk.sh", "arm-unknown-riscos-readelf", "-S", os.path.relpath(elf, ROOT)],
        capture_output=True, text=True, cwd=ROOT).stdout
    for line in out.splitlines():
        if " .text " in line:
            f = line.split("]")[1].split()
            addr, size = int(f[2], 16), int(f[4], 16)
            return addr, addr + size
    raise SystemExit("no .text")


lo, hi = text_range()
all_frames = []
for _ in range(samples):
    for _ in range(200):
        cmd("pause")
        r = cmd("regs")
        pc = int(r["pc"], 16) & 0x03FFFFFC
        if r["mode"] in (0, 0x10) and lo <= pc < hi:
            break
        cmd("resume")
        time.sleep(0.01)
    regs = [int(x, 16) for x in r["regs"]]
    sp = regs[13]
    frames = [pc, regs[14] & 0x03FFFFFC]
    data = b""
    for off in range(0, depth * 4, 4096):
        m = cmd("mem %x %d" % (sp + off, min(4096, depth * 4 - off)))
        if not m.get("ok"):
            break
        data += bytes.fromhex(m["data"])
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - 3, 4)]
    cands = sorted({w & 0x03FFFFFC for w in words if lo <= (w & 0x03FFFFFC) < hi})
    # Keep only addresses preceded by a BL/BLX-style instruction.
    for w in words:
        a = w & 0x03FFFFFC
        if lo <= a < hi:
            m = cmd("mem %x 4" % (a - 4))
            if m.get("ok"):
                ins = int.from_bytes(bytes.fromhex(m["data"]), "little")
                if (ins & 0x0F000000) == 0x0B000000 or (ins & 0x0FFFFFF0) == 0x012FFF30 or ins == 0xE1A0F00E - 0:
                    frames.append(a)
    cmd("resume")
    all_frames.append(frames)

addrs = sorted({a for f in all_frames for a in f})
out = subprocess.run([ROOT + "/riscos/scripts/sdk.sh", "arm-unknown-riscos-addr2line", "-f", "-C", "-e",
                      os.path.relpath(elf, ROOT)] + ["%x" % a for a in addrs],
                     capture_output=True, text=True, cwd=ROOT).stdout.splitlines()
names = {a: (out[2 * i], out[2 * i + 1].replace("/work/", "")) for i, a in enumerate(addrs)}
for n, f in enumerate(all_frames):
    print("sample %d:" % n)
    seen = []
    for a in f:
        if a in seen:
            continue
        seen.append(a)
        fn, loc = names[a]
        print("  %08x %s  %s" % (a, fn[:70], loc))
