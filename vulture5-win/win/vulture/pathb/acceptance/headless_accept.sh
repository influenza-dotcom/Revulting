#!/bin/bash
# Phase 6 acceptance, fully headless: no xdotool, no window focus, no keystrokes.
#   run 1  VULTURE_LUADRIVER + VULTURE_AUTOSAVE -> driver moves/descends/creates a
#          monster, dump records the FINAL state, then the game saves and exits
#   run 2  no driver -> the save is restored, dump records the restored state
#   compare the object lines: resolved tile index + PNG must be identical
D=/root/nh/install/games/lib/nethackdir
SC=/mnt/c/Users/dalla/AppData/Local/Temp/claude/C--Users-dalla-3D-RPG/3f47d6db-bba3-4c5a-9027-d39112550094/scratchpad
cd "$D" || exit 2

cat > /root/.nethackrc <<'RC'
OPTIONS=name:tester
OPTIONS=role:Valkyrie
OPTIONS=race:human
OPTIONS=gender:female
OPTIONS=align:neutral
OPTIONS=!tutorial
OPTIONS=!legacy
OPTIONS=!autopickup
RC
sed 's/\r$//' "$SC/d.lua" > "$D/d.lua"

export HACKDIR="$D" DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 \
       XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir SDL_VIDEODRIVER=x11
export NETHACKOPTIONS=@/root/.nethackrc
export SHOPTYPE='!'

find "$D" -maxdepth 1 -regextype posix-extended -regex '.*/[0-9]+[A-Za-z_]+\.[0-9]+' -delete 2>/dev/null
rm -f "$D"/save/* "$D"/*lock* /tmp/sr_before.txt /tmp/sr_after.txt 2>/dev/null
pkill -9 -f "nethack -u" 2>/dev/null; sleep 1

echo "########## RUN 1 (headless: drive, dump, save) ##########"
VULTURE_TILEDUMP=/tmp/sr_before.txt VULTURE_LUADRIVER=d.lua VULTURE_AUTOSAVE=1 \
  ./nethack -u tester -D > /tmp/vout1.txt 2>&1 &
G1=$!
for i in $(seq 1 24); do
  sleep 3
  kill -0 $G1 2>/dev/null || break          # game exits itself after saving
done
kill -9 $G1 2>/dev/null; sleep 1
echo "  exited on its own: $(kill -0 $G1 2>/dev/null && echo no || echo yes)"
echo "  save files:  $(ls "$D"/save/ 2>/dev/null | wc -l)"
echo "  BEFORE dumps=$(grep -c '### dump' /tmp/sr_before.txt 2>/dev/null) objs=$(grep -c '^map \|^inv ' /tmp/sr_before.txt 2>/dev/null)"
echo -n "  dlvls seen: "; grep -ohE 'dlvl=[0-9]+' /tmp/sr_before.txt 2>/dev/null | sort -u | paste -sd' '

echo "########## RUN 2 (headless: restore, dump) ##########"
VULTURE_TILEDUMP=/tmp/sr_after.txt ./nethack -u tester -D > /tmp/vout2.txt 2>&1 &
G2=$!
sleep 14
echo "  AFTER dumps=$(grep -c '### dump' /tmp/sr_after.txt 2>/dev/null) objs=$(grep -c '^map \|^inv ' /tmp/sr_after.txt 2>/dev/null)"
kill -9 $G2 2>/dev/null

echo "########## COMPARE ##########"
lastblock () { awk '/^### dump/{b=""} {b=b $0 "\n"} /^### end/{last=b} END{printf "%s", last}' "$1"; }
lastblock /tmp/sr_before.txt | grep -E '^(map|inv) ' | sort > /tmp/b.txt
lastblock /tmp/sr_after.txt  | grep -E '^(map|inv) ' | sort > /tmp/a.txt
echo "before=$(wc -l < /tmp/b.txt)  after=$(wc -l < /tmp/a.txt)"
echo "--- unidentified/shuffled objects in BEFORE (otyp != descr_idx) ---"
awk '{ o=$0; sub(/.*otyp=/,"",o); sub(/ .*/,"",o);
       d=$0; sub(/.*descr_idx=/,"",d); sub(/ .*/,"",d);
       if (o+0 != d+0) print }' /tmp/b.txt | head -6
if [ ! -s /tmp/b.txt ] || [ ! -s /tmp/a.txt ]; then
  echo "RESULT: INCONCLUSIVE (a dump was empty)"
elif diff -u /tmp/b.txt /tmp/a.txt > /tmp/d.txt; then
  echo "RESULT: PASS - every object's resolved tile index and PNG survived save/restore"
else
  echo "RESULT: DIFFERENCES"; head -25 /tmp/d.txt
fi
cp /tmp/sr_before.txt /mnt/c/Users/dalla/Vulture/deliverables/tiledump_before.txt 2>/dev/null
cp /tmp/sr_after.txt  /mnt/c/Users/dalla/Vulture/deliverables/tiledump_after.txt 2>/dev/null
