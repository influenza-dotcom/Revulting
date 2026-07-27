/* NetHack may be freely redistributed.  See license for details. */

#ifndef _vulture_types_h_
#define _vulture_types_h_

#include <SDL.h>
/*
 * This file is meant to be a place to put structure declarations
 * and enums that have global importance.
 * 
 * In the interest of modularization THIS FILE SHOULD NOT EXPORT FUNCTIONS
 */
typedef struct { 
    /* image data (width & height encoded in first 4 bytes) */
    SDL_Surface *graphic;
    /* hotspot offsets;
     * difference between left/top most non-transparent pixel
     * and hotspot defined in the image
     */
    int xmod,ymod;
} vulture_tile;


typedef struct {
    int x;
    int y;
} point;


/*
 * UI scale compensation (see vulture_sdl.h's vulture_render_scale).
 *
 * At render_scale s the whole frame is box-filtered down by s when it is
 * presented, so anything drawn at native pixel size ends up 1/s of its
 * apparent size.  That is exactly what we want for the MAP - the extra render
 * area buys extra visible dungeon - and exactly what we do NOT want for the
 * UI, which must stay legible.  So every UI element is authored s times larger
 * in render space and lands back at native apparent size on screen.
 *
 * vulture_ui_scale is the factor the UI is CURRENTLY BUILT AT (fonts opened,
 * chrome art upscaled), which is not necessarily vulture_render_scale: it only
 * catches up when vulture_ui_rescale() reloads them.  Layout code must use
 * this one, so that layout always agrees with the art it is laying out.
 *
 * The boundary is drawn at the drawing primitive:
 *   - vulture_put_tile*() (winclass/levelwin.cpp's tile loops, and the map
 *     tiles the minimap/overview map do NOT use) is NEVER compensated.
 *   - fonts, vulture_winelem, and the per-window chrome PNGs (statusbar,
 *     toolbars, minimap background, parchment) ARE.
 */
extern int vulture_ui_scale;

/* scale a compile-time UI layout constant into render space */
#define V_UI(n) ((n) * vulture_ui_scale)

#endif

