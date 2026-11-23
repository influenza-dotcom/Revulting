/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

#include <thread> /* s167: before NetHack's min/max macros */
extern "C" {
#include "hack.h"
}

#include "contextmenu.h"
#include "hotspot.h"
#include "levelwin.h"
#include "map.h"
#include "messagewin.h"
#include "minimap.h"
#include "toolbar.h"

#include "vulture_gen.h"
#include "vulture_gra.h"
#include "vulture_main.h"
#include "vulture_mou.h"
#include "vulture_opt.h"
#include "vulture_tile.h"
#include "vulture_win.h"
#include "vulture_txt.h" /* s85: damage numbers */

static int v_rot_door(int tile); /* s62 */

/* ??????????
 * without this the file will occasionally fail to compile, seemingly at
 * random
 */
#undef min
#undef max
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

#define VULTURE_CLIPMARGIN 200

levelwin *levwin;

/* s95: cansee()/couldsee() index gv.viz_array, which doesn't exist until
 * vision_init() - and the main menu runs BEFORE that (windmain.c), with
 * the windows already up.  A map update there dereferenced NULL: the
 * startup crash.  In Vulture's map code they answer "no" until vision is
 * set up. */
#undef cansee
#define cansee(x, y) \
    (gv.viz_array && (gv.viz_array[y][x] & IN_SIGHT) != 0)
#undef couldsee
#define couldsee(x, y) \
    (gv.viz_array && (gv.viz_array[y][x] & COULD_SEE) != 0)
static void vst_add(int, int, int); /* s93: blood stains, defined below */
static double vpan_px = 0.0, vpan_py = 0.0; /* s79: MMB drag residual */
int vulture_map_draw_msecs = 0;
int vulture_map_draw_lastmove = 0;
int vulture_map_highlight_objects = 0;

levelwin::levelwin(mapdata *data) : window(NULL), map_data(data)
{
    v_type = V_WINTYPE_LEVEL;

    x = 0;
    y = 0;
    w = vulture_screen->w;
    h = vulture_screen->h;
    need_redraw = 1;

    /* Edge-scroll hotspots.  These are UI (mouse targets), not map, so their
     * band width is scaled: a 20-render-pixel band would be a 10-screen-pixel
     * band at render_scale 2 and a 5-pixel one at 4. */
    /* upper left scroll hotspot */
    new hotspot(this, 0, 0, V_UI(20), V_UI(20), V_HOTSPOT_SCROLL_UPLEFT,
                "scroll diagonally");
    /* upper right scroll hotspot */
    new hotspot(this, w - V_UI(20), 0, V_UI(20), V_UI(20),
                V_HOTSPOT_SCROLL_UPRIGHT, "scroll diagonally");
    /* left scroll hotspot */
    new hotspot(this, 0, V_UI(20), V_UI(20), h - V_UI(40),
                V_HOTSPOT_SCROLL_LEFT, "scroll left");
    /* right scroll hotspot */
    new hotspot(this, w - V_UI(20), V_UI(20), V_UI(20), h - V_UI(40),
                V_HOTSPOT_SCROLL_RIGHT, "scroll right");
    /* bottom left scroll hotspot */
    new hotspot(this, 0, h - V_UI(20), V_UI(20), V_UI(20),
                V_HOTSPOT_SCROLL_DOWNLEFT, "scroll diagonally");
    /* bottom scroll hotspot */
    new hotspot(this, V_UI(20), h - V_UI(20), w - V_UI(40), V_UI(20),
                V_HOTSPOT_SCROLL_DOWN, "scroll down");
    /* upper left scroll hotspot */
    new hotspot(this, w - V_UI(20), h - V_UI(20), V_UI(20), V_UI(20),
                V_HOTSPOT_SCROLL_DOWNRIGHT, "scroll diagonally");

    /* create the minimap now, so that it's drawn _under_ the message window
     */
    new minimap(this, map_data);

    /*NOTE: the upper scroll hotspot gets created later in ::init(),
     * so that it covers the message window */

    clip_tl_x = 999999;
    clip_tl_y = 999999;
    clip_br_x = 0;
    clip_br_y = 0;

    levwin = this;

    map_data->add_viewer(this);
}

void
levelwin::init()
{
    int i, j;

    /* Initialize map */
    for (i = 0; i < ROWNO; i++) {
        for (j = 0; j < COLNO; j++) {
            map_deco[i][j] = 0;
            room_indices[i][j] = 0;

            clear_walls(i, j);
        }
    }

    w = vulture_screen->w;
    h = vulture_screen->h;

    /* create the upper scroll hotspot. NOTE that all other scroll hotspots
    // 		* are created earlier in the ctor */
    new hotspot(this, V_UI(20), 0, w - V_UI(40), V_UI(20),
                V_HOTSPOT_SCROLL_UP, "scroll up");

    /* Toolbar1: inventory, map, cast spell, extended commands */
    const tb_buttondesc tb1_desc[6] = {
        { V_HOTSPOT_BUTTON_INVENTORY, "Inventory" },
        { V_HOTSPOT_BUTTON_MAP, "Map" },
        { V_HOTSPOT_BUTTON_SPELLBOOK, "Cast spell" },
        { V_HOTSPOT_BUTTON_EXTENDED, "Extended commands" },
        { V_HOTSPOT_BUTTON_DISCOVERIES, "Show discoveries" },
        { 0, "" }
    };

    new toolbar(this, V_WIN_TOOLBAR1, vulture_opts.show_actiontb,
                w - V_UI(215), h - V_UI(84), V_FILENAME_TOOLBAR1, tb1_desc);

    /* Toolbar 2: look at, previous messages, options, help */
    const tb_buttondesc tb2_desc[6] = {
        { V_HOTSPOT_BUTTON_GOLD, "Gold" },
        { V_HOTSPOT_BUTTON_LOOK, "Look" },
        { V_HOTSPOT_BUTTON_MESSAGES, "Old messages" },
        { V_HOTSPOT_BUTTON_OPTIONS, "Options" },
        { V_HOTSPOT_BUTTON_IFOPTIONS, "Interface Options" },
        { V_HOTSPOT_BUTTON_HELP, "Help" }
    };

    new toolbar(this, V_WIN_TOOLBAR2, vulture_opts.show_helptb,
                w - V_UI(255), h - V_UI(45), V_FILENAME_TOOLBAR2, tb2_desc);

    /* select wall style */
    switch (vulture_opts.wall_style) {
    case V_WALL_DISPLAY_STYLE_FULL:
        walltiles = (struct walls *) walls_full;
        break;

    case V_WALL_DISPLAY_STYLE_HALF_HEIGHT:
        walltiles = (struct walls *) walls_half;
        break;

    default:
        walltiles = (struct walls *) walls_half;
        break;
    }

    view_x = COLNO / 2;
    view_y = ROWNO / 2;
}

levelwin::~levelwin()
{
    map_data->del_viewer(this);
}

void
levelwin::set_view(int x, int y)
{
    view_x = x;
    view_y = y;
    vpan_px = vpan_py = 0.0; /* s79: a recentre ends any drag offset */
}

/* s121: mouse-wheel zoom factor (>1 = zoomed in; see vzoom_apply) */
static double vzoom = 1.0;

/* s117: the hero has left the visible map (the free camera's rescue) */
bool
levelwin::hero_off_view(int map_x, int map_y)
{
    int sx, sy;

    v_rot(map_x - view_x, map_y - view_y, 4 - vulture_view_rot, &map_x,
          &map_y);
    sx = vulture_screen->w / 2 + V_MAP_XMOD * (map_x - map_y) + (int) vpan_px;
    sy = vulture_screen->h / 2 + V_MAP_YMOD * (map_x + map_y) + (int) vpan_py;
    /* s121: zoomed in, less of the map is on screen */
    sx = vulture_screen->w / 2 + (int) ((sx - vulture_screen->w / 2) * vzoom);
    sy = vulture_screen->h / 2 + (int) ((sy - vulture_screen->h / 2) * vzoom);
    return sx < V_MAP_XMOD || sx > vulture_screen->w - V_MAP_XMOD
           || sy < V_MAP_YMOD * 2 || sy > vulture_screen->h * 5 / 6;
}

/* check whether the map needs recentering */
bool
levelwin::need_recenter(int map_x, int map_y)
{
    int screen_x, screen_y;

    /* §62: through the view rotation */
    v_rot(map_x - view_x, map_y - view_y, 4 - vulture_view_rot, &map_x,
          &map_y);
    screen_x = vulture_screen->w / 2 + V_MAP_XMOD * (map_x - map_y);
    screen_y = vulture_screen->h / 2 + V_MAP_YMOD * (map_x + map_y);

    /* if the player is within the outer 1/4 of the screen, the map needs
     * recentering  */
    if ((screen_x >= vulture_screen->w / 4)
        && (screen_x < vulture_screen->w * 3 / 4)
        && (screen_y >= vulture_screen->h / 4)
        && (screen_y < vulture_screen->h * 3 / 4))
        return true;

    return false;
}

void
levelwin::force_redraw(void)
{
    need_redraw = 1;

    clip_tl_x = 0;
    clip_tl_y = 0;
    clip_br_x = w;
    clip_br_y = h;
}

#include <map>
#include "statuswin.h"

/* s117: the hero portrait left of the status panel (statuswin.cpp s110):
   click = centre the camera on the hero, double-click = lock it there */
static bool
vpt_hit(int mx, int my)
{
    int ps, px, py;

    if (!stwin || !stwin->visible)
        return false;
    ps = stwin->h, px = stwin->abs_x - V_UI(4) - ps, py = stwin->abs_y;
    return px >= 0 && mx >= px && mx < px + ps && my >= py && my < py + ps;
}

/* s123: Dota-style - double-clicking the portrait makes the camera
   follow the hero until you move the camera yourself (edge scroll or
   middle-drag); session only, no badge, no popup */
bool vulture_cam_follow = true; /* s127: starts following the hero */
bool vulture_cam_manual = false; /* s124: you've panned: no auto-rescue */
bool vulture_swallow_lclick_up = false; /* s128: that click stopped travel */
Uint32 vulture_swallow_lclick_t = 0;

static void
vpt_click(void)
{
    static Uint32 last = 0;
    Uint32 now = SDL_GetTicks();

    if (last && now - last < 400) {
        last = 0;
        vulture_cam_follow = true;
        vulture_sfx("ui_lock"); /* s131 */
    } else {
        last = now;
        vulture_sfx("ui_click");
    }
    vulture_cam_manual = false; /* s124 */
    if (levwin) {
        levwin->set_view(u.ux, u.uy);
        levwin->force_redraw();
    }
}

/*
 * IMPLEMENTATION_LOG s75: tweening + facing.
 *
 * Every visible mover (hero and monsters, keyed by m_id) is compared with
 * the positions committed after the last animation.  A one-square move
 * makes the map draw that mover - and the camera, when the view followed
 * the hero - at the OLD place first (vtw_t = 0); vulture_tween_run(),
 * called after that frame reaches the screen, then plays the in-between
 * frames (eased) and commits.  So the screen never shows stale positions:
 * the slide starts the moment the move is displayed.  Teleports, level
 * changes and view rotation snap.
 *
 * Facing: the last horizontal screen direction each mover travelled.
 * Vulture's art faces either way (jackal right, horse left), so each
 * sprite's own facing is detected once from where its top (head/ears)
 * sits; sprites flip to match travel.  Front-facing art counts as right.
 */
void vulture_tween_run(void); /* s154: file scope, C++ linkage like its callers */
static int vheft_mon(const struct permonst *p); /* s151 */
static int vheft_obj(const struct obj *o);
static void vimpact(int x, int y, int heft, int delay_ms);
static void vstep_puff(int x, int y, int sub, bool wet); /* s154 */

/* s157: moves of up to this many squares glide (fast monsters cover 2-3
   between the frames you see); longer jumps - teleports - still snap */
#define VTW_REACH 3

struct vtw_ent {
    int x, y;
    signed char face;
};
static std::map<unsigned, vtw_ent> vtw_snap, vtw_cur;
static int vtw_snap_vx = -1, vtw_snap_vy = -1, vtw_snap_rot = -1;
static d_level vtw_snap_lev = { -1, -1 };
static bool vtw_pending = false, vtw_animating = false;
static double vtw_t = 1.0;
static short vtw_ox[ROWNO][COLNO], vtw_oy[ROWNO][COLNO];
static signed char vtw_face[ROWNO][COLNO];
static int vtw_gx = 0, vtw_gy = 0;
static int vtw_last_vx = -1, vtw_last_vy = -1;
static double vtw_te = 1.0;               /* s79: eased tween time */
static bool vtw_linear = false;           /* s117: walking glide, no easing */
int vulture_tween_ate_ms = 0;             /* s117: delay_output skips this */
static char vtw_mv[ROWNO][COLNO];         /* s79: square holds a mover */
#define VTW_HERO 0xFFFFFFFFu

static void
vtw_proj(int a, int b, int *sx, int *sy)
{
    int va, vb;

    v_rot(a, b, 4 - vulture_view_rot, &va, &vb);
    *sx = V_MAP_XMOD * (va - vb);
    *sy = V_MAP_YMOD * (va + vb);
}

static void
vtw_commit(int vx, int vy)
{
    vtw_snap = vtw_cur;
    vtw_snap_vx = vx, vtw_snap_vy = vy;
    vtw_snap_rot = vulture_view_rot;
    vtw_snap_lev = u.uz;
}

/* fill vtw_ox/oy/face and the camera offset for this draw */
static void
vtw_prepare(int vx, int vy)
{
    struct monst *mtmp;
    bool snap_ok, moved = false;
    double t, te;
    std::map<unsigned, vtw_ent>::iterator it, sn;

    memset(vtw_ox, 0, sizeof vtw_ox);
    memset(vtw_oy, 0, sizeof vtw_oy);
    memset(vtw_face, 0, sizeof vtw_face);
    memset(vtw_mv, 0, sizeof vtw_mv);
    vtw_te = 1.0;
    vtw_gx = vtw_gy = 0;
    vtw_last_vx = vx, vtw_last_vy = vy;

    vtw_cur.clear();
    {
        vtw_ent e = { u.ux, u.uy, 0 };
        vtw_cur[VTW_HERO] = e;
    }
    for (mtmp = fmon; mtmp; mtmp = mtmp->nmon)
        if (!DEADMONSTER(mtmp) && canspotmon(mtmp) && isok(mtmp->mx, mtmp->my)) {
            vtw_ent e = { mtmp->mx, mtmp->my, 0 };
            vtw_cur[mtmp->m_id] = e;
        }

    snap_ok = (vtw_snap_rot == vulture_view_rot && on_level(&vtw_snap_lev, &u.uz));

    for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it) {
        sn = vtw_snap.find(it->first);
        if (sn == vtw_snap.end())
            continue;
        it->second.face = sn->second.face;
        if (!snap_ok)
            continue;
        int dx = it->second.x - sn->second.x, dy = it->second.y - sn->second.y;
        if ((dx || dy) && abs(dx) <= VTW_REACH && abs(dy) <= VTW_REACH) {
            int sx, sy;

            if ((abs(dx) > 1 || abs(dy) > 1) && !vtw_animating && !vtw_pending
                && getenv("VULTURE_SELFTEST")) /* probe: s157 */
                vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "glide %d,%d squares\n", dx, dy);
            moved = true;
            vtw_proj(dx, dy, &sx, &sy);
            if (sx > 0)
                it->second.face = 1;
            else if (sx < 0)
                it->second.face = -1;
        }
    }
    if (snap_ok && vtw_snap_vx >= 0 && (vx != vtw_snap_vx || vy != vtw_snap_vy)
        && abs(vx - vtw_snap_vx) <= VTW_REACH && abs(vy - vtw_snap_vy) <= VTW_REACH)
        moved = true;

    if (moved && !vtw_animating && !vtw_pending) {
        vtw_pending = true;
        vtw_t = 0.0;
        /* s154: every step kicks up a little puff where it left from -
           dust, or a splash in water; nothing for flyers and ghosts */
        for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it) {
            struct monst *pm3 = 0;
            int ox2, oy2;

            sn = vtw_snap.find(it->first);
            if (sn == vtw_snap.end() || !snap_ok
                || (sn->second.x == it->second.x && sn->second.y == it->second.y)
                || abs(sn->second.x - it->second.x) > 1 || abs(sn->second.y - it->second.y) > 1)
                continue;
            ox2 = sn->second.x, oy2 = sn->second.y;
            if (it->first == VTW_HERO) {
                if (Levitation || Flying || u.usteed)
                    continue;
            } else if (!(pm3 = m_at(it->second.x, it->second.y)) || !canspotmon(pm3)
                       || is_floater(pm3->data) || is_flyer(pm3->data)
                       || noncorporeal(pm3->data)) {
                continue;
            }
            if (isok(ox2, oy2) && cansee(ox2, oy2)) {
                int sub = pm3 ? (vheft_mon(pm3->data) > 2 ? 2 : vheft_mon(pm3->data)) : 1;

                vstep_puff(ox2, oy2, sub < 1 ? 1 : sub,
                           is_pool(ox2, oy2) || is_pool(it->second.x, it->second.y));
            }
        }
        /* s151: the heavy ones' feet come down at the end of the step */
        for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it) {
            struct monst *hm;

            sn = vtw_snap.find(it->first);
            if (it->first == VTW_HERO || sn == vtw_snap.end() || !snap_ok
                || (sn->second.x == it->second.x && sn->second.y == it->second.y)
                || abs(sn->second.x - it->second.x) > 1 || abs(sn->second.y - it->second.y) > 1
                || !(hm = m_at(it->second.x, it->second.y)) || !canspotmon(hm))
                continue;
            if (vheft_mon(hm->data) >= 4)
                vimpact(it->second.x, it->second.y, vheft_mon(hm->data) - 1, 100);
        }
    }
    if (!vtw_pending && !vtw_animating) {
        vtw_commit(vx, vy);
        for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it)
            vtw_face[it->second.y][it->second.x] = it->second.face;
        return;
    }

    t = vtw_t;
    /* s147: ease-out - the move starts the instant you press, then settles
       (smoothstep's slow start read as input lag) */
    te = vtw_linear ? t : t * (2.0 - t);
    vtw_te = te;
    for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it) {
        int x = it->second.x, y = it->second.y;

        vtw_face[y][x] = it->second.face;
        sn = vtw_snap.find(it->first);
        if (sn == vtw_snap.end() || !snap_ok)
            continue;
        int dx = sn->second.x - x, dy = sn->second.y - y;
        if ((dx || dy) && abs(dx) <= VTW_REACH && abs(dy) <= VTW_REACH) {
            int sx, sy;

            vtw_proj(dx, dy, &sx, &sy);
            vtw_ox[y][x] = (short) ((1.0 - te) * sx);
            /* s78/s79: a hop on each step - airborne for the first 0.8,
               then a landing squash (drawn in levelwin::draw) */
            double hopk = 1.0; /* s151: light things spring, heavy ones plod */

            if (it->first != VTW_HERO) {
                struct monst *hm = m_at(x, y);
                int hf = hm ? vheft_mon(hm->data) : 2;

                hopk = hf <= 0 ? 1.4 : hf == 1 ? 1.2 : hf == 2 ? 1.0 : hf <= 4 ? 0.6 : 0.3;
            }
            vtw_oy[y][x] = (short) ((1.0 - te) * sy
                                    - hopk * (vtw_linear ? 2.5 : 6.0) /* s147: was 5 / 12 */
                                      * sin(3.14159265358979 * (te < 0.8 ? te / 0.8 : 1.0)));
            vtw_mv[y][x] = 1;
        }
    }
    if (snap_ok && vtw_snap_vx >= 0 && abs(vx - vtw_snap_vx) <= VTW_REACH
        && abs(vy - vtw_snap_vy) <= VTW_REACH) {
        int sx, sy;

        vtw_proj(vx - vtw_snap_vx, vy - vtw_snap_vy, &sx, &sy);
        vtw_gx = (int) ((1.0 - te) * sx);
        vtw_gy = (int) ((1.0 - te) * sy);
    }
}

/* the art's own facing: +1 right, -1 left, 0 unclear (cached per tile) */
static int
vtw_art_facing(int tile_id, SDL_Surface *s)
{
    static std::map<int, int> cache;
    std::map<int, int>::iterator ci = cache.find(tile_id);
    int x, y, x0 = 99999, x1 = -1, y0 = 99999, y1 = -1, bpp, res = 0;
    Uint32 key = 0;
    bool haskey;
    long sumx = 0, cnt = 0;

    if (ci != cache.end())
        return ci->second;
    if (!s || SDL_LockSurface(s) != 0) {
        cache[tile_id] = 0;
        return 0;
    }
    bpp = s->format->BytesPerPixel;
    haskey = (SDL_GetColorKey(s, &key) == 0);
#define VTW_PIX(xx, yy) \
    (bpp == 4 ? *(Uint32 *) ((Uint8 *) s->pixels + (yy) * s->pitch + (xx) * 4) \
     : bpp == 2 ? *(Uint16 *) ((Uint8 *) s->pixels + (yy) * s->pitch + (xx) * 2) \
     : *((Uint8 *) s->pixels + (yy) * s->pitch + (xx) * bpp))
    for (int pass = 0; pass < 2; pass++)
        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->w; x++) {
                Uint32 p = VTW_PIX(x, y);
                Uint8 r, g, b, a;

                if (haskey && p == key)
                    continue;
                SDL_GetRGBA(p, s->format, &r, &g, &b, &a);
                if (a < 200)
                    continue;
                /* skip the grey drop shadow */
                if (abs(r - g) < 6 && abs(g - b) < 6 && r < 110)
                    continue;
                if (pass == 0) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                } else if (y <= y0 + (y1 - y0) * 3 / 10) {
                    sumx += x, cnt++;
                }
            }
#undef VTW_PIX
    SDL_UnlockSurface(s);
    if (cnt && x1 > x0) {
        double d = ((double) sumx / cnt - (x0 + x1) / 2.0) / (x1 - x0);

        res = d > 0.06 ? 1 : d < -0.06 ? -1 : 0;
    }
    cache[tile_id] = res;
    return res;
}

/* horizontally mirrored copy of a tile graphic (cached per tile id) */
static SDL_Surface *
vtw_flipped(int tile_id, SDL_Surface *s)
{
    static std::map<int, std::pair<SDL_Surface *, SDL_Surface *> > cache;
    std::map<int, std::pair<SDL_Surface *, SDL_Surface *> >::iterator ci
        = cache.find(tile_id);
    SDL_Surface *f;
    SDL_BlendMode bm;
    Uint32 key;
    Uint8 am;
    int x, y, bpp;

    if (ci != cache.end()) {
        if (ci->second.first == s)
            return ci->second.second;
        SDL_FreeSurface(ci->second.second); /* tile was reloaded */
        cache.erase(ci);
    }
    f = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, s->format->BitsPerPixel,
                                       s->format->format);
    if (!f)
        return NULL;
    if (SDL_LockSurface(s) == 0) {
        if (SDL_LockSurface(f) == 0) {
            bpp = s->format->BytesPerPixel;
            for (y = 0; y < s->h; y++) {
                Uint8 *src = (Uint8 *) s->pixels + y * s->pitch;
                Uint8 *dst = (Uint8 *) f->pixels + y * f->pitch;

                for (x = 0; x < s->w; x++)
                    memcpy(dst + (s->w - 1 - x) * bpp, src + x * bpp, bpp);
            }
            SDL_UnlockSurface(f);
        }
        SDL_UnlockSurface(s);
    }
    if (SDL_GetSurfaceBlendMode(s, &bm) == 0)
        SDL_SetSurfaceBlendMode(f, bm);
    if (SDL_GetColorKey(s, &key) == 0)
        SDL_SetColorKey(f, SDL_TRUE, key);
    if (SDL_GetSurfaceAlphaMod(s, &am) == 0)
        SDL_SetSurfaceAlphaMod(f, am);
    cache[tile_id] = std::make_pair(s, f);
    return f;
}


/*
 * IMPLEMENTATION_LOG s79: squash, stretch and rotation.
 * vtx_blit() draws a sprite scaled (sx, sy) and rotated (deg, clockwise)
 * about its FEET - the bottom-centre of the untransformed image - with an
 * optional red tint and fade.  Nearest-neighbour inverse mapping into a
 * temporary ARGB surface: only animating sprites take this path, and they
 * are ~70x90 px, so it costs next to nothing.
 */
static void
vtx_blit(SDL_Surface *img, int dx, int dy, double sx, double sy, double deg,
         bool red, int alpha)
{
    SDL_Surface *src, *dst;
    double rad, c, s, ax, ay, cx[4], cy[4], minx, miny, maxx, maxy;
    int w, h, bw, bh, x, y, k;
    Uint32 key = 0;
    bool haskey;

    if (sx == 1.0 && sy == 1.0 && deg == 0.0 && alpha >= 255) {
        if (red)
            SDL_SetSurfaceColorMod(img, 255, 70, 70);
        vulture_put_img(dx, dy, img);
        if (red)
            SDL_SetSurfaceColorMod(img, 255, 255, 255);
        return;
    }
    src = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!src)
        return;
    haskey = (SDL_GetColorKey(src, &key) == 0);
    /* s94: a sprite with no alpha and no colour key would come out as a
       black box: treat its pure black as transparent */
    if (!img->format->Amask && !haskey) {
        haskey = true;
        key = SDL_MapRGBA(src->format, 0, 0, 0, 255);
    }
    w = src->w, h = src->h;
    ax = w / 2.0, ay = (double) h;           /* the feet */
    rad = deg * 3.14159265358979 / 180.0;
    c = cos(rad), s = sin(rad);
    {
        double px[4] = { -ax, w - ax, -ax, w - ax };
        double py[4] = { -ay, -ay, h - ay, h - ay };

        minx = miny = 1e9, maxx = maxy = -1e9;
        for (k = 0; k < 4; k++) {
            double qx = px[k] * sx, qy = py[k] * sy;

            cx[k] = qx * c - qy * s;
            cy[k] = qx * s + qy * c;
            if (cx[k] < minx) minx = cx[k];
            if (cx[k] > maxx) maxx = cx[k];
            if (cy[k] < miny) miny = cy[k];
            if (cy[k] > maxy) maxy = cy[k];
        }
    }
    bw = (int) (maxx - minx) + 2, bh = (int) (maxy - miny) + 2;
    if (bw <= 0 || bh <= 0 || bw > 1000 || bh > 1000) {
        SDL_FreeSurface(src);
        return;
    }
    dst = SDL_CreateRGBSurfaceWithFormat(0, bw, bh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) {
        SDL_FreeSurface(src);
        return;
    }
    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    for (y = 0; y < bh; y++) {
        Uint32 *drow = (Uint32 *) ((Uint8 *) dst->pixels + y * dst->pitch);

        for (x = 0; x < bw; x++) {
            /* inverse: rotate back, unscale, then into source pixels */
            double rx = x + minx, ry = y + miny;
            double ux = (rx * c + ry * s) / sx, uy = (-rx * s + ry * c) / sy;
            int sxp = (int) floor(ux + ax), syp = (int) floor(uy + ay);
            Uint32 p;
            Uint8 r, g, b, a;

            drow[x] = 0;
            if (sxp < 0 || syp < 0 || sxp >= w || syp >= h)
                continue;
            p = *(Uint32 *) ((Uint8 *) src->pixels + syp * src->pitch + sxp * 4);
            if (haskey && p == key)
                continue;
            SDL_GetRGBA(p, src->format, &r, &g, &b, &a);
            if (red) {
                g = (Uint8) (g * 0.3);
                b = (Uint8) (b * 0.3);
            }
            a = (Uint8) (a * (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha) / 255);
            drow[x] = SDL_MapRGBA(dst->format, r, g, b, a);
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_BLEND);
    vulture_put_img(dx + (int) ax + (int) minx, dy + (int) ay + (int) miny, dst);
    SDL_FreeSurface(dst);
    SDL_FreeSurface(src);
}

/*
 * IMPLEMENTATION_LOG s158: contact shadows.  A soft dark ellipse on the
 * floor under every monster and object grounds them (they floated before).
 * Pre-rendered per width (radial falloff), cached; monsters' follow the
 * slide, a hopping sprite's shadow stays on the floor and shrinks a little.
 */
static void
vshadow(int cx, int cy, int w, int alpha)
{
    static std::map<int, SDL_Surface *> cache;
    SDL_Surface *s;
    SDL_Rect d;
    int h;

    if (w < 8)
        return;
    w = (w + 3) & ~3; /* a few sizes only */
    if (w > 120)
        w = 120;
    h = w * 2 / 5;
    if (cache.count(w)) {
        s = cache[w];
    } else {
        int x, y;

        s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s) {
            SDL_LockSurface(s);
            for (y = 0; y < h; y++) {
                Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + y * s->pitch);

                for (x = 0; x < w; x++) {
                    double dx = (x + 0.5 - w / 2.0) / (w / 2.0), dy = (y + 0.5 - h / 2.0) / (h / 2.0);
                    double r = dx * dx + dy * dy, a = r >= 1.0 ? 0.0 : (1.0 - r) * (1.0 - r);

                    row[x] = (Uint32) (a * 255) << 24;
                }
            }
            SDL_UnlockSurface(s);
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
        }
        cache[w] = s;
    }
    if (!s)
        return;
    SDL_SetSurfaceAlphaMod(s, (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
    d.x = cx - w / 2, d.y = cy - h / 2, d.w = w, d.h = h;
    SDL_BlitSurface(s, NULL, vulture_screen, &d);
}

/* draw a tile facing `face`, transformed */
static void
vtw_put_tile_x(int x, int y, int tile_id, int face, double sx, double sy,
               double deg, bool red, int alpha)
{
    vulture_tile *tile = vulture_get_tile(tile_id);
    SDL_Surface *img;
    int art, dx;
    bool flip;

    if (!tile || !tile->graphic)
        return;
    art = vtw_art_facing(tile_id, tile->graphic);
    flip = face != 0 && ((art == 0 && face < 0) || (art != 0 && art != face));
    img = tile->graphic;
    dx = x + tile->xmod;
    if (flip) {
        SDL_Surface *f = vtw_flipped(tile_id, tile->graphic);

        if (f) {
            img = f;
            dx = x - tile->xmod - tile->graphic->w + 1;
        }
    }
    vtx_blit(img, dx, y + tile->ymod, sx, sy, deg, red, alpha);
    if (vulture_normal_capture && vulture_normal_buf) { /* s149: its normal map, same pose */
        SDL_Surface *nm = vulture_get_tile_normal(tile_id, img != tile->graphic);

        if (nm) {
            SDL_Surface *save = vulture_screen;

            vulture_screen = vulture_normal_buf;
            vtx_blit(nm, dx, y + tile->ymod, sx, sy, deg, false, alpha);
            vulture_screen = save;
        }
    }
}



/*
 * IMPLEMENTATION_LOG s78: quasi-animations.
 *  - Melee lunge: the attacker darts ~40% of a square toward its target and
 *    back; the target flashes red at the moment of impact.  Played at once
 *    (vulture_anim_attack, called from do_attack() and mattacku()).
 *  - Death: a slain monster's last sprite sinks and fades where it stood.
 *    Queued by vulture_anim_death() (from mondead(), while the monster is
 *    still on the map) and played after the next refresh that shows the
 *    square without it.
 * Both draw through levelwin::draw (vanim_* state) and present frames the
 * same way the movement tween does.  Each run is timed into vulture_log.
 */
static int vanim_kind = 0;           /* 0 none, 1 lunge, 2 death */
static int vanim_ax, vanim_ay, vanim_tx, vanim_ty;
static double vanim_t = 0.0;
struct vanim_death {
    int x, y, tile, heft; /* s151: heft */
};
static vanim_death vanim_deaths[8];
static int vanim_ndeaths = 0, vanim_dtile = -1, vanim_dx = 0, vanim_dy = 0;

/* s81: animations never hold the player up - a keypress or click waiting
   in the queue ends one at once */
static bool
vanim_input_waiting(void)
{
    SDL_Event ev[16];
    int n, i;

    SDL_PumpEvents();
    if (SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_KEYDOWN, SDL_KEYDOWN) > 0)
        return true;
    /* s124: a middle press is a camera drag, not a command */
    n = SDL_PeepEvents(ev, 16, SDL_PEEKEVENT, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONDOWN);
    for (i = 0; i < n; i++)
        if (ev[i].button.button != SDL_BUTTON_MIDDLE)
            return true;
    return false;
}

static void
vanim_frames(int kind, int frames, int delay_ms)
{
    /* s147: time-based (the old frame count + sleeps came out at 3-10
       uneven frames), and a waiting key hurries the end instead of cutting
       the animation off mid-motion */
    Uint32 t0 = SDL_GetTicks(), last = t0, now;
    double dur = frames * (delay_ms + 6.0), t = 0.0, rate;

    if (dur > 170.0)
        dur = 170.0;
    rate = 1.0 / dur;
    vanim_kind = kind;
    frames = 0;
    for (;;) {
        now = SDL_GetTicks();
        t += (now - last) * rate;
        last = now;
        if (t > 1.0)
            t = 1.0;
        vanim_t = t;
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
        frames++;
        if (t >= 1.0)
            break;
        if (vanim_input_waiting()) {
            double hurry = (1.0 - t) / 30.0;

            if (hurry > rate)
                rate = hurry;
        }
        SDL_Delay(1);
    }
    vanim_kind = 0;
    vanim_t = 0.0;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vulture_refresh_window_region();
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                      "anim %s: %d frames in %u ms\n",
                      kind == 1 ? "lunge" : kind == 2 ? "death" : "drop",
                      frames,
                      (unsigned) (SDL_GetTicks() - t0));
}

extern "C" void
vulture_anim_attack(int ax, int ay, int tx, int ty)
{
    if (!levwin || vulture_windows_inited != TRUE || vanim_kind || vtw_animating)
        return;
    if (abs(ax - tx) > 1 || abs(ay - ty) > 1 || (ax == tx && ay == ty))
        return;
    { /* s154: finish any step first.  You and the monster can move in the
         same turn before the screen catches up; the lunge then struck at
         the square you were walking into while you were still drawn on the
         old one.  Draw once (which notices the pending moves) and play them
         out, so the blow lands where you visibly are. */
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        if (vtw_pending)
            vulture_tween_run();
        if (vanim_kind || vtw_animating)
            return;
    }
    vanim_ax = ax, vanim_ay = ay, vanim_tx = tx, vanim_ty = ty;
    { /* s132: a swing you can hear, three variants in turn */
        static int sw = 0;
        static const char *const nm[3] = { "swing1", "swing2", "swing3" };

        vulture_sfx_at(nm[sw], ax - u.ux, ay - u.uy); /* s136: heard from where it is */
        sw = (sw + 1) % 3;
    }
    vanim_frames(1, 7, 10);
}

extern "C" void
vulture_anim_death(int x, int y)
{
    int tile;

    if (!levwin || !map_data || vanim_ndeaths >= 8 || !isok(x, y))
        return;
    tile = map_data->get_glyph(MAP_MON, x, y);
    if (tile == V_TILE_NONE || tile < 0)
        return;
    vanim_deaths[vanim_ndeaths].x = x;
    vanim_deaths[vanim_ndeaths].y = y;
    vanim_deaths[vanim_ndeaths].tile = tile;
    {
        struct monst *dm = m_at(x, y);

        vanim_deaths[vanim_ndeaths].heft = dm ? vheft_mon(dm->data) : 2; /* s151 */
        if (!dm || !noncorporeal(dm->data))
            vst_add(x, y, 2); /* s93: a death leaves a pool */
    }
    vanim_ndeaths++;
}

/*
 * IMPLEMENTATION_LOG s83: dropped items fall.  vulture_anim_drop(x,y)
 * queues a square (your drops, monster drops, fresh corpses); after the
 * next refresh that shows an object there, every queued square plays at
 * once: the item falls ~30 px with gravity, squashes on impact, bounces
 * once and settles with a smaller squash.
 */
static int vanim_drops[8][2];
static int vanim_ndrops = 0;
static bool vanim_drop_on[ROWNO][COLNO];
static signed char vanim_drop_heft[ROWNO][COLNO]; /* s151 */

extern "C" void
vulture_anim_drop(int x, int y)
{
    int i;

    if (!levwin || vulture_windows_inited != TRUE || !isok(x, y))
        return;
    for (i = 0; i < vanim_ndrops; i++)
        if (vanim_drops[i][0] == x && vanim_drops[i][1] == y)
            return;
    if (vanim_ndrops < 8) {
        vanim_drops[vanim_ndrops][0] = x;
        vanim_drops[vanim_ndrops][1] = y;
        vanim_ndrops++;
    }
}

static void
vanim_play_drops(void)
{
    int i, n = vanim_ndrops, shown = 0;

    vanim_ndrops = 0;
    memset(vanim_drop_on, 0, sizeof vanim_drop_on);
    for (i = 0; i < n; i++) {
        int x = vanim_drops[i][0], y = vanim_drops[i][1];

        if (map_data->get_glyph(MAP_OBJ, x, y) != V_TILE_NONE) {
            vanim_drop_on[y][x] = true;
            vanim_drop_heft[y][x] = (signed char) vheft_obj(svl.level.objects[x][y]); /* s151 */
            vimpact(x, y, vanim_drop_heft[y][x], 80); /* lands about halfway in */
            shown++;
        }
    }
    if (shown)
        vanim_frames(3, 10, 12);
    memset(vanim_drop_on, 0, sizeof vanim_drop_on);
}

/* draw the object on (j,i): falling/bouncing while a drop plays there */
static void
vanim_draw_object(int j, int i, int x, int y, int tile_id)
{
    vulture_tile *tile;
    double t = vanim_t, oy, sx = 1.0, sy = 1.0;

    if (vanim_kind != 3 || !vanim_drop_on[i][j]
        || !(tile = vulture_get_tile(tile_id)) || !tile->graphic) {
        vulture_put_tile(x, y, tile_id);
        return;
    }
    if (t < 0.5) {                       /* fall, accelerating */
        double u = t / 0.5;

        oy = -30.0 * (1.0 - u * u);
        sy = 1.0 + 0.12 * u;             /* stretched by speed */
        sx = 1.0 / sy;
    } else if (t < 0.65) {               /* impact squash */
        double k = sin(3.14159265358979 * (t - 0.5) / 0.15);

        oy = 0.0;
        sy = 1.0 - 0.32 * k;
        sx = 1.0 + 0.30 * k;
    } else if (t < 0.9) {                /* one bounce */
        double u = (t - 0.65) / 0.25;
        int hf = vanim_drop_heft[i][j]; /* s151: a feather bounces, an anvil doesn't */

        oy = -9.0 * (hf <= 0 ? 1.5 : hf == 1 ? 1.1 : hf == 2 ? 0.6 : hf <= 4 ? 0.25 : 0.05)
             * sin(3.14159265358979 * u);
    } else {                             /* settle */
        double k = sin(3.14159265358979 * (t - 0.9) / 0.1);

        oy = 0.0;
        sy = 1.0 - 0.12 * k;
        sx = 1.0 + 0.10 * k;
    }
    vtx_blit(tile->graphic, x + tile->xmod, y + tile->ymod + (int) oy,
             sx, sy, 0.0, false, 255);
}

/* play queued deaths whose monster is gone from the map */
static void
vanim_play_deaths(void)
{
    int i, n = vanim_ndeaths;

    vanim_ndeaths = 0;
    for (i = 0; i < n; i++) {
        if (map_data->get_glyph(MAP_MON, vanim_deaths[i].x, vanim_deaths[i].y)
            == vanim_deaths[i].tile)
            continue; /* still shown (lifesaved, or not redrawn): skip */
        vanim_dx = vanim_deaths[i].x, vanim_dy = vanim_deaths[i].y;
        vanim_dtile = vanim_deaths[i].tile;
        vimpact(vanim_dx, vanim_dy, vanim_deaths[i].heft, 120); /* s151: the body lands */
        vanim_frames(2, 9, 14);
        vanim_dtile = -1;
    }
}

/* s79: attacker lunges - stretched along the blow and leaning into it;
   the target is knocked back a little, squashed and flashes red */
static void
vanim_xform(int j, int i, int *ox, int *oy, double *xs, double *ys,
            double *deg, bool *red)
{
    double t = vanim_t;
    int sx, sy, side;

    if (vanim_kind != 1)
        return;
    vtw_proj(vanim_tx - vanim_ax, vanim_ty - vanim_ay, &sx, &sy);
    side = sx > 0 ? 1 : sx < 0 ? -1 : 0;
    if (j == vanim_ax && i == vanim_ay) {
        double k = sin(3.14159265358979 * t);

        *ox += (int) (sx * 0.45 * k);
        *oy += (int) (sy * 0.45 * k);
        *xs *= 1.0 + 0.20 * k;
        *ys *= 1.0 - 0.10 * k;
        *deg += side * 18.0 * k;
    }
    if (j == vanim_tx && i == vanim_ty && t > 0.35) {
        double u = (t - 0.35) / 0.65, k = sin(3.14159265358979 * u);

        *ox += (int) (sx * 0.14 * k);
        *oy += (int) (sy * 0.14 * k);
        *ys *= 1.0 - 0.24 * k;
        *xs *= 1.0 + 0.16 * k;
        *deg += side * 12.0 * k;
        *red = u < 0.6;
    }
}

/* s79: the slain monster topples over, sinks and fades out, reddening */
static void
vanim_draw_death(int j, int i, int x, int y)
{
    vulture_tile *tile;
    double t = vanim_t, e = t * t * (3.0 - 2.0 * t);

    if (vanim_kind != 2 || j != vanim_dx || i != vanim_dy || vanim_dtile < 0)
        return;
    tile = vulture_get_tile(vanim_dtile);
    if (!tile || !tile->graphic)
        return;
    vtx_blit(tile->graphic, x + tile->xmod, y + tile->ymod + (int) (10 * e),
             1.0 + 0.1 * e, 1.0 - 0.15 * e, 85.0 * e, t < 0.5,
             (int) (255 * (1.0 - t * t)));
}


/*
 * IMPLEMENTATION_LOG s93: blood stains.  Wherever blood is spilled (a hit
 * that draws blood - the same moments as the blood spurts - or a death)
 * the floor keeps a dark splat, drawn under items and creatures, darker
 * with each new bleed (3 steps).  Stored per level for the session (not in
 * the save file, which would change its format): coming back to a level
 * shows its old stains.
 */
struct vst_ent {
    unsigned char x, y, level, seed;
};
static std::map<int, std::vector<vst_ent> > vst_store;
static SDL_Surface *vst_art[4];

static int
vst_key(void)
{
    return u.uz.dnum * 1000 + u.uz.dlevel;
}

static void
vst_make_art(void)
{
    int k, i, w = V_MAP_XMOD * 2, h = V_MAP_YMOD * 2;
    unsigned r = 12345;

    for (k = 0; k < 4; k++) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                        SDL_PIXELFORMAT_ARGB8888);
        if (!s)
            continue;
        SDL_FillRect(s, NULL, 0);
        SDL_LockSurface(s);
        /* a few overlapping flattened blobs + droplets */
        for (i = 0; i < 9 + k * 2; i++) {
            int cx, cy, rad, x, y;

            r = r * 1103515245u + 12345u;
            cx = w / 2 + (int) ((r >> 8) % (w / 2)) - w / 4;
            r = r * 1103515245u + 12345u;
            cy = h / 2 + (int) ((r >> 8) % (h / 2)) - h / 4;
            r = r * 1103515245u + 12345u;
            rad = i < 4 ? 6 + (int) ((r >> 8) % 9) : 1 + (int) ((r >> 8) % 3);
            for (y = cy - rad; y <= cy + rad; y++)
                for (x = cx - rad * 2; x <= cx + rad * 2; x++) {
                    double dx = (x - cx) / (2.0 * rad), dy = (y - cy) / (double) rad;

                    double rr = dx * dx + dy * dy, a;
                    Uint32 *px;
                    Uint8 pr, pg, pb, pa;

                    if (x < 0 || y < 0 || x >= w || y >= h || rr > 1.0)
                        continue;
                    /* s98: soft edge - full in the middle, fading out */
                    a = rr < 0.5 ? 1.0 : (1.0 - rr) / 0.5;
                    px = &((Uint32 *) ((Uint8 *) s->pixels + y * s->pitch))[x];
                    SDL_GetRGBA(*px, s->format, &pr, &pg, &pb, &pa);
                    if (210 * a > pa)
                        *px = SDL_MapRGBA(s->format, 95, 6, 8, (Uint8) (210 * a));
                }
        }
        SDL_UnlockSurface(s);
        SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
        vst_art[k] = s;
    }
}

static void
vst_add(int x, int y, int amount)
{
    std::vector<vst_ent> &v = vst_store[vst_key()];
    size_t i;

    if (!isok(x, y) || !ACCESSIBLE(levl[x][y].typ) || is_pool_or_lava(x, y))
        return;
    for (i = 0; i < v.size(); i++)
        if (v[i].x == x && v[i].y == y) {
            v[i].level = (unsigned char) (v[i].level + amount > 3 ? 3 : v[i].level + amount);
            return;
        }
    if (v.size() < 400) {
        vst_ent e;

        e.x = (unsigned char) x, e.y = (unsigned char) y;
        e.level = (unsigned char) (amount > 3 ? 3 : amount);
        e.seed = (unsigned char) ((x * 31 + y * 17) & 3);
        v.push_back(e);
    }
}

/* draw the stain (if any) of map square (j,i) centred at screen (x,y) */
static void
vst_draw(int j, int i, int x, int y)
{
    std::map<int, std::vector<vst_ent> >::iterator it = vst_store.find(vst_key());
    size_t k;

    if (it == vst_store.end())
        return;
    for (k = 0; k < it->second.size(); k++) {
        vst_ent &e = it->second[k];

        if (e.x != j || e.y != i)
            continue;
        if (!vst_art[0])
            vst_make_art();
        if (vst_art[e.seed]) {
            SDL_Rect d;

            d.x = x - V_MAP_XMOD, d.y = y - V_MAP_YMOD;
            d.w = V_MAP_XMOD * 2, d.h = V_MAP_YMOD * 2;
            SDL_SetSurfaceAlphaMod(vst_art[e.seed],
                                   (Uint8) (e.level == 1 ? 110 : e.level == 2 ? 170 : 225));
            SDL_BlitSurface(vst_art[e.seed], NULL, vulture_screen, &d);
        }
        return;
    }
}

/*
 * IMPLEMENTATION_LOG s85: Stoneshard-style combat feedback.
 *  - Damage numbers: any visible monster that loses HP between frames
 *    gets a number rising off it (white); the hero gets red ones when hurt
 *    and green ones when healed.  Detected from HP changes, so every
 *    damage source counts without hooking each one.
 *  - Blood: a short spurt of particles with gravity on every hit.
 *  - Swing arc: a pale crescent sweeps across the target during a lunge.
 *  - Breathing: idle creatures (and the hero) breathe - a slow ~3% scale.
 * The idle loop keeps these moving (vulture_idle_bob) while the map waits.
 */
#include <vector>

static Uint32 vsr_t = 0;            /* s144: last search / rest turn */
static int vsr_kind = 0;            /* s144: 1 searching, 2 resting */
struct vfx_ent {
    int kind;                 /* 1 number, 2 blood, 3 click marker (s108) */
    int x, y;                 /* map square */
    Uint32 t0, dur;
    char txt[40]; /* s113: room for a bark line */
    Uint8 r, g, b;
    int sub;                  /* s117: marker kind / bubble text slot */
    unsigned mid;             /* s117: bubble speaker (0 = hero); marker: up */
    float vx[10], vy[10];
};
static std::vector<vfx_ent> vfx_list;
extern "C" void vulture_sfx_at(const char *, int, int);           /* s136 */
extern "C" void vulture_sfx_at_vol(const char *, int, int, int); /* s136 */
static void vhurt_flash(int dmg, int maxhp);                      /* s136 */
static std::map<unsigned, int> vfx_hp;
static int vfx_hero_hp = -1, vfx_hero_poly = -1;
static d_level vfx_lev = { -1, -1 };

/* s129: big hits freeze the frame for a beat (hit-stop) and burst */
static int vhitstop_ms = 0;
static bool vfx_shadows = true;   /* s158: contact shadows (Effects menu) */
static bool vfx_ambient = true;   /* s160: area atmosphere particles */
static bool vfx_jolts = true;     /* s161: camera jolts from heavy impacts */
static bool vfx_puffs = true;     /* s161: dust puffs from steps */
static bool vfx_words = true;     /* s161: rising words (SNEAK ATTACK, LEVEL UP...) */
static void vl_flash(int x, int y, float r, float g, float b, float inten, float rad,
                     Uint32 dur); /* s133 */

static void
vfx_burst(int x, int y, Uint8 r, Uint8 g, Uint8 b)
{
    vfx_ent e;

    memset(&e, 0, sizeof e);
    e.kind = 6, e.x = x, e.y = y, e.t0 = SDL_GetTicks(), e.dur = 280;
    e.r = r, e.g = g, e.b = b;
    vfx_list.push_back(e);
    vl_flash(x, y, r / 255.0f, g / 255.0f, b / 255.0f, 0.7f, 2.0f, 300); /* s133 */
}

/*
 * IMPLEMENTATION_LOG s151: weight.  Heavy things land, fall and walk with
 * weight: a "heft" (0 feather .. 8 boulder / gigantic) picks a dust puff, a
 * thud and a short non-blocking camera jolt that fades with distance from
 * you.  Impacts are queued with a delay so they happen when the foot comes
 * down, the body hits the floor or the item lands, and fire from the draw.
 */
struct vimp_q {
    int x, y, heft;
    Uint32 when;
};
static std::vector<vimp_q> vimp_queue;
static Uint32 vimp_t0 = 0;
static double vimp_amp = 0.0;

/* how heavy a monster is: by size, nudged by its corpse weight */
static int
vheft_mon(const struct permonst *p)
{
    static const int bysize[8] = { 0, 1, 2, 4, 6, 8, 8, 8 };
    int h = bysize[p->msize > 7 ? 7 : p->msize];

    if (p->cwt >= 2500 && h < 8)
        h++;
    if (is_floater(p) || is_flyer(p) || noncorporeal(p))
        h = h > 1 ? h / 3 : 0; /* drifting and flapping things don't stomp */
    return h;
}

/* how heavy an object is */
static int
vheft_obj(const struct obj *o)
{
    unsigned w = o ? o->owt : 0;

    return w < 15 ? 0 : w < 60 ? 1 : w < 200 ? 2 : w < 450 ? 4 : w < 1500 ? 6 : 8;
}

/* queue an impact at (x, y) delay_ms from now */
static void
vimpact(int x, int y, int heft, int delay_ms)
{
    vimp_q q;

    if (heft < 2 || !isok(x, y) || !cansee(x, y))
        return;
    q.x = x, q.y = y, q.heft = heft > 8 ? 8 : heft;
    q.when = SDL_GetTicks() + (Uint32) (delay_ms < 0 ? 0 : delay_ms);
    vimp_queue.push_back(q);
}

/* s157: a pushed boulder or a thrown object comes to rest at (x, y) */
extern "C" void
vulture_object_impact(int x, int y, int weight)
{
    int h = weight < 15 ? 0 : weight < 60 ? 1 : weight < 200 ? 2 : weight < 450 ? 4
          : weight < 1500 ? 6 : 8;

    if (levwin)
        vimpact(x, y, h > 6 ? 6 : h, 40); /* a rolling boulder rumbles, it doesn't slam */
}

/* fire impacts that are due: dust, a thud, a jolt */
static void
vimp_tick(void)
{
    Uint32 now = SDL_GetTicks();
    size_t i, n = 0;

    for (i = 0; i < vimp_queue.size(); i++) {
        vimp_q &q = vimp_queue[i];

        if ((Sint32) (now - q.when) < 0) {
            vimp_queue[n++] = q;
            continue;
        }
        {
            vfx_ent e;
            int d = distmin(u.ux, u.uy, q.x, q.y);
            double amp = (q.heft >= 4 ? 0.9 * q.heft - 1.5 : 0.0) * (d <= 2 ? 1.0 : d >= 10 ? 0.0 : (10 - d) / 8.0);

            memset(&e, 0, sizeof e);
            e.kind = 10, e.x = q.x, e.y = q.y, e.t0 = now, e.dur = 420 + 35 * q.heft;
            e.sub = q.heft;
            e.r = 150, e.g = 135, e.b = 115;
            vfx_list.push_back(e);
            if (amp > vimp_amp * (1.0 - (now - vimp_t0) / 260.0))
                vimp_amp = amp, vimp_t0 = now;
            if (getenv("VULTURE_SELFTEST")) { /* probe: s151 */
                char fpi[700];
                FILE *fi;

                snprintf(fpi, sizeof fpi, "%s\\impacts.txt", getenv("VULTURE_SELFTEST"));
                if ((fi = fopen(fpi, "a")) != 0) {
                    fprintf(fi, "impact heft %d at %d,%d jolt %.1f px\n", q.heft, q.x, q.y, amp);
                    fclose(fi);
                }
            }
            if (q.heft >= 4)
                vulture_sfx_at_vol("impact_heavy", q.x - u.ux, q.y - u.uy, 25 + 8 * q.heft);
            else
                vulture_sfx_at_vol("drop", q.x - u.ux, q.y - u.uy, 35 + 8 * q.heft);
        }
    }
    vimp_queue.resize(n);
}

/* s154: the puff a step kicks up behind it */
static void
vstep_puff(int x, int y, int sub, bool wet)
{
    vfx_ent e;

    if (!vfx_puffs) /* s161 */
        return;
    memset(&e, 0, sizeof e);
    e.kind = 10, e.x = x, e.y = y, e.t0 = SDL_GetTicks(), e.dur = wet ? 420 : 340;
    e.sub = sub;
    if (getenv("VULTURE_SELFTEST")) /* probe: s154 */
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "puff at %d,%d size %d%s\n", x, y, sub,
                          wet ? " (splash)" : "");
    if (wet)
        e.r = 130, e.g = 180, e.b = 235;
    else
        e.r = 150, e.g = 136, e.b = 116;
    vfx_list.push_back(e);
}

/* this frame's camera jolt: a quick, damped vertical bounce */
static void
vimp_offset(int *ox, int *oy)
{
    Uint32 age = SDL_GetTicks() - vimp_t0;
    double k;

    *ox = *oy = 0;
    if (!vfx_jolts || vimp_amp <= 0.0 || age > 260) /* s161 */
        return;
    /* s152: (double) first - "-age" on an unsigned age wrapped to ~4e9,
       exp() gave infinity, and the map was drawn 2^31 px away: SDL crashed */
    k = vimp_amp * exp(-(double) age / 70.0);
    if (!(k > -40.0 && k < 40.0))
        k = 0.0; /* belt and braces: never a wild offset */
    *oy = (int) (k * sin(age / 260.0 * 3.14159265358979 * 5.0));
    *ox = (int) (0.35 * k * sin(age / 260.0 * 3.14159265358979 * 7.0 + 1.0));
}

static void
vfx_hit(int x, int y, int amount, Uint8 r, Uint8 g, Uint8 b, bool blood)
{
    vfx_ent e;
    int k;

    memset(&e, 0, sizeof e);
    e.kind = 1, e.x = x, e.y = y, e.t0 = SDL_GetTicks(), e.dur = 900;
    snprintf(e.txt, sizeof e.txt, amount < 0 ? "+%d" : "-%d",
             amount < 0 ? -amount : amount);
    e.r = r, e.g = g, e.b = b;
    { /* s146: stack above a crit word or another number on this square */
        size_t q;

        for (q = 0; q < vfx_list.size(); q++)
            if (vfx_list[q].kind == 1 && vfx_list[q].x == x && vfx_list[q].y == y
                && e.t0 - vfx_list[q].t0 < 700 && vfx_list[q].sub >= e.sub)
                e.sub = vfx_list[q].sub + 1;
        if (e.sub > 3)
            e.sub = 3;
    }
    vfx_list.push_back(e);
    if (!blood)
        return;
    vst_add(x, y, 1); /* s93: the floor remembers */
    memset(&e, 0, sizeof e);
    e.kind = 2, e.x = x, e.y = y, e.t0 = SDL_GetTicks(), e.dur = 500;
    for (k = 0; k < 10; k++) {
        e.vx[k] = (float) (rand() % 140 - 70);
        e.vy[k] = (float) (-(rand() % 110) - 40);
    }
    vfx_list.push_back(e);
}

/* compare HP with last frame; spawn numbers/blood for changes */
static void
vfx_scan(void)
{
    struct monst *m;
    int hp = Upolyd ? u.mh : u.uhp, poly = Upolyd ? 1 : 0;

    if (!on_level(&vfx_lev, &u.uz)) {
        vfx_lev = u.uz;
        vfx_hp.clear();
        vfx_list.clear();
        vfx_hero_hp = -1;
    }
    if (vfx_hero_hp >= 0 && poly == vfx_hero_poly && hp != vfx_hero_hp && hp > 0) {
        if (hp < vfx_hero_hp) {
            vfx_hit(u.ux, u.uy, vfx_hero_hp - hp, 255, 70, 60, true);
            vhurt_flash(vfx_hero_hp - hp, Upolyd ? u.mhmax : u.uhpmax); /* s136 */
            /* s129: a hit worth a quarter of your max HP lands hard */
            if ((vfx_hero_hp - hp) * 4 >= (Upolyd ? u.mhmax : u.uhpmax)) {
                vfx_burst(u.ux, u.uy, 255, 60, 50);
                vhitstop_ms = 55;
            }
        }
        else
            vfx_hit(u.ux, u.uy, -(hp - vfx_hero_hp), 90, 230, 90, false);
    }
    vfx_hero_hp = hp, vfx_hero_poly = poly;
    for (m = fmon; m; m = m->nmon) {
        std::map<unsigned, int>::iterator it;

        /* s99: seen, not merely sensed (telepathy doesn't show blood) */
        if (DEADMONSTER(m) || !canseemon(m))
            continue;
        it = vfx_hp.find(m->m_id);
        if (it != vfx_hp.end() && m->mhp < it->second) {
            int dmg = it->second - m->mhp;
            /* s129: a blow taking 40% of its max HP is a "big hit":
               gold number, impact burst, hit-stop */
            bool big = dmg >= 4 && dmg * 5 >= m->mhpmax * 2;

            vfx_hit(m->mx, m->my, dmg, 255, big ? 205 : 245, big ? 70 : 210,
                    !noncorporeal(m->data));
            if (big) {
                vfx_burst(m->mx, m->my, 255, 235, 190);
                vhitstop_ms = 70;
                vulture_sfx("hit_big"); /* s131 */
            }
        }
        vfx_hp[m->m_id] = m->mhp;
    }
}

static bool
vfx_alive(void)
{
    Uint32 now = SDL_GetTicks();
    size_t i, n = 0;

    for (i = 0; i < vfx_list.size(); i++)
        if (now - vfx_list[i].t0 < vfx_list[i].dur)
            vfx_list[n++] = vfx_list[i];
    vfx_list.resize(n);
    return n > 0;
}

static void
vfx_square_screen(int j, int i, int cx, int cy, int vx, int vy, int *sx, int *sy)
{
    int va, vb;

    v_rot(j - vx, i - vy, 4 - vulture_view_rot, &va, &vb);
    *sx = cx + V_MAP_XMOD * (va - vb);
    *sy = cy + V_MAP_YMOD * (va + vb);
}

/* a small filled square, clipped */
static void
vfx_dot(int x, int y, int sz, Uint32 col)
{
    SDL_Rect r;

    r.x = x - sz / 2, r.y = y - sz / 2, r.w = sz, r.h = sz;
    SDL_FillRect(vulture_screen, &r, col);
}

/* s98: a soft, round blood droplet (3 sizes), alpha-faded at the edge */
static void
vfx_droplet(int x, int y, int size, int alpha)
{
    static SDL_Surface *drop[3];
    SDL_Rect d;

    if (size < 0 || size > 2)
        size = 0;
    if (!drop[size]) {
        int r = 2 + size, dim = r * 2 + 1, xx, yy;

        drop[size] = SDL_CreateRGBSurfaceWithFormat(0, dim, dim, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
        if (!drop[size])
            return;
        SDL_LockSurface(drop[size]);
        for (yy = 0; yy < dim; yy++)
            for (xx = 0; xx < dim; xx++) {
                double dd = sqrt((double) ((xx - r) * (xx - r) + (yy - r) * (yy - r))) / (r + 0.5);
                double a = dd >= 1.0 ? 0.0 : 1.0 - dd * dd;

                ((Uint32 *) ((Uint8 *) drop[size]->pixels + yy * drop[size]->pitch))[xx]
                    = SDL_MapRGBA(drop[size]->format, (Uint8) (150 - 40 * dd), 8, 10,
                                  (Uint8) (255 * a));
            }
        SDL_UnlockSurface(drop[size]);
        SDL_SetSurfaceBlendMode(drop[size], SDL_BLENDMODE_BLEND);
    }
    d.x = x - drop[size]->w / 2, d.y = y - drop[size]->h / 2;
    d.w = drop[size]->w, d.h = drop[size]->h;
    SDL_SetSurfaceAlphaMod(drop[size], (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
    SDL_BlitSurface(drop[size], NULL, vulture_screen, &d);
}

/* s108: a soft round glow dot in any colour (one cached white sprite,
   tinted and faded per call) */
static void
vfx_glowdot(int x, int y, Uint8 r, Uint8 g, Uint8 b, int alpha)
{
    static SDL_Surface *dot = NULL;
    SDL_Rect d;

    if (!dot) {
        int R = 4, dim = 2 * R + 1, xx, yy;

        dot = SDL_CreateRGBSurfaceWithFormat(0, dim, dim, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!dot)
            return;
        SDL_LockSurface(dot);
        for (yy = 0; yy < dim; yy++)
            for (xx = 0; xx < dim; xx++) {
                double dd = sqrt((double) ((xx - R) * (xx - R) + (yy - R) * (yy - R))) / (R + 0.5);
                double a = dd >= 1.0 ? 0.0 : 1.0 - dd * dd;

                ((Uint32 *) ((Uint8 *) dot->pixels + yy * dot->pitch))[xx]
                    = SDL_MapRGBA(dot->format, 255, 255, 255, (Uint8) (255 * a));
            }
        SDL_UnlockSurface(dot);
        SDL_SetSurfaceBlendMode(dot, SDL_BLENDMODE_BLEND);
    }
    SDL_SetSurfaceColorMod(dot, r, g, b);
    SDL_SetSurfaceAlphaMod(dot, (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
    d.x = x - dot->w / 2, d.y = y - dot->h / 2, d.w = dot->w, d.h = dot->h;
    SDL_BlitSurface(dot, NULL, vulture_screen, &d);
}

/* s108: Dota-style click marker on map square (x, y).  s117: its shape
   and colour say what the click sends the hero to do */
enum { VCK_MOVE, VCK_ATTACK, VCK_ALLY, VCK_ITEM, VCK_DOOR, VCK_STAIRS,
       VCK_DRINK, VCK_BLOCKED /* s148 */ };
static bool vmv_pass(int x, int y); /* s148 */

static void
vfx_click(int x, int y, int sub, bool up)
{
    static const Uint8 col[8][3] = {
        { 90, 255, 120 },  /* move: green */
        { 255, 70, 60 },   /* attack: red */
        { 110, 180, 255 }, /* pet / peaceful: blue */
        { 255, 205, 70 },  /* item: gold */
        { 255, 145, 50 },  /* door: amber */
        { 190, 120, 255 }, /* stairs: violet */
        { 80, 225, 255 },  /* fountain / sink: cyan */
        { 70, 120, 255 },  /* s148: can't move in there: blue */
    };
    vfx_ent e;

    if (!isok(x, y) || sub < 0 || sub > VCK_BLOCKED)
        return;
    memset(&e, 0, sizeof e);
    e.kind = 3, e.x = x, e.y = y, e.t0 = SDL_GetTicks();
    e.dur = sub == VCK_STAIRS ? 750 : (sub == VCK_ITEM || sub == VCK_DRINK) ? 650 : 520;
    e.sub = sub, e.mid = up ? 1 : 0;
    e.r = col[sub][0], e.g = col[sub][1], e.b = col[sub][2];
    vfx_list.push_back(e);
    { /* s131: each order has its own sound */
        static const char *const mk[8] = { "mk_move", "mk_attack", "mk_ally", "mk_item",
                                           "mk_door", "mk_stairs", "mk_drink", "mk_move" };
        vulture_sfx(mk[sub]);
    }
}

#define VCK_PI 3.14159265358979

/* a flattened ring of glow dots (with a dark under-ring so it reads on
   bright floors) */
static void
vfx_ring(int sx, int sy, double rad, const vfx_ent &e, int alpha)
{
    int k, n = rad > 14 ? 32 : 20;

    for (k = 0; k < n; k++) {
        double a = 2 * VCK_PI * k / n;
        int px = sx + (int) (rad * cos(a)), py = sy + (int) (rad * 0.5 * sin(a));

        vfx_glowdot(px, py + 1, 0, 0, 0, alpha / 2);
        vfx_glowdot(px, py, e.r, e.g, e.b, alpha);
    }
}

/* a dotted line */
static void
vfx_dline(double x0, double y0, double x1, double y1, const vfx_ent &e, int alpha)
{
    double len = sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    int k, n = (int) (len / 2.2) + 1;

    for (k = 0; k <= n; k++) {
        double f = (double) k / n;

        vfx_glowdot((int) (x0 + (x1 - x0) * f), (int) (y0 + (y1 - y0) * f),
                    e.r, e.g, e.b, alpha);
    }
}

/* draw one click marker (flattened for the iso floor), fading out */
static void
vfx_draw_click(const vfx_ent &e, double age, int sx, int sy)
{
    double pop = age < 0.18 ? age / 0.18 : 1.0;
    double ease = 1 - (1 - age) * (1 - age);
    int alpha = (int) (255 * (age < 0.6 ? 1.0 : (1.0 - age) / 0.4));
    int k, j;

    switch (e.sub) {
    case VCK_ATTACK: {
        /* a red ring snapping shut, crosshair ticks spinning in, then an X */
        double rad = 34 - 24 * ease, rot = (1 - ease) * 1.4;

        vfx_ring(sx, sy, rad, e, alpha);
        for (k = 0; k < 4; k++) {
            double a = rot + k * VCK_PI / 2, d = 42 - 26 * ease;

            vfx_dline(sx + d * cos(a), sy + d * 0.5 * sin(a),
                      sx + (d + 9) * cos(a), sy + (d + 9) * 0.5 * sin(a), e, alpha);
        }
        if (age > 0.25) {
            double s = 7 * (age < 0.4 ? (age - 0.25) / 0.15 : 1.0);

            vfx_dline(sx - s, sy - s * 0.6, sx + s, sy + s * 0.6, e, alpha);
            vfx_dline(sx - s, sy + s * 0.6, sx + s, sy - s * 0.6, e, alpha);
        }
        break;
    }
    case VCK_ALLY:
        /* two soft blue ripples spreading out */
        for (k = 0; k < 2; k++) {
            double a2 = (age - 0.22 * k) / (1.0 - 0.22 * k);

            if (a2 > 0)
                vfx_ring(sx, sy, 8 + 28 * a2, e, (int) (alpha * (1.0 - a2 * 0.6)));
        }
        vfx_glowdot(sx, sy, e.r, e.g, e.b, alpha);
        break;
    case VCK_ITEM: {
        /* gold ring, sparkles rising, a bobbing "grab it" arrow */
        double ay = sy - 34 + 5 * sin(age * 4 * VCK_PI);

        vfx_ring(sx, sy, 20 - 6 * ease, e, alpha);
        for (k = 0; k < 6; k++) {
            double a = k * VCK_PI / 3 + age * 2;

            vfx_glowdot(sx + (int) (18 * cos(a)), sy + (int) (9 * sin(a) - 38 * age),
                        255, 245, 200, alpha * 2 / 3);
        }
        vfx_dline(sx, ay - 12, sx, ay, e, alpha);
        vfx_dline(sx - 6, ay - 6, sx, ay, e, alpha);
        vfx_dline(sx + 6, ay - 6, sx, ay, e, alpha);
        break;
    }
    case VCK_DOOR: {
        /* a door frame whose two leaves swing apart */
        double gap = 3 + 12 * ease;

        vfx_ring(sx, sy, 22 - 6 * ease, e, alpha / 2);
        vfx_dline(sx - gap, sy - 30, sx - gap, sy - 4, e, alpha);
        vfx_dline(sx + gap, sy - 30, sx + gap, sy - 4, e, alpha);
        vfx_dline(sx - 16, sy - 33, sx + 16, sy - 33, e, alpha / 2);
        break;
    }
    case VCK_STAIRS: {
        /* a spiral winding down (or up) the stairs */
        double dir = e.mid ? -1.0 : 1.0;

        for (k = 0; k < 26; k++) {
            double f = k / 26.0, a = f * 4 * VCK_PI + age * 7;
            double r = 30 * (1 - f) * (1 - 0.45 * ease) + 3;

            vfx_glowdot(sx + (int) (r * cos(a)),
                        sy + (int) (r * 0.5 * sin(a) + dir * (f * 14 * ease - 4)),
                        e.r, e.g, e.b, (int) (alpha * (0.4 + 0.6 * (1 - f))));
        }
        break;
    }
    case VCK_DRINK:
        /* droplets fall into a rippling pool */
        vfx_ring(sx, sy, 14 + 10 * age, e, alpha / 2);
        for (j = 0; j < 3; j++) {
            double f = age * 1.7 - j * 0.25;

            if (f > 0 && f < 1)
                vfx_glowdot(sx + (j - 1) * 5, sy - 40 + (int) (40 * f * f),
                            e.r, e.g, e.b, alpha);
            else if (f >= 1 && f < 1.4)
                vfx_ring(sx + (j - 1) * 5, sy, 4 + 14 * (f - 1), e,
                         (int) (alpha * (1.4 - f) / 0.4));
        }
        break;
    default: {
        /* move: a ring that pops then shrinks into the square, four
           chevrons closing in on the diagonals */
        double rad = (age < 0.18 ? 16 + 14 * pop : 30 - 22 * (age - 0.18) / 0.82);

        vfx_ring(sx, sy, rad, e, alpha);
        for (k = 0; k < 4; k++) {
            double a = VCK_PI / 4 + k * VCK_PI / 2;
            double ux = cos(a), uy = sin(a) * 0.5;
            double dist = 46 - 30 * ease, len = 7;

            for (j = -3; j <= 3; j++) {
                double t = fabs((double) j) / 3.0;
                double bx = sx + ux * (dist + len * t), by = sy + uy * (dist + len * t);
                double pxo = -uy * 2, pyo = ux * 0.5 * 2;
                double nrm = sqrt(pxo * pxo + pyo * pyo);

                if (nrm > 0)
                    pxo /= nrm, pyo /= nrm;
                vfx_glowdot((int) (bx + pxo * j * 2.2), (int) (by + pyo * j * 2.2),
                            e.r, e.g, e.b, alpha);
            }
        }
        break;
    }
    }
}

/* s109/s113: Dota-style barks - the hero says a short line for what
   they're doing (vulture_bark.h).  Each kind has its own lines, a chance
   and a cooldown; chatty kinds also share a global cooldown so the hero
   never rattles on.  Shown above their head; spoken with `voices`. */
#include "vulture_bark.h"
extern "C" void vulture_tts_speak(const char *, int, int, int);
extern "C" void vulture_tts_speak_voice(const char *, int, double, int);
extern "C" void vulture_tts_speak_speaker(const char *, int, double, int, double, int);

/* s114: the hero's bark voice by role and gender (Piper voice + pitch),
   nudged by race - so a Barbarian growls and a Tourist chirps */
static void
vbark_voice(int *voice, double *factor, int *speaker)
{
    /* s116: ms/fs = LibriTTS speakers picked by measured pitch (male
       97-140 Hz, female 181-251 Hz; deep for fighters, high for Tourists) */
    static const struct { short pm; signed char mv, fv; float mf, ff; short ms, fs; } tab[] = {
        /* role            male/female voice+pitch (fallback)  speakers */
        { PM_ARCHEOLOGIST, 0, 1, 1.02f, 1.03f, 520, 290 },
        { PM_BARBARIAN,    2, 1, 0.88f, 0.87f, 380, 260 },
        { PM_CAVE_DWELLER, 2, 1, 0.86f, 0.86f, 400, 480 },
        { PM_HEALER,       0, 1, 1.08f, 1.10f, 330,  10 },
        { PM_KNIGHT,       0, 1, 0.94f, 0.95f,  90,   0 },
        { PM_MONK,         0, 1, 1.00f, 1.00f, 410, 860 },
        { PM_CLERIC,       0, 1, 0.97f, 0.97f, 800, 440 },
        { PM_RANGER,       2, 1, 1.04f, 0.98f, 700, 710 },
        { PM_ROGUE,        0, 1, 1.12f, 1.12f,  70,  30 },
        { PM_SAMURAI,      0, 1, 0.91f, 0.92f, 500, 160 },
        { PM_TOURIST,      0, 1, 1.15f, 1.18f, 110,  60 },
        { PM_VALKYRIE,     2, 1, 0.98f, 0.90f,  50,  80 },
        { PM_WIZARD,       0, 1, 1.05f, 1.05f, 660, 220 },
    };
    size_t i;
    double f = 1.0;
    int v = flags.female ? 1 : 0;

    *speaker = -1;
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (Role_if(tab[i].pm)) {
            v = flags.female ? tab[i].fv : tab[i].mv;
            f = flags.female ? tab[i].ff : tab[i].mf;
            *speaker = flags.female ? tab[i].fs : tab[i].ms;
            break;
        }
    if (Race_if(PM_DWARF))
        f *= 0.94;
    else if (Race_if(PM_ORC))
        f *= 0.92;
    else if (Race_if(PM_GNOME))
        f *= 1.10;
    else if (Race_if(PM_ELF))
        f *= 1.04;
    *voice = v, *factor = f;
}

struct vbark_kind {
    int chance;          /* percent */
    Uint32 gap;          /* ms since this kind last spoke */
    bool urgent;         /* ignores the shared cooldown */
    const char *lines[10];
};
static const vbark_kind vbark_tab[VB_COUNT] = {
    /* MOVE */    { 55, 3500, false, { "Mhm.", "Moving!", "On my way.", "Going.", "Right.",
                                       "Heading there.", "On it.", "Aye.", "Got it.", "Let's go." } },
    /* ATTACK */  { 35, 4000, false, { "Have at you!", "Attacking!", "Die!", "Take that!",
                                       "For glory!", "Back off!", "Hyah!", "Come on, then!" } },
    /* KILL */    { 60, 2500, false, { "Got 'em.", "Down you go.", "Next!", "That's one.",
                                       "Too easy.", "Stay down.", "Hah!", "Rest now." } },
    /* PICKUP */  { 35, 5000, false, { "Mine.", "Ooh.", "That'll do.", "Useful.", "Shiny.",
                                       "Into the pack.", "Don't mind if I do." } },
    /* DESCEND */ { 80, 2000, true,  { "Going down.", "Deeper we go.", "Down the stairs.",
                                       "Into the dark.", "Lower still." } },
    /* ASCEND */  { 80, 2000, true,  { "Going up.", "Back up.", "Up we go.", "Fresh air, maybe." } },
    /* CAST */    { 40, 4000, false, { "By my will!", "Magic!", "Let it burn!", "Behold!",
                                       "Arcane might!", "Hear me!" } },
    /* NOMANA */  { 100, 1500, true, { "Not enough mana!", "I need more power.",
                                       "My magic's spent.", "Not enough energy!" } },
    /* DOOR */    { 25, 6000, false, { "Open sesame.", "After me.", "Door's open." } },
    /* LOCKED */  { 90, 2500, true,  { "Locked.", "It won't budge.", "Locked tight.",
                                       "Need a key for this." } },
    /* EAT */     { 50, 5000, false, { "Mm, food.", "Dinner time.", "Better than nothing.",
                                       "Chew, chew..." } },
    /* DRINK */   { 50, 5000, false, { "Bottoms up.", "Here goes.", "Cheers.", "Hope this is good." } },
    /* READ */    { 45, 5000, false, { "Let's see...", "What's this say?", "Words of power.",
                                       "Reading it." } },
    /* PRAY */    { 70, 4000, true,  { "Hear me!", "A little help?", "Please...",
                                       "Grant me aid!" } },
    /* LEVELUP */ { 100, 1000, true, { "I feel stronger!", "Getting better.", "Power!",
                                       "Level up!", "Stronger now." } },
    /* LOWHP */   { 100, 8000, true, { "I need healing!", "I'm hurt bad!", "Not like this...",
                                       "Ugh... careful.", "I can't take much more!" } },
    /* HUNGRY */  { 100, 8000, true, { "I'm starving.", "Need to eat.", "So hungry...",
                                       "My stomach's growling." } },
    /* WEAR */    { 30, 6000, false, { "Suits me.", "Snug.", "Better protected." } },
    /* WIELD */   { 40, 5000, false, { "Ready.", "This'll do.", "Armed." } },
    /* THROW */   { 25, 5000, false, { "Catch!", "Heads up!", "Fore!" } },
    /* ZAP */     { 35, 5000, false, { "Zap!", "Let's see what this does.", "Fire!" } },
};
static Uint32 vbark_kind_last[VB_COUNT];
static int vbark_kind_prev[VB_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static Uint32 vbark_last = 0;

/* s117: say a line in the hero's voice (s114/s116 voices) */
static void
vbark_speak(const char *line)
{
    int voice, speaker;
    double factor, rf;

    if (!iflags.voices || Deaf)
        return;
    vbark_voice(&voice, &factor, &speaker);
    /* a real speaker needs only the race nudge; the full factor is
       for faking voices out of the three single-speaker models */
    rf = Race_if(PM_DWARF) ? 0.96 : Race_if(PM_ORC) ? 0.95
         : Race_if(PM_GNOME) ? 1.07 : Race_if(PM_ELF) ? 1.03 : 1.0;
    if (speaker >= 0)
        vulture_tts_speak_speaker(line, speaker, rf, voice, factor,
                                  flags.female ? 1 : 0);
    else
        vulture_tts_speak_voice(line, voice, factor, flags.female ? 1 : 0);
}

extern "C" void
vulture_bark(int kind)
{
    Uint32 now = SDL_GetTicks();
    const vbark_kind *bk;
    int n, k;
    vfx_ent e;

    if (kind < 0 || kind >= VB_COUNT || !levwin || vulture_windows_inited != TRUE)
        return;
    bk = &vbark_tab[kind];
    if (Hallucination || Strangled || u.uswallow || Underwater
        || program_state.gameover)
        return;
    if (vbark_kind_last[kind] && now - vbark_kind_last[kind] < bk->gap)
        return;
    if (!bk->urgent && vbark_last && now - vbark_last < 2500)
        return;
    if (rn2(100) >= bk->chance)
        return;
    for (n = 0; n < 10 && bk->lines[n]; n++)
        ;
    if (!n)
        return;
    do
        k = rn2(n);
    while (k == vbark_kind_prev[kind] && n > 1);
    vbark_kind_prev[kind] = k;
    vbark_kind_last[kind] = vbark_last = now;
    memset(&e, 0, sizeof e);
    e.kind = 4, e.x = u.ux, e.y = u.uy, e.t0 = now, e.dur = 1400;
    snprintf(e.txt, sizeof e.txt, "%s", bk->lines[k]);
    e.r = 245, e.g = 240, e.b = 220;
    vfx_list.push_back(e);
    vbark_speak(bk->lines[k]);
}

static void
vbark_move(void)
{
    vulture_bark(VB_MOVE);
}

/* s117: speech bubbles - the hero's muttering and what monsters say
   float over their heads instead of filling the message window */
static std::string vsay_txt[16];
static int vsay_next = 0;

extern "C" int
vulture_say_above(unsigned mid, int x, int y, const char *text)
{
    Uint32 now = SDL_GetTicks();
    size_t i, len;
    vfx_ent e;

    if (!text || !*text || !levwin || vulture_windows_inited != TRUE
        || !isok(x, y))
        return 0;
    for (i = 0; i < vfx_list.size(); i++)
        if (vfx_list[i].kind == 5 && vfx_list[i].mid == mid)
            vfx_list[i].t0 = now - 10, vfx_list[i].dur = 1; /* one each */
    len = strlen(text);
    memset(&e, 0, sizeof e);
    e.kind = 5, e.x = x, e.y = y, e.t0 = now, e.mid = mid;
    e.dur = (Uint32) (1800 + 65 * len);
    if (e.dur > 9000)
        e.dur = 9000;
    e.sub = vsay_next;
    vsay_txt[vsay_next] = text;
    vsay_next = (vsay_next + 1) % 16;
    if (mid)
        e.r = 205, e.g = 230, e.b = 255;
    else
        e.r = 255, e.g = 244, e.b = 214;
    vfx_list.push_back(e);
    return 1;
}

/* the hero says something: a bubble plus their bark voice */
extern "C" int
vulture_hero_say(const char *text)
{
    if (!vulture_say_above(0, u.ux, u.uy, text))
        return 0;
    vbark_speak(text);
    return 1;
}

static void
vsay_draw(const vfx_ent &e, double age, int cx, int cy, int vx, int vy)
{
    const std::string &s = vsay_txt[e.sub];
    std::vector<std::string> lines;
    std::string cur, word;
    int maxw = vulture_text_length(V_FONT_SMALL, "abcdefghijklmnopqrstuvwxyzabcd");
    int lh = vulture_get_lineheight(V_FONT_SMALL), bw = 0, bh, bx, by, sx, sy;
    int x = e.x, y = e.y, fade;
    size_t i;
    SDL_Surface *box;
    SDL_Rect d;

    if (!e.mid) {
        x = u.ux, y = u.uy;
    } else {
        struct monst *m;

        for (m = fmon; m; m = m->nmon)
            if (m->m_id == e.mid)
                break;
        if (!m || DEADMONSTER(m) || !canspotmon(m))
            return;
        x = m->mx, y = m->my;
    }
    for (i = 0; i <= s.size(); i++) { /* word wrap */
        if (i == s.size() || s[i] == ' ') {
            std::string trial = cur.empty() ? word : cur + " " + word;

            if (!cur.empty() && vulture_text_length(V_FONT_SMALL, trial) > maxw) {
                lines.push_back(cur);
                cur = word;
            } else {
                cur = trial;
            }
            word.clear();
        } else {
            word += s[i];
        }
    }
    if (!cur.empty())
        lines.push_back(cur);
    if (lines.empty())
        return;
    for (i = 0; i < lines.size(); i++) {
        int w = vulture_text_length(V_FONT_SMALL, lines[i]);

        if (w > bw)
            bw = w;
    }
    bw += 14, bh = (int) lines.size() * lh + 8;
    vfx_square_screen(x, y, cx, cy, vx, vy, &sx, &sy);
    bx = sx - bw / 2, by = sy - 100 - bh;
    if (!e.mid) /* above a bark line, if one is showing */
        for (i = 0; i < vfx_list.size(); i++)
            if (vfx_list[i].kind == 4
                && SDL_GetTicks() - vfx_list[i].t0 < vfx_list[i].dur)
                by -= lh + 6;
    if (bx < 4)
        bx = 4;
    if (bx + bw > vulture_screen->w - 4)
        bx = vulture_screen->w - 4 - bw;
    if (by < 4)
        by = 4;
    fade = age > 0.92 ? (int) (255 * (1.0 - age) / 0.08) : 255;
    box = SDL_CreateRGBSurfaceWithFormat(0, bw, bh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (box) {
        SDL_Rect r;
        Uint32 edge = SDL_MapRGBA(box->format, e.r, e.g, e.b, (Uint8) (fade * 3 / 5));

        SDL_FillRect(box, NULL, SDL_MapRGBA(box->format, 14, 12, 18, (Uint8) (fade * 2 / 3)));
        r.x = 0, r.y = 0, r.w = bw, r.h = 1;
        SDL_FillRect(box, &r, edge);
        r.y = bh - 1;
        SDL_FillRect(box, &r, edge);
        r.y = 0, r.w = 1, r.h = bh;
        SDL_FillRect(box, &r, edge);
        r.x = bw - 1;
        SDL_FillRect(box, &r, edge);
        SDL_SetSurfaceBlendMode(box, SDL_BLENDMODE_BLEND);
        d.x = bx, d.y = by, d.w = bw, d.h = bh;
        SDL_BlitSurface(box, NULL, vulture_screen, &d);
        SDL_FreeSurface(box);
    }
    vfx_glowdot(sx, by + bh + 3, e.r, e.g, e.b, fade * 2 / 3);
    vfx_glowdot(sx, by + bh + 9, e.r, e.g, e.b, fade / 3);
    if (fade < 60)
        return;
    for (i = 0; i < lines.size(); i++) {
        int w = vulture_text_length(V_FONT_SMALL, lines[i]);

        vulture_put_text_shadow(V_FONT_SMALL, lines[i], vulture_screen,
                                bx + (bw - w) / 2, by + 4 + (int) i * lh,
                                SDL_MapRGB(vulture_screen->format, e.r, e.g, e.b),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    }
}

/* called from the map click handler: what is the hero being sent to do? */
static void
vfx_click_at(int x, int y)
{
    int g, sub = VCK_MOVE, cm = -1;
    bool up = false;
    struct monst *m;

    if (!isok(x, y) || u_at(x, y) || !levl[x][y].seenv)
        return;
    g = glyph_at(x, y);
    if (glyph_is_cmap(g))
        cm = glyph_to_cmap(g);
    if (glyph_is_monster(g) || glyph_is_invisible(g)) {
        m = m_at(x, y);
        sub = (glyph_is_pet(g)
               || (m && canspotmon(m) && (m->mtame || m->mpeaceful)))
                  ? VCK_ALLY : VCK_ATTACK;
    } else if (glyph_is_object(g) && glyph_to_obj(g) != BOULDER) {
        sub = VCK_ITEM;
    } else if (cm == S_vcdoor || cm == S_hcdoor) {
        sub = VCK_DOOR;
    } else if (cm == S_dnstair || cm == S_dnladder || cm == S_brdnstair
               || cm == S_brdnladder) {
        sub = VCK_STAIRS;
    } else if (cm == S_upstair || cm == S_upladder || cm == S_brupstair
               || cm == S_brupladder) {
        sub = VCK_STAIRS, up = true;
    } else if (cm == S_fountain || cm == S_sink) {
        sub = VCK_DRINK;
    } else if (!vmv_pass(x, y) || glyph_is_statue(g)) {
        sub = VCK_BLOCKED; /* s148: a wall, boulder, statue, water... */
    }
    vfx_click(x, y, sub, up);
    if (sub == VCK_MOVE)
        vbark_move(); /* s109 */
}

static bool vmagic_light(int tt, int x, int y, double t, double *c, double *pu);

/*
 * IMPLEMENTATION_LOG s160: atmosphere.  A few dozen particles over the map,
 * by area: dust motes drifting and twinkling in the dungeon, drips falling
 * (and a few spores) in the Gnomish Mines, embers rising in Gehennom.  Each
 * particle's place is a formula of time and its index - no state, nearly
 * free - and they fade towards the map's edges.
 */
static void
vambient_draw(int ax, int ay, int w, int h)
{
    double t = SDL_GetTicks() / 1000.0;
    int k, kind, n;

    if (!vfx_ambient || !u.ux || u.uswallow || Underwater || w < 64 || h < 64)
        return;
    kind = Inhell ? 2 : In_mines(&u.uz) ? 1 : 0;
    n = kind == 2 ? 46 : kind == 1 ? 34 : 40;
    for (k = 0; k < n; k++) {
        double s1 = fmod(k * 0.6180339 + 0.13, 1.0), s2 = fmod(k * 0.7548777 + 0.29, 1.0);
        double s3 = fmod(k * 0.5698403 + 0.71, 1.0), x, y, edge;
        int px, py, al;

        if (kind == 2) { /* embers: rise, sway, flare and die */
            double ph = fmod(t * (0.10 + 0.08 * s3) + s2, 1.0);

            x = ax + w * s1 + 18 * sin(t * (0.9 + s3) + k);
            y = ay + h * (1.0 - ph);
            al = (int) (200 * sin(3.14159265358979 * ph) * (0.6 + 0.4 * sin(t * 7 + k)));
            px = (int) x, py = (int) y;
            edge = fmin(fmin(x - ax, ax + w - x), fmin(y - ay, ay + h - y)) / 80.0;
            if (edge < 1.0)
                al = (int) (al * (edge < 0 ? 0 : edge));
            if (al > 6)
                vfx_glowdot(px, py, 255, (Uint8) (110 + 60 * s3), 40, al);
        } else if (kind == 1 && k < 20) { /* drips: fall as short streaks */
            double ph = fmod(t * (0.45 + 0.3 * s3) + s2, 1.0);

            x = ax + w * s1, y = ay + h * ph;
            al = (int) (110 * (ph < 0.85 ? 1.0 : (1.0 - ph) / 0.15));
            px = (int) x, py = (int) y;
            if (al > 6) {
                vfx_glowdot(px, py, 140, 180, 230, al);
                vfx_glowdot(px, py - 5, 140, 180, 230, al / 2);
                vfx_glowdot(px, py - 10, 140, 180, 230, al / 4);
            }
        } else { /* dust (or Mines spores): slow drift, a soft twinkle */
            x = ax + fmod(w * s1 + t * (6 + 8 * s3) + 25 * sin(t * 0.21 + k), (double) w);
            y = ay + fmod(h * s2 + t * (kind == 1 ? -4.0 : 3.0) * (0.5 + s3) + h, (double) h);
            al = (int) ((kind == 1 ? 70 : 55) * (0.55 + 0.45 * sin(t * (0.8 + s3) + k * 1.7)));
            edge = fmin(fmin(x - ax, ax + w - x), fmin(y - ay, ay + h - y)) / 90.0;
            if (edge < 1.0)
                al = (int) (al * (edge < 0 ? 0 : edge));
            px = (int) x, py = (int) y;
            if (al > 5) {
                if (kind == 1)
                    vfx_glowdot(px, py, 150, 210, 140, al);
                else
                    vfx_glowdot(px, py, 230, 220, 195, al);
            }
        }
    }
}

/* s154: the sprite of a portal or magical trap throbs: drawn again on top,
   additively, in the trap's drifting colour, on the same beats as its light */
static void
vmagic_sprite(int j, int i, int x, int y, int tile_id)
{
    vulture_tile *tile;
    double t = SDL_GetTicks() / 1000.0, c[3], pu;
    int g, tt, al;

    if (tile_id == V_TILE_NONE || tile_id < 0 || Hallucination)
        return;
    g = glyph_at(j, i);
    if (!glyph_is_trap(g))
        return;
    tt = glyph_to_trap(g);
    if (!vmagic_light(tt, j, i, t, c, &pu) || !(tile = vulture_get_tile(tile_id)) || !tile->graphic)
        return;
    {
        double s1 = sin(t * 2.1 + j * 1.3), s2 = sin(t * 5.3 + i * 1.7), sg = sin(t * 0.83 + j * 7.1 + i * 3.3);
        double surge = sg > 0 ? pow(sg, 14.0) : 0.0;
        double k = 0.5 + 0.30 * s1 + 0.15 * s2 + 0.6 * surge;

        al = (int) ((tt == MAGIC_PORTAL ? 150 : 110) * (k < 0 ? 0 : k));
    }
    if (al <= 4)
        return;
    SDL_SetSurfaceBlendMode(tile->graphic, SDL_BLENDMODE_ADD);
    SDL_SetSurfaceColorMod(tile->graphic, (Uint8) (255 * c[0]), (Uint8) (255 * c[1]), (Uint8) (255 * c[2]));
    SDL_SetSurfaceAlphaMod(tile->graphic, (Uint8) (al > 255 ? 255 : al));
    vulture_put_img(x + tile->xmod, y + tile->ymod, tile->graphic);
    SDL_SetSurfaceAlphaMod(tile->graphic, 255);
    SDL_SetSurfaceColorMod(tile->graphic, 255, 255, 255);
    SDL_SetSurfaceBlendMode(tile->graphic, SDL_BLENDMODE_BLEND);
}

/* s149: motes of magical energy over the magical traps and portals you can
   see - rising and swaying over traps, circling a portal */
static void
vmagic_motes(int cx, int cy, int vx, int vy)
{
    double t = SDL_GetTicks() / 1000.0;
    int x, y, k, sx, sy;

    if (!u.ux || Hallucination)
        return;
    for (x = vx - 16; x <= vx + 16; x++)
        for (y = vy - 16; y <= vy + 16; y++) {
            int g, tt, n;
            double c[3], pu;

            if (!isok(x, y) || !cansee(x, y))
                continue;
            g = glyph_at(x, y);
            if (!glyph_is_trap(g))
                continue;
            tt = glyph_to_trap(g);
            if (!vmagic_light(tt, x, y, t, c, &pu))
                continue;
            vfx_square_screen(x, y, cx, cy, vx, vy, &sx, &sy);
            n = tt == MAGIC_PORTAL ? 14 : 8;
            for (k = 0; k < n; k++) {
                double seed = k * 0.618 + x * 0.37 + y * 0.71;
                double ph = fmod(t * (tt == MAGIC_PORTAL ? 0.35 : 0.5) + seed, 1.0);
                double fade = sin(3.14159265358979 * ph);
                int px, py, al;

                if (tt == MAGIC_PORTAL) { /* a slow swirl, lifting as it goes */
                    double a = t * 1.6 + k * 6.2832 / n;
                    double rr = 20 - 8 * ph;

                    px = sx + (int) (rr * cos(a));
                    py = sy - 8 + (int) (rr * 0.5 * sin(a)) - (int) (26 * ph);
                } else { /* rising, swaying */
                    px = sx + (int) (12 * sin(seed * 9.0) + 4 * sin(t * 2.3 + k));
                    py = sy - 4 - (int) (40 * ph);
                }
                al = (int) (fade * (150 + 90 * pu));
                vfx_glowdot(px, py, (Uint8) (255 * c[0]), (Uint8) (255 * c[1]),
                            (Uint8) (255 * c[2]), al > 255 ? 255 : al);
            }
        }
}

static void
vfx_draw(int cx, int cy, int vx, int vy)
{
    vmagic_motes(cx, cy, vx, vy); /* s149 */
    Uint32 now = SDL_GetTicks();
    size_t i;
    int k, sx, sy;

    /* swing arc across the target of a lunge */
    if (vanim_kind == 1 && vanim_t > 0.3 && vanim_t < 0.9) {
        double u = (vanim_t - 0.3) / 0.6, base, a;
        int dx, dy;

        vfx_square_screen(vanim_tx, vanim_ty, cx, cy, vx, vy, &sx, &sy);
        vtw_proj(vanim_tx - vanim_ax, vanim_ty - vanim_ay, &dx, &dy);
        base = atan2((double) dy, (double) dx);
        for (k = 0; k <= 14; k++) {
            double f = k / 14.0;

            if (f > u)
                break;
            a = base - 1.1 + 2.2 * f;
            vfx_dot(sx + (int) (30 * cos(a)), sy - 26 + (int) (18 * sin(a)),
                    2 + (int) (4 * sin(3.14159265358979 * f) * (1.0 - u * 0.5)),
                    SDL_MapRGB(vulture_screen->format, 255, 250, 225));
        }
    }

    if (u.ux && vsr_kind && now - vsr_t < 1100) { /* s144: searching vs resting */
        int hx2, hy2, k2;
        double ph2 = now / 160.0;
        vfx_ent ic;

        memset(&ic, 0, sizeof ic);
        vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &hx2, &hy2);
        hx2 += 22, hy2 -= 96;
        if (vsr_kind == 1) { /* a magnifying glass, sweeping */
            int ox = (int) (3 * sin(ph2));

            ic.r = 150, ic.g = 220, ic.b = 255;
            for (k2 = 0; k2 < 16; k2++) {
                double a = 2 * 3.14159265358979 * k2 / 16;

                vfx_glowdot(hx2 + ox + (int) (6 * cos(a)), hy2 + (int) (6 * sin(a)), 150, 220, 255, 230);
            }
            vfx_dline(hx2 + ox + 4, hy2 + 4, hx2 + ox + 10, hy2 + 10, ic, 240);
            vulture_put_text_shadow(V_FONT_SMALL, "searching", vulture_screen, hx2 - 20, hy2 + 12,
                                    SDL_MapRGB(vulture_screen->format, 150, 220, 255),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        } else { /* an hourglass */
            ic.r = 255, ic.g = 225, ic.b = 160;
            vfx_dline(hx2 - 5, hy2 - 7, hx2 + 5, hy2 - 7, ic, 230);
            vfx_dline(hx2 - 5, hy2 + 7, hx2 + 5, hy2 + 7, ic, 230);
            vfx_dline(hx2 - 5, hy2 - 7, hx2 + 5, hy2 + 7, ic, 230);
            vfx_dline(hx2 + 5, hy2 - 7, hx2 - 5, hy2 + 7, ic, 230);
            vfx_glowdot(hx2, hy2 + 5 - (int) (fmod(now / 300.0, 1.0) * 4), 255, 225, 160, 255);
            vulture_put_text_shadow(V_FONT_SMALL, "resting", vulture_screen, hx2 - 16, hy2 + 12,
                                    SDL_MapRGB(vulture_screen->format, 255, 225, 160),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        }
    }
    if (!Hallucination) { /* s137: you can see who is asleep at a glance */
        struct monst *zm;

        for (zm = fmon; zm; zm = zm->nmon) {
            if (DEADMONSTER(zm) || !canseemon(zm) || !(zm->msleeping || !zm->mcanmove))
                continue;
            vfx_square_screen(zm->mx, zm->my, cx, cy, vx, vy, &sx, &sy);
            if (zm->msleeping) {
                for (k = 0; k < 3; k++) {
                    double ph = fmod(now / 2200.0 + k / 3.0 + (zm->m_id % 7) * 0.13, 1.0);
                    int c = (int) (255 * sin(3.14159265358979 * ph));
                    char zs[2] = { ph > 0.55 ? 'Z' : 'z', 0 };

                    vulture_put_text_shadow(V_FONT_SMALL, zs, vulture_screen,
                                            sx + 10 + (int) (16 * ph)
                                                + (int) (3 * sin(ph * 9 + k)),
                                            sy - 66 - (int) (30 * ph),
                                            SDL_MapRGB(vulture_screen->format, c * 170 / 255,
                                                       c * 200 / 255, c),
                                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
                }
            } else { /* helpless: stars circling its head */
                for (k = 0; k < 3; k++) {
                    double a = now / 300.0 + k * 2.0944;

                    vfx_glowdot(sx + (int) (13 * cos(a)), sy - 62 + (int) (5 * sin(a)),
                                255, 230, 120, 220);
                }
            }
        }
    }

    for (i = 0; i < vfx_list.size(); i++) {
        vfx_ent &e = vfx_list[i];
        double age = (double) (now - e.t0) / e.dur, ts = (now - e.t0) / 1000.0;

        if (age < 1.0 && e.kind == 11) { /* s155: level up, around you */
            int k3;

            vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &sx, &sy);
            for (k3 = 0; k3 < 3; k3++) { /* rings rippling out from the feet */
                double a3 = (age - 0.12 * k3) / 0.55;

                if (a3 > 0.0 && a3 < 1.0)
                    vfx_ring(sx, sy, 10 + 46 * a3, e, (int) (230 * (1.0 - a3)));
            }
            { /* a soft beam of light rising from you, swelling then fading */
                double bl = age < 0.15 ? age / 0.15 : age > 0.6 ? (1.0 - age) / 0.4 : 1.0;
                int yy;

                for (yy = 0; yy < 130; yy += 3) {
                    double hf = 1.0 - yy / 130.0;
                    int al = (int) (150 * bl * hf * hf);

                    if (al <= 4)
                        continue;
                    vfx_glowdot(sx - 5, sy - 8 - yy, 255, 225, 140, al);
                    vfx_glowdot(sx + 5, sy - 8 - yy, 255, 225, 140, al);
                    vfx_glowdot(sx, sy - 10 - yy, 255, 240, 190, al);
                }
            }
            for (k3 = 0; k3 < 40; k3++) { /* a spiral of motes climbing */
                double ph = age * 1.6 - k3 * 0.025;
                double ang = ph * 9.0 + k3 * 0.9, rr = 26 - 12 * (ph > 1 ? 1 : ph);
                int px3, py3, al3;

                if (ph <= 0.0 || ph >= 1.0)
                    continue;
                px3 = sx + (int) (rr * cos(ang));
                py3 = sy - 6 + (int) (rr * 0.45 * sin(ang)) - (int) (110 * ph);
                al3 = (int) (255 * sin(3.14159265358979 * ph) * (1.0 - age * 0.4));
                vfx_glowdot(px3, py3, 255, (Uint8) (205 + 40 * (k3 & 1)), 120, al3);
                vfx_glowdot(px3 + 1, py3 - 1, 255, 250, 210, al3 / 2); /* a bright core */
            }
            continue;
        }
        if (age < 1.0 && e.kind == 10) { /* s151: a dust puff along the floor */
            int nd = 6 + 2 * e.sub, k2;
            double ease = 1.0 - (1.0 - age) * (1.0 - age), rr = (10 + 3.2 * e.sub) * ease;

            vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
            for (k2 = 0; k2 < nd; k2++) {
                double a = k2 * 6.2832 / nd + e.x * 0.7 + e.y * 1.3;
                int al = (int) ((1.0 - age) * (110 + 12 * e.sub));

                vfx_glowdot(sx + (int) (rr * cos(a)), sy + (int) (rr * 0.5 * sin(a)) - (int) (7 * age),
                            e.r, e.g, e.b, al > 230 ? 230 : al);
            }
            continue;
        }
        if (age < 1.0 && e.kind == 3) { /* s108 */
            vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
            vfx_draw_click(e, age, sx, sy);
            continue;
        }
        if (age < 1.0 && e.kind == 6) { /* s129: impact burst */
            int k2, a = (int) (255 * (1.0 - age));
            double rad = 6 + 30 * (1 - (1 - age) * (1 - age));

            vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
            for (k2 = 0; k2 < 24; k2++) {
                double an = 2 * 3.14159265358979 * k2 / 24;

                vfx_glowdot(sx + (int) (rad * cos(an)), sy - 26 + (int) (rad * 0.6 * sin(an)),
                            e.r, e.g, e.b, a);
            }
            if (age < 0.35) {
                int c = (int) (255 * (1.0 - age / 0.35));

                vfx_glowdot(sx, sy - 26, 255, 255, 255, c);
                vfx_glowdot(sx - 3, sy - 24, 255, 255, 255, c);
                vfx_glowdot(sx + 3, sy - 28, 255, 255, 255, c);
            }
            continue;
        }
        if (age < 1.0 && e.kind == 7) { /* s136: a glint on magic loot */
            int a = (int) (230 * sin(3.14159265358979 * age)), k2;
            int arm = 2 + (int) (7 * sin(3.14159265358979 * age));

            vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
            sx += e.sub, sy -= 10;
            for (k2 = -arm; k2 <= arm; k2++) {
                int fa = a * (arm + 1 - abs(k2)) / (arm + 1);

                vfx_glowdot(sx + k2, sy, e.r, e.g, e.b, fa);
                vfx_glowdot(sx, sy + k2, e.r, e.g, e.b, fa);
            }
            vfx_glowdot(sx, sy, 255, 255, 255, a);
            continue;
        }
        if (age < 1.0 && e.kind == 9) { /* s137: how far a noise carries */
            int ring;

            vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
            for (ring = 0; ring < 2; ring++) {
                double f = age - ring * 0.18, ea, rw, rh;
                int n2, k2, al;

                if (f <= 0)
                    continue;
                ea = 1.0 - (1.0 - f) * (1.0 - f);
                rw = e.sub * ea * 79.0 / 10.0, rh = e.sub * ea * 31.0 / 10.0;
                al = (int) ((ring ? 110 : 190) * (1.0 - f));
                n2 = 24 + (int) (rw / 4);
                for (k2 = 0; k2 < n2; k2++) {
                    double an = 2 * 3.14159265358979 * k2 / n2;

                    vfx_glowdot(sx + (int) (rw * cos(an)), sy + (int) (rh * sin(an)),
                                e.r, e.g, e.b, al);
                }
            }
            continue;
        }
        if (age < 1.0 && e.kind == 8) { /* s136: Brogue-style search pulse */
            vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
            vfx_ring(sx, sy, 10 + 95 * age, e, (int) (200 * (1.0 - age)));
            if (age > 0.25)
                vfx_ring(sx, sy, 10 + 95 * (age - 0.25), e, (int) (110 * (1.0 - age)));
            continue;
        }
        if (age < 1.0 && e.kind == 5) { /* s117: a speech bubble */
            vsay_draw(e, age, cx, cy, vx, vy);
            continue;
        }
        if (age < 1.0 && e.kind == 4) { /* s109: a bark over the hero's head */
            int tw = vulture_text_length(V_FONT_SMALL, e.txt);

            vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &sx, &sy);
            vulture_put_text_shadow(V_FONT_SMALL, e.txt, vulture_screen,
                                    sx - tw / 2, sy - 104 - (int) (8 * age),
                                    SDL_MapRGB(vulture_screen->format, e.r, e.g, e.b),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            continue;
        }
        if (age >= 1.0)
            continue;
        vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
        if (e.kind == 1) {
            int ty = sy - 62 - (int) (34 * (1.0 - (1.0 - age) * (1.0 - age)))
                     - e.sub * (vulture_get_lineheight(V_FONT_LARGE) + 2); /* s146 */
            int w = vulture_text_length(V_FONT_LARGE, e.txt);
            double dim = age < 0.6 ? 1.0 : 1.0 - (age - 0.6) / 0.4;

            vulture_put_text_shadow(V_FONT_LARGE, e.txt, vulture_screen,
                                    sx - w / 2, ty,
                                    SDL_MapRGB(vulture_screen->format,
                                               (Uint8) (e.r * dim), (Uint8) (e.g * dim),
                                               (Uint8) (e.b * dim)),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        } else if (e.kind == 2) {
            /* s98: soft round droplets (the square dots read as boxes) */
            for (k = 0; k < 10; k++) {
                int px = sx + (int) (e.vx[k] * ts);
                int py = sy - 30 + (int) (e.vy[k] * ts + 0.5 * 420.0 * ts * ts);

                vfx_droplet(px, py, k % 3, (int) (255 * (1.0 - age * 0.8)));
            }
        }
    }
}

/* idle breathing: ~3% vertical, out of phase per creature */
static void
vfx_breathe(int j, int i, double *xs, double *ys)
{
    struct monst *m;
    double ph, s, amp = 1.0, rate = 2.4;

    if (u_at(j, i)) {
        ph = 0.3;
        if (u.usleep) /* s119 */
            amp = 0.4, rate = 1.0;
    } else if ((m = m_at(j, i)) != 0 && canspotmon(m)) {
        ph = m->m_id * 0.7;
        if (m->msleeping || !m->mcanmove) /* s119: slow, shallow sleep breaths */
            amp = 0.4, rate = 1.0;
    } else {
        return;
    }
    s = sin(SDL_GetTicks() / 1000.0 * rate + ph);
    *ys *= 1.0 + 0.03 * amp * s;
    *xs *= 1.0 - 0.015 * amp * s;
}


/*
 * IMPLEMENTATION_LOG s86: atmosphere (Brogue/Stoneshard-inspired).
 *  - Light: every active light source the hero can see (lamps, candles,
 *    glowing monsters, the hero's own light) and every visible lava square
 *    adds a warm additive glow, sized by the source's range.
 *  - Night: during real-world night (5.0's night(): the clock NetHack
 *    already uses for werecreatures) the map is tinted a cool blue.
 *  - Dust: a few motes drift slowly across the view.
 *  - Monster panel: hostile monsters in view, nearest first, each with a
 *    rough condition word.
 * All drawn over the finished map, under the UI windows.
 */
static SDL_Surface *vw_glow = NULL, *vw_night = NULL, *vw_panel = NULL;

static void
vw_make_glow(void)
{
    int x, y, r = 64;

    vw_glow = SDL_CreateRGBSurfaceWithFormat(0, r * 2, r * 2, 32,
                                             SDL_PIXELFORMAT_ARGB8888);
    if (!vw_glow)
        return;
    SDL_LockSurface(vw_glow);
    for (y = 0; y < r * 2; y++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) vw_glow->pixels + y * vw_glow->pitch);

        for (x = 0; x < r * 2; x++) {
            double dx = (x - r) / (double) r, dy = (y - r) / (double) r;
            double d = sqrt(dx * dx + dy * dy), a = 1.0 - d;

            if (a < 0)
                a = 0;
            a = a * a;
            row[x] = SDL_MapRGBA(vw_glow->format, (Uint8) (90 * a),
                                 (Uint8) (55 * a), (Uint8) (18 * a), 255);
        }
    }
    SDL_UnlockSurface(vw_glow);
    SDL_SetSurfaceBlendMode(vw_glow, SDL_BLENDMODE_ADD);
}

static void
vw_glow_at(int sx, int sy, int radius, double strength)
{
    SDL_Rect d;

    if (!vw_glow)
        vw_make_glow();
    if (!vw_glow || radius <= 0)
        return;
    d.x = sx - radius, d.y = sy - radius * 6 / 10;
    d.w = radius * 2, d.h = radius * 12 / 10; /* flattened: iso floor */
    SDL_SetSurfaceColorMod(vw_glow, (Uint8) (255 * strength),
                           (Uint8) (255 * strength), (Uint8) (255 * strength));
    SDL_BlitScaled(vw_glow, NULL, vulture_screen, &d);
}

static void
vw_draw(int cx, int cy, int vx, int vy, int x0, int y0, int w, int h)
{
    light_source *ls;
    int j, i, sx, sy;
    Uint32 now = SDL_GetTicks();

    /* lava glow (flickers a little) */
    for (i = 0; i < ROWNO; i++)
        for (j = 1; j < COLNO; j++)
            if (!Blind && is_lava(j, i) && cansee(j, i)) {
                vfx_square_screen(j, i, cx, cy, vx, vy, &sx, &sy);
                if (sx < x0 - 80 || sy < y0 - 80 || sx > x0 + w + 80
                    || sy > y0 + h + 80)
                    continue;
                vw_glow_at(sx, sy, 70,
                           0.55 + 0.15 * sin(now / 300.0 + j * 1.3 + i * 0.7));
            }
    /* light sources - s99: a blind hero sees no light at all, and only
       sources actually in sight glow (the hero's own lamp included) */
    for (ls = gl.light_base; ls && !Blind; ls = ls->next) {
        if (!isok(ls->x, ls->y) || ls->range <= 0 || !cansee(ls->x, ls->y))
            continue;
        vfx_square_screen(ls->x, ls->y, cx, cy, vx, vy, &sx, &sy);
        vw_glow_at(sx, sy - 10, 40 + ls->range * 38,
                   0.75 + 0.08 * sin(now / 180.0 + ls->x));
    }

    /* s87: night tint, OUTDOORS only - squares open to the sky: forest
       levels (arboreal: several quest homes) outside any room/building,
       and the Plane of Air.  The rest of the dungeon is underground.  One
       translucent blue diamond per outdoor floor square. */
    if (night() && (svl.level.flags.arboreal || Is_airlevel(&u.uz))) {
        if (!vw_night) {
            int dw = V_MAP_XMOD * 2, dh = V_MAP_YMOD * 2, xx, yy;

            vw_night = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32,
                                                      SDL_PIXELFORMAT_ARGB8888);
            if (vw_night) {
                SDL_LockSurface(vw_night);
                for (yy = 0; yy < dh; yy++) {
                    Uint32 *row = (Uint32 *) ((Uint8 *) vw_night->pixels
                                              + yy * vw_night->pitch);

                    for (xx = 0; xx < dw; xx++) {
                        double fx = fabs((xx + 0.5) - dw / 2.0) / (dw / 2.0);
                        double fy = fabs((yy + 0.5) - dh / 2.0) / (dh / 2.0);

                        row[xx] = SDL_MapRGBA(vw_night->format, 10, 20, 60,
                                              fx + fy <= 1.0 ? 255 : 0);
                    }
                }
                SDL_UnlockSurface(vw_night);
                SDL_SetSurfaceBlendMode(vw_night, SDL_BLENDMODE_BLEND);
                SDL_SetSurfaceAlphaMod(vw_night, 70);
            }
        }
        if (vw_night)
            for (i = 0; i < ROWNO; i++)
                for (j = 1; j < COLNO; j++) {
                    SDL_Rect d;

                    if (!Is_airlevel(&u.uz)
                        && (levl[j][i].roomno != NO_ROOM
                            || !ACCESSIBLE(levl[j][i].typ)))
                        continue; /* inside a building, or not floor */
                    if (!levl[j][i].seenv)
                        continue;
                    vfx_square_screen(j, i, cx, cy, vx, vy, &sx, &sy);
                    if (sx < x0 - V_MAP_XMOD || sy < y0 - V_MAP_YMOD
                        || sx > x0 + w + V_MAP_XMOD || sy > y0 + h + V_MAP_YMOD)
                        continue;
                    d.x = sx - V_MAP_XMOD, d.y = sy - V_MAP_YMOD;
                    d.w = V_MAP_XMOD * 2, d.h = V_MAP_YMOD * 2;
                    SDL_BlitSurface(vw_night, NULL, vulture_screen, &d);
                }
    }

    /* dust motes: deterministic drift from the clock, no state to keep.
       s90: only visible where there's light - a mote is drawn only if the
       map square under it is in view and lit (a lit room, or within reach
       of a light source such as the hero's lamp) */
    for (i = 0; i < 90; i++) {
        double t = now / 1000.0, seedx = (i * 7919) % 1000 / 1000.0,
               seedy = (i * 104729) % 1000 / 1000.0;
        int px = x0 + (int) (fmod(seedx * w + t * (6 + i % 5) + 30 * sin(t * 0.3 + i), w));
        int py = y0 + (int) (fmod(seedy * h + t * (3 + i % 3) + 20 * cos(t * 0.25 + i), h));
        double fa = (double) (px - cx) / V_MAP_XMOD, fb = (double) (py - cy) / V_MAP_YMOD;
        int va = (int) floor((fa + fb) / 2.0 + 0.5), vb = (int) floor((fb - fa) / 2.0 + 0.5);
        int mdx, mdy, mx, my;
        bool lit = false;

        v_rot(va, vb, vulture_view_rot, &mdx, &mdy);
        mx = vx + mdx, my = vy + mdy;
        if (!isok(mx, my) || !cansee(mx, my))
            continue;
        if (levl[mx][my].lit) {
            lit = true;
        } else {
            for (ls = gl.light_base; ls && !lit; ls = ls->next)
                if (ls->range > 0
                    && dist2(mx, my, ls->x, ls->y) <= ls->range * ls->range)
                    lit = true;
        }
        if (!lit)
            continue;
        vfx_dot(px, py, 1 + (i % 3 == 0),
                SDL_MapRGB(vulture_screen->format, 135, 128, 110));
    }

    /* monster panel - s130: replaced by the "In sight" panel */
    if (0) {
        struct monst *m, *list[6];
        int n = 0, k, row = 18, pw = 230, ph;
        char line[BUFSZ];

        for (m = fmon; m; m = m->nmon) {
            if (DEADMONSTER(m) || m->mpeaceful || m->mtame || !canspotmon(m)
                || !couldsee(m->mx, m->my))
                continue;
            if (n < 6) {
                list[n++] = m;
            } else {
                int far = 0;

                for (k = 1; k < 6; k++)
                    if (distmin(u.ux, u.uy, list[k]->mx, list[k]->my)
                        > distmin(u.ux, u.uy, list[far]->mx, list[far]->my))
                        far = k;
                if (distmin(u.ux, u.uy, m->mx, m->my)
                    < distmin(u.ux, u.uy, list[far]->mx, list[far]->my))
                    list[far] = m;
            }
        }
        if (!n)
            return;
        ph = 26 + n * row;
        if (!vw_panel || vw_panel->h != ph) {
            if (vw_panel)
                SDL_FreeSurface(vw_panel);
            vw_panel = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32,
                                                      SDL_PIXELFORMAT_ARGB8888);
            if (vw_panel) {
                SDL_FillRect(vw_panel, NULL,
                             SDL_MapRGBA(vw_panel->format, 0, 0, 0, 255));
                SDL_SetSurfaceBlendMode(vw_panel, SDL_BLENDMODE_BLEND);
                SDL_SetSurfaceAlphaMod(vw_panel, 130);
            }
        }
        if (vw_panel) {
            SDL_Rect d;

            d.x = x0 + 12, d.y = y0 + h / 3, d.w = pw, d.h = ph;
            SDL_BlitSurface(vw_panel, NULL, vulture_screen, &d);
            vulture_put_text_shadow(V_FONT_SMALL, "Hostiles in view",
                                    vulture_screen, d.x + 8, d.y + 5,
                                    SDL_MapRGB(vulture_screen->format, 230, 200, 120),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            for (k = 0; k < n; k++) {
                int mx = list[k]->mhpmax > 0 ? list[k]->mhpmax : 1;
                int pct = list[k]->mhp * 100 / mx;
                const char *cond = pct >= 90 ? "unhurt" : pct >= 60 ? "scratched"
                                   : pct >= 35 ? "wounded"
                                   : pct >= 15 ? "badly hurt" : "near death";
                Uint8 r = pct >= 60 ? 200 : pct >= 35 ? 230 : 255;
                Uint8 g = pct >= 60 ? 220 : pct >= 35 ? 190 : 90;

                snprintf(line, sizeof line, "%s - %s",
                         pmname(list[k]->data, Mgender(list[k])), cond);
                vulture_put_text_shadow(V_FONT_SMALL, line, vulture_screen,
                                        d.x + 10, d.y + 24 + k * row,
                                        SDL_MapRGB(vulture_screen->format, r, g, 120),
                                        SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            }
        }
    }
}

/* s86: F11 photo mode - the map without the interface, saved as a BMP in
   %USERPROFILE%\Pictures\Revulting */
#include <direct.h>
#include <time.h>

static void
vw_photo(levelwin *lw)
{
    char dir[512], path[600];
    const char *home = getenv("USERPROFILE");
    SDL_Surface *shot;
    SDL_Rect r;
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);

    if (!home)
        return;
    snprintf(dir, sizeof dir, "%s\\Pictures", home);
    (void) _mkdir(dir);
    snprintf(dir, sizeof dir, "%s\\Pictures\\Revulting", home);
    (void) _mkdir(dir);
    snprintf(path, sizeof path, "%s\\shot-%04d%02d%02d-%02d%02d%02d.bmp", dir,
             tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday, tmv->tm_hour,
             tmv->tm_min, tmv->tm_sec);
    lw->force_redraw();
    lw->draw(); /* the map alone - the UI windows are drawn afterwards */
    r.x = lw->abs_x, r.y = lw->abs_y, r.w = lw->w, r.h = lw->h;
    shot = SDL_CreateRGBSurfaceWithFormat(0, r.w, r.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (shot) {
        SDL_BlitSurface(vulture_screen, &r, shot, NULL);
        if (SDL_SaveBMP(shot, path) == 0)
            pline("Photo saved: %s", path);
        else
            pline("Couldn't save the photo.");
        SDL_FreeSurface(shot);
    }
    lw->force_redraw();
}

/*
 * IMPLEMENTATION_LOG s92: death-cam.  Once per turn the screen as it stands
 * is copied, at half its size (s107: smoothly), into a ring of the last 40 turns.
 * When the hero dies for good, really_done() plays the ring back, in colour,
 * oldest to newest, "Your last moments..." - before the end-of-game
 * questions.  Any key or click skips it.
 */
#define VRP_N 40
static SDL_Surface *vrp_frame[VRP_N];
static int vrp_head = 0, vrp_count = 0;
static d_level vrp_lev = { -1, -1 };

/* s107: smooth death-cam - frames are kept at half size, box-filtered
   down (not nearest-pixel), and bilinear-filtered back up on playback, so
   the replay no longer looks pixelated */
static SDL_Surface *vrp_scratch = NULL; /* full-size ARGB work surface */

static SDL_Surface *
vrp_scratch_for(int w, int h)
{
    if (!vrp_scratch || vrp_scratch->w != w || vrp_scratch->h != h) {
        if (vrp_scratch)
            SDL_FreeSurface(vrp_scratch);
        vrp_scratch = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                     SDL_PIXELFORMAT_ARGB8888);
    }
    return vrp_scratch;
}

/* full-size ARGB `s` -> half-size ARGB `f`, averaging each 2x2 block */
static void
vrp_down2(SDL_Surface *s, SDL_Surface *f)
{
    int x, y;

    SDL_LockSurface(s);
    SDL_LockSurface(f);
    for (y = 0; y < f->h; y++) {
        Uint32 *r0 = (Uint32 *) ((Uint8 *) s->pixels + (2 * y) * s->pitch);
        Uint32 *r1 = (Uint32 *) ((Uint8 *) s->pixels + (2 * y + 1 < s->h ? 2 * y + 1 : 2 * y) * s->pitch);
        Uint32 *o = (Uint32 *) ((Uint8 *) f->pixels + y * f->pitch);

        for (x = 0; x < f->w; x++) {
            Uint32 a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], d = r1[2 * x + 1];
            Uint32 rb = ((a & 0xFF00FF) + (b & 0xFF00FF) + (c & 0xFF00FF) + (d & 0xFF00FF)) >> 2;
            Uint32 g = ((a & 0xFF00) + (b & 0xFF00) + (c & 0xFF00) + (d & 0xFF00)) >> 2;

            o[x] = 0xFF000000 | (rb & 0xFF00FF) | (g & 0xFF00);
        }
    }
    SDL_UnlockSurface(f);
    SDL_UnlockSurface(s);
}

/* half-size ARGB `f` -> `o` (any size, ARGB), bilinear */
static void
vrp_up(SDL_Surface *f, SDL_Surface *o)
{
    int x, y;
    double sx = (double) f->w / o->w, sy = (double) f->h / o->h;

    SDL_LockSurface(f);
    SDL_LockSurface(o);
    for (y = 0; y < o->h; y++) {
        double fy = (y + 0.5) * sy - 0.5;
        int y0 = fy < 0 ? 0 : (int) fy, y1 = y0 + 1 < f->h ? y0 + 1 : y0;
        int wy = (int) ((fy - y0) * 256);
        Uint32 *r0 = (Uint32 *) ((Uint8 *) f->pixels + y0 * f->pitch);
        Uint32 *r1 = (Uint32 *) ((Uint8 *) f->pixels + y1 * f->pitch);
        Uint32 *out = (Uint32 *) ((Uint8 *) o->pixels + y * o->pitch);

        if (wy < 0)
            wy = 0;
        for (x = 0; x < o->w; x++) {
            double fx = (x + 0.5) * sx - 0.5;
            int x0 = fx < 0 ? 0 : (int) fx, x1 = x0 + 1 < f->w ? x0 + 1 : x0;
            int wx = (int) ((fx - x0) * 256);
            Uint32 p00 = r0[x0], p01 = r0[x1], p10 = r1[x0], p11 = r1[x1];
            Uint32 rb0, g0, rb1, g1, rb, g;

            if (wx < 0)
                wx = 0;
            rb0 = (((p00 & 0xFF00FF) * (256 - wx) + (p01 & 0xFF00FF) * wx) >> 8) & 0xFF00FF;
            g0 = (((p00 & 0xFF00) * (256 - wx) + (p01 & 0xFF00) * wx) >> 8) & 0xFF00;
            rb1 = (((p10 & 0xFF00FF) * (256 - wx) + (p11 & 0xFF00FF) * wx) >> 8) & 0xFF00FF;
            g1 = (((p10 & 0xFF00) * (256 - wx) + (p11 & 0xFF00) * wx) >> 8) & 0xFF00;
            rb = ((rb0 * (256 - wy) + rb1 * wy) >> 8) & 0xFF00FF;
            g = ((g0 * (256 - wy) + g1 * wy) >> 8) & 0xFF00;
            out[x] = 0xFF000000 | rb | g;
        }
    }
    SDL_UnlockSurface(o);
    SDL_UnlockSurface(f);
}

extern "C" void
vulture_replay_capture(void)
{
    static long last = -1L;
    SDL_Surface *f;
    SDL_Rect src, dst;

    if (!levwin || vulture_windows_inited != TRUE || svm.moves == last)
        return;
    last = svm.moves;
    if (!on_level(&vrp_lev, &u.uz)) { /* new level: keep going, it's a story */
        vrp_lev = u.uz;
    }
    src.x = levwin->abs_x, src.y = levwin->abs_y;
    src.w = levwin->w, src.h = levwin->h;
    if (src.w < 3 || src.h < 3)
        return;
    f = vrp_frame[vrp_head];
    if (!f || f->w != src.w / 2 || f->h != src.h / 2) {
        if (f)
            SDL_FreeSurface(f);
        f = SDL_CreateRGBSurfaceWithFormat(0, src.w / 2, src.h / 2, 32,
                                           SDL_PIXELFORMAT_ARGB8888);
        vrp_frame[vrp_head] = f;
        if (!f)
            return;
    }
    {
        SDL_Surface *sc = vrp_scratch_for(src.w, src.h);

        if (!sc)
            return;
        dst.x = dst.y = 0, dst.w = src.w, dst.h = src.h;
        {
            SDL_BlendMode bm;

            SDL_GetSurfaceBlendMode(vulture_screen, &bm);
            SDL_SetSurfaceBlendMode(vulture_screen, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(vulture_screen, &src, sc, &dst); /* s107 */
            SDL_SetSurfaceBlendMode(vulture_screen, bm);
        }
        vrp_down2(sc, f);
    }
    vrp_head = (vrp_head + 1) % VRP_N;
    if (vrp_count < VRP_N)
        vrp_count++;
}

extern "C" void
vulture_replay_play(void)
{
    int k, start;
    SDL_Rect d;
    SDL_Surface *win;

    if (!levwin || vulture_windows_inited != TRUE || vrp_count < 3)
        return;
    vulture_replay_capture(); /* the final moment too */
    start = (vrp_head - vrp_count + VRP_N) % VRP_N;
    d.x = levwin->abs_x, d.y = levwin->abs_y, d.w = levwin->w, d.h = levwin->h;
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
    for (k = 0; k < vrp_count; k++) {
        SDL_Surface *f = vrp_frame[(start + k) % VRP_N];
        const char *cap = "Your last moments...";

        if (!f)
            continue;
        SDL_FillRect(vulture_screen, &d, SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        { /* s105: no sepia; s107: smooth upscale, not blocky */
            SDL_Surface *sc = vrp_scratch_for(d.w, d.h);
            SDL_Rect dd = d;

            if (sc) {
                vrp_up(f, sc);
                SDL_BlitSurface(sc, NULL, vulture_screen, &dd);
            } else {
                SDL_BlitScaled(f, NULL, vulture_screen, &dd);
            }
        }
        vulture_put_text_shadow(V_FONT_LARGE, cap, vulture_screen,
                                d.x + d.w / 2 - vulture_text_length(V_FONT_LARGE, cap) / 2,
                                d.y + d.h - 60,
                                SDL_MapRGB(vulture_screen->format, 235, 215, 170),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        nhUse(win); /* s168: no window-surface grab (a renderer may own the window) */
        vulture_refresh();
        /* slow toward the end */
        SDL_Delay(k > vrp_count - 6 ? 260 : 120);
        if (vanim_input_waiting())
            break;
    }
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
    vrp_count = 0;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vulture_refresh_window_region();
}

/*
 * IMPLEMENTATION_LOG s82: floating and flying monsters bob.  Floaters
 * (eyes, lights) drift slowly and hover a little above the floor; flyers
 * flutter faster.  Each has its own phase (from m_id) so a flock doesn't
 * move in step.  The offset comes from the clock, so it stays smooth
 * through tweens; vulture_idle_bob() keeps it moving while the game waits
 * for input - only when such a monster is in view, ~15 fps, and never
 * when a key or click is already queued.
 */
static int
vbob_offset(int j, int i)
{
    struct monst *m = m_at(j, i);
    double t, ph;

    if (!m || DEADMONSTER(m) || !canspotmon(m))
        return 0;
    t = SDL_GetTicks() / 1000.0;
    ph = m->m_id * 1.618;
    if (m->msleeping || !m->mcanmove) { /* s119: asleep - settle, barely drift */
        if (is_floater(m->data))
            return (int) (-3.0 - 1.5 * sin(t * 0.8 + ph));
        if (is_flyer(m->data))
            return (int) (-1.0 - 1.0 * sin(t * 1.0 + ph));
        return 0;
    }
    if (is_floater(m->data))
        return (int) (-6.0 - 5.0 * sin(t * 2.6 + ph));
    if (is_flyer(m->data))
        return (int) (-5.0 - 4.0 * sin(t * 7.0 + ph));
    return 0;
}

static bool
vbob_wanted(void)
{
    struct monst *m;

    if (!levwin || vulture_windows_inited != TRUE)
        return false;
    for (m = fmon; m; m = m->nmon)
        if (!DEADMONSTER(m) && canspotmon(m)
            && (is_floater(m->data) || is_flyer(m->data))
            && distmin(m->mx, m->my, vtw_last_vx, vtw_last_vy) <= 18)
            return true;
    return false;
}

/* wait timeout for the input loop: shorter while something is bobbing */
static Uint32 vidle_interval = 33; /* s85: grows if frames are slow; s147: 30 fps */

int
vulture_idle_wait_ms(bool at_map)
{
    nhUse(vbob_wanted);
    return at_map ? (vfx_list.empty() ? 30 : 15) : 100; /* s108; s147 */
}

/* one idle frame for bobbing, breathing and combat effects (called from
   the input loop while the map waits for a command) */
static void vselftest(void); /* s126 probe */
static point vck_from = { -1, -1 }, vck_to = { -1, -1 }; /* s145 probe */

static void
vprobe_push_click(point sq) /* probe: s145 a real left click on a square */
{
    point mp = levwin->map_to_mouse(sq);
    SDL_Event e;

    vulture_set_mouse_pos(mp.x, mp.y);
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT, e.button.state = SDL_PRESSED;
    e.button.x = mp.x, e.button.y = mp.y, e.button.clicks = 1;
    SDL_PushEvent(&e);
    e.type = SDL_MOUSEBUTTONUP, e.button.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

static void
vprobe_push_key(SDL_Keycode k) /* probe: s145 */
{
    SDL_Event e;

    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.state = SDL_PRESSED;
    e.key.keysym.sym = k;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    SDL_PushEvent(&e);
    e.type = SDL_KEYUP, e.key.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}
static void vgs_probe(int stage, const char *dir); /* s145 probe */
static SDL_Rect vbtn_rect[4];

/* probe: s166 - VULTURE_SELFTEST_SOAK: play by itself, through real input.
   Tops up HP and food each tick; Tab when an enemy is close, otherwise the
   EXPLORE button; when exploring stops moving you, '>' twice (walk to the
   stairs, go down); a menu or prompt that stays up gets Esc.  Logs a status
   line every 30 s. */
static void
vsoak_tick(bool at_map)
{
    static Uint32 last = 0, off_map_since = 0, last_log = 0;
    static int still = 0, lastx = -1, lasty = -1, cycle = 0;
    Uint32 now = SDL_GetTicks();
    struct monst *m;
    bool foe = false;

    if (!getenv("VULTURE_SELFTEST_SOAK") || !u.ux)
        return;
    if (!at_map) { /* a menu or prompt: give it 2.5 s, then Esc */
        if (!off_map_since)
            off_map_since = now;
        else if (now - off_map_since > 2500) {
            vprobe_push_key(SDLK_ESCAPE);
            off_map_since = now;
        }
        return;
    }
    off_map_since = 0;
    if (now - last < 220 || vanim_input_waiting())
        return;
    last = now;
    u.uhp = u.uhpmax;
    if (Upolyd)
        u.mh = u.mhmax;
    u.uhunger = 900;
    if (now - last_log > 30000) {
        last_log = now;
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "soak: turn %ld dlvl %d at %d,%d\n",
                          svm.moves, (int) depth(&u.uz), u.ux, u.uy);
    }
    for (m = fmon; m; m = m->nmon)
        if (!DEADMONSTER(m) && canspotmon(m) && !m->mtame && !m->mpeaceful
            && distmin(u.ux, u.uy, m->mx, m->my) <= 6)
            foe = true;
    if (u.ux == lastx && u.uy == lasty)
        still++;
    else
        still = 0;
    lastx = u.ux, lasty = u.uy;
    cycle++;
    if (foe && still < 12) {
        vprobe_push_key(SDLK_TAB);
    } else if (still >= 8) { /* exploring has stopped: head down */
        vprobe_push_key(SDLK_GREATER);
        vprobe_push_key(SDLK_GREATER);
        if (still > 30) { /* stuck somewhere: a random step */
            static const SDL_Keycode ks[4] = { SDLK_h, SDLK_j, SDLK_k, SDLK_l };

            vprobe_push_key(ks[cycle & 3]);
        }
    } else if (vbtn_rect[3].w) { /* the EXPLORE button, clicked for real */
        SDL_Event e;

        memset(&e, 0, sizeof e);
        vulture_set_mouse_pos(vbtn_rect[3].x + 4, vbtn_rect[3].y + 4);
        e.type = SDL_MOUSEBUTTONDOWN, e.button.button = SDL_BUTTON_LEFT;
        e.button.state = SDL_PRESSED, e.button.x = vbtn_rect[3].x + 4, e.button.y = vbtn_rect[3].y + 4;
        e.button.clicks = 1;
        SDL_PushEvent(&e);
        e.type = SDL_MOUSEBUTTONUP, e.button.state = SDL_RELEASED;
        SDL_PushEvent(&e);
    }
}
static void vaim_probe(int stage, const char *dir); /* s156 probe */
static Uint32 vpause_probe_t = 0; /* s145 probe: shoot the pause menu */
static void vl_relief_set(bool on); /* s145 */
static void vl_relief_report(FILE *f);

static long vrt_probe_moves0 = 0; /* s140 probe */
static int vrt_get(void);
static void vrt_probe_dump(FILE *f);
extern "C" void vulture_search_fx(void); /* s136 */

void
vulture_idle_bob(bool at_map)
{
    static Uint32 last = 0;
    Uint32 now = SDL_GetTicks(), took;

    if (vpause_probe_t && now - vpause_probe_t > 700 && getenv("VULTURE_SELFTEST")) {
        char pp[700];

        vpause_probe_t = 0;
        ROOTWIN->draw_windows();
        snprintf(pp, sizeof pp, "%s\\selftest_pause.bmp", getenv("VULTURE_SELFTEST"));
        SDL_SaveBMP(vulture_screen, pp);
    }
    vsoak_tick(at_map); /* probe: s166 automated play */
    if (!at_map || !levwin || vulture_windows_inited != TRUE || vtw_animating
        || vanim_kind || vanim_input_waiting())
        return;
    {   /* s108: a click marker animates at ~40 fps, everything else idles */
        bool marker = false;
        size_t q;

        for (q = 0; q < vfx_list.size(); q++)
            if (vfx_list[q].kind == 3 && now - vfx_list[q].t0 < vfx_list[q].dur)
                marker = true;
        if (now - last < (marker ? 25u : vidle_interval))
            return;
    }
    last = now;
    vselftest(); /* s126 probe (VULTURE_SELFTEST only) */
    (void) vfx_alive();
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vulture_refresh_window_region();
    /* s117: the redraw painted over the hover tooltip - put it back (it
       used to flash up and vanish within ~80 ms) */
    vulture_mouse_draw();
    vulture_refresh_window_region();
    vulture_mouse_refresh();
    vulture_mouse_restore_bg();
    /* keep idle drawing to about a third of the time at most */
    took = SDL_GetTicks() - now;
    vidle_interval = took * 3 > 33 ? (took * 3 > 400 ? 400 : took * 3) : 33; /* s147 */
    if (getenv("VULTURE_SELFTEST")) /* probe: s152 whole idle frame */
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "idle frame %u ms\n", (unsigned) took);
}

/* play the in-between frames of a pending tween, then any queued death
   animations (vulture_win.cpp calls this after each refresh; re-entry is
   blocked while animating) */
/* s124: while travelling the game takes no input, but a middle-button
   drag still pans the camera: only MOUSEMOTION events are taken off the
   queue (keys and clicks stay for the game) and fed to the drag code */
extern int vulture_handle_global_event(SDL_Event *event);

void
vulture_travel_pan(void)
{
    SDL_Event ev;
    bool moved = false;

    if (!levwin || vulture_windows_inited != TRUE)
        return;
    SDL_PumpEvents();
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) > 0) {
        vulture_handle_global_event(&ev); /* render-space coords, pointer pos */
        if (ev.motion.state & SDL_BUTTON_MMASK) {
            levwin->handle_mousemotion_event(levwin, NULL, ev.motion.xrel,
                                             ev.motion.yrel, ev.motion.state);
            moved = true;
        }
    }
    if (moved && !vtw_animating) {
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
    }
}

static bool vtw_fast = false; /* s112: this step is part of a run/travel */

void
vulture_tween_run(void)
{
    int frames;

    if (vtw_animating || vanim_kind || !levwin)
        return;
    if (vhitstop_ms > 0) { /* s129: hold the frame that shows a big hit */
        SDL_Delay(vhitstop_ms);
        vhitstop_ms = 0;
    }
    /* s81 snapped every step of running / travel / auto-explore (long walks
       crawled).  s112: they slide again, just quickly - 4 short frames a
       step - so the hero visibly walks instead of teleporting.  A key or
       click still cuts any step short. */
    vtw_fast = svc.context.run || svc.context.travel || gm.multi || autoexplore_active;
    /* s147: one time-based tween for every step.  It renders as many frames
       as fit (about 7 ms each) instead of a fixed count with sleeps between,
       and a key or click that is already waiting HURRIES the rest (~35 ms)
       instead of snapping to the end - so a held direction key walks in one
       continuous glide.  Runs / travel keep their 90 ms linear glide, which
       vulture_delay_output then subtracts from the step's pause. */
    if (vtw_pending && !(vtw_fast && flags.runmode == RUN_TPORT)) {
        Uint32 t0 = SDL_GetTicks(), last = t0, now = t0;
        double dur = vtw_fast ? (flags.runmode == RUN_CRAWL ? 240.0 : 90.0) : 120.0;
        double t = 0.0, rate = 1.0 / dur;

        vtw_animating = true;
        vtw_linear = vtw_fast || vanim_input_waiting();
        frames = 0;
        for (;;) {
            now = SDL_GetTicks();
            t += (now - last) * rate;
            last = now;
            if (t > 1.0)
                t = 1.0;
            vtw_t = t;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            vulture_refresh_window_region();
            frames++;
            if (t >= 1.0)
                break;
            if (vtw_fast)
                vulture_travel_pan(); /* s124 */
            if (vanim_input_waiting()) {
                double hurry = (1.0 - t) / 35.0;

                if (hurry > rate)
                    rate = hurry;
                vtw_linear = true; /* no settle mid-chain */
            }
            SDL_Delay(1);
        }
        if (vtw_fast)
            vulture_tween_ate_ms = (int) (now - t0);
        vtw_linear = false;
        vtw_t = 1.0;
        vtw_pending = false;
        vtw_animating = false;
        vtw_commit(vtw_last_vx, vtw_last_vy);
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "tween: %d frames in %u ms\n",
                          frames, (unsigned) (now - t0));
    }
    if (vanim_ndeaths)
        vanim_play_deaths();
    if (vanim_ndrops)
        vanim_play_drops(); /* s83: after deaths, so corpses land last */
}

#include <math.h>

/*
 * IMPLEMENTATION_LOG s100: the 2027 HUD.
 *  - Toasts: achievement / level-up cards slide in on the right, hold,
 *    and slide back out (vulture_toast, from insight.c and exper.c).
 *  - Location card: a big title fades in mid-screen on arriving somewhere
 *    new - "THE GNOMISH MINES / Minetown" (vulture_location_card, do.c).
 *  - Waypoint: a bobbing marker over the remembered way onward (down
 *    stairs; up stairs while carrying the Amulet), or an arrow at the edge
 *    of the view pointing toward it when it is off-screen.
 */
struct vhud_toast {
    std::string title, body;
    Uint32 born;
};
static std::vector<vhud_toast> vhud_toasts;
static std::string vhud_card_title, vhud_card_sub;
static Uint32 vhud_card_born = 0;
static bool vhud_card_pending = false;
static SDL_Surface *vhud_dark = NULL;

extern "C" void
vulture_toast(const char *title, const char *body)
{
    vhud_toast t;

    t.title = title ? title : "";
    t.body = body ? body : "";
    t.born = 0; /* the clock starts when it first appears */
    if (vhud_toasts.size() >= 8)
        vhud_toasts.erase(vhud_toasts.begin());
    vhud_toasts.push_back(t);
}

extern "C" void
vulture_location_card(const char *title, const char *sub)
{
    vhud_card_title = title ? title : "";
    vhud_card_sub = sub ? sub : "";
    vhud_card_pending = true;
}

/* a translucent dark box (one cached surface, blitted clipped) */
static void
vhud_box(int x, int y, int w, int h, int alpha)
{
    SDL_Rect s, d;

    if (!vhud_dark) {
        vhud_dark = SDL_CreateRGBSurfaceWithFormat(0, 640, 160, 32,
                                                   SDL_PIXELFORMAT_ARGB8888);
        if (!vhud_dark)
            return;
        SDL_FillRect(vhud_dark, NULL, SDL_MapRGBA(vhud_dark->format, 8, 6, 10, 255));
        SDL_SetSurfaceBlendMode(vhud_dark, SDL_BLENDMODE_BLEND);
    }
    SDL_SetSurfaceAlphaMod(vhud_dark, (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
    s.x = 0, s.y = 0, s.w = w > 640 ? 640 : w, s.h = h > 160 ? 160 : h;
    d.x = x, d.y = y, d.w = s.w, d.h = s.h;
    SDL_BlitSurface(vhud_dark, &s, vulture_screen, &d);
}

/* the remembered way onward on this level, or false */
static bool
vhud_waypoint(int *wx, int *wy)
{
    stairway *st, *best = NULL;
    bool up = u.uhave.amulet ? true : false;

    for (st = gs.stairs; st; st = st->next) {
        if ((st->up ? true : false) != up || !isok(st->sx, st->sy)
            || !levl[st->sx][st->sy].seenv)
            continue;
        /* prefer the main way on in this dungeon over a branch */
        if (!best || (st->tolev.dnum == u.uz.dnum && best->tolev.dnum != u.uz.dnum))
            best = st;
    }
    if (!best || u_at(best->sx, best->sy))
        return false;
    *wx = best->sx, *wy = best->sy;
    return true;
}

static void
vhud_draw(int cx, int cy, int vx, int vy, int x0, int y0, int w, int h)
{
    Uint32 now = SDL_GetTicks();
    Uint32 black = SDL_MapRGB(vulture_screen->format, 0, 0, 0);
    int rightx = x0 + w - 16, k;

    /* ---- waypoint ---- */
    {
        int wx, wy, sx, sy;
        bool hostiles = false;
        struct monst *m;

        for (m = fmon; m; m = m->nmon)
            if (!DEADMONSTER(m) && !m->mpeaceful && !m->mtame && canspotmon(m)
                && couldsee(m->mx, m->my)) {
                hostiles = true;
                break;
            }
        if (!hostiles && !Blind && !Hallucination && !u.uswallow
            && vhud_waypoint(&wx, &wy)) {
            Uint32 cyan = SDL_MapRGB(vulture_screen->format, 120, 230, 255);
            const char *lbl = u.uhave.amulet ? "Up" : "Down";
            int margin = 48, ex, ey;

            vfx_square_screen(wx, wy, cx, cy, vx, vy, &sx, &sy);
            sy -= 46 + (int) (5 * sin(now / 260.0)); /* float above the tile */
            if (sx > x0 + margin && sx < x0 + w - margin && sy > y0 + margin
                && sy < y0 + h - margin) {
                /* s109: on screen Vulture's own stair label already says
                   "Down" - draw nothing (it showed the word twice) */
            } else {
                /* off screen: clamp to the view edge, arrow pointing at it */
                int mx = x0 + w / 2, my = y0 + h / 2;
                double dx = sx - mx, dy = sy - my, len = sqrt(dx * dx + dy * dy);
                double sc, ux, uy;
                int dist = distmin(u.ux, u.uy, wx, wy);
                char buf[40];

                if (len < 1)
                    len = 1;
                ux = dx / len, uy = dy / len;
                sc = fabs(ux) * (h / 2 - margin) > fabs(uy) * (w / 2 - margin)
                         ? (w / 2 - margin) / fabs(ux)
                         : (h / 2 - margin) / fabs(uy);
                ex = mx + (int) (ux * sc), ey = my + (int) (uy * sc);
                for (k = 0; k < 14; k++) {
                    /* a solid triangle: tip at +14 along the direction */
                    double t = k / 13.0, half = 8 * (1 - t);
                    int bx = ex + (int) (ux * k), by = ey + (int) (uy * k), s2;

                    for (s2 = -(int) half; s2 <= (int) half; s2++)
                        vfx_dot(bx + (int) (-uy * s2), by + (int) (ux * s2), 2, cyan);
                }
                snprintf(buf, sizeof buf, "%s %d", lbl, dist);
                vulture_put_text_shadow(V_FONT_SMALL, buf, vulture_screen,
                                        ex - (int) (ux * 26) - vulture_text_length(V_FONT_SMALL, buf) / 2,
                                        ey - (int) (uy * 26) - 7, cyan, black);
            }
        }
    }

    /* ---- toasts ---- */
    {
        const Uint32 life = 4800, slide = 260;
        int ty = y0 + h * 27 / 100, tw = 300, th = 50, shown = 0;
        Uint32 gold = SDL_MapRGB(vulture_screen->format, 255, 200, 70);
        Uint32 body = SDL_MapRGB(vulture_screen->format, 235, 232, 225);

        for (k = 0; k < (int) vhud_toasts.size() && shown < 3;) {
            vhud_toast &t = vhud_toasts[k];
            Uint32 age;
            double in;
            int x;

            if (!t.born) {
                t.born = now ? now : 1;
                vulture_sfx("chime");
            }
            age = now - t.born;
            if (age >= life) {
                vhud_toasts.erase(vhud_toasts.begin() + k);
                continue;
            }
            /* ease in from the right, ease back out at the end */
            in = age < slide ? (double) age / slide
                 : age > life - slide ? (double) (life - age) / slide : 1.0;
            in = 1 - (1 - in) * (1 - in) * (1 - in);
            x = rightx - (int) (tw * in);
            vhud_box(x, ty, tw + 16, th, 200);
            SDL_Rect bar;

            bar.x = x, bar.y = ty, bar.w = 4, bar.h = th;
            SDL_FillRect(vulture_screen, &bar, gold);
            vulture_put_text_shadow(V_FONT_SMALL, t.title, vulture_screen,
                                    x + 14, ty + 7, gold, black);
            vulture_put_text_shadow(V_FONT_LARGE, t.body, vulture_screen,
                                    x + 14, ty + 23, body, black);
            ty += th + 8;
            shown++;
            k++;
        }
    }

    /* ---- location card ---- */
    if (vhud_card_pending) {
        vhud_card_pending = false;
        vhud_card_born = now ? now : 1;
        vulture_sfx("card_whoosh"); /* s131 */
    }
    if (vhud_card_born) {
        const Uint32 life = 3600;
        Uint32 age = now - vhud_card_born;

        if (age >= life) {
            vhud_card_born = 0;
        } else {
            /* letter-spaced title, sliding apart a touch as it plays */
            double in = age < 400 ? age / 400.0 : age > life - 500 ? (life - age) / 500.0 : 1.0;
            int spacing = 4 + (int) (age / 900);
            int tl = 0, x, cy0 = y0 + h * 22 / 100;
            size_t c;
            Uint32 tcol = SDL_MapRGB(vulture_screen->format, 255, 236, 200);
            Uint32 scol = SDL_MapRGB(vulture_screen->format, 220, 180, 110);

            for (c = 0; c < vhud_card_title.size(); c++)
                tl += vulture_text_length(V_FONT_LARGE, std::string(1, vhud_card_title[c])) + spacing;
            vhud_box(x0 + w / 2 - tl / 2 - 30, cy0 - 8, tl + 60, 56, (int) (150 * in));
            if (in > 0.35) {
                SDL_Rect line;

                x = x0 + w / 2 - tl / 2;
                for (c = 0; c < vhud_card_title.size(); c++) {
                    std::string ch(1, vhud_card_title[c]);

                    vulture_put_text_shadow(V_FONT_LARGE, ch, vulture_screen, x, cy0, tcol, black);
                    x += vulture_text_length(V_FONT_LARGE, ch) + spacing;
                }
                line.w = (int) ((tl + 20) * in), line.h = 1;
                line.x = x0 + w / 2 - line.w / 2, line.y = cy0 + 22;
                SDL_FillRect(vulture_screen, &line, scol);
                if (!vhud_card_sub.empty())
                    vulture_put_text_shadow(V_FONT_SMALL, vhud_card_sub, vulture_screen,
                                            x0 + w / 2 - vulture_text_length(V_FONT_SMALL, vhud_card_sub) / 2,
                                            cy0 + 28, scol, black);
            }
        }
    }
}


/*
 * IMPLEMENTATION_LOG s74: low-HP vignette.  Below half HP the map's edges
 * darken to blood red, stronger as HP falls.  Drawn at the end of the map
 * draw (which repaints the whole map), so the UI windows on top stay
 * clean and the overlay never stacks.
 */
static SDL_Surface *vg_surf = NULL;

static void
vulture_draw_vignette(int x0, int y0, int w, int h)
{
    int hp = Upolyd ? u.mh : u.uhp, hpmax = Upolyd ? u.mhmax : u.uhpmax;
    double r, k;
    SDL_Rect d;

    if (hpmax <= 0 || hp <= 0 || hp * 2 >= hpmax || w <= 0 || h <= 0)
        return;
    if (!vg_surf || vg_surf->w != w || vg_surf->h != h) {
        int x, y;
        Uint32 *px;

        if (vg_surf)
            SDL_FreeSurface(vg_surf);
        vg_surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                 SDL_PIXELFORMAT_ARGB8888);
        if (!vg_surf)
            return;
        SDL_LockSurface(vg_surf);
        for (y = 0; y < h; y++) {
            px = (Uint32 *) ((Uint8 *) vg_surf->pixels + y * vg_surf->pitch);
            for (x = 0; x < w; x++) {
                double dx = (x - w / 2.0) / (w / 2.0);
                double dy = (y - h / 2.0) / (h / 2.0);
                double dd = sqrt(dx * dx + dy * dy);
                double a = (dd - 0.55) / 0.75;

                if (a < 0)
                    a = 0;
                if (a > 1)
                    a = 1;
                a = a * a;
                px[x] = SDL_MapRGBA(vg_surf->format, 110, 0, 0,
                                    (Uint8) (a * 255));
            }
        }
        SDL_UnlockSurface(vg_surf);
        SDL_SetSurfaceBlendMode(vg_surf, SDL_BLENDMODE_BLEND);
    }
    r = (double) hp / hpmax;      /* < 0.5 here */
    k = (0.5 - r) / 0.5;          /* 0 at half HP, 1 near death */
    SDL_SetSurfaceAlphaMod(vg_surf, (Uint8) (90 + 165 * k));
    d.x = x0;
    d.y = y0;
    d.w = w;
    d.h = h;
    SDL_BlitSurface(vg_surf, NULL, vulture_screen, &d);
}

static SDL_Rect vhb_rect = { 0, 0, 0, 0 }; /* s126: ability bar on screen */
extern "C" void vulture_sfx(const char *); /* s131: sound/<name>.wav */

/* s131: a soft tick when the pointer moves onto another UI element */
static void
vui_hover_sfx(int id)
{
    static int last = -1;
    static Uint32 when = 0;
    Uint32 now = SDL_GetTicks();

    if (id == last)
        return;
    last = id;
    if (id >= 0 && now - when > 45) {
        when = now;
        vulture_sfx("ui_hover");
    }
}

/*
 * IMPLEMENTATION_LOG s125: modern hover and aim (Brogue / Cogmind).
 *  - Travel preview: hovering the map shows the path the hero would walk
 *    (marching dots, step count), over the terrain the hero knows.
 *  - Hover cards: a monster under the pointer gets a card - name, a threat
 *    word from its difficulty against your level, asleep/fleeing/trapped
 *    tags, and a health bar once it's hurt (or for pets).
 *  - Aim: while the game asks for a direction, a line runs from the hero
 *    toward the pointer (snapped to the 8 directions NetHack shoots in):
 *    green when the first creature in line is hostile, red when it's a pet
 *    or a peaceful, amber when it hits nothing.
 *  - Ground card: "Things that are here" shows as a card beside the map
 *    instead of a window you have to dismiss.
 */
static bool vquiet_search = false;  /* s140: resting - no search pulse/ping */
static point vhv = { -1, -1 };     /* map square under the pointer */
static Uint32 vhv_since = 0;       /* when the pointer settled there */
static bool vaim_on = false;       /* a direction prompt is up */
static point vaim_at = { -1, -1 }; /* aim cursor square */

/* can the hero (as far as they know) walk onto (x, y)? */
static bool
vmv_pass(int x, int y)
{
    int g, cm;

    if (!isok(x, y) || !levl[x][y].seenv)
        return false;
    g = glyph_at(x, y);
    if (glyph_is_unexplored(g) || glyph_is_nothing(g))
        return false;
    if (glyph_is_object(g) && glyph_to_obj(g) == BOULDER)
        return false;
    if (glyph_is_cmap(g)) {
        cm = glyph_to_cmap(g);
        if (cm <= S_trwall || cm == S_vcdoor || cm == S_hcdoor || cm == S_bars
            || cm == S_tree || cm == S_pool || cm == S_lava
            || cm == S_lavawall || cm == S_water)
            return false;
    }
    return true;
}

/* an (open) door square: no diagonal moves in or out */
static bool
vmv_door(int x, int y)
{
    return isok(x, y) && levl[x][y].seenv && IS_DOOR(levl[x][y].typ)
           && (levl[x][y].doormask & ~(D_NODOOR | D_BROKEN)) != 0;
}

/* walls and rock stop an aim line */
static bool
vmv_solid(int x, int y)
{
    int g, cm;

    if (!isok(x, y))
        return true;
    if (!levl[x][y].seenv)
        return false; /* unknown: keep going */
    g = glyph_at(x, y);
    if (!glyph_is_cmap(g))
        return false;
    cm = glyph_to_cmap(g);
    return cm <= S_trwall || cm == S_vcdoor || cm == S_hcdoor || cm == S_tree;
}

/* shortest known path hero -> (tx, ty), written target-first into out[];
   returns its length (0 = no path) */
static int
vpath_find(int tx, int ty, point *out, int maxn)
{
    static short px[ROWNO][COLNO], py[ROWNO][COLNO];
    static char seen[ROWNO][COLNO];
    static short qx[COLNO * ROWNO], qy[COLNO * ROWNO];
    int h = 0, t = 0, n = 0, k, x, y;

    if (!isok(tx, ty) || !vmv_pass(tx, ty))
        return 0;
    memset(seen, 0, sizeof seen);
    seen[u.uy][u.ux] = 1;
    qx[t] = u.ux, qy[t++] = u.uy;
    while (h < t) {
        x = qx[h], y = qy[h++];
        if (x == tx && y == ty)
            break;
        for (k = 0; k < N_DIRS; k++) {
            int nx = x + xdir[k], ny = y + ydir[k];

            if (!isok(nx, ny) || seen[ny][nx] || !vmv_pass(nx, ny))
                continue;
            if (xdir[k] && ydir[k] && (vmv_door(x, y) || vmv_door(nx, ny)))
                continue;
            seen[ny][nx] = 1;
            px[ny][nx] = (short) x, py[ny][nx] = (short) y;
            qx[t] = (short) nx, qy[t++] = (short) ny;
        }
    }
    if (!seen[ty][tx])
        return 0;
    x = tx, y = ty;
    while (!(x == u.ux && y == u.uy) && n < maxn) {
        int ox = px[y][x];

        out[n].x = x, out[n].y = y, n++;
        y = py[y][x], x = ox;
    }
    return n;
}

/* is the pointer over the map itself (not a panel on top of it)? */
static bool
vhv_over_map(void)
{
    point mp = vulture_get_mouse_pos();
    window *w = ROOTWIN ? ROOTWIN->get_window_from_point(mp) : NULL;

    return w && levwin && (w == (window *) levwin || w->parent == (window *) levwin);
}

/* NetHack shoots in 8 directions: snap a target square to one */
void
vulture_aim_snap(int tx, int ty, int *sdx, int *sdy)
{
    int dx = tx - u.ux, dy = ty - u.uy, adx = abs(dx), ady = abs(dy);

    if (!dx && !dy) {
        *sdx = *sdy = 0;
    } else if (adx > 2 * ady) {
        *sdx = sgn(dx), *sdy = 0;
    } else if (ady > 2 * adx) {
        *sdx = 0, *sdy = sgn(dy);
    } else {
        *sdx = sgn(dx), *sdy = sgn(dy);
    }
}

/* the direction key for aiming at (tx, ty) - '.' for yourself */
char
vulture_aim_key(int tx, int ty)
{
    int sdx, sdy, dd;

    vulture_aim_snap(tx, ty, &sdx, &sdy);
    if (!sdx && !sdy)
        return gc.Cmd.spkeys[NHKF_GETDIR_SELF];
    dd = xytodir(sdx, sdy);
    return dd == DIR_ERR ? 0 : gc.Cmd.dirchars[dd];
}

/* is there a map and a hero to aim from? */
bool
vulture_aim_ok(void)
{
    return levwin && vulture_windows_inited == TRUE && u.ux;
}

void
vulture_aim_set(bool on, int x, int y)
{
    vaim_on = on;
    vaim_at.x = x, vaim_at.y = y;
}

static void
vfx_ringc(int sx, int sy, double rad, Uint8 r, Uint8 g, Uint8 b, int alpha)
{
    vfx_ent e;

    memset(&e, 0, sizeof e);
    e.r = r, e.g = g, e.b = b;
    vfx_ring(sx, sy, rad, e, alpha);
}

/* the travel preview, drawn with the map */
static void
vpath_draw(int cx, int cy, int vx, int vy)
{
    static point pts[COLNO * ROWNO];
    int n, i, sx, sy;
    double ph;
    char num[16];

    if (vaim_on || vulture_aim_mode == 3 || !isok(vhv.x, vhv.y) || u_at(vhv.x, vhv.y)
        || gm.multi || go.occupation /* s145: travel/run flags outlive the walk */
        || vulture_whatis_active || u.uswallow || !vhv_over_map())
        return;
    if (distmin(u.ux, u.uy, vhv.x, vhv.y) < 2)
        return; /* a plain step */
    n = vpath_find(vhv.x, vhv.y, pts, COLNO * ROWNO);
    ph = SDL_GetTicks() / 140.0;
    if (n < 2) {
        /* s129: no known way (fog, unexplored): a faint straight line and
           the distance, so you can still judge how far it is */
        int hx, hy, tx, ty, k, steps = distmin(u.ux, u.uy, vhv.x, vhv.y) * 3;

        vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &hx, &hy);
        vfx_square_screen(vhv.x, vhv.y, cx, cy, vx, vy, &tx, &ty);
        for (k = 1; k <= steps; k++)
            vfx_glowdot(hx + (tx - hx) * k / steps, hy + (ty - hy) * k / steps,
                        200, 190, 160, 70 + (int) (40 * (0.5 + 0.5 * sin(ph - k * 0.6))));
        vfx_ringc(tx, ty, 12, 200, 190, 160, 150);
        snprintf(num, sizeof num, "~%d", distmin(u.ux, u.uy, vhv.x, vhv.y));
        vulture_put_text_shadow(V_FONT_SMALL, num, vulture_screen, tx + 16, ty - 20,
                                SDL_MapRGB(vulture_screen->format, 200, 190, 160),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        return;
    }
    {
        /* a dotted line hero -> target: 3 dots per step, a brighter one on
           each square, a soft pulse marching toward the target */
        int px0, py0;

        vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &px0, &py0);
        for (i = n - 1; i >= 0; i--) {
            int k = n - 1 - i, j, a = 150 + (int) (90 * (0.5 + 0.5 * sin(ph - k * 0.9)));

            vfx_square_screen(pts[i].x, pts[i].y, cx, cy, vx, vy, &sx, &sy);
            for (j = 1; j <= 3; j++) {
                int qx = px0 + (sx - px0) * j / 3, qy = py0 + (sy - py0) * j / 3;

                vfx_glowdot(qx, qy + 1, 0, 0, 0, a / 2);
                vfx_glowdot(qx, qy, 255, 228, 140, j == 3 ? a : a * 2 / 3);
            }
            px0 = sx, py0 = sy;
        }
    }
    vfx_square_screen(pts[0].x, pts[0].y, cx, cy, vx, vy, &sx, &sy);
    vfx_ringc(sx, sy, 12 + 2 * sin(ph * 0.6), 255, 228, 140, 220);
    snprintf(num, sizeof num, "%d", n);
    vulture_put_text_shadow(V_FONT_SMALL, num, vulture_screen, sx + 16, sy - 20,
                            SDL_MapRGB(vulture_screen->format, 235, 240, 205),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    { /* s142: danger preview - who can hit you there, traps on the way */
        struct monst *dm;
        int threat = 0, traps = 0, ly = sy - 20 + vulture_get_lineheight(V_FONT_SMALL);
        char wb[64];

        for (dm = fmon; dm; dm = dm->nmon)
            if (!DEADMONSTER(dm) && !dm->mtame && !dm->mpeaceful && canspotmon(dm)
                && !dm->msleeping && dm->mcanmove
                && distmin(dm->mx, dm->my, pts[0].x, pts[0].y) <= 1
                && !(dm->mx == pts[0].x && dm->my == pts[0].y))
                threat++;
        for (i = 1; i < n; i++) {
            int tg = glyph_at(pts[i].x, pts[i].y);

            if (glyph_is_trap(tg)) {
                int tx2, ty2;

                vfx_square_screen(pts[i].x, pts[i].y, cx, cy, vx, vy, &tx2, &ty2);
                vfx_ringc(tx2, ty2, 11, 255, 70, 60, 230);
                traps++;
            }
        }
        if (threat) {
            snprintf(wb, sizeof wb, "%d can hit you there", threat);
            vulture_put_text_shadow(V_FONT_SMALL, wb, vulture_screen, sx + 16, ly,
                                    SDL_MapRGB(vulture_screen->format, 255, 100, 80),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            ly += vulture_get_lineheight(V_FONT_SMALL);
        }
        if (traps)
            vulture_put_text_shadow(V_FONT_SMALL, traps > 1 ? "traps on the way" : "a trap on the way",
                                    vulture_screen, sx + 16, ly,
                                    SDL_MapRGB(vulture_screen->format, 255, 160, 80),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    }
}

/* ---- s144: speed, made visible ----
   NetHack gives everyone movement points each turn: 12 = one action.
   Your actions per turn: your speed, +1 action on 1/3 of turns (Fast) or
   2/3 (Very fast), cut by encumbrance (u_calc_moveamt).  A monster's:
   mcalcmove(mon, FALSE) / 12, randomly rounded.  Its actions per one of
   yours = the ratio; the threat zone is every square it can reach AND hit
   you on before you act again (path length <= that many actions). */
extern "C" int mcalcmove(struct monst *, boolean);

static double
vhero_speed(void)
{
    double s = Upolyd ? gy.youmonst.data->mmove : 12.0;

    if (Very_fast)
        s += 12.0 * 2 / 3;
    else if (Fast)
        s += 12.0 / 3;
    switch (near_capacity()) {
    case SLT_ENCUMBER: s *= 0.75; break;
    case MOD_ENCUMBER: s *= 0.5; break;
    case HVY_ENCUMBER: s *= 0.25; break;
    case EXT_ENCUMBER: s *= 0.125; break;
    }
    return s < 1 ? 1 : s;
}

/* a monster's actions for each one of yours */
static double
vmon_ratio(struct monst *m)
{
    return mcalcmove(m, FALSE) / vhero_speed();
}

static std::string
vspeed_words(struct monst *m)
{
    double r = vmon_ratio(m);
    char b[96];

    if (mcalcmove(m, FALSE) == 0)
        return "Never moves";
    if (r >= 1.0)
        snprintf(b, sizeof b, "Moves %.1f square%s per move of yours%s", r, r == 1.0 ? "" : "s",
                 r >= 1.9 ? "  -  you can't outrun it" : r > 1.05 ? "  -  faster than you"
                                                       : "");
    else
        snprintf(b, sizeof b, "Moves once per %.1f of your moves  -  you can outrun it", 1.0 / r);
    return b;
}

/*
 * IMPLEMENTATION_LOG s163: telegraphs.  A hostile with a breath or spit
 * attack that is ready (not on cooldown), lined up with you within 8
 * squares and with a clear path shows a pulsing dotted line to you in the
 * attack's colour, and its card says so; a gaze attacker that can see you
 * gets an eye ring.  What the wiki teaches ("don't stand in line with a
 * dragon"), drawn.
 */
static int
vtele_attack(struct monst *m, int *adtyp)
{
    int k;

    for (k = 0; k < NATTK; k++) {
        const struct attack *a = &m->data->mattk[k];

        if (a->aatyp == AT_BREA || a->aatyp == AT_SPIT || a->aatyp == AT_GAZE) {
            *adtyp = a->adtyp;
            return a->aatyp;
        }
    }
    return 0;
}

/* 1 if m is ready and lined up to breathe / spit at you right now */
static bool
vtele_lined(struct monst *m)
{
    int ad, at = vtele_attack(m, &ad), dx = u.ux - m->mx, dy = u.uy - m->my;

    if ((at != AT_BREA && at != AT_SPIT) || m->mpeaceful || m->mtame || m->msleeping
        || !m->mcanmove || m->mcan || (at == AT_BREA && m->mspec_used))
        return false;
    if (!(dx == 0 || dy == 0 || abs(dx) == abs(dy)) || distmin(m->mx, m->my, u.ux, u.uy) > 8
        || distmin(m->mx, m->my, u.ux, u.uy) < 2)
        return false;
    return clear_path(m->mx, m->my, u.ux, u.uy) != 0;
}

static void
vtele_colour(int ad, Uint8 *r, Uint8 *g, Uint8 *b)
{
    switch (ad) {
    case AD_FIRE: *r = 255, *g = 120, *b = 40; break;
    case AD_COLD: *r = 140, *g = 200, *b = 255; break;
    case AD_ELEC: *r = 255, *g = 245, *b = 120; break;
    case AD_ACID: case AD_DRST: *r = 140, *g = 240, *b = 90; break;
    case AD_SLEE: *r = 190, *g = 140, *b = 255; break;
    case AD_DISN: *r = 255, *g = 255, *b = 255; break;
    case AD_BLND: *r = 255, *g = 255, *b = 200; break;
    default: *r = 255, *g = 90, *b = 90; break;
    }
}

static const char *
vtele_word(int ad)
{
    switch (ad) {
    case AD_FIRE: return "fire";
    case AD_COLD: return "frost";
    case AD_ELEC: return "lightning";
    case AD_ACID: return "acid";
    case AD_DRST: return "poison";
    case AD_SLEE: return "sleep gas";
    case AD_DISN: return "disintegration";
    case AD_MAGM: return "magic missiles";
    case AD_BLND: return "blinding venom";
    }
    return "something nasty";
}

static void
vtele_draw(int cx, int cy, int vx, int vy)
{
    struct monst *m;
    double t = SDL_GetTicks() / 1000.0;

    if (!u.ux || Hallucination || vulture_whatis_active)
        return;
    for (m = fmon; m; m = m->nmon) {
        int ad = 0, at, sx, sy;
        Uint8 r, g, b;

        if (DEADMONSTER(m) || !canspotmon(m) || distmin(u.ux, u.uy, m->mx, m->my) > 12)
            continue;
        at = vtele_attack(m, &ad);
        if (!at)
            continue;
        vtele_colour(ad, &r, &g, &b);
        if (vtele_lined(m)) { /* the line it would fire along */
            int dx = sgn(u.ux - m->mx), dy = sgn(u.uy - m->my), x = m->mx, y = m->my, k = 0;

            while (!(x == u.ux && y == u.uy) && k < 10) {
                x += dx, y += dy, k++;
                vfx_square_screen(x, y, cx, cy, vx, vy, &sx, &sy);
                vfx_glowdot(sx, sy - 4, r, g, b,
                            (int) (150 + 90 * sin(t * 6.0 - k * 0.8)));
            }
            vfx_square_screen(m->mx, m->my, cx, cy, vx, vy, &sx, &sy);
            vfx_glowdot(sx, sy - 70, r, g, b, (int) (200 + 55 * sin(t * 8.0))); /* a warning spark */
            vfx_glowdot(sx, sy - 76, r, g, b, (int) (160 + 55 * sin(t * 8.0)));
        } else if (at == AT_GAZE && !m->mpeaceful && !m->mtame && m->mcansee
                   && !m->msleeping && couldsee(m->mx, m->my)) { /* an eye ring */
            int k;

            vfx_square_screen(m->mx, m->my, cx, cy, vx, vy, &sx, &sy);
            for (k = 0; k < 10; k++) {
                double a = k * 0.628 + t * 1.5;

                vfx_glowdot(sx + (int) (14 * cos(a)), sy - 70 + (int) (6 * sin(a)), r, g, b, 160);
            }
        }
    }
}

/* tint the squares where m can reach you before your next action */
static void vtile_mark(int x, int y, Uint8 r, Uint8 g, Uint8 b, int alpha, int cx, int cy,
                       int vx, int vy);
static bool vmv_pass(int x, int y);

static void
vthreat_zone(struct monst *m, int cx, int cy, int vx, int vy, int alpha)
{
    static short dist[ROWNO][COLNO];
    static short qx[COLNO * ROWNO], qy[COLNO * ROWNO];
    double r = vmon_ratio(m);
    int sure = (int) r, maybe = (int) ceil(r - 1e-9), h = 0, t = 0, x, y, k;

    if (maybe < 1)
        maybe = 1; /* slow ones still threaten their neighbours sometimes */
    if (maybe > 6)
        maybe = 6;
    if (sure > maybe)
        sure = maybe;
    for (y = 0; y < ROWNO; y++)
        for (x = 0; x < COLNO; x++)
            dist[y][x] = 99;
    dist[m->my][m->mx] = 0;
    qx[t] = m->mx, qy[t++] = m->my;
    while (h < t) {
        x = qx[h], y = qy[h++];
        if (dist[y][x] >= maybe)
            continue;
        for (k = 0; k < N_DIRS; k++) {
            int nx = x + xdir[k], ny = y + ydir[k];

            if (!isok(nx, ny) || dist[ny][nx] != 99)
                continue;
            dist[ny][nx] = dist[y][x] + 1;
            /* it can hit a square it reaches, but only walk on through
               known open ground */
            if (vmv_pass(nx, ny) || u_at(nx, ny))
                qx[t] = (short) nx, qy[t++] = (short) ny;
        }
    }
    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++) {
            if (dist[y][x] == 0 || dist[y][x] > maybe || !levl[x][y].seenv
                || (!vmv_pass(x, y) && !u_at(x, y)))
                continue;
            if (dist[y][x] <= sure)
                vtile_mark(x, y, 255, 70, 55, alpha, cx, cy, vx, vy);
            else
                vtile_mark(x, y, 255, 170, 60, alpha / 2, cx, cy, vx, vy);
        }
}

/* hovered monster's zone, or every awake hostile's while Alt is held */
static void
vthreat_draw(int cx, int cy, int vx, int vy)
{
    struct monst *m;
    bool alt = (SDL_GetModState() & KMOD_ALT) != 0;

    if (vaim_on || vulture_aim_mode || vulture_whatis_active || Hallucination || !u.ux)
        return;
    if (alt) {
        for (m = fmon; m; m = m->nmon)
            if (!DEADMONSTER(m) && canspotmon(m) && !m->mtame && !m->mpeaceful
                && !m->msleeping && m->mcanmove && distmin(u.ux, u.uy, m->mx, m->my) <= 14)
                vthreat_zone(m, cx, cy, vx, vy, 120);
        return;
    }
    if (!isok(vhv.x, vhv.y) || !vhv_over_map() || SDL_GetTicks() - vhv_since < 160)
        return;
    m = m_at(vhv.x, vhv.y);
    if (m && canspotmon(m) && !m->mtame && !u_at(vhv.x, vhv.y))
        vthreat_zone(m, cx, cy, vx, vy, (m->msleeping || !m->mcanmove) ? 70 : 150);
}

/* ---- s141: the codex (Pathos-style built-in knowledge, no wiki) ----
   Monster attacks, defences and what eating its corpse does, and what each
   trap does - straight from the game's own data. */
static const char *
vat_name(int at)
{
    switch (at) {
    case AT_CLAW: return "claw";
    case AT_BITE: return "bite";
    case AT_KICK: return "kick";
    case AT_BUTT: return "butt";
    case AT_TUCH: return "touch";
    case AT_STNG: return "sting";
    case AT_HUGS: return "crushing hug";
    case AT_SPIT: return "spit";
    case AT_ENGL: return "engulf";
    case AT_BREA: return "breath";
    case AT_EXPL: return "explodes";
    case AT_BOOM: return "explodes when killed";
    case AT_GAZE: return "gaze";
    case AT_TENT: return "tentacles";
    case AT_WEAP: return "weapon";
    case AT_MAGC: return "casts spells";
    }
    return 0;
}

static const char *
vad_name(int ad)
{
    switch (ad) {
    case AD_MAGM: return "magic missiles";
    case AD_FIRE: return "fire";
    case AD_COLD: return "cold";
    case AD_SLEE: return "puts you to sleep";
    case AD_DISN: return "disintegrates";
    case AD_ELEC: return "shock";
    case AD_DRST: return "poison, saps Str";
    case AD_ACID: return "acid";
    case AD_BLND: return "blinds";
    case AD_STUN: return "stuns";
    case AD_SLOW: return "slows you";
    case AD_PLYS: return "paralyzes";
    case AD_DRLI: return "drains levels";
    case AD_DREN: return "drains magic power";
    case AD_LEGS: return "wounds legs";
    case AD_STON: return "PETRIFIES";
    case AD_STCK: return "sticks to you";
    case AD_SGLD: return "steals gold";
    case AD_SITM: return "steals items";
    case AD_SEDU: return "seduces, steals items";
    case AD_TLPT: return "teleports you";
    case AD_RUST: return "rusts armour";
    case AD_CONF: return "confuses";
    case AD_DGST: return "DIGESTS you";
    case AD_HEAL: return "heals you";
    case AD_WRAP: return "drowns you";
    case AD_WERE: return "lycanthropy";
    case AD_DRDX: return "poison, saps Dex";
    case AD_DRCO: return "poison, saps Con";
    case AD_DRIN: return "eats your brain, saps Int";
    case AD_DISE: return "disease";
    case AD_DCAY: return "rots armour";
    case AD_SSEX: return "seduces";
    case AD_HALU: return "hallucination";
    case AD_DETH: return "death touch";
    case AD_PEST: return "pestilence";
    case AD_FAMN: return "hunger";
    case AD_SLIM: return "SLIMES you";
    case AD_ENCH: return "disenchants";
    case AD_CORR: return "corrodes armour";
    case AD_POLY: return "polymorphs you";
    case AD_CLRC: return "clerical spells";
    case AD_SPEL: return "mage spells";
    case AD_RBRE: return "random breath";
    case AD_SAMU: return "steals the Amulet";
    case AD_CURS: return "curses, steals intrinsics";
    }
    return 0;
}

static std::string
vmon_attacks(struct permonst *p)
{
    std::string s;
    int i;
    char b[96];

    for (i = 0; i < NATTK; i++) {
        const struct attack *a = &p->mattk[i];
        const char *an = vat_name(a->aatyp), *dn = vad_name(a->adtyp);

        if (!an || (a->aatyp == AT_NONE && !a->damn))
            continue;
        if (a->damn && a->damd)
            snprintf(b, sizeof b, "%s %dd%d", an, a->damn, a->damd);
        else
            snprintf(b, sizeof b, "%s", an);
        if (dn && !(a->aatyp == AT_MAGC && (a->adtyp == AD_SPEL || a->adtyp == AD_CLRC)))
            snprintf(b + strlen(b), sizeof b - strlen(b), " (%s)", dn);
        else if (a->aatyp == AT_MAGC && dn)
            snprintf(b + strlen(b), sizeof b - strlen(b), " (%s)", dn);
        if (!s.empty())
            s += ", ";
        s += b;
    }
    for (i = 0; i < NATTK; i++) /* passive defence: hurts you when you hit it */
        if (p->mattk[i].aatyp == AT_NONE && vad_name(p->mattk[i].adtyp)) {
            s += s.empty() ? "" : ", ";
            s += std::string("passive: ") + vad_name(p->mattk[i].adtyp);
        }
    return s.empty() ? "none" : s;
}

static std::string
vmon_resists(unsigned mr)
{
    static const char *const nm[8] = { "fire", "cold", "sleep", "disintegration", "shock",
                                       "poison", "acid", "petrification" };
    std::string s;
    int i;

    for (i = 0; i < 8; i++)
        if (mr & (1u << i)) {
            if (!s.empty())
                s += ", ";
            s += nm[i];
        }
    return s;
}

/* what eating this corpse does; danger 0 fine, 1 careful, 2 deadly/terrible */
static void
vcorpse_info(struct permonst *p, std::vector<std::string> &out, int *danger)
{
    int pm = monsndx(p), grant;
    bool cannibal_ok = Role_if(PM_CAVE_DWELLER) || Race_if(PM_ORC);
    std::string g;

    *danger = 0;
    if (is_rider(p)) {
        out.push_back("Eating it is fatal");
        *danger = 2;
        return;
    }
    if (touch_petrifies(p)) {
        out.push_back("Turns you to STONE if eaten (or touched barehanded)");
        *danger = 2;
    }
    if (pm == PM_GREEN_SLIME)
        out.push_back("Turns you into green slime"), *danger = 2;
    if (is_were(p))
        out.push_back("Gives you lycanthropy"), *danger = 2;
    if (your_race(p) && !cannibal_ok)
        out.push_back("Cannibalism: your god will be angry"), *danger = 2;
    if (is_domestic(p) && (p->mlet == S_DOG || p->mlet == S_FELINE) && !cannibal_ok) {
        out.push_back("A pet species: you'll aggravate monsters");
        if (*danger < 1)
            *danger = 1;
    }
    if (pm == PM_CHAMELEON || pm == PM_DOPPELGANGER || pm == PM_GENETIC_ENGINEER)
        out.push_back("Polymorphs you"), *danger = 2;
    if (poisonous(p)) {
        out.push_back(Poison_resistance ? "Poisonous - you resist it"
                                        : "Poisonous: may sap Str or hurt badly");
        if (!Poison_resistance && *danger < 1)
            *danger = 1;
    }
    if (acidic(p)) {
        out.push_back("Acidic: stings a bit, but cures stoning");
        if (*danger < 1 && !Acid_resistance)
            *danger = 1;
    }
    if (pm == PM_LIZARD)
        out.push_back("Cures stoning; never rots");
    if (pm == PM_LICHEN)
        out.push_back("Never rots");
    if (pm == PM_VIOLET_FUNGUS)
        out.push_back("Makes you hallucinate");
    if (pm == PM_BAT || pm == PM_GIANT_BAT || pm == PM_VAMPIRE_BAT)
        out.push_back("Stuns you for a while");
    if (pm == PM_QUANTUM_MECHANIC)
        out.push_back("Toggles your intrinsic speed");
    if (pm == PM_DISENCHANTER)
        out.push_back("Strips one of your intrinsics"), *danger = *danger < 1 ? 1 : *danger;
    if (pm == PM_NEWT)
        out.push_back("Restores a little magic power");
    if (pm == PM_MIND_FLAYER || pm == PM_MASTER_MIND_FLAYER)
        out.push_back("May raise Int");
    grant = p->mconveys & 0x3F; /* eating only ever gives fire..poison */
    g = vmon_resists((unsigned) grant);
    if (pm == PM_FLOATING_EYE || pm == PM_MIND_FLAYER || pm == PM_MASTER_MIND_FLAYER)
        g += g.empty() ? "telepathy" : ", telepathy";
    if (p->mlet == S_NYMPH || p->mlet == S_LEPRECHAUN || pm == PM_TENGU)
        g += g.empty() ? "teleportitis" : ", teleportitis";
    if (pm == PM_TENGU)
        g += ", teleport control";
    if (is_giant(p))
        g += g.empty() ? "strength" : ", strength";
    if (!g.empty())
        out.push_back("May grant: " + g);
    if (out.empty())
        out.push_back("Safe to eat");
}

static const char *
vtrap_desc(int t)
{
    switch (t) {
    case ARROW_TRAP: return "Arrow trap  -  shoots an arrow at you";
    case DART_TRAP: return "Dart trap  -  shoots darts, sometimes poisoned";
    case ROCKTRAP: return "Falling rock trap  -  a helmet softens it";
    case SQKY_BOARD: return "Squeaky board  -  harmless, but wakes monsters";
    case BEAR_TRAP: return "Bear trap  -  holds you for several turns";
    case LANDMINE: return "Land mine  -  heavy damage, wounds your legs";
    case ROLLING_BOULDER_TRAP: return "Rolling boulder trap  -  a boulder comes crashing";
    case SLP_GAS_TRAP: return "Sleeping gas  -  knocks you out unless you resist sleep";
    case RUST_TRAP: return "Rust trap  -  rusts iron armour and weapons";
    case FIRE_TRAP: return "Fire trap  -  burns you and your scrolls/potions";
    case PIT: return "Pit  -  you fall in and climb out for a few turns";
    case SPIKED_PIT: return "Spiked pit  -  a pit with spikes, maybe poisoned";
    case HOLE: return "Hole  -  drops you to the level below";
    case TRAPDOOR: return "Trap door  -  drops you one or more levels";
    case TELEP_TRAP: return "Teleport trap  -  sends you elsewhere on this level";
    case LEVEL_TELEP: return "Level teleporter  -  sends you to another level";
    case MAGIC_PORTAL: return "Magic portal  -  a gateway to another place";
    case WEB: return "Web  -  entangles you; strong heroes tear free";
    case STATUE_TRAP: return "Statue trap  -  the statue may come to life";
    case MAGIC_TRAP: return "Magic trap  -  unpredictable, sometimes helpful";
    case ANTI_MAGIC: return "Anti-magic field  -  drains your magic power";
    case POLY_TRAP: return "Polymorph trap  -  turns you into something else";
    case VIBRATING_SQUARE: return "The vibrating square  -  the end of your quest";
    }
    return "A known trap";
}

/* ---- s139: item stat cards (inventory hover) ----
   Only what the hero knows: dice and armour values only for a type whose
   identity is known (or that has no random appearance); enchantment only
   when known. Compared with what is wielded / worn in the same slot. */
static bool
vitem_type_known(struct obj *o)
{
    return o->dknown && (!OBJ_DESCR(objects[o->otyp]) || objects[o->otyp].oc_name_known);
}

static double
vitem_avg(struct obj *o, bool *exact)
{
    double a = (objects[o->otyp].oc_wsdam + 1) / 2.0;

    if (o->known)
        a += o->spe;
    else
        *exact = false;
    return a;
}

static struct obj *
vworn_in(int cat)
{
    switch (cat) {
    case ARM_SUIT: return uarm;
    case ARM_SHIELD: return uarms;
    case ARM_HELM: return uarmh;
    case ARM_GLOVES: return uarmg;
    case ARM_BOOTS: return uarmf;
    case ARM_CLOAK: return uarmc;
    case ARM_SHIRT: return uarmu;
    }
    return (struct obj *) 0;
}

/* s164: one line comparing o with what you use now ("" if nothing to say) */
static std::string
vitem_compare(struct obj *o)
{
    char b[200];

    if (!o || Hallucination || !vitem_type_known(o))
        return "";
    if ((o->oclass == WEAPON_CLASS || is_weptool(o)) && !is_ammo(o) && !is_launcher(o)) {
        std::string s;

        if (uwep && uwep != o && (uwep->oclass == WEAPON_CLASS || is_weptool(uwep))
            && vitem_type_known(uwep)) {
            bool ex = true;
            double d = vitem_avg(o, &ex) - vitem_avg(uwep, &ex);

            snprintf(b, sizeof b, "%s%.1f damage vs your %s%s", d >= 0 ? "+" : "", d,
                     simpleonames(uwep), ex ? "" : " (roughly)");
            s = b;
        } else if (!uwep) {
            s = "Better than bare hands";
        }
        if (bimanual(o) && uarms)
            s += s.empty() ? "Two-handed: your shield would come off"
                           : "  -  two-handed: your shield would come off";
        return s;
    }
    if (o->oclass == ARMOR_CLASS) {
        int cat = objects[o->otyp].oc_armcat, mine = objects[o->otyp].a_ac + (o->known ? o->spe : 0);
        struct obj *w = vworn_in(cat);

        if (w == o)
            return "";
        if (!w) {
            snprintf(b, sizeof b, "AC %d better - nothing worn there now%s", mine,
                     o->known ? "" : " (enchantment unknown)");
            return b;
        }
        if (vitem_type_known(w)) {
            int theirs = objects[w->otyp].a_ac + (w->known ? w->spe : 0), d = mine - theirs;
            int mc = objects[o->otyp].a_can - objects[w->otyp].a_can;

            if (d)
                snprintf(b, sizeof b, "Instead of your %s: AC %d %s", simpleonames(w), d > 0 ? d : -d,
                         d > 0 ? "better" : "worse");
            else
                snprintf(b, sizeof b, "Same AC as your %s", simpleonames(w));
            if (mc) {
                size_t l = strlen(b);

                snprintf(b + l, sizeof b - l, ", magic cancellation %+d", mc);
            }
            if (!o->known || !w->known) {
                size_t l = strlen(b);

                snprintf(b + l, sizeof b - l, " (enchantments not all known)");
            }
            return b;
        }
    }
    return "";
}

std::string
vulture_item_info(struct obj *o)
{
    static const char *const slot[7] = { "body armour", "shield", "helmet", "gloves",
                                         "boots", "cloak", "shirt" };
    std::string s;
    char b[160];
    bool tk;

    if (!o || Hallucination)
        return s;
    tk = vitem_type_known(o);
    if ((o->oclass == WEAPON_CLASS || is_weptool(o)) && tk) {
        int sk = weapon_type(o);

        snprintf(b, sizeof b, "Damage: d%d vs small, d%d vs large%s",
                 objects[o->otyp].oc_wsdam, objects[o->otyp].oc_wldam,
                 o->known ? "" : "  (enchantment unknown)");
        s += b;
        if (o->known) {
            snprintf(b, sizeof b, "  %+d", o->spe);
            s += b;
        }
        if (bimanual(o))
            s += uarms ? "\nTwo-handed - your shield would come off" : "\nTwo-handed"; /* s164 */
        if (sk != P_NONE) {
            static const char *const lvl[] = { "Restricted", "Unskilled", "Basic",
                                               "Skilled", "Expert", "Master",
                                               "Grand Master" };
            int p = P_SKILL(sk);

            snprintf(b, sizeof b, "\nSkill: %s (%s)", skill_name(sk),
                     p >= 0 && p <= 6 ? lvl[p] : "?");
            s += b;
        }
        if (uwep && uwep != o && (uwep->oclass == WEAPON_CLASS || is_weptool(uwep))
            && vitem_type_known(uwep) && !is_ammo(o) && !is_launcher(o)) {
            bool ex = true;
            double d = vitem_avg(o, &ex) - vitem_avg(uwep, &ex);

            snprintf(b, sizeof b, "\nVs your %s: %s%.1f average damage%s",
                     simpleonames(uwep), d >= 0 ? "+" : "", d, ex ? "" : " (roughly)");
            s += b;
        }
    } else if (o->oclass == ARMOR_CLASS && tk) {
        int cat = objects[o->otyp].oc_armcat, ac = objects[o->otyp].a_ac;
        struct obj *w = vworn_in(cat);

        if (o->known)
            snprintf(b, sizeof b, "Armour class: %d (%d %+d)", ac + o->spe, ac, o->spe);
        else
            snprintf(b, sizeof b, "Armour class: %d + enchantment unknown", ac);
        s += b;
        snprintf(b, sizeof b, "\nSlot: %s", cat >= 0 && cat < 7 ? slot[cat] : "?");
        s += b;
        if (objects[o->otyp].a_can) {
            snprintf(b, sizeof b, "\nMagic cancellation: %d", objects[o->otyp].a_can);
            s += b;
        }
        {
            std::string cmp = vitem_compare(o); /* s164 */

            if (!cmp.empty())
                s += "\n" + cmp;
            else if (w && w != o)
                s += std::string("\nWorn now: ") + simpleonames(w);
        }
    } else if (o->otyp == CORPSE && ismnum(o->corpsenm)) { /* s141 */
        std::vector<std::string> ln;
        int dg;
        size_t i;

        vcorpse_info(&mons[o->corpsenm], ln, &dg);
        for (i = 0; i < ln.size(); i++) {
            if (i)
                s += "\n";
            s += ln[i];
        }
        if (o->corpsenm != PM_LIZARD && o->corpsenm != PM_LICHEN)
            s += "\nEat it fresh: corpses go bad after about 30 turns";
    } else if (o->oclass == FOOD_CLASS && o->otyp != CORPSE && tk) {
        snprintf(b, sizeof b, "Nutrition: %d, takes %d turn%s to eat",
                 objects[o->otyp].oc_nutrition, objects[o->otyp].oc_delay,
                 objects[o->otyp].oc_delay == 1 ? "" : "s");
        s += b;
    } else if (o->oclass == SPBOOK_CLASS && tk && o->otyp != SPE_BLANK_PAPER
               && o->otyp != SPE_NOVEL && o->otyp != SPE_BOOK_OF_THE_DEAD) {
        snprintf(b, sizeof b, "Spell level %d", objects[o->otyp].oc_level);
        s += b;
    } else if (!tk) {
        s += "Unidentified";
    }
    if (!o->bknown)
        s += s.empty() ? "Curse status unknown" : "\nCurse status unknown";
    if (o->oclass == WAND_CLASS && !o->known && tk)
        s += "\nCharges unknown";
    snprintf(b, sizeof b, "%sWeight: %d", s.empty() ? "" : "\n", (int) o->owt);
    s += b;
    return s;
}

/* ---- s138: preview area effects before you commit ---- */
static void vbox(int x, int y, int w, int h, int alpha);

static const Uint8 vzc[6][3] = { { 170, 200, 255 }, { 255, 140, 50 }, { 130, 220, 255 },
                                 { 200, 150, 255 }, { 190, 90, 255 }, { 255, 250, 160 } };
static const char *const vzn[6] = { "Magic missile", "Fire", "Frost", "Sleep",
                                    "Death", "Lightning" };

/* screen offsets of one map step in x and in y (follows view rotation) */
static void
vtile_axes(int cx, int cy, int vx, int vy, double *ax, double *ay, double *bx, double *by)
{
    int x0, y0, x1, y1, x2, y2, hx = u.ux, hy = u.uy;

    vfx_square_screen(hx, hy, cx, cy, vx, vy, &x0, &y0);
    vfx_square_screen(hx + 1, hy, cx, cy, vx, vy, &x1, &y1);
    vfx_square_screen(hx, hy + 1, cx, cy, vx, vy, &x2, &y2);
    *ax = x1 - x0, *ay = y1 - y0, *bx = x2 - x0, *by = y2 - y0;
}

/* a floor tile outlined (and faintly filled) in a colour */
static void
vtile_mark(int x, int y, Uint8 r, Uint8 g, Uint8 b, int alpha, int cx, int cy, int vx,
           int vy)
{
    double ax, ay, bx, by, px[4], py[4];
    int sx, sy, k;
    vfx_ent e;

    if (!isok(x, y))
        return;
    vtile_axes(cx, cy, vx, vy, &ax, &ay, &bx, &by);
    vfx_square_screen(x, y, cx, cy, vx, vy, &sx, &sy);
    memset(&e, 0, sizeof e);
    e.r = r, e.g = g, e.b = b;
    for (k = 0; k < 4; k++) {
        double s = (k == 0 || k == 3) ? -0.46 : 0.46, t = (k < 2) ? -0.46 : 0.46;

        px[k] = sx + s * ax + t * bx, py[k] = sy + s * ay + t * by;
    }
    vfx_dline(px[0], py[0], px[1], py[1], e, alpha);
    vfx_dline(px[1], py[1], px[2], py[2], e, alpha);
    vfx_dline(px[2], py[2], px[3], py[3], e, alpha);
    vfx_dline(px[3], py[3], px[0], py[0], e, alpha);
    for (k = 0; k < 4; k++) /* inner glow */
        vfx_dline(sx, sy, (sx + px[k]) / 2, (sy + py[k]) / 2, e, alpha / 3);
}

/* the little card that says, in words, what the effect will do */
static void
vaoe_card(const std::vector<std::string> &ln, const std::vector<Uint32> &col)
{
    point mp = vulture_get_mouse_pos();
    int lh = vulture_get_lineheight(V_FONT_SMALL), bw = 0, bh, bx, by;
    size_t i;

    for (i = 0; i < ln.size(); i++)
        if (vulture_text_length(V_FONT_SMALL, ln[i]) > bw)
            bw = vulture_text_length(V_FONT_SMALL, ln[i]);
    bw += 24, bh = (int) ln.size() * lh + 12;
    bx = mp.x + 22, by = mp.y + 26;
    if (bx + bw > vulture_screen->w - 4)
        bx = mp.x - 22 - bw;
    if (by + bh > vulture_screen->h - 4)
        by = mp.y - 14 - bh;
    vbox(bx, by, bw, bh, 215);
    for (i = 0; i < ln.size(); i++)
        vulture_put_text_shadow(V_FONT_SMALL, ln[i], vulture_screen, bx + 12,
                                by + 6 + (int) i * lh, col[i],
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
}

/* who stands on these squares: hostiles, friends, you */
static void
vaoe_victims(const std::vector<point> &sq, std::string *hit, int *nh, std::string *pal,
             bool *you)
{
    size_t i, j;
    std::vector<struct monst *> done;

    *nh = 0, *you = false;
    for (i = 0; i < sq.size(); i++) {
        struct monst *m;
        bool dup = false;

        if (u_at(sq[i].x, sq[i].y))
            *you = true;
        if (!(m = m_at(sq[i].x, sq[i].y)) || !canspotmon(m) || u_at(sq[i].x, sq[i].y))
            continue;
        for (j = 0; j < done.size(); j++)
            if (done[j] == m)
                dup = true;
        if (dup)
            continue; /* a bounce passing it twice is still one victim */
        done.push_back(m);
        if (m->mtame || m->mpeaceful) {
            if (pal->empty())
                *pal = (m->mtame ? "your " : "the peaceful ")
                       + std::string(pmname(m->data, Mgender(m)));
        } else {
            if (*nh < 3)
                *hit += (*nh ? ", " : "") + std::string(pmname(m->data, Mgender(m)));
            ++*nh;
        }
    }
}

/* deterministic copy of zap.c's bounce_dir (the 1-in-75 bounce-back and
   the coin flip on corners are luck, the preview takes the usual case) */
static void
vray_bounce(int sx, int sy, int *dx, int *dy)
{
    int lsx = sx - *dx, lsy = sy - *dy, bounce = 0;

    if (!*dx || !*dy) {
        *dx = -*dx, *dy = -*dy;
        return;
    }
    if (isok(sx, lsy) && ZAP_POS(levl[sx][lsy].typ) && !closed_door(sx, lsy)
        && (IS_ROOM(levl[sx][lsy].typ)
            || (isok(sx + *dx, lsy) && ZAP_POS(levl[sx + *dx][lsy].typ))))
        bounce = 1;
    if (!bounce && isok(lsx, sy) && ZAP_POS(levl[lsx][sy].typ) && !closed_door(lsx, sy)
        && (IS_ROOM(levl[lsx][sy].typ)
            || (isok(lsx, sy + *dy) && ZAP_POS(levl[lsx][sy + *dy].typ))))
        bounce = 2;
    if (bounce == 0)
        *dx = -*dx, *dy = -*dy;
    else if (bounce == 1)
        *dy = -*dy;
    else
        *dx = -*dx;
}

/* follow a ray from the hero up to 13 squares over ground the hero knows;
   explode = an unskilled fireball (stops before the first obstacle) */
static int
vray_path(int dx, int dy, bool explode, point *pts, int *steps, int *stop)
{
    int x = u.ux, y = u.uy, step, n = 0;

    *stop = 0;
    for (step = 1; step <= 13; step++) {
        x += dx, y += dy;
        if (!isok(x, y) || !levl[x][y].seenv) {
            *stop = -step; /* into the unknown */
            break;
        }
        if (!ZAP_POS(levl[x][y].typ) || closed_door(x, y)) {
            if (explode) {
                *stop = step;
                break;
            }
            vray_bounce(x, y, &dx, &dy);
            continue;
        }
        pts[n].x = x, pts[n].y = y, steps[n++] = step;
    }
    return n;
}

static void
vray_draw(int sdx, int sdy, int cx, int cy, int vx, int vy)
{
    static point pts[16];
    static int steps[16];
    int n, i, stop, hx, hy, px, py, sx, sy, d = vulture_aim_dmg % 6, nh;
    bool fb = vulture_aim_mode == 2, you;
    Uint8 r = vzc[d][0], g = vzc[d][1], b = vzc[d][2];
    double ph = SDL_GetTicks() / 120.0;
    std::vector<point> path, blast;
    std::vector<std::string> ln;
    std::vector<Uint32> col;
    std::string hit, pal;
    char buf[96];

    n = vray_path(sdx, sdy, fb, pts, steps, &stop);
    vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &hx, &hy);
    px = hx, py = hy;
    for (i = 0; i < n; i++) {
        int k, a = steps[i] <= 7 ? 230 : 40 + 190 * (13 - steps[i]) / 6;
        bool jump = i > 0 && distmin(pts[i].x, pts[i].y, pts[i - 1].x, pts[i - 1].y) > 1;

        vfx_square_screen(pts[i].x, pts[i].y, cx, cy, vx, vy, &sx, &sy);
        if (!jump)
            for (k = 1; k <= 4; k++) {
                int qx = px + (sx - px) * k / 4, qy = py + (sy - py) * k / 4;
                int aa = a * (60 + (int) (40 * (0.5 + 0.5 * sin(ph - (i * 4 + k) * 0.5)))) / 100;

                vfx_glowdot(qx, qy + 1, 0, 0, 0, aa / 2);
                vfx_glowdot(qx, qy, r, g, b, aa);
            }
        px = sx, py = sy;
        path.push_back(pts[i]);
        vtile_mark(pts[i].x, pts[i].y, r, g, b, a * 2 / 3, cx, cy, vx, vy);
    }
    if (fb && n) { /* where the fireball can burst */
        int first = -1, j, dx2, dy2;

        if (stop > 0 && stop <= 8)
            first = n - 1; /* hits a wall before range runs out: certain */
        else
            for (j = 0; j < n; j++)
                if (steps[j] >= 7) {
                    first = j;
                    break;
                }
        if (first < 0)
            first = n - 1;
        for (j = first; j < n; j++)
            for (dx2 = -1; dx2 <= 1; dx2++)
                for (dy2 = -1; dy2 <= 1; dy2++) {
                    point q;
                    size_t t;
                    bool dup = false;

                    q.x = pts[j].x + dx2, q.y = pts[j].y + dy2;
                    for (t = 0; t < blast.size(); t++)
                        if (blast[t].x == q.x && blast[t].y == q.y)
                            dup = true;
                    if (!dup && isok(q.x, q.y))
                        blast.push_back(q);
                }
        for (j = 0; j < (int) blast.size(); j++)
            vtile_mark(blast[j].x, blast[j].y, r, g, b,
                       first == n - 1 ? 200 : 120, cx, cy, vx, vy);
    }
    { /* rings on everyone it will reach */
        std::vector<point> all = path;

        all.insert(all.end(), blast.begin(), blast.end());
        for (i = 0; i < (int) all.size(); i++) {
            struct monst *m = m_at(all[i].x, all[i].y);

            vfx_square_screen(all[i].x, all[i].y, cx, cy, vx, vy, &sx, &sy);
            if (u_at(all[i].x, all[i].y))
                vfx_ringc(sx, sy, 18 + 3 * sin(ph), 255, 60, 50, 240);
            else if (m && canspotmon(m))
                vfx_ringc(sx, sy, 15 - 2 * sin(ph),
                          (m->mtame || m->mpeaceful) ? 255 : 255,
                          (m->mtame || m->mpeaceful) ? 170 : 70,
                          (m->mtame || m->mpeaceful) ? 40 : 60, 235);
        }
        vaoe_victims(all, &hit, &nh, &pal, &you);
    }
    snprintf(buf, sizeof buf, fb ? "Fireball  -  flies 7-13 squares, then bursts 3x3"
                                 : "%s ray  -  reaches 7-13 squares, bounces off walls",
             vzn[d]);
    ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, r, g, b));
    if (nh) {
        snprintf(buf, sizeof buf, "Hits: %s%s", hit.c_str(), nh > 3 ? ", ..." : "");
        ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, 140, 230, 150));
    } else {
        ln.push_back("Nothing hostile in its path");
        col.push_back(SDL_MapRGB(vulture_screen->format, 170, 170, 170));
    }
    if (!pal.empty()) {
        snprintf(buf, sizeof buf, "Careful: %s is in the way!", pal.c_str());
        ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, 255, 180, 60));
    }
    if (you) {
        ln.push_back(fb ? "You are inside the blast!" : "It bounces back at you!");
        col.push_back(SDL_MapRGB(vulture_screen->format, 255, 80, 60));
    }
    if (stop < 0) {
        ln.push_back("Beyond that: unexplored");
        col.push_back(SDL_MapRGB(vulture_screen->format, 150, 150, 160));
    }
    vaoe_card(ln, col);
}

/* skilled fireball / cone of cold: the blast around the chosen square */
static void
vblast_draw(int cx, int cy, int vx, int vy)
{
    int x = vhv.x, y = vhv.y, dx, dy, d = vulture_aim_dmg % 6, nh, sx, sy;
    Uint8 r = vzc[d][0], g = vzc[d][1], b = vzc[d][2];
    double ph = SDL_GetTicks() / 120.0;
    std::vector<point> core, spill;
    std::vector<std::string> ln;
    std::vector<Uint32> col;
    std::string hit, pal;
    bool you, far, blind;
    char buf[96];
    struct monst *tm;

    if (vulture_aim_mode != 3 || !isok(x, y) || !vhv_over_map())
        return;
    far = distmin(u.ux, u.uy, x, y) > 10;
    blind = (!u_at(x, y) && !cansee(x, y) && (!(tm = m_at(x, y)) || !canspotmon(tm)))
            || IS_STWALL(levl[x][y].typ);
    for (dx = -2; dx <= 2; dx++)
        for (dy = -2; dy <= 2; dy++) {
            point q;

            q.x = x + dx, q.y = y + dy;
            if (!isok(q.x, q.y))
                continue;
            if (abs(dx) <= 1 && abs(dy) <= 1)
                core.push_back(q);
            else
                spill.push_back(q);
        }
    {
        size_t i;
        int a = (far || blind) ? 90 : 210;

        for (i = 0; i < spill.size(); i++)
            vtile_mark(spill[i].x, spill[i].y, r, g, b, a / 3, cx, cy, vx, vy);
        for (i = 0; i < core.size(); i++)
            vtile_mark(core[i].x, core[i].y, r, g, b, a, cx, cy, vx, vy);
        for (i = 0; i < core.size() + spill.size(); i++) {
            point q = i < core.size() ? core[i] : spill[i - core.size()];
            struct monst *m = m_at(q.x, q.y);

            vfx_square_screen(q.x, q.y, cx, cy, vx, vy, &sx, &sy);
            if (u_at(q.x, q.y))
                vfx_ringc(sx, sy, 18 + 3 * sin(ph), 255, 60, 50, i < core.size() ? 240 : 140);
            else if (m && canspotmon(m))
                vfx_ringc(sx, sy, 15 - 2 * sin(ph), 255,
                          (m->mtame || m->mpeaceful) ? 170 : 70,
                          (m->mtame || m->mpeaceful) ? 40 : 60, i < core.size() ? 235 : 130);
        }
    }
    vaoe_victims(core, &hit, &nh, &pal, &you);
    snprintf(buf, sizeof buf, "%s  -  2 to 9 bursts on this 3x3, some spill 1 further",
             d == 2 ? "Cone of cold" : "Fireball");
    ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, r, g, b));
    if (far) {
        ln.push_back("Too far (over 10)  -  the spell will dissipate");
        col.push_back(SDL_MapRGB(vulture_screen->format, 255, 80, 60));
    } else if (blind) {
        ln.push_back("You can't see there  -  your mind won't lock on");
        col.push_back(SDL_MapRGB(vulture_screen->format, 255, 80, 60));
    } else if (nh) {
        snprintf(buf, sizeof buf, "Caught: %s%s", hit.c_str(), nh > 3 ? ", ..." : "");
        ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, 140, 230, 150));
    } else {
        ln.push_back("No hostiles in the blast");
        col.push_back(SDL_MapRGB(vulture_screen->format, 170, 170, 170));
    }
    if (!pal.empty() && !far && !blind) {
        snprintf(buf, sizeof buf, "Careful: %s is in the blast!", pal.c_str());
        ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, 255, 180, 60));
    }
    if (!far && !blind && pal.empty()) { /* friends in the spill-over ring */
        std::string h2, p2;
        int n2;
        bool y2;

        vaoe_victims(spill, &h2, &n2, &p2, &y2);
        if (!p2.empty()) {
            snprintf(buf, sizeof buf, "A spill-over burst may hit %s", p2.c_str());
            ln.push_back(buf), col.push_back(SDL_MapRGB(vulture_screen->format, 255, 180, 60));
        }
    }
    if (u_at(x, y)) {
        ln.push_back("Cast on yourself: you take it all!");
        col.push_back(SDL_MapRGB(vulture_screen->format, 255, 80, 60));
    } else if (you && !far && !blind) {
        ln.push_back("You are inside the blast!");
        col.push_back(SDL_MapRGB(vulture_screen->format, 255, 80, 60));
    } else if (!far && !blind && distmin(u.ux, u.uy, x, y) == 2) {
        ln.push_back("A spill-over burst can reach you");
        col.push_back(SDL_MapRGB(vulture_screen->format, 255, 180, 60));
    }
    vaoe_card(ln, col);
}

/* the aim line, drawn with the map */
static void
vaim_draw(int cx, int cy, int vx, int vy)
{
    int sdx, sdy, k, last = 0, sx, sy, ex, ey, hx, hy, tgt = 0;
    Uint8 r = 255, g = 200, b = 90;
    double ph = SDL_GetTicks() / 120.0;

    if (!vaim_on || !isok(vaim_at.x, vaim_at.y))
        return;
    vfx_square_screen(u.ux, u.uy, cx, cy, vx, vy, &hx, &hy);
    vulture_aim_snap(vaim_at.x, vaim_at.y, &sdx, &sdy);
    { /* s129: range to the pointer, known ground or not */
        char rng[16];
        int px, py, dr = distmin(u.ux, u.uy, vaim_at.x, vaim_at.y);

        if (dr > 0) {
            vfx_square_screen(vaim_at.x, vaim_at.y, cx, cy, vx, vy, &px, &py);
            vfx_ringc(px, py, 9, 255, 255, 255, 110);
            snprintf(rng, sizeof rng, "%d", dr);
            vulture_put_text_shadow(V_FONT_SMALL, rng, vulture_screen, px + 13, py - 18,
                                    SDL_MapRGB(vulture_screen->format, 255, 255, 255),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        }
    }
    if (!sdx && !sdy) { /* yourself */
        vfx_ringc(hx, hy, 16 + 3 * sin(ph), r, g, b, 220);
        if (vulture_aim_mode == 1 || vulture_aim_mode == 2) { /* s138 */
            std::vector<std::string> ln;
            std::vector<Uint32> col;

            ln.push_back(std::string(vzn[vulture_aim_dmg % 6]) + " - aimed at yourself!");
            col.push_back(SDL_MapRGB(vulture_screen->format, 255, 80, 60));
            vaoe_card(ln, col);
        }
        return;
    }
    if (vulture_aim_mode == 1 || vulture_aim_mode == 2) { /* s138: show the outcome */
        vray_draw(sdx, sdy, cx, cy, vx, vy);
        return;
    }
    for (k = 1; k <= 13; k++) {
        int x = u.ux + sdx * k, y = u.uy + sdy * k;
        struct monst *m;

        if (vmv_solid(x, y))
            break;
        last = k;
        if (!tgt && (m = m_at(x, y)) != 0 && canspotmon(m)) {
            tgt = k;
            if (m->mtame || m->mpeaceful)
                r = 255, g = 80, b = 70;   /* careful: a friend in the way */
            else
                r = 110, g = 255, b = 140; /* clear shot at a hostile */
        }
    }
    if (!last) { /* blocked right away: a small red cross */
        vfx_ent e;

        memset(&e, 0, sizeof e);
        e.r = 255, e.g = 70, e.b = 60;
        vfx_square_screen(u.ux + sdx, u.uy + sdy, cx, cy, vx, vy, &sx, &sy);
        vfx_dline(sx - 7, sy - 4, sx + 7, sy + 4, e, 230);
        vfx_dline(sx - 7, sy + 4, sx + 7, sy - 4, e, 230);
        return;
    }
    vfx_square_screen(u.ux + sdx * last, u.uy + sdy * last, cx, cy, vx, vy, &ex, &ey);
    for (k = 0; k <= last * 4; k++) {
        double f = (double) k / (last * 4);
        int a = 120 + (int) (100 * (0.5 + 0.5 * sin(ph - k * 0.5)));

        sx = hx + (int) ((ex - hx) * f), sy = hy + (int) ((ey - hy) * f);
        vfx_glowdot(sx, sy + 1, 0, 0, 0, a / 2);
        vfx_glowdot(sx, sy, r, g, b, a);
    }
    if (tgt) {
        vfx_square_screen(u.ux + sdx * tgt, u.uy + sdy * tgt, cx, cy, vx, vy, &sx, &sy);
        vfx_ringc(sx, sy, 15 - 2 * sin(ph), r, g, b, 235);
        vfx_ringc(sx, sy, 8, r, g, b, 160);
    }
}

/* a dark box of any size */
static void
vbox(int x, int y, int w, int h, int alpha)
{
    int yy, xx;

    for (yy = 0; yy < h; yy += 160)
        for (xx = 0; xx < w; xx += 640)
            vhud_box(x + xx, y + yy, w - xx > 640 ? 640 : w - xx,
                     h - yy > 160 ? 160 : h - yy, alpha);
}

/* s134 (Brogue): the odds - how often you hit it, how often it hits you
   and for what share of your current HP, from the game's own formulas
   (find_roll_to_hit; mattacku's AC + 10 + level; average attack dice) */
static void
vodds(struct monst *m, int *you_hit, int *it_hit, int *it_pct)
{
    int tmp, cnt = 0, pen = 0, i, t2, ac = u.uac, hp = Upolyd ? u.mh : u.uhp;
    double dmg = 0;

    tmp = find_roll_to_hit(m, AT_WEAP, uwep, &cnt, &pen);
    *you_hit = (tmp - 1) * 5;
    /* negative AC: AC_VALUE rolls -rnd(-AC); use its average */
    t2 = (ac >= 0 ? ac : -((-ac + 1) / 2)) + 10 + m->m_lev;
    *it_hit = (t2 - 1) * 5;
    for (i = 0; i < NATTK; i++) {
        struct attack *a = &m->data->mattk[i];

        switch (a->aatyp) {
        case AT_CLAW: case AT_BITE: case AT_KICK: case AT_BUTT: case AT_TUCH:
        case AT_STNG: case AT_HUGS: case AT_WEAP: case AT_TENT:
            if (a->damn && a->damd)
                dmg += a->damn * (a->damd + 1) / 2.0;
            else if (a->aatyp == AT_WEAP)
                dmg += 3.5; /* a typical weapon */
            break;
        default:
            break;
        }
    }
    *you_hit = *you_hit < 0 ? 0 : *you_hit > 100 ? 100 : *you_hit;
    *it_hit = *it_hit < 5 ? 5 : *it_hit > 100 ? 100 : *it_hit;
    *it_pct = hp > 0 ? (int) (100.0 * dmg / hp + 0.5) : 100;
}

/* the hover card over a monster (crisp: drawn after the zoom) */
static void
vcard_draw(const std::string &name)
{
    struct monst *m;
    point mp = vulture_get_mouse_pos();
    Uint32 now = SDL_GetTicks(), age;
    const char *threat;
    Uint8 tr, tg, tb;
    std::string tags, title = name;
    int lh = vulture_get_lineheight(V_FONT_SMALL), bw, bh, bx, by, d, fade;
    bool bar;
    SDL_Rect rc;

    if (vaim_on || vulture_aim_mode == 3 || !isok(vhv.x, vhv.y) || u_at(vhv.x, vhv.y) || Hallucination
        || vulture_whatis_active || !vhv_over_map() || name.empty())
        return;
    m = m_at(vhv.x, vhv.y);
    if (!m || DEADMONSTER(m) || !canspotmon(m))
        return;
    age = now - vhv_since;
    if (age < 160)
        return;
    fade = age < 280 ? (int) (255 * (age - 160) / 120.0) : 255;
    d = (int) m->data->difficulty - (int) u.ulevel;
    if (m->mtame)
        threat = "Ally", tr = 120, tg = 230, tb = 140;
    else if (m->mpeaceful)
        threat = "Peaceful", tr = 130, tg = 190, tb = 255;
    else if (d <= -4)
        threat = "Trivial", tr = 170, tg = 170, tb = 170;
    else if (d <= 0)
        threat = "Fair fight", tr = 220, tg = 230, tb = 150;
    else if (d <= 3)
        threat = "Dangerous", tr = 255, tg = 160, tb = 60;
    else
        threat = "Deadly", tr = 255, tg = 70, tb = 60;
    if (!m->mtame && (m->msleeping || !m->mcanmove)) /* s137: asleep = no threat now */
        tr = (Uint8) ((tr + 140) / 2), tg = (Uint8) ((tg + 150) / 2), tb = (Uint8) ((tb + 170) / 2);
    tags = threat;
    if (m->msleeping)
        tags += "  -  asleep";
    else if (!m->mcanmove)
        tags += "  -  helpless";
    if (m->mflee)
        tags += "  -  fleeing";
    if (m->mtrapped)
        tags += "  -  trapped";
    if (!m->mcansee) /* s162: what your blows (and spells) did to it */
        tags += "  -  blinded";
    if (m->mstun)
        tags += "  -  stunned";
    if (m->mconf)
        tags += "  -  confused";
    if (m->mspeed == MSLOW)
        tags += "  -  slowed";
    if (!title.empty())
        title[0] = (char) toupper((unsigned char) title[0]);
    bar = m->mtame || m->mhp < m->mhpmax;
    std::string odds1, odds2, odds3;
    int risk = 0, wcol = 0;
    bool zz = m->msleeping || !m->mcanmove;

    if (!m->mtame && !m->mpeaceful) { /* s134 */
        int yh, ih, ip;
        char ob[96];

        vodds(m, &yh, &ih, &ip);
        if (zz) { /* s137: what a sneak attack gets you, and will it wake */
            snprintf(ob, sizeof ob, "Sneak attack: always hits, triple damage%s",
                     Role_if(PM_ROGUE) ? ", backstab" : ""); /* s146 */
            odds1 = ob;
            if (m->msleeping)
                odds2 = "Asleep  -  it won't fight back until it wakes";
            else if (m->mfrozen) {
                snprintf(ob, sizeof ob, "Helpless for about %d more turn%s", m->mfrozen,
                         m->mfrozen == 1 ? "" : "s");
                odds2 = ob;
            } else
                odds2 = "Helpless  -  it can't fight back";
            if (m->msleeping) {
                if (Stealth && m->data != &mons[PM_ETTIN])
                    odds3 = "Your steps won't wake it  -  you're stealthy", wcol = 0;
                else if (m->data == &mons[PM_ETTIN] && Stealth)
                    odds3 = "Ettins hear even stealthy steps", wcol = 1;
                else if (m->data->mlet == S_NYMPH || m->data->mlet == S_LEPRECHAUN
                         || m->data == &mons[PM_JABBERWOCK])
                    odds3 = "A deep sleeper  -  it rarely wakes", wcol = 0;
                else if (Aggravate_monster)
                    odds3 = "You aggravate monsters  -  it wakes once it sees you", wcol = 2;
                else if (m->data->mlet == S_DOG || m->data->mlet == S_HUMAN)
                    odds3 = "Wakes the moment it sees you", wcol = 2;
                else
                    odds3 = "Each turn it sees you nearby: 1 in 7 it wakes", wcol = 1;
            }
        } else {
            if (vulture_unaware(m)) /* s146 */
                snprintf(ob, sizeof ob, "You hit it %d%%  -  it hasn't noticed you: AMBUSH x2", yh);
            else
                snprintf(ob, sizeof ob, "You hit it %d%%  -  crits %d%%  -  Ctrl+click to aim", yh,
                         vulture_crit_chance()); /* s156 */
            odds1 = ob;
            snprintf(ob, sizeof ob, "It hits you %d%%, ~%d%% of your health", ih, ip);
            odds2 = ob;
            risk = ip * ih / 100;
            { /* s163: the telegraph, in words */
                static char tb[96];
                int ad2 = 0, at2 = vtele_attack(m, &ad2);

                if (vtele_lined(m)) {
                    snprintf(tb, sizeof tb, "Lined up with you - it can %s %s!",
                             at2 == AT_SPIT ? "spit" : "breathe", vtele_word(ad2));
                    odds3 = tb, wcol = 2;
                } else if (at2 == AT_BREA || at2 == AT_SPIT) {
                    snprintf(tb, sizeof tb, "It %s %s in straight lines - stay off them",
                             at2 == AT_SPIT ? "spits" : "breathes", vtele_word(ad2));
                    odds3 = tb, wcol = 1;
                } else if (at2 == AT_GAZE) {
                    odds3 = "Its gaze works whenever it can see you", wcol = 1;
                }
            }
        }
    }
    /* s141: the codex - hold Shift for everything about it */
    std::vector<std::string> deep;
    std::vector<Uint32> dcol;
    {
        bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
        struct permonst *p = m->data;
        std::vector<std::string> cl;
        int dg;

        vcorpse_info(p, cl, &dg);
        if (shift) {
            char b[160];
            int ys = 12 * (Very_fast ? 5 : Fast ? 4 : 3) / 3;
            std::string at = vmon_attacks(p), rs = vmon_resists(p->mresists), fl;
            size_t i, cut;

            snprintf(b, sizeof b, "Level %d   AC %d   MR %d   Speed %d %s", p->mlevel, p->ac,
                     p->mr, p->mmove,
                     p->mmove > ys ? "(faster than you)" : p->mmove < ys ? "(slower than you)"
                                                         : "(as fast as you)");
            deep.push_back(b), dcol.push_back(SDL_MapRGB(vulture_screen->format, 220, 215, 200));
            at = "Attacks: " + at;
            while (at.size() > 52 && (cut = at.rfind(", ", 52)) != std::string::npos) {
                deep.push_back(at.substr(0, cut + 1));
                dcol.push_back(SDL_MapRGB(vulture_screen->format, 255, 190, 150));
                at = "   " + at.substr(cut + 2);
            }
            deep.push_back(at), dcol.push_back(SDL_MapRGB(vulture_screen->format, 255, 190, 150));
            if (!rs.empty())
                deep.push_back("Resists: " + rs),
                    dcol.push_back(SDL_MapRGB(vulture_screen->format, 150, 200, 255));
            if (is_flyer(p))
                fl += "flies, ";
            if (is_swimmer(p))
                fl += "swims, ";
            if (regenerates(p))
                fl += "regenerates, ";
            if (p->mflags2 & M2_STALK)
                fl += "follows you between levels, ";
            if (perceives(p))
                fl += "sees invisible, ";
            if (!fl.empty())
                deep.push_back("Traits: " + fl.substr(0, fl.size() - 2)),
                    dcol.push_back(SDL_MapRGB(vulture_screen->format, 200, 200, 210));
            for (i = 0; i < cl.size(); i++)
                deep.push_back((i ? "   " : "Corpse: ") + cl[i]),
                    dcol.push_back(dg >= 2 ? SDL_MapRGB(vulture_screen->format, 255, 90, 70)
                                   : dg == 1 ? SDL_MapRGB(vulture_screen->format, 240, 200, 100)
                                             : SDL_MapRGB(vulture_screen->format, 140, 220, 150));
        } else {
            deep.push_back(vspeed_words(m)); /* s144 */
            dcol.push_back(vmon_ratio(m) > 1.05 ? SDL_MapRGB(vulture_screen->format, 255, 170, 90)
                                                : SDL_MapRGB(vulture_screen->format, 190, 200, 210));
            if (dg >= 2) /* the one thing you must not miss */
                deep.push_back("Corpse: " + cl[0]),
                    dcol.push_back(SDL_MapRGB(vulture_screen->format, 255, 90, 70));
            deep.push_back("Hold Shift for full details");
            dcol.push_back(SDL_MapRGB(vulture_screen->format, 130, 130, 140));
        }
    }
    bw = vulture_text_length(V_FONT_SMALL, title);
    if (vulture_text_length(V_FONT_SMALL, tags) > bw)
        bw = vulture_text_length(V_FONT_SMALL, tags);
    for (size_t di = 0; di < deep.size(); di++) /* s141 */
        if (vulture_text_length(V_FONT_SMALL, deep[di]) > bw)
            bw = vulture_text_length(V_FONT_SMALL, deep[di]);
    if (!odds2.empty() && vulture_text_length(V_FONT_SMALL, odds2) > bw)
        bw = vulture_text_length(V_FONT_SMALL, odds2);
    if (!odds1.empty() && vulture_text_length(V_FONT_SMALL, odds1) > bw)
        bw = vulture_text_length(V_FONT_SMALL, odds1);
    if (!odds3.empty() && vulture_text_length(V_FONT_SMALL, odds3) > bw) /* s137 */
        bw = vulture_text_length(V_FONT_SMALL, odds3);
    bw += 26;
    if (bw < 150)
        bw = 150;
    bh = 2 * lh + 14 + (bar ? 10 : 0) + (odds1.empty() ? 0 : 2 * lh + 4)
         + (odds3.empty() ? 0 : lh) /* s137 */
         + (int) deep.size() * lh + (deep.empty() ? 0 : 4); /* s141 */
    bx = mp.x + 20, by = mp.y + 24;
    if (bx + bw > vulture_screen->w - 4)
        bx = mp.x - 20 - bw;
    if (by + bh > vulture_screen->h - 4)
        by = mp.y - 12 - bh;
    vbox(bx, by, bw, bh, fade * 215 / 255);
    rc.x = bx, rc.y = by, rc.w = 3, rc.h = bh;
    SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, tr, tg, tb));
    if (fade < 120)
        return;
    vulture_put_text_shadow(V_FONT_SMALL, title, vulture_screen, bx + 12, by + 6,
                            SDL_MapRGB(vulture_screen->format, 245, 240, 230),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    vulture_put_text_shadow(V_FONT_SMALL, tags, vulture_screen, bx + 12, by + 6 + lh,
                            SDL_MapRGB(vulture_screen->format, tr, tg, tb),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    if (!odds1.empty()) { /* s134: the odds, below the bar */
        int oy = by + 8 + 2 * lh + (bar ? 10 : 0);

        vulture_put_text_shadow(V_FONT_SMALL, odds1, vulture_screen, bx + 12, oy,
                                SDL_MapRGB(vulture_screen->format, 200, 210, 230),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        if (!odds3.empty()) /* s137: will it wake? */
            vulture_put_text_shadow(V_FONT_SMALL, odds3, vulture_screen, bx + 12, oy + 2 * lh,
                                    wcol == 2 ? SDL_MapRGB(vulture_screen->format, 255, 120, 90)
                                    : wcol == 1 ? SDL_MapRGB(vulture_screen->format, 240, 200, 100)
                                    : SDL_MapRGB(vulture_screen->format, 130, 220, 140),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        vulture_put_text_shadow(V_FONT_SMALL, odds2, vulture_screen, bx + 12, oy + lh,
                                zz ? SDL_MapRGB(vulture_screen->format, 150, 200, 255) :
                                risk >= 25 ? SDL_MapRGB(vulture_screen->format, 255, 90, 70)
                                : risk >= 10 ? SDL_MapRGB(vulture_screen->format, 255, 190, 80)
                                : SDL_MapRGB(vulture_screen->format, 200, 210, 230),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    }
    { /* s141: the codex lines at the bottom */
        int dy = by + bh - 6 - (int) deep.size() * lh;
        size_t di;

        for (di = 0; di < deep.size(); di++)
            vulture_put_text_shadow(V_FONT_SMALL, deep[di], vulture_screen, bx + 12,
                                    dy + (int) di * lh, dcol[di],
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    }
    if (bar) {
        int full = bw - 24, fill = m->mhpmax > 0 ? full * m->mhp / m->mhpmax : 0;
        int pct = m->mhpmax > 0 ? 100 * m->mhp / m->mhpmax : 0;

        rc.x = bx + 12, rc.y = by + 10 + 2 * lh, rc.w = full, rc.h = 5;
        SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, 40, 20, 20));
        rc.w = fill < 0 ? 0 : fill;
        SDL_FillRect(vulture_screen, &rc,
                     pct > 60 ? SDL_MapRGB(vulture_screen->format, 90, 210, 90)
                     : pct > 30 ? SDL_MapRGB(vulture_screen->format, 230, 190, 60)
                     : SDL_MapRGB(vulture_screen->format, 220, 60, 50));
    }
}

/* s135: Brogue-style card for things on the floor and dungeon features -
   what it is, and what you can do with it */
static const char *
vsq_hint(int x, int y)
{
    int g, c;
    struct monst *m;

    if (!isok(x, y) || u_at(x, y) || Hallucination || !levl[x][y].seenv)
        return NULL;
    m = m_at(x, y);
    if (m && canspotmon(m))
        return NULL; /* the monster card covers it */
    g = glyph_at(x, y);
    if (glyph_is_object(g)) {
        if (glyph_to_obj(g) == BOULDER)
            return "Walk into it to push it";
        if (glyph_to_obj(g) == CORPSE) { /* s141: what eating it would do */
            static std::string cs;
            struct obj *co = vobj_at(x, y);

            if (co && co->otyp == CORPSE && ismnum(co->corpsenm)) {
                std::vector<std::string> ln;
                int dg;

                vcorpse_info(&mons[co->corpsenm], ln, &dg);
                cs = "If eaten: " + ln[0];
                return cs.c_str();
            }
            return "Click to walk there  -  eat it fresh or not at all";
        }
        { /* s164: how it compares with what you use */
            static std::string fs;
            struct obj *fo = vobj_at(x, y);
            std::string cmp = fo ? vitem_compare(fo) : "";

            if (!cmp.empty()) {
                fs = cmp + "  -  click to pick it up";
                return fs.c_str();
            }
        }
        return "Click to walk there and pick it up";
    }
    if (glyph_is_trap(g)) /* s141: say what it does */
        return vtrap_desc(glyph_to_trap(g));
    if (!glyph_is_cmap(g))
        return NULL;
    c = glyph_to_cmap(g);
    switch (c) {
    case S_dnstair: case S_dnladder: case S_brdnstair: case S_brdnladder:
        return "Press > from anywhere to walk here and go down";
    case S_upstair: case S_upladder: case S_brupstair: case S_brupladder:
        return "Press < from anywhere to walk here and go up";
    case S_fountain:
        return "Drink from it (q) or dip into it (#dip)  -  a gamble";
    case S_altar:
        return "Drop items on it to learn if they are cursed  -  or click yourself to test them all";
    case S_sink:
        return "Drop a ring in to learn what it does; kick it for surprises";
    case S_throne:
        return "Sit on it (#sit)  -  a gamble that can pay off";
    case S_grave:
        return "Dig it up for buried loot  -  and its owner";
    case S_tree:
        return "Kick it: fruit may fall  -  or bees";
    case S_vcdoor: case S_hcdoor:
        return "Click to open it  -  kick it if it is locked";
    default:
        return NULL;
    }
}

static void
vsq_card_draw(const std::string &name)
{
    point mp = vulture_get_mouse_pos();
    Uint32 age = SDL_GetTicks() - vhv_since;
    const char *hint;
    std::string title = name;
    int lh = vulture_get_lineheight(V_FONT_SMALL), bw, bh, bx, by, fade;
    SDL_Rect rc;

    if (vaim_on || vulture_aim_mode == 3 || vulture_whatis_active || !vhv_over_map() || name.empty()
        || age < 160 || !(hint = vsq_hint(vhv.x, vhv.y)))
        return;
    fade = age < 280 ? (int) (255 * (age - 160) / 120.0) : 255;
    title[0] = (char) toupper((unsigned char) title[0]);
    bw = vulture_text_length(V_FONT_SMALL, title);
    if (vulture_text_length(V_FONT_SMALL, hint) > bw)
        bw = vulture_text_length(V_FONT_SMALL, hint);
    bw += 26;
    bh = 2 * lh + 14;
    bx = mp.x + 20, by = mp.y + 24;
    if (bx + bw > vulture_screen->w - 4)
        bx = mp.x - 20 - bw;
    if (by + bh > vulture_screen->h - 4)
        by = mp.y - 12 - bh;
    vbox(bx, by, bw, bh, fade * 215 / 255);
    rc.x = bx, rc.y = by, rc.w = 3, rc.h = bh;
    SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, 210, 180, 110));
    if (fade < 120)
        return;
    vulture_put_text_shadow(V_FONT_SMALL, title, vulture_screen, bx + 12, by + 6,
                            SDL_MapRGB(vulture_screen->format, 245, 240, 230),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    vulture_put_text_shadow(V_FONT_SMALL, hint, vulture_screen, bx + 12, by + 6 + lh,
                            SDL_MapRGB(vulture_screen->format, 170, 190, 215),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
}

/* ground card: what's lying here */
static std::vector<std::string> vhere_lines;
static std::string vhere_title;
static Uint32 vhere_born = 0;
static int vhere_x = -1, vhere_y = -1;

void
vulture_here_card(const std::string &title, const std::vector<std::string> &lines)
{
    vhere_title = title;
    vhere_lines = lines;
    vhere_born = SDL_GetTicks();
    if (!vhere_born)
        vhere_born = 1;
    vhere_x = u.ux, vhere_y = u.uy;
    vulture_sfx("rustle"); /* s131 */
}

static void
vhere_draw(int ax, int ay, int w, int h)
{
    Uint32 now = SDL_GetTicks(), age;
    const Uint32 life = 7000;
    int lh = vulture_get_lineheight(V_FONT_SMALL), n, i, bw, bh, bx, by, fade;
    char more[40];

    if (!vhere_born)
        return;
    age = now - vhere_born;
    if (age >= life || u.ux != vhere_x || u.uy != vhere_y) {
        vhere_born = 0;
        return;
    }
    fade = age > life - 500 ? (int) (255 * (life - age) / 500.0) : 255;
    n = (int) vhere_lines.size() > 8 ? 7 : (int) vhere_lines.size();
    bw = vulture_text_length(V_FONT_SMALL, vhere_title);
    for (i = 0; i < n; i++)
        if (vulture_text_length(V_FONT_SMALL, vhere_lines[i]) > bw)
            bw = vulture_text_length(V_FONT_SMALL, vhere_lines[i]);
    bw += 28;
    if (bw > w / 2)
        bw = w / 2;
    bh = (n + 1 + ((int) vhere_lines.size() > n ? 1 : 0)) * lh + 14;
    bx = ax + 14;
    by = (vhb_rect.w ? vhb_rect.y : stwin && stwin->visible ? stwin->abs_y
                                                            : ay + h - 120) - bh - 12;
    if (by < ay + 60)
        by = ay + 60;
    vbox(bx, by, bw, bh, fade * 210 / 255);
    {
        SDL_Rect rc;

        rc.x = bx, rc.y = by, rc.w = 3, rc.h = bh;
        SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, 255, 200, 70));
    }
    if (fade < 120)
        return;
    vulture_put_text_shadow(V_FONT_SMALL, vhere_title, vulture_screen, bx + 12, by + 6,
                            SDL_MapRGB(vulture_screen->format, 255, 200, 70),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    for (i = 0; i < n; i++)
        vulture_put_text_shadow(V_FONT_SMALL, vhere_lines[i], vulture_screen,
                                bx + 12, by + 6 + (i + 1) * lh,
                                SDL_MapRGB(vulture_screen->format, 235, 232, 225),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    if ((int) vhere_lines.size() > n) {
        snprintf(more, sizeof more, "+%d more  (press , to pick up)",
                 (int) vhere_lines.size() - n);
        vulture_put_text_shadow(V_FONT_SMALL, more, vulture_screen, bx + 12,
                                by + 6 + (n + 1) * lh,
                                SDL_MapRGB(vulture_screen->format, 170, 165, 150),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    }
}

/*
 * IMPLEMENTATION_LOG s126: the ability bar (Dota's item slots, Stoneshard's
 * belt).  Ten slots above the status panel hold items or spells: click a
 * slot or press 1-0 to use it (quaff / read / zap / eat / put on / wear /
 * throw / wield / apply, or cast), right-click to change it, click an empty
 * slot to fill it.  A used-up stack refills from another of the same kind.
 * Saved per character next to the save files.
 */
struct vhb_slot {
    int kind; /* 0 empty, 1 item, 2 spell */
    unsigned oid;
    int otyp;
    int spell;
};
static vhb_slot vhb[10];
static char vhb_owner[PL_NSIZ + 4] = "";
static Uint32 vhb_flash[10];
extern "C" {
extern boolean vulture_click_consumed; /* cmd.c s72 */
}

static std::string
vhb_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char name[PL_NSIZ + 4], buf[700];
    int i;

    for (i = 0; svp.plname[i] && i < PL_NSIZ; i++)
        name[i] = isalnum((unsigned char) svp.plname[i]) ? svp.plname[i] : '_';
    name[i] = 0;
    snprintf(buf, sizeof buf, "%s\\NetHack\\5.0\\hotbar-%s.txt", la ? la : ".", name);
    return buf;
}

static void
vhb_save(void)
{
    FILE *fp = fopen(vhb_path().c_str(), "w");
    int i;

    if (!fp)
        return;
    for (i = 0; i < 10; i++)
        fprintf(fp, "%d %u %d %d\n", vhb[i].kind, vhb[i].oid, vhb[i].otyp, vhb[i].spell);
    fclose(fp);
}

static void
vhb_load(void)
{
    FILE *fp;
    int i;

    if (!svp.plname[0] || !strcmp(vhb_owner, svp.plname))
        return;
    snprintf(vhb_owner, sizeof vhb_owner, "%s", svp.plname);
    memset(vhb, 0, sizeof vhb);
    if ((fp = fopen(vhb_path().c_str(), "r")) != NULL) {
        for (i = 0; i < 10; i++)
            if (fscanf(fp, "%d %u %d %d", &vhb[i].kind, &vhb[i].oid,
                       &vhb[i].otyp, &vhb[i].spell) != 4) {
                vhb[i].kind = 0;
                break;
            }
        fclose(fp);
    }
}

static struct obj *
vhb_obj(int i)
{
    struct obj *o;

    if (vhb[i].kind != 1)
        return 0;
    for (o = gi.invent; o; o = o->nobj)
        if (o->o_id == vhb[i].oid)
            return o;
    for (o = gi.invent; o; o = o->nobj) /* refill from the same kind */
        if (o->otyp == vhb[i].otyp) {
            vhb[i].oid = o->o_id;
            return o;
        }
    return 0;
}

static int
vhb_spell(int i)
{
    int s;

    if (vhb[i].kind != 2)
        return -1;
    if (vhb[i].spell >= 0 && vhb[i].spell < MAXSPELL
        && svs.spl_book[vhb[i].spell].sp_id == vhb[i].otyp)
        return vhb[i].spell;
    for (s = 0; s < MAXSPELL && svs.spl_book[s].sp_id != NO_SPELL; s++)
        if (svs.spl_book[s].sp_id == vhb[i].otyp)
            return vhb[i].spell = s;
    return -1;
}

/* queue the slot's action for the core (picked up by readchar_core as an
   already-handled click) */
static bool
vhb_use(int i)
{
    int (*ec)(void) = 0;

    if (vhb[i].kind == 2) {
        int s = vhb_spell(i);

        if (s < 0)
            return false;
        cmdq_add_ec(CQ_CANNED, docast);
        cmdq_add_key(CQ_CANNED, (char) (s < 26 ? 'a' + s : 'A' + s - 26));
    } else if (vhb[i].kind == 1) {
        struct obj *o = vhb_obj(i);

        if (!o)
            return false;
        switch (o->oclass) {
        case POTION_CLASS:
            ec = dodrink;
            break;
        case SCROLL_CLASS:
        case SPBOOK_CLASS:
            ec = doread;
            break;
        case WAND_CLASS:
            ec = dozap;
            break;
        case FOOD_CLASS:
            ec = doeat;
            break;
        case RING_CLASS:
        case AMULET_CLASS:
            ec = o->owornmask ? doremring : doputon;
            break;
        case ARMOR_CLASS:
            ec = o->owornmask ? dotakeoff : dowear;
            break;
        case WEAPON_CLASS:
            /* missiles and ammo are thrown (ammo is shot when its
               launcher is wielded); anything else is wielded */
            if (is_missile(o) || is_ammo(o) || (o->quan > 1 && o != uwep))
                ec = dothrow;
            else
                ec = dowield;
            break;
        case GEM_CLASS:
            ec = dothrow;
            break;
        default:
            ec = doapply;
            break;
        }
        cmdq_add_ec(CQ_CANNED, ec);
        cmdq_add_key(CQ_CANNED, o->invlet);
    } else {
        return false;
    }
    vulture_click_consumed = TRUE;
    vhb_flash[i] = SDL_GetTicks();
    vulture_sfx("ui_click"); /* s131 */
    return true;
}

static int
vhb_slot_size(void)
{
    return V_LISTITEM_HEIGHT;
}

static int
vhb_hit(int mx, int my)
{
    int S = vhb_slot_size(), gap = V_UI(4), i;

    if (!vhb_rect.w || my < vhb_rect.y || my >= vhb_rect.y + S
        || mx < vhb_rect.x || mx >= vhb_rect.x + vhb_rect.w)
        return -1;
    i = (mx - vhb_rect.x) / (S + gap);
    if ((mx - vhb_rect.x) % (S + gap) >= S || i > 9)
        return -1;
    return i;
}

/* the picker: what goes in this slot? */
static void
vhb_pick(int slot)
{
    contextmenu *menu;
    std::vector<struct obj *> objs;
    struct obj *o;
    int result = 0, i, n = 0;
    char buf[BUFSZ];

    vhb_load();
    menu = new contextmenu(levwin);
    for (o = gi.invent; o && objs.size() < 30; o = o->nobj) {
        switch (o->oclass) {
        case POTION_CLASS: case SCROLL_CLASS: case WAND_CLASS:
        case RING_CLASS: case AMULET_CLASS: case FOOD_CLASS:
        case TOOL_CLASS: case WEAPON_CLASS: case GEM_CLASS:
        case SPBOOK_CLASS: case ARMOR_CLASS:
            break;
        default:
            continue;
        }
        snprintf(buf, sizeof buf, "%c - %s", o->invlet, doname(o));
        if (strlen(buf) > 56)
            strcpy(buf + 53, "...");
        menu->add_item(buf, 100 + (int) objs.size());
        objs.push_back(o);
        n++;
    }
    for (i = 0; i < MAXSPELL && svs.spl_book[i].sp_id != NO_SPELL; i++) {
        snprintf(buf, sizeof buf, "Spell: %s (%d Pw)",
                 OBJ_NAME(objects[svs.spl_book[i].sp_id]),
                 SPELL_LEV_PW(svs.spl_book[i].sp_lev));
        menu->add_item(buf, 1000 + i);
        n++;
    }
    if (vhb[slot].kind) {
        menu->add_item("Clear this slot", 9999);
        n++;
    }
    if (!n) {
        delete menu;
        return;
    }
    menu->layout();
    vulture_sfx("ui_open"); /* s131 */
    vulture_event_dispatcher(&result, V_RESPOND_INT, menu);
    delete menu;
    if (result == 9999) {
        vulture_sfx("ui_clear");
        vhb[slot].kind = 0;
    } else if (result >= 1000 && result < 1000 + MAXSPELL) {
        vhb[slot].kind = 2;
        vhb[slot].spell = result - 1000;
        vhb[slot].otyp = svs.spl_book[result - 1000].sp_id;
        vhb[slot].oid = 0;
    } else if (result >= 100 && result < 100 + (int) objs.size()) {
        vhb[slot].kind = 1;
        vhb[slot].oid = objs[result - 100]->o_id;
        vhb[slot].otyp = objs[result - 100]->otyp;
        vhb[slot].spell = -1;
    } else {
        return;
    }
    if (vhb[slot].kind)
        vulture_sfx("ui_assign"); /* s131 */
    for (i = 0; i < 10; i++) /* one slot per thing */
        if (i != slot && vhb[slot].kind && vhb[i].kind == vhb[slot].kind
            && vhb[i].otyp == vhb[slot].otyp
            && (vhb[i].kind == 2 || vhb[i].oid == vhb[slot].oid))
            vhb[i].kind = 0;
    vhb_save();
}

/* s134: "Add to ability bar" from the inventory - the item's slot if it
   has one, else the first empty slot, else the last */
void
vulture_hotbar_bind(struct obj *o)
{
    int i, slot = -1;

    if (!o)
        return;
    vhb_load();
    for (i = 0; i < 10 && slot < 0; i++)
        if (vhb[i].kind == 1 && vhb[i].oid == o->o_id)
            slot = i;
    for (i = 0; i < 10 && slot < 0; i++)
        if (!vhb[i].kind)
            slot = i;
    if (slot < 0)
        slot = 9;
    vhb[slot].kind = 1, vhb[slot].oid = o->o_id, vhb[slot].otyp = o->otyp, vhb[slot].spell = -1;
    vhb_flash[slot] = SDL_GetTicks();
    vhb_save();
    vulture_sfx("ui_assign");
}

/* the tooltip text for a slot */
static std::string
vhb_tip(int i)
{
    char buf[BUFSZ + 80];
    struct obj *o;
    int s;

    vhb_load();
    if (!vhb[i].kind)
        return "Empty slot - click to put an item or spell here";
    if (vhb[i].kind == 2) {
        s = vhb_spell(i);
        if (s < 0)
            return "Spell no longer known - right-click to change";
        snprintf(buf, sizeof buf, "[%d] Cast %s (%d Pw)  -  right-click to change",
                 (i + 1) % 10, OBJ_NAME(objects[svs.spl_book[s].sp_id]),
                 SPELL_LEV_PW(svs.spl_book[s].sp_lev));
        return buf;
    }
    o = vhb_obj(i);
    if (!o)
        return "None left - right-click to change";
    snprintf(buf, sizeof buf, "[%d] %s  -  right-click to change", (i + 1) % 10, doname(o));
    return buf;
}

static void
vhb_draw(int ax, int ay, int w, int h)
{
    int S = vhb_slot_size(), gap = V_UI(4), total = 10 * S + 9 * gap, i, x0, y0;
    Uint32 now = SDL_GetTicks();
    point mp = vulture_get_mouse_pos();
    int hov = -1;
    Uint32 black = SDL_MapRGB(vulture_screen->format, 0, 0, 0);

    if (vulture_windows_inited != TRUE || !svp.plname[0] || !u.ux)
        return;
    vhb_load();
    if (stwin && stwin->visible) {
        x0 = stwin->abs_x;
        y0 = stwin->abs_y - S - V_UI(8);
    } else {
        x0 = ax + (w - total) / 2;
        y0 = ay + h - S - V_UI(10);
    }
    { /* s134: keep the bar AND its 2x2 buttons on screen */
        int extra = V_UI(8) + 2 * V_UI(96) + V_UI(3);

        if (x0 + total + extra > ax + w - 4)
            x0 = ax + w - 4 - total - extra;
    }
    if (x0 < ax + 4)
        x0 = ax + 4;
    vhb_rect.x = x0, vhb_rect.y = y0, vhb_rect.w = total, vhb_rect.h = S;
    hov = vhb_hit(mp.x, mp.y);
    for (i = 0; i < 10; i++) {
        int sx = x0 + i * (S + gap), tile = -1;
        bool dim = false, flash = vhb_flash[i] && now - vhb_flash[i] < 260;
        struct obj *o = 0;
        SDL_Rect rc;
        Uint32 edge;
        char lab[16];

        vbox(sx, y0, S, S, 185);
        if (vhb[i].kind == 1) {
            o = vhb_obj(i);
            if (o)
                tile = vulture_object_to_tile(o->otyp, -1, -1, o);
            else
                tile = vulture_object_to_tile(vhb[i].otyp, -1, -1, NULL), dim = true;
        } else if (vhb[i].kind == 2) {
            int s = vhb_spell(i);

            tile = vulture_object_to_tile(vhb[i].otyp, -1, -1, NULL);
            if (s < 0 || u.uen < SPELL_LEV_PW(svs.spl_book[s].sp_lev))
                dim = true;
        }
        if (tile >= 0) {
            if (TILE_IS_OBJECT(tile))
                vulture_put_tile_ui(sx + V_UI(2), y0 + V_UI(2),
                                    tile - OBJTILEOFFSET + ICOTILEOFFSET);
            else
                vulture_put_tile_ui(sx + S / 2, y0 + S * 3 / 4, tile);
        }
        if (dim)
            vbox(sx, y0, S, S, 150);
        edge = flash ? SDL_MapRGB(vulture_screen->format, 255, 245, 200)
               : i == hov ? SDL_MapRGB(vulture_screen->format, 255, 200, 70)
               : SDL_MapRGB(vulture_screen->format, 90, 82, 66);
        rc.x = sx, rc.y = y0, rc.w = S, rc.h = 1;
        SDL_FillRect(vulture_screen, &rc, edge);
        rc.y = y0 + S - 1;
        SDL_FillRect(vulture_screen, &rc, edge);
        rc.y = y0, rc.w = 1, rc.h = S;
        SDL_FillRect(vulture_screen, &rc, edge);
        rc.x = sx + S - 1;
        SDL_FillRect(vulture_screen, &rc, edge);
        snprintf(lab, sizeof lab, "%d", (i + 1) % 10);
        vulture_put_text_shadow(V_FONT_SMALL, lab, vulture_screen, sx + V_UI(3), y0 + V_UI(1),
                                SDL_MapRGB(vulture_screen->format, 200, 190, 160), black);
        lab[0] = 0;
        if (o && o->oclass == WAND_CLASS && o->known)
            snprintf(lab, sizeof lab, "%d", o->spe);
        else if (o && o->quan > 1)
            snprintf(lab, sizeof lab, "%ld", (long) o->quan);
        else if (vhb[i].kind == 2 && vhb_spell(i) >= 0)
            snprintf(lab, sizeof lab, "%d", SPELL_LEV_PW(svs.spl_book[vhb_spell(i)].sp_lev));
        if (lab[0]) {
            int tw = vulture_text_length(V_FONT_SMALL, lab);

            vulture_put_text_shadow(V_FONT_SMALL, lab, vulture_screen,
                                    sx + S - tw - V_UI(3),
                                    y0 + S - vulture_get_lineheight(V_FONT_SMALL),
                                    vhb[i].kind == 2 ? SDL_MapRGB(vulture_screen->format, 120, 170, 255)
                                                     : SDL_MapRGB(vulture_screen->format, 245, 240, 230),
                                    black);
        }
    }
}

/* s126 probe: VULTURE_SELFTEST=<dir> stages the modern-UI pieces (bar,
   travel preview, ground card, hover card, aim line) off-screen and saves
   a screenshot of each into <dir>.  Off unless the variable is set. */
static void
vselftest(void)
{
    static int stage = 0;
    static Uint32 t0 = 0;
    const char *dir = getenv("VULTURE_SELFTEST");
    char path[700];
    struct monst *m;
    int x, y, bx = -1, by = -1, bd = 999;

    if (!dir || !*dir || stage > 4 || !levwin || !u.ux)
        return;
    if (!t0)
        t0 = SDL_GetTicks();
    if (SDL_GetTicks() - t0 < 1500 + 700u * stage)
        return;
    if (stage == 0)
        vrt_probe_moves0 = svm.moves; /* probe: s140 */
    switch (stage) {
    case 0: {
        struct obj *o;
        int i = 0;
        std::vector<std::string> lines;

        vhb_load();
        for (o = gi.invent; o && i < 6; o = o->nobj)
            if (o->oclass != COIN_CLASS) {
                vhb[i].kind = 1, vhb[i].oid = o->o_id, vhb[i].otyp = o->otyp;
                i++;
            }
        if (svs.spl_book[0].sp_id != NO_SPELL)
            vhb[i].kind = 2, vhb[i].otyp = svs.spl_book[0].sp_id, vhb[i].spell = 0;
        for (x = 1; x < COLNO; x++)
            for (y = 0; y < ROWNO; y++) {
                int d = distmin(u.ux, u.uy, x, y);

                if (d >= 4 && d < bd && vmv_pass(x, y) && vpath_find(x, y, NULL, 0) >= 0) {
                    static point tmp[COLNO * ROWNO];

                    if (vpath_find(x, y, tmp, COLNO * ROWNO) >= 4)
                        bd = d, bx = x, by = y;
                }
            }
        if (bx >= 0) {
            point sq, mp;

            sq.x = bx, sq.y = by;
            mp = levwin->map_to_mouse(sq);
            vulture_set_mouse_pos(mp.x, mp.y);
            vhv = sq;
            vhv_since = SDL_GetTicks() - 1000;
            if (getenv("VULTURE_SELFTEST_ITEM")) { /* probe: s135 square card */
                (void) mksobj_at(LONG_SWORD, bx, by, TRUE, FALSE);
                newsym(bx, by);
            }
        }
        if (getenv("VULTURE_SELFTEST_LIGHT")) { /* probe: s143 feature/monster glows */
            coord cc;

            if (getenv("VULTURE_SELFTEST_FLAT")) /* probe: s145 relief off */
                vl_relief_set(false);

            for (x = 1; x < COLNO; x++)
                for (y = 0; y < ROWNO; y++)
                    levl[x][y].lit = 0;
            if (isok(u.ux - 2, u.uy) && ACCESSIBLE(levl[u.ux - 2][u.uy].typ)) {
                levl[u.ux - 2][u.uy].typ = ALTAR;
                levl[u.ux - 2][u.uy].altarmask = AM_CHAOTIC;
            }
            if (isok(u.ux + 2, u.uy) && ACCESSIBLE(levl[u.ux + 2][u.uy].typ))
                levl[u.ux + 2][u.uy].typ = FOUNTAIN;
            if (enexto(&cc, u.ux, u.uy + 2, &mons[PM_FIRE_VORTEX]))
                (void) makemon(&mons[PM_FIRE_VORTEX], cc.x, cc.y, NO_MM_FLAGS);
            { /* probe: s149 magic that pulses */
                static const int tts[2] = { MAGIC_TRAP, TELEP_TRAP };
                int q;

                for (q = 0; q < 2; q++) {
                    coord tc;
                    struct trap *tr;

                    if (enexto(&tc, u.ux + (q ? 1 : -1), u.uy - 1, (struct permonst *) 0)
                        && !t_at(tc.x, tc.y) && (tr = maketrap(tc.x, tc.y, tts[q])) != 0) {
                        tr->tseen = 1;
                        newsym(tc.x, tc.y);
                    }
                }
            }
            gv.vision_full_recalc = 1;
            vision_recalc(0);
            for (x = u.ux - 3; x <= u.ux + 3; x++)
                for (y = u.uy - 3; y <= u.uy + 3; y++)
                    if (isok(x, y))
                        newsym(x, y);
        }
        if (getenv("VULTURE_SELFTEST_DARK")) { /* probe: unlit level + a flash */
            for (x = 1; x < COLNO; x++)
                for (y = 0; y < ROWNO; y++)
                    levl[x][y].lit = 0;
            gv.vision_full_recalc = 1;
            vision_recalc(0);
            vl_flash(u.ux - 3, u.uy, 1.0f, 0.6f, 0.3f, 0.9f, 2.5f, 60000);
        }
        if (getenv("VULTURE_SELFTEST_PORTAL")) { /* probe: s166 a magic trap you can see */
            int dd;
            static const int ddx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, ddy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

            for (dd = 0; dd < 8; dd++) {
                int tx2 = u.ux + ddx[dd], ty2 = u.uy + ddy[dd];
                struct trap *tr;

                if (isok(tx2, ty2) && levl[tx2][ty2].typ == ROOM && !t_at(tx2, ty2)
                    && !svl.level.objects[tx2][ty2] && !m_at(tx2, ty2)
                    && (tr = maketrap(tx2, ty2, MAGIC_TRAP)) != 0) {
                    FILE *pf;
                    char pp[700];

                    tr->tseen = 1;
                    newsym(tx2, ty2);
                    snprintf(pp, sizeof pp, "%s\\portal.txt", dir);
                    if ((pf = fopen(pp, "w")) != 0) {
                        fprintf(pf, "trap at %d,%d hero %d,%d\n", tx2, ty2, u.ux, u.uy);
                        fclose(pf);
                    }
                    break;
                }
            }
        }
        if (getenv("VULTURE_SELFTEST_TELE")) { /* probe: s163 a dragon lined up with you */
            int dd;
            static const int ddx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, ddy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

            for (dd = 0; dd < 8; dd++) {
                int tx2 = u.ux + 3 * ddx[dd], ty2 = u.uy + 3 * ddy[dd];
                struct monst *dm;

                if (isok(tx2, ty2) && ACCESSIBLE(levl[tx2][ty2].typ) && !m_at(tx2, ty2)
                    && clear_path(u.ux, u.uy, tx2, ty2)
                    && (dm = makemon(&mons[PM_RED_DRAGON], tx2, ty2, NO_MM_FLAGS)) != 0) {
                    dm->mpeaceful = 0, dm->mspec_used = 0, dm->msleeping = 0;
                    break;
                }
            }
        }
        if (getenv("VULTURE_SELFTEST") && !getenv("VULTURE_SELFTEST_NOJACKAL")) { /* probe: a hostile to hover (odds) */
            coord cc;

            int pmj = getenv("VULTURE_SELFTEST_SPEED") ? PM_SOLDIER_ANT : PM_JACKAL; /* probe: s144 fast */

            if (enexto(&cc, u.ux, u.uy, &mons[pmj])) {
                struct monst *jm = makemon(&mons[pmj], cc.x, cc.y, NO_MM_FLAGS);

                if (jm && getenv("VULTURE_SELFTEST_SPEED")) { /* a second, slow one for Alt */
                    coord c2;

                    if (enexto(&c2, u.ux - 3, u.uy, &mons[PM_LICHEN]))
                        (void) makemon(&mons[PM_LICHEN], c2.x, c2.y, NO_MM_FLAGS);
                }

                if (jm && getenv("VULTURE_SELFTEST_SLEEP")) /* probe: s137 sleeper */
                    jm->msleeping = 1;
            }
        }
        if (getenv("VULTURE_SELFTEST_CODEX")) { /* probe: s141 corpse + attack text */
            char fp[700];
            FILE *f;
            static const int pms[] = { PM_COCKATRICE, PM_FLOATING_EYE, PM_JACKAL, PM_KITTEN,
                                       PM_NEWT, PM_LIZARD, PM_SOLDIER_ANT, PM_WATER_NYMPH,
                                       PM_GREEN_SLIME, PM_MIND_FLAYER };
            size_t q;

            snprintf(fp, sizeof fp, "%s\\codex.txt", dir);
            if ((f = fopen(fp, "w")) != 0) {
                for (q = 0; q < sizeof pms / sizeof pms[0]; q++) {
                    std::vector<std::string> ln;
                    int dg, k2;

                    vcorpse_info(&mons[pms[q]], ln, &dg);
                    fprintf(f, "== %s (danger %d)\n  attacks: %s\n  resists: %s\n",
                            mons[pms[q]].pmnames[NEUTRAL] ? mons[pms[q]].pmnames[NEUTRAL] : "?",
                            dg, vmon_attacks(&mons[pms[q]]).c_str(),
                            vmon_resists(mons[pms[q]].mresists).c_str());
                    for (k2 = 0; k2 < (int) ln.size(); k2++)
                        fprintf(f, "  corpse: %s\n", ln[k2].c_str());
                }
                fclose(f);
            }
            SDL_SetModState(KMOD_LSHIFT);
        }
        if (getenv("VULTURE_SELFTEST_ITEMS")) { /* probe: s139 cards + altar test */
            char fp[700];
            FILE *f;
            struct obj *io;

            { /* probe: s164 things to compare */
                static const int extra[3] = { LEATHER_ARMOR, ELVEN_CLOAK, DAGGER };
                int q;

                for (q = 0; q < 3; q++) {
                    struct obj *xo = mksobj(extra[q], FALSE, FALSE);

                    xo->known = xo->dknown = 1, xo->cursed = 0;
                    (void) addinv(xo);
                }
            }
            snprintf(fp, sizeof fp, "%s\\items.txt", dir);
            if ((f = fopen(fp, "w")) != 0) {
                for (io = gi.invent; io; io = io->nobj)
                    fprintf(f, "== %s\n%s\n", doname(io), vulture_item_info(io).c_str());
                fclose(f);
            }
            levl[u.ux][u.uy].typ = ALTAR;
            levl[u.ux][u.uy].altarmask = AM_CHAOTIC;
            newsym(u.ux, u.uy);
            {
                int nn = 0;

                for (io = gi.invent; io && nn < 3; io = io->nobj)
                    if (!io->owornmask)
                        io->bknown = 0, nn++;
            }
            (void) vulture_altar_test();
            snprintf(fp, sizeof fp, "%s\\altar.txt", dir);
            if ((f = fopen(fp, "w")) != 0) {
                for (io = svl.level.objects[u.ux][u.uy]; io; io = io->nexthere)
                    fprintf(f, "floor: %s bknown=%d nomerge=%d\n", doname(io), io->bknown,
                            io->nomerge);
                fprintf(f, "queued: %d\n", cmdq_peek(CQ_CANNED) ? 1 : 0);
                fclose(f);
            }
        }
        if (getenv("VULTURE_SELFTEST_GEAR"))
            vgs_probe(0, dir);
        lines.push_back("a +0 dagger");
        lines.push_back("2 uncursed food rations");
        lines.push_back("a scroll labeled ELBIB YLOH");
        vulture_here_card("Things that are here", lines);
        break;
    }
    case 1:
        if (getenv("VULTURE_SELFTEST_ITEM")) { /* probe: s136 pulse + glint */
            coord cc;
            vulture_search_fx();
            vfx_list.back().t0 -= 380;
            if (enexto(&cc, u.ux + 2, u.uy, (struct permonst *) 0)) {
                vfx_ent e;
                (void) mksobj_at(POT_WATER, cc.x, cc.y, TRUE, FALSE);
                newsym(cc.x, cc.y);
                memset(&e, 0, sizeof e);
                e.kind = 7, e.x = cc.x, e.y = cc.y, e.t0 = SDL_GetTicks() - 320, e.dur = 650;
                e.r = 255, e.g = 245, e.b = 200;
                vfx_list.push_back(e);
            }
        }
        if (getenv("VULTURE_SELFTEST_SPEED")) /* probe: s144 hourglass */
            vsr_t = SDL_GetTicks() - 300, vsr_kind = 2;
        if (!getenv("VULTURE_SELFTEST_NOCENTER")) /* probe: undo any edge-scroll */
            levwin->set_view(u.ux, u.uy);
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(path, sizeof path, "%s\\selftest_hover.bmp", dir);
        SDL_SaveBMP(vulture_screen, path);
        if (getenv("VULTURE_SELFTEST_LIGHT") || getenv("VULTURE_SELFTEST_CMP")) { /* probe: s145 relief cost */
            char fp2[700];
            FILE *f2;

            snprintf(fp2, sizeof fp2, "%s/relief.txt", dir);
            if ((f2 = fopen(fp2, "w")) != 0) {
                vl_relief_report(f2);
                {
                    struct trap *tq;

                    for (tq = gf.ftrap; tq; tq = tq->ntrap)
                        fprintf(f2, "trap %d at %d,%d seen=%d cansee=%d glyph_is_trap=%d (hero %d,%d)\n",
                                tq->ttyp, tq->tx, tq->ty, tq->tseen, cansee(tq->tx, tq->ty),
                                glyph_is_trap(glyph_at(tq->tx, tq->ty)), u.ux, u.uy);
                }
                fclose(f2);
            }
            vl_relief_set(false); /* the same frame, flat, to compare */
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(fp2, sizeof fp2, "%s\\selftest_flat.bmp", dir);
            SDL_SaveBMP(vulture_screen, fp2);
            vl_relief_set(true);
        }
        if (getenv("VULTURE_SELFTEST_MARK")) { /* probe: s148 markers on wall / boulder / floor */
            char fp3[700];
            FILE *f3;
            int wx = -1, wy = -1, fx = -1, fy = -1, dx2, dy2;
            coord bc;

            for (dx2 = -3; dx2 <= 3; dx2++)
                for (dy2 = -3; dy2 <= 3; dy2++) {
                    int xx = u.ux + dx2, yy = u.uy + dy2;

                    if (!isok(xx, yy) || u_at(xx, yy) || !levl[xx][yy].seenv)
                        continue;
                    if (wx < 0 && IS_WALL(levl[xx][yy].typ))
                        wx = xx, wy = yy;
                    if (fx < 0 && levl[xx][yy].typ == ROOM && !m_at(xx, yy)
                        && !svl.level.objects[xx][yy] && distmin(u.ux, u.uy, xx, yy) == 2)
                        fx = xx, fy = yy;
                }
            if (enexto(&bc, u.ux + 1, u.uy - 1, (struct permonst *) 0)) {
                (void) mksobj_at(BOULDER, bc.x, bc.y, FALSE, FALSE);
                newsym(bc.x, bc.y);
            }
            vfx_click_at(wx, wy);
            vfx_click_at(bc.x, bc.y);
            vfx_click_at(fx, fy);
            snprintf(fp3, sizeof fp3, "%s\\mark.txt", dir);
            if ((f3 = fopen(fp3, "w")) != 0) {
                size_t q;

                for (q = 0; q < vfx_list.size(); q++)
                    if (vfx_list[q].kind == 3)
                        fprintf(f3, "marker at %d,%d sub=%d rgb=%d,%d,%d\n", vfx_list[q].x,
                                vfx_list[q].y, vfx_list[q].sub, vfx_list[q].r, vfx_list[q].g,
                                vfx_list[q].b);
                fprintf(f3, "wall=%d,%d boulder=%d,%d floor=%d,%d\n", wx, wy, bc.x, bc.y, fx, fy);
                fclose(f3);
            }
            for (size_t q = 0; q < vfx_list.size(); q++)
                if (vfx_list[q].kind == 3)
                    vfx_list[q].t0 = SDL_GetTicks() - 120; /* mid-pop for the shot */
            levwin->set_view(u.ux, u.uy);
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(fp3, sizeof fp3, "%s\\selftest_mark.bmp", dir);
            SDL_SaveBMP(vulture_screen, fp3);
        }
        if (getenv("VULTURE_SELFTEST_HEAVY")) { /* probe: s151 a gigantic death + drops */
            coord hc;
            struct monst *bm;
            struct obj *ch, *dg;

            if (enexto(&hc, u.ux + 1, u.uy, &mons[PM_MASTODON])
                && (bm = makemon(&mons[PM_MASTODON], hc.x, hc.y, NO_MM_FLAGS)) != 0) {
                bm->mhp = 1;
                bm->mcanmove = 0, bm->mfrozen = 40;
                vprobe_push_key(SDLK_TAB);
                vprobe_push_key(SDLK_TAB);
            }
            if (enexto(&hc, u.ux - 1, u.uy, (struct permonst *) 0)) {
                ch = mksobj_at(CHEST, hc.x, hc.y, FALSE, FALSE);
                dg = mksobj_at(DAGGER, hc.x, hc.y + 1, FALSE, FALSE);
                newsym(hc.x, hc.y), newsym(hc.x, hc.y + 1);
                vulture_anim_drop(hc.x, hc.y);
                vulture_anim_drop(hc.x, hc.y + 1);
                nhUse(ch), nhUse(dg);
            }
        }
        if (getenv("VULTURE_SELFTEST_READ")) { /* probe: s150 ambiguous reads get identified */
            static const int scr[2] = { SCR_TAMING, SCR_FOOD_DETECTION };
            int q;
            struct monst *pm2;

            for (pm2 = fmon; pm2; pm2 = pm2->nmon) /* nobody to tame */
                if (!DEADMONSTER(pm2) && distmin(u.ux, u.uy, pm2->mx, pm2->my) <= 2)
                    mongone(pm2);
            for (q = 0; q < 2; q++) {
                struct obj *so = mksobj(scr[q], FALSE, FALSE);

                so->cursed = so->blessed = 0;
                so = addinv(so);
                vprobe_push_key(SDLK_r);
                vprobe_push_key((SDL_Keycode) so->invlet);
            }
            {
                static const int pot[2] = { POT_WATER, POT_SEE_INVISIBLE };

                for (q = 0; q < 2; q++) {
                    struct obj *po = mksobj(pot[q], FALSE, FALSE);

                    po->cursed = po->blessed = 0;
                    po = addinv(po);
                    vprobe_push_key(SDLK_q);
                    vprobe_push_key((SDL_Keycode) po->invlet);
                }
            }
        }
        if (getenv("VULTURE_SELFTEST_LEVEL")) /* probe: s155 the real level-up path */
            pluslvl(FALSE);
        if (getenv("VULTURE_SELFTEST_DIE")) { /* probe: s165 the run summary on death */
            extern long vulture_dmg_dealt;

            vulture_dmg_dealt += 37;
            vulture_dmg_taken_add(12);
            u.uhp = 0;
            svk.killer.format = KILLED_BY_AN;
            Strcpy(svk.killer.name, "probe gremlin");
            vpause_probe_t = SDL_GetTicks(); /* shoots the window that's up */
            done(DIED);
        }
        if (getenv("VULTURE_SELFTEST_AIM")) /* probe: s156 aimed blows, stage by stage */
            vaim_probe(1, dir);
        if (getenv("VULTURE_SELFTEST_BOULDER")) { /* probe: s157 push a boulder twice */
            int bdx, dd, placed = 0;
            static const int ddx[4] = { 1, -1, 0, 0 }, ddy[4] = { 0, 0, 1, -1 };

            /* keys follow the view rotation: a boulder on every side with
               room behind it, so the push works whichever way 'l' goes */
            for (dd = 0; dd < 4; dd++) {
                for (bdx = 1; bdx <= 3; bdx++) {
                    int xx = u.ux + ddx[dd] * bdx, yy = u.uy + ddy[dd] * bdx;

                    if (!isok(xx, yy) || !ACCESSIBLE(levl[xx][yy].typ) || m_at(xx, yy))
                        break;
                }
                if (bdx > 3) {
                    (void) mksobj_at(BOULDER, u.ux + ddx[dd], u.uy + ddy[dd], FALSE, FALSE);
                    newsym(u.ux + ddx[dd], u.uy + ddy[dd]);
                    placed++;
                }
            }
            if (placed) {
                vprobe_push_key(SDLK_l);
                vprobe_push_key(SDLK_l);
            } else {
                FILE *bf;
                char bp[700];

                snprintf(bp, sizeof bp, "%s\\impacts.txt", dir);
                if ((bf = fopen(bp, "a")) != 0) {
                    fprintf(bf, "no room east to push a boulder\n");
                    fclose(bf);
                }
            }
        }
        if (getenv("VULTURE_SELFTEST_WALK")) { /* probe: s147 a held key, then single steps */
            static const SDL_Keycode ks[] = { SDLK_h, SDLK_l, SDLK_j, SDLK_k };
            int q;

            for (q = 0; q < 6; q++)
                vprobe_push_key(ks[q & 3]);
        }
        if (getenv("VULTURE_SELFTEST_CRIT")) { /* probe: s146 Tab at the sleeper */
            struct monst *cm;

            for (cm = fmon; cm; cm = cm->nmon)
                if (!DEADMONSTER(cm) && !cm->mtame) {
                    cm->mhpmax = cm->mhp = 60; /* survive the blow, to see it */
                    if (getenv("VULTURE_SELFTEST_SLEEP")) /* the kitten wakes sleepers */
                        cm->mcanmove = 0, cm->mfrozen = 40;
                    if (!getenv("VULTURE_SELFTEST_SLEEP")) {
                        cm->msleeping = 0;
                        cm->mux = 0, cm->muy = 0; /* lost track of you */
                        HInvis |= FROMOUTSIDE;    /* and can't see you */
                    }
                }
            vprobe_push_key(SDLK_TAB);
            vprobe_push_key(SDLK_TAB);
            vprobe_push_key(SDLK_TAB);
        }
        if (getenv("VULTURE_SELFTEST_CLICK") && isok(vhv.x, vhv.y)) { /* probe: s145 travel */
            vck_from.x = u.ux, vck_from.y = u.uy;
            vck_to = vhv;
            vprobe_push_click(vck_to);
            break;
        }
        for (m = fmon; m; m = m->nmon) /* a hostile if there is one (odds) */
            if (!DEADMONSTER(m) && canspotmon(m) && !m->mtame)
                break;
        if (!m)
            m = fmon;
        for (; m; m = m->nmon)
            if (!DEADMONSTER(m) && canspotmon(m)) {
                point sq, mp;

                sq.x = m->mx, sq.y = m->my;
                mp = levwin->map_to_mouse(sq);
                vulture_set_mouse_pos(mp.x, mp.y);
                vhv = sq;
                vhv_since = SDL_GetTicks() - 1000;
                break;
            }
        break;
    case 2:
        if (getenv("VULTURE_SELFTEST_AIM")) /* probe: s156 */
            vaim_probe(2, dir);
        if (getenv("VULTURE_SELFTEST_CLICK") && isok(vck_from.x, vck_from.y)) { /* probe: s145 */
            char fp[700];
            FILE *f;
            point mp = levwin->map_to_mouse(vck_from);

            vulture_set_mouse_pos(mp.x, mp.y);
            vhv = vck_from;
            vhv_since = SDL_GetTicks() - 1000;
            snprintf(fp, sizeof fp, "%s\\click.txt", dir);
            if ((f = fopen(fp, "w")) != 0) {
                fprintf(f, "from=%d,%d to=%d,%d now=%d,%d travel=%d run=%d multi=%d\n",
                        vck_from.x, vck_from.y, vck_to.x, vck_to.y, u.ux, u.uy,
                        svc.context.travel, svc.context.run, gm.multi);
                fclose(f);
            }
        }
        if (getenv("VULTURE_SELFTEST_SLEEP")) { /* probe: s137 ripple + payoff */
            vulture_noise(u.ux, u.uy, 25);
            vfx_list.back().t0 -= 260;
            vulture_silent_kill(u.ux - 2, u.uy + 1);
            for (size_t q = 0; q < vfx_list.size(); q++)
                if (vfx_list[q].kind == 1 || vfx_list[q].kind == 6)
                    vfx_list[q].t0 -= 200;
            vhitstop_ms = 0;
        }
        if (!getenv("VULTURE_SELFTEST_NOCENTER")) /* probe: undo any edge-scroll */
            levwin->set_view(u.ux, u.uy);
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(path, sizeof path, "%s\\selftest_card.bmp", dir);
        SDL_SaveBMP(vulture_screen, path);
        vulture_aim_set(true, u.ux - 6, u.uy - 1);
        if (getenv("VULTURE_SELFTEST_AOE")) { /* probe: s138 fire ray at a hostile */
            for (m = fmon; m; m = m->nmon)
                if (!DEADMONSTER(m) && canspotmon(m) && !m->mtame)
                    break;
            vulture_aim_mode = 1, vulture_aim_dmg = 1;
            if (m)
                vulture_aim_set(true, u.ux + 6 * sgn(m->mx - u.ux), u.uy + 6 * sgn(m->my - u.uy));
        }
        break;
    case 3:
        if (getenv("VULTURE_SELFTEST_AIM")) /* probe: s156 */
            vaim_probe(3, dir);
        if (!getenv("VULTURE_SELFTEST_NOCENTER")) /* probe: undo any edge-scroll */
            levwin->set_view(u.ux, u.uy);
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(path, sizeof path, "%s\\selftest_aim.bmp", dir);
        SDL_SaveBMP(vulture_screen, path);
        vulture_aim_set(false, -1, -1);
        if (getenv("VULTURE_SELFTEST_CLICK")) /* probe: s145 pause menu */
            vprobe_push_key(SDLK_ESCAPE);
        if (getenv("VULTURE_SELFTEST_SPEED")) { /* probe: s144 Alt danger map */
            SDL_SetModState(KMOD_LALT);
            vhv.x = -1;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(path, sizeof path, "%s\\selftest_alt.bmp", dir);
            SDL_SaveBMP(vulture_screen, path);
            SDL_SetModState(KMOD_NONE);
        }
        if (getenv("VULTURE_SELFTEST_AOE")) { /* probe: s138 skilled fireball spot */
            point sq, mp;

            vulture_aim_mode = 3, vulture_aim_dmg = 1;
            for (m = fmon; m; m = m->nmon)
                if (!DEADMONSTER(m) && canspotmon(m) && !m->mtame)
                    break;
            sq.x = m ? m->mx : u.ux + 2, sq.y = m ? m->my : u.uy;
            mp = levwin->map_to_mouse(sq);
            vulture_set_mouse_pos(mp.x, mp.y);
            vhv = sq;
        } else {
            vulture_aim_mode = 0;
        }
        break;
    case 4:
        if (getenv("VULTURE_SELFTEST_AIM")) /* probe: s156 */
            vaim_probe(4, dir);
        if (getenv("VULTURE_SELFTEST_READ")) { /* probe: s150 what got identified */
            char fp4[700];
            FILE *f4;
            static const int ty[4] = { SCR_TAMING, SCR_FOOD_DETECTION, POT_WATER, POT_SEE_INVISIBLE };
            int q;

            snprintf(fp4, sizeof fp4, "%s\\read.txt", dir);
            if ((f4 = fopen(fp4, "w")) != 0) {
                for (q = 0; q < 4; q++)
                    fprintf(f4, "%s known=%d\n", OBJ_NAME(objects[ty[q]]),
                            objects[ty[q]].oc_name_known);
                fclose(f4);
            }
        }
        if (getenv("VULTURE_SELFTEST_GEAR"))
            vgs_probe(4, dir);
        if (getenv("VULTURE_SELFTEST_RT")) { /* probe: s140 turns passing */
            char fp[700];
            FILE *f;

            snprintf(fp, sizeof fp, "%s\\rt.txt", dir);
            if ((f = fopen(fp, "w")) != 0) {
                fprintf(f, "mode=%d moves_start=%ld moves_now=%ld hp=%d\n", vrt_get(),
                        vrt_probe_moves0, svm.moves, u.uhp);
                vrt_probe_dump(f);
                fclose(f);
            }
        }
        if (vulture_aim_mode == 3 || getenv("VULTURE_SELFTEST_CLICK")) {
            if (!getenv("VULTURE_SELFTEST_NOCENTER"))
                levwin->set_view(u.ux, u.uy);
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(path, sizeof path, "%s\\selftest_blast.bmp", dir);
            SDL_SaveBMP(vulture_screen, path);
        }
        vulture_aim_mode = 0;
        break;
    }
    stage++;
}

/*
 * IMPLEMENTATION_LOG s129-s130: polish + modern controls.
 *  - Status pulse: becoming confused / stunned / blind / hallucinating /
 *    sick / stoned / slimed / strangled / weak from hunger flashes the
 *    screen edge in that status's colour, with a label rising off the hero.
 *  - Auto-fight (Tab), attack-move (Shift+A then click), rest until
 *    healed (F5), each also a button beside the ability bar.  They run one
 *    turn at a time from the input loop (vulture_auto_step), so any key or
 *    click stops them.
 *  - "In sight" panel: every monster you can see, threat-coloured, with a
 *    health bar once hurt; hover to mark it on the map, click to look.
 */
static SDL_Surface *vtint_px = NULL;

/* a translucent coloured rectangle */
static void
vtint(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, int a)
{
    SDL_Rect d;

    if (w <= 0 || h <= 0 || a <= 0)
        return;
    if (!vtint_px) {
        vtint_px = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!vtint_px)
            return;
        SDL_FillRect(vtint_px, NULL, 0xFFFFFFFF);
        SDL_SetSurfaceBlendMode(vtint_px, SDL_BLENDMODE_BLEND);
    }
    SDL_SetSurfaceColorMod(vtint_px, r, g, b);
    SDL_SetSurfaceAlphaMod(vtint_px, (Uint8) (a > 255 ? 255 : a));
    d.x = x, d.y = y, d.w = w, d.h = h;
    SDL_BlitScaled(vtint_px, NULL, vulture_screen, &d);
}

static Uint32 vst_pulse_t = 0;
static Uint8 vst_r, vst_g, vst_b;

/* ---- s137: stealth and sleep, communicated ---- */
static std::map<unsigned, bool> vheard; /* woken by a noise this frame: "?" */

static void
vfx_word(int x, int y, const char *txt, Uint8 r, Uint8 g, Uint8 b, Uint32 dur)
{
    vfx_ent e;

    if (!vfx_words) /* s161 */
        return;
    memset(&e, 0, sizeof e);
    e.kind = 1, e.x = x, e.y = y, e.t0 = SDL_GetTicks(), e.dur = dur;
    snprintf(e.txt, sizeof e.txt, "%s", txt);
    e.r = r, e.g = g, e.b = b;
    { /* s146: stack above words still rising from the same square */
        size_t q;

        for (q = 0; q < vfx_list.size(); q++)
            if (vfx_list[q].kind == 1 && vfx_list[q].x == x && vfx_list[q].y == y
                && e.t0 - vfx_list[q].t0 < 700 && vfx_list[q].sub >= e.sub)
                e.sub = vfx_list[q].sub + 1;
        if (e.sub > 3)
            e.sub = 3;
    }
    vfx_list.push_back(e);
}

/* s156: Ctrl+click an adjacent enemy - pick a body part (Fallout's called
   shots): each line shows your chance and what a hit there does */
static bool vauto_step_toward(int tx, int ty);
static void vauto_result(void *result);

static bool
vaim_pick(struct monst *m, void *result)
{
    static const struct {
        int part;
        const char *name, *what;
    } parts[6] = { { VAP_TORSO, "Torso", "a normal blow" },
                   { VAP_LEGS, "Legs", "slows it" },
                   { VAP_ARMS, "Arms", "makes it drop its weapon" },
                   { VAP_GROIN, "Groin", "x1.2 damage, may stun" },
                   { VAP_HEAD, "Head", "x1.5 damage, may stun" },
                   { VAP_EYES, "Eyes", "blinds it" } };
    contextmenu *menu;
    int pick = 0, k, yh, ih, ip;
    bool sure = m->msleeping || !m->mcanmove;
    char buf[160];

    vodds(m, &yh, &ih, &ip);
    menu = new contextmenu(levwin);
    for (k = 0; k < 6; k++) {
        int pc;

        if (!vulture_aim_has(m, parts[k].part))
            continue;
        pc = sure ? 100 : yh - 5 * vulture_aim_penalty(parts[k].part);
        pc = pc < 5 ? 5 : pc > 95 && !sure ? 95 : pc;
        if (parts[k].part == VAP_ARMS && !MON_WEP(m))
            snprintf(buf, sizeof buf, "%-6s %3d%%   x1.2 damage (it holds no weapon)", parts[k].name, pc);
        else
            snprintf(buf, sizeof buf, "%-6s %3d%%   %s", parts[k].name, pc, parts[k].what);
        menu->add_item(buf, parts[k].part);
    }
    menu->layout();
    vulture_sfx("aim_ready");
    vulture_event_dispatcher(&pick, V_RESPOND_INT, menu);
    delete menu;
    if (pick < VAP_TORSO || pick > VAP_GROIN) {
        vulture_sfx("ui_cancel");
        return false;
    }
    vulture_aim_part = pick, vulture_aim_mid = m->m_id, vulture_aim_moves = svm.moves;
    if (!vauto_step_toward(m->mx, m->my)) {
        vulture_aim_part = 0;
        return false;
    }
    vauto_result(result);
    return true;
}

/* probe: s156 - an armed, paralysed hill orc beside you; aim at its arms,
   then eyes, then legs (a real Tab each time); report, then show the menu */
static void
vaim_probe(int stage, const char *dir)
{
    static unsigned oid = 0;
    struct monst *m = 0, *q;
    static const int seq[4] = { 0, VAP_ARMS, VAP_EYES, VAP_LEGS };

    for (q = fmon; q; q = q->nmon)
        if (!DEADMONSTER(q) && q->m_id == oid)
            m = q;
    if (stage == 1) {
        coord cc;

        if (enexto(&cc, u.ux + 1, u.uy, &mons[PM_HILL_ORC])
            && (m = makemon(&mons[PM_HILL_ORC], cc.x, cc.y, NO_MM_FLAGS)) != 0) {
            if (!MON_WEP(m)) {
                struct obj *w = mksobj(ORCISH_SHORT_SWORD, FALSE, FALSE);

                w->cursed = 0;
                (void) mpickobj(m, w);
                m->weapon_check = NEED_HTH_WEAPON;
                (void) mon_wield_item(m);
            }
            m->mhpmax = m->mhp = 200;
            m->mpeaceful = 0; /* the probe hero is an orc too */
            m->mcanmove = 0, m->mfrozen = 60; /* every blow lands */
            oid = m->m_id;
        }
    }
    if (stage >= 1 && stage <= 3 && m) {
        vulture_aim_part = seq[stage], vulture_aim_mid = m->m_id, vulture_aim_moves = svm.moves;
        vprobe_push_key(SDLK_TAB);
    }
    if (stage == 4) {
        char fp[700];
        FILE *f;

        snprintf(fp, sizeof fp, "%s\\aim.txt", dir);
        if ((f = fopen(fp, "w")) != 0) {
            if (m)
                fprintf(f, "orc hp %d/%d weapon=%s blinded=%d cansee=%d speed=%d(slow=%d) aim_left=%d\n",
                        m->mhp, m->mhpmax, MON_WEP(m) ? "yes" : "no", m->mblinded, m->mcansee,
                        m->mspeed, MSLOW, vulture_aim_part);
            else
                fprintf(f, "orc gone\n");
            fclose(f);
        }
        if (m) { /* the menu, for a screenshot (the run ends here) */
            int dummy[4];

            vpause_probe_t = SDL_GetTicks();
            (void) vaim_pick(m, dummy);
        }
    }
}

/* s155: levelling up - golden rings ripple out from your feet, a spiral of
   motes rises around you, the area flashes warm and LEVEL UP! floats up.
   Drawn around wherever you are (vfx kind 11), so it follows you. */
extern "C" void
vulture_levelup_fx(void)
{
    vfx_ent e;

    if (!levwin || !u.ux)
        return;
    memset(&e, 0, sizeof e);
    e.kind = 11, e.x = u.ux, e.y = u.uy, e.t0 = SDL_GetTicks(), e.dur = 1900;
    e.r = 255, e.g = 215, e.b = 110;
    vfx_list.push_back(e);
    vfx_word(u.ux, u.uy, "LEVEL UP!", 255, 225, 120, 1700);
    vl_flash(u.ux, u.uy, 1.0f, 0.85f, 0.45f, 1.1f, 4.5f, 900);
    vulture_sfx("gain");
}

/* a sleeping or helpless monster died to your attack the same turn */
extern "C" void
vulture_silent_kill(int x, int y)
{
    if (!levwin)
        return;
    vfx_word(x, y, "SILENT KILL", 235, 215, 255, 1500);
    vfx_burst(x, y, 170, 120, 255);
    vhitstop_ms = 95;
    vulture_sfx("silent_kill");
}

extern "C" void
vulture_backstab(int x, int y)
{
    if (!levwin)
        return;
    vfx_word(x, y, "BACKSTAB", 255, 170, 70, 1100);
    vulture_sfx("backstab");
}

/* s146: a sneak attack (1), ambush (2) or critical hit (0) landed */
extern "C" void
vulture_crit_fx(int x, int y, int kind)
{
    if (getenv("VULTURE_SELFTEST")) { /* probe: s146 */
        char fp[700];
        FILE *f;

        snprintf(fp, sizeof fp, "%s\\crit.txt", getenv("VULTURE_SELFTEST"));
        if ((f = fopen(fp, "a")) != 0) {
            fprintf(f, "turn %ld kind %d luck-crit %d%%\n", svm.moves, kind, vulture_crit_chance());
            fclose(f);
        }
    }
    if (!levwin || !isok(x, y))
        return;
    if (kind == 1)
        vfx_word(x, y, "SNEAK ATTACK x3", 255, 215, 90, 1200);
    else if (kind == 2)
        vfx_word(x, y, "AMBUSH x2", 255, 150, 60, 1100);
    else if (kind == 3) /* s156: an aimed blow to the head or eyes */
        vfx_word(x, y, "AIMED!", 255, 120, 90, 1000);
    else if (kind == 4) /* s156: an aimed blow elsewhere */
        vfx_word(x, y, "AIMED", 230, 200, 160, 900);
    else
        vfx_word(x, y, "CRITICAL x2", 255, 245, 230, 1000);
    vulture_sfx(kind == 0 ? "skill_up" : "backstab");
    vhitstop_ms = kind == 1 ? 110 : 85;
}

/* a noise woke monsters within sqrt(dist2) squares of (x, y) */
extern "C" void
vulture_noise(int x, int y, int dist2v)
{
    vfx_ent e;
    int r;

    if (!levwin || !u.ux || !isok(x, y) || distmin(u.ux, u.uy, x, y) > 16)
        return;
    r = dist2v <= 0 ? 18 : (int) sqrt((double) dist2v);
    if (r < 1)
        return;
    if (r > 18)
        r = 18;
    memset(&e, 0, sizeof e);
    e.kind = 9, e.x = x, e.y = y, e.t0 = SDL_GetTicks(), e.dur = 650 + 40 * r;
    e.sub = r * 10; /* tenths of a square, see the draw */
    e.r = 255, e.g = 235, e.b = 170;
    vfx_list.push_back(e);
}

extern "C" void
vulture_heard(unsigned mid)
{
    vheard[mid] = true;
}

/* entering a special room: how many sleep, how many your entrance woke */
extern "C" void
vulture_room_sleepers(int asleep, int woke, int stealthy)
{
    if (asleep + woke < 2)
        return;
    if (woke)
        pline("Your entrance wakes %d of them%s; %d still sleep%s.", woke,
              stealthy ? "" : " - you are not stealthy", asleep,
              asleep == 1 ? "s" : "");
    else if (stealthy)
        pline("You slip in unheard.  %d of them sleep on.", asleep);
    else
        pline("%d of them are asleep.  Tread carefully.", asleep);
    vulture_sfx(woke ? "wake_q" : "sleeper_spot");
}

/* the stealth badge above the portrait: STEALTHY / NOISY / AGGRAVATING */
static SDL_Rect vsb_rect = { 0, 0, 0, 0 };
static SDL_Rect vspd_rect = { 0, 0, 0, 0 }; /* s144 */
static int vbadge_row_x = 0, vbadge_row_y = 0, vbadge_row_h = 0, vbadge_row_w = 0; /* s159 */
static double vhero_speed(void);

static const char *
vstealth_state(Uint8 *r, Uint8 *g, Uint8 *b, std::string *why)
{
    bool aggr_known = HAggravate_monster
        || (uleft && uleft->otyp == RIN_AGGRAVATE_MONSTER
            && objects[RIN_AGGRAVATE_MONSTER].oc_name_known)
        || (uright && uright->otyp == RIN_AGGRAVATE_MONSTER
            && objects[RIN_AGGRAVATE_MONSTER].oc_name_known);

    if (Stealth) {
        *why = HStealth ? "Stealthy by nature" : "Stealthy thanks to something you wear";
        if (HStealth && EStealth)
            *why = "Stealthy by nature and by what you wear";
        *why += " - your steps never wake sleeping monsters (ettins excepted)";
        *r = 120, *g = 220, *b = 140;
        return "STEALTHY";
    }
    if (aggr_known) {
        *why = "You aggravate monsters - sleepers wake as soon as they see you";
        *r = 255, *g = 90, *b = 70;
        return "AGGRAVATING";
    }
    if ((HStealth || EStealth) && BStealth)
        *why = "Riding cancels your stealth - ";
    else
        *why = "";
    *why += "sleepers that see you may wake: dogs and humans at once, others 1 in 7 a turn";
    *r = 200, *g = 170, *b = 120;
    return "NOISY";
}

static void
vstealth_draw(void)
{
    Uint8 r, g, b;
    std::string why;
    const char *lab;
    int ps, px, py, tw, lh = vulture_get_lineheight(V_FONT_SMALL);
    SDL_Rect rc;

    vsb_rect.w = 0;
    vspd_rect.w = 0;
    vbadge_row_w = 0; /* s159 */
    if (!stwin || !stwin->visible || !u.ux)
        return;
    lab = vstealth_state(&r, &g, &b, &why);
    ps = stwin->h, px = stwin->abs_x - V_UI(4) - ps, py = stwin->abs_y;
    tw = vulture_text_length(V_FONT_SMALL, lab);
    rc.w = tw + 14 > ps ? tw + 14 : ps, rc.h = lh + 4;
    rc.x = px, rc.y = py - rc.h - 3;
    if (rc.x < 0)
        rc.x = 0;
    /* s159: badges only when they say something - the row closes up */
    vbadge_row_x = rc.x, vbadge_row_y = rc.y, vbadge_row_h = rc.h, vbadge_row_w = 0;
    {
        bool sleepers = false;
        struct monst *sm;

        for (sm = fmon; sm && !sleepers; sm = sm->nmon)
            if (!DEADMONSTER(sm) && canspotmon(sm) && sm->msleeping && !sm->mtame)
                sleepers = true;
        if (strcmp(lab, "NOISY") || sleepers) {
            vbox(rc.x, rc.y, rc.w, rc.h, 200);
            {
                SDL_Rect bar = { (Sint16) rc.x, (Sint16) (rc.y + rc.h - 2), (Uint16) rc.w, 2 };

                SDL_FillRect(vulture_screen, &bar, SDL_MapRGB(vulture_screen->format, r, g, b));
            }
            vulture_put_text_shadow(V_FONT_SMALL, lab, vulture_screen, rc.x + (rc.w - tw) / 2,
                                    rc.y + 2, SDL_MapRGB(vulture_screen->format, r, g, b),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            vsb_rect = rc;
            vbadge_row_w = rc.w + 4;
        }
    }
    { /* s144: your speed, beside it - s159: only when it isn't normal */
        double s = vhero_speed();
        const char *sl = NULL;
        Uint8 sr = 0, sg = 0, sb = 0;
        int tw2;
        SDL_Rect r2;

        if (s >= 19)
            sl = "VERY FAST", sr = 120, sg = 230, sb = 255;
        else if (s >= 15)
            sl = "FAST", sr = 140, sg = 220, sb = 255;
        else if (s < 11.5)
            sl = "SLOW", sr = 255, sg = 150, sb = 80;
        if (sl) {
            tw2 = vulture_text_length(V_FONT_SMALL, sl);
            r2.x = vbadge_row_x + vbadge_row_w, r2.y = rc.y, r2.w = tw2 + 14, r2.h = rc.h;
            vbox(r2.x, r2.y, r2.w, r2.h, 200);
            {
                SDL_Rect bar = { (Sint16) r2.x, (Sint16) (r2.y + r2.h - 2), (Uint16) r2.w, 2 };

                SDL_FillRect(vulture_screen, &bar, SDL_MapRGB(vulture_screen->format, sr, sg, sb));
            }
            vulture_put_text_shadow(V_FONT_SMALL, sl, vulture_screen, r2.x + 7, r2.y + 2,
                                    SDL_MapRGB(vulture_screen->format, sr, sg, sb),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            vspd_rect = r2;
            vbadge_row_w += r2.w + 4;
        }
    }
}

static bool
vstealth_hit(int mx, int my)
{
    return vsb_rect.w && mx >= vsb_rect.x && mx < vsb_rect.x + vsb_rect.w && my >= vsb_rect.y
           && my < vsb_rect.y + vsb_rect.h;
}

/* ---- s142: first-time tips ----
   One short tip the first time something comes up, once ever per Windows
   user (tips-seen.txt).  Shown under the time pill; click to dismiss,
   right-click to turn tips off for good. */
struct vtip_def {
    const char *id, *title, *body;
};
static const vtip_def vtip_defs[] = {
    { "welcome", "Welcome",
      "Click to move, click a monster to attack, right-click for actions.\n"
      "Hover anything to learn about it - hold Shift on a monster\n"
      "for its full codex: attacks, resistances, what its corpse does." },
    { "sleeper", "Sleeping monsters",
      "z z marks a sleeper. A sneak attack on it always hits for triple\n"
      "damage - kill it before it wakes for a SILENT KILL. Stealth keeps\n"
      "them asleep (see the badge above your portrait)." },
    { "shop", "Shops",
      "Pick an item up to see its price. The price narrows down what an\n"
      "unidentified item can be, and the game remembers it for you.\n"
      "Pay with p, or right-click yourself." },
    { "altar", "Altars",
      "Drop items on an altar to see whether they're cursed.\n"
      "Standing on one, right-click yourself to test everything at once." },
    { "fountain", "Fountains",
      "Drinking from or dipping into a fountain is a gamble: a rare wish,\n"
      "more often water moccasins, nymphs or a soaking." },
    { "hungry", "Hunger",
      "You're hungry - eat soon (e). Hover a corpse to see if it's safe;\n"
      "corpses go bad after about 30 turns." },
    { "lowhp", "Low health",
      "Back off, rest (REST / F5), drink a healing potion, or pray -\n"
      "prayer usually works in real trouble if you haven't prayed lately." },
    { "trap", "Traps",
      "Hover a known trap to see what it does. Click-to-walk steers\n"
      "around the traps you know about." },
    { "burdened", "Burdened",
      "You carry too much and move slower. Hover items to see their\n"
      "weight and drop what you don't need." },
    { "stairs", "Stairs",
      "Press > (or <) from anywhere to walk to the stairs you've found.\n"
      "EXPLORE maps the level for you first." },
    { "unknown", "Unidentified items",
      "Its true name is hidden until you identify it: try it carefully,\n"
      "check its shop price, or read a scroll of identify." },
    { "gearsets", "Gear sets",
      "Click SET 1 (beside your speed badge) to save what you wear and wield.\n"
      "One click (or Ctrl+1) changes you back into it later - each piece\n"
      "is swapped for you, in the right order, at the normal speed." },
    { "realtime", "Real-time",
      "Turns pass while you stand still, and it switches to turn-based\n"
      "when a fight starts. Click the top-left pill or press Pause to change." },
};
#define VTIP_N ((int) (sizeof vtip_defs / sizeof vtip_defs[0]))
static int vtip_state = -1; /* -1 unloaded, 0 on, 1 off */
static std::map<std::string, bool> vtip_seen;
static int vtip_cur = -1, vtip_queue[VTIP_N], vtip_nq = 0;
static Uint32 vtip_born = 0;
static SDL_Rect vtip_rect = { 0, 0, 0, 0 };

static std::string
vtip_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char buf[700];

    snprintf(buf, sizeof buf, "%s\\NetHack\\5.0\\tips-seen.txt", la ? la : ".");
    return buf;
}

static void
vtip_save(void)
{
    FILE *f = fopen(vtip_path().c_str(), "w");
    std::map<std::string, bool>::iterator it;

    if (!f)
        return;
    if (vtip_state == 1)
        fprintf(f, "OFF\n");
    for (it = vtip_seen.begin(); it != vtip_seen.end(); ++it)
        fprintf(f, "%s\n", it->first.c_str());
    fclose(f);
}

static void
vtip_load(void)
{
    FILE *f;
    char ln[64];

    if (vtip_state >= 0)
        return;
    vtip_state = 0;
    if ((f = fopen(vtip_path().c_str(), "r")) == 0)
        return;
    while (fgets(ln, sizeof ln, f)) {
        ln[strcspn(ln, "\r\n")] = 0;
        if (!strcmp(ln, "OFF"))
            vtip_state = 1;
        else if (*ln)
            vtip_seen[ln] = true;
    }
    fclose(f);
}

static void
vtip_want(const char *id)
{
    int i, k;

    vtip_load();
    if (vtip_state == 1 || vtip_seen.count(id))
        return;
    for (i = 0; i < VTIP_N; i++)
        if (!strcmp(vtip_defs[i].id, id))
            break;
    if (i == VTIP_N || i == vtip_cur)
        return;
    for (k = 0; k < vtip_nq; k++)
        if (vtip_queue[k] == i)
            return;
    vtip_seen[id] = true; /* counts as seen once queued */
    vtip_save();
    vtip_queue[vtip_nq++] = i;
}

/* what is going on right now that deserves a tip? (every frame) */
static void
vtip_tick(void)
{
    static Uint32 next = 0;
    Uint32 now = SDL_GetTicks();
    struct monst *m;
    int x, y, hp = Upolyd ? u.mh : u.uhp, hpmax = Upolyd ? u.mhmax : u.uhpmax;

    if (!u.ux || now < next || Hallucination)
        return;
    next = now + 400;
    vtip_load();
    if (vtip_state == 1)
        return;
    if (svm.moves < 50)
        vtip_want("welcome");
    for (m = fmon; m; m = m->nmon)
        if (!DEADMONSTER(m) && canseemon(m) && !m->mtame && !m->mpeaceful && m->msleeping) {
            vtip_want("sleeper");
            break;
        }
    if (vrt_get() != 0) /* s142 */
        vtip_want("realtime");
    if (*u.ushops)
        vtip_want("shop");
    if (u.uhs >= HUNGRY)
        vtip_want("hungry");
    if (hp > 0 && hp * 3 < hpmax)
        vtip_want("lowhp");
    if (near_capacity() > UNENCUMBERED)
        vtip_want("burdened");
    for (x = u.ux - 6; x <= u.ux + 6; x++)
        for (y = u.uy - 6; y <= u.uy + 6; y++) {
            int g, c;

            if (!isok(x, y) || !cansee(x, y))
                continue;
            g = glyph_at(x, y);
            if (glyph_is_trap(g))
                vtip_want("trap");
            if (!glyph_is_cmap(g))
                continue;
            c = glyph_to_cmap(g);
            if (c == S_altar)
                vtip_want("altar");
            else if (c == S_fountain)
                vtip_want("fountain");
            else if (c == S_dnstair || c == S_dnladder)
                vtip_want("stairs");
        }
    {
        struct obj *o;

        for (o = gi.invent; o; o = o->nobj)
            if ((o->oclass == POTION_CLASS || o->oclass == SCROLL_CLASS)
                && !objects[o->otyp].oc_name_known && o->dknown) {
                vtip_want("unknown");
                break;
            }
        for (o = gi.invent; o; o = o->nobj)
            if (o->oclass == ARMOR_CLASS && !o->owornmask && svm.moves > 30) { /* s145 */
                vtip_want("gearsets");
                break;
            }
    }
}

static void
vtip_draw(void)
{
    const vtip_def *t;
    Uint32 now = SDL_GetTicks(), age;
    int lh = vulture_get_lineheight(V_FONT_SMALL), bw = 0, bh, nl = 1, a;
    std::string body;
    size_t p0 = 0, p1;

    vtip_rect.w = 0;
    if (vtip_cur < 0) {
        if (!vtip_nq)
            return;
        vtip_cur = vtip_queue[0];
        memmove(vtip_queue, vtip_queue + 1, (size_t) (--vtip_nq) * sizeof vtip_queue[0]);
        vtip_born = now;
        vulture_sfx("ui_open");
    }
    age = now - vtip_born;
    if (age > 11000) {
        vtip_cur = -1;
        return;
    }
    t = &vtip_defs[vtip_cur];
    body = t->body;
    while ((p1 = body.find('\n', p0)) != std::string::npos) {
        if (vulture_text_length(V_FONT_SMALL, body.substr(p0, p1 - p0)) > bw)
            bw = vulture_text_length(V_FONT_SMALL, body.substr(p0, p1 - p0));
        p0 = p1 + 1, nl++;
    }
    if (vulture_text_length(V_FONT_SMALL, body.substr(p0)) > bw)
        bw = vulture_text_length(V_FONT_SMALL, body.substr(p0));
    bw += 26, bh = (nl + 2) * lh + 10;
    a = age < 250 ? (int) (age * 200 / 250) : age > 10500 ? (int) ((11000 - age) * 200 / 500) : 200;
    vtip_rect.x = 8, vtip_rect.y = 54, vtip_rect.w = bw, vtip_rect.h = bh;
    vbox(vtip_rect.x, vtip_rect.y, bw, bh, a);
    {
        SDL_Rect bar = { (Sint16) vtip_rect.x, (Sint16) vtip_rect.y, 3, (Uint16) bh };

        SDL_FillRect(vulture_screen, &bar, SDL_MapRGB(vulture_screen->format, 120, 200, 255));
    }
    if (a < 100)
        return;
    vulture_put_text_shadow(V_FONT_SMALL, std::string("TIP  -  ") + t->title, vulture_screen,
                            vtip_rect.x + 12, vtip_rect.y + 5,
                            SDL_MapRGB(vulture_screen->format, 120, 200, 255),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    p0 = 0, nl = 1;
    while (true) {
        p1 = body.find('\n', p0);
        vulture_put_text_shadow(V_FONT_SMALL, body.substr(p0, p1 == std::string::npos ? std::string::npos : p1 - p0),
                                vulture_screen, vtip_rect.x + 12, vtip_rect.y + 5 + nl * lh,
                                SDL_MapRGB(vulture_screen->format, 230, 228, 220),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        if (p1 == std::string::npos)
            break;
        p0 = p1 + 1, nl++;
    }
    vulture_put_text_shadow(V_FONT_SMALL, "click to close  -  right-click: no more tips",
                            vulture_screen, vtip_rect.x + 12, vtip_rect.y + 5 + (nl + 1) * lh,
                            SDL_MapRGB(vulture_screen->format, 120, 120, 130),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
}

static bool
vtip_hit(int mx, int my)
{
    return vtip_rect.w && mx >= vtip_rect.x && mx < vtip_rect.x + vtip_rect.w
           && my >= vtip_rect.y && my < vtip_rect.y + vtip_rect.h;
}

/* s136: the screen edge flashes red when a blow really lands */
static void
vhurt_flash(int dmg, int maxhp)
{
    if (maxhp <= 0 || dmg * 10 < maxhp)
        return;
    vst_pulse_t = SDL_GetTicks() | 1;
    vst_r = 210, vst_g = 25, vst_b = 20;
}

/* s136: magic loot in view glints now and then, so you notice it (Brogue) */
static void
vglint_tick(void)
{
    static Uint32 next = 0;
    Uint32 now = SDL_GetTicks();
    int x, y, n = 0, pick;
    static short cx[COLNO * ROWNO], cy[COLNO * ROWNO];

    if (now < next || Hallucination || Blind)
        return;
    next = now + 900 + rand() % 900;
    for (x = 1; x < COLNO; x++)
        for (y = 0; y < ROWNO; y++) {
            int g, oc;

            if (!cansee(x, y) || u_at(x, y))
                continue;
            g = glyph_at(x, y);
            if (!glyph_is_object(g))
                continue;
            oc = objects[glyph_to_obj(g)].oc_class;
            if (oc == POTION_CLASS || oc == SCROLL_CLASS || oc == WAND_CLASS
                || oc == RING_CLASS || oc == AMULET_CLASS || oc == SPBOOK_CLASS
                || oc == GEM_CLASS)
                cx[n] = (short) x, cy[n++] = (short) y;
        }
    if (!n)
        return;
    pick = rand() % n;
    {
        vfx_ent e;

        memset(&e, 0, sizeof e);
        e.kind = 7, e.x = cx[pick], e.y = cy[pick], e.t0 = now, e.dur = 650;
        e.r = 255, e.g = 245, e.b = 200;
        e.sub = rand() % 17 - 8;
        vfx_list.push_back(e);
    }
}

/* s136: the dungeon breathes - quiet faraway sounds from random directions */
static void
vamb_tick(void)
{
    static Uint32 next = 0;
    Uint32 now = SDL_GetTicks();
    const char *pool[10];
    int n = 0;
    double a;
    int d;

    if (!next)
        next = now + 12000 + rand() % 15000;
    if (now < next || !u.ux || u.uhp <= 0)
        return;
    next = now + 18000 + rand() % 30000;
    if (Deaf)
        return;
    pool[n++] = "far_drip";
    pool[n++] = "far_creak";
    pool[n++] = "far_chain";
    pool[n++] = "far_rumble";
    if (depth(&u.uz) >= 5)
        pool[n++] = "far_howl";
    if (Inhell) {
        pool[n++] = "far_moan";
        pool[n++] = "far_rumble";
        pool[n++] = "far_moan";
    }
    a = (rand() % 360) * 3.14159265358979 / 180.0;
    d = 7 + rand() % 4;
    vulture_sfx_at_vol(pool[rand() % n], (int) (d * cos(a)), (int) (d * sin(a)),
                       45 + rand() % 30);
}

/* s144: a wait/rest turn ('.', counted rests) */
extern "C" void
vulture_rest_fx(void)
{
    vsr_t = SDL_GetTicks(), vsr_kind = 2;
}

/* s136: each search turn sends a pulse out from the hero (called from
   dosearch0) */
extern "C" void
vulture_search_fx(void)
{
    vfx_ent e;

    vsr_t = SDL_GetTicks(), vsr_kind = vquiet_search ? 2 : 1; /* s144 */
    if (!levwin || !u.ux || vquiet_search)
        return;
    memset(&e, 0, sizeof e);
    e.kind = 8, e.x = u.ux, e.y = u.uy, e.t0 = SDL_GetTicks(), e.dur = 700;
    e.r = 150, e.g = 220, e.b = 255;
    vfx_list.push_back(e);
    vulture_sfx("search_ping");
}

static void
vstatus_scan(void)
{
    static unsigned prev = 0;
    static bool init = false;
    static d_level lev = { -1, -1 };
    struct vst_def {
        bool on;
        const char *label;
        Uint8 r, g, b;
    } st[] = {
        { Confusion != 0, "Confused!", 190, 90, 255 },
        { Stunned != 0, "Stunned!", 255, 220, 70 },
        { Blind != 0, "Blinded!", 140, 140, 160 },
        { Hallucination != 0, "Hallucinating!", 255, 90, 200 },
        { Sick != 0, "Deathly sick!", 120, 220, 60 },
        { Stoned != 0, "Turning to stone!", 205, 200, 190 },
        { Slimed != 0, "Turning to slime!", 90, 230, 90 },
        { Strangled != 0, "Can't breathe!", 230, 60, 60 },
        { u.uhs >= WEAK, "Weak with hunger!", 235, 160, 60 },
    };
    unsigned mask = 0;
    int i, n = (int) (sizeof st / sizeof st[0]);

    if (!u.ux)
        return;
    for (i = 0; i < n; i++)
        if (st[i].on)
            mask |= 1u << i;
    if (!init || !on_level(&lev, &u.uz) && lev.dnum < 0) {
        prev = mask, init = true, lev = u.uz;
        return;
    }
    lev = u.uz;
    for (i = 0; i < n; i++)
        if ((mask & (1u << i)) && !(prev & (1u << i))) {
            vfx_ent e;

            vst_pulse_t = SDL_GetTicks() | 1;
            vulture_sfx("status_bad"); /* s131 */
            vst_r = st[i].r, vst_g = st[i].g, vst_b = st[i].b;
            memset(&e, 0, sizeof e);
            e.kind = 1, e.x = u.ux, e.y = u.uy, e.t0 = SDL_GetTicks(), e.dur = 1400;
            snprintf(e.txt, sizeof e.txt, "%s", st[i].label);
            e.r = st[i].r, e.g = st[i].g, e.b = st[i].b;
            vfx_list.push_back(e);
        }
    prev = mask;

    { /* s131: an enemy you haven't seen on this level comes into view */
        static std::map<unsigned, bool> seen;
        static d_level slev = { -1, -1 };
        static Uint32 last_alert = 0;
        bool fresh = !on_level(&slev, &u.uz), any = false;
        struct monst *m;

        static std::map<unsigned, bool> dozing; /* s135 */

        if (fresh) {
            seen.clear();
            dozing.clear();
            slev = u.uz;
        }
        for (m = fmon; m; m = m->nmon) { /* s135: it woke up and saw you */
            bool z;

            if (DEADMONSTER(m) || !canspotmon(m) || m->mtame || m->mpeaceful)
                continue;
            z = m->msleeping || !m->mcanmove;
            if (!fresh && dozing.count(m->m_id) && dozing[m->m_id] && !z
                && m->m_id != vulture_sneak_mid) { /* s137 */
                bool noise = vheard.count(m->m_id) != 0;

                vulture_say_above(m->m_id, m->mx, m->my, noise ? "?" : "!");
                vulture_sfx(noise ? "wake_q" : "spotted");
            }
            dozing[m->m_id] = z;
        }
        vheard.clear(); /* s137 */
        bool anyz = false; /* s137 */

        for (m = fmon; m; m = m->nmon)
            if (!DEADMONSTER(m) && canspotmon(m) && !m->mtame && !m->mpeaceful
                && !seen.count(m->m_id)) {
                seen[m->m_id] = true;
                if (m->msleeping || !m->mcanmove)
                    anyz = true; /* s137: a sleeper is an opportunity, not an alarm */
                else
                    any = true;
            }
        if ((any || anyz) && !fresh && SDL_GetTicks() - last_alert > 1200) {
            last_alert = SDL_GetTicks();
            vulture_sfx(any ? "alert" : "sleeper_spot");
        }
    }
}

static void
vstatus_pulse_draw(int ax, int ay, int w, int h)
{
    Uint32 age;
    int k, A, t;

    if (!vst_pulse_t)
        return;
    age = SDL_GetTicks() - vst_pulse_t;
    if (age > 1000) {
        vst_pulse_t = 0;
        return;
    }
    A = (int) (170 * (age < 150 ? age / 150.0 : 1.0 - (age - 150) / 850.0));
    t = (w < h ? w : h) / 45;
    if (t < 3)
        t = 3;
    for (k = 0; k < 8; k++) {
        int a = A * (8 - k) / 16;

        vtint(ax + k * t, ay + k * t, w - 2 * k * t, t, vst_r, vst_g, vst_b, a);
        vtint(ax + k * t, ay + h - (k + 1) * t, w - 2 * k * t, t, vst_r, vst_g, vst_b, a);
        vtint(ax + k * t, ay + (k + 1) * t, t, h - 2 * (k + 1) * t, vst_r, vst_g, vst_b, a);
        vtint(ax + w - (k + 1) * t, ay + (k + 1) * t, t, h - 2 * (k + 1) * t,
              vst_r, vst_g, vst_b, a);
    }
}

/* threat word + colour for a monster (shared by the card and the panel) */
static const char *
vthreat(struct monst *m, Uint8 *r, Uint8 *g, Uint8 *b)
{
    int d = (int) m->data->difficulty - (int) u.ulevel;

    if (m->mtame)
        return *r = 120, *g = 230, *b = 140, "Ally";
    if (m->mpeaceful)
        return *r = 130, *g = 190, *b = 255, "Peaceful";
    if (d <= -4)
        return *r = 170, *g = 170, *b = 170, "Trivial";
    if (d <= 0)
        return *r = 220, *g = 230, *b = 150, "Fair fight";
    if (d <= 3)
        return *r = 255, *g = 160, *b = 60, "Dangerous";
    return *r = 255, *g = 70, *b = 60, "Deadly";
}

/* ---- auto actions ---- */
enum { VAUTO_NONE, VAUTO_AMOVE, VAUTO_REST, VAUTO_GEAR /* s145 */ };
static int vauto_mode = VAUTO_NONE;
static point vauto_to = { -1, -1 };
static bool vamove_pending = false;
static long vauto_moves = -1;
static int vauto_idle = 0;

static struct monst *
vauto_nearest(int *dist)
{
    struct monst *m, *best = 0;
    int bd = 999;

    for (m = fmon; m; m = m->nmon) {
        int d;

        if (DEADMONSTER(m) || !canspotmon(m) || m->mtame || m->mpeaceful)
            continue;
        d = distmin(u.ux, u.uy, m->mx, m->my);
        if (d < bd)
            bd = d, best = m;
    }
    if (dist)
        *dist = bd;
    return best;
}

/* queue one step (an attack, if a hostile stands there) toward (tx, ty) */
static bool
vauto_step_toward(int tx, int ty)
{
    static point pts[COLNO * ROWNO];
    int n, dx, dy, dd;

    if (distmin(u.ux, u.uy, tx, ty) == 1) {
        dx = tx - u.ux, dy = ty - u.uy;
    } else {
        n = vpath_find(tx, ty, pts, COLNO * ROWNO);
        if (n < 1)
            return false;
        dx = pts[n - 1].x - u.ux, dy = pts[n - 1].y - u.uy;
    }
    dd = xytodir(dx, dy);
    if (dd == DIR_ERR)
        return false;
    cmdq_add_key(CQ_CANNED, gc.Cmd.dirchars[dd]);
    vulture_click_consumed = TRUE;
    return true;
}

static void
vauto_result(void *result)
{
    ((vulture_event *) result)->num = 0;
    ((vulture_event *) result)->x = u.ux;
    ((vulture_event *) result)->y = u.uy;
}

/* Tab / FIGHT: one blow (or a step) at the nearest hostile */
static bool
vauto_fight(void *result)
{
    struct monst *m = vauto_nearest(NULL);

    if (!m || !vauto_step_toward(m->mx, m->my))
        return false;
    vauto_result(result);
    return true;
}

static void
vauto_stop(const char *why)
{
    if (vauto_mode != VAUTO_NONE) { /* s131 */
        if (why && strstr(why, "fully rested"))
            vulture_sfx("rest_done");
        else if (why)
            vulture_sfx("ui_cancel");
    }
    if (vauto_mode != VAUTO_NONE && why)
        pline("%s", why);
    vauto_mode = VAUTO_NONE;
    vquiet_search = false; /* s140 */
}

static void
vauto_start(int mode, int tx, int ty)
{
    vauto_mode = mode;
    vauto_to.x = tx, vauto_to.y = ty;
    vauto_moves = -1;
    vauto_idle = 0;
    if (mode != VAUTO_GEAR) /* s145: the gear makes its own sounds */
        vulture_sfx(mode == VAUTO_REST ? "rest_start" : "amove_go"); /* s131 */
    vquiet_search = mode == VAUTO_REST; /* s140: rest quietly */
}

/* ---- s145: gear sets ----
   Three outfits per character, beside the speed badge.  Click an empty one
   (or right-click any) to save what you wear and wield now; click a saved
   one to change into it.  The change runs one stock action per turn through
   the auto-step driver (VAUTO_GEAR -> vulture_gear_act in do_wear.c), so
   delays, curses and interruptions are the game's own, and any key or
   click stops it.  Ctrl+1..3 from the keyboard.  Kept per character in
   gearsets-<name>.txt by object id (ids survive save and restore), tagged
   with the character's birthday so a new hero of the same name starts
   empty. */
#define VGS_N 3
#define VGS_MAX 16
struct vgs_set {
    int n;
    unsigned id[VGS_MAX];
    long slot[VGS_MAX];
};
static vgs_set vgs[VGS_N];
static char vgs_owner[PL_NSIZ + 32] = "";
static SDL_Rect vgs_rect[VGS_N];
static int vgs_going = -1;
static const long vgs_slots[] = { W_WEP, W_ARMU, W_ARM,  W_ARMC, W_ARMH,  W_ARMG, W_ARMF,
                                  W_ARMS, W_AMUL, W_TOOL, W_RINGL, W_RINGR, 0L };

static std::string
vgs_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char name[PL_NSIZ + 4], buf[700];
    int i;

    for (i = 0; svp.plname[i] && i < PL_NSIZ; i++)
        name[i] = isalnum((unsigned char) svp.plname[i]) ? svp.plname[i] : '_';
    name[i] = 0;
    snprintf(buf, sizeof buf, "%s\\NetHack\\5.0\\gearsets-%s.txt", la ? la : ".", name);
    return buf;
}

static void
vgs_save(void)
{
    FILE *f = fopen(vgs_path().c_str(), "w");
    int s, i;

    if (!f)
        return;
    fprintf(f, "%ld\n", (long) ubirthday);
    for (s = 0; s < VGS_N; s++) {
        fprintf(f, "%d", vgs[s].n);
        for (i = 0; i < vgs[s].n; i++)
            fprintf(f, " %u %ld", vgs[s].id[i], vgs[s].slot[i]);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void
vgs_load(void)
{
    char key[PL_NSIZ + 32];
    FILE *f;
    long bd = 0;
    int s, i, n;

    if (!svp.plname[0])
        return;
    snprintf(key, sizeof key, "%s/%ld", svp.plname, (long) ubirthday);
    if (!strcmp(key, vgs_owner))
        return;
    snprintf(vgs_owner, sizeof vgs_owner, "%s", key);
    memset(vgs, 0, sizeof vgs);
    if ((f = fopen(vgs_path().c_str(), "r")) == 0)
        return;
    if (fscanf(f, "%ld", &bd) == 1 && bd == (long) ubirthday)
        for (s = 0; s < VGS_N; s++) {
            if (fscanf(f, "%d", &n) != 1 || n < 0 || n > VGS_MAX)
                break;
            for (i = 0; i < n; i++)
                if (fscanf(f, "%u %ld", &vgs[s].id[i], &vgs[s].slot[i]) != 2) {
                    n = i;
                    break;
                }
            vgs[s].n = n;
        }
    fclose(f);
}

static struct obj *
vgs_obj(unsigned id)
{
    struct obj *o;

    for (o = gi.invent; id && o; o = o->nobj)
        if (o->o_id == id)
            return o;
    return NULL;
}

static struct obj *
vgs_in(long s)
{
    switch (s) {
    case W_WEP: return uwep;
    case W_ARMU: return uarmu;
    case W_ARM: return uarm;
    case W_ARMC: return uarmc;
    case W_ARMH: return uarmh;
    case W_ARMG: return uarmg;
    case W_ARMF: return uarmf;
    case W_ARMS: return uarms;
    case W_AMUL: return uamul;
    case W_TOOL: return ublindf;
    case W_RINGL: return uleft;
    case W_RINGR: return uright;
    }
    return NULL;
}

static void
vgs_store(int k)
{
    vgs_set &g = vgs[k];
    int i;

    vgs_load();
    g.n = 0;
    for (i = 0; vgs_slots[i] && g.n < VGS_MAX; i++) {
        struct obj *o = vgs_in(vgs_slots[i]);

        if (vgs_slots[i] == W_WEP || o) {
            g.id[g.n] = o ? o->o_id : 0;
            g.slot[g.n++] = vgs_slots[i];
        }
    }
    vgs_save();
    vulture_sfx("ui_click");
    pline("Gear set %d saved: %s and %d piece%s worn.  Click it to change back into it.",
          k + 1, uwep ? "your weapon" : "bare hands", g.n - 1, g.n - 1 == 1 ? "" : "s");
}

/* 2 wearing it now, 1 saved, 0 empty */
static int
vgs_state(int k)
{
    int i;

    if (!vgs[k].n)
        return 0;
    for (i = 0; i < vgs[k].n; i++) {
        struct obj *o = vgs_obj(vgs[k].id[i]);

        if (vgs[k].slot[i] == W_WEP) {
            if (vgs[k].id[i] ? (!o || uwep != o) : uwep != NULL)
                return 1;
        } else if (!o || !(o->owornmask & (W_ARMOR | W_ACCESSORY))) {
            return 1;
        }
    }
    return 2;
}

static void
vgs_equip(int k)
{
    vgs_load();
    if (!vgs[k].n) {
        vgs_store(k);
        return;
    }
    if (vgs_state(k) == 2) {
        pline("You're already wearing gear set %d.", k + 1);
        return;
    }
    if (vauto_mode != VAUTO_NONE)
        vauto_stop(NULL);
    vulture_gear_begin(vgs[k].id, vgs[k].slot, vgs[k].n);
    vgs_going = k;
    vulture_sfx("ui_click");
    pline("You start changing into gear set %d.", k + 1);
    vauto_start(VAUTO_GEAR, -1, -1);
}

static int
vgs_hit(int mx, int my)
{
    int i;

    for (i = 0; i < VGS_N; i++)
        if (vgs_rect[i].w && mx >= vgs_rect[i].x && mx < vgs_rect[i].x + vgs_rect[i].w
            && my >= vgs_rect[i].y && my < vgs_rect[i].y + vgs_rect[i].h)
            return i;
    return -1;
}

static void
vgs_draw(void)
{
    int i, x;

    memset(vgs_rect, 0, sizeof vgs_rect);
    if (!vbadge_row_h || !u.ux)
        return;
    vgs_load();
    { /* s159: only once there's something to swap - a saved set, or spare
         armour in the pack */
        bool any = vgs[0].n || vgs[1].n || vgs[2].n;
        struct obj *so;

        for (so = gi.invent; so && !any; so = so->nobj)
            if (so->oclass == ARMOR_CLASS && !so->owornmask)
                any = true;
        if (!any)
            return;
    }
    x = vbadge_row_x + vbadge_row_w;
    for (i = 0; i < VGS_N; i++) {
        char lab[16];
        int st = vgs_state(i), tw;
        bool going = vauto_mode == VAUTO_GEAR && vgs_going == i;
        Uint8 r = 120, g = 120, b = 130;
        SDL_Rect rc;

        snprintf(lab, sizeof lab, "SET %d", i + 1);
        tw = vulture_text_length(V_FONT_SMALL, lab);
        rc.x = x, rc.y = vbadge_row_y, rc.w = tw + 14, rc.h = vbadge_row_h; /* s159 */
        if (going) {
            int p = 170 + (int) (85 * (0.5 + 0.5 * sin(SDL_GetTicks() / 120.0)));

            r = 255, g = (Uint8) p, b = 90;
        } else if (st == 2) {
            r = 120, g = 220, b = 140;
        } else if (st == 1) {
            r = 210, g = 195, b = 160;
        }
        vbox(rc.x, rc.y, rc.w, rc.h, st || going ? 200 : 120);
        if (st || going) {
            SDL_Rect bar = { (Sint16) rc.x, (Sint16) (rc.y + rc.h - 2), (Uint16) rc.w, 2 };

            SDL_FillRect(vulture_screen, &bar, SDL_MapRGB(vulture_screen->format, r, g, b));
        }
        vulture_put_text_shadow(V_FONT_SMALL, lab, vulture_screen, rc.x + 7, rc.y + 2,
                                SDL_MapRGB(vulture_screen->format, r, g, b),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        vgs_rect[i] = rc;
        x += rc.w + 3;
    }
}

/* probe: s145 - save set 1 as worn, build set 2 (coat under the cloak,
   helmet, dagger) and change into it; stage 4 writes what's worn */

static void
vgs_probe(int stage, const char *dir)
{
    char fp[700];
    FILE *f;

    if (stage == 0) {
        struct obj *a = mksobj(ELVEN_MITHRIL_COAT, FALSE, FALSE),
                   *h = mksobj(DWARVISH_IRON_HELM, FALSE, FALSE),
                   *d = mksobj(DAGGER, FALSE, FALSE);
        vgs_set &g = vgs[1];

        a->cursed = h->cursed = d->cursed = 0;
        a = addinv(a), h = addinv(h), d = addinv(d);
        vgs_store(0);
        g.n = 0;
        g.id[g.n] = d->o_id, g.slot[g.n++] = W_WEP;
        g.id[g.n] = a->o_id, g.slot[g.n++] = W_ARM;
        g.id[g.n] = h->o_id, g.slot[g.n++] = W_ARMH;
        if (uarmc)
            g.id[g.n] = uarmc->o_id, g.slot[g.n++] = W_ARMC;
        vgs_save();
        vgs_equip(1);
        return;
    }
    snprintf(fp, sizeof fp, "%s\\gear.txt", dir);
    if ((f = fopen(fp, "w")) != 0) {
        fprintf(f, "turn=%ld set1=%d set2=%d going=%d mode=%d\n", svm.moves, vgs_state(0),
                vgs_state(1), vgs_going, vauto_mode);
        fprintf(f, "wep=%s\n", uwep ? doname(uwep) : "-");
        fprintf(f, "suit=%s\n", uarm ? doname(uarm) : "-");
        fprintf(f, "cloak=%s\n", uarmc ? doname(uarmc) : "-");
        fprintf(f, "helm=%s\n", uarmh ? doname(uarmh) : "-");
        fclose(f);
    }
}

static std::string
vgs_tip(int k)
{
    char hb[160];
    std::string t;
    int i;

    vgs_load();
    if (!vgs[k].n) {
        snprintf(hb, sizeof hb,
                 "Gear set %d (empty)\nClick to save what you wear and wield right now,\n"
                 "then click it again any time to change back into it  [Ctrl+%d]",
                 k + 1, k + 1);
        return hb;
    }
    snprintf(hb, sizeof hb,
             "Gear set %d - click to change into it, one piece per turn  [Ctrl+%d]\n"
             "Right-click to save what you wear now instead",
             k + 1, k + 1);
    t = hb;
    for (i = 0; i < vgs[k].n; i++) {
        struct obj *o = vgs_obj(vgs[k].id[i]);

        t += "\n  ";
        if (vgs[k].slot[i] == W_WEP && !vgs[k].id[i])
            t += "bare hands";
        else
            t += o ? doname(o) : "(no longer carried)";
    }
    return t;
}

/* ---- s140: time modes ----
   0 turn-based (classic), 1 real-time, 2 real-time until combat (default).
   While the map waits for a command and you stand idle, real-time passes a
   turn every VRT_MS (a plain wait: no search, no safe_wait prompt).  Mode 2
   stops ticking while an awake hostile is in view or just after you were
   hurt, and resumes a moment after the fight.  Nothing ticks inside menus,
   prompts, farlook, multi-turn actions or with the window unfocused.
   The choice is kept per Windows user in timemode.txt. */
static int vrt_speed = -1; /* s142: 0 normal, 1 slow, 2 fast */
static const int vrt_speed_ms[3] = { 650, 1000, 400 };
static const char *const vrt_speed_nm[3] = { "normal", "slow", "fast" };
#define VRT_MS (vrt_speed_ms[vrt_speed < 0 ? 0 : vrt_speed])
#define VRT_RESUME_MS 1200
extern int vulture_has_focus;
static int vrt_mode = -1;
static Uint32 vrt_last_tick = 0, vrt_last_call = 0, vrt_combat_until = 0;
static int vrt_calls = 0, vrt_ticks = 0; /* probe counters */
static long vrt_last_moves = -1;
static int vrt_last_hp = -1;
static SDL_Rect vrt_rect = { 0, 0, 0, 0 };
static const char *const vrt_names[3] = { "Turn-based", "Real-time",
                                          "Real-time, turn-based in combat" };

static std::string
vrt_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char buf[700];

    snprintf(buf, sizeof buf, "%s\\NetHack\\5.0\\timemode.txt", la ? la : ".");
    return buf;
}

static int
vrt_get(void)
{
    if (vrt_mode < 0) {
        FILE *f = fopen(vrt_path().c_str(), "r");
        int m = 2;

        int sp = 0;

        if (f) {
            if (fscanf(f, "%d", &m) != 1 || m < 0 || m > 2)
                m = 2;
            if (fscanf(f, "%d", &sp) != 1 || sp < 0 || sp > 2) /* s142 */
                sp = 0;
            fclose(f);
        }
        vrt_mode = m;
        vrt_speed = sp;
    }
    return vrt_mode;
}

static void
vrt_cycle(void)
{
    FILE *f;

    vrt_mode = (vrt_get() + 1) % 3;
    if ((f = fopen(vrt_path().c_str(), "w")) != 0) {
        fprintf(f, "%d %d\n", vrt_mode, vrt_speed < 0 ? 0 : vrt_speed);
        fclose(f);
    }
    vrt_last_tick = SDL_GetTicks();
    pline("Time mode: %s.", vrt_names[vrt_mode]);
    vulture_sfx("ui_lock");
}

/* s142: right-click the pill - how fast real-time turns pass */
static void
vrt_speed_cycle(void)
{
    FILE *f;

    (void) vrt_get();
    vrt_speed = (vrt_speed + 1) % 3;
    if ((f = fopen(vrt_path().c_str(), "w")) != 0) {
        fprintf(f, "%d %d\n", vrt_mode, vrt_speed);
        fclose(f);
    }
    pline("Real-time speed: %s (a turn every %.2f s).", vrt_speed_nm[vrt_speed],
          vrt_speed_ms[vrt_speed] / 1000.0);
    vulture_sfx("ui_click");
}

/* is there a fight going on (for mode 2)? */
static bool
vrt_combat(void)
{
    struct monst *m;
    Uint32 now = SDL_GetTicks();
    int hp = Upolyd ? u.mh : u.uhp;
    bool fight = false;

    if (vrt_last_hp >= 0 && hp < vrt_last_hp)
        fight = true; /* something hurt you */
    vrt_last_hp = hp;
    for (m = fmon; m && !fight; m = m->nmon)
        if (!DEADMONSTER(m) && !m->mtame && !m->mpeaceful && canspotmon(m)
            && !m->msleeping && m->mcanmove && couldsee(m->mx, m->my)
            && distmin(u.ux, u.uy, m->mx, m->my) <= 12)
            fight = true;
    if (fight)
        vrt_combat_until = now + VRT_RESUME_MS;
    return fight || now < vrt_combat_until;
}

static int
vrt_wait(void)
{
    return ECMD_TIME; /* one turn passes */
}

bool
vulture_rt_step(void *result)
{
    Uint32 now = SDL_GetTicks();
    bool gap = now - vrt_last_call > 300; /* we were in a menu or a prompt */

    vrt_last_call = now;
    vrt_calls++;
    if (vrt_get() == 0 || !levwin || vulture_whatis_active || vauto_mode != VAUTO_NONE
        || gm.multi || go.occupation || !u.ux || (Upolyd ? u.mh : u.uhp) <= 0
        || vaim_on || vulture_aim_mode)
        return false;
    if (gap || svm.moves != vrt_last_moves || !vulture_has_focus
        || vanim_input_waiting()) {
        vrt_last_moves = svm.moves;
        vrt_last_tick = now; /* you just acted (or came back): full wait */
        return false;
    }
    if (vrt_mode == 2 && vrt_combat()) {
        vrt_last_tick = now;
        return false;
    }
    if (now - vrt_last_tick < VRT_MS)
        return false;
    vrt_last_tick = now;
    cmdq_add_ec(CQ_CANNED, vrt_wait);
    vrt_ticks++;
    vulture_click_consumed = TRUE;
    vauto_result(result);
    return true;
}

/* probe only: why real-time is (not) ticking */
static void
vrt_probe_dump(FILE *f)
{
    fprintf(f, "focus=%d combat=%d multi=%d occ=%d aim=%d aimmode=%d whatis=%d auto=%d "
               "last_call_age=%u input=%d\n",
            vulture_has_focus, vrt_combat() ? 1 : 0, gm.multi, go.occupation ? 1 : 0,
            vaim_on ? 1 : 0, vulture_aim_mode, vulture_whatis_active ? 1 : 0, vauto_mode,
            (unsigned) (SDL_GetTicks() - vrt_last_call), vanim_input_waiting() ? 1 : 0);
    fprintf(f, "calls=%d ticks=%d lastmoves=%ld\n", vrt_calls, vrt_ticks, vrt_last_moves);
}

/* the pill in the top-left corner: which mode, and the state */
static void
vrt_draw(void)
{
    const char *lab;
    Uint8 r, g, b;
    int lh = vulture_get_lineheight(V_FONT_SMALL), tw, m = vrt_get();
    Uint32 now = SDL_GetTicks();
    bool fight = false, run = false;
    SDL_Rect rc, bar;

    vrt_rect.w = 0;
    if (!u.ux || !levwin)
        return;
    if (m == 2) {
        fight = vrt_combat_until && now < vrt_combat_until;
        if (!fight) {
            struct monst *mm;

            for (mm = fmon; mm; mm = mm->nmon)
                if (!DEADMONSTER(mm) && !mm->mtame && !mm->mpeaceful && canspotmon(mm)
                    && !mm->msleeping && mm->mcanmove && couldsee(mm->mx, mm->my)
                    && distmin(u.ux, u.uy, mm->mx, mm->my) <= 12)
                    fight = true;
        }
    }
    if (m == 0)
        lab = "TURN-BASED", r = 170, g = 170, b = 180;
    else if (!vulture_has_focus)
        lab = "PAUSED", r = 170, g = 170, b = 180;
    else if (fight)
        lab = "COMBAT  -  TURN-BASED", r = 255, g = 120, b = 80;
    else
        lab = "REAL-TIME", r = 120, g = 225, b = 150, run = true;
    tw = vulture_text_length(V_FONT_SMALL, lab);
    rc.x = 8, rc.y = 8, rc.w = tw + 18, rc.h = lh + 7;
    vbox(rc.x, rc.y, rc.w, rc.h, 190);
    vulture_put_text_shadow(V_FONT_SMALL, lab, vulture_screen, rc.x + 9, rc.y + 2,
                            SDL_MapRGB(vulture_screen->format, r, g, b),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
    bar.x = rc.x, bar.y = rc.y + rc.h - 2, bar.h = 2, bar.w = rc.w;
    SDL_FillRect(vulture_screen, &bar, SDL_MapRGB(vulture_screen->format, r / 3, g / 3, b / 3));
    if (run && vrt_last_tick) { /* time until the next turn passes */
        Uint32 el = now - vrt_last_tick;

        bar.w = (Uint16) (rc.w * (el > VRT_MS ? VRT_MS : el) / VRT_MS);
        SDL_FillRect(vulture_screen, &bar, SDL_MapRGB(vulture_screen->format, r, g, b));
    }
    vrt_rect = rc;
}

static bool
vrt_hit(int mx, int my)
{
    return vrt_rect.w && mx >= vrt_rect.x && mx < vrt_rect.x + vrt_rect.w
           && my >= vrt_rect.y && my < vrt_rect.y + vrt_rect.h;
}

/* the input loop asks this while the map waits for a command: queue the
   next turn of an auto action (true), or let the player act (false) */
bool
vulture_auto_step(void *result)
{
    int hp = Upolyd ? u.mh : u.uhp, hpmax = Upolyd ? u.mhmax : u.uhpmax, d;
    struct monst *m;

    if (vauto_mode == VAUTO_NONE || !levwin || vulture_whatis_active)
        return false;
    if (vanim_input_waiting()) { /* any key or click takes over */
        SDL_Event ev[16];
        int n, i;

        n = SDL_PeepEvents(ev, 16, SDL_PEEKEVENT, SDL_KEYDOWN, SDL_KEYDOWN);
        for (i = 0; i < n; i++)
            if (ev[i].key.keysym.sym == SDLK_ESCAPE)
                SDL_FlushEvent(SDL_KEYDOWN);
        n = SDL_PeepEvents(ev, 16, SDL_PEEKEVENT, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONDOWN);
        for (i = 0; i < n; i++)
            if (ev[i].button.button == SDL_BUTTON_LEFT) {
                SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);
                vulture_swallow_lclick_up = true;
                vulture_swallow_lclick_t = SDL_GetTicks();
                break;
            }
        vauto_stop("You stop.");
        return false;
    }
    if (gm.multi || go.occupation || u.uswallow)
        return false;
    if (vauto_mode == VAUTO_GEAR) { /* s145: one stock action per turn */
        int p = vulture_gear_pending();

        if (p <= 0) {
            if (p == 0) {
                int miss = vulture_gear_missed();

                if (miss)
                    pline("Gear set %d is on - %d piece%s couldn't be put on or %s gone.",
                          vgs_going + 1, miss, miss == 1 ? "" : "s", miss == 1 ? "is" : "are");
                else
                    pline("Gear set %d is on.", vgs_going + 1);
            }
            vauto_stop(NULL);
            vgs_going = -1;
            return false;
        }
        cmdq_add_ec(CQ_CANNED, vulture_gear_act);
        vulture_click_consumed = TRUE;
        vauto_result(result);
        return true;
    }
    if (svm.moves == vauto_moves) {
        if (++vauto_idle > 3) {
            vauto_stop("You stop - you're not getting anywhere.");
            return false;
        }
    } else {
        vauto_idle = 0;
    }
    vauto_moves = svm.moves;
    if (vauto_mode == VAUTO_REST) {
        if (vauto_nearest(&d)) {
            vauto_stop("You stop resting - an enemy is in view.");
            return false;
        }
        if (hp >= hpmax && u.uen >= u.uenmax) {
            vauto_stop("You feel fully rested.");
            return false;
        }
        if (u.uhs >= WEAK) {
            vauto_stop("You're too hungry to rest.");
            return false;
        }
        cmdq_add_ec(CQ_CANNED, dosearch);
        vulture_click_consumed = TRUE;
        vauto_result(result);
        return true;
    }
    /* attack-move: fight whatever shows up, else keep walking */
    if (hp * 3 < hpmax) {
        vauto_stop("You stop - you're badly hurt!");
        return false;
    }
    if ((m = vauto_nearest(&d)) != 0 && d <= 8 && vauto_step_toward(m->mx, m->my)) {
        vauto_result(result);
        return true;
    }
    if (u.ux == vauto_to.x && u.uy == vauto_to.y) {
        vauto_stop(NULL);
        return false;
    }
    if (!vauto_step_toward(vauto_to.x, vauto_to.y)) {
        vauto_stop("You can't find a way there.");
        return false;
    }
    vauto_result(result);
    return true;
}

/* ---- s161: Visual effects - every new effect can be switched off ----
   Kept per Windows user in effects.txt. */
static void vl_relief_set(bool on);
static bool vl_relief_get(void);

static std::string
vfxm_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char buf[700];

    snprintf(buf, sizeof buf, "%s\\NetHack\\5.0\\effects.txt", la ? la : ".");
    return buf;
}

static void
vfxm_save(void)
{
    FILE *f = fopen(vfxm_path().c_str(), "w");

    if (!f)
        return;
    fprintf(f, "normalmaps=%d\nshadows=%d\natmosphere=%d\npuffs=%d\njolts=%d\nwords=%d\n",
            (int) vl_relief_get(), (int) vfx_shadows, (int) vfx_ambient, (int) vfx_puffs,
            (int) vfx_jolts, (int) vfx_words);
    fclose(f);
}

static void
vfxm_load(void)
{
    static bool done = false;
    FILE *f;
    char ln[80];

    if (done)
        return;
    done = true;
    if ((f = fopen(vfxm_path().c_str(), "r")) == 0)
        return;
    while (fgets(ln, sizeof ln, f)) {
        int v = 1;
        char *eq = strchr(ln, '=');

        if (!eq)
            continue;
        v = atoi(eq + 1);
        *eq = 0;
        if (!strcmp(ln, "normalmaps"))
            vl_relief_set(v != 0);
        else if (!strcmp(ln, "shadows"))
            vfx_shadows = v != 0;
        else if (!strcmp(ln, "atmosphere"))
            vfx_ambient = v != 0;
        else if (!strcmp(ln, "puffs"))
            vfx_puffs = v != 0;
        else if (!strcmp(ln, "jolts"))
            vfx_jolts = v != 0;
        else if (!strcmp(ln, "words"))
            vfx_words = v != 0;
    }
    fclose(f);
}

static void
vfx_menu(void)
{
    for (;;) {
        int winid, n, pick;
        anything any;
        menu_item *sel = NULL;
        char lb[6][80];
        const char *nm[6] = { "Normal-mapped lighting", "Contact shadows", "Atmosphere particles",
                              "Dust puffs when walking", "Camera jolts from heavy impacts",
                              "Rising words (SNEAK ATTACK, LEVEL UP...)" };
        bool on[6];
        int k;

        on[0] = vl_relief_get(), on[1] = vfx_shadows, on[2] = vfx_ambient, on[3] = vfx_puffs;
        on[4] = vfx_jolts, on[5] = vfx_words;
        winid = vulture_create_nhwindow(NHW_MENU);
        vulture_start_menu(winid, MENU_BEHAVE_STANDARD);
        for (k = 0; k < 6; k++) {
            snprintf(lb[k], sizeof lb[k], "%s  -  %s", nm[k], on[k] ? "ON" : "off");
            any = cg.zeroany;
            any.a_int = k + 1;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, (char) ('a' + k), 0,
                             on[k] ? ATR_BOLD : ATR_NONE, NO_COLOR, lb[k], MENU_ITEMFLAGS_NONE);
        }
        { /* s168: the GPU presenter, applies on restart */
            static char gl[96];

            snprintf(gl, sizeof gl, "GPU presentation  -  %s  (takes effect when you restart)",
                     vulture_opts.gpu ? "ON" : "off");
            any = cg.zeroany;
            any.a_int = 7;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 'g', 0,
                             vulture_opts.gpu ? ATR_BOLD : ATR_NONE, NO_COLOR, gl, MENU_ITEMFLAGS_NONE);
        }
        vulture_end_menu(winid, "Visual effects  -  pick one to switch it");
        n = vulture_select_menu(winid, PICK_ONE, &sel);
        vulture_destroy_nhwindow(winid);
        pick = (n > 0 && sel) ? sel[0].item.a_int : 0;
        if (sel)
            free((genericptr_t) sel);
        if (pick == 7) { /* s168 */
            vulture_opts.gpu = !vulture_opts.gpu;
            vulture_write_userconfig();
            vulture_sfx("ui_click");
            pline("GPU presentation %s - restart the game for it to take effect.",
                  vulture_opts.gpu ? "on" : "off");
            continue;
        }
        if (pick < 1 || pick > 6)
            return;
        switch (pick) {
        case 1: vl_relief_set(!vl_relief_get()); break;
        case 2: vfx_shadows = !vfx_shadows; break;
        case 3: vfx_ambient = !vfx_ambient; break;
        case 4: vfx_puffs = !vfx_puffs; break;
        case 5: vfx_jolts = !vfx_jolts; break;
        case 6: vfx_words = !vfx_words; break;
        }
        vfxm_save();
        vulture_sfx("ui_click");
        if (levwin)
            levwin->force_redraw();
    }
}

/* ---- s145: the pause menu (Esc during play) ----
   Nothing moves while it is open: turns only pass on commands, and the
   real-time clock never ticks inside a menu.  Game commands are queued so
   they run exactly as typed; interface settings apply at once. */
static bool
vpause_menu(void *result)
{
    int winid, n, pick;
    anything any;
    menu_item *sel = NULL;
    char tm[96], title[64];
    static const struct {
        int id;
        char key;
        const char *txt;
    } items[] = { { 1, 'r', "Resume" },
                  { 2, 'o', "Game options" },
                  { 3, 'i', "Interface options" },
                  { 4, 't', NULL },
                  { 8, 'v', "Visual effects" },
                  { 5, 'h', "Help and keys" },
                  { 6, 's', "Save and quit" },
                  { 7, 'q', "Quit without saving" } };
    size_t i;

    vulture_sfx("ui_click");
    winid = vulture_create_nhwindow(NHW_MENU);
    vulture_start_menu(winid, MENU_BEHAVE_STANDARD);
    snprintf(tm, sizeof tm, "Time mode: %s (%s speed)", vrt_names[vrt_get()],
             vrt_speed_nm[vrt_speed < 0 ? 0 : vrt_speed]);
    for (i = 0; i < sizeof items / sizeof items[0]; i++) {
        any = cg.zeroany;
        any.a_int = items[i].id;
        vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, items[i].key, 0,
                         items[i].id == 1 ? ATR_BOLD : ATR_NONE, NO_COLOR,
                         items[i].txt ? items[i].txt : tm, MENU_ITEMFLAGS_NONE);
    }
    snprintf(title, sizeof title, "Paused  -  turn %ld", svm.moves);
    vulture_end_menu(winid, title);
    if (getenv("VULTURE_SELFTEST_CLICK")) /* probe: s145 */
        vpause_probe_t = SDL_GetTicks();
    n = vulture_select_menu(winid, PICK_ONE, &sel);
    vulture_destroy_nhwindow(winid);
    pick = (n > 0 && sel) ? sel[0].item.a_int : 1;
    if (sel)
        free((genericptr_t) sel);
    switch (pick) {
    case 2:
        cmdq_add_ec(CQ_CANNED, doset);
        break;
    case 3:
        vulture_iface_opts();
        return false;
    case 4:
        vrt_cycle();
        return false;
    case 8: /* s161 */
        vfx_menu();
        return false;
    case 5:
        cmdq_add_ec(CQ_CANNED, dohelp);
        break;
    case 6:
        cmdq_add_ec(CQ_CANNED, dosave);
        break;
    case 7:
        cmdq_add_ec(CQ_CANNED, done2);
        break;
    default:
        vulture_sfx("ui_cancel");
        return false;
    }
    vauto_result(result); /* a dummy command; the queued one runs next */
    return true;
}

/* the three buttons beside the ability bar (vbtn_rect is defined above, s166) */
static const char *const vbtn_name[4] = { "FIGHT", "A-MOVE", "REST", "EXPLORE" };
static const char *const vbtn_key[4] = { "Tab", "S-A", "F5", "" };

static int
vbtn_hit(int mx, int my)
{
    int i;

    for (i = 0; i < 4; i++)
        if (vbtn_rect[i].w && mx >= vbtn_rect[i].x && mx < vbtn_rect[i].x + vbtn_rect[i].w
            && my >= vbtn_rect[i].y && my < vbtn_rect[i].y + vbtn_rect[i].h)
            return i;
    return -1;
}

static void
vbtn_draw(void)
{
    int S = V_LISTITEM_HEIGHT, gap = V_UI(3), bh, bw = V_UI(96), i, hov;
    point mp = vulture_get_mouse_pos();
    Uint32 black = SDL_MapRGB(vulture_screen->format, 0, 0, 0);

    if (!vhb_rect.w)
        return;
    bh = (S - gap) / 2; /* s134: 2x2 - FIGHT / A-MOVE | REST / EXPLORE */
    for (i = 0; i < 4; i++) {
        vbtn_rect[i].x = vhb_rect.x + vhb_rect.w + V_UI(8) + (i / 2) * (bw + gap);
        vbtn_rect[i].y = vhb_rect.y + (i % 2) * (bh + gap);
        vbtn_rect[i].w = bw, vbtn_rect[i].h = bh;
    }
    hov = vbtn_hit(mp.x, mp.y);
    for (i = 0; i < 4; i++) {
        SDL_Rect rc = vbtn_rect[i];
        bool active = (i == 1 && (vamove_pending || vauto_mode == VAUTO_AMOVE))
                      || (i == 2 && vauto_mode == VAUTO_REST)
                      || (i == 3 && autoexplore_active);
        Uint32 edge = active ? SDL_MapRGB(vulture_screen->format, 120, 230, 140)
                      : i == hov ? SDL_MapRGB(vulture_screen->format, 255, 200, 70)
                      : SDL_MapRGB(vulture_screen->format, 90, 82, 66);

        vbox(rc.x, rc.y, rc.w, rc.h, 185);
        rc.w = 3;
        SDL_FillRect(vulture_screen, &rc, edge);
        vulture_put_text_shadow(V_FONT_SMALL, vbtn_name[i], vulture_screen,
                                vbtn_rect[i].x + V_UI(7), vbtn_rect[i].y + (bh - vulture_get_lineheight(V_FONT_SMALL)) / 2,
                                SDL_MapRGB(vulture_screen->format, 240, 232, 210), black);
        vulture_put_text_shadow(V_FONT_SMALL, vbtn_key[i], vulture_screen,
                                vbtn_rect[i].x + bw - V_UI(5) - vulture_text_length(V_FONT_SMALL, vbtn_key[i]),
                                vbtn_rect[i].y + (bh - vulture_get_lineheight(V_FONT_SMALL)) / 2,
                                SDL_MapRGB(vulture_screen->format, 150, 142, 120), black);
    }
}

/* ---- the "in sight" panel ---- */
static struct {
    int x, y, w, rowh, n;
    unsigned mid[8];
} vmp;
static unsigned vmp_hl = 0;

static int
vmp_hit(int mx, int my)
{
    int i;

    if (!vmp.n || mx < vmp.x || mx >= vmp.x + vmp.w)
        return -1;
    i = (my - (vmp.y + vmp.rowh)) / vmp.rowh;
    if (my < vmp.y + vmp.rowh || i < 0 || i >= vmp.n)
        return -1;
    return i;
}

static struct monst *
vmp_mon(unsigned mid)
{
    struct monst *m;

    for (m = fmon; m; m = m->nmon)
        if (m->m_id == mid && !DEADMONSTER(m) && canspotmon(m))
            return m;
    return 0;
}

static void
vmp_draw(int ax, int ay, int w, int h)
{
    struct monst *m, *list[64];
    int n = 0, i, j, lh = vulture_get_lineheight(V_FONT_SMALL), hov;
    point mp = vulture_get_mouse_pos();
    Uint32 black = SDL_MapRGB(vulture_screen->format, 0, 0, 0);

    vmp.n = 0;
    if (!u.ux || Hallucination || vulture_whatis_active)
        return;
    {
        bool others = false; /* s159: just your pet in view - no panel */

        for (m = fmon; m && n < 64; m = m->nmon)
            if (!DEADMONSTER(m) && canspotmon(m)) {
                list[n++] = m;
                if (!m->mtame)
                    others = true;
            }
        if (!n || !others)
            return;
    }
    /* hostiles nearest first, then peacefuls, then pets */
    for (i = 1; i < n; i++)
        for (j = i; j > 0; j--) {
            struct monst *a = list[j - 1], *b2 = list[j];
            int ka = a->mtame ? 2 : a->mpeaceful ? 1 : 0;
            int kb = b2->mtame ? 2 : b2->mpeaceful ? 1 : 0;

            if (ka > kb || (ka == kb && distmin(u.ux, u.uy, a->mx, a->my)
                                            > distmin(u.ux, u.uy, b2->mx, b2->my))) {
                list[j - 1] = b2, list[j] = a;
            } else {
                break;
            }
        }
    if (n > 8)
        n = 8;
    vmp.rowh = lh + 7;
    vmp.w = V_UI(200);
    vmp.x = ax + w - vmp.w - V_UI(10);
    vmp.y = ay + h * 11 / 100;
    if (levwin) /* s136: start below the minimap so it never covers a row */
        for (window *c = levwin->first_child; c; c = c->sib_next)
            if (c->visible && c->menu_id == V_WIN_MINIMAP && c->x < vmp.x + vmp.w
                && vmp.y < c->abs_y + c->h + 6)
                vmp.y = c->abs_y + c->h + 6;
    vmp.n = n;
    for (i = 0; i < n; i++)
        vmp.mid[i] = list[i]->m_id;
    hov = vmp_hit(mp.x, mp.y);
    vmp_hl = hov >= 0 ? vmp.mid[hov] : 0;
    vbox(vmp.x, vmp.y, vmp.w, vmp.rowh * (n + 1) + 4, 170);
    vulture_put_text_shadow(V_FONT_SMALL, "IN SIGHT", vulture_screen, vmp.x + V_UI(8), vmp.y + 4,
                            SDL_MapRGB(vulture_screen->format, 255, 200, 70), black);
    for (i = 0; i < n; i++) {
        Uint8 r, g, b;
        int ry = vmp.y + vmp.rowh * (i + 1) + 2;
        std::string nm;
        SDL_Rect rc;

        m = list[i];
        (void) vthreat(m, &r, &g, &b);
        if (!m->mtame && (m->msleeping || !m->mcanmove)) /* s137 */
            r = (Uint8) ((r + 140) / 2), g = (Uint8) ((g + 150) / 2), b = (Uint8) ((b + 170) / 2);
        if (i == hov)
            vtint(vmp.x + 2, ry - 2, vmp.w - 4, vmp.rowh, 255, 255, 255, 30);
        rc.x = vmp.x + V_UI(8), rc.y = ry + lh / 2 - 3, rc.w = 6, rc.h = 6;
        SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, r, g, b));
        nm = pmname(m->data, Mgender(m));
        if (!nm.empty())
            nm[0] = (char) toupper((unsigned char) nm[0]);
        if (has_mgivenname(m))
            nm = std::string(MGIVENNAME(m)) + " (" + nm + ")";
        { /* s136: Brogue-style - what it is doing, right-aligned */
            const char *st = NULL;
            Uint8 sr = 160, sg = 160, sb = 160;

            if (m->msleeping || !m->mcanmove)
                st = "asleep";
            else if (m->mtrapped)
                st = "trapped", sr = 200, sg = 170, sb = 120;
            else if (m->mflee)
                st = "fleeing", sr = 230, sg = 210, sb = 110;
            else if (!m->mtame && !m->mpeaceful)
                st = m_canseeu(m) ? "hunting" : "wandering",
                sr = m_canseeu(m) ? 255 : 170, sg = m_canseeu(m) ? 110 : 170,
                sb = m_canseeu(m) ? 90 : 170;
            if (st) {
                int sw = vulture_text_length(V_FONT_SMALL, st), room;

                room = vmp.w - V_UI(28) - sw - 8;
                while (nm.size() > 3 && vulture_text_length(V_FONT_SMALL, nm) > room)
                    nm.resize(nm.size() - 1);
                vulture_put_text_shadow(V_FONT_SMALL, st, vulture_screen,
                                        vmp.x + vmp.w - V_UI(8) - sw, ry,
                                        SDL_MapRGB(vulture_screen->format, sr, sg, sb), black);
            }
        }
        vulture_put_text_shadow(V_FONT_SMALL, nm, vulture_screen, vmp.x + V_UI(20), ry,
                                SDL_MapRGB(vulture_screen->format, 235, 230, 220), black);
        if ((m->mtame || m->mhp < m->mhpmax) && m->mhpmax > 0) {
            int full = vmp.w - V_UI(28), fill = full * m->mhp / m->mhpmax;
            int pct = 100 * m->mhp / m->mhpmax;

            rc.x = vmp.x + V_UI(20), rc.y = ry + lh, rc.w = full, rc.h = 2;
            SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, 50, 25, 25));
            rc.w = fill < 0 ? 0 : fill;
            SDL_FillRect(vulture_screen, &rc,
                         pct > 60 ? SDL_MapRGB(vulture_screen->format, 90, 210, 90)
                         : pct > 30 ? SDL_MapRGB(vulture_screen->format, 230, 190, 60)
                         : SDL_MapRGB(vulture_screen->format, 220, 60, 50));
        }
    }
}

/* the hovered panel row's monster, marked on the map */
static void
vmp_mark(int cx, int cy, int vx, int vy)
{
    struct monst *m;
    int sx, sy;

    if (!vmp_hl || !(m = vmp_mon(vmp_hl)))
        return;
    vfx_square_screen(m->mx, m->my, cx, cy, vx, vy, &sx, &sy);
    vfx_ringc(sx, sy, 16 + 2 * sin(SDL_GetTicks() / 150.0), 255, 200, 70, 230);
}

/*
 * IMPLEMENTATION_LOG s133: dynamic lighting.
 * Every square gets a light level: lit rooms are bright, dark ground you
 * can see is dim, remembered ground a little dim; then each light source
 * the hero can see (lamps, candles, glowing monsters, Sunsword...), lava,
 * the hero's own faint glow and short-lived flashes (zaps, explosions, big
 * hits) add soft pools of coloured light that respect walls (clear_path)
 * and flicker.  The level is sampled at 1/4 resolution in MAP space
 * (bilinear between square centres, so pools are smooth, not square),
 * stretched up and laid over the map as a darkness + tint layer, under
 * the glows, markers and UI.  F9 toggles it.
 */
static bool vl_on = true;
static bool vl_relief_on2 = true; /* s149: mirrors vl_relief_on for vnm_begin */
static bool vnm_ok = false;        /* s149: the buffer holds this frame's normals */
static bool vl_magic = false;      /* s149: pulsing magic in view: refresh faster */

/* s149: start a frame's normal buffer (cleared, screen-sized) */
static void
vnm_begin(void)
{
    vnm_ok = false;
    vulture_normal_capture = false;
    if (!vl_on || !vl_relief_on2 || !vulture_screen)
        return;
    if (!vulture_normal_buf || vulture_normal_buf->w != vulture_screen->w
        || vulture_normal_buf->h != vulture_screen->h) {
        if (vulture_normal_buf)
            SDL_FreeSurface(vulture_normal_buf);
        vulture_normal_buf = SDL_CreateRGBSurfaceWithFormat(0, vulture_screen->w, vulture_screen->h,
                                                            32, SDL_PIXELFORMAT_ARGB8888);
        if (!vulture_normal_buf)
            return;
        SDL_SetSurfaceBlendMode(vulture_normal_buf, SDL_BLENDMODE_NONE);
    }
    if (levwin) { /* s152: only the map's part */
        SDL_Rect mr = { (Sint16) levwin->abs_x, (Sint16) levwin->abs_y, (Uint16) levwin->w,
                        (Uint16) levwin->h };

        SDL_FillRect(vulture_normal_buf, &mr, 0);
    } else
        SDL_FillRect(vulture_normal_buf, NULL, 0);
    vulture_normal_capture = true;
    vnm_ok = true;
}
static float vl_L[ROWNO][COLNO], vl_R[ROWNO][COLNO], vl_G[ROWNO][COLNO], vl_B[ROWNO][COLNO];
static SDL_Surface *vl_low = NULL, *vl_full = NULL;
struct vl_flash_t {
    int x, y;
    Uint32 t0, dur;
    float r, g, b, inten, rad;
};
static std::vector<vl_flash_t> vl_flashes;

static void
vl_flash(int x, int y, float r, float g, float b, float inten, float rad, Uint32 dur)
{
    vl_flash_t f;

    f.x = x, f.y = y, f.t0 = SDL_GetTicks(), f.dur = dur;
    f.r = r, f.g = g, f.b = b, f.inten = inten, f.rad = rad;
    if (vl_flashes.size() < 32)
        vl_flashes.push_back(f);
}

static void
vl_add(int cx, int cy, double radius, double inten, double r, double g, double b)
{
    int x, y, R = (int) radius + 1;

    for (y = cy - R; y <= cy + R; y++)
        for (x = cx - R; x <= cx + R; x++) {
            double d, k, f;

            if (!isok(x, y) || !levl[x][y].seenv) /* light only lands on known ground */
                continue;
            d = sqrt((double) ((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            k = 1.0 - d / (radius + 1.0);
            if (k <= 0)
                continue;
            if ((x != cx || y != cy) && !clear_path(cx, cy, x, y))
                continue;
            f = inten * k * k;
            vl_L[y][x] += (float) f;
            vl_R[y][x] += (float) (f * r);
            vl_G[y][x] += (float) (f * g);
            vl_B[y][x] += (float) (f * b);
        }
}

/* s149: the colour of a magical trap's light right now (0 = not magical);
   *pu gets its pulse */
static bool
vmagic_light(int tt, int x, int y, double t, double *c, double *pu)
{
    double h = 0.5 + 0.5 * sin(t * 1.3 + x * 0.9 + y * 1.1); /* colour drift */

    *pu = 0.5 + 0.5 * sin(t * 3.0 + x + y);
    switch (tt) {
    case MAGIC_PORTAL: /* violet <-> cyan */
        c[0] = 0.80 - 0.45 * h, c[1] = 0.40 + 0.45 * h, c[2] = 1.0;
        return true;
    case TELEP_TRAP: /* blue-violet */
        c[0] = 0.55 - 0.15 * h, c[1] = 0.50 + 0.25 * h, c[2] = 1.0;
        return true;
    case LEVEL_TELEP: /* deep violet */
        c[0] = 0.75, c[1] = 0.30 + 0.15 * h, c[2] = 1.0;
        return true;
    case MAGIC_TRAP: /* wandering hue */
        c[0] = 0.5 + 0.5 * sin(t * 1.7 + x), c[1] = 0.5 + 0.5 * sin(t * 1.7 + x + 2.1),
        c[2] = 0.5 + 0.5 * sin(t * 1.7 + x + 4.2);
        return true;
    case POLY_TRAP: /* green <-> magenta */
        c[0] = 0.35 + 0.6 * h, c[1] = 0.95 - 0.6 * h, c[2] = 0.55 + 0.4 * h;
        return true;
    case ANTI_MAGIC: /* a cold, dim grey-blue */
        c[0] = 0.45, c[1] = 0.55, c[2] = 0.75 + 0.2 * h;
        return true;
    }
    return false;
}

static void
vl_compute(mapdata *md)
{
    vl_magic = false; /* s149 */
    int x, y;
    double t = SDL_GetTicks() / 1000.0;
    light_source *ls;
    Uint32 now = SDL_GetTicks();
    size_t i, n = 0;

    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++) {
            float base;

            if (!levl[x][y].seenv)
                base = 0.0f;
            else if (cansee(x, y))
                base = levl[x][y].lit ? 0.80f : 0.38f; /* s152: 0.93 left lights nothing to do */
            else
                base = 0.50f; /* remembered, out of sight */
            if (base > 0.0f && !IS_STWALL(levl[x][y].typ)) { /* s143: contact shadows */
                int nw = 0;

                nw += (isok(x - 1, y) && IS_STWALL(levl[x - 1][y].typ));
                nw += (isok(x + 1, y) && IS_STWALL(levl[x + 1][y].typ));
                nw += (isok(x, y - 1) && IS_STWALL(levl[x][y - 1].typ));
                nw += (isok(x, y + 1) && IS_STWALL(levl[x][y + 1].typ));
                base *= 1.0f - 0.055f * (float) nw;
            }
            vl_L[y][x] = base;
            vl_R[y][x] = vl_G[y][x] = vl_B[y][x] = base;
        }
    for (ls = gl.light_base; ls; ls = ls->next) {
        double fl;

        if (!isok(ls->x, ls->y) || ls->range <= 0)
            continue;
        if (!cansee(ls->x, ls->y) && !u_at(ls->x, ls->y))
            continue;
        /* flames flicker: two sines out of step plus a little jitter */
        fl = 0.86 + 0.08 * sin(t * 9.0 + ls->x * 1.7 + ls->y * 2.3)
             + 0.05 * sin(t * 23.0 + ls->x) + 0.02 * ((rand() % 100) / 100.0);
        if (ls->type == LS_OBJECT)
            vl_add(ls->x, ls->y, ls->range + 0.8, 0.85 * fl, 1.0, 0.72, 0.42);
        else
            vl_add(ls->x, ls->y, ls->range + 0.8, 0.75 * fl, 1.0, 0.9, 0.65);
    }
    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++) {
            int g;

            if (!cansee(x, y))
                continue;
            g = glyph_at(x, y);
            if (glyph_is_cmap(g) && (glyph_to_cmap(g) == S_lava || glyph_to_cmap(g) == S_lavawall))
                vl_add(x, y, 2.2, 0.45 + 0.08 * sin(t * 2.5 + x * 0.9 + y * 1.3), 1.0, 0.42, 0.12);
            if (md && md->get_glyph(MAP_SPECIAL, x, y) != V_TILE_NONE) /* zap / explosion */
                vl_add(x, y, 2.5, 0.9, 0.85, 0.9, 1.0);
            if (glyph_is_cmap(g)) { /* s143: features give off light */
                int c = glyph_to_cmap(g);
                double sh = sin(t * 1.7 + x * 1.3 + y * 0.7);

                if (c == S_fountain)
                    vl_add(x, y, 2.4, 0.30 + 0.05 * sh, 0.45, 0.75, 1.0);
                else if (c == S_pool || c == S_water)
                    vl_add(x, y, 1.2, 0.10 + 0.04 * sin(t * 2.3 + x * 2.1 + y * 1.7), 0.4, 0.65, 1.0);
                else if (c == S_altar) {
                    int al = levl[x][y].altarmask & AM_MASK;

                    if (al == AM_LAWFUL)
                        vl_add(x, y, 2.6, 0.32 + 0.04 * sh, 1.0, 0.92, 0.62);
                    else if (al == AM_NEUTRAL)
                        vl_add(x, y, 2.6, 0.28 + 0.04 * sh, 0.82, 0.88, 1.0);
                    else if (al == AM_CHAOTIC)
                        vl_add(x, y, 2.6, 0.30 + 0.05 * sh, 1.0, 0.35, 0.3);
                    else
                        vl_add(x, y, 2.6, 0.26, 0.6, 0.3, 0.9);
                } else if (c == S_throne)
                    vl_add(x, y, 2.4, 0.30 + 0.03 * sh, 1.0, 0.8, 0.35);
                else if (c == S_sink)
                    vl_add(x, y, 1.4, 0.12, 0.7, 0.8, 0.9);
                else if (c == S_grave)
                    vl_add(x, y, 1.3, 0.08 + 0.03 * sh, 0.55, 0.9, 0.6);
            } else if (glyph_is_trap(g)) {
                int tt = glyph_to_trap(g);
                double pu = 0.5 + 0.5 * sin(t * 3.0 + x + y);

                double vc[3];

                /* s149: magic breathes - three beats at once plus a rare
                   surge, and its colour drifts, so no two seconds look alike */
                if (vmagic_light(tt, x, y, t, vc, &pu)) {
                    double s1 = sin(t * 2.1 + x * 1.3), s2 = sin(t * 5.3 + y * 1.7),
                           s3 = sin(t * 12.7 + x * 3.1 + y), sg = sin(t * 0.83 + x * 7.1 + y * 3.3);
                    double surge = sg > 0 ? pow(sg, 14.0) : 0.0;
                    double base = tt == MAGIC_PORTAL ? 0.55 : 0.30;
                    double I = base * (1.0 + 0.30 * s1 + 0.16 * s2 + 0.08 * s3) + 0.55 * surge;
                    double R = (tt == MAGIC_PORTAL ? 3.3 : 2.2) * (1.0 + 0.12 * s1 + 0.25 * surge);

                    vl_add(x, y, R, I, vc[0], vc[1], vc[2]);
                    vl_magic = true;
                } else if (tt == FIRE_TRAP)
                    vl_add(x, y, 1.6, 0.16 + 0.06 * pu, 1.0, 0.5, 0.2);
            }
        }
    { /* s143: fiery, electric and icy creatures light their surroundings */
        struct monst *gm2;

        for (gm2 = fmon; gm2; gm2 = gm2->nmon) {
            int k, fire = 0, elec = 0, cold = 0;

            if (DEADMONSTER(gm2) || !canseemon(gm2))
                continue;
            for (k = 0; k < NATTK; k++) {
                const struct attack *a = &gm2->data->mattk[k];
                bool aura = a->aatyp == AT_NONE || a->aatyp == AT_ENGL || a->aatyp == AT_EXPL
                            || a->aatyp == AT_BOOM || gm2->data->mlet == S_ELEMENTAL;

                if (!aura)
                    continue;
                fire |= a->adtyp == AD_FIRE;
                elec |= a->adtyp == AD_ELEC;
                cold |= a->adtyp == AD_COLD;
            }
            if (fire)
                vl_add(gm2->mx, gm2->my, 2.6,
                       0.45 + 0.1 * sin(t * 11.0 + gm2->m_id) + 0.04 * ((rand() % 100) / 100.0),
                       1.0, 0.55, 0.2);
            else if (elec)
                vl_add(gm2->mx, gm2->my, 2.4, (rand() % 6) ? 0.35 : 0.75, 0.7, 0.8, 1.0);
            else if (cold)
                vl_add(gm2->mx, gm2->my, 2.0, 0.28, 0.6, 0.85, 1.0);
        }
    }
    if (u.ux)
    { /* s152: you carry a warm pool of light - surroundings catch it as you move */
        double fl = 0.94 + 0.04 * sin(t * 7.0) + 0.02 * sin(t * 17.0);

        vl_add(u.ux, u.uy, 1.6, 0.30, 1.0, 0.95, 0.85); /* you can always see yourself */
        vl_add(u.ux, u.uy, 3.6, 0.32 * fl, 1.0, 0.82, 0.58);
    }
    for (i = 0; i < vl_flashes.size(); i++) {
        vl_flash_t &f = vl_flashes[i];
        double age = (double) (now - f.t0) / f.dur;

        if (age >= 1.0)
            continue;
        vl_add(f.x, f.y, f.rad, f.inten * (1.0 - age) * (1.0 - age), f.r, f.g, f.b);
        vl_flashes[n++] = f;
    }
    vl_flashes.resize(n);
}

/* bilinear light at fractional map position */
static void
vl_sample(double mx, double my, float *L, float *r, float *g, float *b)
{
    int x0 = (int) floor(mx), y0 = (int) floor(my), k;
    double fx = mx - x0, fy = my - y0;
    float acc[4] = { 0, 0, 0, 0 };

    for (k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        double w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);

        if (x < 1 || x >= COLNO || y < 0 || y >= ROWNO)
            continue;
        acc[0] += (float) (w * vl_L[y][x]);
        acc[1] += (float) (w * vl_R[y][x]);
        acc[2] += (float) (w * vl_G[y][x]);
        acc[3] += (float) (w * vl_B[y][x]);
    }
    *L = acc[0], *r = acc[1], *g = acc[2], *b = acc[3];
}

/*
 * IMPLEMENTATION_LOG s145: surface relief (normal-mapped lighting).
 * Each frame the textures under the light get a normal map made from
 * themselves: brightness is read as height (mortar and cracks low, stone
 * faces high), its slope gives a surface normal per pixel, and that normal
 * is lit from the direction the light actually comes from - the slope of
 * the light field vl_render already samples on its coarse grid, which points
 * at the lamps, fires and glows nearby.  So stonework catches the light on
 * the side facing your torch and falls into shadow on the far side, and a
 * fire elemental rakes the floor around it.  Where the light is even (a lit
 * room, away from any source) a faint top-left key keeps a little depth.
 * Full-resolution, integer-only, on the map pixels before the darkness
 * layer; F9 cycles full / flat / off.
 */
static bool vl_relief_on = true;
static std::vector<short> vr_dx, vr_dy; /* light direction x strength, per cell */
static std::vector<short> vr_lv;        /* s149: light level 0..256, per cell */
static int vr_lw = 0, vr_lh = 0;
static Uint32 vr_last_ms = 0; /* probe: cost of the last pass */

static void
vl_relief_set(bool on)
{
    vl_relief_on = vl_relief_on2 = on;
}

static bool
vl_relief_get(void) /* s161 */
{
    return vl_relief_on;
}

static void
vl_relief_report(FILE *f) /* probe */
{
    fprintf(f, "relief_ms=%u on=%d grid=%dx%d\n", (unsigned) vr_last_ms, (int) vl_relief_on,
            vr_lw, vr_lh);
}

static std::vector<int> vr_cmin, vr_cmax; /* lit cells per grid row (-1: none) */

/* s167: run fn over [y0, y1) in horizontal bands, one per core (up to 6) -
   the light passes treat every row on its own, so the bands never touch */
template <typename Fn>
static void
vpar_rows(int y0, int y1, Fn fn)
{
    static int ncpu = 0;
    int n, k, step;

    if (!ncpu) {
        ncpu = (int) std::thread::hardware_concurrency();
        if (getenv("VULTURE_THREADS"))
            ncpu = atoi(getenv("VULTURE_THREADS"));
        ncpu = ncpu < 1 ? 1 : ncpu > 6 ? 6 : ncpu;
    }
    n = ncpu;
    if (y1 - y0 < 64 || n == 1) {
        fn(y0, y1);
        return;
    }
    step = (y1 - y0 + n - 1) / n;
    {
        std::vector<std::thread> pool;

        for (k = 1; k < n; k++) {
            int a = y0 + k * step, z = a + step > y1 ? y1 : a + step;

            if (a < z)
                pool.emplace_back(fn, a, z);
        }
        fn(y0, y0 + step > y1 ? y1 : y0 + step); /* this thread takes the first band */
        for (k = 0; k < (int) pool.size(); k++)
            pool[k].join();
    }
}

/* s149: light each pixel from its normal-map normal (vulture_normal_buf),
   facing the light direction of its coarse cell */
static void
vl_nmap(int x0, int y0, int x1, int y1, int gx0, int gy0, int FS)
{
    SDL_Surface *s = vulture_screen, *nb = vulture_normal_buf;
    int x, y, rs = s->format->Rshift, gs = s->format->Gshift, bs = s->format->Bshift;
    Uint32 mask = ~(((Uint32) 255 << rs) | ((Uint32) 255 << gs) | ((Uint32) 255 << bs));

    for (y = y0; y < y1; y++) {
        int cj = (y - gy0) / FS, ci, ca, cb;
        Uint32 *row, *nrow;

        if (y < gy0 || cj >= vr_lh || vr_cmin[cj] < 0)
            continue;
        row = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch);
        nrow = (Uint32 *) ((Uint8 *) nb->pixels + (size_t) y * nb->pitch);
        ca = vr_cmin[cj], cb = vr_cmax[cj];
        for (ci = ca; ci <= cb; ci++) {
            int k = cj * vr_lw + ci, dx = vr_dx[k], dy = vr_dy[k], lv = vr_lv[k], xs, xe;

            if (!dx && !dy)
                continue;
            xs = gx0 + ci * FS, xe = xs + FS;
            if (xs < x0)
                xs = x0;
            if (xe > x1)
                xe = x1;
            for (x = xs; x < xe; x++) {
                Uint32 pn = nrow[x], p;
                int a = (int) (pn >> 24), nx, ny, nz, d, r, g, b2, m, sp = 0;

                if (a < 24)
                    continue;
                nx = (int) ((pn >> 16) & 255) - 128;
                ny = (int) ((pn >> 8) & 255) - 128;
                nz = (int) (pn & 255) - 128;
                /* lit where the normal faces the light: n . L */
                d = ((nx * dx + ny * dy) * 3) >> 9; /* s152: 1.5x */
                /* crevices, near a real light: faces tilted away take a
                   little less of it */
                d -= ((127 - nz) * lv) >> 9;
                /* a soft glint where a nearby light hits a face squarely */
                if (d > 30 && lv > 64)
                    sp = ((d - 30) * (d - 30) * lv) >> 16;
                d = d * a >> 8;
                if (!d && !sp)
                    continue;
                if (d > 80)
                    d = 80;
                else if (d < -80)
                    d = -80;
                if (sp > 32)
                    sp = 32;
                p = row[x], m = 256 + d;
                r = ((int) (((p >> rs) & 255) * m) >> 8) + sp;
                g = ((int) (((p >> gs) & 255) * m) >> 8) + sp;
                b2 = ((int) (((p >> bs) & 255) * m) >> 8) + sp * 3 / 4;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b2 > 255) b2 = 255;
                row[x] = (p & mask) | ((Uint32) r << rs) | ((Uint32) g << gs) | ((Uint32) b2 << bs);
            }
        }
    }
}

static void
vl_relief(int ax, int ay, int w, int h, int gx0, int gy0, int FS)
{
    SDL_Surface *s = vulture_screen;
    static std::vector<Uint8> lbuf[3];
    int lrow_y[3], lrow_a[3] = { 0, 0, 0 }, lrow_b[3] = { 0, 0, 0 };
    static std::vector<Uint32> ob[2];
    int ob_y[2] = { -1, -1 }, ob_a[2] = { 0, 0 }, ob_b[2] = { 0, 0 };
    Uint32 t0 = SDL_GetTicks(), mask;
    int x, y, x0, y0, x1, y1, W, rs, gs, bs, sl;

    if (!vl_relief_on || !s || s->format->BytesPerPixel != 4 || !vr_lw || !vr_lh)
        return;
    x0 = ax < 1 ? 1 : ax, y0 = ay < 1 ? 1 : ay;
    x1 = ax + w > s->w - 1 ? s->w - 1 : ax + w;
    y1 = ay + h > s->h - 1 ? s->h - 1 : ay + h;
    if (x1 - x0 < 8 || y1 - y0 < 8)
        return;
    if (vnm_ok && vulture_normal_buf && vulture_normal_buf->w == s->w
        && vulture_normal_buf->h == s->h) { /* s149: real normal maps */
        if (SDL_MUSTLOCK(s))
            SDL_LockSurface(s);
        vpar_rows(y0, y1, [&](int a, int z) { vl_nmap(x0, a, x1, z, gx0, gy0, FS); }); /* s167 */
        if (SDL_MUSTLOCK(s))
            SDL_UnlockSurface(s);
        vr_last_ms = SDL_GetTicks() - t0;
        return;
    }
    W = s->w;
    rs = s->format->Rshift, gs = s->format->Gshift, bs = s->format->Bshift;
    mask = ~(((Uint32) 255 << rs) | ((Uint32) 255 << gs) | ((Uint32) 255 << bs));
    lbuf[0].resize(W), lbuf[1].resize(W), lbuf[2].resize(W);
    ob[0].resize(W), ob[1].resize(W);
    if (SDL_MUSTLOCK(s))
        SDL_LockSurface(s);
#define VR_PX(yy) ((Uint32 *) ((Uint8 *) s->pixels + (size_t) (yy) * s->pitch))
#define VR_LUM(p) ((Uint8) (((((p) >> rs) & 255) * 2 + (((p) >> gs) & 255) * 5 \
                             + (((p) >> bs) & 255)) >> 3))
#define VR_FLUSH(k)                                                                     \
    if (ob_y[k] >= 0) {                                                                 \
        memcpy(VR_PX(ob_y[k]) + ob_a[k], &ob[k][ob_a[k]],                               \
               sizeof(Uint32) * (size_t) (ob_b[k] - ob_a[k]));                          \
        ob_y[k] = -1;                                                                   \
    }
    /* each row is written back only after the row below it has read it, so
       heights always come from the untouched picture */
    /* height rows: 3 slots, each remembering which row and columns it holds */
    for (x = 0; x < 3; x++)
        lrow_y[x] = -99;
    auto lrow = [&](int r, int a, int bb) -> Uint8 * {
        int k2, best = 0, xx;
        Uint32 *px = VR_PX(r);
        Uint8 *L;

        for (k2 = 0; k2 < 3; k2++)
            if (lrow_y[k2] == r)
                break;
        if (k2 == 3) { /* rows only go down: reuse the oldest slot */
            for (k2 = 1; k2 < 3; k2++)
                if (lrow_y[k2] < lrow_y[best])
                    best = k2;
            k2 = best;
            lrow_y[k2] = r, lrow_a[k2] = a, lrow_b[k2] = a - 1;
        }
        L = &lbuf[k2][0];
        if (a < lrow_a[k2]) {
            for (xx = a; xx < lrow_a[k2]; xx++)
                L[xx] = VR_LUM(px[xx]);
            lrow_a[k2] = a;
        }
        if (bb > lrow_b[k2]) {
            for (xx = lrow_b[k2] + 1 > a ? lrow_b[k2] + 1 : a; xx <= bb; xx++)
                L[xx] = VR_LUM(px[xx]);
            lrow_b[k2] = bb;
        }
        return L;
    };
    for (y = y0; y < y1; y++) {
        int cj = (y - gy0) / FS, xa, xb, ci, ca, cb;
        Uint32 *rc;

        sl = y & 1;
        VR_FLUSH(sl);
        if (y < gy0 || cj >= vr_lh || vr_cmin[cj] < 0)
            continue;
        ca = vr_cmin[cj], cb = vr_cmax[cj];
        xa = gx0 + ca * FS, xb = gx0 + (cb + 1) * FS;
        if (xa < x0)
            xa = x0;
        if (xb > x1)
            xb = x1;
        if (xb - xa < 2)
            continue;
        /* s147: each row's heights are worked out once and reused by the
           two rows that need them next (was three rows per row) */
        rc = VR_PX(y);
        Uint8 *lp = lrow(y - 1, xa - 1, xb), *lc = lrow(y, xa - 1, xb),
              *ln = lrow(y + 1, xa - 1, xb);
        memcpy(&ob[sl][xa], rc + xa, sizeof(Uint32) * (size_t) (xb - xa));
        ob_y[sl] = y, ob_a[sl] = xa, ob_b[sl] = xb;
        for (ci = ca; ci <= cb; ci++) { /* a cell's run of pixels at a time */
            int k = cj * vr_lw + ci, dx = vr_dx[k], dy = vr_dy[k], xs, xe;

            if (!dx && !dy)
                continue;
            xs = gx0 + ci * FS, xe = xs + FS;
            if (xs < xa)
                xs = xa;
            if (xe > xb)
                xe = xb;
            for (x = xs; x < xe; x++) {
                int hx = lc[x + 1] - lc[x - 1], hy = ln[x] - lp[x], d, r, g, bb, m;
                Uint32 p;

                /* lit side: the slope faces the light when -grad(h) . light > 0 */
                d = -(hx * dx + hy * dy) >> 8;
                if (!d)
                    continue;
                if (d > 70)
                    d = 70;
                else if (d < -80)
                    d = -80;
                p = ob[sl][x], m = 256 + d;
                r = (int) (((p >> rs) & 255) * m) >> 8;
                g = (int) (((p >> gs) & 255) * m) >> 8;
                bb = (int) (((p >> bs) & 255) * m) >> 8;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (bb > 255) bb = 255;
                ob[sl][x] = (p & mask) | ((Uint32) r << rs) | ((Uint32) g << gs)
                            | ((Uint32) bb << bs);
            }
        }
    }
    VR_FLUSH(0);
    VR_FLUSH(1);
#undef VR_FLUSH
#undef VR_PX
#undef VR_LUM
    if (SDL_MUSTLOCK(s))
        SDL_UnlockSurface(s);
    vr_last_ms = SDL_GetTicks() - t0;
}

/* the light's direction and strength per coarse cell, from the sampled
   light levels Lg (lw x lh): towards brighter, stronger where it falls off
   faster (near a source), plus a faint top-left key where it's lit */
static void
vl_relief_dirs(const std::vector<float> &Lg, int lw, int lh)
{
    int i, j;

    vr_lw = lw, vr_lh = lh;
    vr_dx.assign((size_t) lw * lh, 0);
    vr_dy.assign((size_t) lw * lh, 0);
    vr_lv.assign((size_t) lw * lh, 0); /* s149 */
    vr_cmin.assign((size_t) lh, -1);
    vr_cmax.assign((size_t) lh, -1);
    for (j = 1; j < lh - 1; j++)
        for (i = 1; i < lw - 1; i++) {
            float L = Lg[(size_t) j * lw + i], gx, gy, mag, st, key;

            if (L < 0.08f)
                continue; /* too dark to show any relief */
            gx = Lg[(size_t) j * lw + i + 1] - Lg[(size_t) j * lw + i - 1];
            gy = Lg[(size_t) (j + 1) * lw + i] - Lg[(size_t) (j - 1) * lw + i];
            mag = sqrtf(gx * gx + gy * gy);
            st = mag * 7.0f; /* near a lamp: full strength (s147: 8 px cells) */
            if (st > 1.0f)
                st = 1.0f;
            /* s149b: remembered ground (L 0.5, not in sight) and anything
               not near a real light gets no relief - lighting it drew a
               bright grid over every unseen square */
            if (L < 0.6f && st < 0.15f)
                continue;
            if (L < 1.0f)
                st *= 0.4f + 0.6f * L;
            key = 0.36f * (L > 1.0f ? 1.0f : L); /* s149b: 0.45 too much; s152: 0.30 too little */
            {
                float ux = mag > 1e-5f ? gx / mag : 0, uy = mag > 1e-5f ? gy / mag : 0;
                float lx = ux * st - 0.6f * key, ly = uy * st - 0.8f * key;

                vr_dx[(size_t) j * lw + i] = (short) (lx * 256.0f);
                vr_dy[(size_t) j * lw + i] = (short) (ly * 256.0f);
                /* lv now carries how much of it is a real nearby light
                   (glints and crevices follow that, not the room's key) */
                vr_lv[(size_t) j * lw + i] = (short) (256.0f * st);
                if (vr_cmin[j] < 0)
                    vr_cmin[j] = i;
                vr_cmax[j] = i;
            }
        }
}

/* s152: blend only the part of the (margin-padded) light layer that covers
   the map - the margins are there for sliding, not for drawing */
static void
vl_blit_visible(SDL_Surface *layer, int dx, int dy, int mx, int my, int mw, int mh)
{
    SDL_Rect src, dst;
    int x0 = dx > mx ? dx : mx, y0 = dy > my ? dy : my;
    int x1 = dx + layer->w < mx + mw ? dx + layer->w : mx + mw;
    int y1 = dy + layer->h < my + mh ? dy + layer->h : my + mh;

    if (x1 <= x0 || y1 <= y0)
        return;
    src.x = x0 - dx, src.y = y0 - dy, src.w = x1 - x0, src.h = y1 - y0;
    dst.x = x0, dst.y = y0, dst.w = src.w, dst.h = src.h;
    SDL_BlitSurface(layer, &src, vulture_screen, &dst);
}

/*
 * IMPLEMENTATION_LOG s153: the light layer applied straight from its grid.
 * The coarse grid (vl_low: A = darkness, RGB = tint, one sample every FS px
 * from (ox, oy)) used to be stretched to a full-screen surface (~7 ms per
 * rebuild at 1080p) and alpha-blended by SDL over every pixel (~6 ms per
 * frame).  Now each map row interpolates the grid vertically once, then
 * walks each cell with running increments: out = d + (tint - d) * A.
 * Pure black pixels (the void, unlit walls' backs) are skipped - often half
 * the screen.  Same result as the old stretch + blend.
 */
static void
vl_apply_band(SDL_Surface *low, int mx, int my, int mw, int mh, int ox, int oy, int FS, int by0,
              int by1)
{
    SDL_Surface *s = vulture_screen;
    std::vector<int> rA, rR, rG, rB; /* s167: per thread */
    int lw = low->w, lh = low->h, x, y, x0, y0, x1, y1, rs, gs, bs;
    Uint32 mask;

    if (!s || s->format->BytesPerPixel != 4)
        return;
    x0 = mx < 0 ? 0 : mx, y0 = my < 0 ? 0 : my;
    x1 = mx + mw > s->w ? s->w : mx + mw;
    y1 = my + mh > s->h ? s->h : my + mh;
    if (y0 < by0)
        y0 = by0;
    if (y1 > by1)
        y1 = by1;
    if (x1 <= x0 || y1 <= y0)
        return;
    rs = s->format->Rshift, gs = s->format->Gshift, bs = s->format->Bshift;
    mask = ~(((Uint32) 255 << rs) | ((Uint32) 255 << gs) | ((Uint32) 255 << bs));
    rA.resize(lw), rR.resize(lw), rG.resize(lw), rB.resize(lw);
    for (y = y0; y < y1; y++) {
        int fy = y - oy, j = fy >= 0 ? fy / FS : -1, t = fy >= 0 ? ((fy % FS) << 8) / FS : 0, i;
        Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch);
        const Uint32 *g0, *g1;

        if (j < 0)
            j = 0, t = 0;
        if (j >= lh - 1)
            j = lh - 2, t = 256;
        g0 = (const Uint32 *) ((Uint8 *) low->pixels + (size_t) j * low->pitch);
        g1 = (const Uint32 *) ((Uint8 *) low->pixels + (size_t) (j + 1) * low->pitch);
        for (i = 0; i < lw; i++) { /* this row's samples, 8.8 fixed point */
            Uint32 a = g0[i], b = g1[i];
            int aa = (int) (a >> 24), ba = (int) (b >> 24);

            rA[i] = (aa << 8) + (ba - aa) * t;
            rR[i] = ((int) ((a >> 16) & 255) << 8) + ((int) ((b >> 16) & 255) - (int) ((a >> 16) & 255)) * t;
            rG[i] = ((int) ((a >> 8) & 255) << 8) + ((int) ((b >> 8) & 255) - (int) ((a >> 8) & 255)) * t;
            rB[i] = ((int) (a & 255) << 8) + ((int) (b & 255) - (int) (a & 255)) * t;
        }
        x = x0;
        while (x < x1) {
            int fx = x - ox, ci = fx >= 0 ? fx / FS : 0, off = fx >= 0 ? fx % FS : 0, xe, n;
            int A, R, G, B, dA, dR, dG, dB;

            if (ci >= lw - 1)
                ci = lw - 2, off = FS;
            A = rA[ci], R = rR[ci], G = rG[ci], B = rB[ci];
            dA = (rA[ci + 1] - A) / FS, dR = (rR[ci + 1] - R) / FS;
            dG = (rG[ci + 1] - G) / FS, dB = (rB[ci + 1] - B) / FS;
            A += dA * off, R += dR * off, G += dG * off, B += dB * off;
            n = FS - off;
            if (n <= 0)
                n = 1;
            xe = x + n > x1 ? x1 : x + n;
            if (rs == 16 && gs == 8 && bs == 0) {
                /* s153: the usual XRGB layout - blend red+blue in one
                   multiply and green in another; the tint is held for the
                   cell (it changes slowly), only the darkness slides */
                Uint32 trb = ((Uint32) ((R + dR * (n / 2)) >> 8) << 16) | (Uint32) ((B + dB * (n / 2)) >> 8);
                Uint32 tg = (Uint32) ((G + dG * (n / 2)) >> 8) << 8;

                trb &= 0x00FF00FFu, tg &= 0x0000FF00u;
                for (; x < xe; x++, A += dA) {
                    Uint32 p = row[x], a8 = (Uint32) (A > 0 ? A >> 8 : 0), ia;

                    if (!(p & 0x00FFFFFFu) || !a8)
                        continue; /* black stays black; nothing to shade */
                    ia = 256 - a8;
                    row[x] = (p & 0xFF000000u)
                             | ((((p & 0x00FF00FFu) * ia + trb * a8) >> 8) & 0x00FF00FFu)
                             | ((((p & 0x0000FF00u) * ia + tg * a8) >> 8) & 0x0000FF00u);
                }
                continue;
            }
            for (; x < xe; x++, A += dA, R += dR, G += dG, B += dB) {
                Uint32 p = row[x];
                int a8 = A >> 8, dr, dg, db;

                if (!(p & ~mask) || a8 <= 0) /* black stays black; no shade here */
                    continue;
                dr = (int) ((p >> rs) & 255), dg = (int) ((p >> gs) & 255), db = (int) ((p >> bs) & 255);
                dr += (((R >> 8) - dr) * a8) >> 8;
                dg += (((G >> 8) - dg) * a8) >> 8;
                db += (((B >> 8) - db) * a8) >> 8;
                row[x] = (p & mask) | ((Uint32) dr << rs) | ((Uint32) dg << gs) | ((Uint32) db << bs);
            }
        }
    }
}

static void
vl_apply(SDL_Surface *low, int mx, int my, int mw, int mh, int ox, int oy, int FS)
{
    SDL_Surface *s = vulture_screen;

    if (!s || !low || s->format->BytesPerPixel != 4)
        return;
    if (SDL_MUSTLOCK(s))
        SDL_LockSurface(s);
    SDL_LockSurface(low);
    vpar_rows(my < 0 ? 0 : my, my + mh > s->h ? s->h : my + mh, [&](int a, int z) {
        vl_apply_band(low, mx, my, mw, mh, ox, oy, FS, a, z);
    });
    SDL_UnlockSurface(low);
    if (SDL_MUSTLOCK(s))
        SDL_UnlockSurface(s);
}

static void
vl_render(mapdata *md, int cx, int cy, int vx, int vy, int ax, int ay, int w, int h)
{
    extern int vulture_render_scale;
    static int c_cx, c_cy, c_vx = -99, c_vy, c_ux, c_uy, c_rot, c_w, c_h;
    static long c_moves = -1;
    static Uint32 c_when = 0;
    /* s147: 8 px cells (was 4) - pools are bilinear across 56 px squares, so
       it looks the same at a quarter of the sampling */
    int FS = 8 * (vulture_render_scale > 0 ? vulture_render_scale : 1);
    int M = 64; /* margin: a cached layer can slide with the camera (s152: was 96) */
    int lw, lh, i, j;
    Uint32 now = SDL_GetTicks();
    SDL_Rect src, dst;
    int max0 = ax, may0 = ay, mw0 = w, mh0 = h; /* s145: the map's own rect */
    static int c_gx0 = 0, c_gy0 = 0;              /* s145: where grid cell 0,0 was */
    static std::vector<float> Lg;

    if (!vl_on || !u.ux || w < 32 || h < 32 || u.uswallow)
        return;
    /* reuse the last layer (shifted with the camera) unless the scene
       changed or ~90 ms passed (flicker); during a slide, always reuse */
    if (vl_low && c_vx == vx && c_vy == vy && c_ux == u.ux && c_uy == u.uy
        && c_moves == svm.moves && c_rot == vulture_view_rot && c_w == w && c_h == h
        && abs(cx - c_cx) < M && abs(cy - c_cy) < M
        && (vtw_animating || now - c_when < (vl_magic ? 50u : 120u)) && vl_flashes.empty()) {
        dst.x = ax - M + (cx - c_cx), dst.y = ay - M + (cy - c_cy);
        vl_relief(max0, may0, mw0, mh0, c_gx0 + (cx - c_cx), c_gy0 + (cy - c_cy), FS);
        vl_apply(vl_low, max0, may0, mw0, mh0, dst.x, dst.y, FS); /* s153 */
        return;
    }
    c_cx = cx, c_cy = cy, c_vx = vx, c_vy = vy, c_ux = u.ux, c_uy = u.uy;
    c_moves = svm.moves, c_rot = vulture_view_rot, c_w = w, c_h = h, c_when = now;
    ax -= M, ay -= M, w += 2 * M, h += 2 * M;
    lw = w / FS + 2, lh = h / FS + 2;
    vl_compute(md);
    Uint32 vt_c = SDL_GetTicks(); /* probe: s147 */
    if (!vl_low || vl_low->w != lw || vl_low->h != lh) {
        if (vl_low)
            SDL_FreeSurface(vl_low);
        vl_low = SDL_CreateRGBSurfaceWithFormat(0, lw, lh, 32, SDL_PIXELFORMAT_ARGB8888);
    }
    if (vl_full) { /* s153: no full-size layer any more */
        SDL_FreeSurface(vl_full);
        vl_full = NULL;
    }
    if (!vl_low)
        return;
    Lg.assign((size_t) lw * lh, 0.0f);
    SDL_LockSurface(vl_low);
    for (j = 0; j < lh; j++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) vl_low->pixels + j * vl_low->pitch);

        for (i = 0; i < lw; i++) {
            /* screen -> view -> map (inverse of vfx_square_screen) */
            double sx = (ax + i * FS - cx) / (double) V_MAP_XMOD;
            double sy = (ay + j * FS - cy) / (double) V_MAP_YMOD;
            double a = (sx + sy) / 2.0, b2 = (sy - sx) / 2.0, t;
            float L, r, g, b, dark, tint, A, cr, cg, cb, mx, mn;
            int k;

            for (k = vulture_view_rot & 3; k > 0; k--) { /* view -> map */
                t = a;
                a = -b2;
                b2 = t;
            }
            vl_sample(vx + a, vy + b2, &L, &r, &g, &b);
            if (L < 0)
                L = 0;
            Lg[(size_t) j * lw + i] = L; /* s145 */
            dark = (1.0f - (L > 1.0f ? 1.0f : L)) * 0.80f;
            cr = cg = cb = 1.0f;
            if (L > 0.01f)
                cr = r / L, cg = g / L, cb = b / L;
            mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
            mn = cr < cg ? (cr < cb ? cr : cb) : (cg < cb ? cg : cb);
            tint = (mx - mn) * 0.35f * (L > 1.2f ? 1.2f : L);
            A = dark + tint * (1.0f - dark);
            if (A > 0.95f)
                A = 0.95f;
            {
                float s = A > 0.001f ? tint * (1.0f - dark) / A : 0.0f;
                Uint8 R8 = (Uint8) (255 * (cr / mx) * s), G8 = (Uint8) (255 * (cg / mx) * s),
                      B8 = (Uint8) (255 * (cb / mx) * s);

                row[i] = ((Uint32) (A * 255) << 24) | ((Uint32) R8 << 16) | ((Uint32) G8 << 8) | B8;
            }
        }
    }
    SDL_UnlockSurface(vl_low);
    Uint32 vt_g = SDL_GetTicks(); /* probe: s147 */
    nhUse(src);
    Uint32 vt_s = SDL_GetTicks(); /* probe: s147 (no stretch since s153) */
    vl_relief_dirs(Lg, lw, lh); /* s145 */
    c_gx0 = ax - FS / 2, c_gy0 = ay - FS / 2; /* cell i covers ax+i*FS +- FS/2 */
    vl_relief(max0, may0, mw0, mh0, c_gx0, c_gy0, FS);
    Uint32 vt_r = SDL_GetTicks(); /* probe: s147 */
    vl_apply(vl_low, max0, may0, mw0, mh0, ax, ay, FS); /* s153 */
    if (getenv("VULTURE_SELFTEST")) /* probe: s147 cost of a fresh lighting layer */
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                          "light: fresh %u ms = compute %u grid %u stretch %u relief %u blit %u\n",
                          (unsigned) (SDL_GetTicks() - now), (unsigned) (vt_c - now),
                          (unsigned) (vt_g - vt_c), (unsigned) (vt_s - vt_g),
                          (unsigned) (vt_r - vt_s), (unsigned) (SDL_GetTicks() - vt_r));
}

/*
 * IMPLEMENTATION_LOG s121: mouse-wheel zoom.  Zooming in scales the drawn
 * map up around the window centre (bilinear) just before the vignette and
 * the HUD, so toasts and the interface stay sharp; clicks and hovers are
 * mapped back through it (mouse_to_map).  Wheeling out past 1x switches
 * to the 2x render scale - the F8 zoomed-out view.
 */
/* s122: crisp steps - 2x/3x are exact pixel multiples; 1.5x is "sharp
   bilinear" (nearest up to 2x, then a small smooth step down) */
static const double vzoom_steps[] = { 1.0, 1.5, 2.0, 3.0 };
static int vzoom_idx = 0;
static SDL_Surface *vzoom_buf = NULL, *vzoom_big = NULL;

static void
vzoom_apply(int ax, int ay, int w, int h)
{
    SDL_Rect src, dst;

    if (vzoom <= 1.0 || w < 16 || h < 16
        || vulture_screen->format->BytesPerPixel != 4)
        return;
    if (!vzoom_buf || vzoom_buf->w != w || vzoom_buf->h != h
        || vzoom_buf->format->format != vulture_screen->format->format) {
        if (vzoom_buf)
            SDL_FreeSurface(vzoom_buf);
        vzoom_buf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                   vulture_screen->format->format);
        if (!vzoom_buf)
            return;
        SDL_SetSurfaceBlendMode(vzoom_buf, SDL_BLENDMODE_NONE);
    }
    {
        int k = (int) ceil(vzoom - 1e-6);

        src.w = (int) (w / vzoom), src.h = (int) (h / vzoom);
        src.x = ax + (w - src.w) / 2, src.y = ay + (h - src.h) / 2;
        dst.x = 0, dst.y = 0, dst.w = w, dst.h = h;
        if (fabs(vzoom - k) < 1e-6) {
            /* exact multiple: nearest neighbour, every pixel a clean block */
            src.w = w / k, src.h = h / k;
            src.x = ax + (w - src.w) / 2, src.y = ay + (h - src.h) / 2;
            dst.w = src.w * k, dst.h = src.h * k;
            dst.x = (w - dst.w) / 2, dst.y = (h - dst.h) / 2;
            if (SDL_SoftStretch(vulture_screen, &src, vzoom_buf, &dst) != 0)
                return;
        } else {
            SDL_Rect big;

            big.x = 0, big.y = 0, big.w = src.w * k, big.h = src.h * k;
            if (!vzoom_big || vzoom_big->w < big.w || vzoom_big->h < big.h
                || vzoom_big->format->format != vulture_screen->format->format) {
                if (vzoom_big)
                    SDL_FreeSurface(vzoom_big);
                vzoom_big = SDL_CreateRGBSurfaceWithFormat(0, big.w, big.h, 32,
                                                           vulture_screen->format->format);
                if (!vzoom_big)
                    return;
            }
            if (SDL_SoftStretch(vulture_screen, &src, vzoom_big, &big) != 0
                || SDL_SoftStretchLinear(vzoom_big, &big, vzoom_buf, &dst) != 0)
                return;
        }
    }
    dst.x = ax, dst.y = ay;
    SDL_BlitSurface(vzoom_buf, NULL, vulture_screen, &dst);
}

/* one wheel notch: dir > 0 in, < 0 out */
static void
vzoom_step(int dir)
{
    static Uint32 last_rescale = 0;
    Uint32 now = SDL_GetTicks();

    if (dir > 0) {
        if (vulture_render_scale > 1) {
            if (now - last_rescale > 350) {
                last_rescale = now;
                vulture_set_render_scale(1);
            }
            return;
        }
        if (vzoom_idx < (int) (sizeof vzoom_steps / sizeof vzoom_steps[0]) - 1)
            vzoom_idx++;
    } else {
        if (vzoom_idx > 0) {
            vzoom_idx--;
        } else if (vulture_render_scale == 1 && now - last_rescale > 350) {
            last_rescale = now;
            vulture_set_render_scale(2);
        }
    }
    vzoom = vzoom_steps[vzoom_idx];
}

bool
levelwin::draw()
{
    int i, j, dir_idx;
    vulture_tile *cur_tile;
    int x, y;
    int tile_id, shadelevel;
    int map_tr_x, map_tr_y, __i, __j, diff, sum;
    int rot_dj, rot_di;     /* s62: map offset of the current view offset */
    int nwx, nwy, nnx, nny, nex, ney, nsx, nsy; /* s62: view-side neighbours */
    int map_centre_x = this->w / 2;
    int map_centre_y = this->h / 2;
    int map_back, map_darkness, map_obj, map_mon;

    unsigned long startticks = SDL_GetTicks();

    static int cur_dlevel = -1;
    static int prev_cx = -1;
    static int prev_cy = -1;

    if (vulture_windows_inited != TRUE) {
        SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);
        /* s65: keep the main menu's title image behind its dialogs */
        if (vulture_backdrop)
            SDL_BlitSurface(vulture_backdrop, NULL, vulture_screen, NULL);
        vulture_invalidate_region(clip_tl_x, clip_tl_y, clip_br_x - clip_tl_x,
                                  clip_br_y - clip_tl_y);
        return 1;
    }

    if (cur_dlevel != u.uz.dlevel) {
        init_floor_decors(10);
        cur_dlevel = u.uz.dlevel;
    }

    /* s75: tween offsets (camera + movers) and facing for this frame */
    vtw_prepare(view_x, view_y);
    vfx_scan(); /* s85: damage numbers / blood from HP changes */
    vstatus_scan(); /* s129 */
    vtip_tick();    /* s142 */
    vglint_tick();  /* s136 */
    vamb_tick();    /* s136 */
    vfxm_load(); /* s161: saved effect choices */
    vimp_tick(); /* s151 */
    {
        int jx, jy;

        vimp_offset(&jx, &jy); /* s151: a heavy landing jolts the view */
        map_centre_x += vtw_gx + (int) vpan_px + jx; /* s79: + MMB drag residual */
        map_centre_y += vtw_gy + (int) vpan_py + jy;
    }

    if (vzoom > 1.0)
        prev_cx = -1; /* s121: zoomed, every frame is a full redraw */
    if (prev_cx != view_x || prev_cy != view_y
        || map_data->map_swallow != V_TILE_NONE) {
        clip_tl_x = this->abs_x;
        clip_tl_y = this->abs_y;
        clip_br_x = this->abs_x + this->w;
        clip_br_y = this->abs_y + this->h;
    } else {
        clip_tl_x = min(clip_tl_x, 0);
        clip_tl_y = min(clip_tl_y, 0);
        clip_br_x = max(clip_br_x, this->abs_x + this->w - 1);
        clip_br_y = max(clip_br_y, this->abs_y + this->h - 1);
    }

    if (clip_tl_x >= this->w + this->abs_x
        || clip_tl_y >= this->h + this->abs_y)
        /* nothing changed onscreen */
        return 1;

    prev_cx = view_x;
    prev_cy = view_y;

    /* Only draw on map area */
    vulture_set_draw_region(clip_tl_x, clip_tl_y, clip_br_x, clip_br_y);

    /* If swallowed draw ONLY the engulf tile and the player! */
    if (u.uswallow && map_data->map_swallow != V_TILE_NONE) {
        /* Clear map area */
        SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);

        {
            int vj, vi; /* §62 */

            v_rot(u.ux - view_x, u.uy - view_y, 4 - vulture_view_rot, &vj,
                  &vi);
            x = map_centre_x + V_MAP_XMOD * (vj - vi);
            y = map_centre_y + V_MAP_YMOD * (vj + vi);
        }

        /* engulf tile */
        vulture_put_tile(x, y, map_data->map_swallow);

        /* player */
        vulture_put_tile(x, y, map_data->get_glyph(MAP_MON, u.ux, u.uy));

        vulture_invalidate_region(clip_tl_x, clip_tl_y, clip_br_x - clip_tl_x,
                                  clip_br_y - clip_tl_y);
        return 1;
    } else
        map_data->map_swallow = V_TILE_NONE;

    /* prevent double redraws if the map view just moved under the mouse
     * cursor */
    map_highlight = mouse_to_map(vulture_get_mouse_pos());

    /* coords of the top right corner */
    map_tr_x = (-V_MAP_YMOD * (map_centre_x + 50)
                + V_MAP_XMOD * (map_centre_y + 50) + V_MAP_XMOD * V_MAP_YMOD)
               / (2 * V_MAP_XMOD * V_MAP_YMOD);
    map_tr_y = (V_MAP_YMOD * (map_centre_x + 50)
                + V_MAP_XMOD * (map_centre_y + 50) + V_MAP_XMOD * V_MAP_YMOD)
               / (2 * V_MAP_XMOD * V_MAP_YMOD);

    Uint32 vpf_a = SDL_GetTicks(); /* probe: s152 frame stages */
    vnm_begin(); /* s149: the map's tiles lay their normal maps from here */

    /* without the +-1 small corners within the viewport are not drawn */
    diff = map_tr_x - map_tr_y - 1;
    sum = map_tr_x + map_tr_y + 1;

    for (__i = -map_tr_y; __i <= map_tr_y; __i++) {
        for (__j = diff + __i; __j + __i <= sum; __j++) {
            /* s62: screen position (view offset) -> map square */
            v_rot(__j, __i, vulture_view_rot, &rot_dj, &rot_di);
            j = view_x + rot_dj;
            i = view_y + rot_di;

            x = map_centre_x + V_MAP_XMOD * (__j - __i);
            y = map_centre_y + V_MAP_YMOD * (__j + __i);

            if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO) {
                vulture_put_tile(x, y, V_MISC_OFF_MAP);
                continue;
            }

            map_back = map_data->get_glyph(MAP_BACK, j, i);
            map_darkness = map_data->get_glyph(MAP_DARKNESS, j, i);
            /* s153: only squares the light layer will shade with normals
               lay theirs (seen and lit, or close to you) */
            if (vnm_ok)
                vulture_normal_capture = vl_L[i][j] >= 0.55f || distmin(u.ux, u.uy, j, i) <= 4;

            /*
            Draw Vulture's tiles, in order:
            pass 1
                    1. Floor
                    2. Floor edges

            pass 2
                    1. North & west walls
                    2. Furniture
                    3. Traps
                    4. Objects
                    5. Monsters
                    6. Effects
                    7. South & east walls
            */

            /* 0. init walls and floor edges for this tile*/
            if (map_back == V_TILE_WALL_GENERIC
                || map_back == V_MISC_UNMAPPED_AREA)
                get_wall_tiles(i, j);
            else
                /* certain events (amnesia or a secret door being discovered)
                 * require us to clear walls again. since we cannot check for
                 * those cases we simply clear walls whenever we don't set
                 * any... */
                clear_walls(i, j);

            if (map_back == V_TILE_FLOOR_WATER || map_back == V_TILE_FLOOR_ICE
                || map_back == V_TILE_FLOOR_LAVA
                || map_back == V_TILE_FLOOR_AIR)
                /* these tiles get edges */
                get_floor_edges(i, j);
            else
                /* everything else doesn't. However we can't just assume a
                 * tile that doesn't need egdes doesn't have any: pools may be
                 * dried out by fireballs, and suddenly we have a pit with
                 * edges :( Therefore we always clear them explicitly */
                clear_floor_edges(i, j);

            /* 2. Floor */
            tile_id = map_back;
            shadelevel = 0;

            if ((tile_id >= V_TILE_FLOOR_COBBLESTONE)
                && (tile_id <= V_TILE_FLOOR_DARK)) {
                tile_id = get_floor_tile(tile_id, i, j);
                shadelevel = map_darkness;
            } else if (tile_id == V_TILE_NONE
                       || tile_id == V_TILE_WALL_GENERIC)
                tile_id = V_MISC_UNMAPPED_AREA;

            vulture_put_tile_shaded(x, y, tile_id, shadelevel);

            /* shortcut for unmapped case */
            if (tile_id == V_MISC_UNMAPPED_AREA)
                continue;

            if (vulture_opts.highlight_cursor_square
                && (j == map_highlight.x && i == map_highlight.y))
                vulture_put_tile(x, y, V_MISC_FLOOR_HIGHLIGHT);

            /* 3. Floor edges */
            for (dir_idx = 0; dir_idx < 12; dir_idx++)
                vulture_put_tile(x, y, maptile_floor_edge[i][j].dir[dir_idx]);
        }
    }

    for (__i = -map_tr_y; __i <= map_tr_y; __i++) {
        for (__j = diff + __i; __j + __i <= sum; __j++) {
            v_rot(__j, __i, vulture_view_rot, &rot_dj, &rot_di); /* s62 */
            j = view_x + rot_dj;
            i = view_y + rot_di;
            if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO)
                continue;
            /* Find position of tile centre */
            x = map_centre_x + V_MAP_XMOD * (__j - __i);
            y = map_centre_y + V_MAP_YMOD * (__j + __i);

            map_back = map_data->get_glyph(MAP_BACK, j, i);
            map_obj = map_data->get_glyph(MAP_OBJ, j, i);
            map_mon = map_data->get_glyph(MAP_MON, j, i);
            if (vnm_ok) /* s153: as in the floor pass */
                vulture_normal_capture = vl_L[i][j] >= 0.55f || distmin(u.ux, u.uy, j, i) <= 4;

            /* s62: the squares on this square's screen-west/north/east/
             * south sides, in map coordinates */
            v_rot(-1, 0, vulture_view_rot, &nwx, &nwy);
            v_rot(0, -1, vulture_view_rot, &nnx, &nny);
            v_rot(1, 0, vulture_view_rot, &nex, &ney);
            v_rot(0, 1, vulture_view_rot, &nsx, &nsy);
            nwx += j, nwy += i, nnx += j, nny += i;
            nex += j, ney += i, nsx += j, nsy += i;

            /* 1. West and north walls */
            if (nwx >= 1 && nwx < COLNO && nwy >= 0 && nwy < ROWNO)
                vulture_put_tile_shaded(
                    x, y, maptile_wall[i][j].west,
                    map_data->get_glyph(MAP_DARKNESS, nwx, nwy));
            if (nnx >= 1 && nnx < COLNO && nny >= 0 && nny < ROWNO)
                vulture_put_tile_shaded(
                    x, y, maptile_wall[i][j].north,
                    map_data->get_glyph(MAP_DARKNESS, nnx, nny));

            /* shortcut for unmapped case */
            if (map_back != V_MISC_UNMAPPED_AREA || map_obj != V_TILE_NONE) {
                /* 2. Furniture (s62: a quarter turn swaps the door
                   arch art between the two wall directions) */
                vulture_put_tile(x, y,
                                 v_rot_door(map_data->get_glyph(
                                     MAP_FURNITURE, j, i)));

                /* s93: blood stains on the floor */
                vst_draw(j, i, x, y);

                /* 3. Traps */
                vulture_put_tile(x, y, map_data->get_glyph(MAP_TRAP, j, i));
                vmagic_sprite(j, i, x, y, map_data->get_glyph(MAP_TRAP, j, i)); /* s154 */

                /* 4. Objects (s83: falling while a drop plays) */
                if (map_obj != V_TILE_NONE && vfx_shadows) { /* s158: grounded */
                    vulture_tile *ot = vulture_get_tile(map_obj);

                    if (ot && ot->graphic)
                        vshadow(x, y + 2, ot->graphic->w * 3 / 4, 90);
                }
                vanim_draw_object(j, i, x, y, map_obj);

                /* 5. Monsters */
                if ((cur_tile = vulture_get_tile(map_mon)) != NULL) {
                    /* s75: slide between squares, face the way it went */
                    int tx = x + vtw_ox[i][j],
                        ty = y + vtw_oy[i][j] + vbob_offset(j, i); /* s82 */
                    double xs = 1.0, ys = 1.0, deg = 0.0;
                    bool red = false;

                    if (vfx_shadows && cur_tile->graphic) { /* s158: a shadow on the floor */
                        struct monst *sm = m_at(j, i);
                        int sw = cur_tile->graphic->w * 3 / 5;
                        bool up = sm && (is_flyer(sm->data) || is_floater(sm->data));

                        if (sw < 18)
                            sw = 18;
                        vshadow(tx, ty - vbob_offset(j, i) + 3, up ? sw * 3 / 4 : sw,
                                up ? 90 : 150);
                    }

                    /* s79: squash & stretch + lean while stepping */
                    if (vtw_mv[i][j]) {
                        /* s147: a soft stretch while moving and a settle
                           spread over the last third (the old 26% squash in
                           the final 24 ms showed as a one-frame pop); runs
                           and held keys get less, so they read as walking */
                        double te = vtw_te, a = vtw_linear ? 0.45 : 1.0, sq = 1.0, st = 1.0;
                        struct monst *wm = m_at(j, i); /* s151: weight */
                        int wf = wm ? vheft_mon(wm->data) : 2;

                        if (wf >= 4)
                            sq = 1.6, st = 0.4; /* heavy: barely lifts, lands hard */
                        else if (wf <= 1)
                            sq = 0.7, st = 1.4; /* light: springy */
                        ys = 1.0 + a * st * 0.05 * sin(3.14159265358979 * (te < 0.7 ? te / 0.7 : 1.0));
                        if (te > 0.62)
                            ys -= a * sq * 0.07 * sin(3.14159265358979 * (te - 0.62) / 0.38);
                        xs = 1.0 / pow(ys, 0.7);
                        deg = vtw_face[i][j] * 5.0 * a * sin(3.14159265358979 * te);
                    } else {
                        vfx_breathe(j, i, &xs, &ys); /* s85 */
                        { /* s162: stunned or confused, it sways */
                            struct monst *wm2 = m_at(j, i);

                            if (wm2 && (wm2->mstun || wm2->mconf) && !wm2->msleeping) {
                                double tt2 = SDL_GetTicks() / 1000.0;

                                deg += (wm2->mstun ? 9.0 : 6.0) * sin(tt2 * (wm2->mstun ? 5.0 : 3.3) + j);
                                tx += (int) (2.0 * sin(tt2 * 2.7 + i));
                            }
                        }
                    }
                    vanim_xform(j, i, &tx, &ty, &xs, &ys, &deg, &red);
                    vtw_put_tile_x(tx, ty, map_mon, vtw_face[i][j], xs, ys,
                                   deg, red, 255);
                    if (iflags.hilite_pet
                        && map_data->get_glyph(MAP_PET, j, i))
                        vulture_put_img(
                            tx + cur_tile->xmod, ty + cur_tile->ymod - 10,
                            vulture_get_tile(V_MISC_HILITE_PET)->graphic);
                }

                /* s78: a slain monster sinking and fading */
                vanim_draw_death(j, i, x, y);

                /* 6. Effects */
                vulture_put_tile(x, y,
                                 map_data->get_glyph(MAP_SPECIAL, j, i));
            }

            /* 7. South & East walls */
            if (nsx >= 1 && nsx < COLNO && nsy >= 0 && nsy < ROWNO)
                vulture_put_tile_shaded(
                    x, y, maptile_wall[i][j].south,
                    map_data->get_glyph(MAP_DARKNESS, nsx, nsy));
            if (nex >= 1 && nex < COLNO && ney >= 0 && ney < ROWNO)
                vulture_put_tile_shaded(
                    x, y, maptile_wall[i][j].east,
                    map_data->get_glyph(MAP_DARKNESS, nex, ney));
        }
    }

    /* draw object highlights if requested */
    if (vulture_map_highlight_objects) {
        for (__i = -map_tr_y; __i <= map_tr_y; __i++) {
            for (__j = diff + __i; __j + __i <= sum; __j++) {
                v_rot(__j, __i, vulture_view_rot, &rot_dj, &rot_di); /* s62 */
                j = view_x + rot_dj;
                i = view_y + rot_di;
                if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO)
                    continue;

                x = map_centre_x + V_MAP_XMOD * (__j - __i);
                y = map_centre_y + V_MAP_YMOD * (__j + __i);

                vulture_put_tilehighlight(x, y,
                                          map_data->get_glyph(MAP_OBJ, j, i));
            }
        }
    }
    vulture_normal_capture = false; /* s149 */
    Uint32 vpf_b = SDL_GetTicks(); /* probe */
    vl_render(map_data, map_centre_x, map_centre_y, view_x, view_y, this->abs_x,
              this->abs_y, this->w, this->h); /* s133: dynamic lighting */
    Uint32 vpf_c = SDL_GetTicks(); /* probe */
    vw_draw(map_centre_x, map_centre_y, view_x, view_y, this->abs_x,
            this->abs_y, this->w, this->h); /* s86: light, night, dust, panel */
    Uint32 vpf_d = SDL_GetTicks(); /* probe */
    vambient_draw(this->abs_x, this->abs_y, this->w, this->h); /* s160 */
    vfx_draw(map_centre_x, map_centre_y, view_x, view_y); /* s85 */
    vpath_draw(map_centre_x, map_centre_y, view_x, view_y); /* s125 */
    vmp_mark(map_centre_x, map_centre_y, view_x, view_y);   /* s130 */
    vthreat_draw(map_centre_x, map_centre_y, view_x, view_y); /* s144 */
    vtele_draw(map_centre_x, map_centre_y, view_x, view_y);   /* s163 */
    vaim_draw(map_centre_x, map_centre_y, view_x, view_y);  /* s125 */
    vblast_draw(map_centre_x, map_centre_y, view_x, view_y); /* s138 */
    vzoom_apply(this->abs_x, this->abs_y, this->w, this->h); /* s121 */
    Uint32 vpf_e = SDL_GetTicks(); /* probe */
    vulture_draw_vignette(this->abs_x, this->abs_y, this->w, this->h); /* s74 */
    Uint32 vpf_f = SDL_GetTicks(); /* probe */
    vstatus_pulse_draw(this->abs_x, this->abs_y, this->w, this->h); /* s129 */
    vmp_draw(this->abs_x, this->abs_y, this->w, this->h); /* s130 */
    vhud_draw(map_centre_x, map_centre_y, view_x, view_y, this->abs_x,
              this->abs_y, this->w, this->h); /* s100: toasts, card, waypoint */
    vhb_draw(this->abs_x, this->abs_y, this->w, this->h);   /* s126 */
    vbtn_draw();                                            /* s130 */
    vstealth_draw();                                        /* s137 */
    vgs_draw();                                             /* s145 */
    vrt_draw();                                             /* s140 */
    vtip_draw();                                            /* s142 */
    vhere_draw(this->abs_x, this->abs_y, this->w, this->h); /* s125 */
    if (isok(vhv.x, vhv.y) && !vaim_on) /* s125: monster hover card */
        vcard_draw(map_data->map_square_description(vhv, 0));
    if (isok(vhv.x, vhv.y) && !vaim_on) /* s135: item / feature card */
        vsq_card_draw(map_data->map_square_description(vhv, 0));

    /* Restore drawing region */
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    vulture_invalidate_region(clip_tl_x, clip_tl_y, clip_br_x - clip_tl_x,
                              clip_br_y - clip_tl_y);

    clip_tl_x = 999999;
    clip_tl_y = 999999;
    clip_br_x = 0;
    clip_br_y = 0;

    vulture_tilecache_age();

    vulture_map_draw_msecs = SDL_GetTicks() - startticks;
    if (getenv("VULTURE_SELFTEST")) /* probe: s152 where a frame goes */
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                          "frame %u: pre %u tiles %u light %u vw %u fx %u vignette %u hud %u\n",
                          (unsigned) vulture_map_draw_msecs, (unsigned) (vpf_a - startticks),
                          (unsigned) (vpf_b - vpf_a), (unsigned) (vpf_c - vpf_b),
                          (unsigned) (vpf_d - vpf_c), (unsigned) (vpf_e - vpf_d),
                          (unsigned) (vpf_f - vpf_e), (unsigned) (SDL_GetTicks() - vpf_f));
    vulture_map_draw_lastmove = svm.moves;

    return true;
}

eventresult
levelwin::handle_timer_event(window *target, void *result, int time)
{
    std::string ttext;
    point mappos, mouse;
    bool mouse_moved;
    /* cursor position at the previous timer tick; -1,-1 = none yet.  Updated
     * on EVERY tick, not only inside the hotspot branch, so that leaving a
     * hotspot and returning to the identical pixel is still recognised as a
     * non-move.  Function-static because there is exactly one levelwin. */
    static point last_hover_mouse = { -1, -1 };

    mouse = vulture_get_mouse_pos();
    mappos = mouse_to_map(mouse);

    mouse_moved =
        (mouse.x != last_hover_mouse.x || mouse.y != last_hover_mouse.y);
    last_hover_mouse = mouse;

    /* hovering on a border: scroll */
    if (target != this && target->menu_id) {
        int increment = (time / 500) + 1;

        /*
         * Only scroll for a cursor that is actually MOVING.
         *
         * The 100 ms hover timer fires whenever the window merely has mouse
         * focus (vulture_wait_event(): "send timer events only while the mouse
         * is in the window"), no motion required.  A cursor left resting in
         * one of the corner hotspots therefore scrolled the viewport on every
         * tick, with the increment growing as (time / 500) + 1, until view_x /
         * view_y hit the 0 clamp below - dragging the map clean off the hero
         * and leaving nothing on screen but unmapped rock.  That, not the
         * supersampling layer, is what produced the "dungeon never appears"
         * screenshots: the hero's room was scrolled out of the viewport, and
         * because recenter_after_movement re-centres on the hero at each
         * display while the parked cursor immediately dragged the view back to
         * column 0, consecutive frames came out byte-identical.
         *
         * Requiring movement keeps the intended gesture (sweep the pointer to
         * the edge to scroll) and kills the accidental one.  Note this is a
         * deliberate divergence from stock Vulture.
         */
        if (!mouse_moved)
            return V_EVENT_HANDLED_NOREDRAW;

        {
        /* s62: these deltas are screen directions (view space);
           turn them into map space for the current rotation */
        int sdx = 0, sdy = 0, mdx, mdy;

        switch (target->menu_id) {
        case V_HOTSPOT_SCROLL_UPLEFT:
            sdx -= increment;
            break;
        case V_HOTSPOT_SCROLL_UP:
            sdx -= increment;
            sdy -= increment;
            break;
        case V_HOTSPOT_SCROLL_UPRIGHT:
            sdy -= increment;
            break;
        case V_HOTSPOT_SCROLL_LEFT:
            sdx -= increment;
            sdy += increment;
            break;
        case V_HOTSPOT_SCROLL_RIGHT:
            sdx += increment;
            sdy -= increment;
            break;
        case V_HOTSPOT_SCROLL_DOWNLEFT:
            sdy += increment;
            break;
        case V_HOTSPOT_SCROLL_DOWN:
            sdx += increment;
            sdy += increment;
            break;
        case V_HOTSPOT_SCROLL_DOWNRIGHT:
            sdx += increment;
            break;
        }

        v_rot(sdx, sdy, vulture_view_rot, &mdx, &mdy);
        view_x += mdx;
        view_y += mdy;
        }

        view_x = (view_x < 0) ? 0 : view_x;
        view_y = (view_y < 0) ? 0 : view_y;
        view_x = (view_x > COLNO) ? COLNO : view_x;
        view_y = (view_y > ROWNO) ? ROWNO : view_y;
        vulture_cam_follow = false; /* s123: you moved the camera */
        vulture_cam_manual = true;

        need_redraw = 1;
        return V_EVENT_HANDLED_REDRAW;
    }

    /* hovering over the map: display a tooltip */
    if (time < HOVERTIMEOUT)
        return V_EVENT_HANDLED_NOREDRAW;

    {
        point mp = vulture_get_mouse_pos();

        if (vpt_hit(mp.x, mp.y)) /* s123: no tooltip over the portrait */
            return V_EVENT_HANDLED_NOREDRAW;
        if (vbtn_hit(mp.x, mp.y) >= 0) { /* s130 */
            static const char *const tip[4] = {
                "Fight the nearest enemy  [Tab]",
                "Attack-move: walk to a spot, fighting anything on the way  [Shift+A]",
                "Rest until healed - stops if an enemy appears  [F5]",
                "Explore the level automatically - stops when something comes up"
            };
            vulture_mouse_set_tooltip(tip[vbtn_hit(mp.x, mp.y)]);
            return V_EVENT_HANDLED_NOREDRAW;
        }
        if (vmp_hit(mp.x, mp.y) >= 0)
            return V_EVENT_HANDLED_NOREDRAW;
        if (vrt_hit(mp.x, mp.y)) { /* s140 */
            std::string t = std::string("Time mode: ") + vrt_names[vrt_get()]
                            + " (" + vrt_speed_nm[vrt_speed < 0 ? 0 : vrt_speed] + " speed)"
                            + "\nClick or press Pause to change mode\nRight-click to change speed";

            vulture_mouse_set_tooltip(t);
            return V_EVENT_HANDLED_NOREDRAW;
        }
        if (vgs_hit(mp.x, mp.y) >= 0) { /* s145 */
            vulture_mouse_set_tooltip(vgs_tip(vgs_hit(mp.x, mp.y)));
            return V_EVENT_HANDLED_NOREDRAW;
        }
        if (vspd_rect.w && mp.x >= vspd_rect.x && mp.x < vspd_rect.x + vspd_rect.w
            && mp.y >= vspd_rect.y && mp.y < vspd_rect.y + vspd_rect.h) { /* s144 */
            char sb2[200];
            double s = vhero_speed();

            snprintf(sb2, sizeof sb2,
                     "Your speed %.0f: about %.2f actions per turn\n"
                     "A speed-12 monster acts once per turn%s%s\n"
                     "Hover a monster to see where it can reach you; hold Alt for all of them",
                     s, s / 12.0, Very_fast ? "\nVery fast: an extra action on 2 turns in 3"
                                  : Fast ? "\nFast: an extra action on 1 turn in 3" : "",
                     near_capacity() > UNENCUMBERED ? "\nYour load is slowing you down" : "");
            vulture_mouse_set_tooltip(sb2);
            return V_EVENT_HANDLED_NOREDRAW;
        }
        if (vstealth_hit(mp.x, mp.y)) { /* s137 */
            Uint8 r, g, b;
            std::string why;

            (void) vstealth_state(&r, &g, &b, &why);
            vulture_mouse_set_tooltip(why);
            return V_EVENT_HANDLED_NOREDRAW;
        }
        if (vhb_hit(mp.x, mp.y) >= 0) { /* s126 */
            vulture_mouse_set_tooltip(vhb_tip(vhb_hit(mp.x, mp.y)));
            return V_EVENT_HANDLED_NOREDRAW;
        }
    }
    {
        struct monst *hm = isok(mappos.x, mappos.y) ? m_at(mappos.x, mappos.y) : 0;

        if (hm && canspotmon(hm) && !u_at(mappos.x, mappos.y) && !Hallucination)
            return V_EVENT_HANDLED_NOREDRAW; /* s125: hover card instead */
        if (vsq_hint(mappos.x, mappos.y))
            return V_EVENT_HANDLED_NOREDRAW; /* s135: square card instead */
    }
    if (target != this && !target->caption.empty())
        vulture_mouse_set_tooltip(target->caption);
    else {
        ttext = map_data->map_square_description(mappos, 1);
        if (!ttext.empty())
            vulture_mouse_set_tooltip(ttext);
    }

    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
levelwin::handle_mousemotion_event(window *target, void *result, int xrel,
                                   int yrel, int state)
{
    point mouse, mappos;

    /* s79: middle-mouse drag grabs the map (Dota 2 style): the map follows
     * the mouse, i.e. the camera moves the opposite way.  Whole tiles of
     * drag become view shifts; the remainder is a pixel offset. */
    if (state & SDL_BUTTON_MMASK) {
        extern int vulture_render_scale;
        int sc = vulture_render_scale > 0 ? vulture_render_scale : 1;
        int vx0 = view_x, vy0 = view_y;

        vulture_cam_follow = false; /* s123/s124: you moved the camera */
        vulture_cam_manual = true;

        vpan_px += xrel * sc;
        vpan_py += yrel * sc;
        for (int guard = 0; guard < 64; guard++) {
            double a = (vpan_px / V_MAP_XMOD + vpan_py / V_MAP_YMOD) / 2.0;
            double b = (vpan_py / V_MAP_YMOD - vpan_px / V_MAP_XMOD) / 2.0;
            int ra = (int) floor(a + 0.5), rb = (int) floor(b + 0.5);
            int mdx, mdy, ovx = view_x, ovy = view_y;

            if (!ra && !rb)
                break;
            v_rot(ra, rb, vulture_view_rot, &mdx, &mdy);
            view_x -= mdx;
            view_y -= mdy;
            view_x = (view_x < 0) ? 0 : (view_x > COLNO) ? COLNO : view_x;
            view_y = (view_y < 0) ? 0 : (view_y > ROWNO) ? ROWNO : view_y;
            vulture_cam_follow = false; /* s123: you moved the camera */
            vpan_px -= V_MAP_XMOD * (ra - rb);
            vpan_py -= V_MAP_YMOD * (ra + rb);
            if (view_x == ovx && view_y == ovy) { /* hit the map edge */
                vpan_px = vpan_py = 0.0;
                break;
            }
        }
        /* panning is not movement: don't let the tween animate it (s124:
           shift the snapshot by the pan, so a drag during a travel glide
           doesn't disturb the hero's slide) */
        if (!vtw_pending && !vtw_animating)
            vtw_snap_vx = view_x, vtw_snap_vy = view_y;
        else
            vtw_snap_vx += view_x - vx0, vtw_snap_vy += view_y - vy0;
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }

    mouse = vulture_get_mouse_pos();
    mappos = mouse_to_map(mouse);

    {
        static unsigned last_hl = 0;
        int ph = vmp_hit(mouse.x, mouse.y);
        unsigned hl = ph >= 0 ? vmp.mid[ph] : 0;

        if (hl != last_hl) { /* s130: mark the hovered panel monster */
            last_hl = hl;
            vmp_hl = hl;
            force_redraw();
            need_redraw = 1;
        }
    }
    if (vamove_pending)
        vulture_set_mcursor(V_CURSOR_TARGET_RED);
    { /* s131 */
        int hs = vhb_hit(mouse.x, mouse.y), bt = vbtn_hit(mouse.x, mouse.y),
            pr = vmp_hit(mouse.x, mouse.y);

        vui_hover_sfx(hs >= 0 ? 100 + hs : bt >= 0 ? 200 + bt : pr >= 0 ? 300 + pr
                      : vpt_hit(mouse.x, mouse.y) ? 400 : -1);
    }
    if (vhb_hit(mouse.x, mouse.y) >= 0 || vpt_hit(mouse.x, mouse.y)
        || vbtn_hit(mouse.x, mouse.y) >= 0 || vmp_hit(mouse.x, mouse.y) >= 0
        || vgs_hit(mouse.x, mouse.y) >= 0) { /* s126, s145 */
        vulture_set_mcursor(V_CURSOR_NORMAL);
        if (vhv.x != -1) {
            vhv.x = vhv.y = -1;
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
        return V_EVENT_HANDLED_NOREDRAW;
    }

    if (target != this && target->menu_id) {
        /* show a map scroll cursor if the mouse is in the edge zone */
        switch (target->menu_id) {
        case V_HOTSPOT_SCROLL_UPLEFT:
            vulture_set_mcursor(V_CURSOR_SCROLLUPLEFT);
            break;
        case V_HOTSPOT_SCROLL_UP:
            vulture_set_mcursor(V_CURSOR_SCROLLUP);
            break;
        case V_HOTSPOT_SCROLL_UPRIGHT:
            vulture_set_mcursor(V_CURSOR_SCROLLUPRIGHT);
            break;
        case V_HOTSPOT_SCROLL_LEFT:
            vulture_set_mcursor(V_CURSOR_SCROLLLEFT);
            break;
        case V_HOTSPOT_SCROLL_RIGHT:
            vulture_set_mcursor(V_CURSOR_SCROLLRIGHT);
            break;
        case V_HOTSPOT_SCROLL_DOWNLEFT:
            vulture_set_mcursor(V_CURSOR_SCROLLDOWNLEFT);
            break;
        case V_HOTSPOT_SCROLL_DOWN:
            vulture_set_mcursor(V_CURSOR_SCROLLDOWN);
            break;
        case V_HOTSPOT_SCROLL_DOWNRIGHT:
            vulture_set_mcursor(V_CURSOR_SCROLLDOWNRIGHT);
            break;
        default:
            vulture_set_mcursor(get_map_cursor(mappos));
        }
    } else
        /* select a cursor for the current position */
        vulture_set_mcursor(get_map_cursor(mappos));

    /* s125: the hovered square drives the travel preview and hover card */
    if (mappos.x != vhv.x || mappos.y != vhv.y) {
        vhv = mappos;
        vhv_since = SDL_GetTicks();
        force_redraw();
        need_redraw = 1;
    }

    /* if the highlight option is on, store the map position of the mouse
     * and refresh the current and previous positions */
    if (vulture_opts.highlight_cursor_square
        && (map_highlight.x != mappos.x || map_highlight.y != mappos.y)) {
        mouse = map_to_mouse(map_highlight);
        add_to_clipregion(mouse.x - V_MAP_XMOD, mouse.y - V_MAP_YMOD,
                          mouse.x + V_MAP_XMOD, mouse.y + V_MAP_YMOD);
        mouse = map_to_mouse(mappos);
        add_to_clipregion(mouse.x - V_MAP_XMOD, mouse.y - V_MAP_YMOD,
                          mouse.x + V_MAP_XMOD, mouse.y + V_MAP_YMOD);

        map_highlight = mappos;
        need_redraw = 1;
        return V_EVENT_HANDLED_REDRAW;
    }

    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
levelwin::handle_mousebuttonup_event(window *target, void *result,
                                     int mouse_x, int mouse_y, int button,
                                     int state)
{
    point mouse, mappos;

    /* s79: releasing a middle-mouse drag snaps the leftover pixels away, so
     * clicks map to squares exactly again */
    if (button == SDL_BUTTON_LEFT && vulture_swallow_lclick_up) {
        vulture_swallow_lclick_up = false; /* s128: it only stopped you */
        if (SDL_GetTicks() - vulture_swallow_lclick_t < 1500)
            return V_EVENT_HANDLED_NOREDRAW;
    }
    if (button == SDL_BUTTON_WHEELUP || button == SDL_BUTTON_WHEELDOWN) {
        vzoom_step(button == SDL_BUTTON_WHEELUP ? 1 : -1); /* s121 */
        vulture_sfx("ui_zoom"); /* s131 */
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }
    if (button == SDL_BUTTON_MIDDLE) {
        vpan_px = vpan_py = 0.0;
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }

    mouse = vulture_get_mouse_pos();
    mappos = mouse_to_map(mouse);

    if (vamove_pending) { /* s130: attack-move target */
        vamove_pending = false;
        if (button == SDL_BUTTON_LEFT && isok(mappos.x, mappos.y)
            && vbtn_hit(mouse.x, mouse.y) < 0 && vhb_hit(mouse.x, mouse.y) < 0) {
            vauto_start(VAUTO_AMOVE, mappos.x, mappos.y);
            vfx_click(mappos.x, mappos.y, VCK_ATTACK, false);
        }
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }
    if (vgs_hit(mouse.x, mouse.y) >= 0) { /* s145: gear sets */
        int k = vgs_hit(mouse.x, mouse.y);

        if (button == SDL_BUTTON_LEFT)
            vgs_equip(k);
        else if (button == SDL_BUTTON_RIGHT)
            vgs_store(k);
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }
    if (vtip_hit(mouse.x, mouse.y)) { /* s142: close the tip / no more tips */
        if (button == SDL_BUTTON_RIGHT) {
            vtip_state = 1;
            vtip_save();
            vtip_nq = 0;
            pline("Tips turned off.");
        }
        vtip_cur = -1;
        vulture_sfx("ui_cancel");
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }
    if (vrt_hit(mouse.x, mouse.y)) { /* s140: click the pill to change mode */
        if (button == SDL_BUTTON_LEFT)
            vrt_cycle();
        else if (button == SDL_BUTTON_RIGHT) /* s142 */
            vrt_speed_cycle();
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }
    {
        int bh = vbtn_hit(mouse.x, mouse.y), ph = vmp_hit(mouse.x, mouse.y);

        if (bh >= 0) { /* s130: FIGHT / A-MOVE / REST */
            if (button == SDL_BUTTON_LEFT) {
                vulture_sfx("ui_click"); /* s131 */
                if (bh == 0) {
                    if (vauto_fight(result)) {
                        force_redraw();
                        return V_EVENT_HANDLED_FINAL;
                    }
                    vulture_sfx("ui_error");
                    pline("No enemies in sight.");
                } else if (bh == 1) {
                    vulture_sfx("aim_ready");
                    vamove_pending = true;
                    pline("Attack-move: click where to go (Esc to cancel).");
                } else if (bh == 2) {
                    vauto_start(VAUTO_REST, -1, -1);
                } else { /* s134: EXPLORE (Brogue's x) */
                    cmdq_add_ec(CQ_CANNED, doautoexplore);
                    vulture_click_consumed = TRUE;
                    ((vulture_event *) result)->num = 0;
                    ((vulture_event *) result)->x = u.ux;
                    ((vulture_event *) result)->y = u.uy;
                    force_redraw();
                    return V_EVENT_HANDLED_FINAL;
                }
            }
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
        if (ph >= 0) { /* s130: look at a monster from the panel */
            struct monst *pm = vmp_mon(vmp.mid[ph]);

            if (pm && button == SDL_BUTTON_LEFT) {
                set_view(pm->mx, pm->my);
                vulture_cam_follow = false;
                vulture_cam_manual = true;
            }
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
    }
    { /* s126: the ability bar */
        int hs = vhb_hit(mouse.x, mouse.y);

        if (hs >= 0) {
            if (button == SDL_BUTTON_LEFT && vhb[hs].kind && vhb_use(hs)) {
                ((vulture_event *) result)->num = 0;
                ((vulture_event *) result)->x = u.ux;
                ((vulture_event *) result)->y = u.uy;
                force_redraw();
                return V_EVENT_HANDLED_FINAL;
            }
            if (button == SDL_BUTTON_RIGHT
                || (button == SDL_BUTTON_LEFT && !vhb[hs].kind))
                vhb_pick(hs);
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
    }
    if (vpt_hit(mouse.x, mouse.y)) { /* s117: the portrait drives the camera */
        if (button == SDL_BUTTON_LEFT)
            vpt_click();
        return V_EVENT_HANDLED_REDRAW;
    }
    if (button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_CTRL) && isok(mappos.x, mappos.y)
        && distmin(u.ux, u.uy, mappos.x, mappos.y) == 1) { /* s156: an aimed blow */
        struct monst *am = m_at(mappos.x, mappos.y);

        if (am && canspotmon(am) && !am->mtame) {
            if (vaim_pick(am, result)) {
                force_redraw();
                return V_EVENT_HANDLED_FINAL;
            }
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
    }
    if (button == SDL_BUTTON_LEFT) { /* s108: Dota-style click marker */
        vfx_click_at(mappos.x, mappos.y);
        force_redraw();
    }
    return map_data->handle_click(result, button, mappos);
}

eventresult
levelwin::handle_keydown_event(window *target, void *result, int sym, int mod,
                               int unicode)
{
    int translated_key, key;
    int macronum, i;

    if ((mod & KMOD_CTRL) && !(mod & (KMOD_ALT | KMOD_SHIFT)) && sym >= SDLK_1
        && sym <= SDLK_3) { /* s145: gear sets */
        vgs_equip(sym - SDLK_1);
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }
    switch (sym) {
    case SDLK_TAB:
        /* s130: Tab fights the nearest enemy (DCSS); Shift+Tab is the map */
        if (mod & KMOD_SHIFT) {
            map::toggle();
            return V_EVENT_HANDLED_REDRAW;
        }
        if (vauto_fight(result)) {
            force_redraw();
            return V_EVENT_HANDLED_FINAL;
        }
        vulture_sfx("ui_error"); /* s131 */
        pline("No enemies in sight.");
        return V_EVENT_HANDLED_REDRAW;

    case SDLK_ESCAPE:
        if (vamove_pending) { /* s130 */
            vamove_pending = false;
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
        if (vauto_mode != VAUTO_NONE) { /* s145: stop FIGHT / REST / gear first */
            vauto_stop("You stop.");
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
        if (vpause_menu(result)) { /* s145: the pause menu */
            force_redraw();
            return V_EVENT_HANDLED_FINAL;
        }
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;

    case SDLK_a:
        if ((mod & KMOD_SHIFT) && !(mod & (KMOD_CTRL | KMOD_ALT))) {
            vamove_pending = true; /* s130: Dota's attack-move */
            vulture_sfx("aim_ready"); /* s131 */
            pline("Attack-move: click where to go (Esc to cancel).");
            vulture_set_mcursor(V_CURSOR_TARGET_RED);
            return V_EVENT_HANDLED_REDRAW;
        }
        break;

    /* F7 toggles wall height at runtime (QoL: full-height walls occlude the
     * squares behind them).  Deliberately NOT in vulture_sdl.cpp's global
     * hook - that fires inside menus and text prompts too; this handler only
     * runs while the level window is topwin, i.e. during play.  No
     * vulture_write_userconfig() here either: it rewrites the entire user
     * conf on every call; persistence stays with the Ctrl+Shift+O menu. */
    case SDLK_F7:
        vulture_opts.wall_style =
            (vulture_opts.wall_style == V_WALL_DISPLAY_STYLE_FULL)
                ? V_WALL_DISPLAY_STYLE_HALF_HEIGHT
                : V_WALL_DISPLAY_STYLE_FULL;
        set_wall_style(vulture_opts.wall_style);
        force_redraw();
        pline("Walls: %s.",
              vulture_opts.wall_style == V_WALL_DISPLAY_STYLE_FULL
                  ? "full height" : "half height");
        return V_EVENT_HANDLED_REDRAW;

    /* F8 toggles the zoomed-out (supersampled) view between 1x and 2x.  Safe
     * to do at runtime because vulture_set_render_scale() recreates the render
     * surface and then calls vulture_win_resize(), which is exactly the
     * relayout a real window resize performs (and which the Alt+Enter
     * fullscreen toggle and the Interface Options menu already depend on).  It
     * deliberately does NOT re-issue SDL_SetWindowSize()/SetWindowFullscreen(),
     * so toggling cannot flash the display or undo the sub-540x300 resize
     * clamp.  Same placement rationale as F7 (level window only, so it cannot
     * fire inside a menu or a text prompt; no userconfig rewrite - persistence
     * stays with the Ctrl+Shift+O menu). */
    case SDLK_LSHIFT: /* s141: the hover card shows / hides its codex */
    case SDLK_RSHIFT:
    case SDLK_LALT: /* s144: the danger map */
    case SDLK_RALT:
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;

    case SDLK_PAUSE: /* s140: cycle the time mode */
        vrt_cycle();
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;

    case SDLK_F9: /* s133: dynamic lighting; s145: full / flat / off */
        if (vl_on && vl_relief_on)
            vl_relief_set(false);
        else if (vl_on)
            vl_on = false;
        else
            vl_on = true, vl_relief_set(true);
        pline("Dynamic lighting: %s.", !vl_on ? "off" : vl_relief_on ? "full, normal-mapped"
                                                                     : "flat (no normal maps)");
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;

    /* s86: F11 photo mode - the map without the interface (F12 is
     * Vulture's own full-screen screenshot, caught globally) */
    case SDLK_F11:
        vw_photo(this);
        return V_EVENT_HANDLED_REDRAW;

    case SDLK_F8:
        vulture_set_render_scale(vulture_opts.render_scale > 1 ? 1 : 2);
        /* w/h are this window's own dimensions, which vulture_win_resize()
         * has just re-derived from the new render size - right in fullscreen
         * too, where vulture_opts.width/height are the windowed size.  Report
         * vulture_render_scale, not the option, so a failed surface
         * allocation is reported honestly. */
        pline("View: %s (rendering %dx%d).",
              vulture_render_scale > 1 ? "zoomed out" : "normal", w, h);
        return V_EVENT_HANDLED_REDRAW;

    /* Vulture mod (IMPLEMENTATION_LOG §62): turn the view a quarter at a
     * time.  Level window only (same reasoning as F7/F8: never fires inside
     * a menu or prompt), takes no game time, not saved - a new session
     * starts at the stock angle. */
    case SDLK_PAGEUP:
    case SDLK_PAGEDOWN:
        vulture_view_rot = (vulture_view_rot + ((sym == SDLK_PAGEUP) ? 3 : 1))
                           & 3;
        force_redraw();
        pline("View turned to %d degrees.  (PageUp/PageDown)",
              vulture_view_rot * 90);
        return V_EVENT_HANDLED_REDRAW;

    /* F1- F6 trigger macros */
    case SDLK_F1:
    case SDLK_F2:
    case SDLK_F3:
    case SDLK_F4:
    case SDLK_F5:
    case SDLK_F6:
        macronum = sym - SDLK_F1;
        if (sym == SDLK_F5 && !vulture_opts.macro[macronum][0]) {
            vauto_start(VAUTO_REST, -1, -1); /* s130: rest until healed */
            force_redraw();
            return V_EVENT_HANDLED_REDRAW;
        }
        if (!vulture_opts.macro[macronum][0])
            break;

        ((vulture_event *) result)->num = vulture_opts.macro[macronum][0];
        for (i = strlen(vulture_opts.macro[macronum]) - 1; i > 0; i--)
            vulture_eventstack_add(vulture_opts.macro[macronum][i], -1, -1,
                                   V_RESPOND_ANY);
        return V_EVENT_HANDLED_FINAL;

    /* CTRL+SHIFT+o opens the interface options */
    case SDLK_o:
        if (!(mod & KMOD_SHIFT) || !(mod & KMOD_CTRL))
            break;
        vulture_iface_opts();
        return V_EVENT_HANDLED_REDRAW;

    /* CTRL+SHIFT+p shows the message log */
    case SDLK_p:
        if (!(mod & KMOD_SHIFT) || !(mod & KMOD_CTRL))
            break;
        msgwin->view_all();
        return V_EVENT_HANDLED_REDRAW;

    default:
        break;
    }

    /* s126: 1-0 use the ability bar's slot, if it's filled */
    if (sym >= SDLK_0 && sym <= SDLK_9
        && !(mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI))) {
        int hs = sym == SDLK_0 ? 9 : sym - SDLK_1;

        vhb_load();
        if (vhb[hs].kind && vhb_use(hs)) {
            ((vulture_event *) result)->num = 0;
            ((vulture_event *) result)->x = u.ux;
            ((vulture_event *) result)->y = u.uy;
            force_redraw();
            return V_EVENT_HANDLED_FINAL;
        }
    }

    /* all other keys are converted and passed to the core */
    key = vulture_make_nh_key(sym, mod, unicode);

    /* s134 (Brogue): '>' or '<' away from the stairs walks you to the
       known staircase (or ladder) going that way; on them, it's the usual */
    if ((key == '>' || key == '<') && u.ux && !u.uswallow) {
        stairway *here = stairway_at(u.ux, u.uy), *st, *best = NULL;
        bool up = (key == '<');

        if (!here || (here->up ? true : false) != up) {
            for (st = gs.stairs; st; st = st->next)
                if ((st->up ? true : false) == up && isok(st->sx, st->sy)
                    && levl[st->sx][st->sy].seenv
                    && (!best || distmin(u.ux, u.uy, st->sx, st->sy)
                                     < distmin(u.ux, u.uy, best->sx, best->sy)))
                    best = st;
            if (best) {
                vulture_travel_to(best->sx, best->sy);
                vfx_click(best->sx, best->sy, VCK_STAIRS, up);
                ((vulture_event *) result)->num = 0;
                ((vulture_event *) result)->x = u.ux;
                ((vulture_event *) result)->y = u.uy;
                force_redraw();
                return V_EVENT_HANDLED_FINAL;
            }
        }
    }

    if (!key)
        return V_EVENT_HANDLED_NOREDRAW;

    if (mapwin && isdigit(key))
        translated_key = key;
    else
        translated_key = vulture_translate_key(key);

    if (translated_key) {
        ((vulture_event *) result)->num = translated_key;
        return V_EVENT_HANDLED_FINAL;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
levelwin::handle_resize_event(window *target, void *result, int res_w,
                              int res_h)
{
    window *winelem;

    if (this == target) {
        w = res_w;
        h = res_h;

        /* update window scrolling activation regions.  The band width follows
         * vulture_ui_scale, so a runtime render-scale change (which reaches us
         * as a resize) re-sizes the bands as well as repositioning them. */
        for (winelem = first_child; winelem; winelem = winelem->sib_next) {
            switch (winelem->menu_id) {
            case V_HOTSPOT_SCROLL_UPLEFT:
                winelem->w = V_UI(20);
                winelem->h = V_UI(20);
                break;

            case V_HOTSPOT_SCROLL_UP:
                winelem->x = V_UI(20);
                winelem->w = w - V_UI(40);
                winelem->h = V_UI(20);
                break;

            case V_HOTSPOT_SCROLL_UPRIGHT:
                winelem->x = w - V_UI(20);
                winelem->w = V_UI(20);
                winelem->h = V_UI(20);
                break;

            case V_HOTSPOT_SCROLL_LEFT:
                winelem->y = V_UI(20);
                winelem->w = V_UI(20);
                winelem->h = h - V_UI(40);
                break;

            case V_HOTSPOT_SCROLL_RIGHT:
                winelem->y = V_UI(20);
                winelem->w = V_UI(20);
                winelem->h = h - V_UI(40);
                winelem->x = w - V_UI(20);
                break;

            case V_HOTSPOT_SCROLL_DOWNLEFT:
                winelem->y = h - V_UI(20);
                winelem->w = V_UI(20);
                winelem->h = V_UI(20);
                break;

            case V_HOTSPOT_SCROLL_DOWN:
                winelem->x = V_UI(20);
                winelem->y = h - V_UI(20);
                winelem->w = w - V_UI(40);
                winelem->h = V_UI(20);
                break;

            case V_HOTSPOT_SCROLL_DOWNRIGHT:
                winelem->x = w - V_UI(20);
                winelem->y = h - V_UI(20);
                winelem->w = V_UI(20);
                winelem->h = V_UI(20);
                break;
            }
        }
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

void
levelwin::map_update(glyph_type type, int prev_glyph, int new_glyph, int x,
                     int y)
{
    int pixel_x, pixel_y;
    int tl_x = 99999, tl_y = 99999, br_x = 0, br_y = 0;
    vulture_tile *oldtile, *newtile;

    {
        int vj, vi; /* §62: map -> view offset */

        v_rot(x - view_x, y - view_y, 4 - vulture_view_rot, &vj, &vi);
        pixel_x = (this->w / 2) + V_MAP_XMOD * (vj - vi);
        pixel_y = (this->h / 2) + V_MAP_YMOD * (vj + vi);
    }

    if (pixel_x < -VULTURE_CLIPMARGIN || pixel_y < -VULTURE_CLIPMARGIN
        || pixel_x > vulture_screen->w + VULTURE_CLIPMARGIN
        || pixel_y > vulture_screen->h + VULTURE_CLIPMARGIN)
        return;

    oldtile = vulture_get_tile(prev_glyph);
    newtile = vulture_get_tile(new_glyph);

    if (type != MAP_BACK) {
        if (oldtile) {
            tl_x = oldtile->xmod;
            tl_y = oldtile->ymod;

            br_x = oldtile->xmod + oldtile->graphic->w;
            br_y = oldtile->ymod + oldtile->graphic->h;
        }

        if (newtile) {
            tl_x = min(newtile->xmod, tl_x);
            tl_y = min(newtile->ymod, tl_y);

            br_x = max(br_x, newtile->xmod + newtile->graphic->w);
            br_y = max(br_y, newtile->ymod + newtile->graphic->h);
        }

        tl_x += pixel_x;
        tl_y += pixel_y;
        br_x += pixel_x;
        br_y += pixel_y;

        if (type == MAP_MON)
            /* allow for the heart icon on pets */
            tl_y -= 10;
    } else {
        /* floor tiles tend to be placeholders until we reach draw_level,
         * so we do this manually */
        tl_x = pixel_x - 56;
        tl_y =
            pixel_y - 100; /* 100 pixels accounts for possible walls, too */
        br_x = pixel_x + 56;
        br_y = pixel_y + 22;
    }

    add_to_clipregion(tl_x, tl_y, br_x, br_y);
}

void
levelwin::map_clear()
{
    int i, j;

    for (i = 0; i < ROWNO; i++)
        for (j = 0; j < COLNO; j++) {
            clear_floor_edges(i, j);
            clear_walls(i, j);
        }
}

/* s62: on a quarter (odd) turn the two door-arch directions trade places */
static int
v_rot_door(int tile)
{
    if (!(vulture_view_rot & 1))
        return tile;
    switch (tile) {
    case V_MISC_VDOOR_WOOD_OPEN:   return V_MISC_HDOOR_WOOD_OPEN;
    case V_MISC_HDOOR_WOOD_OPEN:   return V_MISC_VDOOR_WOOD_OPEN;
    case V_MISC_VDOOR_WOOD_CLOSED: return V_MISC_HDOOR_WOOD_CLOSED;
    case V_MISC_HDOOR_WOOD_CLOSED: return V_MISC_VDOOR_WOOD_CLOSED;
    default:                       return tile;
    }
}

/* floor division for a positive divisor (C '/' truncates toward zero) */
static int
v_floordiv(int a, int b)
{
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

point
levelwin::mouse_to_map(point mouse)
{
    point mappos;
    int px, py, vj, vi, dj, di;

    /* s121: undo the zoom - the map under the pointer was drawn nearer the
       centre before being scaled up */
    if (vzoom > 1.0) {
        mouse.x = w / 2 + (int) floor((mouse.x - w / 2) / vzoom);
        mouse.y = h / 2 + (int) floor((mouse.y - h / 2) / vzoom);
    }
    px = mouse.x - (w / 2), py = mouse.y - (h / 2);

    /* §62: screen -> VIEW offset (the stock formula, with the view centre
     * factored out and floor division so negative offsets round the same
     * way), then view -> map through the rotation */
    vj = v_floordiv(V_MAP_YMOD * px + V_MAP_XMOD * py + V_MAP_XMOD * V_MAP_YMOD,
                    2 * V_MAP_XMOD * V_MAP_YMOD);
    vi = v_floordiv(-V_MAP_YMOD * px + V_MAP_XMOD * py
                        + V_MAP_XMOD * V_MAP_YMOD,
                    2 * V_MAP_XMOD * V_MAP_YMOD);
    v_rot(vj, vi, vulture_view_rot, &dj, &di);
    mappos.x = view_x + dj;
    mappos.y = view_y + di;

    return mappos;
}

point
levelwin::map_to_mouse(point mappos)
{
    int map_centre_x = w / 2;
    int map_centre_y = h / 2;
    point mouse;
    int vj, vi;

    /* §62: map -> view offset, then the stock projection */
    v_rot(mappos.x - view_x, mappos.y - view_y, 4 - vulture_view_rot, &vj,
          &vi);
    mouse.x = map_centre_x + V_MAP_XMOD * (vj - vi);
    mouse.y = map_centre_y + V_MAP_YMOD * (vj + vi);
    if (vzoom > 1.0) { /* s121 */
        mouse.x = map_centre_x + (int) ((mouse.x - map_centre_x) * vzoom);
        mouse.y = map_centre_y + (int) ((mouse.y - map_centre_y) * vzoom);
    }

    /* FIXME Why does this happen sometimes ? */
    if (mouse.x < 0)
        mouse.x = 0;
    if (mouse.y < 0)
        mouse.y = 0;

    return mouse;
}

void
levelwin::toggle_uiwin(int menuid, bool enabled)
{
    window *win = this->first_child;
    while (win) {
        if (win->menu_id == menuid)
            win->visible = enabled;
        win = win->sib_next;
    }

    force_redraw();
}

void
levelwin::add_to_clipregion(int tl_x, int tl_y, int br_x, int br_y)
{
    clip_tl_x = min(clip_tl_x, tl_x);
    clip_tl_y = min(clip_tl_y, tl_y);
    clip_br_x = max(clip_br_x, br_x);
    clip_br_y = max(clip_br_y, br_y);
}

void
levelwin::set_wall_style(int style)
{
    switch (style) {
    case V_WALL_DISPLAY_STYLE_FULL:
        walltiles = (struct walls *) walls_full;
        break;
    case V_WALL_DISPLAY_STYLE_HALF_HEIGHT:
        walltiles = (struct walls *) walls_half;
        break;
    }
}

/* get room index is only used to semi-randomly select room decorations
 * therefore the number we return can be as bogus as we want, so long as
 * it's consistently the same. Using the current depth will provide a bit of
 * variety on the maze levels...*/
int
levelwin::get_room_index(int x, int y)
{
    int rindex;
    if (svn.nroom == 0) /* maze levels */
        return u.uz.dlevel;

    if (In_mines(&u.uz)) /* The mines are a patchwork dungeon otherwise :( */
        return (u.uz.dlevel
                + svn.nroom); /* cleverly prevent a repetitive sequence :P */

    rindex = room_indices[y][x];
    if (!rindex)
        return (u.uz.dlevel + svn.nroom);

    return rindex;
}

/*
 * Convert wall tile index (ie. wall type) to an associated decoration style.
 */
int
levelwin::get_wall_decor(int floortype, int wally, int wallx, int floory,
                         int floorx)
{
    int roomid;

#ifdef REINCARNATION
    if (Is_rogue_level(&u.uz))
        return V_WALL_LIGHT;
#endif

    switch (floortype) {
    case V_TILE_FLOOR_ROUGH:
    case V_TILE_FLOOR_ROUGH_LIT:
        return V_WALL_ROUGH;
    case V_MISC_FLOOR_NOT_VISIBLE:
    case V_TILE_FLOOR_COBBLESTONE: {
        roomid = get_room_index(floorx, floory);
        switch (roomid % 4) {
        case 0:
            return V_WALL_STUCCO;
        case 1:
            return V_WALL_BRICK + ((wally * wallx + wally + wallx) % 5);
        case 2:
            return V_WALL_VINE_COVERED;
        case 3:
            return V_WALL_MARBLE;
        }
    }
    default:
        return V_WALL_BRICK;
    }
}

/*
 * Convert floor tile index (ie. floor type) to an associated decoration
 * style.
 */
int
levelwin::get_floor_decor(int floorstyle, int floory, int floorx)
{
    int roomid;
#ifdef REINCARNATION
    if (Is_rogue_level(&u.uz))
        return V_FLOOR_DARK;
#endif

    switch (floorstyle) {
    case V_TILE_FLOOR_ROUGH:
        return V_FLOOR_ROUGH;
    case V_TILE_FLOOR_ROUGH_LIT:
        return V_FLOOR_ROUGH_LIT;
    case V_TILE_FLOOR_COBBLESTONE: {
        roomid = get_room_index(floorx, floory);
        switch (roomid % 4) {
        case 0:
            return V_FLOOR_CERAMIC;
        case 1:
            return V_FLOOR_COBBLESTONE;
        case 2:
            return V_FLOOR_MOSS_COVERED;
        case 3:
            return V_FLOOR_MARBLE;
        }
    }
    case V_TILE_FLOOR_WATER:
        return V_FLOOR_WATER;
    case V_TILE_FLOOR_ICE:
        return V_FLOOR_ICE;
    case V_TILE_FLOOR_AIR:
        return V_FLOOR_AIR;
    case V_TILE_FLOOR_LAVA:
        return V_FLOOR_LAVA;
    case V_TILE_FLOOR_DARK:
        return V_FLOOR_DARK;
    default:
        return V_FLOOR_COBBLESTONE;
    }
}

void
levelwin::init_floor_decors(int num_decors)
{
    int i, j, k;
    int lx, ly;

    if (!svn.nroom)
        return; /* the level doesn't have distinct rooms, so do nothing */

    /* after putting a decor in room roomno, we calculate (roomno + 5) modulo
     * svn.nroom. (svn.nroom is the global containing the number of rooms on the
     * level) */
    int roomno = 0;
    /* when roomno wraps we also add wrapadd, to ensure that a different bunch
     * of rooms gets the next few decorations this will distibute decorations
     * seemingly at random but repeatably throughout the level (rooms are
     * sorted from left to right, so a step of 1 would leave most decorations
     * on the left side)*/
    int wrapadd = (svn.nroom % 5) ? 0 : 1;

    /* if placing a decor fails, try at most retries other rooms */
    int retries;

    /* did we manage to place the deco?  */
    int placed;

    int old_deco, old_deco2;
    int deco_height;
    int deco_width;
    int xoffset, yoffset;

    int current_deco = V_FLOOR_CARPET;

    /* reset the room indices and map decor arrays */
    for (i = 0; i < ROWNO; i++) {
        memset(room_indices[i], 0, COLNO);
        memset(map_deco[i], 0, COLNO);
    }

    /* init room indices */
    for (i = 0; i < svn.nroom; i++)
        for (j = svr.rooms[i].ly; j <= svr.rooms[i].hy; j++)
            memset(&room_indices[j][svr.rooms[i].lx], (i + 1),
                   (svr.rooms[i].hx - svr.rooms[i].lx + 1));

            /* do no more if we're on the rogue level or in the mines, because
             * decors there look dumb */
#ifdef REINCARNATION
    if (Is_rogue_level(&u.uz))
        return;
#endif
    s_level *lev;
    if (In_mines(&u.uz)
        && ((lev = Is_special(&u.uz)) == 0 || !lev->flags.town))
        return;

    for (i = 0; i < num_decors; i++) {
        retries = svn.nroom;
        placed = 0;
        switch (roomno % 3) {
        case 0:
            current_deco = V_FLOOR_CARPET;
            break;
        case 1:
            current_deco = V_FLOOR_MURAL;
            break;
        case 2:
            current_deco = V_FLOOR_MURAL2;
            break;
        }

        deco_width = floorstyles[current_deco].x;
        deco_height = floorstyles[current_deco].y;

        while (retries-- && !placed) {
            lx = svr.rooms[roomno].lx;
            ly = svr.rooms[roomno].ly;
            while (ly <= svr.rooms[roomno].hy
                   && (old_deco = (map_deco[ly][lx] >> 4)) != 0) {
                while (lx <= svr.rooms[roomno].hx
                       && (old_deco2 = (map_deco[ly][lx] >> 4)) != 0)
                    lx += floorstyles[old_deco2 - 1].x;
                ly += floorstyles[old_deco - 1].y;
                lx = svr.rooms[roomno].lx;
            }

            if ((svr.rooms[roomno].hx - lx + 1) >= deco_width
                && (svr.rooms[roomno].hy - ly + 1) >= deco_height) {
                placed = 1;
                for (j = 0; j < deco_height; j++)
                    for (k = 0; k < deco_width; k++)
                        map_deco[ly + j][lx + k] =
                            ((current_deco + 1) << 4) + (j * deco_width + k);
            }
        }

        if (!retries && !placed)
            /* placing this one failed, so trying to place others is futile */
            break;

        roomno += 5;
        if (roomno > svn.nroom)
            roomno = (roomno % svn.nroom) + wrapadd;
    }

    /* centre the decorations in the rooms, as the previous code always plces
     * them in the top corner*/
    for (i = 0; i < svn.nroom; i++) {
        lx = svr.rooms[i].lx;
        ly = svr.rooms[i].ly;

        while (map_deco[ly][lx] != 0 && lx <= svr.rooms[i].hx)
            lx++;

        deco_width = lx - svr.rooms[i].lx;
        lx = svr.rooms[i].lx;

        while (map_deco[ly][lx] != 0 && ly <= svr.rooms[i].hy)
            ly++;

        deco_height = ly - svr.rooms[i].ly;
        xoffset =
            (int) ((svr.rooms[i].lx + svr.rooms[i].hx + 1) / 2.0 - deco_width / 2.0);
        yoffset =
            (int) ((svr.rooms[i].ly + svr.rooms[i].hy + 1) / 2.0 - deco_height / 2.0);

        for (j = deco_height - 1; j >= 0; j--)
            for (k = deco_width - 1; k >= 0; k--)
                map_deco[yoffset + j][xoffset + k] =
                    map_deco[svr.rooms[i].ly + j][svr.rooms[i].lx + k];

        for (j = svr.rooms[i].ly; j < yoffset; j++)
            memset(&map_deco[j][svr.rooms[i].lx], 0,
                   (svr.rooms[i].hx - svr.rooms[i].lx + 1));

        for (j = svr.rooms[i].ly; j <= svr.rooms[i].hy; j++)
            memset(&map_deco[j][svr.rooms[i].lx], 0, (xoffset - svr.rooms[i].lx));
    }
}

void
levelwin::get_wall_tiles(int y, int x)
{
    /* s62: the four sides are SCREEN sides - west is whatever map square
     * lies to this square's screen-west at the current view rotation (at
     * rotation 0 that is x - 1, exactly as before) */
    static const int side_dx[4] = { -1, 0, 1, 0 }; /* W N E S (view) */
    static const int side_dy[4] = { 0, -1, 0, 1 };
    int side, style, nx, ny, bg, tile;

    if (!svl.level.locations[x][y].seenv)
        return;

    for (side = 0; side < 4; side++) {
        v_rot(side_dx[side], side_dy[side], vulture_view_rot, &nx, &ny);
        nx += x;
        ny += y;
        tile = V_TILE_NONE;
        if (nx >= 0 && nx < COLNO && ny >= 0 && ny < ROWNO) {
            bg = map_data->get_glyph(MAP_BACK, nx, ny);
            if (bg != V_TILE_WALL_GENERIC && bg != V_MISC_UNMAPPED_AREA) {
                style = get_wall_decor(bg, y, x, ny, nx);
                tile = (side == 0) ? walltiles[style].west
                       : (side == 1) ? walltiles[style].north
                       : (side == 2) ? walltiles[style].east
                       : walltiles[style].south;
            }
        }
        if (side == 0)
            maptile_wall[y][x].west = tile;
        else if (side == 1)
            maptile_wall[y][x].north = tile;
        else if (side == 2)
            maptile_wall[y][x].east = tile;
        else
            maptile_wall[y][x].south = tile;
    }
}

int
levelwin::get_floor_tile(int tile, int y, int x)
{
    int style;
    int deco_pos;
    unsigned char deco = map_deco[y][x];

    /* s62: multi-square floor art (rugs) is laid out for rotation 0;
       turned, it would come out scrambled, so it sits out */
    if (deco && tile == V_TILE_FLOOR_COBBLESTONE && !(vulture_view_rot & 3)) {
        style = (deco >> 4) - 1;
        deco_pos = (int) (deco & 0x0F);
        return floorstyles[style].array[deco_pos];
    }

    style = get_floor_decor(tile, y, x);
    {
        /* s62: tile the floor pattern in VIEW coordinates so it lines
           up on screen at every rotation */
        int px, py, fx = floorstyles[style].x, fy = floorstyles[style].y;

        v_rot(x, y, 4 - vulture_view_rot, &px, &py);
        px = ((px % fx) + fx) % fx;
        py = ((py % fy) + fy) % fy;
        deco_pos = fx * py + px;
    }
    return floorstyles[style].array[deco_pos];
}

void
levelwin::get_floor_edges(int y, int x)
{
    int i, x2, y2;
    int tile = map_data->get_glyph(MAP_BACK, x, y);
    int style = V_FLOOR_EDGE_COBBLESTONE;

    point s_delta[4] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
    point d_delta[4] = { { -1, 1 }, { -1, -1 }, { 1, -1 }, { 1, 1 } };

    /* s62: the edge art is per SCREEN direction; turn the probes */
    for (i = 0; i < 4; i++) {
        int rx, ry;

        v_rot(s_delta[i].x, s_delta[i].y, vulture_view_rot, &rx, &ry);
        s_delta[i].x = rx, s_delta[i].y = ry;
        v_rot(d_delta[i].x, d_delta[i].y, vulture_view_rot, &rx, &ry);
        d_delta[i].x = rx, d_delta[i].y = ry;
    }

    /* Default: no floor edges around tile */
    clear_floor_edges(y, x);

    /* straight sections */
    for (i = 0; i < 4; i++) {
        x2 = x + s_delta[i].x;
        y2 = y + s_delta[i].y;
        if (x > 0 && y > 0 && x2 >= 0 && x2 < COLNO && y2 >= 0
            && y2 < ROWNO
            && tile != map_data->get_glyph(MAP_BACK, x2, y2)
            && tile + map_data->get_glyph(MAP_BACK, x2, y2)
                   != V_TILE_FLOOR_WATER
                          + V_TILE_FLOOR_ICE) /* this prevents borders between
                                                 water and ice*/
            maptile_floor_edge[y][x].dir[i] = flooredges[style].dir[i];
    }

    /* "inward pointing" corners */
    for (i = 4; i < 8; i++) {
        if ((maptile_floor_edge[y][x].dir[(i + 3) % 4] != V_TILE_NONE)
            && (maptile_floor_edge[y][x].dir[i % 4]) != V_TILE_NONE)

            maptile_floor_edge[y][x].dir[i] = flooredges[style].dir[i];
    }

    /* "outward pointing" corners */
    for (i = 8; i < 12; i++) {
        x2 = x + d_delta[i % 4].x;
        y2 = y + d_delta[i % 4].y;
        if ((maptile_floor_edge[y][x].dir[(i + 3) % 4] == V_TILE_NONE)
            && (maptile_floor_edge[y][x].dir[i % 4] == V_TILE_NONE) && x > 0
            && x < COLNO - 1 && y > 0 && y < ROWNO - 1
            && tile != map_data->get_glyph(MAP_BACK, x2, y2)
            && tile + map_data->get_glyph(MAP_BACK, x2, y2)
                   != V_TILE_FLOOR_WATER + V_TILE_FLOOR_ICE)
            maptile_floor_edge[y][x].dir[i] = flooredges[style].dir[i];
    }

    /*    for (i = 0; i < 12; i++)
                    if (!maptile_floor_edge[y][x].dir[i])
                            maptile_floor_edge[y][x].dir[i] = V_TILE_NONE;*/
}

void
levelwin::clear_walls(int y, int x)
{
    maptile_wall[y][x].west = V_TILE_NONE;
    maptile_wall[y][x].north = V_TILE_NONE;
    maptile_wall[y][x].east = V_TILE_NONE;
    maptile_wall[y][x].south = V_TILE_NONE;
}

void
levelwin::clear_floor_edges(int y, int x)
{
    int i;
    for (i = 0; i < 12; i++)
        maptile_floor_edge[y][x].dir[i] = V_TILE_NONE;
}

/* select an appropriate cursor for the given location */
int
levelwin::get_map_cursor(point mappos)
{
    if ((mappos.x < 1) || (mappos.x >= COLNO) || (mappos.y < 0)
        || (mappos.y >= ROWNO))
        return V_CURSOR_TARGET_INVALID;

    /* whatis: look or teleport */
    if (vulture_whatis_active)
        return V_CURSOR_TARGET_HELP;

    /* monsters and objects get a red circle */
    if (map_data->get_glyph(MAP_MON, mappos.x, mappos.y) != V_TILE_NONE
        && ((mappos.x != u.ux) || (mappos.y != u.uy)))
        return V_CURSOR_TARGET_RED;

    if (map_data->get_glyph(MAP_OBJ, mappos.x, mappos.y) != V_TILE_NONE)
        return V_CURSOR_TARGET_RED;

    /* other valid visible locations  */
    if (map_data->get_glyph(MAP_BACK, mappos.x, mappos.y) != V_TILE_NONE) {
        int furniture =
            map_data->get_glyph(MAP_FURNITURE, mappos.x, mappos.y);
        /* Closed doors get an 'open door' cursor */
        if ((furniture == V_MISC_VDOOR_WOOD_CLOSED)
            || (furniture == V_MISC_HDOOR_WOOD_CLOSED))
            return V_CURSOR_OPENDOOR;

        /* Stairs and ladders get a 'stairs' cursor */
        if ((furniture == V_MISC_STAIRS_UP)
            || (furniture == V_MISC_STAIRS_DOWN)
            || (furniture == V_MISC_LADDER_UP)
            || (furniture == V_MISC_LADDER_DOWN))
            return V_CURSOR_STAIRS;

        /* Fountains get a 'goblet' cursor */
        if ((furniture == V_MISC_FOUNTAIN) || (furniture == V_MISC_SINK)
#ifdef VULTURE_SLASHEM
            || (furniture == V_MISC_TOILET)
#endif
        )
            return V_CURSOR_GOBLET;

        if (map_data->get_glyph(MAP_BACK, mappos.x, mappos.y)
            != V_TILE_WALL_GENERIC)
            return V_CURSOR_TARGET_GREEN;
    }

    return V_CURSOR_TARGET_INVALID;
}
