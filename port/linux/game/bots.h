/* bots.h: computer players for the multiplayer games this computer hosts
(port/linux/game/bots.c)

A bot is a player of a game this machine hosts (a local split screen game,
or a system link or internet one) whose controls come from code instead of
a controller: it is in the lobby, the scores and the game's rules as any
player is, spawns, respawns and scores. Each bot is the one player of a
machine of its own at the top of the machine indices (BOTS_FIRST_MACHINE up),
which no real machine takes (network_server_manager.c numbers the machines
that join below them): a machine with no connection, whose player the host
plays as it plays a remote player whose input comes at once. The other
machines of a network game see an ordinary player of an ordinary machine.
The bots have no view and no controller: nothing is drawn for them. */
#ifndef HALO_BOTS_H
#define HALO_BOTS_H

struct player_action;
struct network_player;

/* the most bots in one game: half the session's machines, the other half
the real machines' (64 machines joining a game, each with up to four
players, more than the session's 128 players). A game takes no more bots
than its most players leave room for */
#define BOTS_MAXIMUM 64
/* the first bot's machine index: the bots' are the session's last
BOTS_MAXIMUM, which no real machine takes (network_server_manager.c numbers
those that join below them), and every machine of a game tells a bot's
player by it (bots_machine_is_bot: HALO_PORT_STANDARD_NETWORK_VERSION 17) */
#define BOTS_FIRST_MACHINE (HALO_PORT_MAXIMUM_NETWORK_MACHINES - BOTS_MAXIMUM)

enum
{
	/* bots.teams: in a team game, the bots on both teams (evening them
	with the players), or all on the team the players are not on */
	_bots_teams_even = 0,
	_bots_teams_against,
};

/* the settings (bots.count, bots.skill, bots.teams, bots.make_room,
bots.fill_to) */
long bots_wanted_count(void);
short bots_skill(void);
short bots_teams(void);
int bots_make_room(void);
long bots_fill_to(void);
/* the bots play the game engine's games (Slayer's, and so SWAT, Infection
and Gun Game; Capture the Flag's, Oddball's (Juggernaut's) and King of the
Hill's: bots_objectives.h) */
int bots_play_game_engine(long game_engine_index);

/* a matchmaking game's bots in place of the settings' (the host's of its
playlist: port/linux/game/matchmaking_menu.inc): count of them, making room
for players, filling the game to fill_to players and bots; count NONE: the
settings' again */
void bots_set_matchmaking(long count, long fill_to);
/* ... whether a matchmaking game's are set (Host Controls leaves them) */
int bots_matchmaking_on(void);
/* a machine is a bot's */
int bots_machine_is_bot(long machine_index);
/* a player (its datum) is a bot: on any machine of the game, as its machine
says (an OpenH1 machine's scores tag it) */
int bots_player_is_bot(long player_index);
/* the network player for bot number `bot_index` (0 up): its machine,
controller, name and no team or colour yet */
void bots_network_player(long bot_index, struct network_player *player);

/* the game tick (players_update_before_game): every bot's controls this
tick, into the actions of the players by their index, on the machine that
hosts the game (a network game's host gives them to its clients as it gives
a remote player's: player_queues_new.c's update_server_take_bot_action);
nothing on a client */
void bots_update_actions(struct player_action *actions);
/* debug.play_log's tick (port/linux/game/play_log.c) */
void play_log_tick(struct player_action const *actions);
/* the first local player's view turned toward the nearest enemy in sight
(debug.test_input's "hunt"), and how it moves (forward and strafe, -1 to
1); TRUE once on it, to fire */
boolean bots_hunt(float *forward, float *strafe);

/* a map's start and end (players.c) */
void bots_initialize_for_new_map(void);
void bots_dispose_from_old_map(void);

/* ---------- orders (port/linux/game/basic_training.c)

Basic training's bots are told what to do, each by its number (0 up): an
order holds until another is given, and the game's next map clears them
all. A bot under an order other than _bots_order_free does only that: it
does not look for enemies, nor walk to goals of its own, nor fire. One with
a post is put there each time it spawns (and once as the order is given),
turned the way the order says; a free bot with a post starts its fights
from there. */
enum
{
	/* its own play (the default), at its order's skill */
	_bots_order_free = 0,
	/* standing at its post, turned to the first local player */
	_bots_order_hold,
	/* side to side between its post and its second place, turned to the
	first local player */
	_bots_order_strafe,
	/* behind cover at its post, stepping out to its second place for a
	while every so often, turned to the first local player */
	_bots_order_peek,
	/* out of the game: its unit killed without a word, kept from spawning */
	_bots_order_park,
	NUMBER_OF_BOTS_ORDERS
};

struct bots_order
{
	short kind;
	/* its skill (_bots_skill_easy up), NONE: the setting's */
	short skill;
	/* where it is put as it spawns (has_post), and its second place */
	boolean has_post;
	real_point3d post;
	real_point3d other;
	/* strafe and peek: the ticks of a crossing, or of a peek out and of
	the wait behind cover */
	long period_ticks;
};

/* while basic training plays: four bots, the first three on the team the
players are not on, the fourth on theirs (bots_team), whatever bots.count
and bots.teams say; FALSE: the settings again (and every order cleared) */
void bots_set_training(int on);
int bots_training_on(void);
/* the team a bot joins (network_server_manager.c): basic training's, else
the team the setting chose */
long bots_team(long bot_index, long players_team, long chosen_team);
/* a bot's order (the bot that has none: free); NULL clears it */
void bots_set_order(long bot_index, struct bots_order const *order);
short bots_order_kind(long bot_index);
/* a player (its datum) is a bot under _bots_order_park
(game_engine_should_spawn_player) */
int bots_player_parked(long player_index);
/* the player of bot number bot_index, NONE if it is not in the game */
long bots_player_of_bot(long bot_index);
/* where an ordered bot goes this tick (strafe, peek: the place it walks
to; hold: its post), and whether it is out of cover (peek); game ticks
since the order was given (bots.c's, for basic_training.c's tests) */
void bots_order_goal(struct bots_order const *order, long ticks, real_point3d *goal, boolean *exposed);

#endif
