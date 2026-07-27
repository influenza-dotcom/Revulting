cd /root/port/vulture5/win/vulture || exit 2
FILES="$(ls *.cpp winclass/*.cpp)"
sed -i -E 's/\bsvl\.rooms\b/svr.rooms/g; s/\bsvl\.nroom\b/svn.nroom/g' $FILES
FLAGS="-g -I../../include -I. -Ibuild_nethack_3_6_0 -I/usr/include/SDL2 -D_REENTRANT -DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 -DVULTURE_GRAPHICS -DUSE_SDL_SYSCALLS -include vulture_nh5compat.h"
: > /tmp/allerr5.txt; tot=0
for f in *.cpp winclass/*.cpp; do
  g++ $FLAGS -fsyntax-only "$f" 2>>/tmp/allerr5.txt
done
tot=$(grep -c 'error:' /tmp/allerr5.txt)
echo "=== ROUND 3: total_errors=$tot ==="
echo "--- residual errors (file:line: msg) ---"
grep -E 'error:' /tmp/allerr5.txt | sed -E 's#/root/port/vulture5/win/vulture/##' | head -45
