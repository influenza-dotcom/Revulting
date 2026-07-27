#!/bin/bash
# Gate 2: build the PORTED 5.0.0 core (all my Phase 3/4/5 changes) natively for
# Windows. Vulture windowport not wired in yet - this proves the core changes
# compile and link under mingw before adding the SDL layer.
set -o pipefail
P=/c/Users/dalla/Vulture/vulture5-win
STOCK=/c/Users/dalla/Vulture/win-build/nethack-5.0.0-stock

cd "$P" || exit 2
echo "=== stage the Windows mingw makefiles ==="
cp sys/windows/GNUmakefile* src/ && echo "copied"
sed -i 's/^NHV=370$/NHV=500/' src/GNUmakefile
grep -n "^NHV=" src/GNUmakefile | head -2

echo "=== supply Lua from the stock tree (this tree has no .git submodules) ==="
mkdir -p lib submodules
# NB: the shallow clone left EMPTY submodule placeholder dirs, so a "does the
# directory exist" test wrongly skips the copy. Test for a real header instead.
copy_sub () {  # $1 = submodule, $2 = marker file inside it
  if [ ! -f "submodules/$1/$2" ] && [ -f "$STOCK/submodules/$1/$2" ]; then
    rm -rf "submodules/$1"
    cp -r "$STOCK/submodules/$1" submodules/ && echo "$1 copied"
  else
    echo "$1: $( [ -f submodules/$1/$2 ] && echo present || echo UNAVAILABLE )"
  fi
}
copy_sub lua lua.h
copy_sub pdcursesmod curses.h
if [ -d "$STOCK/lib" ]; then cp -r "$STOCK/lib/." lib/ 2>/dev/null; fi
ls submodules/lua/lua.h >/dev/null 2>&1 && echo "lua.h present" || echo "WARNING: no lua.h"

cd src || exit 2
MK="make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y"
echo "=== clean ==="  ; $MK clean  > /tmp/p_clean.log 2>&1; echo "rc=$?"
echo "=== depend ===" ; $MK depend > /tmp/p_dep.log   2>&1; echo "rc=$?"
echo "=== build ==="  ; $MK nethack > /tmp/p_build.log 2>&1; echo "rc=$?"
echo "--- errors ---"
grep -inE "error:|\*\*\* |undefined reference" /tmp/p_build.log | grep -vi warning | head -25
echo "--- product ---"
ls -la "$P/binary/"*.exe 2>/dev/null
