#!/usr/bin/env python3
"""Make the benchmark battle: GDI mission 1 with two big armies and no triggers.

Usage: make-battle.py <GDI GENERAL.MIX> <out.INI>

Takes SCG01EA.INI from the game's own GENERAL.MIX (the game data isn't in this
repository), replaces its units and infantry with 57 vehicles and 80 infantry,
all set to Hunt, on the open ground east of the ridge, and removes the
triggers so the mission never ends. With VC_SEED the battle plays out the same
every time, on any machine, so timings can be compared run to run.
"""
import struct
import sys


def td_crc(name):
    """Tiberian Dawn's MIX file ID: rotate left and add, four bytes at a time."""
    n = name.upper().encode()
    n += b"\0" * (-len(n) % 4)
    crc = 0
    for i in range(0, len(n), 4):
        crc = ((crc << 1) | (crc >> 31)) & 0xFFFFFFFF
        crc = (crc + struct.unpack("<I", n[i:i + 4])[0]) & 0xFFFFFFFF
    return crc


def mix_file(path, name):
    data = open(path, "rb").read()
    count, _size = struct.unpack("<hI", data[:6])
    want = td_crc(name)
    base = 6 + count * 12
    for i in range(count):
        crc, off, length = struct.unpack("<III", data[6 + i * 12:18 + i * 12])
        if crc == want:
            return data[base + off:base + off + length].decode("latin-1").replace("\r\n", "\n")
    sys.exit("%s isn't in %s" % (name, path))


def main():
    s = mix_file(sys.argv[1], "SCG01EA.INI")

    def replace_section(text, name, lines):
        a = text.index("[%s]" % name)
        b = text.index("\n[", a + 1)
        return text[:a] + "\n".join(["[%s]" % name] + lines) + "\n" + text[b:]

    def cell(x, y):
        return y * 64 + x

    units = []

    def unit(house, kind, x, y, facing):
        units.append("%03d=%s,%s,256,%d,%d,Hunt,None" % (len(units), house, kind, cell(x, y), facing))

    for x in range(46, 58):
        unit("GoodGuy", "MTNK", x, 53, 0)
    for x in range(48, 54):
        unit("GoodGuy", "HTNK", x, 54, 0)
    for x in range(50, 54):
        unit("GoodGuy", "MSAM", x, 55, 0)
    for x in range(46, 58):
        unit("BadGuy", "LTNK", x, 44, 128)
    for x in range(46, 52):
        unit("BadGuy", "FTNK", x, 45, 128)
    for x in range(52, 58):
        unit("BadGuy", "BGGY", x, 45, 128)
    for x in range(46, 52):
        unit("BadGuy", "BIKE", x, 43, 128)
    for x in range(52, 56):
        unit("BadGuy", "ARTY", x, 43, 128)
    unit("GoodGuy", "MCV", 58, 55, 0)

    infantry = []

    def soldier(house, kind, x, y, sub, facing):
        infantry.append("%03d=%s,%s,256,%d,%d,Hunt,%d,None" % (len(infantry), house, kind, cell(x, y), sub, facing))

    for k, x in enumerate(range(46, 54)):
        for sub in range(5):
            soldier("GoodGuy", ["E1", "E3", "E2", "E1"][k % 4], x, 52, sub, 0)
    for k, x in enumerate(range(46, 54)):
        for sub in range(5):
            soldier("BadGuy", ["E1", "E3", "E4", "E1"][k % 4], x, 42, sub, 128)

    s = replace_section(s, "UNITS", units)
    s = replace_section(s, "INFANTRY", infantry)
    s = replace_section(s, "Triggers", [])
    open(sys.argv[2], "w", encoding="latin-1").write(s)


main()
