#!/usr/bin/env python3
"""Make Red Alert's benchmark battle: Allied mission 1 with two big armies and no triggers.

Usage: make-ra-battle.py <SCG01EA.INI> <out.INI>

SCG01EA.INI comes from the game's own data (it isn't in this repository):
  ramix extract MAIN.MIX GENERAL.MIX general.mix
  ramix extract general.mix SCG01EA.INI SCG01EA.INI
The units and infantry are replaced by two armies, all set to Hunt: the Allies
(Greece) along the north edge of the map and the Soviets (USSR) along the south,
with the Soviet base left between them, and the triggers and teams removed so
the mission never ends. Units placed on cells they can't stand on are left out by
the game. With VC_SEED the battle plays out the same every time, on any machine.
"""
import sys


def main():
    s = open(sys.argv[1], "rb").read().decode("latin-1").replace("\r\n", "\n")

    def replace_section(text, name, lines):
        a = text.index("[%s]" % name)
        b = text.find("\n[", a + 1)
        b = len(text) if b < 0 else b
        return text[:a] + "\n".join(["[%s]" % name] + lines) + "\n" + text[b:]

    def cell(x, y):
        return y * 128 + x

    units = []
    infantry = []

    def unit(house, kind, x, y, facing):
        units.append("%d=%s,%s,256,%d,%d,Hunt,None" % (len(units), house, kind, cell(x, y), facing))

    def man(house, kind, x, y, sub, facing):
        infantry.append("%d=%s,%s,256,%d,%d,Hunt,%d,None" % (len(infantry), house, kind, cell(x, y), sub, facing))

    allied = ["2TNK", "2TNK", "1TNK", "ARTY", "JEEP", "APC"]
    soviet = ["3TNK", "3TNK", "4TNK", "V2RL", "FTRK", "3TNK"]
    import os
    north, south = (int(v) for v in os.environ.get("RA_BATTLE_ROWS", "52,72").split(","))
    for i, x in enumerate(range(51, 77)):
        unit("Greece", allied[i % len(allied)], x, north, 128)
        unit("USSR", soviet[i % len(soviet)], x, south, 0)
    for i, x in enumerate(range(52, 76, 2)):
        unit("Greece", allied[i % len(allied)], x, north - 1, 128)
        unit("USSR", soviet[i % len(soviet)], x, south + 1, 0)
    for i, x in enumerate(range(51, 77)):
        for sub in (1, 3):
            man("Greece", ["E1", "E3", "E1"][(i + sub) % 3], x, north + 1, sub, 128)
            man("USSR", ["E1", "E2", "E4"][(i + sub) % 3], x, south - 1, sub, 0)

    s = replace_section(s, "UNITS", units)
    s = replace_section(s, "INFANTRY", infantry)
    for name in ("Trigs", "TeamTypes", "CellTriggers"):
        if "[%s]" % name in s:
            s = replace_section(s, name, [])
    open(sys.argv[2], "wb").write(s.replace("\n", "\r\n").encode("latin-1"))
    print("%d units, %d infantry" % (len(units), len(infantry)))


if __name__ == "__main__":
    main()
