/*
MENU_MODELS_FILE.C

The menus' models' file (menu_models.h): written from a map's tags through
their schemas, and read back, every offset made a pointer as its schema
says, before the validator checks the tags as it checks a map's. Nothing
here reads the game: tools/harness/tests/test_menu_models.py runs it on
tags of its own.
*/

#include "cseries.h"
#include "rasterizer/rasterizer_geometry.h"
#include "tag_schema.h"
#include "menu_models.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the file's first tag's index's salt (any: the validator only asks
	that a tag's absolute index be its place in the table) */
	MENU_MODELS_TAG_SALT = 0x6D6D,
};

/* (the game's free, debug_free, takes no NULL) */
static void file_free(
	void *pointer)
{
	if (pointer)
		free(pointer);
}

/* size bytes, zeros (the game's malloc, which its free takes) */
static void *file_zeroed(
	unsigned long size)
{
	void *pointer = malloc(size ? size : 1);

	if (pointer)
		memset(pointer, 0, size ? size : 1);
	return pointer;
}

/* ---------- the file written */

void menu_models_writer_new(
	struct menu_models_writer *writer)
{
	memset(writer, 0, sizeof(*writer));
	/* (the tag header first, at offset 0, as a map's tags begin) */
	menu_models_writer_append(writer, NULL, sizeof(struct menu_models_tag_header));
}

void menu_models_writer_dispose(
	struct menu_models_writer *writer)
{
	file_free(writer->tags);
	file_free(writer->pixels);
	file_free(writer->pixel_fixups);
	file_free(writer->parts);
	file_free(writer->written);
	file_free(writer->references);
	memset(writer, 0, sizeof(*writer));
}

static void writer_fail(
	struct menu_models_writer *writer,
	char const *why)
{
	if (!writer->failed)
		writer->why = why;
	writer->failed = TRUE;
}

/* (a buffer grown to hold size bytes more: FALSE if it cannot be) */
static int writer_grow(
	unsigned char **bytes,
	unsigned long *capacity,
	unsigned long used,
	unsigned long more)
{
	unsigned long wanted = used + more;
	unsigned char *grown;

	if (wanted < used || wanted > MENU_MODELS_MAXIMUM_FILE_SIZE)
		return FALSE;
	if (wanted <= *capacity)
		return TRUE;
	wanted = MAX(wanted, *capacity * 2);
	wanted = MAX(wanted, 4096);
	grown = realloc(*bytes, wanted);
	if (!grown)
		return FALSE;
	*bytes = grown;
	*capacity = wanted;
	return TRUE;
}

unsigned long menu_models_writer_append(
	struct menu_models_writer *writer,
	void const *bytes,
	unsigned long size)
{
	unsigned long offset = (writer->tags_size + 7) & ~7UL;

	if (writer->failed)
		return 0;
	if (!writer_grow(&writer->tags, &writer->tags_capacity, writer->tags_size, offset - writer->tags_size + size))
	{
		writer_fail(writer, "the tags are too large");
		return 0;
	}
	memset(writer->tags + writer->tags_size, 0, offset - writer->tags_size);
	if (bytes)
		memcpy(writer->tags + offset, bytes, size);
	else
		memset(writer->tags + offset, 0, size);
	writer->tags_size = offset + size;
	return offset;
}

int menu_models_writer_pixels(
	struct menu_models_writer *writer,
	void const *bytes,
	unsigned long size,
	unsigned long field_offset)
{
	unsigned long offset = (writer->pixels_size + 15) & ~15UL;

	if (writer->failed)
		return FALSE;
	if (!writer_grow(&writer->pixels, &writer->pixels_capacity, writer->pixels_size, offset - writer->pixels_size + size))
	{
		writer_fail(writer, "the pixels are too large");
		return FALSE;
	}
	if (writer->pixel_fixup_count >= writer->pixel_fixup_capacity)
	{
		long capacity = MAX(16, writer->pixel_fixup_capacity * 2);
		unsigned long *fixups = realloc(writer->pixel_fixups, capacity * sizeof(*fixups));

		if (!fixups)
		{
			writer_fail(writer, "out of memory");
			return FALSE;
		}
		writer->pixel_fixups = fixups;
		writer->pixel_fixup_capacity = capacity;
	}
	memset(writer->pixels + writer->pixels_size, 0, offset - writer->pixels_size);
	memcpy(writer->pixels + offset, bytes, size);
	writer->pixels_size = offset + size;
	writer->pixel_fixups[writer->pixel_fixup_count++] = field_offset;
	*(long *)(writer->tags + field_offset) = (long)offset;
	return TRUE;
}

int menu_models_writer_part(
	struct menu_models_writer *writer,
	unsigned long vertex_field_offset,
	unsigned long index_field_offset,
	void const *vertices,
	unsigned long vertices_size,
	void const *indices,
	unsigned long indices_size)
{
	struct menu_models_part_buffers part;

	part.vertex_field_offset = vertex_field_offset;
	part.index_field_offset = index_field_offset;
	part.vertices_offset = menu_models_writer_append(writer, vertices, vertices_size);
	part.indices_offset = menu_models_writer_append(writer, indices, indices_size);
	if (writer->failed)
		return FALSE;
	if (writer->part_count >= writer->part_capacity)
	{
		long capacity = MAX(16, writer->part_capacity * 2);
		struct menu_models_part_buffers *parts = realloc(writer->parts, capacity * sizeof(*parts));

		if (!parts)
		{
			writer_fail(writer, "out of memory");
			return FALSE;
		}
		writer->parts = parts;
		writer->part_capacity = capacity;
	}
	writer->parts[writer->part_count++] = part;
	return TRUE;
}

long menu_models_tag_index(
	long absolute_index)
{
	return ((long)(MENU_MODELS_TAG_SALT + absolute_index) << 16) | absolute_index;
}

/* an element's fields copied from source (the element's bytes are already
at offset in the tags): blocks and data with their bytes, references and
tag indices the file's */
static void copy_fields(
	struct menu_models_copy *copy,
	unsigned long offset,
	unsigned char const *source,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct menu_models_writer *writer = copy->writer;
	struct tag_schema_field const *field;

	if (depth > MENU_MODELS_MAXIMUM_DEPTH)
	{
		writer_fail(writer, "a tag is too deep");
		return;
	}
	for (field = definition->fields; field->type != _tag_schema_terminator && !writer->failed; field++)
	{
		short index;

		for (index = 0; index < field->count && !writer->failed; index++)
		{
			unsigned long field_offset = offset + field->offset + index * field->size;
			unsigned char const *field_source = source + field->offset + index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				copy_fields(copy, field_offset, field_source, field->definition, depth);
				break;

			case _tag_schema_block:
			{
				struct tag_block const *block = (struct tag_block const *)field_source;
				struct tag_schema_definition const *element = field->definition;
				unsigned long elements = 0;
				long element_index;

				if (block->count > 0)
				{
					elements = menu_models_writer_append(writer, block->address,
						(unsigned long)block->count * element->size);
					if (writer->failed)
						break;
					for (element_index = 0; element_index < block->count && !writer->failed; element_index++)
					{
						copy_fields(copy, elements + element_index * element->size,
							(unsigned char const *)block->address + element_index * element->size, element, depth + 1);
					}
				}
				{
					struct tag_block *written = (struct tag_block *)(writer->tags + field_offset);

					written->count = block->count > 0 ? block->count : 0;
					written->address = (void *)(unsigned long)elements;
					written->definition = NULL;
				}
				break;
			}

			case _tag_schema_data:
			case _tag_schema_file_data:
			{
				struct tag_data const *data = (struct tag_data const *)field_source;
				unsigned long bytes = 0;
				int pixels = FALSE;

				if (data->size > 0 && field->type == _tag_schema_data)
				{
					bytes = menu_models_writer_append(writer, data->address, (unsigned long)data->size);
				}
				else if (data->size > 0)
				{
					/* (in the file after the tags, as a map's are after its) */
					void *buffer = malloc((size_t)data->size);

					if (!buffer || !copy->file_read || !copy->file_read(copy->context, data->file_offset, data->size,
						buffer))
					{
						writer_fail(writer, "data in the map's file cannot be read");
					}
					else
					{
						pixels = TRUE;
					}
					if (pixels)
						menu_models_writer_pixels(writer, buffer, (unsigned long)data->size,
							field_offset + offsetof(struct tag_data, file_offset));
					file_free(buffer);
				}
				if (writer->failed)
					break;
				{
					struct tag_data *written = (struct tag_data *)(writer->tags + field_offset);

					written->size = data->size > 0 ? data->size : 0;
					written->pad = 0;
					if (!pixels)
						written->file_offset = 0;
					written->address = (void *)(unsigned long)bytes;
					written->definition = NULL;
				}
				break;
			}

			case _tag_schema_reference:
			{
				struct tag_reference *written = (struct tag_reference *)(writer->tags + field_offset);
				long index = copy->tag_index(copy->context, written->index, written->group_tag);

				written->index = index;
				written->name = NULL;
				written->name_length = 0;
				/* (its tag's name, as a map's references have theirs: made as
				the file is finished) */
				if (writer->reference_count >= writer->reference_capacity)
				{
					long capacity = MAX(64, writer->reference_capacity * 2);
					struct menu_models_reference_name *references =
						realloc(writer->references, capacity * sizeof(*references));

					if (!references)
					{
						writer_fail(writer, "out of memory");
						break;
					}
					writer->references = references;
					writer->reference_capacity = capacity;
				}
				writer->references[writer->reference_count].offset = field_offset;
				writer->references[writer->reference_count].absolute_index = index == NONE ? NONE : (long)(short)index;
				writer->reference_count++;
				break;
			}

			case _tag_schema_tag_index:
			{
				long *written = (long *)(writer->tags + field_offset);

				*written = copy->tag_index(copy->context, *written, 0);
				break;
			}

			default:
				break;
			}
		}
	}
	if (!writer->failed && copy->element && !copy->element(copy->context, definition, offset, source))
		writer_fail(writer, "an element cannot be copied");
}

long menu_models_copy_tag(
	struct menu_models_copy *copy,
	unsigned long group_tag,
	unsigned long const parent_group_tags[2],
	char const *name,
	void const *source,
	struct tag_schema_definition const *definition)
{
	struct menu_models_writer *writer = copy->writer;
	struct menu_models_written_tag *written;
	long absolute_index = writer->tag_count;
	unsigned long root;

	if (writer->failed)
		return NONE;
	if (absolute_index >= MENU_MODELS_MAXIMUM_TAGS)
	{
		writer_fail(writer, "too many tags");
		return NONE;
	}
	written = realloc(writer->written, (absolute_index + 1) * sizeof(*written));
	if (!written)
	{
		writer_fail(writer, "out of memory");
		return NONE;
	}
	writer->written = written;
	written += absolute_index;
	memset(written, 0, sizeof(*written));
	written->group_tag = group_tag;
	written->parent_group_tags[0] = parent_group_tags[0];
	written->parent_group_tags[1] = parent_group_tags[1];
	strncpy(written->name, name ? name : "", sizeof(written->name) - 1);
	/* (counted first: what it names may name it back) */
	writer->tag_count++;
	root = menu_models_writer_append(writer, source, definition->size);
	written = &writer->written[absolute_index];
	written->root_offset = root;
	if (!writer->failed)
		copy_fields(copy, root, source, definition, 0);
	return writer->failed ? NONE : menu_models_tag_index(absolute_index);
}

unsigned long menu_models_crc(
	unsigned long crc,
	unsigned char const *bytes,
	unsigned long size)
{
	unsigned long index;

	crc = ~crc & 0xFFFFFFFFUL;
	for (index = 0; index < size; index++)
	{
		int bit;

		crc ^= bytes[index];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
	}
	return ~crc & 0xFFFFFFFFUL;
}

unsigned char *menu_models_writer_finish(
	struct menu_models_writer *writer,
	char const *map,
	unsigned long map_checksum,
	unsigned long *size)
{
	unsigned long instances, vertex_buffers, index_buffers, tags_offset, pixels_offset, file_size, empty_name;
	unsigned long *name_offsets;
	struct menu_models_tag_header *tag_header;
	struct menu_models_file_header header;
	unsigned char *file;
	long index;

	*size = 0;
	if (writer->failed || !writer->tag_count)
		return NULL;
	name_offsets = malloc(writer->tag_count * sizeof(*name_offsets));
	if (!name_offsets)
		return NULL;
	instances = menu_models_writer_append(writer, NULL, writer->tag_count * sizeof(struct menu_models_instance));
	vertex_buffers = menu_models_writer_append(writer, NULL, writer->part_count * MENU_MODELS_BUFFER_SIZE);
	index_buffers = menu_models_writer_append(writer, NULL, writer->part_count * MENU_MODELS_BUFFER_SIZE);
	for (index = 0; index < writer->tag_count; index++)
	{
		name_offsets[index] = menu_models_writer_append(writer, writer->written[index].name,
			(unsigned long)strlen(writer->written[index].name) + 1);
	}
	empty_name = menu_models_writer_append(writer, "", 1);
	if (writer->failed)
	{
		file_free(name_offsets);
		return NULL;
	}
	tag_header = (struct menu_models_tag_header *)writer->tags;
	tag_header->instances = (struct menu_models_instance *)(unsigned long)instances;
	tag_header->scenario_tag_index = NONE;
	tag_header->checksum = 0;
	tag_header->tag_count = writer->tag_count;
	tag_header->vertex_buffer_count = writer->part_count;
	tag_header->vertex_buffers = (unsigned char *)(unsigned long)vertex_buffers;
	tag_header->index_buffer_count = writer->part_count;
	tag_header->index_buffers = (unsigned char *)(unsigned long)index_buffers;
	tag_header->signature = MENU_MODELS_TAG_HEADER_SIGNATURE;
	for (index = 0; index < writer->tag_count; index++)
	{
		struct menu_models_instance *instance =
			(struct menu_models_instance *)(writer->tags + instances) + index;

		instance->group_tag = writer->written[index].group_tag;
		instance->parent_group_tags[0] = writer->written[index].parent_group_tags[0];
		instance->parent_group_tags[1] = writer->written[index].parent_group_tags[1];
		instance->tag_index = menu_models_tag_index(index);
		instance->name = (char *)(unsigned long)name_offsets[index];
		instance->base_address = (void *)(unsigned long)writer->written[index].root_offset;
	}
	/* each reference's name, its tag's (an empty one for none) */
	for (index = 0; index < writer->reference_count; index++)
	{
		struct tag_reference *reference = (struct tag_reference *)(writer->tags + writer->references[index].offset);
		long target = writer->references[index].absolute_index;

		if (target >= 0 && target < writer->tag_count)
		{
			reference->name = (char *)name_offsets[target];
			reference->name_length = (long)strlen(writer->written[target].name);
		}
		else
		{
			reference->name = (char *)empty_name;
			reference->name_length = 0;
		}
	}
	file_free(name_offsets);
	/* each part's buffers: the header's, whose data are the part's vertices
	and indices (a buffer's Data, its second long) */
	for (index = 0; index < writer->part_count; index++)
	{
		struct menu_models_part_buffers const *part = &writer->parts[index];
		unsigned char *vertex_buffer = writer->tags + vertex_buffers + index * MENU_MODELS_BUFFER_SIZE;
		unsigned char *index_buffer = writer->tags + index_buffers + index * MENU_MODELS_BUFFER_SIZE;

		*(void **)(vertex_buffer + 4) = (void *)(unsigned long)part->vertices_offset;
		*(void **)(index_buffer + 4) = (void *)(unsigned long)part->indices_offset;
		*(void **)(writer->tags + part->vertex_field_offset) =
			(void *)(vertex_buffers + index * MENU_MODELS_BUFFER_SIZE);
		*(void **)(writer->tags + part->index_field_offset) =
			(void *)(index_buffers + index * MENU_MODELS_BUFFER_SIZE);
	}

	tags_offset = MENU_MODELS_HEADER_SIZE;
	pixels_offset = (tags_offset + writer->tags_size + 15) & ~15UL;
	/* (and zeros after the pixels: a bitmap's last mipmaps, rounded up to
	whole blocks, are read past its pixels' size, which the validator asks
	to be in the file) */
	file_size = pixels_offset + writer->pixels_size + (writer->pixels_size ? MENU_MODELS_PIXELS_PADDING : 0);
	for (index = 0; index < writer->pixel_fixup_count; index++)
		*(long *)(writer->tags + writer->pixel_fixups[index]) += (long)pixels_offset;
	file = file_zeroed(file_size);
	if (!file)
		return NULL;
	memcpy(file + tags_offset, writer->tags, writer->tags_size);
	if (writer->pixels_size)
		memcpy(file + pixels_offset, writer->pixels, writer->pixels_size);
	memset(&header, 0, sizeof(header));
	header.pixels_size = (unsigned int)(file_size - pixels_offset);
	header.signature = MENU_MODELS_SIGNATURE;
	header.version = MENU_MODELS_VERSION;
	header.file_size = (unsigned int)file_size;
	header.map_checksum = (unsigned int)map_checksum;
	header.tags_offset = (unsigned int)tags_offset;
	header.tags_size = (unsigned int)writer->tags_size;
	header.pixels_offset = (unsigned int)pixels_offset;
	header.tag_count = (unsigned int)writer->tag_count;
	strncpy(header.map_name, map ? map : "", sizeof(header.map_name) - 1);
	header.body_crc = (unsigned int)menu_models_crc(0, file + MENU_MODELS_HEADER_SIZE,
		file_size - MENU_MODELS_HEADER_SIZE);
	memcpy(file, &header, sizeof(header));
	*size = file_size;
	return file;
}

/* ---------- the file read */

typedef char verify_menu_models_file_header_size[
	sizeof(struct menu_models_file_header) <= MENU_MODELS_HEADER_SIZE ? 1 : -1];

int menu_models_file_check(
	unsigned char const *file,
	unsigned long size,
	struct menu_models_file_header *header,
	char const **why)
{
	if (size < MENU_MODELS_HEADER_SIZE)
	{
		*why = "it is too short";
		return FALSE;
	}
	memcpy(header, file, sizeof(*header));
	if (header->signature != MENU_MODELS_SIGNATURE)
		*why = "it is not one";
	else if (header->version != MENU_MODELS_VERSION)
		*why = "it is of another version";
	else if (header->file_size != size || size > MENU_MODELS_MAXIMUM_FILE_SIZE)
		*why = "it is cut short or too long";
	else if (header->tags_offset != MENU_MODELS_HEADER_SIZE ||
		header->tags_size < sizeof(struct menu_models_tag_header) || header->tags_size > size - header->tags_offset ||
		header->pixels_offset < header->tags_offset + header->tags_size || header->pixels_offset > size ||
		header->pixels_size != size - header->pixels_offset)
		*why = "its parts are not where it says";
	else if (header->tag_count < 1 || header->tag_count > MENU_MODELS_MAXIMUM_TAGS)
		*why = "it has too many tags";
	else if (menu_models_crc(0, file + MENU_MODELS_HEADER_SIZE, size - MENU_MODELS_HEADER_SIZE) != header->body_crc)
		*why = "it is damaged";
	else
		return TRUE;
	return FALSE;
}

struct tag_schema_group const *menu_models_schema_group(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	return NULL;
}

struct relocation
{
	unsigned char *tags;
	unsigned long size;
	/* a bit for each byte made a pointer's (each walked once) */
	unsigned char *walked;
	int refused;
	char const *why;
	/* the header's buffers (a model part's must be one of them) */
	unsigned long buffers_begin, buffers_end;
};

static void relocation_refuse(
	struct relocation *relocation,
	char const *why)
{
	if (!relocation->refused)
		relocation->why = why;
	relocation->refused = TRUE;
}

/* the offset of count elements of size bytes made their pointer, if they lie
in the tags and none of their bytes was walked (marked so); NULL (and the
file refused) if not */
static void *relocation_claim(
	struct relocation *relocation,
	unsigned long offset,
	unsigned long count,
	unsigned long size)
{
	unsigned long bytes, at;

	if (!count || !size)
		return NULL;
	if (count > relocation->size / size || (bytes = count * size) > relocation->size ||
		offset > relocation->size - bytes)
	{
		relocation_refuse(relocation, "a pointer is outside the tags");
		return NULL;
	}
	for (at = offset; at < offset + bytes; at++)
	{
		if (relocation->walked[at >> 3] & (1 << (at & 7)))
		{
			relocation_refuse(relocation, "two pointers share bytes");
			return NULL;
		}
		relocation->walked[at >> 3] |= (unsigned char)(1 << (at & 7));
	}
	return relocation->tags + offset;
}

/* a pointer to a byte in the tags (one only read: a name, a buffer's data),
or NULL */
static void *relocation_pointer(
	struct relocation *relocation,
	unsigned long offset)
{
	return offset && offset < relocation->size ? relocation->tags + offset : NULL;
}

static void relocate_fields(
	struct relocation *relocation,
	unsigned char *base,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > MENU_MODELS_MAXIMUM_DEPTH)
	{
		relocation_refuse(relocation, "a tag is too deep");
		return;
	}
	for (field = definition->fields; field->type != _tag_schema_terminator && !relocation->refused; field++)
	{
		short index;

		for (index = 0; index < field->count && !relocation->refused; index++)
		{
			unsigned char *address = base + field->offset + index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				relocate_fields(relocation, address, field->definition, depth);
				break;

			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)address;
				struct tag_schema_definition const *element = field->definition;
				long element_index;

				if (block->count < 0)
				{
					relocation_refuse(relocation, "a block has fewer than no elements");
					break;
				}
				block->definition = NULL;
				block->address = relocation_claim(relocation, (unsigned long)block->address,
					(unsigned long)block->count, (unsigned long)element->size);
				if (!block->address)
				{
					block->count = 0;
					break;
				}
				for (element_index = 0; element_index < block->count && !relocation->refused; element_index++)
				{
					relocate_fields(relocation, (unsigned char *)block->address + element_index * element->size,
						element, depth + 1);
				}
				break;
			}

			case _tag_schema_data:
			{
				struct tag_data *data = (struct tag_data *)address;

				if (data->size < 0)
				{
					relocation_refuse(relocation, "data has fewer than no bytes");
					break;
				}
				data->definition = NULL;
				data->address = relocation_claim(relocation, (unsigned long)data->address, (unsigned long)data->size, 1);
				if (!data->address)
					data->size = 0;
				break;
			}

			case _tag_schema_reference:
			{
				/* (a name only read: the validator checks it is a string in
				the tags, and in no tag's bytes) */
				struct tag_reference *reference = (struct tag_reference *)address;

				reference->name = relocation_pointer(relocation, (unsigned long)reference->name);
				break;
			}

			default:
				/* (data in the file is at its offset in it, which the
				validator checks; the rest is no pointer) */
				break;
			}
		}
	}
	/* (a model part's buffers are the header's, which the validator checks
	they are: model_geometry_part_check. Here only made pointers) */
	if (!relocation->refused && !strcmp(definition->name, "model_geometry_part"))
	{
		struct tag_schema_field const *type_field;

		for (type_field = definition->fields; type_field->type != _tag_schema_terminator; type_field++)
		{
			unsigned long *buffer;

			/* (the hardware format follows each buffer's other fields: the
			schema names the buffers' types, triangle_buffer.type and
			vertex_buffer.type, at the buffers' starts) */
			if (strcmp(type_field->name, "triangle_buffer.type") && strcmp(type_field->name, "vertex_buffer.type"))
				continue;
			buffer = (unsigned long *)(base + type_field->offset +
				(!strcmp(type_field->name, "triangle_buffer.type") ?
					offsetof(struct triangle_buffer, hardware_format) :
					offsetof(struct vertex_buffer, hardware_format)));
			*(void **)buffer = *(unsigned long *)buffer >= relocation->buffers_begin &&
				*(unsigned long *)buffer < relocation->buffers_end ?
					relocation->tags + *(unsigned long *)buffer :
					NULL;
		}
	}
}

int menu_models_relocate(
	unsigned char *tags,
	unsigned long size,
	char const **why)
{
	struct relocation relocation;
	struct menu_models_tag_header *header = (struct menu_models_tag_header *)tags;
	struct menu_models_instance *instances;
	long index;

	memset(&relocation, 0, sizeof(relocation));
	relocation.tags = tags;
	relocation.size = size;
	relocation.walked = file_zeroed((size + 7) / 8);
	if (!relocation.walked)
	{
		*why = "out of memory";
		return FALSE;
	}
	if (size < sizeof(*header) || header->signature != MENU_MODELS_TAG_HEADER_SIGNATURE ||
		header->tag_count < 1 || header->tag_count > MENU_MODELS_MAXIMUM_TAGS ||
		header->vertex_buffer_count < 0 || header->index_buffer_count < 0 ||
		header->vertex_buffer_count != header->index_buffer_count)
	{
		relocation_refuse(&relocation, "its tag header is damaged");
	}
	relocation_claim(&relocation, 0, 1, sizeof(*header));
	instances = relocation_claim(&relocation, (unsigned long)header->instances, (unsigned long)header->tag_count,
		sizeof(*instances));
	header->instances = instances;
	header->vertex_buffers = relocation_claim(&relocation, (unsigned long)header->vertex_buffers,
		(unsigned long)header->vertex_buffer_count, MENU_MODELS_BUFFER_SIZE);
	header->index_buffers = relocation_claim(&relocation, (unsigned long)header->index_buffers,
		(unsigned long)header->index_buffer_count, MENU_MODELS_BUFFER_SIZE);
	if (header->vertex_buffer_count && !relocation.refused)
	{
		/* (the two arrays, one after the other, as the file writes them) */
		relocation.buffers_begin = (unsigned long)(header->vertex_buffers - tags);
		relocation.buffers_end = (unsigned long)(header->index_buffers - tags) +
			(unsigned long)header->index_buffer_count * MENU_MODELS_BUFFER_SIZE;
		for (index = 0; index < header->vertex_buffer_count * 2; index++)
		{
			unsigned char *buffer = index < header->vertex_buffer_count ?
				header->vertex_buffers + index * MENU_MODELS_BUFFER_SIZE :
				header->index_buffers + (index - header->vertex_buffer_count) * MENU_MODELS_BUFFER_SIZE;

			*(void **)(buffer + 4) = relocation_pointer(&relocation, *(unsigned long *)(buffer + 4));
		}
	}
	for (index = 0; !relocation.refused && index < header->tag_count; index++)
	{
		struct menu_models_instance *instance = &instances[index];
		struct tag_schema_group const *group = menu_models_schema_group(instance->group_tag);

		instance->name = relocation_pointer(&relocation, (unsigned long)instance->name);
		if (!group || !group->definition)
		{
			relocation_refuse(&relocation, "a tag is of a group it cannot have");
			break;
		}
		instance->base_address = relocation_claim(&relocation, (unsigned long)instance->base_address, 1,
			(unsigned long)group->definition->size);
		if (!instance->base_address)
		{
			relocation_refuse(&relocation, "a tag has no data");
			break;
		}
		relocate_fields(&relocation, instance->base_address, group->definition, 0);
	}
	file_free(relocation.walked);
	*why = relocation.why;
	return !relocation.refused;
}

static void remap_fields(
	unsigned char *base,
	struct tag_schema_definition const *definition,
	long tag_count,
	long const *tag_indices,
	char *const *names)
{
	struct tag_schema_field const *field;

	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short index;

		for (index = 0; index < field->count; index++)
		{
			unsigned char *address = base + field->offset + index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				remap_fields(address, field->definition, tag_count, tag_indices, names);
				break;

			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)address;
				struct tag_schema_definition const *element = field->definition;
				long element_index;

				for (element_index = 0; element_index < block->count; element_index++)
				{
					remap_fields((unsigned char *)block->address + element_index * element->size, element, tag_count,
						tag_indices, names);
				}
				break;
			}

			case _tag_schema_reference:
			{
				struct tag_reference *reference = (struct tag_reference *)address;
				long absolute_index = reference->index == NONE ? NONE : (long)(short)reference->index;

				if (absolute_index < 0 || absolute_index >= tag_count)
				{
					reference->index = NONE;
					reference->name = "";
					reference->name_length = 0;
					break;
				}
				reference->index = tag_indices[absolute_index];
				reference->name = names[absolute_index];
				reference->name_length = (long)strlen(names[absolute_index]);
				break;
			}

			case _tag_schema_tag_index:
			{
				long *tag_index = (long *)address;
				long absolute_index = *tag_index == NONE ? NONE : (long)(short)*tag_index;

				*tag_index = absolute_index < 0 || absolute_index >= tag_count ? NONE : tag_indices[absolute_index];
				break;
			}

			default:
				break;
			}
		}
	}
}

void menu_models_remap(
	unsigned char *tags,
	long const *tag_indices,
	char *const *names)
{
	struct menu_models_tag_header *header = (struct menu_models_tag_header *)tags;
	long index;

	for (index = 0; index < header->tag_count; index++)
	{
		struct menu_models_instance *instance = &header->instances[index];
		struct tag_schema_group const *group = menu_models_schema_group(instance->group_tag);

		if (group && group->definition && instance->base_address)
			remap_fields(instance->base_address, group->definition, header->tag_count, tag_indices, names);
	}
}
