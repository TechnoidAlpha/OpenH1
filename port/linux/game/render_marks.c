/*
RENDER_MARKS.C

debug.render_marks (render_marks.h): the renderer's command stream told what
the draws that follow belong to, for the inspector's replay of a recording
(port/macos/tools/halo_replay.m --serve). The game's renderer calls these
where it knows (each a `port:` line in source/): the frame and each window's
camera (render.c), the category drawing (rasterizer_xbox_profile.c: a kind
for every draw), the model drawn and the object it is (models.c), each
model part's and structure material's shader (models.c, structure_render.c),
the transparent groups (rasterizer_xbox_transparent_geometry.c: the object,
the shader, or the particles', contrail's, lightning's or weather's that made
the group), the HUD's interfaces, a menu widget (its tag, and the generator
source that wrote it: menu_tags.c's pc_menu_widget_source), text, the
cinematics' letterbox and the debug drawing.

Each call is the encoder's (port/linux/src/xgpu_stream_encode.c:
xgpu_backend_mark_view and _owner), which sends a string once and an owner
only when it changes. With the setting off (read at each frame's start)
every call returns at once and nothing is sent. Nothing is allocated a
frame: the table of the tags' data (a shader's tag found from its data) is
made again only when the loaded tags change.
*/

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "objects/objects.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "game/game.h"
#include "scenario/scenario.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_enumerate.h"
#include "render_marks.h"

int config_boolean(char const *name);
void platform_log(char const *format, ...);
unsigned long config_changes(void);
void halo_screen_scale_axes(float scale[2]); /* port/linux/src/d3d8_device.c */

/* port/linux/src/xgpu_backend.h */
void xgpu_backend_mark_view(const char *map, unsigned long tick, long window, const float position[3],
	const float forward[3], const float up[3], float field_of_view, const long viewport[4]);
void xgpu_backend_mark_owner(int kind, long id, const char *label, const char *object, const char *model,
	const char *shader, const char *bitmap, const char *widget, const char *source);
/* menu_tags.c */
char const *pc_menu_widget_source(long definition_index);

int render_marks_on;

/* ---------- the owner */

struct render_mark_owner
{
	int kind;
	long id;
	char label[64];
	/* tag indices (NONE: none) */
	long object, model, shader, bitmap, widget;
	char const *source;
};

static struct render_mark_owner owner, text_saved;
static boolean text_drawing;

/* what is being drawn, kept with the setting off too (a few words a model
or transparent group): what render_marks_pipeline_unprepared names */
static struct
{
	int category;
	long object_index, model, shader;
	void const *shader_data;
	boolean first_person;
} drawing = { -1, NONE, NONE, NONE, NULL, FALSE };

static void owner_clear(int kind, char const *label)
{
	owner.kind = kind;
	owner.id = NONE;
	owner.label[0] = 0;
	if (label)
		snprintf(owner.label, sizeof(owner.label), "%s", label);
	owner.object = owner.model = owner.shader = owner.bitmap = owner.widget = NONE;
	owner.source = NULL;
}

/* a loaded tag's path, NULL for none */
static char const *tag_path(long tag_index)
{
	short absolute_index = (short)tag_index;

	if (tag_index == NONE || !global_tag_instances || absolute_index < 0 || absolute_index >= global_tag_count ||
		global_tag_instances[absolute_index].tag_index != tag_index)
		return NULL;
	return global_tag_instances[absolute_index].name;
}

static void owner_send(void)
{
	xgpu_backend_mark_owner(owner.kind, owner.id, owner.label, tag_path(owner.object), tag_path(owner.model),
		tag_path(owner.shader), tag_path(owner.bitmap), tag_path(owner.widget), owner.source);
}

/* ---------- a tag found from its data (a shader's, which the renderer
holds as a pointer) */

struct tag_by_data
{
	void const *data;
	long tag_index;
};

static struct
{
	struct tag_by_data *entries;
	long count;
	struct cache_file_tag_instance const *instances;
	long instance_count;
} tags_by_data;

static int __cdecl tag_by_data_compare(void const *a, void const *b)
{
	char const *x = ((struct tag_by_data const *)a)->data, *y = ((struct tag_by_data const *)b)->data;

	return x < y ? -1 : x > y;
}

static long tag_of_data(void const *data)
{
	long low = 0, high;

	if (!data || !global_tag_instances)
		return NONE;
	if (tags_by_data.instances != global_tag_instances || tags_by_data.instance_count != global_tag_count)
	{
		long index;

		if (tags_by_data.entries)
			free(tags_by_data.entries);
		tags_by_data.count = 0;
		tags_by_data.entries = malloc((global_tag_count + 1) * sizeof(*tags_by_data.entries));
		tags_by_data.instances = global_tag_instances;
		tags_by_data.instance_count = global_tag_count;
		if (!tags_by_data.entries)
			return NONE;
		for (index = 0; index < global_tag_count; index++)
		{
			if (global_tag_instances[index].base_address && global_tag_instances[index].tag_index != NONE)
			{
				tags_by_data.entries[tags_by_data.count].data = global_tag_instances[index].base_address;
				tags_by_data.entries[tags_by_data.count].tag_index = global_tag_instances[index].tag_index;
				tags_by_data.count++;
			}
		}
		qsort(tags_by_data.entries, tags_by_data.count, sizeof(*tags_by_data.entries), tag_by_data_compare);
	}
	high = tags_by_data.count - 1;
	while (low <= high)
	{
		long middle = (low + high) / 2;
		char const *at = tags_by_data.entries[middle].data;

		if (at == (char const *)data)
			return tags_by_data.entries[middle].tag_index;
		if (at < (char const *)data)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NONE;
}

/* ---------- the transparent groups made by particles, contrails,
lightning and weather: who made each (render_marks_next, before; the table
emptied each frame) */

#define GROUP_SLOTS 8192

static struct
{
	void const *groups[GROUP_SLOTS];
	unsigned char kinds[GROUP_SLOTS];
	long tags[GROUP_SLOTS];
	long count;
	int pending_kind;
	long pending_tag;
} made;

static unsigned long group_slot(void const *group)
{
	unsigned long value = (unsigned long)(size_t)group;

	return ((value >> 4) * 2654435761u) & (GROUP_SLOTS - 1);
}

/* ---------- public code */

void render_marks_frame_begin(void)
{
	/* (debug.render_marks, read again as the settings change: config_changes) */
	static unsigned long read_at = (unsigned long)-1;
	static boolean setting;
	long tick;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		setting = config_boolean("debug.render_marks") != 0;
	}
	render_marks_on = setting;
	if (!render_marks_on)
		return;
	memset(made.groups, 0, sizeof(made.groups));
	made.count = 0;
	made.pending_kind = NONE;
	text_drawing = FALSE;
	tick = game_time_initialized() ? game_time_get() : 0;
	xgpu_backend_mark_view(tag_path(global_scenario_index), (unsigned long)tick, -1, NULL, NULL, NULL, 0.0f, NULL);
	owner_clear(RENDER_MARK_OTHER, NULL);
	owner_send();
}

void render_marks_view(
	short window_index,
	struct render_camera const *camera)
{
	float position[3], forward[3], up[3];
	long viewport[4];
	float vertical, horizontal = 0.0f, scale[2];
	long width, height;

	if (!render_marks_on || !camera)
		return;
	position[0] = camera->position.x;
	position[1] = camera->position.y;
	position[2] = camera->position.z;
	forward[0] = camera->forward.i;
	forward[1] = camera->forward.j;
	forward[2] = camera->forward.k;
	up[0] = camera->up.i;
	up[1] = camera->up.j;
	up[2] = camera->up.k;
	width = camera->viewport_bounds.x1 - camera->viewport_bounds.x0;
	height = camera->viewport_bounds.y1 - camera->viewport_bounds.y0;
	/* (in the back buffer's pixels: the screen's units scaled) */
	halo_screen_scale_axes(scale);
	viewport[0] = (long)(camera->viewport_bounds.x0 * scale[0] + 0.5f);
	viewport[1] = (long)(camera->viewport_bounds.y0 * scale[1] + 0.5f);
	viewport[2] = (long)(width * scale[0] + 0.5f);
	viewport[3] = (long)(height * scale[1] + 0.5f);
	/* (the camera's is vertical: the stream's is horizontal) */
	vertical = camera->vertical_field_of_view;
	if (height > 0)
		horizontal = 2.0f * atanf(tanf(vertical * 0.5f) * (float)width / (float)height);
	xgpu_backend_mark_view(tag_path(global_scenario_index),
		(unsigned long)(game_time_initialized() ? game_time_get() : 0), window_index, position, forward, up,
		horizontal, viewport);
}

/* rasterizer.h's _rasterizer_profile_*: each category's kind and label */
static struct
{
	unsigned char kind;
	char const *label;
} const categories[] =
{
	{ RENDER_MARK_CLEAR, "clear" },
	{ RENDER_MARK_SKY, "sky" },
	{ RENDER_MARK_WORLD_OBJECT, "models" },
	{ RENDER_MARK_BSP, "environment lightmaps" },
	{ RENDER_MARK_SHADOW, "environment shadows" },
	{ RENDER_MARK_BSP, "environment diffuse lights" },
	{ RENDER_MARK_DECAL, "decals (light)" },
	{ RENDER_MARK_DECAL, "decals (alpha tested)" },
	{ RENDER_MARK_BSP, "environment textures" },
	{ RENDER_MARK_DECAL, "decals (primary)" },
	{ RENDER_MARK_DECAL, "decals (secondary)" },
	{ RENDER_MARK_BSP, "environment specular lights" },
	{ RENDER_MARK_BSP, "environment specular lightmaps" },
	{ RENDER_MARK_BSP, "environment reflection masks" },
	{ RENDER_MARK_BSP, "environment reflection mirrors" },
	{ RENDER_MARK_BSP, "environment reflections" },
	{ RENDER_MARK_BSP, "environment transparents" },
	{ RENDER_MARK_FOG, "fog" },
	{ RENDER_MARK_FOG, "fog screen" },
	{ RENDER_MARK_WATER, "water" },
	{ RENDER_MARK_DECAL, "decals (water)" },
	{ RENDER_MARK_DETAIL_OBJECT, "detail objects" },
	{ RENDER_MARK_EFFECT, "transparents" },
	{ RENDER_MARK_LENS_FLARE, "lens flare occlusion" },
	{ RENDER_MARK_LENS_FLARE, "lens flare occlusion query" },
	{ RENDER_MARK_LENS_FLARE, "lens flares" },
	{ RENDER_MARK_SCREEN_EFFECT, "screen effect" },
	{ RENDER_MARK_HUD, "hud" },
	{ RENDER_MARK_SCREEN_EFFECT, "screen flash" },
};

static int category_kind = RENDER_MARK_OTHER;

void render_marks_category(
	int category)
{
	drawing.category = category;
	drawing.object_index = drawing.model = drawing.shader = NONE;
	drawing.shader_data = NULL;
	drawing.first_person = FALSE;
	if (!render_marks_on)
		return;
	if (category >= 0 && category < (int)NUMBEROF(categories))
	{
		category_kind = categories[category].kind;
		owner_clear(category_kind, categories[category].label);
	}
	else
	{
		category_kind = RENDER_MARK_OTHER;
		owner_clear(RENDER_MARK_OTHER, NULL);
	}
	owner_send();
}

void render_marks_model(
	long model_index,
	long object_index,
	int first_person,
	int shadow)
{
	struct object_datum *object;
	char const *name;

	drawing.object_index = shadow ? NONE : object_index;
	drawing.model = model_index;
	drawing.shader = NONE;
	drawing.shader_data = NULL;
	drawing.first_person = first_person != 0;
	if (!render_marks_on)
		return;
	owner_clear(shadow ? RENDER_MARK_SHADOW : first_person ? RENDER_MARK_FIRST_PERSON :
		category_kind == RENDER_MARK_SKY ? RENDER_MARK_SKY : RENDER_MARK_WORLD_OBJECT, NULL);
	owner.model = model_index;
	object = object_index != NONE && owner.kind != RENDER_MARK_SKY ? object_try_and_get(object_index) : NULL;
	if (object)
	{
		owner.id = object_index;
		owner.object = object->definition_index;
	}
	name = tag_path(owner.object != NONE ? owner.object : model_index);
	if (name)
	{
		if (owner.id != NONE)
			snprintf(owner.label, sizeof(owner.label), "%s #%ld", tag_name_strip_path(name),
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(owner.id));
		else
			snprintf(owner.label, sizeof(owner.label), "%s", tag_name_strip_path(name));
	}
	owner_send();
}

void render_marks_model_shader(
	long shader_index)
{
	drawing.shader = shader_index;
	if (!render_marks_on || owner.shader == shader_index)
		return;
	owner.shader = shader_index;
	owner_send();
}

void render_marks_structure(
	long shader_index,
	short lightmap_index,
	short material_index)
{
	char const *name;

	drawing.object_index = drawing.model = NONE;
	drawing.shader = shader_index;
	drawing.shader_data = NULL;
	drawing.first_person = FALSE;
	if (!render_marks_on)
		return;
	owner_clear(category_kind == RENDER_MARK_OTHER ? RENDER_MARK_BSP : category_kind, NULL);
	owner.id = ((long)lightmap_index << 16) | (material_index & 0xFFFF);
	owner.shader = shader_index;
	name = tag_path(shader_index);
	snprintf(owner.label, sizeof(owner.label), "%s (lightmap %d, material %d)",
		name ? tag_name_strip_path(name) : "structure", lightmap_index, material_index);
	owner_send();
}

void render_marks_next(
	int kind,
	long tag_index)
{
	if (!render_marks_on)
		return;
	made.pending_kind = kind;
	made.pending_tag = tag_index;
}

void render_marks_group_made(
	void const *group)
{
	unsigned long slot;

	if (!render_marks_on || !group || made.pending_kind == NONE || made.count * 2 >= GROUP_SLOTS)
		return;
	for (slot = group_slot(group); made.groups[slot] && made.groups[slot] != group; slot = (slot + 1) & (GROUP_SLOTS - 1))
		;
	if (!made.groups[slot])
		made.count++;
	made.groups[slot] = group;
	made.kinds[slot] = (unsigned char)made.pending_kind;
	made.tags[slot] = made.pending_tag;
}

void render_marks_transparent(
	void const *group,
	unsigned long geometry_flags,
	long object_index,
	void const *shader,
	int first_person,
	int sky,
	int water)
{
	unsigned long slot;
	struct object_datum *object = NULL;
	char const *name;

	drawing.object_index = object_index != 0 ? object_index : NONE;
	drawing.model = drawing.shader = NONE;
	drawing.shader_data = shader;
	drawing.first_person = first_person != 0;
	if (!render_marks_on)
		return;
	owner_clear(first_person ? RENDER_MARK_FIRST_PERSON : sky ? RENDER_MARK_SKY : water ? RENDER_MARK_WATER :
		RENDER_MARK_EFFECT, NULL);
	(void)geometry_flags;
	owner.shader = tag_of_data(shader);
	for (slot = group_slot(group); made.groups[slot]; slot = (slot + 1) & (GROUP_SLOTS - 1))
	{
		if (made.groups[slot] == group)
		{
			owner.kind = made.kinds[slot];
			owner.bitmap = made.tags[slot];
			break;
		}
	}
	/* (a model's transparent part: its object's; 0 is a dynamic group's) */
	if (object_index != NONE && object_index != 0)
		object = object_try_and_get(object_index);
	if (object)
	{
		if (owner.kind == RENDER_MARK_EFFECT)
			owner.kind = RENDER_MARK_WORLD_OBJECT;
		owner.id = object_index;
		owner.object = object->definition_index;
	}
	name = tag_path(owner.object != NONE ? owner.object : owner.shader != NONE ? owner.shader : owner.bitmap);
	if (name && owner.id != NONE)
		snprintf(owner.label, sizeof(owner.label), "%s #%ld", tag_name_strip_path(name),
			(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(owner.id));
	else if (name)
		snprintf(owner.label, sizeof(owner.label), "%s", tag_name_strip_path(name));
	owner_send();
}

void render_marks_widget(
	long definition_index,
	void const *widget,
	long bitmap_index)
{
	char const *name;

	if (!render_marks_on)
		return;
	owner_clear(RENDER_MARK_MENU_WIDGET, NULL);
	owner.id = (long)(size_t)widget;
	owner.widget = definition_index;
	owner.bitmap = bitmap_index;
	owner.source = pc_menu_widget_source(definition_index);
	name = tag_path(definition_index);
	if (name)
		snprintf(owner.label, sizeof(owner.label), "%s", tag_name_strip_path(name));
	owner_send();
}

void render_marks_hud(
	long tag_index,
	char const *label)
{
	if (!render_marks_on)
		return;
	owner_clear(RENDER_MARK_HUD, label);
	owner.object = tag_index;
	owner_send();
}

void render_marks_kind(
	int kind,
	char const *label)
{
	if (!render_marks_on)
		return;
	owner_clear(kind, label);
	owner_send();
}

/* text drawn: its own owner, but in a menu widget (the widget's) and the
HUD (the HUD's, labelled with it); the owner before it again after */
static void text_begin(char const *utf8)
{
	if (owner.kind == RENDER_MARK_MENU_WIDGET)
		return;
	/* (the text before not ended: drawn by draw_string alone; this one's
	label from here on) */
	if (text_drawing)
	{
		if (owner.kind == RENDER_MARK_TEXT || owner.kind == RENDER_MARK_HUD)
		{
			snprintf(owner.label, sizeof(owner.label), "%s", utf8);
			owner_send();
		}
		return;
	}
	text_saved = owner;
	text_drawing = TRUE;
	if (owner.kind != RENDER_MARK_HUD)
		owner_clear(RENDER_MARK_TEXT, NULL);
	snprintf(owner.label, sizeof(owner.label), "%s", utf8);
	owner_send();
}

void render_marks_text(
	char const *string)
{
	char text[41];

	if (!render_marks_on || !string)
		return;
	snprintf(text, sizeof(text), "%s", string);
	text_begin(text);
}

void render_marks_unicode_text(
	unsigned short const *string)
{
	char text[44];
	long used = 0;

	if (!render_marks_on || !string)
		return;
	/* (UTF-16 as UTF-8, the first 40 bytes' worth) */
	for (; *string && used < 40; string++)
	{
		unsigned long character = *string;

		if (character < 0x80)
		{
			text[used++] = (char)character;
		}
		else if (character < 0x800)
		{
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
	text_begin(text);
}

void render_marks_text_end(void)
{
	if (!render_marks_on || !text_drawing)
		return;
	text_drawing = FALSE;
	owner = text_saved;
	owner_send();
}

/* (port/linux/src/xgpu_stream_encode.c, as a draw needs a pipeline the
stream has not been given: in play, one the map's preparation did not make,
which the backend makes on its compilers, the draw skipped until it is) the
draw named in the log, what it draws and in which category: a report's log
then says what was missing. A few a map at most */
#define UNPREPARED_PIPELINES_LOGGED 16

void render_marks_pipeline_unprepared(void)
{
	static long scenario = NONE;
	static short logged;
	struct object_datum *object = NULL;
	char const *object_name = NULL, *model_name, *shader_name;
	long shader;

	if (render_enumerating || global_scenario_index == NONE)
		return;
	if (scenario != global_scenario_index)
	{
		scenario = global_scenario_index;
		logged = 0;
	}
	if (logged >= UNPREPARED_PIPELINES_LOGGED)
		return;
	logged++;
	if (drawing.object_index != NONE)
		object = object_try_and_get(drawing.object_index);
	if (object)
		object_name = tag_path(object->definition_index);
	model_name = tag_path(drawing.model);
	shader = drawing.shader != NONE ? drawing.shader : tag_of_data(drawing.shader_data);
	shader_name = tag_path(shader);
	platform_log("render: a pipeline the map's preparation did not make, made now (the draw skipped until it is): "
		"%s%s%s%s%s%s%s%s%s", drawing.category >= 0 && drawing.category < (int)NUMBEROF(categories) ?
		categories[drawing.category].label : "other",
		object_name ? ", object " : "", object_name ? object_name : "",
		model_name ? ", model " : "", model_name ? model_name : "",
		shader_name ? ", shader " : "", shader_name ? shader_name : "",
		drawing.first_person ? ", first person" : "", logged == UNPREPARED_PIPELINES_LOGGED ? " (no more logged in this map)" : "");
}
