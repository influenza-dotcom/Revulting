#!/bin/bash
# The Windows port expects a distribution layout with *.template files beside
# the exe; it copies them into the playground on first run.  Without them
# windmain.c calls error() and the game dies before the window opens.
P=/c/Users/dalla/Vulture/vulture5-win
OUT=/c/Users/dalla/Vulture/VultureNetHack

echo "=== locating source files ==="
find "$P" -maxdepth 3 \( -name 'nethackrc.template' -o -name 'symbols' \
     -o -name 'opthelp' -o -name 'Guidebook.txt' -o -name 'sysconf.template' \) \
     2>/dev/null | grep -v '/o/' | head -20

echo
echo "=== installing into the package ==="

# sysconf.template: our already-corrected sysconf is the canonical copy
cp "$OUT/sysconf" "$OUT/sysconf.template" && echo "  sysconf.template   <- corrected sysconf"

# symbols.template
if [ -f "$P/dat/symbols" ]; then
  cp "$P/dat/symbols" "$OUT/symbols"
  cp "$P/dat/symbols" "$OUT/symbols.template"
  echo "  symbols(.template) <- dat/symbols"
fi

# nethackrc.template -> becomes .nethackrc in the playground
for c in "$P/sys/windows/nethackrc.template" "$P/dat/nethackrc.template"; do
  if [ -f "$c" ]; then cp "$c" "$OUT/nethackrc.template"; echo "  nethackrc.template <- $c"; break; fi
done
if [ ! -f "$OUT/nethackrc.template" ]; then
  cat > "$OUT/nethackrc.template" <<'EOF'
# NetHack configuration file (Vulture / SDL2 build)
OPTIONS=windowtype:vulture
OPTIONS=color,hilite_pet,showexp,time
EOF
  echo "  nethackrc.template <- generated (windowtype:vulture)"
fi

# Guidebook + opthelp are update_file()'d unconditionally
for f in Guidebook.txt opthelp; do
  src=$(find "$P/doc" "$P/dat" -maxdepth 1 -name "$f" 2>/dev/null | head -1)
  if [ -n "$src" ]; then cp "$src" "$OUT/$f"; echo "  $f <- $src"
  else : > "$OUT/$f"; echo "  $f <- (empty stub, source not found)"; fi
done

echo
echo "=== package config files ==="
ls -la "$OUT"/*.template "$OUT/sysconf" "$OUT/symbols" "$OUT/opthelp" \
       "$OUT/Guidebook.txt" 2>/dev/null | awk '{printf "  %-10s %s\n", $5, $9}'
