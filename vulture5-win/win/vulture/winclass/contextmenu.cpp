/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gra.h"
#include "vulture_mou.h"
#include "vulture_sdl.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "button.h"
#include "contextmenu.h"

contextmenu::contextmenu(window *p) : window(p)
{
    v_type = V_WINTYPE_CONTEXTMENU;
    layout_done = false;
    autobg = true;
}

bool
contextmenu::draw()
{
    int x, y;

    if (!layout_done)
        layout();

    x = abs_x;
    y = abs_y;

    vulture_ui_panel(x, y, w, h); /* s200: was tiled stone, bevelled */

    vulture_invalidate_region(x, y, w, h);

    return true;
}

/* arrange the btns on a dropdown (context menu) */
void
contextmenu::layout(void)
{
    button *btn;
    point mouse;
    int lineheight;
    int btn_maxwidth = 0;

    if (layout_done)
        return;

    mouse = vulture_get_mouse_pos();

    /* every gap here sits next to scaled text, so it scales too */
    h = V_UI(3);
    lineheight = vulture_get_lineheight(V_FONT_BUTTON);

    /* calculate the hight and find the max btn width */
    for (itemlist::iterator i = items.begin(); i != items.end(); i++) {
        btn = new button(this, i->first, i->second, '\0');
        btn->h = lineheight + V_UI(10);
        btn->y = h;
        btn->x = V_UI(3);
        h += btn->h + V_UI(2);

        btn->w = vulture_text_length(V_FONT_BUTTON, btn->caption) + V_UI(11);
        btn_maxwidth = (btn_maxwidth > btn->w) ? btn_maxwidth : btn->w;
    }
    h += V_UI(2);

    /* set the width of every btn to the max btn width */
    for (window *win = first_child; win; win = win->sib_next)
        win->w = btn_maxwidth;

    w = btn_maxwidth + V_UI(6);

    /* set dropdown position */
    if (mouse.x < parent->w - w)
        x = mouse.x;
    else
        x = parent->w - w;

    if (mouse.y < parent->h - h)
        y = mouse.y;
    else
        y = parent->h - h;

    abs_x = x + parent->abs_x;
    abs_y = y + parent->abs_y;

    layout_done = true;
}

eventresult
contextmenu::handle_mousemotion_event(window *target, void *result, int xrel,
                                      int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
contextmenu::handle_mousebuttonup_event(window *target, void *result,
                                        int mouse_x, int mouse_y, int button,
                                        int state)
{
    if (button == SDL_BUTTON_LEFT) {
        if (this != target && target->menu_id) {
            *(int *) result = target->menu_id;
            return V_EVENT_HANDLED_FINAL;
        }
    }

    *(int *) result = 0;
    return V_EVENT_HANDLED_FINAL;
}

eventresult
contextmenu::handle_keydown_event(window *target, void *result,
                                  SDL_Keysym keysym)
{
    *(int *) result = 0;
    return V_EVENT_HANDLED_FINAL;
}

void
contextmenu::add_item(const char *label, int menuid)
{
    items.push_back(std::pair<char *, int>(strdup(label), menuid));
}