/*
SETTINGS_SEARCH.C (test)

Settings' search, the menu engine's (port/linux/game/menu_search.c): the rows
of the real model (port/assets/menus/model/settings.xml, its tabs' and
pages' rows, written into model_data.inc by test_settings_search.py) that
what is typed finds, best first. The case is what is typed ("query:<words>");
each row found is printed on a line, "<key> <score> <crumb>", for
test_settings_search.py to check, and "matched <count>" after them. The
case "hint:<words>" prints the search's hint row a word finds, or "none".
*/

#include "harness.h"

#include "model.inc"
#include "under_test.inc"
#include "model_data.inc"

#define FOUND_MOST 64

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct halo_menu_model model;
	struct menu_search_found found[FOUND_MOST];
	long matched, index;

	memset(&model, 0, sizeof(model));
	model.screens = test_screens;
	model.screen_count = (long)(sizeof(test_screens) / sizeof(*test_screens));
	model.rows = test_rows;
	model.row_count = (long)(sizeof(test_rows) / sizeof(*test_rows));
	if (!strncmp(case_name, "query:", 6))
	{
		matched = menu_search_rank(&model, case_name + 6, NULL, NULL, found, FOUND_MOST);
		for (index = 0; index < matched && index < FOUND_MOST; index++)
		{
			struct halo_menu_model_row const *row = &model.rows[found[index].row];

			CHECK(found[index].score == menu_search_score(row, case_name + 6), "a row's score is its own");
			CHECK(!index || found[index - 1].score >= found[index].score, "the rows found best first");
			printf("%s %ld %s\n", row->key, found[index].score, row->crumb ? row->crumb : "");
		}
		printf("matched %ld\n", matched);
		return 0;
	}
	if (!strncmp(case_name, "hint:", 5))
	{
		struct halo_menu_model_row const *hint = NULL;

		for (index = 0; index < model.screen_count && !hint; index++)
		{
			if (model.screens[index].layout && !strcmp(model.screens[index].layout, "search"))
				hint = menu_search_hint(&model, &model.screens[index], case_name + 5);
		}
		printf("%s\n", hint ? hint->help : "none");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
