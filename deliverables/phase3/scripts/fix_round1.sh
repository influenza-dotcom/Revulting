#!/bin/bash
# Round 1 Phase 4/5 fixes: safe uppercase shim (force-included) + targeted lowercase-global
# seds at their sites (NOT force-included, to avoid corrupting SDL/system headers).
cd /root/port/vulture5/win/vulture || exit 2

# 1) minimal SAFE shim: only stdlib + uppercase renames (no lowercase common words)
cat > vulture_nh5compat.h <<'HDR'
/* Vulture -> NetHack 5.0.0 compat shim (SAFE subset only: stdlib + uppercase renames).
 * Lowercase relocated globals (moves/context/level/...) are edited at their sites, since
 * force-including their macros would corrupt SDL/system headers. */
#ifndef VULTURE_NH5COMPAT_H
#define VULTURE_NH5COMPAT_H
#include <stdlib.h>
#ifndef MENU_UNSELECTED
#define MENU_UNSELECTED FALSE
#endif
#define PM_PRIEST          PM_CLERIC
#define PM_ALIGNED_PRIEST  PM_ALIGNED_CLERIC
#define PM_HIGH_PRIEST     PM_HIGH_CLERIC
#endif
HDR

# 2) safe lowercase-global seds (these identifiers are only the NetHack global in vulture)
FILES="$(ls *.cpp winclass/*.cpp)"
sed -i -E 's/\bmoves\b/svm.moves/g;   s/\binvent\b/gi.invent/g; s/\bdungeons\b/svd.dungeons/g; \
           s/\brooms\b/svl.rooms/g;    s/\bnroom\b/svl.nroom/g;  s/\bplname\b/svp.plname/g;   \
           s/\btoplines\b/gt.toplines/g' $FILES

# 3) level -> svl.level in every file EXCEPT minimap (where 'level' is a member var)
for f in $FILES; do
  case "$f" in winclass/minimap.cpp) continue;; esac
  sed -i -E 's/\blevel\b/svl.level/g' "$f"
done

# 4) rescan with the safe shim force-included
FLAGS="-g -I../../include -I. -Ibuild_nethack_3_6_0 -I/usr/include/SDL2 -D_REENTRANT \
-DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 -DVULTURE_GRAPHICS -DUSE_SDL_SYSCALLS \
-include vulture_nh5compat.h"
: > /tmp/allerr3.txt; tot=0; errfiles=0
for f in *.cpp winclass/*.cpp; do
  g++ $FLAGS -fsyntax-only "$f" 2>/tmp/fe.txt
  n=$(grep -c 'error:' /tmp/fe.txt); tot=$((tot+n))
  if [ "$n" -gt 0 ]; then errfiles=$((errfiles+1)); printf "%4d  %s\n" "$n" "$f"; cat /tmp/fe.txt >> /tmp/allerr3.txt; fi
done
echo "=== ROUND 1: files_with_errors=$errfiles  total_errors=$tot ==="
grep 'error:' /tmp/allerr3.txt | sed -E "s/.*error: //; s/'[^']*'/'X'/g; s/[0-9]+/N/g" | sort | uniq -c | sort -rn | head -20
