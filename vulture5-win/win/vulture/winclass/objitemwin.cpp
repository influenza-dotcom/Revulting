/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gra.h"
#include "vulture_mou.h"
#include "vulture_sdl.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "menuwin.h"
#include "objitemwin.h"

objitemwin::objitemwin(window *p, menuitem *mi, std::string cap, char accel,
                       char group_accel, int glyph, bool selected,
                       bool multiselect)
    : optionwin(p, mi, cap, accel, group_accel, glyph, selected, multiselect)
{
    v_type = V_WINTYPE_OBJITEM;
    last_toggled = false;
    hover = false;
    autobg = true;
    w = V_LISTITEM_WIDTH;
    h = V_LISTITEM_HEIGHT;
    obj = NULL;
}

bool
objitemwin::draw()
{
    char tmpstr[32];
    int text_start_x, text_start_y, txt_height;
    int x = abs_x;
    int y = abs_y;
    int weight = 0;
    Uint32 textcolor;
    int fr, fg, fb, br, bg, bb;

    vulture_set_draw_region(x, y, x + w - 1, y + h - 1);

    /* re-set the background to prevent shadings from stacking repatedly until
     * they become solid */
    if (background)
        vulture_put_img(x, y, background);

    /* s200: a flat card (it was an etched bevel with a blue / orange frame):
     * lighter with a gold edge under the pointer, gold-tinted when selected,
     * a white edge on the one toggled last */
    if (item->selected)
        fr = 66, fg = 52, fb = 24, br = 255, bg = 175, bb = 60;
    else if (hover)
        fr = 46, fg = 40, fb = 33, br = 255, bg = 200, bb = 70;
    else
        fr = 28, fg = 25, fb = 24, br = 64, bg = 58, bb = 52;
    vulture_ui_fill(x, y, w, h, fr, fg, fb, 255);
    vulture_ui_border(x, y, w, h, br, bg, bb);
    if (hover || item->selected)
        vulture_ui_fill(x, y, V_UI(3), h, br, bg, bb, 255);
    if (last_toggled)
        vulture_ui_border(x, y, w, h, 255, 255, 255);

    /* use a different text color for worn objects */
    if (obj && obj->owornmask)
        textcolor = vulture_ui_rgb(150, 232, 150);
    else
        textcolor = vulture_ui_rgb(240, 235, 225);

    /* draw text, leaving a h by h square on the left free for the object tile
     */
    /* line 1 and if necessary line 2 contain the item description */
    vulture_put_text_multiline(V_FONT_MENU, caption, vulture_screen, x + h + V_UI(2),
                               y + V_UI(3), textcolor, CLR32_BLACK,
                               w - h - V_UI(8));

    if (obj) {
        weight = obj->owt;
    }

    /* weight is in line 3, dim */
    txt_height = vulture_text_height(V_FONT_MENU, caption);
    text_start_y = y + txt_height * 2 + V_UI(4);

    tmpstr[0] = '\0';
    if (weight)
        snprintf(tmpstr, 32, "weight %d", weight);
    vulture_put_text_shadow(V_FONT_MENU, tmpstr, vulture_screen, x + h + V_UI(2),
                            text_start_y, vulture_ui_rgb(150, 145, 135), CLR32_BLACK);

    if (item->selected) {
        tmpstr[0] = '\0';
        if (item->count <= 0 || (obj && item->count > obj->quan))
            snprintf(tmpstr, 32, "selected (all)");
        else
            snprintf(tmpstr, 32, "selected (%d)", item->count);
        text_start_x = x + w - vulture_text_length(V_FONT_MENU, tmpstr) - V_UI(6);
        vulture_put_text_shadow(V_FONT_MENU, tmpstr, vulture_screen,
                                text_start_x, text_start_y, vulture_ui_rgb(255, 205, 90),
                                CLR32_BLACK);
    }

    /* the tile's well: constrained, so large tiles don't overlap */
    vulture_set_draw_region(x + V_UI(2), y + V_UI(2), x + h - V_UI(3),
                            y + h - V_UI(3));
    vulture_ui_fill(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 12, 11, 12, 255);

    /* blessed / cursed, as far as you know it: a tint and an edge */
    if (obj && obj->bknown && obj->blessed) {
        vulture_ui_fill(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 80, 140, 255, 60);
        vulture_ui_border(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 90, 150, 255);
    }
    if (obj && obj->bknown && obj->cursed) {
        vulture_ui_fill(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 230, 60, 50, 60);
        vulture_ui_border(x + V_UI(2), y + V_UI(2), h - V_UI(4), h - V_UI(4), 230, 70, 60);
    }

    /* draw the object tile.
     *
     * This is game art, but it lives INSIDE the UI: the cell around it is laid
     * out in UI space (V_LISTITEM_WIDTH/HEIGHT), so the icon scales with the
     * cell.  vulture_put_tile_ui() is the only tile call in the port that
     * compensates; the level map keeps calling vulture_put_tile() and stays
     * native.  See vulture_types.h. */
    if (obj) {
        int tile_x, tile_y;
        int tile = 0;

        tile = vulture_object_to_tile(obj->otyp, -1, -1, obj);

        tile_x = x + h / 2;
        tile_y = y + h * 3 / 4;

        if (TILE_IS_OBJECT(tile)) {
            tile = tile - OBJTILEOFFSET + ICOTILEOFFSET;
            tile_x = x + V_UI(2);
            tile_y = y + V_UI(2);
        }

        vulture_put_tile_ui(tile_x, tile_y, tile);
    }

    /* draw the item letter on the top left corner of the object tile */
    snprintf(tmpstr, 11, "%c", accelerator);
    vulture_put_text_shadow(V_FONT_MENU, tmpstr, vulture_screen, x + V_UI(2),
                            y + V_UI(2), textcolor, CLR32_BLACK);

    /* draw the quantity on the tile */
    if (obj && obj->quan > 1) {
        snprintf(tmpstr, 11, "%ld", obj->quan);
        txt_height = vulture_text_height(V_FONT_MENU, tmpstr);
        text_start_x =
            x + h - vulture_text_length(V_FONT_MENU, tmpstr) - V_UI(2);
        vulture_put_text_shadow(V_FONT_MENU, tmpstr, vulture_screen,
                                text_start_x, y + h - txt_height, CLR32_WHITE,
                                CLR32_BLACK);
    }

    /* restore the drawing region */
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    vulture_invalidate_region(x, y, w, h);

    return 0;
}

eventresult
objitemwin::handle_mousemotion_event(window *target, void *result, int xrel,
                                     int yrel, int state)
{
    bool prevhover;

    vulture_set_mcursor(V_CURSOR_NORMAL);
    prevhover = hover;
    hover = true;

    if (hover != prevhover) {
        need_redraw = true;
        return V_EVENT_HANDLED_REDRAW;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

/* s139: hover an item for what it does and how it compares */
std::string vulture_item_info(struct obj *o); /* levelwin.cpp */

eventresult
objitemwin::handle_timer_event(window *target, void *result, int time)
{
    if (time > HOVERTIMEOUT && obj) {
        std::string s = vulture_item_info(obj);

        if (!s.empty())
            vulture_mouse_set_tooltip(s);
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
objitemwin::handle_other_event(window *target, void *result, SDL_Event *event)
{
    bool prevhover;

    if (event->type == SDL_MOUSEMOVEOUT) {
        prevhover = hover;
        hover = false;

        if (hover != prevhover) {
            need_redraw = true;
            return V_EVENT_HANDLED_REDRAW;
        }
        return V_EVENT_HANDLED_NOREDRAW;
    }

    return V_EVENT_UNHANDLED;
}

eventresult
objitemwin::handle_mousebuttonup_event(window *target, void *result,
                                       int mouse_x, int mouse_y, int button,
                                       int state)
{
    if (button == SDL_BUTTON_WHEELUP || button == SDL_BUTTON_WHEELDOWN)
        hover = false;
    return V_EVENT_UNHANDLED;
}
