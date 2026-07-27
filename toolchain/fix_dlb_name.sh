#!/bin/bash
# Windows builds define VERSION_IN_DLB_FILENAME, so NetHack opens
# "nhdat" + MAJOR MINOR PATCHLEVEL == nhdat500, not plain "nhdat".
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack

# stale 3.7 name in the makefile's own package list
sed -i 's/nhdat370/nhdat$(NHV)/' "$P/sys/windows/GNUmakefile"

echo "=== build the DLB with the Windows makefile ==="
cd "$P/src" || exit 2
cp "$P/sys/windows/GNUmakefile" "$P/src/GNUmakefile"
sed -i 's/^NHV=370$/NHV=500/' "$P/src/GNUmakefile"
make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 \
     DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y "$P/binary/nhdat500" \
     > /tmp/dlb500.log 2>&1
echo "  rc=$? (target may already be satisfied)"

echo "=== candidates on disk ==="
find "$P" -maxdepth 3 -name 'nhdat*' 2>/dev/null | while read f; do
  printf "  %-58s %s bytes\n" "$f" "$(stat -c%s "$f")"
done

echo "=== install as nhdat500 ==="
src=""
for c in "$P/binary/nhdat500" "$P/dat/nhdat" "$OUT/nhdat"; do
  [ -f "$c" ] && { src="$c"; break; }
done
if [ -z "$src" ]; then echo "  NO DLB FOUND"; exit 1; fi
cp "$src" "$OUT/nhdat500" && echo "  $src -> $OUT/nhdat500 ($(stat -c%s "$OUT/nhdat500") bytes)"
rm -f "$OUT/nhdat"          # wrong name; keeping it would just confuse
echo "  header: $(head -c 40 "$OUT/nhdat500" | tr -d '\0' | head -1)"
