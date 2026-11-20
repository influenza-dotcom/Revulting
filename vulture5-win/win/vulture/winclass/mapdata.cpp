/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

#include "vulture_gen.h"
#include "vulture_main.h"
#include "vulture_mou.h"
#include "vulture_tile.h"
#include "vulture_win.h"

#include "contextmenu.h"
#include "levelwin.h"
#include "mapdata.h"
#include "window.h"

/* mapdata.h has already pulled in hack.h inside extern "C"; func_tab.h adds
 * extcmdlist[], used to look up the live #retravel binding (see
 * vulture_retravel_key() below). */
extern "C" {
#include "func_tab.h"
}

#include <functional>

using namespace std::placeholders;

extern "C" struct permonst *lookat(coordxy, coordxy, char *, char *);

/* 5.0.0 replaced mapglyph() with map_glyphinfo() filling a glyph_info struct.
 * This shim preserves the old (glyph -> char/color/flags) call shape used below. */
static void
vulture_mapglyph(int glyph, int *ochar, int *ocolor, unsigned *ospecial, int x, int y)
{
    glyph_info gi;
    map_glyphinfo((coordxy) x, (coordxy) y, glyph, 0, &gi);
    *ochar = gi.ttychar;
    *ocolor = (int) gi.gm.sym.color;
    *ospecial = gi.gm.glyphflags;
}

/* The engraving decal for (x,y), or V_TILE_NONE.  Mirrors the core's own
 * test in display.c: an engraving shows once the hero has revealed it
 * (erevealed is set on sight, by touch when blind, and when stepped on)
 * and nothing that hides traps - water, lava - lies over it. */
static int
vulture_engraving_tile(int x, int y)
{
    struct engr *ep = engr_at((coordxy) x, (coordxy) y);

    if (ep && ep->erevealed && !covers_traps(x, y))
        return V_MISC_ENGRAVING;
    return V_TILE_NONE;
}

/* The warning marker if this glyph is 5.0.0's trapped-door/trapped-chest
 * "^", else V_TILE_NONE.  These are dummy traps (TRAPPED_DOOR/TRAPPED_CHEST,
 * detect.c): sense_trap() paints the glyph but t_at() finds nothing, so the
 * glyph is the ONLY place the information exists.  Like tty, the mark lasts
 * until the core redraws the square (newsym() puts the door/box back). */
static int
vulture_trapped_marker(int glyph)
{
    if (glyph_is_trap(glyph)) {
        if (glyph_to_cmap(glyph) == S_trapped_door)
            return V_MISC_TRAPPED_DOOR;
        if (glyph_to_cmap(glyph) == S_trapped_chest)
            return V_MISC_TRAPPED_CHEST;
    }
    return V_TILE_NONE;
}

/* the only other cmaps beside furniture that have significant height and
 * therefore need to be drawn as part of the second pass with the furniture.
 */
#define V_EXTRA_FURNITURE(typ) \
    ((typ) == DOOR || (typ) == DBWALL || (typ) == IRONBARS || (typ) == TREE)

/*
 * Door arch orientation (IMPLEMENTATION_LOG §50).
 *
 * The core picks S_v?door / S_h?door from levl[][].horizontal, set by
 * set_door_orientation() (sp_lev.c): horizontal = (wall left or right) &&
 * !(wall above AND below).  That is a best guess for tty, where it only
 * picks '|' vs '-'; for a door in a CORNER of irregular walls (Mines,
 * themed rooms) it picks the axis of the short stub, and in this isometric
 * view the arch then stands sideways to the wall it is set in.
 *
 * So the art follows the walls actually around the door, in three steps:
 *  1. adjacent wall squares: more along map x (left/right) = horizontal
 *     wall = H art, more along map y = V art;
 *  2. tie (a corner: one each way) - the longer straight wall run wins,
 *     which is the wall the eye reads the door as sitting in;
 *  3. still tied - the side you can walk in from: a walkable square
 *     left/right means the door is passed along x, i.e. a vertical wall;
 *  then the core's choice.  Uses the real terrain, which reveals nothing
 *  the door art itself doesn't.
 */
static bool
vulture_door_wallish(int x, int y)
{
    int typ;

    if (!isok(x, y))
        return false;
    typ = svl.level.locations[x][y].typ;
    return IS_WALL(typ) || IS_DOOR(typ) || typ == SDOOR;
}

/* straight wall squares from the door in one direction, up to 4 */
static int
vulture_door_wallrun(int x, int y, int dx, int dy)
{
    int k;

    for (k = 1; k <= 4; k++)
        if (!vulture_door_wallish(x + k * dx, y + k * dy))
            break;
    return k - 1;
}

static bool
vulture_door_walkable(int x, int y)
{
    int typ;

    if (!isok(x, y))
        return false;
    typ = svl.level.locations[x][y].typ;
    return ACCESSIBLE(typ) && !IS_DOOR(typ);
}

static int
vulture_orient_door_tile(int tile, int x, int y)
{
    int wx = vulture_door_wallish(x - 1, y) + vulture_door_wallish(x + 1, y);
    int wy = vulture_door_wallish(x, y - 1) + vulture_door_wallish(x, y + 1);
    int rx = vulture_door_wallrun(x, y, -1, 0) + vulture_door_wallrun(x, y, 1, 0);
    int ry = vulture_door_wallrun(x, y, 0, -1) + vulture_door_wallrun(x, y, 0, 1);
    bool vertical;

    if (wy != wx) {
        vertical = (wy > wx);
    } else if (ry != rx) {
        vertical = (ry > rx);
    } else {
        bool walk_x = vulture_door_walkable(x - 1, y)
                      || vulture_door_walkable(x + 1, y);
        bool walk_y = vulture_door_walkable(x, y - 1)
                      || vulture_door_walkable(x, y + 1);

        if (walk_x == walk_y)
            return tile; /* no better information: trust the core */
        vertical = walk_x;
    }

    switch (tile) {
    case V_MISC_VDOOR_WOOD_OPEN:
    case V_MISC_HDOOR_WOOD_OPEN:
        return vertical ? V_MISC_VDOOR_WOOD_OPEN : V_MISC_HDOOR_WOOD_OPEN;
    case V_MISC_VDOOR_WOOD_CLOSED:
    case V_MISC_HDOOR_WOOD_CLOSED:
        return vertical ? V_MISC_VDOOR_WOOD_CLOSED : V_MISC_HDOOR_WOOD_CLOSED;
    default:
        return tile; /* broken door / doorway: no orientation */
    }
}

/* 3.6.x's CMD_TRAVEL (0x90, i.e. Cmd.spkeys[NHKF_TRAVEL]) does not exist in
 * 5.0.0 -- see vulture_retravel_key() below. */
#define META(c) (0x80 | (c))
#define CTRL(c) (0x1f & (c))

mapdata *map_data = NULL;

mapdata::mapdata()
{
    int i, j;

    for (i = 0; i < ROWNO; i++) {
        for (j = 0; j < COLNO; j++) {
            map_glyph[i][j] = NO_GLYPH;
            map_tileidx[i][j] = -1;
            map_bktileidx[i][j] = -1;
            map_back[i][j] = V_MISC_UNMAPPED_AREA;
            map_obj[i][j] = V_TILE_NONE;
            map_trap[i][j] = V_TILE_NONE;
            map_furniture[i][j] = V_TILE_NONE;
            map_specialeff[i][j] = V_TILE_NONE;
            map_mon[i][j] = V_TILE_NONE;
            map_pet[i][j] = 0;
            map_darkness[i][j] = 2;
        }
    }

    map_swallow = V_TILE_NONE;

    /* build engulf tile array */
    for (i = 0; i < NUMMONS; i++)
        vulture_tilemap_engulf[i] = V_TILE_NONE;

    vulture_tilemap_engulf[PM_OCHRE_JELLY] = V_MISC_ENGULF_OCHRE_JELLY;
    vulture_tilemap_engulf[PM_LURKER_ABOVE] = V_MISC_ENGULF_LURKER_ABOVE;
    vulture_tilemap_engulf[PM_TRAPPER] = V_MISC_ENGULF_TRAPPER;
    vulture_tilemap_engulf[PM_PURPLE_WORM] = V_MISC_ENGULF_PURPLE_WORM;
    vulture_tilemap_engulf[PM_DUST_VORTEX] = V_MISC_ENGULF_DUST_VORTEX;
    vulture_tilemap_engulf[PM_ICE_VORTEX] = V_MISC_ENGULF_ICE_VORTEX;
    vulture_tilemap_engulf[PM_ENERGY_VORTEX] = V_MISC_ENGULF_ENERGY_VORTEX;
    vulture_tilemap_engulf[PM_STEAM_VORTEX] = V_MISC_ENGULF_STEAM_VORTEX;
    vulture_tilemap_engulf[PM_FIRE_VORTEX] = V_MISC_ENGULF_FIRE_VORTEX;
    vulture_tilemap_engulf[PM_FOG_CLOUD] = V_MISC_ENGULF_FOG_CLOUD;
    vulture_tilemap_engulf[PM_AIR_ELEMENTAL] = V_MISC_ENGULF_AIR_ELEMENTAL;
    vulture_tilemap_engulf[PM_JUIBLEX] = V_MISC_ENGULF_JUIBLEX;

    /* Every cmap symbol we do not explicitly map below must render the
     * conspicuous placeholder.  These entries were previously left
     * UNINITIALISED, so any dungeon feature Vulture didn't know about indexed
     * the tile array with garbage (arbitrary art, or nothing).  NetHack 5.0.0
     * adds several such symbols - S_lavawall, S_poisoncloud, S_vibrating_square,
     * S_trapped_chest, S_trapped_door, the branch stair/ladder variants and the
     * engraving markers - so this must be a full sweep, not a few new entries.
     *
     * Symbols bound below get their real art.  What is deliberately LEFT at
     * V_MISC_ERROR (no 3.6.6-era art exists and guessing would be wrong):
     *   S_lavawall         - a wall of lava; drawing it as lava floor would
     *                        imply it is walkable
     *   S_vibrating_square - Sanctum marker, new in 5.0.0
     *   (S_trapped_door / S_trapped_chest were here until 2026-11-10; they
     *   now draw a warning marker - see vulture_trapped_marker())
     *   S_goodpos          - debug "valid position" overlay
     * The S_sw_* and S_expl_* runs are never looked up here; swallow and
     * explosion glyphs are routed through their own tile ranges above. */
    for (i = 0; i < MAXPCHARS; i++)
        vulture_tilemap_misc[i] = V_MISC_ERROR;

    /* build "special tile" array: these are the tiles for dungeon glyphs */
#ifdef VULTURE_SLASHEM
    vulture_tilemap_misc[S_toilet] = V_MISC_TOILET;
#endif
#if defined(VULTURE_UNNETHACK) || defined(VULTURE_NETHACK_3_6_0)
    vulture_tilemap_misc[S_darkroom] = V_TILE_FLOOR_COBBLESTONE;
#endif

    vulture_tilemap_misc[S_stone] = V_MISC_UNMAPPED_AREA;
    vulture_tilemap_misc[S_vwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_hwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_tlcorn] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_trcorn] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_blcorn] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_brcorn] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_crwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_tuwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_tdwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_tlwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_trwall] = V_TILE_WALL_GENERIC;
    vulture_tilemap_misc[S_ndoor] = V_MISC_DOOR_WOOD_BROKEN;
    vulture_tilemap_misc[S_vodoor] = V_MISC_VDOOR_WOOD_OPEN;
    vulture_tilemap_misc[S_hodoor] = V_MISC_HDOOR_WOOD_OPEN;
    vulture_tilemap_misc[S_vcdoor] = V_MISC_VDOOR_WOOD_CLOSED;
    vulture_tilemap_misc[S_hcdoor] = V_MISC_HDOOR_WOOD_CLOSED;
    vulture_tilemap_misc[S_room] = V_TILE_FLOOR_COBBLESTONE;
    vulture_tilemap_misc[S_corr] = V_TILE_FLOOR_ROUGH;
    vulture_tilemap_misc[S_upstair] = V_MISC_STAIRS_UP;
    vulture_tilemap_misc[S_dnstair] = V_MISC_STAIRS_DOWN;

    /* 5.0.0 splits stairs that cross a dungeon branch into their own cmap
     * symbols (tty draws them in yellow rather than gray).  They are the same
     * physical feature and Vulture has one set of stair/ladder art, so bind
     * them to it - dlvl 1's up stair leaves the dungeon and is a BRANCH stair,
     * which is why the hero's starting square rendered as a placeholder. */
    vulture_tilemap_misc[S_brupstair] = V_MISC_STAIRS_UP;
    vulture_tilemap_misc[S_brdnstair] = V_MISC_STAIRS_DOWN;
    vulture_tilemap_misc[S_brupladder] = V_MISC_LADDER_UP;
    vulture_tilemap_misc[S_brdnladder] = V_MISC_LADDER_DOWN;

    /* 5.0.0 gives engraved squares their own cmap symbols.  These two entries
     * are the FLOOR under the engraving (map_back); the engraving itself is a
     * decal, V_MISC_ENGRAVING, put on the trap layer by
     * vulture_engraving_tile() below - a trap hides an engraving in the core
     * too (traps outrank engravings in display.c). */
    vulture_tilemap_misc[S_engroom] = V_TILE_FLOOR_COBBLESTONE;
    vulture_tilemap_misc[S_engrcorr] = V_TILE_FLOOR_ROUGH;

    /* bound so nothing that indexes the table by cmap can hit ERROR; the
     * map itself routes these through vulture_trapped_marker() */
    vulture_tilemap_misc[S_trapped_door] = V_MISC_TRAPPED_DOOR;
    vulture_tilemap_misc[S_trapped_chest] = V_MISC_TRAPPED_CHEST;

    /* a poison cloud is a cloud; 3.6.6 had only S_cloud */
    vulture_tilemap_misc[S_poisoncloud] = V_MISC_CLOUD;
    vulture_tilemap_misc[S_fountain] = V_MISC_FOUNTAIN;
    vulture_tilemap_misc[S_altar] = V_MISC_ALTAR;
    vulture_tilemap_misc[S_teleportation_trap] = V_MISC_TRAP_TELEPORTER;
    vulture_tilemap_misc[S_tree] = V_MISC_TREE;
    vulture_tilemap_misc[S_cloud] = V_MISC_CLOUD;
    vulture_tilemap_misc[S_air] = V_TILE_FLOOR_AIR;
    vulture_tilemap_misc[S_grave] = V_MISC_GRAVE;
    vulture_tilemap_misc[S_sink] = V_MISC_SINK;
    vulture_tilemap_misc[S_bear_trap] = V_MISC_TRAP_BEAR;
    vulture_tilemap_misc[S_rust_trap] = V_MISC_TRAP_WATER;
    vulture_tilemap_misc[S_pit] = V_MISC_TRAP_PIT;
    vulture_tilemap_misc[S_hole] = V_MISC_TRAP_PIT;
    vulture_tilemap_misc[S_trap_door] = V_MISC_TRAP_DOOR;
    vulture_tilemap_misc[S_water] = V_TILE_FLOOR_WATER;
    vulture_tilemap_misc[S_pool] = V_TILE_FLOOR_WATER;
    vulture_tilemap_misc[S_ice] = V_TILE_FLOOR_ICE;
    vulture_tilemap_misc[S_lava] = V_TILE_FLOOR_LAVA;
    vulture_tilemap_misc[S_throne] = V_MISC_THRONE;
    vulture_tilemap_misc[S_bars] = V_MISC_BARS;
    vulture_tilemap_misc[S_upladder] = V_MISC_LADDER_UP;
    vulture_tilemap_misc[S_dnladder] = V_MISC_LADDER_DOWN;
    vulture_tilemap_misc[S_arrow_trap] = V_MISC_TRAP_ARROW;
    vulture_tilemap_misc[S_rolling_boulder_trap] =
        V_MISC_ROLLING_BOULDER_TRAP;
    vulture_tilemap_misc[S_sleeping_gas_trap] = V_MISC_GAS_TRAP;
    vulture_tilemap_misc[S_fire_trap] = V_MISC_TRAP_FIRE;
    vulture_tilemap_misc[S_web] = V_MISC_WEB_TRAP;
    vulture_tilemap_misc[S_statue_trap] = OBJECT_TO_VTILE(STATUE);
    vulture_tilemap_misc[S_anti_magic_trap] = V_MISC_TRAP_ANTI_MAGIC;
    vulture_tilemap_misc[S_polymorph_trap] = V_MISC_TRAP_POLYMORPH;
    vulture_tilemap_misc[S_vbeam] = V_MISC_ZAP_VERTICAL;
    vulture_tilemap_misc[S_hbeam] = V_MISC_ZAP_HORIZONTAL;
    vulture_tilemap_misc[S_lslant] = V_MISC_ZAP_SLANT_LEFT;
    vulture_tilemap_misc[S_rslant] = V_MISC_ZAP_SLANT_RIGHT;
    vulture_tilemap_misc[S_litcorr] = V_TILE_FLOOR_ROUGH_LIT;
    vulture_tilemap_misc[S_ss1] = V_MISC_RESIST_SPELL_1;
    vulture_tilemap_misc[S_ss2] = V_MISC_RESIST_SPELL_2;
    vulture_tilemap_misc[S_ss3] = V_MISC_RESIST_SPELL_3;
    vulture_tilemap_misc[S_ss4] = V_MISC_RESIST_SPELL_4;
    vulture_tilemap_misc[S_dart_trap] = V_MISC_DART_TRAP;
    vulture_tilemap_misc[S_falling_rock_trap] = V_MISC_FALLING_ROCK_TRAP;
    vulture_tilemap_misc[S_squeaky_board] = V_MISC_SQUEAKY_BOARD;
    vulture_tilemap_misc[S_land_mine] = V_MISC_LAND_MINE;
    vulture_tilemap_misc[S_magic_portal] = V_MISC_MAGIC_PORTAL;
    vulture_tilemap_misc[S_spiked_pit] = V_MISC_SPIKED_PIT;
    vulture_tilemap_misc[S_hole] = V_MISC_HOLE;
    vulture_tilemap_misc[S_level_teleporter] = V_MISC_LEVEL_TELEPORTER;
    vulture_tilemap_misc[S_magic_trap] = V_MISC_MAGIC_TRAP;
    vulture_tilemap_misc[S_digbeam] = V_MISC_DIGBEAM;
    vulture_tilemap_misc[S_flashbeam] = V_MISC_FLASHBEAM;
    vulture_tilemap_misc[S_boomleft] = V_MISC_BOOMLEFT;
    vulture_tilemap_misc[S_boomright] = V_MISC_BOOMRIGHT;
    vulture_tilemap_misc[S_hcdbridge] = V_MISC_HCDBRIDGE;
    vulture_tilemap_misc[S_vcdbridge] = V_MISC_VCDBRIDGE;
    vulture_tilemap_misc[S_hodbridge] = V_MISC_HODBRIDGE;
    vulture_tilemap_misc[S_vodbridge] = V_MISC_VODBRIDGE;
}

void
mapdata::clear()
{
    int i, j;

    for (i = 0; i < ROWNO; i++)
        for (j = 0; j < COLNO; j++) {
            map_darkness[i][j] = 2;
            /* ideally this is what we'd do to clear background:
             * map_back[i][j] = V_MISC_UNMAPPED_AREA;
             * unfortunately doing so breaks dark tiles in rooms... */
            /* 5.0.0: print_glyph now takes glyph_info*; this is an internal
             * map reset, so drive set_glyph directly with the glyph number. */
            set_glyph(j, i, cmap_to_glyph(S_stone));
            set_tileidx(j, i, (short) -1, (short) -1);
            map_trap[i][j] = V_TILE_NONE;
            map_furniture[i][j] = V_TILE_NONE;
            map_obj[i][j] = V_TILE_NONE;
            map_mon[i][j] = V_TILE_NONE;
            map_specialeff[i][j] = V_TILE_NONE;
            map_pet[i][j] = 0;
            map_glyph[i][j] = cmap_to_glyph(S_stone);
        }

    /* notify observers */
    for (std::vector<mapviewer *>::iterator i = views.begin();
         i != views.end(); ++i)
        (*i)->map_clear();
}

void
mapdata::set_tileidx(int x, int y, short tileidx, short bktileidx)
{
    if (x < 0 || x >= COLNO || y < 0 || y >= ROWNO)
        return;
    map_tileidx[y][x] = tileidx;
    map_bktileidx[y][x] = bktileidx;
}

void
mapdata::set_glyph(int x, int y, int glyph)
{
    struct obj *obj;
    struct trap *trap;
    int character, colour;
    int memglyph;

    /* if we're blind, we also need to print what we remember under us */
    if (iflags.window_inited == TRUE && !cansee(x, y) && x == u.ux
        && y == u.uy) {
#ifdef DISPLAY_LAYERS
        memglyph = memory_glyph(x, y);
#else
        memglyph = svl.level.locations[x][y].glyph;
#endif
        if (glyph != memglyph)
            set_glyph(x, y, memglyph);
    }

    int map_mon = this->map_mon[y][x];
    int map_obj = this->map_obj[y][x];
    int map_trap = this->map_trap[y][x];
    int map_back = this->map_back[y][x];
    int map_special = V_TILE_NONE;
    int map_furniture = this->map_furniture[y][x];
    int darkness = this->map_darkness[y][x];
    int prev_darkness = darkness;
    int is_pet = 0;
    unsigned int attr;

    set_map_data(MAP_GLYPH, x, y, glyph, false);

    /* check wether we are swallowed, if so, return, because nothing but the
     * swallow graphic will be drawn anyway */
    if (glyph_is_swallow(glyph)) {
        /* we only SET vulture_map_swallow here; we expect vulture_draw_level
         * to reset it */
        map_swallow =
            (vulture_tilemap_engulf[(glyph - GLYPH_SWALLOW_OFF) >> 3]);
        return;
    }

    /* Nethack will only show us one glyph per position. We need up to 4
     * "things" per mapsquare: monsters, objects, traps & furniture, floor &
     * walls. Therefore we rely on the glyph only for monsters (which are
     * never covered) and magical vision where we can't just display eg the
     * object a telepathically seen monster is standing on,
     * because that would be cheating. */
    if (iflags.window_inited == TRUE && cansee(x, y)) {
        /* various special effects, these occur only during the player's turn
         * and should be layered on top of everything else */
        /* These are the core's glyph blocks, and 5.0.0 REORDERED them:
         *   3.6.6:  EXPLODE < ZAP < SWALLOW < WARNING
         *   5.0.0:  ZAP < CMAP_C < SWALLOW < EXPLODE < WARNING
         * The hand-rolled ranges here were written against the 3.6.6 order,
         * so under 5.0.0 "glyph >= GLYPH_EXPLODE_OFF && glyph < GLYPH_ZAP_OFF"
         * became unsatisfiable - every explosion tile was dead code - while
         * the zap arm's "< GLYPH_WARNING_OFF" over-captured the CMAP_C,
         * SWALLOW and EXPLODE blocks and painted all of them as beam art.
         * Use the core's own classifiers so this cannot drift again. */
        if (glyph_is_cmap_zap(glyph))
            map_special =
                vulture_tilemap_misc[S_vbeam
                                     + ((glyph - GLYPH_ZAP_OFF) & 0x03)];
        else if (glyph_is_explosion(glyph))
            /* EXPTILECOUNT is EXPL_MAX * MAXEXPCHARS + 1, and the core lays
             * the block out in the same type-major order, so the difference
             * indexes the explosion tiles directly and cannot run past the
             * reserved last (ERROR) slot. */
            map_special = EXPTILEOFFSET + (glyph - GLYPH_EXPLODE_OFF);
        else if (glyph_is_cmap_c(glyph)
                 && glyph_to_cmap(glyph) >= S_digbeam
                 && glyph_to_cmap(glyph) <= S_ss4)
            /* digbeam, camera flash, boomerang, magic resistance: these are
             * not floor tiles ... */
            map_special = vulture_tilemap_misc[glyph_to_cmap(glyph)];

        /* if the player is invisible and can't see himself, nethack does not
         * print a glyph for him; we need to insert it ourselves */
        if (x == u.ux && y == u.uy && !canseeself()) {
            set_map_data(MAP_GLYPH, x, y, monnum_to_glyph(u.umonnum, Ugender), false);
            is_pet = 0;
            map_mon = V_MISC_PLAYER_INVIS;
        }
        /* We rely on the glyph for monsters, as they are never covered by
         * anything
         * at the start of the turn and dealing with them manually is ugly */
        else if (glyph_is_monster(glyph)) {
            /* we need special attributes, so that we can highlight the pet */
            vulture_mapglyph(glyph, &character, &colour, &attr, x, y);
            is_pet = attr & MG_PET;
            map_mon = vulture_monster_to_tile(glyph_to_mon(glyph), x, y);
        }
        /* handle invisible monsters */
        else if (glyph_is_invisible(glyph)) {
            vulture_mapglyph(glyph, &character, &colour, &attr, x, y);
            is_pet = attr & MG_PET;
            map_mon = V_MISC_INVISIBLE_MONSTER;
        }
        /* handle monsters you are warned of */
        else if (glyph_is_warning(glyph)) {
            map_mon = V_MISC_WARNLEV_1 + glyph_to_warning(glyph);
            vulture_mapglyph(glyph, &character, &colour, &attr, x, y);
            is_pet = attr & MG_PET;
        }
        /* however they may be temporarily obscured by magic effects... */
        else if (map_special == V_TILE_NONE)
            map_mon = V_TILE_NONE;

        /* visible objects that are not covered by lava */
        if ((obj = vobj_at(x, y)) && !covers_objects(x, y)) {
            /* glyph_to_obj(obj_to_glyph(foo)) looks like nonsense, but is
             * actually an elegant way of handling hallucination, especially
             * the complicated matter of hallucinated corpses... */
            map_obj = vulture_object_to_tile(
                glyph_to_obj(obj_to_glyph(obj, rn2_on_display_rng)), x, y,
                NULL);
        }
        /* just to make things interesting, the above does not handle
           thrown/kicked objects... */
        else if (glyph_is_object(glyph))
            map_obj = vulture_object_to_tile(glyph_to_obj(glyph), x, y, NULL);
        else
            map_obj = V_TILE_NONE;

        /* traps that are not covered by lava and have been seen */
        if ((trap = t_at(x, y)) && !covers_traps(x, y) && trap->tseen)
            /* 5.0.0: what_trap() is gone; trap_to_defsym() maps ttyp -> cmap sym.
             * (hallucination randomization dropped; the real trap tile is shown) */
            map_trap =
                vulture_tilemap_misc[trap_to_defsym(trap->ttyp)];
        else
            /* no visible trap: an engraving takes the same flat layer */
            map_trap = vulture_engraving_tile(x, y);

        /* a trapped door/box just found by searching: the glyph is the only
         * record.  The door (furniture pass) and box (vobj_at above) still
         * draw normally; the marker goes over them. */
        if (map_special == V_TILE_NONE)
            map_special = vulture_trapped_marker(glyph);

        /* handle furniture: altars, stairs,...
         * furniture is separated out from walls and floors, so that it can be
         * used on any type of floor, rather than producing a discolored
         * blob*/
        if (glyph_to_cmap(glyph) >= S_upstair
            && glyph_to_cmap(glyph) <= S_fountain) {
            /* this is a mimic pretending to be part of the dungeon */
            map_furniture = vulture_tilemap_misc[glyph_to_cmap(glyph)];
            map_back = V_TILE_FLOOR_COBBLESTONE;
        } else if (IS_FURNITURE(svl.level.locations[x][y].typ)) {
            map_furniture =
                vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))];
            /* furniture is only found in rooms, so set the background type */
            map_back = V_TILE_FLOOR_COBBLESTONE;
        } else if (glyph_to_cmap(glyph) >= S_ndoor
                   && glyph_to_cmap(glyph) <= S_tree) {
            /* this is a mimic pretending to be part of the dungeon */
            map_furniture = vulture_orient_door_tile(
                vulture_tilemap_misc[glyph_to_cmap(glyph)], x, y); /* §50 */
            map_back = V_TILE_FLOOR_ROUGH;
        } else if (V_EXTRA_FURNITURE(svl.level.locations[x][y].typ)) {
            /* this stuff is not furniture, but we need to draw it at the same
             * time, so we pack it in with the furniture ...*/
            map_furniture = vulture_orient_door_tile(
                vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))],
                x, y); /* §50: arch follows the walls around the door */
            map_back = V_TILE_FLOOR_ROUGH;
        } else {
            map_furniture = V_TILE_NONE;
            map_back =
                vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))];
            /* Gas-cloud regions.  3.6.6 drew every region as S_cloud; 5.0.0
             * draws the damaging (stinking/poison) kind as S_poisoncloud and
             * only harmless vapour as S_cloud (create_gas_cloud() in
             * region.c).  Checking S_cloud alone meant a stinking cloud never
             * got this overlay and just looked like unlit floor. */
            if (map_special == V_TILE_NONE
                && (glyph_to_cmap(glyph) == S_cloud
                    || glyph_to_cmap(glyph) == S_poisoncloud)
                && back_to_glyph(x, y) != glyph)
                map_special = V_MISC_STINKING_CLOUD;
        }

        /* physically seen tiles cannot be dark */
        darkness = 0;
    }

    /* location seen via some form of magical vision OR
     * when a save is restored*/
    else {
        /* if we can see a monster here, it will be re-shown explicitly */
        map_mon = V_TILE_NONE;

        /* need to clear special effect explicitly here:
         * if we just got blinded by lightnig, the beam will remain onscreen
         * otherwise */
        map_special = V_TILE_NONE;

        /* monsters */
        if (glyph_is_monster(glyph)) {
            vulture_mapglyph(glyph, &character, &colour, &attr, x, y);
            is_pet = attr & MG_PET;
            map_mon = vulture_monster_to_tile(glyph_to_mon(glyph), x, y);

            /* if seen telepathically in an unexplored area, it might not have
             * a floor */
            if (map_back == V_MISC_UNMAPPED_AREA
                && svl.level.locations[x][y].typ != STONE)
                map_back = V_MISC_FLOOR_NOT_VISIBLE;
        }

        /* handle invisible monsters */
        else if (glyph_is_invisible(glyph)) {
            vulture_mapglyph(glyph, &character, &colour, &attr, x, y);
            is_pet = attr & MG_PET;
            map_mon = V_MISC_INVISIBLE_MONSTER;
        }

        /* handle monsters you are warned of */
        else if (glyph_is_warning(glyph)) {
            map_mon = V_MISC_WARNLEV_1 + glyph_to_warning(glyph);
            if (map_back == V_MISC_UNMAPPED_AREA
                && svl.level.locations[x][y].typ != STONE)
                map_back = V_MISC_FLOOR_NOT_VISIBLE;
        }

        /* same as above, for objects */
        else if (glyph_is_object(glyph)) {
            map_obj = vulture_object_to_tile(glyph_to_obj(glyph), x, y, NULL);
            if (map_back == V_MISC_UNMAPPED_AREA
                && svl.level.locations[x][y].typ != STONE)
                map_back = V_MISC_FLOOR_NOT_VISIBLE;
        }

        /* traps are not seen magically (I think?), so this only triggers when
           loading a level */
        else if (glyph_is_trap(glyph)) {
            if ((map_special = vulture_trapped_marker(glyph)) != V_TILE_NONE) {
                /* trap detection's trapped door/box: not a floor trap.  Keep
                 * whatever box is remembered here (map_obj is untouched),
                 * redraw a door we have actually seen, and mark it. */
                map_trap = V_TILE_NONE;
                if (V_EXTRA_FURNITURE(svl.level.locations[x][y].typ)
                    && svl.level.locations[x][y].seenv) {
                    map_furniture = vulture_tilemap_misc[glyph_to_cmap(
                        back_to_glyph(x, y))];
                    map_back = V_TILE_FLOOR_ROUGH;
                }
            } else
                map_trap = vulture_tilemap_misc[glyph_to_cmap(glyph)];
            if (map_back == V_MISC_UNMAPPED_AREA)
                map_back = V_MISC_FLOOR_NOT_VISIBLE;
        }

        else if (glyph_is_cmap(glyph)) {
            /* Nethack shows us the cmaps, therefore there are no traps or
             * objects here */
            map_obj = V_TILE_NONE;
            /* ...but a remembered engraving IS a cmap in 5.0.0: keep its
             * decal, from the memory glyph rather than engr_at(), so it
             * matches what the core remembers */
            map_trap = (glyph_to_cmap(glyph) == S_engroom
                        || glyph_to_cmap(glyph) == S_engrcorr)
                           ? (int) V_MISC_ENGRAVING
                           : (int) V_TILE_NONE;

            /* IS_FURNITURE() may be true while the cmap is S_stone for dark
             * rooms  */
            if (IS_FURNITURE(svl.level.locations[x][y].typ)
                && glyph_to_cmap(glyph) != S_stone) {
                map_furniture = vulture_tilemap_misc[glyph_to_cmap(glyph)];
                map_back = V_TILE_FLOOR_COBBLESTONE;
            } else if (V_EXTRA_FURNITURE(svl.level.locations[x][y].typ)
                       && glyph_to_cmap(glyph) != S_stone) {
                /* V_EXTRA_FURNITURE = doors, drawbridges, iron bars
                 * that stuff is not furniture, but we need to draw it at the
                 * same time, so we pack it in with the furniture ...*/
                map_furniture =
                    vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))];
                map_back = V_TILE_FLOOR_ROUGH;
            } else {
                map_furniture = V_TILE_NONE;
                map_back = vulture_tilemap_misc[glyph_to_cmap(glyph)];
            }
        }

        /* When a save is restored, we are shown a number of glyphs for
         * objects, traps, etc whose background we actually know and can
         * display, even though we can't physically see it*/
        if (svl.level.locations[x][y].seenv != 0
            && map_back == V_MISC_FLOOR_NOT_VISIBLE) {
            if (IS_FURNITURE(svl.level.locations[x][y].typ)) {
                map_furniture =
                    vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))];
                /* furniture is only found in rooms, so set the background
                 * type */
                map_back = V_TILE_FLOOR_COBBLESTONE;
            } else if (V_EXTRA_FURNITURE(svl.level.locations[x][y].typ)) {
                /* this stuff is not furniture, but we need to draw it at the
                 * same time, so we pack it in with the furniture ...*/
                map_furniture =
                    vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))];
                map_back = V_TILE_FLOOR_ROUGH;
            } else {
                map_furniture = V_TILE_NONE;
                map_back =
                    vulture_tilemap_misc[glyph_to_cmap(back_to_glyph(x, y))];
            }
        }

        /* handle unlit room tiles; until now we were assuming them to be lit;
         * whereas tiles converted via vulture_tilemap_misc are currently
         * V_TILE_NONE */
        if (((!svl.level.locations[x][y].waslit)
             && map_back == V_TILE_FLOOR_COBBLESTONE)
            || (svl.level.locations[x][y].typ == ROOM
                && map_back == V_MISC_UNMAPPED_AREA
                && svl.level.locations[x][y].seenv)) {
            map_back = V_TILE_FLOOR_COBBLESTONE;
            darkness = 2;
        } else
            darkness = 1;
    }

    /* if the lightlevel changed, we need to force the tile to be updated */
    int force_update = (prev_darkness != darkness);

    /* fix drawbriges, so they don't start in the middle of the moat when
     * they're open */
    /* horizontal drawbridges */
    if (svl.level.locations[x][y].typ == 22
        && svl.level.locations[x - 1][y].typ == DRAWBRIDGE_DOWN
        && svl.level.locations[x - 1][y].seenv) {
        map_furniture = V_MISC_HODBRIDGE;
        switch (svl.level.locations[x][y].drawbridgemask & DB_UNDER) {
        case DB_MOAT:
            set_map_data(MAP_BACK, x - 1, y, vulture_tilemap_misc[S_pool],
                         force_update);
            break;

        case DB_LAVA:
            set_map_data(MAP_BACK, x - 1, y, vulture_tilemap_misc[S_lava],
                         force_update);
            break;

        case DB_ICE:
            set_map_data(MAP_BACK, x - 1, y, vulture_tilemap_misc[S_ice],
                         force_update);
            break;

        case DB_FLOOR:
            set_map_data(MAP_BACK, x - 1, y, vulture_tilemap_misc[S_room],
                         force_update);
            break;
        }
    }

    /* vertical drawbridges */
    if (svl.level.locations[x][y].typ == 22
        && svl.level.locations[x][y - 1].typ == DRAWBRIDGE_DOWN
        && svl.level.locations[x][y - 1].seenv) {
        map_furniture = V_MISC_VODBRIDGE;
        switch (svl.level.locations[x][y].drawbridgemask & DB_UNDER) {
        case DB_MOAT:
            set_map_data(MAP_BACK, x, y - 1, vulture_tilemap_misc[S_pool],
                         force_update);
            break;

        case DB_LAVA:
            set_map_data(MAP_BACK, x, y - 1, vulture_tilemap_misc[S_lava],
                         force_update);
            break;

        case DB_ICE:
            set_map_data(MAP_BACK, x, y - 1, vulture_tilemap_misc[S_ice],
                         force_update);
            break;

        case DB_FLOOR:
            set_map_data(MAP_BACK, x, y - 1, vulture_tilemap_misc[S_room],
                         force_update);
            break;
        }
    }

    /* write new map data */
    set_map_data(MAP_MON, x, y, map_mon, force_update);
    set_map_data(MAP_OBJ, x, y, map_obj, force_update);
    set_map_data(MAP_TRAP, x, y, map_trap, force_update);
    set_map_data(MAP_SPECIAL, x, y, map_special, force_update);
    set_map_data(MAP_FURNITURE, x, y, map_furniture, force_update);
    set_map_data(MAP_BACK, x, y, map_back, force_update);
    set_map_data(MAP_DARKNESS, x, y, darkness, force_update);
    set_map_data(MAP_PET, x, y, is_pet,
                 false); /* you can't be your own pet */
}

/* NetHack 3.6.x let a windowport request travel by returning the core-defined
 * pseudo-key CMD_TRAVEL (0x90), which rhack() recognised inline.  5.0.0 deleted
 * that whole spkeys/NHKF_TRAVEL layer, so returning 0x90 now just produces
 * "Unknown command 'M-^P'".  Travel to a remembered destination is now the
 * ordinary command #retravel (dotravel_target), which commands_init() binds to
 * CTRL+'_'.  Look the binding up at run time so that a user rebinding it in
 * their RC file cannot silently break click-to-move. */
static int
vulture_retravel_key(void)
{
    const struct ext_func_tab *extcmd;

    for (extcmd = extcmdlist; extcmd->ef_txt; extcmd++)
        if (!strcmp(extcmd->ef_txt, "retravel")) {
            char key = cmd_from_func(extcmd->ef_funct);

            /* 0 means "currently unbound"; fall back to the stock key rather
             * than returning 0, which the core would read as a mouse click. */
            if (key)
                return (int) (unsigned char) key;
            break;
        }
    return CTRL('_');
}

/* convert an action_id into an actual key to be passed to nethack to perform
 * the action */
int
mapdata::perform_map_action(int action_id, point mappos)
{
    switch (action_id) {
    case V_ACTION_TRAVEL:
        /* 5.0.0's dotravel_target() takes its destination from
         * iflags.travelcc, NOT from u.tx/u.ty; findtravelpath()/domove() still
         * want u.tx/u.ty.  Every in-tree 5.0.0 caller sets both. */
        iflags.travelcc.x = u.tx = (coordxy) mappos.x;
        iflags.travelcc.y = u.ty = (coordxy) mappos.y;
        return vulture_retravel_key();

    case V_ACTION_MOVE_HERE:
        return mappos_to_dirkey(mappos);

    case V_ACTION_LOOT:
        return META('l');
    case V_ACTION_PICK_UP:
        return ',';
    case V_ACTION_GO_DOWN:
        return '>';
    case V_ACTION_GO_UP:
        return '<';
    case V_ACTION_SEARCH:
        return 's';
    case V_ACTION_ENGRAVE:
        return 'E';
    case V_ACTION_LOOK_AROUND:
        return ':';
    case V_ACTION_PAY_BILL:
        return 'p';
    case V_ACTION_OFFER:
        return META('o');
    case V_ACTION_PRAY:
        return META('p');
    case V_ACTION_REST:
        return '.';
    case V_ACTION_SIT:
        return META('s');
    case V_ACTION_TURN_UNDEAD:
        return META('t');
    case V_ACTION_WIPE_FACE:
        return META('w');
    case V_ACTION_FORCE_LOCK:
        return META('f');
    case V_ACTION_MONSTER_ABILITY:
        return META('m');

    case V_ACTION_DRINK:
#if VULTURE_SLASHEM
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
#endif
        return 'q';

    case V_ACTION_READ:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
        return 'r';

    case V_ACTION_KICK:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
        return CTRL('d');

    case V_ACTION_OPEN_DOOR:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
        return 'o';

    case V_ACTION_UNTRAP:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
        return META('u');

    case V_ACTION_CLOSE_DOOR:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
        return 'c';

    case V_ACTION_WHATS_THIS:
        /* events get stored on a STACK, so calls to eventstack add
         * need to be done in revers order */
        vulture_whatis_singleshot = 1;

        /* select the mapsquare */
        vulture_eventstack_add(0, mappos.x, mappos.y, V_RESPOND_POSKEY);

        /* yes, we _really do_ want more info */
        vulture_eventstack_add('y', -1, -1, V_RESPOND_CHARACTER);

        return '/';

    case V_ACTION_CHAT:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_CHARACTER);
        return META('c');

    case V_ACTION_FIGHT:
        vulture_eventstack_add(mappos_to_dirkey(mappos), -1, -1,
                               V_RESPOND_POSKEY);
        return 'F';

    case V_ACTION_FIRE:
        return 'f';

    case V_ACTION_NAMEMON:
        vulture_eventstack_add(0, mappos.x, mappos.y, V_RESPOND_POSKEY);
        return 'C';

    default:
        break;
    }
    return 0;
}

/* converts a mappos adjacent to the player to the dirkey pointing in that
 * direction */
char
mapdata::mappos_to_dirkey(point mappos)
{
    const char chartable[3][3] = { { '7', '8', '9' },
                                   { '4', '.', '6' },
                                   { '1', '2', '3' } };
    int dx = mappos.x - u.ux;
    int dy = mappos.y - u.uy;

    if (dx < -1 || dx > 1 || dy < -1 || dy > 1)
        return 0;

    return vulture_numpad_to_hjkl(chartable[dy + 1][dx + 1], 0);
}

/* s72: 5.0 context-menu bridge (cmd.c) and the last popup's entries */
extern "C" int vulture_collect_cmd_menu(coordxy, coordxy, int *,
                                        char (*)[80], int);
extern "C" void vulture_do_cmd_menu_act(coordxy, coordxy, int);
static int vmenu_acts[32];
static int vmenu_count = 0;

eventresult
mapdata::handle_click(void *result, int button, point mappos)
{
    int retval, action_id = 0;

    /* if vulture_whatis_active is set, we want a location (for look or
     * teleport) */
    if (vulture_whatis_active) {
        ((vulture_event *) result)->num = 0;
        ((vulture_event *) result)->x = mappos.x;
        ((vulture_event *) result)->y = mappos.y;
        return V_EVENT_HANDLED_FINAL;
    }

    /* else  */
    /* right click: try to resolve the click on the map to a default action */
    if (button == SDL_BUTTON_LEFT)
        action_id = get_map_action(mappos);
    /* left click: allow the user to choose from a context menu */
    else if (button == SDL_BUTTON_RIGHT)
        action_id = get_map_contextmenu(mappos);

    /* s72: an entry from 5.0's own menu - queue it through the core and
     * hand back a click that readchar_core() knows is already handled */
    if (action_id >= V_ACTION_NHMENU
        && action_id < V_ACTION_NHMENU + vmenu_count) {
        vulture_do_cmd_menu_act(mappos.x, mappos.y,
                                vmenu_acts[action_id - V_ACTION_NHMENU]);
        ((vulture_event *) result)->num = 0;
        ((vulture_event *) result)->x = mappos.x;
        ((vulture_event *) result)->y = mappos.y;
        return V_EVENT_HANDLED_FINAL;
    }

    /* if an action was chosen, return it and leave the event loop */
    if (action_id) {
        retval = perform_map_action(action_id, mappos);
        if (retval) {
            ((vulture_event *) result)->num = retval;
            return V_EVENT_HANDLED_FINAL;
        }
    }

    return V_EVENT_HANDLED_NOREDRAW;
}

void
mapdata::set_map_data(glyph_type type, int x, int y, int newval, bool force)
{
    int(*data_array)[ROWNO][COLNO], prevtile;

    switch (type) {
    case MAP_MON:
        data_array = &map_mon;
        break;
    case MAP_OBJ:
        data_array = &map_obj;
        break;
    case MAP_TRAP:
        data_array = &map_trap;
        break;
    case MAP_BACK:
        data_array = &map_back;
        break;
    case MAP_SPECIAL:
        data_array = &map_specialeff;
        break;
    case MAP_FURNITURE:
        data_array = &map_furniture;
        break;
    case MAP_DARKNESS:
        data_array = &map_darkness;
        break;
    case MAP_PET:
        data_array = &map_pet;
        break;
    case MAP_GLYPH:
        /* raw glyph values are only stored, not printed */
        map_glyph[y][x] = newval;
        return;
    default:
        throw "Invalid type for map data";
    }

    if ((*data_array)[y][x] != newval || force) {
        prevtile = (*data_array)[y][x];

        for (std::vector<mapviewer *>::iterator i = views.begin();
             i != views.end(); ++i)
            (*i)->map_update(type, prevtile, newval, x, y);

        (*data_array)[y][x] = newval;
    }
}

int
mapdata::get_glyph(glyph_type type, int x, int y) const
{
    switch (type) {
    case MAP_MON:
        return map_mon[y][x];
    case MAP_OBJ:
        return map_obj[y][x];
    case MAP_TRAP:
        return map_trap[y][x];
    case MAP_BACK:
        return map_back[y][x];
    case MAP_SPECIAL:
        return map_specialeff[y][x];
    case MAP_FURNITURE:
        return map_furniture[y][x];
    case MAP_DARKNESS:
        return map_darkness[y][x];
    case MAP_PET:
        return map_pet[y][x];
    case MAP_GLYPH:
        return map_glyph[y][x];

    default:
        return -1;
    }
}

std::string
mapdata::map_square_description(point target, int include_seen)
{
    struct permonst *pm;
    std::string out_str = "";
    char monbuf[BUFSZ], temp_buf[BUFSZ], coybuf[BUFSZ], look_buf[BUFSZ];
    struct monst *mtmp = (struct monst *) 0;
    const char *firstmatch;
    int n_objs;
    struct obj *obj;

    if ((target.x < 1) || (target.x >= COLNO) || (target.y < 0)
        || (target.y >= ROWNO))
        return out_str;

    /* All of monsters, objects, traps and furniture get descriptions */
    if ((map_mon[target.y][target.x] != V_TILE_NONE)) {
        look_buf[0] = '\0';
        monbuf[0] = '\0';
        /* coyotename() leaves its buffer untouched when it is passed a
         * NULL monster (m_at() finds nothing at the square -- e.g. the
         * hero's own square while polymorphed into a coyote, or a
         * remembered/hallucinated coyote glyph), so coybuf must not be
         * passed uninitialized */
        coybuf[0] = '\0';
        pm = lookat(target.x, target.y, look_buf, monbuf);
        firstmatch = look_buf;
        if (look_buf[0]) {
            mtmp = m_at(target.x, target.y);
            Sprintf(temp_buf, "%s",
                    (pm == &mons[PM_COYOTE]) ? coyotename(mtmp, coybuf)
                                             : firstmatch);
            out_str = temp_buf;
        }
        if (include_seen) {
            if (monbuf[0]) {
                sprintf(temp_buf, " [seen: %s]", monbuf);
                out_str += temp_buf;
            }
        }

        /* Vulture mod (IMPLEMENTATION_LOG §46): a pet you can actually see
           shows its health and hunger - the hero can tell at a glance.
           Hunger mirrors dogmove.c (dog_hunger / DOG_HUNGRY 300, DOG_WEAK
           500): "hungry" once it would eat anything, "starving" once it is
           losing max HP.  Skipped while hallucinating (the shown monster
           is not the real one). */
        if (look_buf[0] && mtmp && mtmp->mtame && canspotmon(mtmp)
            && !Hallucination) {
            const char *hunger = "";

            if (has_edog(mtmp)) {
                if (svm.moves > EDOG(mtmp)->hungrytime + 500L)
                    hunger = ", starving!";
                else if (svm.moves > EDOG(mtmp)->hungrytime + 300L)
                    hunger = ", hungry";
            }
            snprintf(temp_buf, BUFSZ, " (HP %d/%d%s)", mtmp->mhp,
                     mtmp->mhpmax, hunger);
            out_str += temp_buf;
        }
    } else if (map_obj[target.y][target.x] != V_TILE_NONE) {
        look_buf[0] = '\0';
        monbuf[0] = '\0';
        lookat(target.x, target.y, look_buf, monbuf);

        n_objs = 0;
        obj = svl.level.objects[target.x][target.y];
        while (obj) {
            n_objs++;
            obj = obj->nexthere;
        }

        if (n_objs > 1) {
            snprintf(temp_buf, BUFSZ, "%s (+%d other object%s)", look_buf,
                     n_objs - 1, (n_objs > 2) ? "s" : "");
            out_str = temp_buf;
        } else
            out_str = look_buf;
    } else if ((map_trap[target.y][target.x] != V_TILE_NONE)
               || (map_furniture[target.y][target.x] != V_TILE_NONE)) {
        lookat(target.x, target.y, look_buf, monbuf);
        out_str = look_buf;
    }

    return out_str;
}

map_action
mapdata::get_map_action(point mappos)
{
    int mapglyph_offset;

    /* Off-map squares have no default action */
    if ((mappos.x < 1) || (mappos.x >= COLNO) || (mappos.y < 0)
        || (mappos.y >= ROWNO))
        return V_ACTION_NONE;

    /* Target is at least 2 squares away */
    if ((abs(u.ux - mappos.x) >= 2) || (abs(u.uy - mappos.y) >= 2))
        return V_ACTION_TRAVEL;

    /* Monster on target square */
    if (map_mon[mappos.y][mappos.x] != V_TILE_NONE
        && (u.ux != mappos.x || u.uy != mappos.y))
        return V_ACTION_MOVE_HERE;

    /* Object on target square */
    if (map_obj[mappos.y][mappos.x] != V_TILE_NONE && u.ux == mappos.x
        && u.uy == mappos.y) {
        mapglyph_offset = map_obj[mappos.y][mappos.x];
        switch (mapglyph_offset - OBJTILEOFFSET) {
        case LARGE_BOX:
        case ICE_BOX:
        case CHEST:
            return V_ACTION_LOOT;

        default:
            return V_ACTION_PICK_UP;
        }
    }

    /* map feature on target square */
    if (map_furniture[mappos.y][mappos.x] != V_TILE_NONE) {
        if ((u.ux == mappos.x) && (u.uy == mappos.y))
            switch (map_furniture[mappos.y][mappos.x]) {
            case V_MISC_STAIRS_DOWN:
            case V_MISC_LADDER_DOWN:
                return V_ACTION_GO_DOWN;

            case V_MISC_STAIRS_UP:
            case V_MISC_LADDER_UP:
                return V_ACTION_GO_UP;

            case V_MISC_FOUNTAIN:
            case V_MISC_SINK:
#ifdef VULTURE_SLASHEM
            case V_MISC_TOILET:
#endif
                return V_ACTION_DRINK;
            }

        else
            switch (map_furniture[mappos.y][mappos.x]) {
            case V_MISC_VDOOR_WOOD_CLOSED:
            case V_MISC_HDOOR_WOOD_CLOSED:
                return V_ACTION_OPEN_DOOR;
            }
    }

    /* default action for your own square */
    if (u.ux == mappos.x && u.uy == mappos.y)
        return V_ACTION_SEARCH;

    /* default action for adjacent squares (nonadjacent squares were handled
     * further up)*/
    /* if the square contains an object and there is no monster ther, use
     * trvel after all to suppress the messegebox listing the objects.
     * EXCEPTION: boulders.  You interact with a boulder by walking INTO it
     * (push), and 5.0.0's travel refuses to path into an occupied square, so
     * classifying an adjacent boulder as TRAVEL made click-pushing
     * impossible; only a direction key pushes. */
    if (map_obj[mappos.y][mappos.x] && !svl.level.monsters[mappos.x][mappos.y]
        && !sobj_at(BOULDER, mappos.x, mappos.y))
        return V_ACTION_TRAVEL;

    if (u.ux != mappos.x || u.uy != mappos.y)
        return V_ACTION_MOVE_HERE;

    return V_ACTION_NONE;
}

/* IMPLEMENTATION_LOG s72: the right-click popup lists what 5.0's own
 * #therecmdmenu would offer for this square (doors: open/close/lock/
 * unlock/kick/untrap; monsters: chat/attack/name/ride/saddle; your square:
 * pick up/loot/eat/quaff/dip/sit/offer/stairs/tip/...; far squares:
 * throw/travel) and performs the pick through 5.0's act_on_act().  The
 * old hardcoded 3.x list missed most of these.  Vulture's own extras that
 * 5.0's menu lacks (Fire, Engrave, Pray, Turn undead, Wipe face, Pay bill,
 * What's this?) are appended. */

map_action
mapdata::get_map_contextmenu(point mappos)
{
    contextmenu *menu;
    char txt[32][80];
    int i, result, extras = 0;
    bool self = (u.ux == mappos.x && u.uy == mappos.y);
    bool has_look = false;

    /* Dropdown commands are shown only for valid squares */
    if ((mappos.x < 1) || (mappos.x >= COLNO) || (mappos.y < 0)
        || (mappos.y >= ROWNO))
        return V_ACTION_NONE;

    menu = new contextmenu(levwin);

    vmenu_count = vulture_collect_cmd_menu(mappos.x, mappos.y, vmenu_acts,
                                           txt, 32);
    for (i = 0; i < vmenu_count; i++) {
        if (strstr(txt[i], "Look at"))
            has_look = true;
        menu->add_item(txt[i], V_ACTION_NHMENU + i);
    }

    if (self) {
        menu->add_item("Fire", V_ACTION_FIRE), extras++;
        menu->add_item("Engrave", V_ACTION_ENGRAVE), extras++;
        if (*u.ushops)
            menu->add_item("Pay bill", V_ACTION_PAY_BILL), extras++;
        menu->add_item("Pray", V_ACTION_PRAY), extras++;
        if (Role_if(PM_PRIEST) || Role_if(PM_KNIGHT)
            || objects[SPE_TURN_UNDEAD].oc_name_known)
            menu->add_item("Turn undead", V_ACTION_TURN_UNDEAD), extras++;
        if (Glib || u.ucreamed)
            menu->add_item("Wipe face", V_ACTION_WIPE_FACE), extras++;
    }
    if (!has_look && map_back[mappos.y][mappos.x] != V_TILE_NONE)
        menu->add_item("What's this?", V_ACTION_WHATS_THIS), extras++;

    if (!vmenu_count && !extras) {
        delete menu;
        return V_ACTION_NONE;
    }

    menu->layout();
    vulture_event_dispatcher(&result, V_RESPOND_INT, menu);

    delete menu;

    return (map_action) result;
}

void
mapdata::add_viewer(mapviewer *v)
{
    views.push_back(v);
}

void
mapdata::del_viewer(mapviewer *v)
{
    for (std::vector<mapviewer *>::iterator i = views.begin();
         i != views.end(); ++i) {
        if (*i == v) {
            views.erase(i);
            return;
        }
    }
}
