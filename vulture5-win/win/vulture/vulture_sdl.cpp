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


#ifndef __EMSCRIPTEN__
/* s186: take the next queued event, skipping stale MOUSEMOTION (only the
   newest of a run matters for hovering) - but a middle-drag's run adds up:
   the event kept carries the whole run's xrel/yrel, so the camera moves as
   far as the mouse did (it moved by the last event's few pixels, so a fast
   drag felt like dragging through mud), and a click or key that ends a
   drag run waits one call, so the run before it isn't dropped either */
static SDL_Event vte_stash;
static int vte_stashed = 0;

static int vulture_take_event(SDL_Event * event)
{
	SDL_Event tmp;
	int n = 0, dx = 0, dy = 0;

	if (vte_stashed) {
		*event = vte_stash;
		vte_stashed = 0;
		return 1;
	}
	while (SDL_PollEvent(&tmp)) {
		if (tmp.type != SDL_MOUSEMOTION) {
			if (n && (event->motion.state & SDL_BUTTON_MMASK)) {
				vte_stash = tmp, vte_stashed = 1; /* the drag first */
				break;
			}
			*event = tmp;
			return 1;
		}
		dx += tmp.motion.xrel, dy += tmp.motion.yrel;
		*event = tmp;
		n++;
	}
	if (n && (event->motion.state & SDL_BUTTON_MMASK))
		event->motion.xrel = dx, event->motion.yrel = dy;
	return n > 0;
}
#endif

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
		/* Drain stale MOUSEMOTION events, keeping the last polled event
		   (s186: a middle-drag keeps their motion too) */
		(void) vulture_take_event(event);
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
		/* Drain stale MOUSEMOTION events, keeping the last polled event
		   (s186: a middle-drag keeps their motion too) */
		(void) vulture_take_event(event);
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
		if (vte_stashed) /* s196: a key or click held back behind a middle-drag (s186) */
			*event = vte_stash, vte_stashed = 0;
		else if (!SDL_WaitEvent(event))
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
		if (vte_stashed) /* s196: as in vulture_wait_input */
			*event = vte_stash, vte_stashed = 0;
		else if (!SDL_WaitEvent(event))
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
static bool vgpu_gl_wanted = false; /* s185: OpenGL while the map is lit */

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
/* s194: the overlay as last uploaded (OpenGL) - see vgm_diff */
static std::vector<Uint32> vgm_shadow;
static int vgm_shadow_w = 0, vgm_shadow_h = 0;
static SDL_Texture *vgm_shadow_tex = NULL; /* the texture the copy matches */
static int vgm_diff_on = -1;               /* -1 unknown, 0 whole uploads, 1 compare */
static unsigned vgm_diff_up = 0;           /* probe: pixels uploaded by the last present */

struct vgm_ent {
	SDL_Texture *t;
	int w, h;
	void *px;
};
static std::unordered_map<SDL_Surface *, vgm_ent> vgm_cache;

/*
 * IMPLEMENTATION_LOG s201: sharper art when zoomed in.  Zoom drew the map at
 * 1x and blew the picture up - nearest at 2x and 3x, so every sprite turned
 * into blocks.  With tiles_hd/ (every sprite at double resolution, made by
 * toolchain/make_hd_tiles.py) the map layer is drawn AT the zoom instead:
 * levelwin sets the view each frame (vulture_gpumap_set_view: the 1x region
 * that will fill the window, and where it goes), and every map draw - blits,
 * posed sprites, cast shadows, normal maps - goes through vgm_vx / vgm_vy,
 * from the sprite's HD art where it has some (linear filtering otherwise).
 * The textures stay screen-sized, the lighting shader maps its pixels back
 * to the 1x map (u_view, u_vsc), and the compose step copies the map 1:1.
 * The screen shows the same region as before, so the mouse, the overlay and
 * the HUD are untouched - only sharper.
 */
struct vgm_hdkey {   /* where a surface's HD art comes from */
	std::string path;
	bool flip;       /* mirrored */
	int rgb;         /* colour scale, of 256 (a shaded copy) */
	int alpha;       /* alpha scale, of 256 (see-through walls, the invisible hero) */
	SDL_Surface *cut; /* s203: kept only where this 1x mask is opaque - or, cutout, clear */
	bool cutout;
};
static std::unordered_map<SDL_Surface *, vgm_hdkey> vgm_hd_src;
static std::unordered_map<std::string, SDL_Texture *> vgm_hd_cache; /* by vgm_hd_name */
static std::unordered_map<std::string, bool> vgm_hd_none;           /* art that isn't there */
static int vgm_hd_state = -1;     /* tiles_hd/: -1 not looked yet, 0 absent, 1 there */
static bool vgm_view = false;     /* this frame's map is drawn at the zoom */
static bool vgm_tex_view = false; /* the map layer holds a map drawn so */
static float vgm_vsx = 1.0f, vgm_vsy = 1.0f, vgm_vlx = 0.0f, vgm_vly = 0.0f;
static float vgm_vpx = 0.0f, vgm_vpy = 0.0f;
static SDL_Texture *vgm_dof_t[3] = { NULL, NULL, NULL }; /* s205: the map halved, quartered, eighthed */
static int vgm_dof_w = 0, vgm_dof_h = 0;
static int vgm_focus_y = -1; /* s205: where the camera focuses (you), 1x map space; < 0: the middle */
extern SDL_Surface *vulture_load_surface(char *srcbuf, unsigned int buflen); /* vulture_gfl.h */

/*
 * IMPLEMENTATION_LOG s185: dynamic lighting on the GPU.
 *
 * SDL2's renderer has no shaders, but its OpenGL backend lends its textures
 * to OpenGL (SDL_GL_BindTexture), so with the OpenGL renderer one GLSL pass
 * does what vl_nmap (normal-mapped relief) and vl_apply (the light layer)
 * did on the CPU, pixel for pixel the same arithmetic: the map layer, a
 * normal layer (the normal-map tiles, recorded during the map pass and
 * replayed into vgm_nrm), the light grid (darkness + tint, sampled
 * bilinearly) and the per-cell light directions (sampled per cell) go in;
 * the lit map comes out in vgm_lit, which the composite then shows instead
 * of vgm_tex.  Raw GL runs between SDL's own commands: the renderer is
 * flushed first and every bit of state is pushed and popped around the pass.
 * Without the OpenGL renderer or a working shader, lighting stays on the CPU.
 */
#ifdef _WIN32
#define VGL_API __stdcall
#else
#define VGL_API
#endif
typedef unsigned int vgl_enum;
typedef unsigned int vgl_uint;
typedef int vgl_int;
typedef int vgl_sizei;
typedef char vgl_char;
typedef float vgl_float;
typedef unsigned int vgl_bitfield;
#define VGL_FRAGMENT_SHADER 0x8B30
#define VGL_VERTEX_SHADER 0x8B31
#define VGL_COMPILE_STATUS 0x8B81
#define VGL_LINK_STATUS 0x8B82
#define VGL_CURRENT_PROGRAM 0x8B8D
#define VGL_TEXTURE0 0x84C0
#define VGL_TEXTURE_2D 0x0DE1
#define VGL_TEXTURE_MAG_FILTER 0x2800
#define VGL_TEXTURE_MIN_FILTER 0x2801
#define VGL_NEAREST 0x2600
#define VGL_LINEAR 0x2601
#define VGL_QUADS 0x0007
#define VGL_BLEND 0x0BE2
#define VGL_SCISSOR_TEST 0x0C11
#define VGL_ALL_ATTRIB_BITS 0x000FFFFF

static struct {
	vgl_uint (VGL_API *CreateShader)(vgl_enum);
	void (VGL_API *ShaderSource)(vgl_uint, vgl_sizei, const vgl_char *const *, const vgl_int *);
	void (VGL_API *CompileShader)(vgl_uint);
	void (VGL_API *GetShaderiv)(vgl_uint, vgl_enum, vgl_int *);
	void (VGL_API *GetShaderInfoLog)(vgl_uint, vgl_sizei, vgl_sizei *, vgl_char *);
	vgl_uint (VGL_API *CreateProgram)(void);
	void (VGL_API *AttachShader)(vgl_uint, vgl_uint);
	void (VGL_API *LinkProgram)(vgl_uint);
	void (VGL_API *GetProgramiv)(vgl_uint, vgl_enum, vgl_int *);
	void (VGL_API *GetProgramInfoLog)(vgl_uint, vgl_sizei, vgl_sizei *, vgl_char *);
	void (VGL_API *UseProgram)(vgl_uint);
	vgl_int (VGL_API *GetUniformLocation)(vgl_uint, const vgl_char *);
	void (VGL_API *Uniform1i)(vgl_int, vgl_int);
	void (VGL_API *Uniform4f)(vgl_int, vgl_float, vgl_float, vgl_float, vgl_float);
	void (VGL_API *Uniform4fv)(vgl_int, vgl_sizei, const vgl_float *); /* s190 */
	void (VGL_API *ActiveTexture)(vgl_enum);
	void (VGL_API *GetIntegerv)(vgl_enum, vgl_int *);
	void (VGL_API *PushAttrib)(vgl_bitfield);
	void (VGL_API *PopAttrib)(void);
	void (VGL_API *Disable)(vgl_enum);
	void (VGL_API *TexParameteri)(vgl_enum, vgl_enum, vgl_int);
	void (VGL_API *Begin)(vgl_enum);
	void (VGL_API *End)(void);
	void (VGL_API *TexCoord2f)(vgl_float, vgl_float);
	void (VGL_API *Vertex2f)(vgl_float, vgl_float);
} vgl;
static int vgl_state = -1; /* -1 untried, 0 can't, 1 ready */
static vgl_uint vgl_prog = 0;
enum { VGL_U_MAP, VGL_U_NRM, VGL_U_LIGHT, VGL_U_DIRS, VGL_U_SIZE, VGL_U_LG, VGL_U_LGS, VGL_U_DG,
       VGL_U_DGS, VGL_U_AREA, VGL_U_PL, VGL_U_PLI, VGL_U_RKA, VGL_U_RKB, VGL_U_NOISE, VGL_U_FOG,
       VGL_U_MIST, VGL_U_MCOL, VGL_U_VIEW, VGL_U_VSC, VGL_U_N };
static vgl_int vgl_loc[VGL_U_N];
static SDL_Texture *vgm_nrm = NULL;   /* the normal layer */
static SDL_Texture *vgm_lit = NULL;   /* the lit map */
static SDL_Texture *vgl_light = NULL; /* the light grid: A darkness, RGB tint */
static SDL_Texture *vgl_dirs = NULL;  /* per cell: light direction and strength */
static SDL_Texture *vgl_fogt = NULL;  /* s192: the fog of war per light-grid sample */
static SDL_Texture *vgl_noiset = NULL; /* s192: the mist's noise */
static SDL_Surface *vgl_fogsrc = NULL;
static bool vgl_fogfresh = false;
static const Uint8 *vgl_noisesrc = NULL;
static float vgl_mistp[8];
static bool vgm_lit_valid = false;    /* vgm_lit is this frame's map */
struct vgm_nrec {
	SDL_Surface *s;
	int x, y;
	bool ex;
	double sx, sy, deg;
	int alpha;
	bool quad;    /* s204: src laid into the quad q (a door leaf, a falling body) */
	float q[8];
	SDL_Rect src;
};
static std::vector<vgm_nrec> vgm_nlist; /* this frame's normal-map draws */
static SDL_Rect vgm_clip;               /* the region the map pass redrew */

static const char *const vgl_vs = R"VGL(#version 110
void main()
{
    gl_TexCoord[0] = gl_MultiTexCoord0;
    gl_Position = gl_Vertex;
}
)VGL";

/* vl_nmap + vl_apply (levelwin.cpp), in floats - same constants, same order */
static const char *const vgl_fs = R"VGL(#version 110
uniform sampler2D u_map;
uniform sampler2D u_nrm;
uniform sampler2D u_light;
uniform sampler2D u_dirs;
uniform vec4 u_size;  /* W, H, texcoord scale of the map / normal layers */
uniform vec4 u_lg;    /* light grid: origin x, y, cell size, - */
uniform vec4 u_lgs;   /* light grid: columns, rows, texcoord scale */
uniform vec4 u_dg;    /* direction grid: origin x, y, cell size, relief on */
uniform vec4 u_dgs;   /* direction grid: columns, rows, texcoord scale */
uniform vec4 u_area;  /* the map's rectangle: x0, y0, x1, y1 */
uniform vec4 u_pl[72]; /* s190: 24 point lights, 3 each: (x, y, y raised, height),
                          (radius + 1, intensity, r, g), (b, -, -, -) */
uniform vec4 u_pli;   /* 1 / V_MAP_XMOD, 1 / V_MAP_YMOD, how many lights, - */
uniform vec4 u_rka;   /* the key's direction, how much facing away from a height counts */
uniform vec4 u_rkb;   /* the relief's strength, the glint's */
uniform sampler2D u_noise; /* s192: the mist's noise (repeating) */
uniform sampler2D u_fog;   /* s192: the fog of war, beside the light grid */
uniform vec4 u_mist;  /* the map's anchor x, y, time, thickness */
uniform vec4 u_mcol;  /* the mist's colour (0..255) */
uniform vec4 u_view;  /* s201: a zoomed map - its origin on screen x, y and on the 1x map x, y */
uniform vec4 u_vsc;   /* s201: 1 / the zoom, x and y */
void main()
{
    vec2 tc = gl_TexCoord[0].xy;
    vec2 px = u_view.zw + (tc * u_size.xy - u_view.xy) * u_vsc.xy; /* s201: on the 1x map */
    vec4 c = texture2D(u_map, tc * u_size.zw);
    vec3 rgb = c.rgb * 255.0;

    if (px.x < u_area.x || px.y < u_area.y || px.x >= u_area.z || px.y >= u_area.w) {
        gl_FragColor = c;
        return;
    }
    if (u_dg.w > 0.5 && rgb.r + rgb.g + rgb.b > 0.5) { /* s190: vl_nmap */
        vec4 nn = texture2D(u_nrm, tc * u_size.zw);
        float a = nn.a * 255.0;

        if (a >= 24.0) {
            vec2 cell = floor((px - u_dg.xy) / u_dg.z);

            if (cell.x >= 0.0 && cell.y >= 0.0 && cell.x < u_dgs.x && cell.y < u_dgs.y) {
                vec4 dd = texture2D(u_dirs, (cell + 0.5) / u_dgs.xy * u_dgs.zw);

                if (dd.a > 0.5) {
                    float kw = dd.r, sw = dd.g * 2.0;
                    vec3 n = (nn.rgb * 255.0 - 128.0) / 127.0;
                    float d = kw * (n.x * u_rka.x + n.y * u_rka.y + u_rka.w * (n.z - 1.0) * u_rka.z);
                    vec3 spc = vec3(0.0);

                    if (sw > 0.0) {
                        float rel = 0.0, ws = 0.0;

                        for (int i = 0; i < 24; i++) {
                            if (float(i) >= u_pli.z)
                                break;
                            vec4 A = u_pl[3 * i];
                            vec4 B = u_pl[3 * i + 1];
                            vec4 C = u_pl[3 * i + 2];
                            float dx = A.x - px.x, dy = A.y - px.y;
                            float ma = (dx * u_pli.x + dy * u_pli.y) * 0.5;
                            float mb = (dy * u_pli.y - dx * u_pli.x) * 0.5;
                            float kk = 1.0 - sqrt(ma * ma + mb * mb) / B.x;

                            if (kk > 0.0) {
                                float w = B.y * kk * kk;
                                vec3 l = normalize(vec3(dx, A.z - px.y, A.w));
                                vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
                                float sp = dot(n, h);

                                rel += w * (n.x * l.x + n.y * l.y + u_rka.w * (n.z - 1.0) * l.z);
                                if (sp > 0.0) {
                                    float s16 = sp * sp, f16 = h.z * h.z;

                                    s16 *= s16; s16 *= s16; s16 *= s16;
                                    f16 *= f16; f16 *= f16; f16 *= f16;
                                    if (s16 > f16)
                                        spc += (s16 - f16) * w * vec3(B.z, B.w, C.x);
                                }
                                ws += w;
                            }
                        }
                        if (ws > 0.0) {
                            d += rel * sw / ws;
                            spc *= sw / ws;
                        }
                    }
                    d = clamp(d * u_rkb.x * a / 255.0, -0.5, 0.55);
                    spc = min(spc * (u_rkb.y * a), vec3(40.0));
                    rgb = min(rgb * (1.0 + d) + spc, 255.0);
                }
            }
        }
    }
    if (rgb.r + rgb.g + rgb.b > 0.5) { /* black stays black */
        vec2 g = (px - u_lg.xy) / u_lg.z + 0.5;
        vec4 L = texture2D(u_light, g / u_lgs.xy * u_lgs.zw);

        rgb = mix(rgb, L.rgb * 255.0, L.a);
    }
    if (u_mist.w > 0.0) { /* s192: vl_mist */
        vec2 gf = (px - u_lg.xy) / u_lg.z + 0.5;
        float f = texture2D(u_fog, gf / u_lgs.xy * u_lgs.zw).r;

        if (f > 0.004) {
            vec2 q = px - u_mist.xy;
            float n1 = texture2D(u_noise, vec2(q.x / 230.0 + u_mist.z * 0.045, q.y / 230.0 + u_mist.z * 0.016)).r;
            float n2 = texture2D(u_noise, vec2(q.x / 97.0 - u_mist.z * 0.07, q.y / 97.0 + u_mist.z * 0.035)).r;
            float d = clamp((0.62 * n1 + 0.38 * n2 - 0.38) / 0.42, 0.0, 1.0);

            d = d * d * (3.0 - 2.0 * d);
            rgb = mix(rgb, u_mcol.rgb, f * u_mist.w * d);
        }
    }
    gl_FragColor = vec4(rgb / 255.0, c.a);
}
)VGL";

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
	{ /* s201: the HD textures too (the surfaces' registry stays) */
		std::unordered_map<std::string, SDL_Texture *>::iterator hi;

		if (destroy)
			for (hi = vgm_hd_cache.begin(); hi != vgm_hd_cache.end(); ++hi)
				SDL_DestroyTexture(hi->second);
		vgm_hd_cache.clear();
		vgm_hd_none.clear();
	}
	vgm_tex = vgm_out = vgm_big = vgm_rgb = NULL;
	{ /* s205: the depth-of-field steps */
		int k;

		for (k = 0; k < 3; k++) {
			if (destroy && vgm_dof_t[k])
				SDL_DestroyTexture(vgm_dof_t[k]);
			vgm_dof_t[k] = NULL;
		}
		vgm_dof_w = vgm_dof_h = 0;
	}
	vgm_valid = false;
	vgm_blend = -1;
	if (destroy) { /* s185 */
		if (vgm_nrm)
			SDL_DestroyTexture(vgm_nrm);
		if (vgm_lit)
			SDL_DestroyTexture(vgm_lit);
		if (vgl_light)
			SDL_DestroyTexture(vgl_light);
		if (vgl_dirs)
			SDL_DestroyTexture(vgl_dirs);
		if (vgl_fogt) /* s192 */
			SDL_DestroyTexture(vgl_fogt);
		if (vgl_noiset)
			SDL_DestroyTexture(vgl_noiset);
	}
	vgm_nrm = vgm_lit = vgl_light = vgl_dirs = NULL;
	vgl_fogt = vgl_noiset = NULL; /* s192 */
	vgl_fogfresh = true;
	vgm_lit_valid = false;
	vgl_state = -1, vgl_prog = 0; /* a new renderer is a new GL context */
	vgm_diff_on = -1, vgm_shadow_tex = NULL; /* s194: decided again, everything uploaded again */
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

/* s201: the view - a 1x map point on the zoomed map layer */
static inline float vgm_vx(float x)
{
	return vgm_vpx + (x - vgm_vlx) * vgm_vsx;
}

static inline float vgm_vy(float y)
{
	return vgm_vpy + (y - vgm_vly) * vgm_vsy;
}

static std::string vgm_hd_name(const vgm_hdkey &k)
{
	char b[48];

	snprintf(b, sizeof b, "|%d|%d|%d|%p%c", (int) k.flip, k.rgb, k.alpha, (void *) k.cut, k.cutout ? 'o' : 'i');
	return k.path + b;
}

/* s201: a map surface's HD texture, or NULL (no HD art: draw the 1x one).
   Kept by its file and variant, so a tile the cache freed and loaded again
   finds it still made. */
static SDL_Texture *vgm_hd_texture(SDL_Surface *s)
{
	std::unordered_map<SDL_Surface *, vgm_hdkey>::iterator si = vgm_hd_src.find(s);
	std::unordered_map<std::string, SDL_Texture *>::iterator ci;
	std::string name;
	SDL_Surface *raw = NULL, *c;
	SDL_Texture *t;
	FILE *fp;
	long n;
	int x, y;

	if (si == vgm_hd_src.end())
		return NULL;
	name = vgm_hd_name(si->second);
	ci = vgm_hd_cache.find(name);
	if (ci != vgm_hd_cache.end())
		return ci->second;
	if (vgm_hd_none.count(name))
		return NULL;
	if (vgm_hd_cache.size() > 3000) { /* a ceiling, never reached by one game */
		for (ci = vgm_hd_cache.begin(); ci != vgm_hd_cache.end(); ++ci)
			SDL_DestroyTexture(ci->second);
		vgm_hd_cache.clear();
	}
	if ((fp = fopen(si->second.path.c_str(), "rb")) != NULL) {
		fseek(fp, 0, SEEK_END);
		n = ftell(fp);
		rewind(fp);
		if (n > 0) {
			char *data = new char[n];

			if (fread(data, (size_t) n, 1, fp) == 1)
				raw = vulture_load_surface(data, (unsigned int) n);
			delete[] data;
		}
		fclose(fp);
	}
	c = raw ? SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0) : NULL;
	if (raw)
		SDL_FreeSurface(raw);
	if (!c || c->w != s->w * 2 || c->h != s->h * 2) { /* missing, or not this sprite's double */
		if (c)
			SDL_FreeSurface(c);
		vgm_hd_none[name] = true;
		return NULL;
	}
	if (SDL_LockSurface(c) == 0) {
		const vgm_hdkey &k = si->second;

		for (y = 0; y < c->h; y++) {
			Uint32 *row = (Uint32 *) ((Uint8 *) c->pixels + (size_t) y * c->pitch);

			if (k.flip)
				for (x = 0; x < c->w / 2; x++) {
					Uint32 tmp = row[x];

					row[x] = row[c->w - 1 - x], row[c->w - 1 - x] = tmp;
				}
			if (k.rgb != 256 || k.alpha != 256)
				for (x = 0; x < c->w; x++) {
					Uint32 p = row[x];
					Uint32 A = ((p >> 24) * (Uint32) k.alpha) >> 8;
					Uint32 R = (((p >> 16) & 255) * (Uint32) k.rgb) >> 8;
					Uint32 G = (((p >> 8) & 255) * (Uint32) k.rgb) >> 8;
					Uint32 B = ((p & 255) * (Uint32) k.rgb) >> 8;

					row[x] = (A > 255 ? 255 : A) << 24 | (R > 255 ? 255 : R) << 16
						| (G > 255 ? 255 : G) << 8 | (B > 255 ? 255 : B);
				}
		}
		if (k.cut && k.cut->w * 2 == c->w && k.cut->h * 2 == c->h && SDL_LockSurface(k.cut) == 0) {
			for (y = 0; y < c->h; y++) { /* s203: a part of the art (a door's arch, its panel) */
				Uint32 *row = (Uint32 *) ((Uint8 *) c->pixels + (size_t) y * c->pitch);
				const Uint32 *m = (const Uint32 *) ((const Uint8 *) k.cut->pixels + (size_t) (y / 2) * k.cut->pitch);

				for (x = 0; x < c->w; x++)
					if ((m[x / 2] >> 24) ? k.cutout : !k.cutout)
						row[x] = 0;
			}
			SDL_UnlockSurface(k.cut);
		}
		vgm_dilate(c);
		SDL_UnlockSurface(c);
	}
	t = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, c->w, c->h);
	if (t)
		SDL_UpdateTexture(t, NULL, c->pixels, c->pitch);
	SDL_FreeSurface(c);
	if (t)
		vgm_hd_cache[name] = t;
	else
		vgm_hd_none[name] = true;
	return t;
}

/* s201: vulture_load_tile - a tile's surface and the file it came from;
   alpha: the alpha scale already applied to it (see-through walls) */
void vulture_gpu_hd_register(SDL_Surface *s, const char *file_1x, double alpha)
{
	std::string f = file_1x ? file_1x : "";
	size_t p = f.rfind("tiles/");
	vgm_hdkey k;

	if (p == std::string::npos)
		p = f.rfind("tiles\\");
	if (!s || p == std::string::npos)
		return;
	k.path = f.substr(0, p) + "tiles_hd/" + f.substr(p + 6);
	k.flip = false, k.rgb = 256, k.alpha = (int) (alpha * 256.0 + 0.5);
	k.cut = NULL, k.cutout = false;
	vgm_hd_src[s] = k;
	if (vgm_hd_state < 0) { /* is the HD art installed at all? */
		FILE *fp = fopen(k.path.c_str(), "rb");

		vgm_hd_state = fp ? 1 : 0;
		if (fp)
			fclose(fp);
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "hd art: %s\n",
			vgm_hd_state ? "tiles_hd found - a zoomed map is drawn sharp"
			             : "no tiles_hd - zoom stays pixel-crisp");
	}
}

/* s201: a copy made from a registered surface - mirrored, darkened (rgb of
   256), faded (alpha of 256) - uses its art the same way */
void vulture_gpu_hd_derive(SDL_Surface *copy, SDL_Surface *orig, bool flip, int rgb, int alpha)
{
	std::unordered_map<SDL_Surface *, vgm_hdkey>::iterator it = vgm_hd_src.find(orig);
	vgm_hdkey k;

	if (!copy || it == vgm_hd_src.end())
		return;
	k = it->second;
	k.flip = k.flip != flip;
	k.rgb = k.rgb * rgb / 256;
	k.alpha = k.alpha * alpha / 256;
	vgm_hd_src[copy] = k;
}

/* s203: a copy holding part of a registered surface's art - where the 1x
   mask (ARGB, its size) is opaque, or with outside where it is clear -
   uses that part of its HD art */
void vulture_gpu_hd_derive_cut(SDL_Surface *copy, SDL_Surface *orig, SDL_Surface *mask, bool outside)
{
	std::unordered_map<SDL_Surface *, vgm_hdkey>::iterator it = vgm_hd_src.find(orig);
	vgm_hdkey k;

	if (!copy || !mask || it == vgm_hd_src.end())
		return;
	k = it->second;
	k.cut = mask, k.cutout = outside;
	vgm_hd_src[copy] = k;
}

bool vulture_gpumap_hd_ok(void)
{
	return vgm_hd_state == 1 && !getenv("VULTURE_NO_HD");
}

/* s201: levelwin, before the map pass: the 1x region `src` will fill `dst`
   on screen (NULL: not zoomed, or no HD art - the old blow-up) */
void vulture_gpumap_set_view(const SDL_Rect *src, const SDL_Rect *dst)
{
	vgm_view = src && dst && src->w > 0 && src->h > 0 && dst->w > 0 && dst->h > 0;
	if (vgm_view) {
		vgm_vsx = (float) dst->w / src->w, vgm_vsy = (float) dst->h / src->h;
		vgm_vlx = (float) src->x, vgm_vly = (float) src->y;
		vgm_vpx = (float) dst->x, vgm_vpy = (float) dst->y;
	}
}

/*
 * IMPLEMENTATION_LOG s196: the window is made for OpenGL from the start.
 * Lighting draws with SDL's OpenGL renderer, and SDL puts that only on a
 * window made for it: on any other it hides the window, destroys it and
 * makes a new one (SDL_RecreateWindow).  With lighting on that happened as
 * every game started - for about 0.2 s the game had no window, Windows gave
 * the keyboard to another program, and it doesn't hand it back to a window
 * that appears afterwards - so keys went to whatever was behind and the menus
 * wouldn't open until an alt-tab.  Direct3D draws on such a window as well.
 */
static bool vgpu_gl_allowed(void)
{
	return vulture_opts.gpu && !getenv("VULTURE_GPU_SOFT") && !getenv("SDL_RENDER_DRIVER")
		&& !getenv("VULTURE_NO_GL");
}

/* the OpenGL renderer can go on this window without SDL remaking it */
static bool vgpu_window_gl(void)
{
	return vulture_sdl_window && (SDL_GetWindowFlags(vulture_sdl_window) & SDL_WINDOW_OPENGL);
}

static SDL_Window *vgpu_create_window(int w, int h, Uint32 flags)
{
	SDL_Window *win = NULL;

	if (vgpu_gl_allowed()) {
		win = SDL_CreateWindow("Vulture NetHack", SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED, w, h, flags | SDL_WINDOW_OPENGL);
		if (!win) /* no OpenGL here: a plain window, lighting in software */
			vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
				"gpu: no window for OpenGL (%s) - lighting will run in software\n", SDL_GetError());
	}
	if (!win)
		win = SDL_CreateWindow("Vulture NetHack", SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED, w, h, flags);
	if (win)
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "gpu: window %dx%d%s\n", w, h,
			(SDL_GetWindowFlags(win) & SDL_WINDOW_OPENGL) ? ", made for OpenGL" : "");
	return win;
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
		/* s185: OpenGL while lit - its textures can be lent to the lighting
		   shader; SDL's choice otherwise (Direct3D presents ~6 ms faster) */
		bool gl = vgpu_gl_wanted && vgpu_gl_allowed() && vgpu_window_gl(); /* s196 */

		if (gl)
			SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
		vulture_gpu = SDL_CreateRenderer(vulture_sdl_window, -1, fl);
		if (gl) {
			SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
			if (!vulture_gpu) /* no OpenGL: whatever SDL picks */
				vulture_gpu = SDL_CreateRenderer(vulture_sdl_window, -1, fl);
		}
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

/*
 * IMPLEMENTATION_LOG s194: only the changed parts of the overlay go up.  A
 * map frame clears the overlay and draws it again, so every present sent all
 * 8 MB of it (1080p) - 2.6 ms a frame on the OpenGL renderer (lighting on),
 * though most of it - the clear map area, the HUD - comes out the same.  On
 * OpenGL the overlay as last uploaded is kept (vgm_shadow) and the frame is
 * compared with it in 64 x 16 blocks: runs of changed blocks are uploaded
 * (and copied into the copy), the rest is left.  Direct3D uploads the whole
 * frame faster than comparing it, so it keeps doing that.
 */

/* the changed rectangles of r (render space; NULL = all) - the copy updated */
static void vgm_diff(SDL_Surface *s, const SDL_Rect *r, std::vector<SDL_Rect> &out)
{
	const int BW = 64, BH = 16;
	SDL_Rect c;
	int bx, by, y;

	out.clear();
	if (r && (r->w <= 0 || r->h <= 0))
		return;
	c = r ? *r : SDL_Rect { 0, 0, s->w, s->h };
	if (c.x < 0) c.w += c.x, c.x = 0;
	if (c.y < 0) c.h += c.y, c.y = 0;
	if (c.x + c.w > s->w) c.w = s->w - c.x;
	if (c.y + c.h > s->h) c.h = s->h - c.y;
	if (c.w <= 0 || c.h <= 0)
		return;
	if (vgm_shadow_w != s->w || vgm_shadow_h != s->h || vgm_shadow_tex != vulture_gpu_tex) {
		/* a new texture or size: everything goes up, and is remembered */
		vgm_shadow.assign((size_t) s->w * s->h, 0u);
		for (y = 0; y < s->h; y++)
			memcpy(&vgm_shadow[(size_t) y * s->w], (Uint8 *) s->pixels + (size_t) y * s->pitch,
				(size_t) s->w * 4);
		vgm_shadow_w = s->w, vgm_shadow_h = s->h, vgm_shadow_tex = vulture_gpu_tex;
		out.push_back(SDL_Rect { 0, 0, s->w, s->h });
		return;
	}
	for (by = c.y; by < c.y + c.h; by += BH) {
		int bh = c.y + c.h - by < BH ? c.y + c.h - by : BH, run0 = -1, run1 = -1, bw;

		/* s197: each step is the block's own width, so the one after the last
		   block lands on the end - the pass that sends a run still open.  A
		   step of 64 jumped past the end whenever the width wasn't a multiple
		   of 64 (a menu, a tooltip - any refresh but the whole 1920 screen),
		   and a run reaching the right edge, all of a menu just drawn, never
		   went up: the screen stayed as it was until a full present. */
		for (bx = c.x; ; bx += bw) {
			bool last = bx >= c.x + c.w, changed = false;

			bw = last ? 0 : (c.x + c.w - bx < BW ? c.x + c.w - bx : BW);
			for (y = by; !last && y < by + bh && !changed; y++)
				changed = memcmp((Uint8 *) s->pixels + (size_t) y * s->pitch + (size_t) bx * 4,
					&vgm_shadow[(size_t) y * s->w + bx], (size_t) bw * 4) != 0;
			if (changed) {
				if (run0 < 0)
					run0 = bx;
				run1 = bx + bw;
			} else if (run0 >= 0) { /* a run ends: upload it, remember it */
				for (y = by; y < by + bh; y++)
					memcpy(&vgm_shadow[(size_t) y * s->w + run0],
						(Uint8 *) s->pixels + (size_t) y * s->pitch + (size_t) run0 * 4,
						(size_t) (run1 - run0) * 4);
				out.push_back(SDL_Rect { run0, by, run1 - run0, bh });
				run0 = -1;
			}
			if (last)
				break;
		}
	}
}

/* s183: the overlay (and, for the two-pass composite, its no-alpha twin) */
static void vgm_upload_overlay(const SDL_Rect *r)
{
	SDL_Surface *s = vulture_screen;

	if (vgm_diff_on < 0) { /* s194: compare frames on OpenGL only */
		SDL_RendererInfo ri;

		vgm_diff_on = SDL_GetRendererInfo(vulture_gpu, &ri) == 0 && !strcmp(ri.name, "opengl")
			&& !getenv("VULTURE_NO_DIFFUP") ? 1 : 0;
	}
	if (vgm_diff_on == 1 && s && s->format->BytesPerPixel == 4) {
		std::vector<SDL_Rect> rs;
		size_t k;

		if (SDL_MUSTLOCK(s))
			SDL_LockSurface(s);
		vgm_diff(s, r, rs);
		if (SDL_MUSTLOCK(s))
			SDL_UnlockSurface(s);
		vgm_diff_up = 0;
		for (k = 0; k < rs.size(); k++) {
			vgm_upload(vulture_gpu_tex, s, &rs[k]);
			vgm_diff_up += (unsigned) (rs[k].w * rs[k].h);
		}
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
			for (k = 0; k < rs.size(); k++)
				vgm_upload(vgm_rgb, s, &rs[k]);
		return;
	}
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

/* s205: depth of field.  Close over a diorama a camera keeps only a band in
   focus: the map layer far above and below you softens.  The composed map is
   halved three times (each step a 2x2 average) and laid back, upscaled, in
   thin strips that fade in away from the focus.  The overlay - effects,
   text, the HUD - goes on after it, sharp. */
static void vgm_dof_pass(void)
{
	static int off = -1, band_pct = 24; /* probe: VULTURE_DOF_BAND (percent) narrows the focus */
	SDL_Texture *prev = vgm_out;
	int W = 0, H = 0, k, fy, band, ramp, y;

	if (off < 0) {
		const char *e = getenv("VULTURE_DOF_BAND");

		off = getenv("VULTURE_NO_DOF") ? 1 : 0;
		if (e && atoi(e) > 0 && atoi(e) < 50)
			band_pct = atoi(e);
	}
	if (off || !vulture_opts.depth_blur || !vgm_out)
		return;
	SDL_QueryTexture(vgm_out, NULL, NULL, &W, &H);
	if (W < 128 || H < 128)
		return;
	if (vgm_dof_w != W || vgm_dof_h != H) {
		for (k = 0; k < 3; k++) {
			if (vgm_dof_t[k])
				SDL_DestroyTexture(vgm_dof_t[k]);
			vgm_dof_t[k] = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
			                                 W >> (k + 1), H >> (k + 1));
		}
		vgm_dof_w = W, vgm_dof_h = H;
	}
	for (k = 0; k < 3; k++)
		if (!vgm_dof_t[k])
			return;
	for (k = 0; k < 3; k++) { /* down: each step averages 2x2 */
		SDL_SetRenderTarget(vulture_gpu, vgm_dof_t[k]);
		SDL_SetTextureBlendMode(prev, SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(prev, SDL_ScaleModeLinear);
		SDL_RenderCopy(vulture_gpu, prev, NULL, NULL);
		prev = vgm_dof_t[k];
	}
	SDL_SetRenderTarget(vulture_gpu, vgm_out);
	SDL_RenderSetClipRect(vulture_gpu, NULL);
	fy = vgm_focus_y >= 0 ? vgm_focus_y : H / 2;
	if (vgm_focus_y >= 0 && vgm_tex_view) /* s201: drawn at the zoom */
		fy = (int) vgm_vy((float) fy);
	else if (vgm_focus_y >= 0 && vgm_zoom && vgm_zsrc.h > 0)
		fy = vgm_zdst.y + (fy - vgm_zsrc.y) * vgm_zdst.h / vgm_zsrc.h;
	band = H * band_pct / 100, ramp = H * 20 / 100;
	SDL_SetTextureBlendMode(prev, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(prev, SDL_ScaleModeLinear);
	SDL_SetTextureColorMod(prev, 255, 255, 255);
	for (y = 0; y + 8 <= H; y += 8) { /* up, in strips fading in away from the focus */
		int d = abs(y + 4 - fy);
		double a = d <= band ? 0.0 : d >= band + ramp ? 1.0 : (double) (d - band) / ramp;
		SDL_Rect sr, dr;

		if (a <= 0.0)
			continue;
		a = 0.8 * a * a * (3.0 - 2.0 * a);
		sr.x = 0, sr.y = y >> 3, sr.w = W >> 3, sr.h = 1;
		dr.x = 0, dr.y = y, dr.w = W, dr.h = 8;
		SDL_SetTextureAlphaMod(prev, (Uint8) (a * 255.0 + 0.5));
		SDL_RenderCopy(vulture_gpu, prev, &sr, &dr);
	}
	SDL_SetTextureAlphaMod(prev, 255);
}

/* s205: levelwin, each frame: where you are on the map layer (1x) */
void vulture_gpumap_set_focus(int y)
{
	vgm_focus_y = y;
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
	SDL_Texture *mapt = (vgm_lit_valid && vgm_lit) ? vgm_lit : vgm_tex; /* s185 */

	SDL_SetTextureBlendMode(mapt, SDL_BLENDMODE_NONE);
	SDL_SetTextureColorMod(mapt, 255, 255, 255);
	SDL_SetTextureAlphaMod(mapt, 255);
	SDL_SetTextureScaleMode(mapt, SDL_ScaleModeNearest);
	if (!vgm_zoom || vgm_tex_view) { /* s201: a map drawn at the zoom goes on 1:1 */
		SDL_RenderCopy(vulture_gpu, mapt, NULL, NULL);
	} else if (vgm_zexact || vgm_zk <= 1) {
		SDL_RenderCopy(vulture_gpu, mapt, &vgm_zsrc, &vgm_zdst);
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
			SDL_RenderCopy(vulture_gpu, mapt, &vgm_zsrc, &big);
			SDL_SetRenderTarget(vulture_gpu, vgm_out);
			SDL_SetTextureBlendMode(vgm_big, SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(vgm_big, SDL_ScaleModeLinear);
			SDL_RenderCopy(vulture_gpu, vgm_big, &big, &vgm_zdst);
		} else
			SDL_RenderCopy(vulture_gpu, vgm_tex, &vgm_zsrc, &vgm_zdst);
	}
	vgm_dof_pass(); /* s205: the far map softens, under the sharp overlay */
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
	Uint64 pc1 = SDL_GetPerformanceCounter(); /* probe */
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
	Uint64 pc2 = SDL_GetPerformanceCounter(); /* probe */
	SDL_RenderPresent(vulture_gpu);
	if (getenv("VULTURE_SELFTEST")) {
		Uint64 pc3 = SDL_GetPerformanceCounter(), pf = SDL_GetPerformanceFrequency();

		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"present %u us (%s, %dx%d) upload %u draw %u swap %u sent %u px\n",
			(unsigned) ((pc3 - pc0) * 1000000 / pf), vgm_live() ? "gpumap" : "software",
			r ? r->w : s->w, r ? r->h : s->h, (unsigned) ((pc1 - pc0) * 1000000 / pf),
			(unsigned) ((pc2 - pc1) * 1000000 / pf), (unsigned) ((pc3 - pc2) * 1000000 / pf),
			vgm_diff_on == 1 ? vgm_diff_up : (unsigned) ((r ? r->w : s->w) * (r ? r->h : s->h)));
	}
	if (vgm_diff_on == 1 && getenv("VULTURE_SELFTEST_DIFFUP") && s->format->BytesPerPixel == 4
		&& vgm_shadow_w == s->w && vgm_shadow_h == s->h) { /* probe: s197 */
		SDL_Rect c = r ? *r : SDL_Rect { 0, 0, s->w, s->h };
		unsigned miss = 0;
		int x, y;

		if (c.x < 0) c.w += c.x, c.x = 0;
		if (c.y < 0) c.h += c.y, c.y = 0;
		if (c.x + c.w > s->w) c.w = s->w - c.x;
		if (c.y + c.h > s->h) c.h = s->h - c.y;
		for (y = c.y; y < c.y + c.h; y++)
			for (x = c.x; x < c.x + c.w; x++)
				miss += ((Uint32 *) ((Uint8 *) s->pixels + (size_t) y * s->pitch))[x]
					!= vgm_shadow[(size_t) y * s->w + x];
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"diffup check: %dx%d at %d,%d - %u px of it not on the GPU\n", c.w, c.h, c.x, c.y, miss);
	}
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
	{
		SDL_Rect c = clip ? *clip : all;

		vgm_tex_view = vgm_view; /* s201 */
		if (vgm_view) { /* the redrawn region, as the zoomed map has it */
			int x0 = (int) floorf(vgm_vx((float) c.x)), y0 = (int) floorf(vgm_vy((float) c.y));
			int x1 = (int) ceilf(vgm_vx((float) (c.x + c.w)));
			int y1 = (int) ceilf(vgm_vy((float) (c.y + c.h)));

			x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
			x1 = x1 > all.w ? all.w : x1, y1 = y1 > all.h ? all.h : y1;
			c.x = x0, c.y = y0, c.w = x1 > x0 ? x1 - x0 : 0, c.h = y1 > y0 ? y1 - y0 : 0;
		}
		SDL_RenderSetClipRect(vulture_gpu, &c);
		SDL_SetRenderDrawBlendMode(vulture_gpu, SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(vulture_gpu, 0, 0, 0, 255);
		SDL_RenderFillRect(vulture_gpu, &c);
		vgm_clip = c; /* s185 */
	}
	vulture_gpumap_on = true;
	vgm_nlist.clear();
	vgm_lit_valid = false;
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
	vgm_lit_valid = false; /* s185 */
	vgm_tex_view = false;  /* s201 */
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
	if (vgm_view) { /* s201: drawn at the zoom, from the HD art if there is some */
		SDL_Texture *ht = vgm_hd_texture(s);
		SDL_FRect fd;
		SDL_Rect sr;
		const SDL_Rect *sp = srcrect;

		if (ht) {
			SDL_SetTextureBlendMode(ht, bm);
			SDL_SetTextureColorMod(ht, r, g, b);
			SDL_SetTextureAlphaMod(ht, a);
			t = ht;
			if (srcrect) {
				sr.x = srcrect->x * 2, sr.y = srcrect->y * 2;
				sr.w = srcrect->w * 2, sr.h = srcrect->h * 2;
				sp = &sr;
			}
		}
		SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
		fd.x = vgm_vx((float) x), fd.y = vgm_vy((float) y);
		fd.w = (float) (srcrect ? srcrect->w : s->w) * vgm_vsx;
		fd.h = (float) (srcrect ? srcrect->h : s->h) * vgm_vsy;
		SDL_RenderCopyF(vulture_gpu, t, sp, &fd);
		return;
	}
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
	if (vgm_view) { /* s201 */
		SDL_Texture *ht = vgm_hd_texture(s);

		if (ht)
			t = ht;
		d.x = vgm_vx(d.x), d.y = vgm_vy(d.y), d.w *= vgm_vsx, d.h *= vgm_vsy;
		c.x = d.w * 0.5f, c.y = d.h;
	}
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureColorMod(t, 255, red ? 77 : 255, red ? 77 : 255);
	SDL_SetTextureAlphaMod(t, (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
	SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
	SDL_RenderCopyExF(vulture_gpu, t, NULL, &d, deg, &c, SDL_FLIP_NONE);
}

/* s191: a sprite's silhouette laid on the map layer as a cast shadow: quad -
   the screen corners of the image's top-left, top-right, bottom-right and
   bottom-left; a4 - their alphas (black, faded by these) */
bool vulture_gpumap_shadow(SDL_Surface *s, const float *quad, const Uint8 *a4)
{
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
	SDL_Vertex v[4];
	SDL_Texture *t;
	int k;

	if (!vulture_gpumap_on || !s || !(t = vgm_texture(s)))
		return false;
	for (k = 0; k < 4; k++) {
		v[k].position.x = vgm_view ? vgm_vx(quad[2 * k]) : quad[2 * k]; /* s201 */
		v[k].position.y = vgm_view ? vgm_vy(quad[2 * k + 1]) : quad[2 * k + 1];
		v[k].color.r = v[k].color.g = v[k].color.b = 0, v[k].color.a = a4[k];
		v[k].tex_coord.x = (k == 1 || k == 2) ? 1.0f : 0.0f;
		v[k].tex_coord.y = k >= 2 ? 1.0f : 0.0f;
	}
	if (vgm_view) { /* s201 */
		SDL_Texture *ht = vgm_hd_texture(s);

		if (ht)
			t = ht;
	}
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureColorMod(t, 255, 255, 255);
	SDL_SetTextureAlphaMod(t, 255);
	SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
	return SDL_RenderGeometry(vulture_gpu, t, v, 4, idx, 6) == 0;
}

/* s203: src of a surface (NULL: all of it) laid on the map layer as a quad
   - quad: the screen corners of src's top-left, top-right, bottom-right
   and bottom-left (a door leaf turning on its hinge, a body falling over) -
   coloured (r, g, b of 255) and faded */
bool vulture_gpumap_quad(SDL_Surface *s, const SDL_Rect *src, const float *quad, int r, int g, int b,
                         int alpha)
{
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
	SDL_Vertex v[4];
	SDL_Texture *t;
	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	Uint8 cr = (Uint8) (r < 0 ? 0 : r > 255 ? 255 : r), cg = (Uint8) (g < 0 ? 0 : g > 255 ? 255 : g);
	Uint8 cb = (Uint8) (b < 0 ? 0 : b > 255 ? 255 : b);
	Uint8 al = (Uint8) (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
	int k;

	if (!vulture_gpumap_on || !s || !(t = vgm_texture(s)))
		return false;
	if (vgm_view) { /* s201: the HD art, if there is some */
		SDL_Texture *ht = vgm_hd_texture(s);

		if (ht)
			t = ht;
	}
	if (src && s->w > 0 && s->h > 0) {
		u0 = (float) src->x / s->w, v0 = (float) src->y / s->h;
		u1 = (float) (src->x + src->w) / s->w, v1 = (float) (src->y + src->h) / s->h;
	}
	for (k = 0; k < 4; k++) {
		v[k].position.x = vgm_view ? vgm_vx(quad[2 * k]) : quad[2 * k];
		v[k].position.y = vgm_view ? vgm_vy(quad[2 * k + 1]) : quad[2 * k + 1];
		v[k].color.r = cr, v[k].color.g = cg, v[k].color.b = cb, v[k].color.a = al;
		v[k].tex_coord.x = (k == 1 || k == 2) ? u1 : u0;
		v[k].tex_coord.y = k >= 2 ? v1 : v0;
	}
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureColorMod(t, 255, 255, 255);
	SDL_SetTextureAlphaMod(t, 255);
	SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
	return SDL_RenderGeometry(vulture_gpu, t, v, 4, idx, 6) == 0;
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

	if (s) /* s201: the address may come back as another surface */
		vgm_hd_src.erase(s);

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

/* s185: a GL shader */
static vgl_uint vgl_shader(vgl_enum type, const char *src)
{
	vgl_uint sh = vgl.CreateShader(type);
	vgl_int ok = 0;
	char lg[600];

	vgl.ShaderSource(sh, 1, &src, NULL);
	vgl.CompileShader(sh);
	vgl.GetShaderiv(sh, VGL_COMPILE_STATUS, &ok);
	if (!ok) {
		lg[0] = 0;
		vgl.GetShaderInfoLog(sh, (vgl_sizei) sizeof lg, NULL, lg);
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpulit: a shader didn't compile - lighting stays in software: %s\n", lg);
		return 0;
	}
	return sh;
}

/* s185: can the GPU light the map? (the OpenGL renderer and the shader) */
bool vulture_gpulit_ok(void)
{
	SDL_RendererInfo ri;
	vgl_uint vs, fsh;
	vgl_int ok = 0;
	static const char *const un[VGL_U_N] = { "u_map", "u_nrm", "u_light", "u_dirs", "u_size",
		"u_lg", "u_lgs", "u_dg", "u_dgs", "u_area", "u_pl[0]", "u_pli", "u_rka", "u_rkb",
		"u_noise", "u_fog", "u_mist", "u_mcol", "u_view", "u_vsc" };
	int k;

	if (vgl_state >= 0)
		return vgl_state == 1 && vulture_gpu;
	if (!vulture_gpu || getenv("VULTURE_NO_GPULIT"))
		return false;
	vgl_state = 0;
	if (SDL_GetRendererInfo(vulture_gpu, &ri) != 0 || strcmp(ri.name, "opengl") != 0) {
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpulit: the renderer isn't OpenGL - lighting stays in software\n");
		return false;
	}
	SDL_RenderFlush(vulture_gpu); /* its GL context is the current one */
#define VGL_GET(f)                                                                    \
	if (!(*(void **) &vgl.f = SDL_GL_GetProcAddress("gl" #f))) {                      \
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,                             \
			"gpulit: no gl%s - lighting stays in software\n", #f);                    \
		return false;                                                                 \
	}
	VGL_GET(CreateShader) VGL_GET(ShaderSource) VGL_GET(CompileShader) VGL_GET(GetShaderiv)
	VGL_GET(GetShaderInfoLog) VGL_GET(CreateProgram) VGL_GET(AttachShader) VGL_GET(LinkProgram)
	VGL_GET(GetProgramiv) VGL_GET(GetProgramInfoLog) VGL_GET(UseProgram)
	VGL_GET(GetUniformLocation) VGL_GET(Uniform1i) VGL_GET(Uniform4f) VGL_GET(Uniform4fv)
	VGL_GET(ActiveTexture)
	VGL_GET(GetIntegerv) VGL_GET(PushAttrib) VGL_GET(PopAttrib) VGL_GET(Disable)
	VGL_GET(TexParameteri) VGL_GET(Begin) VGL_GET(End) VGL_GET(TexCoord2f) VGL_GET(Vertex2f)
#undef VGL_GET
	vs = vgl_shader(VGL_VERTEX_SHADER, vgl_vs);
	fsh = vgl_shader(VGL_FRAGMENT_SHADER, vgl_fs);
	if (!vs || !fsh)
		return false;
	vgl_prog = vgl.CreateProgram();
	vgl.AttachShader(vgl_prog, vs);
	vgl.AttachShader(vgl_prog, fsh);
	vgl.LinkProgram(vgl_prog);
	vgl.GetProgramiv(vgl_prog, VGL_LINK_STATUS, &ok);
	if (!ok) {
		char lg[600];

		lg[0] = 0;
		vgl.GetProgramInfoLog(vgl_prog, (vgl_sizei) sizeof lg, NULL, lg);
		vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
			"gpulit: the shader didn't link - lighting stays in software: %s\n", lg);
		return false;
	}
	for (k = 0; k < VGL_U_N; k++)
		vgl_loc[k] = vgl.GetUniformLocation(vgl_prog, un[k]);
	vgl_state = 1;
	vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
		"gpulit: dynamic lighting runs on the GPU (%s)\n", ri.name);
	return true;
}

/* s185: OpenGL while the map is lit, SDL's default renderer otherwise.  A
   change makes a new renderer: every GPU texture is remade lazily and the
   level redrawn; vulture_screen (the overlay / software frame) is kept. */
void vulture_gpu_use_gl(bool on)
{
	SDL_RendererInfo ri;
	bool is_gl;

	if (on == vgpu_gl_wanted)
		return;
	vgpu_gl_wanted = on;
	if (!vulture_gpu || getenv("VULTURE_GPU_SOFT") || getenv("SDL_RENDER_DRIVER")
	    || getenv("VULTURE_NO_GL"))
		return;
	if (on && !vgpu_window_gl()) { /* s196: SDL would remake the window (and lose the keys) */
		static bool told = false;

		if (!told)
			vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
				"gpu: the window isn't made for OpenGL - lighting stays in software\n");
		told = true;
		return;
	}
	is_gl = SDL_GetRendererInfo(vulture_gpu, &ri) == 0 && !strcmp(ri.name, "opengl");
	if (is_gl == on)
		return;
	vgm_drop(true);
	if (vulture_gpu_tex)
		SDL_DestroyTexture(vulture_gpu_tex);
	vulture_gpu_tex = NULL;
	SDL_DestroyRenderer(vulture_gpu);
	vulture_gpu = NULL;
	vgpu_reattach(vulture_render_scale);
	vulture_gpumap_lost = true;
	vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__, "gpu: renderer remade for %s\n",
		on ? "lighting (OpenGL)" : "no lighting");
}

/* s185: a normal-map draw during the map pass, replayed into the normal layer */
void vulture_gpumap_normal(SDL_Surface *s, int x, int y)
{
	vgm_nrec r;

	if (!vulture_gpumap_on || !s)
		return;
	r.s = s, r.x = x, r.y = y, r.ex = false, r.sx = r.sy = 1.0, r.deg = 0.0, r.alpha = 255;
	r.quad = false;
	vgm_nlist.push_back(r);
}

void vulture_gpumap_normal_ex(SDL_Surface *s, int dx, int dy, double sx, double sy, double deg,
                              int alpha)
{
	vgm_nrec r;

	if (!vulture_gpumap_on || !s)
		return;
	r.s = s, r.x = dx, r.y = dy, r.ex = true, r.sx = sx, r.sy = sy, r.deg = deg, r.alpha = alpha;
	r.quad = false;
	vgm_nlist.push_back(r);
}

/* s204: a normal map laid as vulture_gpumap_quad lays its colour - the
   pieces that turn and fall get their own relief, not the floor's under them */
void vulture_gpumap_normal_quad(SDL_Surface *s, const SDL_Rect *src, const float *quad, int alpha)
{
	vgm_nrec r;
	int k;

	if (!vulture_gpumap_on || !s || !quad)
		return;
	r.s = s, r.x = r.y = 0, r.ex = false, r.sx = r.sy = 1.0, r.deg = 0.0, r.alpha = alpha;
	r.quad = true;
	for (k = 0; k < 8; k++)
		r.q[k] = quad[k];
	if (src)
		r.src = *src;
	else
		r.src.x = r.src.y = 0, r.src.w = s->w, r.src.h = s->h;
	vgm_nlist.push_back(r);
}

/* s185: (re)make a texture at w x h; 0 failed, 1 kept, 2 new */
static int vgl_texture(SDL_Texture **t, int w, int h, int access)
{
	int tw = 0, th = 0;

	if (*t) {
		SDL_QueryTexture(*t, NULL, NULL, &tw, &th);
		if (tw == w && th == h)
			return 1;
		SDL_DestroyTexture(*t);
	}
	*t = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888, access, w, h);
	return *t ? 2 : 0;
}

/*
 * s185: light this frame's map on the GPU - vl_render calls this where it
 * would run vl_relief + vl_apply, with the same grids and origins: the light
 * grid `low` (sample i at x = lox + i*FS), the per-cell directions dx/dy/lv
 * (dw x dh cells from gx0, gy0), relief on or off, `fresh` when the grids
 * were rebuilt (upload them), `area` the map's rectangle.
 */
/* s192: the living fog's inputs for the next frame */
void vulture_gpulit_mist(SDL_Surface *fog, bool fresh, const Uint8 *noise, const float *mist)
{
	vgl_fogsrc = fog, vgl_noisesrc = noise;
	vgl_fogfresh = vgl_fogfresh || fresh;
	if (mist)
		memcpy(vgl_mistp, mist, sizeof vgl_mistp);
	else
		memset(vgl_mistp, 0, sizeof vgl_mistp);
}

void vulture_gpulit_frame(SDL_Surface *low, int lox, int loy, int FS, const short *kw,
                          const short *sw, int dw, int dh, int gx0, int gy0, const float *pl,
                          int npl, const float *rk, float ixm, float iym, bool relief, bool fresh,
                          const SDL_Rect *area)
{
	int W, Hh, mk;
	float tw = 1.0f, th = 1.0f, ltw = 1.0f, lth = 1.0f, dtw = 1.0f, dth = 1.0f;
	float ntw = 1.0f, nth = 1.0f, ftw = 1.0f, fth = 1.0f; /* s192 */
	bool mist; /* s192 */
	size_t i;

	vgm_lit_valid = false;
	if (!vgm_live() || !low || FS <= 0 || !vulture_gpulit_ok())
		return;
	W = vulture_screen->w, Hh = vulture_screen->h;
	if (!vgl_texture(&vgm_lit, W, Hh, SDL_TEXTUREACCESS_TARGET))
		return;
	if (!kw || !sw || !rk || dw <= 0 || dh <= 0)
		relief = false;
	if (relief && !vgl_texture(&vgm_nrm, W, Hh, SDL_TEXTUREACCESS_TARGET))
		relief = false;
	/* the light grid */
	mk = vgl_texture(&vgl_light, low->w, low->h, SDL_TEXTUREACCESS_STREAMING);
	if (!mk)
		return;
	if (fresh || mk == 2) {
		if (SDL_MUSTLOCK(low))
			SDL_LockSurface(low);
		SDL_UpdateTexture(vgl_light, NULL, low->pixels, low->pitch);
		if (SDL_MUSTLOCK(low))
			SDL_UnlockSurface(low);
	}
	/* s190: per cell: R the key's weight, G the light from sources / 2, A on */
	if (relief) {
		mk = vgl_texture(&vgl_dirs, dw, dh, SDL_TEXTUREACCESS_STREAMING);
		if (!mk)
			relief = false;
		else if (fresh || mk == 2) {
			std::vector<Uint32> px((size_t) dw * dh);

			for (i = 0; i < px.size(); i++) {
				int r = kw[i] * 255 / 256, g = sw[i] * 255 / 512;

				r = r < 0 ? 0 : r > 255 ? 255 : r;
				g = g < 0 ? 0 : g > 255 ? 255 : g;
				px[i] = ((kw[i] || sw[i]) ? 0xFF000000u : 0u) | ((Uint32) r << 16)
					| ((Uint32) g << 8);
			}
			SDL_UpdateTexture(vgl_dirs, NULL, &px[0], dw * 4);
		}
	}
	/* s192: the fog grid (when rebuilt) and the noise (once) */
	mist = vgl_fogsrc && vgl_noisesrc && vgl_mistp[3] > 0.0f;
	if (mist) {
		int mk2 = vgl_texture(&vgl_fogt, vgl_fogsrc->w, vgl_fogsrc->h, SDL_TEXTUREACCESS_STREAMING);

		if (!mk2)
			mist = false;
		else if (vgl_fogfresh || fresh || mk2 == 2) {
			if (SDL_MUSTLOCK(vgl_fogsrc))
				SDL_LockSurface(vgl_fogsrc);
			SDL_UpdateTexture(vgl_fogt, NULL, vgl_fogsrc->pixels, vgl_fogsrc->pitch);
			if (SDL_MUSTLOCK(vgl_fogsrc))
				SDL_UnlockSurface(vgl_fogsrc);
			vgl_fogfresh = false;
		}
		if (mist && !vgl_noiset
		    && (vgl_noiset = SDL_CreateTexture(vulture_gpu, SDL_PIXELFORMAT_ARGB8888,
		                                       SDL_TEXTUREACCESS_STATIC, 256, 256)) != NULL) {
			std::vector<Uint32> px(65536);

			for (i = 0; i < px.size(); i++)
				px[i] = 0xFF000000u | ((Uint32) vgl_noisesrc[i] << 16)
					| ((Uint32) vgl_noisesrc[i] << 8) | (Uint32) vgl_noisesrc[i];
			SDL_UpdateTexture(vgl_noiset, NULL, &px[0], 256 * 4);
		}
		if (!vgl_noiset)
			mist = false;
	}
	/* the normal layer: this frame's normal-map draws, as the map pass made them */
	if (relief) {
		SDL_SetRenderTarget(vulture_gpu, vgm_nrm);
		SDL_RenderSetClipRect(vulture_gpu, &vgm_clip);
		SDL_SetRenderDrawBlendMode(vulture_gpu, SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(vulture_gpu, 0, 0, 0, 0);
		SDL_RenderFillRect(vulture_gpu, &vgm_clip);
		for (i = 0; i < vgm_nlist.size(); i++) {
			const vgm_nrec &r = vgm_nlist[i];
			SDL_Texture *t = vgm_texture(r.s);
			SDL_BlendMode bm = SDL_BLENDMODE_BLEND;

			if (!t)
				continue;
			SDL_GetSurfaceBlendMode(r.s, &bm);
			SDL_SetTextureBlendMode(t, bm);
			SDL_SetTextureColorMod(t, 255, 255, 255);
			if (r.quad && r.s->w > 0 && r.s->h > 0) { /* s204: as vulture_gpumap_quad laid it */
				static const int qidx[6] = { 0, 1, 2, 0, 2, 3 };
				SDL_Vertex v[4];
				float u0 = (float) r.src.x / r.s->w, v0 = (float) r.src.y / r.s->h;
				float u1 = (float) (r.src.x + r.src.w) / r.s->w, v1 = (float) (r.src.y + r.src.h) / r.s->h;
				int k;

				for (k = 0; k < 4; k++) {
					v[k].position.x = vgm_tex_view ? vgm_vx(r.q[2 * k]) : r.q[2 * k];
					v[k].position.y = vgm_tex_view ? vgm_vy(r.q[2 * k + 1]) : r.q[2 * k + 1];
					v[k].color.r = v[k].color.g = v[k].color.b = 255;
					v[k].color.a = (Uint8) (r.alpha < 0 ? 0 : r.alpha > 255 ? 255 : r.alpha);
					v[k].tex_coord.x = (k == 1 || k == 2) ? u1 : u0;
					v[k].tex_coord.y = k >= 2 ? v1 : v0;
				}
				SDL_SetTextureAlphaMod(t, 255);
				SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
				SDL_RenderGeometry(vulture_gpu, t, v, 4, qidx, 6);
				continue;
			}
			if (!r.ex && vgm_tex_view) { /* s201: at the zoom, as the map pass drew it */
				SDL_FRect d = { vgm_vx((float) r.x), vgm_vy((float) r.y), r.s->w * vgm_vsx,
				                r.s->h * vgm_vsy };

				SDL_SetTextureAlphaMod(t, 255);
				SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
				SDL_RenderCopyF(vulture_gpu, t, NULL, &d);
			} else if (!r.ex) {
				SDL_Rect d = { r.x, r.y, r.s->w, r.s->h };

				SDL_SetTextureAlphaMod(t, 255);
				SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
				SDL_RenderCopy(vulture_gpu, t, NULL, &d);
			} else if (r.sx > 0.0 && r.sy > 0.0) { /* vulture_gpumap_blit_ex's pose */
				SDL_FRect d;
				SDL_FPoint c;

				d.w = (float) (r.s->w * r.sx), d.h = (float) (r.s->h * r.sy);
				d.x = (float) (r.x + r.s->w * 0.5 * (1.0 - r.sx));
				d.y = (float) (r.y + r.s->h * (1.0 - r.sy));
				c.x = d.w * 0.5f, c.y = d.h;
				if (vgm_tex_view) { /* s201 */
					d.x = vgm_vx(d.x), d.y = vgm_vy(d.y), d.w *= vgm_vsx, d.h *= vgm_vsy;
					c.x = d.w * 0.5f, c.y = d.h;
				}
				SDL_SetTextureAlphaMod(t, (Uint8) (r.alpha < 0 ? 0 : r.alpha > 255 ? 255 : r.alpha));
				SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
				SDL_RenderCopyExF(vulture_gpu, t, NULL, &d, r.deg, &c, SDL_FLIP_NONE);
			}
		}
		SDL_RenderSetClipRect(vulture_gpu, NULL);
	}
	/* the shader pass: map (+ normals) + light -> vgm_lit */
	SDL_SetRenderTarget(vulture_gpu, vgm_lit);
	SDL_RenderSetClipRect(vulture_gpu, NULL);
	SDL_RenderFlush(vulture_gpu);
	{
		vgl_int prev = 0;

		vgl.GetIntegerv(VGL_CURRENT_PROGRAM, &prev);
		vgl.PushAttrib(VGL_ALL_ATTRIB_BITS);
		vgl.UseProgram(vgl_prog);
		vgl.ActiveTexture(VGL_TEXTURE0);
		SDL_GL_BindTexture(vgm_tex, &tw, &th);
		vgl.ActiveTexture(VGL_TEXTURE0 + 1);
		SDL_GL_BindTexture(relief ? vgm_nrm : vgm_tex, NULL, NULL);
		vgl.ActiveTexture(VGL_TEXTURE0 + 2);
		SDL_GL_BindTexture(vgl_light, &ltw, &lth);
		vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MIN_FILTER, VGL_LINEAR);
		vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MAG_FILTER, VGL_LINEAR);
		vgl.ActiveTexture(VGL_TEXTURE0 + 3);
		SDL_GL_BindTexture(relief ? vgl_dirs : vgl_light, &dtw, &dth);
		if (relief) {
			vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MIN_FILTER, VGL_NEAREST);
			vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MAG_FILTER, VGL_NEAREST);
		}
		vgl.ActiveTexture(VGL_TEXTURE0 + 4); /* s192: the mist's noise, repeating */
		SDL_GL_BindTexture(mist ? vgl_noiset : vgl_light, &ntw, &nth);
		if (mist) {
			vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MIN_FILTER, VGL_LINEAR);
			vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MAG_FILTER, VGL_LINEAR);
			vgl.TexParameteri(VGL_TEXTURE_2D, 0x2802 /* WRAP_S */, 0x2901 /* REPEAT */);
			vgl.TexParameteri(VGL_TEXTURE_2D, 0x2803 /* WRAP_T */, 0x2901);
		}
		vgl.ActiveTexture(VGL_TEXTURE0 + 5); /* s192: the fog grid */
		SDL_GL_BindTexture(mist ? vgl_fogt : vgl_light, &ftw, &fth);
		if (mist) {
			vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MIN_FILTER, VGL_LINEAR);
			vgl.TexParameteri(VGL_TEXTURE_2D, VGL_TEXTURE_MAG_FILTER, VGL_LINEAR);
		}
		if (mist && (ntw > 1.5f || ftw > 1.5f))
			mist = false; /* rectangle textures: no mist */
		if (tw > 1.5f || ltw > 1.5f) { /* rectangle textures: not this shader's kind */
			vgl.UseProgram((vgl_uint) prev);
			SDL_GL_UnbindTexture(vgm_tex);
			vgl.PopAttrib();
			SDL_SetRenderTarget(vulture_gpu, NULL);
			vgl_state = 0;
			vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
				"gpulit: rectangle textures - lighting stays in software\n");
			return;
		}
		vgl.Uniform1i(vgl_loc[VGL_U_MAP], 0);
		vgl.Uniform1i(vgl_loc[VGL_U_NRM], 1);
		vgl.Uniform1i(vgl_loc[VGL_U_LIGHT], 2);
		vgl.Uniform1i(vgl_loc[VGL_U_DIRS], 3);
		vgl.Uniform4f(vgl_loc[VGL_U_SIZE], (float) W, (float) Hh, tw, th);
		vgl.Uniform4f(vgl_loc[VGL_U_LG], (float) lox, (float) loy, (float) FS, 0.0f);
		vgl.Uniform4f(vgl_loc[VGL_U_LGS], (float) low->w, (float) low->h, ltw, lth);
		vgl.Uniform4f(vgl_loc[VGL_U_DG], (float) gx0, (float) gy0, (float) FS, relief ? 1.0f : 0.0f);
		vgl.Uniform4f(vgl_loc[VGL_U_DGS], (float) (dw > 0 ? dw : 1), (float) (dh > 0 ? dh : 1), dtw, dth);
		vgl.Uniform4f(vgl_loc[VGL_U_AREA], (float) area->x, (float) area->y,
			(float) (area->x + area->w), (float) (area->y + area->h));
		/* s190: the point lights and the relief's constants */
		if (npl > 24)
			npl = 24;
		vgl.Uniform4f(vgl_loc[VGL_U_PLI], ixm, iym, (float) (pl ? npl : 0), 0.0f);
		if (pl && npl > 0)
			vgl.Uniform4fv(vgl_loc[VGL_U_PL], npl * 3, pl);
		if (rk) {
			vgl.Uniform4f(vgl_loc[VGL_U_RKA], rk[0], rk[1], rk[2], rk[3]);
			vgl.Uniform4f(vgl_loc[VGL_U_RKB], rk[4], rk[5], rk[6], rk[7]);
		}
		/* s192: the living fog */
		vgl.Uniform1i(vgl_loc[VGL_U_NOISE], 4);
		vgl.Uniform1i(vgl_loc[VGL_U_FOG], 5);
		vgl.Uniform4f(vgl_loc[VGL_U_MIST], vgl_mistp[0], vgl_mistp[1], vgl_mistp[2],
			mist ? vgl_mistp[3] : 0.0f);
		vgl.Uniform4f(vgl_loc[VGL_U_MCOL], vgl_mistp[4], vgl_mistp[5], vgl_mistp[6], 0.0f);
		if (vgm_tex_view) { /* s201: the map was drawn at the zoom */
			vgl.Uniform4f(vgl_loc[VGL_U_VIEW], vgm_vpx, vgm_vpy, vgm_vlx, vgm_vly);
			vgl.Uniform4f(vgl_loc[VGL_U_VSC], 1.0f / vgm_vsx, 1.0f / vgm_vsy, 0.0f, 0.0f);
		} else {
			vgl.Uniform4f(vgl_loc[VGL_U_VIEW], 0.0f, 0.0f, 0.0f, 0.0f);
			vgl.Uniform4f(vgl_loc[VGL_U_VSC], 1.0f, 1.0f, 0.0f, 0.0f);
		}
		vgl.Disable(VGL_BLEND);
		vgl.Disable(VGL_SCISSOR_TEST);
		vgl.Begin(VGL_QUADS);
		vgl.TexCoord2f(0.0f, 0.0f);
		vgl.Vertex2f(-1.0f, -1.0f);
		vgl.TexCoord2f(1.0f, 0.0f);
		vgl.Vertex2f(1.0f, -1.0f);
		vgl.TexCoord2f(1.0f, 1.0f);
		vgl.Vertex2f(1.0f, 1.0f);
		vgl.TexCoord2f(0.0f, 1.0f);
		vgl.Vertex2f(-1.0f, 1.0f);
		vgl.End();
		vgl.UseProgram((vgl_uint) prev);
		SDL_GL_UnbindTexture(vgm_tex); /* SDL forgets which texture it thinks is bound */
		vgl.PopAttrib();
	}
	SDL_SetRenderTarget(vulture_gpu, NULL);
	vgm_lit_valid = true;
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
		vulture_sdl_window = vgpu_create_window(newwidth, newheight,
			SDL_WINDOW_FULLSCREEN_DESKTOP); /* s196 */
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
		vulture_sdl_window = vgpu_create_window(vulture_opts.width, vulture_opts.height,
			SDL_WINDOW_RESIZABLE
			| (getenv("VULTURE_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN : 0)); /* s183: GPU probes; s196 */
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
		vulture_sdl_window = vgpu_create_window(800, 600, SDL_WINDOW_RESIZABLE
			| (getenv("VULTURE_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN : 0)); /* s183; s196 */
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
