/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gfl.h"
#include "vulture_gra.h"
#include "vulture_mou.h"
#include "vulture_opt.h"
#include "vulture_sdl.h"
#include "vulture_tile.h"
#include "vulture_win.h"

#include "contextmenu.h"
#include "hotspot.h"
#include "levelwin.h"
#include "map.h"
#include "messagewin.h"
#include "toolbar.h"

toolbar::toolbar(window *p, int menuid, bool visible, int x, int y,
                 std::string imgfile, const tb_buttondesc buttons[5])
    : window(p)
{
    /* UI chrome: loaded at vulture_ui_scale (see vulture_types.h), so the
     * button cells below have to be scaled by the same factor to keep landing
     * on the artwork they belong to. */
    this->bgimage = vulture_load_graphic_ui(imgfile);
    this->bg_scale = vulture_ui_scale;
    this->imgfile = imgfile;
    this->w = bgimage->w;
    this->h = bgimage->h;
    this->x = x;
    this->y = y;
    this->visible = visible;
    this->menu_id = menuid;

    /* Create buttons */
    new hotspot(this, V_UI(4), 0, V_UI(38), V_UI(39), buttons[0].menu_id, buttons[0].name);
    new hotspot(this, V_UI(44), 0, V_UI(38), V_UI(39), buttons[1].menu_id, buttons[1].name);
    new hotspot(this, V_UI(84), 0, V_UI(38), V_UI(39), buttons[2].menu_id, buttons[2].name);
    new hotspot(this, V_UI(124), 0, V_UI(38), V_UI(39), buttons[3].menu_id, buttons[3].name);
    new hotspot(this, V_UI(164), 0, V_UI(38), V_UI(39), buttons[4].menu_id, buttons[4].name);
    new hotspot(this, V_UI(184), 0, V_UI(38), V_UI(39), buttons[5].menu_id, buttons[5].name);
}

toolbar::~toolbar()
{
    SDL_FreeSurface(bgimage);
}

bool
toolbar::draw()
{
    vulture_set_draw_region(abs_x, abs_y, abs_x + w - 1, abs_y + h - 1);
    vulture_put_img(abs_x, abs_y, bgimage);
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    return true;
}

eventresult
toolbar::handle_timer_event(window *target, void *result, int time)
{
    if (time > HOVERTIMEOUT)
        if (target != this && !target->caption.empty())
            vulture_mouse_set_tooltip(target->caption);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
toolbar::handle_mousemotion_event(window *target, void *result, int xrel,
                                  int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
toolbar::handle_mousebuttonup_event(window *target, void *result, int mouse_x,
                                    int mouse_y, int button, int state)
{
    /* throw away uninteresting clicks */
    if (button != SDL_BUTTON_LEFT || target == this || !target->menu_id)
        return V_EVENT_HANDLED_NOREDRAW;

    /* one of the buttons was clicked */
    switch (target->menu_id) {
    case V_HOTSPOT_BUTTON_GOLD: {
        int action;
        contextmenu *menu = new contextmenu(levwin);

        menu->add_item("Check Wallet", V_ACTION_GOLD_QUERY);
        menu->add_item("Drop Gold", V_ACTION_GOLD_DROP);
        menu->add_item("Throw Gold", V_ACTION_GOLD_THROW);
        if (*u.ushops)
            menu->add_item("Pay shopkeeper", V_ACTION_GOLD_PAY);

        menu->layout();
        vulture_event_dispatcher(&action, V_RESPOND_INT, menu);

        switch (action) {
        case V_ACTION_GOLD_QUERY:
            ((vulture_event *) result)->num = '$';
            break;

        case V_ACTION_GOLD_DROP:
            vulture_eventstack_add('$', -1, -1, V_RESPOND_CHARACTER);
            ((vulture_event *) result)->num = 'd';
            break;

        case V_ACTION_GOLD_THROW:
            vulture_eventstack_add('$', -1, -1, V_RESPOND_CHARACTER);
            ((vulture_event *) result)->num = 't';
            break;

        case V_ACTION_GOLD_PAY:
            ((vulture_event *) result)->num = 'p';
            break;
        }

        delete menu;
    }
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_LOOK:
        vulture_eventstack_add('y', -1, -1, V_RESPOND_CHARACTER);
        ((vulture_event *) result)->num = '/';
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_EXTENDED:
        ((vulture_event *) result)->num = '#';
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_MAP:
        map::toggle();
        return V_EVENT_HANDLED_REDRAW;

    case V_HOTSPOT_BUTTON_SPELLBOOK:
        ((vulture_event *) result)->num = 'Z';
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_INVENTORY:
        ((vulture_event *) result)->num = 'i';
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_DISCOVERIES:
        ((vulture_event *) result)->num = '\\';
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_MESSAGES:
        msgwin->view_all();
        break;

    case V_HOTSPOT_BUTTON_OPTIONS:
        ((vulture_event *) result)->num = 'O';
        return V_EVENT_HANDLED_FINAL;

    case V_HOTSPOT_BUTTON_IFOPTIONS:
        vulture_iface_opts();
        break;

    case V_HOTSPOT_BUTTON_HELP:
        ((vulture_event *) result)->num = '?';
        return V_EVENT_HANDLED_FINAL;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
toolbar::handle_resize_event(window *target, void *result, int res_w,
                             int res_h)
{
    /* a runtime render-scale change (F8 / Interface Options) arrives here as a
     * resize; rebuild the art and the button cells at the new factor first */
    if (bg_scale != vulture_ui_scale) {
        SDL_Surface *newbg = vulture_load_graphic_ui(imgfile);

        if (newbg) {
            window *btn;
            int i = 0;
            static const int btn_x[6] = { 4, 44, 84, 124, 164, 184 };

            SDL_FreeSurface(bgimage);
            bgimage = newbg;
            bg_scale = vulture_ui_scale;
            w = bgimage->w;
            h = bgimage->h;

            for (btn = first_child; btn && i < 6; btn = btn->sib_next, i++) {
                btn->x = V_UI(btn_x[i]);
                btn->y = 0;
                btn->w = V_UI(38);
                btn->h = V_UI(39);
            }
        }
    }

    x = parent->w - (w + V_UI(6));
    if (menu_id == V_WIN_TOOLBAR1)
        y = parent->h - (h * 2 + V_UI(8));
    else
        y = parent->h - (h + V_UI(6));

    return V_EVENT_HANDLED_NOREDRAW;
}
