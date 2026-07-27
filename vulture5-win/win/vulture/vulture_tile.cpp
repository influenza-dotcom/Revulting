/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

#include <errno.h>

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
	SDL_FreeSurface(tile->graphic);
	delete tile;
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


void vulture_put_tile_shaded(int x, int y, int tile_id, int shadelevel)
{
	vulture_tile * tile = NULL;

	if (tile_id < 0)
		return;

	tile = vulture_get_tile_shaded(tile_id, shadelevel);

	if (tile != NULL)
		vulture_put_img(x + tile->xmod, y + tile->ymod, tile->graphic);
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
			tile = vulture_get_tile_shaded(tile_id, 0);
			tile = vulture_shade_tile(tile, shadelevel);
		}
		else
			tile = vulture_load_tile(tile_id);
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

	/* load the tile */
	data = new char[fsize];
	fread(data, fsize, 1, fp);

	newtile = new vulture_tile;
	if (!newtile)
	{
		free(data);
		return NULL;
	}

	newtile->graphic = vulture_load_surface(data, fsize);
	newtile->xmod = vulture_gametiles[tile_id].hs_x;
	newtile->ymod = vulture_gametiles[tile_id].hs_y;

	delete data;
	fclose(fp);

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
	vulture_tile *tile = new vulture_tile;

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

	/* get the base tile */
	tile = vulture_get_tile_shaded(tile_id, 0);

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
