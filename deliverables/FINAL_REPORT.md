# NetHack 5.0.0 → Vulture port — final report

Status date: 2026-07-25. Authoritative running detail: `../IMPLEMENTATION_LOG.md`.

---

## 1. Bottom line

NetHack **5.0.0** builds, links, launches and **renders through Vulture's isometric SDL2
interface**. Character creation and a dungeon level render correctly; content that 5.0.0
has but Vulture has no art for renders as an **unmistakable magenta placeholder**, never as
tile zero, nothing, or plausible-but-wrong art.

Evidence: `shots/*.png`, captured by the game's own F12 screenshot function.

## 2. Pinned revisions

| Component | Ref | SHA | Verified |
|---|---|---|---|
| NetHack 5.0.0 | tag `NetHack-5.0.0_Released` | `16ff59115315917b93185d026aeefea06db9b0f4` | ✅ |
| NetHack 3.6.7 | tag `NetHack-3.6.7_Released` | `ed600d9f0f3c37677418f0150f59363ca641f3dc` | ✅ |
| NetHack 3.6.6 | tag `NetHack-3.6.6_Released` | `34c56eece35f42aa99bb5ab1a829bb31b2db3b1a` | ✅ |
| Vulture-NetHack-3.6.6 | default branch HEAD | `c7d1cd3119ebf1319a3e3d142ff93643d1325a38` | ✅ |

## 3. Environment manifest

WSL2 Ubuntu 24.04.2 · gcc 13.3.0 · GNU make · bison 3.8.2 · flex · pkg-config ·
SDL2 2.30.0 (+image/mixer/ttf) · libpng16 · libogg/vorbis/theora · zlib · uuid-dev ·
Lua 5.4.8 (via `make fetch-lua`, checksum verified) · Python 3.12 (tooling).
Display: WSLg (`DISPLAY=:0`, `SDL_VIDEODRIVER=x11`).

Build (port tree `~/port/vulture5`):
```bash
cd sys/unix && sh setup.sh hints/linux.500 && cd ../..
make WANT_WIN_VULTURE=1 WANT_DEFAULT=vulture -j1 all
make WANT_WIN_VULTURE=1 WANT_DEFAULT=vulture install
cp -r win/vulture/gamedata/* /root/nh/install/games/lib/nethackdir/
```
Build **serially** (`-j1`): parallel make races on `liblua.a` and `util/makedefs`.

## 4. Gates

| Gate | Result |
|---|---|
| Phase 0 baselines / pins / manifests | ✅ |
| Path A atlas validation (SHA + dims + transparency) | ✅ exact match, 280×518, alpha |
| Patch inventory vs vanilla 3.6.6 | ✅ matches task's hook list exactly (src 11, include 4) |
| `gamedata/tiles` manifest | ✅ 2388 PNGs, 11 categories |
| Gate A — stock 5.0.0 tty launch/level/descend | ✅ runtime-verified |
| Gate B — tile generator (`TILES_IN_GLYPHMAP`, tile.c, links) | ✅ |
| Phase 0.1 — baseline Vulture 3.6.6 builds+runs | ✅ |
| **Phase 1 — Path B compatibility gate** | ✅ PASS (0 violations) |
| **Phase 2 — Path A remap** | ✅ PASS (0 violations, 151 placeholders) |
| Phase 3/4/5 — core import, window API, globals | ✅ builds, links, runs |
| Phase 6 — runtime acceptance | ◐ partial (see §6) |

## 5. Deliverables index

| Path | What |
|---|---|
| `../IMPLEMENTATION_LOG.md` | full log: gates, failures, decisions, hashes, risks |
| `patch_inventory_{src,include}.txt` | Vulture-vs-3.6.6 hook inventory |
| `gamedata_tiles_manifest.sha256.txt` | sorted SHA-256 of all 2388 tile PNGs |
| `nh500_names.tsv`, `nh366_names.tsv` | authoritative symbolic names per version |
| `pathb_compat_report.md`, `pathb_compat.json` | Path B compatibility report (fails non-zero) |
| `pathb_loader_test.out` | Path B loader + intentional-break test transcript |
| `{wall,floor,edge}_names.baseline` | auto-expand name-set baselines |
| `phase3/` | hook-reapply scripts + `vulture_nh5compat.h` |
| `shots/` | runtime screenshots (proof of rendering) |
| in-tree `win/vulture/pathb/` | `validate_pathb.py`, `make_error_tiles.py`, `dump_names.c`, `test_pathb_loader.cpp` |
| in-tree `win/vulture/patha/` | `remap.manifest.json`, `validate.py`, `remap_table.h`, `contact_sheet.py`, contact sheets, `mapping.csv`, baselines |

## 6. Phase 6 acceptance status

| # | Item | Status |
|---|---|---|
| 1 | Character selection renders | ✅ proven (screenshot) |
| 2 | A dungeon level renders | ✅ proven (screenshot) |
| 3 | Movement works | ◐ keys reach the game (character creation, wizmap and F12 all respond); no before/after positional proof captured yet |
| 4 | Mapped art correct, unmapped visibly marked | ✅ proven — screenshots **and** the tile dump (objects resolve to their real PNGs; unmapped → placeholder) |
| 5 | Descending works | ◐ proven for the stock 5.0.0 tty build (Gate A); not yet re-proven through Vulture |
| 6 | A shop renders | ◐ strong evidence: `SHOPTYPE='!'` generated a potion shop and the dump lists its stock as a contiguous room block (x=10‑14, y=15‑16) of unidentified potions on a rendered level; shopkeeper interaction not yet visually confirmed |
| 7 | Monster interaction works | ☐ not attempted |
| 8 | Save/restore keeps art + tile index | ❌ **FAILS — confirmed port defect: GUI save does not work** (see §6a) |

### 6a. Confirmed defect — GUI save is broken

This was investigated properly rather than dismissed as test flakiness, because save/restore
is a non-negotiable criterion. It is a **real defect in the Vulture windowport**:

1. **The core is fine** — the stock 5.0.0 **tty** build, driven over a pty with the same keys,
   prompts `Really save? [yn] (n)` and on `y` writes `save/0wizard.gz`.
2. **`S` reaches NetHack under Vulture** — a screenshot taken right after `S` shows Vulture
   rendering the **"Really save?" dialog with `yes`/`no` buttons**.
3. **`y` does not complete the save** — with focus verified by a canary and only `S` then `y`
   sent, there is **no save file and the game keeps running** (a successful save exits).
4. **The command-queue path saves fine** — `cmdq_add_key(CQ_CANNED,'S'/'y')` produces a clean
   save, which localises the fault to the dialog's key handling rather than the save code.

Scope is narrowed but the root cause is not yet pinned: the accelerator mapping (`yes`→`y`),
the label mapping and `vulture_keysym_to_char()` all look correct in isolation, so the next
step is to trace `vulture_event_dispatcher(…, V_RESPOND_CHARACTER, win)` and see whether the
keydown reaches the dialog at all.

**Impact: a human player cannot save from the GUI.** This is the single most important open
defect in the port.

### Save/restore — what is and isn't established

The hard part — making "resolved tile index" observable — is **done and working**. The dump
records, for every inventory and map object, the raw id, the resolved id and the PNG actually
used, via the *same* resolver the renderer calls. A run captured 32 objects including a whole
shop of unidentified potions with the shuffle plainly visible:

```
map x=11 y=15 otyp=299 descr_idx=320 resolved=320 png='tiles/object.pot_acid.png'
map x=12 y=15 otyp=313 descr_idx=299 resolved=299 png='tiles/object.pot_confusion.png'
map x=13 y=16 otyp=305 descr_idx=300 resolved=300 png='tiles/object.pot_blindness.png'
```

What is missing is only the trigger: neither `S`+`y` nor `#save` produced a file in `save/`,
so run 2 had nothing to restore and the diff is meaningless. The comparison harness
(`scratchpad/save_restore.sh`) is written and runs end to end; it needs a working save
keystroke (or a `#wizloadlua` + `nh.pushkey` driver, which the research established as the
deterministic route) to finish. **This item is NOT proven and is not claimed as passing.**

## 7. Known placeholders (intentional, by design)

Rendered as the conspicuous magenta/hazard placeholder rather than wrong art:

- **Path B (isometric)** — 5.0.0 symbols with no Vulture art:
  object/objicon 33, monster 17, statue 27, figurine 31.
  Drivers include renames (`PM_ALIGNED_PRIEST`→`PM_CLERIC`, dwarf/elf/gnome royalty →
  `_LEADER`/`_RULER`/`_NOBLE`/`_MONARCH`, `ACID_VENOM`→`SPLASH_OF_ACID_VENOM`) and genuinely
  new content (`PM_GOLD_DRAGON`, `PM_DISPLACER_BEAST`, the `GENERIC_*` objects).
- **Path A (minimap)** — 151 tile indices map to the reserved placeholder cell 1199.
- **Dungeon features** — 5.0.0 cmap symbols with no Vulture art now render the misc
  placeholder instead of *uninitialised garbage* (see §8): `S_lavawall`, `S_poisoncloud`,
  `S_vibrating_square`, `S_trapped_chest`, `S_trapped_door`, branch stair/ladder variants,
  engraving markers.

Authoring art for these is content work, deliberately out of scope; the port makes each
one visible and enumerable rather than silently wrong.

## 8. Notable defects found and fixed

1. **Fuzzy `patch(1)` silently corrupted core files** — hunks spliced into unrelated
   functions in `version.c`, `files.c`, `end.c`, `do_name.c` while reporting success.
   Reverted to pristine 5.0.0 and reapplied every hook by exact match. (Log §18.)
2. **Uninitialised dungeon-feature tile map** — `vulture_tilemap_misc[MAXPCHARS]` was never
   initialised, so unmapped cmap symbols indexed the tile array with garbage. Now swept to
   `V_MISC_ERROR`. (Log §21.)
3. **Every object rendered as a placeholder** — `init_objnames()` used 3.6.x's
   "stop at the first `ILLOBJ_CLASS`" terminator, but 5.0.0 puts "generic <class>" ILLOBJ
   objects at *low* indices, so the loop ended immediately and virtually no object got a name
   to match the config. Now iterates the full `NUM_OBJECTS`. Objects render correctly again,
   including the shuffled-appearance substitution. (Log §22.)
   *This is the defect the Path B placeholder system was built to expose — it showed up as
   conspicuous magenta tiles rather than silently wrong art.*
4. **Vulture windowport never rebuilt** — the `vulture.o` make rule didn't depend on
   `win/vulture/**`, so source edits silently produced a stale binary. Now always recurses.

## 8a. Acceptance-harness lessons (documented so they are not rediscovered)

1. NetHack traps **SIGTERM** to save, and that save blocks on a GUI prompt — plain `kill` +
   `wait` hangs forever. Always `kill -9`.
2. **SDL ignores XSendEvent**, so `xdotool key --window` silently does nothing. Activate the
   window and send **global XTEST** events.
3. A `kill -9`'d run leaves level files `<uid><plname>.<lvl>` (e.g. `0wizard.0`). The next
   launch then shows *"Old game in progress. Destroy/Recover/Cancel?"*, blocking everything —
   and `save/*` / `*lock*` globs do **not** match them.
4. Vulture's **F12 screenshot pops a modal** that swallows the next keypress; a capture must
   be F12 *followed by* a dismiss key.
5. `SHOPTYPE=<class>` forces a shop onto every random level in wizard mode with no depth gate;
   `SHOPTYPE='!'` gives a potion shop — shop test and unidentified shuffled items in one level.
6. NetHack 5.0.0 has a real scripting path — `#wizloadlua` + `nh.pushkey` (→ command queue)
   + `nh.doturn`. Menus do **not** read the queue, so `!tutorial` is mandatory. There is **no**
   RNG seed option in 5.0.0; determinism must be structural.

## 9. Intentionally deferred

- **Emscripten/WebAssembly support** — explicitly not preserved (task instruction).
- **`options.c` cosmetic defaults** (`dark_room`, `toptenwin`) — 5.0.0 restructured the
  option table; not behaviour-critical.
- **Art authoring** for the placeholders enumerated in §7.
- **`total_tiles_used`/`max*tile` counts** left in NetHack tile space (not atlas space);
  they are counts, not indices, and no consumer sizes the atlas from them.


---

# UPDATE 2026-07-25 (late) — Phase 7 complete, deliverable is a double-clickable .exe

## Deliverable

`C:\Users\dalla\Vulture\VultureNetHack\` — 69 MB, portable, self-contained.
Double-click **NetHackV.exe**. No WSL, no X server, no command line.
GUI subsystem (no console window); 28 bundled DLLs; every other import is a
stock Windows system DLL. The build anchors to its own directory, so the folder
can be moved anywhere. `README.txt` in the folder explains play and known gaps.

## Gate table — revised

| Criterion | Status | Evidence |
|---|---|---|
| Builds and launches | PASS | `NetHackV.exe`, GUI subsystem, launched from `C:\` |
| Renders character creation | PASS | `shots/08`, `shots/12` |
| Renders a dungeon | PASS | `shots/10`, `shots/19`, `shots/24` |
| Movement | PASS | `shots/11` — hero and pet advance, view scrolls |
| Monsters / pets | PASS | pony, kitten, hostiles rendered and animate |
| Inventory | PASS | `shots/25` — icons, quantities, blessed shading, worn colour |
| Save | PASS | `dalla.NetHack-saved-game`, 67,971 bytes |
| Restore | PASS | "Aloha dalla, the human Tourist, welcome back to NetHack!" + state intact |
| Unmapped tile renders as conspicuous placeholder | PASS | magenta / black-X; never tile 0, never invisible |
| Descend a level | NOT EXERCISED | — |
| Shops | NOT EXERCISED | — |

## §6a defect — RESOLVED

The GUI-save defect confirmed earlier was `other_self_recover_prompt()` in
`sys/windows/windmain.c`: it asks via `y_n()` (which works), then — for any port
that is neither mswin nor curses — drops into a raw console loop
`while ((ci = nhgetch()) && !(ci == '\n' || ci == 13))`. Vulture fell into that
branch and hung behind a stale, button-less dialog. Fixed by adding
`isvulture = WINDOWPORT(vulture)` to both guards. Save/restore now round-trips.

## Known placeholders — final, enumerated

Reproduce with `toolchain/cmap_gap.py`, which diffs the cmap symbols 5.0.0
declares in `include/defsym.h` against those bound in `winclass/mapdata.cpp`.

**Bound to real art this round:** `S_brupstair`, `S_brdnstair`, `S_brupladder`,
`S_brdnladder` (5.0.0 splits branch-crossing stairs into their own symbols;
dlvl 1's up stair leaves the dungeon, so this was the hero's *starting square*),
`S_engroom`, `S_engrcorr` (3.6.6 had no engraving symbols and drew plain floor —
restoring parity, not inventing art), and `S_poisoncloud` → cloud.

**Deliberately left as placeholders** — no 3.6.6-era art exists and a guess would
be worse than an honest placeholder:

- `S_lavawall` — a *wall* of lava. Drawing it as lava floor would imply it is
  enterable, which is exactly the "plausible-but-incorrect" failure the spec
  forbids.
- `S_vibrating_square`
- `S_trapped_door`, `S_trapped_chest`
- `S_goodpos` — debug "valid position" overlay

`S_sw_*` (8 swallow cells) and `S_expl_*` (9 explosion cells) are never looked up
in `vulture_tilemap_misc[]` — swallow and explosion glyphs route through their own
tile ranges — so their absence from that table is correct, not a gap.

## Defects fixed to get from "it links" to "it plays"

Each was found by running the binary, not by reading it.

**D1 — Unix `sysconf` shipped to Windows.** `GDBPATH=/usr/bin/gdb` and
`GREPPATH=/bin/grep` do not exist on Windows, so NetHack raised two config errors
and blocked on `--More--`. Under `-mwindows` there is no console to display it,
so the process ran forever with **no window at all**. Fixed by commenting out the
Unix paths, zeroing `PANICTRACE_*`, and setting `PORTABLE_DEVICE_PATHS=1`.

**D2 — `nhwindow` vector indexed out of bounds.** 5.0.0 passes `WIN_ERR` (-1) to
window procs where 3.6.6 did not. `vulture_get_nhwindow()` indexed unchecked, so
-1 became a huge `size_t` and tripped libstdc++'s bounds assertion before the
first frame was drawn. Also fixed the constructor's free-slot scan, which
dereferenced `vulture_nhwindows[id]` *before* testing `id < windowcount_max`.

**D3 — parentless dialog SIGSEGV.** 5.0.0's `main()` calls `pline()` for the
copyright banner *before* `WIN_MESSAGE` exists, so `vulture_putstr()` sees
`WIN_ERR` and raises a messagebox while `ROOTWIN` is still NULL;
`mainwin::layout()` then did `parent->w`. Now centres on `vulture_screen` when
parentless — the same guard `inputdialog` already had, extended to
`choicedialog`, `anykeydialog` and `dirdialog`.

**D4 — wrong DLB filename.** Windows defines `VERSION_IN_DLB_FILENAME`, so
NetHack opens `nhdat` + MAJOR MINOR PATCHLEVEL = **`nhdat500`**. Packaging shipped
plain `nhdat`, `dlb_init()` failed, and `error()` killed the process. The four
copyright `pline()`s observed in D3 were a *symptom* of this, not a separate
event.

**D5 — `other_self_recover_prompt()` hangs any GUI port.** See §6a above.

**D6 — user-reported, two parts.**

- *Stairs drew as a placeholder.* Root cause in "Known placeholders" above: the
  starting square is a **branch** stair, a symbol that did not exist in 3.6.6.
- *No inventory icons at all.* 5.0.0 only creates `WIN_INVEN` when `perm_invent`
  is enabled; a plain `i` renders through `gc.cached_pickinv_win` instead (see
  `display_pickinv()` in `src/invent.c`). Vulture resolved each row's
  `struct obj *` only `if (nhid == WIN_INVEN)`, so `obj` stayed NULL and
  `objitemwin::draw()` skipped the tile entirely. The identifier is the invlet in
  both cases (`any.a_char = ilet`), so the lookup now always falls back to
  scanning `gi.invent`. Menus that reference non-inventory objects (floor pickup,
  containers) pass a real `obj` pointer and take the branch above, so this cannot
  bind the wrong object.

## Portability fix (not a defect)

`vulture_chdir_to_datadir()` was a no-op on `_WIN32`, leaving the game dependent
on the working directory — fine for an Explorer double-click, broken for a
shortcut with a different "Start in". It now anchors to the executable's own
directory via `GetModuleFileNameA`. Verified by launching from `C:\`.

## Test-hygiene note

While purging stale lock files between runs, a filter intended for level files
(`<player>.<n>`) also matched `bonD0.4` and deleted a pre-existing **bones** file
from the user's own earlier NetHack play. Bones files are regenerated by design
and nothing else was touched, but the filter was too broad and is recorded here
rather than quietly dropped.

