/*
SETTINGS_SEARCH_MENU.INC

Settings' search, the menu engine's alone (display.menu_engine "new";
docs/menu_engine.md, "Deliberate differences"; tools/port_settings.py's
SETTINGS_SEARCH), included by menu_functions.c, whose Settings it searches:
menu_search.c ranks the rows of every tab and page against what is typed,
menu_engine.c makes the rows found in the search's screen (each its own
page's real row, which the functions here and in menu_functions.c run as on
its page) and lays them out under the way to them; this takes what is typed.

- On any Settings screen (a tab or a page: not Key Bindings' nor Button
  Remapping's, which take keys themselves), letters and digits typed (but Q
  and E, the tabs' keys), Ctrl+F or / open the search ("port settings
  search", by a Y posted to the screen, so that it opens as a screen's press
  opens one): its screen in the history, so that leaving it goes back to the
  tab or page and the row it was opened from. A controller's Y opens it too,
  with the on-screen keyboard (its title SEARCH, FIND A SETTING over its keys); the field at the
  tabs' left (A, a click, a tap) with the field typed into (a touchscreen's
  system keyboard up). On Settings the keyboard's W, A, S, D, F, R and L are
  letters (halo_keyboard_settings_keys), the arrows move the focus.
- On the search's screen what is typed is in the field across the tabs' row,
  the rows found changing as each letter is typed. Enter keeps it, the focus
  on the first row found; the arrows (or a click) take the focus off the
  field to the rows, which change their settings there, as on their pages,
  kept as a tab's are (settings_pending) until an OK; letters typed with the
  focus on a row type on in the field. A row that opens a page opens it, and
  back from it the search is as it was, the focus on that row. The panel says
  what the row with the focus is, as on its page.
- Esc, B, Back or BACK ("port settings search back") clear it and go back
  to where it was opened, the changes made in it kept; OK ("port settings
  keep") writes every change, as a page's OK, and goes back too. Esc while a
  slider's number is typed into forgets the number, the search kept.
*/

/* the most letters typed (the on-screen keyboard's, its buffer's: 64 bytes) */
#define SETTINGS_SEARCH_LENGTH 31
#define SETTINGS_SEARCH_NAME \
	"pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\settings_search\\settings_search_screen"

boolean virtual_keyboard_port_launch(wchar_t *text_buffer, word buffer_size, wchar_t const *title,
	wchar_t const *caption);
boolean virtual_keyboard_active(void);
boolean virtual_keyboard_last_exit_saved_text(void);

static struct
{
	/* what is typed: the rows found are its */
	char typed[SETTINGS_SEARCH_LENGTH + 1];
	/* letters typed on a Settings screen, the search to open with them (its Y
	posted) */
	char starting[SETTINGS_SEARCH_LENGTH + 1];
	boolean starting_posted;
	/* as the search's screen is made: typed into at once (a keyboard's, a
	touchscreen's), or the on-screen keyboard brought up (a controller's) */
	boolean begin_typing, begin_keyboard;
	/* the on-screen keyboard up for it, and what it types */
	boolean keyboard_up;
	wchar_t keyboard_text[SETTINGS_SEARCH_LENGTH + 1];
	/* the search's screen last seen (made again: back from a page a row
	found opened), and the row found with the focus, to put it back on */
	struct widget_instance *screen;
	unsigned long screen_time;
	long focused;
	/* Enter kept what is typed: the focus to the first row found */
	boolean focus_first;
	/* the search left: the page made again keeps what it kept as it first
	opened, for its Cancel */
	boolean returning;
	/* the model's screen whose rows a condition's keys name (a row found's:
	its own page's, settings_condition_term) */
	struct halo_menu_model_screen const *condition_screen;
} settings_search = { "", "", FALSE, FALSE, FALSE, FALSE, { 0 }, NULL, 0, NONE, FALSE, FALSE, NULL };

static boolean settings_search_on(void)
{
	return menu_engine_looks();
}

/* a key typed that starts or goes on with a search: a letter or a digit */
static boolean settings_search_letter(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9');
}

/* the letters typed this frame added to text (Ctrl+F and / open it with
none; with tabs, not Q and E first, the tabs' keys; on the search's screen,
Backspace takes the last off, which is not Esc there:
halo_keyboard_settings_keys): whether any key opens it, or goes on with it */
static boolean settings_search_keys_read(char *text, size_t size, boolean tabs)
{
	struct key_stroke key;
	boolean found = FALSE;

	while (input_get_key(&key))
	{
		boolean control = TEST_FLAG(key.modifier_flags, KEY_MODIFIER_CONTROL_BIT);
		char character = key.ascii_code;
		size_t length = strlen(text);

		if (control && key.key_code == _key_f)
			found = TRUE;
		else if (!tabs && key.key_code == _key_backspace)
		{
			if (length)
				text[length - 1] = 0;
			found = TRUE;
		}
		else if (!control && character == '/')
			found = TRUE;
		else if (!control && settings_search_letter(character) && !(tabs && !text[0] &&
			(character == 'q' || character == 'Q' || character == 'e' || character == 'E')))
		{
			if (length + 1 < size)
			{
				text[length] = character;
				text[length + 1] = 0;
			}
			found = TRUE;
		}
	}
	return found;
}

/* a tab's or a page's each frame: its keys letters, and letters typed open
the search with them */
static void settings_search_keys(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);

	if (!settings_search_on() || text_field_editing(NULL) || virtual_keyboard_active() || controls_page(screen) ||
		pad_editor_page(screen))
	{
		return;
	}
	halo_keyboard_settings_keys(FALSE);
	if (settings_search_keys_read(settings_search.starting, sizeof(settings_search.starting), TRUE) &&
		!settings_search.starting_posted)
	{
		settings_search.starting_posted = TRUE;
		ui_widget_port_post_button(0, BUTTON_Y);
	}
}

/* the tabs' row of a tab: the menu engine's, with the search's field
(tabs_search), in the place of the classic engine's; the field's magnifying
glass (a keyboard's, a touchscreen's) or Y (a controller's), and its word
where it is wide enough */
static void settings_search_bars_show(struct widget_instance *list)
{
	struct widget_instance *tabs = settings_part(list, "tabs");
	struct widget_instance *search_tabs = settings_part(list, "search tabs");
	struct widget_instance *field = settings_part(search_tabs, "search field");
	struct halo_menu_model const *model = menu_engine_model();
	boolean on = settings_search_on();
	boolean keys = halo_keyboard_prompts() || ui_widget_port_touch_menus();

	if (!search_tabs)
		return;
	if (tabs)
		tabs->visible = !on;
	search_tabs->visible = on;
	if (!on || !field)
		return;
	/* (the focus coming up into the row on a tab, as into the classic
	engine's, not the field: left of the first tab is the field) */
	if (list->focused_child != search_tabs && search_tabs->focused_child == field && field->next)
	{
		search_tabs->focused_child = field->next;
		search_tabs->parameters.list.selected_index = 1;
	}
	visible_set(settings_part(field, "search icon"), keys);
	visible_set(settings_part(field, "search prompt"), !keys);
	visible_set(settings_part(field, "search word"),
		model && ui_widget_port_wide_extra() >= model->geometry.search_word_extra);
}

/* the field's text typed into, kept (Enter: the focus to the first row
found) */
static void settings_search_done(char const *text)
{
	snprintf(settings_search.typed, sizeof(settings_search.typed), "%s", text);
	settings_search.focus_first = TRUE;
}

/* the field typed into, the focus on it, the letters typed this frame (as
the search opened) added */
static void settings_search_type(struct widget_instance *list)
{
	struct widget_instance *field = settings_part(list, "search field");

	if (!field)
		return;
	if (text_field_editing(NULL))
		text_field_end(TRUE);
	settings_search_keys_read(settings_search.typed, sizeof(settings_search.typed), FALSE);
	list_focus_child(list, field);
	text_field_begin(field, settings_search.typed, SETTINGS_SEARCH_LENGTH, settings_search_done);
	text_field.enter_ends = TRUE;
}

/* the on-screen keyboard brought up for it, with what is typed */
static void settings_search_keyboard(void)
{
	short index;

	if (text_field_editing(NULL))
		text_field_end(TRUE);
	for (index = 0; settings_search.typed[index] && index < SETTINGS_SEARCH_LENGTH; index++)
		settings_search.keyboard_text[index] = (wchar_t)(unsigned char)settings_search.typed[index];
	settings_search.keyboard_text[index] = 0;
	settings_search.keyboard_up = virtual_keyboard_port_launch(settings_search.keyboard_text,
		(word)sizeof(settings_search.keyboard_text), L"SEARCH",
		L"FIND A SETTING");
}

/* whether the keyboard's (and mouse's) or a touchscreen's, not a
controller's, typed last: the field typed into, else the on-screen keyboard */
static boolean settings_search_typed_here(void)
{
	return halo_keyboard_prompts() || ui_widget_port_touch_menus();
}

/* "port settings search" (Y on a Settings screen, the letters typed there,
A or a click on the tabs' field): the search's screen opened in the history
from the row with the focus (back returns to it), the changes kept; on the
search's screen, typed into again. Nothing on the classic engine */
static boolean settings_search_open(struct widget_instance *widget, boolean *widget_deleted)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *list = named(screen, "options_menu", 0);
	struct widget_instance *from = list && list->focused_child ? list->focused_child : widget;

	if (!settings_search_on())
	{
		settings_search.starting[0] = 0;
		settings_search.starting_posted = FALSE;
		return TRUE;
	}
	if (menu_engine_search_screen(screen))
	{
		settings_search.starting[0] = 0;
		settings_search.starting_posted = FALSE;
		if (list && settings_search_typed_here())
			settings_search_type(list);
		else
			settings_search_keyboard();
		return TRUE;
	}
	settings_each(screen, setting_stash);
	snprintf(settings_search.typed, sizeof(settings_search.typed), "%s", settings_search.starting);
	settings_search.begin_keyboard = !settings_search.typed[0] && !settings_search_typed_here();
	settings_search.begin_typing = !settings_search.begin_keyboard;
	settings_search.starting[0] = 0;
	settings_search.starting_posted = FALSE;
	settings_search.focused = NONE;
	settings_search.screen = NULL;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return ui_widget_port_open(from, SETTINGS_SEARCH_NAME, widget_deleted);
}

/* "port settings search edit" (the search's field's A, or a click on it):
typed into (again: a press while it is typed into keeps what is typed, the
focus on the first row found), or the on-screen keyboard */
static void settings_search_edit(struct widget_instance *widget)
{
	struct widget_instance *list = named(screen_of(widget), "options_menu", 0);
	struct widget_instance *field = list ? settings_part(list, "search field") : NULL;

	if (!field)
		return;
	if (text_field_editing(field))
	{
		if (!ui_widget_port_touch_menus())
			text_field_end(TRUE);
	}
	else if (settings_search_typed_here())
		settings_search_type(list);
	else
		settings_search_keyboard();
}

/* the search left (BACK, Esc, OK): what is typed forgotten */
static void settings_search_left(void)
{
	settings_search.typed[0] = 0;
	settings_search.screen = NULL;
	settings_search.focused = NONE;
	settings_search.returning = TRUE;
}

/* "port settings search back" (Esc, B, Back, BACK): what is typed cleared,
the changes kept, back to where the search was opened; FALSE (the search
kept) while a slider's number is typed into, which is forgotten */
static boolean settings_search_back(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct widget_instance *list = named(screen, "options_menu", 0);
	struct widget_instance *field = list ? settings_part(list, "search field") : NULL;

	if (text_field_editing(NULL) && !text_field_editing(field))
	{
		text_field_end(FALSE);
		return FALSE;
	}
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	settings_each(screen, setting_stash);
	settings_search_left();
	return TRUE;
}

/* whether a row may be found: its conditions hold (its platform's, the
game's state's, its page's other rows' values: settings_condition_term), and
its page's profile is there */
static int settings_search_shown(void *context, struct halo_menu_model_screen const *screen,
	struct halo_menu_model_row const *row)
{
	struct widget_instance *list = context;
	int shown;

	if (screen->profile && !player_ui_get_edit_player_profile())
		return FALSE;
	if (!row->when)
		return TRUE;
	settings_search.condition_screen = screen;
	shown = settings_condition(list, row->when);
	settings_search.condition_screen = NULL;
	return shown;
}

/* a term "<key>=<value>" of a row found's condition: its page's row of the
key's setting at the value (the change kept, else config.toml's), the row
not being in the search's list */
static boolean settings_search_term(char const *key, unsigned long key_length, char const *value,
	unsigned long value_length)
{
	struct halo_menu_model_screen const *screen = settings_search.condition_screen;
	struct halo_menu_model const *model = menu_engine_model();
	char text[96];
	long index;

	for (index = 0; screen && model && index < screen->row_count; index++)
	{
		struct halo_menu_model_row const *row = &model->rows[screen->first_row + index];

		if (row->setting && strlen(row->key) == key_length && !strncmp(row->key, key, key_length))
		{
			return settings_value(row->setting, text, sizeof(text)) && strlen(text) == value_length &&
				!strncmp(text, value, value_length);
		}
	}
	return FALSE;
}

/* the rows found's conditions, as a page's are (settings_rows_conditions):
each shown while its when holds, dimmed while its enabled does not */
static void settings_search_conditions(struct widget_instance *list)
{
	long index;
	struct widget_instance *row;

	for (index = 0; (row = menu_engine_search_found(index)) != NULL; index++)
	{
		struct halo_menu_model_row const *model_row = menu_engine_model_row_of(row);
		char name[80];

		if (!model_row)
			continue;
		settings_search.condition_screen = menu_engine_search_found_screen(index);
		if (model_row->when && settings_condition_runs(model_row->when))
			row->visible = settings_condition(list, model_row->when);
		if (model_row->enabled)
		{
			snprintf(name, sizeof(name), "%s_spinner", model_row->key);
			row_dim(row, named(row, name, 0), !settings_condition(list, model_row->enabled));
		}
		settings_search.condition_screen = NULL;
	}
}

/* the words under the rows found: what to type for none typed, that none
match (and where a word typed is found elsewhere in the game: a hint row of
the search's model), or how many more than are shown match */
static void settings_search_note(struct widget_instance *list, long count, long shown)
{
	struct widget_instance *note = settings_part(list, "search note");
	long matched = menu_engine_search_matched();
	wchar_t words[ROW_TEXT_LENGTH];
	char text[ROW_TEXT_LENGTH];

	if (!note)
		return;
	if (!settings_search.typed[0])
		snprintf(text, sizeof(text), "Type a setting's name, a word of what it does, or a choice.");
	else if (!shown)
		snprintf(text, sizeof(text), "No settings match \"%s\"", settings_search.typed);
	else if (matched > count)
		snprintf(text, sizeof(text), "%ld more match: type more to narrow them.", matched - count);
	else
		text[0] = 0;
	note->visible = text[0] != 0;
	halo_menus_utf16(text, (unsigned short *)words, NUMBEROF(words));
	text_set(note, words);
}

/* the search's screen each frame (its list's "port settings page"): the
on-screen keyboard's text taken, what is typed shown and the rows found
for it made (menu_engine.c), their crumbs and conditions, the focus kept on
a row shown, and the panel */
static void settings_search_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *field = settings_part(list, "search field");
	boolean made = screen != settings_search.screen || screen->creation_time != settings_search.screen_time;
	boolean typing;
	long count, index, shown = 0;
	struct widget_instance *child;

	if (made)
	{
		settings_search.screen = screen;
		settings_search.screen_time = screen->creation_time;
	}
	if (!settings_search_on())
		return;
	halo_keyboard_settings_keys(TRUE);
	/* (the on-screen keyboard closed: its DONE's text searched for; B, or
	nothing to search for, back to where the search was opened) */
	if (settings_search.keyboard_up && !virtual_keyboard_active())
	{
		settings_search.keyboard_up = FALSE;
		if (virtual_keyboard_last_exit_saved_text())
		{
			for (index = 0; settings_search.keyboard_text[index] && index < SETTINGS_SEARCH_LENGTH; index++)
			{
				wchar_t character = settings_search.keyboard_text[index];

				settings_search.typed[index] = character >= ' ' && character <= '~' ? (char)character : ' ';
			}
			settings_search.typed[index] = 0;
			settings_search.focus_first = TRUE;
		}
		if (!settings_search.typed[0])
			ui_widget_port_post_button(controller_of(screen), BUTTON_B);
	}
	if (settings_search.begin_keyboard)
	{
		settings_search.begin_keyboard = FALSE;
		settings_search_keyboard();
	}
	if (settings_search.begin_typing)
	{
		settings_search.begin_typing = FALSE;
		settings_search_type(list);
	}
	/* (letters typed with the focus on a row: typed on in the field) */
	if (!text_field_editing(NULL) && !virtual_keyboard_active() &&
		settings_search_keys_read(settings_search.typed, sizeof(settings_search.typed), FALSE))
	{
		settings_search_type(list);
	}
	typing = field && text_field_editing(field);
	/* (the focus taken off the field, by the arrows or a click: what is
	typed kept) */
	if (typing && list->focused_child != field)
	{
		text_field_end(TRUE);
		settings_search.focus_first = FALSE;
		typing = FALSE;
	}
	if (field)
		text_field_show(field, settings_search.typed, typing);
	if (typing)
		snprintf(settings_search.typed, sizeof(settings_search.typed), "%s", text_field.text);
	count = menu_engine_search_find(list, settings_search.typed, settings_search_shown, list);
	settings_search_conditions(list);
	for (index = 0; index < count; index++)
		shown += menu_engine_search_found(index)->visible != FALSE;
	/* (each group's crumb: the way to its rows) */
	for (index = 0, child = list->child; child; child = child->next)
	{
		if (settings_is_part(child, "search crumb"))
		{
			char const *crumb = menu_engine_search_crumb(index++);
			wchar_t words[ROW_TEXT_LENGTH];

			halo_menus_utf16(crumb ? crumb : "", (unsigned short *)words, NUMBEROF(words));
			text_set(named(child, "crumb_title", 0), words);
		}
	}
	settings_search_note(list, count, shown);
	/* (the focus: back on the row found it was on, as the screen is made
	again; on the first row found, Enter having kept what is typed; off a
	row gone or hidden, to the first shown, else the field) */
	if (made && settings_search.focused != NONE && menu_engine_search_found(settings_search.focused))
		list_focus_child(list, menu_engine_search_found(settings_search.focused));
	else if (settings_search.focus_first && !typing)
	{
		settings_search.focus_first = FALSE;
		for (index = 0; index < count && !menu_engine_search_found(index)->visible; index++)
		{
		}
		list_focus_child(list, index < count ? menu_engine_search_found(index) : field);
	}
	if (!list->focused_child || !list->focused_child->visible)
	{
		for (index = 0; index < count && !menu_engine_search_found(index)->visible; index++)
		{
		}
		list_focus_child(list, index < count && !typing ? menu_engine_search_found(index) : field);
	}
	settings_search.focused = NONE;
	for (index = 0; index < count; index++)
	{
		if (list->focused_child == menu_engine_search_found(index))
			settings_search.focused = index;
	}
	settings_each(screen, setting_stash);
	settings_segments_show(list);
	settings_sliders_show(list);
	settings_panel_show(list, NONE);
}

/* the search's words for the panel (its screen's in the model: what it
does, and how to open it, after a "|"), NULL if the model has none */
static wchar_t const *settings_search_about(void)
{
	struct halo_menu_model const *model = menu_engine_model();
	long index;

	for (index = 0; model && index < model->screen_count; index++)
	{
		struct halo_menu_model_screen const *screen = &model->screens[index];

		if (screen->layout && !strcmp(screen->layout, "search"))
			return settings_model_text(screen->about, 1);
	}
	return NULL;
}

/* every spinner of a setting of the rows found (settings_each's, on the
search's screen): each row's in its own page's model */
static void settings_search_each(boolean (*visit)(struct widget_instance *spinner,
	struct pc_menu_setting *setting))
{
	struct widget_instance *row;
	long index;

	for (index = 0; (row = menu_engine_search_found(index)) != NULL; index++)
	{
		struct halo_menu_model_row const *model_row = menu_engine_model_row_of(row);
		struct pc_menu_setting *setting = settings_row_setting(model_row);
		struct widget_instance *spinner;
		char name[80];

		if (!setting)
			continue;
		settings_widget_name(name, sizeof(name), "", model_row->key, "_spinner");
		spinner = named(row, name, 0);
		if (spinner && spinner->type == 2 && !spinner_item(spinner))
			visit(spinner, setting);
	}
}

/* (a page made again as the search is left: whether it was, once) */
static boolean settings_search_returned(void)
{
	boolean returned = settings_search.returning;

	settings_search.returning = FALSE;
	return returned;
}

/* the panel's words for the search's field: where a word typed that finds
no row is in the game (a hint row of the search's model), else what the
search does */
static wchar_t const *settings_search_field_help(struct widget_instance *list)
{
	struct halo_menu_model const *model = menu_engine_model();
	struct halo_menu_model_screen const *screen = menu_engine_model_screen_of(list);
	struct halo_menu_model_row const *hint;

	if (menu_engine_search_screen(list) && settings_search.typed[0] && !menu_engine_search_found(0) &&
		(hint = menu_search_hint(model, screen, settings_search.typed)) != NULL)
	{
		return settings_model_text(hint->help, 1);
	}
	return settings_search_about();
}
