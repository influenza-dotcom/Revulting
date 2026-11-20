# Vulture ← NetHack 5.0.0 port — Implementation Log

Started: 2026-07-23. Working host: Windows 10 Home 19045 (`C:\Users\dalla`).

This log records completed gates, failures, decisions, asset hashes, and remaining
risks, per the task working rules. Everything here is verified in *this* environment
unless explicitly marked HYPOTHESIS.

---

## 1. Pinned revisions (source of truth)

All from `github.com/NetHack/NetHack` and `github.com/rahuljaguste/Vulture-NetHack-3.6.6`,
captured via `git ls-remote` on 2026-07-23. Commit SHAs below are the pinned targets;
clone `rev-parse HEAD` values are confirmed as the trees land.

| Component | Ref | Commit SHA (pinned) | Confirmed in clone |
|---|---|---|---|
| NetHack 5.0.0 | tag `NetHack-5.0.0_Released` | `16ff59115315917b93185d026aeefea06db9b0f4` | ✅ confirmed |
| NetHack 5.0.0 (annotated tag obj) | `NetHack-5.0.0_Release` | `8bb2e094625ada92543898de7623fe61549a02bb` | n/a |
| NetHack 3.6.7 | tag `NetHack-3.6.7_Released` | `ed600d9f0f3c37677418f0150f59363ca641f3dc` | ✅ confirmed |
| NetHack 3.6.6 | tag `NetHack-3.6.6_Released` | `34c56eece35f42aa99bb5ab1a829bb31b2db3b1a` | ✅ confirmed |
| Vulture-NetHack-3.6.6 baseline | default branch HEAD | `c7d1cd3119ebf1319a3e3d142ff93643d1325a38` | ✅ confirmed |

NetHack 5.0.0 is the release of the `NetHack-3.7` dev branch (patchlevel.h: VERSION 5.0,
PATCHLEVEL 0, `$NHDT-Branch: NetHack-3.7`). Lua/PDCurses are **git submodules**
(`submodules/lua` → github.com/lua/lua.git; pdcurses/pdcursesmod) — NOT vendored, and the
`--depth 1` clone left them empty. Fetching them (`git submodule update --init` or the
bundled `sys/windows/fetch.cmd`) is required before any 5.0.0 build.

Clones are shallow (`--depth 1`) at the pinned refs — enough to pin the tree; full
history not needed. Tarball SHA-256 not used (cloning by pinned tag/commit instead).

## 2. Workspace layout

```
C:\Users\dalla\Vulture\
  vulture-baseline\        # Vulture-NetHack-3.6.6 clone → becomes the port working tree
  refs\nethack-5.0.0\      # vanilla NetHack 5.0.0 (port target source)
  refs\nethack-3.6.6\      # vanilla 3.6.6 (for baseline patch-inventory diff)
  refs\nethack-3.6.7\      # vanilla 3.6.7 (reference for reconciling hooks)
  IMPLEMENTATION_LOG.md    # this file
```
`win/vulture/` inside `vulture-baseline` is PRESERVED — never overwritten by imports.

## 3. Build environment manifest (verified 2026-07-23)

| Tool | Status | Detail |
|---|---|---|
| C compiler | ✅ PRESENT | MSVC `cl.exe` **19.50.35721** x64 (VS Community 2026 = "VS 18"). Toolsets 14.44.35207 + 14.50.35717. |
| vcvars | ✅ | `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat` |
| nmake | ✅ PRESENT | via vcvars (MSVC 14.50 bin) |
| Windows SDK | ✅ | 10.0.14393.0 / 15063.0 / 16299.0 (older kits) |
| Python | ✅ | 3.13.2 + pip (for Path A `validate.py`, `contact_sheet.py`) |
| git | ✅ | 2.54.0.windows.1 |
| node / npm | ✅ | v14.18.1 / 6.14.15 (not needed for port) |
| **gcc / clang / mingw / msys2** | ❌ MISSING | no Unix/gcc toolchain; `/c/msys64` on PATH but absent |
| **make / gmake** | ❌ MISSING | only nmake (MSVC) |
| **lua / luac (external)** | ❌ MISSING | — but NetHack 5.0.0 vendors Lua in-tree (HYPOTHESIS, verifying) |
| **flex / bison / yacc** | ❌ MISSING | NetHack ships pre-generated parser C (HYPOTHESIS, verifying) |
| **SDL2 / SDL2_ttf / _mixer / _image** | ❌ MISSING | required by Vulture isometric renderer (Path B graphics), NOT by tty/tilemap gates |
| winget / choco / scoop | ❌ MISSING | no package manager to auto-provision a toolchain |

MSVC sanity check PASSED: `hello.c` → `cl /nologo hello.c` → `hello.exe` prints `MSVC-OK`.

## 3a. DECISION (2026-07-23): build environment = WSL2 Ubuntu 24.04

User chose **WSL2 Ubuntu (apt)** among {MSVC-native, WSL2, MSYS2}. Rationale: Vulture is
a native Unix/SDL project; WSL is the only chosen path that can reach the isometric
renderer. Toolchain installed into the existing Ubuntu 24.04 distro (as root, non-interactive,
with user consent):

```
gcc 13.3.0, g++, make, bison 3.8.2, flex, pkg-config
SDL2 2.30.0 + SDL2_image/_mixer/_ttf, libpng, libogg/vorbis/theora, zlib, uuid-dev
```
All headers present (`/usr/include/SDL2/SDL.h`, `png.h`, `ogg/ogg.h`, …). Builds run in
**WSL-native FS** (`~/port/…`) for speed/correctness; canonical trees + deliverables stay
on Windows at `C:\Users\dalla\Vulture`.

Build recipes (verified from source/READMEs):
- **NetHack 5.0.0 tty:** `cd sys/unix && sh setup.sh hints/linux.500 && cd ../.. && make fetch-lua && make -j all && make install` (Lua 5.4.8 via `make fetch-lua`).
- **Baseline Vulture 3.6.6:** `cd sys/unix && sh setup.sh && cd ../.. && make -j && make install` → `compiled/games/lib/vulture-nethack-3.6.6dir/`.

## 4. (RESOLVED) Build strategy — see §3a

The task specifies building via **"Vulture's makefile structure"**, which is historically
a **Unix/autotools + gcc + SDL** build. This host has **MSVC only** (no gcc/make/mingw),
**no SDL2**, and **no package manager** to install them.

- Build-system stop condition (Gate A tty 5.0.0, Gate B tilemap smoke) needs **only**
  a C compiler + make + (vendored) Lua — **no SDL2**. This is plausibly reachable with
  **MSVC + NetHack's native `sys/winnt` nmake build**.
- The isometric renderer (Path B, the product objective) needs **SDL2**, which is absent.

→ Real fork, surfaced to user before committing an 8-session effort. See §5.

## 5. Gate status

| Gate | Status |
|---|---|
| Phase 0: baselines / manifests | ✅ DONE (4 revs pinned+verified; env manifest; atlas validated; patch inventory) |
| Phase 0.2: stock 5.0.0 tty build+run | ✅ DONE — built in WSL, runtime-verified (see §7) |
| Gate A: 5.0.0 tty via **Vulture** makefile structure | PARTIAL — stock 5.0.0 tty done; Vulture-driven build needs core import (Phase 3) |
| Gate B: tile-generator smoke (`-DTTY_TILES_ESCCODES`) | ✅ DONE (see §9) |
| Phase 0.1: baseline Vulture 3.6.6 build+run | BUILT ✅ (graphics/audio linked, WSLg live); visual screenshot pending (see §11) |
| Phase 1: Path B compatibility gate | ✅ DONE — gate PASS, loader+break tests pass (see §13) |
| Phase 2: Path A remap tooling | ✅ DONE — gate PASS, remap_table.h generated (see §14) |
| Phase 3–6 | NOT STARTED |

## 7. Phase 0.2 result — stock NetHack 5.0.0 tty (VERIFIED 2026-07-23)

Build (WSL2 Ubuntu, in-WSL LF clone at `~/port/nethack-5.0.0`):
`sh setup.sh hints/linux.500` → `make fetch-lua` (lua-5.4.8, shasum OK) → build **liblua
serially** (avoids the `-j` `ar` race on `liblua.a`) → `make -j all` → `make install`.
Result: `src/nethack` 11.6 MB; installed to `/root/nh/install/games/lib/nethackdir/`
with `nethack`, **`nhdat` 1.48 MB** (DLB w/ Lua levels), `recover`, `sysconf`.

Runtime (headless pty harness `nh_playtest.py`, wizard mode `-D`, WIZARDS=root):
```
LAUNCHED=True  LEVEL1=True  DESCENDED=True
```
Observed: version banner "5.0.0 Unix"; character creation (wizard the lawful human
Knight); Dlvl:1 map rendered (walls/floor/@/monsters/$/%); wizard level-teleport →
"Dlvl:2 … You materialize on a different level!" (new level generated + rendered).
→ Validates Lua link + runtime dat/ lookup + level gen + descent.

Gotchas found: (1) Windows git clone = CRLF → dash `setup.sh` syntax error; fix = clone
in-WSL (LF). (2) `-j` race truncates `liblua.a`; fix = build lua serially first.
(3) 5.0.0 adds a **"Do you want a tutorial? [y/n]"** startup prompt (new vs 3.6.x) — any
automated driver must answer it. (4) WSL default user = root; `WIZARDS=root` in sysconf.

## 8. Verified deliverable: patch inventory (Vulture vs vanilla 3.6.6)

`deliverables/patch_inventory_{src,include}.txt`. Matches the task's stated hook list
EXACTLY; Vulture added 0 new src files (pure in-place hooks):
- **src (11):** cmd, display, do_name, do_wear, end, files, options, pager, version, weapon, windows
- **include (4):** config, global, hack, tradstdc

## 5a. Path A atlas — VALIDATED (Phase 0 art input)

`win/vulture/gamedata/map/ascii/nethack_3_6_0.png` (in Vulture baseline):

| Property | Expected (task) | Measured | Result |
|---|---|---|---|
| SHA-256 | `15582d0e…dcf7edd4` | `15582d0e34caa7e177f2328a1b66f04da7238da68330f0500903a5cddcf7edd4` | ✅ exact match |
| Dimensions | 280 × 518 | 280 × 518 | ✅ match |
| Transparency | (record type) | PNG colortype 6 (RGBA), no `tRNS` | **ALPHA channel** (not colour-key) |

Grid 40 cols × 7×14 px/cell → 280/7 = 40 cols, 518/14 = 37 rows = 1480 grid cells
(task's "1,200 addressable" cap to be reconciled in Phase 2).

## 5b. Build-system reality (verified against pinned trees)

**Vulture baseline** (`win/vulture/GNUmakefile`): GNU **make** + **gcc/g++** + **SDL2**
(`sdl2-config`) + **libpng** (`pkg-config`) + **flex** + **bison** (regenerates
`vulture_tileconfig.parser/.lexer` at build time). No `configure`/CMake/nmake at root.
→ A **Unix/SDL** build. Cannot build as-is on an MSVC-only host.

**NetHack 5.0.0**: hints-based Unix makefiles (`sys/unix/Makefile.* + hints`) **and**
first-class Windows support in `sys/windows/`: `Makefile.nmake`, `GNUmakefile`,
`build-msys2.txt` / `build-nmake.txt` / `build-vs.txt`, and a full VS solution
(`sys/windows/vs/*.vcxproj` incl. `lualib.vcxproj`, `makedefs.vcxproj`, `NetHack.vcxproj`).
`win/share/tilemap.c` present (Gate B). dat/ is runtime Lua.
→ Gates A/B (tty + tilemap, **no SDL**) are reachable with the **MSVC** toolchain on hand.

**Consequence:** MSVC can pass the build-system stop condition, but **cannot** reach the
Path B isometric renderer (Vulture's SDL/flex/bison GNU build). The product objective needs
a Unix-style toolchain (gcc/make/flex/bison/SDL2).

## 5c. Host toolchain options (all verified present/absent)

| Environment | Status | Can pass Gates A/B | Can build Path B (SDL renderer) | Setup cost |
|---|---|---|---|---|
| MSVC (VS 2026) | ✅ installed | Yes (5.0.0 `Makefile.nmake`/VS) | No (no SDL/flex/bison MSVC path) | none |
| WSL2 Ubuntu 24.04 | ✅ installed (bare) | Yes | **Yes** (`apt` → gcc/make/flex/bison/SDL2) | `sudo apt-get install …` |
| MSYS2 + mingw-w64 | ❌ not installed | Yes | Yes | download+run MSYS2 installer, then pacman |

→ **Decision surfaced to user** (see §4). No toolchain will be installed without consent.

## 9. Gate B result — tile-generator smoke (VERIFIED 2026-07-23)

Enabled in `sys/unix/hints/linux.500`: uncommented `NHCFLAGS+=-DTTY_TILES_ESCCODES`
and `WINSRC += tile.c` / `WINOBJ += tile.o`; re-ran `setup.sh`; rebuilt.
- `config.h:697-701`: `#ifdef TTY_TILES_ESCCODES → #define TILES_IN_GLYPHMAP` — confirmed
  by a preprocessor probe (`cc -DTTY_TILES_ESCCODES … #ifndef TILES_IN_GLYPHMAP #error`) → exit 0. ✅
- `win/share/tilemap.c` → built `util/tilemap` (296 KB) → generated `src/tile.c`
  (1.32 MB, **9661 lines**, real content, NOT the `#error bogus` fallback). ✅
- Full tiles build `make -j all` exit 0; `src/tile.o` (378 KB) **linked** into `src/nethack`
  (symbols `Tile_corr`, `Tile_stone`, `Tile_unexplored`, `maxmontile`, `maxobjtile`, `maxothtile`). ✅

## 10. Verified deliverable: gamedata/tiles manifest

`deliverables/gamedata_tiles_manifest.sha256.txt` — sorted path + SHA-256 for **2388**
PNGs. Files are named `<category>.<name>.png`; the 11 prefixes match the task's
`vulture_tiles.conf` categories exactly:
`cursor 17, edge 12, explosion 27, figurine 405, floor 109, misc 112, monster 517,
object 348, objicon 348, statue 405, wall 88`.

## 11. Phase 0.1 result — baseline Vulture 3.6.6 (BUILT 2026-07-23)

`cd sys/unix && sh setup.sh && cd ../.. && make -j && make install` in WSL (in-WSL LF clone
`~/port/vulture-baseline`, SHA c7d1cd3…). Binary+gamedata installed to
`compiled/games/lib/vulture-nethack-3.6.6dir/vulture-nethack-3.6.6` (11.3 MB).
`ldd` confirms linkage of SDL2, SDL2_mixer, SDL2_ttf, libpng16, libogg, libvorbis(file/enc),
libtheoradec → graphics + sound + music + intro-video codecs. **WSLg live** (DISPLAY=:0,
WAYLAND_DISPLAY=wayland-0, /mnt/wslg + PulseAudio) → app can render + play audio.
REMAINING: a pixel-level visual confirmation (launch under WSLg + screenshot) — needs a
screenshot tool (grim/scrot) and driving the SDL intro; deferred (not toolchain-blocking).

## 12. Phase 1 (Path B) resolver notes — for next session

`win/vulture/vulture_tileconfig.cpp`:
- `vulture_get_tile_index(int type, char *name, int allow_expand)` (:268) — linear search of
  `tilenames[type]`; **if `allow_expand` and not found, it APPENDS a new slot** (`vulture_typecount[type]++`).
  This is the auto-expand the Phase-1 gate must restrict to the literal `ERROR` name only.
- `vulture_typecount[NUM_TILETYPES]` per category; WALL/FLOOR/EDGE start at 0 and grow during parse.
- `deftiles[NUM_TILETYPES]` = per-category default; `V_TILE_NONE = -1000` (vulture_tile.h:248).
- `vulture_redirect_tile(type,name,type2,name2)` implements `=>` aliases.
Config `vulture_tiles.conf` (3561 lines): `<cat>.<SYMBOLIC_NAME> = "tiles/<file>.png" offx offy`
(direct) | `=> <cat>.OTHER` (alias) | `<cat>.default => …`. Symbolic names = NetHack object/
monster constant names (DAGGER, LONG_SWORD, STRANGE_OBJECT…) → must resolve vs 5.0.0 objects[]/mons[].

## 13. Phase 1 (Path B) — audit complete + placeholder design (2026-07-23)

**Mechanism (verified):** `init_objnames()`/`init_monnames()` derive a symbolic name per
`objects[]`/`mons[]` index (`obj_descr[].oc_name` → uppercased with class prefixes;
`PM_<mname>`). `vulture_tiles.conf` `<cat>.<NAME>` binds only if `<NAME>` matches a derived
name; unmatched config names return −1 (dropped); NetHack indices with no config entry fall
to `deftiles[cat]` which today = ordinary art (`object.STRANGE_OBJECT`) — the forbidden
"plausible-but-wrong" fallback.

**Authoritative name extraction:** `pathb/dump_names.c` reproduces the derivation, compiled
against each tree's real data (5.0.0 needs `objects_globals_init()`/`monst_globals_init()`;
struct layout requires the tree's exact build flags). Output: `pathb/nh500_names.tsv`
(481 obj, 383 mon), `pathb/nh366_names.tsv` (454 obj, 382 mon).

**Compatibility report** (`deliverables/pathb_compat_report.md`, `pathb_compat.json`;
generated by `pathb/validate_pathb.py`, non-zero exit on gate violation). Current gate:
**FAIL, 21 violations** (expected pre-placeholder). Drift vs 5.0.0:

| cat | 5.0.0 syms w/o config entry → wrong art | new species/objs in 5.0.0 |
|---|--:|--:|
| object/objicon | 33 | 30 |
| monster | 17 | 19 |
| statue | 27 | 19 |
| figurine | 31 | 19 |

Confirmed renames: `PM_ALIGNED_PRIEST`→`PM_CLERIC`/`PM_ALIGNED_CLERIC`; dwarf/elf/gnome
royalty → `_LEADER`/`_RULER`/`_NOBLE`/`_MONARCH`; `ACID_VENOM`→`SPLASH_OF_ACID_VENOM`;
new `GOLD_DRAGON`, `DISPLACER_BEAST`, and 5.0.0's `GENERIC_<class>` objects. Only 4/11
categories had a default, all pointing at ordinary art.

**Placeholder work done:**
- `pathb/make_error_tiles.py` → **11 conspicuous `<cat>.error.png`** (64×64 magenta + hazard
  border + black X + per-category accent) in `gamedata/tiles/`. Never resembles game art.
- Resolver allow-list applied to `vulture_tileconfig.cpp:vulture_get_tile_index()`: expands
  only WALL/FLOOR/EDGE (auto-expand) **or** the literal `ERROR` (any category); every other
  unknown returns −1 (typo detection preserved; drift reported by validate_pathb.py).

**⭐Constraint discovered:** `GAMETILECOUNT` + fixed-category offsets derive from COMPILE-TIME
counts (`NUM_OBJECTS`/`NUMMONS`/`V_MISC_MAX`/`V_CURSOR_MAX`/`EXPL_MAX*9`), and `game_tiles[]`
is sized from them. So an ERROR slot for a FIXED category must be RESERVED (bump the
`*TILECOUNT` macro by 1 + set `tilenames[cat][count]="ERROR"` in `init_*names()`), NOT
runtime-appended (which overflows). WALL/FLOOR/EDGE are runtime-counted and expand safely.

**Phase 1 COMPLETED (2026-07-24):**
- ERROR slots RESERVED: `vulture_tile.h` fixed-cat `*TILECOUNT` macros +1; `init_*names()`
  name the reserved last slot `"ERROR"` (OBJECT/MONSTER/EXPL/MISC/CURSOR; OBJICON/STATUE/
  FIGURINE inherit by copy); CURSOR typecount now uses `CURTILECOUNT` (was `V_CURSOR_MAX`).
- Config: 11 `<cat>.ERROR = "tiles/<cat>.error.png"` bindings + 11 `<cat>.default => <cat>.ERROR`.
- Rebuilt in WSL (exit 0, no errors), reinstalled. Gate `validate_pathb.py` → **PASS (0 violations)**;
  WALL/FLOOR/EDGE baselines committed (`pathb/{wall,floor,edge}_names.baseline` = 89/110/13)
  and asserted on re-run.
- **Loader/break test** `pathb/test_pathb_loader.cpp` (parses the real config against NetHack
  data, no display). Results (`pathb/pathb_loader_test.out`, 3× PASS / 0 FAIL):
  - `GAMETILECOUNT == sum(typecount) == 2440` → **no game_tiles[] overflow** (reservation correct).
  - all 11 categories: ERROR → valid abs index, owns its `<cat>.error.png` (ptr −1), PNG on disk,
    not tile 0 / not `V_TILE_NONE`.
  - intentional break (remove `monster.PM_GIANT_ANT`, `explosion.FIERY_1`) → those tiles
    redirect to their category ERROR (`ptr==err_abs`) i.e. the conspicuous placeholder renders.
- Artifacts committed to the port tree under `win/vulture/pathb/` and mirrored to `deliverables/`.

Scoping note: break test covers monster+explosion (fixed, data-indexed → `deftiles`→ERROR path);
wall/floor are auto-expand (every index is config-defined, so no "unmatched" index occurs) — their
ERROR asset + default are validated by the per-category loader checks and the baseline assertion.
The runtime WSLg *visual* of a rendered ERROR tile is deferred to Phase 6 acceptance.

## 14. Phase 2 (Path A remap tooling) — DONE (2026-07-24)

Path A = the ASCII map view (`winclass/map.cpp:125` `map_symbols[glyph2tile[glyph]]`); atlas
`gamedata/map/ascii/nethack_3_6_0.png` (280×518, 40 cols × 7×14px), `V_MAX_MAP_SYMBOLS =
40*30 = 1200` addressable. 5.0.0 `glyph2tile` reaches 1514 → must remap into the atlas.

Data (per-file `# tile N`): 5.0.0 = 1515 (mon 789 / obj 483 / other 243, global = mon,
obj+789, other+1272); 3.6.6 = 1082 (== atlas cells); 3.6.0 (atlas order) = 1082, monsters.txt
byte-identical to 3.6.6.

Tooling in `win/vulture/patha/` (mirrored to `deliverables/patha/`):
- **`remap.manifest.json`** — hand-edited source of truth: 73 aliases (5 wall-sets × 11 →
  3.6.6 named/`sub X walls N`; monster royalty/clergy renames leader→lord, ruler→king,
  cleric→aligned priest, …; 5 per-alignment altars → altar), 6 force-placeholder new species,
  6 anchors, metadata hashes, atlas SHA (pristine+modified).
- **`validate.py`** — parses tiles; **3.6.0-vs-3.6.6 order diff** (51 label-only diffs at stable
  positions) committed as `order_baseline.json`, fails on change; matches full-name → head
  (ordinal-aware) with **gender-collapse** for monsters (5.0.0 male+female → one 3.6.6 tile;
  handles werejackal/wererat/werewolf human/animal dups) + object `" / "` fallback; validates
  coverage(1515)/range/anchors/metadata hashes/atlas SHA+dims; **placeholder allow-list**
  committed as `placeholder_baseline.json`; emits `remap_table.h` only on PASS.
- **`remap_table.h`** — generated, committed: `vulture_patha_remap[1515]` → atlas cell (0..1081)
  or `VULTURE_PATHA_PLACEHOLDER` (1199). Gate: **PASS (0 violations, 151 placeholders)**.
- **`contact_sheet.py`** → `contact_{monsters,objects,other}.png` (placeholders in magenta),
  `patha_mapping.html`, `mapping.csv` (5.0.0 idx/file/name, target cell, 3.6.x name, provenance).
- **`reserve_placeholder_cell.py`** — painted a conspicuous magenta+X marker into atlas cell
  1199 (blank in 3.6.x, addressable). Atlas SHA: pristine `15582d0e…` → modified `40f50838…`
  (280×518 unchanged), asserted by validate.py.
- GNUmakefile: `patha-remap` target regenerates the committed header from the manifest.

Placeholders (151 = safe conspicuous, per task principle): monsters 12 (6 new species ×
male/female: gold dragon, displacer beast, …), objects 32 (5.0.0 `generic <class>` + a few),
other 107 (animation/effect tiles — zaps/explosions/swallow/warning — rarely persistent on the
map view). Provenance: 525 full + 752 head + 86 alias + 1 slash + 151 placeholder.

V_TILE_PLACEHOLDER design: chose the **reserved-atlas-cell** option (task-preferred). Cell 1199
< V_MAX (1200) so no code-size change; distinct from `V_TILE_NONE` (−1000, "draw nothing").
Draw-path wiring (route glyph2tile output through the remap; special-case placeholder) is
Phase 4.

## 15. Git checkpoint (2026-07-24)

Branch `nethack-5.0.0-port` off Vulture baseline `c7d1cd3`:
- `6a613b6` Path B: conspicuous ERROR placeholders + resolver allow-list (Phase 1)
- `33b3aeb` Path A: NetHack 5.0.0 → Vulture atlas remap tooling (Phase 2)
Working tree clean. (IMPLEMENTATION_LOG.md + deliverables/ live outside the repo.)

## 16. Phase 3 foundation + Phase 3-6 scope (STARTED 2026-07-24)

**Hooks extracted** → `deliverables/phase3_hooks/*.diff` (Vulture vs vanilla 3.6.6):
544 total diff lines across the 15 files, mostly small additions
(largest: config.h +25/-8, files.c +35, do_wear.c +27, end.c +19; hack.h +3 = the
C++ min/max guard). Reapplying these onto 5.0.0 is BOUNDED/mechanical.

**The real Phase 3-6 effort = adapting `win/vulture/` (91 files, ~17.2k lines) to the
5.0.0 API** (win/vulture is PRESERVED, so its call sites must be ported):
- print_glyph: 7 sites → change to `print_glyph(coordxy, const glyph_info*)`,
  use `glyph->gm.tileidx`, handle fg/bg (Phase 4).
- `mons[].mname` → `pmnames[NEUTRAL]` (vulture_tileconfig.cpp init_monnames; also the
  Path A/B dumpers already handle this).
- add no-op `win_ctrl_nhwindow`; XCHAR_P→coordxy (Phase 4).
- **Phase 5 globals migration: ~230 direct reads** — u. 87, flags. 76, level. 27,
  iflags. 16, objects[ 17, mons[ 7 (many may be unchanged in 5.0.0; each needs review).
- Import disposition (replace src/include/dat/util/win-share/win-tty; patch tilemap.c
  with remap_tileidx routing glyph indices through patha/remap_table.h; preserve
  win/vulture; manually merge sys/ + top-level makefiles; reconcile for runtime Lua).

## 17. Phase 3/4/5 EXECUTION (2026-07-24) — port compiles down to 9 errors

Approach (cleaner than the literal "replace core in Vulture's build"): **add the vulture
windowport to 5.0.0's known-good build**. Port tree = `~/port/vulture5` (WSL) = 5.0.0 core +
build + Lua (from the Gate-A tree) with `win/vulture` overlaid (incl. Path A/B changes;
atlas SHA 40f50838 verified). Snapshotted to vulture5 branch `nethack-5.0.0-port` (WSL commit
`f5e7faa`); reproducible scripts + `vulture_nh5compat.h` in `deliverables/phase3/`.

- **Hooks:** 8/15 applied cleanly via `patch --fuzz`; 7 rejects analyzed (end.c = Emscripten,
  skip; options.c defaults, defer; config.h VULTURE_GRAPHICS, do_wear/pager non-static,
  display.c show_glyph = adapt). 5.0.0 removed FDECL/NDECL/STATIC_* macros.
- **API-gap measured then fixed: 65 → 9 compile errors** (`-fsyntax-only`, 43 win/vulture files;
  28 already clean). Phase 5 globals migration essentially DONE — 5.0.0 moved globals into
  `sv*`/`g*` structs; fixed via site seds: `level→svl.level` (excl. minimap member),
  `moves→svm.moves`, `context→svc.context`, `invent→gi.invent`, `dungeons→svd.dungeons`,
  `rooms→svr.rooms`, `nroom→svn.nroom`, `plname→svp.plname`, `toplines→gt.toplines`; plus
  `mname→pmname(&mons[i],NEUTRAL)`, PM priest→cleric, `SET_IN_FILE→set_in_config`,
  `describe_level(x)→(x,0)`, `monnum_to_glyph(m)→(m,Ugender)`, `can_advance` dup decl removed.
  Uppercase-safe shim `vulture_nh5compat.h` (force-included) handles stdlib + PM/MENU renames.
  ⭐LESSON: don't force-include macros for common lowercase words (moves/context) — they
  corrupt SDL/system headers; edit sites instead.
- **Remaining 9 errors = the Phase 4 window-API adaptation, in 2 files:**
  1. `vulture_main.cpp` window_procs initializer (3): 5.0.0 inserted `enum wp_ids wp_id` as
     field 2 (misaligns the positional init) and changed `win_print_glyph`→`(coordxy, const
     glyph_info *)`, `win_add_menu`→`glyph_info`; needs the initializer reconciled field-by-field
     + `win_ctrl_nhwindow` no-op + the vulture fn signatures updated.
  2. `winclass/mapdata.cpp` (6): `mapglyph(glyph,&ch,&col,&attr,x,y,0)` ×5 → `map_glyphinfo()`
     + extract from `glyph_info`; `what_trap` (removed) → 5.0.0 equivalent.
- **Then:** link (resolve undefined win_* / vulture symbols), merge build system (add a vulture
  windowport option to 5.0.0's Makefile.src/hints: WINVULTUREOBJ + `$(MAKE) -C win/vulture` +
  SDL libs), Phase 4 tile.c `remap_tileidx` (route glyph indices through `patha/remap_table.h`),
  then Phase 6 runtime.

## 18. ⭐CRITICAL: fuzzy `patch(1)` SILENTLY CORRUPTED core files (2026-07-24)

Reapplying the Vulture hooks with `patch --fuzz=3` reported "8/15 applied cleanly" — but an
audit of *where* each hunk landed found **silent misapplication into unrelated functions**:
- `src/version.c`: an Emscripten `#ifdef __EMSCRIPTEN__ … return TRUE; #else` was spliced into
  the middle of a documentation-parsing function, with its `#endif` landing ~170 lines away in
  a *different* function. Balanced, compiles, totally wrong control flow.
- `src/files.c`: same pattern (`return TRUE;` at 1737, `#endif` at 2433).
- `src/end.c`: the Emscripten `nethack_exit()` block spliced into the middle of `done1()`.
- `src/do_name.c`: both hunks landed in the wrong functions — and `getpos()` **moved to
  `src/getpos.c`** in 5.0.0, so the correct target file wasn't even touched.

This is exactly the "plausible but wrong" failure the task warns about, and it would not have
been caught by "it compiles". **Resolution:** reverted all 16 core files to pristine 5.0.0
(`git checkout 16ff5911 -- …`) and reapplied every hook by **exact string match** via
`deliverables/phase3/scripts/apply_hooks_manual.py` (fails loudly if an anchor is missing).
17/17 anchors matched. LESSON: never fuzzy-patch across a major version gap; match exactly
and audit the landing site.

Hooks re-evaluated against 5.0.0 (several turned out unnecessary):
| hook | disposition |
|---|---|
| config.h DEFAULT_WINDOW_SYS, global.h vulture_conf/version + PORT_SUB_ID | applied |
| windows.c include + `{ &vulture_procs, win_vulture_init }` table entry | applied |
| cmd.c MAX_EXT_CMD guard; end.c unconditional outrip | applied |
| display.c show_glyph always-notify (`if (1 \|\| …)`) | applied |
| getpos.c `vulture_whatis_active` | applied **to getpos.c** (moved from do_name.c) |
| pager.c `lookat`, do_wear.c `select_off`/`take_off` non-static | applied (win/vulture calls them) |
| weapon.c `can_advance` non-static | **not needed** — already non-static in 5.0.0 |
| hack.h C++ min/max guard | **not needed** — 5.0.0 already has `#ifndef min/max` |
| files.c `VULTURE_SAVE_*` | **not needed** — unused by win/vulture |
| version.c, tradstdc.h, end.c hunk 1 | **dropped** — Emscripten only (not preserved, per task) |
| options.c dark_room/toptenwin defaults | deferred (cosmetic; 5.0.0 option table restructured) |

Also required for 5.0.0: `src/mdlib.c` `window_opts[]` needed a `{"vulture", …}` entry, else
`makedefs` aborts with `Configuration error: DEFAULT_WINDOW_SYS (vulture)`.

⭐Second parallel-make race found: `-j` builds `util/makedefs` twice → `Permission denied`
(Error 127). Same class as the liblua race. Build serially (`-j1`) through the generated-file
stage.

## 19. Build-system merge (Phase 3)

Added a `WANT_WIN_VULTURE` block to `sys/unix/hints/linux.500` (task's "manually merge"):
sets `WINVULTUREOBJ = win/vulture/build_nethack_3_6_0/vulture.o + tile.o + theoraplay.o`,
`WINSRC=tile.c`, SDL2/png/ogg/vorbis/theora `WINLIB`, `TARGET_LINK=$(CXX)` (vulture.o is C++),
`NHCFLAGS += -I../win/vulture -DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 -DVULTURE_GRAPHICS
-DTILES_IN_GLYPHMAP`, plus rules that recurse into `win/vulture`'s GNUmakefile
(`BASECFLAGS` force-includes `vulture_nh5compat.h`) and compile `theoraplay.c`.
Build command: `make WANT_WIN_VULTURE=1 WANT_DEFAULT=vulture`.

## 20. ⭐⭐ IT RUNS AND RENDERS — NetHack 5.0.0 through Vulture (2026-07-24)

`make WANT_WIN_VULTURE=1 WANT_DEFAULT=vulture` → **exit 0**, `src/nethack` **16.3 MB**,
linking SDL2/SDL2_mixer/SDL2_ttf/libpng/ogg/vorbis/theora. Installed to
`/root/nh/install/games/lib/nethackdir` together with the full Vulture gamedata
(config/, tiles/ incl. **11/11 `<cat>.error.png`**, fonts/, graphics/, map/ascii atlas
SHA `40f50838…` = the placeholder-reserved copy, sound/, music/), `nhdat` 1.48 MB.

Runtime under WSLg (`SDL_VIDEODRIVER=x11`), captured with Vulture's own **F12** screenshot
(`vulture_save_screenshot()`), saved to `deliverables/shots/`:

**`screenshot-000.png` proves, in one frame:**
- NetHack **5.0.0** running through Vulture's **isometric** renderer
- character creation completed → "Velkommen tester, welcome to NetHack! You are a lawful
  dwarven Valkyrie."
- a real dungeon level rendered in 3/4 view: cobblestone floor, stone walls, a rug, an
  arched doorway
- the player sprite, a **pet kitten** (red-heart pet marker; "You swap places with your
  kitten") and a jackal sprite
- status line `Tester the Stripling  St:17 … HP:18(18) … Dlvl:1 $:0` and the item hotbar
- ⭐**the Phase 1 ERROR placeholders rendering exactly as designed** — several magenta
  squares with black X + yellow/black hazard border where 5.0.0 content has no 3.6.x art.
  Unmapped content is conspicuous, never tile 0, never nothing, never plausible-but-wrong.

`screenshot-001.png` additionally shows Vulture's dialog/button widgets working
("Screenshot saved as …" + `continue`).

Phase 6 acceptance so far: character selection ✅, dungeon level renders ✅, mapped art
correct ✅, unmapped art visibly marked ✅. Remaining: movement/descend/shop/monster
interaction/save-restore under a proper input driver (xdotool key injection into the WSLg
window was unreliable — the sends land as "Unknown command ' '"; needs a better harness).

## 21. ⭐BUG FOUND + FIXED: uninitialised dungeon-feature tile map (2026-07-25)

Investigating the placeholders visible in the first runtime screenshot turned up a genuine
defect that directly violates the task's core rule ("never render an unresolved tile as
tile zero, ordinary game art, or nothing"):

`mapdata::mapdata()` initialises `vulture_tilemap_engulf[]` with a loop
(`for (i = 0; i < NUMMONS; i++) … = V_TILE_NONE;`) but **never initialises
`vulture_tilemap_misc[MAXPCHARS]`** — it only assigns the ~77 `S_*` cmap symbols Vulture
knew about in 3.4.3/3.6.x. Every other entry was **uninitialised class-member storage**,
then used directly as a tile index. NetHack 5.0.0 makes this materially worse by adding
cmap symbols Vulture never assigns, including:
`S_lavawall, S_poisoncloud, S_vibrating_square, S_trapped_chest, S_trapped_door,
S_brdnstair, S_brupstair, S_brdnladder, S_brupladder, S_engrcorr, S_engroom`
— i.e. real dungeon features (branch stairs! the vibrating square!) drawing from garbage.

**Fix** (`win/vulture/winclass/mapdata.cpp` + `vulture_tile.h`): added
`#define V_MISC_ERROR V_MISC_MAX` (the misc category's reserved ERROR slot is last, so its
absolute tile number is exactly `V_MISC_MAX`) and a full sweep before the specific
assignments:
```c
for (i = 0; i < MAXPCHARS; i++)
    vulture_tilemap_misc[i] = V_MISC_ERROR;
```
Now any dungeon feature without Vulture art renders the conspicuous placeholder instead of
arbitrary art/garbage. Rebuilt + reinstalled; binary grew 16297496 → 16298032 bytes.

⭐Also fixed a build-correctness bug this exposed: the `vulture.o` rule in
`hints/linux.500` depended only on `objects.o/monst.o/date.h`, so **edits to
`win/vulture/**.cpp` did not trigger a rebuild** (make reported success without
recompiling). Rule now depends on a `.PHONY: vulture-force` target and always recurses —
the win/vulture GNUmakefile does its own dependency checking.

Note the same class of latent issue does NOT affect `vulture_tilemap_engulf[]` (properly
loop-initialised to `V_TILE_NONE`).

## 22. ⭐MAJOR BUG: every object rendered as a placeholder (2026-07-25)

The Path B placeholder system paid for itself. New tile-resolution instrumentation
(`$VULTURE_TILEDUMP`) showed **every** object on the level and in inventory resolving to
`tiles/object.error.png` (index 481 = the reserved ERROR slot) — the magenta squares in the
runtime screenshots were objects, not dungeon features.

Cause: `init_objnames()` (vulture_tileconfig.cpp) used 3.6.x's terminator
`for (i = 0; !i || objects[i].oc_class != ILLOBJ_CLASS; i++)`. **NetHack 5.0.0 inserts
"generic <class>" ILLOBJ objects at LOW indices**, so the loop ended almost immediately,
leaving nearly every object unnamed → unmatched against `vulture_tiles.conf` → drawn as the
category placeholder. (Exactly the trap already documented for `dump_names.c` in §13 — it
was fixed in the audit tool but never in Vulture itself.) Fix: iterate the full `NUM_OBJECTS`.

Verified after the fix — objects resolve to real art again, **including the shuffled
appearance substitution**:
```
inv otyp=27  descr_idx=27  resolved=27  png='tiles/object.spear.png'
map otyp=438 descr_idx=438 resolved=438 png='tiles/object.gold_piece.png'
map otyp=305 descr_idx=307 resolved=307 png='tiles/object.pot_healing.png'   <- unidentified potion
```

### Instrumentation added (acceptance testing; no-op unless `$VULTURE_TILEDUMP` is set)
- `vulture_resolve_tile_id()` — the shuffle + config-redirect chain **extracted** from
  `vulture_get_tile_shaded()`, so the renderer and the dump can never drift apart.
- `vulture_tile_filename()` — the PNG a resolved id draws from.
- `vulture_dump_tiles()` — writes inventory + every map object with raw/resolved tile id and
  PNG; triggered from both `vulture_nhgetch()` and `vulture_nh_poskey()`.
- Player selection is now skipped when role+race+gender+align are all preset (matches core
  NetHack, and lets the harness start a game with no keystrokes).

### ⭐Harness lessons (these cost hours; they are not obvious)
1. **NetHack traps SIGTERM** to save, and that save blocks on a GUI prompt — a plain `kill`
   plus `wait` hangs forever. The harness must `kill -9`.
2. **SDL ignores XSendEvent**, so `xdotool key --window` silently does nothing. Activate the
   window and send **global XTEST** events instead.
3. A `kill -9`'d run leaves level files named `<uid><plname>.<lvl>` (e.g. `0wizard.0`) in the
   playground. On the next launch NetHack shows *"Old game in progress. Destroy/Recover/
   Cancel?"* — which silently blocks everything. `save/*` and `*lock*` globs do **not** match
   them; purge `[0-9]+[A-Za-z_]+\.[0-9]+`.
4. Vulture's **F12 screenshot pops a modal** that swallows the next keypress, so a capture
   must be F12 **followed by a dismiss key**.
5. `SHOPTYPE=<class>` forces a shop onto every random level in wizard mode with no depth
   gate (`mklev.c`), and `SHOPTYPE='!'` yields a potion shop — a whole room of unidentified,
   shuffled-appearance items, i.e. the shop test and the save/restore subject in one level.
   (Research also confirmed: wishing does **not** identify; `#wizmakemap`/`#wizloaddes` are
   unreachable from Vulture's UI, so forcing must happen via env vars before launch.)

## 23. P1 — Lua/command-queue acceptance driver (2026-07-25)

Replaced flaky GUI key injection with NetHack 5.0.0's own scripting path
(`nh.pushkey` → `CQ_CANNED` command queue → drained by rhack/getdir/yn_function/getlin/
getpos, plus `nh.doturn`). Verified present in the built binary (`nhl_pushkey`/`nhl_doturn`
symbols; `nh.pline`, `u.ux/u.uy/u.depth` also exposed, so acceptance can assert on real game
state rather than screenshots).

**Trigger problem and fix.** `#wizloadlua` is unreachable from Vulture: it has no default key
binding and no `AUTOCOMPLETE` flag (so it never appears in Vulture's extended-command menu),
and a `BINDINGS=^X:wizloadlua` / `BINDINGS=&:wizloadlua` line in the rc did **not** reach the
game through this windowport — although the rc *is* read (the preset role/race/gender/align
took effect, giving a zero-prompt start). F-keys demonstrably do reach Vulture (F12), so two
hooks were added beside the existing F11/F12 handlers:
- **F10** — run the Lua script named by `$VULTURE_LUADRIVER` (default `d.lua`)
- **F9** — queue `S` + `y` via `cmdq_add_key(CQ_CANNED, …)`

⭐**Real finding behind F9:** the save **works when driven through the command queue**
(produced a save file *and* removed the level files = a clean save) but **not** when the same
two keys go through Vulture's normal GUI key path, which failed across many attempts. That
asymmetry is a candidate port defect worth investigating on its own.

Driver committed as `win/vulture/pathb/acceptance_driver.lua`: moves, descends via wizard
level-teleport, creates a monster, and asserts on `u.ux/u.uy/u.depth`, reporting via `nh.pline`.

**Remaining flakiness.** WSLg window focus is intermittent: a "focus canary" (send F12,
require a screenshot file to appear before proceeding) succeeded on try 2 in two consecutive
runs but failed all six tries in another — so keystroke-driven runs succeed about two times
in three.

**Headless hooks added (commit `381e614`) — PARTIAL, not working end to end.** To take GUI
input out of the critical path, `vulture_nhgetch()` now supports three env vars (all no-ops
unless set): `VULTURE_LUADRIVER` runs the driver once at the first command prompt;
`VULTURE_TILEDUMP` was moved to fire *after* the driver so it records the FINAL state; and
`VULTURE_AUTOSAVE=1` queues `S`,`y` via `cmdq_add_key()` and returns ESC so `rhack` loops
round and consumes them.

Result of the first headless run: the game **does** run the driver and exit on its own, but
**no save file is produced**, and the driver did not descend (dump shows `dlvl=1`, 33 objects).
Most likely cause: the autosave fires too early — `iflags.window_inited` is true before the
game is genuinely at the main command loop, so `S`/`y` can be swallowed by an earlier prompt.
**Next fix:** gate the autosave on real progress (`moves > 0`, or require that the driver
completed *and* a dump has already been written) rather than `window_inited` alone.

⭐What IS verified and useful regardless: the tile dump
(`deliverables/tiledump_sample.txt`) — objects resolve to real art with the shuffled-appearance
substitution intact, which is the measurement the save/restore criterion needs.

## 24. ⭐CONFIRMED PORT DEFECT: GUI save does not work (2026-07-25)

Save/restore is a non-negotiable success criterion, so the GUI save failure was investigated
properly rather than written off as harness flakiness. Result: **it is a real defect in the
Vulture windowport, not a test artefact.**

**Evidence chain**
1. **Core is good.** The stock NetHack 5.0.0 **tty** build, driven over a pty with the same
   keys, prompts `Really save? [yn] (n)` and on `y` writes `save/0wizard.gz` — SAVE WORKS.
   (`scratchpad/tty_save_test.py`, isolated hackdir so it cannot disturb the port.)
2. **`S` reaches NetHack under Vulture.** A screenshot taken immediately after pressing `S`
   shows Vulture rendering the **"Really save?" dialog with `yes` / `no` buttons**
   (`deliverables/shots/`), so the command dispatched correctly.
3. **`y` does not complete the save.** With window focus *verified by a canary* (F12 must
   actually produce a screenshot file before proceeding) and **only** `S` then `y` sent —
   nothing in between — the result is **no save file and the game still running** (a
   successful save exits). `scratchpad/gui_save_clean.sh`.
4. **The command-queue path DOES save.** Queuing `S`,`y` via `cmdq_add_key(CQ_CANNED, …)`
   produces a save file and removes the level files (a clean save). That bypasses the dialog's
   key handling entirely — which is what localises the fault.

⭐**Correction to an earlier reading in this log:** a previous run that also showed "no save"
was *confounded* — it interleaved an F12 screenshot between `S` and `y`, and F12 pops its own
"Screenshot saved" modal, so the `Return` used to clear that modal may have hit the save
dialog's default button (`no`). The clean re-run above removes that confound and reproduces
the failure, so the conclusion stands on its own.

**Scope narrowed, root cause not yet pinned.** Inspection of the dialog code shows the pieces
look individually correct, which points at event *delivery* rather than dialog logic:
- `vulture_yn_function()` builds a `choicedialog` when `choices` is non-null (vulture_main.cpp).
- `choicedialog` maps `"yn"` to `yes`/`no` labels and takes each button's accelerator from the
  label's first character, i.e. `'y'` and `'n'` (winclass/choicedialog.cpp).
- `choicedialog::handle_keydown_event()` handles Return/Escape via `sym`, and letters via
  `default: if (find_accel((char) unicode) …)`.
- `unicode` here is **not** SDL1's removed field but `vulture_keysym_to_char(sym, mod)`, which
  correctly maps `SDLK_y` → `'y'` (vulture_gen.cpp:298).

So the accelerator, the label mapping and the keysym→char conversion all look right in
isolation. **Next step:** trace `vulture_event_dispatcher(&response, V_RESPOND_CHARACTER, win)`
to see whether the keydown is being routed to the dialog at all, or is consumed by another
window/target first.

**Impact:** a human player currently cannot save from the GUI, so acceptance item 8
(save/restore) **fails** for the product, independent of the test harness. This is the single
most important open defect in the port.

## 25. PHASE 7 — native Windows .exe (2026-07-25)

**Why:** the user wants a double-clickable `.exe`, not a WSL app. WSLg presentation proved
unreliable (correct title bar + audio, blank canvas); my test scripts only ever displayed
because they called `xdotool windowmap/raise/activate/focus` **with retries**. The SDL Wayland
driver is a hard dead end — it crashes in
`vulture_win_resize → vulture_set_screensize → vulture_enter_graphics_mode`.
This was one of the three environments offered in §3a; WSL2 was chosen then, and it was the
right call for *getting the port working*, but the wrong target for *shipping something to play*.

**Environment set up (with consent — machine-modifying):**
- MSYS2 installed silently to `C:\msys64`:
  `msys2-installer.exe in --confirm-command --accept-messages --root C:/msys64`
- mingw-w64 toolchain + SDL2/SDL2_mixer/SDL2_ttf/libpng/zlib/ogg/vorbis/theora/flex/bison:
  **gcc 16.1.0, sdl2-config 2.32.10**, all headers verified present.
- Port source copied to `C:\Users\dalla\Vulture\vulture5-win` (102 MB, 11 ERROR tiles intact).
  ⭐Copy from the **Windows** side with `robocopy \\wsl.localhost\Ubuntu\... ` — a WSL-side
  `tar` into `/mnt/c` is glacially slow. (robocopy rc=1 means success.)

**Research (workflow `wf_b4b6e9e8-024`, 3 agents) — key facts:**
- `sys/windows/GNUmakefile` **is** a genuine mingw/gcc makefile (MSVC is served separately by
  `Makefile.nmake` + `sys/windows/vs/`). Build from `src/` after `cp sys/windows/GNUmakefile* src/`
  (**both** files — `depend` lives in `GNUmakefile.depend`).
- Arch comes **only** from `$MSYSTEM` (MINGW32→x86, else x64).
- Known-green CI form (`azure-pipelines.yml:164-167`):
  `make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 {clean,depend,}`
  — `CI_COMPILER=1` also forces `DEBUGINFO=N`.
- ⭐**Shipped bug**: `NHV=370` (~line 1391) makes the DLB step `mv` a file `dlb.exe` actually
  names `nhdat500`. Must be 500.
- ⭐`DEBUGINFO=N` is mandatory for anything redistributable (`Y` pulls `-lucrtbased`, a
  non-redistributable debug CRT). `USE_LUADLL=N` links Lua statically (default Y ships a DLL).
- ⭐Lua on Windows is built **by the GNUmakefile itself** (33 .c files), *not* via `make fetch-lua`.
- Console link: `-mconsole … -static -lstdc++`; GUI link: `-mwindows` with
  `-DTILES -D_WINDOWS -DMSWIN_GRAPHICS -DNOTTYGRAPHICS`. The GUI block is the template to clone
  for a Vulture windowport (obj dir, CFLAGS var, 8 pattern rules, date.o-last rule, link rule).
- ⭐`windmain.c:79-84` `char default_window_sys[7] = …` is a hard syntax error for any third
  windowport and is one byte too small for `"vulture"`; `GUILaunched`/`msmsg` only exist under
  `WIN32CON`, so **keep `-DWIN32CON` and keep compiling `consoletty.c`**.
- ⭐SDL2 on Windows: `sdl2-config --libs` emits `-lSDL2main`, which wants an `SDL_main` NetHack
  doesn't provide → drop it, or `#define SDL_MAIN_HANDLED` + `SDL_SetMainReady()`. mingw needs
  **no** `WinMain` (plain `main()` + `-mwindows`).
- ⭐`win/vulture/GNUmakefile` needs a MINGW/MSYS arm in its `uname -s` branch **without**
  `-no-pie` (Linux-only). bison/flex/sdl2-config all exist in MSYS2.
- ⭐`win/vulture/gamedata` is **not** in the DLB and no rule ships it — must be copied beside the exe.

**Gate first (same discipline as Gate A):** build the *stock* 5.0.0 core natively before
touching the port. In progress at time of writing — 139 objects compiled, **zero errors**.

**Honest status:** Phases 0-2 complete/tested/committed. Phase 3 largely done — port source
compiles to **9 well-understood errors** (down from 65), all in the 2 Phase-4 window-API files.
The effort is now clearly TRACTABLE (not the open-ended marathon first feared): the remaining
is bounded window_procs reconciliation + mapglyph adaptation + build-system merge + link +
tile remap wiring + runtime. Build-system stop condition (Gates A+B) met and exceeded.

## 26. PHASE 7 COMPLETE — the .exe runs, and six runtime defects fixed (2026-07-25)

`NetHackV.exe` (GUI subsystem, 5,581,312 bytes) launches, renders and plays natively on
Windows 10. Verified by screenshot, not by "it linked". Package: `VultureNetHack/`, 69 MB,
28 bundled DLLs, all other imports stock Windows.

Getting from "links" to "plays" took six real fixes. Each was found by running the thing.

**D1 — Unix `sysconf` shipped to Windows.** `GDBPATH=/usr/bin/gdb` / `GREPPATH=/bin/grep`
do not exist, so NetHack raised 2 config errors and blocked on `--More--`. Under `-mwindows`
there is no console to show it: the process ran forever with **no window at all**. Fixed by
commenting the Unix paths out, zeroing `PANICTRACE_*`, and setting `PORTABLE_DEVICE_PATHS=1`.
*Lesson: a GUI build that blocks on an invisible console prompt looks exactly like a hang.*

**D2 — `nhwindow` vector indexed out of bounds.** 5.0.0 passes `WIN_ERR` (-1) to window
procs where 3.6.6 did not; `vulture_get_nhwindow()` indexed unchecked, so -1 became a huge
`size_t` and tripped libstdc++'s bounds assertion before the first frame. Also fixed the
constructor's free-slot scan, which dereferenced `vulture_nhwindows[id]` *before* testing
`id < windowcount_max`. Callers in `vulture_main.cpp` now all tolerate NULL.

**D3 — parentless dialog crash.** 5.0.0's `main()` calls `pline()` for the copyright banner
*before* `WIN_MESSAGE` exists, so `vulture_putstr()` sees `WIN_ERR` and raises a messagebox
while `ROOTWIN` is still NULL. `mainwin::layout()` then did `parent->w` → SIGSEGV. Now
centres on `vulture_screen` when parentless; same guard applied to `choicedialog`,
`anykeydialog` and `dirdialog`. (`inputdialog` already had it — that was the hint.)

**D4 — wrong DLB filename.** Windows defines `VERSION_IN_DLB_FILENAME`, so NetHack opens
`nhdat` + MAJOR MINOR PATCHLEVEL = **`nhdat500`**, not `nhdat`. Packaging shipped plain
`nhdat`; `dlb_init()` failed and `error()` killed the process. The four copyright `pline()`s
seen in D3 were the *symptom* of this, not an independent event.

**D5 — `other_self_recover_prompt()` hangs any GUI port.** `sys/windows/windmain.c` asks
via `y_n()` (which works), then — for anything that is not mswin or curses — drops into a
raw `while ((ci = nhgetch()) && ci != '\n')` console loop. Vulture fell into that branch and
sat behind a stale, button-less dialog forever. Added `isvulture = WINDOWPORT(vulture)` to
both guards. **This was the "confirmed GUI-save defect" of §24** — same shape, and GUI save
now round-trips (`dalla.NetHack-saved-game`, 67,971 bytes; restore printed
"Aloha dalla, the human Tourist, welcome back to NetHack!" with state intact).

**D6 — user-reported, two parts.**
- *Stairs drew as a placeholder.* 5.0.0 splits branch-crossing stairs into their own cmap
  symbols (`S_brupstair`/`S_brdnstair`/`S_brupladder`/`S_brdnladder`, tty draws them yellow).
  Dlvl 1's up stair leaves the dungeon and is therefore a **branch** stair — which is why the
  hero's *starting square* was a magenta X. Bound to the existing stair/ladder art. Also bound
  `S_engroom`/`S_engrcorr` (3.6.6 had no engraving symbols and drew plain floor — restoring
  parity, not inventing art) and `S_poisoncloud` → cloud.
- *No inventory icons at all.* 5.0.0 only creates `WIN_INVEN` when `perm_invent` is on; a
  plain `i` renders through `gc.cached_pickinv_win` instead. Vulture resolved each row's
  `struct obj *` only `if (nhid == WIN_INVEN)`, so `obj` stayed NULL and `objitemwin::draw()`
  skipped the tile. The identifier is the invlet either way (`any.a_char = ilet`), so the
  lookup now always falls back to scanning `gi.invent`; menus that reference non-inventory
  objects pass a real pointer and take the branch above, so nothing can bind the wrong object.

**Remaining deliberate placeholders** (no 3.6.6-era art; guessing would be worse than a
placeholder): `S_lavawall` (a *wall* of lava — drawing it as lava floor would imply it is
walkable), `S_vibrating_square`, `S_trapped_door`, `S_trapped_chest`, `S_goodpos` (debug
overlay). The `S_sw_*` / `S_expl_*` runs are never looked up in `vulture_tilemap_misc[]` —
swallow and explosion glyphs route through their own tile ranges — so their absence there is
correct, not a gap. Enumerated reproducibly by `toolchain/cmap_gap.py`.

**Portability fix (not a defect):** `vulture_chdir_to_datadir()` was a no-op on `_WIN32`, so
the game depended on the cwd. It now anchors to the executable's own directory via
`GetModuleFileNameA` — verified by launching from `C:\`. The folder can be moved anywhere.

**Acceptance status:** build+launch ✅, character creation renders ✅, dungeon renders ✅,
movement ✅, monsters/pets ✅, inventory ✅, save ✅, restore ✅, unmapped tiles render as a
conspicuous magenta placeholder ✅. **Not yet exercised: descending a level, and shops.**

**Note on test hygiene:** while purging stale lock files between runs I used a filter that
also matched `bonD0.4` and deleted a pre-existing *bones* file belonging to the user's own
earlier NetHack play. Bones are regenerated by design and nothing else was touched, but the
filter was too broad — level files are `<player>.<n>`, and that pattern also matches bones.

## 27. Three more user-reported input/render defects (2026-07-25)

All three are the same species as §26's D6: 5.0.0 changed something underneath a
Vulture assumption that still compiled. Found by the user playing, not by tests.

**D7 — numeric keypad does nothing.** `win/vulture/vulture_sdl.cpp` filtered keys
with `return sym >= SDLK_NUMLOCK && sym <= SDLK_COMPOSE;`. Under SDL 1.2 those two
keysyms bracketed exactly the 15 contiguous modifier values. SDL2 derives keycodes
from scancodes (`keycode = scancode | 0x40000000`), and the SDL2 shim in
`vulture_sdl.h` aliases only the two endpoint *names* — so the test still compiled
while silently changing meaning. In SDL2 the range is scancodes 83..101, which is
**the entire numeric keypad**; the real modifiers (LCTRL..RGUI) are scancodes
224..231, outside it. The predicate was effectively inverted: every keypad press
was swallowed by `vulture_wait_event()` and never reached NetHack, while bare
modifier presses were delivered as commands. No error was printed because the key
was *dropped*, not mistranslated. Fixed by enumerating the modifiers explicitly.
`hjkl` survived because it is plain ASCII; the arrow keys survived by exactly one
scancode (79..82, one below the bad lower bound). `number_pad` was already on in
the shipped config and was **not** the cause.

**D8 — click-to-move prints `Unknown command 'M-^P'`.**
`winclass/mapdata.cpp` kept a private copy of 3.6.x's core pseudo-key
`#define CMD_TRAVEL (char) 0x90` and returned it for `V_ACTION_TRAVEL`. 5.0.0
deleted the whole `spkeys`/`NHKF_TRAVEL` layer; travel is now the ordinary
command `#retravel` → `dotravel_target`, bound to `CTRL+'_'` by
`commands_init()`. `rhack()`'s `cmdbind_get(144)` missed and printed
`visctrl(0x90)` == `M-^P`. Second half of the same stale assumption:
`dotravel_target()` reads `iflags.travelcc`, never `u.tx`/`u.ty`, so setting only
`u.tx`/`u.ty` would have given "No travel destination set." Fixed by setting both
and returning the *runtime* `#retravel` binding (looked up via `extcmdlist` +
`cmd_from_func`) so an RC-file rebind cannot silently break click-to-move.

**D8b — out-of-bounds read next door.** `vulture_nh_poskey()` assigned `*x`/`*y`
but never `*mod`. 5.0.0's `readchar_core()` calls `click_to_cmd()`, which indexes
`gc.Cmd.mousebtn[*mod - 1]` with no range check, and `getpos()` passes an
uninitialised `int`. Now assigns `CLICK_1`, as every 5.0.0-native port does.

**D9 — gems render as the placeholder.** The user diagnosed the mechanic
correctly: the colour is not known until you get close. 5.0.0 added 17 per-class
*generic object* pseudo-entries in slots 1..17 ahead of `FIRST_OBJECT`. For an
object whose specific type the hero has not seen, `obj_to_glyph()` routes through
`generic_obj_to_glyph()`, which encodes the object **class** rather than the
otyp; `glyph_to_obj()` hands that class number straight to the tile layer, where
slot 13 is `GENERIC_GEM` — a name Vulture's 3.6.0-era config never had, so it hit
`object.default => object.ERROR`. Fixed at two layers: a guard in
`vulture_obfuscate_object()` (the chokepoint all three map-draw paths funnel
through) and explicit conf bindings for all 17 slots.

⭐**Gem stand-in — deliberate divergence from the agent's proposal.** The trace
suggested `glassgems[CLR_BLUE]`, matching Vulture's blind-hero convention. That
is wrong here: 5.0.0 emits the generic gem *precisely because the colour is not
established*, so rendering blue glass asserts something the game has not told the
player — and a player who has identified blue glass would read it as "worthless,
skip". Used **white/clear** glass instead. Note the generic entry's own
`oc_color` is `CLR_GRAY` (`GENERIC` macro, objects.h:75) and NetHack has no gray
glass gem, so `glassgems[]` cannot be indexed by it directly.

⭐**Tile-config keys are NOT the C enum names.** They are what `init_objnames()`
generates at runtime, captured in `win/vulture/pathb/nh500_names.tsv`. Validating
against `include/objects.h` gives false failures — objects.h says `GENERIC_RING`,
`GENERIC_POTION`, `GENERIC_SPBOOK`; the runtime names are `RIN_GENERIC_RING`,
`POT_GENERIC_POTION`, `SPE_GENERIC_SPELLBOOK`. My first validator made exactly
this mistake and reported 9 spurious failures; the TSV settled it.
`toolchain/verify_generic_binding.py` now checks against the TSV and passes 17/17.

**Verification status:** D9 verified statically (17/17 slots resolve to real art
in both the map and inventory ranges). D7/D8/D8b compile and are code-verified,
but were **not** exercised in a live session — the user was at the machine and
synthetic input was fighting them for the cursor and foreground. Left for the
user to confirm.

## 28. View QoL, the inert-block incident, and the Steam-bug campaign (2026-07-26)

**Walls/perspective QoL (user request).** Half-height walls SHIPPED ALL ALONG —
44 PNGs, all 11 styles, loaded into walls_half[] and runtime-switchable; the
user's own %APPDATA%\Roaming\Vulture\vulture.conf said wall_style=full and the
USER conf is read after the shipped one and wins. Set half_height + opacity 0.6
at all three layers (user conf, both shipped confs, compiled fallback in
vulture_opt.cpp) and added F7 = live wall toggle in levelwin::handle_keydown_event
(NOT the global SDL hook — that fires inside menus; and no vulture_write_userconfig
from a key handler — it rewrites the whole user conf). Perspective: camera
rotation impossible (sprites are single-viewpoint; only wall FACES have _n/_s/_e/_w
variants), but TAB already opens a flat clickable overview and right-click on the
minimap jumps the camera. ⭐Ctrl+Shift+O is Vulture's in-game options menu; it
persists changes.

**⭐THE INERT-BLOCK INCIDENT (the day's most instructive failure).** My monster
rename fix (§ "gnome lord") was 30 lines of `monster.PM_NEW => monster.PM_OLD`.
Every line was silently DEAD: the conf parser resolves names against tilenames[]
generated at runtime from the RUNNING game's tables (init_monnames over 5.0.0
mons[]), and vulture_redirect_tile() drops any line whose TARGET is not a live
slot (dst == -1). 3.6.6 names like PM_KOBOLD_LORD are not slots in 5.0.0 — only
their PNG files survive. The lines parsed clean, my TEXTUAL validator blessed
them, and the game installed nothing. I told the user it was fixed; it was not.
Fix = REKEY IN PLACE: direct '=' on the live 5.0.0 name carrying the old PNG
path + per-range hotspots verbatim. verify_all_bindings.py now models NAME
LIVENESS (keys and targets checked against nh500_names.tsv) + the one-hop rule +
last-wins. ⭐Any tool that validates config against config, rather than against
what the RUNTIME accepts, will bless dead entries.

**Monster-range repair (32 + 1).** 12 pure renames rekeyed (leaders/rulers/
nobles/monarch/clerics — 5.0.0 degendered titles; pmnames[MALE] is the literal
3.6.6 name on byte-identical stat blocks). Gender merges bound by SLOT RETENTION
(cave dweller→caveman art, cleric→priest art). PM_HEALER was collateral: its
stock redirect targeted removed PM_PRIEST → dead → magenta TODAY (found only by
the liveness resolver). Werecreature statue gaps → werejackal art (byte-identical
sprites, md5-verified). Aleax/angel/archon statues + 4 golem figurines → generic
humanoid statue/figurine art (TRUE-but-underspecified beats magenta).
Non-humanoids (xorn/ki-rin/zruty/lurker/trapper) EXPLICITLY => ERROR — a humanoid
statue would lie about body plan (statue traps!). ⭐DISPLACER BEAST: stock conf
redirected it to PM_LARGE_CAT — dead config in 3.6.6 (no such monster), went
LIVE in 5.0.0: a difficulty-14 M3_DISPLACES nasty drawn as the player's own pet
(isometric view has NO pet marker). Resolves successfully, so no failure-audit
sees it; now => ERROR. Gold dragons stay placeholders (colour IS breath type;
both gold dragons are the game's only emits_light() dragons and this port renders
light — a self-lit "red dragon" would be a phenomenon no red dragon produces;
yellow already OWNS the gold colour value and breathes acid). Statue/figurine
gaps verified PRE-EXISTING UPSTREAM (resolved stock c7d1cd3 conf against
nh366_names.tsv: same 14, same split). ⭐vulture-baseline working tree is
CONTAMINATED (Path B commit retro-applied); diff against git show c7d1cd3:...,
not the checkout. ⭐Latent hazard: FIVE categories carry duplicate <cat>.default
lines (last-wins currently correct); a reorder would silently downgrade unbound
slots to plausible art.

**Amorous demon (the one merge where gender is MECHANICAL).** Seduction fires
only cross-gender (mhitu.c) and messages print the gendered name, so one static
sprite contradicts both ~half the time. Implemented per-instance dispatch:
V_MISC_SUCCUBUS/V_MISC_INCUBUS misc slots (enum + miscnames + conf lines reusing
the retired 3.6.6 art/hotspots), Mgender(m_at(x,y)) in vulture_monster_to_tile;
NULL monster (map memory) falls to the slot tile = conspicuous placeholder.

**Steam-forum bug campaign (user-directed; forums = goldmine).**
- ⭐THE priest bug (Moloch Sanctum/Astral, 3+ threads, one player stuck 20 YEARS,
  another 9-reply thread derived the SAME fix): vulture_monster_to_tile()
  dereferenced EPRI(mtmp)->shralign unguarded; minion priests carry emin NOT
  epri; mtmp can be NULL (map memory). Fixed: has_epri/has_emin/A_NONE fallback.
  Byte-identical in baseline = decade-old stock bug. TWO independent field fixes
  matched mine exactly.
- Hallucination crash (2020+2023 threads): SAME BUG — hallu rolls RANDOM monster
  glyphs, so "aligned priest" lands on squares whose real monster is a non-priest
  (garbage union deref) or a statue (NULL). The guard covers both.
- #enhance slot-eater ("most skilled in no skill", known since Feb 2020):
  menuwin::layout() made EVERY non-empty row a clickable optionwin, ignoring the
  identifier contract (identifier==zeroany rows are display-only). Mouse was the
  vector (keyboard path was safe: assign_accelerators skips identifier-less
  rows). Core decodes NULL identifier → skill -1 → OOB write + slot spend.
  Fixed in menuwin (identifier-less → textwin), matching inventory's
  objheaderwin. Repairs all 111 add_menu_str/heading sites at once.
- Ascension final-score crash (4 threads, 3.4.3→3.6.6, distinct from priest bug:
  quit-at-same-moment works) — under investigation (wf_f15e5d6a).

**Path A all-magenta overview map + "svl.level 1" title (user screenshot).**
Root cause CONFIRMED: sys/windows/GNUmakefile compiled tilemap.o with plain
$(CFLAGSU) — no -DVULTURE_GRAPHICS — so remap_tileidx() compiled as the identity
macro and the generated tile.c carried RAW 5.0.0 indices (9,624 rows, max 2302,
4,975 ≥1200); every terrain tileidx (1272..1514) clamps to placeholder cell 1199
in vulture_patha_cell(). The magenta+X being VISIBLE proved the atlas loaded
(that art lives IN cell 1199) — diagnostic worth remembering. ⭐The obvious
one-flag fix DOES NOT COMPILE: VULTURE_GRAPHICS makes global.h include
vulture_conf.h which lives only in win/vulture/ → the rule also needs
-I$(VULTUREDIR) (verifier caught this empirically; sole side effect is the
pre-existing benign 'DLB redefined' warning). Residual fixes in map.cpp: skip
GLYPH_UNEXPLORED/GLYPH_NOTHING (their remap IS placeholder) and substitute
S_darkroom (remap 850) for seen-dark stone. Statues on the overview map remain
placeholder BY DESIGN (raw 1515..2302 outside the 1515-entry table; same in the
WSL-validated build); ⭐~1,532 stderr placeholder lines during tile.c regen are
EXPECTED, not failure. Sed sweep: exactly ONE string-literal corruption (the
map title sprintf) + 19 comment-only tokens across 5 files; config/gamedata/
package clean. Core src/ NOT swept — if a special-level title ever reads oddly,
run the same grep there.

## 29. Ending screens, derived art, boulders, and the intro video (2026-07-26)

**Endings (ascension workflow).** The historical Steam ascension crash could NOT
be pinned to any statement in this tree — every ascension-only candidate ruled
out, and two Steam-era crash classes were already dead via this port's guards
(WIN_ERR window lookups; identifier-less menu rows). The decisive experiment is
a wizard-mode ascension (documented in the workflow result). What WAS found and
fixed, all stock 3.6.6-identical: ⭐ending_type off-by-one — vulture_outrip
stored raw `how` while the consumers gate on >0 and switch on (ending_type-1),
so DIED (how==0) NEVER SHOWED AN ENDING SCREEN in stock Vulture and ASCENDED
drew quitgame.png instead of night.png. Store how+1. Also fixed:
recover_savefile negative-count buffer smash (the surviving "crash again with
the same name" path), map_symbols[] NULL-init on atlas-load failure, an
uninitialised hover buffer, ⭐vulture_write_userconfig now LOSSLESS (preserves
comments/unknown lines — was a whole Steam complaint thread), and the user
sounds conf now parsed by the correct reader. That workflow's agents applied
their own patches to disk and compile-verified each TU — future sessions should
expect workflow results phrased as "applied", not "proposed".

**Derived art (user hit two triaged placeholders in play).** SPE_NOVEL ("The
Truth" — the Discworld tribute item, 3.6.0+, newer than Vulture's art) and the
four GLOB_OF_* now have real tiles: toolchain/derive_missing_art.py does
luminance-preserving recolors of existing painted tiles (novel <- blank book in
paperback tan, a colour no book uses; globs <- royal-jelly lump per species
colour). 10 PNGs into both tile dirs; conf rekeyed from => ERROR to direct
definitions. Validator: 11 triaged placeholders, 0 untriaged.

**Boulder click-push regression (user report).** get_map_action() classifies an
adjacent object square as TRAVEL (to suppress the object-list popup) — but a
boulder is interacted with by WALKING INTO it, and 5.0.0's travel refuses to
path into an occupied square. Under the old M-^P bug all clicks failed equally,
so this surfaced only after travel started working. Fix: adjacent boulder →
V_ACTION_MOVE_HERE (+ explicit "Push boulder" context-menu item), via
sobj_at(BOULDER,…).

**Dark Arts intro video re-enabled.** The whole SDL1.2 player sat in one giant
/* XXX Remove Video */ comment (⭐which is why no preprocessor-guard grep found
it, and why theoraplay.o + libtheoradec shipped as dead weight all along).
Ported to SDL2: THEORAPLAY_VIDFMT_RGBA → SDL_CreateRGBSurfaceWithFormatFrom
(RGBA32) → BLENDMODE_NONE → SDL_BlitScaled aspect-fit letterbox →
vulture_refresh(). Original frame-pacing tail preserved; ⭐framems was declared
but never assigned in the SDL1 code (dead frame-dropper) — now set from
video->fps. Verifier catches folded in: silent-audio NULL deref demoted to
silent playback; audio pre-roll BOUNDED (~2s) so a corrupt ogv cannot spin.
Skip = any key/click/QUIT. New option play_intro_video (default 1), wired
through parser/defaults/writer/known_keys and both confs. SDL_INIT_AUDIO
brought up explicitly; SDL_CloseAudio on every exit path (SDL2_mixer opens the
device right after).

**Overview map (Tab) enlarged + `map_zoom`.** The parchment is a fixed 640x480
painting, so on a 1920x1080 screen the level overview was a postage stamp. The
class already derived draw() and the mouse inverse from a captured `ui_scale`,
so the map now picks its OWN integer zoom and builds ALL its art at it
(parchment via vulture_load_graphic + map_scale_surface, symbols likewise);
every ctor offset moved off the global V_UI() macro, which would otherwise
silently disagree with the art the moment the two scales differ.

The zoom is fitted BELOW the message band, not centred on the whole screen:
messagewin anchors to the top and grows to `messagelines` lines, and burying
the "what just happened" text made the map unusable to play with (user report).
`map_message_band()` is shared by the ctor and the resize handler so they
cannot drift. New option `map_zoom` (0 = auto, 1..4 = forced) wired through
parser/defaults/writer/known_keys; always clamped to what fits. Verified
in-game at both auto (2x = 1280x960, all 5 message lines clear) and 1.

(*) Tried keying out the symbol atlas's opaque grey backdrop so glyphs sat
directly on the parchment - REVERTED, user wants the backdrop. It is authentic:
the atlas is a terminal screenshot, alpha 255 everywhere, and neither this port
nor the SDL1.2 original ever keyed it.

**Gold dragons + LIVELOG.** LIVELOG=0x1FFF added to the LIVE sysconf
(ProgramData/NetHack/5.0/sysconf - the package copy is not read); confirmed
real entries (Gnomish Mines, xp level, (*)"entered a shop", which also retires
the never-tested-shops risk). Gold dragon art derived from the silver donor by
luminance-preserving recolour.
(*) The STATUE was initially recoloured too and that was WRONG: every dragon
statue in Vulture is stone (red/blue/silver all measure ~0.05-0.07 mean
saturation) and only the pose differs per species, so a gold statue would have
been the game's single metallic one - plausible-but-incorrect art. Statues are
now VERBATIM copies of the silver stone render; only the monster (colour = its
breath type) and the figurine are recoloured. Validator: 481/481 objects bound,
7 triaged placeholders, 0 untriaged.

(*) FOLLOW-UP, same session: the message-band placement above was silently
UNDONE by `map::toggle()`, which re-centred x/y on the full screen right after
construction. window.cpp:221-224 recomputes abs_x/abs_y from x/y on every walk,
so the constructor's value lost every time and the map opened msg_h/2 (~38px)
too high - still burying the newest message line. Placement now lives ONLY in
the constructor. Measured before/after: parchment top edge y=72 -> y=110, a
38px shift exactly matching msg_h/2. My first verification missed this because
the screenshot happened to show only 4 message lines; msg_h is derived from the
`messagelines` OPTION (always 5 here), not from the current message count, so
the map's y is constant and the bug was invisible at <5 lines.

**Explosions rendered as zap beams — FIXED (mapdata.cpp:309-330).** 5.0.0
reordered the core glyph blocks; 3.6.6 had EXPLODE < ZAP < SWALLOW < WARNING,
5.0.0 has ZAP < CMAP_C < SWALLOW < EXPLODE < WARNING. The two hand-rolled range
tests (identical to the 3.6.6 baseline, so this was pure version drift, not a
porting slip) therefore broke in opposite directions:
  - `glyph >= GLYPH_EXPLODE_OFF && glyph < GLYPH_ZAP_OFF` became UNSATISFIABLE
    - all 63 explosion glyphs / 27 shipped PNGs were dead code.
  - the zap arm's `< GLYPH_WARNING_OFF` over-captured CMAP_C + SWALLOW +
    EXPLODE and painted every one of them as `S_vbeam` beam art.
Now uses the core's own classifiers (`glyph_is_cmap_zap` / `glyph_is_explosion`
/ `glyph_is_cmap_c`) so it cannot drift again.

Proved with a throwaway C program compiled against the real headers
(`-I include -I sys/windows -DTILES`), not by inspection:
    order:  ZAP=4051 CMAP_C=4083 SWALLOW=4093 EXPLODE=7157 WARNING=7220
    OLD explosion branch matched    0 glyphs  (provably dead)
    OLD zap branch matched       3169 glyphs  (only 32 are zaps)
    NEW zap 32, NEW explosion 63, overlap/range violations 0
Tile layout re-checked against the conf: groups run DARK, NOXIOUS, MUDDY, WET,
MAGICAL, FIERY, FROSTY x9 = 63 + ERROR = 64 = EXPTILECOUNT (EXPL_MAX*9+1), and
the type-major order matches the core enum, so `EXPTILEOFFSET + (glyph -
GLYPH_EXPLODE_OFF)` indexes correctly and cannot pass the reserved slot.

Two deliberate consequences, NEITHER runtime-tested (no explosion or
engulfment was reproduced - `-D` exits immediately on this build, so wizard
genesis was not reachable):
  - CMAP_C (dig beam, camera flash, boomerang, magic resistance) now reaches
    the third arm and draws its proper misc tiles instead of a beam. Clear fix.
  - SWALLOW no longer gets a beam overlay; `map_special` stays V_TILE_NONE.
    3.6.6 DID capture swallow in the zap arm, but only as an accident of glyph
    ordering (the comment describes only the cmap-C symbols), and beams during
    engulfment are wrong art by this project's own rule. NOTE: Vulture ships
    `misc.ENGULF_*` art and a `V_MISC_ENGULF_*` enum that NO C++ references -
    dead in 3.6.6 too. Wiring it up is the honest follow-up.

(*) PRE-EXISTING, not introduced here: only MAGICAL, FIERY and FROSTY have real
explosion art; DARK, NOXIOUS, MUDDY and WET all `=>` redirect to FIERY. A gas
spore's noxious blast therefore renders as a fiery one. Upstream Vulture's own
aliasing, single-hop and legal, but it is a type substitution and a candidate
for an honest recolour (cf. make_gold_dragons.py).

**Missing tile art crashed instead of placeholding - FIXED (vulture_tile.cpp).**
Gamedata ships as 2419 loose PNGs with no DLB, so one absent or truncated file
was fatal rather than conspicuous. Three holes, all on constantly-hit paths:
`vulture_shade_tile()` dereferenced `orig->xmod` / `orig->graphic->w` unchecked
(EVERY dark square goes through it); `vulture_load_tile()` never checked
`vulture_load_surface()`, so a corrupt PNG yielded a tile with `graphic == NULL`
- not a NULL tile, so callers' NULL checks did not catch it; and
`vulture_put_tilehighlight()` dereferenced `tile->graphic->format` unchecked.

Now `vulture_get_tile_shaded()` substitutes the reserved ERROR placeholder for
anything that will not load, so missing art is VISIBLE rather than an invisible
hole. `vulture_error_tile_for()` picks the placeholder of the tile's OWN
category (each fixed-size category reserves its last slot) so hotspots are
right; edge/floor/wall are variable-size with no known slot and fall back to
misc. A placeholder cannot stand in for itself, which ends the recursion. A
fresh copy is loaded per cache slot - one pointer under several ids would let
`vulture_tilecache_discard()` free it twice. Warn-once-per-tile via a static
std::set (GAMETILECOUNT is a RUNTIME value - `WALTILECOUNT` reads
`vulture_typecount[]` - so a fixed array does not compile). Also:
`vulture_free_tile()` NULL-tolerant, `delete data` -> `delete[] data`.

(*) VERIFIED BY INJECTING THE FAULT, not by inspection: pulled all 18
`floor.floor_rough_*` tiles out of the package and played. Game starts, the
walked corridor renders as bright magenta/black hazard placeholders correctly
anchored in the isometric grid, 9 distinct tiles substituted, each logged
EXACTLY once, fully playable throughout. Tiles restored and confirmed
byte-identical to the tracked gamedata master (2419/2419); a clean run then
showed zero placeholder pixels and zero substitutions.

**Version control.** `C:/Users/dalla/Vulture` is now a git repo (5b3dcd9
baseline, 4899 files). Six nested repos (refs/*, vulture-baseline, win-build)
are excluded, as are the regenerable VultureNetHack/ package, shots/, hearse/,
the 91MB msys2 installer, and all local runtime state that carries the player's
name/email/saves. NOTE: `VultureNetHack/tiles` is NOT tracked - the tracked
master is `vulture5-win/win/vulture/gamedata/tiles`, and the two are currently
kept in sync BY HAND.

**No permadeath ("RPG mode") - vulture.conf no_permadeath.** User request:
"i like nethack as an RPG game... remove the permadeath somehow. like, when i
die, force quit the program and relaunch it or something?" The literal
force-quit+recover design was REJECTED on evidence: with checkpointing on,
the .0 state file is rewritten continuously (observed: same-second mtimes on
.0 + current-level file), so crash-recovery would resurrect the player ~one
turn before death - low HP, killer adjacent, a death loop, not a save reload.

Instead the existing explore/wizard "Die? [yn]" second chance (end.c done(),
savelife()) is extended to normal play: the branch gate becomes
(wizard || discover || vulture_no_permadeath()). Default answer n = survive
via savelife() (partial heal, 50+10*CON/2 HP, cures the killing condition,
expels from engulfers, pulls out of lava); y = normal permadeath with bones,
topten and Hearse. QUIT/ESCAPED/ASCENDED unaffected (how <= GENOCIDED=10;
enum verified). Life-saving amulets still fire FIRST and are consumed.

Plumbing: no_permadeath wired at the usual 5 points in vulture_opt.{h,cpp} +
extern "C" accessor; end.c sees it via -DVULTURE_GRAPHICS (already on ALL
NetHackV core objects via CFLAGSV - verified, so tty builds are untouched:
they get a 0 macro). Linkage PROVEN: end.o shows U vulture_no_permadeath,
vulture.o shows T, link rc=0. Set =1 in the live conf; shipped conf
documents it and defaults 0. NOT runtime-tested - the user asked that the
game no longer be launched for testing; first real death will be the test.
The branch is byte-parallel to explore mode's, which is battle-tested.

(*) BUILD-ORDER FIX uncovered by the clean rebuild: vulture_sdl.cpp includes
date.h (build date in the window title) but 5.0 core stopped using date.h, so
NOTHING generated it on a fresh checkout. $(VULTUREOBJ) now depends on
$(INCL)/date.h.

(*) (*) GITIGNORE INCIDENT (separate commit 0237548): the baseline commit had
silently EXCLUDED src/bones.c and gamedata/config/vulture.conf - git on
Windows matches .gitignore CASE-INSENSITIVELY (core.ignorecase=true), so
bon[A-Z]* matched boneS.c and unanchored vulture.conf matched the tracked
master. Discovered only because the first from-scratch build after the tree
slim-down failed on the missing file; recovered from the WSL tree
(/root/port/vulture5 - bones.c byte-identical to upstream) and the shipped
package copy. Full 7147-file WSL-inventory x check-ignore sweep found no
other source casualties. LESSONS: (1) never use [A-Z] classes or unanchored
runtime-state names in .gitignore on Windows; (2) "N files committed" is not
a completeness proof - only a from-scratch BUILD is; (3) the WSL tree at
/root/port/{vulture5,nethack-5.0.0} remains a recovery source.

(*) ADVERSARIAL REVIEW of the no_permadeath patch (3 lenses, pre-commit)
found 1 blocker + 2 real issues; fixes applied before commit:
  - BLOCKER (docs): %APPDATA%\Vultureulture.conf is rewritten on every
    exit and read LAST, so it silently overrides the game-dir conf. A player
    enabling no_permadeath only in the game-dir file would get an unexpected
    PERMANENT death. Fixed with explicit warnings in the shipped conf + the
    end.c header comment naming the winning file. (Shadowing is pre-existing
    for all options; only the stakes are new.)
  - GENOCIDED was survivable in normal play - STRONGER than the Amulet of
    Life Saving, which refuses that death. Now excluded from the normal-play
    offer (wizard/explore keep their historical behaviour).
  - The new $(INCL)/date.h prerequisite armed a makedefs -v double-fire race
    under make -j (two independent targets sharing one recipe). Rule is now
    grouped (&:, GNU make 4.3+; toolchain has 4.4). Latent only - all
    toolchain builds are serial.
  Accepted as-is (precedented upstream): amulet fires before the free prompt
  and is consumed; the prefix parser routes degenerate keys like 'no=1'.

## 30. Play-report fixes: clouds, stray keys, grid menus, ghosting (2026-11-09)

All from user play reports; each compiled clean (MSYS2 MINGW64), shipped in
the 2026-11-09 NetHackV.exe, NOT yet confirmed in play. Previous exe kept as
VultureNetHack\NetHackV.exe.bak-2026-11-09.

**Stinking clouds drew as unlit floor** (mapdata.cpp). The overlay test
looked only for S_cloud. 5.0.0's create_gas_cloud() (region.c) uses
S_poisoncloud for the damaging kind and S_cloud only for harmless vapour,
so stinking clouds never got misc.STINKING_CLOUD and, since gas blocks
sight, read as fog of war. Test now accepts both. (Harmless vapour shares
the art - cosmetic.)

**⭐Non-character keys answered dialogs** (choicedialog.cpp, menuwin.cpp,
inventory.cpp). vulture_keysym_to_char() returns 0 for numpad, arrows,
Shift/Ctrl/Alt, F-keys; every child window has group_accelerator '\0', so
find_accel(0) matched EVERYTHING. In a choicedialog a held/repeating numpad
key "answered" with '\0', which the core reads as neither 'n' nor 'q':
autounlock's "Check this door for a trap?" took it as YES (search, no
unlock) and "Unlock it with your key?" as NO - the user's "pressing no
boots me out". In menus/grid, a stray Shift toggled every item or picked
the first in PICK_ONE. All three lookups now ignore key 0 (anykeydialog
already did). vulture_yn_function() also flushes queued SDL_KEYDOWNs before
a choice dialog, so map-movement auto-repeat can't answer it. Config side:
all character rcs now use autounlock:apply-key (untrap dropped) - one
prompt per locked door/box.

**Blank grid cells** (inventory.cpp). Unselectable rows with empty text are
spacers (5.0.0's reroll menu adds two); the grid gave each a full empty
cell. Skipped; layout counts created cells, so nothing else changes.

**Unselectable item rows had no icon** (objheaderwin.{h,cpp},
inventory.cpp). Every identifier-0 row became a centred heading. Rows that
carry an object glyph (reroll menu's starting inventory) now draw like an
item cell - icon via vulture_object_to_tile(glyph_to_obj()), which keeps
the unidentified-appearance shuffle - but stay unselectable. No BUC
tint/weight/letter: those need a struct obj, which these rows lack.

**Ghosting / garbled text - BEST GUESS** (window.cpp). ~window() and hide()
restored the saved background and refreshed it, but never invalidated the
area, so nothing beneath repainted it. The snapshot is from when the popup
opened; anything that changed while it was up stayed stale, and the
translucent message window drew over - and saved as its own background -
the stale pixels (old text through new). Both now vulture_invalidate_region
the restored rect. Ruled out first: cursor/tooltip (drawn last, wiped
before any window draws), levelwin (repaints the whole viewport every pass
- the else-branch min/max widens the clip; unmapped/off-map diamonds tile
the 112x44 grid gap-free, verified by simulating the real alpha mask),
statuswin/toolbar (opaque repaint). If ghosting persists, get an F12
screenshot - it saves vulture_screen itself.

**Still open** (not touched): S_trapped_door / S_trapped_chest /
S_vibrating_square / S_lavawall placeholders; vulture_getmsghistory /
putmsghistory are //TODO stubs (Ctrl+P history lost on restore);
SCALE_UI_DEFECTS.md inventory layout at render_scale=2; extmenu ignored.

## 31. Engraving decal (2026-11-10)

User report: 5.0.0 draws engravings (S_engroom/S_engrcorr) but Vulture showed
plain floor - §D6 had bound both symbols to the floor tiles for 3.6.6 parity,
so the cmap_gap audit counted them "bound" and never flagged them.

- New misc tile `V_MISC_ENGRAVING` / conf `misc.ENGRAVING` (tiles/misc.engraving.png,
  65x28, offset -32 -14): three carved runes + scratch, projected onto the
  109x44 floor diamond. Added to BOTH gamedata/ and the shipped VultureNetHack/
  config + tiles.
- Drawn on the TRAP layer (pass 3: over furniture, under objects), never in
  map_back, so the cobblestone/corridor stays visible. Traps outrank
  engravings in the core's display.c, and a seen trap still wins here.
- Seen squares: `vulture_engraving_tile()` mirrors display.c -
  `engr_at() && erevealed && !covers_traps()`. Remembered squares: taken from
  the memory glyph's cmap, so it matches what the core remembers (a wiped
  engraving stays until re-seen, as in tty).
- S_engroom/S_engrcorr stay mapped to the floor tiles: that is the map_back.
- The enum insert shifts every tile id after V_MISC_INCUBUS; verified all 35
  objects that include vulture_tile.h rebuilt (the 12 that did not have no
  dependency on it). verify_all_bindings: PASS-WITH-PLACEHOLDERS, unchanged.
- Not ported to VultureNetHack-Crafting (separate build).

## 32. Trapped door / trapped chest marker (2026-11-10)

S_trapped_door / S_trapped_chest (open since §30) rendered the magenta X.
They are DUMMY traps (TRAPPED_DOOR/TRAPPED_CHEST in detect.c): trap
detection, the crystal ball and searching call sense_trap(), which paints
the "^" glyph, but t_at() finds nothing - the glyph is the only record.

- Remembered squares: the glyph_is_trap arm drew vulture_tilemap_misc[cmap]
  = V_MISC_ERROR. Now: no floor trap; keep any remembered box (map_obj);
  redraw a door we have actually seen (seenv, from back_to_glyph, the same
  as the restore fix-up does); put the marker on the effects layer.
- Seen squares: the seen arm resolved traps only via t_at(), so a trapped
  door found by searching right next to you showed NO warning at all.
  Now the marker goes on map_special (after the monster block, so a stale
  monster is never kept under it). As in tty, it lasts until the core
  redraws the square (newsym() puts the plain door/box back).
- Art: tiles/misc.trapped_marker.png, 21x19 orange (CLR_ORANGE) warning
  triangle. Two conf entries share it at different offsets, so it sits
  on the door face (-10 -52) or the box face (-10 -32).
- Both cmap entries bound (no table lookup can reach ERROR). cmap_gap now
  lists only S_lavawall, S_vibrating_square, S_goodpos (+ the sw/expl
  runs, which never index this table). verify_all_bindings unchanged.
- Also cast the §31 engraving ternary to int (-Wenum-compare).

## 33. `bestroll` option: best-possible Tourist start (2026-11-10)

User request: always start with the best Tourist kit and stats. New
boolean option `bestroll` (optlist.h, set_in_config, stored in iflags -
iflags is never written to a save, so save files are unaffected). Gated
by bestroll_active() = iflags.bestroll && Role_if(PM_TOURIST); every
other role rolls normally. Each value is the ceiling of a roll the stock
game makes, so nothing is impossible in an ordinary game:
- trquan() returns trquan_max: 40 darts (21-40).
- UNDEF_BLESS items blessed (darts, potions, scrolls, shirt).
- Random food: LEFT RANDOM (2026-11-11). It was 10 x lembas wafer at mksobj()'s
  1-in-6 pair = 20 lembas; the user found that overpowered (food stops mattering).
- Camera 99 charges (rn1(70,30)); gold 1000 (rnd(1000)).
- Extra item: magic marker (else-if chain's last, 1-in-20 slot) at
  19+3 = 22 charges.
- Stats: bestroll_attrs() = init_attr(75) + vary_init_attr() at their
  ceiling - fill priority stats to cap-4, let vary's +4 finish them.
  Tourist priority Con, Dex, Cha, Str, Int, Wis gives
  Str 18/03 Int 14 Wis 10 Dex 18 Con 18 Cha 18.
Enabled in kilmer.nethackrc (backup: NetHack/backup-2026-11-09/
kilmer.nethackrc.pre-bestroll-2026-11-10).

TRAP: the core makefile has NO header dependency tracking. Touching
flag.h relinked 138 stale objects against the new struct instance_flags
layout. Stale objects were moved aside (scratch dir) and the core rebuilt
clean: o/nethackv (139 objs) + o/hacklib. craft.o (the Crafting mod's
leftover object, not part of NetHackV.exe) was put back untouched. After
ANY include/*.h edit, check `find src/o/nethackv -name '*.o' ! -newer
include/<file>.h` before trusting the build.

## 34. `autoid` option: price, sink-ring and wand-engrave identification (2026-11-10)

New boolean option `autoid` (optlist.h, set_in_game, iflags - not saved).
Everything goes through autoid_call(otyp, label, why) in do_name.c, which
#calls the type the way docall() does (oc_uname + discover_object) and
prints "You make a note from <why>: <type>.". It never touches a
formally identified type or a name the player chose: an existing name
is replaced only if it is one of our five group labels
(autoid_group_labels[]), and never by a wider group.

- Prices (shk.c): only min/max buy and sell quotes per type exist
  (record_price_quote). priceid_candidates() tests every unknown type in
  the item's obj_shuffle_range() group against ALL of them, mirroring
  get_cost()/set_cost() rounding: unID 4/3, dunce/tourist 4/3, Cha table,
  angry +1/3; sell /3 or /2, x3/4 shk, x9/10 credit, per-unit integer
  division over stack sizes 1-40. Fallbacks: drop the low sell quote
  (short-funds offer), then sell entirely. Armor allows +10/enchant.
  Uses the hero's CURRENT Cha/look, so quotes from a different Cha can
  stop fitting -> no note (never a guess). objnam.c appends " [light]" /
  " [a/b/c]" / " [100: 7 kinds]" (needs dknown). autoid_price_pass()
  runs once per input (allmain.c) and names single-candidate types that
  are oc_encountered (no leak from quotes seen blind).
- Sinks (do.c dosinkring): with eyes and no hallucination every ring
  has its own message (all 28 RIN_ ids have a case; hunger's is
  silence), except "The sink vanishes." = teleportation or polymorph.
  Named before trycall(), so the #call prompt no longer appears.
- Engraving (engrave.c): labels set per wand case only when the message
  is actually shown; applied where post_engr_text prints (the message
  is lost if the player escapes the text prompt). "engraving vanishes"
  is shown at once, so it names at once. No message at all: "no engrave
  message" (an engraving was here) or "... - retry on an engraving"
  (cancellation/make invisible/teleportation/polymorph also silent).
  Self-identifying wands are oc_name_known by then and are skipped.
- Offline check (Python mirror of the price code) on a real inventory:
  50 -> light, 20 -> identify, 60 -> enchant weapon, 80 -> enchant
  armor/remove curse, sell-only 67 -> 8 kinds (200, or 300 as credit).
- Enabled in all eight character rc files (backups in
  NetHack/backup-2026-11-10/), not in the near-stock .nethackrc.

Build incident: the session was cut off mid-build. include/date.h was
left as 664 bytes of padding but newer than makedefs, so make would not
regenerate it and vulture_sdl.cpp failed on VERSION_ID. Fix: move
date.h aside (and the half-written vulture_gen.o) and rebuild. Installed
23:17; previous exe = NetHackV.exe.bak-2026-11-10.

## 35. Mouse cursor ghosting (2026-11-18)

User report: the mouse cursor leaves ghost images behind. Not the §30
window-ghosting guess (that is popups restoring stale backgrounds); this one
is the software cursor itself (vulture_mou.cpp).

**Cause.** The cursor is painted into vulture_screen, its rect pushed to the
display, then the saved background goes straight back into vulture_screen,
so afterwards the cursor pixels exist ONLY on the display. The next
vulture_mouse_refresh() had to erase them, but it picked the rect from
`vulture_mouse.prev + prevcursor` - the position before the LATEST
vulture_set_mouse_pos() and the shape before the LATEST set_mcursor(). That
is the last DRAWN rect only if each changes at most once between draws.
They change more often: mouse motion during vulture_wait_key() (--More--)
updates cur/prev with no draw; vulture_curs() warps the pointer in
look/teleport; a MOUSEMOVEOUT + MOUSEMOTION pair can each set a cursor. The
true last rect was never refreshed, so a cursor image stayed on the display
until a window happened to repaint there (soon on the map, never over static
UI). The two right-Ctrl object-highlight paths (vulture_sdl.cpp) also pushed
the cursor via vulture_refresh_window_region() and skipped
vulture_mouse_refresh() altogether.

**Fix.** Rects are now recorded, never re-derived: `bg_rect` (where the
background was read; restore writes it back exactly there), `drawn_rect`
(the latest paint) and `shown_rect` (what refresh last pushed). Refresh =
refresh(shown) + refresh(drawn), then shown = drawn. A draw that never
reached refresh is folded into shown_rect so it still gets erased. Both
highlight paths now call vulture_mouse_refresh(). set_mcursor's own
previous-shape refresh is left as is (harmless, now redundant).

Built clean (no new warnings; GUI subsystem). Installed by swapping
binary/NetHackV.exe over VultureNetHack/NetHackV.exe once the running game
quit; previous exe = NetHackV.exe.bak-2026-11-18. NOT yet confirmed in play.
Not ported to VultureNetHack-Crafting (separate build).

## 36. One-step armor swap on W (2026-11-18)

User request: wearing a new helm over an old one should not need a manual
T first. do_wear.c:

- `wear_swap_blocker(otmp)` returns the worn piece in otmp's own slot when
  that piece is the ONLY obstacle: it empties the slot pointer for one
  quiet canwearobj() call and restores it, so every other refusal (2-hander
  vs shield, welded weapon vs gloves, horns, trapped feet, polyform) still
  wins with its stock message. Same slot only - a suit under a cloak or a
  shirt under suit/cloak still needs the outer layer off by hand. uskin
  never swaps.
- accessory_or_armor_on(): if there is a blocker, take it off through
  armor_or_accessory_off() (stock cursed check, cockatrice-glove prompt,
  delay, messages), then queue `dowear` + the new item's invlet on
  CQ_CANNED. Same turn cost as by hand.
- ⭐Why not chain cloak+suit: armoroff() with a delay calls nomul(), and
  nomul() clears CQ_CANNED. So the queue must be added AFTER the takeoff
  starts, and only ONE follow-up step survives. Anything that interrupts
  the takeoff (stop_occupation, teleport) clears the queue: old piece off,
  new one unworn - safe.
- equip_ok(): a swappable piece is GETOBJ_SUGGEST (listed in the W prompt)
  instead of DOWNPLAY. dowear(): "full complement of armor" only when
  nothing unworn could swap in.

No save-format change. Built clean. NOT yet confirmed in play.

## 37. Tourist photo album: #photos (2026-11-18)

User request: a Tourist should automatically record which species they
have photographed. 5.0.0 ALREADY records it - svm.mvitals[].photographed,
set by see_monster_closeup() (mon.c) on a camera flash, saved with the game
(it also drives the Tourist's first-photo EXP bonus) - but nothing ever
displayed it. So, no save change, and games started before the mod keep
their photos:

- mon.c: a Tourist gets "You add a <species> to your photo album." the
  first time each species is photographed (same rules as the record:
  no hallucination, invisible/hidden monsters don't count, a mimic posing
  as a monster records the pose).
- insight.c `dophotos()` + cmd.c `#photos` (alphabetical slot after
  perminv; extern in extern.h): text window "Photo album: N species", then
  species names in monster-index order (groups by class). Works for any
  role. Vulture's # opens extcmd_via_menu(), so it appears in that list.
- extern.h only gained a prototype - no struct change, so the missing
  header deps in the core makefile (see memory) don't matter here.

Built clean together with §36 (21:46). Installed once the game quit;
previous exe (the §35 cursor build) = NetHackV.exe.bak-2026-11-18-cursorfix.
NOT yet confirmed in play.

## 38-42. QOL batch (2026-11-18)

User approved a list of eight; three turned out to exist already (5.0 or
earlier mods), so they were extended or switched on instead of rebuilt.
All built clean together (22:01), installed by the same quit-watcher (it
copies whatever binary/NetHackV.exe is at exit, so it shipped this build,
not the §36-37 one). No save-format change anywhere. NOT yet confirmed in
play.

**§38 Corpse freshness tag** (objnam.c doname_base). Corpses read
"(fresh)" / "(getting old)" / "(dangerously old)" / "(never rots)".
Mirrors eatcorpse(): rotted = age/(10+rn2(20)) (+2 cursed, -2 blessed),
>3 can sicken, >5 = tainted. The label uses the WORST divisor (10), so
"fresh" (rot<=3) is a promise and "dangerously old" (rot>=6) means "could
be tainted". BUC shifts it only when bknown. Age via
peek_at_iced_corpse_age(); never-rot list copied from eat.c's
nonrotting_corpse() (lizard, lichen, Riders, acid blob). Hidden while
hallucinating. The 1-in-7 random "rotten" roll on any corpse is not age
based and is not covered by "fresh".

**§39 #enhance** (weapon.c). Confirm "Spend N skill slot(s) on <skill>?
(Can't be undone.)" before skill_advance(); "no" re-shows the menu
(`reshow` joins the do/while condition). Skipped in wizard speedy mode.
Skill-ready message now names the skill: "You feel more confident in your
dart skill.  (#enhance to advance)". (Vulture's enhance icon already
existed - enhancebutton.cpp.)

**§40 Thrown missiles with autopickup off** (pickup.c). All 8 rc files
have autopickup ON, so pickup_thrown already worked; but '@' (toggle off)
made pickup() return before autopick(). Now, with '@' off and
pickup_thrown on and something the hero threw underfoot, it still
autopicks - only LOST_THROWN objects (file-static autopick_thrown_only,
checked first in autopick_testobj, set/cleared around the autopick()
call). **Auto-quiver**: stock option, just enabled - `OPTIONS=autoquiver`
appended to all 8 character rcs (backups NetHack/backup-2026-11-18/).
autoquiver() prefers launcher ammo, then darts/shuriken, then daggers;
addinv() also quivers picked-up missiles when the quiver is empty.

**§41 Cloak + suit swap** (do_wear.c). W on a suit while a cloak is on:
if the suit would fit (or swap) with the cloak gone, take the cloak off,
remember it (static swap_cloak_oid + turn stamp, not saved), queue W+suit.
Re-entry swaps the old suit out (§36), the next re-entry dons the suit,
and AFTER its nomul() queues W+cloak (only if the cloak is carried, unworn,
and the record is <100 turns old). Shirt-under-suit is still manual. A
cursed old suit stops the chain with the cloak off.

**§42 #stash** (dungeon.c dostash, cmd.c, extern.h). Prompts "What did you
stash here?" and APPENDS "note (on <dfeature>)" to the level's
mapseen->custom (#annotate text), "; "-separated, capped at BUFSZ - so
#overview (^O) lists stashes per level. Same saved string as #annotate.

**Already present, no code:** shop price memory = §34 autoid (records
min/max quotes per type, tags names [a/b/c] / [100: 7 kinds]).

## 43-49. QOL batch 2 (2026-11-18)

Built clean together (22:15; only the two pre-existing vulture_main.cpp
warnings at the delay loop). No save-format change - every new piece of
state is static or lives in already-saved fields. Installed by the
quit-watcher; previous exe = NetHackV.exe.bak-2026-11-18-qol2. NOT yet
confirmed in play. Sources before this batch: scratchpad tarball only.

**§43 Message history across save/restore** (vulture_main.cpp,
messagewin.{h,cpp}). vulture_get/putmsghistory were //TODO stubs, so the
log came back empty after every reload. Modelled on tty: get hands out the
newest 200 (V_SAVED_MSGHIST) oldest-first from a static snapshot; put sets
this session's messages aside on the first restoring call and re-adds them
after the NULL terminator. messagewin gained history(max) / clear_history();
add_message() never stores age 0 (0 = empty slot). Saves written by older
exes carry no history - that's expected, not a bug.

**§44 Typed '#' commands** (vulture_main.cpp vulture_get_ext_cmd,
vulture_win.{h,cpp} vulture_get_input_completing, inputdialog.{h,cpp}).
Unless `extmenu` is set: a text box; Tab completes to the longest common
prefix; the headline shows live matches ("#: photos, pickup, pray, ...")
cut to the dialog width. Enter: exact name or unique prefix runs it,
ambiguous prefix -> menu of just those matches, empty -> the old full
menu. Uses extcmds_match(ECM_IGNOREAC|ECM_NO1CHARCMD); its result list is
static, so the ambiguous case copies it before building the menu.

**§45 Low-HP warning + quiver readout** (allmain.c vulture_qol_pass,
statuswin.cpp). ⭐Correction: the HP number ALREADY changed colour (green/
yellow/orange/red at 90/70/50/25%) - added only the one-time message "You
feel dangerously weak!  (HP n of m)" below 1/3 (or <=5 when max>10), which
also stops an occupation; re-armed above 1/2. Status bar: bottom-left cell
{0,4} shows "Qv:23 darts" (yellow at <=5); conditions now use 7 cells (the
stock 9th wrote over the alignment). Throwing doesn't set disp.botl, so
vulture_qol_pass flags the status line when the quiver object/count changes.

**§46 Pet info on hover** (mapdata.cpp map_square_description). A visible
pet adds " (HP 12/15, hungry)"; hunger from EDOG hungrytime +300 hungry /
+500 starving (dogmove.c DOG_HUNGRY / DOG_WEAK). Not while hallucinating.

**§47 Monster know-how** (pager.c monster_lore, do_look). After a ';' /
click-look on a monster the hero really sees (canspotmon, not disguised,
not hallucinating): "You recall: hitting it can paralyze you, poisonous
to eat." Built from the attack table + flags (touch_petrifies, passive
AD_*, AT_ENGL/BREA/GAZE/MAGC/BOOM, steal/drain/teleport/etc., acidic,
poisonous, regenerates), max 6 phrases, deduped. Exported (extern.h) so a
future hover tooltip can reuse it.

**§48 Item weights - REVERTED same evening.** Vulture's own inventory
window already shows weights, so the "(wt N)" suffix was a duplicate;
invent.c is back to stock (byte-identical to before the batch).

**§49 Auto-explore** (cmd.c doautoexplore, key `'` / #autoexplore;
allmain.c chain). BFS from the hero over KNOWN walkable squares, same
neighbour rule as findtravelpath (test_move TEST_TRAV, seenv or couldsee,
no known trap via TEST_TRAP, no boulder), to the nearest square touching a
GLYPH_UNEXPLORED square; then dotravel_target() (u.tx/ty set too). So
walking, door opening and interruptions are stock travel. Chain: in
vulture_qol_pass, once travel fully stops, re-queue doautoexplore on
CQ_CANNED only if the hero stands on the leg's target, no hostile is in
view (canspotmon && couldsee), and HP isn't low; otherwise stop. Refuses
when blind, trapped, engulfed, or with hostiles in sight; 200-leg cap per
press. Named "autoexplore" (not "explore") because #exploremode exists.
Travel sets nopick, so explore walks over items without picking them up.

## 50-53. Door arches, quick photo, travel pickup, OS cursor (2026-11-18)

Built 22:42, installed by the quit-watcher; previous exe =
NetHackV.exe.bak-2026-11-18-qol3. NOT yet confirmed in play.

**§50 Door arch orientation** (mapdata.cpp vulture_orient_door_tile, both
door branches of the furniture pass). User screenshot: an open door in a
corner of irregular walls drew its arch sideways. The core's
set_door_orientation() (sp_lev.c) says horizontal = (wall left or right)
&& !(wall above AND below) - fine for tty's '|'/'-', but a corner door gets
the short stub's axis. Projection check (levelwin map_to_mouse): screen x ~
(x - y), screen y ~ (x + y), so map +x runs to the lower right and a
horizontal wall runs upper-left -> lower-right; H art faces that way, V art
the other.  Now the art follows the real walls: adjacent wall count per
axis, then the longer straight wall run (up to 4), then the walkable side
(walkable left/right = passed along x = vertical wall), then the core.
Broken doors/doorways untouched. ⭐The first build had the helper but no
call site - the -Wunused-function warning is how that showed up.

**§51 #photograph, Ctrl+S** (apply.c dophotograph, cmd.c, extern.h). User:
"What do you want to use or apply?" on every camera shot. Queues doapply +
the camera's invlet on CQ_CANNED, so only the direction is asked. Ctrl+S
was unbound (checked the extcmdlist keys and bind_key defaults); Vulture's
vulture_make_nh_key passes Ctrl+letter through.

**§52 Travel picks up thrown missiles** (cmd.c travel_pickup_thrown,
pickup.c). User: "how come I don't autopickup darts". Two causes: Kilmer's
pickup_types is $"?!/= (no weapons), and dotravel_target() sets
context.nopick - and Vulture's click-to-move IS travel - so even thrown
darts were never picked up when walking by mouse. dotravel_target() now sets
travel_pickup_thrown (cleared at the top of every rhack(), so an 'm' move
never inherits it); pickup() lets a nopick travel autopick, thrown-only
(autopick_thrown_only = !flags.pickup || nopick). Config:
AUTOPICKUP_EXCEPTION="<.*dart.*" added to kilmer.nethackrc (normal walking
grabs any darts; shop stock is still rejected first in autopick_testobj).

**§53 The pointer is an OS cursor** (vulture_mou.cpp, vulture_sdl.cpp,
vulture_main.cpp). §35 did not end it: the user then showed a pointer, and
later a whole trail of arrows, baked into the top-of-screen message area
(a pixels-in-vulture_screen bake, not a stale display rect - the exact path
was never found by reading). So the software cursor is gone: each cursor
tile becomes an SDL_CreateColorCursor (native art, hotspot = -xmod/-ymod,
clamped), set by set_mcursor and on every mouse_draw; SDL_ShowCursor is now
ENABLED at startup; the pointer's get_bg/put/restore and the §35 rect
tracking are deleted. The tooltip is still painted by Vulture and still
placed with the scaled vulture_cursor() geometry. vulture_curs() (keyboard
look/teleport cursor) now also SDL_WarpMouseInWindow()s the real pointer
(render -> window space). mouse_destroy frees the cursors and restores the
system arrow. Backup of the software-cursor file: session scratchpad only.

## 54. Scores were never broken; end-of-game dumplogs turned on (2026-11-18)

User: "fix the score files". ⭐VultureNetHack/record, xlogfile, logfile,
livelog and perm are 0-byte placeholders from the July packaging; nothing
reads them.  The live files are in C:/ProgramData/NetHack/5.0/ (record,
xlogfile, logfile, livelog - all current, 220 xlogfile games), as is the
LIVE sysconf (its LIVELOG=0x1FFF is what writes livelog).  The game-folder
sysconf is NOT read.  No code change for scores.

Dumplog: DUMPLOG was commented out in include/config.h.  Now defined (adds
sysopt.dumplogfile - runtime config only, not in any save, so saves stay
compatible).  ⭐Because the core makefile has no header deps, ALL core objects
were moved aside (src/o/nethackv.pre-dumplog/) and rebuilt - 139 files, clean;
craft.o (Crafting build, not in this makefile) put back untouched.  No Vulture
code reads sysopt, so its objects needed nothing.  Live sysconf (backup
sysconf.bak-2026-11-18): DUMPLOGFILE=C:/Users/dalla/NetHack/dumplogs/%n-%D.log
(folder created).  Built 23:31, installed directly (game not running);
previous exe = NetHackV.exe.bak-2026-11-18-predumplog.  NOT yet seen writing
a report - it appears when a game ends.

## 55-61. QOL batch 4 (2026-11-18)

Built 23:45 clean (no new warnings), installed directly; previous exe =
NetHackV.exe.bak-2026-11-18-qol4.  No save-format change (new state is
static; album count uses the already-saved lifelist counter).  NOT yet
confirmed in play.  Pre-batch sources: scratchpad pre-batch4.tar.

Keys (all verified unbound first; Vulture's vulture_make_nh_key maps
Alt+letter to M()): Alt+Q #firenearest, Alt+K #attacknearest, Alt+Z #rest,
Alt+H #elbereth, Ctrl+S #photograph (now aims), '>' smart descend.

**§55 Smarter auto-explore** (cmd.c).  Per-level stood-on bitmap
(autoexplore_note_position from vulture_qol_pass).  BFS targets: frontier,
or a remembered object never stood on (not corpse/boulder/shop) -> AEX_ITEM,
chain stops there.  Nothing left -> nearest unsearched corridor dead end
(CORR with <=1 known accessible neighbour) -> AEX_DEADEND; on arrival
autoexplore_start_search() searches 15 turns (occupation), AEX_SEARCHING,
then explores again.  Already standing on the dead end = no travel.

**§56 One-key commands** (cmd.c, apply.c, engrave.c).  vq_line_target():
8 straight lines up to BOLT_LIM, stops at walls/closed doors/the FIRST
monster of any kind (pets and peacefuls block, never get shot); camera mode
prefers species not yet photographed.  #firenearest queues dofire + dir.
#attacknearest: adjacent -> queue the walk into it (paranoid attack rules
still apply), else travel toward the nearest visible hostile.  #rest:
occupation searching until HP and Pw full; stops at Weak, 2000 turns, or
any interruption.  #elbereth: preset text (vq_engrave_preset, valid only
the same turn) + queued doengrave + '-' (+ 'n' ONLY if a visible dust
engraving is there - otherwise the 'n' would become a move).  Smart '>':
stock dodown on stairs/ladder/trap or when levitating/blind/trapped; else
travel to the nearest remembered down stair/ladder (incl. branch) and
vulture_qol_pass queues dodown on arrival.

**§57 Pets mark items** (dogmove.c, after place_monster).  A pet (has_edog,
not a food move) stepping onto a pile with cursemsg false, pet and spot both
seen, no hallucination -> bknown=1 on every item there.  No "not cursed"
state exists, so blessed items show as blessed - documented trade-off.

**§58 Danger colours** (messagewin.cpp).  danger_phrases[] (stoning, slime,
strangling, terminal illness, fainting, low HP, paralysis, ...) draw in a
red version of the age fade.

**§59 Prayer sense** (statuswin.cpp condition cell).  NoPray (red: angry god
or Luck<0), Pray (green: ublesscnt 0), Pray? (yellow: <=200, major trouble
only), nothing above 200.

**§60 Photo album rewards** (mon.c, shk.c).  Tourist milestones at
10/25/50/100 species: message + 100/400/1500/5000 exp.  famous_photographer()
(Tourist, total_photographed >= 50) removes the low-level Tourist surcharge in
get_cost, set_cost AND priceid_touristy (autoid's mirror) - the visible-shirt
surcharge stays.

**§61 Save backups** (restore.c vq_backup_savefile).  Before a successful
restore deletes the save: copy to <savedir>/backups/<save>-<YYYYMMDDhhmmss>.
Never pruned.  Restore one by copying it back under the original name.

## 62. Rotatable isometric view - PageUp / PageDown (2026-11-18)

User's "dream feature".  Built 23:53 clean, installed; previous exe =
NetHackV.exe.bak-2026-11-18-prerotate.  NOT yet seen on screen - this is the
change most likely to need a visual follow-up.

Model: vulture_view_rot (vulture_gen.cpp, 0-3 quarter turns, not saved).
Screen layout stays in VIEW coordinates; v_rot() (levelwin.h, one step =
(a,b) -> (-b,a)) maps view offset -> map offset, and 4-rot back.  Rotation 0
is stock Vulture exactly (identity everywhere; mouse_to_map rewritten with
the view centre factored out + floor division, equal to the old formula for
the old non-negative numerators).

Touched (levelwin.cpp unless noted): all three draw passes (view offset ->
map square, bounds checked per square); wall faces (get_wall_tiles now
loops over the four SCREEN sides via rotated neighbour deltas - "west" means
screen-west); wall shading neighbours; water/lava edge probes; floor
pattern tiled in view coordinates; multi-square rug decor disabled while
turned (it would scramble); door arch art swapped V<->H on odd turns
(v_rot_door, on top of §50's orientation); mouse_to_map / map_to_mouse /
need_recenter / glyph-change invalidation / engulf drawing; edge-scroll
hotspots (screen deltas rotated into map deltas); keys - vulture_translate_key
(vulture_gen.cpp) now ends in vulture_rotate_dirkey() so hjkl/numpad stay
SCREEN-relative (also when no_key_translation is set); dirdialog's on-map
click path rotates its result (the off-map path goes through translate).
PageUp/PageDown in levelwin::handle_keydown_event (level window only, like
F7/F8), no game time.

Not rotated (by design or not worth it): monster/object sprites (they face
the camera anyway), directional furniture art (stairs, altars), drawbridge
art, the 2D minimap/overview.

**§63 Follow-ups after play review (11-19).**
- Smart descend moved OFF plain `>`: `>` is stock dodown again (the user:
  "> is already the command to go downstairs").  It is now #smartdown on
  M('>'); vulture_make_nh_key (vulture_gen.cpp) turns Ctrl+> AND Alt+>
  into M('>') (checks ch=='>' or Shift+period, since Ctrl may blank the
  text char).  Not tried in play.
- §57 pet BUC marking REVERTED (dogmove.c back to HEAD): "feels like
  cheating".  Vanilla's "moves only reluctantly" message is untouched.
- Photo album vs stock 5.0.0: stock already tracks mvitals[].photographed,
  counts lifelist.total_photographed, and gives a Tourist experience(mtmp)
  exp for the FIRST photo of each species (not the starting pet, not worm
  tails / disguised Wizard).  The mod only ADDS: the album message (§37),
  #photos list, Ctrl+S auto-aim (§51), milestone bonuses + 50-species shop
  discount (§60).  The last two are non-vanilla balance changes.
- Prayer cell (§59) no longer reads Luck: NoPray = god angry only ("knowing
  your luck is also cheating").  With Luck<0, "Pray" can show and still fail.
- Pet marking, CURSED-ONLY (dogmove.c, inside the "reluctantly steps onto"
  block): if the hero sees the spot, no pile item is already known cursed,
  and exactly one item has unknown BUC, that item gets bknown (it must be
  the cursed one).  Never reveals blessed/uncursed.
- Prayer sense reworked to hero knowledge only: dopray() now adds a
  chronicle line "prayed to <god>" (LL_MINORAC, gamelog is saved - no format
  change); vq_turns_since_prayer() (pray.c) finds the latest one (or the
  first-prayer conduct line; none = turns since start).  Cell: Pray green
  at 1000+ turns, Pray? yellow 500-999, NoPray red if god angry, else
  nothing.  ublesscnt and Luck are no longer read.  Old saves only have the
  first prayer on record.
- #smartup on M('<') = Ctrl+< / Alt+< (vulture_gen.cpp, Shift+comma too);
  smart_stairs(up) shared with #smartdown; descend_is_up picks doup/dodown
  in vulture_qol_pass.

**§64 Mute when unfocused** (vulture_sound.cpp, vulture_sdl.cpp).
vulture_focus_mute(): FOCUS_LOST in vulture_handle_global_event sets
Mix_Volume(-1, 0) + Mix_VolumeMusic(0); FOCUS_GAINED restores MIX_MAX_VOLUME
(nothing else in the port sets volume).  Volume rather than pause, so
effects fired while away don't queue up and music keeps its place.  The
intro video's own audio queue is not covered.

**§65 Main menu / launcher** (NEW win/vulture/vulture_mainmenu.cpp; hook in
sys/windows/windmain.c before plnamesuffix(); levelwin.cpp; vulture_gra.h).
Shown when started WITHOUT -nethackrc= / NETHACKOPTIONS=@ (captured right
after early_options(), since initoptions() frees gc.cmdline_rcfile), not in
wizard mode, and not when VULTURE_SKIP_MENU is set (cleared on entry so a
later "Play again?" child shows the menu).  Items: Continue (newest save by
mtime), Load (get_saved_games(), SELECTSAVED), roster hero (scans
<CONFIGPREFIX>*.nethackrc except .nethackrc for name:/role:/race:/pettype:),
new hero (name prompt, default rc, in process), high scores (parses
record), past games (sysopt.dumplogfile dir, fate = the "You died ... with
N points" line), save backups (restore: current save backed up first;
archive: rename into backups/), Guidebook.txt, quit.  A save/hero with a
roster rc RELAUNCHES the exe with -nethackrc= + VULTURE_SKIP_MENU/INTRO
(options are read before the window port exists).  Title backdrop =
graphics/entrance.png + random tip line, held in vulture_backdrop so
levelwin::draw() repaints it (not black) while no map exists.  Per-hero
shortcuts still bypass the menu.  NOT yet seen on screen.

## 66. `bestroll` extended to Wizards (2026-11-19)

User request: give the Wizard (Phar'massi) the Tourist's best-start
treatment.  User's call: rolled item TYPES stay random (picking a wand of
death etc. would be overpowered, same reasoning as the Tourist's food);
only the rolls go to their ceiling.  bestroll_active() is now Tourist OR
Wizard.  Wizard effects (u_init.c):
- UNDEF_BLESS items blessed: cloak, wand, rings, potions, scrolls, random
  spellbook (existing generic path).
- Wand charges = top of mksobj()'s roll: 8 (rn1(5,4)), NODIR 15
  (rn1(5,11)), stasis 6 (rn1(4,3)).  Wishing is already filtered out.
- Chargeable rings +5 (rne(3) caps at 5 below XL 15).
- Magic marker 22 (generic 19+rn2(4) path); blindfold always (1-in-5).
- Stats: bestroll_attrs() takes a per-role priority; Wizard = Int, Con,
  Dex, Wis, Str, Cha.  Elven Wizard: Str 14 Int 20 Wis 20 Dex 18 Con 16
  Cha 11.
Option description/comments in optlist.h + flag.h updated (comment/string
only - no struct layout change); options.o moved aside so it rebuilt.
Enabled in pharmassi.nethackrc + pharmassi-nopet.nethackrc (backups in
NetHack/backup-2026-11-19/).  Kilmer rc comment fixed (it still said
20 lembas).  Built via finalize_exe.sh, rc=0.  NOT yet seen in a new game.

## 67. Auto-explore opens doors (2026-11-19)

Bug report: `'` auto-explore never opened doors.  Cause: it walks by
travel, travel sets context.run, and stock test_move() (hack.c) only
autoopens when !context.run - so the leg stopped with "That door is
closed." short of its target and the chain ended.  Fix: autoopen also
fires while autoexplore_active.  A door found LOCKED by that attempt is
recorded (autoexplore_door_locked -> aex_locked[][], per level, cleared
with the other aex maps) and the BFS treats it as a wall from then on;
the chain re-plans once (autoexplore_reroute) instead of stopping.  The
hero saw "This door is locked", so no hidden info leaks.  A stuck door
still just ends the leg (press `'` again).

## 68. Gor'oth: orcish melee Wizard + `bestroll_melee` (2026-11-19)

New roster hero: goroth.nethackrc (copy of pharmassi-nopet: chaotic male
orc Wizard, pettype:none, bestroll) + `Play as Gor'oth.lnk`.  New boolean
option `bestroll_melee` (flag.h iflags, optlist.h): with bestroll, stat
priority Con, Str, Dex, Int, Wis, Cha.  First version also SET Str/Con to
the race cap (18/50) - user REJECTED that as cheating the same day; now
within the dice: orc Str 18/16 Con 18 Int 14 Wis 11 Dex 11 Cha 11.
flag.h + extern.h changed -> all 139 core objects moved aside (scratchpad
stale-o2, craft.o kept) and rebuilt; finalize_exe.sh rc=0.  NOT yet seen
in a new game.

## 69. Local-LLM monster chat: `llmchat` option (2026-11-19)

User idea: #chat barks written by a local LLM.  OPTIONS=llmchat:<ollama
model> (compound, iflags.llmchat[64], optfn_llmchat; empty/none = off).
- Transport: win/vulture/vulture_llm.cpp - blocking HTTP/1.0 POST to
  127.0.0.1:11434 only (winsock; -lws2_32 added to sys/windows/GNUmakefile
  LIBS), 8 s timeout, refused connection = instant fail.  Kept free of
  NetHack headers.  vulture_llm_warmup() loads the model on a thread once
  per run (called from vulture_qol_pass) - a cold load took 60-70 s.
  -DLLM_DEBUG prints raw replies (standalone test only).
- Game side: sounds.c llm_rewrite()/llm_chat_ok(), hooked after the
  domonnoise() switch.  The STOCK line is computed as before and given to
  the model as the meaning to keep.  Action barks keep the stock verb
  verbatim and the model only appends a clause ("The little dog whines,
  tail low and eyes on you.") so info-bearing barks can't change meaning.
  Speech is rewritten whole; narration around a quote is stripped.
- Model sees only hero-knowable things: displayed species (+given name),
  attitude, the hero as seen (race/role/gender or poly form, wielded item
  via simpleonames), place (shop/town/Mines/Sokoban/Gehennom/DoD depth).
  System prompt forbids new facts/hints.  Per-monster memory of its last 2
  speech lines (12 slots, not saved) to avoid repeats.
- Stock text kept for: Hallucination, unseen monsters (name would leak),
  Death, cancelled lines, MS_NURSE/GUARD/RIDER/SPELL/SELL/PRIEST/ORACLE/
  LEADER/NEMESIS/GUARDIAN.  cuss() prints itself and is untouched.
- Any failure -> stock line.  Prompt tested via Ollama: ~1.2-2 s per line
  warm on the GTX 1660 with qwen2.5-coder:7b; standalone transport test
  ok (1.1 s) and dead-port test ok.
Enabled in all 9 roster rc files (llmchat:qwen2.5-coder:7b; backups
*.pre-llmchat in NetHack/backup-2026-11-19/).  flag.h changed -> full core
rebuild (stale-o4 in scratchpad).  NOT yet seen in a real game.

## 70. LLM: every spoken line, typing to monsters, names (2026-11-19)

Model switched to llama3.2:3b (pulled; fits the 6 GB GTX 1660 fully):
0.2-0.6 s a line warm, ~5 s cold, vs 1-2 s / 60-70 s for the 7B.
- EVERY verbalize()d line: vpline() (pline.c) hands PLINE_VERBALIZE lines
  to llm_rewrite_spoken() (sounds.c).  Skipped: lines with digits
  (prices/amounts), hallucination, lines the model already wrote
  (llm_skip_spoken).  Rejected replies: > 2x stock length + 30, any digit,
  or a lost proper name (capitalised word not starting a sentence must
  survive).  Cached per run (128 lines): stable within a session, new
  wording next run.  Speaker species from set_voice() -> llm_note_speaker()
  (same turn only, cleared after use).  TESTED: a rich-context prompt made
  the 3B echo the scene and add facts ("Thou hast angered me" -> a
  description of the player); a bare 2-example few-shot rephrase keeps
  meaning and names ("You have incurred my wrath.").
- llmtalk (boolean): dochat() -> llm_free_talk(): getlin "Say what?";
  blank/Esc = normal chat.  Speakers answer in character (deflect hints);
  animals get a described reaction.  Per-monster memory of the last
  exchange.  Not for shk/priest/vault guard/Oracle/quest/Riders.
- Names: llm_pet_name_pass() (vulture_qol_pass) names an unnamed starting
  pet from turn 5 ("You decide to call your little dog Flicker."), up to 4
  tries.  A peaceful speaker (not shk/priest/guard/minion/unique) is
  christened the first time it talks and introduces itself.  HERO naming
  NOT done: plname keys the save file and lock and is fixed before role
  selection - needs a design decision.
Enabled llmtalk in all 9 roster rc files (backups *.pre-llmtalk).

## 71. Logs from trees, #build walls and doors (2026-11-19)

New object "log" (objects.h, ROCK_CLASS after statue, prob 0, wt 30,
WOOD, mergeable).  EDITLEVEL 0 -> 1: the object table is saved, so old
saves/bones are invalid (no saves existed; the 9 old bones files get
discarded).  Tile: win/share/objects.txt entry after statue + renumbered;
Vulture art tiles/object.log.png + objicon.log.png (drawn from the club's
bark texture, scratchpad mklog.py), conf object.LOG/objicon.LOG in both
gamedata and the package conf.  Cutting down a tree (axe, dig.c) now drops
2-3 logs.  #build (dig.c dobuild, cmd.c ext cmd): adjacent doorway/broken
door -> door (2 logs) or wall (5 rocks); floor/corridor -> wall (5 rocks).
Not in shops/Sokoban, not on traps/objects/monsters; 5 turns.  Wall type
HWALL/VWALL from neighbouring walls; xy_set_wall_state + vision recalc.
Full rebuild incl. tilemap.exe/tile.c/makedefs (stale-o5).  NOT yet seen
in a game.

## 72. Right-click menu = 5.0's own #therecmdmenu (2026-11-19)

Vulture's popup (mapdata::get_map_contextmenu) was a hardcoded 3.x list.
It now lists exactly what 5.0's there_cmd_menu would offer for the square
and performs the pick with 5.0's act_on_act().  cmd.c: mcmd_addmenu() has
a capture mode; vulture_collect_cmd_menu(x,y,...) runs the self/next2u/
far/common builders into a buffer; vulture_do_cmd_menu_act() queues the
action and sets vulture_click_consumed, which makes readchar_core() skip
click_to_cmd() for the click Vulture hands back (num 0) - rhack() then
runs the canned queue.  Popup ids V_ACTION_NHMENU (1000) + index.  Vulture
extras 5.0 lacks are appended: Fire, Engrave, Pay bill, Pray, Turn undead,
Wipe face (only when Glib/creamed), What's this? (if no Look at).

## 73. Ask your god a question (2026-11-19)

pray.c pleased(): when the prayer fixed no trouble (trouble0 == 0,
p_trouble == 0) and granted no favour (pat_roll <= 0, i.e. no pat on the
head or its "nothing" roll), llm_ask_god() (sounds.c) offers a getlin
question; the llmchat model answers as the god - cryptic, archaic, never
game facts.  "The voice of X booms out:" + verbalize (skip-flagged so the
spoken-line rewrite doesn't touch it).

## 74. Sound fixes, new SFX, location music, shake, vignette, footsteps (2026-11-19)

- Event sounds: whole-word matching (vulture_sound_match) - [ cat] fired
  inside "catch" ("You catch fire" meowed).  Animal lines now keyed on the
  noise verb (barks/yips/mews/purrs/roars/growls...), not the species.
- 14 synthesized effects (pure-Python synth_sfx.py in scratchpad, WAV):
  explode, fire, zap, chime, hurt, click, coins, crunch, gulp, rattle,
  whoosh, splash, step1-4; conf lines for explosions, catching fire,
  bolts, prayer/blessing glows, monster hits ("bites!"), traps, gold,
  eating, locked doors, level teleport; bell.ogg on "You die".  16 mixer
  channels (was 4).
- Music: message triggers for shops removed ([store] etc.).  The core
  calls vulture_music_zone() every turn (vulture_qol_pass): battle (a
  real-threat hostile within 7 squares, or one that hurt you, +12 turns of
  hysteresis) > shop > town > Oracle > Mines > Gehennom > dungeon.  A zone
  change fades the new track in, so shop music hands back as you leave.
  Shop music at 30% volume, everything else 55%; dungeon rotates calm
  tracks only (battle tracks reserved for fights).  The old random
  ambient picker defers once zones run.
- Screen shake + red hit flash on HP loss (vulture_screen_shake in
  vulture_sdl.cpp, on the presented window surface, ~8 frames).
- Low-HP vignette below half HP (levelwin.cpp, drawn at the end of the
  full map repaint; vulture_force_map_redraw when the HP step changes).
- Footsteps from domove() (quiet, 4 variants; splash in water; none when
  levitating/flying/riding).
None of this is toggleable yet.  NOT yet seen/heard in a game.

## 75. Every line reworded, text-to-speech, tweening, facing (2026-11-20)

User: variety is the point - every line of text, not only speech, and
not the same wording twice in a game.
- llm_rewrite_line() (sounds.c) replaces llm_rewrite_spoken(): vpline()
  hands it EVERY line except PLINE_NOREPEAT/OVERRIDE_MSGTYPE/
  SUPPRESS_HISTORY/NO_CURS_ON_U (Norep and getpos autodescribe need exact
  text), inventory lines ("a - ..."), [prompts], "--", < 8 chars,
  hallucination.  NO cache.  Narration uses a 3-example few-shot;
  speech keeps the speaker few-shot.  2.5 s timeout; a slow (not refused)
  call backs off 60 s.
- Meaning check llm_meaning_kept(): same numbers; every proper name;
  every GAME NOUN (vocab = every word of every object name/appearance and
  monster name, built once) - and the reverse: no game noun or
  capitalised name the original didn't have (tested: the 3B invented a
  "warhammer" and a "translation" of ZELGO MER).  Protected state words
  must survive (hungry, weak, faint, burdened..., blind, confused, cursed/
  blessed/uncursed, compass directions, locked, peaceful/hostile/tame).
  Verbs are NOT protected - "strikes" for "hits" is fine (an earlier list
  that protected hit/kill/opens/up/down rejected half of all rewrites).
  Parroted few-shot examples rejected.  Prefix labels stripped.  Test on
  36 real messages: ~0.2 s each.
- Sound effects key on the ORIGINAL text: vpline calls
  vulture_msg_sound_original() before swapping the line; vulture_main.cpp
  skips its own match for that message.
- TTS: win/vulture/vulture_tts.cpp, Windows SAPI 5 (David/Zira), async,
  XML pitch/rate.  llm_tts_line() for verbalize()d lines when
  OPTIONS=voices (5.0's own flag; its windsound_verbal is a stub).
  Voice from set_voice() -> llm_note_speaker(mtmp, moreinfo): gender ->
  voice, m_id -> stable pitch/rate, small/big monsters higher/lower, deity
  deep and slow.  voices enabled in all 9 rc files (backups
  *.pre-voices).
- Tweening (levelwin.cpp vtw_*): movers keyed by m_id (hero = VTW_HERO)
  vs positions committed after the last animation; a 1-square move draws
  the mover and the camera at the OLD place (t = 0), then
  vulture_tween_run() - called at the end of
  vulture_refresh_window_region(), re-entry guarded - plays 6 eased
  frames (3 while running/travelling, ~12 ms each) and commits.  Camera
  offset = (1-t)*proj(new view - old view); mover offset =
  (1-t)*proj(old - new), both through v_rot so view rotation is right.
  Teleports, level changes and rotation snap.
- Facing: last horizontal screen direction per mover; each sprite's own
  facing detected once from the x-centroid of the top 30% of its opaque,
  non-shadow pixels (jackal/dog/kitten/rat right, horse left, symmetric
  -> treated as right); flipped copies cached per tile id.
NOT yet seen/heard in a game.

## 76. Mail daemon over Discord; the hero mutters remarks (2026-11-20)

- MAIL is now defined for Windows (windconf.h).  SCR_MAIL and flags.biff
  were already unconditional, so no object-table/struct change.
- Discord transport: win/vulture/vulture_discord.cpp - WinHTTP (HTTPS,
  -lwinhttp added to LIBS) on a background thread polling
  GET /api/v10/channels/{id}/messages?after={last} every `poll` s (min 10;
  the first poll only records the newest id, so history isn't delivered);
  small JSON parser; bot-authored messages skipped (no echo of replies);
  non-ASCII -> '?'.  Config: %USERPROFILE%\NetHack\discord.txt (token=,
  channel=, poll=), written by the player; template discord.txt.example
  there.  UA "DiscordBot (https://www.nethack.org, 1.0)" - nothing about
  the player is sent.
- mail.c (!UNIX branch): ckmailstatus() pops queued messages ->
  newmail() with "I have a message for you from X", scroll named after
  the sender, omailcmd = "discord<US>id<US>author<US>text" (saved with the
  object).  readmail() -> read_discord_mail(): text window, word-wrapped,
  then "Write a reply?" -> POST as a Discord reply (message_reference).
  newmail()'s give_up path still hands over a Discord scroll if no daemon
  could reach you.  Without discord.txt the old fake junk mail is
  unchanged (and never fires on Windows: mustgetmail stays -1).
- heroquips (boolean, iflags): llm_hero_quip_pass() from vulture_qol_pass:
  every 200-500 turns, or (60-turn cooldown) on arriving at a new level
  (1 in 3), getting hungrier, or dropping below 1/3 HP.  Context =
  role/race/name, place, rough health, hunger, burden, up to 3 visible
  non-pets, last message.  llm_no_new_nouns(): the remark may not name a
  game noun absent from that context.  'You mutter, "..."' (skip-flagged
  for the rewording pass) + TTS in the hero's gender with voices on.
  Enabled in all 9 rc files.
Full rebuild (stale-o6).  Not exercised against a live Discord bot or in
a game yet.

## 77. The Fates name your hero from their kit (2026-11-20)

User: name the character from their starting items, stats and class.
A hero named "Nameless" - a blank name on the main menu's "Create a new
hero" (mm_new_hero now maps blank -> Nameless instead of cancelling), or
OPTIONS=name:Nameless in an rc - is named by llm_name_hero() (sounds.c),
called in newgame() right after u_init_skills_discoveries(), i.e. once the
kit is final (after any reroll), stats are rolled and the starting pet
exists.  Prompt: gender/race/role/alignment, stat line with best and worst
stat, the kit (up to 12 items via simpleonames, gold as a sum), the pet;
asks for a first name with an optional epithet from the kit/stats/role.
Sanitised to letters/apostrophes/spaces, at most 3 words, PL_NSIZ.  Then
svp.plname is replaced and set_savefile_name(TRUE) renames the save; the
level files keep the lock name taken at getlock() (consistent for the
session).  Uses the llmchat model, or borrows llama3.2:3b when none is set
(the main menu's new hero uses the near-stock default rc); 25 s timeout for
a cold model; on failure the hero simply stays Nameless.  Tested: orc
Wizard -> "Korvok Darkstrike" / "Gorthok Blind" (blindfold), Tourist ->
"Kaili Snap" (camera), dwarf Valkyrie -> "Kragnok Steelshield".
NOT yet seen in a game.

## 78. Rewording every line is opt-in; melee lunge; death fade (2026-11-20)

User: the every-line LLM rewording is far too slow, but keep the LLM for
the on-demand things.  New boolean `llmreword` (iflags, default off) gates
the vpline() rewording (narration AND spoken lines); llmchat alone now
means #chat barks, llmtalk, names, ask-your-god, heroquips.  rc files:
llmchat back on, `#OPTIONS=llmreword` added commented out.
- vulture_anim_attack(ax,ay,tx,ty) from do_attack() (uhitm.c, before
  hitum/hmonas) and mattacku() (mhitu.c, adjacent + foundyou + youseeit):
  the attacker lunges, the target flashes red.  vulture_anim_death(x,y)
  from mondead() (mon.c, while the monster is still displayed) queues a
  death; played after the next refresh that no longer shows that tile.
  Both present frames like the tween; vulture_tween_run() runs the tween,
  then queued deaths, and refuses to nest inside either.  Each run logs
  "tween:/anim lunge:/anim death: N frames in M ms" (V_LOG_NOTE) to
  vulture_log.txt so we can confirm it fires.

## 79. Squash & stretch, rotation; middle-mouse camera drag (2026-11-20)

User: tweens must be more obvious - rotations, squash and stretch; and a
Dota 2 style middle-mouse drag.
- vtx_blit(): scale + rotate a sprite about its feet (bottom-centre),
  red tint and fade, nearest-neighbour inverse mapping into a temp ARGB
  surface (only animating sprites take this path).
- Step: 11 frames (5 running/travelling) x 14 ms; hop 12 px airborne for
  te < 0.8 with a vertical stretch (+18%), then a landing squash (-26%),
  width = 1/h^0.7, leaning 10 deg toward travel.  Lunge: 45% of a square,
  stretched along the blow (+20%), leaning 18 deg.  Target: knocked back
  14%, squashed 24%, tilted, red.  Death: topples to 85 deg, sinks 10 px,
  reddens then fades.
- MMB drag (levelwin::handle_mousemotion_event, state & SDL_BUTTON_MMASK):
  the map follows the mouse (camera moves opposite); xrel/yrel x
  render_scale accumulate in vpan_px/py; whole tiles become view shifts
  through the inverse iso projection + v_rot; the remainder is a pixel
  offset on map_centre.  Panning updates the tween snapshot so it never
  animates; release (or any set_view recentre) snaps the remainder away.

## 80. Way more sound effects; action sounds (2026-11-20)

31 more synthesized effects (scratchpad synth_sfx2.py).  vulture_sfx(name)
(vulture_sound.cpp) plays sound/<name>.wav straight from game code:
pick_obj (pickup), dropx (drop), ready_weapon (wield), accessory_or_armor_on
and armor_or_accessory_off (armor), start_eating (crunch), dopotion (gulp),
seffects (read), weffects (zap), throwit (throw), use_container (loot),
goto_level (stairs / fall), dopray once the prayer starts (pray).  ~60 new
message lines: misses, kills, shrieks, healing, curses/displeasure,
polymorph, invisibility, enlightenment, monster spells, poison/acid,
freezing, boulders and rumbles, lava/gas hiss, drips, cash register,
tree chopping, digging, locks, chests, sad feelings, wails, land mines.
NOT yet seen/heard in a game.

## 81. Lag: stale exe, background AI, animations yield (2026-11-20)

User: the game is too laggy.  Root cause: the game was RUNNING during the
s78-80 builds, so finalize_exe.sh's copy into VultureNetHack/ failed
("Device or resource busy" - hidden by filtering the build output) and the
user kept playing the 00:27 build, in which llmchat (re-enabled in the rc
files in s78) still meant rewording EVERY line.  Lesson: check the copy
step / exe timestamp after every build; watch for the lock.
Fixes: hero remarks and pet naming now use a background request
(vulture_llm_async_start/poll in vulture_llm.cpp; llm_bg_start/poll in
sounds.c, one job at a time); animations end at once when a key or click
is queued, drop to fewer frames when a frame takes > 25 ms, cap at
~150 ms, and the step tween is skipped while running/travelling/
auto-exploring/repeating; the per-sound "Playing file" debug log line is
gone (20,000+ file opens in one session's log).

## 82. Floating and flying monsters bob (2026-11-20)

vbob_offset() (levelwin.cpp): floaters (is_floater: eyes, lights) hover
6 px up and drift +-5 px on a ~2.4 s cycle; flyers (is_flyer) flutter
+-4 px on a ~0.9 s cycle; phase from m_id.  Clock-driven, added to the
monster's draw offset, so it composes with tweens/lunges.  Idle motion:
the input loop (vulture_event_dispatcher) waits 50 ms instead of 100 while
such a monster is within 18 squares of the view and calls
vulture_idle_bob() after each non-final event - only while the map is
waiting for a command (V_RESPOND_POSKEY, topwin == ROOTWIN), at most every
66 ms, never during another animation or with input queued.

## 83. Dropped items fall and bounce (2026-11-20)

vulture_anim_drop(x,y) from dropx() (do.c), mdrop_obj() (steal.c, when the
spot is in view) and make_corpse() (mon.c).  Queued squares play together
after the refresh that shows an object there (after any death fade): fall
30 px with gravity and speed-stretch, impact squash, one 9 px bounce,
settling squash.  levelwin's object layer goes through vanim_draw_object().

## 84. autosokoban option (2026-11-20)

User hates Sokoban.  OPTIONS=autosokoban (iflags): goto_level() ends with
vulture_autosokoban() (do.c) - on an unsolved Sokoban level (Sokoban flag
still set) the level's own pits/holes are deltrap()ed (which runs
maybe_finish_sokoban: rules lifted, completion livelogged) and all boulders
are delobj()ed with unblock_point; "This puzzle has already been solved."
No luck penalty (nothing squeezed or broken); prizes untouched.  Enabled in
all 9 rc files.

## 85. Stoneshard-style combat feedback; idle breathing (2026-11-20)

levelwin.cpp vfx_*: vfx_scan() compares every visible monster's mhp (and
the hero's HP) with the last frame and spawns a rising, fading number
(white for monsters, red/green for the hero) plus a 10-particle blood spurt
with gravity (not for noncorporeal monsters) - all damage sources count
without hooking each one.  During a lunge a pale crescent sweeps across the
target.  Idle creatures and the hero breathe (+-3% height, +-1.5% width,
phase per m_id).  The idle loop now always animates at the map (40 ms wait,
frames >= 80 ms apart, interval grows to 3x the frame cost, max 400 ms, and
never with input queued).

## 86. Light glow, night tint, dust, monster panel, F11 photo mode (2026-11-20)

vw_draw(): additive warm glow (flattened radial gradient, SDL_BLENDMODE_ADD
+ BlitScaled) for each gl.light_base source the hero can see (sized by
range, slight flicker) and each visible lava square; a cool blue tint
during real-world night (night()); 36 clock-driven dust motes; a
"Hostiles in view" panel (nearest 6 hostile, seen monsters with a rough
condition word: unhurt/scratched/wounded/badly hurt/near death).  F11 =
photo mode: redraws the map window alone and saves it as a BMP in
%USERPROFILE%\Pictures\Revulting (F12 stays Vulture's own full-screen
screenshot, which its global key hook catches first).

## 87. Piper voices; outdoor-only night; exe swap by rename (2026-11-20)

- The user was still seeing every line reworded: a relaunch beat the
  watcher, so the 00:27 exe kept running with llmchat on (= reword all in
  that build).  Fixed by renaming the running exe aside and copying the
  new one in - Windows allows renaming a running exe.  finalize_exe.sh now
  does this itself when the copy fails ("swapped the exe by rename") and
  deletes stale NetHackV.old*.exe copies once they're not running.
- Piper (user approved the ~210 MB download): piper_windows_amd64.zip
  2023.11.14-2 from github.com/rhasspy/piper, voices en_US-ryan-medium,
  en_US-amy-medium, en_GB-northern_english_male-medium from
  huggingface.co/rhasspy/piper-voices, installed in VultureNetHack\piper  (not in git).  Ryan/Amy configs are piper 1.5 and list 5 multi-codepoint
  diphthong keys that the 2023 binary rejects ("is not a single
  codepoint"); removed (originals in voices\original-configs).
  vulture_tts.cpp: one persistent piper.exe per voice (--output_dir,
  --quiet, stderr to NUL), a worker thread feeds a queue (max 6), loads
  the WAV (retrying ~2 s: piper prints the path before the file is
  complete), resamples for pitch (factor 1 + pitch*0.025), and plays lines
  one after another on reserved mixer channel 0.  Female -> Amy, male ->
  Ryan, pitch <= -6 (gods) -> the gruff northern voice.  SAPI fallback.
  Tested with SDL's dummy audio driver: line 1 after 1.5 s (incl. model
  load), line 2 queued behind it.
- Night tint now only outdoors: arboreal levels outside rooms
  (roomno == NO_ROOM, accessible, seen) and the Plane of Air, as one
  translucent blue diamond per square.

## 88. Stereo sound and environmental loops (2026-11-20)

- Mixer channels: 0 speech (Piper), 1-5 ambient loops - reserved in
  vulture_init_sound (Piper's own Mix_ReserveChannels(1) removed, it would
  have undone this).
- vsnd_dir(): a map offset from the hero -> stereo angle the way the map
  is drawn (isometric, through vulture_view_rot), distance ~22/square.
  vulture_sfx_at(name, dx, dy) plays a positioned one-shot; every other
  sound clears any position left on its channel (Mix_SetPosition(ch,0,0)).
  Used for: a monster swinging at you (swish, mattacku), a monster's
  dropped item and a fresh corpse hitting the floor (drop).
- Environmental loops (synth_amb.py, seamless crossfaded 4 s loops):
  lava (rumble + thick bubbles), water (lapping), fountain (trickle),
  sink (drips), wind.  vulture_qol_pass scans the 19x19 around the hero
  each turn for the nearest SEEN square of each kind (no leaking unseen
  lava) and calls vulture_ambient(type, present, dx, dy): fade in 0.9 s,
  follow it in the stereo field, fade out when gone.  Wind: forest levels,
  outside rooms.  Nothing while Deaf or Underwater.

## 89. Silence on death (2026-11-20)

really_done() (end.c) calls vulture_music_silence() when how < PANICKED -
only once the game is truly over, so lifesaving/second chances don't
trigger it: music and ambient loops (channels 1-5) fade out over 1.2 s and
vmz_silenced blocks zone changes and the random ambient picker; the
tombstone screen no longer plays nhfe_music_end_died.  Quit/escape/ascend
endings keep their music.

## 90. Dust motes only in light (2026-11-20)

Each mote's screen position is inverse-projected (iso + v_rot) to the map
square under it; drawn only if that square is cansee() and lit
(levl[].lit, or within range of a gl.light_base source).  90 motes now,
since most are culled.

## 91. Hearing monsters you can't see (2026-11-20)

- mattacku: an unseen attacker (adjacent, attacking you, not swallowed,
  not Deaf) is heard swinging from its direction (positioned swish).
- vulture_qol_pass: monsters within 9 squares are tracked by m_id between
  turns; ones you can't see that moved are heard - footsteps (step1-4,
  picked from the turn number so the game RNG isn't touched) or wingbeats
  for flyers (new flap.wav) - positioned, volume 70 - 6/square (min 15),
  the nearest two per turn.  Silent: floaters, noncorporeal, hidden
  (mundetected), helpless; nothing while Deaf/Underwater.
- vulture_sfx_at_vol(): per-channel volume for a one-shot; every other
  sound resets its channel to full volume, and the focus-mute flag is now
  shared so a sound can't unmute a channel while alt-tabbed.

## 6. Open risks

- R1 **Build system mismatch:** RESOLVED — WSL2 Unix build chosen; both 5.0.0 and Vulture build cleanly.
- R2 **SDL2 absent:** RESOLVED — installed via apt; Vulture links SDL2/mixer/ttf; WSLg display live.
- R3 Old Windows SDK / R4 Vulture Windows support: MOOT — building under WSL Linux, not MSVC/Windows.
- **R5 (NEW, highest for Path B):** name-compatibility drift — Vulture's config symbolic names are
  3.4.3/3.6.0-era; 5.0.0 renamed/added objects & monsters. Unresolved names currently auto-expand
  (silent). Phase 1 must convert these to conspicuous ERROR placeholders + a narrow allow-list.
- **R6:** the 3.7/5.0 `print_glyph`/`glyph_info`/`coordxy` window-API change vs Vulture's 3.6.6 API
  (Phase 4) — Vulture's `vulture_gra.cpp`/`windows.c` hooks assume the old signature.
- **R7:** globals namespacing in 5.0.0 (Phase 5) touches every Vulture direct read of `u`, `level`, `flags`.
