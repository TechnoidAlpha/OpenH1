/*
MENU_MODEL.C

The menu engine's model (halo_menus.h; docs/menu_engine.md): the files in
the menus' model folder (port/assets/menus/model, which
tools/port_settings.py writes beside the menus' XML, or a menus folder's
file of the same name) read with Expat into screens, their rows and their
choices, the widgets' looks (model/look, tools/menu_look.py) and the
widgets' behaviours (model/behaviour, tools/menu_behaviour.py: their
behaviour flags and their <on> handlers, read as the menus' own), and the
setup screens' nodes (model/nodes.xml, tools/menu_nodes.py: rows of kind
"node", each keyed by its widget's name), for the game's menu engine (port/linux/game/menu_engine.c).

As with the menus' files (menu_files.c), only the files' shape is checked
here: their elements and attributes, and numbers. A problem is logged with
the file and line, and then there is no model: the engine leaves the screens
to the game's widgets.
*/

#include "halo_menus.h"
#include "platform.h"

#include "expat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum model_element
{
	_model_none,
	_model_model,
	_model_geometry,
	_model_screen,
	_model_about,
	_model_row,
	_model_help,
	_model_note,
	_model_choice,
	_model_look,
	_model_look_text,
	_model_place,
	_model_behaviour,
	_model_behaviour_on,
};

#define MODEL_MAXIMUM_DEPTH 8

struct model_reader
{
	struct halo_menu_model model;
	char const *file;
	XML_Parser parser;
	int failed;
	enum model_element elements[MODEL_MAXIMUM_DEPTH];
	int depth;
	/* the text of the open <about>, <help> or <note> */
	char *text;
	size_t text_length;
	/* the capacities of the model's arrays */
	long screen_capacity, row_capacity, choice_capacity, look_capacity, place_capacity;
	long behaviour_capacity, handler_capacity;
};

static struct halo_menu_model model;
static int model_read;
/* each model read's number (halo_menus_model_reads) */
static unsigned long model_reads;
static int model_succeeded;

static void model_error(struct model_reader *reader, char const *format, char const *detail)
{
	char message[512];

	snprintf(message, sizeof(message), format, detail);
	if (!reader->failed)
	{
		platform_log("menus: %s:%lu: %s", reader->file, (unsigned long)XML_GetCurrentLineNumber(reader->parser),
			message);
	}
	reader->failed = 1;
	XML_StopParser(reader->parser, XML_FALSE);
}

static char *model_copy(char const *text)
{
	size_t length = strlen(text);
	char *result = malloc(length + 1);

	if (result)
		memcpy(result, text, length + 1);
	return result;
}

/* room for one more in an array of size each, count of capacity */
static int model_grow(struct model_reader *reader, void **array, long count, long *capacity, size_t size)
{
	void *grown;
	long wanted;

	if (count < *capacity)
		return 1;
	wanted = *capacity ? *capacity * 2 : 64;
	grown = realloc(*array, (size_t)wanted * size);
	if (!grown)
	{
		model_error(reader, "out of memory%s", "");
		return 0;
	}
	*array = grown;
	*capacity = wanted;
	return 1;
}

/* the looks by name (halo_menus_model_look), and the behaviours
(halo_menus_model_behaviour) */
static struct halo_menu_model_look const **model_looks_sorted;
static struct halo_menu_model_behaviour const **model_behaviours_sorted;

static int model_long(struct model_reader *reader, char const *text, long *value)
{
	char *end;

	errno = 0;
	*value = strtol(text, &end, 10);
	if (!*text || *end || errno == ERANGE || *value < -32768 || *value > 32767)
	{
		model_error(reader, "\"%s\" is not a whole number from -32768 to 32767", text);
		return 0;
	}
	return 1;
}

static int model_real(struct model_reader *reader, char const *text, double *value)
{
	char *end;

	*value = strtod(text, &end);
	if (!*text || *end)
	{
		model_error(reader, "\"%s\" is not a number", text);
		return 0;
	}
	return 1;
}

static int model_flag(struct model_reader *reader, char const *text, long *value)
{
	if (strcmp(text, "true") && strcmp(text, "false"))
	{
		model_error(reader, "\"%s\" is not true or false", text);
		return 0;
	}
	*value = !strcmp(text, "true");
	return 1;
}

/* whole numbers separated by spaces, as many as values has */
static int model_longs(struct model_reader *reader, char const *text, long *values, int count)
{
	char numbers[64], *word, *rest;
	int index;

	if (strlen(text) >= sizeof(numbers))
	{
		model_error(reader, "\"%s\" is too long", text);
		return 0;
	}
	strcpy(numbers, text);
	rest = numbers;
	for (index = 0; index < count; index++)
	{
		rest += strspn(rest, " ");
		word = rest;
		rest += strcspn(rest, " ");
		if (*rest)
			*rest++ = 0;
		if (!*word)
		{
			model_error(reader, "\"%s\" has too few numbers", text);
			return 0;
		}
		if (!model_long(reader, word, &values[index]))
			return 0;
	}
	if (rest[strspn(rest, " ")])
	{
		model_error(reader, "\"%s\" has too many numbers", text);
		return 0;
	}
	return 1;
}

/* an attribute an element may have: text, a whole number, a number or a
flag (true or false) */
struct model_attribute
{
	char const *name;
	char const **text;
	long *number;
	double *real;
	long *flag;
};

static void model_attributes(struct model_reader *reader, char const *element, XML_Char const **attributes,
	struct model_attribute const *table, size_t count)
{
	int index;

	for (index = 0; attributes[index] && !reader->failed; index += 2)
	{
		char const *name = attributes[index], *value = attributes[index + 1];
		size_t field;

		for (field = 0; field < count && strcmp(name, table[field].name); field++)
			;
		if (field == count)
		{
			char detail[160];

			snprintf(detail, sizeof(detail), "<%s> has no attribute %s", element, name);
			model_error(reader, "%s", detail);
		}
		else if (table[field].text)
		{
			*table[field].text = model_copy(value);
			if (!*table[field].text)
				model_error(reader, "out of memory%s", "");
		}
		else if (table[field].number)
			model_long(reader, value, table[field].number);
		else if (table[field].real)
			model_real(reader, value, table[field].real);
		else
			model_flag(reader, value, table[field].flag);
	}
}

static void model_geometry(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_geometry made;
	struct halo_menu_model_geometry *geometry = &made;
	struct model_attribute table[] =
	{
		{ "name", &geometry->name }, { "panel_bottom", NULL, &geometry->panel_bottom },
		{ "scrolls", NULL, NULL, NULL, &geometry->scrolls },
		{ "row_x", NULL, &geometry->row_x }, { "row_width", NULL, &geometry->row_width },
		{ "row_height", NULL, &geometry->row_height }, { "row_step", NULL, &geometry->row_step },
		{ "section_height", NULL, &geometry->section_height }, { "section_step", NULL, &geometry->section_step },
		{ "section_gap", NULL, &geometry->section_gap }, { "tab_top", NULL, &geometry->tab_top },
		{ "page_top", NULL, &geometry->page_top }, { "rows_bottom", NULL, &geometry->rows_bottom },
		{ "spinner_x", NULL, &geometry->spinner_x }, { "spinner_width", NULL, &geometry->spinner_width },
		{ "search_word_extra", NULL, &geometry->search_word_extra },
		{ "search_results", NULL, &geometry->search_results }, { "search_crumbs", NULL, &geometry->search_crumbs },
	};

	memset(&made, 0, sizeof(made));
	model_attributes(reader, "geometry", attributes, table, sizeof(table) / sizeof(*table));
	if (reader->failed)
		return;
	if (reader->model.geometry_count >= HALO_MENU_MODEL_MAXIMUM_GEOMETRIES)
	{
		model_error(reader, "more than %s <geometry> elements", "4");
		return;
	}
	if ((reader->model.geometry_count == 0) != !made.name)
	{
		model_error(reader, "the first <geometry> has no name, and every other one has%s", "");
		return;
	}
	reader->model.geometries[reader->model.geometry_count++] = made;
	if (reader->model.geometry_count == 1)
		reader->model.geometry = made;
}

static void model_screen(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_screen screen;
	struct model_attribute table[] =
	{
		{ "name", &screen.name }, { "kind", &screen.kind }, { "layout", &screen.layout },
		{ "geometry", &screen.geometry }, { "tab", &screen.tab },
		{ "title", &screen.title }, { "spacing", NULL, &screen.spacing },
		{ "platform_places", NULL, NULL, NULL, &screen.platform_places },
		{ "profile", NULL, NULL, NULL, &screen.profile },
	};

	memset(&screen, 0, sizeof(screen));
	model_attributes(reader, "screen", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && (!screen.name || !screen.kind))
		model_error(reader, "a <screen> needs a name and a kind%s", "");
	if (reader->failed || !model_grow(reader, (void **)&reader->model.screens, reader->model.screen_count,
		&reader->screen_capacity, sizeof(screen)))
		return;
	screen.first_row = reader->model.row_count;
	screen.file = reader->file;
	screen.line = (long)XML_GetCurrentLineNumber(reader->parser);
	reader->model.screens[reader->model.screen_count++] = screen;
}

/* a <row>, <section>, <button> or <node> of the screen open */
static void model_row(struct model_reader *reader, char const *element, XML_Char const **attributes)
{
	struct halo_menu_model_row row;
	struct model_attribute table[] =
	{
		{ "kind", &row.kind }, { "key", &row.key }, { "setting", &row.setting }, { "label", &row.label },
		{ "title", &row.title }, { "when", &row.when }, { "open", &row.open }, { "run", &row.run },
		{ "help", &row.help }, { "enabled", &row.enabled },
		{ "crumb", &row.crumb }, { "search", &row.search }, { "search_choices", &row.search_choices },
		{ "same_place", NULL, NULL, NULL, &row.same_place }, { "not_in_vr", NULL, NULL, NULL, &row.not_in_vr },
		{ "named", NULL, NULL, NULL, &row.named }, { "status", NULL, NULL, NULL, &row.status },
		{ "least", NULL, NULL, &row.least }, { "most", NULL, NULL, &row.most }, { "step", NULL, NULL, &row.step },
		{ "typed_least", NULL, NULL, &row.typed_least }, { "typed_most", NULL, NULL, &row.typed_most },
		{ "whole", NULL, NULL, NULL, &row.whole },
		{ "name", &row.key }, { "parent", &row.parent },
		{ "focusable", NULL, NULL, NULL, &row.focusable }, { "initial", NULL, NULL, NULL, &row.initial },
		{ "help_index", NULL, &row.help_index }, { "help_choices", NULL, NULL, NULL, &row.help_choices },
	};
	struct halo_menu_model_screen *screen;

	memset(&row, 0, sizeof(row));
	row.help_index = -1;
	model_attributes(reader, element, attributes, table, sizeof(table) / sizeof(*table));
	if (reader->failed)
		return;
	if (!row.key)
	{
		model_error(reader, "a <%s> needs a key (a <node>, a name)", element);
		return;
	}
	if (!strcmp(element, "node"))
		row.kind = "node";
	else if (!strcmp(element, "section"))
		row.kind = "section";
	else if (!strcmp(element, "button"))
	{
		row.label = row.kind;
		row.kind = "button";
	}
	else if (!row.kind)
	{
		model_error(reader, "a <row> needs a kind%s", "");
		return;
	}
	if (!model_grow(reader, (void **)&reader->model.rows, reader->model.row_count, &reader->row_capacity,
		sizeof(row)))
		return;
	row.first_choice = reader->model.choice_count;
	row.file = reader->file;
	row.line = (long)XML_GetCurrentLineNumber(reader->parser);
	reader->model.rows[reader->model.row_count++] = row;
	screen = &reader->model.screens[reader->model.screen_count - 1];
	screen->row_count++;
}

static void model_choice(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_choice choice;
	struct model_attribute table[] = { { "shown", &choice.shown }, { "value", &choice.value } };

	memset(&choice, 0, sizeof(choice));
	model_attributes(reader, "choice", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && (!choice.shown || !choice.value))
		model_error(reader, "a choice needs what it shows and its value%s", "");
	if (reader->failed || !model_grow(reader, (void **)&reader->model.choices, reader->model.choice_count,
		&reader->choice_capacity, sizeof(choice)))
		return;
	reader->model.choices[reader->model.choice_count++] = choice;
	reader->model.rows[reader->model.row_count - 1].choice_count++;
}

/* a box ("x0 y0 x1 y1") or a pair ("x y") given as text, read into values
(*given: it was) */
static void model_numbers(struct model_reader *reader, char const *text, long *values, int count, long *given)
{
	if (text && !reader->failed && model_longs(reader, text, values, count) && given)
		*given = 1;
}

static void model_look(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_look look;
	char const *box = NULL, *header_box = NULL, *footer_box = NULL;
	struct model_attribute table[] =
	{
		{ "name", &look.name }, { "role", &look.role }, { "type", &look.type }, { "box", &box },
		{ "wide", &look.wide }, { "past", &look.past },
		{ "place", &look.place }, { "picture", &look.picture }, { "radius", NULL, &look.radius },
		{ "effects", &look.effects }, { "list", &look.list }, { "header", &look.header },
		{ "header_box", &header_box }, { "footer", &look.footer }, { "footer_box", &footer_box },
	};

	memset(&look, 0, sizeof(look));
	model_attributes(reader, "look", attributes, table, sizeof(table) / sizeof(*table));
	model_numbers(reader, box, look.box, 4, &look.has_box);
	model_numbers(reader, header_box, look.header_box, 4, &look.has_header_box);
	model_numbers(reader, footer_box, look.footer_box, 4, &look.has_footer_box);
	if (!reader->failed && !look.name)
		model_error(reader, "a <look> needs a name%s", "");
	if (reader->failed || !model_grow(reader, (void **)&reader->model.looks, reader->model.look_count,
		&reader->look_capacity, sizeof(look)))
		return;
	look.first_place = reader->model.place_count;
	look.file = reader->file;
	look.line = (long)XML_GetCurrentLineNumber(reader->parser);
	reader->model.looks[reader->model.look_count++] = look;
}

/* the open <look>'s <text> */
static void model_look_text(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_look *look = &reader->model.looks[reader->model.look_count - 1];
	char const *at = NULL;
	long given = 0;
	struct model_attribute table[] =
	{
		{ "font", &look->font }, { "color", &look->color }, { "align", &look->align }, { "at", &at },
		{ "flags", &look->text_flags }, { "string_list", &look->string_list },
		{ "index", NULL, &look->string_index }, { "text", &look->text }, { "items", &look->items },
		{ "replace", &look->replace }, { "scale", &look->text_scale },
	};

	if (look->has_text)
	{
		model_error(reader, "a <look> has one <text>%s", "");
		return;
	}
	look->has_text = 1;
	model_attributes(reader, "text", attributes, table, sizeof(table) / sizeof(*table));
	if (at)
	{
		long pair[2];

		model_numbers(reader, at, pair, 2, &given);
		look->text_x = pair[0];
		look->text_y = pair[1];
	}
}

/* the open <look>'s <at> */
static void model_place(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_place place;
	struct model_attribute table[] =
	{
		{ "widget", &place.widget }, { "x", NULL, &place.x }, { "y", NULL, &place.y },
	};

	memset(&place, 0, sizeof(place));
	model_attributes(reader, "at", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !place.widget)
		model_error(reader, "an <at> needs a widget%s", "");
	if (reader->failed || !model_grow(reader, (void **)&reader->model.places, reader->model.place_count,
		&reader->place_capacity, sizeof(place)))
		return;
	reader->model.places[reader->model.place_count++] = place;
	reader->model.looks[reader->model.look_count - 1].place_count++;
}

/* a <behaviour> (tools/menu_behaviour.py) */
static void model_behaviour(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_model_behaviour behaviour;
	struct model_attribute table[] = { { "name", &behaviour.name }, { "flags", &behaviour.flags } };

	memset(&behaviour, 0, sizeof(behaviour));
	model_attributes(reader, "behaviour", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !behaviour.name)
		model_error(reader, "a <behaviour> needs a name%s", "");
	if (reader->failed || !model_grow(reader, (void **)&reader->model.behaviours, reader->model.behaviour_count,
		&reader->behaviour_capacity, sizeof(behaviour)))
		return;
	behaviour.first_handler = reader->model.handler_count;
	behaviour.file = reader->file;
	behaviour.line = (long)XML_GetCurrentLineNumber(reader->parser);
	reader->model.behaviours[reader->model.behaviour_count++] = behaviour;
}

/* the open <behaviour>'s <on>: an event handler, as the menus' own are
(menu_files.c), which menu_tags.c makes the game's of */
static void model_behaviour_on(struct model_reader *reader, XML_Char const **attributes)
{
	struct halo_menu_handler handler;
	struct model_attribute table[] =
	{
		{ "event", &handler.event }, { "run", &handler.run }, { "open", &handler.open },
		{ "replace", &handler.replace }, { "close", &handler.close }, { "back", NULL, NULL, NULL, &handler.back },
		{ "branch", NULL, NULL, NULL, &handler.branch }, { "sound", &handler.sound },
		{ "otherwise", &handler.otherwise }, { "focus", &handler.focus }, { "reload", &handler.reload },
		{ "widget", &handler.widget }, { "script", &handler.script }, { "label", &handler.label },
	};

	memset(&handler, 0, sizeof(handler));
	model_attributes(reader, "on", attributes, table, sizeof(table) / sizeof(*table));
	if (!reader->failed && !handler.event)
		model_error(reader, "an <on> needs an event%s", "");
	if (reader->failed || !model_grow(reader, (void **)&reader->model.handlers, reader->model.handler_count,
		&reader->handler_capacity, sizeof(handler)))
		return;
	handler.next = HALO_MENU_NONE;
	handler.file = reader->file;
	handler.line = (long)XML_GetCurrentLineNumber(reader->parser);
	reader->model.handlers[reader->model.handler_count++] = handler;
	reader->model.behaviours[reader->model.behaviour_count - 1].handler_count++;
}

static void XMLCALL model_start(void *data, XML_Char const *name, XML_Char const **attributes)
{
	struct model_reader *reader = data;
	enum model_element parent = reader->depth ? reader->elements[reader->depth - 1] : _model_none;
	enum model_element element = _model_none;

	if (reader->failed)
		return;
	if (reader->depth == MODEL_MAXIMUM_DEPTH)
	{
		model_error(reader, "elements nested too deeply%s", "");
		return;
	}
	if (!strcmp(name, "model") && parent == _model_none)
		element = _model_model;
	else if (!strcmp(name, "geometry") && parent == _model_model)
	{
		element = _model_geometry;
		model_geometry(reader, attributes);
	}
	else if (!strcmp(name, "screen") && parent == _model_model)
	{
		element = _model_screen;
		model_screen(reader, attributes);
	}
	else if (!strcmp(name, "about") && parent == _model_screen)
		element = _model_about;
	else if ((!strcmp(name, "row") || !strcmp(name, "section") || !strcmp(name, "button") ||
		!strcmp(name, "node")) && parent == _model_screen)
	{
		element = _model_row;
		model_row(reader, name, attributes);
	}
	else if (!strcmp(name, "help") && parent == _model_row)
		element = _model_help;
	else if (!strcmp(name, "note") && parent == _model_row)
		element = _model_note;
	else if ((!strcmp(name, "choice") || !strcmp(name, "stop")) && parent == _model_row)
	{
		element = _model_choice;
		model_choice(reader, attributes);
	}
	else if (!strcmp(name, "look") && parent == _model_model)
	{
		element = _model_look;
		model_look(reader, attributes);
	}
	else if (!strcmp(name, "text") && parent == _model_look)
	{
		element = _model_look_text;
		model_look_text(reader, attributes);
	}
	else if (!strcmp(name, "at") && parent == _model_look)
	{
		element = _model_place;
		model_place(reader, attributes);
	}
	else if (!strcmp(name, "behaviour") && parent == _model_model)
	{
		element = _model_behaviour;
		model_behaviour(reader, attributes);
	}
	else if (!strcmp(name, "on") && parent == _model_behaviour)
	{
		element = _model_behaviour_on;
		model_behaviour_on(reader, attributes);
	}
	if (element == _model_none)
	{
		model_error(reader, "<%s> cannot be here", name);
		return;
	}
	if ((element == _model_about || element == _model_help || element == _model_note) && attributes[0])
		model_error(reader, "<%s> has no attributes", name);
	reader->text_length = 0;
	reader->elements[reader->depth++] = element;
}

static void XMLCALL model_end(void *data, XML_Char const *name)
{
	struct model_reader *reader = data;
	enum model_element element;
	char *text;

	(void)name;
	if (reader->failed || !reader->depth)
		return;
	element = reader->elements[--reader->depth];
	if (element != _model_about && element != _model_help && element != _model_note)
		return;
	text = malloc(reader->text_length + 1);
	if (!text)
	{
		model_error(reader, "out of memory%s", "");
		return;
	}
	memcpy(text, reader->text ? reader->text : "", reader->text_length);
	text[reader->text_length] = 0;
	if (element == _model_about)
		reader->model.screens[reader->model.screen_count - 1].about = text;
	else if (element == _model_help)
		reader->model.rows[reader->model.row_count - 1].help = text;
	else
		reader->model.rows[reader->model.row_count - 1].note = text;
}

static void XMLCALL model_text(void *data, XML_Char const *characters, int length)
{
	struct model_reader *reader = data;
	enum model_element element = reader->depth ? reader->elements[reader->depth - 1] : _model_none;
	char *grown;
	int index;

	if (reader->failed)
		return;
	if (element != _model_about && element != _model_help && element != _model_note)
	{
		for (index = 0; index < length; index++)
		{
			if (!strchr(" \t\r\n", characters[index]))
			{
				model_error(reader, "text belongs in <about>, <help> or <note>%s", "");
				return;
			}
		}
		return;
	}
	grown = realloc(reader->text, reader->text_length + (size_t)length + 1);
	if (!grown)
	{
		model_error(reader, "out of memory%s", "");
		return;
	}
	reader->text = grown;
	memcpy(reader->text + reader->text_length, characters, (size_t)length);
	reader->text_length += (size_t)length;
}

static void model_file(struct model_reader *reader, char const *path, unsigned char const *data, unsigned long size)
{
	reader->file = path;
	reader->depth = 0;
	reader->parser = XML_ParserCreate("UTF-8");
	if (!reader->parser)
	{
		platform_log("menus: %s: no XML parser (out of memory)", path);
		reader->failed = 1;
		return;
	}
	XML_SetUserData(reader->parser, reader);
	XML_SetElementHandler(reader->parser, model_start, model_end);
	XML_SetCharacterDataHandler(reader->parser, model_text);
	if (XML_Parse(reader->parser, (char const *)data, (int)size, XML_TRUE) == XML_STATUS_ERROR && !reader->failed)
	{
		platform_log("menus: %s:%lu:%lu: %s", path, (unsigned long)XML_GetCurrentLineNumber(reader->parser),
			(unsigned long)XML_GetCurrentColumnNumber(reader->parser),
			XML_ErrorString(XML_GetErrorCode(reader->parser)));
		reader->failed = 1;
	}
	XML_ParserFree(reader->parser);
}

static int model_look_order(void const *a, void const *b)
{
	return strcmp((*(struct halo_menu_model_look const *const *)a)->name,
		(*(struct halo_menu_model_look const *const *)b)->name);
}

static int model_behaviour_order(void const *a, void const *b)
{
	return strcmp((*(struct halo_menu_model_behaviour const *const *)a)->name,
		(*(struct halo_menu_model_behaviour const *const *)b)->name);
}

struct halo_menu_model const *halo_menus_model(void)
{
	struct model_reader reader;
	char const *path;
	unsigned char const *data;
	unsigned long size;
	long index;

	if (model_read)
		return model_succeeded ? &model : NULL;
	model_read = 1;
	if (!halo_menus_load())
		return NULL;
	memset(&reader, 0, sizeof(reader));
	for (index = 0; !reader.failed && halo_menus_file_at(index, &path, &data, &size); index++)
	{
		size_t length = strlen(path);

		if (!strncmp(path, HALO_MENU_MODEL_FOLDER, strlen(HALO_MENU_MODEL_FOLDER)) && length > 4 &&
			!strcmp(path + length - 4, ".xml"))
			model_file(&reader, path, data, size);
	}
	free(reader.text);
	if (reader.failed || !reader.model.screen_count)
	{
		if (reader.failed)
			platform_log("menus: the model has a problem; the menus run on the game's widgets");
		return NULL;
	}
	model = reader.model;
	/* (the looks by name) */
	model_looks_sorted = model.look_count ? malloc((size_t)model.look_count * sizeof(*model_looks_sorted)) : NULL;
	if (model_looks_sorted)
	{
		for (index = 0; index < model.look_count; index++)
			model_looks_sorted[index] = &model.looks[index];
		qsort(model_looks_sorted, (size_t)model.look_count, sizeof(*model_looks_sorted), model_look_order);
	}
	model_behaviours_sorted = model.behaviour_count ?
		malloc((size_t)model.behaviour_count * sizeof(*model_behaviours_sorted)) : NULL;
	if (model_behaviours_sorted)
	{
		for (index = 0; index < model.behaviour_count; index++)
			model_behaviours_sorted[index] = &model.behaviours[index];
		qsort(model_behaviours_sorted, (size_t)model.behaviour_count, sizeof(*model_behaviours_sorted),
			model_behaviour_order);
	}
	model_succeeded = 1;
	model_reads++;
	platform_log("menus: the model has %ld screens, %ld rows, %ld widgets' looks, %ld widgets' behaviours",
		model.screen_count, model.row_count, model.look_count, model.behaviour_count);
	return &model;
}

struct halo_menu_model_look const *halo_menus_model_look(struct halo_menu_model const *of, char const *name)
{
	struct halo_menu_model_look key;
	struct halo_menu_model_look const *pointer = &key;
	struct halo_menu_model_look const **found;

	if (!of || of != &model || !model_looks_sorted || !name)
		return NULL;
	key.name = name;
	found = bsearch(&pointer, model_looks_sorted, (size_t)model.look_count, sizeof(*model_looks_sorted),
		model_look_order);
	return found ? *found : NULL;
}

struct halo_menu_model_behaviour const *halo_menus_model_behaviour(struct halo_menu_model const *of,
	char const *name)
{
	struct halo_menu_model_behaviour key;
	struct halo_menu_model_behaviour const *pointer = &key;
	struct halo_menu_model_behaviour const **found;

	if (!of || of != &model || !model_behaviours_sorted || !name)
		return NULL;
	key.name = name;
	found = bsearch(&pointer, model_behaviours_sorted, (size_t)model.behaviour_count,
		sizeof(*model_behaviours_sorted), model_behaviour_order);
	return found ? *found : NULL;
}

unsigned long halo_menus_model_reads(void)
{
	return model_reads;
}

/* the model read again with the menus (halo_menus_reload: the menu studio's) */
void halo_menus_model_forget(void)
{
	model_read = 0;
	model_succeeded = 0;
	memset(&model, 0, sizeof(model));
	free(model_looks_sorted);
	model_looks_sorted = NULL;
	free(model_behaviours_sorted);
	model_behaviours_sorted = NULL;
}
