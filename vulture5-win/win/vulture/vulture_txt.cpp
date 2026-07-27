/* NetHack may be freely redistributed under the Nethack General Public License
* See nethack/dat/license for details */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "SDL.h"
#include "SDL_ttf.h"

#include "vulture_txt.h"
#include "vulture_types.h"

#define VULTURE_MAX_FONTS  2

/* the rest of vulture gets these defines indirectly from nethack code.
* but I'd prefer not to pull all that in here */
#define TRUE 1
#define FALSE 0

struct vulture_font {
	TTF_Font *fontptr;
	int lineheight;
	/* what this font was opened FROM, so vulture_reload_fonts() can reopen it
	 * at a different size when the UI scale changes at runtime (F8) */
	std::string filename;
	int fontindex;
	int basesize;

	vulture_font() : fontptr(NULL), lineheight(0), filename(""), fontindex(0), basesize(0) {}
};

/* new[]/delete[], not calloc/free: this now holds a std::string, which needs
 * its constructor run (and its destructor at shutdown) */
static struct vulture_font *vulture_fonts = NULL;


/*
 * Load a font from a ttf file at the given BASE pointsize.
 *
 * The size actually handed to SDL_ttf is basesize * vulture_ui_scale: at
 * render_scale s the frame is downscaled by s on present, so a font opened at
 * the native size would reach the screen s times too small (that is exactly
 * why the status bar and the menus became unreadable at 2x).  Opening it s
 * times larger lands it back at its native apparent size - and because the
 * downscale is a box filter over real TTF hinting rather than a bitmap
 * upscale, the glyphs come out CRISPER than at 1x, not merely the right size.
 *
 * Everything that lays text out (vulture_text_length/height, the cached
 * lineheight) reports the render-space metrics of this scaled font, so every
 * caller's layout arithmetic scales with it for free.
 */
int vulture_load_font (int font_id, const char * ttf_filename, int fontindex, int pointsize)
{
	TTF_Font *newfont;
	if(!TTF_WasInit()) {
		if(TTF_Init()==-1)
			return FALSE;
	}

	/* the array must exist even if TTF_Init() had already run for some other
	 * reason; the original code only allocated it inside the !WasInit branch */
	if (!vulture_fonts)
		vulture_fonts = new struct vulture_font[VULTURE_MAX_FONTS];

	if (!vulture_fonts)
		return FALSE;

	/* font_id should always be passed as a #define, which is always <  VULTURE_MAX_FONTS */
	if (font_id >= VULTURE_MAX_FONTS)
		return FALSE;

	newfont = TTF_OpenFontIndex(ttf_filename, pointsize * vulture_ui_scale, fontindex);

	if (!newfont)
		return FALSE;

	if (vulture_fonts[font_id].fontptr)
		TTF_CloseFont(vulture_fonts[font_id].fontptr);

	vulture_fonts[font_id].fontptr = newfont;
	vulture_fonts[font_id].lineheight = TTF_FontAscent(newfont) + 2 * vulture_ui_scale;
	vulture_fonts[font_id].filename = ttf_filename;
	vulture_fonts[font_id].fontindex = fontindex;
	vulture_fonts[font_id].basesize = pointsize;

	return TRUE;
}


/* Reopen every loaded font at the current vulture_ui_scale.  Called from
 * vulture_ui_rescale() when the render scale changes at runtime; a no-op
 * before any font has been loaded. */
int vulture_reload_fonts(void)
{
	int font_id;
	int all_ok = TRUE;

	if (!vulture_fonts)
		return TRUE;

	for (font_id = 0; font_id < VULTURE_MAX_FONTS; font_id++) {
		if (!vulture_fonts[font_id].fontptr || vulture_fonts[font_id].filename.empty())
			continue;

		/* copy: vulture_load_font() writes the struct it reads from */
		std::string fname = vulture_fonts[font_id].filename;

		all_ok &= vulture_load_font(font_id, fname.c_str(),
				vulture_fonts[font_id].fontindex,
				vulture_fonts[font_id].basesize);
	}

	return all_ok;
}


int vulture_put_text (int font_id, std::string str, SDL_Surface *dest, int x, int y, Uint32 color)
{
	SDL_Surface *textsurface;
	SDL_Color fontcolor;
	SDL_Rect dstrect;
  std::string cleaned_str;
	unsigned int i;

	if (font_id >= VULTURE_MAX_FONTS || (!vulture_fonts[font_id].fontptr) || str.empty())
		return FALSE;

	/* sanitize the input string of unprintable characters */
	cleaned_str = str;
	for (i = 0; i < cleaned_str.length(); i++)
		if (!isprint(cleaned_str[i]))
			cleaned_str[i] = ' ';


	/* extract components from the 32-bit color value */
	fontcolor.r = (color & dest->format->Rmask) >> dest->format->Rshift;
	fontcolor.g = (color & dest->format->Gmask) >> dest->format->Gshift;
	fontcolor.b = (color & dest->format->Bmask) >> dest->format->Bshift;
	fontcolor.a = 255;

	textsurface = TTF_RenderText_Blended(vulture_fonts[font_id].fontptr,
	                                     cleaned_str.c_str(), fontcolor);
	if (!textsurface)
		return FALSE;

	dstrect.x = x;
	dstrect.y = y - 1;
	dstrect.w = textsurface->w;
	dstrect.h = textsurface->h;

	SDL_BlitSurface(textsurface, NULL, dest, &dstrect);
	SDL_FreeSurface(textsurface);
	
	return TRUE;
}



int vulture_put_text_shadow (int font_id, std::string str, SDL_Surface *dest,
							int x, int y, Uint32 textcolor, Uint32 shadowcolor)
{
	/* Draw the shadow first, offset by the UI scale rather than by one raw
	 * pixel: the font is opened at vulture_ui_scale, so a 1px offset would be
	 * a sub-pixel offset once the frame is boxed down, and every drop shadow
	 * in the game would fade to a faint smear at scale > 1.  Exactly x+1,y+1
	 * at scale 1. */
	vulture_put_text(font_id, str, dest, x + vulture_ui_scale, y + vulture_ui_scale, shadowcolor);
	/* we only truly care that the actual text got drawn, so only retrun that status */
	return vulture_put_text(font_id, str, dest, x, y, textcolor);
}


/* draw text over multiple lines if its length exceeds maxlen */
void vulture_put_text_multiline(int font_id, std::string str, SDL_Surface *dest,
								int x, int y, Uint32 color, Uint32 shadowcolor, int maxlen)
{
  std::string str_copy;
	int lastfit, txtlen, lineno, text_height;
	size_t endpos, startpos;

	lineno = 0;
	lastfit = 0;
	startpos = endpos = 0;
	text_height = vulture_text_height(font_id, str);

	/* lastfit is true when the last segment has been fit onto the surface (drawn) */
	while (!lastfit) {
		lastfit = 1;

		startpos = startpos + endpos;
		str_copy = str.substr(startpos, std::string::npos);
		endpos = str_copy.length() - 1;

		txtlen = vulture_text_length(font_id, str_copy);

		/* split word off the end of the string until it is short enough to fit */
		while (txtlen > maxlen) {
			/* a piece will be split off the end, so this is not the last piece */
			lastfit = 0;

			/* find a suitable place to split */
			endpos = str_copy.find_last_of(" \t\n");

			/* prevent infinite loops if a long word doesn't fit */
			if (endpos == std::string::npos) {
				endpos = str_copy.length() - 1;
				lastfit = 1;
				break;
			}

			/* truncate the text */
			str_copy.erase(endpos++);
			txtlen = vulture_text_length(font_id, str_copy);
		}
		vulture_put_text_shadow(font_id, str_copy, dest, x, y + lineno * text_height, color, shadowcolor);

		lineno++;
	}
}



int vulture_text_length (int font_id, std::string str)
{
	int width = 0;

	if (!vulture_fonts[font_id].fontptr || str.empty())
		return 0;

	TTF_SizeText(vulture_fonts[font_id].fontptr, str.c_str(), &width, NULL);

	return width;
}



int vulture_text_height (int font_id, std::string str)
{
	int height = 0;

	if (!vulture_fonts[font_id].fontptr || str.empty())
		return 0;

	TTF_SizeText(vulture_fonts[font_id].fontptr, str.c_str(), NULL, &height);

	return height;
}



int vulture_get_lineheight(int font_id)
{
	if (font_id < VULTURE_MAX_FONTS && vulture_fonts[font_id].fontptr)
		return vulture_fonts[font_id].lineheight;
	return 0;
}



void vulture_free_fonts()
{
	int font_id;

	if (!vulture_fonts)
		return;

	for (font_id = 0; font_id < VULTURE_MAX_FONTS; font_id++)
	{
		if (vulture_fonts[font_id].fontptr)
			TTF_CloseFont(vulture_fonts[font_id].fontptr);
		vulture_fonts[font_id].fontptr = NULL;
	}

	delete[] vulture_fonts;
	vulture_fonts = NULL;

	TTF_Quit();
}
