/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gen.h"
#include "vulture_gra.h"
#include "vulture_mou.h"
#include "vulture_sdl.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "dirdialog.h"
#include "hotspot.h"
#include "map.h"

/* for defs of V_MAP_XMOD and V_MAP_YMOD */
#include "levelwin.h"

extern void vulture_aim_set(bool on, int x, int y); /* s125: levelwin.cpp */
extern char vulture_aim_key(int tx, int ty);
extern bool vulture_aim_ok(void);
extern "C" void vulture_sfx(const char *); /* s131 */

dirdialog::dirdialog(window *p, std::string ques) : mainwin(p)
{
    int arrows_w, arrows_h;

    caption = ques;

    arrows_w = vulture_winelem.direction_arrows->w;
    arrows_h = vulture_winelem.direction_arrows->h;

    /* calculate window layout */
    if (!ques.empty())
        w = vulture_text_length(V_FONT_HEADLINE, ques);
    w = (w > arrows_w) ? w : arrows_w;
    w += border_left + border_right;

    arroffset_y = border_top + vulture_get_lineheight(V_FONT_HEADLINE) * 1.5;
    arroffset_x = (w - arrows_w) / 2;

    h = arroffset_y + arrows_h + border_bottom;

    if (parent) {
        x = (parent->get_w() - w) / 2;
        y = (parent->get_h() - h) / 2;
        abs_x = parent->abs_x + x;
        abs_y = parent->abs_y + y;
    } else {
        x = (vulture_screen->w - w) / 2;
        y = (vulture_screen->h - h) / 2;
        abs_x = x;
        abs_y = y;
    }

    arr = new hotspot(this, arroffset_x, arroffset_y, arrows_w, arrows_h, 0,
                      "");

    /* s125: during play, aim with the mouse instead - this dialog becomes
       a transparent layer over the whole screen (so it gets every click)
       with a banner at the top, and the map draws the aim line */
    aim = false;
    if (vulture_aim_ok() && parent) {
        point mp = vulture_get_mouse_pos(), sq;

        aim = true;
        x = y = 0;
        w = parent->get_w(), h = parent->get_h();
        abs_x = parent->abs_x, abs_y = parent->abs_y;
        arr->visible = false;
        sq = levwin->mouse_to_map(mp);
        vulture_aim_set(true, sq.x, sq.y);
        levwin->force_redraw();
        vulture_sfx("aim_ready"); /* s131 */
    }
}

dirdialog::~dirdialog()
{
    if (aim) {
        vulture_aim_set(false, -1, -1);
        if (levwin)
            levwin->force_redraw();
    }
}


eventresult
dirdialog::handle_mousemotion_event(window *target, void *result, int xrel,
                                    int yrel, int state)
{
    if (aim && levwin) { /* s125: move the aim line with the pointer */
        point sq = levwin->mouse_to_map(vulture_get_mouse_pos());

        vulture_set_mcursor(V_CURSOR_TARGET_RED);
        vulture_aim_set(true, sq.x, sq.y);
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
        return V_EVENT_HANDLED_NOREDRAW;
    }
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
dirdialog::handle_mousebuttonup_event(window *target, void *result,
                                      int mouse_x, int mouse_y, int button,
                                      int state)
{
    point mouse;
    int dir_x, dir_y;
    char choice = 0;
    const char dirkeys[3][3] = { { '8', '9', '6' },
                                 { '7', '.', '3' },
                                 { '4', '1', '2' } };
    /*
     * The arrow art is a 3x3 diamond grid of V_MAP_XMOD x V_MAP_YMOD
     * half-cells, and vulture_winelem.direction_arrows was upscaled by
     * vulture_ui_scale when it was loaded (it is UI chrome, not map).  The
     * hotspot we are hit-testing therefore has scaled dimensions, so the cell
     * size this inverse divides by must be scaled too.  Using the raw
     * V_MAP_XMOD/V_MAP_YMOD here - which are MAP constants and deliberately
     * never scaled - divided render-space clicks by native-space cells, so at
     * scale 2 a click on a visually adjacent arrow produced dir_x/dir_y of
     * +-2: outside the -1..1 grid, and liable to be mistaken for the '<' / '>'
     * corner gestures below.  At scale 1 these are exactly V_MAP_XMOD and
     * V_MAP_YMOD, so this is a no-op there.
     */
    const int xmod = V_UI(V_MAP_XMOD);
    const int ymod = V_UI(V_MAP_YMOD);

    if (aim && levwin) { /* s125: click fires toward the pointer */
        point sq;

        if (button == SDL_BUTTON_RIGHT) {
            vulture_sfx("ui_cancel"); /* s131 */
            *(char *) result = -1; /* cancel, like Esc */
            return V_EVENT_HANDLED_FINAL;
        }
        if (button != SDL_BUTTON_LEFT)
            return V_EVENT_HANDLED_NOREDRAW;
        sq = levwin->mouse_to_map(vulture_get_mouse_pos());
        choice = vulture_aim_key(sq.x, sq.y);
        if (choice) {
            vulture_sfx("aim_fire"); /* s131 */
            *(char *) result = choice;
            return V_EVENT_HANDLED_FINAL;
        }
        return V_EVENT_HANDLED_NOREDRAW;
    }
    if (button != SDL_BUTTON_LEFT || target != first_child)
        return V_EVENT_HANDLED_NOREDRAW;

    /* get the click coordinates and normalize them to the center of the arrow
     * grid */
    mouse = vulture_get_mouse_pos();
    mouse.x -= (target->abs_x + target->w / 2);
    mouse.y -= (target->abs_y + target->h / 2);

    /* translate the click position to a direction */
    dir_x = ymod * mouse.x + xmod * mouse.y + xmod * ymod;
    dir_x = dir_x / (2 * xmod * ymod) - (dir_x < 0);
    dir_y = -ymod * mouse.x + xmod * mouse.y + xmod * ymod;
    dir_y = dir_y / (2 * xmod * ymod) - (dir_y < 0);

    /* convert the chosen direction to a key */
    choice = 0;
    if (dir_y >= -1 && dir_y <= 1 && dir_x >= -1 && dir_x <= 1)
        choice = vulture_numpad_to_hjkl(dirkeys[dir_y + 1][dir_x + 1], 0);

    if (dir_x >= 2 && mouse.x < target->w / 2 && mouse.y < target->h / 2)
        choice = '>';

    if (dir_x <= -2 && mouse.x > -target->w / 2 && mouse.y > -target->h / 2)
        choice = '<';

    if (choice) {
        if (!mapwin)
            choice = vulture_translate_key(choice); /* includes §62 turn */
        else
            choice = vulture_rotate_dirkey(choice); /* §62: view turned */
        *(char *) result = choice;
        return V_EVENT_HANDLED_FINAL;
    }

    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
dirdialog::handle_keydown_event(window *target, void *result, int sym,
                                int mod, int unicode)
{
    char choice = 0;

    if (sym == SDLK_ESCAPE) {
        choice = -1;
        if (aim)
            vulture_sfx("ui_cancel"); /* s131 */
    }
    else
        choice = vulture_make_nh_key(sym, mod, unicode);

    if (!mapwin)
        choice = vulture_translate_key(choice);
    *(char *) result = choice;
    return V_EVENT_HANDLED_FINAL;
}

eventresult
dirdialog::handle_resize_event(window *target, void *result, int res_w,
                               int res_h)
{
    x = ((parent ? parent->w : vulture_screen->w) - w) / 2;
    y = ((parent ? parent->h : vulture_screen->h) - h) / 2;
    return V_EVENT_HANDLED_NOREDRAW;
}

bool
dirdialog::draw()
{
    if (aim) { /* s125: just a banner; the map shows through */
        std::string hint = caption + "   Click to aim  -  right-click or Esc to cancel";
        int tw = vulture_text_length(V_FONT_HEADLINE, hint) + 40;
        int bx = abs_x + (w - tw) / 2, by = abs_y + V_UI(46);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, tw, vulture_get_lineheight(V_FONT_HEADLINE) + 14,
                                                        32, SDL_PIXELFORMAT_ARGB8888);

        if (s) {
            SDL_Rect d;

            SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 10, 8, 12, 200));
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
            d.x = bx, d.y = by, d.w = s->w, d.h = s->h;
            SDL_BlitSurface(s, NULL, vulture_screen, &d);
            vulture_invalidate_region(bx, by, s->w, s->h);
            SDL_FreeSurface(s);
        }
        vulture_put_text_shadow(V_FONT_HEADLINE, hint, vulture_screen, bx + 20, by + 7,
                                SDL_MapRGB(vulture_screen->format, 255, 215, 120),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        return false;
    }
    mainwin::draw();

    vulture_set_draw_region(arr->abs_x, arr->abs_y, arr->abs_x + arr->w - 1,
                            arr->abs_y + arr->h - 1);
    vulture_put_img(arr->abs_x, arr->abs_y, vulture_winelem.direction_arrows);
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    vulture_invalidate_region(abs_x, abs_y, w, h);

    return false;
}
