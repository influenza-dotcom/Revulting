cd /root/port/vulture5/win/vulture || exit 2
FILES="$(ls *.cpp winclass/*.cpp)"
sed -i -E 's/\bmoves\b/svm.moves/g' $FILES
sed -i -E 's/\binvent\b/gi.invent/g' $FILES
sed -i -E 's/\bdungeons\b/svd.dungeons/g' $FILES
sed -i -E 's/\brooms\b/svl.rooms/g' $FILES
sed -i -E 's/\bnroom\b/svl.nroom/g' $FILES
sed -i -E 's/\bplname\b/svp.plname/g' $FILES
sed -i -E 's/\btoplines\b/gt.toplines/g' $FILES
FLAGS="-g -I../../include -I. -Ibuild_nethack_3_6_0 -I/usr/include/SDL2 -D_REENTRANT -DVULTURE_NETHACK -DVULTURE_NETHACK_3_6_0 -DVULTURE_GRAPHICS -DUSE_SDL_SYSCALLS -include vulture_nh5compat.h"
: > /tmp/allerr4.txt; tot=0; errfiles=0
for f in *.cpp winclass/*.cpp; do
  g++ $FLAGS -fsyntax-only "$f" 2>/tmp/fe.txt
  n=$(grep -c 'error:' /tmp/fe.txt); tot=$((tot+n))
  if [ "$n" -gt 0 ]; then errfiles=$((errfiles+1)); printf "%4d  %s\n" "$n" "$f"; cat /tmp/fe.txt >> /tmp/allerr4.txt; fi
done
echo "=== ROUND 2: files_with_errors=$errfiles total_errors=$tot ==="
grep -E 'error:|warning:.*fpermissive' /tmp/allerr4.txt | sed -E "s/.*(error|warning): //; s/'[^']*'/'X'/g; s/[0-9]+/N/g" | sort | uniq -c | sort -rn | head -20
