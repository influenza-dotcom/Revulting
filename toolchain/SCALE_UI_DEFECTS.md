# render_scale UI compensation — outstanding defects (2026-07-26)

Evidence: user screenshot of the inventory at render_scale=2, plus my repro
(shots/36..38). The zoom itself now WORKS — map draws, status bar and toolbar
are correctly compensated, title line reads "View: zoomed out (rendering
3840x2160)".

## DEFECT 1 — inventory window: half-compensated layout (CONFIRMED, user shot)

Symptom at scale 2: window frame, section headings ("Weapons", "Armor",
"Comestibles") and row cells are drawn at NATIVE size, while the item TEXT and
item ICONS inside them are compensated to 2x. Result:
  * row `a` text overruns and overlaps row `b`
  * "Armor" / "Comestibles" headings are covered by the rows above them
  * rows overflow the right edge of the frame
  * the Cancel button overlaps the last row
  * the window itself is sized too small for its contents

Root cause: the cell-geometry constants are not multiplied by the UI scale
while their contents are. `V_LISTITEM_WIDTH` / `V_LISTITEM_HEIGHT` are used in
EIGHT places in winclass/inventory.cpp and must all scale together:
    142  (itemcol - newpos) * (V_LISTITEM_WIDTH + 4) + leftoffset
    144  (itemcount % ow_vrows) * V_LISTITEM_HEIGHT + topoffset
    687  (parent->h - topoffset - bottomoffset) / V_LISTITEM_HEIGHT   <- rows that fit
    690  (parent->w - 2*leftoffset - 10) / (V_LISTITEM_WIDTH + 4)     <- cols that fit
    715  (itemcount / ow_vrows) * (V_LISTITEM_WIDTH + 4) + leftoffset
    717  (itemcount % ow_vrows) * V_LISTITEM_HEIGHT + topoffset
    753  ow_vcols * (V_LISTITEM_WIDTH + 4) - 4 + leftoffset + rightoffset  <- window w
    (+ the matching h computation immediately after 753)
Also: leftoffset/rightoffset/topoffset/bottomoffset, the literal +4 / -4 / -10
gutters, the scrollbar and page buttons, and winclass/objitemwin.cpp's own
draw geometry (it draws the icon box, item letter, quantity and weight inside
a cell whose size comes from those same constants).

Fix rule: anything expressed in UI PIXELS must be multiplied by the UI scale
exactly once, at one consistent layer. Prefer scaling the CONSTANTS at their
point of use via a helper (e.g. UISCALE(v)) over sprinkling multiplications,
so a missed site is visible as an unscaled literal.

## DEFECT 2 — inventory did not open at all in my automated repro

With the 08:52 build, pressing `i` and clicking the toolbar Inventory button
both did nothing at scale 2 (shots/37, /38). The user's screenshot proves it
DOES open for them, so this is likely a HIT-TEST/focus issue in my synthetic
input path rather than a second bug — but confirm: at scale 2, does the
toolbar button's clickable rect still match where the button is DRAWN?
Mouse coords are scaled window->render; if the toolbar is drawn UI-compensated
but its hotspot rect is computed uncompensated (or vice versa), clicks land
off-target. Check winclass/toolbar.cpp and hotspot.cpp.

## VERIFICATION GATE for any fix

At render_scale=2, with a starting character:
  1. `i` opens the inventory; every row is fully inside the frame, no row
     overlaps the next, headings are visible, Cancel does not overlap a row.
  2. Item icons are legible and sit inside their cells.
  3. Clicking an item row selects THAT row (hit-test matches drawn position).
  4. The toolbar Inventory button opens the inventory when clicked.
  5. Multidrop (D) and pickup (,) menus — same objitemwin path — are also sane.
  6. At render_scale=1 all of the above is pixel-identical to the 1x baseline.
