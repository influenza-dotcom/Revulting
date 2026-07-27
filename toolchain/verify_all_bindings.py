#!/usr/bin/env python3
"""Definitive list of EVERY NetHack 5.0.0 entity that renders as the magenta
ERROR placeholder in the Vulture port -- objects, monsters, statues, figurines.

Authoritative names come from win/vulture/pathb/nh500_names.tsv, captured by the
port's dump_names.c from init_objnames()/init_monnames() at runtime. Do NOT
validate against include/*.h: the C enum names differ from the runtime names
(objects.h says GENERIC_RING; the runtime name is RIN_GENERIC_RING).

A monster name feeds THREE tile ranges: monster.*, statue.*, figurine.*
An object name feeds TWO:                object.*, objicon.*
"""
import re
import pathlib
import sys
import collections

ROOT = pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win")
PKG = pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack")

tsv = (ROOT / "win" / "vulture" / "pathb" / "nh500_names.tsv").read_text(errors="replace")
by_kind = collections.defaultdict(dict)
for line in tsv.splitlines():
    p = line.split("\t")
    if len(p) == 3:
        by_kind[p[0]][int(p[1])] = p[2]

print("name dump contains:")
for k in sorted(by_kind):
    print(f"  {k:<10} {len(by_kind[k])} entries")
print()

conf = (PKG / "config" / "vulture_tiles.conf").read_text(errors="replace")

# ---------------------------------------------------------------------------
# NAME LIVENESS -- the lesson of the inert-block incident.
#
# The conf parser maps names to slot indices via tilenames[], which
# vulture_tileconfig.cpp generates AT RUNTIME from the running game's tables
# (init_monnames() from mons[], init_objnames() from objects[]).  A conf line
# keyed on -- or redirecting to -- a name that is not a live 5.0.0 slot is
# SILENTLY DROPPED (vulture_get_tile_index() returns -1; vulture_redirect_tile()
# returns without installing).  A purely textual resolver called such lines
# healthy; the game never saw them.  Model liveness from nh500_names.tsv, which
# was captured from the same generation logic.  "ERROR" is live in every
# category (installed programmatically in the last slot), and LAST-WINS applies
# per key: the conf parser overwrites earlier installs, so later duplicate keys
# supersede earlier ones (used deliberately, e.g. displacer beast).
# ---------------------------------------------------------------------------
live = {"object": set(), "monster": set()}
for line in tsv.splitlines():
    p = line.split("\t")
    if len(p) == 3 and p[0] in live:
        live[p[0]].add(p[2])

LIVE_BY_PREFIX = {
    "object": live["object"] | {"ERROR"},
    "objicon": live["object"] | {"ERROR"},
    "monster": live["monster"] | {"ERROR"},
    "statue": live["monster"] | {"ERROR"},
    "figurine": live["monster"] | {"ERROR"},
}


def is_live(name):
    prefix, _, leaf = name.partition(".")
    known = LIVE_BY_PREFIX.get(prefix)
    if known is None:
        return True     # misc./floor./wall./... categories: not name-checked here
    return leaf in known


# last-wins: later lines overwrite earlier installs for the same key
defined = {}
redirects = {}
for m in re.finditer(r'^(\w+\.\w+)\s*(=>|=)\s*(\S+)', conf, re.M):
    key, op, rhs = m.group(1), m.group(2), m.group(3)
    if not is_live(key):
        continue                      # dropped by the parser: key not a slot
    if op == "=":
        defined[key] = True
        redirects.pop(key, None)
    else:
        if not is_live(rhs):
            continue                  # dropped: target not a slot (dst == -1)
        redirects[key] = rhs
        defined.pop(key, None)


def resolve(name):
    """Model vulture_resolve_tile_id(): at most ONE redirect hop, and the
    terminal slot must carry its own art (a direct definition)."""
    if not is_live(name):
        return ("NOT-A-SLOT", name)
    if name in defined:
        return ("ART", name)
    if name in redirects:
        target = redirects[name]
        if target in defined:
            return ("ART", target)
        if target in redirects:
            return ("CHAIN", target)   # 2+ hops: runtime stops short, no art
        return ("DANGLING", target)
    return ("UNBOUND", name)


# which tile-config prefixes each dumped kind feeds
FEEDS = {
    "object":  ["object", "objicon"],
    "monster": ["monster", "statue", "figurine"],
}

total_bad = 0
untriaged_bad = 0
for kind, prefixes in FEEDS.items():
    entries = by_kind.get(kind, {})
    if not entries:
        print(f"!! no '{kind}' rows in the dump -- cannot validate {prefixes}")
        continue

    bad = collections.OrderedDict()
    for idx in sorted(entries):
        nm = entries[idx]
        broken = []
        for prefix in prefixes:
            k, target = resolve(f"{prefix}.{nm}")
            leaf = target.split(".", 1)[1] if "." in target else target
            if k != "ART":
                broken.append(f"{prefix}:{k}")
            elif leaf == "ERROR":
                # explicit "=> <cat>.ERROR" is a TRIAGED placeholder (deliberate,
                # documented in the conf); reaching ERROR via the category
                # default means nobody has looked at it yet.
                explicit = f"{prefix}.{nm}" in redirects and \
                    redirects[f"{prefix}.{nm}"].endswith(".ERROR")
                broken.append(f"{prefix}:{'TRIAGED' if explicit else 'ERROR'}")
        if broken:
            bad[idx] = (nm, broken)

    print("=" * 78)
    print(f"{kind.upper()}  ({', '.join(p + '.*' for p in prefixes)})")
    print(f"  checked      : {len(entries)}")
    print(f"  fully bound  : {len(entries) - len(bad)}")
    print(f"  PLACEHOLDING : {len(bad)}")
    if bad:
        print()
        for idx, (nm, broken) in bad.items():
            print(f"   {idx:>4}  {nm:<32} {' '.join(broken)}")
    print()
    total_bad += len(bad)
    untriaged_bad += sum(
        1 for _, (nm2, broken2) in bad.items()
        if any(not b.endswith("TRIAGED") for b in broken2))

if total_bad:
    # exit nonzero only for UNTRIAGED failures: an explicit "=> <cat>.ERROR"
    # is a documented, deliberate placeholder and must not fail CI, or the
    # signal drowns and real regressions go unnoticed.
    if untriaged_bad:
        print(f"FAIL: {untriaged_bad} entity/entities placehold WITHOUT triage.")
        sys.exit(1)
    print(f"PASS-WITH-PLACEHOLDERS: {total_bad} triaged placeholder(s), "
          f"0 untriaged failures.")
    sys.exit(0)
print("PASS: every entity resolves to real art in every range.")
