#!/bin/bash
# Overlay win/vulture into the 5.0.0 port tree (vulture5), then sync the 4 Windows-only
# bits (patha/, pathb/, modified atlas, GNUmakefile) that weren't in the in-WSL baseline.
set -e
DST=/root/port/vulture5
WSRC=/mnt/c/Users/dalla/Vulture/vulture-baseline/win/vulture

rm -rf "$DST/win/vulture"
cp -a /root/port/vulture-baseline/win/vulture "$DST/win/vulture"
echo "overlaid win/vulture ($(du -sh "$DST/win/vulture" | cut -f1))"

mkdir -p "$DST/win/vulture/patha" "$DST/win/vulture/pathb"
for d in patha pathb; do
  for f in "$WSRC/$d"/*; do
    [ -d "$f" ] && continue
    b=$(basename "$f")
    case "$b" in
      *.png) cp "$f" "$DST/win/vulture/$d/$b" ;;
      *) sed 's/\r$//' "$f" > "$DST/win/vulture/$d/$b" ;;
    esac
  done
done
cp "$WSRC/gamedata/map/ascii/nethack_3_6_0.png" "$DST/win/vulture/gamedata/map/ascii/nethack_3_6_0.png"
sed 's/\r$//' "$WSRC/GNUmakefile" > "$DST/win/vulture/GNUmakefile"

echo "synced patha/ pathb/ atlas GNUmakefile"
echo "atlas sha: $(sha256sum "$DST/win/vulture/gamedata/map/ascii/nethack_3_6_0.png" | cut -c1-16) (want 40f50838)"
echo "patha/remap_table.h: $(test -f "$DST/win/vulture/patha/remap_table.h" && echo present)"
echo "win/vulture cpp+h: $(ls "$DST/win/vulture"/*.cpp "$DST/win/vulture"/*.h "$DST/win/vulture"/winclass/*.cpp "$DST/win/vulture"/winclass/*.h 2>/dev/null | wc -l)"
echo "DONE"
