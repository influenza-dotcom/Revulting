/* NetHack may be freely redistributed.  See license for details. */

#include <vector>
#include <string>


#include <errno.h>

#include "vulture_win.h"
#include "vulture_gra.h"
#include "vulture_gen.h"
#include "vulture_sdl.h"
#include "vulture_gfl.h"
#include "vulture_txt.h"
#include "vulture_init.h"
#include "vulture_nhplayerselection.h"
#include "vulture_main.h"
#include "vulture_sound.h"
#include "vulture_mou.h"
#include "vulture_tile.h"
#include "vulture_opt.h"

#include "winclass/introwin.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static void vulture_show_intro(std::string introscript_name);
static void vulture_free_winelem(void);
static int vulture_load_winelem(void);


/*
 * (Re)build vulture_winelem - the window borders, corners, tiled centre,
 * checkboxes, radio buttons, scrollbar parts, direction arrows and close
 * button - from graphics/winelem.png at the current vulture_ui_scale.
 *
 * Every consumer of these surfaces (mainwin, menuwin, scrollwin, scrollbar,
 * optionwin, dirdialog, inventory) already derives its layout from their
 * ->w/->h rather than from hardcoded numbers, so upscaling the surfaces here
 * scales both the art AND the chrome geometry that surrounds it, with no
 * change at any of those call sites.
 */
static int vulture_load_winelem(void)
{
	SDL_Surface *image;

	vulture_free_winelem();

	image = vulture_load_graphic(V_FILENAME_WINDOW_STYLE);
	if (image == NULL)
		return FALSE;

	vulture_winelem.corner_tl = vulture_get_img_src(1, 1, 23, 23, image);
	vulture_winelem.border_top = vulture_get_img_src(27, 1, 57, 23, image);
	vulture_winelem.corner_tr = vulture_get_img_src(61, 1, 84, 23, image);
	vulture_winelem.border_left = vulture_get_img_src(1, 27, 23, 54, image);
	vulture_winelem.center = vulture_get_img_src(141, 1, 238, 168, image);
	vulture_winelem.border_right = vulture_get_img_src(61, 27, 84, 54, image);
	vulture_winelem.corner_bl = vulture_get_img_src(1, 58, 23, 82, image);
	vulture_winelem.border_bottom = vulture_get_img_src(27, 58, 57, 82, image);
	vulture_winelem.corner_br = vulture_get_img_src(61, 58, 84, 82, image);
	vulture_winelem.checkbox_off = vulture_get_img_src(1, 107, 17, 123, image);
	vulture_winelem.checkbox_on = vulture_get_img_src(21, 107, 37, 123, image);
	vulture_winelem.checkbox_count = vulture_get_img_src(21, 127, 37, 143, image);
	vulture_winelem.radiobutton_off = vulture_get_img_src(41, 107, 57, 123, image);
	vulture_winelem.radiobutton_on = vulture_get_img_src(61, 107, 77, 123, image);
	vulture_winelem.scrollbar = vulture_get_img_src(81, 107, 97, 123, image);
	vulture_winelem.scrollbutton_down = vulture_get_img_src(101, 107, 117, 123, image);
	vulture_winelem.scrollbutton_up = vulture_get_img_src(121, 107, 137, 123, image);
	vulture_winelem.scroll_indicator = vulture_get_img_src(1, 127, 17, 154, image);
	vulture_winelem.direction_arrows = vulture_get_img_src(242, 1, 576, 134, image);
	vulture_winelem.invarrow_left = vulture_get_img_src(1, 158, 67, 174, image);
	vulture_winelem.invarrow_right = vulture_get_img_src(1, 178, 67, 194, image);
	vulture_winelem.closebutton = vulture_get_img_src(41, 127, 59, 145, image);
	SDL_FreeSurface(image);

	/* UI compensation: no-op at scale 1 */
	vulture_scale_ui_surface(&vulture_winelem.corner_tl);
	vulture_scale_ui_surface(&vulture_winelem.border_top);
	vulture_scale_ui_surface(&vulture_winelem.corner_tr);
	vulture_scale_ui_surface(&vulture_winelem.border_left);
	vulture_scale_ui_surface(&vulture_winelem.center);
	vulture_scale_ui_surface(&vulture_winelem.border_right);
	vulture_scale_ui_surface(&vulture_winelem.corner_bl);
	vulture_scale_ui_surface(&vulture_winelem.border_bottom);
	vulture_scale_ui_surface(&vulture_winelem.corner_br);
	vulture_scale_ui_surface(&vulture_winelem.checkbox_off);
	vulture_scale_ui_surface(&vulture_winelem.checkbox_on);
	vulture_scale_ui_surface(&vulture_winelem.checkbox_count);
	vulture_scale_ui_surface(&vulture_winelem.radiobutton_off);
	vulture_scale_ui_surface(&vulture_winelem.radiobutton_on);
	vulture_scale_ui_surface(&vulture_winelem.scrollbar);
	vulture_scale_ui_surface(&vulture_winelem.scrollbutton_down);
	vulture_scale_ui_surface(&vulture_winelem.scrollbutton_up);
	vulture_scale_ui_surface(&vulture_winelem.scroll_indicator);
	vulture_scale_ui_surface(&vulture_winelem.direction_arrows);
	vulture_scale_ui_surface(&vulture_winelem.invarrow_left);
	vulture_scale_ui_surface(&vulture_winelem.invarrow_right);
	vulture_scale_ui_surface(&vulture_winelem.closebutton);

	return TRUE;
}


static void vulture_free_winelem(void)
{
	SDL_Surface **elem[] = {
		&vulture_winelem.corner_tl,       &vulture_winelem.border_top,
		&vulture_winelem.corner_tr,       &vulture_winelem.border_left,
		&vulture_winelem.center,          &vulture_winelem.border_right,
		&vulture_winelem.corner_bl,       &vulture_winelem.border_bottom,
		&vulture_winelem.corner_br,       &vulture_winelem.checkbox_off,
		&vulture_winelem.checkbox_on,     &vulture_winelem.checkbox_count,
		&vulture_winelem.radiobutton_off, &vulture_winelem.radiobutton_on,
		&vulture_winelem.scrollbar,       &vulture_winelem.scrollbutton_down,
		&vulture_winelem.scrollbutton_up, &vulture_winelem.scroll_indicator,
		&vulture_winelem.direction_arrows,&vulture_winelem.invarrow_left,
		&vulture_winelem.invarrow_right,  &vulture_winelem.closebutton
	};
	unsigned int i;

	for (i = 0; i < sizeof(elem) / sizeof(elem[0]); i++) {
		SDL_FreeSurface(*elem[i]);
		*elem[i] = NULL;
	}
}


/*
 * Rebuild everything that is baked at a particular UI scale, after
 * vulture_opts.render_scale changed at runtime (F8, or the Interface Options
 * menu).  Must run BEFORE the vulture_win_resize() relayout, because that
 * drives handle_resize_event() on every window and those recompute positions
 * from text metrics and chrome sizes.
 *
 * The per-window chrome PNGs (status bar, toolbars, minimap background) are
 * owned by their windows and reload themselves from their own
 * handle_resize_event(); the overview map window is destroyed and recreated by
 * map::toggle(), so it picks the new scale up on its next open.
 */
int vulture_ui_rescale(void)
{
	if (vulture_ui_scale == vulture_render_scale)
		return 0;

	vulture_ui_scale = vulture_render_scale;

	vulture_reload_fonts();
	vulture_load_winelem();

	/* the caller must repaint from scratch - see vulture_win_resize() */
	return 1;
}


/*----------------------------
* function implementaions
*---------------------------- */



/* SDL2 port of the SDL1.2 "Dark Arts" intro movie player.
 * The vendored THEORAPLAY decoder thread converts frames straight to RGBA
 * (SDL2 has no YUV overlays for software surfaces); each frame is wrapped in
 * an SDL_Surface, aspect-fit onto the window's software surface (letterboxed
 * on black) and presented through the port's own refresh path
 * (vulture_refresh() -> SDL_UpdateWindowSurface). */

#include <string.h>
#include <stdlib.h>

#include "vendor/theoraplay/theoraplay.h"

static Uint32 baseticks = 0;

typedef struct AudioQueue
{
    const THEORAPLAY_AudioPacket *audio;
    int offset;
    struct AudioQueue *next;
} AudioQueue;

static volatile AudioQueue *audio_queue = NULL;
static volatile AudioQueue *audio_queue_tail = NULL;

static void vulture_show_darkarts_intro_queue_audio(const THEORAPLAY_AudioPacket *audio)
{
    AudioQueue *item = (AudioQueue *) malloc(sizeof (AudioQueue));
    if (!item)
    {
        THEORAPLAY_freeAudio(audio);
        return;  // oh well.
    } // if

    item->audio = audio;
    item->offset = 0;
    item->next = NULL;

    SDL_LockAudio();
    if (audio_queue_tail)
        audio_queue_tail->next = item;
    else
        audio_queue = item;
    audio_queue_tail = item;
    SDL_UnlockAudio();
}

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    // !!! FIXME: this should refuse to play if item->playms is in the future.
    //const Uint32 now = SDL_GetTicks() - baseticks;
    Sint16 *dst = (Sint16 *) stream;

    while (audio_queue && (len > 0))
    {
        volatile AudioQueue *item = audio_queue;
        AudioQueue *next = item->next;
        const int channels = item->audio->channels;

        const float *src = item->audio->samples + (item->offset * channels);
        int cpy = (item->audio->frames - item->offset) * channels;
        int i;

        if (cpy > (len / (int)sizeof (Sint16)))
            cpy = len / (int)sizeof (Sint16);

        for (i = 0; i < cpy; i++)
        {
            const float val = *(src++);
            if (val < -1.0f)
                *(dst++) = -32768;
            else if (val > 1.0f)
                *(dst++) = 32767;
            else
                *(dst++) = (Sint16) (val * 32767.0f);
        } // for

        item->offset += (cpy / channels);
        len -= cpy * sizeof (Sint16);

        if (item->offset >= item->audio->frames)
        {
            THEORAPLAY_freeAudio(item->audio);
            free((void *) item);
            audio_queue = next;
        } // if
    } // while

    if (!audio_queue)
        audio_queue_tail = NULL;

    if (len > 0)
        memset(dst, '\0', len);
} // audio_callback


/* free any audio packets still sitting in the playback queue.
 * Only call this after SDL_CloseAudio(): the callback is stopped by then,
 * so the queue can be walked without holding the audio lock. */
static void vulture_show_darkarts_intro_drain_queue(void)
{
    volatile AudioQueue *item = audio_queue;
    while (item)
    {
        volatile AudioQueue *next = item->next;
        THEORAPLAY_freeAudio(item->audio);
        free((void *) item);
        item = next;
    }
    audio_queue = NULL;
    audio_queue_tail = NULL;
}

void vulture_show_darkarts_intro(void)
{
  THEORAPLAY_Decoder *decoder = NULL;
  const THEORAPLAY_VideoFrame *video = NULL;
  const THEORAPLAY_AudioPacket *audio = NULL;

  SDL_Event event;
  SDL_AudioSpec spec;

  Uint32 framems = 0;
  int has_audio = 0;
  int audio_opened = 0;

  std::string fname;
  int quit = 0;

  fname = vulture_make_filename(V_MOVIE_DIRECTORY, "", "darkarts.ogv");

  decoder = THEORAPLAY_startDecodeFile(fname.c_str(), 100, THEORAPLAY_VIDFMT_RGBA);
  if (!decoder)
  {
      /* missing or unreadable movie file: log it and carry on with init */
      fprintf(stderr, "Failed to start decoding '%s'!\n", fname.c_str());
      return;
  }

  /* wait for the decoder to examine the streams. Unlike the SDL1 version,
     never spin forever on a broken file, and stay skippable while waiting. */
  while (!quit && THEORAPLAY_isDecoding(decoder) &&
         !THEORAPLAY_isInitialized(decoder))
  {
      while (SDL_PollEvent(&event))
          if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN ||
              event.type == SDL_MOUSEBUTTONDOWN)
              quit = 1;
      SDL_Delay(10);
  }

  has_audio = THEORAPLAY_hasAudioStream(decoder);

  /* wait for the first video frame (and the first audio packet, if the
     movie declares an audio stream). BOUNDED: a corrupt file can declare
     a Vorbis stream yet never produce a packet -- once video is available,
     give up on audio after ~2 seconds instead of spinning forever. */
  {
      const Uint32 waitstart = SDL_GetTicks();

      while (!quit && THEORAPLAY_isDecoding(decoder) &&
             (!video || (has_audio && !audio)))
      {
          if (has_audio && !audio)
          {
              audio = THEORAPLAY_getAudio(decoder);
              if (!audio && video && (SDL_GetTicks() - waitstart) > 2000)
                  has_audio = 0;  /* declared but silent: play without audio */
          }
          if (!video)
              video = THEORAPLAY_getVideo(decoder);

          while (SDL_PollEvent(&event))
              if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN ||
                  event.type == SDL_MOUSEBUTTONDOWN)
                  quit = 1;
          SDL_Delay(10);
      }
  }

  /* if the wait ended (decoder finished/failed) with the declared audio
     stream never yielding a packet, demote to silent playback so the
     audio-open code below can never dereference a NULL packet */
  if (has_audio && !audio)
      has_audio = 0;

  if (quit || !video || THEORAPLAY_decodingError(decoder))
  {
      if (!quit)
          fprintf(stderr, "Could not decode '%s'!\n", fname.c_str());
      if (audio)
          THEORAPLAY_freeAudio(audio);
      if (video)
          THEORAPLAY_freeVideo(video);
      THEORAPLAY_stopDecode(decoder);
      if (vulture_screen)
      {
          SDL_FillRect(vulture_screen, NULL, SDL_MapRGB(vulture_screen->format, 0, 0, 0));
          vulture_refresh();
      }
      return;
  }

  /* nominal frame duration, used by the catch-up frame dropper below.
     (the SDL1 version declared framems but never assigned it, leaving its
     frame dropper dead; this restores the upstream sdlplayer behavior) */
  framems = (video->fps == 0.0) ? 0 : ((Uint32) (1000.0 / video->fps));

  if (has_audio)
  {
      memset(&spec, '\0', sizeof (SDL_AudioSpec));
      spec.freq = audio->freq;
      spec.format = AUDIO_S16SYS;
      spec.channels = audio->channels;
      spec.samples = 2048;
      spec.callback = audio_callback;

      /* vulture_enter_graphics_mode() starts only VIDEO|TIMER and
         vulture_init_sound() (SDL2_mixer) runs after we return, so bring
         the audio subsystem up ourselves. SDL refcounts subsystem inits,
         so leaving it up for the mixer is harmless. The legacy 1.2-style
         SDL_OpenAudio() API still exists in SDL2 and is sufficient here,
         but the device MUST be closed again before this function returns. */
      if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 ||
          SDL_OpenAudio(&spec, NULL) != 0)
      {
          fprintf(stderr, "Audio Init Failed: %s\n", SDL_GetError());
          /* a missing audio device is no reason to drop the movie: play silent */
          has_audio = 0;
          THEORAPLAY_freeAudio(audio);
          audio = NULL;
      }
      else
          audio_opened = 1;
  }

  if (audio_opened)
  {
      while (audio)
      {
          vulture_show_darkarts_intro_queue_audio(audio);
          audio = THEORAPLAY_getAudio(decoder);
      }
      SDL_PauseAudio(0);
  }

  SDL_FillRect(vulture_screen, NULL, SDL_MapRGB(vulture_screen->format, 0, 0, 0));
  vulture_refresh();

  SDL_Delay(100); // give some cpu

  baseticks = SDL_GetTicks();

  while (!quit && THEORAPLAY_isDecoding(decoder))
  {
      const Uint32 now = SDL_GetTicks() - baseticks;

      if (!video)
          video = THEORAPLAY_getVideo(decoder);

      // Play video frames when it's time.
      if (video && (video->playms <= now))
      {
          if ( framems && ((now - video->playms) >= framems) )
          {
              // Skip frames to catch up, but keep track of the last one
              //  in case we catch up to a series of dupe frames, which
              //  means we'd have to draw that final frame and then wait for
              //  more.
              const THEORAPLAY_VideoFrame *last = video;
              while ((video = THEORAPLAY_getVideo(decoder)) != NULL)
              {
                  THEORAPLAY_freeVideo(last);
                  last = video;
                  if ((now - video->playms) < framems)
                      break;
              } // while

              if (!video)
                  video = last;
          } // if

          if (!video)  // do nothing; we're far behind and out of options.
          {
              static int warned = 0;
              if (!warned)
              {
                  warned = 1;
                  fprintf(stderr, "WARNING: Playback can't keep up!\n");
              } // if
          } // if
          else
          {
              /* THEORAPLAY's RGBA converter emits bytes in R,G,B,A memory
                 order (theoraplay_cvtrgb.h), i.e. SDL_PIXELFORMAT_RGBA32 */
              SDL_Surface *frame = SDL_CreateRGBSurfaceWithFormatFrom(
                              (void *) video->pixels,
                              video->width, video->height, 32,
                              video->width * 4, SDL_PIXELFORMAT_RGBA32);
              if (frame)
              {
                  /* aspect-fit into the window, letterboxed on black */
                  SDL_Rect dstrect;
                  int dstw = vulture_screen->w;
                  int dsth = (dstw * (int) video->height) / (int) video->width;
                  if (dsth > vulture_screen->h)
                  {
                      dsth = vulture_screen->h;
                      dstw = (dsth * (int) video->width) / (int) video->height;
                  }
                  dstrect.x = (vulture_screen->w - dstw) / 2;
                  dstrect.y = (vulture_screen->h - dsth) / 2;
                  dstrect.w = dstw;
                  dstrect.h = dsth;

                  /* blend + scale mangles alpha in SDL2 (see the note in
                     vulture_tile.cpp); the movie is opaque, so blit raw */
                  SDL_SetSurfaceBlendMode(frame, SDL_BLENDMODE_NONE);
                  SDL_BlitScaled(frame, NULL, vulture_screen, &dstrect);
                  SDL_FreeSurface(frame);

                  vulture_refresh();
              }
              else
              {
                  static int warned = 0;
                  if (!warned)
                  {
                      warned = 1;
                      fprintf(stderr, "Couldn't wrap video frame: %s\n", SDL_GetError());
                  } // if
              }
          } // else

          if (video)
          {
              THEORAPLAY_freeVideo(video);
              video = NULL;
          }
      } // if
      else  // no new video frame? Give up some CPU.
      {
          SDL_Delay(10);
      } // else

      while ((audio = THEORAPLAY_getAudio(decoder)) != NULL)
      {
          if (audio_opened)
              vulture_show_darkarts_intro_queue_audio(audio);
          else
              THEORAPLAY_freeAudio(audio);
      }

      // Pump the event loop here: skip on any key, mouse click or quit.
      memset(&event, 0, sizeof(event)); // reset event
      while (SDL_PollEvent(&event))
      {
          switch (event.type)
          {
              case SDL_WINDOWEVENT:
                  if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                  {
                      /* a resize invalidates the old window surface (and, at
                         render_scale > 1, the offscreen render surface sized
                         from it - so go through the one helper that owns
                         both, never SDL_GetWindowSurface directly) */
                      vulture_reattach_render_surface();
                      if (!vulture_screen)
                          quit = 1;
                      else
                          SDL_FillRect(vulture_screen, NULL,
                                       SDL_MapRGB(vulture_screen->format, 0, 0, 0));
                  }
                  else if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                           event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                      vulture_refresh();
                  break;

              case SDL_QUIT:
              case SDL_KEYDOWN:
              case SDL_MOUSEBUTTONDOWN:
                  quit = 1;
                  break;
          } // switch
      } // while
  } // while

  /* single cleanup path, shared by completion, skip and failure */
  if (video)
      THEORAPLAY_freeVideo(video);

  if (audio_opened)
  {
      SDL_CloseAudio();  /* must be fully closed before vulture_init_sound() */
      vulture_show_darkarts_intro_drain_queue();
  }

  /* drain whatever the decoder still buffered, then shut it down */
  while ((audio = THEORAPLAY_getAudio(decoder)) != NULL)
      THEORAPLAY_freeAudio(audio);
  while ((video = THEORAPLAY_getVideo(decoder)) != NULL)
      THEORAPLAY_freeVideo(video);
  THEORAPLAY_stopDecode(decoder);

  /* hand a clean black screen back to the normal init sequence */
  if (vulture_screen)
  {
      SDL_FillRect(vulture_screen, NULL, SDL_MapRGB(vulture_screen->format, 0, 0, 0));
      vulture_refresh();
  }
}


void vulture_player_selection(void)
{
	SDL_Surface *logo;
  std::string filename;

	SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);
	/* full-screen chrome, not map art: compensate it like the rest of the UI
	 * so character creation is not a postage stamp at render_scale > 1 */
	logo = vulture_load_graphic_ui(V_FILENAME_CHARACTER_GENERATION);
	if (logo != NULL) {
		vulture_put_img((vulture_screen->w - logo->w) / 2, (vulture_screen->h - logo->h) / 2, logo);
		SDL_FreeSurface(logo);
	}

	vulture_fade_in(0.2);

	vulture_player_selection_internal();

	/* Success! Show introduction. */
	vulture_fade_out(0.2);

	filename = vulture_make_filename(V_CONFIG_DIRECTORY, "", V_FILENAME_INTRO_SCRIPT);

	if (flags.legacy)
		vulture_show_intro(filename);
}


void vulture_askname(void)
{
	int done;
	char inputbuffer[256];

	/* "web_user" was a hardcoded default inherited from the Emscripten fork.
	 * NetHack has already seeded svp.plname from NETHACKOPTIONS, the config
	 * file or the OS login name, so offer that instead. */
	/* vulture_screen is the RENDER surface, so this bottom-relative offset is
	 * in render pixels while the dialog it positions grew by vulture_ui_scale;
	 * scale it so the two agree.  In practice inputdialog overrides force_y
	 * with a centred position whenever parent == NULL, which is the case here
	 * (levelwin does not exist yet at askname time), so this is belt and
	 * braces - but it must not be left as the one place that would put a
	 * double-height dialog 170 render pixels from the bottom of a 2160px
	 * surface if that ever stopped being true. */
	done = vulture_get_input(-1, vulture_screen->h - V_UI(170),
						"What is your name?", inputbuffer,
						svp.plname[0] ? svp.plname : "");
	if (!done)
		/* Player pressed ESC during the name query, so quit the game */
		vulture_bail(NULL);

	strncpy(svp.plname, inputbuffer, 32);

	/* do not fade out if user didn't enter anything, we will come back here */
	if (svp.plname[0])
		vulture_fade_out(0.2);
}


static void vulture_show_intro(std::string introscript_name)
{
	FILE *f;
	char buffer[1024];
	unsigned int nr_scenes, lineno;
  std::string line;
  std::vector<std::string> imagenames;
  std::vector< std::vector<std::string> > subtitles;
	introwin *iw;
    int dummy;

	/* read intro script */
	f = fopen(introscript_name.c_str(), "rb");
	if (f == NULL) {
		vulture_write_log(V_LOG_NOTE, NULL, 0, "intro script %s not found\n",
		                   introscript_name.c_str());
		return;
	}
	
	lineno = nr_scenes = 0;
	while (fgets(buffer, 1024, f)) {
		lineno++;
		
		if (buffer[0] == '%') {
			/* new scene */
			line = std::string(&buffer[1]);
			trim(line);
			
			if (line.length() == 0)
				continue;
			
			nr_scenes++;
			
			if (line.substr(line.length() - 4, 4) != ".png")
				vulture_write_log(V_LOG_NOTE, NULL, 0, "scene image %s not png?", line.c_str());
			
			/* trim off the file extension */
			line = line.substr(0, line.length() - 4);
			
			imagenames.push_back(line);
			subtitles.resize(nr_scenes);
			
		} else {
			/* text lines for current scene */
			if (nr_scenes == 0) {
				vulture_write_log(V_LOG_NOTE, NULL, 0, "subtitle without a preceding scene in line %d of intro script %s\n", lineno, introscript_name.c_str());
				continue;
			}
			
			line = std::string(buffer);
			trim(line);
			
			subtitles[nr_scenes - 1].push_back(line);
		}
	}

	if (nr_scenes == 0) {
		vulture_write_log(V_LOG_NOTE, NULL, 0, "no scenes found in intro script %s\n",
		                   introscript_name.c_str());
		return;
	}
	
	/* display intro */
	iw = new introwin(NULL, imagenames, subtitles);
	vulture_event_dispatcher(&dummy, V_RESPOND_INT, iw);
	delete iw;
}


static void vulture_init_colors()
{
	/* set up the colors used in the game
	* the only good way to do this without needing graphics to have been loaded first
	* is to create a surface here which we then put into display format + alpha ourselves */
	/* SDL2: SDL_DisplayFormatAlpha is gone; use the screen's format directly */
	SDL_Surface * pixel = SDL_CreateRGBSurface(0, 1, 1, 32,
								0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);

	if (vulture_screen)
		vulture_px_format = SDL_AllocFormat(vulture_screen->format->format);
	else
		vulture_px_format = SDL_AllocFormat(pixel->format->format);

	/* The screen surface may not have an alpha channel (e.g. Emscripten returns
	 * XRGB8888). We need alpha for tile transparency, so add an alpha mask
	 * in the unused byte position. */
	if (vulture_px_format->Amask == 0 && vulture_px_format->BitsPerPixel == 32) {
		Uint32 used = vulture_px_format->Rmask | vulture_px_format->Gmask | vulture_px_format->Bmask;
		vulture_px_format->Amask = ~used;
		Uint32 m = vulture_px_format->Amask;
		int shift = 0;
		while (m && !(m & 1)) { shift++; m >>= 1; }
		vulture_px_format->Ashift = shift;
		vulture_px_format->Aloss = 0;
	}

	SDL_FreeSurface(pixel);

	CLR32_BLACK      = SDL_MapRGBA(vulture_px_format, 0,0,0, 0xff);
	CLR32_BLACK_A30  = SDL_MapRGBA(vulture_px_format, 0,0,0, 0x50);
	CLR32_BLACK_A50  = SDL_MapRGBA(vulture_px_format, 0,0,0, 0x80);
	CLR32_BLACK_A70  = SDL_MapRGBA(vulture_px_format, 0,0,0, 0xB0);
	CLR32_GREEN      = SDL_MapRGBA(vulture_px_format, 0x57, 0xff, 0x57, 0xff);
	CLR32_YELLOW     = SDL_MapRGBA(vulture_px_format, 0xff, 0xff, 0x57, 0xff);
	CLR32_ORANGE     = SDL_MapRGBA(vulture_px_format, 0xff, 0xc7, 0x3b, 0xff);
	CLR32_RED        = SDL_MapRGBA(vulture_px_format, 0xff, 0x23, 0x07, 0xff);
	CLR32_GRAY20     = SDL_MapRGBA(vulture_px_format, 0xb7, 0xab, 0xab, 0xff);
	CLR32_GRAY70     = SDL_MapRGBA(vulture_px_format, 0x53, 0x53, 0x53, 0xff);
	CLR32_GRAY77     = SDL_MapRGBA(vulture_px_format, 0x43, 0x3b, 0x3b, 0xff);
	CLR32_PURPLE44   = SDL_MapRGBA(vulture_px_format, 0x4f, 0x43, 0x6f, 0xff);
	CLR32_LIGHTPINK  = SDL_MapRGBA(vulture_px_format, 0xcf, 0xbb, 0xd3, 0xff);
	CLR32_LIGHTGREEN = SDL_MapRGBA(vulture_px_format, 0xaa, 0xff, 0xcc, 0xff);
	CLR32_BROWN      = SDL_MapRGBA(vulture_px_format, 0x9b, 0x6f, 0x57, 0xff);
	CLR32_WHITE      = SDL_MapRGBA(vulture_px_format, 0xff, 0xff, 0xff, 0xff);
	CLR32_BLESS_BLUE = SDL_MapRGBA(vulture_px_format, 0x96, 0xdc, 0xfe, 0x60);
	CLR32_CURSE_RED  = SDL_MapRGBA(vulture_px_format, 0x60, 0x00, 0x00, 0x50);
	CLR32_GOLD_SHADE = SDL_MapRGBA(vulture_px_format, 0xf0, 0xe0, 0x57, 0x40);
}


int vulture_init_graphics(void)
{
	int all_ok = TRUE;
  std::string fullname;
	int font_loaded = 0;


	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Initializing filenames\n");
	vulture_init_gamepath();


	/* Read options file */
	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Reading Vulture's options\n");
	vulture_read_options();


	/* Enter graphics mode */
	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Enter graphics mode\n");
	vulture_enter_graphics_mode();


	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Initializing screen buffer\n");


	vulture_init_colors();

	/*
	 * From here on the UI is built at the active supersampling factor: the
	 * whole frame is box-filtered down by vulture_render_scale on present, so
	 * fonts must be opened that many times larger and every piece of window
	 * chrome upscaled by the same factor, or the UI reaches the screen at 1/s
	 * of its apparent size.  vulture_enter_graphics_mode() has already settled
	 * vulture_render_scale (it degrades to 1 if the offscreen surface could
	 * not be allocated), so this is the first point at which the real factor
	 * is known.  The MAP is deliberately left uncompensated - see
	 * vulture_types.h.
	 */
	vulture_ui_scale = vulture_render_scale;

	/* Load fonts */
	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Initializing fonts\n");

	/* try custom font first */
	if (iflags.wc_font_text) {
		if (access(iflags.wc_font_text, R_OK) == 0) {
			font_loaded = 1;
			font_loaded &= vulture_load_font(V_FONT_SMALL, iflags.wc_font_text, 0, 13); /* s200: 12 */
			font_loaded &= vulture_load_font(V_FONT_LARGE, iflags.wc_font_text, 0, 15); /* s200: 14 */
		}
		else
			printf("Could not access %s: %s\n", iflags.wc_font_text, strerror(errno));
	}

	if (!font_loaded) {/* fallback to default font */
		font_loaded = 1;
		/* add the path to the filename */
		fullname = vulture_make_filename(V_FONTS_DIRECTORY, "", V_FILENAME_FONT);

		font_loaded &= vulture_load_font(V_FONT_SMALL, fullname.c_str(), 0, 13); /* s200: 12 */
		font_loaded &= vulture_load_font(V_FONT_LARGE, fullname.c_str(), 0, 15); /* s200: 14 */
	}

	all_ok &= font_loaded;

	/* Load window style graphics */
	all_ok &= vulture_load_winelem();

	/* Initialize tile bitmaps */
	vulture_load_gametiles();


	/* make sure the cursor is not NULL when we try to draw for the first time */
	vulture_mouse_init();

	vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__, "Vulture's window system ready.\n");

	vulture_set_draw_region(0, 0, vulture_screen->w, vulture_screen->h);
	SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);

	return all_ok;
}



void vulture_destroy_graphics(void)
{
	unsigned int i;

	/* free tilearrays, related data */
	vulture_unload_gametiles();

	/* clean up fonts */
	vulture_free_fonts();

	/* unload mouse & tooltip backgrounds, tooltip text and tootltip surface */
	vulture_mouse_destroy();
	
	vulture_eventstack_destroy();

	/* free window elements */
	vulture_free_winelem();

	/* free sound descriptions */
	for (i = 0; i < vulture_event_sounds.size(); i++)
		free (vulture_event_sounds[i].searchpattern);

	/* misc small stuff */
	SDL_FreeFormat(vulture_px_format);
}
