#!/bin/bash
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack
cd "$P/src" || exit 2
MK="make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=Y USE_LUADLL=N SKIP_NETHACKW=Y"
rm -f "$P/binary/NetHackV.exe"
$MK nethackv > /tmp/dbgbuild2.log 2>&1
echo "debug build rc=$?"
cp "$P/binary/NetHackV.exe" "$OUT/NetHackV-dbg.exe" || exit 2
cd "$OUT" || exit 2
cat > /tmp/gdbcmd3 <<'EOF'
set pagination off
set confirm off
set breakpoint pending on
break nethack_exit
break exit
break vulture_bail
break vulture_exit_nhwindows
run
bt 25
quit
EOF
timeout 150 gdb -q -batch -x /tmp/gdbcmd3 ./NetHackV-dbg.exe 2>&1 | tail -40
