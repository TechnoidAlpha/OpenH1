/*
XGPU_MANIFEST.C

The renderer's manifest (xgpu_manifest.h): its text, read and written, and
the front end's recorder.
*/

#include "xgpu_manifest.h"
#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_hdr.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XGPU_MANIFEST_READER_ONLY
#include "xgpu_stream_describe.h"
#include "port_config.h"
#endif

/* ---------- the fields */

uint64_t xgpu_manifest_hash(const void *data, size_t size, uint64_t hash)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 1099511628211ULL;
	return hash;
}

/* a field of a key: its name, place, the size of each value (1, 2 or 4
bytes), how many, and whether it is written in hex */
struct field
{
	const char *name;
	size_t offset;
	unsigned char size, count, hex;
};

#define KEY_FIELD(name, member, size, count, hex) \
	{ name, offsetof(struct xgpu_stream_pipeline_key, member), size, count, hex }
#define PIXEL_FIELD(name, member, size, count, hex) \
	{ name, offsetof(struct xgpu_stream_pixel_key, member), size, count, hex }

static const struct field key_fields[] = {
	KEY_FIELD("pa", packed_attributes, 4, 1, 1),
	KEY_FIELD("im", immediate, 1, 1, 0),
	KEY_FIELD("li", lighting, 1, 1, 0),
	KEY_FIELD("vw", views, 1, 1, 0),
	KEY_FIELD("bl", blend, 1, 4, 0),
	KEY_FIELD("cm", color_mask, 1, 1, 0),
	KEY_FIELD("dt", depth_test, 1, 3, 0),
	KEY_FIELD("st", stencil_test, 1, 5, 0),
	KEY_FIELD("sm", stencil_read_mask, 4, 2, 0),
	KEY_FIELD("cu", cull, 1, 3, 0),
	KEY_FIELD("fi", fill, 1, 1, 0),
	KEY_FIELD("db", depth_bias, 1, 1, 0),
	KEY_FIELD("fm", color_format, 1, 3, 0),
};

static const struct field pixel_fields[] = {
	PIXEL_FIELD("tm", texture_modes, 4, 1, 1),
	PIXEL_FIELD("sa", sampler_type, 1, 4, 0),
	PIXEL_FIELD("ak", alpha_kill, 1, 4, 0),
	PIXEL_FIELD("sg", color_sign, 1, 4, 0),
	PIXEL_FIELD("at", alpha_test_function, 4, 1, 0),
	PIXEL_FIELD("fe", fog_enable, 1, 1, 0),
	PIXEL_FIELD("ft", fog_table_mode, 1, 1, 0),
	PIXEL_FIELD("cn", count_samples, 1, 1, 0),
	PIXEL_FIELD("ca", coverage_alpha, 1, 1, 0),
	PIXEL_FIELD("pt", point_threshold, 1, 1, 0),
	PIXEL_FIELD("pl", per_pixel_lighting, 1, 1, 0),
	PIXEL_FIELD("as", alpha_test_samples, 1, 1, 0),
	PIXEL_FIELD("mv", multiview, 1, 1, 0),
	PIXEL_FIELD("sb", source_blend, 1, 1, 0),
	PIXEL_FIELD("dl", dynamic_lights, 1, 1, 0),
	PIXEL_FIELD("ve", vr_effect, 1, 1, 0),
	PIXEL_FIELD("ao", ambient_occlusion, 1, 1, 0),
};

#define CONTEXT_FIELD(name, member, size, count) \
	{ name, offsetof(struct xgpu_manifest_context, member), size, count, 0 }

/* a d record's context (xgpu_manifest_context): cl the vertex program's
lighting (lights, world's transform found), ct the targets (multisampled,
screen colour), cd the dynamic lights
(present, the lightmap pass), ch the HDR tag's (category, 3D view, kind),
cw the world's occlusion's light (a recording without it: none), cf its
window found once a pixel by the pre-pass (none) */
static const struct field context_fields[] = {
	CONTEXT_FIELD("cl", lights, 1, 2),
	CONTEXT_FIELD("ct", multisampled, 1, 2),
	CONTEXT_FIELD("cd", lights_present, 1, 2),
	CONTEXT_FIELD("ch", hdr_category, 1, 2),
	CONTEXT_FIELD("ck", hdr_kind, 2, 1),
	CONTEXT_FIELD("cw", world, 1, 1),
	CONTEXT_FIELD("cf", found, 1, 1),
};

#define FIELD_COUNT(fields) (sizeof(fields) / sizeof((fields)[0]))

#ifndef XGPU_MANIFEST_READER_ONLY
static uint32_t field_value(const unsigned char *base, const struct field *field, int index)
{
	const unsigned char *at = base + field->offset + (size_t)index * field->size;
	uint32_t value;
	uint16_t half;

	if (field->size == 1)
		return *at;
	if (field->size == 2)
	{
		memcpy(&half, at, sizeof(half));
		return half;
	}
	memcpy(&value, at, sizeof(value));
	return value;
}
#endif

static void field_set(unsigned char *base, const struct field *field, int index, uint32_t value)
{
	unsigned char *at = base + field->offset + (size_t)index * field->size;
	uint16_t half = (uint16_t)value;

	if (field->size == 1)
		*at = (unsigned char)value;
	else if (field->size == 2)
		memcpy(at, &half, sizeof(half));
	else
		memcpy(at, &value, sizeof(value));
}

#ifndef XGPU_MANIFEST_READER_ONLY
static uint64_t draw_vertex_program(const struct xgpu_manifest_draw *draw)
{
	return draw->key.vertex_program[0] | ((uint64_t)draw->key.vertex_program[1] << 32);
}
#endif

static uint64_t pixel_hash(const struct xgpu_stream_pixel_key *pixel)
{
	return xgpu_manifest_hash(pixel, sizeof(*pixel), XGPU_MANIFEST_HASH_START);
}

#ifndef XGPU_MANIFEST_READER_ONLY

/* a growing line */
struct text
{
	char *buffer;
	size_t length, capacity;
};

static void text_add(struct text *text, const char *format, ...)
{
	va_list arguments;
	int written;

	for (;;)
	{
		size_t room = text->capacity - text->length;

		va_start(arguments, format);
		written = text->buffer ? vsnprintf(text->buffer + text->length, room, format, arguments) : -1;
		va_end(arguments);
		if (written >= 0 && (size_t)written < room)
		{
			text->length += (size_t)written;
			return;
		}
		{
			size_t capacity = text->capacity ? text->capacity * 2 : 1024;
			char *buffer;

			while (written >= 0 && capacity < text->length + (size_t)written + 1)
				capacity *= 2;
			buffer = realloc(text->buffer, capacity);
			if (!buffer)
				return;
			text->buffer = buffer;
			text->capacity = capacity;
		}
	}
}

static void fields_write(struct text *text, const void *structure, const struct field *fields, size_t count)
{
	size_t index;
	int value;

	for (index = 0; index < count; index++)
	{
		text_add(text, " %s=", fields[index].name);
		for (value = 0; value < fields[index].count; value++)
		{
			uint32_t number = field_value(structure, &fields[index], value);

			text_add(text, fields[index].hex ? "%s0x%lx" : "%s%lu", value ? "," : "", (unsigned long)number);
		}
	}
}

static void hash_write(struct text *text, uint64_t hash)
{
	text_add(text, "%08lx%08lx", (unsigned long)(uint32_t)(hash >> 32), (unsigned long)(uint32_t)hash);
}

static void pixel_write(struct text *text, const struct xgpu_stream_pixel_key *pixel)
{
	int index, written = 0;

	text_add(text, "p ");
	hash_write(text, pixel_hash(pixel));
	text_add(text, " cs=");
	for (index = 0; index < XGPU_STREAM_COMBINER_STATES; index++)
	{
		if (pixel->combiner_state[index])
		{
			text_add(text, "%s%d:0x%lx", written ? "," : "", index, (unsigned long)pixel->combiner_state[index]);
			written = 1;
		}
	}
	fields_write(text, pixel, pixel_fields, FIELD_COUNT(pixel_fields));
	text_add(text, "\n");
}

static void draw_write(struct text *text, const struct xgpu_manifest_draw *draw,
	const struct xgpu_manifest_context *context)
{
	uint32_t index;
	int stream, first = 1;

	text_add(text, "d vp=");
	hash_write(text, draw_vertex_program(draw));
	fields_write(text, &draw->key, key_fields, FIELD_COUNT(key_fields));
	text_add(text, " px=");
	hash_write(text, pixel_hash(&draw->key.pixel));
	if (draw->key.immediate)
	{
		text_add(text, " i=0x%lx", (unsigned long)draw->immediate_written);
	}
	else
	{
		text_add(text, " l=");
		for (index = 0; index < draw->element_count; index++)
		{
			const struct xgpu_manifest_element *element = &draw->elements[index];

			text_add(text, "%s%u.%u.%u.%u.%u", index ? "/" : "", element->reg, element->stream, element->format,
				element->bytes, element->offset);
		}
		text_add(text, " w=");
		for (stream = 0; stream < XGPU_MANIFEST_STREAMS; stream++)
		{
			if (draw->strides[stream])
			{
				text_add(text, "%s%d:%u", first ? "" : ",", stream, draw->strides[stream]);
				first = 0;
			}
		}
	}
	text_add(text, " t=");
	for (stream = 0; stream < 4; stream++)
	{
		const struct xgpu_manifest_stage *stage = &draw->stages[stream];

		text_add(text, "%s%u.%u.%u.%u.%u.0x%lx", stream ? "/" : "", stage->source, stage->sampler_type,
			stage->address[0], stage->address[1], stage->address[2], (unsigned long)stage->border_color);
	}
	if (context)
		fields_write(text, context, context_fields, FIELD_COUNT(context_fields));
	text_add(text, "\n");
}

static void vertex_program_write(struct text *text, const struct xgpu_manifest_vertex_program *program)
{
	uint32_t index;

	text_add(text, "v ");
	hash_write(text, program->hash);
	text_add(text, " %lu ", (unsigned long)program->instruction_count);
	for (index = 0; index < program->instruction_count * 4; index++)
		text_add(text, "%08lx", (unsigned long)program->words[index]);
	text_add(text, "\n");
}

#endif

/* ---------- reading */

/* the pixel keys read, by their hash, until the draws are matched */
struct pixel_entry
{
	uint64_t hash;
	struct xgpu_stream_pixel_key key;
};

struct reader
{
	struct xgpu_manifest *manifest;
	struct pixel_entry *pixels;
	uint32_t pixel_count, pixel_capacity, draw_capacity, context_capacity, program_capacity, shader_capacity;
	/* the draws' pixel keys' hashes, matched at the end */
	uint64_t *draw_pixels;
	char *error;
	size_t error_size;
	unsigned long line;
};

static int fail(struct reader *reader, const char *what)
{
	if (reader->error && reader->error_size)
		snprintf(reader->error, reader->error_size, "line %lu: %s", reader->line, what);
	return 0;
}

static int hex_digit(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* digits hex digits at text into value; the text after, or NULL */
static const char *hex_read(const char *text, int digits, uint64_t *value)
{
	int index;

	*value = 0;
	for (index = 0; index < digits; index++)
	{
		int digit = hex_digit((unsigned char)text[index]);

		if (digit < 0)
			return NULL;
		*value = (*value << 4) | (uint64_t)digit;
	}
	return text + digits;
}

/* a number (decimal, or hex after 0x) at text, which ends at one of ends
or the line's end; the text after it, or NULL */
static const char *number_read(const char *text, uint32_t *value)
{
	char *end;
	unsigned long number;

	if (!*text || *text == ' ' || *text == '\n')
		return NULL;
	number = strtoul(text, &end, 0);
	if (end == text || number > 0xffffffffUL)
		return NULL;
	*value = (uint32_t)number;
	return end;
}

/* name=value,value... into the fields; the text after, or NULL */
static const char *fields_read(const char *text, void *structure, const struct field *fields, size_t count)
{
	size_t index, length;
	int value;
	const char *equals = strchr(text, '=');

	if (!equals)
		return NULL;
	length = (size_t)(equals - text);
	for (index = 0; index < count; index++)
	{
		if (strlen(fields[index].name) == length && !memcmp(fields[index].name, text, length))
			break;
	}
	if (index == count)
		return NULL;
	text = equals + 1;
	for (value = 0; value < fields[index].count; value++)
	{
		uint32_t number;

		if (value && *text++ != ',')
			return NULL;
		if (!(text = number_read(text, &number)))
			return NULL;
		if ((fields[index].size == 1 && number > 0xff) || (fields[index].size == 2 && number > 0xffff))
			return NULL;
		field_set(structure, &fields[index], value, number);
	}
	return text;
}

static int grow(void **array, uint32_t *capacity, uint32_t count, size_t size)
{
	void *larger;
	uint32_t wanted;

	if (count < *capacity)
		return 1;
	wanted = *capacity ? *capacity * 2 : 256;
	larger = realloc(*array, (size_t)wanted * size);
	if (!larger)
		return 0;
	*array = larger;
	*capacity = wanted;
	return 1;
}

static int vertex_program_read(struct reader *reader, const char *text)
{
	struct xgpu_manifest *manifest = reader->manifest;
	struct xgpu_manifest_vertex_program *program;
	uint64_t value;
	uint32_t count, index;

	if (!grow((void **)&manifest->vertex_programs, &reader->program_capacity, manifest->vertex_program_count,
		sizeof(*manifest->vertex_programs)))
	{
		return fail(reader, "out of memory");
	}
	program = &manifest->vertex_programs[manifest->vertex_program_count];
	memset(program, 0, sizeof(*program));
	if (!(text = hex_read(text, 16, &program->hash)) || *text++ != ' ' || !(text = number_read(text, &count)) ||
		*text++ != ' ' || count > XGPU_MANIFEST_MICROCODE_WORDS / 4)
	{
		return fail(reader, "a vertex program's hash and size");
	}
	program->instruction_count = count;
	for (index = 0; index < count * 4; index++)
	{
		if (!(text = hex_read(text, 8, &value)))
			return fail(reader, "a vertex program's microcode");
		program->words[index] = (uint32_t)value;
	}
	manifest->vertex_program_count++;
	return 1;
}

static int pixel_read(struct reader *reader, const char *text)
{
	struct pixel_entry *entry;

	if (!grow((void **)&reader->pixels, &reader->pixel_capacity, reader->pixel_count, sizeof(*reader->pixels)))
		return fail(reader, "out of memory");
	entry = &reader->pixels[reader->pixel_count];
	memset(entry, 0, sizeof(*entry));
	if (!(text = hex_read(text, 16, &entry->hash)) || strncmp(text, " cs=", 4))
		return fail(reader, "a pixel key's hash");
	text += 4;
	while (*text && *text != ' ' && *text != '\n')
	{
		uint32_t index, value;

		if (!(text = number_read(text, &index)) || *text++ != ':' || index >= XGPU_STREAM_COMBINER_STATES ||
			!(text = number_read(text, &value)))
		{
			return fail(reader, "a pixel key's combiner states");
		}
		entry->key.combiner_state[index] = value;
		if (*text == ',')
			text++;
	}
	while (*text == ' ')
	{
		if (!(text = fields_read(text + 1, &entry->key, pixel_fields, FIELD_COUNT(pixel_fields))))
			return fail(reader, "a pixel key's field");
	}
	if (pixel_hash(&entry->key) != entry->hash)
		return fail(reader, "a pixel key that is not its hash's");
	reader->pixel_count++;
	return 1;
}

/* a draw, or (shader) a vertex shader: a draw's vertex program and layout
alone */
static int draw_read(struct reader *reader, const char *text, int shader)
{
	struct xgpu_manifest *manifest = reader->manifest;
	struct xgpu_manifest_draw *draw;
	struct xgpu_manifest_context *context = NULL;
	const char *after;
	uint64_t value, unused;
	uint32_t number;

	if (shader)
	{
		if (!grow((void **)&manifest->vertex_shaders, &reader->shader_capacity, manifest->vertex_shader_count,
			sizeof(*manifest->vertex_shaders)))
		{
			return fail(reader, "out of memory");
		}
		draw = &manifest->vertex_shaders[manifest->vertex_shader_count];
	}
	else
	{
		if (!grow((void **)&manifest->draws, &reader->draw_capacity, manifest->draw_count, sizeof(*manifest->draws)) ||
			!grow((void **)&manifest->contexts, &reader->context_capacity, manifest->draw_count,
			sizeof(*manifest->contexts)) ||
			!(reader->draw_pixels = realloc(reader->draw_pixels, (size_t)reader->draw_capacity * sizeof(uint64_t))))
		{
			return fail(reader, "out of memory");
		}
		draw = &manifest->draws[manifest->draw_count];
		context = &manifest->contexts[manifest->draw_count];
		memset(context, 0, sizeof(*context));
	}
	memset(draw, 0, sizeof(*draw));
	if (strncmp(text, "vp=", 3) || !(text = hex_read(text + 3, 16, &value)))
		return fail(reader, "a draw's vertex program");
	draw->key.vertex_program[0] = (uint32_t)value;
	draw->key.vertex_program[1] = (uint32_t)(value >> 32);
	while (*text == ' ')
	{
		text++;
		if (!strncmp(text, "px=", 3))
		{
			if (shader || !(text = hex_read(text + 3, 16, shader ? &unused : &reader->draw_pixels[manifest->draw_count])))
				return fail(reader, "a draw's pixel key");
		}
		else if (!strncmp(text, "i=", 2))
		{
			if (!(text = number_read(text + 2, &draw->immediate_written)))
				return fail(reader, "a draw's immediate registers");
		}
		else if (!strncmp(text, "l=", 2))
		{
			text += 2;
			while (*text && *text != ' ' && *text != '\n')
			{
				struct xgpu_manifest_element *element;
				uint32_t parts[5];
				int part;

				if (draw->element_count >= XGPU_MANIFEST_ATTRIBUTES)
					return fail(reader, "a draw's layout");
				for (part = 0; part < 5; part++)
				{
					if ((part && *text++ != '.') || !(text = number_read(text, &parts[part])))
						return fail(reader, "a draw's layout");
				}
				if (parts[0] >= XGPU_MANIFEST_ATTRIBUTES || parts[1] >= XGPU_MANIFEST_STREAMS || parts[2] > 0xff ||
					parts[3] > 0xff || parts[4] > 0xffff)
				{
					return fail(reader, "a draw's layout");
				}
				element = &draw->elements[draw->element_count++];
				element->reg = (uint8_t)parts[0];
				element->stream = (uint8_t)parts[1];
				element->format = (uint8_t)parts[2];
				element->bytes = (uint8_t)parts[3];
				element->offset = (uint16_t)parts[4];
				if (*text == '/')
					text++;
			}
		}
		else if (!strncmp(text, "w=", 2))
		{
			text += 2;
			while (*text && *text != ' ' && *text != '\n')
			{
				uint32_t stride;

				if (!(text = number_read(text, &number)) || *text++ != ':' || !(text = number_read(text, &stride)) ||
					number >= XGPU_MANIFEST_STREAMS || stride > 0xffff)
				{
					return fail(reader, "a draw's strides");
				}
				draw->strides[number] = (uint16_t)stride;
				if (*text == ',')
					text++;
			}
		}
		else if (!strncmp(text, "t=", 2))
		{
			int stage, part;

			text += 2;
			for (stage = 0; stage < 4; stage++)
			{
				uint32_t parts[6];

				for (part = 0; part < 6; part++)
				{
					if ((part && *text++ != '.') || !(text = number_read(text, &parts[part])) || (part < 5 && parts[part] > 0xff))
						return fail(reader, "a draw's texture stages");
				}
				draw->stages[stage].source = (uint8_t)parts[0];
				draw->stages[stage].sampler_type = (uint8_t)parts[1];
				draw->stages[stage].address[0] = (uint8_t)parts[2];
				draw->stages[stage].address[1] = (uint8_t)parts[3];
				draw->stages[stage].address[2] = (uint8_t)parts[4];
				draw->stages[stage].border_color = parts[5];
				if (stage < 3 && *text++ != '/')
					return fail(reader, "a draw's texture stages");
			}
		}
		else if (context && (after = fields_read(text, context, context_fields, FIELD_COUNT(context_fields))) != NULL)
		{
			text = after;
		}
		else if (!(text = fields_read(text, &draw->key, key_fields, FIELD_COUNT(key_fields))))
		{
			return fail(reader, "a draw's field");
		}
	}
	if (*text && *text != '\n')
		return fail(reader, "a draw's end");
	if (shader)
		manifest->vertex_shader_count++;
	else
		manifest->draw_count++;
	return 1;
}

static int pixel_compare(const void *a, const void *b)
{
	uint64_t left = ((const struct pixel_entry *)a)->hash, right = ((const struct pixel_entry *)b)->hash;

	return left < right ? -1 : left > right;
}

int xgpu_manifest_read(const char *path, struct xgpu_manifest *manifest, char *error, size_t error_size)
{
	struct reader reader;
	FILE *file = fopen(path, "r");
	char *line = NULL;
	size_t capacity = 0;
	int ok = 1, versioned = 0;
	uint32_t index;

	memset(manifest, 0, sizeof(*manifest));
	memset(&reader, 0, sizeof(reader));
	reader.manifest = manifest;
	reader.error = error;
	reader.error_size = error_size;
	if (!file)
		return fail(&reader, "cannot open the file");
	for (;;)
	{
		size_t length = 0;
		int c;

		/* (a line of any length) */
		while ((c = fgetc(file)) != EOF && c != '\n')
		{
			if (length + 2 > capacity)
			{
				char *larger = realloc(line, capacity = capacity ? capacity * 2 : 4096);

				if (!larger)
				{
					ok = fail(&reader, "out of memory");
					break;
				}
				line = larger;
			}
			line[length++] = (char)c;
		}
		if (!ok || (c == EOF && !length))
			break;
		if (!line && !(line = malloc(capacity = 4096)))
		{
			ok = fail(&reader, "out of memory");
			break;
		}
		line[length] = 0;
		reader.line++;
		if (!length || line[0] == '#')
			continue;
		if (!strncmp(line, "manifest ", 9))
		{
			if (atoi(line + 9) != XGPU_MANIFEST_VERSION)
			{
				ok = fail(&reader, "another version of the manifest");
				break;
			}
			versioned = 1;
		}
		else if (!versioned)
			ok = fail(&reader, "no version first");
		else if (!strncmp(line, "v ", 2))
			ok = vertex_program_read(&reader, line + 2);
		else if (!strncmp(line, "p ", 2))
			ok = pixel_read(&reader, line + 2);
		else if (!strncmp(line, "d ", 2))
			ok = draw_read(&reader, line + 2, 0);
		else if (!strncmp(line, "s ", 2))
			ok = draw_read(&reader, line + 2, 1);
		else
			ok = fail(&reader, "an unknown record");
		if (!ok || c == EOF)
			break;
	}
	fclose(file);
	free(line);
	/* each draw's pixel key */
	if (ok)
	{
		qsort(reader.pixels, reader.pixel_count, sizeof(*reader.pixels), pixel_compare);
		for (index = 0; index < manifest->draw_count && ok; index++)
		{
			struct pixel_entry probe, *found;

			probe.hash = reader.draw_pixels[index];
			found = bsearch(&probe, reader.pixels, reader.pixel_count, sizeof(*reader.pixels), pixel_compare);
			if (!found)
			{
				reader.line = 0;
				ok = fail(&reader, "a draw's pixel key is not in the manifest");
				break;
			}
			manifest->draws[index].key.pixel = found->key;
		}
	}
	free(reader.pixels);
	free(reader.draw_pixels);
	if (!ok)
		xgpu_manifest_free(manifest);
	return ok;
}

void xgpu_manifest_free(struct xgpu_manifest *manifest)
{
	free(manifest->vertex_programs);
	free(manifest->draws);
	free(manifest->contexts);
	free(manifest->vertex_shaders);
	memset(manifest, 0, sizeof(*manifest));
}

const struct xgpu_manifest_vertex_program *xgpu_manifest_vertex_program(const struct xgpu_manifest *manifest,
	uint64_t hash)
{
	uint32_t index;

	for (index = 0; index < manifest->vertex_program_count; index++)
	{
		if (manifest->vertex_programs[index].hash == hash)
			return &manifest->vertex_programs[index];
	}
	return NULL;
}

/* ---------- a draw's pipeline under the settings */

int xgpu_draw_derive(const struct xgpu_draw_facts *facts, int unlit, struct xgpu_draw_derived *derived)
{
	memset(derived, 0, sizeof(*derived));
	derived->samples = (uint8_t)(facts->samples > 1 ? facts->samples : 1);
	derived->color_format = (uint8_t)(!facts->color ? XGPU_FORMAT_NONE : facts->hdr_target ? XGPU_FORMAT_RGBA16F :
		XGPU_FORMAT_BGRA8);
	/* (an alpha test by sample, where the backend has a sample mask, of an
	opaque draw into multisampled targets) */
	if (facts->sample_mask && facts->samples > 1 && facts->alpha_test && !facts->blend)
		derived->alpha_test_samples = (uint8_t)facts->samples;
	derived->hdr = facts->hdr_target ? facts->hdr : 0;
	derived->lighting = XGPU_LIGHTING_NONE;
	if (!unlit && facts->lights && !facts->lighting_failed && facts->per_pixel)
	{
		/* a model lighting program's draw lit for each pixel: the enhanced
		dynamic lights in its two point lights' place, its ambient light by
		the occlusion (the world's where its lighting has its world position:
		the point lights' or the world's transform) */
		derived->per_pixel_lighting = (uint8_t)facts->lights;
		if (facts->lights_active && facts->world_found)
			derived->dynamic_lights = XGPU_DYNAMIC_LIGHTS_MODEL;
		if (facts->world && (facts->lights == 2 || facts->world_found))
			derived->ambient_occlusion = (uint8_t)(facts->world | XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL);
		derived->lighting = facts->lights == 2 ? XGPU_LIGHTING_MODEL_POINT : XGPU_LIGHTING_MODEL;
		return 1;
	}
	if (!unlit && facts->lights && !facts->lighting_failed && !facts->split_failed && facts->world &&
		(facts->lights == 2 || facts->world_found))
	{
		/* the world's occlusion over a model lit for each vertex, where its
		lighting has its world position: its ambient light handed on apart,
		by the occlusion at each pixel (the program's own sum clamps each
		vertex's light: xgpu.h) */
		derived->ambient_occlusion = (uint8_t)(facts->world | XGPU_AMBIENT_OCCLUSION_LIGHT_VERTEX);
		derived->lighting = XGPU_LIGHTING_MODEL_SPLIT;
		return 1;
	}
	if (!unlit && facts->environment && facts->lights_active && !facts->immediate && !facts->lighting_failed &&
		(facts->packed_attributes & 0xe) == 0xe)
	{
		/* a structure's lightmap pass lit by the dynamic lights (its normal,
		binormal and tangent packed: rasterizer_xbox_vertex_shaders_initialize.c's
		declaration), their light past white with display.hdr, the
		lightmap's light by the occlusion, theirs added after */
		derived->dynamic_lights = XGPU_DYNAMIC_LIGHTS_ENVIRONMENT;
		if (facts->hdr_target)
			derived->hdr |= XGPU_HDR_LIGHTS;
		if (facts->world && !facts->blend)
			derived->ambient_occlusion = (uint8_t)(facts->world | XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP);
		derived->lighting = XGPU_LIGHTING_ENVIRONMENT;
		return 1;
	}
	if (!unlit && facts->world && facts->environment && !facts->immediate && !facts->lighting_failed &&
		!facts->blend && (facts->packed_attributes & 0xe) == 0xe)
	{
		/* the world's occlusion over a structure's lightmap pass without the
		enhanced dynamic lights: lit as its program lights it, its vertex
		shader handing on the world position and normal alone for its light
		by the proxies' occlusion */
		derived->ambient_occlusion = (uint8_t)(facts->world | XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP);
		derived->lighting = XGPU_LIGHTING_ENVIRONMENT;
		return 1;
	}
	return 0;
}

/* multisampling's samples a pixel (d3d8_device.c's anti_aliasing_values':
tools/test_render_manifest.py checks that it has no others), 1 first */
static const int settings_samples[] = { 1, 2, 4, 8 };

#define SETTINGS_SAMPLES ((int)(sizeof(settings_samples) / sizeof(settings_samples[0])))

int xgpu_manifest_settings(int index, struct xgpu_manifest_settings *settings)
{
	if (index < 0 || index >= SETTINGS_SAMPLES * 8 * XGPU_MANIFEST_OCCLUSIONS)
		return 0;
	memset(settings, 0, sizeof(*settings));
	settings->samples = settings_samples[index % SETTINGS_SAMPLES];
	index /= SETTINGS_SAMPLES;
	settings->hdr = index & 1;
	settings->per_pixel_lighting = (index >> 1) & 1;
	settings->enhanced_lights = (index >> 2) & 1;
	settings->ambient_occlusion = index >> 3;
	return 1;
}

int xgpu_manifest_derive(const struct xgpu_manifest_draw *draw, const struct xgpu_manifest_context *context,
	const struct xgpu_manifest_settings *settings, int unlit, struct xgpu_manifest_draw *derived)
{
	const struct xgpu_stream_pipeline_key *key = &draw->key;
	struct xgpu_draw_facts facts;
	struct xgpu_draw_derived parts;
	int lit;

	memset(&facts, 0, sizeof(facts));
	facts.blend = key->blend;
	facts.color = key->color_format != XGPU_FORMAT_NONE;
	facts.immediate = key->immediate;
	facts.packed_attributes = key->packed_attributes;
	facts.alpha_test = key->pixel.alpha_test_function;
	facts.lights = context->lights;
	facts.world_found = context->world_found;
	/* (d3d8_device.c's bind_targets: multisampled where a screen buffer
	is; a screen buffer's colour in half floats with display.hdr) */
	facts.samples = context->multisampled ? settings->samples : 1;
	facts.hdr_target = settings->hdr && context->screen_colour && facts.color;
	if (facts.hdr_target)
	{
		/* (d3d8_device.c's hdr_tag) */
		struct xgpu_hdr_draw tagged;

		memset(&tagged, 0, sizeof(tagged));
		tagged.blend = key->blend;
		tagged.blend_source = key->blend_source;
		tagged.blend_destination = key->blend_destination;
		tagged.blend_operation = key->blend_operation;
		tagged.color_mask = key->color_mask;
		tagged.category = (signed char)context->hdr_category;
		tagged.interface = !context->hdr_view;
		tagged.kind = context->hdr_kind;
		facts.hdr = xgpu_hdr_classify(&tagged);
	}
	/* ("world"'s light where its window is given: d3d8_device.c's
	world_ao_key) */
	facts.world = settings->ambient_occlusion == XGPU_MANIFEST_OCCLUSION_WORLD && context->world ?
		XGPU_AMBIENT_OCCLUSION_WORLD : 0;
	/* (d3d8_device.c's per_pixel_lighting, dynamic_lights_active: the
	enhanced lights light each pixel whatever display.per_pixel_lighting
	says) */
	facts.per_pixel = settings->per_pixel_lighting || settings->enhanced_lights;
	facts.lights_active = settings->enhanced_lights && context->lights_present;
	facts.environment = context->environment;
	facts.sample_mask = 1;
	lit = xgpu_draw_derive(&facts, unlit, &parts);
	/* (its bytes, padding too: a draw's hash is of them) */
	memcpy(derived, draw, sizeof(*derived));
	derived->key.color_format = parts.color_format;
	derived->key.samples = parts.samples;
	derived->key.lighting = parts.lighting;
	derived->key.pixel.alpha_test_samples = parts.alpha_test_samples;
	derived->key.pixel.hdr = parts.hdr;
	derived->key.pixel.per_pixel_lighting = parts.per_pixel_lighting;
	derived->key.pixel.dynamic_lights = parts.dynamic_lights;
	derived->key.pixel.ambient_occlusion = parts.ambient_occlusion;
	return lit;
}

int xgpu_manifest_world_ao_forms(const struct xgpu_manifest_draw *derived, const struct xgpu_manifest_context *context,
	const struct xgpu_manifest_settings *settings, struct xgpu_manifest_draw forms[XGPU_MANIFEST_WORLD_AO_FORMS])
{
	struct xgpu_stream_pipeline_define twin;
	uint8_t occlusion = derived->key.pixel.ambient_occlusion;
	int count = 0;

	forms[count++] = *derived;
	/* (the pre-pass draws only a found window, with "world" on:
	d3d8_device.c's world_ao_twins) */
	if (!context->found || settings->ambient_occlusion != XGPU_MANIFEST_OCCLUSION_WORLD)
		return count;
	memset(&twin, 0, sizeof(twin));
	twin.key = derived->key;
	if ((occlusion & 0x30u) == XGPU_AMBIENT_OCCLUSION_WORLD)
	{
		forms[count] = *derived;
		forms[count].key.pixel.ambient_occlusion = (uint8_t)(occlusion | XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED);
		twin.key = forms[count++].key;
	}
	twin.pixel_program.ambient_occlusion = twin.key.pixel.ambient_occlusion;
	if (xgpu_stream_world_ao_twin(&twin))
	{
		forms[count] = *derived;
		forms[count++].key = twin.key;
	}
	return count;
}

/* ---------- the recorder */

#ifndef XGPU_MANIFEST_READER_ONLY

/* a set of 64-bit hashes (0 is never one: it is made 1) */
struct hash_set
{
	uint64_t *slots;
	uint32_t count, capacity;
};

/* adds hash; 1 if it was not there */
static int hash_set_add(struct hash_set *set, uint64_t hash)
{
	uint32_t index;

	if (!hash)
		hash = 1;
	if ((set->count + 1) * 2 > set->capacity)
	{
		struct hash_set larger;
		uint32_t old;

		larger.capacity = set->capacity ? set->capacity * 2 : 1024;
		larger.count = 0;
		larger.slots = calloc(larger.capacity, sizeof(uint64_t));
		if (!larger.slots)
			return 0;
		for (old = 0; old < set->capacity; old++)
		{
			if (set->slots[old])
				hash_set_add(&larger, set->slots[old]);
		}
		free(set->slots);
		*set = larger;
	}
	for (index = (uint32_t)hash & (set->capacity - 1); set->slots[index]; index = (index + 1) & (set->capacity - 1))
	{
		if (set->slots[index] == hash)
			return 0;
	}
	set->slots[index] = hash;
	set->count++;
	return 1;
}

static int hash_set_has(const struct hash_set *set, uint64_t hash)
{
	uint32_t index;

	if (!hash)
		hash = 1;
	if (!set->capacity)
		return 0;
	for (index = (uint32_t)hash & (set->capacity - 1); set->slots[index]; index = (index + 1) & (set->capacity - 1))
	{
		if (set->slots[index] == hash)
			return 1;
	}
	return 0;
}

static struct
{
	/* -1 not read yet, 0 off, 1 on */
	int state;
	FILE *file;
	struct xgpu_manifest_vertex_program *programs;
	uint32_t program_count, program_capacity;
	struct hash_set written_programs, written_pixels, draws;
	/* the next draw's: as drawn, and as no setting changes it with its
	context (written) */
	struct xgpu_stream_pipeline_key key, basis;
	struct xgpu_manifest_context context;
	int have_key, have_basis;
	struct xgpu_manifest_stage stages[4];
	uint32_t element_count;
	struct xgpu_manifest_element elements[XGPU_MANIFEST_ATTRIBUTES];
	uint16_t strides[XGPU_MANIFEST_STREAMS];
	struct text text;
	unsigned long recorded;
	/* a map's enumeration being drawn (xgpu_manifest_enumerating), its
	draws so far */
	int enumerating;
	struct xgpu_manifest enumeration;
	uint32_t enumeration_capacity;
	struct hash_set enumerated, missed;
	int enumerated_once;
} recorder = { -1 };

/* the next draw is noted: recorded, or a map's enumeration's */
static int noting(void)
{
	return recorder.state > 0 || recorder.enumerating;
}

int xgpu_manifest_recording(void)
{
	if (recorder.state < 0)
	{
		const char *path = config_string("debug.render_manifest");

		recorder.state = 0;
		if (path && *path)
		{
			recorder.file = fopen(path, "a");
			if (!recorder.file)
			{
				platform_log("GPU: cannot write the renderer's manifest to %s", path);
				return 0;
			}
			fseek(recorder.file, 0, SEEK_END);
			if (ftell(recorder.file) == 0)
				fprintf(recorder.file, "# the renderer's manifest, recorded (xgpu_manifest.h)\nmanifest %d\n",
					XGPU_MANIFEST_VERSION);
			fflush(recorder.file);
			platform_log("GPU: recording the draws' shaders and pipelines to %s", path);
			recorder.state = 1;
		}
	}
	return recorder.state > 0;
}

void xgpu_manifest_note_vertex_program(unsigned long long hash, const uint32_t *words, unsigned long instruction_count)
{
	struct xgpu_manifest_vertex_program *program;
	uint32_t index;

	if (!xgpu_manifest_recording() || instruction_count > XGPU_MANIFEST_MICROCODE_WORDS / 4)
		return;
	for (index = 0; index < recorder.program_count; index++)
	{
		if (recorder.programs[index].hash == hash)
			return;
	}
	if (recorder.program_count == recorder.program_capacity)
	{
		uint32_t capacity = recorder.program_capacity ? recorder.program_capacity * 2 : 64;
		struct xgpu_manifest_vertex_program *larger = realloc(recorder.programs, capacity * sizeof(*larger));

		if (!larger)
			return;
		recorder.programs = larger;
		recorder.program_capacity = capacity;
	}
	program = &recorder.programs[recorder.program_count++];
	memset(program, 0, sizeof(*program));
	program->hash = hash;
	program->instruction_count = (uint32_t)instruction_count;
	memcpy(program->words, words, instruction_count * 4 * sizeof(uint32_t));
}

void xgpu_manifest_note_textures(const struct xgpu_texture_source *stages)
{
	int index;

	if (!noting())
		return;
	memset(recorder.stages, 0, sizeof(recorder.stages));
	for (index = 0; index < 4; index++)
	{
		const struct xgpu_texture_source *from = &stages[index];
		struct xgpu_manifest_stage *to = &recorder.stages[index];

		to->source = from->source;
		to->sampler_type = from->sampler_type;
		memcpy(to->address, from->sampler.address, sizeof(to->address));
		/* (w addresses a 3D texture alone: in another, what earlier draws
		left) */
		/* (but the high-res text's distance fields') */
		if (to->sampler_type != _xgpu_sampler_3d && to->address[2] != XGPU_ADDRESS_MSDF)
			to->address[2] = 0;
		if (from->source != XGPU_SOURCE_NONE && (to->address[0] == XGPU_ADDRESS_BORDER ||
			to->address[1] == XGPU_ADDRESS_BORDER || to->address[2] == XGPU_ADDRESS_BORDER ||
			to->address[0] == XGPU_ADDRESS_REGION))
		{
			to->border_color = (uint32_t)from->sampler.border_color;
		}
	}
}

void xgpu_manifest_note_pipeline(const struct xgpu_pipeline_key *key)
{
	if (!noting())
		return;
	xgpu_stream_pipeline_key_describe(key, &recorder.key);
	recorder.have_key = 1;
}

void xgpu_manifest_note_basis(const struct xgpu_pipeline_key *key, const struct xgpu_manifest_context *context)
{
	if (recorder.state <= 0)
		return;
	xgpu_stream_pipeline_key_describe(key, &recorder.basis);
	recorder.context = *context;
	recorder.have_basis = 1;
}

void xgpu_manifest_note_vertices(const struct xgpu_vertex_input *input)
{
	unsigned long index;

	if (!noting())
		return;
	recorder.element_count = 0;
	memset(recorder.elements, 0, sizeof(recorder.elements));
	memset(recorder.strides, 0, sizeof(recorder.strides));
	for (index = 0; index < input->element_count && index < XGPU_MANIFEST_ATTRIBUTES; index++)
	{
		const struct xgpu_vertex_element *from = &input->elements[index];
		struct xgpu_manifest_element *to = &recorder.elements[recorder.element_count++];

		to->reg = from->reg;
		to->stream = from->stream;
		to->format = from->format;
		to->bytes = from->bytes;
		to->offset = from->offset;
		if (from->stream < XGPU_MANIFEST_STREAMS && input->streams[from->stream].present)
			recorder.strides[from->stream] = (uint16_t)input->streams[from->stream].stride;
	}
}

static void record_write(void)
{
	if (recorder.text.length)
	{
		fwrite(recorder.text.buffer, 1, recorder.text.length, recorder.file);
		fflush(recorder.file);
		recorder.text.length = 0;
	}
}

void xgpu_manifest_note(const char *text)
{
	if (recorder.state <= 0)
		return;
	fprintf(recorder.file, "# %s\n", text);
	fflush(recorder.file);
}

/* the functions a draw was made in, from its caller out (each frame's
return address), as a comment line: a draw recorded outside a map's
enumeration, a draw the enumeration misses (addr2line -f -e the guest's
or the game's image names them) */
static void missed_write(void)
{
#if defined(__x86_64__) || defined(__i386__)
	uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
	int level;
#endif

	fprintf(recorder.file, "# missed:");
#if defined(__x86_64__) || defined(__i386__)
	for (level = 0; level < 24 && frame; level++)
	{
		uintptr_t next = *(uintptr_t *)frame;
		/* (the return address after the saved frame pointer, 8 bytes each in
		the x32 guest as in the 64-bit ABI) */
		unsigned long long address = *(unsigned long long *)(frame + sizeof(unsigned long long));

#ifdef __i386__
		address = *(unsigned long *)(frame + sizeof(unsigned long));
#endif
		fprintf(recorder.file, " %llx", address);
		if (next <= frame || next - frame > 1024 * 1024)
			break;
		frame = next;
	}
#endif
	fprintf(recorder.file, "\n");
	fflush(recorder.file);
}

static void enumeration_add(const struct xgpu_manifest_draw *draw, uint64_t hash)
{
	if (!hash_set_add(&recorder.enumerated, hash))
		return;
	if (recorder.enumeration.draw_count == recorder.enumeration_capacity)
	{
		uint32_t capacity = recorder.enumeration_capacity ? recorder.enumeration_capacity * 2 : 256;
		struct xgpu_manifest_draw *larger = realloc(recorder.enumeration.draws, capacity * sizeof(*larger));

		if (!larger)
			return;
		recorder.enumeration.draws = larger;
		recorder.enumeration_capacity = capacity;
	}
	recorder.enumeration.draws[recorder.enumeration.draw_count++] = *draw;
}

void xgpu_manifest_enumerating(int enumerating, int new_map)
{
	xgpu_manifest_recording();
	if (enumerating && new_map)
	{
		free(recorder.enumerated.slots);
		memset(&recorder.enumerated, 0, sizeof(recorder.enumerated));
		recorder.enumeration.draw_count = 0;
		free(recorder.missed.slots);
		memset(&recorder.missed, 0, sizeof(recorder.missed));
	}
	recorder.enumerating = enumerating;
	recorder.enumerated_once = 1;
	xgpu_manifest_note(enumerating ? "enumerating: the draws until \"prepared\" are a map's enumeration's" :
		"prepared: a draw after not in the map's enumeration is marked \"missed\"");
}

const struct xgpu_manifest *xgpu_manifest_enumeration(void)
{
	recorder.enumeration.vertex_programs = recorder.programs;
	recorder.enumeration.vertex_program_count = recorder.program_count;
	return &recorder.enumeration;
}

void xgpu_manifest_record_draw(int immediate, unsigned long written)
{
	struct xgpu_manifest_draw draw;
	uint64_t hash, recorded, program;
	uint32_t index;
	int missed;

	if (!noting() || !recorder.have_key)
		return;
	memset(&draw, 0, sizeof(draw));
	draw.key = recorder.key;
	if (immediate)
	{
		draw.immediate_written = (uint32_t)written;
	}
	else
	{
		draw.element_count = recorder.element_count;
		memcpy(draw.elements, recorder.elements, sizeof(draw.elements));
		memcpy(draw.strides, recorder.strides, sizeof(draw.strides));
	}
	memcpy(draw.stages, recorder.stages, sizeof(draw.stages));
	hash = xgpu_manifest_hash(&draw, sizeof(draw), XGPU_MANIFEST_HASH_START);
	if (recorder.enumerating)
		enumeration_add(&draw, hash);
	if (recorder.state <= 0)
		return;
	/* (a draw of play that the map's enumeration does not have, recorded
	again after the stack that made it) */
	missed = !recorder.enumerating && recorder.enumerated_once && !hash_set_has(&recorder.enumerated, hash) &&
		hash_set_add(&recorder.missed, hash);
	/* (written as no setting changes it, with its context: each written
	once) */
	if (recorder.have_basis)
	{
		draw.key = recorder.basis;
		recorded = xgpu_manifest_hash(&recorder.context, sizeof(recorder.context),
			xgpu_manifest_hash(&draw, sizeof(draw), XGPU_MANIFEST_HASH_START));
	}
	else
	{
		recorded = hash;
	}
	if (!hash_set_add(&recorder.draws, recorded) && !missed)
		return;
	if (missed)
		missed_write();
	/* (the vertex program and the pixel key first, once) */
	program = draw_vertex_program(&draw);
	if (hash_set_add(&recorder.written_programs, program))
	{
		for (index = 0; index < recorder.program_count; index++)
		{
			if (recorder.programs[index].hash == program)
				vertex_program_write(&recorder.text, &recorder.programs[index]);
		}
	}
	if (hash_set_add(&recorder.written_pixels, pixel_hash(&draw.key.pixel)))
		pixel_write(&recorder.text, &draw.key.pixel);
	draw_write(&recorder.text, &draw, recorder.have_basis ? &recorder.context : NULL);
	record_write();
	recorder.recorded++;
}

#endif
