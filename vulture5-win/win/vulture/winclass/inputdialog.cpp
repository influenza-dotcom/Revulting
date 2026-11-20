/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_gen.h"
#include "vulture_mou.h"
#include "vulture_sdl.h"
#include "vulture_tile.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "inputdialog.h"
#include "textwin.h"
#include "button.h"

inputdialog::inputdialog(window *p, std::string ques, int size, int force_x,
                         int force_y, const std::string &default_text)
    : mainwin(p)
{
    caption = ques;

    destsize = size;

    input = new textwin(this, destsize);
    input->menu_id = 1;

    if (!default_text.empty())
        input->caption = default_text;

    /* calc sizes and positions */
    w = vulture_text_length(V_FONT_HEADLINE, ques);
    /* the 500px minimum is a UI dimension, so it scales with the UI (the text
     * it has to hold already does) */
    w = (w < V_UI(500)) ? V_UI(500) : w;

    input->w = w;
    w += border_left + border_right;

    int lineheight = vulture_get_lineheight(V_FONT_INPUT);
    int buttonheight = vulture_get_lineheight(V_FONT_MENU) + V_UI(15);

    input->h = lineheight + 1;
    input->x = (w - input->w) / 2;
    input->y = border_top + vulture_get_lineheight(V_FONT_HEADLINE) + lineheight;

    /* add a Continue button below the input field */
    button *btn = new button(this, "Continue", 1, '\r');
    btn->x = (w - btn->w) / 2;
    btn->y = input->y + input->h + lineheight;

    h = border_top + vulture_get_lineheight(V_FONT_HEADLINE)
        + 3 * lineheight + buttonheight + border_bottom;

    if (parent == NULL) {
        force_x = (vulture_screen->w - w) / 2;
        force_y = (vulture_screen->h - h) / 2;
    }

    if (force_x > -1)
        x = force_x;
    else
        x = (parent->w - w) / 2;

    if (force_y > -1)
        y = force_y;
    else
        y = (parent->h - h) / 2;

    if (parent == NULL) {
        abs_x = x;
        abs_y = y;
    } else {
        abs_x = parent->x + x;
        abs_y = parent->y + y;
    }
}

void
inputdialog::copy_input(char *dest)
{
    strncpy(dest, input->caption.c_str(), destsize);
}

/* §44: case-insensitive "word starts with prefix" */
static bool
starts_with_ci(const std::string &word, const std::string &prefix)
{
    size_t i;

    if (prefix.size() > word.size())
        return false;
    for (i = 0; i < prefix.size(); i++)
        if (tolower((unsigned char) word[i])
            != tolower((unsigned char) prefix[i]))
            return false;
    return true;
}

void
inputdialog::set_completions(const std::vector<std::string> &words)
{
    completions = words;
    base_caption = caption;
    refresh_hint();
}

/* extend the typed text to the longest prefix every match shares */
void
inputdialog::complete_tab(void)
{
    std::string &text = input->caption;
    std::string common;
    bool first = true;
    size_t i, j;

    for (i = 0; i < completions.size(); i++) {
        if (!starts_with_ci(completions[i], text))
            continue;
        if (first) {
            common = completions[i];
            first = false;
        } else {
            for (j = 0; j < common.size() && j < completions[i].size()
                        && common[j] == completions[i][j];
                 j++)
                ;
            common.resize(j);
        }
    }
    if (!first && common.size() > text.size()) {
        text = common;
        input->need_redraw = 1;
    }
}

/* headline = the prompt plus the commands the typed text still matches,
   cut to the dialog's width */
void
inputdialog::refresh_hint(void)
{
    const std::string &text = input->caption;
    std::string hint;
    int shown = 0, nmatch = 0, room;
    size_t i;

    if (completions.empty())
        return;

    if (text.empty()) {
        caption = base_caption;
        need_redraw = 1;
        return;
    }

    room = w - border_left - border_right - V_UI(10);
    for (i = 0; i < completions.size(); i++) {
        if (!starts_with_ci(completions[i], text))
            continue;
        nmatch++;
        std::string next = hint.empty() ? completions[i]
                                        : hint + ", " + completions[i];
        if (vulture_text_length(V_FONT_HEADLINE, "#: " + next + ", ...")
            < room) {
            hint = next;
            shown++;
        }
    }
    if (!nmatch)
        caption = "#: no such command";
    else if (shown < nmatch)
        caption = "#: " + hint + ", ...";
    else
        caption = "#: " + hint;
    need_redraw = 1;
}

bool
inputdialog::draw()
{
    vulture_write_log(V_LOG_DEBUG, __FILE__, __LINE__,
                      "inputdialog::draw()\n");
    return mainwin::draw();
}

eventresult
inputdialog::handle_mousemotion_event(window *target, void *result, int xrel,
                                      int yrel, int state)
{
    vulture_set_mcursor(V_CURSOR_NORMAL);
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
inputdialog::handle_mousebuttonup_event(window *target, void *result,
                                        int mouse_x, int mouse_y,
                                        int button, int state)
{
    if (button == SDL_BUTTON_LEFT
        && target->get_wintype() == V_WINTYPE_BUTTON) {
        *(int *) result = 1;
        return V_EVENT_HANDLED_FINAL;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
inputdialog::handle_keydown_event(window *target, void *result, int sym,
                                  int mod, int unicode)
{
    std::string &text = first_child->caption;

    switch (sym) {
    case SDLK_KP_ENTER:
    case SDLK_RETURN:
        /* done! */
        *(int *) result = 1;
        return V_EVENT_HANDLED_FINAL;

    case SDLK_ESCAPE:
        /* cancel */
        *(int *) result = 0;
        return V_EVENT_HANDLED_FINAL;

    case SDLK_BACKSPACE:
        if (!text.empty()) {
            text.erase(text.end() - 1);
            first_child->need_redraw = 1;
        }
        refresh_hint(); /* §44 */
        return V_EVENT_HANDLED_REDRAW;

    case SDLK_TAB:
        /* §44: complete a typed '#' command */
        if (!completions.empty()) {
            complete_tab();
            refresh_hint();
            return V_EVENT_HANDLED_REDRAW;
        }
        return V_EVENT_HANDLED_NOREDRAW;

    default:
        /* add characters up to a maximum of 256 */
        if (text.length() < 256
            && vulture_text_length(V_FONT_MENU, text) < (first_child->w - 10)
            && isprint(unicode)) {
            text.push_back((char) unicode);
            first_child->need_redraw = 1;
        }
        refresh_hint(); /* §44 */
        /* only the child needs to be redrawn */
        return V_EVENT_HANDLED_REDRAW;
    }
    return V_EVENT_HANDLED_NOREDRAW;
}

eventresult
inputdialog::handle_resize_event(window *target, void *result, int res_w,
                                 int res_h)
{
    vulture_write_log(
        V_LOG_DEBUG, __FILE__, __LINE__,
        "inputdialog::handle_resize_event(*target,*result,%d,%d) {entry}\n",
        res_w, res_h);

    if (parent == NULL) {
        x = (vulture_screen->w - w) / 2;
        y = (vulture_screen->h - h) / 2;
    } else {
        x = (parent->w - w) / 2;
        y = (parent->h - h) / 2;
    }

    target->need_redraw = 1;
    return V_EVENT_HANDLED_REDRAW;
}
