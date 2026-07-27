/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

extern "C" {
#include "hack.h"

/* from the generated src/tile.c (5.0.0 replaced 3.6.x's glyph2tile[]);
 * NetHack declares it locally in each consumer rather than in a header. */
extern glyph_map glyphmap[MAX_GLYPH];
}

#include "vulture_gfl.h"
#include "vulture_gra.h"
#include "vulture_mou.h"
#include "vulture_opt.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"
#include "patha/remap_table.h"

/* 5.0.0 dropped glyph2tile[]; a glyph's tile index (already remapped into
 * Vulture atlas space at generation time by tilemap.c) now lives in glyphmap[].
 * Bounds-check at the draw site so an out-of-range value renders the reserved
 * placeholder cell rather than arbitrary art. */
static int
vulture_patha_cell(int glyph)
{
    int cell;

    if (glyph < 0 || glyph >= MAX_GLYPH)
        return VULTURE_PATHA_PLACEHOLDER;
    cell = (int) glyphmap[glyph].tileidx;
    if (cell < 0 || cell > VULTURE_PATHA_PLACEHOLDER)
        return VULTURE_PATHA_PLACEHOLDER;
    return cell;
}

#include "hotspot.h"
#include "levelwin.h"
#include "map.h"
#include "mapdata.h"
#include "textwin.h"

/* Cell size of the ascii symbol atlas, in the atlas's OWN (native) pixels.
 * The parchment, the symbols and every offset into them are UI, not map, so
 * they are all multiplied by this window's ui_scale - which keeps the overview
 * consistent at any render_scale, and additionally zooms it to fill the
 * screen (see the constructor). */
#define SYMBOL_WIDTH 7
#define SYMBOL_HEIGHT 14

/* Native size of the parchment painting. */
#define MAP_BASE_W 640
#define MAP_BASE_H 480
/* Sanity stop for the zoom search below.  Nothing is expected to reach it
 * (4x is a 2560x1920 parchment), it just bounds the scaled art's memory. */
#define MAP_MAX_SCALE 4

window *mapwin = NULL;

/* Height of the message band at the top of the screen, which the overview map
 * must not cover.  messagewin lays itself out at the very top and grows to at
 * most 'messagelines' lines, so this is its maximum extent. */
static int
map_message_band(int parent_h)
{
    int msg_h = vulture_opts.messagelines
                    * (vulture_get_lineheight(V_FONT_MESSAGE) + 1) + 2;

    /* a nonsense value must not cost us half the screen */
    if (msg_h < 0 || msg_h > parent_h / 2)
        return 0;
    return msg_h;
}

/* vulture_scale_ui_surface() is hardwired to the global UI scale.  The
 * overview map builds its art at its OWN zoom (see the constructor), so it
 * needs the same operation against an explicit factor. */
static void
map_scale_surface(SDL_Surface **surf, int factor)
{
    SDL_Surface *scaled;

    if (!surf || !*surf || factor <= 1)
        return;

    scaled = vulture_scale_surface_int(*surf, factor);
    if (!scaled)
        return;

    SDL_FreeSurface(*surf);
    *surf = scaled;
}

map::map(levelwin *p, mapdata *data) : window(p), map_data(data)
{
    int i;
    int msg_h, avail_h, max_scale;
    SDL_Surface *image;

    v_type = V_WINTYPE_MAP;
    autobg = 1;
    need_redraw = 1;
    /* The parchment is a fixed 640x480 painting, so at ui_scale 1 the whole
     * level overview is a postage stamp in the middle of a modern display.
     * Build ALL of this window's art at an integer zoom instead.
     *
     * The message window is anchored to the top of the screen and is as tall
     * as 'messagelines' lines, so reserve that band and fit the map BELOW it:
     * the map is a thing you consult mid-game and it must never bury the
     * messages telling you what just happened.  The status bar and toolbar
     * draw over the map, so only the top needs reserving.
     *
     * map_zoom overrides the search outright for anyone who wants a specific
     * size.  Either way the result is clamped so the map still fits on screen.
     *
     * From here on ui_scale is THIS WINDOW'S scale, not necessarily the
     * global one: every offset below, draw() and the mouse inverse all derive
     * from it, so the art and the hit-test cannot drift apart. */
    msg_h = map_message_band(parent->h);
    avail_h = parent->h - msg_h;

    max_scale = 1;
    while (max_scale < MAP_MAX_SCALE
           && (max_scale + 1) * MAP_BASE_W <= parent->w
           && (max_scale + 1) * MAP_BASE_H <= avail_h)
        max_scale++;

    if (vulture_opts.map_zoom > 0)
        ui_scale = vulture_opts.map_zoom;
    else
        ui_scale = max_scale;

    if (ui_scale > max_scale)
        ui_scale = max_scale;
    if (ui_scale < 1)
        ui_scale = 1;

    w = ui_scale * MAP_BASE_W;
    h = ui_scale * MAP_BASE_H;
    x = (parent->w - w) / 2;
    /* centre in the band below the messages, not on the whole screen */
    y = msg_h + (avail_h - h) / 2;
    if (x < 0)
        x = 0;
    if (y < msg_h)
        y = msg_h;
    abs_x = parent->abs_x + x;
    abs_y = parent->abs_y + y;

    /* Load map parchment (unscaled, then scaled to this window's zoom) */
    mapbg = vulture_load_graphic(V_FILENAME_MAP_PARCHMENT);
    map_scale_surface(&mapbg, ui_scale);

    /* level title */
    textwin *txt = new textwin(this, "");
    txt->x = w / 2;
    txt->y = ui_scale * 48;

    /* close button */
    hotspot *close = new hotspot(this, ui_scale * 598, ui_scale * 11,
                                 ui_scale * 28, ui_scale * 24, 1, "Close map");
    close->abs_x = abs_x + close->x;
    close->abs_y = abs_y + close->y;

    /* Null the symbol array first: if the atlas fails to load we return
     * early below, and draw()/~map() must then see NULLs (vulture_put_img
     * and SDL_FreeSurface are both NULL-safe), not uninitialized garbage. */
    for (i = 0; i < V_MAX_MAP_SYMBOLS; i++)
        map_symbols[i] = NULL;

    /* Load map symbols */
    image = vulture_load_map(V_THEME_MAP_SYMBOLS, V_FILENAME_MAP_SYMBOLS);
    if (image == NULL)
        return;
    else {
        for (i = 0; i < V_MAX_MAP_SYMBOLS; i++) {
            map_symbols[i] = vulture_get_img_src(
                (i % 40) * SYMBOL_WIDTH, (i / 40) * SYMBOL_HEIGHT,
                (i % 40) * SYMBOL_WIDTH + (SYMBOL_WIDTH - 1),
                (i / 40) * SYMBOL_HEIGHT + (SYMBOL_HEIGHT - 1), image);
            /* the symbols are UI art (ascii glyphs on parchment), not map
             * tiles - scale them exactly like the parchment they sit on */
            map_scale_surface(&map_symbols[i], ui_scale);
        }
        SDL_FreeSurface(image);
    }
}

map::~map()
{
    int i;

    /* free the map symbols */
    for (i = 0; i < V_MAX_MAP_SYMBOLS; i++)
        SDL_FreeSurface(map_symbols[i]);

    SDL_FreeSurface(mapbg);
}

bool
map::draw()
{
    int map_x, map_y;
    int i, j;
    window *txt = first_child;
    char buffer[256];

    /* Draw parchment */
    vulture_put_img(abs_x, abs_y, mapbg);

    /* Draw the level name */
#ifdef VULTURE_SLASHEM
    describe_level(buffer, TRUE);
#else
    if (!describe_level(buffer, 0))
        sprintf(buffer, "%s, level %d ", svd.dungeons[u.uz.dnum].dname,
                depth(&u.uz));
#endif
    txt->set_caption(buffer);
    txt->x = (w - vulture_text_length(V_FONT_HEADLINE, txt->caption)) / 2;
    txt->abs_x = abs_x + txt->x;
    txt->abs_y = abs_y + txt->y;
    vulture_put_text_shadow(V_FONT_HEADLINE, txt->caption, vulture_screen,
                            txt->abs_x, txt->abs_y, CLR32_BROWN,
                            CLR32_BLACK_A50);

    /* Find upper left corner of map on parchment */
    map_x = abs_x + ui_scale * 39;
    map_y = abs_y + ui_scale * 91;

    /* Draw map on parchment, and create hotspots */
    for (i = 0; i < ROWNO; i++)
        for (j = 1; j < COLNO; j++) {
            int map_glyph = map_data->get_glyph(MAP_GLYPH, j, i);
            bool is_dark = (map_data->get_glyph(MAP_DARKNESS, j, i) == 2);

            /* 5.0.0: never-seen squares arrive as GLYPH_UNEXPLORED (and
             * GLYPH_NOTHING), no longer as the stone cmap glyph, so skip
             * them explicitly - blank parchment, as in 3.6.6. Seen-but-dark
             * stone is drawn with the darkroom symbol because 5.0.0's
             * "stone" tile has no named counterpart in the 3.6.6 atlas
             * (its remap entry is the placeholder). */
            if (map_glyph != NO_GLYPH
                && map_glyph != GLYPH_UNEXPLORED
                && map_glyph != GLYPH_NOTHING
                && (map_glyph != cmap_to_glyph(S_stone)
                    || (svl.level.locations[j][i].seenv && is_dark))) {
                int cell_glyph = (map_glyph == cmap_to_glyph(S_stone))
                                     ? cmap_to_glyph(S_darkroom) : map_glyph;
                vulture_put_img(map_x + ui_scale * SYMBOL_WIDTH * j,
                                map_y + ui_scale * SYMBOL_HEIGHT * i,
                                map_symbols[vulture_patha_cell(cell_glyph)]);
            }
        }

    vulture_invalidate_region(abs_x, abs_y, w, h);

    return false;
}

void
map::toggle(void)
{
    if (!mapwin) {
        /* Placement belongs to the constructor: it is the only code that
         * knows the chosen zoom and the message band that must stay clear.
         * Re-centring here would silently undo both - window.cpp recomputes
         * abs_x/abs_y from x/y on every walk, so whatever is left in x/y
         * wins. */
        mapwin = new map(levwin, ::map_data);
    } else {
        mapwin->hide();
        delete mapwin;
        mapwin = NULL;
    }
}

eventresult
map::handle_timer_event(window *target, void *result, int time)
{
    std::string ttext;
    point mappos = get_mouse_mappos();

    if (time < HOVERTIMEOUT)
        return V_EVENT_HANDLED_NOREDRAW;

    /* draw the tooltip for the close button */
    if (this != target && target->menu_id == 1)
        vulture_mouse_set_tooltip(target->caption);
    /* draw a tooltip for the map location */
    else if (mappos.x != -1) {
        ttext = map_data->map_square_description(mappos, 1);
        if (!ttext.empty())
            vulture_mouse_set_tooltip(ttext);
    }

    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
map::handle_mousemotion_event(window *target, void *result, int xrel,
                              int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
map::handle_mousebuttonup_event(window *target, void *result, int mouse_x,
                                int mouse_y, int button, int state)
{
    point mappos = get_mouse_mappos();

    /* handler != target if the user clicked on the X in the upper right
     * corner */
    if (this != target && target->menu_id == 1) {
        toggle();
        vulture_mouse_invalidate_tooltip(1);
        return V_EVENT_HANDLED_REDRAW;
    }

    /* only handle clicks on valid map locations */
    if (mappos.x != -1)
        return map_data->handle_click(result, button, mappos);

    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
map::handle_resize_event(window *target, void *result, int res_w, int res_h)
{
    /* The zoom is not recomputed here: the art was already built at this
     * window's scale and rebuilding it mid-event is not worth it - toggling
     * the map off and on picks a zoom for the new size.  Keep clear of the
     * message band exactly as the constructor does, and clamp so a shrink
     * below the parchment's size leaves it visible at the top left rather
     * than centred off-screen. */
    int msg_h = map_message_band(parent->h);

    x = (parent->w - w) / 2;
    y = msg_h + (parent->h - msg_h - h) / 2;
    if (x < 0)
        x = 0;
    if (y < msg_h)
        y = msg_h;
    abs_x = parent->abs_x + x;
    abs_y = parent->abs_y + y;
    return V_EVENT_HANDLED_NOREDRAW;
}

point
map::get_mouse_mappos(void)
{
    point mouse, mappos;

    mouse = vulture_get_mouse_pos();
    /* exact inverse of draw()'s placement, in the same UI-scaled space */
    mappos.x = (mouse.x - abs_x - ui_scale * 39) / (ui_scale * SYMBOL_WIDTH);
    mappos.y = (mouse.y - abs_y - ui_scale * 91) / (ui_scale * SYMBOL_HEIGHT);

    if (mappos.x < 1 || mappos.x >= COLNO || mappos.y < 0
        || mappos.y >= ROWNO) {
        mappos.x = -1;
        mappos.y = -1;
    }

    return mappos;
}
