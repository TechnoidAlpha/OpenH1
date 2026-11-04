/*
HOST_CONTROLS_MENU.INC

Host Controls (tools/port_settings.py's _host_controls, in the server
browser's look: the menus' shared parts), the host's of a network game: the
pause menu's HOST CONTROLS (menu_tags.c's pause_patch) and the lobby's (its
HOST CONTROLS: the players there, before the game starts), which the
scoreboard's "Host controls..." and the console's host_controls open too
(network_host_controls_open). Included in menu_functions.c, whose widgets'
helpers it uses. What it does is network_host_controls.c's and
network_host_game.c's, by the host's own ways: this file only shows it.

- PLAYERS: the game's players (their name, team, score, ping and tag:
  HOST, BOT, OPENH1, OPENCE; in a Legacy game the host cannot tell the
  last two apart, and shows neither), and for the player chosen the panel's
  KICK, BAN... (then how long: an hour, a day or for ever, and a REASON
  typed), MOVE TO RED or BLUE (in the lobby, every game; in a game, one whose
  machines all hear of it), MUTE... (then how long) or UNMUTE, those the
  player may have done to them (network_host_controls_player_actions).
- GAME: End game now and Restart round (each pressed twice), CHANGE MAP...
  (MAPS), CHANGE RULES... (RULES), Shuffle and Balance teams (pressed twice in a game, where the
  players moved die), Announce (typed: every machine's notice), Score limit
  and Time limit (left and right: the lobby's at once, else the next
  game's), Password (typed: a public internet game's, at once), Lock teams,
  Allow votekick, Listing (an internet game's), Max players (left and
  right: never fewer than are in it), Allow OpenCE players (an OpenH1
  game's).
- MAPS: the gametype (left and right: first CURRENT, the game's own with
  its score, time and rules, then the gametypes), then the multiplayer maps, the
  Xbox's and Custom Edition's; for the map chosen the panel's PLAY NOW
  (pressed twice in a game: it ends), AFTER THIS GAME and ADD TO PLAYLIST.
- PLAYLIST: a change asked for, then network.host_playlist's games, the next
  marked; for the one chosen PLAY NOW, PLAY NEXT, REMOVE and CLEAR PLAYLIST
  (pressed twice), or a change's CANCEL.
- BANS: bans.txt's lines (read again each second), and the panel's UNBAN.
- RULES (GAME's CHANGE RULES..., in GAME's place on the strip; B goes back
  to GAME): the gametype's rules (network_host_rules_rows: the vehicles,
  starting weapons, weapon set, shields, motion tracker, infinite grenades,
  invisible players), each stepped by left and right or A, in the game under
  way or the lobby (network_host_rules.c), and SAVE AS GAMETYPE... (a name
  typed: Enter saves).
- BOTS (not in co-op, where the tab is hidden): the bots' rows
  (network_host_controls_bot_rows: BOTS, the lobby's NONE, ADD n or FILL TO
  n; SKILL, TEAMS, BALANCE TEAMS, TEAM TACTICS), each written to config.toml and
  taken by the game at once, then the bots playing (their name, team and
  score, as PLAYERS lists them), and for the bot chosen REMOVE BOT.

An admin of a dedicated server (network_admin.c) has them too, in the
server's game: the screens show what the server tells the admin's machine,
and each change is a request the server runs as its own command. It has
PLAYERS, GAME (End game now, Restart round, CHANGE MAP..., Shuffle and
Balance teams, Announce, Lock teams), MAPS (the server's gametypes; ADD TO
PLAYLIST adds to its rotation), PLAYLIST (the server's rotation: PLAY NOW,
AFTER THIS GAME, REMOVE) and BANS; the server's settings are its owner's.

A or a click on a player's, a map's, a playlist's, a ban's or a bot's row
chooses it and puts the focus on the panel's buttons; on a game's or a
bots' setting's row it changes the row (left and right step it; a typed row's A begins and ends the typing,
B leaves it as it was). What was done is said in the panel for a few
seconds.
*/

/* the rows and the panel's buttons (port_settings.py's HOST_ROWS,
HOST_ACTIONS) */
#define HOST_CONTROLS_ROWS 14
#define HOST_CONTROLS_ACTIONS 4
/* milliseconds a second press confirms in, a notice shows, the bans are
read again after */
#define HOST_CONTROLS_CONFIRM_TIME 5000
#define HOST_CONTROLS_NOTICE_TIME 5000
#define HOST_CONTROLS_BANS_INTERVAL 1000
#define HOST_CONTROLS_TEXT_LENGTH 160
/* the team column's colours (red, blue) */
#define HOST_CONTROLS_RED 0xE86A5AFFUL
#define HOST_CONTROLS_BLUE 0x6FB4FFFFUL
/* a ban's reason, typed */
#define HOST_CONTROLS_REASON_LENGTH 64
/* an announcement, typed (a notice's, network_distributed.c) */
#define HOST_CONTROLS_ANNOUNCE_LENGTH 100

enum
{
	_host_tab_players,
	_host_tab_game,
	_host_tab_maps,
	_host_tab_playlist,
	_host_tab_bans,
	/* (last: hidden in co-op, leaving no gap in the strip) */
	_host_tab_bots,
	NUMBER_OF_HOST_TABS,
	/* (GAME's CHANGE RULES...: no tab of the strip's, GAME's shown) */
	_host_tab_rules = NUMBER_OF_HOST_TABS,
};

/* Game's rows */
enum
{
	_host_game_end,
	_host_game_restart,
	_host_game_change_map,
	_host_game_change_rules,
	_host_game_shuffle_teams,
	_host_game_balance_teams,
	_host_game_announce,
	_host_game_score_limit,
	_host_game_time_limit,
	_host_game_password,
	_host_game_lock_teams,
	_host_game_votekick,
	_host_game_listing,
	_host_game_maximum_players,
	_host_game_opence,
	NUMBER_OF_HOST_GAME_ROWS,
	MAXIMUM_HOST_GAME_ROWS = NUMBER_OF_HOST_GAME_ROWS,
};

/* the panel's buttons' actions: the player's (_host_controls_*), and the
others' */
enum
{
	_host_button_unban = NUMBER_OF_HOST_CONTROLS_ACTIONS,
	/* (a ban's and a mute's lengths, the ban's reason, and leaving them) */
	_host_button_ban_hour,
	_host_button_ban_day,
	_host_button_ban_forever,
	_host_button_ban_reason,
	_host_button_mute_short,
	_host_button_mute_hour,
	_host_button_mute_day,
	_host_button_unmute,
	_host_button_cancel,
	/* (a map's) */
	_host_button_map_now,
	_host_button_map_after,
	_host_button_map_add,
	/* (a playlist's entry's, and a change's) */
	_host_button_entry_now,
	_host_button_entry_next,
	_host_button_entry_remove,
	_host_button_playlist_clear,
	_host_button_change_cancel,
	/* (a bot's) */
	_host_button_bot_remove,
	/* (what a second press confirms: a game's row plus
	MAXIMUM_HOST_GAME_ROWS, a bots' row plus HOST_CONFIRM_BOT_ROW, a button
	plus HOST_CONFIRM_BUTTON) */
	_host_confirm_none = NONE,
	HOST_CONFIRM_BOT_ROW = 500,
	HOST_CONFIRM_BUTTON = 1000,
};

/* the playlist's tab's entry for a change asked for (before its games) */
#define HOST_PLAYLIST_CHANGE (-2)

/* the steps of the score to win and of the time limit (minutes; 0: none) */
static long const host_score_steps[] = { 1, 2, 3, 4, 5, 7, 10, 15, 20, 25, 30, 40, 50, 75, 100, 150, 200, 250, 500,
	999 };
static short const host_time_steps[] = { 0, 5, 10, 15, 20, 25, 30, 45, 60, 90, 120 };
/* a ban's lengths (seconds; 0: for ever) and a mute's (minutes), as the
buttons have them */
#define HOST_BAN_HOUR (60L * 60)
#define HOST_BAN_DAY (24L * 60 * 60)
#define HOST_MUTE_SHORT 10
#define HOST_MUTE_HOUR 60
#define HOST_MUTE_DAY (24 * 60)

static struct
{
	short tab;
	/* the first entry the rows show */
	short first;
	/* the entry chosen: a player (datum index, or a lobby's slot), a game's
	row, a map (its level), a playlist's entry (HOST_PLAYLIST_CHANGE: the
	change), a ban's line */
	long chosen_player;
	long chosen_slot;
	long chosen_machine;
	wchar_t chosen_name[12];
	short chosen_game_row;
	char chosen_map[HOST_GAME_MAP_SIZE];
	short chosen_entry;
	char chosen_ban[HOST_CONTROLS_BAN_LINE_SIZE];
	/* (the BOTS tab's: a row of theirs, NONE; else the bot, as the players'
	are chosen, if chosen_bot_valid) */
	short chosen_bot_row;
	boolean chosen_bot_valid;
	struct host_controls_player chosen_bot;
	/* (RULES': a row, NONE) */
	short chosen_rule;
	/* the panel's buttons asking how long (a _host_controls_ban or _mute;
	NONE: not), and the ban's reason typed */
	short choosing;
	char reason[HOST_CONTROLS_REASON_LENGTH + 1];
	/* the gametype the maps play (network_host_game_gametypes' index; NONE:
	CURRENT, the game's own kept) */
	short gametype;
	/* the press a second confirms, and when it was asked */
	short confirm;
	unsigned long confirm_time;
	/* what was done, and when */
	wchar_t notice[HOST_CONTROLS_TEXT_LENGTH];
	unsigned long notice_time;
	/* this frame's entries */
	struct host_controls_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short player_count;
	short game_rows[MAXIMUM_HOST_GAME_ROWS];
	short game_row_count;
	short bot_rows[NUMBER_OF_HOST_BOT_ROWS];
	short bot_row_count;
	struct host_controls_player bots[BOTS_MAXIMUM];
	short bot_count;
	short rule_rows[NUMBER_OF_HOST_RULES];
	short rule_row_count;
	struct host_game_map const *maps;
	short map_count;
	struct host_controls_ban bans[MAXIMUM_HOST_CONTROLS_BANS];
	char ban_lines[MAXIMUM_HOST_CONTROLS_BANS][HOST_CONTROLS_BAN_LINE_SIZE];
	short ban_count;
	unsigned long bans_time;
	boolean bans_read;
	/* the buttons' actions shown (NONE: hidden) */
	short buttons[HOST_CONTROLS_ACTIONS];
	/* each row's team (NONE: none), for its colour */
	short row_teams[HOST_CONTROLS_ROWS];
	/* the opening's player, to give the focus to once its row is shown;
	the first row, once shown (the screen opened, a tab chosen) */
	boolean focus_chosen;
	boolean focus_first;
	/* the tab the strip last showed */
	short tab_shown;
	/* (an admin's) when the server's lists were last asked for */
	unsigned long lists_time;
} host_menu;

/* a Host Controls row's index (0 up), else NONE */
static short host_row_index(struct widget_instance const *row)
{
	return row && !strncmp(row->name, "host_item_", 10) && row->name[10] >= '1' && row->name[10] <= '9' ?
		(short)(atoi(row->name + 10) - 1) : NONE;
}

static short host_action_index(struct widget_instance const *button)
{
	return button && !strncmp(button->name, "host_action_", 12) ? (short)atoi(button->name + 12) : NONE;
}

/* the rows the list has (tools/port_settings.py's HOST_ROWS on the
computer, its touchscreen's fewer and taller: TOUCH_HOST_ROWS), at most
HOST_CONTROLS_ROWS */
static short host_rows_of(struct widget_instance *list)
{
	struct widget_instance *child;
	short rows = 0;

	for (child = list->child; child; child = child->next)
	{
		if (host_row_index(child) != NONE && rows < HOST_CONTROLS_ROWS)
			rows++;
	}
	return rows ? rows : HOST_CONTROLS_ROWS;
}

static void host_focus(struct widget_instance *list, struct widget_instance *child)
{
	struct widget_instance *each;
	short index = 0;

	for (each = list->child; each; each = each->next, index++)
	{
		if (each == child)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
			return;
		}
	}
}

/* ui_widget.c's colours (pc_menu_text_color): a player's team in its own */
static boolean host_controls_text_color(struct widget_instance *widget, real_argb_color *color)
{
	short row = widget->parent ? host_row_index(widget->parent) : NONE;
	unsigned long rgba;

	if (row < 0 || row >= HOST_CONTROLS_ROWS || strcmp(widget->name, "host_item_team") ||
		host_menu.row_teams[row] == NONE)
	{
		return FALSE;
	}
	rgba = host_menu.row_teams[row] ? HOST_CONTROLS_BLUE : HOST_CONTROLS_RED;
	color->red = (real)((rgba >> 24) & 0xFF) / 255.0f;
	color->green = (real)((rgba >> 16) & 0xFF) / 255.0f;
	color->blue = (real)((rgba >> 8) & 0xFF) / 255.0f;
	return TRUE;
}

static void host_notice(wchar_t const *format, wchar_t const *name)
{
	usnprintf(host_menu.notice, NUMBEROF(host_menu.notice) - 1, format, name ? name : L"");
	host_menu.notice[NUMBEROF(host_menu.notice) - 1] = 0;
	host_menu.notice_time = system_milliseconds();
}

/* what was done, said as it is (a reason why not, from the host's code) */
static void host_notice_text(char const *text)
{
	wchar_t wide[HOST_CONTROLS_TEXT_LENGTH];

	text_to_wide(text, wide, NUMBEROF(wide));
	if (wide[0] >= 'a' && wide[0] <= 'z')
		wide[0] = (wchar_t)(wide[0] - 'a' + 'A');
	host_notice(L"%s.", wide);
}

static boolean host_confirming(short confirm)
{
	return host_menu.confirm == confirm && confirm != _host_confirm_none &&
		system_milliseconds() - host_menu.confirm_time < HOST_CONTROLS_CONFIRM_TIME;
}

/* a press that is done on the second: TRUE once confirmed */
static boolean host_confirmed(short confirm)
{
	if (host_confirming(confirm))
	{
		host_menu.confirm = _host_confirm_none;
		return TRUE;
	}
	host_menu.confirm = confirm;
	host_menu.confirm_time = system_milliseconds();
	ui_play_audio_feedback_sound(SOUND_CURSOR);
	return FALSE;
}

static wchar_t const *host_team_name(short team)
{
	return team == 0 ? L"Red" : team == 1 ? L"Blue" : L"";
}

static wchar_t const *host_tag_name(short tag)
{
	switch (tag)
	{
	case _host_controls_tag_host:
		return L"HOST";
	case _host_controls_tag_bot:
		return L"BOT";
	case _host_controls_tag_openh1:
		return L"OPENH1";
	case _host_controls_tag_opence:
		return L"OPENCE";
	default:
		return L"";
	}
}

/* whether the entry is the player chosen (a game's by datum, a lobby's by
slot) */
static boolean host_player_chosen(struct host_controls_player const *player)
{
	return network_host_controls_same_player(player, host_menu.chosen_player, host_menu.chosen_slot,
		host_menu.chosen_machine, host_menu.chosen_name);
}

/* the player chosen, as the entry is */
static void host_player_choose(struct host_controls_player const *player)
{
	host_menu.chosen_player = player ? player->player_index : NONE;
	host_menu.chosen_slot = player ? player->slot : NONE;
	host_menu.chosen_machine = player ? player->machine_index : NONE;
	csmemset(host_menu.chosen_name, 0, sizeof(host_menu.chosen_name));
	if (player)
		csmemcpy(host_menu.chosen_name, player->name, sizeof(host_menu.chosen_name));
}

/* (the chosen player's entry, else NULL) */
static struct host_controls_player *host_chosen_player(void)
{
	short index;

	for (index = 0; index < host_menu.player_count; index++)
	{
		if (host_player_chosen(&host_menu.players[index]))
			return &host_menu.players[index];
	}
	return NULL;
}

/* ---- an admin's Host Controls (a dedicated server's: network_admin.c),
whose game is the server's as it tells this machine, and whose changes are
the server's commands, asked for */

static boolean host_remote(void)
{
	return network_admin_remote();
}

/* whether this machine has Host Controls: the host's, or an admin's */
static boolean host_available(void)
{
	return network_host_controls_available() || host_remote();
}

static boolean host_flag(short flag)
{
	struct network_admin_state state;

	return network_admin_remote_state(&state) && TEST_FLAG(state.flags, flag);
}

/* whether the game is in its lobby */
static boolean host_lobby(void)
{
	return host_remote() ? host_flag(_network_admin_flag_lobby) : network_host_controls_in_lobby();
}

/* a request to the server (its answer, if it refuses, in the panel) */
static boolean host_ask(short verb, struct host_controls_player const *player, long number, char const *text)
{
	if (!network_admin_send(verb, player ? player->slot : NONE, player ? player->name : NULL, number, text))
	{
		host_notice(L"The server can't be asked now.", NULL);
		return FALSE;
	}
	return TRUE;
}

static struct server_rotation const *host_playlist(void)
{
	return host_remote() ? network_admin_rotation() : host_playlist();
}

static long host_playlist_index(void)
{
	struct network_admin_state state;

	if (network_admin_remote_state(&state))
		return state.rotation_index;
	return host_playlist_index();
}

static short host_gametypes(struct host_game_gametype const **gametypes)
{
	return host_remote() ? network_admin_gametypes(gametypes) : network_host_game_gametypes(gametypes);
}

/* an entry's text read (an admin's: the server's current or next game) */
static boolean host_entry_read(char const *text, struct server_entry *entry)
{
	csmemset(entry, 0, sizeof(*entry));
	if (!text[0] || !server_rotation_parse_entry(text, (int)strlen(text), entry))
		return FALSE;
	if (!entry->map_name[0])
		csstrncpy(entry->map_name, entry->map, sizeof(entry->map_name) - 1);
	return TRUE;
}

/* the next game: the host's change asked for, or the server's next */
static boolean host_next(struct server_entry *entry, boolean *now)
{
	struct network_admin_state state;

	if (network_admin_remote_state(&state))
	{
		*now = FALSE;
		return host_entry_read(state.next, entry);
	}
	return network_host_game_next(entry, now);
}

/* what is done to a player, the host's way or asked of the server */
static boolean host_kick(struct host_controls_player const *player, boolean ban, long seconds, char const *reason)
{
	if (host_remote())
		return host_ask(ban ? _network_admin_ban : _network_admin_kick, player, seconds, reason);
	return network_host_game_kick_player(player, ban, seconds, reason);
}

static boolean host_mute(struct host_controls_player const *player, short minutes)
{
	if (host_remote())
		return host_ask(minutes ? _network_admin_mute : _network_admin_unmute, player, minutes, NULL);
	return network_host_game_mute_player(player, minutes);
}

static short host_muted(struct host_controls_player const *player)
{
	if (host_remote())
		return network_admin_player_muted((short)player->slot) ? 1 : 0;
	return network_host_game_player_mute_minutes(player);
}

static boolean host_move(struct host_controls_player const *player)
{
	if (host_remote())
		return host_ask(_network_admin_move_team, player, 0, NULL);
	return network_host_game_move_player(player);
}

/* ---- the game's rows */

/* the game this machine hosts (its record) */
static struct network_game *host_game(void)
{
	void *server = global_network_game_server_get();

	return server ? network_game_server_get_game(server) : NULL;
}

/* whether the game (the lobby's, or the one played) has teams */
static boolean host_game_teams(void)
{
	struct network_game *game = host_game();

	if (host_remote())
		return host_flag(_network_admin_flag_teams);
	if (network_host_controls_in_lobby())
		return game && game->variant.universal_variant.teams;
	return game_engine_running() && game_engine_has_teams();
}

/* the rows Game shows, in their order */
static void host_game_rows_read(void)
{
	boolean teams = host_game_teams();
	boolean playing = game_engine_running() && !host_lobby();
	short index;

	host_menu.game_row_count = 0;
	/* (an admin's: what the server's commands do) */
	if (host_remote())
	{
		boolean remote_playing = host_flag(_network_admin_flag_playing);

		for (index = 0; index < NUMBER_OF_HOST_GAME_ROWS; index++)
		{
			boolean shown = FALSE;

			switch (index)
			{
			case _host_game_end:
			case _host_game_restart:
			case _host_game_announce:
				shown = remote_playing;
				break;
			case _host_game_change_map:
				shown = TRUE;
				break;
			case _host_game_shuffle_teams:
			case _host_game_balance_teams:
				shown = remote_playing && teams && host_flag(_network_admin_flag_team_moves);
				break;
			case _host_game_lock_teams:
				shown = teams;
				break;
			default:
				break;
			}
			if (shown)
				host_menu.game_rows[host_menu.game_row_count++] = index;
		}
		return;
	}
	for (index = 0; index < NUMBER_OF_HOST_GAME_ROWS; index++)
	{
		boolean shown = TRUE;

		switch (index)
		{
		case _host_game_end:
		case _host_game_restart:
			shown = playing && game_engine_can_score() && !network_host_controls_restarting();
			break;
		case _host_game_change_map:
			shown = network_host_game_hosting();
			break;
		case _host_game_change_rules:
			shown = network_host_rules_available();
			break;
		case _host_game_shuffle_teams:
		case _host_game_balance_teams:
			shown = teams && network_host_game_teams_arrangeable();
			break;
		case _host_game_announce:
			shown = playing && game_engine_can_score();
			break;
		case _host_game_score_limit:
		case _host_game_time_limit:
			shown = network_host_game_hosting() && host_game() && host_game()->variant.game_engine_index;
			break;
		case _host_game_lock_teams:
			shown = teams;
			break;
		case _host_game_password:
		case _host_game_listing:
			shown = multiplayer.mode == _multiplayer_mode_host_internet && config_boolean("network.online") &&
				config_boolean("network.public_lobby") &&
				(index == _host_game_listing || !server_settings_private());
			break;
		case _host_game_opence:
			shown = network_standard_hosting();
			break;
		default:
			break;
		}
		if (shown)
			host_menu.game_rows[host_menu.game_row_count++] = index;
	}
}

/* the score to win and the minutes of the lobby's game, or the ones that
wait for the next game's (waiting: whether they do) */
static void host_game_limits(long *score, short *minutes, boolean *waiting)
{
	struct network_game *game = host_game();
	long score_waiting;
	short minutes_waiting;

	*score = game ? game->variant.universal_variant.score_to_win : 0;
	*minutes = game ? game->variant_options.time_limit : 0;
	*waiting = network_host_game_limits_waiting(&score_waiting, &minutes_waiting);
	if (score_waiting != NONE)
		*score = score_waiting;
	if (minutes_waiting != NONE)
		*minutes = minutes_waiting;
}

/* a game's row's label, value and what it does */
static void host_game_row_text(short row, wchar_t *label, wchar_t *value, wchar_t *help, short size)
{
	struct network_game *game = host_game();
	boolean confirming = host_confirming((short)(row + MAXIMUM_HOST_GAME_ROWS));
	boolean lobby = host_lobby();
	long score;
	short minutes;
	boolean waiting;

	label[0] = value[0] = help[0] = 0;
	host_game_limits(&score, &minutes, &waiting);
	switch (row)
	{
	case _host_game_end:
		usnprintf(label, size - 1, L"END GAME NOW");
		usnprintf(value, size - 1, confirming ? L"PRESS AGAIN" : L"");
		usnprintf(help, size - 1, L"Ends the game now, as its time limit does: the scores, then the lobby, "
			L"with the playlist's next game if there is one.");
		break;
	case _host_game_restart:
		usnprintf(label, size - 1, L"RESTART ROUND");
		usnprintf(value, size - 1, confirming ? L"PRESS AGAIN" : L"");
		usnprintf(help, size - 1, L"Ends the game and starts it again on this map and gametype, after a moment "
			L"in the lobby. Scores start over.");
		break;
	case _host_game_change_map:
	{
		struct server_entry next;
		boolean now;

		usnprintf(label, size - 1, L"CHANGE MAP...");
		if (host_next(&next, &now))
			network_host_game_entry_shown(&next, value, size);
		usnprintf(help, size - 1, lobby ? L"Pick the map and gametype this lobby plays, or build a playlist." :
			L"Pick the next map and gametype: now, or after this game. Or build a playlist.");
		break;
	}
	case _host_game_change_rules:
		usnprintf(label, size - 1, L"CHANGE RULES...");
		usnprintf(help, size - 1, lobby ? L"The vehicles, starting weapons, shields and more of the lobby's game." :
			L"The vehicles, starting weapons, shields and more, while the game plays. Or save them as a gametype.");
		break;
	case _host_game_shuffle_teams:
	case _host_game_balance_teams:
		usnprintf(label, size - 1, row == _host_game_shuffle_teams ? L"SHUFFLE TEAMS" : L"BALANCE TEAMS");
		usnprintf(value, size - 1, confirming ? L"PRESS AGAIN" : L"");
		if (row == _host_game_shuffle_teams)
			usnprintf(help, size - 1, lobby ? L"Puts the players on teams at random, as many on each." :
				L"Puts the players on teams at random, as many on each. Those who move die and respawn.");
		else
			usnprintf(help, size - 1, lobby ? L"Evens the teams' numbers, moving as few players as it can." :
				L"Evens the teams' numbers, and their scores. Those who move die and respawn.");
		break;
	case _host_game_announce:
		usnprintf(label, size - 1, L"ANNOUNCE...");
		usnprintf(help, size - 1, L"Type a message every player sees, as a server notice. Enter sends it.");
		break;
	case _host_game_score_limit:
		usnprintf(label, size - 1, L"SCORE LIMIT");
		usnprintf(value, size - 1, waiting && !lobby ? L"%ld NEXT GAME" : L"%ld", score);
		usnprintf(help, size - 1, lobby ? L"The score to win. Changes the lobby's game now." :
			L"The score to win, from the next game: the game being played keeps its own.");
		break;
	case _host_game_time_limit:
		usnprintf(label, size - 1, L"TIME LIMIT");
		if (minutes > 0)
			usnprintf(value, size - 1, waiting && !lobby ? L"%d MIN NEXT GAME" : L"%d MIN", minutes);
		else
			usnprintf(value, size - 1, waiting && !lobby ? L"NONE NEXT GAME" : L"NONE");
		usnprintf(help, size - 1, lobby ? L"The game's length. Changes the lobby's game now." :
			L"The game's length, from the next game: the game being played keeps its own.");
		break;
	case _host_game_password:
	{
		short index;

		usnprintf(label, size - 1, L"PASSWORD");
		for (index = 0; multiplayer.game_password[index] && index < size - 1 && index < 16; index++)
			value[index] = L'*';
		value[index] = 0;
		if (!index)
			usnprintf(value, size - 1, L"NONE");
		usnprintf(help, size - 1, L"Players from the server browser need it to join. Changes at once; those "
			L"in the game stay.");
		break;
	}
	case _host_game_lock_teams:
		usnprintf(label, size - 1, L"LOCK TEAMS");
		usnprintf(value, size - 1, (host_remote() ? host_flag(_network_admin_flag_teams_locked) :
			network_host_controls_teams_locked()) ? L"ON" : L"OFF");
		usnprintf(help, size - 1, L"Players can't switch teams in the lobby. You can still move anyone, "
			L"and yourself.");
		break;
	case _host_game_votekick:
		usnprintf(label, size - 1, L"ALLOW VOTEKICK");
		usnprintf(value, size - 1, config_boolean("network.votekick") ? L"ON" : L"OFF");
		usnprintf(help, size - 1, L"Players may vote to kick a player, from the scoreboard. You can always "
			L"kick.");
		break;
	case _host_game_listing:
		usnprintf(label, size - 1, L"LISTING");
		usnprintf(value, size - 1, server_settings_private() ? L"PRIVATE" : L"PUBLIC");
		usnprintf(help, size - 1, server_settings_private() ? L"Only players with your invite link can join." :
			L"Listed in everyone's server browser.");
		break;
	case _host_game_maximum_players:
		usnprintf(label, size - 1, L"MAX PLAYERS");
		usnprintf(value, size - 1, L"%d", game ? (int)game->maximum_players : 0);
		usnprintf(help, size - 1, L"The most players who can be in the game (%d are in it now).",
			game ? (int)game->player_count : 0);
		break;
	case _host_game_opence:
		usnprintf(label, size - 1, L"ALLOW OPENCE PLAYERS");
		usnprintf(value, size - 1, network_host_controls_opence_joins() ? L"ON" : L"OFF");
		usnprintf(help, size - 1, L"Players of OpenCE may join from now on, playing at 30 ticks. Those in "
			L"the game stay.");
		break;
	default:
		break;
	}
	label[size - 1] = value[size - 1] = help[size - 1] = 0;
}

/* the next value of a list past the current, either way (none: the
current) */
static long host_step_value(long const *values, short count, long current, short step)
{
	short index;

	if (step > 0)
	{
		for (index = 0; index < count; index++)
		{
			if (values[index] > current)
				return values[index];
		}
	}
	else
	{
		for (index = (short)(count - 1); index >= 0; index--)
		{
			if (values[index] < current)
				return values[index];
		}
	}
	return current;
}

/* the widget of a game's row shown on the list (NULL: none) */
static struct widget_instance *host_game_row_widget(struct widget_instance *list, short row)
{
	short index;

	for (index = 0; index < host_menu.game_row_count; index++)
	{
		if (host_menu.game_rows[index] == row && index >= host_menu.first &&
			index < host_menu.first + host_rows_of(list))
		{
			char name[24];

			snprintf(name, sizeof(name), "host_item_%d", index - host_menu.first + 1);
			return named(list, name, 0);
		}
	}
	return NULL;
}

/* (the typing's ends) the password, at once; an announcement, sent */
static void host_password_done(char const *text)
{
	snprintf(multiplayer.game_password, sizeof(multiplayer.game_password), "%s", text);
	p2p_set_hosting_password(multiplayer.mode == _multiplayer_mode_host_internet && !server_settings_private() &&
		multiplayer.game_password[0] ? multiplayer.game_password : NULL);
	host_notice(multiplayer.game_password[0] ? L"The password is changed." : L"The game has no password.", NULL);
}

static void host_announce_done(char const *text)
{
	if (!text[0])
		return;
	if (host_remote())
	{
		if (host_ask(_network_admin_announce, NULL, 0, text))
			host_notice(L"Everyone sees your message.", NULL);
		return;
	}
	if (network_host_game_announce(text))
		host_notice(L"Everyone sees your message.", NULL);
	else
		host_notice(L"Nobody else sees a message now.", NULL);
}

static void host_reason_done(char const *text)
{
	snprintf(host_menu.reason, sizeof(host_menu.reason), "%s", text);
}

/* (RULES' SAVE AS GAMETYPE's typing's end) the rules saved by that name */
static void host_rules_save_done(char const *text)
{
	char reason[96];

	if (!text[0])
		return;
	if (network_host_rules_save(text, reason, sizeof(reason)))
		host_notice(L"Saved: MAPS and the playlist offer it.", NULL);
	else
		host_notice_text(reason);
}

/* the teams shuffled or balanced (_host_game_*), pressed twice in a game
(those moved die): said in the panel */
static void host_teams_arrange(short how, short confirm)
{
	short moved;

	if (!host_lobby() && !host_confirmed(confirm))
		return;
	if (host_remote())
	{
		if (host_ask(how == _host_game_shuffle ? _network_admin_shuffle_teams : _network_admin_balance_teams, NULL, 0,
			NULL))
		{
			host_notice(how == _host_game_shuffle ? L"The server shuffles the teams." :
				L"The server balances the teams.", NULL);
			ui_play_audio_feedback_sound(SOUND_FORWARD);
		}
		return;
	}
	moved = network_host_game_teams_arrange(how);
	if (moved == NONE)
	{
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return;
	}
	if (moved == 0)
		host_notice(L"The teams are even already.", NULL);
	else
	{
		wchar_t count[16];

		usnprintf(count, NUMBEROF(count) - 1, L"%d", moved);
		count[NUMBEROF(count) - 1] = 0;
		host_notice(moved == 1 ? L"%s player moved." : L"%s players moved.", count);
	}
	ui_play_audio_feedback_sound(SOUND_FORWARD);
}

/* a game's row stepped (left -1, right 1) or chosen (A: 0) */
static void host_game_row_change(struct widget_instance *row_widget, short row, short step)
{
	struct network_game *game = host_game();

	switch (row)
	{
	case _host_game_end:
	case _host_game_restart:
		if (step || !host_confirmed((short)(row + MAXIMUM_HOST_GAME_ROWS)))
			return;
		if (host_remote() ? host_ask(row == _host_game_end ? _network_admin_end_game : _network_admin_restart, NULL, 0,
			NULL) : row == _host_game_end ? network_host_controls_end_game() : network_host_controls_restart())
		{
			ui_play_audio_feedback_sound(SOUND_FORWARD);
			/* (back to the game, which shows its scores) */
			ui_widgets_close_all();
		}
		else
			ui_play_audio_feedback_sound(SOUND_ERROR);
		return;
	case _host_game_change_map:
	case _host_game_change_rules:
		if (step)
			return;
		host_menu.tab = row == _host_game_change_map ? _host_tab_maps : _host_tab_rules;
		host_menu.gametype = NONE;
		host_menu.first = 0;
		host_menu.confirm = _host_confirm_none;
		host_menu.focus_first = TRUE;
		break;
	case _host_game_shuffle_teams:
	case _host_game_balance_teams:
		if (!step)
		{
			host_teams_arrange(row == _host_game_shuffle_teams ? _host_game_shuffle : _host_game_balance,
				(short)(row + MAXIMUM_HOST_GAME_ROWS));
		}
		return;
	case _host_game_announce:
	case _host_game_password:
		if (step || !row_widget)
			return;
		if (text_field_editing(row_widget))
		{
			text_field_end(TRUE);
			ui_play_audio_feedback_sound(SOUND_FORWARD);
			return;
		}
		if (row == _host_game_announce)
			text_field_begin(row_widget, "", HOST_CONTROLS_ANNOUNCE_LENGTH, host_announce_done);
		else
			text_field_begin_masked(row_widget, multiplayer.game_password, PASSWORD_LENGTH, host_password_done);
		break;
	case _host_game_score_limit:
	case _host_game_time_limit:
	{
		long score, chosen;
		short minutes;
		boolean waiting;

		host_game_limits(&score, &minutes, &waiting);
		if (row == _host_game_score_limit)
		{
			chosen = host_step_value(host_score_steps, NUMBEROF(host_score_steps), score, (short)(step ? step : 1));
			if (chosen == score || !network_host_game_set_limits(chosen, NONE))
			{
				ui_play_audio_feedback_sound(SOUND_ERROR);
				return;
			}
		}
		else
		{
			long values[NUMBEROF(host_time_steps)];
			short index;

			for (index = 0; index < NUMBEROF(host_time_steps); index++)
				values[index] = host_time_steps[index];
			chosen = host_step_value(values, NUMBEROF(values), minutes, (short)(step ? step : 1));
			if (chosen == minutes || !network_host_game_set_limits(NONE, (short)chosen))
			{
				ui_play_audio_feedback_sound(SOUND_ERROR);
				return;
			}
		}
		break;
	}
	case _host_game_lock_teams:
		if (host_remote())
			host_ask(_network_admin_lock_teams, NULL, !host_flag(_network_admin_flag_teams_locked), NULL);
		else
			network_host_controls_lock_teams(!network_host_controls_teams_locked());
		break;
	case _host_game_votekick:
		if (!config_write("network.votekick", config_boolean("network.votekick") ? "false" : "true"))
			platform_log("menus: could not write network.votekick to config.toml");
		break;
	case _host_game_listing:
		server_settings_private_set(!server_settings_private());
		p2p_set_hosting_public(!server_settings_private());
		break;
	case _host_game_maximum_players:
	{
		short values[NUMBEROF(maximum_players)];
		short index, chosen;

		if (!game)
			return;
		for (index = 0; index < NUMBEROF(maximum_players); index++)
			values[index] = maximum_players[index];
		chosen = network_host_controls_maximum_step(values, NUMBEROF(values), game->maximum_players,
			game->player_count, (short)(step ? step : 1));
		if (chosen == game->maximum_players)
		{
			ui_play_audio_feedback_sound(SOUND_ERROR);
			return;
		}
		network_game_server_port_set_settings(game->name, chosen);
		/* (and Server Setup's, for the next game hosted) */
		for (index = 0; index < NUMBEROF(maximum_players); index++)
		{
			if (maximum_players[index] == chosen)
				*server_settings_maximum_players_index() = index;
		}
		break;
	}
	case _host_game_opence:
		network_host_controls_set_opence_joins(!network_host_controls_opence_joins());
		break;
	default:
		break;
	}
	ui_play_audio_feedback_sound(SOUND_CURSOR);
}

/* ---- the bots */

/* a bots' row stepped (left -1, right 1) or chosen (A: 0): its setting
written (network_host_controls.c), or the teams balanced */
static void host_bot_row_change(short row, short step)
{
	if (row == _host_bot_row_balance)
	{
		if (!step)
			host_teams_arrange(_host_game_balance, (short)(row + HOST_CONFIRM_BOT_ROW));
		return;
	}
	if (!network_host_controls_bot_row_step(row, (short)(step ? step : 1)))
	{
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return;
	}
	ui_play_audio_feedback_sound(SOUND_CURSOR);
}

/* ---- the rules */

/* a rule's row stepped (left -1, right 1) or chosen (A: 0): the rule
changed (network_host_rules.c), or SAVE AS GAMETYPE's name typed */
static void host_rule_change(struct widget_instance *row_widget, short rule, short step)
{
	if (rule == _host_rule_save)
	{
		if (step || !row_widget)
			return;
		if (text_field_editing(row_widget))
		{
			text_field_end(TRUE);
			ui_play_audio_feedback_sound(SOUND_FORWARD);
			return;
		}
		text_field_begin(row_widget, "", HOST_RULES_NAME_LENGTH, host_rules_save_done);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return;
	}
	if (!network_host_rules_step(rule, (short)(step ? step : 1)))
	{
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return;
	}
	if (network_host_rules_waits(rule))
		host_notice(L"It changes from the next game.", NULL);
	ui_play_audio_feedback_sound(SOUND_CURSOR);
}

/* whether the bot is the one chosen */
static boolean host_bot_chosen(struct host_controls_player const *bot)
{
	return host_menu.chosen_bot_valid && network_host_controls_same_player(bot, host_menu.chosen_bot.player_index,
		host_menu.chosen_bot.slot, host_menu.chosen_bot.machine_index, host_menu.chosen_bot.name);
}

/* (the chosen bot's entry, else NULL) */
static struct host_controls_player *host_chosen_bot(void)
{
	short index;

	for (index = 0; index < host_menu.bot_count; index++)
	{
		if (host_bot_chosen(&host_menu.bots[index]))
			return &host_menu.bots[index];
	}
	return NULL;
}

/* ---- the maps and the playlist */

/* the gametype the maps play (its rotation name; NULL: CURRENT, the
game's own) */
static struct host_game_gametype const *host_gametype(void)
{
	struct host_game_gametype const *gametypes;
	short count = host_gametypes(&gametypes);

	if (!count || host_menu.gametype == NONE)
	{
		host_menu.gametype = NONE;
		return NULL;
	}
	host_menu.gametype = (short)PIN(host_menu.gametype, 0, count - 1);
	return &gametypes[host_menu.gametype];
}

/* the gametype stepped (left -1, right 1): CURRENT, then the list's */
static void host_gametype_step(short step)
{
	struct host_game_gametype const *gametypes;
	short count = host_gametypes(&gametypes);

	host_gametype();
	host_menu.gametype = (short)((host_menu.gametype + 1 + step + count + 1) % (count + 1) - 1);
}

/* the game's own gametype as shown (its variant's name) */
static void host_current_gametype(wchar_t *text, short size)
{
	struct network_game *game = host_game();
	short index;

	/* (an admin's: the server's game's) */
	if (host_remote())
	{
		static struct server_entry entry;
		struct network_admin_state state;

		text[0] = 0;
		if (network_admin_remote_state(&state) && host_entry_read(state.current, &entry))
			network_host_game_gametype_text(entry.gametype, text, size);
		return;
	}
	for (index = 0; game && index < size - 1 &&
		index < NUMBEROF(game->variant.human_readable_game_description) &&
		game->variant.human_readable_game_description[index]; index++)
	{
		text[index] = game->variant.human_readable_game_description[index];
	}
	text[index] = 0;
}

/* the gametype as MAPS shows it ("CURRENT \x00B7 CTF", or the list's) */
static void host_gametype_shown(wchar_t *text, short size)
{
	struct host_game_gametype const *gametype = host_gametype();
	wchar_t current[HOST_GAME_GAMETYPE_SHOWN_SIZE];

	if (gametype)
		usnprintf(text, size - 1, L"%s", gametype->shown);
	else
	{
		host_current_gametype(current, NUMBEROF(current));
		usnprintf(text, size - 1, L"CURRENT \x00B7 %s", current);
	}
	text[size - 1] = 0;
}

/* the map chosen with the gametype, as an entry's text: CURRENT's the map
alone (the game's gametype, score, time and rules kept: network_host_game_
change), but a playlist's names the list's gametype the game's is (FALSE:
none is) */
static boolean host_map_entry_text(struct host_game_map const *map, boolean playlist, char *text, short size)
{
	struct host_game_gametype const *gametype = host_gametype();

	if (!map)
		return FALSE;
	if (!gametype && !playlist)
	{
		snprintf(text, size, "%s%s", map->custom ? "custom_maps\\" : "", map->name);
		return TRUE;
	}
	if (!gametype)
	{
		struct host_game_gametype const *gametypes;
		short count = host_gametypes(&gametypes);
		wchar_t current[HOST_GAME_GAMETYPE_SHOWN_SIZE];
		short index;

		host_current_gametype(current, NUMBEROF(current));
		index = network_host_game_gametype_find(gametypes, count, current);
		if (index == NONE)
			return FALSE;
		gametype = &gametypes[index];
	}
	snprintf(text, size, "%s%s:%s", map->custom ? "custom_maps\\" : "", map->name, gametype->name);
	return TRUE;
}

/* the map chosen (NULL: none) */
static struct host_game_map const *host_chosen_map(void)
{
	short index;

	for (index = 0; index < host_menu.map_count; index++)
	{
		if (!strcmp(host_menu.maps[index].level, host_menu.chosen_map))
			return &host_menu.maps[index];
	}
	return NULL;
}

/* whether the game (lobby's or played) is the map's */
static boolean host_map_current(struct host_game_map const *map)
{
	struct network_game *game = host_game();

	if (host_remote())
	{
		static struct server_entry entry;
		struct network_admin_state state;

		return network_admin_remote_state(&state) && host_entry_read(state.current, &entry) &&
			!_stricmp(entry.map, map->name);
	}
	return game && !_stricmp(game->map.name, map->level);
}

/* (the playlist's tab's entries: a change asked for, then the playlist's)
the change asked for (TRUE), the one network_host_game_next gives when
it is no playlist's next */
static boolean host_change_asked(struct server_entry *entry)
{
	struct server_rotation const *playlist = host_playlist();
	long next = network_host_game_playlist_after(playlist, host_playlist_index());
	boolean now;

	/* (an admin's: the server's rotation alone, its next marked) */
	if (host_remote())
		return FALSE;
	if (!network_host_game_next(entry, &now))
		return FALSE;
	return next == NONE || !entry->gametype[0] || strcmp(entry->text, playlist->entries[next].text) || now;
}

static short host_playlist_entries(void)
{
	struct server_entry change;

	return (short)(host_playlist()->count + (host_change_asked(&change) ? 1 : 0));
}

/* the playlist's entry of the tab's (NONE, or HOST_PLAYLIST_CHANGE) */
static short host_playlist_entry(short entry)
{
	struct server_entry change;
	boolean asked = host_change_asked(&change);

	if (asked && entry == 0)
		return HOST_PLAYLIST_CHANGE;
	return (short)(entry - (asked ? 1 : 0));
}

/* ---- the screen */

static short host_entry_count(void)
{
	switch (host_menu.tab)
	{
	case _host_tab_players:
		return host_menu.player_count;
	case _host_tab_game:
		return host_menu.game_row_count;
	case _host_tab_maps:
		return (short)(host_menu.map_count + 1);
	case _host_tab_playlist:
		return host_playlist_entries();
	case _host_tab_bots:
		return (short)(host_menu.bot_row_count + host_menu.bot_count);
	case _host_tab_rules:
		return host_menu.rule_row_count;
	default:
		return host_menu.ban_count;
	}
}

/* the entry of the row (NONE: none) */
static short host_row_entry(short row)
{
	short entry = (short)(host_menu.first + row);

	return row != NONE && entry < host_entry_count() ? entry : NONE;
}

static void host_tab_set(short tab)
{
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	/* (RULES, GAME's own; the host's of a gametype, else GAME) */
	if (tab == _host_tab_rules)
		tab = network_host_rules_available() ? _host_tab_rules : _host_tab_game;
	else
		tab = (short)((tab + NUMBER_OF_HOST_TABS) % NUMBER_OF_HOST_TABS);
	/* (BOTS, not the host's in co-op: PLAYERS) */
	if (tab == _host_tab_bots && !network_host_controls_bots_tab())
		tab = _host_tab_players;
	/* (MAPS: CURRENT, the game's own gametype) */
	if (tab == _host_tab_maps && host_menu.tab != _host_tab_maps)
		host_menu.gametype = NONE;
	host_menu.tab = tab;
	host_menu.first = 0;
	host_menu.confirm = _host_confirm_none;
	host_menu.choosing = NONE;
	host_menu.bans_read = FALSE;
	host_menu.focus_first = TRUE;
	host_menu.lists_time = 0;
}

/* "port host init" (the list made): Players, nothing chosen */
static boolean host_controls_initialize(void)
{
	host_menu.tab = _host_tab_players;
	host_menu.first = 0;
	host_player_choose(NULL);
	host_menu.chosen_game_row = NONE;
	host_menu.chosen_map[0] = 0;
	host_menu.gametype = NONE;
	host_menu.chosen_entry = NONE;
	host_menu.chosen_ban[0] = 0;
	host_menu.chosen_bot_row = NONE;
	host_menu.chosen_bot_valid = FALSE;
	host_menu.chosen_rule = NONE;
	host_menu.choosing = NONE;
	host_menu.reason[0] = 0;
	host_menu.confirm = _host_confirm_none;
	host_menu.notice_time = 0;
	host_menu.bans_read = FALSE;
	host_menu.focus_chosen = FALSE;
	host_menu.focus_first = TRUE;
	host_menu.tab_shown = NONE;
	host_menu.lists_time = 0;
	return TRUE;
}

/* "port host open" (the pause menu's and the lobby's HOST CONTROLS): the
host's alone */
static boolean host_controls_open(void)
{
	if (!host_available())
	{
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return FALSE;
	}
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port host back" (B, Escape): a typing let go of, as it was, staying;
else the screen left */
static boolean host_controls_back(void)
{
	if (text_field_editing(NULL))
	{
		text_field_end(FALSE);
		return FALSE;
	}
	if (host_menu.choosing != NONE)
	{
		host_menu.choosing = NONE;
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return FALSE;
	}
	/* (RULES: back to GAME) */
	if (host_menu.tab == _host_tab_rules)
	{
		host_tab_set(_host_tab_game);
		ui_play_audio_feedback_sound(SOUND_BACK);
		return FALSE;
	}
	return TRUE;
}

/* the entries of the tab shown, read anew */
static void host_entries_read(void)
{
	unsigned long now = system_milliseconds();

	/* (an admin's: the server's, as it last told this machine; its lists
	asked for as a tab that shows one opens, and every few seconds) */
	if (host_remote())
	{
		host_menu.player_count = network_admin_players(host_menu.players, NUMBEROF(host_menu.players));
		host_game_rows_read();
		host_menu.bot_row_count = 0;
		host_menu.bot_count = 0;
		host_menu.rule_row_count = 0;
		host_menu.map_count = network_admin_maps(&host_menu.maps);
		host_menu.ban_count = network_admin_bans(host_menu.bans, host_menu.ban_lines, MAXIMUM_HOST_CONTROLS_BANS);
		if ((host_menu.tab == _host_tab_maps || host_menu.tab == _host_tab_playlist || host_menu.tab == _host_tab_bans ||
			host_menu.tab == _host_tab_game) && (!host_menu.lists_time || now - host_menu.lists_time >= 3000))
		{
			network_admin_send(_network_admin_lists, NONE, NULL, 0, NULL);
			host_menu.lists_time = now ? now : 1;
		}
		return;
	}
	host_menu.player_count = network_host_controls_players(host_menu.players, NUMBEROF(host_menu.players));
	host_game_rows_read();
	host_menu.bot_row_count = network_host_controls_bot_rows(host_menu.bot_rows, NUMBEROF(host_menu.bot_rows));
	host_menu.bot_count = host_menu.tab == _host_tab_bots ?
		network_host_controls_bots(host_menu.bots, NUMBEROF(host_menu.bots)) : 0;
	host_menu.rule_row_count = host_menu.tab == _host_tab_rules ?
		network_host_rules_rows(host_menu.rule_rows, NUMBEROF(host_menu.rule_rows)) : 0;
	if (host_menu.tab == _host_tab_maps)
		host_menu.map_count = network_host_game_maps(&host_menu.maps);
	if (host_menu.tab == _host_tab_bans && (!host_menu.bans_read || now - host_menu.bans_time >= HOST_CONTROLS_BANS_INTERVAL))
	{
		host_menu.ban_count = network_host_controls_bans(host_menu.bans, host_menu.ban_lines,
			MAXIMUM_HOST_CONTROLS_BANS);
		host_menu.bans_time = now;
		host_menu.bans_read = TRUE;
	}
}

/* an entry chosen (the row's: its focus, a click, A) */
static void host_choose(short entry)
{
	if (entry == NONE)
		return;
	switch (host_menu.tab)
	{
	case _host_tab_players:
		if (!host_player_chosen(&host_menu.players[entry]))
		{
			host_menu.confirm = _host_confirm_none;
			host_menu.choosing = NONE;
			host_menu.reason[0] = 0;
		}
		host_player_choose(&host_menu.players[entry]);
		break;
	case _host_tab_game:
		if (host_menu.game_rows[entry] != host_menu.chosen_game_row)
			host_menu.confirm = _host_confirm_none;
		host_menu.chosen_game_row = host_menu.game_rows[entry];
		break;
	case _host_tab_maps:
		/* (the gametype's row chooses no map) */
		if (entry == 0)
		{
			host_menu.chosen_map[0] = 0;
			break;
		}
		if (strcmp(host_menu.maps[entry - 1].level, host_menu.chosen_map))
			host_menu.confirm = _host_confirm_none;
		csstrncpy(host_menu.chosen_map, host_menu.maps[entry - 1].level, sizeof(host_menu.chosen_map) - 1);
		host_menu.chosen_map[sizeof(host_menu.chosen_map) - 1] = 0;
		break;
	case _host_tab_playlist:
		if (host_playlist_entry(entry) != host_menu.chosen_entry)
			host_menu.confirm = _host_confirm_none;
		host_menu.chosen_entry = host_playlist_entry(entry);
		break;
	case _host_tab_bots:
		if (entry < host_menu.bot_row_count)
		{
			if (host_menu.bot_rows[entry] != host_menu.chosen_bot_row)
				host_menu.confirm = _host_confirm_none;
			host_menu.chosen_bot_row = host_menu.bot_rows[entry];
			host_menu.chosen_bot_valid = FALSE;
			break;
		}
		if (!host_bot_chosen(&host_menu.bots[entry - host_menu.bot_row_count]))
			host_menu.confirm = _host_confirm_none;
		host_menu.chosen_bot_row = NONE;
		host_menu.chosen_bot = host_menu.bots[entry - host_menu.bot_row_count];
		host_menu.chosen_bot_valid = TRUE;
		break;
	case _host_tab_rules:
		host_menu.chosen_rule = host_menu.rule_rows[entry];
		break;
	default:
		if (strcmp(host_menu.ban_lines[entry], host_menu.chosen_ban))
			host_menu.confirm = _host_confirm_none;
		csstrncpy(host_menu.chosen_ban, host_menu.ban_lines[entry], sizeof(host_menu.chosen_ban) - 1);
		host_menu.chosen_ban[sizeof(host_menu.chosen_ban) - 1] = 0;
		break;
	}
}

/* the entry chosen's place among the tab's (NONE: none) */
static short host_chosen_entry(void)
{
	short index;

	for (index = 0; index < host_entry_count(); index++)
	{
		if ((host_menu.tab == _host_tab_players && host_player_chosen(&host_menu.players[index])) ||
			(host_menu.tab == _host_tab_game && host_menu.game_rows[index] == host_menu.chosen_game_row) ||
			(host_menu.tab == _host_tab_maps && index > 0 &&
				!strcmp(host_menu.maps[index - 1].level, host_menu.chosen_map)) ||
			(host_menu.tab == _host_tab_playlist && host_playlist_entry(index) == host_menu.chosen_entry) ||
			(host_menu.tab == _host_tab_rules && host_menu.rule_rows[index] == host_menu.chosen_rule) ||
			(host_menu.tab == _host_tab_bots && (index < host_menu.bot_row_count ?
				host_menu.bot_rows[index] == host_menu.chosen_bot_row :
				host_bot_chosen(&host_menu.bots[index - host_menu.bot_row_count]))) ||
			(host_menu.tab == _host_tab_bans && !strcmp(host_menu.ban_lines[index], host_menu.chosen_ban)))
		{
			return index;
		}
	}
	return NONE;
}

/* the column titles of the tab */
static void host_headers_show(struct widget_instance *screen)
{
	static wchar_t const *const headers[NUMBER_OF_HOST_TABS + 1][5] =
	{
		{ L"PLAYER", L"TEAM", L"SCORE", L"PING", L"" },
		{ L"SETTING", L"", L"", L"", L"" },
		{ L"MAP", L"", L"", L"", L"" },
		{ L"MAP", L"", L"", L"", L"GAMETYPE" },
		{ L"BANNED", L"", L"", L"", L"WHEN" },
		{ L"BOTS", L"TEAM", L"SCORE", L"", L"" },
		{ L"RULE", L"", L"", L"", L"" },
	};
	static char const *const names[5] = { "header_label", "header_team", "header_score", "header_ping",
		"header_value" };
	short index;

	for (index = 0; index < 5; index++)
		text_set(named(screen, names[index], 0), headers[host_menu.tab][index]);
}

/* a row's texts (an entry of the tab), its team's colour, its tag's pill */
static void host_row_show(struct widget_instance *row, short row_index, short entry)
{
	wchar_t label[HOST_CONTROLS_TEXT_LENGTH], value[HOST_CONTROLS_TEXT_LENGTH], help[HOST_CONTROLS_TEXT_LENGTH];
	wchar_t team[16] = L"", score[64] = L"", ping[16] = L"";
	struct widget_instance *tag = named(row, "host_item_tag", 0);
	short tag_index = _host_controls_tag_none;
	boolean typed = FALSE;

	label[0] = value[0] = 0;
	host_menu.row_teams[row_index] = NONE;
	switch (host_menu.tab)
	{
	case _host_tab_players:
	{
		struct host_controls_player const *player = &host_menu.players[entry];

		ustrncpy(label, player->name, NUMBEROF(label) - 1);
		label[NUMBEROF(label) - 1] = 0;
		if (host_game_teams() && player->team != NONE)
		{
			usnprintf(team, NUMBEROF(team) - 1, L"%s", host_team_name(player->team));
			host_menu.row_teams[row_index] = (short)PIN(player->team, 0, 1);
		}
		if (game_engine && player->player_index != NONE)
		{
			wchar_t formatted[256];

			formatted[0] = 0;
			game_engine->format_player_score(player->player_index, formatted);
			ustrncpy(score, formatted, NUMBEROF(score) - 1);
			score[NUMBEROF(score) - 1] = 0;
		}
		if (player->tag == _host_controls_tag_host || player->tag == _host_controls_tag_bot)
			usnprintf(ping, NUMBEROF(ping) - 1, L"");
		else if (player->ping == NONE)
			usnprintf(ping, NUMBEROF(ping) - 1, L"-");
		else
			usnprintf(ping, NUMBEROF(ping) - 1, L"%ld", MIN(player->ping, 999));
		tag_index = player->tag;
		break;
	}
	case _host_tab_game:
	{
		short game_row = host_menu.game_rows[entry];

		host_game_row_text(game_row, label, value, help, HOST_CONTROLS_TEXT_LENGTH);
		/* (a typed row's words as they are typed) */
		if ((game_row == _host_game_announce || game_row == _host_game_password) && text_field_editing(row))
		{
			typed = TRUE;
			text_field_show(named(row, "host_item_value", 0), "", TRUE);
		}
		break;
	}
	case _host_tab_rules:
		network_host_rules_row(host_menu.rule_rows[entry], label, value, help, HOST_CONTROLS_TEXT_LENGTH);
		/* (SAVE AS GAMETYPE's name as it is typed) */
		if (host_menu.rule_rows[entry] == _host_rule_save && text_field_editing(row))
		{
			typed = TRUE;
			text_field_show(named(row, "host_item_value", 0), "", TRUE);
		}
		break;
	case _host_tab_bots:
		if (entry < host_menu.bot_row_count)
		{
			short bot_row = host_menu.bot_rows[entry];

			network_host_controls_bot_row(bot_row, label, value, help, HOST_CONTROLS_TEXT_LENGTH);
			if (bot_row == _host_bot_row_balance && host_confirming((short)(bot_row + HOST_CONFIRM_BOT_ROW)))
				usnprintf(value, NUMBEROF(value) - 1, L"PRESS AGAIN");
		}
		else
		{
			struct host_controls_player const *bot = &host_menu.bots[entry - host_menu.bot_row_count];

			ustrncpy(label, bot->name, NUMBEROF(label) - 1);
			label[NUMBEROF(label) - 1] = 0;
			if (host_game_teams() && bot->team != NONE)
			{
				usnprintf(team, NUMBEROF(team) - 1, L"%s", host_team_name(bot->team));
				host_menu.row_teams[row_index] = (short)PIN(bot->team, 0, 1);
			}
			if (game_engine && bot->player_index != NONE)
			{
				wchar_t formatted[256];

				formatted[0] = 0;
				game_engine->format_player_score(bot->player_index, formatted);
				ustrncpy(score, formatted, NUMBEROF(score) - 1);
				score[NUMBEROF(score) - 1] = 0;
			}
		}
		break;
	case _host_tab_maps:
		if (entry == 0)
		{
			wchar_t gametype[HOST_CONTROLS_TEXT_LENGTH];

			host_gametype_shown(gametype, NUMBEROF(gametype));
			usnprintf(label, NUMBEROF(label) - 1, L"GAMETYPE");
			usnprintf(value, NUMBEROF(value) - 1, L"\x2039 %s \x203A", gametype);
		}
		else
		{
			struct host_game_map const *map = &host_menu.maps[entry - 1];

			lobby_board_map_text(map->level, label, NUMBEROF(label));
			usnprintf(value, NUMBEROF(value) - 1, L"%s%s%s", host_map_current(map) ? L"PLAYING" : L"",
				host_map_current(map) && map->custom ? L" \x00B7 " : L"", map->custom ? L"CUSTOM EDITION" : L"");
		}
		break;
	case _host_tab_playlist:
	{
		struct server_rotation const *playlist = host_playlist();
		short index = host_playlist_entry(entry);
		struct server_entry change;
		struct server_entry const *shown;
		long next = network_host_game_playlist_after(playlist, host_playlist_index());
		wchar_t text[HOST_CONTROLS_TEXT_LENGTH];
		wchar_t *dot;

		if (index == HOST_PLAYLIST_CHANGE)
		{
			boolean now;

			network_host_game_next(&change, &now);
			shown = &change;
		}
		else
			shown = &playlist->entries[index];
		network_host_game_entry_shown(shown, text, NUMBEROF(text));
		/* (the map's name, then its gametype: "Blood Gulch \x00B7 CTF") */
		dot = text;
		while (*dot && *dot != 0x00B7)
			dot++;
		if (*dot)
		{
			ustrncpy(value, dot[1] ? dot + 2 : dot + 1, NUMBEROF(value) - 1);
			value[NUMBEROF(value) - 1] = 0;
			if (dot > text)
				dot[-1] = 0;
			else
				*dot = 0;
		}
		if (index == HOST_PLAYLIST_CHANGE)
			usnprintf(label, NUMBEROF(label) - 1, L"NEXT: %s", text);
		else
			usnprintf(label, NUMBEROF(label) - 1, L"%d. %s", index + 1, text);
		if (index != HOST_PLAYLIST_CHANGE && shown->rejected[0])
			usnprintf(value, NUMBEROF(value) - 1, L"CAN'T BE PLAYED");
		else if (index != HOST_PLAYLIST_CHANGE && index == next && !host_change_asked(&change))
		{
			size_t length = ustrlen(value);

			usnprintf(value + length, NUMBEROF(value) - 1 - length, L" \x00B7 NEXT");
		}
		break;
	}
	default:
	{
		struct host_controls_ban const *ban = &host_menu.bans[entry];

		text_to_wide(ban->players[0] ? ban->players : ban->address, label, HOST_CONTROLS_TEXT_LENGTH);
		text_to_wide(ban->when, value, HOST_CONTROLS_TEXT_LENGTH);
		break;
	}
	}
	label[NUMBEROF(label) - 1] = value[NUMBEROF(value) - 1] = 0;
	menu_text_box_fit(named(row, "host_item_label", 0), label, 0);
	text_set(named(row, "host_item_team", 0), team);
	menu_text_box_fit(named(row, "host_item_score", 0), score, 0);
	text_set(named(row, "host_item_ping", 0), ping);
	if (!typed)
		menu_text_box_fit(named(row, "host_item_value", 0), value, 0);
	visible_set(tag, tag_index != _host_controls_tag_none);
	if (tag)
	{
		text_set(tag, host_tag_name(tag_index));
		tag->animation.current_frame_index = tag_index == _host_controls_tag_openh1 ? 0 :
			tag_index == _host_controls_tag_host ? 2 : 1;
	}
}

/* the buttons' actions for the entry chosen: a player's actions (or how
long a ban or mute is), a map's, a playlist's entry's, a ban's UNBAN; their
count */
static short host_buttons_of(struct host_controls_player const *player, short chosen, short *buttons)
{
	static short const order[] = { _host_controls_kick, _host_controls_ban, _host_controls_move_team,
		_host_controls_mute };
	boolean lobby = host_lobby();
	short shown = 0, index;

	switch (host_menu.tab)
	{
	case _host_tab_players:
		if (!player)
			break;
		if (host_menu.choosing == _host_controls_ban && TEST_FLAG(player->actions, _host_controls_ban))
		{
			buttons[shown++] = _host_button_ban_hour;
			buttons[shown++] = _host_button_ban_day;
			buttons[shown++] = _host_button_ban_forever;
			buttons[shown++] = _host_button_ban_reason;
			break;
		}
		if (host_menu.choosing == _host_controls_mute && TEST_FLAG(player->actions, _host_controls_mute))
		{
			buttons[shown++] = _host_button_mute_short;
			buttons[shown++] = _host_button_mute_hour;
			buttons[shown++] = _host_button_mute_day;
			buttons[shown++] = _host_button_cancel;
			break;
		}
		for (index = 0; index < NUMBEROF(order); index++)
		{
			if (TEST_FLAG(player->actions, order[index]))
				buttons[shown++] = order[index] == _host_controls_mute && host_muted(player) ?
					_host_button_unmute : order[index];
		}
		break;
	case _host_tab_maps:
		/* (co-op plays the campaign's levels: no change) */
		if (!host_chosen_map() || (!host_remote() && network_host_game_cooperative()))
			break;
		buttons[shown++] = _host_button_map_now;
		if (!lobby)
			buttons[shown++] = _host_button_map_after;
		buttons[shown++] = _host_button_map_add;
		break;
	case _host_tab_playlist:
		if (chosen == NONE || (!host_remote() && network_host_game_cooperative()))
			break;
		if (host_menu.chosen_entry == HOST_PLAYLIST_CHANGE)
		{
			buttons[shown++] = _host_button_change_cancel;
			break;
		}
		buttons[shown++] = _host_button_entry_now;
		if (!lobby)
			buttons[shown++] = _host_button_entry_next;
		buttons[shown++] = _host_button_entry_remove;
		/* (the server's rotation is never emptied) */
		if (!host_remote())
			buttons[shown++] = _host_button_playlist_clear;
		break;
	case _host_tab_bans:
		if (chosen != NONE)
			buttons[shown++] = _host_button_unban;
		break;
	case _host_tab_bots:
		if (host_chosen_bot() && TEST_FLAG(host_chosen_bot()->actions, _host_controls_kick))
			buttons[shown++] = _host_button_bot_remove;
		break;
	default:
		break;
	}
	return shown;
}

/* a button's words */
static void host_button_text(short action, struct host_controls_player const *player, wchar_t *text, short size)
{
	boolean lobby = host_lobby();
	boolean confirming = host_confirming((short)(action + HOST_CONFIRM_BUTTON));

	text[0] = 0;
	switch (action)
	{
	case _host_controls_kick:
		usnprintf(text, size - 1, L"KICK");
		break;
	case _host_controls_ban:
		usnprintf(text, size - 1, L"BAN...");
		break;
	case _host_controls_move_team:
		usnprintf(text, size - 1, player && player->team == 1 ? L"MOVE TO RED" : L"MOVE TO BLUE");
		break;
	case _host_controls_mute:
		usnprintf(text, size - 1, L"MUTE...");
		break;
	case _host_button_unmute:
		usnprintf(text, size - 1, L"UNMUTE");
		break;
	case _host_button_unban:
		usnprintf(text, size - 1, L"UNBAN");
		break;
	case _host_button_ban_hour:
		usnprintf(text, size - 1, L"BAN FOR AN HOUR");
		break;
	case _host_button_ban_day:
		usnprintf(text, size - 1, L"BAN FOR A DAY");
		break;
	case _host_button_ban_forever:
		usnprintf(text, size - 1, L"BAN FOR GOOD");
		break;
	case _host_button_ban_reason:
	{
		wchar_t reason[HOST_CONTROLS_REASON_LENGTH + 1];

		text_to_wide(host_menu.reason, reason, NUMBEROF(reason));
		usnprintf(text, size - 1, reason[0] ? L"WHY: %s" : L"ADD A REASON", reason);
		break;
	}
	case _host_button_mute_short:
		usnprintf(text, size - 1, L"MUTE FOR 10 MINUTES");
		break;
	case _host_button_mute_hour:
		usnprintf(text, size - 1, L"MUTE FOR AN HOUR");
		break;
	case _host_button_mute_day:
		usnprintf(text, size - 1, L"MUTE FOR A DAY");
		break;
	case _host_button_cancel:
	case _host_button_change_cancel:
		usnprintf(text, size - 1, action == _host_button_cancel ? L"CANCEL" : L"CANCEL CHANGE");
		break;
	case _host_button_map_now:
	case _host_button_entry_now:
		usnprintf(text, size - 1, lobby ? L"PLAY IN THIS LOBBY" : confirming ? L"PRESS AGAIN" : L"PLAY NOW");
		break;
	case _host_button_map_after:
	case _host_button_entry_next:
		usnprintf(text, size - 1, L"AFTER THIS GAME");
		break;
	case _host_button_map_add:
		usnprintf(text, size - 1, L"ADD TO PLAYLIST");
		break;
	case _host_button_entry_remove:
		usnprintf(text, size - 1, L"REMOVE");
		break;
	case _host_button_playlist_clear:
		usnprintf(text, size - 1, confirming ? L"PRESS AGAIN" : L"CLEAR PLAYLIST");
		break;
	case _host_button_bot_remove:
		usnprintf(text, size - 1, L"REMOVE BOT");
		break;
	default:
		break;
	}
	text[size - 1] = 0;
}

/* the panel's buttons for the entry chosen */
static void host_buttons_show(struct widget_instance *list, struct host_controls_player const *player,
	short chosen)
{
	short shown, index;
	short buttons[HOST_CONTROLS_ACTIONS];

	for (index = 0; index < HOST_CONTROLS_ACTIONS; index++)
		buttons[index] = host_menu.buttons[index] = NONE;
	shown = host_buttons_of(player, chosen, buttons);
	for (index = 0; index < shown; index++)
		host_menu.buttons[index] = buttons[index];
	/* (the buttons from the panel's foot up: its last the lowest) */
	for (index = 0; index < HOST_CONTROLS_ACTIONS; index++)
	{
		char name[24];
		struct widget_instance *button;
		short action = index >= HOST_CONTROLS_ACTIONS - shown ?
			host_menu.buttons[index - (HOST_CONTROLS_ACTIONS - shown)] : NONE;
		wchar_t text[96];

		snprintf(name, sizeof(name), "host_action_%d", index);
		button = named(list, name, 0);
		visible_set(button, action != NONE);
		/* (a reason being typed: its words as typed) */
		if (button && action == _host_button_ban_reason && text_field_editing(button))
		{
			text_field_show(button, "", TRUE);
			continue;
		}
		host_button_text(action, player, text, NUMBEROF(text));
		/* (the confirm key's prompt on the button with the focus alone, as
		the device in use has it: ui_widget.c; a touchscreen's buttons are
		tapped, and name no key) */
		if (text[0] && button && button == list->focused_child && !ui_widget_port_touch_menus())
		{
			wchar_t words[96];

			ustrncpy(words, text, NUMBEROF(words) - 1);
			words[NUMBEROF(words) - 1] = 0;
			usnprintf(text, NUMBEROF(text) - 1, L"%%a-button %s", words);
		}
		text[NUMBEROF(text) - 1] = 0;
		menu_text_box_fit(button, text, 0);
	}
}

/* the panel: the entry chosen's name, what it is, and what was done or what
its row does */
static void host_panel_show(struct widget_instance *list, short chosen)
{
	struct widget_instance *panel = list->parameters.list.extended_description;
	wchar_t title[HOST_CONTROLS_TEXT_LENGTH], where[HOST_CONTROLS_TEXT_LENGTH], status[HOST_CONTROLS_TEXT_LENGTH];
	struct host_controls_player const *player = NULL;
	boolean notice = host_menu.notice_time && system_milliseconds() - host_menu.notice_time < HOST_CONTROLS_NOTICE_TIME;
	boolean lobby = host_lobby();

	title[0] = where[0] = status[0] = 0;
	switch (host_menu.tab)
	{
	case _host_tab_players:
		player = chosen != NONE ? &host_menu.players[chosen] : NULL;
		if (!player)
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Players");
			usnprintf(status, NUMBEROF(status) - 1, L"Choose a player to kick, ban, move or mute them.");
			break;
		}
		ustrncpy(title, player->name, NUMBEROF(title) - 1);
		title[NUMBEROF(title) - 1] = 0;
		if (player->tag == _host_controls_tag_host)
			usnprintf(where, NUMBEROF(where) - 1, L"Your player");
		else if (player->tag == _host_controls_tag_bot)
			usnprintf(where, NUMBEROF(where) - 1, L"A bot");
		else if (player->ping != NONE)
		{
			usnprintf(where, NUMBEROF(where) - 1, L"%s%s%ld ms", player->tag == _host_controls_tag_opence ?
				L"OpenCE \x00B7 " : player->tag == _host_controls_tag_openh1 ? L"OpenH1 \x00B7 " : L"",
				L"", player->ping);
		}
		else if (player->tag == _host_controls_tag_opence || player->tag == _host_controls_tag_openh1)
			usnprintf(where, NUMBEROF(where) - 1, player->tag == _host_controls_tag_opence ? L"OpenCE" : L"OpenH1");
		if (host_game_teams() && player->team != NONE)
		{
			size_t length = ustrlen(where);

			usnprintf(where + length, NUMBEROF(where) - 1 - length, L"%s%s team", length ? L"\r\n" : L"",
				host_team_name(player->team));
		}
		if (host_menu.choosing == _host_controls_ban)
			usnprintf(status, NUMBEROF(status) - 1, L"For how long? They can't join until then.");
		else if (host_menu.choosing == _host_controls_mute)
			usnprintf(status, NUMBEROF(status) - 1, L"For how long? No chat or voice.");
		else if (!lobby && game_engine_running() && game_engine_has_teams() &&
			!TEST_FLAG(player->actions, _host_controls_move_team))
		{
			usnprintf(status, NUMBEROF(status) - 1, (host_remote() ? host_flag(_network_admin_flag_standard) :
				network_standard_hosting()) ? L"Players move teams only when everyone is on OpenH1." :
				L"Players can't move teams in a Legacy game.");
		}
		break;
	case _host_tab_game:
		if (chosen != NONE)
		{
			wchar_t label[HOST_CONTROLS_TEXT_LENGTH];

			host_game_row_text(host_menu.game_rows[chosen], label, where, status, HOST_CONTROLS_TEXT_LENGTH);
			ustrncpy(title, label, NUMBEROF(title) - 1);
			title[NUMBEROF(title) - 1] = 0;
		}
		else
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Game");
			usnprintf(status, NUMBEROF(status) - 1, lobby ? L"Change the next game before it starts." :
				L"Change the game while it plays. Choose a row to see what it does.");
		}
		break;
	case _host_tab_maps:
	{
		struct host_game_map const *map = host_chosen_map();
		wchar_t gametype[HOST_CONTROLS_TEXT_LENGTH];

		host_gametype_shown(gametype, NUMBEROF(gametype));
		if (!map)
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Maps");
			usnprintf(where, NUMBEROF(where) - 1, L"%s", gametype);
			if (!host_remote() && network_host_game_maps_left_out())
				usnprintf(status, NUMBEROF(status) - 1, L"%ld more maps aren't listed: the list holds %d.",
					network_host_game_maps_left_out(), MAXIMUM_HOST_GAME_MAPS);
			else
				usnprintf(status, NUMBEROF(status) - 1, host_gametype() ? L"Choose the gametype (left and right), "
					L"then a map: play it now, after this game, or add it to the playlist." :
					L"CURRENT keeps this game's gametype, score, time and rules. Choose a map, or another gametype "
					L"(left and right).");
			break;
		}
		lobby_board_map_text(map->level, title, NUMBEROF(title));
		usnprintf(where, NUMBEROF(where) - 1, L"%s%s", gametype, map->custom ? L"\r\nCustom Edition" : L"");
		if (host_confirming((short)(_host_button_map_now + HOST_CONFIRM_BUTTON)))
			usnprintf(status, NUMBEROF(status) - 1, L"Press PLAY NOW again: this game ends, with no scores, and "
				L"this one starts.");
		else
			usnprintf(status, NUMBEROF(status) - 1, lobby ? L"Play it here, or add it to the playlist." :
				L"Play it now, next, or add it to the playlist.");
		break;
	}
	case _host_tab_playlist:
	{
		struct server_rotation const *playlist = host_playlist();

		if (chosen == NONE)
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Playlist");
			usnprintf(status, NUMBEROF(status) - 1, playlist->count ?
				L"After each game the next one loads. Choose a game to play it, or take it out." :
				L"Add games from MAPS: after each game the next one loads.");
			break;
		}
		if (host_menu.chosen_entry == HOST_PLAYLIST_CHANGE)
		{
			struct server_entry change;
			boolean now;

			network_host_game_next(&change, &now);
			usnprintf(title, NUMBEROF(title) - 1, L"Next game");
			network_host_game_entry_shown(&change, where, NUMBEROF(where));
			usnprintf(status, NUMBEROF(status) - 1, now ? L"Starting now." :
				L"Plays after this game. The playlist goes on after it.");
			break;
		}
		if (host_menu.chosen_entry >= 0 && host_menu.chosen_entry < playlist->count)
		{
			struct server_entry const *entry = &playlist->entries[host_menu.chosen_entry];
			wchar_t reason[HOST_CONTROLS_TEXT_LENGTH];

			usnprintf(title, NUMBEROF(title) - 1, L"Game %d", host_menu.chosen_entry + 1);
			network_host_game_entry_shown(entry, where, NUMBEROF(where));
			text_to_wide(entry->rejected, reason, NUMBEROF(reason));
			if (host_confirming((short)(_host_button_playlist_clear + HOST_CONFIRM_BUTTON)))
				usnprintf(status, NUMBEROF(status) - 1, L"Press again to take every game out.");
			else if (reason[0])
				usnprintf(status, NUMBEROF(status) - 1, L"%s", reason);
			else
				usnprintf(status, NUMBEROF(status) - 1, lobby ? L"Play it in this lobby, or take it out." :
					L"Play it now or next, or take it out.");
		}
		break;
	}
	case _host_tab_rules:
		if (chosen != NONE)
		{
			wchar_t label[HOST_CONTROLS_TEXT_LENGTH];

			network_host_rules_row(host_menu.rule_rows[chosen], label, where, status, HOST_CONTROLS_TEXT_LENGTH);
			ustrncpy(title, label, NUMBEROF(title) - 1);
			title[NUMBEROF(title) - 1] = 0;
		}
		else
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Rules");
			usnprintf(status, NUMBEROF(status) - 1, lobby ? L"The lobby's game's rules. B goes back to GAME." :
				L"Change the rules while the game plays. B goes back to GAME.");
		}
		break;
	case _host_tab_bots:
	{
		struct host_controls_player const *bot = host_chosen_bot();

		if (chosen != NONE && host_menu.chosen_bot_row != NONE)
		{
			wchar_t label[HOST_CONTROLS_TEXT_LENGTH];

			network_host_controls_bot_row(host_menu.chosen_bot_row, label, where, status, HOST_CONTROLS_TEXT_LENGTH);
			ustrncpy(title, label, NUMBEROF(title) - 1);
			title[NUMBEROF(title) - 1] = 0;
			if (host_menu.chosen_bot_row == _host_bot_row_balance &&
				host_confirming((short)(host_menu.chosen_bot_row + HOST_CONFIRM_BOT_ROW)))
			{
				usnprintf(status, NUMBEROF(status) - 1, L"Press again: those who move die and respawn.");
			}
		}
		else if (bot)
		{
			ustrncpy(title, bot->name, NUMBEROF(title) - 1);
			title[NUMBEROF(title) - 1] = 0;
			usnprintf(where, NUMBEROF(where) - 1, L"A bot%s%s%s", host_game_teams() && bot->team != NONE ?
				L"\r\n" : L"", host_game_teams() && bot->team != NONE ? host_team_name(bot->team) : L"",
				host_game_teams() && bot->team != NONE ? L" team" : L"");
			usnprintf(status, NUMBEROF(status) - 1, TEST_FLAG(bot->actions, _host_controls_kick) ?
				L"Take it out of the game: one bot fewer plays." : L"The matchmaking playlist keeps its bots.");
		}
		else
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Bots");
			usnprintf(status, NUMBEROF(status) - 1, !network_host_controls_bots_play() ?
				L"Bots do not play Race: these apply from the next game." : lobby ?
				L"Change the bots before the game starts. Choose a bot to take it out." :
				L"Change the bots while the game plays. Choose a bot to take it out.");
		}
		break;
	}
	default:
		if (chosen != NONE)
		{
			struct host_controls_ban const *ban = &host_menu.bans[chosen];
			wchar_t reason[HOST_CONTROLS_TEXT_LENGTH];

			text_to_wide(ban->players[0] ? ban->players : ban->address, title, HOST_CONTROLS_TEXT_LENGTH);
			text_to_wide(ban->reason, reason, HOST_CONTROLS_TEXT_LENGTH);
			if (ban->when[0])
			{
				wchar_t when[HOST_CONTROLS_BAN_WHEN_SIZE];

				text_to_wide(ban->when, when, HOST_CONTROLS_BAN_WHEN_SIZE);
				usnprintf(where, NUMBEROF(where) - 1, L"Banned %s", when);
			}
			usnprintf(status, NUMBEROF(status) - 1, L"%s", reason[0] ? reason : L"Banned by hand in bans.txt");
		}
		else
		{
			usnprintf(title, NUMBEROF(title) - 1, L"Bans");
			usnprintf(status, NUMBEROF(status) - 1, L"Players you ban are listed here. They can't join your "
				L"games until you unban them.");
		}
		break;
	}
	if (notice)
		ustrncpy(status, host_menu.notice, NUMBEROF(status) - 1);
	title[NUMBEROF(title) - 1] = where[NUMBEROF(where) - 1] = status[NUMBEROF(status) - 1] = 0;
	host_buttons_show(list, player, chosen);
	if (!panel)
		return;
	menu_text_box_wrap(named(panel, "panel_title", 0), title, 2);
	menu_text_box_wrap(named(panel, "panel_where", 0), where, 3);
	menu_text_box_wrap(named(panel, "panel_status", 0), status, 5);
}

/* "port host update" (the list's, each frame) */
static void host_controls_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *child;
	struct widget_instance *focused = list->focused_child;
	short focused_row = host_row_index(focused);
	short count, rows, chosen, index, shown = host_rows_of(list);
	long opening_player;
	short opening_tab;
	wchar_t text[HOST_CONTROLS_TEXT_LENGTH];

	/* (an admin's request the server refused: why, in the panel) */
	{
		boolean ok;
		char said[NETWORK_ADMIN_RESULT_SIZE];

		if (network_admin_take_result(&ok, said, sizeof(said)) && !ok)
			host_notice_text(said);
	}
	/* (an opening's tab and player: the scoreboard's, the console's) */
	if (network_host_controls_take_opening(&opening_player, &opening_tab))
	{
		host_tab_set(opening_tab);
		host_player_choose(NULL);
		host_menu.chosen_player = opening_player;
		host_menu.focus_chosen = opening_player != NONE;
	}
	/* (BOTS gone: co-op, or no longer the host's; RULES, back to GAME) */
	if (host_menu.tab == _host_tab_bots && !network_host_controls_bots_tab())
		host_tab_set(_host_tab_players);
	if (host_menu.tab == _host_tab_rules && !network_host_rules_available())
		host_tab_set(_host_tab_game);
	host_entries_read();
	/* (the player a ban's or mute's length, or a ban's reason, was being
	chosen for gone: none of it kept for whoever comes next) */
	if (host_menu.tab == _host_tab_players && !host_chosen_player() &&
		(host_menu.choosing != NONE || host_menu.reason[0]))
	{
		if (text_field_editing(NULL))
			text_field_end(FALSE);
		host_menu.choosing = NONE;
		host_menu.reason[0] = 0;
	}
	count = host_entry_count();
	rows = (short)MIN(count, shown);
	/* (a list of more entries than rows: the focus kept off its ends while
	there are more past them, the list moving instead; not while a row's
	words are typed) */
	if (!text_field_editing(NULL))
	{
		if (focused_row == rows - 1 && host_menu.first + rows < count && list_scrolls_at_end())
			host_menu.first++;
		else if (focused_row == 0 && host_menu.first > 0 && list_scrolls_at_end())
			host_menu.first--;
	}
	host_menu.first = (short)PIN(host_menu.first, 0, MAX(count - shown, 0));
	/* (the opening's player's row given the focus, once shown) */
	if (host_menu.focus_chosen)
	{
		chosen = host_chosen_entry();
		if (chosen != NONE)
		{
			char name[24];

			if (chosen < host_menu.first || chosen >= host_menu.first + shown)
				host_menu.first = (short)PIN(chosen - shown / 2, 0, MAX(count - shown, 0));
			snprintf(name, sizeof(name), "host_item_%d", chosen - host_menu.first + 1);
			host_focus(list, named(list, name, 0));
			focused = list->focused_child;
			focused_row = host_row_index(focused);
		}
		host_menu.focus_chosen = FALSE;
		host_menu.focus_first = FALSE;
	}
	/* (the first row given the focus, once shown: opened, a tab chosen) */
	if (host_menu.focus_first && !host_menu.focus_chosen)
	{
		struct widget_instance *first = named(list, "host_item_1", 0);

		if (count > 0 && first)
		{
			first->visible = TRUE;
			host_focus(list, first);
			focused = first;
			focused_row = 0;
		}
		host_menu.focus_first = FALSE;
	}
	/* (the row with the focus chooses its entry) */
	if (focused_row != NONE)
		host_choose(host_row_entry(focused_row));
	chosen = host_chosen_entry();
	for (child = list->child; child; child = child->next)
	{
		short row_index = host_row_index(child);
		short entry = host_row_entry(row_index);

		if (row_index == NONE)
			continue;
		child->visible = entry != NONE;
		if (entry != NONE)
			host_row_show(child, row_index, entry);
		/* (the rows' backgrounds, port_settings.py's PARTS row: the chosen
		entry's brighter, the focused one's outlined) */
		child->animation.current_frame_index = entry != NONE && entry == chosen ? 2 : child == focused ? 1 : 0;
	}
	host_panel_show(list, chosen);
	/* (a typed row gone from the list: its typing let go of) */
	if (text_field_editing(NULL) && host_menu.tab == _host_tab_game &&
		!host_game_row_widget(list, _host_game_announce) && !host_game_row_widget(list, _host_game_password))
	{
		text_field_end(FALSE);
	}
	if (text_field_editing(NULL) && host_menu.tab == _host_tab_rules && host_menu.chosen_rule != _host_rule_save)
		text_field_end(FALSE);
	/* (the focus off what is hidden: the first row, else the tabs) */
	if (!focused || !focused->visible)
	{
		struct widget_instance *first = named(list, "host_item_1", 0);

		host_focus(list, first && first->visible ? first : named(list, "host_tabs", 0));
	}
	/* (the tabs, the counts, the titles, the words for no rows) */
	{
		struct widget_instance *strip = named(list, "host_tabs", 0);

		/* (a tab chosen by LB or RB, or opened on, takes the strip's focus;
		RULES shows GAME's) */
		short strip_tab = host_menu.tab == _host_tab_rules ? _host_tab_game : host_menu.tab;

		if (strip && host_menu.tab_shown != host_menu.tab)
		{
			struct widget_instance *tab = strip->child;

			for (index = 0; tab && index < strip_tab; index++)
				tab = tab->next;
			if (tab)
			{
				strip->focused_child = tab;
				strip->parameters.list.selected_index = strip_tab;
			}
		}
		host_menu.tab_shown = host_menu.tab;
		menu_tabs_show(strip, strip_tab);
		/* (BOTS, the last tab, the host's but in co-op) */
		if (strip)
		{
			struct widget_instance *tab = strip->child;

			for (index = 0; tab && index < _host_tab_bots; index++)
				tab = tab->next;
			visible_set(tab, network_host_controls_bots_tab());
			focus_off_hidden(strip);
		}
	}
	/* (the players, and the game's kind: its tick rate, or LEGACY; in the
	lobby, so said) */
	if (host_remote() ? host_flag(_network_admin_flag_standard) : network_standard_hosting())
	{
		struct network_admin_state state;
		long rate = network_admin_remote_state(&state) ? state.rate : network_standard_server_rate(FALSE);

		usnprintf(text, NUMBEROF(text) - 1, L"%d %s    %ld TICK%s", host_menu.player_count,
			host_menu.player_count == 1 ? L"PLAYER" : L"PLAYERS", rate,
			host_lobby() ? L"    LOBBY" : L"");
	}
	else
	{
		usnprintf(text, NUMBEROF(text) - 1, L"%d %s    LEGACY%s", host_menu.player_count,
			host_menu.player_count == 1 ? L"PLAYER" : L"PLAYERS", host_lobby() ? L"    LOBBY" : L"");
	}
	text[NUMBEROF(text) - 1] = 0;
	menu_text_box_fit(named(screen, "host_counts", 0), text, 0);
	host_headers_show(screen);
	visible_set(named(screen, "host_empty", 0), count == 0);
	if (!count)
	{
		menu_text_box_wrap(named(screen, "host_empty", 0), !host_available() ?
			L"The game is over." : host_menu.tab == _host_tab_bans ? L"No one is banned." :
			host_menu.tab == _host_tab_playlist ? L"No games yet: add them from MAPS." : L"Nothing here.", 3);
	}
}

/* "port host tab", "port host tab next", "port host tab previous" */
static boolean host_controls_tab(struct widget_instance *widget, short step)
{
	/* (not while a row's words are typed: its keys are the typing's) */
	if (text_field_editing(NULL))
		return TRUE;
	if (step)
	{
		/* (from RULES, as from GAME) */
		short from = host_menu.tab == _host_tab_rules ? _host_tab_game : host_menu.tab;
		short tab = (short)((from + step + NUMBER_OF_HOST_TABS) % NUMBER_OF_HOST_TABS);

		/* (BOTS hidden: past it) */
		if (tab == _host_tab_bots && !network_host_controls_bots_tab())
			tab = (short)((tab + step + NUMBER_OF_HOST_TABS) % NUMBER_OF_HOST_TABS);
		host_tab_set(tab);
	}
	else
		host_tab_set(sibling_index(widget));
	ui_play_audio_feedback_sound(SOUND_CURSOR);
	return TRUE;
}

/* the panel's first button shown given the focus: TRUE if one was */
static boolean host_focus_buttons(struct widget_instance *list)
{
	short index;

	host_panel_show(list, host_chosen_entry());
	for (index = 0; index < HOST_CONTROLS_ACTIONS; index++)
	{
		char name[24];
		struct widget_instance *button;

		snprintf(name, sizeof(name), "host_action_%d", index);
		button = named(list, name, 0);
		if (button && button->visible)
		{
			host_focus(list, button);
			return TRUE;
		}
	}
	return FALSE;
}

/* "port host row" (A or a click on a row): a player's, a map's, a
playlist's or a ban's chosen, the focus to its buttons; a game's row
changed; the gametype's stepped */
static boolean host_controls_row(struct widget_instance *row)
{
	short entry = host_row_entry(host_row_index(row));
	struct widget_instance *list = row->parent;

	if (entry == NONE || !list)
		return FALSE;
	host_choose(entry);
	if (host_menu.tab == _host_tab_game)
	{
		host_game_row_change(row, host_menu.game_rows[entry], 0);
		return TRUE;
	}
	if (host_menu.tab == _host_tab_bots && entry < host_menu.bot_row_count)
	{
		host_bot_row_change(host_menu.bot_rows[entry], 0);
		return TRUE;
	}
	if (host_menu.tab == _host_tab_rules)
	{
		host_rule_change(row, host_menu.rule_rows[entry], 0);
		return TRUE;
	}
	if (host_menu.tab == _host_tab_maps && entry == 0)
	{
		host_gametype_step(1);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	}
	if (host_focus_buttons(list))
		ui_play_audio_feedback_sound(SOUND_CURSOR);
	else
		ui_play_audio_feedback_sound(SOUND_ERROR);
	return TRUE;
}

/* "port host left", "port host right": a game's row's value stepped, the
maps' gametype */
static boolean host_controls_step(struct widget_instance *row, short step)
{
	short entry = host_row_entry(host_row_index(row));

	if (entry == NONE || text_field_editing(NULL))
		return FALSE;
	if (host_menu.tab == _host_tab_maps && entry == 0)
	{
		host_gametype_step(step);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	}
	if (host_menu.tab == _host_tab_rules)
	{
		if (host_menu.rule_rows[entry] == _host_rule_save)
			return FALSE;
		host_choose(entry);
		host_rule_change(row, host_menu.rule_rows[entry], step);
		return TRUE;
	}
	if (host_menu.tab == _host_tab_bots)
	{
		if (entry >= host_menu.bot_row_count || host_menu.bot_rows[entry] == _host_bot_row_balance)
			return FALSE;
		host_choose(entry);
		host_bot_row_change(host_menu.bot_rows[entry], step);
		return TRUE;
	}
	if (host_menu.tab != _host_tab_game)
		return FALSE;
	host_choose(entry);
	switch (host_menu.game_rows[entry])
	{
	case _host_game_end:
	case _host_game_restart:
	case _host_game_change_map:
	case _host_game_change_rules:
	case _host_game_shuffle_teams:
	case _host_game_balance_teams:
	case _host_game_announce:
	case _host_game_password:
		return FALSE;
	default:
		host_game_row_change(row, host_menu.game_rows[entry], step);
		return TRUE;
	}
}

/* a map's or a playlist's entry played (now or after this game) or added:
said in the panel */
static boolean host_play(char const *text, short when, long playlist_index)
{
	char reason[SERVER_REASON_SIZE];

	/* (an admin's: the server's sv_map, or sv_map_after) */
	if (host_remote())
	{
		if (!host_ask(when == _host_game_now ? _network_admin_map_now : _network_admin_map_after, NULL, 0, text))
			return FALSE;
		host_notice(host_lobby() ? L"The lobby plays it now." : when == _host_game_now ? L"The server plays it now." :
			L"It plays after this game.", NULL);
		return TRUE;
	}
	if (!network_host_game_change(text, when, playlist_index, reason, sizeof(reason)))
	{
		host_notice_text(reason);
		return FALSE;
	}
	if (host_lobby())
		host_notice(L"The lobby plays it now.", NULL);
	else if (when == _host_game_now && !strncmp(reason, "now", 3))
	{
		/* (back to the game, which ends) */
		ui_widgets_close_all();
	}
	else
		host_notice(L"It plays after this game.", NULL);
	return TRUE;
}

/* "port host action" (a panel's button) */
static boolean host_controls_action(struct widget_instance *button)
{
	short index = host_action_index(button);
	short shown = 0, action;
	struct host_controls_player *player = host_chosen_player();
	struct host_game_map const *map = host_chosen_map();
	wchar_t name[16];
	char text[SERVER_ENTRY_TEXT_SIZE];
	char reason[SERVER_REASON_SIZE];

	if (index == NONE)
		return FALSE;
	/* (a reason typed: its button's A ends it) */
	if (text_field_editing(button))
	{
		text_field_end(TRUE);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	}
	/* (the buttons from the panel's foot up: host_buttons_show) */
	while (shown < HOST_CONTROLS_ACTIONS && host_menu.buttons[shown] != NONE)
		shown++;
	action = index >= HOST_CONTROLS_ACTIONS - shown ? host_menu.buttons[index - (HOST_CONTROLS_ACTIONS - shown)] : NONE;
	name[0] = 0;
	if (player)
	{
		ustrncpy(name, player->name, NUMBEROF(name) - 1);
		name[NUMBEROF(name) - 1] = 0;
	}
	switch (action)
	{
	case _host_controls_kick:
		if (!player || !host_kick(player, FALSE, 0, NULL))
			break;
		host_notice(L"%s was kicked.", name);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_controls_ban:
	case _host_controls_mute:
		if (!player)
			break;
		host_menu.choosing = action;
		host_menu.reason[0] = 0;
		host_focus_buttons(button->parent);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	case _host_button_ban_hour:
	case _host_button_ban_day:
	case _host_button_ban_forever:
		if (!player || !host_kick(player, TRUE, action == _host_button_ban_hour ? HOST_BAN_HOUR :
			action == _host_button_ban_day ? HOST_BAN_DAY : 0, host_menu.reason))
		{
			break;
		}
		host_menu.choosing = NONE;
		host_notice(action == _host_button_ban_hour ? L"%s is banned for an hour." :
			action == _host_button_ban_day ? L"%s is banned for a day." : L"%s is banned.", name);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_ban_reason:
		text_field_begin(button, host_menu.reason, HOST_CONTROLS_REASON_LENGTH, host_reason_done);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	case _host_button_mute_short:
	case _host_button_mute_hour:
	case _host_button_mute_day:
		if (!player || !host_mute(player, action == _host_button_mute_short ? HOST_MUTE_SHORT :
			action == _host_button_mute_hour ? HOST_MUTE_HOUR : HOST_MUTE_DAY))
		{
			break;
		}
		host_menu.choosing = NONE;
		host_notice(action == _host_button_mute_short ? L"%s is muted for 10 minutes." :
			action == _host_button_mute_hour ? L"%s is muted for an hour." : L"%s is muted for a day.", name);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_unmute:
		if (!player || !host_mute(player, 0))
			break;
		host_notice(L"%s can talk again.", name);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_cancel:
		host_menu.choosing = NONE;
		host_focus_buttons(button->parent);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	case _host_controls_move_team:
		if (!player || !host_move(player))
			break;
		host_notice(player->team == 1 ? L"%s moves to Red." : L"%s moves to Blue.", name);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_map_now:
	case _host_button_map_after:
		if (!host_map_entry_text(map, FALSE, text, sizeof(text)))
			break;
		/* (now, in a game: pressed twice, as it ends) */
		if (action == _host_button_map_now && !host_lobby() &&
			!host_confirmed((short)(action + HOST_CONFIRM_BUTTON)))
		{
			return TRUE;
		}
		if (!host_play(text, action == _host_button_map_now ? _host_game_now : _host_game_after, NONE))
			break;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_map_add:
		if (!map)
			break;
		/* (a playlist's entry names its gametype: CURRENT's, if the list has
		it) */
		if (!host_map_entry_text(map, TRUE, text, sizeof(text)))
		{
			host_notice(L"Choose a gametype (left and right) to add this map to the playlist.", NULL);
			break;
		}
		if (host_remote())
		{
			if (!host_ask(_network_admin_rotation_add, NULL, 0, text))
				break;
			host_notice(L"Added to the server's rotation.", NULL);
			ui_play_audio_feedback_sound(SOUND_FORWARD);
			return TRUE;
		}
		if (!network_host_game_playlist_add(text, reason, sizeof(reason)))
		{
			host_notice_text(reason);
			break;
		}
		{
			wchar_t count[16];

			usnprintf(count, NUMBEROF(count) - 1, L"%d", host_playlist()->count);
			count[NUMBEROF(count) - 1] = 0;
			host_notice(L"Added: the playlist has %s games.", count);
		}
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_entry_now:
	case _host_button_entry_next:
	{
		struct server_rotation const *playlist = host_playlist();

		if (host_menu.chosen_entry < 0 || host_menu.chosen_entry >= playlist->count)
			break;
		if (action == _host_button_entry_now && !host_lobby() &&
			!host_confirmed((short)(action + HOST_CONFIRM_BUTTON)))
		{
			return TRUE;
		}
		snprintf(text, sizeof(text), "%s", playlist->entries[host_menu.chosen_entry].text);
		if (!host_play(text, action == _host_button_entry_now ? _host_game_now : _host_game_after,
			host_menu.chosen_entry))
		{
			break;
		}
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	case _host_button_entry_remove:
		if (host_remote() ? !host_ask(_network_admin_rotation_remove, NULL, host_menu.chosen_entry, NULL) :
			!network_host_game_playlist_remove(host_menu.chosen_entry))
		{
			break;
		}
		host_notice(L"Taken out of the playlist.", NULL);
		host_menu.chosen_entry = NONE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_playlist_clear:
		if (!host_confirmed((short)(action + HOST_CONFIRM_BUTTON)))
			return TRUE;
		if (!network_host_game_playlist_set("", reason, sizeof(reason)))
			break;
		host_notice(L"The playlist is empty.", NULL);
		host_menu.chosen_entry = NONE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_change_cancel:
		network_host_game_cancel();
		host_notice(L"The change is cancelled.", NULL);
		host_menu.chosen_entry = NONE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	case _host_button_bot_remove:
	{
		struct host_controls_player *bot = host_chosen_bot();

		if (!bot)
			break;
		ustrncpy(name, bot->name, NUMBEROF(name) - 1);
		name[NUMBEROF(name) - 1] = 0;
		if (!host_kick(bot, FALSE, 0, NULL))
			break;
		host_notice(L"%s left the game.", name);
		host_menu.chosen_bot_valid = FALSE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	case _host_button_unban:
		if (!host_menu.chosen_ban[0])
			break;
		/* (an admin's: the ban's number in the server's bans.txt) */
		if (host_remote())
		{
			short index;

			if (!host_ask(_network_admin_unban, NULL, atol(host_menu.chosen_ban), NULL))
				break;
			for (index = 0; index < host_menu.ban_count; index++)
			{
				if (!strcmp(host_menu.ban_lines[index], host_menu.chosen_ban))
				{
					wchar_t names[HOST_CONTROLS_TEXT_LENGTH];

					text_to_wide(host_menu.bans[index].players[0] ? host_menu.bans[index].players :
						host_menu.bans[index].address, names, HOST_CONTROLS_TEXT_LENGTH);
					host_notice(L"%s can join again.", names);
				}
			}
			host_menu.chosen_ban[0] = 0;
			ui_play_audio_feedback_sound(SOUND_FORWARD);
			return TRUE;
		}
		if (!network_host_controls_unban(host_menu.chosen_ban))
			break;
		{
			struct host_controls_ban ban;
			wchar_t names[HOST_CONTROLS_TEXT_LENGTH];

			network_host_controls_ban_parse(host_menu.chosen_ban, &ban);
			text_to_wide(ban.players[0] ? ban.players : ban.address, names, HOST_CONTROLS_TEXT_LENGTH);
			host_notice(L"%s can join again.", names);
		}
		host_menu.chosen_ban[0] = 0;
		host_menu.bans_read = FALSE;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	default:
		break;
	}
	ui_play_audio_feedback_sound(SOUND_ERROR);
	return TRUE;
}
