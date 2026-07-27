#!/bin/bash
# Build NetHackV.exe - the Vulture SDL2 isometric windowport, native Windows.
set -o pipefail
P=/c/Users/dalla/Vulture/vulture5-win
cd "$P/src" || exit 2

# refresh the staged makefile (it lives in sys/windows and we just edited it)
cp ../sys/windows/GNUmakefile* . && sed -i 's/^NHV=370$/NHV=500/' GNUmakefile

MK="make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y"
echo "=== build the vulture windowport object first (its own makefile) ==="
$MK ../win/vulture/build_nethack_3_6_0/vulture.o > /tmp/v_win.log 2>&1
echo "rc=$?"
echo "--- errors ---"
grep -inE "error:|\*\*\* |No rule" /tmp/v_win.log | grep -vi warning | head -25
echo "--- object? ---"
ls -la "$P/win/vulture/build_nethack_3_6_0/vulture.o" 2>/dev/null
echo
echo "=== then link NetHackV.exe ==="
$MK nethackv > /tmp/v_link.log 2>&1
echo "rc=$?"
grep -inE "error:|\*\*\* |undefined reference|No rule" /tmp/v_link.log | grep -vi warning | head -25
echo "--- product ---"
ls -la "$P/binary/"*.exe 2>/dev/null
