/*
BOTS_MATCH.C

A match run from the bot control (bots_match.h; docs/bot_control.md,
"Running a match from the command line").

match start sets the game up as the host's own controls do: a dedicated
server plays it now as sv_map does (server_director_interpose), a player's
host as Host Controls' change of game does (network_host_game_change: in
the lobby at once, then started; in a game, ended and the new one started
from the lobby). Its bots are bots.count for this run, half of them on
each team (bots_command.c's join teams). Its seed is the game's random
seed (the network game's, which the start gives every machine), given in
the lobby until the game starts.

match speed is the game's own speed (game_time_set_speed, which a frame's
ticks follow: game_time.c), so a tick runs as it always does and only more
of them run a frame: a dedicated server's frame runs up to 30 of them. It
runs only where no other machine is in the game: a dedicated server or a
host whose game has no remote machine, and goes back to 1 the frame one
joins.

The results: when the game ends (its postgame), each player's kills,
deaths, assists, shots, objective statistics and score, the teams', the
winner, the duration and the lines its ticks applied, kept until the next
match starts. A coach's (--team) results list its own team's lines alone.

What runs in a tick here (bots_match_due, bots_match_note_line,
bots_match_note_kill) takes no clock and no random number: the frame's
clock (its speed measured) is the frame's alone.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/items.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"

#include "bots.h"
#include "bots_command.h"
#include "bots_match.h"
#include "network_host_game.h"
#include "server_director.h"

#include "../src/port_config.h" /* port: port/linux/src/port_config.c */
#include "../src/server_json.h" /* port: port/linux/src/server_json.c */
#include "../src/server_mode.h" /* port: HALO_SERVER_BUILD */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int halo_server_mode(void);
unsigned long system_milliseconds(void);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the players a match's results keep */
	BOTS_MATCH_PLAYERS = 64,
	/* the kills the knowledge keeps */
	BOTS_MATCH_KILLS = 16,
	/* the frames a lobby set up waits before its start (the lobby's
	change sent to every machine first) */
	BOTS_MATCH_START_FRAMES = 30,
};

enum
{
	_bots_match_none = 0,
	/* asked for: the lobby set up, the game not started yet */
	_bots_match_starting,
	_bots_match_playing,
	_bots_match_over,
};

static char const *const bots_match_stage_names[] = { "none", "starting", "playing", "over" };
static char const *const bots_match_team_names[] = { "red", "blue" };

/* ---------- structures */

struct bots_match_player
{
	char name[16];
	short team;
	boolean bot;
	long score;
	long kills, deaths, assists, suicides, betrayals;
	long shots_fired, shots_hit;
	long flag_grabs, flag_returns, flag_captures;
	long ball_ticks, carrier_kills, hill_ticks, laps;
};

struct bots_match_line
{
	long tick;
	unsigned long teams;
	char line[BOTS_COMMAND_LINE_SIZE];
};

struct bots_match_kill
{
	long tick;
	char killer[16];
	char victim[16];
	short killer_team;
	short victim_team;
	char weapon[32];
};

/* ---------- globals */

static struct
{
	short stage;
	/* the matches started (any game the host played counts) */
	long number;
	/* the one asked for, and the last one (match restart's) */
	struct bots_match_options asked;
	struct bots_match_options last;
	boolean has_last;
	boolean has_asked;
	boolean start_requested;
	/* match start ran: the bots play with nobody else */
	boolean bots_alone;
	long lobby_frames;
	/* the game being played: its map and mode, its seed, when it began */
	char map[BOTS_MATCH_NAME_SIZE];
	char mode[16];
	unsigned long seed;
	long start_tick;
	long end_tick;
	/* (the ticks' own: the first and last that could score, as the bots'
	ticks saw them, whatever ticks a frame ran) */
	boolean tick_seen;
	/* match speed: asked, run, and the frames' measure of it (ticks a
	second over real seconds, a second at a time) */
	real speed;
	real measured;
	unsigned long measure_wall;
	long measure_tick;
	unsigned long started_wall;
	unsigned long ended_wall;
	/* match at's lines for their ticks (in order of tick, then of
	arrival), the next one due */
	struct bots_match_line schedule[BOTS_MATCH_LINES];
	long schedule_count;
	long schedule_next;
	/* the lines the ticks applied */
	struct bots_match_line lines[BOTS_MATCH_LINES];
	long line_count;
	long lines_dropped;
	/* the results: the players and teams when it ended */
	boolean has_results;
	struct bots_match_player players[BOTS_MATCH_PLAYERS];
	long player_count;
	long team_scores[2];
	boolean teams;
	long score_to_win;
	/* the last kills */
	struct bots_match_kill kills[BOTS_MATCH_KILLS];
	long kill_count;
} bots_match = { 0, 0, { "", "", NONE, 0, NONE, NONE }, { "", "", NONE, 0, NONE, NONE }, FALSE, FALSE, FALSE, FALSE, 0,
	"", "", 0, 0, 0, 1.0f };

/* ---------- the pure parts */

boolean bots_match_parse_start(char **words, long count, struct bots_match_options *options, char *error,
	long error_size)
{
	long index, positional = 0;

	csmemset(options, 0, sizeof(*options));
	options->bots_per_team = NONE;
	options->score = NONE;
	options->minutes = NONE;
	for (index = 0; index < count; index++)
	{
		char const *word = words[index];

		if (!strcmp(word, "--bots") || !strcmp(word, "--seed") || !strcmp(word, "--score") || !strcmp(word, "--time"))
		{
			char *end;
			double value;

			if (index + 1 >= count)
			{
				snprintf(error, (size_t)error_size, "%s needs a number", word);
				return FALSE;
			}
			value = strtod(words[index + 1], &end);
			if (end == words[index + 1] || *end || value < 0.0 || value != floor(value) ||
				(!strcmp(word, "--seed") ? value > 4294967295.0 || value < 1.0 :
				!strcmp(word, "--bots") ? value > BOTS_MAXIMUM / 2 : !strcmp(word, "--score") ? value > 999.0 :
				value > 1440.0))
			{
				snprintf(error, (size_t)error_size, "%s takes a whole number%s", word,
					!strcmp(word, "--seed") ? " from 1" : !strcmp(word, "--bots") ? " of bots a team, 0 to 32" :
					!strcmp(word, "--score") ? " from 0 to 999" : " of minutes, 0 (none) to 1440");
				return FALSE;
			}
			if (!strcmp(word, "--bots"))
				options->bots_per_team = (long)value;
			else if (!strcmp(word, "--seed"))
				options->seed = (unsigned long)value;
			else if (!strcmp(word, "--score"))
				options->score = (long)value;
			else
				options->minutes = (long)value;
			index++;
			continue;
		}
		if (word[0] == '-' && word[1] == '-')
		{
			snprintf(error, (size_t)error_size, "match start takes --bots, --seed, --score and --time, not %s", word);
			return FALSE;
		}
		/* (map and gametype: words of the rotation's, no rules of their own) */
		if (positional >= 2 || strlen(word) >= BOTS_MATCH_NAME_SIZE || strchr(word, ':') || strchr(word, '+') ||
			strchr(word, ','))
		{
			snprintf(error, (size_t)error_size, "match start <map> <gametype> [--bots N] [--seed S] [--score N] "
				"[--time M]");
			return FALSE;
		}
		snprintf(positional ? options->gametype : options->map, BOTS_MATCH_NAME_SIZE, "%s", word);
		positional++;
	}
	if (positional < 2)
	{
		snprintf(error, (size_t)error_size, "match start <map> <gametype> [--bots N] [--seed S] [--score N] [--time M]");
		return FALSE;
	}
	return TRUE;
}

void bots_match_entry_text(struct bots_match_options const *options, char *text, long size)
{
	long length = snprintf(text, (size_t)size, "%s:%s", options->map, options->gametype);

	if (options->score != NONE && length < size)
		length += snprintf(text + length, (size_t)(size - length), "+score_to_win=%ld", options->score);
	if (options->minutes != NONE && length < size)
		snprintf(text + length, (size_t)(size - length), "+time_limit=%ld", options->minutes);
}

boolean bots_match_speed_allowed(boolean server_mode, long remote_machines)
{
	return server_mode || remote_machines == 0;
}

real bots_match_speed_kept(real asked)
{
	if (!(asked > 0.0f))
		return 1.0f;
	return asked > (real)BOTS_MATCH_SPEED_MAXIMUM ? (real)BOTS_MATCH_SPEED_MAXIMUM : asked;
}

/* ---------- the game */

static struct network_game_server *bots_match_server(void)
{
	return game_connection() == _game_connection_network_server ? global_network_game_server_get() : NULL;
}

static boolean bots_match_in_lobby(void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server && !network_game_server_playing(server) && !network_game_server_port_start_sent(server);
}

static long bots_match_remote_machines(void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server ? network_game_server_port_remote_machine_count(server, FALSE) : 0;
}

static void bots_match_player_name(struct player_datum const *player, char *name, long size)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(player->name) && index < size - 1 && player->name[index]; index++)
		name[index] = player_name_character_ascii(player->name[index]);
	name[index] = 0;
}

static char const *bots_match_engine_name(long engine)
{
	static char const *const names[] = { "none", "ctf", "slayer", "oddball", "king", "race" };

	return engine >= 0 && engine < (long)NUMBEROF(names) ? names[engine] : "other";
}

/* the players' and teams' statistics as the game has them now */
static void bots_match_take_results(void)
{
	struct game_variant *variant = game_engine_running() ? game_engine_get_variant() : NULL;
	struct data_iterator iterator;
	struct player_datum *player;

	bots_match.player_count = 0;
	bots_match.teams = variant && game_engine_has_teams();
	bots_match.team_scores[0] = bots_match.teams ? game_engine_get_team_score(0) : 0;
	bots_match.team_scores[1] = bots_match.teams ? game_engine_get_team_score(1) : 0;
	bots_match.score_to_win = variant ? variant->universal_variant.score_to_win : 0;
	if (!player_data)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
		bots_match.player_count < BOTS_MATCH_PLAYERS)
	{
		struct bots_match_player *out = &bots_match.players[bots_match.player_count];
		struct game_statistics const *statistics = &player->statistics;
		union multiplayer_statistics const *game = &statistics->multiplayer_statistics;
		long engine = variant ? variant->game_engine_index : NONE;

		csmemset(out, 0, sizeof(*out));
		bots_match_player_name(player, out->name, sizeof(out->name));
		out->team = bots_match.teams && VALID_INDEX(player->team_index, 2) ? (short)player->team_index : (short)NONE;
		out->bot = bots_player_is_bot(iterator.datum_index) != 0;
		out->score = game_engine && game_engine->get_player_score ?
			game_engine->get_player_score(DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), _get_score_individual) : 0;
		out->kills = statistics->kills[0];
		out->deaths = statistics->deaths;
		out->assists = statistics->assists[0];
		out->suicides = statistics->suicides;
		out->betrayals = statistics->friendly_fire_kills;
		out->shots_fired = statistics->shots_fired;
		out->shots_hit = statistics->shots_hit;
		if (engine == game_engine_ctf)
		{
			out->flag_grabs = game->ctf_statistics.flag_grabs;
			out->flag_returns = game->ctf_statistics.flag_returns;
			out->flag_captures = game->ctf_statistics.flag_scores;
		}
		else if (engine == game_engine_oddball)
		{
			out->ball_ticks = (unsigned short)game->oddball_statistics.time_with_the_ball;
			out->carrier_kills = game->oddball_statistics.ball_carrier_kills;
		}
		else if (engine == game_engine_king)
			out->hill_ticks = (unsigned short)game->king_statistics.time_on_hill;
		else if (engine == game_engine_race)
			out->laps = game->race_statistics.laps;
		bots_match.player_count++;
	}
}

static void bots_match_event(char const *name, boolean results)
{
	struct server_json json;

	bots_command_event_open(&json, name);
	server_json_integer(&json, "match", bots_match.number);
	server_json_string(&json, "map", bots_match.map);
	server_json_string(&json, "mode", bots_match.mode);
	if (results)
	{
		long winner_score = 0, index;
		char const *winner = "";

		if (bots_match.teams)
		{
			winner = bots_match.team_scores[0] > bots_match.team_scores[1] ? "red" :
				bots_match.team_scores[1] > bots_match.team_scores[0] ? "blue" : "tie";
			server_json_array_begin(&json, "score");
			server_json_integer(&json, NULL, bots_match.team_scores[0]);
			server_json_integer(&json, NULL, bots_match.team_scores[1]);
			server_json_array_end(&json);
		}
		else
		{
			for (index = 0; index < bots_match.player_count; index++)
			{
				if (!winner[0] || bots_match.players[index].score > winner_score)
				{
					winner = bots_match.players[index].name;
					winner_score = bots_match.players[index].score;
				}
			}
		}
		server_json_string(&json, "winner", winner);
		server_json_integer(&json, "ticks", bots_match.end_tick - bots_match.start_tick);
	}
	bots_command_event_close(&json, 3);
}

/* ---------- each frame */

void bots_match_frame(void)
{
	struct network_game_server *server = bots_match_server();
	boolean playing = server && game_in_progress() && game_engine_running();
	boolean over = playing && !game_engine_can_score();
	unsigned long now = system_milliseconds();

	/* (a game begun: by match start, or any other way) */
	if (playing && !over && bots_match.stage != _bots_match_playing)
	{
		struct game_variant *variant = game_engine_get_variant();
		char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
		char const *part = name ? strrchr(name, '\\') : NULL;

		bots_match.stage = _bots_match_playing;
		bots_match.number++;
		snprintf(bots_match.map, sizeof(bots_match.map), "%s", part ? part + 1 : name ? name : "");
		snprintf(bots_match.mode, sizeof(bots_match.mode), "%s", bots_match_engine_name(variant->game_engine_index));
		bots_match.seed = (unsigned long)network_game_get_random_seed();
		bots_match.start_tick = game_time_get();
		bots_match.end_tick = bots_match.start_tick;
		bots_match.tick_seen = FALSE;
		bots_match.started_wall = now;
		bots_match.measure_wall = now;
		bots_match.measure_tick = bots_match.start_tick;
		bots_match.measured = 0.0f;
		bots_match.has_results = FALSE;
		bots_match.kill_count = 0;
		/* (match start's lines and its schedule are this game's; a game the
		host began otherwise starts with none) */
		if (!bots_match.has_asked)
		{
			bots_match.line_count = 0;
			bots_match.lines_dropped = 0;
		}
		bots_match.has_asked = FALSE;
		bots_match.start_requested = FALSE;
		platform_log("bots: match %ld on %s (%s), seed %lu", bots_match.number, bots_match.map, bots_match.mode,
			bots_match.seed);
		bots_match_event("match_started", FALSE);
	}
	/* (its end: the postgame, or the game gone before one) */
	if (bots_match.stage == _bots_match_playing && (over || !playing))
	{
		bots_match.stage = _bots_match_over;
		bots_match.ended_wall = now;
		if (playing)
			bots_match_take_results();
		bots_match.has_results = TRUE;
		platform_log("bots: match %ld over at tick %ld (%.1f s of game in %.1f s)", bots_match.number,
			bots_match.end_tick, (bots_match.end_tick - bots_match.start_tick) / (double)TICKS_PER_SECOND,
			(now - bots_match.started_wall) / 1000.0);
		bots_match_event("match_over", TRUE);
	}

	/* (asked for: its seed in the lobby, and the lobby's start) */
	if (bots_match.has_asked && bots_match_in_lobby())
	{
		if (bots_match.asked.seed)
			network_game_set_random_seed((long)bots_match.asked.seed);
		bots_match.lobby_frames++;
		if (!halo_server_mode() && !bots_match.start_requested && bots_match.lobby_frames >= BOTS_MATCH_START_FRAMES)
		{
			bots_match.start_requested = TRUE;
			network_game_client_request_immediate_start();
		}
	}

	/* (its speed: where nobody else plays alone) */
	if (playing && !over)
	{
		boolean allowed = bots_match_speed_allowed(halo_server_mode() != 0, bots_match_remote_machines());
		real wanted = allowed ? bots_match.speed : 1.0f;

		if (!allowed && bots_match.speed != 1.0f)
		{
			struct server_json json;

			bots_match.speed = 1.0f;
			bots_command_event_open(&json, "match_speed");
			server_json_real(&json, "speed", 1.0);
			server_json_string(&json, "reason", "another machine is in the game");
			bots_command_event_close(&json, 3);
		}
		if (game_time_get_speed() != wanted)
			game_time_set_speed(wanted);
		if (now - bots_match.measure_wall >= 1000)
		{
			bots_match.measured = (real)((game_time_get() - bots_match.measure_tick) /
				(double)TICKS_PER_SECOND / ((now - bots_match.measure_wall) / 1000.0));
			bots_match.measure_wall = now;
			bots_match.measure_tick = game_time_get();
		}
	}
}

boolean bots_match_bots_alone(void)
{
	return bots_match.bots_alone;
}

/* ---------- the ticks' parts */

void bots_match_note_tick(long tick, boolean scoring)
{
	if (!scoring || bots_match.stage != _bots_match_playing)
		return;
	if (!bots_match.tick_seen)
		bots_match.start_tick = tick;
	bots_match.tick_seen = TRUE;
	bots_match.end_tick = tick + 1;
}

boolean bots_match_due(long tick, char *line, long size)
{
	struct bots_match_line const *next;

	if (bots_match.schedule_next >= bots_match.schedule_count)
		return FALSE;
	next = &bots_match.schedule[bots_match.schedule_next];
	if (next->tick > tick)
		return FALSE;
	snprintf(line, (size_t)size, "%s", next->line);
	bots_match.schedule_next++;
	return TRUE;
}

void bots_match_note_line(long tick, char const *line, unsigned long teams)
{
	struct bots_match_line *kept;
	long length;

	if (bots_match.line_count >= BOTS_MATCH_LINES)
	{
		bots_match.lines_dropped++;
		return;
	}
	kept = &bots_match.lines[bots_match.line_count++];
	kept->tick = tick;
	kept->teams = teams;
	/* (the line as it plays again: its spaces and --json off) */
	while (*line == ' ')
		line++;
	snprintf(kept->line, sizeof(kept->line), "%s", line);
	{
		char *json = strstr(kept->line, " --json");

		if (json)
			memmove(json, json + 7, strlen(json + 7) + 1);
		length = (long)strlen(kept->line);
		while (length > 0 && kept->line[length - 1] == ' ')
			kept->line[--length] = 0;
	}
}

void bots_match_note_kill(long tick, long killer, long victim, char const *weapon)
{
	struct bots_match_kill *kill;
	struct player_datum *killing = killer != NONE ? player_try_and_get(killer) : NULL;
	struct player_datum *dead = victim != NONE ? player_try_and_get(victim) : NULL;
	boolean teams = game_engine_running() && game_engine_has_teams();

	if (bots_match.kill_count >= BOTS_MATCH_KILLS)
	{
		memmove(&bots_match.kills[0], &bots_match.kills[1], sizeof(bots_match.kills[0]) * (BOTS_MATCH_KILLS - 1));
		bots_match.kill_count = BOTS_MATCH_KILLS - 1;
	}
	kill = &bots_match.kills[bots_match.kill_count++];
	csmemset(kill, 0, sizeof(*kill));
	kill->tick = tick;
	if (killing)
		bots_match_player_name(killing, kill->killer, sizeof(kill->killer));
	if (dead)
		bots_match_player_name(dead, kill->victim, sizeof(kill->victim));
	kill->killer_team = killing && teams && VALID_INDEX(killing->team_index, 2) ? (short)killing->team_index : (short)NONE;
	kill->victim_team = dead && teams && VALID_INDEX(dead->team_index, 2) ? (short)dead->team_index : (short)NONE;
	snprintf(kill->weapon, sizeof(kill->weapon), "%s", weapon ? weapon : "");
}

void bots_match_kills_json(struct server_json *json, char const *key)
{
	long index;

	server_json_array_begin(json, key);
	for (index = 0; index < bots_match.kill_count; index++)
	{
		struct bots_match_kill const *kill = &bots_match.kills[index];

		server_json_object_begin(json, NULL);
		server_json_integer(json, "tick", kill->tick);
		if (kill->killer[0])
			server_json_string(json, "killer", kill->killer);
		else
			server_json_null(json, "killer");
		if (VALID_INDEX(kill->killer_team, 2))
			server_json_string(json, "killer_team", bots_match_team_names[kill->killer_team]);
		server_json_string(json, "victim", kill->victim);
		if (VALID_INDEX(kill->victim_team, 2))
			server_json_string(json, "victim_team", bots_match_team_names[kill->victim_team]);
		if (kill->weapon[0])
			server_json_string(json, "weapon", kill->weapon);
		server_json_object_end(json);
	}
	server_json_array_end(json);
}

/* ---------- the commands */

static void bots_match_say(char *reply, long reply_size, char const *text)
{
	long length = (long)strlen(reply);

	if (length < reply_size - 1)
		snprintf(reply + length, (size_t)(reply_size - length), "%s\n", text);
}

static void bots_match_say_json(char *reply, long reply_size, struct server_json *json)
{
	char const *text = server_json_text(json);

	if ((long)strlen(text) + 2 >= reply_size - (long)strlen(reply))
		bots_match_say(reply, reply_size, "{\"ok\":false,\"error\":\"the answer is too long\"}");
	else
		bots_match_say(reply, reply_size, text);
	server_json_free(json);
}

static void bots_match_results_json(struct server_json *json, short team)
{
	long index, red = 0, blue = 0;
	boolean live = bots_match.stage == _bots_match_playing;

	if (live)
		bots_match_take_results();
	server_json_integer(json, "match", bots_match.number);
	server_json_string(json, "state", bots_match_stage_names[bots_match.stage]);
	server_json_string(json, "map", bots_match.map);
	server_json_string(json, "mode", bots_match.mode);
	server_json_integer(json, "seed", (long)bots_match.seed);
	if (bots_match.teams)
	{
		server_json_string(json, "winner", live ? "" : bots_match.team_scores[0] > bots_match.team_scores[1] ? "red" :
			bots_match.team_scores[1] > bots_match.team_scores[0] ? "blue" : "tie");
		server_json_array_begin(json, "score");
		server_json_integer(json, NULL, bots_match.team_scores[0]);
		server_json_integer(json, NULL, bots_match.team_scores[1]);
		server_json_array_end(json);
	}
	else
	{
		long best = NONE;

		for (index = 0; index < bots_match.player_count; index++)
		{
			if (best == NONE || bots_match.players[index].score > bots_match.players[best].score)
				best = index;
		}
		server_json_string(json, "winner", live || best == NONE ? "" : bots_match.players[best].name);
	}
	server_json_integer(json, "score_to_win", bots_match.score_to_win);
	server_json_integer(json, "ticks", bots_match.end_tick - bots_match.start_tick);
	server_json_real(json, "seconds", floor((bots_match.end_tick - bots_match.start_tick) * 10.0 / TICKS_PER_SECOND) / 10.0);
	server_json_real(json, "real_seconds", floor(((live ? system_milliseconds() : bots_match.ended_wall) -
		bots_match.started_wall) / 100.0) / 10.0);
	server_json_array_begin(json, "players");
	for (index = 0; index < bots_match.player_count; index++)
	{
		struct bots_match_player const *player = &bots_match.players[index];

		server_json_object_begin(json, NULL);
		server_json_string(json, "name", player->name);
		if (VALID_INDEX(player->team, 2))
			server_json_string(json, "team", bots_match_team_names[player->team]);
		server_json_boolean(json, "bot", player->bot);
		server_json_integer(json, "score", player->score);
		server_json_integer(json, "kills", player->kills);
		server_json_integer(json, "deaths", player->deaths);
		server_json_integer(json, "assists", player->assists);
		server_json_integer(json, "suicides", player->suicides);
		server_json_integer(json, "betrayals", player->betrayals);
		server_json_integer(json, "shots_fired", player->shots_fired);
		server_json_integer(json, "shots_hit", player->shots_hit);
		if (player->shots_fired > 0)
			server_json_real(json, "accuracy", floor(player->shots_hit * 1000.0 / player->shots_fired) / 1000.0);
		else
			server_json_null(json, "accuracy");
		if (!strcmp(bots_match.mode, "ctf"))
		{
			server_json_integer(json, "flag_grabs", player->flag_grabs);
			server_json_integer(json, "flag_captures", player->flag_captures);
			server_json_integer(json, "flag_returns", player->flag_returns);
		}
		else if (!strcmp(bots_match.mode, "oddball"))
		{
			server_json_integer(json, "ball_seconds", player->ball_ticks / TICKS_PER_SECOND);
			server_json_integer(json, "carrier_kills", player->carrier_kills);
		}
		else if (!strcmp(bots_match.mode, "king"))
			server_json_integer(json, "hill_seconds", player->hill_ticks / TICKS_PER_SECOND);
		else if (!strcmp(bots_match.mode, "race"))
			server_json_integer(json, "laps", player->laps);
		server_json_object_end(json);
		if (player->team == 0)
			red++;
		else if (player->team == 1)
			blue++;
	}
	server_json_array_end(json);
	if (bots_match.teams)
	{
		short side;

		server_json_object_begin(json, "teams");
		for (side = 0; side < 2; side++)
		{
			long kills = 0, deaths = 0, captures = 0, returns = 0, grabs = 0, ball = 0, hill = 0, laps = 0;

			for (index = 0; index < bots_match.player_count; index++)
			{
				struct bots_match_player const *player = &bots_match.players[index];

				if (player->team != side)
					continue;
				kills += player->kills;
				deaths += player->deaths;
				captures += player->flag_captures;
				returns += player->flag_returns;
				grabs += player->flag_grabs;
				ball += player->ball_ticks;
				hill += player->hill_ticks;
				laps += player->laps;
			}
			server_json_object_begin(json, bots_match_team_names[side]);
			server_json_integer(json, "score", bots_match.team_scores[side]);
			server_json_integer(json, "players", side ? blue : red);
			server_json_integer(json, "kills", kills);
			server_json_integer(json, "deaths", deaths);
			if (!strcmp(bots_match.mode, "ctf"))
			{
				server_json_integer(json, "flag_captures", captures);
				server_json_integer(json, "flag_returns", returns);
				server_json_integer(json, "flag_grabs", grabs);
			}
			else if (!strcmp(bots_match.mode, "oddball"))
				server_json_integer(json, "ball_seconds", ball / TICKS_PER_SECOND);
			else if (!strcmp(bots_match.mode, "king"))
				server_json_integer(json, "hill_seconds", hill / TICKS_PER_SECOND);
			else if (!strcmp(bots_match.mode, "race"))
				server_json_integer(json, "laps", laps);
			server_json_object_end(json);
		}
		server_json_object_end(json);
	}
	/* (the lines the ticks applied: a coach's, its own team's alone) */
	server_json_array_begin(json, "orders");
	for (index = 0; index < bots_match.line_count; index++)
	{
		struct bots_match_line const *line = &bots_match.lines[index];

		if (team != NONE && line->teams != (1UL << team))
			continue;
		server_json_object_begin(json, NULL);
		server_json_integer(json, "tick", line->tick);
		server_json_string(json, "line", line->line);
		server_json_object_end(json);
	}
	server_json_array_end(json);
	server_json_integer(json, "orders_dropped", bots_match.lines_dropped);
}

static void bots_match_status_json(struct server_json *json)
{
	server_json_integer(json, "match", bots_match.number);
	server_json_string(json, "state", bots_match.has_asked ? "starting" : bots_match_stage_names[bots_match.stage]);
	server_json_string(json, "map", bots_match.has_asked ? bots_match.asked.map : bots_match.map);
	server_json_string(json, "mode", bots_match.has_asked ? bots_match.asked.gametype : bots_match.mode);
	server_json_integer(json, "seed", (long)(bots_match.has_asked && bots_match.asked.seed ? bots_match.asked.seed :
		bots_match.seed));
	server_json_integer(json, "tick", bots_match.stage == _bots_match_playing ? game_time_get() : bots_match.end_tick);
	server_json_integer(json, "ticks", bots_match.end_tick - bots_match.start_tick);
	if (bots_match.stage == _bots_match_playing && game_engine_has_teams())
	{
		server_json_array_begin(json, "score");
		server_json_integer(json, NULL, game_engine_get_team_score(0));
		server_json_integer(json, NULL, game_engine_get_team_score(1));
		server_json_array_end(json);
	}
	server_json_real(json, "speed", bots_match.speed);
	server_json_real(json, "speed_measured", floor(bots_match.measured * 10.0 + 0.5) / 10.0);
	server_json_boolean(json, "speed_allowed", bots_match_speed_allowed(halo_server_mode() != 0,
		bots_match_remote_machines()));
	server_json_integer(json, "scheduled", bots_match.schedule_count - bots_match.schedule_next);
	server_json_integer(json, "orders", bots_match.line_count);
}

/* match start's: the game asked for, set up as the host's controls set one */
static boolean bots_match_start(struct bots_match_options const *options, char *reason, long reason_size)
{
	char entry[256];
	long index;

	if (bots_matchmaking_on() || bots_training_on())
	{
		snprintf(reason, (size_t)reason_size, "this game's bots are its playlist's or Basic Training's");
		return FALSE;
	}
	if (!global_network_game_server_get())
	{
		snprintf(reason, (size_t)reason_size, "no network game is hosted here: host one (Multiplayer > CREATE GAME), "
			"or run a dedicated server");
		return FALSE;
	}
	if (options->bots_per_team != NONE)
	{
		char text[16];

		snprintf(text, sizeof(text), "%ld", options->bots_per_team * 2);
		for (index = 0; index < options->bots_per_team * 2; index++)
			bots_command_set_join_team(index, (short)(index % 2));
		config_set_for_run("bots.make_room", "false");
		config_set_for_run("bots.fill_to", "0");
		config_set_for_run("bots.count", text);
	}
	bots_match_entry_text(options, entry, sizeof(entry));
	if (halo_server_mode())
	{
#if HALO_SERVER_BUILD
		if (!server_director_interpose(entry, reason, reason_size))
			return FALSE;
#else
		return FALSE;
#endif
	}
	else if (!network_host_game_change(entry, _host_game_now, NONE, reason, reason_size))
		return FALSE;
	bots_match.asked = *options;
	bots_match.last = *options;
	bots_match.has_last = TRUE;
	bots_match.has_asked = TRUE;
	bots_match.bots_alone = TRUE;
	bots_match.start_requested = FALSE;
	bots_match.lobby_frames = 0;
	bots_match.line_count = 0;
	bots_match.lines_dropped = 0;
	bots_match.schedule_count = 0;
	bots_match.schedule_next = 0;
	return TRUE;
}

boolean bots_match_command(char **words, long count, boolean json, short team, char *reply, long reply_size,
	char *error, long error_size)
{
	struct server_json out;
	char const *verb = count > 0 ? words[0] : "status";
	boolean host_only = !strcmp(verb, "start") || !strcmp(verb, "restart") || !strcmp(verb, "end") ||
		!strcmp(verb, "speed") || !strcmp(verb, "at");

	reply[0] = 0;
	if (host_only && team != NONE)
	{
		snprintf(error, (size_t)error_size, "a team's coach reads the match; only the host runs it");
		return FALSE;
	}
	if (!strcmp(verb, "status") || !strcmp(verb, "results"))
	{
		if (count > 1)
		{
			snprintf(error, (size_t)error_size, "match %s takes nothing more", verb);
			return FALSE;
		}
		if (!strcmp(verb, "results") && bots_match.stage == _bots_match_none)
		{
			snprintf(error, (size_t)error_size, "no match has been played");
			return FALSE;
		}
		if (!json)
		{
			char line[160];
			long ticks = bots_match.end_tick - bots_match.start_tick;

			snprintf(line, sizeof(line), "match %ld: %s, %s on %s, %ld:%02ld played, speed %.1f (running at %.1f)",
				bots_match.number, bots_match.has_asked ? "starting" : bots_match_stage_names[bots_match.stage],
				bots_match.has_asked ? bots_match.asked.gametype : bots_match.mode,
				bots_match.has_asked ? bots_match.asked.map : bots_match.map, ticks / TICKS_PER_SECOND / 60,
				ticks / TICKS_PER_SECOND % 60, bots_match.speed, bots_match.measured);
			bots_match_say(reply, reply_size, line);
			if (!strcmp(verb, "results"))
				bots_match_say(reply, reply_size, "(match results --json lists the players, teams and orders)");
			return TRUE;
		}
		server_json_begin(&out);
		server_json_object_begin(&out, NULL);
		server_json_boolean(&out, "ok", TRUE);
		if (!strcmp(verb, "status"))
			bots_match_status_json(&out);
		else
			bots_match_results_json(&out, team);
		server_json_object_end(&out);
		bots_match_say_json(reply, reply_size, &out);
		return TRUE;
	}
	if (!strcmp(verb, "start") || !strcmp(verb, "restart"))
	{
		struct bots_match_options options;
		char reason[160];

		if (!strcmp(verb, "restart"))
		{
			if (count > 1 || !bots_match.has_last)
			{
				snprintf(error, (size_t)error_size, count > 1 ? "match restart takes nothing more" :
					"no match was started here (match start)");
				return FALSE;
			}
			options = bots_match.last;
		}
		else if (!bots_match_parse_start(words + 1, count - 1, &options, error, error_size))
			return FALSE;
		if (!bots_match_start(&options, reason, sizeof(reason)))
		{
			snprintf(error, (size_t)error_size, "%s", reason);
			return FALSE;
		}
		if (json)
		{
			server_json_begin(&out);
			server_json_object_begin(&out, NULL);
			server_json_boolean(&out, "ok", TRUE);
			server_json_string(&out, "map", options.map);
			server_json_string(&out, "mode", options.gametype);
			server_json_integer(&out, "seed", (long)options.seed);
			server_json_string(&out, "when", reason);
			server_json_object_end(&out);
			bots_match_say_json(reply, reply_size, &out);
		}
		else
		{
			char line[200];

			snprintf(line, sizeof(line), "%s on %s: %s", options.gametype, options.map, reason);
			bots_match_say(reply, reply_size, line);
		}
		return TRUE;
	}
	if (!strcmp(verb, "end"))
	{
		if (count > 1 || bots_match.stage != _bots_match_playing || !game_engine_can_score())
		{
			snprintf(error, (size_t)error_size, count > 1 ? "match end takes nothing more" : "no match is being played");
			return FALSE;
		}
		if (halo_server_mode())
			server_director_end_game(_server_end_command, FALSE);
		else
			game_engine_end_game();
		bots_match_say(reply, reply_size, json ? "{\"ok\":true}" : "the match ends (its results: match results)");
		return TRUE;
	}
	if (!strcmp(verb, "speed"))
	{
		char *end;
		double value;
		char line[96];

		if (count != 2)
		{
			snprintf(error, (size_t)error_size, "match speed <times real time, up to %d; 1 for real time>",
				BOTS_MATCH_SPEED_MAXIMUM);
			return FALSE;
		}
		value = !strcmp(words[1], "max") ? (double)BOTS_MATCH_SPEED_MAXIMUM : strtod(words[1], &end);
		if (strcmp(words[1], "max") && (end == words[1] || *end || !(value >= 0.1 && value <= BOTS_MATCH_SPEED_MAXIMUM)))
		{
			snprintf(error, (size_t)error_size, "match speed is a number from 0.1 to %d, or max", BOTS_MATCH_SPEED_MAXIMUM);
			return FALSE;
		}
		if (value != 1.0 && !bots_match_speed_allowed(halo_server_mode() != 0, bots_match_remote_machines()))
		{
			snprintf(error, (size_t)error_size, "another machine is in the game: it plays at real time (match speed "
				"is for a dedicated server's or a host's game of bots alone)");
			return FALSE;
		}
		bots_match.speed = bots_match_speed_kept((real)value);
		if (json)
			snprintf(line, sizeof(line), "{\"ok\":true,\"speed\":%.2f}", bots_match.speed);
		else
			snprintf(line, sizeof(line), "speed %.2f times real time%s", bots_match.speed,
				bots_match.stage == _bots_match_playing ? "" : " (from the next game)");
		bots_match_say(reply, reply_size, line);
		return TRUE;
	}
	if (!strcmp(verb, "at"))
	{
		char *end;
		long tick, index, length = 0;
		struct bots_match_line *kept;
		char line[BOTS_COMMAND_LINE_SIZE];

		tick = count >= 3 ? strtol(words[1], &end, 10) : -1;
		if (count < 3 || end == words[1] || *end || tick < 0)
		{
			snprintf(error, (size_t)error_size, "match at <tick> <order|set|team line>");
			return FALSE;
		}
		if (bots_match.schedule_count >= BOTS_MATCH_LINES)
		{
			snprintf(error, (size_t)error_size, "%d lines wait for their ticks already", BOTS_MATCH_LINES);
			return FALSE;
		}
		line[0] = 0;
		for (index = 2; index < count && length < (long)sizeof(line) - 1; index++)
		{
			length += snprintf(line + length, sizeof(line) - (size_t)length, strchr(words[index], ' ') ? "%s\"%s\"" :
				"%s%s", index > 2 ? " " : "", words[index]);
		}
		if (strncmp(line, "order ", 6) && strncmp(line, "set ", 4) && strncmp(line, "team ", 5))
		{
			snprintf(error, (size_t)error_size, "match at takes an order, set or team line");
			return FALSE;
		}
		/* (in order of tick, after those of its tick already) */
		for (index = bots_match.schedule_count; index > bots_match.schedule_next &&
			bots_match.schedule[index - 1].tick > tick; index--)
		{
			bots_match.schedule[index] = bots_match.schedule[index - 1];
		}
		kept = &bots_match.schedule[index];
		kept->tick = tick;
		kept->teams = 0;
		snprintf(kept->line, sizeof(kept->line), "%s", line);
		bots_match.schedule_count++;
		bots_match_say(reply, reply_size, json ? "{\"ok\":true}" : "at its tick");
		return TRUE;
	}
	snprintf(error, (size_t)error_size, "match start|restart|end|status|speed|results|at");
	return FALSE;
}
