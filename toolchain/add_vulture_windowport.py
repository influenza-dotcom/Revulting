#!/usr/bin/env python3
"""Add a third windowport (Vulture / SDL2 isometric) to NetHack 5.0.0's
sys/windows/GNUmakefile, by cloning the NetHackW (MSWIN_GRAPHICS) block.

Produces NetHackV.exe:
  * -mwindows  (no console window)
  * WIN32CON still DEFINED so consoletty.c compiles and keeps GUILaunched/msmsg
    defined; windmain.c was patched so it does not actually enter the console tty
  * TILES_IN_GLYPHMAP so the generated tile.c (with the Path A remap) is used
  * the vulture windowport itself is built by win/vulture/GNUmakefile into a
    single partial-linked object, exactly as on the Linux build
"""
import io, sys

G = r"C:\Users\dalla\Vulture\vulture5-win\sys\windows\GNUmakefile"
s = io.open(G, encoding="latin-1").read()

if "NetHackV.exe" in s:
    print("vulture windowport block already present")
    sys.exit(0)

BLOCK = r'''
#==========================================
# nethackv  (Vulture - SDL2 isometric windowport)
#==========================================

VULTUREDIR   = ../win/vulture
VULTUREBUILD = $(VULTUREDIR)/build_nethack_3_6_0
VULTUREOBJ   = $(VULTUREBUILD)/vulture.o
THEORAPLAYOBJ = $(VULTUREBUILD)/theoraplay.o

SDL2CFLAGS = $(shell sdl2-config --cflags)
# NB: drop -lSDL2main - it expects an SDL_main symbol, and NetHack keeps its own
# main() in windmain.c. win/vulture defines SDL_MAIN_HANDLED for the same reason.
SDL2LIBS   = $(filter-out -lSDL2main -mwindows,$(shell sdl2-config --libs)) \
	     -lSDL2_mixer -lSDL2_ttf -lpng -lz -logg -lvorbis -lvorbisfile \
	     -ltheoradec

CFLAGSV = $(CFLAGS) $(CFLAGSXTRA) $(SOUNDLIBINCL) $(COMMONDEF) $(DLBFLG) \
	  -D_CONSOLE -DWIN32CON -DTILES_IN_GLYPHMAP \
	  -DVULTURE_GRAPHICS -DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 \
	  -DNOTTYGRAPHICS -I$(VULTUREDIR) $(SDL2CFLAGS) $(SOUNDLIBDEFS)
CPPFLAGSV = $(CFLAGS) $(CPPFLAGSXTRA) $(COMMONDEF) $(DLBFLG) \
	  -D_CONSOLE -DWIN32CON -DTILES_IN_GLYPHMAP \
	  -DVULTURE_GRAPHICS -DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 \
	  -DNOTTYGRAPHICS -I$(VULTUREDIR) $(SDL2CFLAGS) $(SOUNDLIBDEFS)

ONHV = $(O)nethackv
NHVONLY = $(addsuffix .o, tile consoletty)
NHVOBJS = $(addprefix $(ONHV)/, $(COREOBJS) $(NHVONLY))
DATEV_O = $(addsuffix .o, $(addprefix $(ONHV)/, date))
NHVTARGETS = $(GAMEDIR)/NetHackV.exe

nethackv: $(NHVTARGETS)

$(GAMEDIR)/NetHackV.exe: $(NHVOBJS) $(DATEV_O) $(VULTUREOBJ) $(THEORAPLAYOBJ) \
			 $(LUALIB) $(HLHACKLIB) | $(GAMEDIR)
	$(ld) $(LDFLAGS) -mwindows $^ $(HLHACKLIB) $(LIBS) $(SDL2LIBS) \
		-static-libgcc -static-libstdc++ -lstdc++ $(SOUNDLIBLIBS) -o$@

# the vulture windowport is built by its own makefile into one partial object
.PHONY: vulture-force
vulture-force:

$(VULTUREOBJ): vulture-force
	$(MAKE) -C $(VULTUREDIR) VARIANT_NAME=nethack VARIANT_VERSION=3.6.0 \
		BASECFLAGS="-Wall -g -DVULTURE_GRAPHICS -DWIN32 -include vulture_nh5compat.h"

$(THEORAPLAYOBJ): $(VULTUREDIR)/vendor/theoraplay/theoraplay.c | $(ONHV)
	@mkdir -p $(VULTUREBUILD)
	$(cc) $(CFLAGS) $(SDL2CFLAGS) $< -o$@

$(ONHV)/%.o: $(SRC)/%.c $(NHLUAH) | $(ONHV)
	$(cc) $(CFLAGSV) $< -o$@

$(ONHV)/date.o: $(SRC)/date.c $(NHVOBJS) | $(ONHV)
	$(cc) $(CFLAGSV) $(GITHASH) $(GITBRANCH) $(GITPREFIX) $< -o$@

$(ONHV)/cppregex.o: $(SSYS)/cppregex.cpp $(NHLUAH) | $(ONHV)
	$(cc) $(CPPFLAGSV) $< -o$@

$(ONHV)/%.o: $(SSYS)/%.c $(NHLUAH) | $(ONHV)
	$(cc) $(CFLAGSV) $< -o$@

ifeq "$(SOUND_WINDSOUND)" "Y"
$(ONHV)/%.o: ../sound/windsound/%.c $(NHLUAH) | $(ONHV)
	$(cc) $(CFLAGSV) $< -o$@
endif

$(ONHV)/%.o: $(MSWSYS)/%.c $(NHLUAH) | $(ONHV)
	$(cc) $(CFLAGSV) $< -o$@

$(ONHV)/%.o: $(WSHR)/%.c $(NHLUAH) | $(ONHV)
	$(cc) $(CFLAGSV) $< -o$@

$(ONHV):
	@mkdir -p $@

CLEAN_DIR += $(ONHV)
CLEAN_FILE += $(NHVTARGETS) $(NHVOBJS)

'''

ANCHOR = "#==========================================\n# nethack\n#==========================================\n"
if ANCHOR not in s:
    print("ANCHOR NOT FOUND - makefile layout differs"); sys.exit(1)
s = s.replace(ANCHOR, BLOCK + ANCHOR, 1)
io.open(G, "w", encoding="latin-1", newline="\n").write(s)
print("inserted nethackv windowport block into sys/windows/GNUmakefile")
