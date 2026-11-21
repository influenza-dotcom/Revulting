/* NetHack may be freely redistributed.  See license for details. */

#include "vulture_sdl.h" /* XXX this must be the first include,
                             no idea why but it won't compile otherwise */

extern "C" {
#include "hack.h"
}

#include "vulture_gra.h"
#include "vulture_main.h"
#include "vulture_mou.h"
#include "vulture_opt.h"
#include "vulture_txt.h"
#include "vulture_win.h"

#include "levelwin.h"
#include "messagewin.h"

messagewin *msgwin;
static Uint32 message_colors[V_MAX_MESSAGE_COLORS];
static Uint32 danger_colors[V_MAX_MESSAGE_COLORS]; /* §58 */

messagewin::messagewin(window *p) : window(p)
{
    int i;
    Uint32 color;

    v_type = V_WINTYPE_CUSTOM;

    bg_img = SDL_CreateRGBSurface(
        SDL_SWSURFACE, 40, 20, 32, vulture_px_format->Rmask,
        vulture_px_format->Gmask, vulture_px_format->Bmask, 0);
    SDL_FillRect(bg_img, NULL, CLR32_BLACK);
    SDL_SetSurfaceAlphaMod(bg_img, 128);
    SDL_SetSurfaceBlendMode(bg_img, SDL_BLENDMODE_BLEND);

    /* init message ring buffer */
    memset(message_ages, 0, V_MESSAGEBUF_SIZE * sizeof(int));
    message_top = -1;
    message_cur = -1;

    msgwin = this;

    /* Set message shading */
    for (i = 0; i < V_MAX_MESSAGE_COLORS; i++) {
        color = 255 - (i * 11);
        message_colors[i] =
            SDL_MapRGB(vulture_px_format, color, color, color);
        /* §58: the same fade in red, for messages that mean danger */
        danger_colors[i] = SDL_MapRGB(vulture_px_format, color,
                                      color * 45 / 100, color * 45 / 100);
    }
}

/*
 * Vulture mod (IMPLEMENTATION_LOG §58): messages that mean "you are in real
 * trouble" draw in red so they can't scroll past unnoticed.  Matched on the
 * text (the core gives the window port no severity); the list is the
 * deadly-or-nearly-so status messages, not every hit.
 */
static const char *const danger_phrases[] = {
    "dangerously weak",          /* §45 low-HP warning */
    "You feel weak",             /* fainting from hunger is near */
    "You faint from lack of food",
    "You are slowing down",      /* turning to stone */
    "Your limbs are stiffening",
    "Your limbs have turned to stone",
    "You are turning into a green slime",
    "You are turning a little green",
    "Your limbs are getting oozy",
    "Your skin begins to peel",
    "You feel deathly sick",     /* terminal illness */
    "You feel even worse",
    "You find it hard to breathe", /* strangulation */
    "You're gasping for air",
    "You can no longer breathe",
    "You are being crushed",
    "You are being choked",
    "You are beginning to feel weak",
    "You feel feverish",         /* lycanthropy */
    "You die",
    "You hear the shrieks",
    "is about to explode",
    "The engulfing",
    "You are put to sleep",
    "You can't move",
    "You are frozen",
    NULL
};

static bool
vulture_message_is_danger(const std::string &msg)
{
    for (int i = 0; danger_phrases[i]; i++)
        if (msg.find(danger_phrases[i]) != std::string::npos)
            return true;
    return false;
}

messagewin::~messagewin(void)
{
    SDL_FreeSurface(bg_img);
}

bool
messagewin::draw()
{
    int age, textlen, num_messages;
    int pos_x, pos_y, i;
    std::string message;
    int refresh_x, refresh_y, refresh_h, refresh_w;
    int time_cur = svm.moves;

    if (getshown() != 0)
        /* set time_cur to the age of the first shown message */
        get_message(0, &time_cur);

    refresh_x = refresh_y = 99999;
    refresh_h = refresh_w = 0;

    /* repaint background and free it */
    if (background) {
        vulture_put_img(abs_x, abs_y, background);
        SDL_FreeSurface(background);
        background = NULL;

        /* we need these values, so that we can refresh the larger
         * even if the window shrinks during the redraw */
        refresh_x = abs_x;
        refresh_y = abs_y;
        refresh_w = w;
        refresh_h = h;
    }

    num_messages = 0;
    h = 0;
    w = 0;

    /* calculate height & width of new message area */
    while (!(message = get_message(num_messages, &age)).empty()
           && (time_cur - age) < V_MAX_MESSAGE_COLORS
           && num_messages < vulture_opts.messagelines) {
        h += (vulture_get_lineheight(V_FONT_MESSAGE) + 1);
        textlen = vulture_text_length(V_FONT_MESSAGE, message);
        w = (w < textlen) ? textlen : w;
        num_messages++;
    }

    /* add a bit of padding around the text */
    h += 2;
    w += 4;

    x = (parent->w - w) / 2;
    abs_x = parent->abs_x + x;
    abs_y = parent->abs_y;

    /* save new background */
    background = vulture_get_img(abs_x, abs_y, abs_x + w - 1, abs_y + h - 1);

    /* shade the message area */
    vulture_set_draw_region(abs_x, abs_y, abs_x + w - 1, abs_y + h - 1);
    pos_y = abs_y;
    while (pos_y <= abs_y + h) {
        pos_x = abs_x;

        while (pos_x <= abs_x + w) {
            vulture_put_img(pos_x, pos_y, bg_img);
            pos_x += bg_img->w;
        }
        pos_y += bg_img->h;
    }
    vulture_set_draw_region(0, 0, vulture_screen->w - 1,
                            vulture_screen->h - 1);

    /* draw the messages */
    for (i = 0; i < num_messages; i++) {
        message = get_message(num_messages - i - 1, &age);

        pos_x =
            abs_x + (w - vulture_text_length(V_FONT_MESSAGE, message)) / 2;
        pos_y = abs_y + i * (vulture_get_lineheight(V_FONT_MESSAGE) + 1);
        vulture_put_text(V_FONT_MESSAGE, message, vulture_screen, pos_x,
                         pos_y,
                         vulture_message_is_danger(message)
                             ? danger_colors[time_cur - age]
                             : message_colors[time_cur - age]);
    }

    refresh_w = (refresh_w > w) ? refresh_w : w;
    refresh_h = (refresh_h > h) ? refresh_h : h;

    if (refresh_x > abs_x) {
        refresh_w += refresh_x - abs_x;
        refresh_x = abs_x;
    }
    refresh_y = (refresh_y < abs_y) ? refresh_y : abs_y;

    vulture_invalidate_region(refresh_x, refresh_y, refresh_w, refresh_h);

    return false;
}

eventresult
messagewin::handle_mousemotion_event(window *target, void *result, int xrel,
                                     int yrel, int state)
{
    point mouse = vulture_get_mouse_pos();
    window *new_target = levwin->get_window_from_point(mouse);

    return levwin->handle_mousemotion_event(new_target, result, xrel, yrel,
                                            state);
}

eventresult
messagewin::handle_mousebuttonup_event(window *target, void *result,
                                       int mouse_x, int mouse_y, int button,
                                       int state)
{
    point mouse = vulture_get_mouse_pos();
    window *new_target = levwin->get_window_from_point(mouse);

    return levwin->handle_mousebuttonup_event(new_target, result, mouse_x,
                                              mouse_y, button, state);
}

void
messagewin::add_message(std::string str)
{
    /* s125: a line repeating the last one (this turn or the next couple)
       is condensed onto it: "You hit the jackal. (x3)" */
    if (message_top >= 0 && message_ages[message_top]) {
        std::string &last = message_buf[message_top];
        std::string base = last;
        int n = 1, nowm = svm.moves ? (int) svm.moves : 1;
        size_t p = base.rfind(" (x");

        if (p != std::string::npos && !base.empty()
            && base[base.size() - 1] == ')') {
            n = atoi(base.c_str() + p + 3);
            if (n > 1)
                base = base.substr(0, p);
            else
                n = 1;
        }
        if (base == str && nowm - message_ages[message_top] <= 2) {
            char cnt[24];

            snprintf(cnt, sizeof cnt, " (x%d)", n + 1);
            last = base + cnt;
            message_ages[message_top] = nowm;
            message_cur = message_top;
            return;
        }
    }
    message_cur = message_top = (message_top + 1) % V_MESSAGEBUF_SIZE;
    message_buf[message_top] = str;
    /* age 0 marks an EMPTY slot (get_message), so never store 0 */
    message_ages[message_top] = svm.moves ? (int) svm.moves : 1;
}

/* the newest `maxcount` messages, oldest first - what the core wants to
   write into the save file (§43) */
std::vector<std::string>
messagewin::history(size_t maxcount)
{
    std::vector<std::string> out;
    int i, idx;

    if (message_top < 0)
        return out;
    /* walk back from the newest to the first empty slot (or the cap) */
    for (i = 0; i < V_MESSAGEBUF_SIZE && out.size() < maxcount; i++) {
        idx = (message_top - i + V_MESSAGEBUF_SIZE) % V_MESSAGEBUF_SIZE;
        if (!message_ages[idx])
            break;
        out.push_back(message_buf[idx]);
    }
    /* collected newest first; the core wants oldest first */
    return std::vector<std::string>(out.rbegin(), out.rend());
}

void
messagewin::clear_history(void)
{
    int i;

    for (i = 0; i < V_MESSAGEBUF_SIZE; i++) {
        message_ages[i] = 0;
        message_buf[i].clear();
    }
    message_top = -1;
    message_cur = -1;
}

void
messagewin::setshown(int first)
{
    message_cur =
        (message_top - first + V_MESSAGEBUF_SIZE) % V_MESSAGEBUF_SIZE;
}

int
messagewin::getshown()
{
    return (message_top - message_cur + V_MESSAGEBUF_SIZE)
           % V_MESSAGEBUF_SIZE;
}

/* retrieve a message from the message buffer, offset messages before the
 * current one */
std::string
messagewin::get_message(int offset, int *age)
{
    if (offset < V_MESSAGEBUF_SIZE) {
        int msg_id =
            (message_cur - offset + V_MESSAGEBUF_SIZE) % V_MESSAGEBUF_SIZE;

        if (message_ages[msg_id]) {
            *age = message_ages[msg_id];
            return message_buf[msg_id];
        }
    }

    *age = 0;
    return "";
}

void
messagewin::view_all(void)
{
    int offset, time, winid;
    std::string message;
    char menuline[256];

    winid = vulture_create_nhwindow(NHW_MENU);

    offset = -getshown();
    while (!(message = get_message(offset, &time)).empty()) {
        sprintf(menuline, "T:%d %s", time, message.c_str());
        vulture_putstr(winid, ATR_NONE, menuline);
        offset++;
    }

    /* Display the messages */
    vulture_display_nhwindow(winid, TRUE);

    /* Clean up */
    vulture_destroy_nhwindow(winid);
}
