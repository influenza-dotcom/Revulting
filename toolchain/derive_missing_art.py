#!/usr/bin/env python3
"""Derive honest art for 5.0.0 items Vulture's 3.4.3-era tile set predates,
by luminance-preserving colorize of existing painted tiles. This keeps the
original painting's shading/outline so the result matches the set's style.

  novel (SPE_NOVEL, appearance "paperback book")
      <- spe_blank_paper, recolored paperback-tan. Blank paper's own WHITE
         tile is an identification channel (writable marker stock), so the
         derived tile must be a color no book in the set uses.
  glob of gray ooze / brown pudding / green slime / black pudding
      <- lump_of_royal_jelly (an amorphous lump), recolored per species.
         The species COLOUR is the identity, so each gets its own tile;
         monster art was rejected (asserts a live hostile).

Outputs to BOTH the authored gamedata tiles dir and the shipped package.
"""
from PIL import Image
import pathlib

PKG_TILES = pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack\tiles")
AUT_TILES = pathlib.Path(
    r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\gamedata\tiles")

# (source stem, output stem, target RGB)
JOBS = [
    ("spe_blank_paper",     "spe_novel",             (196, 126,  60)),  # paperback tan
    ("lump_of_royal_jelly", "glob_of_gray_ooze",     (150, 150, 150)),
    ("lump_of_royal_jelly", "glob_of_brown_pudding", (130,  86,  48)),
    ("lump_of_royal_jelly", "glob_of_green_slime",   ( 70, 160,  60)),
    ("lump_of_royal_jelly", "glob_of_black_pudding", ( 55,  50,  55)),
]


def colorize(src: pathlib.Path, dst_name: str, target):
    im = Image.open(src).convert("RGBA")
    px = im.load()
    w, h = im.size

    # normalize against the mean luminance of opaque pixels so the original
    # mid-tones land exactly on the target colour
    lums, count = 0, 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 32:
                lums += 0.299 * r + 0.587 * g + 0.114 * b
                count += 1
    ref = max(1.0, lums / max(1, count))

    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a <= 32:
                continue
            lum = (0.299 * r + 0.587 * g + 0.114 * b) / ref
            px[x, y] = (
                min(255, int(target[0] * lum)),
                min(255, int(target[1] * lum)),
                min(255, int(target[2] * lum)),
                a,
            )
    for outdir in (PKG_TILES, AUT_TILES):
        out = outdir / dst_name
        im.save(out)
        print(f"  {out.name:<34} -> {outdir.name}/ ({out.stat().st_size} b)")


for prefix in ("object", "objicon"):
    for src_stem, dst_stem, rgb in JOBS:
        src = PKG_TILES / f"{prefix}.{src_stem}.png"
        colorize(src, f"{prefix}.{dst_stem}.png", rgb)

print("done: 10 tiles derived")
