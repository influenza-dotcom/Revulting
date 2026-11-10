/* NetHack may be freely redistributed.  See license for details. */

#ifndef _objheaderwin_h_
#define _objheaderwin_h_

#include "window.h"

/* A non-selectable cell in the object grid.
 *
 * Normally a section heading ("Weapons", "Armor"): centred text, no icon.
 * When the menu row carried an object glyph (tile >= 0) it is instead an
 * informational item row - e.g. 5.0.0's "Reroll this character?" menu lists
 * the starting inventory as unselectable rows - and is drawn like an item
 * cell (icon box on the left, text beside it) but still cannot be picked. */
class objheaderwin : public window
{
  public:
    objheaderwin(window *parent, std::string cap, int tile = -1);
    virtual bool draw();

  private:
    int tile;
};

#endif
