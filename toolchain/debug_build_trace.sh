#!/bin/bash
# Build NetHackV WITH debug info so gdb can symbolize, then trace the exit.
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack

cd "$P/src" || exit 2
MK="make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=Y USE_LUADLL=N SKIP_NETHACKW=Y"
rm -f "$P/binary/NetHackV.exe"
$MK nethackv > /tmp/dbgbuild.log 2>&1
echo "debug build rc=$?"
grep -iE "Error [0-9]|undefined ref" /tmp/dbgbuild.log | head -10
ls -la "$P/binary/NetHackV.exe" 2>/dev/null | awk '{print "  ", $5, "bytes"}'
cp "$P/binary/NetHackV.exe" "$OUT/NetHackV-dbg.exe" || exit 2

cd "$OUT" || exit 2
cat > /tmp/gdbcmd2 <<'EOF'
set pagination off
set confirm off
set breakpoint pending on
break nethack_exit
break exit
break vulture_bail
run
bt 25
echo \n=== continuing ===\n
continue
bt 25
quit
EOF
timeout 150 gdb -q -batch -x /tmp/gdbcmd2 ./NetHackV-dbg.exe 2>&1 | tail -60
