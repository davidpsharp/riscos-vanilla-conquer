#!/usr/bin/env python3
"""Zip a directory tree for RISC OS, keeping filetypes.

Files named NAME,xxx (the HostFS/NFS convention) are stored as NAME with the
filetype in an Acorn "ARC0" extra field (tag 0x4341), which SparkFS,
SparkPlug and Info-ZIP on RISC OS all read. Other files are stored as Text.

Usage: riscos-zip.py <out.zip> <directory>...
  Each directory is added under its own leaf name.
"""
import os
import re
import struct
import sys
import time
import zipfile

TYPED = re.compile(r"^(.*),([0-9a-fA-F]{3})$")
TEXT = 0xFFF
EPOCH_1900 = 2208988800  # seconds from 1900-01-01 to 1970-01-01


def acorn_extra(filetype, mtime):
    cs = int((mtime + EPOCH_1900) * 100)  # RISC OS time: centiseconds since 1900
    load = 0xFFF00000 | (filetype << 8) | ((cs >> 32) & 0xFF)
    exec_ = cs & 0xFFFFFFFF
    attr = 0x13  # owner read/write, public read
    return struct.pack("<HH4sIIII", 0x4341, 20, b"ARC0", load, exec_, attr, 0)


def add(z, path, arcname):
    mtime = os.stat(path).st_mtime
    zi = zipfile.ZipInfo(arcname, time.localtime(mtime)[:6])
    if os.path.isdir(path):
        zi.filename = arcname + "/"
        zi.external_attr = (0o40755 << 16) | 0x10
        z.writestr(zi, b"")
        return
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    m = TYPED.match(arcname)
    filetype = TEXT
    if m:
        zi.filename, filetype = m.group(1), int(m.group(2), 16)
    zi.extra = acorn_extra(filetype, mtime)
    with open(path, "rb") as f:
        z.writestr(zi, f.read())


def main():
    out, dirs = sys.argv[1], sys.argv[2:]
    with zipfile.ZipFile(out, "w") as z:
        for top in dirs:
            base = os.path.dirname(os.path.abspath(top))
            for root, subdirs, files in os.walk(top):
                subdirs.sort()
                rel = os.path.relpath(root, base)
                add(z, root, rel)
                for name in sorted(files):
                    if name.startswith("."):
                        continue
                    add(z, os.path.join(root, name), os.path.join(rel, name))
    print(out)


main()
