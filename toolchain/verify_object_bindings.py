#!/usr/bin/env python3
"""Definitive list of every NetHack 5.0.0 object that renders as the magenta
ERROR placeholder in the Vulture port.

Authoritative object names come from win/vulture/pathb/nh500_names.tsv, which the
port's dump_names.c captured from init_objnames() at runtime -- NOT from
include/objects.h, whose C enum names differ (objects.h says GENERIC_RING; the
runtime name is RIN_GENERIC_RING).

Checks both ranges:
  object.*   map art
  objicon.*  inventory art
"""
import re
import pathlib
import sys
import collections

ROOT = pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win")
PKG = pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack")

tsv = (ROOT / "win" / "vulture" / "pathb" / "nh500_names.tsv").read_text(errors="replace")
objs = {}
for line in tsv.splitlines():
    p = line.split("\t")
    if len(p) == 3 and p[0] == "object":
        objs[int(p[1])] = p[2]

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


bad = collections.defaultdict(list)
for idx in sorted(objs):
    nm = objs[idx]
    for prefix in ("object", "objicon"):
        kind, target = resolve(f"{prefix}.{nm}")
        leaf = target.split(".", 1)[1] if "." in target else target
        if kind != "ART":
            bad[nm].append((prefix, "UNBOUND -> falls to default => ERROR"))
        elif leaf == "ERROR":
            bad[nm].append((prefix, "explicitly ERROR"))

print(f"5.0.0 objects checked : {len(objs)}")
print(f"objects with art      : {len(objs) - len(bad)}")
print(f"objects that PLACEHOLD: {len(bad)}")
print()

if bad:
    # group by prefix family to make the pattern obvious
    fam = collections.Counter()
    for nm in bad:
        fam[nm.split("_")[0]] += 1
    print("by name prefix:")
    for k, v in fam.most_common():
        print(f"  {k:<12} {v}")
    print()
    print("full list (slot, runtime name, which ranges are broken):")
    for idx in sorted(objs):
        nm = objs[idx]
        if nm in bad:
            ranges = ", ".join(f"{p}({why})" for p, why in bad[nm])
            print(f"  {idx:>4}  {nm:<28} {ranges}")
    sys.exit(1)

print("PASS: every 5.0.0 object resolves to real art in both ranges.")
