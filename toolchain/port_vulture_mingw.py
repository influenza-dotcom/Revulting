#!/usr/bin/env python3
"""Make win/vulture buildable with mingw-w64 on MSYS2."""
import io, os, sys, glob

V = r"C:\Users\dalla\Vulture\vulture5-win\win\vulture"
done, fails = [], []

def edit(path, old, new, what, required=True):
    p = os.path.join(V, path)
    if not os.path.exists(p):
        (fails if required else done).append(what + " (file missing)"); return
    s = io.open(p, encoding="latin-1").read()
    if new.split("\n")[0].strip() and new.split("\n")[0] in s and old not in s:
        done.append(what + " (already)"); return
    if old not in s:
        (fails if required else done).append(what + " (anchor miss)"); return
    io.open(p, "w", encoding="latin-1", newline="\n").write(s.replace(old, new, 1))
    done.append(what)

# 1. GNUmakefile: add a Windows/MSYS arm. -no-pie is a Linux-only driver flag.
edit("GNUmakefile",
"""UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	PARTIAL_LINK_FLAGS := -nostdlib -Xlinker -r -lc++
else
	PARTIAL_LINK_FLAGS := -nostdlib -no-pie -Xlinker -r -lstdc++
endif""",
"""UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	PARTIAL_LINK_FLAGS := -nostdlib -Xlinker -r -lc++
else ifneq (,$(findstring MINGW,$(UNAME_S)))
	# mingw-w64: -no-pie is a Linux-only gcc driver flag
	PARTIAL_LINK_FLAGS := -nostdlib -Xlinker -r -lstdc++
else ifneq (,$(findstring MSYS,$(UNAME_S)))
	PARTIAL_LINK_FLAGS := -nostdlib -Xlinker -r -lstdc++
else
	PARTIAL_LINK_FLAGS := -nostdlib -no-pie -Xlinker -r -lstdc++
endif""",
"GNUmakefile: mingw partial-link flags")

# 2. SDL2 on Windows renames main -> SDL_main unless the app opts out. NetHack
#    keeps its own main() in windmain.c, so every TU that includes SDL.h must
#    declare SDL_MAIN_HANDLED first. vulture_sdl.h is the common include.
p = os.path.join(V, "vulture_sdl.h")
if os.path.exists(p):
    s = io.open(p, encoding="latin-1").read()
    if "SDL_MAIN_HANDLED" not in s:
        marker = "#include <SDL.h>"
        if marker in s:
            s = s.replace(marker,
                "/* NetHack keeps its own main() (sys/windows/windmain.c). Without this,\n"
                " * SDL2 on Windows #defines main -> SDL_main and the link fails. */\n"
                "#ifndef SDL_MAIN_HANDLED\n#define SDL_MAIN_HANDLED 1\n#endif\n"
                "#include <SDL.h>", 1)
            io.open(p, "w", encoding="latin-1", newline="\n").write(s)
            done.append("vulture_sdl.h: SDL_MAIN_HANDLED")
        else:
            fails.append("vulture_sdl.h: no #include <SDL.h>")
    else:
        done.append("vulture_sdl.h: SDL_MAIN_HANDLED (already)")

# 3. SDL_SetMainReady() must be called before SDL_Init when main is handled.
for cand in ("vulture_sdl.cpp",):
    p = os.path.join(V, cand)
    if not os.path.exists(p): continue
    s = io.open(p, encoding="latin-1").read()
    if "SDL_SetMainReady" in s:
        done.append(cand + ": SDL_SetMainReady (already)"); continue
    i = s.find("SDL_Init(")
    if i < 0:
        fails.append(cand + ": no SDL_Init call"); continue
    line_start = s.rfind("\n", 0, i) + 1
    indent = ""
    for ch in s[line_start:i]:
        indent += ch if ch in " \t" else ""
    s = s[:line_start] + indent + "SDL_SetMainReady();  /* we define SDL_MAIN_HANDLED */\n" + s[line_start:]
    io.open(p, "w", encoding="latin-1", newline="\n").write(s)
    done.append(cand + ": SDL_SetMainReady")

print("APPLIED:")
for d in done: print("  ok  ", d)
if fails:
    print("FAILED:")
    for f in fails: print("  FAIL", f)
sys.exit(1 if fails else 0)
