#!/bin/bash
# Every 3.6.6 rename source must be a DIRECT "=" definition in ALL THREE ranges
# (monster/statue/figurine), because vulture_resolve_tile_id() follows only ONE
# redirect -- pointing at another "=>" key would silently render nothing.
C=/c/Users/dalla/Vulture/VultureNetHack/config/vulture_tiles.conf

SRC="PM_KOBOLD_LORD PM_DWARF_LORD PM_DWARF_KING PM_GNOME_LORD PM_GNOME_KING \
     PM_OGRE_LORD PM_OGRE_KING PM_VAMPIRE_LORD PM_ELF_LORD PM_ELVENKING"

bad=0
for n in $SRC; do
  row="  $(printf '%-20s' "$n")"
  for pre in monster statue figurine; do
    if grep -qE "^${pre}\.${n}[[:space:]]*=[^>]" "$C"; then
      row="$row $(printf '%-14s' "${pre}:direct")"
    elif grep -qE "^${pre}\.${n}[[:space:]]*=>" "$C"; then
      row="$row $(printf '%-14s' "${pre}:REDIRECT")"; bad=1
    else
      row="$row $(printf '%-14s' "${pre}:MISSING")"; bad=1
    fi
  done
  echo "$row"
done

echo
echo "=== 5.0.0 target names must not already exist ==="
grep -cE '^(monster|statue|figurine)\.(PM_KOBOLD_LEADER|PM_DWARF_LEADER|PM_DWARF_RULER|PM_GNOME_LEADER|PM_GNOME_RULER|PM_OGRE_LEADER|PM_OGRE_TYRANT|PM_VAMPIRE_LEADER|PM_ELF_NOBLE|PM_ELVEN_MONARCH)' "$C"

echo
[ $bad -eq 0 ] && echo "ALL SOURCES OK - safe to redirect" || echo "PROBLEM - do not apply"
