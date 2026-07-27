cd /root/port/vulture5/win/vulture || exit 2
# --- clean mechanical fixes ---
sed -i 's/mons\[i\]\.mname/pmname(\&mons[i], NEUTRAL)/g' vulture_tileconfig.cpp
sed -i '/^boolean can_advance(int skill, int speedy);/d' winclass/enhancebutton.cpp
sed -i 's/hname ? hname : "NetHack"/"NetHack"/' vulture_gen.cpp
sed -i 's/monnum_to_glyph(u\.umonnum)/monnum_to_glyph(u.umonnum, Ugender)/' winclass/mapdata.cpp
sed -i 's/\bcontext\b/svc.context/g' winclass/inventory.cpp
sed -i -E 's/describe_level\(([A-Za-z_][A-Za-z0-9_]*)\)/describe_level(\1, 0)/' winclass/map.cpp
echo "applied clean fixes"
# --- gather info for the remaining (window_procs, mapglyph, SET_IN, what_trap) ---
cd /root/port/vulture5
echo "=== SET_IN_* / set_option_mod_status enum (5.0.0) ==="; grep -rhoE "\bset_[a-z_]+\b|SET_IN_[A-Z]+" include/optlist.h include/flag.h 2>/dev/null | sort -u | grep -iE "set_" | head
echo "=== glyph_info / glyph_map fields (for mapglyph adaptation) ==="; sed -n '/typedef struct glyph_map_entry/,/} glyph_info;/p' include/display.h 2>/dev/null | head -30
echo "=== what_trap site (mapdata:306) ==="; sed -n '304,308p' win/vulture/winclass/mapdata.cpp
echo "=== window_procs struct (5.0.0) - first fields + print_glyph ==="; sed -n '/struct window_procs {/,/win_print_glyph/p' include/winprocs.h 2>/dev/null | head -25
