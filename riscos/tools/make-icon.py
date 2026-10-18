#!/usr/bin/env python3
"""Make the !VanillaTD and !VanillaRA icon pictures from the OpenTS banner.

They match the OpenTS RISC OS port's icon: letters from the banner on a gold-rimmed bronze
chevron. The banner spells "OpenTS", so its T is used as it is, and D, R and A are made
from pieces of its other letters, keeping their fire texture and bevel:
  D  the e's straight left side and the O's rounded right side
  R  the p, with a sloping leg cut from its bowl's right stroke
  A  the n's arch, with a crossbar from the e's middle arm
The p, e and n are drawn shorter than O, T and S in the banner, so they are stretched to
the same height. TD is then coloured gold and RA red, by moving the fire's orange-to-yellow
range of hues, so its veins stay.

Writes riscos/icons/vanillatd-icon.png and vanillara-icon.png (1024x1024); make-sprites.py
turns them into the applications' sprites.

Usage: make-icon.py <opents-logo.png>
  The banner is https://raw.githubusercontent.com/OpenTS-Developers/.github/main/assets/opents-logo.png
  (1280x389). Needs Pillow and NumPy.
"""
import os
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
N = 1024
Y0, Y1 = 88, 246   # the banner rows that hold every letter


# ---- the banner's letters ----

def letter_mask(logo):
    """The banner's letters: very saturated orange to yellow, unlike the duller gold chevron."""
    a = np.asarray(logo).astype(float) / 255
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    mx, mn = a[..., :3].max(-1), a[..., :3].min(-1)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    d = np.maximum(mx - mn, 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) / 6
    m = Image.fromarray((((s > 0.70) & (h < 0.13) & (al > 0.5) & (mx > 0.35)) * 255).astype(np.uint8))
    m = m.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
    m = m.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    return np.asarray(m) > 127


def components(b):
    """Label the 4-connected regions of a boolean array; returns the labels and each one's size."""
    H, W = b.shape
    lab = np.zeros(b.shape, int)
    sizes = []
    for y in range(H):
        for x in range(W):
            if b[y, x] and not lab[y, x]:
                n = len(sizes) + 1
                q = deque([(y, x)])
                lab[y, x] = n
                count = 0
                while q:
                    cy, cx = q.popleft()
                    count += 1
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < H and 0 <= nx < W and b[ny, nx] and not lab[ny, nx]:
                            lab[ny, nx] = n
                            q.append((ny, nx))
                sizes.append(count)
    return lab, sizes


def letters(logo):
    """The banner with everything but the letters made transparent."""
    m = letter_mask(logo)
    lab, sizes = components(m)
    keep = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s > 3000])   # drop specks of chevron
    lab, sizes = components(~keep)
    for i, s in enumerate(sizes):
        if s < 600:                                                        # fill specks in the letters
            keep |= lab == i + 1
    out = logo.copy()
    out.putalpha(Image.fromarray((keep * 255).astype(np.uint8)))
    return np.asarray(out).copy()


# ---- T, D, R and A ----

def image(a):
    return Image.fromarray(a, "RGBA")


def words(L):
    def glyph(x0, x1):
        return L[Y0:Y1, x0:x1].copy()

    def clear(a, y0, y1, x0, x1):
        a[y0 - Y0:y1 - Y0, x0:x1] = 0

    def tall(a):
        """p, e and n are drawn shorter (rows 118-240) than O, T and S (94-240): stretch them."""
        im = image(a)
        part = im.crop((0, 118 - Y0, im.width, 241 - Y0)).resize((im.width, 241 - 94), Image.LANCZOS)
        out = Image.new("RGBA", im.size, (0, 0, 0, 0))
        out.paste(part, (0, 94 - Y0))
        return np.asarray(out).copy()

    T, O, P, E, Nn = glyph(774, 914), glyph(220, 362), glyph(372, 506), glyph(515, 629), glyph(640, 765)

    # R: the p, its chevron stub cut off, with a leg from under the middle of the bowl down to
    # the bottom right, cut from the bowl's straight right stroke
    R = P.copy()
    clear(R, 168, 188, 0, 12)
    legh, slope = 241 - 196, 34
    leg = image(P[142 - Y0:182 - Y0, 89:134]).resize((45, legh), Image.LANCZOS)
    leg = leg.transform((45 + slope, legh), Image.AFFINE, (1, -slope / legh, 0, 0, 1, 0), resample=Image.BICUBIC)
    R = np.concatenate([R, np.zeros((R.shape[0], 6, 4), np.uint8)], 1)
    Ri = image(R)
    Ri.alpha_composite(leg, (89 - slope + 6, 196 - Y0))
    R = np.asarray(Ri).copy()

    # A: the n's arch, with a crossbar from the e's middle arm between (and under) its legs
    bar = image(E[172 - Y0:186 - Y0, 60:100]).resize((40, 15), Image.LANCZOS)
    Ai = Image.new("RGBA", (Nn.shape[1], Nn.shape[0]), (0, 0, 0, 0))
    Ai.alpha_composite(bar, (48, 184 - Y0))
    Ai.alpha_composite(image(Nn), (0, 0))
    A = np.asarray(Ai).copy()

    # D: the e's straight left side (stem and the ends of its top and bottom arms), and the
    # O's rounded right side, each without the bits between that belong to other strokes
    El = E[:, 0:60].copy()
    clear(El, 160, 198, 55, 60)
    Or = O[:, 58:].copy()
    clear(Or, 160, 196, 0, 88 - 58)

    R, A, El = tall(R), tall(A), tall(El)
    D = np.concatenate([El, Or], 1)

    def word(*gs, gap=8):
        w = sum(x.shape[1] for x in gs) + gap * (len(gs) - 1)
        out = Image.new("RGBA", (w, gs[0].shape[0]), (0, 0, 0, 0))
        x = 0
        for a in gs:
            out.alpha_composite(image(a), (x, 0))
            x += a.shape[1] + gap
        return out

    return word(T, D), word(R, A)


# ---- colours and the chevron ----

def shift(img, base, span, sat_mul, val_mul, val_add=0):
    """Moves the fire's orange-to-yellow range (hue 0.03-0.14) onto base..base+span, keeping
    each pixel's place in it, and its brightness, so the veins stay."""
    a = np.asarray(img.convert("RGB")).astype(float) / 255
    mx, mn = a.max(-1), a.min(-1)
    v = mx
    d = np.maximum(mx - mn, 1e-6)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) / 6
    hh = (base + span * np.clip((h - 0.03) / 0.11, 0, 1)) % 1.0
    s = np.clip(s * sat_mul, 0, 1)
    v = np.clip(v * val_mul + val_add, 0, 1)
    h6 = hh * 6
    i = np.floor(h6).astype(int) % 6
    f = h6 - np.floor(h6)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    R = np.choose(i, [v, q, p, p, t, v])
    G = np.choose(i, [t, v, v, q, p, p])
    B = np.choose(i, [p, p, t, v, v, q])
    return Image.fromarray((np.stack([R, G, B], -1) * 255).astype(np.uint8), "RGB")


def gradient(top, bottom):
    t = np.linspace(0, 1, N)[:, None, None]
    a = np.array(top)[None, None, :] * (1 - t) + np.array(bottom)[None, None, :] * t
    return Image.fromarray(np.broadcast_to(a, (N, N, 3)).astype(np.uint8), "RGB").convert("RGBA")


def polygon(points):
    m = Image.new("L", (N, N), 0)
    ImageDraw.Draw(m).polygon([(x * N, y * N) for x, y in points], fill=255)
    return m


def chevron():
    """The OpenTS icon's chevron, exactly; its tip goes just past the bottom so the last row stays solid."""
    icon = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    icon.paste(gradient((255, 236, 150), (150, 110, 40)), (0, 0), polygon([(0.0, 0.0), (1.0, 0.0), (0.5, 1.05)]))
    icon.paste(gradient((105, 86, 50), (45, 36, 18)), (0, 0), polygon([(0.075, 0.06), (0.925, 0.06), (0.5, 0.955)]))
    icon.paste(gradient((240, 200, 110), (190, 140, 50)), (0, 0),
               polygon([(0.22, 0.63), (0.31, 0.63), (0.5, 0.81), (0.69, 0.63), (0.78, 0.63), (0.5, 0.885)]))
    return icon


def make(word, colour):
    icon = chevron()
    w = int(N * 0.95)
    word = word.resize((w, int(word.height * w / word.width)), Image.LANCZOS)
    mask = word.getchannel("A")
    rgb = shift(word.convert("RGB"), *colour)
    face = rgb.convert("RGBA")
    face.putalpha(mask)
    shadow = Image.new("RGBA", face.size, (0, 0, 0, 0))
    shadow.putalpha(mask.point(lambda v: int(v * 0.7)))
    shadow = shadow.filter(ImageFilter.GaussianBlur(10))
    outline = Image.new("RGBA", face.size, (30, 18, 6, 0))
    outline.putalpha(mask.filter(ImageFilter.MaxFilter(17)))   # a dark edge so the letters read at 18x18
    x, y = (N - w) // 2, int(N * 0.135)
    icon.alpha_composite(shadow, (x + 14, y + 18))
    icon.alpha_composite(outline, (x, y))
    icon.alpha_composite(face, (x, y))
    return icon


GOLD = (0.105, 0.05, 0.78, 1.0, 0.03)   # amber to yellow-gold
RED = (0.975, 0.055, 1.1, 0.9)          # crimson to orange-red


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    logo = Image.open(sys.argv[1]).convert("RGBA")
    if logo.size != (1280, 389):
        sys.exit("expected the 1280x389 banner, got %dx%d" % logo.size)
    td, ra = words(letters(logo))
    for name, word, colour in (("vanillatd", td, GOLD), ("vanillara", ra, RED)):
        out = os.path.join(ROOT, "riscos", "icons", "%s-icon.png" % name)
        make(word, colour).save(out)
        print(out)


main()
