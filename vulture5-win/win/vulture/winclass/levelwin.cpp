/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

#include <thread> /* s167: before NetHack's min/max macros */
#include <algorithm> /* s190: std::sort, likewise */
extern "C" {
#include "hack.h"
#include "func_tab.h" /* s195: the command table, for changing keys */
}

#include "contextmenu.h"
#include "anykeydialog.h" /* s195 */
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

    default: /* s209: full height */
        walltiles = (struct walls *) walls_full;
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
static void vprobe_seq_shot(const char *tag); /* probe: s184 */

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
        vulture_gpu_forget(ci->second.second); /* s183 */
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
    vulture_gpu_hd_derive(f, s, true, 256, 256); /* s201: its HD art, mirrored */
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
/* s169: the sprite transformer - scale, rotate, tint and fade a sprite.
   Reads the sprite's own 32-bit pixels where it can (no per-call format
   conversion), samples BILINEARLY with alpha weighting (smooth squash,
   stretch, breathing and lean instead of jaggy nearest pixels), writes into
   one reused buffer, and does the pixel maths inline (no SDL_GetRGBA /
   SDL_MapRGBA per pixel). */
static int vtx_tint = -1; /* s193: >= 0: the sprite drawn in this colour (0xRRGGBB), its alpha kept */

static void
vtx_blit(SDL_Surface *img, int dx, int dy, double sx, double sy, double deg,
         bool red, int alpha)
{
    static SDL_Surface *dst = NULL;
    SDL_Surface *src = NULL, *conv = NULL;
    double rad, c, s, ax, ay, minx, miny, maxx, maxy;
    int w, h, bw, bh, x, y, k, rsh, gsh, bsh, ash;
    Uint32 key = 0, amask;
    bool haskey, hasalpha;
    int amul = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;

    if (!img)
        return;
    if (sx == 1.0 && sy == 1.0 && deg == 0.0 && alpha >= 255 && vtx_tint < 0) {
        if (red)
            SDL_SetSurfaceColorMod(img, 255, 70, 70);
        vulture_put_img(dx, dy, img);
        if (red)
            SDL_SetSurfaceColorMod(img, 255, 255, 255);
        return;
    }
    if (vulture_gpumap_on) { /* s183: the map layer is drawn on the GPU */
        vulture_gpumap_blit_ex(img, dx, dy, sx, sy, deg, red, alpha);
        return;
    }
    if (img->format->BytesPerPixel == 4) {
        src = img;
    } else {
        conv = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!conv)
            return;
        src = conv;
    }
    rsh = src->format->Rshift, gsh = src->format->Gshift, bsh = src->format->Bshift;
    ash = src->format->Ashift, amask = src->format->Amask;
    hasalpha = amask != 0;
    haskey = (SDL_GetColorKey(src, &key) == 0);
    /* s94: a sprite with no alpha and no colour key would come out as a
       black box: treat its pure black as transparent */
    if (!hasalpha && !haskey) {
        haskey = true;
        key = SDL_MapRGB(src->format, 0, 0, 0) & ~amask;
    }
    w = src->w, h = src->h;
    ax = w / 2.0, ay = (double) h; /* the feet */
    rad = deg * 3.14159265358979 / 180.0;
    c = cos(rad), s = sin(rad);
    {
        double px[4] = { -ax, w - ax, -ax, w - ax };
        double py[4] = { -ay, -ay, h - ay, h - ay };

        minx = miny = 1e9, maxx = maxy = -1e9;
        for (k = 0; k < 4; k++) {
            double qx = px[k] * sx, qy = py[k] * sy;
            double cx2 = qx * c - qy * s, cy2 = qx * s + qy * c;

            if (cx2 < minx) minx = cx2;
            if (cx2 > maxx) maxx = cx2;
            if (cy2 < miny) miny = cy2;
            if (cy2 > maxy) maxy = cy2;
        }
    }
    bw = (int) (maxx - minx) + 3, bh = (int) (maxy - miny) + 3;
    if (bw <= 0 || bh <= 0 || bw > 1000 || bh > 1000 || sx == 0.0 || sy == 0.0) {
        if (conv)
            SDL_FreeSurface(conv);
        return;
    }
    if (!dst || dst->w < bw || dst->h < bh) {
        if (dst)
            SDL_FreeSurface(dst);
        dst = SDL_CreateRGBSurfaceWithFormat(0, bw > 160 ? bw : 160, bh > 200 ? bh : 200, 32,
                                             SDL_PIXELFORMAT_ARGB8888);
        if (!dst) {
            if (conv)
                SDL_FreeSurface(conv);
            return;
        }
        SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_BLEND);
    }
    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    {
        /* inverse mapping, stepped incrementally along each row */
        double isx = 1.0 / sx, isy = 1.0 / sy;
        double dux = c * isx, dvx = -s * isy; /* source step per destination x */
        const Uint8 *sp = (const Uint8 *) src->pixels;
        int spitch = src->pitch;
        /* destination pixel (X0 + x, Y0 + y): its centre, relative to the
           sprite's anchor (dx + ax, dy + ay), is where we sample from */
        int X0 = dx + (int) floor(ax + minx) - 1, Y0 = dy + (int) floor(ay + miny) - 1;

        for (y = 0; y < bh; y++) {
            Uint32 *drow = (Uint32 *) ((Uint8 *) dst->pixels + y * dst->pitch);
            double ry = Y0 + y + 0.5 - (dy + ay), rx0 = X0 + 0.5 - (dx + ax);
            double u = (rx0 * c + ry * s) * isx + ax - 0.5;
            double v = (-rx0 * s + ry * c) * isy + ay - 0.5;

            for (x = 0; x < bw; x++, u += dux, v += dvx) {
                int x0 = (int) floor(u), y0 = (int) floor(v), q;
                double fx = u - x0, fy = v - y0;
                double wq[4], aa = 0, rr = 0, gg = 0, bb = 0;

                drow[x] = 0;
                if (x0 < -1 || y0 < -1 || x0 >= w || y0 >= h)
                    continue;
                wq[0] = (1 - fx) * (1 - fy), wq[1] = fx * (1 - fy);
                wq[2] = (1 - fx) * fy, wq[3] = fx * fy;
                for (q = 0; q < 4; q++) {
                    int xx = x0 + (q & 1), yy = y0 + (q >> 1);
                    Uint32 pp;
                    double pa;

                    if (xx < 0 || yy < 0 || xx >= w || yy >= h)
                        continue;
                    pp = *(const Uint32 *) (sp + yy * spitch + xx * 4);
                    if (haskey && (pp & ~amask) == (key & ~amask))
                        continue;
                    pa = hasalpha ? ((pp & amask) >> ash) / 255.0 : 1.0;
                    pa *= wq[q];
                    aa += pa;
                    rr += pa * ((pp >> rsh) & 255);
                    gg += pa * ((pp >> gsh) & 255);
                    bb += pa * ((pp >> bsh) & 255);
                }
                if (aa <= 0.004)
                    continue;
                {
                    int R = (int) (rr / aa), G = (int) (gg / aa), B = (int) (bb / aa);
                    int A = (int) (aa * amul);

                    if (red) {
                        G = G * 3 / 10;
                        B = B * 3 / 10;
                    }
                    if (vtx_tint >= 0) /* s193 */
                        R = (vtx_tint >> 16) & 255, G = (vtx_tint >> 8) & 255, B = vtx_tint & 255;
                    if (A > 255)
                        A = 255;
                    drow[x] = ((Uint32) A << 24) | ((Uint32) (R > 255 ? 255 : R) << 16)
                              | ((Uint32) (G > 255 ? 255 : G) << 8) | (Uint32) (B > 255 ? 255 : B);
                }
            }
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    {
        SDL_Rect sr = { 0, 0, bw, bh }, dr;

        dr.x = dx + (int) floor(ax + minx) - 1, dr.y = dy + (int) floor(ay + miny) - 1;
        dr.w = bw, dr.h = bh;
        SDL_BlitSurface(dst, &sr, vulture_screen, &dr);
    }
    if (conv)
        SDL_FreeSurface(conv);
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
    vulture_put_img(d.x, d.y, s); /* s183: GPU-aware */
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
    if (vulture_normal_capture && vulture_gpumap_on) { /* s185: recorded for the GPU */
        SDL_Surface *nm = vulture_get_tile_normal(tile_id, img != tile->graphic);

        if (nm)
            vulture_gpumap_normal_ex(nm, dx, y + tile->ymod, sx, sy, deg, alpha);
    } else if (vulture_normal_capture && vulture_normal_buf) { /* s149: its normal map, same pose */
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
static void vimp_note_attack(int ax, int ay, int tx, int ty);                /* s202 */
static void vimp_recoil_xform(int j, int i, int *ox, int *oy, double *deg); /* s202 */
static void vimp_hit(int x, int y, int dmg, int maxhp, bool hero);         /* s202 */
static void vimp_kill(int x, int y, int maxhp);                            /* s202 */
static void vdth_start(int x, int y, int tile, int heft, int face, bool ghost); /* s204 */
static bool vdth_draw(int j, int i, int x, int y);                              /* s204 */
static int vdth_object_alpha(int j, int i);                                     /* s204 */
static bool vbr_draw(int j, int i, int x, int y, int tile_id);                  /* s204 */
static bool vstair_busy(Uint32 now);                                            /* s216 */
static bool vbr_offset(int j, int i, int *ox, int *oy);                         /* s204 */
static bool vlid_draw(int j, int i, int x, int y, int tile_id);                 /* s207 */
static bool vfl_hides(int j, int i, int tile_id);                               /* s207 */
static int vanim_ax, vanim_ay, vanim_tx, vanim_ty;
static double vanim_t = 0.0;
struct vanim_death {
    int x, y, tile, heft; /* s151: heft */
    int face;             /* s204: the way it faced; a ghost fades instead of falling */
    bool ghost;
};
static vanim_death vanim_deaths[8];
static int vanim_ndeaths = 0;

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
        vprobe_seq_shot(kind == 1 ? "lunge" : "anim"); /* probe: s184 */
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
    vimp_note_attack(ax, ay, tx, ty); /* s202: who swings at whom, for the impact */
    if (!levwin || vulture_windows_inited != TRUE || vanim_kind || vtw_animating)
        return;
    if (abs(ax - tx) > 1 || abs(ay - ty) > 1 || (ax == tx && ay == ty))
        return;
    { /* s154: finish any step first.  You and the monster can move in the
         same turn before the screen catches up; the lunge then struck at
         the square you were walking into while you were still drawn on the
         old one.  Draw once (which notices the pending moves) and play them
         out, so the blow lands where you visibly are. */
        if (getenv("VULTURE_SELFTEST")) /* probe: s184 */
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                              "lunge %d,%d -> %d,%d: hero %d,%d drawn-from %d,%d pending %d\n",
                              ax, ay, tx, ty, u.ux, u.uy,
                              vtw_snap.count(VTW_HERO) ? vtw_snap[VTW_HERO].x : -1,
                              vtw_snap.count(VTW_HERO) ? vtw_snap[VTW_HERO].y : -1, (int) vtw_pending);
        /* s184: the core's display buffer first.  The blow comes mid-turn,
           before the core flushes what it drew; until then Vulture's map
           still shows you (and a monster that just moved) on the old
           square - the glide below moved nothing, the lunge struck an empty
           square, and you popped into it afterwards. */
        flush_screen(1);
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        if (vtw_pending)
            vulture_tween_run();
        if (vanim_kind || vtw_animating)
            return;
        if (vtw_pending) { /* s184: a glide it skipped (a teleport-mode run):
                              snap it, so the blow lands where you are drawn */
            vtw_t = 1.0;
            vtw_pending = false;
            vtw_commit(vtw_last_vx, vtw_last_vy);
        }
        if (getenv("VULTURE_SELFTEST")) /* probe: s184 */
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                              "lunge plays: hero drawn-from %d,%d (at %d,%d)\n",
                              vtw_snap.count(VTW_HERO) ? vtw_snap[VTW_HERO].x : -1,
                              vtw_snap.count(VTW_HERO) ? vtw_snap[VTW_HERO].y : -1, u.ux, u.uy);
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
    vanim_deaths[vanim_ndeaths].face = vtw_face[y][x]; /* s204 */
    {
        struct monst *dm = m_at(x, y);

        vanim_deaths[vanim_ndeaths].heft = dm ? vheft_mon(dm->data) : 2; /* s151 */
        vanim_deaths[vanim_ndeaths].ghost = dm && noncorporeal(dm->data);
        if (!dm || !noncorporeal(dm->data))
            vst_add(x, y, 2); /* s93: a death leaves a pool */
        if (dm) /* s202: the killing blow lands too (vfx_scan never sees it) */
            vimp_kill(x, y, dm->mhpmax);
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

        if (map_data->get_glyph(MAP_OBJ, x, y) != V_TILE_NONE
            && vdth_object_alpha(x, y) >= 255) { /* s204: a falling body brings its corpse in itself */
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
    int ha;

    if (vbr_draw(j, i, x, y, tile_id)) /* s204: a boulder rolling in */
        return;
    if (vlid_draw(j, i, x, y, tile_id)) /* s207: a lid up */
        return;
    if (vfl_hides(j, i, tile_id)) /* s207: in flight - drawn in the air by vfl_draw */
        return;
    ha = vdth_object_alpha(j, i); /* s204: a corpse waits for its body to land */
    if (ha < 255) {
        if (ha > 0 && (tile = vulture_get_tile(tile_id)) != NULL && tile->graphic)
            vtx_blit(tile->graphic, x + tile->xmod, y + tile->ymod, 1.0, 1.0, 0.0, false, ha);
        return;
    }
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
        /* s204: it falls on the clock (vdth_*), never holding you up */
        vdth_start(vanim_deaths[i].x, vanim_deaths[i].y, vanim_deaths[i].tile, vanim_deaths[i].heft,
                   vanim_deaths[i].face, vanim_deaths[i].ghost);
        if (!vanim_deaths[i].ghost) /* s151: the body lands - as it reaches the floor */
            vimpact(vanim_deaths[i].x, vanim_deaths[i].y, vanim_deaths[i].heft, 330);
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

    vimp_recoil_xform(j, i, ox, oy, deg); /* s202: a flinch or a lean */
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
            vulture_put_img(d.x, d.y, vst_art[e.seed]); /* s183: GPU-aware */
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
static bool vfx_mist = true;      /* s192: drifting fog over remembered ground */
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
            vimp_hit(u.ux, u.uy, vfx_hero_hp - hp, Upolyd ? u.mhmax : u.uhpmax, true); /* s202 */
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
            vimp_hit(m->mx, m->my, dmg, m->mhpmax, false); /* s202 */
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

/* s184: where the creature on square (j, i) is DRAWN right now - the square
   plus the glide offset of whoever is stepping into it (vtw_ox / vtw_oy).
   Text and effects that follow a creature (barks, speech, the searching
   label, damage numbers, the level-up burst, the trail from the hero) used
   the square alone, so mid-step they jumped ahead to the square it was
   walking into while its sprite was still sliding there. */
static void
vfx_mover_screen(int j, int i, int cx, int cy, int vx, int vy, int *sx, int *sy)
{
    vfx_square_screen(j, i, cx, cy, vx, vy, sx, sy);
    if (j >= 0 && j < COLNO && i >= 0 && i < ROWNO) {
        *sx += vtw_ox[i][j];
        *sy += vtw_oy[i][j];
    }
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
    vfx_mover_screen(x, y, cx, cy, vx, vy, &sx, &sy); /* s184 */
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
 * IMPLEMENTATION_LOG s171: halos and living water.  Light sources (lamps,
 * candles, glowing monsters) and fiery / electric creatures get a soft
 * additive bloom that flickers with the flame; water squares carry moving
 * glints, lava slow molten pulses.  Drawn after the light layer, so they
 * glow over the darkness; part of the "Atmosphere" effect switch.
 */
static void
vhalo(int sx, int sy, int rad, Uint8 r, Uint8 g, Uint8 b, int alpha)
{
    static SDL_Surface *disc = NULL;
    SDL_Rect d;

    if (!disc) {
        int x, y, n = 128;

        disc = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!disc)
            return;
        SDL_LockSurface(disc);
        for (y = 0; y < n; y++) {
            Uint32 *row = (Uint32 *) ((Uint8 *) disc->pixels + y * disc->pitch);

            for (x = 0; x < n; x++) {
                double dx = (x + 0.5 - n / 2.0) / (n / 2.0), dy = (y + 0.5 - n / 2.0) / (n / 2.0);
                double r2 = dx * dx + dy * dy, a = r2 >= 1.0 ? 0.0 : pow(1.0 - r2, 2.2);

                row[x] = ((Uint32) (a * 255) << 24) | 0x00FFFFFFu;
            }
        }
        SDL_UnlockSurface(disc);
        SDL_SetSurfaceBlendMode(disc, SDL_BLENDMODE_ADD);
    }
    if (rad < 4 || alpha <= 2)
        return;
    SDL_SetSurfaceColorMod(disc, r, g, b);
    SDL_SetSurfaceAlphaMod(disc, (Uint8) (alpha > 255 ? 255 : alpha));
    d.x = sx - rad, d.y = sy - rad / 2 - rad / 3, d.w = rad * 2, d.h = rad * 2 * 2 / 3;
    SDL_BlitScaled(disc, NULL, vulture_screen, &d);
}

static void
vhalo_draw(int cx, int cy, int vx, int vy)
{
    double t = SDL_GetTicks() / 1000.0;
    light_source *ls;
    struct monst *m;
    int x, y, sx, sy;

    if (!vfx_ambient || !u.ux || u.uswallow || Hallucination)
        return;
    for (ls = gl.light_base; ls; ls = ls->next) { /* lamps, candles, glowing things */
        double fl = 0.85 + 0.10 * sin(t * 9.0 + ls->x * 1.7) + 0.05 * sin(t * 23.0 + ls->y);

        if (!isok(ls->x, ls->y) || !cansee(ls->x, ls->y) || ls->range <= 0)
            continue;
        vfx_square_screen(ls->x, ls->y, cx, cy, vx, vy, &sx, &sy);
        vhalo(sx, sy - 30, (int) (30 + 9 * ls->range), 255, 190, 110, (int) (60 * fl));
    }
    for (m = fmon; m; m = m->nmon) { /* fire, lightning and frost creatures */
        int k, kind = 0;

        if (DEADMONSTER(m) || !canseemon(m))
            continue;
        for (k = 0; k < NATTK && !kind; k++) {
            const struct attack *a = &m->data->mattk[k];

            if (a->aatyp == AT_NONE || a->aatyp == AT_ENGL || a->aatyp == AT_EXPL
                || a->aatyp == AT_BOOM || m->data->mlet == S_ELEMENTAL)
                kind = a->adtyp == AD_FIRE ? 1 : a->adtyp == AD_ELEC ? 2 : a->adtyp == AD_COLD ? 3 : 0;
        }
        if (!kind)
            continue;
        vfx_square_screen(m->mx, m->my, cx, cy, vx, vy, &sx, &sy);
        if (kind == 1)
            vhalo(sx, sy - 34, 70, 255, 120, 40, (int) (95 + 25 * sin(t * 11.0 + m->m_id)));
        else if (kind == 2)
            vhalo(sx, sy - 34, 64, 150, 180, 255, (rand() % 5) ? 70 : 140);
        else
            vhalo(sx, sy - 34, 60, 140, 200, 255, 60);
    }
    for (x = vx - 14; x <= vx + 14; x++) /* water glints and lava pulses */
        for (y = vy - 14; y <= vy + 14; y++) {
            int g, c;

            if (!isok(x, y) || !cansee(x, y))
                continue;
            g = glyph_at(x, y);
            if (!glyph_is_cmap(g))
                continue;
            c = glyph_to_cmap(g);
            if (c == S_pool || c == S_water) {
                int k;

                vfx_square_screen(x, y, cx, cy, vx, vy, &sx, &sy);
                for (k = 0; k < 3; k++) {
                    double ph = fmod(t * (0.35 + 0.1 * k) + x * 0.37 + y * 0.61 + k * 0.33, 1.0);
                    int gx = sx + (int) (24 * sin(ph * 6.2832 + k * 2.1 + x)),
                        gy = sy + (int) (9 * cos(ph * 6.2832 * 0.7 + k + y));
                    int al = (int) (150 * sin(3.14159265358979 * ph));

                    if (al > 10)
                        vfx_glowdot(gx, gy, 200, 230, 255, al);
                }
            } else if (c == S_lava || c == S_lavawall) {
                double pu = 0.5 + 0.5 * sin(t * 1.3 + x * 0.9 + y * 1.4);

                vfx_square_screen(x, y, cx, cy, vx, vy, &sx, &sy);
                vhalo(sx, sy - 6, 40, 255, 110, 30, (int) (40 + 50 * pu));
                if (pu > 0.85)
                    vfx_glowdot(sx + (int) (10 * sin(t * 3 + x)), sy - (int) (12 * (pu - 0.85) / 0.15),
                                255, 200, 90, 200);
            }
        }
}

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
        vfx_mover_screen(u.ux, u.uy, cx, cy, vx, vy, &hx2, &hy2); /* s184 */
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
            vfx_mover_screen(zm->mx, zm->my, cx, cy, vx, vy, &sx, &sy); /* s184 */
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

            vfx_mover_screen(u.ux, u.uy, cx, cy, vx, vy, &sx, &sy); /* s184 */
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

            vfx_mover_screen(u.ux, u.uy, cx, cy, vx, vy, &sx, &sy); /* s184 */
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
            if (isok(e.x, e.y)) /* s184: with the sprite, mid-step */
                sx += vtw_ox[e.y][e.x], sy += vtw_oy[e.y][e.x];
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
        vulture_frame_to_surface(shot, &r); /* s183: GPU map included */
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
        nhUse(dst);
        vulture_frame_to_surface(sc, &src); /* s107; s183: GPU map included */
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

    if (u_at(j, i)) /* s193: levitating or flying, you float a hand's breadth up */
        return (Levitation || Flying) && !u.usteed
                   ? (int) (-9.0 - 3.0 * sin(SDL_GetTicks() / 1000.0 * 2.2)) : 0;
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

static bool vsp_active(Uint32 now); /* s198 */

int
vulture_idle_wait_ms(bool at_map)
{
    nhUse(vbob_wanted);
    return at_map ? (vfx_list.empty() && !vsp_active(SDL_GetTicks()) ? 30 : 15) : 100; /* s108; s147; s198 */
}

/* one idle frame for bobbing, breathing and combat effects (called from
   the input loop while the map waits for a command) */
static void vselftest(void); /* s126 probe */

/* s182: dynamic lighting started OFF - the user asked for unlit by default
   (the light passes were the biggest CPU cost in a frame).  s189: ON again,
   normal-mapped - the user: normal maps on by default (they only show
   with dynamic lighting), and since s185 it runs on the GPU.  F9 and the
   Visual effects menu switch it; the choice is kept in effects.txt
   ("lighting="), so a saved "off" stays off. */
static bool vl_on = true;
static bool vl_gpu_frame = false; /* s185: this frame's map is lit on the GPU */
static point vck_from = { -1, -1 }, vck_to = { -1, -1 }; /* s145 probe */

/*
 * IMPLEMENTATION_LOG s198: spells, wands, breath and explosions.
 * The core draws a ray one square at a time (tmp_at, a delay_output a step)
 * and an explosion as a 3x3 of glyphs for ~100 ms.  Vulture drew every ray
 * with the same orange zigzag, whatever it was, and four of the seven
 * explosion types with the fire sprites; nothing moved between the core's
 * steps.  Each square a ray or an explosion covers is recorded here
 * (vsp_glyph, from mapdata::set_glyph) with its type and when it came and
 * went, and:
 *  - a ray is drawn as light instead of a sprite (vsp_draw_ray): a core and
 *    a halo in its colour, brightest at its head, each type moving its own
 *    way - lightning re-forks ~22 times a second, fire licks, frost
 *    glitters, gas billows, magic missiles pulse, death is a dark core in a
 *    violet haze;
 *  - it lights the room in that colour (vsp_light, from vl_compute), so
 *    walls and floor catch it and shadows swing as the head passes, and
 *    every square glows on for a moment after the ray has gone (vl_flash);
 *  - sparks, embers, frost, droplets, gas and smoke fly off and keep moving
 *    after the core has cleared the glyphs (vsp_parts);
 *  - an explosion flashes the room, its sprites pop in, a shockwave ring
 *    runs across the floor, debris flies, the camera jolts;
 *  - vulture_fx_pump draws frames during the core's delay_output while any
 *    of it is alive, and the idle loop runs at 60 fps meanwhile.
 */
struct vsp_cell_t {
    unsigned char on, kind, type, dir; /* kind: 1 ray, 2 explosion, 3 wand flash, 4 digging, 5 sparkle */
    Uint32 t_on, t_off;
};
static vsp_cell_t vsp_cells[ROWNO][COLNO];
static int vsp_on_count = 0;  /* squares shown right now */
static Uint32 vsp_live_t = 0; /* when one last came or went */
static d_level vsp_lev = { -1, -1 };

struct vsp_part_t {
    short sqx, sqy;        /* the square it is anchored to */
    float ox, oy, oz;      /* px from that square's floor centre; oz = height */
    float vx, vy, vz;      /* px a second */
    float grav, drag;      /* px/s/s on vz (negative falls); velocity lost a second */
    float size, grow;      /* radius px; px a second */
    Uint32 t0, dur;
    Uint8 r, g, b, mode;   /* 0 spark, 1 soft glow, 2 dark wisp, 3 smoke, 4 twinkle */
    Uint8 alpha;
};
static std::vector<vsp_part_t> vsp_parts;
#define VSP_MAX_PARTS 900

struct vsp_wave_t {
    int x, y;
    Uint32 t0, dur;
    Uint8 r, g, b;
    float rad; /* squares */
};
static std::vector<vsp_wave_t> vsp_waves;
static int vsp_ex_x = -1, vsp_ex_y = -1; /* the last explosion's centre */
static Uint32 vsp_ex_t = 0;
static int vsp_cx = 0, vsp_cy = 0, vsp_vx = 0, vsp_vy = 0; /* this frame's map centre and view */

struct vsp_style_t {
    Uint8 cr, cg, cb, hr, hg, hb; /* core, halo */
    float li, lr;                 /* the light: intensity, radius in squares */
};
static const vsp_style_t vsp_ray_st[NUM_ZAP] = {
    { 225, 236, 255, 80, 130, 255, 1.05f, 2.9f },  /* magic missile */
    { 255, 236, 170, 255, 105, 25, 1.30f, 3.2f },  /* fire */
    { 236, 250, 255, 120, 200, 255, 1.05f, 3.0f }, /* cold */
    { 245, 215, 255, 170, 105, 255, 0.85f, 2.8f }, /* sleep */
    { 30, 8, 40, 150, 60, 210, 0.35f, 2.2f },      /* death, disintegration: a dark core */
    { 255, 255, 255, 140, 175, 255, 1.50f, 3.5f }, /* lightning */
    { 215, 255, 140, 90, 205, 40, 0.80f, 2.8f },   /* poison gas */
    { 235, 255, 150, 160, 235, 40, 0.90f, 2.8f },  /* acid */
};
static const vsp_style_t vsp_expl_st[EXPL_MAX] = {
    { 200, 150, 255, 110, 40, 190, 0.55f, 4.5f },  /* dark */
    { 210, 255, 150, 90, 200, 50, 1.00f, 5.0f },   /* noxious */
    { 230, 200, 150, 150, 105, 55, 0.80f, 4.5f },  /* muddy */
    { 220, 240, 255, 70, 140, 255, 1.00f, 5.0f },  /* wet */
    { 255, 210, 255, 220, 90, 255, 1.30f, 5.5f },  /* magical */
    { 255, 240, 190, 255, 120, 30, 1.60f, 6.0f },  /* fiery */
    { 240, 250, 255, 150, 210, 255, 1.30f, 5.5f }, /* frosty */
};
/* a ray glyph's direction: | - \ / */
static const int vsp_dx[4] = { 0, 1, 1, -1 }, vsp_dy[4] = { 1, 0, 1, 1 };

static double
vsp_frand(void)
{
    return (rand() % 10000) / 10000.0;
}

static double
vsp_hash(int a, int b, int c) /* 0..1, steady for the same three numbers */
{
    unsigned h = (unsigned) a * 73856093u ^ (unsigned) b * 19349663u ^ (unsigned) c * 83492791u;

    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFFFFu) / 16777216.0;
}

static void
vsp_screen(int x, int y, int *sx, int *sy)
{
    vfx_square_screen(x, y, vsp_cx, vsp_cy, vsp_vx, vsp_vy, sx, sy);
}

/* soft round discs, white: [0] added (glow), [1] blended (shade); radius
   1..8 px kept as they are, [.][0] a 127 px one scaled for bigger ones */
static SDL_Surface *vsp_disc[2][9];

static SDL_Surface *
vsp_make_disc(int rad, bool add)
{
    int n = rad * 2 + 1, x, y;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_ARGB8888);

    if (!s)
        return NULL;
    SDL_LockSurface(s);
    for (y = 0; y < n; y++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + y * s->pitch);

        for (x = 0; x < n; x++) {
            double dx = (x - rad) / (rad + 0.5), dy = (y - rad) / (rad + 0.5), r2 = dx * dx + dy * dy;
            double a = r2 >= 1.0 ? 0.0 : pow(1.0 - r2, add ? 1.8 : 1.2);

            row[x] = ((Uint32) (a * 255.0 + 0.5) << 24) | 0x00FFFFFFu;
        }
    }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    return s;
}

/* a soft glow (add) or shade (!add) of radius rad px at (x, y); aspect < 1
   flattens it onto the floor */
static void
vsp_blob(int x, int y, double rad, int r, int g, int b, int alpha, bool add, double aspect = 1.0)
{
    SDL_Surface *s;
    SDL_Rect d;
    int ir = (int) (rad + 0.5), k = add ? 0 : 1;

    if (alpha <= 2 || rad < 0.5)
        return;
    if (alpha > 255)
        alpha = 255;
    if (ir <= 8 && aspect == 1.0) {
        if (ir < 1)
            ir = 1;
        if (!vsp_disc[k][ir] && !(vsp_disc[k][ir] = vsp_make_disc(ir, add)))
            return;
        s = vsp_disc[k][ir];
        d.x = x - ir, d.y = y - ir, d.w = s->w, d.h = s->h;
        SDL_SetSurfaceColorMod(s, (Uint8) r, (Uint8) g, (Uint8) b);
        SDL_SetSurfaceAlphaMod(s, (Uint8) alpha);
        SDL_BlitSurface(s, NULL, vulture_screen, &d);
        return;
    }
    if (!vsp_disc[k][0] && !(vsp_disc[k][0] = vsp_make_disc(63, add)))
        return;
    s = vsp_disc[k][0];
    d.w = (int) (rad * 2), d.h = (int) (rad * 2 * aspect);
    if (d.w < 2 || d.h < 2)
        return;
    d.x = x - d.w / 2, d.y = y - d.h / 2;
    SDL_SetSurfaceColorMod(s, (Uint8) r, (Uint8) g, (Uint8) b);
    SDL_SetSurfaceAlphaMod(s, (Uint8) alpha);
    SDL_BlitScaled(s, NULL, vulture_screen, &d);
}

/* soft blobs every `step` px from (x0, y0) to (x1, y1) */
static void
vsp_line(double x0, double y0, double x1, double y1, double rad, int r, int g, int b, int alpha,
         bool add, double step)
{
    double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy);
    int k, n = (int) (len / step) + 1;

    for (k = 0; k <= n; k++) {
        double f = (double) k / n;

        vsp_blob((int) (x0 + dx * f), (int) (y0 + dy * f), rad, r, g, b, alpha, add);
    }
}

static void
vsp_spawn(int sqx, int sqy, double ox, double oy, double oz, double vx, double vy, double vz,
          double grav, double drag, double size, double grow, int dur, int r, int g, int b,
          int mode, int alpha)
{
    vsp_part_t p;

    if (vsp_parts.size() >= VSP_MAX_PARTS)
        return;
    p.sqx = (short) sqx, p.sqy = (short) sqy;
    p.ox = (float) ox, p.oy = (float) oy, p.oz = (float) oz;
    p.vx = (float) vx, p.vy = (float) vy, p.vz = (float) vz;
    p.grav = (float) grav, p.drag = (float) drag, p.size = (float) size, p.grow = (float) grow;
    p.t0 = SDL_GetTicks(), p.dur = (Uint32) (dur < 50 ? 50 : dur);
    p.r = (Uint8) r, p.g = (Uint8) g, p.b = (Uint8) b, p.mode = (Uint8) mode, p.alpha = (Uint8) alpha;
    vsp_parts.push_back(p);
}

/* n particles of ray type ty off square (x, y), at chest height;
   lingering: what is left when the ray has gone */
static void
vsp_emit_ray(int x, int y, int ty, int n, bool lingering)
{
    const vsp_style_t &s = vsp_ray_st[ty];
    int k;

    for (k = 0; k < n; k++) {
        double ox = vsp_frand() * 30 - 15, oy = vsp_frand() * 14 - 7;
        double a = vsp_frand() * 6.2832, sp = 20 + vsp_frand() * 60;

        switch (ty) {
        case 1: /* fire: embers rising; smoke when it has gone */
            vsp_spawn(x, y, ox, oy, 28, cos(a) * sp * 0.4, sin(a) * sp * 0.2, 40 + vsp_frand() * 70,
                      -30, 0.6, 1.6, 0, 500 + rand() % 500, 255, 140 + rand() % 90, 40, 0, 230);
            if (lingering && k % 2 == 0)
                vsp_spawn(x, y, ox, oy, 30, 0, 0, 22 + vsp_frand() * 18, 0, 0.3, 4, 9, 1100 + rand() % 500,
                          60, 55, 52, 3, 70);
            break;
        case 2: /* cold: frost glittering down */
            vsp_spawn(x, y, ox, oy, 26 + vsp_frand() * 10, cos(a) * 12, sin(a) * 6, -8 - vsp_frand() * 14,
                      0, 0.5, 1.2, 0, 600 + rand() % 600, 220, 245, 255, 4, 255);
            if (lingering && k % 2 == 0)
                vsp_spawn(x, y, ox, oy, 12, 0, 0, 4, 0, 0.3, 6, 10, 900, 170, 210, 255, 1, 40);
            break;
        case 3: /* sleep: slow motes */
            vsp_spawn(x, y, ox, oy, 28, cos(a) * 8, sin(a) * 4, 6 + vsp_frand() * 8, 0, 0.2, 1.6, 0,
                      1000 + rand() % 600, s.hr, s.hg, s.hb, 4, 230);
            break;
        case 4: /* death: dark wisps curling up, a few violet sparks */
            vsp_spawn(x, y, ox, oy, 26, cos(a) * 8, sin(a) * 4, 18 + vsp_frand() * 14, 0, 0.3, 4, 6,
                      800 + rand() % 400, 0, 0, 0, 2, 110);
            vsp_spawn(x, y, ox, oy, 26, cos(a) * 14, sin(a) * 7, 20, 0, 0.4, 1.2, 0, 500, s.hr, s.hg, s.hb, 0, 200);
            break;
        case 5: /* lightning: sparks thrown off */
            vsp_spawn(x, y, ox * 0.4, oy * 0.4, 28, cos(a) * sp * 2.2, sin(a) * sp * 1.1, 40 + vsp_frand() * 90,
                      -260, 2.0, 1.3, 0, 260 + rand() % 220, 220, 230, 255, 0, 255);
            break;
        case 6: /* poison gas: billows */
            vsp_spawn(x, y, ox, oy, 24, cos(a) * 10, sin(a) * 5, 6, 0, 0.4, 6, 12, 1000 + rand() % 600,
                      s.hr, s.hg, s.hb, 1, 55);
            break;
        case 7: /* acid: drops falling */
            vsp_spawn(x, y, ox, oy, 26, cos(a) * 18, sin(a) * 9, 10, -320, 0.2, 1.4, 0, 700, 200, 255, 60, 0, 230);
            break;
        default: /* magic missile: blue-white sparkles */
            vsp_spawn(x, y, ox, oy, 28, cos(a) * sp, sin(a) * sp * 0.5, 20 + vsp_frand() * 40, -60, 1.2, 1.3, 0,
                      380 + rand() % 300, 170, 200, 255, 0, 255);
            break;
        }
    }
}

/* n pieces of explosion type ty thrown from square (x, y) (big: its centre) */
static void
vsp_emit_expl(int x, int y, int ty, int n, bool big)
{
    const vsp_style_t &s = vsp_expl_st[ty];
    int k;

    for (k = 0; k < n; k++) {
        double a = vsp_frand() * 6.2832, sp = (big ? 60 : 25) + vsp_frand() * (big ? 160 : 60);
        double ox = vsp_frand() * 20 - 10, oy = vsp_frand() * 10 - 5;
        double vx = cos(a) * sp, vy = sin(a) * sp * 0.5;

        switch (ty) {
        case EXPL_FIERY:
            vsp_spawn(x, y, ox, oy, 20, vx, vy, 60 + vsp_frand() * 150, -220, 0.9, 1.8, 0, 600 + rand() % 600,
                      255, 120 + rand() % 120, 30, 0, 255);
            break;
        case EXPL_FROSTY:
            vsp_spawn(x, y, ox, oy, 20, vx, vy, 50 + vsp_frand() * 120, -300, 0.6, 1.6, 0, 700 + rand() % 500,
                      225, 245, 255, 4, 255);
            break;
        case EXPL_WET:
            vsp_spawn(x, y, ox, oy, 18, vx, vy, 80 + vsp_frand() * 120, -420, 0.3, 1.7, 0, 700, 140, 190, 255, 0, 220);
            break;
        case EXPL_MUDDY:
            vsp_spawn(x, y, ox, oy, 18, vx, vy, 70 + vsp_frand() * 110, -420, 0.3, 2.2, 0, 700, 120, 85, 45, 3, 200);
            break;
        case EXPL_NOXIOUS:
            vsp_spawn(x, y, ox, oy, 16, vx * 0.35, vy * 0.35, 10, 0, 0.8, 6, 16, 1200 + rand() % 600,
                      s.hr, s.hg, s.hb, 1, 60);
            break;
        case EXPL_MAGICAL:
            vsp_spawn(x, y, ox, oy, 22, vx, vy, 40 + vsp_frand() * 120, -80, 1.2, 1.5, 0, 700 + rand() % 500,
                      200 + rand() % 55, 120 + rand() % 100, 255, 4, 255);
            break;
        default: /* dark */
            vsp_spawn(x, y, ox, oy, 16, vx * 0.4, vy * 0.4, 16, 0, 0.8, 6, 12, 1000 + rand() % 500, 0, 0, 0, 2, 120);
            vsp_spawn(x, y, ox, oy, 18, vx, vy, 40, -100, 1.0, 1.3, 0, 600, s.hr, s.hg, s.hb, 0, 230);
            break;
        }
    }
}

/* what an explosion leaves on a square: smoke, mist, gas, dust */
static void
vsp_emit_linger(int x, int y, int ty)
{
    const vsp_style_t &s = vsp_expl_st[ty];
    int k;

    for (k = 0; k < 3; k++) {
        double ox = vsp_frand() * 36 - 18, oy = vsp_frand() * 16 - 8;

        switch (ty) {
        case EXPL_FIERY:
            vsp_spawn(x, y, ox, oy, 24, 0, 0, 16 + vsp_frand() * 16, 0, 0.2, 6, 10, 1400 + rand() % 600,
                      50, 46, 44, 3, 90);
            break;
        case EXPL_FROSTY:
            vsp_spawn(x, y, ox, oy, 10, 0, 0, 3, 0, 0.2, 9, 10, 1300, 190, 225, 255, 1, 45);
            break;
        case EXPL_NOXIOUS:
            vsp_spawn(x, y, ox, oy, 14, 0, 0, 4, 0, 0.2, 9, 12, 1600, s.hr, s.hg, s.hb, 1, 50);
            break;
        case EXPL_MUDDY:
            vsp_spawn(x, y, ox, oy, 8, 0, 0, 6, 0, 0.2, 7, 9, 1100, 110, 85, 55, 3, 80);
            break;
        case EXPL_WET:
            vsp_spawn(x, y, ox, oy, 8, 0, 0, 2, 0, 0.2, 8, 8, 900, 150, 200, 255, 1, 35);
            break;
        case EXPL_MAGICAL:
            vsp_spawn(x, y, ox, oy, 20, 0, 0, 14, 0, 0.3, 1.4, 0, 900 + rand() % 400, 255, 180, 255, 4, 255);
            break;
        default:
            vsp_spawn(x, y, ox, oy, 16, 0, 0, 10, 0, 0.2, 7, 8, 1200, 0, 0, 0, 2, 100);
            break;
        }
    }
}

/* a quick camera jolt from an explosion at (x, y), weaker far from you */
static void
vsp_jolt(int x, int y, double amp)
{
    Uint32 now = SDL_GetTicks();
    int d = distmin(u.ux, u.uy, x, y);
    double a = amp * (d <= 2 ? 1.0 : d >= 12 ? 0.0 : (12 - d) / 10.0);

    if (!vfx_jolts || a <= 0.0)
        return;
    if (now - vimp_t0 > 260 || a > vimp_amp * (1.0 - (now - vimp_t0) / 260.0))
        vimp_amp = a, vimp_t0 = now;
}

/* a square started showing a ray / explosion / ... */
static void
vsp_start(int x, int y, int kind, int ty, int dir)
{
    if (kind == 1) {
        vsp_emit_ray(x, y, ty, ty == 5 ? 5 : 2, false);
    } else if (kind == 2) {
        const vsp_style_t &s = vsp_expl_st[ty];
        int cx = x - (dir % 3 - 1), cy = y - (dir / 3 - 1);
        Uint32 now = SDL_GetTicks();

        if (cx != vsp_ex_x || cy != vsp_ex_y || now - vsp_ex_t > 400) { /* a new explosion */
            vsp_wave_t w;

            vsp_ex_x = cx, vsp_ex_y = cy, vsp_ex_t = now;
            vl_flash(cx, cy, s.hr / 255.0f, s.hg / 255.0f, s.hb / 255.0f, s.li, s.lr, 950);
            w.x = cx, w.y = cy, w.t0 = now, w.dur = 560, w.r = s.cr, w.g = s.cg, w.b = s.cb, w.rad = 2.6f;
            if (vsp_waves.size() < 16)
                vsp_waves.push_back(w);
            vsp_emit_expl(cx, cy, ty, 44, true);
            vsp_jolt(cx, cy, ty == EXPL_FIERY || ty == EXPL_MAGICAL ? 7.0 : 5.0);
        }
        vsp_emit_expl(x, y, ty, 5, false);
    } else if (kind == 3) {
        vl_flash(x, y, 0.80f, 0.88f, 1.0f, 0.55f, 2.2f, 260);
        vsp_emit_ray(x, y, 0, 2, false);
    } else if (kind == 4) {
        int k;

        for (k = 0; k < 3; k++) {
            double a = vsp_frand() * 6.2832;

            vsp_spawn(x, y, vsp_frand() * 24 - 12, vsp_frand() * 10 - 5, 14, cos(a) * 20, sin(a) * 10, 10, 0,
                      0.6, 4, 8, 700, 140, 120, 95, 3, 90);
        }
    }
}

/* ... and stopped: it glows on a moment and leaves something behind */
static void
vsp_end(int x, int y, int kind, int ty)
{
    if (kind == 1) {
        const vsp_style_t &s = vsp_ray_st[ty];

        vl_flash(x, y, s.hr / 255.0f, s.hg / 255.0f, s.hb / 255.0f, s.li * 0.55f, s.lr * 0.85f, 600);
        vsp_emit_ray(x, y, ty, ty == 1 || ty == 2 ? 3 : 2, true);
    } else if (kind == 2) {
        const vsp_style_t &s = vsp_expl_st[ty];

        vl_flash(x, y, s.hr / 255.0f, s.hg / 255.0f, s.hb / 255.0f, s.li * 0.35f, 2.2f, 650);
        vsp_emit_linger(x, y, ty);
    }
}

static void
vsp_reset(void)
{
    memset(vsp_cells, 0, sizeof vsp_cells);
    vsp_on_count = 0;
    vsp_parts.clear();
    vsp_waves.clear();
    vsp_ex_x = vsp_ex_y = -1;
}

static void
vsp_level_check(void)
{
    if (!on_level(&vsp_lev, &u.uz)) {
        vsp_lev = u.uz;
        vsp_reset();
    }
}

/* mapdata::set_glyph: the glyph shown on (x, y) changed */
void
vsp_glyph(int x, int y, int glyph)
{
    int kind = 0, ty = 0, dir = 0;
    Uint32 now;
    vsp_cell_t *c;

    if (x < 1 || x >= COLNO || y < 0 || y >= ROWNO || !u.ux)
        return;
    vsp_level_check();
    c = &vsp_cells[y][x];
    if (iflags.window_inited && cansee(x, y)) {
        if (glyph_is_cmap_zap(glyph)) {
            kind = 1, ty = (glyph - GLYPH_ZAP_OFF) >> 2, dir = (glyph - GLYPH_ZAP_OFF) & 3;
        } else if (glyph_is_explosion(glyph)) {
            kind = 2, ty = (glyph - GLYPH_EXPLODE_OFF) / MAXEXPCHARS;
            dir = (glyph - GLYPH_EXPLODE_OFF) % MAXEXPCHARS;
        } else if (glyph_is_cmap_c(glyph)) {
            int cm = glyph_to_cmap(glyph);

            kind = cm == S_flashbeam ? 3 : cm == S_digbeam ? 4 : (cm >= S_ss1 && cm <= S_ss4) ? 5 : 0;
        }
        if ((kind == 1 && (ty < 0 || ty >= NUM_ZAP)) || (kind == 2 && (ty < 0 || ty >= EXPL_MAX)))
            kind = 0;
    }
    now = SDL_GetTicks();
    if (kind) {
        if (!c->on || c->kind != kind || c->type != ty) {
            if (!c->on)
                vsp_on_count++;
            c->on = 1, c->kind = (unsigned char) kind, c->type = (unsigned char) ty;
            c->dir = (unsigned char) dir, c->t_on = now;
            vsp_start(x, y, kind, ty, dir);
        } else if (kind == 1 && c->dir != dir) { /* it came back the other way: it bounced */
            c->dir = (unsigned char) dir, c->t_on = now;
            vsp_emit_ray(x, y, ty, 8, false);
        }
        vsp_live_t = now;
    } else if (c->on) {
        c->on = 0, c->t_off = now;
        vsp_on_count--;
        vsp_end(x, y, c->kind, c->type);
        vsp_live_t = now;
    }
}

static bool
vsp_active(Uint32 now)
{
    return vsp_on_count > 0 || !vsp_parts.empty() || !vsp_waves.empty() || now - vsp_live_t < 1000;
}

/* step 6 of the map: a ray's square is drawn as light by vsp_draw instead
   of the old sprite; an explosion's sprite pops in */
static void
vsp_put_special(int x, int y, int j, int i, int tile)
{
    if (tile == V_TILE_NONE)
        return;
    if (j >= 1 && j < COLNO && i >= 0 && i < ROWNO && vsp_cells[i][j].on) {
        const vsp_cell_t &c = vsp_cells[i][j];

        if (c.kind == 1 && tile >= V_MISC_ZAP_SLANT_RIGHT && tile <= V_MISC_ZAP_VERTICAL)
            return;
        if (c.kind == 2 && tile >= EXPTILEOFFSET && tile < EXPTILEOFFSET + EXPTILECOUNT) {
            double age = (double) (SDL_GetTicks() - c.t_on) / 1000.0;
            double sc = age < 0.08 ? 0.72 + 0.36 * age / 0.08
                        : age < 0.2 ? 1.08 - 0.08 * (age - 0.08) / 0.12 : 1.0;

            vtw_put_tile_x(x, y, tile, 0, sc, sc, 0.0, false, 255);
            return;
        }
    }
    vulture_put_tile(x, y, tile);
}

static void
vsp_draw_ray(int x, int y, const vsp_cell_t &c, Uint32 now, double t, double dt)
{
    static const double rate[NUM_ZAP] = { 9, 15, 8, 4, 6, 10, 5, 6 }; /* particles a second */
    const vsp_style_t &s = vsp_ray_st[c.type];
    double age = (double) (now - c.t_on) / 1000.0, hf = 0.55 + 0.45 * exp(-age / 0.12);
    double hx, hy, len, nx, ny, ax, ay, bx, by, py;
    int sx, sy, ex, ey, k, ty = c.type;

    vsp_screen(x, y, &sx, &sy);
    vsp_screen(x + vsp_dx[c.dir], y + vsp_dy[c.dir], &ex, &ey);
    hx = (ex - sx) * 0.5, hy = (ey - sy) * 0.5; /* half a square along the ray */
    len = sqrt(hx * hx + hy * hy);
    if (len < 1.0)
        return;
    nx = -hy / len, ny = hx / len; /* across it */
    py = sy - 28.0;                /* rays fly at chest height */
    ax = sx - hx, ay = py - hy, bx = sx + hx, by = py + hy;

    /* its glow on the floor (the light layer adds the real light) */
    vsp_blob(sx, sy, vl_on ? 34 : 46, s.hr, s.hg, s.hb, (int) ((vl_on ? 34 : 62) * hf), true, 0.5);
    if (ty == 4) { /* death: a dark core in a violet haze */
        for (k = -1; k <= 1; k++)
            vsp_blob((int) (sx + hx * 0.66 * k), (int) (py + hy * 0.66 * k), 15, s.hr, s.hg, s.hb,
                     (int) (60 * hf), true);
        vsp_line(ax, ay, bx, by, 4.5, 0, 0, 0, (int) (175 * hf), false, 3.0);
        vsp_line(ax + nx * 5, ay + ny * 5, bx + nx * 5, by + ny * 5, 2.0, s.hr, s.hg, s.hb,
                 (int) (150 * hf), true, 3.0);
        vsp_line(ax - nx * 5, ay - ny * 5, bx - nx * 5, by - ny * 5, 2.0, s.hr, s.hg, s.hb,
                 (int) (150 * hf), true, 3.0);
    } else {
        double big = ty == 3 || ty == 6 ? 25 : 21;

        for (k = -1; k <= 1; k++)
            vsp_blob((int) (sx + hx * 0.66 * k), (int) (py + hy * 0.66 * k), big, s.hr, s.hg, s.hb,
                     (int) ((ty == 5 ? 115 : 92) * hf), true);
    }
    switch (ty) {
    case 5: { /* lightning: a jagged bolt, forked anew ~22 times a second */
        int seed = (int) (t * 22.0), i;
        double px[5], pyy[5], st = 0.75 + 0.25 * vsp_hash(x, y, seed * 3 + 1);

        for (i = 0; i < 5; i++) {
            double f = -1.0 + 0.5 * i;
            double off = (i == 0 || i == 4) ? 0.0 : (vsp_hash(x * 7 + i, y, seed) - 0.5) * 18.0;

            px[i] = sx + hx * f + nx * off, pyy[i] = py + hy * f + ny * off;
        }
        for (i = 0; i < 4; i++) {
            vsp_line(px[i], pyy[i], px[i + 1], pyy[i + 1], 4.0, s.hr, s.hg, s.hb, (int) (150 * hf * st), true, 2.5);
            vsp_line(px[i], pyy[i], px[i + 1], pyy[i + 1], 1.5, 255, 255, 255, (int) (255 * hf * st), true, 1.5);
        }
        if (vsp_hash(x, y, seed * 13 + 5) > 0.55) { /* a branch off the middle */
            double side = vsp_hash(x, y, seed * 17) > 0.5 ? 1.0 : -1.0;
            double qx = px[2] + nx * side * 16 + hx * 0.4, qy = pyy[2] + ny * side * 16 + hy * 0.4;
            double rx = qx + nx * side * 10 + hx * 0.3, ry = qy + ny * side * 10 + hy * 0.3 + 6;

            vsp_line(px[2], pyy[2], qx, qy, 1.2, 220, 230, 255, (int) (200 * hf * st), true, 1.5);
            vsp_line(qx, qy, rx, ry, 1.0, 190, 210, 255, (int) (140 * hf * st), true, 1.5);
        }
        break;
    }
    case 1: /* fire: a thick licking core, white-hot inside, tongues of flame rising off it */
        for (k = 0; k <= 16; k++) {
            double f = -1.0 + k / 8.0, w = 2.5 * sin(t * 38.0 + f * 5.0 + x * 3.1 + y);
            double fl = 0.8 + 0.2 * sin(t * 47.0 + k * 1.9);
            int qx = (int) (sx + hx * f + nx * w), qy = (int) (py + hy * f + ny * w);

            vsp_blob(qx, qy, 8, 255, 110, 25, (int) (115 * hf * fl), true);
            vsp_blob(qx, qy, 4, s.cr, s.cg, s.cb, (int) (230 * hf * fl), true);
        }
        vsp_line(ax, ay, bx, by, 1.5, 255, 250, 225, (int) (170 * hf), true, 2.0);
        for (k = 0; k < 5; k++) {
            double ph0 = vsp_hash(x, y, k * 5 + 1), ph = fmod(t * 2.6 + ph0, 1.0);
            double f = vsp_hash(x, y, k * 7 + (int) (t * 2.6 + ph0)) * 2.0 - 1.0;
            int up = (int) (4 + 20 * ph);

            vsp_blob((int) (sx + hx * f), (int) (py + hy * f) - up, 7.0 * (1.0 - 0.5 * ph), 255,
                     (int) (210 - 140 * ph), (int) (60 - 40 * ph), (int) (160 * (1.0 - ph) * hf), true);
        }
        break;
    case 6: /* poison gas: a thin core in billows */
        vsp_line(ax, ay, bx, by, 2.5, s.cr, s.cg, s.cb, (int) (170 * hf), true, 3.0);
        for (k = 0; k < 3; k++) {
            double f = -0.66 + 0.66 * k, wob = 4 * sin(t * 3.0 + k * 2.0 + x);

            vsp_blob((int) (sx + hx * f + nx * wob), (int) (py + hy * f + ny * wob), 12 + 2 * sin(t * 2.0 + k),
                     s.hr, s.hg, s.hb, (int) (55 * hf), true);
        }
        break;
    case 4:
        break; /* drawn with its haze */
    default: { /* magic missile, cold, sleep, acid: a straight core */
        vsp_line(ax, ay, bx, by, ty == 3 ? 4.5 : 3.5, s.cr, s.cg, s.cb, (int) (200 * hf), true, 2.5);
        vsp_line(ax, ay, bx, by, 1.5, 255, 255, 255, (int) (150 * hf), true, 2.0);
        if (ty == 0) /* magic missile: pulses running along it */
            for (k = 0; k < 2; k++) {
                double f = fmod(t * 3.5 + k * 0.5 + (x + y) * 0.25, 1.0) * 2.0 - 1.0;

                vsp_blob((int) (sx + hx * f), (int) (py + hy * f), 5, 230, 240, 255, (int) (220 * hf), true);
            }
        if (ty == 2) /* cold: glitter */
            for (k = 0; k < 4; k++) {
                double tw = sin(t * 9.0 + vsp_hash(x, y, k) * 6.2832);
                double f = vsp_hash(x, y, k + 9) * 2.0 - 1.0, o = (vsp_hash(x, y, k + 20) - 0.5) * 20.0;

                if (tw > 0.2)
                    vsp_blob((int) (sx + hx * f + nx * o), (int) (py + hy * f + ny * o), 2, 255, 255, 255,
                             (int) (255 * tw), true);
            }
        break;
    }
    }
    /* sparks and the like while it shines (lightning throws most at its head) */
    if (vsp_frand() < rate[ty] * dt * (ty == 5 ? (age < 0.2 ? 2.0 : 0.3) : 1.0))
        vsp_emit_ray(x, y, ty, 1, false);
}

static void
vsp_draw_expl(int x, int y, const vsp_cell_t &c, Uint32 now, double t)
{
    const vsp_style_t &s = vsp_expl_st[c.type];
    double age = (double) (now - c.t_on) / 1000.0;
    int sx, sy;

    vsp_screen(x, y, &sx, &sy);
    if (c.type == EXPL_DARK)
        vsp_blob(sx, sy - 24, 30, 0, 0, 0, 120, false);
    vsp_blob(sx, sy - 24, 40, s.hr, s.hg, s.hb, (int) (110 * (0.85 + 0.15 * sin(t * 40.0 + x + y))), true);
    if (age < 0.09) /* white-hot for an instant */
        vsp_blob(sx, sy - 24, 34, 255, 255, 255, (int) (210 * (1.0 - age / 0.09)), true);
}

static void
vsp_draw_small(int x, int y, const vsp_cell_t &c, double t)
{
    int sx, sy;

    vsp_screen(x, y, &sx, &sy);
    if (c.kind == 3) /* a wand's flash */
        vsp_blob(sx, sy - 26, 20, 170, 200, 255, (int) (90 + 40 * sin(t * 30.0 + x)), true);
    else if (c.kind == 4) /* digging */
        vsp_blob(sx, sy - 10, 18, 200, 160, 110, 50, true);
    else /* resistance sparkle */
        vsp_blob(sx, sy - 26, 14, 230, 240, 255, (int) (80 + 60 * sin(t * 25.0 + x * 2 + y)), true);
}

static void
vsp_draw_waves(Uint32 now)
{
    size_t i, n = 0;

    for (i = 0; i < vsp_waves.size(); i++) {
        vsp_wave_t &w = vsp_waves[i];
        double age = (double) (now - w.t0) / w.dur, ea, rw, rh;
        int sx, sy, k, m, al;

        if (age >= 1.0)
            continue;
        vsp_waves[n++] = w;
        ea = 1.0 - (1.0 - age) * (1.0 - age) * (1.0 - age);
        rw = w.rad * 79.0 * ea, rh = w.rad * 31.0 * ea;
        al = (int) (220 * (1.0 - age) * (rw < 30.0 ? rw / 30.0 : 1.0)); /* s203: it blooms from dim */
        vsp_screen(w.x, w.y, &sx, &sy);
        m = 48 + (int) (rw * 0.6); /* close enough to read as one ring, not dots */
        { /* s203: and few enough that a small one doesn't pile up into a white-hot disc */
            int lim = (int) (6.2832 * sqrt((rw * rw + rh * rh) / 2.0) / 4.0);

            if (lim < m)
                m = lim < 10 ? 10 : lim;
        }
        for (k = 0; k < m; k++) {
            double a = 6.28318 * k / m;

            vsp_blob((int) (sx + rw * cos(a)), (int) (sy + rh * sin(a)), 4.5, w.r, w.g, w.b, al, true);
            if (k % 2 == 0)
                vsp_blob((int) (sx + rw * 0.86 * cos(a)), (int) (sy + rh * 0.86 * sin(a)), 6, w.r, w.g, w.b,
                         al / 4, true);
        }
    }
    vsp_waves.resize(n);
}

static void
vsp_draw_parts(Uint32 now, double t, double dt)
{
    size_t i, n = 0;

    for (i = 0; i < vsp_parts.size(); i++) {
        vsp_part_t p = vsp_parts[i];
        double age = (double) (now - p.t0) / p.dur, dk = exp(-p.drag * dt), a;
        int sx, sy, px, py;

        if (age >= 1.0)
            continue;
        p.vz += (float) (p.grav * dt);
        p.vx *= (float) dk, p.vy *= (float) dk;
        if (p.grav == 0.0f)
            p.vz *= (float) dk;
        p.ox += (float) (p.vx * dt), p.oy += (float) (p.vy * dt), p.oz += (float) (p.vz * dt);
        if (p.oz < 0.0f) /* on the floor: it settles */
            p.oz = 0.0f, p.vz = -p.vz * 0.25f, p.vx *= 0.5f, p.vy *= 0.5f;
        p.size += (float) (p.grow * dt);
        vsp_parts[n++] = p;
        vsp_screen(p.sqx, p.sqy, &sx, &sy);
        px = sx + (int) p.ox, py = sy + (int) p.oy - (int) p.oz;
        switch (p.mode) {
        case 0: /* a spark */
            a = p.alpha * pow(1.0 - age, 1.3);
            vsp_blob(px, py, p.size, p.r, p.g, p.b, (int) a, true);
            if (p.size >= 1.2)
                vsp_blob(px, py, 1, 255, 255, 255, (int) (a * 0.6), true);
            break;
        case 4: /* a twinkle */
            a = p.alpha * (1.0 - age) * (0.35 + 0.65 * fabs(sin(t * 13.0 + (p.t0 % 97) * 0.37)));
            vsp_blob(px, py, p.size, p.r, p.g, p.b, (int) a, true);
            break;
        case 5: /* s202: a solid bit - chips, splinters, drops */
            vsp_blob(px, py, p.size, p.r, p.g, p.b, (int) (p.alpha * (1.0 - age)), false);
            break;
        case 1: /* gas, mist */
            vsp_blob(px, py, p.size, p.r, p.g, p.b, (int) (p.alpha * sin(3.14159265 * age)), true);
            break;
        default: /* 2 dark wisp, 3 smoke: shading */
            vsp_blob(px, py, p.size, p.r, p.g, p.b, (int) (p.alpha * sin(3.14159265 * age)), false);
            break;
        }
    }
    vsp_parts.resize(n);
}


/*
 * IMPLEMENTATION_LOG s202: impact.  A blow that lands, a missile that
 * strikes, a thrown thing that stops, a flask that shatters - each shows
 * where and how it hit, not only the number and the blood:
 *  - every hit on a creature in sight (vfx_scan sees its HP drop) bursts at
 *    the point of contact in the style of what struck it - an edged weapon
 *    sweeps a bright arc and throws sparks, a pointed one jabs a streak
 *    through, a blunt one flashes and rings the floor with dust, claws rake
 *    three red lines, a bite snaps shut, fire / frost / lightning burst in
 *    their colour - sprayed away from the attacker (vulture_anim_attack
 *    says who swung; a missile's flight says where from); the room flashes
 *    with it and the creature flinches from the blow;
 *  - a miss ("You miss", "misses") whiffs past the target, who leans aside;
 *  - thrown and fired things (the core's tmp_at DISP_FLASH / TETHER, via
 *    vulture_flight_*) streak through the air, and one stopped by a wall
 *    chips stone, throws sparks (metal) or splinters (wood);
 *  - potions and glass shatter (breakobj, potionhit): shards, and a splash
 *    in the potion's colour (the colour you see).
 */
enum { VIMP_GENERIC, VIMP_SLASH, VIMP_PIERCE, VIMP_BLUNT, VIMP_CLAW, VIMP_BITE, VIMP_FIRE,
       VIMP_COLD, VIMP_SHOCK };
struct vimp_attack_t {
    int ax, ay, tx, ty, style;
    Uint32 t;
};
static vimp_attack_t vimp_last = { 0, 0, 0, 0, 0, 0 };
struct vimp_fx_t {
    int x, y, style;
    float dx, dy, size; /* the blow's screen direction (unit, or 0), its size */
    Uint32 t0, dur;
    bool whiff;
};
static std::vector<vimp_fx_t> vimp_fx;
struct vimp_rec_t { /* a creature flinching from a blow, or leaning from a miss */
    int x, y;
    float dx, dy, amp;
    Uint32 t0, dur;
    bool dodge;
};
static std::vector<vimp_rec_t> vimp_recs;
struct vfl_pt_t {
    int x, y;
    Uint32 t;
};
static std::vector<vfl_pt_t> vfl_pts; /* the flight in the air, or just over */
static bool vfl_on = false;
static int vfl_style = VIMP_GENERIC, vfl_mat = 0; /* 0 other, 1 metal, 2 wood, 3 stone, 4 glass */
/* s207: the arc - the range it was given, its height, whether fired from a
   launcher (flat), the thrower's square (-1: not told), the thing's tile */
static int vfl_next_range = 0, vfl_next_fired = 0, vfl_next_sx = -1, vfl_next_sy = -1;
static double vfl_arc_r = 6.0, vfl_arc_h = 20.0, vfl_step_ms = 45.0;
static int vfl_fired = 0, vfl_start_x = -1, vfl_start_y = -1, vfl_tile = -1;
static bool vfl_point = false; /* s222: flies point first (arrows, bolts, darts, spears, daggers...) */
static double vfl_art = 0.0;   /* s222: which way its art points (radians, screen) */
static Uint32 vfl_end_t = 0;
static int vfl_end_x = -1, vfl_end_y = -1;
static float vfl_end_dx = 0.0f, vfl_end_dy = 0.0f;
static const Uint8 vimp_col[9][3] = {
    { 255, 245, 220 }, { 255, 240, 200 }, { 230, 240, 255 }, { 255, 235, 205 }, { 255, 110, 90 },
    { 255, 200, 200 }, { 255, 150, 50 },  { 170, 220, 255 }, { 200, 215, 255 },
};
/* NetHack's 16 colours, as the map shows them */
static const Uint8 vimp_nhrgb[16][3] = {
    { 60, 60, 70 },   { 205, 40, 35 },  { 50, 170, 50 },   { 160, 105, 45 },
    { 60, 80, 225 },  { 190, 55, 190 }, { 45, 180, 190 },  { 175, 175, 175 },
    { 200, 200, 200 }, { 255, 140, 35 }, { 100, 255, 100 }, { 255, 245, 80 },
    { 110, 150, 255 }, { 255, 100, 255 }, { 100, 255, 255 }, { 255, 255, 255 },
};

/* how a thing strikes: its weapon's damage type, or the creature's first
   melee attack */
static int
vimp_style_of(int ax, int ay)
{
    struct monst *m = NULL;
    struct obj *w = NULL;
    const struct permonst *pd = NULL;
    int k;

    if (u_at(ax, ay))
        w = uwep, pd = Upolyd ? gy.youmonst.data : NULL;
    else if ((m = m_at(ax, ay)) != 0)
        w = MON_WEP(m), pd = m->data;
    if (w && (w->oclass == WEAPON_CLASS || is_weptool(w))) {
        int d = objects[w->otyp].oc_dir;

        return (d & SLASH) ? VIMP_SLASH : (d & PIERCE) ? VIMP_PIERCE : VIMP_BLUNT;
    }
    if (w)
        return VIMP_BLUNT; /* a tool, a corpse... */
    if (pd)
        for (k = 0; k < NATTK; k++) {
            const struct attack *a = &pd->mattk[k];

            if (a->aatyp == AT_NONE || a->aatyp == AT_BOOM || a->aatyp == AT_EXPL
                || a->aatyp == AT_BREA || a->aatyp == AT_SPIT || a->aatyp == AT_GAZE
                || a->aatyp == AT_MAGC || a->aatyp == AT_ENGL)
                continue;
            if (a->adtyp == AD_FIRE)
                return VIMP_FIRE;
            if (a->adtyp == AD_COLD)
                return VIMP_COLD;
            if (a->adtyp == AD_ELEC)
                return VIMP_SHOCK;
            if (a->aatyp == AT_CLAW)
                return VIMP_CLAW;
            if (a->aatyp == AT_BITE)
                return VIMP_BITE;
            if (a->aatyp == AT_STNG)
                return VIMP_PIERCE;
            return VIMP_BLUNT;
        }
    return VIMP_BLUNT; /* fists */
}

/* vulture_anim_attack: who swings at whom */
static void
vimp_note_attack(int ax, int ay, int tx, int ty)
{
    vimp_last.ax = ax, vimp_last.ay = ay, vimp_last.tx = tx, vimp_last.ty = ty;
    vimp_last.style = vimp_style_of(ax, ay);
    vimp_last.t = SDL_GetTicks();
}

/* a unit screen vector from square (ax, ay) toward (tx, ty) */
static void
vimp_dir(int ax, int ay, int tx, int ty, float *dx, float *dy)
{
    int sx, sy;
    double l;

    vtw_proj(tx - ax, ty - ay, &sx, &sy);
    l = sqrt((double) sx * sx + (double) sy * sy);
    *dx = l > 0.5 ? (float) (sx / l) : 0.0f;
    *dy = l > 0.5 ? (float) (sy / l) : 0.0f;
}

/* vfx_scan: a creature in sight just lost HP */
static void
vimp_hit(int x, int y, int dmg, int maxhp, bool hero)
{
    Uint32 now = SDL_GetTicks();
    vimp_fx_t f;
    vimp_rec_t r;
    float dx = 0.0f, dy = 0.0f;
    int style = VIMP_GENERIC, n, k;
    bool known = false;
    double big = maxhp > 0 ? (double) dmg / maxhp : 0.3;

    if (!isok(x, y) || dmg <= 0)
        return;
    if (now - vimp_last.t < 1500 && vimp_last.tx == x && vimp_last.ty == y) {
        style = vimp_last.style, known = true;
        vimp_dir(vimp_last.ax, vimp_last.ay, x, y, &dx, &dy);
        vimp_last.t = 0; /* one blow, one burst */
    } else if (now - vfl_end_t < 800 && distmin(vfl_end_x, vfl_end_y, x, y) <= 1) {
        style = vfl_style, dx = vfl_end_dx, dy = vfl_end_dy, known = true;
        vfl_end_t = 0;
    }
    if (hero && !known) /* hunger, poison, a trap: no blow to show */
        return;
    if (big > 0.6)
        big = 0.6;
    f.x = x, f.y = y, f.style = style, f.dx = dx, f.dy = dy;
    f.size = (float) (0.7 + big * 1.4);
    f.t0 = now, f.dur = style == VIMP_BLUNT ? 300 : 240, f.whiff = false;
    if (vimp_fx.size() < 24)
        vimp_fx.push_back(f);
    vl_flash(x, y, vimp_col[style][0] / 255.0f, vimp_col[style][1] / 255.0f, vimp_col[style][2] / 255.0f,
             style >= VIMP_FIRE ? 0.9f : 0.55f, 1.8f, 220);
    /* sparks, grit, flecks - sprayed away from the attacker */
    n = 5 + (int) (big * 18.0);
    for (k = 0; k < n; k++) {
        double a = vsp_frand() * 6.2832, sp = 60 + vsp_frand() * 140;
        double vx = cos(a) * sp * 0.6 + dx * sp, vy = sin(a) * sp * 0.3 + dy * sp * 0.5;

        switch (style) {
        case VIMP_SLASH:
        case VIMP_PIERCE:
        case VIMP_GENERIC: /* sparks */
            vsp_spawn(x, y, dx * 6, dy * 3, 30, vx * 1.3, vy * 1.3, 40 + vsp_frand() * 90, -300, 2.4, 1.2, 0,
                      200 + rand() % 220, 255, 230 + rand() % 25, 170 + rand() % 60, 0, 255);
            break;
        case VIMP_BLUNT: /* grit */
            vsp_spawn(x, y, dx * 4, dy * 2, 18 + vsp_frand() * 10, vx * 0.5, vy * 0.5, 30 + vsp_frand() * 40,
                      -260, 1.6, 1.4, 0, 300 + rand() % 250, 200, 185, 160, 5, 220);
            break;
        case VIMP_CLAW:
        case VIMP_BITE: /* red flecks (the blood is vfx_hit's) */
            vsp_spawn(x, y, dx * 4, dy * 2, 28, vx * 0.9, vy * 0.9, 30 + vsp_frand() * 60, -320, 1.5, 1.3, 0,
                      260 + rand() % 200, 210, 40 + rand() % 40, 40, 5, 230);
            break;
        default: /* fire, cold, shock: the s198 particles of that kind */
            vsp_emit_ray(x, y, style == VIMP_FIRE ? 1 : style == VIMP_COLD ? 2 : 5, 1, false);
            break;
        }
    }
    if (style == VIMP_BLUNT) { /* a small shockwave along the floor */
        vsp_wave_t w;

        w.x = x, w.y = y, w.t0 = now, w.dur = 300, w.r = 255, w.g = 240, w.b = 210;
        w.rad = (float) (0.35 + big * 0.7);
        if (vsp_waves.size() < 16)
            vsp_waves.push_back(w);
    }
    /* the creature flinches from the blow */
    r.x = x, r.y = y, r.dx = dx, r.dy = dy, r.amp = (float) (5.0 + big * 12.0);
    r.t0 = now, r.dur = 170, r.dodge = false;
    if (dx == 0.0f && dy == 0.0f) /* no direction known: a jolt upward */
        r.dy = -1.0f, r.amp *= 0.5f;
    if (vimp_recs.size() < 24)
        vimp_recs.push_back(r);
    vsp_live_t = now; /* frames keep coming */
}

/* vulture_anim_death: a creature in sight died - if a blow or a missile we
   saw did it, that blow lands as hard as a big hit (deaths from poison,
   traps, hunger show nothing) */
static void
vimp_kill(int x, int y, int maxhp)
{
    if (maxhp < 1)
        maxhp = 1;
    if (cansee(x, y))
        vimp_hit(x, y, (maxhp + 1) / 2, maxhp, true);
}

/* vanim_xform: this frame's flinch or lean for the creature on (j, i) */
static void
vimp_recoil_xform(int j, int i, int *ox, int *oy, double *deg)
{
    Uint32 now = SDL_GetTicks();
    size_t k;

    for (k = 0; k < vimp_recs.size(); k++) {
        const vimp_rec_t &r = vimp_recs[k];
        double age = (double) (now - r.t0) / r.dur, e;

        if (r.x != j || r.y != i || age >= 1.0)
            continue;
        /* out fast, back slower */
        e = sin(3.14159265 * (age < 0.3 ? age / 0.6 : 0.5 + (age - 0.3) / 1.4));
        if (r.dodge) { /* across the swing */
            *ox += (int) (-r.dy * r.amp * e);
            *oy += (int) (r.dx * r.amp * 0.5 * e);
            *deg += 8.0 * e * (r.dx >= 0.0f ? 1.0 : -1.0);
        } else {
            *ox += (int) (r.dx * r.amp * e);
            *oy += (int) (r.dy * r.amp * e);
            *deg += 6.0 * e * (r.dx >= 0.0f ? 1.0 : -1.0);
        }
    }
}

/* a miss: a whiff past the target, who leans aside */
static void
vimp_whiff(void)
{
    Uint32 now = SDL_GetTicks();
    vimp_fx_t f;
    vimp_rec_t r;
    float dx, dy;

    if (now - vimp_last.t > 1500 || !isok(vimp_last.tx, vimp_last.ty) || !cansee(vimp_last.tx, vimp_last.ty))
        return;
    vimp_dir(vimp_last.ax, vimp_last.ay, vimp_last.tx, vimp_last.ty, &dx, &dy);
    f.x = vimp_last.tx, f.y = vimp_last.ty, f.style = vimp_last.style, f.dx = dx, f.dy = dy;
    f.size = 0.8f, f.t0 = now, f.dur = 230, f.whiff = true;
    if (vimp_fx.size() < 24)
        vimp_fx.push_back(f);
    r.x = f.x, r.y = f.y, r.dx = dx, r.dy = dy, r.amp = 7.0f, r.t0 = now, r.dur = 230, r.dodge = true;
    if (vimp_recs.size() < 24)
        vimp_recs.push_back(r);
    vimp_last.t = 0;
    vsp_live_t = now;
}

/* vulture_putstr: the messages that mean a miss */
void
vulture_msg_fx(const char *s)
{
    if (!s || !levwin || !u.ux)
        return;
    if (strstr(s, "You miss") || strstr(s, " misses") || strstr(s, " miss it"))
        vimp_whiff();
}

/* a thrown thing stopped against a wall beyond square (x, y) */
static void
vimp_chips(int x, int y, float dx, float dy, int mat)
{
    int k, n = mat == 1 ? 10 : 7;
    double ox = dx * 26, oy = dy * 13; /* the square's far edge */

    for (k = 0; k < n; k++) {
        double a = vsp_frand() * 6.2832, sp = 40 + vsp_frand() * 120;
        double vx = cos(a) * sp * 0.7 - dx * sp * 0.8, vy = sin(a) * sp * 0.35 - dy * sp * 0.4;

        if (mat == 1) /* metal: sparks */
            vsp_spawn(x, y, ox, oy, 26, vx * 1.4, vy * 1.4, 40 + vsp_frand() * 80, -300, 2.4, 1.2, 0,
                      180 + rand() % 200, 255, 225 + rand() % 30, 150 + rand() % 70, 0, 255);
        else if (mat == 2) /* wood: splinters */
            vsp_spawn(x, y, ox, oy, 24, vx, vy, 30 + vsp_frand() * 60, -360, 1.2, 1.4, 0,
                      350 + rand() % 250, 170, 120, 70, 5, 230);
        else /* stone and the rest: chips */
            vsp_spawn(x, y, ox, oy, 24, vx * 0.8, vy * 0.8, 30 + vsp_frand() * 60, -380, 1.2, 1.6, 0,
                      350 + rand() % 250, 175, 165, 150, 5, 220);
    }
    vsp_spawn(x, y, ox, oy, 20, 0, 0, 8, 0, 0.6, 5, 9, 500, 150, 140, 125, 3, 70); /* a puff */
    if (mat == 1)
        vl_flash(x, y, 1.0f, 0.9f, 0.7f, 0.45f, 1.4f, 160);
    vsp_live_t = SDL_GetTicks();
}

/*
 * IMPLEMENTATION_LOG s222: darts are not footballs.  A thrown pointed
 * weapon spun end over end (300 degrees a square) and a fired one flew at
 * whatever angle its art lies at - an arrow drawn pointing left flew right
 * tail first.  Now arrows, bolts, darts, spears and javelins, daggers and
 * knives, tridents fly point first along their arc (nose up off the hand,
 * level at the top, down into the target); shuriken, boomerangs, axes,
 * rocks, flasks still tumble - about their middle now, not their feet.
 * Which way an art points is read off its pixels: the long axis of what is
 * opaque, and the point the more metal end (bright, grey) - fletching, a
 * hilt or a shaft is the other.
 */
static double
vfl_art_angle(int tile_id, bool arrowlike)
{
    /* which end is the point: for arrows, bolts and darts the narrower end
       (fletching is wide; their heads can be dark or faint), for blades,
       spears and tridents the more metal end (bright, grey: not a hilt, a
       shaft, a grip) */
    static std::vector<std::pair<int, double> > cache;
    vulture_tile *t = vulture_get_tile(tile_id);
    SDL_Surface *s;
    double mx = 0, my = 0, sxx = 0, syy = 0, sxy = 0, n = 0, ax = 1, ay = 0, tmin = 1e9, tmax = -1e9, ang = 0.0;
    double em[2] = { 0, 0 }, ew[2] = { 0, 0 }, ep[2] = { 0, 0 }, epp[2] = { 0, 0 };
    size_t k;
    int x, y, pass, key = tile_id * 2 + (arrowlike ? 1 : 0);

    for (k = 0; k < cache.size(); k++)
        if (cache[k].first == key)
            return cache[k].second;
    if (!t || !t->graphic || !(s = SDL_ConvertSurfaceFormat(t->graphic, SDL_PIXELFORMAT_ARGB8888, 0)))
        return 0.0;
    SDL_LockSurface(s);
    for (pass = 0; pass < 4; pass++) {
        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->w; x++) {
                Uint32 p = ((Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch))[x];
                double al = (double) (p >> 24), r = (p >> 16) & 255, g = (p >> 8) & 255, bb = p & 255;
                double tt, pp, hi, lo;
                int e;

                if (al < 40.0)
                    continue;
                if (pass == 0) { /* the middle */
                    mx += x * al, my += y * al, n += al;
                    continue;
                }
                if (pass == 1) { /* the spread: the long axis */
                    sxx += (x - mx) * (x - mx) * al, syy += (y - my) * (y - my) * al;
                    sxy += (x - mx) * (y - my) * al;
                    continue;
                }
                tt = (x - mx) * ax + (y - my) * ay, pp = -(x - mx) * ay + (y - my) * ax;
                if (pass == 2) { /* how far it reaches both ways */
                    tmin = tt < tmin ? tt : tmin, tmax = tt > tmax ? tt : tmax;
                    continue;
                }
                e = tt > tmax - 0.3 * (tmax - tmin) ? 0 : tt < tmin + 0.3 * (tmax - tmin) ? 1 : -1;
                if (e < 0)
                    continue;
                hi = r > g ? (r > bb ? r : bb) : (g > bb ? g : bb), lo = r < g ? (r < bb ? r : bb) : (g < bb ? g : bb);
                em[e] += (hi > 0 ? 1.0 - (hi - lo) / hi : 1.0) * sqrt(hi / 255.0) * al; /* metal */
                ew[e] += al, ep[e] += pp * al, epp[e] += pp * pp * al;                   /* width */
            }
        if (pass == 0 && n > 0)
            mx /= n, my /= n;
        if (pass == 1) {
            double th = 0.5 * atan2(2.0 * sxy, sxx - syy);

            ax = cos(th), ay = sin(th);
        }
    }
    SDL_UnlockSurface(s);
    SDL_FreeSurface(s);
    if (n > 0 && ew[0] > 0 && ew[1] > 0) {
        bool hi_end;

        if (arrowlike) { /* the narrower end */
            double v0 = epp[0] / ew[0] - (ep[0] / ew[0]) * (ep[0] / ew[0]);
            double v1 = epp[1] / ew[1] - (ep[1] / ew[1]) * (ep[1] / ew[1]);

            hi_end = v0 < v1;
        } else { /* the more metal end */
            hi_end = em[0] / ew[0] >= em[1] / ew[1];
        }
        ang = hi_end ? atan2(ay, ax) : atan2(-ay, -ax);
    }
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "flight: art %d points %.0f deg (%s)\n", tile_id,
                      ang * 180.0 / 3.14159265358979, arrowlike ? "narrow end" : "metal end");
    cache.push_back(std::make_pair(key, ang));
    return ang;
}

/* the core's tmp_at: a thrown / fired / kicked thing takes off (its glyph) */
extern "C" void
vulture_flight_begin(int glyph)
{
    vfl_pts.clear();
    vfl_on = true;
    vfl_style = VIMP_GENERIC, vfl_mat = 0;
    vfl_tile = -1, vfl_step_ms = 45.0;
    vfl_point = false; /* s222 */
    /* s207: the arc it will fly (told by the thrower just before, or a guess) */
    vfl_arc_r = vfl_next_range > 0 ? vfl_next_range : 6;
    vfl_arc_r = vfl_arc_r < 2.0 ? 2.0 : vfl_arc_r > 20.0 ? 20.0 : vfl_arc_r;
    vfl_fired = vfl_next_fired, vfl_start_x = vfl_next_sx, vfl_start_y = vfl_next_sy;
    vfl_next_range = 0, vfl_next_fired = 0, vfl_next_sx = vfl_next_sy = -1;
    if (glyph_is_object(glyph)) {
        int otyp = glyph_to_obj(glyph);

        if (otyp >= 0 && otyp < NUM_OBJECTS)
            vfl_tile = OBJECT_TO_VTILE(otyp);

        if (otyp >= 0 && otyp < NUM_OBJECTS) {
            const struct objclass *oc = &objects[otyp];
            int mat = oc->oc_material;

            vfl_mat = (mat == IRON || mat == METAL || mat == COPPER || mat == SILVER || mat == GOLD
                       || mat == PLATINUM || mat == MITHRIL) ? 1
                      : mat == WOOD ? 2 : (mat == MINERAL || mat == GEMSTONE) ? 3 : mat == GLASS ? 4 : 0;
            if (oc->oc_class == WEAPON_CLASS)
                vfl_style = (oc->oc_dir & PIERCE) ? VIMP_PIERCE : (oc->oc_dir & SLASH) ? VIMP_SLASH
                                                                                        : VIMP_BLUNT;
            else
                vfl_style = VIMP_BLUNT; /* rocks, gems, anything thrown */
            { /* s222: point first, or tumbling */
                int sk = oc->oc_skill < 0 ? -oc->oc_skill : oc->oc_skill;

                vfl_point = oc->oc_class == WEAPON_CLASS
                            && (oc->oc_skill < 0 /* arrows, bolts */ || sk == P_DART || sk == P_SPEAR
                                || sk == P_DAGGER || sk == P_KNIFE || sk == P_TRIDENT || sk == P_LANCE);
                if (vfl_point && vfl_tile >= 0)
                    vfl_art = vfl_art_angle(vfl_tile, oc->oc_skill < 0 || sk == P_DART);
            }
        }
    }
    /* s207: fired - nearly flat; lobbed glass - high; thrown weapons and
       stones in between; longer throws higher */
    vfl_arc_h = vfl_fired ? 3.0 + 0.8 * vfl_arc_r
                : vfl_mat == 4 ? 12.0 + 3.0 * vfl_arc_r
                : vfl_style == VIMP_BLUNT ? 10.0 + 2.6 * vfl_arc_r : 8.0 + 2.2 * vfl_arc_r;
    if (vfl_arc_h > 40.0)
        vfl_arc_h = 40.0;
}

/* the core (throwit, m_throw): the flight about to start - its range, fired
   from a launcher or not, the thrower's square */
extern "C" void
vulture_flight_range(int range, int fired, int x, int y)
{
    vfl_next_range = range, vfl_next_fired = fired ? 1 : 0;
    vfl_next_sx = isok(x, y) ? x : -1, vfl_next_sy = isok(x, y) ? y : -1;
}

extern "C" void
vulture_flight_step(int x, int y)
{
    vfl_pt_t p;

    if (!vfl_on || !isok(x, y))
        return;
    p.x = x, p.y = y, p.t = SDL_GetTicks();
    if (vfl_pts.size() < 64)
        vfl_pts.push_back(p);
    vsp_live_t = p.t;
}

extern "C" void
vulture_flight_end(void)
{
    Uint32 now = SDL_GetTicks();
    size_t n = vfl_pts.size();

    vfl_on = false;
    if (n == 0)
        return;
    vfl_end_t = now, vfl_end_x = vfl_pts[n - 1].x, vfl_end_y = vfl_pts[n - 1].y;
    if (n >= 2) {
        int ddx = vfl_end_x - vfl_pts[n - 2].x, ddy = vfl_end_y - vfl_pts[n - 2].y;
        int wx = vfl_end_x + ddx, wy = vfl_end_y + ddy;

        vimp_dir(vfl_pts[n - 2].x, vfl_pts[n - 2].y, vfl_end_x, vfl_end_y, &vfl_end_dx, &vfl_end_dy);
        /* stopped by a wall, a closed door or a tree, in sight (glass breaks:
           breakobj shows that) */
        if (isok(wx, wy) && cansee(vfl_end_x, vfl_end_y) && !m_at(vfl_end_x, vfl_end_y) && vfl_mat != 4
            && (IS_STWALL(levl[wx][wy].typ) || closed_door(wx, wy) || levl[wx][wy].typ == TREE))
            vimp_chips(vfl_end_x, vfl_end_y, vfl_end_dx, vfl_end_dy, vfl_mat);
    } else
        vfl_end_dx = vfl_end_dy = 0.0f;
    vsp_live_t = now;
}

/* breakobj, potionhit: something fragile breaks at (x, y) */
extern "C" void
vulture_shatter_fx(int x, int y, int otyp)
{
    Uint32 now = SDL_GetTicks();
    const struct objclass *oc;
    int k, col;
    bool potion, egg, glass;

    if (!levwin || !u.ux || !isok(x, y) || !cansee(x, y) || otyp < 0 || otyp >= NUM_OBJECTS)
        return;
    oc = &objects[otyp];
    potion = oc->oc_class == POTION_CLASS, egg = otyp == EGG;
    glass = potion || oc->oc_material == GLASS;
    col = potion ? oc->oc_color : egg ? CLR_YELLOW : CLR_WHITE;
    if (col < 0 || col > 15)
        col = CLR_WHITE;
    if (glass)
        for (k = 0; k < 9; k++) { /* shards, glinting as they fall */
            double a = vsp_frand() * 6.2832, sp = 40 + vsp_frand() * 120;

            vsp_spawn(x, y, 0, 0, 22, cos(a) * sp, sin(a) * sp * 0.5, 40 + vsp_frand() * 90, -420, 0.6, 1.3,
                      0, 500 + rand() % 400, 210, 235, 255, 4, 255);
        }
    if (potion || egg)
        for (k = 0; k < 12; k++) { /* what was inside, in its colour */
            double a = vsp_frand() * 6.2832, sp = 30 + vsp_frand() * 100;

            vsp_spawn(x, y, 0, 0, 20, cos(a) * sp, sin(a) * sp * 0.5, 30 + vsp_frand() * 70, -380, 0.4,
                      1.8 + vsp_frand(), 0, 500 + rand() % 300, vimp_nhrgb[col][0], vimp_nhrgb[col][1],
                      vimp_nhrgb[col][2], 5, 230);
        }
    {
        vsp_wave_t w;

        w.x = x, w.y = y, w.t0 = now, w.dur = 320, w.rad = 0.5f;
        w.r = vimp_nhrgb[col][0], w.g = vimp_nhrgb[col][1], w.b = vimp_nhrgb[col][2];
        if (vsp_waves.size() < 16)
            vsp_waves.push_back(w);
    }
    vsp_live_t = now;
}

/*
 * IMPLEMENTATION_LOG s207 (flights): a thrown or fired thing flies on an
 * arc.  The core shows it a square at a time; it is drawn here instead,
 * gliding between the core's squares one step behind (the step's own
 * length), up off the floor - from hand height, over a rise sized by the
 * range the thrower gave it (flat for arrows and bolts off a launcher,
 * high for a lobbed flask), down to the floor at full range - with its
 * shadow on the floor under it, tumbling if thrown (not fired), and the
 * streak follows the arc.
 */
static double
vfl_lift(double s) /* how high the thing is, s squares out from the thrower */
{
    double f = s / vfl_arc_r;

    f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
    return 26.0 * (1.0 - f) + 6.0 * f + vfl_arc_h * 4.0 * f * (1.0 - f);
}

/* the floor point (screen) s squares along the path: the thrower's square
   is 0 (if told), the core's first square 1 */
static void
vfl_path(double s, int *px, int *py)
{
    size_t n = vfl_pts.size();
    int ax, ay, bx, by, k;
    double u;

    if (s <= 1.0) {
        vsp_screen(vfl_pts[0].x, vfl_pts[0].y, &bx, &by);
        if (vfl_start_x < 0 || s >= 1.0) {
            *px = bx, *py = by;
            return;
        }
        vsp_screen(vfl_start_x, vfl_start_y, &ax, &ay);
        u = s < 0.0 ? 0.0 : s;
    } else {
        k = (int) floor(s) - 1;
        if (k >= (int) n - 1) {
            vsp_screen(vfl_pts[n - 1].x, vfl_pts[n - 1].y, px, py);
            return;
        }
        vsp_screen(vfl_pts[k].x, vfl_pts[k].y, &ax, &ay);
        vsp_screen(vfl_pts[k + 1].x, vfl_pts[k + 1].y, &bx, &by);
        u = s - floor(s);
    }
    *px = ax + (int) floor((bx - ax) * u + 0.5), *py = ay + (int) floor((by - ay) * u + 0.5);
}

/* where the head is along the path now: a step behind the core, gliding */
static double
vfl_head_s(Uint32 now)
{
    size_t n = vfl_pts.size();
    double u;

    if (n >= 2) {
        vfl_step_ms = (double) (vfl_pts[n - 1].t - vfl_pts[n - 2].t);
        vfl_step_ms = vfl_step_ms < 20.0 ? 20.0 : vfl_step_ms > 120.0 ? 120.0 : vfl_step_ms;
    } else if (vfl_start_x < 0) {
        return 1.0;
    }
    u = (now - vfl_pts[n - 1].t) / vfl_step_ms;
    u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
    return (double) (n - 1) + u;
}

/* the object step: the thing is in the air over this square - vfl_draw
   draws it there (and a moment after it lands, till its glide arrives) */
static bool
vfl_hides(int j, int i, int tile_id)
{
    size_t n = vfl_pts.size();

    if (!n || vfl_tile < 0 || tile_id != vfl_tile || vfl_pts[n - 1].x != j || vfl_pts[n - 1].y != i)
        return false;
    return vfl_on || SDL_GetTicks() - vfl_pts[n - 1].t < (Uint32) vfl_step_ms + 10;
}

static void
vfl_draw_arc(Uint32 now, Uint8 r, Uint8 g, Uint8 b)
{
    size_t n = vfl_pts.size();
    double s, lift;
    int px, py, k;
    bool flying;

    if (!n)
        return;
    s = vfl_head_s(now);
    flying = vfl_on || now - vfl_pts[n - 1].t < (Uint32) vfl_step_ms + 10;
    for (k = 48; k >= 0; k--) { /* the streak, along the arc behind the head */
        double ss = s - k * 0.05, fade = 1.0 - k / 49.0;
        int tx, ty;

        if (ss < (vfl_start_x < 0 ? 1.0 : 0.0))
            continue;
        if (!vfl_on)
            fade *= 1.0 - (double) (now - vfl_end_t) / 200.0;
        if (fade <= 0.0)
            continue;
        vfl_path(ss, &tx, &ty);
        ty -= (int) vfl_lift(ss);
        vsp_blob(tx, ty, 3.5, r, g, b, (int) (26 * fade), true);
        vsp_blob(tx, ty, 1.2, r, g, b, (int) (120 * fade), true);
    }
    if (!flying || vfl_tile < 0)
        return;
    vfl_path(s, &px, &py);
    lift = vfl_lift(s);
    vsp_blob(px, py + 3, 8.5 - lift / 16.0, 0, 0, 0, (int) (125 * (1.0 - lift / 70.0)), false, 0.45); /* its shadow */
    {
        double deg = vfl_style == VIMP_BLUNT ? s * 200.0 : s * 300.0, th;
        vulture_tile *ft = vulture_get_tile(vfl_tile);
        int hh = ft && ft->graphic ? ft->graphic->h : 0;

        if (vfl_point) { /* s222: point first, along the arc - nose up, level, down */
            double s0 = s - 0.2 < (vfl_start_x < 0 ? 1.0 : 0.0) ? (vfl_start_x < 0 ? 1.0 : 0.0) : s - 0.2, s1 = s0 + 0.4;
            int ax2, ay2, bx2, by2;
            double fdx, fdy;

            vfl_path(s0, &ax2, &ay2);
            vfl_path(s1, &bx2, &by2);
            fdx = bx2 - ax2, fdy = (by2 - vfl_lift(s1)) - (ay2 - vfl_lift(s0));
            if (fabs(fdx) + fabs(fdy) < 0.5 && vfl_pts.size() >= 2) { /* no way yet: the core's last step */
                int cx2, cy2, dx2, dy2;

                vsp_screen(vfl_pts[vfl_pts.size() - 2].x, vfl_pts[vfl_pts.size() - 2].y, &cx2, &cy2);
                vsp_screen(vfl_pts[vfl_pts.size() - 1].x, vfl_pts[vfl_pts.size() - 1].y, &dx2, &dy2);
                fdx = dx2 - cx2, fdy = dy2 - cy2;
            }
            deg = fabs(fdx) + fabs(fdy) < 0.5 ? 0.0 : (atan2(fdy, fdx) - vfl_art) * 180.0 / 3.14159265358979;
        } else if (vfl_fired) {
            deg = 0.0;
        }
        /* turned about its middle (the blit turns about its feet) */
        th = deg * 3.14159265358979 / 180.0;
        vtw_put_tile_x(px - (int) floor(0.5 * hh * sin(th) + 0.5),
                       py - (int) lift - (int) floor(0.5 * hh * (1.0 - cos(th)) + 0.5), vfl_tile, 0, 1.0, 1.0, deg,
                       false, 255);
    }
}

/* the streak behind a flying thing */
static void
vfl_draw(Uint32 now)
{
    size_t n = vfl_pts.size();
    Uint8 r = 235, g = 240, b = 255;

    if (!n)
        return;
    if (!vfl_on && now - vfl_end_t > 200) {
        vfl_pts.clear();
        return;
    }
    if (vfl_style == VIMP_BLUNT)
        r = 215, g = 195, b = 165;
    else if (vfl_mat == 4)
        r = 200, g = 230, b = 255;
    vfl_draw_arc(now, r, g, b); /* s207: in the air, on its arc */
}

/* the impact flashes, and the flinches' upkeep */
static void
vimp_draw(Uint32 now, double t)
{
    size_t i, n = 0;

    for (i = 0; i < vimp_recs.size(); i++)
        if (now - vimp_recs[i].t0 < vimp_recs[i].dur)
            vimp_recs[n++] = vimp_recs[i];
    vimp_recs.resize(n);
    n = 0;
    for (i = 0; i < vimp_fx.size(); i++) {
        vimp_fx_t f = vimp_fx[i];
        double age = (double) (now - f.t0) / f.dur, fl, cx, cy, a, s = f.size;
        float dx = f.dx, dy = f.dy, nx, ny;
        const Uint8 *c = vimp_col[f.style];
        int sx, sy, k;

        if (age >= 1.0)
            continue;
        vimp_fx[n++] = f;
        vsp_screen(f.x, f.y, &sx, &sy);
        if (isok(f.x, f.y)) /* with the sprite, mid-step */
            sx += vtw_ox[f.y][f.x], sy += vtw_oy[f.y][f.x];
        cx = sx - dx * 8.0, cy = sy - 30.0 - dy * 4.0; /* chest height, the attacker's side */
        if (dx == 0.0f && dy == 0.0f)
            dx = 1.0f;
        nx = -dy, ny = dx;
        fl = 1.0 - age;
        if (f.whiff) { /* a pale swoosh going past */
            for (k = 0; k <= 12; k++) {
                double u2 = (double) k / 12, ang = -1.0 + 2.0 * u2, p = age * 1.4 - u2 * 0.5;

                if (p < 0.0 || p > 1.0)
                    continue;
                vsp_blob((int) (cx + nx * 22 * sin(ang) + dx * (22 * cos(ang) - 8)),
                         (int) (cy + ny * 11 * sin(ang) + dy * (11 * cos(ang) - 4)), 2.5, 230, 235, 240,
                         (int) (150 * sin(3.14159265 * p)), true);
            }
            continue;
        }
        if (age < 0.4) /* white-hot, at once */
            vsp_blob((int) cx, (int) cy, 14 * s, 255, 255, 255, (int) (200 * (1.0 - age / 0.4)), true);
        vsp_blob((int) cx, (int) cy, 24 * s, c[0], c[1], c[2], (int) (120 * fl), true);
        switch (f.style) {
        case VIMP_SLASH: { /* a bright arc sweeping across */
            double sweep = age < 0.5 ? age / 0.5 : 1.0;

            for (k = 0; k <= 16; k++) {
                double u2 = (double) k / 16;
                int al = (int) (255 * fl * (0.4 + 0.6 * sin(3.14159265 * u2)));

                if (u2 > sweep)
                    break;
                a = -1.1 + 2.2 * u2;
                vsp_blob((int) (cx + nx * 26 * s * sin(a) + dx * 10 * s * cos(a)),
                         (int) (cy + ny * 13 * s * sin(a) + dy * 5 * s * cos(a) - 3 * s * cos(a)), 2.0 * s, 255,
                         250, 235, al, true);
                vsp_blob((int) (cx + nx * 26 * s * sin(a) + dx * 10 * s * cos(a)),
                         (int) (cy + ny * 13 * s * sin(a) + dy * 5 * s * cos(a) - 3 * s * cos(a)), 5.0 * s,
                         c[0], c[1], c[2], al / 3, true);
            }
            break;
        }
        case VIMP_PIERCE: /* a jab through, a star at its tip */
            vsp_line(cx - dx * 18 * s, cy - dy * 9 * s, cx + dx * 18 * s, cy + dy * 9 * s, 1.6 * s, 255, 255,
                     255, (int) (240 * fl), true, 1.5);
            for (k = 0; k < 4; k++) {
                a = k * 1.5708 + 0.785 + t * 3.0;
                vsp_line(cx + dx * 14 * s, cy + dy * 7 * s, cx + dx * 14 * s + cos(a) * 9 * s,
                         cy + dy * 7 * s + sin(a) * 9 * s, 1.0, c[0], c[1], c[2], (int) (200 * fl), true, 1.5);
            }
            break;
        case VIMP_CLAW: /* three raking lines */
            for (k = -1; k <= 1; k++) {
                double o = k * 7 * s, sweep = age < 0.45 ? age / 0.45 : 1.0;
                double x0 = cx + nx * o - (dx + nx) * 14 * s, y0 = cy + ny * o * 0.5 - 14 * s;
                double x1 = cx + nx * o + (dx + nx) * 14 * s, y1 = cy + ny * o * 0.5 + 10 * s;

                vsp_line(x0, y0, x0 + (x1 - x0) * sweep, y0 + (y1 - y0) * sweep, 1.6 * s, 255, 120, 100,
                         (int) (230 * fl), true, 1.5);
            }
            break;
        case VIMP_BITE: /* jaws snapping shut */
            for (k = 0; k <= 10; k++) {
                double a2 = 3.14159265 * k / 10, close = 1.0 - (age < 0.5 ? age / 0.5 : 1.0) * 0.7;

                vsp_blob((int) (cx + cos(a2) * 13 * s), (int) (cy - sin(a2) * 8 * s * close - 2), 1.6, 255,
                         225, 225, (int) (220 * fl), true);
                vsp_blob((int) (cx + cos(a2) * 13 * s), (int) (cy + sin(a2) * 8 * s * close + 2), 1.6, 255,
                         225, 225, (int) (220 * fl), true);
            }
            break;
        default: /* a star burst: blunt, unknown, fire, cold, shock */
            for (k = 0; k < 6; k++) {
                double len = (10 + 14 * age) * s;

                a = k * 1.0472 + vsp_hash(f.x, f.y, (int) f.t0) * 1.0;
                vsp_line(cx + cos(a) * 4 * s, cy + sin(a) * 2 * s, cx + cos(a) * len, cy + sin(a) * len * 0.6,
                         1.2 * s, c[0], c[1], c[2], (int) (220 * fl), true, 1.6);
            }
            break;
        }
    }
    vimp_fx.resize(n);
}

/* probe: s202 - VULTURE_SELFTEST_IMPACT: real blows (do_attack, mattacku)
   with a jackal beside you, a dagger flown into a wall through the core's
   tmp_at, a potion shattering - each shot as it plays */
static Uint32 vimp_probe_t = 0;
static int vimp_probe_n = 0, vimp_probe_shots = 0, vimp_probe_sub = 0;

static void
vimp_probe_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];
    SDL_Surface *o;
    SDL_Rect a;
    int hx, hy;

    if (!dir || vimp_probe_shots >= 40 || !vulture_screen || vulture_screen->w < 960 || vulture_screen->h < 540)
        return;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vsp_screen(u.ux, u.uy, &hx, &hy);
    a.w = 960, a.h = 540, a.x = hx - 480, a.y = hy - 310;
    a.x = a.x < 0 ? 0 : a.x + a.w > vulture_screen->w ? vulture_screen->w - a.w : a.x;
    a.y = a.y < 0 ? 0 : a.y + a.h > vulture_screen->h ? vulture_screen->h - a.h : a.y;
    if (!(o = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return;
    snprintf(p, sizeof p, "%s\\impact%02d_%s_%02d.bmp", dir, vimp_probe_n, tag, vimp_probe_shots++);
    if (vulture_frame_to_surface(o, &a))
        SDL_SaveBMP(o, p);
    SDL_FreeSurface(o);
}

static struct monst *
vimp_probe_jackal(void)
{
    static const int ddx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    struct monst *m;
    int k;

    for (k = 0; k < 8; k++)
        if ((m = m_at(u.ux + ddx[k], u.uy + ddy[k])) != 0 && !m->mtame && !m->mpeaceful)
            return m;
    for (k = 0; k < 8; k++) {
        int x = u.ux + ddx[k], y = u.uy + ddy[k];

        if (isok(x, y) && ACCESSIBLE(levl[x][y].typ) && !m_at(x, y))
            return makemon(&mons[PM_JACKAL], x, y, NO_MM_FLAGS);
    }
    return NULL;
}

static void
vimp_probe_tick(Uint32 now)
{
    static const int ddx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    struct monst *m;
    int k;

    if (vimp_probe_sub == 1) { /* the burst a moment on */
        if (now - vimp_probe_t > 110) {
            vimp_probe_shot("later");
            vimp_probe_sub = 0, vimp_probe_t = now, vimp_probe_n++;
        }
        return;
    }
    if (now - vimp_probe_t < 900)
        return;
    u.uhp = u.uhpmax;
    u.uhitinc = 20; /* the probe's blows land */
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "impact probe %d\n", vimp_probe_n);
    switch (vimp_probe_n) {
    case 0:
    case 1:
    case 2: /* you swing at a jackal: two it lives through, then the killing blow */
        if ((m = vimp_probe_jackal()) != 0) {
            m->mhpmax = 60, m->mhp = vimp_probe_n < 2 ? 60 : 1;
            (void) do_attack(m);
        }
        break;
    case 3:
    case 4: /* one bites back, knowing where you stand */
        if ((m = vimp_probe_jackal()) != 0) {
            m->mux = u.ux, m->muy = u.uy, m->msleeping = 0, m->mcanmove = 1;
            (void) mattacku(m);
        }
        break;
    case 5: { /* a dagger flown into the wall the farthest way */
        int best = 0, bl = 0, l, x, y;

        for (k = 0; k < 8; k++) {
            for (l = 0, x = u.ux + ddx[k], y = u.uy + ddy[k];
                 isok(x, y) && ACCESSIBLE(levl[x][y].typ) && !m_at(x, y) && l < 10; l++)
                x += ddx[k], y += ddy[k];
            if (l > bl)
                bl = l, best = k;
        }
        tmp_at(DISP_FLASH, objnum_to_glyph(DAGGER));
        for (l = 1; l <= bl; l++) {
            tmp_at(u.ux + ddx[best] * l, u.uy + ddy[best] * l);
            nh_delay_output();
        }
        tmp_at(DISP_END, 0);
        break;
    }
    case 6: /* a potion breaks beside you */
        for (k = 0; k < 8; k++)
            if (isok(u.ux + ddx[k], u.uy + ddy[k]) && ACCESSIBLE(levl[u.ux + ddx[k]][u.uy + ddy[k]].typ)) {
                vulture_shatter_fx(u.ux + ddx[k], u.uy + ddy[k], POT_SICKNESS);
                break;
            }
        break;
    default:
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "impact probe: done, %d shots\n", vimp_probe_shots);
        vimp_probe_t = 0;
        return;
    }
    flush_screen(1);
    vimp_probe_shot("now");
    vimp_probe_sub = 1, vimp_probe_t = SDL_GetTicks();
}

/*
 * IMPLEMENTATION_LOG s203: doors swing, and using things in the world
 * shows.
 *  - A door seen opening or shutting swings instead of popping.  The closed
 *    door's art is split once into its stone arch and its wooden panel
 *    (vdr_split: warm wood against grey stone, a 5x5 majority, only big
 *    blobs kept), and the panel's two halves turn about their pillars on
 *    the isometric floor - each an affine quad (vulture_gpumap_quad) - away
 *    from the viewer, the way the open art shows its leaves; the open art
 *    fades in as they arrive.  Shutting plays it back, with a rebound and a
 *    puff of dust at the sill.  A door smashed in shudders, bursts into
 *    splinters and falls away from you onto its planks; one conjured into
 *    an empty doorway rises out of gathering dust.
 *  - Fountains, sinks, altars, thrones, chests and hidden doors answer when
 *    used or found (vulture_world_fx, from the core).
 */
struct vdr_split_t {
    SDL_Surface *frame, *panel, *nframe; /* the arch, the panel (ARGB, the art's size), the arch's normals */
    SDL_Surface *npanel;                 /* s204: the panel's normals, which turn with it */
    int pl, pr, pt, pb;                  /* the panel's box in the art */
    bool ok;
};
static vdr_split_t vdr_splits[2]; /* the closed door arts: [0] V, [1] H */
static bool vdr_split_tried[2] = { false, false };
struct vdr_anim_t {
    int x, y, from, to, kind; /* furniture as the map holds it; 1 open, 2 shut, 3 smash, 4 conjure */
    Uint32 t0, dur;
    float push; /* a smash: +1 it falls away from the viewer, -1 toward */
    bool fx;    /* the slam's dust is out */
};
static std::vector<vdr_anim_t> vdr_anims;
static d_level vdr_lev = { -1, -1 };
static double vdr_freeze = -1.0; /* probe: >= 0 holds every door at this point of its swing */
#define VDR_K (22.0 / 56.0)      /* the floor's slope on screen: V_MAP_YMOD / V_MAP_XMOD */

static int
vdr_class(int t) /* 1 shut, 2 open, 3 broken or no door, 0 not a door */
{
    if (t == V_MISC_VDOOR_WOOD_CLOSED || t == V_MISC_HDOOR_WOOD_CLOSED)
        return 1;
    if (t == V_MISC_VDOOR_WOOD_OPEN || t == V_MISC_HDOOR_WOOD_OPEN)
        return 2;
    return t == V_MISC_DOOR_WOOD_BROKEN ? 3 : 0;
}

/* the map's door tile t is drawn with the V art (its wall rising to the
   right on screen) - after the view's quarter turns */
static bool
vdr_vart(int t)
{
    t = v_rot_door(t);
    return t == V_MISC_VDOOR_WOOD_CLOSED || t == V_MISC_VDOOR_WOOD_OPEN;
}

/* the closed door art split into its arch and its panel: wood is warm (red
   well above blue), stone grey; a 5x5 majority smooths the speckle and
   only blobs of 150+ pixels count.  Once per art; NULL: no clear panel */
static vdr_split_t *
vdr_split(int tile_id)
{
    int idx = tile_id == V_MISC_VDOOR_WOOD_CLOSED ? 0 : tile_id == V_MISC_HDOOR_WOOD_CLOSED ? 1 : -1;
    vulture_tile *tile;
    SDL_Surface *src, *nm;
    vdr_split_t *sp;
    int w, h, x, y, k, big = 0;

    if (idx < 0)
        return NULL;
    sp = &vdr_splits[idx];
    if (vdr_split_tried[idx])
        return sp->ok ? sp : NULL;
    vdr_split_tried[idx] = true;
    memset(sp, 0, sizeof *sp);
    tile = vulture_get_tile(tile_id);
    if (!tile || !tile->graphic
        || !(src = SDL_ConvertSurfaceFormat(tile->graphic, SDL_PIXELFORMAT_ARGB8888, 0)))
        return NULL;
    w = src->w, h = src->h;
    std::vector<signed char> warm((size_t) (w * h)), m((size_t) (w * h));
    std::vector<int> lab((size_t) (w * h), 0), sizes(1, 0), stack;

    SDL_LockSurface(src);
    for (y = 0; y < h; y++) {
        const Uint32 *row = (const Uint32 *) ((const Uint8 *) src->pixels + y * src->pitch);

        for (x = 0; x < w; x++) {
            Uint32 p = row[x];
            int r = (int) ((p >> 16) & 255), b = (int) (p & 255);

            warm[y * w + x] = (signed char) ((p >> 24) < 128 ? -1 : r - b >= 14 ? 1 : 0);
        }
    }
    for (y = 0; y < h; y++) /* the majority, over the opaque pixels */
        for (x = 0; x < w; x++) {
            int s = 0, c = 0, dx, dy;

            m[y * w + x] = 0;
            if (warm[y * w + x] < 0)
                continue;
            for (dy = -2; dy <= 2; dy++)
                for (dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy;

                    if (xx >= 0 && xx < w && yy >= 0 && yy < h && warm[yy * w + xx] >= 0)
                        s += warm[yy * w + xx], c++;
                }
            m[y * w + x] = (signed char) (s * 2 > c);
        }
    for (k = 0; k < w * h; k++) { /* the blobs */
        int id, cnt = 0;

        if (!m[k] || lab[k])
            continue;
        id = (int) sizes.size();
        lab[k] = id;
        stack.push_back(k);
        while (!stack.empty()) {
            int q = stack.back(), qx = q % w, qy = q / w;

            stack.pop_back();
            cnt++;
            if (qx > 0 && m[q - 1] && !lab[q - 1])
                lab[q - 1] = id, stack.push_back(q - 1);
            if (qx < w - 1 && m[q + 1] && !lab[q + 1])
                lab[q + 1] = id, stack.push_back(q + 1);
            if (qy > 0 && m[q - w] && !lab[q - w])
                lab[q - w] = id, stack.push_back(q - w);
            if (qy < h - 1 && m[q + w] && !lab[q + w])
                lab[q + w] = id, stack.push_back(q + w);
        }
        sizes.push_back(cnt);
    }
    sp->frame = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    sp->panel = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sp->frame || !sp->panel) {
        SDL_UnlockSurface(src);
        SDL_FreeSurface(src);
        if (sp->frame)
            SDL_FreeSurface(sp->frame);
        if (sp->panel)
            SDL_FreeSurface(sp->panel);
        sp->frame = sp->panel = NULL;
        return NULL;
    }
    sp->pl = w, sp->pr = 0, sp->pt = h, sp->pb = 0;
    SDL_LockSurface(sp->frame);
    SDL_LockSurface(sp->panel);
    for (y = 0; y < h; y++) {
        const Uint32 *row = (const Uint32 *) ((const Uint8 *) src->pixels + y * src->pitch);
        Uint32 *fr = (Uint32 *) ((Uint8 *) sp->frame->pixels + y * sp->frame->pitch);
        Uint32 *pa = (Uint32 *) ((Uint8 *) sp->panel->pixels + y * sp->panel->pitch);

        for (x = 0; x < w; x++) {
            int l = lab[y * w + x];
            bool in = l && sizes[l] >= 150;

            if (in) { /* wood (warm for its brightness) or the dark seams between the
                         planks - not the arch's lit bevel the smoothing took in */
                Uint32 p = row[x];
                int r = (int) ((p >> 16) & 255), g = (int) ((p >> 8) & 255), b = (int) (p & 255);

                in = (r - b >= 14 && (r - b) * 100 >= 18 * (r > 0 ? r : 1)) || (r * 3 + g * 6 + b) / 10 < 75;
            }
            fr[x] = in ? 0 : row[x];
            pa[x] = in ? row[x] : 0;
            if (in) {
                big++;
                if (x < sp->pl)
                    sp->pl = x;
                if (x + 1 > sp->pr)
                    sp->pr = x + 1;
                if (y < sp->pt)
                    sp->pt = y;
                if (y + 1 > sp->pb)
                    sp->pb = y + 1;
            }
        }
    }
    SDL_UnlockSurface(sp->panel);
    SDL_UnlockSurface(sp->frame);
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    SDL_SetSurfaceBlendMode(sp->frame, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceBlendMode(sp->panel, SDL_BLENDMODE_BLEND);
    if (big < 400 || sp->pr - sp->pl < 12 || sp->pb - sp->pt < 20) { /* not this kind of art: cross-fade it */
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "door art %d: no panel found (%d px)\n", tile_id, big);
        SDL_FreeSurface(sp->frame);
        SDL_FreeSurface(sp->panel);
        sp->frame = sp->panel = NULL;
        return NULL;
    }
    /* the normals split the same way: the arch's alone (the panel's would
       light the open doorway) and the panel's (s204: they turn with it, so
       a leaf isn't lit with the floor's relief under it) */
    nm = vulture_get_tile_normal(tile_id, false);
    if (nm && nm->w == w && nm->h == h
        && (sp->nframe = SDL_ConvertSurfaceFormat(nm, SDL_PIXELFORMAT_ARGB8888, 0)) != NULL) {
        if (!(sp->npanel = SDL_ConvertSurfaceFormat(nm, SDL_PIXELFORMAT_ARGB8888, 0))) {
            SDL_FreeSurface(sp->nframe);
            sp->nframe = NULL;
        }
    }
    if (sp->nframe && sp->npanel) {
        SDL_LockSurface(sp->nframe);
        SDL_LockSurface(sp->npanel);
        SDL_LockSurface(sp->panel);
        for (y = 0; y < h; y++) {
            Uint32 *nr = (Uint32 *) ((Uint8 *) sp->nframe->pixels + y * sp->nframe->pitch);
            Uint32 *np = (Uint32 *) ((Uint8 *) sp->npanel->pixels + y * sp->npanel->pitch);
            const Uint32 *pa = (const Uint32 *) ((const Uint8 *) sp->panel->pixels + y * sp->panel->pitch);

            for (x = 0; x < w; x++)
                if (pa[x] >> 24)
                    nr[x] = 0;
                else
                    np[x] = 0;
        }
        SDL_UnlockSurface(sp->panel);
        SDL_UnlockSurface(sp->npanel);
        SDL_UnlockSurface(sp->nframe);
        SDL_SetSurfaceBlendMode(sp->nframe, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceBlendMode(sp->npanel, SDL_BLENDMODE_BLEND);
    }
    /* zoomed in: the HD art cut the same way, the two adding up to it */
    vulture_gpu_hd_derive_cut(sp->frame, tile->graphic, sp->panel, true);
    vulture_gpu_hd_derive_cut(sp->panel, tile->graphic, sp->panel, false);
    sp->ok = true;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "door art %d: panel %d px, box x %d-%d y %d-%d\n", tile_id,
                      big, sp->pl, sp->pr, sp->pt, sp->pb);
    return sp;
}

/* src of s laid into the quad q (corners TL, TR, BR, BL; its sides upright,
   as a turning door's are), shaded and faded: on the GPU's map layer, or in
   2 px strips into the frame */
static void
vdr_quad_rgb(SDL_Surface *s, const SDL_Rect &src, const float *q, int r, int g, int b, int alpha)
{
    int c;

    if (!s || alpha <= 0 || src.w <= 0 || src.h <= 0)
        return;
    alpha = alpha > 255 ? 255 : alpha;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    if (vulture_gpumap_on) {
        vulture_gpumap_quad(s, &src, q, r, g, b, alpha);
        return;
    }
    SDL_SetSurfaceColorMod(s, (Uint8) r, (Uint8) g, (Uint8) b);
    SDL_SetSurfaceAlphaMod(s, (Uint8) alpha);
    for (c = 0; c < src.w; c++) {
        double f = (c + 0.5) / src.w;
        double tx = q[0] + (q[2] - q[0]) * f, ty = q[1] + (q[3] - q[1]) * f, by = q[7] + (q[5] - q[7]) * f;
        SDL_Rect sr, dr;

        sr.x = src.x + c, sr.y = src.y, sr.w = 1, sr.h = src.h;
        dr.x = (int) floor(tx), dr.y = (int) floor(ty + 0.5), dr.w = 2, dr.h = (int) (by - ty + 0.5);
        if (dr.h > 0)
            SDL_BlitScaled(s, &sr, vulture_screen, &dr);
    }
    SDL_SetSurfaceColorMod(s, 255, 255, 255);
    SDL_SetSurfaceAlphaMod(s, 255);
}

static void
vdr_quad(SDL_Surface *s, const SDL_Rect &src, const float *q, int shade, int alpha) /* grey-shaded */
{
    vdr_quad_rgb(s, src, q, shade, shade, shade, alpha);
}

/* s204: its normal map laid the same way, for the light (GPU only) */
static void
vdr_quad_normal(SDL_Surface *nm, const SDL_Rect &src, const float *q, int alpha)
{
    if (nm && alpha > 0 && vulture_normal_capture && vulture_gpumap_on)
        vulture_gpumap_normal_quad(nm, &src, q, alpha);
}

/* s (a door piece, or a whole door tile) with its top-left at (x, y),
   faded; nm: its normals, for the light */
static void
vdr_img(SDL_Surface *s, SDL_Surface *nm, int x, int y, int alpha)
{
    if (!s || alpha <= 0)
        return;
    alpha = alpha > 255 ? 255 : alpha;
    SDL_SetSurfaceAlphaMod(s, (Uint8) alpha);
    vulture_put_img(x, y, s);
    SDL_SetSurfaceAlphaMod(s, 255);
    if (!nm || !vulture_normal_capture)
        return;
    if (vulture_gpumap_on) {
        vulture_gpumap_normal_ex(nm, x, y, 1.0, 1.0, 0.0, alpha);
    } else if (vulture_normal_buf && alpha >= 128) {
        SDL_Rect r;

        r.x = x, r.y = y, r.w = nm->w, r.h = nm->h;
        SDL_BlitSurface(nm, NULL, vulture_normal_buf, &r);
    }
}

static void
vdr_tile(int tile_id, int x, int y, int alpha) /* a door tile on the square at (x, y), faded */
{
    vulture_tile *t = vulture_get_tile(tile_id);

    if (t && t->graphic)
        vdr_img(t->graphic, vulture_get_tile_normal(tile_id, false), x + t->xmod, y + t->ymod, alpha);
}

/* half the panel - columns c0..c1 of the art - turned th radians about its
   pillar at column hx: (cx, cy) the way it runs from the pillar shut,
   (ax, ay) the way it lies open, in screen px per px across.  On the
   isometric floor that is an affine map, so the half stays a quad: its
   columns slide along the turned direction, each keeping its height */
static void
vdr_leaf(vdr_split_t *sp, int ox, int oy, int hx, int c0, int c1, double cx, double cy, double ax,
         double ay, double th, int shade, int alpha)
{
    double kx = cos(th) * cx + sin(th) * ax, ky = cos(th) * cy + sin(th) * ay - cy;
    double d0 = fabs((double) (c0 - hx)), d1 = fabs((double) (c1 - hx));
    float q[8];
    SDL_Rect r;

    if (c1 <= c0 || alpha <= 0)
        return;
    r.x = c0, r.y = sp->pt, r.w = c1 - c0, r.h = sp->pb - sp->pt;
    q[0] = (float) (ox + hx + d0 * kx), q[1] = (float) (oy + sp->pt + d0 * ky);
    q[2] = (float) (ox + hx + d1 * kx), q[3] = (float) (oy + sp->pt + d1 * ky);
    q[4] = q[2], q[5] = (float) (oy + sp->pb + d1 * ky);
    q[6] = q[0], q[7] = (float) (oy + sp->pb + d0 * ky);
    vdr_quad(sp->panel, r, q, shade, alpha);
    vdr_quad_normal(sp->npanel, r, q, alpha); /* s204 */
}

/* step 2 of the map: the door on (j, i) part way through opening, shutting,
   breaking or appearing, drawn at (x, y) instead of its tile `cur` (the
   map's furniture there); false: none */
static bool
vdr_draw(int x, int y, int j, int i, int cur)
{
    vdr_anim_t *a = NULL;
    vdr_split_t *sp;
    vulture_tile *ct;
    double t, K = VDR_K;
    size_t k;
    int ox, oy;
    bool vd;

    if (vdr_anims.empty())
        return false;
    for (k = 0; k < vdr_anims.size(); k++)
        if (vdr_anims[k].x == j && vdr_anims[k].y == i)
            a = &vdr_anims[k];
    if (!a || cur != a->to) /* none, or it has changed again since */
        return false;
    t = vdr_freeze >= 0.0 ? vdr_freeze : (double) (SDL_GetTicks() - a->t0) / a->dur;
    if (t >= 1.0)
        return false;
    if (t < 0.0)
        t = 0.0;
    if (a->kind == 4) { /* conjured: the planks go, the door rises out of the dust */
        double u = (t - 0.15) / 0.7, e;
        int al;

        u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
        e = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);
        al = (int) (255 * (u < 0.6 ? u / 0.6 : 1.0));
        if (t < 0.45 && vdr_class(a->from) == 3)
            vdr_tile(a->from, x, y, (int) (255 * (1.0 - t / 0.45)));
        if (al > 0)
            vtw_put_tile_x(x, y, v_rot_door(a->to), 0, 1.0, 0.5 + 0.5 * e, 0.0, false, al);
        return true;
    }
    if (a->kind == 3) { /* smashed: it shudders, topples and goes; the planks show */
        int C = v_rot_door(a->from), pa, fa;
        double jx, u, ph, H, dx, dy, Dx, Dy;
        float q[8];
        SDL_Rect r;

        vdr_tile(a->to, x, y, t < 0.25 ? 0 : t > 0.8 ? 255 : (int) (255 * (t - 0.25) / 0.55));
        if (vdr_class(a->from) != 1 || !(sp = vdr_split(C)) || !(ct = vulture_get_tile(C))) {
            vdr_tile(C, x, y, t < 0.1 ? 255 : t > 0.6 ? 0 : (int) (255 * (0.6 - t) / 0.5));
            return true;
        }
        vd = vdr_vart(a->from);
        ox = x + ct->xmod, oy = y + ct->ymod;
        jx = t < 0.14 ? 2.5 * sin(t * 140.0) * (1.0 - t / 0.14) : 0.0;
        u = (t - 0.08) / 0.5;
        u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
        ph = 1.35 * u * u; /* radians: it topples, faster as it goes */
        H = sp->pb - sp->pt;
        Dx = a->push * (vd ? -1.0 : 1.0), Dy = a->push * -K;
        dx = H * 0.55 * sin(ph) * Dx, dy = H * (1.0 - cos(ph)) + H * 0.55 * sin(ph) * Dy;
        pa = t < 0.15 ? 255 : t > 0.6 ? 0 : (int) (255 * (0.6 - t) / 0.45);
        fa = t < 0.15 ? 255 : t > 0.78 ? 0 : (int) (255 * (0.78 - t) / 0.63);
        r.x = sp->pl, r.y = sp->pt, r.w = sp->pr - sp->pl, r.h = sp->pb - sp->pt;
        q[0] = (float) (ox + sp->pl + jx + dx), q[1] = (float) (oy + sp->pt + dy);
        q[2] = (float) (ox + sp->pr + jx + dx), q[3] = q[1];
        q[4] = (float) (ox + sp->pr + jx), q[5] = (float) (oy + sp->pb);
        q[6] = (float) (ox + sp->pl + jx), q[7] = q[5];
        if (a->push > 0.0f) /* falling away: behind the arch */
            vdr_quad(sp->panel, r, q, 255 - (int) (70 * u), pa), vdr_quad_normal(sp->npanel, r, q, pa);
        vdr_img(sp->frame, sp->nframe, ox, oy, fa);
        if (a->push <= 0.0f)
            vdr_quad(sp->panel, r, q, 255 - (int) (70 * u), pa), vdr_quad_normal(sp->npanel, r, q, pa);
        return true;
    }
    { /* opening (1) or shutting (2): the leaves turn about their pillars */
        int C = v_rot_door(a->kind == 1 ? a->from : a->to), O = v_rot_door(a->kind == 1 ? a->to : a->from);
        int fade, la, shade, mid;
        double th, lcy, lax;

        if (a->kind == 1) {
            double u = t - 1.0;

            th = (1.0 + 2.3 * u * u * u + 1.3 * u * u) * 1.5708; /* a touch past, then it settles */
            fade = t < 0.7 ? 0 : t > 0.9 ? 255 : (int) (255 * (t - 0.7) / 0.2);
            la = t < 0.85 ? 255 : (int) (255 * (1.0 - t) / 0.15);
        } else {
            double u = t / 0.78;

            th = t < 0.78 ? (1.0 - u * u) * 1.5708                  /* gathering speed */
                          : 0.1 * sin(3.14159 * (t - 0.78) / 0.22); /* the rebound */
            fade = t < 0.2 ? (int) (255 * (1.0 - t / 0.2)) : 0;
            la = t < 0.1 ? (int) (255 * t / 0.1) : 255;
        }
        if (!(sp = vdr_split(C)) || !(ct = vulture_get_tile(C))) { /* no panel found: a cross-fade */
            int oa = a->kind == 1 ? (int) (255 * t) : (int) (255 * (1.0 - t));

            vdr_tile(C, x, y, 255 - oa);
            vdr_tile(O, x, y, oa);
            return true;
        }
        vd = vdr_vart(a->kind == 1 ? a->from : a->to);
        ox = x + ct->xmod, oy = y + ct->ymod, mid = (sp->pl + sp->pr) / 2;
        shade = (int) (255 * (1.0 - 0.22 * fabs(sin(2.0 * th))));
        lcy = vd ? -K : K, lax = vd ? -1.0 : 1.0;
        /* both leaves open away from the viewer: up-left (V art) / up-right (H) */
        vdr_leaf(sp, ox, oy, sp->pl, sp->pl, mid, 1.0, lcy, lax, -K, th, shade, la);
        vdr_leaf(sp, ox, oy, sp->pr, mid, sp->pr, -1.0, -lcy, lax, -K, th, shade, la);
        vdr_img(sp->frame, sp->nframe, ox, oy, 255);
        if (fade > 0)
            vdr_tile(O, x, y, fade);
        return true;
    }
}

static void
vdr_wave(int x, int y, float rad, int r, int g, int b, Uint32 dur)
{
    vsp_wave_t w;

    w.x = x, w.y = y, w.t0 = SDL_GetTicks(), w.dur = dur, w.rad = rad;
    w.r = (Uint8) r, w.g = (Uint8) g, w.b = (Uint8) b;
    if (vsp_waves.size() < 16)
        vsp_waves.push_back(w);
}

/* dust along a door's sill (n puffs), and a little shaken off its lintel */
static void
vdr_dust(int x, int y, int tile, int n)
{
    double sy = vdr_vart(tile) ? -VDR_K : VDR_K;
    int k;

    for (k = 0; k < n; k++) {
        double s = (vsp_frand() * 2.0 - 1.0) * 20.0;

        vsp_spawn(x, y, s, s * sy + vsp_frand() * 4 - 2, 2, vsp_frand() * 16 - 8, 6 + vsp_frand() * 8,
                  6 + vsp_frand() * 6, 0, 0.9, 3.5, 10, 650 + rand() % 300, 160, 145, 120, 3, 85);
    }
    for (k = 0; k < (n + 1) / 2; k++) {
        double s = (vsp_frand() * 2.0 - 1.0) * 16.0;

        vsp_spawn(x, y, s, s * sy, 70 + vsp_frand() * 10, 0, 0, -10, -120, 0.5, 0.9, 0, 700 + rand() % 300,
                  175, 160, 135, 5, 170);
    }
    vsp_live_t = SDL_GetTicks();
}

/* a door smashed in: splinters thrown the way it falls, dust, grit off the arch */
static void
vdr_burst(int x, int y, int from, float push)
{
    bool vd = vdr_vart(from);
    double ax = push * (vd ? -1.0 : 1.0), ay = push * -VDR_K, sy = vd ? -VDR_K : VDR_K;
    int k;

    for (k = 0; k < 26; k++) {
        double s = (vsp_frand() * 2.0 - 1.0) * 18.0, sp = 40 + vsp_frand() * 110;
        int sh = rand() % 50;

        vsp_spawn(x, y, s, s * sy, 10 + vsp_frand() * 60, ax * sp + vsp_frand() * 40 - 20,
                  ay * sp + vsp_frand() * 20 - 10, 40 + vsp_frand() * 110, -400, 0.6, 1.2 + vsp_frand() * 1.1, 0,
                  600 + rand() % 500, 115 + sh, 78 + sh / 2, 45 + sh / 3, 5, 235);
    }
    for (k = 0; k < 8; k++) {
        double s = (vsp_frand() * 2.0 - 1.0) * 22.0;

        vsp_spawn(x, y, s, s * sy, 4 + vsp_frand() * 20, ax * 20 + vsp_frand() * 20 - 10,
                  ay * 20 + vsp_frand() * 10 - 5, 10 + vsp_frand() * 10, 0, 0.8, 5, 14, 900 + rand() % 400, 160,
                  145, 122, 3, 75);
    }
    for (k = 0; k < 6; k++) {
        double s = (vsp_frand() * 2.0 - 1.0) * 20.0;

        vsp_spawn(x, y, s, s * sy, 75 + vsp_frand() * 12, vsp_frand() * 30 - 15, vsp_frand() * 14 - 7, 10, -380,
                  0.4, 1.3, 0, 600 + rand() % 300, 175, 168, 155, 5, 220);
    }
    vsp_jolt(x, y, 3.0);
    vsp_live_t = SDL_GetTicks();
}

/* a door conjured into an empty doorway: dust gathers in, a glimmer */
static void
vdr_gather(int x, int y)
{
    int k;

    for (k = 0; k < 16; k++) {
        double a = vsp_frand() * 6.2832, r = 30 + vsp_frand() * 18, px = cos(a) * r, py = sin(a) * r * 0.45;

        vsp_spawn(x, y, px, py, 6 + vsp_frand() * 56, -px * 1.5, -py * 1.5, 0, 0, 0.2, 4, 4, 600 + rand() % 200,
                  170, 155, 130, 3, 95);
    }
    for (k = 0; k < 8; k++)
        vsp_spawn(x, y, vsp_frand() * 30 - 15, vsp_frand() * 12 - 6, 10 + vsp_frand() * 60, 0, 0, 8, 0, 0.3, 1.3,
                  0, 700 + rand() % 300, 255, 245, 220, 4, 230);
    vl_flash(x, y, 0.95f, 0.88f, 0.75f, 0.35f, 1.6f, 650);
    vsp_live_t = SDL_GetTicks();
}

/* mapdata::set_glyph: the furniture shown on (x, y) is changing */
void
vdr_furniture(int x, int y, int from, int to)
{
    int cf = vdr_class(from), ct = vdr_class(to), kind;
    Uint32 now = SDL_GetTicks();
    vdr_anim_t a;
    size_t k, n = 0;

    if (!cf || !ct || cf == ct || !levwin || !u.ux || !iflags.window_inited || !isok(x, y) || !cansee(x, y)
        || u.uswallow)
        return;
    if (!on_level(&vdr_lev, &u.uz)) {
        vdr_lev = u.uz;
        vdr_anims.clear();
    }
    kind = cf == 1 && ct == 2 ? 1 : cf == 2 && ct == 1 ? 2 : ct == 3 ? 3 : 4;
    for (k = 0; k < vdr_anims.size(); k++) /* one at a time on a square */
        if (vdr_anims[k].x != x || vdr_anims[k].y != y)
            vdr_anims[n++] = vdr_anims[k];
    vdr_anims.resize(n);
    if (n >= 12)
        return;
    a.x = x, a.y = y, a.from = from, a.to = to, a.kind = kind, a.t0 = now, a.fx = false, a.push = 1.0f;
    a.dur = kind == 1 ? 400 : kind == 2 ? 380 : kind == 3 ? 650 : 720;
    if (kind == 3) { /* it falls away from you */
        int hx, hy, dx, dy;

        vsp_screen(u.ux, u.uy, &hx, &hy);
        vsp_screen(x, y, &dx, &dy);
        a.push = ((dx - hx) * (vdr_vart(from) ? -1.0 : 1.0) + (dy - hy) * -VDR_K) >= 0.0 ? 1.0f : -1.0f;
        vdr_burst(x, y, from, a.push);
    } else if (kind == 4) {
        vdr_gather(x, y);
    } else if (kind == 1) {
        vdr_dust(x, y, to, 2);
    }
    vdr_anims.push_back(a);
    vsp_live_t = now;
}

/* the core: something in the world used or found on (x, y) - 1 a fountain
   drunk from, 2 dipped into, 3 a sink's tap, 4 / 5 an altar's amber / black
   flash, 6 a thing landing on an altar, 7 an altar's flash seen
   hallucinating, 8 a throne sat on, 9 a chest or box opened, 10 a hidden
   door or passage found */
extern "C" void
vulture_world_fx(int x, int y, int kind)
{
    static const Uint8 amber[3] = { 255, 185, 70 };
    static const Uint8 hal[6][3] = { { 255, 80, 200 }, { 80, 255, 120 }, { 90, 160, 255 },
                                     { 255, 240, 70 }, { 255, 120, 40 }, { 170, 90, 255 } };
    int k, n;

    if (!levwin || !u.ux || !isok(x, y) || !cansee(x, y))
        return;
    switch (kind) {
    case 1: /* water wells up and falls back */
    case 2: /* a bigger splash */
        n = kind == 2 ? 20 : 11;
        for (k = 0; k < n; k++) {
            double a = vsp_frand() * 6.2832, sp = 8 + vsp_frand() * (kind == 2 ? 60 : 34);

            vsp_spawn(x, y, cos(a) * 5, sin(a) * 2.5, 28, cos(a) * sp, sin(a) * sp * 0.45,
                      60 + vsp_frand() * (kind == 2 ? 100 : 60), -400, 0.3, 1.2 + vsp_frand() * 0.8, 0,
                      550 + rand() % 350, 140 + rand() % 50, 185 + rand() % 45, 255, 5, 215);
        }
        for (k = 0; k < 4; k++) /* glints on it */
            vsp_spawn(x, y, vsp_frand() * 16 - 8, vsp_frand() * 8 - 4, 30 + vsp_frand() * 20, 0, 0, 10, 0, 0.3, 1.2,
                      0, 500 + rand() % 300, 210, 235, 255, 4, 230);
        vdr_wave(x, y, 0.4f, 120, 175, 255, 520);
        if (kind == 2)
            vdr_wave(x, y, 0.75f, 100, 160, 255, 700);
        vsp_spawn(x, y, 0, 0, 22, 0, 0, 6, 0, 0.3, 6, 10, 800, 150, 195, 255, 1, 34); /* a breath of mist */
        break;
    case 3: /* the tap runs */
        for (k = 0; k < 9; k++)
            vsp_spawn(x, y, vsp_frand() * 4 - 2, vsp_frand() * 2 - 1, 36 + vsp_frand() * 6, vsp_frand() * 10 - 5,
                      vsp_frand() * 4 - 2, -20 - vsp_frand() * 30, -420, 0.2, 1.1, 0, 300 + k * 60, 150, 195, 255, 5,
                      210);
        vdr_wave(x, y, 0.3f, 120, 175, 255, 420);
        break;
    case 4: /* amber */
    case 7: { /* hallucinating: any colour at all */
        const Uint8 *c = kind == 4 ? amber : hal[rand() % 6];

        vl_flash(x, y, c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 0.85f, 2.6f, 520);
        for (k = 0; k < 14; k++) {
            double a = vsp_frand() * 6.2832, r = vsp_frand() * 14;

            vsp_spawn(x, y, cos(a) * r, sin(a) * r * 0.5, 14 + vsp_frand() * 10, cos(a) * 12, sin(a) * 6,
                      30 + vsp_frand() * 40, 0, 0.6, 1.3, 0, 700 + rand() % 400, c[0], c[1], c[2], 4, 255);
        }
        vsp_spawn(x, y, 0, 0, 18, 0, 0, 10, 0, 0.3, 8, 22, 500, c[0], c[1], c[2], 1, 110);
        vdr_wave(x, y, 0.7f, c[0], c[1], c[2], 480);
        break;
    }
    case 5: /* black: darkness puffs out of it */
        for (k = 0; k < 12; k++) {
            double a = vsp_frand() * 6.2832, sp = 20 + vsp_frand() * 40;

            vsp_spawn(x, y, 0, 0, 16 + vsp_frand() * 8, cos(a) * sp, sin(a) * sp * 0.5, 14 + vsp_frand() * 20, 0,
                      1.0, 5, 14, 700 + rand() % 300, 0, 0, 0, 2, 150);
        }
        vsp_spawn(x, y, 0, 0, 16, 0, 0, 0, 0, 0.5, 10, 36, 450, 0, 0, 0, 2, 170);
        break;
    case 6: /* it lands: a little dust */
        for (k = 0; k < 3; k++)
            vsp_spawn(x, y, vsp_frand() * 12 - 6, vsp_frand() * 6 - 3, 16, vsp_frand() * 16 - 8, vsp_frand() * 8 - 4,
                      8, 0, 0.8, 3, 8, 600, 160, 150, 135, 3, 70);
        break;
    case 8: /* a throne: a golden shimmer */
        vl_flash(x, y, 1.0f, 0.84f, 0.45f, 0.5f, 2.2f, 650);
        for (k = 0; k < 16; k++) {
            double a = vsp_frand() * 6.2832, r = 6 + vsp_frand() * 16;

            vsp_spawn(x, y, cos(a) * r, sin(a) * r * 0.5, 8 + vsp_frand() * 50, 0, 0, 14 + vsp_frand() * 24, 0, 0.5,
                      1.3, 0, 800 + rand() % 500, 255, 215 + rand() % 30, 110 + rand() % 60, 4, 240);
        }
        vsp_spawn(x, y, 0, 0, 28, 0, 0, 6, 0, 0.3, 9, 14, 700, 255, 210, 120, 1, 70);
        break;
    case 9: /* a lid lifts: dust */
        for (k = 0; k < 4; k++)
            vsp_spawn(x, y, vsp_frand() * 14 - 7, vsp_frand() * 6 - 3, 14 + vsp_frand() * 6, vsp_frand() * 14 - 7,
                      vsp_frand() * 6 - 3, 14 + vsp_frand() * 10, 0, 0.8, 3.5, 10, 700 + rand() % 300, 165, 150,
                      125, 3, 80);
        for (k = 0; k < 6; k++)
            vsp_spawn(x, y, vsp_frand() * 16 - 8, vsp_frand() * 8 - 4, 16, vsp_frand() * 24 - 12,
                      vsp_frand() * 12 - 6, 30 + vsp_frand() * 30, -200, 0.6, 0.9, 0, 700 + rand() % 300, 190, 175,
                      150, 5, 170);
        break;
    case 10: /* found: grit trickles down, dust, a glint */
        for (k = 0; k < 14; k++)
            vsp_spawn(x, y, vsp_frand() * 30 - 15, vsp_frand() * 12 - 6, 40 + vsp_frand() * 45,
                      vsp_frand() * 10 - 5, vsp_frand() * 4 - 2, -vsp_frand() * 10, -300, 0.4,
                      1.1 + vsp_frand() * 0.6, 0, 700 + rand() % 400, 170, 160, 145, 5, 210);
        for (k = 0; k < 6; k++)
            vsp_spawn(x, y, vsp_frand() * 28 - 14, vsp_frand() * 10 - 5, 20 + vsp_frand() * 40,
                      vsp_frand() * 10 - 5, vsp_frand() * 4 - 2, 4, 0, 0.6, 4, 12, 900 + rand() % 400, 165, 155,
                      140, 3, 80);
        for (k = 0; k < 3; k++)
            vsp_spawn(x, y, vsp_frand() * 20 - 10, vsp_frand() * 8 - 4, 30 + vsp_frand() * 30, 0, 0, 6, 0, 0.3,
                      1.4, 0, 600, 255, 255, 240, 4, 255);
        vl_flash(x, y, 0.95f, 0.95f, 1.0f, 0.3f, 1.4f, 320);
        break;
    default:
        return;
    }
    vsp_live_t = SDL_GetTicks();
}

/* the overlay pass: swings that have run their course go; a shutting door's
   slam raises its dust */
static void
vdr_tick(Uint32 now)
{
    size_t k, n = 0;

    if (!on_level(&vdr_lev, &u.uz)) {
        vdr_lev = u.uz;
        vdr_anims.clear();
    }
    for (k = 0; k < vdr_anims.size(); k++) {
        vdr_anim_t a = vdr_anims[k];
        double t = vdr_freeze >= 0.0 ? vdr_freeze : (double) (now - a.t0) / a.dur;

        if (a.kind == 2 && !a.fx && t >= 0.78) {
            a.fx = true;
            vdr_dust(a.x, a.y, a.to, 7);
        }
        if (t >= 1.0 && vdr_freeze < 0.0)
            continue;
        vdr_anims[n++] = a;
    }
    vdr_anims.resize(n);
    if (n)
        vsp_live_t = now; /* frames keep coming */
}

/* probe: s203 - VULTURE_SELFTEST_DOOR: the nearest door (then one with the
   other art) opened, shut, smashed and conjured back as the core would set
   it, the swings shot held at set points; then each world effect */
static Uint32 vdr_probe_t = 0;
static int vdr_probe_n = 0, vdr_probe_sub = 0, vdr_probe_shots = 0, vdr_probe_dx = 0, vdr_probe_dy = 0;
static int vdr_probe_art = -1;

static void
vdr_probe_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];
    SDL_Surface *o;
    SDL_Rect a;
    int hx, hy, ex, ey;

    if (!dir || vdr_probe_shots >= 70 || !vulture_screen || vulture_screen->w < 960 || vulture_screen->h < 540)
        return;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vsp_screen(u.ux, u.uy, &hx, &hy);
    vsp_screen(vdr_probe_dx ? vdr_probe_dx : u.ux, vdr_probe_dx ? vdr_probe_dy : u.uy, &ex, &ey);
    a.w = 480, a.h = 320, a.x = (hx + ex) / 2 - 240, a.y = (hy + ey) / 2 - 200;
    a.x = a.x < 0 ? 0 : a.x + a.w > vulture_screen->w ? vulture_screen->w - a.w : a.x;
    a.y = a.y < 0 ? 0 : a.y + a.h > vulture_screen->h ? vulture_screen->h - a.h : a.y;
    if (!(o = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return;
    snprintf(p, sizeof p, "%s\\door%02d_%s_%02d.bmp", dir, vdr_probe_n, tag, vdr_probe_shots++);
    if (vulture_frame_to_surface(o, &a))
        SDL_SaveBMP(o, p);
    SDL_FreeSurface(o);
}

/* the nearest door with a room floor square beside it (other: one whose
   art lies the other way); you are put on that square */
static bool
vdr_probe_pick(bool other)
{
    static const int ox4[4] = { 1, -1, 0, 0 }, oy4[4] = { 0, 0, 1, -1 };
    int x, y, k, best = 9999, bx = 0, by = 0, bfx = 0, bfy = 0;

    for (x = 1; x < COLNO; x++)
        for (y = 0; y < ROWNO; y++) {
            if (levl[x][y].typ != DOOR
                || (other && ((int) levl[x][y].horizontal == vdr_probe_art || (x == vdr_probe_dx && y == vdr_probe_dy))))
                continue;
            for (k = 0; k < 4; k++) {
                int fx = x + ox4[k], fy = y + oy4[k], d;

                if (!isok(fx, fy) || levl[fx][fy].typ != ROOM || m_at(fx, fy) || t_at(fx, fy))
                    continue;
                d = distmin(u.ux, u.uy, fx, fy);
                if (d < best)
                    best = d, bx = x, by = y, bfx = fx, bfy = fy;
            }
        }
    if (best == 9999)
        return false;
    vdr_probe_dx = bx, vdr_probe_dy = by, vdr_probe_art = (int) levl[bx][by].horizontal;
    if (bfx != u.ux || bfy != u.uy)
        teleds(bfx, bfy, TELEDS_NO_FLAGS);
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "door probe: door %d,%d (%s), you at %d,%d\n", bx, by,
                      vdr_probe_art ? "horizontal" : "vertical", u.ux, u.uy);
    return true;
}

static void
vdr_probe_set(int mask) /* the probe door's state, as the core sets it */
{
    levl[vdr_probe_dx][vdr_probe_dy].doormask = mask;
    recalc_block_point(vdr_probe_dx, vdr_probe_dy);
    gv.vision_full_recalc = 1;
    newsym(vdr_probe_dx, vdr_probe_dy);
    flush_screen(1);
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "door probe: mask %d, %d swinging\n", mask,
                      (int) vdr_anims.size());
}

static void
vdr_probe_tick(Uint32 now)
{
    static const double hold[2][5] = { { 0.12, 0.3, 0.5, 0.7, 0.88 }, { 0.12, 0.35, 0.6, 0.8, 0.9 } };
    static const int live[2][5] = { { 30, 120, 260, 430, 720 }, { 100, 260, 420, 600, 800 } };
    int step = vdr_probe_n;

    if (vdr_probe_sub > 0) { /* this step's frames */
        int s = vdr_probe_sub - 1;

        if (step == 1 || step == 2 || step == 6 || step == 7) { /* the swing, held */
            vdr_freeze = hold[step == 2 || step == 7][s];
            vdr_probe_shot(step == 1 || step == 6 ? "open" : "shut");
            if (++vdr_probe_sub > 5)
                vdr_freeze = -1.0, vdr_probe_sub = 0, vdr_probe_n++, vdr_probe_t = now;
            return;
        }
        if (step == 3 || step == 4) {
            if (now - vdr_probe_t < (Uint32) live[step == 4][s])
                return;
            vdr_probe_shot(step == 3 ? "smash" : "conjure");
            if (++vdr_probe_sub > 5)
                vdr_probe_sub = 0, vdr_probe_n++, vdr_probe_t = now;
            return;
        }
        if (now - vdr_probe_t < (Uint32) (s == 0 ? 90 : 330))
            return;
        vdr_probe_shot("world");
        if (++vdr_probe_sub > 2)
            vdr_probe_sub = 0, vdr_probe_n++, vdr_probe_t = now;
        return;
    }
    if (now - vdr_probe_t < 650)
        return;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "door probe %d\n", step);
    switch (step) {
    case 0:
    case 5: /* a door, you beside it, shut */
        if (!vdr_probe_pick(step == 5)) {
            vdr_probe_n = 8, vdr_probe_t = now;
            return;
        }
        vdr_probe_set(D_CLOSED);
        vdr_probe_n++, vdr_probe_t = SDL_GetTicks();
        return;
    case 1:
    case 6:
        vdr_probe_set(D_ISOPEN);
        break;
    case 2:
    case 4:
    case 7:
        vdr_probe_set(D_CLOSED);
        break;
    case 3:
        vdr_probe_set(D_BROKEN);
        break;
    default:
        if (step >= 8 && step <= 17) {
            int k = step - 7, wx = u.ux, wy = u.uy;

            if (k == 10 && vdr_probe_dx)
                wx = vdr_probe_dx, wy = vdr_probe_dy;
            vulture_world_fx(wx, wy, k);
            break;
        }
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "door probe: done, %d shots\n", vdr_probe_shots);
        vdr_probe_t = 0;
        return;
    }
    vdr_probe_sub = 1, vdr_probe_t = SDL_GetTicks();
}

/*
 * IMPLEMENTATION_LOG s204: more of the room's own perspective.
 *  - A slain creature falls over the way something standing on the
 *    isometric floor would: it pivots on its feet away from the blow that
 *    killed it (who struck it, a missile's line, else away from you), the
 *    sprite shearing along that floor direction and foreshortening as it
 *    goes down (one quad), lands with a thud, rocks back and fades, its
 *    corpse fading in as it lands.  On the clock - it never holds you up
 *    (s79's spin-and-sink was a blocking 170 ms).  A ghost thins upward.
 *  - A pushed boulder rolls into its square: the shadow baked under it (its
 *    translucent dark pixels) slides along the floor while the rock turns
 *    about its centre as far as a ball its size would over that ground,
 *    shoved fast and slowed by friction.
 */
#define VDTH_TALL 80.0 /* screen px of a creature's height to a square's length of floor */
struct vdth_t {
    int x, y, tile, face, heft;
    float fx, fy; /* the floor direction it falls: screen px per px of height */
    Uint32 t0, dur;
    bool ghost;
};
static std::vector<vdth_t> vdth_list;
struct vbr_art_t {
    SDL_Surface *rock, *shadow, *nrock; /* nrock: the rock's normals (no shadow) */
    float cx, cy, rad; /* the rock's centre and size in the art */
    int tile;
    bool ok, tried;
};
static vbr_art_t vbr_art = { NULL, NULL, NULL, 0.0f, 0.0f, 0.0f, -1, false, false };
struct vbr_t {
    int x, y;     /* where the map shows it now */
    float ox, oy; /* where it came from: screen px from there */
    Uint32 t0, dur;
};
static std::vector<vbr_t> vbr_list;
static d_level vfall_lev = { -1, -1 };
static double vfall_freeze = -1.0; /* probe: >= 0 holds every fall and roll at this point of it */

/* a floor direction seen on screen as (ux, uy): the screen px a body 1 px
   tall covers lying down that way - the iso projection undone, the
   direction made a unit on the floor, projected again */
static void
vdth_floor(double ux, double uy, float *fx, float *fy)
{
    double va = (ux / V_MAP_XMOD + uy / V_MAP_YMOD) / 2.0, vb = (uy / V_MAP_YMOD - ux / V_MAP_XMOD) / 2.0;
    double len = sqrt(va * va + vb * vb);

    if (len < 1e-6) { /* no way known: toward you */
        *fx = 0.0f, *fy = (float) (V_MAP_YMOD * 2.0 / (1.4142 * VDTH_TALL));
        return;
    }
    *fx = (float) (V_MAP_XMOD * (va - vb) / (len * VDTH_TALL));
    *fy = (float) (V_MAP_YMOD * (va + vb) / (len * VDTH_TALL));
}

static void
vdth_start(int x, int y, int tile, int heft, int face, bool ghost)
{
    Uint32 now = SDL_GetTicks();
    vdth_t d;
    double ux = 0.0, uy = 0.0;
    int sx, sy, side;
    size_t k, n = 0;

    if (!isok(x, y))
        return;
    if (!on_level(&vfall_lev, &u.uz)) {
        vfall_lev = u.uz;
        vdth_list.clear(), vbr_list.clear();
    }
    if (vimp_last.t && now - vimp_last.t < 2000 && vimp_last.tx == x && vimp_last.ty == y
        && (vimp_last.ax != x || vimp_last.ay != y)) { /* away from who struck it */
        vtw_proj(x - vimp_last.ax, y - vimp_last.ay, &sx, &sy);
        ux = sx, uy = sy;
    } else if (vfl_end_t && now - vfl_end_t < 2000 && distmin(vfl_end_x, vfl_end_y, x, y) <= 1
               && (vfl_end_dx != 0.0f || vfl_end_dy != 0.0f)) { /* along the missile's line */
        ux = vfl_end_dx, uy = vfl_end_dy;
    } else if (u.ux != x || u.uy != y) { /* away from you */
        vtw_proj(x - u.ux, y - u.uy, &sx, &sy);
        ux = sx, uy = sy;
    } else {
        vtw_proj(rand() % 2 ? 1 : -1, 0, &sx, &sy);
        ux = sx, uy = sy;
    }
    for (k = 0; k < vdth_list.size(); k++) /* one body a square */
        if (vdth_list[k].x != x || vdth_list[k].y != y)
            vdth_list[n++] = vdth_list[k];
    vdth_list.resize(n);
    if (n >= 12)
        return;
    d.x = x, d.y = y, d.tile = tile, d.face = face, d.heft = heft, d.ghost = ghost;
    /* back along the floor, on the side the blow pushed it: falling toward
       you the flat sprite would turn edge-on and thin to a sliver */
    side = ux > 0.5 ? 1 : ux < -0.5 ? -1 : (rand() % 2 ? 1 : -1);
    vdth_floor(side * V_MAP_XMOD, -V_MAP_YMOD, &d.fx, &d.fy);
    d.t0 = now, d.dur = ghost ? 520 : 600;
    vdth_list.push_back(d);
    vsp_live_t = now;
}

/* step 5 of the map: a body falling over on (j, i); false: none there */
static bool
vdth_draw(int j, int i, int x, int y)
{
    Uint32 now;
    size_t k;
    bool any = false;

    if (vdth_list.empty())
        return false;
    now = SDL_GetTicks();
    for (k = 0; k < vdth_list.size(); k++) {
        const vdth_t &d = vdth_list[k];
        vulture_tile *tile;
        SDL_Surface *img;
        double t = vfall_freeze >= 0.0 ? vfall_freeze : (double) (now - d.t0) / d.dur, ph, H, dx, dy;
        int ix, iy, art, al, g;
        float q[8];
        SDL_Rect r;

        if (d.x != j || d.y != i || t >= 1.0 || !(tile = vulture_get_tile(d.tile)) || !tile->graphic)
            continue;
        any = true;
        img = tile->graphic, ix = x + tile->xmod, iy = y + tile->ymod;
        art = vtw_art_facing(d.tile, tile->graphic); /* as vtw_put_tile_x faces it */
        if (d.face != 0 && ((art == 0 && d.face < 0) || (art != 0 && art != d.face))) {
            SDL_Surface *f = vtw_flipped(d.tile, tile->graphic);

            if (f)
                img = f, ix = x - tile->xmod - tile->graphic->w + 1;
        }
        if (d.ghost) { /* it thins away upward */
            vtx_blit(img, ix, iy - (int) (18 * t), 1.0 - 0.1 * t, 1.0 + 0.15 * t, 0.0, false,
                     (int) (220 * (1.0 - t)));
            continue;
        }
        if (t < 0.55) { /* down, faster as it goes */
            double u = t / 0.55;

            ph = 1.35 * u * u;
        } else if (t < 0.72) { /* it lands and rocks back a little */
            ph = 1.35 - 0.1 * sin(3.14159 * (t - 0.55) / 0.17);
        } else {
            ph = 1.35;
        }
        H = img->h;
        dx = H * d.fx * sin(ph), dy = H * (1.0 - cos(ph)) + H * d.fy * sin(ph);
        al = t < 0.62 ? 255 : (int) (255 * (1.0 - (t - 0.62) / 0.38));
        g = t < 0.3 ? 255 - (int) (150 * (1.0 - t / 0.3)) : 255; /* red from the blow, as s79 had it */
        r.x = 0, r.y = 0, r.w = img->w, r.h = img->h;
        q[0] = (float) (ix + dx), q[1] = (float) (iy + dy);
        q[2] = (float) (ix + img->w + dx), q[3] = q[1];
        q[4] = (float) (ix + img->w), q[5] = (float) (iy + img->h);
        q[6] = (float) ix, q[7] = q[5];
        vdr_quad_rgb(img, r, q, 255, g, g, al);
        vdr_quad_normal(vulture_get_tile_normal(d.tile, img != tile->graphic), r, q, al);
    }
    return any;
}

/* the corpse a falling body leaves waits for it to land: the alpha to draw
   what the map shows on (j, i) with (255: as usual) */
static int
vdth_object_alpha(int j, int i)
{
    Uint32 now;
    size_t k;

    if (vdth_list.empty() || !map_data || !isok(j, i) || !glyph_is_body(map_data->get_glyph(MAP_GLYPH, j, i)))
        return 255;
    now = SDL_GetTicks();
    for (k = 0; k < vdth_list.size(); k++) {
        const vdth_t &d = vdth_list[k];
        double t;

        if (d.x != j || d.y != i || d.ghost)
            continue;
        t = vfall_freeze >= 0.0 ? vfall_freeze : (double) (now - d.t0) / d.dur;
        return t < 0.5 ? 0 : t >= 0.75 ? 255 : (int) (255 * (t - 0.5) / 0.25);
    }
    return 255;
}

/* the boulder art split once into the rock and the shadow baked under it
   (translucent, dark, in its lower part); NULL: another art, rolled whole */
static vbr_art_t *
vbr_split(int tile_id)
{
    vulture_tile *tile;
    SDL_Surface *src;
    int x, y, n = 0, x0, y0, x1 = 0, y1 = 0;

    if (vbr_art.tried)
        return vbr_art.ok && vbr_art.tile == tile_id ? &vbr_art : NULL;
    vbr_art.tried = true, vbr_art.tile = tile_id;
    tile = vulture_get_tile(tile_id);
    if (!tile || !tile->graphic
        || !(src = SDL_ConvertSurfaceFormat(tile->graphic, SDL_PIXELFORMAT_ARGB8888, 0)))
        return NULL;
    vbr_art.rock = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    vbr_art.shadow = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!vbr_art.rock || !vbr_art.shadow) {
        SDL_FreeSurface(src);
        if (vbr_art.rock)
            SDL_FreeSurface(vbr_art.rock);
        if (vbr_art.shadow)
            SDL_FreeSurface(vbr_art.shadow);
        vbr_art.rock = vbr_art.shadow = NULL;
        return NULL;
    }
    x0 = src->w, y0 = src->h;
    SDL_LockSurface(src);
    SDL_LockSurface(vbr_art.rock);
    SDL_LockSurface(vbr_art.shadow);
    for (y = 0; y < src->h; y++) {
        const Uint32 *row = (const Uint32 *) ((const Uint8 *) src->pixels + y * src->pitch);
        Uint32 *rk = (Uint32 *) ((Uint8 *) vbr_art.rock->pixels + y * vbr_art.rock->pitch);
        Uint32 *sd = (Uint32 *) ((Uint8 *) vbr_art.shadow->pixels + y * vbr_art.shadow->pitch);

        for (x = 0; x < src->w; x++) {
            Uint32 p = row[x];
            int a = (int) (p >> 24), r = (int) ((p >> 16) & 255), g = (int) ((p >> 8) & 255), b = (int) (p & 255);
            bool sh = a > 0 && a < 200 && (r * 3 + g * 6 + b) / 10 < 70 && y > src->h * 0.55;

            rk[x] = sh ? 0 : p;
            sd[x] = sh ? p : 0;
            if (!sh && a >= 128) {
                n++;
                if (x < x0)
                    x0 = x;
                if (x + 1 > x1)
                    x1 = x + 1;
                if (y < y0)
                    y0 = y;
                if (y + 1 > y1)
                    y1 = y + 1;
            }
        }
    }
    SDL_UnlockSurface(vbr_art.shadow);
    SDL_UnlockSurface(vbr_art.rock);
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    SDL_SetSurfaceBlendMode(vbr_art.rock, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceBlendMode(vbr_art.shadow, SDL_BLENDMODE_BLEND);
    if (n < 200) {
        SDL_FreeSurface(vbr_art.rock);
        SDL_FreeSurface(vbr_art.shadow);
        vbr_art.rock = vbr_art.shadow = NULL;
        return NULL;
    }
    vbr_art.cx = (x0 + x1) / 2.0f, vbr_art.cy = (y0 + y1) / 2.0f;
    vbr_art.rad = ((x1 - x0) + (y1 - y0)) / 4.0f;
    { /* the rock's normals, without the shadow's */
        SDL_Surface *nm = vulture_get_tile_normal(tile_id, false);

        if (nm && nm->w == vbr_art.rock->w && nm->h == vbr_art.rock->h
            && (vbr_art.nrock = SDL_ConvertSurfaceFormat(nm, SDL_PIXELFORMAT_ARGB8888, 0)) != NULL) {
            SDL_LockSurface(vbr_art.nrock);
            SDL_LockSurface(vbr_art.shadow);
            for (y = 0; y < nm->h; y++) {
                Uint32 *nr = (Uint32 *) ((Uint8 *) vbr_art.nrock->pixels + y * vbr_art.nrock->pitch);
                const Uint32 *sd = (const Uint32 *) ((const Uint8 *) vbr_art.shadow->pixels + y * vbr_art.shadow->pitch);

                for (x = 0; x < nm->w; x++)
                    if (sd[x] >> 24)
                        nr[x] = 0;
            }
            SDL_UnlockSurface(vbr_art.shadow);
            SDL_UnlockSurface(vbr_art.nrock);
            SDL_SetSurfaceBlendMode(vbr_art.nrock, SDL_BLENDMODE_BLEND);
        }
    }
    vulture_gpu_hd_derive_cut(vbr_art.rock, tile->graphic, vbr_art.shadow, true); /* zoomed: its HD art */
    vulture_gpu_hd_derive_cut(vbr_art.shadow, tile->graphic, vbr_art.shadow, false);
    vbr_art.ok = true;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "boulder art: rock %d px, centre %.0f,%.0f, radius %.0f\n", n,
                      vbr_art.cx, vbr_art.cy, vbr_art.rad);
    return &vbr_art;
}

/* img turned th radians (clockwise) about its point (cx, cy) - the sprite
   transformer turns about the feet, so the feet are moved to make up */
static void
vbr_turn(SDL_Surface *img, SDL_Surface *nm, int dx, int dy, double cx, double cy, double th)
{
    double vx = cx - img->w / 2.0, vy = cy - img->h, c = cos(th), s = sin(th);
    double mx = vx - (vx * c - vy * s), my = vy - (vx * s + vy * c);
    int px = dx + (int) floor(mx + 0.5), py = dy + (int) floor(my + 0.5);

    vtx_blit(img, px, py, 1.0, 1.0, th * 57.29578, false, 255);
    if (nm && vulture_normal_capture && vulture_gpumap_on) /* s204: its own relief, turned with it */
        vulture_gpumap_normal_ex(nm, px, py, 1.0, 1.0, th * 57.29578, 255);
}

static double
vbr_ease(const vbr_t &r, Uint32 now) /* how far it has come: shoved, then friction */
{
    double t = (double) (now - r.t0) / r.dur;

    if (vfall_freeze >= 0.0)
        return vfall_freeze;
    return t >= 1.0 ? 1.0 : 1.0 - (1.0 - t) * (1.0 - t);
}

/* step 4 of the map: a boulder rolling into (j, i) */
static bool
vbr_draw(int j, int i, int x, int y, int tile_id)
{
    Uint32 now;
    vulture_tile *tile;
    vbr_art_t *sp;
    const vbr_t *r = NULL;
    double e, mx, rad, th;
    size_t k;
    int bx, by;

    if (vbr_list.empty())
        return false;
    now = SDL_GetTicks();
    for (k = 0; k < vbr_list.size(); k++)
        if (vbr_list[k].x == j && vbr_list[k].y == i
            && (vfall_freeze >= 0.0 || now - vbr_list[k].t0 < vbr_list[k].dur))
            r = &vbr_list[k];
    if (!r || !(tile = vulture_get_tile(tile_id)) || !tile->graphic)
        return false;
    e = vbr_ease(*r, now);
    bx = x + tile->xmod + (int) floor(r->ox * (1.0 - e) + 0.5);
    by = y + tile->ymod + (int) floor(r->oy * (1.0 - e) + 0.5);
    sp = vbr_split(tile_id);
    rad = sp ? sp->rad : tile->graphic->h * 0.45;
    /* the turn of a ball its size over the ground covered: across the
       screen it reads as rolling; toward or away from you a little less */
    mx = -r->ox;
    if (fabs(mx) < 1.0)
        mx = 0.35 * fabs(r->oy);
    th = mx * e / (rad > 4.0 ? rad : 4.0);
    if (sp) {
        vdr_img(sp->shadow, NULL, bx, by, 255); /* the shadow slides; the rock rolls */
        vbr_turn(sp->rock, sp->nrock, bx, by, sp->cx, sp->cy, th);
    } else {
        vbr_turn(tile->graphic, vulture_get_tile_normal(tile_id, false), bx, by, tile->graphic->w / 2.0,
                 tile->graphic->h / 2.0, th);
    }
    return true;
}

static bool
vbr_offset(int j, int i, int *ox, int *oy) /* a boulder rolling into (j, i): how far back it is */
{
    Uint32 now = SDL_GetTicks();
    size_t k;

    for (k = 0; k < vbr_list.size(); k++) {
        const vbr_t &r = vbr_list[k];
        double e;

        if (r.x != j || r.y != i || (vfall_freeze < 0.0 && now - r.t0 >= r.dur))
            continue;
        e = vbr_ease(r, now);
        *ox = (int) floor(r.ox * (1.0 - e) + 0.5), *oy = (int) floor(r.oy * (1.0 - e) + 0.5);
        return true;
    }
    return false;
}

/* the core (dopush): a boulder pushed from (fx, fy) to (tx, ty) */
extern "C" void
vulture_boulder_roll(int fx, int fy, int tx, int ty)
{
    vbr_t r;
    int sx, sy;
    size_t k, n = 0;

    /* the square it leaves: vision still has the boulder there when the
       core moves it, so the square beyond counts as out of sight */
    if (!levwin || !u.ux || !isok(fx, fy) || !isok(tx, ty) || (!cansee(fx, fy) && !cansee(tx, ty)))
        return;
    if (!on_level(&vfall_lev, &u.uz)) {
        vfall_lev = u.uz;
        vdth_list.clear(), vbr_list.clear();
    }
    vtw_proj(fx - tx, fy - ty, &sx, &sy);
    for (k = 0; k < vbr_list.size(); k++) /* one roll a square */
        if (vbr_list[k].x != tx || vbr_list[k].y != ty)
            vbr_list[n++] = vbr_list[k];
    vbr_list.resize(n);
    if (n >= 8)
        return;
    r.x = tx, r.y = ty, r.ox = (float) sx, r.oy = (float) sy, r.t0 = SDL_GetTicks(), r.dur = 280;
    vbr_list.push_back(r);
    vsp_live_t = r.t0;
}

/* the overlay pass: falls and rolls that have run their course go */
static void
vfall_tick(Uint32 now)
{
    size_t k, n = 0;

    if (!on_level(&vfall_lev, &u.uz)) {
        vfall_lev = u.uz;
        vdth_list.clear(), vbr_list.clear();
    }
    for (k = 0; k < vdth_list.size(); k++)
        if (vfall_freeze >= 0.0 || now - vdth_list[k].t0 < vdth_list[k].dur)
            vdth_list[n++] = vdth_list[k];
    vdth_list.resize(n);
    n = 0;
    for (k = 0; k < vbr_list.size(); k++)
        if (vfall_freeze >= 0.0 || now - vbr_list[k].t0 < vbr_list[k].dur)
            vbr_list[n++] = vbr_list[k];
    vbr_list.resize(n);
    if (!vdth_list.empty() || !vbr_list.empty())
        vsp_live_t = now; /* frames keep coming */
}

/* probe: s204 - VULTURE_SELFTEST_FALL: jackals killed beside you from two
   sides (real blows), then boulders pushed two ways (the core's moverock),
   frames shot as each plays */
static Uint32 vfall_probe_t = 0;
static int vfall_probe_n = 0, vfall_probe_sub = 0, vfall_probe_shots = 0;

static void
vfall_probe_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];
    SDL_Surface *o;
    SDL_Rect a;
    int hx, hy;

    if (!dir || vfall_probe_shots >= 40 || !vulture_screen || vulture_screen->w < 960 || vulture_screen->h < 540)
        return;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vsp_screen(u.ux, u.uy, &hx, &hy);
    a.w = 480, a.h = 320, a.x = hx - 240, a.y = hy - 190;
    a.x = a.x < 0 ? 0 : a.x + a.w > vulture_screen->w ? vulture_screen->w - a.w : a.x;
    a.y = a.y < 0 ? 0 : a.y + a.h > vulture_screen->h ? vulture_screen->h - a.h : a.y;
    if (!(o = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return;
    snprintf(p, sizeof p, "%s\\fall%02d_%s_%02d.bmp", dir, vfall_probe_n, tag, vfall_probe_shots++);
    if (vulture_frame_to_surface(o, &a))
        SDL_SaveBMP(o, p);
    SDL_FreeSurface(o);
}

static void vprobe_push_key(SDL_Keycode k); /* probe: s145, further down */

static void
vfall_probe_tick(Uint32 now)
{
    static const int ddx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const double hold[5] = { 0.1, 0.3, 0.5, 0.7, 0.9 };
    static int last = -1;
    int step = vfall_probe_n, k;

    if (vfall_probe_sub > 0) { /* held at set points once the blow or the step has played */
        int s = vfall_probe_sub - 1;

        if (now - vfall_probe_t < 450)
            return;
        if (s == 0)
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe: %d falling, %d rolling, you at %d,%d\n",
                              (int) vdth_list.size(), (int) vbr_list.size(), u.ux, u.uy);
        vfall_freeze = hold[s];
        vfall_probe_shot(step < 2 ? "fall" : "roll");
        if (++vfall_probe_sub > 5)
            vfall_freeze = -1.0, vfall_probe_sub = 0, vfall_probe_n++, vfall_probe_t = now;
        return;
    }
    if (now - vfall_probe_t < 900)
        return;
    u.uhp = u.uhpmax;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe %d\n", step);
    switch (step) {
    case 0:
    case 1: { /* a jackal killed: first to the screen's right (map NE), then its left (SW) */
        struct monst *m = NULL;

        for (k = 0; k < 8 && !m; k++) {
            int d = ((step == 0 ? 7 : 3) + k) % 8, x = u.ux + ddx[d], y = u.uy + ddy[d];

            if (!isok(x, y))
                continue;
            if ((m = m_at(x, y)) != 0 && (m->mtame || m->mpeaceful))
                m = NULL;
            else if (!m && ACCESSIBLE(levl[x][y].typ))
                m = makemon(&mons[PM_JACKAL], x, y, NO_MM_FLAGS);
        }
        if (!m) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe: no jackal\n");
            vfall_probe_n++, vfall_probe_t = now;
            return;
        }
        m->mhpmax = 60, m->mhp = 1;
        u.uhitinc = 20;     /* the probe's blow lands */
        vfall_freeze = 0.0; /* held from the start; the shots move it on */
        (void) do_attack(m);
        break;
    }
    case 2:
    case 3: { /* a boulder the most open way (then another way); you walk into it */
        int best = -1;

        for (k = 0; k < 8; k++) {
            int x1 = u.ux + ddx[k], y1 = u.uy + ddy[k], x2 = u.ux + 2 * ddx[k], y2 = u.uy + 2 * ddy[k];

            if (k == last || !isok(x2, y2) || !ACCESSIBLE(levl[x1][y1].typ) || !ACCESSIBLE(levl[x2][y2].typ)
                || m_at(x1, y1) || m_at(x2, y2) || OBJ_AT(x1, y1) || OBJ_AT(x2, y2) || t_at(x1, y1)
                || t_at(x2, y2) || IS_DOOR(levl[x1][y1].typ) || IS_DOOR(levl[x2][y2].typ))
                continue;
            best = k;
            if (ddx[k] && ddy[k] == 0) /* across the screen first */
                break;
        }
        if (best < 0) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe: nowhere to push\n");
            vfall_probe_n++, vfall_probe_t = now;
            return;
        }
        last = best;
        (void) mksobj_at(BOULDER, u.ux + ddx[best], u.uy + ddy[best], FALSE, FALSE);
        newsym(u.ux + ddx[best], u.uy + ddy[best]);
        flush_screen(1);
        vfall_freeze = 0.1; /* held from the start; the shots move it on */
        { /* your step - Vulture moves by the screen, so the key for that way */
            int sx, sy;

            vtw_proj(ddx[best], ddy[best], &sx, &sy);
            vprobe_push_key(sx > 0 ? (sy > 0 ? SDLK_n : sy < 0 ? SDLK_u : SDLK_l)
                            : sx < 0 ? (sy > 0 ? SDLK_b : sy < 0 ? SDLK_y : SDLK_h)
                                     : (sy > 0 ? SDLK_j : SDLK_k));
        }
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe: walking %d,%d into a boulder\n",
                          ddx[best], ddy[best]);
        vfall_probe_sub = 1, vfall_probe_t = SDL_GetTicks();
        return;
    }
    default:
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe: done, %d shots\n", vfall_probe_shots);
        vfall_probe_t = 0;
        return;
    }
    flush_screen(1);
    vulture_tween_run(); /* the queued death plays, as the next frame would */
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "fall probe: %d falling\n", (int) vdth_list.size());
    vfall_probe_sub = 1, vfall_probe_t = SDL_GetTicks();
}

/*
 * IMPLEMENTATION_LOG s205: the dungeon as solid blocks.
 *  - Contact shadow (ambient occlusion): a floor square darkens softly along
 *    the edges where a wall or rock stands behind it, deepest in corners, so
 *    walls stand ON the floor instead of floating over it.
 *  - Slab faces: where a floor square's front edge (toward you) meets rock
 *    or the void, the cut face of the slab hangs below it - rock with a
 *    lit rim and strata, lit from the upper left (the left-facing face
 *    brighter), fading into the dark below - so rooms and corridors read as
 *    solid blocks rather than sheets floating in black.  Drawn after the
 *    whole floor pass (the void's black tiles would paint over them).
 */
#define VSLAB_D 28 /* a slab face's depth, px */
struct vslab_t {
    int x, y;            /* the square's centre on screen */
    Uint8 side, shade;   /* side 0: its lower-left edge, 1: lower-right */
};
static std::vector<vslab_t> vslab_list;
static SDL_Surface *vao_mask[2] = { NULL, NULL }; /* the contact shadow along the upper-left / upper-right edge */
static SDL_Surface *vslab_tex = NULL;

static bool
vao_floor(int b) /* a floor of some kind - not rock, a wall, or the unknown */
{
    return b >= V_TILE_FLOOR_COBBLESTONE && b <= V_TILE_FLOOR_DARK;
}

static SDL_Surface *
vao_make_mask(int side)
{
    int w = 110, h = 44, x, y;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);

    if (!s)
        return NULL;
    SDL_LockSurface(s);
    for (y = 0; y < h; y++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + y * s->pitch);

        for (x = 0; x < w; x++) {
            double u = (x + 0.5 - w / 2.0) / (w / 2.0), v = (y + 0.5 - h / 2.0) / (h / 2.0);
            double d = side == 0 ? 1.0 + u + v : 1.0 - u + v, a = 0.0; /* 0 at that edge, 1 at the middle */

            if (fabs(u) + fabs(v) <= 1.0 && d < 0.5)
                a = pow(1.0 - d / 0.5, 1.7) * 150.0;
            row[x] = (Uint32) (a + 0.5) << 24; /* black */
        }
    }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    return s;
}

static SDL_Surface *
vslab_make_tex(void) /* rock seen in section: a lit rim, grain, strata, into the dark */
{
    int w = 64, h = VSLAB_D, x, y;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);

    if (!s)
        return NULL;
    SDL_LockSurface(s);
    for (y = 0; y < h; y++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + y * s->pitch);
        double fy = (double) y / (h - 1), a = 255.0 * pow(1.0 - fy, 1.2);

        for (x = 0; x < w; x++) {
            double n = 0.5 * vsp_hash(x, y, 7) + 0.5 * vsp_hash(x / 4, y / 3, 11);
            double strata = 0.84 + 0.16 * sin(y * 1.3 + 1.5 * sin(x * 0.21));
            double c = (0.55 + 0.45 * n) * strata * (y < 2 ? 1.4 : 1.0);
            int r = (int) (98 * c), g = (int) (88 * c), b = (int) (78 * c);

            r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
            row[x] = (Uint32) (a + 0.5) << 24 | (Uint32) r << 16 | (Uint32) g << 8 | (Uint32) b;
        }
    }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    return s;
}

/* the floor pass: square (j, i), drawn at (x, y), is floor - shade the foot
   of what stands behind it and queue the faces of the slab in front */
static void
vao_square(mapdata *md, int j, int i, int x, int y, int dark, bool lit)
{
    /* screen up-left, up-right, down-left, down-right: map W, N, S, E at no turn */
    static const int nb[4][2] = { { -1, 0 }, { 0, -1 }, { 0, 1 }, { 1, 0 } };
    int k, ax, ay;

    if (!vao_floor(md->get_glyph(MAP_BACK, j, i)))
        return;
    if (!vao_mask[0])
        vao_mask[0] = vao_make_mask(0), vao_mask[1] = vao_make_mask(1);
    for (k = 0; k < 4; k++) {
        int nj, ni;

        v_rot(nb[k][0], nb[k][1], vulture_view_rot, &ax, &ay);
        nj = j + ax, ni = i + ay;
        if (nj >= 1 && nj < COLNO && ni >= 0 && ni < ROWNO && vao_floor(md->get_glyph(MAP_BACK, nj, ni)))
            continue;
        if (k < 2) { /* behind: the foot of the wall darkens the floor */
            if (vao_mask[k])
                vulture_put_img(x - 55, y - 21, vao_mask[k]);
        } else if (vslab_list.size() < 1500) { /* in front: the slab's cut face */
            vslab_t f;

            f.x = x, f.y = y, f.side = (Uint8) (k - 2);
            f.shade = (Uint8) (lit || dark <= 0 ? 255 : dark == 1 ? 140 : 80);
            vslab_list.push_back(f);
        }
    }
}

/* after the floor pass: the slab faces queued, hanging from their edges */
static void
vslab_draw(void)
{
    size_t k;
    SDL_Rect r;

    if (vslab_list.empty())
        return;
    if (!vslab_tex && !(vslab_tex = vslab_make_tex())) {
        vslab_list.clear();
        return;
    }
    r.x = 0, r.y = 0, r.w = vslab_tex->w, r.h = vslab_tex->h;
    for (k = 0; k < vslab_list.size(); k++) {
        const vslab_t &f = vslab_list[k];
        int sh = f.side == 0 ? f.shade : f.shade * 7 / 10; /* lit from the upper left */
        /* on the square grid's own diamond (the floor art is 109 px across a
           112 px step), so neighbouring faces meet without a seam */
        float x0 = (float) (f.side == 0 ? f.x - V_MAP_XMOD : f.x), y0 = (float) (f.side == 0 ? f.y + 1 : f.y + 1 + V_MAP_YMOD);
        float x1 = (float) (f.side == 0 ? f.x : f.x + V_MAP_XMOD), y1 = (float) (f.side == 0 ? f.y + 1 + V_MAP_YMOD : f.y + 1);
        float q[8];

        q[0] = x0, q[1] = y0, q[2] = x1, q[3] = y1;
        q[4] = x1, q[5] = y1 + VSLAB_D, q[6] = x0, q[7] = y0 + VSLAB_D;
        vdr_quad_rgb(vslab_tex, r, q, sh, sh, sh, 255);
    }
    vslab_list.clear();
}

/*
 * IMPLEMENTATION_LOG s206: a living dungeon.
 *  - Creatures turn in depth: a change of facing narrows the sprite to an
 *    edge and opens it out the other way (~150 ms) - a turn about its own
 *    upright axis - instead of a mirror flip.
 *  - Awake hostiles in view turn to face you and keep watching; pets and
 *    peaceful folk glance about now and then.  Every few seconds an awake
 *    creature standing still does something small - a little one hops (its
 *    shadow staying on the floor), others shift their weight or stretch -
 *    each on its own clock.  Sleepers keep still.  You too, standing idle.
 *  - Trees sway in a breeze that rises and dies down, each in its own time,
 *    and now and then drop a leaf; fountains glitter and drip into their
 *    basins; a sink's tap drips; incense curls up from altars; a low mist
 *    creeps about graves; thrones catch the light.
 */
struct vlife_t {
    signed char face, from, glance; /* the way it faces; before its turn; a glance's way */
    Uint8 act;                      /* a fidget: 0 none, 1 hop, 2 weight shift, 3 stretch */
    Uint32 turn_t0, next, act_t0, glance_until, seen;
};
static std::map<unsigned, vlife_t> vlife; /* by monster id; 0xFFFFFFFF is you */
static Uint32 vlife_prune_t = 0;

static Uint32
vlife_gap(bool hero) /* how long until the next fidget or glance */
{
    static int busy = -1; /* probe: VULTURE_LIFE_BUSY makes it often */

    if (busy < 0)
        busy = getenv("VULTURE_LIFE_BUSY") ? 1 : 0;
    if (busy)
        return 300 + rand() % 500;
    return hero ? 6000 + rand() % 7000 : 2500 + rand() % 5500;
}

/* step 5 of the map: the creature on (j, i), drawn as `tile` facing `face`
   (its walk's) at screen x `sx`, its pose so far in xs, ys, deg and the y it
   stands at - its life adds to the pose; the face to draw comes back */
static int
vlife_apply(int j, int i, int tile, int face, bool moving, int sx, double *xs, double *ys, double *deg,
            int *y)
{
    Uint32 now = SDL_GetTicks();
    vulture_tile *vt = vulture_get_tile(tile);
    struct monst *m = NULL;
    std::map<unsigned, vlife_t>::iterator it;
    bool hero = u_at(j, i), asleep, hostile, small;
    unsigned key;
    int art, eff, want, hx, hy;

    if (!vfx_ambient || !vt || !vt->graphic)
        return face;
    if (hero) {
        key = 0xFFFFFFFFu;
        asleep = u.usleep || gm.multi < 0;
        hostile = false;
        small = verysmall(gy.youmonst.data) != 0;
    } else {
        if (!(m = m_at(j, i)) || DEADMONSTER(m) || !canspotmon(m))
            return face;
        key = m->m_id;
        asleep = m->msleeping || !m->mcanmove;
        hostile = !m->mtame && !m->mpeaceful;
        small = m->data->msize <= MZ_SMALL && !is_flyer(m->data) && !is_floater(m->data);
    }
    art = vtw_art_facing(tile, vt->graphic);
    eff = face ? face : (art ? art : 1); /* the way it faces on screen */
    if ((it = vlife.find(key)) == vlife.end()) {
        vlife_t r;

        r.face = r.from = (signed char) eff, r.glance = 0, r.act = 0;
        r.turn_t0 = r.act_t0 = r.glance_until = 0, r.next = now + vlife_gap(hero), r.seen = now;
        it = vlife.insert(std::make_pair(key, r)).first;
    }
    vlife_t &r = it->second;
    r.seen = now;
    want = eff;
    if (asleep) { /* still */
        r.glance = 0, r.act = 0;
        want = r.face;
    } else if (moving) { /* the way it walks */
        r.glance = 0, r.act = 0;
    } else {
        if (hostile) { /* it watches you */
            vsp_screen(u.ux, u.uy, &hx, &hy);
            want = hx > sx + 4 ? 1 : hx < sx - 4 ? -1 : r.face;
        } else if (r.glance && now < r.glance_until) {
            want = r.glance;
        } else {
            r.glance = 0;
        }
        if (now >= r.next && !r.act) { /* something small */
            r.next = now + vlife_gap(hero);
            if (!hostile && rand() % 100 < 40) { /* a glance: the other way, or a pet at you */
                r.glance = (signed char) -eff;
                if (m && m->mtame && rand() % 2) {
                    vsp_screen(u.ux, u.uy, &hx, &hy);
                    r.glance = (signed char) (hx >= sx ? 1 : -1);
                }
                r.glance_until = now + 900 + rand() % 1400;
            } else {
                r.act = (Uint8) (small && rand() % 3 ? 1 : 2 + rand() % 2);
                r.act_t0 = now;
            }
        }
    }
    if (want != r.face) /* it turns */
        r.from = r.face, r.face = (signed char) want, r.turn_t0 = now;
    eff = r.face;
    if (r.turn_t0) {
        double u = (now - r.turn_t0) / 150.0;

        if (u >= 1.0) {
            r.turn_t0 = 0;
        } else { /* narrowed to an edge, opened out the other way */
            double c = fabs(cos(3.14159265 * u));

            *xs *= c < 0.06 ? 0.06 : c;
            eff = u < 0.5 ? r.from : r.face;
            vsp_live_t = now;
        }
    }
    if (r.act) {
        double dur = r.act == 1 ? 340.0 : r.act == 2 ? 760.0 : 640.0, u = (now - r.act_t0) / dur;

        if (u >= 1.0) {
            r.act = 0;
        } else {
            double s = sin(3.14159265 * u);

            if (r.act == 1) { /* a hop - up, down, a squash as it lands */
                *y -= (int) (8.0 * s);
                if (u > 0.82) {
                    double k = sin(3.14159265 * (u - 0.82) / 0.18);

                    *ys *= 1.0 - 0.08 * k, *xs *= 1.0 + 0.06 * k;
                }
            } else if (r.act == 2) { /* its weight shifted one way and back */
                *deg += 4.5 * sin(6.2831853 * u);
            } else { /* a stretch */
                *ys *= 1.0 + 0.06 * s, *xs *= 1.0 - 0.03 * s;
            }
            vsp_live_t = now;
        }
    }
    return eff;
}

static void
vlife_tick(Uint32 now) /* the overlay pass: forget creatures long out of sight */
{
    std::map<unsigned, vlife_t>::iterator it;

    if (now - vlife_prune_t < 2000)
        return;
    vlife_prune_t = now;
    for (it = vlife.begin(); it != vlife.end();)
        if (now - it->second.seen > 10000)
            vlife.erase(it++);
        else
            ++it;
}

/* step 2 of the map: a tree in the breeze - its crown sways, its foot stays;
   false: not a tree (or the atmosphere is off) */
static bool
vtree_draw(int x, int y, int j, int i, int furn)
{
    vulture_tile *t;
    double tm, ph, gust, sw;
    float q[8];
    SDL_Rect r;

    if (furn != V_MISC_TREE || !vfx_ambient || !(t = vulture_get_tile(furn)) || !t->graphic)
        return false;
    tm = SDL_GetTicks() / 1000.0, ph = vsp_hash(j, i, 5) * 6.2832;
    gust = 0.55 + 0.45 * sin(tm * 0.31 + ph * 0.7); /* the wind rises and dies down */
    sw = gust * (3.4 * sin(tm * 1.15 + ph) + 1.4 * sin(tm * 2.9 + ph * 1.7));
    r.x = 0, r.y = 0, r.w = t->graphic->w, r.h = t->graphic->h;
    q[0] = (float) (x + t->xmod + sw), q[1] = (float) (y + t->ymod);
    q[2] = (float) (x + t->xmod + t->graphic->w + sw), q[3] = q[1];
    q[4] = (float) (x + t->xmod + t->graphic->w), q[5] = (float) (y + t->ymod + t->graphic->h);
    q[6] = (float) (x + t->xmod), q[7] = q[5];
    vdr_quad_rgb(t->graphic, r, q, 255, 255, 255, 255);
    vdr_quad_normal(vulture_get_tile_normal(furn, false), r, q, 255);
    return true;
}

/* the overlay pass: the furniture in sight alive - formulas of the clock and
   the square, nothing kept */
static void
vlive_furniture(mapdata *md, double t)
{
    int x, y, k;

    if (!vfx_ambient || !md || u.uswallow)
        return;
    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++) {
            int f, sx, sy;
            double ph;

            if (!cansee(x, y))
                continue;
            f = md->get_glyph(MAP_FURNITURE, x, y);
            if (f != V_MISC_FOUNTAIN && f != V_MISC_SINK && f != V_MISC_ALTAR && f != V_MISC_GRAVE
                && f != V_MISC_THRONE && f != V_MISC_TREE)
                continue;
            vsp_screen(x, y, &sx, &sy);
            ph = vsp_hash(x, y, 3) * 100.0;
            if (f == V_MISC_FOUNTAIN) {
                /* the plume shimmers as the water moves through it, and glitters */
                vsp_blob(sx + (int) (3.0 * sin(t * 1.7 + ph)), sy - 56, 24, 110, 160, 255,
                         (int) (30 + 16 * sin(t * 4.3 + ph)), true, 0.7);
                for (k = 0; k < 14; k++) {
                    double s = sin(t * 6.0 + k * 1.7 + ph), a = s > 0.0 ? pow(s, 3.0) : 0.0;

                    if (a > 0.05)
                        vsp_blob(sx + (int) ((vsp_hash(x * 7 + k, y, 1) - 0.5) * 56.0),
                                 sy - 66 + (int) (vsp_hash(x, y * 7 + k, 2) * 24.0), 1.8, 210, 235, 255,
                                 (int) (235 * a), true);
                }
                for (k = 0; k < 6; k++) { /* drops falling into the basin */
                    double u = fmod(t / 0.9 + k / 6.0 + ph, 1.0), side = k % 2 ? 1.0 : -1.0;
                    int px = sx + (int) (side * (20 + 12 * vsp_hash(x, y, k))), py = sy - 48 + (int) (30 * u * u);

                    vsp_blob(px, py, 1.6, 175, 215, 255, (int) (230 * (1.0 - u)), true);
                }
                vsp_blob(sx, sy - 16, 22, 120, 170, 255, (int) (18 + 14 * sin(t * 3.0 + ph)), true, 0.35);
            } else if (f == V_MISC_SINK) { /* the tap drips */
                double u = fmod(t / 1.7 + ph, 1.0);

                if (u < 0.5)
                    vsp_blob(sx + 2, sy - 40, 0.6 + 1.6 * u, 180, 215, 255, 200, true);
                else if (u < 0.62)
                    vsp_blob(sx + 2, sy - 40 + (int) (12 * (u - 0.5) / 0.12), 1.4, 180, 215, 255, 220, true);
                else if (u < 0.85) {
                    double w = (u - 0.62) / 0.23;

                    for (k = 0; k < 6; k++)
                        vsp_blob(sx + 2 + (int) (cos(k * 1.047) * 7 * w), sy - 28 + (int) (sin(k * 1.047) * 2.5 * w),
                                 1.0, 170, 205, 255, (int) (150 * (1.0 - w)), true);
                }
            } else if (f == V_MISC_ALTAR) { /* incense curls up */
                for (k = 0; k < 5; k++) {
                    double u = fmod(t / 3.4 + k * 0.2 + ph, 1.0);
                    int px = sx + (int) (sin(t * 1.3 + k * 2.1) * 9.0 * u + (vsp_hash(x, y, k) - 0.5) * 10.0);

                    vsp_blob(px, sy - 40 - (int) (u * 70.0), 3.0 + 8.0 * u, 175, 185, 205,
                             (int) (80 * sin(3.14159265 * u)), false);
                }
            } else if (f == V_MISC_GRAVE) { /* a low mist creeps about it */
                for (k = 0; k < 3; k++) {
                    double u = fmod(t / 5.0 + k / 3.0 + ph, 1.0);

                    vsp_blob(sx - 32 + (int) (u * 64.0), sy - 4 + (int) (sin(t + k) * 3.0), 13.0, 140, 155, 190,
                             (int) (62 * sin(3.14159265 * u)), false, 0.5);
                }
            } else if (f == V_MISC_THRONE) { /* the gold studs catch the light */
                for (k = 0; k < 2; k++) {
                    double s = sin(t * 1.6 + k * 2.3 + ph), a = s > 0.0 ? pow(s, 14.0) : 0.0;

                    if (a > 0.05)
                        vsp_blob(sx + (k ? 22 : -18), sy - (k ? 66 : 60), 2.0, 255, 225, 140, (int) (230 * a), true);
                }
            } else { /* a tree drops a leaf now and then */
                double c = t / 2.6 + ph, u = fmod(c, 1.0);
                int leaf = (int) floor(c), lx = sx + (int) ((vsp_hash(x, y, leaf) - 0.5) * 40.0);

                if (u < 0.7)
                    vsp_blob(lx + (int) (8.0 * sin(u * 12.0 + ph)), sy - 60 + (int) (64.0 * u / 0.7), 1.4, 70, 140,
                             60, 220, false);
                else
                    vsp_blob(lx + (int) (8.0 * sin(8.4 + ph)), sy + 4, 1.4, 70, 140, 60,
                             (int) (220 * (1.0 - (u - 0.7) / 0.3)), false);
            }
        }
}

/* probe: s206 - VULTURE_SELFTEST_LIFE: furniture set round you and three
   creatures (a hostile jackal, a peaceful gnome, a newt); frames as they
   live; then you stand on the jackal's far side and it turns to watch */
static Uint32 vlife_probe_t = 0;
static int vlife_probe_n = 0, vlife_probe_sub = 0, vlife_probe_shots = 0, vlife_probe_jx = 0, vlife_probe_jy = 0;

static void
vlife_probe_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];
    SDL_Surface *o;
    SDL_Rect a;
    int hx, hy;

    if (!dir || vlife_probe_shots >= 40 || !vulture_screen || vulture_screen->w < 960 || vulture_screen->h < 540)
        return;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vsp_screen(u.ux, u.uy, &hx, &hy);
    a.w = 640, a.h = 400, a.x = hx - 320, a.y = hy - 230;
    a.x = a.x < 0 ? 0 : a.x + a.w > vulture_screen->w ? vulture_screen->w - a.w : a.x;
    a.y = a.y < 0 ? 0 : a.y + a.h > vulture_screen->h ? vulture_screen->h - a.h : a.y;
    if (!(o = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return;
    snprintf(p, sizeof p, "%s\\life%02d_%s_%02d.bmp", dir, vlife_probe_n, tag, vlife_probe_shots++);
    if (vulture_frame_to_surface(o, &a))
        SDL_SaveBMP(o, p);
    SDL_FreeSurface(o);
}

static struct monst *
vlife_probe_mon(int pm, int dx, int dy, bool peaceful)
{
    int x = u.ux + dx, y = u.uy + dy;
    struct monst *m;

    if (!isok(x, y) || !ACCESSIBLE(levl[x][y].typ) || m_at(x, y))
        return NULL;
    if ((m = makemon(&mons[pm], x, y, NO_MM_FLAGS)) != 0) {
        m->mpeaceful = peaceful ? 1 : 0;
        m->msleeping = 0, m->mcanmove = 1;
    }
    return m;
}

static void
vlife_probe_tick(Uint32 now)
{
    static const int fx[6] = { 2, 2, -2, -2, 0, 0 }, fy[6] = { 0, 2, 2, 0, 2, -2 };
    static const schar ft[6] = { FOUNTAIN, ALTAR, SINK, GRAVE, THRONE, TREE };
    static const int at[6] = { 30, 60, 90, 120, 150, 400 };
    int k;

    if (vlife_probe_sub > 0) {
        int s = vlife_probe_sub - 1;

        if (vlife_probe_n == 1) { /* living: a burst, a frame every 70 ms */
            if (now - vlife_probe_t < (Uint32) (300 + 70 * s))
                return;
            vlife_probe_shot("live");
            if (++vlife_probe_sub > 12)
                vlife_probe_sub = 0, vlife_probe_n++, vlife_probe_t = now;
        } else { /* the turn */
            if (now - vlife_probe_t < (Uint32) at[s])
                return;
            vlife_probe_shot("turn");
            if (++vlife_probe_sub > 6)
                vlife_probe_sub = 0, vlife_probe_n++, vlife_probe_t = now;
        }
        return;
    }
    if (now - vlife_probe_t < 700)
        return;
    u.uhp = u.uhpmax;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "life probe %d\n", vlife_probe_n);
    switch (vlife_probe_n) {
    case 0: { /* furniture round you; three creatures */
        struct monst *j;

        for (k = 0; k < 6; k++) { /* the nearest free room floor 2-3 squares off */
            int x, y, bx = 0, by = 0, bd = 99;

            for (y = u.uy - 3; y <= u.uy + 3; y++)
                for (x = u.ux - 3; x <= u.ux + 3; x++) {
                    int d = distmin(u.ux, u.uy, x, y) * 10 + abs(x - u.ux - fx[k]) + abs(y - u.uy - fy[k]);

                    if (isok(x, y) && distmin(u.ux, u.uy, x, y) >= 2 && levl[x][y].typ == ROOM && !m_at(x, y)
                        && !OBJ_AT(x, y) && d < bd)
                        bd = d, bx = x, by = y;
                }
            if (!bx)
                continue;
            levl[bx][by].typ = ft[k];
            if (ft[k] == ALTAR)
                levl[bx][by].altarmask = AM_NEUTRAL;
            newsym(bx, by);
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "life probe: furniture %d at %d,%d\n", (int) ft[k], bx, by);
        }
        if ((j = vlife_probe_mon(PM_JACKAL, 1, -1, false)) != 0 || (j = vlife_probe_mon(PM_JACKAL, 1, 1, false)) != 0)
            vlife_probe_jx = j->mx, vlife_probe_jy = j->my;
        (void) vlife_probe_mon(PM_GNOME, -1, -1, true);
        (void) vlife_probe_mon(PM_NEWT, -1, 1, false);
        vlife_probe_n = 1;
        break;
    }
    case 2: { /* you, on the jackal's other side (on screen) */
        int tx = 0, ty = 0, x, y, jsx, jsy, hsx, hsy;

        vsp_screen(vlife_probe_jx, vlife_probe_jy, &jsx, &jsy);
        vsp_screen(u.ux, u.uy, &hsx, &hsy);
        for (y = vlife_probe_jy - 2; y <= vlife_probe_jy + 2 && vlife_probe_jx && !tx; y++)
            for (x = vlife_probe_jx - 2; x <= vlife_probe_jx + 2 && !tx; x++) {
                int sx2, sy2;

                if (!isok(x, y) || !ACCESSIBLE(levl[x][y].typ) || m_at(x, y) || IS_FURNITURE(levl[x][y].typ)
                    || levl[x][y].typ == TREE || (x == u.ux && y == u.uy))
                    continue;
                vsp_screen(x, y, &sx2, &sy2);
                if ((sx2 - jsx) * (hsx - jsx) < 0 && abs(sx2 - jsx) >= 40)
                    tx = x, ty = y;
            }
        if (!tx) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "life probe: no way round the jackal\n");
            vlife_probe_n = 3, vlife_probe_t = now;
            return;
        }
        teleds(tx, ty, TELEDS_NO_FLAGS);
        break;
    }
    default:
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "life probe: done, %d shots, %d lives\n",
                          vlife_probe_shots, (int) vlife.size());
        vlife_probe_t = 0;
        return;
    }
    flush_screen(1);
    vlife_probe_sub = 1, vlife_probe_t = SDL_GetTicks();
}

/*
 * IMPLEMENTATION_LOG s207 (lids): opening a chest, a large box or an ice
 * box on the floor lifts its lid on its back hinge in the room's
 * perspective - the lid cut from the body along its top face, turned about
 * the hinge (an affine quad: the lid treated as the plate of the top face,
 * its depth swinging up off the floor) - showing the dark inside.  The lift
 * plays before the loot menu covers the map; the lid stays up while you
 * rummage and swings shut with a puff of dust when you are done.
 */
struct vlid_art_t {
    SDL_Surface *lid, *body, *nlid, *nbody;
    int lx0, ly0, lx1, ly1; /* the lid's box in the art */
    bool ok, tried;
};
static const struct {
    int otyp;
    float c[8]; /* the top face in the art: front-left, front-right, back-right, back-left */
    int th;     /* the lid's own depth below the front and side top edges */
} vlid_fit[3] = {
    { CHEST, { 0, 17, 28, 28, 51, 19, 23, 8 }, 2 },
    { LARGE_BOX, { 3, 9, 38, 21, 60, 12, 25, 0 }, 3 },
    { ICE_BOX, { 3, 15, 33, 28, 70, 15, 40, 2 }, 5 },
};
static vlid_art_t vlid_art[3];
struct vlid_t {
    int x, y, tile, kind, state; /* state 1 lifting, 2 up, 3 shutting */
    Uint32 t0;
};
static vlid_t vlid_cur = { 0, 0, -1, 0, 0, 0 };
static double vlid_freeze = -1.0; /* probe: >= 0 holds the lid at this much of open */
static SDL_Surface *vlid_inside = NULL;
#define VLID_OPEN 1.25 /* radians the lid stands open */
#define VLID_VR 1.3    /* screen px up per screen-x px of floor (the lid's depth stood up) */

static int
vlid_kind(int tile) /* which of the lidded boxes the tile is; -1: none */
{
    int k;

    for (k = 0; k < 3; k++)
        if (tile == OBJECT_TO_VTILE(vlid_fit[k].otyp))
            return k;
    return -1;
}

static vlid_art_t *
vlid_split(int kind, int tile)
{
    vlid_art_t *a = &vlid_art[kind];
    const float *c = vlid_fit[kind].c;
    vulture_tile *t;
    SDL_Surface *src, *nm;
    int x, y, n = 0, th = vlid_fit[kind].th;

    if (a->tried)
        return a->ok ? a : NULL;
    a->tried = true;
    if (!(t = vulture_get_tile(tile)) || !t->graphic
        || !(src = SDL_ConvertSurfaceFormat(t->graphic, SDL_PIXELFORMAT_ARGB8888, 0)))
        return NULL;
    a->lid = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    a->body = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!a->lid || !a->body) {
        SDL_FreeSurface(src);
        if (a->lid)
            SDL_FreeSurface(a->lid);
        if (a->body)
            SDL_FreeSurface(a->body);
        a->lid = a->body = NULL;
        return NULL;
    }
    a->lx0 = src->w, a->ly0 = src->h, a->lx1 = 0, a->ly1 = 0;
    SDL_LockSurface(src);
    SDL_LockSurface(a->lid);
    SDL_LockSurface(a->body);
    for (y = 0; y < src->h; y++) {
        const Uint32 *row = (const Uint32 *) ((const Uint8 *) src->pixels + y * src->pitch);
        Uint32 *lr = (Uint32 *) ((Uint8 *) a->lid->pixels + y * a->lid->pitch);
        Uint32 *br = (Uint32 *) ((Uint8 *) a->body->pixels + y * a->body->pitch);

        for (x = 0; x < src->w; x++) {
            /* above the front top edge (left of its front-right corner) or the
               side top edge, by the lid's own depth: the lid */
            double yb = x <= c[2] ? c[1] + (x - c[0]) * (c[3] - c[1]) / (c[2] - c[0])
                                  : c[3] + (x - c[2]) * (c[5] - c[3]) / (c[4] - c[2]);
            bool in = y < yb + th;

            lr[x] = in ? row[x] : 0;
            br[x] = in ? 0 : row[x];
            if (in && (row[x] >> 24)) {
                n++;
                if (x < a->lx0)
                    a->lx0 = x;
                if (x + 1 > a->lx1)
                    a->lx1 = x + 1;
                if (y < a->ly0)
                    a->ly0 = y;
                if (y + 1 > a->ly1)
                    a->ly1 = y + 1;
            }
        }
    }
    SDL_UnlockSurface(a->body);
    SDL_UnlockSurface(a->lid);
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    SDL_SetSurfaceBlendMode(a->lid, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceBlendMode(a->body, SDL_BLENDMODE_BLEND);
    if (n < 60) {
        SDL_FreeSurface(a->lid);
        SDL_FreeSurface(a->body);
        a->lid = a->body = NULL;
        return NULL;
    }
    nm = vulture_get_tile_normal(tile, false); /* the normals cut the same way */
    if (nm && nm->w == a->lid->w && nm->h == a->lid->h
        && (a->nlid = SDL_ConvertSurfaceFormat(nm, SDL_PIXELFORMAT_ARGB8888, 0)) != NULL
        && (a->nbody = SDL_ConvertSurfaceFormat(nm, SDL_PIXELFORMAT_ARGB8888, 0)) != NULL) {
        SDL_LockSurface(a->nlid);
        SDL_LockSurface(a->nbody);
        SDL_LockSurface(a->lid);
        for (y = 0; y < nm->h; y++) {
            Uint32 *nl = (Uint32 *) ((Uint8 *) a->nlid->pixels + y * a->nlid->pitch);
            Uint32 *nb = (Uint32 *) ((Uint8 *) a->nbody->pixels + y * a->nbody->pitch);
            const Uint32 *lr = (const Uint32 *) ((const Uint8 *) a->lid->pixels + y * a->lid->pitch);

            for (x = 0; x < nm->w; x++)
                if (lr[x] >> 24)
                    nb[x] = 0;
                else
                    nl[x] = 0;
        }
        SDL_UnlockSurface(a->lid);
        SDL_UnlockSurface(a->nbody);
        SDL_UnlockSurface(a->nlid);
        SDL_SetSurfaceBlendMode(a->nlid, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceBlendMode(a->nbody, SDL_BLENDMODE_BLEND);
    } else if (a->nlid) {
        SDL_FreeSurface(a->nlid);
        a->nlid = NULL;
    }
    vulture_gpu_hd_derive_cut(a->lid, t->graphic, a->lid, false); /* zoomed: the HD art, cut the same way */
    vulture_gpu_hd_derive_cut(a->body, t->graphic, a->lid, true);
    a->ok = true;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "lid art %d: lid %d px, box x %d-%d y %d-%d\n", kind, n,
                      a->lx0, a->lx1, a->ly0, a->ly1);
    return a;
}

static SDL_Surface *
vlid_make_inside(void) /* the dark of a box's inside: the back wall caught by the light */
{
    int w = 16, h = 32, x, y;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);

    if (!s)
        return NULL;
    SDL_LockSurface(s);
    for (y = 0; y < h; y++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + y * s->pitch);
        double f = (double) y / (h - 1);
        Uint32 r = (Uint32) (56 - 44 * f), g = (Uint32) (40 - 32 * f), b = (Uint32) (28 - 22 * f);

        for (x = 0; x < w; x++)
            row[x] = 0xFF000000u | r << 16 | g << 8 | b;
    }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    return s;
}

static double
vlid_angle(Uint32 now) /* how far up the lid is (radians); < 0: it has shut */
{
    double t;

    if (vlid_freeze >= 0.0)
        return VLID_OPEN * vlid_freeze;
    if (vlid_cur.state == 2)
        return VLID_OPEN;
    if (vlid_cur.state == 1) { /* up, a touch past, back */
        double u;

        t = (now - vlid_cur.t0) / 360.0;
        if (t >= 1.0)
            return VLID_OPEN;
        u = t - 1.0;
        return VLID_OPEN * (1.0 + 2.2 * u * u * u + 1.2 * u * u);
    }
    if (vlid_cur.state == 3) { /* down under its weight, a small bounce */
        t = (now - vlid_cur.t0) / 280.0;
        if (t >= 1.0)
            return -1.0;
        return t < 0.85 ? VLID_OPEN * (1.0 - (t / 0.85) * (t / 0.85)) : 0.07 * sin(3.14159 * (t - 0.85) / 0.15);
    }
    return -1.0;
}

/* the object step: the box on (j, i) with its lid up (or lifting, shutting) */
static bool
vlid_draw(int j, int i, int x, int y, int tile_id)
{
    Uint32 now;
    vlid_art_t *a;
    vulture_tile *t;
    const float *c;
    double ph, ax, ay, fx, fy, gx, gy, det;
    int ix, iy, k, sh;
    float q[8];
    SDL_Rect r;

    if (!vlid_cur.state || j != vlid_cur.x || i != vlid_cur.y || tile_id != vlid_cur.tile)
        return false;
    now = SDL_GetTicks();
    if ((ph = vlid_angle(now)) < 0.0) { /* shut: a puff, and the box as it is */
        vlid_cur.state = 0;
        vulture_world_fx(j, i, 9);
        return false;
    }
    if (!(a = vlid_split(vlid_cur.kind, tile_id)) || !(t = vulture_get_tile(tile_id)) || !t->graphic)
        return false;
    vsp_live_t = now;
    c = vlid_fit[vlid_cur.kind].c;
    ix = x + t->xmod, iy = y + t->ymod;
    vdr_img(a->body, a->nbody, ix, iy, 255);
    /* the hinge (the top face's back edge) and its depth to the front */
    ax = c[4] - c[6], ay = c[5] - c[7]; /* back-left to back-right */
    fx = c[0] - c[6], fy = c[1] - c[7]; /* back-left to front-left */
    if (ph > 0.02) { /* the dark inside */
        if (!vlid_inside)
            vlid_inside = vlid_make_inside();
        if (vlid_inside) {
            r.x = 0, r.y = 0, r.w = vlid_inside->w, r.h = vlid_inside->h;
            q[0] = (float) (ix + c[6]), q[1] = (float) (iy + c[7]);
            q[2] = (float) (ix + c[4]), q[3] = (float) (iy + c[5]);
            q[4] = (float) (ix + c[4] + fx), q[5] = (float) (iy + c[5] + fy);
            q[6] = (float) (ix + c[0]), q[7] = (float) (iy + c[1]);
            vdr_quad_rgb(vlid_inside, r, q, 255, 255, 255, (int) (255 * (ph < 0.25 ? ph / 0.25 : 1.0)));
        }
    }
    /* the lid: each corner of its box as a point on the top face's plate
       (along the hinge, toward the front), its depth turned up off the floor */
    gx = cos(ph) * fx, gy = cos(ph) * fy - sin(ph) * VLID_VR * fabs(fx);
    det = ax * fy - ay * fx;
    if (fabs(det) < 1e-6)
        return true;
    for (k = 0; k < 4; k++) {
        double qx = (k == 1 || k == 2 ? a->lx1 : a->lx0) - c[6], qy = (k >= 2 ? a->ly1 : a->ly0) - c[7];
        double e = (qx * fy - qy * fx) / det, d = (ax * qy - ay * qx) / det;

        q[2 * k] = (float) (ix + c[6] + e * ax + d * gx);
        q[2 * k + 1] = (float) (iy + c[7] + e * ay + d * gy);
    }
    r.x = a->lx0, r.y = a->ly0, r.w = a->lx1 - a->lx0, r.h = a->ly1 - a->ly0;
    { /* its top turning from the light; past edge-on (where the turned depth
         lies along the hinge on screen) its underside, in shadow */
        double edge = atan2(det, ax * VLID_VR * fabs(fx));

        sh = ph < edge ? 255 - (int) (40 * ph / edge) : 170;
    }
    vdr_quad_rgb(a->lid, r, q, sh, sh, sh, 255);
    vdr_quad_normal(a->nlid, r, q, 255);
    return true;
}

/* the core (use_container): a box on (x, y) opened (open), or the one open
   done with (x, y unused) */
extern "C" void
vulture_lid(int x, int y, int open)
{
    Uint32 t0;
    int tile, kind;

    if (!open) { /* done: it swings shut */
        if (vlid_cur.state == 1 || vlid_cur.state == 2)
            vlid_cur.state = 3, vlid_cur.t0 = SDL_GetTicks(), vsp_live_t = vlid_cur.t0;
        return;
    }
    if (!levwin || !map_data || !u.ux || !isok(x, y) || !cansee(x, y) || vulture_windows_inited != TRUE
        || (kind = vlid_kind(tile = map_data->get_glyph(MAP_OBJ, x, y))) < 0 || !vlid_split(kind, tile))
        return;
    vlid_cur.x = x, vlid_cur.y = y, vlid_cur.tile = tile, vlid_cur.kind = kind;
    vlid_cur.state = 1, vlid_cur.t0 = t0 = SDL_GetTicks();
    /* the lift plays now, before the menu covers the map; a waiting key
       hurries it */
    while (!vtw_animating && !vanim_kind) {
        Uint32 now = SDL_GetTicks();

        if (now - t0 >= 380 || vanim_input_waiting())
            break;
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
        SDL_Delay(1);
    }
    vlid_cur.state = 2;
    levwin->force_redraw();
}

/* probe: s207 - VULTURE_SELFTEST_LID: a chest, a large box and an ice box
   beside you, each lid held at points of opening, then let shut (on the
   clock); VULTURE_SELFTEST_ARC: a dagger thrown, an arrow fired, a rock
   lobbed through the core's tmp_at, shot along the way */
static Uint32 vlid_probe_t = 0;
static int vlid_probe_n = 0, vlid_probe_sub = 0, vlid_probe_shots = 0, vlid_probe_x = 0, vlid_probe_y = 0;

static void
vlid_probe_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];
    SDL_Surface *o;
    SDL_Rect a;
    int hx, hy;

    if (!dir || vlid_probe_shots >= 60 || !vulture_screen || vulture_screen->w < 960 || vulture_screen->h < 540)
        return;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vsp_screen(u.ux, u.uy, &hx, &hy);
    a.w = 560, a.h = 360, a.x = hx - 280, a.y = hy - 220;
    a.x = a.x < 0 ? 0 : a.x + a.w > vulture_screen->w ? vulture_screen->w - a.w : a.x;
    a.y = a.y < 0 ? 0 : a.y + a.h > vulture_screen->h ? vulture_screen->h - a.h : a.y;
    if (!(o = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return;
    snprintf(p, sizeof p, "%s\\lid%02d_%s_%02d.bmp", dir, vlid_probe_n, tag, vlid_probe_shots++);
    if (vulture_frame_to_surface(o, &a))
        SDL_SaveBMP(o, p);
    SDL_FreeSurface(o);
}

static void
vlid_probe_tick(Uint32 now)
{
    static const int boxes[3] = { CHEST, LARGE_BOX, ICE_BOX };
    static const double hold[4] = { 0.15, 0.4, 0.7, 1.0 };
    static const int shut_at[3] = { 90, 180, 400 };
    int step = vlid_probe_n;

    if (getenv("VULTURE_SELFTEST_ARC")) { /* the flights */
        static const int ddx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        static const int what[5] = { DART, DART, ARROW, DAGGER, ROCK }; /* s222: darts two ways */
        static const char *const wn[5] = { "dart", "dart2", "arrow", "dagger", "rock" };
        static int first = -1;
        int best = 0, bl = 0, l, k, x, y;

        if (now - vlid_probe_t < 900)
            return;
        if (step >= 5) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "arc probe: done, %d shots\n", vlid_probe_shots);
            vlid_probe_t = 0;
            return;
        }
        for (k = 0; k < 8; k++) { /* the most open way (the second dart: another, a quarter round or more) */
            int off = first < 0 ? 0 : (k - first + 8) % 8;

            if (step == 1 && first >= 0 && (off <= 1 || off >= 7))
                continue;
            for (l = 0, x = u.ux + ddx[k], y = u.uy + ddy[k];
                 isok(x, y) && ACCESSIBLE(levl[x][y].typ) && !m_at(x, y) && l < 9; l++)
                x += ddx[k], y += ddy[k];
            if (l > bl)
                bl = l, best = k;
        }
        if (step == 0)
            first = best;
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "arc probe %d: %s, %d squares way %d\n", step, wn[step], bl,
                          best);
        vulture_flight_range(bl, step == 2, u.ux, u.uy);
        tmp_at(DISP_FLASH, objnum_to_glyph(what[step]));
        for (l = 1; l <= bl; l++) {
            tmp_at(u.ux + ddx[best] * l, u.uy + ddy[best] * l);
            nh_delay_output();
            vlid_probe_shot(wn[step]);
        }
        tmp_at(DISP_END, 0);
        vlid_probe_n++, vlid_probe_t = SDL_GetTicks();
        return;
    }
    if (vlid_probe_sub > 0) { /* this box's frames */
        int s = vlid_probe_sub - 1;

        if (s < 4) { /* held at points of opening */
            vlid_freeze = hold[s];
            vlid_probe_shot("open");
            if (s == 3) /* then it shuts on the clock */
                vlid_freeze = -1.0, vlid_cur.state = 2, vulture_lid(-1, -1, 0), vlid_probe_t = now;
            vlid_probe_sub++;
            return;
        }
        if (now - vlid_probe_t < (Uint32) shut_at[s - 4])
            return;
        vlid_probe_shot("shut");
        if (++vlid_probe_sub > 7)
            vlid_probe_sub = 0, vlid_probe_n++, vlid_probe_t = now;
        return;
    }
    if (now - vlid_probe_t < 700)
        return;
    if (step >= 3) {
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "lid probe: done, %d shots\n", vlid_probe_shots);
        vlid_probe_t = 0;
        return;
    }
    if (!vlid_probe_x) { /* a free floor square beside you */
        static const int ddx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        int k;

        for (k = 0; k < 16 && !vlid_probe_x; k++) { /* beside you, else a square further */
            int x = u.ux + ddx[k % 8] * (k / 8 + 1), y = u.uy + ddy[k % 8] * (k / 8 + 1);

            if (isok(x, y) && levl[x][y].typ == ROOM && !m_at(x, y) && !OBJ_AT(x, y) && cansee(x, y))
                vlid_probe_x = x, vlid_probe_y = y;
        }
        if (!vlid_probe_x) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "lid probe: no room beside you\n");
            vlid_probe_t = 0;
            return;
        }
    }
    {
        struct obj *o, *on;

        for (o = svl.level.objects[vlid_probe_x][vlid_probe_y]; o; o = on) { /* the last box goes */
            on = o->nexthere;
            obj_extract_self(o);
            obfree(o, (struct obj *) 0);
        }
    }
    (void) mksobj_at(boxes[step], vlid_probe_x, vlid_probe_y, FALSE, FALSE);
    newsym(vlid_probe_x, vlid_probe_y);
    flush_screen(1);
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "lid probe %d: box %d at %d,%d, tile %d\n", step, boxes[step],
                      vlid_probe_x, vlid_probe_y, map_data->get_glyph(MAP_OBJ, vlid_probe_x, vlid_probe_y));
    vlid_freeze = 0.0; /* no clip: held from the start */
    vlid_cur.state = 0;
    {
        int tile = map_data->get_glyph(MAP_OBJ, vlid_probe_x, vlid_probe_y), kind = vlid_kind(tile);

        if (kind >= 0 && vlid_split(kind, tile)) {
            vlid_cur.x = vlid_probe_x, vlid_cur.y = vlid_probe_y, vlid_cur.tile = tile, vlid_cur.kind = kind;
            vlid_cur.state = 2, vlid_cur.t0 = now;
        }
    }
    vlid_probe_sub = 1, vlid_probe_t = SDL_GetTicks();
}

/*
 * IMPLEMENTATION_LOG s210: what the walls hide, and doors round you.
 *  - Outlines: a creature that a wall face covers (a face drawn after it,
 *    over its sprite) shows through that face as an outline and a faint
 *    fill in its colour - red hostile, yellow peaceful, green tame, grey
 *    when you only remember or sense it - only where the face covers it,
 *    and fading where the see-through circle already shows it.  Drawn in
 *    the overlay, unlit, over the walls (vxr_draw).  vwall_draw records the
 *    faces as foot lines and heights; the monster step records each
 *    creature with its pose.
 *  - Doors: a door in a wall in front of you is see-through round you like
 *    the wall it stands in (vhole_put, which vwall_draw now shares).
 */
static bool vrock_on = true; /* s215: the solid rock mass (vrock_*) */
/* s219: how the rock is drawn - 0 stone tops, low with high behind walls
   (s218), 1 the same dark, 2 walls only (no band: the void, its dust and
   the floors' slab faces), 3 one ceiling - all rock at wall height, flush
   with the walls, 4 one ceiling, see-through where it would hide ground
   you know */
static int vrock_style = 4;

struct vxr_face_t {
    float x0, y0, k; /* its foot: y = y0 + k (X - x0), X in [x0, x0 + 56] */
    int h;           /* how high it stands above its foot */
    int seq;         /* drawn after anything with a smaller seq */
    int amin;        /* 255, or how solid its middle was in the see-through circle */
    bool cap;        /* s215: a block's top instead - a diamond round (x0, y0) */
    int cut;         /* s223: see-through all over (your room's front walls, rock over ground): its alpha */
};
struct vxr_mon_t {
    int tile, face, tx, ty, seq;
    double xs, ys, deg;
    Uint8 r, g, b;
};
static std::vector<vxr_face_t> vxr_faces;
static std::vector<vxr_mon_t> vxr_mons;
static int vxr_seq = 0;

/*
 * IMPLEMENTATION_LOG s209: walls you see whole - and through, round you;
 * the void's depth.
 *  - A room's front walls (its south / east wall squares' north / west
 *    tiles: the walls' room side seen from behind, faint black panels) are
 *    drawn as their style's outer face instead - the back-wall art (a brick
 *    face and its top) facing you, moved onto that edge: every wall solid,
 *    standing on the room's edge.
 *  - Walls in front of you are see-through round you: a wall whose foot
 *    runs in front of where you stand, over a circle about your body, is
 *    laid as a grid whose alpha falls to wall_opacity within VWALL_R0 and
 *    comes back by VWALL_R1 (vulture_gpumap_holed; its normals the same).
 *    Walls beside and behind you stay solid.
 *  - The void: two layers of dust drift deep below the rooms, moving slower
 *    than the map when the view scrolls (a parallax), drawn only where no
 *    floor, wall or slab face can be.
 */
#define VWALL_R0 56.0f
#define VWALL_R1 124.0f
#define VWALL_BODY 34 /* your body's middle, above your feet */
static bool vwall_hole_on = false;
static int vwall_hx = 0, vwall_hy = 0; /* your feet on screen this frame */
static int vwall_amin = 77;            /* how solid a wall stays at the circle's middle */
static float vl_light_at(int x, int y); /* s212: the light grid at a square (last computed) */
#define VWALL_DIM 190                   /* s212: a wall's outside facing the dark: this much black over it */
static bool vwall_probe_off = false;   /* probe: the same view with no circle */
static int vcut_now = 255; /* s223: this square see-through (the room you are in): its alpha, 255 solid */

static int
vwall_outer(int tile) /* a front wall's tile: its style's face toward you (back-wall art); -1 none */
{
    int k, o;

    for (k = 0; k < V_WALL_STYLE_MAX; k++) {
        /* s212: the outside of a decorated wall (a banner, a painting, a
           niche, a pillar) is plain brick - the decorations hang inside */
        o = k > V_WALL_BRICK && k <= V_WALL_BRICK + 4 ? V_WALL_BRICK : k;
        if (walls_full[k].north == tile)
            return walls_full[o].south;
        if (walls_full[k].west == tile)
            return walls_full[o].east;
        if (walls_half[k].north == tile)
            return walls_half[o].south;
        if (walls_half[k].west == tile)
            return walls_half[o].east;
    }
    return -1;
}

/* s212: black at alpha dim over a tile just laid at (rx0, ry0) - holed as it
   was (amin < 255), or as see-through as it was (soft, no GPU map) */
static void
vhole_dim(SDL_Surface *g, int rx0, int ry0, int dim, int amin, int soft)
{
    if (dim <= 0 || !g)
        return;
    SDL_SetSurfaceColorMod(g, 0, 0, 0);
    SDL_SetSurfaceAlphaMod(g, (Uint8) (dim * (soft >= 0 ? soft : 255) / 255));
    if (amin < 255 && vulture_gpumap_on)
        vulture_gpumap_holed(g, rx0, ry0, (float) vwall_hx, (float) (vwall_hy - VWALL_BODY), VWALL_R0, VWALL_R1,
                             amin);
    else
        vulture_put_img(rx0, ry0, g);
    SDL_SetSurfaceColorMod(g, 255, 255, 255);
    SDL_SetSurfaceAlphaMod(g, 255);
}

/* s210: a tile at (x, y) (the square's point), see-through round you by
   the amount f (<= 0 solid, 1 the full circle) if its rect reaches the
   circle; returns how solid the circle's middle was (255: drawn plain).
   s212: dim > 0 darkens it (a wall's outside, facing the dark) */
static int
vhole_put(int x, int y, int tile, int shade, float f, int dim = 0)
{
    vulture_tile *t;

    if (f > 0.0f && vwall_hole_on && vwall_amin < 250 && tile >= 0 && tile != V_TILE_NONE
        && (t = vulture_get_tile_shaded(tile, shade)) != NULL && t->graphic) {
        float cx = (float) vwall_hx, cy = (float) (vwall_hy - VWALL_BODY);
        float rx0 = (float) (x + t->xmod), ry0 = (float) (y + t->ymod);
        float rx1 = rx0 + t->graphic->w, ry1 = ry0 + t->graphic->h;
        float qx = cx < rx0 ? rx0 : cx > rx1 ? rx1 : cx, qy = cy < ry0 ? ry0 : cy > ry1 ? ry1 : cy;

        if ((qx - cx) * (qx - cx) + (qy - cy) * (qy - cy) < VWALL_R1 * VWALL_R1) {
            int amin;

            f = f >= 1.0f ? 1.0f : f * f * (3.0f - 2.0f * f); /* stepping through a door: it fades */
            amin = (int) (255.0f - (255.0f - vwall_amin) * f + 0.5f);
            if (vulture_gpumap_on) {
                vulture_gpumap_holed(t->graphic, (int) rx0, (int) ry0, cx, cy, VWALL_R0, VWALL_R1, amin);
                if (vulture_normal_capture) {
                    SDL_Surface *nm = vulture_get_tile_normal(tile, false);

                    if (nm)
                        vulture_gpumap_normal_holed(nm, (int) rx0, (int) ry0, cx, cy, VWALL_R0, VWALL_R1,
                                                    amin);
                }
                vhole_dim(t->graphic, (int) rx0, (int) ry0, dim, amin, -1); /* s212 */
            } else { /* no GPU map: the whole tile, as see-through as its nearest point */
                float d = sqrtf((qx - cx) * (qx - cx) + (qy - cy) * (qy - cy)), g;
                int a;

                g = d <= VWALL_R0 ? 0.0f : (d - VWALL_R0) / (VWALL_R1 - VWALL_R0);
                g = g * g * (3.0f - 2.0f * g);
                a = (int) (amin + (255 - amin) * g);
                SDL_SetSurfaceAlphaMod(t->graphic, (Uint8) a);
                vulture_put_tile_shaded(x, y, tile, shade);
                SDL_SetSurfaceAlphaMod(t->graphic, 255);
                vhole_dim(t->graphic, (int) rx0, (int) ry0, dim, 255, a); /* s212 */
            }
            return amin;
        }
    }
    vulture_put_tile_shaded(x, y, tile, shade);
    if (dim > 0 && tile >= 0 && tile != V_TILE_NONE && (t = vulture_get_tile_shaded(tile, shade)) != NULL
        && t->graphic) /* s212: a wall's outside, darkened */
        vhole_dim(t->graphic, x + t->xmod, y + t->ymod, dim, 255, -1);
    return 255;
}

/* steps 1 and 7 of the map: a wall tile of the square drawn at (x, y) - side
   0 west, 1 north (a room's front walls), 2 south, 3 east (its back walls) */
static void
vwall_draw(int x, int y, int tile, int side, int shade, int ox = -1, int oy = -1, int dim2 = 0)
{
    const float k = (float) V_MAP_YMOD / V_MAP_XMOD;
    bool eface = side == 0 || side == 3; /* an east face: its foot runs up to the right */
    vulture_tile *t;
    float foot;
    int out, amin, dim = dim2; /* s218: an outside face, darkened */

    if (tile < 0 || tile == V_TILE_NONE)
        return;
    if (side <= 1) {
        if ((out = vwall_outer(tile)) < 0) {
            vulture_put_tile_shaded(x, y, tile, shade);
            return;
        }
        tile = out; /* the face toward you, on this edge */
        x += side == 0 ? -V_MAP_XMOD : V_MAP_XMOD;
        y -= V_MAP_YMOD;
        /* s212: it is the wall's OUTSIDE - lit by what it faces (the square
           beyond the wall, (ox, oy)), not by the room.  Rock beyond is dark
           (the light grid gives the stone round a lit room half its light);
           open ground beyond lends its own light - and ground you have not
           seen has none, so nothing shows that you don't know */
        if (!isok(ox, oy) || IS_OBSTRUCTED(levl[ox][oy].typ)) {
            dim = VWALL_DIM;
        } else {
            float lo = vl_light_at(ox, oy) / 0.7f;

            dim = (int) (VWALL_DIM * (lo >= 1.0f ? 0.0f : lo <= 0.0f ? 1.0f : 1.0f - lo));
        }
    }
    /* the wall's foot, carried on to where you stand: in front of you
       (lower on the screen) it hides you; at your side or behind, not */
    foot = eface ? y + V_MAP_YMOD - (vwall_hx - x) * k : y + (vwall_hx - x + V_MAP_XMOD) * k;
    if (vcut_now < 255) { /* s223: the room you are in - this wall see-through, all of it */
        vulture_tile *ct = vulture_get_tile_shaded(tile, shade);

        dim = 0;
        if (ct && ct->graphic)
            SDL_SetSurfaceAlphaMod(ct->graphic, (Uint8) vcut_now);
        amin = vhole_put(x, y, tile, shade, (foot - vwall_hy + 11.0f) / 22.0f, 0);
        if (ct && ct->graphic)
            SDL_SetSurfaceAlphaMod(ct->graphic, 255);
        amin = amin < vcut_now ? amin : vcut_now;
    } else
        amin = vhole_put(x, y, tile, shade, (foot - vwall_hy + 11.0f) / 22.0f, dim);
    if ((t = vulture_get_tile_shaded(tile, shade)) != NULL && t->graphic) { /* s210: what it may hide */
        vxr_face_t fc;

        fc.x0 = (float) (eface ? x : x - V_MAP_XMOD);
        fc.y0 = (float) (eface ? y + V_MAP_YMOD : y);
        fc.k = eface ? -k : k;
        fc.h = -t->ymod;
        fc.seq = ++vxr_seq;
        fc.amin = amin;
        fc.cap = false;
        fc.cut = vcut_now; /* s223 */
        vxr_faces.push_back(fc);
    }
}

/* the void's dust: is screen point (sx, sy) clear of the dungeon - nothing
   mapped on its square, behind it (whose slab face hangs into it) or up to
   two rows in front (whose walls rise over it) */
static bool
vvoid_open(mapdata *md, int sx, int sy)
{
    static const int nb[9][2] = { { 0, 0 }, { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 },
                                  { 1, 1 }, { 2, 1 }, { 1, 2 }, { 2, 2 } };
    double A = (double) (sx - vsp_cx) / V_MAP_XMOD, B = (double) (sy - vsp_cy) / V_MAP_YMOD;
    int va = (int) floor((A + B) / 2.0 + 0.5), vb = (int) floor((B - A) / 2.0 + 0.5), k;

    for (k = 0; k < 9; k++) {
        int dj, di, j, i, b;

        v_rot(va + nb[k][0], vb + nb[k][1], vulture_view_rot, &dj, &di);
        j = vsp_vx + dj, i = vsp_vy + di;
        if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO)
            continue;
        b = md->get_glyph(MAP_BACK, j, i);
        if (b != V_MISC_UNMAPPED_AREA && b != V_TILE_NONE)
            return false;
    }
    return true;
}

/* the overlay pass: dust drifting deep in the void, slower than the map */
static void
vvoid_draw(mapdata *md, double t)
{
    int ox, oy, k, layer, W, H;

    if (!md || !vfx_ambient || !u.ux || u.uswallow || Underwater || !vulture_screen)
        return;
    if (vrock_on && vrock_style != 2) /* s215: the dark is rock now, not a void (s219: unless walls only) */
        return;
    W = vulture_screen->w + 240, H = vulture_screen->h + 240;
    vsp_screen(0, 0, &ox, &oy); /* where the map lies: the dust follows it, slower */
    for (layer = 0; layer < 2; layer++) {
        double f = layer ? 0.72 : 0.45;
        int n = layer ? 40 : 70;

        for (k = 0; k < n; k++) {
            double ph = vsp_hash(k, layer, 47) * 6.2832;
            double px = vsp_hash(k, layer, 41) * W + ox * f + t * (vsp_hash(k, layer, 51) - 0.5) * 7.0;
            double py = vsp_hash(layer, k, 43) * H + oy * f + t * (vsp_hash(k, layer, 53) - 0.5) * 4.0;
            int sx, sy;

            px = fmod(px, (double) W), py = fmod(py, (double) H);
            sx = (int) (px < 0 ? px + W : px) - 120, sy = (int) (py < 0 ? py + H : py) - 120;
            if (!vvoid_open(md, sx, sy))
                continue;
            vsp_blob(sx, sy, layer ? 1.6 : 1.0, 150, 165, 200,
                     (int) ((layer ? 120 : 70) * (0.6 + 0.4 * sin(t * 0.9 + ph))), true);
        }
    }
}

/* probe: s209 - VULTURE_SELFTEST_WALLS: you stand just behind a front wall
   (a floor square whose screen down-left or down-right neighbour is wall),
   shot, and the same with no circle; then just inside a doorway in a front
   wall, the same two */
static Uint32 vwall_probe_t = 0;
static int vwall_probe_n = 0; /* 0 move behind, 1 shoot, 2 no circle, 3 move to a door, 4 shoot, 5 no circle */

static void
vwall_probe_tick(Uint32 now)
{
    char p[700];

    if (now - vwall_probe_t < 1200)
        return;
    vwall_probe_t = SDL_GetTicks();
    if (vwall_probe_n == 0 || vwall_probe_n == 3) {
        int x, y, bx = 0, by = 0, best = 9999;

        for (y = 1; y < ROWNO - 1; y++)
            for (x = 2; x < COLNO - 1; x++) {
                int sj, si, ej, ei, d;
                bool front;

                if (levl[x][y].typ != ROOM || m_at(x, y) || !levl[x][y].seenv)
                    continue;
                v_rot(0, 1, vulture_view_rot, &sj, &si);
                v_rot(1, 0, vulture_view_rot, &ej, &ei);
                if (vwall_probe_n == 3) /* a doorway in front of you */
                    front = (isok(x + sj, y + si) && IS_DOOR(levl[x + sj][y + si].typ))
                            || (isok(x + ej, y + ei) && IS_DOOR(levl[x + ej][y + ei].typ));
                else
                    front = (isok(x + sj, y + si) && IS_WALL(levl[x + sj][y + si].typ))
                            || (isok(x + ej, y + ei) && IS_WALL(levl[x + ej][y + ei].typ));
                if (!front)
                    continue;
                d = distmin(u.ux, u.uy, x, y) + (levl[x][y].lit ? 0 : 40);
                if (d < best)
                    best = d, bx = x, by = y;
            }
        if (bx && (bx != u.ux || by != u.uy)) {
            teleds(bx, by, TELEDS_NO_FLAGS);
            vision_recalc(0);
        }
        flush_screen(1);
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "walls probe %d: you at %d,%d\n", vwall_probe_n, u.ux,
                          u.uy);
    } else {
        static const char *const nm[6] = { "", "behind", "behind_solid", "", "door", "door_solid" };

        vwall_probe_off = vwall_probe_n == 2 || vwall_probe_n == 5;
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "walls_%s.bmp", getenv("VULTURE_SELFTEST"), nm[vwall_probe_n]);
        vulture_save_frame_bmp(p);
        vwall_probe_off = false;
    }
    if (++vwall_probe_n >= 6) {
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "walls probe: done\n");
        vwall_probe_t = 0;
    }
}

/* s210: a sprite's shape: 1 inside, 2 its edge (a band two pixels wide
   across the outline), 0 outside; kept per tile and facing */
static const std::vector<Uint8> *
vxr_mask(int tile, bool flip, SDL_Surface *s)
{
    static std::map<int, std::vector<Uint8> > cache;
    int key = tile * 2 + (flip ? 1 : 0), w = s->w, h = s->h, x, y, bpp;
    std::map<int, std::vector<Uint8> >::iterator it = cache.find(key);
    std::vector<Uint8> in;
    Uint32 ck = 0;
    bool haskey;

    if (it != cache.end() && it->second.size() == (size_t) w * (size_t) h)
        return &it->second;
    std::vector<Uint8> &mk = cache[key];

    mk.assign((size_t) w * (size_t) h, 0);
    if (w <= 0 || h <= 0 || SDL_LockSurface(s) != 0)
        return &mk;
    bpp = s->format->BytesPerPixel;
    haskey = SDL_GetColorKey(s, &ck) == 0;
    in.assign((size_t) w * (size_t) h, 0);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const Uint8 *q = (const Uint8 *) s->pixels + (size_t) y * s->pitch + (size_t) x * bpp;
            Uint32 v = bpp == 4 ? *(const Uint32 *) q
                       : bpp == 3 ? (Uint32) q[0] | (Uint32) q[1] << 8 | (Uint32) q[2] << 16
                       : bpp == 2 ? (Uint32) *(const Uint16 *) q : (Uint32) *q;
            Uint8 r, g, b, a;

            SDL_GetRGBA(v, s->format, &r, &g, &b, &a);
            in[(size_t) y * w + x] = a >= 110 && !(haskey && v == ck);
        }
    SDL_UnlockSurface(s);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            bool me = in[(size_t) y * w + x] != 0, edge = false;
            int d;

            for (d = 0; d < 4 && !edge; d++) {
                int nx = x + (d == 0 ? -1 : d == 1 ? 1 : 0), ny = y + (d == 2 ? -1 : d == 3 ? 1 : 0);
                bool nb = nx >= 0 && nx < w && ny >= 0 && ny < h && in[(size_t) ny * w + nx];

                edge = nb != me;
            }
            mk[(size_t) y * w + x] = edge ? 2 : me ? 1 : 0;
        }
    return &mk;
}

/* the overlay pass: creatures behind wall faces, seen through them */
static void
vxr_draw(void)
{
    static SDL_Surface *scr = NULL;
    const int S = 256;
    size_t m, q;

    if (vxr_mons.empty() || vxr_faces.empty() || !vulture_screen)
        return;
    if (!scr) {
        if (!(scr = SDL_CreateRGBSurfaceWithFormat(0, S, S, 32, SDL_PIXELFORMAT_ARGB8888)))
            return;
        SDL_SetSurfaceBlendMode(scr, SDL_BLENDMODE_BLEND);
    }
    for (m = 0; m < vxr_mons.size(); m++) {
        const vxr_mon_t &c = vxr_mons[m];
        vulture_tile *tile = vulture_get_tile(c.tile);
        const std::vector<Uint8> *mk;
        SDL_Surface *img;
        SDL_Rect sr, dr;
        int art, dx, dy, w, h, bx0, by0, bx1, by1, px, py, nf = 0, cov[32], k;
        bool flip;
        double fx, fy, cs, sn;

        if (!tile || !tile->graphic || fabs(c.xs) < 0.15 || fabs(c.ys) < 0.15)
            continue;
        /* as vtw_put_tile_x drew it */
        art = c.face ? vtw_art_facing(c.tile, tile->graphic) : 0;
        flip = c.face != 0 && ((art == 0 && c.face < 0) || (art != 0 && art != c.face));
        img = tile->graphic, dx = c.tx + tile->xmod;
        if (flip) {
            SDL_Surface *fl = vtw_flipped(c.tile, tile->graphic);

            if (fl)
                img = fl, dx = c.tx - tile->xmod - tile->graphic->w + 1;
            else
                flip = false;
        }
        dy = c.ty + tile->ymod, w = img->w, h = img->h;
        fx = dx + w / 2.0, fy = dy + h; /* its feet: what it was scaled and turned about */
        cs = cos(c.deg * 3.14159265358979 / 180.0), sn = sin(c.deg * 3.14159265358979 / 180.0);
        bx0 = by0 = 1 << 30, bx1 = by1 = -(1 << 30);
        for (k = 0; k < 4; k++) {
            double rx = ((k & 1 ? w : 0) - w / 2.0) * c.xs, ry = ((k & 2 ? h : 0) - h) * c.ys;
            int ex = (int) floor(fx + cs * rx - sn * ry), ey = (int) floor(fy + sn * rx + cs * ry);

            bx0 = ex < bx0 ? ex : bx0, bx1 = ex + 1 > bx1 ? ex + 1 : bx1;
            by0 = ey < by0 ? ey : by0, by1 = ey + 1 > by1 ? ey + 1 : by1;
        }
        if (bx1 - bx0 > S)
            bx1 = bx0 + S;
        if (by1 - by0 > S)
            by1 = by0 + S;
        /* the faces drawn over it */
        for (q = 0; q < vxr_faces.size() && nf < 32; q++) {
            const vxr_face_t &f = vxr_faces[q];
            float ylo = f.k > 0 ? f.y0 : f.y0 + f.k * 56.0f, yhi = f.k > 0 ? f.y0 + f.k * 56.0f : f.y0;

            if (f.cap) { /* s215: a top, a diamond round (x0, y0) */
                if (f.seq < c.seq || f.x0 - V_MAP_XMOD > bx1 || f.x0 + V_MAP_XMOD < bx0
                    || f.y0 - V_MAP_YMOD > by1 || f.y0 + V_MAP_YMOD < by0)
                    continue;
                cov[nf++] = (int) q;
                continue;
            }
            if (f.seq < c.seq || f.x0 > bx1 || f.x0 + 56.0f < bx0 || yhi < by0 || ylo - f.h > by1)
                continue;
            cov[nf++] = (int) q;
        }
        if (!nf || !(mk = vxr_mask(c.tile, flip, img)) || mk->empty())
            continue;
        for (py = by0; py < by1; py++)
            memset((Uint8 *) scr->pixels + (size_t) (py - by0) * scr->pitch, 0, (size_t) (bx1 - bx0) * 4);
        for (py = by0; py < by1; py++) {
            Uint32 *row = (Uint32 *) ((Uint8 *) scr->pixels + (size_t) (py - by0) * scr->pitch);

            for (px = bx0; px < bx1; px++) {
                float X = px + 0.5f, Y = py + 0.5f, vis = 0.0f;
                double rx, ry, u, v;
                int iu, iv, val, al;

                for (k = 0; k < nf; k++) {
                    const vxr_face_t &f = vxr_faces[cov[k]];
                    float foot, a = 1.0f, e;

                    if (f.cap) { /* s215 */
                        if (fabsf(X - f.x0) / V_MAP_XMOD + fabsf(Y - f.y0) / V_MAP_YMOD > 1.0f)
                            continue;
                    } else {
                        if (X < f.x0 || X > f.x0 + 56.0f)
                            continue;
                        foot = f.y0 + f.k * (X - f.x0);
                        if (Y > foot || Y < foot - f.h)
                            continue;
                    }
                    if (f.amin < 255) { /* see-through round you: it shows there already */
                        float hx = X - vwall_hx, hy = Y - (vwall_hy - VWALL_BODY);
                        float d = sqrtf(hx * hx + hy * hy), g;

                        g = d <= VWALL_R0 ? 0.0f : d >= VWALL_R1 ? 1.0f : (d - VWALL_R0) / (VWALL_R1 - VWALL_R0);
                        g = g * g * (3.0f - 2.0f * g);
                        a = (f.amin + (255.0f - f.amin) * g) / 255.0f;
                    }
                    if (f.cut < 255 && f.cut / 255.0f < a) /* s223: see-through all over */
                        a = f.cut / 255.0f;
                    e = (a - 0.5f) / 0.5f;
                    vis = e > vis ? e : vis;
                }
                if (vis <= 0.0f)
                    continue;
                rx = X - fx, ry = Y - fy;
                u = (cs * rx + sn * ry) / c.xs + w / 2.0, v = (-sn * rx + cs * ry) / c.ys + h;
                iu = (int) floor(u), iv = (int) floor(v);
                if (iu < 0 || iu >= w || iv < 0 || iv >= h || !(val = (*mk)[(size_t) iv * w + iu]))
                    continue;
                al = (int) ((val == 2 ? 215.0f : 48.0f) * (vis > 1.0f ? 1.0f : vis));
                row[px - bx0] = (Uint32) al << 24 | (Uint32) c.r << 16 | (Uint32) c.g << 8 | c.b;
            }
        }
        sr.x = 0, sr.y = 0, sr.w = bx1 - bx0, sr.h = by1 - by0;
        dr.x = bx0, dr.y = by0, dr.w = sr.w, dr.h = sr.h;
        SDL_BlitSurface(scr, &sr, vulture_screen, &dr);
    }
}

/* probe: s210 - VULTURE_SELFTEST_XRAY: a hostile jackal and a peaceful
   gnome just behind front walls a few squares off, shot */
static Uint32 vxr_probe_t = 0;
static int vxr_probe_n = 0;

static void
vxr_probe_tick(Uint32 now)
{
    char p[700];

    if (now - vxr_probe_t < 1200)
        return;
    vxr_probe_t = SDL_GetTicks();
    if (vxr_probe_n == 0) {
        int x, y, placed = 0;

        for (y = 1; y < ROWNO - 1 && placed < 3; y++)
            for (x = 2; x < COLNO - 1 && placed < 3; x++) {
                int sj, si, ej, ei;
                struct monst *mm;
                bool front;

                if (levl[x][y].typ != ROOM || m_at(x, y) || u_at(x, y) || !cansee(x, y)
                    || distmin(u.ux, u.uy, x, y) < 2)
                    continue;
                v_rot(0, 1, vulture_view_rot, &sj, &si);
                v_rot(1, 0, vulture_view_rot, &ej, &ei);
                front = (isok(x + sj, y + si) && IS_WALL(levl[x + sj][y + si].typ))
                        || (isok(x + ej, y + ei) && IS_WALL(levl[x + ej][y + ei].typ));
                if (!front)
                    continue;
                if (placed == 2) { /* s212: and an item */
                    (void) mksobj_at(CHEST, x, y, FALSE, FALSE);
                    newsym(x, y);
                    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "xray probe: chest at %d,%d\n", x, y);
                    placed++;
                    continue;
                }
                if ((mm = makemon(&mons[placed ? PM_GNOME : PM_JACKAL], x, y, NO_MM_FLAGS)) != 0) {
                    if (placed)
                        mm->mpeaceful = 1;
                    mm->msleeping = 1; /* it stays for the shot */
                    newsym(x, y);
                    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "xray probe: %s at %d,%d\n",
                                      placed ? "peaceful gnome" : "jackal", x, y);
                    placed++;
                    x += 2; /* not side by side */
                }
            }
        flush_screen(1);
    } else {
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "xray.bmp", getenv("VULTURE_SELFTEST"));
        vulture_save_frame_bmp(p);
    }
    if (++vxr_probe_n >= 2) {
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "xray probe: done\n");
        vxr_probe_t = 0;
    }
}

/*
 * IMPLEMENTATION_LOG s211: torches on the walls of lit rooms.
 *  - A lit room's back walls (the faces you look at) carry a torch in an
 *    iron sconce on every third square along a wall ((2j + i + the level)
 *    % 3 - whichever way the wall runs), never on a banner, a painting or
 *    a niche.  Dark rooms stay dark.
 *  - The flame is drawn on the map layer (walls and creatures in front of
 *    it hide it) as additive blobs that flicker and lean; seen, each torch
 *    is a point light in vl_compute (warm, radius 2.4, lifted to the
 *    flame), so the relief light, the shine, the creatures' shadows and the
 *    light pools all move with it.  Out of sight only the sconce is drawn.
 */
struct vtorch_t {
    int fj, fi; /* the floor square it lights */
};
static std::vector<vtorch_t> vtorch_list; /* this frame's, from the map pass */

static SDL_Surface *
vtorch_sconce(void) /* an iron bracket holding a torch: 9 x 16, its foot at the bottom middle */
{
    static SDL_Surface *s = NULL;
    int x, y;

    if (s)
        return s;
    if (!(s = SDL_CreateRGBSurfaceWithFormat(0, 9, 16, 32, SDL_PIXELFORMAT_ARGB8888)))
        return NULL;
    SDL_FillRect(s, NULL, 0);
    SDL_LockSurface(s);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 9; x++) {
            Uint32 *q = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch) + x, c = 0;

            if (y <= 1 && x >= 3 && x <= 5)
                c = y == 0 ? 0xFF2A201A : 0xFF3A2A20; /* charred top */
            else if (y == 7 && x >= 2 && x <= 6)
                c = 0xFF77716B; /* the ring, lit edge */
            else if (y == 8 && x >= 2 && x <= 6)
                c = 0xFF46423E;
            else if (y >= 2 && y <= 11 && x >= 3 && x <= 5)
                c = x == 3 ? 0xFF8A5A33 : x == 4 ? 0xFF6E4728 : 0xFF4E321C; /* the stick */
            else if (y == 12 && x >= 3 && x <= 5)
                c = 0xFF5A554F; /* the plate on the wall */
            else if (y >= 13 && x >= 3 && x <= 5)
                c = 0xFF3A3633;
            else if (y >= 13 && y <= 14 && (x == 2 || x == 6))
                c = 0xFF2E2B29;
            *q = c;
        }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    return s;
}

static void
vtorch_glow(int x, int y, int r, int g, int b, int alpha) /* the flame's light on the wall: 49 px */
{
    static SDL_Surface *s = NULL;

    if (alpha <= 2)
        return;
    if (!s) {
        int px, py;

        if (!(s = SDL_CreateRGBSurfaceWithFormat(0, 49, 49, 32, SDL_PIXELFORMAT_ARGB8888)))
            return;
        SDL_LockSurface(s);
        for (py = 0; py < 49; py++)
            for (px = 0; px < 49; px++) {
                double dx = (px - 24) / 24.5, dy = (py - 24) / 24.5, d = sqrt(dx * dx + dy * dy);
                double a = d >= 1.0 ? 0.0 : (1.0 - d) * (1.0 - d);

                ((Uint32 *) ((Uint8 *) s->pixels + (size_t) py * s->pitch))[px] =
                    (Uint32) (a * 255.0) << 24 | 0x00FFFFFFu;
            }
        SDL_UnlockSurface(s);
        SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_ADD);
    }
    SDL_SetSurfaceColorMod(s, (Uint8) r, (Uint8) g, (Uint8) b);
    SDL_SetSurfaceAlphaMod(s, (Uint8) (alpha > 255 ? 255 : alpha));
    vulture_put_img(x - 24, y - 24, s);
}

static void
vtorch_blob(int x, int y, int ir, int r, int g, int b, int alpha, bool add = true) /* on the map layer */
{
    SDL_Surface *s;
    int k = add ? 0 : 1; /* s214: blended, a flame shows on white marble too */

    ir = ir < 1 ? 1 : ir > 8 ? 8 : ir;
    if (alpha <= 2)
        return;
    if (!vsp_disc[k][ir] && !(vsp_disc[k][ir] = vsp_make_disc(ir, add)))
        return;
    s = vsp_disc[k][ir];
    SDL_SetSurfaceColorMod(s, (Uint8) r, (Uint8) g, (Uint8) b);
    SDL_SetSurfaceAlphaMod(s, (Uint8) (alpha > 255 ? 255 : alpha));
    vulture_put_img(x - ir, y - ir, s);
}

/* step 7 of the map: a back wall's face (tile) on square (j, i), drawn at
   (x, y), facing the floor square (fj, fi) - a torch on it? */
static void
vtorch_draw(int x, int y, int j, int i, int fj, int fi, int tile, bool eface)
{
    vulture_tile *wt;
    SDL_Surface *sc;
    int k, tx, ty, seed;

    if (!vfx_ambient || tile < 0 || tile == V_TILE_NONE || !isok(j, i) || !isok(fj, fi))
        return;
    if (levl[fj][fi].typ != ROOM || !levl[fj][fi].lit || !levl[j][i].seenv || !levl[fj][fi].seenv)
        return;
    seed = (int) ledger_no(&u.uz) * 7;
    if ((2 * j + i + seed) % 3 != 0) /* every third square, whichever way the wall runs */
        return;
    for (k = 1; k <= 3; k++) /* not on a banner, a painting or a niche (a pillar will do) */
        if (tile == walls_full[V_WALL_BRICK + k].south || tile == walls_full[V_WALL_BRICK + k].east
            || tile == walls_half[V_WALL_BRICK + k].south || tile == walls_half[V_WALL_BRICK + k].east)
            return;
    if (!(wt = vulture_get_tile(tile)) || !wt->graphic)
        return;
    /* the middle of the face's foot, a little over half way up it */
    tx = eface ? x + V_MAP_XMOD / 2 : x - V_MAP_XMOD / 2;
    ty = y + V_MAP_YMOD / 2 - (int) (-wt->ymod * 0.55);
    if ((sc = vtorch_sconce()) != NULL)
        vulture_put_img(tx - 4, ty - 15, sc);
    {
        vtorch_t q;

        q.fj = fj, q.fi = fi;
        vtorch_list.push_back(q);
    }
    if (cansee(fj, fi)) { /* the flame */
        double tt = SDL_GetTicks() / 1000.0, ph = j * 1.9 + i * 2.7;
        double fl = 0.82 + 0.10 * sin(tt * 8.3 + ph) + 0.06 * sin(tt * 19.7 + j) + 0.03 * ((rand() % 100) / 100.0);
        int lean = (int) floor(1.4 * sin(tt * 5.1 + ph) + 0.5), fy = ty - 17;

        vtorch_glow(tx, fy, 255, 125, 40, (int) (52 * fl)); /* its light on the wall round it */
        vtorch_blob(tx, fy - 2, 7, 255, 125, 35, (int) (115 * fl));
        /* the flame's body blended, not added: on white marble an added
           orange is white (s214) */
        vtorch_blob(tx, fy, 4, 240, 120, 30, (int) (230 * fl), false);
        vtorch_blob(tx + lean, fy - 3, 3, 255, 185, 70, (int) (235 * fl), false);
        vtorch_blob(tx + lean, fy - 7 - (int) (2.5 * fl), 2, 250, 150, 45, (int) (200 * fl), false);
        vtorch_blob(tx, fy - 1, 2, 255, 245, 200, 255, false);
    }
}

/* probe: s211 - VULTURE_SELFTEST_TORCH: the room you start in, twice, a
   quarter second apart (the flames move) */
static Uint32 vtorch_probe_t = 0;
static int vtorch_probe_n = 0;

static void
vtorch_probe_tick(Uint32 now)
{
    char p[700];

    if (now - vtorch_probe_t < (vtorch_probe_n ? 250u : 1500u))
        return;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    snprintf(p, sizeof p, "%s" "\\" "torch_%c.bmp", getenv("VULTURE_SELFTEST"), 'a' + vtorch_probe_n);
    vulture_save_frame_bmp(p);
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "torch probe: shot %d, %d torches\n", vtorch_probe_n,
                      (int) vtorch_list.size());
    vtorch_probe_t = SDL_GetTicks();
    if (++vtorch_probe_n >= 2)
        vtorch_probe_t = 0;
}

/*
 * IMPLEMENTATION_LOG s212: the colour of where you are.  Each part of the
 * dungeon gets its own grade, a multiply over the map layer done on the
 * GPU with the bloom (vgm_bloom_pass): the Mines warm, Sokoban cool,
 * Gehennom red, Fort Ludios gold, Vlad's Tower violet, Medusa's island
 * sea-green, the Quest faintly violet, each Plane its element; the main
 * dungeon untouched.  Probe VULTURE_SELFTEST_GRADE shoots each in turn.
 */
static const char *vgrade_probe_name = NULL; /* probe: an area forced for a shot */

static void
vgrade_area(int *r, int *g, int *b)
{
    const char *f = vgrade_probe_name;

    *r = *g = *b = 255;
    if (f ? !strcmp(f, "astral") : Is_astralevel(&u.uz))
        *r = 232, *g = 238, *b = 255;
    else if (f ? !strcmp(f, "fire") : Is_firelevel(&u.uz))
        *r = 255, *g = 212, *b = 184;
    else if (f ? !strcmp(f, "water") : Is_waterlevel(&u.uz))
        *r = 208, *g = 228, *b = 255;
    else if (f ? !strcmp(f, "earth") : Is_earthlevel(&u.uz))
        *r = 246, *g = 232, *b = 214;
    else if (f ? !strcmp(f, "air") : Is_airlevel(&u.uz))
        *r = 238, *g = 247, *b = 255;
    else if (f ? !strcmp(f, "gehennom") : In_hell(&u.uz))
        *r = 255, *g = 200, *b = 176;
    else if (f ? !strcmp(f, "mines") : In_mines(&u.uz))
        *r = 255, *g = 236, *b = 206;
    else if (f ? !strcmp(f, "sokoban") : In_sokoban(&u.uz))
        *r = 218, *g = 232, *b = 255;
    else if (f ? !strcmp(f, "ludios") : Is_knox(&u.uz))
        *r = 255, *g = 242, *b = 200;
    else if (f ? !strcmp(f, "vlad") : In_V_tower(&u.uz))
        *r = 226, *g = 210, *b = 246;
    else if (f ? !strcmp(f, "medusa") : Is_medusa_level(&u.uz))
        *r = 212, *g = 244, *b = 236;
    else if (f ? !strcmp(f, "quest") : In_quest(&u.uz))
        *r = 242, *g = 232, *b = 255;
}

/* probe: s212 - VULTURE_SELFTEST_GRADE: the same view as each area */
static Uint32 vgrade_probe_t = 0;
static int vgrade_probe_n = 0;

static void
vgrade_probe_tick(Uint32 now)
{
    static const char *const nm[6] = { "dungeon", "mines", "sokoban", "gehennom", "vlad", "medusa" };
    char p[700];

    if (now - vgrade_probe_t < 900)
        return;
    if (vgrade_probe_n > 0) { /* the last one, shot */
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "grade_%s.bmp", getenv("VULTURE_SELFTEST"), nm[vgrade_probe_n - 1]);
        vulture_save_frame_bmp(p);
    }
    if (vgrade_probe_n >= 6) {
        vgrade_probe_name = NULL, vgrade_probe_t = 0;
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "grade probe: done\n");
        return;
    }
    vgrade_probe_name = vgrade_probe_n == 0 ? "none" : nm[vgrade_probe_n];
    vgrade_probe_n++, vgrade_probe_t = SDL_GetTicks();
    levwin->force_redraw();
}

/*
 * IMPLEMENTATION_LOG s213: water that reflects.
 *  - A creature (you too) standing on or by water is mirrored in it: at
 *    the monster step, before its sprite, its pose (tile, facing, glide,
 *    scale) is laid upside down from its feet onto the water and ice
 *    squares in front of it, clipped to their diamonds in 2 px strips -
 *    on water each strip sways with the waves and the colour goes blue and
 *    fades with depth; ice reflects sharper and paler, without waves.
 *    Whatever stands on those squares, and walls in front, are drawn over
 *    it later in the pass.  GPU map only (vulture_gpumap_quad).
 *  - A creature IN water (not flying or floating) sends rings out from its
 *    feet.
 */
static bool
vrefl_wet(mapdata *md, int x, int y, bool *ice) /* water or ice the hero knows */
{
    int b;

    if (!md || !isok(x, y) || !levl[x][y].seenv)
        return false;
    b = md->get_glyph(MAP_BACK, x, y);
    if (ice)
        *ice = b == V_TILE_FLOOR_ICE;
    return b == V_TILE_FLOOR_WATER || b == V_TILE_FLOOR_ICE;
}

/* the monster step: the creature on (j, i), about to be drawn at (tx, ty)
   facing face, scaled xs, ys - its reflection, and rings if it is in water */
static void
vrefl_draw(mapdata *md, int j, int i, int tile_id, int face, int tx, int ty, double xs, double ys, int cx, int cy,
           int vx, int vy)
{
    /* its square and the ones in front, as screen steps (va, vb) */
    static const int nb[9][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 0 },
                                  { 0, 2 }, { 2, 1 }, { 1, 2 }, { 2, 2 } };
    vulture_tile *tile;
    SDL_Surface *img;
    int art, dx, dy, w, h, k, feet, x0, x1;
    double tt;
    bool flip, any = false, wet[9], ice[9];
    int wx[9], wy[9];

    if (!vulture_gpumap_on || !md || !vfx_ambient || !(tile = vulture_get_tile(tile_id)) || !tile->graphic)
        return;
    for (k = 0; k < 9; k++) {
        int dj, di;

        v_rot(nb[k][0], nb[k][1], vulture_view_rot, &dj, &di);
        wet[k] = vrefl_wet(md, j + dj, i + di, &ice[k]);
        if (wet[k]) {
            vfx_square_screen(j + dj, i + di, cx, cy, vx, vy, &wx[k], &wy[k]);
            any = true;
        }
    }
    if (!any)
        return;
    art = face ? vtw_art_facing(tile_id, tile->graphic) : 0;
    flip = face != 0 && ((art == 0 && face < 0) || (art != 0 && art != face));
    img = tile->graphic, dx = tx + tile->xmod;
    if (flip) {
        SDL_Surface *fl = vtw_flipped(tile_id, tile->graphic);

        if (fl)
            img = fl, dx = tx - tile->xmod - tile->graphic->w + 1;
    }
    dy = ty + tile->ymod, w = img->w, h = img->h;
    if (xs < 0.2 || ys < 0.2)
        return;
    feet = dy + h;
    {
        double cxm = dx + w / 2.0;

        x0 = (int) floor(cxm - w / 2.0 * xs), x1 = (int) ceil(cxm + w / 2.0 * xs);
    }
    tt = SDL_GetTicks() / 1000.0;
    for (k = 0; k < 9; k++) {
        int d;

        if (!wet[k])
            continue;
        for (d = 0; d < h; d += 2) { /* screen rows below the feet, two at a time */
            int y = feet + d, yc = y + 1, dyr = abs(yc - wy[k]), hw, xa, xb, wave, v, al;
            float q[8];
            SDL_Rect src;

            if (dyr >= V_MAP_YMOD)
                continue;
            hw = V_MAP_XMOD * (V_MAP_YMOD - dyr) / V_MAP_YMOD;
            xa = wx[k] - hw > x0 ? wx[k] - hw : x0;
            xb = wx[k] + hw < x1 ? wx[k] + hw : x1;
            if (xb - xa < 1)
                continue;
            v = h - 2 - (int) (d / ys); /* the sprite's rows, from its feet up */
            if (v < 0)
                break;
            src.x = (int) ((xa - x0) / xs), src.w = (int) ceil((xb - xa) / xs);
            src.y = v, src.h = 2;
            if (src.x < 0)
                src.x = 0;
            if (src.x + src.w > w)
                src.w = w - src.x;
            if (src.w < 1)
                continue;
            wave = ice[k] ? 0 : (int) floor(1.6 * sin(y * 0.45 + tt * 2.4 + j) + 0.5);
            /* upside down: the source's top row goes at the strip's bottom */
            q[0] = (float) (xa + wave), q[1] = (float) (y + 2);
            q[2] = (float) (xb + wave), q[3] = (float) (y + 2);
            q[4] = (float) (xb + wave), q[5] = (float) y;
            q[6] = (float) (xa + wave), q[7] = (float) y;
            al = (int) ((ice[k] ? 150.0 : 160.0) * (1.0 - (double) d / (h * 1.15)));
            if (al <= 3)
                continue;
            if (ice[k])
                vulture_gpumap_quad(img, &src, q, 215, 228, 255, al);
            else
                vulture_gpumap_quad(img, &src, q, 170, 205, 240, al);
        }
    }
    /* in the water, not over it: rings from its feet */
    if (wet[0] && !ice[0]) {
        struct monst *m = m_at(j, i);
        bool up = u_at(j, i) ? (Levitation || Flying) : (m && (is_flyer(m->data) || is_floater(m->data)));

        if (!up) {
            double fx = dx + w / 2.0;
            int r;

            for (r = 0; r < 3; r++) {
                double ph = fmod(tt * 0.8 + r / 3.0, 1.0), rad = 6.0 + 26.0 * ph;
                int a = (int) (95.0 * (1.0 - ph)), s;

                for (s = 0; s < 22; s++) {
                    double an = s * 6.2832 / 22.0;

                    vtorch_blob((int) (fx + cos(an) * rad), (int) (feet - 2 + sin(an) * rad * 0.42), 1, 200, 225,
                                255, a);
                }
            }
        }
    }
}

/* probe: s213 - VULTURE_SELFTEST_WATER: a pool in front of you, a giant eel
   in it; shot, and again a moment later (the waves move) */
static Uint32 vrefl_probe_t = 0;
static int vrefl_probe_n = 0;

static void
vrefl_probe_tick(Uint32 now)
{
    static const int nb[8][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 0 }, { 0, 2 }, { 2, 1 }, { 1, 2 }, { 2, 2 } };
    char p[700];

    if (now - vrefl_probe_t < (vrefl_probe_n == 2 ? 300u : 1200u))
        return;
    vrefl_probe_t = SDL_GetTicks();
    if (vrefl_probe_n == 0) {
        int k, made = 0, ex = 0, ey = 0, x, y, bx = 0, by = 0, best = 9999;

        /* first somewhere with open floor in front: all eight squares */
        for (y = 1; y < ROWNO - 1; y++)
            for (x = 2; x < COLNO - 1; x++) {
                int d;

                int va, vb, ahead = 0;

                if (levl[x][y].typ != ROOM || m_at(x, y) || !levl[x][y].lit)
                    continue;
                for (k = 0; k < 8; k++) {
                    int dj, di;

                    v_rot(nb[k][0], nb[k][1], vulture_view_rot, &dj, &di);
                    if (!isok(x + dj, y + di) || levl[x + dj][y + di].typ != ROOM || m_at(x + dj, y + di)
                        || u_at(x + dj, y + di))
                        break;
                }
                if (k < 8)
                    continue;
                for (va = 0; va <= 4; va++) /* how much floor lies in front: the more, the less wall hides */
                    for (vb = 0; vb <= 4; vb++) {
                        int dj, di;

                        v_rot(va, vb, vulture_view_rot, &dj, &di);
                        ahead += isok(x + dj, y + di) && levl[x + dj][y + di].typ == ROOM;
                    }
                d = distmin(u.ux, u.uy, x, y) - 4 * ahead;
                if (d < best)
                    best = d, bx = x, by = y;
            }
        if (bx && (bx != u.ux || by != u.uy)) {
            teleds(bx, by, TELEDS_NO_FLAGS);
            vision_recalc(0);
        }
        for (k = 0; k < 8; k++) {
            int dj, di, x, y;

            v_rot(nb[k][0], nb[k][1], vulture_view_rot, &dj, &di);
            x = u.ux + dj, y = u.uy + di;
            if (!isok(x, y) || levl[x][y].typ != ROOM || m_at(x, y))
                continue;
            levl[x][y].typ = POOL;
            newsym(x, y);
            made++;
            if (k == 2)
                ex = x, ey = y;
        }
        if (ex) {
            struct monst *e = makemon(&mons[PM_GIANT_EEL], ex, ey, NO_MM_FLAGS);

            if (e) {
                e->mundetected = 0, e->msleeping = 1;
                newsym(ex, ey);
            }
        }
        flush_screen(1);
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "water probe: %d pool squares, eel at %d,%d\n", made, ex,
                          ey);
    } else {
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "water_%c.bmp", getenv("VULTURE_SELFTEST"), 'a' + vrefl_probe_n - 1);
        vulture_save_frame_bmp(p);
    }
    if (++vrefl_probe_n >= 3) {
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "water probe: done\n");
        vrefl_probe_t = 0;
    }
}

/*
 * IMPLEMENTATION_LOG s214: floors with a history.  Cosmetic only - nothing
 * to pick up, nothing the rules see: on about one floor square in six (one
 * in eight in corridors) a small decal drawn in the floor pass, under
 * everything that stands there - cracks, pebbles, a puddle, moss, a few
 * old bones - picked from the square, the level and the room's floor
 * style (moss rooms mossy, marble finely cracked, corridors pebbly); not
 * under furniture or a rug.  Procedural, 4 variants a kind, lit and fogged
 * with the floor.
 */
enum { VDC_CRACK, VDC_PEBBLES, VDC_PUDDLE, VDC_MOSS, VDC_BONES, VDC_KINDS };
#define VDC_W 44
#define VDC_H 20

static void
vdc_px(SDL_Surface *s, int x, int y, int r, int g, int b, int a) /* over what is there */
{
    Uint32 *q, c;
    int oa, na;

    if (x < 0 || y < 0 || x >= s->w || y >= s->h || a <= 0)
        return;
    q = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch) + x;
    c = *q, oa = (int) (c >> 24);
    na = a + oa * (255 - a) / 255;
    if (na <= 0)
        return;
    r = (r * a + (int) ((c >> 16) & 255) * oa * (255 - a) / 255) / na;
    g = (g * a + (int) ((c >> 8) & 255) * oa * (255 - a) / 255) / na;
    b = (b * a + (int) (c & 255) * oa * (255 - a) / 255) / na;
    *q = (Uint32) na << 24 | (Uint32) r << 16 | (Uint32) g << 8 | (Uint32) b;
}

static double
vdc_rnd(unsigned *st) /* 0..1 */
{
    *st = *st * 1103515245u + 12345u;
    return ((*st >> 8) & 0xFFFF) / 65536.0;
}

static SDL_Surface *
vdc_make(int kind, int var)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, VDC_W, VDC_H, 32, SDL_PIXELFORMAT_ARGB8888);
    unsigned st = 977u + (unsigned) kind * 7919u + (unsigned) var * 104729u;
    const double cx = VDC_W / 2.0, cy = VDC_H / 2.0;
    int k, n, x, y;

    if (!s)
        return NULL;
    SDL_FillRect(s, NULL, 0);
    SDL_LockSurface(s);
    switch (kind) {
    case VDC_CRACK: { /* a jagged line or two, a lit lip under each */
        int branches = 1 + (int) (vdc_rnd(&st) * 2.0), b2;

        for (b2 = 0; b2 < branches; b2++) {
            double px = cx + (vdc_rnd(&st) - 0.5) * 10.0, py = cy + (vdc_rnd(&st) - 0.5) * 4.0;
            double dir = vdc_rnd(&st) * 6.2832;

            n = 3 + (int) (vdc_rnd(&st) * 3.0);
            for (k = 0; k < n; k++) {
                double len = 3.0 + vdc_rnd(&st) * 5.0, s2;

                dir += (vdc_rnd(&st) - 0.5) * 1.6;
                for (s2 = 0.0; s2 <= len; s2 += 0.5) {
                    x = (int) (px + cos(dir) * s2), y = (int) (py + sin(dir) * s2 * 0.42);
                    vdc_px(s, x, y, 28, 22, 18, 175);
                    vdc_px(s, x, y + 1, 200, 190, 172, 55);
                }
                px += cos(dir) * len, py += sin(dir) * len * 0.42;
            }
        }
        break;
    }
    case VDC_PEBBLES: /* small stones, lit from above left, each with its shadow */
        n = 5 + (int) (vdc_rnd(&st) * 4.0);
        for (k = 0; k < n; k++) {
            double a = vdc_rnd(&st) * 6.2832, d = sqrt(vdc_rnd(&st));
            int sx = (int) (cx + cos(a) * d * 15.0), sy = (int) (cy + sin(a) * d * 6.0);
            int w = 2 + (int) (vdc_rnd(&st) * 2.0), h = 1 + (int) (vdc_rnd(&st) * 2.0);
            int gr = 95 + (int) (vdc_rnd(&st) * 60.0);

            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++)
                    vdc_px(s, sx + x + 1, sy + y + 1, 0, 0, 0, 90);
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    int l = (x == 0 && y == 0) ? 40 : (x == w - 1 || y == h - 1) ? -25 : 0;

                    vdc_px(s, sx + x, sy + y, gr + l + 8, gr + l + 4, gr + l, 235);
                }
        }
        break;
    case VDC_PUDDLE: { /* still water: dark, soft-edged, a sheen on its far edge */
        double rx = 10.0 + vdc_rnd(&st) * 7.0, ry = 4.0 + vdc_rnd(&st) * 2.5;

        for (y = 0; y < VDC_H; y++)
            for (x = 0; x < VDC_W; x++) {
                double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry, wob = 0.12 * sin(x * 0.7 + var * 2.0);
                double r = sqrt(dx * dx + dy * dy) - wob;

                if (r >= 1.0)
                    continue;
                vdc_px(s, x, y, 36, 46, 60, (int) (150.0 * (r > 0.75 ? (1.0 - r) / 0.25 : 1.0)));
                if (r > 0.62 && r < 0.9 && dy < -0.25) /* the sheen */
                    vdc_px(s, x, y, 180, 198, 218, 85);
            }
        break;
    }
    case VDC_MOSS: /* a lumpy patch of green dots */
        n = 34 + (int) (vdc_rnd(&st) * 20.0);
        for (k = 0; k < n; k++) {
            double a = vdc_rnd(&st) * 6.2832, d = pow(vdc_rnd(&st), 0.7);
            int mx = (int) (cx + cos(a) * d * (12.0 + 4.0 * sin(a * 3.0 + var))), my = (int) (cy + sin(a) * d * 5.5);
            int g = 100 + (int) (vdc_rnd(&st) * 55.0), al = 150 + (int) (vdc_rnd(&st) * 70.0);

            vdc_px(s, mx, my, g / 2 + 5, g, g / 3, al);
            if (vdc_rnd(&st) < 0.5)
                vdc_px(s, mx + 1, my, g / 2, g - 15, g / 3, al - 30);
        }
        break;
    case VDC_BONES: /* two or three old bones, knobbed ends, a shadow under */
        n = 2 + (int) (vdc_rnd(&st) * 2.0);
        for (k = 0; k < n; k++) {
            double a = vdc_rnd(&st) * 3.1416, len = 5.0 + vdc_rnd(&st) * 4.0, s2;
            double bx = cx + (vdc_rnd(&st) - 0.5) * 18.0, by = cy + (vdc_rnd(&st) - 0.5) * 6.0;
            double ex = cos(a) * len / 2.0, ey = sin(a) * len / 2.0 * 0.42;

            for (s2 = -1.0; s2 <= 1.0; s2 += 0.1) {
                x = (int) (bx + ex * s2), y = (int) (by + ey * s2);
                vdc_px(s, x + 1, y + 1, 0, 0, 0, 70);
                vdc_px(s, x, y, 208, 200, 184, 235);
            }
            for (y = -1; y <= 0; y++) /* the knobs */
                for (x = -1; x <= 0; x++) {
                    vdc_px(s, (int) (bx + ex) + x, (int) (by + ey) + y, 222, 214, 198, 240);
                    vdc_px(s, (int) (bx - ex) + x, (int) (by - ey) + y, 222, 214, 198, 240);
                }
        }
        break;
    }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    return s;
}

/* step 3 of the floor pass: a decal on (j, i), drawn at (x, y)?  style is
   the room's floor style (get_floor_decor), -1 for none */
static void
vdecal_draw(int j, int i, int x, int y, int style, bool rug, bool furniture)
{
    static SDL_Surface *cache[VDC_KINDS][4];
    /* weights: crack, pebbles, puddle, moss, bones */
    static const double wcob[5] = { 0.35, 0.30, 0.15, 0.10, 0.10 }, wcer[5] = { 0.55, 0.20, 0.25, 0.0, 0.0 };
    static const double wmoss[5] = { 0.10, 0.10, 0.25, 0.55, 0.0 }, wmar[5] = { 0.70, 0.30, 0.0, 0.0, 0.0 };
    static const double wcor[5] = { 0.15, 0.55, 0.20, 0.0, 0.10 };
    const double *w;
    double h, dens, pick;
    int seed = (int) ledger_no(&u.uz) * 131 + 17, kind, var;
    SDL_Surface *s;

    if (!vfx_ambient || style < 0 || rug || furniture)
        return;
    switch (style) {
    case V_FLOOR_COBBLESTONE: case V_FLOOR_DARK: w = wcob, dens = 0.16; break;
    case V_FLOOR_CERAMIC: w = wcer, dens = 0.16; break;
    case V_FLOOR_MOSS_COVERED: w = wmoss, dens = 0.20; break;
    case V_FLOOR_MARBLE: w = wmar, dens = 0.12; break;
    case V_FLOOR_ROUGH: case V_FLOOR_ROUGH_LIT: w = wcor, dens = 0.12; break;
    default: return;
    }
    h = vsp_hash(j, i, seed);
    if (h >= dens && !getenv("VULTURE_DECALS_ALL")) /* probe: every square */
        return;
    pick = vsp_hash(i, j, seed + 3);
    for (kind = 0; kind < VDC_KINDS - 1 && pick >= w[kind]; kind++)
        pick -= w[kind];
    var = (int) (vsp_hash(j + 7, i, seed + 5) * 4.0) & 3;
    if (!cache[kind][var] && !(cache[kind][var] = vdc_make(kind, var)))
        return;
    s = cache[kind][var];
    vulture_put_img(x - VDC_W / 2 + (int) ((vsp_hash(j, i + 3, seed + 9) - 0.5) * 22.0),
                    y - VDC_H / 2 + (int) ((vsp_hash(j + 3, i, seed + 11) - 0.5) * 8.0), s);
}

/*
 * IMPLEMENTATION_LOG s215: a solid rock mass.  The user: "i dont like the
 * lack of ceilings ... like a weird 2d paper diorama and not a 3d model" -
 * chose "Solid rock mass": walls as thick blocks with tops, and the known
 * rock round rooms and corridors capped at wall height.
 *  - Walls are blocks: only the faces turned toward you are drawn (a
 *    wall's north / west faces look away - step 1 draws nothing now), and
 *    every wall gets a stone top at its height.
 *  - The band: every square you have not mapped within VROCK_D of one you
 *    have (map knowledge only - nothing about the level's real rock leaks)
 *    is a rock block with a top, darker the further out (into the dark).
 *    A block shows a rough rock face toward open ground in front of it, or
 *    toward a lower block in front.
 *  - Corridors stay visible: a block in the "reach" of open ground behind
 *    it (a corridor, a doorway, a floor - reach 5 rows, a row less each
 *    step forward, a wall or a full block stops it) is cut down to a low
 *    shelf (VROCK_LOW), so a corridor reads as a trench and a door as a
 *    notch.  A room's band in front of its walls stays full: the wall hid
 *    the floor behind it anyway.
 *  - Tops are flat-normalled (no false relief), see-through round you like
 *    walls, and occlude for the outlines (vxr: a diamond).  The void's dust
 *    is off: the dark is rock now.
 */
/*
 * IMPLEMENTATION_LOG s218: the rock mass, reworked - "you need to fix these
 * ceilings".  In play s215's rock read as a jumble: full-height blocks by
 * default with low shelves cut out wherever open ground lay behind, so a
 * mapped level turned into terraces, towers and brown cliffs; the dark tops
 * in front of a room covered its front walls and floor; and a dark room's
 * unseen floor was drawn as rock round you.
 *  - Heights: the rock is LOW (VROCK_LOW) everywhere by default - one flat
 *    shelf corridors run through as trenches.  It rises to wall height only
 *    behind a wall (a square whose front neighbour is a wall, or rock that
 *    rose, chained back), and only where no open ground lies behind it in
 *    reach - so the mass stands behind a room's back walls, flush with
 *    their tops, and nowhere it could hide what you know.
 *  - A room's front walls keep their full height (s209) and show their own
 *    outside - the room's style, dark (s212) - over the low rock in front.
 *  - The band is rock you know or may take for granted: stone you have
 *    seen, squares beside a corridor, squares outside a wall you have seen
 *    from its room; then on, VROCK_D deep, a square at a time (no corner
 *    cutting), through squares you have not seen - but never a square you
 *    have not seen next to room ground you know (or a doorway): a dark
 *    room's unseen floor is not rock.
 *  - Rock faces are dark like walls' outsides: toward rock, dark; toward
 *    open ground, as lit as it is (the s212 rule).
 * IMPLEMENTATION_LOG s219: one ceiling.  The user: "the walls are drawn
 * above the ceiling" - low rock with walls standing over it read wrong, and
 * mixed heights read as broken.  Style 4 (the default): every block of the
 * band at the walls' height, flush; rock that would hide ground you know
 * (open ground's reach, which walls stop) is see-through - its top alone,
 * faint, no faces.  Tops light themselves (a marker normal; vgl_fs leaves
 * them out of the screen-space light grid, which lit them with the floor
 * behind them on screen), shaded by their own square's light.
 * IMPLEMENTATION_LOG s220: "shadows have ceilings too? very jarring to
 * explore corridors" - the dark ahead of you in a corridor was capped (an
 * unseen square beside a corridor counted as rock - the corridor's way on is
 * one) and opened as you walked.  The band is now rock you have SEEN (stone
 * beside where you walked; all of it once the level is mapped), and the one
 * square just outside a wall you saw from its room (not beside a doorway);
 * deeper only through seen rock.  The unknown stays dark.
 */
#define VROCK_D 3
#define VROCK_LOW 18
static signed char vrock_d[ROWNO][COLNO];     /* band depth: 0 mapped, -1 never rock, 1..VROCK_D, VROCK_D+1 beyond */
static signed char vrock_reach[ROWNO][COLNO]; /* open ground's sight, rows left */
static unsigned char vrock_hh[ROWNO][COLNO];  /* this frame: a block's height (0 none) */

static bool
vrock_open(int b) /* ground you can stand on, as you know it */
{
    return b != V_TILE_WALL_GENERIC && b != V_MISC_UNMAPPED_AREA && b != V_TILE_NONE;
}

/* s218: a square as you know it - 0 nothing mapped (rock you have seen, or
   a square you have never seen), 1 a wall, 2 a corridor, 3 a room's ground
   (floor, furniture, water...), 4 a doorway */
static int
vrock_kind(mapdata *md, int x, int y)
{
    int b;

    if (!isok(x, y))
        return 0;
    b = md->get_glyph(MAP_BACK, x, y);
    if (b == V_MISC_UNMAPPED_AREA || b == V_TILE_NONE)
        return 0;
    if (b == V_TILE_WALL_GENERIC)
        return 1;
    if (b == V_TILE_FLOOR_ROUGH || b == V_TILE_FLOOR_ROUGH_LIT)
        return md->get_glyph(MAP_FURNITURE, x, y) != V_TILE_NONE ? 4 : 2;
    return 3;
}

static int
vrock_wall_h(void) /* the walls' height now: their art's */
{
    vulture_tile *wt = vulture_get_tile(vulture_opts.wall_style == V_WALL_DISPLAY_STYLE_FULL
                                            ? walls_full[V_WALL_BRICK].south
                                            : walls_half[V_WALL_BRICK].south);

    return wt && wt->graphic ? -wt->ymod : 75;
}

static SDL_Surface *
vrock_cap(int var, bool normal, bool dark = false) /* a block's top: the square's diamond, 112 x 44; or its normal map */
{
    static SDL_Surface *c[2][3] = { { NULL, NULL, NULL }, { NULL, NULL, NULL } }, *n = NULL;
    SDL_Surface **slot = normal ? &n : &c[dark ? 1 : 0][var % 3], *s;
    const int W = 2 * V_MAP_XMOD, H = 2 * V_MAP_YMOD;
    int x, y;

    if (*slot)
        return *slot;
    if (!(s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888)))
        return NULL;
    SDL_LockSurface(s);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double dx = (x + 0.5 - W / 2.0) / (W / 2.0), dy = (y + 0.5 - H / 2.0) / (H / 2.0);
            double r = fabs(dx) + fabs(dy), a = r > 1.03 ? 0.0 : 1.0; /* hard, a pixel over: no seams */
            Uint32 px = 0;

            if (a > 0.0) {
                if (normal) {
                    px = 0x00808000u; /* s219: the marker - a top lights itself (vgl_fs) */
                } else {
                    /* low-contrast grain, no rims (rims drew a grid of
                       tiles): the tops run together into one surface */
                    double nz = vsp_hash(x, y, 31 + var * 7), nb = vsp_hash(x / 4, y / 2, 57 + var);
                    int v = (dark ? 13 : 40) + (int) (nz * (dark ? 6.0 : 9.0) + nb * (dark ? 5.0 : 7.0))
                            - (nb < 0.07 ? (dark ? 5 : 10) : 0); /* s219: dark rock */

                    v = v < 0 ? 0 : v > 255 ? 255 : v;
                    px = (Uint32) (v + 5) << 16 | (Uint32) (v + 2) << 8 | (Uint32) v;
                }
                px |= (Uint32) (a * 255.0) << 24;
            }
            ((Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch))[x] = px;
        }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    *slot = s;
    return s;
}

/*
 * IMPLEMENTATION_LOG s225: the ceiling in its wall's stone.  The user: "make
 * the ceiling color actually match the wall".  A top is its wall style's own
 * face art laid flat on the square's diamond (along the wall, and into it) -
 * a brick wall's top is brick, marble's marble, vines' vines - and the rock
 * of the band takes the nearest wall's stone (beside a corridor, the
 * corridor's dark rock).  It is lit as that wall is (its light, through the
 * light grid's own darkness curve), so in a lit room it matches the lit
 * face and in the fog it is as dim as the fogged wall.
 */
static signed char vtop_style[ROWNO][COLNO]; /* the stone each top is (-1: the plain grain) */
static float vtop_light[ROWNO][COLNO];       /* the light it is lit by */

static SDL_Surface *
vrock_top_tex(int style)
{
    static SDL_Surface *c[V_WALL_STYLE_MAX][2];
    bool full = vulture_opts.wall_style == V_WALL_DISPLAY_STYLE_FULL;
    const int W = 2 * V_MAP_XMOD, H = 2 * V_MAP_YMOD;
    vulture_tile *wt;
    SDL_Surface *src, *s;
    double ar = 0, ag = 0, ab = 0, an = 0;
    int x, y, fh;

    if (style < 0 || style >= V_WALL_STYLE_MAX)
        return NULL;
    if (c[style][full ? 0 : 1])
        return c[style][full ? 0 : 1];
    wt = vulture_get_tile(full ? walls_full[style].south : walls_half[style].south);
    if (!wt || !wt->graphic || !(src = SDL_ConvertSurfaceFormat(wt->graphic, SDL_PIXELFORMAT_ARGB8888, 0)))
        return NULL;
    if (!(s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888))) {
        SDL_FreeSurface(src);
        return NULL;
    }
    fh = -wt->ymod; /* the face's height (75 full, 43 half) */
    SDL_LockSurface(src);
    SDL_LockSurface(s);
    /* s226: the mean colour of each row down the face (the face is skewed:
       column ax starts at 22 ax / 56) - its shading top to bottom, divided
       out below (laid flat, it striped every top) */
    double rowm[128][4];

    memset(rowm, 0, sizeof rowm);
    for (x = 0; x < src->w; x++)
        for (y = 0; y < src->h; y++) {
            Uint32 p = ((Uint32 *) ((Uint8 *) src->pixels + (size_t) y * src->pitch))[x];
            int r = y - (int) (22.0 * x / 56.0);

            if ((p >> 24) < 200 || r < 0 || r >= 128)
                continue;
            rowm[r][0] += (p >> 16) & 255, rowm[r][1] += (p >> 8) & 255, rowm[r][2] += p & 255, rowm[r][3]++;
        }
    for (y = 0; y < 128; y++)
        if (rowm[y][3] > 0)
            rowm[y][0] /= rowm[y][3], rowm[y][1] /= rowm[y][3], rowm[y][2] /= rowm[y][3];
    for (y = 0; y < src->h; y++) /* the face's mean colour: for any hole */
        for (x = 0; x < src->w; x++) {
            Uint32 p = ((Uint32 *) ((Uint8 *) src->pixels + (size_t) y * src->pitch))[x];

            if ((p >> 24) >= 200)
                ar += (p >> 16) & 255, ag += (p >> 8) & 255, ab += p & 255, an++;
        }
    if (an > 0)
        ar /= an, ag /= an, ab /= an;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double u = (x + 0.5 - W / 2.0) / (W / 2.0), v = (y + 0.5 - H / 2.0) / (H / 2.0);
            double a = (1.0 + u - v) / 2.0, b = (1.0 + u + v) / 2.0; /* into the wall, along it */
            Uint32 px = 0;

            if (fabs(u) + fabs(v) <= 1.03) {
                int ax, ay;
                Uint32 q;

                a = a < 0.0 ? 0.0 : a > 1.0 ? 1.0 : a, b = b < 0.0 ? 0.0 : b > 1.0 ? 1.0 : b;
                ax = (int) (b * (src->w - 2));
                ay = (int) (22.0 * ax / 56.0 + 4.0 + a * (fh - 10));
                ax = ax < 0 ? 0 : ax >= src->w ? src->w - 1 : ax;
                ay = ay < 0 ? 0 : ay >= src->h ? src->h - 1 : ay;
                q = ((Uint32 *) ((Uint8 *) src->pixels + (size_t) ay * src->pitch))[ax];
                if ((q >> 24) < 128) { /* off the face: its mean colour */
                    q = (Uint32) ar << 16 | (Uint32) ag << 8 | (Uint32) ab;
                } else { /* s226: this row's shading out, the face's mean in */
                    int rr = ay - (int) (22.0 * ax / 56.0), ch;
                    double cc[3] = { (double) ((q >> 16) & 255), (double) ((q >> 8) & 255), (double) (q & 255) },
                           mm[3] = { ar, ag, ab };

                    if (rr >= 0 && rr < 128 && rowm[rr][3] > 3)
                        for (ch = 0; ch < 3; ch++) {
                            double f = rowm[rr][ch] > 4.0 ? mm[ch] / rowm[rr][ch] : 1.0;

                            f = f < 0.5 ? 0.5 : f > 2.0 ? 2.0 : f;
                            cc[ch] = cc[ch] * f > 255.0 ? 255.0 : cc[ch] * f;
                        }
                    q = (Uint32) cc[0] << 16 | (Uint32) cc[1] << 8 | (Uint32) cc[2];
                }
                px = 0xFF000000u | (q & 0x00FFFFFFu);
            }
            ((Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch))[x] = px;
        }
    SDL_UnlockSurface(s);
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    c[style][full ? 0 : 1] = s;
    return s;
}

static int vrock_style_of(int tile); /* s218, below */

/* s225: the stone a wall square is built of (from its tiles, else a neighbour's) */
static int
vrock_wall_stone(struct walls (*mw)[COLNO], int j, int i)
{
    static const int d8[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    int st = -1, k, n;

    for (n = 0; n < 9 && st < 0; n++) {
        int x = n ? j + d8[n - 1][0] : j, y = n ? i + d8[n - 1][1] : i;

        if (!isok(x, y))
            continue;
        for (k = 0; k < 4 && st < 0; k++)
            st = vrock_style_of(k == 0 ? mw[y][x].north : k == 1 ? mw[y][x].west : k == 2 ? mw[y][x].south
                                                                                         : mw[y][x].east);
    }
    return st;
}

/* s225: once a frame, after the band: each top's stone and light - a wall
   its own, the band the nearest wall's (a corridor's: dark rock) */
static void
vtop_prepare(mapdata *md, struct walls (*mw)[COLNO])
{
    static short q[ROWNO * COLNO][2];
    int qh = 0, qt = 0, x, y, k, pass;

    memset(vtop_style, -1, sizeof vtop_style);
    memset(vtop_light, 0, sizeof vtop_light);
    if (!md || !vrock_on || !mw)
        return;
    for (pass = 0; pass < 2; pass++) /* walls first: they win a tie */
        for (y = 0; y < ROWNO; y++)
            for (x = 1; x < COLNO; x++) {
                int kd = vrock_kind(md, x, y);

                if (pass == 0 && kd == 1) {
                    int st = vrock_wall_stone(mw, x, y);

                    vtop_style[y][x] = (signed char) (st >= 0 ? st : V_WALL_DARK);
                } else if (pass == 1 && (kd == 2 || kd == 4)) {
                    vtop_style[y][x] = (signed char) V_WALL_DARK;
                } else {
                    continue;
                }
                vtop_light[y][x] = vl_light_at(x, y);
                q[qt][0] = (short) x, q[qt][1] = (short) y, qt++;
            }
    while (qh < qt) {
        static const int d4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        int cx = q[qh][0], cy = q[qh][1];

        qh++;
        for (k = 0; k < 4; k++) {
            int nx = cx + d4[k][0], ny = cy + d4[k][1];

            if (isok(nx, ny) && vtop_style[ny][nx] < 0 && vrock_d[ny][nx] >= 1 && vrock_d[ny][nx] <= VROCK_D) {
                vtop_style[ny][nx] = vtop_style[cy][cx], vtop_light[ny][nx] = vtop_light[cy][cx];
                q[qt][0] = (short) nx, q[qt][1] = (short) ny, qt++;
            }
        }
    }
}

/* before the second pass: the band, and each block's height */
static void
vrock_prepare(mapdata *md, int vx, int vy, int tr, int diff, int sum)
{
    static unsigned char kind[ROWNO][COLNO], wroom[ROWNO][COLNO];
    static short q[ROWNO * COLNO][2];
    static const int d8[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
    int x, y, ii, jj, k, qh = 0, qt = 0, hw = vrock_wall_h();

    memset(vrock_hh, 0, sizeof vrock_hh);
    memset(vrock_reach, 0, sizeof vrock_reach);
    if (!vrock_on || !md)
        return;
    {
        static bool envd = false;

        if (!envd) { /* probe / testing: VULTURE_ROCK_STYLE=n */
            envd = true;
            if (getenv("VULTURE_ROCK_STYLE"))
                vrock_style = atoi(getenv("VULTURE_ROCK_STYLE"));
        }
    }
    for (y = 0; y < ROWNO; y++)
        for (x = 0; x < COLNO; x++)
            kind[y][x] = (unsigned char) (x >= 1 ? vrock_kind(md, x, y) : 0);
    for (y = 0; y < ROWNO; y++) /* walls you have seen from their room */
        for (x = 0; x < COLNO; x++) {
            wroom[y][x] = 0;
            if (kind[y][x] == 1)
                for (k = 0; k < 8; k++)
                    if (isok(x + d8[k][0], y + d8[k][1]) && kind[y + d8[k][1]][x + d8[k][0]] == 3)
                        wroom[y][x] = 1;
        }
    /* the band's first ring: rock you know, or may take for granted */
    for (y = 0; y < ROWNO; y++)
        for (x = 0; x < COLNO; x++) {
            bool seen, seed = false, room = false, door = false;

            if (x < 1 || kind[y][x]) {
                vrock_d[y][x] = 0;
                continue;
            }
            vrock_d[y][x] = VROCK_D + 1;
            seen = svl.level.locations[x][y].seenv != 0; /* stone you have seen */
            for (k = 0; k < 8; k++) {
                int nx = x + d8[k][0], ny = y + d8[k][1], kk;

                if (!isok(nx, ny))
                    continue;
                kk = kind[ny][nx];
                /* s220: rock you have seen; or just outside a wall you saw from
                   its room.  NOT an unseen square beside a corridor - the
                   corridor's own way on is one: the dark ahead got a ceiling
                   that opened as you walked */
                if ((seen && kk) || (!seen && kk == 1 && wroom[ny][nx]))
                    seed = true;
                room = room || kk == 3;
                door = door || kk == 4;
            }
            if (!seen && (room || door)) { /* s220: nor beside a doorway - where its corridor starts */
                vrock_d[y][x] = -1; /* beside room ground you know: maybe floor, never rock */
                continue;
            }
            if (seed && vrock_style != 2) { /* s219: walls only - no band */
                vrock_d[y][x] = 1;
                q[qt][0] = (short) x, q[qt][1] = (short) y, qt++;
            }
        }
    /* and on, a step at a time, through what you have not mapped */
    while (qh < qt) {
        static const int d4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        int cx = q[qh][0], cy = q[qh][1], d = vrock_d[cy][cx];

        qh++;
        if (d >= VROCK_D)
            continue;
        for (k = 0; k < 4; k++) {
            int nx = cx + d4[k][0], ny = cy + d4[k][1];

            if (isok(nx, ny) && vrock_d[ny][nx] == VROCK_D + 1
                && svl.level.locations[nx][ny].seenv) { /* s220: only rock you have seen */
                vrock_d[ny][nx] = (signed char) (d + 1);
                q[qt][0] = (short) nx, q[qt][1] = (short) ny, qt++;
            }
        }
    }
    /* in drawing order (a square's back neighbours first): how far open
       ground behind sees forward; every block low for now */
    for (ii = -tr; ii <= tr; ii++)
        for (jj = diff + ii; jj + ii <= sum; jj++) {
            int dj, di, j, i, b, r = -9;
            static const int bk[3][3] = { { -1, 0, 1 }, { 0, -1, 1 }, { -1, -1, 2 } }; /* behind: UL, UR, up */

            v_rot(jj, ii, vulture_view_rot, &dj, &di);
            j = vx + dj, i = vy + di;
            if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO)
                continue;
            b = md->get_glyph(MAP_BACK, j, i);
            if (vrock_open(b)) {
                vrock_reach[i][j] = 5;
                continue;
            }
            if (b == V_TILE_WALL_GENERIC) {
                vrock_reach[i][j] = 0, vrock_hh[i][j] = (unsigned char) hw;
                continue;
            }
            if (vrock_d[i][j] < 1 || vrock_d[i][j] > VROCK_D)
                continue;
            for (k = 0; k < 3; k++) {
                int oj, oi, nj, ni;

                v_rot(bk[k][0], bk[k][1], vulture_view_rot, &oj, &oi);
                nj = j + oj, ni = i + oi;
                if (isok(nj, ni) && vrock_reach[ni][nj] - bk[k][2] > r)
                    r = vrock_reach[ni][nj] - bk[k][2];
            }
            vrock_reach[i][j] = (signed char) (r >= 1 ? r : 0);
            vrock_hh[i][j] = vrock_style >= 3 ? (unsigned char) hw : VROCK_LOW; /* s219: one ceiling */
        }
    /* front to back: rock behind a wall (or behind rock that rose) rises to
       the walls' height - unless open ground behind it would be hidden */
    for (ii = tr; ii >= -tr; ii--)
        for (jj = sum - ii; jj >= diff + ii; jj--) {
            int dj, di, j, i;

            v_rot(jj, ii, vulture_view_rot, &dj, &di);
            j = vx + dj, i = vy + di;
            if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO || vrock_hh[i][j] != VROCK_LOW || vrock_reach[i][j] >= 1)
                continue;
            for (k = 0; k < 2; k++) { /* its front neighbours: screen down-right, down-left */
                int oj, oi, nj, ni;

                v_rot(k ? 0 : 1, k ? 1 : 0, vulture_view_rot, &oj, &oi);
                nj = j + oj, ni = i + oi;
                if (isok(nj, ni) && vrock_hh[ni][nj] == hw) {
                    vrock_hh[i][j] = (unsigned char) hw;
                    break;
                }
            }
        }
}

/* a top's far edges, 2 px lines: e 0 up-left, 1 up-right - the lip where
   the rock drops away behind it into a room or a corridor */
static SDL_Surface *
vrock_lip(int e)
{
    static SDL_Surface *l[2] = { NULL, NULL };
    const int W = 2 * V_MAP_XMOD, H = 2 * V_MAP_YMOD;
    int x, y;

    if (l[e])
        return l[e];
    if (!(l[e] = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888)))
        return NULL;
    SDL_LockSurface(l[e]);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double dx = (x + 0.5 - W / 2.0) / (W / 2.0), dy = (y + 0.5 - H / 2.0) / (H / 2.0);
            double r = fabs(dx) + fabs(dy), a = 0.0;

            if (dy < 0.0 && (e ? dx > 0.0 : dx < 0.0) && r < 1.0 && r > 0.9)
                a = (r - 0.9) / 0.1;
            ((Uint32 *) ((Uint8 *) l[e]->pixels + (size_t) y * l[e]->pitch))[x] =
                (Uint32) (a * 190.0) << 24 | 0x00A69E94u;
        }
    SDL_UnlockSurface(l[e]);
    SDL_SetSurfaceBlendMode(l[e], SDL_BLENDMODE_BLEND);
    return l[e];
}

/* s219: a top's soft edge toward side e (0 screen up-left, 1 up-right, 2
   down-right, 3 down-left): black, nothing at the far side, deep at that
   edge - where the rock you know ends, it fades into the dark */
static SDL_Surface *
vrock_edge(int e)
{
    static SDL_Surface *s[4] = { NULL, NULL, NULL, NULL };
    static const int su[4] = { -1, 1, 1, -1 }, sv[4] = { -1, -1, 1, 1 };
    const int W = 2 * V_MAP_XMOD, H = 2 * V_MAP_YMOD;
    int x, y;

    if (s[e])
        return s[e];
    if (!(s[e] = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888)))
        return NULL;
    SDL_LockSurface(s[e]);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double u = (x + 0.5 - W / 2.0) / (W / 2.0), v = (y + 0.5 - H / 2.0) / (H / 2.0);
            double p = (su[e] * u + sv[e] * v + 1.0) / 2.0, a = 0.0;

            if (fabs(u) + fabs(v) <= 1.03 && p > 0.0)
                a = 235.0 * pow(p > 1.0 ? 1.0 : p, 1.4);
            ((Uint32 *) ((Uint8 *) s[e]->pixels + (size_t) y * s[e]->pitch))[x] = (Uint32) (a + 0.5) << 24;
        }
    SDL_UnlockSurface(s[e]);
    SDL_SetSurfaceBlendMode(s[e], SDL_BLENDMODE_BLEND);
    return s[e];
}

/* a block's top at height h over the square drawn at (x, y), shaded sh
   (255 full), see-through round you by f; it occludes for the outlines.
   lips: bit 0 its up-left edge, bit 1 its up-right edge drop away */
static void
vrock_cap_put(int x, int y, int h, int sh, int var, float f, int lips = 0, bool dark = false, int ghost = 0,
              int edges = 0, int stone = -1)
{
    SDL_Surface *c = stone >= 0 ? vrock_top_tex(stone) : NULL, *nm = ghost ? NULL : vrock_cap(0, true);

    if (!c) /* s225: its wall's stone - or the plain grain */
        c = vrock_cap(var, false, dark);
    int cx0 = x - V_MAP_XMOD, cy0 = y - V_MAP_YMOD - h, amin = 255;
    vxr_face_t fc;

    if (!c)
        return;
    SDL_SetSurfaceColorMod(c, (Uint8) sh, (Uint8) sh, (Uint8) sh);
    SDL_SetSurfaceAlphaMod(c, (Uint8) (ghost ? ghost : 255)); /* s219: see-through */
    if (f > 0.0f && vwall_hole_on && vwall_amin < 250 && vulture_gpumap_on && !ghost) { /* s224: not a see-through top */
        float hx = (float) vwall_hx, hy = (float) (vwall_hy - VWALL_BODY);
        float qx = hx < cx0 ? cx0 : hx > cx0 + c->w ? cx0 + c->w : hx;
        float qy = hy < cy0 ? cy0 : hy > cy0 + c->h ? cy0 + c->h : hy;

        if ((qx - hx) * (qx - hx) + (qy - hy) * (qy - hy) < VWALL_R1 * VWALL_R1) {
            f = f >= 1.0f ? 1.0f : f * f * (3.0f - 2.0f * f);
            amin = (int) (255.0f - (255.0f - vwall_amin) * f + 0.5f);
        }
    }
    if (amin < 255) {
        vulture_gpumap_holed(c, cx0, cy0, (float) vwall_hx, (float) (vwall_hy - VWALL_BODY), VWALL_R0, VWALL_R1,
                             amin);
        if (vulture_normal_capture && nm)
            vulture_gpumap_normal_holed(nm, cx0, cy0, (float) vwall_hx, (float) (vwall_hy - VWALL_BODY), VWALL_R0,
                                        VWALL_R1, amin);
    } else {
        vulture_put_img(cx0, cy0, c);
        if (vulture_normal_capture && nm && vulture_gpumap_on)
            vulture_gpumap_normal(nm, cx0, cy0);
    }
    SDL_SetSurfaceColorMod(c, 255, 255, 255);
    SDL_SetSurfaceAlphaMod(c, 255);
    for (int e = 0; e < 4; e++) { /* s219: the soft edges, into the dark */
        SDL_Surface *es = (edges >> e) & 1 ? vrock_edge(e) : NULL;

        if (!es)
            continue;
        SDL_SetSurfaceAlphaMod(es, (Uint8) (ghost ? ghost : 255));
        if (amin < 255)
            vulture_gpumap_holed(es, cx0, cy0, (float) vwall_hx, (float) (vwall_hy - VWALL_BODY), VWALL_R0,
                                 VWALL_R1, amin);
        else
            vulture_put_img(cx0, cy0, es);
        SDL_SetSurfaceAlphaMod(es, 255);
    }
    for (int e = 0; e < 2 && !ghost; e++) { /* the lips (s224: none on a see-through top - a grid over the floor) */
        SDL_Surface *ls = (lips >> e) & 1 ? vrock_lip(e) : NULL;

        if (!ls)
            continue;
        SDL_SetSurfaceColorMod(ls, (Uint8) sh, (Uint8) sh, (Uint8) sh);
        SDL_SetSurfaceAlphaMod(ls, (Uint8) (ghost ? ghost : 255));
        if (amin < 255)
            vulture_gpumap_holed(ls, cx0, cy0, (float) vwall_hx, (float) (vwall_hy - VWALL_BODY), VWALL_R0,
                                 VWALL_R1, amin);
        else
            vulture_put_img(cx0, cy0, ls);
        SDL_SetSurfaceColorMod(ls, 255, 255, 255);
        SDL_SetSurfaceAlphaMod(ls, 255);
    }
    fc.x0 = (float) x, fc.y0 = (float) (y - h), fc.k = 0.0f, fc.h = 0;
    fc.seq = ++vxr_seq, fc.amin = amin, fc.cap = true;
    fc.cut = ghost ? ghost : 255; /* s223: see-through rock or a see-through wall's top */
    vxr_faces.push_back(fc);
}

/* the foot of a rough rock face, low high: the bottom of its art; s218:
   darkened by dim (black over it) like a wall's outside */
static void
vrock_low_face(int x, int y, int tile, int low, int sh, int dim = 0)
{
    vulture_tile *t = vulture_get_tile(tile);
    SDL_Rect src;
    int keep, pass;

    if (!t || !t->graphic)
        return;
    keep = low + V_MAP_YMOD + 1;
    if (keep > t->graphic->h)
        keep = t->graphic->h;
    src.x = 0, src.y = t->graphic->h - keep, src.w = t->graphic->w, src.h = keep;
    for (pass = 0; pass < (dim > 0 ? 2 : 1); pass++) {
        Uint8 c = (Uint8) (pass ? 0 : sh);

        SDL_SetSurfaceColorMod(t->graphic, c, c, c);
        SDL_SetSurfaceAlphaMod(t->graphic, (Uint8) (pass ? dim : 255));
        if (vulture_gpumap_on) {
            vulture_gpumap_blit(t->graphic, &src, x + t->xmod, y + t->ymod + src.y);
        } else {
            SDL_Rect d = { (Sint16) (x + t->xmod), (Sint16) (y + t->ymod + src.y), (Uint16) src.w, (Uint16) src.h };

            SDL_BlitSurface(t->graphic, &src, vulture_screen, &d);
        }
    }
    SDL_SetSurfaceColorMod(t->graphic, 255, 255, 255);
    SDL_SetSurfaceAlphaMod(t->graphic, 255);
}

/* s218: the light outside a block's face - toward rock (or anything not
   known to be open) dark; toward open ground, as lit as it (s212's rule) */
static int
vrock_face_dim(int nj, int ni, int nb)
{
    float lo;
    int d;

    if (!isok(nj, ni) || !vrock_open(nb))
        return VWALL_DIM;
    lo = vl_light_at(nj, ni) / 0.7f; /* VL_LIT: a lit room in sight */
    lo = lo > 1.0f ? 1.0f : lo < 0.0f ? 0.0f : lo;
    d = (int) (255.0f * (1.0f - lo)); /* the face no brighter than that ground */
    return d > VWALL_DIM ? VWALL_DIM : d;
}

/* s218: a wall's face toward open ground (step 7) - toward a room's ground
   it is that room's wall, lit by it; toward a corridor or a doorway it is
   the wall's OUTSIDE, as lit as what it faces */
static int
vrock_out_dim(mapdata *md, int nj, int ni)
{
    int k;

    if (!vrock_on || !md || !isok(nj, ni))
        return 0;
    k = vrock_kind(md, nj, ni);
    return k == 2 || k == 4 ? vrock_face_dim(nj, ni, md->get_glyph(MAP_BACK, nj, ni)) : 0;
}

/* s218: the wall style a wall tile belongs to (a decorated brick wall's
   outside is plain brick - the decorations hang inside); -1 none */
static int
vrock_style_of(int tile)
{
    int k;

    if (tile < 0 || tile == V_TILE_NONE)
        return -1;
    for (k = 0; k < V_WALL_STYLE_MAX; k++)
        if (walls_full[k].north == tile || walls_full[k].west == tile || walls_full[k].south == tile
            || walls_full[k].east == tile || walls_half[k].north == tile || walls_half[k].west == tile
            || walls_half[k].south == tile || walls_half[k].east == tile)
            return k > V_WALL_BRICK && k <= V_WALL_BRICK + 4 ? V_WALL_BRICK : k;
    return -1;
}

/* s218: a wall's outside on its screen-south face (side 0) or screen-east
   face (side 1), in its room's style: the style of the face its room sees
   (its north / west tile), else a neighbour's along the wall, else any face
   it has; -1 none */
static int
vrock_wall_face(struct walls (*mw)[COLNO], int j, int i, int side)
{
    int st = vrock_style_of(side ? mw[i][j].west : mw[i][j].north), k;
    bool full = vulture_opts.wall_style == V_WALL_DISPLAY_STYLE_FULL;

    for (k = 0; k < 4 && st < 0; k++) {
        int oj, oi, s = (k & 1) ? 1 : -1, n = k / 2 + 1;

        v_rot(side ? 0 : s * n, side ? s * n : 0, vulture_view_rot, &oj, &oi);
        if (isok(j + oj, i + oi))
            st = vrock_style_of(side ? mw[i + oi][j + oj].west : mw[i + oi][j + oj].north);
    }
    if (st < 0)
        st = vrock_style_of(mw[i][j].south);
    if (st < 0)
        st = vrock_style_of(mw[i][j].east);
    if (st < 0)
        return -1;
    return side ? (full ? walls_full[st].east : walls_half[st].east)
                : (full ? walls_full[st].south : walls_half[st].south);
}

/*
 * IMPLEMENTATION_LOG s223: the room you are in shows whole.  Inside a room,
 * its front walls (the ring squares with the room's floor behind them on
 * the screen) and its doors there turn see-through - all of them, at the
 * circle's wall_opacity, not only round you - and the rock in front of
 * them that would hide the room's floor goes see-through like the rock
 * over a corridor.  Fades in as you enter (250 ms) and out as you leave.
 * wall_opacity 1.0 (circle off) leaves it off; VULTURE_NO_ROOMCUT too.
 */
static Uint8 vcut_a[ROWNO][COLNO];   /* per square: 255 solid, less see-through */
static Uint8 vcut_r[ROWNO][COLNO];   /* band rock over the room: 255 no, less the see-through amount */
static int vcut_room = -1, vcut_prev = -1;
static bool vcut_probe_off = false; /* probe: the same view, no room cut */
static Uint32 vcut_t_in = 0, vcut_t_out = 0;

static bool
vcut_busy(Uint32 now)
{
    return (vcut_room >= 0 && now - vcut_t_in < 300) || (vcut_prev >= 0 && now - vcut_t_out < 300);
}

static void
vcut_mark(mapdata *md, int room, double kk, int vx, int vy, int tr, int diff, int sum)
{
    static signed char rr[ROWNO][COLNO];
    static const int bk[3][3] = { { -1, 0, 1 }, { 0, -1, 1 }, { -1, -1, 2 } }; /* behind: UL, UR, up */
    int ii, jj, k, a = 255 - (int) ((255 - vwall_amin) * kk + 0.5), ra = 255 - (int) ((255 - 45) * kk + 0.5); /* s224: 45 */
    struct mkroom *mr;

    if (room < 0 || kk <= 0.0)
        return;
    mr = &svr.rooms[room];
    memset(rr, 0, sizeof rr);
    for (ii = -tr; ii <= tr; ii++) /* in drawing order: the room's floor seen forward */
        for (jj = diff + ii; jj + ii <= sum; jj++) {
            int dj, di, j, i, b, r = -9;
            bool ring;

            v_rot(jj, ii, vulture_view_rot, &dj, &di);
            j = vx + dj, i = vy + di;
            if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO)
                continue;
            b = md->get_glyph(MAP_BACK, j, i);
            if (levl[j][i].roomno == room + ROOMOFFSET && vrock_open(b)) {
                rr[i][j] = 5;
                continue;
            }
            for (k = 0; k < 3; k++) {
                int oj, oi, nj, ni;

                v_rot(bk[k][0], bk[k][1], vulture_view_rot, &oj, &oi);
                nj = j + oj, ni = i + oi;
                if (isok(nj, ni) && rr[ni][nj] - bk[k][2] > r)
                    r = rr[ni][nj] - bk[k][2];
            }
            rr[i][j] = (signed char) (r > 0 ? r : 0);
            ring = j >= mr->lx - 1 && j <= mr->hx + 1 && i >= mr->ly - 1 && i <= mr->hy + 1;
            if (ring && r >= 3 && (b == V_TILE_WALL_GENERIC || IS_DOOR(levl[j][i].typ))) /* a front wall, a door */
                vcut_a[i][j] = (Uint8) (a < vcut_a[i][j] ? a : vcut_a[i][j]);
            else if (r >= 1 && b != V_TILE_WALL_GENERIC && vrock_hh[i][j])
                vcut_r[i][j] = (Uint8) (ra < vcut_r[i][j] ? ra : vcut_r[i][j]);
        }
}

/* once a frame, after the circle's settings: what of your room goes see-through */
static void
vcut_prepare(mapdata *md, int vx, int vy, int tr, int diff, int sum)
{
    Uint32 now = SDL_GetTicks();
    int room = -1;
    double kin, kout;

    memset(vcut_a, 255, sizeof vcut_a);
    memset(vcut_r, 255, sizeof vcut_r);
    vcut_now = 255;
    if (md && u.ux && !u.uswallow && vwall_hole_on && vwall_amin < 250 && !getenv("VULTURE_NO_ROOMCUT") && !vcut_probe_off) {
        int rn = levl[u.ux][u.uy].roomno, k;

        /* IMPLEMENTATION_LOG s224: standing in a doorway you are still at
           that room - the room beside the door, not none (the walls faded
           back in and out at every door you went through) */
        if (rn < ROOMOFFSET && IS_DOOR(levl[u.ux][u.uy].typ))
            for (k = 0; k < 4 && rn < ROOMOFFSET; k++) {
                static const int ddx[4] = { 1, -1, 0, 0 }, ddy[4] = { 0, 0, 1, -1 };
                int nx = u.ux + ddx[k], ny = u.uy + ddy[k];

                if (isok(nx, ny) && levl[nx][ny].roomno >= ROOMOFFSET
                    && md->get_glyph(MAP_BACK, nx, ny) != V_MISC_UNMAPPED_AREA)
                    rn = levl[nx][ny].roomno;
            }
        if (rn >= ROOMOFFSET && rn - ROOMOFFSET < (MAXNROFROOMS + 1) * 2)
            room = rn - ROOMOFFSET;
    }
    if (room != vcut_room) {
        if (vcut_room >= 0)
            vcut_prev = vcut_room, vcut_t_out = now;
        vcut_room = room, vcut_t_in = now;
    }
    if (vcut_probe_off) /* probe: no fade either way */
        vcut_room = -1, vcut_prev = -1;
    kin = vcut_room >= 0 ? (now - vcut_t_in) / 250.0 : 0.0;
    kin = kin > 1.0 ? 1.0 : kin, kin = kin * kin * (3.0 - 2.0 * kin);
    kout = vcut_prev >= 0 ? 1.0 - (now - vcut_t_out) / 250.0 : 0.0;
    if (kout <= 0.0)
        vcut_prev = -1, kout = 0.0;
    kout = kout * kout * (3.0 - 2.0 * kout);
    if (vcut_prev == vcut_room)
        vcut_prev = -1;
    vcut_mark(md, vcut_room, kin, vx, vy, tr, diff, sum);
    vcut_mark(md, vcut_prev, kout, vx, vy, tr, diff, sum);
}

/* s219: is the block at (j, i) see-through (style 4: band rock open
   ground behind would be hidden by) - to its neighbours it is not there */
static bool
vrock_ghost_at(mapdata *md, int j, int i)
{
    int d;

    if (vrock_style != 4 || !isok(j, i) || !vrock_hh[i][j] || md->get_glyph(MAP_BACK, j, i) == V_TILE_WALL_GENERIC)
        return false;
    d = vrock_d[i][j];
    return d >= 1 && d <= VROCK_D && (vrock_reach[i][j] >= 1 || vcut_r[i][j] < 168); /* s223: over your room */
}

/* end of a square in the second pass: its block - faces toward what lies
   lower in front, and its top */
static void
vrock_draw(mapdata *md, int j, int i, int x, int y, int sj, int si, int ej, int ei, struct walls (*mw)[COLNO])
{
    static const int fadeby[VROCK_D + 1] = { 255, 255, 255, 255 }; /* s219: one shade - soft edges instead */
    int h, b, sh, side, hw, dd, ghost;

    if (!vrock_on || !md || !isok(j, i) || !(h = vrock_hh[i][j]))
        return;
    b = md->get_glyph(MAP_BACK, j, i);
    if (b != V_TILE_WALL_GENERIC) { /* s224: a lone block (no rock, no wall beside it) floats like a loose tile */
        static const int n4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        int k4, nn = 0;

        for (k4 = 0; k4 < 4; k4++)
            if (isok(j + n4[k4][0], i + n4[k4][1]) && vrock_hh[i + n4[k4][1]][j + n4[k4][0]])
                nn++;
        if (!nn)
            return;
    }
    hw = vrock_wall_h();
    dd = vrock_d[i][j] < 0 ? 0 : vrock_d[i][j] > VROCK_D ? VROCK_D : vrock_d[i][j];
    sh = b == V_TILE_WALL_GENERIC ? 255 : fadeby[dd];
    ghost = vrock_style == 4 && b != V_TILE_WALL_GENERIC && vrock_reach[i][j] >= 1 ? 80 : 0; /* s219 */
    if (!ghost && b != V_TILE_WALL_GENERIC && vcut_r[i][j] < 168) /* s223: over the room you are in */
        ghost = vcut_r[i][j];
    if (b == V_TILE_WALL_GENERIC && vcut_now < 255) /* s223: a front wall of your room: its top too */
        ghost = vcut_now;
    for (side = 0; side < 2; side++) { /* screen-south (side 2), screen-east (side 3) */
        int nj = side ? ej : sj, ni = side ? ei : si, nb, nh, face, dim;
        bool out = !isok(nj, ni);

        nb = out ? V_MISC_UNMAPPED_AREA : md->get_glyph(MAP_BACK, nj, ni);
        nh = out ? 0 : vrock_hh[ni][nj];
        if (!out && vrock_ghost_at(md, nj, ni)) /* s219: see-through in front - this face shows */
            nh = 0;
        dim = vrock_face_dim(nj, ni, nb);
        {
            int rs = vrock_style == 1 || vrock_style == 4 ? V_WALL_DARK : V_WALL_ROUGH; /* s219: dark rock */

            face = side ? walls_full[rs].east : walls_full[rs].south;
            if (vulture_opts.wall_style != V_WALL_DISPLAY_STYLE_FULL)
                face = side ? walls_half[rs].east : walls_half[rs].south;
        }
        if (b == V_TILE_WALL_GENERIC) {
            /* a wall: its faces toward open ground come from get_wall_tiles;
               s218: toward anything lower that is not open ground (the low
               rock in front, the dark, the map's edge), its own outside -
               its room's style, dark */
            if (out || (!vrock_open(nb) && nh < h)) {
                int own = mw ? vrock_wall_face(mw, j, i, side) : -1;

                vwall_draw(x, y, own >= 0 ? own : face, side ? 3 : 2, 0, -1, -1, VWALL_DIM);
            }
            continue;
        }
        if (ghost) /* s219: see-through rock has no faces - only its top, faint */
            continue;
        if (vrock_open(nb) || (nh < h && (nh > 0 || (!out && vrock_ghost_at(md, nj, ni))))) {
            if (h >= hw) {
                vulture_tile *ft = vulture_get_tile(face);

                if (ft && ft->graphic && sh < 255) /* darker with the band */
                    SDL_SetSurfaceColorMod(ft->graphic, (Uint8) sh, (Uint8) sh, (Uint8) sh);
                if (ft && ft->graphic && ghost) /* s219: see-through */
                    SDL_SetSurfaceAlphaMod(ft->graphic, (Uint8) ghost);
                vwall_draw(x, y, face, side ? 3 : 2, 0, -1, -1, ghost ? 0 : dim);
                if (ft && ft->graphic && sh < 255)
                    SDL_SetSurfaceColorMod(ft->graphic, 255, 255, 255);
                if (ft && ft->graphic && ghost)
                    SDL_SetSurfaceAlphaMod(ft->graphic, 255);
            } else {
                vrock_low_face(x, y, face, h, sh, dim);
            }
        }
    }
    { /* the lips: where what lies behind is lower - the room, the corridor, a shelf */
        int lips = 0, e, edges = 0;

        for (e = 0; e < 2; e++) {
            int oj, oi, nj, ni;

            v_rot(e ? 0 : -1, e ? -1 : 0, vulture_view_rot, &oj, &oi);
            nj = j + oj, ni = i + oi;
            if (isok(nj, ni) && vrock_hh[ni][nj] < h && md->get_glyph(MAP_BACK, nj, ni) != V_MISC_UNMAPPED_AREA)
                lips |= 1 << e;
            else if (isok(nj, ni) && vrock_hh[ni][nj] > 0 && vrock_hh[ni][nj] < h)
                lips |= 1 << e;
            else if (!ghost && vrock_ghost_at(md, nj, ni)) /* s219: the solid ceiling's edge */
                lips |= 1 << e;
        }
        { /* s225: lit as its wall is - its light through the light grid's darkness curve (VL_DMAX) */
            float L = vtop_style[i][j] >= 0 ? vtop_light[i][j] : 0.3f;

            L = L > 1.0f ? 1.0f : L < 0.0f ? 0.0f : L;
            sh = (int) (sh * (1.0f - (1.0f - L) * 0.94f) * 0.82f); /* a top a little under its lit face (by a lamp, white marble glowed) */
        }
        { /* s219: the soft edges - toward rock you do not know (or the map's edge) */
            static const int ev[4][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };

            for (e = 0; e < 4; e++) {
                int oj, oi, nj, ni, nb;

                v_rot(ev[e][0], ev[e][1], vulture_view_rot, &oj, &oi);
                nj = j + oj, ni = i + oi;
                if (!isok(nj, ni)) {
                    edges |= 1 << e;
                    continue;
                }
                nb = md->get_glyph(MAP_BACK, nj, ni);
                if ((nb == V_MISC_UNMAPPED_AREA || nb == V_TILE_NONE) && !vrock_hh[ni][nj])
                    edges |= 1 << e;
            }
        }
        vrock_cap_put(x, y, h, sh, (j * 7 + i * 3) % 3, (float) (y - vwall_hy) / 22.0f, lips,
                      vrock_style == 1 && b != V_TILE_WALL_GENERIC, ghost, edges,
                      vrock_style == 1 ? -1 : vtop_style[i][j]); /* s219: dark / see-through; s225: its stone */
    }
}

/*
 * IMPLEMENTATION_LOG s216: stairs you take.  When the core is sure of a
 * level change (goto_level, past its last refusal) it calls
 * vulture_level_leave: a short blocking clip - you sink down the stairs,
 * rise up them, drop through the floor (or just go, by magic) while the
 * map fades to black - and the screen holds black until the new level is
 * first drawn; then it fades in while you step down onto it, rise into
 * view, or drop in from above.  The UI stays.  A change that never comes
 * lets go after 1.5 s.  VULTURE_NO_STAIRFX turns it off.
 */
#define VSTAIR_OUT 380
#define VSTAIR_IN 520
static int vstair_how = 0;       /* the leave clip playing: 1 down, 2 up, 3 falling, 4 other */
static double vstair_u = 0.0;    /* its progress */
static bool vstair_hold = false; /* black, waiting for the new level */
static Uint32 vstair_hold_t = 0;
static long vstair_from = -1;    /* the level left */
static int vstair_in_how = 0;    /* how you arrive */
static Uint32 vstair_in_t = 0;   /* when the arrival began */

static double
vstair_ease(double u)
{
    u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
    return u * u * (3.0 - 2.0 * u);
}

static bool
vstair_busy(Uint32 now)
{
    return vstair_how || vstair_hold || (vstair_in_t && now - vstair_in_t < VSTAIR_IN);
}

/* how far your sprite is moved now, leaving or arriving */
static int
vstair_hero_dy(void)
{
    Uint32 now = SDL_GetTicks();

    if (vstair_how) {
        double u = vstair_u;

        switch (vstair_how) {
        case 1: return (int) (26.0 * u * u);           /* down the stairs: you sink */
        case 2: return (int) (-22.0 * vstair_ease(u)); /* up them: you rise */
        case 3: return (int) (70.0 * u * u);           /* through the floor */
        default: return 0;
        }
    }
    if (vstair_in_t && now - vstair_in_t < VSTAIR_IN) {
        double u = (now - vstair_in_t) / (double) VSTAIR_IN, e = 1.0 - vstair_ease(u);

        switch (vstair_in_how) {
        case 1: return (int) (-18.0 * e); /* came down: you step down onto it */
        case 2: return (int) (18.0 * e);  /* came up: you rise into view */
        case 3: {                         /* dropped in from above */
            double f = u < 0.6 ? u / 0.6 : 1.0;

            return (int) (-80.0 * (1.0 - f * f));
        }
        default: return 0;
        }
    }
    return 0;
}

/* over the map, after its effects: the fade - leaving, held, arriving */
static void
vstair_fade(int x, int y, int w, int h)
{
    static SDL_Surface *blk = NULL;
    Uint32 now = SDL_GetTicks();
    int a = 0;
    SDL_Rect d;

    if (vstair_from >= 0 && !vstair_how && u.ux && (long) ledger_no(&u.uz) != vstair_from) {
        vstair_from = -1, vstair_hold = false, vstair_in_t = now; /* the new level: fade in */
    } else if (vstair_hold && now - vstair_hold_t > 1500) {      /* it never came: let go */
        vstair_from = -1, vstair_hold = false, vstair_in_t = now, vstair_in_how = 0;
    }
    if (vstair_how)
        a = (int) (255.0 * vstair_ease(vstair_u));
    else if (vstair_hold)
        a = 255;
    else if (vstair_in_t && now - vstair_in_t < VSTAIR_IN)
        a = (int) (255.0 * (1.0 - vstair_ease((now - vstair_in_t) / (double) VSTAIR_IN)));
    if (a <= 0 || !vulture_screen)
        return;
    if (!blk) {
        if (!(blk = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_ARGB8888)))
            return;
        SDL_FillRect(blk, NULL, 0xFF000000u);
        SDL_SetSurfaceBlendMode(blk, SDL_BLENDMODE_BLEND);
    }
    SDL_SetSurfaceAlphaMod(blk, (Uint8) (a > 255 ? 255 : a));
    d.x = x, d.y = y, d.w = w, d.h = h;
    SDL_BlitScaled(blk, NULL, vulture_screen, &d);
}

/* the core (goto_level), once the move is sure: play leaving */
extern "C" void
vulture_level_leave(int how)
{
    Uint32 t0;
    bool shot = false;

    if (vulture_windows_inited != TRUE || !levwin || !u.ux || getenv("VULTURE_NO_STAIRFX"))
        return;
    vstair_from = (long) ledger_no(&u.uz), vstair_in_how = how, vstair_how = how, vstair_u = 0.0;
    vstair_in_t = 0;
    t0 = SDL_GetTicks();
    while (vstair_u < 1.0) {
        vstair_u = (SDL_GetTicks() - t0) / (double) VSTAIR_OUT;
        if (vstair_u > 1.0)
            vstair_u = 1.0;
        SDL_PumpEvents();
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
        if (!shot && vstair_u >= 0.45 && getenv("VULTURE_SELFTEST") && getenv("VULTURE_SELFTEST_STAIRS")) {
            char p[700];

            snprintf(p, sizeof p, "%s" "\\" "stairs_leave.bmp", getenv("VULTURE_SELFTEST"));
            vulture_save_frame_bmp(p);
            shot = true;
        }
        SDL_Delay(10);
    }
    vstair_how = 0, vstair_hold = true, vstair_hold_t = SDL_GetTicks();
}

/* probe: s216 - VULTURE_SELFTEST_STAIRS: onto the down stairs and down;
   shots mid-leave (in vulture_level_leave), mid-arrival and after */
static Uint32 vstair_probe_t = 0;
static int vstair_probe_n = 0;

static void
vstair_probe_tick(Uint32 now)
{
    char p[700];

    if (vstair_probe_n == 0) {
        stairway *st;

        if (now - vstair_probe_t < 1200)
            return;
        for (st = gs.stairs; st; st = st->next)
            if (!st->up && !st->isladder && st->tolev.dnum == u.uz.dnum)
                break;
        if (!st) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "stairs probe: no down stairs\n");
            vstair_probe_t = 0;
            return;
        }
        teleds(st->sx, st->sy, TELEDS_NO_FLAGS);
        vision_recalc(0);
        flush_screen(1);
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "stairs probe: going down from %d,%d\n", u.ux, u.uy);
        vstair_probe_n = 1;
        next_level(TRUE);
        vstair_probe_t = SDL_GetTicks();
        return;
    }
    if (vstair_probe_n == 1 && vstair_in_t && now - vstair_in_t >= 200) {
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "stairs_arrive.bmp", getenv("VULTURE_SELFTEST"));
        vulture_save_frame_bmp(p);
        vstair_probe_n = 2;
        return;
    }
    if (vstair_probe_n == 2 && vstair_in_t && now - vstair_in_t >= 900) {
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "stairs_after.bmp", getenv("VULTURE_SELFTEST"));
        vulture_save_frame_bmp(p);
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "stairs probe: done, now on level %d at %d,%d\n",
                          (int) depth(&u.uz), u.ux, u.uy);
        vstair_probe_t = 0;
    }
}

/*
 * IMPLEMENTATION_LOG s221: last seen.  The user: pets and monsters "run back
 * and forth into fog of war" and blink out; and in "real-time until combat"
 * a hostile stepping out of view ended the fight, so a moment later the
 * clock passed turns again and it walked back in and hit you.
 *  - A creature you lose sight of stays where you last saw it: its sprite,
 *    faded, fading further turn by turn (VGHOST_TURNS; a pet longer), lit
 *    as that square is.  Behind a wall it gets a dim outline (vxr).
 *  - Only what you saw: it never moves with the creature.  It goes when you
 *    see that creature again, stand next to the spot and see it empty,
 *    something else is seen there, it fades out, or you change level.  A
 *    creature that dies (or leaves) while you watch leaves none.  None while
 *    hallucinating.
 *  - A hostile seen in the last VGHOST_FIGHT turns, within 12, keeps the
 *    fight on (vrt_combat): real time stays paused until you act and it has
 *    stayed out of sight that long.
 */
#define VGHOST_TURNS 20
#define VGHOST_PET_TURNS 40
#define VGHOST_FIGHT 6
struct vghost_t {
    unsigned id;    /* the creature (m_id) */
    int tile, face; /* how it was drawn */
    int x, y;       /* where you last saw it */
    long moves;     /* when (turns) */
    Uint32 t_lost;  /* when it went out of sight (0 while seen) */
    Uint8 rel;      /* 0 hostile, 1 peaceful, 2 pet */
    bool seen;      /* seen this frame */
};
static std::vector<vghost_t> vghost_list;
static long vghost_level = -1;

static bool
vghost_alive(unsigned id)
{
    struct monst *m;

    for (m = fmon; m; m = m->nmon)
        if (m->m_id == id && !DEADMONSTER(m))
            return true;
    return false;
}

/* once a frame, before the map pass: who you see, who you lost */
static void
vghost_update(mapdata *md)
{
    long lev = u.ux ? (long) ledger_no(&u.uz) : -1;
    Uint32 now = SDL_GetTicks();
    struct monst *m;
    size_t k;

    if (lev != vghost_level || !md || !u.ux || Hallucination || getenv("VULTURE_NO_GHOSTS")) {
        vghost_list.clear();
        vghost_level = lev;
        return;
    }
    for (k = 0; k < vghost_list.size(); k++)
        vghost_list[k].seen = false;
    for (m = fmon; m; m = m->nmon) {
        int t;

        if (DEADMONSTER(m) || m == u.usteed || !isok(m->mx, m->my) || !canspotmon(m)
            || !glyph_is_monster(glyph_at(m->mx, m->my)))
            continue;
        t = md->get_glyph(MAP_MON, m->mx, m->my);
        if (t < 0 || t == V_TILE_NONE)
            continue;
        for (k = 0; k < vghost_list.size() && vghost_list[k].id != m->m_id; k++)
            ;
        if (k == vghost_list.size()) {
            vghost_t g;

            memset(&g, 0, sizeof g);
            g.id = m->m_id;
            vghost_list.push_back(g);
        }
        vghost_list[k].tile = t, vghost_list[k].face = vtw_face[m->my][m->mx];
        vghost_list[k].x = m->mx, vghost_list[k].y = m->my, vghost_list[k].moves = svm.moves;
        vghost_list[k].t_lost = 0, vghost_list[k].seen = true;
        vghost_list[k].rel = (Uint8) (m->mtame ? 2 : m->mpeaceful ? 1 : 0);
    }
    for (k = 0; k < vghost_list.size();) {
        vghost_t &g = vghost_list[k];
        bool drop = false;

        if (!g.seen) {
            if (!g.t_lost) { /* gone this frame: died or left before your eyes, or out of sight */
                if (!vghost_alive(g.id))
                    drop = true;
                else
                    g.t_lost = now ? now : 1;
            }
            if (svm.moves - g.moves > (g.rel == 2 ? VGHOST_PET_TURNS : VGHOST_TURNS))
                drop = true; /* faded out */
            else if (distmin(u.ux, u.uy, g.x, g.y) <= 1 && cansee(g.x, g.y))
                drop = true; /* you are right there: it is not */
            else if (md->get_glyph(MAP_MON, g.x, g.y) != V_TILE_NONE)
                drop = true; /* something else is seen there now */
        }
        if (drop)
            vghost_list.erase(vghost_list.begin() + (long) k);
        else
            k++;
    }
}

/* s228: a sprite as a memory - washed out to a cool grey (a faint sprite in
   full colour read as a live see-through one, like an invisible creature you
   can see); cached per tile and facing */
static SDL_Surface *
vghost_surface(SDL_Surface *src, int key)
{
    static std::vector<std::pair<int, SDL_Surface *> > cache;
    SDL_Surface *s;
    size_t k;
    int x, y;

    for (k = 0; k < cache.size(); k++)
        if (cache[k].first == key)
            return cache[k].second;
    if (cache.size() >= 256 || !src || !(s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0)))
        return NULL;
    SDL_LockSurface(s);
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++) {
            Uint32 *pp = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch) + x, p = *pp;
            double r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255, l = 0.30 * r + 0.59 * g + 0.11 * b;
            int nr, ng, nb;

            if (!(p >> 24))
                continue;
            nr = (int) (r + (l * 0.86 - r) * 0.75), ng = (int) (g + (l * 0.94 - g) * 0.75);
            nb = (int) (b + (l * 1.10 + 6.0 - b) * 0.75);
            nr = nr < 0 ? 0 : nr > 255 ? 255 : nr, ng = ng < 0 ? 0 : ng > 255 ? 255 : ng;
            nb = nb < 0 ? 0 : nb > 255 ? 255 : nb;
            *pp = (p & 0xFF000000u) | (Uint32) nr << 16 | (Uint32) ng << 8 | (Uint32) nb;
        }
    SDL_UnlockSurface(s);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
    cache.push_back(std::make_pair(key, s));
    return s;
}

/* the map pass, a square with no creature seen on it: one you saw here */
static void
vghost_draw(int j, int i, int x, int y)
{
    Uint32 now = SDL_GetTicks();
    size_t k;

    for (k = 0; k < vghost_list.size(); k++) {
        const vghost_t &g = vghost_list[k];
        double age, life, a;
        vxr_mon_t mr;

        if (g.seen || g.x != j || g.y != i)
            continue;
        life = g.rel == 2 ? VGHOST_PET_TURNS : VGHOST_TURNS;
        age = (double) (svm.moves - g.moves) / life;
        a = 150.0 * pow(1.0 - (age > 1.0 ? 1.0 : age < 0.0 ? 0.0 : age), 0.7);
        if (g.t_lost && now - g.t_lost < 350) /* it was just here, solid: settle into a ghost */
            a = 255.0 + (a - 255.0) * (now - g.t_lost) / 350.0;
        if (a < 6.0)
            continue;
        { /* s228: washed out - a memory, not a live see-through creature */
            vulture_tile *tl = vulture_get_tile(g.tile);
            SDL_Surface *img, *gs;
            int art, dx;
            bool flip;

            if (!tl || !tl->graphic)
                continue;
            art = vtw_art_facing(g.tile, tl->graphic);
            flip = g.face != 0 && ((art == 0 && g.face < 0) || (art != 0 && art != g.face));
            img = tl->graphic, dx = x + tl->xmod;
            if (flip) {
                SDL_Surface *fl = vtw_flipped(g.tile, tl->graphic);

                if (fl)
                    img = fl, dx = x - tl->xmod - tl->graphic->w + 1;
                else
                    flip = false;
            }
            gs = vghost_surface(img, g.tile * 2 + (flip ? 1 : 0));
            vtx_blit(gs ? gs : img, dx, y + tl->ymod, 1.0, 1.0, 0.0, false, a >= 255.0 ? 254 : (int) a);
            if (g.t_lost && now - g.t_lost < 350 && gs) /* settling: its colour drains out */
                vtx_blit(img, dx, y + tl->ymod, 1.0, 1.0, 0.0, false,
                         (int) (254.0 * (1.0 - (now - g.t_lost) / 350.0)));
        }
        mr.tile = g.tile, mr.face = g.face, mr.tx = x, mr.ty = y, mr.seq = ++vxr_seq; /* behind a wall: outlined */
        mr.xs = mr.ys = 1.0, mr.deg = 0.0;
        if (g.rel == 2)
            mr.r = 80, mr.g = 160, mr.b = 80;
        else if (g.rel == 1)
            mr.r = 165, mr.g = 145, mr.b = 65;
        else
            mr.r = 170, mr.g = 60, mr.b = 45;
        vxr_mons.push_back(mr);
        return;
    }
}

/* s221: a hostile you saw lately, near: the fight is not over */
static bool
vghost_fight(void)
{
    size_t k;

    for (k = 0; k < vghost_list.size(); k++) {
        const vghost_t &g = vghost_list[k];

        if (!g.seen && g.rel == 0 && svm.moves - g.moves <= VGHOST_FIGHT
            && distmin(u.ux, u.uy, g.x, g.y) <= 12)
            return true;
    }
    return false;
}

/* probe: s221 - VULTURE_SELFTEST_GHOST: a jackal by you and your pet in
   sight, then both moved where you cannot see - their ghosts; then ten
   turns on, fainter */
static Uint32 vghost_probe_t = 0;
static int vghost_probe_n = 0;

static void
vghost_probe_shot(const char *name)
{
    char p[700];

    levwin->set_view(u.ux, u.uy);
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    snprintf(p, sizeof p, "%s" "\\" "%s.bmp", getenv("VULTURE_SELFTEST"), name);
    vulture_save_frame_bmp(p);
}

static void
vghost_probe_tick(Uint32 now)
{
    if (now - vghost_probe_t < 900)
        return;
    vghost_probe_t = now;
    switch (vghost_probe_n++) {
    case 0: {
        coord cc;
        struct monst *jm;

        int x, y, n = 0;
        struct monst *pm;

        /* every creature in sight moved 2-5 squares off, as far up the
           screen as can be (no wall in front hides it) */
        (void) jm, (void) cc;
        for (pm = fmon; pm; pm = pm->nmon) {
            int bx = -1, by = -1, bs = 9999;

            if (DEADMONSTER(pm) || !canspotmon(pm))
                continue;
            for (y = 0; y < ROWNO; y++)
                for (x = 1; x < COLNO; x++) {
                    int dd = distmin(u.ux, u.uy, x, y);

                    if (dd >= 2 && dd <= 5 && cansee(x, y) && ACCESSIBLE(levl[x][y].typ) && !m_at(x, y)
                        && !u_at(x, y) && x + y + 3 * n < bs && goodpos(x, y, pm, 0))
                        bs = x + y + 3 * n, bx = x, by = y;
                }
            if (bx >= 0) {
                rloc_to(pm, bx, by);
                n++;
            }
        }
        flush_screen(1);
        vghost_probe_shot("ghost_a_seen");
        break;
    }
    case 1: {
        struct monst *pm;
        int moved = 0;

        for (pm = fmon; pm; pm = pm->nmon) {
            int x, y, done = 0;

            if (DEADMONSTER(pm) || !canspotmon(pm))
                continue;
            for (x = 1; x < COLNO && !done; x++)
                for (y = 0; y < ROWNO && !done; y++)
                    if (distmin(u.ux, u.uy, x, y) > 8 && !cansee(x, y) && ACCESSIBLE(levl[x][y].typ)
                        && !m_at(x, y) && goodpos(x, y, pm, 0)) {
                        rloc_to(pm, x, y);
                        done = 1, moved++;
                    }
        }
        vision_recalc(0);
        flush_screen(1);
        vghost_probe_shot("ghost_b_lost");
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "ghost probe: moved %d out of sight, %d ghosts, fight %d\n",
                          moved, (int) vghost_list.size(), vghost_fight() ? 1 : 0);
        break;
    }
    case 2:
        svm.moves += 10;
        vghost_probe_shot("ghost_c_later");
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "ghost probe: 10 turns on, %d ghosts, fight %d\n",
                          (int) vghost_list.size(), vghost_fight() ? 1 : 0);
        break;
    default:
        vghost_probe_t = 0;
        break;
    }
}

/* probe: s218 - VULTURE_SELFTEST_ROCK: the rock mass where you play - the
   start room, its nearest door, a corridor walked, the level mapped, back
   in the room; VULTURE_ROCK_DARK=1: the level unlit and forgotten, then a
   walk through the dark */
static Uint32 vrock_probe_t = 0;
static int vrock_probe_n = 0;

static void
vrock_probe_shot(const char *name)
{
    char p[700];
    int keep = vrock_style, s;

    levwin->set_view(u.ux, u.uy);
    for (s = 0; s < 4; s++) { /* s219: each style, the same frame */
        static const int sv[4] = { 3, 4, 2, 0 };

        vrock_style = sv[s];
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(p, sizeof p, "%s" "\\" "%s_s%d.bmp", getenv("VULTURE_SELFTEST"), name, sv[s]);
        vulture_save_frame_bmp(p);
    }
    vrock_style = 4, vcut_probe_off = true; /* s223: the same, the room you are in not cut */
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    snprintf(p, sizeof p, "%s" "\\" "%s_s4nocut.bmp", getenv("VULTURE_SELFTEST"), name);
    vulture_save_frame_bmp(p);
    vcut_probe_off = false;
    vrock_style = keep;
    levwin->force_redraw();
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "rock probe: %s at %d,%d\n", name, u.ux, u.uy);
}

static void
vrock_probe_go(int x, int y)
{
    teleds(x, y, TELEDS_NO_FLAGS);
    vision_recalc(0);
    flush_screen(1);
    levwin->set_view(u.ux, u.uy);
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vulture_refresh_window_region();
}

static void
vrock_probe_tick(Uint32 now)
{
    static int sx = -1, sy = -1;
    static bool seen[COLNO][ROWNO];
    bool dark = getenv("VULTURE_ROCK_DARK") != NULL;
    int x, y, k;

    if (now - vrock_probe_t < 900)
        return;
    vrock_probe_t = now;
    if (getenv("VULTURE_ROCK_MINES")) { /* the Gnomish Mines: walls everywhere */
        switch (vrock_probe_n++) {
        case 0: {
            d_level nl;

            nl.dnum = mines_dnum, nl.dlevel = (xint16) atoi(getenv("VULTURE_ROCK_MINES"));
            goto_level(&nl, FALSE, FALSE, FALSE);
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "rock probe: in the mines, depth %d\n",
                              (int) depth(&u.uz));
            break;
        }
        case 1:
            vrock_probe_shot("rock_m_start");
            break;
        case 2:
            do_mapping();
            flush_screen(1);
            vrock_probe_shot("rock_m_mapped");
            break;
        default:
            vrock_probe_t = 0;
            break;
        }
        return;
    }
    switch (vrock_probe_n++) {
    case 0:
        sx = u.ux, sy = u.uy;
        if (dark) {
            for (x = 1; x < COLNO; x++)
                for (y = 0; y < ROWNO; y++) {
                    levl[x][y].lit = 0, levl[x][y].waslit = 0, levl[x][y].seenv = 0;
                    levl[x][y].glyph = GLYPH_UNEXPLORED;
                }
            gv.vision_full_recalc = 1;
            vision_recalc(0);
            docrt();
            flush_screen(1);
        }
        vrock_probe_shot(dark ? "rock_a_dark" : "rock_a_start");
        break;
    case 1:
    case 2:
        if (dark) { /* two steps east (or wherever is open), twice */
            static const int ddx[4] = { 1, 0, -1, 0 }, ddy[4] = { 0, 1, 0, -1 };

            for (k = 0; k < 4; k++)
                if (isok(u.ux + 2 * ddx[k], u.uy + 2 * ddy[k]) && ACCESSIBLE(levl[u.ux + ddx[k]][u.uy + ddy[k]].typ)
                    && ACCESSIBLE(levl[u.ux + 2 * ddx[k]][u.uy + 2 * ddy[k]].typ)) {
                    vrock_probe_go(u.ux + ddx[k], u.uy + ddy[k]);
                    vrock_probe_go(u.ux + ddx[k], u.uy + ddy[k]);
                    break;
                }
            vrock_probe_shot(vrock_probe_n == 2 ? "rock_b_dark_walk" : "rock_c_dark_walk");
            break;
        }
        if (vrock_probe_n == 2) { /* onto the nearest open door */
            int bd = 999, bx = -1, by = -1;

            for (x = 1; x < COLNO; x++)
                for (y = 0; y < ROWNO; y++)
                    if (IS_DOOR(levl[x][y].typ) && !(levl[x][y].doormask & (D_CLOSED | D_LOCKED))
                        && distmin(sx, sy, x, y) < bd)
                        bd = distmin(sx, sy, x, y), bx = x, by = y;
            if (bx < 0) {
                vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "rock probe: no open door\n");
                break;
            }
            memset(seen, 0, sizeof seen);
            seen[bx][by] = true;
            vrock_probe_go(bx, by);
            vrock_probe_shot("rock_b_door");
        } else { /* down the corridor, 5 squares */
            int step;

            for (step = 0; step < 5; step++) {
                int nx = -1, ny = -1;

                for (k = 0; k < 8 && nx < 0; k++) {
                    static const int ddx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, ddy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
                    int qx = u.ux + ddx[k], qy = u.uy + ddy[k];

                    if (isok(qx, qy) && !seen[qx][qy] && levl[qx][qy].typ == CORR)
                        nx = qx, ny = qy;
                }
                if (nx < 0)
                    break;
                seen[nx][ny] = true;
                vrock_probe_go(nx, ny);
            }
            vrock_probe_shot("rock_c_corridor");
        }
        break;
    case 3:
        if (dark) {
            vrock_probe_t = 0;
            break;
        }
        do_mapping();
        flush_screen(1);
        vrock_probe_shot("rock_d_mapped_corridor");
        break;
    case 4:
        vrock_probe_go(sx, sy);
        vrock_probe_shot("rock_e_mapped_room");
        break;
    default:
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "rock probe: done\n");
        vrock_probe_t = 0;
        break;
    }
}

/* the overlay pass, after vfx_draw */
static void
vsp_draw(mapdata *md, int cx, int cy, int vx, int vy)
{
    static Uint32 last = 0;
    Uint32 now = SDL_GetTicks();
    double dt = last ? (now - last) / 1000.0 : 0.0, t = now / 1000.0;
    int x, y;

    last = now;
    if (dt > 0.05)
        dt = 0.05;
    if (!u.ux)
        return;
    vsp_level_check();
    vsp_cx = cx, vsp_cy = cy, vsp_vx = vx, vsp_vy = vy;
    { /* s205: the camera focuses on you */
        int fx, fy;

        vfx_mover_screen(u.ux, u.uy, cx, cy, vx, vy, &fx, &fy);
        vulture_gpumap_set_focus(fy - 30);
    }
    { /* s212: the colour of where you are */
        int gr, gg, gb;

        vgrade_area(&gr, &gg, &gb);
        vulture_gpumap_set_grade(gr, gg, gb);
    }
    if (vsp_on_count > 0)
        for (y = 0; y < ROWNO; y++)
            for (x = 1; x < COLNO; x++) {
                vsp_cell_t &c = vsp_cells[y][x];

                if (!c.on)
                    continue;
                /* the map stopped showing it without our hearing (a level
                   change, a cleared map) or it is stuck: let it go */
                if ((md && md->get_glyph(MAP_SPECIAL, x, y) == V_TILE_NONE) || now - c.t_on > 6000) {
                    c.on = 0, c.t_off = now;
                    vsp_on_count--;
                    continue;
                }
                if (c.kind == 1)
                    vsp_draw_ray(x, y, c, now, t, dt);
                else if (c.kind == 2)
                    vsp_draw_expl(x, y, c, now, t);
                else
                    vsp_draw_small(x, y, c, t);
            }
    if (vsp_on_count < 0)
        vsp_on_count = 0;
    vdr_tick(now);   /* s203 */
    vfall_tick(now); /* s204 */
    vlife_tick(now); /* s206 */
    vvoid_draw(md, t); /* s209 */
    vlive_furniture(md, t); /* s206 */
    vsp_draw_waves(now);
    vsp_draw_parts(now, t, dt);
    vfl_draw(now);    /* s202 */
    vimp_draw(now, t); /* s202 */
}

/* probe: s198 - VULTURE_SELFTEST_SPELL fires each ray type, then each
   explosion type, from the hero, shooting frames as they play */
static Uint32 vsp_probe_t = 0, vsp_probe_end = 0;
static int vsp_probe_n = 0, vsp_probe_shots = 0, vsp_probe_pumped = 0;

static void
vsp_probe_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];
    SDL_Surface *o;
    SDL_Rect a;
    int hx, hy;

    if (!dir || vsp_probe_shots >= 80 || !vulture_screen || vulture_screen->w < 960 || vulture_screen->h < 540)
        return;
    vsp_screen(u.ux, u.uy, &hx, &hy);
    a.w = 960, a.h = 540, a.x = hx - 480, a.y = hy - 310;
    if (a.x < 0)
        a.x = 0;
    if (a.y < 0)
        a.y = 0;
    if (a.x + a.w > vulture_screen->w)
        a.x = vulture_screen->w - a.w;
    if (a.y + a.h > vulture_screen->h)
        a.y = vulture_screen->h - a.h;
    if (!(o = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return;
    snprintf(p, sizeof p, "%s\\spell%02d_%s_%02d.bmp", dir, vsp_probe_n - 1, tag, vsp_probe_shots++);
    if (vulture_frame_to_surface(o, &a))
        SDL_SaveBMP(o, p);
    SDL_FreeSurface(o);
}

static void
vsp_probe_fire(void)
{
    static const int rays[8] = { 1, 5, 2, 4, 0, 3, 7, 6 }; /* gas last: its clouds block the view */
    static const int expl[7] = { EXPL_FIERY, EXPL_FROSTY, EXPL_NOXIOUS, EXPL_WET, EXPL_MAGICAL, EXPL_DARK,
                                 EXPL_MUDDY };
    static const int ddx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    int n = vsp_probe_n++, k, best = -1, bl = 0, dx = 1, dy = 0;

    if (n >= 15) {
        vsp_probe_t = 0;
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "spellfx probe: done, %d shots\n", vsp_probe_shots);
        return;
    }
    /* the probe hero lives through its own bounces */
    HReflecting |= FROMOUTSIDE;
    HFire_resistance |= FROMOUTSIDE, HCold_resistance |= FROMOUTSIDE, HShock_resistance |= FROMOUTSIDE;
    HSleep_resistance |= FROMOUTSIDE, HPoison_resistance |= FROMOUTSIDE, HDisint_resistance |= FROMOUTSIDE;
    HAcid_resistance |= FROMOUTSIDE;
    u.uhp = u.uhpmax;
    if (Blinded) /* a lightning bolt that came back still dazzles */
        make_blinded(0L, FALSE);
    for (k = 0; k < 8; k++) { /* the most open way */
        int l = 0, x = u.ux + ddx[k], y = u.uy + ddy[k];

        while (isok(x, y) && ACCESSIBLE(levl[x][y].typ) && !m_at(x, y) && l < 12)
            l++, x += ddx[k], y += ddy[k];
        if (l > bl)
            bl = l, best = k;
    }
    if (best >= 0)
        dx = ddx[best], dy = ddy[best];
    vsp_probe_pumped = 0;
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "spellfx probe %d: way %d,%d, %d open\n", n, dx, dy, bl);
    if (n < 7 || n == 14) {
        buzz(rays[n < 7 ? n : 7], 2, u.ux, u.uy, dx, dy);
    } else {
        int d = bl > 4 ? 4 : bl < 2 ? 2 : bl;

        explode(u.ux + dx * d, u.uy + dy * d, 11, 1, WAND_CLASS, expl[n - 7]);
    }
    flush_screen(1); /* as the end of a turn does */
    vsp_probe_t = vsp_probe_end = SDL_GetTicks();
    vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "spellfx probe %d: %d particles, %d waves after\n", n,
                      (int) vsp_parts.size(), (int) vsp_waves.size());
}

static void
vsp_probe_tick(Uint32 now)
{
    if (vsp_probe_end && now - vsp_probe_end > 260) { /* the afterglow */
        vsp_probe_end = 0;
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vsp_probe_shot("after");
    }
    if (now - vsp_probe_t > 1700)
        vsp_probe_fire();
}

/* vulture_delay_output: while a spell effect is alive, keep drawing frames
   through the core's pause between steps (it used to show one a step) */
void
vulture_fx_pump(void)
{
    static Uint32 last = 0;
    Uint32 now = SDL_GetTicks();

    if (!levwin || vulture_windows_inited != TRUE || vtw_animating || vanim_kind || !vsp_active(now)
        || now - last < 15)
        return;
    last = now;
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vulture_refresh_window_region();
    if (vsp_probe_t && getenv("VULTURE_SELFTEST_SPELL")) { /* probe: s198 */
        int f = ++vsp_probe_pumped;

        if (f == 2 || f == 5 || f == 10)
            vsp_probe_shot("live");
    }
}

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
static void vcontrols_page(void); /* s175 */
static void vkeys_probe(const char *dir); /* s195 probe */
static void vpn_probe(const char *dir);   /* s199 probe */
void vzoom_probe_set(int idx);            /* s201 probe */
static SDL_Rect vbtn_rect[4];

/* probe: s166 - VULTURE_SELFTEST_SOAK: play by itself, through real input.
   Tops up HP and food each tick; Tab when an enemy is close, otherwise the
   EXPLORE button; when exploring stops moving you, '>' twice (walk to the
   stairs, go down); a menu or prompt that stays up gets Esc.  Logs a status
   line every 30 s.
   s179: it no longer fights one monster forever - after 60 Tab presses or
   150 turns at the same one it ignores that monster for 400 turns (and
   leaves Tab alone while it is the nearest); a level that drags on is
   magic-mapped after 1200 turns and left after 2500 (straight down the
   main dungeon); with no enemy near it reads or quaffs an unidentified
   scroll or potion now and then (the auto-ID paths; with none in the pack
   it makes one); and 20 s without a game turn gets random steps.  Stuck
   in one spot (a vault, a trap) also counts in real time: 60 s maps the
   level, 150 s leaves it - a stuck hero may pass only a turn a second -
   and so does a level as a whole (4 min maps it, 8 min leaves it): a loop
   that meets a prompt every lap moves, but only a few turns a minute. */
static void
vsoak_tick(bool at_map)
{
    static Uint32 last = 0, off_map_since = 0, last_log = 0, last_use = 0, turn_t = 0,
                  moved_t = 0, lvl_t0 = 0;
    static int still = 0, lastx = -1, lasty = -1, cycle = 0;
    static unsigned tgt_id = 0, ign_id[4] = { 0, 0, 0, 0 };
    static long tgt_moves0 = 0, ign_until[4] = { 0, 0, 0, 0 }, turn_moves = -1;
    static int tgt_tabs = 0, ign_next = 0, ign_total = 0, uses = 0, lvl = -1, lvl_maps = 0,
               lvl_drops = 0;
    static long lvl_moves0 = 0;
    static bool lvl_mapped = false;
    static const SDL_Keycode dirs[8] = { SDLK_h, SDLK_j, SDLK_k, SDLK_l,
                                         SDLK_y, SDLK_u, SDLK_b, SDLK_n };
    Uint32 now = SDL_GetTicks();
    struct monst *m, *near = 0, *near_any = 0;
    int dn = 99, dany = 99, i;

    if (!getenv("VULTURE_SELFTEST_SOAK") || !u.ux)
        return;
    if (!at_map) { /* a menu or prompt: give it 1.5 s, then Esc */
        if (!off_map_since)
            off_map_since = now;
        else if (now - off_map_since > 1500) {
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
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                          "soak: turn %ld dlvl %d at %d,%d  XL %d kills %ld  ignored %d"
                          "  used %d  mapped %d  dropped %d\n",
                          svm.moves, (int) depth(&u.uz), u.ux, u.uy, u.ulevel,
                          u.uconduct.killer, ign_total, uses, lvl_maps, lvl_drops);
    }
    /* game turns: none for 20 s means every action here is a no-op */
    if (svm.moves != turn_moves)
        turn_moves = svm.moves, turn_t = now;
    /* the level clock: map a level that drags on, then leave it */
    if (depth(&u.uz) != lvl)
        lvl = depth(&u.uz), lvl_moves0 = svm.moves, lvl_mapped = false, moved_t = now,
        lvl_t0 = now;
    if (u.ux != lastx || u.uy != lasty || !moved_t)
        moved_t = now;
    if (!lvl_mapped
        && (svm.moves - lvl_moves0 > 1200 || now - moved_t > 60000 || now - lvl_t0 > 240000)) {
        lvl_mapped = true, lvl_maps++;
        do_mapping();
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "soak: dlvl %d dragged on - mapped it\n",
                          lvl);
    } else if ((svm.moves - lvl_moves0 > 2500 || now - moved_t > 150000
                || now - lvl_t0 > 480000)
               && !u.utotype && !In_endgame(&u.uz)) {
        d_level nl;

        nl.dnum = 0; /* the Dungeons of Doom: dlevel == depth there */
        nl.dlevel = (xint16) (depth(&u.uz) + 1);
        lvl_moves0 = svm.moves, moved_t = now, lvl_t0 = now; /* not again at once */
        if (nl.dlevel <= dunlevs_in_dungeon(&nl)) {
            lvl_drops++;
            schedule_goto(&nl, UTOTYPE_NONE, (char *) 0, (char *) 0);
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                              "soak: dlvl %d dragged on - sent down\n", lvl);
        }
    }
    /* the nearest enemy, and the nearest one not being ignored */
    for (m = fmon; m; m = m->nmon) {
        bool ign = false;
        int d;

        if (DEADMONSTER(m) || !canspotmon(m) || m->mtame || m->mpeaceful)
            continue;
        d = distmin(u.ux, u.uy, m->mx, m->my);
        if (d > 6)
            continue;
        for (i = 0; i < 4; i++)
            if (ign_id[i] == m->m_id && svm.moves < ign_until[i])
                ign = true;
        if (d < dany)
            dany = d, near_any = m;
        if (!ign && d < dn)
            dn = d, near = m;
    }
    if (u.ux == lastx && u.uy == lasty)
        still++;
    else
        still = 0;
    lastx = u.ux, lasty = u.uy;
    cycle++;
    if (now - turn_t > 20000) { /* nothing has taken a turn: shake loose */
        vprobe_push_key(dirs[cycle & 7]);
        return;
    }
    if (near && near == near_any && still < 12) { /* Tab goes for the nearest */
        if (near->m_id != tgt_id)
            tgt_id = near->m_id, tgt_tabs = 0, tgt_moves0 = svm.moves;
        if (++tgt_tabs > 60 || svm.moves - tgt_moves0 > 150) {
            ign_id[ign_next] = tgt_id, ign_until[ign_next] = svm.moves + 400;
            ign_next = (ign_next + 1) & 3, ign_total++;
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                              "soak: giving up on %s after %d tries / %ld turns\n",
                              pmname(near->data, Mgender(near)), tgt_tabs,
                              svm.moves - tgt_moves0);
            tgt_id = 0;
            return;
        }
        vprobe_push_key(SDLK_TAB);
        return;
    }
    if (!near_any && now - last_use > 25000) { /* try an unknown scroll / potion */
        struct obj *o;

        for (o = gi.invent; o; o = o->nobj)
            if ((o->oclass == SCROLL_CLASS || o->oclass == POTION_CLASS)
                && !objects[o->otyp].oc_name_known && o->invlet >= 'a' && o->invlet <= 'z')
                break;
        last_use = now;
        if (!o) { /* nothing to try: the probe brings its own */
            o = mkobj(rn2(2) ? SCROLL_CLASS : POTION_CLASS, FALSE);
            if (o)
                o = addinv(o);
            if (o && (o->invlet < 'a' || o->invlet > 'z'))
                o = 0;
        }
        if (o) {
            uses++;
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "soak: %s %s\n",
                              o->oclass == SCROLL_CLASS ? "reading" : "quaffing", xname(o));
            vprobe_push_key(o->oclass == SCROLL_CLASS ? SDLK_r : SDLK_q);
            vprobe_push_key((SDL_Keycode) o->invlet);
            return;
        }
    }
    if (still >= 8) { /* exploring has stopped: head down */
        vprobe_push_key(SDLK_GREATER);
        vprobe_push_key(SDLK_GREATER);
        if (still > 30) /* stuck somewhere: a random step */
            vprobe_push_key(dirs[cycle & 7]);
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
static Uint32 vlook_probe_t = 0;  /* s205 probe: the whole level known, shot */
/* probe: s187 - VULTURE_SELFTEST_FOG maps the level and shoots the fog of
   war against the light: in the start room, in a corridor, in a dark room */
static Uint32 vfog_probe_t = 0;
static int vfog_probe_n = 0, vfog_sx = 0, vfog_sy = 0;
/* probe: s193 - VULTURE_SELFTEST_COND gives the hero condition after
   condition and shoots each (cond_*.bmp) */
static Uint32 vcond_probe_t = 0;
static int vcond_probe_n = 0;

static void
vcond_probe_set(int n)
{
    switch (n) {
    case 0:
        make_stunned(300L, FALSE);
        break;
    case 1:
        make_stunned(0L, FALSE);
        make_confused(300L, FALSE);
        make_sick(300L, "probe", FALSE, SICK_VOMITABLE);
        break;
    case 2:
        make_confused(0L, FALSE);
        make_sick(0L, (char *) 0, FALSE, SICK_ALL);
        make_hallucinated(300L, FALSE, 0L);
        break;
    case 3:
        make_hallucinated(0L, FALSE, 0L);
        HLevitation = (HLevitation & ~TIMEOUT) | 300L;
        break;
    case 4:
        HLevitation &= ~TIMEOUT;
        HInvis |= FROMOUTSIDE;
        newsym(u.ux, u.uy);
        break;
    }
    flush_screen(1);
}

static bool
vfog_probe_move(int n)
{
    int x, y, bx = -1, by = -1, bd = 999;

    if (n == 1) { /* the nearest corridor a few squares off */
        for (x = 1; x < COLNO; x++)
            for (y = 0; y < ROWNO; y++) {
                int d = distmin(vfog_sx, vfog_sy, x, y);

                if (levl[x][y].typ == CORR && d >= 5 && d < bd && !m_at(x, y))
                    bd = d, bx = x, by = y;
            }
    } else { /* back to the start, every room dark */
        for (x = 1; x < COLNO; x++)
            for (y = 0; y < ROWNO; y++)
                levl[x][y].lit = 0;
        bx = vfog_sx, by = vfog_sy;
    }
    if (bx < 0)
        return false;
    teleds(bx, by, TELEDS_NO_FLAGS);
    vision_recalc(0);
    flush_screen(1);
    return true;
}
static void vl_relief_set(bool on); /* s145 */
static void vl_relief_report(FILE *f);

static long vrt_probe_moves0 = 0; /* s140 probe */
static int vrt_get(void);
static void vrt_probe_dump(FILE *f);
extern "C" void vulture_search_fx(void); /* s136 */

/* probe: s186 - VULTURE_SELFTEST_PAN pushes a burst of middle-drag motion
   and reports how long the game took to catch up, and in how many frames */
static Uint32 vpan_probe_t0 = 0;
static int vpan_probe_frames = 0, vpan_probe_round = 0;
static int vpan_probe_dx = 0, vpan_probe_dy = 0, vpan_probe_moves = 0;

void
vulture_idle_bob(bool at_map)
{
    static Uint32 last = 0;
    Uint32 now = SDL_GetTicks(), took;

    if (vpan_probe_t0) { /* probe: s186 */
        SDL_PumpEvents();
        if (SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) == 0) {
            vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                              "pan: round %d - 300 drag events caught up in %u ms, %d frames;"
                              " %d drag moves took (%d, %d) px of (%d, 0)\n",
                              vpan_probe_round, (unsigned) (now - vpan_probe_t0),
                              vpan_probe_frames, vpan_probe_moves, vpan_probe_dx,
                              vpan_probe_dy, 300 * (vulture_render_scale > 1 ? vulture_render_scale : 1));
            vpan_probe_t0 = 0;
        }
    }

    if (vcond_probe_t && now - vcond_probe_t > 900 && getenv("VULTURE_SELFTEST")) { /* probe: s193 */
        static const char *const nm[5] = { "stun", "confused_sick", "halluc", "float", "invis" };
        char pp[700];

        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(pp, sizeof pp, "%s\\cond_%s.bmp", getenv("VULTURE_SELFTEST"), nm[vcond_probe_n]);
        vulture_save_frame_bmp(pp);
        vcond_probe_t = 0;
        if (++vcond_probe_n < 5) {
            vcond_probe_set(vcond_probe_n);
            vcond_probe_t = SDL_GetTicks();
        }
    }
    if (vfog_probe_t && now - vfog_probe_t > 900 && getenv("VULTURE_SELFTEST")) { /* probe: s187 */
        /* s192: "room_later" is the same view a little later (the mist drifts) */
        static const char *const nm[4] = { "room", "room_later", "corridor", "dark" };
        char pp[700];

        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(pp, sizeof pp, "%s\\fog_%s.bmp", getenv("VULTURE_SELFTEST"), nm[vfog_probe_n]);
        vulture_save_frame_bmp(pp);
        vfog_probe_t = 0;
        if (++vfog_probe_n < 4 && (vfog_probe_n == 1 || vfog_probe_move(vfog_probe_n - 1)))
            vfog_probe_t = SDL_GetTicks();
    }
    if (vlook_probe_t && now - vlook_probe_t > 1200 && getenv("VULTURE_SELFTEST")) { /* probe: s205 */
        char pp[700];

        vlook_probe_t = 0;
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        snprintf(pp, sizeof pp, "%s" "\\" "look_full.bmp", getenv("VULTURE_SELFTEST"));
        vulture_save_frame_bmp(pp);
    }
    if (vpause_probe_t && now - vpause_probe_t > 700 && getenv("VULTURE_SELFTEST")) {
        char pp[700];

        vpause_probe_t = 0;
        ROOTWIN->draw_windows();
        snprintf(pp, sizeof pp, "%s\\selftest_pause.bmp", getenv("VULTURE_SELFTEST"));
        vulture_save_frame_bmp(pp);
    }
    if (vsp_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s198 */
        vsp_probe_tick(now);
    if (vimp_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s202 */
        vimp_probe_tick(now);
    if (vdr_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s203 */
        vdr_probe_tick(now);
    if (vfall_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s204 */
        vfall_probe_tick(now);
    if (vlife_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s206 */
        vlife_probe_tick(now);
    if (vlid_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s207 */
        vlid_probe_tick(now);
    if (vwall_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s209 */
        vwall_probe_tick(now);
    if (vxr_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s210 */
        vxr_probe_tick(now);
    if (vtorch_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s211 */
        vtorch_probe_tick(now);
    if (vgrade_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s212 */
        vgrade_probe_tick(now);
    if (vrefl_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s213 */
        vrefl_probe_tick(now);
    if (vstair_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s216 */
        vstair_probe_tick(now);
    if (vrock_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s218 */
        vrock_probe_tick(now);
    if (vghost_probe_t && at_map && getenv("VULTURE_SELFTEST")) /* probe: s221 */
        vghost_probe_tick(now);
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
        if (now - last < ((vsp_active(now) || vstair_busy(now) || vcut_busy(now)) ? 16u : marker ? 25u : vidle_interval)) /* s198, s216, s223: 60 fps */
            return;
    }
    last = now;
    vselftest(); /* s126 probe (VULTURE_SELFTEST only) */
    (void) vfx_alive();
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    /* s117: the redraw painted over the hover tooltip - put it back (it
       used to flash up and vanish within ~80 ms).  s215: BEFORE the one
       present - presenting the windows first sent a frame out without it
       every idle tick, and the tooltip over your hero flickered */
    vulture_mouse_draw_noflush();
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

/* probe: s184 - VULTURE_SELFTEST_APPROACH saves every frame of the glide and
   the lunge that follow its step, to check what is drawn where */
static bool vprobe_seq_on = false;
static int vprobe_seq_n = 0;

static void
vprobe_seq_shot(const char *tag)
{
    const char *dir = getenv("VULTURE_SELFTEST");
    char p[700];

    if (!vprobe_seq_on || !dir || vprobe_seq_n >= 40)
        return;
    snprintf(p, sizeof p, "%s\\seq%02d_%s.bmp", dir, vprobe_seq_n++, tag);
    vulture_save_frame_bmp(p);
}

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
            vprobe_seq_shot("glide"); /* probe: s184 */
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

        vfx_mover_screen(u.ux, u.uy, cx, cy, vx, vy, &hx, &hy); /* s184 */
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

        vfx_mover_screen(u.ux, u.uy, cx, cy, vx, vy, &px0, &py0); /* s184 */
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

/*
 * s181: what an identified potion, scroll, wand, ring or amulet does - the
 * rules, in a line or two, so the hover card answers "what is this for?"
 * without a wiki.  Only for identified kinds (these are rules, not hidden
 * state); checked against this 5.0 source where it was in doubt (speed,
 * see invisible, invisibility, sleeping, full healing, fire, earth,
 * guarding).  NULL for anything else.
 */
static const char *
vitem_effect(int otyp)
{
    switch (otyp) {
    /* potions */
    case POT_BOOZE: return "Confuses you for a while (not if blessed) and feeds you a little.\nCursed: you pass out.";
    case POT_FRUIT_JUICE: return "Food in a bottle: a little nutrition (more if blessed).";
    case POT_SEE_INVISIBLE: return "You see invisible things for a long while (blessed: sometimes for\ngood); cures blindness unless cursed.  Tastes like fruit juice.";
    case POT_SICKNESS: return "Costs HP and lowers a random attribute - only a little if you\nresist poison.  Blessed: barely hurts.  Healers are immune.";
    case POT_CONFUSION: return "Confuses you for a while.";
    case POT_EXTRA_HEALING: return "Heals a lot and cures blindness and hallucination; healed past\nfull, your max HP rises by 2 (5 if blessed).";
    case POT_HALLUCINATION: return "You hallucinate for a long while: monsters and items look random.";
    case POT_HEALING: return "Heals some HP and cures blindness (not if cursed); healed past\nfull, your max HP rises by 1.";
    case POT_RESTORE_ABILITY: return "Restores a lost attribute (blessed: all of them).  Cursed: nothing.";
    case POT_SLEEPING: return "Puts you to sleep for a while - unless you resist sleep or have\nfree action.";
    case POT_BLINDNESS: return "Blinds you for a long while (with telepathy, you then sense minds).";
    case POT_GAIN_ENERGY: return "Raises your maximum magic energy and refills some.  Cursed: drains it.";
    case POT_INVISIBILITY: return "Makes you invisible for a while (blessed: rarely for good).\nCursed: wakes monsters.  Shopkeepers won't let the invisible in.";
    case POT_MONSTER_DETECTION: return "Shows the monsters on this level (blessed: you keep sensing them\nfor a while).";
    case POT_OBJECT_DETECTION: return "Shows the objects on this level.";
    case POT_ENLIGHTENMENT: return "Tells you your intrinsics and condition (blessed: +1 Int and Wis).\nCursed: nothing.";
    case POT_FULL_HEALING: return "Heals fully and cures blindness and hallucination; healed past full,\nmax HP +4 (+8 blessed).  Blessed: restores a lost experience level.";
    case POT_LEVITATION: return "You float for a while: no going down stairs, no picking things up.\nBlessed: stop at will with >.  Cursed: stuck against the ceiling.";
    case POT_POLYMORPH: return "Turns you into a random monster for a while - armour may burst.";
    case POT_SPEED: return "Heals wounded legs; otherwise very fast for a while and, unless\ncursed, fast for good if you weren't.";
    case POT_ACID: return "Burns you unless you resist acid - and cures stoning.";
    case POT_OIL: return "Fuel: apply it to light it, or throw it lit as a firebomb.\nDrinking it unlit does no harm.";
    case POT_GAIN_ABILITY: return "Raises an attribute by 1 (blessed: all of them).  Cursed: nothing.";
    case POT_GAIN_LEVEL: return "Raises your experience level by one.  Cursed: you rise through\nthe ceiling to the level above.";
    case POT_PARALYSIS: return "Freezes you in place for a while - unless you have free action.";
    case POT_WATER: return "Holy water (blessed) blesses what you dip in it and cures\nlycanthropy if drunk; unholy water (cursed) curses.";
    /* scrolls */
    case SCR_ENCHANT_ARMOR: return "A piece of your worn armour +1 (blessed: up to +3).  Above +3\n(elven: +5) it may evaporate.  Cursed: -1.";
    case SCR_DESTROY_ARMOR: return "Destroys a piece of your worn armour, the cloak first.\nConfused: makes armour erodeproof instead.";
    case SCR_CONFUSE_MONSTER: return "Your hands glow: your next melee hit confuses the monster\n(blessed: several hits).";
    case SCR_SCARE_MONSTER: return "Lying on the floor, it keeps most monsters from attacking you in\nmelee while you stand on it.  Picked up a second time, it turns to dust.";
    case SCR_REMOVE_CURSE: return "Uncurses what you wear and wield (blessed: everything you carry).";
    case SCR_ENCHANT_WEAPON: return "Your wielded weapon +1 (blessed: up to +3); above +5 it may\nevaporate.  Cursed: -1.  Confused: makes it rustproof.";
    case SCR_CREATE_MONSTER: return "Creates a monster next to you.";
    case SCR_TAMING: return "Tames the monsters next to you (confused: a wider area).";
    case SCR_GENOCIDE: return "Wipes out a kind of monster for good (blessed: a whole class).\nCursed: brings them around you instead.";
    case SCR_LIGHT: return "Lights up the area around you (cursed: darkens it).";
    case SCR_TELEPORTATION: return "Teleports you somewhere on this level (confused: to another level).";
    case SCR_GOLD_DETECTION: return "Shows the gold on this level (confused: the traps).";
    case SCR_FOOD_DETECTION: return "Shows the food on this level.";
    case SCR_IDENTIFY: return "Identifies an item - sometimes several (blessed: more).";
    case SCR_MAGIC_MAPPING: return "Maps the whole level.";
    case SCR_AMNESIA: return "You forget the map of this level (maybe others) and some of what\nyou've identified.";
    case SCR_FIRE: return "A burst of fire centred on you (blessed: you pick the spot, five\ntimes stronger).  Confused: it only singes your hands.";
    case SCR_EARTH: return "Boulders fall around you - and on you, unless blessed.\nCursed: only on you.";
    case SCR_PUNISHMENT: return "Chains a heavy iron ball to you (blessed: nothing happens).";
    case SCR_CHARGING: return "Recharges a wand, tool or ring (blessed: more).  Confused:\nrestores your magic energy instead.";
    case SCR_STINKING_CLOUD: return "Places a poison cloud where you choose, nearby.";
    case SCR_MAIL: return "A letter for you.";
    case SCR_BLANK_PAPER: return "Blank: write a scroll on it with a magic marker.";
    /* wands */
    case WAN_LIGHT: return "Lights up the area around you.";
    case WAN_SECRET_DOOR_DETECTION: return "Reveals hidden doors, passages and traps near you.";
    case WAN_ENLIGHTENMENT: return "Tells you your intrinsics and condition.";
    case WAN_CREATE_MONSTER: return "Creates a monster next to you.";
    case WAN_WISHING: return "Zap it to wish for an item!";
    case WAN_NOTHING: return "Does nothing.";
    case WAN_STRIKING: return "A force bolt at a monster (magic resistance stops it); shatters\nboulders and statues.";
    case WAN_MAKE_INVISIBLE: return "Makes a monster invisible (zapped at yourself: you, for good).";
    case WAN_SLOW_MONSTER: return "Slows a monster down (zapped at yourself: you).";
    case WAN_SPEED_MONSTER: return "Speeds a monster up (zapped at yourself: fast for good).";
    case WAN_UNDEAD_TURNING: return "Hurts and frightens the undead; brings corpses back to life.";
    case WAN_POLYMORPH: return "Transforms a monster, or objects it hits (zapped at yourself: you).";
    case WAN_CANCELLATION: return "Strips the magic from a monster or item.  Never put it in a bag\nof holding - the bag explodes.";
    case WAN_TELEPORTATION: return "Sends a monster or object elsewhere (zapped at yourself: you).";
    case WAN_OPENING: return "Opens doors, locks and boxes; frees a monster - or you - from a trap.";
    case WAN_LOCKING: return "Locks doors and boxes; closes trap doors and holes.";
    case WAN_PROBING: return "Shows a monster's HP and what it carries (zapped at yourself:\nyour own).";
    case WAN_DIGGING: return "Digs a tunnel through rock; zapped down, a hole to the level below.";
    case WAN_MAGIC_MISSILE: return "A ray of magic missiles (magic resistance stops it).";
    case WAN_FIRE: return "A ray of fire.  Engraving with it burns words that won't smudge.";
    case WAN_COLD: return "A ray of cold; freezes water it crosses.";
    case WAN_SLEEP: return "A ray that puts monsters to sleep - one of the best escapes.";
    case WAN_DEATH: return "A death ray: kills most living things outright - not demons, the\nundead, or anything that resists magic or reflects.";
    case WAN_LIGHTNING: return "A ray of lightning that can blind.  Engraving with it burns words\nthat won't smudge (and blinds you).";
    /* rings */
    case RIN_ADORNMENT: return "Charisma up (or down) by its enchantment.";
    case RIN_GAIN_STRENGTH: return "Strength up (or down) by its enchantment.";
    case RIN_GAIN_CONSTITUTION: return "Constitution up (or down) by its enchantment.";
    case RIN_INCREASE_ACCURACY: return "You hit more often (or less) by its enchantment.";
    case RIN_INCREASE_DAMAGE: return "You hit harder (or softer) by its enchantment.";
    case RIN_PROTECTION: return "Better armour class by its enchantment.";
    case RIN_REGENERATION: return "Heals you 1 HP every turn; you get hungry faster.";
    case RIN_SEARCHING: return "Searches for hidden things around you every turn.";
    case RIN_STEALTH: return "Your steps don't wake sleeping monsters.";
    case RIN_SUSTAIN_ABILITY: return "Your attributes can't change - nothing lost, nothing gained.";
    case RIN_LEVITATION: return "You float while you wear it: no stairs down, no picking up.\nIf it's cursed, it won't come off.";
    case RIN_HUNGER: return "You get hungry much faster.";
    case RIN_AGGRAVATE_MONSTER: return "Wakes monsters and draws them to you.";
    case RIN_CONFLICT: return "Monsters near you fight each other - and you.  Uses up food.";
    case RIN_WARNING: return "Shows hostile monsters nearby as numbers - higher is deadlier.";
    case RIN_POISON_RESISTANCE: return "Protects you from poison.";
    case RIN_FIRE_RESISTANCE: return "Protects you from fire.";
    case RIN_COLD_RESISTANCE: return "Protects you from cold.";
    case RIN_SHOCK_RESISTANCE: return "Protects you from electricity.";
    case RIN_FREE_ACTION: return "You can't be paralysed or frozen in place.";
    case RIN_SLOW_DIGESTION: return "You get hungry very slowly.";
    case RIN_TELEPORTATION: return "Teleports you at random now and then.";
    case RIN_TELEPORT_CONTROL: return "You choose where you teleport.";
    case RIN_POLYMORPH: return "Polymorphs you at random now and then.";
    case RIN_POLYMORPH_CONTROL: return "You choose what you polymorph into.";
    case RIN_INVISIBILITY: return "Makes you invisible while you wear it.";
    case RIN_SEE_INVISIBLE: return "You see invisible things.";
    case RIN_PROTECTION_FROM_SHAPE_CHAN: return "Shapeshifters stay in their true form.";
    /* amulets */
    case AMULET_OF_ESP: return "Telepathy: you sense monsters' minds nearby - on the whole level\nwhile you're blind.";
    case AMULET_OF_LIFE_SAVING: return "If you die while wearing it, you live instead - once.";
    case AMULET_OF_STRANGULATION: return "Strangles you: take it off within 6 turns or die.";
    case AMULET_OF_RESTFUL_SLEEP: return "Makes you fall asleep now and then.";
    case AMULET_VERSUS_POISON: return "Protects you from poison.";
    case AMULET_OF_CHANGE: return "Changes your sex when you put it on - then it's gone.";
    case AMULET_OF_UNCHANGING: return "Keeps your form: you can't polymorph - or change back.";
    case AMULET_OF_REFLECTION: return "Reflects rays, gazes and breath attacks.";
    case AMULET_OF_MAGICAL_BREATHING: return "You need no air: no drowning, choking or poison gas.";
    case AMULET_OF_FLYING: return "You fly: over water, lava and holes.";
    case AMULET_OF_GUARDING: return "Armour class +2, and fewer special attacks get through\n(stronger magic cancellation).";
    case FAKE_AMULET_OF_YENDOR: return "A cheap plastic imitation of the Amulet of Yendor.";
    case AMULET_OF_YENDOR: return "The Amulet of Yendor - take it up to the Astral Plane.";
    default: return NULL;
    }
}

/* s181: what to do about an item you haven't identified */
static const char *
vitem_unknown_hint(struct obj *o)
{
    switch (o->oclass) {
    case POTION_CLASS: return "Unidentified potion - drinking it tells you what it was.";
    case SCROLL_CLASS: return "Unidentified scroll - reading it tells you what it was.";
    case WAND_CLASS: return "Unidentified wand - engrave with it (E) for a clue to what it is\n(that uses a charge).";
    case RING_CLASS: return "Unidentified ring - risky to try: a cursed ring won't come off.\nDrop it on an altar first to see if it's cursed.";
    case AMULET_CLASS: return "Unidentified amulet - risky to try: one kind strangles you.\nDrop it on an altar first, and never wear a cursed one.";
    default: return "Unidentified";
    }
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
    } else if (tk && vitem_effect(o->otyp)) { /* s181 */
        s += vitem_effect(o->otyp);
    } else if (!tk) {
        s += vitem_unknown_hint(o); /* s181 */
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
    /* s180: only what the hero knows.  The game's formula counts Luck, the
       enchantment of a weapon or accuracy ring nobody has identified, and a
       blessed weapon's bonus against the undead before its blessing is
       known; left in, the odds would give those hidden values away. */
    tmp -= (int) Luck;
    if (uwep && (uwep->oclass == WEAPON_CLASS || is_weptool(uwep))) {
        if (!uwep->known)
            tmp -= uwep->spe;
        if (uwep->blessed && !uwep->bknown
            && (is_undead(m->data) || is_demon(m->data) || is_vampshifter(m)))
            tmp -= 2;
    }
    if (uleft && uleft->otyp == RIN_INCREASE_ACCURACY && !uleft->known)
        tmp -= uleft->spe;
    if (uright && uright->otyp == RIN_INCREASE_ACCURACY && !uright->known)
        tmp -= uright->spe;
    *you_hit = (tmp - 1) * 5;
    /* negative AC: AC_VALUE rolls -rnd(-AC); use its average.  s180: its
       kind's level (the codex knows it) - this one's own level is hidden */
    t2 = (ac >= 0 ? ac : -((-ac + 1) / 2)) + 10 + m->data->mlevel;
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
                snprintf(ob, sizeof ob, "You hit it %d%%  -  crits %d%%+  -  Ctrl+click to aim", yh,
                         vulture_crit_chance_shown()); /* s156; s180: no Luck in it */
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
            /* s181: what it does, when you know what it is - one line,
               cut short at a word if long (the full text is on its
               inventory tooltip) */
            if (fo && !Hallucination && vitem_type_known(fo) && vitem_effect(fo->otyp)) {
                std::string e = vitem_effect(fo->otyp);
                size_t q;

                for (q = 0; q < e.size(); q++)
                    if (e[q] == '\n')
                        e[q] = ' ';
                if (e.size() > 100) {
                    q = e.rfind(' ', 96);
                    e = e.substr(0, q == std::string::npos ? 96 : q) + "...";
                }
                fs = e + "  -  click to pick it up";
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
    if (stage > 0 && (getenv("VULTURE_SELFTEST_FOG") || getenv("VULTURE_SELFTEST_COND")))
        return; /* probe: s187, s193 - only their own shots */
    if (!t0)
        t0 = SDL_GetTicks();
    if (SDL_GetTicks() - t0 < 1500 + 700u * stage)
        return;
    if (stage == 0) {
        vrt_probe_moves0 = svm.moves; /* probe: s140 */
        /* s182: the probes run unlit unless a light probe (or
           VULTURE_SELFTEST_LIT=1 for any run) asks - s189: kept so now that
           the game starts lit */
        vl_on = getenv("VULTURE_SELFTEST_LIGHT") || getenv("VULTURE_SELFTEST_PORTAL")
                || getenv("VULTURE_SELFTEST_LIT");
        if (getenv("VULTURE_SELFTEST_FLAT")) /* s185: lighting without the normal maps */
            vl_on = true, vl_relief_set(false);
        if (getenv("VULTURE_SELFTEST_KEYS")) /* probe: s195 */
            vkeys_probe(dir);
        if (getenv("VULTURE_SELFTEST_ZOOM")) /* probe: s201 */
            vzoom_probe_set(atoi(getenv("VULTURE_SELFTEST_ZOOM")));
        if (getenv("VULTURE_SELFTEST_COND")) { /* probe: s193 */
            vcond_probe_n = 0;
            vcond_probe_set(0);
            vcond_probe_t = SDL_GetTicks();
        }
        if (getenv("VULTURE_SELFTEST_FOG")) { /* probe: s187 */
            vfog_sx = u.ux, vfog_sy = u.uy, vfog_probe_n = 0;
            do_mapping();
            flush_screen(1);
            vfog_probe_t = SDL_GetTicks();
        }
    }
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
                struct monst *pm4;

                for (pm4 = fmon; pm4; pm4 = pm4->nmon) /* probe: s173 which pet came */
                    if (!DEADMONSTER(pm4) && pm4->mtame)
                        fprintf(f, "PET: %s\n", pmname(pm4->data, Mgender(pm4)));
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
        if (getenv("VULTURE_SELFTEST_MENUKEY")) { /* probe: s197 a menu opened by its key */
            vprobe_push_key(SDLK_i);
            vpause_probe_t = SDL_GetTicks(); /* s200: and shoot it */
            break;
        }
        if (getenv("VULTURE_SELFTEST_STATUSTIP")) { /* probe: s199 the panel's tooltips */
            vpn_probe(dir);
            break;
        }
        if (getenv("VULTURE_SELFTEST_SPELL")) { /* probe: s198 rays and explosions */
            vsp_probe_t = SDL_GetTicks() - 1500, vsp_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_IMPACT")) { /* probe: s202 blows, a flight, a shatter */
            vimp_probe_t = SDL_GetTicks(), vimp_probe_n = 0, vimp_probe_sub = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_DOOR")) { /* probe: s203 doors swinging, things used */
            vdr_probe_t = SDL_GetTicks(), vdr_probe_n = 0, vdr_probe_sub = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_GHOST")) { /* probe: s221 last seen */
            vghost_probe_t = SDL_GetTicks(), vghost_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_ROCK")) { /* probe: s218 the rock mass where you play */
            vrock_probe_t = SDL_GetTicks(), vrock_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_STAIRS")) { /* probe: s216 leaving, arriving */
            vstair_probe_t = SDL_GetTicks(), vstair_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_WATER")) { /* probe: s213 reflections */
            vrefl_probe_t = SDL_GetTicks(), vrefl_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_GRADE")) { /* probe: s212 the areas' colour */
            vgrade_probe_t = SDL_GetTicks(), vgrade_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_TORCH")) { /* probe: s211 torches */
            vtorch_probe_t = SDL_GetTicks(), vtorch_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_XRAY")) { /* probe: s210 creatures behind walls */
            vxr_probe_t = SDL_GetTicks(), vxr_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_WALLS")) { /* probe: s209 walls round you */
            vwall_probe_t = SDL_GetTicks(), vwall_probe_n = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_LID") || getenv("VULTURE_SELFTEST_ARC")) { /* probe: s207 lids, arcs */
            vlid_probe_t = SDL_GetTicks(), vlid_probe_n = 0, vlid_probe_sub = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_LIFE")) { /* probe: s206 creatures and furniture alive */
            vlife_probe_t = SDL_GetTicks(), vlife_probe_n = 0, vlife_probe_sub = 0;
            break;
        }
        if (getenv("VULTURE_SELFTEST_LOOK")) { /* probe: s205 the whole level known: far parts out of focus */
            do_mapping();
            vlook_probe_t = SDL_GetTicks();
            break;
        }
        if (getenv("VULTURE_SELFTEST_FALL")) { /* probe: s204 bodies falling, boulders rolling */
            vfall_probe_t = SDL_GetTicks(), vfall_probe_n = 0, vfall_probe_sub = 0;
            break;
        }
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
        vulture_save_frame_bmp(path);
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
            vulture_save_frame_bmp(fp2);
            vl_relief_set(true);
            vfx_shadows = false; /* probe: s191 the same frame without shadows */
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(fp2, sizeof fp2, "%s\\selftest_noshadow.bmp", dir);
            vulture_save_frame_bmp(fp2);
            vfx_shadows = true;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(fp2, sizeof fp2, "%s\\selftest_shadow.bmp", dir);
            vulture_save_frame_bmp(fp2);
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
            vulture_save_frame_bmp(fp3);
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
        if (getenv("VULTURE_SELFTEST_CONTROLS")) { /* probe: s175 the page (modal; the run ends here) */
            vpause_probe_t = SDL_GetTicks();
            vcontrols_page();
        }
        if (getenv("VULTURE_SELFTEST_DISCO")) { /* probe: s176 gold headings in a NetHack list */
            vpause_probe_t = SDL_GetTicks();
            (void) dodiscovered();
        }
        if (getenv("VULTURE_SELFTEST_LEVEL")) /* probe: s155 the real level-up path */
            pluslvl(FALSE);
        if (getenv("VULTURE_SELFTEST_PRAY")) { /* probe: s174 ok / soon / unlucky */
            const char *pm = getenv("VULTURE_SELFTEST_PRAY");

            u.uhp = 1; /* close to death */
            /* s180: ok = never prayed, 1500 turns in; soon = prayed 300
               turns ago; early = turn 1-3; angry = an angry god */
            if (strcmp(pm, "early"))
                svm.moves = 1500;
            if (!strcmp(pm, "soon"))
                gamelog_add(LL_MINORAC, 1200L, "prayed to the probe");
            if (!strcmp(pm, "angry"))
                u.ugangr = 1;
            disp.botl = TRUE;
        }
        if (getenv("VULTURE_SELFTEST_PHOTO")) /* probe: s170 the album polaroid */
            vulture_photo_fx(u.ux + 1, u.uy, PM_JACKAL, 10);
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
        if (getenv("VULTURE_SELFTEST_PAN")) { /* probe: s186 a fast middle-drag */
            SDL_Event e;
            int q;

            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEMOTION;
            e.motion.state = SDL_BUTTON_MMASK;
            e.motion.x = vulture_screen->w / 2, e.motion.y = vulture_screen->h / 2;
            for (q = 0; q < 300; q++) {
                e.motion.xrel = (q / 60) % 2 ? -5 : 5;
                e.motion.yrel = (q / 75) % 2 ? -2 : 2;
                SDL_PushEvent(&e);
            }
            vpan_probe_t0 = SDL_GetTicks(), vpan_probe_frames = 0, vpan_probe_round++;
            vpan_probe_dx = vpan_probe_dy = vpan_probe_moves = 0;
        }
        if (getenv("VULTURE_SELFTEST_APPROACH")) { /* probe: s184 step next to a hostile */
            int ex1 = u.ux + 1, ex2 = u.ux + 2, ey = u.uy;

            if (isok(ex2, ey) && ACCESSIBLE(levl[ex1][ey].typ) && ACCESSIBLE(levl[ex2][ey].typ)
                && !m_at(ex1, ey) && !m_at(ex2, ey)) {
                struct monst *jm = makemon(&mons[PM_JACKAL], ex2, ey, NO_MM_FLAGS);

                if (jm) {
                    jm->mpeaceful = 0, jm->msleeping = 0;
                    jm->mhpmax = jm->mhp = 40; /* it strikes, it doesn't die */
                    jm->movement = 2 * NORMAL_SPEED; /* and in the same turn as your step */
                    newsym(ex2, ey);
                }
                {
                    point sq; /* a real click on the square between (the keys
                                 turn with the view, so 'l' isn't map-east) */

                    sq.x = ex1, sq.y = ey;
                    vprobe_seq_on = true;
                    vprobe_push_click(sq);
                }
                vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                                  "approach: hero %d,%d, jackal %s at %d,%d, clicking %d,%d\n",
                                  u.ux, u.uy, jm ? "made" : "NOT made", ex2, ey, ex1, ey);
            } else {
                vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "approach: no room east\n");
            }
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
        if (getenv("VULTURE_SELFTEST_ITEMFX")) { /* probe: s181 a known item's hover line */
            int dx, dy;
            bool done = false;

            makeknown(POT_HEALING);
            for (dy = 1; dy >= -1 && !done; dy--) /* in front of the hero first */
                for (dx = 1; dx >= -1 && !done; dx--) {
                    coordxy fx = (coordxy) (u.ux + dx), fy = (coordxy) (u.uy + dy);

                    if ((dx || dy) && isok(fx, fy) && ACCESSIBLE(levl[fx][fy].typ)
                        && !m_at(fx, fy) && !OBJ_AT(fx, fy)) {
                        struct obj *po = mksobj_at(POT_HEALING, fx, fy, FALSE, FALSE);
                        point sq, mp;

                        po->dknown = 1;
                        newsym(fx, fy);
                        flush_screen(1); /* the map learns of it now, not next turn */
                        sq.x = fx, sq.y = fy;
                        mp = levwin->map_to_mouse(sq);
                        vulture_set_mouse_pos(mp.x, mp.y);
                        vhv = sq;
                        vhv_since = SDL_GetTicks() - 1000;
                        done = true;
                        {
                            char ip[700];
                            FILE *itf;
                            const char *ih = vsq_hint(fx, fy);

                            snprintf(ip, sizeof ip, "%s\\itemfx.txt", dir);
                            if ((itf = fopen(ip, "w")) != 0) {
                                fprintf(itf, "hero %d,%d potion %d,%d mouse %d,%d\nname [%s]\nhint [%s]\n",
                                        u.ux, u.uy, fx, fy, mp.x, mp.y,
                                        map_data->map_square_description(sq, 0).c_str(),
                                        ih ? ih : "(null)");
                                fclose(itf);
                            }
                        }
                    }
                }
            break;
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
        vulture_save_frame_bmp(path);
        if (getenv("VULTURE_SELFTEST_GPUDIFF")) { /* probe: s183 the same frame both ways */
            bool was = vulture_gpumap_wanted;

            vulture_gpumap_wanted = true;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(path, sizeof path, "%s\\gpudiff_gpu.bmp", dir);
            vulture_save_frame_bmp(path);
            vulture_gpumap_wanted = false;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(path, sizeof path, "%s\\gpudiff_soft.bmp", dir);
            vulture_save_frame_bmp(path);
            { /* and zoomed: 2x (exact blocks) and 1.5x (nearest, then smooth) */
                static const double zz[2] = { 2.0, 1.5 };
                int q;

                for (q = 0; q < 2; q++) {
                    vzoom = zz[q];
                    vulture_gpumap_wanted = true;
                    levwin->force_redraw();
                    ROOTWIN->draw_windows();
                    snprintf(path, sizeof path, "%s\\gpudiff_z%d_gpu.bmp", dir, q);
                    vulture_save_frame_bmp(path);
                    vulture_gpumap_wanted = false;
                    levwin->force_redraw();
                    ROOTWIN->draw_windows();
                    snprintf(path, sizeof path, "%s\\gpudiff_z%d_soft.bmp", dir, q);
                    vulture_save_frame_bmp(path);
                }
                vzoom = 1.0;
            }
            vulture_gpumap_wanted = was;
            levwin->force_redraw();
        }
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
        vulture_save_frame_bmp(path);
        vulture_aim_set(false, -1, -1);
        if (getenv("VULTURE_SELFTEST_CLICK")) /* probe: s145 pause menu */
            vprobe_push_key(SDLK_ESCAPE);
        if (getenv("VULTURE_SELFTEST_SPEED")) { /* probe: s144 Alt danger map */
            SDL_SetModState(KMOD_LALT);
            vhv.x = -1;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            snprintf(path, sizeof path, "%s\\selftest_alt.bmp", dir);
            vulture_save_frame_bmp(path);
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
            vulture_save_frame_bmp(path);
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


/*
 * IMPLEMENTATION_LOG s199: a status panel anyone can read.
 * The bottom-left panel was NetHack's own status line cut into a 5 x 5 grid
 * of words on stone - "St:18/16 In:14 HP:11(11) Pw:6(6) AC:9 Dlvl:1 $:0 T:3",
 * conditions clipped to "Hallu" and "FoodPois".  It is drawn here now, in the
 * newer HUD's style (vbox, gold): your name and rank, HP, power and
 * experience as labelled bars (HP shows what a blow just took, fading), the
 * six attributes and armor, gold, depth and turn spelled out, and conditions
 * as coloured pills - each with a plain-language tooltip (vulture_status_tip)
 * saying what it means and what to do.  Only what the status line already
 * showed (and the s63 prayer hunch): nothing hidden.
 */
struct vpn_hot_t {
    SDL_Rect r;
    std::string tip;
};
static std::vector<vpn_hot_t> vpn_hots;
static SDL_Rect vpn_rect = { 0, 0, 0, 0 };
static double vpn_ghost = -1.0; /* the HP the bar still shows as "just lost" */
static Uint32 vpn_ghost_t = 0;

static void
vpn_hot(int x, int y, int w, int h, const std::string &tip)
{
    vpn_hot_t t;

    t.r.x = x, t.r.y = y, t.r.w = w, t.r.h = h;
    t.tip = tip;
    vpn_hots.push_back(t);
}

/* the panel's size - the status window takes it (statuswin.cpp) */
void
vulture_status_panel_size(int *w, int *h)
{
    int lh = vulture_get_lineheight(V_FONT_SMALL);

    *w = V_UI(8 + 236 + 18 + 164 + 18 + 112 + 8);
    *h = V_UI(7) + lh + V_UI(5 + 16 + 5 + 12 + 5 + 6 + 8) + lh + V_UI(6 + 6);
}

/* the portrait's frame, in the same style (statuswin.cpp vpt_draw) */
void
vulture_hud_frame(int x, int y, int w, int h)
{
    vbox(x, y, w, h, 205);
    vtint(x, y, w, 1, 255, 200, 70, 110);
}

static void
vpn_text(const std::string &s, int x, int y, Uint8 r, Uint8 g, Uint8 b)
{
    vulture_put_text_shadow(V_FONT_SMALL, s, vulture_screen, x, y,
                            SDL_MapRGB(vulture_screen->format, r, g, b),
                            SDL_MapRGB(vulture_screen->format, 0, 0, 0));
}

/* s cut down to fit w px */
static std::string
vpn_fit(std::string s, int w)
{
    while (s.size() > 3 && vulture_text_length(V_FONT_SMALL, s) > w)
        s.resize(s.size() - 1);
    return s;
}

/* a bar over (br, bgc, bb): f of it in (r, g, b) with a sheen on top */
static void
vpn_bar(int x, int y, int w, int h, double f, Uint8 r, Uint8 g, Uint8 b, Uint8 br, Uint8 bgc, Uint8 bb)
{
    SDL_Rect rc;
    int fw;

    if (f < 0.0)
        f = 0.0;
    if (f > 1.0)
        f = 1.0;
    rc.x = x, rc.y = y, rc.w = w, rc.h = h;
    SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, br, bgc, bb));
    fw = (int) (w * f + 0.5);
    if (fw > 0) {
        rc.w = fw;
        SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, r, g, b));
        vtint(x, y, fw, h * 2 / 5 > 1 ? h * 2 / 5 : 1, 255, 255, 255, 50);
    }
    vtint(x, y + h - 1, w, 1, 0, 0, 0, 110);
}

/* a pill; returns its width */
static int
vpn_pill(int x, int y, int h, const char *txt, Uint8 r, Uint8 g, Uint8 b, bool urgent, const std::string &tip)
{
    int lh = vulture_get_lineheight(V_FONT_SMALL), w = vulture_text_length(V_FONT_SMALL, txt) + V_UI(14);
    int a = urgent ? (int) (60 + 50 * (0.5 + 0.5 * sin(SDL_GetTicks() / 140.0))) : 55;

    vtint(x, y, w, h, r, g, b, a);
    vtint(x, y, w, 1, r, g, b, 180);
    vtint(x, y + h - 1, w, 1, r, g, b, 180);
    vtint(x, y + 1, 1, h - 2, r, g, b, 180);
    vtint(x + w - 1, y + 1, 1, h - 2, r, g, b, 180);
    vpn_text(txt, x + V_UI(7), y + (h - lh) / 2, (Uint8) ((r + 255) / 2), (Uint8) ((g + 255) / 2),
             (Uint8) ((b + 255) / 2));
    vpn_hot(x, y, w, h, tip);
    return w;
}

static std::string
vpn_strength(void)
{
    char b[16];
    int s = ACURR(A_STR);

    if (s > 18) {
        if (s > STR18(100))
            snprintf(b, sizeof b, "%d", s - 100);
        else if (s < STR18(100))
            snprintf(b, sizeof b, "18/%02d", s - 18);
        else
            snprintf(b, sizeof b, "18/**");
    } else
        snprintf(b, sizeof b, "%d", s);
    return b;
}

void
vulture_status_panel_draw(int x, int y, int w, int h)
{
    static int last_hp = -1;
    static Uint32 last_t = 0;
    int lh = vulture_get_lineheight(V_FONT_SMALL), pad = V_UI(8);
    int lx = x + pad, lw = V_UI(236), mx = lx + lw + V_UI(18), mw = V_UI(164), rx = mx + mw + V_UI(18);
    int rw = x + w - pad - rx;
    int ny = y + V_UI(7);
    int hpy = ny + lh + V_UI(5), hph = V_UI(16);
    int pwy = hpy + hph + V_UI(5), pwh = V_UI(12);
    int xpy = pwy + pwh + V_UI(5), xph = V_UI(6);
    int ply = xpy + xph + V_UI(8), plh = lh + V_UI(6);
    int hp = Upolyd ? u.mh : u.uhp, hpmax = Upolyd ? u.mhmax : u.uhpmax;
    int rowy[4], k, px, bw;
    Uint32 now = SDL_GetTicks();
    double dt = last_t ? (now - last_t) / 1000.0 : 0.0;
    char b[400];
    std::string s, tip;

    last_t = now;
    if (dt > 0.1)
        dt = 0.1;
    vpn_hots.clear();
    vpn_rect.x = x, vpn_rect.y = y, vpn_rect.w = w, vpn_rect.h = h;
    if (!u.ux)
        return;
    vbox(x, y, w, h, 205);
    vtint(x, y, w, 1, 255, 200, 70, 110); /* a gold hairline along the top */
    /* the middle and right columns: four rows spread evenly over the bars' band */
    for (k = 0; k < 4; k++)
        rowy[k] = ny + k * (xpy + xph - ny - lh) / 3;

    /* your name and rank, your level */
    s = svp.plname;
    if (!s.empty())
        s[0] = (char) toupper((unsigned char) s[0]);
    if (Upolyd) {
        std::string mn = pmname(&mons[u.umonnum], Ugender);

        if (!mn.empty())
            mn[0] = (char) toupper((unsigned char) mn[0]);
        s += " the " + mn;
    } else
        s += std::string(" the ") + rank_of(u.ulevel, Role_switch, flags.female);
    snprintf(b, sizeof b, "Level %d", u.ulevel);
    bw = vulture_text_length(V_FONT_SMALL, b);
    vpn_text(vpn_fit(s, lw - bw - V_UI(10)), lx, ny, 255, 200, 70);
    vpn_text(b, lx + lw - bw, ny, 235, 230, 220);
    vpn_hot(lx, ny, lw - bw - V_UI(6), lh,
            Upolyd ? "Your name - and what you've turned into for now."
                   : "Your name and rank.  The rank rises with your experience level.");
    if (u.ulevel < MAXULEV) {
        long need = newuexp(u.ulevel) - u.uexp;

        snprintf(b, sizeof b, "Experience level %d.  %ld more experience point%s to reach level %d:\n"
                 "more hit points and power, and better at everything.  You gain experience\n"
                 "mostly by defeating monsters.", u.ulevel, need, need == 1 ? "" : "s", u.ulevel + 1);
    } else
        snprintf(b, sizeof b, "Experience level %d - as high as it goes.", u.ulevel);
    vpn_hot(lx + lw - bw, ny, bw, lh, b);

    /* HP: the part a blow just took stays pale a moment, then drains away */
    if (last_hp >= 0 && hp < last_hp)
        vpn_ghost_t = now;
    if (vpn_ghost < hp)
        vpn_ghost = hp;
    else if (now - vpn_ghost_t > 450) {
        vpn_ghost -= (vpn_ghost - hp) * (dt * 4.0 > 1.0 ? 1.0 : dt * 4.0) + 0.2;
        if (vpn_ghost < hp)
            vpn_ghost = hp;
    }
    last_hp = hp;
    {
        double f = hpmax > 0 ? (double) hp / hpmax : 0.0, fg = hpmax > 0 ? vpn_ghost / hpmax : 0.0;
        Uint8 r = 70, g = 190, bb = 80;
        SDL_Rect rc;

        if (f <= 0.3)
            r = 215, g = 50, bb = 40;
        else if (f <= 0.6)
            r = 225, g = 175, bb = 45;
        if (f <= 0.25) { /* in danger: it throbs */
            double pu = 0.78 + 0.22 * sin(now / 120.0);

            r = (Uint8) (r * pu), g = (Uint8) (g * pu), bb = (Uint8) (bb * pu);
        }
        vpn_bar(lx, hpy, lw, hph, 0.0, 0, 0, 0, 40, 12, 12);
        if (fg > f) {
            rc.x = lx + (int) (lw * (f < 0 ? 0 : f)), rc.y = hpy;
            rc.w = (int) (lw * (fg > 1 ? 1 : fg)) - (rc.x - lx), rc.h = hph - 1;
            if (rc.w > 0)
                SDL_FillRect(vulture_screen, &rc, SDL_MapRGB(vulture_screen->format, 250, 215, 195));
        }
        vpn_bar(lx, hpy, (int) (lw * (f < 0 ? 0 : f > 1 ? 1 : f) + 0.5), hph, 1.0, r, g, bb, r, g, bb);
        vpn_text("HP", lx + V_UI(6), hpy + (hph - lh) / 2, 255, 255, 255);
        snprintf(b, sizeof b, "%d / %d", hp, hpmax);
        bw = vulture_text_length(V_FONT_SMALL, b);
        vpn_text(b, lx + lw - bw - V_UI(6), hpy + (hph - lh) / 2, 255, 255, 255);
        snprintf(b, sizeof b, "Hit points: %d of %d.  At 0 you die.\n"
                 "They come back slowly as you rest or walk, faster with healing.%s", hp, hpmax,
                 Upolyd ? "\nYou're transformed: if these run out you turn back into yourself." : "");
        vpn_hot(lx, hpy, lw, hph, b);
    }

    /* power */
    vpn_bar(lx, pwy, lw, pwh, u.uenmax > 0 ? (double) u.uen / u.uenmax : 0.0, 70, 125, 245, 12, 16, 42);
    vpn_text("Power", lx + V_UI(6), pwy + (pwh - lh) / 2, 230, 236, 255);
    snprintf(b, sizeof b, "%d / %d", u.uen, u.uenmax);
    bw = vulture_text_length(V_FONT_SMALL, b);
    vpn_text(b, lx + lw - bw - V_UI(6), pwy + (pwh - lh) / 2, 230, 236, 255);
    snprintf(b, sizeof b, "Power: %d of %d.  Spent to cast spells (Z) and use some abilities;\n"
             "it comes back over time.", u.uen, u.uenmax);
    vpn_hot(lx, pwy, lw, pwh, b);

    /* experience: how far into this level */
    {
        long lo = newuexp(u.ulevel - 1), hi = newuexp(u.ulevel);
        double f = u.ulevel >= MAXULEV ? 1.0 : hi > lo ? (double) (u.uexp - lo) / (hi - lo) : 0.0;

        vpn_bar(lx, xpy, lw, xph, f, 255, 200, 70, 40, 32, 12);
        if (u.ulevel < MAXULEV)
            snprintf(b, sizeof b, "Experience: %ld points.  %ld more to reach level %d.", u.uexp,
                     hi - u.uexp, u.ulevel + 1);
        else
            snprintf(b, sizeof b, "Experience: %ld points - the highest level.", u.uexp);
        vpn_hot(lx, xpy - V_UI(2), lw, xph + V_UI(4), b);
    }

    /* alignment, the six attributes */
    {
        static const char *const lab[6] = { "STR", "DEX", "CON", "INT", "WIS", "CHA" };
        static const char *const what[6] = {
            "Strength: how hard you hit in melee and how much you can carry.",
            "Dexterity: how often you hit, above all with thrown and fired weapons.",
            "Constitution: how many hit points each level brings, and how much you can carry.",
            "Intelligence: spellcasting for most classes - wizards above all.",
            "Wisdom: how fast your power comes back; spellcasting for priests,\nhealers, knights and monks.",
            "Charisma: what shopkeepers charge you, and how well you get on with others."
        };
        static const int attr[6] = { A_STR, A_DEX, A_CON, A_INT, A_WIS, A_CHA };
        const char *al = u.ualign.type == A_CHAOTIC ? "Chaotic" : u.ualign.type == A_NEUTRAL ? "Neutral" : "Lawful";
        Uint8 ar = u.ualign.type == A_CHAOTIC ? 235 : u.ualign.type == A_NEUTRAL ? 185 : 235;
        Uint8 ag = u.ualign.type == A_CHAOTIC ? 125 : u.ualign.type == A_NEUTRAL ? 205 : 220;
        Uint8 ab = u.ualign.type == A_CHAOTIC ? 115 : u.ualign.type == A_NEUTRAL ? 230 : 150;

        vpn_text(al, mx, ny, ar, ag, ab);
        snprintf(b, sizeof b, "Your alignment: %s.  Your god favours %s deeds, and an altar\n"
                 "of your own alignment is where to pray and sacrifice.", al,
                 u.ualign.type == A_CHAOTIC ? "chaotic" : u.ualign.type == A_NEUTRAL ? "balanced" : "lawful");
        vpn_hot(mx, ny, vulture_text_length(V_FONT_SMALL, al), lh, b);
        for (k = 0; k < 6; k++) {
            int cx = mx + (k / 3) * (mw / 2), cy = rowy[1 + k % 3];
            std::string v = k == 0 ? vpn_strength() : std::to_string((int) ACURR(attr[k]));

            vpn_text(lab[k], cx, cy, 150, 145, 135);
            vpn_text(v, cx + V_UI(36), cy, 240, 235, 225);
            vpn_hot(cx, cy, mw / 2 - V_UI(4), lh, what[k]);
        }
    }

    /* depth, armor, gold, turn */
    {
        std::string dn = svd.dungeons[u.uz.dnum].dname;
        long gold = money_cnt(gi.invent);
        const char *lab[4] = { "Depth", "Armor", "Gold", "Turn" };
        std::string val[4], tips[4];

        val[0] = std::to_string(depth(&u.uz));
        snprintf(b, sizeof b, "You are on level %d of %s.\nStairs down lead deeper; the Amulet lies far below.",
                 dunlev(&u.uz), dn.c_str());
        tips[0] = b;
        val[1] = std::to_string(u.uac);
        tips[1] = "Armor class: LOWER is better.  Each point lower makes monsters miss\n"
                  "you more often; below 0 it also softens their blows.  Wear armor to lower it.";
        val[2] = gold >= 100000L ? std::to_string(gold / 1000) + "k" : std::to_string(gold);
        tips[2] = "Gold: spend it in shops, or donate it to a temple priest.";
        val[3] = flags.time ? std::to_string(svm.moves) : std::string("");
        tips[3] = "Game turns so far.";
        for (k = 0; k < 4; k++) {
            if (val[k].empty())
                continue;
            vpn_text(lab[k], rx, rowy[k], 150, 145, 135);
            bw = vulture_text_length(V_FONT_SMALL, val[k]);
            vpn_text(val[k], rx + rw - bw, rowy[k], k == 2 ? 255 : 240, k == 2 ? 215 : 235, k == 2 ? 90 : 225);
            vpn_hot(rx, rowy[k], rw, lh, tips[k]);
        }
    }

    /* conditions, as pills; the quiver on the right */
    px = lx;
    {
        struct vpn_cond { const char *txt; Uint8 r, g, b; bool urgent; const char *tip; };
        std::vector<vpn_cond> cs;
        int cap = near_capacity();
        long since = vq_turns_since_prayer();
        int qw = 0;
        std::string q;

        if (Stoned)
            cs.push_back({ "Turning to stone", 230, 60, 50, true,
                           "You'll be a statue in a few turns!  Eat a lizard corpse or an acidic\n"
                           "corpse, drink acid, or pray." });
        if (Slimed)
            cs.push_back({ "Turning into slime", 230, 60, 50, true,
                           "You're turning into green slime!  Burn it off with fire, or pray." });
        if (Strangled)
            cs.push_back({ "Being strangled", 230, 60, 50, true,
                           "You're choking!  Take off your amulet (R) - it's strangling you - or pray." });
        if (Sick && (u.usick_type & SICK_VOMITABLE))
            cs.push_back({ "Food poisoning", 230, 60, 50, true,
                           "Deadly in a few turns!  Pray, apply a unicorn horn, eat a eucalyptus\n"
                           "leaf, or drink a potion of extra or full healing." });
        if (Sick && (u.usick_type & SICK_NONVOMITABLE))
            cs.push_back({ "Terminally ill", 230, 60, 50, true,
                           "Deadly in a few turns!  Pray, apply a unicorn horn, eat a eucalyptus\n"
                           "leaf, or drink a potion of extra or full healing." });
        if (u.uhs >= FAINTING)
            cs.push_back({ "Fainting", 230, 60, 50, true,
                           "You keep passing out from hunger.  Eat something NOW, or pray." });
        else if (u.uhs == WEAK)
            cs.push_back({ "Weak", 240, 140, 50, false,
                           "Starving: eat soon (E).  Your strength is down until you do." });
        else if (u.uhs == HUNGRY)
            cs.push_back({ "Hungry", 235, 200, 70, false, "Hungry: eat something soon (E)." });
        else if (u.uhs == SATIATED)
            cs.push_back({ "Satiated", 150, 190, 120, false,
                           "Full.  Eating much more could make you choke to death." });
        if (cap >= EXT_ENCUMBER)
            cs.push_back({ cap == OVERLOADED ? "Overloaded" : "Overtaxed", 230, 60, 50, false,
                           "Far too heavy: you can hardly move.  Drop things (d or D)." });
        else if (cap == HVY_ENCUMBER)
            cs.push_back({ "Strained", 240, 140, 50, false,
                           "Very heavy: very slow, and you don't heal while walking.  Drop something." });
        else if (cap == MOD_ENCUMBER)
            cs.push_back({ "Stressed", 240, 160, 60, false,
                           "Heavy: slower, and you don't heal while walking.  Drop something heavy." });
        else if (cap == SLT_ENCUMBER)
            cs.push_back({ "Burdened", 235, 200, 70, false,
                           "Carrying a lot: you move a little slower." });
        if (Blind)
            cs.push_back({ "Blind", 200, 160, 255, false,
                           "You can't see.  You can still feel your way and fight what you bump into." });
        if (Deaf)
            cs.push_back({ "Deaf", 180, 180, 200, false, "You can't hear." });
        if (Confusion)
            cs.push_back({ "Confused", 200, 160, 255, false,
                           "Your steps may go astray, and some scrolls and spells act strangely." });
        if (Stunned)
            cs.push_back({ "Stunned", 200, 160, 255, false,
                           "You stagger in random directions.  Wait (s) for it to pass." });
        if (Hallucination)
            cs.push_back({ "Hallucinating", 220, 130, 255, false,
                           "Monsters and items look like random things.  It wears off." });
        if (Levitation)
            cs.push_back({ "Levitating", 120, 200, 255, false,
                           "You float: you can't pick things up or go down stairs." });
        else if (Flying)
            cs.push_back({ "Flying", 120, 200, 255, false, "You fly over water, lava and traps." });
        if (u.usteed)
            cs.push_back({ "Riding", 170, 200, 150, false, "You're riding your steed." });
        if (u.ugangr) /* s63's hunch, as a pill */
            cs.push_back({ "God angry", 230, 60, 50, false,
                           "Your god is angry with you: praying now makes things worse.\n"
                           "Make amends first - sacrifice fresh corpses at an altar of your god." });
        else if (since >= 1000L)
            cs.push_back({ "Can pray", 110, 210, 120, false,
                           "A long time since you last prayed (or you never have).  If you're in real\n"
                           "trouble - very low HP, weak from hunger, ill - praying should help.\n"
                           "(A hunch: the real wait is hidden and varies.)" });
        else if (since >= 500L)
            cs.push_back({ "Pray?", 235, 200, 70, false,
                           "A while since you last prayed: praying now might be too soon.\n"
                           "Only risk it if you'd die otherwise." });
        /* the quiver */
        if (uquiver) {
            q = std::string("Quiver: ") + std::to_string(uquiver->quan) + " " + simpleonames(uquiver);
            snprintf(b, sizeof b, "What you fire with f: %ld left.%s", uquiver->quan,
                     uquiver->quan <= 5 ? "  Running low!" : "");
        } else {
            q = "Quiver: empty";
            snprintf(b, sizeof b, "Nothing to fire: put arrows, darts, daggers or rocks in your quiver with Q.");
        }
        q = vpn_fit(q, V_UI(180));
        qw = vulture_text_length(V_FONT_SMALL, q);
        vpn_text(q, x + w - pad - qw, ply + (plh - lh) / 2, uquiver && uquiver->quan <= 5 ? 235 : 190,
                 uquiver && uquiver->quan <= 5 ? 200 : 185, uquiver && uquiver->quan <= 5 ? 70 : 175);
        vpn_hot(x + w - pad - qw, ply, qw, plh, b);
        for (k = 0; k < (int) cs.size(); k++) {
            int pw2 = vulture_text_length(V_FONT_SMALL, cs[k].txt) + V_UI(14);

            if (px + pw2 > x + w - pad - qw - V_UI(10))
                break;
            px += vpn_pill(px, ply, plh, cs[k].txt, cs[k].r, cs[k].g, cs[k].b, cs[k].urgent, cs[k].tip)
                  + V_UI(5);
        }
    }
}

/* probe: s199 - every hot spot answers with its own tip; then the real hover
   path over the Armor row (shot as selftest_pause.bmp) */
bool vulture_status_tip(int mx, int my, std::string &tip);

static void
vpn_probe(const char *dir)
{
    char path[700];
    FILE *o;
    size_t i;
    std::string t;
    int bad = 0;

    snprintf(path, sizeof path, "%s\\statustip.txt", dir);
    if (!(o = fopen(path, "w")))
        return;
    fprintf(o, "panel %d,%d %dx%d, %d hot spots\n", vpn_rect.x, vpn_rect.y, vpn_rect.w, vpn_rect.h,
            (int) vpn_hots.size());
    for (i = 0; i < vpn_hots.size(); i++) {
        const SDL_Rect r = vpn_hots[i].r;
        bool in = vulture_status_tip(r.x + r.w / 2, r.y + r.h / 2, t);
        std::string first = vpn_hots[i].tip.substr(0, vpn_hots[i].tip.find('\n'));
        bool ok = in && t == vpn_hots[i].tip;

        bad += !ok;
        fprintf(o, "%4d,%3d %3dx%2d %s | %s\n", r.x - vpn_rect.x, r.y - vpn_rect.y, r.w, r.h,
                ok ? "ok  " : "MISS", first.c_str());
    }
    fprintf(o, "outside the panel: %s\n",
            vulture_status_tip(vpn_rect.x - 50, vpn_rect.y - 50, t) ? "WRONG" : "ok");
    for (i = 0; i < vpn_hots.size(); i++)
        if (vpn_hots[i].tip.compare(0, 11, "Armor class") == 0)
            break;
    if (i < vpn_hots.size()) {
        const SDL_Rect r = vpn_hots[i].r;

        vulture_cam_follow = true; /* the old hover path switched it off */
        vulture_set_mouse_pos(r.x + r.w / 2, r.y + r.h / 2);
        levwin->handle_timer_event(stwin, NULL, 5000);
        vpause_probe_t = SDL_GetTicks();
        fprintf(o, "hovered the Armor row: see selftest_pause.bmp; camera follow %s\n",
                vulture_cam_follow ? "still on" : "OFF");
    }
    fprintf(o, "%d of %d hot spots wrong\n", bad, (int) vpn_hots.size());
    fclose(o);
}

/* the hover chain (handle_timer_event): over the panel - its tooltip, if any */
bool
vulture_status_tip(int mx, int my, std::string &tip)
{
    size_t i;

    tip.clear();
    if (!stwin || !stwin->visible || !vpn_rect.w || mx < vpn_rect.x || my < vpn_rect.y
        || mx >= vpn_rect.x + vpn_rect.w || my >= vpn_rect.y + vpn_rect.h)
        return false;
    for (i = 0; i < vpn_hots.size(); i++) {
        const SDL_Rect &r = vpn_hots[i].r;

        if (mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h) {
            tip = vpn_hots[i].tip;
            break;
        }
    }
    return true;
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

/*
 * IMPLEMENTATION_LOG s170: a new species in the Tourist's photo album.
 * The camera flashes - white light bursts where the monster stands and the
 * whole screen flashes briefly - and a polaroid slides in on the right with
 * the monster's picture, its name and the album count; milestone counts
 * (10 / 25 / 50 / 100) get a gold frame.  It holds, then slides back out.
 */
static Uint32 vphoto_t0 = 0;
static int vphoto_mndx = -1, vphoto_count = 0;

extern "C" void
vulture_photo_fx(int x, int y, int mndx, int count)
{
    if (!levwin)
        return;
    vphoto_t0 = SDL_GetTicks(), vphoto_mndx = mndx, vphoto_count = count;
    if (isok(x, y))
        vl_flash(x, y, 1.0f, 1.0f, 1.0f, 1.6f, 4.0f, 350);
    vulture_sfx("camera");
}

static void
vphoto_draw(int ax, int ay, int aw, int ah)
{
    Uint32 age;
    int cw = V_UI(170), ch = V_UI(196), px, py, slide, tile;
    bool gold;

    if (!vphoto_t0 || vphoto_mndx < 0 || !ismnum(vphoto_mndx))
        return;
    age = SDL_GetTicks() - vphoto_t0;
    if (age > 3400) {
        vphoto_t0 = 0;
        return;
    }
    if (age < 160) /* the flash */
        vtint(ax, ay, aw, ah, 255, 255, 255, (int) (170 * (1.0 - age / 160.0)));
    gold = vphoto_count == 10 || vphoto_count == 25 || vphoto_count == 50 || vphoto_count == 100;
    /* slide in over 260 ms (eased), hold, slide out over the last 300 */
    if (age < 260) {
        double t = age / 260.0;

        slide = (int) ((cw + V_UI(30)) * (1.0 - t) * (1.0 - t));
    } else if (age > 3100) {
        double t = (age - 3100) / 300.0;

        slide = (int) ((cw + V_UI(30)) * t * t);
    } else {
        slide = 0;
    }
    px = ax + aw - cw - V_UI(18) + slide;
    py = ay + ah / 2 - ch / 2 - V_UI(40);
    /* the frame: a drop shadow, white (or gold) card, dark photo window */
    vtint(px + 4, py + 5, cw, ch, 0, 0, 0, 110);
    if (gold)
        vtint(px, py, cw, ch, 245, 215, 120, 255);
    else
        vtint(px, py, cw, ch, 244, 240, 228, 255);
    {
        int wx = px + V_UI(10), wy = py + V_UI(10), ww = cw - V_UI(20), wh = ch - V_UI(58);
        vulture_tile *t;

        vtint(wx, wy, ww, wh, 34, 30, 28, 255);
        tile = MONSTER_TO_VTILE(vphoto_mndx);
        if ((t = vulture_get_tile(tile)) != NULL && t->graphic) {
            double sc = (double) (wh - V_UI(8)) / (t->graphic->h > 0 ? t->graphic->h : 1);

            if (sc > 1.6)
                sc = 1.6;
            /* stand it on the window's floor, centred */
            vtx_blit(t->graphic, wx + ww / 2 - t->graphic->w / 2, wy + wh - V_UI(6) - t->graphic->h,
                     sc, sc, 0.0, false, 255);
        }
        /* a developing-photo sheen in the first half second */
        if (age < 700)
            vtint(wx, wy, ww, wh, 255, 250, 235, (int) (200 * (1.0 - age / 700.0)));
    }
    {
        char l1[64], l2[64];
        const char *nm = mons[vphoto_mndx].pmnames[NEUTRAL];
        Uint32 ink = SDL_MapRGB(vulture_screen->format, 40, 34, 30);
        Uint32 none = SDL_MapRGB(vulture_screen->format, 0, 0, 0);

        snprintf(l1, sizeof l1, "%s", gold ? "MILESTONE!" : "NEW PHOTO");
        snprintf(l2, sizeof l2, "%s  -  %d species", nm ? nm : "?", vphoto_count);
        vulture_put_text(V_FONT_SMALL, l1, vulture_screen,
                         px + cw / 2 - vulture_text_length(V_FONT_SMALL, l1) / 2, py + ch - V_UI(44), ink);
        vulture_put_text(V_FONT_SMALL, l2, vulture_screen,
                         px + cw / 2 - vulture_text_length(V_FONT_SMALL, l2) / 2, py + ch - V_UI(26), ink);
        nhUse(none);
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
static SDL_Rect vpray_rect = { 0, 0, 0, 0 }; /* s174 (s180: no luck badge - Luck is hidden) */
static int vpray_adv = 0;
static double vhero_speed(void);

/* s180: worn stealth the hero has identified.  Putting on elven boots, an
   elven cloak or a ring of stealth while already stealthy says nothing and
   identifies nothing, so "by what you wear" would give the item away. */
static bool
vworn_known_stealth(void)
{
    return (uarmf && uarmf->otyp == ELVEN_BOOTS && objects[ELVEN_BOOTS].oc_name_known)
           || (uarmc && uarmc->otyp == ELVEN_CLOAK && objects[ELVEN_CLOAK].oc_name_known)
           || (uleft && uleft->otyp == RIN_STEALTH && objects[RIN_STEALTH].oc_name_known)
           || (uright && uright->otyp == RIN_STEALTH && objects[RIN_STEALTH].oc_name_known)
           || (EStealth & (W_ART | W_ARTI)) != 0; /* an artifact's powers come with its name */
}

static const char *
vstealth_state(Uint8 *r, Uint8 *g, Uint8 *b, std::string *why)
{
    bool aggr_known = HAggravate_monster
        || (uleft && uleft->otyp == RIN_AGGRAVATE_MONSTER
            && objects[RIN_AGGRAVATE_MONSTER].oc_name_known)
        || (uright && uright->otyp == RIN_AGGRAVATE_MONSTER
            && objects[RIN_AGGRAVATE_MONSTER].oc_name_known);

    if (Stealth) {
        bool worn = EStealth && vworn_known_stealth(); /* s180 */

        *why = HStealth ? "Stealthy by nature" : worn ? "Stealthy thanks to something you wear"
                                                      : "Stealthy";
        if (HStealth && worn)
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
    vpray_rect.w = 0;
    { /* s174: in serious trouble - will a prayer save you? */
        const char *why, *tr;
        int adv = vulture_prayer_advice(&why, &tr);

        if (adv) { /* s180: PRAY NOW / PRAY? (maybe too soon) / DON'T PRAY */
            const char *pl = adv == 1 ? "PRAY NOW" : adv == 3 ? "PRAY?" : "DON'T PRAY";
            int tw3 = vulture_text_length(V_FONT_SMALL, pl);
            double pu = 0.5 + 0.5 * sin(SDL_GetTicks() / (adv == 1 ? 160.0 : 320.0));
            Uint8 pr = adv == 1 ? 120 : 255, pg = adv == 1 ? 240 : adv == 3 ? 205 : 90,
                  pb = adv == 1 ? 140 : 70;
            SDL_Rect r3 = { (Sint16) rc.x, (Sint16) rc.y, (Uint16) (tw3 + 16), (Uint16) rc.h };

            vbox(r3.x, r3.y, r3.w, r3.h, 220);
            {
                SDL_Rect bar = { (Sint16) r3.x, (Sint16) (r3.y + r3.h - 3), (Uint16) r3.w, 3 };

                SDL_FillRect(vulture_screen, &bar,
                             SDL_MapRGB(vulture_screen->format, (Uint8) (pr * (0.6 + 0.4 * pu)),
                                        (Uint8) (pg * (0.6 + 0.4 * pu)), (Uint8) (pb * (0.6 + 0.4 * pu))));
            }
            vulture_put_text_shadow(V_FONT_SMALL, pl, vulture_screen, r3.x + 8, r3.y + 2,
                                    SDL_MapRGB(vulture_screen->format, pr, pg, pb),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
            vpray_rect = r3, vpray_adv = adv;
            vbadge_row_x += r3.w + 4;
            rc.x += r3.w + 4;
        }
    }
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
      "Hover anything to learn about it - hold Shift on a monster for its\n"
      "codex.  Esc, then Controls, lists every shortcut." },
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
    if (!fight && vghost_fight()) /* s221: one stepped out of sight - still a fight */
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
            if (!fight && vghost_fight()) /* s221 */
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

/* vl_on (s182; s189: lighting on by default) is defined near vselftest */
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
    fprintf(f, "lighting=%d\n", (int) vl_on); /* s182 */
    fprintf(f, "gpumap=%d\n", (int) vulture_gpumap_wanted); /* s183 */
    fprintf(f, "mist=%d\n", (int) vfx_mist);                /* s192 */
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
        else if (!strcmp(ln, "lighting")) /* s182 */
            vl_on = v != 0;
        else if (!strcmp(ln, "gpumap")) /* s183 */
            vulture_gpumap_wanted = v != 0;
        else if (!strcmp(ln, "mist")) /* s192 */
            vfx_mist = v != 0;
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
        const char *nm[6] = { "Normal-mapped lighting", "Shadows (cast by lights too)", "Atmosphere particles",
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
        { /* s182: dynamic lighting (s189: on by default) */
            static char ll[96];

            snprintf(ll, sizeof ll, "Dynamic lighting  -  %s  (F9 cycles it too)",
                     !vl_on ? "off" : vl_relief_get() ? "full, normal-mapped" : "flat");
            any = cg.zeroany;
            any.a_int = 8;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 'l', 0,
                             vl_on ? ATR_BOLD : ATR_NONE, NO_COLOR, ll, MENU_ITEMFLAGS_NONE);
        }
        { /* s192: the living fog */
            static char fl[96];

            snprintf(fl, sizeof fl, "Drifting fog over remembered ground  -  %s  (with lighting)",
                     vfx_mist ? "ON" : "off");
            any = cg.zeroany;
            any.a_int = 10;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 'o', 0,
                             vfx_mist ? ATR_BOLD : ATR_NONE, NO_COLOR, fl, MENU_ITEMFLAGS_NONE);
        }
        { /* s183: the map drawn on the GPU (needs GPU presentation; lit too since s185) */
            extern SDL_Renderer *vulture_gpu;
            static char ml[120];

            snprintf(ml, sizeof ml, "Draw the map on the GPU  -  %s%s",
                     vulture_gpumap_wanted ? "ON" : "off",
                     !vulture_gpu ? "  (needs GPU presentation)"
                     : vl_on && !vulture_gpulit_ok() ? "  (lighting needs OpenGL - software now)"
                     : ""); /* s185 */
            any = cg.zeroany;
            any.a_int = 9;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 'm', 0,
                             vulture_gpumap_wanted ? ATR_BOLD : ATR_NONE, NO_COLOR, ml,
                             MENU_ITEMFLAGS_NONE);
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
        if (pick == 10) { /* s192 */
            vfx_mist = !vfx_mist;
            vfxm_save();
            vulture_sfx("ui_click");
            if (levwin)
                levwin->force_redraw();
            continue;
        }
        if (pick == 9) { /* s183 */
            vulture_gpumap_wanted = !vulture_gpumap_wanted;
            vfxm_save();
            vulture_sfx("ui_click");
            if (levwin)
                levwin->force_redraw();
            continue;
        }
        if (pick == 8) { /* s182; s189: off -> full -> flat -> off, as F9 -
                            turning it on brings the normal maps */
            if (!vl_on)
                vl_on = true, vl_relief_set(true);
            else if (vl_relief_get())
                vl_relief_set(false);
            else
                vl_on = false;
            vfxm_save();
            vulture_sfx("ui_click");
            if (levwin)
                levwin->force_redraw();
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

/* ---- s175: every shortcut this interface adds, on one page ----
   Each row is "key<TAB>what it does": the text window lines tab columns up
   (scrollwin::layout), so the keys sit in one column whatever the font.  A
   row without a tab is a section heading (bold) or a plain note. */
static void
vcontrols_page(void)
{
    static const char *const lines[] = {
        "Moving and fighting",
        "  Left-click\twalk there, attack, pick up or open (the marker shows which)",
        "  Ctrl+click an enemy\tan aimed blow: eyes, head, arms, legs, groin or torso",
        "  Tab\tfight the nearest enemy",
        "  Shift+A, then click\tattack-move: walk there, fighting on the way",
        "  F5 or REST\trest until healed (stops when something shows up)",
        "  EXPLORE\texplore the level by yourself",
        "  > or <\twalk to the stairs you have seen (press again to take them)",
        "  s\tsearch the squares around you",
        "",
        "Seeing more",
        "  Hover anything\twhat it is, what it does, your odds against it",
        "  Shift, over a monster\tits full codex: attacks, resistances, its corpse",
        "  Alt\tevery enemy's reach: the squares they can hit you from",
        "  Mouse wheel\tzoom in and out",
        "  Middle-drag\tmove the camera",
        "  Click your portrait\tcentre the camera on you (double-click: keep it there)",
        "  PageUp / PageDown\tturn the view",
        "  Shift+Tab\tthe level map",
        "",
        "Gear and items",
        "  Right-click yourself\tactions here (an altar test on an altar, pray, ...)",
        "  1 - 0\tuse an ability-bar slot (right-click a slot to fill it)",
        "  SET 1-3 / Ctrl+1-3\tsave an outfit / change back into it",
        "",
        "Game",
        "  Esc\tthis menu: visual effects, time mode, save",
        "  Esc, then Change keys\tput any command on another key (kept between games)",
        "  Pause\ttime mode: turn-based / real-time / real-time until combat",
        "  F9\tlighting: full / flat / off",
        "  F7\twall height",
        "  F8\tzoomed-out view",
        "  F11\tphoto mode (the map without the interface)",
        "  F12\tscreenshot",
        "  Ctrl+Shift+O\tinterface options",
        "  Ctrl+Shift+P\tthe message log",
        "",
        "Badges above your portrait (only when they matter; hover one for why)",
        "  PRAY NOW / PRAY? / DON'T PRAY\tin serious trouble: whether a prayer should save you",
        "  STEALTHY / NOISY\twhether your steps can wake sleeping monsters",
        "  FAST / SLOW\tyour speed, when it isn't normal",
        "  SET 1-3\tyour saved outfits",
        NULL
    };
    winid w = vulture_create_nhwindow(NHW_TEXT);
    int k;

    for (k = 0; lines[k]; k++)
        vulture_putstr(w, (lines[k][0] && lines[k][0] != ' ') ? ATR_BOLD : ATR_NONE, lines[k]);
    vulture_display_nhwindow(w, TRUE);
    vulture_destroy_nhwindow(w);
}

/*
 * IMPLEMENTATION_LOG s195: change keys in the game.  Esc > Change keys: the
 * everyday commands with the keys they are on now; pick one and press its
 * new key.  It is bound the way the config file's BIND= binds (bind_key), so
 * NetHack's own command table does the work, and whatever the key did before
 * is named.  "All commands..." opens NetHack's own rebinding menu (any
 * command, any key, or unbinding).  Only the changes made in the game are
 * kept - in keys.txt beside effects.txt - and applied again at every start,
 * over the config file's; "Forget my key changes" drops them (the defaults
 * and the config file's come back at the next start).
 */
static std::map<int, std::string> vkeys_changes; /* key -> command ("nothing": unbound) */
static const struct ext_func_tab *vkeys_on(uchar k);

static std::string
vkeys_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char buf[700];

    snprintf(buf, sizeof buf, "%s\\NetHack\\5.0\\keys.txt", la ? la : ".");
    return buf;
}

static void
vkeys_save(void)
{
    std::map<int, std::string>::iterator it;
    char kb[QBUFSZ];
    FILE *f;

    if (vkeys_changes.empty()) {
        (void) remove(vkeys_path().c_str());
        return;
    }
    if (!(f = fopen(vkeys_path().c_str(), "w")))
        return;
    fprintf(f, "# keys changed in the game (Esc > Change keys): key:command\n");
    for (it = vkeys_changes.begin(); it != vkeys_changes.end(); ++it)
        fprintf(f, "%s:%s\n", key2txt((uchar) it->first, kb), it->second.c_str());
    fclose(f);
}

/* the game's changes, applied once at the start (over the config file's) */
static void
vkeys_load(void)
{
    static bool done = false;
    char ln[256];
    FILE *f;

    if (done)
        return;
    done = true;
    if (!(f = fopen(vkeys_path().c_str(), "r")))
        return;
    while (fgets(ln, sizeof ln, f)) {
        char *e = ln + strlen(ln), *c;
        uchar k;

        while (e > ln && (e[-1] == '\n' || e[-1] == '\r'))
            *--e = '\0';
        if (!ln[0] || ln[0] == '#' || !(c = strchr(ln + 1, ':'))) /* the key is a character at least */
            continue;
        *c = '\0';
        if ((k = txt2key(ln)) != 0 && bind_key(k, c + 1, TRUE)) {
            vkeys_changes[k] = c + 1;
            if (getenv("VULTURE_SELFTEST")) /* probe: s195 */
                vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "keys: '%s' does %s now\n", ln,
                                  vkeys_on(k) ? vkeys_on(k)->ef_txt : "nothing");
        }
    }
    fclose(f);
}

static const struct ext_func_tab *
vkeys_cmd(const char *name)
{
    const struct ext_func_tab *ec;

    for (ec = extcmdlist; ec->ef_txt; ec++)
        if (!strcmp(ec->ef_txt, name))
            return ec;
    return NULL;
}

/* what key k does now (NULL: nothing) */
static const struct ext_func_tab *
vkeys_on(uchar k)
{
    struct Cmd_bind *b;

    for (b = gc.Cmd.cmdbinds; b; b = b->next)
        if (b->key == k)
            return b->cmd;
    return NULL;
}

/* the keys a command is on, as NetHack writes them */
static std::string
vkeys_keys(const struct ext_func_tab *ec)
{
    struct Cmd_bind *b;
    std::string s;
    char kb[QBUFSZ];
    int n = 0;

    for (b = gc.Cmd.cmdbinds; b && ec; b = b->next)
        if (b->cmd == ec && n++ < 3)
            s += (s.empty() ? "" : "  or  ") + std::string(key2txt(b->key, kb));
    return s;
}

/* the command on each key, all 256 */
static void
vkeys_snapshot(std::string *m)
{
    struct Cmd_bind *b;
    int k;

    for (k = 0; k < 256; k++)
        m[k].clear();
    for (b = gc.Cmd.cmdbinds; b; b = b->next)
        if (b->cmd) {
            m[b->key] = b->cmd->ef_txt;
            if (b->param)
                m[b->key] += std::string("(") + b->param + ")";
        }
}

/* put a command on a key, keep it, say what changed */
static bool
vkeys_set(const struct ext_func_tab *ec, uchar key, bool talk)
{
    const struct ext_func_tab *prev = vkeys_on(key);
    char kb[QBUFSZ];

    if (!ec || !key || !bind_key(key, ec->ef_txt, TRUE))
        return false;
    vkeys_changes[key] = ec->ef_txt;
    vkeys_save();
    if (talk) {
        if (prev && prev != ec)
            pline("\"%s\" is on '%s' now (it was \"%s\").", ec->ef_desc, key2txt(key, kb), prev->ef_desc);
        else
            pline("\"%s\" is on '%s' now.", ec->ef_desc, key2txt(key, kb));
    }
    return true;
}

static void
vkeys_menu(void)
{
    static const char *const common[] = {
        "eat", "quaff", "read", "zap", "cast", "wear", "takeoff", "puton", "remove", "wield",
        "swap", "twoweapon", "throw", "fire", "apply", "pickup", "drop", "loot", "open", "close",
        "kick", "force", "untrap", "search", "rest", "pray", "offer", "chat", "pay", "engrave",
        "sit", "look", "glance", "inventory", "travel", NULL
    };

    for (;;) {
        std::vector<std::string> labels;
        menu_item *sel = NULL;
        anything any;
        int winid, n, pick, k;

        labels.reserve(64);
        winid = vulture_create_nhwindow(NHW_MENU);
        vulture_start_menu(winid, MENU_BEHAVE_STANDARD);
        for (k = 0; common[k]; k++) {
            const struct ext_func_tab *ec = vkeys_cmd(common[k]);
            std::string keys = vkeys_keys(ec);

            if (!ec)
                continue;
            labels.push_back(std::string(ec->ef_desc) + "\t" + (keys.empty() ? "(no key)" : keys));
            any = cg.zeroany;
            any.a_int = k + 1;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 0, 0, ATR_NONE, NO_COLOR,
                             labels.back().c_str(), MENU_ITEMFLAGS_NONE);
        }
        any = cg.zeroany;
        any.a_int = 100;
        vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 0, 0, ATR_BOLD, NO_COLOR,
                         "All commands...\tNetHack's full list: any command, any key",
                         MENU_ITEMFLAGS_NONE);
        if (!vkeys_changes.empty()) {
            any.a_int = 101;
            vulture_add_menu(winid, vulture_menu_glyph(NO_GLYPH), &any, 0, 0, ATR_NONE, NO_COLOR,
                             "Forget my key changes\tthe defaults come back when you next start",
                             MENU_ITEMFLAGS_NONE);
        }
        vulture_end_menu(winid, "Change keys  -  pick one, then press its new key");
        n = vulture_select_menu(winid, PICK_ONE, &sel);
        vulture_destroy_nhwindow(winid);
        pick = (n > 0 && sel) ? sel[0].item.a_int : 0;
        if (sel)
            free((genericptr_t) sel);
        if (pick <= 0)
            return;
        vulture_sfx("ui_click");
        if (pick == 100) { /* NetHack's own menu; what it changes is kept too */
            static std::string before[256], after[256];

            vkeys_snapshot(before);
            handler_rebind_keys();
            vkeys_snapshot(after);
            for (k = 1; k < 256; k++)
                if (before[k] != after[k])
                    vkeys_changes[k] = after[k].empty() ? "nothing" : after[k];
            vkeys_save();
            continue;
        }
        if (pick == 101) {
            vkeys_changes.clear();
            vkeys_save();
            pline("Your key changes are forgotten - the usual keys come back when you next start.");
            continue;
        }
        if (pick >= 1 && pick <= (int) (sizeof common / sizeof common[0]) - 1) {
            const struct ext_func_tab *ec = vkeys_cmd(common[pick - 1]);
            char q[200], response = 0;
            anykeydialog *win;

            if (!ec)
                continue;
            snprintf(q, sizeof q, "Press the new key for: %s   (Esc leaves it as it is)", ec->ef_desc);
            win = new anykeydialog(ROOTWIN, q);
            vulture_event_dispatcher(&response, V_RESPOND_CHARACTER, win);
            delete win;
            if (response && response != '\033')
                (void) vkeys_set(ec, (uchar) response, true);
        }
    }
}

/* probe: s195 - VULTURE_SELFTEST_KEYS puts "eat" on 'E', reads keys.txt
   back, checks the table, then puts both back */
static void
vkeys_probe(const char *dir)
{
    const struct ext_func_tab *eat = vkeys_cmd("eat"), *was = vkeys_on('E');
    char path[700], ln[200];
    FILE *f, *o;
    bool ok;

    snprintf(path, sizeof path, "%s\\keys_probe.txt", dir);
    if (!(o = fopen(path, "w")))
        return;
    ok = vkeys_set(eat, 'E', false);
    fprintf(o, "set eat on E: %d; E does now: %s\n", (int) ok, vkeys_on('E') ? vkeys_on('E')->ef_txt : "nothing");
    fprintf(o, "eat is on: %s\n", vkeys_keys(eat).c_str());
    if ((f = fopen(vkeys_path().c_str(), "r")) != NULL) {
        while (fgets(ln, sizeof ln, f))
            fprintf(o, "keys.txt: %s", ln);
        fclose(f);
    } else
        fprintf(o, "keys.txt: missing\n");
    vkeys_changes.clear(); /* put it all back as it was */
    vkeys_save();
    if (was)
        (void) bind_key('E', was->ef_txt, FALSE);
    fprintf(o, "restored: E does %s; keys.txt %s\n", vkeys_on('E') ? vkeys_on('E')->ef_txt : "nothing",
            (f = fopen(vkeys_path().c_str(), "r")) != NULL ? "still there" : "gone");
    if (f)
        fclose(f);
    fclose(o);
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
                  { 9, 'c', "Controls" },
                  { 10, 'k', "Change keys" },
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
    case 9: /* s175 */
        vcontrols_page();
        return false;
    case 10: /* s195 */
        vkeys_menu();
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
/* vl_on: defined with the Visual effects settings (s182) */
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
    if (vulture_gpumap_on) { /* s185: the normal-map draws are recorded for the GPU */
        vulture_normal_capture = true;
        vnm_ok = true;
        return;
    }
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
/*
 * IMPLEMENTATION_LOG s187: the fog of war is drawn by the light layer.
 * Squares out of sight used to be drawn with pre-darkened tiles (x0.68, or
 * x0.31 for a dark room's floor) and then lit at 0.50 - brighter than the
 * real shadows beside them, flat, and cut off at every tile's edge.  While
 * lighting is on, every tile is drawn at full brightness and this layer
 * does all of it: remembered ground gets a low light and a cold blue
 * shadow (vl_F, sampled bilinearly like the light, so the edge of sight is
 * a soft falloff), and light from lamps falls short into it.  The void -
 * rock and never-seen squares, judged from the map as the hero knows it -
 * is drawn black (its tile is a dark grey texture), and lends the walls
 * standing on or reaching over it most of its brightest known neighbour's
 * light, so they fade into the dark instead of stopping at a tile's edge.
 * The darkness runs deeper (0.94 where no light reaches, was 0.80), the
 * hero carries a bigger, warmer pool, and light past full brightness glows
 * in its own colour.
 */
static float vl_F[ROWNO][COLNO]; /* s187: 1 remembered out of sight, 0 in sight (or the
                                    void, unless it borrows a neighbour's) */
static float vl_S[ROWNO][COLNO]; /* s187: the light from sources alone - the relief follows it */
static bool vl_V[ROWNO][COLNO];  /* s187: nothing known here but rock - the void */

/* s187: what the hero knows is here - the map as drawn, never the level
   itself (a secret door is a wall, a secret corridor rock, until found) */
static bool
vl_voidish(mapdata *md, int x, int y)
{
    int mb;

    if (!levl[x][y].seenv)
        return true;
    if (!md)
        return false;
    mb = md->get_glyph(MAP_BACK, x, y);
    return mb == V_MISC_UNMAPPED_AREA || mb == V_TILE_NONE;
}

static bool
vl_rockish(mapdata *md, int x, int y) /* wall or rock, as known */
{
    if (!isok(x, y) || vl_voidish(md, x, y))
        return true;
    return md && md->get_glyph(MAP_BACK, x, y) == V_TILE_WALL_GENERIC;
}
#define VL_LIT 0.70f     /* a lit room in sight (s152: 0.80) */
#define VL_DARK 0.26f    /* an unlit square in sight - next to you (0.38) */
#define VL_MEM 0.19f     /* remembered: the fog of war (0.50 on a x0.68 tile; s188: 0.26) */
#define VL_MEMDARK 0.12f /* a remembered dark room's floor (0.50 on a x0.31 tile; s188: 0.16) */
#define VL_DMAX 0.94f    /* the darkness where no light reaches (0.80) */
#define VL_TINT 0.45f    /* how strongly a coloured light colours (0.35) */
#define VL_GLOW 0.30f    /* light past full brightness glows in its colour */
#define VL_FOG_R 0.028f  /* the fog of war's shadow: a cold dark blue (s188: 0.035 */
#define VL_FOG_G 0.040f  /* 0.050 0.100 - the user: a bit darker) */
#define VL_FOG_B 0.080f
static SDL_Surface *vl_low = NULL, *vl_full = NULL;
static SDL_Surface *vl_fogm = NULL; /* s192: the fog of war per light-grid sample (R = G = B = F) */
struct vl_flash_t {
    int x, y;
    Uint32 t0, dur;
    float r, g, b, inten, rad;
};
static std::vector<vl_flash_t> vl_flashes;

/* s190: this layer's point lights, for the relief: vl_nmap (and the GPU
   pass) light each pixel's normal from the lights around it */
struct vl_pl_t {
    int x, y; /* the square */
    float rad, inten, r, g, b;
    float lift, z; /* drawn this far above the square's centre; its height towards you */
    bool mover;    /* a creature's: follows its glide */
};
static std::vector<vl_pl_t> vl_pls;
#define VL_PL_MAX 24 /* the strongest this many - the GPU pass's array (vgl_fs) */

static void
vl_flash(int x, int y, float r, float g, float b, float inten, float rad, Uint32 dur)
{
    vl_flash_t f;

    f.x = x, f.y = y, f.t0 = SDL_GetTicks(), f.dur = dur;
    f.r = r, f.g = g, f.b = b, f.inten = inten, f.rad = rad;
    if (vl_flashes.size() < 128) /* s198: a ray leaves one a square (was 32) */
        vl_flashes.push_back(f);
}

static void
vl_add(int cx, int cy, double radius, double inten, double r, double g, double b,
       float lift = 14.0f, float z = 44.0f, bool mover = false)
{
    int x, y, R = (int) radius + 1;

    if (inten > 0.02 && isok(cx, cy)) { /* s190: the relief's point lights */
        vl_pl_t p;

        p.x = cx, p.y = cy, p.rad = (float) radius, p.inten = (float) inten;
        p.r = (float) r, p.g = (float) g, p.b = (float) b;
        p.lift = lift, p.z = z, p.mover = mover;
        vl_pls.push_back(p);
    }

    for (y = cy - R; y <= cy + R; y++)
        for (x = cx - R; x <= cx + R; x++) {
            double d, k, f;

            if (!isok(x, y) || vl_V[y][x]) /* light only lands on known ground (s187: not rock) */
                continue;
            d = sqrt((double) ((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            k = 1.0 - d / (radius + 1.0);
            if (k <= 0)
                continue;
            if ((x != cx || y != cy) && !clear_path(cx, cy, x, y))
                continue;
            f = inten * k * k;
            if (vl_F[y][x] > 0.5f) /* s187: light falls short into the fog of war */
                f *= 0.35;
            vl_S[y][x] += (float) f;
            vl_L[y][x] += (float) f;
            vl_R[y][x] += (float) (f * r);
            vl_G[y][x] += (float) (f * g);
            vl_B[y][x] += (float) (f * b);
        }
}

/* s212: the light grid at a square, as last computed (0 in rock, the void) */
static float
vl_light_at(int x, int y)
{
    return isok(x, y) ? vl_L[y][x] : 0.0f;
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

/* s198: a ray's or an explosion's square lights the room in its colour */
static void
vsp_light(int x, int y, double t)
{
    const vsp_cell_t &c = vsp_cells[y][x];
    double age, hf;

    if (!c.on) { /* not one of ours (s32's trap marker): as before */
        vl_add(x, y, 2.5, 0.9, 0.85, 0.9, 1.0);
        return;
    }
    age = (double) (SDL_GetTicks() - c.t_on) / 1000.0;
    hf = 0.6 + 0.4 * exp(-age / 0.15); /* the head is brightest */
    if (c.kind == 1) {
        const vsp_style_t &s = vsp_ray_st[c.type];
        double fl = c.type == 5 ? 0.65 + 0.7 * vsp_hash(x, y, (int) (t * 25.0)) /* lightning strobes */
                    : c.type == 1 ? 0.85 + 0.15 * sin(t * 31.0 + x * 1.7 + y) : 1.0;

        vl_add(x, y, s.lr, s.li * hf * fl, s.hr / 255.0, s.hg / 255.0, s.hb / 255.0, 30.0f, 48.0f);
    } else if (c.kind == 2) {
        const vsp_style_t &s = vsp_expl_st[c.type];

        vl_add(x, y, 2.6, s.li * (age < 0.1 ? 0.9 : 0.6), s.hr / 255.0, s.hg / 255.0, s.hb / 255.0, 30.0f,
               48.0f);
    } else if (c.kind == 3)
        vl_add(x, y, 2.4, 0.8, 0.85, 0.92, 1.0);
    else if (c.kind == 4)
        vl_add(x, y, 1.8, 0.4, 1.0, 0.8, 0.55);
    else
        vl_add(x, y, 1.6, 0.45, 0.9, 0.95, 1.0);
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

    vl_pls.clear(); /* s190 */
    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++) {
            float base;

            vl_F[y][x] = 0.0f, vl_S[y][x] = 0.0f; /* s187 */
            vl_V[y][x] = vl_voidish(md, x, y);
            if (vl_V[y][x])
                base = 0.0f;
            else if (cansee(x, y))
                base = levl[x][y].lit ? VL_LIT : VL_DARK;
            else { /* remembered, out of sight (s187: the tile is no longer darkened) */
                base = md && md->get_glyph(MAP_DARKNESS, x, y) == 2 ? VL_MEMDARK : VL_MEM;
                vl_F[y][x] = 1.0f;
            }
            if (base > 0.0f && !vl_rockish(md, x, y)) { /* s143: contact shadows */
                int nw = 0;

                /* s187: walls and rock as the hero knows them - the level's
                   own terrain told a secret door (no shadow) from a wall */
                nw += vl_rockish(md, x - 1, y);
                nw += vl_rockish(md, x + 1, y);
                nw += vl_rockish(md, x, y - 1);
                nw += vl_rockish(md, x, y + 1);
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
        if (ls->type == LS_OBJECT) /* s190: at hand height (a carried one follows you) */
            vl_add(ls->x, ls->y, ls->range + 0.8, 0.85 * fl, 1.0, 0.72, 0.42, 34.0f, 52.0f,
                   u_at(ls->x, ls->y));
        else
            vl_add(ls->x, ls->y, ls->range + 0.8, 0.75 * fl, 1.0, 0.9, 0.65, 34.0f, 52.0f, true);
    }
    for (size_t ti = 0; ti < vtorch_list.size(); ti++) { /* s211: the torches on lit rooms' walls */
        const vtorch_t &q = vtorch_list[ti];
        double fl;

        if (!cansee(q.fj, q.fi))
            continue;
        fl = 0.80 + 0.10 * sin(t * 8.3 + q.fj * 1.9 + q.fi * 2.7) + 0.06 * sin(t * 19.7 + q.fj)
             + 0.04 * ((rand() % 100) / 100.0);
        vl_add(q.fj, q.fi, 2.6, 0.34 * fl, 1.0, 0.68, 0.38, 52.0f, 40.0f);
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
                vsp_light(x, y, t); /* s198: in its own colour */
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
                       1.0, 0.55, 0.2, 34.0f, 52.0f, true); /* s190: raised, following it */
            else if (elec)
                vl_add(gm2->mx, gm2->my, 2.4, (rand() % 6) ? 0.35 : 0.75, 0.7, 0.8, 1.0, 34.0f,
                       52.0f, true);
            else if (cold)
                vl_add(gm2->mx, gm2->my, 2.0, 0.28, 0.6, 0.85, 1.0, 34.0f, 52.0f, true);
        }
    }
    if (u.ux)
    { /* s152: you carry a warm pool of light - surroundings catch it as you move */
        double fl = 0.94 + 0.04 * sin(t * 7.0) + 0.02 * sin(t * 17.0);

        /* s187: bigger and warmer (1.6 / 0.30 and 3.6 / 0.32) */
        /* s190: held up - a torch at head height, a little in front of you */
        vl_add(u.ux, u.uy, 1.8, 0.34, 1.0, 0.93, 0.82, 58.0f, 72.0f, true); /* you can always see yourself */
        vl_add(u.ux, u.uy, 4.6, 0.46 * fl, 1.0, 0.78, 0.50, 58.0f, 72.0f, true);
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
    /* s187: rock and never-seen squares (drawn black while lit) borrow
       most of their brightest known neighbour's light and fog, for what
       stands on them or reaches over them: a corridor's walls, a wall's top
       drawn over the square behind it - lit like what they border, fading
       into the void instead of stopping at a tile's edge */
    for (y = 0; y < ROWNO; y++)
        for (x = 1; x < COLNO; x++) {
            int dx, dy, bx2 = -1, by2 = -1;
            float m = 0.0f;

            if (!vl_V[y][x])
                continue;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;

                    if ((!dx && !dy) || xx < 1 || xx >= COLNO || yy < 0 || yy >= ROWNO
                        || vl_V[yy][xx])
                        continue;
                    if (vl_L[yy][xx] > m)
                        m = vl_L[yy][xx], bx2 = xx, by2 = yy;
                }
            if (bx2 < 0)
                continue;
            vl_L[y][x] = 0.70f * m;
            vl_R[y][x] = 0.70f * vl_R[by2][bx2];
            vl_G[y][x] = 0.70f * vl_G[by2][bx2];
            vl_B[y][x] = 0.70f * vl_B[by2][bx2];
            vl_F[y][x] = vl_F[by2][bx2];
        }
}

/* bilinear light at fractional map position */
static void
vl_sample(double mx, double my, float *L, float *r, float *g, float *b, float *F, float *S)
{
    int x0 = (int) floor(mx), y0 = (int) floor(my), k;
    double fx = mx - x0, fy = my - y0;
    float acc[6] = { 0, 0, 0, 0, 0, 0 };

    for (k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        double w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);

        if (x < 1 || x >= COLNO || y < 0 || y >= ROWNO)
            continue;
        acc[0] += (float) (w * vl_L[y][x]);
        acc[1] += (float) (w * vl_R[y][x]);
        acc[2] += (float) (w * vl_G[y][x]);
        acc[3] += (float) (w * vl_B[y][x]);
        acc[4] += (float) (w * vl_F[y][x]); /* s187 */
        acc[5] += (float) (w * vl_S[y][x]);
    }
    *L = acc[0], *r = acc[1], *g = acc[2], *b = acc[3], *F = acc[4], *S = acc[5];
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
/* s190: per cell (x256), the soft key's weight and the light that arrives
   from sources; vr_lm: per cell, a bit for each point light that can reach
   it (vl_plf holds the lights) */
static std::vector<short> vr_kw, vr_sw;
static std::vector<Uint32> vr_lm;
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

/*
 * IMPLEMENTATION_LOG s190: normal-mapped light from the lights themselves.
 * Each pixel's normal (vulture_normal_buf) is lit by the point lights that
 * reach it: the direction to each light from the pixel (a torch or lamp is
 * raised - up the screen and towards you), weighted by its falloff, the sum
 * scaled by the light that really arrives there (the cell's source light,
 * which walls and the fog of war already cut).  A soft key from the top
 * left, by the cell's light level, keeps lit rooms from going flat.  It is
 * shading relative to flat ground - the light layer does the brightness: a
 * face turned to a light brightens, one turned away darkens, and a face
 * square to the light and your eye glints in the light's colour.  Was one
 * direction per 8 px cell from the light field's slope, under a strong key,
 * so the relief barely moved with the light.  vgl_fs does the same in floats.
 */
static std::vector<float> vl_plf; /* 12 floats a light: see vl_pl_project */
static int vl_npl = 0;
static const float vl_rk[8] = {
    -0.45f, -0.60f, 0.66f, /* the key: from the top left, above */
    0.40f,                 /* how much facing away from a light's height counts (crevices) */
    1.20f,                 /* the relief's strength */
    0.30f,                 /* the glint's */
    0.0f, 0.0f
};
#define VL_KEYW 0.22f /* the key's weight per unit of light level */

static void
vl_nmap(int x0, int y0, int x1, int y1, int gx0, int gy0, int FS)
{
    SDL_Surface *s = vulture_screen, *nb = vulture_normal_buf;
    int x, y, rs = s->format->Rshift, gs = s->format->Gshift, bs = s->format->Bshift;
    Uint32 mask = ~(((Uint32) 255 << rs) | ((Uint32) 255 << gs) | ((Uint32) 255 << bs));
    const float ixm = 1.0f / V_MAP_XMOD, iym = 1.0f / V_MAP_YMOD;
    const float *pl = vl_plf.empty() ? NULL : &vl_plf[0];
    const float kx = vl_rk[0], ky = vl_rk[1], kz = vl_rk[2], tz = vl_rk[3];

    for (y = y0; y < y1; y++) {
        int cj = (y - gy0) / FS, ci, ca, cb;
        Uint32 *row, *nrow;

        if (y < gy0 || cj >= vr_lh || vr_cmin[cj] < 0)
            continue;
        row = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch);
        nrow = (Uint32 *) ((Uint8 *) nb->pixels + (size_t) y * nb->pitch);
        ca = vr_cmin[cj], cb = vr_cmax[cj];
        for (ci = ca; ci <= cb; ci++) {
            int k = cj * vr_lw + ci, xs, xe;
            float kw = vr_kw[k] / 256.0f, sw = vr_sw[k] / 256.0f;
            Uint32 lm = (pl && sw > 0.0f && (size_t) k < vr_lm.size()) ? vr_lm[k] : 0;

            if (!vr_kw[k] && !vr_sw[k])
                continue;
            xs = gx0 + ci * FS, xe = xs + FS;
            if (xs < x0)
                xs = x0;
            if (xe > x1)
                xe = x1;
            for (x = xs; x < xe; x++) {
                Uint32 pn = nrow[x], p = row[x], bits = lm;
                int a = (int) (pn >> 24), r, g, b2;
                float nx, ny, nz, d, sr = 0.0f, sg = 0.0f, sb = 0.0f, m;

                if (a < 24 || !(p & ~mask))
                    continue; /* no normal here; black stays black */
                nx = (float) ((int) ((pn >> 16) & 255) - 128) / 127.0f;
                ny = (float) ((int) ((pn >> 8) & 255) - 128) / 127.0f;
                nz = (float) ((int) (pn & 255) - 128) / 127.0f;
                d = kw * (nx * kx + ny * ky + tz * (nz - 1.0f) * kz);
                if (bits) {
                    float rel = 0.0f, ws = 0.0f;

                    while (bits) {
                        const float *P = pl + 12 * __builtin_ctz(bits);
                        float dx = P[0] - (float) x, dy = P[1] - (float) y;
                        float ma = (dx * ixm + dy * iym) * 0.5f, mb = (dy * iym - dx * ixm) * 0.5f;
                        float kk = 1.0f - sqrtf(ma * ma + mb * mb) / P[4];

                        bits &= bits - 1;
                        if (kk > 0.0f) {
                            float w = P[5] * kk * kk, ly = P[2] - (float) y;
                            float inv = 1.0f / sqrtf(dx * dx + ly * ly + P[3] * P[3]);
                            float lx = dx * inv, lz = P[3] * inv, hz, hi, sp;

                            ly *= inv;
                            rel += w * (nx * lx + ny * ly + tz * (nz - 1.0f) * lz);
                            hz = lz + 1.0f; /* the half vector, towards your eye */
                            hi = 1.0f / sqrtf(lx * lx + ly * ly + hz * hz);
                            sp = (nx * lx + ny * ly + nz * hz) * hi;
                            if (sp > 0.0f) {
                                float f = hz * hi, s16 = sp * sp, f16 = f * f;

                                s16 *= s16, s16 *= s16, s16 *= s16;
                                f16 *= f16, f16 *= f16, f16 *= f16;
                                if (s16 > f16) { /* squarer to it than flat ground */
                                    s16 = (s16 - f16) * w;
                                    sr += s16 * P[6], sg += s16 * P[7], sb += s16 * P[8];
                                }
                            }
                            ws += w;
                        }
                    }
                    if (ws > 0.0f) {
                        float q = sw / ws;

                        d += rel * q;
                        sr *= q, sg *= q, sb *= q;
                    }
                }
                d *= vl_rk[4] * (float) a / 255.0f;
                d = d > 0.55f ? 0.55f : d < -0.5f ? -0.5f : d;
                m = vl_rk[5] * (float) a;
                { /* s209: shine, by material - as vgl_fs */
                    int cr = (int) ((p >> rs) & 255), cg = (int) ((p >> gs) & 255), cb = (int) ((p >> bs) & 255);
                    int mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
                    int mn = cr < cg ? (cr < cb ? cr : cb) : (cg < cb ? cg : cb);
                    float e = ((float) mx / 255.0f - 0.25f) / 0.5f, gloss;

                    e = e < 0.0f ? 0.0f : e > 1.0f ? 1.0f : e;
                    gloss = (mx > 5 ? 1.0f - (float) (mx - mn) / (float) mx : 0.0f) * e * e * (3.0f - 2.0f * e);
                    m *= 0.6f + 2.0f * gloss;
                }
                sr = sr * m > 96.0f ? 96.0f : sr * m;
                sg = sg * m > 96.0f ? 96.0f : sg * m;
                sb = sb * m > 96.0f ? 96.0f : sb * m;
                r = (int) ((float) ((p >> rs) & 255) * (1.0f + d) + sr);
                g = (int) ((float) ((p >> gs) & 255) * (1.0f + d) + sg);
                b2 = (int) ((float) ((p >> bs) & 255) * (1.0f + d) + sb);
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
            int k = cj * vr_lw + ci, dx = -vr_kw[k] * 6 / 10, dy = -vr_kw[k] * 8 / 10, xs, xe;

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

/* s190: per coarse cell, how much of the soft key and how much light from
   real sources reach it (Lg: all the light, Sg: the sources' alone - walls
   and the fog of war have already cut it) */
static void
vl_relief_dirs(const std::vector<float> &Lg, const std::vector<float> &Sg, int lw, int lh)
{
    int i, j;

    vr_lw = lw, vr_lh = lh;
    vr_kw.assign((size_t) lw * lh, 0);
    vr_sw.assign((size_t) lw * lh, 0);
    vr_cmin.assign((size_t) lh, -1);
    vr_cmax.assign((size_t) lh, -1);
    for (j = 1; j < lh - 1; j++)
        for (i = 1; i < lw - 1; i++) {
            size_t k = (size_t) j * lw + i;
            float L = Lg[k], S = Sg[k];

            if (L < 0.08f)
                continue; /* too dark to show any relief */
            /* s149b: remembered ground away from any real light gets none -
               lighting it drew a bright grid over every unseen square */
            if (L < 0.6f && S < 0.04f)
                continue;
            vr_kw[k] = (short) (256.0f * VL_KEYW * (L > 1.0f ? 1.0f : L));
            vr_sw[k] = (short) (256.0f * (S > 2.0f ? 2.0f : S));
            if (!vr_kw[k] && !vr_sw[k])
                continue;
            if (vr_cmin[j] < 0)
                vr_cmin[j] = i;
            vr_cmax[j] = i;
        }
}

/* s190: this frame's point lights on the screen, 12 floats each: x, y (the
   square's centre - the falloff's), y of the light itself (raised), its
   height towards you, radius + 1 in squares, intensity, r, g, b, three
   spare - the VL_PL_MAX strongest that reach the map; and per relief cell
   (origin gx0, gy0) a bit for each light that can reach it */
static void
vl_pl_project(int cx, int cy, int vx, int vy, int mx, int my, int mw, int mh, int gx0, int gy0,
              int FS)
{
    struct cand {
        float key, v[12];
    };
    std::vector<cand> c;
    size_t i;
    int k;

    vl_plf.clear(), vl_npl = 0;
    vr_lm.assign((size_t) vr_lw * vr_lh, 0);
    for (i = 0; i < vl_pls.size(); i++) {
        const vl_pl_t &p = vl_pls[i];
        float ex = V_MAP_XMOD * 1.42f * (p.rad + 1.0f), ey = V_MAP_YMOD * 1.42f * (p.rad + 1.0f);
        int sx, sy;
        cand q;

        if (p.mover)
            vfx_mover_screen(p.x, p.y, cx, cy, vx, vy, &sx, &sy);
        else
            vfx_square_screen(p.x, p.y, cx, cy, vx, vy, &sx, &sy);
        if (sx + ex < mx || sx - ex > mx + mw || sy + ey < my || sy - ey > my + mh)
            continue;
        q.key = p.inten * (p.rad + 1.0f);
        q.v[0] = (float) sx, q.v[1] = (float) sy, q.v[2] = (float) sy - p.lift, q.v[3] = p.z;
        q.v[4] = p.rad + 1.0f, q.v[5] = p.inten, q.v[6] = p.r, q.v[7] = p.g;
        q.v[8] = p.b, q.v[9] = q.v[10] = q.v[11] = 0.0f;
        c.push_back(q);
    }
    std::sort(c.begin(), c.end(), [](const cand &a, const cand &b) { return a.key > b.key; });
    if (c.size() > VL_PL_MAX)
        c.resize(VL_PL_MAX);
    for (k = 0; k < (int) c.size(); k++) {
        /* the falloff's reach on the screen: a circle of radius + 1 squares
           is an ellipse up to sqrt 2 squares' width across */
        float ex = V_MAP_XMOD * 1.42f * c[k].v[4], ey = V_MAP_YMOD * 1.42f * c[k].v[4];
        int ci0 = (int) floorf((c[k].v[0] - ex - gx0) / FS) - 1;
        int ci1 = (int) floorf((c[k].v[0] + ex - gx0) / FS) + 1;
        int cj0 = (int) floorf((c[k].v[1] - ey - gy0) / FS) - 1;
        int cj1 = (int) floorf((c[k].v[1] + ey - gy0) / FS) + 1;
        int ci, cj;

        vl_plf.insert(vl_plf.end(), c[k].v, c[k].v + 12);
        ci0 = ci0 < 0 ? 0 : ci0, cj0 = cj0 < 0 ? 0 : cj0;
        ci1 = ci1 > vr_lw - 1 ? vr_lw - 1 : ci1, cj1 = cj1 > vr_lh - 1 ? vr_lh - 1 : cj1;
        for (cj = cj0; cj <= cj1; cj++)
            for (ci = ci0; ci <= ci1; ci++)
                vr_lm[(size_t) cj * vr_lw + ci] |= 1u << k;
    }
    vl_npl = (int) c.size();
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

/*
 * IMPLEMENTATION_LOG s192: living fog.  Over remembered ground (the fog of
 * war - vl_F, on its own grid vl_fogm beside the light grid) a slow mist
 * drifts: two layers of tiling noise, anchored to the map so it pans with
 * it and crawling at their own speeds, thicken into wisps of cold grey; it
 * thins out with the fog at the edge of your light and spills a little into
 * the dark beyond the edge of what you know.  Per pixel, after the light
 * layer (vl_mist here, vgl_fs on the GPU - the same arithmetic).  Visual
 * effects: "Drifting fog over remembered ground".
 */
#define VL_MIST_K 0.32f /* the thickest it gets */
static const float vl_mist_rgb[3] = { 62.0f, 72.0f, 92.0f };

/* 256 x 256 tiling value noise, 0..255 (made once, the same for the GPU) */
static const Uint8 *
vl_noise(void)
{
    static std::vector<Uint8> n;

    if (n.empty()) {
        static const int cell[4] = { 64, 32, 16, 8 };
        static const float wt[4] = { 0.45f, 0.28f, 0.17f, 0.10f };
        std::vector<float> acc(65536, 0.0f);
        Uint32 seed = 0x2545F491u;
        float lo = 1e9f, hi = -1e9f;
        size_t q;
        int o, x, y;

        for (o = 0; o < 4; o++) {
            int c = cell[o], g = 256 / c;
            std::vector<float> lat((size_t) g * g);

            for (q = 0; q < lat.size(); q++) {
                seed = seed * 1664525u + 1013904223u; /* its own generator: no game rolls */
                lat[q] = (float) (seed >> 8) / 16777216.0f;
            }
            for (y = 0; y < 256; y++)
                for (x = 0; x < 256; x++) {
                    int x0 = x / c, y0 = y / c, x1 = (x0 + 1) % g, y1 = (y0 + 1) % g;
                    float fx = (float) (x % c) / c, fy = (float) (y % c) / c, a, b;

                    fx = fx * fx * (3.0f - 2.0f * fx), fy = fy * fy * (3.0f - 2.0f * fy);
                    a = lat[y0 * g + x0] + (lat[y0 * g + x1] - lat[y0 * g + x0]) * fx;
                    b = lat[y1 * g + x0] + (lat[y1 * g + x1] - lat[y1 * g + x0]) * fx;
                    acc[(size_t) y * 256 + x] += wt[o] * (a + (b - a) * fy);
                }
        }
        for (q = 0; q < acc.size(); q++)
            lo = acc[q] < lo ? acc[q] : lo, hi = acc[q] > hi ? acc[q] : hi;
        n.resize(65536);
        for (q = 0; q < acc.size(); q++)
            n[q] = (Uint8) (255.0f * (acc[q] - lo) / (hi - lo > 1e-6f ? hi - lo : 1.0f));
    }
    return &n[0];
}

/* the noise at (s, t) noise tiles - as GL_LINEAR with GL_REPEAT samples it */
static inline float
vl_noise_at(const Uint8 *n, float s, float t)
{
    float x = s * 256.0f - 0.5f, y = t * 256.0f - 0.5f, fx, fy, a, b;
    int x0 = (int) floorf(x), y0 = (int) floorf(y), x1, y1;

    fx = x - (float) x0, fy = y - (float) y0;
    x0 &= 255, y0 &= 255, x1 = (x0 + 1) & 255, y1 = (y0 + 1) & 255;
    a = n[y0 * 256 + x0] + (n[y0 * 256 + x1] - n[y0 * 256 + x0]) * fx;
    b = n[y1 * 256 + x0] + (n[y1 * 256 + x1] - n[y1 * 256 + x0]) * fx;
    return (a + (b - a) * fy) / 255.0f;
}

/* how thick the mist is at map-anchored pixel (qx, qy), time t s */
static inline float
vl_mist_d(const Uint8 *n, float qx, float qy, float t)
{
    /* ~10 and ~7 px a second, different ways (vgl_fs: the same) */
    float n1 = vl_noise_at(n, qx / 230.0f + t * 0.045f, qy / 230.0f + t * 0.016f);
    float n2 = vl_noise_at(n, qx / 97.0f - t * 0.07f, qy / 97.0f + t * 0.035f);
    float d = (0.62f * n1 + 0.38f * n2 - 0.38f) / 0.42f;

    d = d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d;
    return d * d * (3.0f - 2.0f * d);
}

/* this frame's mist: the map's anchor on the screen, the time, how thick,
   its colour */
static void
vl_mist_params(int cx, int cy, int vx, int vy, float *mp)
{
    int ox, oy;

    vfx_square_screen(0, 0, cx, cy, vx, vy, &ox, &oy);
    mp[0] = (float) ox, mp[1] = (float) oy;
    mp[2] = (float) (SDL_GetTicks() % 3600000u) / 1000.0f;
    mp[3] = vfx_mist ? VL_MIST_K : 0.0f;
    mp[4] = vl_mist_rgb[0], mp[5] = vl_mist_rgb[1], mp[6] = vl_mist_rgb[2], mp[7] = 0.0f;
}

/* the software pass: the mist is smooth at the light grid's scale, so it is
   worked out once per grid sample (vl_mist) and blended between samples
   like the light - per pixel it cost ~18 ms a frame over a mapped level */
static void
vl_mist_band(const std::vector<float> &Mg, int lw, int lh, int mx, int my, int mw, int mh, int ox,
             int oy, int FS, int by0, int by1, const float *mp)
{
    SDL_Surface *s = vulture_screen;
    std::vector<float> rM;
    int x, y, x0, y0, x1, y1, rs, gs, bs;
    Uint32 mask;

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
    rM.resize(lw);
    for (y = y0; y < y1; y++) {
        int fy = y - oy, j = fy >= 0 ? fy / FS : -1, i;
        float t = fy >= 0 ? (float) (fy % FS) / FS : 0.0f, any = 0.0f;
        Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch);
        const float *m0, *m1;

        if (j < 0)
            j = 0, t = 0.0f;
        if (j >= lh - 1)
            j = lh - 2, t = 1.0f;
        m0 = &Mg[(size_t) j * lw], m1 = &Mg[(size_t) (j + 1) * lw];
        for (i = 0; i < lw; i++) {
            rM[i] = m0[i] + (m1[i] - m0[i]) * t;
            any = rM[i] > any ? rM[i] : any;
        }
        if (any < 0.002f)
            continue;
        for (x = x0; x < x1; x++) {
            int fx = x - ox, ci = fx >= 0 ? fx / FS : 0, r, g, b2;
            float off = fx >= 0 ? (float) (fx % FS) / FS : 0.0f, m;
            Uint32 p;

            if (ci >= lw - 1)
                ci = lw - 2, off = 1.0f;
            m = rM[ci] + (rM[ci + 1] - rM[ci]) * off;
            if (m < 0.002f)
                continue;
            p = row[x];
            r = (int) ((p >> rs) & 255), g = (int) ((p >> gs) & 255), b2 = (int) ((p >> bs) & 255);
            r = (int) ((float) r + (mp[4] - (float) r) * m + 0.5f);
            g = (int) ((float) g + (mp[5] - (float) g) * m + 0.5f);
            b2 = (int) ((float) b2 + (mp[6] - (float) b2) * m + 0.5f);
            row[x] = (p & mask) | ((Uint32) r << rs) | ((Uint32) g << gs) | ((Uint32) b2 << bs);
        }
    }
}

static void
vl_mist(SDL_Surface *fog, int mx, int my, int mw, int mh, int ox, int oy, int FS, const float *mp)
{
    static std::vector<float> Mg;
    SDL_Surface *s = vulture_screen;
    const Uint8 *nz = vl_noise();
    int lw, lh, i, j;

    if (!s || !fog || mp[3] <= 0.0f || s->format->BytesPerPixel != 4 || fog->w < 2 || fog->h < 2)
        return;
    lw = fog->w, lh = fog->h;
    Mg.assign((size_t) lw * lh, 0.0f);
    SDL_LockSurface(fog);
    vpar_rows(0, lh, [&](int ja, int jz) { /* the grid's rows on every core too */
        int jj, ii;

        for (jj = ja; jj < jz; jj++) {
            const Uint32 *g = (const Uint32 *) ((Uint8 *) fog->pixels + (size_t) jj * fog->pitch);

            for (ii = 0; ii < lw; ii++) {
                float f = (float) ((g[ii] >> 16) & 255) / 255.0f;

                if (f >= 0.004f)
                    Mg[(size_t) jj * lw + ii] = f * mp[3]
                                                * vl_mist_d(nz, (float) (ox + ii * FS) - mp[0],
                                                            (float) (oy + jj * FS) - mp[1], mp[2]);
            }
        }
    });
    SDL_UnlockSurface(fog);
    nhUse(i), nhUse(j);
    if (SDL_MUSTLOCK(s))
        SDL_LockSurface(s);
    vpar_rows(my < 0 ? 0 : my, my + mh > s->h ? s->h : my + mh, [&](int a, int z) {
        vl_mist_band(Mg, lw, lh, mx, my, mw, mh, ox, oy, FS, a, z, mp);
    });
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
    int M = 128; /* margin: a cached layer can slide with the camera (s152: 96 -> 64;
                    s186: 128 - panning slides it instead of rebuilding) */
    int pvx = 0, pvy = 0, sox, soy; /* s186: a view shift, in pixels */
    int lw, lh, i, j;
    Uint32 now = SDL_GetTicks();
    SDL_Rect src, dst;
    int max0 = ax, may0 = ay, mw0 = w, mh0 = h; /* s145: the map's own rect */
    static int c_gx0 = 0, c_gy0 = 0;              /* s145: where grid cell 0,0 was */
    static std::vector<float> Lg, Sg; /* s187: Sg - the sources' light alone */
    float mistp[8]; /* s192 */

    if (!vl_on || !u.ux || w < 32 || h < 32 || u.uswallow)
        return;
    /* reuse the last layer (shifted with the camera) unless the scene
       changed or ~90 ms passed (flicker); during a slide, always reuse */
    /* s186: a pan that changed the view square is a slide too - the grid is
       map light sampled on the screen, so it only moves */
    if (c_vx != -99 && abs(vx - c_vx) <= 8 && abs(vy - c_vy) <= 8)
        vtw_proj(vx - c_vx, vy - c_vy, &pvx, &pvy);
    sox = (cx - c_cx) - pvx, soy = (cy - c_cy) - pvy;
    if (vl_low && abs(vx - c_vx) <= 8 && abs(vy - c_vy) <= 8 && c_ux == u.ux && c_uy == u.uy
        && c_moves == svm.moves && c_rot == vulture_view_rot && c_w == w && c_h == h
        && abs(sox) < M && abs(soy) < M
        && (vtw_animating || now - c_when < (vl_magic ? 50u : 120u)) && vl_flashes.empty()) {
        dst.x = ax - M + sox, dst.y = ay - M + soy;
        vl_pl_project(cx, cy, vx, vy, max0, may0, mw0, mh0, c_gx0 + sox, c_gy0 + soy, FS); /* s190 */
        vl_mist_params(cx, cy, vx, vy, mistp); /* s192 */
        if (vl_gpu_frame) { /* s185: the same grids and origins, lit by the GPU */
            SDL_Rect ar = { max0, may0, mw0, mh0 };

            vulture_gpulit_mist(vl_fogm, false, vl_noise(), mistp); /* s192 */

            vulture_gpulit_frame(vl_low, dst.x, dst.y, FS, vr_kw.empty() ? NULL : &vr_kw[0],
                                 vr_sw.empty() ? NULL : &vr_sw[0], vr_lw, vr_lh, c_gx0 + sox,
                                 c_gy0 + soy, vl_plf.empty() ? NULL : &vl_plf[0], vl_npl, vl_rk,
                                 1.0f / V_MAP_XMOD, 1.0f / V_MAP_YMOD, vl_relief_on && vnm_ok,
                                 false, &ar);
            return;
        }
        vl_relief(max0, may0, mw0, mh0, c_gx0 + sox, c_gy0 + soy, FS); /* s186 */
        vl_apply(vl_low, max0, may0, mw0, mh0, dst.x, dst.y, FS); /* s153 */
        vl_mist(vl_fogm, max0, may0, mw0, mh0, dst.x, dst.y, FS, mistp); /* s192 */
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
    if (!vl_fogm || vl_fogm->w != lw || vl_fogm->h != lh) { /* s192 */
        if (vl_fogm)
            SDL_FreeSurface(vl_fogm);
        vl_fogm = SDL_CreateRGBSurfaceWithFormat(0, lw, lh, 32, SDL_PIXELFORMAT_ARGB8888);
    }
    if (vl_full) { /* s153: no full-size layer any more */
        SDL_FreeSurface(vl_full);
        vl_full = NULL;
    }
    if (!vl_low || !vl_fogm)
        return;
    Lg.assign((size_t) lw * lh, 0.0f);
    Sg.assign((size_t) lw * lh, 0.0f); /* s187 */
    SDL_LockSurface(vl_low);
    SDL_LockSurface(vl_fogm); /* s192 */
    for (j = 0; j < lh; j++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) vl_low->pixels + j * vl_low->pitch);
        Uint32 *frow = (Uint32 *) ((Uint8 *) vl_fogm->pixels + j * vl_fogm->pitch); /* s192 */

        for (i = 0; i < lw; i++) {
            /* screen -> view -> map (inverse of vfx_square_screen) */
            double sx = (ax + i * FS - cx) / (double) V_MAP_XMOD;
            double sy = (ay + j * FS - cy) / (double) V_MAP_YMOD;
            double a = (sx + sy) / 2.0, b2 = (sy - sx) / 2.0, t;
            float L, r, g, b, dark, tint, A, cr, cg, cb, mx, mn, F, S;
            int k;

            for (k = vulture_view_rot & 3; k > 0; k--) { /* view -> map */
                t = a;
                a = -b2;
                b2 = t;
            }
            vl_sample(vx + a, vy + b2, &L, &r, &g, &b, &F, &S);
            if (L < 0)
                L = 0;
            Lg[(size_t) j * lw + i] = L; /* s145 */
            Sg[(size_t) j * lw + i] = S; /* s187 */
            {
                Uint32 f8 = (Uint32) (255.0f * (F < 0.0f ? 0.0f : F > 1.0f ? 1.0f : F));

                frow[i] = 0xFF000000u | (f8 << 16) | (f8 << 8) | f8; /* s192 */
            }
            dark = (1.0f - (L > 1.0f ? 1.0f : L)) * VL_DMAX; /* s187: was 0.80 */
            cr = cg = cb = 1.0f;
            if (L > 0.01f)
                cr = r / L, cg = g / L, cb = b / L;
            mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
            mn = cr < cg ? (cr < cb ? cr : cb) : (cg < cb ? cg : cb);
            if (mx < 0.001f)
                cr = cg = cb = mx = 1.0f, mn = 1.0f;
            tint = (mx - mn) * VL_TINT * (L > 1.2f ? 1.2f : L);
            if (L > 1.0f) /* s187: hot spots glow in the light's colour */
                tint += VL_GLOW * (L - 1.0f > 0.8f ? 0.8f : L - 1.0f);
            if (tint > 0.6f)
                tint = 0.6f;
            A = dark + tint * (1.0f - dark);
            {
                /* s187: the colour pulled towards: the shadow's (black in
                   sight, a cold blue in the fog of war) by the darkness's
                   share, the light's by the tint's */
                float wd = A > 0.001f ? dark / A : 0.0f;
                float wt = A > 0.001f ? tint * (1.0f - dark) / A : 0.0f;
                float tr = wd * F * VL_FOG_R + wt * cr / mx, tg = wd * F * VL_FOG_G + wt * cg / mx;
                float tb = wd * F * VL_FOG_B + wt * cb / mx;
                Uint8 R8 = (Uint8) (255 * (tr > 1.0f ? 1.0f : tr));
                Uint8 G8 = (Uint8) (255 * (tg > 1.0f ? 1.0f : tg));
                Uint8 B8 = (Uint8) (255 * (tb > 1.0f ? 1.0f : tb));

                if (A > 0.95f)
                    A = 0.95f;
                row[i] = ((Uint32) (A * 255) << 24) | ((Uint32) R8 << 16) | ((Uint32) G8 << 8) | B8;
            }
        }
    }
    SDL_UnlockSurface(vl_fogm);
    SDL_UnlockSurface(vl_low);
    Uint32 vt_g = SDL_GetTicks(); /* probe: s147 */
    nhUse(src);
    Uint32 vt_s = SDL_GetTicks(); /* probe: s147 (no stretch since s153) */
    vl_relief_dirs(Lg, Sg, lw, lh); /* s145, s187 */
    c_gx0 = ax - FS / 2, c_gy0 = ay - FS / 2; /* cell i covers ax+i*FS +- FS/2 */
    vl_pl_project(cx, cy, vx, vy, max0, may0, mw0, mh0, c_gx0, c_gy0, FS); /* s190 */
    vl_mist_params(cx, cy, vx, vy, mistp); /* s192 */
    if (vl_gpu_frame) { /* s185: rebuilt grids - the GPU uploads and lights */
        SDL_Rect ar = { max0, may0, mw0, mh0 };

        vulture_gpulit_mist(vl_fogm, true, vl_noise(), mistp); /* s192 */

        vulture_gpulit_frame(vl_low, ax, ay, FS, vr_kw.empty() ? NULL : &vr_kw[0],
                             vr_sw.empty() ? NULL : &vr_sw[0], vr_lw, vr_lh, c_gx0, c_gy0,
                             vl_plf.empty() ? NULL : &vl_plf[0], vl_npl, vl_rk,
                             1.0f / V_MAP_XMOD, 1.0f / V_MAP_YMOD, vl_relief_on && vnm_ok, true,
                             &ar);
        return;
    }
    vl_relief(max0, may0, mw0, mh0, c_gx0, c_gy0, FS);
    Uint32 vt_r = SDL_GetTicks(); /* probe: s147 */
    vl_apply(vl_low, max0, may0, mw0, mh0, ax, ay, FS); /* s153 */
    vl_mist(vl_fogm, max0, may0, mw0, mh0, ax, ay, FS, mistp); /* s192 */
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

/* s201: vzoom_apply's rectangles, ahead of the map pass - the 1x region src
   that will fill dst (render space) */
static bool
vzoom_rects(int ax, int ay, int w, int h, SDL_Rect *src, SDL_Rect *dst)
{
    int k;

    if (vzoom <= 1.0 || w < 16 || h < 16)
        return false;
    k = (int) ceil(vzoom - 1e-6);
    if (fabs(vzoom - k) < 1e-6) { /* as vzoom_apply's exact branch */
        src->w = w / k, src->h = h / k;
        src->x = ax + (w - src->w) / 2, src->y = ay + (h - src->h) / 2;
        dst->w = src->w * k, dst->h = src->h * k;
        dst->x = ax + (w - dst->w) / 2, dst->y = ay + (h - dst->h) / 2;
    } else {
        src->w = (int) (w / vzoom), src->h = (int) (h / vzoom);
        src->x = ax + (w - src->w) / 2, src->y = ay + (h - src->h) / 2;
        dst->x = ax, dst->y = ay, dst->w = w, dst->h = h;
    }
    return true;
}

/* probe: s201 - VULTURE_SELFTEST_ZOOM=n starts at zoom step n */
void
vzoom_probe_set(int idx)
{
    if (idx >= 0 && idx < (int) (sizeof vzoom_steps / sizeof vzoom_steps[0]))
        vzoom_idx = idx, vzoom = vzoom_steps[idx];
}

static void
vzoom_apply(int ax, int ay, int w, int h)
{
    SDL_Rect src, dst;

    vulture_gpumap_set_zoom(NULL, NULL, 1, true); /* s183 */
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
            {
                SDL_Rect gd = { ax + dst.x, ay + dst.y, dst.w, dst.h }; /* s183 */

                vulture_gpumap_set_zoom(&src, &gd, k, true);
            }
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
            {
                SDL_Rect gd = { ax, ay, w, h }; /* s183 */

                vulture_gpumap_set_zoom(&src, &gd, k, false);
            }
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

/*
 * IMPLEMENTATION_LOG s191: shadows cast by the lights.  Every creature in
 * sight (you too) and every big object (boulders, statues) lays its
 * silhouette on the floor, stretching away from the strongest light that
 * reaches it with a clear path - your torch, a lamp, a fire - longer the
 * farther it stands from the light, darker the more of the light where it
 * stands is that light's, fading towards the tip.  A light's own carrier
 * casts none from it (your torch never shadows you).  Drawn after the floor
 * and before anything standing on it, so creatures in front cover it.  Only
 * while lighting is on (its lights, vl_pls); the Shadows switch covers it.
 * The GPU map draws the silhouette as one textured quad (SDL_RenderGeometry);
 * in software it is rasterized the same way.
 */
#define VCS_MAX 0.75f /* the darkest a cast shadow gets */

/* one silhouette - img drawn at (dx, dy), its feet at (fx, fy) - laid on the
   floor: a sprite point u right of and v above the feet lands at
   feet + u * w + v * h; alpha a0 at the feet, fading to 45% at the top */
static void
vcast_one(SDL_Surface *img, int dx, int dy, float fx, float fy, const float *w, const float *h,
          float a0, const SDL_Rect *clip)
{
    float vtop = fy - (float) dy, us[4], vs[4], qx[4], qy[4];
    int k;

    if (vtop < 4.0f || a0 <= 0.0f)
        return;
    us[0] = us[3] = (float) dx - fx, us[1] = us[2] = us[0] + (float) img->w;
    vs[0] = vs[1] = vtop, vs[2] = vs[3] = vtop - (float) img->h;
    for (k = 0; k < 4; k++)
        qx[k] = fx + us[k] * w[0] + vs[k] * h[0], qy[k] = fy + us[k] * w[1] + vs[k] * h[1];
    if (vulture_gpumap_on) {
        float q[8];
        Uint8 a4[4];

        for (k = 0; k < 4; k++) {
            float f = vs[k] / vtop, a;

            f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
            a = a0 * (1.0f - 0.55f * f);
            q[2 * k] = qx[k], q[2 * k + 1] = qy[k];
            a4[k] = (Uint8) (255.0f * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a));
        }
        (void) vulture_gpumap_shadow(img, q, a4);
        return;
    }
    {
        SDL_Surface *s = vulture_screen;
        float det = w[0] * h[1] - w[1] * h[0], i00, i01, i10, i11;
        int x0, y0, x1, y1, px, py, rs, gs, bs, ash;
        Uint32 mask;

        if (fabsf(det) < 1e-4f || !s || s->format->BytesPerPixel != 4
            || img->format->BytesPerPixel != 4 || !img->format->Amask)
            return;
        i00 = h[1] / det, i01 = -h[0] / det, i10 = -w[1] / det, i11 = w[0] / det;
        x0 = (int) floorf(min(min(qx[0], qx[1]), min(qx[2], qx[3])));
        x1 = (int) ceilf(max(max(qx[0], qx[1]), max(qx[2], qx[3])));
        y0 = (int) floorf(min(min(qy[0], qy[1]), min(qy[2], qy[3])));
        y1 = (int) ceilf(max(max(qy[0], qy[1]), max(qy[2], qy[3])));
        x0 = max(x0, (int) clip->x), y0 = max(y0, (int) clip->y);
        x1 = min(x1, (int) (clip->x + clip->w)), y1 = min(y1, (int) (clip->y + clip->h));
        x0 = max(x0, 0), y0 = max(y0, 0), x1 = min(x1, s->w), y1 = min(y1, s->h);
        if (x1 <= x0 || y1 <= y0)
            return;
        rs = s->format->Rshift, gs = s->format->Gshift, bs = s->format->Bshift;
        mask = ~(((Uint32) 255 << rs) | ((Uint32) 255 << gs) | ((Uint32) 255 << bs));
        ash = img->format->Ashift;
        if (SDL_MUSTLOCK(s))
            SDL_LockSurface(s);
        SDL_LockSurface(img);
        for (py = y0; py < y1; py++) {
            Uint32 *row = (Uint32 *) ((Uint8 *) s->pixels + (size_t) py * s->pitch);

            for (px = x0; px < x1; px++) {
                float ox = (float) px + 0.5f - fx, oy = (float) py + 0.5f - fy;
                float u = i00 * ox + i01 * oy, v = i10 * ox + i11 * oy, a;
                int sx = (int) floorf(fx + u - (float) dx), sy = (int) floorf(fy - v - (float) dy);
                int ia, m;
                Uint32 p;

                if (v < 0.0f || sx < 0 || sy < 0 || sx >= img->w || sy >= img->h)
                    continue;
                ia = (int) ((((Uint32 *) ((Uint8 *) img->pixels + (size_t) sy * img->pitch))[sx] >> ash)
                            & 255);
                if (!ia)
                    continue;
                a = (float) ia / 255.0f * a0 * (1.0f - 0.55f * (v > vtop ? 1.0f : v / vtop));
                m = (int) (256.0f * (1.0f - a));
                p = row[px];
                row[px] = (p & mask) | ((((p >> rs) & 255) * (Uint32) m >> 8) << rs)
                          | ((((p >> gs) & 255) * (Uint32) m >> 8) << gs)
                          | ((((p >> bs) & 255) * (Uint32) m >> 8) << bs);
            }
        }
        SDL_UnlockSurface(img);
        if (SDL_MUSTLOCK(s))
            SDL_UnlockSurface(s);
    }
}

/* the silhouette's axes for a light at (lx, ly) on the floor, hanging lift
   px above it: the shadow runs away from the light along the floor (the
   iso floor is squashed 2:1), longer the farther the light */
static void
vcast_draw(SDL_Surface *img, int dx, int dy, float fx, float fy, float lx, float ly, float lift,
           float a0, const SDL_Rect *clip)
{
    float ex = fx - lx, ey = (fy - ly) * 2.0f, dist = sqrtf(ex * ex + ey * ey), ux, uy, kk;
    float w[2], h[2];

    if (dist < 6.0f || !img)
        return;
    ux = ex / dist, uy = ey / dist;
    kk = dist / ((lift > 10.0f ? lift : 10.0f) * 1.2f);
    kk = kk < 0.3f ? 0.3f : kk > 1.6f ? 1.6f : kk;
    h[0] = ux * kk, h[1] = uy * kk * 0.5f;
    w[0] = -uy, w[1] = ux * 0.5f;
    vcast_one(img, dx, dy, fx, fy, w, h, a0, clip);
}

/* s191: every cast shadow on the visible map (after the floor pass) */
static void
vcast_pass(mapdata *md, int cx, int cy, int vx, int vy, int map_tr_y, int diff, int sum,
           const SDL_Rect *clip)
{
    int ti, tj, rot_dj, rot_di;

    static Uint32 vcs_log = 0; /* probe: s191 */
    bool plog = getenv("VULTURE_SELFTEST") && SDL_GetTicks() - vcs_log > 1000;

    if (plog) {
        vcs_log = SDL_GetTicks();
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "cast: %d lights\n", (int) vl_pls.size());
    }
    if (vl_pls.empty() || !md)
        return;
    for (ti = -map_tr_y; ti <= map_tr_y; ti++)
        for (tj = diff + ti; tj + ti <= sum; tj++) {
            int j, i, mon, obj, x, y, k, best = -1, lsx, lsy;
            float bw = 0.0f, a0;

            v_rot(tj, ti, vulture_view_rot, &rot_dj, &rot_di);
            j = vx + rot_dj, i = vy + rot_di;
            if (j < 1 || j >= COLNO || i < 0 || i >= ROWNO)
                continue;
            if ((!cansee(j, i) && !u_at(j, i)) || vl_S[i][j] < 0.04f)
                continue;
            mon = md->get_glyph(MAP_MON, j, i), obj = md->get_glyph(MAP_OBJ, j, i);
            if (mon == V_TILE_NONE && obj == V_TILE_NONE)
                continue;
            for (k = 0; k < (int) vl_pls.size(); k++) { /* the strongest light with a clear path */
                const vl_pl_t &p = vl_pls[k];
                float d = sqrtf((float) ((p.x - j) * (p.x - j) + (p.y - i) * (p.y - i)));
                float kk = 1.0f - d / (p.rad + 1.0f), w;

                if (d < 0.5f || kk <= 0.0f) /* its own light, or out of reach */
                    continue;
                w = p.inten * kk * kk;
                if (w > bw && clear_path(p.x, p.y, j, i))
                    bw = w, best = k;
            }
            if (best < 0)
                continue;
            a0 = bw / (vl_L[i][j] > 0.1f ? vl_L[i][j] : 0.1f) * 2.5f;
            a0 = VCS_MAX * (a0 > 1.0f ? 1.0f : a0) * (bw * 5.0f > 1.0f ? 1.0f : bw * 5.0f);
            if (plog) /* probe: s191 */
                vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                                  "cast: (%d,%d) mon %d obj %d light %d at (%d,%d) w %.2f L %.2f a0 %.2f gpu %d\n",
                                  j, i, mon != V_TILE_NONE, obj != V_TILE_NONE, best, vl_pls[best].x,
                                  vl_pls[best].y, bw, vl_L[i][j], a0, (int) vulture_gpumap_on);
            if (a0 < 0.04f)
                continue;
            {
                const vl_pl_t &p = vl_pls[best];

                if (p.mover)
                    vfx_mover_screen(p.x, p.y, cx, cy, vx, vy, &lsx, &lsy);
                else
                    vfx_square_screen(p.x, p.y, cx, cy, vx, vy, &lsx, &lsy);
                x = cx + V_MAP_XMOD * (tj - ti), y = cy + V_MAP_YMOD * (tj + ti);
                if (mon != V_TILE_NONE && !(u_at(j, i) && Invis && !See_invisible)) {
                    vulture_tile *t = vulture_get_tile(mon);

                    if (t && t->graphic) { /* posed as vtw_put_tile_x poses it */
                        int tx = x + vtw_ox[i][j], ty = y + vtw_oy[i][j];
                        int art = vtw_art_facing(mon, t->graphic), face = vtw_face[i][j];
                        bool flip = face != 0 && ((art == 0 && face < 0) || (art != 0 && art != face));
                        SDL_Surface *img = t->graphic, *f;
                        int dx = tx + t->xmod;

                        if (flip && (f = vtw_flipped(mon, t->graphic)) != NULL)
                            img = f, dx = tx - t->xmod - t->graphic->w + 1;
                        vcast_draw(img, dx, ty + t->ymod, (float) tx, (float) ty + 2.0f,
                                   (float) lsx, (float) lsy, p.lift, a0, clip);
                    }
                }
                if (obj != V_TILE_NONE) { /* boulders, statues: the big ones only */
                    vulture_tile *t = vulture_get_tile(obj);

                    if (t && t->graphic && t->graphic->h >= 44)
                        vcast_draw(t->graphic, x + t->xmod, y + t->ymod, (float) x, (float) y + 2.0f,
                                   (float) lsx, (float) lsy, p.lift, a0, clip);
                }
            }
        }
}

/*
 * IMPLEMENTATION_LOG s193: your conditions, on you.  What the status line
 * already says, shown on the hero too: stunned or confused, you sway (as a
 * dazed monster does, s162) and stars (stunned) or swirls (confused) circle
 * your head; blind, dark smoke curls about it; deadly countdowns pulse a
 * colour over you (turning to stone grey, to slime yellow-green, ill or
 * food-poisoned green, strangled red); hallucinating, you shimmer through
 * the colours; stressed or worse under your load, you sweat; levitating or
 * flying, you float (vbob_offset); invisible and not seeing invisible, you
 * are drawn faint.  Hero knowledge only - each of these is on the status
 * line.  Drawn over the lit map (the overlay), posed as the monster pass
 * drew you (vhp).
 */
static struct {
    bool ok;
    int tile, x, y, face;
    double sx, sy, deg;
} vhp; /* s193: how the hero was drawn this frame */

/* s193: the little sprites: 0 a star, 1 a swirl dot, 2 a puff of smoke,
   3 a drop of sweat (made once) */
static SDL_Surface *
vcond_sprite(int kind)
{
    static SDL_Surface *s[4];
    static const Uint32 col[4] = { 0xFFE65A, 0xBE8CFF, 0x19191F, 0x8CC8FF };
    int w = kind == 2 ? 18 : kind == 3 ? 5 : 9, h = kind == 3 ? 8 : w, x, y;

    if (s[kind] || kind < 0 || kind > 3)
        return kind < 0 || kind > 3 ? NULL : s[kind];
    if (!(s[kind] = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)))
        return NULL;
    SDL_LockSurface(s[kind]);
    for (y = 0; y < h; y++) {
        Uint32 *row = (Uint32 *) ((Uint8 *) s[kind]->pixels + y * s[kind]->pitch);

        for (x = 0; x < w; x++) {
            double dx = (x + 0.5 - w / 2.0) / (w / 2.0), dy = (y + 0.5 - h / 2.0) / (h / 2.0);
            double r = sqrt(dx * dx + dy * dy), a = 0.0;

            if (kind == 0) { /* a four-pointed star with a bright heart */
                double ax = fabs(dx), ay = fabs(dy);

                a = (ax < 0.22 ? 1.0 - ay : 0.0) + (ay < 0.22 ? 1.0 - ax : 0.0)
                    + (r < 0.45 ? 0.6 * (1.0 - r / 0.45) : 0.0);
            } else if (kind == 1) {
                a = r < 1.0 ? (1.0 - r) * (1.0 - r) * 1.6 : 0.0;
            } else if (kind == 2) {
                a = r < 1.0 ? 0.6 * pow(1.0 - r, 1.4) : 0.0;
            } else { /* a drop: round below, pointed above */
                double rr = sqrt(dx * dx + (dy > 0 ? dy * dy : dy * dy * 0.35));

                a = rr < 1.0 ? (1.0 - rr) * 1.4 : 0.0;
            }
            a = a < 0.0 ? 0.0 : a > 1.0 ? 1.0 : a;
            row[x] = ((Uint32) (a * 255) << 24) | col[kind];
        }
    }
    SDL_UnlockSurface(s[kind]);
    SDL_SetSurfaceBlendMode(s[kind], SDL_BLENDMODE_BLEND);
    return s[kind];
}

static void
vcond_put(int kind, double x, double y, int alpha)
{
    SDL_Surface *s = vcond_sprite(kind);

    if (!s || alpha <= 0)
        return;
    SDL_SetSurfaceAlphaMod(s, (Uint8) (alpha > 255 ? 255 : alpha));
    vulture_put_img((int) floor(x - s->w / 2.0), (int) floor(y - s->h / 2.0), s);
}

/* where a tile's art starts (its first fairly opaque row), from its anchor */
static int
vcond_top(int tile_id)
{
    static std::map<int, int> cache;
    vulture_tile *t = vulture_get_tile(tile_id);
    SDL_Surface *c;
    int row = 0, x, y;

    if (!t || !t->graphic)
        return -60;
    if (cache.count(tile_id))
        return cache[tile_id];
    if ((c = SDL_ConvertSurfaceFormat(t->graphic, SDL_PIXELFORMAT_ARGB8888, 0)) != NULL) {
        SDL_LockSurface(c);
        for (y = 0, row = -1; y < c->h && row < 0; y++) {
            const Uint32 *p = (const Uint32 *) ((Uint8 *) c->pixels + y * c->pitch);

            for (x = 0; x < c->w; x++)
                if ((p[x] >> 24) > 60) {
                    row = y;
                    break;
                }
        }
        SDL_UnlockSurface(c);
        SDL_FreeSurface(c);
    }
    cache[tile_id] = t->ymod + (row < 0 ? 0 : row);
    return cache[tile_id];
}

static void
vcond_draw(void)
{
    double t = SDL_GetTicks() / 1000.0, pulse = 0.5 + 0.5 * sin(t * 4.0), hx, hy;
    int tint = -1, ta = 0, k;

    if (!vhp.ok || u.uswallow || Underwater)
        return;
    /* a colour pulsing over you: the deadly ones first */
    if (Stoned)
        tint = 0x9A9A9A, ta = (int) (70 + 80 * pulse);
    else if (Slimed)
        tint = 0x8CC828, ta = (int) (60 + 80 * pulse);
    else if (Strangled)
        tint = 0xD22828, ta = (int) (60 + 90 * pulse);
    else if (Sick)
        tint = 0x50C83C, ta = (int) (50 + 70 * pulse);
    else if (Hallucination) { /* through the colours */
        double h6 = fmod(t * 0.3, 1.0) * 6.0, f = h6 - floor(h6);
        int seg = (int) h6, r, g, b, up = (int) (255 * f), dn = 255 - up;

        r = seg == 0 || seg == 5 ? 255 : seg == 1 ? dn : seg == 4 ? up : 0;
        g = seg == 1 || seg == 2 ? 255 : seg == 0 ? up : seg == 3 ? dn : 0;
        b = seg == 3 || seg == 4 ? 255 : seg == 2 ? up : seg == 5 ? dn : 0;
        tint = (r << 16) | (g << 8) | b, ta = 75;
    }
    if (tint >= 0) {
        vtx_tint = tint;
        vtw_put_tile_x(vhp.x, vhp.y, vhp.tile, vhp.face, vhp.sx, vhp.sy, vhp.deg, false, ta);
        vtx_tint = -1;
    }
    hx = vhp.x, hy = vhp.y + vcond_top(vhp.tile) + 5 + vbob_offset(u.ux, u.uy);
    for (k = 0; k < 3; k++) { /* things circling your head: behind it dimmer */
        double a = t * 3.2 + k * 2.0944, sn = sin(a);

        if (Stunned)
            vcond_put(0, hx + cos(a) * 15.0, hy + sn * 5.0, sn < 0 ? 120 : 235);
        else if (Confusion) {
            double a2 = -t * 2.4 + k * 2.0944, rr = 12.0 + 4.0 * sin(t * 5.0 + k);

            vcond_put(1, hx + cos(a2) * rr, hy + sin(a2) * rr * 0.4, sin(a2) < 0 ? 110 : 220);
        }
        if (Blind) {
            double a3 = t * 0.9 + k * 2.0944;

            vcond_put(2, hx + cos(a3) * 13.0, hy + 4.0 + sin(a3) * 6.0, 170);
        }
    }
    if (near_capacity() >= MOD_ENCUMBER) /* stressed or worse: sweat */
        for (k = 0; k < 2; k++) {
            double ph = fmod(t * 1.25 + k * 0.5, 1.0);

            vcond_put(3, hx + (k ? 9.0 : -9.0), hy + 6.0 + ph * 14.0, (int) (210 * (1.0 - ph)));
        }
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
    vkeys_load(); /* s195: keys changed in the game */
    vimp_tick(); /* s151 */
    {
        int jx, jy;

        vimp_offset(&jx, &jy); /* s151: a heavy landing jolts the view */
        map_centre_x += vtw_gx + (int) vpan_px + jx; /* s79: + MMB drag residual */
        map_centre_y += vtw_gy + (int) vpan_py + jy;
    }

    if (vzoom > 1.0)
        prev_cx = -1; /* s121: zoomed, every frame is a full redraw */
    { /* s183: the map on the GPU when there is a renderer and lighting is
         off (the light passes work on the map's pixels); switching between
         the two, or textures lost, redraws everything */
        extern SDL_Renderer *vulture_gpu;
        static int last_gpu = -1;
        int want_gpu;

        vulture_gpu_use_gl(vl_on && vulture_gpumap_wanted); /* s185: OpenGL only while lit */
        want_gpu = (!vl_on || vulture_gpulit_ok()) /* s185: lit too, given the shader */
                   && vulture_gpumap_wanted && vulture_gpu != NULL;

        if (want_gpu != last_gpu || vulture_gpumap_lost) {
            prev_cx = -1;
            last_gpu = want_gpu;
            vulture_gpumap_lost = false;
        }
    }
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
        vulture_gpumap_abandon(); /* s183: drawn in software, opaque */
        prev_cx = -1;             /* and all of it again afterwards */
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
    bool vgm = false; /* s183: tiles and sprites into the GPU map layer */

    if (!vl_on || vulture_gpulit_ok()) { /* s185: lighting on the GPU too */
        SDL_Rect cr;

        cr.x = clip_tl_x, cr.y = clip_tl_y;
        cr.w = clip_br_x + 1 - clip_tl_x, cr.h = clip_br_y + 1 - clip_tl_y;
        { /* s201: zoomed in with HD art - the map layer is drawn at the zoom */
            SDL_Rect zs, zd;

            if (vulture_gpumap_hd_ok()
                && vzoom_rects(this->abs_x, this->abs_y, this->w, this->h, &zs, &zd))
                vulture_gpumap_set_view(&zs, &zd);
            else
                vulture_gpumap_set_view(NULL, NULL);
        }
        vgm = vulture_gpumap_begin(&cr);
    }
    if (vgm)
        SDL_FillRect(vulture_screen, NULL, 0); /* the overlay starts clear over the map */
    else
        vulture_gpumap_abandon();
    vl_gpu_frame = vgm && vl_on; /* s185 */
    /* s187: while lighting is on, the light layer draws the fog of war -
       remembered squares' tiles aren't darkened (vl_render's own test) */
    bool vl_fog = vl_on && u.ux && !u.uswallow && this->w >= 32 && this->h >= 32;
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
                shadelevel = vl_fog ? 0 : map_darkness; /* s187 */
            } else if (tile_id == V_TILE_NONE
                       || tile_id == V_TILE_WALL_GENERIC)
                tile_id = V_MISC_UNMAPPED_AREA;

            /* s187: while lit the void is black (its tile is a dark grey
               texture, which the light layer drew as the level's outline) */
            vulture_put_tile_shaded(x, y, vl_fog && tile_id == V_MISC_UNMAPPED_AREA ? V_MISC_OFF_MAP
                                                                                  : tile_id,
                                    shadelevel);

            /* shortcut for unmapped case */
            if (tile_id == V_MISC_UNMAPPED_AREA)
                continue;

            if (vulture_opts.highlight_cursor_square
                && (j == map_highlight.x && i == map_highlight.y))
                vulture_put_tile(x, y, V_MISC_FLOOR_HIGHLIGHT);

            /* 3. Floor edges */
            for (dir_idx = 0; dir_idx < 12; dir_idx++)
                vulture_put_tile(x, y, maptile_floor_edge[i][j].dir[dir_idx]);

            /* s214: cracks, pebbles, a puddle, moss, old bones */
            if (map_back >= V_TILE_FLOOR_COBBLESTONE && map_back <= V_TILE_FLOOR_DARK)
                vdecal_draw(j, i, x, y, get_floor_decor(map_back, i, j), map_deco[i][j] != 0,
                            map_data->get_glyph(MAP_FURNITURE, j, i) != V_TILE_NONE);

            /* s205: the foot of the walls behind, the slab's faces in front */
            vao_square(map_data, j, i, x, y, map_darkness, vl_fog);
        }
    }
    vslab_draw(); /* s205: over the void's tiles, under everything standing */
    vxr_faces.clear(), vxr_mons.clear(), vxr_seq = 0; /* s210 */
    vtorch_list.clear(); /* s211 */
    vrock_prepare(map_data, view_x, view_y, map_tr_y, diff, sum); /* s215: the rock mass */
    vtop_prepare(map_data, maptile_wall); /* s225: each top's stone and light */
    vghost_update(map_data); /* s221: who you see, who you lost */
    vwall_hole_on = u.ux && !u.uswallow && !vwall_probe_off; /* s209: the walls in front of you see-through round you */
    if (vwall_hole_on) {
        double o = vulture_opts.wall_opacity;

        vwall_amin = (int) ((o < 0.0 ? 0.0 : o > 1.0 ? 1.0 : o) * 255.0 + 0.5);
        vfx_mover_screen(u.ux, u.uy, map_centre_x, map_centre_y, view_x, view_y, &vwall_hx, &vwall_hy);
    }
    vcut_prepare(map_data, view_x, view_y, map_tr_y, diff, sum); /* s223: the room you are in, whole */
    vhp.ok = false; /* s193 */
    if (vl_fog && vfx_shadows) { /* s191: shadows cast by the lights, on the floor */
        SDL_Rect cr = { (Sint16) this->abs_x, (Sint16) this->abs_y, (Uint16) this->w,
                        (Uint16) this->h };
        bool cap = vulture_normal_capture;

        vulture_normal_capture = false;
        vcast_pass(map_data, map_centre_x, map_centre_y, view_x, view_y, map_tr_y, diff, sum, &cr);
        vulture_normal_capture = cap;
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
            vcut_now = vcut_a[i][j]; /* s223 */
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

            /* 1. West and north walls (s209: a room's front walls - solid,
               their face toward you; see-through round you).  s215: walls
               are blocks now - these faces look away from you; the block's
               top and the rock in front of it close it (vrock_draw) */
            if (!vrock_on) {
                if (nwx >= 1 && nwx < COLNO && nwy >= 0 && nwy < ROWNO) /* s212: its outside faces (nex, ney) */
                    vwall_draw(x, y, maptile_wall[i][j].west, 0,
                               vl_fog ? 0 : map_data->get_glyph(MAP_DARKNESS, nwx, nwy), nex, ney); /* s187 */
                if (nnx >= 1 && nnx < COLNO && nny >= 0 && nny < ROWNO) /* s212: its outside faces (nsx, nsy) */
                    vwall_draw(x, y, maptile_wall[i][j].north, 1,
                               vl_fog ? 0 : map_data->get_glyph(MAP_DARKNESS, nnx, nny), nsx, nsy); /* s187 */
            }

            /* shortcut for unmapped case */
            if (map_back != V_MISC_UNMAPPED_AREA || map_obj != V_TILE_NONE) {
                /* 2. Furniture (s62: a quarter turn swaps the door
                   arch art between the two wall directions; s203: a door
                   part way through a swing is drawn by vdr_draw) */
                {
                    int furn = map_data->get_glyph(MAP_FURNITURE, j, i);

                    if (!vdr_draw(x, y, j, i, furn) && !vtree_draw(x, y, j, i, furn)) { /* s206: trees sway */
                        vulture_tile *dt = vcut_now < 255 && furn != V_TILE_NONE ? vulture_get_tile(v_rot_door(furn)) : NULL;

                        if (dt && dt->graphic) /* s223: in your room's front wall - see-through */
                            SDL_SetSurfaceAlphaMod(dt->graphic, (Uint8) vcut_now);
                        if (vwall_hole_on && furn != V_TILE_NONE && IS_DOOR(levl[j][i].typ))
                            /* s210: a door in front of you, see-through round you */
                            vhole_put(x, y, v_rot_door(furn), 0, (float) (y - vwall_hy) / 22.0f);
                        else
                            vulture_put_tile(x, y, v_rot_door(furn));
                        if (dt && dt->graphic)
                            SDL_SetSurfaceAlphaMod(dt->graphic, 255);
                    }
                }

                /* s93: blood stains on the floor */
                vst_draw(j, i, x, y);

                /* 3. Traps */
                vulture_put_tile(x, y, map_data->get_glyph(MAP_TRAP, j, i));
                vmagic_sprite(j, i, x, y, map_data->get_glyph(MAP_TRAP, j, i)); /* s154 */

                /* 4. Objects (s83: falling while a drop plays) */
                if (map_obj != V_TILE_NONE && vfx_shadows && vdth_object_alpha(j, i) >= 255) { /* s158: grounded */
                    vulture_tile *ot = vulture_get_tile(map_obj);
                    int rox = 0, roy = 0;

                    vbr_offset(j, i, &rox, &roy); /* s204: under a boulder rolling in */
                    if (ot && ot->graphic)
                        vshadow(x + rox, y + 2 + roy, ot->graphic->w * 3 / 4, 90);
                }
                vanim_draw_object(j, i, x, y, map_obj);
                if (map_obj != V_TILE_NONE) { /* s212: an item the walls in front of it may hide */
                    vxr_mon_t mr;

                    mr.tile = map_obj, mr.face = 0, mr.tx = x, mr.ty = y, mr.seq = ++vxr_seq;
                    mr.xs = mr.ys = 1.0, mr.deg = 0.0;
                    mr.r = 165, mr.g = 225, mr.b = 255; /* items: pale blue */
                    vxr_mons.push_back(mr);
                }

                /* 5. Monsters */
                /* s193: invisible and not seeing it, the game shows nothing
                   where you stand - you know where you are: drawn faint */
                if (map_mon == V_TILE_NONE && u_at(j, i) && Invis && !canspotself())
                    map_mon = MONSTER_TO_VTILE(u.umonnum);
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
                            } else if (u_at(j, i) && (Stunned || Confusion)) { /* s193: you too */
                                double tt2 = SDL_GetTicks() / 1000.0;

                                deg += (Stunned ? 9.0 : 6.0) * sin(tt2 * (Stunned ? 5.0 : 3.3) + j);
                                tx += (int) (2.0 * sin(tt2 * 2.7 + i));
                            }
                        }
                    }
                    vanim_xform(j, i, &tx, &ty, &xs, &ys, &deg, &red);
                    if (u_at(j, i)) /* s216: down the stairs, up them, dropping in */
                        ty += vstair_hero_dy();
                    { /* s206: it turns in depth, watches you, fidgets */
                        int face = vlife_apply(j, i, map_mon, vtw_face[i][j], vtw_mv[i][j] != 0, tx, &xs, &ys, &deg,
                                               &ty);
                        int ha = 255;

                        if (u_at(j, i)) { /* s193: how you were drawn, for vcond_draw */
                            if (Invis && !See_invisible)
                                ha = 150; /* invisible: faint (110 was too faint to read) */
                            vhp.ok = true, vhp.tile = map_mon, vhp.x = tx, vhp.y = ty;
                            vhp.face = face, vhp.sx = xs, vhp.sy = ys, vhp.deg = deg;
                        }
                        if (ha >= 255) /* s213: mirrored in the water in front of it */
                            vrefl_draw(map_data, j, i, map_mon, face, tx, ty, xs, ys, map_centre_x, map_centre_y,
                                       view_x, view_y);
                        vtw_put_tile_x(tx, ty, map_mon, face, xs, ys, deg, red, ha);
                        if (!u_at(j, i)) { /* s210: what the walls in front of it may hide */
                            struct monst *om = m_at(j, i);
                            vxr_mon_t mr;

                            mr.tile = map_mon, mr.face = face, mr.tx = tx, mr.ty = ty, mr.seq = ++vxr_seq;
                            mr.xs = xs, mr.ys = ys, mr.deg = deg;
                            if (om && !DEADMONSTER(om) && canspotmon(om)) {
                                if (om->mtame)
                                    mr.r = 120, mr.g = 235, mr.b = 120;
                                else if (om->mpeaceful)
                                    mr.r = 245, mr.g = 215, mr.b = 95;
                                else
                                    mr.r = 255, mr.g = 85, mr.b = 65;
                            } else {
                                mr.r = 205, mr.g = 205, mr.b = 215;
                            }
                            vxr_mons.push_back(mr);
                        }
                    }
                    if (iflags.hilite_pet
                        && map_data->get_glyph(MAP_PET, j, i))
                        vulture_put_img(
                            tx + cur_tile->xmod, ty + cur_tile->ymod - 10,
                            vulture_get_tile(V_MISC_HILITE_PET)->graphic);
                } else if (!vghost_list.empty()) {
                    vghost_draw(j, i, x, y); /* s221: last seen here */
                }

                /* s78 / s204: a slain monster falling over */
                vdth_draw(j, i, x, y);

                /* 6. Effects (s198: a ray is drawn as light by vsp_draw; an explosion pops) */
                vsp_put_special(x, y, j, i, map_data->get_glyph(MAP_SPECIAL, j, i));
            }

            /* 7. South & East walls (s209: see-through round you when you
               are behind them) */
            if (nsx >= 1 && nsx < COLNO && nsy >= 0 && nsy < ROWNO) {
                vwall_draw(x, y, maptile_wall[i][j].south, 2,
                           vl_fog ? 0 : map_data->get_glyph(MAP_DARKNESS, nsx, nsy), /* s187 */
                           -1, -1, vrock_out_dim(map_data, nsx, nsy)); /* s218: an outside, dark */
                vtorch_draw(x, y, j, i, nsx, nsy, maptile_wall[i][j].south, false); /* s211 */
            }
            if (nex >= 1 && nex < COLNO && ney >= 0 && ney < ROWNO) {
                vwall_draw(x, y, maptile_wall[i][j].east, 3,
                           vl_fog ? 0 : map_data->get_glyph(MAP_DARKNESS, nex, ney), /* s187 */
                           -1, -1, vrock_out_dim(map_data, nex, ney)); /* s218: an outside, dark */
                vtorch_draw(x, y, j, i, nex, ney, maptile_wall[i][j].east, true); /* s211 */
            }
            vrock_draw(map_data, j, i, x, y, nsx, nsy, nex, ney, maptile_wall); /* s215, s218: its block - faces, top */
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
    if (vgm)
        vulture_gpumap_end(); /* s183: effects and the HUD go into the overlay */
    Uint32 vpf_b = SDL_GetTicks(); /* probe */
    vl_render(map_data, map_centre_x, map_centre_y, view_x, view_y, this->abs_x,
              this->abs_y, this->w, this->h); /* s133: dynamic lighting */
    Uint32 vpf_c = SDL_GetTicks(); /* probe */
    vw_draw(map_centre_x, map_centre_y, view_x, view_y, this->abs_x,
            this->abs_y, this->w, this->h); /* s86: light, night, dust, panel */
    Uint32 vpf_d = SDL_GetTicks(); /* probe */
    vhalo_draw(map_centre_x, map_centre_y, view_x, view_y); /* s171 */
    vcond_draw(); /* s193: your conditions, on you */
    vambient_draw(this->abs_x, this->abs_y, this->w, this->h); /* s160 */
    vxr_draw(); /* s210: creatures behind walls, outlined */
    vfx_draw(map_centre_x, map_centre_y, view_x, view_y); /* s85 */
    vsp_draw(map_data, map_centre_x, map_centre_y, view_x, view_y); /* s198 */
    vpath_draw(map_centre_x, map_centre_y, view_x, view_y); /* s125 */
    vmp_mark(map_centre_x, map_centre_y, view_x, view_y);   /* s130 */
    vthreat_draw(map_centre_x, map_centre_y, view_x, view_y); /* s144 */
    vtele_draw(map_centre_x, map_centre_y, view_x, view_y);   /* s163 */
    vaim_draw(map_centre_x, map_centre_y, view_x, view_y);  /* s125 */
    vblast_draw(map_centre_x, map_centre_y, view_x, view_y); /* s138 */
    vstair_fade(this->abs_x, this->abs_y, this->w, this->h); /* s216: leaving, arriving */
    vzoom_apply(this->abs_x, this->abs_y, this->w, this->h); /* s121 */
    Uint32 vpf_e = SDL_GetTicks(); /* probe */
    vulture_draw_vignette(this->abs_x, this->abs_y, this->w, this->h); /* s74 */
    Uint32 vpf_f = SDL_GetTicks(); /* probe */
    vstatus_pulse_draw(this->abs_x, this->abs_y, this->w, this->h); /* s129 */
    vmp_draw(this->abs_x, this->abs_y, this->w, this->h); /* s130 */
    vphoto_draw(this->abs_x, this->abs_y, this->w, this->h); /* s170 */
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
    if (vpan_probe_t0) /* probe: s186 */
        vpan_probe_frames++;
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

    /* hovering on a border: scroll (s199: the scroll hotspots only - any
       child with a menu_id used to land here, so moving the mouse over the
       status panel switched camera-follow off) */
    if (target != this && target->menu_id >= V_HOTSPOT_SCROLL_UPLEFT
        && target->menu_id <= V_HOTSPOT_SCROLL_DOWNRIGHT) {
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
        {
            std::string st;

            if (vulture_status_tip(mp.x, mp.y, st)) { /* s199: the status panel */
                if (!st.empty())
                    vulture_mouse_set_tooltip(st);
                return V_EVENT_HANDLED_NOREDRAW;
            }
        }
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
        if (vpray_rect.w && mp.x >= vpray_rect.x && mp.x < vpray_rect.x + vpray_rect.w
            && mp.y >= vpray_rect.y && mp.y < vpray_rect.y + vpray_rect.h) { /* s174 */
            const char *why, *tr;
            int adv = vulture_prayer_advice(&why, &tr);
            std::string t;

            if (adv == 1) /* s180: only what the hero knows */
                t = std::string("You're ") + tr + ".  Praying should save you: " + why + ".\n"
                    "(A god you've secretly displeased - bad luck, misdeeds - may still refuse.)\n"
                    "Click here (or #pray) to pray.";
            else if (adv == 3)
                t = std::string("You're ") + tr + ", and " + why + ".\n"
                    "A prayer that comes too soon angers your god - but if you'd die\n"
                    "otherwise, it's a gamble worth taking.  Click here to pray anyway.";
            else if (adv == 2)
                t = std::string("You're ") + tr + ", but don't pray: " + why + ".\n"
                    "Instead: retreat, quaff a healing potion, use an escape item,\n"
                    "or engrave Elbereth in the dust (most monsters won't step on it).";
            if (!t.empty()) {
                vulture_mouse_set_tooltip(t);
                return V_EVENT_HANDLED_NOREDRAW;
            }
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

        { /* s186: every drag event already waiting goes into this one move,
             so one redraw covers them all - a redraw per event (a mouse
             sends hundreds a second) left the camera trailing the mouse */
            SDL_Event mev;

            SDL_PumpEvents();
            while (SDL_PeepEvents(&mev, 1, SDL_PEEKEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) > 0
                   && (mev.motion.state & SDL_BUTTON_MMASK)) {
                SDL_PeepEvents(&mev, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION);
                vulture_handle_global_event(&mev); /* render space, pointer position */
                xrel += mev.motion.xrel, yrel += mev.motion.yrel;
            }
        }
        if (vpan_probe_t0) /* probe: s186 */
            vpan_probe_dx += xrel, vpan_probe_dy += yrel, vpan_probe_moves++;
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

    { /* s186: a newer motion event is already waiting - let it do the work */
        SDL_Event mev;

        if (SDL_PeepEvents(&mev, 1, SDL_PEEKEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) > 0
            && !(mev.motion.state & SDL_BUTTON_MMASK))
            return V_EVENT_HANDLED_NOREDRAW;
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
        || vgs_hit(mouse.x, mouse.y) >= 0
        || (vpray_rect.w && mouse.x >= vpray_rect.x && mouse.x < vpray_rect.x + vpray_rect.w
            && mouse.y >= vpray_rect.y && mouse.y < vpray_rect.y + vpray_rect.h)) { /* s126, s145, s174 */
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
    if (vpray_rect.w && mouse.x >= vpray_rect.x && mouse.x < vpray_rect.x + vpray_rect.w
        && mouse.y >= vpray_rect.y && mouse.y < vpray_rect.y + vpray_rect.h
        && button == SDL_BUTTON_LEFT) { /* s174: pray from the badge */
        if (vpray_adv == 1 || vpray_adv == 3) { /* s180: PRAY? prays too */
            cmdq_add_ec(CQ_CANNED, dopray);
            vulture_click_consumed = TRUE;
            vauto_result(result);
            return V_EVENT_HANDLED_FINAL;
        }
        vulture_sfx("ui_error");
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
        vfxm_save(); /* s182: remembered */
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
                int fbg = bg, fx = nx, fy = ny, k2;

                /* s209: a room wall's face toward a doorway or a corridor is
                   that room's wall seen from there - the room's style, not
                   the corridor's rough rock (solid full-height front walls
                   made the doorways' rough sides stand out).  s213: toward
                   any floor but a room's (a pool, ice, lava...) the same -
                   get_wall_decor gives those plain brick */
                if (bg != V_TILE_FLOOR_COBBLESTONE && bg != V_MISC_FLOOR_NOT_VISIBLE)
                    for (k2 = 0; k2 < 8; k2++) { /* beside it first, then corner to corner */
                        static const int ndx[8] = { -1, 0, 1, 0, -1, 1, 1, -1 };
                        static const int ndy[8] = { 0, -1, 0, 1, -1, -1, 1, 1 };
                        int ox = x + ndx[k2], oy = y + ndy[k2], b2;

                        if (ox < 0 || ox >= COLNO || oy < 0 || oy >= ROWNO)
                            continue;
                        b2 = map_data->get_glyph(MAP_BACK, ox, oy);
                        if (b2 == V_TILE_FLOOR_COBBLESTONE || b2 == V_MISC_FLOOR_NOT_VISIBLE) {
                            fbg = b2, fx = ox, fy = oy;
                            break;
                        }
                    }
                style = get_wall_decor(fbg, y, x, fy, fx);
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
