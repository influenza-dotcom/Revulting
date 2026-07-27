#!/bin/bash
C=/c/Users/dalla/Vulture/VultureNetHack/config/vulture_tiles.conf
echo '=== the whole vampire family across all three ranges ==='
grep -nE '^(monster|statue|figurine)\.PM_VAMPIRE' "$C"
echo
echo '=== what the statue/figurine redirects resolve to (one more hop) ==='
for pre in statue figurine; do
  tgt=$(grep -E "^${pre}\.PM_VAMPIRE_LORD[[:space:]]*=>" "$C" | sed 's/.*=>[[:space:]]*//' | tr -d ' \r')
  echo "  ${pre}.PM_VAMPIRE_LORD => ${tgt}"
  if grep -qE "^${tgt}[[:space:]]*=[^>]" "$C"; then
    echo "      ${tgt} is a DIRECT definition -> use this as the target"
    grep -nE "^${tgt}[[:space:]]*=[^>]" "$C" | head -1 | sed 's/^/        /'
  else
    echo "      ${tgt} is NOT direct - dig further"
    grep -nE "^${tgt}[[:space:]]*=>" "$C" | head -1 | sed 's/^/        /'
  fi
done
