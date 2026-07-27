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


static struct {
	point cur;
	SDL_Surface *background, *tip;
  std::string text;
	int valid;
	SDL_Rect refresh;
} vulture_tooltip = {{0, 0}, NULL, NULL, "", 0, {0,0,0,0}};

static Uint32 tooltipcolor;


static void vulture_mouse_get_bg(void);
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
	tooltipcolor = SDL_MapRGBA(vulture_px_format, 0xff, 0xff, 0xf0, 0xd0);
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
}



void vulture_set_mcursor(int cursornum)
{
	struct vulture_cursor_geom prevcursor;

	if (cursornum < 0 || cursornum >= V_CURSOR_MAX)
		return;

	vulture_mouse.prevcursor = vulture_mouse.cursor;
	vulture_mouse.cursor = (CURTILEOFFSET + cursornum);

	prevcursor = vulture_cursor(vulture_mouse.prevcursor);

	/* refresh region of previous cursor */
	if (vulture_mouse.cursor != vulture_mouse.prevcursor)
		vulture_refresh_region(vulture_mouse.cur.x + prevcursor.xmod,
						vulture_mouse.cur.y + prevcursor.ymod,
						vulture_mouse.cur.x + prevcursor.xmod + prevcursor.w,
						vulture_mouse.cur.y + prevcursor.ymod + prevcursor.h);
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

	vulture_mouse_get_bg();

	cursor = vulture_cursor(vulture_mouse.cursor);

	new_x = vulture_mouse.cur.x + cursor.xmod;
	new_y = vulture_mouse.cur.y + cursor.ymod;

	/* draw the mouse - vulture_put_img of the (possibly scaled) cursor art
	 * rather than vulture_put_tile, which would always draw it native */
	vulture_put_img(new_x, new_y, cursor.graphic);


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
	int old_x, old_y, new_x, new_y;
	struct vulture_cursor_geom cursor, prevcursor;

	cursor = vulture_cursor(vulture_mouse.cursor);
	prevcursor = vulture_cursor(vulture_mouse.prevcursor);

	old_x = vulture_mouse.prev.x + prevcursor.xmod;
	old_y = vulture_mouse.prev.y + prevcursor.ymod;
	new_x = vulture_mouse.cur.x + cursor.xmod;
	new_y = vulture_mouse.cur.y + cursor.ymod;

	vulture_refresh_region(old_x-1, old_y-1, old_x + prevcursor.w,
							old_y + prevcursor.h);
	if (old_x != new_x || old_y != new_y)
		vulture_refresh_region(new_x-1, new_y-1, new_x + cursor.w,
								new_y + cursor.h);

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
	struct vulture_cursor_geom cursor;

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

	if (vulture_mouse.background) {
		cursor = vulture_cursor(vulture_mouse.cursor);

		vulture_put_img(vulture_mouse.cur.x + cursor.xmod,
					vulture_mouse.cur.y + cursor.ymod,
					vulture_mouse.background);
		SDL_FreeSurface(vulture_mouse.background);
		vulture_mouse.background = NULL;
	}
}



static void vulture_mouse_get_bg(void)
{
	struct vulture_cursor_geom cursor;

	if (vulture_mouse.background)
		SDL_FreeSurface(vulture_mouse.background);

	cursor = vulture_cursor(vulture_mouse.cursor);

	vulture_mouse.background =
		vulture_get_img(vulture_mouse.cur.x + cursor.xmod,
					vulture_mouse.cur.y + cursor.ymod,
					vulture_mouse.cur.x + cursor.xmod + cursor.w,
					vulture_mouse.cur.y + cursor.ymod + cursor.h);
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
		vulture_tooltip.cur.x = -1;
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
		length = vulture_text_length(V_FONT_TOOLTIP, str) + V_UI(6);
		height = vulture_text_height(V_FONT_TOOLTIP, str) + V_UI(6);

		vulture_tooltip.tip = SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_SRCALPHA,
				length, height, vulture_px_format->BitsPerPixel,
				vulture_px_format->Rmask,
				vulture_px_format->Gmask,
				vulture_px_format->Bmask,
				vulture_px_format->Amask);

		SDL_FillRect(vulture_tooltip.tip, NULL, tooltipcolor);
		vulture_rect_surface(vulture_tooltip.tip, 0, 0, length-1, height-1, V_COLOR_BACKGROUND);
		vulture_put_text(V_FONT_TOOLTIP, str, vulture_tooltip.tip,
						V_UI(3), V_UI(3), V_COLOR_BACKGROUND);
	}

	if (vulture_tooltip.tip)
		vulture_tooltip.valid = 1;
	else
		vulture_tooltip.valid = 0;

}


