/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_win.h"

#include "hotspot.h"
#include "scrollbar.h"

scrollbar::scrollbar(window *p, int scrolloff) : window(p)
{
    v_type = V_WINTYPE_SCROLLBAR;
    scrollpos = 0;
}

bool
scrollbar::draw()
{
    int pos_y, scroll_pos;
    int scrollarea_top = abs_y + vulture_winelem.scrollbutton_up->h;
    int scrollarea_bottom = abs_y + h - vulture_winelem.scrollbutton_down->h;

    int bw = vulture_winelem.scrollbar->w, uh = vulture_winelem.scrollbutton_up->h;
    int dh = vulture_winelem.scrollbutton_down->h, th = vulture_winelem.scroll_indicator->h;
    int k, cx = abs_x + bw / 2, t = vulture_ui_scale > 0 ? vulture_ui_scale : 1;

    /* s200: flat - a dark track, two small chevron buttons, a gold-edged
     * thumb (it was carved stone).  The art's sizes stay as the geometry, so
     * clicking and dragging work exactly as before. */
    vulture_ui_fill(abs_x, abs_y, bw, h, 20, 18, 18, 230);
    vulture_ui_fill(abs_x, abs_y, bw, uh, 34, 30, 29, 255);
    vulture_ui_border(abs_x, abs_y, bw, uh, 104, 90, 66);
    vulture_ui_fill(abs_x, scrollarea_bottom, bw, dh, 34, 30, 29, 255);
    vulture_ui_border(abs_x, scrollarea_bottom, bw, dh, 104, 90, 66);
    for (k = 0; k < bw / 4; k++) { /* chevrons: up and down */
        vulture_ui_fill(cx - k, abs_y + uh / 2 - bw / 8 + k, 2 * k + 1, t, 240, 225, 190, 255);
        vulture_ui_fill(cx - k, scrollarea_bottom + dh / 2 + bw / 8 - k, 2 * k + 1, t, 240, 225, 190, 255);
    }

    /* for menus that are only _very_ slightly longer than V_MENU_MAXHEIGHT
     * failing to consider spacing between menu elements will lead to
     * scrollpos values that are substantially larger than 8192 (100%). That's
     * OK for the main menu area (desired even), but not for the scroll
     * indicator */
    scroll_pos = (scrollpos <= 8192) ? scrollpos : 8192;

    pos_y = scrollarea_top
            + ((scrollarea_bottom - scrollarea_top - th) * scroll_pos) / 8192.0;
    vulture_ui_fill(abs_x + V_UI(2), pos_y, bw - V_UI(4), th, 110, 95, 70, 255);
    vulture_ui_border(abs_x + V_UI(2), pos_y, bw - V_UI(4), th, 255, 200, 70);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    return false;
}
