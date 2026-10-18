#!/usr/bin/env python3
"""Make the !VanillaTD or !VanillaRA application sprites from riscos/icons/vanilla<game>-icon.png.

Writes two RISC OS sprite files (filetype &FF9), each holding a 34x34
"!vanilla<game>" and an 18x18 "sm!vanilla<game>" icon with a mask:
  !Sprites22  mode 28, 8bpp, with 256 colours of its own chosen for the
              icon (square-pixel modes)
  !Sprites    mode 27, 4bpp, with its own 16-colour palette (the fallback;
              the Wimp scales it for rectangular-pixel modes such as 12)
Both work on RISC OS 3.5 and later, which this port needs. The pictures are made
by make-icon.py; ImageMagick downscales them.

Usage: make-sprites.py <output directory> [game]
  game: td (the default, !vanillatd) or ra (!vanillara).
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = sys.argv[2] if len(sys.argv) > 2 else "td"
PICTURE = os.path.join(ROOT, "riscos", "icons", "vanilla%s-icon.png" % GAME)
NAME = "!vanilla%s" % GAME


def render(size):
    """Return size*size (r, g, b, a) tuples."""
    raw = subprocess.run(
        ["magick", PICTURE, "-filter", "Lanczos", "-resize", "%dx%d" % (size, size), "-unsharp", "0x0.6+0.6+0",
         "-depth", "8", "rgba:-"], check=True, capture_output=True).stdout
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def palette_256(pixels):
    """255 colours for the opaque pixels (Pillow's median cut), plus black."""
    from PIL import Image
    opaque = [p[:3] for p in pixels if p[3] >= 128]
    im = Image.new("RGB", (len(opaque), 1))
    im.putdata(opaque)
    pal = im.quantize(colors=255, method=Image.Quantize.MEDIANCUT).getpalette()[:255 * 3]
    pal = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    return pal + [(0, 0, 0)] * (256 - len(pal))


def nearest(pal, c):
    return min(range(len(pal)), key=lambda i: sum((pal[i][k] - c[k]) ** 2 for k in range(3)))


def kmeans_16(pixels):
    """Pick 16 colours for the opaque pixels (simple k-means)."""
    pts = [p[:3] for p in pixels if p[3] >= 128]
    uniq = sorted(set(pts))
    pal = [uniq[i * len(uniq) // 16] for i in range(16)] if len(uniq) >= 16 else uniq + [(0, 0, 0)] * (16 - len(uniq))
    for _ in range(20):
        sums = [[0, 0, 0, 0] for _ in pal]
        for c in pts:
            s = sums[nearest(pal, c)]
            s[0] += c[0]; s[1] += c[1]; s[2] += c[2]; s[3] += 1
        pal = [(s[0] // s[3], s[1] // s[3], s[2] // s[3]) if s[3] else pal[i] for i, s in enumerate(sums)]
    return pal


def pack_rows(values, w, h, bpp):
    """Pack pixel values into RISC OS sprite rows (whole words, pixel 0 in the low bits)."""
    words = (w * bpp + 31) // 32
    out = bytearray()
    for y in range(h):
        bits = 0
        for x in range(w):
            bits |= values[y * w + x] << (x * bpp)
        out += bits.to_bytes(words * 4, "little")
    return words, bytes(out)


def sprite(name, pixels, size, mode, bpp, pal, store_palette):
    idx = [nearest(pal, p[:3]) if p[3] >= 128 else 0 for p in pixels]
    mask = [(1 << bpp) - 1 if p[3] >= 128 else 0 for p in pixels]
    words, image = pack_rows(idx, size, size, bpp)
    _, maskdata = pack_rows(mask, size, size, bpp)
    palette = b"".join(struct.pack("<II", *(2 * [b << 24 | g << 16 | r << 8])) for r, g, b in pal) if store_palette else b""
    header = 44 + len(palette)
    total = header + len(image) + len(maskdata)
    return struct.pack("<I12sIIIIIII", total, name.encode("latin-1"), words - 1, size - 1, 0,
                       (size * bpp - 1) % 32, header, header + len(image), mode) + palette + image + maskdata, idx


def sprite_file(sprites):
    body = b"".join(sprites)
    # A sprite file is a sprite area without its first (size) word.
    return struct.pack("<III", len(sprites), 16, 16 + len(body)) + body


def preview(path, pixels_idx, pal, size):
    """Write a PNG of what the sprite looks like, for checking."""
    raw = bytes(c for i in pixels_idx for c in pal[i])
    subprocess.run(["magick", "-size", "%dx%d" % (size, size), "-depth", "8", "rgb:-", "-scale", "400%", path],
                   input=raw, check=True)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    big, small = render(34), render(18)
    pal256 = palette_256(big + small)
    pal16 = kmeans_16(big + small)
    files = {"!Sprites22,ff9": (28, 8, pal256, True), "!Sprites,ff9": (27, 4, pal16, True)}
    for fname, (mode, bpp, pal, store) in files.items():
        spr = []
        for name, pixels, size in ((NAME, big, 34), ("sm" + NAME, small, 18)):
            data, idx = sprite(name, pixels, size, mode, bpp, pal, store)
            spr.append(data)
            if os.environ.get("PREVIEW"):
                preview(os.path.join(os.environ["PREVIEW"], "%s-%s.png" % (fname.split(",")[0].lstrip("!"), name.lstrip("!"))), idx, pal, size)
        with open(os.path.join(out, fname), "wb") as f:
            f.write(sprite_file(spr))
        print(os.path.join(out, fname))


main()
