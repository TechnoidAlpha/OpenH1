/*
The menus' hand-written screens (port/linux/src/menu_screens.c): a screen file
read into its building blocks, checked against the game's actions and
bindings (port/linux/game/menu_screen.c's, as names.inc has them), a file with
a problem refused with its line and the screen read before it kept, texts'
bindings filled, and an item's when. The real main menu's file
(port/assets/menus/screens/main.xml) is main_xml.
*/

#include "harness.h"
#include "expat.h"

#include <stdarg.h>

/* ---------- the fake world */

/* (the platform: a computer, VR's headset when vr_running) */
static int vr_running;

int halo_menus_platform(const char *name, unsigned long length)
{
	static char const *const known[] = { "desktop", "android", "vr", "hdr", "touch", "pointer", "computer" };
	unsigned index;

	for (index = 0; index < sizeof(known) / sizeof(known[0]); index++)
	{
		if (strlen(known[index]) == length && !strncmp(known[index], name, length))
		{
			if (!strncmp(name, "vr", length))
				return vr_running;
			return !strncmp(name, "desktop", length) || !strncmp(name, "computer", length) ||
				!strncmp(name, "pointer", length);
		}
	}
	return -1;
}

struct halo_menus { int unused; };
static struct halo_menus menus;

struct halo_menus const *halo_menus_load(void)
{
	return &menus;
}

/* the menus' files gathered: a screen file of the case's */
static char const *file_paths[4];
static char const *file_texts[4];
static long file_count;

int halo_menus_file_at(long index, char const **path, unsigned char const **data, unsigned long *size)
{
	if (index < 0 || index >= file_count)
		return 0;
	*path = file_paths[index];
	*data = (unsigned char const *)file_texts[index];
	*size = (unsigned long)strlen(file_texts[index]);
	return 1;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

static char logged[8192];

void platform_log(char const *format, ...)
{
	va_list arguments;
	size_t used = strlen(logged);

	va_start(arguments, format);
	vsnprintf(logged + used, sizeof(logged) - used, format, arguments);
	va_end(arguments);
	used = strlen(logged);
	if (used + 1 < sizeof(logged))
	{
		logged[used] = '\n';
		logged[used + 1] = 0;
	}
}

#include "menu_screens.h"
#include "under_test.inc"
#include "names.inc"
#include "main_xml.inc"

/* ---------- the cases */

static struct menu_screen_names const names =
{
	game_actions, sizeof(game_actions) / sizeof(game_actions[0]),
	game_bindings, sizeof(game_bindings) / sizeof(game_bindings[0]),
	game_sources, sizeof(game_sources) / sizeof(game_sources[0]),
	game_chats, sizeof(game_chats) / sizeof(game_chats[0]),
};

/* the count of a screen's blocks of a kind (in the block parent; -2: any) */
static int kind_total(struct menu_screen const *screen, int kind, int parent)
{
	int index, count = 0;

	for (index = 0; index < screen->block_count; index++)
		count += screen->blocks[index].kind == kind && (parent == -2 || screen->blocks[index].parent == parent);
	return count;
}

static int kind_first(struct menu_screen const *screen, int kind)
{
	int index;

	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].kind == kind)
			return index;
	}
	return -1;
}

/* a table screen, its table's insides given */
#define TABLE(inside) \
	"<screen name=\"t\"><table source=\"test.rows\">" inside "</table></screen>"
#define COLUMN "<column id=\"server\" label=\"SERVER\" width=\"110\"/>"

static int read_text(char const *text, struct menu_screen *screen, char *problem)
{
	return menu_screen_read("screens/test.xml", (unsigned char const *)text, (unsigned long)strlen(text), screen,
		problem, 512);
}

/* a file that must be refused, its problem naming what */
static void refused(char const *text, char const *what)
{
	struct menu_screen screen;
	char problem[512];

	CHECK(!read_text(text, &screen, problem), "read, but has a problem (%s): %s", what, text);
	CHECK(strstr(problem, what) != NULL, "the problem \"%s\" does not say \"%s\"", problem, what);
	CHECK(strstr(problem, "screens/test.xml:") == problem, "the problem names no file and line: %s", problem);
	CHECK(screen.name == NULL && screen.string_count == 0, "a refused screen keeps something");
}

static struct menu_screen_block const *block_of(struct menu_screen const *screen, char const *id)
{
	int index;

	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].id && !strcmp(screen->blocks[index].id, id))
			return &screen->blocks[index];
	}
	return NULL;
}

/* the values of the bindings: the saved game's, when there is one */
static int saved_game;

static int value(void *context, char const *name, char *out, unsigned long size)
{
	(void)context;
	if (!saved_game)
		return 0;
	if (!strcmp(name, "campaign.level"))
		snprintf(out, size, "%s", "The Silent Cartographer");
	else if (!strcmp(name, "campaign.difficulty"))
		snprintf(out, size, "%s", "Legendary");
	else
		return 0;
	return 1;
}

#define HINTED \
	"<screen name=\"main\"><list><item id=\"campaign\" label=\"CAMPAIGN\" do=\"open campaign\">" \
	"<hint>Continue \xC2\xB7 {campaign.level} \xC2\xB7 {campaign.difficulty}</hint>" \
	"<hint>The story \xC2\xB7 solo, or with friends online</hint></item></list></screen>"

#define GOOD(label) \
	"<screen name=\"main\" back=\"quit\"><title-art/><list focus=\"a\">" \
	"<item id=\"a\" label=\"" label "\" do=\"open multiplayer\"/><item id=\"b\" label=\"QUIT\" do=\"quit\"/></list>" \
	"<space/><buttons><button key=\"accept\" label=\"SELECT\"/></buttons></screen>"

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";
	struct menu_screen screen;
	char problem[512];

	menu_screens_names(&names);
	if (!strcmp(name, "main"))
	{
		struct menu_screen_block const *campaign, *versus, *list;
		int index, items = 0, buttons = 0, kinds[6] = { 0 };

		CHECK(read_text(main_xml, &screen, problem), "the main menu's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "main"), "its name is %s", screen.name);
		CHECK(screen.back && !strcmp(screen.back, "quit"), "its back is not quit");
		for (index = 0; index < screen.block_count; index++)
		{
			kinds[screen.blocks[index].kind]++;
			items += screen.blocks[index].kind == MENU_SCREEN_ITEM;
			buttons += screen.blocks[index].kind == MENU_SCREEN_BUTTON;
		}
		CHECK(kinds[MENU_SCREEN_TITLE_ART] == 1 && kinds[MENU_SCREEN_LIST] == 1, "not one title art and list");
		campaign = block_of(&screen, "campaign");
		versus = block_of(&screen, "multiplayer");
		CHECK(campaign && versus && block_of(&screen, "settings") && block_of(&screen, "quit"),
			"the four doors are not all there");
		CHECK(!strcmp(campaign->action, "open campaign") && !strcmp(versus->action, "open multiplayer"),
			"CAMPAIGN or MULTIPLAYER does not open its screen");
		CHECK(!strcmp(block_of(&screen, "quit")->action, "quit"), "QUIT does not quit");
		CHECK(campaign->picture && !strcmp(campaign->picture, "main_menu/menu_load_campaign") && versus->picture &&
			!strcmp(versus->picture, "main_menu/menu_multiplayer"), "CAMPAIGN or MULTIPLAYER is not its lettered picture");
		list = &screen.blocks[campaign->parent];
		CHECK(list->kind == MENU_SCREEN_LIST && list->focus && !strcmp(list->focus, "campaign"),
			"the list's focus is not CAMPAIGN");
		CHECK(items == 5 && buttons == 0, "%d items and %d buttons", items, buttons);
		/* (no PRO 2V2) */
		for (index = 0; index < screen.block_count; index++)
		{
			char const *label = screen.blocks[index].label;

			CHECK(!label || !strstr(label, "2V2"), "the main menu has %s", label);
		}
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "hints"))
	{
		char text[256];
		struct menu_screen_block const *campaign;

		CHECK(read_text(HINTED, &screen, problem), "the hinted screen is refused: %s", problem);
		campaign = block_of(&screen, "campaign");
		/* (no saved game: the first hint's binding has no value, so its line
		is not shown, and the second is) */
		saved_game = 0;
		CHECK(!menu_screen_text(campaign->hints[0], value, NULL, text, sizeof(text)),
			"a hint with a binding of no value is complete");
		CHECK(menu_screen_text(campaign->hints[1], value, NULL, text, sizeof(text)), "the story's hint is not");
		CHECK(!strcmp(text, "The story \xC2\xB7 solo, or with friends online"), "the story's hint is %s", text);
		saved_game = 1;
		CHECK(menu_screen_text(campaign->hints[0], value, NULL, text, sizeof(text)), "the saved game's is not");
		CHECK(!strcmp(text, "Continue \xC2\xB7 The Silent Cartographer \xC2\xB7 Legendary"),
			"the saved game's hint is %s", text);
		/* (too small a room: cut, not overrun) */
		{
			char small[8];

			menu_screen_text(campaign->hints[0], value, NULL, small, sizeof(small));
			CHECK(strlen(small) == 7, "the text overran its room: %s", small);
		}
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "good"))
	{
		CHECK(read_text(GOOD("VERSUS"), &screen, problem), "refused: %s", problem);
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "syntax"))
	{
		refused("<screen name=\"main\"><list><item label=\"A\" do=\"quit\"></list></screen>", "mismatched tag");
		refused("", "no element found");
	}
	else if (!strcmp(name, "elements"))
	{
		refused("<screen name=\"main\"><grid/></screen>", "<grid> is not one of");
		refused("<screen name=\"main\"><list><column id=\"a\" width=\"1\"/></list></screen>", "<column> is not one of");
		refused("<screen name=\"main\"><panel><tab id=\"a\" label=\"A\" do=\"quit\"/></panel></screen>",
			"<tab> is not one of");
		refused("<screen name=\"main\"><item label=\"A\" do=\"quit\"/></screen>", "<item> is not one of");
		refused("<screen name=\"main\"><list><item label=\"A\" do=\"quit\" colour=\"red\"/></list></screen>",
			"has no attribute colour");
		refused("<screen><title-art/></screen>", "has no name");
		refused("<screen name=\"main\"/>", "nothing in it");
		refused("<screen name=\"main\"><list/></screen>", "has no <item>");
		refused("<screen name=\"main\"><title-art/><title-art/></screen>", "one <title-art>");
	}
	else if (!strcmp(name, "spartan"))
	{
		/* (the profile color's screen: its colors, and its Spartan in the
		focused one, beside them) */
		CHECK(read_text("<screen name=\"main\"><colors id=\"c\"/><spartan x=\"380\" y=\"64\" width=\"200\" "
			"height=\"350\" color=\"focus\"/></screen>", &screen, problem), "refused: %s", problem);
		CHECK(screen.block_count == 2 && screen.blocks[0].kind == MENU_SCREEN_COLORS &&
			screen.blocks[1].kind == MENU_SCREEN_SPARTAN, "the blocks read");
		CHECK(screen.blocks[1].box_x == 380 && screen.blocks[1].box_y == 64 && screen.blocks[1].box_width == 200 &&
			screen.blocks[1].box_height == 350 && screen.blocks[1].color == MENU_SCREEN_SPARTAN_FOCUS &&
			screen.blocks[1].players == 1, "its box and color");
		menu_screen_free(&screen);
		/* (four side by side, of the players' profiles; its box by default) */
		CHECK(read_text("<screen name=\"main\"><spartan players=\"4\"/></screen>", &screen, problem), "refused: %s",
			problem);
		CHECK(screen.blocks[0].players == 4 && screen.blocks[0].color == MENU_SCREEN_SPARTAN_PROFILE &&
			screen.blocks[0].box_width > 0 && screen.blocks[0].box_height > 0, "four, of the profiles");
		menu_screen_free(&screen);
		refused("<screen name=\"main\"><spartan players=\"5\"/></screen>", "players are 1 to 4");
		refused("<screen name=\"main\"><spartan color=\"red\"/></screen>", "no binding {red}");
		/* (a binding's color, and in a panel a line of its height) */
		CHECK(read_text("<screen name=\"main\"><spartan color=\"profile.color\"/></screen>", &screen, problem),
			"refused: %s", problem);
		CHECK(screen.blocks[0].color == MENU_SCREEN_SPARTAN_BINDING &&
			!strcmp(screen.blocks[0].value, "profile.color"), "a binding's color");
		menu_screen_free(&screen);
		CHECK(read_text("<screen name=\"main\"><panel><heading text=\"A\"/><spartan height=\"150\" "
			"color=\"profile.color\"/></panel></screen>", &screen, problem), "refused: %s", problem);
		CHECK(screen.blocks[2].kind == MENU_SCREEN_SPARTAN && screen.blocks[2].parent == 0 &&
			screen.blocks[2].box_height == 150, "a panel's Spartan");
		menu_screen_free(&screen);
		refused("<screen name=\"main\"><panel><spartan x=\"4\"/></panel></screen>", "has no attribute x");
		refused("<screen name=\"main\"><panel><spartan height=\"400\"/></panel></screen>", "1 to 360");
		refused("<screen name=\"main\"><spartan color=\"profile.color\" players=\"2\"/></screen>",
			"one player's");
		/* (a lineup's names, a binding's: none of a profile's Spartan, none
		of no binding, none in a panel) */
		CHECK(read_text("<screen name=\"main\"><spartan color=\"test.value\" names=\"test.flag\"/></screen>", &screen,
			problem), "refused: %s", problem);
		CHECK(screen.blocks[0].names && !strcmp(screen.blocks[0].names, "test.flag"), "a lineup's names");
		menu_screen_free(&screen);
		CHECK(read_text("<screen name=\"main\"><spartan color=\"test.value\"/></screen>", &screen, problem),
			"refused: %s", problem);
		CHECK(!screen.blocks[0].names, "names without a names attribute");
		menu_screen_free(&screen);
		refused("<screen name=\"main\"><spartan names=\"test.flag\"/></screen>", "names are a lineup's");
		refused("<screen name=\"main\"><spartan color=\"test.value\" names=\"nothing.here\"/></screen>",
			"no binding {nothing.here}");
		refused("<screen name=\"main\"><panel><spartan color=\"test.value\" names=\"test.flag\"/></panel></screen>",
			"has no attribute names");
		/* (one of the screen's own and one in each panel: a player's card's
		beside a lineup; a touchscreen's height) */
		CHECK(read_text("<screen name=\"main\"><spartan color=\"test.value\" touch-height=\"60\"/><panel>"
			"<spartan height=\"120\" color=\"test.value\"/></panel><panel><spartan/></panel></screen>", &screen,
			problem), "refused: %s", problem);
		CHECK(kind_total(&screen, MENU_SCREEN_SPARTAN, -2) == 3 && screen.blocks[0].touch_box_height == 60 &&
			screen.blocks[2].touch_box_height == 120, "three Spartans, their touchscreen's heights");
		menu_screen_free(&screen);
		refused("<screen name=\"main\"><panel><spartan/><spartan/></panel></screen>", "a panel has one <spartan>");
		refused("<screen name=\"main\"><panel><spartan touch-height=\"0\"/></panel></screen>",
			"touch-height is a number");
		refused("<screen name=\"main\"><spartan width=\"wide\"/></screen>", "are numbers");
		refused("<screen name=\"main\"><spartan color=\"focus\" players=\"2\"/></screen>", "one player's");
		refused("<screen name=\"main\"><spartan/><spartan/></screen>", "one <spartan> of its own");
		refused("<screen name=\"main\"><colors/><list><item label=\"A\" do=\"quit\"/></list></screen>",
			"one <list>, <cards> or <colors>");
		refused("<screen name=\"main\"><colors/><colors/></screen>", "one <list>, <cards> or <colors>");
	}
	else if (!strcmp(name, "items"))
	{
		refused("<screen name=\"main\"><list><item do=\"quit\"/></list></screen>", "has no label");
		refused("<screen name=\"main\"><list><item label=\"A\"/></list></screen>", "has no do");
		refused("<screen name=\"main\"><list focus=\"x\"><item id=\"a\" label=\"A\" do=\"quit\"/></list></screen>",
			"focus names no item");
		refused("<screen name=\"main\"><list><item id=\"a\" label=\"A\" do=\"quit\"/>"
			"<item id=\"a\" label=\"B\" do=\"quit\"/></list></screen>", "two items' id");
		refused("<screen name=\"main\"><list><item label=\"A\" when=\"phone\" do=\"quit\"/></list></screen>",
			"when=\"phone\"");
		refused("<screen name=\"main\"><buttons><button key=\"z\" label=\"A\"/></buttons></screen>", "key is");
	}
	else if (!strcmp(name, "actions"))
	{
		refused("<screen name=\"main\"><list><item label=\"A\" do=\"open nowhere\"/></list></screen>",
			"no action \"open nowhere\"");
		refused("<screen name=\"main\" back=\"explode\"><title-art/></screen>", "no action \"explode\"");
		refused("<screen name=\"main\" start=\"explode\"><title-art/></screen>", "no action \"explode\"");
		CHECK(read_text("<screen name=\"main\" start=\"quit\"><title-art/></screen>", &screen, problem) &&
			screen.start && !strcmp(screen.start, "quit"), "a screen's start is not read: %s", problem);
		menu_screen_free(&screen);
		/* (every action the game has is one a file may name) */
		{
			unsigned index;

			for (index = 0; index < sizeof(game_actions) / sizeof(game_actions[0]); index++)
			{
				char text[256];

				snprintf(text, sizeof(text), "<screen name=\"main\"><list><item label=\"A\" do=\"%s\"/></list>"
					"</screen>", game_actions[index]);
				CHECK(read_text(text, &screen, problem), "the game's action %s is refused: %s", game_actions[index],
					problem);
				menu_screen_free(&screen);
			}
		}
	}
	else if (!strcmp(name, "bindings"))
	{
		refused("<screen name=\"main\"><list><item label=\"A {nothing.here}\" do=\"quit\"/></list></screen>",
			"no binding {nothing.here}");
		refused("<screen name=\"main\"><list><item label=\"A\" hint=\"{campaign.level\" do=\"quit\"/></list></screen>",
			"a { with no }");
		refused("<screen name=\"main\"><list><item label=\"A\" do=\"quit\"><hint>{}</hint></item></list></screen>",
			"a binding with no name");
		refused("<screen name=\"main\"><list><item label=\"A}\" do=\"quit\"/></list></screen>", "a } with no {");
	}
	else if (!strcmp(name, "when"))
	{
		vr_running = 0;
		CHECK(menu_screen_when(NULL), "no when does not hold");
		CHECK(!menu_screen_when("vr"), "vr holds out of the headset");
		CHECK(menu_screen_when("vr desktop"), "vr or desktop does not hold on the desktop");
		vr_running = 1;
		CHECK(menu_screen_when("vr"), "vr does not hold in the headset");
	}
	else if (!strcmp(name, "kept"))
	{
		struct menu_screen const *found;
		unsigned long generation;

		/* (the embedded file read, then a broken one in its place: the first
		kept, the problem logged; then a good one: read again) */
		file_paths[0] = "screens/main.xml";
		file_texts[0] = GOOD("VERSUS");
		file_paths[1] = "screens/blocks.xml";
		file_texts[1] = "<menus/>";
		file_count = 2;
		found = menu_screens_find("main");
		CHECK(found && !strcmp(found->blocks[2].label, "VERSUS"), "the embedded screen is not read");
		generation = menu_screens_generation("main");
		CHECK(generation, "no generation");
		CHECK(!strstr(logged, "blocks.xml"), "the blocks' file is read as a screen");
		screens_read("screens/main.xml", (unsigned char const *)"<screen name=\"main\"><list>", 26, 1);
		found = menu_screens_find("main");
		CHECK(found && !strcmp(found->blocks[2].label, "VERSUS"), "the broken file took the screen's place");
		CHECK(menu_screens_generation("main") == generation, "the broken file changed the generation");
		CHECK(strstr(logged, "screens/main.xml:1:") && strstr(logged, "the screen stays as it was"),
			"the problem is not logged: %s", logged);
		screens_read("screens/main.xml", (unsigned char const *)GOOD("FIGHT"), (unsigned long)strlen(GOOD("FIGHT")),
			1);
		found = menu_screens_find("main");
		CHECK(found && !strcmp(found->blocks[2].label, "FIGHT"), "the file read again is not the screen");
		CHECK(menu_screens_generation("main") > generation, "the generation did not change");
		CHECK(!menu_screens_find("versus"), "a screen no file has is found");
	}
	else if (!strcmp(name, "profile_select"))
	{
		struct menu_screen_block const *table, *spartan;

		/* (the real Select Profile's file: its table of profiles, its panel
		with the chosen one's Spartan in its color, and its bar) */
		CHECK(read_text(profile_select_xml, &screen, problem), "Select Profile's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "profile_select") && !screen.back, "its name or back");
		table = &screen.blocks[kind_first(&screen, MENU_SCREEN_TABLE)];
		CHECK(!strcmp(table->source, "profiles") && !strcmp(table->action, "profile select"),
			"the table's source or do");
		spartan = &screen.blocks[kind_first(&screen, MENU_SCREEN_SPARTAN)];
		CHECK(screen.blocks[spartan->parent].kind == MENU_SCREEN_PANEL &&
			spartan->color == MENU_SCREEN_SPARTAN_BINDING && !strcmp(spartan->value, "profile.color"),
			"the panel's Spartan, in the profile's color");
		CHECK(kind_total(&screen, MENU_SCREEN_BUTTON, -2) == 4, "%d buttons",
			kind_total(&screen, MENU_SCREEN_BUTTON, -2));
		/* (BACK at the bar's left, the rest at its right; the difficulty's
		line gone while the profile has none) */
		{
			int index, backs = 0, rights = 0, difficulty = 0;

			for (index = 0; index < screen.block_count; index++)
			{
				struct menu_screen_block const *block = &screen.blocks[index];

				if (block->kind == MENU_SCREEN_BUTTON && !strcmp(block->key, "back"))
					backs += !block->side;
				else if (block->kind == MENU_SCREEN_BUTTON)
					rights += block->side && !strcmp(block->side, "right");
				else if (block->kind == MENU_SCREEN_PANEL_TEXT && block->condition)
					difficulty += !strcmp(block->condition, "profile.difficulty");
			}
			CHECK(backs == 1 && rights == 3, "BACK at the left (%d), the rest at the right (%d)", backs, rights);
			CHECK(difficulty == 1, "the difficulty's line has no if");
		}
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "profile_color"))
	{
		struct menu_screen_block const *table, *spartan, *swatch;
		int table_index;

		/* (the real Profile Color's file: the tall layout's table of the
		colors, each its swatch and name; its panel with the focused color's
		Spartan; BACK at the bar's left, SELECT at its right) */
		CHECK(read_text(profile_color_xml, &screen, problem), "Profile Color's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "profile_color") && !screen.back && screen.tall_background,
			"its name, back or background");
		CHECK(kind_total(&screen, MENU_SCREEN_COLORS, -2) == 0, "it still has <colors>");
		table_index = kind_first(&screen, MENU_SCREEN_TABLE);
		CHECK(table_index >= 0, "no table");
		table = &screen.blocks[table_index];
		CHECK(!strcmp(table->source, "profile.colors") && !strcmp(table->action, "profile color"),
			"the table's source or do");
		swatch = &screen.blocks[kind_first(&screen, MENU_SCREEN_COLUMN)];
		CHECK(swatch->parent == table_index && swatch->cell == MENU_SCREEN_CELL_SWATCH && !strcmp(swatch->id, "swatch"),
			"the first column is not the swatch");
		spartan = &screen.blocks[kind_first(&screen, MENU_SCREEN_SPARTAN)];
		CHECK(screen.blocks[spartan->parent].kind == MENU_SCREEN_PANEL &&
			spartan->color == MENU_SCREEN_SPARTAN_BINDING && !strcmp(spartan->value, "color.spartan"),
			"the panel's Spartan, in the focused color");
		CHECK(kind_total(&screen, MENU_SCREEN_BUTTON, -2) == 2, "%d buttons",
			kind_total(&screen, MENU_SCREEN_BUTTON, -2));
		menu_screen_free(&screen);
		/* (a column of a kind there is none of refused, naming the kinds) */
		refused("<screen name=\"main\"><table source=\"test.rows\"><column id=\"a\" kind=\"chip\" width=\"9\"/>"
			"</table></screen>", "text, strong, ping, star, dot or swatch");
		/* (a background neither own nor tall refused) */
		refused("<screen name=\"main\" background=\"blue\"><list><item label=\"A\" do=\"quit\"/></list></screen>",
			"background is own or tall");
	}
	else if (!strcmp(name, "browser"))
	{
		struct menu_screen_block const *table, *server, *ping;
		int table_index, index, searches = 0, touch_searches = 0;

		/* (the real server browser's file: its tabs, its table's columns and
		their searches, its panel and its bar) */
		CHECK(read_text(browser_xml, &screen, problem), "the browser's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "browser") && screen.back && !strcmp(screen.back, "browser back"),
			"its name or back is not the browser's");
		CHECK(kind_total(&screen, MENU_SCREEN_TAB, -2) == 6, "%d tabs", kind_total(&screen, MENU_SCREEN_TAB, -2));
		table_index = kind_first(&screen, MENU_SCREEN_TABLE);
		CHECK(table_index >= 0, "no table");
		table = &screen.blocks[table_index];
		CHECK(!strcmp(table->source, "browser.games") && !strcmp(table->value, "browser.sort") &&
			!strcmp(table->action, "browser join") && table->empty, "the table's source, sort, do or empty");
		CHECK(kind_total(&screen, MENU_SCREEN_COLUMN, table_index) == 6, "%d columns",
			kind_total(&screen, MENU_SCREEN_COLUMN, table_index));
		server = block_of(&screen, "server");
		ping = block_of(&screen, "ping");
		CHECK(server && server->cell == MENU_SCREEN_CELL_STRONG && server->width == 110 && server->grow == 64 &&
			server->touch_width == 220 && server->title_width == 76, "the server's column");
		CHECK(ping && ping->cell == MENU_SCREEN_CELL_PING && ping->right, "the ping's column");
		CHECK(block_of(&screen, "players")->gap == 2 && block_of(&screen, "map")->gap == 4, "the columns' gaps");
		CHECK(block_of(&screen, "favorite")->cell == MENU_SCREEN_CELL_STAR &&
			!strcmp(block_of(&screen, "favorite")->action, "browser favorite"), "the star's column");
		for (index = 0; index < screen.block_count; index++)
		{
			if (screen.blocks[index].kind != MENU_SCREEN_SEARCH)
				continue;
			if (screen.blocks[index].parent < 0)
				touch_searches++;
			else
				searches++;
		}
		CHECK(searches == 2 && touch_searches == 1, "%d columns' searches and %d of the screen's", searches,
			touch_searches);
		CHECK(kind_total(&screen, MENU_SCREEN_PANEL, -2) == 1 && kind_total(&screen, MENU_SCREEN_PILL, -2) == 3 &&
			kind_total(&screen, MENU_SCREEN_PAIR, -2) == 2, "the panel");
		CHECK(kind_total(&screen, MENU_SCREEN_BUTTON, kind_first(&screen, MENU_SCREEN_BUTTONS)) == 8, "the bar's buttons");
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "tabs"))
	{
		CHECK(read_text("<screen name=\"t\"><tabs current=\"test.value\"><tab id=\"a\" label=\"A\" do=\"quit\"/>"
			"</tabs></screen>", &screen, problem), "good tabs refused: %s", problem);
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><tabs/></screen>", "has no <tab>");
		refused("<screen name=\"t\"><tabs><tab label=\"A\" do=\"quit\"/></tabs></screen>", "<tab> has no id");
		refused("<screen name=\"t\"><tabs><tab id=\"a\" do=\"quit\"/></tabs></screen>", "<tab> has no label");
		refused("<screen name=\"t\"><tabs><tab id=\"a\" label=\"A\"/></tabs></screen>", "<tab> has no do");
		refused("<screen name=\"t\"><tabs><tab id=\"a\" label=\"A\" do=\"quit\"/><tab id=\"a\" label=\"B\" "
			"do=\"quit\"/></tabs></screen>", "id is \"a\"");
		refused("<screen name=\"t\"><tabs current=\"nothing.here\"><tab id=\"a\" label=\"A\" do=\"quit\"/></tabs>"
			"</screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><tabs if=\"nothing.here\"><tab id=\"a\" label=\"A\" do=\"quit\"/></tabs>"
			"</screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><tabs><tab id=\"a\" label=\"A\" do=\"quit\"/></tabs><tabs><tab id=\"b\" "
			"label=\"B\" do=\"quit\"/></tabs></screen>", "one <tabs>");
	}
	else if (!strcmp(name, "table"))
	{
		struct menu_screen_block const *column;

		CHECK(read_text(TABLE(COLUMN "<column id=\"ping\" kind=\"ping\" width=\"54\" align=\"right\" "
			"touch=\"272 30 80 144 0\"><search value=\"test.value\"/></column>"), &screen, problem),
			"a good table refused: %s", problem);
		column = block_of(&screen, "ping");
		CHECK(column && column->right && column->touch_x == 272 && column->touch_y == 30 && column->touch_width == 80 &&
			column->touch_move == 144 && column->touch_grow == 0 && column->gap == 4, "the ping's column as read");
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><table>" COLUMN "</table></screen>", "has no source");
		refused("<screen name=\"t\"><table source=\"nowhere\">" COLUMN "</table></screen>",
			"no row source \"nowhere\"");
		refused(TABLE(""), "has no <column>");
		refused(TABLE("<column label=\"A\" width=\"10\"/>"), "<column> has no id");
		refused(TABLE("<column id=\"a\"/>"), "<column> has no width");
		refused(TABLE("<column id=\"a\" width=\"wide\"/>"), "width is a number");
		refused(TABLE("<column id=\"a\" width=\"10\" kind=\"bars\"/>"), "kind is text, strong, ping, star, dot or swatch");
		refused(TABLE("<column id=\"a\" width=\"10\" align=\"center\"/>"), "align is left or right");
		refused(TABLE("<column id=\"a\" width=\"10\" touch=\"1 2\"/>"), "touch is");
		refused(TABLE("<column id=\"a\" width=\"10\" sort=\"sort nowhere\"/>"), "no action \"sort nowhere\"");
		refused(TABLE(COLUMN COLUMN), "id is \"server\"");
		refused(TABLE("<column id=\"a\" width=\"10\"><search/></column>"), "<search> has no value");
		refused(TABLE("<column id=\"a\" width=\"10\"><search value=\"nothing.here\"/></column>"),
			"no binding {nothing.here}");
		refused("<screen name=\"t\"><table source=\"test.rows\" sorted=\"nothing.here\">" COLUMN "</table>"
			"</screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><table source=\"test.rows\" empty=\"{nothing.here}\">" COLUMN "</table>"
			"</screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\">" "<table source=\"test.rows\">" COLUMN "</table>"
			"<table source=\"test.rows\">" COLUMN "</table></screen>", "one <table>");
	}
	else if (!strcmp(name, "panel"))
	{
		CHECK(read_text("<screen name=\"t\"><panel><heading text=\"{test.value}\" lines=\"2\"/>"
			"<text text=\"A\" lines=\"3\" touch-lines=\"4\" if=\"test.flag\"/><pills><pill text=\"B\" tone=\"test.value\" "
			"width=\"64\"/><pill text=\"C\" tone=\"2\" width=\"64\"/></pills><pair label=\"P\" value=\"1\"/>"
			"<button key=\"accept\" label=\"JOIN\" do=\"quit\"/></panel></screen>", &screen, problem),
			"a good panel refused: %s", problem);
		CHECK(screen.blocks[kind_first(&screen, MENU_SCREEN_PANEL_TEXT)].lines == 3 &&
			screen.blocks[kind_first(&screen, MENU_SCREEN_PANEL_TEXT)].touch_lines == 4, "a text's lines");
		CHECK(screen.blocks[kind_first(&screen, MENU_SCREEN_PANEL_TEXT)].condition &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_PANEL_TEXT)].condition, "test.flag"), "a text's if");
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><panel/></screen>", "has nothing in it");
		refused("<screen name=\"t\"><panel><text text=\"A\" if=\"nothing.here\"/></panel></screen>",
			"no binding {nothing.here}");
		refused("<screen name=\"t\"><panel><text/></panel></screen>", "<text> has no text");
		refused("<screen name=\"t\"><panel><text text=\"A\" lines=\"9\"/></panel></screen>", "lines is a number");
		refused("<screen name=\"t\"><panel><pills/></panel></screen>", "has no <pill>");
		refused("<screen name=\"t\"><panel><pills><pill text=\"A\"/></pills></panel></screen>", "has no width");
		refused("<screen name=\"t\"><panel><pills><pill text=\"A\" width=\"64\" tone=\"nothing.here\"/></pills>"
			"</panel></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><panel><pills><pill text=\"A\" width=\"64\" tone=\"99\"/></pills></panel>"
			"</screen>", "tone is a number");
		refused("<screen name=\"t\"><panel><pair label=\"A\"/></panel></screen>", "<pair> has no value");
		refused("<screen name=\"t\"><panel><pair value=\"A\"/></panel></screen>", "<pair> has no label");
	}
	else if (!strcmp(name, "picture"))
	{
		struct menu_screen_block const *picture;

		/* (a panel's picture: its set, its frame's binding, its heights, its
		if) */
		CHECK(read_text("<screen name=\"t\"><panel><picture set=\"maps\" frame=\"test.value\" height=\"132\" "
			"touch-height=\"72\" if=\"test.flag\"/><heading text=\"A\"/></panel></screen>", &screen, problem),
			"a good picture refused: %s", problem);
		picture = &screen.blocks[kind_first(&screen, MENU_SCREEN_PICTURE)];
		CHECK(!strcmp(picture->picture, "maps") && !strcmp(picture->value, "test.value") && picture->box_height == 132 &&
			picture->touch_box_height == 72 && !strcmp(picture->condition, "test.flag") &&
			screen.blocks[picture->parent].kind == MENU_SCREEN_PANEL, "the picture as read");
		menu_screen_free(&screen);
		CHECK(read_text("<screen name=\"t\"><panel><picture set=\"missions\" frame=\"test.value\"/></panel>"
			"</screen>", &screen, problem), "a mission's picture refused: %s", problem);
		CHECK(screen.blocks[1].box_height == 120 && screen.blocks[1].touch_box_height == 120, "its height by default");
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><panel><picture frame=\"test.value\"/></panel></screen>", "set is maps or missions");
		refused("<screen name=\"t\"><panel><picture set=\"photos\" frame=\"test.value\"/></panel></screen>",
			"set is maps or missions");
		refused("<screen name=\"t\"><panel><picture set=\"maps\"/></panel></screen>", "<picture> has no frame");
		refused("<screen name=\"t\"><panel><picture set=\"maps\" frame=\"nothing.here\"/></panel></screen>",
			"no binding {nothing.here}");
		refused("<screen name=\"t\"><panel><picture set=\"maps\" frame=\"test.value\" height=\"300\"/></panel>"
			"</screen>", "height is a number");
		refused("<screen name=\"t\"><panel><picture set=\"maps\" frame=\"test.value\" if=\"nothing.here\"/>"
			"</panel></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><picture set=\"maps\" frame=\"test.value\"/></screen>", "is not one of what");
	}
	else if (!strcmp(name, "panels"))
	{
		/* (a screen's panels, up to four, each its if; a lineup, its own
		<spartan>, over a table of lanes alone) */
		CHECK(read_text("<screen name=\"t\"><panel if=\"test.flag\"><heading text=\"A\"/></panel>"
			"<panel if=\"test.value\"><heading text=\"B\"/></panel><panel><heading text=\"C\"/></panel>"
			"<panel><heading text=\"D\"/></panel></screen>", &screen, problem), "four panels refused: %s", problem);
		CHECK(kind_total(&screen, MENU_SCREEN_PANEL, -2) == 4 && !strcmp(screen.blocks[0].condition, "test.flag"),
			"the panels as read");
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><panel><heading text=\"A\"/></panel><panel><heading text=\"B\"/></panel>"
			"<panel><heading text=\"C\"/></panel><panel><heading text=\"D\"/></panel><panel><heading text=\"E\"/>"
			"</panel></screen>", "at most 4 <panel>s");
		refused("<screen name=\"t\"><panel if=\"nothing.here\"><heading text=\"A\"/></panel></screen>",
			"no binding {nothing.here}");
		CHECK(read_text("<screen name=\"t\"><spartan height=\"88\" color=\"test.value\"/><table source=\"test.rows\" "
			"lanes=\"2\"><lane label=\"A\"/><lane label=\"B\"/>" COLUMN "</table></screen>", &screen, problem),
			"a lineup over lanes refused: %s", problem);
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><spartan height=\"88\" color=\"test.value\"/>" "<table source=\"test.rows\">"
			COLUMN "</table></screen>", "stands over a table of lanes");
	}
	else if (!strcmp(name, "lanes"))
	{
		struct menu_screen_block const *table;

		/* (a table of lanes: its lanes, each its label, detail and tone (a
		number or a binding), its left and right; a dot's column) */
		CHECK(read_text("<screen name=\"t\"><table source=\"test.rows\" lanes=\"2\" left=\"quit\" right=\"quit\">"
			"<lane label=\"RED\" detail=\"{test.value}\" tone=\"0\"/><lane label=\"{test.value}\" tone=\"test.value\"/>"
			"<column id=\"dot\" kind=\"dot\" width=\"8\"/>" COLUMN "</table></screen>", &screen, problem),
			"a good table of lanes refused: %s", problem);
		table = &screen.blocks[kind_first(&screen, MENU_SCREEN_TABLE)];
		CHECK(table->lanes == 2 && !strcmp(table->left_action, "quit") && !strcmp(table->right_action, "quit"),
			"the table's lanes, left or right");
		CHECK(kind_total(&screen, MENU_SCREEN_LANE, kind_first(&screen, MENU_SCREEN_TABLE)) == 2 &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_LANE)].empty, "{test.value}"), "the lanes as read");
		CHECK(block_of(&screen, "dot")->cell == MENU_SCREEN_CELL_DOT, "the dot's column");
		menu_screen_free(&screen);
		CHECK(read_text(TABLE(COLUMN), &screen, problem) && screen.blocks[kind_first(&screen, MENU_SCREEN_TABLE)].lanes == 1,
			"a table without lanes is not of one lane: %s", problem);
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><table source=\"test.rows\" lanes=\"5\">" COLUMN "</table></screen>",
			"lanes is a number");
		refused("<screen name=\"t\"><table source=\"test.rows\"><lane label=\"A\"/>" COLUMN "</table></screen>",
			"lanes=\"2\" or more");
		refused("<screen name=\"t\"><table source=\"test.rows\" lanes=\"2\"><lane label=\"A\"/>" COLUMN "</table>"
			"</screen>", "a <lane> for each lane");
		refused("<screen name=\"t\"><table source=\"test.rows\" lanes=\"2\"><lane/><lane label=\"B\"/>" COLUMN
			"</table></screen>", "<lane> has no label");
		refused("<screen name=\"t\"><table source=\"test.rows\" lanes=\"2\"><lane label=\"A\" tone=\"nothing.here\"/>"
			"<lane label=\"B\"/>" COLUMN "</table></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><table source=\"test.rows\" lanes=\"2\"><lane label=\"A\" tone=\"7\"/>"
			"<lane label=\"B\"/>" COLUMN "</table></screen>", "tone is a number");
		refused("<screen name=\"t\"><table source=\"test.rows\" lanes=\"2\"><lane label=\"{nothing.here}\"/>"
			"<lane label=\"B\"/>" COLUMN "</table></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><table source=\"test.rows\" left=\"go nowhere\">" COLUMN "</table></screen>",
			"no action \"go nowhere\"");
		refused("<screen name=\"t\"><table source=\"test.rows\" right=\"go nowhere\">" COLUMN "</table></screen>",
			"no action \"go nowhere\"");
	}
	else if (!strcmp(name, "choice"))
	{
		struct menu_screen_block const *choice;

		/* (a panel's choice: its label, value, left and right, its if and
		dim; and several buttons, one over another) */
		CHECK(read_text("<screen name=\"t\"><panel><choice label=\"BOTS\" value=\"{test.value}\" left=\"quit\" "
			"right=\"quit\" if=\"test.flag\" dim=\"test.flag\"/><button key=\"accept\" label=\"A\" do=\"quit\"/>"
			"<button key=\"accept\" label=\"B\" do=\"quit\"/></panel></screen>", &screen, problem),
			"a good choice refused: %s", problem);
		choice = &screen.blocks[kind_first(&screen, MENU_SCREEN_CHOICE)];
		CHECK(!strcmp(choice->label, "BOTS") && !strcmp(choice->hints[0], "{test.value}") &&
			!strcmp(choice->left_action, "quit") && !strcmp(choice->right_action, "quit") &&
			!strcmp(choice->condition, "test.flag") && !strcmp(choice->dim, "test.flag"), "the choice as read");
		CHECK(kind_total(&screen, MENU_SCREEN_BUTTON, kind_first(&screen, MENU_SCREEN_PANEL)) == 2, "the panel's buttons");
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><panel><choice value=\"V\" left=\"quit\" right=\"quit\"/></panel></screen>",
			"<choice> has no label");
		refused("<screen name=\"t\"><panel><choice label=\"A\" left=\"quit\" right=\"quit\"/></panel></screen>",
			"<choice> has no value");
		refused("<screen name=\"t\"><panel><choice label=\"A\" value=\"V\" left=\"quit\"/></panel></screen>",
			"no left or no right");
		refused("<screen name=\"t\"><panel><choice label=\"A\" value=\"V\" right=\"quit\"/></panel></screen>",
			"no left or no right");
		refused("<screen name=\"t\"><panel><choice label=\"A\" value=\"V\" left=\"go nowhere\" right=\"quit\"/>"
			"</panel></screen>", "no action \"go nowhere\"");
		refused("<screen name=\"t\"><panel><choice label=\"A\" value=\"{nothing.here}\" left=\"quit\" right=\"quit\"/>"
			"</panel></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><panel><choice label=\"A\" value=\"V\" left=\"quit\" right=\"quit\" "
			"dim=\"nothing.here\"/></panel></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><choice label=\"A\" value=\"V\" left=\"quit\" right=\"quit\"/></screen>",
			"is not one of what");
	}
	else if (!strcmp(name, "chat"))
	{
		struct menu_screen_block const *chat;

		/* (a chat under a table: its source, lines, do and if) */
		CHECK(read_text("<screen name=\"t\"><table source=\"test.rows\">" COLUMN "</table><chat source=\"test.chat\" "
			"lines=\"4\" do=\"quit\" if=\"test.flag\"/></screen>", &screen, problem), "a good chat refused: %s", problem);
		chat = &screen.blocks[kind_first(&screen, MENU_SCREEN_CHAT)];
		CHECK(!strcmp(chat->source, "test.chat") && chat->lines == 4 && !strcmp(chat->action, "quit") &&
			!strcmp(chat->condition, "test.flag"), "the chat as read");
		menu_screen_free(&screen);
		CHECK(read_text("<screen name=\"t\"><chat source=\"test.chat\"/></screen>", &screen, problem) &&
			screen.blocks[kind_first(&screen, MENU_SCREEN_CHAT)].lines == 5, "a chat's lines are not 5 at first: %s",
			problem);
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><chat/></screen>", "<chat> has no source");
		refused("<screen name=\"t\"><chat source=\"nowhere\"/></screen>", "there is no chat \"nowhere\"");
		refused("<screen name=\"t\"><chat source=\"test.chat\" lines=\"20\"/></screen>", "lines is a number");
		refused("<screen name=\"t\"><chat source=\"test.chat\" do=\"go nowhere\"/></screen>", "no action \"go nowhere\"");
		refused("<screen name=\"t\"><chat source=\"test.chat\" if=\"nothing.here\"/></screen>",
			"no binding {nothing.here}");
		refused("<screen name=\"t\"><chat source=\"test.chat\"/><chat source=\"test.chat\"/></screen>", "one <chat>");
		refused("<screen name=\"t\"><panel><chat source=\"test.chat\"/></panel></screen>", "is not one of what");
	}
	else if (!strcmp(name, "lobby"))
	{
		struct menu_screen_block const *table;
		int table_index, index, choices = 0, panel_buttons = 0, rights = 0;

		/* (the real lobby's file: its table of two lanes (red's and blue's)
		of the players, its panel's choices (the host's bots) and buttons,
		its bar: LEAVE at the left, the rest at the right) */
		CHECK(read_text(lobby_xml, &screen, problem), "the lobby's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "lobby") && !screen.back, "its name or back");
		table_index = kind_first(&screen, MENU_SCREEN_TABLE);
		table = &screen.blocks[table_index];
		CHECK(!strcmp(table->source, "lobby.players") && table->lanes == 2 && !strcmp(table->left_action, "lobby team red")
			&& !strcmp(table->right_action, "lobby team blue") && !strcmp(table->action, "lobby card"), "the table");
		CHECK(kind_total(&screen, MENU_SCREEN_LANE, table_index) == 2, "the lanes");
		CHECK(block_of(&screen, "dot")->cell == MENU_SCREEN_CELL_DOT && block_of(&screen, "tag")->right, "the columns");
		for (index = 0; index < screen.block_count; index++)
		{
			struct menu_screen_block const *block = &screen.blocks[index];

			choices += block->kind == MENU_SCREEN_CHOICE;
			panel_buttons += block->kind == MENU_SCREEN_BUTTON && screen.blocks[block->parent].kind == MENU_SCREEN_PANEL;
			rights += block->kind == MENU_SCREEN_BUTTON && block->side && !strcmp(block->side, "right");
		}
		/* (the panels' buttons: a co-op host's fix too, coop.fix; the bar's
		right: a co-op host's COPY LINK too) */
		CHECK(choices == 4 && panel_buttons == 9, "%d choices, %d panel buttons", choices, panel_buttons);
		CHECK(rights == 7, "%d buttons at the bar's right", rights);
		CHECK(kind_first(&screen, MENU_SCREEN_CHAT) >= 0 &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_CHAT)].source, "lobby.chat"), "the lobby's chat");
		/* (its panels: the next game's (its map's picture), a player's card
		(their Spartan) and the friends' invite, each its if; the lineup, its
		own Spartan) */
		CHECK(kind_total(&screen, MENU_SCREEN_PANEL, -2) == 3 && kind_total(&screen, MENU_SCREEN_PICTURE, -2) == 1,
			"the panels and the picture");
		CHECK(kind_total(&screen, MENU_SCREEN_SPARTAN, -1) == 1 && kind_total(&screen, MENU_SCREEN_SPARTAN, -2) == 2 &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_SPARTAN)].value, "lobby.lineup") &&
			screen.blocks[kind_first(&screen, MENU_SCREEN_SPARTAN)].names &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_SPARTAN)].names, "lobby.lineup.names"),
			"the lineup and the card's Spartan");
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "buttons"))
	{
		static char const *const keys[] = { "accept", "back", "x", "y", "lb", "rb", "lt", "rt", "r3", "view", "dpad" };
		unsigned index;

		for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
		{
			char text[256];

			snprintf(text, sizeof(text), "<screen name=\"t\"><buttons><button key=\"%s\" label=\"A\" width=\"90\" "
				"if=\"test.flag\" do=\"quit\"/></buttons></screen>", keys[index]);
			CHECK(read_text(text, &screen, problem), "the key %s refused: %s", keys[index], problem);
			menu_screen_free(&screen);
		}
		refused("<screen name=\"t\"><buttons><button key=\"l3\" label=\"A\"/></buttons></screen>", "key is");
		refused("<screen name=\"t\"><buttons><button key=\"x\" label=\"A\" do=\"nowhere\"/></buttons></screen>",
			"no action \"nowhere\"");
		refused("<screen name=\"t\"><buttons><button key=\"x\" label=\"A\" if=\"nothing.here\"/></buttons></screen>",
			"no binding {nothing.here}");
		refused("<screen name=\"t\"><buttons><button key=\"x\" label=\"A\" width=\"0\"/></buttons></screen>",
			"width is a number");
		refused("<screen name=\"t\"><buttons><button key=\"x\" label=\"A\" touch-width=\"0\"/></buttons></screen>",
			"touch-width is a number");
		CHECK(read_text("<screen name=\"t\"><buttons><button key=\"x\" label=\"A\" touch-width=\"80\"/></buttons>"
			"</screen>", &screen, problem) && screen.blocks[kind_first(&screen, MENU_SCREEN_BUTTON)].touch_width == 80,
			"a button's touch-width is not read: %s", problem);
		menu_screen_free(&screen);
		refused("<screen name=\"t\"><header title=\"T\"><button key=\"view\" label=\"{nothing.here}\"/></header>"
			"</screen>", "no binding {nothing.here}");
	}
	else if (!strcmp(name, "box"))
	{
		/* the pause menus' files: a <box> of rows (an if each may have),
		its legend, and the campaign's objectives beside it */
		struct menu_screen_block const *box, *list, *legend;
		int box_index, list_index;

		CHECK(read_text(pause_xml, &screen, problem), "the pause menu's file is refused: %s", problem);
		box_index = kind_first(&screen, MENU_SCREEN_BOX);
		list_index = kind_first(&screen, MENU_SCREEN_LIST);
		CHECK(box_index >= 0 && list_index >= 0, "no box, or no list");
		box = &screen.blocks[box_index];
		list = &screen.blocks[list_index];
		CHECK(box->box_x == 206 && box->box_y == 243 && box->box_width == 226 && box->side &&
			!strcmp(box->side, "middle"), "the box's place %ld %ld %ld", box->box_x, box->box_y, box->box_width);
		CHECK(list->parent == box_index, "the list not in the box");
		CHECK(kind_total(&screen, MENU_SCREEN_ITEM, list_index) == 6, "%d rows",
			kind_total(&screen, MENU_SCREEN_ITEM, list_index));
		CHECK(block_of(&screen, "host") && block_of(&screen, "host")->condition &&
			!strcmp(block_of(&screen, "host")->condition, "pause.host_controls"), "HOST CONTROLS' if");
		CHECK(!block_of(&screen, "resume")->condition, "RESUME GAME has an if");
		legend = &screen.blocks[kind_first(&screen, MENU_SCREEN_LEGEND)];
		CHECK(legend->parent == box_index && legend->box_x == 29, "the legend");
		CHECK(kind_total(&screen, MENU_SCREEN_OBJECTIVES, -2) == 0, "objectives in the multiplayer pause menu");
		menu_screen_free(&screen);
		CHECK(read_text(pause_campaign_xml, &screen, problem), "the campaign's pause menu's file is refused: %s",
			problem);
		CHECK(kind_total(&screen, MENU_SCREEN_OBJECTIVES, -1) == 1 &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_OBJECTIVES)].label, "MISSION OBJECTIVES:"),
			"the campaign's objectives");
		CHECK(!screen.blocks[kind_first(&screen, MENU_SCREEN_BOX)].side, "the campaign's box aligned");
		CHECK(block_of(&screen, "friendly_fire") && !strcmp(block_of(&screen, "friendly_fire")->label,
			"{pause.friendly_fire}"), "FRIENDLY FIRE's label");
		menu_screen_free(&screen);
		/* (what a box must have and may not) */
		refused("<screen name=\"t\"><box y=\"10\" width=\"200\"><list><item label=\"A\" do=\"quit\"/></list>"
			"</box></screen>", "x, y and width");
		refused("<screen name=\"t\"><box x=\"1\" y=\"10\" width=\"200\" align=\"bottom\"><list><item label=\"A\" "
			"do=\"quit\"/></list></box></screen>", "align is top or middle");
		refused("<screen name=\"t\"><box x=\"1\" y=\"10\" width=\"20\"><list><item label=\"A\" do=\"quit\"/></list>"
			"</box></screen>", "width is a number");
		refused("<screen name=\"t\"><box x=\"1\" y=\"10\" width=\"200\"><legend/></box></screen>", "has a <list>");
		refused("<screen name=\"t\"><legend/></screen>", "<legend> is not one of");
		refused("<screen name=\"t\"><objectives title=\"O\"/><box x=\"1\" y=\"10\" width=\"200\"><list>"
			"<item label=\"A\" do=\"quit\"/></list></box></screen>", "beside a <box>");
		refused("<screen name=\"t\"><box x=\"1\" y=\"10\" width=\"200\"><list><item label=\"A\" do=\"quit\" "
			"if=\"nothing.here\"/></list></box></screen>", "no binding {nothing.here}");
		refused("<screen name=\"t\"><box x=\"1\" y=\"10\" width=\"200\"><list><item label=\"A\" do=\"quit\"/>"
			"</list></box><box x=\"1\" y=\"10\" width=\"200\"/></screen>", "one <box>");
	}
	else if (!strcmp(name, "dialog"))
	{
		/* the pop-ups' files (#901): a <dialog>'s title and words, and its
		list's items its choices; the question as the gametype editor is
		left with changes (SAVE, DON'T SAVE, CANCEL) and Quit's */
		struct menu_screen_block const *dialog;
		int dialog_index, list_index;

		CHECK(read_text(save_changes_xml, &screen, problem), "the save question's file is refused: %s", problem);
		dialog_index = kind_first(&screen, MENU_SCREEN_DIALOG);
		list_index = kind_first(&screen, MENU_SCREEN_LIST);
		CHECK(dialog_index >= 0 && list_index >= 0, "no dialog, or no list");
		dialog = &screen.blocks[dialog_index];
		CHECK(dialog->label && !strcmp(dialog->label, "SAVE CHANGES?"), "its title");
		CHECK(dialog->text && strstr(dialog->text, "unsaved changes"), "its words");
		CHECK(screen.blocks[list_index].parent == dialog_index, "the list not in the dialog");
		CHECK(kind_total(&screen, MENU_SCREEN_ITEM, list_index) == 3, "%d choices",
			kind_total(&screen, MENU_SCREEN_ITEM, list_index));
		CHECK(block_of(&screen, "save") && !strcmp(block_of(&screen, "save")->action, "edit save") &&
			block_of(&screen, "discard") && !strcmp(block_of(&screen, "discard")->action, "edit discard") &&
			block_of(&screen, "cancel") && !strcmp(block_of(&screen, "cancel")->action, "back"),
			"its choices' actions");
		CHECK(!strcmp(screen.blocks[list_index].focus, "save"), "its focus");
		menu_screen_free(&screen);
		CHECK(read_text(quit_xml, &screen, problem), "Quit's file is refused: %s", problem);
		CHECK(kind_total(&screen, MENU_SCREEN_DIALOG, -1) == 1 &&
			kind_total(&screen, MENU_SCREEN_ITEM, kind_first(&screen, MENU_SCREEN_LIST)) == 2, "Quit's two choices");
		CHECK(block_of(&screen, "ok") && !strcmp(block_of(&screen, "ok")->action, "quit game"), "Quit's OK");
		menu_screen_free(&screen);
		/* (what a dialog must have and may not) */
		refused("<screen name='t'><dialog title='T' text='W'/></screen>", "<list> of its choices");
		refused("<screen name='t'><dialog text='W'><list><item label='A' do='back'/></list></dialog>"
			"</screen>", "has its title");
		refused("<screen name='t'><dialog title='T'><list><item label='A' do='back'/>"
			"<item label='B' do='back'/><item label='C' do='back'/><item label='D' do='back'/>"
			"<item label='E' do='back'/></list></dialog></screen>", "at most 4 choices");
		refused("<screen name='t'><dialog title='T' text='{nothing.here}'><list><item label='A' do='back'/>"
			"</list></dialog></screen>", "no binding {nothing.here}");
		refused("<screen name='t'><dialog title='T'><list><item label='A' do='open nowhere'/></list>"
			"</dialog></screen>", "no action");
		refused("<screen name='t'><dialog title='T'><list><item label='A' do='back'/></list></dialog>"
			"<dialog title='U'/></screen>", "one <dialog>");
		refused("<screen name='t'><list><item label='A' do='back'/></list><dialog title='T'/></screen>",
			"with no <box>, <table> or <list> beside it");
		refused("<screen name='t'><dialog title='T' words='W'><list><item label='A' do='back'/></list>"
			"</dialog></screen>", "no attribute words");
	}
	else if (!strcmp(name, "coop"))
	{
		static char const *const coop_actions[] = { "coop host", "coop join", "coop split" };
		static char const *const who_actions[] = { "coop host friends", "coop host anyone", "coop host invite" };
		static char const *const columns[] = { "host", "mission", "difficulty", "players", "ping" };
		struct menu_screen_block const *table, *fix;
		int index, table_index, cards = 0;

		/* (co-op's one screen: HOST, JOIN and SPLIT SCREEN's cards, HOST
		focused first) */
		CHECK(read_text(coop_xml, &screen, problem), "co-op's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "coop") && kind_total(&screen, MENU_SCREEN_CARDS, -2) == 1 &&
			!strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_CARDS)].focus, "host"), "co-op's cards");
		for (index = 0; index < screen.block_count; index++)
		{
			if (screen.blocks[index].kind != MENU_SCREEN_CARD)
				continue;
			CHECK(cards < 3 && !strcmp(screen.blocks[index].action, coop_actions[cards]), "co-op's card %d", cards);
			cards++;
		}
		CHECK(cards == 3, "%d co-op cards", cards);
		menu_screen_free(&screen);
		/* (WHO CAN JOIN: FRIENDS, ANYONE, INVITE ONLY, each hosting) */
		CHECK(read_text(coop_who_xml, &screen, problem), "WHO CAN JOIN's file is refused: %s", problem);
		cards = 0;
		for (index = 0; index < screen.block_count; index++)
		{
			if (screen.blocks[index].kind != MENU_SCREEN_CARD)
				continue;
			CHECK(cards < 3 && !strcmp(screen.blocks[index].action, who_actions[cards]), "WHO CAN JOIN's card %d",
				cards);
			cards++;
		}
		CHECK(cards == 3 && !strcmp(screen.blocks[kind_first(&screen, MENU_SCREEN_CARDS)].focus, "friends"),
			"WHO CAN JOIN's cards");
		menu_screen_free(&screen);
		/* (the co-op list: the server browser's games, its columns the
		host, mission, difficulty, players and ping, an empty list's fix
		under its words, no tabs nor searches) */
		CHECK(read_text(coop_join_xml, &screen, problem), "the co-op list's file is refused: %s", problem);
		table_index = kind_first(&screen, MENU_SCREEN_TABLE);
		CHECK(table_index >= 0, "no table");
		table = &screen.blocks[table_index];
		CHECK(!strcmp(table->source, "browser.games") && !strcmp(table->action, "browser join") && table->empty,
			"the co-op list's source, do or empty");
		CHECK(kind_total(&screen, MENU_SCREEN_COLUMN, table_index) == 5, "%d columns",
			kind_total(&screen, MENU_SCREEN_COLUMN, table_index));
		for (index = 0; index < 5; index++)
			CHECK(block_of(&screen, columns[index]) != NULL, "no %s column", columns[index]);
		CHECK(block_of(&screen, "ping")->cell == MENU_SCREEN_CELL_PING, "the ping's column");
		fix = &screen.blocks[kind_first(&screen, MENU_SCREEN_BUTTON)];
		CHECK(fix->parent == table_index && !strcmp(fix->action, "coop fix") && !strcmp(fix->condition, "browser.fix"),
			"the empty list's fix");
		CHECK(kind_total(&screen, MENU_SCREEN_TAB, -2) == 0 && kind_total(&screen, MENU_SCREEN_SEARCH, -2) == 0,
			"the co-op list has tabs or searches");
		menu_screen_free(&screen);
		/* (the server browser's CO-OP tab: co-op's screen) */
		CHECK(read_text(browser_xml, &screen, problem), "the browser's file is refused: %s", problem);
		CHECK(block_of(&screen, "coop") && !strcmp(block_of(&screen, "coop")->action, "open coop"),
			"the browser's CO-OP tab does not open co-op's screen");
		menu_screen_free(&screen);
	}
	else if (!strcmp(name, "add_player"))
	{
		static char const *const columns[] = { "color", "controller", "profile", "state" };
		struct menu_screen_block const *done;
		int index, table_index;

		/* (Add Player: the four controllers' rows, its back and Start the
		screen's own (those joining let go of; brought in), DONE in the
		panel once all are ready) */
		CHECK(read_text(add_player_xml, &screen, problem), "Add Player's file is refused: %s", problem);
		CHECK(!strcmp(screen.name, "add_player") && !strcmp(screen.back, "add player back") &&
			!strcmp(screen.start, "add player done"), "Add Player's name, back or start");
		table_index = kind_first(&screen, MENU_SCREEN_TABLE);
		CHECK(table_index >= 0 && !strcmp(screen.blocks[table_index].source, "add.players"), "Add Player's table");
		for (index = 0; index < 4; index++)
			CHECK(block_of(&screen, columns[index]) != NULL, "no %s column", columns[index]);
		CHECK(block_of(&screen, "color")->cell == MENU_SCREEN_CELL_SWATCH, "the color's column");
		done = &screen.blocks[kind_first(&screen, MENU_SCREEN_BUTTON)];
		CHECK(!strcmp(done->action, "add player done") && !strcmp(done->condition, "add.done"), "DONE");
		menu_screen_free(&screen);
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
