#!/usr/bin/env python3
"""Capture what the game's SDL audio callback produces on a RISC OS guest.

Breaks on Mixer_Callback, runs it to its return, then reads the output buffer
it filled (16 bit stereo) and reports RMS/peak. Optionally appends the raw
audio to a file so it can be listened to.

Usage: capture-mixer.py <debug sock> <callback addr> <count> [out.raw]
"""
import json, math, socket, struct, sys, time

sock, addr, count = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3])
out = open(sys.argv[4], "ab") if len(sys.argv) > 4 else None

def cmd(c):
    s = socket.socket(socket.AF_UNIX); s.connect(sock); s.sendall((c + "\n").encode()); b = b""
    while not b.endswith(b"\n"):
        x = s.recv(65536)
        if not x: break
        b += x
    s.close()
    return json.loads(b)

def wait_pause(timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        if cmd("status")["paused"]:
            return True
        time.sleep(0.005)
    return False

for n in range(count):
    cmd("bp add %x once" % addr)
    if not wait_pause():
        print("callback not reached"); break
    r = [int(x, 16) for x in cmd("regs")["regs"]]
    stream, length, ret = r[1], r[2], r[14] & 0x03FFFFFC
    cmd("bp add %x once" % ret)
    cmd("resume")
    if not wait_pause():
        print("callback did not return"); break
    data = b""
    for off in range(0, length, 4096):
        m = cmd("mem %x %d" % (stream + off, min(4096, length - off)))
        data += bytes.fromhex(m["data"])
    cmd("resume")
    samples = struct.unpack("<%dh" % (len(data) // 2), data)
    rms = math.sqrt(sum(x * x for x in samples) / max(1, len(samples)))
    peak = max(abs(x) for x in samples) if samples else 0
    print("callback %d: %d bytes rms=%d peak=%d" % (n, length, rms, peak), flush=True)
    if out:
        out.write(data)
