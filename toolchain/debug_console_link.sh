#!/bin/bash
# Temporarily link NetHackV as a CONSOLE app so early errors are visible.
P=/c/Users/dalla/Vulture/vulture5-win
G=$P/sys/windows/GNUmakefile
sed -i 's|\$(ld) \$(LDFLAGS) -mwindows \$^ \$(HLHACKLIB) \$(LIBS) \$(SDL2LIBS)|$(ld) $(LDFLAGS) -mconsole $^ $(HLHACKLIB) $(LIBS) $(SDL2LIBS)|' "$G"
grep -n "mconsole\|mwindows" "$G" | sed -n '1,6p'
cp "$P/sys/windows/GNUmakefile" "$P/src/GNUmakefile"
sed -i 's/^NHV=370$/NHV=500/' "$P/src/GNUmakefile"
cd "$P/src" || exit 2
rm -f "$P/binary/NetHackV.exe"
make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 \
     DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y nethackv > /tmp/dbg_link.log 2>&1
echo "relink rc=$?"
grep -iE "error|undefined" /tmp/dbg_link.log | head -5
ls -la "$P/binary/NetHackV.exe" 2>/dev/null | awk '{print $5" bytes"}'
cp "$P/binary/NetHackV.exe" /c/Users/dalla/Vulture/VultureNetHack/NetHackV-console.exe 2>/dev/null && echo "copied as NetHackV-console.exe"
