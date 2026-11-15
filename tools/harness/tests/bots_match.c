/*
BOTS_MATCH.C (test)

A match run from the bot control (port/linux/game/bots_match.c): match
start's words, its rotation entry, where match speed may run and how fast,
match at's lines given to their ticks in order, and the lines kept for the
results within their bound. test_bots_match.py takes the constants,
structures and globals (config.inc) and the code (under_test.inc).
*/

#include "harness.h"

#include <math.h>

#define csmemset memset
#define BOTS_MAXIMUM 64
#define TICKS_PER_SECOND 30

#include "config.inc"
#include "under_test.inc"

static boolean parse(char const *line, struct bots_match_options *options, char *error)
{
	static char copy[256];
	char *words[16];
	long count = 0;
	char *word;

	snprintf(copy, sizeof(copy), "%s", line);
	for (word = strtok(copy, " "); word && count < 16; word = strtok(NULL, " "))
		words[count++] = word;
	return bots_match_parse_start(words, count, options, error, 160);
}

static void case_parse(void)
{
	struct bots_match_options options;
	char error[160];

	CHECK(parse("bloodgulch ctf --bots 4 --seed 7 --score 3 --time 5", &options, error), "a whole start: %s", error);
	CHECK(!strcmp(options.map, "bloodgulch") && !strcmp(options.gametype, "ctf"), "map and gametype");
	CHECK(options.bots_per_team == 4 && options.seed == 7 && options.score == 3 && options.minutes == 5,
		"its numbers (%ld %lu %ld %ld)", options.bots_per_team, options.seed, options.score, options.minutes);
	CHECK(parse("bloodgulch slayer", &options, error), "map and gametype alone: %s", error);
	CHECK(options.bots_per_team == NONE && options.seed == 0 && options.score == NONE && options.minutes == NONE,
		"none of its own: the gametype's");
	CHECK(!parse("bloodgulch", &options, error), "no gametype");
	CHECK(!parse("bloodgulch ctf+score_to_win=1", &options, error), "a rule smuggled into the gametype");
	CHECK(!parse("bloodgulch:ctf slayer", &options, error), "an entry for a map");
	CHECK(!parse("bloodgulch ctf --seed 0", &options, error), "seed 0 (none)");
	CHECK(!parse("bloodgulch ctf --bots 40", &options, error), "more bots a team than there are");
	CHECK(!parse("bloodgulch ctf --bots 2.5", &options, error), "half a bot");
	CHECK(!parse("bloodgulch ctf --bots", &options, error), "--bots without its number");
	CHECK(!parse("bloodgulch ctf --fast", &options, error), "a word it does not take");
	CHECK(!parse("bloodgulch ctf extra", &options, error), "a third name");
}

static void case_entry(void)
{
	struct bots_match_options options;
	char error[160], text[256];

	CHECK(parse("bloodgulch ctf --score 3 --time 5", &options, error), "%s", error);
	bots_match_entry_text(&options, text, sizeof(text));
	CHECK(!strcmp(text, "bloodgulch:ctf+score_to_win=3+time_limit=5"), "the entry (%s)", text);
	CHECK(parse("bloodgulch slayer --seed 9", &options, error), "%s", error);
	bots_match_entry_text(&options, text, sizeof(text));
	CHECK(!strcmp(text, "bloodgulch:slayer"), "no limits: the gametype's (%s)", text);
}

static void case_speed(void)
{
	CHECK(bots_match_speed_allowed(TRUE, 3), "a dedicated server's game runs fast");
	CHECK(bots_match_speed_allowed(FALSE, 0), "a host's game of bots alone runs fast");
	CHECK(!bots_match_speed_allowed(FALSE, 1), "never with another machine in the game");
	CHECK(bots_match_speed_kept(8.0f) == 8.0f, "8 as asked");
	CHECK(bots_match_speed_kept(500.0f) == (real)BOTS_MATCH_SPEED_MAXIMUM, "kept to the most (%g)",
		bots_match_speed_kept(500.0f));
	CHECK(bots_match_speed_kept(0.0f) == 1.0f && bots_match_speed_kept(-2.0f) == 1.0f, "none: real time");
}

static void schedule(long tick, char const *line)
{
	struct bots_match_line *kept = &bots_match.schedule[bots_match.schedule_count++];

	kept->tick = tick;
	snprintf(kept->line, sizeof(kept->line), "%s", line);
}

static void case_schedule(void)
{
	char line[BOTS_COMMAND_LINE_SIZE];

	memset(&bots_match, 0, sizeof(bots_match));
	schedule(100, "order red attack");
	schedule(100, "order blue defend");
	schedule(250, "order red retreat");
	CHECK(!bots_match_due(99, line, sizeof(line)), "none before its tick");
	CHECK(bots_match_due(100, line, sizeof(line)) && !strcmp(line, "order red attack"), "its tick's first (%s)", line);
	CHECK(bots_match_due(100, line, sizeof(line)) && !strcmp(line, "order blue defend"), "then its second (%s)", line);
	CHECK(!bots_match_due(100, line, sizeof(line)), "nothing more at 100");
	CHECK(!bots_match_due(249, line, sizeof(line)), "none at 249");
	CHECK(bots_match_due(250, line, sizeof(line)) && !strcmp(line, "order red retreat"), "250's (%s)", line);
	CHECK(!bots_match_due(9999, line, sizeof(line)), "all given once");
}

static void case_lines(void)
{
	long index;

	memset(&bots_match, 0, sizeof(bots_match));
	for (index = 0; index < BOTS_MATCH_LINES + 5; index++)
		bots_match_note_line(index, "order red attack", 1);
	CHECK(bots_match.line_count == BOTS_MATCH_LINES, "kept to its bound (%ld)", bots_match.line_count);
	CHECK(bots_match.lines_dropped == 5, "the rest counted (%ld)", bots_match.lines_dropped);
	CHECK(bots_match.lines[7].tick == 7 && bots_match.lines[7].teams == 1, "each line's tick and teams");
}

int main(int argc, char **argv)
{
	static struct
	{
		char const *name;
		void (*run)(void);
	} const cases[] =
	{
		{ "parse", case_parse },
		{ "entry", case_entry },
		{ "speed", case_speed },
		{ "schedule", case_schedule },
		{ "lines", case_lines },
	};
	long index;

	for (index = 0; argc > 1 && index < (long)(sizeof(cases) / sizeof(cases[0])); index++)
	{
		if (!strcmp(argv[1], cases[index].name))
		{
			cases[index].run();
			return 0;
		}
	}
	fprintf(stderr, "no case %s\n", argc > 1 ? argv[1] : "");
	return 2;
}
