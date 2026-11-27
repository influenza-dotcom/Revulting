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

## 92. Death-cam; rival adventurers (2026-11-20)

- vulture_replay_capture() (from vulture_qol_pass, once per turn) copies
  the map window at 1/3 size into a 40-turn ring; vulture_replay_play()
  (really_done, deaths only, after the music fades) plays it back sepia,
  oldest first, slowing at the end, "Your last moments...", any key skips.
- OPTIONS=rivals (iflags): goto_level -> vulture_maybe_rival(new): first
  visit, depth 3+, not Sokoban/endgame/Ludios/quest/no-teleport levels,
  1 in 8: mk_mplayer of a random role placed out of sight >= 8 squares
  away, m_lev = depth +-2 capped at u.ulevel+3, hp d(lvl,8)+10, syllable
  name; same-alignment rivals peaceful half the time; "You hear someone
  else moving about on this level."

## 93. Blood stains (2026-11-20)

vst_*: every blood spurt (vfx_hit with blood) adds 1, every visible
corporeal death adds 2, to a per-level (dnum*1000+dlevel) stain store
(session only - not in the save); drawn as one of 4 procedural dark-red
splats on the floor layer, alpha by level (max 3), under traps/objects/
monsters.  Not on water/lava or non-floor.

## Main-menu crash (open, 2026-11-20)

User: the game crashes at startup unless the main menu is skipped.  Event
log: two access violations at RVA 0x363b08 in the 00:27 build (deleted -
can't symbolise).  Off-screen run of the current build (SDL dummy video/
audio) reaches the menu without crashing, so it likely needs the real
display.  An unstripped twin of each build (relink without -s; identical
.text) is kept as vulture5-win/binary/NetHackV-syms.exe for addr2line.

## 94. Black box when hit (2026-11-20)

User: "there's a black box when you get hit".  vulture_screen_shake()
filled the window black and blitted the frame shifted, so every hit
showed black bars of up to 18 px along two edges.  Now each shake frame
is drawn overscanned (amp px bigger on every side, SDL_BlitScaled), so
the window is always covered.  Insurance in vtx_blit(): a sprite with no
alpha mask and no colour key treats pure black as transparent (it would
otherwise transform into a black box).  No crash events since 10:41
(old build only).

## 95. MAIN-MENU CRASH FOUND; camera and instrument sounds (2026-11-20)

- Crash 12:08:50 (build 0x6b009a4a), access violation at RVA 0x36d548 ->
  addr2line on the unstripped twin: mapdata::set_glyph() mapdata.cpp:397,
  the `!cansee(x, y)` test.  cansee()/couldsee() index gv.viz_array, which
  is NULL until vision_init() - and windmain.c calls vulture_main_menu()
  (line ~403) BEFORE vision_init() (~444) with the windows already up, so
  a map update during the menu dereferenced NULL.  Hero shortcuts skip the
  menu, which is why they worked.  Fix: mapdata.cpp and levelwin.cpp
  redefine cansee/couldsee to answer "no" while gv.viz_array is NULL.
  (Very likely the same bug as the 10:41 crashes on the 00:27 build.)
- Debugging recipe kept: relink the build's objects without -s
  (/tmp/relink.sh, from the link line in final.log) -> binary/
  NetHackV-syms.exe with identical .text; Event Viewer id 1000 "Fault
  offset" + 0x140000000 -> addr2line -f -C -i.
- Sounds: camera (flash whine, shutter clacks, pop) from use_camera()
  after the charge is spent; engraving scratch (or fire when burning)
  from doengrave; message lines for tin/magic whistle, tooled horn,
  bugle, drum, guard footsteps, throne-room conversation, snoring snakes.

## 96. Hurt sounds: grunts, heartbeat, death cry (2026-11-20)

Grunts voiced by the installed Piper voices (Ryan / Amy: "Ugh!",
"Argh!", "Oof!", "Agh!", death "Aaaargh, no!"), silence-trimmed
(hurt_prep.py) -> sound/hurt_m1-4, hurt_f1-4, die_m, die_f.
vulture_hurt_vocal(female, severity) from the HP-loss branch of
vulture_qol_pass (next to the shake): severity 2 at >= 1/4 max HP (full
volume), 1 at >= 1/10, else 0 (40% chance); 450 ms cooldown; not while
Deaf.  Heartbeat (synthesized amb_heart.wav, ~80 bpm) is ambient loop 6
(7 reserved channels now) while hp < 1/4 max.  Death cry from really_done
before the music fades.

## 97. Harder shake, impact sounds (2026-11-20)

"It crashes again" (12:16): no crash event for the current build; that
startup ended without creating game windows and the next one went
straight into play - the main menu's relaunch into a roster hero, which
looks like a crash (window closes and reopens).  The game was running
fine afterwards (tween ~170 ms a step).
Shake: amp 4 + 3*strength (was 2 + 2*strength), 10 frames, stronger red
flash.  vulture_impact(severity) plays with every HP loss, before the
shake: impact_light (< 1/10 max HP), impact_mid (>= 1/10), impact_heavy
bone-crunch (>= 1/4), heavy + impact_slam deep boom (>= 1/2); synthesized
(synth_impact.py).  Layered with the Piper grunt (s96).

## 98. Startup trace for the Play NetHack.lnk close; softer blood (2026-11-20)

User: Play NetHack.lnk (main menu, no args) closes instantly; no crash
event.  Off-screen (SDL dummy video/audio) the earlier test never left the
intro movie - with VULTURE_SKIP_INTRO it reaches the menu and waits, so the
close needs the real display or a real choice.  Added
win/vulture/vulture_trace.cpp: vulture_trace() timestamped "[trace]"
lines in vulture_log.txt at each startup/menu step (windmain.c: menu
entered/returned, getlock, vision_init; main menu: saves listed, backdrop,
waiting, picked N, relaunch), plus last-words handlers - unhandled SEH
(code + RVA), std::terminate (uncaught C++ exception), abort(), exit().
Suspect: getlock() refusing when the same hero is already running.
Blood: spurt particles were square dots ("red boxes") - now soft round
alpha droplets (vfx_droplet, 3 sizes); stain blobs feathered at the edge.

## 99. No light or blood while blind (2026-11-20)

User: light effects still show when blind.  vw_draw counted a light
source on the hero's own square as visible (cansee || u_at) - now no glow
at all while Blind and only cansee() sources; lava glow also gated on
!Blind.  Monster damage numbers/blood (vfx_scan) and the death fade +
its bloodstain (mondead hook) used canspotmon (true via telepathy) - now
canseemon.  The hero's own damage numbers stay (you know your HP).

## 100. The 2027 HUD: toasts, location cards, heat stars, waypoint (2026-11-20)

All in levelwin.cpp (vhud_*), drawn last in the map draw, so it animates
through vulture_idle_bob.
- **Toasts** (vulture_toast): a card slides in on the right, holds about
  4.8 s, then slides out, with a chime, three at a time.
  - insight.c record_achievement: "ACHIEVEMENT UNLOCKED" or "NEW RANK".
  - exper.c pluslvl: "LEVEL UP".
- **Location card** (do.c vulture_place_card -> vulture_location_card):
  - Content: the dungeon name in spaced capitals, with the special place
    or "Level N" under it.
  - Shown on a new level, on a change of dungeon, and once per session
    from vulture_qol_pass.
- **Heat stars** (vhud_heat, 0-5):
  - Points: angry shopkeeper +2, angry watch +2, angry priest +1, angry
    vault guard +1, angry god +1 (+2 when ugangr > 3), udemigod +2.
  - The stars flash and a whistle sounds when the heat rises.
- **Waypoint**:
  - Target: the remembered (seenv) down stairs, or up stairs with the
    Amulet. It prefers stairs in the same dungeon over a branch.
  - On screen: a bobbing chevron labelled "Down"/"Up". Off screen: an
    edge arrow with the distance.
  - Hidden while hostiles are in view, blind, hallucinating or engulfed.
- **Smoke test:** an off-screen test hero ran 10 s in game with no crash.
  The visuals have not been checked on a real display.

## 101. Wanted stars REMOVED (2026-11-20)

User: "you're taking the GTA VI thing way too literally. This is still
NetHack." vhud_heat and the star surfaces are gone, and the toasts moved
up into the freed space. "Bring it into 2027 / GTA VI" means modern polish
and feel, not GTA mechanics.

## 102. AI is opt-in everywhere; a release package for new players (2026-11-20)

- **Opt-in:** sounds.c llm_name_hero no longer borrows llama3.2:3b when
  llmchat is unset, so a blank name just stays "Nameless". The main-menu
  name prompt no longer mentions the Fates. Every other LLM path was
  already gated on *iflags.llmchat.
- **Release package:** deliverables/release/Revulting-Windows.zip, 240 MB.
  - Contents: VultureNetHack minus the exe backups, the log, save/,
    hearse.ini (it held a personal email) and piper original-configs.
  - Fresh empty score and log files.
  - New-player .nethackrc (deliverables/release/newplayer.nethackrc): no
    role preset, AI lines commented out, voices/autoid/rivals on.
  - Added START HERE.txt and LICENSE-NetHack.txt.
- **Test:** the zip, extracted and run with an empty fake USERPROFILE,
  reaches the main menu. A preset hero reaches vision_init, so the new rc
  loads cleanly.
- **Not done:** gh isn't installed, so the GitHub Release isn't created
  yet. The README links to releases/latest.

## 103. "Start with a pet?" when creating a hero (2026-11-20)

vulture_mainmenu.cpp mm_new_hero: after the name (and not when resuming a
save), it asks yes/no. No sets gp.preferred_pet = 'n'. Yes keeps any
pettype from the settings, or clears a pettype:none, so the role's usual
pet comes. Hero shortcuts launched with their own rc skip the menu and keep
their pettype.

## 104. Hearse setup from the main menu (2026-11-20)

New main-menu item: [e] "Hearse: share bones with other players..."
(vulture_mainmenu.cpp mm_hearse*).
- **Not set up:** Set up asks for an email (private) and a public
  nickname, rejecting an email address as the nickname.
  - It writes hearse\hearse.ini so hearse.exe never prompts. A fresh file
    gets Mode = replace plus the BONESPREFIX/SAVEPREFIX dirs; an existing
    file keeps its other keys.
  - It runs hearse.exe once to register: windowless, stdin = NUL, output to
    hearse\hearse_last.txt and shown in a menu, 60 s cap.
  - A "User token" afterwards means success, so hearse_path is set in the
    user's vulture.conf (vulture_write_userconfig) and the exit-time
    exchange (vulture_run_hearse) runs. Otherwise it stays off.
- **Set up:** on/off toggle, "Exchange bones now", change email/nickname
  (the rerun notifies the server through Hearse's own CRC check).
- **Release:** the zip still ships without hearse.ini, so new players start
  "not set up".
- **Untested:** the setup itself against the live Hearse server.

## 105. Death-cam plays in colour (2026-11-20)

User: "remove the stupid filter from the last moments thing". The sepia
SDL_SetSurfaceColorMod in vulture_replay_play is gone, and the frames play
back as they were drawn.

## 106. Red box on every hit = the hit flash was clipped (2026-11-20)

User: "blood is still momentarily shown as big rectangles after being
hurt". vulture_screen_shake drew onto the window surface without resetting
its clip rect. That surface can still be clipped to the last draw region
(the same trap vulture_sdl.cpp ~415 already unclips for), so the red flash
and the jolt covered only that rectangle. The shake now saves the clip
rect, unclips, and restores it at the end. Not yet seen on a real display.

## 107. Death-cam no longer pixelated (2026-11-20)

User: "remove the pixelation too". Frames were stored at 1/3 size with a
nearest-pixel SDL_BlitScaled and blown back up the same way. Now:
- **Capture:** the map region is copied unblended into a full-size ARGB
  scratch surface, then box-filtered 2x2 down to half size (vrp_down2).
- **Playback:** bilinear back up to the map size (vrp_up).
- **Memory:** about 2 MB per frame at 1920x1080, so about 83 MB for the
  40-turn ring.
- **Test:** an off-screen game ran 10 s with no crash. The replay hasn't
  been watched on a real display.

## 108. Dota-style click marker (2026-11-20)

Left-clicking a seen map square (not your own) spawns vfx kind 3
(levelwin.cpp vfx_click / vfx_draw_click). Over 0.52 s:
- **Ring:** an iso-flattened ring of soft glow dots pops out, then shrinks
  into the square.
- **Chevrons:** four slide in on the diagonals.
- **Fade:** everything fades over the last 40%.
- **Colour:** green to move, red when the square shows a monster (attack).
- **Helper:** vfx_glowdot is one cached white soft-dot sprite, tinted and
  faded per call.
- **Smoothness:** while a marker is alive the idle loop redraws every
  25 ms and the input wait drops to 20 ms.
- **Test:** an off-screen game ran 8 s with no crash. The marker hasn't
  been watched on a real display.

## 109. Real red-box fix; move barks; best roll everywhere; one "Down" (2026-11-20)

- **Red boxes:** the s106 unclip was not the cause. SDL_ConvertSurface
  gives the shake's frame copy BLEND mode (its format has alpha), so each
  shake frame alpha-blended the copy onto the window. Pixels where blood
  left partial alpha soaked up the red flash and showed as solid red
  rectangles until the next full redraw. The copy is now
  SDL_BLENDMODE_NONE.
- **Move barks** (vbark_move): on a left-click move, the hero barks a short
  line from 18 ("Mhm.", "Moving!", "On my way."...).
  - Throttle: 3.5 s cooldown, a 55% chance, never the same line twice in a
    row.
  - Text above the hero's head (vfx kind 4) for 1.3 s; spoken via
    vulture_tts_speak with the voices option unless Deaf.
- **Best roll for every role:** bestroll_active is just iflags.bestroll.
  bestroll_attrs has priorities for fighters, Healer, Priest, Monk and
  Arc/Ran/Rog.
  - Main menu toggle [o] "Best starting roll for new heroes: ON/OFF"
    (this session; the rc sets the default).
  - mm_new_hero asks for it, defaulting to the current state.
- **Waypoint:** on screen it draws nothing, because Vulture's stair label
  already says "Down". The off-screen edge arrow stays.
- **Status:** none of these have been seen on a real display.

## 110. Dota-style hero portrait (2026-11-20)

statuswin.cpp vpt_*: a square, panel-high portrait left of the status
panel, which moves right to make room (constructor and resize).
- **Image:** the hero's current map sprite (MAP_MON at u.ux,u.uy, so
  polymorph shows), cropped to the top 55% of its opaque pixels (head and
  shoulders), squared, and bilinear-scaled.
- **Frame:** cut from the status panel's stone art, with a dark well.
- **Bars:** thin HP (green to red) and Pw (blue) along the bottom.
- **Caching:** rebuilt only when the tile or the size changes.

## 111. Music fades out, then in (2026-11-20)

- **Before:** vmz_start did Mix_HaltMusic and then a fade-in, so every
  zone change cut dead.
- **Now:** a zone change fades the old track out (1.1 s, or 0.35 s into
  battle) and queues the new zone (vmz_pending).
  - vulture_music_poll starts it with a 2 s fade-in once the old track has
    stopped. It's called per turn and from the input idle loop
    (vulture_win.cpp), so it never waits on a key.
  - vulture_play_song now fades in as well.
- **Status:** an off-screen game ran 9 s with no crash. Not yet heard or
  seen on a real display.

## 112. Travel animates instead of teleporting (2026-11-20)

- **Tweens:** vulture_tween_run no longer snaps steps of run, travel,
  auto-explore or counts (s81). Those play a fast tween (vtw_fast: 4
  frames, 5 ms apart, 2 if frames are slow). A waiting key or click still
  snaps the step.
- **Settings:** every hero rc, the default .nethackrc and the release rc
  went from runmode:teleport (no redraw until travel ends) to runmode:walk.
  Backups are in %USERPROFILE%\NetHack\backup-2026-11-20-runmode.
- **If long walks stall again:** set runmode back to teleport (or run) in
  that rc.

## 113. Hero barks for every kind of action (2026-11-20)

include/vulture_bark.h has VB_* kinds; vulture_bark(kind) lives in
levelwin.cpp (vbark_tab).
- **Kinds (21):** move, attack, kill, pickup, descend/ascend, cast, no
  mana, door opens, locked, eat, drink, read, pray, level up, low HP (first
  drop under 1/3), hunger (gets hungrier to Hungry/Weak), wear, wield,
  throw, zap.
- **Per kind:** 3-10 lines, a chance and its own cooldown.
- **Shared cooldown:** chatty kinds share a 2.5 s cooldown. Urgent kinds
  (stairs, no mana, locked, pray, level up, low HP, hunger) ignore it.
- **Delivery:** never the same line twice in a row per kind. Text over the
  head for 1.4 s (vfx txt grew to 40 chars); spoken with the voices option.
  Silent while hallucinating, strangled, engulfed, underwater or game over.
- **Hooks:** the existing s80 action-sound sites, uhitm vulture_anim_attack,
  mon.c xkilled (not pets or peacefuls), spell.c (cast / not enough energy),
  lock.c (door opens / locked), do.c goto_level (stairs, up vs down by
  depth), exper.c pluslvl, allmain.c (low HP, hunger).
- **Status:** an off-screen game ran 9 s with no crash. Not yet seen or
  heard on a real display.

## 114. Bark voices by class, gender and race (2026-11-20)

- **API:** vulture_tts_speak_voice(text, voice, factor, female) in
  vulture_tts.cpp queues a line on an explicit Piper voice (0 ryan, 1 amy,
  2 northern-English gruff) at a playback factor (0.75-1.3). It falls back
  to SAPI by gender.
- **Voice table:** levelwin.cpp vbark_voice maps the role to a male and a
  female voice/pitch.
  - Gruff low: male Barbarian, Cave dweller, Ranger, Valkyrie.
  - High: Tourist, Rogue, Healer.
- **Race nudges:** dwarf x0.94, orc x0.92, gnome x1.10, elf x1.04.
- **Limit:** only one female model, so women differ by pitch only. A
  multi-speaker model would fix that.

## 115. Talk to shopkeepers (2026-11-20)

- **Typed talk:** llm_free_talk (#chat with llmtalk) no longer skips
  shopkeepers. A blank line still gives the stock shk_chat price talk.
- **Context:** llm_who describes a shopkeeper by name, species and shop
  type (shtypes), plus a mood from its state:
  - angry (and robbed);
  - robbed;
  - debit owed;
  - unpaid items carried;
  - friendly.
- **Guard:** the prompt says its prices are fixed by the game. It may
  bargain in words or refuse, but never change a price, give goods away or
  forgive a debt.
- **Still excluded:** priests, guards, the Oracle, quest figures and Riders
  keep their stock behaviour.

## 116. Discord status, LibriTTS class voices, Steam shortcut tool (2026-11-20)

- **Discord Rich Presence** (NEW win/vulture/vulture_presence.cpp):
  - Talks to the local Discord client over \.\pipe\discord-ipc-N:
    handshake, then SET_ACTIVITY frames, on a background thread with at
    most one update per 15 s.
  - Shows "Name the Race Role - XL n" / "[Shopping, ]Dungeon depth -
    HP x/y" plus elapsed time, with large_image key "revulting".
  - The application ID comes from discord_app_id.txt in the game folder
    or REVULTING_DISCORD_APP; without one it does nothing.
  - Fed every turn from vulture_qol_pass (only changes are sent).
- **LibriTTS voices:** en_US-libritts_r-medium (904 speakers, 75 MB, from
  rhasspy/piper-voices, user approved) is in piper\voices.
  - vulture_tts.cpp voice 3 runs piper with --json-input, one
    {"text","speaker_id"} line per request.
  - vulture_tts_speak_speaker falls back to the old voice and pitch when
    the model is missing.
  - Speakers were sorted by pitch: 91 speakers generated (scratchpad
    voice_survey.py) and their F0 measured by autocorrelation. Males are
    97-140 Hz, females 181-251 Hz.
  - Barks pick one per role and gender (vbark_voice), plus a mild race
    nudge.
- **Steam:** toolchain/add_steam_shortcut.py adds Revulting to
  shortcuts.vdf (binary VDF, latin-1 byte-exact, backed up, idempotent).
  - It refuses while Steam runs, because Steam rewrites the file.
  - Dry-run on a copy: the existing entries stay byte-identical.

### 117. Speech bubbles, click markers per action, free camera + portrait lock, walk glide, hover fix

- **Speech bubbles:** speech floats over the speaker instead of filling
  the message window.
  - Covers the hero's muttering (heroquips) and any verbalize() line from
    a monster you can see, when the speaker is known this turn (set_voice).
  - Built from vulture_say_above / vulture_hero_say / vsay_draw (vfx kind
    5): word-wrapped, dark translucent box, follows the speaker. Each
    speaker has one bubble at a time, lasting 1.8 s plus 65 ms per
    character (9 s cap).
  - pline.c skips the message window for these lines. Lines that mention
    "zorkmid" stay in it too, so prices are never lost. The dumplog keeps
    everything.
- **Click markers:** each target gets its own shape and colour:
  - move: green chevrons
  - attack: red closing ring, spinning crosshair and an X
  - pet/peaceful: blue ripples
  - item: gold ring, rising sparkles and a bobbing arrow
  - closed door: amber leaves swinging apart
  - stairs: violet spiral, down or up
  - fountain/sink: cyan droplets into a pool
- **Free camera:** recenter_after_movement is now the camera LOCK, off by
  default.
  - Free, the view only moves on arriving on a level, or when the hero
    moves and walks right out of view (levelwin::hero_off_view).
  - Clicking the portrait centres the view; double-clicking toggles the
    lock and saves it to the user config. The portrait shows a LOCKED
    badge, a tooltip and a toast.
- **Walk glide:** run, travel and multi-step moves under runmode
  walk/crawl are now one linear glide per step (90 / 240 ms) with a
  smaller hop.
  - vulture_delay_output subtracts the glide time (vulture_tween_ate_ms).
  - Before, each step was a ~20 ms eased hop followed by a frozen 50 ms
    pause, which looked like teleport-stutter.
- **Hover tooltips:** the idle animation redraw (about 12 fps) painted
  over the tooltip, so it flashed and vanished. vulture_idle_bob now
  repaints it after each frame. Also fixed the cur.y reset typo in
  vulture_mou.cpp.

### 118. Esc cancels travel

- vulture_esc_cancels_travel peeks the SDL queue for Esc while
  travel/run/mv/multi is active.
  - It runs from vulture_delay_output (per step) and get_nh_event.
  - It flushes the key, so the Esc doesn't also open a menu, then calls
    nomul(0).
- The old TRAVEL_HACK was SLASHEM-only and never compiled here.

### 119. Death screams; calmer sleepers

- **Death screams:** mondead plays a positioned death scream from any
  non-silent monster you can see, or one within about 10 squares.
  Louder when closer; picked with the display RNG, so the game RNG is
  untouched.
  - Sets: humanoid male/female (scream_m1-4 / f1-4), small creatures
    (scream_s1-3, pitched up) and big creatures (scream_l1-3, pitched
    down).
  - Voiced with Piper LibriTTS (scratchpad make_screams.py): trimmed,
    saturated, normalised, faded, capped at 1.6 s.
- **Calmer sleepers:** sleeping or paralysed monsters, and the hero
  while asleep, breathe at 40% depth and under half the rate.
  Floaters/flyers settle and barely drift.

### 120. Esc also stops waits, searches and occupations

- vulture_esc_cancels_travel now also watches go.occupation (resting,
  searching until disturbed, eating, digging...) and counted repeats
  (20s).
  - An occupation ends with stop_occupation(), which prints its own
    "You stop ..." message.
  - A counted wait or search ends with nomul(0) and "You stop."

### 121. Mouse-wheel zoom

- **Zooming in:** wheel up zooms through 125/150/175/200%.
  - The drawn map is scaled up around the window centre with
    SDL_SoftStretchLinear (vzoom_apply), after the vfx and before the
    vignette and HUD, so toasts stay sharp.
  - While zoomed, every frame is a full redraw.
  - mouse_to_map, map_to_mouse and hero_off_view map through the zoom.
- **Zooming out:** wheel down zooms back out. One notch past 100%
  switches to render scale 2 (the F8 zoomed-out view), debounced to one
  rescale per 350 ms.

### 122. Crisp zoom

- Bilinear scaling at 1.25x/1.75x made the pixel art blurry.
- The steps are now 1.5x, 2x and 3x:
  - 2x and 3x use exact nearest-neighbour pixel blocks.
  - 1.5x uses sharp bilinear: a nearest upscale to 2x, then one small
    linear step down.

### 123. Portrait camera, Dota-style

- Removed the LOCKED badge, the camera toasts and the portrait tooltip
  (the user found them unintuitive).
- Clicking the portrait centres the view. Double-clicking sets
  vulture_cam_follow, so the camera follows the hero.
- Moving the camera yourself (edge scroll or middle-drag) clears it.
- The follow is session-only: vulture_opts.recenter / the config line no
  longer drive the camera, and nothing is written.

### 124. Drag the camera while travelling

- vulture_travel_pan takes only MOUSEMOTION events off the SDL queue
  during travel/run/multi and feeds middle-held motion to the drag
  handler.
  - It runs from the walk-glide frames and the vulture_delay_output loop.
  - Keys and clicks stay queued for the game.
- vanim_input_waiting ignores middle presses, so a drag doesn't snap the
  glides.
- A drag mid-glide shifts the tween snapshot by the pan, so the hero's
  slide isn't disturbed.
- vulture_cam_manual: once you pan, the free camera's "walked out of
  view" rescue stops pulling you back. It resets on a portrait click or a
  level change.

### 125. Modern hover and aim (Brogue / Cogmind), condensed messages, ground card

Research notes:
- **Brogue:** hovering highlights the travel path, a click walks it, and
  Esc aborts.
- **Cogmind:** the line of fire turns green/red when the path is
  clear/blocked, info windows are animated, and the HUD aims for
  "immediate visual access without opening windows".
- **Message-log designs:** condense duplicates; only stop for unseen
  messages. Vulture never had --More-- in the log; its blocking point is
  pop-up windows.

What was built:
- **Travel preview:** hovering a map square at least 2 away draws the
  path the hero would walk, with a step count.
  - vpath_find does a BFS over KNOWN terrain: seen squares, glyph-based
    walls/water/lava/boulders, no diagonals through doors.
  - Drawn as a gold dotted line with a marching pulse.
  - Hidden while travelling, during prompts, or when the pointer is over
    a panel (vhv_over_map).
- **Hover card:** a visible monster under the pointer gets a card after
  160 ms, fading in. The plain tooltip is suppressed for monster squares.
  - Shows the name and a threat word from the monster's difficulty
    against your level (Trivial / Fair fight / Dangerous / Deadly; Ally,
    Peaceful).
  - Tags: asleep / helpless / fleeing / trapped.
  - Health bar for pets and hurt monsters (no new information: damage
    numbers already showed it).
- **Aim:** a direction prompt during play turns dirdialog into a
  full-screen transparent layer with a banner.
  - The map draws a line from the hero toward the pointer, snapped to 8
    directions, stopping at walls: green when the first creature is
    hostile, red when it's a pet or peaceful, amber when there is no
    creature. A red cross means blocked at once.
  - Click to fire (vulture_aim_key -> dirchars / getdir self);
    right-click or Esc cancels. Direction keys still work.
- **Condensed messages:** messagewin::add_message folds a repeat within
  2 turns into "msg (xN)".
- **Ground card:** a blocking "Things that are here:" NHW_MENU becomes a
  non-blocking card beside the map. It lasts 7 s or until you move, and
  shows 7 lines plus "+N more". A felt cockatrice ("..." line) stays
  modal.

### 126. The ability bar

- **Layout:** 10 slots above the status panel, items or spells.
- **Controls:**
  - Left-click or 1-0 uses a slot. An empty slot's digit still types a
    count.
  - Clicking an empty slot, or right-clicking any slot, opens a picker:
    inventory, known spells, Clear.
- **Actions:** queued for the core through cmdq plus
  vulture_click_consumed (as the s72 popup does):
  - potion: quaff; scroll/book: read; wand: zap
  - food: eat; ring/amulet: put on/remove; armour: wear/take off
  - missile/ammo/stack, gem: throw
  - other weapon: wield; anything else: apply; spell: cast
- **Display:** shows quantity, known wand charges and spell Pw cost. A
  slot dims when its item is gone or Pw is short, and flashes on use. A
  used-up stack refills from the same otyp.
- **Saving:** per character in
  %LOCALAPPDATA%\NetHack\5.0\hotbar-<name>.txt.
- **Self-test probe:** VULTURE_SELFTEST=<dir> (off unless set) stages
  the bar, path, ground card, hover card and aim line, and saves BMPs.
  Verified off-screen.

### 127. Camera starts locked; Discord hover text

- vulture_cam_follow now starts true: the camera follows the hero until
  you pan it, and a double-click on the portrait resumes following.
- The presence large_text is "NetHack". The "Playing ..." title is the
  Discord app name, which the user renamed in the Developer Portal.

### 128. A left click stops travel

- vulture_esc_cancels_travel also stops travel/run/multi/occupation on a
  queued left-button press.
- It flushes the mouse-button events, so the click never becomes a new
  travel target.
- If the button-up hasn't arrived yet, levelwin swallows the next left
  button-up (vulture_swallow_lclick_up, expires after 1.5 s).

### 129. Polish: hit-stop, status pulse, fog distance markers, auto-pickup checklist

- **Hit-stop:** a monster losing at least 40% of its max HP in one blow
  (and at least 4) gets a gold damage number, an impact burst (vfx kind
  6) and a 70 ms frame hold (vhitstop_ms, applied in vulture_tween_run).
  Taking a quarter of your own max HP does the same in red, held 55 ms.
- **Status pulse:** newly becoming confused, stunned, blind,
  hallucinating, sick, stoned, slimed, strangled or Weak flashes the
  screen edges in that status's colour, and a label rises off the hero.
- **Fog markers:** hovering an unknown or unreachable square draws a faint
  straight line and a "~N" distance. While aiming, the pointer square
  shows its range.
- **Auto-pickup rules:** right-click your own square -> "Auto-pickup
  rules..." opens a checklist (vulture_autopickup_menu in pickup.c).
  - It sets flags.pickup and pickup_types.
  - If nothing is ticked, auto-pickup turns off, rather than falling back
    to the game's "everything" default.
  - Saved with the game, not in the rc file.

### 130. Auto-fight, attack-move, rest until healed, "In sight" panel

- **Tab:** one blow (or a step along the known path) at the nearest
  visible hostile. Shift+Tab now opens the overview map.
- **Shift+A then click:** attack-move. Each turn it fights a hostile
  within 8 squares, otherwise steps toward the spot. It stops below 1/3
  HP, on arrival, or when there is no way.
- **F5:** rest until HP and Pw are full, unless an F5 macro is set. It
  searches turn by turn and stops when a hostile comes into view, or when
  you're Weak.
- **How they run:** vulture_auto_step, called from the input loop, queues
  one canned command per turn.
  - Any key or click takes over: Esc is eaten, and a left click is eaten
    and swallowed.
  - It gives up after 4 actions that take no game time.
- **Buttons:** FIGHT / A-MOVE / REST sit beside the ability bar, with
  tooltips.
- **"In sight" panel (top right):** visible monsters, hostiles nearest
  first, then peacefuls and pets.
  - Each row has a threat-colour dot, name, and a health bar when hurt or
    tame.
  - Hovering a row rings the monster on the map; clicking looks at it.
- The old left-side "Hostiles in view" box (s86 vw_draw) is disabled.

### 131. Audio feedback pass

- 26 new effects synthesized in pure Python (scratchpad make_sfx.py), so
  there are no samples and no licensing questions. 22 kHz mono, in
  sound/.
- **UI:**
  - ui_hover: a tick when the pointer moves onto another bar slot,
    button, panel row or the portrait (rate-limited).
  - ui_click: bar use, buttons, portrait.
  - ui_open / ui_assign / ui_clear: the slot picker.
  - ui_error: "No enemies in sight".
  - ui_cancel: aim cancelled or an auto action stopped.
  - ui_lock: camera follow.
  - ui_zoom: wheel zoom.
- **Click markers:** each order has its own sound:
  - mk_move: tick-whoosh
  - mk_attack: blade ring
  - mk_ally: bell
  - mk_item: sparkle
  - mk_door: knock-knock
  - mk_stairs: low swoosh
  - mk_drink: bloops
- **Aiming:** aim_ready when the aim layer or attack-move opens;
  aim_fire on click.
- **Combat and status:**
  - hit_big: thump + crunch with the hit-stop.
  - status_bad: a dissonant sting with the status pulse.
  - alert: a two-note cue when an enemy you haven't seen on this level
    comes into view (at most every 1.2 s).
- **Auto actions:** amove_go, rest_start, rest_done.
- **HUD:** rustle for the ground card, card_whoosh for the level title.

### 132. More SFX (second batch)

- 22 more synthesized effects (toolchain/make_sfx2.py).
- **Melee:** swing1-3, a whoosh on every lunge (vulture_anim_attack).
- **Spell schools:** cast_attack / heal / div / ench / cler / escape /
  matter, played in spelleffects by spell_skilltype.
- **Pick-up by kind (pick_obj):**
  - coins
  - pk_potion: clink
  - pk_scroll: paper
  - pk_weapon: metal ring
  - pk_armor: clank
  - pk_gem: click, also rings and amulets
  - pk_food
- **Message-triggered (vulture_sounds.conf, game and release copies):**
  - fizzle: "fail to cast"
  - squeak: squeaky board
  - snap: bear trap
  - teleport: "wrenching sensation"
  - dart_thunk: dart and arrow traps
  - glass: "shatter"
  - bell: a shopkeeper's "Welcome to"

### 133. Dynamic lighting

- **Light levels (vl_compute):** a per-square light level.
  - Base: lit and in view 0.93, dark and in view 0.38, remembered 0.50,
    unknown 0.
  - Added on top:
    - every visible light source from gl.light_base, flickering: objects
      warm amber, monsters pale gold
    - lava (orange, slow pulse)
    - squares holding a zap or explosion glyph (MAP_SPECIAL, cool white)
    - the hero's own faint glow
    - decaying flashes (vl_flash: impact bursts)
  - Each source adds (1 - d/(r+1))^2 falloff, only through clear_path and
    only onto squares you've seen.
- **Overlay (vl_render):** sampled at 1/4 resolution in map space through
  the inverse projection and view rotation, bilinear between square
  centres. It's turned into a darkness + colour-tint ARGB layer,
  stretched with SoftStretchLinear and blended over the map.
  - Drawn under the s86 glows, vfx, markers and HUD, and before the zoom,
    so it scales with the map.
- **Cost:** about 4.8 ms per rebuild at 800x600.
  - The layer is cached with a 96 px margin and slides with the camera
    during glides.
  - It rebuilds at most every 90 ms (flicker), or when the
    view/hero/turn/rotation/size changes, or while a flash is live.
- **Toggle:** F9.
- **Probe:** VULTURE_SELFTEST_DARK=1 (with VULTURE_SELFTEST) unlights the
  level and fires a flash for screenshots.

### 134. Brogue notes: combat odds, one-key stairs, clickable inventory, EXPLORE

- **Combat odds on the hover card:** for a hostile, the card shows "You
  hit it N% of the time" and "It hits you N%, ~M% of your health".
  - Your chance is find_roll_to_hit with your weapon, at (roll - 1) x 5.
  - Its chance uses mattacku's AC + 10 + level; negative AC uses the
    average of its random roll.
  - Damage is the average of its physical attack dice, as a share of your
    current HP.
  - The second line turns orange when expected damage per turn is 10% of
    your HP or more, red at 25% or more.
- **One-key stairs:** '>' or '<' away from matching stairs travels to the
  nearest known staircase or ladder that way, using the game's own travel
  (new vulture_travel_to in cmd.c). On the stairs it behaves as usual.
- **Clickable inventory:** a left or right click on an item opens its
  actions.
  - Only sensible ones, likeliest first: potion Drink; scroll/book Read;
    wand Zap/break; food Eat; ring/amulet Put on/Remove; armour
    Wear/Take off; weapon Wield/Throw/Quiver; tool Apply (+ Put on for
    blindfold/towel/lenses, Wield for weapon-tools); gem Throw/Quiver.
  - Then Throw, Remove, "Add to ability bar" (vulture_hotbar_bind: its
    slot, else the first empty one), Drop, Name.
- **EXPLORE:** a fourth button runs doautoexplore. The buttons are now
  2x2, and the bar shifts left so it and the buttons always fit.
- **Probe:** VULTURE_SELFTEST also spawns a jackal next to the hero, so
  the odds can be checked.

### 135. Brogue batch 2: square cards, wake-up "!", explore collects, 5-turn search

- Hovering an item or a dungeon feature (stairs, fountain, altar, sink, throne, grave, tree, closed door, known trap) shows a card: its name plus what you can do with it (`vsq_hint`/`vsq_card_draw` in levelwin.cpp); the plain tooltip is skipped for those squares.
- A visible hostile that goes from asleep/helpless to awake gets a "!" bubble and the new `spotted` sting (toolchain/make_sfx3.py), tracked per level in the s131 alert block.
- Auto-explore reaching an item: if auto-pickup is on, the new canned `autoexplore_collect` picks up what the pickup rules want and, when nothing is left on the square, keeps exploring (`aex_resume` keeps the leg count). Items auto-pickup skips still stop it, as before.
- Plain `s` searches 5 turns (`vulture_search_more`, the shared `vq_search_occ` occupation, Esc or a monster stops it); a typed count works as before.
- Probe: `VULTURE_SELFTEST_ITEM=1` drops a long sword on the hover square.

### 136. Brogue batch 3 + immersion: monster intent, loot glints, search pulse, dungeon ambience, ~40 message sounds

- IN SIGHT panel (Brogue sidebar): each row says what the monster is doing - asleep / trapped / fleeing / hunting (it can see you) / wandering; long names are trimmed to fit. The panel now starts below the minimap (the minimap window was drawing over its top rows).
- Magic loot in view (potions, scrolls, wands, rings, amulets, spellbooks, gems) glints every ~1-2 s (`vglint_tick`, vfx kind 7).
- Every explicit search turn sends a blue pulse ring out from the hero with a soft sonar ping (`vulture_search_fx`, called from `dosearch0` when `!aflag`; vfx kind 8).
- The dungeon breathes: every 18-48 s a quiet faraway sound (drip, creak, chains, rumble; howls from depth 5; moans and more rumble in Gehennom) plays from a random direction (`vamb_tick`, positional).
- Melee swings are positional (heard from the attacker's side).
- A blow taking 10%+ of max HP flashes the screen edge red (`vhurt_flash`, reuses the status pulse).
- toolchain/make_sfx4.py: far_drip/creak/chain/rumble/howl/moan, search_ping, eat_chew, skill_up, gain, room_sting, web, scribble. vulture_sounds.conf: eating, skill advance, ~14 intrinsic gains, special-room entries, temple feelings, prayer, altar flashes, camera flash, webs, engraving, "Nothing happens", wresting, distant noises, fleeing. The repo copy (gamedata/config) re-synced with the game folder (it was stale since log 95).

### 137. Stealth and sleep, communicated (Brogue-style), no rule changes

Audit (vanilla rules, unchanged): `disturb()` wakes a sleeper that can see you within 10 squares unless you are Stealthy (ettins excepted); dogs and humans at once, others 1 in 7 a turn; nymphs/leprechauns/jabberwocks rarely. Entering a zoo/court/swamp/morgue without Stealth wakes ~1/3. Sneak attacks get +2 to hit (asleep), +4 (helpless); Rogues backstab for +d(level). Plain melee is silent; kicks, locks, digging, horns etc. call `wake_nearto`. None of this reached the screen.

- Stealth badge above the portrait: STEALTHY / NOISY / AGGRAVATING (Aggravate only when its source is known, so no unidentified-ring leak); hover says why (nature, worn gear, riding cancels it) and what it means.
- Sleepers show drifting "z Z" over their heads; helpless (paralysed) monsters get circling stars. Threat colours on the card and the IN SIGHT dot are dimmed while asleep.
- Hover card for a sleeping/helpless hostile replaces the misleading "It hits you N%" with: hit chance + "sneak attack, +2/+4" (+ "backstab" for Rogues), "Asleep - it won't fight back until it wakes" / "Helpless for about N more turns", and a wake line from the real rules (stealthy / ettin / deep sleeper / aggravating / wakes on sight / 1 in 7), coloured green/amber/red.
- Payoff: `do_attack` records a target that was asleep/helpless (`vulture_sneak_mid` + move); if `xkilled` gets it the same turn -> "SILENT KILL" popup, violet burst, hit-stop, `silent_kill` sound. Rogue backstab -> "BACKSTAB" popup + sound.
- Noise: `wake_nearto_core` -> `vulture_noise` draws a ripple sized to the real waking radius (within 16 squares); visible monsters it woke get "?" (and the `wake_q` sound) instead of "!" (seen you); unseen hostiles it woke -> "You hear something stir." The monster you just attacked gets no bubble.
- Special-room entry (zoo, court, beehive, barracks...): "Your entrance wakes N of them - you are not stealthy; M still sleep." / "You slip in unheard. M of them sleep on." / "M of them are asleep. Tread carefully."
- Sleepers are opportunities, not alarms: the new-hostile alert plays a soft `sleeper_spot` cue for them; auto-explore says "A sleeping X is in view - sneak past it or strike first."; FIGHT/Tab announces "Sneak attack on the sleeping X!".
- toolchain/make_sfx5.py: silent_kill, backstab, sleeper_spot, wake_q. Probe: `VULTURE_SELFTEST_SLEEP=1`.

### 138. Preview area effects before casting

Only for effects the hero knows (an unidentified wand gets the plain aim line, so nothing leaks). `vulture_aim_mode`/`vulture_aim_dmg` (zap.c) are set around the prompt: `vulture_getdir_ray()` wraps getdir in `dozap` and in `spelleffects`; the skilled fireball / cone of cold branch sets mode 3 around `throwspell()` (cleared by `vulture_aim_end`).

- Rays (mode 1: magic missile, fire, cold, sleep, death, lightning; wand or spell): `vray_path` follows the ray over KNOWN ground only (unexplored squares end the preview: "Beyond that: unexplored"), bouncing with a deterministic copy of zap.c's `bounce_dir` (the 1-in-75 bounce-back and the corner coin flip are luck). Squares are outlined in the damage colour, solid to 7, fading to 13 (range is rn1(7,7)). Rings on everything it reaches; card: what it is, "Hits: ...", "Careful: your kitten is in the way!", "It bounces back at you!".
- Unskilled fireball (mode 2): flies without bouncing and bursts 3x3 at 7-13 or before the first obstacle; every square it can burst over is outlined (bright when the wall makes it certain).
- Skilled fireball / cone of cold (mode 3, getpos): the 3x3 core at the pointer (bright) and the 5x5 spill-over ring (faint) - 2-9 bursts, later ones re-centred within 1. Warnings: over 10 away (dissipates), unseen / wall spot (mind won't lock on), you in the blast, a spill-over can reach you, a friend in the core or spill. Hover cards and the travel path stay hidden meanwhile.
- Probe: `VULTURE_SELFTEST_AOE=1` (selftest_aim = fire ray at a hostile, selftest_blast = skilled fireball). Note: probe frames can show a panned camera - the probe parks the mouse near the screen edge and edge-scroll moves the view (that was log 136's "black frame").

### 139. Item stat cards + one-click altar test

Already present and on in the new-player rc (no change): `autoid` shop price identification and wand engrave-ID calls, `price_quotes`, pet-reluctance cursed marking (§63), autounlock, autoquiver.

- Inventory hover (objitemwin `handle_timer_event` -> `vulture_item_info`, levelwin.cpp): weapons show damage dice vs small/large (+enchantment when known), two-handed, skill and your level in it, and average-damage difference against your wielded weapon; armour shows AC (base + known enchantment), slot, magic cancellation, and what you wear in that slot now; food shows nutrition and eating time; spellbooks their level; always curse status if unknown, wand charges if unknown, and weight. Only what the hero knows: dice/AC only for a type whose identity is known or that has no random appearance.
- Tooltips can now have several lines (vulture_mou.cpp splits on newlines).
- Altar test: standing on an altar, the self-click menu offers "Test my unknown items on the altar" (`vulture_altar_test`, pickup.c): drops every carried item with unknown curse status (not worn/wielded, coins, ball/chain, loadstones; refused when levitating or blind), each gets the normal altar flash, items are kept from merging (`nomerge`, cleared by addinv), and a queued canned command picks exactly those back up next turn with a summary ("4 items - 1 cursed, 2 blessed, 1 uncursed"). The altar hover hint mentions it.
- Probe: `VULTURE_SELFTEST_ITEMS=1` dumps items.txt (cards) and altar.txt (drop stage).

### 140. Time modes: real-time, real-time until combat (default), turn-based

- `vulture_rt_step` (levelwin.cpp), called from the map's command-wait loop beside `vulture_auto_step`: when you stand idle for `VRT_MS` (650 ms) it queues one plain wait turn (`vrt_wait`: no search, no safe_wait prompt). Nothing ticks in menus/prompts (a >300 ms gap between loop calls restarts the wait), farlook, aiming, multi-turn actions/occupations, auto actions, or with the window unfocused (`vulture_has_focus`, set from SDL focus events). Acting restarts the wait.
- Mode 2 (default): no ticks while an awake hostile is in view within 12 squares or right after you lost HP; resumes `VRT_RESUME_MS` (1.2 s) after the fight.
- Top-left pill: REAL-TIME (with a bar filling toward the next turn) / COMBAT - TURN-BASED / PAUSED / TURN-BASED. Click it or press Pause to cycle (every F-key was taken); kept per Windows user in `%LOCALAPPDATA%\NetHack\5.0\timemode.txt`; missing file = mode 2.
- Resting (REST button) no longer pulses/pings every turn (`vquiet_search`).
- **Bug fix (affects logs 129, 135, 139):** `cmdq_add_ec` only finds functions listed in `extcmdlist`; anything else got a NULL entry and rhack() dropped it with a beep. So the "Auto-pickup rules..." self-menu entry, explore's auto-collect and the altar test's pick-up never ran. `cmdq_add_ec` now gives an unlisted function a synthetic table entry (up to 24), so every queued interface action runs.
- Probe: `VULTURE_SELFTEST_RT=1` (+ `VULTURE_SELFTEST_NOJACKAL=1`) writes rt.txt: real-time advanced turns 3->6 in ~3 s; hybrid with an awake jackal adjacent held at 0 ticks.
- Groundwork only for multiplayer (no pause/time-scale in MP per the co-op decisions) - nothing networked yet.

### 141. The codex: monster, corpse and trap knowledge in-game (Pathos-style, no wiki)

Inspired by Pathos: NetHack Codex's "comprehensive inbuilt help on all items, entities, traps". All from the game's own data (mons[], trap types).

- Monster hover card, hold Shift: level, AC, MR, speed vs yours (Fast/Very fast counted), every attack with dice and effect (claw 1d6 (paralyzes), passive: PETRIFIES...), resistances, traits (flies, swims, regenerates, follows you between levels, sees invisible), and what eating its corpse does. Without Shift it shows "Hold Shift for full details" and, if eating it would be deadly, that one line. Shift press redraws the card.
- Corpses (`vcorpse_info`): fatal (Riders), petrifies, slimes, lycanthropy, cannibalism (cave dwellers and orcs exempt), pet species aggravate, polymorph, poisonous (says if you resist), acidic (cures stoning), lizard/lichen never rot, hallucination, stun, speed toggle, disenchanter, newt energy, mind flayer Int, and "May grant:" from mconveys (only the six resistances eating can give) plus telepathy / teleportitis / teleport control / strength. Shown on the monster card, on the inventory card of a corpse (+ "eat it fresh"), and on the floor-corpse hover hint ("If eaten: ...").
- Traps: the hover hint names the trap and what it does (all 23 types) instead of "A known trap".
- Probe: `VULTURE_SELFTEST_CODEX=1` writes codex.txt and forces Shift for the card shot.

### 142. Danger preview, explore reasons, first-time tips, real-time speed

- Travel preview (hovering a square): "N can hit you there" (awake hostiles that would be adjacent) and red rings on known traps along the path ("a trap on the way").
- Auto-explore says why it stopped ("You stop exploring: a jackal is in view." / "...badly hurt.") and, when the level is done and the down stairs are known, "Level explored. Press > to walk to the stairs down."
- First-time tips: 12 short tips (welcome, sleepers, shops, altars, fountains, hunger, low health, traps, burdened, stairs, unidentified items, real-time), each shown once ever per Windows user (`tips-seen.txt`), one at a time under the time pill for 11 s. Click to close, right-click to turn tips off for good.
- Real-time speed: right-click the time pill cycles normal 0.65 s / slow 1.0 s / fast 0.4 s per turn (kept as the second number in timemode.txt); the pill tooltip shows mode and speed.
- Gear sets: not built - wearing armour is multi-turn with prompts and ordering rules (cloak over suit, etc.); needs its own design pass.

### 143. Richer dynamic lighting: glowing features, elemental monsters, contact shadows

- Features light their surroundings: fountains (shimmering blue), pools/water (faint moving blue), altars by alignment (lawful gold, neutral silver, chaotic red, unaligned violet), thrones (gold), sinks, graves (faint green); traps: magic portal (strong violet pulse), teleport/level-teleport/magic/polymorph/anti-magic (violet pulse), fire trap (orange).
- Monsters with fire/shock/cold auras (passive, engulf, explode, or elementals) cast flickering orange / crackling blue-white / icy light.
- Contact shadows: floor squares next to walls are a little darker (5.5% per wall side), for depth.
- Probe: `VULTURE_SELFTEST_LIGHT=1`; probe screenshots now re-centre on the hero first (`VULTURE_SELFTEST_NOCENTER=1` to skip) - this fixes the panned/black probe frames.
- NOT verified visually: the random probe levels never placed the test altar/fountain where the frame could show them; only contact shadows and fire light were seen.

### 144. Searching vs resting icon; speed made visible (threat zones, speed badge)

- Above the hero for ~1 s after each turn of it: a sweeping magnifying glass + "searching" (explicit `s`, incl. the 5-turn search) or an hourglass + "resting" (the REST button, `.` and counted rests via a hook in `donull`). Real-time idle ticks show nothing (the time pill covers that).
- Speed model (from u_calc_moveamt / mcalcmove): your speed = form speed, +4 average when Fast (extra action 1 turn in 3), +8 when Very fast, x3/4 / 1/2 / 1/4 / 1/8 for Burdened..Overloaded; a monster's = `mcalcmove(m, FALSE)` (haste/slow included). Ratio = its actions per one of yours.
- Threat zone: hovering a hostile tints every square where it can reach you AND hit you before your next action (path length over known open ground <= its actions): red = every time, orange = only with a lucky extra move (fractional speed; slow monsters threaten their neighbours in orange). Hold Alt: the combined zone of every awake hostile within 14 squares (Alt/Shift presses redraw).
- Monster card (compact and Shift): "Moves 2.0 squares per move of yours - you can't outrun it" / "Moves once per 1.5 of your moves - you can outrun it", orange when faster than you.
- Speed badge beside the stealth badge: SPEED: NORMAL / FAST / VERY FAST / SLOW; tooltip gives actions per turn, the Fast/Very fast rule, load warning, and the Alt hint.

### 145. Gear sets, pause menu, click-travel fixes, surface relief lighting

- **Gear sets** (SET 1-3 beside the speed badge, Ctrl+1..3). Click an empty
  set (or right-click any) to save what you wear and wield; click a saved set
  to change into it.  Core: `vulture_gear_begin/pending/act/missed` in
  do_wear.c plan ONE stock action per call - blockers come off through the
  'A' takeoff occupation (`select_off` + `take_off`, so order, delays, curses
  and interruptions are stock), then the weapon (`vulture_ready_weapon` in
  wield.c, swapwep/quiver aware), then each missing piece
  (`accessory_or_armor_on`; a ring's hand is pre-answered with a queued key,
  popped again if never asked).  Anything that fails is skipped, never
  retried.  Driver: VAUTO_GEAR in `vulture_auto_step` (NOT a self-requeueing
  cmdq chain: `nomul()` clears CQ_CANNED, so multi-turn armour would wipe
  it).  Stops when hurt or when the takeoff is interrupted.  Saved per
  character in gearsets-<name>.txt by o_id, tagged with ubirthday.  Tip
  "gearsets" once you carry spare armour.  Probe VULTURE_SELFTEST_GEAR:
  coat under the cloak + helm + dagger went on in 5 turns, set flagged worn.
  Bug found on the way: the de-dup loop's bound grew with the counter it
  advanced and read an uninitialised pointer (crash) - fixed.
- **Pause menu on Esc** (`vpause_menu`): Resume / Game options / Interface
  options / Time mode (cycles) / Help and keys / Save and quit / Quit without
  saving.  Game commands are queued (doset, dohelp, dosave, done2); Esc first
  stops a running FIGHT/REST/gear.  The old `vulture_show_mainmenu` was never
  called and is left alone.
- **Trail vanished after a click** until Esc: rhack() clears context.travel
  only at the START of the next command, and context.run stayed 8 after
  arriving (probe: travel=0 run=8 multi=0).  The preview now hides only while
  `gm.multi || go.occupation`.
- **Click-travel looked like teleporting**: runmode "leap" (the default)
  redrew every 7th step and the walk tween only glides one-square moves, so
  most steps snapped.  `runmode_delay_output` now redraws every step in leap
  mode too; each step is the 90 ms glide.
- **Surface relief** (normal-mapped lighting, `vl_relief`): per frame,
  texture brightness is read as height, its slope gives a normal per pixel,
  lit from the light field's own gradient (towards the lamps/fires/glows on
  the coarse grid) plus a faint top-left key where lit.  Integer, only over
  each row's lit cells, delayed row write-back so heights come from the
  untouched frame.  ~6 ms at 1x in the probe (11 ms before the span pass).
  F9 cycles full / flat / off.  Normal maps are derived, not shipped: no new
  assets.
- s144 checks done: hourglass icon, Alt danger map (ant: red ring + orange
  outer ring), lichen orange-only ring all seen in probe shots.

### 146. Critical strikes (Brogue): sneak attacks, ambushes, lucky crits

- `vulture_crit_apply` (uhitm.c), right after `hmon_hitmon_dmg_recalc`, for
  every hero weapon/hand hit except kicked objects:
  - **sneak attack** - target asleep or unable to move: x3, and a melee
    blow on it always hits (hitum: `mhit = TRUE`);
  - **ambush** - awake but `vulture_unaware()`: it can't see you
    (`!m_canseeu`) and its idea of where you are is wrong (mux/muy): x2;
  - **critical hit** - otherwise `vulture_crit_chance()` = 3% + 2% per Luck
    (0 at Luck <= -2, cap 35%): x2.
  The biggest applies, no stacking.  Melee reads the target's state saved
  in `do_attack` (vulture_sneak_mid / new vulture_ambush_mid) before the
  blow wakes it; thrown and fired weapons read it at impact.
- Feedback: "Sneak attack!  (triple damage)" / "You ambush the X!  (double
  damage)" / "Critical hit!  (double damage)", a rising word (SNEAK ATTACK
  x3 / AMBUSH x2 / CRITICAL x2), sound and hit-stop (`vulture_crit_fx`).
  Rising words and damage numbers on one square now stack (`sub` slot)
  instead of overprinting - a sneak-attack kill shows both SNEAK ATTACK and
  SILENT KILL.
- The monster card spells it out: sleepers "Sneak attack: always hits,
  triple damage"; awake hostiles "You hit it N%  -  critical hits M%", or
  "it hasn't noticed you: AMBUSH x2".  The crit chance shows your Luck's
  effect - deliberate, per the no-wiki goal.  Sleeper tip text updated; the
  auto-fight's own "Sneak attack on..." line removed (the core says it).
- Probe VULTURE_SELFTEST_CRIT (+SLEEP: target paralysed so the kitten can't
  wake it) presses Tab for real: two sneak attacks logged on consecutive
  turns (crit.txt kind 1); without SLEEP (hero invisible, target lost
  track) an ambush logged once (kind 2); a run where both swings missed
  logged nothing, as it should.

### 147. Fluid motion: time-based tweens, hurry-not-snap, -O2 build

- **The build was unoptimised.**  Neither the core (sys/windows/GNUmakefile
  `CFLAGS += -DNDEBUG`) nor the Vulture interface (`BASECFLAGS="-Wall -g"`)
  had any -O flag.  Both now get `-O2` (still `-g`).  The exe went 6.3 ->
  5.3 MB; a fresh lighting layer 17-40 ms -> 5-6 ms; surface relief 7 ->
  1 ms per frame; a step now draws up to ~20 frames in its 120 ms.  The old
  objects (src/o/nethackv, win/vulture/build_nethack_3_6_0) were moved to
  deliverables/release/trash/objs-O0-* - make doesn't track flag changes,
  so a flag change needs those moved for a full rebuild.  All 12 probe
  configurations pass on the -O2 build; gear sets still go on in order.
- **Time-based tweens** (`vulture_tween_run`, `vanim_frames`): every step,
  travel glide, lunge, death and drop plays over a fixed time and renders as
  many frames as fit, instead of a frame count with SDL_Delay(5-14) between
  (the log showed steps at 4 frames/50 ms or 9 frames/172 ms).  Steps 120 ms
  (was ~172), travel glide 90 ms, lunge/death/drop capped at 170 ms.
- **Hurry, don't snap:** a key or click already waiting speeds the rest of
  the animation to ~35 ms (lunges ~30 ms) instead of jumping to the end, so
  a held direction key walks in one continuous glide.
- **Ease-out** steps (`t*(2-t)`) instead of smoothstep: motion starts the
  instant you press.  Hop 12 -> 6 px (runs 5 -> 2.5); squash & stretch 18% /
  26% (the squash lived in the last 24 ms = a one-frame pop) -> 5% stretch
  and a 7% settle over the last third; lean 10 -> 5 deg; runs and held keys
  get 45% of that.
- **Idle effects at ~30 fps** (was ~12): vidle_interval floor 80 -> 33 ms,
  still at most a third of the time; input-wait timeout 40/20 -> 30/15 ms.
- Light grid cells 4 -> 8 px (pools are bilinear across 56 px squares);
  relief luminance rows cached and reused (3 rows -> 1 per row).
- Probe VULTURE_SELFTEST_WALK queues six movement keys (held-key walking);
  the lighting stage breakdown is logged under VULTURE_SELFTEST.

### 148. Blue click marker on squares you can't move into

- `vfx_click_at`: a click on a square you can't walk into - wall, boulder,
  statue, iron bars, tree, water, lava (`!vmv_pass` or a statue glyph) -
  now gets a blue VCK_BLOCKED marker instead of the green move one.
  Monsters, items, doors, stairs and fountains keep their own markers.
- Probe VULTURE_SELFTEST_MARK: wall and boulder sub=7 (blue), plain floor
  sub=0 (green), seen in the shot.

### 149. Real normal maps; magic that pulses

- **Normal maps for every tile** (2405 PNGs, 23 MB) in gamedata/tiles_n/,
  same names as tiles/, made by toolchain/make_normal_maps.py (numpy +
  Pillow + SciPy; ~10 s).  RGB = screen-space normal (x right, y down, z to
  the viewer), alpha = the tile's.  Height model: floors/edges = fine
  texture only (alpha-weighted high-pass of brightness - the unweighted
  blur drew a grid along tile edges); walls the same, coarser, slight bevel;
  sprites (monsters, objects, statues, features) texture + a rounded bevel
  from the silhouette (distance transform), so they light like volumes.
  cursor.* skipped.
- **Normal buffer** (vulture_tile.cpp): `vnm_begin()` clears a screen-sized
  ARGB buffer before the map loops and turns `vulture_normal_capture` on;
  `vulture_put_tile_shaded` also blits the tile's normal map there, and
  `vtw_put_tile_x` draws a monster's normal map with the same scale, lean
  and flip (a mirrored copy with x negated).  Normal-map alpha is copied
  from the colour tile as loaded, so see-through walls blend correctly.
  Capture stops before `vl_render`.  Cached per tile id, flipped separately.
- **Lighting from the normals** (`vl_nmap`, replaces the luminance relief
  when the buffer is good): per pixel, n . L with L from the coarse light
  field's gradient (towards lamps, fires, glows) plus a top-left key (0.45
  of the light level, was 0.22); faces tilted away darken in proportion to
  the local light (mortar, folds, gaps); faces squarely lit get a small
  additive glint.  ~1-2 ms at 1x on the -O2 build.  F9: full (normal
  mapped) / flat / off.
- **Magic pulses**: magic portals and teleport / level-teleport / magic /
  polymorph / anti-magic traps get light that beats on three frequencies
  with a rare surge, radius breathing with it, and a drifting colour per
  kind (portal violet-cyan, magic trap a wandering hue, poly green-magenta,
  anti-magic cold grey-blue) - `vmagic_light`.  While any is in view the
  light layer refreshes every 33 ms (else 90) so the pulse is smooth.
  `vmagic_motes`: glowing motes rise and sway over magical traps and swirl
  up around portals, in the trap's colour.
- Probes: VULTURE_SELFTEST_CMP takes a relief-off shot of the same frame
  in a normal lit room; LIGHT also places a magic and a teleport trap by
  the hero and logs every trap.  Lit room: carved floor and wall faces gain
  depth; by the fire vortex the stones catch its light; motes seen over the
  magic trap.
- Also: autosokoban had vanished from the player's two rc files (rewritten
  11-20 21:29 after log 84 enabled it); added back (outside the repo).

### 150. A footstep per step; scrolls and potions always identify

- **Footsteps** (hack.c `domove`): the sound used to need DOMOVE_RUSH /
  DOMOVE_WALK in domove_succeeded, which only a command's FIRST step has,
  so travel and click-walks played one footstep per click.  Now any move of
  exactly one square on the same level plays one (not while levitating,
  flying, riding or underwater; a splash in water).  Probe (CLICK): a
  4-square walk logged 4 footsteps.
- **No more "Call a scroll labeled ...:" / "Call a ... potion:"** -
  `vulture_read_autoid` (read.c) identifies the item, gives the usual
  discovery XP and says "(That was a scroll of taming.)".  Used where doread
  and dodrink used to `trycall` (effect unclear), by `strange_feeling` for
  scrolls and potions, and by potion vapours (`potionbreathe`).  A thrown
  potion shattering out of reach still only asks (no free ID-by-throwing).
  Probe VULTURE_SELFTEST_READ: unclear taming / food detection reads and a
  see invisible quaff all came back identified with the message.

### 151. Normal maps fixed (they looked broken in real play); weight and impact

- **What went wrong with 149**: tuned from small 800x600 probe shots.  At
  the player's real settings (1920x1080 fullscreen, wall_opacity 0.3 - now
  copied into the probe profile's vulture.conf, fullscreen off) it drew a
  bright diamond grid over every unseen square and a crumpled-foil sheen on
  every lit floor.  Causes and fixes:
  - misc.floor_not_visible / floor_highlight / off_map were treated as
    sprites and got a silhouette bevel, lit by the room key light -> their
    normal maps are now alpha 0 (never lit);
  - floor and wall normal maps had a ridge along the tile edge (height
    meets transparency) -> slope zeroed in a 2 px band along the edge;
  - strengths halved (floor 5 -> 2.2, wall 4.5 -> 2.5, sprite 4 -> 2.8,
    sprite bevel 0.6 -> 0.35);
  - runtime: no relief on remembered ground or away from a real light
    (L < 0.6 and light-gradient strength < 0.15), key light 0.45 -> 0.30,
    gain halved, crevices a third and only near a real light, glints only
    from a real nearby light and capped at +24, total clamp +-60.
  Measured in the dark around a room: same mean brightness as flat (4.9).
- **Weight** (`vheft_mon` by size / corpse weight, floaters and flyers
  light; `vheft_obj` by owt): heavy monsters hop less (0.3x) and land with
  a harder settle, tiny ones spring (1.4x); a heavy monster's step, a body
  falling (death anim) and an item landing queue `vimpact` with a delay so
  it fires on contact: a dust puff along the floor (vfx kind 10), a thud
  (impact_heavy or drop, by heft, positional) and a short non-blocking
  camera jolt (damped bounce added to the camera offset, fading with
  distance).  Dropped items bounce by weight (feather 1.5x, heavy 0.05x).
  Probe VULTURE_SELFTEST_HEAVY: a mastodon's death fired a heft-5 impact
  with a 3 px jolt.

### 152. Crash fix (impact jolt); lighting you can see; lighter light layer

- **The crashes** (3 in the player's Event Log, all SDL2.dll+0xdec20, an
  access violation in a blitter): reproduced at 1920x1080 by the HEAVY and
  WALK probes; a symbols build (LDFLAGS -s dropped temporarily) under gdb
  showed `vulture_put_tile_shaded(x=-2147483640, y=2147483638)`.  Cause:
  `vimp_offset` computed `exp(-age / 70.0)` with an UNSIGNED age, so -age
  wrapped to ~4e9, exp() = inf, and the camera jolt sent the whole map 2^31
  px away whenever something heavy landed near you (a jackal corpse is
  enough).  Fixed with a (double) cast plus a +-40 px guard.  All crash
  cases and the probe suite now survive at 1920x1080.
- **Lighting visible again**: lit rooms 0.93 -> 0.80 (lights had nothing to
  brighten); the hero carries a warm, softly flickering pool (radius 3.6);
  normal-map relief halfway back up (1.5x gain, clamp +-80, glint cap 32,
  key 0.36).
- **Lighter light layer**: blended only where it covers the map
  (`vl_blit_visible`; the slide margins were blended too), margin 96 -> 64,
  rebuilds every 120 ms (was 90) / 50 ms with magic (was 33), normal buffer
  cleared only over the map.  Measured at 1920x1080 in the probe: frame
  ~17 ms median (tiles 9, light 8), idle frame ~26 ms - the light blend
  itself (~6 ms, SDL software blending of a full-screen layer) is the
  remaining cost; not solved here.
- Probe timing: per-stage frame log and idle-frame log under
  VULTURE_SELFTEST.  The probe profile now uses the player's vulture.conf
  (1920x1080, wall_opacity 0.3).

### 153. Light layer applied from its grid; normals only where lit

- `vl_apply` replaces SDL_SoftStretchLinear (to a full-screen layer) +
  SDL alpha blend: per map row the coarse grid is interpolated vertically
  once, then each FS-px cell is walked with a running darkness value; the
  tint is held per cell (it changes slowly); on the XRGB layout red+blue
  blend in one multiply, green in another; pure black pixels are skipped.
  A generic per-channel loop covers other layouts.  The full-size vl_full
  surface is gone.
- The map draw sets `vulture_normal_capture` per square: only squares with
  light >= 0.55 or within 4 of you lay normal maps (others are never
  relief-lit anyway).
- Measured at 1920x1080 (probe, player's vulture.conf): light rebuild 17 ->
  11 ms, idle frame median 26 -> 18 ms, tiles 9 -> 6 ms; per-frame light
  application ~8 ms vs SDL's ~6, so a whole frame stays ~17 ms.  Beyond
  this needs GPU rendering.

### 154-155. Step puffs, throbbing portals, blows land where you are, level-up burst

- **Movement particles** (`vstep_puff`): every step - yours and every seen
  monster's, not flyers / floaters / ghosts, not while you levitate, fly or
  ride - kicks up a small puff (vfx kind 10) on the square it left: dust,
  or a blue splash when either square is water.  Probe WALK: 10 puffs for
  the hero and the kitten.
- **The portal sprite itself pulses** (`vmagic_sprite`): magic portals and
  magical traps are drawn a second time on top with SDL additive blending,
  colour-modulated to the trap's drifting colour and alpha-modulated on
  the same three-beat-plus-surge pulse as their light.
- **Enemies hit the square you were walking into**: you and a monster can
  both act in one turn before the screen draws your step, so the lunge
  aimed at your new square while you were drawn on the old one.
  `vulture_anim_attack` now draws once (which notices the pending moves)
  and plays them out first.  (A block-scope extern for vulture_tween_run
  inside the extern "C" function got C linkage and broke the link - it is
  declared at file scope now.)
- **Level up** (`vulture_levelup_fx`, from pluslvl next to the toast): three
  golden rings ripple out from your feet, a soft beam of light rises from
  you, 40 motes spiral up with bright cores, the area flashes warm, LEVEL
  UP! floats up, and the "gain" sound; it follows you if you move.  Probe
  VULTURE_SELFTEST_LEVEL calls the real pluslvl().

### 156. Aimed blows at body parts (Fallout 1's called shots)

- **Ctrl+click an adjacent enemy**: a menu lists the body parts it has, each
  with your hit chance and what a hit there does; pick one and the blow is
  struck (`vaim_pick` sets vulture_aim_part / _mid / _moves and queues the
  step through vauto_step_toward).  The monster card's odds line now says
  "Ctrl+click to aim".
- Core (uhitm.c): `find_roll_to_hit` subtracts the part's penalty for that
  monster this turn; a melee hit runs `vulture_aim_effect` after the crit
  check, then the aim is spent (one blow):
  torso 0 normal; legs -3 slows it (mon_adjust_speed); arms -4 it drops its
  weapon half the time (possibly_unwield + setmnotwielded + mdrop_obj; not
  if cursed), else x1.2; groin -4 (humanoid, living) x1.2 and 50% stun;
  head -6 x1.5 and 1-in-3 stun; eyes -8 blinded 3-6 turns.  Penalties in
  d20 points (5% each).  `vulture_aim_has` hides parts the body lacks
  (eyes, head, hands, legs - not slithy / limbless / floating - groin).
  Sleeping or helpless targets are always hit, so an aimed sneak attack
  can't miss.  "AIMED!" (head / eyes) or "AIMED" rises over the target.
- Probe VULTURE_SELFTEST_AIM: a paralysed, armed (made hostile) hill orc;
  arms, eyes, legs with real Tab presses -> damaged, blinded, slowed (it
  kept its sword: the arms roll is 50/50 and fell to x1.2 damage); every
  aim spent; the menu shot shows each part with its chance and effect.

### 157. Multi-square moves glide; boulders and thrown things land with weight

- **Monsters snapped when they moved 2+ squares** between frames you see
  (fast ones do): vtw_prepare only tweened moves of exactly one square.
  `VTW_REACH` (3) now applies at all four places (mover detection, mover
  offsets, camera detection, camera offset): moves up to 3 squares glide
  along the straight line in the step's time; longer jumps (teleports)
  still snap.  A probe-only "glide dx,dy" log line marks them.  Not seen
  in the short probe runs (the fast ant never moved 2 between frames).
- **Weight, moving half**: `vulture_object_impact(x, y, owt)` - called when
  a pushed boulder rolls into its square (hack.c, after movobj) and when a
  thrown object comes to rest (dothrow.c) - queues the same dust / thud /
  jolt as landings (heft by weight, capped at 6 for a rumbling boulder).
  The probe could not push the boulder (movement keys follow the view
  rotation; the hero walked past it), so this hook is untested.

### 158-165. Shadows, less clutter, atmosphere, effect toggles, injuries, telegraphs, comparisons, run summary

- **158 contact shadows** (`vshadow`): a soft radial ellipse (cached per
  width) on the floor under every object and monster; monsters' follow the
  slide; flyers / floaters get a smaller, fainter one.
- **159 clutter pass**: the stealth badge shows STEALTHY / AGGRAVATING always
  but NOISY only while a sleeper is in view; the speed badge only when not
  normal; the SET buttons only once a set is saved or spare armour is
  carried; the In-sight panel only when something other than your pet is
  visible.  Badges lay out in a row that closes up (vbadge_row_*).
- **160 atmosphere** (`vambient_draw`): stateless particles over the map -
  dust motes in the dungeon, drips and spores in the Mines, rising embers in
  Gehennom; fade at the map edges.
- **161 Visual effects menu** (pause menu "Visual effects"): normal maps,
  shadows, atmosphere, step puffs, camera jolts and rising words each switch
  on/off; saved in effects.txt and loaded at the first map draw.
- **162 injury feedback**: monster cards add blinded / stunned / confused /
  slowed; stunned or confused monsters sway on the map.
- **163 telegraphs** (`vtele_*`): a ready (not on cooldown) breath or spit
  attacker lined up with you within 8 squares on a clear path shows a
  pulsing dotted line in the attack's colour and a warning spark, and its
  card says "Lined up with you - it can breathe fire!"; otherwise the card
  says it breathes in straight lines; gaze attackers that can see you get
  an eye ring.  Probe VULTURE_SELFTEST_TELE (red dragon 3 away): card line
  confirmed.
- **164 item comparison** (`vitem_compare`): armour "Instead of your X: AC n
  better/worse" (+ magic cancellation, enchantment caveat) or "AC n better -
  nothing worn there now"; weapons "+n damage vs your X" and a warning when
  a two-handed weapon would take your shield off; shown on inventory cards
  and on floor hover cards once the item's type is known.
- **165 run summary** (`vulture_run_summary`, end.c, before the end
  questions on a real death): how it ended (formatkiller), turns, deepest
  level, XP level, kills, damage dealt (summed in hmon_hitmon) and taken
  (losehp), best weapon and armour.  Probe VULTURE_SELFTEST_DIE shot the
  window.  (The insertion first split `staticfn` off really_done - fixed.)

### 166. Soak mode; the unseen features confirmed

- `vsoak_tick` (VULTURE_SELFTEST_SOAK, run by scratchpad soak.ps1): plays by
  itself through real input - tops up HP and food, Tab when an enemy is
  within 6, otherwise clicks the EXPLORE button, '>' twice when exploring
  stops moving you, a random step when stuck, Esc on a menu or prompt left
  up 2.5 s; logs "soak: turn / dlvl" every 30 s.
- 12 minutes at the player's 1920x1080 settings: no crash, turn 3331,
  353 lunges, 212 step tweens, 129 step puffs, 7 multi-square glides, 4
  heavy impacts.  (The bot ended up fighting in one spot on dlvl 2 for the
  last ten minutes - a limit of the bot.)
- Probe VULTURE_SELFTEST_PORTAL: a seen magic trap beside the hero; three
  frames show its sprite's colour and brightness changing - the pulse works.

### 167. Lighting on every core (GPU project, stage 1)

- The GPU move needs the map and the interface split into separate layers
  (the light belongs between them, and today everything shares one software
  surface) - see docs/GPU_RENDERING_PLAN.md.  First, a win that keeps the
  normal maps: both light passes treat each row on its own, so `vpar_rows`
  runs them in horizontal bands on up to 6 threads (std::thread; the light
  layer's scratch rows are now per thread; VULTURE_THREADS overrides the
  count).  <thread> is included before hack.h - NetHack's min/max macros
  break it otherwise.
- At 1920x1080 (probe, player's settings): lighting 9 -> 3 ms per frame,
  frame 15 -> 9 ms, idle frame 16 -> 10 ms; frames drawn in the same run
  244 -> 442.  No band seams; a 2-minute soak ran clean.

### 168. Frames presented through the GPU (GPU project, stage 2)

- `vgpu_reattach` / `vgpu_present` (vulture_sdl.cpp): with gpu=1 (the default;
  vulture.conf `gpu=`, and a "GPU presentation" switch in Visual effects that
  applies on restart) the window gets an accelerated SDL_Renderer; the game
  keeps drawing into an off-screen RGB888 surface (render_scale times the
  window) and each refresh uploads the changed rectangle to a streaming
  texture that the GPU draws - linearly filtered when render_scale > 1, in
  place of the CPU box filter.  The screen shake is done by the GPU too.  No
  accelerated renderer -> logged, and the window-surface path runs as
  before.  A window recreated by a fullscreen switch gets a new renderer
  (SDL_DestroyWindow freed the old one).  The death-cam's unused
  SDL_GetWindowSurface call is gone (it would fight the renderer).
- Probes: the dummy driver has no accelerated renderer -> "gpu: no renderer
  ... using the window surface" and play as before; VULTURE_GPU_SOFT=1 forces
  SDL's software renderer through the new path -> "gpu: renderer software",
  a full fight probe ran and the frames are correct.  The accelerated path
  itself (Direct3D / OpenGL) needs a real window - untested here.

### 169-171. Smooth sprites, the photo-album polaroid, halos and living water

- **169 sprite transformer rewrite** (`vtx_blit`): reads 32-bit sprites in
  place (no per-call conversion), samples BILINEARLY with alpha weighting
  (smooth squash / stretch / breathing / lean instead of jaggy nearest
  pixels), reuses one buffer, inline pixel maths; destination pixels are
  sampled at their exact centres relative to the sprite's anchor.  Frame
  time unchanged in the fight probe (median 14 ms) - it was never the
  bottleneck; the gain is how sprites look.
- **170 photo album** (`vulture_photo_fx`, from mon.c where a Tourist adds a
  new species): the camera flashes (white light where the monster stands and
  a brief full-screen flash, camera.wav) and a polaroid slides in on the
  right - the monster's picture developing in, "NEW PHOTO", its name and the
  album count; milestones 10/25/50/100 get a gold frame and "MILESTONE!".
  Probe VULTURE_SELFTEST_PHOTO shot the gold card.
- **171 halos and living water** (`vhalo`, `vhalo_draw`): a cached radial
  disc blitted additively - lamps and candles (flickering, sized by range),
  fiery / electric / frost creatures; moving glints on water, slow molten
  pulses and rising sparks on lava.  Under the Atmosphere switch.  Probe
  LIGHT: the fire vortex blooms warm over the floor.

### 172. Tourists of any race and alignment

- role.c: the Tourist allows human, elf, dwarf, gnome and orc, and lawful /
  neutral / chaotic; each race keeps its own limits, so the combinations are
  human any, dwarf lawful, gnome neutral, elf and orc chaotic.  The three
  Tourist gods already cover the alignments (Blind Io / the Lady / Offler);
  hack_artifacts() re-aligns the role's quest artifact (the Platinum
  Yendorian Express Card) to the hero, so it never blasts a lawful or
  chaotic Tourist; starting kits get the usual racial substitutions.
  Character creation reads the same tables, so the new picks show up there.
- Probe (scratchpad selftest_rc.ps1 + tour_<race>.nethackrc): a dwarf
  (lawful), elf (chaotic), orc (chaotic), gnome (neutral) and human (lawful)
  Tourist each started, with the normal kit and the right alignment on the
  status line.

### 173. Racial Tourist kits; choose a kitten or a little dog

- u_init.c: on top of the Tourist kit, by race - dwarf a dwarvish iron helm
  (worn); elf an elven dagger and 2 lembas wafers; gnome 2 wax candles and an
  oil lamp (a tin opener was tried first - NetHack wields it, so it became
  the lamp); orc an orcish helm (worn) and 3 tripe rations; human 2 fortune
  cookies.
- Pet choice (vulture_nhplayerselection.cpp, `vulture_player_selection_pet`):
  after race / role / gender / alignment, a role without a fixed pet (the
  Tourist among others) asks "Which pet comes with you?" - a kitten, a little
  dog, surprise me, or no pet - setting preferred_pet (= the pettype
  option, which pet_type() honours).  Skipped if you declined a pet earlier
  or for roles whose pet is fixed (Wizard, Knight, Samurai, Caveman); also
  asked when the config pins the whole character (not in the probes).
- Fixed on the way: leaving gender or alignment random picked the gender
  with pick_align and the alignment with pick_gend.
- Probe: fresh dwarf / elf / gnome / orc / human Tourists each got their
  extras; pettype:dog -> little dog, pettype:cat -> kitten.  (Earlier runs
  had resumed old saves of the same names - moved aside.)  The menu's own
  clicks are untested off-screen.

### 174. Should I pray?  The prayer and luck badges

- pray.c `vulture_prayer_advice(&why, &trouble)`: 0 = no serious trouble; 1 =
  in serious trouble and your god will answer; 2 = in trouble but praying
  would go wrong, with the reason - demon form, a Moloch altar, prayed too
  recently (prayer timeout over 200), bad luck, an angry god, negative
  alignment, undead form, Gehennom.  It reads the same state can_pray() does
  without calling it (can_pray() sets prayer state as a side effect).
- Badge row: "PRAY NOW" (green, pulsing) or "DON'T PRAY" (red) first, while
  you are in serious trouble; the tooltip names the trouble or why not, and
  suggests Elbereth when praying is out.  Clicking PRAY NOW prays.  A "LUCK
  +n" / "BAD LUCK n" badge appears after the speed badge when luck isn't
  zero; its tooltip says what luck changes.
- Probe VULTURE_SELFTEST_PRAY=ok / soon / unlucky (1 HP; timeout 0 / 600;
  Luck -3): PRAY NOW; DON'T PRAY "you prayed too recently"; DON'T PRAY "your
  luck is bad" with BAD LUCK -3.

### 175. A Controls page

- Esc -> Controls lists every shortcut this interface adds, in four sections
  (moving and fighting, seeing more, gear and items, game) plus what each
  badge means.  Each row is "key<TAB>what it does" - Vulture's text windows
  line tab columns up (scrollwin::layout), so the keys stay in one column in
  the proportional font (space padding came out ragged).  Every key listed
  was checked against levelwin's key handler; "s" is listed as a plain
  search (the 5-turn search I first wrote does not exist).
- The welcome tip now ends "Esc, then Controls, lists every shortcut."
- Probe VULTURE_SELFTEST_CONTROLS shot the page.

### 176. Headings in gold

- Menus and text windows draw a row NetHack marks as a heading (the
  menu_headings attribute, or bold / underlined / inverse text) in gold.
  Vulture used to drop putstr's and add_menu's attribute; menuitem now keeps
  it (`attr`) and menuwin::layout colours those text rows
  (`vmenu_style_row`).  The discoveries list, the dungeon overview, pick-up
  menus' class headings and the Controls page now read as sections.
- menuitem changed size; the window port was rebuilt from scratch (its
  build directory moved aside) to be safe - not needed after all: its
  objects, winclass/ included, track header dependencies (.d files).
- Probe VULTURE_SELFTEST_DISCO: the discoveries list shows gold Weapons /
  Armor / Scrolls / Spellbooks / Potions / Rings / Wands headings.

### 177. Demons and prayer (an upstream typo)

- can_pray() in the NetHack 5.0.0 baseline tested
  `p_aligntyp == A_LAWFUL || p_aligntyp != A_NEUTRAL` for a hero in demon
  form: chaotic demons (and Moloch's altar) were turned away and neutral
  ones could pray - the opposite of its own comment ("ok if chaotic or none
  (Moloch)"), and its message only knows "lawful" / "neutral".  Now `==
  A_NEUTRAL`.  Found while checking the PRAY badge's advice against
  can_pray; the advice mirrors the fix, and for a neutral god it now says an
  undead form "may offend your god (1 in 10)" - the real odds.
- Probe PRAY=unlucky re-run: DON'T PRAY + BAD LUCK -3 as before.

### 178. Last moments, and a tidier run summary

- The run summary (end.c `vulture_run_summary`) ends with "Last moments":
  the last 8 messages before the end, oldest first, repeats folded into
  "(x3)", long lines cut at 99 characters - so a sudden death ("who is Jora
  and why am I dead?") can be read back.  Deaths only (how <= GENOCIDED).
  It walks the dumplog buffer (gs.saved_plines) itself: upstream's
  get_saved_pline() starts at slot 45 instead of 49 once the index wraps to
  0 (unsigned (0 - 1) % 50), so it would return the wrong lines.  The
  buffer is still intact here - the dump log frees it later in really_done.
- The summary's rows are "label<TAB>value" now, so the values line up
  (s175's tab columns); "The run" and "Last moments" are headings, gold
  since s176.  "(died on N)" only shows for a death.
- Probe VULTURE_SELFTEST_DIE: the summary shows the aligned rows and the
  last four messages of the probe game.  (The probe calls done() directly,
  so its list has no "You die..." line; a real death's does.)

### 179. A soak bot that keeps moving

- VULTURE_SELFTEST_SOAK (levelwin `vsoak_tick`) used to fight one monster
  forever (Tab at something it could not reach or kill) and stall on levels
  whose stairs it never found.  Now: after 60 Tab presses or 150 turns at
  the same monster it ignores that one for 400 turns, and leaves Tab alone
  while an ignored monster is the nearest (Tab always goes for the
  nearest); a level that drags on is magic-mapped after 1200 turns and
  left after 2500 (schedule_goto straight down the main dungeon) - or after
  60 s / 150 s without moving, since a hero sealed in a vault or caught in
  a trap may pass only a turn a second (the first run sat on dlvl 8 like
  that); with no
  enemy near, every 25 s it reads or quaffs an unidentified scroll or
  potion (autopickup brings them, and with none in the pack the probe
  makes one; this drives s150's auto-ID paths);
  20 s without a game turn gets random steps.  The 30 s status line adds
  XL, kills, ignored, used, mapped, dropped.

### 180. The prayer badge reads only what the hero knows

- s174's badge read the prayer timeout and Luck, and showed a LUCK badge.
  The user ruled exactly that out at log 59 / 63 ("knowing your luck is
  also cheating"; the status line's prayer cell was reworked to hero
  knowledge then).  Reworked: `vulture_prayer_advice` now uses the trouble
  the hero can see (a cursed blindfold / levitation item only once its
  curse is known), the turns since they last prayed (the chronicle,
  vq_turns_since_prayer), an angry god (they were told), their form, the
  altar they stand on, Gehennom, the Friday-the-13th warning, and one fixed
  rule (every game: the gods don't answer in the first 100 turns - the
  timeout starts at 300 and major trouble needs 200 or less).
- Three states: PRAY NOW (green: never prayed and past turn 100, or 1000+
  turns since), PRAY? (yellow: prayed under 1000 turns ago - "it may be
  too soon" / "probably too soon" under 500; its tooltip says a too-early
  prayer angers your god but is worth the gamble if you'd die anyway;
  clicking it prays), DON'T PRAY (red, with the reason).  The PRAY NOW
  tooltip adds that a god secretly displeased (bad luck, misdeeds) may
  still refuse - the part the hero can't know.
- The LUCK / BAD LUCK badge is gone, and its row on the Controls page.
- Probe VULTURE_SELFTEST_PRAY=ok / soon / early / angry.
- The same audit found two smaller leaks, fixed: the hover card's "you hit
  it N%" (s134 `vodds`) used find_roll_to_hit as-is, which counts Luck,
  an unidentified weapon's or accuracy ring's enchantment, and a blessed
  weapon's +2 against undead before the blessing is known - those are
  taken back out, and the monster's to-hit uses its kind's level, not its
  own (hidden) one; the stealth tooltip said "by what you wear" for
  unidentified elven boots / cloak / ring of stealth put on while already
  stealthy (no message, no identification) - it now names only identified
  gear (`vworn_known_stealth`).  Item comparison (s163) was already clean.
- And a third: the card's "crits N%" was vulture_crit_chance() = 3 + 2 x
  Luck, so it read Luck off exactly.  It shows the luck-free base now,
  "crits 3%+" (`vulture_crit_chance_shown`); the roll itself is unchanged.
  The aimed-blow picker takes its odds from vodds, so it is clean too.
- Probes: PRAY=ok -> PRAY NOW, soon -> PRAY?, early -> DON'T PRAY, angry ->
  DON'T PRAY, no luck badge; the card reads "crits 3%+" in a full-moon game
  (Luck +1 - it said 5% before); the Controls page's badge rows updated.

### 181. What it does: item effects on hover

- The item tooltip (`vulture_item_info`, inventory and item grids) gave
  numbers for weapons, armour, food and spellbooks but nothing for magic
  items.  Now an identified potion, scroll, wand, ring or amulet says what
  it does in a line or two (`vitem_effect`, 114 entries): e.g. "Heals some
  HP and cures blindness (not if cursed); healed past full, your max HP
  rises by 1", "Never put it in a bag of holding - the bag explodes",
  "Strangles you: take it off within 6 turns or die".  Rules only, and only
  for identified kinds - the hero-knowledge rule (log 59/63/180) holds.
- Checked against this 5.0 source where in doubt, and corrected from
  memory where it differed: a non-cursed potion of speed also makes you
  fast for good; blessed see invisible / invisibility are only sometimes
  permanent; free action also stops the sleeping potion; blessed full
  healing restores a lost level; blessed fire lets you pick the spot (5x),
  confused fire only singes your hands; earth: blessed around you,
  uncursed around and on you, cursed only on you; guarding = AC +2 and
  better magic cancellation; magical breathing does NOT save you from
  strangling (only the message changes); burned Elbereth still erases when
  you attack from it (so "words that won't smudge", not "permanent").
- Unidentified magic items get advice instead (`vitem_unknown_hint`):
  potions / scrolls name themselves when used (s150); engrave-test a wand
  (uses a charge); a ring or amulet is risky to try - test it on an altar
  first, never wear a cursed amulet.
- Floor items: the map hover card shows the effect too (one line, cut at a
  word past 100 characters).
- Probe VULTURE_SELFTEST_ITEMFX: an identified potion of healing beside
  the hero, hovered -> "A potion of healing / Heals some HP and cures
  blindness (not if cursed); healed past full, your max HP rises by 1."
  (itemfx.txt logs the square, name and hint; the probe flushes the
  screen so the map knows the potion before the shot).

### 182. Unlit by default

- The user asked for the map unlit by default: dynamic lighting starts OFF
  (`vl_on = false`, now defined beside vselftest).  The light passes were
  the biggest CPU cost of a frame (median ~6 ms, ~25 ms in the slow
  frames).  F9 still cycles full / flat / off, the Visual effects menu has
  a "Dynamic lighting - off / flat / full" entry, and either choice is kept
  in effects.txt (`lighting=`; a file without it means off).
- The light probes (VULTURE_SELFTEST_LIGHT / PORTAL) and
  VULTURE_SELFTEST_LIT=1 switch it on for their run.

### 183. The map drawn by the GPU

- With a renderer (gpu=1, the default since s168) and lighting off, the
  map's tiles and sprites are drawn with SDL_Render* into a render target
  the size of the frame (vgm_tex) instead of being blitted into
  vulture_screen.  The hooks are the two primitives everything in the
  tile passes goes through: `vulture_put_img` (tiles, shaded tiles, magic
  sprites, highlights - and now the contact shadows and blood stains,
  which blitted directly before) honours the surface's blend mode, colour
  and alpha modulation; the sprite transformer `vtx_blit` becomes one
  SDL_RenderCopyExF scaled and turned about the feet, red-tinted by colour
  modulation and faded by alpha.  Textures are made once per surface
  (transparent pixels take a neighbour's colour, so linear filtering grows
  no dark fringe) and dropped when the surface is freed
  (`vulture_gpu_forget` in vulture_free_tile, the flipped-sprite cache and
  the object highlight).
- Everything drawn after the tile passes - effects, the HUD, every other
  window - still goes into vulture_screen, which is ARGB now and is
  cleared to transparent over the map at the start of each map frame: an
  overlay.  SDL's software blending into a destination with alpha yields
  premultiplied colour, additive blits leave its alpha alone, and every
  overlay effect turned out to use plain blend / add blits or opaque fills
  (no multiply), so the overlay goes over the map with the premultiplied
  "over" blend (ONE, ONE_MINUS_SRC_ALPHA).  Renderers without custom blend
  modes (SDL's software one) get the same in two passes.  Map and overlay
  are composed into vgm_out, then shown.  Zoom (s121) zooms the overlay on
  the CPU as before and the map layer with the same rectangles on the GPU
  (1.5x: nearest to 2x, then linear, as s122).
- Lighting on, a swallow, or no renderer: the old software path, and the
  overlay is copied opaque.  A switch between the two redraws everything.
  The death-cam, F11 photos, F12 screenshots and the probes take the frame
  as shown (`vulture_frame_to_surface` composes and reads back).  Device /
  render-target resets drop the textures and redraw.  Visual effects has a
  "Draw the map on the GPU - ON / off" switch (effects.txt `gpumap=`);
  VULTURE_NO_GPUMAP=1 forces software.
- Probes: VULTURE_SELFTEST_GPUDIFF draws one frame both ways (and at 2x
  and 1.5x zoom).  VULTURE_HIDDEN_WINDOW=1 gives a probe a real but hidden
  window - a real renderer and nothing on screen (the keyboard look
  cursor then doesn't warp the real pointer).  On this PC (Direct3D):
  "premultiplied composite"; the same frame both ways differs by a mean
  of 0.20 per channel, 50 pixels over 16 - sprite edges (GPU linear vs the
  transformer's bilinear) and particles that moved between the two draws.
  SDL's software renderer (two-pass): mean 0.28.
- Measured on this PC, hidden window, 45 s of the soak bot each, lighting
  off: software map - map draw median 7 ms (p90 16, p99 53), present 2.0
  ms (p90 6.2); GPU map - map draw median 1 ms (p90 3, p99 12), present
  1.6 ms (p90 3.4).  About 9 ms -> 2.6 ms of CPU per frame.

### 184. Blows land where you are (for real); text rides the glide

- The user, still: monsters attack the square you are walking into before
  you get there, and text that follows the hero runs ahead of it.
- Root cause of the first, found with a frame-by-frame probe: the blow
  (mattacku -> vulture_anim_attack) comes MID-TURN, before the core
  flushes its display buffer, so Vulture's map still showed the hero (and
  a monster that had just moved) on the OLD square.  s154's "play the
  pending step first" glided positions taken from u.ux / m->mx while the
  sprites stayed put - the lunge struck an empty square and the hero
  popped into it afterwards.  vulture_anim_attack now calls
  flush_screen(1) before it draws, so the glide moves the sprites and the
  blow lands on them.  A glide the tween would skip (teleport-mode runs)
  is snapped before the lunge as well.
- Second: anything following a creature took its square alone, with no
  glide offset, so mid-step it sat on the destination while the sprite
  slid there.  `vfx_mover_screen` adds the square's vtw_ox / vtw_oy; used
  by the hero's barks, speech bubbles, the searching / resting label, the
  level-up burst, sleepers' Zs and the stunned's stars, damage numbers,
  and the hero end of the path trail.  Floor things (click markers,
  puffs, blood) stay on the floor.
- Probe VULTURE_SELFTEST_APPROACH: a hostile jackal two squares away (with
  the movement to strike at once), a real click on the square between;
  every glide / lunge frame is saved (seqNN_*.bmp).  Before: the hero
  stays on the stairs through the glide and the jackal lunges at the
  empty marked square.  After: the hero walks onto it (the bark "Right."
  with them), then the jackal lunges and the hero flashes red.  The keys
  turn with the view, so the probe clicks instead of pressing 'l'.

### 185. Dynamic lighting on the GPU

- The user: make the dynamic lighting work with the GPU.  s183 drew the map
  on the GPU only with lighting off, because the light passes (vl_nmap's
  normal-mapped relief, vl_apply's light layer) read and write the map's
  pixels.  SDL2's renderer has no shaders, but its OpenGL backend lends its
  textures to OpenGL (SDL_GL_BindTexture) - so while the map is lit the
  renderer is OpenGL (`vulture_gpu_use_gl`, called from levelwin::draw:
  turning lighting on or off remakes the renderer once - every GPU texture
  is rebuilt lazily, vulture_screen is kept; VULTURE_NO_GL=1 or
  SDL_RENDER_DRIVER override it), and unlit it stays SDL's default
  (Direct3D here): OpenGL presents cost ~6 ms more per frame on this PC
  (unlit: present 8.1 ms vs 1.5, mostly in the swap).  One GLSL pass does both passes'
  arithmetic, same constants and order, in floats: n . L with the 1.5x
  gain, crevices, the soft glint, the +-80 / 32 clamps, then
  out = rgb + (tint - rgb) * A from the light grid, black staying black.
- Inputs: the map layer; a normal layer (vgm_nrm) - the normal-map tiles
  and transformed sprite normals are recorded during the map pass
  (vulture_put_normal and vtw_put_tile_x branch on vulture_gpumap_on) and
  replayed into it, so it holds exactly what vulture_normal_buf held; the
  light grid vl_low (bilinear) and the per-cell directions vr_dx / vr_dy /
  vr_lv packed into a small texture (per cell).  vl_render hands its grids
  and origins over where it would have run vl_relief + vl_apply
  (`vulture_gpulit_frame`; uploads only when the grids were rebuilt).  The
  lit map goes into vgm_lit, which the composite shows.  Raw GL runs
  between SDL's commands: the renderer is flushed, all state pushed and
  popped, the program restored, SDL's texture cache reset.
- Without OpenGL, or if the shader fails, lighting draws the old way (the
  log says which); VULTURE_NO_GPULIT=1 forces that.  The Visual effects
  switch says when lighting is in software.
- Probes (hidden window, this PC, OpenGL): "gpulit: dynamic lighting runs
  on the GPU (opengl)"; the same lit frame both ways (GPUDIFF with
  VULTURE_SELFTEST_LIT=1) differs by a mean of 0.73 per channel, 100 pixels
  over 16 - rounding (the GPU is ~0.5 brighter on average), sprite edges,
  and faint 8 px banding where the CPU held the tint per cell.  Zoomed 2x /
  1.5x: 1.1 / 1.1.  VULTURE_SELFTEST_FLAT=1: lighting without the normal
  maps.
- Measured (hidden window, this PC, 30 s of the soak bot, lighting on): CPU
  lighting on Direct3D - map draw median 12 ms (p90 13), present 1.4 ms,
  ~13.4 ms a frame; GPU lighting on OpenGL - map draw 2 ms (p90 4), present
  3.5 ms (p90 8.6), ~5.5 ms a frame, and a third more frames drawn.  Unlit
  stays on Direct3D: map draw 1 ms, present 1.5 ms.  The present log now
  splits upload / draw / swap.

### 186. The camera keeps up with a middle-drag

- The user: the game noticeably lags when you move the camera.  Four causes:
  1. The drag lost most of its distance.  vulture_wait_event /
     vulture_poll_event skip stale MOUSEMOTION and dispatch only the newest
     event of a run - right for hovering, wrong for a middle-drag: the
     camera moved by the last event's few pixels per frame, so a fast drag
     felt like dragging through mud and slower frames lost more.  The skip
     is now `vulture_take_event`: a drag run's xrel/yrel add up into the
     event kept (hover runs are unchanged), and a key or click that ends a
     drag run is held one call so the run before it isn't dropped (hover
     motion before a click is still dropped, so clicks aren't delayed).
  2. vulture_event_dispatcher slept 20 ms after every event, which held a
     drag to ~35 camera moves a second; it doesn't sleep after a drag event.
     handle_mousemotion_event also takes any drag events that arrived during
     the frame (and vulture_travel_pan's), and plain hovering skips a motion
     event when a newer one is already waiting.
  3. The light grid was rebuilt whenever the pan changed the view square.
     It is map light sampled on the screen, so it only moves: a view shift
     of up to 8 squares slides it (vtw_proj of the shift), with a 128 px
     margin (was 64), in the CPU path and the GPU hand-off alike.
  4. TILECACHE_MAXAGE 4 counted in turns once but vulture_tilecache_age now
     runs every map draw, so a tile off the screen for four frames was freed
     and panning back decoded its PNG again (and re-uploaded it); now 1800
     frames.
- Probe: VULTURE_SELFTEST_PAN pushes 300 drag events at once (net +300 px
  across) and reports how the game took them: "1 drag moves took (300, 0) px
  of (300, 0)", unlit and lit.  By the code the old skip took only the last
  event's (5, -2).  (Its "caught up in N ms, N frames" counts every frame
  until the queue is empty, other stage work included.)

### 187. Dynamic lighting draws the fog of war

- The user: make the dynamic lighting better - the fog of war and unseen
  tiles should blend with the shadows, and it should be more dramatic.  What
  was wrong (VULTURE_SELFTEST_FOG shots): squares out of sight were drawn
  with pre-darkened tiles (x0.68, x0.31 for a dark room's floor) and then
  lit at 0.50 - brighter than the real shadows beside them, flat, and cut off
  at every tile's edge; the void's tile is a dark grey texture (mean 24), so
  the light layer drew it as a grey outline of the whole level.
- Now, while lighting is on (levelwin::draw's `vl_fog`, vl_render's own
  test), tiles are drawn at full brightness and the light layer does it all:
  - remembered squares: light 0.26 (a dark room's floor 0.16) and a cold
    blue shadow - a new fog map vl_F, sampled bilinearly like the light, so
    the edge of sight is a soft falloff; the grid's colour is now the
    shadow's colour by the darkness's share plus the light's by the tint's
    (out = p + (T - p) * A as before - the GPU shader is unchanged);
  - light from sources falls short into the fog (x0.35);
  - the void - rock and never-seen squares, judged from the map as drawn
    (MAP_BACK unmapped or none), never the level - is drawn as the black
    off-map tile, gets no light of its own, and lends the walls standing on
    or reaching over it 70% of its brightest known neighbour's light and fog;
  - deeper darkness (0.94 where no light reaches, was 0.80), lit rooms 0.70
    (0.80), unlit squares in sight 0.26 (0.38); the hero's pool is bigger and
    warmer (radius 4.6 / 0.46, was 3.6 / 0.32); light past full brightness
    glows in its own colour (0.30 per unit, up to 0.8 over); coloured light
    colours more (0.45, was 0.35).
- The relief's light direction follows the sources' light alone (vl_S): the
  step down into the fog, or out of a lit room, isn't a lamp.
- Hero knowledge: the contact shadows (s143) read IS_STWALL off the level, so
  a secret door (or corridor) beside a known floor cast no shadow - a tell.
  They read the drawn map now (wall, rock, or unknown).
- Measured (hidden window): remembered corridor floor ~30 (was ~45), lit
  room floor ~118 (was ~110) - the lit-to-fog contrast went from ~2.4x to
  ~3.9x; the same lit frame on the GPU and in software differs by a mean of
  0.2 per channel; the light stage stays at a 1 ms median (3 ms p90).
  Unlit play is unchanged.

### 188. The fog of war a bit darker

- The user: make the fog a bit darker.  Remembered squares' light 0.26 ->
  0.19 (a remembered dark room's floor 0.16 -> 0.12), and the fog's cold
  shadow colour dimmer (0.035 0.050 0.100 -> 0.028 0.040 0.080), so the haze
  lifts dark pixels less.  Measured (VULTURE_SELFTEST_FOG, lit): remembered
  corridor floor ~24 (was ~30), still readable; the void's borrowed light for
  walls follows.

### 189. Normal-mapped lighting on by default

- The user: normal maps on by default.  The normal maps (s145/s149 relief)
  only show with dynamic lighting, which s182 had turned off by default for
  speed (the light passes were the biggest CPU cost; since s185 they run on
  the GPU).  So `vl_on` starts true again, normal-mapped (the "normalmaps"
  setting already defaulted on).  A saved choice in effects.txt still wins
  ("lighting=0" stays off) - and only builds since s182 (today, unreleased)
  write that line, so released players get the new default.
- The Visual effects switch now goes off -> full -> flat -> off, as F9 does:
  turning lighting on brings the normal maps (it went to flat first).
- The probes keep their meaning: unlit unless a light probe or
  VULTURE_SELFTEST_LIT=1 asks (stage 0 now sets vl_on either way).
- Checked (hidden window, fresh profile): the first map frame remakes the
  renderer for lighting and lights on the GPU; the s149 compare shot (the
  same frame flat vs normal-mapped) differs by a mean of ~2.4 per channel
  over the map, the stone relief plainly visible.

### 190. Normal maps lit by the lights themselves

- The user: improve the normal maps.  What was weak: one light direction per
  8 px cell, taken from the slope of the light field, under a strong
  top-left key (0.36 of the light level) - so in a lit room the relief was
  the same emboss everywhere and barely moved with your light; light
  sources shaded themselves (the old crevice term darkened a fire vortex's
  edges at full strength); walls' normals faced the viewer, so a wall took
  light the same from either side.
- Now (vl_nmap and vgl_fs, the same arithmetic in floats): every vl_add
  records a point light (vl_pls: square, radius, intensity, colour, how far
  above the square it hangs and its height towards you - the hero's at head
  height, lamps and glowing creatures at hand height, following a glide).
  vl_pl_project puts the strongest 24 that reach the map on the screen each
  frame (vl_plf, 12 floats each - the GPU pass's uniform array) and marks
  per relief cell which can reach it (vr_lm, for the CPU loop).  Per pixel:
  for each light, the direction from the pixel to it and its falloff weight;
  the shading is relative to flat ground (the light layer does brightness):
  n.xy . l.xy plus 0.4 of the "facing away from its height" term, averaged
  over the lights by weight and scaled by the light that really arrives
  (the cell's source light from vl_S - walls and the fog of war already cut
  it); a Blinn-Phong glint (n.h^16 above flat ground's) in the light's
  colour; a soft key (0.22 of the light level).  Constants in one place
  (vl_rk), handed to the shader.  Relief grid per cell is now the key's
  weight and the source light (vr_kw, vr_sw); the dirs texture carries them.
- Normal maps (toolchain/make_normal_maps.py, regenerated for the 134
  affected tiles, copied into the game folder): half walls' faces turn the
  way they face on the screen (_n/_s down-left, _w/_e down-right), so they
  catch light from that side; things that give off light (explosions, lava
  floors, fire / energy / ice / steam vortices, the spheres, the fire
  elemental) get flat normals, so the relief leaves them as drawn.
- Measured (hidden window, the s143 light scene): the GPU path's light stage
  stays at a 1 ms median; the relief costs ~0.75 ms of GPU time at 1080p
  (swap 6.1 ms vs 5.4 with it off); the software path's relief ~4-5 ms (was
  ~1).  The same lit frame on the GPU and in software differs by a mean of
  0.18 per channel.

### 191. Shadows cast by the lights

- The user: shadows from your light.  Only the round contact blob (s158)
  sat under things.  Now every creature in sight (you too) and every big
  object (an object tile 44 px tall or more: boulders, statues) lays its
  silhouette on the floor, away from the strongest light that reaches it
  with a clear path (the light layer's point lights, vl_pls - your torch,
  lamps, glowing creatures, fires), skipping a light on its own square (your
  torch never shadows you).  Length: distance / (light's height x 1.2),
  0.3-1.6 of the sprite's height, laid along the iso floor (2:1); darkness:
  0.75 x (that light's share of the light where it stands x 2.5, capped) x
  (its strength x 5, capped), fading to 45% at the tip.  Drawn after the
  floor pass and before anything stands on it, so things in front cover it;
  posed like vtw_put_tile_x (glide, facing).  An invisible hero who can't
  see invisible casts none.
- GPU map: one textured quad per silhouette (SDL_RenderGeometry, black with
  per-corner alpha - `vulture_gpumap_shadow`); software: the same affine map
  rasterized (inverse-mapped per pixel).  Lighting on only; the Shadows
  switch in Visual effects (was "Contact shadows") covers both kinds.
- Probes: VULTURE_SELFTEST_LIGHT now also saves selftest_shadow.bmp and
  selftest_noshadow.bmp (the same frame, shadows on/off); "cast:" log lines
  give each shadow's light, weight and darkness once a second.  Both paths
  darken the expected silhouettes (software: 3483 px by more than 20).

### 192. Living fog

- The user: living fog of war.  Over remembered ground a slow mist drifts:
  two layers of a 256x256 tiling value noise (vl_noise, its own generator -
  no game rolls), anchored to the map (the screen position of square 0,0)
  so it pans with it, crawling ~10 and ~7 px a second in different
  directions; smoothstepped into wisps and blended up to 0.32 towards a cold
  grey (62, 72, 92).  Its weight is the fog of war itself (vl_F), now kept
  on its own grid beside the light grid (vl_fogm, R = G = B = F, sliding
  with it), so it thins out at the edge of your light and spills a little
  into the dark beyond the edge of what you know (the void's borrowed fog).
- GPU: vgl_fs mixes it in after the light layer, per pixel - the noise as a
  repeating texture (unit 4), the fog grid bilinear (unit 5), inputs set by
  `vulture_gpulit_mist`.  Software (vl_mist): the mist worked out once per
  grid sample on every core, then blended between samples like the light -
  per pixel it cost ~18 ms a frame over a magic-mapped level, now the light
  layer's apply + mist is 5-8 ms there.
- Visual effects: "Drifting fog over remembered ground" (effects.txt
  "mist=", on by default; with lighting).  Probe: VULTURE_SELFTEST_FOG now
  also shoots fog_room_later (the same view 0.9 s on - the mist has moved).

### 193. Your conditions, on you

- The user: show conditions on the character, not only in the status line.
  What the status line already says (hero knowledge only), on the hero:
  - stunned or confused: you sway like a dazed monster (s162's formula), and
    yellow stars (stunned) or violet swirls (confused) circle your head
    (behind it dimmer);
  - blind: dark smoke curls about your head;
  - deadly countdowns pulse a colour over you - turning to stone grey, to
    slime yellow-green, strangled red, ill or food-poisoned green (in that
    order of precedence); hallucinating: you shimmer through the colours;
  - stressed or worse under your load: drops of sweat;
  - levitating or flying (not riding): you float a hand's breadth up,
    bobbing (vbob_offset; the contact shadow stays on the floor);
  - invisible and not seeing invisible: the game shows nothing where you
    stand, so you are drawn there faint (your own form, 60%).
- How: the monster pass records how it drew you (vhp: tile, place, facing,
  scale, lean); vcond_draw, over the lit map, draws a colour-tinted copy of
  you in that pose (the sprite transformer's new `vtx_tint`: the colour
  replaces the sprite's, its alpha kept) and the little procedural sprites
  (star, swirl dot, smoke puff, sweat drop) around your head - the art's
  first opaque row found once per tile (vcond_top).
- Probe: VULTURE_SELFTEST_COND gives the hero stun; confusion + illness;
  hallucination; levitation; invisibility - one after another, shooting
  each (cond_*.bmp).  All five read in the shots.

### 194. Only the changed parts of the overlay go up

- The user: smoother frames with lighting on.  A map frame clears the
  overlay (vulture_screen) and draws it again, so every present uploaded all
  of it - 8 MB at 1080p, 2.6 ms a frame on the OpenGL renderer that
  lighting uses - though most of it (the clear map area, the HUD) comes out
  the same each frame.
- On OpenGL the overlay as last uploaded is kept (vgm_shadow); vgm_diff
  compares the frame with it in 64 x 16 blocks within the invalidated
  rectangle, and only runs of changed blocks are uploaded (to the overlay
  texture and, for the two-pass composite, its twin) and copied into the
  copy.  A new texture, size or renderer uploads everything once.  Direct3D
  (unlit) uploads a whole frame faster than comparing it, so it keeps doing
  that; VULTURE_NO_DIFFUP=1 turns the comparing off.
- Measured (hidden window, 1080p, the s143 light scene, lit on the GPU):
  upload median 2.6 ms -> 1.3 ms (p90 1.7), typically 3.5% of the frame sent
  (71680 px); the whole present 8.9 ms -> 5.4 ms (the swap waits less too).
  The present log line now says how many pixels went up.  The same lit frame
  on the GPU and in software still differs by a mean of 0.2 per channel.

### 195. Change keys in the game

- The user: rebind keys in-game (they could only be changed in the config
  file - NetHack 5.0's own rebinding menu exists, but deep in the options,
  and forgets its changes when you quit).
- Esc > Change keys (also on the Controls page): 35 everyday commands (eat,
  quaff, read, zap, cast, wear, ..., inventory, travel) with the keys they
  are on now; pick one, press the new key in the "press any key" dialog.
  It is bound the way the config file's BIND= binds (bind_key), so
  NetHack's own command table does the work, and the message names what the
  key did before.  "All commands..." opens NetHack's own menu (any command,
  any key, unbinding); a before/after snapshot of the table catches what it
  changed.  "Forget my key changes" drops them.
- Only the changes made in the game are kept - keys.txt beside effects.txt
  (key:command, NetHack's key2txt / txt2key notation, "nothing" for
  unbound) - and applied again at every start after the config file
  (vkeys_load from levelwin::draw, once), so a config BIND isn't copied into
  it and still works where the game didn't change that key.
- Probes: VULTURE_SELFTEST_KEYS puts "eat" on E (E did eat; eat on "E or e";
  keys.txt "E:eat"), then restores engrave and removes the file; started
  with a keys.txt of E:eat and ^E:engrave, the log shows both applied.

### 196. Keys reach the game after the switch to OpenGL

- The user: "i cant open menus without having to alt tab back into the game
  first".
- Cause: lighting runs on SDL's OpenGL renderer (s185), and with lighting on
  (the default since s189) every game start swaps Direct3D for OpenGL.  SDL
  puts the OpenGL renderer only on a window made for OpenGL; on any other it
  hides the window, destroys it and makes a new one (SDL_RecreateWindow),
  loading OpenGL in between.  For ~0.18 s the game had no window, Windows
  gave the foreground to another program, and it doesn't hand the keyboard
  to a window that appears afterwards (the foreground lock): keys went to
  whatever was behind until an alt-tab.  Hover kept working (the mouse goes
  to the window under it), so it looked like only the menus were broken.
- Seen with a probe that watches the process's SDL_app windows (EnumWindows)
  and the log line at each change: before, the game's window handle changed
  inside the call that made the OpenGL renderer; after, one window lives
  through the switch and back (the only short-lived windows are SDL's own
  OpenGL setup windows, at start-up, before the game window exists).
- Fix: the window is made with SDL_WINDOW_OPENGL from the start
  (vgpu_create_window) - a plain one if that fails, or with gpu=0,
  VULTURE_NO_GL, SDL_RENDER_DRIVER or VULTURE_GPU_SOFT.  Direct3D draws on it
  as well: unlit presents measured the same (median 1.49 vs 1.47 ms against
  a plain window, VULTURE_NO_GL).  The OpenGL renderer is never asked for on
  a window not made for it (vgpu_window_gl): lighting stays in software
  rather than SDL remaking the window.
- Also: vulture_wait_input and vulture_wait_key wait on SDL directly, so
  they skipped a key or click held back behind a middle-drag (s186's
  vte_stash); they take it first now.
- Probes: scratchpad window_watch.ps1 (hidden window; game windows seen,
  when, and the log line at each change); PAN still 1 move of (300, 0).

### 197. Menus reach the screen again

- The user, after s196: "bug persists, if i press any key that opens a menu
  the screen gets frozen".  s196 fixed a real window remake, but not this.
- Cause: s194's diff upload.  vgm_diff walks each 16-row band in 64-px
  blocks and sends a run of changed blocks when an unchanged block - or the
  pass after the last block - closes it.  The column loop stepped by 64 and
  ran while bx <= the right edge, so it only landed on that closing pass when
  the width was a multiple of 64: the whole 1920 screen, but hardly ever a
  menu.  A menu just drawn changes every block of its rectangle, so each band
  was one run reaching the right edge, and none of it went up.  A menu opened
  from the prompt is presented by its own region refresh and nothing else
  (the map is idle under it), so the screen stayed as it was; alt-tabbing
  back sends FOCUS_GAINED -> vulture_refresh(), a 1920-wide present, and the
  menu appeared.  OpenGL only (lighting on, the default): Direct3D sends the
  whole rectangle and never compares.
- Fix: each step is the block's own width, so the one after the last block
  lands exactly on the end, and the loop leaves after that pass.
- Tested on its own: scratchpad diffh/harness.cpp cuts vgm_diff out of
  vulture_sdl.cpp and draws 4000 rectangles over a 1920x1080 frame (whole
  menus, scattered pixels, every 10th a full present).  Before: 3478 presents
  left pixels unsent (188 M px), every one a width not a multiple of 64.
  After: none, and the copy matches what the fake texture got.
- Tested in the game: probe VULTURE_SELFTEST_MENUKEY presses i at the prompt
  (a real key event); VULTURE_SELFTEST_DIFFUP checks after each OpenGL present
  that every pixel of its rectangle is on the GPU (vgm_shadow matches the
  frame there).  Old loop (a side build): the inventory, 652x734, sent 0 px,
  474102 px of it not on the GPU, and no present after it; tooltips (69x21)
  were lost the same way.  Fixed: it sent all 478568 px; 74 presents checked,
  none missing anything.
- Why the probes missed it: their shots read the frame on the CPU
  (vulture_save_frame_bmp), never what went to the GPU, and a menu opened by a
  probe inside the map's idle tick keeps the idle frames' full presents
  coming, which carry it up.  DIFFUP checks the GPU side; MENUKEY opens a
  menu the way a key does.

### 198. Spells, wands, breath and explosions that look current

- The user picked four improvements, in order: this (4), a readable status
  panel (2), modern menus and inventory (1), sharper art (3).
- Before: every ray - fire, frost, lightning, death, sleep, gas, acid, magic
  missile, wand or spell or breath - was the same orange zigzag sprite over a
  white splat (Vulture read only the glyph's direction, not its type), with
  one white light per square; dark, noxious, muddy and wet explosions used
  the fire sprites; a ray got one frame per step of the core (~20 fps).
- Now (levelwin.cpp "s198", vsp_*): mapdata::set_glyph tells vsp_glyph about
  every ray / explosion / wand-flash / digging / resistance-sparkle square,
  with its type and when it came and went.
  - A ray is drawn as light in the overlay (vsp_draw_ray), not the sprite: a
    core and halo in its colour, brightest at the head, each type with its
    own motion - lightning re-forks ~22 times a second with side branches,
    fire licks with a white-hot centre and rising tongues, frost glitters,
    gas billows, magic missiles pulse along it, death is a dark core in a
    violet haze.  It flies at chest height and pools on the floor below.
  - It lights the room in its colour (vsp_light replaces the white light in
    vl_compute), so the walls and floor catch it and the cast shadows swing
    as the head passes; each square glows on 0.6 s after the ray has gone.
  - Particles (vsp_parts, at most 900): embers and smoke, frost, sparks,
    acid drops, gas, dark wisps, motes - they keep moving after the core
    has cleared the glyphs.  A ray coming back the other way (a bounce)
    throws a burst where it turns.
  - An explosion: a room flash in its colour (0.95 s), its sprites pop in
    (scale 0.72 -> 1.08 -> 1), a white-hot instant, a shockwave ring across
    the floor, 44 pieces of debris from the centre and 5 from each square,
    a camera jolt (vimp, weaker with distance), smoke / mist / gas left
    behind.
  - New explosion sprites for dark, noxious, muddy and wet: the fire set's
    brightness mapped onto each type's three-stop ramp, alpha kept, the fire
    normal maps copied (toolchain/make_explosion_tiles.py, which also points
    vulture_tiles.conf at them).  5.0 never makes a dark explosion, and its
    explosion_to_glyph() shows EXPL_DARK as fire anyway; the set is there
    for completeness.
  - vulture_delay_output calls vulture_fx_pump: while any of this is alive,
    frames are drawn through the core's pause between steps (every 15 ms),
    and the idle loop runs at 60 fps (16 ms) meanwhile.
- Core (display.c tmp_at DISP_END): a flush_screen(0) after a beam's squares
  are restored.  The erasure reached the window port only at the next flush,
  so a ray or an explosion stayed drawn into the monsters' turn (the old
  zigzag did too; it just didn't move).
- Probe VULTURE_SELFTEST_SPELL: from the hero, the most open way, fires the
  7 ray types, the 7 explosion types, then poison gas (last: its clouds
  block the view), with reflection and resistances, and shoots 960x540 crops
  live (pumped frames 2, 5, 10) and 260 ms after each ends.  Lightning that
  passes your square blinds even through reflection (flashburn), so the
  probe cures it before each.  Lit (OpenGL) and unlit (Direct3D) both run
  all 15; MENUKEY + DIFFUP still 0 px missing.
- Cost: a map frame during a ray or explosion took 3 ms median, 6 ms p90
  (otherwise 4 / 6), the effects about 1 ms of it.

### 199. A status panel anyone can read

- Step 2 of the user's 4-2-1-3.  The bottom-left panel was NetHack's status
  line cut into a 5 x 5 grid of words on stone art: "St:18/16 In:14
  HP:11(11) Pw:6(6) AC:9 Dlvl:1 $:0 T:3", conditions clipped to "Hallu",
  "FoodPois", "Pray?".
- Now drawn by levelwin.cpp (vulture_status_panel_draw, the newer HUD's
  style: vbox, a gold hairline) - statuswin.cpp takes its size
  (vulture_status_panel_size, 564 x 104 at UI scale 1) and hides its old
  text cells:
  - your name and rank (or what you've turned into), your level;
  - HP, power and experience as bars with their numbers on them; HP turns
    amber at 60%, red at 30% and throbs under 25%, and the part a blow just
    took stays pale for 0.45 s, then drains away;
  - alignment and the six attributes (STR 18/16 kept as NetHack writes it);
  - Depth, Armor, Gold, Turn spelled out;
  - conditions as coloured pills, in full words - Turning to stone, Food
    poisoning, Fainting / Weak / Hungry / Satiated, Burdened .. Overloaded,
    Blind, Deaf, Confused, Stunned, Hallucinating, Levitating / Flying,
    Riding, and s63's prayer hunch as Can pray / Pray? / God angry - the
    deadly ones pulsing; the quiver on the right.
  - Every piece has a tooltip in plain words: what it is, and for a
    condition what to do about it (vulture_status_tip, in levelwin's hover
    chain).  Only what the status line already showed; no hidden values.
- The portrait's frame is the same dark box (it was cut from the stone art)
  and its thin bars are gone - the panel has proper ones.  The skill
  "enhance" button sits right of the panel, clear of the hotbar above it,
  and follows it on a UI-scale change.  The hotbar, the badges and "Things
  that are here" were already anchored to the panel and moved up with it.
- Fix on the way: levelwin's hover handler treated ANY child window with a
  menu_id as a scroll hotspot, so moving the mouse over the status panel
  ran the edge-scroll code - which scrolled nothing but switched
  camera-follow off.  Only the eight scroll hotspots go there now.
- Probe VULTURE_SELFTEST_STATUSTIP: all 17 hot spots answer with their own
  tip, a point outside the panel with none, and a real hover over Armor
  (handle_timer_event) shows its tooltip and leaves camera-follow on.
  VULTURE_SELFTEST_COND shows the pills (food poisoning, confused,
  levitating, hallucinating).

### 200. Modern menus, inventory and dialogs

- Step 1 of the user's 4-2-1-3.  Every menu, dialog, the inventory and the
  right-click menu were 2006 Vulture: tiled grey stone in carved borders,
  grey bevelled buttons that shifted 2 px when pressed, stone radio buttons
  and checkboxes, item cells with etched frames (blue on hover, orange when
  chosen), a pale yellow tooltip with black text.
- A small kit in vulture_gra.cpp draws them now, in the newer HUD's style:
  vulture_ui_fill (one cached pixel, blended and clipped by the draw
  region), vulture_ui_border (ui-scale thick), vulture_ui_panel (dark,
  97% opaque, a dim-gold frame, a gold top edge).
  - mainwin (every menu / text window / dialog): the panel, the title in
    gold over a thin gold rule.  The stone borders' sizes stay as padding,
    so no layout moved.  contextmenu: the panel.
  - button: flat - dark, a thin border, gold under the pointer (new hover,
    via handle_mousemotion_event / MOVEOUT), deeper when pressed; sized by
    the font it is drawn in (it measured V_FONT_MENU and drew V_FONT_BUTTON).
  - optionwin: a drawn box instead of the stone radio/checkbox, filled gold
    when chosen (orange with a count); the row under the pointer is lit -
    menuwin redraws when the hovered row changes (it kept only the address).
  - objitemwin (inventory and item menus): a flat card - gold edge and left
    accent under the pointer, gold tint when selected, white edge on the one
    toggled last; the icon sits in a dark well, blue-tinted and -edged when
    known blessed, red when known cursed; "weight N" dim, "selected (all)"
    gold, worn items green as before.  objheaderwin: category headings in
    gold on darker cards, its item rows as objitemwin.
  - scrollbar: a dark track, chevron buttons, a gold-edged thumb (the art's
    sizes kept as the geometry, so clicking and dragging are unchanged).
  - inventory: its title band dark with a gold title and rule.
  - tooltip: dark with a dim-gold edge and light text, more padding.
- Text: the two fonts are a point bigger - 13 / 15 (were 12 / 14).
- Shots (MENUKEY now shoots the inventory it opens; DISCO; CLICK's pause
  menu): inventory, discoveries, the pause menu and a tooltip all in the new
  style.  MENUKEY + DIFFUP still 0 px missing; STATUSTIP all 17 tips right.
- Probe script trap: menu_probe.ps1 set its VULTURE_SELFTEST_* variable in
  the calling PowerShell, so a second probe in the same call inherited the
  first one's (and LIT never cleared) - it clears them all first now.

### 201. Sharp sprites when zoomed in

- Step 3 of the user's 4-2-1-3.  Zoom (1.5x / 2x / 3x, mouse wheel) drew the
  map at 1x and blew the picture up - nearest neighbour at 2x and 3x - so the
  2006 sprites turned into blocks; at 1x the dungeon is a small strip on a
  1080p screen.
- The art: gamedata/tiles_hd/ - every sprite (2459) at double resolution,
  made by toolchain/make_hd_tiles.py with Real-ESRGAN (realesrgan-ncnn-vulkan
  v0.2.5.0, the general x4plus model, run on the GPU; downloaded with the
  user's OK into deliverables/downloads/realesrgan) at 4x, brought to exactly
  2x with Lanczos, alpha kept and the original's empty margin kept empty.
  x4plus beat the anime and animevideov3 models on Vulture's pre-rendered 3D
  art: it smooths the jaggies and the old dithering without flattening.
- TRAP: one realesrgan-ncnn-vulkan process goes bad after ~20 images here -
  every later output stripes and noise (the first full run: 2418 of 2458
  ruined; any thread count, any tile size).  The script runs batches of 16,
  each a fresh process, and checks every output (shrunk back to 1x it must
  match the original where opaque: ~8-16 levels off when good, 40-230 when
  ruined); failures are redone alone, and anything still bad would fall back
  to a Lanczos 2x (22 sprites did).
- The engine (vulture_sdl.cpp s201): zoomed in with tiles_hd present, the GPU
  map layer is drawn AT the zoom - levelwin sets the view before the map
  pass (vulture_gpumap_set_view: the 1x region vzoom_apply would blow up,
  and where it lands), and every map draw - blits, posed sprites, cast
  shadows, the normal-map layer - goes through vgm_vx / vgm_vy, from the
  sprite's HD texture (linear filtering for anything without one).  The map
  textures stay screen-sized; the lighting shader maps each pixel back to
  the 1x map (u_view, u_vsc) so the light grid, point lights and mist line
  up; the compose step copies the map 1:1.  The screen shows exactly the
  region it did before, so mouse picking, the overlay effects (still
  blown up by vzoom_apply) and the HUD are unchanged.
  - HD art per surface: vulture_load_tile registers each tile's surface with
    its file (and the see-through walls' alpha); vtw_flipped, the shaded
    copies and the invisible hero register as mirrored / darkened / faded
    variants of it.  HD textures are cached by file + variant, so a tile the
    tile cache freed and reloaded finds its texture still made.
  - Without tiles_hd, with lighting in software (the map goes through
    vulture_screen) or with VULTURE_NO_HD, zoom is the old pixel blow-up.
- Terrain tiles (floor., wall., edge.) keep the original's HARD alpha,
  doubled: their soft upscaled edges left a hairline seam between
  neighbouring floor diamonds (seen at 2x on a carpet).
- Probe VULTURE_SELFTEST_ZOOM=n starts at zoom step n (0 1x, 1 1.5x, 2 2x,
  3 3x); VULTURE_NO_HD turns the HD path off for a before/after.
- COST: making the art ran the user's GPU/CPU flat out for ~40 minutes in
  all (the batches restart the upscaler each time); the user asked for no
  more of it.  The art is done and committed - do NOT run make_hd_tiles.py
  again on this machine.  At 1x zoom nothing in tiles_hd is ever loaded.

### 202. Impact: blows, misses, missiles, shattering

- The user: "add impact effects to attacks and thrown items and whatnot".
  A hit showed a damage number and blood (s85) and a big one a burst
  (s129); the lunge (s78) made the target recoil before the hit was even
  decided; misses, thrown things and breakage showed nothing.
- levelwin.cpp "s202" (vimp_*, vfl_*):
  - A hit on a creature in sight bursts at the point of contact in the
    style of what struck it (vimp_style_of: the weapon's damage type - S/P/B
    from oc_dir - or the creature's first melee attack): an edged weapon
    sweeps a bright arc and throws sparks, a pointed one jabs a streak
    through with a star at the tip, a blunt one flashes and rings the floor
    with grit, claws rake three red lines, a bite snaps shut, fire / frost /
    lightning attacks burst in their colour.  Sprayed away from the attacker
    (vulture_anim_attack records who swung at whom; a missile's flight says
    where from), sized by the share of HP it took, a light flash with it, and
    the creature flinches from the blow (vimp_recoil_xform, in vanim_xform).
  - vfx_scan sees non-lethal hits; a killing blow is caught in
    vulture_anim_death (the monster is gone before the next scan), and only
    when a blow or missile we saw did it - no burst for poison, traps,
    hunger.  The hero only bursts for a blow or missile too.
  - A miss ("You miss", "misses": vulture_msg_fx from vulture_putstr)
    whiffs past the target, who leans aside.
  - Thrown / fired / kicked things: the core's tmp_at now tells Vulture
    about DISP_FLASH / DISP_TETHER flights (display.c: vulture_flight_begin /
    step / end); the thing streaks through the air, and one stopped by a
    wall, closed door or tree chips stone, throws sparks (metal) or
    splinters (wood).  Glass breaking is breakobj's to show.
  - Shattering (dothrow.c breakobj, potion.c potionhit -> vulture_shatter_fx):
    glass shards glinting as they fall, and a splash in the potion's colour
    (the colour you see), yolk for eggs.
  - New particle mode 5 (a solid bit: chips, splinters, drops).
- Probe VULTURE_SELFTEST_IMPACT: two blows a 60-HP jackal survives, a
  killing blow, two bites back, a dagger flown into a wall through tmp_at,
  a potion shattering - each shot as it plays.

### 203. Doors swing; using things in the world shows

- The user: "add special effects for when you use objects in the game world
  such as doors, im thinking you could do simple inbetweens with code for
  things like doors".  A door popped from one sprite to the other.
- levelwin.cpp "s203" (vdr_*):
  - The closed door art is split once, at runtime, into its stone arch and
    its wooden panel (vdr_split: red well above blue = wood; a 5x5 majority
    over the opaque pixels removes the speckle of the rendered stone; only
    blobs of 150+ pixels count).  Both door arts are double doors whose
    leaves open AWAY from the viewer (the open art shows them so), so each
    half of the panel turns 90 degrees about its own pillar on the
    isometric floor.  In an orthographic iso view that is an affine map -
    every column slides along the turned direction and keeps its height -
    so each leaf is one textured quad (vulture_gpumap_quad, SDL_RenderGeometry;
    2 px strips in software).  The floor slope is V_MAP_YMOD / V_MAP_XMOD
    (22/56), not 2:1.
  - Opening: an ease-out with a 6% overshoot (it swings a touch past and
    settles), the open art fading in over the last 30%, the leaves fading
    out under it at the end.  Shutting: the open art fades out, the leaves
    gather speed shut, rebound 6 degrees, and a puff of dust rises along the
    sill.  Smashed in (closed -> broken): it shudders, splinters fly the way
    it falls, it topples away from you (or toward the viewer if you stand
    behind it) and fades, the arch with it (the broken art has none), the
    planks fading in.  Conjured (broken -> closed, a locking spell on an
    empty doorway): dust gathers in and the door rises out of it.
  - The hook: mapdata::set_glyph calls vdr_furniture when a square's
    furniture tile changes; only door-to-door changes in sight animate (a
    door simply coming into view does not).  vdr_draw replaces the tile at
    the map's furniture step, so walls and creatures in front still cover it.
  - Zoomed in, the arch and the panel use the HD art cut the same way
    (vulture_gpu_hd_derive_cut: the arch keeps everything outside the panel
    mask, so the two add up to the whole HD door - no pop at either end).
    The arch's normal map, minus the panel, goes to the light layer.
- vulture_world_fx (from the core): a fountain drunk from or dipped into
  (water wells up, ripples, glints), a sink's tap, an altar's amber or black
  flash where the message says one (a random colour while hallucinating -
  the message's colour is random then too), a thing landing on an altar
  (dust), a throne sat on (a golden shimmer), a chest or box opened on the
  floor (dust off the lid), a hidden door or passage found by searching
  (grit trickling down, a glint).
- The split's membership test also drops the H art's lit inner bevel that
  the smoothing took in (wood must be warm FOR ITS BRIGHTNESS - (r-b) at
  least 18% of r - or dark, a plank seam): without it a ragged notch opened
  in the arch while the panel was away.
- Waves (s198/s202 rings): one blooms from dim (alpha ramps up to 30 px
  across) and a small one uses fewer dots (about one per 4 px of its
  circumference) - they used to pile up into a white-hot disc at the start.
  Big explosion rings are unchanged.
- Probe VULTURE_SELFTEST_DOOR: the nearest door, then one with the other
  art, opened / shut / smashed / conjured as the core would set it, the
  swings shot held at set points (vdr_freeze); then each world effect.

### 204. Bodies fall over; boulders roll

- The user, after 203: "it looks literally 3d. can you add more shit like
  that?!" - more in-betweens made in code from the existing sprites, in the
  room's own isometric perspective.
- levelwin.cpp "s204" (vdth_*: deaths):
  - s79's death spun the sprite flat in the screen plane while it sank, in
    a BLOCKING 170 ms vanim_frames.  Now a slain creature topples on the
    clock (it never holds you up): it pivots on its feet onto the floor -
    the top of the sprite shears along the floor direction and the sprite
    foreshortens (one quad, as the door leaves), to 1.35 rad, rocks back,
    fades from t 0.62.  VDTH_TALL: 80 px of height to a square's length of
    floor.  Red from the blow at first, as s79 had it; a ghost thins upward.
  - Which way: away from whoever struck it (vimp_last), else along a
    missile's line (vfl_end), else away from you - but only its SIDE
    (screen left or right): it falls BACK along that side's floor axis.
    Falling toward the viewer turns the flat sprite edge-on - it thinned to
    a sliver in the first probe.
  - Its corpse waits for it (vdth_object_alpha: hidden while the body
    falls, faded in as it lands) and skips the s83 drop; the s151 thud is
    timed to the landing (330 ms).
- levelwin.cpp "s204" (vbr_*: boulders): dopush (hack.c) calls
  vulture_boulder_roll(from, to).  The boulder art is split once into the
  rock and the shadow baked under it (translucent dark pixels in its lower
  part); the shadow slides, the rock turns about its centre by the ground
  covered over its radius (screen x; toward or away from you a little
  less), shoved fast and slowed by friction over 280 ms, and the s158
  contact shadow follows it.  The hook tests the square the boulder LEAVES:
  vision still has the boulder in place when the core moves it, so the
  square it lands on counts as out of sight.
- GPU light: pieces drawn without normals were lit with the floor's relief
  under them - a rolling rock looked washed out, the floor's pattern showing
  through.  Quads now record their own normals
  (vulture_gpumap_normal_quad, replayed into the normal layer as a quad):
  door leaves (the panel's own masked normal map), falling bodies; the rock
  through normal_ex.  vulture_gpumap_quad takes a colour now.
- Probe VULTURE_SELFTEST_FALL: jackals killed to your screen right and
  left, boulders pushed by a real key - Vulture moves by the SCREEN, so the
  key is chosen from the screen direction ('l' is screen right = map NE);
  vfall_freeze holds the falls and rolls at set points.

### 205. The dungeon as solid blocks

- The user: "make the game look more 3d in general".  The creatures were
  already relief-lit (tiles_n has volume for nearly every sprite); what
  looked flat was the world: floors ended paper-thin against the void and
  nothing darkened where a wall met the floor.
- levelwin.cpp "s205" (vao_*, vslab_*), from the floor pass:
  - Contact shadow: a floor square darkens softly along each back edge
    (screen up-left / up-right) where rock or a wall stands behind it -
    two 110x44 diamond masks, 150 alpha at the edge falling to nothing
    halfway in, corners doubling up.
  - Slab faces: where a front edge (screen down-left / down-right) meets
    rock or the unknown, the cut face of the slab hangs below it: a
    generated rock section (lit rim, grain, strata) fading into the dark
    over 28 px, the left-facing face lit and the right-facing one at 70%.
    Queued in the floor pass and drawn after it - the void's own black
    tiles would paint over them.  On the square grid's diamond (the floor
    art is 109 px across a 112 px step), or neighbouring faces leave seams.
  - "Floor" is V_TILE_FLOOR_COBBLESTONE..DARK (rooms, corridors, doorways,
    water, ice, lava); anything else is rock to these.
- vulture_sdl.cpp vgm_dof_pass (s205), in vgm_compose between the map layer
  and the overlay: depth of field.  The composed map is halved three times
  (2x2 averages) and laid back upscaled in 8 px strips whose alpha rises
  with distance from the focus (you: levelwin sets it each frame) - sharp
  within 24% of the screen height, full softening 20% further.  The
  overlay (effects, text, HUD) stays sharp.  A room around you is always
  in focus; big caves and far parts soften at the top and bottom.
  vulture.conf depth_blur=0 turns it off (vulture_opts.depth_blur, default
  1); probes: VULTURE_NO_DOF, VULTURE_DOF_BAND (percent).
- Probe VULTURE_SELFTEST_LOOK: magic mapping, then the whole frame
  (look_full.bmp).  Remembered parts stay dark under the fog of war.

### 206. A living dungeon

- The user: "i want the game to feel more alive even and look more 3D".
  Ambient life already existed (dust, drips, embers s160; halos, water
  glints, lava pulses s171; living fog s192; breathing s85; flyers bob s82),
  so this round gives the creatures and the furniture behaviour.
- levelwin.cpp "s206" (vlife_*), at the creature step:
  - A change of facing is a turn in depth: the sprite narrows to an edge
    and opens out the other way over 150 ms (|cos| on its width, the face
    switching at the middle) instead of a mirror flip - walking, watching,
    glancing alike.  Facing 0 (never moved) counts as the art's own way, so
    a first look doesn't "turn".
  - Awake hostiles in view face you and keep watching (screen x against
    yours; always, whether or not it can see you - its perception stays
    hidden).  Pets and peacefuls glance about (40% of their moments; a pet
    sometimes at you); every 2.5-8 s an awake creature standing still
    fidgets: a small one (MZ_SMALL or less, not flying) hops 8 px - its
    s158 shadow stays on the floor - with a squash on landing; others shift
    their weight or stretch.  Sleepers (shown as asleep anyway, s137) keep
    still.  You too, every 6-13 s.  Records by m_id (0xFFFFFFFF is you),
    forgotten 10 s out of sight.
- vtree_draw (the furniture step): trees sway - the crown sheared over a
  fixed foot by a breeze that rises and dies down, each tree its own phase.
- vlive_furniture (the overlay pass, furniture in sight, stateless): a
  fountain's plume shimmers and glitters and drops fall into its basin; a
  sink's tap drips and splashes; incense curls up from altars; a low mist
  creeps about graves; a throne's gold studs glint; trees drop a leaf.
- All under the Atmosphere switch (vfx_ambient).
- Probe VULTURE_SELFTEST_LIFE: furniture on the nearest free floor round
  you, a hostile jackal, a peaceful gnome, a newt; a burst of frames, then
  you stand on the jackal's other side (on screen) and it turns.
  VULTURE_LIFE_BUSY makes the fidgets come every 0.3-0.8 s.

### 207. Lids lift; thrown things arc

- The user: "do those too" - the two follow-ups from 204: chest lids and
  missiles flying on an arc with a shadow.
- Lids (levelwin.cpp vlid_*): a chest, a large box or an ice box opened on
  the floor (use_container: vulture_lid(x, y, 1) where s203 puffs the
  dust; vulture_lid(-1, -1, 0) at containerdone, the one exit) lifts its
  lid on its back hinge.  Each art's top face is four hand-fitted corners
  (vlid_fit: front-left, front-right, back-right, back-left) plus the lid's
  own depth below the front/side top edges; the lid is cut off the body
  along those edges.  The lid is taken as the plate of the top face: each
  corner of its box is solved into (along the hinge, toward the front)
  and its depth swung up off the floor - one affine quad again; the dark
  inside fills the top face.  VLID_VR 1.3 (px up per screen-x px of floor)
  matches the projection: the floor's 22/56 squash is a 23-degree camera.
  At that camera a lid hinged along a floor axis goes edge-on at ~31
  degrees (tan = det / (ax * VR * |fx|)), and past it you see its
  underside - shaded 170.
  The lift plays as a short blocking clip (<= 380 ms, a waiting key
  hurries it) because the loot menu is modal and the map doesn't animate
  under it; the lid stays up while you rummage and shuts on the clock -
  down under its weight, a small bounce, a puff of dust.
- Arcs (vfl_draw_arc, the s202 flight): throwit and m_throw tell Vulture
  the range, whether the thing is fired from its launcher, and the thrower's
  square (vulture_flight_range) just before bhit / tmp_at.  The thing is
  drawn in the air instead of the core's glyph (vfl_hides at the object
  step), gliding between the core's squares one step behind - the step's
  own length - up off the floor: hand height (26 px) to 6 px at full
  range, plus a rise of 3 + 0.8 R (fired), 12 + 3 R (glass, lobbed),
  10 + 2.6 R (stones) or 8 + 2.2 R (thrown weapons), capped at 40 - so a
  thing that hits early hits a body, not feet.  Its shadow on the floor
  under it; thrown things tumble (fired ones fly straight); the streak
  follows the arc (dense, 0.05-square samples - 0.15 came out dotted).
- Probes: VULTURE_SELFTEST_LID (each box held at points of opening, then
  shutting on the clock), VULTURE_SELFTEST_ARC (dagger, arrow, rock through
  the core's tmp_at, shot each step).

### 208. Solid back walls

- The user: "make the game look more 3D" (again).  The walls looked thin:
  the conf's wall_opacity 0.6 (see-through walls) was baked into every
  wall tile's alpha, so a room's back walls - brick faces with their tops
  drawn as a band - let the black void through and read as dark glass.
- What the wall tiles are (worth knowing): a wall tile belongs to the WALL
  square, one per side facing open floor (get_wall_tiles).  A room's back
  walls are its north / west wall squares' south / east tiles (brick faces
  with a top band); its front walls are its south / east wall squares'
  north / west tiles - the walls' room side seen from behind, which the
  art draws as half-transparent black panels (alpha 127) with a dark top
  line: the cutaway's ghost walls.
- vulture_tile.cpp (vwall_back): wall_opacity now applies to the front
  walls only (and to their HD art); a back wall loads solid.  The room
  reads as a box - solid back walls with their tops, the front cut away -
  on its slab (s205).  The see-through setting keeps its use: seeing into
  the room past the walls in front of it.
- Tried first and dropped: solid tops laid along every wall's top edge
  (the matching south tile's band, straightened).  On the back walls they
  only repeated the art's band; on the front walls - whose bodies are
  ghost panels - they floated over the floor like rails.

### 209. Full-height solid walls, see-through round you; shine; the void's depth

- The user: "do both [shine; depth in the void], make all walls completely
  visible but let me see through them if im around them in a circle", then
  mid-round "make it so walls are full height and not half height".
- Full height: the default at every layer - vulture_opt.cpp's compiled
  default, levelwin's style-switch fallback (walls_full), the shipped
  gamedata/config/vulture.conf (and the two playable build folders' copies,
  untracked) and the user's own %APPDATA% conf (wall_style=full; it wins
  over the shipped one).  The July QoL round (section 28) had set half
  height because full walls hid what stood behind them; the circle below
  is what makes full height playable.  F7 still toggles.
- Front walls solid (levelwin vwall_draw / vwall_outer): a room's front
  walls are its south / east wall squares' north / west tiles - the walls'
  room side seen from behind, drawn by the art as faint black panels.  They
  are now drawn as their style's OUTER face instead (the style's south /
  east tile - brick face plus top band), moved onto the same edge: the
  north tile's face drawn at (x + 56, y - 22), the west tile's at
  (x - 56, y - 22).  Every wall stands on the room's edge (a thin-wall
  model): back faces and front faces meet at the room's corner points, so
  corners close with no extra pieces.  Side effect: a front doorway's two
  jambs stick out one square toward you, framing the door as a short
  passage.
- The circle: a wall face whose FOOT LINE, carried on to your x, runs in
  front of your feet (lower on the screen) is laid with
  vulture_gpumap_holed - an SDL_RenderGeometry grid of 6 px cells whose
  vertex alpha falls to wall_opacity within 56 px of your body's middle
  (feet - 34) and returns by 124 px; its normal map is recorded the same
  way (vgm_nrec.holed) so the relief light matches.  Walls beside and
  behind you never fade (the foot-line test, not a row test: a back wall's
  face on your own row, 56 px to your side, has its foot line 22 px BEHIND
  you).  Crossing a doorway the amount eases over 22 px of foot distance,
  so the hole fades rather than pops.  The software path fades the whole
  face by its nearest point.
- wall_opacity now means how solid walls stay inside the circle (1.0 turns
  the circle off).  Default 0.3 (was 0.6, which would leave you 40%
  visible); the conf text and the manual say so.
- Doorway sides: get_wall_decor styles a wall side by the floor it faces,
  so faces toward a doorway or corridor (rough floor) came out ROUGH
  (brown rock) - unnoticed while front walls were faint panels, glaring
  once solid.  get_wall_tiles now styles such a side by a room floor
  beside the wall square (orthogonal first, then diagonal - a door next to
  a corner has its jamb on the corner block, which touches the room only
  corner to corner).  Walls with no room floor near (mines, corridors)
  stay rough.
- Shine (vgl_fs + vl_nmap): the point-light glint is scaled by a gloss
  from the albedo - (1 - saturation) x smoothstep(0.25, 0.75, brightest
  channel): grey and bright (metal, armour, wet stone) up to 2.6x, colour
  (cloth, wood, skin) and dark 0.6x; cap 40 -> 96.  Same in the CPU relief.
- The void's depth (vvoid_draw, overlay pass): 70 + 40 dim blue-grey motes
  in two layers that follow the map at 0.45 / 0.72 of its motion (a
  parallax as the view scrolls), drift slowly and twinkle; drawn only where
  the screen point's square, the two behind it (slab faces hang down) and
  six in front (walls rise up) are all unmapped.
- Probe VULTURE_SELFTEST_WALLS: you behind a front wall (shot with and
  without the circle), then just inside a front doorway (the same two).

### 210. What the walls hide: outlines; doors in the circle

- The user, asked what else: "do all of these in order" - eight items:
  outlines through walls, doors in the circle, wall torches, bloom and
  colour grading, water reflections, floor details, a stairs transition,
  a frame-rate check.  This is the first two.
- Outlines (vxr_draw, overlay pass, unlit): full-height walls hide
  whatever stands behind them.  vwall_draw records every face it lays as
  a foot line (y = y0 + k (X - x0) over 56 px) and a height (-ymod of its
  art); the monster step records each creature with its pose (tile,
  facing, tx/ty, scale, turn).  A creature's pixels that a LATER face
  covers are drawn as an outline band (2 px, alpha 215) and a faint fill
  (48) in its colour - red hostile, yellow peaceful, green tame, grey if
  only remembered or sensed - mapped back through the same scale and turn
  about the feet that vtx_blit used, flipped as vtw_put_tile_x chose.
  Where the see-through circle already shows it the outline fades by the
  wall's own alpha there.  Masks are cached per tile and facing.  You get
  none (the circle shows you).
- Doors: a door square's furniture in a wall in front of you is laid with
  vhole_put (split out of vwall_draw, which shares it), by how far in
  front of your feet the door's square is.
- Probe VULTURE_SELFTEST_XRAY places a sleeping hostile jackal and a
  peaceful gnome behind front walls; seen in the shots instead: a jackal's
  legs and a newt wholly behind a front wall, outlined in red.

### 211. Torches on the walls of lit rooms

- A lit room's back walls (the S/E faces you look at) carry a torch in an
  iron sconce on every third square ((2j + i + 7 x ledger) % 3, so one in
  three whichever way the wall runs); not on banners, paintings or niches
  (a pillar will do); dark rooms none.  vtorch_draw runs in step 7 right
  after the face, so walls and creatures in front hide it.
- The sconce is a 9 x 16 procedural sprite; the flame is additive blobs on
  the MAP layer (flicker, lean) plus a 49 px warm glow on the wall round
  it; out of sight only the sconce shows.
- Each seen torch is a point light in vl_compute (radius 2.6, 0.34, warm,
  lifted 52 px): relief light, shine, creature shadows and the light pools
  move with it.  The torch list comes from the map pass, which runs before
  vl_render in the same frame.
- First cut was 7 px of flame on busy wall art: unreadable at 1080p; every
  fourth square plus "no decorated walls" left brick rooms (4 in 5 squares
  decorated) with one torch.  Probe VULTURE_SELFTEST_TORCH.

### 212. Bloom, colour grades, dark wall outsides, item outlines

- Bloom (vgm_bloom_pass, vulture_sdl.cpp, in vgm_compose before the depth
  of field): vgm_out's map is halved, a reverse-subtract fill takes off
  everything under T per channel, three more halvings, and the four steps
  are added back upscaled (the two widest twice).  The overlay is not
  bloomed.  ⭐ The legacy GL renderer refuses a custom blend mode whose
  colour and alpha OPERATIONS differ (GL_SupportsBlendMode) - the first
  try logged "no subtract blending" and switched itself off; the alpha now
  uses REV_SUBTRACT too, with factors (0, 1) so it is unchanged.  T 215
  hazed white marble over; T 230.  VULTURE_NO_BLOOM / _BLOOM_T / _BLOOM.
- Colour grades (vgrade_area -> vulture_gpumap_set_grade, a MOD fill over
  the map in the same pass): Mines warm, Sokoban cool, Gehennom red,
  Ludios gold, Vlad's violet, Medusa sea-green, Quest faint violet, each
  Plane its element; the main dungeon untouched.  Probe
  VULTURE_SELFTEST_GRADE forces each in turn on one view.
- The user, mid-round: "the outside of the walls shouldnt be lit".  The
  converted front faces (s209) are a wall's OUTSIDE but were lit like the
  room.  vwall_draw now darkens them (vhole_dim: the same tile again in
  black at alpha dim, holed the same way if it is in the circle) by what
  the wall faces beyond it: rock (IS_OBSTRUCTED, the 5.0 name for IS_ROCK)
  is dark (190); open ground lends its light-grid light, and ground you
  have not seen has none.  ⭐ The light grid gives the stone round a lit
  room about 0.5 (logged), so it cannot say "dark" by itself.  A
  decorated wall's outside is plain brick (banners and paintings hang
  inside).
- The user, mid-round: "also have outlines for items".  The object step
  records each item as an outline candidate too (pale blue); vxr_draw
  maps it like a creature with no turn or scale.  Probe XRAY now also
  drops a chest behind a front wall.

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
