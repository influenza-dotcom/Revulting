#!/bin/bash
# Clean GUI-save test: establish focus FIRST, then send ONLY 'S' and 'y'.
# The previous run interleaved an F12 screenshot (which pops its own
# "Screenshot saved" modal) between the two keys, so the Return used to dismiss
# that modal could have answered the save dialog's default ("no") instead.
D=/root/nh/install/games/lib/nethackdir
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
export HACKDIR="$D" DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 \
       XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir SDL_VIDEODRIVER=x11
export NETHACKOPTIONS=@/root/.nethackrc
unset VULTURE_LUADRIVER VULTURE_AUTOSAVE VULTURE_TILEDUMP

for attempt in 1 2 3 4; do
  echo "===== attempt $attempt ====="
  find "$D" -maxdepth 1 -regextype posix-extended -regex '.*/[0-9]+[A-Za-z_]+\.[0-9]+' -delete 2>/dev/null
  rm -f "$D"/save/* "$D"/*lock* "$D"/screenshot-*.png 2>/dev/null
  pkill -9 -f "nethack -u" 2>/dev/null; sleep 2
  ./nethack -u tester -D > /tmp/vout.txt 2>&1 &
  G=$!; sleep 9
  kill -0 $G 2>/dev/null || { echo "game died"; continue; }

  shots () { ls "$D"/screenshot-*.png 2>/dev/null | wc -l; }
  OK=0
  for t in 1 2 3 4 5 6; do
    W=$(xdotool search --pid $G 2>/dev/null | tail -1)
    [ -z "$W" ] && W=$(xdotool search --onlyvisible --class "." 2>/dev/null | tail -1)
    xdotool windowmap "$W" 2>/dev/null; xdotool windowraise "$W" 2>/dev/null
    xdotool windowactivate "$W" 2>/dev/null; xdotool windowfocus "$W" 2>/dev/null
    sleep 1
    n0=$(shots); xdotool key --clearmodifiers F12 2>/dev/null; sleep 2
    if [ "$(shots)" -gt "$n0" ]; then OK=1; echo "focus verified (try $t)"; break; fi
  done
  [ "$OK" = "1" ] || { echo "focus failed"; kill -9 $G; continue; }

  # clear the "Screenshot saved" modal, then let things settle
  xdotool key --clearmodifiers Return 2>/dev/null; sleep 3

  # ---- the clean experiment: S then y, nothing in between ----
  echo "sending S ..."; xdotool key --clearmodifiers S 2>/dev/null; sleep 3
  echo "sending y ..."; xdotool key --clearmodifiers y 2>/dev/null; sleep 4

  echo "  save files: $(ls "$D"/save/ 2>/dev/null | wc -l)"
  echo "  game alive: $(kill -0 $G 2>/dev/null && echo yes || echo NO-exited)"
  if [ "$(ls "$D"/save/ 2>/dev/null | wc -l)" -gt 0 ]; then
    echo "RESULT: GUI SAVE WORKS (my earlier 'broken' call was the F12 modal confounding it)"
    kill -9 $G 2>/dev/null; exit 0
  fi
  echo "RESULT: no save produced on this attempt"
  kill -9 $G 2>/dev/null
  exit 1
done
echo "INCONCLUSIVE - focus never established"
exit 3
