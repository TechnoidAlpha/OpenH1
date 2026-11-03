#include "harness.h"

/* Controls Setup's rows (port/linux/game/menu_functions.c): a control's two
bindings, the one set chosen by left and right, by a click on it, or by the
pointer over it (ui_widget.c's ui_widget_port_take_click and take_point);
the key taken or cleared, a game control keeping one (control_capture_poll) */

#include <wchar.h>

#include <strings.h>

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define ROW_TEXT_LENGTH 64
#define SOUND_ERROR 4
#define _stricmp strcasecmp

struct widget_instance
{
	long definition_tag_index;
	char const *name;
	short horizontal_offset;
	boolean visible;
	struct widget_instance *parent, *child, *next, *focused_child;
	wchar_t shown[ROW_TEXT_LENGTH];
	union
	{
		struct
		{
			struct widget_instance *extended_description;
		} list;
		struct
		{
			short string_list_index;
		} text_box;
	} parameters;
};

/* the fake world: the pointer's latest click and place, the wide layout's
move (of the list's definition), the captures begun */
static boolean clicked, pointed;
static short click_x, point_x;
static short wide_offset;
static int captures;
/* the capture's result to come (1 a key, 2 Delete) and the key's name, the
clock, the error sounds played */
static int capture_result;
static char const *capture_name = "";
static unsigned long now_ms = 1000;
static int error_sounds;

static int platform_binding_capture_poll(int *input)
{
	int result = capture_result;

	capture_result = 0;
	*input = 7;
	return result;
}

static void halo_input_name(int input, char *name, unsigned int size)
{
	snprintf(name, size, "%s", input == 7 ? capture_name : "");
}

static unsigned long system_milliseconds(void)
{
	return now_ms;
}

static void ui_play_audio_feedback_sound(short sound)
{
	if (sound == SOUND_ERROR)
		error_sounds++;
}

static boolean ui_widget_port_take_click(short *x, short *y)
{
	if (!clicked)
		return FALSE;
	clicked = FALSE;
	*x = click_x;
	*y = 0;
	return TRUE;
}

static boolean ui_widget_port_take_point(short *x, short *y)
{
	if (!pointed)
		return FALSE;
	pointed = FALSE;
	*x = point_x;
	*y = 0;
	return TRUE;
}

static short ui_widget_port_wide_offset(long definition_index)
{
	return definition_index == 1 ? wide_offset : 0;
}

static void platform_binding_capture_begin(void)
{
	captures++;
}

static void halo_input_shown_name(char const *binding, char *shown, unsigned int size)
{
	snprintf(shown, size, "%s", binding);
}

static struct widget_instance *named_in(struct widget_instance *widget, char const *name, long *nth)
{
	struct widget_instance *child;

	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *found;

		if (!strcmp(child->name, name) && (*nth)-- == 0)
			return child;
		if ((found = named_in(child, name, nth)))
			return found;
	}
	return NULL;
}

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	return widget ? named_in(widget, name, &nth) : NULL;
}

static void text_set(struct widget_instance *text_box, wchar_t const *text)
{
	if (text_box)
		wcsncpy(text_box->shown, text, ROW_TEXT_LENGTH - 1);
}

#define screen_of(widget) (widget)
#define controls_group(screen) ((void)(screen), (short)0)
#define focus_row(list, row) ((void)0)

#include "under_test.inc"

/* the screen's list: three rows of group 0 (MOVE FORWARD, MOVE BACKWARD,
STRAFE LEFT), at x 54, each a label and its two bindings (at 200 and 356) */
#define ROWS 3
static struct widget_instance list, rows[ROWS], labels[ROWS], bindings[ROWS][CONTROL_BINDINGS];
static char const *const row_names[ROWS] = { "op_command_1", "op_command_2", "op_command_3" };

static void setup(void)
{
	short index, slot;

	memset(&list, 0, sizeof(list));
	list.name = "options_menu";
	list.definition_tag_index = 1;
	list.child = &rows[0];
	for (index = 0; index < ROWS; index++)
	{
		rows[index] = (struct widget_instance){ .name = row_names[index], .horizontal_offset = 54, .parent = &list,
			.child = &labels[index], .next = index + 1 < ROWS ? &rows[index + 1] : NULL };
		labels[index] = (struct widget_instance){ .name = "command_label", .parent = &rows[index],
			.next = &bindings[index][0] };
		bindings[index][0] = (struct widget_instance){ .name = "command_binding", .horizontal_offset = 200,
			.parent = &rows[index], .next = &bindings[index][1] };
		bindings[index][1] = (struct widget_instance){ .name = "command_binding", .horizontal_offset = 356,
			.parent = &rows[index] };
		for (slot = 0; slot < CONTROL_BINDINGS; slot++)
			snprintf(controls_screen.bindings[index][slot], CONTROL_NAME_LENGTH, "%c%d", 'A' + index, slot);
	}
	list.focused_child = &rows[1];
	controls_screen.slot = 0;
	controls_screen.capturing_control = NONE;
	clicked = pointed = FALSE;
	wide_offset = 0;
	captures = 0;
}

static boolean shows(short row, short slot, wchar_t const *text)
{
	return !wcscmp(bindings[row][slot].shown, text);
}

/* a click on a row (it is focused and pressed with A, as ui_widget.c does)
at x on the screen: the binding set */
static void click(short row, short x)
{
	list.focused_child = &rows[row];
	clicked = TRUE;
	click_x = x;
	CHECK(control_capture_begin(&bindings[row][0]), "a click on row %d began nothing", row);
}

/* a binding set: the capture begun on it, then a key taken (result 1) or
Delete (2) */
static void set_binding(short control, short slot, int result, char const *name)
{
	controls_screen.capturing_control = control;
	controls_screen.capturing_slot = slot;
	capture_result = result;
	capture_name = name;
	control_capture_poll();
	CHECK(controls_screen.capturing_control == NONE, "the capture did not end");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a click on the second binding sets it, a click on the first (or the
	label) the first, whichever left and right chose before */
	if (!strcmp(case_name, "click"))
	{
		setup();
		click(1, 54 + 356 + 10);
		CHECK(controls_screen.capturing_control == 1 && controls_screen.capturing_slot == 1,
			"a click on the second binding set control %d's binding %d", controls_screen.capturing_control,
			controls_screen.capturing_slot);
		CHECK(captures == 1, "%d captures begun", captures);
		controls_update(&list);
		CHECK(shows(1, 1, L"PRESS A KEY") && shows(1, 0, L"B0"), "while setting the second: %ls | %ls",
			bindings[1][0].shown, bindings[1][1].shown);
		controls_screen.capturing_control = NONE;
		click(2, 54 + 200 + 5);
		CHECK(controls_screen.capturing_control == 2 && controls_screen.capturing_slot == 0,
			"a click on the first binding set binding %d", controls_screen.capturing_slot);
		controls_screen.capturing_control = NONE;
		controls_screen.slot = 1;
		click(0, 54 + 20);
		CHECK(controls_screen.capturing_slot == 0, "a click on the label set binding %d", controls_screen.capturing_slot);
		return 0;
	}

	/* A from the keyboard or a controller (no click): the binding left and
	right chose */
	if (!strcmp(case_name, "keys"))
	{
		setup();
		controls_screen.slot = 1;
		CHECK(control_capture_begin(&rows[1]), "A began nothing");
		CHECK(controls_screen.capturing_control == 1 && controls_screen.capturing_slot == 1,
			"A on the second binding set binding %d", controls_screen.capturing_slot);
		controls_screen.capturing_control = NONE;
		controls_screen.slot = 0;
		CHECK(control_capture_begin(&rows[1]), "A began nothing");
		CHECK(controls_screen.capturing_slot == 0, "A on the first binding set binding %d",
			controls_screen.capturing_slot);
		return 0;
	}

	/* the pointer over a binding of the focused row marks it, as left and
	right do; it moves nothing while a binding is being set */
	if (!strcmp(case_name, "hover"))
	{
		setup();
		controls_update(&list);
		CHECK(shows(1, 0, L"> B0 <") && shows(1, 1, L"B1"), "at first: %ls | %ls", bindings[1][0].shown,
			bindings[1][1].shown);
		pointed = TRUE;
		point_x = 54 + 356 + 100;
		controls_update(&list);
		CHECK(controls_screen.slot == 1, "over the second binding: slot %d", controls_screen.slot);
		CHECK(shows(1, 0, L"B0") && shows(1, 1, L"> B1 <"), "over the second: %ls | %ls", bindings[1][0].shown,
			bindings[1][1].shown);
		pointed = TRUE;
		point_x = 54 + 210;
		controls_update(&list);
		CHECK(controls_screen.slot == 0 && shows(1, 0, L"> B0 <"), "over the first binding: slot %d",
			controls_screen.slot);
		/* (no new place: the keys' choice stays) */
		controls_screen.slot = 1;
		controls_update(&list);
		CHECK(controls_screen.slot == 1, "a still pointer took the keys' choice back");
		controls_screen.capturing_control = 1;
		controls_screen.capturing_slot = 1;
		pointed = TRUE;
		point_x = 54 + 210;
		controls_update(&list);
		CHECK(controls_screen.slot == 1 && shows(1, 1, L"PRESS A KEY"), "the pointer moved the binding being set");
		return 0;
	}

	/* the wide layout moves the rows: the bindings are where they are drawn */
	if (!strcmp(case_name, "wide"))
	{
		setup();
		wide_offset = 80;
		click(1, 54 + 356 + 10);
		CHECK(controls_screen.capturing_slot == 0, "a click left of the moved second binding set binding %d",
			controls_screen.capturing_slot);
		controls_screen.capturing_control = NONE;
		click(1, 80 + 54 + 356 + 10);
		CHECK(controls_screen.capturing_slot == 1, "a click on the moved second binding set binding %d",
			controls_screen.capturing_slot);
		return 0;
	}

	/* a game control keeps a key: its only key is not taken for another
	control, nor cleared (the error's sound, the help's why); with another
	key it is, as a control that is not the game's (SHOW SCORES, 15) */
	if (!strcmp(case_name, "keep-a-key"))
	{
		static struct widget_instance help = { .name = "help" };

		setup();
		list.parameters.list.extended_description = &help;
		controls_screen.bindings[0][1][0] = 0;
		/* MOVE FORWARD's only key, for MOVE BACKWARD */
		set_binding(1, 0, 1, "A0");
		CHECK(!strcmp(controls_screen.bindings[0][0], "A0") && !strcmp(controls_screen.bindings[1][0], "B0"),
			"the only key was taken: %s | %s", controls_screen.bindings[0][0], controls_screen.bindings[1][0]);
		CHECK(error_sounds == 1, "%d error sounds", error_sounds);
		controls_update(&list);
		CHECK(list.parameters.list.extended_description->parameters.text_box.string_list_index == 3,
			"the help is %d, not why", list.parameters.list.extended_description->parameters.text_box.string_list_index);
		now_ms += 5000;
		controls_update(&list);
		CHECK(list.parameters.list.extended_description->parameters.text_box.string_list_index == 1,
			"the why stayed: %d", list.parameters.list.extended_description->parameters.text_box.string_list_index);
		/* cleared */
		set_binding(0, 0, 2, "");
		CHECK(!strcmp(controls_screen.bindings[0][0], "A0") && error_sounds == 2, "the only key was cleared");
		/* moved to its own other binding */
		set_binding(0, 1, 1, "A0");
		CHECK(!controls_screen.bindings[0][0][0] && !strcmp(controls_screen.bindings[0][1], "A0") && error_sounds == 2,
			"the only key did not move along: %s | %s", controls_screen.bindings[0][0], controls_screen.bindings[0][1]);
		/* given another key, the first may go */
		set_binding(0, 0, 1, "Z");
		set_binding(1, 1, 1, "A0");
		CHECK(!strcmp(controls_screen.bindings[1][1], "A0") && !controls_screen.bindings[0][1][0] &&
			!strcmp(controls_screen.bindings[0][0], "Z") && error_sounds == 2, "a second key did not free the first");
		/* an empty binding cleared: nothing changes, nothing refused */
		set_binding(0, 1, 2, "");
		CHECK(!strcmp(controls_screen.bindings[0][0], "Z") && error_sounds == 2, "an empty binding's clearing refused");
		/* a game control with no key at all (an old config's) blocks nothing */
		controls_screen.bindings[2][0][0] = controls_screen.bindings[2][1][0] = 0;
		set_binding(1, 0, 1, "Q");
		CHECK(!strcmp(controls_screen.bindings[1][0], "Q") && error_sounds == 2, "a control with no key blocked it");
		/* not the game's: its only key taken, and cleared */
		snprintf(controls_screen.bindings[15][0], CONTROL_NAME_LENGTH, "%s", "Tab");
		set_binding(1, 0, 1, "Tab");
		CHECK(!strcmp(controls_screen.bindings[1][0], "Tab") && !controls_screen.bindings[15][0][0] &&
			error_sounds == 2, "SHOW SCORES kept its key");
		set_binding(1, 0, 2, "");
		CHECK(!controls_screen.bindings[1][0][0] && error_sounds == 2, "MOVE BACKWARD's first key was not cleared");
		set_binding(1, 1, 2, "");
		CHECK(!strcmp(controls_screen.bindings[1][1], "A0") && error_sounds == 3, "MOVE BACKWARD's last key was cleared");
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
