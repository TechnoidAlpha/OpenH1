/*
INPUT_TRACE.C

What the keyboard, mouse and gamepads sent, and what the game read of
them, kept for the last INPUT_TRACE_SECONDS: always on, so that input that
goes wrong (a key the game takes as held when nothing is) can be followed
from the report the player sends (F8: input.txt, report.c) or the log
(debug.input_trace), back to the layer where it went wrong:

  - the events, as the window's thread takes them from SDL: the keys
    (scancode, down or up, the system's own code) and mouse buttons, the
    gamepads' buttons, their sticks and triggers as they cross a quarter of
    their range, gamepads coming and going, and the window's focus,
    visibility, fullscreen and the mouse entering and leaving it;
  - each key or button the events left held that SDL (its state) or the
    system (macOS's) says is up, and so is let go of (sdl_platform.c:
    input_held_reconcile);
  - the controller's readings, as the game takes them: the keys and
    buttons held and those latched (pressed and let go of since the last
    reading, counted in it), what the first controller then reads (its
    pad, the keyboard's actions, the mouse's motion), and a sub-step's
    reading where it differs from the frame's;
  - the player's move and look as the game reads them (input_abstraction.c),
    each frame and sub-step;
  - the look stick's acceleration as the game takes it (player_control.c,
    by the aim style, input.aim_style): whether it accelerates and how long
    it has.

A record is written where it differs from the last of its kind, into a
fixed ring (no allocation, a lock no other thread waits on long); the text
is made only when asked for.
*/

#include "input_trace.h"

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

#define INPUT_TRACE_RECORDS 8192
/* a reading's held keys traced by name (more are counted) */
#define INPUT_TRACE_KEYS 4
/* an axis traced as it crosses this (a quarter of 32767) */
#define INPUT_TRACE_AXIS 8192
#define INPUT_TRACE_GAMEPADS 4
#define INPUT_TRACE_AXES 6

enum
{
	_trace_key = 1,
	_trace_mouse_button,
	_trace_gamepad_button,
	_trace_gamepad_axis,
	_trace_gamepad_device,
	_trace_window,
	_trace_release,
	_trace_reading,
	_trace_latched,
	_trace_pad,
	_trace_pad_sticks,
	_trace_look,
	_trace_player,
	_trace_player_look,
	_trace_aim
};

/* a record: when (ms since SDL started), how long before that the event
was sent (ms, -1 for none), its kind, and what it says (by kind) */
struct input_trace_record
{
	unsigned int ms;
	int lag_ms;
	unsigned char kind;
	unsigned char a;
	unsigned short b;
	int c;
	int d;
};

static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static struct input_trace_record trace[INPUT_TRACE_RECORDS];
static unsigned long trace_count;
/* the last of each kind written where only a change is (the pad's and the
player's for the frame and the sub-step apart) */
static struct input_trace_record last_reading, last_pad[2], last_sticks[2], last_player[2][4],
	last_player_look[2][4], last_aim[4];
static unsigned long readings_unchanged;
static struct
{
	unsigned int id;
	unsigned char past[INPUT_TRACE_AXES];
} trace_axes[INPUT_TRACE_GAMEPADS];

static void trace_put(int kind, unsigned long long event_ns, int a, int b, int c, int d)
{
	unsigned long long now = input_trace_now_ns();
	struct input_trace_record *record;

	pthread_mutex_lock(&trace_lock);
	record = &trace[trace_count % INPUT_TRACE_RECORDS];
	record->ms = (unsigned int)(now / 1000000ull);
	record->lag_ms = event_ns ? (event_ns <= now ? (int)((now - event_ns) / 1000000ull) : 0) : -1;
	record->kind = (unsigned char)kind;
	record->a = (unsigned char)a;
	record->b = (unsigned short)b;
	record->c = c;
	record->d = d;
	trace_count++;
	pthread_mutex_unlock(&trace_lock);
}

/* a record written only where it differs from *last (whose writer is one
thread, the game's): 1 if it was */
static int trace_put_changed(struct input_trace_record *last, int kind, int a, int b, int c, int d)
{
	if (last->kind == kind && last->a == (unsigned char)a && last->b == (unsigned short)b && last->c == c &&
		last->d == d)
	{
		return 0;
	}
	last->kind = (unsigned char)kind;
	last->a = (unsigned char)a;
	last->b = (unsigned short)b;
	last->c = c;
	last->d = d;
	trace_put(kind, 0, a, b, c, d);
	return 1;
}

void input_trace_key(unsigned long long event_ns, int scancode, int down, int repeat, int raw)
{
	/* (a key held repeats: only its first) */
	if (repeat)
		return;
	trace_put(_trace_key, event_ns, down != 0, scancode, raw, 0);
}

void input_trace_mouse_button(unsigned long long event_ns, int button, int down, int pointer)
{
	trace_put(_trace_mouse_button, event_ns, down != 0, button, pointer != 0, 0);
}

void input_trace_gamepad_button(unsigned long long event_ns, unsigned int id, int button, int down)
{
	trace_put(_trace_gamepad_button, event_ns, down != 0, button, (int)id, 0);
}

void input_trace_gamepad_axis(unsigned long long event_ns, unsigned int id, int axis, int value)
{
	int slot, empty = -1, past;

	if (axis < 0 || axis >= INPUT_TRACE_AXES)
		return;
	for (slot = 0; slot < INPUT_TRACE_GAMEPADS && trace_axes[slot].id != id; slot++)
	{
		if (!trace_axes[slot].id && empty < 0)
			empty = slot;
	}
	if (slot == INPUT_TRACE_GAMEPADS)
	{
		slot = empty < 0 ? (int)(id % INPUT_TRACE_GAMEPADS) : empty;
		memset(&trace_axes[slot], 0, sizeof(trace_axes[slot]));
		trace_axes[slot].id = id;
	}
	past = value >= INPUT_TRACE_AXIS ? 1 : value <= -INPUT_TRACE_AXIS ? 2 : 0;
	if (trace_axes[slot].past[axis] == past)
		return;
	trace_axes[slot].past[axis] = (unsigned char)past;
	trace_put(_trace_gamepad_axis, event_ns, axis, 0, (int)id, value);
}

void input_trace_gamepad_device(unsigned long long event_ns, unsigned int id, int added)
{
	trace_put(_trace_gamepad_device, event_ns, added != 0, 0, (int)id, 0);
}

void input_trace_window(unsigned long long event_ns, int what)
{
	trace_put(_trace_window, event_ns, what, 0, 0, 0);
}

void input_trace_release(int input, int by)
{
	trace_put(_trace_release, 0, by, input, 0, 0);
}

void input_trace_reading(const unsigned char *keys, int key_count, const unsigned char *buttons, int button_count,
	const unsigned char *latched_keys, const unsigned char *latched_buttons, int flags)
{
	int held[INPUT_TRACE_KEYS] = { 0 };
	int count = 0, latched = 0, first_latched = 0, scancode, button;
	unsigned int button_mask = 0, latched_mask = 0;

	for (scancode = 0; scancode < key_count; scancode++)
	{
		if (keys[scancode])
		{
			if (count < INPUT_TRACE_KEYS)
				held[count] = scancode;
			count++;
		}
		if (latched_keys && latched_keys[scancode])
		{
			if (!latched)
				first_latched = scancode;
			latched++;
		}
	}
	for (button = 0; button < button_count && button < 16; button++)
	{
		if (buttons[button])
			button_mask |= 1u << button;
		if (latched_buttons && latched_buttons[button])
			latched_mask |= 1u << button;
	}
	/* (a reading that latched something is always written) */
	if (latched || latched_mask)
		last_reading.kind = 0;
	if (count > 255)
		count = 255;
	pthread_mutex_lock(&trace_lock);
	readings_unchanged++;
	pthread_mutex_unlock(&trace_lock);
	if (!trace_put_changed(&last_reading, _trace_reading, count, (int)(button_mask | (unsigned int)flags << 8),
		held[0] | held[1] << 16, held[2] | held[3] << 16))
	{
		return;
	}
	pthread_mutex_lock(&trace_lock);
	readings_unchanged = 0;
	pthread_mutex_unlock(&trace_lock);
	if (latched || latched_mask)
		trace_put(_trace_latched, 0, latched > 255 ? 255 : latched, first_latched, (int)latched_mask, 0);
}

static int trace_stick(int value)
{
	return value & 0xffff;
}

void input_trace_pad(int substep, unsigned int buttons, int left_x, int left_y, int right_x, int right_y,
	int left_trigger, int right_trigger, unsigned long actions, float mouse_dx, float mouse_dy)
{
	substep = substep != 0;
	trace_put_changed(&last_pad[substep], _trace_pad, substep, (int)(buttons & 0xffff),
		(left_trigger & 0xff) | (right_trigger & 0xff) << 8, (int)actions);
	trace_put_changed(&last_sticks[substep], _trace_pad_sticks, substep, 0,
		trace_stick(left_x) | trace_stick(left_y) << 16, trace_stick(right_x) | trace_stick(right_y) << 16);
	if (!substep && (mouse_dx != 0.0f || mouse_dy != 0.0f))
		trace_put(_trace_look, 0, 0, 0, (int)lrintf(mouse_dx * 100.0f), (int)lrintf(mouse_dy * 100.0f));
}

void input_trace_player(int controller, int substep, float forward, float strafe, float yaw, float pitch)
{
	if (controller < 0 || controller >= 4)
		return;
	substep = substep != 0;
	trace_put_changed(&last_player[substep][controller], _trace_player, controller, substep,
		(int)lrintf(forward * 1000.0f), (int)lrintf(strafe * 1000.0f));
	trace_put_changed(&last_player_look[substep][controller], _trace_player_look, controller, substep,
		(int)lrintf(yaw * 1000.0f), (int)lrintf(pitch * 1000.0f));
}

void input_trace_aim(int controller, int modern, int accelerating, float acceleration_seconds)
{
	if (controller < 0 || controller >= 4)
		return;
	trace_put_changed(&last_aim[controller], _trace_aim, controller, (modern != 0) | (accelerating != 0) << 1,
		(int)lrintf(acceleration_seconds * 1000.0f), 0);
}

/* ---------- the text */

static const char *const window_events[NUMBER_OF_INPUT_TRACE_WINDOW_EVENTS] =
{
	"?", "focus gained", "focus lost", "shown", "hidden", "exposed", "occluded", "minimized", "restored",
	"fullscreen", "windowed", "mouse entered", "mouse left", "keyboard removed",
};

static const char *const pad_buttons[16] =
{
	"up", "down", "left", "right", "start", "back", "left-stick", "right-stick",
	"?", "?", "?", "?", "?", "?", "?", "?",
};

struct trace_text
{
	char *text;
	size_t size, used;
};

static void text_add(struct trace_text *text, const char *format, ...)
{
	va_list arguments;
	int length;

	if (!text->text)
		return;
	va_start(arguments, format);
	length = vsnprintf(text->text + text->used, text->size - text->used, format, arguments);
	va_end(arguments);
	if (length < 0)
		return;
	if ((size_t)length >= text->size - text->used)
	{
		size_t size = text->size * 2 + (size_t)length;
		char *grown = realloc(text->text, size);

		if (!grown)
		{
			free(text->text);
			text->text = NULL;
			return;
		}
		text->text = grown;
		text->size = size;
		va_start(arguments, format);
		vsnprintf(text->text + text->used, text->size - text->used, format, arguments);
		va_end(arguments);
	}
	text->used += (size_t)length;
}

static void text_input_name(struct trace_text *text, int input)
{
	char name[64];

	halo_input_name(input, name, sizeof(name));
	if (name[0])
		text_add(text, "%s", name);
	else
		text_add(text, "#%d", input);
}

static void text_fraction(struct trace_text *text, int value, int scale)
{
	text_add(text, "%s%d.%03d", value < 0 ? "-" : "", abs(value) / scale, (abs(value) % scale) * (1000 / scale));
}

static void text_record(struct trace_text *text, const struct input_trace_record *record)
{
	int index;

	text_add(text, "%7u.%03u ", record->ms / 1000, record->ms % 1000);
	switch (record->kind)
	{
	case _trace_key:
		text_add(text, "key ");
		text_input_name(text, record->b);
		text_add(text, " %s (code %d)", record->a ? "down" : "up", record->c);
		break;
	case _trace_mouse_button:
		text_add(text, "mouse %d %s%s", record->b, record->a ? "down" : "up", record->c ? " (the menus' pointer)" : "");
		break;
	case _trace_gamepad_button:
		text_add(text, "gamepad %d button %d %s", record->c, record->b, record->a ? "down" : "up");
		break;
	case _trace_gamepad_axis:
		text_add(text, "gamepad %d axis %d at %d", record->c, record->a, record->d);
		break;
	case _trace_gamepad_device:
		text_add(text, "gamepad %d %s", record->c, record->a ? "added" : "removed");
		break;
	case _trace_window:
		text_add(text, "window: %s", record->a < NUMBER_OF_INPUT_TRACE_WINDOW_EVENTS ? window_events[record->a] : "?");
		break;
	case _trace_release:
		text_add(text, "let go of ");
		text_input_name(text, record->b);
		text_add(text, ", which its events left held: %s",
			record->a == INPUT_TRACE_RELEASED_BY_SYSTEM ? "the system has it up (SDL missed its release)" :
			"SDL has it up (its release was lost on the way)");
		break;
	case _trace_reading:
		text_add(text, "read: keys");
		if (!record->a)
			text_add(text, " -");
		for (index = 0; index < record->a && index < INPUT_TRACE_KEYS; index++)
		{
			text_add(text, " ");
			text_input_name(text, (index & 1 ? (index < 2 ? record->c : record->d) >> 16 :
				(index < 2 ? record->c : record->d)) & 0xffff);
		}
		if (record->a > INPUT_TRACE_KEYS)
			text_add(text, " (%d in all)", record->a);
		text_add(text, ", buttons");
		if (!(record->b & 0xff))
			text_add(text, " -");
		for (index = 0; index < 8; index++)
		{
			if (record->b & (1 << index))
				text_add(text, " %d", index);
		}
		text_add(text, "%s%s%s%s", record->b & (INPUT_TRACE_READING_MENUS << 8) ? ", menus" : ", game",
			record->b & (INPUT_TRACE_READING_FOCUSED << 8) ? ", focused" : ", not focused",
			record->b & (INPUT_TRACE_READING_POINTER << 8) ? ", pointer" : "",
			record->b & (INPUT_TRACE_READING_RELEASED << 8) ? ", mouse released" : "");
		break;
	case _trace_latched:
		text_add(text, "  latched (pressed and let go of since the last reading):");
		if (record->a)
		{
			text_add(text, " %d key%s, ", record->a, record->a == 1 ? "" : "s");
			text_input_name(text, record->b);
		}
		for (index = 0; index < 16; index++)
		{
			if (record->c & (1 << index))
				text_add(text, " mouse %d", index);
		}
		break;
	case _trace_pad:
		text_add(text, "%s: buttons", record->a ? "sub-step pad" : "pad");
		if (!record->b)
			text_add(text, " -");
		for (index = 0; index < 16; index++)
		{
			if (record->b & (1 << index))
				text_add(text, " %s", pad_buttons[index]);
		}
		text_add(text, ", triggers %d %d, actions", record->c & 0xff, (record->c >> 8) & 0xff);
		if (!record->d)
			text_add(text, " -");
		for (index = 0; index < 32; index++)
		{
			if ((unsigned int)record->d & (1u << index))
			{
				const char *name = halo_keyboard_action_setting(index);

				text_add(text, " %s", name ? name : "?");
			}
		}
		break;
	case _trace_pad_sticks:
		text_add(text, "%s: left stick %d %d, right stick %d %d", record->a ? "sub-step pad" : "pad",
			(short)(record->c & 0xffff), (short)((unsigned int)record->c >> 16), (short)(record->d & 0xffff),
			(short)((unsigned int)record->d >> 16));
		break;
	case _trace_look:
		text_add(text, "mouse moved ");
		text_fraction(text, record->c * 10, 1000);
		text_add(text, " ");
		text_fraction(text, record->d * 10, 1000);
		break;
	case _trace_player:
		text_add(text, "player %d%s: forward ", record->a, record->b ? " sub-step" : "");
		text_fraction(text, record->c, 1000);
		text_add(text, ", strafe ");
		text_fraction(text, record->d, 1000);
		break;
	case _trace_player_look:
		text_add(text, "player %d%s: look yaw ", record->a, record->b ? " sub-step" : "");
		text_fraction(text, record->c, 1000);
		text_add(text, ", pitch ");
		text_fraction(text, record->d, 1000);
		break;
	case _trace_aim:
		text_add(text, "player %d: %s aim, %s, acceleration time ", record->a, record->b & 1 ? "modern" : "classic",
			record->b & 2 ? "accelerating" : "not accelerating");
		text_fraction(text, record->c, 1000);
		break;
	default:
		text_add(text, "?");
		break;
	}
	if (record->lag_ms > 0)
		text_add(text, " [sent %d ms before]", record->lag_ms);
	text_add(text, "\n");
}

char *input_trace_text(int seconds, size_t *size)
{
	struct trace_text text;
	unsigned long first, index, unchanged;
	unsigned int now = (unsigned int)(input_trace_now_ns() / 1000000ull);
	unsigned int from = now > (unsigned int)seconds * 1000u ? now - (unsigned int)seconds * 1000u : 0;
	struct input_trace_record *copy = malloc(sizeof(trace));
	unsigned long count;

	text.size = 64 * 1024;
	text.used = 0;
	text.text = copy ? malloc(text.size) : NULL;
	if (!text.text)
	{
		free(copy);
		return NULL;
	}
	text.text[0] = 0;
	pthread_mutex_lock(&trace_lock);
	count = trace_count;
	first = count > INPUT_TRACE_RECORDS ? count - INPUT_TRACE_RECORDS : 0;
	for (index = first; index < count; index++)
		copy[index - first] = trace[index % INPUT_TRACE_RECORDS];
	unchanged = readings_unchanged;
	pthread_mutex_unlock(&trace_lock);
	text_add(&text, "The input trace (port/linux/src/input_trace.c): the last %d s, oldest first, at the seconds since "
		"the game started; now %u.%03u. Each reading and pad is written where it changes.\n", seconds,
		now / 1000, now % 1000);
	for (index = 0; index < count - first; index++)
	{
		if (copy[index].ms >= from)
			text_record(&text, &copy[index]);
	}
	text_add(&text, "(the last reading unchanged for %lu more readings; %lu records in all)\n", unchanged, count);
	free(copy);
	if (!text.text)
		return NULL;
	if (size)
		*size = text.used;
	return text.text;
}

void input_trace_log(const char *why, int seconds)
{
	size_t size;
	char *text = input_trace_text(seconds, &size);
	char *line, *next;

	if (!text)
		return;
	platform_log("input trace: %s", why);
	for (line = text; *line; line = next)
	{
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		else
			next = line + strlen(line);
		platform_log("input trace: %s", line);
	}
	free(text);
}
