/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

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
        if ((dx || dy) && abs(dx) <= 1 && abs(dy) <= 1) {
            int sx, sy;

            moved = true;
            vtw_proj(dx, dy, &sx, &sy);
            if (sx > 0)
                it->second.face = 1;
            else if (sx < 0)
                it->second.face = -1;
        }
    }
    if (snap_ok && vtw_snap_vx >= 0 && (vx != vtw_snap_vx || vy != vtw_snap_vy)
        && abs(vx - vtw_snap_vx) <= 1 && abs(vy - vtw_snap_vy) <= 1)
        moved = true;

    if (moved && !vtw_animating && !vtw_pending) {
        vtw_pending = true;
        vtw_t = 0.0;
    }
    if (!vtw_pending && !vtw_animating) {
        vtw_commit(vx, vy);
        for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it)
            vtw_face[it->second.y][it->second.x] = it->second.face;
        return;
    }

    t = vtw_t;
    te = t * t * (3.0 - 2.0 * t); /* smoothstep */
    vtw_te = te;
    for (it = vtw_cur.begin(); it != vtw_cur.end(); ++it) {
        int x = it->second.x, y = it->second.y;

        vtw_face[y][x] = it->second.face;
        sn = vtw_snap.find(it->first);
        if (sn == vtw_snap.end() || !snap_ok)
            continue;
        int dx = sn->second.x - x, dy = sn->second.y - y;
        if ((dx || dy) && abs(dx) <= 1 && abs(dy) <= 1) {
            int sx, sy;

            vtw_proj(dx, dy, &sx, &sy);
            vtw_ox[y][x] = (short) ((1.0 - te) * sx);
            /* s78/s79: a hop on each step - airborne for the first 0.8,
               then a landing squash (drawn in levelwin::draw) */
            vtw_oy[y][x] = (short) ((1.0 - te) * sy
                                    - 12.0 * sin(3.14159265358979 * (te < 0.8 ? te / 0.8 : 1.0)));
            vtw_mv[y][x] = 1;
        }
    }
    if (snap_ok && vtw_snap_vx >= 0 && abs(vx - vtw_snap_vx) <= 1
        && abs(vy - vtw_snap_vy) <= 1) {
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
    int x, y, tile;
};
static vanim_death vanim_deaths[8];
static int vanim_ndeaths = 0, vanim_dtile = -1, vanim_dx = 0, vanim_dy = 0;

/* s81: animations never hold the player up - a keypress or click waiting
   in the queue ends one at once */
static bool
vanim_input_waiting(void)
{
    SDL_PumpEvents();
    return SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_KEYDOWN, SDL_KEYDOWN) > 0
           || SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_MOUSEBUTTONDOWN,
                             SDL_MOUSEBUTTONDOWN) > 0;
}

static void
vanim_frames(int kind, int frames, int delay_ms)
{
    Uint32 t0 = SDL_GetTicks();
    int f;

    vanim_kind = kind;
    for (f = 1; f <= frames; f++) {
        Uint32 tf = SDL_GetTicks();

        vanim_t = (double) f / frames;
        levwin->force_redraw();
        ROOTWIN->draw_windows();
        vulture_refresh_window_region();
        if (vanim_input_waiting())
            break;
        /* slow machine/big window: keep the whole thing under ~150 ms */
        if (f == 1 && SDL_GetTicks() - tf > 25 && frames > 3)
            frames = 3;
        if (SDL_GetTicks() - t0 > 150)
            break;
        SDL_Delay(delay_ms);
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
    vanim_ax = ax, vanim_ay = ay, vanim_tx = tx, vanim_ty = ty;
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

        oy = -9.0 * sin(3.14159265358979 * u);
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

                    if (x < 0 || y < 0 || x >= w || y >= h || dx * dx + dy * dy > 1.0)
                        continue;
                    ((Uint32 *) ((Uint8 *) s->pixels + y * s->pitch))[x]
                        = SDL_MapRGBA(s->format, 95, 6, 8, 210);
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

struct vfx_ent {
    int kind;                 /* 1 number, 2 blood */
    int x, y;                 /* map square */
    Uint32 t0, dur;
    char txt[16];
    Uint8 r, g, b;
    float vx[10], vy[10];
};
static std::vector<vfx_ent> vfx_list;
static std::map<unsigned, int> vfx_hp;
static int vfx_hero_hp = -1, vfx_hero_poly = -1;
static d_level vfx_lev = { -1, -1 };

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
        if (hp < vfx_hero_hp)
            vfx_hit(u.ux, u.uy, vfx_hero_hp - hp, 255, 70, 60, true);
        else
            vfx_hit(u.ux, u.uy, -(hp - vfx_hero_hp), 90, 230, 90, false);
    }
    vfx_hero_hp = hp, vfx_hero_poly = poly;
    for (m = fmon; m; m = m->nmon) {
        std::map<unsigned, int>::iterator it;

        if (DEADMONSTER(m) || !canspotmon(m))
            continue;
        it = vfx_hp.find(m->m_id);
        if (it != vfx_hp.end() && m->mhp < it->second)
            vfx_hit(m->mx, m->my, it->second - m->mhp, 255, 245, 210,
                    !noncorporeal(m->data));
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

static void
vfx_draw(int cx, int cy, int vx, int vy)
{
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

    for (i = 0; i < vfx_list.size(); i++) {
        vfx_ent &e = vfx_list[i];
        double age = (double) (now - e.t0) / e.dur, ts = (now - e.t0) / 1000.0;

        if (age >= 1.0)
            continue;
        vfx_square_screen(e.x, e.y, cx, cy, vx, vy, &sx, &sy);
        if (e.kind == 1) {
            int ty = sy - 62 - (int) (34 * (1.0 - (1.0 - age) * (1.0 - age)));
            int w = vulture_text_length(V_FONT_LARGE, e.txt);
            double dim = age < 0.6 ? 1.0 : 1.0 - (age - 0.6) / 0.4;

            vulture_put_text_shadow(V_FONT_LARGE, e.txt, vulture_screen,
                                    sx - w / 2, ty,
                                    SDL_MapRGB(vulture_screen->format,
                                               (Uint8) (e.r * dim), (Uint8) (e.g * dim),
                                               (Uint8) (e.b * dim)),
                                    SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        } else if (e.kind == 2) {
            Uint32 col = SDL_MapRGB(vulture_screen->format, 150, 10, 10);

            for (k = 0; k < 10; k++) {
                int px = sx + (int) (e.vx[k] * ts);
                int py = sy - 30 + (int) (e.vy[k] * ts + 0.5 * 420.0 * ts * ts);

                vfx_dot(px, py, age < 0.6 ? 3 : 2, col);
            }
        }
    }
}

/* idle breathing: ~3% vertical, out of phase per creature */
static void
vfx_breathe(int j, int i, double *xs, double *ys)
{
    struct monst *m;
    double ph, s;

    if (u_at(j, i))
        ph = 0.3;
    else if ((m = m_at(j, i)) != 0 && canspotmon(m))
        ph = m->m_id * 0.7;
    else
        return;
    s = sin(SDL_GetTicks() / 1000.0 * 2.4 + ph);
    *ys *= 1.0 + 0.03 * s;
    *xs *= 1.0 - 0.015 * s;
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
            if (is_lava(j, i) && cansee(j, i)) {
                vfx_square_screen(j, i, cx, cy, vx, vy, &sx, &sy);
                if (sx < x0 - 80 || sy < y0 - 80 || sx > x0 + w + 80
                    || sy > y0 + h + 80)
                    continue;
                vw_glow_at(sx, sy, 70,
                           0.55 + 0.15 * sin(now / 300.0 + j * 1.3 + i * 0.7));
            }
    /* light sources */
    for (ls = gl.light_base; ls; ls = ls->next) {
        if (!isok(ls->x, ls->y) || ls->range <= 0
            || !(cansee(ls->x, ls->y) || u_at(ls->x, ls->y)))
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

    /* monster panel */
    {
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
 * is copied, at a third of its size, into a ring of the last 40 turns.
 * When the hero dies for good, really_done() plays the ring back - sepia,
 * oldest to newest, "Your last moments..." - before the end-of-game
 * questions.  Any key or click skips it.
 */
#define VRP_N 40
static SDL_Surface *vrp_frame[VRP_N];
static int vrp_head = 0, vrp_count = 0;
static d_level vrp_lev = { -1, -1 };

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
    if (!f || f->w != src.w / 3 || f->h != src.h / 3) {
        if (f)
            SDL_FreeSurface(f);
        f = SDL_CreateRGBSurfaceWithFormat(0, src.w / 3, src.h / 3, 32,
                                           SDL_PIXELFORMAT_ARGB8888);
        vrp_frame[vrp_head] = f;
        if (!f)
            return;
    }
    dst.x = dst.y = 0, dst.w = f->w, dst.h = f->h;
    SDL_BlitScaled(vulture_screen, &src, f, &dst);
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
        SDL_SetSurfaceColorMod(f, 225, 195, 150); /* sepia-ish */
        SDL_BlitScaled(f, NULL, vulture_screen, &d);
        SDL_SetSurfaceColorMod(f, 255, 255, 255);
        vulture_put_text_shadow(V_FONT_LARGE, cap, vulture_screen,
                                d.x + d.w / 2 - vulture_text_length(V_FONT_LARGE, cap) / 2,
                                d.y + d.h - 60,
                                SDL_MapRGB(vulture_screen->format, 235, 215, 170),
                                SDL_MapRGB(vulture_screen->format, 0, 0, 0));
        win = SDL_GetWindowSurface(vulture_sdl_window);
        nhUse(win);
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
static Uint32 vidle_interval = 80; /* s85: grows if frames are slow */

int
vulture_idle_wait_ms(bool at_map)
{
    nhUse(vbob_wanted);
    return at_map ? 40 : 100;
}

/* one idle frame for bobbing, breathing and combat effects (called from
   the input loop while the map waits for a command) */
void
vulture_idle_bob(bool at_map)
{
    static Uint32 last = 0;
    Uint32 now = SDL_GetTicks(), took;

    if (!at_map || !levwin || vulture_windows_inited != TRUE || vtw_animating
        || vanim_kind || now - last < vidle_interval || vanim_input_waiting())
        return;
    last = now;
    (void) vfx_alive();
    levwin->force_redraw();
    ROOTWIN->draw_windows();
    vulture_refresh_window_region();
    /* keep idle drawing to about a third of the time at most */
    took = SDL_GetTicks() - now;
    vidle_interval = took * 3 > 80 ? (took * 3 > 400 ? 400 : took * 3) : 80;
}

/* play the in-between frames of a pending tween, then any queued death
   animations (vulture_win.cpp calls this after each refresh; re-entry is
   blocked while animating) */
void
vulture_tween_run(void)
{
    int f, frames;

    if (vtw_animating || vanim_kind || !levwin)
        return;
    /* s81: running, travelling, auto-explore and repeated commands snap -
       animating every step there is what made long walks crawl */
    if (vtw_pending && (svc.context.run || svc.context.travel || gm.multi
                        || autoexplore_active)) {
        vtw_pending = false;
        vtw_t = 1.0;
        vtw_commit(vtw_last_vx, vtw_last_vy);
        levwin->force_redraw();
    }
    if (vtw_pending) {
        Uint32 t0 = SDL_GetTicks();

        vtw_animating = true;
        frames = 9;
        for (f = 1; f <= frames; f++) {
            Uint32 tf = SDL_GetTicks();

            vtw_t = (double) f / frames;
            levwin->force_redraw();
            ROOTWIN->draw_windows();
            vulture_refresh_window_region();
            if (f < frames && vanim_input_waiting()) {
                /* next key already pressed: jump to the end */
                vtw_t = 1.0;
                levwin->force_redraw();
                ROOTWIN->draw_windows();
                vulture_refresh_window_region();
                break;
            }
            if (f == 1 && SDL_GetTicks() - tf > 25)
                frames = 4; /* slow frames: fewer of them */
            SDL_Delay(12);
        }
        vtw_t = 1.0;
        vtw_pending = false;
        vtw_animating = false;
        vtw_commit(vtw_last_vx, vtw_last_vy);
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                          "tween: %d frames in %u ms\n", frames,
                          (unsigned) (SDL_GetTicks() - t0));
    }
    if (vanim_ndeaths)
        vanim_play_deaths();
    if (vanim_ndrops)
        vanim_play_drops(); /* s83: after deaths, so corpses land last */
}

#include <math.h>

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
    map_centre_x += vtw_gx + (int) vpan_px; /* s79: + MMB drag residual */
    map_centre_y += vtw_gy + (int) vpan_py;

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

                /* 4. Objects (s83: falling while a drop plays) */
                vanim_draw_object(j, i, x, y, map_obj);

                /* 5. Monsters */
                if ((cur_tile = vulture_get_tile(map_mon)) != NULL) {
                    /* s75: slide between squares, face the way it went */
                    int tx = x + vtw_ox[i][j],
                        ty = y + vtw_oy[i][j] + vbob_offset(j, i); /* s82 */
                    double xs = 1.0, ys = 1.0, deg = 0.0;
                    bool red = false;

                    /* s79: squash & stretch + lean while stepping */
                    if (vtw_mv[i][j]) {
                        double te = vtw_te;

                        if (te < 0.8)
                            ys = 1.0 + 0.18 * sin(3.14159265358979 * te / 0.8);
                        else
                            ys = 1.0 - 0.26 * sin(3.14159265358979 * (te - 0.8) / 0.2);
                        xs = 1.0 / pow(ys, 0.7);
                        deg = vtw_face[i][j] * 10.0 * sin(3.14159265358979 * te);
                    } else {
                        vfx_breathe(j, i, &xs, &ys); /* s85 */
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
    vw_draw(map_centre_x, map_centre_y, view_x, view_y, this->abs_x,
            this->abs_y, this->w, this->h); /* s86: light, night, dust, panel */
    vfx_draw(map_centre_x, map_centre_y, view_x, view_y); /* s85 */
    vulture_draw_vignette(this->abs_x, this->abs_y, this->w, this->h); /* s74 */

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

        need_redraw = 1;
        return V_EVENT_HANDLED_REDRAW;
    }

    /* hovering over the map: display a tooltip */
    if (time < HOVERTIMEOUT)
        return V_EVENT_HANDLED_NOREDRAW;

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
            vpan_px -= V_MAP_XMOD * (ra - rb);
            vpan_py -= V_MAP_YMOD * (ra + rb);
            if (view_x == ovx && view_y == ovy) { /* hit the map edge */
                vpan_px = vpan_py = 0.0;
                break;
            }
        }
        /* panning is not movement: don't let the tween animate it */
        if (!vtw_pending && !vtw_animating)
            vtw_snap_vx = view_x, vtw_snap_vy = view_y;
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }

    mouse = vulture_get_mouse_pos();
    mappos = mouse_to_map(mouse);

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
    if (button == SDL_BUTTON_MIDDLE) {
        vpan_px = vpan_py = 0.0;
        force_redraw();
        return V_EVENT_HANDLED_REDRAW;
    }

    mouse = vulture_get_mouse_pos();
    mappos = mouse_to_map(mouse);

    return map_data->handle_click(result, button, mappos);
}

eventresult
levelwin::handle_keydown_event(window *target, void *result, int sym, int mod,
                               int unicode)
{
    int translated_key, key;
    int macronum, i;

    switch (sym) {
    case SDLK_TAB:
        map::toggle();
        return V_EVENT_HANDLED_REDRAW;

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

    /* all other keys are converted and passed to the core */
    key = vulture_make_nh_key(sym, mod, unicode);

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
    int px = mouse.x - (w / 2), py = mouse.y - (h / 2), vj, vi, dj, di;

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
