#!/usr/bin/env python3
"""Extract the best frame from nh_icon.ico into a PNG the SDL window icon can use.

Vulture's image loader is PNG-only, so the shipped .ico (which the linked .rc
resource gives the EXE) cannot be handed to SDL_SetWindowIcon directly. Emit a
256x256 RGBA PNG next to the other graphics instead of inventing new art.
"""
import pathlib
from PIL import Image

SRC = pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\gamedata\nh_icon.ico")
OUTS = [
    pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\gamedata\graphics\window_icon.png"),
    pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack\graphics\window_icon.png"),
]

im = Image.open(SRC)
print(f"{SRC.name}: frames available = {sorted(im.info.get('sizes', []))}")

# pick the largest frame the .ico actually contains
best = max(im.info.get("sizes", [(im.width, im.height)]))
im.size = best
im = im.convert("RGBA")
print(f"chose {im.size} {im.mode}")

if im.size != (256, 256):
    im = im.resize((256, 256), Image.LANCZOS)
    print("resized to 256x256")

for out in OUTS:
    out.parent.mkdir(parents=True, exist_ok=True)
    im.save(out, "PNG")
    print(f"wrote {out}  ({out.stat().st_size} bytes)")
