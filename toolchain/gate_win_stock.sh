#!/bin/bash
# GATE: build the STOCK NetHack 5.0.0 core natively for Windows with mingw-w64,
# before touching the port. Proves the toolchain end to end.
# Run inside the MSYS2 MINGW64 shell.
set -o pipefail
ROOT=/c/Users/dalla/Vulture/win-build
STOCK=$ROOT/nethack-5.0.0-stock
mkdir -p "$ROOT"

echo "=== MSYSTEM=$MSYSTEM  gcc: $(command -v gcc) ==="
gcc --version | head -1

if [ ! -d "$STOCK/.git" ]; then
  echo "=== cloning pristine 5.0.0 (LF endings, with submodules) ==="
  rm -rf "$STOCK"
  git -c core.autocrlf=false clone --depth 1 --recurse-submodules \
      --branch NetHack-5.0.0_Released \
      https://github.com/NetHack/NetHack "$STOCK" 2>&1 | tail -3
fi
cd "$STOCK" || exit 2
echo "SHA: $(git rev-parse HEAD)"

echo "=== stage the Windows mingw makefile into src/ ==="
cp sys/windows/GNUmakefile* src/ && echo "copied GNUmakefile* -> src/"

echo "=== fix the shipped NHV bug (line ~1391: NHV=370 -> 500) ==="
grep -n "^NHV=" src/GNUmakefile | head -3
sed -i 's/^NHV=370$/NHV=500/' src/GNUmakefile
grep -n "^NHV=" src/GNUmakefile | head -3

cd src || exit 2
MK="make -f GNUmakefile GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=N SKIP_NETHACKW=Y"
echo "=== clean ==="; $MK clean   > /tmp/w_clean.log 2>&1; echo "rc=$?"
echo "=== depend ==="; $MK depend > /tmp/w_dep.log   2>&1; echo "rc=$?"; tail -3 /tmp/w_dep.log
echo "=== build console core ==="
$MK nethack > /tmp/w_build.log 2>&1; echo "rc=$?"
echo "--- errors ---"
grep -inE "error|\*\*\*|undefined reference" /tmp/w_build.log | grep -vi warning | head -20
echo "--- products ---"
ls -la "$STOCK/binary/"*.exe 2>/dev/null
ls -la "$STOCK/src/"*.exe 2>/dev/null | head -5
