/*
SAVING_SCREENS.C

The stock maps' saving screens, which ask the player not to turn off their
Xbox console. Each is a wait for show: SAVE AND QUIT's ("Saving last
checkpoint... please don't turn off your Xbox console.") stays three seconds
and only then saves, and the Xbox menus' (a profile made, its settings or a
game type saved) stay three seconds after the save is done. A save takes a
few hundredths of a second here, so when a map's tags load
(scenario_tags_load) each button or screen that opens one does at once what
the screen does as it closes: SAVE AND QUIT saves and goes to the main menu,
a new profile goes on to choosing the difficulty. The save itself is the
game's, as before.

The port's own menus (port/assets/menus, built into tags of their own by
menu_tags.c just before) have the same screens, the PC version's, and its
profile manager's and Settings' too: those are skipped the same way, in
the widgets' handlers (the classic engine's) and in the model's behaviours'
(the menu engine's, display.menu_engine "new"), so that a new profile, a
profile's settings or a game type saved go on at once on either engine.
*/

#include "cseries.h"
#include "errors.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "interface/ui_widget_definitions.h"

/* the game's (port) */
char *tag_get_name(long tag_index);
/* menu_tags.c's: the menu engine's handlers of its widgets' behaviours */
long menu_tags_behaviour_count(void);
void *menu_tags_behaviour_handlers_at(long index, long *count, char const **name);

/* ---------- constants */

/* (ui_widget.c's event handler flags, and the event of a widget closing) */
enum
{
	_event_handler_open_widget_bit = 3,
	_event_handler_run_function_bit = 7,
};

#define EVENT_DELETED 25

/* ---------- structures */

/* a widget definition's event handler, as ui_widget.c has it */
struct saving_screen_event_handler
{
	long flags;
	short event_type;
	short function;
	struct tag_reference widget_tag;
	struct tag_reference sound_effect;
	char script[32];
};

typedef char verify_saving_screen_event_handler_size[
	sizeof(struct saving_screen_event_handler) == 0x48 ? 1 : -1];

/* ---------- globals */

/* the screens that only wait: SAVE AND QUIT's (and the one it leaves up
while the main menu loads), and the Xbox menus'; and the port's menus'
(menu_tags.c's tags, "pc\\" and their names in port/assets/menus): a
profile's settings or a game type saved, and a profile made from the main
menu (CAMPAIGN, MULTIPLAYER, SETTINGS) or the profile manager */
static char const *const saving_screen_names[] =
{
	"ui\\shell\\error\\warning_saving_file_screen_ingame",
	"ui\\shell\\error\\warning_saving_file_screen_fakery",
	"ui\\shell\\error\\warning_saving_file_screen",
	"ui\\shell\\main_menu\\new_campaign_saving_profile",
	"pc\\error\\warning_saving_file_screen",
	"pc\\main_menu\\new_campaign_saving_profile",
	"pc\\main_menu\\profile_manager\\multiplayer_saving_profile",
	"pc\\main_menu\\profile_manager\\pm_empty_saving_profile",
	"pc\\main_menu\\profile_manager\\settings_saving_profile",
	"pc\\main_menu\\settings\\settings_saving_profile",
};

/* their tags in the map loaded (NONE: it has not got it) */
static long saving_screen_tags[NUMBEROF(saving_screen_names)];

/* ---------- private code */

/* whether the widget definition is a saving screen */
static boolean saving_screen(
	long tag_index)
{
	long index;

	if (tag_index == NONE)
		return FALSE;
	for (index = 0; index < (long)NUMBEROF(saving_screen_tags); index++)
	{
		if (saving_screen_tags[index] == tag_index)
			return TRUE;
	}
	return FALSE;
}

/* the handler a saving screen runs as it closes, or NULL */
static struct saving_screen_event_handler const *saving_screen_closing(
	long tag_index)
{
	struct ui_widget_definition *definition = ui_widget_definition_get(tag_index);
	struct saving_screen_event_handler const *handlers = definition->event_handlers.address;
	long index;

	for (index = 0; index < definition->event_handlers.count; index++)
	{
		if (handlers[index].event_type == EVENT_DELETED)
			return &handlers[index];
	}
	return NULL;
}

/* A handler that opens a saving screen does what the screen does as it
closes instead: runs its function (where the handler runs none) and opens
what it opens, unless that is a saving screen too. FALSE when both run a
function, and the handler is left as it is. */
static boolean saving_screen_skip(
	struct saving_screen_event_handler *handler)
{
	long flags = handler->flags & ~FLAG(_event_handler_open_widget_bit);
	short function = handler->function;
	long widget = NONE;
	long screen = handler->widget_tag.index;
	long depth;

	for (depth = 0; depth < (long)NUMBEROF(saving_screen_tags) && saving_screen(screen); depth++)
	{
		struct saving_screen_event_handler const *closing = saving_screen_closing(screen);

		widget = screen = NONE;
		if (!closing)
			break;
		if (TEST_FLAG(closing->flags, _event_handler_run_function_bit) && closing->function)
		{
			if (TEST_FLAG(flags, _event_handler_run_function_bit) && function)
				return FALSE;
			SET_FLAG(flags, _event_handler_run_function_bit, TRUE);
			function = closing->function;
		}
		if (TEST_FLAG(closing->flags, _event_handler_open_widget_bit))
			screen = widget = closing->widget_tag.index;
	}
	if (saving_screen(widget))
		return FALSE;
	if (widget != NONE)
		SET_FLAG(flags, _event_handler_open_widget_bit, TRUE);
	handler->flags = flags;
	handler->function = function;
	handler->widget_tag.index = widget;
	return TRUE;
}

/* each of the handlers that opens a saving screen skips it */
static void saving_screens_skip_all(
	struct saving_screen_event_handler *handlers,
	long count,
	char const *name)
{
	long index;

	for (index = 0; handlers && index < count; index++)
	{
		if (TEST_FLAG(handlers[index].flags, _event_handler_open_widget_bit) &&
			saving_screen(handlers[index].widget_tag.index) &&
			!saving_screen_skip(&handlers[index]))
		{
			error(_error_silent, "saving screens: %s keeps its saving screen", name);
		}
	}
}

/* ---------- public code */

void saving_screens_tags_loaded(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	long index;
	boolean any = FALSE;

	for (index = 0; index < (long)NUMBEROF(saving_screen_names); index++)
	{
		saving_screen_tags[index] = tag_loaded(UI_WIDGET_DEFINITION_TAG, saving_screen_names[index]);
		any |= saving_screen_tags[index] != NONE;
	}
	if (!any)
		return;
	tag_iterator_new(&iterator, UI_WIDGET_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct ui_widget_definition *definition = ui_widget_definition_get(tag_index);

		saving_screens_skip_all(definition->event_handlers.address, definition->event_handlers.count,
			tag_get_name(tag_index));
	}
	/* (the menu engine's: the same handlers, made from the model) */
	for (index = 0; index < menu_tags_behaviour_count(); index++)
	{
		char const *name;
		long count;
		struct saving_screen_event_handler *handlers = menu_tags_behaviour_handlers_at(index, &count, &name);

		saving_screens_skip_all(handlers, count, name);
	}
}
