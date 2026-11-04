/*
NETWORK_HOST_GAME.C

The next game of a network game a player hosts (network_host_game.h): what
a dedicated server's director does between games (server_director.c), for
a host who plays. Only the host decides, by the lobby's own messages, which
every machine (OpenCE's too) understands: nothing new is sent.

- A change of map and gametype (Host Controls' MAPS, the console's
  host_map) is an entry of the rotation's grammar (server_rotation.c:
  map[:gametype[+rule=value...]]), checked as the server checks it (its map
  on disk, a Custom Edition one's in custom_maps; its gametype built,
  server_gametypes.c). A map alone (MAPS' CURRENT) keeps the game's
  gametype, its score to win, time limit and rules (the lobby's record as it
  is when the change is set); a playlist's entry without one is Slayer, as
  the server's rotation's is. In the lobby it is set at once, as the director sets
  its entry (the map's name and the variant in the host's game record, sent
  to every machine; map sharing gives it to those without it). In a game,
  NOW ends the game and starts the new one as Restart round starts it
  (network_host_controls.c: no scores, every machine to the lobby, its
  countdown let go); AFTER THIS GAME waits for the game's end, its scores,
  and the lobby the host's A brings every machine to.
- The playlist (network.host_playlist, the rotation's text) gives the game
  after each one: in the lobby after a game (but a restart's) its next entry
  is set and the countdown started. A change asked for comes first, and the
  playlist goes on after it.
- The score to win and the time limit change only in the lobby, as Server
  Setup changes them: asked for during a game, they wait for the next
  game's lobby (no machine of the Xbox's game changes them in a game).
- As a game's scores show, every machine is told what plays next (the
  host's notice, as the dedicated server's).

Not a dedicated server's (halo --server has its rotation and sv_map), nor a
game of this machine alone's. The pure part (network_host_game_playlist_after)
is tools/harness/tests/test_host_game.py's.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "interface/player_ui.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "text/unicode.h"
#include "game/players.h"
#include "network_distributed.h"
#include "network_chat.h"
#include "network_standard.h"
#include "network_host_controls.h"
#include "game_modes.h"
#include "network_coop.h"
#include "custom_edition_maps.h"
#include "bots.h"
#include "network_host_game.h"
#include "network_host_rules.h"
#include "network_admin.h"
#include "server_gametypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* port_config.c's, console.c's, server_mode.c's, menu_functions.c's */
const char *config_string(const char *name);
int config_write(const char *name, const char *value);
void console_warning(const char *format, ...);
void console_printf(boolean clear, const char *format, ...);
int halo_server_mode(void);
unsigned long system_milliseconds(void);
void menu_map_shown(char const *level, wchar_t *text, short size);
/* network_server_manager.c's, network_server_message_handler.c's (the
host's game record, the lobby's sent) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
boolean network_game_server_send_game_data_pregame(struct network_game_server *server);

/* ---------- constants */

enum
{
	/* the maps and gametypes read again after (milliseconds) */
	HOST_GAME_LISTS_INTERVAL = 10000,
	/* the players the teams are arranged of, at most, and the teams */
	MAXIMUM_HOST_GAME_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	HOST_GAME_TEAMS = 2,
	/* a notice's text, at most (network_distributed.c's) */
	HOST_GAME_NOTICE_SIZE = 128,
	/* a mute's minutes, at most (network_chat.c's: a day) */
	HOST_GAME_MUTE_MOST_MINUTES = 24 * 60,
};

/* ---------- globals */

static struct
{
	/* the playlist (network.host_playlist), as last read, and its text */
	struct server_rotation playlist;
	char playlist_text[SERVER_ROTATION_MAXIMUM_TEXT];
	boolean playlist_read;
	/* its entry played last (NONE: none yet) */
	long index;
	/* a change asked for: now or after this game, and the playlist's entry
	it is (NONE: none) */
	struct server_entry next;
	boolean has_next;
	boolean next_now;
	long next_index;
	/* the limits asked for (NONE: as they are) */
	long score;
	short minutes;
	/* the scores' notice said */
	boolean over_said;
	/* the maps and gametypes, and when they were read */
	struct host_game_map maps[MAXIMUM_HOST_GAME_MAPS];
	short map_count;
	long maps_left_out;
	struct host_game_gametype gametypes[MAXIMUM_HOST_GAME_GAMETYPES];
	short gametype_count;
	short builtin_gametype_count;
	unsigned long lists_time;
	boolean lists_read;
} host_game = { .index = NONE, .next_index = NONE, .score = NONE, .minutes = NONE };

/* ---------- the pure part */

long network_host_game_playlist_after(
	struct server_rotation const *playlist,
	long index)
{
	long step;

	if (playlist->count <= 0)
		return NONE;
	if (index < 0 || index >= playlist->count)
		index = playlist->count - 1;
	for (step = 1; step <= playlist->count; step++)
	{
		long candidate = (index + step) % playlist->count;

		if (!playlist->entries[candidate].rejected[0])
			return candidate;
	}
	return NONE;
}

long network_host_game_duration_parse(
	char const *text)
{
	char const *end = text;
	long number = 0;
	long unit;

	if (!csstrcmp(text, "forever") || !csstrcmp(text, "permanent") || !csstrcmp(text, "0"))
		return 0;
	if (*text < '0' || *text > '9')
		return NONE;
	while (*end >= '0' && *end <= '9')
	{
		number = number * 10 + (*end - '0');
		if (number > 100000)
			return NONE;
		end++;
	}
	switch (*end)
	{
	case 0: case 'm': unit = 60; break;
	case 'h': unit = 60 * 60; break;
	case 'd': unit = 24 * 60 * 60; break;
	case 'w': unit = 7 * 24 * 60 * 60; break;
	default: return NONE;
	}
	if ((*end && end[1]) || number <= 0)
		return NONE;
	/* (ten years at most: longer is for ever) */
	if (number > 10L * 365 * 24 * 60 * 60 / unit)
		return 0;
	return number * unit;
}

void network_host_game_teams_plan(
	short count,
	long const *scores,
	char const *current,
	short how,
	unsigned long seed,
	char *teams)
{
	short order[MAXIMUM_HOST_GAME_PLAYERS];
	long totals[HOST_GAME_TEAMS] = { 0, 0 };
	short counts[HOST_GAME_TEAMS] = { 0, 0 };
	short most = (short)((count + 1) / 2);
	boolean scored = FALSE;
	boolean arranged = TRUE;
	short index, kept;

	count = (short)PIN(count, 0, MAXIMUM_HOST_GAME_PLAYERS);
	for (index = 0; index < count; index++)
	{
		order[index] = index;
		scored |= scores && scores[index] != 0;
	}
	if (how == _host_game_shuffle)
	{
		/* (in an order drawn from the seed, every other one on each team) */
		for (index = (short)(count - 1); index > 0; index--)
		{
			short other, swapped;

			seed = seed * 1664525UL + 1013904223UL;
			other = (short)((seed >> 8) % (unsigned long)(index + 1));
			swapped = order[index];
			order[index] = order[other];
			order[other] = swapped;
		}
		for (index = 0; index < count; index++)
			teams[order[index]] = (char)(index % HOST_GAME_TEAMS);
	}
	else if (scored)
	{
		/* (the best first, each to the team behind, as many on each) */
		for (index = 1; index < count; index++)
		{
			short moved = order[index];
			short place = index;

			while (place > 0 && scores[order[place - 1]] < scores[moved])
			{
				order[place] = order[place - 1];
				place--;
			}
			order[place] = moved;
		}
		for (index = 0; index < count; index++)
		{
			short player = order[index];
			short team = totals[0] < totals[1] ? 0 : totals[1] < totals[0] ? 1 : counts[0] <= counts[1] ? 0 : 1;

			if (counts[team] >= most)
				team = (short)(1 - team);
			teams[player] = (char)team;
			totals[team] += scores[player];
			counts[team]++;
		}
	}
	else
	{
		/* (each where they are, those on no team on the smaller, then the
		last of the larger moved till they differ by one at most) */
		arranged = FALSE;
		for (index = 0; index < count; index++)
		{
			teams[index] = current[index];
			if (teams[index] == 0 || teams[index] == 1)
				counts[(short)teams[index]]++;
		}
		for (index = 0; index < count; index++)
		{
			if (teams[index] != 0 && teams[index] != 1)
			{
				teams[index] = (char)(counts[0] <= counts[1] ? 0 : 1);
				counts[(short)teams[index]]++;
			}
		}
		for (index = (short)(count - 1); index >= 0 && (counts[0] - counts[1] > 1 || counts[1] - counts[0] > 1); index--)
		{
			short larger = counts[0] > counts[1] ? 0 : 1;

			if (teams[index] == larger)
			{
				teams[index] = (char)(1 - larger);
				counts[larger]--;
				counts[1 - larger]++;
			}
		}
	}
	/* (the teams' colours swapped when that leaves more players where they
	were) */
	if (arranged)
	{
		for (index = 0, kept = 0; index < count; index++)
			kept += teams[index] == current[index] ? 1 : 0;
		if (kept * 2 < count)
		{
			for (index = 0; index < count; index++)
				teams[index] = (char)(1 - teams[index]);
		}
	}
}

unsigned long network_host_game_lobby_takes(
	boolean cooperative,
	boolean has_next,
	boolean next_now,
	boolean restarting)
{
	unsigned long takes = 0;

	/* (co-op's lobby has the campaign's next level: nothing over it) */
	if (cooperative)
		return 0;
	/* (a restart replays its game, but for a change asked for now) */
	if (has_next && (!restarting || next_now))
		SET_FLAG(takes, _host_game_take_change, TRUE);
	if (!restarting)
		SET_FLAG(takes, _host_game_take_playlist, TRUE);
	return takes;
}

long network_host_game_playlist_try(
	struct server_rotation const *playlist,
	long index,
	boolean (*playable)(struct server_entry const *entry, void *context),
	void *context)
{
	long tried;

	/* (each entry once at most: a playlist of none that can be played
	gives none) */
	for (tried = 0; tried < playlist->count; tried++)
	{
		long next = network_host_game_playlist_after(playlist, index);

		if (next == NONE)
			return NONE;
		if (playable(&playlist->entries[next], context))
			return next;
		index = next;
	}
	return NONE;
}

boolean network_host_game_playlist_fits(
	struct server_rotation const *playlist,
	char const *text)
{
	long length = (long)strlen(text);
	long index;

	if (playlist->count >= SERVER_ROTATION_MAXIMUM_ENTRIES)
		return FALSE;
	/* (as server_rotation_text joins them: ", " between) */
	for (index = 0; index < playlist->count; index++)
		length += (long)strlen(playlist->entries[index].text) + 2;
	return length < SERVER_ROTATION_MAXIMUM_TEXT;
}

boolean network_host_game_ban_parse(
	char const *text,
	char *name,
	long name_size,
	long *seconds,
	char *reason,
	long reason_size)
{
	char line[256];
	char *rest, *word;
	long length;

	name[0] = reason[0] = 0;
	*seconds = 0;
	while (*text == ' ' || *text == '\t')
		text++;
	snprintf(line, sizeof(line), "%s", text);
	/* (the expression's closing parenthesis off, and the spaces before it) */
	length = (long)strlen(line);
	while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t'))
		line[--length] = 0;
	if (length > 0 && line[length - 1] == ')')
		line[--length] = 0;
	while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t'))
		line[--length] = 0;
	rest = NULL;
	if (line[0] == '"')
	{
		/* (a quoted name: the rest after it) */
		char *closing = strchr(line + 1, '"');

		if (!closing)
			return FALSE;
		*closing = 0;
		snprintf(name, (size_t)name_size, "%s", line + 1);
		rest = closing + 1;
	}
	else
	{
		/* (the name: up to the first word after it that is a length with
		its unit, a number alone being a name's, "Player 2") */
		rest = line + strlen(line);
		for (word = strchr(line, ' '); word; word = strchr(word + 1, ' '))
		{
			char *end = word + 1;
			char saved;
			boolean unit;

			while (*end && *end != ' ')
				end++;
			saved = *end;
			*end = 0;
			unit = end - word > 1 && (end[-1] < '0' || end[-1] > '9') &&
				network_host_game_duration_parse(word + 1) != NONE;
			*end = saved;
			if (unit)
			{
				*word = 0;
				rest = word + 1;
				break;
			}
		}
		snprintf(name, (size_t)name_size, "%s", line);
	}
	while (*rest == ' ' || *rest == '\t')
		rest++;
	/* (a length, then the reason: its quotes off) */
	if (*rest)
	{
		char *end = rest;
		char saved;
		long parsed;

		while (*end && *end != ' ')
			end++;
		saved = *end;
		*end = 0;
		parsed = network_host_game_duration_parse(rest);
		*end = saved;
		if (parsed != NONE)
		{
			*seconds = parsed;
			rest = end;
		}
	}
	while (*rest == ' ' || *rest == '\t' || *rest == '"')
		rest++;
	snprintf(reason, (size_t)reason_size, "%s", rest);
	length = (long)strlen(reason);
	while (length > 0 && (reason[length - 1] == '"' || reason[length - 1] == ' '))
		reason[--length] = 0;
	/* (no tab in a ban's line, whose parts tabs part) */
	for (length = 0; reason[length]; length++)
	{
		if (reason[length] == '\t')
			reason[length] = ' ';
	}
	length = (long)strlen(name);
	while (length > 0 && name[length - 1] == ' ')
		name[--length] = 0;
	return name[0] != 0;
}

boolean network_host_game_entry_keeps(
	char const *text)
{
	return text && !strchr(text, ':');
}

boolean network_host_game_variant_kept(
	struct server_entry const *entry,
	struct network_game const *game,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	if (entry->gametype[0] || !game)
		return FALSE;
	*variant = game->variant;
	*options = game->variant_options;
	return TRUE;
}

short network_host_game_gametype_find(
	struct host_game_gametype const *gametypes,
	short count,
	wchar_t const *shown)
{
	short index, at;

	if (!shown || !shown[0])
		return NONE;
	for (index = 0; index < count; index++)
	{
		wchar_t const *listed = gametypes[index].shown;

		for (at = 0; at < HOST_GAME_GAMETYPE_SHOWN_SIZE - 1 && listed[at] && listed[at] == shown[at]; at++)
		{
		}
		if (at == HOST_GAME_GAMETYPE_SHOWN_SIZE - 1 ? !shown[at] : listed[at] == shown[at])
			return index;
	}
	return NONE;
}

/* ---------- where the host is */

/* a player's host of a network game, co-op's too */
static boolean host_game_hosting_any(
	void)
{
	return global_network_game_server_get() && !network_game_is_splitscreen_local() && !halo_server_mode();
}

boolean network_host_game_cooperative(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	/* (a co-op game played, or one whose lobby has a campaign level: the
	record's variant, which has no game engine in co-op, has none either as
	a lobby is set up again after any game) */
	return server && (network_coop_active() ||
		custom_edition_maps_level_campaign(network_game_server_get_game(server)->map.name));
}

boolean network_host_game_hosting(
	void)
{
	return host_game_hosting_any() && !network_host_game_cooperative();
}

boolean network_host_game_in_lobby(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	return host_game_hosting_any() && main_menu_is_loaded() && !network_game_server_playing(server) &&
		!network_game_server_port_start_sent(server);
}

/* ---------- entries */

/* the entry of the text, checked as the dedicated server checks its
rotation's: TRUE, else FALSE and why */
static boolean host_game_entry(
	char const *text,
	struct server_entry *entry,
	char *reason,
	long reason_size)
{
	if (!server_rotation_parse_entry(text, (int)strlen(text), entry))
	{
		snprintf(reason, (size_t)reason_size, "give a map, and a gametype: bloodgulch ctf");
		return FALSE;
	}
	if (!entry->rejected[0] && !strcmp(entry->gametype, "coop"))
		snprintf(entry->rejected, sizeof(entry->rejected), "co-op is hosted from CO-OP, not changed to");
	server_rotation_resolve(entry);
	server_gametype_check_entry(entry);
	if (entry->rejected[0])
	{
		snprintf(reason, (size_t)reason_size, "%s", entry->rejected);
		return FALSE;
	}
	return TRUE;
}

/* the lobby's limits set to those asked for (the host's variant and its
options, the menus' too: network_game_server_variant_options) */
static void host_game_apply_limits(
	struct game_variant *variant,
	struct game_variant_options *options)
{
	if (host_game.score != NONE)
		variant->universal_variant.score_to_win = host_game.score;
	if (host_game.minutes != NONE)
		options->time_limit = host_game.minutes;
	host_game.score = NONE;
	host_game.minutes = NONE;
	/* (and the rules' changes that waited for it: network_host_rules.c) */
	network_host_rules_apply_waiting(variant, TRUE);
	(void)options;
}

/* the lobby's game set to the entry's, as the director sets it
(server_apply_entry): every machine told; FALSE (said) if it cannot be */
static boolean host_game_apply(
	struct server_entry const *entry)
{
	struct network_game_server *server = global_network_game_server_get();
	struct game_variant variant;
	struct game_variant_options options;
	char reason[SERVER_REASON_SIZE];

	struct server_entry checked = *entry;

	if (!server || network_game_server_playing(server) || network_game_server_port_start_sent(server))
		return FALSE;
	/* (its map and gametype again: either may have gone since it was read) */
	checked.rejected[0] = 0;
	checked.map_missing = 0;
	server_rotation_resolve(&checked);
	if (checked.rejected[0])
	{
		console_warning("host: %s cannot be played: %s", entry->text, checked.rejected);
		error(_error_log, "host: %s cannot be played: %s", entry->text, checked.rejected);
		return FALSE;
	}
	/* (a map alone: the lobby's own gametype, score, time and rules kept) */
	if (!network_host_game_variant_kept(entry, network_game_server_get_game(server), &variant, &options) &&
		!server_gametype_build(entry->gametype, entry->rules, entry->rule_count, &variant, &options, reason,
		sizeof(reason)))
	{
		console_warning("host: %s cannot be played: %s", entry->text, reason);
		error(_error_log, "host: %s cannot be played: %s", entry->text, reason);
		return FALSE;
	}
	host_game_apply_limits(&variant, &options);
	main_set_multiplayer_map_name(entry->level);
	network_game_server_change_map_name(server, entry->level);
	player_ui_set_game_variant(&variant);
	player_ui_set_game_variant_options(&options);
	network_game_server_change_game_variant(server, &variant);
	error(_error_log, "host: the lobby's game is %s (%s)", entry->map_name,
		entry->gametype[0] ? entry->gametype : "its gametype kept");
	return TRUE;
}

/* (network_host_game_playlist_try's) */
static boolean host_game_apply_entry(
	struct server_entry const *entry,
	void *context)
{
	(void)context;
	return host_game_apply(entry);
}

/* the limits asked for set on the lobby's game as it is */
static boolean host_game_apply_limits_to_lobby(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	struct game_variant variant;
	struct game_variant_options options;

	if (!game || network_game_server_playing(server) || network_game_server_port_start_sent(server))
		return FALSE;
	variant = game->variant;
	options = game->variant_options;
	host_game_apply_limits(&variant, &options);
	player_ui_set_game_variant(&variant);
	player_ui_set_game_variant_options(&options);
	network_game_server_change_game_variant(server, &variant);
	error(_error_log, "host: the lobby's game plays to %ld, for %d minutes", game->variant.universal_variant.score_to_win,
		(int)game->variant_options.time_limit);
	return TRUE;
}

/* ---------- the playlist */

struct server_rotation const *network_host_game_playlist(
	void)
{
	char const *text = config_string("network.host_playlist");
	long index;

	if (host_game.playlist_read && !strcmp(text, host_game.playlist_text))
		return &host_game.playlist;
	snprintf(host_game.playlist_text, sizeof(host_game.playlist_text), "%s", text);
	server_rotation_parse(text, &host_game.playlist);
	for (index = 0; index < host_game.playlist.count; index++)
	{
		struct server_entry *entry = &host_game.playlist.entries[index];

		if (!entry->rejected[0] && !strcmp(entry->gametype, "coop"))
			snprintf(entry->rejected, sizeof(entry->rejected), "co-op is not in a host's playlist");
		server_rotation_resolve(entry);
		server_gametype_check_entry(entry);
	}
	/* (an index past a shorter playlist: from its start) */
	if (host_game.index >= host_game.playlist.count)
		host_game.index = NONE;
	host_game.playlist_read = TRUE;
	return &host_game.playlist;
}

long network_host_game_playlist_index(
	void)
{
	network_host_game_playlist();
	return host_game.index;
}

/* the playlist written (its entries' text, ", " between them) */
static boolean host_game_playlist_write(
	struct server_rotation const *playlist)
{
	static char text[SERVER_ROTATION_MAXIMUM_TEXT];

	server_rotation_text(playlist, text, sizeof(text));
	if (!config_write("network.host_playlist", text))
	{
		console_warning("host: could not write network.host_playlist");
		return FALSE;
	}
	return TRUE;
}

boolean network_host_game_playlist_set(
	char const *text,
	char *reason,
	long reason_size)
{
	static struct server_rotation playlist;

	server_rotation_parse(text, &playlist);
	if (text[0] && !playlist.count)
	{
		snprintf(reason, (size_t)reason_size, "no entry in \"%s\"", text);
		return FALSE;
	}
	if (playlist.truncated)
	{
		snprintf(reason, (size_t)reason_size, "a playlist holds %d games, %d characters, at most",
			SERVER_ROTATION_MAXIMUM_ENTRIES, SERVER_ROTATION_MAXIMUM_TEXT - 1);
		return FALSE;
	}
	/* (the same playlist again goes on where it was; another starts at its
	first) */
	{
		static char written[SERVER_ROTATION_MAXIMUM_TEXT];

		server_rotation_text(&playlist, written, sizeof(written));
		if (strcmp(written, config_string("network.host_playlist")))
			host_game.index = NONE;
	}
	return host_game_playlist_write(&playlist);
}

boolean network_host_game_playlist_add(
	char const *text,
	char *reason,
	long reason_size)
{
	static struct server_rotation playlist;
	struct server_entry entry;

	if (!host_game_entry(text, &entry, reason, reason_size))
		return FALSE;
	playlist = *network_host_game_playlist();
	if (!network_host_game_playlist_fits(&playlist, entry.text))
	{
		snprintf(reason, (size_t)reason_size, "the playlist is full (%d games, %d characters at most)",
			SERVER_ROTATION_MAXIMUM_ENTRIES, SERVER_ROTATION_MAXIMUM_TEXT - 1);
		return FALSE;
	}
	playlist.entries[playlist.count++] = entry;
	return host_game_playlist_write(&playlist);
}

boolean network_host_game_playlist_remove(
	long index)
{
	static struct server_rotation playlist;
	long after;

	playlist = *network_host_game_playlist();
	if (index < 0 || index >= playlist.count)
		return FALSE;
	for (after = index; after + 1 < playlist.count; after++)
		playlist.entries[after] = playlist.entries[after + 1];
	playlist.count--;
	/* (the entry played last kept, or the one before it taken out) */
	if (host_game.index != NONE && index <= host_game.index)
		host_game.index = host_game.index == index ? index - 1 : host_game.index - 1;
	if (host_game.index < 0)
		host_game.index = NONE;
	if (host_game.next_index == index)
		host_game.next_index = NONE;
	else if (host_game.next_index > index)
		host_game.next_index--;
	return host_game_playlist_write(&playlist);
}

/* ---------- the next game */

boolean network_host_game_next(
	struct server_entry *entry,
	boolean *now)
{
	struct server_rotation const *playlist;
	long next;

	*now = FALSE;
	/* (co-op's next is the campaign's next level) */
	if (network_host_game_cooperative())
		return FALSE;
	if (host_game.has_next)
	{
		*entry = host_game.next;
		*now = host_game.next_now;
		return TRUE;
	}
	playlist = network_host_game_playlist();
	next = network_host_game_playlist_after(playlist, host_game.index);
	if (next == NONE)
		return FALSE;
	*entry = playlist->entries[next];
	return TRUE;
}

void network_host_game_cancel(
	void)
{
	host_game.has_next = FALSE;
	host_game.next_now = FALSE;
	host_game.next_index = NONE;
}

/* an entry's map and gametype as shown, in printable ASCII for a notice
("Blood Gulch, Host CTF"; what is not ASCII "?") */
static void host_game_notice_text(
	struct server_entry const *entry,
	char *text,
	long size)
{
	wchar_t wide[96];
	long index, length = 0;

	network_host_game_entry_shown(entry, wide, NUMBEROF(wide));
	for (index = 0; wide[index] && length < size - 2; index++)
	{
		if (wide[index] == 0x00B7)
		{
			while (length > 0 && text[length - 1] == ' ')
				length--;
			text[length++] = ',';
		}
		else
			text[length++] = wide[index] >= 32 && wide[index] < 127 ? (char)wide[index] : '?';
	}
	text[length] = 0;
}

boolean network_host_game_change(
	char const *text,
	short when,
	long playlist_index,
	char *reason,
	long reason_size)
{
	struct server_entry entry;
	char notice[128];
	char shown[96];

	if (!network_host_game_hosting())
	{
		snprintf(reason, (size_t)reason_size, halo_server_mode() ? "a dedicated server's is sv_map" :
			host_game_hosting_any() ? "co-op plays the campaign's levels in their order: no map changes" :
			"only the host of a network game changes its map");
		return FALSE;
	}
	if (!host_game_entry(text, &entry, reason, reason_size))
		return FALSE;
	/* (a map alone, not a playlist's: the game's gametype, score to win,
	time limit and rules kept, as the lobby has them when it is set:
	host_game_apply) */
	if (playlist_index == NONE && network_host_game_entry_keeps(text))
		entry.gametype[0] = 0;
	/* (in the lobby: at once, whenever it was asked for) */
	if (network_host_game_in_lobby())
	{
		if (!host_game_apply(&entry))
		{
			snprintf(reason, (size_t)reason_size, "the lobby's game could not be changed");
			return FALSE;
		}
		network_host_game_cancel();
		if (playlist_index != NONE)
			host_game.index = playlist_index;
		snprintf(reason, (size_t)reason_size, "set in the lobby");
		return TRUE;
	}
	host_game.next = entry;
	host_game.has_next = TRUE;
	host_game.next_now = FALSE;
	host_game.next_index = playlist_index;
	if (when == _host_game_now)
	{
		/* (as Restart round: the game ended, every machine to the lobby,
		where network_host_game_lobby_take sets it; a game whose scores show
		already: once its lobby is reached) */
		host_game.next_now = TRUE;
		if (!network_host_controls_restart())
		{
			host_game.next_now = FALSE;
			snprintf(reason, (size_t)reason_size, "after this game (it is over)");
			return TRUE;
		}
		host_game_notice_text(&entry, shown, sizeof(shown));
		snprintf(notice, sizeof(notice), "The host changed the game: %s", shown);
		distributed_send_notice(notice);
		snprintf(reason, (size_t)reason_size, "now: this game ends");
		return TRUE;
	}
	snprintf(reason, (size_t)reason_size, "after this game");
	return TRUE;
}

boolean network_host_game_lobby_take(
	boolean restarting)
{
	struct server_rotation const *playlist;
	struct server_entry entry;
	long index = NONE;
	boolean applied = FALSE;

	unsigned long takes = network_host_game_lobby_takes(network_host_game_cooperative(), host_game.has_next,
		host_game.next_now, restarting);

	if (!host_game_hosting_any())
		return FALSE;
	if (TEST_FLAG(takes, _host_game_take_change))
	{
		entry = host_game.next;
		index = host_game.next_index;
		network_host_game_cancel();
		applied = host_game_apply(&entry);
		if (!applied)
			index = NONE;
	}
	/* (no change, or one that can no longer be played: the playlist's
	next that can, each tried once) */
	if (!applied && TEST_FLAG(takes, _host_game_take_playlist))
	{
		playlist = network_host_game_playlist();
		index = network_host_game_playlist_try(playlist, host_game.index, host_game_apply_entry, NULL);
		applied = index != NONE;
	}
	/* (the playlist goes on after the entry played; a change asked for
	that is no entry of it leaves its place as it was) */
	if (applied && index != NONE)
		host_game.index = index;
	/* (limits asked for, with no new game: on the lobby's as it is) */
	if (!applied && !network_host_game_cooperative() && (host_game.score != NONE || host_game.minutes != NONE ||
		network_host_rules_waiting()))
		host_game_apply_limits_to_lobby();
	return applied;
}

/* ---------- the limits */

boolean network_host_game_set_limits(
	long score,
	short minutes)
{
	if (!network_host_game_hosting())
		return FALSE;
	if (score != NONE)
		host_game.score = score;
	if (minutes != NONE)
		host_game.minutes = minutes;
	if (network_host_game_in_lobby())
		return host_game_apply_limits_to_lobby();
	return TRUE;
}

boolean network_host_game_limits_waiting(
	long *score,
	short *minutes)
{
	*score = host_game.score;
	*minutes = host_game.minutes;
	return host_game.score != NONE || host_game.minutes != NONE;
}

/* ---------- what is shown */

/* a gametype's name as shown (its variant's description; else its name) */
static void host_game_gametype_shown(
	char const *gametype,
	struct server_rule const *rules,
	long rule_count,
	wchar_t *text,
	short size)
{
	struct game_variant variant;
	struct game_variant_options options;
	char reason[SERVER_REASON_SIZE];
	short index;

	text[0] = 0;
	if (server_gametype_build(gametype, rules, rule_count, &variant, &options, reason, sizeof(reason)))
	{
		ustrncpy(text, variant.human_readable_game_description, size - 1);
		text[size - 1] = 0;
	}
	if (!text[0])
	{
		for (index = 0; gametype[index] && index < size - 1; index++)
			text[index] = (wchar_t)(unsigned char)gametype[index];
		text[index] = 0;
	}
}

void network_host_game_gametype_text(
	char const *name,
	wchar_t *text,
	short size)
{
	host_game_gametype_shown(name, NULL, 0, text, size);
}

void network_host_game_entry_shown(
	struct server_entry const *entry,
	wchar_t *text,
	short size)
{
	/* (the menus show them each frame: the last few kept, as a gametype
	file's name is read from its file) */
	static struct
	{
		char text[SERVER_ENTRY_TEXT_SIZE];
		wchar_t shown[96];
	} kept[16];
	static short next_kept;
	wchar_t map[64], gametype[HOST_GAME_GAMETYPE_SHOWN_SIZE];
	short index;

	/* (a map alone: the game's own gametype, as it is now) */
	if (!entry->gametype[0])
	{
		struct network_game_server *server = global_network_game_server_get();
		struct network_game *game = server ? network_game_server_get_game(server) : NULL;
		wchar_t const *description = game ? game->variant.human_readable_game_description : L"";

		menu_map_shown(entry->level[0] ? entry->level : entry->map_name, map, NUMBEROF(map));
		for (index = 0; index < NUMBEROF(gametype) - 1 &&
			index < NUMBEROF(game->variant.human_readable_game_description) && description[index]; index++)
		{
			gametype[index] = description[index];
		}
		gametype[index] = 0;
		usnprintf(text, size - 1, L"%s \x00B7 %s", map, gametype);
		text[size - 1] = 0;
		return;
	}
	for (index = 0; index < NUMBEROF(kept); index++)
	{
		if (kept[index].text[0] && !strcmp(kept[index].text, entry->text))
		{
			ustrncpy(text, kept[index].shown, size - 1);
			text[size - 1] = 0;
			return;
		}
	}
	menu_map_shown(entry->level[0] ? entry->level : entry->map_name, map, NUMBEROF(map));
	host_game_gametype_shown(entry->gametype, entry->rules, entry->rule_count, gametype, NUMBEROF(gametype));
	index = next_kept;
	next_kept = (short)((next_kept + 1) % NUMBEROF(kept));
	snprintf(kept[index].text, sizeof(kept[index].text), "%s", entry->text);
	usnprintf(kept[index].shown, NUMBEROF(kept[index].shown) - 1, L"%s \x00B7 %s", map, gametype);
	kept[index].shown[NUMBEROF(kept[index].shown) - 1] = 0;
	ustrncpy(text, kept[index].shown, size - 1);
	text[size - 1] = 0;
}

static void host_game_map_listed(
	void *context,
	char const *name,
	char const *level,
	char const *kind)
{
	struct host_game_map *map;

	if (host_game.map_count >= MAXIMUM_HOST_GAME_MAPS)
	{
		host_game.maps_left_out++;
		return;
	}
	map = &host_game.maps[host_game.map_count++];
	snprintf(map->name, sizeof(map->name), "%s", name);
	snprintf(map->level, sizeof(map->level), "%s", level);
	map->custom = !strcmp(kind, "custom");
}

static void host_game_lists_read(
	void)
{
	unsigned long now = system_milliseconds();
	long index;
	char const *name;

	if (host_game.lists_read && now - host_game.lists_time < HOST_GAME_LISTS_INTERVAL)
		return;
	host_game.lists_read = TRUE;
	host_game.lists_time = now;
	host_game.map_count = 0;
	host_game.maps_left_out = 0;
	server_maps_each(host_game_map_listed, NULL);
	if (host_game.maps_left_out)
	{
		error(_error_log, "host: %ld maps past the first %d left out of Host Controls' MAPS", host_game.maps_left_out,
			MAXIMUM_HOST_GAME_MAPS);
	}
	/* (the built-in and OpenCE's once, then those this machine's player has
	saved, read again with the maps) */
	if (!host_game.builtin_gametype_count)
	{
		for (index = 0; (name = server_gametype_builtin_name(index)) != NULL &&
			host_game.builtin_gametype_count < MAXIMUM_HOST_GAME_GAMETYPES; index++)
		{
			struct host_game_gametype *gametype = &host_game.gametypes[host_game.builtin_gametype_count++];

			snprintf(gametype->name, sizeof(gametype->name), "%s", name);
			host_game_gametype_shown(name, NULL, 0, gametype->shown, NUMBEROF(gametype->shown));
		}
	}
	host_game.gametype_count = host_game.builtin_gametype_count;
	{
		static char names[MAXIMUM_HOST_GAME_GAMETYPES][SERVER_GAMETYPE_SIZE];
		static wchar_t shown[MAXIMUM_HOST_GAME_GAMETYPES][SERVER_GAMETYPE_SHOWN_SIZE];
		short saved = server_gametype_saved_list(names, shown,
			(short)(MAXIMUM_HOST_GAME_GAMETYPES - host_game.gametype_count));

		for (index = 0; index < saved; index++)
		{
			struct host_game_gametype *gametype = &host_game.gametypes[host_game.gametype_count++];

			snprintf(gametype->name, sizeof(gametype->name), "%s", names[index]);
			ustrncpy(gametype->shown, shown[index], NUMBEROF(gametype->shown) - 1);
			gametype->shown[NUMBEROF(gametype->shown) - 1] = 0;
		}
	}
}

short network_host_game_maps(
	struct host_game_map const **maps)
{
	host_game_lists_read();
	*maps = host_game.maps;
	return host_game.map_count;
}

long network_host_game_maps_left_out(
	void)
{
	return host_game.maps_left_out;
}

short network_host_game_gametypes(
	struct host_game_gametype const **gametypes)
{
	host_game_lists_read();
	*gametypes = host_game.gametypes;
	return host_game.gametype_count;
}

/* ---------- the players: the lobby's, and what the host does to them */

/* the lobby's game (the host's record) */
static struct network_game *host_game_lobby_game(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server && network_host_controls_in_lobby() ? network_game_server_get_game(server) : NULL;
}

/* whether the lobby's players choose a team (not Infection's, whose
zombies are chosen as it starts: lobby_menu.inc's lobby_board_teams) */
static boolean host_game_lobby_teams(
	struct network_game const *game)
{
	return game->variant.universal_variant.teams &&
		game_modes_variant_mode(&game->variant, &game->variant_options) != _game_mode_infection;
}

/* the lobby's players, as its record has them: every machine hears of a team
changed there (the lobby's own message), OpenCE's too */
short network_host_game_lobby_players(
	struct host_controls_player *players,
	short maximum)
{
	struct network_game *game = host_game_lobby_game();
	long own_machine = network_game_client_get_local_machine_index();
	boolean teams;
	short count = 0;
	short slot;

	if (!game)
		return 0;
	teams = host_game_lobby_teams(game);
	for (slot = 0; slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < maximum; slot++)
	{
		struct network_player const *player = &game->players[slot];
		struct host_controls_player *entry = &players[count];
		struct host_controls_player_view view;
		long machine_index = player->machine_index;

		if (!network_player_is_valid(player))
			continue;
		csmemset(entry, 0, sizeof(*entry));
		entry->player_index = NONE;
		entry->slot = slot;
		entry->machine_index = machine_index;
		ustrncpy(entry->name, player->name, NUMBEROF(entry->name) - 1);
		entry->name[NUMBEROF(entry->name) - 1] = 0;
		entry->team = teams ? (short)player->team_index : NONE;
		entry->ping = NONE;
		view.bot = bots_machine_is_bot(machine_index) != 0;
		view.own = !view.bot && machine_index == own_machine;
		view.bot_removable = view.bot && !bots_matchmaking_on();
		view.teams = teams;
		view.team_moves = teams;
		if (view.bot)
			entry->tag = _host_controls_tag_bot;
		else if (view.own)
			entry->tag = _host_controls_tag_host;
		else if (network_standard_machine_is_opence(machine_index))
			entry->tag = _host_controls_tag_opence;
		else if (network_standard_machine_is_openh1(machine_index))
			entry->tag = _host_controls_tag_openh1;
		else
			entry->tag = _host_controls_tag_none;
		entry->actions = network_host_controls_actions_for(&view);
		count++;
	}
	return count;
}

/* ---------- a player of the list: in a game or the lobby */

/* the entry's player as the record has them now (the lobby's): its
machine and name the same, else NULL */
static struct network_player *host_game_lobby_player(
	struct host_controls_player const *entry)
{
	struct network_game *game = host_game_lobby_game();
	struct network_player *player;

	if (!game || entry->player_index != NONE || !VALID_INDEX(entry->slot, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		return NULL;
	player = &game->players[entry->slot];
	if (!network_player_is_valid(player) || player->machine_index != entry->machine_index ||
		ustrncmp(player->name, entry->name, NUMBEROF(entry->name) - 1))
	{
		return NULL;
	}
	return player;
}

/* the actions the host may take on the entry's player now */
static unsigned long host_game_entry_actions(
	struct host_controls_player const *entry)
{
	static struct host_controls_player players[MAXIMUM_HOST_GAME_PLAYERS];
	short count, index;

	if (entry->player_index != NONE)
		return network_host_controls_player_actions(entry->player_index);
	if (!host_game_lobby_player(entry))
		return 0;
	count = network_host_controls_players(players, MAXIMUM_HOST_GAME_PLAYERS);
	for (index = 0; index < count; index++)
	{
		if (players[index].slot == entry->slot)
			return players[index].actions;
	}
	return 0;
}

boolean network_host_game_kick_player(
	struct host_controls_player const *entry,
	boolean ban,
	long seconds,
	char const *reason)
{
	unsigned long actions = host_game_entry_actions(entry);
	boolean kicked;

	if (!TEST_FLAG(actions, ban ? _host_controls_ban : _host_controls_kick))
		return FALSE;
	/* (a lobby's bot out, bots.count one fewer than play, as a game's) */
	if (entry->player_index == NONE && bots_machine_is_bot(entry->machine_index))
	{
		network_host_controls_bots_one_fewer();
		return network_game_server_port_bots_remove(global_network_game_server_get(), entry->machine_index);
	}
	/* (the ban's line: its reason, and its end) */
	if (ban)
	{
		network_distributed_ban_details(reason && reason[0] ? reason : "banned by the host",
			seconds > 0 ? (unsigned long)time(NULL) + (unsigned long)seconds : 0);
	}
	if (entry->player_index != NONE)
		kicked = network_host_controls_kick(entry->player_index, ban);
	else
		kicked = network_game_server_kick_machine_of_player(entry->machine_index, ban);
	if (ban)
		network_distributed_ban_details(NULL, 0);
	return kicked;
}

/* (the host) a lobby's player put on a team, every machine told */
static boolean host_game_lobby_team_set(
	struct network_player *player,
	char team)
{
	struct network_game_server *server = global_network_game_server_get();

	if (!server || player->team_index == team)
		return server != NULL;
	player->team_index = team;
	if (!network_game_server_send_game_data_pregame(server))
		error(_error_log, "host controls: the lobby's teams could not be sent");
	return TRUE;
}

boolean network_host_game_move_player(
	struct host_controls_player const *entry)
{
	struct network_player *player;

	if (entry->player_index != NONE)
		return network_host_controls_move_team(entry->player_index);
	player = host_game_lobby_player(entry);
	if (!player || !TEST_FLAG(host_game_entry_actions(entry), _host_controls_move_team))
		return FALSE;
	return host_game_lobby_team_set(player, (char)(player->team_index == 1 ? 0 : 1));
}

/* the entry's name as the chat keeps mutes (ASCII, network_chat.c) */
static void host_game_entry_ascii(
	struct host_controls_player const *entry,
	char *name,
	short size)
{
	short index;

	for (index = 0; index < size - 1 && index < (short)NUMBEROF(entry->name) && entry->name[index]; index++)
		name[index] = (char)player_name_character_ascii(entry->name[index]);
	name[index] = 0;
}

short network_host_game_player_mute_minutes(
	struct host_controls_player const *entry)
{
	wchar_t name[NUMBEROF(entry->name) + 1];

	if (entry->player_index != NONE)
		return network_chat_host_mute_minutes((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(entry->player_index));
	ustrncpy(name, entry->name, NUMBEROF(entry->name));
	name[NUMBEROF(entry->name)] = 0;
	return network_chat_host_name_muted(name) ? 1 : 0;
}

boolean network_host_game_mute_player(
	struct host_controls_player const *entry,
	short minutes)
{
	char name[16];

	if (!TEST_FLAG(host_game_entry_actions(entry), _host_controls_mute))
		return FALSE;
	host_game_entry_ascii(entry, name, sizeof(name));
	return network_chat_host_mute_named(name, (short)PIN(minutes, 0, HOST_GAME_MUTE_MOST_MINUTES), minutes > 0);
}

/* ---------- the teams arranged */

/* a game's player's score, by its game engine (none in the lobby) */
static long host_game_player_score(
	struct host_controls_player const *entry)
{
	if (entry->player_index == NONE || !game_engine || !game_engine->get_player_score)
		return 0;
	return game_engine->get_player_score(entry->player_index, _get_score_individual);
}

boolean network_host_game_teams_arrangeable(
	void)
{
	struct network_game *lobby = host_game_lobby_game();

	return lobby ? host_game_lobby_teams(lobby) : network_host_controls_team_moves();
}

short network_host_game_teams_arrange(
	short how)
{
	static struct host_controls_player players[MAXIMUM_HOST_GAME_PLAYERS];
	static struct host_controls_player *arranged[MAXIMUM_HOST_GAME_PLAYERS];
	long scores[MAXIMUM_HOST_GAME_PLAYERS];
	char current[MAXIMUM_HOST_GAME_PLAYERS];
	char teams[MAXIMUM_HOST_GAME_PLAYERS];
	boolean lobby = host_game_lobby_game() != NULL;
	short count, index, moved = 0, arranged_count = 0;

	if (!network_host_game_teams_arrangeable())
		return NONE;
	count = network_host_controls_players(players, MAXIMUM_HOST_GAME_PLAYERS);
	for (index = 0; index < count; index++)
	{
		/* (the lobby's bots follow its players: bots.c's teams) */
		if (lobby && players[index].tag == _host_controls_tag_bot)
			continue;
		if (!TEST_FLAG(players[index].actions, _host_controls_move_team))
			continue;
		arranged[arranged_count] = &players[index];
		scores[arranged_count] = host_game_player_score(&players[index]);
		current[arranged_count] = (char)players[index].team;
		arranged_count++;
	}
	network_host_game_teams_plan(arranged_count, scores, current, how, system_milliseconds(), teams);
	for (index = 0; index < arranged_count; index++)
	{
		if (teams[index] != current[index] && network_host_game_move_player(arranged[index]))
			moved++;
	}
	error(_error_log, "host controls: the teams %s, %d moved", how == _host_game_shuffle ? "shuffled" : "balanced",
		moved);
	return moved;
}

/* ---------- the host's words to everyone */

boolean network_host_game_announce(
	char const *text)
{
	char notice[HOST_GAME_NOTICE_SIZE];
	long length = 0;

	while (*text == ' ' || *text == '\t')
		text++;
	/* (printable ASCII, as the notice is shown) */
	for (; *text && length < (long)sizeof(notice) - 1; text++)
		notice[length++] = *text >= 32 && *text <= 126 ? *text : '?';
	while (length > 0 && notice[length - 1] == ' ')
		length--;
	notice[length] = 0;
	if (!host_game_hosting_any())
	{
		console_warning("host_say: only the host of a network game says this");
		return FALSE;
	}
	if (!length)
	{
		console_warning("host_say: give the words");
		return FALSE;
	}
	if (!game_in_progress() || !game_engine_running())
	{
		console_warning("host_say: in the lobby no machine sees a notice: say it once the game runs");
		return FALSE;
	}
	distributed_send_notice(notice);
	return TRUE;
}


boolean network_host_game_ban_command(
	char const *text)
{
	char name[64];
	char reason[96];
	long seconds;
	boolean banned;

	if (!network_host_game_ban_parse(text, name, sizeof(name), &seconds, reason, sizeof(reason)))
	{
		console_warning("ban: give a player's name, then a length (30m, 2h, 7d, 1w, forever) and why");
		return FALSE;
	}
	network_distributed_ban_details(reason[0] ? reason : "banned by the host",
		seconds > 0 ? (unsigned long)time(NULL) + (unsigned long)seconds : 0);
	banned = network_game_server_ban_player(name);
	network_distributed_ban_details(NULL, 0);
	if (banned && seconds > 0)
		console_printf(FALSE, "%s banned for %ld minutes", name, seconds / 60);
	return banned;
}

/* the text after the command word the expression starts with (else NULL) */
static char const *host_game_word_text(
	char const *expression,
	char const *word)
{
	size_t length = strlen(word);

	while (*expression == ' ' || *expression == '\t' || *expression == '(')
		expression++;
	if (strncmp(expression, word, length) || (expression[length] && expression[length] != ' ' &&
		expression[length] != '\t' && expression[length] != ')'))
	{
		return NULL;
	}
	expression += length;
	while (*expression == ' ' || *expression == '\t' || *expression == '"')
		expression++;
	return expression;
}

boolean network_host_game_console(
	char const *expression,
	boolean *result)
{
	char text[HOST_GAME_NOTICE_SIZE];
	char const *rest;
	long length;

	if (network_host_game_command(expression, result))
		return TRUE;
	/* (a dedicated server's admin's: network_admin.c) */
	if (network_admin_console(expression, result))
		return TRUE;
	/* (the rules: network_host_rules.c) */
	if (network_host_rules_console(expression, result))
		return TRUE;
	if ((rest = host_game_word_text(expression, "host_say")) != NULL)
	{
		snprintf(text, sizeof(text), "%s", rest);
		length = (long)strlen(text);
		while (length > 0 && (text[length - 1] == ')' || text[length - 1] == '"' || text[length - 1] == ' '))
			text[--length] = 0;
		*result = network_host_game_announce(text);
		return TRUE;
	}
	if ((rest = host_game_word_text(expression, "host_shuffle_teams")) != NULL ||
		(rest = host_game_word_text(expression, "host_balance_teams")) != NULL)
	{
		short how = host_game_word_text(expression, "host_shuffle_teams") ? _host_game_shuffle :
			_host_game_balance;
		short moved = network_host_controls_available() ? network_host_game_teams_arrange(how) : NONE;

		*result = moved != NONE;
		if (moved == NONE)
		{
			console_warning("%s: only in a team game you host, in its lobby or (an OpenH1 game without OpenCE "
				"players) while it plays", how == _host_game_shuffle ? "host_shuffle_teams" : "host_balance_teams");
		}
		else
			console_printf(FALSE, "%d player%s moved", moved, moved == 1 ? "" : "s");
		return TRUE;
	}
	/* (the BOTS tab's count and skill: network_host_controls.c) */
	if ((rest = host_game_word_text(expression, "host_bots")) != NULL)
	{
		long count = atol(rest);
		short kind = NONE;
		long number;

		/* (a number, or as the lobby's BOTS: none, add <n>, fill <n>) */
		if (rest[0] >= '0' && rest[0] <= '9')
			kind = count ? _host_bots_add : _host_bots_none;
		else if (!strncmp(rest, "none", 4))
			kind = _host_bots_none;
		else if (!strncmp(rest, "add ", 4) && rest[4] >= '0' && rest[4] <= '9')
			kind = _host_bots_add, count = atol(rest + 4);
		else if (!strncmp(rest, "fill ", 5) && rest[5] >= '0' && rest[5] <= '9')
			kind = _host_bots_fill, count = atol(rest + 5);
		if (kind == NONE)
		{
			kind = network_host_controls_bot_choice(network_host_controls_bots_chosen(), &number);
			console_printf(FALSE, "bots: %s %ld, %s", kind == _host_bots_fill ? "fill to" :
				kind == _host_bots_add ? "add" : "none", number,
				bots_matchmaking_on() ? "the matchmaking playlist's" : "bots.count, bots.make_room, bots.fill_to");
			*result = TRUE;
			return TRUE;
		}
		*result = network_host_controls_bots_tab() && network_host_controls_choose_bots(kind, count);
		if (*result)
			console_printf(FALSE, "bots set");
		else
		{
			console_warning("host_bots: give none, add <1 to %d> or fill <2 to %d>, as the host of a network game "
				"(not co-op, not matchmaking)", BOTS_MAXIMUM, BOTS_MAXIMUM);
		}
		return TRUE;
	}
	if ((rest = host_game_word_text(expression, "host_bot_skill")) != NULL)
	{
		short skill = network_host_controls_bot_skill_named(rest);

		*result = skill != NONE && network_host_controls_set_bot_skill(skill);
		if (*result)
			console_printf(FALSE, "bot skill set");
		else
			console_warning("host_bot_skill: give easy, normal, heroic or legendary, as the host of a network game");
		return TRUE;
	}
	if ((rest = host_game_word_text(expression, "host_score_limit")) != NULL ||
		(rest = host_game_word_text(expression, "host_time_limit")) != NULL)
	{
		boolean score = host_game_word_text(expression, "host_score_limit") != NULL;
		long value = atol(rest);

		*result = rest[0] >= '0' && rest[0] <= '9' && value >= (score ? 1 : 0) && value <= 999 &&
			network_host_game_set_limits(score ? value : NONE, score ? NONE : (short)value);
		if (!*result)
		{
			console_warning("%s: give %s, as the host of a network game", score ? "host_score_limit" :
				"host_time_limit", score ? "the score to win (1 to 999)" : "the minutes (0: none, up to 999)");
		}
		else
			console_printf(FALSE, network_host_game_in_lobby() ? "set in the lobby" : "set for the next game");
		return TRUE;
	}
	return FALSE;
}

/* ---------- each frame */

void network_host_game_update(
	void)
{
	struct server_entry entry;
	boolean now;
	char notice[128];
	char shown[96];
	boolean postgame = network_host_game_hosting() && game_engine_running() && game_engine_showing_postgame();

	if (!global_network_game_server_get())
	{
		network_host_game_cancel();
		host_game.score = NONE;
		host_game.minutes = NONE;
	}
	if (!postgame)
	{
		host_game.over_said = FALSE;
		return;
	}
	if (host_game.over_said)
		return;
	host_game.over_said = TRUE;
	if (network_host_game_next(&entry, &now) && !now)
	{
		host_game_notice_text(&entry, shown, sizeof(shown));
		snprintf(notice, sizeof(notice), "Next game: %s", shown);
		distributed_send_notice(notice);
	}
}

/* ---------- the console */

/* the text after the command word (in either case) the expression starts
with (spaces and "(" before it, a space or its end after it), else NULL */
static char const *host_game_command_text(
	char const *expression,
	char const *word)
{
	size_t length = strlen(word);
	size_t index;

	while (*expression == ' ' || *expression == '\t' || *expression == '(')
		expression++;
	for (index = 0; index < length; index++)
	{
		char character = expression[index];

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		if (character != word[index])
			return NULL;
	}
	expression += length;
	if (*expression != ' ' && *expression != '\t' && *expression != ')' && *expression)
		return NULL;
	while (*expression == ' ' || *expression == '\t' || *expression == '"')
		expression++;
	return expression;
}

/* the rest of a command's text, its closing ")" and quotes off */
static void host_game_command_rest(
	char const *text,
	char *rest,
	long size)
{
	long length = 0;

	while (*text && length < size - 1)
	{
		if (*text != '"')
			rest[length++] = *text;
		text++;
	}
	while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t' || rest[length - 1] == ')'))
		length--;
	rest[length] = 0;
}

/* "map gametype rule..." as an entry ("map:gametype+rule"), or an entry as
it is */
static void host_game_command_entry(
	char const *text,
	char *entry,
	long size)
{
	char words[SERVER_ENTRY_TEXT_SIZE];
	char *word, *after;
	long length = 0;
	short count = 0;

	host_game_command_rest(text, words, sizeof(words));
	if (strchr(words, ':'))
	{
		snprintf(entry, (size_t)size, "%s", words);
		return;
	}
	entry[0] = 0;
	for (word = words; *word; word = after)
	{
		while (*word == ' ' || *word == '\t')
			word++;
		if (!*word)
			break;
		for (after = word; *after && *after != ' ' && *after != '\t'; after++)
		{
		}
		length += snprintf(entry + length, (size_t)(size - length), "%s%.*s",
			count == 0 ? "" : count == 1 && word[0] != '+' ? ":" : "", (int)(after - word), word);
		if (length >= size)
		{
			entry[size - 1] = 0;
			return;
		}
		count++;
	}
}

static void host_game_say_playlist(
	void)
{
	struct server_rotation const *playlist = network_host_game_playlist();
	long next = network_host_game_playlist_after(playlist, host_game.index);
	long index;

	if (!playlist->count)
	{
		console_printf(FALSE, "the playlist is empty: host_playlist_add bloodgulch ctf");
		return;
	}
	for (index = 0; index < playlist->count; index++)
	{
		struct server_entry const *entry = &playlist->entries[index];

		console_printf(FALSE, "%ld. %s%s%s%s", index + 1, entry->text, index == next ? " (next)" : "",
			entry->rejected[0] ? ": " : "", entry->rejected);
	}
}

boolean network_host_game_command(
	char const *expression,
	boolean *result)
{
	char entry[SERVER_ENTRY_TEXT_SIZE];
	char reason[SERVER_REASON_SIZE];
	char const *text;
	boolean next;

	if ((text = host_game_command_text(expression, "host_map")) != NULL ||
		(text = host_game_command_text(expression, "host_map_next")) != NULL)
	{
		next = host_game_command_text(expression, "host_map_next") != NULL;
		host_game_command_entry(text, entry, sizeof(entry));
		if (!entry[0])
		{
			console_warning("%s: give a map, and a gametype (none: the game's kept): %s bloodgulch ctf",
				next ? "host_map_next" : "host_map", next ? "host_map_next" : "host_map");
			*result = FALSE;
			return TRUE;
		}
		*result = network_host_game_change(entry, next ? _host_game_after : _host_game_now, NONE, reason,
			sizeof(reason));
		if (*result)
			console_printf(FALSE, "next: %s (%s)", entry, reason);
		else
		{
			console_warning("host_map: %s", reason);
			error(_error_log, "host: host_map %s: %s", entry, reason);
		}
		return TRUE;
	}
	if ((text = host_game_command_text(expression, "host_playlist_add")) != NULL)
	{
		host_game_command_entry(text, entry, sizeof(entry));
		*result = network_host_game_playlist_add(entry, reason, sizeof(reason));
		if (*result)
			host_game_say_playlist();
		else
			console_warning("host_playlist_add: %s", reason);
		return TRUE;
	}
	if ((text = host_game_command_text(expression, "host_playlist_remove")) != NULL)
	{
		long number = atol(text);

		*result = network_host_game_playlist_remove(number - 1);
		if (*result)
			host_game_say_playlist();
		else
			console_warning("host_playlist_remove: give an entry's number (host_playlist lists them)");
		return TRUE;
	}
	if ((text = host_game_command_text(expression, "host_playlist_clear")) != NULL)
	{
		*result = network_host_game_playlist_set("", reason, sizeof(reason));
		if (*result)
			console_printf(FALSE, "the playlist is empty");
		return TRUE;
	}
	if ((text = host_game_command_text(expression, "host_playlist")) != NULL)
	{
		static char rest[SERVER_ROTATION_MAXIMUM_TEXT];

		host_game_command_rest(text, rest, sizeof(rest));
		*result = TRUE;
		if (rest[0])
		{
			*result = network_host_game_playlist_set(rest, reason, sizeof(reason));
			if (!*result)
				console_warning("host_playlist: %s", reason);
		}
		if (*result)
			host_game_say_playlist();
		return TRUE;
	}
	return FALSE;
}
