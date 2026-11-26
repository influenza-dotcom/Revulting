/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "button.h"

button::button(window *p, std::string caption, int menuid, char accel)
    : window(p)
{
    v_type = V_WINTYPE_BUTTON;

    this->caption = caption;

    menu_id = menuid;
    accelerator = accel;

    image = NULL;
    selected = false;
    hover = false;
    autobg = true;

    /* the text metrics already scale (the font is opened at vulture_ui_scale);
     * the padding around them has to scale with it or buttons get cramped */
    /* s200: sized by the font it is drawn in (V_FONT_BUTTON) */
    w = vulture_text_length(V_FONT_BUTTON, caption) + V_UI(18);
    h = vulture_get_lineheight(V_FONT_BUTTON) + V_UI(10);
}

button::~button()
{
    if (image)
        SDL_FreeSurface(image);
    image = NULL;
}

bool
button::draw()
{
    int x = abs_x;
    int y = abs_y;
    int fr, fg, fb, br, bg, bb;

    if (background)
        /* re-draw background: if the button just became un-pressed
         * we get messed up graphics otherwise */
        vulture_put_img(x, y, background);

    /* s200: a flat button - dark with a thin border, gold under the pointer,
     * deeper when pressed (it was a grey bevel that shifted 2 px) */
    if (selected)
        fr = 82, fg = 62, fb = 24, br = 255, bg = 200, bb = 70;
    else if (hover)
        fr = 56, fg = 46, fb = 34, br = 255, bg = 200, bb = 70;
    else
        fr = 34, fg = 30, fb = 29, br = 104, bg = 90, bb = 66;
    vulture_ui_fill(x, y, w, h, fr, fg, fb, 255);
    vulture_ui_border(x, y, w, h, br, bg, bb);

    if (!caption.empty()) {
        int tx = x + (w - vulture_text_length(V_FONT_BUTTON, caption)) / 2;
        int ty = y + (h - vulture_get_lineheight(V_FONT_BUTTON)) / 2;

        vulture_put_text_shadow(V_FONT_BUTTON, caption, vulture_screen, tx, ty,
                                hover || selected ? vulture_ui_rgb(255, 232, 170)
                                                  : vulture_ui_rgb(240, 235, 225),
                                V_COLOR_BACKGROUND);
    } else if (image) {
        vulture_put_img(x + (w - image->w) / 2, y + (h - image->h) / 2,
                        image);
    }

    vulture_invalidate_region(x, y, w, h);

    return false;
}

eventresult
button::handle_mousebuttonup_event(window *target, void *result, int mouse_x,
                                   int mouse_y, int button, int state)
{
    if (selected)
        need_redraw = 1;
    selected = 0;
    return V_EVENT_UNHANDLED_REDRAW;
}

eventresult
button::handle_other_event(window *target, void *result, SDL_Event *event)
{
    if (this == target && event->type == SDL_MOUSEBUTTONDOWN
        && event->button.button == SDL_BUTTON_LEFT) {
        selected = 1;
        need_redraw = 1;
    } else if (event->type == SDL_MOUSEMOVEOUT) {
        if (selected || hover)
            need_redraw = 1;
        selected = 0;
        hover = false;
    }

    if (need_redraw)
        return V_EVENT_UNHANDLED_REDRAW;

    return V_EVENT_UNHANDLED;
}

/* s200: light up under the pointer */
eventresult
button::handle_mousemotion_event(window *target, void *result, int xrel,
                                 int yrel, int state)
{
    if (target == this && !hover) {
        hover = true;
        need_redraw = 1;
        return V_EVENT_UNHANDLED_REDRAW;
    }
    return V_EVENT_UNHANDLED;
}
