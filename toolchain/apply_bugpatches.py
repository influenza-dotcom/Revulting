#!/usr/bin/env python3
"""Apply extracted old/new patch pairs by exact substring replacement.
Refuses unless old_text occurs exactly once. Idempotent: skips a patch whose
new_text is already present."""
import pathlib
import sys

TOOL = pathlib.Path(r"C:\Users\dalla\Vulture\toolchain")
TARGETS = {
    1: pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\winclass\menuwin.cpp"),
    2: pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win\win\vulture\vulture_tile.cpp"),
}

failed = False
for n, target in TARGETS.items():
    old = (TOOL / f"bugpatch_{n}.old.txt").read_text(encoding="utf-8")
    new = (TOOL / f"bugpatch_{n}.new.txt").read_text(encoding="utf-8")
    text = target.read_text(encoding="utf-8", errors="replace")

    if new in text:
        print(f"patch {n}: already applied to {target.name}")
        continue
    count = text.count(old)
    if count != 1:
        print(f"patch {n}: REFUSED - old_text occurs {count} times in {target.name}")
        failed = True
        continue
    target.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
    print(f"patch {n}: applied to {target.name}")

sys.exit(1 if failed else 0)
