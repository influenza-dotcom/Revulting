#!/usr/bin/env python3
"""
Path A contact sheets + machine-readable mapping output.

Reads mapping.csv (from validate.py) and the (modified) atlas, and renders one
labeled contact sheet per group (monsters, objects, other): each 5.0.0 tile shown
as the atlas cell it maps to, upscaled; PLACEHOLDER entries rendered in MAGENTA.
Also emits patha_mapping.html (browsable table with 5.0.0 index/name, target index,
3.6.x name, provenance). mapping.csv itself is the CSV deliverable.
"""
import csv, os
from PIL import Image, ImageDraw

COLS_ATLAS, CW, CH = 40, 7, 14
SCALE = 4                      # upscale factor for the tiny 7x14 cells
SHEET_COLS = 24
MAGENTA = (255, 0, 255, 255)


def load_rows(csv_path):
    with open(csv_path, newline="") as fh:
        return list(csv.DictReader(fh))


def atlas_cell(atlas, idx):
    x0, y0 = (idx % COLS_ATLAS) * CW, (idx // COLS_ATLAS) * CH
    return atlas.crop((x0, y0, x0 + CW, y0 + CH))


def make_sheet(rows, atlas, out_png, placeholder_cell):
    cw, ch = CW * SCALE, CH * SCALE
    pad = 2
    n = len(rows)
    cols = SHEET_COLS
    rowsn = (n + cols - 1) // cols
    W = cols * (cw + pad) + pad
    H = rowsn * (ch + pad) + pad
    sheet = Image.new("RGBA", (W, H), (30, 30, 30, 255))
    for i, r in enumerate(rows):
        cx = pad + (i % cols) * (cw + pad)
        cy = pad + (i // cols) * (ch + pad)
        tgt = int(r["target_index"])
        if r["provenance"].endswith("placeholder") or tgt == placeholder_cell:
            cell = Image.new("RGBA", (CW, CH), MAGENTA)
            d = ImageDraw.Draw(cell)
            d.line((0, 0, CW - 1, CH - 1), fill=(0, 0, 0, 255))
            d.line((0, CH - 1, CW - 1, 0), fill=(0, 0, 0, 255))
        else:
            cell = atlas_cell(atlas, tgt)
        sheet.paste(cell.resize((cw, ch), Image.NEAREST), (cx, cy))
    sheet.save(out_png)
    return n, W, H


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    atlas_path = os.path.join(root, "win", "vulture", "gamedata", "map", "ascii", "nethack_3_6_0.png")
    atlas = Image.open(atlas_path).convert("RGBA")
    rows = load_rows(os.path.join(here, "mapping.csv"))
    placeholder_cell = 1199

    for grp in ("monsters", "objects", "other"):
        grp_rows = [r for r in rows if r["src_file"] == grp]
        n, W, H = make_sheet(grp_rows, atlas, os.path.join(here, "contact_%s.png" % grp), placeholder_cell)
        ph = sum(1 for r in grp_rows if r["provenance"].endswith("placeholder"))
        print("contact_%s.png: %d tiles (%d placeholder), %dx%d" % (grp, n, ph, W, H))

    # HTML mapping table
    total_ph = sum(1 for r in rows if r["provenance"].endswith("placeholder"))
    html = ["<!doctype html><meta charset=utf-8><title>Path A 5.0.0->Vulture mapping</title>",
            "<style>body{font:13px monospace;background:#111;color:#ddd}"
            "table{border-collapse:collapse}td,th{border:1px solid #333;padding:2px 6px}"
            ".ph{background:#f0f;color:#000}</style>",
            "<h2>Path A remap: NetHack 5.0.0 (%d tiles) &rarr; Vulture atlas (3.6.6 order)</h2>" % len(rows),
            "<p>%d placeholders (magenta). Sheets: contact_monsters.png, contact_objects.png, contact_other.png</p>" % total_ph,
            "<table><tr><th>5.0.0 idx</th><th>file</th><th>5.0.0 name</th>"
            "<th>target cell</th><th>3.6.x name</th><th>provenance</th></tr>"]
    for r in rows:
        ph = "ph" if r["provenance"].endswith("placeholder") else ""
        html.append("<tr class=%s><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
                    % (ph, r["src_index"], r["src_file"], r["src_name"],
                       r["target_index"], r["target_name"], r["provenance"]))
    html.append("</table>")
    with open(os.path.join(here, "patha_mapping.html"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(html))
    print("wrote patha_mapping.html and mapping.csv (%d rows)" % len(rows))


if __name__ == "__main__":
    main()
