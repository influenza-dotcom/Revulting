#!/bin/bash
cd /c/Users/dalla/Vulture/vulture5-win/src || exit 2
MK="make -f GNUmakefile CI_COMPILER=1 GIT=1 MSYSTEM=$MSYSTEM LUA_VERSION=5.4.8 DEBUGINFO=N USE_LUADLL=N SKIP_NETHACKW=Y"
echo "=== targets that build the data ==="
$MK dlb   > /tmp/dlb.log 2>&1;  echo "dlb rc=$?";  tail -5 /tmp/dlb.log
$MK dat   > /tmp/dat.log 2>&1;  echo "dat rc=$?";  tail -5 /tmp/dat.log
echo "=== nhdat / sysconf on disk ==="
find /c/Users/dalla/Vulture/vulture5-win -maxdepth 3 \( -name 'nhdat*' -o -name 'sysconf' \) 2>/dev/null | head -10
echo "=== what the binary dir has ==="
ls -la /c/Users/dalla/Vulture/vulture5-win/binary/ 2>/dev/null | head -15
