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

#include <sstream>

statuswin *stwin;
#define V_STATUS_CONDS 7 /* condition cells; see add_cond() (§45) */
static const int status_xpos[5] = { 0, 60, 100, 180, 250 };
static Uint32 warn_colors[V_MAX_WARN];

statuswin::statuswin(window *p) : window(p)
{
    window *subwin;

    /* UI chrome: loaded at vulture_ui_scale so it survives the present-time
     * downscale at its native apparent size (see vulture_types.h) */
    statusbg = vulture_load_graphic_ui(V_FILENAME_STATUS_BAR);
    bg_scale = vulture_ui_scale;
    this->w = statusbg->w;
    this->h = statusbg->h;
    this->x = V_UI(6);
    this->y = vulture_screen->h - (this->h + V_UI(6));
    this->menu_id = V_WIN_STATUSBAR;

    /* The enhance symbol: usually invisible, it is shown only when skill
     * enhancement is possible */
    subwin = new enhancebutton(this->parent);
    subwin->x = this->x + this->w;
    subwin->y = this->y - subwin->h;
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
    vulture_set_draw_region(abs_x, abs_y, abs_x + w - 1, abs_y + h - 1);
    vulture_put_img(abs_x, abs_y, statusbg);
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    vulture_invalidate_region(abs_x, abs_y, w, h);

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
            w = statusbg->w;
            h = statusbg->h;
            x = V_UI(6);

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
