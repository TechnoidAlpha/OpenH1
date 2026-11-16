/*
DOUBLES_BROWSER.INC

The 2V2 browser (tools/port_settings.py's _doubles_browser): the main menu's
2V2, its kind chosen (CLASSIC or CRISPY: matchmaking_menu.inc's
chooser), lists that kind's games. It is the server browser: its list is the
server browser's widgets (the column titles, which sort and search, and the
games' rows) and is filled, ordered, grouped and joined by the server
browser's code (lobby_browser_update, lobby_browser_select), with the games
kept to the kind's (browser_doubles_playlist: matchmaking.c's
matchmaking_playlist_games, the search's own test, so the browser and the
search never disagree: an OpenH1 game listed for the kind's playlist, at its
tick rate, 120 for both; never the other kind's, nor a Legacy game). A row's A joins its
game, the party following its leader as always.

Under the rows: HOST GAME, the kind's game hosted at once as matchmaking
hosts one (matchmaking_host_playlist: its tick rate, Pro Doubles, its maps,
listed for its playlist, OpenCE's players kept out, Classic's Instant Aim,
bots once the wait runs out), and FIND GAME, matchmaking's search of the
kind (a game with room, else one hosted: the chooser's search before), CANCEL
SEARCH while it goes on, its progress in the panel.
With no game of the kind, the list says so and HOST GAME has the focus.

The panel on the right: the game chosen (its name, map and mode, how it is,
as the server browser's panel says), or the search under way, and the
friends online under it, those in a 2v2 of this kind first, marked IN PRO 2V2 (their
presence says "OpenH1 120 Tick · Classic · Pro Doubles on ..." for a Classic
game, without "Classic · " for a Crispy one: lobby_menu.inc's
lobby_presence_tell); a friend's A joins them where the Friends screen
would (p2p_friends_join), else invites them to this machine's party; the
footer names it for the friend chosen (A JOIN, with X INVITE where both
are offered, else A INVITE: friends_prompts_of's rules). The
friends' rows come after the buttons in the list, so the controller's and
the keyboard's Down reaches them from the buttons.
Included in menu_functions.c after friends_menu.inc, whose helpers it uses.
*/

#define DOUBLES_BROWSER_NAME "pc\\main_menu\\doubles\\browser\\doubles_browser_screen"
#define DOUBLES_FRIEND_ROWS 6
/* milliseconds: the friends read again; what was just done shown */
#define DOUBLES_FRIENDS_INTERVAL 500
#define DOUBLES_DONE_TIME 6000

static struct
{
	/* the kind the chooser opened it for (_matchmaking_doubles_*), and the
	browser's mode before it */
	short kind;
	short previous_mode;
	/* the friends online, those in this kind's 2v2 first, as last read */
	struct p2p_friend friends[DOUBLES_FRIEND_ROWS];
	boolean in_kind[DOUBLES_FRIEND_ROWS];
	short friend_count;
	unsigned long friends_time;
	/* the focus put on HOST GAME as the list emptied (once: it may then be
	moved) */
	boolean empty_focused;
	/* the friend chosen (doubles_friend_chosen): whether there is one, its
	code; the prompts last logged for it */
	boolean friend_chosen;
	byte friend_code[P2P_FRIEND_CODE_BYTES];
	unsigned long prompts_logged;
	byte logged_code[P2P_FRIEND_CODE_BYTES];
	/* what was just done (a friend joined or invited, a host refused) */
	wchar_t done[96];
	unsigned long done_time;
} doubles_browser = { _matchmaking_doubles_classic, NONE };

/* the kind's words: "PRO 2V2 CLASSIC", "Pro 2v2 Classic" */
static wchar_t const *doubles_browser_title(void)
{
	return doubles_browser.kind == _matchmaking_doubles_crispy ? L"PRO 2V2 CRISPY" : L"PRO 2V2 CLASSIC";
}

static wchar_t const *doubles_browser_words(void)
{
	return doubles_browser.kind == _matchmaking_doubles_crispy ? L"Pro 2v2 Crispy" : L"Pro 2v2 Classic";
}

/* whether a friend's game (their presence's words) is a 2v2 of this kind:
Pro Doubles at the kind's tick rate, said to be Classic for Classic's and
not for Crispy's (both at 120: lobby_presence_tell) */
static boolean doubles_friend_in_kind(struct p2p_friend const *entry)
{
	char prefix[32];
	char const *classic = " \xC2\xB7 Classic \xC2\xB7 ";
	short rate = matchmaking_playlist_rate(browser_doubles_playlist);
	size_t length;

	if (entry->kind != _p2p_friend_online || !rate || !entry->game[0])
		return FALSE;
	snprintf(prefix, sizeof(prefix), "OpenH1 %d Tick", rate);
	length = strlen(prefix);
	if (strncmp(entry->game, prefix, length) || entry->game[length] != ' ' || !strstr(entry->game, "Pro Doubles"))
		return FALSE;
	return !strncmp(entry->game + length, classic, strlen(classic)) ==
		(doubles_browser.kind == _matchmaking_doubles_classic);
}

/* the friends online, read again a while apart: those in this kind's 2v2
first, then the others in the list's order, DOUBLES_FRIEND_ROWS at most */
static void doubles_friends_read(void)
{
	static struct p2p_friend entries[P2P_FRIENDS_LIST_MAXIMUM];
	unsigned long now = system_milliseconds();
	short count, index, pass;

	if (doubles_browser.friends_time && now - doubles_browser.friends_time < DOUBLES_FRIENDS_INTERVAL)
		return;
	doubles_browser.friends_time = now | 1;
	count = (short)p2p_friends_list(entries, P2P_FRIENDS_LIST_MAXIMUM);
	doubles_browser.friend_count = 0;
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < count && doubles_browser.friend_count < DOUBLES_FRIEND_ROWS; index++)
		{
			boolean in_kind = doubles_friend_in_kind(&entries[index]);

			if (entries[index].kind != _p2p_friend_online || in_kind != (pass == 0))
				continue;
			doubles_browser.friends[doubles_browser.friend_count] = entries[index];
			doubles_browser.in_kind[doubles_browser.friend_count] = in_kind;
			doubles_browser.friend_count++;
		}
	}
}

/* (the footer's prompts for the friend chosen: below) */
static void doubles_friend_prompts_show(struct widget_instance *list);

/* the friend of a row (doubles_friend_<n>, 1 up), NONE if it is none */
static short doubles_friend_row_of(struct widget_instance const *widget)
{
	return widget && !strncmp(widget->name, "doubles_friend_", 15) ? (short)(atoi(widget->name + 15) - 1) : NONE;
}

/* the words the list shows while it has no game of the kind (the server
browser's own while it is still looking, or internet play is off) */
static void doubles_browser_empty(wchar_t *text, short size)
{
	short problem = matchmaking_can_search(browser_doubles_playlist);

	if (problem == _matchmaking_problem_no_maps)
	{
		usnprintf(text, size - 1, L"None of %s's maps is in your maps folder.\r\nCopy the multiplayer maps there.",
			doubles_browser_words());
	}
	else
		usnprintf(text, size - 1, L"No %s games right now.\r\nHost one, and others find it.", doubles_browser_words());
	text[size - 1] = 0;
}

/* the chooser's row chosen (matchmaking_menu.inc): the browser of its kind
opened */
static boolean doubles_browser_open(struct widget_instance *widget, short kind, boolean *widget_deleted)
{
	if (!VALID_INDEX(kind, NUMBER_OF_MATCHMAKING_DOUBLES_KINDS))
		return TRUE;
	doubles_browser.kind = kind;
	platform_log("menus: 2V2 %s chosen: its games listed (OpenH1 %d Tick)",
		kind == _matchmaking_doubles_classic ? "CLASSIC" : "CRISPY",
		matchmaking_playlist_rate(matchmaking_doubles_playlist(kind)));
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return ui_widget_port_open(widget, DOUBLES_BROWSER_NAME, widget_deleted);
}

/* "port doubles init" (the screen's created): the server browser's own
beginning, of the kind's games; its title the kind's */
static boolean doubles_browser_initialize(struct widget_instance *screen, struct event_record *event,
	boolean *widget_deleted)
{
	doubles_browser.previous_mode = multiplayer.mode;
	multiplayer.mode = _multiplayer_mode_server_browser;
	lan_mode = FALSE;
	browser_doubles_playlist = matchmaking_doubles_playlist(doubles_browser.kind);
	browser_doubles_name = doubles_browser_title();
	doubles_browser.friends_time = 0;
	doubles_browser.done_time = 0;
	doubles_browser.friend_chosen = FALSE;
	doubles_browser.prompts_logged = (unsigned long)-1;
	doubles_browser.empty_focused = FALSE;
	text_set(named(screen, "doubles_kind", 0), doubles_browser.kind == _matchmaking_doubles_crispy ?
		L"CRISPY \x00B7 120 TICK \x00B7 INSTANT AIM ON" : L"CLASSIC \x00B7 120 TICK \x00B7 INSTANT AIM OFF");
	return browser_initialize(screen, event, widget_deleted);
}

/* "port doubles dispose": the server browser's end, the browser itself
again for the server browser */
static void doubles_browser_dispose(void)
{
	lobby_browser_end();
	browser_searches_forget();
	browser_doubles_playlist = NONE;
	if (doubles_browser.previous_mode != NONE)
		multiplayer.mode = doubles_browser.previous_mode;
}

/* HOST GAME's and FIND GAME's frames (port_settings.py's part_button_set:
plain, lit, lit; three, so that the engine, which lights a picture of two by
its parent's focus whatever has the list's, leaves them to this, as the
server browser's rows: browser_focus): lit only for the one focused while
their bar has the list's focus, so that a friend or a game chosen is the
one thing that looks focused */
static void doubles_actions_focus(struct widget_instance *list, struct widget_instance *actions)
{
	struct widget_instance *button;

	for (button = actions ? actions->child : NULL; button; button = button->next)
	{
		button->animation.current_frame_index =
			(short)(list->focused_child == actions && actions->focused_child == button);
	}
}

/* "port doubles update" (the list's): the server browser's update of the
rows and the panel, then the buttons, the friends and the focus */
static void doubles_browser_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *panel = list->parameters.list.extended_description;
	struct widget_instance *actions = named(list, "doubles_actions", 0);
	struct matchmaking_status status;
	wchar_t text[ROW_TEXT_LENGTH];
	unsigned long now = system_milliseconds();
	short index;
	boolean searching;

	(void)screen;
	browser_update(list);
	matchmaking_status(&status);
	searching = status.stage != _matchmaking_idle && status.playlist == browser_doubles_playlist;
	text_set(named(actions, "button_doubles_find", 0), searching ? L"CANCEL SEARCH" : L"FIND GAME");
	/* (the search under way, or what was just done, in the panel's line) */
	if (panel && (searching || (doubles_browser.done_time && now - doubles_browser.done_time < DOUBLES_DONE_TIME)))
	{
		wchar_t headline[64], detail[160], line[SETTINGS_PANEL_TEXT_LENGTH];
		wchar_t playlist[MATCHMAKING_PLAYLIST_NAME_SIZE * 2];

		if (searching)
		{
			matchmaking_playlist_text(status.playlist, playlist, NUMBEROF(playlist), NULL, 0);
			matchmaking_status_text(&status, playlist, headline, NUMBEROF(headline), detail, NUMBEROF(detail));
			usnprintf(line, NUMBEROF(line) - 1, L"%s. %s", headline, detail);
		}
		else
			usnprintf(line, NUMBEROF(line) - 1, L"%s", doubles_browser.done);
		line[NUMBEROF(line) - 1] = 0;
		menu_text_box_wrap(named(panel, "panel_status", 0), line, 3);
	}
	/* (the friends online, under the game chosen) */
	doubles_friends_read();
	for (index = 0; index < DOUBLES_FRIEND_ROWS; index++)
	{
		char name[24];
		struct widget_instance *row;
		struct p2p_friend const *entry = &doubles_browser.friends[index];
		boolean shown = index < doubles_browser.friend_count;

		snprintf(name, sizeof(name), "doubles_friend_%d", index + 1);
		row = named(list, name, 0);
		visible_set(row, shown);
		if (!row)
			continue;
		row->disabled = !shown;
		if (!shown)
			continue;
		friends_wide(entry->name[0] ? entry->name : "Player", text, NUMBEROF(text));
		menu_text_box_fit(row, text, 0);
		if (doubles_browser.in_kind[index])
			usnprintf(text, NUMBEROF(text) - 1, L"IN PRO 2V2");
		else
			usnprintf(text, NUMBEROF(text) - 1, L"%s", friends_where(entry));
		text[NUMBEROF(text) - 1] = 0;
		snprintf(name, sizeof(name), "df_live_%d", index + 1);
		text_set(named(row, name, 0), doubles_browser.in_kind[index] ? text : L"");
		snprintf(name, sizeof(name), "df_note_%d", index + 1);
		text_set(named(row, name, 0), doubles_browser.in_kind[index] ? L"" : text);
	}
	doubles_friend_prompts_show(list);
	if (panel)
	{
		struct p2p_friends_state state;

		p2p_friends_state(&state);
		visible_set(named(panel, "doubles_friends_empty", 0), !doubles_browser.friend_count);
		menu_text_box_wrap(named(panel, "doubles_friends_empty", 0), !config_boolean("network.online") ?
			L"Internet play is off." : !state.identity ? L"Friends belong to a player profile." :
			L"No friends online. Add them in Multiplayer, Friends.", 2);
	}
	/* (no game of the kind: HOST GAME has the focus, once as the list
	empties) */
	if (lobby_browser_rows() > 0)
		doubles_browser.empty_focused = FALSE;
	else if (actions && !doubles_browser.empty_focused)
	{
		doubles_browser.empty_focused = TRUE;
		list_focus_child(list, actions);
		actions->focused_child = actions->child;
		actions->parameters.list.selected_index = 0;
	}
	doubles_actions_focus(list, actions);
}

/* HOST GAME or FIND GAME refused: why, in the panel's line (matchmaking's
words: internet play off, no maps, a party's member, a party too big) */
static boolean doubles_browser_refused(void)
{
	struct matchmaking_status status;
	wchar_t headline[64], detail[160], playlist[MATCHMAKING_PLAYLIST_NAME_SIZE * 2];

	matchmaking_status(&status);
	matchmaking_playlist_text(browser_doubles_playlist, playlist, NUMBEROF(playlist), NULL, 0);
	status.stage = _matchmaking_idle;
	matchmaking_status_text(&status, playlist, headline, NUMBEROF(headline), detail, NUMBEROF(detail));
	usnprintf(doubles_browser.done, NUMBEROF(doubles_browser.done) - 1, L"%s. %s", headline, detail);
	doubles_browser.done[NUMBEROF(doubles_browser.done) - 1] = 0;
	doubles_browser.done_time = system_milliseconds();
	return campaign_fail();
}

/* "port doubles host" (HOST GAME): the kind's game hosted at once */
static boolean doubles_browser_host(short controller)
{
	if (!matchmaking_host_playlist(browser_doubles_playlist, controller >= 0 && controller < 4 ? controller : 0))
		return doubles_browser_refused();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port doubles find" (FIND GAME, X): matchmaking's search of the kind, or
the search given up */
static boolean doubles_browser_find(short controller)
{
	struct matchmaking_status status;

	matchmaking_status(&status);
	if (status.stage != _matchmaking_idle)
	{
		matchmaking_cancel();
		ui_play_audio_feedback_sound(SOUND_BACK);
		return TRUE;
	}
	if (!matchmaking_search(browser_doubles_playlist, controller >= 0 && controller < 4 ? controller : 0))
		return doubles_browser_refused();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* the friend chosen (the row focused last, kept while the focus is on the
footer's prompts), NULL for none */
static struct p2p_friend const *doubles_friend_chosen(void)
{
	short index;

	for (index = 0; doubles_browser.friend_chosen && index < doubles_browser.friend_count; index++)
	{
		if (!memcmp(doubles_browser.friends[index].code, doubles_browser.friend_code, P2P_FRIEND_CODE_BYTES))
			return &doubles_browser.friends[index];
	}
	return NULL;
}

/* what the Friends screen offers a friend (friends_prompts_of): JOIN and
INVITE, a bit each */
static unsigned long doubles_friend_prompts(struct p2p_friend const *entry)
{
	struct p2p_friends_state state;

	if (!entry)
		return 0;
	p2p_friends_state(&state);
	return friends_prompts_of(entry, state.identity, config_boolean("friends.share_status"),
		config_boolean("network.online"));
}

/* the footer's prompts for the friend chosen (the Friends screen's: A JOIN
where they can be joined, with X INVITE, else A INVITE; neither offline),
REFRESH and SORT where these take their places; logged as they change */
static void doubles_friend_prompts_show(struct widget_instance *list)
{
	struct widget_instance *bar = named(list, "doubles_prompts", 0);
	struct widget_instance *focused = list->focused_child;
	struct p2p_friend const *entry;
	unsigned long prompts;
	boolean join, invite;
	short row = doubles_friend_row_of(focused);

	if (row != NONE && row < doubles_browser.friend_count)
	{
		doubles_browser.friend_chosen = TRUE;
		csmemcpy(doubles_browser.friend_code, doubles_browser.friends[row].code, P2P_FRIEND_CODE_BYTES);
	}
	else if (focused && strcmp(focused->name, "doubles_prompts"))
		doubles_browser.friend_chosen = FALSE;
	entry = doubles_friend_chosen();
	prompts = doubles_friend_prompts(entry);
	join = TEST_FLAG(prompts, _friends_prompt_join);
	invite = TEST_FLAG(prompts, _friends_prompt_invite);
	menu_prompts_show(bar, "prompt_join", join);
	menu_prompts_show(bar, "prompt_invite", invite && !join);
	menu_prompts_show(bar, "prompt_invite_x", invite && join);
	menu_prompts_show(bar, "prompt_refresh", !entry);
	menu_prompts_show(bar, "prompt_sort", !(invite && join));
	{
		unsigned long shown = (entry ? 4UL : 0UL) | (join ? 1UL : 0UL) | (invite ? 2UL : 0UL);

		if (shown != doubles_browser.prompts_logged || (entry && memcmp(entry->code, doubles_browser.logged_code,
			P2P_FRIEND_CODE_BYTES)))
		{
			doubles_browser.prompts_logged = shown;
			if (entry)
			{
				csmemcpy(doubles_browser.logged_code, entry->code, P2P_FRIEND_CODE_BYTES);
				platform_log("menus: 2V2 friend %s chosen%s: prompts %s", entry->name,
					doubles_browser.in_kind[entry - doubles_browser.friends] ? " (in a 2v2 of this kind)" : "",
					join && invite ? "A JOIN, X INVITE" : join ? "A JOIN" : invite ? "A INVITE" : "none");
			}
		}
	}
}

/* a friend joined where they are (the Friends screen's JOIN), or invited to
this machine's party (its INVITE): action _friends_prompt_join or _invite,
or NONE for the one A offers (JOIN where it can, else INVITE) */
static boolean doubles_friend_act(struct p2p_friend const *entry, short action)
{
	unsigned long prompts = doubles_friend_prompts(entry);
	wchar_t name[P2P_FRIEND_NAME_SIZE];

	if (!entry)
		return TRUE;
	if (action == NONE)
		action = TEST_FLAG(prompts, _friends_prompt_join) ? _friends_prompt_join : _friends_prompt_invite;
	if (!TEST_FLAG(prompts, action))
		return campaign_fail();
	friends_wide(entry->name[0] ? entry->name : "Player", name, NUMBEROF(name));
	if (action == _friends_prompt_join)
	{
		if (!p2p_friends_join(entry->code))
			return campaign_fail();
		usnprintf(doubles_browser.done, NUMBEROF(doubles_browser.done) - 1, L"Joining %s...", name);
	}
	else
	{
		if (!p2p_friends_invite(entry->code))
			return campaign_fail();
		usnprintf(doubles_browser.done, NUMBEROF(doubles_browser.done) - 1, L"Invited %s to your party.", name);
	}
	platform_log("menus: 2V2 friend %s: %s", entry->name, action == _friends_prompt_join ? "joining" : "invited");
	doubles_browser.done[NUMBEROF(doubles_browser.done) - 1] = 0;
	doubles_browser.done_time = system_milliseconds();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port doubles friend" (a friend's row's A or click): what A offers them */
static boolean doubles_browser_friend(struct widget_instance *widget)
{
	short row = doubles_friend_row_of(widget);

	if (row == NONE || row >= doubles_browser.friend_count)
		return TRUE;
	return doubles_friend_act(&doubles_browser.friends[row], NONE);
}

/* "port doubles join" and "port doubles invite" (the footer's prompts):
the friend chosen joined, or invited */
static boolean doubles_browser_friend_prompt(boolean join)
{
	return doubles_friend_act(doubles_friend_chosen(), join ? _friends_prompt_join : _friends_prompt_invite);
}

/* "port doubles x" (the screen's X): INVITE for a friend chosen whom A
joins, else the server browser's SORT */
static boolean doubles_browser_x(void)
{
	unsigned long prompts = doubles_friend_prompts(doubles_friend_chosen());

	if (TEST_FLAG(prompts, _friends_prompt_join) && TEST_FLAG(prompts, _friends_prompt_invite))
		return doubles_friend_act(doubles_friend_chosen(), _friends_prompt_invite);
	return browser_sort_choose(_browser_sort_players, TRUE);
}
