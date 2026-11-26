/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

extern "C" {
#include "hack.h"

extern const char *hu_stat[];        /* defined in eat.c */
extern const char *const enc_stat[]; /* defined in botl.c */
}

#include "vulture_gfl.h"
#include "vulture_gra.h"
#include "vulture_mou.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "enhancebutton.h"
#include "statuswin.h"
#include "textwin.h"
#include "mapdata.h" /* s110: portrait */

#include <sstream>

statuswin *stwin;
/* s199: the panel itself is drawn by levelwin.cpp, in the HUD's style */
extern void vulture_status_panel_size(int *w, int *h);
extern void vulture_status_panel_draw(int x, int y, int w, int h);
extern void vulture_hud_frame(int x, int y, int w, int h);
#define V_STATUS_CONDS 7 /* condition cells; see add_cond() (§45) */
static const int status_xpos[5] = { 0, 60, 100, 180, 250 };
static Uint32 warn_colors[V_MAX_WARN];

/*
 * IMPLEMENTATION_LOG s110: Dota-style hero portrait left of the status
 * panel.  The hero's current map sprite (so polymorph shows), cropped to
 * head-and-shoulders (the top ~55% of its opaque pixels), bilinear-scaled
 * into a frame cut from the status panel's own stone art, with thin HP
 * and power bars along the bottom.  Rebuilt only when the sprite or the
 * size changes.
 */
static SDL_Surface *vpt_cache = NULL;
static int vpt_tile = -1, vpt_size = 0;

/* bilinear sample of ARGB surface s at (fx, fy) */
static Uint32
vpt_sample(SDL_Surface *s, double fx, double fy)
{
    int x0, y0, x1, y1, wx, wy;
    Uint32 p00, p01, p10, p11, out = 0;
    int sh;

    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    x0 = (int) fx, y0 = (int) fy;
    if (x0 >= s->w) x0 = s->w - 1;
    if (y0 >= s->h) y0 = s->h - 1;
    x1 = x0 + 1 < s->w ? x0 + 1 : x0;
    y1 = y0 + 1 < s->h ? y0 + 1 : y0;
    wx = (int) ((fx - x0) * 256), wy = (int) ((fy - y0) * 256);
    p00 = ((Uint32 *) ((Uint8 *) s->pixels + y0 * s->pitch))[x0];
    p01 = ((Uint32 *) ((Uint8 *) s->pixels + y0 * s->pitch))[x1];
    p10 = ((Uint32 *) ((Uint8 *) s->pixels + y1 * s->pitch))[x0];
    p11 = ((Uint32 *) ((Uint8 *) s->pixels + y1 * s->pitch))[x1];
    for (sh = 0; sh < 32; sh += 8) {
        int a = (p00 >> sh) & 255, b = (p01 >> sh) & 255;
        int c = (p10 >> sh) & 255, d = (p11 >> sh) & 255;
        int top = a * (256 - wx) + b * wx, bot = c * (256 - wx) + d * wx;
        int v = (top * (256 - wy) + bot * wy) >> 16;

        out |= (Uint32) (v > 255 ? 255 : v) << sh;
    }
    return out;
}

static SDL_Surface *
vpt_build(int tile_id, int size)
{
    vulture_tile *t = vulture_get_tile(tile_id);
    SDL_Surface *src, *dst;
    int x, y, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1, cw, ch, side;
    double sc, ox, oy;

    if (!t || !t->graphic || size < 8)
        return NULL;
    src = SDL_ConvertSurfaceFormat(t->graphic, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!src)
        return NULL;
    SDL_LockSurface(src);
    for (y = 0; y < src->h; y++)
        for (x = 0; x < src->w; x++) {
            Uint32 p = ((Uint32 *) ((Uint8 *) src->pixels + y * src->pitch))[x];
            Uint8 r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255, a = p >> 24;

            if (a < 160)
                continue;
            if (abs(r - g) < 6 && abs(g - b) < 6 && r < 110)
                continue; /* the grey drop shadow */
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    if (x1 < 0) {
        SDL_UnlockSurface(src);
        SDL_FreeSurface(src);
        return NULL;
    }
    /* head and shoulders: the top 55% of the figure, squared up */
    ch = (int) ((y1 - y0 + 1) * 0.55);
    cw = x1 - x0 + 1;
    side = ch > cw ? ch : cw;
    if (side < 8)
        side = 8;
    ox = (x0 + x1) / 2.0 - side / 2.0;
    oy = y0 - side * 0.04;
    sc = (double) side / size;
    dst = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (dst) {
        SDL_LockSurface(dst);
        for (y = 0; y < size; y++)
            for (x = 0; x < size; x++) {
                double fx = ox + (x + 0.5) * sc - 0.5, fy = oy + (y + 0.5) * sc - 0.5;
                Uint32 p = 0;

                if (fx >= -0.5 && fy >= -0.5 && fx < src->w && fy < src->h)
                    p = vpt_sample(src, fx, fy);
                ((Uint32 *) ((Uint8 *) dst->pixels + y * dst->pitch))[x] = p;
            }
        SDL_UnlockSurface(dst);
        SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_BLEND);
    }
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    return dst;
}

/* the portrait box: (px, py) top-left, side `ps` */
static void
vpt_draw(SDL_Surface *bg, int px, int py, int ps)
{
    int tile = (map_data && isok(u.ux, u.uy))
                   ? map_data->get_glyph(MAP_MON, u.ux, u.uy) : V_TILE_NONE;
    int inset = V_UI(4), inner = ps - 2 * inset;
    SDL_Rect d;

    /* frame: the HUD's dark box (s199: was the stone art), a dark inner well;
       s199: no bars any more - the status panel has proper ones */
    nhUse(bg);
    vulture_hud_frame(px, py, ps, ps);
    d.x = px + inset, d.y = py + inset, d.w = inner, d.h = inner;
    SDL_FillRect(vulture_screen, &d, SDL_MapRGB(vulture_screen->format, 18, 16, 20));

    if (tile != V_TILE_NONE && (tile != vpt_tile || inner != vpt_size || !vpt_cache)) {
        if (vpt_cache)
            SDL_FreeSurface(vpt_cache);
        vpt_cache = vpt_build(tile, inner);
        vpt_tile = tile, vpt_size = inner;
    }
    if (tile != V_TILE_NONE && vpt_cache) {
        d.x = px + inset, d.y = py + inset, d.w = inner, d.h = inner;
        SDL_BlitSurface(vpt_cache, NULL, vulture_screen, &d);
    }

}

statuswin::statuswin(window *p) : window(p)
{
    window *subwin;

    /* UI chrome: loaded at vulture_ui_scale so it survives the present-time
     * downscale at its native apparent size (see vulture_types.h) */
    statusbg = vulture_load_graphic_ui(V_FILENAME_STATUS_BAR);
    bg_scale = vulture_ui_scale;
    vulture_status_panel_size(&this->w, &this->h); /* s199 */
    this->x = V_UI(6) + this->h + V_UI(4); /* s110: room for the portrait */
    this->y = vulture_screen->h - (this->h + V_UI(6));
    this->menu_id = V_WIN_STATUSBAR;

    /* The enhance symbol: usually invisible, it is shown only when skill
     * enhancement is possible.  s199: right of the panel, clear of the
     * hotbar above it */
    subwin = new enhancebutton(this->parent);
    subwin->x = this->x + this->w + V_UI(4);
    subwin->y = this->y + (this->h - subwin->h) / 2;
    subwin->menu_id = V_WIN_ENHANCE;

    /* The column offsets and the text box sizes are in the same space as the
     * (now scaled) background art, so they scale with it.  The line height
     * already does, because the font itself was opened scaled. */
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++) {
            tokenarray[i][j] = new textwin(this, "");
            tokenarray[i][j]->x = V_UI(3 + status_xpos[i]);
            tokenarray[i][j]->y =
                V_UI(2) + j * vulture_get_lineheight(V_FONT_STATUS);
            tokenarray[i][j]->w = V_UI(100);
            tokenarray[i][j]->h = vulture_get_lineheight(V_FONT_STATUS);
        }

    /* the player is longer than everything else */
    tokenarray[0][0]->w = V_UI(250);
    for (int i = 0; i < 5; i++) /* s199: the panel draws everything itself */
        for (int j = 0; j < 5; j++)
            tokenarray[i][j]->visible = 0;

    stwin = this;

    /* Set warning colors */
    warn_colors[V_WARN_NONE] = V_COLOR_TEXT;
    warn_colors[V_WARN_NORMAL] = CLR32_GREEN;
    warn_colors[V_WARN_MORE] = CLR32_YELLOW;
    warn_colors[V_WARN_ALERT] = CLR32_ORANGE;
    warn_colors[V_WARN_CRITICAL] = CLR32_RED;
}

statuswin::~statuswin()
{
    SDL_FreeSurface(statusbg);
}

bool
statuswin::draw()
{
    vulture_status_panel_draw(abs_x, abs_y, w, h); /* s199 */

    vulture_invalidate_region(abs_x, abs_y, w, h);
    /* s110: the hero portrait, square, just left of the panel */
    {
        int ps = h, px = abs_x - V_UI(4) - ps;

        if (px >= 0) {
            vpt_draw(statusbg, px, abs_y, ps);
            vulture_invalidate_region(px, abs_y, ps, ps);
        }
    }

    return true;
}

eventresult
statuswin::handle_mousemotion_event(window *target, void *result, int xrel,
                                    int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
statuswin::handle_mousebuttonup_event(window *target, void *result,
                                      int mouse_x, int mouse_y, int button,
                                      int state)
{
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
statuswin::handle_resize_event(window *target, void *result, int res_w,
                               int res_h)
{
    /* A render-scale change (F8 / Interface Options) reaches every window as a
     * resize.  Our background art and our children's positions were baked at
     * the old scale, so rebuild them before repositioning. */
    if (bg_scale != vulture_ui_scale) {
        SDL_Surface *newbg = vulture_load_graphic_ui(V_FILENAME_STATUS_BAR);

        if (newbg) {
            SDL_FreeSurface(statusbg);
            statusbg = newbg;
            bg_scale = vulture_ui_scale;
            vulture_status_panel_size(&w, &h); /* s199 */
            x = V_UI(6) + h + V_UI(4); /* s110 */

            for (int i = 0; i < 5; i++)
                for (int j = 0; j < 5; j++) {
                    tokenarray[i][j]->x = V_UI(3 + status_xpos[i]);
                    tokenarray[i][j]->y =
                        V_UI(2) + j * vulture_get_lineheight(V_FONT_STATUS);
                    tokenarray[i][j]->w = V_UI(100);
                    tokenarray[i][j]->h = vulture_get_lineheight(V_FONT_STATUS);
                }
            tokenarray[0][0]->w = V_UI(250);
        }
    }

    /* x coordinate does not change */
    y = parent->h - (h + V_UI(6));
    if (enhancebtn) { /* s199: it follows the panel */
        enhancebtn->x = x + w + V_UI(4);
        enhancebtn->y = y + (h - enhancebtn->h) / 2;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

void
statuswin::parse_statusline(std::string str)
{
    int hp, hpmax, nconds;
    long val;
    std::string txt;
    int cap = near_capacity();
    size_t pos;
    char buf[64];

    /* get player name + title */
    pos = str.find("St:", 0);
    if (pos != std::string::npos)
        tokenarray[0][0]->caption = str.substr(0, pos);

    /* strength needs special treatment */
    if (ACURR(A_STR) > 18) {
        if (ACURR(A_STR) > STR18(100))
            sprintf(buf, "St:%2d", ACURR(A_STR) - 100);
        else if (ACURR(A_STR) < STR18(100))
            sprintf(buf, "St:18/%02d", ACURR(A_STR) - 18);
        else
            sprintf(buf, "St:18/**");
    } else
        sprintf(buf, "St:%-1d", ACURR(A_STR));
    tokenarray[0][1]->caption = buf;

    /* the other stats */
    sprintf(buf, "Dx:%-1d", ACURR(A_DEX));
    tokenarray[0][2]->caption = buf;

    sprintf(buf, "Co:%-1d", ACURR(A_CON));
    tokenarray[0][3]->caption = buf;

    sprintf(buf, "In:%-1d", ACURR(A_INT));
    tokenarray[1][1]->caption = buf;

    sprintf(buf, "Wi:%-1d", ACURR(A_WIS));
    tokenarray[1][2]->caption = buf;

    sprintf(buf, "Ch:%-1d", ACURR(A_CHA));
    tokenarray[1][3]->caption = buf;

    /* alignment */
    tokenarray[4][0]->visible = 1;
    tokenarray[4][0]->caption =
        (u.ualign.type == A_CHAOTIC)
            ? "Chaotic"
            : (u.ualign.type == A_NEUTRAL) ? "Neutral" : "Lawful";

    /* score */
#ifdef SCORE_ON_BOTL
    if (flags.showscore) {
        sprintf(buf, "S:%ld", botl_score());
        tokenarray[3][4]->caption = buf;
    } else
        tokenarray[3][4]->caption.clear();
#endif

        /* money */
#if defined(VULTURE_NETHACK_3_6_0) || defined(GOLDOBJ)
    val = money_cnt(gi.invent);
#else
    val = u.ugold;
#endif

    char coin_char =
#ifdef VULTURE_NETHACK_3_6_0
        '$';
#else
        oc_syms[COIN_CLASS];
#endif

    if (val >= 100000) {
        sprintf(buf, "%c:%-2ldk", coin_char, val / 1000);
    } else {
        sprintf(buf, "%c:%-2ld", coin_char, val);
    }
    tokenarray[3][2]->caption = buf;

    /* Experience */
    if (Upolyd) {
        sprintf(buf, "HD:%d", mons[u.umonnum].mlevel);
        tokenarray[3][0]->caption = buf;
    }
    else if (flags.showexp) {
        Sprintf(buf, "Xp:%u/%-1ld", u.ulevel, u.uexp);
        tokenarray[3][0]->caption = buf;
        /* if the exp gets too long, suppress displaying the alignment */
        if (tokenarray[3][0]->caption.length() > 10)
            tokenarray[4][0]->visible = 0;
    }
    else {
        Sprintf(buf, "Exp:%u", u.ulevel);
        tokenarray[3][0]->caption = buf;
    }

    /* HP, energy, armor */
    hp = Upolyd ? u.mh : u.uhp;
    hpmax = Upolyd ? u.mhmax : u.uhpmax;
    sprintf(buf, "HP:%d(%d)", hp, hpmax);
    tokenarray[2][1]->caption = buf;
    if (hp >= ((hpmax * 90) / 100))
        tokenarray[2][1]->textcolor = warn_colors[V_WARN_NONE];
    else if (hp >= ((hpmax * 70) / 100))
        tokenarray[2][1]->textcolor = warn_colors[V_WARN_NORMAL];
    else if (hp >= ((hpmax * 50) / 100))
        tokenarray[2][1]->textcolor = warn_colors[V_WARN_MORE];
    else if (hp >= ((hpmax * 25) / 100))
        tokenarray[2][1]->textcolor = warn_colors[V_WARN_ALERT];
    else
        tokenarray[2][1]->textcolor = warn_colors[V_WARN_CRITICAL];
    sprintf(buf, "Pw:%d(%d)", u.uen, u.uenmax);
    tokenarray[2][2]->caption = buf;
    sprintf(buf, "AC:%-2d", u.uac);
    tokenarray[2][3]->caption = buf;

    /* time */
    if (flags.time) {
        sprintf(buf, "T:%ld", svm.moves);
        tokenarray[3][3]->caption = buf;
    } else
        tokenarray[3][3]->caption.clear();

    /* depth again (numeric) */
    sprintf(buf, "Dlvl:%-2d ", depth(&u.uz));
    tokenarray[3][1]->caption = buf;

    /* conditions (hunger, confusion, etc) */
    nconds = 0;

    if (u.uhs > 1) /* hunger */
        add_cond(hu_stat[u.uhs], nconds++, u.uhs - 1);
    else if (u.uhs < 1) /* satiated */
        add_cond(hu_stat[u.uhs], nconds++, 0);

    if (Confusion)
        add_cond("Conf", nconds++, V_WARN_MORE);

    if (Sick) {
        if (u.usick_type & SICK_VOMITABLE)
            add_cond("FoodPois", nconds++, V_WARN_ALERT);
        if (u.usick_type & SICK_NONVOMITABLE)
            add_cond("Ill", nconds++, V_WARN_ALERT);
    }

    if (Blind)
        add_cond("Blind", nconds++, V_WARN_MORE);
    if (Stunned)
        add_cond("Stun", nconds++, V_WARN_MORE);
    if (Hallucination)
        add_cond("Hallu", nconds++, V_WARN_MORE);
    if (Slimed)
        add_cond("Slime", nconds++, V_WARN_ALERT);

    /* Vulture mod (IMPLEMENTATION_LOG §59, reworked §63): a VAGUE sense of
     * how long it's been since you prayed - only what the hero knows.  It
     * reads turns since the last prayer (the chronicle), never the hidden
     * prayer timeout or Luck (the user: both are cheating), so it is a hunch,
     * not a guarantee - the real timeout is random and can run long.
     *   "Pray"   green  : 1000+ turns since you prayed (or never prayed)
     *   "Pray?"  yellow : 500-999 turns - a while
     *   "NoPray" red    : god angry (the hero was told)
     * Nothing shown under 500 turns. */
    {
        long since = vq_turns_since_prayer();

        if (u.ugangr)
            add_cond("NoPray", nconds++, V_WARN_CRITICAL);
        else if (since >= 1000L)
            add_cond("Pray", nconds++, V_WARN_NORMAL);
        else if (since >= 500L)
            add_cond("Pray?", nconds++, V_WARN_MORE);
    }

    if (cap > UNENCUMBERED)
        add_cond(enc_stat[cap], nconds++, cap);

    /* reset the empty positions */
    for (; nconds < V_STATUS_CONDS; nconds++)
        add_cond("", nconds, 0);

    /* Vulture mod (IMPLEMENTATION_LOG §45): quiver readout in the
       bottom-left cell, which conditions no longer use.  Yellow when 5 or
       fewer are left.  The core refreshes the status line when the quiver
       changes (vulture_qol_pass, allmain.c). */
    if (uquiver) {
        std::stringstream qs;

        qs << "Qv:" << uquiver->quan << " " << simpleonames(uquiver);
        tokenarray[0][4]->caption = qs.str();
        tokenarray[0][4]->textcolor =
            warn_colors[(uquiver->quan <= 5) ? V_WARN_MORE : V_WARN_NONE];
    } else {
        tokenarray[0][4]->caption = "Qv:-";
        tokenarray[0][4]->textcolor = warn_colors[V_WARN_NONE];
    }

    for (int i = 0; i < 5; i++) /* s199: the panel draws everything itself */
        for (int j = 0; j < 5; j++)
            tokenarray[i][j]->visible = 0;

#ifdef SHOW_WEIGHT
    if (flags.showweight && !tokenarray[0][4]->caption[0]) {
        std::stringstream stream;
        stream << "Wt:" << static_cast<long>((inv_weight() + weight_cap()))
               << "/" << static_cast<long>(weight_cap());
        tokenarray[0][4]->caption = stream.str();
    }
#endif
}

void
statuswin::add_cond(std::string str, int warnno, int color)
{
    /* §45: {0,4} now holds the quiver readout and {4,0} is the alignment
       (stock wrote a 9th condition over it); 7 condition cells remain */
    static const point pos[V_STATUS_CONDS] = { { 4, 1 }, { 4, 2 }, { 4, 3 },
                                               { 4, 4 }, { 3, 4 }, { 2, 4 },
                                               { 1, 4 } };
    if (warnno >= V_STATUS_CONDS)
        return;

    tokenarray[pos[warnno].x][pos[warnno].y]->caption = str;
    tokenarray[pos[warnno].x][pos[warnno].y]->textcolor = warn_colors[color];
}
