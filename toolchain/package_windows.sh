#!/bin/bash
# Package a self-contained, double-clickable Windows build:
#   NetHackV.exe + every non-system DLL + NetHack data + Vulture gamedata
set -o pipefail
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack
M=/c/msys64/mingw64/bin

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$P/binary/NetHackV.exe" "$OUT/" || exit 2

echo "=== NetHack data files ==="
for f in nhdat sysconf symbols license; do
  [ -f "$P/binary/$f" ] && cp "$P/binary/$f" "$OUT/" && echo "  $f"
done
# make sure the data library exists (dlb) - build it if the binary dir lacks it
if [ ! -f "$OUT/nhdat" ]; then
  echo "  (nhdat missing - building dat/dlb)"
  ( cd "$P/src" && make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM \
      LUA_VERSION=5.4.8 DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y dlb ) \
      > /tmp/dlb.log 2>&1
  [ -f "$P/binary/nhdat" ] && cp "$P/binary/nhdat" "$OUT/" && echo "  nhdat built"
fi
for f in sysconf symbols; do
  [ -f "$OUT/$f" ] || { [ -f "$P/dat/$f" ] && cp "$P/dat/$f" "$OUT/"; }
done
[ -f "$P/sys/windows/sysconf" ] && [ ! -f "$OUT/sysconf" ] && cp "$P/sys/windows/sysconf" "$OUT/"

echo "=== Vulture gamedata (art, config, fonts, sound - NOT in the DLB) ==="
cp -r "$P/win/vulture/gamedata/." "$OUT/" && echo "  gamedata copied"

echo "=== DLLs the exe needs (recursively resolved) ==="
need_dlls () {
  local target="$1"
  objdump -p "$target" 2>/dev/null | awk '/DLL Name:/{print $3}'
}
declare -A seen
queue=("$OUT/NetHackV.exe")
while [ ${#queue[@]} -gt 0 ]; do
  cur="${queue[0]}"; queue=("${queue[@]:1}")
  for d in $(need_dlls "$cur"); do
    [ -n "${seen[$d]}" ] && continue
    seen[$d]=1
    if [ -f "$M/$d" ]; then
      cp -n "$M/$d" "$OUT/" 2>/dev/null && echo "  + $d"
      queue+=("$OUT/$d")
    fi
  done
done

echo "=== result ==="
ls "$OUT" | wc -l | xargs echo "files:"
du -sh "$OUT" | cut -f1 | xargs echo "size:"
echo "--- exe deps still unresolved (should be system DLLs only) ---"
objdump -p "$OUT/NetHackV.exe" 2>/dev/null | awk '/DLL Name:/{print $3}' | while read d; do
  [ -f "$OUT/$d" ] || echo "  system: $d"
done
