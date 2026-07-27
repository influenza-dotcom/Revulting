/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

extern "C" {
#include "hack.h"
}

#include "vulture_gfl.h"
#include "vulture_gra.h"
#include "vulture_main.h"
#include "vulture_mou.h"
#include "vulture_opt.h"
#include "vulture_tile.h"
#include "vulture_win.h"

#include "levelwin.h"
#include "minimap.h"

minimap *minimapwin = NULL;

minimap::minimap(levelwin *p, mapdata *data)
    : window(p), level(p), map_data(data)
{
    /* UI chrome: loaded at vulture_ui_scale (see vulture_types.h).  draw()
     * pokes the symbol pixels straight into this surface, so every offset it
     * uses is scaled by the same factor - see the V_UI() calls there. */
    minimapbg = vulture_load_graphic_ui(V_FILENAME_MINIMAPBG);
    bg_scale = vulture_ui_scale;
    w = minimapbg->w;
    h = minimapbg->h;
    x = parent->w - (w + V_UI(6));
    y = V_UI(6);
    visible = vulture_opts.show_minimap;
    menu_id = V_WIN_MINIMAP;
    autobg = 1;

    // Initialize minimap symbols
    for (int map_y = 0; map_y < ROWNO; map_y++) {
        for (int map_x = 1; map_x < COLNO; map_x++) {
            vulture_minimap_syms[map_y][map_x] = V_MMTILE_NONE;
        }
    }

    minimapwin = this;
}

minimap::~minimap()
{
    SDL_FreeSurface(minimapbg);
    minimapwin = NULL;
}

bool
minimap::draw()
{
    int map_x, map_y, sym;
    SDL_Rect destrect = { 0, 0, 3, 2 };
    const int s = bg_scale;

    if (vulture_windows_inited != TRUE || minimapwin != this)
        return false;

    Uint32 minimap_colors[V_MMTILE_MAX] = {
        CLR32_BLACK,       V_COLOR_MINI_FLOOR, V_COLOR_MINI_STAIRS,
        V_COLOR_MINI_DOOR, V_COLOR_MINI_YOU,   CLR32_GREEN,
        CLR32_RED
    };

    if (!minimapbg)
        return false;

    if (this->background)
        vulture_put_img(abs_x, abs_y, background);

    for (map_y = 0; map_y < ROWNO; map_y++) {
        for (map_x = 1; map_x < COLNO; map_x++) {
            /* translate the contents of the map into a minimap symbol color
             */
            switch (map_data->get_glyph(MAP_BACK, map_x, map_y)) {
            case V_TILE_WALL_GENERIC:
            case V_MISC_UNMAPPED_AREA:
                sym = V_MMTILE_NONE;
                break;

            default:
                sym = V_MMTILE_FLOOR;
                break;
            }

            switch (map_data->get_glyph(MAP_FURNITURE, map_x, map_y)) {
            case V_MISC_STAIRS_UP:
            case V_MISC_STAIRS_DOWN:
                sym = V_MMTILE_STAIRS;
                break;

            case V_MISC_VDOOR_WOOD_OPEN:
            case V_MISC_VDOOR_WOOD_CLOSED:
            case V_MISC_HDOOR_WOOD_OPEN:
            case V_MISC_HDOOR_WOOD_CLOSED:
            case V_MISC_VODBRIDGE:
            case V_MISC_HODBRIDGE:
            case V_MISC_VCDBRIDGE:
            case V_MISC_HCDBRIDGE:
                sym = V_MMTILE_DOOR;
                break;
            }

            if (map_data->get_glyph(MAP_TRAP, map_x, map_y)
                == V_MISC_MAGIC_PORTAL)
                sym = V_MMTILE_STAIRS;

            if (map_data->get_glyph(MAP_MON, map_x, map_y) != V_TILE_NONE) {
                if (map_data->get_glyph(MAP_PET, map_x, map_y))
                    sym = V_MMTILE_PET;
                else
                    sym = V_MMTILE_MONSTER;
            }

            if ((map_y == u.uy) && (map_x == u.ux))
                sym = V_MMTILE_YOU;

            /* draw symbols that changed onto the "minimap surface" */
            if (sym != vulture_minimap_syms[map_y][map_x]) {
                int row, col, sx, sy;

                vulture_minimap_syms[map_y][map_x] = sym;

                destrect.x = 40 + 2 * map_x - 2 * map_y;
                destrect.y = map_x + map_y;

                /* A minimap symbol has this shape: _ C _
                 *                                  C C C
                 * where "_" is transparent and "C" is a colored pixel.
                 *
                 * The background surface was upscaled s:1 when it was loaded,
                 * so the symbol's origin scales with it and each of its
                 * notional pixels becomes an s x s block.  At s == 1 this
                 * writes exactly the six words the original did. */
                static const char pattern[2][3] = { { 0, 1, 0 },
                                                    { 1, 1, 1 } };

                for (row = 0; row < 2; row++)
                    for (col = 0; col < 3; col++) {
                        if (!pattern[row][col])
                            continue;

                        for (sy = 0; sy < s; sy++) {
                            Uint32 *pixels = (Uint32 *) ((char *) minimapbg->pixels
                                    + minimapbg->pitch
                                          * (s * (destrect.y + 6 + row) + sy)
                                    + s * (destrect.x + 6 + col) * 4);

                            for (sx = 0; sx < s; sx++)
                                pixels[sx] = minimap_colors[sym];
                        }
                    }
            }
        }
    }

    vulture_put_img(abs_x, abs_y, minimapbg);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    return false;
}

eventresult
minimap::handle_mousemotion_event(window *target, void *result, int xrel,
                                  int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
minimap::handle_mousebuttonup_event(window *target, void *result, int mouse_x,
                                    int mouse_y, int button, int state)
{
    point mouse, mappos;
    int offs_x, offs_y;

    /* translate the mouse position to a map coordinate */
    mouse = vulture_get_mouse_pos();

    /* inverse of draw()'s symbol placement, in the same (scaled) space: the
     * background was upscaled bg_scale:1, so both the origin and the 4-unit
     * cell pitch scale with it */
    offs_x = mouse.x - abs_x - (6 + 40) * bg_scale;
    offs_y = mouse.y - abs_y - 6 * bg_scale;
    mappos.x = (offs_x + 2 * offs_y) / (4 * bg_scale);
    mappos.y = (-offs_x + 2 * offs_y) / (4 * bg_scale);

    if (mappos.x < 1 || mappos.x > COLNO || mappos.y < 0 || mappos.y > ROWNO)
        return V_EVENT_UNHANDLED;

    /* limited mouse event handling, due to the fact that the minimap
     * is too small for precise targeting */
    /* left button: direct selection of a location (for very imprecise
     * teleport) */
    if (button == SDL_BUTTON_LEFT) {
        if (vulture_whatis_active) {
            ((vulture_event *) result)->num = 0;
            ((vulture_event *) result)->x = mappos.x;
            ((vulture_event *) result)->y = mappos.y;
            return V_EVENT_HANDLED_FINAL;
        }

        ((vulture_event *) result)->num =
            map_data->perform_map_action(V_ACTION_TRAVEL, mappos);
        return V_EVENT_HANDLED_FINAL;
    }
    /* right button: travel to location */
    else if (button == SDL_BUTTON_RIGHT) {
        levwin->set_view(mappos.x, mappos.y);
        levwin->need_redraw = 1;
        return V_EVENT_HANDLED_REDRAW;
    }

    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
minimap::handle_resize_event(window *target, void *result, int res_w,
                             int res_h)
{
    /* a runtime render-scale change (F8 / Interface Options) arrives here as a
     * resize; rebuild the background at the new factor and re-poke every
     * symbol from scratch */
    if (bg_scale != vulture_ui_scale) {
        SDL_Surface *newbg = vulture_load_graphic_ui(V_FILENAME_MINIMAPBG);

        if (newbg) {
            SDL_FreeSurface(minimapbg);
            minimapbg = newbg;
            bg_scale = vulture_ui_scale;
            w = minimapbg->w;
            h = minimapbg->h;
            y = V_UI(6);

            for (int map_y = 0; map_y < ROWNO; map_y++)
                for (int map_x = 1; map_x < COLNO; map_x++)
                    vulture_minimap_syms[map_y][map_x] = V_MMTILE_NONE;
        }
    }

    x = parent->w - (w + V_UI(6));
    return V_EVENT_HANDLED_NOREDRAW;
}
