/* NetHack may be freely redistributed.  See license for details. */

#include <stdlib.h>
#include <string.h>

#include "vulture_sdl.h"
#include "vulture_gra.h"


Uint32 CLR32_BLACK;
Uint32 CLR32_BLACK_A30;
Uint32 CLR32_BLACK_A50;
Uint32 CLR32_BLACK_A70;
Uint32 CLR32_GREEN;
Uint32 CLR32_YELLOW;
Uint32 CLR32_ORANGE;
Uint32 CLR32_RED;
Uint32 CLR32_GRAY20;
Uint32 CLR32_GRAY70;
Uint32 CLR32_GRAY77;
Uint32 CLR32_PURPLE44;
Uint32 CLR32_LIGHTPINK;
Uint32 CLR32_LIGHTGREEN;
Uint32 CLR32_BROWN;
Uint32 CLR32_WHITE;
Uint32 CLR32_BLESS_BLUE;
Uint32 CLR32_CURSE_RED;
Uint32 CLR32_GOLD_SHADE;


SDL_PixelFormat * vulture_px_format = NULL;


#define FADESTEPS_PER_SEC 20

static void dofade(double n_secs, SDL_Surface * blendimg)
{
	unsigned int cur_clock, end_clock, start_clock, sleeptime, elapsed;

	/* calculate how many individual blends we want to do */
	int n_steps = FADESTEPS_PER_SEC * n_secs;
	SDL_SetSurfaceAlphaMod(blendimg, (SDL_ALPHA_OPAQUE / n_steps));
	SDL_SetSurfaceBlendMode(blendimg, SDL_BLENDMODE_BLEND);

	start_clock = cur_clock = SDL_GetTicks();
	end_clock = start_clock + (n_secs * 1000);
	while (cur_clock < end_clock)
	{
		/* blitting the image onto the screen repeatedly
		* will slowly add up to full brightness, while
		* drawing over the screen with semi-transparent
		* darkness repeatedly gives us a fade-out effect*/
		SDL_BlitSurface(blendimg, NULL, vulture_screen, NULL);
		vulture_refresh();

		elapsed = SDL_GetTicks() - cur_clock;
		sleeptime = ((1000/FADESTEPS_PER_SEC) > elapsed) ? ((1000/FADESTEPS_PER_SEC) - elapsed) : 0;
		SDL_Delay(sleeptime);
		cur_clock = SDL_GetTicks();
	}

	/* ensure that the screen is fully faded in */
	SDL_SetSurfaceAlphaMod(blendimg, SDL_ALPHA_OPAQUE);
	SDL_SetSurfaceBlendMode(blendimg, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(blendimg, NULL, vulture_screen, NULL);
	vulture_refresh();
}


void vulture_fade_out(double n_secs)
{
	/* create a screen sized pure black surface without an alpha channel */
	SDL_Surface * img = SDL_CreateRGBSurface(SDL_SWSURFACE, vulture_screen->w, vulture_screen->h,
											vulture_px_format->BitsPerPixel,
											vulture_px_format->Rmask,
											vulture_px_format->Gmask,
											vulture_px_format->Bmask, 0);
	SDL_FillRect(img, NULL, 0);

	/* blend the blackness with the existing image */
	dofade(n_secs, img);

	SDL_FreeSurface(img);
}


void vulture_fade_in(double n_secs)
{
	/* create a copy of the screen without an alpha-channel (ie an alpha mask == 0) */
	SDL_Surface * img = SDL_CreateRGBSurface(SDL_SWSURFACE, vulture_screen->w, vulture_screen->h,
											vulture_px_format->BitsPerPixel,
											vulture_px_format->Rmask,
											vulture_px_format->Gmask,
											vulture_px_format->Bmask, 0);
	SDL_BlitSurface(vulture_screen, NULL, img, NULL);

	/* set the screen to all-black and display that */
	SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);
	vulture_refresh();

	dofade(n_secs, img);

	SDL_FreeSurface(img);
}


/*--------------------------------------------------------------------------
Basic graphics plotting
--------------------------------------------------------------------------*/
void vulture_set_draw_region
(
int _drx1, int _dry1,
int _drx2, int _dry2
)
{
	SDL_Rect rect;

	rect.x = _drx1;
	rect.y = _dry1;
	rect.w = _drx2+1-_drx1;
	rect.h = _dry2+1-_dry1;
	SDL_SetClipRect( vulture_screen, &rect );
}



/* draws an unfilled rectangle onto the given surface;
* this function may blow up if given invalid coordinates */
void vulture_rect_surface
(
	SDL_Surface *surface,
	int x1, int y1,
	int x2, int y2,
	Uint32 color
)
{
	int i,j; 
	Uint32 *screen;

	if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
	screen = (Uint32 *)surface->pixels;

	for (i = x1; i <= x2; i++)
	{
		screen[y1*surface->w+i] = color;
		screen[y2*surface->w+i] = color;
	}
	for (j = y1; j <= y2; j++)
	{
		screen[j*surface->w+x1] = color;
		screen[j*surface->w+x2] = color;
	}

	if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
}


/* draws a filled rectangle onto the given surface;
* this function blows up if given invalid coordinates */
void vulture_fill_rect_surface
(
	SDL_Surface *surface,
	int x1, int y1,
	int x2, int y2,
	Uint32 color
)
{
	SDL_Rect dstrect, srcrect;

	dstrect.x = x1;
	dstrect.y = y1;
	dstrect.w = x2+1-x1;
	dstrect.h = y2+1-y1;

	srcrect.x = 0;
	srcrect.y = 0;
	srcrect.w = dstrect.w;
	srcrect.h = dstrect.h;

	/* to get alpha-blending with our rect-filling we create a temp surface, fill that
	* and then let SDL_BlitSurface do the blending for us */
	SDL_Surface * tmp_surface = SDL_CreateRGBSurface(
			0,
			srcrect.w,
			srcrect.h,
			32, surface->format->Rmask, surface->format->Gmask, surface->format->Bmask, DEF_AMASK);

	SDL_FillRect(tmp_surface, &srcrect, color);
	
	SDL_BlitSurface(tmp_surface, &srcrect, surface, &dstrect);
	SDL_FreeSurface(tmp_surface);
}


/*
 * Etched bezels around UI elements (buttons, inventory cells, list headers).
 *
 * These are drawn vulture_ui_scale pixels thick rather than one pixel thick.
 * Every caller is UI - button.cpp, objitemwin.cpp, objheaderwin.cpp; nothing
 * on the level map uses them - and a one-pixel line in render space survives
 * the present-time box filter as a HALF-intensity apparent pixel at scale 2,
 * so at native size every bezel in the game lost roughly half its contrast.
 * Nesting s copies of the frame reproduces the intended solid edge.
 *
 * At scale 1 the loop runs exactly once and emits precisely the four lines it
 * always did.
 */
void vulture_draw_raised_frame(int x1, int y1, int x2, int y2)
{
	int i;

	for (i = 0; i < vulture_ui_scale; i++) {
		/* bottom edge */
		vulture_line(x1 + i + 1, y2 - i, x2 - i,     y2 - i,     CLR32_GRAY70);
		/* top edge */
		vulture_line(x1 + i,     y1 + i, x2 - i - 1, y1 + i,     CLR32_GRAY20);
		/* right edge */
		vulture_line(x2 - i,     y1 + i, x2 - i,     y2 - i,     CLR32_GRAY77);
		/* left edge */
		vulture_line(x1 + i,     y1 + i, x1 + i,     y2 - i - 1, CLR32_GRAY20);
	}
}


void vulture_draw_lowered_frame(int x1, int y1, int x2, int y2)
{
	int i;

	for (i = 0; i < vulture_ui_scale; i++) {
		/* top edge */
		vulture_line(x1 + i,     y1 + i,     x2 - i - 1, y1 + i, CLR32_GRAY70);
		/* left edge */
		vulture_line(x1 + i,     y1 + i + 1, x1 + i,     y2 - i - 1, CLR32_GRAY77);
		/* bottom edge */
		vulture_line(x1 + i + 1, y2 - i,     x2 - i,     y2 - i, CLR32_GRAY20);
		/* right edge */
		vulture_line(x2 - i,     y1 + i + 1, x2 - i,     y2 - i, CLR32_GRAY20);
	}
}

/*
 * IMPLEMENTATION_LOG s200: the modern UI kit.  Every menu, dialog, item card
 * and button was carved stone with grey bevels; they are drawn with these
 * now, in the newer HUD's style - a dark translucent panel, thin dim-gold
 * borders, gold where the pointer is.  Fills are blits of one cached pixel
 * (blended, clipped by the draw region), so a panel over the map lets a
 * little of it through and nothing draws outside its window.
 */
Uint32 vulture_ui_rgb(int r, int g, int b)
{
	return 0xFF000000u | ((Uint32) (r & 255) << 16) | ((Uint32) (g & 255) << 8) | (Uint32) (b & 255);
}

void vulture_ui_fill(int x, int y, int w, int h, int r, int g, int b, int alpha)
{
	static SDL_Surface *px = NULL;
	SDL_Rect d;

	if (w <= 0 || h <= 0 || alpha <= 0 || !vulture_screen)
		return;
	if (!px) {
		px = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!px)
			return;
		SDL_FillRect(px, NULL, 0xFFFFFFFFu);
		SDL_SetSurfaceBlendMode(px, SDL_BLENDMODE_BLEND);
	}
	SDL_SetSurfaceColorMod(px, (Uint8) r, (Uint8) g, (Uint8) b);
	SDL_SetSurfaceAlphaMod(px, (Uint8) (alpha > 255 ? 255 : alpha));
	d.x = x, d.y = y, d.w = w, d.h = h;
	SDL_BlitScaled(px, NULL, vulture_screen, &d);
}

/* a rectangle's outline, vulture_ui_scale thick (one render pixel would be a
   half-intensity line after the present-time downscale) */
void vulture_ui_border(int x, int y, int w, int h, int r, int g, int b)
{
	int t = vulture_ui_scale > 0 ? vulture_ui_scale : 1;

	vulture_ui_fill(x, y, w, t, r, g, b, 255);
	vulture_ui_fill(x, y + h - t, w, t, r, g, b, 255);
	vulture_ui_fill(x, y + t, t, h - 2 * t, r, g, b, 255);
	vulture_ui_fill(x + w - t, y + t, t, h - 2 * t, r, g, b, 255);
}

/* a window: dark and nearly opaque, a dim-gold frame, a gold top edge */
void vulture_ui_panel(int x, int y, int w, int h)
{
	int t = vulture_ui_scale > 0 ? vulture_ui_scale : 1;

	vulture_ui_fill(x, y, w, h, 12, 10, 13, 248);
	vulture_ui_border(x, y, w, h, 86, 72, 48);
	vulture_ui_fill(x + t, y + t, w - 2 * t, 2 * t, 255, 200, 70, 150);
}


/*--------------------------------------------------------------------------
Bitmap handling
--------------------------------------------------------------------------*/
SDL_Surface *vulture_get_img_src
(
	int x1, int y1,
	int x2, int y2,
	SDL_Surface *img_source
)
{
	SDL_Surface *toSurface;
	int i;
	unsigned char * srcpixels = (unsigned char*)img_source->pixels;


	toSurface = SDL_CreateRGBSurface(
			0,
			x2+1-x1,
			y2+1-y1,
			img_source->format->BitsPerPixel,
			img_source->format->Rmask,
			img_source->format->Gmask,
			img_source->format->Bmask,
			img_source->format->Amask);

	/* we might have been passed coordinates that are outside the source which will blow up our memcpy */
	int leftoffset = (x1 < 0) ? -x1 : 0;
	int rightoffset = (x2 >= img_source->w) ? (x2 - img_source->w + 1) : 0;
	int topoffset = (y1 < 0) ? -y1: 0;
	int bottomoffset = (y2 >= img_source->h) ? (y2 - img_source->h + 1) : 0;

	int destwidth = x2+1-x1-leftoffset-rightoffset;
	int destheight = y2+1-y1-topoffset-bottomoffset;
	if (destwidth <= 0 || destheight <= 0)
		return toSurface;

	/* Blitting would be a mistake here, because we need to preserve
	* the alpha channel, which blitting does not do */
	for (i = topoffset; i < (y2+1-y1-bottomoffset); i++)
		memcpy((char*)toSurface->pixels + toSurface->pitch*i + (leftoffset * 4),
			&srcpixels[(i+y1)*img_source->pitch + (x1 + leftoffset)*4],
			destwidth*4);

	return toSurface;
}



/*
 * Nearest-neighbour integer upscale of a UI bitmap, used to compensate the
 * present-time downscale at render_scale > 1 (see vulture_types.h).
 *
 * Deliberately NOT SDL_BlitScaled: this must preserve the alpha channel
 * byte-for-byte (SDL2 blits blend or drop alpha depending on the surface's
 * blend mode, which is exactly the bug vulture_get_img_src() exists to avoid),
 * and an integer factor needs no filtering - each source pixel simply becomes
 * an s x s block.  The result is then box-filtered back down 1:1 on present,
 * so the art reaches the screen pixel-identical to the unscaled original.
 *
 * Returns a NEW surface; the caller owns it and the source is untouched.
 * Returns NULL for a NULL source, and a plain copy for factor <= 1, so callers
 * never need to special-case 1x.
 */
SDL_Surface *vulture_scale_surface_int(SDL_Surface *src, int factor)
{
	SDL_Surface *dst;
	int x, y, i;

	if (!src)
		return NULL;

	/* factor <= 1, or a format this loop cannot walk safely (the whole port
	 * runs 32bpp, so this is defensive): hand back an unscaled copy */
	if (factor <= 1 || src->format->BytesPerPixel != 4)
		dst = vulture_get_img_src(0, 0, src->w - 1, src->h - 1, src);
	else {
		dst = SDL_CreateRGBSurface(0, src->w * factor, src->h * factor,
				src->format->BitsPerPixel,
				src->format->Rmask, src->format->Gmask,
				src->format->Bmask, src->format->Amask);
		if (!dst)
			return NULL;

		for (y = 0; y < src->h; y++) {
			const Uint32 *srow = (const Uint32 *)((Uint8 *)src->pixels
					+ (size_t)y * src->pitch);
			Uint32 *drow = (Uint32 *)((Uint8 *)dst->pixels
					+ (size_t)y * factor * dst->pitch);

			/* expand one source row horizontally... */
			for (x = 0; x < src->w; x++) {
				Uint32 px = srow[x];
				for (i = 0; i < factor; i++)
					drow[x * factor + i] = px;
			}
			/* ...then replicate that finished row vertically */
			for (i = 1; i < factor; i++)
				memcpy((Uint8 *)drow + (size_t)i * dst->pitch, drow,
						(size_t)dst->w * 4);
		}
	}

	if (!dst)
		return NULL;

	/*
	 * Inherit the source's blit state.  This matters: most UI art (the minimap
	 * frame, the window corners, the direction arrows) is alpha-cut PNG that
	 * must keep SDL_BLENDMODE_BLEND, or its fully transparent pixels blit as
	 * whatever RGB happens to sit under the zero alpha - which paints the
	 * minimap as a solid white box (observed, then fixed here).  A freshly
	 * created surface does not necessarily get the same default, so copy it
	 * explicitly rather than assuming.
	 */
	{
		SDL_BlendMode bm;
		Uint8 amod;

		if (SDL_GetSurfaceBlendMode(src, &bm) == 0)
			SDL_SetSurfaceBlendMode(dst, bm);
		if (SDL_GetSurfaceAlphaMod(src, &amod) == 0)
			SDL_SetSurfaceAlphaMod(dst, amod);
	}

	return dst;
}


/* replace *surf with a vulture_ui_scale-times-larger copy of itself, freeing
 * the original.  A no-op at scale 1 (and for a NULL/failed surface), so it is
 * safe to sprinkle over every UI asset load site. */
void vulture_scale_ui_surface(SDL_Surface **surf)
{
	SDL_Surface *scaled;

	if (!surf || !*surf || vulture_ui_scale <= 1)
		return;

	scaled = vulture_scale_surface_int(*surf, vulture_ui_scale);
	if (!scaled)
		return;

	SDL_FreeSurface(*surf);
	*surf = scaled;
}


void vulture_put_img
(
	int x, int y,
	SDL_Surface *a
)
{
	if (!a)
		return;
	if (vulture_gpumap_on) { /* s183: the map layer is drawn on the GPU */
		vulture_gpumap_blit(a, NULL, x, y);
		return;
	}

	SDL_Rect toRect;

	toRect.x = x;
	toRect.y = y;
	toRect.w = a->w;
	toRect.h = a->h;

	SDL_BlitSurface( a, NULL, vulture_screen, &toRect);
}

