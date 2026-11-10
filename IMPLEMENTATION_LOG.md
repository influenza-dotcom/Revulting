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
