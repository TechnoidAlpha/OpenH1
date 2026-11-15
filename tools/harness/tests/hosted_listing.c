#include "harness.h"

#include <wchar.h>

/* A hosted internet game's listing in the server browser, as the menus set
it (port/linux/game/menu_functions.c, host_controls_menu.inc): Create Game >
Internet lists it as network.host_public says; Server Setup (first, and again
from the lobby's GAME SETTINGS between games) and its START GAME keep that,
and give a public game its password; Host Controls' LISTING shows it and
turns it from PUBLIC to PRIVATE and back, which Server Setup keeps after;
a local network game is never listed, nor has the row. The listing is
internet play's (p2p_lobby.c's p2p_set_hosting_public: listed while public,
withdrawn as it goes private, which tools/p2p_lobby_check.c checks) */

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define SET_FLAG(flags, bit, on) ((on) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define HOST_CONTROLS_CONFIRM_TIME 3000
#define _host_confirm_none NONE
#define usnprintf swprintf
#define ustrncpy wcsncpy

struct widget_instance
{
	char const *name;
	struct widget_instance *child, *next;
	union
	{
		struct
		{
			short selected_index;
		} list;
	} parameters;
};

struct event_record { int type; };
struct player_profile { wchar_t player_name[12]; };
struct network_game
{
	wchar_t name[64];
	short maximum_players, player_count;
	struct { long game_engine_index; struct { long score_to_win; boolean teams; } universal_variant; } variant;
	struct { short time_limit; } variant_options;
};
/* a next game (network_host_game.h's) */
struct server_entry { char text[64]; };
#define PASSWORD_LENGTH 32
#define HOST_CONTROLS_ANNOUNCE_LENGTH 100
enum { SOUND_CURSOR, SOUND_ERROR, SOUND_FORWARD };
enum { _friendly_fire_off, _friendly_fire_on, _friendly_fire_shields_only, _friendly_fire_explosives_only };

#include "config.inc"

/* ---------- the fake world */

/* config.toml's settings */
static boolean host_public = TRUE, online = TRUE, public_lobby = TRUE;

static int config_boolean(char const *name)
{
	if (!strcmp(name, "network.host_public"))
		return host_public;
	if (!strcmp(name, "network.online"))
		return online;
	if (!strcmp(name, "network.public_lobby"))
		return public_lobby;
	return 0;
}

/* (co-op's WHO CAN JOIN, network.coop_join: "friends", "anyone", "invite") */
static char coop_join[16] = "friends";

static boolean config_write(char const *name, char const *value)
{
	if (!strcmp(name, "network.coop_join"))
		snprintf(coop_join, sizeof(coop_join), "%s", value);
	return TRUE;
}

static char const *config_string(char const *name)
{
	return !strcmp(name, "network.coop_join") ? coop_join : "";
}
#define platform_log(...) ((void)0)

/* internet play's side (p2p.c, p2p_lobby.c): hosting from the internet, the
listing public, its password; how often it was listed and withdrawn */
static struct
{
	boolean hosting_allowed, public, has_password;
	int listings, withdrawals;
	/* (p2p_friends.c's: friends offered no JOIN, a co-op game's INVITE
	ONLY) */
	boolean closed;
} p2p;

static void p2p_friends_set_game_closed(int closed) { p2p.closed = closed != 0; }
static int p2p_friends_game_closed(void) { return p2p.closed; }

static void p2p_set_hosting_allowed(int allowed) { p2p.hosting_allowed = allowed != 0; }

static void p2p_set_hosting_public(int public)
{
	if (p2p.public && !public)
		p2p.withdrawals++;
	if (!p2p.public && public)
		p2p.listings++;
	p2p.public = public != 0;
}

static void p2p_set_hosting_password(char const *password) { p2p.has_password = password && *password; }

/* (in the server browser: p2p_hosting_listed, while hosting from the
internet) */
static boolean listed(void)
{
	return p2p.public && p2p.hosting_allowed && online && public_lobby;
}

/* the game hosted, its record */
static struct network_game game = { L"Host", 16, 1, { 2, { 25, FALSE } }, { 0 } };
static int server_made;

static void *global_network_game_server_get(void) { return server_made ? &game : NULL; }
static struct network_game *network_game_server_get_game(void *server) { return server; }
/* (a co-op game hosted: ONLINE CO-OP's, or co-op's HOST's) */
static boolean cooperative;
static boolean hosting_cooperative(void) { return cooperative && server_made; }
static void network_game_server_port_set_settings(wchar_t const *name, long maximum)
{
	(void)name;
	game.maximum_players = (short)maximum;
}
static void network_standard_server_mode_read(void) {}
static void network_game_server_port_set_cooperative_friendly_fire(short mode) { (void)mode; }
static void network_game_server_port_set_cooperative_player_collisions(boolean on) { (void)on; }
static void network_game_server_port_set_cooperative_ally_friendly_fire(boolean on) { (void)on; }

/* the menus around the code (menu_functions.c's) */
static boolean campaign_fail(void) { return FALSE; }
static boolean multiplayer_player(short controller) { (void)controller; return TRUE; }
static boolean ui_widget_port_host(struct widget_instance *widget, struct event_record *event, boolean *deleted)
{
	(void)widget; (void)event; (void)deleted;
	server_made = TRUE;
	return TRUE;
}
static boolean ui_widget_port_host_local(struct widget_instance *widget, struct event_record *event, boolean *deleted)
{
	return ui_widget_port_host(widget, event, deleted);
}
static boolean text_field_editing(void *row) { (void)row; return FALSE; }
static void text_field_end(boolean keep) { (void)keep; }
static void text_field_begin(void *row, char const *text, short maximum, void (*done)(char const *text))
{
	(void)row; (void)text; (void)maximum; (void)done;
}
static void text_field_begin_masked(void *row, char const *text, short maximum, void (*done)(char const *text))
{
	(void)row; (void)text; (void)maximum; (void)done;
}
static void gametype_setup_begin(void) {}
static void gametype_setup_end(void) {}
static boolean gametype_setup_apply(void) { return TRUE; }
static short server_settings_game_mode(void) { return 0; }
static short player_ui_get_active_player_profile_index(short player) { (void)player; return 0; }
static void player_ui_get_active_player_profile(short player, struct player_profile *profile)
{
	(void)player;
	wcscpy(profile->player_name, L"Host");
}
static void ui_play_audio_feedback_sound(short sound) { (void)sound; }
static void ui_widgets_close_all(void) {}
static unsigned long system_milliseconds(void) { return 1000; }

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	struct widget_instance *child;

	(void)nth;
	for (child = widget ? widget->child : NULL; child; child = child->next)
	{
		if (!strcmp(child->name, name))
			return child;
	}
	return NULL;
}

/* Create Game's state (menu_functions.c's multiplayer) */
static struct
{
	short mode;
	boolean campaign_first, game_private, cooperative_maximum_players_set;
	short maximum_players_index, cooperative_friendly_fire;
	boolean cooperative_no_player_collisions;
	wchar_t game_name[32];
	char game_password[32];
} multiplayer;

static short *server_settings_maximum_players_index(void) { return &multiplayer.maximum_players_index; }

/* the game's side of Host Controls (network_host_controls.c's) */
static boolean game_engine_running(void) { return TRUE; }
static boolean game_engine_has_teams(void) { return FALSE; }
static boolean game_engine_can_score(void) { return TRUE; }
static boolean network_standard_hosting(void) { return TRUE; }
static boolean network_host_controls_restarting(void) { return FALSE; }
static boolean network_host_rules_available(void) { return FALSE; }
static boolean network_host_controls_end_game(void) { return TRUE; }
static boolean network_host_controls_restart(void) { return TRUE; }
static boolean network_host_controls_teams_locked(void) { return FALSE; }
static void network_host_controls_lock_teams(boolean locked) { (void)locked; }
static boolean network_host_controls_opence_joins(void) { return FALSE; }
static void network_host_controls_set_opence_joins(boolean allowed) { (void)allowed; }
static short network_host_controls_maximum_step(short const *values, short count, short current, short present,
	short step)
{
	(void)values; (void)count; (void)present; (void)step;
	return current;
}

/* the host's next game, players and limits (network_host_game.c's) */
static boolean network_host_controls_in_lobby(void) { return FALSE; }
static boolean network_host_game_hosting(void) { return server_made; }
static boolean network_host_game_teams_arrangeable(void) { return FALSE; }
static short network_host_game_teams_arrange(short how) { (void)how; return NONE; }
static boolean network_host_game_next(struct server_entry *entry, boolean *now)
{
	(void)entry;
	*now = FALSE;
	return FALSE;
}
static void network_host_game_entry_shown(struct server_entry const *entry, wchar_t *text, short size)
{
	(void)entry; (void)size;
	text[0] = 0;
}
static boolean network_host_game_limits_waiting(long *score, short *minutes)
{
	*score = NONE;
	*minutes = NONE;
	return FALSE;
}
static boolean network_host_game_set_limits(long score, short minutes) { (void)score; (void)minutes; return TRUE; }
static boolean network_host_game_announce(char const *text) { (void)text; return TRUE; }

/* an admin's Host Controls (host_controls_menu.inc's, network_admin.c's):
this machine hosts, and is no dedicated server's admin */
static boolean host_remote(void) { return FALSE; }
static boolean host_flag(short flag) { (void)flag; return FALSE; }
static boolean host_lobby(void) { return network_host_controls_in_lobby(); }
static boolean host_ask(short verb, void const *player, long number, char const *text)
{
	(void)verb; (void)player; (void)number; (void)text;
	return FALSE;
}
static boolean host_next(struct server_entry *entry, boolean *now) { return network_host_game_next(entry, now); }
/* (a multiplayer game's host: no co-op level to change) */
#define HOST_GAME_MAP_SIZE 128
static boolean host_cooperative(void) { return FALSE; }
static boolean network_host_game_cooperative_next(char *level, long size, short *difficulty, boolean *now)
{
	(void)level; (void)size; (void)difficulty;
	*now = FALSE;
	return FALSE;
}
static void network_host_game_cooperative_text(char const *level, short difficulty, wchar_t *text, short size)
{
	(void)level; (void)difficulty; (void)size;
	text[0] = 0;
}

/* Host Controls' screen state (host_controls_menu.inc's host_menu) */
static struct
{
	short tab, first;
	/* (MAPS' gametype: CHANGE MAP... opens it on CURRENT) */
	short gametype;
	/* (co-op's difficulty, which it opens on too) */
	short difficulty;
	boolean focus_first;
	short game_rows[MAXIMUM_HOST_GAME_ROWS];
	short game_row_count;
	short confirm;
	unsigned long confirm_time;
	wchar_t notice[160];
	unsigned long notice_time;
} host_menu = { .confirm = NONE };

#include "under_test.inc"

/* ---------- the cases */

/* Server Setup's list: its MAX PLAYERS, LISTING and GAME spinners */
static struct widget_instance setup_list, maximum_spinner, listing_spinner, mode_spinner;

static void server_setup(void)
{
	setup_list = (struct widget_instance){ .name = "options_menu", .child = &maximum_spinner };
	maximum_spinner = (struct widget_instance){ .name = "max_players_spinner", .next = &listing_spinner };
	listing_spinner = (struct widget_instance){ .name = "listing_spinner", .next = &mode_spinner };
	mode_spinner = (struct widget_instance){ .name = "game_mode_spinner" };
	server_settings_initialize(&setup_list);
}

/* Create Game's item chosen ("join controller to mp game"): its name, cut to
31 characters as an instance's is */
static void create(char const *item)
{
	struct widget_instance widget = { .name = item };
	struct event_record event = { 0 };
	boolean deleted = FALSE;

	CHECK(multiplayer_host(&widget, &event, 0, &deleted), "Create Game's %s failed", item);
}

/* Host Controls' Game tab: whether it has LISTING, and its value */
static boolean listing_row(wchar_t *value, short size)
{
	wchar_t label[64], help[160];
	short index;

	host_game_rows_read();
	for (index = 0; index < host_menu.game_row_count; index++)
	{
		if (host_menu.game_rows[index] == _host_game_listing)
		{
			host_game_row_text(_host_game_listing, label, value, help, size);
			return TRUE;
		}
	}
	value[0] = 0;
	return FALSE;
}

/* the listing's row chosen (A: 0) or stepped (left -1, right 1) */
static void listing_changed(short step)
{
	host_game_row_change(NULL, _host_game_listing, step);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	wchar_t value[32];

	CASE("public")
	{
		/* Create Game > Internet, network.host_public on: listed */
		create("multiplayer_type_create_interne");
		CHECK(p2p.hosting_allowed && listed(), "a public internet game is not listed when made");
		/* Server Setup: PUBLIC shown, still listed; START GAME, its password */
		server_setup();
		CHECK(listing_spinner.parameters.list.selected_index == 0, "Server Setup's LISTING shows %d, not PUBLIC",
			listing_spinner.parameters.list.selected_index);
		CHECK(listed(), "Server Setup took a public game out of the browser");
		strcpy(multiplayer.game_password, "secret");
		CHECK(server_start(), "START GAME failed");
		CHECK(listed() && p2p.has_password, "START GAME: listed %d, password %d", listed(), p2p.has_password);
		/* the lobby's GAME SETTINGS, and START GAME again */
		server_setup();
		CHECK(server_start(), "START GAME failed");
		CHECK(listed() && p2p.withdrawals == 0, "the lobby's GAME SETTINGS withdrew the listing (%d times)",
			p2p.withdrawals);
		/* in the game: Host Controls has LISTING, PUBLIC */
		CHECK(listing_row(value, NUMBEROF(value)), "Host Controls has no LISTING in a public internet game");
		CHECK(!wcscmp(value, L"PUBLIC"), "Host Controls' LISTING says %ls", value);
		CHECK(listed() && p2p.withdrawals == 0, "Host Controls withdrew the listing by opening");
		return 0;
	}
	CASE("toggle")
	{
		create("multiplayer_type_create_interne");
		server_setup();
		CHECK(server_start(), "START GAME failed");
		/* Host Controls' LISTING: PRIVATE (withdrawn), then PUBLIC (listed) */
		listing_changed(0);
		CHECK(!listed() && p2p.withdrawals == 1, "LISTING to PRIVATE: listed %d, withdrawn %d times", listed(),
			p2p.withdrawals);
		CHECK(listing_row(value, NUMBEROF(value)) && !wcscmp(value, L"PRIVATE"), "LISTING says %ls", value);
		/* (Server Setup, from the lobby, keeps it private) */
		server_setup();
		CHECK(listing_spinner.parameters.list.selected_index == 1 && !listed(),
			"Server Setup made the private game public again");
		CHECK(server_start() && !p2p.has_password, "a private game has a password");
		listing_changed(1);
		CHECK(listed() && p2p.listings == 2, "LISTING to PUBLIC: listed %d, %d listings", listed(), p2p.listings);
		CHECK(listing_row(value, NUMBEROF(value)) && !wcscmp(value, L"PUBLIC"), "LISTING says %ls", value);
		server_setup();
		CHECK(listing_spinner.parameters.list.selected_index == 0 && listed(),
			"Server Setup took the public game out again");
		/* and stepped the other way */
		listing_changed(-1);
		CHECK(!listed() && p2p.withdrawals == 2, "LISTING stepped to PRIVATE: listed %d", listed());
		return 0;
	}
	CASE("private")
	{
		/* Create Game > Internet, network.host_public off: never listed,
		till Host Controls' LISTING makes it public */
		host_public = FALSE;
		create("multiplayer_type_create_interne");
		server_setup();
		CHECK(listing_spinner.parameters.list.selected_index == 1, "Server Setup's LISTING is not PRIVATE");
		CHECK(server_start(), "START GAME failed");
		server_setup();
		CHECK(!listed() && p2p.listings == 0, "a private game was listed (%d times)", p2p.listings);
		CHECK(listing_row(value, NUMBEROF(value)) && !wcscmp(value, L"PRIVATE"), "LISTING says %ls", value);
		listing_changed(0);
		CHECK(listed(), "LISTING to PUBLIC did not list a private game");
		return 0;
	}
	CASE("coop")
	{
		/* co-op hosted for FRIENDS (WHO CAN JOIN's default): not listed for
		strangers, its friends offered JOIN */
		cooperative = TRUE;
		create("multiplayer_type_online_coop_item");
		server_setup();
		CHECK(coop_who_kept() == _coop_join_friends, "WHO CAN JOIN's default is %s", coop_join);
		CHECK(listing_spinner.parameters.list.selected_index == 1 && !listed() && !p2p.closed,
			"FRIENDS: LISTING %d, listed %d, friends offered no JOIN %d", listing_spinner.parameters.list.selected_index,
			listed(), p2p.closed);
		CHECK(server_start() && coop_who_now() == _coop_join_friends, "START GAME, or FRIENDS is not who can join");
		/* ANYONE (co-op's HOST's card, as coop_host_start keeps and applies
		it): listed, and Server Setup keeps it so */
		config_write("network.coop_join", "anyone");
		coop_who_apply(coop_who_kept());
		CHECK(listed() && !p2p.closed, "ANYONE is not listed");
		server_setup();
		CHECK(listing_spinner.parameters.list.selected_index == 0 && listed() && coop_who_now() == _coop_join_anyone,
			"Server Setup took a co-op game for ANYONE out of the list");
		/* INVITE ONLY: not listed, and friends offered no JOIN */
		config_write("network.coop_join", "invite");
		server_setup();
		CHECK(!listed() && p2p.closed && coop_who_now() == _coop_join_invite, "INVITE ONLY: listed %d, closed %d",
			listed(), p2p.closed);
		/* Server Setup's LISTING: PUBLIC is ANYONE (friends offered JOIN
		again), PRIVATE FRIENDS */
		server_settings_private_set(FALSE);
		CHECK(!strcmp(coop_join, "anyone") && !p2p.closed, "LISTING to PUBLIC: %s, closed %d", coop_join, p2p.closed);
		server_settings_private_set(TRUE);
		CHECK(!strcmp(coop_join, "friends"), "LISTING to PRIVATE: %s", coop_join);
		return 0;
	}
	CASE("lan")
	{
		/* Create Game > Local Network: no listing, no LISTING row */
		create("multiplayer_type_create_lan_item");
		server_setup();
		CHECK(server_start(), "START GAME failed");
		CHECK(!listed() && !p2p.public && p2p.listings == 0, "a local network game was listed");
		CHECK(!listing_row(value, NUMBEROF(value)), "Host Controls has LISTING in a local network game");
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 1;
}
