/*
MENU_LAYOUT.C

The open screen's geometry as the first player's render of the menus draws
it (ui_widget.c's render_ui_widgets): each widget drawn, its box and clip;
each string drawn (rasterizer_text.c), the bounds it is laid out in, its
clip and the extent of its characters (and of all of them: the lines that
run past its box's foot are not drawn, nor a line's letters past its right
edge); each button's glyph (a key's cap or a controller's button, in words
or as a widget's picture); and the pointer's targets. So that the layout's checks read what the game drew rather than
guess at pixels.

A frame is recorded when it is written or kept:
- written as JSON, to a file beside its path then renamed over it so that a
  reader never sees half of one: every 30th frame of the menus to
  debug.menu_layout's path, or one frame's to the path asked for
  (menu_layout_request: the menu tour's, menu_tour.c), both in the data
  root, where the game's fopen finds them, for tools/menu_gallery.py;
- kept for the menu studio's overlay (menu_layout_keep: menu_studio.c),
  which draws each widget's box and the problems the layout's checks find
  (menu_layout_overlay): the checks tools/menu_gallery.py's check_layout
  makes, at its thresholds.

{"frame": n, "screen_name": "...", "screen": [x0, y0, x1, y1],
 "prompts": "keyboard" or "controller", "touch_menus": bool,
 "items": [{"kind": "widget", "id", "parent", "name", "type", "box",
	"clip", "focused"}, {"kind": "text", "widget", "text", "box", "clip",
	"extent", "whole", "wide", "scale"}, {"kind": "glyph", "widget", "key",
	"name", "box"}, {"kind": "target", "widget", "target", "box"}]}

Rectangles are [x0, y0, x1, y1] in the menus' units (menu_layout.h).
*/

#include "cseries.h"
#include "math/integer_math.h"
#include "menu_layout.h"

#include <stdio.h>
#include <string.h>

char const *config_string(char const *name);
int halo_keyboard_prompts(void);
int ui_widget_port_touch_menus(void);
void platform_log(char const *format, ...);

#define MENU_LAYOUT_EVERY 30
#define MENU_LAYOUT_MAXIMUM_WIDGETS 4096
#define MENU_LAYOUT_MAXIMUM_TEXTS 2048
#define MENU_LAYOUT_MAXIMUM_GLYPHS 512
#define MENU_LAYOUT_MAXIMUM_TARGETS 1024
#define MENU_LAYOUT_TEXT_LENGTH 256
#define MENU_LAYOUT_NAME_LENGTH 160

struct menu_layout_widget_record
{
	void const *widget;
	long parent;
	short type;
	boolean focused;
	boolean clipped;
	rectangle2d box;
	rectangle2d clip;
	char name[MENU_LAYOUT_NAME_LENGTH];
};

struct menu_layout_text_record
{
	long widget;
	real scale;
	boolean drawn;
	boolean laid;
	boolean widened;
	rectangle2d box;
	rectangle2d clip;
	rectangle2d extent;
	rectangle2d whole;
	rectangle2d wide;
	wchar_t text[MENU_LAYOUT_TEXT_LENGTH];
};

struct menu_layout_glyph_record
{
	long widget;
	boolean key;
	rectangle2d box;
	char name[32];
};

struct menu_layout_target_record
{
	long widget;
	short kind;
	rectangle2d box;
};

static struct
{
	/* recording this frame, and why */
	boolean recording;
	boolean writing;
	boolean keeping;
	/* the file being written, beside its path; and the path asked for the
	next frame's (menu_layout_request) */
	char path[512];
	char temporary[520];
	char requested[512];
	long frame;
	long frames;
	long current;
	long screen_width;
	char screen_name[MENU_LAYOUT_NAME_LENGTH];
	long widget_count, text_count, glyph_count, target_count;
	struct menu_layout_widget_record widgets[MENU_LAYOUT_MAXIMUM_WIDGETS];
	struct menu_layout_text_record texts[MENU_LAYOUT_MAXIMUM_TEXTS];
	struct menu_layout_glyph_record glyphs[MENU_LAYOUT_MAXIMUM_GLYPHS];
	struct menu_layout_target_record targets[MENU_LAYOUT_MAXIMUM_TARGETS];
} menu_layout;

static char const *menu_layout_path(
	void)
{
	static int read = 0;
	static char path[480];

	if (!read)
	{
		read = 1;
		snprintf(path, sizeof(path), "%s", config_string("debug.menu_layout"));
	}

	return path;
}

int menu_layout_recording(
	void)
{
	return menu_layout.recording;
}

void menu_layout_request(
	char const *path)
{
	snprintf(menu_layout.requested, sizeof(menu_layout.requested), "%s", path ? path : "");
}

void menu_layout_keep(
	int keep)
{
	menu_layout.keeping = keep != 0;
}

static long menu_layout_widget_id(
	void const *widget)
{
	long index;

	for (index = menu_layout.widget_count - 1; widget && index >= 0; index--)
	{
		if (menu_layout.widgets[index].widget == widget)
			return index;
	}

	return -1;
}

void menu_layout_frame_begin(
	long screen_width)
{
	char const *path = menu_layout_path();

	if (menu_layout.recording)
		return;
	menu_layout.writing = FALSE;
	if (menu_layout.requested[0])
	{
		snprintf(menu_layout.path, sizeof(menu_layout.path), "%s", menu_layout.requested);
		menu_layout.requested[0] = 0;
		menu_layout.writing = TRUE;
	}
	else if (path[0] && menu_layout.frames++ % MENU_LAYOUT_EVERY == MENU_LAYOUT_EVERY - 1)
	{
		snprintf(menu_layout.path, sizeof(menu_layout.path), "%s", path);
		menu_layout.writing = TRUE;
	}
	if (!menu_layout.writing && !menu_layout.keeping)
		return;
	menu_layout.recording = TRUE;
	menu_layout.screen_width = screen_width;
	menu_layout.widget_count = menu_layout.text_count = 0;
	menu_layout.glyph_count = menu_layout.target_count = 0;
	menu_layout.current = -1;
}

/* ---------- writing */

static void menu_layout_write_rectangle(
	FILE *file,
	char const *name,
	rectangle2d const *rectangle)
{
	if (rectangle)
		fprintf(file, ",\"%s\":[%d,%d,%d,%d]", name, rectangle->x0, rectangle->y0, rectangle->x1, rectangle->y1);
	else
		fprintf(file, ",\"%s\":null", name);
}

/* a string as JSON's, from UTF-16 (a pair of surrogates one character) */
static void menu_layout_write_wide(
	FILE *file,
	wchar_t const *text)
{
	long index;

	fputc('"', file);
	for (index = 0; text && text[index]; index++)
	{
		unsigned long character = (unsigned short)text[index];

		if (character >= 0xd800 && character < 0xdc00 && (unsigned short)text[index + 1] >= 0xdc00 &&
			(unsigned short)text[index + 1] < 0xe000)
		{
			character = 0x10000 + ((character - 0xd800) << 10) + ((unsigned short)text[index + 1] - 0xdc00);
			index++;
		}
		if (character == '"' || character == '\\')
			fprintf(file, "\\%c", (char)character);
		else if (character < 0x20 || (character >= 0xd800 && character < 0xe000))
			fprintf(file, "\\u%04lx", character < 0x20 ? character : 0xfffdUL);
		else if (character < 0x80)
			fputc((int)character, file);
		else if (character < 0x800)
		{
			fputc((int)(0xc0 | (character >> 6)), file);
			fputc((int)(0x80 | (character & 0x3f)), file);
		}
		else if (character < 0x10000)
		{
			fputc((int)(0xe0 | (character >> 12)), file);
			fputc((int)(0x80 | ((character >> 6) & 0x3f)), file);
			fputc((int)(0x80 | (character & 0x3f)), file);
		}
		else
		{
			fputc((int)(0xf0 | (character >> 18)), file);
			fputc((int)(0x80 | ((character >> 12) & 0x3f)), file);
			fputc((int)(0x80 | ((character >> 6) & 0x3f)), file);
			fputc((int)(0x80 | (character & 0x3f)), file);
		}
	}
	fputc('"', file);
}

static void menu_layout_write_string(
	FILE *file,
	char const *text)
{
	long index;

	fputc('"', file);
	for (index = 0; text && text[index]; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character == '"' || character == '\\')
			fprintf(file, "\\%c", character);
		else if (character < 0x20)
			fprintf(file, "\\u%04x", character);
		else
			fputc(character, file);
	}
	fputc('"', file);
}

static void menu_layout_write(
	void)
{
	FILE *file;
	long index;
	int written;
	char const *separator = "";

	snprintf(menu_layout.temporary, sizeof(menu_layout.temporary), "%s.part", menu_layout.path);
	file = fopen(menu_layout.temporary, "w");
	if (!file)
	{
		static int reported = 0;

		if (!reported)
			platform_log("menus: menu layout: cannot write %s", menu_layout.temporary);
		reported = 1;
		return;
	}
	menu_layout.frame++;
	fprintf(file, "{\"frame\":%ld,\"prompts\":\"%s\",\"touch_menus\":%s,\"screen\":[%ld,0,%ld,480],\"items\":[",
		menu_layout.frame, halo_keyboard_prompts() ? "keyboard" : "controller",
		ui_widget_port_touch_menus() ? "true" : "false", -(menu_layout.screen_width - 640) / 2,
		640 + (menu_layout.screen_width - 640) / 2);
	for (index = 0; index < menu_layout.widget_count; index++)
	{
		struct menu_layout_widget_record const *widget = &menu_layout.widgets[index];

		fprintf(file, "%s\n{\"kind\":\"widget\",\"id\":%ld,\"parent\":%ld,\"type\":%d,\"focused\":%s,\"name\":",
			separator, index, widget->parent, widget->type, widget->focused ? "true" : "false");
		separator = ",";
		menu_layout_write_string(file, widget->name);
		menu_layout_write_rectangle(file, "box", &widget->box);
		menu_layout_write_rectangle(file, "clip", widget->clipped ? &widget->clip : NULL);
		fputc('}', file);
	}
	for (index = 0; index < menu_layout.text_count; index++)
	{
		struct menu_layout_text_record const *text = &menu_layout.texts[index];

		fprintf(file, "%s\n{\"kind\":\"text\",\"widget\":%ld,\"scale\":%.3f,\"text\":", separator, text->widget,
			(double)text->scale);
		separator = ",";
		menu_layout_write_wide(file, text->text);
		menu_layout_write_rectangle(file, "box", &text->box);
		menu_layout_write_rectangle(file, "clip", &text->clip);
		menu_layout_write_rectangle(file, "extent", text->drawn ? &text->extent : NULL);
		menu_layout_write_rectangle(file, "whole", text->laid ? &text->whole : NULL);
		menu_layout_write_rectangle(file, "wide", text->widened ? &text->wide : NULL);
		fputc('}', file);
	}
	for (index = 0; index < menu_layout.glyph_count; index++)
	{
		struct menu_layout_glyph_record const *glyph = &menu_layout.glyphs[index];

		fprintf(file, "%s\n{\"kind\":\"glyph\",\"widget\":%ld,\"key\":%s,\"name\":", separator, glyph->widget,
			glyph->key ? "true" : "false");
		separator = ",";
		menu_layout_write_string(file, glyph->name);
		menu_layout_write_rectangle(file, "box", &glyph->box);
		fputc('}', file);
	}
	for (index = 0; index < menu_layout.target_count; index++)
	{
		struct menu_layout_target_record const *target = &menu_layout.targets[index];

		fprintf(file, "%s\n{\"kind\":\"target\",\"widget\":%ld,\"target\":%d", separator, target->widget,
			target->kind);
		separator = ",";
		menu_layout_write_rectangle(file, "box", &target->box);
		fputc('}', file);
	}
	fputs("],\"screen_name\":", file);
	menu_layout_write_string(file, menu_layout.screen_name);
	fputs("}\n", file);
	written = fclose(file) == 0;
	if (!written || rename(menu_layout.temporary, menu_layout.path) != 0)
		remove(menu_layout.temporary);
}

void menu_layout_frame_end(
	char const *screen_name)
{
	if (!menu_layout.recording)
		return;
	snprintf(menu_layout.screen_name, sizeof(menu_layout.screen_name), "%s", screen_name ? screen_name : "");
	menu_layout.recording = FALSE;
	if (menu_layout.writing)
		menu_layout_write();
	menu_layout.writing = FALSE;
}

/* ---------- recording */

void menu_layout_widget(
	void const *widget,
	void const *parent,
	char const *name,
	short type,
	rectangle2d const *box,
	rectangle2d const *clip,
	int focused)
{
	struct menu_layout_widget_record *record;

	if (!menu_layout.recording || menu_layout.widget_count >= MENU_LAYOUT_MAXIMUM_WIDGETS)
		return;
	record = &menu_layout.widgets[menu_layout.widget_count];
	record->widget = widget;
	record->parent = menu_layout_widget_id(parent);
	record->type = type;
	record->focused = focused != 0;
	record->box = *box;
	record->clipped = clip != NULL;
	if (clip)
		record->clip = *clip;
	snprintf(record->name, sizeof(record->name), "%s", name ? name : "");
	menu_layout.current = menu_layout.widget_count++;
}

void menu_layout_widget_none(
	void)
{
	menu_layout.current = -1;
}

/* a rectangle laid out before a scale about its origin, as drawn */
static void menu_layout_scaled(
	rectangle2d const *rectangle,
	real scale,
	real origin_x,
	real origin_y,
	rectangle2d *scaled)
{
	scaled->x0 = (short)(origin_x + (rectangle->x0 - origin_x) * scale);
	scaled->x1 = (short)(origin_x + (rectangle->x1 - origin_x) * scale);
	scaled->y0 = (short)(origin_y + (rectangle->y0 - origin_y) * scale);
	scaled->y1 = (short)(origin_y + (rectangle->y1 - origin_y) * scale);
}

void menu_layout_text(
	rectangle2d const *box,
	rectangle2d const *clip,
	rectangle2d const *extent,
	rectangle2d const *whole,
	rectangle2d const *wide,
	wchar_t const *text,
	real scale,
	real origin_x,
	real origin_y)
{
	struct menu_layout_text_record *record;
	long index;

	if (!menu_layout.recording || menu_layout.text_count >= MENU_LAYOUT_MAXIMUM_TEXTS)
		return;
	if (scale <= 0.0f)
		scale = 1.0f;
	record = &menu_layout.texts[menu_layout.text_count++];
	record->widget = menu_layout.current;
	record->scale = scale;
	menu_layout_scaled(box, scale, origin_x, origin_y, &record->box);
	menu_layout_scaled(clip, scale, origin_x, origin_y, &record->clip);
	/* (none for a string of nothing but spaces) */
	record->drawn = extent->x1 >= extent->x0;
	if (record->drawn)
		menu_layout_scaled(extent, scale, origin_x, origin_y, &record->extent);
	record->laid = whole->x1 >= whole->x0;
	if (record->laid)
		menu_layout_scaled(whole, scale, origin_x, origin_y, &record->whole);
	record->widened = wide->x1 >= wide->x0;
	if (record->widened)
		menu_layout_scaled(wide, scale, origin_x, origin_y, &record->wide);
	for (index = 0; text && text[index] && index < MENU_LAYOUT_TEXT_LENGTH - 1; index++)
		record->text[index] = text[index];
	record->text[index] = 0;
}

void menu_layout_glyph(
	int key,
	char const *name,
	short x0,
	short y0,
	short x1,
	short y1)
{
	struct menu_layout_glyph_record *record;

	if (!menu_layout.recording || menu_layout.glyph_count >= MENU_LAYOUT_MAXIMUM_GLYPHS)
		return;
	record = &menu_layout.glyphs[menu_layout.glyph_count++];
	record->widget = menu_layout.current;
	record->key = key != 0;
	record->box.x0 = x0;
	record->box.y0 = y0;
	record->box.x1 = x1;
	record->box.y1 = y1;
	snprintf(record->name, sizeof(record->name), "%s", name ? name : "");
}

void menu_layout_glyph_wide(
	int key,
	wchar_t const *name,
	short x0,
	short y0,
	short x1,
	short y1)
{
	char narrow[64];
	long index;

	for (index = 0; name && name[index] && index < (long)sizeof(narrow) - 1; index++)
		narrow[index] = name[index] < 0x80 ? (char)name[index] : '?';
	narrow[index] = 0;
	menu_layout_glyph(key, narrow, x0, y0, x1, y1);
}

void menu_layout_target(
	void const *widget,
	short kind,
	rectangle2d const *box)
{
	struct menu_layout_target_record *record;

	if (!menu_layout.recording || menu_layout.target_count >= MENU_LAYOUT_MAXIMUM_TARGETS)
		return;
	record = &menu_layout.targets[menu_layout.target_count++];
	record->widget = menu_layout_widget_id(widget);
	record->kind = kind;
	record->box = *box;
}

/* ---------- the checks (tools/menu_gallery.py's check_layout, at its
thresholds), for the menu studio's overlay */

/* a phone in landscape, in the menus' units (the gallery's touch-phone: an
iPhone 15 Pro's safe area, 59 points at each side and 21 at the foot, and
7 mm, at 2400x1080 and 421.6 pixels an inch) */
#define MENU_LAYOUT_PHONE_SIDE 72.0f
#define MENU_LAYOUT_PHONE_FOOT 25.6f
#define MENU_LAYOUT_PHONE_LEAST_TARGET 51.6f

static short menu_layout_beyond(
	rectangle2d const *inner,
	rectangle2d const *outer)
{
	short most = 0;

	most = MAX(most, (short)(outer->x0 - inner->x0));
	most = MAX(most, (short)(outer->y0 - inner->y0));
	most = MAX(most, (short)(inner->x1 - outer->x1));
	most = MAX(most, (short)(inner->y1 - outer->y1));

	return most;
}

/* the part of a text's letters its clip lets show; FALSE for none */
static boolean menu_layout_visible(
	struct menu_layout_text_record const *text,
	rectangle2d *area)
{
	area->x0 = MAX(text->extent.x0, text->clip.x0);
	area->y0 = MAX(text->extent.y0, text->clip.y0);
	area->x1 = MIN(text->extent.x1, text->clip.x1);
	area->y1 = MIN(text->extent.y1, text->clip.y1);

	return text->drawn && area->x1 > area->x0 && area->y1 > area->y0;
}

/* a text with letters, not only spaces */
static boolean menu_layout_text_shown(
	struct menu_layout_text_record const *text)
{
	long index;

	if (!text->drawn)
		return FALSE;
	for (index = 0; text->text[index]; index++)
	{
		if (text->text[index] != ' ' && text->text[index] != '\t' && text->text[index] != '\r' &&
			text->text[index] != '\n')
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* a controller's button the keyboard has a key for in the menus */
static boolean menu_layout_button_with_key(
	char const *name)
{
	static char const *const buttons[] = { "a-button", "b-button", "x-button", "y-button", "black-button",
		"white-button", "left-trigger", "right-trigger", "back-button" };
	long index;

	for (index = 0; index < (long)NUMBEROF(buttons); index++)
	{
		if (!strcmp(name, buttons[index]))
			return TRUE;
	}

	return FALSE;
}

long menu_layout_overlay(
	menu_layout_outline_proc outline,
	long *counts)
{
	rectangle2d screen, safe, area, other, overlap;
	boolean phone = ui_widget_port_touch_menus() != 0;
	boolean keyboard = halo_keyboard_prompts() != 0;
	long index, second, problems = 0;

	csmemset(counts, 0, NUMBER_OF_MENU_LAYOUT_PROBLEMS * sizeof(long));
	screen.x0 = (short)(-(menu_layout.screen_width - 640) / 2);
	screen.x1 = (short)(640 + (menu_layout.screen_width - 640) / 2);
	screen.y0 = 0;
	screen.y1 = 480;
	safe = screen;
	safe.x0 = (short)(screen.x0 + MENU_LAYOUT_PHONE_SIDE);
	safe.x1 = (short)(screen.x1 - MENU_LAYOUT_PHONE_SIDE);
	safe.y1 = (short)(480 - MENU_LAYOUT_PHONE_FOOT);
	for (index = 0; index < menu_layout.widget_count; index++)
		outline(&menu_layout.widgets[index].box, _menu_layout_box);
#define MENU_LAYOUT_PROBLEM(kind, rectangle) (outline((rectangle), (kind)), counts[(kind)]++, problems++)
	for (index = 0; index < menu_layout.text_count; index++)
	{
		struct menu_layout_text_record const *text = &menu_layout.texts[index];

		if (!menu_layout_text_shown(text))
			continue;
		if (menu_layout_beyond(&text->extent, &text->clip) > 1)
			MENU_LAYOUT_PROBLEM(_menu_layout_clipped, &text->extent);
		else if (text->extent.x1 > text->box.x1 + 2 || text->extent.x0 < text->box.x0 - 2 ||
			(text->box.y1 - text->box.y0 >= 8 && text->extent.y1 > text->box.y1 + 4))
		{
			MENU_LAYOUT_PROBLEM(_menu_layout_overflow, &text->extent);
		}
		if (text->laid && text->whole.y1 > text->extent.y1 + 2)
		{
			area = text->box;
			area.y0 = text->box.y1;
			area.y1 = text->whole.y1;
			MENU_LAYOUT_PROBLEM(_menu_layout_cut, &area);
		}
		/* (a line, laid out without the right edge, the same height and
		wider: its last letters not drawn) */
		else if (text->widened && text->wide.y1 == text->extent.y1 &&
			text->wide.x1 - text->wide.x0 > text->extent.x1 - text->extent.x0 + 2)
		{
			MENU_LAYOUT_PROBLEM(_menu_layout_cut, &text->extent);
		}
		if (menu_layout_beyond(&text->extent, &screen) > 0)
			MENU_LAYOUT_PROBLEM(_menu_layout_offscreen, &text->extent);
		if (!menu_layout_visible(text, &area))
			continue;
		if (phone && menu_layout_beyond(&area, &safe) > 1 && menu_layout_beyond(&area, &screen) <= 0)
			MENU_LAYOUT_PROBLEM(_menu_layout_safe_area, &area);
		for (second = index + 1; second < menu_layout.text_count; second++)
		{
			struct menu_layout_text_record const *next = &menu_layout.texts[second];

			if (next->widget == text->widget || !menu_layout_text_shown(next) || !menu_layout_visible(next, &other))
				continue;
			overlap.x0 = MAX(area.x0, other.x0);
			overlap.y0 = MAX(area.y0, other.y0);
			overlap.x1 = MIN(area.x1, other.x1);
			overlap.y1 = MIN(area.y1, other.y1);
			if (overlap.x1 - overlap.x0 > 2 && overlap.y1 - overlap.y0 > 3)
				MENU_LAYOUT_PROBLEM(_menu_layout_overlap, &overlap);
		}
	}
	for (index = 0; index < menu_layout.glyph_count; index++)
	{
		struct menu_layout_glyph_record const *glyph = &menu_layout.glyphs[index];

		if (glyph->box.x1 <= glyph->box.x0)
			continue;
		if (menu_layout_beyond(&glyph->box, &screen) > 0)
			MENU_LAYOUT_PROBLEM(_menu_layout_offscreen, &glyph->box);
		if ((keyboard && !glyph->key && menu_layout_button_with_key(glyph->name)) || (!keyboard && glyph->key))
			MENU_LAYOUT_PROBLEM(_menu_layout_glyphs, &glyph->box);
		if (phone && menu_layout_beyond(&glyph->box, &safe) > 1)
			MENU_LAYOUT_PROBLEM(_menu_layout_safe_area, &glyph->box);
	}
	for (index = 0; phone && index < menu_layout.target_count; index++)
	{
		rectangle2d const *box = &menu_layout.targets[index].box;
		short width = (short)(box->x1 - box->x0), height = (short)(box->y1 - box->y0);

		if (width > 0 && height > 0 && (real)MIN(width, height) + 0.5f < MENU_LAYOUT_PHONE_LEAST_TARGET)
			MENU_LAYOUT_PROBLEM(_menu_layout_touch_target, box);
	}
#undef MENU_LAYOUT_PROBLEM

	return problems;
}

/* ---------- the widgets of the kept frame, for the menu studio's notes */

long menu_layout_widget_at(
	short x,
	short y)
{
	long index;

	/* (the last drawn that holds the point: the deepest, the one on top;
	those over the whole screen passed over, as the screen's own) */
	for (index = menu_layout.widget_count - 1; index >= 0; index--)
	{
		rectangle2d const *box = &menu_layout.widgets[index].box;

		if (x >= box->x0 && x < box->x1 && y >= box->y0 && y < box->y1 &&
			(box->x1 - box->x0 < 640 || box->y1 - box->y0 < 480))
		{
			return index;
		}
	}

	return -1;
}

long menu_layout_widget_of(
	void const *widget)
{
	return menu_layout_widget_id(widget);
}

long menu_layout_widget_named(
	char const *name)
{
	long index;

	for (index = 0; index < menu_layout.widget_count; index++)
	{
		if (!strcmp(menu_layout.widgets[index].name, name))
			return index;
	}

	return -1;
}

boolean menu_layout_widget_box(
	long id,
	rectangle2d *box)
{
	if (id < 0 || id >= menu_layout.widget_count)
		return FALSE;
	*box = menu_layout.widgets[id].box;

	return TRUE;
}

char const *menu_layout_widget_name(
	long id)
{
	return id >= 0 && id < menu_layout.widget_count ? menu_layout.widgets[id].name : "";
}

/* (into a buffer, its end kept) */
static void menu_layout_append(
	char *json,
	long size,
	char const *text)
{
	long length = (long)strlen(json);

	snprintf(json + length, (size_t)MAX(size - length, 0), "%s", text);
}

static void menu_layout_append_escaped(
	char *json,
	long size,
	char const *text)
{
	char one[8];
	long index;

	for (index = 0; text[index]; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character == '"' || character == '\\')
			snprintf(one, sizeof(one), "\\%c", character);
		else if (character < 0x20)
			snprintf(one, sizeof(one), "\\u%04x", character);
		else
			snprintf(one, sizeof(one), "%c", character);
		menu_layout_append(json, size, one);
	}
}

void menu_layout_widget_json(
	long id,
	char *json,
	long size)
{
	char const *path[32];
	char part[64];
	long depth = 0, step, index;

	json[0] = 0;
	if (id < 0 || id >= menu_layout.widget_count)
	{
		menu_layout_append(json, size, "\"widget\":null,\"widget_path\":[],\"widget_text\":\"\",\"box\":null");
		return;
	}
	menu_layout_append(json, size, "\"widget\":\"");
	menu_layout_append_escaped(json, size, menu_layout.widgets[id].name);
	menu_layout_append(json, size, "\",\"widget_path\":[");
	for (step = id; step >= 0 && depth < 32; step = menu_layout.widgets[step].parent)
		path[depth++] = menu_layout.widgets[step].name;
	for (index = depth - 1; index >= 0; index--)
	{
		menu_layout_append(json, size, index == depth - 1 ? "\"" : ",\"");
		menu_layout_append_escaped(json, size, path[index]);
		menu_layout_append(json, size, "\"");
	}
	menu_layout_append(json, size, "],\"widget_text\":\"");
	/* (the words it and its children draw) */
	for (index = 0; index < menu_layout.text_count; index++)
	{
		long owner;
		char narrow[MENU_LAYOUT_TEXT_LENGTH];
		long character;

		for (owner = menu_layout.texts[index].widget; owner >= 0 && owner != id; owner = menu_layout.widgets[owner].parent)
			;
		if (owner != id)
			continue;
		for (character = 0; menu_layout.texts[index].text[character]; character++)
		{
			wchar_t wide = menu_layout.texts[index].text[character];

			narrow[character] = wide == '\r' || wide == '\n' ? ' ' : wide < 0x80 ? (char)wide : '?';
		}
		narrow[character] = 0;
		menu_layout_append_escaped(json, size, narrow);
		menu_layout_append(json, size, " ");
	}
	snprintf(part, sizeof(part), "\",\"box\":[%d,%d,%d,%d]", menu_layout.widgets[id].box.x0,
		menu_layout.widgets[id].box.y0, menu_layout.widgets[id].box.x1, menu_layout.widgets[id].box.y1);
	menu_layout_append(json, size, part);
}
