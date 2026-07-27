#!/bin/bash
# Rebuild win/vulture + relink NetHackV, then refresh the package binaries.
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack

cd "$P/src" || exit 2
MK="make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y"
rm -f "$P/binary/NetHackV.exe"
$MK nethackv > /tmp/relink.log 2>&1
rc=$?
echo "build rc=$rc"
if [ $rc -ne 0 ]; then
  grep -iE "error|Error [0-9]" /tmp/relink.log | head -20
  exit 1
fi
ls -la "$P/binary/NetHackV.exe" | awk '{print "  NetHackV.exe", $5, "bytes"}'
cp "$P/binary/NetHackV.exe" "$OUT/NetHackV-console.exe" && echo "  -> package (console variant)"
