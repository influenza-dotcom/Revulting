/* NetHack may be freely redistributed.  See license for details. */

#ifndef _vulture_init_h_
#define _vulture_init_h_


/* exported functions */
extern void          vulture_show_darkarts_intro(void);
extern int           vulture_init_graphics(void);
extern void          vulture_destroy_graphics(void);

extern void vulture_askname(void);
extern void vulture_player_selection(void);

/* Rebuild fonts + window chrome after vulture_render_scale changed at runtime.
 * Returns non-zero if it actually rebuilt anything, i.e. the scale really did
 * change - in which case the caller owes the screen a full repaint, because
 * everything currently on it was drawn at the old factor. */
extern int           vulture_ui_rescale(void);

#endif
