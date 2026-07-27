/* NetHack may be freely redistributed.  See license for details. */

#ifndef _enhancebutton_h_
#define _enhancebutton_h_

#include "window.h"
#include <SDL.h>

#define V_FILENAME_ENHANCE "enhance"

class enhancebutton : public window
{
  public:
    enhancebutton(window *p);
    virtual ~enhancebutton();
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
    void check_enhance(void);

  private:
    SDL_Surface *image;
    /* vulture_ui_scale `image` was built at, so a runtime scale change (F8 /
     * Interface Options) can be detected in handle_resize_event() */
    int bg_scale;
};

extern enhancebutton *enhancebtn;

#endif
