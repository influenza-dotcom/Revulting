#!/bin/bash
cd /c/Users/dalla/Vulture/vulture5-win || exit 2

echo '=== does 5.0.0 declare anything "force bolt"? ==='
grep -rn -i 'force bolt\|forcebolt\|FORCE_BOLT' src/objects.c include/objects.h include/obj.h 2>/dev/null | head -20

echo
echo '=== SCROLL entries in 5.0.0 objects.c (names only) ==='
grep -n 'SCROLL(' src/objects.c | head -40

echo
echo '=== SPELL entries mentioning force bolt ==='
grep -n 'SPELL(' src/objects.c | grep -i 'force' | head

echo
echo '=== what the port dumped as 5.0.0 runtime object names, scroll/spell range ==='
grep -iE 'SCR_|SPE_' /c/Users/dalla/Vulture/vulture5-win/win/vulture/pathb/nh500_names.tsv | head -60
