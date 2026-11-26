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

        /* s200: the same flat card as objitemwin */
        vulture_ui_fill(x, y, w, h, 28, 25, 24, 255);
        vulture_ui_border(x, y, w, h, 64, 58, 52);

        vulture_put_text_multiline(V_FONT_MENU, caption, vulture_screen, x + h + V_UI(2),
                                   y + V_UI(3), vulture_ui_rgb(240, 235, 225), CLR32_BLACK,
                                   w - h - V_UI(8));

        vulture_set_draw_region(x + V_UI(2), y + V_UI(2), x + h - V_UI(3),
                                y + h - V_UI(3));
        vulture_ui_fill(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 12, 11, 12, 255);

        /* Blessed/cursed wash, matching objitemwin.  There is no struct obj
         * to read bknown/blessed/cursed from, so take it from the row text
         * the core already showed the player: a whole word "blessed" or
         * "cursed" ("uncursed" is a different word and stays unwashed).
         * It can never reveal more than the text itself says. */
        {
            bool blessed = false, cursed = false;
            size_t pos = 0;

            while (pos < caption.size()) {
                size_t end = caption.find(' ', pos);
                if (end == std::string::npos)
                    end = caption.size();
                std::string word = caption.substr(pos, end - pos);
                if (word == "blessed")
                    blessed = true;
                else if (word == "cursed")
                    cursed = true;
                pos = end + 1;
            }

            if (blessed) {
                vulture_ui_fill(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 80, 140, 255, 60);
                vulture_ui_border(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 90, 150, 255);
            } else if (cursed) {
                vulture_ui_fill(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 230, 60, 50, 60);
                vulture_ui_border(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 230, 70, 60);
            }
        }

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
    /* s200: a category heading: a darker card, its name in gold */
    vulture_ui_fill(abs_x, abs_y, w, h, 18, 16, 15, 255);
    vulture_ui_border(abs_x, abs_y, w, h, 86, 72, 48);

    /* draw the text centered in the window */
    int txt_width = vulture_text_length(V_FONT_MENU, caption);
    int txt_height = vulture_text_height(V_FONT_MENU, caption);
    int text_start_x = abs_x + (w - txt_width) / 2;
    int text_start_y = abs_y + (h - txt_height) / 2;

    vulture_put_text_shadow(V_FONT_MENU, caption, vulture_screen,
                            text_start_x, text_start_y, vulture_ui_rgb(255, 205, 90),
                            CLR32_BLACK);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    /* lift drawing region restriction */
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    return 0;
}
