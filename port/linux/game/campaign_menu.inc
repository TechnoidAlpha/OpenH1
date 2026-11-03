/*
CAMPAIGN_MENU.INC

The campaign's screens (tools/port_settings.py's _campaign_menu, _missions
and _difficulties), included by menu_functions.c, whose campaign they keep:
player 1's profile and its saved game (campaign_profile,
campaign_levels_read), a level started as the PC version's lists start it
(campaign_start, difficulty_start).

- The Campaign menu ("port campaign init", "port campaign update"): CONTINUE
  while the profile has a saved game, its mission and difficulty under it
  ("port campaign continue" goes on with it), CHOOSE MISSION, CO-OP (the
  Multiplayer's CO-OP CAMPAIGN page) and BACK, and the panel of the mission
  CONTINUE plays, else the one Choose Mission opens on, with what the item
  chosen does.
- Choose Mission ("port campaign missions init", "missions update"): HALO's
  ten levels or the custom_maps folder's campaign maps (CUSTOM: LB and RB,
  "tab previous" and "tab next", or a click on a tab, "tab"), three cards at
  a time, the chosen one larger, outlined and in the middle (but at the
  row's ends: mission_cards_first, mission_cards_place). Left and right
  ("previous", "next", or the arrows) choose the one before or after it;
  the pointer passing over a card chooses it. Each card has its level's
  picture (ui.map's sp_levels; a Custom Edition map's own, else a title
  card), MISSION 0N, its name, a badge for each difficulty, veiled where it
  is not beaten on it (the profile's flags, as the Xbox's list marks them),
  NOT YET BEATEN in their place where it is on none, and RESUME on the saved
  game's. Every mission can be played. A card's A, a second tap on a
  touchscreen or SELECT ("choose") opens the difficulties, or goes on with
  the saved game (RESUME).
- Choose Difficulty ("difficulty init", "difficulty update"): four cards,
  the one chosen last focused (game.campaign_difficulty), each its badge,
  its name, the game's words about it and BEATEN where the mission has
  been finished on it; A on one or PLAY ("play") starts the mission
  (difficulty_start).
*/

/* ---------- constants */

#define MISSION_SLOTS 3
/* a move of the row: the cards slide to their places this long */
#define MISSION_SLIDE_MILLISECONDS 160
/* (tools/port_settings.py's MISSION_BIG, MISSION_SMALL, MISSION_CARD_GAP,
MISSION_CARDS_TOP, MISSION_DOTS, MISSION_DOT_STEP) */
#define MISSION_BIG_WIDTH 158
#define MISSION_BIG_HEIGHT 240
#define MISSION_SMALL_WIDTH 132
#define MISSION_SMALL_HEIGHT 211
#define MISSION_CARD_GAP 10
#define MISSION_CARDS_TOP 84
#define MISSION_DOTS 16
#define MISSION_DOT_STEP 20
/* the card's frames (port_settings.py's mission_card: its third unused) */
#define MISSION_CARD_RESTING 0
#define MISSION_CARD_CHOSEN 1
#define MISSION_CAMPAIGN_PICTURES "ui\\shell\\bitmaps\\sp_levels"
#define MISSION_CUSTOM_PICTURES "ui\\shell\\bitmaps\\mp_map_grafix"
#define MISSION_BADGES "ui\\shell\\main_menu\\difficulty_select\\difficulty_options_small"
#define MISSION_BIG_BADGES "ui\\shell\\main_menu\\difficulty_select\\difficulty_options"
#define MISSION_TEXT_LENGTH 256
/* the Campaign menu's items' places (port_settings.py's CAMPAIGN_ITEMS_TOP,
CAMPAIGN_CONTINUE, HUB_ITEM) */
#define CAMPAIGN_ITEMS_TOP 92
#define CAMPAIGN_CONTINUE_HEIGHT 46
#define CAMPAIGN_ITEM_HEIGHT 26
/* CO-OP's words in the panel: up from a mission's name's place to its top */
#define CAMPAIGN_PANEL_COOP_RISE 158
#define NUMBER_OF_DIFFICULTIES 4
/* a card beside the chosen one, a badge not earned (a card's, the panel's):
their opacity */
#define MISSION_CARD_FAINT 0.8f
#define MISSION_BADGE_FAINT 0.3f
#define MISSION_PANEL_BADGE_FAINT 0.4f

/* ---------- globals */

static struct
{
	/* CUSTOM's missions (the custom_maps folder's campaign maps), else HALO's */
	boolean custom;
	/* the mission chosen, the first of the three shown */
	short chosen;
	short first;
	/* the mission Choose Mission opens on (the saved game's, else the one
	played last), and the difficulty screen's mission: a level, or a Custom
	Edition map's display index */
	short opening_level;
	short difficulty_level;
	short difficulty_display_index;
	/* the difficulty chosen on its screen */
	short difficulty;
	/* the mission chosen on Choose Mission last, if it has been open */
	boolean viewed;
	short viewed_level;
	/* the row as it was laid out last (NONE: not yet), and its slide: from
	how far, since when */
	short shown_first;
	real slide_from;
	unsigned long slide_start;
} campaign_cards = { FALSE, 0, 0, 0, NONE, NONE, 1, FALSE, 0, NONE, 0.0f, 0 };

/* ---------- the row's layout (tools/harness/tests/campaign_cards.c) */

/* the first of the cards shown: the chosen one in the middle, but at the
row's ends (the first mission's and the last's cards at their edges) */
static short mission_cards_first(short chosen, short count)
{
	if (count <= MISSION_SLOTS)
		return 0;
	return (short)PIN(chosen - MISSION_SLOTS / 2, 0, count - MISSION_SLOTS);
}

/* each slot's left in the menus' 640 for shown cards (a wide screen's
extra width, extra, spreading them): the chosen slot's card the larger, the
row in the middle */
static void mission_cards_place(short shown, short chosen_slot, short extra, short *x)
{
	short gap = (short)(MISSION_CARD_GAP + extra / 8);
	short total = 0, slot, left;

	for (slot = 0; slot < shown; slot++)
		total = (short)(total + (slot == chosen_slot ? MISSION_BIG_WIDTH : MISSION_SMALL_WIDTH) + (slot ? gap : 0));
	left = (short)(320 - total / 2);
	for (slot = 0; slot < shown; slot++)
	{
		x[slot] = left;
		left = (short)(left + (slot == chosen_slot ? MISSION_BIG_WIDTH : MISSION_SMALL_WIDTH) + gap);
	}
}

/* how far the row is still moved, units, elapsed milliseconds into a slide
from `from` units: eased out over MISSION_SLIDE_MILLISECONDS to nothing */
static real mission_cards_slide(real from, unsigned long elapsed)
{
	real left;

	if (elapsed >= MISSION_SLIDE_MILLISECONDS)
		return 0.0f;
	left = 1.0f - (real)elapsed / MISSION_SLIDE_MILLISECONDS;
	return from * left * left * left;
}

/* the mission one step from the chosen one (by -1 or 1), kept to the row's
ends */
static short mission_cards_step(short chosen, short step, short count)
{
	return (short)(count > 0 ? PIN(chosen + step, 0, count - 1) : 0);
}

/* ---------- words */

static void campaign_widen(char const *text, wchar_t *wide, short size)
{
	short index;

	for (index = 0; text[index] && index < size - 1; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
}

/* one of our string lists' strings on one line, as string_get has it, but
a word the game broke across two lines with a hyphen ("Cove-\nnant") whole
again */
static void campaign_string(char const *list, short index, wchar_t *text, short size)
{
	long tag_index = tag_loaded('ustr', list);
	wchar_t const *string = tag_index != NONE ? unicode_string_list_get_string(tag_index, index) : NULL;
	short length = 0;

	for (; string && *string && length < size - 1; string++)
	{
		wchar_t character = *string;

		if (character == '-' && (string[1] == '\n' || string[1] == '\r') && length && text[length - 1] != ' ')
		{
			while (string[1] == '\n' || string[1] == '\r')
				string++;
			continue;
		}
		if (character == '\r' || character == '\n')
			character = ' ';
		if (character != ' ' || (length && text[length - 1] != ' '))
			text[length++] = character;
	}
	while (length && text[length - 1] == ' ')
		length--;
	text[length] = 0;
}

static void campaign_text_set(struct widget_instance *box, wchar_t const *text)
{
	text_set_length(box, text, MISSION_TEXT_LENGTH);
}

static void campaign_text_set_ascii(struct widget_instance *box, char const *text)
{
	wchar_t wide[MISSION_TEXT_LENGTH];

	campaign_widen(text, wide, MISSION_TEXT_LENGTH);
	campaign_text_set(box, wide);
}

/* a level's name, the Xbox's "This Level Unavailable" for one not reached */
static void campaign_level_title(short level, boolean reached, wchar_t *text, short size)
{
	campaign_string("pc\\main_menu\\map_list", reached ? level : LEVEL_UNAVAILABLE, text, size);
}

static void campaign_difficulty_name(short difficulty, wchar_t *text, short size)
{
	campaign_string("pc\\main_menu\\player_profiles_select\\difficulty_names", difficulty, text, size);
}

/* "MISSION 04" */
static void campaign_mission_number(short level, wchar_t *text, short size)
{
	char number[32];

	snprintf(number, sizeof(number), "MISSION %02d", level + 1);
	campaign_widen(number, text, size);
}

/* where the player is with a level: its saved game, the hardest
difficulty it has been finished on, or not yet */
static void campaign_level_status(short level, wchar_t *text, short size)
{
	wchar_t name[ROW_TEXT_LENGTH];
	char const *before;
	short difficulty;

	if (level < 0 || level >= NUMBER_OF_SINGLE_PLAYER_LEVELS)
	{
		text[0] = 0;
		return;
	}
	if (level == campaign.saved_level)
	{
		short saved_difficulty;
		char const *map_name;
		short saved_level;

		if (ui_widget_port_saved_game(&map_name, &saved_level, &saved_difficulty))
		{
			campaign_difficulty_name(saved_difficulty, name, ROW_TEXT_LENGTH);
			before = "Saved game in progress, on ";
			campaign_widen(before, text, size);
			ustrncpy(text + ustrlen(text), name, (long)(size - 1 - (short)ustrlen(text)));
			text[size - 1] = 0;
			return;
		}
	}
	for (difficulty = NUMBER_OF_DIFFICULTIES - 1; difficulty >= 0; difficulty--)
	{
		if (campaign.levels[level].beaten & (1 << difficulty))
			break;
	}
	if (difficulty < 0)
	{
		campaign_widen("Not yet beaten.", text, size);
		return;
	}
	campaign_difficulty_name(difficulty, name, ROW_TEXT_LENGTH);
	campaign_widen("Beaten on ", text, size);
	ustrncpy(text + ustrlen(text), name, (long)(size - 1 - (short)ustrlen(text)));
	text[size - 1] = 0;
	if ((short)ustrlen(text) < size - 1)
		ustrncat(text, L".", (long)(size - 1 - (short)ustrlen(text)));
}

/* ---------- shared parts */

/* the four badges in a widget: each difficulty's own picture (ui.map's),
faint (faint: its opacity) where the mission is not beaten on it; where
the map has no badges, a shape, lit or not */
static void campaign_badges_show(struct widget_instance *parent, char const *badge, char const *shape, real faint,
	byte beaten, boolean shown)
{
	boolean have_badges = tag_loaded('bitm', MISSION_BADGES) != NONE;
	short index;

	for (index = 0; index < NUMBER_OF_DIFFICULTIES; index++)
	{
		struct widget_instance *picture = named(parent, badge, index);
		struct widget_instance *outline = named(parent, shape, index);
		boolean lit = (beaten >> index) & 1;

		visible_set(picture, shown && have_badges);
		visible_set(outline, shown && !have_badges);
		if (picture)
		{
			picture->animation.current_frame_index = index;
			picture->alpha_modifier = lit ? 1.0f : faint;
		}
		if (outline)
			outline->animation.current_frame_index = lit ? 1 : 0;
	}
}

/* the missions of the source shown */
static short campaign_mission_count(void)
{
	return campaign_cards.custom ? custom_edition_maps_count(TRUE) : NUMBER_OF_SINGLE_PLAYER_LEVELS;
}

/* a Custom Edition campaign map's display index, by its place in CUSTOM */
static short campaign_custom_display_index(short mission)
{
	return custom_edition_maps_display_index_of(TRUE, mission);
}

/* whether a Custom Edition map has its own picture */
static boolean campaign_custom_picture(short display_index)
{
	long pictures = tag_loaded('bitm', MISSION_CUSTOM_PICTURES);
	short frame = display_index;

	return pictures != NONE && display_index != NONE && custom_edition_maps_picture(pictures, &frame) != NULL;
}

/* ---------- the Campaign menu */

/* the mission Choose Mission opens on: the saved game's, else the one
played last, if it is reached */
static short campaign_opening_level(struct player_profile const *profile)
{
	short level = PIN(profile->last_single_player_map_played, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);

	if (campaign.saved_level != NONE)
		level = campaign.saved_level;
	return level;
}

static boolean campaign_screen_initialize(short controller)
{
	struct player_profile profile;

	if (campaign_profile(controller, &profile))
	{
		campaign_levels_read(&profile);
		campaign_cards.opening_level = campaign_opening_level(&profile);
	}
	else
	{
		campaign.saved_level = NONE;
		campaign_cards.opening_level = 0;
	}
	return TRUE;
}

/* the panel's mission: its picture, number, name, where the player is with
it and its objective */
static void campaign_panel_show(struct widget_instance *panel, short level)
{
	wchar_t text[MISSION_TEXT_LENGTH];
	boolean reached = level >= 0 && level < NUMBER_OF_SINGLE_PLAYER_LEVELS;
	struct widget_instance *art = named(panel, "panel_art", 0);

	if (art)
		art->animation.current_frame_index = reached ? level : LEVEL_UNAVAILABLE;
	campaign_mission_number(level, text, MISSION_TEXT_LENGTH);
	campaign_text_set(named(panel, "panel_number", 0), text);
	campaign_level_title(level, reached, text, MISSION_TEXT_LENGTH);
	menu_text_box_fit(named(panel, "panel_title", 0), text, 0);
	campaign_level_status(level, text, MISSION_TEXT_LENGTH);
	menu_text_box_fit(named(panel, "panel_status", 0), text, 0);
	campaign_string("pc\\main_menu\\solo_level_select\\map_data", reached ? level : LEVEL_UNAVAILABLE, text,
		MISSION_TEXT_LENGTH);
	menu_text_box_wrap(named(panel, "panel_objective", 0), text, 3);
}

/* the mission CHOOSE MISSION's panel shows: the one chosen there last,
else the first not yet beaten on any difficulty */
static short campaign_menu_mission(void)
{
	short level;

	if (campaign_cards.viewed)
		return campaign_cards.viewed_level;
	for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS; level++)
	{
		if (!campaign.levels[level].beaten)
			return level;
	}
	return campaign_cards.opening_level;
}

/* the panel as CO-OP's (a mission's parts hidden, its words), or as a
mission's */
static void campaign_panel_coop(struct widget_instance *panel, boolean coop)
{
	visible_set(named(panel, "panel_art", 0), !coop);
	visible_set(named(panel, "panel_art_back", 0), !coop);
	visible_set(named(panel, "panel_art_corners", 0), !coop);
	visible_set(named(panel, "panel_number", 0), !coop);
	visible_set(named(panel, "panel_status", 0), !coop);
	/* (CO-OP's words at the panel's top, where the picture is) */
	if (named(panel, "panel_title", 0))
		named(panel, "panel_title", 0)->vertical_offset = (short)(coop ? -CAMPAIGN_PANEL_COOP_RISE : 0);
	if (named(panel, "panel_objective", 0))
		named(panel, "panel_objective", 0)->vertical_offset = (short)(coop ? -CAMPAIGN_PANEL_COOP_RISE - 16 : 0);
	if (coop)
	{
		menu_text_box_fit(named(panel, "panel_title", 0), L"Co-op", 0);
		menu_text_box_wrap(named(panel, "panel_objective", 0),
			L"Play the campaign with a friend: online with your invite link, or split screen with a controller "
			L"for player 2.", 3);
	}
}

/* "port campaign update", the menu's list's: CONTINUE shown with a saved
game (its mission and difficulty under it), the focus off it without one,
the panel's mission and the item chosen's hint */
static void campaign_screen_update(struct widget_instance *list)
{
	struct widget_instance *panel = list->parameters.list.extended_description;
	struct widget_instance *item = named(list, "item_continue", 0);
	boolean saved = campaign.saved_level != NONE;
	wchar_t text[MISSION_TEXT_LENGTH], difficulty_name[ROW_TEXT_LENGTH];
	short index, hint = 0;
	struct widget_instance *child;

	visible_set(item, saved);
	if (item && !saved && list->focused_child == item)
	{
		struct widget_instance *missions = named(list, "item_missions", 0);

		if (missions)
			list->focused_child = missions;
	}
	/* (the items under CONTINUE, up in its place without it) */
	for (child = list->child, index = 0; child; child = child->next)
	{
		if (!strncmp(child->name, "item_", 5) && child != item)
		{
			child->vertical_offset = (short)(CAMPAIGN_ITEMS_TOP + (saved ? CAMPAIGN_CONTINUE_HEIGHT + 4 : 0) +
				index * (CAMPAIGN_ITEM_HEIGHT + 2));
			index++;
		}
		if (child == list->focused_child)
		{
			hint = !strcmp(child->name, "item_continue") ? 0 : !strcmp(child->name, "item_missions") ? 1 :
				!strcmp(child->name, "item_coop") ? 2 : 3;
		}
	}
	if (saved)
	{
		short difficulty, level;
		char const *map_name;

		campaign_level_title(campaign.saved_level, TRUE, text, MISSION_TEXT_LENGTH);
		if (ui_widget_port_saved_game(&map_name, &level, &difficulty))
		{
			campaign_difficulty_name(difficulty, difficulty_name, ROW_TEXT_LENGTH);
			ustrncat(text, L", ", MISSION_TEXT_LENGTH - 1 - (long)ustrlen(text));
			ustrncat(text, difficulty_name, MISSION_TEXT_LENGTH - 1 - (long)ustrlen(text));
		}
		menu_text_box_fit(named(item, "continue_note", 0), text, 0);
		if (named(item, "continue_art", 0))
			named(item, "continue_art", 0)->animation.current_frame_index = campaign.saved_level;
		/* (its picture's corners CONTINUE's fill, focused or not) */
		if (named(item, "continue_corners", 0))
			named(item, "continue_corners", 0)->animation.current_frame_index = list->focused_child == item ? 1 : 0;
	}
	/* (CONTINUE's mission; CHOOSE MISSION's the one chosen there last, else
	the first not yet beaten; CO-OP's its own words) */
	campaign_panel_coop(panel, hint == 2);
	if (hint != 2)
		campaign_panel_show(panel, hint == 0 && saved ? campaign.saved_level : campaign_menu_mission());
	string_get("pc\\main_menu\\campaign\\hints", hint, text);
	menu_text_box_wrap(named(panel, "panel_hint", 0), text, 2);
	profile_name_show(screen_of(list));
}

/* ---------- Choose Mission */

static boolean campaign_missions_initialize(struct widget_instance *list, short controller)
{
	struct player_profile profile;

	if (campaign_profile(controller, &profile))
	{
		campaign_levels_read(&profile);
		campaign_cards.opening_level = campaign_opening_level(&profile);
	}
	campaign_cards.custom = FALSE;
	campaign_cards.shown_first = NONE;
	campaign_cards.chosen = campaign_cards.opening_level;
	campaign_cards.first = mission_cards_first(campaign_cards.chosen, campaign_mission_count());
	list->focused_child = NULL;
	return TRUE;
}

/* a card's slot (its name's last digit), else NONE */
static short mission_card_slot(struct widget_instance const *widget)
{
	size_t length;

	if (!widget || strncmp(widget->name, "card_", 5))
		return NONE;
	length = strlen(widget->name);
	return length && widget->name[length - 1] >= '0' && widget->name[length - 1] < '0' + MISSION_SLOTS ?
		(short)(widget->name[length - 1] - '0') : NONE;
}

static struct widget_instance *mission_card(struct widget_instance *list, short slot, boolean big)
{
	char name[32];

	snprintf(name, sizeof(name), "card_%s_%d", big ? "big" : "small", slot);
	return named(list, name, 0);
}

/* a card's parts for a mission: its picture (a title card for a Custom
Edition map without one), number, name, badges and RESUME */
static void mission_card_show(struct widget_instance *card, short mission, boolean chosen)
{
	wchar_t text[MISSION_TEXT_LENGTH];
	struct widget_instance *art = named(card, "card_art_big", 0) ? named(card, "card_art_big", 0) :
		named(card, "card_art_small", 0);
	struct widget_instance *custom_art = named(card, "card_custom_art_big", 0) ?
		named(card, "card_custom_art_big", 0) : named(card, "card_custom_art_small", 0);
	char const *kind = named(card, "card_art_big", 0) ? "big" : "small";
	char name[48];
	boolean reached = TRUE;

	if (campaign_cards.custom)
	{
		short display_index = campaign_custom_display_index(mission);
		boolean picture = campaign_custom_picture(display_index);
		wchar_t const *map_name = custom_edition_maps_name(display_index);

		visible_set(art, FALSE);
		visible_set(custom_art, picture);
		if (custom_art)
			custom_art->animation.current_frame_index = display_index;
		ustrncpy(text, map_name ? map_name : L"", MISSION_TEXT_LENGTH - 1);
		text[MISSION_TEXT_LENGTH - 1] = 0;
		snprintf(name, sizeof(name), "card_card_tag_%s", kind);
		visible_set(named(card, name, 0), !picture);
		snprintf(name, sizeof(name), "card_card_title_%s", kind);
		visible_set(named(card, name, 0), !picture);
		menu_text_box_fit(named(card, name, 0), text, 0);
		snprintf(name, sizeof(name), "card_title_%s", kind);
		menu_text_box_wrap(named(card, name, 0), text, 2);
		snprintf(name, sizeof(name), "card_number_%s", kind);
		campaign_text_set_ascii(named(card, name, 0), "CUSTOM MISSION");
		snprintf(name, sizeof(name), "card_resume_%s", kind);
		visible_set(named(card, name, 0), FALSE);
		snprintf(name, sizeof(name), "card_badge_%s", kind);
		{
			char shape[48];

			snprintf(shape, sizeof(shape), "card_badge_shape_%s", kind);
			campaign_badges_show(card, name, shape, MISSION_BADGE_FAINT, 0, FALSE);
		}
		snprintf(name, sizeof(name), "card_unbeaten_%s", kind);
		visible_set(named(card, name, 0), FALSE);
		card->animation.current_frame_index = chosen ? MISSION_CARD_CHOSEN : MISSION_CARD_RESTING;
		return;
	}
	visible_set(custom_art, FALSE);
	visible_set(art, TRUE);
	if (art)
		art->animation.current_frame_index = reached ? mission : LEVEL_UNAVAILABLE;
	snprintf(name, sizeof(name), "card_card_tag_%s", kind);
	visible_set(named(card, name, 0), FALSE);
	snprintf(name, sizeof(name), "card_card_title_%s", kind);
	visible_set(named(card, name, 0), FALSE);
	snprintf(name, sizeof(name), "card_number_%s", kind);
	campaign_mission_number(mission, text, MISSION_TEXT_LENGTH);
	campaign_text_set(named(card, name, 0), text);
	snprintf(name, sizeof(name), "card_title_%s", kind);
	campaign_level_title(mission, reached, text, MISSION_TEXT_LENGTH);
	menu_text_box_wrap(named(card, name, 0), text, 2);
	snprintf(name, sizeof(name), "card_resume_%s", kind);
	visible_set(named(card, name, 0), mission == campaign.saved_level);
	{
		char badge[48], shape[48];
		byte beaten = campaign.levels[mission].beaten;

		snprintf(badge, sizeof(badge), "card_badge_%s", kind);
		snprintf(shape, sizeof(shape), "card_badge_shape_%s", kind);
		/* (not yet beaten on any: said in the badges' place) */
		campaign_badges_show(card, badge, shape, MISSION_BADGE_FAINT, beaten, beaten != 0);
		snprintf(name, sizeof(name), "card_unbeaten_%s", kind);
		visible_set(named(card, name, 0), beaten == 0);
	}
	card->animation.current_frame_index = chosen ? MISSION_CARD_CHOSEN : MISSION_CARD_RESTING;
	/* (its picture's corners the card's fill, resting or chosen) */
	snprintf(name, sizeof(name), "card_art_corners_%s", kind);
	if (named(card, name, 0))
		named(card, name, 0)->animation.current_frame_index = chosen ? 1 : 0;
}

/* the panel under the cards: the chosen mission's name, objective, where
the player is with it, and BEATEN ON's badges */
static void mission_panel_show(struct widget_instance *panel, short mission, short count)
{
	wchar_t text[MISSION_TEXT_LENGTH];
	short index;

	visible_set(panel, count > 0);
	if (count <= 0)
		return;
	if (campaign_cards.custom)
	{
		short display_index = campaign_custom_display_index(mission);
		wchar_t const *words = custom_edition_maps_name(display_index);

		menu_text_box_fit(named(panel, "detail_title", 0), words ? words : L"", 0);
		words = custom_edition_maps_description(display_index);
		menu_text_box_wrap(named(panel, "detail_objective", 0), words ? words : L"", 1);
		campaign_text_set_ascii(named(panel, "detail_status", 0), "A campaign map of your custom_maps folder.");
		visible_set(named(panel, "detail_status", 0), TRUE);
		visible_set(named(panel, "detail_status_new", 0), FALSE);
		campaign_badges_show(panel, "detail_badge", "detail_badge_shape", MISSION_PANEL_BADGE_FAINT, 0, TRUE);
	}
	else
	{
		boolean reached = TRUE;
		byte beaten = campaign.levels[mission].beaten;
		boolean saved = mission == campaign.saved_level;

		campaign_level_title(mission, reached, text, MISSION_TEXT_LENGTH);
		menu_text_box_fit(named(panel, "detail_title", 0), text, 0);
		campaign_string("pc\\main_menu\\solo_level_select\\map_data", reached ? mission : LEVEL_UNAVAILABLE, text,
			MISSION_TEXT_LENGTH);
		menu_text_box_wrap(named(panel, "detail_objective", 0), text, 1);
		campaign_level_status(mission, text, MISSION_TEXT_LENGTH);
		/* (NOT YET BEATEN in its own colour, which the eye finds at once) */
		menu_text_box_fit(named(panel, beaten || saved ? "detail_status" : "detail_status_new", 0), text, 0);
		visible_set(named(panel, "detail_status", 0), beaten || saved);
		visible_set(named(panel, "detail_status_new", 0), !beaten && !saved);
		campaign_badges_show(panel, "detail_badge", "detail_badge_shape", MISSION_PANEL_BADGE_FAINT, beaten, TRUE);
	}
	for (index = 0; index < NUMBER_OF_DIFFICULTIES; index++)
	{
		campaign_difficulty_name(index, text, MISSION_TEXT_LENGTH);
		campaign_text_set(named(panel, "detail_badge_name", index), text);
	}
}

/* "port campaign missions update", the row's list's: the chosen mission
(one the pointer passed over, or tapped), the row laid out about it, the
cards, the dots, the panel and the footer's SELECT or RESUME */
static void campaign_missions_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	short count = campaign_mission_count();
	short slot_x[MISSION_SLOTS];
	short focused_slot = mission_card_slot(list->focused_child);
	/* (an arrow keeps the focus the pointer gave it; else the chosen card has it) */
	boolean focus_on_card = focused_slot != NONE || !list->focused_child || !ui_widget_port_pointer_focused();
	short shown, slot, index;
	wchar_t text[MISSION_TEXT_LENGTH];

	if (focused_slot != NONE && list->focused_child->visible && campaign_cards.first + focused_slot < count)
		campaign_cards.chosen = (short)(campaign_cards.first + focused_slot);
	campaign_cards.chosen = (short)PIN(campaign_cards.chosen, 0, MAX(count - 1, 0));
	campaign_cards.first = mission_cards_first(campaign_cards.chosen, count);
	shown = (short)MIN(count, MISSION_SLOTS);
	mission_cards_place(shown, (short)(campaign_cards.chosen - campaign_cards.first), ui_widget_port_wide_extra(),
		slot_x);
	/* (a row moved slides there, from where it is drawn now: a press
	during a slide carries it on) */
	{
		unsigned long now = system_milliseconds();
		real pitch = (real)(MISSION_SMALL_WIDTH + MISSION_CARD_GAP + ui_widget_port_wide_extra() / 8);
		real shift = mission_cards_slide(campaign_cards.slide_from, now - campaign_cards.slide_start);

		if (campaign_cards.shown_first != NONE && campaign_cards.first != campaign_cards.shown_first)
		{
			campaign_cards.slide_from = shift + (campaign_cards.first - campaign_cards.shown_first) * pitch;
			campaign_cards.slide_start = now;
			shift = campaign_cards.slide_from;
		}
		campaign_cards.shown_first = campaign_cards.first;
		for (slot = 0; slot < shown; slot++)
			slot_x[slot] = (short)(slot_x[slot] + (shift < 0.0f ? -(short)(-shift + 0.5f) : (short)(shift + 0.5f)));
	}
	for (slot = 0; slot < MISSION_SLOTS; slot++)
	{
		short mission = (short)(campaign_cards.first + slot);
		boolean chosen = mission == campaign_cards.chosen;
		struct widget_instance *big = mission_card(list, slot, TRUE);
		struct widget_instance *small = mission_card(list, slot, FALSE);

		visible_set(big, slot < shown && chosen);
		visible_set(small, slot < shown && !chosen);
		if (slot >= shown)
			continue;
		if (big)
		{
			big->horizontal_offset = slot_x[slot];
			big->vertical_offset = MISSION_CARDS_TOP;
		}
		if (small)
		{
			small->horizontal_offset = slot_x[slot];
			small->vertical_offset = (short)(MISSION_CARDS_TOP + (MISSION_BIG_HEIGHT - MISSION_SMALL_HEIGHT) / 2);
		}
		mission_card_show(chosen ? big : small, mission, chosen);
		if (small)
			small->alpha_modifier = MISSION_CARD_FAINT;
		/* (the focus on the chosen card, which is the larger one) */
		if (chosen && focus_on_card && big)
			list->focused_child = big;
	}
	visible_set(named(list, "arrow_previous", 0), campaign_cards.chosen > 0);
	visible_set(named(list, "arrow_next", 0), campaign_cards.chosen < count - 1);
	for (index = 0; index < MISSION_DOTS; index++)
	{
		char name[24];
		struct widget_instance *dot;

		snprintf(name, sizeof(name), "mission_dot_%d", index);
		dot = named(screen, name, 0);
		visible_set(dot, count > 1 && count <= MISSION_DOTS && index < count);
		if (dot)
		{
			dot->animation.current_frame_index = index == campaign_cards.chosen ? 1 : 0;
			dot->horizontal_offset = (short)((MISSION_DOTS - count) * MISSION_DOT_STEP / 2);
		}
	}
	mission_panel_show(list->parameters.list.extended_description, campaign_cards.chosen, count);
	if (!campaign_cards.custom && count > 0)
	{
		campaign_cards.viewed = TRUE;
		campaign_cards.viewed_level = campaign_cards.chosen;
	}
	if (count <= 0)
	{
		campaign_widen("Put Custom Edition campaign maps in your custom_maps folder to play them here.", text,
			MISSION_TEXT_LENGTH);
		menu_text_box_wrap(named(screen, "mission_empty", 0), text, 3);
	}
	visible_set(named(screen, "mission_empty", 0), count <= 0);
	menu_tabs_show(named(screen, "source_tabs", 0), campaign_cards.custom ? 1 : 0);
	campaign_text_set_ascii(named(screen, "prompt_select", 0),
		!campaign_cards.custom && count > 0 && campaign_cards.chosen == campaign.saved_level ?
		"%a-button RESUME" : "%a-button SELECT");
	profile_name_show(screen);
}

/* "port campaign previous", "next": the mission before or after the chosen one */
static boolean campaign_missions_step(struct widget_instance *widget, short step)
{
	short count = campaign_mission_count();
	short chosen = mission_cards_step(campaign_cards.chosen, step, count);
	struct widget_instance *list = widget;

	while (list && strcmp(list->name, "mission_cards"))
		list = list->parent;
	if (chosen == campaign_cards.chosen)
		return TRUE;
	campaign_cards.chosen = chosen;
	campaign_cards.first = mission_cards_first(chosen, count);
	/* (a card focused moves to the chosen one: the update) */
	if (list && mission_card_slot(list->focused_child) != NONE)
		list->focused_child = NULL;
	ui_play_audio_feedback_sound(SOUND_CURSOR);
	return TRUE;
}

/* "port campaign tab", "tab previous", "tab next": HALO's missions or CUSTOM's */
static boolean campaign_missions_source(struct widget_instance *widget, char const *how)
{
	boolean custom = !strcmp(how, "tab") ? sibling_index(widget) == 1 : !campaign_cards.custom;

	if (custom == campaign_cards.custom)
		return TRUE;
	campaign_cards.custom = custom;
	campaign_cards.shown_first = NONE;
	campaign_cards.chosen = custom ? 0 : campaign_cards.opening_level;
	campaign_cards.first = mission_cards_first(campaign_cards.chosen, campaign_mission_count());
	ui_play_audio_feedback_sound(SOUND_CURSOR);
	return TRUE;
}

/* "port campaign choose": a card's (its mission chosen first: a tap
focuses and uses it before the update has read it), or SELECT's; the
difficulties open for it (TRUE), or its saved game goes on (FALSE: none) */
static boolean campaign_missions_choose(struct widget_instance *widget, short controller)
{
	short count = campaign_mission_count();
	short slot = mission_card_slot(widget);
	short mission;

	if (slot != NONE && campaign_cards.first + slot < count)
		campaign_cards.chosen = (short)(campaign_cards.first + slot);
	mission = campaign_cards.chosen;
	if (mission < 0 || mission >= count)
		return campaign_fail();
	if (campaign_cards.custom)
	{
		short display_index = campaign_custom_display_index(mission);
		char const *map_name = custom_edition_maps_level_name(display_index);

		if (!map_name)
			return campaign_fail();
		campaign_cards.difficulty_level = NONE;
		campaign_cards.difficulty_display_index = display_index;
		main_set_map_name(map_name);
		main_defer_map_map_change();
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return TRUE;
	}
	if (mission == campaign.saved_level)
	{
		campaign_continue(controller);
		return FALSE;
	}
	campaign_cards.difficulty_level = mission;
	campaign_cards.difficulty_display_index = NONE;
	main_set_map_name(main_get_solo_level_name(mission));
	main_defer_map_map_change();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* ---------- Choose Difficulty */

/* its mission: the one chosen, else (a new profile's campaign) the map
set, else the first */
static boolean campaign_difficulty_initialize(struct widget_instance *list, short controller)
{
	struct player_profile profile;
	short level = main_get_solo_level_from_name(main_get_map_name());
	struct widget_instance *card;
	char name[24];

	if (campaign_profile(controller, &profile))
		campaign_levels_read(&profile);
	if (campaign_cards.difficulty_display_index == NONE || !custom_edition_maps_level_campaign(main_get_map_name()))
	{
		campaign_cards.difficulty_display_index = NONE;
		campaign_cards.difficulty_level = level != NONE ? level : 0;
	}
	campaign_cards.difficulty = (short)PIN(config_integer("game.campaign_difficulty"), 0, NUMBER_OF_DIFFICULTIES - 1);
	snprintf(name, sizeof(name), "difficulty_%d", campaign_cards.difficulty);
	if ((card = named(list, name, 0)) != NULL)
	{
		list->focused_child = card;
		list->parameters.list.selected_index = campaign_cards.difficulty;
	}
	return TRUE;
}

/* "port campaign difficulty update": the mission over the cards, each
card's badge, name, line and BEATEN; the difficulty chosen is the card
focused */
static void campaign_difficulty_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	wchar_t text[MISSION_TEXT_LENGTH], title[MISSION_TEXT_LENGTH];
	short level = campaign_cards.difficulty_level;
	byte beaten = 0;
	short index;
	struct widget_instance *child;

	if (campaign_cards.difficulty_display_index != NONE)
	{
		wchar_t const *map_name = custom_edition_maps_name(campaign_cards.difficulty_display_index);

		campaign_widen("CUSTOM MISSION: ", text, MISSION_TEXT_LENGTH);
		ustrncat(text, map_name ? map_name : L"", MISSION_TEXT_LENGTH - 1 - (long)ustrlen(text));
	}
	else if (level >= 0 && level < NUMBER_OF_SINGLE_PLAYER_LEVELS)
	{
		beaten = campaign.levels[level].beaten;
		campaign_mission_number(level, text, MISSION_TEXT_LENGTH);
		ustrncat(text, L": ", MISSION_TEXT_LENGTH - 1 - (long)ustrlen(text));
		campaign_level_title(level, TRUE, title, MISSION_TEXT_LENGTH);
		ustrncat(text, title, MISSION_TEXT_LENGTH - 1 - (long)ustrlen(text));
	}
	else
	{
		text[0] = 0;
	}
	campaign_text_set(named(screen, "difficulty_mission", 0), text);
	for (child = list->child, index = 0; child && index < NUMBER_OF_DIFFICULTIES; child = child->next, index++)
	{
		struct widget_instance *widget;

		if (strncmp(child->name, "difficulty_", 11))
			break;
		if (child == list->focused_child)
			campaign_cards.difficulty = index;
		if ((widget = named(child, "difficulty_badge", 0)) != NULL)
		{
			widget->animation.current_frame_index = index;
			visible_set(widget, tag_loaded('bitm', MISSION_BIG_BADGES) != NONE);
		}
		if ((widget = named(child, "difficulty_badge_shape", 0)) != NULL)
		{
			widget->animation.current_frame_index = 1;
			visible_set(widget, tag_loaded('bitm', MISSION_BIG_BADGES) == NONE);
		}
		if ((widget = named(child, "difficulty_name", 0)) != NULL)
			widget->parameters.text_box.string_list_index = index;
		/* (the game's own words, its difficulty menu's; ours where the map has none) */
		campaign_string("pc\\main_menu\\difficulty_select\\difficulty_descriptions", index, text,
			MISSION_TEXT_LENGTH);
		if (!text[0])
			campaign_string("pc\\main_menu\\campaign\\difficulty_blurbs", index, text, MISSION_TEXT_LENGTH);
		menu_text_box_wrap(named(child, "difficulty_blurb", 0), text, 6);
		visible_set(named(child, "difficulty_beaten", 0), (beaten >> index) & 1);
	}
	profile_name_show(screen);
}

/* "port campaign play": a card's difficulty, or (PLAY) the one chosen */
static boolean campaign_difficulty_play(struct widget_instance *widget, short controller)
{
	short difficulty = campaign_cards.difficulty;

	if (!strncmp(widget->name, "difficulty_", 11) && widget->name[11] >= '0' && widget->name[11] <= '3' &&
		!widget->name[12])
	{
		difficulty = (short)(widget->name[11] - '0');
	}
	{
		char value[8];

		snprintf(value, sizeof(value), "%d", PIN(difficulty, 0, NUMBER_OF_DIFFICULTIES - 1));
		if (!config_write("game.campaign_difficulty", value))
			platform_log("menus: could not keep the campaign's difficulty");
	}
	return difficulty_start(difficulty, controller);
}

/* ---------- the functions */

static boolean campaign_menu_event(struct widget_instance *widget, char const *name, short controller)
{
	if (!strcmp(name, "init"))
		return campaign_screen_initialize(controller);
	if (!strcmp(name, "continue"))
		return campaign_continue(controller);
	if (!strcmp(name, "missions"))
		return TRUE;
	if (!strcmp(name, "missions init"))
		return campaign_missions_initialize(widget, controller);
	if (!strcmp(name, "previous"))
		return campaign_missions_step(widget, -1);
	if (!strcmp(name, "next"))
		return campaign_missions_step(widget, 1);
	if (!strncmp(name, "tab", 3))
		return campaign_missions_source(widget, name);
	if (!strcmp(name, "choose"))
		return campaign_missions_choose(widget, controller);
	if (!strcmp(name, "difficulty init"))
		return campaign_difficulty_initialize(widget, controller);
	if (!strcmp(name, "play"))
		return campaign_difficulty_play(widget, controller);
	return FALSE;
}

static void campaign_menu_update(struct widget_instance *widget, char const *name)
{
	if (!strcmp(name, "update"))
		campaign_screen_update(widget);
	else if (!strcmp(name, "missions update"))
		campaign_missions_update(widget);
	else if (!strcmp(name, "difficulty update"))
		campaign_difficulty_update(widget);
}
