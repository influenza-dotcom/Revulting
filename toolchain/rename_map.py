#!/usr/bin/env python3
"""Work out which unbound 5.0.0 names are RENAMES of 3.6.6 entities whose art
Vulture already ships, versus genuinely new content with no art.

A rename is fixable with a one-line redirect and costs nothing. Genuinely new
content is a legitimate placeholder unless a same-kind stand-in is honest.

Matching is by the entity's HUMAN-READABLE description where the dump provides
it; otherwise by slot-neighbourhood and manual review. We print the evidence
rather than guessing silently.
"""
import re
import pathlib
import collections

ROOT = pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win")
PKG = pathlib.Path(r"C:\Users\dalla\Vulture\VultureNetHack")
PB = ROOT / "win" / "vulture" / "pathb"


def load(p):
    out = collections.defaultdict(dict)
    for line in p.read_text(errors="replace").splitlines():
        f = line.split("\t")
        if len(f) == 3:
            out[f[0]][int(f[1])] = f[2]
    return out


n500 = load(PB / "nh500_names.tsv")
n366 = load(PB / "nh366_names.tsv")

conf = (PKG / "config" / "vulture_tiles.conf").read_text(errors="replace")
defined = set(re.findall(r'^(\w+\.\w+)\s*=\s*"', conf, re.M))
redirects = dict(re.findall(r'^(\w+\.\w+)\s*=>\s*(\w+\.\w+)\s*$', conf, re.M))


def resolves(name, seen=None):
    seen = seen or set()
    if name in seen:
        return False
    seen.add(name)
    if name in defined:
        return not name.endswith(".ERROR")
    if name in redirects:
        return resolves(redirects[name], seen)
    return False


# monsters unbound in 5.0.0
mon500 = n500["monster"]
mon366 = set(n366["monster"].values())

PREFIXES = {"monster": ["monster", "statue", "figurine"],
            "object": ["object", "objicon"]}

for kind in ("monster", "object"):
    cur = n500[kind]
    old = set(n366[kind].values())
    prefixes = PREFIXES[kind]

    unbound = []
    for idx in sorted(cur):
        nm = cur[idx]
        missing = [p for p in prefixes if not resolves(f"{p}.{nm}")]
        if missing:
            unbound.append((idx, nm, missing))

    print("=" * 84)
    print(f"{kind.upper()}: {len(unbound)} entities missing art in at least one range")
    print("=" * 84)

    # Which 3.6.6 names vanished in 5.0.0? Those are rename candidates.
    gone = sorted(old - set(cur.values()))
    print(f"\n3.6.6 names that no longer exist in 5.0.0 ({len(gone)}) -- rename sources:")
    for g in gone:
        have = [p for p in prefixes if resolves(f"{p}.{g}")]
        print(f"   {g:<32} art present in: {', '.join(have) if have else 'NONE'}")

    print(f"\nunbound 5.0.0 names, with best rename guess:")
    for idx, nm, missing in unbound:
        # crude but transparent: share a leading token with a vanished old name
        toks = set(nm.replace("PM_", "").split("_"))
        best, score = None, 0
        for g in gone:
            gt = set(g.replace("PM_", "").split("_"))
            s = len(toks & gt)
            if s > score:
                best, score = g, s
        note = ""
        if best and score:
            ok = [p for p in prefixes if resolves(f"{p}.{best}")]
            note = f"  <= maybe {best} (art: {','.join(ok) if ok else 'NONE'})"
        print(f"   {idx:>4} {nm:<32} missing:{','.join(missing):<28}{note}")
    print()
