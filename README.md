# Revulting

**NetHack 5.0.0 with the Vulture isometric interface, ported to SDL2 and
native Windows — then pushed toward 2027.**

Revulting is an unofficial, fan-made modification. It is a port of the
Vulture graphical interface (the
Vulture's Eye / Vulture's Claw lineage, also published on Steam as
*Vulture for NetHack*) onto NetHack 5.0.0, plus a long list of fixes and new
features. It is not affiliated with or endorsed by the NetHack DevTeam or the
Vulture authors.

## What's different

**The port**
- NetHack 5.0.0 core with the Vulture isometric renderer, SDL1.2 → SDL2,
  built natively for Windows (MSYS2 / MinGW-w64).
- Fixes a long-standing stock-Vulture crash (minion priests / hallucination
  in the endgame), keypad and menu bugs, tile-mapping gaps, cursor ghosting.
- A main menu with a hero roster, view rotation (PageUp/PageDown), many
  quality-of-life commands (auto-explore, smart stairs, quick fire/attack,
  photo album, corpse freshness, message history, ...).

**Feel**
- Smooth tweened movement for the hero, monsters and camera; sprites face
  the direction they travel.
- Screen shake and a red flash when hurt, a low-HP vignette, footsteps.
- Music that follows where you are (shop, town, Mines, Oracle, Gehennom,
  battle) and fades between zones; quieter shop music.
- Whole-word sound-effect matching ("You catch fire" no longer meows) and
  new synthesized effects.
- The right-click menu offers NetHack 5.0's own context actions for the
  square (doors, monsters, your own square, far squares).

**Optional local-AI features** (need [Ollama](https://ollama.com) running on
the same PC; nothing leaves the machine)
- `OPTIONS=llmchat:<model>` — every message is reworded fresh each time,
  with checks that keep its meaning (numbers, names, item and monster words,
  status words must survive; nothing new may be added).
- `OPTIONS=llmtalk` — type what you say to monsters.
- Pets and peaceful talkers get names; ask your god a question after a
  prayer that brought nothing; `OPTIONS=heroquips` — your hero mutters
  remarks about their situation.
- `OPTIONS=voices` — text-to-speech for spoken lines.  Uses Piper neural
  voices when `piper/piper.exe` and `piper/voices/en_US-ryan-medium.onnx`,
  `en_US-amy-medium.onnx`, `en_GB-northern_english_male-medium.onnx` sit in
  the game folder (Piper 2023.11.14-2 from github.com/rhasspy/piper, voices
  from huggingface.co/rhasspy/piper-voices; strip multi-character keys from
  the voices' `phoneme_id_map`), otherwise the Windows SAPI voices.

**New gameplay**
- Cutting down a tree with an axe yields logs; `#build` turns rocks into
  walls and logs into doors (repairs dug-through walls).
- `bestroll` / `bestroll_melee` options: best-possible starting rolls for
  Tourists and Wizards (within what the dice can actually roll).
- The mail daemon is back on Windows — and can deliver messages from a
  Discord channel (see `discord.txt.example` in the docs; your own bot,
  your own token, which never leaves your PC except to discord.com).

## Layout

- `vulture5-win/` — the full NetHack 5.0.0 + Vulture source tree.
- `toolchain/` — build and validation scripts (MSYS2 bash / Python).
- `deliverables/` — reports and baselines from the porting work.
- `IMPLEMENTATION_LOG.md` — the engineering log: every change, why, and the
  traps found along the way.

## Building

From an MSYS2 MINGW64 shell with the SDL2, SDL2_mixer, SDL2_ttf, libpng,
libogg/vorbis/theora and Lua 5.4 packages installed:

```
bash toolchain/finalize_exe.sh
```

The scripts currently assume the tree lives at `C:\Users\<you>\Vulture`;
adjust the paths at the top of the scripts for another location.

## License

NetHack and Vulture are distributed under the **NetHack General Public
License** — see `vulture5-win/dat/license`. Revulting's changes are offered
under the same license. Bundled third-party code (SDL2, theoraplay, fonts)
keeps its own license.
