#!/usr/bin/env python3
"""Insert a block of tile-config lines immediately before an anchor line, in the
AUTHORED config, then deploy the result to the shipped package.

Usage: insert_conf_block.py <block-file> <anchor-line-substring>

Writes UTF-8 with LF endings. Refuses to run if the anchor is missing or the
block appears to already be present, so re-running is safe.
"""
import sys
import pathlib

AUTHORED = pathlib.Path(
    r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\gamedata\config\vulture_tiles.conf")
SHIPPED = pathlib.Path(
    r"C:\Users\dalla\Vulture\VultureNetHack\config\vulture_tiles.conf")

block_path = pathlib.Path(sys.argv[1])
anchor = sys.argv[2]

block = block_path.read_text(encoding="utf-8", errors="replace").rstrip("\n")
conf = AUTHORED.read_text(encoding="utf-8", errors="replace")

# idempotence: bail if any non-comment binding line from the block is already there
new_keys = [l.split()[0] for l in block.splitlines()
            if l.strip() and not l.strip().startswith("#") and "." in l.split()[0]]
already = [k for k in new_keys if any(
    ln.strip().startswith(k + " ") or ln.strip().startswith(k + "\t")
    for ln in conf.splitlines())]
if already:
    print(f"REFUSING: {len(already)} key(s) already present, e.g. {already[:3]}")
    sys.exit(1)

lines = conf.splitlines()
idx = next((i for i, l in enumerate(lines) if anchor in l), None)
if idx is None:
    print(f"REFUSING: anchor not found: {anchor!r}")
    sys.exit(1)

print(f"anchor found at line {idx + 1}: {lines[idx]!r}")
out = lines[:idx] + block.splitlines() + ["", ] + lines[idx:]
AUTHORED.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
print(f"authored config: {len(lines)} -> {len(out)} lines")

SHIPPED.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
print(f"deployed to package: {SHIPPED}")
print(f"inserted {len(new_keys)} binding keys")
