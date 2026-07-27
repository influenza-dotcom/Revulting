#include <string.h>

extern "C" {
#include "hack.h"
}

#include "vulture_main.h"
#include "vulture_opt.h"   /* vulture_opts.ask_character */

/* ------------------------------------------------------------------------- */

/* construct a rolename like "neutral male human wizard", using all
* information about the role that is currently known */
static void vulture_player_selection_rolename(char *rolename)
{
	int prev;

	rolename[0] = 0;
	prev = FALSE;

	if (flags.initalign != -1)
	{
		strcat(rolename, aligns[flags.initalign].adj);
		prev = TRUE;
	}

	if (flags.initgend != -1)
	{
		if (prev) strcat(rolename, " ");
		strcat(rolename, genders[flags.initgend].adj);
		prev = TRUE;
	}

	if (flags.initrace != -1)
	{
		if (prev) strcat(rolename, " ");
		strcat(rolename,races[flags.initrace].noun);
		prev = TRUE;
	}

	if (flags.initrole != -1)
	{
		if (prev)
			strcat(rolename, " ");

		if (flags.initgend == 1 && roles[flags.initrole].name.f)
			strcat(rolename, roles[flags.initrole].name.f);
		else
			strcat(rolename, roles[flags.initrole].name.m);

		prev=TRUE;
	}

}

/* ------------------------------------------------------------------------- */

/* prompt the player to choose a role */
static int vulture_player_selection_role (void)
{
	winid window;
	anything identifier;
	menu_item *menu_list;
	int chosen, items, item = 0;
	char thisch, lastch;

	menu_list = 0;
	window = vulture_create_nhwindow(NHW_MENU);
	vulture_start_menu(window, MENU_BEHAVE_STANDARD);
	items = 0;
	lastch = 0;

  identifier.a_void = NULL;

	/* add all possible roles to a menu */
	for (chosen = 0; roles[chosen].name.m || roles[chosen].name.f; chosen++)
	{
		identifier.a_int = chosen + 1;
		if (ok_role(chosen, flags.initrace, flags.initgend, flags.initalign))
		{
			items++;
			item = chosen;

			if (flags.initgend == 1 && roles[chosen].name.f)
			{
				thisch = lowc(roles[chosen].name.f[0]);
				if (thisch == lastch)
					thisch = highc(thisch);
				vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, thisch, 0, 0, NO_COLOR, roles[chosen].name.f, (((chosen == flags.initrole)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));
			}
			else
			{
				thisch = lowc(roles[chosen].name.m[0]);
				if (thisch == lastch)
					thisch = highc(thisch);
				vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, thisch, 0, 0, NO_COLOR, roles[chosen].name.m, (((chosen==flags.initrole)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));
			}
			lastch = thisch;
		}
	}

	/* if the menu contains only one item, select that automatically */
	if (items == 1)
	{
		flags.initrole = item;
		free(menu_list);
		vulture_destroy_nhwindow(window);
		return 1;
	}

	identifier.a_int=-1;
	vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, '*', 0, 0, NO_COLOR, "Random", (((flags.initrole<0)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));

	/* Don't use the rolename here, otherwise we might get "Pick a role for your wizard"
	* which is rather silly. */
	vulture_end_menu(window, "Pick a role for your character");

	if (vulture_select_menu(window, PICK_ONE, &menu_list) == 1)
	{
		chosen = menu_list->item.a_int;
		if (chosen == -1)
			chosen = pick_role(flags.initrace,flags.initgend,flags.initalign,PICK_RANDOM);
		else
			chosen--;

		free(menu_list);
		flags.initrole=chosen;
		vulture_destroy_nhwindow(window);
		return 1;
	}
	else
		vulture_bail(NULL);

	flags.initrole = -1;
	vulture_destroy_nhwindow(window);
	return 0;
}

/* ------------------------------------------------------------------------- */

/* prompt the player to choose a race */
static int vulture_player_selection_race (void)
{
	winid window;
	anything identifier;
	menu_item *menu_list;
	int chosen, items, item = 0;
	char rolename[BUFSZ];
	char selection[BUFSZ];
	char thisch, lastch;

	menu_list = 0;
	window = vulture_create_nhwindow(NHW_MENU);
	vulture_start_menu(window, MENU_BEHAVE_STANDARD);

	items = 0;
	lastch = 0;

	/* add all possible races to a menu */
	for (chosen = 0; races[chosen].noun; chosen++)
	{
		identifier.a_int = chosen + 1;
		if (ok_race(flags.initrole, chosen, flags.initgend, flags.initalign))
		{
			items++;
			item = chosen;
			thisch = lowc(races[chosen].noun[0]);
			if (thisch == lastch)
				thisch = highc(thisch);
			lastch = thisch;
			vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, thisch, 0, 0, NO_COLOR, races[chosen].noun, ((chosen==flags.initrace) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));
		}
	}

	/* if the menu contains only one item, select that automatically */
	if (items == 1) {
		flags.initrace = item;
		free(menu_list);
		vulture_destroy_nhwindow(window);
		return 1;
	}

	identifier.a_int=-1;
	vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, '*', 0, 0, NO_COLOR, "Random", (((flags.initrace<0)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));

	vulture_player_selection_rolename((char *)&rolename);
	sprintf(selection,"Pick the race of your %s", strlen(rolename) ? rolename : "character");
	vulture_end_menu(window, selection);

	if (vulture_select_menu (window, PICK_ONE, &menu_list)==1)
	{
		chosen = menu_list->item.a_int;
		if (chosen == -1)
			chosen = pick_race(flags.initrole, flags.initgend, flags.initalign, PICK_RANDOM);
		else
			chosen--;

		free(menu_list);
		flags.initrace = chosen;
		vulture_destroy_nhwindow(window);
		return 1;
	}
	else
		vulture_bail(NULL);

	flags.initrace = -1;
	vulture_destroy_nhwindow(window);
	return 0;

}

/* ------------------------------------------------------------------------- */

/* prompt the player to choose a gender */
static int vulture_player_selection_gender (void)
{
	winid window;
	anything identifier;
	menu_item *menu_list;
	int chosen, item = 0, items;
	char rolename[BUFSZ];
	char selection[BUFSZ];
	char thisch, lastch;

	menu_list = 0;
	window = vulture_create_nhwindow(NHW_MENU);
	vulture_start_menu(window, MENU_BEHAVE_STANDARD);
	chosen = 0;
	items = 0;
	lastch = 0;

	/* add all possible genders to the menu */
	while (chosen < ROLE_GENDERS)
	{
		identifier.a_int = chosen + 1;
		if (ok_gend(flags.initrole, flags.initrole, chosen, flags.initalign))
		{
			thisch = lowc(genders[chosen].adj[0]);
			if (thisch == lastch)
				thisch = highc(thisch);
			lastch = thisch;
			vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, thisch, 0, 0, NO_COLOR, genders[chosen].adj, ((chosen==flags.initgend) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));
			items++;
			item = chosen;
		}
		chosen++;
	}

	if (items==1)
	{
		flags.initgend = item;
		free(menu_list);
		vulture_destroy_nhwindow(window);
		return 1;
	}

	identifier.a_int=-1;
	vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, '*', 0, 0, NO_COLOR, "Random", (((flags.initgend<0)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));

	vulture_player_selection_rolename((char *)&rolename);
	sprintf(selection,"Pick the gender of your %s", strlen(rolename) ? rolename : "character");
	vulture_end_menu(window, selection);

	if (vulture_select_menu (window, PICK_ONE, &menu_list) == 1)
	{
		chosen=menu_list->item.a_int;
		if (chosen == -1)
			chosen = pick_gend(flags.initrole, flags.initrace, flags.initalign, PICK_RANDOM);
		else
			chosen--;

		flags.initgend = chosen;
		free(menu_list);
		vulture_destroy_nhwindow(window);
		return 1;
	}
	else
		vulture_bail(NULL);

	flags.initgend = -1;
	vulture_destroy_nhwindow(window);
	return 0;
}

/* ------------------------------------------------------------------------- */

/* prompt the player to choose an alignment */
static int vulture_player_selection_alignment (void)
{
	winid window;
	anything identifier;
	menu_item *menu_list;
	int chosen, items, item = 0;
	char rolename[BUFSZ];
	char selection[BUFSZ];
	char thisch, lastch;

	menu_list = 0;
	window = vulture_create_nhwindow(NHW_MENU);
	vulture_start_menu(window, MENU_BEHAVE_STANDARD);
	chosen = 0;
	items = 0;
	lastch = 0;

	/* add the potential alignments for this character to a menu */
	while (chosen < ROLE_ALIGNS)
	{
		identifier.a_int = chosen + 1;
		if (ok_align(flags.initrole, flags.initrace, flags.initgend, chosen))
		{
			items++;
			item = chosen;
			thisch = lowc(aligns[chosen].adj[0]);
			if (thisch == lastch)
				thisch = highc(thisch);
			lastch = thisch;
			vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, thisch, 0, 0, NO_COLOR, aligns[chosen].adj, (((chosen == flags.initalign)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));
		}
		chosen++;
	}

	/* if there is only one possible alignment, select it automatically */
	if (items == 1)
	{
		flags.initalign = item;
		free(menu_list);
		vulture_destroy_nhwindow(window);
		return 1;
	}

	identifier.a_int = -1;
	vulture_add_menu(window, vulture_menu_glyph(NO_GLYPH), &identifier, '*', 0, 0, NO_COLOR, "Random", (((flags.initalign < 0)) ? MENU_ITEMFLAGS_SELECTED : MENU_ITEMFLAGS_NONE));

	vulture_player_selection_rolename((char *)&rolename);
	sprintf(selection,"Pick the alignment of your %s", strlen(rolename) ? rolename : "character");
	vulture_end_menu(window, selection);

	/* let the player pick an alignment */
	if (vulture_select_menu (window, PICK_ONE, &menu_list)==1)
	{
		chosen = menu_list->item.a_int;
		if (chosen == -1)
			chosen = pick_align(flags.initrole, flags.initrace, flags.initgend, PICK_RANDOM);
		else
			chosen--;

		flags.initalign = chosen;
		free(menu_list);
		vulture_destroy_nhwindow(window);
		return 1;
	}
	else
		vulture_bail(NULL);

	flags.initalign = -1;
	vulture_destroy_nhwindow(window);
	return 0;
}

/* ------------------------------------------------------------------------- */

/*
player_selection()
		-- Do a window-port specific player type selection.  If
		player_selection() offers a Quit option, it is its
		responsibility to clean up and terminate the process.
		You need to fill in pl_character[0].
*/
void vulture_player_selection_internal (void)
{
#ifdef DEBUG
	printf("- vulture_player_selection();\n");
#endif
	/* randomize those selected as "random" within .nethackrc */
	if ( (flags.initrole == ROLE_RANDOM) ||
		(flags.randomall && (flags.initrole == ROLE_NONE) ) )
		flags.initrole = pick_role(flags.initrace, flags.initgend, flags.initalign, PICK_RANDOM);

	if ( (flags.initrace == ROLE_RANDOM) ||
		(flags.randomall && (flags.initrace == ROLE_NONE)))
		flags.initrace = pick_race(flags.initrole,flags.initgend,flags.initalign,PICK_RANDOM);

	if ( (flags.initgend == ROLE_RANDOM) ||
		(flags.randomall && (flags.initgend == ROLE_NONE)))
		flags.initgend =
		pick_gend(flags.initrole,flags.initrace,flags.initalign,PICK_RANDOM);

	if ( (flags.initalign== ROLE_RANDOM) ||
		(flags.randomall && (flags.initalign == ROLE_NONE)))
		flags.initalign = pick_align(flags.initrole, flags.initrace, flags.initgend, PICK_RANDOM);

	/* If the config already pins role, race, gender AND alignment there is
	 * nothing left to choose, so don't ask - this matches core NetHack, which
	 * only runs player selection for the parts that are still unspecified.
	 * (It also lets the game start without any keystrokes, which the runtime
	 * acceptance harness relies on.) */
	if (flags.initrole >= 0 && flags.initrace >= 0
	    && flags.initgend >= 0 && flags.initalign >= 0
	    && !vulture_opts.ask_character)
		return;

	/* here this interface should present it's character selection */
	switch (vulture_yn_function("Shall I pick a character's race, role, gender and alignment for you?","ynq",'y')) {
		case 'q':
			vulture_bail("Quit from character selection.");
		case 'y':
			break;
		case 'n':
			vulture_player_selection_role();
			vulture_player_selection_race();
			vulture_player_selection_gender();
			vulture_player_selection_alignment();
			break;
	}

	/* any manually selected as random or left out ? randomize them ... */
	if (flags.initrole == ROLE_NONE)
		flags.initrole = pick_role(flags.initrace,flags.initgend,
								flags.initalign,PICK_RANDOM);

	if (flags.initrace == ROLE_NONE)
		flags.initrace = pick_race(flags.initrole,flags.initgend,
								flags.initalign,PICK_RANDOM);

	if (flags.initgend == ROLE_NONE)
		flags.initgend = pick_align(flags.initrole,flags.initrace,
									flags.initalign,PICK_RANDOM);

	if (flags.initalign == ROLE_NONE)
		flags.initalign = pick_gend(flags.initrole,flags.initrace,
									flags.initgend ,PICK_RANDOM);

}


/* ------------------------------------------------------------------------- */

