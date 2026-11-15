/*
COOP_MENU.INC

Co-op's screens under the new menu engine (docs/menu_screens.md, "Co-op"):
one place every way in lands on (screens/coop.xml: Campaign's CO-OP, the
server browser's CO-OP tab, Multiplayer's CO-OP CAMPAIGN), with HOST, JOIN
and SPLIT SCREEN. Included in menu_functions.c after campaign_menu.inc,
whose Choose Mission and Choose Difficulty HOST uses, and friends_menu.inc,
whose helpers it uses.

- HOST: Choose Mission and Choose Difficulty as the campaign has them (no
  saved game offered: campaign_levels_read), then WHO CAN JOIN
  (screens/coop_who.xml): FRIENDS (its friends see it in their co-op list
  and join; it is not listed for strangers), ANYONE (listed in everyone's
  co-op list and server browser too) or INVITE ONLY (only its invite link
  joins: its presence offers its friends no JOIN,
  p2p_friends_set_game_closed). The choice is kept (network.coop_join), and
  the game is hosted at once, as Server Setup's START would host it, into
  the lobby. The lobby says who can see it and how to invite, and anything
  that keeps people from finding it (internet play off, the brokers not
  reached, the game list off, appearing offline), in plain words, with a
  button that fixes it (coop.* bindings, "lobby fix").
- JOIN (screens/coop_join.xml): the server browser's model (its listings,
  pings, joining, password screen and connecting box) with only the co-op
  games, none hidden by its filters, searches or tab: the player's friends'
  first (each a friend online whose presence says they host a co-op game,
  "Co-op Heroic on The Silent Cartographer", lobby_menu.inc's
  lobby_presence_tell; joined by the game's own listing where it has one,
  else through their party, which follows them in), then the public ones.
  An empty list says why, with its fix.
- SPLIT SCREEN: as the hub's SPLIT-SCREEN CO-OP ("port coop begin").
*/

#define COOP_WHO_NAME "pc\\screens\\coop_who"
#define COOP_FRIENDS_MAXIMUM 16
/* milliseconds: the friends read again; what was just done shown; the
brokers not reached said only this long after hosting */
#define COOP_FRIENDS_INTERVAL 500
#define COOP_DONE_TIME 6000
#define COOP_BROKERS_GRACE 8000
#define COOP_LINK_SHOWN 30

void xnet_internet_play_start(void);

/* WHO CAN JOIN's choices (network.coop_join's words) */
enum
{
	_coop_join_friends,
	_coop_join_anyone,
	_coop_join_invite,
	NUMBER_OF_COOP_JOINS
};

static char const *const coop_join_words[NUMBER_OF_COOP_JOINS] = { "friends", "anyone", "invite" };

/* what a fix button does: the co-op list's when empty, the lobby's */
enum
{
	_coop_fix_none,
	_coop_fix_online,
	_coop_fix_public_lobby,
	_coop_fix_retry,
	_coop_fix_share,
	_coop_fix_host,
	NUMBER_OF_COOP_FIXES
};

static wchar_t const *const coop_fix_labels[NUMBER_OF_COOP_FIXES] =
{
	L"", L"TURN ON INTERNET PLAY", L"SHOW PUBLIC GAMES", L"RETRY", L"SHOW ME ONLINE", L"HOST CO-OP",
};

/* (the co-op list's, under its words for none: as short as its button,
the words over it saying what) */
static wchar_t const *const coop_fix_short_labels[NUMBER_OF_COOP_FIXES] =
{
	L"", L"TURN IT ON", L"SHOW THEM", L"RETRY", L"SHOW ME ONLINE", L"HOST CO-OP",
};

static struct
{
	/* HOST's Choose Mission and Choose Difficulty (their PLAY: WHO CAN
	JOIN next), the map and difficulty chosen */
	boolean active;
	char map[256];
	short difficulty;
	/* when the game was hosted */
	unsigned long hosted_time;
} coop_host;

static struct
{
	boolean on;
	/* the friends online hosting a co-op game they offer JOIN to, as last
	read, and each one's mission */
	struct p2p_friend friends[COOP_FRIENDS_MAXIMUM];
	wchar_t missions[COOP_FRIENDS_MAXIMUM][ROW_TEXT_LENGTH];
	char difficulties[COOP_FRIENDS_MAXIMUM][16];
	short friend_count;
	unsigned long friends_time;
	/* each row listed: its friend (index + 1; 0, a public game's), and
	whether it is joined through their party (a game not listed) */
	unsigned char friend_of[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
	boolean party_of[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
	short friend_games, public_games;
	/* what was just done (a friend's game joined) */
	wchar_t done[96];
	unsigned long done_time;
} coop_join;

/* WHO CAN JOIN as kept (network.coop_join) */
static short coop_who_kept(void)
{
	char const *word = config_string("network.coop_join");
	short index;

	for (index = 0; word && index < NUMBER_OF_COOP_JOINS; index++)
	{
		if (!strcmp(word, coop_join_words[index]))
			return index;
	}
	return _coop_join_friends;
}

/* (menu_screen.c: the WHO CAN JOIN card focused as it opens) */
char const *pc_menu_coop_who_word(void)
{
	return coop_join_words[coop_who_kept()];
}

static boolean coop_host_choosing(void)
{
	return coop_host.active;
}

/* who can join the game hosted: as it is now (Server Setup's LISTING may
have changed it in the lobby) */
static short coop_who_now(void)
{
	if (p2p_friends_game_closed())
		return _coop_join_invite;
	return server_settings_private() ? _coop_join_friends : _coop_join_anyone;
}

static void coop_who_apply(short who)
{
	p2p_set_hosting_public(who == _coop_join_anyone);
	p2p_friends_set_game_closed(who == _coop_join_invite);
}

/* ---------- HOST */

/* "port coop host begin" (HOST, before Choose Mission), "port coop host
end" (co-op's screen shown again, or SOLO) */
static boolean coop_host_begin(boolean begin)
{
	coop_host.active = begin;
	coop_host.map[0] = 0;
	return TRUE;
}

/* Choose Difficulty's PLAY while HOST chooses (in place of "port campaign
play"): the difficulty kept as the campaign keeps it, then WHO CAN JOIN */
static boolean coop_host_difficulty(struct widget_instance *widget, boolean *widget_deleted)
{
	short difficulty = campaign_cards.difficulty;
	char value[8];

	if (!strncmp(widget->name, "difficulty_", 11) && widget->name[11] >= '0' && widget->name[11] <= '3' &&
		!widget->name[12])
	{
		difficulty = (short)(widget->name[11] - '0');
	}
	difficulty = (short)PIN(difficulty, 0, NUMBER_OF_DIFFICULTIES - 1);
	snprintf(value, sizeof(value), "%d", difficulty);
	if (!config_write("game.campaign_difficulty", value))
		platform_log("menus: could not keep the campaign's difficulty");
	snprintf(coop_host.map, sizeof(coop_host.map), "%s", main_get_map_name());
	coop_host.difficulty = difficulty;
	if (!coop_host.map[0])
		return campaign_fail();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return ui_widget_port_open(widget, COOP_WHO_NAME, widget_deleted);
}

/* WHO CAN JOIN's card: the choice kept, and the game hosted for the
internet as Create Game's ONLINE CO-OP and Server Setup's START host it
(multiplayer_host, map_list_choose, server_start), into the lobby */
static boolean coop_host_start(struct widget_instance *widget, struct event_record *event, short controller,
	short who, boolean *widget_deleted)
{
	if (!coop_host.map[0])
		return campaign_fail();
	if (strcmp(config_string("network.coop_join"), coop_join_words[who]) &&
		!config_write("network.coop_join", coop_join_words[who]))
	{
		platform_log("menus: could not write network.coop_join to config.toml");
	}
	multiplayer.mode = _multiplayer_mode_host_internet;
	multiplayer.campaign_first = FALSE;
	if (!multiplayer_player(controller))
		return FALSE;
	p2p_set_hosting_allowed(TRUE);
	multiplayer.cooperative_maximum_players_set = FALSE;
	if (!ui_widget_port_host(widget, event, widget_deleted) || *widget_deleted)
		return campaign_fail();
	if (!ui_widget_port_cooperative_level_choose(coop_host.map, coop_host.difficulty))
		return campaign_fail();
	coop_who_apply(who);
	if (!multiplayer.game_name[0] && player_ui_get_active_player_profile_index(0) != NONE)
	{
		struct player_profile profile;

		player_ui_get_active_player_profile(0, &profile);
		ustrncpy(multiplayer.game_name, profile.player_name, NUMBEROF(multiplayer.game_name) - 1);
	}
	multiplayer.game_password[0] = 0;
	if (!server_start())
		return campaign_fail();
	platform_log("menus: co-op hosted: %s on %s, %s can join", coop_host.map,
		coop_host.difficulty == 3 ? "legendary" : coop_host.difficulty == 2 ? "heroic" :
		coop_host.difficulty == 1 ? "normal" : "easy", coop_join_words[who]);
	/* (HOST's choosing goes on: the lobby left comes back to WHO CAN JOIN,
	and Choose Difficulty's PLAY is still HOST's; co-op's screen ends it) */
	coop_host.hosted_time = system_milliseconds() | 1;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return ui_widget_port_open(widget, LOBBY_NAME, widget_deleted);
}

/* ---------- JOIN: the co-op list */

static boolean coop_join_listing(void)
{
	return coop_join.on && multiplayer.mode == _multiplayer_mode_server_browser;
}

static void coop_join_end(void)
{
	coop_join.on = FALSE;
}

/* "port coop join init" (the list's screen made): the server browser's
model, on the co-op list; the games' client */
static boolean coop_join_initialize(struct widget_instance *screen, struct event_record *event,
	boolean *widget_deleted)
{
	multiplayer.mode = _multiplayer_mode_server_browser;
	coop_join.on = TRUE;
	coop_join.friends_time = 0;
	coop_join.friend_count = 0;
	lobby_browser_begin();
	return ui_widget_port_browse(screen, event, widget_deleted);
}

/* "port coop join dispose" */
static void coop_join_dispose(void)
{
	lobby_browser_end();
	coop_join.on = FALSE;
}

/* a friend's game, as their presence says it: a co-op game they host and
offer JOIN to ("Co-op Heroic on The Silent Cartographer": lobby_menu.inc's
lobby_presence_tell; an older build's "OpenH1 30 Tick \xB7 Co-op on ..."),
its mission and difficulty (empty: not said) */
static boolean coop_friend_game(struct p2p_friend const *entry, wchar_t *mission, short mission_size,
	char *difficulty, int difficulty_size)
{
	char const *coop, *on;

	difficulty[0] = 0;
	if (entry->kind != _p2p_friend_online || !entry->hosting || !entry->joinable)
		return FALSE;
	coop = strstr(entry->game, "Co-op");
	if (!coop || (coop != entry->game && coop[-1] != ' '))
		return FALSE;
	coop += 5;
	on = strstr(coop, " on ");
	if (!on)
		return FALSE;
	if (*coop == ' ' && on > coop + 1)
		snprintf(difficulty, (size_t)difficulty_size, "%.*s", (int)(on - coop - 1), coop + 1);
	friends_wide_utf8(on + 4, mission, mission_size);
	return TRUE;
}

/* the friends hosting co-op, read again a while apart */
static void coop_join_friends_read(void)
{
	static struct p2p_friend entries[P2P_FRIENDS_LIST_MAXIMUM];
	unsigned long now = system_milliseconds();
	short count, index;

	if (coop_join.friends_time && now - coop_join.friends_time < COOP_FRIENDS_INTERVAL)
		return;
	coop_join.friends_time = now | 1;
	coop_join.friend_count = 0;
	count = config_boolean("network.online") ? (short)p2p_friends_list(entries, P2P_FRIENDS_LIST_MAXIMUM) : 0;
	for (index = 0; index < count && coop_join.friend_count < COOP_FRIENDS_MAXIMUM; index++)
	{
		short made = coop_join.friend_count;

		if (!coop_friend_game(&entries[index], coop_join.missions[made], ROW_TEXT_LENGTH,
			coop_join.difficulties[made], (int)sizeof(coop_join.difficulties[made])))
		{
			continue;
		}
		coop_join.friends[made] = entries[index];
		coop_join.friend_count++;
	}
}

/* a group's heading put in the list */
static short coop_join_heading(struct p2p_listing *ordered, short listed, short group)
{
	csmemset(&ordered[listed], 0, sizeof(ordered[listed]));
	ordered[listed].ping = NONE;
	lobby_browser.headed[listed] = (unsigned char)(group + 1);
	coop_join.friend_of[listed] = 0;
	coop_join.party_of[listed] = FALSE;
	lobby_browser.headings++;
	return (short)(listed + 1);
}

/* the list: the friends' co-op games (each its own listing where it has
one: a public game of their name), then the other public co-op games, in
the order found (the most players first); returns how many rows */
static short coop_join_order(short count)
{
	static struct p2p_listing ordered[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
	static boolean used[LOBBY_BROWSER_GAMES];
	short listed = 0, index, friend_index;

	coop_join_friends_read();
	csmemset(used, 0, sizeof(used));
	lobby_browser.headings = 0;
	coop_join.friend_games = coop_join.public_games = 0;
	for (friend_index = 0; friend_index < coop_join.friend_count && listed < LOBBY_BROWSER_GAMES; friend_index++)
	{
		struct p2p_friend const *entry = &coop_join.friends[friend_index];
		short found = NONE;

		if (!coop_join.friend_games)
			listed = coop_join_heading(ordered, listed, _browser_group_friends);
		for (index = 0; index < count && found == NONE; index++)
		{
			if (!used[index] && !lobby_browser.games[index].engine_type &&
				!_stricmp(lobby_browser.games[index].name, entry->name))
			{
				found = index;
			}
		}
		if (found != NONE)
		{
			used[found] = TRUE;
			ordered[listed] = lobby_browser.games[found];
		}
		else
		{
			/* (a game of their party's: its words their presence's) */
			struct p2p_listing *game = &ordered[listed];

			csmemset(game, 0, sizeof(*game));
			snprintf(game->name, sizeof(game->name), "%s", entry->name);
			snprintf(game->gametype, sizeof(game->gametype), coop_join.difficulties[friend_index][0] ? "Co-op %s" :
				"Co-op", coop_join.difficulties[friend_index]);
			game->player_count = entry->players;
			game->maximum_player_count = entry->maximum_players;
			game->open = TRUE;
			game->standard = TRUE;
			game->ping = NONE;
		}
		lobby_browser.headed[listed] = 0;
		coop_join.friend_of[listed] = (unsigned char)(friend_index + 1);
		coop_join.party_of[listed] = found == NONE;
		listed++;
		coop_join.friend_games++;
	}
	for (index = 0; index < count && listed < LOBBY_BROWSER_GAMES; index++)
	{
		if (used[index] || lobby_browser.games[index].engine_type)
			continue;
		if (!coop_join.public_games)
			listed = coop_join_heading(ordered, listed, _browser_group_public);
		ordered[listed] = lobby_browser.games[index];
		lobby_browser.headed[listed] = 0;
		coop_join.friend_of[listed] = 0;
		coop_join.party_of[listed] = FALSE;
		listed++;
		coop_join.public_games++;
	}
	csmemcpy(lobby_browser.games, ordered, listed * sizeof(ordered[0]));
	browser_offline_count = 0;
	browser_filtered_count = browser_searched_count = 0;
	for (index = 0; index < listed; index++)
		browser_listed_favorite[index] = FALSE;
	return listed;
}

/* the row's friend (NULL: a public game's, or none) */
static struct p2p_friend const *coop_join_friend_of(short index)
{
	if (!coop_join_listing() || index < 0 || index >= lobby_browser.count || lobby_browser_heading(index) ||
		!coop_join.friend_of[index] || coop_join.friend_of[index] > coop_join.friend_count)
	{
		return NULL;
	}
	return &coop_join.friends[coop_join.friend_of[index] - 1];
}

static boolean coop_join_party_row(short index)
{
	return coop_join_friend_of(index) && coop_join.party_of[index];
}

/* a friend's game's row as its presence says it: its mission, its ping
not timed (its host is reached through their party) */
static void coop_join_row_fix(short index, struct browser_row *row)
{
	if (!coop_join_party_row(index))
		return;
	settings_wide_copy(row->map, coop_join.missions[coop_join.friend_of[index] - 1], ROW_TEXT_LENGTH);
	row->ping_state = _browser_ping_locked;
	row->rate = 0;
}

static boolean coop_join_said(void)
{
	return coop_join.done_time && system_milliseconds() - coop_join.done_time < COOP_DONE_TIME;
}

/* the panel's line for a friend's game joined through their party */
static void coop_join_status(short index, wchar_t *text, short size)
{
	struct p2p_friend const *entry = coop_join_friend_of(index);
	wchar_t name[P2P_FRIEND_NAME_SIZE];

	if (coop_join_said())
	{
		ustrncpy(text, coop_join.done, size - 1);
		text[size - 1] = 0;
		return;
	}
	friends_wide(entry && entry->name[0] ? entry->name : "Your friend", name, NUMBEROF(name));
	usnprintf(text, size - 1, L"%s is hosting. JOIN takes you into their game.", name);
	text[size - 1] = 0;
}

/* a friend's game joined through their party (p2p_friends_join: the Party
screen then opens over the list, and follows them into their game) */
static boolean coop_join_friend(short index)
{
	struct p2p_friend const *entry = coop_join_friend_of(index);
	wchar_t name[P2P_FRIEND_NAME_SIZE];

	if (!entry || !config_boolean("network.online"))
		return campaign_fail();
	friends_wide(entry->name[0] ? entry->name : "Your friend", name, NUMBEROF(name));
	if (!p2p_friends_join(entry->code))
	{
		usnprintf(coop_join.done, NUMBEROF(coop_join.done) - 1, L"Couldn't reach %s. Try again in a moment.", name);
		coop_join.done_time = system_milliseconds() | 1;
		platform_log("menus: co-op list: could not join %s's party", entry->name);
		return campaign_fail();
	}
	usnprintf(coop_join.done, NUMBEROF(coop_join.done) - 1, L"Joining %s...", name);
	coop_join.done[NUMBEROF(coop_join.done) - 1] = 0;
	coop_join.done_time = system_milliseconds() | 1;
	platform_log("menus: co-op list: joining %s's game through their party", entry->name);
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* why the list has no game, in plain words, and what fixes it */
static short coop_join_problem(wchar_t *text, short size)
{
	short fix = _coop_fix_none;

	if (!config_boolean("network.online"))
	{
		usnprintf(text, size - 1, L"Internet play is off, so no co-op games can be found.\r\n"
			L"Turn it on to see your friends' games and everyone's.");
		fix = _coop_fix_online;
	}
	else if (system_milliseconds() - lobby_browser.begin_time < LOBBY_BROWSER_LOOK_TIME)
		usnprintf(text, size - 1, L"Looking for co-op games...");
	else if (!p2p_lobby_brokers_reached())
	{
		usnprintf(text, size - 1, L"Can't reach the game list.\r\nCheck your internet connection, then retry.");
		fix = _coop_fix_retry;
	}
	else if (!config_boolean("network.public_lobby"))
	{
		usnprintf(text, size - 1, L"No friends are hosting co-op right now,\r\nand public games are hidden.");
		fix = _coop_fix_public_lobby;
	}
	else
	{
		usnprintf(text, size - 1, L"No friends are hosting co-op right now,\r\nand there are no public co-op games.\r\n"
			L"Host one, and your friends will see it here.");
		fix = _coop_fix_host;
	}
	text[size - 1] = 0;
	return fix;
}

static void coop_join_empty(wchar_t *text, short size)
{
	coop_join_problem(text, size);
}

/* the list's bindings of its own: its counts, the chosen game's mission and
difficulty, an empty list's fix (FALSE: none) */
static boolean coop_join_value(char const *name, struct browser_row const *row, wchar_t *text, short size)
{
	text[0] = 0;
	if (!strcmp(name, "browser.counts"))
	{
		usnprintf(text, size - 1, coop_join.friend_games == 1 ? L"%d FRIEND'S GAME    %d PUBLIC" :
			L"%d FRIENDS' GAMES    %d PUBLIC", coop_join.friend_games, coop_join.public_games);
	}
	else if (!strcmp(name, "browser.panel.where") && row)
	{
		wchar_t const *difficulty = browser_coop_difficulty(row);

		if (difficulty[0])
			usnprintf(text, size - 1, L"%s \x00B7 %s", row->map, difficulty);
		else
			ustrncpy(text, row->map, size - 1);
	}
	else if (!strcmp(name, "browser.fix") && !browser_display.rows)
	{
		wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];

		ustrncpy(text, coop_fix_short_labels[coop_join_problem(words, NUMBEROF(words))], size - 1);
	}
	text[size - 1] = 0;
	return text[0] != 0;
}

/* ---------- the lobby: who can see the game, and what keeps them from it */

/* this machine hosts a network co-op game (not split screen's) */
static boolean coop_lobby_hosting(void)
{
	return global_network_game_server_get() && !network_game_is_splitscreen_local() && hosting_cooperative();
}

/* what keeps people from finding the game hosted, in plain words, and
what fixes it */
static short coop_lobby_problem(wchar_t *text, short size)
{
	struct p2p_friends_state state;
	short who = coop_who_now();
	short fix = _coop_fix_none;

	text[0] = 0;
	p2p_friends_state(&state);
	if (!config_boolean("network.online"))
	{
		usnprintf(text, size - 1, L"Internet play is off, so only players on your network can join.");
		fix = _coop_fix_online;
	}
	else if (coop_host.hosted_time && system_milliseconds() - coop_host.hosted_time > COOP_BROKERS_GRACE &&
		!p2p_lobby_brokers_reached())
	{
		usnprintf(text, size - 1, L"Can't reach the game list, so nobody can find your game. Check your internet.");
		fix = _coop_fix_retry;
	}
	else if (who == _coop_join_anyone && !config_boolean("network.public_lobby"))
	{
		usnprintf(text, size - 1, L"Your game isn't listed: public games are turned off.");
		fix = _coop_fix_public_lobby;
	}
	else if (who == _coop_join_friends && !config_boolean("friends.share_status"))
	{
		usnprintf(text, size - 1, L"You appear offline, so your friends can't see your game.");
		fix = _coop_fix_share;
	}
	else if (who == _coop_join_friends && state.identity && !state.friends)
		usnprintf(text, size - 1, L"You have no friends added yet. Copy your invite link and send it.");
	text[size - 1] = 0;
	return fix;
}

/* a "coop." binding's value, UTF-8 (menu_screen.c's): the lobby's WHO CAN
JOIN, its invite link, what keeps people from the game and its fix */
int pc_menu_coop_value(char const *name, char *value, unsigned long size)
{
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	short who;

	value[0] = 0;
	text[0] = 0;
	if (!coop_lobby_hosting())
		return 0;
	who = coop_who_now();
	if (!strcmp(name, "coop.listing"))
	{
		struct p2p_friends_state state;

		if (!config_boolean("network.online"))
			usnprintf(text, NUMBEROF(text) - 1, L"NOT LISTED: internet play is off.");
		else if (who == _coop_join_invite)
			usnprintf(text, NUMBEROF(text) - 1, L"INVITE ONLY: only people with your link can join.");
		else if (who == _coop_join_anyone)
		{
			usnprintf(text, NUMBEROF(text) - 1, p2p_hosting_listed() ?
				L"LISTED: anyone can find it in Co-op, JOIN." :
				L"ANYONE CAN JOIN: listing your game...");
		}
		else
		{
			p2p_friends_state(&state);
			usnprintf(text, NUMBEROF(text) - 1, state.online == 1 ? L"FRIENDS CAN JOIN: 1 friend online sees it." :
				state.online ? L"FRIENDS CAN JOIN: %d friends online see it." :
				L"FRIENDS CAN JOIN: no friend is online yet.", state.online);
		}
	}
	else if (!strcmp(name, "coop.link"))
	{
		char link[256];

		if (!config_boolean("network.online") || p2p_invite_link(link, sizeof(link)) <= 0 || !link[0])
			return 0;
		if (strlen(link) > COOP_LINK_SHOWN)
			snprintf(link + COOP_LINK_SHOWN, sizeof(link) - COOP_LINK_SHOWN, "...");
		snprintf(value, size, "Invite link: %s", link);
		return value[0] != 0;
	}
	else if (!strcmp(name, "coop.problem"))
		coop_lobby_problem(text, NUMBEROF(text));
	else if (!strcmp(name, "coop.fix"))
	{
		wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];

		ustrncpy(text, coop_fix_labels[coop_lobby_problem(words, NUMBEROF(words))], NUMBEROF(text) - 1);
	}
	else
		return 0;
	text[NUMBEROF(text) - 1] = 0;
	if (text[0])
		text_utf8(text, value, (long)size);
	return value[0] != 0;
}

/* a fix: internet play turned on (started now: xnet.c, the game's
sockets known to it), the server browser's game list turned on, the brokers
asked again, the profile shown online to its friends */
static boolean coop_fix(short fix)
{
	switch (fix)
	{
	case _coop_fix_online:
		if (!config_write("network.online", "true"))
			return campaign_fail();
		xnet_internet_play_start();
		p2p_set_hosting_allowed(TRUE);
		if (coop_lobby_hosting())
			coop_who_apply(coop_who_now());
		platform_log("menus: co-op: internet play turned on");
		break;
	case _coop_fix_public_lobby:
		if (!config_write("network.public_lobby", "true"))
			return campaign_fail();
		platform_log("menus: co-op: public games turned on");
		break;
	case _coop_fix_share:
		if (!config_write("friends.share_status", "true"))
			return campaign_fail();
		platform_log("menus: co-op: shown online to friends again");
		break;
	case _coop_fix_retry:
		p2p_lobby_refresh();
		break;
	default:
		return FALSE;
	}
	if (coop_join_listing())
		lobby_browser_begin();
	if (coop_host.hosted_time)
		coop_host.hosted_time = system_milliseconds() | 1;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* (menu_screen.c's "coop fix": whether the empty list's fix is to host) */
int pc_menu_coop_fix_hosts(void)
{
	wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];

	return coop_join_listing() && coop_join_problem(words, NUMBEROF(words)) == _coop_fix_host;
}

/* JOIN BY LINK: Join Game's, as Multiplayer's JOIN BY LINK opens it (an
invite link copied joined at once) */
#define COOP_BROWSER_NAME "pc\\main_menu\\multiplayer_type_select\\join_game\\join_game_screen"

/* the co-op screens' actions of C's own (menu_screen.c's): WHO CAN JOIN's
cards, the co-op list's and the lobby's fix, JOIN BY LINK */
int pc_menu_coop_action(struct widget_instance *widget, struct event_record *event, char const *name, int *deleted)
{
	short controller = event_controller(widget, event);
	boolean widget_deleted = FALSE;
	int result = FALSE;
	short who;

	*deleted = FALSE;
	for (who = 0; who < NUMBER_OF_COOP_JOINS; who++)
	{
		char action[32];

		snprintf(action, sizeof(action), "coop host %s", coop_join_words[who]);
		if (!strcmp(name, action))
		{
			result = coop_host_start(widget, event, controller, who, &widget_deleted);
			*deleted = widget_deleted;
			return result;
		}
	}
	if (!strcmp(name, "coop fix"))
	{
		wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];

		return coop_fix(coop_join_problem(words, NUMBEROF(words)));
	}
	if (!strcmp(name, "lobby fix"))
	{
		wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];

		return coop_lobby_hosting() && coop_fix(coop_lobby_problem(words, NUMBEROF(words)));
	}
	if (!strcmp(name, "coop join link"))
	{
		multiplayer.mode = _multiplayer_mode_direct_link;
		coop_join.on = FALSE;
		pc_menu_browser_visit(NULL);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		result = ui_widget_port_open(widget, COOP_BROWSER_NAME, &widget_deleted);
		*deleted = widget_deleted;
		return result;
	}
	return FALSE;
}

/* (menu_screen.c: co-op's screen opened, made again as Choose Mission's B
comes back to it: HOST's choosing over) */
void pc_menu_coop_opened(void)
{
	coop_host.active = FALSE;
}
