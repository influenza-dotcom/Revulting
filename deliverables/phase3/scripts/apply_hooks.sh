#!/bin/bash
# Reapply the 15 Vulture hooks (diffs vs vanilla 3.6.6) onto the 5.0.0 port tree.
DIFFS=/mnt/c/Users/dalla/Vulture/deliverables/phase3_hooks
DST=/root/port/vulture5
FILES="src/cmd.c src/display.c src/do_name.c src/do_wear.c src/end.c src/files.c \
src/options.c src/pager.c src/version.c src/weapon.c src/windows.c \
include/config.h include/global.h include/hack.h include/tradstdc.h"
ok=0; fail=0; failed=""
for f in $FILES; do
  d="$DIFFS/$(echo "$f" | tr '/' '_').diff"
  sed 's/\r$//' "$d" > /tmp/h.diff
  # apply hooks (old=vanilla36, new=vulture) to the 5.0.0 file, fuzzy, ignore-whitespace
  if patch --no-backup-if-mismatch -s --fuzz=3 -l "$DST/$f" < /tmp/h.diff 2>/tmp/perr; then
    ok=$((ok+1)); echo "OK    $f"
  else
    fail=$((fail+1)); failed="$failed $f"; echo "REJECT $f : $(head -1 /tmp/perr)"
    # show how many hunks rejected
    grep -c "^" "$DST/${f##*/}.rej" 2>/dev/null | xargs -I{} echo "        (.rej lines: {})"
  fi
done
echo "=== applied=$ok rejected=$fail ==="
echo "rejects:$failed"
# collect any .rej files
find "$DST/src" "$DST/include" -name '*.rej' 2>/dev/null | while read r; do echo "REJ FILE: $r"; done
