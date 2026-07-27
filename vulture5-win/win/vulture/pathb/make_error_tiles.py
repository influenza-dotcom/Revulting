#!/usr/bin/env python3
"""
Generate deliberately CONSPICUOUS error placeholder tiles, one per Path B category:
  gamedata/tiles/<category>.error.png
Design: loud magenta field, thick black border, black diagonal X, and a per-category
accent corner (so a rendered error hints at which category failed). Unmistakable, and
never resembles ordinary game art. Deterministic output (no RNG).
"""
import os, sys, struct, zlib

W = H = 64
MAGENTA = (255, 0, 255, 255)
BLACK   = (0, 0, 0, 255)
YELLOW  = (255, 255, 0, 255)

# distinct accent per category (corner swatch) for at-a-glance identification
ACCENT = {
    "object":   (255, 128,   0, 255), "objicon": (255, 200,   0, 255),
    "monster":  (  0, 200, 255, 255), "statue":  (  0, 128, 255, 255),
    "figurine": (128,   0, 255, 255), "misc":    (  0, 255, 128, 255),
    "floor":    (  0, 255,   0, 255), "wall":    (255,   0,   0, 255),
    "explosion":(255, 255, 255, 255), "cursor":  (128, 255,   0, 255),
    "edge":     (200, 200, 200, 255),
}


def _chunk(typ, data):
    return (struct.pack(">I", len(data)) + typ + data
            + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))


def write_png(path, pix):
    raw = bytearray()
    for y in range(H):
        raw.append(0)  # filter type 0
        for x in range(W):
            raw += bytes(pix[y][x])
    data = (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + _chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(data)


def make(cat, accent):
    px = [[list(MAGENTA) for _ in range(W)] for _ in range(H)]
    b = 4  # border thickness
    for y in range(H):
        for x in range(W):
            if x < b or x >= W - b or y < b or y >= H - b:
                px[y][x] = list(YELLOW if (x + y) % 8 < 4 else BLACK)  # hazard border
    for d in range(-2, 3):                       # thick diagonal X
        for i in range(W):
            for (xx, yy) in ((i + d, i), (i, i + d), (W - 1 - i + d, i), (W - 1 - i, i + d)):
                if 0 <= xx < W and 0 <= yy < H:
                    px[yy][xx] = list(BLACK)
    for y in range(6, 20):                        # accent corner swatch
        for x in range(6, 20):
            px[y][x] = list(accent)
    return px


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    tiles = os.path.join(root, "win", "vulture", "gamedata", "tiles")
    if len(sys.argv) > 1:
        tiles = sys.argv[1]
    made = []
    for cat, accent in ACCENT.items():
        p = os.path.join(tiles, "%s.error.png" % cat)
        write_png(p, make(cat, accent))
        made.append(os.path.basename(p))
    print("wrote %d error tiles to %s:" % (len(made), tiles))
    for m in sorted(made):
        print("  ", m)


if __name__ == "__main__":
    main()
