/* NetHack may be freely redistributed.  See license for details. */

#include "nhwindow.h"
#include "window.h"

#include <vector>

extern "C" {
#include "hack.h"
}
#include "vulture_gen.h"

static std::vector<nhwindow *> vulture_nhwindows(8);
int windowcount = 0;
int windowcount_max = 8;

/*
 * NetHack 5.0.0 passes WIN_ERR (-1) to the window procs in several places
 * where 3.6.6 did not (see win_ctrl_nhwindow / wp_ids handling).  The stock
 * Vulture code indexed the vector unchecked, so -1 became a huge size_t and
 * tripped the libstdc++ bounds assertion before the first frame was drawn.
 * Return NULL for any id that does not name a live window; every caller in
 * vulture_main.cpp checks for it.
 */
nhwindow *
vulture_get_nhwindow(int winid)
{
    if (winid < 0 || (size_t) winid >= vulture_nhwindows.size()) {
        vulture_write_log(V_LOG_NOTE, __FILE__, __LINE__,
                          "vulture_get_nhwindow: id %d out of range (size %d)\n",
                          winid, (int) vulture_nhwindows.size());
        return NULL;
    }
    return vulture_nhwindows[winid];
}

nhwindow::nhwindow(int type)
{
    id = 0;

    /* if necessary make space in the vulture_windows array */
    if (windowcount == windowcount_max) {
        vulture_nhwindows.resize(windowcount_max + 8, NULL);
        windowcount_max += 8;

        /* no need to search through the first windowcount_cur ids, they're
         * definitely taken */
        id = windowcount;
    } else {
        /* bounds test must come FIRST: the stock order dereferenced
         * vulture_nhwindows[windowcount_max] whenever every slot was taken */
        while (id < windowcount_max && vulture_nhwindows[id] != NULL)
            id++;
        if (id >= windowcount_max) {
            /* windowcount drifted out of step with the array; grow anyway
             * rather than write past the end */
            vulture_nhwindows.resize(windowcount_max + 8, NULL);
            windowcount_max += 8;
        }
    }

    /* the vector is the authority on capacity, not windowcount_max */
    if ((size_t) id >= vulture_nhwindows.size())
        vulture_nhwindows.resize(id + 8, NULL);

    windowcount++;
    vulture_nhwindows[id] = this;

    this->type = type;
    impl = NULL;
    ending_type = 0;
    caption.clear();
    has_objects = false;
}

nhwindow::~nhwindow()
{
    if (id >= 0 && (size_t) id < vulture_nhwindows.size())
        vulture_nhwindows[id] = NULL;
    windowcount--;

    if (impl)
        delete impl;

    /* clean up after the last window is gone */
    if (windowcount == 0) {
        vulture_nhwindows.clear();
        windowcount_max = 0;
    }
}

void
nhwindow::add_menuitem(std::string str, bool preselected, void *identifier,
                       char accelerator, char group_accelerator, int glyph)
{
    items.push_back(menuitem(str, preselected, identifier, accelerator,
                             group_accelerator, glyph));
}

void
nhwindow::reset()
{
    items.clear();
    ending_type = 0;
    caption.clear();
    has_objects = false;

    if (impl)
        delete impl;
    impl = NULL;
}
