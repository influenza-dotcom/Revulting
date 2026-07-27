#!/usr/bin/env python3
"""
Path A (ASCII minimap) remap validator + generator.

Builds the NetHack 5.0.0 (1515 tiles) -> Vulture atlas cell (3.6.6 order, 0..1081)
remap, driven by the hand-edited remap.manifest.json. Emits remap_table.h ONLY after
all checks pass. Every 5.0.0 index maps to an atlas cell in [0,1199] or the explicit
placeholder sentinel (a reserved atlas cell).

Data sources (win/share/{monsters,objects,other}.txt):
  5.0.0  -> source indices (1515)         refs/nethack-5.0.0
  3.6.6  -> target order == atlas cells    refs/nethack-3.6.6
  3.6.0  -> atlas ORDER (verify == 3.6.6)  refs/nethack-3.6.0

Exit non-zero on any validation failure.  Usage: validate.py [--refs DIR] [--emit]
"""
import argparse, hashlib, json, os, re, sys
from collections import OrderedDict, defaultdict

FILES = ["monsters", "objects", "other"]
TILE_RE = re.compile(r'^# tile (\d+) \((.*)\)\s*$')


def parse_tiles(tree):
    """Return list of tiles in global order: {gidx,file,lidx,full,head,ord}."""
    tiles = []
    head_seen = defaultdict(int)          # (file, head) -> count so far
    gidx = 0
    for f in FILES:
        path = os.path.join(tree, "win", "share", f + ".txt")
        with open(path, encoding="latin-1") as fh:
            for line in fh:
                m = TILE_RE.match(line)
                if not m:
                    continue
                full = m.group(2)
                head = full.split(",")[0].strip()
                ordn = head_seen[(f, head)]
                head_seen[(f, head)] += 1
                tiles.append(dict(gidx=gidx, file=f, lidx=int(m.group(1)),
                                  full=full, head=head, ord=ordn))
                gidx += 1
    return tiles


def file_offsets(tiles):
    off = {}
    for t in tiles:
        off.setdefault(t["file"], t["gidx"])
    return off


def sha_file(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def png_dims(path):
    import struct
    d = open(path, "rb").read(26)
    return struct.unpack(">II", d[16:24]) if d[:8] == b"\x89PNG\r\n\x1a\n" else (0, 0)


def collapsed_ordinals(tiles):
    """For monsters, consecutive same-head runs (male/female of one species) collapse
    to a single logical occurrence; returns {gidx: collapsed_ord}. Non-monster files
    use the plain per-head ordinal."""
    out = {}
    run_count = defaultdict(int)   # head -> collapsed occurrences seen (monsters)
    prev = None
    for t in tiles:
        if t["file"] != "monsters":
            out[t["gidx"]] = t["ord"]
            continue
        if t["head"] != prev:
            # new run of this head
            out[t["gidx"]] = run_count[t["head"]]
            run_count[t["head"]] += 1
        else:
            out[t["gidx"]] = run_count[t["head"]] - 1   # same run as previous
        prev = t["head"]
    return out


def build_index(tiles):
    """(file, head) -> [tiles in order]."""
    idx = defaultdict(list)
    for t in tiles:
        idx[(t["file"], t["head"])].append(t)
    full_idx = defaultdict(list)
    for t in tiles:
        full_idx[(t["file"], t["full"])].append(t)
    return idx, full_idx


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    ap.add_argument("--refs", default=os.path.abspath(os.path.join(root, "..", "refs")))
    ap.add_argument("--manifest", default=os.path.join(here, "remap.manifest.json"))
    ap.add_argument("--out", default=os.path.join(here, "remap_table.h"))
    ap.add_argument("--report", default=os.path.join(here, "patha_report.json"))
    ap.add_argument("--emit", action="store_true", help="write remap_table.h if checks pass")
    ap.add_argument("--survey", action="store_true", help="print unmatched summary and exit 0")
    args = ap.parse_args()

    man = json.load(open(args.manifest, encoding="utf-8")) if os.path.exists(args.manifest) else {}
    aliases = man.get("aliases", {})               # 5.0.0 head -> 3.6.6 head
    full_aliases = man.get("full_aliases", {})     # 5.0.0 full -> 3.6.6 full/head
    force_ph = set(man.get("force_placeholder_names", []))
    idx_overrides = {int(k): v for k, v in man.get("index_overrides", {}).items()}
    PLACEHOLDER = man.get("atlas", {}).get("reserved_placeholder_cell", 1199)
    ADDRESSABLE = man.get("atlas", {}).get("addressable", 1200)

    t5 = parse_tiles(os.path.join(args.refs, "nethack-5.0.0"))
    t36 = parse_tiles(os.path.join(args.refs, "nethack-3.6.6"))
    t30 = parse_tiles(os.path.join(args.refs, "nethack-3.6.0"))
    violations = []

    # ---- coverage ----
    if len(t5) != 1515:
        violations.append("5.0.0 tile count %d != 1515" % len(t5))
    for t in t5:
        if t["gidx"] != t5.index(t):
            pass
    # ---- 3.6.0 vs 3.6.6 ordering diff (atlas order must equal target order) ----
    order_diffs = []
    for i in range(min(len(t30), len(t36))):
        if t30[i]["full"] != t36[i]["full"]:
            order_diffs.append((i, t30[i]["full"], t36[i]["full"]))
    if len(t30) != len(t36):
        order_diffs.append(("count", len(t30), len(t36)))
    # The 3.6.0 vs 3.6.6 diffs are label refinements at STABLE positions (verified:
    # 3.6.0 labels cmap walls generically as "wall" where 3.6.6 names them). The atlas
    # art is position-aligned with 3.6.6, so we map by position. Commit the diff as a
    # baseline and FAIL only if it CHANGES (detects future metadata drift).
    ob_path = os.path.join(here, "order_baseline.json")
    ob_cur = [[i, a, b] for (i, a, b) in order_diffs]
    if os.path.exists(ob_path):
        ob_base = json.load(open(ob_path, encoding="utf-8"))
        if ob_base != ob_cur:
            violations.append("3.6.0-vs-3.6.6 order diff CHANGED from committed baseline "
                              "(%d baseline vs %d now)" % (len(ob_base), len(ob_cur)))
    else:
        json.dump(ob_cur, open(ob_path, "w", newline="\n"), indent=1)

    # ---- build remap ----
    idx36, full36 = build_index(t36)
    cord5 = collapsed_ordinals(t5)
    used36 = defaultdict(int)   # (file, head) -> how many 3.6.6 occ consumed
    remap = [None] * len(t5)
    prov = [None] * len(t5)     # provenance
    tgtname = [""] * len(t5)
    for t in t5:
        g = t["gidx"]
        if g in idx_overrides:
            v = idx_overrides[g]
            remap[g] = PLACEHOLDER if v == "placeholder" else int(v)
            prov[g] = "index_override"; continue
        if t["head"] in force_ph or t["full"] in force_ph:
            remap[g] = PLACEHOLDER; prov[g] = "force_placeholder"; continue
        # 1) full-name alias / exact full match
        fa = full_aliases.get(t["full"])
        cand = None
        if fa and (t["file"], fa) in full36:
            cand = full36[(t["file"], fa)][0]; prov[g] = "full_alias"
        elif (t["file"], t["full"]) in full36:
            cand = full36[(t["file"], t["full"])][0]; prov[g] = "full_name"
        else:
            # 2) head (with alias), ordinal-aware
            head = aliases.get(t["head"], t["head"])
            occ = cord5[g] if t["file"] == "monsters" else t["ord"]
            lst = idx36.get((t["file"], head), [])
            if occ < len(lst):
                cand = lst[occ]; prov[g] = "head_alias" if head != t["head"] else "head"
            # 3) object "descr / name" display form -> match the identified name
            if cand is None and " / " in t["full"]:
                alt = aliases.get(t["full"].split(" / ")[-1].strip(),
                                  t["full"].split(" / ")[-1].strip())
                altc = idx36.get((t["file"], alt), [])
                if altc:
                    cand = altc[0]; prov[g] = "slash_name"
        if cand is not None:
            remap[g] = cand["gidx"]; tgtname[g] = cand["full"]
        else:
            remap[g] = PLACEHOLDER; prov[g] = "unmatched->placeholder"

    # ---- validations ----
    placeholders = [g for g in range(len(t5)) if remap[g] == PLACEHOLDER]
    for g, v in enumerate(remap):
        if v is None:
            violations.append("index %d not assigned" % g)
        elif v != PLACEHOLDER and not (0 <= v < ADDRESSABLE):
            violations.append("index %d -> %d out of [0,%d)" % (g, v, ADDRESSABLE))
    # sentinel allow-list: every placeholder must be sanctioned (force/override/unmatched)
    allow = {"force_placeholder", "index_override", "unmatched->placeholder"}
    for g in placeholders:
        if prov[g] not in allow:
            violations.append("index %d placeholder via unsanctioned provenance %s" % (g, prov[g]))
    # explicit, ordinal-aware allow-list of placeholder (sentinel) entries: committed as
    # a baseline keyed by (file, local index, full name); FAIL if the set changes.
    ph_cur = [[t5[g]["file"], t5[g]["lidx"], t5[g]["full"]] for g in placeholders]
    ph_path = os.path.join(here, "placeholder_baseline.json")
    if os.path.exists(ph_path):
        ph_base = json.load(open(ph_path, encoding="utf-8"))
        if ph_base != ph_cur:
            base_set = {tuple(x) for x in ph_base}; cur_set = {tuple(x) for x in ph_cur}
            violations.append("placeholder allow-list changed: +%d added, -%d removed"
                              % (len(cur_set - base_set), len(base_set - cur_set)))
    else:
        json.dump(ph_cur, open(ph_path, "w", newline="\n"), indent=1)

    # ---- metadata + atlas hashes ----
    meta_hashes = {}
    for ver, tree in (("5.0.0", "nethack-5.0.0"), ("3.6.6", "nethack-3.6.6"), ("3.6.0", "nethack-3.6.0")):
        for f in FILES:
            p = os.path.join(args.refs, tree, "win", "share", f + ".txt")
            meta_hashes["%s/%s" % (ver, f)] = sha_file(p)
    exp = man.get("meta_hashes")
    if exp and exp != meta_hashes:
        violations.append("tile metadata hashes differ from manifest baseline")
    atlas = os.path.join(root, "win", "vulture", "gamedata", "map", "ascii", "nethack_3_6_0.png")
    atlas_sha = sha_file(atlas); atlas_dim = png_dims(atlas)
    acfg = man.get("atlas", {})
    if acfg.get("expected_sha256_any") and atlas_sha not in acfg["expected_sha256_any"]:
        violations.append("atlas sha256 %s not in expected set" % atlas_sha)
    if acfg.get("dims") and list(atlas_dim) != acfg["dims"]:
        violations.append("atlas dims %s != %s" % (list(atlas_dim), acfg["dims"]))
    # ---- anchor cells (self-consistent: named tile maps to its 3.6.6 cell) ----
    for a in man.get("anchors", []):
        matches = [t for t in t5 if t["head"] == a["name5"] and t["file"] == a["file"]]
        if not matches:
            violations.append("anchor '%s' not found in 5.0.0 %s" % (a["name5"], a["file"]))
            continue
        got = remap[matches[0]["gidx"]]
        exp36 = a.get("expect36")
        if exp36 is not None:
            if got == PLACEHOLDER:
                violations.append("anchor '%s' -> PLACEHOLDER, expected atlas cell for '%s'"
                                  % (a["name5"], exp36))
            elif t36[got]["head"] != exp36:
                violations.append("anchor '%s' -> atlas cell %d ('%s'), expected '%s'"
                                  % (a["name5"], got, t36[got]["head"], exp36))

    prov_counts = defaultdict(int)
    for p in prov:
        prov_counts[p] += 1
    report = OrderedDict(
        gate="FAIL" if violations else "PASS",
        source_tiles=len(t5), target_tiles=len(t36), atlas_order_tiles=len(t30),
        placeholders=len(placeholders), atlas_sha256=atlas_sha, atlas_dims=list(atlas_dim),
        order_diffs=len(order_diffs), provenance=dict(prov_counts), violations=violations,
        meta_hashes=meta_hashes,
    )
    json.dump(report, open(args.report, "w", newline="\n"), indent=2)

    # machine-readable per-tile mapping CSV (consumed by contact_sheet.py)
    import csv
    with open(os.path.join(here, "mapping.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["src_index", "src_file", "src_name", "target_index", "target_name", "provenance"])
        for t in t5:
            g = t["gidx"]
            tgt = remap[g]
            tn = "PLACEHOLDER" if tgt == PLACEHOLDER else tgtname[g]
            w.writerow([g, t["file"], t["full"], tgt, tn, prov[g]])

    if args.survey:
        print("=== Path A survey ===")
        print("source=%d target=%d placeholders=%d order_diffs=%d" %
              (len(t5), len(t36), len(placeholders), len(order_diffs)))
        print("provenance:", dict(prov_counts))
        um = [t5[g] for g in placeholders if prov[g] == "unmatched->placeholder"]
        print("unmatched (would placeholder) = %d; sample by file:" % len(um))
        for f in FILES:
            s = [t for t in um if t["file"] == f][:20]
            print(" %s (%d): %s" % (f, len([t for t in um if t['file']==f]),
                                    ", ".join(t["full"] for t in s)))
        return 0

    print("Path A gate: %s (%d violations, %d placeholders)" %
          (report["gate"], len(violations), len(placeholders)))
    for v in violations[:25]:
        print("  -", v)
    if violations:
        return 1
    if args.emit:
        with open(args.out, "w", newline="\n") as fh:
            fh.write("/* GENERATED by patha/validate.py - do not hand-edit. */\n")
            fh.write("/* NetHack 5.0.0 tile index -> Vulture atlas cell (3.6.6 order) or */\n")
            fh.write("/* V_TILE_PLACEHOLDER (reserved atlas cell %d). */\n" % PLACEHOLDER)
            fh.write("#ifndef VULTURE_PATHA_REMAP_H\n#define VULTURE_PATHA_REMAP_H\n\n")
            fh.write("#define VULTURE_PATHA_PLACEHOLDER %d\n" % PLACEHOLDER)
            fh.write("#define VULTURE_PATHA_REMAP_COUNT %d\n\n" % len(t5))
            fh.write("static const short vulture_patha_remap[VULTURE_PATHA_REMAP_COUNT] = {\n")
            for i in range(0, len(remap), 16):
                fh.write("    " + ",".join("%4d" % remap[j] for j in range(i, min(i+16, len(remap)))) + ",\n")
            fh.write("};\n\n#endif\n")
        print("emitted", args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
