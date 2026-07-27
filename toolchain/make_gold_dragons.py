#!/usr/bin/env python3
"""Derive gold dragon art from the silver dragon sprites.

Gold dragons are new in NetHack 5.0.0 and Vulture's 3.4.3-era art set has no
tiles for them, so they were left on the conspicuous ERROR placeholder. That
was the right call for BORROWING another colour's art -- in NetHack a dragon's
colour IS its breath and resistance type, so drawing a gold dragon as a red one
would be a lie about a safety-critical property.

RECOLOURING is different: the result actually looks gold, so it asserts the
truth. Silver is the donor because it is effectively a luminance study -- mean
saturation 0.04 -- so there is no existing hue to fight, and the metallic
character of the shading carries over exactly.

Luminance is preserved per pixel (same technique as the novel/glob art):
only hue and saturation are replaced, so every highlight, shadow and outline
the artist drew survives untouched. Alpha is copied verbatim.
"""
import colorsys
import pathlib
from PIL import Image

TILES = [
    pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack\tiles"),
    pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\gamedata\tiles"),
]

# metallic gold: warm yellow hue, strong but not garish saturation
GOLD_HUE = 45.0 / 360.0
GOLD_SAT = 0.62
# slight lift so gold reads brighter than silver at equal luminance
VALUE_GAIN = 1.06

# Recoloured: the living dragon and the figurine.  A dragon's colour IS its
# breath type, so the monster tile MUST read gold; figurines are carved
# amber-toned objects that already vary in tint per species, so a gold one
# asserts nothing false.
PAIRS = [
    ("monster.pm_silver_dragon.png",       "monster.pm_gold_dragon.png"),
    ("figurine.pm_silver_dragon.png",      "figurine.pm_gold_dragon.png"),
    ("monster.pm_baby_silver_dragon.png",  "monster.pm_baby_gold_dragon.png"),
    ("figurine.pm_baby_silver_dragon.png", "figurine.pm_baby_gold_dragon.png"),
]

# Copied VERBATIM: statues.  Every dragon statue in Vulture is stone - red,
# blue and silver all measure ~0.05-0.07 mean saturation, i.e. greyscale - and
# only the pose differs between species.  Recolouring one gold would make the
# gold dragon the single metallic statue in the game and imply a property
# NetHack does not model.  The silver dragon's stone render is simply a stone
# dragon statue, which is exactly what a gold dragon's statue is.
COPIES = [
    ("statue.pm_silver_dragon.png",      "statue.pm_gold_dragon.png"),
    ("statue.pm_baby_silver_dragon.png", "statue.pm_baby_gold_dragon.png"),
]


def goldify(src: Image.Image) -> Image.Image:
    src = src.convert("RGBA")
    out = Image.new("RGBA", src.size)
    sp, op = src.load(), out.load()
    w, h = src.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = sp[x, y]
            if a == 0:
                op[x, y] = (0, 0, 0, 0)
                continue
            _, _, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            v = min(1.0, v * VALUE_GAIN)
            # saturation tapers toward the highlights so speculars stay bright
            sat = GOLD_SAT * (1.0 - 0.45 * v)
            nr, ng, nb = colorsys.hsv_to_rgb(GOLD_HUE, sat, v)
            op[x, y] = (int(nr * 255 + 0.5), int(ng * 255 + 0.5),
                        int(nb * 255 + 0.5), a)
    return out


src_dir = TILES[0]
for src_name, dst_name in PAIRS:
    src_path = src_dir / src_name
    if not src_path.exists():
        print(f"  MISSING donor {src_name}")
        continue
    img = goldify(Image.open(src_path))
    for d in TILES:
        d.mkdir(parents=True, exist_ok=True)
        img.save(d / dst_name, "PNG")
    print(f"  recolour  {src_name:<38} -> {dst_name}  {img.size}")

for src_name, dst_name in COPIES:
    src_path = src_dir / src_name
    if not src_path.exists():
        print(f"  MISSING donor {src_name}")
        continue
    img = Image.open(src_path).convert("RGBA")
    for d in TILES:
        d.mkdir(parents=True, exist_ok=True)
        img.save(d / dst_name, "PNG")
    print(f"  copy      {src_name:<38} -> {dst_name}  {img.size}  (stone, unaltered)")

print("\ndone")
