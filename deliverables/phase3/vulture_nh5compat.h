/* Vulture -> NetHack 5.0.0 compat shim (SAFE subset only: stdlib + uppercase renames).
 * Lowercase relocated globals (moves/context/level/...) are edited at their sites, since
 * force-including their macros would corrupt SDL/system headers. */
#ifndef VULTURE_NH5COMPAT_H
#define VULTURE_NH5COMPAT_H
#include <stdlib.h>
#ifndef MENU_UNSELECTED
#define MENU_UNSELECTED FALSE
#endif
#define PM_PRIEST          PM_CLERIC
#define PM_ALIGNED_PRIEST  PM_ALIGNED_CLERIC
#define PM_HIGH_PRIEST     PM_HIGH_CLERIC
#endif
