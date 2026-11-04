/*
BOTS_LOBBY.C (test)

The real bots of a hosted game (network_server_manager.c's
network_game_server_port_bots and its neighbours, with network_game_manager.c's
players and bots.c's machines and names): who joins the lobby of a local or
network game, on which team, who leaves, how a network game's bots make room
for the players who join, in the lobby and in the game in progress, and what
the other machines are told of it (the game's own messages alone).
test_bots_lobby.py takes the constants and structures (config.inc) and the
code (under_test.inc).
*/

#include "harness.h"

#include <wchar.h>

typedef unsigned short word;
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define MILLISECONDS_PER_SECOND 1000
#define csmemset memset
#define csmemcpy memcpy
#define error(priority, ...) ((void)0)
#define network_event(...) ((void)0)
#define NETWORK_SERVER_MANAGER_FILE ""

#include "config.inc"

#define MAXIMUM_NETWORK_MACHINE_COUNT HALO_PORT_MAXIMUM_NETWORK_MACHINES
#define MAXIMUM_NETWORK_PLAYER_COUNT HALO_PORT_MAXIMUM_NETWORK_PLAYERS
#define NETWORK_GAME_PLAYER_SLOTS HALO_PORT_MAXIMUM_NETWORK_PLAYERS

struct game_variant
{
	short game_engine_index;
	struct
	{
		boolean teams;
	} universal_variant;
};

struct network_game
{
	struct game_variant variant;
	byte maximum_players;
	short player_count;
	struct network_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
};

struct network_game_server_client_machine
{
	short machine_index;
};

struct network_game_server
{
	word state;
	struct network_game game;
	boolean sent_start_game_message;
	boolean queued_player_valid;
	long waiting_player_count;
};

/* the world the bots ask */
static boolean local_game;
static long wanted;
static short teams_setting;
static boolean make_room;
static long fill_to;
static long sends;
static short next_color;
static long game_time;
static unsigned long clock_milliseconds;
/* each bot's score in the game (by its index) */
static long scores[BOTS_MAXIMUM];
/* the game's messages sent to every machine, by type, and the players
joined in the game in progress */
static long messages[64];
static long other_messages;
static long joined_ingame;

static boolean network_game_is_splitscreen_local(void) { return local_game; }
static long bots_wanted_count(void) { return wanted; }
static short bots_teams(void) { return teams_setting; }
/* (Basic Training's teams: not playing, the team the setting chose) */
static long bots_team(long bot_index, long players_team, long chosen_team) { return chosen_team; }
static int bots_make_room(void) { return make_room; }
static long bots_fill_to(void) { return fill_to; }
static long game_time_get(void) { return game_time; }
static unsigned long system_milliseconds(void) { return clock_milliseconds; }
static boolean network_game_player_slot_held(long player_index) { (void)player_index; return FALSE; }

/* the slots a host holds for a party's members on their way
(network_party.c's, test_party_invites.py's: here only how many, which the
bots that make room leave free); and the machines' own, none this machine's,
at no address */
static short held_slots;
static short network_party_held_slots(void) { return held_slots; }
static boolean network_party_slot_open(unsigned long address) { (void)address; return TRUE; }
static void network_party_slot_taken(long machine_index, unsigned long address)
{
	(void)machine_index;
	(void)address;
}
static boolean network_party_machines_together(long machine_index, unsigned long address, long other_machine_index)
{
	(void)machine_index;
	(void)address;
	(void)other_machine_index;
	return FALSE;
}
static short network_party_team_choice(short const *party_counts, short const *team_counts, short maximum)
{
	(void)party_counts;
	(void)team_counts;
	(void)maximum;
	return NONE;
}
static unsigned long network_game_server_client_machine_addresses[MAXIMUM_NETWORK_MACHINE_COUNT];
static boolean network_game_server_client_machine_is_local(struct network_game_server *server,
	struct network_game_server_client_machine *machine)
{
	(void)server;
	(void)machine;
	return FALSE;
}
static boolean player_name_is_unique(struct network_game_server *server, wchar_t const *name)
{
	(void)server;
	(void)name;
	return TRUE;
}
static void get_unique_random_name(struct network_game_server *server, struct network_player *player)
{
	(void)server;
	player->name[0] = L'H';
}
static void get_unique_random_color(struct network_game_server *server, struct network_player *player)
{
	(void)server;
	player->primary_color_index = next_color++;
}
static boolean network_game_server_send_game_data_pregame(struct network_game_server *server)
{
	(void)server;
	sends++;
	return TRUE;
}
static boolean network_game_server_send_player_joined_info_ingame(struct network_game_server *server,
	struct network_player *player)
{
	(void)server;
	(void)player;
	joined_ingame++;
	return TRUE;
}
static long last_message_type = NONE;
static void *create_network_game_message(long type, void const *message, long size)
{
	static char buffer[256];

	(void)message;
	(void)size;
	last_message_type = type;
	return buffer;
}
static boolean network_game_server_send_message_to_all_machines(struct network_game_server *server, void *message)
{
	(void)server;
	(void)message;
	if (last_message_type >= 0 && last_message_type < (long)NUMBEROF(messages))
		messages[last_message_type]++;
	else
		other_messages++;
	return TRUE;
}
static long network_game_server_port_bot_score(struct network_player const *player)
{
	long bot_index = player->machine_index - BOTS_FIRST_MACHINE;

	return VALID_INDEX(bot_index, BOTS_MAXIMUM) ? scores[bot_index] : 0;
}

/* (the game's own code, as its build compiles it: a char index, a char
machine index against the session's 128) */
#pragma clang diagnostic ignored "-Wchar-subscripts"
#pragma clang diagnostic ignored "-Wtautological-constant-out-of-range-compare"
#include "under_test.inc"

static struct network_game_server server;
static struct network_game_server_client_machine machines[BOTS_FIRST_MACHINE];

/* a pregame lobby of the game engine, room for `maximum`, and its players */
static void lobby(short game_engine_index, long maximum)
{
	long index;

	memset(&server, 0, sizeof(server));
	server.state = _network_game_server_state_pregame;
	server.game.variant.game_engine_index = game_engine_index;
	server.game.maximum_players = (byte)maximum;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		network_game_invalidate_player(&server.game.players[index]);
	for (index = 0; index < BOTS_FIRST_MACHINE; index++)
		machines[index].machine_index = (short)index;
	local_game = TRUE;
	teams_setting = _bots_teams_even;
	make_room = TRUE;
	fill_to = 16;
	sends = 0;
	joined_ingame = 0;
	other_messages = 0;
	memset(messages, 0, sizeof(messages));
	memset(scores, 0, sizeof(scores));
	clock_milliseconds = 100000;
}

/* the same, a network game's (system link, internet) */
static void network_lobby(short game_engine_index, long maximum)
{
	lobby(game_engine_index, maximum);
	local_game = FALSE;
}

static struct network_player *find(long machine_index, long controller_index)
{
	long index;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (network_player_is_valid(&server.game.players[index]) &&
			server.game.players[index].machine_index == machine_index &&
			server.game.players[index].controller_index == controller_index)
		{
			return &server.game.players[index];
		}
	}
	return NULL;
}

/* a player of this machine (0) on the team */
static struct network_player *player(long controller_index, long team_index)
{
	struct network_player joining;

	memset(&joining, 0, sizeof(joining));
	joining.name[0] = L'P';
	joining.machine_index = 0;
	joining.controller_index = (char)controller_index;
	joining.team_index = (char)team_index;
	joining.player_list_index = NONE;
	CHECK(network_game_add_player(&server.game, &joining), "player %ld not added", controller_index);
	return find(0, controller_index);
}

/* a machine's player joins as the server adds one (its team the smaller,
a bot making room for it): whether it could */
static boolean join(long machine_index)
{
	struct network_player joining;

	memset(&joining, 0, sizeof(joining));
	joining.name[0] = L'J';
	joining.name[1] = (wchar_t)(L'A' + machine_index);
	joining.machine_index = (char)machine_index;
	joining.primary_color_index = NONE;
	joining.player_list_index = NONE;
	return network_game_server_add_player_to_game(&server, &machines[machine_index], &joining);
}

/* a machine's player leaves */
static void leave(long machine_index)
{
	struct network_player *leaving = find(machine_index, 0);

	CHECK(leaving, "no player of machine %ld", machine_index);
	CHECK(network_game_remove_player(&server.game, leaving), "machine %ld's player did not leave", machine_index);
}

/* the bots in the game, and on each team */
static long bots_in(long *team0, long *team1)
{
	long index, count = 0;

	*team0 = *team1 = 0;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player *slot = &server.game.players[index];

		if (!network_player_is_valid(slot) || !bots_machine_is_bot(slot->machine_index))
			continue;
		count++;
		if (slot->team_index == 0)
			(*team0)++;
		else if (slot->team_index == 1)
			(*team1)++;
	}
	return count;
}

static long bots(void)
{
	long team0, team1;

	return bots_in(&team0, &team1);
}

static boolean bot_in(long bot_index)
{
	return find(BOTS_FIRST_MACHINE + bot_index, 0) != NULL;
}

/* the lobby's idle, as often as asked */
static void idle(long times)
{
	while (times-- > 0)
	{
		network_game_server_port_bots(&server);
		network_game_server_port_bots_ingame(&server);
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long team0, team1, count;

	/* as many as asked for, once: the lobby unchanged after (nothing sent
	again); more than 15 asked for is 15 */
	CASE("fills")
	{
		lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 6;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 6, "%ld bots, not 6", bots_in(&team0, &team1));
		CHECK(server.game.player_count == 7, "%d players, not 7", server.game.player_count);
		CHECK(sends == 1, "the lobby sent %ld times, not once", sends);
		network_game_server_port_bots(&server);
		CHECK(sends == 1 && server.game.player_count == 7, "the lobby changed again (%ld sends)", sends);
		wanted = 99;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 15, "%ld bots for 99, not 15", bots_in(&team0, &team1));
		return 0;
	}
	/* as far as the game has room */
	CASE("room")
	{
		lobby(game_engine_slayer, 4);
		player(0, 0);
		wanted = 6;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 3, "%ld bots in a game of 4, not 3", bots_in(&team0, &team1));
		CHECK(server.game.player_count == 4, "%d players in a game of 4", server.game.player_count);
		return 0;
	}
	/* fewer asked for: the last leave */
	CASE("fewer")
	{
		lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 6;
		network_game_server_port_bots(&server);
		wanted = 2;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 2, "%ld bots, not 2", bots_in(&team0, &team1));
		CHECK(bot_in(0) && bot_in(1) && !bot_in(2) && !bot_in(5), "not the first two that stayed");
		CHECK(server.game.player_count == 3, "%d players, not 3", server.game.player_count);
		wanted = 0;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 0 && server.game.player_count == 1, "bots stayed for none");
		return 0;
	}
	/* "even": the teams evened with the players */
	CASE("even")
	{
		lobby(game_engine_slayer, 16);
		player(0, 0);
		player(1, 0);
		wanted = 5;
		network_game_server_port_bots(&server);
		count = bots_in(&team0, &team1);
		CHECK(count == 5, "%ld bots, not 5", count);
		CHECK(team0 + 2 - team1 <= 1 && team1 - (team0 + 2) <= 1, "teams of %ld and %ld", team0 + 2, team1);
		return 0;
	}
	/* "against": every bot on the team the players are not on */
	CASE("against")
	{
		lobby(game_engine_slayer, 16);
		player(0, 1);
		teams_setting = _bots_teams_against;
		wanted = 4;
		network_game_server_port_bots(&server);
		count = bots_in(&team0, &team1);
		CHECK(count == 4 && team0 == 4, "%ld of %ld bots against the players' team 1", team0, count);
		return 0;
	}
	/* the players change teams in the lobby: the bots follow */
	CASE("follows-players")
	{
		struct network_player *human;

		lobby(game_engine_slayer, 16);
		human = player(0, 0);
		teams_setting = _bots_teams_against;
		wanted = 3;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 3 && team1 == 3, "%ld bots on team 1, not 3", team1);
		human->team_index = 1;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 3 && team0 == 3, "%ld bots on team 0 once the player moved, not 3", team0);
		CHECK(sends == 2, "the move sent %ld times", sends - 1);
		return 0;
	}
	/* a game other machines may join (system link, internet) takes them as
	a local one does */
	CASE("network-lobby")
	{
		network_lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 6;
		network_game_server_port_bots(&server);
		CHECK(bots() == 6, "%ld bots in a network game's lobby, not 6", bots());
		CHECK(sends == 1, "the lobby sent %ld times, not once", sends);
		return 0;
	}
	/* the gametypes bots.c plays (all but Race: Slayer's, and so SWAT,
	Infection and Gun Game, and the objectives'): none in Race, and those of
	a King of the Hill lobby leave when it becomes Race; a network game's as
	a local one's */
	CASE("not-race")
	{
		long round;

		for (round = 0; round < 2; round++)
		{
			if (round)
				network_lobby(game_engine_race, 16);
			else
				lobby(game_engine_race, 16);
			player(0, 0);
			wanted = 6;
			idle(1);
			CHECK(bots() == 0, "%ld bots in Race", bots());
			server.game.variant.game_engine_index = game_engine_ctf;
			idle(1);
			CHECK(bots() == 6, "%ld bots in CTF, not 6", bots());
			server.game.variant.game_engine_index = game_engine_king;
			idle(1);
			CHECK(bots() == 6, "%ld bots in King of the Hill, not 6", bots());
			server.game.variant.game_engine_index = game_engine_race;
			idle(1);
			CHECK(bots() == 0, "%ld bots stayed for Race", bots());
		}
		return 0;
	}
	/* the lobby's bots change nothing once the game is started; a local game
	in progress changes none */
	CASE("not-once-started")
	{
		lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 6;
		server.sent_start_game_message = TRUE;
		idle(1);
		CHECK(bots_in(&team0, &team1) == 0, "%ld bots joined a starting game", bots_in(&team0, &team1));
		server.sent_start_game_message = FALSE;
		server.state = _network_game_server_state_ingame;
		idle(3);
		CHECK(bots_in(&team0, &team1) == 0, "%ld bots joined a local game in progress", bots_in(&team0, &team1));
		return 0;
	}
	/* no bots before a player is in; a player of no team yet (between two
	games of other kinds) keeps them */
	CASE("no-player-no-bots")
	{
		struct network_player *human;

		lobby(game_engine_slayer, 16);
		wanted = 6;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 0 && server.game.player_count == 0, "bots in a lobby with no player");
		human = player(0, 0);
		network_game_server_port_bots(&server);
		human->team_index = 5;
		sends = 0;
		network_game_server_port_bots(&server);
		CHECK(bots_in(&team0, &team1) == 6 && sends <= 1, "%ld bots, %ld sends with the player of no team",
			bots_in(&team0, &team1), sends);
		return 0;
	}
	/* each bot the one player of a machine no real machine is, named, a
	colour of its own */
	CASE("names-and-machines")
	{
		long index, named = 0;

		lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 15;
		network_game_server_port_bots(&server);
		for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		{
			struct network_player *slot = &server.game.players[index];

			if (!network_player_is_valid(slot) || !bots_machine_is_bot(slot->machine_index))
				continue;
			CHECK(slot->controller_index == 0, "bot of machine %d on controller %d", slot->machine_index,
				slot->controller_index);
			CHECK(slot->machine_index >= BOTS_FIRST_MACHINE && slot->machine_index < BOTS_FIRST_MACHINE + BOTS_MAXIMUM,
				"bot of machine %d", slot->machine_index);
			CHECK(slot->primary_color_index != NONE, "bot of machine %d has no colour", slot->machine_index);
			if (slot->name[0])
				named++;
		}
		CHECK(named == 15, "%ld of 15 bots named", named);
		CHECK(!bots_machine_is_bot(0) && !bots_machine_is_bot(BOTS_FIRST_MACHINE - 1), "a real machine is a bot");
		CHECK(BOTS_FIRST_MACHINE + BOTS_MAXIMUM <= HALO_PORT_MAXIMUM_NETWORK_MACHINES,
			"the bots' machines past the session's");
		return 0;
	}
	/* make room (network): the bots fill the game to bots.fill_to; a player
	who joins a full lobby takes a bot's place at once, and a bot comes back
	as a player leaves */
	CASE("make-room-lobby")
	{
		long machine;

		network_lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 15;
		idle(1);
		CHECK(bots() == 15 && server.game.player_count == 16, "%ld bots, %d players: not 15 and 16", bots(),
			server.game.player_count);
		for (machine = 1; machine <= 5; machine++)
		{
			CHECK(network_game_server_has_room_for_player(&server), "no room for player %ld in a lobby of bots", machine);
			CHECK(join(machine), "player %ld could not join a lobby of bots", machine);
			CHECK(bots() == 15 - machine, "%ld bots once %ld joined, not %ld", bots(), machine, 15 - machine);
			idle(2);
			CHECK(bots() == 15 - machine && server.game.player_count == 16, "%ld bots, %d players after the idle",
				bots(), server.game.player_count);
		}
		leave(3);
		idle(1);
		CHECK(bots() == 11 && server.game.player_count == 16, "%ld bots once a player left, not 11", bots());
		CHECK(server.game.maximum_players == 16, "the most players %d, not 16", server.game.maximum_players);
		return 0;
	}
	/* bots.fill_to: the players and bots no more than it, and the game's most
	players */
	CASE("fill-to")
	{
		network_lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 10;
		fill_to = 6;
		idle(1);
		CHECK(bots() == 5, "%ld bots filling to 6 with one player, not 5", bots());
		join(1);
		join(2);
		idle(1);
		CHECK(bots() == 3, "%ld bots filling to 6 with 3 players, not 3", bots());
		fill_to = 100;
		idle(1);
		CHECK(bots() == 10, "%ld bots, not 10 (bots.count's)", bots());
		fill_to = 128;
		wanted = 15;
		idle(1);
		CHECK(bots() == 13 && server.game.player_count == 16, "%ld bots in a game of 16 with 3 players, not 13", bots());
		/* (a local game's are filled to it too: the lobby's FILL TO) */
		lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 10;
		fill_to = 4;
		idle(1);
		CHECK(bots() == 3, "%ld bots in a local game filled to 4, not 3", bots());
		return 0;
	}
	/* a party's members on their way (network_party.c's holds): the bots
	that make room leave the slots held free, and come back once they are
	not */
	CASE("party-held")
	{
		network_lobby(game_engine_slayer, 8);
		player(0, 0);
		wanted = 10;
		fill_to = 8;
		idle(1);
		CHECK(bots() == 7, "%ld bots filling to 8 with one player, not 7", bots());
		held_slots = 3;
		idle(1);
		CHECK(bots() == 4, "%ld bots with 3 slots held for a party, not 4: they leave for it", bots());
		join(1);
		held_slots = 2;
		idle(1);
		CHECK(bots() == 4 && server.game.player_count == 6, "%ld bots, %d players once a member came in", bots(),
			server.game.player_count);
		held_slots = 0;
		idle(1);
		CHECK(bots() == 6, "%ld bots once the holds lapsed, not 6", bots());
		/* (make room off: the bots take none of the players' slots anyway) */
		network_lobby(game_engine_slayer, 8);
		make_room = FALSE;
		player(0, 0);
		wanted = 5;
		held_slots = 3;
		idle(1);
		CHECK(bots() == 5, "%ld bots of a game whose bots stay, not 5", bots());
		held_slots = 0;
		return 0;
	}
	/* make room off: the bots stay, and the game takes its most players of
	players besides them */
	CASE("fixed-bots")
	{
		long machine;

		network_lobby(game_engine_slayer, 16);
		make_room = FALSE;
		player(0, 0);
		wanted = 10;
		idle(1);
		CHECK(bots() == 10 && server.game.maximum_players == 26, "%ld bots, the most players %d: not 10 and 26",
			bots(), server.game.maximum_players);
		for (machine = 1; machine < 16; machine++)
		{
			CHECK(network_game_server_has_room_for_player(&server), "no room for player %ld", machine + 1);
			CHECK(join(machine), "player %ld could not join", machine + 1);
			idle(1);
			CHECK(bots() == 10, "%ld bots once %ld players joined", bots(), machine + 1);
		}
		CHECK(!network_game_server_has_room_for_player(&server), "room for a 17th player in a game of 16");
		CHECK(network_game_server_port_bots_humans(&server, &count, &team0) == 16 && count == 10 && team0 == 16,
			"the listing's players, bots and most players not 16, 10 and 16");
		/* (the game's most players set again, as Server Setup does: taken) */
		server.game.maximum_players = 20;
		idle(1);
		CHECK(server.game.maximum_players == 30, "the most players %d for 20 and 10 bots", server.game.maximum_players);
		return 0;
	}
	/* the listing and the advertisement: the players alone, the bots apart,
	and the most players for players */
	CASE("listing-counts")
	{
		long listed_bots, maximum;

		network_lobby(game_engine_slayer, 16);
		player(0, 0);
		wanted = 10;
		idle(1);
		join(1);
		count = network_game_server_port_bots_humans(&server, &listed_bots, &maximum);
		CHECK(count == 2 && listed_bots == 10 && maximum == 16, "listed %ld players, %ld bots, %ld most", count,
			listed_bots, maximum);
		return 0;
	}
	/* a network game in progress: a player who joins takes a bot's place at
	once, every machine told by the game's own message; a bot comes back a
	while after a player leaves, not while a player waits to join */
	CASE("make-room-ingame")
	{
		network_lobby(game_engine_slayer, 8);
		player(0, 0);
		wanted = 7;
		idle(1);
		server.state = _network_game_server_state_ingame;
		clock_milliseconds += 10000;
		idle(3);
		CHECK(bots() == 7 && joined_ingame == 0 && messages[_message_server_remove_player_ingame] == 0,
			"the game in progress changed its bots");
		CHECK(network_game_server_has_room_for_player(&server), "a game of bots does not take a player");
		CHECK(join(1), "a player could not join a game of bots");
		CHECK(bots() == 6 && messages[_message_server_remove_player_ingame] == 1,
			"%ld bots, %ld removals once a player joined", bots(), messages[_message_server_remove_player_ingame]);
		idle(5);
		CHECK(bots() == 6 && joined_ingame == 0, "the bots changed again (%ld bots)", bots());
		clock_milliseconds += 10000;
		idle(1);
		leave(1);
		idle(3);
		CHECK(bots() == 6 && joined_ingame == 0, "a bot came back at once as the player left");
		clock_milliseconds += 5000;
		server.waiting_player_count = 1;
		idle(3);
		CHECK(bots() == 6 && joined_ingame == 0, "a bot came back while a player waits to join");
		server.waiting_player_count = 0;
		idle(1);
		CHECK(bots() == 7 && joined_ingame == 1, "%ld bots, %ld joined after a player left", bots(), joined_ingame);
		idle(3);
		CHECK(bots() == 7 && joined_ingame == 1, "more bots came back (%ld)", bots());
		/* (and more asked for in the game: they come one at a time) */
		server.game.maximum_players = 16;
		wanted = 9;
		idle(1);
		CHECK(bots() == 7, "a bot came back at once after the last");
		clock_milliseconds += 5000;
		idle(1);
		CHECK(bots() == 8 && joined_ingame == 2, "%ld bots, not 8", bots());
		CHECK(other_messages == 0, "a message not the game's own");
		return 0;
	}
	/* the bot that leaves in a game in progress: of the team with more
	players, the lowest scoring; in a free for all the lowest scoring */
	CASE("leaver")
	{
		long index;

		network_lobby(game_engine_slayer, 7);
		server.game.variant.universal_variant.teams = TRUE;
		player(0, 0);
		wanted = 6;
		idle(1);
		bots_in(&team0, &team1);
		CHECK(team0 == 3 && team1 == 3, "teams of %ld and %ld bots, not 3 and 3", team0, team1);
		server.state = _network_game_server_state_ingame;
		/* (team 0's, with the player, more: its bot 3 the lowest of it; team
		1's lower still, but its team the smaller) */
		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			struct network_player *bot = find(BOTS_FIRST_MACHINE + index, 0);

			scores[index] = bot && bot->team_index == 0 ? 10 + index : -5;
		}
		scores[3] = 2;
		CHECK(find(BOTS_FIRST_MACHINE + 3, 0)->team_index == 0, "bot 4 not on team 0");
		join(1);
		bots_in(&team0, &team1);
		CHECK(bots() == 5 && !bot_in(3), "not the lowest scoring bot of the team with more players that left");
		CHECK(team0 + 2 - team1 <= 1 && team1 - (team0 + 2) <= 1, "teams of %ld and %ld once a player joined",
			team0 + 2, team1);
		/* (a free for all's: the lowest scoring) */
		network_lobby(game_engine_slayer, 8);
		player(0, 0);
		wanted = 7;
		idle(1);
		server.state = _network_game_server_state_ingame;
		for (index = 0; index < BOTS_MAXIMUM; index++)
			scores[index] = 10 + index;
		scores[4] = -2;
		join(1);
		CHECK(bots() == 6 && !bot_in(4), "not the lowest scoring bot that left a free for all");
		return 0;
	}
	/* the host takes a bot out (Host Controls, bots.count one less): it
	leaves, the others stay as they are, and none takes its place; in a game
	in progress every machine told */
	CASE("host-removes-a-bot")
	{
		long round;

		for (round = 0; round < 2; round++)
		{
			network_lobby(game_engine_slayer, 16);
			player(0, 0);
			wanted = 5;
			idle(1);
			if (round)
				server.state = _network_game_server_state_ingame;
			CHECK(network_game_server_port_bots_remove(&server, BOTS_FIRST_MACHINE + 2), "bot 3 not removed");
			CHECK(!network_game_server_port_bots_remove(&server, BOTS_FIRST_MACHINE + 2), "bot 3 removed twice");
			CHECK(!network_game_server_port_bots_remove(&server, 0), "a player removed as a bot");
			wanted = 4;
			sends = 0;
			clock_milliseconds += 10000;
			idle(3);
			CHECK(bots() == 4 && !bot_in(2) && bot_in(0) && bot_in(1) && bot_in(3) && bot_in(4),
				"the bots changed after the host took bot 3 out (%ld)", bots());
			CHECK(!round || messages[_message_server_remove_player_ingame] == 1, "the game in progress not told");
		}
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
