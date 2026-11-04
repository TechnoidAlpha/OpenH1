/*
NETWORK_HOST_CONTROLS.C

The host's controls of a network game, its internet or LAN game under way:
the pause menu's HOST CONTROLS (menu_functions.c's host_controls_menu.inc,
tools/port_settings.py's _host_controls), the scoreboard's player menu
(game_engine.c) and the console's host_controls (hs.c). Only the host
decides, and everything here takes the host's own ways of deciding:

- Kick and Ban are the console's kick and ban (network_votekick_host_kick,
  network_server_manager.c's network_game_server_kick_machine_of_player):
  the machine dropped, every machine told, a ban's line in bans.txt
  (network_distributed.c). The host's own players are never kicked; a bot
  of a network game is taken out (network_server_manager.c's
  network_game_server_port_bots_remove, every machine told as of a player
  who quits), with bots.count one less so that none takes its place.
- Mute for everyone is the host's mute of the text chat and voice
  (network_chat.c).
- End game now ends the game as its time limit does (game_engine_end_game):
  the carnage report, then the lobby. Restart round ends it too, and as soon
  as the scores show presses the scores' A for the host (every machine to
  the lobby: network_game_server_reset_to_pregame), then shows the lobby in
  the map choice's place and lets its countdown go, so the game starts again
  on its map and gametype.
- Lock teams: the players' own team changes in the lobby (the player
  settings a machine sends: network_server_message_handler.c) keep the team
  the host has them on; the host's machine's are its own. Nothing is sent:
  a client sees its team stay, as from a Legacy host that refused it.
- Move to other team: no machine of the Xbox's game changes a player's team
  in a game, so the others are told (_distributed_message_host_team, an
  OpenH1 game's own message: protocol version 4), which an OpenCE machine
  would not hear. It is offered only in an OpenH1 game without OpenCE's
  machines (network_host_controls_team_moves), whose machines all play the
  host's protocol (their join token says so). A living player's unit is
  killed first, without statistics (as a player who quits), by the host, so
  every machine sees the death as any other; the player respawns on the new
  team. Their score goes with them; their team's stays.
- Allow votekick is network.votekick, which the host reads at each vote;
  Listing is the internet listing's PUBLIC or PRIVATE (p2p_set_hosting_public),
  Max players the game's most (network_game_server_port_set_settings), never
  fewer than are in it, and Allow OpenCE players whether an OpenH1 game takes
  OpenCE's joins from now on (network_standard.c): those in it stay.
- The bans are bans.txt's lines, each taken out by Unban with the server's
  keeping out of its address (network_distributed_unban).

In the lobby (the host's, before the game starts) Host Controls lists the
game record's players, and its changes of map, teams, limits and the
playlist are network_host_game.c's; after a game, the next game it has
(a change asked for, the playlist's) is set in the lobby and started here.

Clients never see it (network_host_controls_available), but a dedicated
server's admins, whose Host Controls show the server's game and ask the
server for each change (network_admin.c): the scoreboard's kick, ban and
team move are asked of the server too. The pure parts
(network_host_controls_*_for, _step, _parse) are
tools/harness/tests/test_host_controls.py's.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "objects/objects.h"
#include "text/unicode.h"
#include "units/units.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "network_votekick.h"
#include "network_chat.h"
#include "network_host_controls.h"
#include "network_host_game.h"
#include "network_host_rules.h"
#include "network_admin.h"
#include "latency_meter.h"
#include "bots.h"

#include <stdio.h>
#include <string.h>

/* port_config.c's, menu_tags.c's, console.c's, network_distributed.c's */
int config_boolean(const char *name);
int config_write(const char *name, const char *value);
char const *pc_menus_screen(char const *name);
void console_warning(const char *format, ...);
unsigned long system_milliseconds(void);
short network_distributed_ban_lines(char (*lines)[HOST_CONTROLS_BAN_LINE_SIZE], short maximum);
boolean network_distributed_unban(char const *line);
/* network_server_manager.c's (the host's game record, its lobby's countdown) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
void network_game_server_begin_game_start_countdown(struct network_game_server *server, long time_remaining);
/* network_standard.c's */
boolean network_standard_opence_joins_open(void);
void network_standard_set_opence_joins(boolean allowed);

/* ---------- constants */

/* the screen (tools/port_settings.py's _host_controls) and the lobby the
restart shows in the map choice's place (the Xbox's name, ours where the PC
version's menus are up: menu_tags.c's pc_menus_screen) */
#define HOST_CONTROLS_SCREEN "pc\\main_menu\\host_controls\\host_controls_screen"
#define HOST_CONTROLS_LOBBY "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"

enum
{
	/* the team move's format: a machine takes its own alone */
	HOST_TEAM_FORMAT = 1,
	MAXIMUM_HOST_CONTROLS_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	/* the teams a player moves between (red, blue) */
	HOST_CONTROLS_TEAMS = 2,
	/* milliseconds since its last update the screen is taken to be shown */
	HOST_CONTROLS_SHOWN_TIME = 250,
	/* the lobby's countdown before a restarted round (milliseconds, as
	network_server_manager.c's NETWORK_GAME_COUNTDOWN_TIME) */
	HOST_CONTROLS_RESTART_COUNTDOWN = 5999,
	/* ... before the next game a change or the playlist set, after a game */
	HOST_CONTROLS_NEXT_COUNTDOWN = 9999,
};

/* ---------- structures */

/* on the wire, the host's to its OpenH1 clients, reliably: a player
(absolute index) put on a team */
struct distributed_host_team
{
	byte format;
	byte player_index;
	byte team;
	byte pad;
};

typedef char distributed_host_team_size_assert[sizeof(struct distributed_host_team) == 4 ? 1 : -1];

/* ---------- globals */

static struct
{
	/* the lobby's team changes refused (but the host machine's), while the
	server lasts */
	boolean teams_locked;
	/* a restart's step (_host_controls_restart_*) */
	short restart_stage;
	/* an opening asked for: the player chosen, the tab; taken once */
	boolean opening;
	long opening_player;
	short opening_tab;
	/* a team move heard of another format (noted once) */
	boolean other_format_noted;
	/* when the screen last showed (its update: host_controls_menu.inc) */
	unsigned long screen_time;
	/* the host's game was played since it was last in its lobby */
	boolean played;
} host_controls = { FALSE, _host_controls_restart_none, FALSE, NONE, 0, FALSE, 0, FALSE };

/* ---------- the pure parts */

boolean network_host_controls_shown_for(
	struct host_controls_seat const *seat)
{
	/* (a game of this machine alone, split screen or with bots, has a
	server too, but no other machine: its pause menu keeps END GAME) */
	return seat->server && ((seat->connection == _game_connection_network_server && seat->playing) || seat->lobby);
}

unsigned long network_host_controls_actions_for(
	struct host_controls_player_view const *view)
{
	unsigned long actions = 0;

	if (view->teams && view->team_moves)
		SET_FLAG(actions, _host_controls_move_team, TRUE);
	/* (the host's own players stay; a bot is the bots', which take theirs
	out, and is neither banned, its machine the host's, nor muted, having
	nothing to say) */
	if (view->own)
		return actions;
	if (view->bot)
	{
		if (view->bot_removable)
			SET_FLAG(actions, _host_controls_kick, TRUE);
		return actions;
	}
	SET_FLAG(actions, _host_controls_kick, TRUE);
	SET_FLAG(actions, _host_controls_ban, TRUE);
	SET_FLAG(actions, _host_controls_mute, TRUE);
	return actions;
}

boolean network_host_controls_team_moves_for(
	struct host_controls_team_view const *view)
{
	return view->standard && view->teams && view->opence_machines == 0;
}

char network_host_controls_requested_team(
	boolean locked,
	boolean host_machine,
	char requested,
	char current)
{
	return locked && !host_machine ? current : requested;
}

short network_host_controls_restart_step(
	short stage,
	struct host_controls_restart_view const *view,
	short *action)
{
	*action = _host_controls_restart_wait;
	/* (the server gone, the host left: nothing to restart) */
	if (!view->server)
		return _host_controls_restart_none;
	switch (stage)
	{
	case _host_controls_restart_ending:
		if (!view->postgame)
			return stage;
		*action = _host_controls_restart_to_lobby;
		return _host_controls_restart_lobby;
	case _host_controls_restart_lobby:
		if (!view->menu_loaded || !view->pregame)
			return stage;
		*action = _host_controls_restart_start;
		return _host_controls_restart_none;
	default:
		return _host_controls_restart_none;
	}
}

boolean network_host_controls_same_player(
	struct host_controls_player const *player,
	long player_index,
	long slot,
	long machine_index,
	wchar_t const *name)
{
	short index;

	/* (a game's player by datum; a lobby's by its slot, machine and name, as
	another may take the slot one left) */
	if (player->player_index != NONE)
		return player->player_index == player_index;
	if (player_index != NONE || player->slot != slot || player->machine_index != machine_index)
		return FALSE;
	for (index = 0; index < (short)NUMBEROF(player->name); index++)
	{
		if (player->name[index] != name[index])
			return FALSE;
		if (!name[index])
			break;
	}
	return TRUE;
}

short network_host_controls_maximum_step(
	short const *values,
	short count,
	short current,
	short present,
	short step)
{
	short index;

	/* (the next value of the list past the current, either way, that holds
	the players in the game; none: the current stays) */
	if (step > 0)
	{
		for (index = 0; index < count; index++)
		{
			if (values[index] > current && values[index] >= present)
				return values[index];
		}
	}
	else if (step < 0)
	{
		for (index = (short)(count - 1); index >= 0; index--)
		{
			if (values[index] < current && values[index] >= present)
				return values[index];
		}
	}
	return current;
}

/* the value of a ban line's part ("key=" up to the next tab or its end),
kept to size */
static void host_controls_ban_part(
	char const *line,
	char const *key,
	char *value,
	long size)
{
	char const *found = line;
	size_t key_length = strlen(key);
	long length = 0;

	value[0] = 0;
	/* (a part at the line's start, or after a tab) */
	while ((found = strstr(found, key)) != NULL && found != line && found[-1] != '\t')
		found += key_length;
	if (!found)
		return;
	for (found += key_length; *found && *found != '\t' && *found != '\r' && *found != '\n' && length < size - 1;
		found++)
	{
		value[length++] = *found;
	}
	value[length] = 0;
}

boolean network_host_controls_ban_parse(
	char const *line,
	struct host_controls_ban *ban)
{
	long length = 0;

	csmemset(ban, 0, sizeof(*ban));
	host_controls_ban_part(line, "ip=", ban->address, sizeof(ban->address));
	host_controls_ban_part(line, "hwid=", ban->hardware_id, sizeof(ban->hardware_id));
	host_controls_ban_part(line, "players=", ban->players, sizeof(ban->players));
	host_controls_ban_part(line, "reason=", ban->reason, sizeof(ban->reason));
	/* (a line that bans nothing: no address, no hardware id) */
	if (!ban->address[0] && (!ban->hardware_id[0] || !strcmp(ban->hardware_id, "none")))
		return FALSE;
	/* (when: the first part, if it is no "key=") */
	while (line[length] && line[length] != '\t' && line[length] != '=' && line[length] != '\r' &&
		line[length] != '\n' && length < (long)sizeof(ban->when) - 1)
	{
		length++;
	}
	if (line[length] == '\t' || !line[length] || line[length] == '\r' || line[length] == '\n')
	{
		csmemcpy(ban->when, line, length);
		ban->when[length] = 0;
	}
	return TRUE;
}

/* ---------- the host */

boolean network_host_controls_available(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct host_controls_seat seat;

	seat.server = server != NULL;
	seat.connection = (short)game_connection();
	seat.playing = server && game_in_progress() && !main_menu_is_loaded();
	seat.lobby = server && network_host_game_in_lobby();
	return network_host_controls_shown_for(&seat);
}

boolean network_host_controls_in_lobby(
	void)
{
	return network_host_controls_available() && network_host_game_in_lobby();
}

/* the player's machine's index in the game (the game record's) */
static long host_controls_player_game_machine(
	struct player_datum const *player)
{
	return player->network_player_data.machine_index;
}

static boolean host_controls_player_bot(
	struct player_datum const *player)
{
	return bots_machine_is_bot(host_controls_player_game_machine(player)) != 0;
}

/* the bots' host-owned bots in a network game (bots.c's): whether the bot
can be taken out by the host (it hosts the game, and not a matchmaking
game's, whose playlist fills it), and taken out, bots.count one less than
play so that none takes its place */
static boolean host_controls_bot_removable(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	return player && !player->quit_out_of_game && global_network_game_server_get() &&
		bots_player_is_bot(player_index) && !bots_matchmaking_on();
}

static boolean host_controls_bot_remove(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!player || !host_controls_bot_removable(player_index))
		return FALSE;
	network_host_controls_bots_one_fewer();
	return network_game_server_port_bots_remove(global_network_game_server_get(),
		host_controls_player_game_machine(player));
}

/* the game's OpenCE machines (the host knows each machine's join token) */
static short host_controls_opence_machines(
	void)
{
	long machines[MAXIMUM_HOST_CONTROLS_MACHINES];
	short count = distributed_client_machines(machines, MAXIMUM_HOST_CONTROLS_MACHINES);
	short opence = 0;
	short index;

	for (index = 0; index < count; index++)
	{
		if (network_standard_machine_is_opence(machines[index]))
			opence++;
	}
	return opence;
}

boolean network_host_controls_team_moves(
	void)
{
	struct host_controls_team_view view;

	if (!network_host_controls_available())
		return FALSE;
	view.standard = network_standard_hosting();
	view.teams = game_engine_has_teams();
	view.opence_machines = host_controls_opence_machines();
	return network_host_controls_team_moves_for(&view);
}

unsigned long network_host_controls_player_actions(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct host_controls_player_view view;

	/* (an admin's: the server's word, but its mute, which the scoreboard
	would take for this machine's) */
	if (!network_host_controls_available() && network_admin_remote())
	{
		struct host_controls_player entry;

		return network_admin_player(player_index, &entry) ? entry.actions & ~FLAG(_host_controls_mute) : 0;
	}
	if (!player || player->quit_out_of_game || !network_host_controls_available())
		return 0;
	view.bot = host_controls_player_bot(player);
	view.own = !view.bot && distributed_player_machine((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)) == NONE;
	view.bot_removable = view.bot && host_controls_bot_removable(player_index);
	view.teams = game_engine_has_teams();
	view.team_moves = network_host_controls_team_moves();
	return network_host_controls_actions_for(&view);
}

short network_host_controls_players(
	struct host_controls_player *players,
	short maximum)
{
	struct data_iterator iterator;
	struct player_datum *player;
	short count = 0;

	/* (the lobby's: its record's players, network_host_game.c) */
	if (network_host_controls_in_lobby())
		return network_host_game_lobby_players(players, maximum);
	if (!network_host_controls_available())
		return 0;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < maximum)
	{
		struct host_controls_player *entry = &players[count];
		short absolute_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		long machine_index = distributed_player_machine(absolute_index);

		if (player->quit_out_of_game)
			continue;
		entry->player_index = iterator.datum_index;
		entry->slot = player->network_player_data.player_list_index;
		entry->machine_index = machine_index;
		ustrncpy(entry->name, player->name, NUMBEROF(entry->name) - 1);
		entry->name[NUMBEROF(entry->name) - 1] = 0;
		entry->team = (short)player->team_index;
		entry->ping = latency_meter_player_ping(absolute_index);
		if (host_controls_player_bot(player))
			entry->tag = _host_controls_tag_bot;
		else if (machine_index == NONE)
			entry->tag = _host_controls_tag_host;
		else if (network_standard_machine_is_opence(machine_index))
			entry->tag = _host_controls_tag_opence;
		else if (network_standard_machine_is_openh1(machine_index))
			entry->tag = _host_controls_tag_openh1;
		/* (a Legacy game's: the host cannot tell OpenCE's from OpenH1's) */
		else
			entry->tag = _host_controls_tag_none;
		entry->actions = network_host_controls_player_actions(iterator.datum_index);
		count++;
	}
	return count;
}

boolean network_host_controls_kick(
	long player_index,
	boolean ban)
{
	unsigned long actions = network_host_controls_player_actions(player_index);
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!TEST_FLAG(actions, ban ? _host_controls_ban : _host_controls_kick) || !player)
		return FALSE;
	/* (an admin's: asked of the server, a ban for ever as the host's) */
	if (!network_host_controls_available())
	{
		struct host_controls_player entry;

		return network_admin_player(player_index, &entry) && network_admin_send(ban ? _network_admin_ban :
			_network_admin_kick, entry.slot, entry.name, 0, NULL);
	}
	if (host_controls_player_bot(player))
		return host_controls_bot_remove(player_index);
	/* (the console's kick and ban: network_server_manager.c) */
	return network_votekick_host_kick((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), ban);
}

/* a player's team on this machine: theirs, their unit's (a client's copy
of a living one, which the host's kill is on its way to), and the game
records' (the host's server's and its own client's, which the lobby and a
late join read) */
static void host_controls_team_set(
	struct player_datum *player,
	long team)
{
	struct network_game_server *server = global_network_game_server_get();
	void *client = global_network_game_client_get();
	long list_index = player->network_player_data.player_list_index;
	struct network_game *games[2];
	short index;

	player->team_index = team;
	player->network_player_data.team_index = (char)team;
	games[0] = server ? network_game_server_get_game(server) : NULL;
	games[1] = client ? network_game_client_get_game(client) : NULL;
	for (index = 0; index < NUMBEROF(games); index++)
	{
		if (games[index] && VALID_INDEX(list_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS) &&
			network_player_is_valid(&games[index]->players[list_index]))
		{
			games[index]->players[list_index].team_index = (char)team;
		}
	}
}

boolean network_host_controls_move_team(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct
	{
		struct distributed_message_header header;
		struct distributed_host_team move;
	} message;
	long machines[MAXIMUM_HOST_CONTROLS_MACHINES];
	short machine_count;
	short index;
	long team;
	long unit_index;

	if (!player || !TEST_FLAG(network_host_controls_player_actions(player_index), _host_controls_move_team))
		return FALSE;
	/* (an admin's: asked of the server) */
	if (!network_host_controls_available())
	{
		struct host_controls_player entry;

		return network_admin_player(player_index, &entry) &&
			network_admin_send(_network_admin_move_team, entry.slot, entry.name, 0, NULL);
	}
	team = player->team_index == 1 ? 0 : 1;
	/* (a living player dies first, as a player who quits does: no kill, no
	death counted, every machine seeing it as the host's other deaths) */
	unit_index = distributed_living_unit(player);
	if (unit_index != NONE)
		unit_kill_no_statistics(unit_index);
	host_controls_team_set(player, team);
	csmemset(&message, 0, sizeof(message));
	message.move.format = HOST_TEAM_FORMAT;
	message.move.player_index = distributed_player_to_byte(player_index);
	message.move.team = (byte)team;
	machine_count = distributed_client_machines(machines, MAXIMUM_HOST_CONTROLS_MACHINES);
	for (index = 0; index < machine_count; index++)
	{
		/* (each an OpenH1 machine of this protocol: team moves are offered in
		no game of OpenCE's machines, and its token is the protocol's) */
		if (network_standard_machine_is_openh1(machines[index]))
		{
			distributed_send_to_machine_reliably(machines[index], &message, _distributed_message_host_team, 1,
				(word)sizeof(message));
		}
	}
	return TRUE;
}

boolean network_host_controls_teams_locked(
	void)
{
	return host_controls.teams_locked && global_network_game_server_get() != NULL;
}

void network_host_controls_lock_teams(
	boolean locked)
{
	host_controls.teams_locked = locked;
}

char network_host_controls_lobby_team(
	boolean host_machine,
	char requested,
	char current)
{
	return network_host_controls_requested_team(network_host_controls_teams_locked(), host_machine, requested,
		current);
}

boolean network_host_controls_end_game(
	void)
{
	if (!network_host_controls_available() || !game_engine_running() || !game_engine_can_score())
		return FALSE;
	game_engine_end_game();
	return TRUE;
}

boolean network_host_controls_restart(
	void)
{
	if (!network_host_controls_end_game())
		return FALSE;
	host_controls.restart_stage = _host_controls_restart_ending;
	return TRUE;
}

boolean network_host_controls_restarting(
	void)
{
	return host_controls.restart_stage != _host_controls_restart_none;
}

short network_host_controls_bans(
	struct host_controls_ban *bans,
	char (*lines)[HOST_CONTROLS_BAN_LINE_SIZE],
	short maximum)
{
	short count = global_network_game_server_get() ? network_distributed_ban_lines(lines, maximum) : 0;
	short kept = 0;
	short index;

	/* (the lines that ban someone, in their order: newest last) */
	for (index = 0; index < count; index++)
	{
		if (network_host_controls_ban_parse(lines[index], &bans[kept]))
		{
			if (kept != index)
				csmemcpy(lines[kept], lines[index], sizeof(lines[kept]));
			kept++;
		}
	}
	return kept;
}

boolean network_host_controls_unban(
	char const *line)
{
	if (!global_network_game_server_get())
		return FALSE;
	return network_distributed_unban(line);
}

boolean network_host_controls_opence_joins(
	void)
{
	return network_standard_hosting() && network_standard_opence_joins_open();
}

void network_host_controls_set_opence_joins(
	boolean allowed)
{
	if (network_host_controls_available() && network_standard_hosting())
		network_standard_set_opence_joins(allowed);
}

/* ---------- the bots: Host Controls' BOTS tab (its rows: their count,
BOTS (NONE, ADD n or FILL TO n, as the lobby's BOTS), SKILL, TEAMS, BALANCE
TEAMS, TEAM TACTICS; then the bots that play).
Each row writes bots.c's setting to config.toml as it changes, which the
host reads as it goes: the lobby's bots join and leave at once
(network_server_manager.c's network_game_server_port_bots), a game in
progress' one at a time (network_game_server_port_bots_ingame, a bot joining
as a player who joins it in progress does), the skill and the team play
each tick (bots.c, bots_tactics.c). A matchmaking game's count, room and
fill are its playlist's (bots_set_matchmaking), and stay. */

enum
{
	/* (the BOTS choices' numbers: one at a time to HOST_BOT_CHOICE_STEPPED,
	then HOST_BOT_CHOICE_STEP at a time to BOTS_MAXIMUM; ADD's from 1,
	FILL TO's from 2) */
	HOST_BOT_CHOICE_STEPPED = 16,
	HOST_BOT_CHOICE_STEP = 4,
	HOST_BOT_ADD_LEAST = 1,
	HOST_BOT_FILL_LEAST = 2,
};

static char const *const host_controls_bot_skill_names[HOST_BOT_SKILLS] = { "easy", "normal", "heroic", "legendary" };

/* how many numbers a kind's choices have, from least */
static short host_controls_bot_numbers(
	long least)
{
	return (short)(HOST_BOT_CHOICE_STEPPED - least + 1 +
		(BOTS_MAXIMUM - HOST_BOT_CHOICE_STEPPED) / HOST_BOT_CHOICE_STEP);
}

/* the number of a kind's choices at index, from least */
static long host_controls_bot_number(
	long least,
	short index)
{
	long stepped = HOST_BOT_CHOICE_STEPPED - least + 1;

	return index < stepped ? least + index :
		HOST_BOT_CHOICE_STEPPED + (index - stepped + 1) * HOST_BOT_CHOICE_STEP;
}

short network_host_controls_bot_choice_count(
	void)
{
	return (short)(1 + host_controls_bot_numbers(HOST_BOT_ADD_LEAST) + host_controls_bot_numbers(HOST_BOT_FILL_LEAST));
}

short network_host_controls_bot_choice(
	short choice,
	long *number)
{
	short adds = host_controls_bot_numbers(HOST_BOT_ADD_LEAST);

	*number = 0;
	if (choice <= 0 || choice >= network_host_controls_bot_choice_count())
		return _host_bots_none;
	if (choice <= adds)
	{
		*number = host_controls_bot_number(HOST_BOT_ADD_LEAST, (short)(choice - 1));
		return _host_bots_add;
	}
	*number = host_controls_bot_number(HOST_BOT_FILL_LEAST, (short)(choice - 1 - adds));
	return _host_bots_fill;
}

void network_host_controls_bot_choice_words(
	short choice,
	wchar_t *text,
	short size)
{
	long number;
	short kind = network_host_controls_bot_choice(choice, &number);

	if (kind == _host_bots_add)
		usnprintf(text, size - 1, L"ADD %ld", number);
	else if (kind == _host_bots_fill)
		usnprintf(text, size - 1, L"FILL TO %ld", number);
	else
		usnprintf(text, size - 1, L"NONE");
	text[size - 1] = 0;
}

short network_host_controls_bot_choice_of(
	long count,
	boolean make_room,
	long fill_to)
{
	short adds = host_controls_bot_numbers(HOST_BOT_ADD_LEAST);
	short choices = network_host_controls_bot_choice_count();
	short kind = !count ? _host_bots_none :
		make_room && count >= MIN(fill_to - 1, BOTS_MAXIMUM) ? _host_bots_fill : _host_bots_add;
	long wanted = kind == _host_bots_fill ? fill_to : count;
	short best = 0;
	short choice;

	/* (the kind's choice of the most not past it, else its least: settings
	written by hand fall between the choices) */
	for (choice = 1; kind != _host_bots_none && choice < choices; choice++)
	{
		long number;

		if (network_host_controls_bot_choice(choice, &number) != kind)
			continue;
		if (!best || number <= wanted)
			best = choice;
	}
	return best;
}

short network_host_controls_bot_choice_step(
	short choice,
	short step)
{
	/* (no wrapping round: the ends stay, so that a count stepped past its
	most never becomes none) */
	long stepped = (long)choice + step;
	long last = network_host_controls_bot_choice_count() - 1;

	return (short)(stepped < 0 ? 0 : stepped > last ? last : stepped);
}

long network_host_controls_bot_count_step(
	long count,
	short step,
	long humans,
	long most_players)
{
	/* (no wrapping round: the fewest none, the most BOTS_MAXIMUM, or as
	many as the game holds beside its players) */
	long most = MIN(BOTS_MAXIMUM, MAX(most_players - humans, 0));
	long stepped = count + step;

	return stepped < 0 ? 0 : stepped > most ? most : stepped;
}

long network_host_controls_bot_count_removed(
	long count,
	long playing)
{
	/* (fewer playing than asked for, the game full or not yet started: one
	fewer than play, so that the bot taken out does not come back) */
	long left = MIN(count, playing) - 1;

	return left < 0 ? 0 : left;
}

short network_host_controls_bot_skill_named(
	char const *text)
{
	short skill;

	for (skill = 0; skill < HOST_BOT_SKILLS; skill++)
	{
		char const *name = host_controls_bot_skill_names[skill];
		short index;

		for (index = 0; name[index]; index++)
		{
			char character = text[index];

			if (character >= 'A' && character <= 'Z')
				character = (char)(character - 'A' + 'a');
			if (character != name[index])
				break;
		}
		if (!name[index] && (!text[index] || text[index] == ' ' || text[index] == ')' || text[index] == '"'))
			return skill;
	}
	return NONE;
}

boolean network_host_controls_bots_tab(
	void)
{
	/* (the host's alone, never in co-op: the bots play no campaign) */
	return network_host_controls_available() && !network_host_game_cooperative();
}

boolean network_host_controls_bots_play(
	void)
{
	/* (a game the bots play: Slayer's) */
	struct network_game_server *server = global_network_game_server_get();

	return server && bots_play_game_engine(network_game_server_get_game(server)->variant.game_engine_index);
}

/* the host's game's bots playing, and its players */
static long host_controls_bots_playing(
	long *humans)
{
	struct network_game_server *server = global_network_game_server_get();
	long bots = 0;
	long players = server ? network_game_server_port_bots_humans(server, &bots, NULL) : 0;

	if (humans)
		*humans = players;
	return bots;
}

short network_host_controls_bot_rows(
	short *rows,
	short maximum)
{
	short count = 0;
	short row;

	if (!network_host_controls_bots_tab())
		return 0;
	for (row = 0; row < NUMBER_OF_HOST_BOT_ROWS && count < maximum; row++)
	{
		/* (BALANCE TEAMS in a team game whose teams may be arranged) */
		if (row == _host_bot_row_balance && !network_host_game_teams_arrangeable())
			continue;
		rows[count++] = row;
	}
	return count;
}

void network_host_controls_bot_row(
	short row,
	wchar_t *label,
	wchar_t *value,
	wchar_t *help,
	short size)
{
	static wchar_t const *const skills[HOST_BOT_SKILLS] = { L"EASY", L"NORMAL", L"HEROIC", L"LEGENDARY" };
	boolean lobby = network_host_controls_in_lobby();
	boolean matchmaking = bots_matchmaking_on() != 0;
	long playing = host_controls_bots_playing(NULL);
	long number;
	short kind = network_host_controls_bot_choice(network_host_controls_bots_chosen(), &number);

	label[0] = value[0] = help[0] = 0;
	switch (row)
	{
	case _host_bot_row_count:
		usnprintf(label, size - 1, L"BOTS");
		network_host_controls_bot_choice_words(network_host_controls_bots_chosen(), value, size);
		if (matchmaking)
			usnprintf(help, size - 1, L"The playlist fills the game with bots. %ld play now.", playing);
		else if (kind == _host_bots_fill)
			usnprintf(help, size - 1, L"Bots fill the game to %ld players, and leave as players join: %ld play "
				L"now.", number, playing);
		else if (kind == _host_bots_add)
			usnprintf(help, size - 1, L"%ld bots play beside the players, who keep their places: %ld play now.",
				number, playing);
		else
			usnprintf(help, size - 1, L"No bots. ADD puts bots beside the players; FILL TO fills the empty "
				L"places with bots, who leave as players join.");
		break;
	case _host_bot_row_skill:
		usnprintf(label, size - 1, L"SKILL");
		usnprintf(value, size - 1, L"%s", skills[bots_skill() < HOST_BOT_SKILLS ? bots_skill() : 1]);
		usnprintf(help, size - 1, L"How well every bot plays: how soon it reacts, how well it aims. At once.");
		break;
	case _host_bot_row_teams:
		usnprintf(label, size - 1, L"TEAMS");
		usnprintf(value, size - 1, bots_teams() == _bots_teams_against ? L"AGAINST YOU" : L"EVEN");
		usnprintf(help, size - 1, lobby ? L"In a team game, bots on both teams, or all against you." :
			L"In a team game, bots on both teams, or all against you: the bots that join from now. BALANCE "
			L"TEAMS moves those playing.");
		break;
	case _host_bot_row_balance:
		usnprintf(label, size - 1, L"BALANCE TEAMS");
		usnprintf(help, size - 1, lobby ? L"Evens the teams' numbers, moving as few players as it can." :
			L"Evens the teams' numbers, and their scores. Those who move die and respawn.");
		break;
	case _host_bot_row_tactics:
		usnprintf(label, size - 1, L"TEAM TACTICS");
		usnprintf(value, size - 1, config_boolean("bots.tactics") ? L"ON" : L"OFF");
		usnprintf(help, size - 1, L"In a team game, a team's bots play together: they spread out, cover each "
			L"other and fall back together. At once.");
		break;
	}
	label[size - 1] = value[size - 1] = help[size - 1] = 0;
}

/* (a value stepped through its range: -1 left, 1 right or A; at its ends
no further, as the menus' spinners) */
static long host_controls_bot_step(
	long value,
	long least,
	long most,
	short step)
{
	value += step;
	return value < least ? least : value > most ? most : value;
}

/* a setting written: FALSE (said) if it could not be */
static boolean host_controls_bot_write(
	char const *name,
	char const *text)
{
	if (config_write(name, text))
		return TRUE;
	console_warning("host_controls: could not set %s", name);
	return FALSE;
}

short network_host_controls_bots_chosen(
	void)
{
	return network_host_controls_bot_choice_of(bots_wanted_count(), bots_make_room() != 0, bots_fill_to());
}

boolean network_host_controls_choose_bots(
	short kind,
	long number)
{
	char text[16];

	if (bots_matchmaking_on() || (kind != _host_bots_none && (number < 1 || number > BOTS_MAXIMUM)) ||
		(kind == _host_bots_fill && number < HOST_BOT_FILL_LEAST))
	{
		return FALSE;
	}
	/* (FILL TO: as many bots as fill the game, bots.count the most) */
	snprintf(text, sizeof(text), "%ld", kind == _host_bots_add ? number : kind == _host_bots_fill ? (long)BOTS_MAXIMUM : 0L);
	if (!host_controls_bot_write("bots.count", text))
		return FALSE;
	if (kind == _host_bots_none)
		return TRUE;
	if (kind == _host_bots_fill)
	{
		snprintf(text, sizeof(text), "%ld", number);
		if (!host_controls_bot_write("bots.fill_to", text))
			return FALSE;
	}
	return host_controls_bot_write("bots.make_room", kind == _host_bots_fill ? "true" : "false");
}

boolean network_host_controls_set_bots(
	long count)
{
	long humans;

	host_controls_bots_playing(&humans);
	if (!network_host_controls_bots_tab() || bots_matchmaking_on() || count < 0 ||
		count != network_host_controls_bot_count_step(count, 0, humans, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
	{
		return FALSE;
	}
	return network_host_controls_choose_bots(count ? _host_bots_add : _host_bots_none, count);
}

boolean network_host_controls_set_bot_skill(
	short skill)
{
	if (!network_host_controls_bots_tab() || skill < 0 || skill >= HOST_BOT_SKILLS)
		return FALSE;
	return host_controls_bot_write("bots.skill", host_controls_bot_skill_names[skill]);
}

boolean network_host_controls_bot_row_step(
	short row,
	short step)
{
	if (!network_host_controls_bots_tab())
		return FALSE;
	switch (row)
	{
	case _host_bot_row_count:
	{
		/* (the lobby's BOTS' choices, at the ends no further) */
		short chosen = network_host_controls_bots_chosen();
		short stepped = network_host_controls_bot_choice_step(chosen, step);
		long number;
		short kind = network_host_controls_bot_choice(stepped, &number);

		return stepped != chosen && network_host_controls_choose_bots(kind, number);
	}
	case _host_bot_row_skill:
	{
		short skill = (short)host_controls_bot_step(bots_skill(), 0, HOST_BOT_SKILLS - 1, step);

		return skill != bots_skill() && network_host_controls_set_bot_skill(skill);
	}
	case _host_bot_row_teams:
		return host_controls_bot_write("bots.teams", bots_teams() == _bots_teams_against ? "even" : "against");
	case _host_bot_row_tactics:
		return host_controls_bot_write("bots.tactics", config_boolean("bots.tactics") ? "false" : "true");
	default:
		/* (BALANCE TEAMS: Host Controls' own, host_controls_menu.inc) */
		return FALSE;
	}
}

short network_host_controls_bots(
	struct host_controls_player *bots,
	short maximum)
{
	static struct host_controls_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short count = network_host_controls_players(players, HALO_PORT_MAXIMUM_NETWORK_PLAYERS);
	short shown = 0;
	short index;

	for (index = 0; index < count && shown < maximum; index++)
	{
		if (players[index].tag == _host_controls_tag_bot)
			bots[shown++] = players[index];
	}
	return shown;
}

void network_host_controls_bots_one_fewer(
	void)
{
	/* (ADD those left, FILL TO too: the bots no longer fill the game, or
	the one taken out would come back) */
	long left = network_host_controls_bot_count_removed(bots_wanted_count(), host_controls_bots_playing(NULL));

	network_host_controls_choose_bots(left ? _host_bots_add : _host_bots_none, left);
}

/* ---------- opening */

boolean network_host_controls_open(
	long player_index,
	short tab)
{
	if (!network_host_controls_available() && !network_admin_remote())
	{
		console_warning("host_controls: only the host of a network game under way has them");
		return FALSE;
	}
	host_controls.opening = TRUE;
	host_controls.opening_player = player_index;
	host_controls.opening_tab = tab;
	/* (one shown takes the opening's tab and player as it is) */
	if (network_host_controls_shown())
		return TRUE;
	/* (the first local player's, whose keyboard and mouse the scoreboard's
	menu and the console are) */
	if (!ui_widget_load_by_name_or_tag(HOST_CONTROLS_SCREEN, NONE, NULL, 0, NONE, NONE, NONE))
	{
		host_controls.opening = FALSE;
		console_warning("host_controls: the menus have no Host Controls (display.menus)");
		return FALSE;
	}
	return TRUE;
}

/* the console's "host_controls unban <players or address>" */
static boolean host_controls_unban_named(
	char const *text)
{
	static struct host_controls_ban bans[MAXIMUM_HOST_CONTROLS_BANS];
	static char lines[MAXIMUM_HOST_CONTROLS_BANS][HOST_CONTROLS_BAN_LINE_SIZE];
	short count = network_host_controls_bans(bans, lines, MAXIMUM_HOST_CONTROLS_BANS);
	size_t length;
	short index;

	while (*text == ' ' || *text == '"')
		text++;
	length = strcspn(text, "\")");
	for (index = 0; length && index < count; index++)
	{
		if (!strncmp(bans[index].players, text, length) || !strncmp(bans[index].address, text, length))
			return network_host_controls_unban(lines[index]);
	}
	console_warning("host_controls: no ban's players or address begin with \"%.*s\"", (int)length, text);
	return FALSE;
}

boolean network_host_controls_open_command(
	char const *text)
{
	/* (host_controls_menu.inc's tabs, in their order) */
	static char const *const tabs[] = { "players", "game", "maps", "playlist", "bans", "bots", "rules" };
	struct data_iterator iterator;
	struct player_datum *player;
	long chosen = NONE;
	short tab = 0;
	short index;

	while (*text == ' ' || *text == '\t' || *text == '"' || *text == '(')
		text++;
	/* (Game's End game now and Restart round, at once) */
	if (!strncmp(text, "end", 3) && (!text[3] || text[3] == ' ' || text[3] == ')'))
		return network_host_controls_end_game();
	if (!strncmp(text, "restart", 7) && (!text[7] || text[7] == ' ' || text[7] == ')'))
		return network_host_controls_restart();
	/* (Bans' Unban: the first ban whose players or address begin with the rest) */
	if (!strncmp(text, "unban ", 6))
		return host_controls_unban_named(text + 6);
	for (index = 0; index < NUMBEROF(tabs); index++)
	{
		size_t length = strlen(tabs[index]);

		if (!strncmp(text, tabs[index], length) && (!text[length] || text[length] == ' ' || text[length] == ')'))
		{
			tab = index;
			text += length;
			break;
		}
	}
	while (*text == ' ' || *text == '"')
		text++;
	/* (the first player whose name begins with the rest, in either case) */
	if (*text && *text != ')' && network_host_controls_available())
	{
		data_iterator_new(&iterator, player_data);
		while (chosen == NONE && (player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			for (index = 0; text[index] && text[index] != '"' && text[index] != ')' &&
				index < (short)NUMBEROF(player->name); index++)
			{
				char a = (char)player_name_character_ascii(player->name[index]);
				char b = text[index];

				if ((a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) != (b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b))
					break;
			}
			if (!player->quit_out_of_game && (!text[index] || text[index] == '"' || text[index] == ')'))
				chosen = iterator.datum_index;
		}
		if (chosen == NONE)
		{
			console_warning("host_controls: no player's name begins with \"%s\"", text);
			return FALSE;
		}
	}
	return network_host_controls_open(chosen, tab);
}

boolean network_host_controls_shown(
	void)
{
	return host_controls.screen_time && system_milliseconds() - host_controls.screen_time < HOST_CONTROLS_SHOWN_TIME &&
		(network_host_controls_available() || network_admin_remote());
}

boolean network_host_controls_take_opening(
	long *player_index,
	short *tab)
{
	host_controls.screen_time = system_milliseconds();
	if (!host_controls.opening)
		return FALSE;
	host_controls.opening = FALSE;
	*player_index = host_controls.opening_player;
	*tab = host_controls.opening_tab;
	return TRUE;
}

/* ---------- each frame */

/* the lobby shown in the map choice's place (which holds the countdown),
and its countdown let go (milliseconds) */
static void host_controls_lobby_start(
	struct network_game_server *server,
	long countdown)
{
	ui_widgets_close_all();
	if (!ui_widget_load_by_name_or_tag(pc_menus_screen(HOST_CONTROLS_LOBBY), NONE, NULL, 0, NONE, NONE, NONE))
		error(_error_log, "host controls: could not show the lobby");
	network_game_server_pause_countdown(server, FALSE);
	network_game_server_begin_game_start_countdown(server, countdown);
}

void network_host_controls_update(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct host_controls_restart_view view;
	boolean playing = server && network_game_server_playing(server);
	short action;

	/* (the lock is the server's: a new one starts unlocked) */
	if (!server)
		host_controls.teams_locked = FALSE;
	network_host_game_update();
	network_host_rules_update();
	/* (a game played, its lobby reached after it by the host's A on its
	scores: the next game, a change asked for or the playlist's, set there
	and started; none, and the host picks it as before) */
	if (playing)
		host_controls.played = TRUE;
	else if (!server)
		host_controls.played = FALSE;
	else if (host_controls.played && host_controls.restart_stage == _host_controls_restart_none &&
		main_menu_is_loaded())
	{
		host_controls.played = FALSE;
		if (network_host_game_lobby_take(FALSE))
		{
			host_controls_lobby_start(server, HOST_CONTROLS_NEXT_COUNTDOWN);
			error(_error_log, "host controls: the next game starts from the lobby");
		}
	}
	if (host_controls.restart_stage == _host_controls_restart_none)
		return;
	view.server = server != NULL;
	view.postgame = server && game_engine_running() && game_engine_showing_postgame();
	view.menu_loaded = main_menu_is_loaded();
	view.pregame = server && !network_game_server_playing(server);
	host_controls.restart_stage = network_host_controls_restart_step(host_controls.restart_stage, &view, &action);
	switch (action)
	{
	case _host_controls_restart_to_lobby:
		/* (as the scores' A does for the host: game_engine.c) */
		error(_error_log, "host controls: restarting the round, every machine to the lobby");
		network_game_server_reset_to_pregame(server);
		break;
	case _host_controls_restart_start:
		/* (the lobby in the map choice's place, which held the countdown:
		the game's map and gametype again, or the one a change asked for
		now, in a few seconds) */
		host_controls.played = FALSE;
		network_host_game_lobby_take(TRUE);
		host_controls_lobby_start(server, HOST_CONTROLS_RESTART_COUNTDOWN);
		error(_error_log, "host controls: the round starts again from the lobby");
		break;
	default:
		break;
	}
}

/* ---------- the team move's message */

boolean network_host_controls_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_host_team;
}

void network_host_controls_handle_message(
	long machine_index,
	word const *message,
	word size,
	boolean from_stream)
{
	struct distributed_message_header header;
	struct distributed_host_team move;
	struct player_datum *player;
	long player_index;

	csmemcpy(&header, message, sizeof(header));
	/* (the host's alone, over its stream, in an OpenH1 game under way of
	teams; one move a message) */
	if (machine_index != NONE || !from_stream || game_connection() != _game_connection_network_client ||
		!network_standard_agreed_rate() || !game_in_progress() || !game_engine_has_teams() ||
		header.count != 1 || size != sizeof(header) + sizeof(move))
	{
		return;
	}
	csmemcpy(&move, (byte const *)message + sizeof(header), sizeof(move));
	if (move.format != HOST_TEAM_FORMAT)
	{
		if (!host_controls.other_format_noted)
			error(_error_log, "host controls: a team move of another format (%d), not taken", move.format);
		host_controls.other_format_noted = TRUE;
		return;
	}
	if (move.team >= HOST_CONTROLS_TEAMS)
		return;
	player_index = distributed_player_from_byte(move.player_index);
	player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	if (player)
		host_controls_team_set(player, move.team);
}
