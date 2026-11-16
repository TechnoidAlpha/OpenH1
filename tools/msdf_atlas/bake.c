/*
The MSDF atlas's glyphs (tools/msdf_atlas.py): each font's characters of the
set made fields (port/third_party/msdfgen/msdf_gen.h) and packed into one atlas,
written as the atlas's file before it is compressed (text_hires.c reads it).

	bake OUTPUT SPEC

SPEC's lines:

	em <texels> range <texels> width <texels>
	file <name> <path> [lacking <file index>]
	codes <code> <first>-<last> ...

A file's glyphs are the codes' it has; one lacking another file only those
that file has not (a fallback's). OUTPUT, little-endian:

	"OHMS", version 1
	u16 width, u16 height, f32 range, f32 em
	u16 files; each: char name[48], f32 scale (texels to a font unit)
	u32 glyphs; each: u16 file, u16 pad, u32 code, u16 x, y, width, height,
	                  f32 left, f32 top (the field's edges, in font units)
	width * height * 4 bytes, rows from the top
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../port/third_party/stb/stb_truetype.h"
#include "../../port/third_party/msdfgen/msdf_gen.h"

#define MAXIMUM_FILES 16
#define MAXIMUM_CODES 1024
#define MAXIMUM_GLYPHS 4096

static struct
{
	char name[48];
	unsigned char *data;
	stbtt_fontinfo info;
	int lacking;
	float scale;
} files[MAXIMUM_FILES];
static int file_count;

static unsigned long codes[MAXIMUM_CODES];
static int code_count;

struct glyph
{
	int file, index;
	unsigned long code;
	int x, y, width, height;
	float left, top;
};
static struct glyph glyphs[MAXIMUM_GLYPHS];
static int glyph_count;

static unsigned char *read_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long size;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (data && fread(data, 1, (size_t)size, file) != (size_t)size)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	return data;
}

static int taller(const void *a, const void *b)
{
	const struct glyph *x = a, *y = b;

	if (x->height != y->height)
		return y->height - x->height;
	if (x->file != y->file)
		return x->file - y->file;
	return x->code < y->code ? -1 : x->code > y->code;
}

static void put16(FILE *out, unsigned value)
{
	fputc((int)(value & 255), out);
	fputc((int)((value >> 8) & 255), out);
}

static void put32(FILE *out, unsigned long value)
{
	put16(out, (unsigned)(value & 0xffff));
	put16(out, (unsigned)((value >> 16) & 0xffff));
}

static void put_float(FILE *out, float value)
{
	unsigned long bits;
	unsigned int word;

	memcpy(&word, &value, 4);
	bits = word;
	put32(out, bits);
}

int main(int argc, char **argv)
{
	FILE *spec, *out;
	char line[4096];
	double em = 40.0, range = 6.0;
	int width = 1024, height, pad, x, y, row, index;
	unsigned char *atlas;

	if (argc != 3 || !(spec = fopen(argv[2], "r")))
	{
		fprintf(stderr, "usage: bake OUTPUT SPEC\n");
		return 2;
	}
	while (fgets(line, sizeof(line), spec))
	{
		char *word = strtok(line, " \t\n");

		if (!word)
			continue;
		if (!strcmp(word, "em"))
		{
			em = atof(strtok(NULL, " \t\n"));
			strtok(NULL, " \t\n");
			range = atof(strtok(NULL, " \t\n"));
			strtok(NULL, " \t\n");
			width = atoi(strtok(NULL, " \t\n"));
		}
		else if (!strcmp(word, "file") && file_count < MAXIMUM_FILES)
		{
			char *name = strtok(NULL, " \t\n"), *path = strtok(NULL, " \t\n"), *lacking = strtok(NULL, " \t\n");
			snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", name);
			files[file_count].data = read_file(path);
			files[file_count].lacking = lacking ? atoi(strtok(NULL, " \t\n")) : -1;
			if (!files[file_count].data ||
				!stbtt_InitFont(&files[file_count].info, files[file_count].data,
					stbtt_GetFontOffsetForIndex(files[file_count].data, 0)))
			{
				fprintf(stderr, "bake: cannot read %s\n", path);
				return 1;
			}
			files[file_count].scale = stbtt_ScaleForMappingEmToPixels(&files[file_count].info, (float)em);
			file_count++;
		}
		else if (!strcmp(word, "codes"))
		{
			while ((word = strtok(NULL, " \t\n")) != NULL)
			{
				char *dash = strchr(word, '-');
				unsigned long first = strtoul(word, NULL, 0), last = dash ? strtoul(dash + 1, NULL, 0) : first, code;

				for (code = first; code <= last && code_count < MAXIMUM_CODES; code++)
					codes[code_count++] = code;
			}
		}
	}
	fclose(spec);
	pad = (int)(range / 2.0 + 0.999) + 1;
	for (index = 0; index < file_count; index++)
	{
		int code;

		for (code = 0; code < code_count; code++)
		{
			int glyph = stbtt_FindGlyphIndex(&files[index].info, (int)codes[code]), x0, y0, x1, y1;
			struct glyph *made;

			if (!glyph || (files[index].lacking >= 0 &&
				stbtt_FindGlyphIndex(&files[files[index].lacking].info, (int)codes[code])))
				continue;
			if (!stbtt_GetGlyphBox(&files[index].info, glyph, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0)
				continue;
			if (glyph_count >= MAXIMUM_GLYPHS)
				return 1;
			made = &glyphs[glyph_count++];
			made->file = index;
			made->index = glyph;
			made->code = codes[code];
			made->width = (int)((x1 - x0) * files[index].scale + 0.999) + 2 * pad;
			made->height = (int)((y1 - y0) * files[index].scale + 0.999) + 2 * pad;
			made->left = (float)(x0 - pad / files[index].scale);
			made->top = (float)(y1 + pad / files[index].scale);
		}
	}
	/* (shelves, the tallest first; a texel between the glyphs) */
	qsort(glyphs, (size_t)glyph_count, sizeof(glyphs[0]), taller);
	x = y = row = 0;
	for (index = 0; index < glyph_count; index++)
	{
		if (x + glyphs[index].width > width)
		{
			x = 0;
			y += row + 1;
			row = 0;
		}
		glyphs[index].x = x;
		glyphs[index].y = y;
		x += glyphs[index].width + 1;
		if (glyphs[index].height > row)
			row = glyphs[index].height;
	}
	height = (y + row + 3) & ~3;
	atlas = calloc((size_t)width * height, 4);
	for (index = 0; index < glyph_count; index++)
	{
		struct glyph *glyph = &glyphs[index];
		stbtt_vertex *vertices;
		struct msdf_vertex *shape;
		unsigned char *field = malloc((size_t)glyph->width * glyph->height * 4);
		int count = stbtt_GetGlyphShape(&files[glyph->file].info, glyph->index, &vertices), vertex;

		shape = malloc(sizeof(*shape) * (size_t)(count ? count : 1));
		for (vertex = 0; vertex < count; vertex++)
		{
			shape[vertex].type = vertices[vertex].type;
			shape[vertex].x = vertices[vertex].x;
			shape[vertex].y = vertices[vertex].y;
			shape[vertex].cx = vertices[vertex].cx;
			shape[vertex].cy = vertices[vertex].cy;
			shape[vertex].cx1 = vertices[vertex].cx1;
			shape[vertex].cy1 = vertices[vertex].cy1;
		}
		if (!msdf_generate(shape, count, files[glyph->file].scale, glyph->left, glyph->top, range, glyph->width,
			glyph->height, field))
		{
			fprintf(stderr, "bake: %s U+%04lX has too many edges\n", files[glyph->file].name, glyph->code);
			return 1;
		}
		for (row = 0; row < glyph->height; row++)
			memcpy(atlas + ((size_t)(glyph->y + row) * width + glyph->x) * 4, field + (size_t)row * glyph->width * 4,
				(size_t)glyph->width * 4);
		stbtt_FreeShape(&files[glyph->file].info, vertices);
		free(shape);
		free(field);
	}
	out = fopen(argv[1], "wb");
	if (!out)
		return 1;
	fwrite("OHMS", 1, 4, out);
	put32(out, 1);
	put16(out, (unsigned)width);
	put16(out, (unsigned)height);
	put_float(out, (float)range);
	put_float(out, (float)em);
	put16(out, (unsigned)file_count);
	for (index = 0; index < file_count; index++)
	{
		fwrite(files[index].name, 1, sizeof(files[index].name), out);
		put_float(out, files[index].scale);
	}
	put32(out, (unsigned long)glyph_count);
	for (index = 0; index < glyph_count; index++)
	{
		put16(out, (unsigned)glyphs[index].file);
		put16(out, 0);
		put32(out, glyphs[index].code);
		put16(out, (unsigned)glyphs[index].x);
		put16(out, (unsigned)glyphs[index].y);
		put16(out, (unsigned)glyphs[index].width);
		put16(out, (unsigned)glyphs[index].height);
		put_float(out, glyphs[index].left);
		put_float(out, glyphs[index].top);
	}
	fwrite(atlas, 4, (size_t)width * height, out);
	fclose(out);
	fprintf(stderr, "bake: %d glyphs of %d files, %dx%d\n", glyph_count, file_count, width, height);
	return 0;
}
