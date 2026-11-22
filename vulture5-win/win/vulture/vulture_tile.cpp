/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

#include <errno.h>
#include <set>
#include <map>

extern "C" {
	#include "hack.h"
  #ifndef VULTURE_NETHACK_3_6_0
	# include "epri.h"
  #endif
}

#if !defined _WIN32
	#include <sys/mman.h>
#endif

#include "vulture_types.h"
#include "vulture_tile.h"
#include "vulture_win.h"
#include "vulture_gra.h"
#include "vulture_gfl.h"
#include "vulture_gen.h"
#include "vulture_opt.h"
#include "vulture_tileconfig.h"

#define TILEARRAYLEN (GAMETILECOUNT*3)
#define TILECACHE_MAXAGE 4

typedef struct {
	vulture_tile *tile;
	int age;
} vulture_tilecache_entry;


/* main tile arrays */
static vulture_tilecache_entry *vulture_tilecache;

/* brightness scale factors for shade levels (integer, divide by 256):
 * shade 1 = 30% darker -> multiply by 175/256 ≈ 0.68
 * shade 2 = 70% darker -> multiply by  79/256 ≈ 0.31 */
static const int shade_scale[3] = { 256, 175, 79 };

static gametiles *vulture_gametiles;

static vulture_tile *vulture_make_alpha_player_tile(int monnum, double op_scale);
static inline vulture_tile * vulture_shade_tile(vulture_tile *orig, int shadelevel);
static inline void vulture_set_tile_alpha(vulture_tile *tile, double opacity);

static int vulture_obfuscate_object(int obj_id);


inline static void vulture_free_tile(vulture_tile *tile)
{
	if (!tile)
		return;

	SDL_FreeSurface(tile->graphic);
	delete tile;
}


/* A tile is only usable if it exists AND carries a decoded surface.
 * vulture_load_tile() can hand back a tile whose graphic failed to decode,
 * which is not the same thing as a NULL tile and has to be checked for
 * separately - it is the case that used to crash the shading path. */
inline static bool vulture_tile_is_drawable(const vulture_tile *tile)
{
	return tile != NULL && tile->graphic != NULL;
}


/* Which conspicuous ERROR placeholder should stand in for a tile that would
 * not load. Every FIXED-size category reserves its last slot for exactly this
 * (see the offset block in vulture_tile.h), so the substitute carries the
 * right category's hotspots and lands where the real tile would have.
 *
 * The three variable-size categories - edge, floor and wall - size themselves
 * from vulture_typecount[] at load time and so have no arithmetically-known
 * reserved slot; they fall back to the misc placeholder, which is still
 * conspicuous, just anchored like a misc tile.
 *
 * Returning tile_id itself means "this IS the placeholder", i.e. there is
 * nothing left to substitute. */
static int vulture_error_tile_for(int tile_id)
{
	/* deliberately not static: several of these counts are runtime values */
	const int range_end[] = {
		OBJTILEOFFSET + OBJTILECOUNT,
		ICOTILEOFFSET + ICOTILECOUNT,
		MONTILEOFFSET + MONTILECOUNT,
		STATILEOFFSET + STATILECOUNT,
		FIGTILEOFFSET + FIGTILECOUNT,
		EXPTILEOFFSET + EXPTILECOUNT,
		CURTILEOFFSET + CURTILECOUNT,
		MISCTILEOFFSET + MISCTILECOUNT
	};
	unsigned int i;

	for (i = 0; i < sizeof range_end / sizeof range_end[0]; i++)
		if (tile_id < range_end[i])
			return range_end[i] - 1;

	return V_MISC_ERROR;
}


/* flip the tile arrays and unload all tiles that were not used for 2 turns */
void vulture_tilecache_discard(void)
{
	int i;
	for (i = 0; i < TILEARRAYLEN; i++)
	{
		if (vulture_tilecache[i].tile)
			vulture_free_tile(vulture_tilecache[i].tile);
		vulture_tilecache[i].tile = NULL;
		vulture_tilecache[i].age = 0;
	}
}


void vulture_tilecache_age(void)
{
	int i;
	for (i = 0; i < TILEARRAYLEN; i++)
	{
		vulture_tilecache[i].age++;
		if (vulture_tilecache[i].tile && vulture_tilecache[i].age > TILECACHE_MAXAGE)
		{
			vulture_free_tile(vulture_tilecache[i].tile);
			vulture_tilecache[i].tile = NULL;
		}
	}
}


void vulture_tilecache_add(vulture_tile *tile, int tile_id)
{
	if (vulture_tilecache[tile_id].tile && vulture_tilecache[tile_id].tile != tile)
		vulture_free_tile(vulture_tilecache[tile_id].tile);

	vulture_tilecache[tile_id].tile = tile;
	vulture_tilecache[tile_id].age = 0;
}


vulture_tile *vulture_tilecache_get(int tile_id)
{
	vulture_tilecache[tile_id].age = 0;
	if (vulture_tilecache[tile_id].tile)
		return vulture_tilecache[tile_id].tile;
	return NULL;
}


/*
 * IMPLEMENTATION_LOG s149: normal maps.
 * Every tile has a normal map in tiles_n/ (same file name; made offline by
 * toolchain/make_normal_maps.py): RGB = the surface normal in screen space
 * (x right, y down, z toward you), alpha = the tile's alpha.  While the level
 * window draws the map it turns vulture_normal_capture on, and every map tile
 * drawn also lays its normal map into vulture_normal_buf at the same place,
 * so the buffer ends up holding the normal of whatever is visible at each
 * pixel.  levelwin's lighting pass then lights each pixel from it.
 * The alpha is taken from the colour tile as loaded (walls get their
 * see-through opacity there), so normals blend exactly like the art.
 * A flipped sprite uses a mirrored copy with x negated.
 */
SDL_Surface *vulture_normal_buf = NULL;
bool vulture_normal_capture = false;
static std::map<int, SDL_Surface *> vnorm_cache[2]; /* [flipped]; NULL = none */

static SDL_Surface *vnorm_load(int tile_id)
{
	int rid = vulture_resolve_tile_id(tile_id);
	std::string fn;
	size_t p, q;
	FILE *fp;
	long fsize;
	char *data;
	SDL_Surface *raw, *n, *col;
	vulture_tile *t;

	if (rid < 0 || rid >= GAMETILECOUNT)
		return NULL;
	fn = vulture_gametiles[rid].filename;
	p = fn.rfind("tiles/");
	q = fn.rfind("tiles\\");
	if (p == std::string::npos || (q != std::string::npos && q > p))
		p = q;
	if (p == std::string::npos)
		return NULL;
	fn.insert(p + 5, "_n");
	if (!(fp = fopen(fn.c_str(), "rb")))
		return NULL;
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	rewind(fp);
	if (fsize <= 0) {
		fclose(fp);
		return NULL;
	}
	data = new char[fsize];
	if (fread(data, fsize, 1, fp) != 1) {
		delete[] data;
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	raw = vulture_load_surface(data, (unsigned int) fsize);
	delete[] data;
	if (!raw)
		return NULL;
	n = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!n)
		return NULL;
	/* the colour tile's alpha, as loaded (wall opacity included) */
	t = vulture_get_tile(tile_id);
	if (t && t->graphic && t->graphic->w == n->w && t->graphic->h == n->h
	    && (col = SDL_ConvertSurfaceFormat(t->graphic, SDL_PIXELFORMAT_ARGB8888, 0)) != NULL) {
		int x, y;

		SDL_LockSurface(n);
		SDL_LockSurface(col);
		for (y = 0; y < n->h; y++) {
			Uint32 *d = (Uint32 *) ((Uint8 *) n->pixels + y * n->pitch);
			Uint32 *s = (Uint32 *) ((Uint8 *) col->pixels + y * col->pitch);

			for (x = 0; x < n->w; x++)
				d[x] = (d[x] & 0x00FFFFFFu) | (s[x] & 0xFF000000u);
		}
		SDL_UnlockSurface(col);
		SDL_UnlockSurface(n);
		SDL_FreeSurface(col);
	}
	SDL_SetSurfaceBlendMode(n, SDL_BLENDMODE_BLEND);
	return n;
}

/* a mirrored copy for sprites drawn facing the other way: x flips sign */
static SDL_Surface *vnorm_mirror(SDL_Surface *s)
{
	SDL_Surface *m;
	int x, y;

	if (!s || !(m = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, 32, SDL_PIXELFORMAT_ARGB8888)))
		return NULL;
	SDL_LockSurface(s);
	SDL_LockSurface(m);
	for (y = 0; y < s->h; y++) {
		Uint32 *src = (Uint32 *) ((Uint8 *) s->pixels + y * s->pitch);
		Uint32 *dst = (Uint32 *) ((Uint8 *) m->pixels + y * m->pitch);

		for (x = 0; x < s->w; x++) {
			Uint32 p = src[s->w - 1 - x];
			Uint32 r = 255 - ((p >> 16) & 255);

			dst[x] = (p & 0xFF00FFFFu) | (r << 16);
		}
	}
	SDL_UnlockSurface(m);
	SDL_UnlockSurface(s);
	SDL_SetSurfaceBlendMode(m, SDL_BLENDMODE_BLEND);
	return m;
}

SDL_Surface *vulture_get_tile_normal(int tile_id, bool flipped)
{
	std::map<int, SDL_Surface *> &c = vnorm_cache[flipped ? 1 : 0];
	std::map<int, SDL_Surface *>::iterator it;
	SDL_Surface *s;

	if (tile_id < 0 || tile_id >= GAMETILECOUNT)
		return NULL;
	if ((it = c.find(tile_id)) != c.end())
		return it->second;
	s = flipped ? vnorm_mirror(vulture_get_tile_normal(tile_id, false)) : vnorm_load(tile_id);
	c[tile_id] = s;
	return s;
}

/* lay a tile's normal map into the normal buffer (map drawing only) */
void vulture_put_normal(int x, int y, int tile_id)
{
	SDL_Surface *n;
	SDL_Rect r;

	if (!vulture_normal_capture || !vulture_normal_buf
	    || !(n = vulture_get_tile_normal(tile_id, false)))
		return;
	r.x = x, r.y = y, r.w = n->w, r.h = n->h;
	SDL_BlitSurface(n, NULL, vulture_normal_buf, &r);
}

void vulture_put_tile_shaded(int x, int y, int tile_id, int shadelevel)
{
	vulture_tile * tile = NULL;

	if (tile_id < 0)
		return;

	tile = vulture_get_tile_shaded(tile_id, shadelevel);

	if (tile != NULL) {
		vulture_put_img(x + tile->xmod, y + tile->ymod, tile->graphic);
		if (vulture_normal_capture) /* s149 */
			vulture_put_normal(x + tile->xmod, y + tile->ymod, tile_id);
	}
}


/*
 * Draw a game tile at vulture_ui_scale rather than at its native pixel size.
 *
 * This is the ONE place a tile is UI-compensated, and it exists for object
 * icons that appear inside UI: the inventory / pickup / multidrop cells
 * (winclass/objitemwin.cpp).  Those cells are laid out in UI space
 * (V_LISTITEM_WIDTH/HEIGHT), so a native-size icon inside them would be
 * unreadable at exactly the moment the surrounding text became readable.
 *
 * The level map deliberately does NOT go through here - levelwin's tile loops
 * call vulture_put_tile()/vulture_put_tile_shaded() and stay native, which is
 * the entire point of rendering large: the extra render area buys extra
 * visible dungeon rather than bigger dungeon.
 *
 * At scale 1 this is vulture_put_tile_shaded() with one extra branch and no
 * allocation.  Above 1 it upscales per call rather than caching: an object
 * icon is a few thousand pixels and a menu redraw touches a few dozen of them,
 * which is nothing next to the full-frame box filter that follows.
 */
void vulture_put_tile_ui(int x, int y, int tile_id)
{
	vulture_tile *tile;
	SDL_Surface *scaled;

	if (tile_id < 0)
		return;

	tile = vulture_get_tile_shaded(tile_id, 0);
	if (tile == NULL)
		return;

	if (vulture_ui_scale <= 1) {
		vulture_put_img(x + tile->xmod, y + tile->ymod, tile->graphic);
		return;
	}

	scaled = vulture_scale_surface_int(tile->graphic, vulture_ui_scale);
	if (!scaled) {
		vulture_put_img(x + tile->xmod, y + tile->ymod, tile->graphic);
		return;
	}

	vulture_put_img(x + tile->xmod * vulture_ui_scale,
			y + tile->ymod * vulture_ui_scale, scaled);
	SDL_FreeSurface(scaled);
}



/* Single source of truth for "which gametiles[] entry actually supplies the art
 * for this tile id": the shuffled-appearance substitution (an unidentified
 * potion/ring/scroll draws its DESCRIPTION's art, via oc_descr_idx) followed by
 * one config-level redirect.  vulture_get_tile_shaded() and
 * vulture_dump_tiles() must both go through here. */
int vulture_resolve_tile_id(int tile_id)
{
	if (tile_id < 0)
		return tile_id;

	if (TILE_IS_OBJECT(tile_id))
		tile_id = objects[tile_id].oc_descr_idx;
	else if (TILE_IS_OBJICON(tile_id))
		tile_id = objects[tile_id - ICOTILEOFFSET].oc_descr_idx + ICOTILEOFFSET;

	if (tile_id >= 0 && tile_id < GAMETILECOUNT
	    && vulture_gametiles[tile_id].ptr != -1)
		tile_id = vulture_gametiles[tile_id].ptr;

	return tile_id;
}


/* The PNG that a RESOLVED tile id draws from ("" if it draws nothing). */
const char *vulture_tile_filename(int resolved_id)
{
	if (resolved_id < 0 || resolved_id >= GAMETILECOUNT)
		return "<out-of-range>";
	return vulture_gametiles[resolved_id].filename.c_str();
}


/* Acceptance-test instrumentation.  No-op unless $VULTURE_TILEDUMP names a file.
 * Writes one block per call: every inventory object and every object lying on
 * the level, with the tile id Vulture ACTUALLY resolves for it and the PNG that
 * supplies the art -- the two values the save/restore test must find unchanged.
 * Also emits the Path A (minimap) tile index for the same squares. */
void vulture_dump_tiles(const char *why)
{
	const char *path = getenv("VULTURE_TILEDUMP");
	static long seq = 0;
	FILE *f;
	struct obj *o;
	int x, y;

	if (!path || !*path || !vulture_gametiles)
		return;
	f = fopen(path, "a");
	if (!f)
		return;

	fprintf(f, "### dump seq=%ld why=%s dlvl=%d\n",
	        seq++, why ? why : "?", (int) depth(&u.uz));

	for (o = gi.invent; o; o = o->nobj) {
		int raw = OBJECT_TO_VTILE(o->otyp);
		int res = vulture_resolve_tile_id(raw);
		fprintf(f, "inv otyp=%d descr_idx=%d raw=%d resolved=%d png='%s' appearance='%s'\n",
		        (int) o->otyp, (int) objects[o->otyp].oc_descr_idx, raw, res,
		        vulture_tile_filename(res),
		        obj_descr[objects[o->otyp].oc_descr_idx].oc_descr
		            ? obj_descr[objects[o->otyp].oc_descr_idx].oc_descr : "-");
	}

	for (x = 0; x < COLNO; x++)
		for (y = 0; y < ROWNO; y++)
			for (o = svl.level.objects[x][y]; o; o = o->nexthere) {
				int raw = OBJECT_TO_VTILE(o->otyp);
				int res = vulture_resolve_tile_id(raw);
				fprintf(f,
				    "map x=%d y=%d otyp=%d descr_idx=%d raw=%d resolved=%d png='%s'\n",
				    x, y, (int) o->otyp,
				    (int) objects[o->otyp].oc_descr_idx, raw, res,
				    vulture_tile_filename(res));
			}

	fprintf(f, "### end seq=%ld\n", seq - 1);
	fclose(f);
}


/* vulture_get_tile is responsible for tile "administration"
* if the tile is already loaded it will return a pointer to it
* otherwise it loads the tile, stores the pointer and returns it */
vulture_tile * vulture_get_tile_shaded(int tile_id, int shadelevel)
{
	vulture_tile *tile;
	int shaded_id = tile_id + shadelevel * GAMETILECOUNT;

	if (tile_id < 0)
		return NULL;

	/* shuffled-appearance substitution + config redirect (shared with the
	 * acceptance-test dump so the two can never drift apart) */
	tile_id = vulture_resolve_tile_id(tile_id);


	/* specialized load functions: second */
	/* if you are invisible you have the V_TILE_PLAYER_INVIS. here we give that tile a meaning */
	if (tile_id == V_MISC_PLAYER_INVIS)
		tile = vulture_make_alpha_player_tile(u.umonnum, canseeself() ? 0.6 : 0.35);

	else
		/* if shadelevel == 0 then shaded_id == tile_id */
		tile = vulture_tilecache_get(shaded_id);

	if (!tile) /* never true if tile_id == V_MISC_PLAYER_INVIS */
	{
		if (shadelevel > 0)
		{
			/* Shade a copy of the unshaded tile. That recursive call has
			 * already substituted the placeholder if the art was missing,
			 * so what comes back is either drawable or NULL - and
			 * vulture_shade_tile() tolerates NULL either way. */
			tile = vulture_shade_tile(vulture_get_tile_shaded(tile_id, 0),
			                          shadelevel);
		}
		else
			tile = vulture_load_tile(tile_id);
	}

	/* Art that is missing or failed to decode must become the CONSPICUOUS
	 * error tile. Never a crash, and never nothing: gamedata ships as ~2400
	 * loose PNGs with no DLB, so a single absent or truncated file used to
	 * take the whole game down through vulture_shade_tile(), and silently
	 * drawing invisible floor would hide exactly the problem this port is
	 * supposed to make obvious.
	 *
	 * A placeholder cannot stand in for itself, so when the missing tile IS
	 * the placeholder there is nothing left to substitute and the NULL simply
	 * propagates to callers, all of which skip it. */
	if (!vulture_tile_is_drawable(tile))
	{
		int err_id = vulture_error_tile_for(tile_id);

		/* warn once per tile, not once per frame: this sits in the draw
		 * path, so an unconditional log would grow without bound.
		 * (GAMETILECOUNT is a runtime value - WALTILECOUNT reads
		 * vulture_typecount[] - so this cannot be a fixed array.) */
		static std::set<int> warned;

		if (warned.insert(tile_id).second)
			vulture_write_log(V_LOG_ERROR, __FILE__, __LINE__,
				"tile_id %d ('%s') has no usable graphic; drawing the error "
				"tile instead\n", tile_id, vulture_tile_filename(tile_id));

		vulture_free_tile(tile);
		tile = NULL;

		if (err_id != tile_id)
		{
			/* Load a FRESH copy rather than reusing the cached placeholder:
			 * vulture_tilecache_discard() frees every slot it holds, so
			 * parking one pointer under several ids would free it twice. */
			tile = vulture_load_tile(err_id);
			if (tile && shadelevel > 0)
			{
				vulture_tile *shaded = vulture_shade_tile(tile, shadelevel);

				vulture_free_tile(tile);
				tile = shaded;
			}
		}
	}

	vulture_tilecache_add(tile, shaded_id);

	return tile;
}


/* vulture_load_tile is the actual tile loader
* it returns a pointer to the tile; the caller is expected to free it */
vulture_tile * vulture_load_tile(int tile_id)
{
	vulture_tile * newtile;
	char * data;
	FILE *fp;
	int fsize;

	/* if data_len is 0 the tile doesn't have a graphic */
	if (vulture_gametiles[tile_id].filename.empty())
  {
    vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Missing Tile! Unable to load tile_id: %d\n", tile_id);
		return NULL;
  }

	fp = fopen(vulture_gametiles[tile_id].filename.c_str(), "rb");
	if (!fp)
		return NULL;

	/* obtain file size. */
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	rewind(fp);

	/* a truncated or empty file has nothing to decode */
	if (fsize <= 0)
	{
		fclose(fp);
		return NULL;
	}

	/* load the tile */
	data = new char[fsize];
	fread(data, fsize, 1, fp);

	newtile = new vulture_tile;

	newtile->graphic = vulture_load_surface(data, fsize);
	newtile->xmod = vulture_gametiles[tile_id].hs_x;
	newtile->ymod = vulture_gametiles[tile_id].hs_y;

	delete[] data;
	fclose(fp);

	/* A corrupt PNG leaves graphic == NULL. Returning the tile anyway would
	 * hand every caller a landmine - vulture_shade_tile() dereferences
	 * graphic->w unconditionally - so fail here and let the caller
	 * substitute the error tile. */
	if (!newtile->graphic)
	{
		vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__,
			"Tile %d ('%s') failed to decode\n", tile_id,
			vulture_gametiles[tile_id].filename.c_str());
		delete newtile;
		return NULL;
	}

	if (TILE_IS_WALL(tile_id))
		vulture_set_tile_alpha(newtile, vulture_opts.wall_opacity);

	return newtile;
}


/* darken a tile by scaling RGB values; alpha channel is preserved so
 * transparent pixels (outside the isometric diamond) stay transparent.
 * The old SDL1 approach of blitting a semi-transparent black overlay
 * breaks under SDL2 because SDL_BLENDMODE_BLEND also modifies the
 * destination alpha, making transparent corners visible as dark rects. */
static inline vulture_tile * vulture_shade_tile(vulture_tile *orig, int shadelevel)
{
	vulture_tile *tile;

	/* Nothing to shade. This is the crash that used to take the game down:
	 * every dark square goes through here, so one missing floor or wall PNG
	 * was fatal rather than merely ugly. */
	if (!vulture_tile_is_drawable(orig))
		return NULL;

	tile = new vulture_tile;

	tile->xmod = orig->xmod;
	tile->ymod = orig->ymod;
	tile->graphic = vulture_get_img_src(0,0, orig->graphic->w-1, orig->graphic->h-1, orig->graphic);

	int scale = shade_scale[shadelevel];
	unsigned char *pixels = (unsigned char *)tile->graphic->pixels;

	for (int y = 0; y < tile->graphic->h; y++) {
		for (int x = 0; x < tile->graphic->w; x++) {
			int idx = y * tile->graphic->pitch + x * 4;
			pixels[idx]     = (pixels[idx]     * scale) >> 8;
			pixels[idx + 1] = (pixels[idx + 1] * scale) >> 8;
			pixels[idx + 2] = (pixels[idx + 2] * scale) >> 8;
			/* pixels[idx + 3] (alpha) unchanged */
		}
	}

	return tile;
}


void vulture_put_tilehighlight(int x, int y, int tile_id)
{
	vulture_tile *tile;
	unsigned int *srcdata, *destdata, alpha;
	SDL_Surface *highlight;
	SDL_PixelFormat *pxf;
	int i, j;

	if (tile_id < 0)
		return;

	/* get the base tile; it is the error placeholder if the art was missing,
	 * and NULL only if the placeholder itself could not be loaded */
	tile = vulture_get_tile_shaded(tile_id, 0);
	if (!vulture_tile_is_drawable(tile))
		return;

	pxf = tile->graphic->format;

	highlight = SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_SRCALPHA, tile->graphic->w, tile->graphic->h,
									pxf->BitsPerPixel, pxf->Rmask, pxf->Gmask, pxf->Bmask, pxf->Amask);

	srcdata = (unsigned int *)tile->graphic->pixels;
	destdata = (unsigned int *)highlight->pixels;

	for (i = 0; i < highlight->h; i++)
	{
		for (j = 0; j < highlight->w; j++)
		{
			alpha = (srcdata[i * highlight->w + j] & pxf->Amask) >> pxf->Ashift;
			/*respect src transparency, but cap it at 50% */
			alpha = (alpha < 0x80) ? alpha : 0x80;

			if (srcdata[i * highlight->w + j] & ~(pxf->Amask))
			{
				destdata[i * highlight->w + j] |= alpha << pxf->Ashift;
				destdata[i * highlight->w + j] |= 0x20 << pxf->Rshift;
				destdata[i * highlight->w + j] |= 0x80 << pxf->Gshift;
				destdata[i * highlight->w + j] |= 0xff << pxf->Bshift;
			}
		}
	}

	/* draw the highlight */
	vulture_put_img(x + tile->xmod, y + tile->ymod, highlight);

	/* free the highlight. no caching, as this shouldn't be a hot path */
	SDL_FreeSurface(highlight);
}


int vulture_load_gametiles(void)
{
  std::string filename;
	FILE * fp;

	/* load gametiles.bin */
	filename = vulture_make_filename(V_CONFIG_DIRECTORY, "", "vulture_tiles.conf");
	fp = fopen(filename.c_str(), "rb");
	if (!fp)
	{
		printf("FATAL: Could not read tile configuration (vulture_tiles.conf) file: %s", strerror(errno));
		exit(1);
	}

	vulture_parse_tileconf(fp, &vulture_gametiles);

	fclose(fp);

	/* initialize the two tile arrays. must happen after reading the config file,
	* as GAMETILECOUNT and TILEARRAYLEN are not know before */
	vulture_tilecache = new vulture_tilecache_entry[TILEARRAYLEN];
	memset(vulture_tilecache, 0, TILEARRAYLEN * sizeof(vulture_tilecache_entry));


	return 1;
}


void vulture_unload_gametiles(void)
{
	vulture_tilecache_discard();
	delete vulture_tilecache;
}


static vulture_tile *vulture_make_alpha_player_tile(int monnum, double op_scale)
{
	static int tilenum = -1;
	static double lastscale = 0;
	vulture_tile *montile;
	vulture_tile *tile = NULL;

	tile = vulture_tilecache_get(V_MISC_PLAYER_INVIS);

	/* monnum may change if player polymorphs... */
	if (monnum != tilenum || lastscale != op_scale || !tile)
	{
		lastscale = op_scale;

		montile = vulture_get_tile(MONSTER_TO_VTILE(monnum));
		tile = new vulture_tile;
		if (tile == NULL)
			return montile;

		/* set tile->graphic to be an exact copy of montile->graphic */
		tile->graphic = vulture_get_img_src(0, 0, montile->graphic->w-1,
								montile->graphic->h-1, montile->graphic);
		tile->xmod = montile->xmod;
		tile->ymod = montile->ymod;

		vulture_set_tile_alpha(tile, op_scale);

		tilenum = monnum;
	}

	return tile;
}


/* makes a tile (partially) transparent */
static inline void vulture_set_tile_alpha(vulture_tile *tile, double opacity)
{
	unsigned char * rawdata;
	int x, y;

	/* scale opacity of every pixel. This works nicely, because
	* complete transparency has a numeric value of 0, so it will remain unchanged,
	* while all other pixels transparency will depend on their current transparency */
	rawdata = (unsigned char *)tile->graphic->pixels;
	for (y = 0; y < tile->graphic->h; y++)
		for (x = 0; x < tile->graphic->pitch; x += 4)
			/* multiply the alpha component by the opacity */
			rawdata[y*tile->graphic->pitch+x+3] *= opacity;
}


int vulture_object_to_tile(int obj_id, int x, int y, struct obj *in_obj)
{
	struct obj *obj;
	int tile;
	static int lastfigurine = PM_KNIGHT;
	static int laststatue = PM_KNIGHT;
 
	if (obj_id < 0 || obj_id > NUM_OBJECTS)
		/* we get *seriously weird* input for hallucinated corpses */
		obj_id = CORPSE;

	/* try to find the actual object corresponding to the given obj_id */
	if (in_obj)
		obj = in_obj;
	else {
		if (x >= 0)
			obj = svl.level.objects[x][y];
		else
			obj = gi.invent;

		while (obj && !(obj->otyp == obj_id && (x >= 0 || obj->invlet == y)))
			obj = (x >= 0) ? obj->nexthere : obj->nobj;
	}

	/* all amulets, potions, etc look the same when the player is blind */
	if (obj && Blind && !obj->dknown) {
		switch (obj->oclass) {
			case AMULET_CLASS: return OBJECT_TO_VTILE(AMULET_OF_ESP);
			case POTION_CLASS: return OBJECT_TO_VTILE(POT_WATER);
			case SCROLL_CLASS: return OBJECT_TO_VTILE(SCR_BLANK_PAPER);
			case WAND_CLASS:   return OBJECT_TO_VTILE(WAN_NOTHING);
			case SPBOOK_CLASS: return OBJECT_TO_VTILE(SPE_BLANK_PAPER);
			case RING_CLASS:   return OBJECT_TO_VTILE(RIN_ADORNMENT);
			case GEM_CLASS:
				if (objects[obj_id].oc_material == MINERAL)
					return OBJECT_TO_VTILE(ROCK);
				else
					return OBJECT_TO_VTILE(glassgems[CLR_BLUE]);
		}
	}

	/* figurines and statues get different tiles depending on which monster they represent */
	if (obj_id == STATUE || obj_id == FIGURINE) {
		if (obj_id == FIGURINE) {
			tile = lastfigurine;
			if (obj) {
				tile = obj->corpsenm;
				lastfigurine = tile;
			}

			tile = FIGURINE_TO_VTILE(tile);
		} else /* obj_id == STATUE */ {
			tile = laststatue;
			if (obj) {
				tile = obj->corpsenm;
				laststatue = tile;
			}

			tile = STATUE_TO_VTILE(tile);
		}
		return tile;
	}

	/* prevent visual identification of unknown objects */
	return OBJECT_TO_VTILE(vulture_obfuscate_object(obj_id));
}


/* prevent visual identification of unknown objects */
static int vulture_obfuscate_object(int obj_id)
{
	/* NetHack 5.0.0 added per-class "generic object" pseudo-entries in slots
	 * 1..LAST_GENERIC, ahead of FIRST_OBJECT (include/objects.h).  For an
	 * object whose specific type the hero has not seen (!dknown),
	 * obj_to_glyph() routes through generic_obj_to_glyph(), which encodes the
	 * object CLASS rather than the otyp; glyph_to_obj() hands that class
	 * number straight back to us.  Those slots carry no art, so without this
	 * remap every not-yet-examined gem, potion and spellbook lying on the
	 * floor fell through to object.default => object.ERROR and drew as the
	 * magenta placeholder.
	 *
	 * Substitute the same unidentified stand-ins Vulture already uses for a
	 * blind hero (see vulture_object_to_tile).  For gems use CLEAR/WHITE
	 * glass, not a coloured one: 5.0.0 emits the generic gem precisely
	 * because the colour is not yet established, so asserting a colour would
	 * be showing the player something the game has not told them.  (The
	 * generic entry's own oc_color is CLR_GRAY, for which NetHack has no
	 * glass gem, so glassgems[] cannot be indexed by it directly.)
	 *
	 * This MUST precede the oc_name_known test below: generic slots have
	 * oc_name_known == 0 and would otherwise fall through the switch. */
	if (obj_id > STRANGE_OBJECT && obj_id < FIRST_OBJECT) {
		switch (objects[obj_id].oc_class) {
			case GEM_CLASS:    return glassgems[CLR_WHITE];
			case POTION_CLASS: return POT_WATER;
			case SPBOOK_CLASS: return SPE_BLANK_PAPER;
			case SCROLL_CLASS: return SCR_BLANK_PAPER;
			case AMULET_CLASS: return AMULET_OF_ESP;
			case RING_CLASS:   return RIN_ADORNMENT;
			case WAND_CLASS:   return WAN_NOTHING;
			default:           return STRANGE_OBJECT;
		}
	}

	/* catch known objects */
	if (objects[obj_id].oc_name_known)
		return obj_id;

	/* revert objects that could be identified by their tiles to a generic
	* representation */
	switch (obj_id) {
		case SACK: case OILSKIN_SACK: case BAG_OF_TRICKS: case BAG_OF_HOLDING:
			return SACK;

		case LOADSTONE: case LUCKSTONE: case FLINT: case TOUCHSTONE: 
#ifdef HEALTHSTONE /* only in SlashEM */
		case HEALTHSTONE:
#endif
#ifdef WHETSTONE /* only in SlashEM */
		case WHETSTONE:
#endif
			return FLINT;

		case OIL_LAMP:
		case MAGIC_LAMP:
			return OIL_LAMP;

		case TIN_WHISTLE:
		case MAGIC_WHISTLE:
			return TIN_WHISTLE;

		/* all gems initially look like pieces of glass */
		case DILITHIUM_CRYSTAL: case DIAMOND: case RUBY: case JACINTH:
		case SAPPHIRE: case BLACK_OPAL: case EMERALD: case TURQUOISE:
		case CITRINE: case AQUAMARINE: case AMBER: case TOPAZ: case JET:
		case OPAL: case CHRYSOBERYL: case GARNET: case AMETHYST: case JASPER:
		case FLUORITE: case OBSIDIAN: case AGATE: case JADE:
			/* select the glass tile at runtime: gem colors get randomized */
			return glassgems[objects[obj_id].oc_color];
	}
	/* the vast majority of objects needs no special treatment */
	return obj_id;
}


/* returns the tile for a given monster id */
int vulture_monster_to_tile(int mon_id, int x, int y)
{
	if (Invis && u.ux == x && u.uy == y)
		return V_MISC_PLAYER_INVIS;

#ifdef REINCARNATION
	/* transform all moster tiles to the "uppercase letter" replacements */
	if (Is_rogue_level(&u.uz)) {
		char sym = monsyms[static_cast<int>(mons[mon_id].mlet)];
		if ((mon_id >= 0) && (mon_id < NUMMONS) &&
		    !(x == u.ux && y == u.uy) && isalpha(sym))
			return V_MISC_ROGUE_LEVEL_A + (toupper(sym) - 'A');
	}
#endif

	/* aleaxes are angelic doppelgangers and always look like the player */
	if (mon_id == PM_ALEAX)
		return MONSTER_TO_VTILE(u.umonnum);

	/* we have different tiles for priests depending on their alignment */
	if (mon_id == PM_ALIGNED_PRIEST) {
		struct monst *mtmp = m_at(x, y);
		aligntyp algn = A_NONE;

		/* Minion priests (Moloch's priests in the Sanctum, summoned
		 * roamers, hostile priests on the Astral Plane) have no epri
		 * struct: they carry emin instead.  Blindly dereferencing
		 * EPRI() for them was the longstanding stock-Vulture crash
		 * that made the game unwinnable.  mtmp can also be NULL when
		 * the glyph is only map memory of a departed priest. */
		if (mtmp && has_epri(mtmp))
			algn = EPRI(mtmp)->shralign;
		else if (mtmp && has_emin(mtmp))
			algn = EMIN(mtmp)->min_align;

		switch (algn) {
			case A_LAWFUL:  return V_MISC_LAWFUL_PRIEST;
			case A_CHAOTIC: return V_MISC_CHAOTIC_PRIEST;
			case A_NEUTRAL: return V_MISC_NEUTRAL_PRIEST;
			default:        return V_MISC_UNALIGNED_PRIEST;
		}
	}

	/* 5.0.0 merged succubus+incubus into one PM_AMOROUS_DEMON whose gender
	 * is per-individual and MECHANICAL (seduction only fires cross-gender,
	 * and messages print the gendered name), so the sprite must follow the
	 * monster, not the species.  With no monster on the square (map memory)
	 * fall through to the slot tile, which the config deliberately leaves
	 * on the conspicuous placeholder rather than guessing a gender. */
	if (mon_id == PM_AMOROUS_DEMON) {
		struct monst *mtmp = m_at(x, y);

		if (mtmp)
			return (Mgender(mtmp) == FEMALE) ? V_MISC_SUCCUBUS
			                                 : V_MISC_INCUBUS;
	}

	if ((mon_id >= 0) && (mon_id < NUMMONS))
		return MONSTER_TO_VTILE(mon_id);

	return V_TILE_NONE;
}
