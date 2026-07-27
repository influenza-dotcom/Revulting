#!/bin/bash
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack

echo "=== nhdat (data library: levels, lua, help) ==="
if [ -f "$P/dat/nhdat" ]; then
  cp "$P/dat/nhdat" "$OUT/" && ls -la "$OUT/nhdat" | awk '{print "  copied", $5, "bytes"}'
else
  echo "  MISSING"
fi

echo "=== sysconf ==="
for c in "$P/sys/windows/sysconf" "$P/sys/unix/sysconf"; do
  [ -f "$c" ] && cp "$c" "$OUT/sysconf" && echo "  from $c" && break
done
# a portable folder build should keep everything beside the exe
if [ -f "$OUT/sysconf" ]; then
  sed -i 's/^WIZARDS=.*/WIZARDS=*/' "$OUT/sysconf" 2>/dev/null
fi

echo "=== scratch files NetHack expects to be able to write ==="
for f in perm record logfile xlogfile livelog; do : > "$OUT/$f"; done
mkdir -p "$OUT/save"
echo "  created perm/record/logfile/xlogfile/livelog/save"

echo "=== final package ==="
ls "$OUT" | wc -l | xargs echo "  entries:"
du -sh "$OUT" | cut -f1 | xargs echo "  size:"
for f in NetHackV.exe nhdat sysconf symbols SDL2.dll; do
  printf "  %-16s " "$f"; [ -e "$OUT/$f" ] && echo ok || echo MISSING
done
for d in config tiles fonts map; do
  printf "  %-16s " "$d/"; [ -d "$OUT/$d" ] && echo ok || echo MISSING
done
