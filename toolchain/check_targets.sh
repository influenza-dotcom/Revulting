#!/bin/bash
C=/c/Users/dalla/Vulture/VultureNetHack/config/vulture_tiles.conf
T=/c/Users/dalla/Vulture/VultureNetHack/tiles

echo '=== every redirect target must be a DIRECT "=" definition (one hop only) ==='
for n in object.HELMET objicon.HELMET \
         object.SMALL_SHIELD objicon.SMALL_SHIELD \
         object.AMULET_OF_ESP objicon.AMULET_OF_ESP \
         object.SPE_DETECT_TREASURE objicon.SPE_DETECT_TREASURE \
         object.WAN_LIGHT objicon.WAN_LIGHT \
         object.POT_WATER objicon.POT_WATER ; do
  line=$(grep -nE "^${n}[[:space:]]*=[^>]" "$C" | head -1)
  if [ -n "$line" ]; then
    printf '  OK   %-30s %s\n' "$n" "$(echo "$line" | cut -c1-70)"
  else
    redir=$(grep -nE "^${n}[[:space:]]*=>" "$C" | head -1)
    if [ -n "$redir" ]; then
      printf '  BAD  %-30s IS ITSELF A REDIRECT: %s\n' "$n" "$redir"
    else
      printf '  BAD  %-30s NOT DEFINED AT ALL\n' "$n"
    fi
  fi
done

echo
echo '=== meatball art on disk (ENORMOUS_MEATBALL binds directly to the PNG) ==='
ls -la "$T"/object.huge_chunk_of_meat.png "$T"/objicon.huge_chunk_of_meat.png 2>&1 | awk '{print "  "$5, $9}'

echo
echo '=== the now-dead 3.6.6 keys the meatball art was orphaned under ==='
grep -nE '^(object|objicon)\.HUGE_CHUNK_OF_MEAT' "$C"

echo
echo '=== confirm the 5.0.0 names are not already present somewhere ==='
grep -cE '^(object|objicon)\.(HELM_OF_CAUTION|SHIELD_OF_DRAIN_RESISTANCE|SHIELD_OF_SHOCK_RESISTANCE|AMULET_OF_GUARDING|ENORMOUS_MEATBALL|SPE_CHAIN_LIGHTNING|WAN_STASIS|SPLASH_OF_)' "$C"
