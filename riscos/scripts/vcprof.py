#!/usr/bin/env python3
"""Turn a VC_PROFILE sample file (common/riscos_profile.cpp) into profiles.

Usage: vcprof.py <samples file> <elf> [options]
  --top N             rows per table (25)
  --thread T          only thread T (0 main, 1 mixer); default all
  --require S[,S..]   only samples whose stack has a function containing S
  --exclude S[,S..]   drop samples whose stack has a function containing S
  --from SECS / --to SECS   only samples in that time window (since the start)
  --callers F[,F..]   for each function whose name contains F: its callers and call sites
  --phases            split the time between the game's phases (see PHASES)
  --markdown          Markdown tables
  --drop-self F[,F..] leave out samples taken in these functions (RPCEmu over-samples
                      Phase_Now_Us, as it takes interrupts where the PSR changes)
  --from-frame N      (interrupt samples) only those once N game frames had begun (1: in the game)
  --until-frame N     (interrupt samples) only those before game frame N+1 (0: start-up)
  --timer             (interrupt samples) only those taken at the 100 Hz timer's interrupt
  --nm CMD            the nm to use (default: /opt/homebrew/bin/arm-none-eabi-nm, or the SDK's)

Interrupt samples (the current format) each weigh the same. Shares are of the time in
this program's own code; the header line says how many interrupts found the machine in
the OS (SWIs, including the program's own) instead. Self time is where the PC was; total time counts each
function once per sample if it's anywhere on the stack.

The stack comes from the APCS frame-pointer chain. A function with no frame of
its own (a leaf, like memcpy or __divsi3) is called from the function at its LR.
"""
import bisect, collections, os, struct, subprocess, sys

def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

path, elf = sys.argv[1], sys.argv[2]
top = int(arg("--top", 25))
thread = arg("--thread")
require = [x for x in (arg("--require") or "").split(",") if x]
exclude = [x for x in (arg("--exclude") or "").split(",") if x]
t_from = float(arg("--from", -1))
t_to = float(arg("--to", 1e12))
callers_of = [x for x in (arg("--callers") or "").split(",") if x]
md = "--markdown" in sys.argv
MASK = 0x03FFFFFC

nm = arg("--nm", "/opt/homebrew/bin/arm-none-eabi-nm")
syms = []
# UnixLib's assembler routines have local labels inside them (memcpy's ucp32lp, dcp4sh2lp...):
# count those as the routine itself.
ASM_ROUTINES = {"memcpy", "memmove", "bcopy", "memset", "bzero", "strcpy", "strlen", "memcmp", "strcmp", "memchr",
                "__divsi3", "__udivsi3", "__modsi3", "__umodsi3", "__aeabi_idiv", "__aeabi_uidiv"}
last_global = None
for line in subprocess.run([nm, "-C", "-n", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split(" ", 2)
    if len(p) == 3 and len(p[1]) == 1 and p[1] in "tTwW" and p[0]:
        name = p[2]
        if p[1] in "TW":
            last_global = name
        elif last_global in ASM_ROUTINES and "(" not in name and "::" not in name:
            name = last_global
        if syms and syms[-1][1] == name:
            continue
        syms.append((int(p[0], 16), name))
addrs = [a for a, _ in syms]
text_hi = syms[-1][0] + 0x10000 if syms else 0

def fname(a):
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0 or a >= text_hi:
        return None
    n = syms[i][1]
    # Shorter C++ names: drop the argument list.
    if "(" in n and not n.startswith("operator"):
        n = n[: n.index("(")]
    return n

def fstart(a):
    i = bisect.bisect_right(addrs, a) - 1
    return syms[i][0] if i >= 0 else 0

data = open(path, "rb").read()
magic, words, count, hz, depth, h5, h6, h7 = struct.unpack_from("<8I", data, 0)
assert magic in (0x31504356, 0x32504356), "not a VC_PROFILE file"
v2 = magic == 0x32504356  # sampled at interrupts: h5 in the OS, h6 in other programs, h7 dropped
lost, span_cs = (0, 0) if v2 else (h5, h6)
samples = []
for i in range(count):
    s = struct.unpack_from("<%dI" % words, data, 32 + i * words * 4)
    samples.append(s)

def stack_of(s):
    """Innermost first: [(function, call site address or None)]."""
    pc, lr, n = s[3] & MASK, s[4] & MASK, s[7]
    frames = [(s[8 + k * 2] & MASK, s[9 + k * 2] & MASK) for k in range(n)]
    owners = [fname(max(0, p - 12)) for p, _ in frames]
    pc_f = fname(pc)
    out = [(pc_f or "<outside the program>", pc)]
    if not frames or owners[0] != pc_f:
        # No frame of its own (a leaf, or in its prologue): it returns to LR, in the
        # function that owns frame 0, unless LR has been reused.
        lf = fname(lr)
        if frames and lf != owners[0]:
            out.append((owners[0] or "?", None))
        elif lf is not None:
            out.append((lf, lr - 4))
    for p, ret in frames:
        f = fname(ret)
        if f is None:
            break
        out.append((f, ret - 4))
    return out

from_frame = int(arg("--from-frame")) if "--from-frame" in sys.argv else None  # v2: game frames begun
until_frame = int(arg("--until-frame")) if "--until-frame" in sys.argv else None
timer_only = "--timer" in sys.argv
drop_self = [x for x in (arg("--drop-self") or "").split(",") if x]  # e.g. Phase_Now_Us on RPCEmu  # v2: only interrupts with the 100 Hz timer (IOMD IRQ A bit 5) pending
sel = []
for s in samples:
    if timer_only and not (s[39] & 0x20):
        continue
    if v2 and from_frame is not None and s[38] < from_frame:
        continue
    if v2 and until_frame is not None and s[38] > until_frame:
        continue
    t = s[0] / 100.0
    if t < t_from or t > t_to:
        continue
    if thread is not None and s[2] != int(thread):
        continue
    st = stack_of(s)
    if drop_self and st and any(d == st[0][0] for d in drop_self):
        continue
    names = [f for f, _ in st]
    if require and not any(r in n for n in names for r in require):
        continue
    if exclude and any(e in n for n in names for e in exclude):
        continue
    sel.append((s, st))

total_w = sum(s[1] for s, _ in sel) or 1
span = max((s[0] for s, _ in sel), default=0) - min((s[0] for s, _ in sel), default=0)
selfw = collections.Counter()
incl = collections.Counter()
for s, st in sel:
    w = s[1]
    selfw[st[0][0] if st else "?"] += w
    for f in set(f for f, _ in st):
        incl[f] += w

def table(title, rows, header):
    print()
    print(("### " if md else "== ") + title)
    if md:
        print("| " + " | ".join(header) + " |")
        print("|" + "|".join(["---"] * len(header)) + "|")
        for r in rows:
            print("| " + " | ".join(str(x) for x in r) + " |")
    else:
        for r in rows:
            print("  ".join(str(x) for x in r))

if v2:
    allints = count + h5 + h6 + h7
    print("%s: %d interrupts in this program's code (%d selected%s), %d in the OS (%.1f%% of all interrupts), %d in other code, %d after the buffer filled" % (
        os.path.basename(path), count, len(sel), ", timer only" if timer_only else "", h5, 100.0 * h5 / max(1, allints), h6, h7))
else:
    print("%s: %d samples (%d selected, %.1f s of %.1f s weighted), %d lost frames, threads %s" % (
        os.path.basename(path), count, len(sel), total_w / 100.0, span_cs / 100.0, lost,
        dict(collections.Counter(s[2] for s, _ in sel))))
table("Self time", [("%.1f%%" % (100.0 * w / total_w), "`%s`" % f if md else f) for f, w in selfw.most_common(top)],
      ["self", "function"])
table("Total time (on the stack)", [("%.1f%%" % (100.0 * w / total_w), "`%s`" % f if md else f) for f, w in incl.most_common(top)],
      ["total", "function"])

for want in callers_of:
    for f in sorted(set(f for f in selfw if want in f), key=lambda f: -selfw[f])[:4]:
        by_caller = collections.Counter()
        by_site = collections.Counter()
        for s, st in sel:
            if st and st[0][0] == f:
                if len(st) > 1:
                    by_caller[st[1][0]] += s[1]
                    a = st[1][1]
                    by_site["%s+0x%x" % (st[1][0], a - fstart(a)) if a else st[1][0]] += s[1]
                else:
                    by_caller["?"] += s[1]
        fw = selfw[f] or 1
        table("Callers of %s (self %.1f%% of all)" % (f, 100.0 * selfw[f] / total_w),
              [("%.1f%%" % (100.0 * w / fw), "`%s`" % c if md else c) for c, w in by_caller.most_common(top)],
              ["of its time", "caller"])
        table("Call sites of %s" % f,
              [("%.1f%%" % (100.0 * w / fw), "`%s`" % c if md else c) for c, w in by_site.most_common(12)],
              ["of its time", "call site"])

# The game's phases: the first entry whose function is on the stack wins, innermost
# categories first so that, say, a Find_Path under Logic counts as paths.
PHASES = [
    ("movie: decode/draw", ["VQA_"]),
    ("sound mixing (mixer thread)", ["Mixer_Callback", "Mix_Channel", "SDL_RunAudio"]),
    ("present (copy to the screen)", ["Video_Render_Frame", "RenderSurface", "Raw_Copy", "Blit_Display", "Show_Rects", "Show_All", "SDL_Flip", "SDL_UpdateRect"]),
    ("paths", ["Find_Path", "Follow_Edge"]),
    ("map and sidebar drawing", ["GScreenClass::Render", "Draw_It", "Redraw_"]),
    ("logic", ["LogicClass::AI", "Logic"]),
    ("events/network", ["Queue_AI"]),
    ("call-back (sound, music)", ["Call_Back"]),
    ("start-up / loading", ["Init_Game", "Read_Scenario", "Start_Scenario", "MixFileClass"]),
]
if "--phases" in sys.argv:
    ph = collections.Counter()
    for s, st in sel:
        names = [f for f, _ in st]
        label = "other"
        for lab, keys in PHASES:
            if any(any(k in n for k in keys) for n in names):
                label = lab
                break
        ph[label] += s[1]
    table("Phases", [("%.1f%%" % (100.0 * w / total_w), lab) for lab, w in ph.most_common()], ["share", "phase"])
