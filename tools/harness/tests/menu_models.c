/*
MENU_MODELS.C (test)

The menus' models' file (port/linux/game/menu_models_file.c): tags of a
small schema of the test's own (a root whose block's elements hold data, a
reference and a tag index, and data in the map's file) written through the
schema, the file checked and its offsets made pointers, then each reference
and tag index made a tag table's: the round trip. A file damaged (a byte
changed, another version) is refused by its check; one whose pointers lie
outside its tags or share bytes, made whole again (its CRC-32 right), is
refused as its pointers are made, before anything reads it (the game's
validator checks what passes, tag_validate.c). test_menu_models.py takes the
schema's header (tag_schema.h), the file's (menu_models.h) and its code.
*/

#include "harness.h"

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
typedef unsigned short word;

#include "menu_models_types.inc"

/* ---------- the test's schema */

struct test_element
{
	long value;
	struct tag_data bytes;
	struct tag_reference other;
	long tag;
};

struct test_root
{
	long magic;
	struct tag_block elements;
	struct tag_data blob;
};

static struct tag_schema_field const test_element_fields[] =
{
	TAG_SCHEMA_DATA(struct test_element, bytes, 256),
	TAG_SCHEMA_REFERENCE(struct test_element, other, TAG_SCHEMA_GROUPS('root')),
	TAG_SCHEMA_TAG_INDEX(struct test_element, tag, TAG_SCHEMA_GROUPS('root')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const test_element_schema =
	TAG_SCHEMA_DEFINITION(test_element, struct test_element, test_element_fields);

static struct tag_schema_field const test_root_fields[] =
{
	TAG_SCHEMA_BLOCK(struct test_root, elements, test_element_schema, 16),
	TAG_SCHEMA_FILE_DATA(struct test_root, blob, 4096),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const test_root_schema =
	TAG_SCHEMA_DEFINITION(test_root, struct test_root, test_root_fields);

static struct tag_schema_group const test_groups[] =
{
	{ 'root', { NONE, NONE }, &test_root_schema },
	{ 0 }
};

struct tag_schema_group const *const tag_schema_group_lists[] = { test_groups, NULL };

#include "under_test.inc"

/* ---------- the map's tags */

enum
{
	SOURCE_A = 0x7000,
	SOURCE_B = 0x7001,
	/* a tag the file leaves out */
	SOURCE_LEFT_OUT = 0x7002,
};

static char const a_bytes[] = "the first element's bytes";
static char const b_bytes[] = "b's";

static struct test_element a_elements[2];
static struct test_element b_elements[1];
static struct test_root a_root, b_root;

static void sources_make(void)
{
	memset(a_elements, 0, sizeof(a_elements));
	a_elements[0].value = 7;
	a_elements[0].bytes.size = sizeof(a_bytes);
	a_elements[0].bytes.address = (void *)a_bytes;
	a_elements[0].other.group_tag = 'root';
	a_elements[0].other.index = SOURCE_B;
	a_elements[0].tag = SOURCE_A;
	a_elements[1].value = 8;
	a_elements[1].other.group_tag = 'root';
	a_elements[1].other.index = SOURCE_LEFT_OUT;
	a_elements[1].tag = NONE;
	memset(&a_root, 0, sizeof(a_root));
	a_root.magic = 0x1234;
	a_root.elements.count = 2;
	a_root.elements.address = a_elements;
	a_root.blob.size = 100;
	a_root.blob.file_offset = 5000;
	memset(b_elements, 0, sizeof(b_elements));
	b_elements[0].value = 9;
	b_elements[0].bytes.size = sizeof(b_bytes);
	b_elements[0].bytes.address = (void *)b_bytes;
	b_elements[0].other.group_tag = 'root';
	b_elements[0].other.index = SOURCE_A;
	b_elements[0].tag = SOURCE_B;
	memset(&b_root, 0, sizeof(b_root));
	b_root.magic = 0x5678;
	b_root.elements.count = 1;
	b_root.elements.address = b_elements;
}

static long source_tag_index(void *context, long tag_index, unsigned long group_tag)
{
	(void)context;
	(void)group_tag;
	return tag_index == SOURCE_A ? menu_models_tag_index(0) : tag_index == SOURCE_B ? menu_models_tag_index(1) : NONE;
}

/* the map's file: each byte its offset's low byte */
static int source_file_read(void *context, long offset, long size, void *buffer)
{
	long index;

	(void)context;
	for (index = 0; index < size; index++)
		((unsigned char *)buffer)[index] = (unsigned char)(offset + index);
	return 1;
}

static unsigned char *file_made(unsigned long *size)
{
	struct menu_models_writer writer;
	struct menu_models_copy copy;
	unsigned long const parents[2] = { NONE, NONE };
	unsigned char *file;

	sources_make();
	menu_models_writer_new(&writer);
	memset(&copy, 0, sizeof(copy));
	copy.writer = &writer;
	copy.tag_index = source_tag_index;
	copy.file_read = source_file_read;
	CHECK(menu_models_copy_tag(&copy, 'root', parents, "a name", &a_root, &test_root_schema) ==
		menu_models_tag_index(0), "a is the file's first tag");
	CHECK(menu_models_copy_tag(&copy, 'root', parents, "b name", &b_root, &test_root_schema) ==
		menu_models_tag_index(1), "b is the file's second tag");
	file = menu_models_writer_finish(&writer, "testmap", 0xCAFEF00DUL, size);
	CHECK(file != NULL, "the file is made: %s", writer.why ? writer.why : "");
	menu_models_writer_dispose(&writer);
	return file;
}

/* the file whole again after a change (its CRC-32 made right) */
static void file_rewhole(unsigned char *file, unsigned long size)
{
	struct menu_models_file_header header;

	memcpy(&header, file, sizeof(header));
	header.body_crc = (unsigned int)menu_models_crc(0, file + MENU_MODELS_HEADER_SIZE, size - MENU_MODELS_HEADER_SIZE);
	memcpy(file, &header, sizeof(header));
}

/* a tag's root in the file not yet read (its offset in the tags) */
static struct test_root *unread_root(unsigned char *file, long absolute_index)
{
	unsigned char *tags = file + MENU_MODELS_HEADER_SIZE;
	struct menu_models_tag_header const *header = (struct menu_models_tag_header const *)tags;
	struct menu_models_instance const *instances =
		(struct menu_models_instance const *)(tags + (unsigned long)header->instances);

	return (struct test_root *)(tags + (unsigned long)instances[absolute_index].base_address);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct menu_models_file_header header;
	char const *why = NULL;
	unsigned long size;
	unsigned char *file = file_made(&size);

	if (!strcmp(case_name, "round_trip"))
	{
		struct menu_models_tag_header *tags;
		struct test_root *a, *b;
		struct test_element *elements;
		long const indices[2] = { 0x11110005, 0x22220006 };
		char *names[2];

		CHECK(menu_models_file_check(file, size, &header, &why), "the file passes its check: %s", why);
		CHECK(header.map_checksum == 0xCAFEF00DUL && !strcmp(header.map_name, "testmap") && header.tag_count == 2,
			"its header names its map");
		CHECK(menu_models_relocate(file + header.tags_offset, header.tags_size, &why), "its pointers are made: %s",
			why ? why : "");
		tags = (struct menu_models_tag_header *)(file + header.tags_offset);
		CHECK(tags->tag_count == 2 && tags->instances[1].tag_index == menu_models_tag_index(1), "two tags");
		CHECK(!strcmp(tags->instances[0].name, "a name") && !strcmp(tags->instances[1].name, "b name"), "named");
		a = tags->instances[0].base_address;
		b = tags->instances[1].base_address;
		CHECK(a->magic == 0x1234 && b->magic == 0x5678, "the roots copied");
		CHECK(a->elements.count == 2 && (unsigned char *)a->elements.address > file &&
			(unsigned char *)a->elements.address < file + size, "a's block in the file");
		elements = a->elements.address;
		CHECK(elements[0].value == 7 && elements[1].value == 8, "a's elements copied");
		CHECK(elements[0].bytes.size == sizeof(a_bytes) && !memcmp(elements[0].bytes.address, a_bytes, sizeof(a_bytes)),
			"a's data copied");
		CHECK(elements[0].other.index == menu_models_tag_index(1) && !strcmp(elements[0].other.name, "b name"),
			"a reference to a tag of the file names it");
		CHECK(elements[1].other.index == NONE && elements[1].other.name && !elements[1].other.name[0],
			"a reference to a tag left out is none");
		CHECK(elements[0].tag == menu_models_tag_index(0) && elements[1].tag == NONE, "tag indices the file's");
		CHECK(a->blob.size == 100 && a->blob.file_offset >= (long)header.pixels_offset &&
			(unsigned long)a->blob.file_offset + 100 <= size, "data in the map's file is in the file's");
		CHECK(file[a->blob.file_offset] == (unsigned char)5000 && file[a->blob.file_offset + 99] == (unsigned char)5099,
			"its bytes read from the map's file");
		elements = b->elements.address;
		CHECK(elements[0].other.index == menu_models_tag_index(0), "b names a back");

		names[0] = "table a";
		names[1] = "table b";
		menu_models_remap(file + header.tags_offset, indices, names);
		elements = a->elements.address;
		CHECK(elements[0].other.index == 0x22220006 && !strcmp(elements[0].other.name, "table b"),
			"a's reference made the table's: %08lx", elements[0].other.index);
		CHECK(elements[0].tag == 0x11110005 && elements[1].tag == NONE, "a's tag indices the table's");
		CHECK(elements[1].other.index == NONE, "none stays none");
		elements = b->elements.address;
		CHECK(elements[0].other.index == 0x11110005 && elements[0].tag == 0x22220006, "b's made the table's");
	}
	else if (!strcmp(case_name, "damaged"))
	{
		file[size / 2] ^= 0x40;
		CHECK(!menu_models_file_check(file, size, &header, &why), "a byte changed is refused");
	}
	else if (!strcmp(case_name, "short"))
	{
		CHECK(!menu_models_file_check(file, size - 16, &header, &why), "a file cut short is refused");
	}
	else if (!strcmp(case_name, "version"))
	{
		struct menu_models_file_header changed;

		memcpy(&changed, file, sizeof(changed));
		changed.version++;
		memcpy(file, &changed, sizeof(changed));
		CHECK(!menu_models_file_check(file, size, &header, &why), "another version is refused");
	}
	else if (!strcmp(case_name, "outside"))
	{
		struct test_root *a = unread_root(file, 0);

		/* (a's block made to run past the tags' end, into the pixels: the
		file whole again) */
		CHECK(menu_models_file_check(file, size, &header, &why), "whole first");
		a->elements.address = (void *)(unsigned long)(header.tags_size - sizeof(struct test_element));
		file_rewhole(file, size);
		CHECK(menu_models_file_check(file, size, &header, &why), "whole again: %s", why);
		CHECK(!menu_models_relocate(file + header.tags_offset, header.tags_size, &why),
			"a block past the tags is refused");
	}
	else if (!strcmp(case_name, "shared"))
	{
		struct test_root *a = unread_root(file, 0);
		struct test_root *b = unread_root(file, 1);

		/* (b's block made a's: the same bytes walked twice) */
		CHECK(menu_models_file_check(file, size, &header, &why), "whole first");
		b->elements.address = a->elements.address;
		file_rewhole(file, size);
		CHECK(menu_models_file_check(file, size, &header, &why), "whole again: %s", why);
		CHECK(!menu_models_relocate(file + header.tags_offset, header.tags_size, &why),
			"blocks that share bytes are refused");
	}
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	free(file);
	return 0;
}
