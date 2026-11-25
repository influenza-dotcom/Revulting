/* NetHack may be freely redistributed.  See license for details. */

/*-------------------------------------------------------------------
vulture_sdl.c : SDL API calls for Vulture's windowing system.
Uses SDL 2.
-------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#ifdef __GNUC__
#include <unistd.h>
#endif
#include <stdarg.h>
#include <unordered_map> /* s183: before NetHack's min/max macros */
#include <vector>
#include "vulture_sound.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "SDL.h"
#include "SDL_mixer.h"
#include "vulture_gen.h"
#include "vulture_gfl.h"
#include "vulture_win.h"
#include "vulture_sdl.h"
#include "vulture_init.h"
#include "vulture_main.h"
#include "vulture_mou.h"
#include "vulture_opt.h"
#include "vulture_tile.h"

#include "winclass/levelwin.h"

#include "date.h"

int vulture_has_focus = 1; /* s140: the game window has input focus */


/* Definitions */


static int fs_message_printed = 0;
static int have_mouse_focus = 1;

/* SDL2 window */
SDL_Window *vulture_sdl_window = NULL;

/* Graphics objects */
SDL_Surface *vulture_screen;             /* Graphics surface */

/* The real window surface.  Everything in the port draws into
 * vulture_screen and lays itself out against vulture_screen->w/h; only the
 * two present functions at the bottom of this file and the raw SDL mouse
 * coordinates are in WINDOW space.  At render_scale 1 the two are the same
 * surface, so the default path is exactly what it always was: no extra
 * surface, no extra copy. */
SDL_Surface *vulture_window_surface = NULL;

/* Active supersampling factor: vulture_screen is this many times larger
 * than the window in each axis.  Distinct from vulture_opts.render_scale so
 * that a failed allocation degrades to 1x instead of crashing. */
int vulture_render_scale = 1;

/* the offscreen surface behind vulture_screen while scaling is active */
static SDL_Surface *vulture_render_surface = NULL;

/* The factor the UI is currently AUTHORED at - see vulture_types.h.  It is
 * deliberately NOT updated by vulture_reattach_render_surface(): the fonts and
 * the chrome art are built at a particular scale and the layout constants must
 * keep agreeing with them until vulture_ui_rescale() has actually rebuilt
 * both.  1 until vulture_init_graphics() raises it. */
int vulture_ui_scale = 1;


Uint32 vulture_timer_callback(Uint32 interval, void * param);
int vulture_handle_global_event(SDL_Event * event);
static void vulture_sdl_error(const char *file, int line, const char *what);


/*
 * Keys that must NOT wake the event pump on their own: pressing Shift by
 * itself is not a command.
 *
 * This was a range test - "sym >= SDLK_NUMLOCK && sym <= SDLK_COMPOSE" - which
 * was correct under SDL 1.2, where those two keysyms bracketed exactly the 15
 * contiguous modifier values.  SDL2 derives keycodes from scancodes instead
 * (keycode = scancode | 0x40000000), and the SDL2 shim in vulture_sdl.h aliases
 * only the two endpoint NAMES, so the test still compiled while silently
 * changing meaning: SDL_SCANCODE_NUMLOCKCLEAR(83)..SDL_SCANCODE_APPLICATION(101)
 * spans the ENTIRE NUMERIC KEYPAD, and the real SDL2 modifiers (LCTRL..RGUI are
 * scancodes 224-231) fall outside it.  The predicate was inverted: every keypad
 * press was swallowed by vulture_wait_event() and never reached NetHack, while
 * bare modifier presses were delivered as if they were commands.
 *
 * Enumerate them explicitly so this cannot drift again.
 */
bool is_modifier_key(int sym)
{
	switch (sym) {
	case SDLK_LSHIFT:  case SDLK_RSHIFT:
	case SDLK_LCTRL:   case SDLK_RCTRL:
	case SDLK_LALT:    case SDLK_RALT:
	case SDLK_LGUI:    case SDLK_RGUI:
	case SDLK_MODE:
	case SDLK_NUMLOCKCLEAR:
	case SDLK_CAPSLOCK:
	case SDLK_SCROLLLOCK:
		return true;
	default:
		return false;
	}
}


void vulture_wait_event(SDL_Event * event, int wait_timeout)
{
	int done = 0;
	SDL_TimerID sleeptimer = NULL;

	if (wait_timeout > 0)
		sleeptimer = SDL_AddTimer(wait_timeout, vulture_timer_callback, NULL);


	while (!done)
	{
		event->type = 0;

#ifdef __EMSCRIPTEN__
		/* Poll events, skipping only internal sentinel events */
		while (SDL_PollEvent(event)) {
			if (event->type == SDL_POLLSENTINEL)
				continue; /* skip SDL_POLLSENTINEL */
			break; /* got a real event */
		}
#else
		/* Drain stale MOUSEMOTION events, keeping the last polled event */
		{
			SDL_Event tmp;
			while (SDL_PollEvent(&tmp)) {
				*event = tmp;
				if (tmp.type != SDL_MOUSEMOTION)
					break;
			}
		}
#endif

		if (!event->type) {
#ifdef __EMSCRIPTEN__
			emscripten_sleep(16); /* Yield to browser event loop (~60fps) */
			continue;
#else
			if (!SDL_WaitEvent(event))
				continue;
#endif
		}

		vulture_handle_global_event(event);

		if ((event->type == SDL_KEYDOWN && !is_modifier_key(event->key.keysym.sym)) ||
			event->type == SDL_MOUSEBUTTONDOWN ||
			event->type == SDL_MOUSEBUTTONUP ||
			event->type == SDL_MOUSEWHEEL ||
			/* send timer events only while the mouse is in the window */
			(event->type == SDL_TIMEREVENT && have_mouse_focus) ||
			event->type == SDL_MOUSEMOTION)
			done = 1;
#ifdef __EMSCRIPTEN__
		else
			emscripten_sleep(1); /* Yield to browser event loop */
#endif
	}

	SDL_RemoveTimer(sleeptimer);
}


int vulture_poll_event(SDL_Event * event)
{
	int done = 0;

	while (!done)
	{
		event->type = 0;

#ifdef __EMSCRIPTEN__
		while (SDL_PollEvent(event)) {
			if (event->type == SDL_POLLSENTINEL)
				continue; /* skip SDL_POLLSENTINEL */
			if (event->type == SDL_MOUSEMOTION) {
				vulture_handle_global_event(event);
				continue;
			}
			break;
		}
#else
		/* Drain stale MOUSEMOTION events, keeping the last polled event */
		{
			SDL_Event tmp;
			while (SDL_PollEvent(&tmp)) {
				*event = tmp;
				if (tmp.type != SDL_MOUSEMOTION)
					break;
			}
		}
#endif

		if (!event->type)
			/* no events queued */
			return 0;

		vulture_handle_global_event(event);

		if (event->type == SDL_KEYDOWN ||
			event->type == SDL_MOUSEBUTTONDOWN ||
			event->type == SDL_MOUSEBUTTONUP ||
			event->type == SDL_MOUSEMOTION)
			/* an interesting event, leave the loop */
			done = 1;

		/* else: an uninteresting event, like those handled in vulture_handle_global_event */
	}
	return 1;
}


/* wait for an input event, but stop waiting after wait_timeout milliseconds */
void vulture_wait_input(SDL_Event * event, int wait_timeout)
{
	int done = 0;
	SDL_TimerID sleeptimer = NULL;

	if (wait_timeout > 0)
		sleeptimer = SDL_AddTimer(wait_timeout, vulture_timer_callback, NULL);

	/* loop until we have an interesting event */
	while (!done)
	{
#ifdef __EMSCRIPTEN__
		if (!SDL_PollEvent(event)) {
			emscripten_sleep(1); /* Yield to browser event loop */
			continue;
		}
		/* Filter out SDL_POLLSENTINEL internal events */
		if (event->type == SDL_POLLSENTINEL) {
			emscripten_sleep(1);
			continue;
		}
#else
		if (!SDL_WaitEvent(event))
			continue;
#endif

		vulture_handle_global_event(event);

		if (event->type == SDL_KEYDOWN ||
			event->type == SDL_MOUSEBUTTONUP ||
			event->type == SDL_TIMEREVENT)
			done = 1;
#ifdef __EMSCRIPTEN__
		else
			emscripten_sleep(1); /* Yield to browser event loop */
#endif
	}

	SDL_RemoveTimer(sleeptimer);
}




void vulture_wait_key(SDL_Event * event)
{
	int done = 0;

	while (!done)
	{
#ifdef __EMSCRIPTEN__
		if (!SDL_PollEvent(event)) {
			emscripten_sleep(1); /* Yield to browser event loop */
			continue;
		}
		/* Filter out SDL_POLLSENTINEL internal events */
		if (event->type == SDL_POLLSENTINEL) {
			emscripten_sleep(1);
			continue;
		}
#else
		if (!SDL_WaitEvent(event))
			continue;
#endif

		vulture_handle_global_event(event);

		if (event->type == SDL_KEYDOWN)
			done = 1;
#ifdef __EMSCRIPTEN__
		else
			emscripten_sleep(1); /* Yield to browser event loop */
#endif
	}
}


Uint32 vulture_timer_callback(Uint32 interval, void * param)
{
	SDL_Event event;
	event.type = SDL_TIMEREVENT;
	event.user.data1 = param;

	SDL_PushEvent(&event);

	return interval;
}



/*
 * (Re)point vulture_screen at the surface the game should draw into.
 *
 * MUST be called after anything that can invalidate the window surface
 * (window creation, SDL_SetWindowSize, SDL_SetWindowFullscreen, a resize
 * event) - SDL2 frees and recreates the window surface behind our back, so a
 * cached pointer is a use-after-free.
 *
 * At render_scale 1 vulture_screen IS the window surface, exactly as before
 * this option existed.  Above 1 it is an offscreen surface render_scale times
 * larger in each axis, in the window surface's own pixel format (so
 * vulture_px_format, derived from vulture_screen->format in
 * vulture_init_colors(), is unchanged).
 */
/*
 * IMPLEMENTATION_LOG s168: frames reach the screen through the graphics card.
 * With gpu=1 the window gets an accelerated SDL_Renderer; the game still draws
 * into an off-screen software surface (vulture_screen, render_scale times the
 * window), and each refresh uploads the changed rectangle to a streaming
 * texture and the GPU draws it - scaled with linear filtering when
 * render_scale > 1 (that replaces the CPU box filter).  Without an
 * accelerated renderer it quietly falls back to the window surface.  A
 * window recreated by a fullscreen switch takes its renderer with it, so a
 * new one is made for the new window.  (VULTURE_GPU_SOFT=1 forces the
 * software renderer - for exercising this path in the off-screen probes.)
 */
SDL_Renderer *vulture_gpu = NULL;
static SDL_Texture *vulture_gpu_tex = NULL;
static SDL_Window *vulture_gpu_win = NULL;
static int vulture_gpu_failed = 0;

/*
 * IMPLEMENTATION_LOG s183: the map layer drawn by the GPU.
 *
 * With a renderer (gpu=1) and dynamic lighting off, levelwin draws the
 * map's tiles and sprites with SDL_Render* into vgm_tex - a render target
 * the size of the frame - instead of blitting them into vulture_screen
 * (vulture_put_img and the sprite transformer branch on vulture_gpumap_on).
 * Everything drawn after them - effects, the HUD, every other window -
 * still goes into vulture_screen, which is ARGB now and starts each map
 * frame transparent over the map: an overlay.  SDL's software blending into
 * a destination with alpha yields premultiplied colour (dst = src*a +
 * dst*(1-a), A = a + A*(1-a)), additive blits leave A alone, so the overlay
 * goes over the map with the premultiplied "over" blend (ONE,
 * ONE_MINUS_SRC_ALPHA).  A renderer without custom blend modes (SDL's
 * software one, which the off-screen probes use) gets the same result in two
 * passes: the overlay as black with its alpha, then its colour added.  The
 * two are composed into vgm_out (render space - the zoom rectangles are
 * render space too), which is then shown.  Lighting on (its passes read and
 * write the map's pixels) draws everything the old way; the overlay is then
 * the whole frame and is copied opaque.
 */
bool vulture_gpumap_on = false;      /* drawing the map layer right now */
bool vulture_gpumap_wanted = true;   /* the Visual effects switch */
bool vulture_gpumap_lost = false;    /* lost textures: levelwin redraws all */
static SDL_Texture *vgm_tex = NULL;  /* the map layer */
static SDL_Texture *vgm_out = NULL;  /* map + overlay */
static SDL_Texture *vgm_big = NULL;  /* zoom scratch: nearest up to k x, then linear */
static SDL_Texture *vgm_rgb = NULL;  /* the overlay without alpha (two-pass composite) */
static bool vgm_rgb_fresh = false;   /* vgm_rgb was just made: upload all of it */
static bool vgm_valid = false;       /* vgm_tex holds the map under this overlay */
static int vgm_blend = -1;           /* premultiplied blend: -1 untested, 0 no, 1 yes */
static SDL_BlendMode vgm_premul = SDL_BLENDMODE_BLEND;
static bool vgm_zoom = false, vgm_zexact = true;
static SDL_Rect vgm_zsrc, vgm_zdst;
static int vgm_zk = 1;
static int vgm_disabled = 0;         /* this renderer can't (no render targets) */

struct vgm_ent {
	SDL_Texture *t;
	int w, h;
	void *px;
};
static std::unordered_map<SDL_Surface *, vgm_ent> vgm_cache;

/* forget every GPU-map texture - destroying them while the renderer lives,
   or only dropping them once SDL_DestroyWindow / a device reset took them */
static void vgm_drop(bool destroy)
{
	std::unordered_map<SDL_Surface *, vgm_ent>::iterator it;

	if (destroy) {
		for (it = vgm_cache.begin(); it != vgm_cache.end(); ++it)
			SDL_DestroyTexture(it->second.t);
		if (vgm_tex)
			SDL_DestroyTexture(vgm_tex);
		if (vgm_out)
			SDL_DestroyTexture(vgm_out);
		if (vgm_big)
			SDL_DestroyTexture(vgm_big);
		if (vgm_rgb)
			SDL_DestroyTexture(vgm_rgb);
	}
	vgm_cache.clear();
	vgm_tex = vgm_out = vgm_big = vgm_rgb = NULL;
	vgm_valid = false;
	vgm_blend = -1;
}

/* transparent pixels take a neighbour's colour (alpha stays 0), so a sprite
   drawn scaled with linear filtering doesn't grow a dark fringe */
static void vgm_dilate(SDL_Surface *c)
{
	static const int ddx[8] = { -1, 1, 0, 0, -1, 1, -1, 1 };
	static const int ddy[8] = { 0, 0, -1, 1, -1, -1, 1, 1 };
	int w = c->w, h = c->h, x, y, k;
	std::vector<Uint32> src((size_t) w * h);

	for (y = 0; y < h; y++)
		memcpy(&src[(size_t) y * w], (Uint8 *) c->pixels + (size_t) y * c->pitch, (size_t) w * 4);
	for (y = 0; y < h; y++) {
		Uint32 *row = (Uint32 *) ((Uint8 *) c->pixels + (size_t) y * c->pitch);

		for (x = 0; x < w; x++) {
			if (src[(size_t) y * w + x] >> 24)
				continue;
			for (k = 0; k < 8; k++) {
				int xx = x + ddx[k], yy = y + ddy[k];
				Uint32 p;

				if (xx < 0 || yy < 0 || xx >= w || yy >= h)
					continue;
				p = src[(size_t) yy * w + xx];
				if (p >> 24) {
					row[x] = p & 0x00FFFFFFu;
					break;
				}
			}
		}
	}
}

/* the texture for a surface (made once, kept until the surface is forgotten) */
static SDL_Texture *vgm_texture(SDL_Surface *s)
{
	std::unordered_map<SDL_Surface *, vgm_ent>::iterator it = vgm_cache.find(s);
	SDL_Surface *c;
	SDL_Texture *t;

	if (it != vgm_cache.end()) {
		if (it->second.w == s->w && it->second.h == s->h && it->second.px == s->pixels)
			return it->second.t;
		SDL_DestroyTexture(it->second.t); /* the address now belongs to another surface */
		vgm_cache.erase(it);
	}
	if (vgm_cache.size() > 6000) { /* a safety net for surfaces nobody forgot */
		for (it = vgm_cache.begin(); it != vgm_cache.end(); ++it)
			SDL_DestroyTexture(it->second.t);
		vgm_cache.clear();
	}
	c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
	if (!c)
		return NULL;
	if (SDL_LockSurface(c) == 0) {
		vgm_dilate(c);
		SDL_UnlockSurface(c);
	}
	t = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, c->w, c->h);
	if (t)
		SDL_UpdateTexture(t, NULL, c->pixels, c->pitch);
	SDL_FreeSurface(c);
	if (t) {
		vgm_ent e;

		e.t = t, e.w = s->w, e.h = s->h, e.px = s->pixels;
		vgm_cache[s] = e;
	}
	return t;
}

static bool vgpu_reattach(int want)
{
	int ow = 0, oh = 0, rw, rh;

	if (vulture_gpu && vulture_gpu_win != vulture_sdl_window) {
		/* SDL_DestroyWindow took the old renderer and its texture with it */
		vulture_gpu = NULL;
		vulture_gpu_tex = NULL;
		vgm_drop(false); /* s183: and the map layer's */
	}
	if (!vulture_gpu) {
		Uint32 fl = getenv("VULTURE_GPU_SOFT") ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;

		vulture_gpu = SDL_CreateRenderer(vulture_sdl_window, -1, fl);
		if (!vulture_gpu) {
			vulture_gpu_failed = 1;
			vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
				"gpu: no renderer (%s) - using the window surface\n", SDL_GetError());
			return false;
		}
		vulture_gpu_win = vulture_sdl_window;
		{
			SDL_RendererInfo ri;

			if (SDL_GetRendererInfo(vulture_gpu, &ri) == 0)
				vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "gpu: renderer %s\n", ri.name);
		}
	}
	if (SDL_GetRendererOutputSize(vulture_gpu, &ow, &oh) != 0 || ow <= 0 || oh <= 0)
		SDL_GetWindowSize(vulture_sdl_window, &ow, &oh);
	rw = ow * want, rh = oh * want;
	if (vulture_render_surface &&
		(vulture_render_surface->w != rw || vulture_render_surface->h != rh ||
		 vulture_render_surface->format->format != SDL_PIXELFORMAT_ARGB8888)) { /* s183: ARGB */
		SDL_FreeSurface(vulture_render_surface);
		vulture_render_surface = NULL;
	}
	if (!vulture_render_surface)
		vulture_render_surface = SDL_CreateRGBSurfaceWithFormat(0, rw, rh, 32, SDL_PIXELFORMAT_ARGB8888);
	if (vulture_gpu_tex) {
		int tw = 0, th = 0;

		SDL_QueryTexture(vulture_gpu_tex, NULL, NULL, &tw, &th);
		if (tw != rw || th != rh) {
			SDL_DestroyTexture(vulture_gpu_tex);
			vulture_gpu_tex = NULL;
		}
	}
	if (!vulture_gpu_tex)
		vulture_gpu_tex = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888, /* s183 */
			SDL_TEXTUREACCESS_STREAMING, rw, rh);
	if (!vulture_render_surface || !vulture_gpu_tex) {
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpu: could not make the frame (%s) - using the window surface\n", SDL_GetError());
		if (vulture_gpu_tex)
			SDL_DestroyTexture(vulture_gpu_tex);
		vgm_drop(true); /* s183 */
		SDL_DestroyRenderer(vulture_gpu);
		vulture_gpu = NULL, vulture_gpu_tex = NULL, vulture_gpu_failed = 1;
		return false;
	}
	SDL_SetTextureScaleMode(vulture_gpu_tex, want > 1 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
	SDL_SetSurfaceBlendMode(vulture_render_surface, SDL_BLENDMODE_NONE);
	SDL_SetClipRect(vulture_render_surface, NULL);
	vulture_window_surface = NULL; /* never touch the window surface with a renderer */
	vulture_render_scale = want;
	vulture_screen = vulture_render_surface;
	return true;
}

/* upload a rectangle of the frame (render space; NULL = all, empty = none) */
static void vgm_upload(SDL_Texture *t, SDL_Surface *s, const SDL_Rect *r)
{
	if (!t || !s)
		return;
	if (r && r->w > 0 && r->h > 0) {
		SDL_Rect c = *r;

		if (c.x < 0) c.w += c.x, c.x = 0;
		if (c.y < 0) c.h += c.y, c.y = 0;
		if (c.x + c.w > s->w) c.w = s->w - c.x;
		if (c.y + c.h > s->h) c.h = s->h - c.y;
		if (c.w > 0 && c.h > 0)
			SDL_UpdateTexture(t, &c,
				(Uint8 *) s->pixels + (size_t) c.y * s->pitch + (size_t) c.x * 4, s->pitch);
	} else if (!r) {
		SDL_UpdateTexture(t, NULL, s->pixels, s->pitch);
	}
}

/* s183: is there a GPU map under the overlay right now? */
static bool vgm_live(void)
{
	return vulture_gpu && vulture_gpu_tex && vgm_valid && vgm_tex && vgm_out;
}

/* s183: the overlay (and, for the two-pass composite, its no-alpha twin) */
static void vgm_upload_overlay(const SDL_Rect *r)
{
	SDL_Surface *s = vulture_screen;

	vgm_upload(vulture_gpu_tex, s, r);
	if (!vgm_live() || vgm_blend == 1)
		return;
	if (vgm_rgb) {
		int tw = 0, th = 0;

		SDL_QueryTexture(vgm_rgb, NULL, NULL, &tw, &th);
		if (tw != s->w || th != s->h) {
			SDL_DestroyTexture(vgm_rgb);
			vgm_rgb = NULL;
		}
	}
	if (!vgm_rgb) {
		vgm_rgb = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_RGB888,
			SDL_TEXTUREACCESS_STREAMING, s->w, s->h);
		vgm_rgb_fresh = true;
	}
	if (vgm_rgb_fresh) {
		vgm_upload(vgm_rgb, s, NULL);
		vgm_rgb_fresh = false;
	} else
		vgm_upload(vgm_rgb, s, r);
}

/* s183: the map layer - zoomed the way vzoom_apply zoomed the overlay - with
   the overlay over it, into vgm_out */
static void vgm_compose(void)
{
	SDL_SetRenderTarget(vulture_gpu, vgm_out);
	SDL_RenderSetClipRect(vulture_gpu, NULL);
	SDL_SetRenderDrawBlendMode(vulture_gpu, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(vulture_gpu, 0, 0, 0, 255);
	SDL_RenderClear(vulture_gpu);
	SDL_SetTextureBlendMode(vgm_tex, SDL_BLENDMODE_NONE);
	SDL_SetTextureColorMod(vgm_tex, 255, 255, 255);
	SDL_SetTextureAlphaMod(vgm_tex, 255);
	SDL_SetTextureScaleMode(vgm_tex, SDL_ScaleModeNearest);
	if (!vgm_zoom) {
		SDL_RenderCopy(vulture_gpu, vgm_tex, NULL, NULL);
	} else if (vgm_zexact || vgm_zk <= 1) {
		SDL_RenderCopy(vulture_gpu, vgm_tex, &vgm_zsrc, &vgm_zdst);
	} else {
		/* 1.5x (s122's "sharp bilinear"): nearest up to k x, then a small
		   smooth step down */
		SDL_Rect big;
		int bw = 0, bh = 0;

		big.x = 0, big.y = 0, big.w = vgm_zsrc.w * vgm_zk, big.h = vgm_zsrc.h * vgm_zk;
		if (vgm_big)
			SDL_QueryTexture(vgm_big, NULL, NULL, &bw, &bh);
		if (!vgm_big || bw < big.w || bh < big.h) {
			if (vgm_big)
				SDL_DestroyTexture(vgm_big);
			vgm_big = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET, big.w, big.h);
		}
		if (vgm_big) {
			SDL_SetRenderTarget(vulture_gpu, vgm_big);
			SDL_RenderCopy(vulture_gpu, vgm_tex, &vgm_zsrc, &big);
			SDL_SetRenderTarget(vulture_gpu, vgm_out);
			SDL_SetTextureBlendMode(vgm_big, SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(vgm_big, SDL_ScaleModeLinear);
			SDL_RenderCopy(vulture_gpu, vgm_big, &big, &vgm_zdst);
		} else
			SDL_RenderCopy(vulture_gpu, vgm_tex, &vgm_zsrc, &vgm_zdst);
	}
	SDL_SetTextureScaleMode(vulture_gpu_tex, SDL_ScaleModeNearest);
	if (vgm_blend == 1) {
		SDL_SetTextureBlendMode(vulture_gpu_tex, vgm_premul);
		SDL_RenderCopy(vulture_gpu, vulture_gpu_tex, NULL, NULL);
	} else {
		/* two passes: darken the map by (1 - A), then add the colour */
		SDL_SetTextureBlendMode(vulture_gpu_tex, SDL_BLENDMODE_BLEND);
		SDL_SetTextureColorMod(vulture_gpu_tex, 0, 0, 0);
		SDL_RenderCopy(vulture_gpu, vulture_gpu_tex, NULL, NULL);
		SDL_SetTextureColorMod(vulture_gpu_tex, 255, 255, 255);
		if (vgm_rgb) {
			SDL_SetTextureBlendMode(vgm_rgb, SDL_BLENDMODE_ADD);
			SDL_SetTextureScaleMode(vgm_rgb, SDL_ScaleModeNearest);
			SDL_RenderCopy(vulture_gpu, vgm_rgb, NULL, NULL);
		}
	}
	SDL_SetRenderTarget(vulture_gpu, NULL);
	SDL_SetTextureScaleMode(vulture_gpu_tex,
		vulture_render_scale > 1 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

/* upload the changed rectangle (render space; NULL = all) and show the frame */
static void vgpu_present(const SDL_Rect *r)
{
	SDL_Surface *s = vulture_screen;

	Uint64 pc0 = SDL_GetPerformanceCounter(); /* probe: s183 cost of a present */

	if (!vulture_gpu || !vulture_gpu_tex || !s)
		return;
	vgm_upload_overlay(r);
	if (vgm_live()) { /* s183 */
		vgm_compose();
		SDL_SetTextureBlendMode(vgm_out, SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(vgm_out,
			vulture_render_scale > 1 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
		SDL_RenderCopy(vulture_gpu, vgm_out, NULL, NULL);
	} else {
		SDL_SetTextureBlendMode(vulture_gpu_tex, SDL_BLENDMODE_NONE);
		SDL_SetTextureColorMod(vulture_gpu_tex, 255, 255, 255);
		SDL_RenderCopy(vulture_gpu, vulture_gpu_tex, NULL, NULL);
	}
	SDL_RenderPresent(vulture_gpu);
	if (getenv("VULTURE_SELFTEST"))
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "present %u us (%s, %dx%d)\n",
			(unsigned) ((SDL_GetPerformanceCounter() - pc0) * 1000000 / SDL_GetPerformanceFrequency()),
			vgm_live() ? "gpumap" : "software", r ? r->w : s->w, r ? r->h : s->h);
}

/* s183: make (or resize) the map layer and the compose target; test the
   premultiplied blend once */
static bool vgm_targets(void)
{
	int w, h, tw = 0, th = 0;

	if (!vulture_gpu || !vulture_gpu_tex || !vulture_screen || vgm_disabled)
		return false;
	w = vulture_screen->w, h = vulture_screen->h;
	if (vgm_tex) {
		SDL_QueryTexture(vgm_tex, NULL, NULL, &tw, &th);
		if (tw != w || th != h) {
			SDL_DestroyTexture(vgm_tex);
			vgm_tex = NULL;
			vgm_valid = false;
		}
	}
	if (vgm_out) {
		SDL_QueryTexture(vgm_out, NULL, NULL, &tw, &th);
		if (tw != w || th != h) {
			SDL_DestroyTexture(vgm_out);
			vgm_out = NULL;
		}
	}
	if (!vgm_tex)
		vgm_tex = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_TARGET, w, h);
	if (!vgm_out)
		vgm_out = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_TARGET, w, h);
	if (!vgm_tex || !vgm_out) {
		vgm_disabled = 1;
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpumap: no render targets (%s) - the map stays in software\n", SDL_GetError());
		if (vgm_tex)
			SDL_DestroyTexture(vgm_tex);
		if (vgm_out)
			SDL_DestroyTexture(vgm_out);
		vgm_tex = vgm_out = NULL;
		vgm_valid = false;
		return false;
	}
	if (vgm_blend < 0) {
		vgm_premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,
			SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
			SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
		vgm_blend = SDL_SetTextureBlendMode(vulture_gpu_tex, vgm_premul) == 0 ? 1 : 0;
		SDL_SetTextureBlendMode(vulture_gpu_tex, SDL_BLENDMODE_NONE);
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpumap: the map is drawn on the GPU (%s composite)\n",
			vgm_blend ? "premultiplied" : "two-pass");
	}
	return true;
}

/* s183: start drawing the map layer (clip: the region being redrawn) */
bool vulture_gpumap_begin(const SDL_Rect *clip)
{
	SDL_Rect all;

	if (vulture_gpumap_on)
		return true;
	if (!vulture_gpumap_wanted || getenv("VULTURE_NO_GPUMAP") || !vgm_targets())
		return false;
	if (SDL_SetRenderTarget(vulture_gpu, vgm_tex) != 0) {
		vgm_disabled = 1;
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpumap: can't draw into the map layer (%s) - software\n", SDL_GetError());
		return false;
	}
	all.x = 0, all.y = 0, all.w = vulture_screen->w, all.h = vulture_screen->h;
	SDL_RenderSetClipRect(vulture_gpu, clip ? clip : &all);
	SDL_SetRenderDrawBlendMode(vulture_gpu, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(vulture_gpu, 0, 0, 0, 255);
	SDL_RenderFillRect(vulture_gpu, clip ? clip : &all);
	vulture_gpumap_on = true;
	return true;
}

/* s183: the map layer is done for this frame */
void vulture_gpumap_end(void)
{
	if (!vulture_gpumap_on)
		return;
	SDL_RenderSetClipRect(vulture_gpu, NULL);
	SDL_SetRenderTarget(vulture_gpu, NULL);
	vulture_gpumap_on = false;
	vgm_valid = true;
}

/* s183: this frame's map went into vulture_screen (lighting on) */
void vulture_gpumap_abandon(void)
{
	if (vulture_gpumap_on)
		vulture_gpumap_end();
	vgm_valid = false;
	vgm_zoom = false;
}

bool vulture_gpumap_valid(void)
{
	return vgm_live();
}

/* s183: a surface into the map layer, as SDL_BlitSurface would put it into
   vulture_screen (its blend mode, colour and alpha modulation) */
void vulture_gpumap_blit(SDL_Surface *s, const SDL_Rect *srcrect, int x, int y)
{
	SDL_Texture *t;
	SDL_BlendMode bm = SDL_BLENDMODE_BLEND;
	Uint8 r = 255, g = 255, b = 255, a = 255;
	SDL_Rect d;

	if (!vulture_gpumap_on || !s || !(t = vgm_texture(s)))
		return;
	SDL_GetSurfaceBlendMode(s, &bm);
	SDL_GetSurfaceColorMod(s, &r, &g, &b);
	SDL_GetSurfaceAlphaMod(s, &a);
	SDL_SetTextureBlendMode(t, bm);
	SDL_SetTextureColorMod(t, r, g, b);
	SDL_SetTextureAlphaMod(t, a);
	SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
	d.x = x, d.y = y;
	d.w = srcrect ? srcrect->w : s->w, d.h = srcrect ? srcrect->h : s->h;
	SDL_RenderCopy(vulture_gpu, t, srcrect, &d);
}

/* s183: the sprite transformer's pose (levelwin vtx_blit): scaled (sx, sy)
   and turned (deg, clockwise) about the feet - the bottom centre of the
   untransformed image at (dx, dy) - tinted red, faded */
void vulture_gpumap_blit_ex(SDL_Surface *s, int dx, int dy, double sx, double sy, double deg,
                            bool red, int alpha)
{
	SDL_Texture *t;
	SDL_FRect d;
	SDL_FPoint c;

	if (!vulture_gpumap_on || !s || sx <= 0.0 || sy <= 0.0 || !(t = vgm_texture(s)))
		return;
	d.w = (float) (s->w * sx), d.h = (float) (s->h * sy);
	d.x = (float) (dx + s->w * 0.5 * (1.0 - sx));
	d.y = (float) (dy + s->h * (1.0 - sy));
	c.x = d.w * 0.5f, c.y = d.h;
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureColorMod(t, 255, red ? 77 : 255, red ? 77 : 255);
	SDL_SetTextureAlphaMod(t, (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
	SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
	SDL_RenderCopyExF(vulture_gpu, t, NULL, &d, deg, &c, SDL_FLIP_NONE);
}

/* s183: vzoom_apply's rectangles (render space) for the map layer; NULL: none */
void vulture_gpumap_set_zoom(const SDL_Rect *src, const SDL_Rect *dst, int k, bool exact)
{
	vgm_zoom = src && dst;
	if (vgm_zoom) {
		vgm_zsrc = *src, vgm_zdst = *dst;
		vgm_zk = k < 1 ? 1 : k;
		vgm_zexact = exact;
	}
}

/* s183: a surface is about to be freed - drop its texture */
void vulture_gpu_forget(SDL_Surface *s)
{
	std::unordered_map<SDL_Surface *, vgm_ent>::iterator it;

	if (!s || vgm_cache.empty())
		return;
	it = vgm_cache.find(s);
	if (it != vgm_cache.end()) {
		SDL_DestroyTexture(it->second.t);
		vgm_cache.erase(it);
	}
}

/* s183: the frame as it would be shown - the GPU map under the overlay, or
   just vulture_screen - from `area` (render space) into out (ARGB8888, at
   least area-sized); always opaque */
bool vulture_frame_to_surface(SDL_Surface *out, const SDL_Rect *area)
{
	SDL_Rect a;
	bool ok = false;
	int y, x;

	if (!out || !vulture_screen || out->format->BytesPerPixel != 4)
		return false;
	if (area)
		a = *area;
	else
		a.x = 0, a.y = 0, a.w = vulture_screen->w, a.h = vulture_screen->h;
	if (vgm_live()) {
		vgm_upload_overlay(NULL);
		vgm_compose();
		SDL_SetRenderTarget(vulture_gpu, vgm_out);
		if (SDL_LockSurface(out) == 0) {
			ok = SDL_RenderReadPixels(vulture_gpu, &a, out->format->format, out->pixels,
			                          out->pitch) == 0;
			SDL_UnlockSurface(out);
		}
		SDL_SetRenderTarget(vulture_gpu, NULL);
	}
	if (!ok) {
		SDL_BlendMode bm;
		SDL_Rect d = { 0, 0, a.w, a.h };

		SDL_GetSurfaceBlendMode(vulture_screen, &bm);
		SDL_SetSurfaceBlendMode(vulture_screen, SDL_BLENDMODE_NONE);
		ok = SDL_BlitSurface(vulture_screen, &a, out, &d) == 0;
		SDL_SetSurfaceBlendMode(vulture_screen, bm);
	}
	if (ok && out->format->Amask && SDL_LockSurface(out) == 0) { /* opaque */
		for (y = 0; y < a.h && y < out->h; y++) {
			Uint32 *row = (Uint32 *) ((Uint8 *) out->pixels + (size_t) y * out->pitch);

			for (x = 0; x < a.w && x < out->w; x++)
				row[x] |= out->format->Amask;
		}
		SDL_UnlockSurface(out);
	}
	return ok;
}

/* s183: the frame as shown, saved as a BMP (screenshots and the probes) */
int vulture_save_frame_bmp(const char *path)
{
	SDL_Surface *o;
	int r = -1;

	if (!vulture_screen)
		return -1;
	o = SDL_CreateRGBSurfaceWithFormat(0, vulture_screen->w, vulture_screen->h, 32,
	                                   SDL_PIXELFORMAT_ARGB8888);
	if (!o)
		return -1;
	if (vulture_frame_to_surface(o, NULL))
		r = SDL_SaveBMP(o, path);
	SDL_FreeSurface(o);
	return r;
}

/* s183: the renderer lost its render targets' pixels, or every texture */
static void vgm_lost(bool device)
{
	vgm_valid = false;
	vulture_gpumap_lost = true;
	if (device && vulture_gpu) {
		vgm_drop(true);
		if (vulture_gpu_tex)
			SDL_DestroyTexture(vulture_gpu_tex);
		vulture_gpu_tex = NULL;
		vgpu_reattach(vulture_render_scale);
	}
}

void vulture_reattach_render_surface(void)
{
	int want = vulture_opts.render_scale;
	int ww, wh;

	if (want < 1)
		want = 1;
	if (want > V_RENDER_SCALE_MAX)
		want = V_RENDER_SCALE_MAX;
	if (vulture_opts.gpu && !vulture_gpu_failed && vulture_sdl_window && vgpu_reattach(want))
		return; /* s168 */

	vulture_window_surface = vulture_sdl_window ?
			SDL_GetWindowSurface(vulture_sdl_window) : NULL;

	if (!vulture_window_surface) {
		/* no window surface: keep the historical behaviour of leaving
		 * vulture_screen NULL so the callers' !vulture_screen fallbacks
		 * still fire */
		vulture_render_scale = 1;
		vulture_screen = NULL;
		return;
	}

	if (want == 1) {
		if (vulture_render_surface) {
			SDL_FreeSurface(vulture_render_surface);
			vulture_render_surface = NULL;
		}
		vulture_render_scale = 1;
		vulture_screen = vulture_window_surface;
		return;
	}

	ww = vulture_window_surface->w;
	wh = vulture_window_surface->h;

	/* throw the old one away only if it no longer fits */
	if (vulture_render_surface &&
		(vulture_render_surface->w != ww * want ||
		 vulture_render_surface->h != wh * want ||
		 vulture_render_surface->format->format !=
				vulture_window_surface->format->format)) {
		SDL_FreeSurface(vulture_render_surface);
		vulture_render_surface = NULL;
	}

	if (!vulture_render_surface)
		vulture_render_surface = SDL_CreateRGBSurfaceWithFormat(0,
				ww * want, wh * want,
				vulture_window_surface->format->BitsPerPixel,
				vulture_window_surface->format->format);

	if (!vulture_render_surface) {
		vulture_sdl_error(__FILE__, __LINE__,
				"Could not create the scaled render surface, falling back to render_scale=1");
		vulture_opts.render_scale = 1;
		vulture_render_scale = 1;
		vulture_screen = vulture_window_surface;
		return;
	}

	/* vulture_screen is a blit SOURCE in exactly two places -
	 * vulture_fade_in() (vulture_gra.cpp, copies the screen into a temp) and
	 * window::update_background() (winclass/window.cpp, captures the area a
	 * window is about to cover) - plus the SDL_BlitScaled fallback in
	 * vulture_present_rect() below.  All three want a straight copy.  SDL2
	 * defaults a surface WITH an alpha channel to SDL_BLENDMODE_BLEND, and
	 * blending on scale mangles alpha in this codebase (see vulture_tile.cpp
	 * ~297), so pin it.
	 *
	 * NOTE this is the one place where "1x is byte-identical" is
	 * format-dependent rather than unconditional: on Windows the window
	 * framebuffer has no alpha (that is why vulture_init_colors() has to patch
	 * an Amask on at vulture_init.cpp ~585), so the window surface defaults to
	 * BLENDMODE_NONE too and this call changes nothing.  On a hypothetical
	 * platform whose window surface carries alpha, the 1x path would blend at
	 * those two sites and the scaled path would not.
	 *
	 * (vulture_get_img() is NOT a blit - it is a raw memcpy loop in
	 * vulture_gra.cpp:vulture_get_img_src() - so blend mode is irrelevant to
	 * it.)
	 */
	SDL_SetSurfaceBlendMode(vulture_render_surface, SDL_BLENDMODE_NONE);

	/* A reused surface keeps whatever clip rect the last frame left on it,
	 * whereas SDL always handed back a freshly unclipped window surface.
	 * vulture_win.cpp's "no level window" path does an unguarded
	 * SDL_FillRect(vulture_screen, NULL, ...), which HONOURS the clip rect, so
	 * reset it to match the historical behaviour. */
	SDL_SetClipRect(vulture_render_surface, NULL);

	vulture_render_scale = want;
	vulture_screen = vulture_render_surface;
}


/*
 * Change the supersampling factor at runtime (F8, and the Interface Options
 * menu when only the scale changed).
 *
 * Deliberately does NOT go through vulture_set_screensize(): that would
 * re-issue SDL_SetWindowSize()/SDL_SetWindowFullscreen() for a size that has
 * not changed - a redundant mode set that can flash in fullscreen, and that
 * would drag the window back to vulture_opts.width/height after the
 * sub-540x300 resize kludge has clamped them.  Everything that actually needs
 * doing is the reattach plus the relayout, and vulture_win_resize() is the
 * same relayout a real window resize performs.
 */
void vulture_set_render_scale(int scale)
{
	if (scale < 1)
		scale = 1;
	if (scale > V_RENDER_SCALE_MAX)
		scale = V_RENDER_SCALE_MAX;

	vulture_opts.render_scale = scale;
	vulture_reattach_render_surface();

	/* Deliberately NO vulture_ui_rescale() here.  vulture_win_resize() calls
	 * it as its first act - it has to, because it is the single point every
	 * relayout passes through and the fonts and chrome must be at the new
	 * factor before any window lays itself out.  Calling it here as well would
	 * CONSUME the scale change: the second call would find the scales already
	 * equal, return "nothing happened", and vulture_win_resize() would skip
	 * the full repaint and the stale-backdrop purge that a scale change
	 * requires - leaving the previous frame's UI smeared under the new one
	 * (doubled status text, a dead block where the old minimap was).  That is
	 * exactly what F8 did before this was removed. */
	if (vulture_screen)
		vulture_win_resize(vulture_screen->w, vulture_screen->h);
}


/*
 * Downscale one WINDOW-space rectangle from vulture_screen onto the window
 * surface.  win_rect must already be clipped to the window surface.
 *
 * Box filter, not SDL_BlitScaled: SDL2's SDL_BlitScaled is
 * SDL_UpperBlitScaled, which hardcodes SDL_ScaleModeNearest (there is no
 * public surface scale mode in SDL2), so at 2x it would throw away three of
 * every four supersampled pixels - 1px features like wall outlines and font
 * stems would flicker in and out instead of getting smaller.  Averaging is
 * the whole point of rendering large and presenting small.
 *
 * Both surfaces always have the SAME format here (the render surface is
 * created in the window surface's format), and it is 32bpp in every
 * configuration this port ships, so each BYTE LANE can be averaged
 * independently: that is exactly a per-channel average whatever the channel
 * order is, with no need to know the masks.  Anything unexpected falls back
 * to SDL's nearest-neighbour scaled blit.
 */
static void vulture_present_rect(const SDL_Rect *win_rect)
{
	const int s = vulture_render_scale;
	SDL_Rect src;

	src.x = win_rect->x * s;
	src.y = win_rect->y * s;
	src.w = win_rect->w * s;
	src.h = win_rect->h * s;

	if (vulture_screen->format->BytesPerPixel != 4 ||
		vulture_window_surface->format->BytesPerPixel != 4) {
		SDL_Rect dst = *win_rect;
		SDL_SetSurfaceBlendMode(vulture_screen, SDL_BLENDMODE_NONE);
		SDL_BlitScaled(vulture_screen, &src, vulture_window_surface, &dst);
		return;
	}

	{
		int locked_src = 0, locked_dst = 0;

		/* This is the only code in the port that writes the WINDOW surface's
		 * raw memory, so honour the lock protocol rather than assuming (as the
		 * rest of the port does for vulture_screen) that it never needs one. */
		if (SDL_MUSTLOCK(vulture_screen)) {
			if (SDL_LockSurface(vulture_screen) < 0)
				return;
			locked_src = 1;
		}
		if (SDL_MUSTLOCK(vulture_window_surface)) {
			if (SDL_LockSurface(vulture_window_surface) < 0) {
				if (locked_src)
					SDL_UnlockSurface(vulture_screen);
				return;
			}
			locked_dst = 1;
		}

		{
			const int spitch = vulture_screen->pitch;
			const int dpitch = vulture_window_surface->pitch;
			Uint8 *srow = (Uint8 *)vulture_screen->pixels
					+ (size_t)src.y * spitch + (size_t)src.x * 4;
			Uint8 *drow = (Uint8 *)vulture_window_surface->pixels
					+ (size_t)win_rect->y * dpitch + (size_t)win_rect->x * 4;
			int dy, dx, sy, sx, lane;

			if (s == 2) {
				/* the case that matters (1920x1080 window -> 3840x2160
				 * render).  SWAR average of two packed pixels: per lane,
				 * floor(a/2) + floor(b/2) + (a&b&1) == floor((a+b)/2), and
				 * neither addend can carry out of its lane. */
#define V_AVG2(a, b) ((((a) >> 1) & 0x7f7f7f7fu) + (((b) >> 1) & 0x7f7f7f7fu) \
                      + ((a) & (b) & 0x01010101u))
				for (dy = 0; dy < win_rect->h; dy++) {
					const Uint32 *s0 = (const Uint32 *)srow;
					const Uint32 *s1 = (const Uint32 *)(srow + spitch);
					Uint32 *dp = (Uint32 *)drow;

					for (dx = 0; dx < win_rect->w; dx++) {
						Uint32 top = V_AVG2(s0[2 * dx], s0[2 * dx + 1]);
						Uint32 bot = V_AVG2(s1[2 * dx], s1[2 * dx + 1]);
						dp[dx] = V_AVG2(top, bot);
					}
					srow += (size_t)spitch * 2;
					drow += dpitch;
				}
#undef V_AVG2
			} else {
				/* generic integer scale (3x, 4x) */
				for (dy = 0; dy < win_rect->h; dy++) {
					Uint8 *sp = srow;
					Uint8 *dp = drow;

					for (dx = 0; dx < win_rect->w; dx++) {
						for (lane = 0; lane < 4; lane++) {
							unsigned int acc = 0;
							for (sy = 0; sy < s; sy++) {
								const Uint8 *q = sp + (size_t)sy * spitch + lane;
								for (sx = 0; sx < s; sx++)
									acc += q[sx * 4];
							}
							dp[lane] = (Uint8)(acc / (unsigned int)(s * s));
						}
						sp += 4 * s;
						dp += 4;
					}
					srow += (size_t)spitch * s;
					drow += dpitch;
				}
			}
		}

		if (locked_dst)
			SDL_UnlockSurface(vulture_window_surface);
		if (locked_src)
			SDL_UnlockSurface(vulture_screen);
	}
}


static void vulture_set_fullscreen(void)
{
	/* SDL2: use desktop display mode for fullscreen */
	SDL_DisplayMode dm;
	int newwidth, newheight;

	if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
		newwidth = dm.w;
		newheight = dm.h;
	} else {
		newwidth = vulture_opts.width;
		newheight = vulture_opts.height;
	}

	if (vulture_sdl_window) {
		SDL_SetWindowSize(vulture_sdl_window, newwidth, newheight);
		SDL_SetWindowFullscreen(vulture_sdl_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		vulture_reattach_render_surface();
	} else {
		vulture_sdl_window = SDL_CreateWindow("Vulture NetHack",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			newwidth, newheight, SDL_WINDOW_FULLSCREEN_DESKTOP);
		if (vulture_sdl_window)
			vulture_reattach_render_surface();
	}

	/* every window lays itself out against the RENDER size, so that is what
	 * vulture_win_resize() must be told.  vulture_render_scale is 1 unless
	 * scaling is actually active, so this is the old call at 1x. */
	vulture_win_resize(newwidth * vulture_render_scale,
					newheight * vulture_render_scale);
}



static void vulture_set_windowed()
{
	/* SDL2: create or resize window */
	if (vulture_sdl_window) {
		SDL_SetWindowFullscreen(vulture_sdl_window, 0);
		SDL_SetWindowSize(vulture_sdl_window, vulture_opts.width, vulture_opts.height);
		vulture_reattach_render_surface();
	} else {
		vulture_sdl_window = SDL_CreateWindow("Vulture NetHack",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			vulture_opts.width, vulture_opts.height, SDL_WINDOW_RESIZABLE
			| (getenv("VULTURE_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN : 0)); /* s183: GPU probes */
		if (vulture_sdl_window)
			vulture_reattach_render_surface();
	}
	vulture_win_resize(vulture_opts.width * vulture_render_scale,
					vulture_opts.height * vulture_render_scale);
}



void vulture_set_screensize(void)
{
	if (vulture_opts.fullscreen)
		vulture_set_fullscreen();
	else
		vulture_set_windowed();
}



int vulture_handle_global_event(SDL_Event * event)
{
	static int quitting = 0;
	static int need_kludge = 0;
	static point actual_winsize = {0,0};

	/*
	 * SDL reports mouse coordinates in WINDOW space; every window in the port
	 * is laid out in RENDER space.  Convert here, in place, because this is
	 * the single earliest point every event passes through: each of
	 * vulture_wait_event(), vulture_poll_event(), vulture_wait_input() and
	 * vulture_wait_key() calls this exactly once per event before handing the
	 * same SDL_Event on to window::event_handler(), which is where the only
	 * other raw reads of event->button.x/y and event->motion.xrel/yrel live.
	 * Nothing re-enters this function with an already-converted event.
	 */
	if (vulture_render_scale > 1) {
		switch (event->type) {
		case SDL_MOUSEMOTION:
			event->motion.x *= vulture_render_scale;
			event->motion.y *= vulture_render_scale;
			event->motion.xrel *= vulture_render_scale;
			event->motion.yrel *= vulture_render_scale;
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			event->button.x *= vulture_render_scale;
			event->button.y *= vulture_render_scale;
			break;
		default:
			break;
		}
	}

	/* blarg. see the SDL_WINDOWEVENT_RESIZED case */
	if (need_kludge &&
		(actual_winsize.x != vulture_opts.width ||
		actual_winsize.y != vulture_opts.height))
	{
    vulture_set_screensize();
		actual_winsize.x = vulture_opts.width;
		actual_winsize.y = vulture_opts.height;
		need_kludge = 0;
	}

	switch (event->type)
	{
		case SDL_RENDER_TARGETS_RESET: /* s183: the map layer lost its pixels */
			vgm_lost(false);
			break;
		case SDL_RENDER_DEVICE_RESET: /* s183: ... or every texture */
			vgm_lost(true);
			break;
		case SDL_WINDOWEVENT:
			switch (event->window.event) {
				case SDL_WINDOWEVENT_EXPOSED:
					vulture_refresh();
					break;
				case SDL_WINDOWEVENT_FOCUS_GAINED:
					vulture_has_focus = 1; /* s140: real-time pauses without it */
					vulture_focus_mute(0); /* s64 */
					vulture_refresh();
					break;
				case SDL_WINDOWEVENT_FOCUS_LOST:
					vulture_has_focus = 0; /* s140 */
					vulture_focus_mute(1); /* s64: alt-tabbed away */
					break;
				case SDL_WINDOWEVENT_ENTER:
					have_mouse_focus = 1;
					break;
				case SDL_WINDOWEVENT_LEAVE:
					have_mouse_focus = 0;
					break;
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					/* Stale-pointer guard.  SDL2 frees and recreates the
					 * window surface on ANY size change, and this port now
					 * writes that surface's raw memory in
					 * vulture_present_rect(), so a stale pointer is a write
					 * into freed memory rather than merely a failed
					 * SDL_UpdateWindowSurface.
					 *
					 * Reattach only - deliberately no relayout.  SDL sends
					 * SIZE_CHANGED for programmatic changes too, and every
					 * programmatic SDL_SetWindowSize/SetWindowFullscreen in
					 * this port is already followed synchronously by
					 * vulture_reattach_render_surface() + vulture_win_resize(),
					 * so by the time this queued event arrives the size has
					 * been fully handled and the reattach is a no-op (the
					 * render surface is reused, not reallocated).  For a
					 * user-driven resize SDL sends SIZE_CHANGED *and*
					 * RESIZED, and the RESIZED case below does the relayout. */
					vulture_reattach_render_surface();
					break;
				case SDL_WINDOWEVENT_RESIZED:
				{
					int rw = event->window.data1;
					int rh = event->window.data2;
					actual_winsize.x = rw;
					actual_winsize.y = rh;
					vulture_opts.width = rw;
					if (rw < 540) {
						need_kludge = 1;
						vulture_opts.width = 540;
					}
					vulture_opts.height = rh;
					if (rh < 300) {
						need_kludge = 1;
						vulture_opts.height = 300;
					}
					/* vulture_set_screensize() -> vulture_set_windowed() /
					 * vulture_set_fullscreen() reattaches unconditionally, so
					 * there is deliberately no separate reattach call here. */
					vulture_set_screensize();
					/* Refresh cached mouse position in the new coordinate space
					 * so clicks immediately after resize hit the right tile */
					{
						int mx, my;
						SDL_GetMouseState(&mx, &my);
						/* SDL_GetMouseState is raw window space */
						vulture_set_mouse_pos(mx * vulture_render_scale,
										my * vulture_render_scale);
					}
					break;
				}
			}
			break;

		case SDL_QUIT:
			if (quitting)
				break;

			/* prevent recursive quit dialogs... */
			quitting++;

			/* exit gracefully */
			if (program_state.gameover)
			{
				/* assume the user really meant this, as the game is already over... */
				/* to make sure we still save bones, just set stop printing flag */
				program_state.stopprint++;
				/* and send keyboard input as if user pressed ESC */
				vulture_eventstack_add('\033', -1, -1, V_RESPOND_POSKEY);
			}
			else if (!program_state.something_worth_saving)
			{
				/* User exited before the game started, e.g. during splash display */
				/* Just get out. */
				vulture_bail(NULL);
			}
			else
			{
				switch (vulture_yn_function("Save and quit?", "yn", 'n'))
				{
					case 'y':
						vulture_eventstack_add('y', -1 , -1, V_RESPOND_CHARACTER);
						dosave();
						break;
					case 'n':
						vulture_eventstack_add('\033', -1, -1, V_RESPOND_CHARACTER);
						break;
					default:
						break;
				}
			}
			quitting--;
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			vulture_set_mouse_pos(event->button.x, event->button.y);
			break;

		case SDL_MOUSEMOTION:
			vulture_set_mouse_pos(event->motion.x, event->motion.y);
			have_mouse_focus = 1; /* mouse is over canvas; backup for WINDOWEVENT_ENTER */
			break;

		case SDL_KEYDOWN:
			if (event->key.keysym.sym == SDLK_TAB && (event->key.keysym.mod & KMOD_ALT) && vulture_opts.fullscreen)
			{
				/* go out of fullscreen for alt + tab */
				vulture_opts.fullscreen = 0;
				vulture_set_windowed();
				if (!vulture_opts.fullscreen && !fs_message_printed++)
					/* This gets displayed only once */
					pline("You have left fullscreen mode. Press ALT+RETURN to reenter it.");

				event->type = 0; /* we don't want to leave the event loop for this */
				return 1;
			}
			else if(event->key.keysym.sym == SDLK_RETURN && (event->key.keysym.mod & KMOD_ALT))
			{
				/* toggle fullscreen with ctrl + enter */
				if (vulture_opts.fullscreen)
				{
					vulture_opts.fullscreen = 0;
					vulture_set_windowed();
				}
				else
				{
					vulture_opts.fullscreen = 1;
					vulture_set_fullscreen();
				}
				event->type = 0; /* we don't want to leave the event loop for this */
				return 1;
			}
			else if(event->key.keysym.sym == SDLK_SYSREQ ||
					event->key.keysym.sym == SDLK_PRINT ||
					event->key.keysym.sym == SDLK_F12)
			{
				vulture_save_screenshot();
				event->type = 0; /* we don't want to leave the event loop for this */
				return 1;
			}
			/* Acceptance-test save key: queue the save command through
			 * NetHack's OWN command queue ('S' then the "Really save?"
			 * confirmation). Driving the same two keys through Vulture's
			 * normal GUI key path does NOT produce a save file, whereas the
			 * queued form does - so the harness uses this to get a
			 * reproducible save. */
			/*
			 * ACCEPTANCE-TEST HOOKS, both gated on their environment
			 * variable so a stray keypress cannot trigger them in a normal
			 * game.  They were previously unconditional, which made F9 a
			 * silent SAVE-AND-QUIT and F10 an "Error opening (d.lua)" in
			 * the paniclog of every player who brushed the key.
			 */
			else if (event->key.keysym.sym == SDLK_F9
			         && getenv("VULTURE_AUTOSAVE"))
			{
				cmdq_add_key(CQ_CANNED, 'S');
				cmdq_add_key(CQ_CANNED, 'y');
				event->type = 0;
				return 1;
			}
			/* Run the Lua script named by $VULTURE_LUADRIVER through
			 * NetHack's own scripting sandbox.  #wizloadlua has no default
			 * binding and no AUTOCOMPLETE flag, so it is not reachable from
			 * Vulture's extended-command menu, and BINDINGS= does not reach
			 * this windowport - but F-keys demonstrably do (F12). */
			else if (event->key.keysym.sym == SDLK_F10
			         && getenv("VULTURE_LUADRIVER")
			         && *getenv("VULTURE_LUADRIVER"))
			{
				const char *script = getenv("VULTURE_LUADRIVER");
				nhl_sandbox_info sbi = {NHL_SB_SAFE | NHL_SB_DEBUGGING,
				                        16*1024*1024, 0, 16*1024*1024};

				(void) load_lua(script, &sbi);
				event->type = 0;
				return 1;
			}
			/* magic tileconfig reload key */
			else if (event->key.keysym.sym == SDLK_F11 &&
					(event->key.keysym.mod & KMOD_ALT) &&
					(event->key.keysym.mod & KMOD_SHIFT))
			{
				vulture_unload_gametiles();
				vulture_load_gametiles();
				levwin->force_redraw();
				ROOTWIN->draw_windows();
				pline("tileconfig reloaded!");
			}

			/* object highlight */
			if (vulture_map_highlight_objects == 0 &&
				((event->key.keysym.mod & KMOD_RCTRL) ||
				event->key.keysym.sym == SDLK_RCTRL))
			{
				/* enable highlighting */
				vulture_map_highlight_objects = 1;

				/* set the max clipping rect */
				levwin->force_redraw();

				/* redraw with the object highlight */
				ROOTWIN->draw_windows();

				/* the mouse got painted over, restore it */
				vulture_mouse_draw();

				/* bring the buffer onto the screen */
				vulture_refresh_window_region();

				/* erase the cursor's last shown rect and show the new one
				 * (IMPLEMENTATION_LOG §35) - the window refresh alone only
				 * covers what the redraw invalidated */
				vulture_mouse_refresh();

				/* make sure no after-images of the mouse are left over */
				vulture_mouse_restore_bg();
			}

			break;

		case SDL_KEYUP:
			if (vulture_map_highlight_objects == 1)
			{
				/* disable highlighting */
				vulture_map_highlight_objects = 0;

				/* set the max clipping rect */
				levwin->force_redraw();

				/* redraw without the object highlight */
				ROOTWIN->draw_windows();

				/* the mouse got painted over, restore it */
				vulture_mouse_draw();

				/* bring the buffer onto the screen */
				vulture_refresh_window_region();

				/* erase the cursor's last shown rect and show the new one
				 * (IMPLEMENTATION_LOG §35) - the window refresh alone only
				 * covers what the redraw invalidated */
				vulture_mouse_refresh();

				/* make sure no after-images of the mouse are left over */
				vulture_mouse_restore_bg();
			}
			break;
	}

	return 0;
}



static void vulture_sdl_error(const char *file, int line, const char *what)
{
	vulture_write_log(V_LOG_ERROR, file, line, "%s: %s\n", what, SDL_GetError());
}


void vulture_enter_graphics_mode()
{
	 SDL_SetMainReady();  /* we define SDL_MAIN_HANDLED */
	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) == -1)
	{
		vulture_sdl_error(__FILE__, __LINE__, "Could not initialize SDL");
		exit(1);
	}

	/* Initialize the event handlers */
	atexit(SDL_Quit);

	vulture_set_screensize();

	/* no screen: maybe the configured video mode didn't work */
	if (!vulture_screen) {
		vulture_sdl_error(__FILE__, __LINE__, "Failed to set configured video mode, trying to fall back to 800x600 windowed");
		vulture_sdl_window = SDL_CreateWindow("Vulture NetHack",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			800, 600, SDL_WINDOW_RESIZABLE
			| (getenv("VULTURE_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN : 0)); /* s183 */
		if (vulture_sdl_window)
			vulture_reattach_render_surface();
		vulture_opts.fullscreen = 0;
	}

	/* still no screen: nothing more to be done */
	if (!vulture_screen)
	{
		vulture_sdl_error(__FILE__, __LINE__, "Could not initialize video mode");
		exit(1);
	}

	if (vulture_sdl_window)
		SDL_SetWindowTitle(vulture_sdl_window, VERSION_ID);

	/*
	 * Window icon (taskbar / Alt-Tab while running).  The linked .rc resource
	 * gives the EXE its icon in Explorer, but SDL creates its window class
	 * without one, so a running game showed the generic Windows placeholder.
	 * window_icon.png is the same artwork as the linked nh_icon.ico, exported
	 * to PNG because Vulture's loader does not read .ico (see
	 * toolchain/make_window_icon.py).  SDL copies the surface, so it can be
	 * freed immediately.
	 */
	if (vulture_sdl_window) {
		SDL_Surface *icon = vulture_load_graphic(
			vulture_make_filename(V_GRAPHICS_DIRECTORY, "", "window_icon.png"));

		if (icon) {
			SDL_SetWindowIcon(vulture_sdl_window, icon);
			SDL_FreeSurface(icon);
		}
	}

	/* The pointer is an OS cursor with Vulture's art (IMPLEMENTATION_LOG
	 * §53, vulture_mou.cpp), so the system cursor stays ON; the first
	 * vulture_mouse_draw()/set_mcursor() swaps in the art. */
	SDL_ShowCursor(SDL_ENABLE);
}



#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE
void vulture_change_resolution(int w, int h)
{
	if (w < 800) w = 800;
	if (h < 600) h = 600;
	/* Push a resize event onto the SDL queue so the game loop handles it
	 * safely instead of resizing mid-frame (reentrancy crash). */
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_WINDOWEVENT;
	ev.window.event = SDL_WINDOWEVENT_RESIZED;
	ev.window.data1 = w;
	ev.window.data2 = h;
	SDL_PushEvent(&ev);
}
}
#endif

void vulture_exit_graphics_mode(void)
{
	SDL_ShowCursor(SDL_ENABLE);

	vulture_stop_music();

	if (vulture_sdl_window) {
		SDL_DestroyWindow(vulture_sdl_window);
		vulture_sdl_window = NULL;
	}
	SDL_Quit();
}



void vulture_refresh_region
(
	int x1, int y1,
	int x2, int y2
)
{
	SDL_Rect rect;

	/* resizing support makes this check necessary: we may try to refresh
	* a region that used to be inside the window, but isn't anymore */
	x1 = (x1 >= vulture_screen->w) ? vulture_screen->w - 1 : x1;
	y1 = (y1 >= vulture_screen->h) ? vulture_screen->h - 1 : y1;

	/* Clip edges (yes, really, otherwise we crash...) */
	rect.x = (x1 < 0) ? 0 : x1;
	rect.y = (y1 < 0) ? 0 : y1;
	rect.w = ((x2 >= vulture_screen->w) ? vulture_screen->w - 1 : x2) - rect.x + 1;
	rect.h = ((y2 >= vulture_screen->h) ? vulture_screen->h - 1 : y2) - rect.y + 1;
	if (vulture_gpu) { /* s168 */
		vgpu_present(&rect);
		return;
	}

	/* rect is RENDER space up to here (the callers' coordinates, and the
	 * clamp above is against the render surface, which is correct).  Map it
	 * into WINDOW space for the present. */
	if (vulture_render_scale > 1 && vulture_window_surface &&
		vulture_screen != vulture_window_surface) {
		const int s = vulture_render_scale;
		int rx2 = rect.x + rect.w;      /* exclusive, render space */
		int ry2 = rect.y + rect.h;

		/* Round OUTWARD to whole s x s render blocks.  This is what makes a
		 * per-region present bit-identical to the same area of a full-frame
		 * present: each window pixel is the average of exactly the block
		 * beneath it, which only holds while the rect starts on a multiple
		 * of s.  Without the snap the filter samples a shifted block along
		 * the rect's edge and leaves a visible seam. */
		rect.x /= s;
		rect.y /= s;
		rect.w = (rx2 + s - 1) / s - rect.x;
		rect.h = (ry2 + s - 1) / s - rect.y;

		if (rect.x + rect.w > vulture_window_surface->w)
			rect.w = vulture_window_surface->w - rect.x;
		if (rect.y + rect.h > vulture_window_surface->h)
			rect.h = vulture_window_surface->h - rect.y;

		if (rect.w <= 0 || rect.h <= 0) {
			/* degenerate: nothing to filter.  Fall through with an empty
			 * rect rather than returning early, so that a degenerate rect
			 * still reaches SDL exactly as it does at 1x (SDL discards it).
			 * Keeps the two paths behaviourally identical for every input,
			 * not just the sane ones. */
			rect.w = 0;
			rect.h = 0;
		} else {
			vulture_present_rect(&rect);
		}
	}

	SDL_UpdateWindowSurfaceRects(vulture_sdl_window, &rect, 1);
}



void vulture_refresh(void)
{
	if (vulture_gpu) { /* s168 */
		vgpu_present(NULL);
		return;
	}
	if (vulture_render_scale > 1 && vulture_window_surface &&
		vulture_screen != vulture_window_surface) {
		SDL_Rect whole;

		whole.x = 0;
		whole.y = 0;
		whole.w = vulture_window_surface->w;
		whole.h = vulture_window_surface->h;
		vulture_present_rect(&whole);
	}

	if (vulture_sdl_window)
		SDL_UpdateWindowSurface(vulture_sdl_window);
}


/*
 * IMPLEMENTATION_LOG s74: screen shake + red hit flash.  Works on the
 * frame as presented (the window surface), so it is independent of the
 * supersampling path; blocks for ~8 frames, then restores the frame.
 * strength 1..8.
 */
extern "C" void vulture_screen_shake(int strength)
{
	SDL_Surface *win, *copy, *red;
	SDL_Rect oldclip;
	int f, frames = 10, amp; /* s97: 10 frames (was 8) */

	if (!vulture_sdl_window || strength <= 0)
		return;
	if (vulture_gpu && vulture_gpu_tex) { /* s168: shake on the GPU */
		int ow = 0, oh = 0;

		SDL_Texture *frame = vulture_gpu_tex;

		SDL_GetRendererOutputSize(vulture_gpu, &ow, &oh);
		vgm_upload_overlay(NULL);
		if (vgm_live()) { /* s183: the map layer under the overlay */
			vgm_compose();
			frame = vgm_out;
		}
		SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
		SDL_SetTextureColorMod(frame, 255, 255, 255);
		if (strength > 8)
			strength = 8;
		amp = 4 + strength * 3;
		for (f = 0; f < frames; f++) {
			double k = 1.0 - (double) f / frames;
			SDL_Rect d;

			d.x = (int) (((rand() % (2 * amp + 1)) - amp) * k) - amp;
			d.y = (int) (((rand() % (2 * amp + 1)) - amp) * k) - amp;
			d.w = ow + 2 * amp, d.h = oh + 2 * amp;
			SDL_SetRenderDrawColor(vulture_gpu, 0, 0, 0, 255);
			SDL_RenderClear(vulture_gpu);
			SDL_RenderCopy(vulture_gpu, frame, NULL, &d);
			SDL_SetRenderDrawBlendMode(vulture_gpu, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(vulture_gpu, 170, 0, 0, (Uint8) ((60 + 15 * strength) * k));
			SDL_RenderFillRect(vulture_gpu, NULL);
			SDL_RenderPresent(vulture_gpu);
			SDL_Delay(16);
		}
		SDL_RenderCopy(vulture_gpu, frame, NULL, NULL);
		SDL_RenderPresent(vulture_gpu);
		return;
	}
	win = SDL_GetWindowSurface(vulture_sdl_window);
	if (!win)
		return;
	/* s106: the window surface may BE vulture_screen, still clipped to
	 * whatever region was drawn last - the red flash (and the shake) then
	 * covered only that rectangle, a red box on every hit.  Unclip for the
	 * duration and put the clip back afterwards. */
	SDL_GetClipRect(win, &oldclip);
	SDL_SetClipRect(win, NULL);
	copy = SDL_ConvertSurface(win, win->format, 0);
	if (!copy) {
		SDL_SetClipRect(win, &oldclip);
		return;
	}
	/* s109: copy it back OPAQUE.  SDL gives a converted surface with an
	 * alpha channel BLEND mode, so the frame was alpha-blended onto itself
	 * each shake frame: everywhere blood had left partial alpha (the stain
	 * and droplet rectangles) soaked up the red flash and showed as solid
	 * red boxes until the next full redraw. */
	SDL_SetSurfaceBlendMode(copy, SDL_BLENDMODE_NONE);
	red = SDL_CreateRGBSurfaceWithFormat(0, win->w, win->h, 32,
	                                     SDL_PIXELFORMAT_ARGB8888);
	if (red) {
		SDL_FillRect(red, NULL, SDL_MapRGBA(red->format, 170, 0, 0, 255));
		SDL_SetSurfaceBlendMode(red, SDL_BLENDMODE_BLEND);
	}
	if (strength > 8)
		strength = 8;
	amp = 4 + strength * 3; /* s97: about 3x the old jolt for a small hit */
	for (f = 0; f < frames; f++) {
		double k = 1.0 - (double) f / frames;
		SDL_Rect d;

		/* s94: overscan - the frame is drawn amp px bigger on every side,
		 * so shifting it never uncovers the window (the old black fill
		 * showed as black bars along two edges on every hit) */
		d.x = (int) (((rand() % (2 * amp + 1)) - amp) * k) - amp;
		d.y = (int) (((rand() % (2 * amp + 1)) - amp) * k) - amp;
		d.w = win->w + 2 * amp;
		d.h = win->h + 2 * amp;
		SDL_BlitScaled(copy, NULL, win, &d);
		if (red) {
			SDL_SetSurfaceAlphaMod(red, (Uint8) ((60 + 15 * strength) * k));
			SDL_BlitSurface(red, NULL, win, NULL);
		}
		SDL_UpdateWindowSurface(vulture_sdl_window);
		SDL_Delay(16);
	}
	SDL_BlitSurface(copy, NULL, win, NULL);
	SDL_UpdateWindowSurface(vulture_sdl_window);
	if (red)
		SDL_FreeSurface(red);
	SDL_FreeSurface(copy);
	SDL_SetClipRect(win, &oldclip);
}
