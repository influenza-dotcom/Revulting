cd /root/port/vulture5/win/vulture || exit 2
sed -i 's/\bSET_IN_FILE\b/set_in_config/g' vulture_main.cpp
# what_trap signature in 5.0.0?
grep -rhInE "what_trap" /root/port/vulture5/include/extern.h | head -2
echo "--- rescan ---"
FLAGS="-g -I../../include -I. -Ibuild_nethack_3_6_0 -I/usr/include/SDL2 -D_REENTRANT -DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 -DVULTURE_GRAPHICS -DUSE_SDL_SYSCALLS -include vulture_nh5compat.h"
: > /tmp/e6.txt
for f in *.cpp winclass/*.cpp; do g++ $FLAGS -fsyntax-only "$f" 2>>/tmp/e6.txt; done
echo "=== total_errors=$(grep -c 'error:' /tmp/e6.txt) ==="
grep -E 'error:' /tmp/e6.txt | sed -E 's#/root/port/vulture5/win/vulture/##' | sort | uniq -c | sort -rn | head -25
