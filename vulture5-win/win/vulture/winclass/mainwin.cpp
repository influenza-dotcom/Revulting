/* NetHack may be freely redistributed.  See license for details. */

#include "mainwin.h"

#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

mainwin::mainwin(window *p) : window(p)
{
    border_right = vulture_winelem.border_right->w;
    border_left = vulture_winelem.border_left->w;
    border_top = vulture_winelem.border_top->h;
    border_bottom = vulture_winelem.border_bottom->h;
    autobg = true;
}

bool
mainwin::draw()
{
    int x = this->abs_x;
    int y = this->abs_y;
    int pos_x, pos_y;

    /* s200: the modern panel (it was tiled stone in carved borders).  The
     * borders' sizes stay as its padding, so every layout is unchanged. */
    vulture_ui_panel(x, y, w, h);

    /* draw title: gold, with a thin gold rule under it */
    pos_x = this->abs_x + border_left;
    pos_y = this->abs_y + border_top;

    if (!caption.empty()) {
        int th = vulture_text_height(V_FONT_HEADLINE, caption);

        vulture_put_text_shadow(V_FONT_HEADLINE, caption, vulture_screen,
                                pos_x, pos_y, vulture_ui_rgb(255, 205, 90),
                                V_COLOR_BACKGROUND);
        vulture_ui_fill(pos_x, pos_y + th + V_UI(3), w - border_left - border_right,
                        V_UI(1), 255, 200, 70, 90);
    }

    vulture_invalidate_region(x, y, w, h);

    return true;
}

int
mainwin::get_frameheight()
{
    int capheight = 0;
    if (!caption.empty())
        capheight = vulture_text_height(V_FONT_HEADLINE, caption);
    return border_top + capheight + border_bottom;
}

void
mainwin::layout()
{
    window *winelem;
    int btncount, btn_maxwidth, btn_totalwidth, pos_x, capheight;
    int max_y, min_y, offset_top;
    int max_x = vulture_text_length(V_FONT_HEADLINE, caption);
    /* text metrics scale with the font; the gaps around them must too */
    int buttonheight = vulture_get_lineheight(V_FONT_MENU) + V_UI(15);
    const int btn_gap = V_UI(10);

    capheight = 0;
    if (!caption.empty())
        capheight = vulture_text_height(V_FONT_HEADLINE, caption) * 2;

    btncount = btn_totalwidth = btn_maxwidth = pos_x = 0;
    max_y = capheight;
    min_y = 9999;

    for (winelem = first_child; winelem != NULL;
         winelem = winelem->sib_next) {
        if (winelem->get_wintype() == V_WINTYPE_BUTTON) {
            btncount++;
            btn_maxwidth = max(btn_maxwidth, winelem->w);
        } else {
            max_x = max(winelem->x + winelem->w, max_x);
            max_y = max(winelem->y + winelem->h, max_y);
            min_y = min(winelem->y, min_y);
        }
    }

    /* if there are any element in the space needed for the headline add extra
     * space at the top of the window */
    offset_top = 0;
    if (min_y < capheight)
        offset_top = capheight;

    /* make the window wide enough to fit all the buttons in */
    btn_totalwidth = (btn_maxwidth + btn_gap) * btncount - btn_gap;
    max_x = max(max_x, btn_totalwidth);
    max_y += offset_top;

    /* assign positions */
    pos_x = max((max_x - btn_totalwidth) / 2, 0);
    for (winelem = first_child; winelem != NULL;
         winelem = winelem->sib_next) {
        if (winelem->get_wintype() == V_WINTYPE_BUTTON) {
            winelem->w = btn_maxwidth;
            winelem->x = pos_x + border_left;
            winelem->y = max_y + border_top;
            pos_x += winelem->w + btn_gap;
        } else {
            winelem->x += border_left;
            winelem->y += border_top + offset_top;
        }
    }

    max_y += buttonheight;
    w = max_x + border_left + border_right;
    h = max_y + border_top + border_bottom;

    /*
     * A dialog can legitimately have no parent: NetHack 5.0.0 calls pline()
     * for the copyright banner in main() before WIN_MESSAGE exists, so
     * vulture_putstr() sees WIN_ERR and raises a messagebox while ROOTWIN is
     * still NULL.  Centre on the screen in that case instead of faulting.
     */
    if (parent) {
        x = (parent->w - w) / 2;
        y = (parent->h - h) / 2;
        abs_x = parent->abs_x + x;
        abs_y = parent->abs_y + y;
    } else {
        int sw = vulture_screen ? vulture_screen->w : w;
        int sh = vulture_screen ? vulture_screen->h : h;

        x = max((sw - w) / 2, 0);
        y = max((sh - h) / 2, 0);
        abs_x = x;
        abs_y = y;
    }
}
