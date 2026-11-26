/* NetHack may be freely redistributed.  See license for details. */

#include <stdlib.h>

#include "SDL.h"

#include "vulture_types.h"
#include "vulture_mou.h"
#include "vulture_gra.h"
#include "vulture_sdl.h"
#include "vulture_txt.h"
#include "vulture_tile.h"


static struct {
	point cur, prev;
	SDL_Surface *background;
	int cursor, prevcursor;
} vulture_mouse = {{0, 0}, {0, 0}, NULL, 0, 0};

/*
 * The pointer is an OS cursor (IMPLEMENTATION_LOG §53).
 *
 * Stock Vulture painted the pointer INTO vulture_screen every frame, pushed
 * that rect to the display, then pasted the saved pixels back underneath.
 * Every cursor ghost came from that dance: §35 fixed the erase rect, but a
 * pointer image still got baked into the message area at the top edge (user
 * report 2026-11-18, cause never pinned down in code).  Rather than patch a
 * third time, the pointer now never touches vulture_screen at all: each
 * cursor tile becomes an SDL colour cursor (same art, same hotspot - the
 * tile's xmod/ymod are offsets FROM the hotspot, so hotspot = -mod) and the
 * window system draws it.  No save/restore, nothing to ghost, no lag.
 *
 * The art is the NATIVE tile: the OS draws in window pixels, which is the
 * apparent size the old scaled software cursor was compensating back to.
 * vulture_cursor() below (the scaled geometry) is still used to place the
 * tooltip under the pointer, which IS still painted by Vulture.
 */
static SDL_Cursor *os_cursor[V_CURSOR_MAX] = { NULL };
static int os_cursor_shown = -1; /* tile id currently set, -1 = none yet */

static void vulture_os_cursor_apply(int tile_id)
{
	int idx = tile_id - CURTILEOFFSET;
	vulture_tile *tile;

	if (idx < 0 || idx >= V_CURSOR_MAX || tile_id == os_cursor_shown)
		return;

	if (!os_cursor[idx]) {
		int hx, hy;

		tile = vulture_get_tile(tile_id);
		if (!tile || !tile->graphic)
			return;
		hx = -tile->xmod;
		hy = -tile->ymod;
		hx = (hx < 0) ? 0 : (hx >= tile->graphic->w) ? tile->graphic->w - 1 : hx;
		hy = (hy < 0) ? 0 : (hy >= tile->graphic->h) ? tile->graphic->h - 1 : hy;
		/* SDL copies the pixels, so the tile cache can age out freely */
		os_cursor[idx] = SDL_CreateColorCursor(tile->graphic, hx, hy);
		if (!os_cursor[idx])
			return; /* keep whatever cursor is showing */
	}
	SDL_SetCursor(os_cursor[idx]);
	SDL_ShowCursor(SDL_ENABLE);
	os_cursor_shown = tile_id;
}


static struct {
	point cur;
	SDL_Surface *background, *tip;
  std::string text;
	int valid;
	SDL_Rect refresh;
} vulture_tooltip = {{0, 0}, NULL, NULL, "", 0, {0,0,0,0}};

static Uint32 tooltipcolor;


static void vulture_mouse_get_tt_bg(void);


/*--------------------------------------------------------------------------
Scaled mouse cursor
--------------------------------------------------------------------------*/
/*
 * The mouse cursors are game art (CURTILEOFFSET tiles) but they are UI, so
 * they are scale-compensated like the rest of the UI: at render_scale 2 an
 * uncompensated pointer reached the screen at half its usual apparent size.
 * This is the second deliberate crossing of the UI/map boundary described in
 * vulture_types.h; the first is vulture_put_tile_ui() for object icons inside
 * menus.  Scaling the art does NOT move where the cursor points: xmod/ymod are
 * offsets FROM the hotspot, so they scale with the art and the hotspot stays
 * on vulture_mouse.cur exactly as before.
 *
 * Every rect in this file - the background save, the background restore, both
 * refresh regions, and the tooltip anchor - is derived from these four
 * numbers.  They must therefore all come from this one accessor: a mismatch
 * between the rect the cursor is DRAWN into and the rect its background is
 * saved from is exactly what leaves cursor trails on screen.
 */
struct vulture_cursor_geom {
	SDL_Surface *graphic;
	int xmod, ymod, w, h;
};

static SDL_Surface *cursor_scaled[V_CURSOR_MAX] = { NULL };
static int cursor_scaled_at = 1;

static void vulture_cursor_flush_cache(void)
{
	int i;

	for (i = 0; i < V_CURSOR_MAX; i++) {
		if (cursor_scaled[i])
			SDL_FreeSurface(cursor_scaled[i]);
		cursor_scaled[i] = NULL;
	}
}

static struct vulture_cursor_geom vulture_cursor(int tile_id)
{
	struct vulture_cursor_geom g;
	vulture_tile *tile = vulture_get_tile(tile_id);
	int idx = tile_id - CURTILEOFFSET;

	g.graphic = tile->graphic;
	g.xmod = tile->xmod;
	g.ymod = tile->ymod;

	if (vulture_ui_scale > 1 && idx >= 0 && idx < V_CURSOR_MAX) {
		/* a runtime scale change (F8) invalidates every cached cursor */
		if (cursor_scaled_at != vulture_ui_scale) {
			vulture_cursor_flush_cache();
			cursor_scaled_at = vulture_ui_scale;
		}

		if (!cursor_scaled[idx])
			cursor_scaled[idx] =
				vulture_scale_surface_int(tile->graphic, vulture_ui_scale);

		/* we own this copy, so the tile cache ageing out underneath us is
		 * harmless; if the allocation failed we simply stay native */
		if (cursor_scaled[idx]) {
			g.graphic = cursor_scaled[idx];
			g.xmod = tile->xmod * vulture_ui_scale;
			g.ymod = tile->ymod * vulture_ui_scale;
		}
	}

	g.w = g.graphic->w;
	g.h = g.graphic->h;

	return g;
}



void vulture_mouse_init(void)
{
	vulture_mouse.prevcursor = CURTILEOFFSET;
	vulture_mouse.cursor = CURTILEOFFSET;

	/* slightly transparent off-white */
	tooltipcolor = SDL_MapRGBA(vulture_px_format, 14, 12, 15, 0xf0); /* s200: dark, as the HUD */
}



void vulture_mouse_destroy(void)
{
	if (vulture_mouse.background)
		SDL_FreeSurface(vulture_mouse.background);

	if (vulture_tooltip.tip)
		SDL_FreeSurface(vulture_tooltip.tip);

	if (vulture_tooltip.background)
		SDL_FreeSurface(vulture_tooltip.background);

	vulture_cursor_flush_cache();

	/* §53: hand the window back the system arrow, then free ours */
	SDL_SetCursor(SDL_GetDefaultCursor());
	for (int i = 0; i < V_CURSOR_MAX; i++) {
		if (os_cursor[i])
			SDL_FreeCursor(os_cursor[i]);
		os_cursor[i] = NULL;
	}
	os_cursor_shown = -1;
}



void vulture_set_mcursor(int cursornum)
{
	if (cursornum < 0 || cursornum >= V_CURSOR_MAX)
		return;

	vulture_mouse.prevcursor = vulture_mouse.cursor;
	vulture_mouse.cursor = (CURTILEOFFSET + cursornum);

	/* §53: the OS draws the pointer; just switch its shape */
	vulture_os_cursor_apply(vulture_mouse.cursor);
}



point vulture_get_mouse_pos(void)
{
	return vulture_mouse.cur;
}


point vulture_get_mouse_prev_pos(void)
{
	return vulture_mouse.prev;
}


void vulture_set_mouse_pos(int x, int y)
{
	vulture_mouse.prev = vulture_mouse.cur;
	vulture_mouse.cur.x = x;
	vulture_mouse.cur.y = y;
}


void vulture_mouse_draw(void)
{
	int new_x, new_y;
	struct vulture_cursor_geom cursor;

	/* §53: the pointer itself is an OS cursor - nothing is painted into
	 * vulture_screen for it.  Make sure the right shape is up (the first
	 * draw happens before any window has called set_mcursor). */
	vulture_os_cursor_apply(vulture_mouse.cursor);

	/* the tooltip is still painted, placed under the pointer's art */
	cursor = vulture_cursor(vulture_mouse.cursor);
	new_x = vulture_mouse.cur.x + cursor.xmod;
	new_y = vulture_mouse.cur.y + cursor.ymod;

	/* draw a new tooltip, if necessary */
	if (vulture_tooltip.valid && vulture_tooltip.tip) {
		if (vulture_tooltip.cur.x == -1) {
			if (new_x + vulture_tooltip.tip->w > vulture_screen->w)
				vulture_tooltip.cur.x = vulture_screen->w - vulture_tooltip.tip->w;
			else if (new_x + cursor.xmod < 0)
				vulture_tooltip.cur.x = 0;
			else
				vulture_tooltip.cur.x = new_x;

			if (new_y + cursor.h + vulture_tooltip.tip->h > vulture_screen->h)
				vulture_tooltip.cur.y = vulture_screen->h - vulture_tooltip.tip->h;
			else if (new_y + cursor.ymod < 0)
				vulture_tooltip.cur.y = 0;
			else
				vulture_tooltip.cur.y = new_y + cursor.h;
		}

		vulture_mouse_get_tt_bg();

		vulture_put_img(vulture_tooltip.cur.x,vulture_tooltip.cur.y,vulture_tooltip.tip);
		vulture_refresh_region(vulture_tooltip.cur.x, vulture_tooltip.cur.y,
						vulture_tooltip.cur.x + vulture_tooltip.tip->w,
						vulture_tooltip.cur.y + vulture_tooltip.tip->h);
	}
}



void vulture_mouse_refresh(void)
{
	/* §53: no pointer pixels to erase any more - only the tooltip's */

	/* refresh the position of the previous tooltip */
	if (vulture_tooltip.refresh.x != -1) {
		vulture_refresh_region(vulture_tooltip.refresh.x, vulture_tooltip.refresh.y,
						vulture_tooltip.refresh.x + vulture_tooltip.refresh.w,
						vulture_tooltip.refresh.y + vulture_tooltip.refresh.h);
		vulture_tooltip.refresh.x = -1;
	}
}



void vulture_mouse_restore_bg()
{
	if (vulture_tooltip.background) {
		vulture_put_img(vulture_tooltip.cur.x, vulture_tooltip.cur.y, vulture_tooltip.background);
		SDL_FreeSurface(vulture_tooltip.background);
		vulture_tooltip.background = NULL;

		/* remember where to refresh later */
		vulture_tooltip.refresh.x = vulture_tooltip.cur.x;
		vulture_tooltip.refresh.y = vulture_tooltip.cur.y;
		vulture_tooltip.refresh.w = vulture_tooltip.tip->w;
		vulture_tooltip.refresh.h = vulture_tooltip.tip->h;
	}

	/* §53: the pointer never saves a background any more (OS cursor) */
}


static void vulture_mouse_get_tt_bg(void)
{
	if (vulture_tooltip.tip && vulture_tooltip.valid) {
		if (vulture_tooltip.background)
			SDL_FreeSurface(vulture_tooltip.background);

		vulture_tooltip.background = vulture_get_img(vulture_tooltip.cur.x, vulture_tooltip.cur.y,
												vulture_tooltip.cur.x + vulture_tooltip.tip->w,
												vulture_tooltip.cur.y + vulture_tooltip.tip->h);
	}
}



void vulture_mouse_invalidate_tooltip(int force)
{
	struct vulture_cursor_geom cursor = vulture_cursor(vulture_mouse.cursor);

	/* invalidate only for "significant" movement. (significant == more than 4 px ...) */
	if (force ||
		abs(vulture_mouse.cur.x - (vulture_tooltip.cur.x -
									cursor.xmod)) > V_UI(4) ||
		abs(vulture_mouse.cur.y - (vulture_tooltip.cur.y -
									cursor.ymod - cursor.h)) > V_UI(4)) {
		vulture_tooltip.valid = 0;
		vulture_tooltip.cur.x = -1;
		vulture_tooltip.cur.y = -1;
	}
}



void vulture_mouse_set_tooltip(std::string str)
{
	int length, height;

	if (!str.empty() && str != vulture_tooltip.text) {
		vulture_tooltip.text = str;

		if (vulture_tooltip.background)
			SDL_FreeSurface(vulture_tooltip.background);
		vulture_tooltip.background = NULL;

		if (vulture_tooltip.tip)
			SDL_FreeSurface(vulture_tooltip.tip);
		/* the text metrics already scale (V_FONT_TOOLTIP is opened at
		 * vulture_ui_scale); the border around them has to scale with it */
		/* s139: several lines when the text has newlines */
		{
			size_t a = 0, b2;
			int lw = 0, nl = 0;

			while (a <= str.size()) {
				b2 = str.find('\n', a);
				if (b2 == std::string::npos)
					b2 = str.size();
				if (vulture_text_length(V_FONT_TOOLTIP, str.substr(a, b2 - a)) > lw)
					lw = vulture_text_length(V_FONT_TOOLTIP, str.substr(a, b2 - a));
				nl++;
				a = b2 + 1;
			}
			length = lw + V_UI(14); /* s200: room to breathe */
			height = nl * vulture_get_lineheight(V_FONT_TOOLTIP) + V_UI(10);
		}

		vulture_tooltip.tip = SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_SRCALPHA,
				length, height, vulture_px_format->BitsPerPixel,
				vulture_px_format->Rmask,
				vulture_px_format->Gmask,
				vulture_px_format->Bmask,
				vulture_px_format->Amask);

		SDL_FillRect(vulture_tooltip.tip, NULL, tooltipcolor);
		vulture_rect_surface(vulture_tooltip.tip, 0, 0, length-1, height-1,
		                     SDL_MapRGBA(vulture_px_format, 150, 122, 64, 0xff)); /* s200: a dim gold edge */
		{ /* s139: one line at a time */
			size_t a = 0, b2;
			int row = 0;

			while (a <= str.size()) {
				b2 = str.find('\n', a);
				if (b2 == std::string::npos)
					b2 = str.size();
				vulture_put_text(V_FONT_TOOLTIP, str.substr(a, b2 - a), vulture_tooltip.tip,
								V_UI(7), V_UI(5) + row * vulture_get_lineheight(V_FONT_TOOLTIP),
								SDL_MapRGBA(vulture_px_format, 240, 235, 225, 0xff)); /* s200 */
				row++;
				a = b2 + 1;
			}
		}
	}

	if (vulture_tooltip.tip)
		vulture_tooltip.valid = 1;
	else
		vulture_tooltip.valid = 0;

}


