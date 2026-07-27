#!/bin/bash
cd /c/Users/dalla/Vulture/vulture5-win || exit 2
echo '=== the loop that fills glassgems[] ==='
sed -n '680,700p' win/vulture/vulture_tileconfig.cpp
echo
echo '=== glass gem objects in 5.0.0 objects.c ==='
grep -n 'GLASS' src/objects.c | head -20
echo
echo '=== their colours ==='
grep -n 'glass' src/objects.c | head -20
