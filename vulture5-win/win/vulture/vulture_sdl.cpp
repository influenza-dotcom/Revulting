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
void vulture_reattach_render_surface(void)
{
	int want = vulture_opts.render_scale;
	int ww, wh;

	if (want < 1)
		want = 1;
	if (want > V_RENDER_SCALE_MAX)
		want = V_RENDER_SCALE_MAX;

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
			vulture_opts.width, vulture_opts.height, SDL_WINDOW_RESIZABLE);
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
		case SDL_WINDOWEVENT:
			switch (event->window.event) {
				case SDL_WINDOWEVENT_EXPOSED:
					vulture_refresh();
					break;
				case SDL_WINDOWEVENT_FOCUS_GAINED:
					vulture_focus_mute(0); /* s64 */
					vulture_refresh();
					break;
				case SDL_WINDOWEVENT_FOCUS_LOST:
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
			800, 600, SDL_WINDOW_RESIZABLE);
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
