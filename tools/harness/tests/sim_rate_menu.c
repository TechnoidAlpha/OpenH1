#include "harness.h"

/* Graphics' SIMULATION row (port/linux/game/menu_functions.c's
sim_rate_row_show, tools/port_settings.py): game.sim_rate's spinner, locked
at ORIGINAL (30) where the game sets the setting aside, and OK and Defaults,
which pass over a locked spinner */

#define MAXIMUM_STRINGS 32
#define _stricmp strcasecmp
#include <strings.h>

struct widget_instance
{
	char const *name;
	long definition_tag_index;
	boolean disabled;
	real alpha_modifier;
	struct widget_instance *child, *next, *focused_child;
	union
	{
		struct
		{
			short selected_index;
		} list;
	} parameters;
};

#include "config.inc"

/* the fake world: the one setting, whether it is set aside, the config */
static struct pc_menu_setting sim_rate_setting;
static boolean locked;
static char config_value[16] = "60";
static int writes;

static struct pc_menu_setting *pc_menu_setting_get(long definition_index)
{
	return definition_index == 7 ? &sim_rate_setting : NULL;
}

/* (a spinner's setting: the one's, by its definition here) */
static struct pc_menu_setting *settings_setting_of(struct widget_instance *spinner)
{
	return spinner ? pc_menu_setting_get(spinner->definition_tag_index) : NULL;
}

static boolean sim_rate_locked(void)
{
	return locked;
}

/* the rate the game takes while locked: a Legacy game's 30, or a Standard
game's agreed rate */
static long agreed_rate = 30;

static long sim_rate_next(void)
{
	return agreed_rate;
}

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	struct widget_instance *child;

	(void)nth;
	if (!widget)
		return NULL;
	if (!strcmp(widget->name, name))
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *found = named(child, name, 0);

		if (found)
			return found;
	}
	return NULL;
}

static boolean setting_text(char const *name, char *text, unsigned int size, boolean default_value)
{
	snprintf(text, size, "%s", strcmp(name, "game.sim_rate") ? "" : default_value ? "60" : config_value);
	return TRUE;
}

static boolean setting_write(char const *name, char const *value)
{
	if (strcmp(name, "game.sim_rate"))
		return FALSE;
	snprintf(config_value, sizeof(config_value), "%s", value);
	writes++;
	return TRUE;
}

#define platform_log(...) ((void)0)

#include "under_test.inc"

/* the screen's list: a row, the SIMULATION row (its label and spinner), the
buttons */
static struct widget_instance list, other_row, row, label, spinner, button_bar;

static void setup(void)
{
	static char const *const values[] = { "30", "60", "120" };
	char text[16];
	unsigned int index;

	memset(&sim_rate_setting, 0, sizeof(sim_rate_setting));
	sim_rate_setting.definition_index = 7;
	sim_rate_setting.setting = "game.sim_rate";
	sim_rate_setting.value_count = 3;
	for (index = 0; index < 3; index++)
		sim_rate_setting.values[index] = values[index];
	list = (struct widget_instance){ .name = "options_menu", .alpha_modifier = 1.0f, .child = &other_row };
	other_row = (struct widget_instance){ .name = "op_model_detail", .alpha_modifier = 1.0f, .next = &row };
	row = (struct widget_instance){ .name = "op_sim_rate", .alpha_modifier = 1.0f, .child = &label,
		.next = &button_bar };
	label = (struct widget_instance){ .name = "sim_rate_label", .alpha_modifier = 1.0f, .next = &spinner };
	spinner = (struct widget_instance){ .name = "sim_rate_spinner", .definition_tag_index = 7, .alpha_modifier = 1.0f };
	button_bar = (struct widget_instance){ .name = "button_bar", .alpha_modifier = 1.0f };
	list.focused_child = &other_row;
	/* ("port setting load", on the spinner's created event) */
	setting_text("game.sim_rate", text, sizeof(text), FALSE);
	sim_rate_setting.loaded_index = setting_value_index(&sim_rate_setting, text);
	spinner.parameters.list.selected_index = sim_rate_setting.loaded_index;
	writes = 0;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a game of this machine alone: the setting's value, the row as any */
	if (!strcmp(case_name, "unlocked"))
	{
		int frame;

		strcpy(config_value, "60");
		locked = FALSE;
		setup();
		for (frame = 0; frame < 3; frame++)
			sim_rate_row_show(&list);
		CHECK(spinner.parameters.list.selected_index == 1, "shows %d, not 60", spinner.parameters.list.selected_index);
		CHECK(!row.disabled && !spinner.disabled && row.alpha_modifier == 1.0f, "unlocked row disabled or dimmed");
		/* changed to 120: OK writes it */
		spinner.parameters.list.selected_index = 2;
		setting_changed_save(&spinner, &sim_rate_setting);
		CHECK(writes == 1 && !strcmp(config_value, "120"), "OK wrote %d times: %s", writes, config_value);
		/* Defaults: 60 (port_config.c) */
		setting_default_show(&spinner, &sim_rate_setting);
		CHECK(spinner.parameters.list.selected_index == 1, "defaults: %d", spinner.parameters.list.selected_index);
		return 0;
	}

	/* a network game of other machines or a film: ORIGINAL (30) shown, dimmed,
	passed over; OK and Defaults leave the setting as it was */
	if (!strcmp(case_name, "locked"))
	{
		int frame;

		strcpy(config_value, "120");
		locked = TRUE;
		setup();
		list.focused_child = &row;
		for (frame = 0; frame < 3; frame++)
			sim_rate_row_show(&list);
		CHECK(spinner.parameters.list.selected_index == 0, "locked shows %d, not ORIGINAL (30)",
			spinner.parameters.list.selected_index);
		CHECK(row.disabled && spinner.disabled, "locked row takes focus or events");
		CHECK(row.alpha_modifier < 1.0f && row.alpha_modifier > 0.0f, "locked row not dimmed: %g", row.alpha_modifier);
		CHECK(list.focused_child == &button_bar && list.parameters.list.selected_index == 2,
			"focus left on the locked row");
		setting_changed_save(&spinner, &sim_rate_setting);
		CHECK(writes == 0 && !strcmp(config_value, "120"), "OK wrote the locked 30: %s", config_value);
		spinner.parameters.list.selected_index = 0;
		setting_default_show(&spinner, &sim_rate_setting);
		setting_changed_save(&spinner, &sim_rate_setting);
		CHECK(writes == 0 && !strcmp(config_value, "120"), "Defaults then OK wrote: %s", config_value);
		CHECK(other_row.alpha_modifier == 1.0f && !other_row.disabled, "another row locked");
		return 0;
	}

	/* a Standard network game: its agreed rate shown, locked, and OK leaves
	the setting as it was */
	if (!strcmp(case_name, "locked-agreed"))
	{
		strcpy(config_value, "30");
		locked = TRUE;
		agreed_rate = 120;
		setup();
		sim_rate_row_show(&list);
		CHECK(spinner.parameters.list.selected_index == 2, "a Standard game at 120 shows %d",
			spinner.parameters.list.selected_index);
		CHECK(row.disabled && spinner.disabled, "locked row takes focus or events");
		setting_changed_save(&spinner, &sim_rate_setting);
		CHECK(writes == 0 && !strcmp(config_value, "30"), "OK wrote the agreed rate: %s", config_value);
		return 0;
	}

	/* the lock lifted (back to a game of this machine alone): the setting's
	value again, the row as any */
	if (!strcmp(case_name, "unlock"))
	{
		strcpy(config_value, "60");
		locked = TRUE;
		setup();
		sim_rate_row_show(&list);
		locked = FALSE;
		sim_rate_row_show(&list);
		CHECK(spinner.parameters.list.selected_index == 1, "unlocked shows %d, not 60",
			spinner.parameters.list.selected_index);
		CHECK(!row.disabled && !spinner.disabled && row.alpha_modifier == 1.0f, "row still locked");
		setting_changed_save(&spinner, &sim_rate_setting);
		CHECK(writes == 0, "unlocking wrote the setting");
		return 0;
	}

	/* another screen (no SIMULATION row): nothing to do */
	if (!strcmp(case_name, "unrelated"))
	{
		locked = TRUE;
		setup();
		row.name = "op_other";
		spinner.name = "other_spinner";
		sim_rate_row_show(&list);
		CHECK(!row.disabled && !spinner.disabled && row.alpha_modifier == 1.0f, "unrelated row locked");
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
