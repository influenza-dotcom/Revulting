/* NetHack may be freely redistributed.  See license for details. */

#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_mixer.h"


#include "vulture_win.h"
#include "vulture_gen.h"
#include "vulture_sound.h"
#include "vulture_opt.h"

/*******************************************************************/


std::vector<vulture_event_sound> vulture_event_sounds;
int vulture_n_background_songs;
int vulture_sound_inited = 0;
int vulture_intros_finished = 0;


/* Sound effects objects */
vulture_cached_sound *vulture_cached_sounds;
int vulture_oldest_cached_sound = 0;

/* Music objects */
/* CD-ROM support removed (SDL2 has no CD-ROM API) */


Mix_Music *vulture_current_music=NULL;

/*******************************************************************/

static void vulture_play_song(std::string midifilename);
static void vulture_play_cd_track(std::string cdtrackname);
static void vulture_play_sound(std::string wavefilename);
static int vulture_is_music_playing(void);

/*******************************************************************/


void vulture_init_sound(void)
{
	int i;

	if (vulture_sound_inited) return;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) == -1) {
		/* init failed */
		vulture_opts.play_effects = 0;
		vulture_opts.play_music = 0;
		vulture_sound_inited = 0;
		return;
	}

	if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 4096) < 0) {
		vulture_opts.play_effects = 0;
		vulture_opts.play_music = 0;
		vulture_sound_inited = 0;
		return;
	}

	Mix_AllocateChannels(16); /* s74: footsteps + effects overlap */
	Mix_ReserveChannels(6);   /* s88: 0 = speech, 1-5 = ambient loops */
	Mix_VolumeMusic(MIX_MAX_VOLUME * 55 / 100); /* s74: quieter music */

	/* Create the sound cache */
	vulture_cached_sounds = new vulture_cached_sound[V_MAX_CACHED_SOUNDS];
	for (i = 0; i < V_MAX_CACHED_SOUNDS; i++) {
		vulture_cached_sounds[i].chunk = NULL;
		vulture_cached_sounds[i].filename = "";
	}

	/* CD-ROM support removed (SDL2 has no CD-ROM API) */

	vulture_sound_inited = 1;
}



#include <ctype.h>

/* s74: whole-word match.  A pattern that starts (ends) with a letter
 * only matches where the message has no letter just before (after) it -
 * so [ cat] no longer fires inside "catch" and [store] not in "restore". */
static int vulture_sound_match(const char *str, const char *pat)
{
	size_t pl = strlen(pat);
	const char *p;
	int lb, rb;

	if (!pl)
		return 0;
	lb = isalpha((unsigned char) pat[0]);
	rb = isalpha((unsigned char) pat[pl - 1]);
	for (p = strstr(str, pat); p; p = strstr(p + 1, pat)) {
		if (lb && p > str && isalpha((unsigned char) p[-1]))
			continue;
		if (rb && isalpha((unsigned char) p[pl]))
			continue;
		return 1;
	}
	return 0;
}

/* tries to play a sound matching the given str */
void vulture_play_event_sound(const char * str)
{
	unsigned int i;


	/* search the configured sounds for one that matches str */
	for (i = 0; i < vulture_event_sounds.size(); i++) {
		if (vulture_sound_match(str, vulture_event_sounds[i].searchpattern)) {
      unsigned int effect_enum( rand()%vulture_event_sounds[i].filenames.size() );
			switch (vulture_event_sounds[i].soundtype)
			{
				case V_EVENT_SOUND_TYPE_SND:
					vulture_play_sound(vulture_event_sounds[i].filenames[effect_enum]);
					break;

				case V_EVENT_SOUND_TYPE_MUS:
					vulture_play_song(vulture_event_sounds[i].filenames[effect_enum]);
					break;

				case V_EVENT_SOUND_TYPE_CD_AUDIO:
					vulture_play_cd_track(vulture_event_sounds[i].filenames[effect_enum]);
					break;

				case V_EVENT_SOUND_TYPE_RANDOM_SONG:
					vulture_play_ambient_sound(1);
					break;
			}

			break;
		}
	}
}


static int vmz_zone = -1;     /* s74: current music zone, -1 = not started */
static int vmz_silenced = 0;  /* s89: the hero died - no more music */
static int vmz_volume = MIX_MAX_VOLUME * 55 / 100;

void vulture_play_ambient_sound(int force_play)
{
	int k;
	char tempbuffer[256];

	/* s74: once the game feeds music zones, it owns the background music */
	if (vmz_zone >= 0 || vmz_silenced)
		return;

	if ((!force_play) && (vulture_is_music_playing()))
		return;

	if (force_play)
		vulture_stop_music();

	k = (rand() >> 4) % vulture_n_background_songs;

	sprintf(tempbuffer, "nhfe_music_background%03d", k);
	vulture_play_event_sound(tempbuffer);
}


static void vulture_play_song(std::string midifilename)
{
	if (!vulture_opts.play_music)
		return;

	if (vulture_current_music)
		Mix_FreeMusic(vulture_current_music);

	vulture_current_music = Mix_LoadMUS(midifilename.c_str());
	Mix_PlayMusic(vulture_current_music,0);
}


static void vulture_play_cd_track(std::string cdtrackname)
{
	/* CD-ROM support removed (SDL2 has no CD-ROM API) */
	(void)cdtrackname;
}


/* s88: stereo position for the next vulture_play_sound() */
static int vsnd_pos_pending = 0;
static Sint16 vsnd_ang = 0;
static Uint8 vsnd_dist = 0;

static void vulture_play_sound(std::string wavefilename)
{
	int i;
	int sound_exists;
	Mix_Chunk *chunk = NULL;

	if (!vulture_opts.play_effects) return;

	/* Check if the sound exists in the sound cache */
	sound_exists = 0;
	for (i = 0; i < V_MAX_CACHED_SOUNDS; i++)
		if (wavefilename == vulture_cached_sounds[i].filename) {
			sound_exists = 1;
			chunk = vulture_cached_sounds[i].chunk;
			break;
		}

	if (!sound_exists) {
		i = vulture_oldest_cached_sound;

		if (vulture_cached_sounds[i].chunk)
			Mix_FreeChunk(vulture_cached_sounds[i].chunk);

		vulture_cached_sounds[i].filename = wavefilename;

		vulture_cached_sounds[i].chunk = Mix_LoadWAV(wavefilename.c_str());
		chunk = vulture_cached_sounds[i].chunk;

		vulture_oldest_cached_sound++;
		if (vulture_oldest_cached_sound >= V_MAX_CACHED_SOUNDS)
			vulture_oldest_cached_sound = 0;
	}

	/* Play sound */
	{
		int ch = Mix_PlayChannel(-1, chunk, 0);

		/* s88: stereo position for vulture_sfx_at(); every other sound
		 * clears any position a previous sound left on this channel */
		if (ch >= 0) {
			if (vsnd_pos_pending)
				Mix_SetPosition(ch, vsnd_ang, vsnd_dist);
			else
				Mix_SetPosition(ch, 0, 0);
		}
	}
}


/* IMPLEMENTATION_LOG s64: mute while the game window is not focused
 * (alt-tabbed or minimised).  Volume, not pause: effects that fire while
 * away play silently instead of piling up, and music keeps its place.
 * Mix_Volume(-1) covers every channel, and SDL_mixer keeps channel and
 * music volume across new plays, so nothing else has to know. */
void vulture_focus_mute(int muted)
{
	static int is_muted = 0;
	int freq, chans;
	Uint16 fmt;

	if (muted == is_muted || !Mix_QuerySpec(&freq, &fmt, &chans))
		return;
	is_muted = muted;
	Mix_Volume(-1, muted ? 0 : MIX_MAX_VOLUME);
	Mix_VolumeMusic(muted ? 0 : vmz_volume); /* s74: zone volume */
}

/*
 * IMPLEMENTATION_LOG s74: location-driven music.  The game calls
 * vulture_music_zone() once per turn with where the hero is; a change
 * fades the new track in (shop music now hands back the moment you walk
 * out).  Shop music is quieter than the rest.  Ordinary dungeon levels
 * rotate calm tracks; the battle tracks only play during fights.
 */
enum { VMZ_DUNGEON, VMZ_SHOP, VMZ_TOWN, VMZ_MINES, VMZ_ORACLE, VMZ_HELL,
       VMZ_BATTLE, VMZ_COUNT };

static void vmz_start(int zone)
{
	static const char *const calm[] = { "ambient1.ogg", "ambient2.ogg",
		"aircaves.ogg", "earthcav.ogg", "watercav.ogg", "lament1.ogg",
		"lament2.ogg" };
	static const char *const battle[] = { "battle1.ogg", "battle2.ogg",
		"battle3.ogg" };
	const char *file;
	int loops = -1;

	switch (zone) {
	case VMZ_SHOP:   file = "shopping.ogg"; break;
	case VMZ_TOWN:   file = "town.ogg"; break;
	case VMZ_MINES:  file = "mines.ogg"; break;
	case VMZ_ORACLE: file = "oracle.ogg"; break;
	case VMZ_HELL:   file = "firecave.ogg"; break;
	case VMZ_BATTLE: file = battle[rand() % 3]; break;
	default:         file = calm[rand() % 7]; loops = 0; break;
	}
	vmz_volume = (zone == VMZ_SHOP) ? MIX_MAX_VOLUME * 30 / 100
	                                : MIX_MAX_VOLUME * 55 / 100;
	if (vulture_current_music) {
		Mix_HaltMusic();
		Mix_FreeMusic(vulture_current_music);
	}
	vulture_current_music = Mix_LoadMUS(
		vulture_make_filename(V_MUSIC_DIRECTORY, "", file).c_str());
	if (!vulture_current_music)
		return;
	Mix_VolumeMusic(vmz_volume);
	Mix_FadeInMusic(vulture_current_music, loops, zone == VMZ_BATTLE ? 400 : 1500);
}

extern "C" void vulture_music_zone(int zone)
{
	if (!vulture_sound_inited || !vulture_opts.play_music || vmz_silenced)
		return;
	if (zone < 0 || zone >= VMZ_COUNT)
		zone = VMZ_DUNGEON;
	if (zone != vmz_zone) {
		vmz_zone = zone;
		vmz_start(zone);
	} else if (!Mix_PlayingMusic()) {
		vmz_start(zone); /* calm track ended: next one */
	}
}

/* s74: footsteps - four stone variants (quiet), or a splash in water */
extern "C" void vulture_sfx_footstep(int water)
{
	static Mix_Chunk *steps[4], *splash;
	static int loaded = 0;
	Mix_Chunk *c;

	if (!vulture_sound_inited || !vulture_opts.play_effects)
		return;
	if (!loaded) {
		char name[16];
		int i;

		for (i = 0; i < 4; i++) {
			sprintf(name, "step%d.wav", i + 1);
			steps[i] = Mix_LoadWAV(vulture_make_filename(V_SOUND_DIRECTORY, "", name).c_str());
			if (steps[i])
				Mix_VolumeChunk(steps[i], MIX_MAX_VOLUME * 35 / 100);
		}
		splash = Mix_LoadWAV(vulture_make_filename(V_SOUND_DIRECTORY, "", "splash.wav").c_str());
		if (splash)
			Mix_VolumeChunk(splash, MIX_MAX_VOLUME * 45 / 100);
		loaded = 1;
	}
	c = water ? splash : steps[rand() % 4];
	if (c)
		Mix_PlayChannel(-1, c, 0);
}


void vulture_stop_music(void)
{
	if (vulture_current_music) Mix_FreeMusic(vulture_current_music);
	vulture_current_music = NULL;
}


#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
int vulture_toggle_sound(void)
{
	static int muted = 0;
	if (muted) {
		Mix_Volume(-1, MIX_MAX_VOLUME);
		Mix_VolumeMusic(MIX_MAX_VOLUME);
		muted = 0;
	} else {
		Mix_Volume(-1, 0);
		Mix_VolumeMusic(0);
		muted = 1;
	}
	return muted;
}
}
#endif


static int vulture_is_music_playing(void)
{
	/* Check for external music files (MIDI or MP3) playing */
	if (Mix_PlayingMusic() > 0)
		return 1;

	/* No music playing */
	return 0;
}

/* s80: action sounds called straight from the game code (pick up, drop,
 * wield, armour, eat, quaff, read, zap, throw, loot, stairs, fall, pray) -
 * they play on the action itself, whatever the message says.  name is a
 * file in sound/ without ".wav". */
extern "C" void vulture_sfx(const char *name)
{
	char file[64];

	if (!vulture_sound_inited || !vulture_opts.play_effects || !name)
		return;
	snprintf(file, sizeof file, "%s.wav", name);
	vulture_play_sound(vulture_make_filename(V_SOUND_DIRECTORY, "", file));
}

/*
 * IMPLEMENTATION_LOG s88: stereo.  A map offset from the hero (dx, dy) is
 * turned into an angle the way the map is drawn on screen - isometric, and
 * through the current view rotation - so "to the left on screen" is heard
 * on the left.  0 = straight up the screen, 90 = right.  Distance: ~22 per
 * square, 255 max.
 */
extern int vulture_view_rot;

static void vsnd_dir(int dx, int dy, Sint16 *ang, Uint8 *dist)
{
	int a = dx, b = dy, r, t;
	double sx, sy, deg, d;

	for (r = (4 - (vulture_view_rot & 3)) & 3; r > 0; r--) {
		t = a;
		a = -b;
		b = t;
	}
	sx = a - b;                 /* screen x per V_MAP_XMOD */
	sy = (a + b) * 22.0 / 56.0; /* screen y, same units (YMOD/XMOD) */
	deg = (sx == 0 && sy == 0) ? 0.0 : atan2(sx, -sy) * 180.0 / 3.14159265358979;
	if (deg < 0)
		deg += 360.0;
	*ang = (Sint16) deg;
	d = sqrt((double) (dx * dx + dy * dy)) * 22.0;
	*dist = (Uint8) (d > 250.0 ? 250.0 : d);
}

/* a one-shot effect heard from map offset (dx, dy) relative to the hero */
extern "C" void vulture_sfx_at(const char *name, int dx, int dy)
{
	char file[64];

	if (!vulture_sound_inited || !vulture_opts.play_effects || !name)
		return;
	snprintf(file, sizeof file, "%s.wav", name);
	vsnd_dir(dx, dy, &vsnd_ang, &vsnd_dist);
	vsnd_pos_pending = 1;
	vulture_play_sound(vulture_make_filename(V_SOUND_DIRECTORY, "", file));
	vsnd_pos_pending = 0;
}

/*
 * s88: environmental loops on reserved channels 1-5 (lava, water, fountain,
 * sink, wind).  The game calls this every turn with the nearest known
 * source of each kind; a loop fades in when one appears, follows it around
 * the stereo field, and fades out when it's gone.
 */
#define VAMB_N 5
static const char *const vamb_file[VAMB_N] = {
	"amb_lava.wav", "amb_water.wav", "amb_fountain.wav", "amb_sink.wav",
	"amb_wind.wav"
};
static const int vamb_vol[VAMB_N] = { 80, 55, 60, 50, 45 }; /* of 128 */
static Mix_Chunk *vamb_chunk[VAMB_N];
static int vamb_on[VAMB_N];

extern "C" void vulture_ambient(int type, int present, int dx, int dy)
{
	int ch = 1 + type;
	Sint16 ang;
	Uint8 dist;

	if (type < 0 || type >= VAMB_N || !vulture_sound_inited)
		return;
	if (!present || !vulture_opts.play_effects) {
		if (vamb_on[type]) {
			Mix_FadeOutChannel(ch, 900);
			vamb_on[type] = 0;
		}
		return;
	}
	if (!vamb_chunk[type]) {
		vamb_chunk[type] = Mix_LoadWAV(
			vulture_make_filename(V_SOUND_DIRECTORY, "", vamb_file[type]).c_str());
		if (!vamb_chunk[type])
			return;
		Mix_VolumeChunk(vamb_chunk[type], vamb_vol[type]);
	}
	vsnd_dir(dx, dy, &ang, &dist);
	if (!vamb_on[type] || !Mix_Playing(ch)) {
		Mix_FadeInChannel(ch, vamb_chunk[type], -1, 900);
		vamb_on[type] = 1;
	}
	Mix_SetPosition(ch, ang, dist);
}

/* s89: the hero has died for good - fade the music and the environmental
 * loops out and keep them off (no zone changes, no random ambient track,
 * no funeral music on the tombstone screen). */
extern "C" void vulture_music_silence(void)
{
	int ch;

	vmz_silenced = 1;
	if (!vulture_sound_inited)
		return;
	Mix_FadeOutMusic(1200);
	for (ch = 1; ch <= 5; ch++)
		Mix_FadeOutChannel(ch, 1200);
}
