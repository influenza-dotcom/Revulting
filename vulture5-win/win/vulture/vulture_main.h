/* NetHack may be freely redistributed.  See license for details. */

#ifndef _vulture_main_h
#define _vulture_main_h

#ifdef __cplusplus
extern "C" {
#endif

#include "hack.h"


extern struct window_procs vulture_procs;
extern int vulture_whatis_active;


extern void win_vulture_init(int);
extern int vulture_find_menu_accelerator(char *used_accelerators);
extern void vulture_bail(const char *mesg);


/* external declarations */
extern void vulture_init_nhwindows(int *, char **);
extern void vulture_get_nh_event() ;
extern void vulture_exit_nhwindows(const char *);
extern void vulture_suspend_nhwindows(const char *);
extern void vulture_resume_nhwindows();
extern winid vulture_create_nhwindow(int);
extern void vulture_clear_nhwindow(winid);
extern void vulture_display_nhwindow(winid, boolean);
extern void vulture_dismiss_nhwindow(winid);
extern void vulture_destroy_nhwindow(winid);
extern void vulture_curs(winid,int,int);
extern void vulture_putstr(winid, int, const char *);
extern void vulture_display_file(const char *, boolean);
extern void vulture_start_menu(winid, unsigned long);
extern const glyph_info *vulture_menu_glyph(int);
extern void vulture_add_menu(int, const glyph_info *, const ANY_P *,
			char, char, int, int, const char *, unsigned int);
extern void vulture_end_menu(winid, const char *);
extern int vulture_select_menu(winid, int, MENU_ITEM_P **);
extern char vulture_message_menu(char,int,const char *);
extern void vulture_update_inventory(int);
extern void vulture_mark_synch();
extern void vulture_wait_synch();
#ifdef CLIPPING
extern void vulture_cliparound(int, int);
#endif
#ifdef POSITIONBAR
extern void vulture_update_positionbar(char *);
#endif
#ifdef VULTURE_NETHACK_3_6_0
extern void vulture_print_glyph(winid, coordxy, coordxy,
			const glyph_info *, const glyph_info *);
#else
extern void vulture_print_glyph(winid, coordxy, coordxy, int);
#endif
extern void vulture_raw_print(const char *);
extern void vulture_raw_print_bold(const char *);
extern int vulture_nhgetch();
extern int vulture_nh_poskey(coordxy *, coordxy *, int *);
extern void vulture_nhbell();
extern int vulture_doprev_message();
extern char vulture_yn_function(const char *, const char *, char);
extern void vulture_getlin(const char *,char *);
extern int vulture_get_ext_cmd();
extern void vulture_number_pad(int);
extern void vulture_delay_output();
extern void vulture_start_screen();
extern void vulture_end_screen();
#ifdef VULTURE_NETHACK_3_6_0
extern void vulture_outrip(winid,int,time_t);
#else
extern void vulture_outrip(winid,int);
#endif
extern void vulture_preference_update(const char *);
extern win_request_info *vulture_ctrl_nhwindow(winid, int, win_request_info *);
#ifdef VULTURE_NETHACK_3_6_0
extern char *vulture_getmsghistory(boolean init);
extern void vulture_putmsghistory(const char *msg, boolean restoring_msghist);
#endif
#ifdef CHANGE_COLOR
extern void vulture_change_color(int color,long rgb,int reverse);
#ifdef MAC
extern void vulture_change_background(int white_or_black);
extern short set_vulture_font_name(winid, char *);
#endif
extern char * vulture_get_color_string();
#endif


#ifdef __cplusplus
}
#endif 

#endif /* _vulture_main_h */
