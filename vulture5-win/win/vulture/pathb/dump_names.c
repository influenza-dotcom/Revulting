/* Path B audit helper: reproduce Vulture's init_objnames/init_monnames name
 * derivation and dump the per-index symbolic names for the linked NetHack data.
 * Compile with -DNH500 against the 5.0.0 tree; without it against 3.6.6/Vulture.
 * Output: TSV  <category>\t<index>\t<NAME>
 */
#include "hack.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void upcase_ident(char *b)
{
    char *c;
    for (c = b; *c; c++) {
        if (*c >= 'a' && *c <= 'z')
            *c -= (char) ('a' - 'A');
        else if ((*c < 'A' || *c > 'Z') && !isdigit((unsigned char) *c))
            *c = '_';
    }
}

int main(void)
{
    int i;
    char buffer[128];
    static const char *objclassnames[] = { 0,
        "Illegal objects", "Weapons", "Armor", "Rings", "Amulets",
        "Tools", "Comestibles", "Potions", "Scrolls", "Spellbooks",
        "Wands", "Coins", "Gems", "Boulders/Statues", "Iron balls",
        "Chains", "Venoms"
    };
    int unnamed_cnt[MAXOCLASSES];
    const char *nameptr;

#ifdef NH500
    /* 5.0.0 populates the runtime arrays from static *_init[] templates */
    extern void objects_globals_init(void);
    extern void monst_globals_init(void);
    objects_globals_init();
    monst_globals_init();
#endif

    memset(unnamed_cnt, 0, sizeof unnamed_cnt);

#ifdef NH500
    /* 5.0.0 inserts "generic <class>" ILLOBJ objects at low indices, so the
     * classic "stop at first ILLOBJ after 0" terminator ends the loop early.
     * Iterate the full NUM_OBJECTS instead. */
    for (i = 0; i < NUM_OBJECTS; i++) {
#else
    for (i = 0; !i || objects[i].oc_class != ILLOBJ_CLASS; i++) {
#endif
        buffer[0] = '\0';
        if (!obj_descr[i].oc_name) {
            unnamed_cnt[(int) objects[i].oc_class]++;
            snprintf(buffer, 60, "%3.3s unnamed %d",
                     objclassnames[(int) objects[i].oc_class],
                     unnamed_cnt[(int) objects[i].oc_class]);
        } else {
            nameptr = obj_descr[i].oc_name;
            switch (objects[i].oc_class) {
            case WAND_CLASS:   snprintf(buffer, 60, "WAN_%s", nameptr); break;
            case RING_CLASS:   snprintf(buffer, 60, "RIN_%s", nameptr); break;
            case POTION_CLASS: snprintf(buffer, 60, "POT_%s", nameptr); break;
            case SPBOOK_CLASS: snprintf(buffer, 60, "SPE_%s", nameptr); break;
            case SCROLL_CLASS: snprintf(buffer, 60, "SCR_%s", nameptr); break;
            case AMULET_CLASS:
                if (objects[i].oc_material == PLASTIC)
                    snprintf(buffer, 60, "FAKE_AMULET_OF_YENDOR");
                else
                    snprintf(buffer, 60, "%s", obj_descr[i].oc_name);
                break;
            case GEM_CLASS:
                if (objects[i].oc_material == GLASS) {
                    switch (objects[i].oc_color) {
                    case CLR_WHITE:  snprintf(buffer, 60, "GEM_WHITE_GLASS"); break;
                    case CLR_BLUE:   snprintf(buffer, 60, "GEM_BLUE_GLASS");  break;
                    case CLR_RED:    snprintf(buffer, 60, "GEM_RED_GLASS");   break;
                    case CLR_BROWN:  snprintf(buffer, 60, "GEM_BROWN_GLASS"); break;
                    case CLR_ORANGE: snprintf(buffer, 60, "GEM_ORANGE_GLASS");break;
                    case CLR_YELLOW: snprintf(buffer, 60, "GEM_YELLOW_GLASS");break;
                    case CLR_BLACK:  snprintf(buffer, 60, "GEM_BLACK_GLASS"); break;
                    case CLR_GREEN:  snprintf(buffer, 60, "GEM_GREEN_GLASS"); break;
                    case CLR_MAGENTA:snprintf(buffer, 60, "GEM_VIOLET_GLASS");break;
                    }
                } else
                    snprintf(buffer, 60, "%s", nameptr);
                break;
            default: snprintf(buffer, 60, "%s", nameptr); break;
            }
        }
        upcase_ident(buffer);
#ifdef SLIME_MOLD
        if (i == SLIME_MOLD) strcpy(buffer, "SLIME_MOLD");
#endif
        printf("object\t%d\t%s\n", i, buffer);
    }

#ifdef NH500
    for (i = 0; i < NUMMONS; i++) {
        nameptr = mons[i].pmnames[NEUTRAL];
        if (!nameptr) nameptr = mons[i].pmnames[MALE];
        if (!nameptr) nameptr = mons[i].pmnames[FEMALE];
        if (!nameptr) nameptr = "";
        if (mons[i].mlet == S_HUMAN && !strncmp(nameptr, "were", 4))
            snprintf(buffer, 64, "PM_HUMAN_%s", nameptr);
        else
            snprintf(buffer, 64, "PM_%s", nameptr);
        upcase_ident(buffer);
        printf("monster\t%d\t%s\n", i, buffer);
    }
#else
    for (i = 0; mons[i].mlet; i++) {
        if (mons[i].mlet == S_HUMAN && !strncmp(mons[i].mname, "were", 4))
            snprintf(buffer, 64, "PM_HUMAN_%s", mons[i].mname);
        else
            snprintf(buffer, 64, "PM_%s", mons[i].mname);
        upcase_ident(buffer);
        printf("monster\t%d\t%s\n", i, buffer);
    }
#endif
    return 0;
}
