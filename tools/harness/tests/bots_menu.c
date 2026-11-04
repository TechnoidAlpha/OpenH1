#include "harness.h"

/* The bots in the menus and the game: Server Setup in a game of this machine
alone (menu_functions.c's server_settings_local: the network's rows hidden,
its line in their place; the bots are the lobby's), the lobby's BOTS
(lobby_menu.inc's lobby_board_bots_choice and lobby_board_bots_load: the
choice the settings make shown, one the host steps to written, a locked one
put back), a spinner of the menus stopping at its ends (ui_widget.c's
ui_widget_port_spinner_stops: never round from the most bots to none), and
a game the bots left going on (game_engine.c's game_engine_should_end_game:
one team left alone by the bots that left does not end it) */

#include <wchar.h>

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define platform_log(...) ((void)0)

enum { _ui_widget_type_container, _ui_widget_type_text_box, _ui_widget_type_spinner_list,
	_ui_widget_type_column_list };

struct widget_instance
{
	char const *name;
	short type;
	long definition_tag_index;
	boolean visible;
	struct widget_instance *parent, *child, *next;
	union
	{
		struct
		{
			short selected_index;
			short number_of_items;
		} list;
	} parameters;
};

/* ---- the fake world */

/* the menus' tags (pc_menu_tag): 1 */
static boolean pc_menu_tag(long tag_index) { return tag_index == 1; }

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	struct widget_instance *child;

	(void)nth;
	if (!widget)
		return NULL;
	if (!strcmp(widget->name, name))
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *found = named(child, name, 0);

		if (found)
			return found;
	}
	return NULL;
}

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget && widget->parent)
		widget = widget->parent;
	return widget;
}

static void visible_set(struct widget_instance *widget, boolean visible)
{
	if (widget)
		widget->visible = visible;
}

/* the BOTS choices (network_host_controls.c's): 0 NONE, then 10 more; the
settings' choice, and the last written */
enum { _host_bots_none, _host_bots_add, _host_bots_fill };
static short choice_settings;
static short choice_written = NONE;
static boolean choice_refused;
static short network_host_controls_bot_choice_count(void) { return 11; }
static short network_host_controls_bots_chosen(void) { return choice_settings; }
static short network_host_controls_bot_choice(short choice, long *number)
{
	*number = choice;
	return choice ? _host_bots_add : _host_bots_none;
}
static boolean network_host_controls_choose_bots(short kind, long number)
{
	(void)kind;
	if (choice_refused)
		return FALSE;
	choice_written = (short)number;
	choice_settings = (short)number;
	return TRUE;
}
static struct
{
	short bots_shown;
} lobby_board;

/* the players (their datums: quit, a bot), the gametype's state */
#define TICKS_PER_SECOND 30
struct player_datum
{
	boolean quit_out_of_game;
	boolean bot;
};
struct data_iterator
{
	long index;
	long datum_index;
};
static struct player_datum players[4];
static long player_count;
static void *player_data;
static void data_iterator_new(struct data_iterator *iterator, void *data)
{
	(void)data;
	iterator->index = 0;
}
static void *data_iterator_next(struct data_iterator *iterator)
{
	if (iterator->index >= player_count)
		return NULL;
	iterator->datum_index = iterator->index;
	return &players[iterator->index++];
}
static int bots_player_is_bot(long player_index) { return players[player_index].bot; }
static boolean teams_alive;
static boolean multiple_teams_alive(void) { return teams_alive; }
static boolean infection_enabled(void) { return FALSE; }
static int game_engine_value = 1;
static int *game_engine = &game_engine_value;
struct game_variant_options
{
	long time_limit;
};
static struct game_variant_options variant_options;
static struct game_variant_options *game_variant_options_get(void) { return &variant_options; }
static long game_time_get(void) { return 100; }

#include "under_test.inc"

/* Server Setup: in a game of this machine alone the network's rows hidden,
its line shown; in a network game the other way round */
static void case_local(void)
{
	static struct widget_instance screen, list, game_mode, invite, map_share, note;

	screen = (struct widget_instance){ "server_settings_screen" };
	list = (struct widget_instance){ "options_menu" };
	game_mode = (struct widget_instance){ "op_game_mode" };
	invite = (struct widget_instance){ "op_invite" };
	map_share = (struct widget_instance){ "op_map_share_options" };
	note = (struct widget_instance){ "local_note" };
	screen.child = &list;
	list.parent = &screen;
	list.next = &note;
	note.parent = &screen;
	list.child = &game_mode;
	game_mode.next = &invite;
	invite.next = &map_share;
	game_mode.parent = invite.parent = map_share.parent = &list;
	server_settings_local(&list, TRUE);
	CHECK(!game_mode.visible && !invite.visible && !map_share.visible, "a local game's network rows shown");
	CHECK(note.visible, "a local game's line hidden");
	server_settings_local(&list, FALSE);
	CHECK(game_mode.visible && invite.visible && map_share.visible && !note.visible, "a network game's rows");
}

/* a spinner of the menus stops at its ends (more than two values); two
toggle, and the Xbox's own spinners wrap as they did */
static void case_spinner_ends(void)
{
	struct widget_instance spinner = { "spinner", _ui_widget_type_spinner_list, 1 };

	spinner.parameters.list.number_of_items = 5;
	spinner.parameters.list.selected_index = 4;
	CHECK(ui_widget_port_spinner_stops(&spinner, TRUE), "the last steps on round to the first");
	CHECK(!ui_widget_port_spinner_stops(&spinner, FALSE), "the last does not step back");
	spinner.parameters.list.selected_index = 0;
	CHECK(ui_widget_port_spinner_stops(&spinner, FALSE), "the first steps back round to the last");
	CHECK(!ui_widget_port_spinner_stops(&spinner, TRUE), "the first does not step on");
	spinner.parameters.list.selected_index = 2;
	CHECK(!ui_widget_port_spinner_stops(&spinner, TRUE) && !ui_widget_port_spinner_stops(&spinner, FALSE),
		"a middle value stops");
	spinner.parameters.list.number_of_items = 2;
	spinner.parameters.list.selected_index = 1;
	CHECK(!ui_widget_port_spinner_stops(&spinner, TRUE), "ON and OFF do not toggle");
	spinner.parameters.list.number_of_items = 5;
	spinner.parameters.list.selected_index = 4;
	spinner.definition_tag_index = 2;
	CHECK(!ui_widget_port_spinner_stops(&spinner, TRUE), "the Xbox's spinner stops");
	spinner.definition_tag_index = 1;
	spinner.type = _ui_widget_type_column_list;
	CHECK(!ui_widget_port_spinner_stops(&spinner, TRUE), "a list stops");
}

/* the lobby's BOTS: made, it shows the settings' choice and writes nothing;
stepped, it writes the choice; settings changed elsewhere (Host Controls)
shown; locked, or refused, the settings' put back */
static void case_lobby_bots(void)
{
	struct widget_instance spinner = { "board_bots_count_spinner", _ui_widget_type_spinner_list, 1 };

	spinner.parameters.list.number_of_items = 11;
	spinner.parameters.list.selected_index = 0;
	choice_settings = 6;
	lobby_board.bots_shown = 3;
	lobby_board_bots_load(&spinner);
	CHECK(spinner.parameters.list.selected_index == 6 && choice_written == NONE, "made: %d shown, %d written",
		spinner.parameters.list.selected_index, choice_written);
	spinner.parameters.list.selected_index = 7;
	lobby_board_bots_choice(&spinner, FALSE);
	CHECK(choice_written == 7 && spinner.parameters.list.selected_index == 7, "stepped to 7: %d written",
		choice_written);
	choice_written = NONE;
	choice_settings = 2;
	lobby_board_bots_choice(&spinner, FALSE);
	CHECK(spinner.parameters.list.selected_index == 2 && choice_written == NONE, "Host Controls' 2 not shown: %d",
		spinner.parameters.list.selected_index);
	spinner.parameters.list.selected_index = 9;
	lobby_board_bots_choice(&spinner, TRUE);
	CHECK(spinner.parameters.list.selected_index == 2 && choice_written == NONE, "a locked BOTS written");
	spinner.parameters.list.selected_index = 9;
	choice_refused = TRUE;
	lobby_board_bots_choice(&spinner, FALSE);
	CHECK(spinner.parameters.list.selected_index == 2, "a choice refused kept");
	choice_refused = FALSE;
}

/* one team left alone: the game ends when the others quit, not once bots
have left it */
static void case_bots_left(void)
{
	player_count = 3;
	memset(players, 0, sizeof(players));
	players[2].bot = TRUE;
	teams_alive = TRUE;
	CHECK(!game_engine_should_end_game(), "a game of two teams ended");
	teams_alive = FALSE;
	CHECK(game_engine_should_end_game(), "a game whose players left did not end");
	players[2].quit_out_of_game = TRUE;
	CHECK(!game_engine_should_end_game(), "a game the bots left ended");
	variant_options.time_limit = 1;
	CHECK(!game_engine_should_end_game(), "a game ended before its time");
	variant_options.time_limit = 0;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("local") case_local();
	else CASE("spinner-ends") case_spinner_ends();
	else CASE("lobby-bots") case_lobby_bots();
	else CASE("bots-left") case_bots_left();
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	return 0;
}
