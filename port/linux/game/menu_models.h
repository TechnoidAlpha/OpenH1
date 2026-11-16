/*
MENU_MODELS.H

The menus' models: a Spartan the game draws in the menus' 3D scene, made of
the player's own maps' tags, never of a picture or anything shipped.

Exported once, as the start's menu pictures pass has a stock multiplayer map
loaded (Blood Gulch, else the first other one the player has: the pass's
"MENU PICTURES" counts it as one item; port/linux/game/menu_pictures_render.c),
as one file in the data folder's menu_models, spartan.bin, with a note
beside it as each picture has (spartan.txt: the map's checksum, the format's
version, the tries that failed); made again when the file is missing or
damaged, the map another, or the format's version another.

The file holds the tags the Spartan is drawn with and nothing else: the
multiplayer cyborg's model, its animation graph, the assault rifle's model,
their shaders and the shaders' bitmaps with their pixels, and two scenery
definitions made for them (the cyborg's and the rifle's, naming the model
and the graph; the biped and the weapon tags themselves are not taken).
Every tag is copied through its group's schema (tag_schema.h): each block
and data copied with its elements, each reference and tag index to a tag of
the closure made the file's own index, every other one none (sounds,
effects, collision, physics). Every pointer is an offset in the file's tags,
which begin with a tag header as a map's do: so they are checked as a map's
are, by the same validator, before anything reads them. What the schema
cannot say, the file says as a map does: a model part's vertices and
triangles are in the tags, its buffers the header's; a bitmap's pixels are
in the file after the tags, its offset the file's.

The file (all of it little-endian, as every build of the game is):

	header (menu_models_file_header)
	tags: the tag header, the tag table, the tags' roots, blocks and data,
		the vertex and index buffers and their data, the tags' names
	pixels

Read when ui.map's tags load (menu_tags.c), and added to its tag table after
the menus' own: a file that cannot be read, that is damaged, or whose tags the
validator refuses is no Spartan (and the menus draw the original pictures),
never a crash.
*/

#ifndef MENU_MODELS_H
#define MENU_MODELS_H

/* ---------- constants */

enum
{
	MENU_MODELS_SIGNATURE = 'mmdl',
	/* the file's format: a file of another is made again */
	MENU_MODELS_VERSION = 1,
	MENU_MODELS_HEADER_SIZE = 64,
	/* the largest file read (the tags must fit the validator's tag cache) */
	MENU_MODELS_MAXIMUM_FILE_SIZE = 64 << 20,
	MENU_MODELS_MAXIMUM_TAGS = 64,
	/* a D3D vertex or index buffer in the tag header's arrays: Common, Data,
	Lock */
	MENU_MODELS_BUFFER_SIZE = 12,
	/* the tag header's signature */
	MENU_MODELS_TAG_HEADER_SIGNATURE = 'tags',
	/* the zeros after the pixels */
	MENU_MODELS_PIXELS_PADDING = 4096,
	/* how deep a schema's blocks go (tag_validate.c's) */
	MENU_MODELS_MAXIMUM_DEPTH = 32,
};

/* the scenery definitions the file makes: the Spartan's (the cyborg's model
and graph) and its rifle's */
#define MENU_MODELS_SPARTAN "ui\\menu_models\\spartan"
#define MENU_MODELS_RIFLE "ui\\menu_models\\assault rifle"

/* ---------- structures */

/* the file's header: every field 32 bits */
struct menu_models_file_header
{
	unsigned int signature;
	unsigned int version;
	unsigned int file_size;
	/* the CRC-32 of the bytes after the header */
	unsigned int body_crc;
	/* the map the tags were taken from */
	unsigned int map_checksum;
	unsigned int tags_offset;
	unsigned int tags_size;
	unsigned int pixels_offset;
	unsigned int pixels_size;
	unsigned int tag_count;
	char map_name[24];
};

/* the tags' header, as a map's (cache_files.c's struct
cache_file_tag_header, tag_validate.c's struct tag_validate_header) */
struct menu_models_tag_header
{
	struct menu_models_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	unsigned char *vertex_buffers;
	long index_buffer_count;
	unsigned char *index_buffers;
	unsigned long signature;
};

/* a tag in the table (tag_groups.h's struct cache_file_tag_instance) */
struct menu_models_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

struct tag_schema_definition;

/* the tags being written: their bytes (the tag header first), the pixels
after them, and what is only known as the file is finished */
struct menu_models_writer
{
	unsigned char *tags;
	unsigned long tags_size, tags_capacity;
	unsigned char *pixels;
	unsigned long pixels_size, pixels_capacity;
	/* the longs in the tags that hold an offset in the pixels, made the
	file's */
	unsigned long *pixel_fixups;
	long pixel_fixup_count, pixel_fixup_capacity;
	/* each model part's buffers: the part's offset and its vertices' and
	indices' */
	struct menu_models_part_buffers *parts;
	long part_count, part_capacity;
	/* each reference's offset in the tags and the tag it names (its
	absolute index, NONE for none), whose name it is given */
	struct menu_models_reference_name *references;
	long reference_count, reference_capacity;
	/* the tags: each one's group, name and root */
	struct menu_models_written_tag *written;
	long tag_count;
	int failed;
	char const *why;
};

struct menu_models_part_buffers
{
	/* the part's vertex and index buffers' hardware formats */
	unsigned long vertex_field_offset;
	unsigned long index_field_offset;
	unsigned long vertices_offset;
	unsigned long indices_offset;
};

struct menu_models_reference_name
{
	unsigned long offset;
	long absolute_index;
};

struct menu_models_written_tag
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	char name[256];
	unsigned long root_offset;
};

/* how a tag's fields are copied from the map's (menu_models_copy_tag) */
struct menu_models_copy
{
	struct menu_models_writer *writer;
	void *context;
	/* the file's index of a tag a reference or a tag index names, of group
	(0 for a tag index), NONE for none (and for a tag the file leaves out) */
	long (*tag_index)(void *context, long tag_index, unsigned long group_tag);
	/* bytes in the map's file (data the schema says is there), read into
	buffer: FALSE if they cannot be */
	int (*file_read)(void *context, long offset, long size, void *buffer);
	/* an element of a definition the schema cannot say all of (a model
	part's buffers, a bitmap's pixels), at element_offset in the tags, from
	source: FALSE if it cannot be copied */
	int (*element)(void *context, struct tag_schema_definition const *definition, unsigned long element_offset,
		void const *source);
};

/* ---------- prototypes (menu_models_file.c: the file, which tools/harness's tests build) */

void menu_models_writer_new(struct menu_models_writer *writer);
void menu_models_writer_dispose(struct menu_models_writer *writer);
/* size bytes (zeros if bytes is NULL) added to the tags, at a multiple of 8:
their offset, or 0 if there is no memory */
unsigned long menu_models_writer_append(struct menu_models_writer *writer, void const *bytes, unsigned long size);
/* size bytes added to the pixels, and the long at field_offset in the tags
made their offset in the file: FALSE if there is no memory */
int menu_models_writer_pixels(struct menu_models_writer *writer, void const *bytes, unsigned long size,
	unsigned long field_offset);
/* a model part's vertices and indices (their bytes) given buffers of the
tag header's, which the pointers at vertex_field_offset and index_field_offset
in the tags (the part's buffers' hardware formats) are made: FALSE if there
is no memory */
int menu_models_writer_part(struct menu_models_writer *writer, unsigned long vertex_field_offset,
	unsigned long index_field_offset, void const *vertices, unsigned long vertices_size, void const *indices,
	unsigned long indices_size);
/* a tag of the group (whose parents are given) added to the table, its root
copied from source through the schema's definition (copy says how): its
index in the file, NONE if it cannot be. The file's index of the tag the
writer will add next is menu_models_tag_index(writer->tag_count) */
long menu_models_copy_tag(struct menu_models_copy *copy, unsigned long group_tag,
	unsigned long const parent_group_tags[2], char const *name, void const *source,
	struct tag_schema_definition const *definition);
/* the file's tag index of its absolute index */
long menu_models_tag_index(long absolute_index);
/* the file made: its bytes (malloc's) and size, its header saying map and
map_checksum; NULL if there is no memory */
unsigned char *menu_models_writer_finish(struct menu_models_writer *writer, char const *map,
	unsigned long map_checksum, unsigned long *size);

/* the CRC-32 (zlib's) of size bytes */
unsigned long menu_models_crc(unsigned long crc, unsigned char const *bytes, unsigned long size);
/* a file of size bytes read: its header, whether it is whole and of this
version (FALSE, and why, if not) */
int menu_models_file_check(unsigned char const *file, unsigned long size, struct menu_models_file_header *header,
	char const **why);
/* the tags of a file that menu_models_file_check passed (size bytes at
tags, a multiple of 8 from the file's start): every offset made a pointer,
walked through the schemas, each byte once (FALSE, and why, if any lies
outside the tags or is walked twice). Then the validator checks them */
int menu_models_relocate(unsigned char *tags, unsigned long size, char const **why);
/* each reference and tag index in the tags (relocated and validated) made
the tag table's they are added to: tag_indices[absolute index in the file],
and each reference's name its tag's (names[absolute index]) */
void menu_models_remap(unsigned char *tags, long const *tag_indices, char *const *names);
/* a group's schema (NULL if none) */
struct tag_schema_group const *menu_models_schema_group(unsigned long group_tag);

/* ---------- prototypes (menu_models.c: the game's) */

/* the pass: the map (of the poses) it exports from, if the file is to be
made, else -1 */
long menu_models_export_pose(void);
/* the map loaded: the file exported from it, and read back as the menus
will read it: TRUE if it was made */
int menu_models_export(void);
/* an export that failed, or was never finished (the pass out of time, or
stopped) */
void menu_models_export_failed(char const *why);

/* ui.map's tags (menu_tags.c): the file read and checked, how many tags it
adds (0: none); then added at those tag indices, into the tag table */
long menu_models_tags_load(void);
void menu_models_tags_add(void *instances, long const *tag_indices);
void menu_models_tags_release(void);
/* the tag index of the file's tag of group and name (NONE if none) */
long menu_models_tag(unsigned long group_tag, char const *name);
/* the texture cache: a bitmap of the file's has its pixels read from it
into buffer (FALSE: not one of the file's) */
int menu_models_bitmap_read(void *bitmap, void *buffer);

#endif
