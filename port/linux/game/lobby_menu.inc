/*
LOBBY_MENU.INC

The lobby on the computer (tools/port_settings.py's _lobby_board), the
network game's and a game of this machine alone's (Create Game's OFFLINE,
split screen), in Multiplayer's look: the menus' shared parts, included in
menu_functions.c after party_menu.inc, whose helpers it uses. The
touchscreen's is the same by the same names, its panel's controls and the
footer's buttons a finger's size (tools/port_settings.py's
_touch_lobby_board).

It draws what this machine knows of the game, and sends nothing of its own
but what the Xbox's lobby sent (a player's team, the start): the game's
players (the host's record, which every machine has), each in its team's
column (one list, both columns', in a game without teams, and in Infection,
whose zombies are chosen when it starts), with a tag: HOST (the host's
machine: the host's own; for a player, the game's first machine, which the
host's own client is), BOT and the bots' skill (bots.c's machines), PARTY
(a member of this machine's party, by the name the party knows it by), OPENCE or OPENH1 (the
host knows each machine's join token: network_standard.c; a player knows
only its own). This machine's players are framed. In each column the host
first, then the party's members together, then the others as they joined,
the bots last. The details panel: the next game (its gametype, map and
limits; OPENH1 n TICK or LEGACY, whether OpenCE's players may join, which
the host alone knows), THEN (a network game's host: the game after it),
the host's bots in any game but co-op (BOTS: NONE, ADD n or FILL TO n, the
choices Host Controls' BOTS steps, network_host_controls.c; SKILL and
TEAMS, bots.skill and bots.teams: each written as it changes, which the
host's bots follow, dimmed for a gametype the bots do not play, and BOTS
for a matchmaking game's, its playlist's), how the game is, and the host's
GAME SETTINGS (Server Setup, in the lobby's place), HOST CONTROLS and START
GAME, or a player's WAITING FOR THE HOST.

Left and right (the arrow keys, the D-pad) put the pressing controller's
player on red's or blue's team, a click on a column on its team; in a game
without teams they do nothing. A or Start on the players (Enter) starts the
game, the host's. The pure parts (lobby_board_tag, lobby_board_lay_out,
lobby_board_teams, lobby_board_controls_for) are
tools/harness/tests/test_lobby_board.py's.
*/

/* (each column's rows and the columns, LOBBY_BOARD_ROWS and
LOBBY_BOARD_COLUMNS, are menu_functions.c's, for the voice chat's icons) */
#define LOBBY_BOARD_ENTRIES HALO_PORT_MAXIMUM_NETWORK_PLAYERS
/* between the columns (tools/port_settings.py's LOBBY_COLUMN_GAP) */
#define LOBBY_BOARD_COLUMN_GAP 12
/* milliseconds the panel says the invite link was copied */
#define LOBBY_BOARD_COPIED_TIME 4000
/* a row's tag's width a name leaves room for (tools/port_settings.py's
LOBBY_TAG_WIDTH: a bot's tag with its skill is wider, its name then cut
to the rest, else the tag BOT alone) and the space between them */
#define LOBBY_BOARD_TAG_WIDTH 58
#define LOBBY_BOARD_TAG_GAP 6

/* a row's tag */
enum
{
	_lobby_tag_none,
	_lobby_tag_host,
	_lobby_tag_bot,
	_lobby_tag_party,
	_lobby_tag_openh1,
	_lobby_tag_opence,
	_lobby_tag_split,
	NUMBER_OF_LOBBY_TAGS
};

/* what this machine knows of a player's machine */
enum
{
	_lobby_kind_unknown,
	_lobby_kind_openh1,
	_lobby_kind_opence,
};

/* what the lobby knows of a player, for its row */
struct lobby_board_entry
{
	/* its team (0 red, 1 blue) */
	short team;
	/* this machine's; the host's machine's; a bot; in this machine's party */
	boolean own;
	boolean host;
	boolean bot;
	boolean party;
	/* a machine's player past its first (split screen's: P2 to P4), the host's
	machine's too, whose first alone is the host */
	boolean split;
	/* _lobby_kind_* */
	short kind;
};

/* the rows: each column's entries (NONE: an empty row), how many players
each column has (a game without teams: all of them, column 0's), and how many
more than its rows show (the last row then says so: in a game without teams,
blue's column's) */
struct lobby_board_layout
{
	short rows[LOBBY_BOARD_COLUMNS][LOBBY_LAYOUT_ROWS];
	short counts[LOBBY_BOARD_COLUMNS];
	short more[LOBBY_BOARD_COLUMNS];
	/* (each column's rows laid out) */
	short row_count;
};

/* what the lobby offers this machine */
struct lobby_board_controls
{
	/* the host's START GAME and GAME SETTINGS; a player's WAITING FOR THE
	HOST */
	boolean start;
	boolean settings;
	boolean waiting;
	/* the bots' rows (the host's, but in co-op), locked (dimmed: a gametype
	the bots do not play) */
	boolean bots;
	boolean bots_locked;
	/* CHANGE TEAM, COPY INVITE, ADD PLAYER */
	boolean team_change;
	boolean invite;
	boolean add_player;
};

static wchar_t const *const lobby_tag_words[NUMBER_OF_LOBBY_TAGS] =
{
	L"", L"HOST", L"BOT", L"PARTY", L"GOAT H1", L"OPENCE", L"SPLIT SCREEN",
};

static struct
{
	/* the rows' players as last shown, by column and row (NULL: none): what
	a row's own action finds its player by (lobby_board_row_player) */
	struct network_player *players[LOBBY_BOARD_COLUMNS][LOBBY_BOARD_ROWS];
	struct lobby_board_entry entries[LOBBY_BOARD_COLUMNS][LOBBY_BOARD_ROWS];
	/* each row's player INVITE TO PARTY could ask (lobby_board_party_invitable) */
	boolean invitable[LOBBY_BOARD_COLUMNS][LOBBY_BOARD_ROWS];
	/* when COPY INVITE last copied it */
	unsigned long copied_time;
	/* the BOTS choice its spinner last showed ("port lobby bots load"): one
	the spinner leaves is the host's, written */
	short bots_shown;
} lobby_board;

/* the bots' rows (tools/port_settings.py's BOTS_ROWS), each a row and its
spinner: BOTS (the choices: lobby_board_bots_choice), SKILL and TEAMS
(bots.skill's and bots.teams' spinners) */
static char const *const lobby_board_bots_rows[] = { "board_bots_count", "board_bots_skill", "board_bots_teams" };

/* ---- the pure parts */

/* whether the game's players choose a team in the lobby: a team game's, but
Infection's (its zombies are chosen as it starts) */
static boolean lobby_board_teams(boolean teams, short mode)
{
	return teams && mode != _game_mode_infection;
}

/* a row's tag: what is most worth knowing of its player */
static short lobby_board_tag(struct lobby_board_entry const *entry)
{
	if (entry->bot)
		return _lobby_tag_bot;
	if (entry->host)
		return _lobby_tag_host;
	if (entry->split)
		return _lobby_tag_split;
	if (entry->kind == _lobby_kind_opence)
		return _lobby_tag_opence;
	if (entry->party)
		return _lobby_tag_party;
	if (entry->kind == _lobby_kind_openh1)
		return _lobby_tag_openh1;
	return _lobby_tag_none;
}

/* each machine's players past its first (by their controllers: split
screen's P2 to P4) marked, and none of them the host: the host is its
machine's first player alone, on the host's machine and on a player's */
static void lobby_board_mark_split(struct lobby_board_entry *entries, long const *machines, short const *controllers,
	short count)
{
	short index, other;

	for (index = 0; index < count; index++)
	{
		entries[index].split = FALSE;
		for (other = 0; other < count && !entries[index].bot; other++)
		{
			if (other != index && !entries[other].bot && machines[other] == machines[index] &&
				(controllers[other] < controllers[index] || (controllers[other] == controllers[index] && other < index)))
			{
				entries[index].split = TRUE;
			}
		}
		if (entries[index].split)
			entries[index].host = FALSE;
	}
}

/* a player's place in its column: the host first, the party's members
together, the others, the bots last */
static short lobby_board_rank(struct lobby_board_entry const *entry)
{
	if (entry->host && !entry->bot)
		return 0;
	if (entry->party && !entry->bot)
		return 1;
	return entry->bot ? 3 : 2;
}

/* the entries (in the game's order) into the columns' rows (rows of them
each, at most LOBBY_LAYOUT_ROWS: the generated screen's LOBBY_BOARD_ROWS,
the hand-written one's as many as its lanes have): a team game's by team,
else one list down red's column and on down blue's; a column with more
than its rows shows one fewer, and how many more in its last */
static void lobby_board_lay_out_rows(struct lobby_board_entry const *entries, short count, boolean teams, short rows,
	struct lobby_board_layout *layout)
{
	short order[LOBBY_BOARD_ENTRIES];
	short column, row, index, rank, shown, total = 0;

	rows = (short)MIN(MAX(rows, 1), LOBBY_LAYOUT_ROWS);
	layout->row_count = rows;
	for (column = 0; column < LOBBY_BOARD_COLUMNS; column++)
	{
		for (row = 0; row < LOBBY_LAYOUT_ROWS; row++)
			layout->rows[column][row] = NONE;
		layout->counts[column] = 0;
		layout->more[column] = 0;
	}
	count = (short)MIN(MAX(count, 0), LOBBY_BOARD_ENTRIES);
	/* (by rank, each rank in the game's order) */
	for (rank = 0; rank < 4; rank++)
	{
		for (index = 0; index < count; index++)
		{
			if (lobby_board_rank(&entries[index]) == rank)
				order[total++] = index;
		}
	}
	if (!teams)
	{
		shown = (short)(total <= LOBBY_BOARD_COLUMNS * rows ? total : LOBBY_BOARD_COLUMNS * rows - 1);
		for (index = 0; index < shown; index++)
			layout->rows[index / rows][index % rows] = order[index];
		layout->counts[0] = total;
		layout->more[LOBBY_BOARD_COLUMNS - 1] = (short)(total - shown);
		return;
	}
	for (column = 0; column < LOBBY_BOARD_COLUMNS; column++)
	{
		short in_column = 0;

		for (index = 0; index < total; index++)
		{
			/* (a team past blue's, which no game has, in red's) */
			short team = entries[order[index]].team == 1 ? 1 : 0;

			if (team != column)
				continue;
			if (in_column < rows)
				layout->rows[column][in_column] = order[index];
			in_column++;
		}
		layout->counts[column] = in_column;
		if (in_column > rows)
		{
			layout->rows[column][rows - 1] = NONE;
			layout->more[column] = (short)(in_column - rows + 1);
		}
	}
}

/* ... in the generated screen's rows */
static void lobby_board_lay_out(struct lobby_board_entry const *entries, short count, boolean teams,
	struct lobby_board_layout *layout)
{
	lobby_board_lay_out_rows(entries, count, teams, LOBBY_BOARD_ROWS, layout);
}

/* what the lobby offers: the host's start and settings (a player waits for
the host), the host's bots in any game but co-op (locked for a gametype
they do not play), a team change while this machine has a player in a game whose
players choose a team, the invite link while there is one, ADD PLAYER while
this machine has room for another */
static void lobby_board_controls_for(boolean host, boolean cooperative, boolean bots_play, boolean teams,
	boolean own_player, boolean invite, boolean room, struct lobby_board_controls *controls)
{
	controls->start = host;
	controls->settings = host;
	controls->waiting = !host;
	controls->bots = host && !cooperative;
	controls->bots_locked = controls->bots && !bots_play;
	controls->team_change = teams && own_player;
	controls->invite = invite;
	controls->add_player = room;
}

/* ---- what this machine knows */

/* a player's name as a party knows it (party_name_tell: its characters
past ASCII as "?") */
static boolean lobby_board_party_name(wchar_t const *name, short length, char const *member)
{
	short index;

	for (index = 0; index < length && name[index]; index++)
	{
		wchar_t character = name[index];

		if (member[index] != (char)(character >= 0x20 && character < 0x7F ? character : '?'))
			return FALSE;
	}
	return index > 0 && member[index] == 0;
}

/* whether a player is a member of this machine's party (one of more than
this machine) */
static boolean lobby_board_in_party(struct p2p_party_status const *status, struct network_player const *player)
{
	short index;

	if (status->state != _p2p_party_member || status->count < 2)
		return FALSE;
	for (index = 0; index < status->count; index++)
	{
		if (lobby_board_party_name(player->name, NUMBEROF(player->name), status->members[index].name))
			return TRUE;
	}
	return FALSE;
}

/* the game's invite link (the host's own, or a player's of the game it
joined), 0 if none: a LAN game, a game of this machine alone */
static int lobby_board_invite(char *link, int size)
{
	void *client = global_network_game_client_get();
	struct transport_address address;

	if (global_network_game_server_get())
		return network_game_is_splitscreen_local() ? 0 : p2p_invite_link(link, size);
	if (!client)
		return 0;
	csmemset(&address, 0, sizeof(address));
	network_game_client_get_remote_server_address(client, &address);
	return address.address.long_words[0] &&
		p2p_game_invite(party_network_order(address.address.long_words[0]), link, size);
}

/* whether a name (at most length characters) is another */
static boolean lobby_board_names_equal(wchar_t const *name, short length, wchar_t const *other)
{
	short index;

	for (index = 0; index < length && name[index]; index++)
	{
		if (name[index] != other[index])
			return FALSE;
	}
	return other[index] == 0;
}

/* a network game's name as the lobby says it: the host's own, or, while it
is the name the game was given (none; a name the server took from the map's
random players' names, ui\random_player_names, as its machine's nickname; or
the host's player's own, Server Setup's first), "<the host's player>'s game" */
static void lobby_board_game_name(struct network_game const *game, struct lobby_board_entry const *entries,
	struct network_player *const *players, short count, wchar_t *name, short size)
{
	long names = tag_loaded('ustr', "ui\\random_player_names");
	struct string_list *list = names != NONE ? unicode_string_list_definition_get(names) : NULL;
	struct network_player const *host = NULL;
	wchar_t host_name[NUMBEROF(host->name) + 1];
	boolean given = !game->name[0];
	short index;

	for (index = 0; index < count && !host; index++)
	{
		if (entries[index].host && !entries[index].bot)
			host = players[index];
	}
	for (index = 0; list && !given && index < list->strings.count; index++)
	{
		wchar_t const *random_name = unicode_string_list_get_string(names, index);

		given = random_name && lobby_board_names_equal(game->name, NUMBEROF(game->name), random_name);
	}
	if (host)
	{
		ustrncpy(host_name, host->name, NUMBEROF(host->name));
		host_name[NUMBEROF(host->name)] = 0;
		given |= lobby_board_names_equal(game->name, NUMBEROF(game->name), host_name);
	}
	if (host && host_name[0] && given)
		usnprintf(name, size - 1, L"%s's game", host_name);
	else
	{
		ustrncpy(name, game->name, MIN(size, (short)NUMBEROF(game->name) + 1) - 1);
		name[MIN(size, (short)NUMBEROF(game->name) + 1) - 1] = 0;
	}
	name[size - 1] = 0;
}

/* the game's map's name: a multiplayer map's (the Xbox's, a Custom Edition
map's), a co-op game's level's, else its file's */
static void lobby_board_map_text(char const *map_name, wchar_t *text, short size)
{
	char const *const *names;
	short count = xbox_multiplayer_map_count(ui_widget_port_multiplayer_maps(&names, NULL)), map = NONE, index;
	short level = campaign_level_of(map_name);
	char const *file = map_name;
	wchar_t name[ROW_TEXT_LENGTH];

	for (index = 0; index < count; index++)
	{
		if (!_stricmp(names[index], map_name))
			map = index;
	}
	if (custom_edition_level_name(map_name) && custom_edition_maps_display_index(map_name) != NONE)
		map = custom_edition_maps_display_index(map_name);
	name[0] = 0;
	if (level != NONE)
		map_level_text(level, name);
	else if (map != NONE)
		string_get("pc\\main_menu\\mp_map_list", map, name);
	if (!name[0])
	{
		for (index = 0; map_name[index]; index++)
		{
			if (map_name[index] == '\\')
				file = map_name + index + 1;
		}
		for (index = 0; index < ROW_TEXT_LENGTH - 1 && file[index]; index++)
			name[index] = (wchar_t)(unsigned char)file[index];
		name[index] = 0;
	}
	ustrncpy(text, name, size - 1);
	text[size - 1] = 0;
}

/* the next game's limits: what wins (by its game engine's score), its time */
static void lobby_board_limits_text(struct network_game const *game, wchar_t *text, short size)
{
	struct game_variant const *variant = &game->variant;
	long score = variant->universal_variant.score_to_win;
	short minutes = game->variant_options.time_limit;
	size_t length;

	switch (game_modes_variant_mode(variant, &game->variant_options))
	{
	case _game_mode_infection:
		usnprintf(text, size - 1, L"Zombies chosen at the start");
		break;
	case _game_mode_gun_game:
		usnprintf(text, size - 1, L"A kill with each weapon");
		break;
	default:
		switch (variant->game_engine_index)
		{
		case 1:
			usnprintf(text, size - 1, L"%ld captures", score);
			break;
		case 2:
			usnprintf(text, size - 1, L"%ld kills", score);
			break;
		case 5:
			usnprintf(text, size - 1, L"%ld laps", score);
			break;
		default:
			usnprintf(text, size - 1, variant->game_engine_index ? L"Score to win %ld" : L"", score);
			break;
		}
		break;
	}
	text[size - 1] = 0;
	length = ustrlen(text);
	if (minutes > 0)
		usnprintf(text + length, size - 1 - length, L"%s%d min", length ? L" \x00B7 " : L"", minutes);
	text[size - 1] = 0;
}

/* this machine's player of the controller, NULL if none */
static struct network_player *lobby_board_own_player(short controller)
{
	return lobby_local_player(controller >= 0 && controller < MAXIMUM_LOCAL_PLAYERS ? controller : 0);
}

/* the row's player as last shown (column, row), NULL if none: the hook for
a row's own action. A player another machine has, not a bot and not in this
machine's party, is one INVITE TO PARTY could ask (lobby_board_party_invitable;
the scoreboard's invites, network_party.c's network_party_player_actions,
take a player of the game being played, which the lobby has not yet: the
row's network player's machine is what they would ask) */
static struct network_player *lobby_board_row_player(short column, short row)
{
	if (column < 0 || column >= LOBBY_BOARD_COLUMNS || row < 0 || row >= LOBBY_BOARD_ROWS)
		return NULL;
	return lobby_board.players[column][row];
}

static boolean lobby_board_party_invitable(short column, short row)
{
	struct lobby_board_entry const *entry;

	if (!lobby_board_row_player(column, row))
		return FALSE;
	entry = &lobby_board.entries[column][row];
	return !entry->own && !entry->bot && !entry->party;
}

/* ---- the screen */

/* a row's parts by their names (board_<part>_<column>_<row>) */
static struct widget_instance *lobby_board_part(struct widget_instance *screen, char const *part, short column,
	short row)
{
	char name[32];

	snprintf(name, sizeof(name), "board_%s_%d_%d", part, column, row);
	return named(screen, name, 0);
}

/* a bot's row's tag: BOT and the bots' skill, if its name still fits beside
it, else BOT alone (the name box fitted to what is left) */
static void lobby_board_bot_tag(struct widget_instance *tag, struct widget_instance *name_box)
{
	static wchar_t const *const skills[] = { L"EASY", L"NORMAL", L"HEROIC", L"LEGENDARY" };
	wchar_t text[ROW_TEXT_LENGTH], name[ROW_TEXT_LENGTH];
	short name_width, tag_width, room;
	long font;

	if (!tag || !name_box || !name_box->parameters.text_box.text)
		return;
	ustrncpy(name, name_box->parameters.text_box.text, NUMBEROF(name) - 1);
	name[NUMBEROF(name) - 1] = 0;
	font = ui_widget_port_text_box_font(name_box, &name_width);
	usnprintf(text, NUMBEROF(text) - 1, L"BOT \x00B7 %s", skills[PIN(bots_skill(), 0, (short)NUMBEROF(skills) - 1)]);
	text[NUMBEROF(text) - 1] = 0;
	tag_width = menu_text_box_fit(tag, text, 0);
	room = (short)(name_width + LOBBY_BOARD_TAG_WIDTH - tag_width - LOBBY_BOARD_TAG_GAP);
	if (font == NONE || !tag_width || settings_text_width(font, name) > room)
		text_set(tag, lobby_tag_words[_lobby_tag_bot]);
}

/* BOTS' spinner (its strings the choices, network_host_controls.c's, in
order): the choice the settings make shown, or one the host stepped to
written (as Host Controls' BOTS writes it), unless locked */
static void lobby_board_bots_choice(struct widget_instance *spinner, boolean locked)
{
	short chosen = network_host_controls_bots_chosen();
	short shown = spinner->parameters.list.selected_index;
	long number;
	short kind;

	if (!locked && lobby_board.bots_shown != NONE && shown != lobby_board.bots_shown)
	{
		kind = network_host_controls_bot_choice(shown, &number);
		if (network_host_controls_choose_bots(kind, number))
		{
			lobby_board.bots_shown = shown;
			return;
		}
	}
	spinner->parameters.list.selected_index = chosen;
	lobby_board.bots_shown = chosen;
}

/* "port lobby bots load" (BOTS' spinner made): the choice the settings make */
static boolean lobby_board_bots_load(struct widget_instance *spinner)
{
	/* (one of the spinner's items, each made from the spinner's own
	definition and so given its created event too: ui_widget.c's list
	items, before the spinner's own) */
	if (spinner->parameters.list.number_of_items == 0)
		return TRUE;
	if (spinner->parameters.list.number_of_items != network_host_controls_bot_choice_count())
		platform_log("menus: the lobby's BOTS has %d choices, not %d", (int)spinner->parameters.list.number_of_items,
			(int)network_host_controls_bot_choice_count());
	lobby_board.bots_shown = NONE;
	lobby_board_bots_choice(spinner, TRUE);
	return TRUE;
}

/* what the lobby shows, worked out from what this machine knows: the
generated screen's widgets (lobby_board_update) and the hand-written
screen's table and bindings (screens/lobby.xml: pc_menu_lobby_source,
pc_menu_lobby_value) are its two views */
struct lobby_board_model
{
	void *client;
	struct network_game *game;
	short state;
	boolean host, local, dedicated, teams, own_player;
	short machine, count, mode, seconds;
	struct lobby_board_entry entries[LOBBY_BOARD_ENTRIES];
	struct network_player *players[LOBBY_BOARD_ENTRIES];
	struct lobby_board_layout layout;
	struct lobby_board_controls controls;
	/* (the next game's: an OpenH1 game's or Legacy's, its rate) */
	boolean standard;
	long rate;
	/* (the host's: whether OpenCE's players may join; THEN's game after this
	one, and HOST CONTROLS, the host's or a dedicated server's admin's) */
	boolean opence, then, host_controls;
	wchar_t subtitle[SETTINGS_PANEL_TEXT_LENGTH];
	/* (the hand-written screen's band: the subtitle's words, shorter, then
	ever shorter forms, each on a line of its own, the first that fits
	shown: the bots, where it is, the players' word dropped in turn) */
	wchar_t band[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t gametype[ROW_TEXT_LENGTH];
	wchar_t map[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t limits[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t after[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t status[SETTINGS_PANEL_TEXT_LENGTH];
	/* (START GAME's words, or the countdown; a player's waiting's) */
	wchar_t start[ROW_TEXT_LENGTH];
	wchar_t waiting[ROW_TEXT_LENGTH];
};

/* the model made now, its columns of rows rows (lobby_board_lay_out_rows);
and what the lobby does each frame its screen is shown, whichever draws it:
a dedicated server's map vote's screen (map_vote_lobby_check), the players
counted (lobby_player_count) */
static void lobby_board_model_make(struct lobby_board_model *model, short rows)
{
	void *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short state_data, state = client ? network_game_client_get_state(client, &state_data) : NONE;
	boolean host = global_network_game_server_get() != NULL;
	struct p2p_party_status party;
	char link[TEXT_FIELD_LENGTH];
	short count = 0, index;
	wchar_t *text;

	model->client = client;
	model->state = state;
	model->host = host;
	model->local = host && network_game_is_splitscreen_local();
	/* (a dedicated server's game: no host's player, its own rules start it) */
	model->dedicated = !host && lobby_joined_dedicated;
	model->machine = network_game_client_get_local_machine_index();
	model->mode = _game_mode_none;
	model->seconds = NONE;
	model->teams = FALSE;
	model->own_player = FALSE;
	if (!game || state < _client_state_pregame)
		game = NULL;
	model->game = game;
	/* (a dedicated server's map vote begun: its screen, map_vote_lobby_check) */
	if (game)
		map_vote_lobby_check();
	p2p_party_status(&party);
	for (index = 0; game && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player *player = &game->players[index];
		struct lobby_board_entry *entry = &model->entries[count];
		long player_machine = player->machine_index;

		if (!network_player_is_valid(player))
			continue;
		model->players[count++] = player;
		entry->team = player->team_index;
		entry->bot = bots_machine_is_bot(player_machine);
		entry->own = !entry->bot && player_machine == model->machine;
		/* (a player knows the host's machine as the game's first: the
		host's own client joins it first) */
		entry->host = !entry->bot && player_machine == (host ? model->machine : 0);
		entry->party = !entry->bot && lobby_board_in_party(&party, player);
		entry->kind = _lobby_kind_unknown;
		if (host && network_standard_machine_is_opence(player_machine))
			entry->kind = _lobby_kind_opence;
		else if ((host && network_standard_machine_is_openh1(player_machine)) || entry->own)
			entry->kind = _lobby_kind_openh1;
		model->own_player |= entry->own;
	}
	{
		long machines[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
		short controllers[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];

		for (index = 0; index < count; index++)
		{
			machines[index] = model->players[index]->machine_index;
			controllers[index] = (short)model->players[index]->controller_index;
		}
		lobby_board_mark_split(model->entries, machines, controllers, count);
	}
	model->count = count;
	lobby_player_count = count;
	if (game)
	{
		model->mode = game_modes_variant_mode(&game->variant, &game->variant_options);
		model->teams = lobby_board_teams(game->variant.universal_variant.teams, model->mode);
		model->seconds = network_game_client_get_seconds_to_game_start(client);
	}
	lobby_board_lay_out_rows(model->entries, count, model->teams, rows, &model->layout);
	lobby_board_controls_for(host, game && !game->variant.game_engine_index,
		game && bots_play_game_engine(game->variant.game_engine_index), model->teams,
		model->own_player, lobby_board_invite(link, sizeof(link)) > 0, lobby_local_player_count() < MAXIMUM_LOCAL_PLAYERS,
		&model->controls);

	/* (the game's name, where it is, its players: a game of this machine
	alone's none, as no one else sees it) */
	text = model->subtitle;
	text[0] = 0;
	if (game && model->local)
	{
		boolean bots = FALSE;

		for (index = 0; index < count; index++)
			bots |= model->entries[index].bot;
		usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, count == 1 ? L"%s \x00B7 1 player" : L"%s \x00B7 %d players",
			bots ? L"Offline with bots" : L"Offline", count);
		usnprintf(model->band, SETTINGS_PANEL_TEXT_LENGTH - 1, count == 1 ? L"%s\n1 player" : L"%s\n%d players",
			text, count);
	}
	else if (game)
	{
		wchar_t name[ROW_TEXT_LENGTH];
		short bots = 0;
		wchar_t const *where = model->dedicated ? L"Dedicated server" : model->controls.invite ? L"Internet" :
			L"Local network";
		wchar_t bot_words[16];

		/* (and the host's bots among them, bots.c's) */
		for (index = 0; index < count; index++)
			bots += model->entries[index].bot ? 1 : 0;
		lobby_board_game_name(game, model->entries, model->players, count, name, NUMBEROF(name));
		usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"%s \x00B7 %s \x00B7 %d of %d players", name, where, count,
			game->maximum_players);
		bot_words[0] = 0;
		if (bots)
			usnprintf(bot_words, NUMBEROF(bot_words) - 1, bots == 1 ? L" \x00B7 1 bot" : L" \x00B7 %d bots", bots);
		bot_words[NUMBEROF(bot_words) - 1] = 0;
		if (bots)
		{
			size_t length = ustrlen(text);

			usnprintf(text + length, SETTINGS_PANEL_TEXT_LENGTH - 1 - length, L"%s", bot_words);
		}
		/* (the band's: the least needed dropped first, the bots (the roster
		shows them), where it is, the players' word, the game's name) */
		usnprintf(model->band, SETTINGS_PANEL_TEXT_LENGTH - 1,
			L"%s \x00B7 %s \x00B7 %d/%d players%s\n%s \x00B7 %s \x00B7 %d/%d players\n%s \x00B7 %d/%d players\n"
			L"%s \x00B7 %d/%d\n%d/%d players\n%d/%d",
			name, where, count, game->maximum_players, bot_words, name, where, count, game->maximum_players, name,
			count, game->maximum_players, name, count, game->maximum_players, count, game->maximum_players, count,
			game->maximum_players);
	}
	text[SETTINGS_PANEL_TEXT_LENGTH - 1] = 0;
	if (!game)
		model->band[0] = 0;
	model->band[SETTINGS_PANEL_TEXT_LENGTH - 1] = 0;

	/* (the next game) */
	model->gametype[0] = model->map[0] = model->limits[0] = model->status[0] = model->start[0] = 0;
	model->waiting[0] = model->after[0] = 0;
	model->standard = FALSE;
	model->rate = 0;
	model->opence = FALSE;
	if (game)
	{
		wchar_t const *kind = game_modes_variant_kind_name(&game->variant, &game->variant_options);

		model->standard = host ? network_standard_hosting() : network_standard_agreed_rate() > 0;
		model->rate = network_standard_agreed_rate();
		ustrncpy(model->gametype, game->variant.human_readable_game_description,
			MIN(NUMBEROF(model->gametype), NUMBEROF(game->variant.human_readable_game_description) + 1) - 1);
		model->gametype[MIN(NUMBEROF(model->gametype), NUMBEROF(game->variant.human_readable_game_description) + 1) - 1] =
			0;
		if (!model->gametype[0])
			ustrncpy(model->gametype, kind ? kind : game->variant.game_engine_index ?
				engine_names[PIN(game->variant.game_engine_index, 0, 5)] : L"CO-OP", NUMBEROF(model->gametype) - 1);
		model->gametype[NUMBEROF(model->gametype) - 1] = 0;
		lobby_board_map_text(game->map.name, model->map, NUMBEROF(model->map));
		/* (a co-op game's difficulty after its level) */
		if (!game->variant.game_engine_index && campaign_level_of(game->map.name) != NONE)
		{
			wchar_t difficulty[ROW_TEXT_LENGTH];
			size_t length = ustrlen(model->map);

			map_difficulty_text(game->difficulty, difficulty);
			usnprintf(model->map + length, NUMBEROF(model->map) - 1 - length, L" \x00B7 %s", difficulty);
			model->map[NUMBEROF(model->map) - 1] = 0;
		}
		lobby_board_limits_text(game, model->limits, NUMBEROF(model->limits));
		model->opence = host && !model->local && network_standard_hosting_mixed();

		/* (how the game is: the countdown, the link just copied, how
		another player of this machine joins, Infection's teams, waiting
		for other machines) */
		text = model->status;
		if (model->seconds > 0)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"The game starts in %d.", model->seconds);
		else if (model->seconds == 0)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"The game is starting.");
		else if (lobby_board.copied_time && system_milliseconds() - lobby_board.copied_time < LOBBY_BOARD_COPIED_TIME)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"Invite copied: paste it in a chat to your friends.");
		else if (model->controls.bots_locked)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"Bots play every gametype but Race.");
		else if (lobby_join.adding)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"New player: press START.");
		else if (model->mode == _game_mode_infection)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"Infection: the zombies are chosen when the game starts.");
		else if (host && !model->local && game->machine_count < 2)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"Waiting for players to join.");
		else if (model->dedicated)
			usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"A dedicated server: it starts once enough players join.");
		else if (lobby_local_player_count() < MAXIMUM_LOCAL_PLAYERS)
		{
			short controller;

			for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
			{
				if (input_has_gamepad(controller) && !lobby_controller_playing(controller))
					usnprintf(text, SETTINGS_PANEL_TEXT_LENGTH - 1, L"Another controller: START joins.");
			}
		}
		text[SETTINGS_PANEL_TEXT_LENGTH - 1] = 0;
		/* (START GAME's words, the countdown's; a player's waiting) */
		if (model->seconds > 0)
			usnprintf(model->start, NUMBEROF(model->start) - 1, model->controls.start ? L"STARTING IN %d" :
				L"Starting in %d", model->seconds);
		else if (model->seconds == 0)
			usnprintf(model->start, NUMBEROF(model->start) - 1, model->controls.start ? L"STARTING..." : L"Starting...");
		else
			usnprintf(model->start, NUMBEROF(model->start) - 1, model->dedicated ? L"Waiting for players" :
				!model->controls.start ? L"Waiting for the host" : L"START GAME");
		model->start[NUMBEROF(model->start) - 1] = 0;
		if (model->controls.waiting)
			ustrncpy(model->waiting, model->start, NUMBEROF(model->waiting) - 1);
	}
	/* (a network game's host: the game after this one, its playlist's, and
	HOST CONTROLS: network_host_game.c; a dedicated server's admin's too:
	network_admin.c) */
	{
		struct server_entry after;
		boolean now;
		boolean hosting = game && host && !model->local && network_host_controls_available();

		model->then = hosting && network_host_game_next(&after, &now) && !now;
		if (model->then)
			network_host_game_entry_shown(&after, model->after, NUMBEROF(model->after));
		model->host_controls = hosting || (game && !host && network_admin_remote());
	}
}

/* "port lobby board update" (the list's) */
static void lobby_board_update(struct widget_instance *list)
{
	static struct lobby_board_model model;
	struct lobby_board_entry const *entries = model.entries;
	struct network_player *const *players = model.players;
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *panel = named(screen, "board_panel", 0);
	struct widget_instance *bar = named(list, "board_prompts", 0);
	struct network_game *game;
	boolean players_focused = list->focused_child && !strcmp(list->focused_child->name, "board_players");
	struct lobby_board_layout layout;
	struct lobby_board_controls controls;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	short count, column, row, index;
	boolean teams, host, local;

	lobby_board_model_make(&model, LOBBY_BOARD_ROWS);
	game = model.game;
	count = model.count;
	teams = model.teams;
	host = model.host;
	local = model.local;
	layout = model.layout;
	controls = model.controls;
	(void)local;
	menu_text_box_fit(named(screen, "board_subtitle", 0), model.subtitle, 0);

	/* (the teams' columns, or one list) */
	for (column = 0; column < LOBBY_BOARD_COLUMNS; column++)
	{
		char name[24];
		struct widget_instance *count_box;

		snprintf(name, sizeof(name), "board_team_%d", column);
		visible_set(named(screen, name, 0), teams);
		/* (its edge its team's: part_team_panel's frames) */
		if (named(screen, name, 0))
			named(screen, name, 0)->animation.current_frame_index = column;
		snprintf(name, sizeof(name), "board_title_%d", column);
		visible_set(named(screen, name, 0), teams);
		snprintf(name, sizeof(name), "board_count_%d", column);
		count_box = named(screen, name, 0);
		if (teams)
			usnprintf(text, NUMBEROF(text) - 1, layout.counts[column] == 1 ? L"1 PLAYER" : L"%d PLAYERS",
				layout.counts[column]);
		/* (an offline game's: its players alone, as its band says; the
		server's most is the bots' room, no limit of its people) */
		else if (game && global_network_game_server_get() && network_game_is_splitscreen_local())
			usnprintf(text, NUMBEROF(text) - 1, count == 1 ? L"1 PLAYER" : L"%d PLAYERS", count);
		else
			usnprintf(text, NUMBEROF(text) - 1, game ? L"%d OF %d" : L"", count, game ? game->maximum_players : 0);
		text[NUMBEROF(text) - 1] = 0;
		/* (a game without teams': at its panel's right, blue's place) */
		visible_set(count_box, teams || column == LOBBY_BOARD_COLUMNS - 1);
		text_set(count_box, text);
	}
	visible_set(named(screen, "board_all", 0), !teams);
	visible_set(named(screen, "board_title_all", 0), !teams);

	/* (each row: its player's dot, name and tag, this machine's framed,
	brighter while the players have the focus; or how many more) */
	lobby_icons.count = 0;
	for (column = 0; column < LOBBY_BOARD_COLUMNS; column++)
	{
		for (row = 0; row < LOBBY_BOARD_ROWS; row++)
		{
			short shown = layout.rows[column][row];
			boolean more = shown == NONE && row == LOBBY_BOARD_ROWS - 1 && layout.more[column] > 0;
			struct widget_instance *row_box = lobby_board_part(screen, "row", column, row);
			struct widget_instance *name_box = lobby_board_part(screen, "name", column, row);
			struct widget_instance *dot = lobby_board_part(screen, "dot", column, row);
			struct widget_instance *tag = lobby_board_part(screen, "tag", column, row);
			struct lobby_board_entry const *entry = shown != NONE ? &entries[shown] : NULL;

			lobby_board.players[column][row] = entry ? players[shown] : NULL;
			if (entry)
				lobby_board.entries[column][row] = *entry;
			lobby_board.invitable[column][row] = lobby_board_party_invitable(column, row);
			visible_set(row_box, entry || more);
			visible_set(dot, entry != NULL);
			visible_set(tag, entry != NULL);
			if (row_box)
				row_box->animation.current_frame_index = (short)(entry && entry->own ? (players_focused ? 2 : 1) : 0);
			if (more)
			{
				usnprintf(text, NUMBEROF(text) - 1, L"+%d more", layout.more[column]);
				text[NUMBEROF(text) - 1] = 0;
				menu_text_box_fit(name_box, text, 0);
			}
			if (!entry)
				continue;
			if (dot)
				dot->animation.current_frame_index = (short)(teams ? (entry->team == 1 ? 1 : 0) : 2);
			{
				struct network_player const *player = players[shown];
				wchar_t name[NUMBEROF(player->name) + 1];

				ustrncpy(name, player->name, NUMBEROF(player->name));
				name[NUMBEROF(player->name)] = 0;
				/* (this machine's players, when it has more than one: their
				controllers) */
				if (entry->own && lobby_local_player_count() >= 2)
					usnprintf(text, NUMBEROF(text) - 1, L"%s (P%d)", name, player->controller_index + 1);
				else
					usnprintf(text, NUMBEROF(text) - 1, L"%s", name);
				text[NUMBEROF(text) - 1] = 0;
				menu_text_box_fit(name_box, text, 0);
			}
			text_set(tag, lobby_tag_words[lobby_board_tag(entry)]);
			/* (a bot's with its skill, as wide as its name leaves room for:
			lobby_board_bot_tag) */
			if (entry->bot)
				lobby_board_bot_tag(tag, name_box);
			/* (its speaker, in voice chat: menu_functions_text_box_drawn) */
			if (lobby_icons.count < LOBBY_ICON_ROWS)
			{
				lobby_icons.texts[lobby_icons.count] = name_box;
				lobby_icons.machines[lobby_icons.count] = players[shown]->machine_index;
				lobby_icons.count++;
			}
		}
	}
	lobby_icons.updated_at = system_milliseconds();

	/* (the details panel: the next game) */
	visible_set(panel, game != NULL);
	if (game)
	{
		struct widget_instance *pill;

		menu_text_box_fit(named(panel, "board_gametype", 0), model.gametype, 0);
		menu_text_box_fit(named(panel, "board_map", 0), model.map, 0);
		menu_text_box_fit(named(panel, "board_limits", 0), model.limits, 0);
		if ((pill = named(panel, "board_kind", 0)) != NULL)
		{
			pill->animation.current_frame_index = model.standard ? 0 : 1;
			text_set(pill, model.standard ? L"GOAT H1" : L"LEGACY");
		}
		if ((pill = named(panel, "board_tick", 0)) != NULL)
		{
			pill->animation.current_frame_index = 2;
			usnprintf(text, NUMBEROF(text) - 1, L"%ld TICK", model.standard && model.rate > 0 ? model.rate : 30L);
			text[NUMBEROF(text) - 1] = 0;
			text_set(pill, text);
		}
		if ((pill = named(panel, "board_opence", 0)) != NULL)
		{
			pill->animation.current_frame_index = 2;
			text_set(pill, L"OPENCE CAN JOIN");
		}
		visible_set(named(panel, "board_opence", 0), model.opence);
		menu_text_box_wrap(named(panel, "board_status", 0), model.status, 2);
		/* (START GAME's words with its key's picture, but on a touchscreen) */
		if (model.seconds < 0 && controls.start && !ui_widget_port_touch_menus())
			usnprintf(text, NUMBEROF(text) - 1, L"%%a-button %s", model.start);
		else
			usnprintf(text, NUMBEROF(text) - 1, L"%s", model.start);
		text[NUMBEROF(text) - 1] = 0;
		text_set(named(list, "board_start", 0), text);
		text_set(named(panel, "board_waiting", 0), text);
	}

	/* (the host's controls, a game of this machine alone's bots, the
	prompts that apply) */
	visible_set(named(list, "board_start", 0), game && controls.start);
	visible_set(named(list, "board_settings", 0), game && controls.settings);
	visible_set(named(panel, "board_waiting", 0), controls.waiting);
	for (index = 0; index < NUMBEROF(lobby_board_bots_rows); index++)
	{
		char name[32];
		struct widget_instance *bots_row, *spinner;
		struct pc_menu_setting *setting;
		/* (BOTS a matchmaking game's too, its playlist's) */
		boolean locked = controls.bots_locked || (index == 0 && bots_matchmaking_on());

		bots_row = named(list, lobby_board_bots_rows[index], 0);
		snprintf(name, sizeof(name), "%s_spinner", lobby_board_bots_rows[index]);
		spinner = named(list, name, 0);
		setting = spinner ? pc_menu_setting_get(spinner->definition_tag_index) : NULL;
		if (!bots_row || !spinner)
			continue;
		visible_set(bots_row, controls.bots);
		bots_row->disabled = locked;
		spinner->disabled = locked;
		bots_row->alpha_modifier = locked ? 0.4f : 1.0f;
		/* (the focus on a row locked goes to the players) */
		if (locked && list->focused_child == bots_row)
		{
			list->focused_child = list->child;
			list->parameters.list.selected_index = 0;
		}
		/* (BOTS' choice written as it changes; the others written to
		config.toml as they change: the host's bots follow them.
		setting_changed_save passes over a locked spinner) */
		if (index == 0)
			lobby_board_bots_choice(spinner, !controls.bots || locked);
		else if (controls.bots && setting)
			setting_changed_save(spinner, setting);
	}
	/* (a network game's host: the game after this one, its playlist's, and
	HOST CONTROLS: network_host_game.c; a dedicated server's admin's too) */
	if (model.then)
		menu_text_box_fit(named(panel, "board_after", 0), model.after, 0);
	visible_set(named(panel, "board_after", 0), model.then);
	visible_set(named(panel, "board_after_label", 0), model.then);
	visible_set(named(list, "board_host", 0), model.host_controls);
	menu_prompts_show(bar, "prompt_team", controls.team_change);
	menu_prompts_show(bar, "prompt_invite", controls.invite);
	menu_prompts_show(bar, "prompt_add", controls.add_player);
	/* (the focus off what is hidden: on the players) */
	focus_off_hidden(list);
}

/* "port lobby team red", "blue" (left and right on the players), "toggle"
(CHANGE TEAM's prompt) and "click" (a click on a column; A or Enter
without one starts the game, the host's): the controller's player on that
team, in a game whose players choose one */
static boolean lobby_board_team(struct widget_instance *widget, short controller, short team)
{
	void *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	struct network_player *own = lobby_board_own_player(controller);
	struct network_player changed;

	if (!game || !own || !lobby_board_teams(game->variant.universal_variant.teams,
		game_modes_variant_mode(&game->variant, &game->variant_options)))
	{
		return FALSE;
	}
	if (team == NONE)
		team = own->team_index == 1 ? 0 : 1;
	if (own->team_index == team)
		return TRUE;
	changed = *own;
	changed.team_index = (char)team;
	if (!network_game_client_update_local_player_data(client, &changed))
		return campaign_fail();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port lobby start" (START GAME, A or Start on the players, Start on the
panel's controls): the host's game starts, at once (the host's own faster:
network_game_server_game_start_request_event) once every machine has the
map; by one of this machine's players alone (FALSE for another controller,
which its START joins: "port lobby join", pc_menu_lobby_start) */
static boolean lobby_board_start(short controller)
{
	void *client = global_network_game_client_get();

	if (!global_network_game_server_get() || !client || !lobby_local_player_count() ||
		!lobby_controller_playing(controller))
		return FALSE;
	if (!network_game_client_request_start_time_change(client, TRUE))
		return campaign_fail();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

static boolean lobby_board_click(struct widget_instance *widget, short controller)
{
	struct widget_instance *blue = named(screen_of(widget), "board_team_1", 0);
	short x, y;

	if (!ui_widget_port_take_click(&x, &y))
		return lobby_board_start(controller);
	if (!blue || !blue->visible)
		return TRUE;
	return lobby_board_team(widget, controller,
		(short)(x >= widget_screen_x(blue) - LOBBY_BOARD_COLUMN_GAP / 2 ? 1 : 0));
}

/* "port lobby invite" (Y, COPY INVITE's prompt): a message with the
party's link (in a party: a friend joins it, then follows its leader into
the game), else the game's, copied (a line with the join page's address of
it, which chat apps make clickable, and one with the link:
p2p_invite_message), said in the panel */
static boolean lobby_board_invite_copy(void)
{
	char link[TEXT_FIELD_LENGTH];
	char message[P2P_INVITE_MESSAGE_SIZE];
	struct p2p_party_status party;

	p2p_party_status(&party);
	if (party.state != _p2p_party_none)
		snprintf(link, sizeof(link), "%s", party.link);
	else if (lobby_board_invite(link, sizeof(link)) <= 0)
		return FALSE;
	if (!p2p_invite_message(link, message, sizeof(message)))
		return FALSE;
	platform_clipboard_set(message);
	lobby_board.copied_time = system_milliseconds() | 1;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port lobby settings" (GAME SETTINGS): the host's Server Setup, in the
lobby's place, out of the history (its START GAME the lobby again, whose B
leaves the game as before) */
static boolean lobby_board_settings(struct widget_instance *widget, boolean *widget_deleted)
{
	if (!global_network_game_server_get())
		return campaign_fail();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return ui_widget_port_switch(widget, SERVER_SETUP_NAME, widget_deleted);
}

/* ---- the hand-written lobby (screens/lobby.xml, docs/menu_screens.md):
the same model in the new engine's tall layout. Its table (the row source
lobby.players) has a lane for each team's column, red's and blue's (in a
game without teams one list down both, under PLAYERS), each place of a lane
a row; its panel and bar the lobby.* bindings; its actions the generated
screen's functions, or these */

static void lobby_utf8(wchar_t const *wide, char *text, short size);
/* (host_controls_menu.inc's: Host Controls' KICK, the host's or a
dedicated server's admin's) */
static boolean host_kick(struct host_controls_player const *player, boolean ban, long seconds, char const *reason);
/* (network_distributed.c's: a client's ping to its host; the host's of a
client's machine) */
long distributed_own_ping(void);
long distributed_machine_ping(long machine_index);

/* the panel the hand-written lobby shows: the next game's, a player's card,
the friends to invite (lobby.details, lobby.card, lobby.inviting) */
enum
{
	_lobby_panel_details,
	_lobby_panel_card,
	_lobby_panel_invite,
};

/* how long a card's or the invite's words of what was done are shown */
#define LOBBY_SAID_TIME 5000
/* a lineup's places: a team's, a game without teams' */
#define LOBBY_LINEUP_TEAM 4
#define LOBBY_LINEUP_ALL 8
/* ui.map's mp_map_grafix's picture of a map it has none of (custom_edition_maps.c's UNKNOWN_LEVEL_FRAME) */
#define LOBBY_UNKNOWN_MAP_FRAME 13

static struct
{
	struct lobby_board_model model;
	/* when the model was made (system_milliseconds), and the rows each lane
	has (the table's: lanes' places) */
	unsigned long made_at;
	boolean made;
	short rows;
	/* the panel shown (_lobby_panel_*); a card's player: its slot of the
	game's record, its machine and name (another there: the card closes) */
	short panel;
	short card_slot;
	long card_machine;
	wchar_t card_name[12];
	/* the friend chosen to invite (its code); what was done, and when */
	unsigned char friend_code[P2P_FRIEND_CODE_BYTES];
	boolean friend_chosen;
	wchar_t said[SETTINGS_PANEL_TEXT_LENGTH];
	unsigned long said_time;
} lobby_screen;

/* the model as this frame has it (made again once a frame: the bindings and
the table read it in turn) */
static struct network_player *lobby_card_player(void);
static short lobby_screen_entry(short index, boolean *more);

static struct lobby_board_model *lobby_screen_model(void)
{
	unsigned long now = system_milliseconds();

	if (!lobby_screen.made || lobby_screen.made_at != now)
	{
		lobby_board_model_make(&lobby_screen.model, lobby_screen.rows ? lobby_screen.rows : LOBBY_LAYOUT_ROWS);
		lobby_screen.made = TRUE;
		lobby_screen.made_at = now;
		/* (a card's player gone: its card closed) */
		if (lobby_screen.panel == _lobby_panel_card && !lobby_card_player())
			lobby_screen.panel = _lobby_panel_details;
	}
	return &lobby_screen.model;
}

/* ---- the panels: a player's card, the friends to invite */

/* the card's player, NULL if it is not in the game now (its slot another's) */
static struct network_player *lobby_card_player(void)
{
	struct lobby_board_model *model = &lobby_screen.model;
	struct network_player *player;

	if (!model->game || lobby_screen.card_slot < 0 || lobby_screen.card_slot >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		return NULL;
	player = &model->game->players[lobby_screen.card_slot];
	if (!network_player_is_valid(player) || player->machine_index != lobby_screen.card_machine ||
		!lobby_board_names_equal(player->name, NUMBEROF(player->name), lobby_screen.card_name))
	{
		return NULL;
	}
	return player;
}

/* ... its entry of the model's (NULL: none) */
static struct lobby_board_entry const *lobby_card_entry(void)
{
	struct lobby_board_model *model = lobby_screen_model();
	struct network_player *player = lobby_card_player();
	short index;

	for (index = 0; player && index < model->count; index++)
	{
		if (model->players[index] == player)
			return &model->entries[index];
	}
	return NULL;
}

/* what was done, said in the panel a while */
static void lobby_say(wchar_t const *format, wchar_t const *name)
{
	usnprintf(lobby_screen.said, NUMBEROF(lobby_screen.said) - 1, format, name ? name : L"");
	lobby_screen.said[NUMBEROF(lobby_screen.said) - 1] = 0;
	lobby_screen.said_time = system_milliseconds() | 1;
}

static boolean lobby_said(void)
{
	return lobby_screen.said_time && system_milliseconds() - lobby_screen.said_time < LOBBY_SAID_TIME;
}

/* a player's color as the game gives it (game_engine_player_get_change_color),
as a <spartan>'s binding's word: a team game's its team's, else its own */
static void lobby_color_word(struct lobby_board_model const *model, struct network_player const *player, char *word,
	unsigned long size)
{
	if (model->game && model->game->variant.universal_variant.teams)
		snprintf(word, size, "%s", player->team_index == 1 ? "blue" : "red");
	else if (player->primary_color_index >= 0 && player->primary_color_index < 18)
		snprintf(word, size, "%d", (int)player->primary_color_index);
	else
		snprintf(word, size, "random");
}

/* the lineup over the players (lobby.lineup): a team game's four of each
team, red's places then blue's (an empty place "-"), else the first eight;
each the host first, then as they joined (the game's record's order). With
names (lobby.lineup.names), each place's player's name in its place, a line
each (an empty place's empty) */
static void lobby_lineup(struct lobby_board_model const *model, boolean names, char *value, unsigned long size)
{
	short team, teams = model->teams ? 2 : 1, written = 0;
	unsigned long used = 0;
	char const *between = names ? "\n" : " ";

	value[0] = 0;
	for (team = 0; team < teams; team++)
	{
		short places = model->teams ? LOBBY_LINEUP_TEAM : LOBBY_LINEUP_ALL, taken = 0, pass, index;

		/* (the host's first, then the rest) */
		for (pass = 0; pass < 2; pass++)
		{
			for (index = 0; index < model->count && taken < places; index++)
			{
				struct lobby_board_entry const *entry = &model->entries[index];
				char word[64];

				if ((pass == 0) != (entry->host != FALSE) || (model->teams && (entry->team == 1 ? 1 : 0) != team))
					continue;
				if (names)
				{
					wchar_t name[NUMBEROF(model->players[index]->name) + 1];

					ustrncpy(name, model->players[index]->name, NUMBEROF(model->players[index]->name));
					name[NUMBEROF(model->players[index]->name)] = 0;
					lobby_utf8(name, word, (short)sizeof(word));
				}
				else
					lobby_color_word(model, model->players[index], word, sizeof(word));
				used += (unsigned long)snprintf(value + used, used < size ? size - used : 0, "%s%s",
					written++ ? between : "", word);
				taken++;
			}
		}
		for (; model->teams && taken < places; taken++)
		{
			used += (unsigned long)snprintf(value + used, used < size ? size - used : 0, "%s%s",
				written++ ? between : "", names ? "" : "-");
		}
	}
	if (used >= size && size)
		value[size - 1] = 0;
}

/* a map's frame of ui.map's mp_map_grafix: an Xbox map's, else its display
index (a Custom Edition map's, a campaign level's: custom_edition_maps.c
draws its picture), else the unknown map's */
static short lobby_map_frame(char const *map_name)
{
	char const *const *names;
	short count = xbox_multiplayer_map_count(ui_widget_port_multiplayer_maps(&names, NULL)), index;
	short shown = custom_edition_maps_display_index(map_name);

	if (shown != NONE)
		return shown;
	for (index = 0; index < count; index++)
	{
		if (!_stricmp(names[index], map_name))
			return index;
	}
	return LOBBY_UNKNOWN_MAP_FRAME;
}

/* the next game's gametype said in a few words: what wins it */
static void lobby_gametype_description(struct network_game const *game, wchar_t *text, short size)
{
	short mode = game_modes_variant_mode(&game->variant, &game->variant_options);
	boolean teams = game->variant.universal_variant.teams;
	wchar_t const *words = L"";

	switch (game->variant.game_engine_index)
	{
	case 0:
		words = L"The campaign, played together.";
		break;
	case 1:
		words = L"Take the other team's flag to your base. The most captures wins.";
		break;
	case 2:
		words = teams ? L"Kill the other team's players. The team with the most kills wins." :
			L"Kill every other player. The most kills wins.";
		break;
	case 3:
		words = L"Hold the skull. The longest held wins.";
		break;
	case 4:
		words = L"Hold the hill. The longest held wins.";
		break;
	case 5:
		words = L"Drive through every checkpoint. The first around wins.";
		break;
	}
	if (mode == _game_mode_infection)
		words = L"The zombies infect everyone they kill. Survive to win.";
	else if (mode == _game_mode_gun_game)
		words = L"A new weapon with every kill. The first through them all wins.";
	else if (mode == _game_mode_swat)
		words = teams ? L"No shields, no radar, one headshot kills. The team with the most kills wins." :
			L"No shields, no radar, one headshot kills. The most kills wins.";
	usnprintf(text, size - 1, L"%s", words);
	text[size - 1] = 0;
}

/* the friends online (p2p_friends.c's list's), into entries: their count */
static short lobby_friends_online(struct p2p_friend *entries, short maximum)
{
	static struct p2p_friend list[P2P_FRIENDS_LIST_MAXIMUM];
	int count = p2p_friends_list(list, P2P_FRIENDS_LIST_MAXIMUM), index;
	short online = 0;

	for (index = 0; index < count && online < maximum; index++)
	{
		if (list[index].kind == _p2p_friend_online)
			entries[online++] = list[index];
	}
	return online;
}

/* the friend chosen to invite among those online (its index; the first if
none is, or it is gone), NONE if none is online */
static short lobby_friend_chosen(struct p2p_friend const *entries, short count)
{
	short index;

	for (index = 0; lobby_screen.friend_chosen && index < count; index++)
	{
		if (!csmemcmp(entries[index].code, lobby_screen.friend_code, P2P_FRIEND_CODE_BYTES))
			return index;
	}
	return count ? 0 : NONE;
}

/* whether INVITE FRIENDS is offered: a game with an invite link, on the
Internet (network.online) */
static boolean lobby_friends_offered(struct lobby_board_model const *model)
{
	return model->controls.invite && config_boolean("network.online");
}

/* the card's player's actions: ADD FRIEND (a code the host told, of no
friend, request or block yet: network_party.c), MUTE (another machine's
player's voice, lines and pings on this machine: network_voice.c), KICK
(Host Controls' own, for its player: the host's) */
static boolean lobby_card_friend(void)
{
	struct lobby_board_entry const *entry = lobby_card_entry();
	int relation;

	if (!entry || entry->bot || entry->own)
		return FALSE;
	relation = network_party_slot_friend(lobby_screen.card_slot);
	return relation == _p2p_friend_none || relation == _p2p_friend_request;
}

static boolean lobby_card_mutable(void)
{
	struct lobby_board_entry const *entry = lobby_card_entry();

	return entry && !entry->bot && !entry->own && !lobby_screen.model.local;
}

/* the card's player's entry of Host Controls' players (FALSE: none, or it
cannot be kicked) */
static boolean lobby_card_host_entry(struct host_controls_player *found)
{
	struct host_controls_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	struct lobby_board_entry const *entry = lobby_card_entry();
	short count, index;

	if (!entry || entry->own || !lobby_screen.model.host_controls)
		return FALSE;
	count = network_host_controls_players(players, NUMBEROF(players));
	for (index = 0; index < count; index++)
	{
		if (players[index].slot == lobby_screen.card_slot && players[index].machine_index == lobby_screen.card_machine &&
			TEST_FLAG(players[index].actions, _host_controls_kick))
		{
			*found = players[index];
			return TRUE;
		}
	}
	return FALSE;
}

/* a card's binding's value ("lobby.card.<name>": nonzero if it has one) */
static int lobby_card_value(char const *name, wchar_t *text, short size)
{
	struct lobby_board_model *model = lobby_screen_model();
	struct network_player *player = lobby_card_player();
	struct lobby_board_entry const *entry = lobby_card_entry();
	struct host_controls_player kickable;

	text[0] = 0;
	if (!player || !entry)
		return FALSE;
	if (!strcmp(name, "lobby.card.name"))
	{
		ustrncpy(text, player->name, MIN(size, (short)NUMBEROF(player->name) + 1) - 1);
		text[MIN(size, (short)NUMBEROF(player->name) + 1) - 1] = 0;
	}
	else if (!strcmp(name, "lobby.card.color"))
	{
		char word[16];
		short letter;

		lobby_color_word(model, player, word, sizeof(word));
		for (letter = 0; word[letter] && letter < size - 1; letter++)
			text[letter] = (wchar_t)word[letter];
		text[letter] = 0;
	}
	else if (!strcmp(name, "lobby.card.ping") || !strcmp(name, "lobby.card.facts"))
	{
		long ping = NONE;
		wchar_t platform[32], party[32], ping_text[32];

		/* (the host's own players none; a client the host's machine's, its
		own round trip; the host each client's, as a game measured it) */
		if (entry->bot)
			usnprintf(ping_text, NUMBEROF(ping_text) - 1, L"Bot");
		else if ((model->host && entry->own) || (model->local))
			usnprintf(ping_text, NUMBEROF(ping_text) - 1, L"Host");
		else
		{
			ping = model->host ? distributed_machine_ping(player->machine_index) :
				entry->host || entry->own ? distributed_own_ping() : NONE;
			if (ping == NONE)
				usnprintf(ping_text, NUMBEROF(ping_text) - 1, L"Not yet");
			else
				usnprintf(ping_text, NUMBEROF(ping_text) - 1, L"%ld ms", ping);
		}
		ping_text[NUMBEROF(ping_text) - 1] = 0;
		if (!strcmp(name, "lobby.card.ping"))
		{
			ustrncpy(text, ping_text, size - 1);
			text[size - 1] = 0;
			return text[0] != 0;
		}
		lobby_card_value("lobby.card.platform", platform, NUMBEROF(platform));
		lobby_card_value("lobby.card.party", party, NUMBEROF(party));
		usnprintf(text, size - 1, L"%s \x00B7 %s \x00B7 %s", ping_text, platform, party);
	}
	else if (!strcmp(name, "lobby.card.platform"))
	{
		/* (a player of a machine whose code the host told: OpenH1's; the
		host of a GOAT H1 game) */
		usnprintf(text, size - 1, entry->bot ? L"Bot" : entry->kind == _lobby_kind_opence ? L"OpenCE" :
			entry->kind == _lobby_kind_openh1 || (entry->host && model->standard) ||
			network_party_slot_friend(lobby_screen.card_slot) != NONE ? L"GOAT H1" : L"Unknown");
	}
	else if (!strcmp(name, "lobby.card.party"))
		usnprintf(text, size - 1, entry->bot ? L"None" : entry->party ? L"Your party" : entry->own ? L"You" : L"None");
	/* (what was just done, else what the player is to this profile) */
	else if (!strcmp(name, "lobby.card.note") && lobby_said())
		ustrncpy(text, lobby_screen.said, size - 1);
	else if (!strcmp(name, "lobby.card.note"))
	{
		int relation = entry->bot || entry->own ? NONE : network_party_slot_friend(lobby_screen.card_slot);

		usnprintf(text, size - 1, relation == _p2p_friend_online ? L"A friend of yours." : relation == _p2p_friend_sent ?
			L"Friend request sent." : relation == _p2p_friend_request ? L"Wants to be your friend." :
			relation == _p2p_friend_blocked ? L"Blocked." : L"");
	}
	else if (!strcmp(name, "lobby.card.friend") && lobby_card_friend())
		usnprintf(text, size - 1, L"1");
	else if (!strcmp(name, "lobby.card.mutable") && lobby_card_mutable())
		usnprintf(text, size - 1, L"1");
	else if (!strcmp(name, "lobby.card.mute") && lobby_card_mutable())
		usnprintf(text, size - 1, network_voice_machine_muted(player->machine_index) ? L"UNMUTE" : L"MUTE");
	else if (!strcmp(name, "lobby.card.kick") && lobby_card_host_entry(&kickable))
		usnprintf(text, size - 1, L"1");
	text[size - 1] = 0;
	return text[0] != 0;
}

/* the friends' invite's binding's value ("lobby.friend...": nonzero if it
has one) */
static int lobby_friends_value(char const *name, wchar_t *text, short size)
{
	static struct p2p_friend online[P2P_FRIENDS_MAXIMUM];
	short count = lobby_friends_online(online, NUMBEROF(online));
	short chosen = lobby_friend_chosen(online, count);

	text[0] = 0;
	if (!strcmp(name, "lobby.friends.count"))
	{
		struct p2p_friends_state state;

		p2p_friends_state(&state);
		if (!config_boolean("network.online"))
			usnprintf(text, size - 1, L"Turn on Internet play to see your friends online.");
		else if (!state.identity)
			usnprintf(text, size - 1, L"Make a profile to have friends.");
		else if (!state.connected)
			usnprintf(text, size - 1, L"Reaching your friends...");
		else
			usnprintf(text, size - 1, count == 1 ? L"1 friend online" : count ? L"%d friends online" :
				L"No friends online. Copy the invite link for anyone else.", count);
	}
	else if (!strcmp(name, "lobby.friend") && chosen != NONE)
		friends_wide(online[chosen].name, text, size);
	else if (!strcmp(name, "lobby.friend.where") && chosen != NONE)
		usnprintf(text, size - 1, L"%s", friends_where(&online[chosen]));
	else if (!strcmp(name, "lobby.friends.said") && lobby_said())
		ustrncpy(text, lobby_screen.said, size - 1);
	text[size - 1] = 0;
	return text[0] != 0;
}

/* the lobby's panels' actions: a player's card opened (A, a click or a
tap on its row: again, closed), the card or the invite closed (B, BACK),
the card's ADD FRIEND, MUTE and KICK, INVITE FRIENDS opened, its friend
stepped and invited (as the Friends screen's INVITE: to this machine's
party, which follows its leader into the game). FALSE: not one of them, or
nothing done */
static int lobby_panel_action(struct widget_instance *widget, char const *name, short row, int *deleted)
{
	struct lobby_board_model *model = lobby_screen_model();

	*deleted = FALSE;
	if (!strcmp(name, "lobby card"))
	{
		boolean more;
		short shown = lobby_screen_entry(row, &more);
		struct network_player *player = shown != NONE ? model->players[shown] : NULL;

		if (!player || !model->game)
			return FALSE;
		/* (the card shown already: closed) */
		if (lobby_screen.panel == _lobby_panel_card && lobby_card_player() == player)
		{
			lobby_screen.panel = _lobby_panel_details;
			ui_play_audio_feedback_sound(SOUND_BACK);
			return TRUE;
		}
		lobby_screen.panel = _lobby_panel_card;
		lobby_screen.card_slot = (short)(player - model->game->players);
		lobby_screen.card_machine = player->machine_index;
		ustrncpy(lobby_screen.card_name, player->name, NUMBEROF(lobby_screen.card_name) - 1);
		lobby_screen.card_name[NUMBEROF(lobby_screen.card_name) - 1] = 0;
		lobby_screen.said_time = 0;
		platform_log("menus: the lobby's card of player %d (machine %ld) opened", (int)lobby_screen.card_slot,
			lobby_screen.card_machine);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	if (!strcmp(name, "lobby close"))
	{
		if (lobby_screen.panel == _lobby_panel_details)
			return FALSE;
		platform_log("menus: the lobby's %s closed", lobby_screen.panel == _lobby_panel_card ? "card" : "invite");
		lobby_screen.panel = _lobby_panel_details;
		ui_play_audio_feedback_sound(SOUND_BACK);
		return TRUE;
	}
	if (!strcmp(name, "lobby friends"))
	{
		if (!model->controls.invite)
			return FALSE;
		lobby_screen.panel = lobby_screen.panel == _lobby_panel_invite ? _lobby_panel_details : _lobby_panel_invite;
		lobby_screen.said_time = 0;
		platform_log("menus: the lobby's invite %s", lobby_screen.panel == _lobby_panel_invite ? "opened" : "closed");
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	if (!strncmp(name, "lobby friend ", 13))
	{
		static struct p2p_friend online[P2P_FRIENDS_MAXIMUM];
		short count = lobby_friends_online(online, NUMBEROF(online));
		short chosen = lobby_friend_chosen(online, count);
		wchar_t friend_name[P2P_FRIEND_NAME_SIZE];

		if (chosen == NONE || lobby_screen.panel != _lobby_panel_invite)
			return FALSE;
		if (!strcmp(name, "lobby friend invite"))
		{
			friends_wide(online[chosen].name, friend_name, NUMBEROF(friend_name));
			if (!p2p_friends_invite(online[chosen].code))
			{
				lobby_say(L"%s could not be invited. Try again in a moment.", friend_name);
				return campaign_fail();
			}
			lobby_say(L"Invited %s to your party: they follow you into this game.", friend_name);
			ui_play_audio_feedback_sound(SOUND_FORWARD);
			return TRUE;
		}
		chosen = (short)((chosen + (strstr(name, " left") ? count - 1 : 1)) % count);
		csmemcpy(lobby_screen.friend_code, online[chosen].code, P2P_FRIEND_CODE_BYTES);
		lobby_screen.friend_chosen = TRUE;
		return TRUE;
	}
	if (!strncmp(name, "lobby card ", 11) && lobby_screen.panel == _lobby_panel_card)
	{
		struct network_player *player = lobby_card_player();
		struct host_controls_player kickable;

		if (!player)
			return FALSE;
		if (!strcmp(name, "lobby card friend"))
		{
			if (!lobby_card_friend() || !network_party_slot_add_friend(lobby_screen.card_slot, player->name))
				return campaign_fail();
			lobby_say(L"Friend request sent to %s.", lobby_screen.card_name);
			platform_log("menus: the lobby's card: a friend request sent to player %d", (int)lobby_screen.card_slot);
		}
		else if (!strcmp(name, "lobby card mute"))
		{
			boolean muted = network_voice_machine_muted(player->machine_index);

			if (!lobby_card_mutable())
				return FALSE;
			network_voice_mute_machine(player->machine_index, !muted);
			platform_log("menus: the lobby's card: machine %ld %s", lobby_screen.card_machine, muted ? "unmuted" :
				"muted");
			lobby_say(muted ? L"%s unmuted." : L"%s muted: you no longer hear their voice, lines or pings.",
				lobby_screen.card_name);
		}
		else if (!strcmp(name, "lobby card kick"))
		{
			if (!lobby_card_host_entry(&kickable) || !host_kick(&kickable, FALSE, 0, NULL))
				return campaign_fail();
			platform_log("menus: the lobby's card: player %d kicked", (int)lobby_screen.card_slot);
			lobby_screen.panel = _lobby_panel_details;
		}
		else
			return FALSE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	(void)widget;
	return FALSE;
}

/* a lane's place's player (index: the lane's rows in turn), NONE for none,
and whether it is the lane's last, which says how many more */
static short lobby_screen_entry(short index, boolean *more)
{
	struct lobby_board_model *model = lobby_screen_model();
	short rows = model->layout.row_count, column = (short)(index / MAX(rows, 1)), row = (short)(index % MAX(rows, 1));

	*more = FALSE;
	if (column < 0 || column >= LOBBY_BOARD_COLUMNS || row >= rows)
		return NONE;
	*more = model->layout.rows[column][row] == NONE && row == rows - 1 && model->layout.more[column] > 0;
	return model->layout.rows[column][row];
}

static void lobby_screen_update(struct menu_table_view *view)
{
	short lanes = (short)MAX(view->lanes, 1);

	lobby_screen.rows = (short)MIN(MAX(view->shown / lanes, 1), LOBBY_LAYOUT_ROWS);
	lobby_screen.made = FALSE;
	lobby_screen_model();
	view->count = view->shown;
	view->first = 0;
	view->chosen = NONE;
	view->focus = NONE;
	/* (the speakers of the rows drawn this frame, noted as their cells are:
	pc_menu_speaker_note) */
	lobby_icons.count = 0;
	lobby_icons.updated_at = system_milliseconds();
}

static int lobby_screen_heading(short index, unsigned short *words, short size)
{
	(void)index;
	(void)words;
	(void)size;
	return FALSE;
}

static int lobby_screen_state(short index)
{
	boolean more;
	short shown = lobby_screen_entry(index, &more);

	if (shown == NONE)
		return more ? MENU_TABLE_ROW_PLAIN : MENU_TABLE_ROW_NONE;
	return lobby_screen.model.entries[shown].own ? MENU_TABLE_ROW_MARKED : MENU_TABLE_ROW_PLAIN;
}

/* a row's cells: its player's dot (its team's, or a game without teams'),
name (its controller's too, of this machine's players when it has more than
one; its speaker in voice chat) and tag (a bot's with the bots' skill where
it fits); the lane's last, how many more */
static void lobby_screen_cell(short index, char const *column, int focused, struct menu_table_cell *cell)
{
	struct lobby_board_model *model = lobby_screen_model();
	boolean more;
	short shown = lobby_screen_entry(index, &more);
	struct lobby_board_entry const *entry = shown != NONE ? &model->entries[shown] : NULL;
	wchar_t text[ROW_TEXT_LENGTH];

	(void)focused;
	text[0] = 0;
	if (!strcmp(column, "dot"))
	{
		cell->frame = (short)(!entry ? NONE : model->teams ? (entry->team == 1 ? 1 : 0) : 2);
		return;
	}
	if (more && !strcmp(column, "name"))
	{
		short lane = (short)(index / MAX(model->layout.row_count, 1));

		usnprintf(text, NUMBEROF(text) - 1, L"+%d more", model->layout.more[lane]);
	}
	else if (entry && !strcmp(column, "name"))
	{
		struct network_player const *player = model->players[shown];
		wchar_t name[NUMBEROF(player->name) + 1];

		ustrncpy(name, player->name, NUMBEROF(player->name));
		name[NUMBEROF(player->name)] = 0;
		if (entry->own && lobby_local_player_count() >= 2)
			usnprintf(text, NUMBEROF(text) - 1, L"%s (P%d)", name, player->controller_index + 1);
		else
			usnprintf(text, NUMBEROF(text) - 1, L"%s", name);
		cell->speaker = player->machine_index;
	}
	else if (entry && !strcmp(column, "tag"))
	{
		static wchar_t const *const skills[] = { L"EASY", L"NORMAL", L"HEROIC", L"LEGENDARY" };
		short letter;

		ustrncpy(text, lobby_tag_words[lobby_board_tag(entry)], NUMBEROF(text) - 1);
		/* (SPLIT where SPLIT SCREEN would be cut short) */
		if (lobby_board_tag(entry) == _lobby_tag_split)
		{
			static char const split[] = "SPLIT";

			for (letter = 0; split[letter]; letter++)
				cell->fallback[letter] = (unsigned short)split[letter];
			cell->fallback[letter] = 0;
		}
		if (entry->bot)
		{
			/* (BOT alone where BOT and the skill would be cut short) */
			for (letter = 0; text[letter] && letter < MENU_TABLE_TEXT_LENGTH - 1; letter++)
				cell->fallback[letter] = (unsigned short)text[letter];
			cell->fallback[letter] = 0;
			usnprintf(text, NUMBEROF(text) - 1, L"BOT \x00B7 %s", skills[PIN(bots_skill(), 0, (short)NUMBEROF(skills) - 1)]);
		}
	}
	text[NUMBEROF(text) - 1] = 0;
	{
		short letter;

		for (letter = 0; text[letter] && letter < MENU_TABLE_TEXT_LENGTH - 1; letter++)
			cell->text[letter] = (unsigned short)text[letter];
		cell->text[letter] = 0;
	}
}

struct menu_table_source const pc_menu_lobby_source =
{
	lobby_screen_update, lobby_screen_heading, lobby_screen_cell, NULL, lobby_screen_state,
};

void pc_menu_speaker_note(struct widget_instance *text, long machine)
{
	if (!text || lobby_icons.count >= LOBBY_ICON_ROWS)
		return;
	lobby_icons.texts[lobby_icons.count] = text;
	lobby_icons.machines[lobby_icons.count] = machine;
	lobby_icons.count++;
	lobby_icons.updated_at = system_milliseconds();
}

/* text (UTF-16) as a binding's value, UTF-8: nonzero if it has any */
static int lobby_screen_words(wchar_t const *text, char *value, unsigned long size)
{
	lobby_utf8(text, value, (short)MIN(size, 0x7FFF));
	return value[0] != 0;
}

/* a lobby.* binding's value (docs/menu_screens.md's table) */
int pc_menu_lobby_value(char const *name, char *value, unsigned long size)
{
	struct lobby_board_model *model = lobby_screen_model();
	struct lobby_board_controls const *controls = &model->controls;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH / 4];
	boolean yes = FALSE;

	if (!size)
		return 0;
	value[0] = 0;
	text[0] = 0;
	if (!strcmp(name, "lobby.title"))
		usnprintf(text, NUMBEROF(text) - 1, L"GAME LOBBY");
	/* (the band's words, and its shorter forms, a line each) */
	else if (!strcmp(name, "lobby.subtitle") || !strcmp(name, "lobby.subtitle.short"))
	{
		wchar_t band[SETTINGS_PANEL_TEXT_LENGTH];
		wchar_t *shorter;

		ustrncpy(band, model->band, NUMBEROF(band) - 1);
		band[NUMBEROF(band) - 1] = 0;
		for (shorter = band; *shorter && *shorter != '\n'; shorter++)
			;
		if (*shorter)
			*shorter++ = 0;
		return lobby_screen_words(!strcmp(name, "lobby.subtitle") ? band : shorter, value, size);
	}
	/* (the lanes: RED and BLUE in a team game, each its players' count;
	else PLAYERS across both, how many of the most at its right) */
	else if (!strcmp(name, "lobby.red"))
		usnprintf(text, NUMBEROF(text) - 1, model->teams ? L"RED" : L"PLAYERS");
	else if (!strcmp(name, "lobby.blue") && model->teams)
		usnprintf(text, NUMBEROF(text) - 1, L"BLUE");
	else if ((!strcmp(name, "lobby.red.count") || !strcmp(name, "lobby.blue.count")) && model->teams)
	{
		short count = model->layout.counts[name[6] == 'b' ? 1 : 0];

		usnprintf(text, NUMBEROF(text) - 1, count == 1 ? L"1 PLAYER" : L"%d PLAYERS", count);
	}
	else if (!strcmp(name, "lobby.blue.count") && model->game && model->local)
		usnprintf(text, NUMBEROF(text) - 1, model->count == 1 ? L"1 PLAYER" : L"%d PLAYERS", model->count);
	else if (!strcmp(name, "lobby.blue.count") && model->game)
		usnprintf(text, NUMBEROF(text) - 1, L"%d OF %d", model->count, model->game->maximum_players);
	else if (!strcmp(name, "lobby.red.tone"))
		usnprintf(text, NUMBEROF(text) - 1, model->teams ? L"0" : L"2");
	else if (!strcmp(name, "lobby.blue.tone"))
		usnprintf(text, NUMBEROF(text) - 1, model->teams ? L"1" : L"2");
	/* (the panel: the next game) */
	else if (!model->game && !strncmp(name, "lobby.", 6) && strcmp(name, "lobby.add") && strcmp(name, "lobby.invite") &&
		strcmp(name, "lobby.chat"))
	{
		return 0;
	}
	/* (which panel is shown: the next game's, a player's card, the friends
	to invite; the card's and the invite's words) */
	else if (!strcmp(name, "lobby.details"))
		yes = lobby_screen.panel == _lobby_panel_details;
	else if (!strcmp(name, "lobby.card"))
		yes = lobby_screen.panel == _lobby_panel_card;
	else if (!strcmp(name, "lobby.inviting"))
		yes = lobby_screen.panel == _lobby_panel_invite;
	else if (!strcmp(name, "lobby.panel.open"))
		yes = lobby_screen.panel != _lobby_panel_details;
	else if (!strncmp(name, "lobby.card.", 11))
	{
		if (lobby_screen.panel != _lobby_panel_card || !lobby_card_value(name, text, NUMBEROF(text)))
			return 0;
		return lobby_screen_words(text, value, size);
	}
	else if (!strcmp(name, "lobby.friends"))
		yes = lobby_friends_offered(model);
	else if (!strncmp(name, "lobby.friend", 12))
	{
		if (lobby_screen.panel != _lobby_panel_invite || !lobby_friends_value(name, text, NUMBEROF(text)))
			return 0;
		return lobby_screen_words(text, value, size);
	}
	/* (the next game's map's picture, its gametype's words, its map and
	limits on a line; the lineup over the players) */
	else if (!strcmp(name, "lobby.map.picture"))
		usnprintf(text, NUMBEROF(text) - 1, L"%d", (int)lobby_map_frame(model->game->map.name));
	else if (!strcmp(name, "lobby.description"))
		lobby_gametype_description(model->game, text, NUMBEROF(text));
	else if (!strcmp(name, "lobby.where"))
	{
		if (model->limits[0])
			usnprintf(text, NUMBEROF(text) - 1, L"%s \x00B7 %s", model->map, model->limits);
		else
			ustrncpy(text, model->map, NUMBEROF(text) - 1);
	}
	else if (!strcmp(name, "lobby.lineup") || !strcmp(name, "lobby.lineup.names"))
	{
		lobby_lineup(model, !strcmp(name, "lobby.lineup.names"), value, size);
		return value[0] != 0;
	}
	else if (!strcmp(name, "lobby.gametype"))
		return lobby_screen_words(model->gametype, value, size);
	else if (!strcmp(name, "lobby.map"))
		return lobby_screen_words(model->map, value, size);
	else if (!strcmp(name, "lobby.limits"))
		return lobby_screen_words(model->limits, value, size);
	else if (!strcmp(name, "lobby.kind"))
		usnprintf(text, NUMBEROF(text) - 1, model->standard ? L"GOAT H1" : L"LEGACY");
	else if (!strcmp(name, "lobby.kind.tone"))
		usnprintf(text, NUMBEROF(text) - 1, model->standard ? L"0" : L"1");
	else if (!strcmp(name, "lobby.tick"))
		usnprintf(text, NUMBEROF(text) - 1, L"%ld TICK", model->standard && model->rate > 0 ? model->rate : 30L);
	else if (!strcmp(name, "lobby.opence") && model->opence)
		usnprintf(text, NUMBEROF(text) - 1, L"OPENCE CAN JOIN");
	else if (!strcmp(name, "lobby.after") && model->then)
		return lobby_screen_words(model->after, value, size);
	/* (the host's bots: BOTS' choice, SKILL and TEAMS, dimmed for a gametype
	the bots do not play, BOTS for a matchmaking game's too) */
	else if (!strcmp(name, "lobby.bots"))
		network_host_controls_bot_choice_words(network_host_controls_bots_chosen(), text, NUMBEROF(text));
	else if (!strcmp(name, "lobby.skill"))
	{
		static wchar_t const *const skills[] = { L"EASY", L"NORMAL", L"HEROIC", L"LEGENDARY" };

		usnprintf(text, NUMBEROF(text) - 1, L"%s", skills[PIN(bots_skill(), 0, (short)NUMBEROF(skills) - 1)]);
	}
	else if (!strcmp(name, "lobby.teams"))
		usnprintf(text, NUMBEROF(text) - 1, bots_teams() == _bots_teams_against ? L"AGAINST YOU" : L"EVEN");
	else if (!strcmp(name, "lobby.bots.shown"))
		yes = controls->bots;
	/* (SKILL and TEAMS while BOTS adds some) */
	else if (!strcmp(name, "lobby.bots.some"))
	{
		long number;

		yes = controls->bots &&
			network_host_controls_bot_choice(network_host_controls_bots_chosen(), &number) != _host_bots_none;
	}
	else if (!strcmp(name, "lobby.bots.locked"))
		yes = controls->bots_locked;
	else if (!strcmp(name, "lobby.bots.count.locked"))
		yes = controls->bots_locked || bots_matchmaking_on();
	/* (how the game is; START GAME's words, the countdown; a player's
	waiting) */
	else if (!strcmp(name, "lobby.status"))
		return lobby_screen_words(model->status, value, size);
	else if (!strcmp(name, "lobby.start"))
		return lobby_screen_words(model->start, value, size);
	else if (!strcmp(name, "lobby.waiting"))
		return lobby_screen_words(model->waiting, value, size);
	/* (what is offered, for ifs) */
	else if (!strcmp(name, "lobby.host"))
		yes = controls->start;
	else if (!strcmp(name, "lobby.settings"))
		yes = controls->settings;
	else if (!strcmp(name, "lobby.controls"))
		yes = model->host_controls;
	else if (!strcmp(name, "lobby.team.change"))
		yes = controls->team_change;
	else if (!strcmp(name, "lobby.invite"))
		yes = controls->invite;
	else if (!strcmp(name, "lobby.add"))
		yes = controls->add_player;
	/* (the lobby's chat on: the host's, network_chat.c) */
	else if (!strcmp(name, "lobby.chat"))
		yes = network_chat_lobby_on();
	else
		return 0;
	if (yes)
		usnprintf(text, NUMBEROF(text) - 1, L"1");
	text[NUMBEROF(text) - 1] = 0;
	return lobby_screen_words(text, value, size);
}

/* the lobby's actions of its own: a row chosen (a click on a lane: its
team, as a click on the generated screen's column; else the host's start,
as A on its players), the bots' choices stepped (BOTS' choices at their
ends no further; SKILL and TEAMS round), as the generated screen's spinners
write them */
int pc_menu_lobby_action(struct widget_instance *widget, struct event_record *event, char const *name, short row,
	int *deleted)
{
	struct lobby_board_model *model = lobby_screen_model();
	short controller = event_controller(widget, event);
	short step = (short)(strstr(name, " left") ? -1 : 1);
	short x, y;

	*deleted = FALSE;
	/* (the chat: a line begun, with the on-screen keyboard on a controller
	and a touchscreen; on the keyboard, typed: network_chat.c) */
	if (!strcmp(name, "lobby chat"))
	{
		boolean keyboard = ui_widget_port_touch_menus() || !halo_keyboard_prompts();

		if (!network_chat_lobby_begin(keyboard ? controller : NONE, keyboard))
			return FALSE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	if (!strcmp(name, "lobby card") || !strcmp(name, "lobby close") || !strncmp(name, "lobby card ", 11) ||
		!strncmp(name, "lobby friend", 12))
	{
		return lobby_panel_action(widget, name, row, deleted);
	}
	if (!strcmp(name, "lobby row"))
	{
		short rows = MAX(model->layout.row_count, 1);

		if (!ui_widget_port_take_click(&x, &y))
			return lobby_board_start(controller);
		if (!model->teams || row == NONE)
			return TRUE;
		return lobby_board_team(widget, controller, (short)(row / rows == 1 ? 1 : 0));
	}
	if (!model->controls.bots || model->controls.bots_locked)
		return FALSE;
	if (!strncmp(name, "lobby bots ", 11))
	{
		short chosen = network_host_controls_bots_chosen();
		short stepped = network_host_controls_bot_choice_step(chosen, step);
		long number;
		short kind = network_host_controls_bot_choice(stepped, &number);

		return !bots_matchmaking_on() && stepped != chosen && network_host_controls_choose_bots(kind, number);
	}
	if (!strncmp(name, "lobby skill ", 12))
	{
		static char const *const skills[] = { "easy", "normal", "heroic", "legendary" };

		return config_write("bots.skill", skills[(bots_skill() + step + 4) % 4]) != 0;
	}
	if (!strncmp(name, "lobby teams ", 12))
		return config_write("bots.teams", bots_teams() == _bots_teams_against ? "even" : "against") != 0;
	return FALSE;
}

/* each frame the hand-written lobby is drawn: what the generated screen's
list did (its data inputs: the split screen's players who want to play
added, "net splitscreen prejoin players"; the model made, which opens the
map vote's screen) */
void pc_menu_lobby_frame(struct widget_instance *screen)
{
	ui_widget_game_data_function_invoke(screen, LOBBY_PREJOIN_FUNCTION);
	lobby_screen_model();
	/* (its chat's keys, T and Y, while it is shown) */
	network_chat_lobby_frame();
	/* (the keyboard's keys the tall screens draw, the browsers': X as Tab
	(ADD PLAYER), Y as F (COPY LINK), RB as E (INVITE FRIENDS), each its own
	key; elsewhere E is both X and RB: xinput_sdl.c's keyboard_gamepad) */
	halo_keyboard_browser_keys();
}

/* ---- the lobby's chat (<chat source="lobby.chat">: network_chat.c's lobby
lines, each its sender's name in its team's colour) */

static short lobby_chat_lines(struct menu_chat_line *lines, short maximum)
{
	struct network_chat_lobby_line said[NETWORK_CHAT_LOBBY_LINES];
	short count = network_chat_lobby_lines(said, (short)MIN(maximum, NETWORK_CHAT_LOBBY_LINES)), index;

	for (index = 0; index < count; index++)
	{
		struct menu_chat_line *line = &lines[index];
		short letter;

		csmemset(line, 0, sizeof(*line));
		for (letter = 0; letter < (short)NUMBEROF(said[index].name) && letter < (short)NUMBEROF(line->name) - 1 &&
			said[index].name[letter]; letter++)
			line->name[letter] = (unsigned short)said[index].name[letter];
		for (letter = 0; letter < (short)NUMBEROF(line->text) - 1 && said[index].text[letter]; letter++)
			line->text[letter] = (unsigned short)(unsigned char)said[index].text[letter];
		/* (the scoreboard's team colours; a notice's pale gold) */
		line->color = said[index].color == NETWORK_CHAT_LOBBY_RED ? 0xFF6B5EFFUL :
			said[index].color == NETWORK_CHAT_LOBBY_BLUE ? 0x5AA9FFFFUL : 0xEDD173FFUL;
	}
	return count;
}

static char const *lobby_chat_typed(int *keyboard)
{
	boolean open = FALSE;
	char const *typed = network_chat_lobby_typed(&open);

	*keyboard = open;
	return typed;
}

static void lobby_chat_keyboard(long font, short x0, short y0, short x1, short y1, short screen_width,
	short screen_height)
{
	if (font != NONE)
		network_chat_lobby_draw_keyboard(font, x0, y0, x1, y1, screen_width, screen_height);
}

/* the line typed's place's words while none is: how to begin one, or that
the host has the chat off */
static char const *lobby_chat_prompt(void)
{
	if (!network_chat_lobby_on())
		return "Text chat is off in this lobby";
	if (ui_widget_port_touch_menus())
		return "Tap here to chat";
	return halo_keyboard_prompts() ? "Press T to chat" : "%white-button Chat";
}

struct menu_chat_source const pc_menu_lobby_chat =
{
	lobby_chat_lines, lobby_chat_typed, lobby_chat_keyboard, lobby_chat_prompt,
};

/* ---- what Discord is told (p2p_discord_set_game, from party_update) */

/* scenario.c's: the map played */
extern long global_scenario_index;

/* text as UTF-8 (Discord's), at most size bytes with its end */
static void lobby_utf8(wchar_t const *wide, char *text, short size)
{
	short length = 0;

	for (; *wide && length < size - 1; wide++)
	{
		unsigned long code = (unsigned short)*wide;

		if (code < 0x80)
			text[length++] = (char)code;
		else if (code < 0x800 && length < size - 2)
		{
			text[length++] = (char)(0xC0 | code >> 6);
			text[length++] = (char)(0x80 | (code & 0x3F));
		}
		else if (code >= 0x800 && (code < 0xD800 || code > 0xDFFF) && length < size - 3)
		{
			text[length++] = (char)(0xE0 | code >> 12);
			text[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
			text[length++] = (char)(0x80 | (code & 0x3F));
		}
		else
			break;
	}
	text[length] = 0;
}

/* the game this machine is in, as Discord's activity says it: "OpenH1 120
Tick · Slayer on Blood Gulch", "OpenH1 120 Tick · Classic · Pro Doubles on
Prisoner" for a Pro 2v2 Classic game (Legacy for a game of OpenCE's netcode; a
game of this machine alone's without either), "Campaign on Halo"; whether
this machine hosts it for the network, and whether it is under way, its
invite link and players */
static void lobby_presence_tell(void)
{
	void *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short state = party_client_state();
	wchar_t text[ROW_TEXT_LENGTH * 3], map[ROW_TEXT_LENGTH];
	char description[ROW_TEXT_LENGTH * 3];
	char link[TEXT_FIELD_LENGTH];
	boolean host = global_network_game_server_get() != NULL;
	boolean local = host && network_game_is_splitscreen_local();

	if (game && state >= _client_state_pregame)
	{
		wchar_t const *kind = game_modes_variant_kind_name(&game->variant, &game->variant_options);
		/* (the gametype's own name, or co-op's with its difficulty: "Co-op
		Legendary", longer than a gametype's name) */
		wchar_t gametype[MAX(NUMBEROF(game->variant.human_readable_game_description) + 1, 24)];
		boolean standard = host ? network_standard_hosting() : network_standard_agreed_rate() > 0;
		long rate = network_standard_agreed_rate();
		size_t length = 0;

		ustrncpy(gametype, game->variant.human_readable_game_description,
			NUMBEROF(game->variant.human_readable_game_description));
		gametype[NUMBEROF(game->variant.human_readable_game_description)] = 0;
		if (!gametype[0])
			ustrncpy(gametype, kind ? kind : game->variant.game_engine_index ?
				engine_names[PIN(game->variant.game_engine_index, 0, 5)] : L"Co-op", NUMBEROF(gametype) - 1);
		lobby_board_map_text(game->map.name, map, NUMBEROF(map));
		text[0] = 0;
		/* (network co-op: "Co-op Heroic on The Silent Cartographer", which a
		friend's co-op list reads its mission and difficulty from:
		coop_menu.inc's coop_friend_game) */
		if (!local && !game->variant.game_engine_index && game->difficulty >= 0 && game->difficulty < 4)
		{
			static wchar_t const *const difficulties[] = { L"Easy", L"Normal", L"Heroic", L"Legendary" };

			usnprintf(gametype, NUMBEROF(gametype) - 1, L"Co-op %s", difficulties[game->difficulty]);
			gametype[NUMBEROF(gametype) - 1] = 0;
		}
		else if (!local)
		{
			/* (a Pro 2v2 Classic game's said so, its record's Instant Aim
			off for everyone: the 2V2 browser tells a friend's kind by it,
			doubles_friend_in_kind, both kinds at 120) */
			if (standard && TEST_FLAG(game->cooperative_flags, _network_game_no_direct_camera_bit))
				usnprintf(text, NUMBEROF(text) - 1, L"OpenH1 %ld Tick \x00B7 Classic \x00B7 ", rate > 0 ? rate : 30L);
			else if (standard)
				usnprintf(text, NUMBEROF(text) - 1, L"OpenH1 %ld Tick \x00B7 ", rate > 0 ? rate : 30L);
			else
				usnprintf(text, NUMBEROF(text) - 1, L"Legacy \x00B7 ");
			text[NUMBEROF(text) - 1] = 0;
			length = ustrlen(text);
		}
		usnprintf(text + length, NUMBEROF(text) - 1 - length, L"%s on %s", gametype, map);
		text[NUMBEROF(text) - 1] = 0;
		lobby_utf8(text, description, sizeof(description));
		p2p_discord_set_game(host && !local, state >= _client_state_ingame, description,
			lobby_board_invite(link, sizeof(link)) > 0 ? link : NULL, game->player_count, game->maximum_players);
		return;
	}
	/* (the campaign, or a game of its own without the network) */
	if (!client && pc_menu_game_playing() && global_scenario_index != NONE)
	{
		lobby_board_map_text(tag_get_name(global_scenario_index), map, NUMBEROF(map));
		usnprintf(text, NUMBEROF(text) - 1, L"Campaign on %s", map);
		text[NUMBEROF(text) - 1] = 0;
		lobby_utf8(text, description, sizeof(description));
		p2p_discord_set_game(FALSE, TRUE, description, NULL, 0, 0);
		return;
	}
	p2p_discord_set_game(FALSE, FALSE, NULL, NULL, 0, 0);
}
