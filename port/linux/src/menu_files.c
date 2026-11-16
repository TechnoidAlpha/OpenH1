/*
MENU_FILES.C

The menus written in XML (halo_menus.h): the files embedded from
port/assets/menus (menu_files.h), each replaced by a file of the same name
in a menus folder beside config.toml, and on the desktop any other .xml file
there too, read with Expat (port/third_party/expat) into the description the
game builds its widget tags from (port/linux/game/menu_tags.c).

Only the files' shape is checked here: their elements, which attributes
each may have, and numbers; the game checks what the names mean, against
the loaded map. Any problem is logged with its file and line, and then no
file is used: the game keeps its own menus.

Also the menus' art: the PNGs of their bitmaps' frames, each drawn for the
D3D texture the game made for it (menu_art_texture, xbox_textures.c), as the
high-res HUD's are (hud_hires.c).
*/

#include "xgpu_backend.h"
#include "halo_menus.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "embedded_assets.h"
#include "platform.h"
#include "port_config.h"
/* (the renderer's backend is on the host: the pictures are images) */
#include "xgpu_images.h"
#ifdef HALO_VR
#include "vr.h"
#endif

#include "expat.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXIMUM_DEPTH 32
#define MAXIMUM_ART 1024

/* ---------- the files */

struct menu_file
{
	char *path;
	const unsigned char *data;
	unsigned long size;
	int loaded;
};

static struct menu_file *files;
static long file_count;
static char folder[1024];

static long file_find(const char *path)
{
	long index;

	for (index = 0; index < file_count; index++)
	{
		if (!strcmp(files[index].path, path))
			return index;
	}
	return -1;
}

/* the file's index, or -1 when out of memory (and a loaded file's data is
freed) */
static long file_add(const char *path, const unsigned char *data, unsigned long size, int loaded)
{
	long index = file_find(path);

	if (index < 0)
	{
		struct menu_file *grown = realloc(files, (file_count + 1) * sizeof(*files));
		char *name = strdup(path);

		if (!grown || !name)
		{
			platform_log("menus: out of memory for %s", path);
			if (grown)
				files = grown;
			free(name);
			if (loaded)
				free((void *)data);
			return -1;
		}
		files = grown;
		index = file_count++;
		files[index].path = name;
	}
	else if (files[index].loaded)
	{
		free((void *)files[index].data);
	}
	files[index].data = data;
	files[index].size = size;
	files[index].loaded = loaded;
	return index;
}

static unsigned char *file_read(const char *path, unsigned long *size)
{
#ifdef HALO_GUEST
	FILE *file = fopen(path, "rb");
	unsigned char *data = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0)
	{
		data = malloc((size_t)length + 1);
		if (data && fread(data, 1, (size_t)length, file) == (size_t)length)
		{
			*size = (unsigned long)length;
		}
		else
		{
			free(data);
			data = NULL;
		}
	}
	fclose(file);
	return data;
#else
	size_t length = 0;
	void *loaded = SDL_LoadFile(path, &length);
	unsigned char *data;

	if (!loaded)
		return NULL;
	data = malloc(length + 1);
	if (data)
		memcpy(data, loaded, length);
	SDL_free(loaded);
	*size = (unsigned long)length;
	return data;
#endif
}

/* the embedded files, then those of the menus folder: one of the same name
replaces an embedded one, and (on the desktop, where the folder can be
listed) another .xml file is added */
static void files_gather(void)
{
	unsigned int index;
	char path[1200];

	/* (or debug.menu_files' folder: a source tree's, for the menu studio) */
	if (config_string("debug.menu_files")[0])
	{
		snprintf(folder, sizeof(folder), "%s", config_string("debug.menu_files"));
	}
	else
	{
		config_folder(folder, sizeof(folder));
		snprintf(folder + strlen(folder), sizeof(folder) - strlen(folder), "menus");
	}
	for (index = 0; index < menu_files_embedded_count; index++)
	{
		const struct menu_file_embedded *embedded = &menu_files_embedded[index];
		unsigned long size = 0;
		unsigned char *data;

		snprintf(path, sizeof(path), "%s/%s", folder, embedded->path);
		data = file_read(path, &size);
		if (data)
		{
			platform_log("menus: %s replaces the built-in one", path);
			file_add(embedded->path, data, size, 1);
		}
		else
		{
			/* (the game's own, in the assets' file: embedded_assets.h) */
			const unsigned char *own = (const unsigned char *)embedded_assets_data(embedded->data_offset,
				embedded->size);

			if (own)
				file_add(embedded->path, own, embedded->size, 0);
		}
	}
#ifndef HALO_ANDROID
	{
		/* (the folder's own and its folders': SDL's * does not cross a /) */
		static const char *const patterns[] = { "*.xml", "*/*.xml" };
		int pattern;

		for (pattern = 0; pattern < 2; pattern++)
		{
		int count = 0;
		char **names = SDL_GlobDirectory(folder, patterns[pattern], 0, &count);
		int name;

		for (name = 0; names && name < count; name++)
		{
			unsigned long size = 0;
			unsigned char *data;

			if (file_find(names[name]) >= 0)
				continue;
			snprintf(path, sizeof(path), "%s/%s", folder, names[name]);
			data = file_read(path, &size);
			if (data)
			{
				platform_log("menus: adding %s", path);
				file_add(names[name], data, size, 1);
			}
		}
		SDL_free(names);
		}
	}
#endif
}

/* whether a bitmap's path stays inside the menus folder: relative, with no
drive (c:) and no .. (nor Windows' spellings of it, such as ".. ") */
static int path_inside_folder(const char *path)
{
	const char *component = path;

	if (!*path || *path == '/' || *path == '\\' || strchr(path, ':'))
		return 0;
	while (*component)
	{
		size_t length = strcspn(component, "/\\");

		/* (only dots and spaces, but a lone .) */
		if (length && strspn(component, ". ") == length && !(length == 1 && *component == '.'))
			return 0;
		component += length;
		component += strspn(component, "/\\");
	}
	return 1;
}

/* a file's data (a bitmap's PNG, by the path its <bitmap> gives): the
menus folder's, else the embedded one */
static const unsigned char *file_data(const char *path, unsigned long *size)
{
	long index;

	if (!path_inside_folder(path))
	{
		platform_log("menus: %s is not a path inside the menus folder", path);
		return NULL;
	}
	index = file_find(path);
	if (index < 0)
	{
		char full[1200];
		unsigned char *data;

		snprintf(full, sizeof(full), "%s/%s", folder, path);
		data = file_read(full, size);
		if (!data)
			return NULL;
		index = file_add(path, data, *size, 1);
		if (index < 0)
			return NULL;
	}
	*size = files[index].size;
	return files[index].data;
}

/* ---------- reading */

enum element
{
	_element_none,
	_element_menus,
	_element_bitmap,
	_element_frame,
	_element_strings,
	_element_string,
	_element_widget,
	_element_child,
	_element_on,
	_element_data,
	_element_conditional,
	_element_replace,
};

/* an attribute an element may have: its text (text) or number (number,
with given set when it is there) */
struct attribute
{
	const char *name;
	const char **text;
	long *number;
	long *given;
};

struct reader
{
	struct halo_menus menus;
	const char *file;
	XML_Parser parser;
	int failed;
	/* the open elements, and the widget, bitmap or strings each is in */
	enum element elements[MAXIMUM_DEPTH];
	long owners[MAXIMUM_DEPTH];
	int depth;
	/* how deep inside an element for the other platform we are (0: not) */
	int skipping;
	/* the open <bitmap> has frames= (its <frame>s cannot be added too) */
	int bitmap_frames_attribute;
	/* each widget's last child, handler, input, conditional and replace,
	for adding the next */
	long *last_child, *last_handler, *last_input, *last_conditional, *last_replace;
};

static void reader_error(struct reader *reader, const char *format, ...)
{
	char message[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	if (!reader->failed)
	{
		platform_log("menus: %s:%lu: %s", reader->file,
			(unsigned long)XML_GetCurrentLineNumber(reader->parser), message);
	}
	reader->failed = 1;
	XML_StopParser(reader->parser, XML_FALSE);
}

static char *copy_length(const char *text, size_t length)
{
	char *result = malloc(length + 1);

	if (result)
	{
		memcpy(result, text, length);
		result[length] = 0;
	}
	return result;
}

static char *copy(const char *text)
{
	return text ? copy_length(text, strlen(text)) : NULL;
}

static int parse_long(struct reader *reader, const char *name, const char *text, long *value)
{
	char *end;

	errno = 0;
	*value = strtol(text, &end, 10);
	if (!*text || *end)
	{
		reader_error(reader, "%s=\"%s\" is not a whole number", name, text);
		return 0;
	}
	/* (the tags keep them in shorts) */
	if (errno == ERANGE || *value < -32768 || *value > 32767)
	{
		reader_error(reader, "%s=\"%s\" is out of range (-32768 to 32767)", name, text);
		return 0;
	}
	return 1;
}

/* the element's attributes into the table's fields; one not in the table
(but platform, which for_this_platform reads, and source, which only a
widget keeps) is an error */
static void read_attributes(struct reader *reader, const char *element, const XML_Char **attributes,
	const struct attribute *table, size_t count)
{
	int index;

	for (index = 0; attributes[index] && !reader->failed; index += 2)
	{
		const char *name = attributes[index], *value = attributes[index + 1];
		size_t field;

		if (!strcmp(name, "platform"))
			continue;
		for (field = 0; field < count; field++)
		{
			if (strcmp(name, table[field].name))
				continue;
			if (table[field].text)
			{
				*table[field].text = copy(value);
				if (!*table[field].text)
					reader_error(reader, "out of memory");
			}
			else if (parse_long(reader, name, value, table[field].number) && table[field].given)
				*table[field].given = 1;
			break;
		}
		/* (source, where a generator says it made the element, for the
		menu review tools, tools/menu_review.py, and debug.render_marks: a
		widget's is kept, any other's is not read) */
		if (field == count && strcmp(name, "source"))
			reader_error(reader, "<%s> has no attribute %s", element, name);
	}
}

static long current_line(struct reader *reader)
{
	return (long)XML_GetCurrentLineNumber(reader->parser);
}

/* this build's platform, as the platform attribute names it */
#ifdef HALO_ANDROID
#define MENU_PLATFORM "android"
#else
#define MENU_PLATFORM "desktop"
#endif

/* whether the menus are laid out for a touchscreen (the Android and iOS
apps, or debug.touch_menus): the menus' "touch" platform, the others'
"pointer" */
#if defined(HALO_ANDROID) || defined(HALO_IOS)
#define MENU_TOUCH 1
#else
#define MENU_TOUCH config_boolean("debug.touch_menus")
#endif

/* whether a platform of the menus' is this one (1, 0; -1 not a platform's
name): "vr" the VR build, playing in the headset; "hdr" a renderer that
draws display.hdr: xgpu_backend.h; "touch" the touchscreen's builds,
"pointer" the rest: MENU_TOUCH; "computer" the desktop's builds but iOS's,
whose phone has no window to size nor mouse */
int halo_menus_platform(const char *name, unsigned long length)
{
	if ((length != 7 || strncmp(name, "desktop", 7)) && (length != 7 || strncmp(name, "android", 7)) &&
		(length != 2 || strncmp(name, "vr", 2)) && (length != 3 || strncmp(name, "hdr", 3)) &&
		(length != 5 || strncmp(name, "touch", 5)) && (length != 7 || strncmp(name, "pointer", 7)) &&
		(length != 8 || strncmp(name, "computer", 8)))
	{
		return -1;
	}
#if !defined(HALO_ANDROID) && !defined(HALO_IOS)
	if (length == 8 && !strncmp(name, "computer", 8))
		return 1;
#endif
	if (length == 5 && !strncmp(name, "touch", 5) && MENU_TOUCH)
		return 1;
	if (length == 7 && !strncmp(name, "pointer", 7) && !MENU_TOUCH)
		return 1;
	if (length == 3 && !strncmp(name, "hdr", 3) && xgpu_backend_capabilities.hdr)
		return 1;
	if (length == strlen(MENU_PLATFORM) && !strncmp(name, MENU_PLATFORM, length))
		return 1;
#ifdef HALO_VR
	if (length == 2 && !strncmp(name, "vr", 2) && halo_vr_running())
		return 1;
#endif
	return 0;
}

/* whether the element is for this platform (its platform attribute: the
platforms it is for, separated by spaces) */
static int for_this_platform(struct reader *reader, const XML_Char **attributes)
{
	int index;

	for (index = 0; attributes[index]; index += 2)
	{
		if (!strcmp(attributes[index], "platform"))
		{
			const char *name = attributes[index + 1];
			int found = 0;

			while (*name)
			{
				size_t length = strcspn(name, " ");

				if (length)
				{
					int platform = halo_menus_platform(name, length);

					if (platform < 0)
					{
						reader_error(reader, "platform=\"%s\" names one that is not desktop, android, vr, hdr, "
							"touch, pointer or computer", attributes[index + 1]);
						return 1;
					}
					if (platform)
						found = 1;
				}
				name += length;
				while (*name == ' ')
					name++;
			}
			return found;
		}
	}
	return 1;
}

/* room for one more element in an array of count; NULL when out of memory,
which fails the reading (the array is then as it was) */
static void *grow(struct reader *reader, void *array, long count, size_t size)
{
	void *grown = realloc(array, (size_t)(count + 1) * size);

	if (!grown)
		reader_error(reader, "out of memory");
	return grown;
}

static int grow_last(struct reader *reader, long **array, long count)
{
	long *grown = grow(reader, *array, count, sizeof(**array));

	if (!grown)
		return 0;
	grown[count] = HALO_MENU_NONE;
	*array = grown;
	return 1;
}

/* room in array for one more of its count elements, else the reading
function returns (out of memory: the reading has failed) */
#define GROW(reader, array, count) \
	do \
	{ \
		void *grown = grow((reader), (array), (count), sizeof(*(array))); \
		if (!grown) \
			return; \
		(array) = grown; \
	} while (0)

/* adds index to a widget's list (first, and each item's next) */
#define LIST_ADD(reader, owner, first, last, items, index) \
	do \
	{ \
		if ((reader)->last[owner] == HALO_MENU_NONE) \
			(reader)->menus.widgets[owner].first = (index); \
		else \
			(reader)->menus.items[(reader)->last[owner]].next = (index); \
		(reader)->last[owner] = (index); \
	} while (0)

static void read_bitmap(struct reader *reader, const XML_Char **attributes)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_bitmap bitmap;
	const char *frames = NULL;
	long width = 0, height = 0;
	struct attribute table[] =
	{
		{ "name", &bitmap.name }, { "frames", &frames }, { "width", NULL, &width }, { "height", NULL, &height },
	};

	memset(&bitmap, 0, sizeof(bitmap));
	bitmap.first_frame = menus->frame_count;
	bitmap.file = reader->file;
	bitmap.line = current_line(reader);
	read_attributes(reader, "bitmap", attributes, table, sizeof(table) / sizeof(*table));
	reader->bitmap_frames_attribute = frames != NULL;
	/* (frames="a.png b.png", all width by height, or <frame>s) */
	while (frames && *frames && !reader->failed)
	{
		struct halo_menu_frame *grown;
		size_t length;

		frames += strspn(frames, " \t\r\n");
		length = strcspn(frames, " \t\r\n");
		if (!length)
			break;
		/* (a bitmap's count of frames is a short of the game's) */
		if (bitmap.frame_count >= 32767)
		{
			reader_error(reader, "a bitmap has too many frames");
			break;
		}
		grown = grow(reader, menus->frames, menus->frame_count, sizeof(*menus->frames));
		if (!grown)
			break;
		menus->frames = grown;
		memset(&menus->frames[menus->frame_count], 0, sizeof(*menus->frames));
		menus->frames[menus->frame_count].png = copy_length(frames, length);
		if (!menus->frames[menus->frame_count].png)
		{
			reader_error(reader, "out of memory");
			break;
		}
		menus->frames[menus->frame_count].width = width;
		menus->frames[menus->frame_count].height = height;
		menus->frame_count++;
		bitmap.frame_count++;
		frames += length;
	}
	if (!reader->failed && !bitmap.name)
		reader_error(reader, "a <bitmap> needs a name");
	GROW(reader, menus->bitmaps, menus->bitmap_count);
	menus->bitmaps[menus->bitmap_count++] = bitmap;
}

static void read_frame(struct reader *reader, const XML_Char **attributes, long owner)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_frame frame;
	struct attribute table[] =
	{
		{ "png", &frame.png }, { "map", &frame.map }, { "index", NULL, &frame.index },
		{ "width", NULL, &frame.width }, { "height", NULL, &frame.height },
		{ "x", NULL, &frame.x }, { "y", NULL, &frame.y },
	};

	memset(&frame, 0, sizeof(frame));
	frame.index = -1;
	read_attributes(reader, "frame", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && (frame.png != NULL) == (frame.map != NULL))
		reader_error(reader, "a <frame> has a png or a map bitmap, not both");
	else if (!reader->failed && frame.png && (frame.width <= 0 || frame.height <= 0))
		reader_error(reader, "a <frame> with a png needs width and height");
	else if (!reader->failed && frame.map && frame.index < 0)
		reader_error(reader, "a <frame> of a map bitmap needs its index");
	else if (!reader->failed && frame.map && (frame.width < 0 || frame.height < 0 || !frame.width != !frame.height))
		reader_error(reader, "a <frame> of a map bitmap is scaled to a width and a height, or neither");
	else if (!reader->failed && (frame.x || frame.y) && (!frame.map || !frame.width))
		reader_error(reader, "only a scaled <frame> of a map bitmap is placed at an x and y");
	if (!reader->failed && reader->bitmap_frames_attribute)
		reader_error(reader, "a bitmap's <frame>s and frames= cannot be mixed");
	if (frame.index < 0)
		frame.index = 0;
	if (menus->bitmaps[owner].frame_count >= 32767)
	{
		reader_error(reader, "a bitmap has too many frames");
		return;
	}
	GROW(reader, menus->frames, menus->frame_count);
	menus->frames[menus->frame_count++] = frame;
	menus->bitmaps[owner].frame_count++;
}

static void read_strings(struct reader *reader, const XML_Char **attributes)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_strings strings;
	struct attribute table[] = { { "name", &strings.name }, { "map", &strings.map } };

	memset(&strings, 0, sizeof(strings));
	strings.first = menus->string_count;
	strings.file = reader->file;
	strings.line = current_line(reader);
	read_attributes(reader, "strings", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !strings.name)
		reader_error(reader, "a <strings> needs a name");
	GROW(reader, menus->string_lists, menus->string_list_count);
	menus->string_lists[menus->string_list_count++] = strings;
}

static void read_string(struct reader *reader, const XML_Char **attributes, long owner)
{
	struct halo_menus *menus = &reader->menus;
	const char *text = NULL;
	long map = HALO_MENU_NONE, map_given = 0;
	struct attribute table[] = { { "text", &text }, { "map", NULL, &map, &map_given } };

	read_attributes(reader, "string", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !text)
		reader_error(reader, "a <string> needs text");
	if (!reader->failed && map_given && (map < 0 || !menus->string_lists[owner].map))
		reader_error(reader, "a <string>'s map= is an index in its <strings>' map list");
	GROW(reader, menus->string_maps, menus->string_count);
	menus->string_maps[menus->string_count] = map_given ? map : HALO_MENU_NONE;
	GROW(reader, menus->strings, menus->string_count);
	menus->strings[menus->string_count++] = text ? text : "";
	menus->string_lists[owner].count++;
}

static void child_add(struct reader *reader, long parent, long nested, const char *widget, long x, long y,
	const char *controller)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_child child;
	long index = menus->child_count;

	memset(&child, 0, sizeof(child));
	child.nested = nested;
	child.widget = widget;
	child.x = x;
	child.y = y;
	child.controller = controller;
	child.next = HALO_MENU_NONE;
	child.file = reader->file;
	child.line = current_line(reader);
	GROW(reader, menus->children, menus->child_count);
	menus->children[menus->child_count++] = child;
	LIST_ADD(reader, parent, first_child, last_child, children, index);
}

static void read_widget(struct reader *reader, const XML_Char **attributes, long parent)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_widget widget;
	long index = menus->widget_count;
	const char *child_controller = NULL;
	struct attribute table[] =
	{
		{ "name", &widget.name }, { "type", &widget.type }, { "controller", &widget.controller },
		{ "flags", &widget.flags }, { "bitmap", &widget.bitmap }, { "text", &widget.text },
		{ "strings", &widget.strings }, { "values", &widget.values }, { "setting", &widget.setting },
		{ "font", &widget.font }, { "color", &widget.color }, { "align", &widget.align },
		{ "description", &widget.description }, { "string_list", &widget.string_list },
		{ "list_flags", &widget.list_flags }, { "text_flags", &widget.text_flags },
		{ "header_bitmap", &widget.header_bitmap }, { "footer_bitmap", &widget.footer_bitmap },
		{ "header_bounds", &widget.header_bounds }, { "footer_bounds", &widget.footer_bounds },
		{ "child_controller", &child_controller }, { "wide", &widget.wide },
		{ "x", NULL, &widget.x }, { "y", NULL, &widget.y }, { "left", NULL, &widget.left },
		{ "top", NULL, &widget.top }, { "width", NULL, &widget.width, &widget.has_width },
		{ "height", NULL, &widget.height, &widget.has_height }, { "text_x", NULL, &widget.text_x },
		{ "text_y", NULL, &widget.text_y }, { "auto_close", NULL, &widget.auto_close },
		{ "auto_close_fade", NULL, &widget.auto_close_fade }, { "string_index", NULL, &widget.string_index },
		{ "radius", NULL, &widget.radius }, { "source", &widget.source },
	};

	memset(&widget, 0, sizeof(widget));
	widget.parent = parent;
	widget.first_child = widget.first_handler = widget.first_input = HALO_MENU_NONE;
	widget.first_conditional = widget.first_replace = HALO_MENU_NONE;
	widget.file = reader->file;
	widget.line = current_line(reader);
	read_attributes(reader, "widget", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !widget.name)
		reader_error(reader, "a <widget> needs a name");
	else if (!reader->failed && (widget.radius < 0 || widget.radius > 64))
		reader_error(reader, "a <widget>'s radius is 0 to 64");
	GROW(reader, menus->widgets, index);
	menus->widgets[index] = widget;
	menus->widget_count++;
	if (!grow_last(reader, &reader->last_child, index))
		return;
	if (!grow_last(reader, &reader->last_handler, index))
		return;
	if (!grow_last(reader, &reader->last_input, index))
		return;
	if (!grow_last(reader, &reader->last_conditional, index))
		return;
	if (!grow_last(reader, &reader->last_replace, index))
		return;
	if (parent != HALO_MENU_NONE)
		child_add(reader, parent, index, NULL, widget.x, widget.y, child_controller);
}

static void read_child(struct reader *reader, const XML_Char **attributes, long owner)
{
	const char *widget = NULL, *controller = NULL;
	long x = 0, y = 0;
	struct attribute table[] =
	{
		{ "widget", &widget }, { "controller", &controller }, { "x", NULL, &x }, { "y", NULL, &y },
	};

	read_attributes(reader, "child", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !widget)
		reader_error(reader, "a <child> needs a widget");
	child_add(reader, owner, HALO_MENU_NONE, widget, x, y, controller);
}

static void read_handler(struct reader *reader, const XML_Char **attributes, long owner)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_handler handler;
	long index = menus->handler_count;
	const char *back = NULL, *branch = NULL;
	struct attribute table[] =
	{
		{ "event", &handler.event }, { "run", &handler.run }, { "script", &handler.script },
		{ "open", &handler.open }, { "replace", &handler.replace }, { "close", &handler.close },
		{ "widget", &handler.widget }, { "focus", &handler.focus }, { "reload", &handler.reload },
		{ "sound", &handler.sound }, { "otherwise", &handler.otherwise }, { "label", &handler.label },
		{ "back", &back }, { "branch", &branch },
	};

	memset(&handler, 0, sizeof(handler));
	handler.next = HALO_MENU_NONE;
	handler.file = reader->file;
	handler.line = current_line(reader);
	read_attributes(reader, "on", attributes, table, sizeof(table) / sizeof(*table));
	if ((back && strcmp(back, "true") && strcmp(back, "false")) ||
		(branch && strcmp(branch, "true") && strcmp(branch, "false")))
		reader_error(reader, "back and branch are \"true\" or \"false\"");
	handler.back = back && !strcmp(back, "true");
	handler.branch = branch && !strcmp(branch, "true");
	if (!reader->failed && !handler.event)
		reader_error(reader, "an <on> needs an event");
	GROW(reader, menus->handlers, index);
	menus->handlers[index] = handler;
	menus->handler_count++;
	LIST_ADD(reader, owner, first_handler, last_handler, handlers, index);
}

static void read_input(struct reader *reader, const XML_Char **attributes, long owner)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_input input;
	long index = menus->input_count;
	struct attribute table[] = { { "input", &input.input } };

	memset(&input, 0, sizeof(input));
	input.next = HALO_MENU_NONE;
	input.file = reader->file;
	input.line = current_line(reader);
	read_attributes(reader, "data", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !input.input)
		reader_error(reader, "a <data> needs an input");
	GROW(reader, menus->inputs, index);
	menus->inputs[index] = input;
	menus->input_count++;
	LIST_ADD(reader, owner, first_input, last_input, inputs, index);
}

static void read_conditional(struct reader *reader, const XML_Char **attributes, long owner)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_conditional conditional;
	long index = menus->conditional_count;
	const char *if_failed = NULL;
	struct attribute table[] = { { "widget", &conditional.widget }, { "if_failed", &if_failed } };

	memset(&conditional, 0, sizeof(conditional));
	conditional.next = HALO_MENU_NONE;
	conditional.file = reader->file;
	conditional.line = current_line(reader);
	read_attributes(reader, "conditional", attributes, table, sizeof(table) / sizeof(*table));
	if (if_failed && strcmp(if_failed, "true") && strcmp(if_failed, "false"))
		reader_error(reader, "if_failed is \"true\" or \"false\"");
	conditional.if_failed = if_failed && !strcmp(if_failed, "true");
	if (!reader->failed && !conditional.widget)
		reader_error(reader, "a <conditional> needs a widget");
	GROW(reader, menus->conditionals, index);
	menus->conditionals[index] = conditional;
	menus->conditional_count++;
	LIST_ADD(reader, owner, first_conditional, last_conditional, conditionals, index);
}

static void read_replace(struct reader *reader, const XML_Char **attributes, long owner)
{
	struct halo_menus *menus = &reader->menus;
	struct halo_menu_replace replace;
	long index = menus->replace_count;
	struct attribute table[] = { { "search", &replace.search }, { "function", &replace.function } };

	memset(&replace, 0, sizeof(replace));
	replace.next = HALO_MENU_NONE;
	replace.file = reader->file;
	replace.line = current_line(reader);
	read_attributes(reader, "replace", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !replace.search)
		reader_error(reader, "a <replace> needs a search");
	GROW(reader, menus->replaces, index);
	menus->replaces[index] = replace;
	menus->replace_count++;
	LIST_ADD(reader, owner, first_replace, last_replace, replaces, index);
}

static void read_menus(struct reader *reader, const XML_Char **attributes)
{
	const char *root = NULL;
	struct attribute table[] = { { "root", &root } };

	read_attributes(reader, "menus", attributes, table, sizeof(table) / sizeof(*table));
	if (root)
		reader->menus.root = root;
}

static void XMLCALL element_start(void *data, const XML_Char *name, const XML_Char **attributes)
{
	struct reader *reader = data;
	enum element parent = reader->depth ? reader->elements[reader->depth - 1] : _element_none;
	long owner = reader->depth ? reader->owners[reader->depth - 1] : HALO_MENU_NONE;
	enum element element = _element_none;

	if (reader->failed)
		return;
	if (reader->skipping || !for_this_platform(reader, attributes))
	{
		reader->skipping++;
		return;
	}
	if (reader->depth == MAXIMUM_DEPTH)
	{
		reader_error(reader, "elements nested too deeply");
		return;
	}
	if (!strcmp(name, "menus") && parent == _element_none)
	{
		element = _element_menus;
		read_menus(reader, attributes);
	}
	else if (!strcmp(name, "bitmap") && parent == _element_menus)
	{
		element = _element_bitmap;
		read_bitmap(reader, attributes);
		owner = reader->menus.bitmap_count - 1;
	}
	else if (!strcmp(name, "frame") && parent == _element_bitmap)
	{
		element = _element_frame;
		read_frame(reader, attributes, owner);
	}
	else if (!strcmp(name, "strings") && parent == _element_menus)
	{
		element = _element_strings;
		read_strings(reader, attributes);
		owner = reader->menus.string_list_count - 1;
	}
	else if (!strcmp(name, "string") && parent == _element_strings)
	{
		element = _element_string;
		read_string(reader, attributes, owner);
	}
	else if (!strcmp(name, "widget") && (parent == _element_menus || parent == _element_widget))
	{
		element = _element_widget;
		read_widget(reader, attributes, parent == _element_widget ? owner : HALO_MENU_NONE);
		owner = reader->menus.widget_count - 1;
	}
	else if (parent == _element_widget)
	{
		if (!strcmp(name, "child"))
		{
			element = _element_child;
			read_child(reader, attributes, owner);
		}
		else if (!strcmp(name, "on"))
		{
			element = _element_on;
			read_handler(reader, attributes, owner);
		}
		else if (!strcmp(name, "data"))
		{
			element = _element_data;
			read_input(reader, attributes, owner);
		}
		else if (!strcmp(name, "conditional"))
		{
			element = _element_conditional;
			read_conditional(reader, attributes, owner);
		}
		else if (!strcmp(name, "replace"))
		{
			element = _element_replace;
			read_replace(reader, attributes, owner);
		}
	}
	if (element == _element_none)
	{
		reader_error(reader, "<%s> cannot be here", name);
		return;
	}
	reader->elements[reader->depth] = element;
	reader->owners[reader->depth] = owner;
	reader->depth++;
}

static void XMLCALL element_end(void *data, const XML_Char *name)
{
	struct reader *reader = data;

	(void)name;
	if (reader->skipping)
		reader->skipping--;
	else if (reader->depth)
		reader->depth--;
}

static void XMLCALL text(void *data, const XML_Char *characters, int length)
{
	struct reader *reader = data;
	int index;

	if (reader->failed || reader->skipping)
		return;
	for (index = 0; index < length; index++)
	{
		if (!strchr(" \t\r\n", characters[index]))
		{
			reader_error(reader, "text belongs in an attribute (text=\"...\")");
			return;
		}
	}
}

static int read_file(struct reader *reader, const struct menu_file *file)
{
	reader->file = file->path;
	reader->depth = 0;
	reader->skipping = 0;
	reader->parser = XML_ParserCreate("UTF-8");
	if (!reader->parser)
	{
		platform_log("menus: %s: no XML parser (out of memory)", file->path);
		reader->failed = 1;
		return 0;
	}
	XML_SetUserData(reader->parser, reader);
	XML_SetElementHandler(reader->parser, element_start, element_end);
	XML_SetCharacterDataHandler(reader->parser, text);
	if (XML_Parse(reader->parser, (const char *)file->data, (int)file->size, XML_TRUE) == XML_STATUS_ERROR &&
		!reader->failed)
	{
		platform_log("menus: %s:%lu:%lu: %s", file->path, (unsigned long)XML_GetCurrentLineNumber(reader->parser),
			(unsigned long)XML_GetCurrentColumnNumber(reader->parser),
			XML_ErrorString(XML_GetErrorCode(reader->parser)));
		reader->failed = 1;
	}
	XML_ParserFree(reader->parser);
	return !reader->failed;
}

/* ---------- public code */

/* The files are read once, on a thread of their own from the renderer's
start (halo_menus_read_ahead, once xgpu_backend_capabilities and the VR mode
that for_this_platform reads are set), so that the first map's load (the main
menu's) does not wait the 24 ms Expat takes over them: halo_menus_load waits
for that thread, and nothing else reads the files before it. */
static struct halo_menus menus;
static int menus_succeeded;
static int menus_read;
static pthread_t menus_reader;
static int menus_reading;

static void menus_files_read(void)
{
	struct reader reader;
	long index;

	menus_read = 1;
	files_gather();
	memset(&reader, 0, sizeof(reader));
	for (index = 0; index < file_count && !reader.failed; index++)
	{
		size_t length = strlen(files[index].path);

		/* (but the menu engine's model, menu_model.c, and the hand-written
		screens, menu_screens.c: their building blocks' widgets are read
		here) */
		if (length > 4 && !strcmp(files[index].path + length - 4, ".xml") &&
			strncmp(files[index].path, HALO_MENU_MODEL_FOLDER, strlen(HALO_MENU_MODEL_FOLDER)) &&
			(strncmp(files[index].path, "screens/", 8) || !strcmp(files[index].path, "screens/blocks.xml")))
			read_file(&reader, &files[index]);
	}
	free(reader.last_child);
	free(reader.last_handler);
	free(reader.last_input);
	free(reader.last_conditional);
	free(reader.last_replace);
	if (reader.failed || !reader.menus.widget_count)
	{
		if (reader.failed)
			platform_log("menus: the files have a problem; using the game's own menus");
		return;
	}
	if (!reader.menus.root)
		reader.menus.root = "main_menu";
	menus = reader.menus;
	menus_succeeded = 1;
}

static void *menus_files_reader(void *unused)
{
	(void)unused;
	menus_files_read();
	return NULL;
}

void halo_menus_read_ahead(void)
{
	if (!menus_read && !menus_reading && !pthread_create(&menus_reader, NULL, menus_files_reader, NULL))
		menus_reading = 1;
}

struct halo_menus const *halo_menus_load(void)
{
	if (menus_reading)
	{
		pthread_join(menus_reader, NULL);
		menus_reading = 0;
	}
	if (!menus_read)
		menus_files_read();
	return menus_succeeded ? &menus : NULL;
}

/* a file gathered (halo_menus_load's): its path and data, FALSE past the
last (the menu engine's model: menu_model.c) */
int halo_menus_file_at(long index, char const **path, unsigned char const **data, unsigned long *size)
{
	if (index < 0 || index >= file_count)
		return 0;
	*path = files[index].path;
	*data = files[index].data;
	*size = files[index].size;
	return 1;
}

/* the files read again by the next halo_menus_load (the menu studio's
reload: port/linux/game/menu_studio.c, after the menus' tags are let go).
What the last reading made is kept, not freed: the description points into
it, and a reload is a tool's, a few times a minute */
void halo_menus_reload(void)
{
	if (menus_reading)
	{
		pthread_join(menus_reader, NULL);
		menus_reading = 0;
	}
	files = NULL;
	file_count = 0;
	memset(&menus, 0, sizeof(menus));
	menus_succeeded = 0;
	menus_read = 0;
	halo_menus_model_forget();
}

/* the menu studio's notes file and pictures' folder (debug.menu_notes, else
<tree>/build/menu_notes.jsonl where debug.menu_files is <tree>'s
port/assets/menus, else menu_notes.jsonl beside config.toml), the folder
made */
void halo_menus_notes_paths(char *notes, unsigned long notes_size, char *pictures, unsigned long pictures_size)
{
	char const *files = config_string("debug.menu_files");
	char const *tail = "/port/assets/menus";
	size_t length = strlen(files);
	char *slash;

	if (config_string("debug.menu_notes")[0])
	{
		snprintf(notes, notes_size, "%s", config_string("debug.menu_notes"));
	}
	else if (length > strlen(tail) && (!strcmp(files + length - strlen(tail), tail) ||
		(files[length - 1] == '/' && !strncmp(files + length - strlen(tail) - 1, tail, strlen(tail)))))
	{
		snprintf(notes, notes_size, "%.*s/build/menu_notes.jsonl",
			(int)(length - strlen(tail) - (files[length - 1] == '/')), files);
	}
	else
	{
		config_folder(notes, notes_size);
		snprintf(notes + strlen(notes), notes_size - strlen(notes), "menu_notes.jsonl");
	}
	snprintf(pictures, pictures_size, "%s", notes);
	slash = strrchr(pictures, '/');
	if (slash)
		snprintf(slash + 1, pictures_size - (unsigned long)(slash + 1 - pictures), "menu_notes");
}

/* a line added to the notes file (the menu studio's); FALSE if it could not */
int halo_menus_note_append(char const *path, char const *line)
{
	FILE *file = fopen(path, "a");
	int written;

	if (!file)
		return 0;
	written = fputs(line, file) >= 0 && fputc('\n', file) != EOF;
	return fclose(file) == 0 && written;
}

/* the notes file read whole (malloc's, the caller's to free), or NULL */
char *halo_menus_notes_read(char const *path)
{
	unsigned long size = 0;
	unsigned char *data = file_read(path, &size);

	if (data)
		data[size] = 0;
	return (char *)data;
}

/* (the platform layer's allocation: the game's free is its own) */
void halo_menus_notes_free(char *text)
{
	free(text);
}

/* a number that changes when a menu file of the folder (its own and its
folders' .xml files) is written, added or removed: their times and sizes */
unsigned long long halo_menus_files_stamp(void)
{
	static const char *const patterns[] = { "*.xml", "*/*.xml" };
	unsigned long long stamp = 0;
	char path[1200];
	int pattern;

#ifdef HALO_ANDROID
	(void)patterns;
	(void)path;
	(void)pattern;
	return stamp;
#else
	if (!folder[0])
		return 0;
	for (pattern = 0; pattern < 2; pattern++)
	{
		int count = 0;
		char **names = SDL_GlobDirectory(folder, patterns[pattern], 0, &count);
		int name;

		for (name = 0; names && name < count; name++)
		{
			SDL_PathInfo info;

			snprintf(path, sizeof(path), "%s/%s", folder, names[name]);
			if (SDL_GetPathInfo(path, &info))
				stamp = stamp * 1099511628211ULL + (unsigned long long)info.modify_time + info.size;
		}
		stamp += (unsigned long long)count;
		SDL_free(names);
	}
	return stamp;
#endif
}

long halo_menus_utf16(char const *utf8, unsigned short *out, long capacity)
{
	const unsigned char *in = (const unsigned char *)utf8;
	long count = 0;

	while (*in && count < capacity - 1)
	{
		unsigned long code = *in++;

		if (code == '\\' && *in == 'n')
		{
			in++;
			if (count + 2 >= capacity)
				break;
			out[count++] = '\r';
			out[count++] = '\n';
			continue;
		}
		if (code >= 0xC0)
		{
			int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : 1;

			code &= 0x3F >> more;
			while (more-- && (*in & 0xC0) == 0x80)
				code = (code << 6) | (*in++ & 0x3F);
		}
		/* (beyond the 16-bit characters: the game's fonts have none) */
		out[count++] = code < 0x10000 ? (unsigned short)code : '?';
	}
	out[count++] = 0;
	return count;
}

void halo_menus_log(char const *file, long line, char const *message, char const *detail)
{
	platform_log("menus: %s:%ld: %s%s%s", file ? file : "?", line, message, detail ? " " : "", detail ? detail : "");
}

/* ---------- art

Each bitmap's PNG is decoded on a thread of its own ahead of its first
draw (halo_menus_art_ahead: those of a screen shown and of the screens it
opens, menu_tags.c's pc_menu_screen_shown), so that a screen opens without
decoding its art: the game's thread then only makes the image of the
pixels. A PNG drawn before its decoding is done waits for it; one never
asked for is decoded as it is drawn. */

enum
{
	_art_decode_none,
	_art_decode_queued,
	_art_decode_decoding,
	_art_decode_done,
};

/* threads decoding at once */
#define ART_DECODE_THREADS 2

static struct
{
	unsigned long data;
	char *png;
	unsigned int texture;
	unsigned long levels;
	int failed;
	/* its decoding ahead (art_lock's): the PNG, its pixels once decoded */
	int decode_state;
	const unsigned char *decode_png;
	unsigned long decode_png_size;
	unsigned char *pixels;
	unsigned long width, height;
} art[MAXIMUM_ART];
static long art_count;
static unsigned short art_lookup[MAXIMUM_ART * 2];
/* each slot's texture data, beside it: a draw's texture stages, which are
seldom menu art, probe these alone, not the entries */
static unsigned long art_lookup_data[MAXIMUM_ART * 2];
static pthread_mutex_t art_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t art_decoded = PTHREAD_COND_INITIALIZER;
static int art_decoders;

/* Most textures are not menu art. Find those without walking every menu
bitmap for each texture stage of every draw. Entries survive until forget. */
static unsigned long art_slot(unsigned long data)
{
	unsigned long hash = data, slot;

	hash ^= hash >> 16;
	hash *= 0x85ebca6bUL;
	hash ^= hash >> 13;
	slot = hash % (MAXIMUM_ART * 2);
	while (art_lookup[slot] && art_lookup_data[slot] != data)
		slot = (slot + 1) % (MAXIMUM_ART * 2);
	return slot;
}

/* (art_lock held) an entry's decoding ahead let go: waited for if running */
static void art_decode_forget(long index)
{
	while (art[index].decode_state == _art_decode_decoding)
		pthread_cond_wait(&art_decoded, &art_lock);
	free(art[index].pixels);
	art[index].pixels = NULL;
	art[index].decode_state = _art_decode_none;
}

void halo_menus_art_register(void const *texture, char const *png)
{
	unsigned long data = ((const unsigned long *)texture)[1];
	unsigned long slot = art_slot(data);
	long index = art_lookup[slot] ? art_lookup[slot] - 1 : art_count;
	if (index == art_count)
	{
		if (art_count == MAXIMUM_ART)
		{
			platform_log("menus: more than %d bitmaps; %s is not drawn", MAXIMUM_ART, png);
			return;
		}
		art_lookup[slot] = (unsigned short)(index + 1);
		art_lookup_data[slot] = data;
		art_count++;
	}
	else if (art[index].png && !strcmp(art[index].png, png))
	{
		return;
	}
	else
	{
		pthread_mutex_lock(&art_lock);
		art_decode_forget(index);
		pthread_mutex_unlock(&art_lock);
		free(art[index].png);
		if (art[index].texture)
		{
			xgpu_image_delete(art[index].texture);
		}
	}
	memset(&art[index], 0, sizeof(art[index]));
	art[index].data = data;
	art[index].png = strdup(png);
	xgpu_texture_replacements_changed();
}

void halo_menus_art_forget(void)
{
	long index;

	pthread_mutex_lock(&art_lock);
	for (index = 0; index < art_count; index++)
		art_decode_forget(index);
	pthread_mutex_unlock(&art_lock);
	for (index = 0; index < art_count; index++)
	{
		free(art[index].png);
		if (art[index].texture)
		{
			xgpu_image_delete(art[index].texture);
		}
	}
	art_count = 0;
	memset(art_lookup, 0, sizeof(art_lookup));
	memset(art_lookup_data, 0, sizeof(art_lookup_data));
	xgpu_texture_replacements_changed();
}

/* a decoding thread: each PNG queued in turn, until none is */
static void *art_decode_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&art_lock);
	for (;;)
	{
		long index;
		const unsigned char *png;
		unsigned long size, width = 0, height = 0;
		unsigned char *pixels;

		for (index = 0; index < art_count && art[index].decode_state != _art_decode_queued; index++)
			;
		if (index == art_count)
			break;
		art[index].decode_state = _art_decode_decoding;
		png = art[index].decode_png;
		size = art[index].decode_png_size;
		pthread_mutex_unlock(&art_lock);
		pixels = hud_hires_png_pixels(png, size, &width, &height);
		pthread_mutex_lock(&art_lock);
		art[index].pixels = pixels;
		art[index].width = width;
		art[index].height = height;
		art[index].decode_state = _art_decode_done;
		pthread_cond_broadcast(&art_decoded);
	}
	art_decoders--;
	pthread_cond_broadcast(&art_decoded);
	pthread_mutex_unlock(&art_lock);
	return NULL;
}

void halo_menus_art_ahead(void const *texture)
{
	unsigned long slot = art_slot(((const unsigned long *)texture)[1]);
	long index = art_lookup[slot] ? art_lookup[slot] - 1 : -1;
	const unsigned char *png;
	unsigned long size = 0;

	if (index < 0 || art[index].texture || art[index].failed || !art[index].png ||
		art[index].decode_state != _art_decode_none)
	{
		return;
	}
	/* (the PNG found here: the files' table is the game thread's) */
	png = file_data(art[index].png, &size);
	if (!png)
		return;
	pthread_mutex_lock(&art_lock);
	art[index].decode_png = png;
	art[index].decode_png_size = size;
	art[index].decode_state = _art_decode_queued;
	if (art_decoders < ART_DECODE_THREADS)
	{
		pthread_t thread;

		art_decoders++;
		if (pthread_create(&thread, NULL, art_decode_thread, NULL) == 0)
			pthread_detach(thread);
		else
		{
			art_decoders--;
			/* (none: decoded as it is drawn) */
			if (!art_decoders)
				art[index].decode_state = _art_decode_none;
		}
	}
	pthread_mutex_unlock(&art_lock);
}

unsigned int menu_art_texture(unsigned long data, unsigned long *levels)
{
	unsigned long slot;
	long index;

	/* (none registered: in a game whose menus have not been read) */
	if (!art_count)
		return 0;
	slot = art_slot(data);
	index = art_lookup[slot] ? art_lookup[slot] - 1 : art_count;
	/* (no png: out of memory registering it) */
	if (index == art_count || art[index].failed || !art[index].png)
		return 0;
	if (!art[index].texture)
	{
		unsigned char *pixels = NULL;
		unsigned long width = 0, height = 0;
		int decoded = 0;

		/* (decoded ahead, or being decoded: waited for; queued, and
		not yet begun: decoded here) */
		pthread_mutex_lock(&art_lock);
		while (art[index].decode_state == _art_decode_decoding)
			pthread_cond_wait(&art_decoded, &art_lock);
		if (art[index].decode_state == _art_decode_done)
		{
			pixels = art[index].pixels;
			width = art[index].width;
			height = art[index].height;
			art[index].pixels = NULL;
			decoded = 1;
		}
		art[index].decode_state = _art_decode_none;
		pthread_mutex_unlock(&art_lock);
		if (!decoded)
		{
			unsigned long size = 0;
			const unsigned char *png = file_data(art[index].png, &size);

			pixels = png ? hud_hires_png_pixels(png, size, &width, &height) : NULL;
		}
		art[index].texture = pixels ? hud_hires_pixels_texture(pixels, width, height, &art[index].levels) : 0;
		if (!art[index].texture)
		{
			platform_log("menus: could not draw %s", art[index].png);
			art[index].failed = 1;
			return 0;
		}
	}
	*levels = art[index].levels;
	return art[index].texture;
}
