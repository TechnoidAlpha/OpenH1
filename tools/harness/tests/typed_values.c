/*
TYPED_VALUES.C (test)

The menus' text fields (port/linux/game/menu_functions.c: text_field_open,
text_field_begin_value, text_field_insert, text_field_show, and the
sliders' settings_slider_field_text) and the keyboard while one is typed
into (port/linux/src/xinput_sdl.c's typing_gamepad): a slider's field starts
empty where its value is a word (DEFAULT, AUTO, NONE, OFF) and with its
number chosen otherwise, which the first key typed replaces; Enter is the
row's A, posted to the menus, never the controller's Start (which in a game
closes the menus and drops the change). test_typed_values.py takes the code.
*/

#include "harness.h"

#include <wchar.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef short SHORT;
typedef unsigned short WORD;

#include "config.inc"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))

/* (input/input.h) */
struct key_stroke
{
	byte modifier_flags;
	char ascii_code;
	short key_code;
};
enum { _key_escape, _key_backspace, _key_return, _key_v, _key_other };

struct widget_instance
{
	struct { struct { wchar_t *text; } text_box; } parameters;
};

/* the keys typed, read by input_get_key */
static struct key_stroke keys[16];
static int key_count, key_read;

static boolean input_get_key(struct key_stroke *key)
{
	if (key_read >= key_count)
		return FALSE;
	*key = keys[key_read++];
	return TRUE;
}

static void type_key(short code, char ascii)
{
	keys[key_count].modifier_flags = 0;
	keys[key_count].ascii_code = ascii;
	keys[key_count].key_code = code;
	key_count++;
}

static void type_text(char const *text)
{
	for (; *text; text++)
		type_key(_key_other, *text);
}

/* the presses the menus posted (ui_widget_port_post_button) */
static short posted_controller = NONE, posted_button = NONE;
static int posted_count;

static void ui_widget_port_post_button(short controller, short button)
{
	posted_controller = controller;
	posted_button = button;
	posted_count++;
}

static wchar_t shown_text[256];
static void text_set(struct widget_instance *widget, wchar_t const *text)
{
	(void)widget;
	wcsncpy(shown_text, text, 255);
}

/* the text box whose caret ui_widget.c draws (ui_widget_port_caret_show), lit */
static struct widget_instance *caret_box;
static void ui_widget_port_caret_show(struct widget_instance *box, boolean shown)
{
	if (shown)
		caret_box = box;
	else if (box == caret_box)
		caret_box = NULL;
}

#define platform_text_field(typing, masked) ((void)0)
#define platform_clipboard_get(text, size) (FALSE)
/* (the clock, which the caret blinks by) */
static unsigned long now;
#define system_milliseconds() (now)
/* (the frames' time, not fixed here: frames_clock.c) */
#define frames_clock(milliseconds) (0)
#define text_filter_exempt(text) ((void)0)
#define _stricmp strcasecmp
#include <strings.h>

/* (xinput_sdl.c) */
typedef struct { WORD wButtons; BYTE bAnalogButtons[8]; SHORT sThumbLX, sThumbLY; } XINPUT_GAMEPAD;
struct platform_input_state { unsigned char keys[TEST_SCANCODE_COUNT]; BOOL menu_back; };
static BOOL text_typing_enter_armed;
BOOL text_typing_field;
BOOL text_typing_keyboard;

#include "under_test.inc"

/* a slider made from its row of the model (test_model: the model's rows,
port/assets/menus/model) */
static struct settings_slider const *slider_named(char const *row)
{
	long index;

	for (index = 0; index < test_model.row_count; index++)
	{
		if (!strcmp(test_model.rows[index].key, row + 3))
			return settings_slider_made(&test_model.rows[index]);
	}
	CHECK(FALSE, "no slider %s", row);
	return NULL;
}

static void typed_done(char const *text)
{
	(void)text;
}

static void keys_reset(void)
{
	key_count = key_read = 0;
	posted_count = 0;
	posted_controller = posted_button = NONE;
}

static struct widget_instance row, value;

/* the field shown a frame, the keys typed read */
static void frame(void)
{
	text_field_show(&value, "", TRUE);
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";
	char text[64];

	if (!strcmp(test, "word-empty"))
	{
		/* a stop shown as a word: nothing to edit; a number: its number */
		struct settings_slider const *fov = slider_named("op_fov"), *fps = slider_named("op_max_fps");
		struct settings_slider const *panini = slider_named("op_panini"), *vehicle = slider_named("op_vehicle_fov");

		settings_slider_field_text(fov, 0.0, text, sizeof(text));
		CHECK(!strcmp(text, ""), "FoV's DEFAULT starts as \"%s\"", text);
		settings_slider_field_text(fps, 0.0, text, sizeof(text));
		CHECK(!strcmp(text, ""), "the frame rate's AUTO starts as \"%s\"", text);
		settings_slider_field_text(fps, -1.0, text, sizeof(text));
		CHECK(!strcmp(text, ""), "the frame rate's NONE starts as \"%s\"", text);
		settings_slider_field_text(panini, 0.0, text, sizeof(text));
		CHECK(!strcmp(text, ""), "Panini's OFF starts as \"%s\"", text);
		settings_slider_field_text(vehicle, -1.0, text, sizeof(text));
		CHECK(!strcmp(text, ""), "the vehicle FoV's MATCH starts as \"%s\"", text);
		settings_slider_field_text(fov, 90.0, text, sizeof(text));
		CHECK(!strcmp(text, "90.0"), "FoV 90 starts as \"%s\"", text);
		settings_slider_field_text(fps, 144.0, text, sizeof(text));
		CHECK(!strcmp(text, "144"), "a frame rate of 144 starts as \"%s\"", text);
		return 0;
	}
	if (!strcmp(test, "replace"))
	{
		/* the number chosen: what is typed replaces it */
		keys_reset();
		text_field_begin_value(&row, "90", 7, typed_done);
		type_text("105");
		frame();
		CHECK(!strcmp(text_field.text, "105"), "typed over 90: \"%s\"", text_field.text);
		/* (and then appends, as typing does) */
		keys_reset();
		type_text("5");
		frame();
		CHECK(!strcmp(text_field.text, "1055"), "typed on: \"%s\"", text_field.text);
		return 0;
	}
	if (!strcmp(test, "backspace-clears"))
	{
		keys_reset();
		text_field_begin_value(&row, "90", 7, typed_done);
		type_key(_key_backspace, 8);
		type_text("80");
		frame();
		CHECK(!strcmp(text_field.text, "80"), "backspace then 80: \"%s\"", text_field.text);
		keys_reset();
		type_key(_key_backspace, 8);
		frame();
		CHECK(!strcmp(text_field.text, "8"), "a second backspace takes one: \"%s\"", text_field.text);
		/* an empty field (a word's) is typed into at once */
		keys_reset();
		text_field_begin_value(&row, "", 7, typed_done);
		type_text("110");
		frame();
		CHECK(!strcmp(text_field.text, "110"), "a word's field typed into: \"%s\"", text_field.text);
		return 0;
	}
	if (!strcmp(test, "names-append"))
	{
		/* a name's or a code's field keeps its text: typing adds to it */
		keys_reset();
		text_field_begin(&row, "Blood", 31, typed_done);
		type_text(" Gulch");
		frame();
		CHECK(!strcmp(text_field.text, "Blood Gulch"), "a name typed on: \"%s\"", text_field.text);
		keys_reset();
		type_key(_key_backspace, 8);
		frame();
		CHECK(!strcmp(text_field.text, "Blood Gulc"), "a name's backspace: \"%s\"", text_field.text);
		return 0;
	}
	if (!strcmp(test, "enter-is-a"))
	{
		keys_reset();
		text_field_begin_value(&row, "", 7, typed_done);
		type_text("100");
		type_key(_key_return, 13);
		type_text("5");
		frame();
		CHECK(posted_count == 1 && posted_button == BUTTON_A && posted_controller == 0,
			"Enter posted %d presses (button %d, controller %d), not the row's A", posted_count, posted_button,
			posted_controller);
		CHECK(!strcmp(text_field.text, "100"), "a key after Enter was typed: \"%s\"", text_field.text);
		return 0;
	}
	if (!strcmp(test, "enter-not-start"))
	{
		XINPUT_GAMEPAD pad;
		struct platform_input_state input;

		/* a menu's field: Enter, held, presses no controller button */
		memset(&input, 0, sizeof(input));
		input.keys[SDL_SCANCODE_RETURN] = 1;
		text_typing_field = TRUE;
		text_typing_keyboard = FALSE;
		text_typing_enter_armed = TRUE;
		memset(&pad, 0, sizeof(pad));
		typing_gamepad(&input, &pad);
		CHECK(!(pad.wButtons & XINPUT_GAMEPAD_START), "a field's Enter pressed Start");
		CHECK(!pad.bAnalogButtons[XINPUT_GAMEPAD_A], "a field's Enter pressed A");
		/* the game's on-screen keyboard keeps Start (its Done) */
		text_typing_field = FALSE;
		text_typing_keyboard = TRUE;
		memset(&pad, 0, sizeof(pad));
		typing_gamepad(&input, &pad);
		CHECK(pad.wButtons & XINPUT_GAMEPAD_START, "the on-screen keyboard's Enter is not its Done");
		return 0;
	}
	if (!strcmp(test, "caret"))
	{
		/* the caret is the text box's to draw (a bar on the capitals'
		middle, issue 682), never a character of the text (an underscore,
		under the baseline): lit half the time, unlit once typing ends */
		keys_reset();
		text_field_begin(&row, "gulch", 31, typed_done);
		for (now = 0; now < 2000; now += 250)
		{
			frame();
			CHECK(!wcscmp(shown_text, L"gulch"), "at %lu ms the field shows \"%ls\"", now, shown_text);
			CHECK((caret_box == &value) == ((now / 500) % 2 == 1), "at %lu ms the caret is %s", now,
				caret_box ? "lit" : "unlit");
		}
		now = 500;
		frame();
		CHECK(caret_box == &value, "no caret while typing");
		text_field_show(&value, "gulch", FALSE);
		CHECK(caret_box == NULL && !wcscmp(shown_text, L"gulch"), "a caret after typing ended");
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
