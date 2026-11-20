/* NetHack may be freely redistributed.  See license for details. */

#ifndef _inputdialog_h_
#define _inputdialog_h_

#include "mainwin.h"

#include <string>
#include <vector>

class textwin;

class inputdialog : public mainwin
{
  public:
    inputdialog(window *p, std::string caption, int size, int force_x,
                int force_y, const std::string &default_text = "");
    virtual bool draw();
    virtual eventresult handle_mousemotion_event(window *target, void *result,
                                                 int xrel, int yrel,
                                                 int state);
    virtual eventresult handle_keydown_event(window *target, void *result,
                                             int sym, int mod, int unicode);
    virtual eventresult handle_mousebuttonup_event(window *target,
                                                   void *result, int mouse_x,
                                                   int mouse_y, int button,
                                                   int state);
    virtual eventresult handle_resize_event(window *target, void *result,
                                            int res_w, int res_h);
    void copy_input(char *dest);

    /* Vulture mod (IMPLEMENTATION_LOG §44): optional word list for the
       typed '#' prompt.  Non-empty = Tab completes to the longest common
       prefix of the matches, and the headline shows the live matches. */
    void set_completions(const std::vector<std::string> &words);

  private:
    textwin *input;
    int destsize;
    std::vector<std::string> completions;
    std::string base_caption;
    void complete_tab(void);
    void refresh_hint(void);
};

#endif
