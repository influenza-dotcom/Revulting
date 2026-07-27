#!/usr/bin/env python3
"""Remove the two INERT monster blocks (their '=>' targets are 3.6.6 names that
are not live slots in 5.0.0's runtime tilenames[], so vulture_redirect_tile()
discarded every line), then insert the authoritative rekeyed block.

Deploys the result to the shipped package.
"""
import pathlib
import sys

AUTHORED = pathlib.Path(
    r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\gamedata\config\vulture_tiles.conf")
SHIPPED = pathlib.Path(
    r"C:\Users\dalla\Vulture\VultureNetHack\config\vulture_tiles.conf")
TOOLDIR = pathlib.Path(r"C:\Users\dalla\Vulture\toolchain")

conf = AUTHORED.read_text(encoding="utf-8", errors="replace")

# remove each previously inserted block by exact text (as written by
# insert_conf_block.py: block text + one blank line)
removed = 0
for name in ("monsters_renames_block.conf", "cavedweller_block.conf"):
    block = (TOOLDIR / name).read_text(encoding="utf-8", errors="replace").rstrip("\n")
    needle = block + "\n\n"
    if needle in conf:
        conf = conf.replace(needle, "", 1)
        removed += 1
        print(f"removed inert block: {name}")
    else:
        print(f"WARNING: block not found verbatim: {name}")

if removed != 2:
    print("REFUSING: expected to remove exactly 2 blocks")
    sys.exit(1)

new_block = (TOOLDIR / "monsters_final_block.conf").read_text(
    encoding="utf-8", errors="replace").rstrip("\n")

# idempotence: the new block's first binding must not already be present
if "monster.PM_KOBOLD_LEADER            =" in conf:
    print("REFUSING: final block appears to already be present")
    sys.exit(1)

anchor = "monster.default   => monster.ERROR"
lines = conf.splitlines()
idx = next((i for i, l in enumerate(lines) if anchor in l), None)
if idx is None:
    print("REFUSING: anchor not found")
    sys.exit(1)

out = lines[:idx] + new_block.splitlines() + [""] + lines[idx:]
text = "\n".join(out) + "\n"
AUTHORED.write_text(text, encoding="utf-8", newline="\n")
SHIPPED.write_text(text, encoding="utf-8", newline="\n")
print(f"authored + shipped now {len(out)} lines")
