#!/usr/bin/env python3
"""Poor man's sampling profiler for a program running under RPCEmu Extended.

Repeatedly pauses the guest, records the user-mode PC and a heuristic call
stack (return addresses found on the stack), resumes, and at the end prints
the functions most often seen, both as the innermost frame ("self") and
anywhere on the stack ("total").

Usage: profile.py <debug sock> <elf> <seconds> [--interval s] [--exclude substr,...]
                  [--require substr] [--top N] [--stack bytes] [--fast] [--dump file]
Samples whose stack contains an --exclude substring (e.g. VQA_Play) are dropped;
with --require, only samples whose stack contains it (e.g. LogicClass::AI) are
kept. The stack is a heuristic scan, so --require can let in a few strays.
"""
import collections, json, os, socket, subprocess, sys, time

sock_path, elf, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
interval = float(sys.argv[sys.argv.index("--interval") + 1]) if "--interval" in sys.argv else 0.05
exclude = sys.argv[sys.argv.index("--exclude") + 1].split(",") if "--exclude" in sys.argv else []
require = sys.argv[sys.argv.index("--require") + 1] if "--require" in sys.argv else None
top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 15
depth = int(sys.argv[sys.argv.index("--stack") + 1]) if "--stack" in sys.argv else 1024  # bytes of stack to scan
fast = "--fast" in sys.argv  # just the PC and return address: several times the samples
dump = sys.argv[sys.argv.index("--dump") + 1] if "--dump" in sys.argv else None  # append each sample's frames here
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def cmd(c):
    s = socket.socket(socket.AF_UNIX); s.connect(sock_path); s.sendall((c + "\n").encode()); b = b""
    while not b.endswith(b"\n"):
        x = s.recv(65536)
        if not x: break
        b += x
    s.close()
    return json.loads(b)

sdk = [ROOT + "/riscos/scripts/sdk.sh"]
rel = os.path.relpath(elf, ROOT)
out = subprocess.run(sdk + ["arm-unknown-riscos-readelf", "-S", rel], capture_output=True, text=True, cwd=ROOT).stdout
lo = hi = 0
for line in out.splitlines():
    if " .text " in line:
        f = line.split("]")[1].split(); lo = int(f[2], 16); hi = lo + int(f[4], 16)

# Symbol table for fast address -> function lookup.
syms = []
for line in subprocess.run(sdk + ["bash", "-c", "arm-unknown-riscos-nm -C -n " + rel], capture_output=True, text=True, cwd=ROOT).stdout.splitlines():
    p = line.split(" ", 2)
    if len(p) == 3 and len(p[1]) == 1 and p[1] in "tTwW" and p[0]:
        syms.append((int(p[0], 16), p[2]))
import bisect
addrs = [a for a, _ in syms]
def name(a):
    i = bisect.bisect_right(addrs, a) - 1
    return syms[i][1] if i >= 0 else "?"

samples = []
end = time.time() + seconds
while time.time() < end:
  try:
    cmd("pause")
    r = cmd("regs")
    regs = [int(x, 16) for x in r["regs"]]
    pc = int(r["pc"], 16) & 0x03FFFFFC
    if r["mode"] in (0, 0x10):
        sp = regs[13]; frames = [pc, regs[14] & 0x03FFFFFC]
    else:
        b = cmd("bankregs")
        usr = [x for x in b["banks"] if x["mode"] == "USR"][0]
        sp = int(usr["r13"], 16); frames = [int(usr["r14"], 16) & 0x03FFFFFC]
        frames.insert(0, -1)  # in the OS (SWI/IRQ)
    m = cmd("mem %x %d" % (sp, depth)) if not fast else {}
    if m.get("ok"):
        d = bytes.fromhex(m["data"])
        for i in range(0, len(d) - 3, 4):
            w = int.from_bytes(d[i:i + 4], "little") & 0x03FFFFFC
            if lo <= w < hi:
                frames.append(w)
    cmd("resume")
    names = ["<os>" if a == -1 else name(a) for a in frames if a == -1 or lo <= a < hi]
    if not any(e in n for n in names for e in exclude) and (require is None or any(require in n for n in names)):
        samples.append(names)
    time.sleep(interval)
  except (OSError, ValueError, KeyError):
    time.sleep(0.5)  # the emulator is resetting or busy: skip this sample

if dump:
    with open(dump, "a") as f:
        for s in samples:
            f.write("\t".join(s) + "\n")
selfc = collections.Counter(s[0] for s in samples if s)
total = collections.Counter()
for s in samples:
    for n in set(s):
        total[n] += 1
print("%d samples" % len(samples))
print("-- innermost --")
for n, c in selfc.most_common(top):
    print("%5.1f%%  %s" % (100.0 * c / len(samples), n[:100]))
print("-- callers of the top innermost (the next frame) --")
for n, c in selfc.most_common(10):
    callers = collections.Counter(s[1] for s in samples if len(s) > 1 and s[0] == n)
    print("%s: %s" % (n[:60], ", ".join("%s %d" % (k[:50], v) for k, v in callers.most_common(4))))
print("-- anywhere on stack --")
for n, c in total.most_common(40):
    print("%5.1f%%  %s" % (100.0 * c / len(samples), n[:100]))
