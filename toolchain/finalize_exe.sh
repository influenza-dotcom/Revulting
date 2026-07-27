#!/bin/bash
# Restore the GUI subsystem link (no console window) and produce the final
# double-clickable NetHackV.exe in the package.
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack
G=$P/sys/windows/GNUmakefile

echo "=== restore -mwindows on the nethackv link rule ==="
sed -i 's|\$(ld) \$(LDFLAGS) -mconsole \$^ \$(HLHACKLIB) \$(LIBS) \$(SDL2LIBS)|$(ld) $(LDFLAGS) -mwindows $^ $(HLHACKLIB) $(LIBS) $(SDL2LIBS)|' "$G"
sed -n '1394p' "$G"

cp "$G" "$P/src/GNUmakefile"
sed -i 's/^NHV=370$/NHV=500/' "$P/src/GNUmakefile"

cd "$P/src" || exit 2
rm -f "$P/binary/NetHackV.exe"
make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 \
     DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y nethackv > /tmp/final.log 2>&1
rc=$?
echo "build rc=$rc"
[ $rc -ne 0 ] && { grep -iE "error|Error [0-9]" /tmp/final.log | head -15; exit 1; }

echo "=== subsystem check (must be GUI, not console) ==="
objdump -p "$P/binary/NetHackV.exe" | grep -i "^Subsystem" || \
  objdump -p "$P/binary/NetHackV.exe" | grep -i subsystem

cp "$P/binary/NetHackV.exe" "$OUT/NetHackV.exe" || exit 2
ls -la "$OUT/NetHackV.exe" | awk '{print "  NetHackV.exe", $5, "bytes"}'

echo "=== drop the diagnostic builds from the shipped folder ==="
rm -f "$OUT/NetHackV-console.exe" "$OUT/NetHackV-dbg.exe" && echo "  removed"

echo "=== unresolved imports (should all be Windows system DLLs) ==="
objdump -p "$OUT/NetHackV.exe" | awk '/DLL Name:/{print $3}' | while read d; do
  [ -f "$OUT/$d" ] || echo "  system: $d"
done

echo "=== package size ==="
du -sh "$OUT" | cut -f1
