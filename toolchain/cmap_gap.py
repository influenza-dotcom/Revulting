#!/usr/bin/env python3
"""Definitive list of NetHack 5.0.0 cmap symbols that Vulture never binds.

Anything listed here renders as V_MISC_ERROR (the magenta placeholder), because
winclass/mapdata.cpp sweeps vulture_tilemap_misc[] to V_MISC_ERROR and then
assigns only the symbols that existed in 3.6.6.
"""
import re
import pathlib

ROOT = pathlib.Path(r"C:\Users\dalla\Vulture\vulture5-win")
defsym = (ROOT / "include" / "defsym.h").read_text(errors="replace")
mapdata = (ROOT / "win" / "vulture" / "winclass" / "mapdata.cpp").read_text(errors="replace")
conf = (ROOT / "win" / "vulture" / "gamedata" / "config" / "vulture_tiles.conf").read_text(errors="replace")

# every cmap symbol 5.0.0 declares, in order, with its explanation
declared = []
for m in re.finditer(r'PCHAR2?\(\s*(\d+),\s*\'(?:[^\']|\\.)*\',\s*(S_\w+),\s*"([^"]*)"', defsym):
    declared.append((int(m.group(1)), m.group(2), m.group(3)))

bound = set(re.findall(r'vulture_tilemap_misc\[(S_\w+)\]\s*=', mapdata))

# what art actually exists, so we can say whether a gap is fixable
art = set(re.findall(r'^(misc|floor|wall)\.(\w+)', conf, re.M))
art_names = {n.upper() for _, n in art}

print(f"cmap symbols declared by 5.0.0 : {len(declared)}")
print(f"symbols bound by mapdata.cpp   : {len(bound)}")
print()
print("UNBOUND -> renders as V_MISC_ERROR placeholder")
print("-" * 78)
gaps = []
for idx, sym, expl in declared:
    if sym in bound:
        continue
    # crude guess at a matching art name
    guess = ""
    stem = sym[2:].upper()
    for cand in sorted(art_names):
        if stem in cand or cand in stem:
            guess = cand
            break
    gaps.append((idx, sym, expl, guess))
    print(f"  {idx:3d}  {sym:<22} {expl:<34} {'art: ' + guess if guess else ''}")
print("-" * 78)
print(f"total unbound: {len(gaps)}")
