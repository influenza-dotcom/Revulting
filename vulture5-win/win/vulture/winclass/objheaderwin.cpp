/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "objheaderwin.h"

objheaderwin::objheaderwin(window *parent, std::string cap, int tile)
    : window(parent), tile(tile)
{
    v_type = V_WINTYPE_OBJITEMHEADER;
    w = V_LISTITEM_WIDTH;
    h = V_LISTITEM_HEIGHT;
    autobg = true;
    caption = cap;
}

bool
objheaderwin::draw()
{
    /* constrain drawing to this window */
    vulture_set_draw_region(abs_x, abs_y, abs_x + w - 1, abs_y + h - 1);

    /* informational item row: lay out like objitemwin (see its draw()), minus
     * everything that only means something for a selectable inventory object
     * - selection frames, item letter, weight, BUC tint and quantity all come
     * from a struct obj, and these rows have none */
    if (tile >= 0) {
        int x = abs_x, y = abs_y;
        int tile_x = x + h / 2, tile_y = y + h * 3 / 4;
        int icon = tile;

        if (background)
            vulture_put_img(x, y, background);

        vulture_draw_lowered_frame(x, y, x + w - 1, y + h - 1);
        vulture_draw_raised_frame(x + V_UI(1), y + V_UI(1), x + w - V_UI(2),
                                  y + h - V_UI(2));

        vulture_put_text_multiline(V_FONT_MENU, caption, vulture_screen, x + h,
                                   y + V_UI(3), CLR32_WHITE, CLR32_BLACK,
                                   w - h - V_UI(6));

        vulture_set_draw_region(x + V_UI(2), y + V_UI(2), x + h - V_UI(3),
                                y + h - V_UI(3));
        vulture_fill_rect(x + V_UI(2), y + V_UI(2), x + h - V_UI(3),
                          y + h - V_UI(3), CLR32_BLACK_A30);

        /* same small-icon substitution objitemwin makes */
        if (TILE_IS_OBJECT(icon)) {
            icon = icon - OBJTILEOFFSET + ICOTILEOFFSET;
            tile_x = x + V_UI(2);
            tile_y = y + V_UI(2);
        }
        vulture_put_tile_ui(tile_x, tile_y, icon);

        vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                                vulture_screen->h - 1);
        vulture_invalidate_region(x, y, w, h);
        return 0;
    }

    /* These insets must match objitemwin's, which are also V_UI()-scaled:
     * header cells and item cells are interleaved in the SAME
     * V_LISTITEM_WIDTH x V_LISTITEM_HEIGHT grid, so at scale > 1 an unscaled
     * header frame no longer lines up with the item frames around it. */
    vulture_fill_rect(abs_x + V_UI(2), abs_y + V_UI(2), abs_x + w - V_UI(3),
                      abs_y + h - V_UI(3), CLR32_BLACK_A50);

    /* Outer edge */
    vulture_draw_lowered_frame(abs_x, abs_y, abs_x + w - 1, abs_y + h - 1);
    /* Inner edge */
    vulture_draw_raised_frame(abs_x + V_UI(1), abs_y + V_UI(1),
                              abs_x + w - V_UI(2), abs_y + h - V_UI(2));

    /* draw the text centered in the window */
    int txt_width = vulture_text_length(V_FONT_MENU, caption);
    int txt_height = vulture_text_height(V_FONT_MENU, caption);
    int text_start_x = abs_x + (w - txt_width) / 2;
    int text_start_y = abs_y + (h - txt_height) / 2;

    vulture_put_text_shadow(V_FONT_MENU, caption, vulture_screen,
                            text_start_x, text_start_y, CLR32_WHITE,
                            CLR32_BLACK);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    /* lift drawing region restriction */
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    return 0;
}
