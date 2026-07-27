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
    autobg = true;

    /* the text metrics already scale (the font is opened at vulture_ui_scale);
     * the padding around them has to scale with it or buttons get cramped */
    w = vulture_text_length(V_FONT_MENU, caption) + V_UI(14);
    h = vulture_text_height(V_FONT_MENU, caption) + V_UI(10);
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

    if (background)
        /* re-draw background: if the button just became un-pressed
         * we get messed up graphics otherwise */
        vulture_put_img(x, y, background);

    int text_start_x, text_start_y;

    /* Black edge (drawn scale-deep: see vulture_draw_raised_frame) */
    for (int i = 0; i < vulture_ui_scale; i++)
        vulture_rect(x + 1 + i, y + 1 + i, x + w - 2 - i, y + h - 2 - i,
                     V_COLOR_BACKGROUND);

    /* Outer edge (lowered) */
    vulture_draw_lowered_frame(x, y, x + w - 1, y + h - 1);
    /* Inner edge (raised) - inset past the outer frame AND the black edge,
     * both of which are now vulture_ui_scale thick */
    vulture_draw_raised_frame(x + V_UI(2), y + V_UI(2), x + w - V_UI(3),
                              y + h - V_UI(3));

    if (!caption.empty()) {
        text_start_x =
            x + (w - vulture_text_length(V_FONT_BUTTON, caption)) / 2;
        text_start_y = y + V_UI(5);

        vulture_put_text_shadow(V_FONT_BUTTON, caption, vulture_screen,
                                text_start_x, text_start_y, V_COLOR_TEXT,
                                V_COLOR_BACKGROUND);
    } else if (image) {
        vulture_put_img(x + (w - image->w) / 2, y + (h - image->h) / 2,
                        image);
    }

    if (selected) {
        /* shift the *entire* image of the button (including borders)
         * 2px left and down - scaled, so the pressed-in nudge stays a 2px
         * APPARENT nudge rather than shrinking to 1px at scale 2 */
        SDL_Surface *buttonimage =
            vulture_get_img(x, y, x + w - V_UI(3), y + h - V_UI(3));

        vulture_fill_rect(x, y, x + w - 1, y + V_UI(1), CLR32_BLACK);
        vulture_fill_rect(x, y, x + V_UI(1), y + h - 1, CLR32_BLACK);
        vulture_put_img(x + V_UI(2), y + V_UI(2), buttonimage);

        SDL_FreeSurface(buttonimage);
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
        if (selected)
            need_redraw = 1;
        selected = 0;
    }

    if (need_redraw)
        return V_EVENT_UNHANDLED_REDRAW;

    return V_EVENT_UNHANDLED;
}
