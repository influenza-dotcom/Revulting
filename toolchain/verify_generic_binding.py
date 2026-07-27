#!/usr/bin/env python3
"""Static check: do NetHack 5.0.0's 17 generic pseudo-objects resolve to real
art instead of falling through to object.default => object.ERROR?

The generic slots are what obj_to_glyph() emits for an object whose specific
type the hero has not seen (obj_is_generic(), include/display.h). They are new
in 5.0.0 and were unbound in Vulture's 3.6.0-era tile config, so every
not-yet-examined gem/potion/spellbook drew as the magenta placeholder.

IMPORTANT: the tile-config keys are NOT the C enum names from include/objects.h.
They are the names init_objnames() generates at runtime, captured in
win/vulture/pathb/nh500_names.tsv by the port's dump_names.c. Validating against
objects.h instead gives false failures (e.g. objects.h says GENERIC_RING but the
runtime name is RIN_GENERIC_RING).
"""
import re
import pathlib
import sys

ROOT = pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win")
PKG = pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack")

# authoritative runtime names, slots 1..LAST_GENERIC (slot 0 is STRANGE_OBJECT)
tsv = (ROOT / "win" / "vulture" / "pathb" / "nh500_names.tsv").read_text(errors="replace")
names = {}
for line in tsv.splitlines():
    parts = line.split("\t")
    if len(parts) == 3 and parts[0] == "object":
        names[int(parts[1])] = parts[2]

generics = [(i, names[i]) for i in sorted(names) if 1 <= i <= 17]
print(f"generic pseudo-object slots (runtime names, from nh500_names.tsv): {len(generics)}")
print()

conf = (PKG / "config" / "vulture_tiles.conf").read_text(errors="replace")
defined = set(re.findall(r'^(\w+\.\w+)\s*=\s*"', conf, re.M))
redirects = dict(re.findall(r'^(\w+\.\w+)\s*=>\s*(\w+\.\w+)\s*$', conf, re.M))


def resolve(name, seen=None):
    seen = seen or set()
    if name in seen:
        return ("CYCLE", name)
    seen.add(name)
    if name in defined:
        return ("ART", name)
    if name in redirects:
        return resolve(redirects[name], seen)
    return ("UNBOUND", name)


failures = []
print(f"{'slot':>4}  {'runtime name':<26} {'object.* art':<24} {'objicon.* art'}")
print("-" * 92)
for idx, enum in generics:
    cells = []
    for prefix in ("object", "objicon"):
        kind, target = resolve(f"{prefix}.{enum}")
        leaf = target.split(".", 1)[1] if "." in target else target
        if kind != "ART":
            cells.append(f"!! {kind}")
            failures.append((f"{prefix}.{enum}", kind))
        elif leaf == "ERROR":
            cells.append("!! ERROR PLACEHOLDER")
            failures.append((f"{prefix}.{enum}", "ERROR"))
        else:
            cells.append(leaf)
    print(f"{idx:>4}  {enum:<26} {cells[0]:<24} {cells[1]}")
print("-" * 92)

if failures:
    print(f"\nFAIL: {len(failures)} generic slot(s) do not resolve to real art")
    for name, kind in failures:
        print(f"  {kind:<8} {name}")
    sys.exit(1)

print(f"\nPASS: all {len(generics)} generic slots resolve to real art in both the")
print("      object.* (map) and objicon.* (inventory) ranges.")
print("      None reaches object.default => object.ERROR.")
