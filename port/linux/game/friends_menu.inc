/*
FRIENDS_MENU.INC

Friends' menus (port/linux/src/p2p_friends.c's friends): Multiplayer's
FRIENDS (its note and panel: friends_hub_note, friends_hub_status), the
Friends screen (tools/port_settings.py's _friends and _touch_friends, in
Multiplayer's look) and its ADD BY CODE screen (_friends_add). Included in
menu_functions.c, whose widgets' helpers it uses.

Each frame (friends_update, from main.c) friends are told whose they are:
player 1's profile (its folder, where friends.txt is, and its name), or
debug.friends_folder's, a test's.

The Friends screen's rows: this profile's code first (COPY CODE, ADD BY
CODE, APPEAR OFFLINE or ONLINE: friends.share_status), then the list in
p2p_friends.c's order: the friends online (JOIN when their presence offers
their party, or their invite waits; INVITE), the requests (ACCEPT, IGNORE),
the friends offline, the requests sent (CANCEL), the recent players (ADD
FRIEND, for one with a code), the codes blocked (UNBLOCK); a friend's REMOVE
(which ends its invites too), a request's or a player's BLOCK, each pressed
twice. The rows
scroll at their ends, as the server browser's. JOIN joins the friend's party
(the Party screen then opens over this one, and follows its leader into its
games as any party does).

debug.friends takes the screen's actions at seconds after the start, for a
test's copies of the game, which have no player to press them. The rows' and
the panel's words (friends_row_note, friends_panel_text) and which prompts
a row has (friends_prompts_of) are tools/harness/tests/test_friends_menu.py's.
*/

#define FRIENDS_ADD_NAME "pc\\main_menu\\multiplayer_type_select\\friends\\add\\add_screen"
/* the computer's rows (port_settings.py's FRIENDS_ROWS; the touchscreen
has fewer: counted) */
#define FRIENDS_ROWS 10
#define FRIENDS_UPDATE_INTERVAL 100
/* milliseconds: what was just done shown in the panel; a REMOVE or BLOCK
waits for its second press */
#define FRIENDS_DONE_TIME 6000
#define FRIENDS_CONFIRM_TIME 4000
/* milliseconds: the friends list not reached (no signalling broker) in this
long after the screen opens is said to be out of reach, and why to check */
#define FRIENDS_CONNECT_TIME 10000
#define FRIENDS_CODE_TEXT 20

/* the prompts a row offers (a bit each) */
enum
{
	_friends_prompt_copy,
	_friends_prompt_join,
	_friends_prompt_accept,
	_friends_prompt_add,
	_friends_prompt_unblock,
	_friends_prompt_code,
	_friends_prompt_invite,
	_friends_prompt_ignore,
	_friends_prompt_cancel,
	_friends_prompt_remove,
	_friends_prompt_block,
	_friends_prompt_hide,
	_friends_prompt_show,
	NUMBER_OF_FRIENDS_PROMPTS
};

/* the prompts' widgets, by _friends_prompt_* (port_settings.py's
FRIENDS_PROMPTS) */
static char const *const friends_prompt_names[NUMBER_OF_FRIENDS_PROMPTS] = {
	"prompt_copy", "prompt_join", "prompt_accept", "prompt_add", "prompt_unblock", "prompt_code", "prompt_invite",
	"prompt_ignore", "prompt_cancel", "prompt_remove", "prompt_block", "prompt_hide",
	"prompt_show",
};

/* the ADD BY CODE screen's help (port_settings.py's _friends_add) */
enum
{
	_friends_add_help_ask = 1,
	_friends_add_help_typo,
	_friends_add_help_not_code,
	_friends_add_help_own,
	_friends_add_help_already,
	_friends_add_help_full,
	_friends_add_help_no_profile,
};

static struct
{
	unsigned long update_time;
	/* when the screen opened (still not connected FRIENDS_CONNECT_TIME
	after, it says so) */
	unsigned long open_time;
	/* the list as last read (the rows' entries: the row 0 is this
	profile's code, row n the list's entry n - 1) */
	struct p2p_friend entries[P2P_FRIENDS_LIST_MAXIMUM];
	short count;
	/* the entry on the first row, and the one chosen (rows' terms), and
	the chosen one's code (kept chosen as the list moves) */
	short first;
	short chosen;
	byte chosen_code[P2P_FRIEND_CODE_BYTES];
	short controller;
	/* what was just done, shown in the panel a while */
	wchar_t done[96];
	unsigned long done_time;
	/* a REMOVE or BLOCK pressed once: which, of what, when */
	short confirm;
	byte confirm_code[P2P_FRIEND_CODE_BYTES];
	unsigned long confirm_time;
	/* the prompts last focused for (the row's kind) */
	short prompts_kind;
	/* ADD BY CODE: the code typed, the help */
	char add_text[FRIENDS_CODE_TEXT + 1];
	short add_help;
	/* debug.friends: read, its next action, the start; a row to choose
(its "show": the row + 1, 0 none) */
	boolean debug_read;
	short debug_next;
	unsigned long debug_start;
	short debug_show;
} friends_menu;

/* the platform layer's (platform.h) */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* ---------- whose friends */

/* player 1's profile (the active one, else the one last used): its folder
on this machine and its name; FALSE if there is none */
static boolean friends_profile(char *folder, short folder_size, char *name, short name_size)
{
	struct player_profile profile;
	long profile_index = player_ui_get_active_player_profile_index(0);
	char xbox_path[512];
	short index;

	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	if (profile_index == NONE || !player_profile_get(profile_index, &profile) ||
		!player_profile_get_enclosing_directory_path(profile_index, xbox_path))
	{
		return FALSE;
	}
	for (index = 0; index < name_size - 1 && index < NUMBEROF(profile.player_name) && profile.player_name[index];
		index++)
	{
		wchar_t character = profile.player_name[index];

		name[index] = (char)(character >= 0x20 && character < 0x7F ? character : '?');
	}
	name[index] = 0;
	platform_translate_path(xbox_path, folder, (unsigned long)folder_size);
	return folder[0] != 0;
}

/* ---------- debug.friends */

/* a code of a test's: its text, or "@" and a friends.txt's path (the code
it says it has) */
static boolean friends_debug_code(char const *text, byte *code)
{
	if (*text == '@')
		return p2p_friends_code_in_file(text + 1, code) != 0;
	return p2p_friend_code_parse(text, code) > 0;
}

/* the list logged (a test's: debug.friends' "list") */
static void friends_debug_list(void)
{
	static struct p2p_friend entries[P2P_FRIENDS_LIST_MAXIMUM];
	struct p2p_friends_state state;
	short count = (short)p2p_friends_list(entries, P2P_FRIENDS_LIST_MAXIMUM);
	short index;

	p2p_friends_state(&state);
	platform_log("friends: %s, %d friends (%d online), %d requests%s", state.code, state.friends, state.online,
		state.requests, state.sharing ? "" : ", appearing offline");
	for (index = 0; index < count; index++)
	{
		char code[P2P_FRIEND_CODE_TEXT_SIZE];
		static char const *const kinds[] = { "online", "request", "offline", "sent", "recent", "blocked" };

		p2p_friend_code_format(entries[index].code, code, sizeof(code));
		platform_log("friends:   %s %s (%s)%s%s%s%s", kinds[entries[index].kind % 6], entries[index].name,
			entries[index].has_code ? code : "no code", entries[index].hosting ? ", hosting" : "",
			entries[index].party_count ? ", in a party" : "", entries[index].joinable ? ", joinable" : "",
			entries[index].inviting ? ", invites" : "");
		if (entries[index].game[0])
			platform_log("friends:     %s", entries[index].game);
	}
}

/* one of debug.friends' actions ("<second> <action> [<code>]") */
static void friends_debug_action(char const *action)
{
	static struct p2p_friend entries[P2P_FRIENDS_LIST_MAXIMUM];
	char verb[24] = "", argument[600] = "";
	byte code[P2P_FRIEND_CODE_BYTES];
	boolean coded;
	short count, index;

	/* (its second, its verb, its argument: words apart) */
	while (*action == ' ')
		action++;
	while (*action && *action != ' ')
		action++;
	while (*action == ' ')
		action++;
	for (index = 0; *action && *action != ' ' && index < (short)sizeof(verb) - 1; index++)
		verb[index] = *action++;
	verb[index] = 0;
	while (*action == ' ')
		action++;
	for (index = 0; *action && *action != ' ' && index < (short)sizeof(argument) - 1; index++)
		argument[index] = *action++;
	argument[index] = 0;
	coded = argument[0] && friends_debug_code(argument, code);
	platform_log("friends: debug.friends: %s %s%s", verb, argument, argument[0] && !coded ? " (no code)" : "");
	if (!strcmp(verb, "add") && coded)
		platform_log("friends: add: %d", p2p_friends_add(code, ""));
	else if (!strcmp(verb, "remove") && coded)
		p2p_friends_remove(code);
	else if (!strcmp(verb, "block") && coded)
		p2p_friends_block(code, NULL);
	else if (!strcmp(verb, "unblock") && coded)
		p2p_friends_unblock(code);
	else if (!strcmp(verb, "invite") && coded)
		platform_log("friends: invite: %d", p2p_friends_invite(code));
	else if (!strcmp(verb, "join") && coded)
		platform_log("friends: join: %d", p2p_friends_join(code));
	else if (!strcmp(verb, "accept_invite"))
	{
		char name[P2P_FRIEND_NAME_SIZE];
		unsigned long serial = p2p_friends_invite_prompt(name, sizeof(name));

		platform_log("friends: accept_invite: %s", serial ? name : "none");
		p2p_friends_answer(serial, TRUE);
	}
	else if (!strcmp(verb, "accept") || !strcmp(verb, "ignore"))
	{
		count = (short)p2p_friends_list(entries, P2P_FRIENDS_LIST_MAXIMUM);
		for (index = 0; index < count; index++)
		{
			if (entries[index].kind != _p2p_friend_request)
				continue;
			if (!strcmp(verb, "accept"))
				p2p_friends_add(entries[index].code, entries[index].name);
			else
				p2p_friends_ignore(entries[index].code);
		}
	}
	else if (!strcmp(verb, "share") || !strcmp(verb, "hide"))
		config_write("friends.share_status", !strcmp(verb, "share") ? "true" : "false");
	else if (!strcmp(verb, "list"))
		friends_debug_list();
	else if (!strcmp(verb, "show"))
		friends_menu.debug_show = (short)(atoi(argument) + 1);
}

/* debug.friends' actions due ("<second> <action>|...") */
static void friends_debug_update(unsigned long now)
{
	char const *text = config_string("debug.friends");
	short index;

	if (!*text)
		return;
	if (!friends_menu.debug_read)
	{
		friends_menu.debug_read = TRUE;
		friends_menu.debug_start = now;
	}
	for (index = 0; *text; index++)
	{
		char action[640];
		char const *end = strchr(text, '|');
		size_t length = end ? (size_t)(end - text) : strlen(text);

		if (index >= friends_menu.debug_next)
		{
			snprintf(action, sizeof(action), "%.*s", (int)MIN(length, sizeof(action) - 1), text);
			if (now - friends_menu.debug_start < (unsigned long)atoi(action) * 1000)
				return;
			friends_menu.debug_next = (short)(index + 1);
			friends_debug_action(action);
		}
		text += length + (end ? 1 : 0);
	}
}

void friends_update(void)
{
	char folder[1024], name[P2P_FRIEND_NAME_SIZE];
	unsigned long now = system_milliseconds();
	char const *test_folder;

	if (friends_menu.update_time && now - friends_menu.update_time < FRIENDS_UPDATE_INTERVAL)
		return;
	friends_menu.update_time = now | 1;
	if (!game_time_initialized())
		return;
	folder[0] = name[0] = 0;
	test_folder = config_string("debug.friends_folder");
	if (*test_folder)
		snprintf(folder, sizeof(folder), "%s", test_folder);
	else
		friends_profile(folder, sizeof(folder), name, sizeof(name));
	if (!name[0])
		snprintf(name, sizeof(name), "%s", *config_string("debug.network_test_name") ?
			config_string("debug.network_test_name") : "Player");
	p2p_friends_set_profile(folder, name);
	friends_debug_update(now);
}

/* ---------- the words */

static void friends_wide(char const *text, wchar_t *wide, short size)
{
	short index;

	for (index = 0; text[index] && index < size - 1; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
}

/* UTF-8's characters of the game's words (a friend's game, Discord's), its
others as "?" */
static void friends_wide_utf8(char const *text, wchar_t *wide, short size)
{
	unsigned char const *bytes = (unsigned char const *)text;
	short written = 0;

	while (*bytes && written < size - 1)
	{
		unsigned long character = *bytes++;
		short more = character >= 0xF0 ? 3 : character >= 0xE0 ? 2 : character >= 0xC0 ? 1 : 0;

		if (more)
			character &= 0x3F >> more;
		for (; more && (*bytes & 0xC0) == 0x80; more--)
			character = character << 6 | (*bytes++ & 0x3F);
		wide[written++] = (wchar_t)(character < 0x20 || character > 0xFFFF || more ? '?' : character);
	}
	wide[written] = 0;
}

/* where a friend online is, in a row's few words */
static wchar_t const *friends_where(struct p2p_friend const *entry)
{
	if (entry->inviting)
		return L"Invites you";
	if (entry->hosting)
		return L"Hosting";
	switch (entry->place)
	{
	case _p2p_party_place_joining:
		return L"Joining a game";
	case _p2p_party_place_game:
	case _p2p_party_place_local_game:
		return L"In a game";
	}
	return entry->party_count ? L"In a party" : L"Online";
}

/* a row's note (its end, or under its name on a touchscreen): where a
friend is, or what the row is; entry NULL: this profile's own row (its
code); live: green (a friend online, a request) */
static void friends_row_note(struct p2p_friend const *entry, char const *own_code, long long now, wchar_t *text,
	short size, boolean *live)
{
	char ago[32];

	*live = FALSE;
	text[0] = 0;
	if (!entry)
	{
		friends_wide(own_code[0] ? own_code : "NO PROFILE", text, size);
		return;
	}
	switch (entry->kind)
	{
	case _p2p_friend_online:
		*live = TRUE;
		usnprintf(text, size - 1, L"%s", friends_where(entry));
		break;
	case _p2p_friend_request:
		*live = TRUE;
		usnprintf(text, size - 1, L"Wants to be friends");
		break;
	case _p2p_friend_offline:
		usnprintf(text, size - 1, L"Offline");
		break;
	case _p2p_friend_sent:
		usnprintf(text, size - 1, L"Request sent");
		break;
	case _p2p_friend_recent:
		if (!entry->has_code)
			usnprintf(text, size - 1, entry->opence ? L"OpenCE player" : L"No friend code");
		else if (entry->relation == _p2p_friend_online)
			usnprintf(text, size - 1, L"Friend");
		else if (entry->relation == _p2p_friend_sent)
			usnprintf(text, size - 1, L"Request sent");
		else
		{
			wchar_t when[32];

			browser_favorite_last_seen_text(entry->time, now, ago, sizeof(ago));
			friends_wide(ago, when, NUMBEROF(when));
			usnprintf(text, size - 1, L"Played %s", when);
		}
		break;
	case _p2p_friend_blocked:
		usnprintf(text, size - 1, L"Blocked");
		break;
	}
	text[size - 1] = 0;
}

/* the prompts a row has (a bit each, _friends_prompt_*): entry NULL, this
profile's own row; sharing: friends.share_status; online: internet play on */
static unsigned long friends_prompts_of(struct p2p_friend const *entry, boolean identity, boolean sharing,
	boolean online)
{
	unsigned long prompts = 0;

	if (!entry)
	{
		if (identity)
		{
			SET_FLAG(prompts, _friends_prompt_copy, TRUE);
			SET_FLAG(prompts, _friends_prompt_code, TRUE);
			SET_FLAG(prompts, sharing ? _friends_prompt_hide : _friends_prompt_show, TRUE);
		}
		return prompts;
	}
	switch (entry->kind)
	{
	case _p2p_friend_online:
		if (online && (entry->joinable || entry->inviting))
			SET_FLAG(prompts, _friends_prompt_join, TRUE);
		if (online)
			SET_FLAG(prompts, _friends_prompt_invite, TRUE);
		/* (fall through: a friend's REMOVE, which also ends its invites; a
		removed friend's new request may be blocked) */
	case _p2p_friend_offline:
		SET_FLAG(prompts, _friends_prompt_remove, TRUE);
		break;
	case _p2p_friend_request:
		SET_FLAG(prompts, _friends_prompt_accept, TRUE);
		SET_FLAG(prompts, _friends_prompt_ignore, TRUE);
		SET_FLAG(prompts, _friends_prompt_block, TRUE);
		break;
	case _p2p_friend_sent:
		SET_FLAG(prompts, _friends_prompt_cancel, TRUE);
		break;
	case _p2p_friend_recent:
		if (entry->has_code && (entry->relation == _p2p_friend_none || entry->relation == _p2p_friend_request))
			SET_FLAG(prompts, _friends_prompt_add, TRUE);
		if (entry->has_code && entry->relation != _p2p_friend_blocked && entry->relation != _p2p_friend_online)
			SET_FLAG(prompts, _friends_prompt_block, TRUE);
		break;
	case _p2p_friend_blocked:
		SET_FLAG(prompts, _friends_prompt_unblock, TRUE);
		break;
	}
	return prompts;
}

/* the panel's words for a row (entry NULL: this profile's own): its kind,
its status (where a friend is, or what the row is), its detail; online:
internet play on */
static void friends_panel_text(struct p2p_friend const *entry, char const *own_code, boolean sharing,
	boolean online, long long now, wchar_t *kind, short kind_size, wchar_t *status, short status_size,
	wchar_t *detail, short detail_size)
{
	wchar_t code[P2P_FRIEND_CODE_TEXT_SIZE], game[P2P_FRIEND_GAME_SIZE];
	char code_text[P2P_FRIEND_CODE_TEXT_SIZE], ago[32];

	kind[0] = status[0] = detail[0] = 0;
	if (!entry)
	{
		usnprintf(kind, kind_size - 1, L"YOUR PROFILE");
		if (!own_code[0])
		{
			usnprintf(status, status_size - 1, L"No player profile");
			usnprintf(detail, detail_size - 1, L"Friends belong to a player profile. Make one in Settings first.");
		}
		else
		{
			usnprintf(status, status_size - 1, !online ? L"Offline: internet play is off" : sharing ?
				L"Friends see you online" : L"You appear offline");
			usnprintf(detail, detail_size - 1, L"Send your friends your code. They add you with ADD BY CODE, or "
				L"from Recent Players after a game together.");
		}
		kind[kind_size - 1] = status[status_size - 1] = detail[detail_size - 1] = 0;
		return;
	}
	p2p_friend_code_format(entry->code, code_text, sizeof(code_text));
	friends_wide(code_text, code, NUMBEROF(code));
	/* (a game's "OpenH1 <rate> Tick", as every build sends and reads it:
	doubles_friend_in_kind, said by the name players know) */
	if (!strncmp(entry->game, "OpenH1 ", 7))
	{
		usnprintf(game, NUMBEROF(game) - 1, L"GOAT H1 ");
		friends_wide_utf8(entry->game + 7, game + 8, NUMBEROF(game) - 8);
	}
	else
		friends_wide_utf8(entry->game, game, NUMBEROF(game));
	switch (entry->kind)
	{
	case _p2p_friend_online:
		usnprintf(kind, kind_size - 1, L"FRIEND, ONLINE");
		if (entry->hosting)
			usnprintf(status, status_size - 1, L"Hosting a game");
		else if (entry->place == _p2p_party_place_game || entry->place == _p2p_party_place_local_game)
			usnprintf(status, status_size - 1, L"In a game");
		else if (entry->place == _p2p_party_place_joining)
			usnprintf(status, status_size - 1, L"Joining a game");
		else if (entry->party_count > 1)
			usnprintf(status, status_size - 1, L"In a party of %d", entry->party_count);
		else
			usnprintf(status, status_size - 1, L"In the menus");
		if (entry->inviting)
			usnprintf(detail, detail_size - 1, L"Invites you to their party: JOIN to go with them.");
		else if (game[0] && entry->maximum_players)
			usnprintf(detail, detail_size - 1, L"%s (%d of %d).", game, entry->players, entry->maximum_players);
		else if (game[0])
			usnprintf(detail, detail_size - 1, L"%s.", game);
		else if (entry->joinable)
			usnprintf(detail, detail_size - 1, L"JOIN to party up: you go with them to every game they play.");
		break;
	case _p2p_friend_request:
		usnprintf(kind, kind_size - 1, L"FRIEND REQUEST");
		usnprintf(status, status_size - 1, L"Wants to be friends");
		usnprintf(detail, detail_size - 1, L"ACCEPT, and you see each other online. Their code: %s.", code);
		break;
	case _p2p_friend_offline:
		usnprintf(kind, kind_size - 1, L"FRIEND");
		usnprintf(status, status_size - 1, L"Offline");
		usnprintf(detail, detail_size - 1, L"Their code: %s.", code);
		break;
	case _p2p_friend_sent:
		usnprintf(kind, kind_size - 1, L"REQUEST SENT");
		usnprintf(status, status_size - 1, L"Waiting for them");
		usnprintf(detail, detail_size - 1, L"It's sent again while you play, until they accept. Code: %s.", code);
		break;
	case _p2p_friend_recent:
		usnprintf(kind, kind_size - 1, L"RECENT PLAYER");
		browser_favorite_last_seen_text(entry->time, now, ago, sizeof(ago));
		{
			wchar_t when[32];

			friends_wide(ago, when, NUMBEROF(when));
			usnprintf(status, status_size - 1, L"Played %s", when);
		}
		if (!entry->has_code)
		{
			usnprintf(detail, detail_size - 1, entry->opence ?
				L"An OpenCE player: OpenCE has no friend codes, so they can't be added." :
				L"No friend code: they played on an older version of GOAT H1 or Halo, so they can't be added.");
		}
		else if (entry->relation == _p2p_friend_online)
			usnprintf(detail, detail_size - 1, L"Already your friend. %s", game);
		else
			usnprintf(detail, detail_size - 1, game[0] ? L"%s. ADD FRIEND sends a friend request." :
				L"ADD FRIEND sends a friend request.", game);
		break;
	case _p2p_friend_blocked:
		usnprintf(kind, kind_size - 1, L"BLOCKED");
		usnprintf(status, status_size - 1, L"Blocked");
		usnprintf(detail, detail_size - 1, L"Their requests and invites are let go. UNBLOCK to allow them again.");
		break;
	}
	kind[kind_size - 1] = status[status_size - 1] = detail[detail_size - 1] = 0;
}

/* ---------- the Friends screen */

static void friends_done(wchar_t const *format, char const *name)
{
	wchar_t wide[P2P_FRIEND_NAME_SIZE];

	friends_wide(name && name[0] ? name : "Them", wide, NUMBEROF(wide));
	usnprintf(friends_menu.done, NUMBEROF(friends_menu.done) - 1, format, wide);
	friends_menu.done[NUMBEROF(friends_menu.done) - 1] = 0;
	friends_menu.done_time = system_milliseconds() | 1;
}

/* the row (0 up) of a widget of the list, NONE if it is none */
static short friends_row_of(struct widget_instance const *widget)
{
	return widget && !strncmp(widget->name, "fr_row_", 7) ? (short)atoi(widget->name + 7) : NONE;
}

/* the rows the list has (the computer's FRIENDS_ROWS, the touchscreen's
fewer) */
static short friends_rows_of(struct widget_instance *list)
{
	struct widget_instance *child;
	short rows = 0;

	for (child = list->child; child; child = child->next)
	{
		if (friends_row_of(child) != NONE)
			rows++;
	}
	return (short)MIN(rows, FRIENDS_ROWS);
}

static void friends_focus_row(struct widget_instance *list, short row)
{
	struct widget_instance *child;
	short index = 0;

	for (child = list->child; child; child = child->next, index++)
	{
		if (friends_row_of(child) == row)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
			return;
		}
	}
}

/* the chosen row's entry (NULL: this profile's own row) */
static struct p2p_friend const *friends_chosen_entry(void)
{
	return friends_menu.chosen > 0 && friends_menu.chosen <= friends_menu.count ?
		&friends_menu.entries[friends_menu.chosen - 1] : NULL;
}

/* "port friends open" (the screen's created) */
static boolean friends_screen_open(short controller)
{
	friends_menu.controller = controller >= 0 && controller < 4 ? controller : 0;
	friends_menu.prompts_kind = NONE;
	friends_menu.confirm = NONE;
	friends_menu.open_time = system_milliseconds();
	return TRUE;
}

/* "port friends update" (the list's): the rows (scrolled on at their ends),
the panel, the prompts of the row chosen (not inlined in the menus'
function that runs it, as party_screen_update is not) */
static __declspec(noinline) void friends_screen_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *panel = named(screen, "friends_panel", 0);
	struct widget_instance *bar = named(list, "friends_prompts", 0);
	struct p2p_friends_state state;
	struct p2p_friend const *entry;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH], kind[48], status[64], detail[160];
	long long now = browser_favorites_now();
	boolean touch = ui_widget_port_touch_menus();
	boolean sharing = config_boolean("friends.share_status");
	boolean online = config_boolean("network.online");
	short rows = friends_rows_of(list);
	short focused = friends_row_of(list->focused_child);
	short total, row, index;
	unsigned long prompts;

	/* (the Party screen may open over it: a friend's party joined) */
	party_screen_noted(list);
	p2p_friends_state(&state);
	friends_menu.count = (short)p2p_friends_list(friends_menu.entries, P2P_FRIENDS_LIST_MAXIMUM);
	total = (short)(friends_menu.count + 1);
	/* (the entry chosen kept, as the list moves: by its code) */
	if (friends_menu.chosen > 0)
	{
		short found = NONE;

		for (index = 0; index < friends_menu.count && found == NONE; index++)
		{
			if (!csmemcmp(friends_menu.entries[index].code, friends_menu.chosen_code, P2P_FRIEND_CODE_BYTES) &&
				friends_menu.entries[index].kind != _p2p_friend_recent)
			{
				found = (short)(index + 1);
			}
		}
		if (found == NONE && friends_menu.chosen > friends_menu.count)
			found = friends_menu.count;
		if (found != NONE && found != friends_menu.chosen)
		{
			friends_menu.chosen = found;
			friends_menu.first = (short)PIN(friends_menu.first, found - rows + 1, found);
			if (focused != NONE)
				friends_focus_row(list, focused = (short)(found - friends_menu.first));
		}
	}
	/* (debug.friends' "show": that row chosen, a test's screenshot's) */
	if (friends_menu.debug_show && rows > 0)
	{
		friends_menu.chosen = (short)PIN(friends_menu.debug_show - 1, 0, total - 1);
		friends_menu.first = (short)PIN(friends_menu.chosen - rows + 1, 0, MAX(0, total - rows));
		friends_focus_row(list, focused = (short)(friends_menu.chosen - friends_menu.first));
		friends_menu.debug_show = 0;
	}
	/* (scrolled at the ends, as the server browser's) */
	else if (focused == rows - 1 && friends_menu.first + rows < total && list_scrolls_at_end())
	{
		friends_menu.first++;
		friends_focus_row(list, --focused);
	}
	else if (focused == 0 && friends_menu.first > 0 && list_scrolls_at_end())
	{
		friends_menu.first--;
		friends_focus_row(list, ++focused);
	}
	friends_menu.first = (short)PIN(friends_menu.first, 0, MAX(0, total - rows));
	if (focused != NONE && focused < total - friends_menu.first && !ui_widget_port_hover_focused())
		friends_menu.chosen = (short)(friends_menu.first + focused);
	friends_menu.chosen = (short)PIN(friends_menu.chosen, 0, total - 1);
	entry = friends_chosen_entry();
	if (entry)
		csmemcpy(friends_menu.chosen_code, entry->code, P2P_FRIEND_CODE_BYTES);
	/* (FRIENDS's count) */
	text[0] = 0;
	if (state.friends)
		usnprintf(text, NUMBEROF(text) - 1, L"%d OF %d ONLINE", state.online, state.friends);
	text[NUMBEROF(text) - 1] = 0;
	text_set(named(screen, "friends_count", 0), text);
	/* (the rows: their names and notes; on a touchscreen their frames, the
	chosen one's brighter) */
	for (row = 0; row < rows; row++)
	{
		char row_name[16];
		struct widget_instance *item;
		struct p2p_friend const *shown;
		short at = (short)(friends_menu.first + row);
		boolean live;

		snprintf(row_name, sizeof(row_name), "fr_row_%d", row);
		item = named(list, row_name, 0);
		visible_set(item, at < total);
		if (!item || at >= total)
			continue;
		shown = at > 0 ? &friends_menu.entries[at - 1] : NULL;
		if (shown)
			friends_wide(shown->name[0] ? shown->name : "Player", text, NUMBEROF(text));
		else
			usnprintf(text, NUMBEROF(text) - 1, L"YOUR FRIEND CODE");
		menu_text_box_fit(item, text, 0);
		friends_row_note(shown, state.code, now, text, NUMBEROF(text), &live);
		snprintf(row_name, sizeof(row_name), "fr_note_%d", row);
		text_set(named(item, row_name, 0), live ? L"" : text);
		snprintf(row_name, sizeof(row_name), "fr_live_%d", row);
		text_set(named(item, row_name, 0), live ? text : L"");
		if (touch)
			item->animation.current_frame_index = at == friends_menu.chosen ? 2 : item == list->focused_child ? 1 : 0;
	}
	/* (the panel: the row chosen, and this profile's code) */
	friends_panel_text(entry, state.code, sharing, online, now, kind, NUMBEROF(kind), status, NUMBEROF(status),
		detail, NUMBEROF(detail));
	if (panel)
	{
		text_set(named(panel, "panel_kind", 0), kind);
		if (entry)
			friends_wide(entry->name[0] ? entry->name : "Player", text, NUMBEROF(text));
		else
			friends_wide(state.name[0] ? state.name : "Player", text, NUMBEROF(text));
		menu_text_box_fit(named(panel, "panel_title", 0), text, 0);
		menu_text_box_wrap(named(panel, "panel_status", 0), status, 2);
		menu_text_box_wrap(named(panel, "panel_detail", 0), detail, touch ? 5 : 4);
		friends_wide(state.code[0] ? state.code : "-", text, NUMBEROF(text));
		text_set(named(panel, "panel_code", 0), text);
		if (friends_menu.done_time && system_milliseconds() - friends_menu.done_time < FRIENDS_DONE_TIME)
			menu_text_box_wrap(named(panel, "panel_help", 0), friends_menu.done, 2);
		else
		{
			menu_text_box_wrap(named(panel, "panel_help", 0), !online ?
				L"Internet play is off. Turn it on in Settings, Online." : !state.connected ?
				(system_milliseconds() - friends_menu.open_time < FRIENDS_CONNECT_TIME ? L"Connecting..." :
				L"Can't reach your friends. Check your internet.") :
				!sharing ? L"You appear offline: friends can't see you." :
				L"Only your friends see where you are.", 2);
		}
	}
	/* (the prompts of the row chosen; the focus on the first shown when the
	row's kind changes) */
	prompts = friends_prompts_of(entry, state.identity, sharing, online);
	for (index = 0; index < NUMBER_OF_FRIENDS_PROMPTS; index++)
		menu_prompts_show(bar, friends_prompt_names[index], TEST_FLAG(prompts, index));
	if (bar && (entry ? entry->kind : NONE) != friends_menu.prompts_kind)
	{
		friends_menu.prompts_kind = (short)(entry ? entry->kind : NONE);
		focus_off_hidden(bar);
	}
}

/* a REMOVE or BLOCK, pressed twice to be done: TRUE the second time */
static boolean friends_confirmed(short prompt, struct p2p_friend const *entry)
{
	unsigned long now = system_milliseconds();

	if (friends_menu.confirm == prompt && now - friends_menu.confirm_time < FRIENDS_CONFIRM_TIME &&
		!csmemcmp(friends_menu.confirm_code, entry->code, P2P_FRIEND_CODE_BYTES))
	{
		friends_menu.confirm = NONE;
		return TRUE;
	}
	friends_menu.confirm = prompt;
	friends_menu.confirm_time = now;
	csmemcpy(friends_menu.confirm_code, entry->code, P2P_FRIEND_CODE_BYTES);
	friends_done(prompt == _friends_prompt_remove ? L"Press REMOVE again to remove %s." :
		L"Press BLOCK again to block %s.", entry->name);
	return FALSE;
}

/* a prompt's doing, for the row chosen: FALSE if it fails */
static boolean friends_act(short prompt, struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	struct p2p_friend const *entry = friends_chosen_entry();
	struct p2p_friend chosen;
	struct p2p_friends_state state;
	unsigned long prompts;

	p2p_friends_state(&state);
	prompts = friends_prompts_of(entry, state.identity, config_boolean("friends.share_status"),
		config_boolean("network.online"));
	if (prompt == NONE || !TEST_FLAG(prompts, prompt))
		return TRUE;
	csmemset(&chosen, 0, sizeof(chosen));
	if (entry)
		chosen = *entry;
	switch (prompt)
	{
	case _friends_prompt_copy:
		platform_clipboard_set(state.code);
		friends_done(L"Your code is copied: paste it to your friends.", NULL);
		break;
	case _friends_prompt_code:
		friends_menu.controller = controller >= 0 && controller < 4 ? controller : 0;
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return ui_widget_port_open(widget, FRIENDS_ADD_NAME, widget_deleted);
	case _friends_prompt_hide:
	case _friends_prompt_show:
		config_write("friends.share_status", prompt == _friends_prompt_show ? "true" : "false");
		friends_done(prompt == _friends_prompt_show ? L"Friends see you online again." :
			L"You appear offline: friends can't see you're on.", NULL);
		break;
	case _friends_prompt_join:
		if (!p2p_friends_join(chosen.code))
			return campaign_fail();
		friends_done(L"Joining %s's party...", chosen.name);
		break;
	case _friends_prompt_invite:
		if (!p2p_friends_invite(chosen.code))
			return campaign_fail();
		friends_done(L"Invited %s to your party.", chosen.name);
		break;
	case _friends_prompt_accept:
	case _friends_prompt_add:
		if (p2p_friends_add(chosen.code, chosen.name) != _p2p_friends_done)
			return campaign_fail();
		friends_done(prompt == _friends_prompt_accept ? L"You and %s are friends." : L"Friend request sent to %s.",
			chosen.name);
		break;
	case _friends_prompt_ignore:
		p2p_friends_ignore(chosen.code);
		friends_done(L"Ignored %s's request.", chosen.name);
		break;
	case _friends_prompt_cancel:
		p2p_friends_cancel(chosen.code);
		friends_done(L"Request cancelled.", NULL);
		break;
	case _friends_prompt_remove:
		if (!friends_confirmed(prompt, &chosen))
			return TRUE;
		p2p_friends_remove(chosen.code);
		friends_done(L"Removed %s.", chosen.name);
		break;
	case _friends_prompt_block:
		if (!friends_confirmed(_friends_prompt_block, &chosen))
			return TRUE;
		if (!p2p_friends_block(chosen.code, chosen.name))
			return campaign_fail();
		friends_done(L"Blocked %s: no requests or invites from them.", chosen.name);
		break;
	case _friends_prompt_unblock:
		p2p_friends_unblock(chosen.code);
		friends_done(L"Unblocked %s.", chosen.name);
		break;
	}
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* the first prompt (of those given) the row chosen has, NONE if none */
static short friends_prompt_shown(short const *choices, short count)
{
	struct p2p_friends_state state;
	unsigned long prompts;
	short index;

	p2p_friends_state(&state);
	prompts = friends_prompts_of(friends_chosen_entry(), state.identity, config_boolean("friends.share_status"),
		config_boolean("network.online"));
	for (index = 0; index < count; index++)
	{
		if (TEST_FLAG(prompts, choices[index]))
			return choices[index];
	}
	return NONE;
}

/* ---------- ADD BY CODE */

static void friends_add_done(char const *text)
{
	snprintf(friends_menu.add_text, sizeof(friends_menu.add_text), "%s", text);
}

/* "port friends code init": a code on the clipboard first, its typing begun */
static boolean friends_add_initialize(struct widget_instance *list)
{
	struct widget_instance *row = named(list, "op_code", 0);
	char clipboard[TEXT_FIELD_LENGTH];
	byte code[P2P_FRIEND_CODE_BYTES];

	friends_menu.add_text[0] = 0;
	friends_menu.add_help = _friends_add_help_ask;
	if (platform_clipboard_get(clipboard, sizeof(clipboard)) && p2p_friend_code_parse(clipboard, code) > 0)
		p2p_friend_code_format(code, friends_menu.add_text, sizeof(friends_menu.add_text));
	if (row)
		text_field_begin(row, friends_menu.add_text, FRIENDS_CODE_TEXT, friends_add_done);
	return TRUE;
}

/* "port friends code update" */
static void friends_add_update(struct widget_instance *list)
{
	struct widget_instance *help = list->parameters.list.extended_description;

	text_field_show(named(list, "code_value", 0), friends_menu.add_text, text_field_editing(named(list, "op_code", 0)));
	if (help)
		help->parameters.text_box.string_list_index = friends_menu.add_help;
}

/* "port friends code send" (SEND REQUEST, and Enter): the request sent,
and back to the Friends screen; else the help says why not */
static boolean friends_add_send(struct widget_instance *widget, boolean *widget_deleted)
{
	struct widget_instance *row = named(screen_of(widget), "op_code", 0);
	byte code[P2P_FRIEND_CODE_BYTES];
	int parsed, result;

	if (text_field_editing(NULL))
		text_field_end(TRUE);
	parsed = p2p_friend_code_parse(friends_menu.add_text, code);
	result = parsed > 0 ? p2p_friends_add(code, "") : NONE;
	if (result != _p2p_friends_done)
	{
		friends_menu.add_help = (short)(parsed < 0 ? _friends_add_help_typo : !parsed ? _friends_add_help_not_code :
			result == _p2p_friends_self ? _friends_add_help_own : result == _p2p_friends_already ?
			_friends_add_help_already : result == _p2p_friends_full ? _friends_add_help_full :
			_friends_add_help_no_profile);
		if (row)
			text_field_begin(row, friends_menu.add_text, FRIENDS_CODE_TEXT, friends_add_done);
		return campaign_fail();
	}
	{
		char text[P2P_FRIEND_CODE_TEXT_SIZE];

		p2p_friend_code_format(code, text, sizeof(text));
		friends_done(L"Friend request sent to %s.", text);
	}
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* "port friends code back" (B, Escape): back at once, though it is typed */
static boolean friends_add_back(struct widget_instance *widget, boolean *widget_deleted)
{
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* "port friends code edit" (the field's row's A: the typing begins; Enter,
its end, sends) */
static boolean friends_add_edit(struct widget_instance *row, boolean *widget_deleted)
{
	if (text_field_editing(row))
		return friends_add_send(row, widget_deleted);
	text_field_begin(row, friends_menu.add_text, FRIENDS_CODE_TEXT, friends_add_done);
	return TRUE;
}

/* ---------- the functions' entry ("port friends <name>") */

static boolean friends_menu_event(char const *name, struct widget_instance *widget, short controller,
	boolean *widget_deleted)
{
	static short const x_prompts[] = { _friends_prompt_code, _friends_prompt_invite, _friends_prompt_ignore,
		_friends_prompt_cancel };
	static short const y_prompts[] = { _friends_prompt_hide, _friends_prompt_show, _friends_prompt_remove,
		_friends_prompt_block };
	static short const main_prompts[] = { _friends_prompt_copy, _friends_prompt_join, _friends_prompt_accept,
		_friends_prompt_add, _friends_prompt_unblock };
	short index;

	if (!strcmp(name, "open"))
		return friends_screen_open(controller);
	if (!strcmp(name, "x"))
		return friends_act(friends_prompt_shown(x_prompts, NUMBEROF(x_prompts)), widget, controller, widget_deleted);
	if (!strcmp(name, "y"))
		return friends_act(friends_prompt_shown(y_prompts, NUMBEROF(y_prompts)), widget, controller, widget_deleted);
	if (!strcmp(name, "code init"))
		return friends_add_initialize(widget);
	if (!strcmp(name, "code edit"))
		return friends_add_edit(widget, widget_deleted);
	if (!strcmp(name, "code send"))
		return friends_add_send(widget, widget_deleted);
	if (!strcmp(name, "code back"))
		return friends_add_back(widget, widget_deleted);
	if (strcmp(name, "button"))
		return TRUE;
	/* (a row's A or click: it chosen, its first action; a prompt's: its own) */
	if (friends_row_of(widget) != NONE)
	{
		friends_menu.chosen = (short)(friends_menu.first + friends_row_of(widget));
		if (friends_chosen_entry())
			csmemcpy(friends_menu.chosen_code, friends_chosen_entry()->code, P2P_FRIEND_CODE_BYTES);
		return friends_act(friends_prompt_shown(main_prompts, NUMBEROF(main_prompts)), widget, controller,
			widget_deleted);
	}
	if (!strcmp(widget->name, "prompt_back"))
	{
		event_manager_post_button(controller, BUTTON_B);
		return TRUE;
	}
	for (index = 0; index < NUMBER_OF_FRIENDS_PROMPTS; index++)
	{
		if (!strcmp(widget->name, friends_prompt_names[index]))
			return friends_act(index, widget, controller, widget_deleted);
	}
	return TRUE;
}

/* ---------- Multiplayer's FRIENDS */

/* the note at its end: friends online, or requests */
static void friends_hub_note(wchar_t *text, short size)
{
	struct p2p_friends_state state;

	p2p_friends_state(&state);
	text[0] = 0;
	if (state.requests)
		usnprintf(text, size - 1, state.requests == 1 ? L"1 REQUEST" : L"%d REQUESTS", state.requests);
	else if (state.online)
		usnprintf(text, size - 1, L"%d ONLINE", state.online);
	text[size - 1] = 0;
}

static void friends_hub_status(wchar_t *headline, short headline_size, wchar_t *detail, short detail_size)
{
	struct p2p_friends_state state;
	wchar_t code[P2P_FRIEND_CODE_TEXT_SIZE];

	p2p_friends_state(&state);
	friends_wide(state.code, code, NUMBEROF(code));
	if (!state.identity)
	{
		usnprintf(headline, headline_size - 1, L"NO PLAYER PROFILE");
		usnprintf(detail, detail_size - 1, L"Make a profile in\r\nSettings first.");
	}
	else if (!config_boolean("network.online"))
	{
		usnprintf(headline, headline_size - 1, L"INTERNET PLAY IS OFF");
		usnprintf(detail, detail_size - 1, L"Turn it on in Settings,\r\nOnline.");
	}
	else if (!state.friends)
	{
		usnprintf(headline, headline_size - 1, L"YOUR CODE: %s", code);
		usnprintf(detail, detail_size - 1, L"Send it to friends, or add\r\nsomeone you played with.");
	}
	else
	{
		usnprintf(headline, headline_size - 1, L"%d OF %d ONLINE", state.online, state.friends);
		if (state.requests)
		{
			usnprintf(detail, detail_size - 1, L"%d friend request%s waiting.\r\nYour code: %s", state.requests,
				state.requests == 1 ? L"" : L"s", code);
		}
		else
			usnprintf(detail, detail_size - 1, L"Your code: %s", code);
	}
	headline[headline_size - 1] = detail[detail_size - 1] = 0;
}
