/* NetHack may be freely redistributed.  See license for details. */

#include "optionwin.h"
#include "menuwin.h"

#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_txt.h"
#include "vulture_win.h"
#include "vulture_mou.h"

optionwin::optionwin(window *p, menuitem *mi, std::string cap, char accel,
                     char group_accel, int glyph, bool selected,
                     bool is_checkbox)
    : window(p), glyph(glyph), item(mi), is_checkbox(is_checkbox)
{
    v_type = V_WINTYPE_OPTION;
    accelerator = accel;
    group_accelerator = group_accel;

    caption = cap;
    menu_id_v = (void *) mi->identifier;
}

bool
optionwin::draw()
{
    int bw = vulture_winelem.radiobutton_off->w, bhgt = vulture_winelem.radiobutton_off->h;
    int bs = (bhgt < bw ? bhgt : bw) * 3 / 4, bx, by, in;
    point mp = vulture_get_mouse_pos();
    bool hov = mp.x >= abs_x && mp.x < abs_x + w && mp.y >= abs_y && mp.y < abs_y + h;

    /* s200: the row under the pointer is lit, and the stone radio button /
     * checkbox is a drawn box - filled gold when chosen (orange with a
     * count) */
    if (hov)
        vulture_ui_fill(abs_x - V_UI(3), abs_y, w + V_UI(6), h, 255, 200, 70, 30);
    if (bs < V_UI(8))
        bs = V_UI(8);
    bx = abs_x + (bw - bs) / 2;
    by = abs_y + (h - bs) / 2;
    vulture_ui_fill(bx, by, bs, bs, 18, 16, 16, 255);
    if (item->selected) {
        in = bs / 4 > V_UI(2) ? bs / 4 : V_UI(2);
        vulture_ui_border(bx, by, bs, bs, 255, 200, 70);
        if (is_checkbox && item->count > 0)
            vulture_ui_fill(bx + in, by + in, bs - 2 * in, bs - 2 * in, 255, 140, 50, 255);
        else
            vulture_ui_fill(bx + in, by + in, bs - 2 * in, bs - 2 * in, 255, 200, 70, 255);
    } else
        vulture_ui_border(bx, by, bs, bs, hov ? 200 : 120, hov ? 170 : 108, hov ? 100 : 90);

    /* draw the option description */
    vulture_put_text_shadow(V_FONT_MENU, caption, vulture_screen,
                            abs_x + bw + V_UI(4), abs_y + V_UI(2),
                            hov ? vulture_ui_rgb(255, 240, 205) : vulture_ui_rgb(240, 235, 225),
                            V_COLOR_BACKGROUND);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    return false;
}
