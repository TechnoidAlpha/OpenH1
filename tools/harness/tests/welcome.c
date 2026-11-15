/*
WELCOME.C (test)

The host's welcome to OpenCE players, port/linux/game/network_welcome.c,
played between a host and its client machines: an OpenCE client shows
every notice the host sends (OpenCE's network_chat_handle_down), an OpenH1
client every one but the welcome's (network_welcome_line). Each OpenCE
player sees the two lines once, after they loaded and a few seconds on;
an OpenH1 player never. test_welcome.py takes the code (under_test.inc:
network_welcome.c from its constants on) and the chat's line cleaning and
size (config.inc).
*/

#include "harness.h"

#ifndef NUMBEROF
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#endif
#define csstrcmp strcmp
#define csmemset memset

#include "config.inc"

/* ---------- the host's world */

enum
{
	_game_connection_local,
	_game_connection_network_client,
	_game_connection_network_server,
};

enum
{
	_error_log,
};

#define MAXIMUM_MACHINES 8
#define MAXIMUM_SENT 64

static short connection = _game_connection_network_server;
static boolean setting = TRUE;
static unsigned long now = 1000;

/* the client machines in the game: whether each is, its address, its
hardware id, and whether it runs OpenH1 */
static struct
{
	boolean present;
	unsigned long address;
	char hardware_id[33];
	boolean openh1;
	/* ... and whether it joined with an OpenH1 game's token, which the host
	knows it by (network_standard.c) */
	boolean openh1_token;
	/* the lines it showed */
	char shown[MAXIMUM_SENT][CHAT_TEXT_SIZE];
	short shown_count;
} machines[MAXIMUM_MACHINES];

static short game_connection(
	void)
{
	return connection;
}

static int config_boolean(
	const char *name)
{
	return !strcmp(name, "network.opence_welcome") && setting;
}

static unsigned long system_milliseconds(
	void)
{
	return now;
}

static void error(
	int priority,
	char const *format,
	...)
{
	(void)priority;
	(void)format;
}

static short distributed_client_machines(
	long *machine_indices,
	short maximum)
{
	short count = 0;
	long index;

	for (index = 0; index < MAXIMUM_MACHINES && count < maximum; index++)
	{
		if (machines[index].present)
			machine_indices[count++] = index;
	}
	return count;
}

static unsigned long distributed_machine_real_address(
	long machine_index)
{
	return machines[machine_index].address;
}

static char const *network_game_server_machine_hardware_id(
	long machine_index)
{
	return machines[machine_index].hardware_id;
}

static void p2p_hardware_id_sanitize(
	char *destination,
	int size,
	const char *source)
{
	snprintf(destination, (size_t)size, "%s", source);
}

static boolean network_standard_machine_is_openh1(
	long machine_index)
{
	return machines[machine_index].openh1_token;
}

/* the notices the host sent, to any machine */
static short notices_sent;

static boolean network_welcome_line(char const *text);

/* (the host's chat) a notice to one client machine, which shows it as its
build does */
static void network_chat_host_notice(
	long machine_index,
	char const *text)
{
	char cleaned[CHAT_TEXT_SIZE];

	CHECK(machine_index >= 0 && machine_index < MAXIMUM_MACHINES && machines[machine_index].present,
		"a notice to machine %ld, which is not in the game", machine_index);
	CHECK(strlen(text) < CHAT_TEXT_SIZE, "a line longer than the chat's: '%s'", text);
	notices_sent++;
	chat_text_clean(cleaned, text, CHAT_TEXT_SIZE);
	if (machines[machine_index].openh1 && network_welcome_line(cleaned))
		return;
	CHECK(machines[machine_index].shown_count < MAXIMUM_SENT, "too many lines");
	strcpy(machines[machine_index].shown[machines[machine_index].shown_count++], cleaned);
}

#include "under_test.inc"

/* ---------- the cases */

static void join(
	long machine_index,
	unsigned long address,
	char const *hardware_id,
	boolean openh1)
{
	memset(&machines[machine_index], 0, sizeof(machines[machine_index]));
	machines[machine_index].present = TRUE;
	machines[machine_index].address = address;
	snprintf(machines[machine_index].hardware_id, sizeof(machines[machine_index].hardware_id), "%s", hardware_id);
	machines[machine_index].openh1 = openh1;
}

/* the host's ticks for some milliseconds */
static void play(
	unsigned long milliseconds)
{
	unsigned long end = now + milliseconds;

	for (; now != end; now += 33)
	{
		if (end - now < 33)
			now = end;
		network_welcome_host_tick();
		if (now == end)
			break;
	}
}

/* a new game, every machine loading it */
static void new_game(
	void)
{
	long index;

	network_welcome_new_game();
	for (index = 0; index < MAXIMUM_MACHINES; index++)
	{
		if (machines[index].present)
			network_welcome_machine_loaded(index);
	}
}

/* a machine showed the welcome, its two lines in order, once */
static void check_welcomed(
	long machine_index)
{
	CHECK(machines[machine_index].shown_count == 2, "machine %ld showed %d lines, not the welcome's 2", machine_index,
		machines[machine_index].shown_count);
	CHECK(!strcmp(machines[machine_index].shown[0], "This server runs GOAT H1."), "'%s'",
		machines[machine_index].shown[0]);
	CHECK(!strcmp(machines[machine_index].shown[1],
		"Get it free at goath1.com for smoother 60 and 120 tick games."), "'%s'", machines[machine_index].shown[1]);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* each line is a whole line of the chat: kept as it is when cleaned */
	CASE("lines-fit")
	{
		short line;

		for (line = 0; line < (short)NUMBEROF(welcome_lines); line++)
		{
			char cleaned[CHAT_TEXT_SIZE];

			CHECK(strlen(welcome_lines[line]) <= CHAT_TEXT_SIZE - 1, "line %d is %d characters", line,
				(int)strlen(welcome_lines[line]));
			chat_text_clean(cleaned, welcome_lines[line], CHAT_TEXT_SIZE);
			CHECK(!strcmp(cleaned, welcome_lines[line]), "line %d cleaned to '%s'", line, cleaned);
			CHECK(network_welcome_line(cleaned), "line %d is not the welcome's", line);
		}
		CHECK(!network_welcome_line("Bob muted for 20 minutes"), "another notice taken for the welcome");
		CHECK(!network_welcome_line(""), "an empty notice taken for the welcome");
		return 0;
	}

	/* an OpenCE player sees the welcome once, an OpenH1 player never: not
	before they have loaded, nor again in the next game */
	CASE("once")
	{
		join(1, 0x0A000001, "aa11", FALSE);
		join(2, 0x0A000002, "bb22", TRUE);
		play(5000);
		CHECK(machines[1].shown_count == 0, "welcomed before loading the game");
		new_game();
		play(10000);
		check_welcomed(1);
		CHECK(machines[2].shown_count == 0, "the OpenH1 machine showed %d lines", machines[2].shown_count);
		new_game();
		play(10000);
		new_game();
		play(10000);
		check_welcomed(1);
		CHECK(machines[2].shown_count == 0, "the OpenH1 machine showed %d lines", machines[2].shown_count);
		return 0;
	}

	/* a machine that joined an OpenH1 game with its token (an OpenH1
	machine, as the host knows it) is sent nothing; one that joined with
	OpenCE's, the welcome */
	CASE("openh1-token")
	{
		join(1, 0x0A000001, "aa11", FALSE);
		join(2, 0x0A000002, "bb22", TRUE);
		machines[2].openh1_token = TRUE;
		new_game();
		play(10000);
		check_welcomed(1);
		CHECK(notices_sent == 2, "%d notices sent, not the OpenCE machine's 2", notices_sent);
		return 0;
	}

	/* a few seconds after loading, not as the machine loads */
	CASE("delay")
	{
		join(3, 0x0A000003, "cc33", FALSE);
		new_game();
		play(WELCOME_DELAY_MILLISECONDS / 2);
		CHECK(machines[3].shown_count == 0, "welcomed %d ms after loading", WELCOME_DELAY_MILLISECONDS / 2);
		play(WELCOME_DELAY_MILLISECONDS);
		check_welcomed(3);
		return 0;
	}

	/* a player who leaves and joins again (at another index) is not
	welcomed again; another player at their old index is */
	CASE("rejoin")
	{
		join(1, 0x0A000001, "aa11", FALSE);
		new_game();
		play(10000);
		check_welcomed(1);
		machines[1].present = FALSE;
		join(4, 0x0A000001, "aa11", FALSE);
		network_welcome_machine_loaded(4);
		play(10000);
		CHECK(machines[4].shown_count == 0, "welcomed again joining again");
		join(1, 0x0A000001, "dd44", FALSE);
		network_welcome_machine_loaded(1);
		play(10000);
		check_welcomed(1);
		return 0;
	}

	/* a machine gone before its welcome, and another new at its index that
	has not loaded yet: neither welcomed (the new one when it loads) */
	CASE("replaced")
	{
		join(5, 0x0A000005, "ee55", FALSE);
		new_game();
		play(1000);
		join(5, 0x0A000006, "ff66", FALSE);
		play(10000);
		CHECK(machines[5].shown_count == 0, "a machine welcomed before it loaded (its index's last one's welcome)");
		network_welcome_machine_loaded(5);
		play(10000);
		check_welcomed(5);
		return 0;
	}

	/* a machine loaded as the game ends is welcomed in the game it loads
	next, not while loading it */
	CASE("game-ends")
	{
		join(6, 0x0A000007, "1177", FALSE);
		new_game();
		play(1000);
		network_welcome_new_game();
		play(10000);
		CHECK(machines[6].shown_count == 0, "welcomed while loading the next game");
		network_welcome_machine_loaded(6);
		play(10000);
		check_welcomed(6);
		return 0;
	}

	/* network.opence_welcome false: no one */
	CASE("off")
	{
		setting = FALSE;
		join(1, 0x0A000001, "aa11", FALSE);
		new_game();
		play(10000);
		CHECK(machines[1].shown_count == 0, "welcomed with the setting off");
		return 0;
	}

	/* only a host welcomes */
	CASE("client")
	{
		connection = _game_connection_network_client;
		join(1, 0x0A000001, "aa11", FALSE);
		new_game();
		play(10000);
		CHECK(machines[1].shown_count == 0, "a client welcomed a machine");
		return 0;
	}

	/* the clock's wrap */
	CASE("clock-wrap")
	{
		now = 0xFFFFFFFFUL - 1000;
		join(1, 0x0A000001, "aa11", FALSE);
		new_game();
		play(1000);
		CHECK(machines[1].shown_count == 0, "welcomed at once across the clock's wrap");
		play(10000);
		check_welcomed(1);
		return 0;
	}

	/* the players remembered, the oldest forgotten once every place is
	held, the newest kept */
	CASE("remember-full")
	{
		struct welcome_player players[3];
		struct welcome_player player;
		short count = 0;
		short oldest = 0;
		unsigned long address;

		memset(&player, 0, sizeof(player));
		for (address = 1; address <= 4; address++)
		{
			player.address = address;
			welcome_player_remember(players, &count, &oldest, 3, &player);
		}
		CHECK(count == 3, "%d remembered of 3", count);
		player.address = 1;
		CHECK(!welcome_player_welcomed(players, count, &player), "the oldest kept past the newest");
		for (address = 2; address <= 4; address++)
		{
			player.address = address;
			CHECK(welcome_player_welcomed(players, count, &player), "player %lu forgotten", address);
		}
		return 0;
	}

	fprintf(stderr, "no case '%s'\n", case_name);
	return 2;
}
