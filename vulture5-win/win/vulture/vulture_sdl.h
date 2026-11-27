/* NetHack may be freely redistributed.  See license for details. */

/*-------------------------------------------------------------------
 vulture_sdl.h : SDL API calls for Vulture's windowing system.
 Uses SDL 2.
-------------------------------------------------------------------*/

#ifndef _vulture_sdl_h_
#define _vulture_sdl_h_

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* NetHack keeps its own main() (sys/windows/windmain.c). Without this, SDL2 on
 * Windows #defines main -> SDL_main and the link fails with an undefined
 * SDL_main / duplicate WinMain. vulture_sdl.cpp calls SDL_SetMainReady(). */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif

#include "SDL.h"

#define SDL_TIMEREVENT SDL_USEREVENT
#define SDL_MOUSEMOVEOUT (SDL_USEREVENT+1)

/* Internal SDL2 event that leaks through SDL_PollEvent in Emscripten */
#define SDL_POLLSENTINEL 0x7F00

/* SDL2 window pointer */
extern SDL_Window *vulture_sdl_window;

/* SDL1->SDL2 key name compatibility defines */
#define SDLK_KP0 SDLK_KP_0
#define SDLK_KP1 SDLK_KP_1
#define SDLK_KP2 SDLK_KP_2
#define SDLK_KP3 SDLK_KP_3
#define SDLK_KP4 SDLK_KP_4
#define SDLK_KP5 SDLK_KP_5
#define SDLK_KP6 SDLK_KP_6
#define SDLK_KP7 SDLK_KP_7
#define SDLK_KP8 SDLK_KP_8
#define SDLK_KP9 SDLK_KP_9

#define SDLK_LMETA SDLK_LGUI
#define SDLK_RMETA SDLK_RGUI
#define KMOD_META KMOD_GUI

#define SDLK_PRINT SDLK_PRINTSCREEN
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#define SDLK_NUMLOCK SDLK_NUMLOCKCLEAR
#define SDLK_COMPOSE SDLK_APPLICATION

/* SDL surface flag compatibility */
#define SDL_SRCALPHA 0
#define SDL_SWSURFACE 0

/* SDL1 mouse wheel button compat (SDL2 uses SDL_MOUSEWHEEL events instead) */
#define SDL_BUTTON_WHEELUP 4
#define SDL_BUTTON_WHEELDOWN 5

/* low-level event handling */
extern void vulture_wait_event(SDL_Event *event, int wait_timeout);
extern int vulture_poll_event(SDL_Event *event);
extern void vulture_wait_input(SDL_Event *event, int wait_timeout);
extern void vulture_wait_key(SDL_Event *event);

/* Graphics initialization and closing */
extern void vulture_enter_graphics_mode(void);
extern void vulture_exit_graphics_mode(void);

/* Display updaters */
extern void vulture_refresh(void);
extern void vulture_refresh_region(int, int, int, int);

/* Miscellaneous */
extern void vulture_set_screensize(void);

/* Supersampled rendering (vulture_opts.render_scale).  Call
 * vulture_reattach_render_surface() after ANYTHING that can invalidate the
 * window surface - SDL2 recreates it behind our back on resize. */
extern void vulture_reattach_render_surface(void);

/* Change the render scale at runtime (clamps, reattaches, relays out every
 * window).  Does not touch the SDL window itself - see the definition. */
extern void vulture_set_render_scale(int scale);

/* highest supported render_scale; 2 already means 3840x2160 on a 1080p window */
#define V_RENDER_SCALE_MAX 4


/* The surface the whole port draws into, and against whose ->w/->h every
 * window lays itself out.  This is RENDER space. */
extern SDL_Surface *vulture_screen;

/* The real window surface, and the factor between the two.  These are the
 * only things in WINDOW space; vulture_render_scale == 1 means
 * vulture_screen == vulture_window_surface and there is no scaling at all. */
extern SDL_Surface *vulture_window_surface;
extern int vulture_render_scale;

/* s183: the map layer drawn by the GPU (see vulture_sdl.cpp).  While
 * vulture_gpumap_on is set, vulture_put_img() and the sprite transformer
 * draw into the GPU map layer instead of vulture_screen; vulture_screen is
 * then the overlay (effects, HUD, windows) composited on top. */
extern bool vulture_gpumap_on;      /* drawing the map layer right now */
extern bool vulture_gpumap_wanted;  /* the Visual effects switch (default on) */
extern bool vulture_gpumap_lost;    /* the renderer lost its textures: redraw all */
extern bool vulture_gpumap_begin(const SDL_Rect *clip);
extern void vulture_gpumap_end(void);
extern void vulture_gpumap_abandon(void); /* this frame's map was drawn in software */
extern bool vulture_gpumap_valid(void);
extern void vulture_gpumap_blit(SDL_Surface *s, const SDL_Rect *srcrect, int x, int y);
extern void vulture_gpumap_blit_ex(SDL_Surface *s, int dx, int dy, double sx, double sy,
                                   double deg, bool red, int alpha);
extern void vulture_gpumap_set_zoom(const SDL_Rect *src, const SDL_Rect *dst, int k,
                                    bool exact);
extern void vulture_gpu_forget(SDL_Surface *s);
/* s201: sharper art when zoomed in (tiles_hd/) */
extern void vulture_gpu_hd_register(SDL_Surface *s, const char *file_1x, double alpha);
extern void vulture_gpu_hd_derive(SDL_Surface *copy, SDL_Surface *orig, bool flip, int rgb,
                                  int alpha);
extern void vulture_gpumap_set_view(const SDL_Rect *src, const SDL_Rect *dst);
extern bool vulture_gpumap_hd_ok(void);
extern void vulture_gpumap_set_focus(int y); /* s205: depth of field around you */
extern void vulture_gpu_hd_derive_cut(SDL_Surface *copy, SDL_Surface *orig, SDL_Surface *mask,
                                      bool outside); /* s203 */
/* the frame as it would be shown (map layer + overlay), copied into out
 * (ARGB8888) from `area` of the render surface; plain copy without a GPU map */
extern bool vulture_frame_to_surface(SDL_Surface *out, const SDL_Rect *area);
extern int vulture_save_frame_bmp(const char *path);

/* s185: dynamic lighting on the GPU - the OpenGL renderer runs one shader
 * that does the normal-mapped relief and the light layer (vulture_sdl.cpp) */
extern bool vulture_gpulit_ok(void);
extern bool vulture_gpumap_shadow(SDL_Surface *s, const float *quad, const Uint8 *a4); /* s191 */
extern bool vulture_gpumap_quad(SDL_Surface *s, const SDL_Rect *src, const float *quad, int r, int g,
                                int b, int alpha); /* s203, s204 */
/* s192: the living fog for the next vulture_gpulit_frame: the fog grid (beside
   the light grid; fresh = rebuilt), 256x256 noise, mist[8] = the map's anchor
   x, y, time s, thickness, colour r, g, b (0..255), - */
extern void vulture_gpulit_mist(SDL_Surface *fog, bool fresh, const Uint8 *noise, const float *mist);
extern void vulture_gpu_use_gl(bool on); /* OpenGL while lit, SDL's default otherwise */
extern void vulture_gpumap_normal(SDL_Surface *s, int x, int y);
extern void vulture_gpumap_normal_ex(SDL_Surface *s, int dx, int dy, double sx, double sy,
                                     double deg, int alpha);
extern void vulture_gpumap_normal_quad(SDL_Surface *s, const SDL_Rect *src, const float *quad,
                                       int alpha); /* s204 */
extern void vulture_gpumap_set_grade(int r, int g, int b); /* s212: the area's colour grade */
/* s209: see-through round (cx, cy): alpha amin within r0, solid past r1 */
extern void vulture_gpumap_holed(SDL_Surface *s, int x, int y, float cx, float cy, float r0, float r1,
                                 int amin);
extern void vulture_gpumap_normal_holed(SDL_Surface *s, int x, int y, float cx, float cy, float r0,
                                        float r1, int amin);
/* s190: kw / sw per relief cell (x256), pl: npl point lights, 12 floats each,
   rk: the relief's constants (levelwin.cpp's vl_rk), ixm / iym: 1 / V_MAP_XMOD,
   1 / V_MAP_YMOD */
extern void vulture_gpulit_frame(SDL_Surface *low, int lox, int loy, int FS, const short *kw,
                                 const short *sw, int dw, int dh, int gx0, int gy0,
                                 const float *pl, int npl, const float *rk, float ixm, float iym,
                                 bool relief, bool fresh, const SDL_Rect *area);

#endif
