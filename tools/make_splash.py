#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""BABSZTYL: a colour picture -> a C header in make_egg.py's format, its 16 colours its own (make_egg.py's are greys),
for screen.c's grey_draw, which takes any 16 RGB565 colours.

  tools/make_splash.py art/babsztyl_splash.jpg app/splash_data.h --name SPLASH --crop 64,0,1136,896 --fit 240x201 \
      --bar 412,844,787,865 --bar-gain 1.6 --preview build/splash.png
  tools/make_splash.py art/babsztyl_splash.jpg app/title_data.h --name TITLE --crop 200,555,1000,800 --fit 232x74 \
      --sub 30,36,44 --dither 0 --preview build/title.png

The picture cropped (--crop, its pixels), scaled to fit WxH (aspect kept), optionally a colour taken off it (--sub: a
background to black), reduced to 16 colours: index 0 black (grey_draw draws nothing there, the screen's black behind
it), 15 found by k-means in CIELAB (each as the screen shows it, RGB565), the darkest of them black itself when it
nearly is (each pixel weighed by how rare its colour is: the few pixels of a haze or a lettering heard as well as a
sky's many); then Floyd-Steinberg (serpentine) at --dither's strength where the picture is smooth (0 none: a smaller
header; 0.5 the default: a sunset's bands gone), none at an edge (lettering stays clean) nor in the near black. Run-length coded a byte a run: bits 0-3 the colour, bits 4-7 the run's length less one (1..16
pixels), rows one after another, no run across a row.

--bar x0,y0,x1,y1: a progress bar's inside in the picture (its pixels, x1/y1 past it): emptied (the colour of its
unfilled part) for the firmware to fill as it boots, NAME_BAR_X/Y/W/H (on the picture, scaled) and NAME_BAR_FILL (the
colour of its filled part, RGB565, --bar-gain times brighter) defined.

Defines NAME_W, NAME_H, NAME_PAL[16], NAME_RLE[]. Needs Pillow and numpy.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def srgb_to_lab(rgb):
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[:, 1] - 16, 500 * (f[:, 0] - f[:, 1]), 200 * (f[:, 1] - f[:, 2])], 1)


def kmeans(px, k, iters=40, seed=1):
    """k centres of px (N x 3, CIELAB), k-means++ seeded, the same every run"""
    rng = np.random.default_rng(seed)
    cen = [px[rng.integers(len(px))]]
    for _ in range(1, k):
        d = np.min(((px[:, None, :] - np.array(cen)[None]) ** 2).sum(2), 1)
        cen.append(px[rng.choice(len(px), p=d / d.sum())])
    cen = np.array(cen)
    for _ in range(iters):
        lbl = np.argmin(((px[:, None, :] - cen[None]) ** 2).sum(2), 1)
        new = np.array([px[lbl == i].mean(0) if np.any(lbl == i) else cen[i] for i in range(k)])
        if np.allclose(new, cen, atol=1e-3):
            break
        cen = new
    return lbl, cen


def to565(rgb):
    """RGB (0..255) as the screen shows it, and its RGB565"""
    q = np.clip(np.round(rgb), 0, 255).astype(int)
    r, g, b = q[..., 0] >> 3, q[..., 1] >> 2, q[..., 2] >> 3
    shown = np.stack([r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2], -1).astype(float)
    return shown, r << 11 | g << 5 | b


def box(s):
    v = [int(x) for x in s.split(",")]
    if len(v) != 4:
        raise argparse.ArgumentTypeError("x0,y0,x1,y1")
    return v


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", type=Path)
    ap.add_argument("header", type=Path)
    ap.add_argument("--name", default="SPLASH")
    ap.add_argument("--crop", type=box, help="x0,y0,x1,y1: the part of the picture taken (default all)")
    ap.add_argument("--fit", default="240x240", help="WxH: scaled to fit (aspect kept)")
    ap.add_argument("--dither", type=float, default=0.5, help="Floyd-Steinberg's strength, 0..1 (0: none)")
    ap.add_argument("--sub", help="r,g,b: taken off every pixel first (a background to black)")
    ap.add_argument("--bar", type=box, help="x0,y0,x1,y1: a progress bar's inside, emptied for the firmware")
    ap.add_argument("--bar-gain", type=float, default=1.0, help="its fill's colour this much brighter")
    ap.add_argument("--preview", type=Path, help="a PNG of what the screen shows, twice the size")
    a = ap.parse_args()

    src = np.asarray(Image.open(a.src).convert("RGB")).astype(float)
    x0, y0, x1, y1 = a.crop or (0, 0, src.shape[1], src.shape[0])
    fill = None
    if a.bar:                            # the bar emptied: its unfilled colour (its darker pixels) all through it
        bx0, by0, bx1, by1 = a.bar
        inside = src[by0:by1, bx0:bx1].reshape(-1, 3)
        lum = inside.mean(1)
        cut = (lum.min() + lum.max()) / 2
        fill = np.clip(inside[lum > cut].mean(0) * a.bar_gain, 0, 255)
        src[by0:by1, bx0:bx1] = inside[lum <= cut].mean(0)
    if a.sub:
        src = np.clip(src - np.array([float(v) for v in a.sub.split(",")]), 0, 255)
    part = Image.fromarray(src[y0:y1, x0:x1].astype(np.uint8))
    fw, fh = (int(v) for v in a.fit.lower().split("x"))
    k = min(fw / part.width, fh / part.height)
    w, h = max(1, round(part.width * k)), max(1, round(part.height * k))
    img = np.asarray(part.resize((w, h), Image.LANCZOS)).astype(float)

    flat = img.reshape(-1, 3)
    lab = srgb_to_lab(flat)
    # the colours a few pixels have (the haze, the lettering) heard as well as the sky's many: each pixel weighed
    # by how rare its colour is (a coarse CIELAB histogram), k-means on a draw by those weights
    cell = np.floor(lab / np.array([6.0, 8.0, 8.0])).astype(int)
    _, inv, cnt = np.unique(cell, axis=0, return_inverse=True, return_counts=True)
    wt = cnt[inv.ravel()] ** -0.6
    pick = np.random.default_rng(2).choice(len(flat), size=min(len(flat), 20000), p=wt / wt.sum())
    _, cen = kmeans(lab[pick], 15)
    # the centres back to RGB: each the mean of the pixels nearest it
    lbl = np.argmin(((lab[:, None, :] - cen[None]) ** 2).sum(2), 1)
    cols = np.array([flat[lbl == i].mean(0) if np.any(lbl == i) else [0, 0, 0] for i in range(15)])
    cols = cols[np.argsort(cols.sum(1))]  # darkest first
    if cols[0].sum() < 45:               # (nearly black: black itself, nothing drawn there)
        cols[0] = 0
    pal, pal565 = to565(np.vstack([[0, 0, 0], cols]))

    # the dither where the picture is smooth (the sky's bands), none at an edge (the lettering, the ridges, the bar)
    # nor in the near black (its noise unseen, and dear): its strength a pixel at a time
    lum = srgb_to_lab(img.reshape(-1, 3))[:, 0].reshape(h, w)
    gy, gx = np.gradient(lum)
    g = np.hypot(gx, gy)
    pad = np.pad(g, 2, mode="edge")
    g = np.max([pad[dy:dy + h, dx:dx + w] for dy in range(5) for dx in range(5)], 0)   # (an edge's neighbours too)
    amt = a.dither * np.clip(1 - (g - 2) / 6, 0, 1) * np.clip((lum - 8) / 10, 0, 1)
    err = img.copy()
    idx = np.zeros((h, w), int)
    for y in range(h):
        d = 1 if y % 2 == 0 else -1
        for x in range(w) if d > 0 else range(w - 1, -1, -1):
            c = err[y, x]
            i = int(np.argmin(((pal - c) ** 2).sum(1)))
            idx[y, x] = i
            if amt[y, x] > 0:
                e = (c - pal[i]) * amt[y, x]
                if 0 <= x + d < w:
                    err[y, x + d] += e * 7 / 16
                if y + 1 < h:
                    if 0 <= x - d < w:
                        err[y + 1, x - d] += e * 3 / 16
                    err[y + 1, x] += e * 5 / 16
                    if 0 <= x + d < w:
                        err[y + 1, x + d] += e / 16
    idx[np.all(pal[idx] == 0, -1)] = 0  # (every black one 0: undrawn)

    rle = []
    for row in idx:
        x = 0
        while x < w:
            c, n = row[x], 1
            while x + n < w and row[x + n] == c and n < 16:
                n += 1
            rle.append((n - 1) << 4 | int(c))
            x += n

    bar = None
    if a.bar:
        bx = round((bx0 - x0) * k)
        by = round((by0 - y0) * k)
        bar = (bx, by, max(1, round((bx1 - x0) * k) - bx), max(1, round((by1 - y0) * k) - by))
    if a.preview:
        a.preview.parent.mkdir(parents=True, exist_ok=True)
        shown = pal[idx].astype(np.uint8)
        if bar:                          # (the bar as it ends: full)
            shown[bar[1]:bar[1] + bar[3], bar[0]:bar[0] + bar[2]] = to565(fill)[0].astype(np.uint8)
        Image.fromarray(shown).resize((w * 2, h * 2), Image.NEAREST).save(a.preview)

    n = a.name
    lines = ["/* SPDX-License-Identifier: MPL-2.0 */",
             f"/* BABSZTYL: {n}, made by tools/make_splash.py from {a.src.name}: {w} x {h}, 16 colours, {len(rle)} bytes."
             " Regenerate it, don't edit it. */",
             "#pragma once", "#include <stdint.h>", f"#define {n}_W {w}", f"#define {n}_H {h}"]
    if bar:
        lines += [f"#define {n}_BAR_X {bar[0]}", f"#define {n}_BAR_Y {bar[1]}", f"#define {n}_BAR_W {bar[2]}",
                  f"#define {n}_BAR_H {bar[3]}", f"#define {n}_BAR_FILL 0x{int(to565(fill)[1]):04X}u"]
    lines += [f"static const uint16_t {n}_PAL[16] = {{" + ", ".join(f"0x{int(v):04X}u" for v in pal565) + "};",
              f"static const uint8_t {n}_RLE[{len(rle)}] = {{"]
    for i in range(0, len(rle), 24):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in rle[i:i + 24]) + ",")
    lines.append("};")
    a.header.write_text("\n".join(lines) + "\n")
    print(f"{a.header}: {w} x {h}, {len(rle)} bytes" + (f", bar {bar}" if bar else ""))


if __name__ == "__main__":
    main()
