/*
MENU_SEARCH.H

Settings' search, the menu engine's (menu_search.c): the rows of the menus'
model that match what is typed, best first.
*/

#ifndef MENU_SEARCH_H
#define MENU_SEARCH_H

struct halo_menu_model;
struct halo_menu_model_screen;
struct halo_menu_model_row;

/* how well a row matches a word typed: by its label's words, the other words
it is found by (its search: as its label's, a little less), its choices'
words and its help's, each a whole word, then the start of one, then (its
label's alone) a part of one */
enum
{
	MENU_SEARCH_LABEL_WORD = 100,
	MENU_SEARCH_LABEL_START = 90,
	MENU_SEARCH_OTHER_WORD = 85,
	MENU_SEARCH_OTHER_START = 75,
	MENU_SEARCH_LABEL_PART = 50,
	MENU_SEARCH_CHOICE_WORD = 40,
	MENU_SEARCH_CHOICE_START = 35,
	MENU_SEARCH_HELP_WORD = 25,
	MENU_SEARCH_HELP_START = 20,
	/* (more for a label that starts with the first word typed) */
	MENU_SEARCH_LABEL_FIRST = 5,
	/* (a word typed shorter than this finds no help, nor part of a label) */
	MENU_SEARCH_LEAST_HELP = 3,
};

/* a row found: its index in the model's rows, its screen's, and its score */
struct menu_search_found
{
	long row, screen, score;
};

/* whether a row may be found (its conditions hold): the caller's, NULL for
any row */
typedef int (*menu_search_shown)(void *context, struct halo_menu_model_screen const *screen,
	struct halo_menu_model_row const *row);

/* how well the row matches the words typed: 0 if one of them finds nothing
of it */
long menu_search_score(struct halo_menu_model_row const *row, char const *typed);

/* the rows of Settings' tabs and pages (the model's screens of kind
"settings" laid out as "tab" or "page") that match the words typed, best
first, the model's order among the equal: up to maximum of them in found;
the count of all that match */
long menu_search_rank(struct halo_menu_model const *model, char const *typed, menu_search_shown shown,
	void *context, struct menu_search_found *found, long maximum);

/* the hint row of the search's screen whose words have one of the words
typed (tools/port_settings.py's SETTINGS_SEARCH_ELSEWHERE: a place that is
not Settings'), NULL if none */
struct halo_menu_model_row const *menu_search_hint(struct halo_menu_model const *model,
	struct halo_menu_model_screen const *screen, char const *typed);

#endif
