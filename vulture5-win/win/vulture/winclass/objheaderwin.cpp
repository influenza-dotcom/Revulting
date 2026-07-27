/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "objheaderwin.h"

objheaderwin::objheaderwin(window *parent, std::string cap) : window(parent)
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
