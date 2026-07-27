/* NetHack may be freely redistributed.  See license for details. */

#ifndef _map_h_
#define _map_h_

#include "window.h"

#ifdef VULTURE_NETHACK
#define V_MAX_MAP_SYMBOLS (40 * 30)
#endif
#ifdef VULTURE_SLASHEM
#define V_MAX_MAP_SYMBOLS (40 * 36)
#endif
#ifdef VULTURE_UNNETHACK
#define V_MAX_MAP_SYMBOLS (40 * 31)
#endif

#ifndef V_MAX_MAP_SYMBOLS
#error Variant?
#endif

class levelwin;
class mapdata;

class map : public window
{
  public:
    map(levelwin *p, mapdata *data);
    ~map();
    virtual bool draw();
    virtual eventresult handle_timer_event(window *target, void *result,
                                           int time);
    virtual eventresult handle_mousemotion_event(window *target, void *result,
                                                 int xrel, int yrel,
                                                 int state);
    virtual eventresult handle_mousebuttonup_event(window *target,
                                                   void *result, int mouse_x,
                                                   int mouse_y, int button,
                                                   int state);
    virtual eventresult handle_resize_event(window *target, void *result,
                                            int res_w, int res_h);
    static void toggle(void);

  private:
    point get_mouse_mappos(void);
    SDL_Surface *mapbg;
    SDL_Surface *map_symbols[V_MAX_MAP_SYMBOLS];
    /* Scale the parchment and the symbol atlas were built at, so draw() and
     * get_mouse_mappos() can never disagree with the art.  This is the map's
     * own zoom (>= vulture_ui_scale), chosen in the constructor to fill as
     * much of the parent as an integer factor allows - not a copy of the
     * global UI scale. */
    int ui_scale;
    mapdata *map_data;
};

extern window *mapwin;

#endif
