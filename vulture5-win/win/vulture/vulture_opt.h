/* NetHack may be freely redistributed.  See license for details. */

#ifndef _vulture_opt_h_
#define _vulture_opt_h_



/* Wall display styles (game option) */
enum vulture_wall_display_styles {
	V_WALL_DISPLAY_STYLE_FULL,
	V_WALL_DISPLAY_STYLE_HALF_HEIGHT,
};


/* this is only a struct to keep config options together;
* hopefully this will prevent them from being defined all over the place  */
struct vulture_optstruct {
	int recenter;
	int play_music;
	int play_effects;
	int play_intro_video;
	/* Show the character picker even when .nethackrc pins role, race,
	 * gender AND alignment.  With this off (the default) a fully specified
	 * character starts immediately; with it on the picker still appears,
	 * pre-selected to those config values, so any of them can be changed
	 * for a single game without editing a file. */
	int ask_character;
	int wall_style;
	int height;
	int width;
	/* Supersampling factor: the game is rendered on a surface render_scale
	 * times larger than the window in each axis and box-filtered down on
	 * present, so render_scale=2 on a 1920x1080 window renders at 3840x2160
	 * and shows FOUR TIMES as much of the level, at half apparent size.
	 * 1 = off, and off is byte-for-byte the historical render path. */
	int render_scale;
	/* s168: present frames through the graphics card (an SDL_Renderer);
	 * falls back to the window surface if no accelerated renderer exists */
	int gpu;
	/* s205: depth of field - the map far above and below you softens, as a
	 * camera close over a diorama sees it.  1 (default) on, 0 off. */
	int depth_blur;
	/* s233: the map through a perspective camera - 0 off (the flat iso
	 * picture), 150 (default) the camera 4.3 screen-heights back, more is
	 * stronger (closer) */
	int perspective;
	/* Overview-map (Tab) zoom.  The parchment is a fixed 640x480 painting;
	 * this is the integer factor its art is built at.  0 = auto: the largest
	 * factor that fits on screen WITHOUT covering the message lines. */
	int map_zoom;
	/* "RPG mode": on death, offer the explore-mode "Die?" second chance in
	 * normal play (savelife() revival on n).  Read by src/end.c through
	 * vulture_no_permadeath().  0 = classic roguelike permadeath. */
	int no_permadeath;
	int fullscreen;
	int show_helptb;
	int show_actiontb;
	int show_minimap;
	int messagelines;
	int no_key_translation;
	int use_standard_inventory;
	int use_standard_object_menus;
	int highlight_cursor_square;
	int debug;
	char macro[6][10];
	/* Full path to hearse.exe.  Empty (the default) disables the bones
	 * exchange entirely.  When set, the game runs it once at exit, after
	 * savebones() has written any bones this game left behind. */
	char hearse_path[512];
	double wall_opacity;
};


extern struct vulture_optstruct vulture_opts;


extern void vulture_read_options(void);
extern "C" {
extern int vulture_iface_opts(void);
}
extern void vulture_write_userconfig(void);

#endif
