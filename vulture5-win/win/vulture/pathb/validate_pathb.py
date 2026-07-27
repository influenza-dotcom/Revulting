#!/usr/bin/env python3
"""
Path B (isometric renderer) symbolic-name compatibility audit.

Compares the symbolic names bound in win/vulture/gamedata/config/vulture_tiles.conf
against the names NetHack 5.0.0 actually derives for its objects[]/mons[] (via the
Vulture derivation reproduced in dump_names.c -> *_names.tsv).

Emits a human report (pathb_compat_report.md) and machine JSON (pathb_compat.json),
and returns a non-zero exit code when the Path B gate is violated.

This is the DIAGNOSTIC/GATE tool. Before the ERROR-placeholder system is installed it
is EXPECTED to fail (it quantifies the drift); after, it must pass.
"""
import argparse, json, os, re, sys
from collections import OrderedDict

CATEGORIES = ["objicon", "object", "monster", "statue", "figurine",
              "misc", "floor", "wall", "explosion", "cursor", "edge"]

# which NetHack-derived name set backs each category
DATA_SOURCE = {
    "object": "object", "objicon": "object",
    "monster": "monster", "statue": "monster", "figurine": "monster",
}
AUTO_EXPAND = {"wall", "floor", "edge"}          # names defined BY the config
INTERNAL    = {"misc", "cursor", "explosion"}    # Vulture-internal enum names

DIRECT_RE = re.compile(r'^\s*(\w+)\.(\S+)\s*=\s*"([^"]+)"\s*(-?\d+)?\s*(-?\d+)?')
ALIAS_RE  = re.compile(r'^\s*(\w+)\.(\S+)\s*=>\s*(\w+)\.(\S+)')


def parse_config(path):
    cats = {c: {"direct": OrderedDict(), "alias": OrderedDict(), "default": None}
            for c in CATEGORIES}
    with open(path, encoding="latin-1") as fh:
        for raw in fh:
            line = raw.rstrip("\n").rstrip("\\").strip()
            if not line or line.startswith("#"):
                continue
            m = ALIAS_RE.match(line)              # '=>' before '=' (it contains '=')
            if m:
                cat, name, tcat, tname = m.groups()
                if cat not in cats:
                    continue
                if name == "default":
                    cats[cat]["default"] = (tcat, tname)
                else:
                    cats[cat]["alias"][name] = (tcat, tname)
                continue
            m = DIRECT_RE.match(line)
            if m:
                cat, name, fn = m.group(1), m.group(2), m.group(3)
                if cat not in cats:
                    continue
                if name == "default":
                    cats[cat]["default"] = (cat, name)  # unusual, record anyway
                else:
                    cats[cat]["direct"][name] = fn
    return cats


def load_names(tsv):
    obj, mon = [], []
    with open(tsv, encoding="latin-1") as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) != 3:
                continue
            kind, idx, name = p[0], int(p[1]), p[2]
            (obj if kind == "object" else mon).append(name)
    return obj, mon


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))  # vulture-baseline/
    ap.add_argument("--conf", default=os.path.join(
        root, "win", "vulture", "gamedata", "config", "vulture_tiles.conf"))
    ap.add_argument("--names500", default=os.path.join(here, "nh500_names.tsv"))
    ap.add_argument("--names366", default=os.path.join(here, "nh366_names.tsv"))
    ap.add_argument("--tiles", default=os.path.join(
        root, "win", "vulture", "gamedata", "tiles"))
    ap.add_argument("--report", default=os.path.join(here, "pathb_compat_report.md"))
    ap.add_argument("--json", default=os.path.join(here, "pathb_compat.json"))
    args = ap.parse_args()

    cats = parse_config(args.conf)
    obj500, mon500 = load_names(args.names500)
    obj366, mon366 = load_names(args.names366)
    set500 = {"object": set(obj500), "monster": set(mon500)}
    set366 = {"object": set(obj366), "monster": set(mon366)}

    result = OrderedDict()
    violations = []

    for cat in CATEGORIES:
        info = cats[cat]
        declared = list(info["direct"].keys()) + list(info["alias"].keys())
        declared_set = set(declared)
        entry = OrderedDict(
            declared=len(declared), directs=len(info["direct"]),
            aliases=len(info["alias"]),
            has_default=info["default"] is not None,
            default=("%s.%s" % info["default"]) if info["default"] else None,
        )

        if cat in DATA_SOURCE:
            src = DATA_SOURCE[cat]
            valid = set500[src]
            valid366 = set366[src]
            resolved_direct = [n for n in info["direct"] if n in valid]
            resolved_alias = [n for n in info["alias"] if n in valid]
            no_longer = sorted(n for n in declared_set if n not in valid)
            missing_in_config = sorted(valid - declared_set)
            new_in_500 = sorted(valid - valid366)   # genuinely new symbols
            removed_from_500 = sorted(valid366 - valid)
            entry.update(
                valid_symbols_500=len(valid),
                resolved_direct=len(resolved_direct),
                resolved_via_alias=len(resolved_alias),
                config_names_no_longer_resolve=len(no_longer),
                symbols_500_with_no_config_entry=len(missing_in_config),
                new_species_or_objects_in_500=len(new_in_500),
                removed_since_366=len(removed_from_500),
            )
            entry["_no_longer_resolve_sample"] = no_longer[:15]
            entry["_missing_in_config_sample"] = missing_in_config[:15]
            entry["_new_in_500_sample"] = new_in_500[:15]
            # INFO (not a violation once an ERROR default exists): these render the
            # conspicuous placeholder, which is the REQUIRED behavior. no_longer are
            # unused config entries (also harmless) and are reported for typo review.
            entry["symbols_rendered_as_ERROR"] = len(missing_in_config)

        else:
            entry["kind"] = "auto-expand" if cat in AUTO_EXPAND else "internal"

        # every category must have an explicit default pointing to its ERROR asset
        has_error_default = (info["default"] is not None
                             and "ERROR" in info["default"][1])
        if info["default"] is None:
            violations.append("%s: no explicit <category>.default" % cat)
        elif not has_error_default:
            violations.append("%s: default -> %s.%s is ordinary art, not an ERROR placeholder"
                              % (cat, info["default"][0], info["default"][1]))
        # a 5.0.0 symbol with no direct/alias entry must still resolve to the ERROR
        # asset; only if that safety net is missing is it a gate violation.
        if cat in DATA_SOURCE and not has_error_default \
           and entry.get("symbols_500_with_no_config_entry", 0):
            violations.append("%s: %d 5.0.0 symbols resolve to neither intended art "
                              "nor an ERROR asset" % (cat, entry["symbols_500_with_no_config_entry"]))

        # the ERROR placeholder itself must be a direct binding to an existing PNG
        err_file = info["direct"].get("ERROR")
        if err_file is None:
            violations.append("%s: no ERROR asset binding (<cat>.ERROR = \"...\")" % cat)
        elif not os.path.exists(os.path.join(args.tiles, os.path.basename(err_file))):
            violations.append("%s: ERROR asset PNG missing (%s)" % (cat, err_file))
        else:
            entry["error_asset"] = os.path.basename(err_file)

        # alias target existence + simple cycle check (within declared names)
        for src, (tcat, tname) in info["alias"].items():
            if tcat == cat and tname not in info["direct"] and tname not in info["alias"] \
               and (cat not in DATA_SOURCE or tname not in set500.get(DATA_SOURCE[cat], set())):
                violations.append("%s.%s: alias target %s.%s not found"
                                  % (cat, src, tcat, tname))

        # referenced PNG existence (direct bindings)
        missing_png = []
        for name, fn in info["direct"].items():
            p = os.path.join(args.tiles, os.path.basename(fn))
            if not os.path.exists(p):
                missing_png.append(fn)
        if missing_png:
            entry["missing_png"] = len(missing_png)
            violations.append("%s: %d referenced PNGs absent" % (cat, len(missing_png)))

        result[cat] = entry

    # WALL/FLOOR/EDGE auto-expand: assert the parsed name-set matches a committed
    # baseline EXACTLY (drift here means tiles silently added/renamed). First run
    # writes the baseline; later runs assert equality. Count == runtime typecount.
    for cat in ("wall", "floor", "edge"):
        names = sorted(set(cats[cat]["direct"]) | set(cats[cat]["alias"]))
        result[cat]["parsed_names"] = len(names)
        bl = os.path.join(here, "%s_names.baseline" % cat)
        if os.path.exists(bl):
            with open(bl, encoding="utf-8") as fh:
                base = [l.strip() for l in fh if l.strip()]
            if base != names:
                added = sorted(set(names) - set(base))
                removed = sorted(set(base) - set(names))
                violations.append("%s: auto-expand name-set diverges from baseline "
                                  "(+%d/-%d) added=%s removed=%s"
                                  % (cat, len(added), len(removed), added[:8], removed[:8]))
        else:
            with open(bl, "w", newline="\n") as fh:
                fh.write("\n".join(names) + "\n")
            result[cat]["baseline_written"] = len(names)

    summary = OrderedDict(
        gate="FAIL" if violations else "PASS",
        violation_count=len(violations),
        categories=result,
        violations=violations,
    )
    with open(args.json, "w", newline="\n") as fh:
        json.dump(summary, fh, indent=2)

    # human report
    L = []
    L.append("# Path B compatibility report (vulture_tiles.conf vs NetHack 5.0.0)\n")
    L.append("Gate: **%s** (%d violations)\n" % (summary["gate"], len(violations)))
    L.append("5.0.0 derived symbols: %d objects, %d monsters | "
             "3.6.6: %d objects, %d monsters\n"
             % (len(obj500), len(mon500), len(obj366), len(mon366)))
    L.append("\n## Per-category\n")
    L.append("| category | declared | valid(5.0.0) | resolved(direct/alias) | "
             "no-longer-resolve | 5.0.0 w/o config entry | new in 5.0.0 | default |")
    L.append("|---|--:|--:|--:|--:|--:|--:|---|")
    for cat, e in result.items():
        if cat in DATA_SOURCE:
            L.append("| %s | %d | %d | %d/%d | %d | %d | %d | %s |" % (
                cat, e["declared"], e["valid_symbols_500"],
                e["resolved_direct"], e["resolved_via_alias"],
                e["config_names_no_longer_resolve"],
                e["symbols_500_with_no_config_entry"],
                e["new_species_or_objects_in_500"],
                e["default"] or "**NONE**"))
        else:
            L.append("| %s | %d | (%s) | - | - | - | - | %s |" % (
                cat, e["declared"], e.get("kind", ""), e["default"] or "**NONE**"))
    L.append("\n## Violations (%d)\n" % len(violations))
    for v in violations:
        L.append("- " + v)
    L.append("\n## Samples (data-derived categories)\n")
    for cat in DATA_SOURCE:
        e = result[cat]
        L.append("\n### %s" % cat)
        L.append("- config names no longer resolving (sample): %s"
                 % ", ".join(e["_no_longer_resolve_sample"]) or "(none)")
        L.append("- 5.0.0 symbols with no config entry (sample): %s"
                 % ", ".join(e["_missing_in_config_sample"]) or "(none)")
        L.append("- genuinely new in 5.0.0 (sample): %s"
                 % ", ".join(e["_new_in_500_sample"]) or "(none)")
    with open(args.report, "w", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")

    print("Path B gate: %s (%d violations)" % (summary["gate"], len(violations)))
    print("report: %s" % args.report)
    print("json:   %s" % args.json)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
