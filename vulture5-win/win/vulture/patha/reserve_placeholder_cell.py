#!/usr/bin/env python3
"""
Reserve atlas cell 1199 of the Path A minimap atlas (nethack_3_6_0.png) as the
conspicuous Path A placeholder (V_TILE_PLACEHOLDER). Cell 1199 = (col 39, row 29),
which is blank in the 3.6.x tile set (only 0..1081 are used) and is addressable
(< V_MAX_MAP_SYMBOLS = 1200). Paints magenta + black X so an unresolved 5.0.0 tile
renders unmistakably on the map view. Prints pre/post SHA-256. Idempotent.
"""
import hashlib, os, sys
from PIL import Image

CELL = 1199
COLS, CW, CH = 40, 7, 14
MAGENTA = (255, 0, 255, 255)
BLACK = (0, 0, 0, 255)


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    atlas = os.path.join(root, "win", "vulture", "gamedata", "map", "ascii", "nethack_3_6_0.png")
    print("pre  sha256:", sha(atlas))
    im = Image.open(atlas).convert("RGBA")
    px = im.load()
    x0, y0 = (CELL % COLS) * CW, (CELL // COLS) * CH
    for dy in range(CH):
        for dx in range(CW):
            x, y = x0 + dx, y0 + dy
            edge = dx == 0 or dx == CW - 1 or dy == 0 or dy == CH - 1
            diag = (dx * (CH - 1) // (CW - 1) == dy) or (dx * (CH - 1) // (CW - 1) == CH - 1 - dy)
            px[x, y] = BLACK if (edge or diag) else MAGENTA
    im.save(atlas)
    print("post sha256:", sha(atlas))
    print("reserved cell %d at pixel (%d,%d)..(%d,%d)" % (CELL, x0, y0, x0 + CW - 1, y0 + CH - 1))


if __name__ == "__main__":
    main()
