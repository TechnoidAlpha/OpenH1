/*
MENU_MODELS.C

The menus' models (menu_models.h): the Spartan's tags exported from a stock
multiplayer map the start's pictures pass has loaded, and read into ui.map's
tag table as its tags load.

The export takes what the map has loaded, as the game reads it: each tag's
root, blocks and data through its schema (menu_models_file.c), a model
part's vertices and indices from its buffers, a bitmap's pixels read from
the map's file as the texture cache reads them. The file written is read
back as the menus read it (checked, its offsets made pointers, validated)
before its note says it is made: a file the menus would refuse is never
noted as made.

The import reads the file whole, checks it (its header, its CRC-32), makes
its offsets pointers through the schemas, and has the validator check its
tags as a map's are (tag_validate_tags_apart, the map's own validation kept):
any of them failing is no Spartan, the file and its note deleted so that the
next start makes them again. Its tags are then added after the menus' own
(menu_tags.c), each reference and tag index made the table's, each model
part given buffers of its vertices and indices as a Custom Edition model's
are (rasterizer_vertex_buffer_new), each bitmap drawn from the file's pixels
(menu_models_bitmap_read, which the texture cache asks: xbox_texture_cache.c).
All of it is let go as ui.map's tags are (menu_tags_release).
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "rasterizer/rasterizer_geometry.h"
#include "tag_files/tag_groups.h"

#include "tag_schema.h"
#include "menu_models.h"
#include "menu_pictures.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------- prototypes */

void platform_log(char const *format, ...);
char const *platform_data_root(void);
int config_boolean(char const *name);
int mkdir(const char *path, unsigned int mode);
unsigned long long SDL_GetTicksNS(void);

/* ---------- constants */

#define MENU_MODELS_FOLDER "menu_models"
#define MENU_MODELS_FILE "spartan"
/* the multiplayer cyborg and its rifle (each stock multiplayer map's) */
#define SPARTAN_BIPED "characters\\cyborg_mp\\cyborg_mp"
#define SPARTAN_WEAPON "weapons\\assault rifle\\assault rifle"
/* the map taken first, and the multiplayer maps' scenarios */
#define SPARTAN_MAP "bloodgulch"
#define MULTIPLAYER_SCENARIOS "levels\\test\\"

enum
{
	BIPED_TAG = 'bipd',
	WEAPON_TAG = 'weap',
	SCENERY_TAG = 'scen',
	MODEL_TAG = 'mode',
	ANIMATION_GRAPH_TAG = 'antr',
	BITMAP_TAG = 'bitm',
	SHADER_TAG = 'shdr',

	/* a failed export is tried this many times for the same map */
	MENU_MODELS_FAILURES = 2,
	/* what the game rounds a read up to (cache_files.c) */
	CACHE_FILE_SECTOR_SIZE = 512,

	_bitmap_cached_bit = 7,
};

/* ---------- structures */

/* models.c's */
struct menu_model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct menu_model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

typedef char verify_menu_model_geometry_part_size[sizeof(struct menu_model_geometry_part) == 0x68 ? 1 : -1];
typedef char verify_menu_models_instance_size[
	sizeof(struct menu_models_instance) == sizeof(struct cache_file_tag_instance) ? 1 : -1];

/* the tags an export takes: the map's tag of each of the file's (NONE for
the scenery made) */
struct export
{
	struct menu_models_writer writer;
	long sources[MENU_MODELS_MAXIMUM_TAGS];
	long count;
};

/* ---------- globals */

static struct
{
	/* the file read (its tags added to ui.map's) */
	unsigned char *file;
	unsigned long file_size;
	struct menu_models_file_header header;
	struct menu_models_tag_header *tags;
	/* the tag table's indices of the file's tags */
	long tag_indices[MENU_MODELS_MAXIMUM_TAGS];
	long tag_count;
	boolean added;
	/* the model parts given buffers */
	struct menu_model_geometry_part **parts;
	long part_count;
} menu_models;

/* (the game's free, debug_free, takes no NULL) */
static void models_free(
	void *pointer)
{
	if (pointer)
		free(pointer);
}

/* size bytes, zeros (the game's malloc, which its free takes) */
static void *models_zeroed(
	unsigned long size)
{
	void *pointer = malloc(size ? size : 1);

	if (pointer)
		memset(pointer, 0, size ? size : 1);
	return pointer;
}

/* ---------- the files */

static void models_path(
	char *path,
	size_t size,
	char const *extension)
{
	/* (the game's files are opened by their Xbox paths, d: the data root:
	port/linux/src/xbox_files.c) */
	snprintf(path, size, "d:\\%s\\%s.%s", MENU_MODELS_FOLDER, MENU_MODELS_FILE, extension);
}

/* a whole file (malloc's), or NULL */
static unsigned char *models_read_file(
	char const *path,
	unsigned long maximum,
	unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bytes;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || (unsigned long)length > maximum ||
		fseek(file, 0, SEEK_SET) != 0 || !(bytes = malloc((size_t)length + 1)))
	{
		fclose(file);
		return NULL;
	}
	if (fread(bytes, 1, (size_t)length, file) != (size_t)length)
	{
		fclose(file);
		models_free(bytes);
		return NULL;
	}
	fclose(file);
	bytes[length] = 0;
	*size = (unsigned long)length;
	return bytes;
}

/* a file written whole or not at all (a .new beside it, renamed over it) */
static boolean models_write_file(
	char const *path,
	void const *bytes,
	unsigned long size)
{
	char temporary[1100];
	FILE *file;
	boolean written;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
	{
		platform_log("menu models: %s cannot be opened", temporary);
		return FALSE;
	}
	written = fwrite(bytes, 1, size, file) == size;
	written = fclose(file) == 0 && written;
	if (written)
	{
		remove(path);
		written = rename(temporary, path) == 0;
	}
	if (!written)
	{
		platform_log("menu models: %s (%lu bytes) cannot be written", path, size);
		remove(temporary);
	}
	return written;
}

static boolean models_note_read(
	struct menu_picture_note *note)
{
	char path[1100];
	unsigned long size;
	char *text;
	boolean read;

	models_path(path, sizeof(path), "txt");
	text = (char *)models_read_file(path, 4096, &size);
	read = text && menu_picture_note_parse(text, note);
	models_free(text);
	return read;
}

static void models_note_write(
	struct menu_picture_note const *note)
{
	char path[1100], text[256];

	models_path(path, sizeof(path), "txt");
	if (menu_picture_note_format(note, text, sizeof(text)))
		models_write_file(path, text, (unsigned long)strlen(text));
}

/* the file and its note deleted: made again at the next start */
static void models_forget(
	void)
{
	char path[1100];

	models_path(path, sizeof(path), "txt");
	remove(path);
	models_path(path, sizeof(path), "bin");
	remove(path);
}

/* ---------- the file read and checked */

/* a file's bytes checked, and its tags made pointers and validated: FALSE,
and why, if any of it is wrong (the bytes are then of no use) */
static boolean models_check(
	unsigned char *file,
	unsigned long size,
	struct menu_models_file_header *header,
	long *corrections,
	char const **why)
{
	if (!menu_models_file_check(file, size, header, why) ||
		!menu_models_relocate(file + header->tags_offset, header->tags_size, why))
	{
		return FALSE;
	}
	{
		void *saved = malloc(tag_validate_apart_size());
		boolean valid;

		if (!saved)
		{
			*why = "out of memory";
			return FALSE;
		}
		valid = tag_validate_tags_apart(file + header->tags_offset, (long)header->tags_size, (long)size,
			MENU_MODELS_FOLDER "\\" MENU_MODELS_FILE ".bin", saved, corrections);
		models_free(saved);
		if (!valid)
		{
			*why = "the validator refused its tags";
			return FALSE;
		}
	}
	return TRUE;
}

/* ---------- the export */

static boolean group_exported(
	struct cache_file_tag_instance const *instance)
{
	return instance->group_tag == MODEL_TAG || instance->group_tag == ANIMATION_GRAPH_TAG ||
		instance->group_tag == BITMAP_TAG || instance->group_tag == SHADER_TAG ||
		instance->parent_group_tags[0] == SHADER_TAG || instance->parent_group_tags[1] == SHADER_TAG;
}

/* the file's index of a map's tag a reference names: taken if it is of a
group the Spartan is drawn with (a model, a graph, a shader, a bitmap), else
none */
static long export_tag_index(
	void *context,
	long tag_index,
	unsigned long group_tag)
{
	struct export *export = context;
	short absolute_index = (short)tag_index;
	long index;

	(void)group_tag;
	if (tag_index == NONE || absolute_index < 0 || absolute_index >= global_tag_count ||
		global_tag_instances[absolute_index].tag_index != tag_index ||
		!global_tag_instances[absolute_index].base_address ||
		!group_exported(&global_tag_instances[absolute_index]))
	{
		return NONE;
	}
	for (index = 0; index < export->count; index++)
	{
		if (export->sources[index] == tag_index)
			return menu_models_tag_index(index);
	}
	if (export->count >= MENU_MODELS_MAXIMUM_TAGS)
	{
		platform_log("menu models: more than %d tags: %s left out", MENU_MODELS_MAXIMUM_TAGS,
			global_tag_instances[absolute_index].name);
		return NONE;
	}
	export->sources[export->count] = tag_index;
	return menu_models_tag_index(export->count++);
}

/* size bytes at offset in the map's file, read as the texture cache reads
them (rounded up to a sector), at once */
static boolean export_file_read(
	void *context,
	long offset,
	long size,
	void *buffer)
{
	unsigned long rounded = ((unsigned long)size + CACHE_FILE_SECTOR_SIZE - 1) & ~(unsigned long)(CACHE_FILE_SECTOR_SIZE - 1);
	byte *read = malloc(rounded);
	boolean done = FALSE;

	(void)context;
	if (!read || offset < 0 || size < 0)
	{
		models_free(read);
		return FALSE;
	}
	cache_file_read(NONE, offset, size, read, &done, TRUE);
	cache_file_block_until_not_busy();
	if (done == TRUE)
		memcpy(buffer, read, (size_t)size);
	models_free(read);
	return done == TRUE;
}

static int export_file_read_proc(
	void *context,
	long offset,
	long size,
	void *buffer)
{
	return export_file_read(context, offset, size, buffer);
}

/* what the schema cannot say: a model part's vertices and indices (its
buffers' data), a bitmap's pixels */
static int export_element(
	void *context,
	struct tag_schema_definition const *definition,
	unsigned long offset,
	void const *source)
{
	struct export *export = context;
	struct menu_models_writer *writer = &export->writer;

	if (!strcmp(definition->name, "model_geometry_part"))
	{
		struct menu_model_geometry_part const *part = source;
		struct menu_model_geometry_part *written = (struct menu_model_geometry_part *)(writer->tags + offset);
		long vertex_size = rasterizer_geometry_get_vertex_size(part->vertex_buffer.type);
		long index_count = part->triangle_buffer.type == _triangle_buffer_type_triangles ?
			3 * part->triangle_buffer.count :
			part->triangle_buffer.count > 0 ? part->triangle_buffer.count + 2 : 0;
		void *vertices, *indices;

		written->vertex_buffer.base_address = NULL;
		written->vertex_buffer.offset = 0;
		written->vertex_buffer.hardware_format = NULL;
		written->triangle_buffer.base_address = NULL;
		written->triangle_buffer.hardware_format = NULL;
		if (!part->vertex_buffer.hardware_format || !part->triangle_buffer.hardware_format ||
			part->vertex_buffer.count <= 0 || index_count <= 0)
		{
			/* (a part the map does not draw: nor does the file) */
			return TRUE;
		}
		/* (a buffer's Data, registered: the tag cache's physical address,
		which the contiguous window maps; an index buffer's, its address) */
		vertices = (void *)(((unsigned long *)part->vertex_buffer.hardware_format)[1] | 0x80000000UL);
		indices = (void *)(((unsigned long *)part->triangle_buffer.hardware_format)[1] | 0x80000000UL);
		if (!cache_file_tag_cache_contains(vertices, part->vertex_buffer.count * vertex_size) ||
			!cache_file_tag_cache_contains(indices, index_count * (long)sizeof(word)))
		{
			platform_log("menu models: a model part's buffers are not in the map's tags");
			return FALSE;
		}
		return menu_models_writer_part(writer,
			offset + offsetof(struct menu_model_geometry_part, vertex_buffer.hardware_format),
			offset + offsetof(struct menu_model_geometry_part, triangle_buffer.hardware_format),
			vertices, (unsigned long)(part->vertex_buffer.count * vertex_size), indices,
			(unsigned long)(index_count * sizeof(word)));
	}
	if (!strcmp(definition->name, "bitmap_data"))
	{
		struct bitmap_data *written = (struct bitmap_data *)(writer->tags + offset);

		/* (its pixels given as its group's are: below) */
		written->flags &= (unsigned short)~FLAG(_bitmap_cached_bit);
		written->cache_block_index = NONE;
		written->hardware_format = NULL;
		written->base_address = NULL;
		written->pixels_offset = 0;
		return TRUE;
	}
	if (!strcmp(definition->name, "bitmap_group"))
	{
		struct bitmap_group const *group = source;
		struct bitmap_group *written = (struct bitmap_group *)(writer->tags + offset);
		unsigned long bitmaps = (unsigned long)written->bitmaps.address;
		long bitmap_index;

		/* (each bitmap's pixels its own, at its offset in the file) */
		csmemset(&written->pixel_data, 0, sizeof(written->pixel_data));
		for (bitmap_index = 0; bitmap_index < group->bitmaps.count; bitmap_index++)
		{
			struct bitmap_data const *bitmap = (struct bitmap_data const *)group->bitmaps.address + bitmap_index;
			unsigned long field = bitmaps + bitmap_index * sizeof(struct bitmap_data) +
				offsetof(struct bitmap_data, pixels_offset);
			/* (where the map's file has them: the texture cache adds the
			group's offset to a bitmap's as it first takes the bitmap,
			texture_cache_bitmap_new, which marks it cached) */
			long pixels_offset = TEST_FLAG(bitmap->flags, _bitmap_cached_bit) ? bitmap->pixels_offset :
				group->pixel_data.file_offset + bitmap->pixels_offset;
			void *pixels;
			boolean copied;

			if (bitmap->pixels_size <= 0 || pixels_offset < 0)
				return FALSE;
			pixels = malloc((size_t)bitmap->pixels_size);
			copied = pixels && export_file_read(context, pixels_offset, bitmap->pixels_size, pixels) &&
				menu_models_writer_pixels(writer, pixels, (unsigned long)bitmap->pixels_size, field);
			models_free(pixels);
			if (!copied)
				return FALSE;
		}
		return TRUE;
	}
	return TRUE;
}

/* a scenery definition of an object's (the biped's, the weapon's): its
shape and model and graph, nothing else (no attachments, functions, change
colors, collision or physics) */
static boolean export_scenery(
	struct menu_models_copy *copy,
	long index,
	long object_index,
	char const *name,
	boolean animated)
{
	struct tag_schema_group const *group = menu_models_schema_group(SCENERY_TAG);
	struct object_definition scenery;

	if (!group || group->definition->size != sizeof(scenery))
		return FALSE;
	memcpy(&scenery, object_definition_get(object_index), sizeof(scenery));
	scenery.object.type = _object_type_scenery;
	scenery.object.runtime_flags = 0;
	if (!animated)
		scenery.object.animation_graph.index = NONE;
	scenery.object.collision_model.index = NONE;
	scenery.object.physics.index = NONE;
	scenery.object.modifier_shader.index = NONE;
	scenery.object.creation_effect.index = NONE;
	csmemset(&scenery.object.attachments, 0, sizeof(scenery.object.attachments));
	csmemset(&scenery.object.widgets, 0, sizeof(scenery.object.widgets));
	csmemset(&scenery.object.functions, 0, sizeof(scenery.object.functions));
	csmemset(&scenery.object.change_colors, 0, sizeof(scenery.object.change_colors));
	csmemset(&scenery.object.predicted_resources, 0, sizeof(scenery.object.predicted_resources));
	return menu_models_copy_tag(copy, SCENERY_TAG, group->parent_group_tags, name, &scenery, group->definition) ==
		menu_models_tag_index(index);
}

long menu_models_export_pose(
	void)
{
	long poses[MAXIMUM_MENU_PICTURES];
	long count = menu_pictures_maps(poses, MAXIMUM_MENU_PICTURES), index, chosen = -1;
	struct menu_picture_note note;
	unsigned long checksum;
	char path[1100];
	FILE *file;
	boolean whole = FALSE;

	/* (Blood Gulch, else the first multiplayer map the player has, stock) */
	for (index = 0; index < count && chosen < 0; index++)
	{
		if (!strcasecmp(menu_picture_poses[poses[index]].map, SPARTAN_MAP))
			chosen = poses[index];
	}
	for (index = 0; index < count && chosen < 0; index++)
	{
		if (!strncasecmp(menu_picture_poses[poses[index]].scenario, MULTIPLAYER_SCENARIOS,
			strlen(MULTIPLAYER_SCENARIOS)))
		{
			chosen = poses[index];
		}
	}
	if (chosen < 0)
		return -1;
	checksum = menu_picture_map_checksum(menu_picture_poses[chosen].map);
	/* (its header and size: the rest is checked as ui.map reads it, which
	deletes a file it refuses, for the next start to make again) */
	models_path(path, sizeof(path), "bin");
	file = fopen(path, "rb");
	if (file)
	{
		struct menu_models_file_header header;
		long length;

		whole = fread(&header, sizeof(header), 1, file) == 1 && fseek(file, 0, SEEK_END) == 0 &&
			(length = ftell(file)) > 0 && header.signature == MENU_MODELS_SIGNATURE &&
			header.version == MENU_MODELS_VERSION && header.file_size == (unsigned long)length &&
			header.map_checksum == checksum;
		fclose(file);
	}
	if (models_note_read(&note) && note.pose_hash == MENU_MODELS_VERSION && note.map_checksum == checksum)
	{
		if (note.result == _menu_picture_rendered && whole)
			return -1;
		if (note.failures >= MENU_MODELS_FAILURES)
			return -1;
	}
	return chosen;
}

/* the note before an export: a try counted (a game gone in the middle of
one has it counted as failed) */
static void export_note_begin(
	unsigned long checksum)
{
	struct menu_picture_note note;

	if (!models_note_read(&note) || note.pose_hash != MENU_MODELS_VERSION || note.map_checksum != checksum)
	{
		memset(&note, 0, sizeof(note));
		note.pose_hash = MENU_MODELS_VERSION;
		note.map_checksum = checksum;
	}
	note.result = _menu_picture_none;
	note.scale = 1;
	note.failures++;
	models_note_write(&note);
}

void menu_models_export_failed(
	char const *why)
{
	platform_log("menu models: the Spartan was not exported (%s): the menus draw its pictures", why);
}

int menu_models_export(
	void)
{
	unsigned long long began = SDL_GetTicksNS();
	char const *map = cache_file_loaded_map_name();
	unsigned long checksum = menu_picture_map_checksum(map);
	long biped = tag_loaded(BIPED_TAG, SPARTAN_BIPED);
	long weapon = tag_loaded(WEAPON_TAG, SPARTAN_WEAPON);
	struct export *export = models_zeroed(sizeof(*export));
	struct menu_models_copy copy;
	unsigned char *file = NULL;
	unsigned long size = 0;
	char folder[1100], path[1100];
	char const *why = NULL;
	long index;

	/* (the folder first: the note is written before anything else) */
	snprintf(folder, sizeof(folder), "%s/%s", platform_data_root(), MENU_MODELS_FOLDER);
	mkdir(folder, 0755);
	export_note_begin(checksum);
	if (!export)
	{
		menu_models_export_failed("out of memory");
		return FALSE;
	}
	if (biped == NONE || weapon == NONE)
	{
		models_free(export);
		menu_models_export_failed(biped == NONE ? "the map has no multiplayer cyborg" : "the map has no assault rifle");
		return FALSE;
	}
	menu_models_writer_new(&export->writer);
	memset(&copy, 0, sizeof(copy));
	copy.writer = &export->writer;
	copy.context = export;
	copy.tag_index = export_tag_index;
	copy.file_read = export_file_read_proc;
	copy.element = export_element;
	/* the two sceneries (their places counted first: what they name is
	given the places after them), then what they name, and what that names,
	in turn */
	export->sources[0] = export->sources[1] = NONE;
	export->count = 2;
	if (!export_scenery(&copy, 0, biped, MENU_MODELS_SPARTAN, TRUE) ||
		!export_scenery(&copy, 1, weapon, MENU_MODELS_RIFLE, FALSE))
	{
		why = export->writer.why ? export->writer.why : "its scenery cannot be made";
	}
	for (index = 2; !why && index < export->count; index++)
	{
		struct cache_file_tag_instance const *instance =
			&global_tag_instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(export->sources[index])];
		struct tag_schema_group const *group = menu_models_schema_group(instance->group_tag);

		if (!group || !group->definition ||
			menu_models_copy_tag(&copy, instance->group_tag, group->parent_group_tags, instance->name,
				instance->base_address, group->definition) != menu_models_tag_index(index))
		{
			why = export->writer.why ? export->writer.why : "a tag cannot be copied";
		}
	}
	if (!why)
	{
		file = menu_models_writer_finish(&export->writer, map, checksum, &size);
		if (!file)
			why = "out of memory";
	}
	if (!why)
	{
		/* (read back as ui.map reads it: a file it would refuse is not
		written) */
		unsigned char *check = malloc(size);
		struct menu_models_file_header header;

		if (!check)
		{
			why = "out of memory";
		}
		else
		{
			memcpy(check, file, size);
			long corrections = 0;

			if (!models_check(check, size, &header, &corrections, &why))
				platform_log("menu models: the file made would be refused: %s", why);
			else if (corrections)
				platform_log("menu models: the file made has %ld corrections", corrections);
			models_free(check);
		}
	}
	if (!why)
	{
		models_path(path, sizeof(path), "bin");
		if (!models_write_file(path, file, size))
			why = "it cannot be written";
	}
	if (!why)
	{
		struct menu_picture_note note;

		memset(&note, 0, sizeof(note));
		note.result = _menu_picture_rendered;
		note.pose_hash = MENU_MODELS_VERSION;
		note.map_checksum = checksum;
		note.scale = 1;
		models_note_write(&note);
		platform_log("menu models: the Spartan exported from %s: %ld tags, %lu KB of tags and %lu KB of pixels, "
			"in %.2f s", map, export->writer.tag_count, (export->writer.tags_size + 1023) / 1024,
			(export->writer.pixels_size + 1023) / 1024, (double)(SDL_GetTicksNS() - began) / 1e9);
		for (index = 0; index < export->writer.tag_count; index++)
		{
			char group[5];
			unsigned long tag = export->writer.written[index].group_tag;

			group[0] = (char)(tag >> 24), group[1] = (char)(tag >> 16), group[2] = (char)(tag >> 8);
			group[3] = (char)tag, group[4] = 0;
			platform_log("menu models:   %s %s", group, export->writer.written[index].name);
		}
	}
	else
	{
		menu_models_export_failed(why);
	}
	models_free(file);
	menu_models_writer_dispose(&export->writer);
	models_free(export);
	return why == NULL;
}

/* ---------- ui.map's tags */

long menu_models_tags_load(
	void)
{
	char path[1100];
	unsigned char *file;
	unsigned long size = 0;
	char const *why = NULL;
	long corrections = 0;

	menu_models_tags_release();
	if (!config_boolean("display.menu_pictures"))
		return 0;
	models_path(path, sizeof(path), "bin");
	file = models_read_file(path, MENU_MODELS_MAXIMUM_FILE_SIZE, &size);
	if (!file)
		return 0;
	if (!models_check(file, size, &menu_models.header, &corrections, &why))
	{
		platform_log("menu models: %s is refused (%s): the menus draw their pictures, and it is made again at the "
			"next start", path, why);
		models_free(file);
		models_forget();
		return 0;
	}
	menu_models.file = file;
	menu_models.file_size = size;
	menu_models.tags = (struct menu_models_tag_header *)(file + menu_models.header.tags_offset);
	menu_models.tag_count = menu_models.tags->tag_count;
	return menu_models.tag_count;
}

/* a model's parts given buffers of their vertices and indices (a part
whose buffers cannot be made is not drawn) */
static void models_parts_make(
	struct model *model)
{
	long geometry_index, part_index;

	for (geometry_index = 0; geometry_index < model->geometries.count; geometry_index++)
	{
		struct menu_model_geometry *geometry = (struct menu_model_geometry *)model->geometries.address + geometry_index;

		for (part_index = 0; part_index < geometry->parts.count; part_index++)
		{
			struct menu_model_geometry_part *part = (struct menu_model_geometry_part *)geometry->parts.address +
				part_index;
			void *vertex_buffer = part->vertex_buffer.hardware_format;
			void *index_buffer = part->triangle_buffer.hardware_format;
			long vertex_size = rasterizer_geometry_get_vertex_size(part->vertex_buffer.type);
			struct menu_model_geometry_part **parts;

			part->vertex_buffer.hardware_format = NULL;
			part->triangle_buffer.hardware_format = NULL;
			/* (the validator kept the buffers of the header's whose data are
			in the tags, or none) */
			if (!vertex_buffer || !index_buffer || part->vertex_buffer.count <= 0 || part->triangle_buffer.count <= 0)
				continue;
			parts = realloc(menu_models.parts, (menu_models.part_count + 1) * sizeof(*parts));
			if (!parts)
				continue;
			menu_models.parts = parts;
			if (!rasterizer_vertex_buffer_new(&part->vertex_buffer, part->vertex_buffer.type, part->vertex_buffer.count,
					*(void **)((byte *)vertex_buffer + 4), part->vertex_buffer.count * vertex_size))
			{
				continue;
			}
			if (!rasterizer_triangle_buffer_new(&part->triangle_buffer, part->triangle_buffer.type,
				part->triangle_buffer.count, *(void **)((byte *)index_buffer + 4)))
			{
				rasterizer_vertex_buffer_delete(&part->vertex_buffer);
				continue;
			}
			menu_models.parts[menu_models.part_count++] = part;
		}
	}
}

void menu_models_tags_add(
	void *instances_address,
	long const *tag_indices)
{
	struct cache_file_tag_instance *instances = instances_address;
	struct menu_models_instance *own;
	char *names[MENU_MODELS_MAXIMUM_TAGS];
	long index;

	if (!menu_models.file)
		return;
	own = menu_models.tags->instances;
	for (index = 0; index < menu_models.tag_count; index++)
	{
		names[index] = own[index].name ? own[index].name : "";
		menu_models.tag_indices[index] = tag_indices[index];
	}
	menu_models_remap((unsigned char *)menu_models.tags, tag_indices, names);
	for (index = 0; index < menu_models.tag_count; index++)
	{
		struct cache_file_tag_instance *instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_indices[index])];

		instance->group_tag = (long)own[index].group_tag;
		instance->parent_group_tags[0] = (long)own[index].parent_group_tags[0];
		instance->parent_group_tags[1] = (long)own[index].parent_group_tags[1];
		instance->tag_index = tag_indices[index];
		instance->name = names[index];
		instance->base_address = own[index].base_address;
		instance->unused[0] = instance->unused[1] = 0;
		if (own[index].group_tag == MODEL_TAG && own[index].base_address)
			models_parts_make(own[index].base_address);
		if (own[index].group_tag == BITMAP_TAG && own[index].base_address)
		{
			struct bitmap_group *group = own[index].base_address;
			long bitmap_index;

			/* (each the texture cache's to load, from the file:
			menu_models_bitmap_read) */
			for (bitmap_index = 0; bitmap_index < group->bitmaps.count; bitmap_index++)
			{
				struct bitmap_data *bitmap = (struct bitmap_data *)group->bitmaps.address + bitmap_index;

				bitmap->flags |= FLAG(_bitmap_cached_bit);
				bitmap->tag_index = tag_indices[index];
				bitmap->cache_block_index = NONE;
				bitmap->hardware_format = NULL;
				bitmap->base_address = NULL;
			}
		}
	}
	menu_models.added = TRUE;
	platform_log("menu models: the Spartan's %ld tags (from %s) added to ui.map's, %ld model parts", menu_models.tag_count,
		menu_models.header.map_name, menu_models.part_count);
}

void menu_models_tags_release(
	void)
{
	long index;

	for (index = 0; index < menu_models.part_count; index++)
	{
		rasterizer_triangle_buffer_delete(&menu_models.parts[index]->triangle_buffer);
		rasterizer_vertex_buffer_delete(&menu_models.parts[index]->vertex_buffer);
	}
	models_free(menu_models.parts);
	models_free(menu_models.file);
	memset(&menu_models, 0, sizeof(menu_models));
}

long menu_models_tag(
	unsigned long group_tag,
	char const *name)
{
	long index;

	for (index = 0; menu_models.added && index < menu_models.tag_count; index++)
	{
		struct menu_models_instance const *instance = &menu_models.tags->instances[index];

		if (instance->group_tag == group_tag && instance->name && !strcmp(instance->name, name))
			return menu_models.tag_indices[index];
	}
	return NONE;
}

int menu_models_bitmap_read(
	void *bitmap_address,
	void *buffer)
{
	struct bitmap_data const *bitmap = bitmap_address;
	long index;

	if (!menu_models.added || bitmap->tag_index == NONE)
		return FALSE;
	for (index = 0; index < menu_models.tag_count; index++)
	{
		if (menu_models.tag_indices[index] == bitmap->tag_index)
		{
			/* (the validator kept the pixels in the file) */
			if (bitmap->pixels_offset >= 0 && bitmap->pixels_size >= 0 &&
				(unsigned long)bitmap->pixels_offset <= menu_models.file_size &&
				(unsigned long)bitmap->pixels_size <= menu_models.file_size - (unsigned long)bitmap->pixels_offset)
			{
				memcpy(buffer, menu_models.file + bitmap->pixels_offset, (size_t)bitmap->pixels_size);
			}
			return TRUE;
		}
	}
	return FALSE;
}
