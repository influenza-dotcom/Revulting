/* NetHack may be freely redistributed.  See license for details. */

#include <cstring>

#include "vulture_mou.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "button.h"
#include "choicedialog.h"

choicedialog::choicedialog(window *p, std::string question,
                           const char *choices, char defchoice)
    : mainwin(p)
{
    button *btn;
    int nbuttons = 0, longdesc = 0;
    int i, len;
    const char *str = (char *) choices;
    char *btncaption;

    defbutton = NULL;
    caption = question;
    nbuttons = strlen(choices);

    /* a very common case is "yn" queries. Improve that to a yes/no query*/
    if (strncmp(choices, "yn", 3) == 0) {
        str = "yes\0no";
        longdesc = 1;
    } else if (strncmp(choices, "ynq", 4) == 0) {
        str = "yes\0no\0quit";
        longdesc = 1;
    } else if (strncmp(choices, "c", 1) == 0) {
        str = "continue";
        longdesc = 1;
    }

    for (i = 0; i < nbuttons; i++) {
        len = 1;
        if (longdesc)
            len = strlen(str);

        btncaption = new char[len + 1];
        strncpy(btncaption, str, len);
        btncaption[len] = '\0';

        btn = new button(this, btncaption, i, str[0]);
        if (str[0] == defchoice)
            defbutton = btn;

        delete btncaption;

        if (longdesc)
            str += len;

        str++;
    }

    this->defchoice = defchoice;

    mainwin::layout();
}

bool
choicedialog::draw()
{
    return mainwin::draw();
}

eventresult
choicedialog::handle_mousemotion_event(window *target, void *result, int xrel,
                                       int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
choicedialog::handle_mousebuttonup_event(window *target, void *result,
                                         int mouse_x, int mouse_y, int button,
                                         int state)
{
    if (button == SDL_BUTTON_LEFT && target->accelerator) {
        *(char *) result = (char) target->accelerator;
        return V_EVENT_HANDLED_FINAL;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
choicedialog::handle_keydown_event(window *target, void *result, int sym,
                                   int mod, int unicode)
{
    switch (sym) {
    case SDLK_KP_ENTER:
    case SDLK_RETURN:
        target = defbutton;
        if (target) {
            *(char *) result = (char) target->accelerator;
            return V_EVENT_HANDLED_FINAL;
        }
        break;

    case SDLK_ESCAPE:
        if (find_accel('q').size() > 0) {
            *(char *) result = 'q';
            return V_EVENT_HANDLED_FINAL;
        } else if (find_accel('n').size() > 0) {
            *(char *) result = 'n';
            return V_EVENT_HANDLED_FINAL;
        }
        break;

    default:
        /* unicode is 0 for every key that does not type a character -
         * numpad keys, arrows, Shift/Ctrl/Alt, F-keys.  Every child window
         * has group_accelerator '\0', so find_accel(0) always matched and
         * such a key "answered" the dialog with '\0'.  The core reads that
         * as neither 'n' nor 'q': e.g. autounlock's "Check this door for a
         * trap?" took it as YES and "Unlock it with your key?" as NO, so a
         * repeating numpad key from walking into the door wrecked both
         * prompts.  Only real characters may answer. */
        if (unicode && find_accel((char) unicode).size() > 0) {
            *(char *) result = (char) unicode;
            return V_EVENT_HANDLED_FINAL;
        }
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
choicedialog::handle_resize_event(window *target, void *result, int res_w,
                                  int res_h)
{
    /* parent is NULL for the pre-ROOTWIN startup banner; centre on screen */
    x = ((parent ? parent->w : res_w) - w) / 2;
    y = ((parent ? parent->h : res_h) - h) / 2;
    return V_EVENT_HANDLED_NOREDRAW;
}
