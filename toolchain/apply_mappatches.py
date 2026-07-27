#!/usr/bin/env python3
"""Apply the 17 extracted map/sed patches by exact substring replacement,
using the manifest for target files. Refuses unless old_text occurs exactly
once; skips already-applied patches."""
import json
import pathlib
import sys

TOOL = pathlib.Path(r"C:\Users\dalla\Vulture\toolchain")
manifest = json.loads((TOOL / "mappatch_manifest.json").read_text(encoding="utf-8-sig"))

failed = applied = skipped = 0
for entry in manifest:
    n, target = entry["n"], pathlib.Path(entry["file"])
    old = (TOOL / f"mappatch_{n}.old.txt").read_text(encoding="utf-8")
    new = (TOOL / f"mappatch_{n}.new.txt").read_text(encoding="utf-8")
    text = target.read_text(encoding="utf-8", errors="replace")

    if new in text and old not in text:
        print(f"patch {n:2}: already applied  ({target.name})")
        skipped += 1
        continue
    count = text.count(old)
    if count != 1:
        print(f"patch {n:2}: REFUSED - old_text x{count} in {target.name}")
        failed += 1
        continue
    target.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
    print(f"patch {n:2}: applied  ({target.name})")
    applied += 1

print(f"\napplied={applied} skipped={skipped} failed={failed}")
sys.exit(1 if failed else 0)
