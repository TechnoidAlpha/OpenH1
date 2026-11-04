#include "harness.h"

/* The computer's lobby's data (port/linux/game/lobby_menu.inc's
lobby_board_lay_out, lobby_board_tag, lobby_board_teams and
lobby_board_controls_for): a team game's players in their teams' columns, a
game without teams' (and Infection's) in one list down both; in each column
the host first, the party's members together, the bots last; a column past
its rows says how many more; each row's tag (HOST, BOT, PARTY, OPENH1,
OPENCE); this machine's own row (the one framed); the host's controls (START
GAME, GAME SETTINGS, a game of this machine alone's bots) and a player's
WAITING FOR THE HOST */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#include "config.inc"
#include "under_test.inc"

static struct lobby_board_entry player(short team)
{
	struct lobby_board_entry entry;

	memset(&entry, 0, sizeof(entry));
	entry.team = team;
	return entry;
}

static struct lobby_board_entry bot(short team)
{
	struct lobby_board_entry entry = player(team);

	entry.bot = TRUE;
	return entry;
}

/* the column and row an entry is shown in, FALSE if none */
static boolean shown_at(struct lobby_board_layout const *layout, short entry, short *column, short *row)
{
	for (*column = 0; *column < LOBBY_BOARD_COLUMNS; (*column)++)
	{
		for (*row = 0; *row < LOBBY_BOARD_ROWS; (*row)++)
		{
			if (layout->rows[*column][*row] == entry)
				return TRUE;
		}
	}
	return FALSE;
}

/* the host's game of this machine alone, in Team Slayer: the host (red) and
its five bots */
static short bots_game(struct lobby_board_entry *entries)
{
	entries[0] = player(0);
	entries[0].own = entries[0].host = TRUE;
	entries[0].kind = _lobby_kind_openh1;
	entries[1] = bot(1);
	entries[2] = bot(0);
	entries[3] = bot(1);
	entries[4] = bot(0);
	entries[5] = bot(1);
	return 6;
}

static void case_teams(void)
{
	struct lobby_board_entry entries[8];
	struct lobby_board_layout layout;
	short count = bots_game(entries), index, column, row;

	lobby_board_lay_out(entries, count, TRUE, &layout);
	CHECK(layout.counts[0] == 3 && layout.counts[1] == 3, "red has %d, blue %d", layout.counts[0], layout.counts[1]);
	for (index = 0; index < count; index++)
	{
		CHECK(shown_at(&layout, index, &column, &row), "player %d is not shown", index);
		CHECK(column == entries[index].team, "player %d of team %d in column %d", index, entries[index].team, column);
	}
	CHECK(layout.rows[0][0] == 0, "the host is not first in its column");
	/* (in each column the game's order, after the host) */
	CHECK(layout.rows[0][1] == 2 && layout.rows[0][2] == 4, "red's bots out of order");
	CHECK(layout.rows[1][0] == 1 && layout.rows[1][1] == 3 && layout.rows[1][2] == 5, "blue's bots out of order");
	CHECK(layout.rows[0][3] == NONE && layout.rows[1][3] == NONE, "rows past the players are filled");
	CHECK(!layout.more[0] && !layout.more[1], "no more than the rows, yet more said");
}

static void case_free_for_all(void)
{
	struct lobby_board_entry entries[LOBBY_BOARD_COLUMNS * LOBBY_BOARD_ROWS + 4];
	struct lobby_board_layout layout;
	short count, index, column, row, last = -1;

	/* (one list: the teams' columns one after the other) */
	count = bots_game(entries);
	lobby_board_lay_out(entries, count, FALSE, &layout);
	CHECK(layout.counts[0] == count && layout.counts[1] == 0, "a game without teams counts %d and %d",
		layout.counts[0], layout.counts[1]);
	for (index = 0; index < count; index++)
		CHECK(layout.rows[0][index] != NONE, "row %d of the list is empty", index);
	CHECK(layout.rows[1][0] == NONE, "a short list goes on into blue's column");
	CHECK(layout.rows[0][0] == 0 && layout.rows[0][count - 1] == 5, "the host first, then the game's order");
	/* (a list longer than one column: on down the next; longer than both:
	the last row says how many more) */
	count = LOBBY_BOARD_COLUMNS * LOBBY_BOARD_ROWS + 4;
	for (index = 0; index < count; index++)
		entries[index] = player(index % 2);
	lobby_board_lay_out(entries, count, FALSE, &layout);
	for (index = 0; index < count; index++)
	{
		if (!shown_at(&layout, index, &column, &row))
			continue;
		CHECK(column * LOBBY_BOARD_ROWS + row == index, "player %d at column %d row %d", index, column, row);
		last = index;
	}
	CHECK(last == LOBBY_BOARD_COLUMNS * LOBBY_BOARD_ROWS - 2, "the last shown is %d", last);
	CHECK(layout.more[1] == count - last - 1 && layout.rows[1][LOBBY_BOARD_ROWS - 1] == NONE,
		"the last row says %d more", layout.more[1]);
}

static void case_overflow(void)
{
	struct lobby_board_entry entries[LOBBY_BOARD_ROWS + 6];
	struct lobby_board_layout layout;
	short index;

	for (index = 0; index < LOBBY_BOARD_ROWS + 6; index++)
		entries[index] = player(index < LOBBY_BOARD_ROWS + 4 ? 1 : 0);
	lobby_board_lay_out(entries, LOBBY_BOARD_ROWS + 6, TRUE, &layout);
	CHECK(layout.counts[1] == LOBBY_BOARD_ROWS + 4 && layout.counts[0] == 2, "counted %d and %d", layout.counts[0],
		layout.counts[1]);
	CHECK(layout.more[1] == 5 && layout.rows[1][LOBBY_BOARD_ROWS - 1] == NONE, "blue's last row says %d more",
		layout.more[1]);
	CHECK(layout.rows[1][LOBBY_BOARD_ROWS - 2] == LOBBY_BOARD_ROWS - 2, "blue's rows before its last");
	CHECK(!layout.more[0], "red says more");
}

static void case_party(void)
{
	struct lobby_board_entry entries[6];
	struct lobby_board_layout layout;

	/* (the party's members together, after the host, whatever order they
	joined in; bots last) */
	entries[0] = bot(0);
	entries[1] = player(0);
	entries[2] = player(0);
	entries[2].party = TRUE;
	entries[3] = player(0);
	entries[3].host = TRUE;
	entries[4] = player(0);
	entries[4].party = TRUE;
	entries[4].own = TRUE;
	entries[5] = player(0);
	lobby_board_lay_out(entries, 6, TRUE, &layout);
	CHECK(layout.rows[0][0] == 3, "the host is not first (%d)", layout.rows[0][0]);
	CHECK(layout.rows[0][1] == 2 && layout.rows[0][2] == 4, "the party is not together after the host");
	CHECK(layout.rows[0][3] == 1 && layout.rows[0][4] == 5, "the others in the game's order");
	CHECK(layout.rows[0][5] == 0, "the bot is not last");
	CHECK(lobby_board_tag(&entries[2]) == _lobby_tag_party, "a party member's tag");
}

static void case_tags(void)
{
	struct lobby_board_entry entry;

	entry = bot(0);
	CHECK(lobby_board_tag(&entry) == _lobby_tag_bot, "a bot's tag");
	entry = player(0);
	entry.host = entry.own = TRUE;
	entry.kind = _lobby_kind_openh1;
	CHECK(lobby_board_tag(&entry) == _lobby_tag_host, "the host's tag");
	entry = player(1);
	entry.kind = _lobby_kind_opence;
	CHECK(lobby_board_tag(&entry) == _lobby_tag_opence, "an OpenCE player's tag");
	/* (OpenCE's players are in no party of OpenH1's; what they are is the
	more worth knowing) */
	entry.party = TRUE;
	CHECK(lobby_board_tag(&entry) == _lobby_tag_opence, "an OpenCE player named as a party's member");
	entry = player(1);
	entry.kind = _lobby_kind_openh1;
	CHECK(lobby_board_tag(&entry) == _lobby_tag_openh1, "an OpenH1 player's tag");
	entry.kind = _lobby_kind_unknown;
	CHECK(lobby_board_tag(&entry) == _lobby_tag_none, "a player no one knows is tagged");
	CHECK(!strcmp(lobby_tag_words_test[_lobby_tag_opence], "OPENCE") &&
		!strcmp(lobby_tag_words_test[_lobby_tag_host], "HOST") &&
		!strcmp(lobby_tag_words_test[_lobby_tag_bot], "BOT"), "the tags' words");
}

static void case_controls(void)
{
	struct lobby_board_controls controls;

	/* (host, co-op, bots play, teams, own player, invite, room) */
	lobby_board_controls_for(TRUE, FALSE, TRUE, TRUE, TRUE, FALSE, TRUE, &controls);
	CHECK(controls.start && controls.settings && !controls.waiting, "the host's controls");
	CHECK(controls.bots && !controls.bots_locked, "the host's bots");
	CHECK(controls.team_change && !controls.invite && controls.add_player, "the host's prompts");
	lobby_board_controls_for(TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, &controls);
	CHECK(controls.bots && controls.bots_locked, "a gametype the bots do not play leaves them unlocked");
	lobby_board_controls_for(TRUE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, &controls);
	CHECK(controls.bots && controls.invite, "a network game's host has no bots, or no invite");
	lobby_board_controls_for(TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, TRUE, &controls);
	CHECK(!controls.bots, "a co-op game's host has bots");
	lobby_board_controls_for(FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, FALSE, &controls);
	CHECK(!controls.start && !controls.settings && controls.waiting, "a player has the host's controls");
	CHECK(!controls.bots && controls.team_change && !controls.add_player, "a player's prompts");
	lobby_board_controls_for(FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, &controls);
	CHECK(!controls.team_change, "a game without teams has CHANGE TEAM");
	lobby_board_controls_for(FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, &controls);
	CHECK(!controls.team_change, "CHANGE TEAM with no player of this machine's");
	/* (the teams chosen in the lobby: a team game's, not Infection's) */
	CHECK(lobby_board_teams(TRUE, 0) && !lobby_board_teams(FALSE, 0), "a team game's teams");
	CHECK(!lobby_board_teams(TRUE, _game_mode_infection), "Infection's teams chosen in the lobby");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	if (!strcmp(case_name, "teams"))
		case_teams();
	else if (!strcmp(case_name, "free-for-all"))
		case_free_for_all();
	else if (!strcmp(case_name, "overflow"))
		case_overflow();
	else if (!strcmp(case_name, "party"))
		case_party();
	else if (!strcmp(case_name, "tags"))
		case_tags();
	else if (!strcmp(case_name, "controls"))
		case_controls();
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	return 0;
}
