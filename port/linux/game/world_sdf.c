/*
WORLD_SDF.C

display.ambient_occlusion "world"'s distance field of the structure
(world_sdf.h, docs/world-ao.md): found in the data folder's world_sdf, or
baked in the background (never during a load), and uploaded a few slices a
frame. Workstream B; the bake's arithmetic is world_sdf_bake.c.

The schedule (docs/world-ao.md, "The bake's schedule"):
- a map loaded (scenario_load: world_sdf_map_loaded) or a BSP switched to
  only asks the looker thread to find its field in the cache: no file is
  read in the load;
- a field missing from the cache is baked from a copy of the BSP's
  geometry (world_scene_static_triangles) and of its collision BSP's planes
  (the sign's point test), taken on the game's thread a few frames after
  the load and handed to the baker thread, which bakes it in slabs of z
  slices on the processors the game's own threads leave
  (world_sdf_bake_threads) at the lowest priority, at the frames' pace in
  play (world_sdf_pace_frame), writes it to the cache and hands it back;
- the first launch's "Preparing menu pictures" pass (menu_pictures_render.c)
  bakes the BSPs of every stock map it loads (the one each map starts in,
  and each picture's), where nobody waits to play
  (world_sdf_startup_map_loaded), and runs for them alone when only fields
  are missing (world_sdf_startup_wanted). Only the loaded BSP's tags are
  in memory, and the pass switches to no other BSP for them (repeated
  switches once leaked the device's groups, #506): a campaign map's other
  BSPs are baked in the background the first time the game switches to
  them (world_sdf_bsp_switched: a look in the cache, the copy a few frames
  later, never in the switch), the proxies alone shading them till then;
- a field found or baked is uploaded at most WORLD_SDF_UPLOAD_BYTES a frame,
  then fades in over a second (world_sdf_fade); until then the proxies
  alone shade, without those of what the field will hold (the static
  scenery's, the vehicles' boxes: world_sdf_scenery_awaited,
  world_sdf_object_awaited), which cost the most.

A bake outlives the map it was taken from (it works on copies): its field is
written to the cache all the same, and found there the next time.

The cache (<data>/world_sdf/<map>_<bsp>.wsdf, world_sdf.h's struct
world_sdf_header): the header written field by field, little-endian, the
voxels deflated, a CRC-32 of both; written to a .tmp beside it and renamed
into place, and a file that fails any check is deleted (and baked again).
*/

#include "cseries.h"
#include "ai/ai_scenario_definitions.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "math/real_math.h"
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "physics/bsp3d.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_geometry_environment.h"
#include "rasterizer/rasterizer_model_types.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_files.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

#include <xtl.h>

#include "memory/zlib/zlib.h"

#include "menu_pictures.h"
#include "world_ao.h"
#include "world_proxies.h"
#include "world_proxies_source.h"
#include "world_scene.h"
#include "world_sdf.h"
#include "../src/xgpu_coverage.h"

#include <pthread.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* (the C library's allocator, which every thread may call, not the game's
debug one, cseries.h's: the bake's threads allocate, and its fields are
not the game's memory) */
#undef malloc
#undef free
#undef realloc

/* ---------- constants */

enum
{
	/* the most bytes of the field uploaded a frame */
	WORLD_SDF_UPLOAD_BYTES = 512 * 1024,
	/* the main menu's frames after the start-up pass before its bakes begin */
	WORLD_SDF_MENU_SETTLE_FRAMES = 300,
	/* the frames after a load before a missing field's geometry is copied */
	WORLD_SDF_COPY_DELAY_FRAMES = 2,
	/* the cells a worker takes at a time */
	WORLD_SDF_WORKER_CELLS = 16,
	/* the most bakes waiting (the oldest waiting dropped past it) */
	WORLD_SDF_MAXIMUM_BAKES = 64,
	/* the most fields the start-up pass has tried */
	WORLD_SDF_MAXIMUM_TRIED = 64,
	/* the cache file's header, and its CRC after the deflated voxels */
	WORLD_SDF_FILE_HEADER = 80,
	WORLD_SDF_FILE_CRC = 4,
	/* the deflate's level */
	WORLD_SDF_DEFLATE_LEVEL = 6,
	/* the most threads a bake runs on while a game is played (its passes
	begun then): the game's threads have the processors */
	WORLD_SDF_PLAY_THREADS = 2,
};

/* the fade in, in nanoseconds */
#define WORLD_SDF_FADE_NANOSECONDS 1000000000ULL
/* a resting thread's nap between looks at the pace, and the time without a
frame after which it runs whatever the pace last said */
#define WORLD_SDF_PACE_NAP_MS 4
#define WORLD_SDF_PACE_SILENCE_NS 250000000ULL

/* a bake's passes on its threads: a layout's cells marked, the marked
cells' triangles counted, then put, slice by slice; the bricks baked */
enum
{
	_sdf_pass_mark,
	_sdf_pass_count,
	_sdf_pass_fill,
	_sdf_pass_bake,
	_sdf_pass_scenery,
	_sdf_pass_objects,
};

/* what a job is, and what came of it */
enum
{
	_sdf_job_look, /* the cache looked in */
	_sdf_job_bake, /* baked from the copies */
};

enum
{
	_sdf_result_found,
	_sdf_result_missing,
	_sdf_result_baked,
	_sdf_result_failed,
};

/* ---------- prototypes */

/* the platform layer's */
const char *platform_data_root(void);
void platform_log(char const *format, ...);
void halo_load_trace(char const *phase, char const *detail);
int config_boolean(const char *name);
double config_real(const char *name);
const char *config_string(const char *name);
int halo_server_mode(void);
unsigned long long SDL_GetTicksNS(void);
void SDL_Delay(unsigned int milliseconds);
/* the guest runtime's system calls (port/android/guest/runtime) */
long __guest_syscall(long long number, long long a, long long b, long long c, long long d, long long e, long long f);
/* (sys/stat.h's, which the game's strict ANSI hides; not translated, so
given the folder's whole path) */
int mkdir(const char *path, unsigned int mode);
/* the port's zlib (port/third_party/zlib/zlib_prefixed.h: its inflate,
for data from anywhere, whose allocator is the C library's) */
int z_uncompress(unsigned char *destination, unsigned long *destination_size, const unsigned char *source,
	unsigned long source_size);

/* ---------- structures */

/* a copy of a collision BSP's bsp3d: its nodes (plane, back child, front
child) and planes (i j k d), for its point test on another thread */
struct sdf_bsp
{
	long node_count, plane_count;
	long (*nodes)[3];
	float (*planes)[4];
};

/* a static scenery placement's node's solid, for the sign: its collision
BSP's point test (a copy) and the transform from the world into its space
(x y z of a point: to_local's rows by (point, 1)), within a sphere */
struct sdf_solid
{
	struct sdf_bsp bsp;
	float to_local[3][4];
	float centre[3], radius;
};

/* a lightmap page's copies: its triangles (WORLD_AO_SCENERY_TRIANGLE floats
each, none NULL), their bounds; whether it can be baked (FALSE: a lightmap
whose vertices are not its surfaces', which draws with the shapes walked),
and once baked, its texels, how many its surfaces covered and shared */
struct sdf_page
{
	float *triangles;
	long triangle_count;
	float bounds[6];
	int usable, reached;
	unsigned char *texels;
	long covered, shared;
};

/* an object's copies for its occlusion field (docs/world-ao.md, "The
objects' occlusion fields"): its definition, its triangles (a vehicle's
collision but its wheels', a piece of scenery's drawn ones; in node 0's
space, at rest: the field rides on node 0), each one's opacity (NULL: all
opaque) and their bounds, its nodes' solids (the sign's), and the bake's
grid, values and solid voxels */
struct sdf_object_copy
{
	long definition;
	float *positions;
	float *opacities;
	long triangle_count, opacities_room;
	float bounds[6];
	int bounded, scenery;
	struct sdf_solid *solids;
	long solid_count;
	struct world_sdf_object field;
	float *values;
	unsigned char *solid;
	/* (logged: its tag's name, and its slices' time on the threads) */
	char name[128];
	unsigned long long nanoseconds;
	/* its field found among those kept by their geometry (another map's,
	sdf_object_cache_load): not baked */
	int cached;
};

/* a field's name: its map (and that map's checksum) and BSP */
struct sdf_key
{
	char map[64];
	unsigned long checksum;
	short bsp;
};

struct sdf_job
{
	struct sdf_job *next;
	int kind;
	struct sdf_key key;
	/* a bake's copies: the triangles, their bounds cut to where objects can
	be (the placements' reach), and the collision BSP's point test */
	float *positions;
	long triangle_count;
	float bounds[6];
	struct sdf_bsp bsp;
	/* the static scenery's copies (its triangles are in positions, after
	the structure's): its placements, their solids (the sign's), their
	shapes as placed, and the BSP's lightmap pages (page_count; none -1: no
	lightmaps), the side its pages are baked at, and the next a worker takes */
	struct world_sdf_placement *placements;
	long placement_count;
	/* (each placement's node 0 as placed, and its first shape: its own
	are those to the next one's first) */
	struct world_proxies_frame *placement_frames;
	long *placement_shapes;
	struct sdf_solid *solids;
	long solid_count;
	struct world_proxy *shapes;
	long shape_count;
	struct sdf_page *pages;
	long page_count;
	unsigned long page_side;
	/* the objects' copies (their occlusion fields'), and the opacities of
the shaders they are drawn with, by tag (sdf_shader_opacity) */
	struct sdf_object_copy *object_copies;
	long object_count;
	/* (logged) how many of their fields were found kept by their geometry */
	long objects_cached;
	long opacity_tags[64];
	float opacities[64];
	long opacity_count;
	/* (their slices' directions and weights, made once: a slice allocates
	nothing) */
	struct world_sdf_object_tables *object_tables;
	/* the bake's field; its plan (the cells marked, and each marked cell's
	triangles: world_sdf_bin), the marked cells in order and their bricks
	as baked, each cell's kind, and the next of them (the marked cells,
	then every cell) a worker takes */
	struct world_sdf_volume volume;
	unsigned char *marks;
	long *firsts, *triangles;
	unsigned long *marked, marked_count;
	signed char *baked;
	unsigned char *kinds;
	unsigned long next_item;
	/* the pass the workers do (_sdf_pass_*): its layout and marks, the
	cells it has marked, and the next slice a worker takes */
	int pass;
	struct world_sdf_header const *pass_header;
	unsigned char *pass_marks;
	long pass_marked;
	unsigned long next_slice;
	/* what came of it; the game's thread's time copying the geometry; one
	the start-up pass asked for */
	int result, startup;
	double copy_milliseconds;
};

/* ---------- globals */

/* (the threads' and the game's) */
static pthread_mutex_t world_sdf_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t world_sdf_look_wake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t world_sdf_bake_wake = PTHREAD_COND_INITIALIZER;

static struct
{
	/* (under world_sdf_lock) the threads running, the jobs waiting, the
	bake running (its key), and the results for the game */
	int looker_running, baker_running;
	struct sdf_job *looks, *bakes;
	int baking;
	/* the start-up pass runs (menu_pictures_preparing, to the main menu
	shown): bakes wait, so that the pass and its main menu come exactly when
	they would without them */
	int hold;
	struct sdf_key baking_key;
	struct sdf_job *results;
	/* (under world_sdf_lock, the game's thread's as each frame takes the
	lock) "world" off, a game played, and the field drawn (wanted_known: a
	map loaded): a bake runs only while it is on, and in a game only the
	field drawn is baked; another baking stops for it (sdf_yield) and waits
	behind it, its copies kept, until the game is left (the main menu's
	idle time) */
	int off, playing, wanted_known;
	struct sdf_key wanted;

	/* (the game's) the map and BSP drawn, and its field */
	int map_known;
	struct sdf_key current;
	int state;
	/* its geometry to be copied for a bake, so many frames from now */
	int copy_wanted;
	long copy_frames;
	struct world_sdf_volume field;
	/* how much of it has been uploaded: its table's z slices, then its
	atlas's layers (one at a time, made in layer) */
	unsigned long uploaded_pages, uploaded_cells, uploaded_layers;
	signed char *layer;
	/* (and before the table, the objects' fields' atlas's z slices, each
	made in objects_slice) */
	unsigned long uploaded_objects;
	unsigned char *objects_slice;
	int field_sent;
	unsigned long long faded_from;
	float fade;
	/* the static scenery placements its field will hold, while it is on
	its way (looked for, baking or uploading): in order of x, then y
	(sdf_placement_order); made the first frame it is awaited */
	struct world_sdf_placement *awaited;
	unsigned long awaited_count;
	int awaited_made;
	/* and the vehicles' definitions that it will hold occlusion fields of */
	long awaited_objects[WORLD_SDF_OBJECT_MAXIMUM];
	long awaited_object_count;
	/* the start-up pass: the fields it has tried, and whether any is
	missing (once looked) */
	struct sdf_key tried[WORLD_SDF_MAXIMUM_TRIED];
	long tried_count;
	int startup_looked, startup_wanted;
	/* (under world_sdf_lock) the start-up pass's bakes done and their
	milliseconds, logged with the pass's total once the last is */
	unsigned long baked_milliseconds, baked_count;
	long startup_bakes;
	int startup_logged;
} world_sdf;

/* ---------- the cache file (tools/harness/tests/world_sdf.c takes these) */

/* the game's zlib's deflate (its compress2 allocates with the game's own
allocator, which only the game's thread may call: the bake's thread
deflates with the C library's) */
static voidpf sdf_zalloc(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	return calloc(items, size);
}

static void sdf_zfree(voidpf opaque, voidpf address)
{
	(void)opaque;
	free(address);
}

static int sdf_deflate(unsigned char *destination, unsigned long *destination_size, const unsigned char *source,
	unsigned long source_size)
{
	z_stream stream;
	int result;

	memset(&stream, 0, sizeof(stream));
	stream.zalloc = sdf_zalloc;
	stream.zfree = sdf_zfree;
	if (deflateInit(&stream, WORLD_SDF_DEFLATE_LEVEL) != Z_OK)
		return -1;
	stream.next_in = (Bytef *)source;
	stream.avail_in = (uInt)source_size;
	stream.next_out = destination;
	stream.avail_out = (uInt)*destination_size;
	result = deflate(&stream, Z_FINISH);
	*destination_size = stream.total_out;
	deflateEnd(&stream);
	return result == Z_STREAM_END ? 0 : -1;
}

static void sdf_put32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static unsigned long sdf_get32(unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

static void sdf_put_float(unsigned char *bytes, float value)
{
	unsigned int bits;

	memcpy(&bits, &value, sizeof(bits));
	sdf_put32(bytes, bits);
}

static float sdf_get_float(unsigned char const *bytes)
{
	unsigned int bits = (unsigned int)sdf_get32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* the CRC-32 (zlib's and PNG's, as the menu pictures check theirs) */
static unsigned long sdf_crc(unsigned long crc, unsigned char const *bytes, unsigned long size)
{
	static unsigned long table[256];
	unsigned long index;

	if (!table[1])
	{
		for (index = 0; index < 256; index++)
		{
			unsigned long value = index;
			int bit;

			for (bit = 0; bit < 8; bit++)
				value = value & 1 ? 0xEDB88320UL ^ (value >> 1) : value >> 1;
			table[index] = value;
		}
	}
	crc ^= 0xFFFFFFFFUL;
	for (index = 0; index < size; index++)
		crc = table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
	return (crc ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL;
}

/* the header, field by field (struct world_sdf_header's order) */
static void sdf_header_write(struct world_sdf_header const *header, unsigned char *bytes)
{
	sdf_put32(bytes + 0, header->magic);
	sdf_put32(bytes + 4, header->version);
	sdf_put32(bytes + 8, header->map_checksum);
	sdf_put32(bytes + 12, header->bsp_index);
	sdf_put32(bytes + 16, header->width);
	sdf_put32(bytes + 20, header->height);
	sdf_put32(bytes + 24, header->depth);
	sdf_put_float(bytes + 28, header->origin[0]);
	sdf_put_float(bytes + 32, header->origin[1]);
	sdf_put_float(bytes + 36, header->origin[2]);
	sdf_put_float(bytes + 40, header->voxel);
	sdf_put_float(bytes + 44, header->range);
	sdf_put32(bytes + 48, header->deflated_size);
	sdf_put32(bytes + 52, header->bake_milliseconds);
	sdf_put32(bytes + 56, header->bake_threads);
	sdf_put32(bytes + 60, header->brick_count);
	sdf_put32(bytes + 64, header->scenery_bytes);
	sdf_put32(bytes + 68, header->scenery_milliseconds);
	sdf_put32(bytes + 72, header->objects_bytes);
	sdf_put32(bytes + 76, header->objects_milliseconds);
}

static void sdf_header_read(unsigned char const *bytes, struct world_sdf_header *header)
{
	header->magic = sdf_get32(bytes + 0);
	header->version = sdf_get32(bytes + 4);
	header->map_checksum = sdf_get32(bytes + 8);
	header->bsp_index = sdf_get32(bytes + 12);
	header->width = sdf_get32(bytes + 16);
	header->height = sdf_get32(bytes + 20);
	header->depth = sdf_get32(bytes + 24);
	header->origin[0] = sdf_get_float(bytes + 28);
	header->origin[1] = sdf_get_float(bytes + 32);
	header->origin[2] = sdf_get_float(bytes + 36);
	header->voxel = sdf_get_float(bytes + 40);
	header->range = sdf_get_float(bytes + 44);
	header->deflated_size = sdf_get32(bytes + 48);
	header->bake_milliseconds = sdf_get32(bytes + 52);
	header->bake_threads = sdf_get32(bytes + 56);
	header->brick_count = sdf_get32(bytes + 60);
	header->scenery_bytes = sdf_get32(bytes + 64);
	header->scenery_milliseconds = sdf_get32(bytes + 68);
	header->objects_bytes = sdf_get32(bytes + 72);
	header->objects_milliseconds = sdf_get32(bytes + 76);
}

/* ---------- the static scenery's part (world_sdf.h's struct world_sdf_scenery) */

/* the most bytes a part may have */
static unsigned long sdf_scenery_most_bytes(void)
{
	return WORLD_SDF_SCENERY_COUNTS + WORLD_SDF_SCENERY_MAXIMUM_PAGES * 2 +
		WORLD_SDF_SCENERY_MAXIMUM_PLACEMENTS * WORLD_SDF_SCENERY_PLACEMENT + WORLD_SDF_SCENERY_MAXIMUM_BYTES;
}

/* a part's bytes (none: 0) */
static unsigned long sdf_scenery_size(struct world_sdf_scenery const *scenery)
{
	if (!scenery->page_count && !scenery->placement_count)
		return 0;
	return WORLD_SDF_SCENERY_COUNTS + scenery->page_count * 2 + scenery->placement_count * WORLD_SDF_SCENERY_PLACEMENT +
		scenery->layer_count * scenery->width * scenery->height;
}

static void sdf_scenery_free(struct world_sdf_scenery *scenery)
{
	free(scenery->layers);
	free(scenery->placements);
	free(scenery->texels);
	memset(scenery, 0, sizeof(*scenery));
}

static void sdf_scenery_write(struct world_sdf_scenery const *scenery, unsigned char *bytes)
{
	unsigned long index, at = WORLD_SDF_SCENERY_COUNTS;

	sdf_put32(bytes + 0, scenery->page_count);
	sdf_put32(bytes + 4, scenery->layer_count);
	sdf_put32(bytes + 8, scenery->width);
	sdf_put32(bytes + 12, scenery->height);
	sdf_put32(bytes + 16, scenery->placement_count);
	for (index = 0; index < scenery->page_count; index++, at += 2)
	{
		bytes[at] = (unsigned char)((unsigned short)scenery->layers[index] & 0xFF);
		bytes[at + 1] = (unsigned char)((unsigned short)scenery->layers[index] >> 8);
	}
	for (index = 0; index < scenery->placement_count; index++, at += WORLD_SDF_SCENERY_PLACEMENT)
	{
		sdf_put32(bytes + at, (unsigned long)scenery->placements[index].definition);
		sdf_put_float(bytes + at + 4, scenery->placements[index].position[0]);
		sdf_put_float(bytes + at + 8, scenery->placements[index].position[1]);
		sdf_put_float(bytes + at + 12, scenery->placements[index].position[2]);
	}
	if (scenery->layer_count)
		memcpy(bytes + at, scenery->texels, scenery->layer_count * scenery->width * scenery->height);
}

/* a part from its bytes, if its counts, sizes and layers hold (malloc's);
FALSE if not */
static int sdf_scenery_read(unsigned char const *bytes, unsigned long size, struct world_sdf_scenery *scenery)
{
	unsigned long index, at = WORLD_SDF_SCENERY_COUNTS, texels;

	memset(scenery, 0, sizeof(*scenery));
	if (!size)
		return 1;
	if (size < WORLD_SDF_SCENERY_COUNTS)
		return 0;
	scenery->page_count = sdf_get32(bytes + 0);
	scenery->layer_count = sdf_get32(bytes + 4);
	scenery->width = sdf_get32(bytes + 8);
	scenery->height = sdf_get32(bytes + 12);
	scenery->placement_count = sdf_get32(bytes + 16);
	if (scenery->page_count > WORLD_SDF_SCENERY_MAXIMUM_PAGES ||
		scenery->placement_count > WORLD_SDF_SCENERY_MAXIMUM_PLACEMENTS ||
		scenery->width > WORLD_SDF_SCENERY_MOST_SIDE || scenery->height > WORLD_SDF_SCENERY_MOST_SIDE ||
		(scenery->layer_count && (scenery->width < WORLD_SDF_SCENERY_LEAST_SIDE ||
		scenery->height < WORLD_SDF_SCENERY_LEAST_SIDE)) || scenery->layer_count > scenery->page_count + 1 ||
		scenery->layer_count * scenery->width * scenery->height > WORLD_SDF_SCENERY_MAXIMUM_BYTES)
	{
		memset(scenery, 0, sizeof(*scenery));
		return 0;
	}
	texels = scenery->layer_count * scenery->width * scenery->height;
	if (size != sdf_scenery_size(scenery))
	{
		memset(scenery, 0, sizeof(*scenery));
		return 0;
	}
	scenery->layers = malloc(scenery->page_count * sizeof(*scenery->layers) + 1);
	scenery->placements = malloc(scenery->placement_count * sizeof(*scenery->placements) + 1);
	scenery->texels = malloc(texels + 1);
	if (!scenery->layers || !scenery->placements || !scenery->texels)
	{
		sdf_scenery_free(scenery);
		return 0;
	}
	for (index = 0; index < scenery->page_count; index++, at += 2)
	{
		short layer = (short)(unsigned short)(bytes[at] | bytes[at + 1] << 8);

		if (layer < WORLD_SDF_SCENERY_NO_LAYER || layer >= (long)scenery->layer_count)
		{
			sdf_scenery_free(scenery);
			return 0;
		}
		scenery->layers[index] = layer;
	}
	for (index = 0; index < scenery->placement_count; index++, at += WORLD_SDF_SCENERY_PLACEMENT)
	{
		scenery->placements[index].definition = (long)sdf_get32(bytes + at);
		scenery->placements[index].position[0] = sdf_get_float(bytes + at + 4);
		scenery->placements[index].position[1] = sdf_get_float(bytes + at + 8);
		scenery->placements[index].position[2] = sdf_get_float(bytes + at + 12);
	}
	memcpy(scenery->texels, bytes + at, texels);
	return 1;
}

/* how many pages have a layer, and the layers' GPU bytes */
static unsigned long sdf_pages_layered(struct world_sdf_scenery const *scenery)
{
	unsigned long index, count = 0;

	for (index = 0; index < scenery->page_count; index++)
		count += scenery->layers[index] != WORLD_SDF_SCENERY_NO_LAYER;
	return count;
}

static unsigned long sdf_scenery_gpu_bytes(struct world_sdf_scenery const *scenery)
{
	return scenery->layer_count * scenery->width * scenery->height;
}

/* ---------- the objects' occlusion fields' part (world_sdf.h's struct world_sdf_objects) */

static unsigned long sdf_objects_most_bytes(void)
{
	return 4 + WORLD_SDF_OBJECT_MAXIMUM * (WORLD_SDF_OBJECT_ENTRY +
		(unsigned long)WORLD_SDF_OBJECT_MOST_VOXELS * WORLD_SDF_OBJECT_TEXEL);
}

static unsigned long sdf_object_voxels(struct world_sdf_object const *object)
{
	return object->width * object->height * object->depth;
}

/* a part's bytes (none: 0) */
static unsigned long sdf_objects_size(struct world_sdf_objects const *objects)
{
	unsigned long size = 4 + objects->count * WORLD_SDF_OBJECT_ENTRY, index;

	if (!objects->count)
		return 0;
	for (index = 0; index < objects->count; index++)
		size += sdf_object_voxels(&objects->objects[index]) * WORLD_SDF_OBJECT_TEXEL;
	return size;
}

static void sdf_objects_free(struct world_sdf_objects *objects)
{
	unsigned long index;

	for (index = 0; objects->objects && index < objects->count; index++)
		free(objects->objects[index].texels);
	free(objects->objects);
	memset(objects, 0, sizeof(*objects));
}

static void sdf_objects_write(struct world_sdf_objects const *objects, unsigned char *bytes)
{
	unsigned long index, at = 4, texel;

	sdf_put32(bytes, objects->count);
	for (index = 0; index < objects->count; index++, at += WORLD_SDF_OBJECT_ENTRY)
	{
		struct world_sdf_object const *object = &objects->objects[index];

		sdf_put32(bytes + at, (unsigned long)object->definition);
		sdf_put32(bytes + at + 4, object->width);
		sdf_put32(bytes + at + 8, object->height);
		sdf_put32(bytes + at + 12, object->depth);
		sdf_put_float(bytes + at + 16, object->origin[0]);
		sdf_put_float(bytes + at + 20, object->origin[1]);
		sdf_put_float(bytes + at + 24, object->origin[2]);
		sdf_put_float(bytes + at + 28, object->voxel);
		sdf_put_float(bytes + at + 32, object->reach);
		for (texel = 0; texel < 6; texel++)
			sdf_put_float(bytes + at + 36 + texel * 4, object->bounds[texel]);
	}
	for (index = 0; index < objects->count; index++)
	{
		struct world_sdf_object const *object = &objects->objects[index];

		for (texel = 0; texel < sdf_object_voxels(object) * 4; texel++, at += 2)
		{
			bytes[at] = (unsigned char)(object->texels[texel] & 0xFF);
			bytes[at + 1] = (unsigned char)(object->texels[texel] >> 8);
		}
	}
}

/* a number a field may hold: finite, of a size a world has */
static int sdf_finite(float value, float most)
{
	return value == value && value >= -most && value <= most;
}

/* a part from its bytes, if its counts, sizes and values hold (malloc's),
its objects put in the GPU's atlas; FALSE if not */
static int sdf_objects_read(unsigned char const *bytes, unsigned long size, struct world_sdf_objects *objects)
{
	unsigned long index, at = 4, texel, expected;

	memset(objects, 0, sizeof(*objects));
	if (!size)
		return 1;
	if (size < 4)
		return 0;
	objects->count = sdf_get32(bytes);
	if (!objects->count || objects->count > WORLD_SDF_OBJECT_MAXIMUM || size < 4 + objects->count * WORLD_SDF_OBJECT_ENTRY)
	{
		objects->count = 0;
		return 0;
	}
	objects->objects = calloc(objects->count, sizeof(*objects->objects));
	if (!objects->objects)
	{
		objects->count = 0;
		return 0;
	}
	expected = 4 + objects->count * WORLD_SDF_OBJECT_ENTRY;
	for (index = 0; index < objects->count; index++, at += WORLD_SDF_OBJECT_ENTRY)
	{
		struct world_sdf_object *object = &objects->objects[index];

		object->definition = (long)sdf_get32(bytes + at);
		object->width = sdf_get32(bytes + at + 4);
		object->height = sdf_get32(bytes + at + 8);
		object->depth = sdf_get32(bytes + at + 12);
		object->origin[0] = sdf_get_float(bytes + at + 16);
		object->origin[1] = sdf_get_float(bytes + at + 20);
		object->origin[2] = sdf_get_float(bytes + at + 24);
		object->voxel = sdf_get_float(bytes + at + 28);
		object->reach = sdf_get_float(bytes + at + 32);
		for (texel = 0; texel < 6; texel++)
			object->bounds[texel] = sdf_get_float(bytes + at + 36 + texel * 4);
		if (!sdf_finite(object->bounds[0], 1.0e5f) || !sdf_finite(object->bounds[1], 1.0e5f) ||
			!sdf_finite(object->bounds[2], 1.0e5f) || !sdf_finite(object->bounds[3], 1.0e5f) ||
			!sdf_finite(object->bounds[4], 1.0e5f) || !sdf_finite(object->bounds[5], 1.0e5f) ||
			object->bounds[3] < object->bounds[0] || object->bounds[4] < object->bounds[1] ||
			object->bounds[5] < object->bounds[2])
		{
			sdf_objects_free(objects);
			return 0;
		}
		if (!object->width || !object->height || !object->depth || object->width > WORLD_SDF_OBJECT_MOST_SIDE ||
			object->height > WORLD_SDF_OBJECT_MOST_SIDE || object->depth > WORLD_SDF_OBJECT_MOST_SIDE ||
			sdf_object_voxels(object) > WORLD_SDF_OBJECT_MOST_VOXELS || !sdf_finite(object->origin[0], 1.0e5f) ||
			!sdf_finite(object->origin[1], 1.0e5f) || !sdf_finite(object->origin[2], 1.0e5f) ||
			!(object->voxel > 0.0f) || !sdf_finite(object->voxel, 1000.0f) || !(object->reach >= 0.0f) ||
			!sdf_finite(object->reach, 1.0e6f))
		{
			sdf_objects_free(objects);
			return 0;
		}
		expected += sdf_object_voxels(object) * WORLD_SDF_OBJECT_TEXEL;
	}
	if (size != expected)
	{
		sdf_objects_free(objects);
		return 0;
	}
	for (index = 0; index < objects->count; index++)
	{
		struct world_sdf_object *object = &objects->objects[index];
		unsigned long halves = sdf_object_voxels(object) * 4;

		object->texels = malloc(halves * sizeof(*object->texels));
		if (!object->texels)
		{
			sdf_objects_free(objects);
			return 0;
		}
		for (texel = 0; texel < halves; texel++, at += 2)
		{
			object->texels[texel] = (unsigned short)(bytes[at] | bytes[at + 1] << 8);
			/* (no infinity, no NaN) */
			if ((object->texels[texel] & 0x7C00u) == 0x7C00u)
			{
				sdf_objects_free(objects);
				return 0;
			}
		}
	}
	world_sdf_objects_place(objects);
	return 1;
}

static unsigned long sdf_objects_gpu_bytes(struct world_sdf_objects const *objects)
{
	return objects->atlas_width * objects->atlas_height * objects->atlas_depth * WORLD_SDF_OBJECT_TEXEL;
}

/* whether a header is one of this version's, of the map and BSP, of a
size a field may have */
static int sdf_header_good(struct world_sdf_header const *header, unsigned long checksum, unsigned long bsp_index)
{
	return header->magic == WORLD_SDF_MAGIC && header->version == WORLD_SDF_VERSION &&
		header->map_checksum == checksum && header->bsp_index == bsp_index && header->width && header->height &&
		header->depth && header->width <= WORLD_SDF_MAXIMUM_DIMENSION && header->height <= WORLD_SDF_MAXIMUM_DIMENSION &&
		header->depth <= WORLD_SDF_MAXIMUM_DIMENSION && header->brick_count <= WORLD_SDF_MAXIMUM_BYTES &&
		world_sdf_bytes(header, header->brick_count) <= WORLD_SDF_MAXIMUM_BYTES &&
		header->brick_count <= header->width * header->height * header->depth &&
		header->voxel > 0.0f && header->voxel < 1000.0f &&
		header->range == WORLD_SDF_RANGE_VOXELS * header->voxel &&
		header->scenery_bytes <= sdf_scenery_most_bytes() && header->objects_bytes <= sdf_objects_most_bytes();
}

/* a field's cells' and bricks' bytes */
static unsigned long sdf_cells_size(struct world_sdf_header const *header)
{
	return header->width * header->height * header->depth * WORLD_SDF_CELL_BYTES;
}

static unsigned long sdf_bricks_size(struct world_sdf_header const *header)
{
	return header->brick_count * WORLD_SDF_BRICK_VOXELS;
}

/* whether each cell's kind is one, and each brick's place one of the
bricks' */
static int sdf_cells_good(struct world_sdf_volume const *volume)
{
	unsigned long cells = volume->header.width * volume->header.height * volume->header.depth, cell;

	for (cell = 0; cell < cells; cell++)
	{
		unsigned char const *entry = volume->cells[cell];

		if (entry[3] > WORLD_SDF_CELL_BRICK || (entry[3] == WORLD_SDF_CELL_BRICK &&
			(entry[0] >= WORLD_SDF_ATLAS || entry[1] >= WORLD_SDF_ATLAS ||
			(unsigned long)entry[0] + (unsigned long)entry[1] * WORLD_SDF_ATLAS +
			(unsigned long)entry[2] * WORLD_SDF_ATLAS_LAYER >= volume->header.brick_count)))
		{
			return 0;
		}
	}
	return 1;
}

static void sdf_volume_free(struct world_sdf_volume *volume)
{
	free(volume->cells);
	free(volume->bricks);
	volume->cells = NULL;
	volume->bricks = NULL;
	sdf_scenery_free(&volume->scenery);
	sdf_objects_free(&volume->objects);
}

/* a field's file, whole (malloc's: its size), the header's deflated size
set; NULL if it cannot be made */
static unsigned char *sdf_file_pack(struct world_sdf_volume *volume, unsigned long *size)
{
	unsigned long cells = sdf_cells_size(&volume->header), bricks = sdf_bricks_size(&volume->header);
	unsigned long scenery = sdf_scenery_size(&volume->scenery), objects = sdf_objects_size(&volume->objects);
	unsigned long count = cells + bricks + scenery + objects, deflated = count + count / 1000 + 64;
	unsigned char *bytes = malloc(WORLD_SDF_FILE_HEADER + deflated + WORLD_SDF_FILE_CRC);
	unsigned char *payload = malloc(count + 1);
	int made;

	if (!bytes || !payload)
	{
		free(bytes);
		free(payload);
		return NULL;
	}
	memcpy(payload, volume->cells, cells);
	if (bricks)
		memcpy(payload + cells, volume->bricks, bricks);
	if (scenery)
		sdf_scenery_write(&volume->scenery, payload + cells + bricks);
	if (objects)
		sdf_objects_write(&volume->objects, payload + cells + bricks + scenery);
	volume->header.scenery_bytes = scenery;
	volume->header.objects_bytes = objects;
	made = sdf_deflate(bytes + WORLD_SDF_FILE_HEADER, &deflated, payload, count) == 0;
	free(payload);
	if (!made)
	{
		free(bytes);
		return NULL;
	}
	volume->header.deflated_size = deflated;
	sdf_header_write(&volume->header, bytes);
	sdf_put32(bytes + WORLD_SDF_FILE_HEADER + deflated, sdf_crc(0, bytes, WORLD_SDF_FILE_HEADER + deflated));
	*size = WORLD_SDF_FILE_HEADER + deflated + WORLD_SDF_FILE_CRC;
	return bytes;
}

/* a field from its file's bytes, if every check passes (its cells and
bricks malloc's); FALSE if not */
static int sdf_file_unpack(unsigned char const *bytes, unsigned long size, unsigned long checksum,
	unsigned long bsp_index, struct world_sdf_volume *volume)
{
	unsigned long cells, bricks, scenery, objects, inflated;
	unsigned char *payload;

	volume->cells = NULL;
	volume->bricks = NULL;
	memset(&volume->scenery, 0, sizeof(volume->scenery));
	memset(&volume->objects, 0, sizeof(volume->objects));
	if (size < WORLD_SDF_FILE_HEADER + WORLD_SDF_FILE_CRC)
		return 0;
	sdf_header_read(bytes, &volume->header);
	if (!sdf_header_good(&volume->header, checksum, bsp_index) ||
		volume->header.deflated_size != size - WORLD_SDF_FILE_HEADER - WORLD_SDF_FILE_CRC ||
		sdf_get32(bytes + size - WORLD_SDF_FILE_CRC) != sdf_crc(0, bytes, size - WORLD_SDF_FILE_CRC))
	{
		return 0;
	}
	cells = sdf_cells_size(&volume->header);
	bricks = sdf_bricks_size(&volume->header);
	scenery = volume->header.scenery_bytes;
	objects = volume->header.objects_bytes;
	payload = malloc(cells + bricks + scenery + objects + 1);
	volume->cells = malloc(cells);
	volume->bricks = malloc(bricks + 1);
	inflated = cells + bricks + scenery + objects;
	if (!payload || !volume->cells || !volume->bricks ||
		z_uncompress(payload, &inflated, bytes + WORLD_SDF_FILE_HEADER, volume->header.deflated_size) != 0 ||
		inflated != cells + bricks + scenery + objects ||
		!sdf_scenery_read(payload + cells + bricks, scenery, &volume->scenery) ||
		!sdf_objects_read(payload + cells + bricks + scenery, objects, &volume->objects))
	{
		free(payload);
		sdf_volume_free(volume);
		return 0;
	}
	memcpy(volume->cells, payload, cells);
	memcpy(volume->bricks, payload + cells, bricks);
	free(payload);
	if (!sdf_cells_good(volume))
	{
		sdf_volume_free(volume);
		return 0;
	}
	return 1;
}

/* a file written whole or not at all (a .tmp beside it, renamed over it) */
static int sdf_file_write(char const *path, unsigned char const *bytes, unsigned long size)
{
	char temporary[1100];
	FILE *file;
	int written;

	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	written = fwrite(bytes, 1, size, file) == size;
	written = fclose(file) == 0 && written;
	/* (Windows renames over nothing) */
	if (written)
	{
		remove(path);
		written = rename(temporary, path) == 0;
	}
	if (!written)
		remove(temporary);
	return written;
}

/* a whole file (malloc's), at most maximum bytes; NULL if there is none */
static unsigned char *sdf_file_read(char const *path, unsigned long maximum, unsigned long *size)
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
		free(bytes);
		return NULL;
	}
	fclose(file);
	*size = (unsigned long)length;
	return bytes;
}

/* a cached field: 1 found (its cells and bricks malloc's), 0 none; a file
there that fails a check is deleted, to be baked again */
static int sdf_cache_load(char const *path, unsigned long checksum, unsigned long bsp_index,
	struct world_sdf_volume *volume)
{
	unsigned long size = 0;
	/* (a field's cells and bricks are at most its GPU memory's bytes) */
	unsigned long most = WORLD_SDF_MAXIMUM_BYTES + sdf_scenery_most_bytes() + sdf_objects_most_bytes();
	unsigned char *bytes = sdf_file_read(path, WORLD_SDF_FILE_HEADER + most + most / 1000 + 64 + WORLD_SDF_FILE_CRC,
		&size);
	int found;

	volume->cells = NULL;
	volume->bricks = NULL;
	if (!bytes)
	{
		/* (a file there too large to be one) */
		FILE *file = fopen(path, "rb");

		if (file)
		{
			fclose(file);
			remove(path);
			platform_log("world sdf: %s is not a field: deleted, to be baked again", path);
		}
		return 0;
	}
	found = sdf_file_unpack(bytes, size, checksum, bsp_index, volume);
	free(bytes);
	if (!found)
	{
		remove(path);
		platform_log("world sdf: %s is damaged, of another version or another map: deleted, to be baked again",
			path);
	}
	return found;
}

/* whether a cached field's header is this version's, of the map and BSP
(the voxels not read) */
static int sdf_cache_present(char const *path, unsigned long checksum, unsigned long bsp_index)
{
	unsigned char bytes[WORLD_SDF_FILE_HEADER];
	struct world_sdf_header header;
	FILE *file = fopen(path, "rb");
	int present;

	if (!file)
		return 0;
	present = fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
	fclose(file);
	if (!present)
		return 0;
	sdf_header_read(bytes, &header);
	return sdf_header_good(&header, checksum, bsp_index);
}

/* ---------- the sign: a copy of the collision BSP's point test */

/* bsp3d_test_point's walk (source/physics/bsp3d.c) on the copy: TRUE where
it ends in no leaf (solid); a node that is none, or past the walk's
depth, is solid as there */
static int sdf_bsp_inside(void *context, float const point[3])
{
	struct sdf_bsp const *bsp = context;
	long node_index = 0;
	int depth = 0;

	if (!bsp || bsp->node_count <= 0)
		return 0;
	do
	{
		long const *node;
		float const *plane;

		if (node_index < 0 || node_index >= bsp->node_count || depth++ >= 128)
			return 1;
		node = bsp->nodes[node_index];
		if (node[0] < 0 || node[0] >= bsp->plane_count)
			return 1;
		plane = bsp->planes[node[0]];
		node_index = node[1 + (plane[0] * point[0] + plane[1] * point[1] + plane[2] * point[2] - plane[3] >= 0.0f)];
	}
	while (node_index >= 0);
	return node_index == -1;
}

/* whether a point is in solid: the structure's, or a static scenery
placement's (each within its sphere, by its own collision BSP's point test
in its space) */
static int sdf_job_inside(void *context, float const point[3])
{
	struct sdf_job const *job = context;
	long index;

	if (sdf_bsp_inside((void *)&job->bsp, point))
		return 1;
	for (index = 0; index < job->solid_count; index++)
	{
		struct sdf_solid const *solid = &job->solids[index];
		float offset[3], local[3];
		int axis;

		for (axis = 0; axis < 3; axis++)
			offset[axis] = point[axis] - solid->centre[axis];
		if (offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2] > solid->radius * solid->radius)
			continue;
		for (axis = 0; axis < 3; axis++)
			local[axis] = solid->to_local[axis][0] * point[0] + solid->to_local[axis][1] * point[1] +
				solid->to_local[axis][2] * point[2] + solid->to_local[axis][3];
		if (sdf_bsp_inside((void *)&solid->bsp, local))
			return 1;
	}
	return 0;
}

/* whether a point (node 0's space) is in a vehicle's solid: any of its
nodes' (each within its sphere, by its own collision BSP's point test in its
space) */
static int sdf_object_inside(void *context, float const point[3])
{
	struct sdf_object_copy const *copy = context;
	long index;

	for (index = 0; index < copy->solid_count; index++)
	{
		struct sdf_solid const *solid = &copy->solids[index];
		float offset[3], local[3];
		int axis;

		for (axis = 0; axis < 3; axis++)
			offset[axis] = point[axis] - solid->centre[axis];
		if (offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2] > solid->radius * solid->radius)
			continue;
		for (axis = 0; axis < 3; axis++)
			local[axis] = solid->to_local[axis][0] * point[0] + solid->to_local[axis][1] * point[1] +
				solid->to_local[axis][2] * point[2] + solid->to_local[axis][3];
		if (sdf_bsp_inside((void *)&solid->bsp, local))
			return 1;
	}
	return 0;
}

/* ---------- names and keys */

static int sdf_off(void)
{
	return !(world_ao_mode() & WORLD_AO_FIELD) || config_boolean("debug.null_renderer") || halo_server_mode();
}

static char const *sdf_folder(void)
{
	static char folder[1024];

	if (!folder[0])
		snprintf(folder, sizeof(folder), "%s/world_sdf", platform_data_root());
	return folder;
}

/* <data>/world_sdf/<map>_<bsp>.wsdf, named as the game names its files,
from the data root (the game's fopen, remove and rename find them there:
port/linux/include/stdio.h); the map's name kept to letters, digits, - and
_ (a Custom Edition map's path's last part) */
static void sdf_path(struct sdf_key const *key, char *path, size_t size)
{
	char name[64];
	char const *map = key->map, *part;
	size_t index;

	for (part = map; *part; part++)
	{
		if (*part == '\\' || *part == '/')
			map = part + 1;
	}
	for (index = 0; map[index] && index < sizeof(name) - 1; index++)
	{
		char letter = map[index];

		name[index] = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
			(letter >= '0' && letter <= '9') || letter == '-' ? letter : '_';
	}
	name[index] = 0;
	snprintf(path, size, "world_sdf/%s_%d.wsdf", name, (int)key->bsp);
}

static int sdf_key_same(struct sdf_key const *a, struct sdf_key const *b)
{
	return a->checksum == b->checksum && a->bsp == b->bsp && !strcmp(a->map, b->map);
}

/* the map loaded now and its BSP; FALSE if none */
static int sdf_key_loaded(struct sdf_key *key)
{
	char const *map = cache_file_loaded_map_name();
	short bsp = global_structure_bsp_index_get();

	if (!map || !map[0])
		return 0;
	memset(key, 0, sizeof(*key));
	snprintf(key->map, sizeof(key->map), "%s", map);
	key->checksum = cache_files_get_checksum();
	key->bsp = bsp < 0 ? 0 : bsp;
	return 1;
}

/* ---------- the threads */

static unsigned long long sdf_now(void)
{
	return SDL_GetTicksNS();
}

/* the bake's pace (world_sdf_frame's, world_sdf_pace_frame): whether its
threads may run, and when the game last said so. A word the game's thread
writes and the bake's threads read, no lock between them: no frame ever
waits for a bake's thread */
static int sdf_pace_running = 1;
static unsigned long long sdf_pace_said;
/* (the game's thread's) the pace, the last frame's time (0: the next not
timed) and whether this one prepared a map */
static struct world_sdf_pace sdf_pace;
static unsigned long long sdf_frame_last;
static int sdf_frame_prepared;
/* the bake running asked to stop for another (the game's thread sets it,
under world_sdf_lock; the baker clears it as it takes the next), whether
the pass running may stop (the baker's: not the static scenery's pages,
after which a bake cannot be begun again from its copies), and a game
played (the game's thread's: the threads a pass begun runs on) */
static int sdf_yield;
static int sdf_yield_able;
static int sdf_playing;

/* whether the bake's threads are to stop where they are (sdf_yield) */
static int sdf_yielding(void)
{
	return __atomic_load_n(&sdf_yield_able, __ATOMIC_RELAXED) && __atomic_load_n(&sdf_yield, __ATOMIC_RELAXED);
}

/* (a bake's thread, between its items) while the game's frames want it to
rest; not when the game has said nothing for a while (no frames drawn: a
window minimized, "world" turned off, a load), when nothing is to be kept
from waiting */
static void sdf_pace_wait(void)
{
	while (!sdf_yielding() && !__atomic_load_n(&sdf_pace_running, __ATOMIC_RELAXED) &&
		sdf_now() - __atomic_load_n(&sdf_pace_said, __ATOMIC_RELAXED) < WORLD_SDF_PACE_SILENCE_NS)
		SDL_Delay(WORLD_SDF_PACE_NAP_MS);
}

/* the next of a pass's items for a thread to take (once the pace lets it),
and the next so many */
static unsigned long sdf_next_many(unsigned long *next, unsigned long count)
{
	sdf_pace_wait();
	/* (stopping: no more items, past every pass's end) */
	if (sdf_yielding())
		return (unsigned long)LONG_MAX;
	return __atomic_fetch_add(next, count, __ATOMIC_RELAXED);
}

static unsigned long sdf_next(unsigned long *next)
{
	return sdf_next_many(next, 1);
}

/* the calling thread at the lowest priority (the game's threads first):
setpriority(PRIO_PROCESS, 0, 19), which the guest's C library has not, as
the runtime's dispatcher takes it (port/android/include/halo_guest_syscall.h's
GUEST_SYS_setpriority, asm-generic's number; each host lowers the calling
thread: host_syscall.c) */
static void sdf_thread_background(void)
{
	__guest_syscall(140, 0, 0, 19, 0, 0, 0);
}

static long sdf_threads(void)
{
	char const *forced = getenv("HALO_WORLD_SDF_THREADS");
	long threads = world_sdf_bake_threads(sysconf(_SC_NPROCESSORS_ONLN), forced ? atol(forced) : 0);

	if (!forced && __atomic_load_n(&sdf_playing, __ATOMIC_RELAXED) && threads > WORLD_SDF_PLAY_THREADS)
		threads = WORLD_SDF_PLAY_THREADS;
	return threads;
}

/* a bake's working memory let go of */
static void sdf_job_scratch_free(struct sdf_job *job)
{
	free(job->marks);
	free(job->firsts);
	free(job->triangles);
	free(job->marked);
	free(job->baked);
	free(job->kinds);
	job->marks = NULL;
	job->firsts = job->triangles = NULL;
	job->marked = NULL;
	job->baked = NULL;
	job->kinds = NULL;
}

/* an object's copies let go of */
static void sdf_object_copy_free(struct sdf_object_copy *copy)
{
	long solid;

	for (solid = 0; solid < copy->solid_count; solid++)
	{
		free(copy->solids[solid].bsp.nodes);
		free(copy->solids[solid].bsp.planes);
	}
	free(copy->solids);
	free(copy->positions);
	free(copy->opacities);
	free(copy->values);
	free(copy->solid);
	free(copy->field.texels);
}

/* a job's copies let go of */
static void sdf_job_copies_free(struct sdf_job *job)
{
	long index;

	free(job->positions);
	free(job->bsp.nodes);
	free(job->bsp.planes);
	job->positions = NULL;
	job->bsp.nodes = NULL;
	job->bsp.planes = NULL;
	for (index = 0; index < job->solid_count; index++)
	{
		free(job->solids[index].bsp.nodes);
		free(job->solids[index].bsp.planes);
	}
	for (index = 0; index < job->page_count; index++)
	{
		free(job->pages[index].triangles);
		free(job->pages[index].texels);
	}
	for (index = 0; index < job->object_count; index++)
		sdf_object_copy_free(&job->object_copies[index]);
	free(job->object_copies);
	job->object_copies = NULL;
	job->object_count = 0;
	free(job->solids);
	free(job->pages);
	free(job->shapes);
	free(job->placements);
	free(job->placement_frames);
	free(job->placement_shapes);
	job->solids = NULL;
	job->pages = NULL;
	job->shapes = NULL;
	job->placements = NULL;
	job->placement_frames = NULL;
	job->placement_shapes = NULL;
	job->solid_count = job->page_count = job->shape_count = job->placement_count = 0;
}

static void sdf_job_free(struct sdf_job *job)
{
	if (!job)
		return;
	sdf_job_copies_free(job);
	sdf_job_scratch_free(job);
	sdf_volume_free(&job->volume);
	free(job);
}

/* (under the lock) a job's result handed to the game */
static void sdf_result_locked(struct sdf_job *job, int result)
{
	struct sdf_job **last = &world_sdf.results;

	sdf_job_copies_free(job);
	sdf_job_scratch_free(job);
	job->result = result;
	job->next = NULL;
	while (*last)
		last = &(*last)->next;
	*last = job;
}

static void *sdf_looker(void *context)
{
	(void)context;
	sdf_thread_background();
	for (;;)
	{
		struct sdf_job *job;
		char path[1100];
		int found;

		pthread_mutex_lock(&world_sdf_lock);
		while (!world_sdf.looks)
			pthread_cond_wait(&world_sdf_look_wake, &world_sdf_lock);
		job = world_sdf.looks;
		world_sdf.looks = job->next;
		pthread_mutex_unlock(&world_sdf_lock);
		sdf_path(&job->key, path, sizeof(path));
		found = sdf_cache_load(path, job->key.checksum, (unsigned long)job->key.bsp, &job->volume);
		pthread_mutex_lock(&world_sdf_lock);
		sdf_result_locked(job, found ? _sdf_result_found : _sdf_result_missing);
		pthread_mutex_unlock(&world_sdf_lock);
	}
	return NULL;
}

/* the marked cells' bricks baked, then every other cell's kind found, a
few cells at a time, until there are none */
static void sdf_bake_cells(struct sdf_job *job)
{
	struct world_sdf_header const *header = &job->volume.header;
	unsigned long cells = header->width * header->height * header->depth;
	unsigned long items = job->marked_count + cells;

	for (;;)
	{
		unsigned long first = sdf_next_many(&job->next_item, WORLD_SDF_WORKER_CELLS), item;

		if (first >= items)
			break;
		for (item = first; item < first + WORLD_SDF_WORKER_CELLS && item < items; item++)
		{
			if (item < job->marked_count)
			{
				unsigned long cell = job->marked[item];

				job->kinds[cell] = (unsigned char)world_sdf_bake_cell(header, cell, job->positions,
					job->triangles + job->firsts[cell], job->firsts[cell + 1] - job->firsts[cell], sdf_job_inside,
					job, job->baked + item * WORLD_SDF_BRICK_VOXELS);
			}
			else if (!job->marks[item - job->marked_count])
			{
				job->kinds[item - job->marked_count] = (unsigned char)world_sdf_cell_kind(header,
					item - job->marked_count, sdf_job_inside, job);
			}
		}
	}
}

/* a slice at a time: its cells marked, or their triangles counted or put
(each slice's cells, and their counts, are its own) */
static void sdf_slices(struct sdf_job *job)
{
	long depth = (long)job->pass_header->depth;

	for (;;)
	{
		long z = (long)sdf_next(&job->next_slice), marked;

		if (z >= depth)
			break;
		if (job->pass == _sdf_pass_mark)
		{
			marked = world_sdf_mark_slices(job->pass_header, job->positions, job->triangle_count, job->pass_marks, z,
				z + 1);
			__atomic_fetch_add(&job->pass_marked, marked, __ATOMIC_RELAXED);
		}
		else
		{
			world_sdf_bin_slices(job->pass_header, job->positions, job->triangle_count, job->pass_marks, job->firsts,
				job->pass == _sdf_pass_fill ? job->triangles : NULL, z, z + 1);
		}
	}
}

/* whether any static shape's reach touches a page's surfaces' bounds */
static int sdf_page_reached(struct sdf_job const *job, struct sdf_page const *page)
{
	long index;

	for (index = 0; index < job->shape_count; index++)
	{
		struct world_proxy const *shape = &job->shapes[index];
		float reach = world_proxy_reach(shape), outside = 0.0f;
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			float centre = (int)shape->v[0][3] == _world_proxy_capsule ? 0.5f * (shape->v[0][axis] + shape->v[1][axis]) :
				shape->v[0][axis];
			float away = centre < page->bounds[axis] ? page->bounds[axis] - centre :
				centre > page->bounds[3 + axis] ? centre - page->bounds[3 + axis] : 0.0f;

			outside += away * away;
		}
		if (outside < reach * reach)
			return 1;
	}
	return 0;
}

/* the reached pages baked, a page at a time, until there are none */
static void sdf_bake_pages(struct sdf_job *job)
{
	for (;;)
	{
		struct sdf_page *page;
		unsigned long index = sdf_next(&job->next_item);

		if (index >= (unsigned long)(job->page_count > 0 ? job->page_count : 0))
			break;
		page = &job->pages[index];
		if (!page->usable || !page->reached)
			continue;
		page->texels = malloc(job->page_side * job->page_side);
		if (page->texels)
			page->covered = world_ao_scenery_page(job->shapes, job->shape_count, page->triangles, page->triangle_count,
				job->page_side, job->page_side, page->texels, &page->shared);
	}
}

/* the objects' fields' rows baked, a row at a time (each object's z
slices' rows in turn), until there are none */
static void sdf_bake_objects_slices(struct sdf_job *job)
{
	for (;;)
	{
		unsigned long item = sdf_next(&job->next_item), first = 0;
		long index;

		for (index = 0; index < job->object_count; index++)
		{
			struct sdf_object_copy *copy = &job->object_copies[index];
			unsigned long slice = copy->field.width * copy->field.height;
			unsigned long rows = copy->field.depth * copy->field.height;

			if (!copy->values)
				continue;
			if (item < first + rows)
			{
				unsigned long z = (item - first) / copy->field.height, y = (item - first) % copy->field.height;
				unsigned long long began = sdf_now();

				world_sdf_object_slice(job->object_tables, &copy->field, copy->positions, copy->opacities,
					copy->triangle_count, sdf_object_inside, copy, z, y, y + 1, copy->values + z * slice * 4,
					copy->solid + z * slice);
				__atomic_fetch_add(&copy->nanoseconds, sdf_now() - began, __ATOMIC_RELAXED);
				break;
			}
			first += rows;
		}
		if (index >= job->object_count)
			break;
	}
}

static void *sdf_bake_worker(void *context)
{
	struct sdf_job *job = context;

	sdf_thread_background();
	if (job->pass == _sdf_pass_bake)
		sdf_bake_cells(job);
	else if (job->pass == _sdf_pass_scenery)
		sdf_bake_pages(job);
	else if (job->pass == _sdf_pass_objects)
		sdf_bake_objects_slices(job);
	else
		sdf_slices(job);
	return NULL;
}

/* a pass run on the bake's threads; how many it ran on */
static long sdf_pass(struct sdf_job *job, int pass)
{
	pthread_t threads[WORLD_SDF_MAXIMUM_THREADS];
	long thread_count = sdf_threads(), started = 0, index;

	job->pass = pass;
	job->next_slice = 0;
	job->next_item = 0;
	__atomic_store_n(&sdf_yield_able, pass != _sdf_pass_scenery, __ATOMIC_RELAXED);
	for (index = 1; index < thread_count; index++)
	{
		if (pthread_create(&threads[started], NULL, sdf_bake_worker, job) == 0)
			started++;
	}
	sdf_bake_worker(job);
	for (index = 0; index < started; index++)
		pthread_join(threads[index], NULL);
	return started + 1;
}

/* (world_sdf_plan's marker: a layout tried, marked on the bake's threads) */
static long sdf_mark(void *context, struct world_sdf_header const *header, float const *positions,
	long triangle_count, unsigned char *marks)
{
	struct sdf_job *job = context;

	(void)positions;
	(void)triangle_count;
	job->pass_header = header;
	job->pass_marks = marks;
	job->pass_marked = 0;
	sdf_pass(job, _sdf_pass_mark);
	return job->pass_marked;
}

/* the baked cells' bricks kept, in the cells' order, each given its place in
the atlas; FALSE if no memory */
static int sdf_bake_gather(struct sdf_job *job)
{
	struct world_sdf_volume *volume = &job->volume;
	unsigned long cells = volume->header.width * volume->header.height * volume->header.depth, cell, item, count = 0;

	for (item = 0; item < job->marked_count; item++)
		count += job->kinds[job->marked[item]] == WORLD_SDF_CELL_BRICK;
	volume->cells = calloc(cells, sizeof(*volume->cells));
	volume->bricks = malloc(count * WORLD_SDF_BRICK_VOXELS + 1);
	if (!volume->cells || !volume->bricks)
		return 0;
	for (cell = 0; cell < cells; cell++)
		volume->cells[cell][3] = job->kinds[cell];
	volume->header.brick_count = 0;
	for (item = 0; item < job->marked_count; item++)
	{
		unsigned long n = volume->header.brick_count;

		cell = job->marked[item];
		if (job->kinds[cell] != WORLD_SDF_CELL_BRICK)
			continue;
		volume->cells[cell][0] = (unsigned char)(n % WORLD_SDF_ATLAS);
		volume->cells[cell][1] = (unsigned char)(n / WORLD_SDF_ATLAS % WORLD_SDF_ATLAS);
		volume->cells[cell][2] = (unsigned char)(n / WORLD_SDF_ATLAS_LAYER);
		memcpy(volume->bricks + n * WORLD_SDF_BRICK_VOXELS, job->baked + item * WORLD_SDF_BRICK_VOXELS,
			WORLD_SDF_BRICK_VOXELS);
		volume->header.brick_count++;
	}
	return 1;
}

/* the static placements' shapes the pages are baked with: a placement
whose definition has an occlusion field in the atlas takes it on its node 0
as placed, in place of its shapes (as the window does: world_proxies.c), and
the pages' reference reads the fields' atlas, made here (*atlas, freed after
the pages: world_ao_objects_atlas; the baker is the only thread that bakes
pages, and the game's own never reads it). FALSE if no memory */
static int sdf_scenery_fields(struct sdf_job *job, unsigned short **atlas)
{
	struct world_sdf_objects const *objects = &job->volume.objects;
	struct world_proxy *shapes;
	unsigned long texels, index, y, z;
	long placement, count = 0, fielded = 0;

	*atlas = NULL;
	for (index = 0; index < objects->count; index++)
		fielded += objects->objects[index].atlas[0] != ~0UL;
	if (!fielded || !job->placement_count)
		return 1;
	texels = objects->atlas_width * objects->atlas_height * objects->atlas_depth;
	*atlas = calloc(texels * 4 + 1, sizeof(**atlas));
	shapes = malloc(((size_t)job->shape_count + (size_t)job->placement_count * WORLD_PROXY_FIELD_SLOTS) *
		sizeof(*shapes) + 1);
	if (!*atlas || !shapes)
	{
		free(shapes);
		return 0;
	}
	for (index = 0; index < objects->count; index++)
	{
		struct world_sdf_object const *object = &objects->objects[index];

		if (object->atlas[0] == ~0UL)
			continue;
		for (z = 0; z < object->depth; z++)
		for (y = 0; y < object->height; y++)
		{
			memcpy(*atlas + (((object->atlas[2] + z) * objects->atlas_height + object->atlas[1] + y) *
				objects->atlas_width + object->atlas[0]) * 4, object->texels + ((z * object->height + y) * object->width) * 4,
				object->width * 4 * sizeof(**atlas));
		}
	}
	for (placement = 0; placement < job->placement_count; placement++)
	{
		long first = job->placement_shapes[placement];
		long end = placement + 1 < job->placement_count ? job->placement_shapes[placement + 1] : job->shape_count;
		struct world_sdf_object const *object = NULL;

		for (index = 0; index < objects->count && !object; index++)
		{
			if (objects->objects[index].definition == job->placements[placement].definition &&
				objects->objects[index].atlas[0] != ~0UL)
			{
				object = &objects->objects[index];
			}
		}
		if (object)
		{
			struct world_proxies_source_field field;
			int axis;

			for (axis = 0; axis < 3; axis++)
			{
				field.origin[axis] = object->origin[axis];
				field.place[axis] = object->atlas[axis];
			}
			field.atlas[0] = objects->atlas_width;
			field.atlas[1] = objects->atlas_height;
			field.atlas[2] = objects->atlas_depth;
			field.voxel = object->voxel;
			field.reach = object->reach;
			memcpy(field.bounds, object->bounds, sizeof(field.bounds));
			field.size[0] = object->width;
			field.size[1] = object->height;
			field.size[2] = object->depth;
			world_proxies_field_place(&field, &job->placement_frames[placement], shapes + count);
			count += WORLD_PROXY_FIELD_SLOTS;
			continue;
		}
		memcpy(shapes + count, job->shapes + first, (size_t)(end - first) * sizeof(*shapes));
		count += end - first;
	}
	free(job->shapes);
	job->shapes = shapes;
	job->shape_count = count;
	world_ao_objects_atlas(*atlas, objects->atlas_width, objects->atlas_height, objects->atlas_depth);
	return 1;
}

/* the static scenery's part of a field: its pages baked (those a static
shape reaches) at the finest side that fits (the lightmap pages' own, at
most WORLD_SDF_SCENERY_MOST_SIDE, halved until the layers fit
WORLD_SDF_SCENERY_MAXIMUM_BYTES), each page a layer of its own where any
of its texels is shaded, one layer of none shared by those that are not, and
no layer where it cannot be (no lightmap, its vertices not its surfaces',
or texels two of its surfaces share: those draw with the shapes walked);
FALSE if no memory. The pages' milliseconds in the header */
static int sdf_bake_scenery(struct sdf_job *job)
{
	struct world_sdf_scenery *scenery = &job->volume.scenery;
	unsigned long long began = sdf_now();
	unsigned long side = WORLD_SDF_SCENERY_MOST_SIDE, size, reached = 0;
	long index, white = WORLD_SDF_SCENERY_NO_LAYER;
	unsigned short *atlas = NULL;

	memset(scenery, 0, sizeof(*scenery));
	if (job->page_count <= 0 && !job->placement_count)
		return 1;
	if (!sdf_scenery_fields(job, &atlas))
		return 0;
	scenery->placement_count = (unsigned long)job->placement_count;
	scenery->placements = malloc(scenery->placement_count * sizeof(*scenery->placements) + 1);
	scenery->page_count = job->page_count > 0 ? (unsigned long)job->page_count : 0;
	scenery->layers = malloc(scenery->page_count * sizeof(*scenery->layers) + 1);
	if (!scenery->placements || !scenery->layers)
		return 0;
	if (scenery->placement_count)
		memcpy(scenery->placements, job->placements, scenery->placement_count * sizeof(*scenery->placements));
	for (index = 0; index < job->page_count; index++)
	{
		struct sdf_page *page = &job->pages[index];

		page->reached = page->usable && page->triangle_count > 0 && sdf_page_reached(job, page);
		reached += page->reached;
	}
	/* (each reached page a layer, and one of none) */
	while (side > WORLD_SDF_SCENERY_LEAST_SIDE && (reached + 1) * side * side > WORLD_SDF_SCENERY_MAXIMUM_BYTES)
		side /= 2;
	job->page_side = side;
	if (reached)
		sdf_pass(job, _sdf_pass_scenery);
	/* (the pages' reference no longer reads the fields' atlas) */
	world_ao_objects_atlas(NULL, 0, 0, 0);
	free(atlas);
	size = side * side;
	scenery->width = scenery->height = side;
	scenery->texels = malloc((reached + 1) * size + 1);
	if (!scenery->texels)
		return 0;
	for (index = 0; index < job->page_count; index++)
	{
		struct sdf_page *page = &job->pages[index];
		unsigned long texel;
		int shaded = 0;

		scenery->layers[index] = WORLD_SDF_SCENERY_NO_LAYER;
		if (!page->usable || page->triangle_count <= 0)
			continue;
		if (page->reached && (!page->texels || page->shared * 100 > page->covered))
		{
			platform_log("world sdf: %s bsp %d: lightmap page %ld %s: the static scenery's shapes walked there",
				job->key.map, (int)job->key.bsp, index, page->texels ? "has texels two of its surfaces share" :
				"could not be baked");
			continue;
		}
		for (texel = 0; page->reached && texel < size && !shaded; texel++)
			shaded = page->texels[texel] != 255;
		if (!shaded)
		{
			/* (a page none of whose texels are shaded: the layer of none) */
			if (white == WORLD_SDF_SCENERY_NO_LAYER)
			{
				white = (long)scenery->layer_count++;
				memset(scenery->texels + (unsigned long)white * size, 255, size);
			}
			scenery->layers[index] = (short)white;
			continue;
		}
		memcpy(scenery->texels + scenery->layer_count * size, page->texels, size);
		scenery->layers[index] = (short)scenery->layer_count++;
	}
	if (!scenery->layer_count)
		scenery->width = scenery->height = 0;
	job->volume.header.scenery_milliseconds = (unsigned long)((sdf_now() - began) / 1000000ULL);
	return 1;
}

/* ---------- the objects' fields kept by their geometry

An object's field is made of its geometry alone: its triangles, their
opacities, its solids and its grid (sdf_object_copy). The same object is in
many maps (a Warthog, a flag base, a storage crate: the 217 objects' fields
of the playtest that found this were 119 objects'), so each field baked is kept apart too, named by a
hash of all of that, and a map's bake takes it from there instead of baking
it again: <data>/world_sdf/objects/<hash>.wsdo, the magic, WORLD_SDF_VERSION,
the hash (8 bytes), the part's size, the part (sdf_objects_write's, of the
one object) and the CRC-32 of all before it. A file that fails a check is
baked again (and written over). */

enum
{
	WORLD_SDF_OBJECT_FILE_MAGIC = 0x4F465348,
	WORLD_SDF_OBJECT_FILE_HEADER = 20,
};

/* (FNV-1a, 64 bits) */
static unsigned long long sdf_hash_bytes(unsigned long long hash, void const *bytes, unsigned long size)
{
	unsigned char const *byte = bytes;
	unsigned long index;

	for (index = 0; index < size; index++)
		hash = (hash ^ byte[index]) * 0x100000001B3ULL;
	return hash;
}

static unsigned long long sdf_hash_long(unsigned long long hash, unsigned long value)
{
	unsigned char bytes[4];

	sdf_put32(bytes, value);
	return sdf_hash_bytes(hash, bytes, 4);
}

/* what an object's field is made of, laid out (copy->field's grid) */
static unsigned long long sdf_object_hash(struct sdf_object_copy const *copy)
{
	unsigned long long hash = 0xCBF29CE484222325ULL;
	long index;

	hash = sdf_hash_long(hash, WORLD_SDF_VERSION);
	hash = sdf_hash_long(hash, (unsigned long)copy->scenery);
	hash = sdf_hash_long(hash, copy->field.width);
	hash = sdf_hash_long(hash, copy->field.height);
	hash = sdf_hash_long(hash, copy->field.depth);
	hash = sdf_hash_bytes(hash, copy->field.origin, sizeof(copy->field.origin));
	hash = sdf_hash_bytes(hash, &copy->field.voxel, sizeof(copy->field.voxel));
	hash = sdf_hash_bytes(hash, copy->bounds, sizeof(copy->bounds));
	hash = sdf_hash_long(hash, (unsigned long)copy->triangle_count);
	hash = sdf_hash_bytes(hash, copy->positions, (unsigned long)copy->triangle_count * 9 * sizeof(float));
	hash = sdf_hash_long(hash, copy->opacities != NULL);
	if (copy->opacities)
		hash = sdf_hash_bytes(hash, copy->opacities, (unsigned long)copy->triangle_count * sizeof(float));
	hash = sdf_hash_long(hash, (unsigned long)copy->solid_count);
	for (index = 0; index < copy->solid_count; index++)
	{
		struct sdf_solid const *solid = &copy->solids[index];

		hash = sdf_hash_bytes(hash, solid->to_local, sizeof(solid->to_local));
		hash = sdf_hash_bytes(hash, solid->centre, sizeof(solid->centre));
		hash = sdf_hash_bytes(hash, &solid->radius, sizeof(solid->radius));
		hash = sdf_hash_long(hash, (unsigned long)solid->bsp.node_count);
		hash = sdf_hash_bytes(hash, solid->bsp.nodes, (unsigned long)solid->bsp.node_count * sizeof(*solid->bsp.nodes));
		hash = sdf_hash_long(hash, (unsigned long)solid->bsp.plane_count);
		hash = sdf_hash_bytes(hash, solid->bsp.planes,
			(unsigned long)solid->bsp.plane_count * sizeof(*solid->bsp.planes));
	}
	return hash;
}

static void sdf_object_cache_path(unsigned long long hash, char *path, size_t size)
{
	snprintf(path, size, "world_sdf/objects/%08lx%08lx.wsdo", (unsigned long)(hash >> 32),
		(unsigned long)(hash & 0xFFFFFFFFUL));
}

/* an object's field from those kept, if its geometry's was (copy->field
laid out: its texels and reach taken, its definition kept); FALSE if not */
static int sdf_object_cache_load(struct sdf_object_copy *copy, unsigned long long hash)
{
	char path[128];
	unsigned long size = 0, part;
	unsigned char *bytes;
	struct world_sdf_objects objects;
	int found = 0;

	sdf_object_cache_path(hash, path, sizeof(path));
	bytes = sdf_file_read(path, WORLD_SDF_OBJECT_FILE_HEADER + 4 + WORLD_SDF_OBJECT_ENTRY +
		(unsigned long)WORLD_SDF_OBJECT_MOST_VOXELS * WORLD_SDF_OBJECT_TEXEL + WORLD_SDF_FILE_CRC, &size);
	if (!bytes)
		return 0;
	part = size >= WORLD_SDF_OBJECT_FILE_HEADER + WORLD_SDF_FILE_CRC ? sdf_get32(bytes + 16) : 0;
	if (size >= WORLD_SDF_OBJECT_FILE_HEADER + WORLD_SDF_FILE_CRC && sdf_get32(bytes) == WORLD_SDF_OBJECT_FILE_MAGIC &&
		sdf_get32(bytes + 4) == WORLD_SDF_VERSION && sdf_get32(bytes + 8) == (unsigned long)(hash >> 32) &&
		sdf_get32(bytes + 12) == (unsigned long)(hash & 0xFFFFFFFFUL) &&
		part == size - WORLD_SDF_OBJECT_FILE_HEADER - WORLD_SDF_FILE_CRC &&
		sdf_get32(bytes + size - WORLD_SDF_FILE_CRC) == sdf_crc(0, bytes, size - WORLD_SDF_FILE_CRC) &&
		sdf_objects_read(bytes + WORLD_SDF_OBJECT_FILE_HEADER, part, &objects))
	{
		struct world_sdf_object *object = &objects.objects[0];

		if (objects.count == 1 && object->width == copy->field.width && object->height == copy->field.height &&
			object->depth == copy->field.depth)
		{
			free(copy->field.texels);
			copy->field.texels = object->texels;
			copy->field.reach = object->reach;
			memcpy(copy->field.origin, object->origin, sizeof(copy->field.origin));
			copy->field.voxel = object->voxel;
			memcpy(copy->field.bounds, object->bounds, sizeof(copy->field.bounds));
			object->texels = NULL;
			found = 1;
		}
		sdf_objects_free(&objects);
	}
	free(bytes);
	return found;
}

/* an object's field baked, kept for the other maps that have it */
static void sdf_object_cache_write(struct sdf_object_copy const *copy, unsigned long long hash)
{
	char path[128];
	struct world_sdf_objects objects;
	struct world_sdf_object object = copy->field;
	unsigned long part, size;
	unsigned char *bytes;

	objects.count = 1;
	objects.objects = &object;
	part = sdf_objects_size(&objects);
	size = WORLD_SDF_OBJECT_FILE_HEADER + part + WORLD_SDF_FILE_CRC;
	bytes = malloc(size);
	if (!bytes)
		return;
	sdf_put32(bytes, WORLD_SDF_OBJECT_FILE_MAGIC);
	sdf_put32(bytes + 4, WORLD_SDF_VERSION);
	sdf_put32(bytes + 8, (unsigned long)(hash >> 32));
	sdf_put32(bytes + 12, (unsigned long)(hash & 0xFFFFFFFFUL));
	sdf_put32(bytes + 16, part);
	sdf_objects_write(&objects, bytes + WORLD_SDF_OBJECT_FILE_HEADER);
	sdf_put32(bytes + size - WORLD_SDF_FILE_CRC, sdf_crc(0, bytes, size - WORLD_SDF_FILE_CRC));
	mkdir(sdf_folder(), 0755);
	sdf_object_cache_path(hash, path, sizeof(path));
	{
		char folder[1100];

		snprintf(folder, sizeof(folder), "%s/objects", sdf_folder());
		mkdir(folder, 0755);
	}
	sdf_file_write(path, bytes, size);
	free(bytes);
}

/* the objects' occlusion fields (docs/world-ao.md, "The objects' occlusion fields"):
each laid out about its geometry, its rows baked on the bake's threads, its
voxels in solid filled and its reach found; FALSE if no memory, -1 if it
stopped for another bake (sdf_yield). Their milliseconds in the header */
static int sdf_bake_objects(struct sdf_job *job)
{
	struct world_sdf_objects *objects = &job->volume.objects;
	unsigned long long began = sdf_now();
	unsigned long long *hashes;
	long index, baking = 0, cached = 0;

	memset(objects, 0, sizeof(*objects));
	if (!job->object_count)
		return 1;
	hashes = calloc((size_t)job->object_count, sizeof(*hashes));
	if (!hashes)
		return 0;
	for (index = 0; index < job->object_count; index++)
	{
		struct sdf_object_copy *copy = &job->object_copies[index];
		unsigned long voxels;

		world_sdf_object_layout(copy->bounds, copy->scenery ? WORLD_SDF_OBJECT_SCENERY_VOXELS :
			WORLD_SDF_OBJECT_MOST_VOXELS, &copy->field);
		copy->field.definition = copy->definition;
		/* (another map's, the same geometry: kept) */
		hashes[index] = sdf_object_hash(copy);
		copy->cached = sdf_object_cache_load(copy, hashes[index]);
		if (copy->cached)
		{
			cached++;
			continue;
		}
		voxels = copy->field.width * copy->field.height * copy->field.depth;
		copy->values = malloc(voxels * 4 * sizeof(float));
		copy->solid = malloc(voxels);
		if (!copy->values || !copy->solid)
		{
			free(hashes);
			return 0;
		}
		baking++;
	}
	if (baking)
	{
		job->object_tables = world_sdf_object_tables_make();
		if (!job->object_tables)
		{
			free(hashes);
			return 0;
		}
		sdf_pass(job, _sdf_pass_objects);
		world_sdf_object_tables_free(job->object_tables);
		job->object_tables = NULL;
		if (sdf_yielding())
		{
			free(hashes);
			return -1;
		}
	}
	objects->objects = calloc((size_t)job->object_count, sizeof(*objects->objects));
	if (!objects->objects)
	{
		free(hashes);
		return 0;
	}
	for (index = 0; index < job->object_count; index++)
	{
		struct sdf_object_copy *copy = &job->object_copies[index];

		if (!copy->cached)
		{
			if (!world_sdf_object_finish(&copy->field, copy->values, copy->solid))
			{
				free(hashes);
				return 0;
			}
			sdf_object_cache_write(copy, hashes[index]);
		}
		{
			/* (a piece of scenery's triangles partly drawn, its leaves', and
			their mean opacity) */
			long triangle, partly = 0;
			double opacity = 0.0;

			for (triangle = 0; copy->opacities && triangle < copy->triangle_count; triangle++)
			{
				if (copy->opacities[triangle] < 1.0f)
				{
					partly++;
					opacity += copy->opacities[triangle];
				}
			}
			if (copy->cached)
				platform_log("world sdf: %s bsp %d: %s's occlusion field: %ld triangles, %lux%lux%lu voxels, the "
					"same geometry's baked before (world_sdf/objects)", job->key.map, (int)job->key.bsp, copy->name,
					copy->triangle_count, copy->field.width, copy->field.height, copy->field.depth);
			else
				platform_log("world sdf: %s bsp %d: %s's occlusion field: %ld triangles (%ld partly drawn, %.2f of "
					"each), %lux%lux%lu voxels of %.3f, reach %.1f, %.1f ms on the threads, %lu bytes", job->key.map,
					(int)job->key.bsp, copy->name, copy->triangle_count, partly, partly ? opacity / (double)partly :
					1.0, copy->field.width, copy->field.height, copy->field.depth, (double)copy->field.voxel,
					(double)(copy->field.reach * copy->field.voxel), (double)copy->nanoseconds / 1e6,
					copy->field.width * copy->field.height * copy->field.depth * (unsigned long)WORLD_SDF_OBJECT_TEXEL);
		}
		objects->objects[objects->count++] = copy->field;
		copy->field.texels = NULL;
		free(copy->values);
		free(copy->solid);
		copy->values = NULL;
		copy->solid = NULL;
	}
	free(hashes);
	if (world_sdf_objects_place(objects) < objects->count)
	{
		for (index = 0; index < (long)objects->count; index++)
		{
			if (objects->objects[index].atlas[0] == ~0UL)
				platform_log("world sdf: %s bsp %d: %s's occlusion field is past the atlas's room: its shapes shade",
					job->key.map, (int)job->key.bsp, job->object_copies[index].name);
		}
	}
	job->volume.header.objects_milliseconds = (unsigned long)((sdf_now() - began) / 1000000ULL);
	job->objects_cached = cached;
	return 1;
}

/* a field baked on the bake's threads, written to the cache; FALSE if it
could not be, -1 if it stopped for another (sdf_yield: begun again later,
from its copies) */
static int sdf_bake(struct sdf_job *job)
{
	struct world_sdf_header *header = &job->volume.header;
	long threads;
	int objects;
	unsigned long long began = sdf_now();
	unsigned long cells, cell, size = 0;
	unsigned char *file;
	char path[1100];
	int written;

	memset(header, 0, sizeof(*header));
	/* (the finest voxel its memory allows; the cells a surface may reach,
	and the triangles that may reach each) */
	job->marks = world_sdf_plan(job->bounds, job->positions, job->triangle_count, header, sdf_mark, job);
	if (sdf_yielding())
		return -1;
	if (!job->marks)
		return 0;
	header->magic = WORLD_SDF_MAGIC;
	header->version = WORLD_SDF_VERSION;
	header->map_checksum = job->key.checksum;
	header->bsp_index = (unsigned long)job->key.bsp;
	cells = header->width * header->height * header->depth;
	job->pass_header = header;
	job->pass_marks = job->marks;
	job->firsts = calloc(cells + 1, sizeof(*job->firsts));
	if (!job->firsts)
		return 0;
	sdf_pass(job, _sdf_pass_count);
	if (sdf_yielding())
		return -1;
	job->triangles = world_sdf_bin_place(header, job->firsts);
	if (!job->triangles)
		return 0;
	sdf_pass(job, _sdf_pass_fill);
	if (sdf_yielding())
		return -1;
	world_sdf_bin_placed(header, job->firsts);
	job->marked_count = 0;
	for (cell = 0; cell < cells; cell++)
		job->marked_count += job->marks[cell];
	job->marked = malloc(job->marked_count * sizeof(*job->marked) + 1);
	job->baked = malloc(job->marked_count * WORLD_SDF_BRICK_VOXELS + 1);
	job->kinds = malloc(cells);
	if (!job->triangles || !job->marked || !job->baked || !job->kinds)
		return 0;
	for (cell = 0, job->marked_count = 0; cell < cells; cell++)
	{
		if (job->marks[cell])
			job->marked[job->marked_count++] = cell;
	}
	threads = sdf_pass(job, _sdf_pass_bake);
	if (sdf_yielding())
		return -1;
	/* (the objects' fields before the static scenery's pages, which the
fields shade where a placement has one) */
	if (!sdf_bake_gather(job))
		return 0;
	objects = sdf_bake_objects(job);
	if (objects <= 0)
		return objects;
	if (!sdf_bake_scenery(job))
		return 0;
	sdf_job_scratch_free(job);
	header->bake_milliseconds = (unsigned long)((sdf_now() - began) / 1000000ULL);
	header->bake_threads = (unsigned long)threads;
	sdf_path(&job->key, path, sizeof(path));
	mkdir(sdf_folder(), 0755);
	file = sdf_file_pack(&job->volume, &size);
	written = file && sdf_file_write(path, file, size);
	free(file);
	platform_log("world sdf: %s bsp %d: %ld triangles copied in %.1f ms; within %.0fx%.0fx%.0f, %lux%lux%lu cells, %lu "
		"bricks, voxel %.3f; static scenery %lu placements, %ld shapes, %lu of %ld lightmap pages in %lu layers of "
		"%lux%lu (%lu ms); %lu objects' occlusion fields (%ld of them baked before, %lu ms, %lu bytes in the atlas); in %lu ms on %lu threads, "
		"%lu bytes in memory, %lu cached%s", job->key.map,
		(int)job->key.bsp, job->triangle_count, job->copy_milliseconds, (double)(job->bounds[3] - job->bounds[0]),
		(double)(job->bounds[4] - job->bounds[1]), (double)(job->bounds[5] - job->bounds[2]), header->width,
		header->height, header->depth, header->brick_count, (double)header->voxel,
		job->volume.scenery.placement_count, job->shape_count, sdf_pages_layered(&job->volume.scenery),
		job->page_count > 0 ? job->page_count : 0L, job->volume.scenery.layer_count, job->volume.scenery.width,
		job->volume.scenery.height, header->scenery_milliseconds, job->volume.objects.count,
		job->objects_cached, header->objects_milliseconds, sdf_objects_gpu_bytes(&job->volume.objects), header->bake_milliseconds,
		header->bake_threads, world_sdf_bytes(header, header->brick_count) + sdf_scenery_gpu_bytes(&job->volume.scenery) +
		sdf_objects_gpu_bytes(&job->volume.objects),
		written ? size : 0UL, written ? "" : file ? " (not written: the file could not be)" : " (not written: the deflate failed)");
	if (job->startup)
	{
		pthread_mutex_lock(&world_sdf_lock);
		world_sdf.baked_milliseconds += header->bake_milliseconds;
		world_sdf.baked_count++;
		pthread_mutex_unlock(&world_sdf_lock);
	}
	return 1;
}

/* (under the lock) whether a field may be baked now: "world" on, and in a
game only the field drawn */
static int sdf_bake_wanted_locked(struct sdf_key const *key)
{
	return !world_sdf.off && (!world_sdf.playing || (world_sdf.wanted_known && sdf_key_same(key, &world_sdf.wanted)));
}

/* (under the lock) the next bake to run, taken from those waiting: the
field drawn first (sdf_bake_queued puts it first), and in a game no other;
NULL while none may run (the start-up pass holding them, "world" off) */
static struct sdf_job *sdf_bake_next_locked(void)
{
	struct sdf_job **link;

	if (world_sdf.hold || world_sdf.off)
		return NULL;
	for (link = &world_sdf.bakes; *link; link = &(*link)->next)
	{
		if (sdf_bake_wanted_locked(&(*link)->key))
		{
			struct sdf_job *job = *link;

			*link = job->next;
			job->next = NULL;
			return job;
		}
	}
	return NULL;
}

/* a bake stopped for another (sdf_bake's -1): what it made let go of, its
copies kept, and put back last among those waiting */
static void sdf_bake_yielded(struct sdf_job *job)
{
	struct sdf_job **last;
	long index;

	sdf_job_scratch_free(job);
	for (index = 0; index < job->object_count; index++)
	{
		free(job->object_copies[index].values);
		free(job->object_copies[index].solid);
		free(job->object_copies[index].field.texels);
		job->object_copies[index].values = NULL;
		job->object_copies[index].solid = NULL;
		job->object_copies[index].field.texels = NULL;
		job->object_copies[index].cached = 0;
		job->object_copies[index].nanoseconds = 0;
	}
	if (job->object_tables)
		world_sdf_object_tables_free(job->object_tables);
	job->object_tables = NULL;
	sdf_volume_free(&job->volume);
	memset(&job->volume, 0, sizeof(job->volume));
	pthread_mutex_lock(&world_sdf_lock);
	for (last = &world_sdf.bakes; *last; last = &(*last)->next)
		;
	*last = job;
	job->next = NULL;
	pthread_mutex_unlock(&world_sdf_lock);
}

static void *sdf_baker(void *context)
{
	(void)context;
	sdf_thread_background();
	for (;;)
	{
		struct sdf_job *job;
		char path[1100];
		int result, baked;

		pthread_mutex_lock(&world_sdf_lock);
		world_sdf.baking = 0;
		while (!(job = sdf_bake_next_locked()))
			pthread_cond_wait(&world_sdf_bake_wake, &world_sdf_lock);
		world_sdf.baking = 1;
		world_sdf.baking_key = job->key;
		__atomic_store_n(&sdf_yield, 0, __ATOMIC_RELAXED);
		pthread_mutex_unlock(&world_sdf_lock);
		/* (baked meanwhile: found) */
		sdf_path(&job->key, path, sizeof(path));
		if (sdf_cache_load(path, job->key.checksum, (unsigned long)job->key.bsp, &job->volume))
			result = _sdf_result_found;
		else
		{
			baked = sdf_bake(job);
			if (baked < 0)
			{
				platform_log("world sdf: %s bsp %d: its bake stopped for the field drawn, to begin again after it",
					job->key.map, (int)job->key.bsp);
				sdf_bake_yielded(job);
				continue;
			}
			result = baked ? _sdf_result_baked : _sdf_result_failed;
		}
		pthread_mutex_lock(&world_sdf_lock);
		sdf_result_locked(job, result);
		pthread_mutex_unlock(&world_sdf_lock);
	}
	return NULL;
}

/* a job handed to its thread (made the first time; detached: it ends with
the game) */
static void sdf_queue(struct sdf_job *job)
{
	int bake = job->kind == _sdf_job_bake;
	struct sdf_job **last;
	long waiting = 0;
	int *running;
	/* (a bake queued while the start-up pass runs waits for its end: the
	frames' check, world_sdf_frame, lets it go) */
	int hold = menu_pictures_preparing();

	pthread_mutex_lock(&world_sdf_lock);
	if (bake && hold)
		world_sdf.hold = 1;
	running = bake ? &world_sdf.baker_running : &world_sdf.looker_running;
	if (!*running)
	{
		pthread_t thread;
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		*running = pthread_create(&thread, &attributes, bake ? sdf_baker : sdf_looker, NULL) == 0;
		pthread_attr_destroy(&attributes);
		if (!*running)
		{
			sdf_result_locked(job, _sdf_result_failed);
			pthread_mutex_unlock(&world_sdf_lock);
			return;
		}
	}
	last = bake ? &world_sdf.bakes : &world_sdf.looks;
	while (*last)
	{
		last = &(*last)->next;
		waiting++;
	}
	*last = job;
	job->next = NULL;
	/* (too many bakes waiting: the oldest dropped, its field baked when its
	map is played) */
	if (bake && waiting >= WORLD_SDF_MAXIMUM_BAKES)
	{
		struct sdf_job *oldest = world_sdf.bakes;

		world_sdf.bakes = oldest->next;
		sdf_job_free(oldest);
	}
	pthread_cond_signal(bake ? &world_sdf_bake_wake : &world_sdf_look_wake);
	pthread_mutex_unlock(&world_sdf_lock);
}

/* whether a field is waiting to be baked, or baking; one waiting put
first (the map played: the start-up pass's other fields wait for it) */
static int sdf_bake_queued(struct sdf_key const *key)
{
	struct sdf_job **job;
	int queued;

	pthread_mutex_lock(&world_sdf_lock);
	queued = world_sdf.baking && sdf_key_same(&world_sdf.baking_key, key);
	for (job = &world_sdf.bakes; *job && !queued; job = &(*job)->next)
	{
		if (sdf_key_same(&(*job)->key, key))
		{
			struct sdf_job *found = *job;

			*job = found->next;
			found->next = world_sdf.bakes;
			world_sdf.bakes = found;
			queued = 1;
			break;
		}
	}
	pthread_mutex_unlock(&world_sdf_lock);
	return queued;
}

/* ---------- the game's thread */

/* where objects can be in a BSP: its bounds, and the box of the places in
them that the scenario puts players, units, vehicles, items and devices
and sends the AI (not its scenery, which may be far off, nor its sounds);
the box is grown by WORLD_SDF_PLACEMENTS_REACH */
struct sdf_places
{
	float bounds[6];
	float box[6];
	long count;
};

static void sdf_place(struct sdf_places *places, real_point3d const *point)
{
	float const at[3] = { point->x, point->y, point->z };
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (!(at[axis] >= places->bounds[axis] - WORLD_SDF_PLACEMENTS_REACH &&
			at[axis] <= places->bounds[3 + axis] + WORLD_SDF_PLACEMENTS_REACH))
		{
			return;
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		if (!places->count || at[axis] < places->box[axis])
			places->box[axis] = at[axis];
		if (!places->count || at[axis] > places->box[3 + axis])
			places->box[3 + axis] = at[axis];
	}
	places->count++;
}

/* each element's point, at offset in elements of size bytes */
static void sdf_place_block(struct sdf_places *places, struct tag_block const *block, long size, long offset)
{
	long index;

	for (index = 0; block->address && index < block->count; index++)
		sdf_place(places, (real_point3d const *)((byte const *)block->address + index * size + offset));
}

/* the bounds cut to the placements' box grown by their reach (the BSP's
bounds as they were if none is in them) */
static void sdf_placements_clip(float bounds[6])
{
	struct scenario *scenario = global_scenario_get();
	struct sdf_places places;
	long index, squad;
	int axis;

	if (!scenario)
		return;
	memset(&places, 0, sizeof(places));
	memcpy(places.bounds, bounds, sizeof(places.bounds));
	/* (the blocks' element sizes, and their points' offsets: tag_schema_scenario.c's) */
	sdf_place_block(&places, &scenario->players, 0x34, 0);
	sdf_place_block(&places, &scenario->netgame_flags, 0x94, 0);
	sdf_place_block(&places, &scenario->netgame_equipment, 0x90, 0x40);
	sdf_place_block(&places, &scenario->bipeds, 0x78, 8);
	sdf_place_block(&places, &scenario->vehicles, 0x78, 8);
	sdf_place_block(&places, &scenario->weapons, 0x5C, 8);
	sdf_place_block(&places, &scenario->equipment, 0x28, 8);
	sdf_place_block(&places, &scenario->machines, 0x40, 8);
	sdf_place_block(&places, &scenario->controls, 0x40, 8);
	for (index = 0; scenario->ai_encounters.address && index < scenario->ai_encounters.count; index++)
	{
		struct encounter_definition const *encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, index,
			struct encounter_definition);

		sdf_place_block(&places, &encounter->firing_positions, sizeof(struct firing_position_definition), 0);
		sdf_place_block(&places, &encounter->player_starting_locations, 0x34, 0);
		for (squad = 0; encounter->squads.address && squad < encounter->squads.count; squad++)
		{
			struct squad_definition const *definition = TAG_BLOCK_GET_ELEMENT(&encounter->squads, squad,
				struct squad_definition);

			sdf_place_block(&places, &definition->starting_locations, sizeof(struct actor_starting_location), 0);
		}
	}
	if (!places.count)
		return;
	for (axis = 0; axis < 3; axis++)
	{
		float low = places.box[axis] - WORLD_SDF_PLACEMENTS_REACH, high = places.box[3 + axis] + WORLD_SDF_PLACEMENTS_REACH;

		if (low > bounds[axis])
			bounds[axis] = low;
		if (high < bounds[3 + axis])
			bounds[3 + axis] = high;
		if (bounds[3 + axis] < bounds[axis])
			bounds[3 + axis] = bounds[axis];
	}
}

/* a collision BSP's bsp3d copied (its point test); FALSE if no memory */
static int sdf_bsp_copy(struct collision_bsp const *collision, struct sdf_bsp *copy)
{
	long index;

	copy->node_count = collision->bsp3d.nodes.count;
	copy->plane_count = collision->bsp3d.planes.count;
	copy->nodes = NULL;
	copy->planes = NULL;
	if (copy->node_count <= 0 || copy->plane_count <= 0 || !collision->bsp3d.nodes.address ||
		!collision->bsp3d.planes.address)
	{
		copy->node_count = copy->plane_count = 0;
		return 1;
	}
	copy->nodes = malloc((size_t)copy->node_count * sizeof(*copy->nodes));
	copy->planes = malloc((size_t)copy->plane_count * sizeof(*copy->planes));
	if (!copy->nodes || !copy->planes)
		return 0;
	for (index = 0; index < copy->node_count; index++)
	{
		struct bsp3d_node const *node = TAG_BLOCK_GET_ELEMENT(&collision->bsp3d.nodes, index, struct bsp3d_node);

		copy->nodes[index][0] = node->plane_designator;
		copy->nodes[index][1] = node->children[0];
		copy->nodes[index][2] = node->children[1];
	}
	for (index = 0; index < copy->plane_count; index++)
	{
		real_plane3d const *plane = TAG_BLOCK_GET_ELEMENT(&collision->bsp3d.planes, index, real_plane3d);

		copy->planes[index][0] = plane->n.i;
		copy->planes[index][1] = plane->n.j;
		copy->planes[index][2] = plane->n.k;
		copy->planes[index][3] = plane->d;
	}
	return 1;
}

/* ---------- the static scenery (docs/world-ao.md, "Static scenery") */

/* whether a placement is in this BSP, as the game places a placement in a
BSP (object_types.c): its position or its bounding centre in a leaf */
static int sdf_placement_in_bsp(long definition_index, struct scenario_object_datum const *placement)
{
	struct object_definition const *definition = object_definition_get(definition_index);
	real_matrix4x3 matrix;
	real_point3d offset;

	if (!definition)
		return 0;
	if (scenario_leaf_index_from_point(&placement->position) != NONE)
		return 1;
	matrix4x3_rotation_from_angles(&matrix, placement->rotation.yaw, placement->rotation.pitch, placement->rotation.roll);
	matrix.position = placement->position;
	matrix4x3_transform_point(&matrix, &definition->object.bounding_offset, &offset);
	return scenario_leaf_index_from_point(&offset) != NONE;
}

/* a placement's definition if it is static (FALSE if not): no script can
name it, it is made at the map's start on every difficulty, in this BSP
(as the game places a placement in a BSP: object_types.c), and nothing
moves or destroys it (no animations, which are how a device moves, no body
to damage). Everything else the proxies shade, as before */
static long sdf_scenery_static(struct tag_block const *palette, struct scenario_object_datum const *placement)
{
	struct scenario_object_palette_entry const *entry;
	struct object_definition const *definition;
	long index;

	if (placement->name_index != NONE || (placement->placement_flags & ((1 << NUMBER_OF_SCENARIO_OBJECT_LOCATION_PLACEMENT_FLAGS) - 1)) ||
		placement->palette_entry_index < 0 || placement->palette_entry_index >= palette->count || !palette->address)
	{
		return NONE;
	}
	entry = TAG_BLOCK_GET_ELEMENT(palette, placement->palette_entry_index, struct scenario_object_palette_entry);
	index = entry->reference.index;
	if (index == NONE || !(definition = object_definition_get(index)) || definition->object.model.index == NONE)
		return NONE;
	if (definition->object.animation_graph.index != NONE)
	{
		struct animation_graph const *graph = animation_graph_definition_get(definition->object.animation_graph.index);

		if (graph && graph->animations.count > 0)
			return NONE;
	}
	if (definition->object.collision_model.index != NONE)
	{
		struct collision_model const *collision = collision_model_definition_get(definition->object.collision_model.index);

		if (collision && collision->resistance.maximum_body_vitality > 0.0f)
			return NONE;
	}
	return sdf_placement_in_bsp(index, placement) ? index : NONE;
}

static void sdf_frame_from(real_matrix4x3 const *matrix, struct world_proxies_frame *frame)
{
	frame->axes[0][0] = matrix->scale * matrix->forward.i;
	frame->axes[0][1] = matrix->scale * matrix->forward.j;
	frame->axes[0][2] = matrix->scale * matrix->forward.k;
	frame->axes[1][0] = matrix->scale * matrix->left.i;
	frame->axes[1][1] = matrix->scale * matrix->left.j;
	frame->axes[1][2] = matrix->scale * matrix->left.k;
	frame->axes[2][0] = matrix->scale * matrix->up.i;
	frame->axes[2][1] = matrix->scale * matrix->up.j;
	frame->axes[2][2] = matrix->scale * matrix->up.k;
	frame->position[0] = matrix->position.x;
	frame->position[1] = matrix->position.y;
	frame->position[2] = matrix->position.z;
}

static void sdf_point_from(real_matrix4x3 const *matrix, float const local[3], float *world)
{
	real_point3d in, out;

	in.x = local[0];
	in.y = local[1];
	in.z = local[2];
	matrix4x3_transform_point(matrix, &in, &out);
	world[0] = out.x;
	world[1] = out.y;
	world[2] = out.z;
}

/* grown to hold count more of size bytes (its room in *room); FALSE if
no memory */
static int sdf_grow(void **array, long *room, long used, long count, size_t size)
{
	if (used + count <= *room)
		return 1;
	while (*room < used + count)
		*room = *room ? *room * 2 : 64;
	*array = realloc(*array, (size_t)*room * size);
	return *array != NULL;
}

/* the static scenery's copies: its placements, each node's collision
triangles (put after the structure's in positions) and solid, and its
shapes as placed; FALSE if no memory (none kept) */
/* the scenery's and the devices' placements' blocks, their palettes and
their placements' bytes (tag_schema_scenario.c's) */
struct sdf_scenery_kind
{
	struct tag_block const *placements, *palette;
	long size;
};

enum
{
	SDF_SCENERY_KINDS = 4,
};

static void sdf_scenery_kinds(struct scenario *scenario, struct sdf_scenery_kind kinds[SDF_SCENERY_KINDS])
{
	kinds[0].placements = &scenario->scenery;
	kinds[0].palette = &scenario->scenery_palette;
	kinds[0].size = 0x48;
	kinds[1].placements = &scenario->machines;
	kinds[1].palette = &scenario->machine_palette;
	kinds[1].size = 0x40;
	kinds[2].placements = &scenario->controls;
	kinds[2].palette = &scenario->control_palette;
	kinds[2].size = 0x40;
	kinds[3].placements = &scenario->light_fixtures;
	kinds[3].palette = &scenario->light_fixtures_palette;
	kinds[3].size = 0x58;
}

/* a kind's placement, and its definition if the bake holds it (static,
with a model of nodes: sdf_scenery_copy's); NONE if not */
static long sdf_scenery_placement(struct sdf_scenery_kind const *kind, long index,
	struct scenario_object_datum const **placement)
{
	long definition_index;
	struct model const *model;

	*placement = (struct scenario_object_datum const *)((byte const *)kind->placements->address + index * kind->size);
	definition_index = sdf_scenery_static(kind->palette, *placement);
	if (definition_index == NONE)
		return NONE;
	model = model_definition_get(object_definition_get(definition_index)->object.model.index);
	return model && model->nodes.address && model->nodes.count > 0 ? definition_index : NONE;
}

static int sdf_scenery_copy(struct sdf_job *job)
{
	struct scenario *scenario = global_scenario_get();
	long index, positions_room = job->triangle_count, solids_room = 0, shapes_room = 0;
	long placements_room = 0, frames_room, firsts_room;
	struct sdf_scenery_kind kinds[SDF_SCENERY_KINDS];
	int kind;

	if (!scenario)
		return 1;
	sdf_scenery_kinds(scenario, kinds);
	for (kind = 0; kind < SDF_SCENERY_KINDS; kind++)
	for (index = 0; kinds[kind].placements->address && index < kinds[kind].placements->count &&
		job->placement_count < WORLD_SDF_SCENERY_MAXIMUM_PLACEMENTS; index++)
	{
		struct scenario_object_datum const *placement;
		long definition_index = sdf_scenery_placement(&kinds[kind], index, &placement), node_index;
		struct object_definition const *definition;
		struct model const *model;
		struct collision_model const *collision;
		struct world_proxies_frame frames[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
		real_matrix4x3 matrix, nodes[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
		short node_count;

		if (definition_index == NONE)
			continue;
		definition = object_definition_get(definition_index);
		model = model_definition_get(definition->object.model.index);
		collision = definition->object.collision_model.index != NONE ?
			collision_model_definition_get(definition->object.collision_model.index) : NULL;
		/* (its nodes where they rest, as the object's are: its matrix by each
		default pose's node) */
		matrix4x3_rotation_from_angles(&matrix, placement->rotation.yaw, placement->rotation.pitch,
			placement->rotation.roll);
		matrix.position = placement->position;
		node_count = (short)MIN(model->nodes.count, WORLD_PROXIES_SOURCE_MAXIMUM_NODES);
		for (node_index = 0; node_index < node_count; node_index++)
		{
			struct model_node const *model_node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
			real_matrix4x3 rest;

			matrix4x3_inverse(&model_node->runtime_default_inverse_matrix, &rest);
			matrix4x3_multiply(&matrix, &rest, &nodes[node_index]);
			sdf_frame_from(&nodes[node_index], &frames[node_index]);
		}
		frames_room = placements_room;
		firsts_room = placements_room;
		if (!sdf_grow((void **)&job->placements, &placements_room, job->placement_count, 1, sizeof(*job->placements)) ||
			!sdf_grow((void **)&job->placement_frames, &frames_room, job->placement_count, 1,
			sizeof(*job->placement_frames)) ||
			!sdf_grow((void **)&job->placement_shapes, &firsts_room, job->placement_count, 1,
			sizeof(*job->placement_shapes)) ||
			!sdf_grow((void **)&job->shapes, &shapes_room, job->shape_count, WORLD_PROXY_MAXIMUM_PER_DEFINITION,
			sizeof(*job->shapes)))
		{
			return 0;
		}
		job->placement_frames[job->placement_count] = frames[0];
		job->placement_shapes[job->placement_count] = job->shape_count;
		job->placements[job->placement_count].definition = definition_index;
		job->placements[job->placement_count].position[0] = placement->position.x;
		job->placements[job->placement_count].position[1] = placement->position.y;
		job->placements[job->placement_count].position[2] = placement->position.z;
		job->placement_count++;
		job->shape_count += world_proxies_place(definition_index, frames, node_count, job->shapes + job->shape_count,
			WORLD_PROXY_MAXIMUM_PER_DEFINITION);
		/* (each node's collision geometry, its first permutation's, as the
		proxies take it: its triangles in the field, its solid the sign's) */
		for (node_index = 0; collision && collision->nodes.address && node_index < collision->nodes.count &&
			node_index < node_count; node_index++)
		{
			struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&collision->nodes, node_index, struct collision_node);
			struct collision_bsp const *bsp;
			struct sdf_solid *solid;
			real_matrix4x3 inverse;
			long count, triangle, vertex;
			float low[3] = { 0.0f, 0.0f, 0.0f }, high[3] = { 0.0f, 0.0f, 0.0f };
			int axis;

			if (!node->bsps.address || node->bsps.count <= 0)
				continue;
			bsp = TAG_BLOCK_GET_ELEMENT(&node->bsps, 0, struct collision_bsp);
			count = world_scene_collision_fan(bsp, NULL);
			if (count <= 0)
				continue;
			if (!sdf_grow((void **)&job->positions, &positions_room, job->triangle_count, count, 9 * sizeof(float)) ||
				!sdf_grow((void **)&job->solids, &solids_room, job->solid_count, 1, sizeof(*job->solids)))
			{
				return 0;
			}
			world_scene_collision_fan(bsp, job->positions + job->triangle_count * 9);
			for (triangle = job->triangle_count; triangle < job->triangle_count + count; triangle++)
			{
				for (vertex = 0; vertex < 3; vertex++)
				{
					float local[3], *at = job->positions + triangle * 9 + vertex * 3;

					memcpy(local, at, sizeof(local));
					sdf_point_from(&nodes[node_index], local, at);
					for (axis = 0; axis < 3; axis++)
					{
						if (triangle == job->triangle_count && !vertex)
							low[axis] = high[axis] = at[axis];
						low[axis] = at[axis] < low[axis] ? at[axis] : low[axis];
						high[axis] = at[axis] > high[axis] ? at[axis] : high[axis];
					}
				}
			}
			job->triangle_count += count;
			solid = &job->solids[job->solid_count];
			memset(solid, 0, sizeof(*solid));
			if (!sdf_bsp_copy(bsp, &solid->bsp))
			{
				free(solid->bsp.nodes);
				free(solid->bsp.planes);
				return 0;
			}
			job->solid_count++;
			matrix4x3_inverse(&nodes[node_index], &inverse);
			for (axis = 0; axis < 3; axis++)
			{
				solid->to_local[axis][0] = inverse.scale * inverse.n[0][axis];
				solid->to_local[axis][1] = inverse.scale * inverse.n[1][axis];
				solid->to_local[axis][2] = inverse.scale * inverse.n[2][axis];
				solid->to_local[axis][3] = inverse.n[3][axis];
				solid->centre[axis] = 0.5f * (low[axis] + high[axis]);
			}
			solid->radius = 0.5f * sqrtf((high[0] - low[0]) * (high[0] - low[0]) + (high[1] - low[1]) * (high[1] - low[1]) +
				(high[2] - low[2]) * (high[2] - low[2])) + 0.01f;
		}
	}
	return 1;
}

/* ---------- the objects' copies for their occlusion fields (docs/world-ao.md,
"The objects' occlusion fields")

Two kinds of definition get one:
- each vehicle of the scenario's vehicle palette with collision geometry:
  its collision nodes' triangles (each node's first BSP) but its wheels'
  (which keep their spheres: they turn and spring), opaque;
- each piece of scenery, and each device that cannot move (no animations),
  of the scenario's scenery and devices' palettes placed in this BSP, whose
  proxies stand one box or sphere for it: the triangles it is drawn with
  (its model's most detailed geometry within WORLD_SDF_OBJECT_MOST_TRIANGLES),
  each with its shader's opacity. A box about a tree is solid where the
  tree is mostly air, and the ground inside it was wholly occluded: a dark
  square about every tree (#666). Its leaves are cards whose texture's alpha
  test cuts them out, so a card blocks the share of its texture that is
  drawn.
Both are in node 0's space at rest (the field rides on node 0), with their
collision nodes' solids (the sign's). The vehicles come first, then the
scenery the most placed first: when the atlas is full, the rest keep their
shapes. FALSE if no memory (none kept). One with no triangles keeps its
proxies' shapes */

/* whether a vehicle palette's definition gets an occlusion field (a
vehicle's model of nodes, with collision geometry) */
static int sdf_object_fielded(long definition_index, struct world_proxies_source *source)
{
	struct object_definition const *definition;
	struct model const *model;
	struct collision_model const *collision;

	return definition_index != NONE && (definition = object_definition_get(definition_index)) != NULL &&
		definition->object.model.index != NONE && definition->object.collision_model.index != NONE &&
		(model = model_definition_get(definition->object.model.index)) != NULL && model->nodes.address &&
		model->nodes.count > 0 &&
		(collision = collision_model_definition_get(definition->object.collision_model.index)) != NULL &&
		collision->nodes.address && world_proxies_source_definition(definition_index, source) &&
		source->shape_class == _world_proxies_source_vehicle;
}

/* whether a scenery or devices' palette definition gets an occlusion field
of its drawn triangles: a model of nodes, proxies of one box or sphere
(world_proxies.c's for scenery and devices; none for one too small), and a
device that cannot move (no animations: a door's field at rest would be
wrong once it opens) */
static int sdf_object_scenery(long definition_index, struct world_proxies_source *source)
{
	struct object_definition const *definition;
	struct model const *model;

	if (definition_index == NONE || !(definition = object_definition_get(definition_index)) ||
		definition->object.model.index == NONE || !(model = model_definition_get(definition->object.model.index)) ||
		!model->nodes.address || model->nodes.count <= 0 || !model->geometries.address || !model->regions.address ||
		!world_proxies_source_definition(definition_index, source) ||
		source->shape_class != _world_proxies_source_box || !world_proxies_definition(definition_index))
	{
		return 0;
	}
	if (definition->object.type != _object_type_scenery && definition->object.animation_graph.index != NONE)
	{
		struct animation_graph const *graph = animation_graph_definition_get(definition->object.animation_graph.index);

		if (graph && graph->animations.count > 0)
			return 0;
	}
	return 1;
}

/* the definitions that get occlusion fields, in the order the atlas takes
them: the vehicles', then the scenery's placed in this BSP, the most placed
first; how many (at most maximum). kinds[i]: 1 a vehicle, 0 scenery */
static long sdf_object_definitions(long *definitions, unsigned char *kinds, long maximum)
{
	struct scenario *scenario = global_scenario_get();
	struct world_proxies_source *source;
	struct sdf_scenery_kind palettes[SDF_SCENERY_KINDS];
	long count = 0, placed[WORLD_SDF_OBJECT_MAXIMUM], index, scenery_first;
	int kind;

	if (!scenario || !(source = malloc(sizeof(*source))))
		return 0;
	for (index = 0; scenario->vehicle_palette.address && index < scenario->vehicle_palette.count && count < maximum;
		index++)
	{
		long definition = TAG_BLOCK_GET_ELEMENT(&scenario->vehicle_palette, index,
			struct scenario_object_palette_entry)->reference.index, other;
		int known = 0;

		for (other = 0; other < count; other++)
			known = known || definitions[other] == definition;
		if (!known && sdf_object_fielded(definition, source))
		{
			kinds[count] = 1;
			definitions[count++] = definition;
		}
	}
	scenery_first = count;
	sdf_scenery_kinds(scenario, palettes);
	for (kind = 0; kind < SDF_SCENERY_KINDS; kind++)
	for (index = 0; palettes[kind].placements->address && index < palettes[kind].placements->count; index++)
	{
		struct scenario_object_datum const *placement = (struct scenario_object_datum const *)
			((byte const *)palettes[kind].placements->address + index * palettes[kind].size);
		long definition, other;

		if (placement->palette_entry_index < 0 || placement->palette_entry_index >= palettes[kind].palette->count ||
			!palettes[kind].palette->address)
		{
			continue;
		}
		definition = TAG_BLOCK_GET_ELEMENT(palettes[kind].palette, placement->palette_entry_index,
			struct scenario_object_palette_entry)->reference.index;
		if (definition == NONE || !sdf_placement_in_bsp(definition, placement))
			continue;
		for (other = scenery_first; other < count && definitions[other] != definition; other++)
			;
		if (other < count)
		{
			placed[other]++;
			continue;
		}
		if (count >= maximum || !sdf_object_scenery(definition, source))
			continue;
		kinds[count] = 0;
		placed[count] = 1;
		definitions[count++] = definition;
	}
	free(source);
	/* (the most placed first, then in the palettes' order: an insertion
	sort, stable) */
	for (index = scenery_first + 1; index < count; index++)
	{
		long definition = definitions[index], times = placed[index], at = index;

		while (at > scenery_first && placed[at - 1] < times)
		{
			definitions[at] = definitions[at - 1];
			placed[at] = placed[at - 1];
			at--;
		}
		definitions[at] = definition;
		placed[at] = times;
	}
	return count;
}

/* each node at rest in node 0's space: the root's inverse by the node's
rest (each the default pose's in the model's space) */
static short sdf_object_rest(struct model const *model, real_matrix4x3 rest[WORLD_PROXIES_SOURCE_MAXIMUM_NODES])
{
	short node_count = (short)MIN(model->nodes.count, WORLD_PROXIES_SOURCE_MAXIMUM_NODES), node_index;
	real_matrix4x3 inverse_root = TAG_BLOCK_GET_ELEMENT(&model->nodes, 0, struct model_node)->runtime_default_inverse_matrix;

	for (node_index = 0; node_index < node_count; node_index++)
	{
		struct model_node const *model_node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
		real_matrix4x3 at_rest;

		matrix4x3_inverse(&model_node->runtime_default_inverse_matrix, &at_rest);
		matrix4x3_multiply(&inverse_root, &at_rest, &rest[node_index]);
	}
	return node_count;
}

/* a point taken into a copy's bounds */
static void sdf_object_bound(struct sdf_object_copy *copy, float const point[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (!copy->bounded)
			copy->bounds[axis] = copy->bounds[3 + axis] = point[axis];
		copy->bounds[axis] = point[axis] < copy->bounds[axis] ? point[axis] : copy->bounds[axis];
		copy->bounds[3 + axis] = point[axis] > copy->bounds[3 + axis] ? point[axis] : copy->bounds[3 + axis];
	}
	copy->bounded = 1;
}

/* a copy's collision nodes' solids (the sign's point test), and with
triangles, their triangles (opaque) too, but a vehicle's wheels' (source's
nodes); FALSE if no memory */
static int sdf_object_collision_copy(struct sdf_object_copy *copy, struct collision_model const *collision,
	real_matrix4x3 const *rest, short node_count, struct world_proxies_source const *source, int triangles)
{
	long node_index, positions_room = copy->triangle_count, solids_room = copy->solid_count;

	for (node_index = 0; collision && collision->nodes.address && node_index < collision->nodes.count &&
		node_index < node_count; node_index++)
	{
		struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&collision->nodes, node_index, struct collision_node);
		struct collision_bsp const *bsp;
		struct sdf_solid *solid;
		real_matrix4x3 inverse;
		long count, triangle, vertex;
		float low[3] = { 0.0f, 0.0f, 0.0f }, high[3] = { 0.0f, 0.0f, 0.0f }, *at;
		int axis;

		if (source->nodes[node_index].wheel || !node->bsps.address || node->bsps.count <= 0)
			continue;
		bsp = TAG_BLOCK_GET_ELEMENT(&node->bsps, 0, struct collision_bsp);
		count = world_scene_collision_fan(bsp, NULL);
		if (count <= 0)
			continue;
		if (!sdf_grow((void **)&copy->positions, &positions_room, copy->triangle_count, count, 9 * sizeof(float)) ||
			!sdf_grow((void **)&copy->solids, &solids_room, copy->solid_count, 1, sizeof(*copy->solids)))
		{
			return 0;
		}
		/* (put after the copy's triangles, kept only with triangles) */
		at = copy->positions + copy->triangle_count * 9;
		world_scene_collision_fan(bsp, at);
		for (triangle = 0; triangle < count; triangle++)
		{
			for (vertex = 0; vertex < 3; vertex++)
			{
				float local[3], *point = at + triangle * 9 + vertex * 3;

				memcpy(local, point, sizeof(local));
				sdf_point_from(&rest[node_index], local, point);
				for (axis = 0; axis < 3; axis++)
				{
					if (!triangle && !vertex)
						low[axis] = high[axis] = point[axis];
					low[axis] = point[axis] < low[axis] ? point[axis] : low[axis];
					high[axis] = point[axis] > high[axis] ? point[axis] : high[axis];
				}
				if (triangles)
					sdf_object_bound(copy, point);
			}
		}
		if (triangles)
			copy->triangle_count += count;
		solid = &copy->solids[copy->solid_count];
		memset(solid, 0, sizeof(*solid));
		if (!sdf_bsp_copy(bsp, &solid->bsp))
		{
			free(solid->bsp.nodes);
			free(solid->bsp.planes);
			return 0;
		}
		copy->solid_count++;
		matrix4x3_inverse(&rest[node_index], &inverse);
		for (axis = 0; axis < 3; axis++)
		{
			solid->to_local[axis][0] = inverse.scale * inverse.n[0][axis];
			solid->to_local[axis][1] = inverse.scale * inverse.n[1][axis];
			solid->to_local[axis][2] = inverse.scale * inverse.n[2][axis];
			solid->to_local[axis][3] = inverse.n[3][axis];
			solid->centre[axis] = 0.5f * (low[axis] + high[axis]);
		}
		solid->radius = 0.5f * sqrtf((high[0] - low[0]) * (high[0] - low[0]) + (high[1] - low[1]) * (high[1] - low[1]) +
			(high[2] - low[2]) * (high[2] - low[2])) + 0.01f;
	}
	return 1;
}

/* this build's model geometry and part (models.c's; object_mesh.c reads
them the same way) and a model's shader reference */
struct sdf_model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct sdf_model_part
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

struct sdf_model_shader
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

/* a model shader's flags and base map (rasterizer_xbox_models.c's) */
struct sdf_shader_model
{
	struct shader shader;
	word flags;
	byte reserved[0x7A];
	struct tag_reference base_map;
};

typedef char verify_sdf_model_part_size[sizeof(struct sdf_model_part) == 0x68 ? 1 : -1];
typedef char verify_sdf_model_shader_size[sizeof(struct sdf_model_shader) == 0x20 ? 1 : -1];
typedef char verify_sdf_shader_model_flags[offsetof(struct sdf_shader_model, flags) == 0x28 ? 1 : -1];
typedef char verify_sdf_shader_model_base_map[offsetof(struct sdf_shader_model, base_map) == 0xA4 ? 1 : -1];

enum
{
	/* a part the game does not draw */
	_sdf_model_part_stripped_bit = 0,
	/* a model shader (shader_definitions.h's type), and its flag drawn
	without its base map's alpha test */
	_sdf_shader_type_model = 4,
	_sdf_shader_model_not_alpha_tested_bit = 2,
	/* a model's detail levels (super low to super high) */
	SDF_MODEL_DETAIL_LEVELS = 5,
};

/* the share of a bitmap's first texture's texels its alpha test draws
(alpha over 0x7F, the models' D3DRS_ALPHAREF: rasterizer_xbox_models.c),
at its first level no larger than 512 a side; 1 if it cannot be read (a
solid card). Its texels are loaded as a draw loads them, and waited for */
static float sdf_bitmap_coverage(long bitmap_tag)
{
	struct bitmap_group *group;
	struct bitmap_data const *bitmap;
	DWORD const *header;

	if (bitmap_tag == NONE || !(group = bitmap_group_get(bitmap_tag)) || !group->bitmaps.address ||
		group->bitmaps.count <= 0)
	{
		return 1.0f;
	}
	header = texture_cache_port_load((unsigned long)bitmap_tag, 0);
	bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
	if (!header || !bitmap->base_address)
		return 1.0f;
	return xgpu_texture_alpha_coverage((unsigned int)header[3], (unsigned int)header[4],
		(unsigned char const *)bitmap->base_address, 0x7F, 512);
}

/* a shader's opacity in a field: 0 for none (transparent: glass,
effects), 1 for an opaque surface, and an alpha-tested model shader's base
map's coverage (its leaves' cards); remembered in the job by its tag */
static float sdf_shader_opacity(struct sdf_job *job, long shader_tag)
{
	struct shader *shader;
	float opacity = 1.0f;
	long index;

	if (shader_tag == NONE)
		return 0.0f;
	for (index = 0; index < job->opacity_count; index++)
	{
		if (job->opacity_tags[index] == shader_tag)
			return job->opacities[index];
	}
	shader = shader_definition_get(shader_tag);
	if (!shader || !shader_type_is_valid_for_model(shader->base.type) || shader_type_is_transparent(shader->base.type))
		opacity = 0.0f;
	else if (shader->base.type == _sdf_shader_type_model)
	{
		struct sdf_shader_model const *model = (struct sdf_shader_model const *)shader;

		if (!TEST_FLAG(model->flags, _sdf_shader_model_not_alpha_tested_bit))
			opacity = sdf_bitmap_coverage(model->base_map.index);
	}
	if (job->opacity_count < (long)(sizeof(job->opacity_tags) / sizeof(job->opacity_tags[0])))
	{
		job->opacity_tags[job->opacity_count] = shader_tag;
		job->opacities[job->opacity_count++] = opacity;
	}
	return opacity;
}

/* the geometry a region's first permutation draws at a detail level (NONE:
none) */
static short sdf_model_geometry(struct model const *model, short region_index, short level)
{
	struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
	struct model_region_permutation const *permutation;
	short geometry;

	if (!region->permutations.address || region->permutations.count <= 0)
		return NONE;
	permutation = TAG_BLOCK_GET_ELEMENT(&region->permutations, 0, struct model_region_permutation);
	geometry = permutation->geometry_indices[level];
	return geometry >= 0 && geometry < model->geometries.count ? geometry : NONE;
}

/* the triangles a model draws at a detail level, about (its parts' index
counts: a strip's count is its triangles' and its turns'); -1 if it draws
none at that level */
static long sdf_model_level_triangles(struct model const *model, short level)
{
	long total = 0;
	short region_index, part_index;
	int any = 0;

	for (region_index = 0; region_index < model->regions.count; region_index++)
	{
		short geometry_index = sdf_model_geometry(model, region_index, level);
		struct sdf_model_geometry const *geometry;

		if (geometry_index == NONE)
			continue;
		geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct sdf_model_geometry);
		for (part_index = 0; geometry->parts.address && part_index < geometry->parts.count &&
			part_index < MAXIMUM_PARTS_PER_MODEL_GEOMETRY; part_index++)
		{
			total += TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct sdf_model_part)->triangle_buffer.count;
			any = 1;
		}
	}
	return any ? total : -1;
}

/* a part's drawn triangles onto a copy (model space to node 0's by
to_root), each with its shader's opacity; FALSE if no memory */
static int sdf_object_part_copy(struct sdf_job *job, struct sdf_object_copy *copy, struct model const *model,
	struct sdf_model_part const *part, real_matrix4x3 const *to_root, long *room)
{
	struct vertex_buffer const *vertex_buffer = &part->vertex_buffer;
	struct triangle_buffer const *triangle_buffer = &part->triangle_buffer;
	byte *vertices = NULL;
	BYTE *indices = NULL;
	long stride, index_count, index;
	float opacity;
	int good = 1;

	if (TEST_FLAG(part->flags, _sdf_model_part_stripped_bit) || part->shader_index < 0 ||
		part->shader_index >= model->shaders.count || !model->shaders.address)
	{
		return 1;
	}
	opacity = sdf_shader_opacity(job, TAG_BLOCK_GET_ELEMENT(&model->shaders, part->shader_index,
		struct sdf_model_shader)->shader.index);
	if (opacity <= 0.0f || !vertex_buffer->hardware_format || vertex_buffer->count <= 0 ||
		!triangle_buffer->hardware_format || triangle_buffer->count <= 0)
	{
		return 1;
	}
	if (vertex_buffer->type == _rasterizer_vertex_type_model_compressed)
		stride = sizeof(struct model_vertex_compressed);
	else if (vertex_buffer->type == _rasterizer_vertex_type_model_uncompressed)
		stride = sizeof(struct model_vertex_uncompressed);
	else
		return 1;
	if (triangle_buffer->type == _triangle_buffer_type_precompiled_strip)
		index_count = triangle_buffer->count + 2;
	else if (triangle_buffer->type == _triangle_buffer_type_triangles)
		index_count = triangle_buffer->count * 3;
	else
		return 1;
	IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format, 0, 0, &vertices,
		D3DLOCK_READONLY);
	IDirect3DIndexBuffer8_Lock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format, 0, 0, &indices,
		D3DLOCK_READONLY);
	if (vertices && indices)
	{
		unsigned short const *list = (unsigned short const *)indices;
		long step = triangle_buffer->type == _triangle_buffer_type_precompiled_strip ? 1 : 3;

		for (index = 0; good && index + 2 < index_count; index += step)
		{
			unsigned short corners[3];
			float *out;
			int corner, axis;

			corners[0] = list[index];
			corners[1] = list[index + 1];
			corners[2] = list[index + 2];
			/* (a strip's turns: its degenerate triangles) */
			if (corners[0] == corners[1] || corners[1] == corners[2] || corners[0] == corners[2] ||
				corners[0] >= vertex_buffer->count || corners[1] >= vertex_buffer->count ||
				corners[2] >= vertex_buffer->count)
			{
				continue;
			}
			if (!sdf_grow((void **)&copy->positions, room, copy->triangle_count, 1, 9 * sizeof(float)) ||
				!sdf_grow((void **)&copy->opacities, &copy->opacities_room, copy->triangle_count, 1, sizeof(float)))
			{
				good = 0;
				break;
			}
			out = copy->positions + copy->triangle_count * 9;
			for (corner = 0; corner < 3; corner++)
			{
				/* (both kinds of vertex start with the position, in the model's
				space) */
				real_point3d const *position = (real_point3d const *)(vertices + corners[corner] * stride);
				real_point3d point;

				matrix4x3_transform_point(to_root, position, &point);
				out[corner * 3] = point.x;
				out[corner * 3 + 1] = point.y;
				out[corner * 3 + 2] = point.z;
				for (axis = 0; axis < 3; axis++)
					good = good && out[corner * 3 + axis] == out[corner * 3 + axis] &&
						fabsf(out[corner * 3 + axis]) < 1.0e6f;
			}
			if (!good)
			{
				/* (a vertex not a number: left out) */
				good = 1;
				continue;
			}
			for (corner = 0; corner < 3; corner++)
				sdf_object_bound(copy, out + corner * 3);
			copy->opacities[copy->triangle_count++] = opacity;
		}
	}
	if (vertices)
		IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format);
	if (indices)
		IDirect3DIndexBuffer8_Unlock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format);
	return good;
}

/* a piece of scenery's drawn triangles (its regions' first permutations,
at the most detailed level within WORLD_SDF_OBJECT_MOST_TRIANGLES, else
the least), each with its shader's opacity, in node 0's space; FALSE if no
memory */
static int sdf_object_render_copy(struct sdf_job *job, struct sdf_object_copy *copy, struct model const *model)
{
	real_matrix4x3 to_root = TAG_BLOCK_GET_ELEMENT(&model->nodes, 0, struct model_node)->runtime_default_inverse_matrix;
	long room = copy->triangle_count, least = -1, triangles;
	short level, chosen = NONE, region_index, part_index;

	for (level = SDF_MODEL_DETAIL_LEVELS - 1; level >= 0; level--)
	{
		triangles = sdf_model_level_triangles(model, level);
		if (triangles < 0)
			continue;
		if (triangles <= WORLD_SDF_OBJECT_MOST_TRIANGLES)
		{
			chosen = level;
			break;
		}
		if (least < 0 || triangles < least)
		{
			least = triangles;
			chosen = level;
		}
	}
	if (chosen == NONE)
		return 1;
	for (region_index = 0; region_index < model->regions.count; region_index++)
	{
		short geometry_index = sdf_model_geometry(model, region_index, chosen);
		struct sdf_model_geometry const *geometry;

		if (geometry_index == NONE)
			continue;
		geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct sdf_model_geometry);
		for (part_index = 0; geometry->parts.address && part_index < geometry->parts.count &&
			part_index < MAXIMUM_PARTS_PER_MODEL_GEOMETRY; part_index++)
		{
			if (!sdf_object_part_copy(job, copy, model, TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index,
				struct sdf_model_part), &to_root, &room))
			{
				return 0;
			}
		}
	}
	return 1;
}

static int sdf_objects_copy(struct sdf_job *job)
{
	struct world_proxies_source *source;
	long definitions[WORLD_SDF_OBJECT_MAXIMUM], count, index;
	unsigned char kinds[WORLD_SDF_OBJECT_MAXIMUM];

	count = sdf_object_definitions(definitions, kinds, WORLD_SDF_OBJECT_MAXIMUM);
	if (!count)
		return 1;
	job->object_copies = calloc(WORLD_SDF_OBJECT_MAXIMUM, sizeof(*job->object_copies));
	source = malloc(sizeof(*source));
	if (!job->object_copies || !source)
	{
		free(source);
		return 0;
	}
	for (index = 0; index < count; index++)
	{
		long definition_index = definitions[index];
		struct object_definition const *definition = object_definition_get(definition_index);
		struct model const *model = model_definition_get(definition->object.model.index);
		struct collision_model const *collision = definition->object.collision_model.index != NONE ?
			collision_model_definition_get(definition->object.collision_model.index) : NULL;
		struct sdf_object_copy *copy = &job->object_copies[job->object_count];
		real_matrix4x3 rest[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
		short node_count;
		int good;

		if (!world_proxies_source_definition(definition_index, source))
			continue;
		memset(copy, 0, sizeof(*copy));
		copy->definition = definition_index;
		copy->scenery = !kinds[index];
		snprintf(copy->name, sizeof(copy->name), "%s", tag_get_name(definition_index));
		node_count = sdf_object_rest(model, rest);
		/* (a vehicle: its collision's triangles, opaque; a piece of scenery:
		its drawn ones, and its collision's solids alone) */
		good = kinds[index] ? sdf_object_collision_copy(copy, collision, rest, node_count, source, 1) :
			sdf_object_render_copy(job, copy, model) &&
			sdf_object_collision_copy(copy, collision, rest, node_count, source, 0);
		job->object_count++;
		if (!good)
		{
			free(source);
			return 0;
		}
		/* (none: its shapes, as before) */
		if (copy->triangle_count <= 0)
		{
			job->object_count--;
			sdf_object_copy_free(copy);
			memset(copy, 0, sizeof(*copy));
		}
	}
	free(source);
	return 1;
}

/* the BSP's lightmap pages' triangles copied (each surface's corners'
places, normals and lightmap coordinates, as the lightmap pass draws them),
a page for each of its lightmap bitmap's; a lightmap whose vertices are not
as many as its surfaces' cannot be baked (its page is not usable); FALSE if
no memory */
static int sdf_pages_copy(struct sdf_job *job)
{
	struct structure_bsp const *structure = global_structure_bsp_get();
	struct bitmap_group const *group;
	long *rooms, lightmap_index;
	long vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_compressed);
	long lightmap_vertex_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_environment_lightmap_compressed);

	if (!structure || structure->lightmap_group.index == NONE || !structure->lightmaps.address ||
		!structure->surfaces.address || !(group = bitmap_group_get(structure->lightmap_group.index)) ||
		group->bitmaps.count <= 0)
	{
		return 1;
	}
	job->page_count = MIN(group->bitmaps.count, WORLD_SDF_SCENERY_MAXIMUM_PAGES);
	job->pages = calloc((size_t)job->page_count, sizeof(*job->pages));
	rooms = calloc((size_t)job->page_count, sizeof(*rooms));
	if (!job->pages || !rooms)
	{
		free(rooms);
		job->page_count = 0;
		return 0;
	}
	for (lightmap_index = 0; lightmap_index < job->page_count; lightmap_index++)
		job->pages[lightmap_index].usable = 1;
	for (lightmap_index = 0; lightmap_index < structure->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap const *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, lightmap_index,
			struct structure_lightmap);
		struct sdf_page *page;
		long material_index;

		if (lightmap->bitmap_index < 0 || lightmap->bitmap_index >= job->page_count || !lightmap->materials.address)
			continue;
		page = &job->pages[lightmap->bitmap_index];
		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material const *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			byte const *vertices = material->compressed_vertex_data.address;
			long surface, corner, axis;

			if (material->surface_count <= 0)
				continue;
			if (!vertices || material->vertices.count <= 0 || material->lightmap_vertices.count != material->vertices.count ||
				material->compressed_vertex_data.size < material->vertices.count * (vertex_size + lightmap_vertex_size) ||
				material->first_surface_index < 0 ||
				material->first_surface_index + material->surface_count > structure->surfaces.count)
			{
				page->usable = 0;
				continue;
			}
			if (!sdf_grow((void **)&page->triangles, &rooms[lightmap->bitmap_index], page->triangle_count,
				material->surface_count, WORLD_AO_SCENERY_TRIANGLE * sizeof(float)))
			{
				free(rooms);
				return 0;
			}
			for (surface = 0; surface < material->surface_count; surface++)
			{
				struct structure_surface const *triangle = TAG_BLOCK_GET_ELEMENT(&structure->surfaces,
					material->first_surface_index + surface, struct structure_surface);
				float *out = page->triangles + page->triangle_count * WORLD_AO_SCENERY_TRIANGLE;
				int good = 1;

				for (corner = 0; corner < 3; corner++)
				{
					long vertex = triangle->vertex_indices[corner];
					real_point3d point;
					real_vector3d normal;
					real_point2d texcoord;

					if (vertex < 0 || vertex >= material->vertices.count)
					{
						good = 0;
						break;
					}
					environment_vertex_compressed_get_point((struct environment_vertex_compressed const *)
						(vertices + vertex * vertex_size), &point);
					environment_vertex_compressed_get_normal((struct environment_vertex_compressed const *)
						(vertices + vertex * vertex_size), &normal);
					environment_lightmap_vertex_compressed_get_texcoord((struct environment_lightmap_vertex_compressed const *)
						(vertices + material->vertices.count * vertex_size + vertex * lightmap_vertex_size), &texcoord);
					out[corner * 3 + 0] = point.x;
					out[corner * 3 + 1] = point.y;
					out[corner * 3 + 2] = point.z;
					out[9 + corner * 3 + 0] = normal.i;
					out[9 + corner * 3 + 1] = normal.j;
					out[9 + corner * 3 + 2] = normal.k;
					out[18 + corner * 2 + 0] = texcoord.x;
					out[18 + corner * 2 + 1] = texcoord.y;
					for (axis = 0; axis < 3; axis++)
					{
						float value = out[corner * 3 + axis];

						if (!page->triangle_count && !corner)
							page->bounds[axis] = page->bounds[3 + axis] = value;
						page->bounds[axis] = value < page->bounds[axis] ? value : page->bounds[axis];
						page->bounds[3 + axis] = value > page->bounds[3 + axis] ? value : page->bounds[3 + axis];
					}
				}
				page->triangle_count += good;
			}
		}
	}
	free(rooms);
	return 1;
}

/* the static scenery placements the current BSP's field will hold (as
sdf_scenery_copy finds them), while it is on its way: the window leaves
them out until it is drawn, then they fade in with it. Shading them as
proxies meanwhile (b30: 592 shapes, the window's 64 nearest walked by every
pixel of the level) cost an Apple M2's GPU six times the frame the field
does */
static int sdf_placement_order(void const *a, void const *b);

static void sdf_awaited_make(void)
{
	struct scenario *scenario = global_scenario_get();
	struct sdf_scenery_kind kinds[SDF_SCENERY_KINDS];
	long room = 0, index;
	int kind;

	world_sdf.awaited_made = 1;
	if (!scenario)
		return;
	sdf_scenery_kinds(scenario, kinds);
	for (kind = 0; kind < SDF_SCENERY_KINDS; kind++)
	for (index = 0; kinds[kind].placements->address && index < kinds[kind].placements->count &&
		world_sdf.awaited_count < WORLD_SDF_SCENERY_MAXIMUM_PLACEMENTS; index++)
	{
		struct scenario_object_datum const *placement;
		long definition = sdf_scenery_placement(&kinds[kind], index, &placement);
		struct world_sdf_placement *awaited;

		if (definition == NONE)
			continue;
		if (!sdf_grow((void **)&world_sdf.awaited, &room, (long)world_sdf.awaited_count, 1, sizeof(*world_sdf.awaited)))
		{
			free(world_sdf.awaited);
			world_sdf.awaited = NULL;
			world_sdf.awaited_count = 0;
			return;
		}
		awaited = &world_sdf.awaited[world_sdf.awaited_count++];
		awaited->definition = definition;
		awaited->position[0] = placement->position.x;
		awaited->position[1] = placement->position.y;
		awaited->position[2] = placement->position.z;
	}
	if (world_sdf.awaited_count)
		qsort(world_sdf.awaited, world_sdf.awaited_count, sizeof(*world_sdf.awaited), sdf_placement_order);
	/* (the vehicles and the scenery, as sdf_objects_copy finds them) */
	{
		unsigned char vehicles[WORLD_SDF_OBJECT_MAXIMUM];

		world_sdf.awaited_object_count = sdf_object_definitions(world_sdf.awaited_objects, vehicles,
			WORLD_SDF_OBJECT_MAXIMUM);
	}
}

/* the loaded map's BSP's geometry and planes copied, and its bake queued;
FALSE if they cannot be (no geometry: the proxies alone) */
static int sdf_bake_begin(struct sdf_key const *key, int startup)
{
	struct world_scene_triangles triangles;
	struct collision_bsp const *collision = global_collision_bsp_get();
	struct sdf_job *job;
	unsigned long long began = SDL_GetTicksNS();

	if (sdf_bake_queued(key))
		return 1;
	/* (HALO_WORLD_SDF_NO_BAKE, for measuring: none baked, as if one were
	on its way for good; the frames without a bake's threads) */
	if (getenv("HALO_WORLD_SDF_NO_BAKE"))
		return 1;
	if (!collision || !world_scene_static_triangles(key->bsp, &triangles))
		return 0;
	if (triangles.count <= 0 || !(job = calloc(1, sizeof(*job))))
	{
		world_scene_triangles_free(&triangles);
		return 0;
	}
	job->kind = _sdf_job_bake;
	job->key = *key;
	job->startup = startup;
	/* (copied again: world_scene.c's copy is the game's allocator's, let
	go of here, on the game's thread) */
	job->positions = malloc((size_t)triangles.count * 9 * sizeof(float));
	if (!job->positions)
	{
		world_scene_triangles_free(&triangles);
		sdf_job_free(job);
		return 0;
	}
	memcpy(job->positions, triangles.positions, (size_t)triangles.count * 9 * sizeof(float));
	job->triangle_count = triangles.count;
	memcpy(job->bounds, triangles.bounds, sizeof(job->bounds));
	world_scene_triangles_free(&triangles);
	sdf_placements_clip(job->bounds);
	if (!sdf_bsp_copy(collision, &job->bsp) || !sdf_scenery_copy(job) || !sdf_pages_copy(job) || !sdf_objects_copy(job))
	{
		sdf_job_free(job);
		return 0;
	}
	job->copy_milliseconds = (double)(SDL_GetTicksNS() - began) / 1e6;
	sdf_queue(job);
	return 1;
}

/* (the placements in order of x, then y, for world_sdf_scenery_baked's
search: sdf_results sorts a field's as it takes it) */
static int sdf_placement_order(void const *a, void const *b)
{
	struct world_sdf_placement const *p = a, *q = b;

	if (p->position[0] != q->position[0])
		return p->position[0] < q->position[0] ? -1 : 1;
	if (p->position[1] != q->position[1])
		return p->position[1] < q->position[1] ? -1 : 1;
	return 0;
}

/* ---------- the field's upload */

/* the atlas's layers uploaded (at least one: the backend's atlas is never
empty) */
static unsigned long sdf_upload_layers(struct world_sdf_header const *header)
{
	unsigned long layers = world_sdf_atlas_layers(header);

	return layers ? layers : 1;
}

static int sdf_upload_done(struct world_sdf_volume const *volume, unsigned long pages, unsigned long objects,
	unsigned long cells, unsigned long layers)
{
	return pages >= volume->scenery.layer_count && objects >= volume->objects.atlas_depth &&
		cells >= volume->header.depth && layers >= sdf_upload_layers(&volume->header);
}

/* a z slice of the objects' fields' atlas (atlas_width x atlas_height
texels of 4 halves, x fastest; zeros where no object is: no occlusion) */
static void sdf_objects_slice(struct world_sdf_objects const *objects, unsigned long z, unsigned char *slice)
{
	unsigned long row = objects->atlas_width * WORLD_SDF_OBJECT_TEXEL, index, y, x;

	memset(slice, 0, row * objects->atlas_height);
	for (index = 0; index < objects->count; index++)
	{
		struct world_sdf_object const *object = &objects->objects[index];

		if (object->atlas[0] == ~0UL || z < object->atlas[2] || z >= object->atlas[2] + object->depth)
			continue;
		for (y = 0; y < object->height; y++)
		{
			unsigned short const *from = object->texels + ((z - object->atlas[2]) * object->height + y) * object->width * 4;
			unsigned char *to = slice + (object->atlas[1] + y) * row + object->atlas[0] * WORLD_SDF_OBJECT_TEXEL;

			for (x = 0; x < object->width * 4; x++)
			{
				to[x * 2] = (unsigned char)(from[x] & 0xFF);
				to[x * 2 + 1] = (unsigned char)(from[x] >> 8);
			}
		}
	}
}

/* a field handed on to the backend, at most about budget bytes of it: its
table's z slices (from *cells), then its atlas's layers (from *layers, each
made in layer); TRUE once it all has been */
static int sdf_upload(struct world_sdf_volume const *volume, unsigned long *pages, unsigned long *objects,
	unsigned long *cells, unsigned long *layers, signed char *layer, unsigned char *objects_slice, unsigned long budget)
{
	struct world_sdf_objects const *fields = &volume->objects;
	struct world_sdf_header const *header = &volume->header;
	struct world_sdf_scenery const *scenery = &volume->scenery;
	unsigned long slice = header->width * header->height * WORLD_SDF_CELL_BYTES, sent = 0;
	unsigned long side = WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN, total = sdf_upload_layers(header);

	/* (the static scenery's pages first, a layer at a time: a new field's
	table after them lets go of the atlas before it, not of them) */
	while (*pages < scenery->layer_count && sent < budget)
	{
		unsigned long page_bytes = scenery->width * scenery->height;

		halo_world_ao_field(WORLD_AO_FIELD_SCENERY, scenery->width, scenery->height, scenery->layer_count, *pages, 1,
			scenery->texels + *pages * page_bytes);
		*pages += 1;
		sent += page_bytes;
	}
	/* (then the objects' fields' atlas, a slice at a time) */
	while (*pages >= scenery->layer_count && *objects < fields->atlas_depth && sent < budget && objects_slice)
	{
		sdf_objects_slice(fields, *objects, objects_slice);
		halo_world_ao_field(WORLD_AO_FIELD_OBJECTS, fields->atlas_width, fields->atlas_height, fields->atlas_depth,
			*objects, 1, objects_slice);
		*objects += 1;
		sent += fields->atlas_width * fields->atlas_height * WORLD_SDF_OBJECT_TEXEL;
	}
	while (*pages >= scenery->layer_count && *objects >= fields->atlas_depth && *cells < header->depth &&
		sent < budget)
	{
		unsigned long count = (budget - sent) / slice;

		if (count < 1)
			count = 1;
		if (count > header->depth - *cells)
			count = header->depth - *cells;
		halo_world_ao_field(WORLD_AO_FIELD_TABLE, header->width, header->height, header->depth, *cells, count,
			volume->cells[*cells * header->width * header->height]);
		*cells += count;
		sent += count * slice;
	}
	while (*cells >= header->depth && *layers < total && sent < budget)
	{
		world_sdf_atlas_layer(volume, *layers, layer);
		halo_world_ao_field(WORLD_AO_FIELD_ATLAS, side, side, total * WORLD_SDF_BRICK_SPAN,
			*layers * WORLD_SDF_BRICK_SPAN, WORLD_SDF_BRICK_SPAN, layer);
		*layers += 1;
		sent += WORLD_SDF_ATLAS_LAYER_BYTES;
	}
	return sdf_upload_done(volume, *pages, *objects, *cells, *layers);
}

/* the bytes of one slice of a field's vehicles' atlas (malloc'd for it) */
static unsigned long sdf_objects_slice_bytes(struct world_sdf_objects const *objects)
{
	return objects->atlas_width * objects->atlas_height * WORLD_SDF_OBJECT_TEXEL + 1;
}

void world_sdf_upload(struct world_sdf_volume const *volume)
{
	signed char *layer = malloc(WORLD_SDF_ATLAS_LAYER_BYTES);
	unsigned char *slice = malloc(sdf_objects_slice_bytes(&volume->objects));
	unsigned long pages = 0, objects = 0, cells = 0, layers = 0;

	if (layer && slice)
		sdf_upload(volume, &pages, &objects, &cells, &layers, layer, slice, ~0UL);
	free(layer);
	free(slice);
}

/* the field let go of (and the backend's) */
static void sdf_field_release(void)
{
	if (world_sdf.field_sent)
		halo_world_ao_field(WORLD_AO_FIELD_TABLE, 0, 0, 0, 0, 0, NULL);
	world_sdf.field_sent = 0;
	sdf_volume_free(&world_sdf.field);
	memset(&world_sdf.field, 0, sizeof(world_sdf.field));
	world_sdf.uploaded_pages = world_sdf.uploaded_cells = world_sdf.uploaded_layers = 0;
	world_sdf.uploaded_objects = 0;
	free(world_sdf.objects_slice);
	world_sdf.objects_slice = NULL;
	world_sdf.fade = 0.0f;
	world_sdf.faded_from = 0;
	free(world_sdf.awaited);
	world_sdf.awaited = NULL;
	world_sdf.awaited_count = 0;
	world_sdf.awaited_made = 0;
	world_sdf.awaited_object_count = 0;
}

/* the map's BSP drawn from now on: looked for in the cache (by the looker:
nothing read here) */
static void sdf_current_set(struct sdf_key const *key)
{
	struct sdf_job *job;

	sdf_field_release();
	world_sdf.current = *key;
	world_sdf.map_known = 1;
	/* (the load's frame not timed) */
	sdf_frame_last = 0;
	world_sdf.copy_wanted = 0;
	world_sdf.state = _world_sdf_baking;
	job = calloc(1, sizeof(*job));
	if (!job)
	{
		world_sdf.state = _world_sdf_failed;
		return;
	}
	job->kind = _sdf_job_look;
	job->key = *key;
	sdf_queue(job);
}

/* the threads' results taken: the current BSP's field kept (not this frame
if a bake's thread holds the lock: the game's thread never waits for one,
which may be held off its processor for a while at the lowest priority) */
static void sdf_results(void)
{
	struct sdf_job *jobs, *job;

	if (pthread_mutex_trylock(&world_sdf_lock) != 0)
		return;
	jobs = world_sdf.results;
	world_sdf.results = NULL;
	pthread_mutex_unlock(&world_sdf_lock);
	while ((job = jobs) != NULL)
	{
		jobs = job->next;
		if (world_sdf.map_known && sdf_key_same(&job->key, &world_sdf.current) && !world_sdf.field.cells)
		{
			switch (job->result)
			{
			case _sdf_result_found:
			case _sdf_result_baked:
				if (job->result == _sdf_result_found)
					platform_log("world sdf: %s bsp %d: its field cached (baked in %lu ms on %lu threads)", job->key.map,
						(int)job->key.bsp, job->volume.header.bake_milliseconds, job->volume.header.bake_threads);
				world_sdf.field = job->volume;
				job->volume.cells = NULL;
				job->volume.bricks = NULL;
				memset(&job->volume.scenery, 0, sizeof(job->volume.scenery));
				memset(&job->volume.objects, 0, sizeof(job->volume.objects));
				if (world_sdf.field.scenery.placement_count)
					qsort(world_sdf.field.scenery.placements, world_sdf.field.scenery.placement_count,
						sizeof(*world_sdf.field.scenery.placements), sdf_placement_order);
				world_sdf.uploaded_pages = world_sdf.uploaded_cells = world_sdf.uploaded_layers = 0;
				world_sdf.uploaded_objects = 0;
				world_sdf.state = _world_sdf_ready;
				break;
			case _sdf_result_missing:
				/* (baked from a copy, a few frames from now: never in the load) */
				world_sdf.copy_wanted = 1;
				world_sdf.copy_frames = WORLD_SDF_COPY_DELAY_FRAMES;
				break;
			default:
				world_sdf.state = _world_sdf_failed;
				break;
			}
		}
		sdf_job_free(job);
	}
}

void world_sdf_map_loaded(void)
{
	struct sdf_key key;

	if (sdf_off() || !sdf_key_loaded(&key))
		return;
	/* (measured: debug.load_trace's world_sdf.map_loaded) */
	halo_load_trace("world_sdf.map_loaded.begin", key.map);
	sdf_current_set(&key);
	halo_load_trace("world_sdf.map_loaded.end", key.map);
}

void world_sdf_map_unloaded(void)
{
	/* (a bake keeps going: it has copies, and its field is cached) */
	sdf_field_release();
	world_sdf.map_known = 0;
	world_sdf.copy_wanted = 0;
	world_sdf.state = _world_sdf_none;
}

void world_sdf_bsp_switched(short bsp_index)
{
	struct sdf_key key;

	if (sdf_off() || !sdf_key_loaded(&key))
		return;
	key.bsp = bsp_index < 0 ? 0 : bsp_index;
	if (world_sdf.map_known && sdf_key_same(&key, &world_sdf.current))
		return;
	sdf_current_set(&key);
}

/* the frames: their times pace the bake in play (world_sdf_pace_frame),
and with HALO_WORLD_SDF_FRAMES they are logged while a bake runs and while
none does (for measuring: no frame should be slower for a bake; nor any
of the frames after a load for the world's pipelines, made apart from
them). A frame that prepared a map's pipelines (world_sdf_frame_prepared),
and the first after a map's load, are the load's: not timed */

/* updater.c's, menu_functions.c's: a game played (its pause menu too), not
the main menu */
int pc_menu_game_playing(void);

void world_sdf_frame_prepared(void)
{
	sdf_frame_prepared = 1;
}

static void sdf_frames_measured(double milliseconds, int resting)
{
	static int wanted = -1;
	static double worst[2], total[2];
	static long count[2], rested;
	/* (a bake running: not one waiting for the start-up pass to end) */
	int baking = __atomic_load_n(&world_sdf.baking, __ATOMIC_RELAXED) != 0;

	if (wanted < 0)
		wanted = getenv("HALO_WORLD_SDF_FRAMES") != NULL;
	if (!wanted)
		return;
	if (milliseconds > worst[baking])
		worst[baking] = milliseconds;
	total[baking] += milliseconds;
	count[baking]++;
	rested += baking && resting;
	if (count[0] + count[1] >= 600)
	{
		platform_log("world sdf: frames while baking %ld (mean %.2f ms, worst %.2f ms; the bake resting in %ld), while "
			"not %ld (mean %.2f ms, worst %.2f ms)", count[1], count[1] ? total[1] / count[1] : 0.0, worst[1], rested,
			count[0], count[0] ? total[0] / count[0] : 0.0, worst[0]);
		worst[0] = worst[1] = total[0] = total[1] = 0.0;
		count[0] = count[1] = rested = 0;
	}
}

/* this frame's time, paced and measured; the pace told to the bake's
threads */
static void sdf_frame_timed(void)
{
	unsigned long long now = SDL_GetTicksNS();
	int running = __atomic_load_n(&sdf_pace_running, __ATOMIC_RELAXED);

	if (sdf_frame_prepared)
	{
		sdf_frame_prepared = 0;
		sdf_frame_last = 0;
	}
	if (sdf_frame_last)
	{
		double milliseconds = (double)(now - sdf_frame_last) / 1e6;

		running = world_sdf_pace_frame(&sdf_pace, (float)milliseconds, pc_menu_game_playing());
		sdf_frames_measured(milliseconds, !running);
	}
	sdf_frame_last = now;
	__atomic_store_n(&sdf_pace_running, running, __ATOMIC_RELAXED);
	__atomic_store_n(&sdf_pace_said, now, __ATOMIC_RELAXED);
}

/* the start-up pass's bakes all done (or dropped: a failed bake is never
counted, so once its results are in): their time, after the pass, and the
pass's total with them */
static void sdf_startup_total(void)
{
	unsigned long milliseconds, count;
	int idle;

	if (!world_sdf.startup_bakes || world_sdf.startup_logged || menu_pictures_preparing() ||
		pthread_mutex_trylock(&world_sdf_lock) != 0)
	{
		return;
	}
	milliseconds = world_sdf.baked_milliseconds;
	count = world_sdf.baked_count;
	idle = !world_sdf.baking && !world_sdf.bakes && !world_sdf.hold;
	pthread_mutex_unlock(&world_sdf_lock);
	if (!idle)
		return;
	world_sdf.startup_logged = 1;
	platform_log("world sdf: the first start's %lu of %ld fields baked in %.2f s behind the main menu: the pass and its "
		"fields %.2f s", count, world_sdf.startup_bakes, (double)milliseconds / 1e3,
		menu_pictures_pass_seconds() + (double)milliseconds / 1e3);
}

/* (under the lock) whether the field drawn waits for a bake, another
baking */
static int sdf_drawn_waits_locked(void)
{
	struct sdf_job *job;

	if (!world_sdf.wanted_known || !world_sdf.baking || sdf_key_same(&world_sdf.baking_key, &world_sdf.wanted))
		return 0;
	for (job = world_sdf.bakes; job; job = job->next)
	{
		if (sdf_key_same(&job->key, &world_sdf.wanted))
			return 1;
	}
	return 0;
}

/* the bake's schedule told what the game is doing ("world" off, a game
played, the field drawn), and the bake running stopped if it is not to run
now: in a game, any but the field drawn's (the start-up pass's others wait
for the main menu), and anywhere one the field drawn waits behind. Each
frame, if the lock is free (the game's thread never waits for it) */
static void sdf_schedule_said(int off)
{
	int playing = !off && pc_menu_game_playing();
	int changed;

	__atomic_store_n(&sdf_playing, playing, __ATOMIC_RELAXED);
	if (pthread_mutex_trylock(&world_sdf_lock) != 0)
		return;
	changed = off != world_sdf.off || playing != world_sdf.playing || world_sdf.map_known != world_sdf.wanted_known ||
		(world_sdf.map_known && !sdf_key_same(&world_sdf.current, &world_sdf.wanted));
	world_sdf.off = off;
	world_sdf.playing = playing;
	world_sdf.wanted_known = world_sdf.map_known;
	world_sdf.wanted = world_sdf.current;
	if (world_sdf.baking && (!sdf_bake_wanted_locked(&world_sdf.baking_key) || sdf_drawn_waits_locked()))
		__atomic_store_n(&sdf_yield, 1, __ATOMIC_RELAXED);
	if (changed)
		pthread_cond_signal(&world_sdf_bake_wake);
	pthread_mutex_unlock(&world_sdf_lock);
}

void world_sdf_frame(void)
{
	struct sdf_key key;
	int hold, off = sdf_off();

	sdf_schedule_said(off);
	if (off)
		return;
	/* (no bake while the start-up pass runs, up to the main menu shown:
	they begin once it is) */
	hold = menu_pictures_preparing();
	/* (and a few seconds more, the main menu's first frames its own) */
	{
		static long settle;

		settle = hold ? WORLD_SDF_MENU_SETTLE_FRAMES : settle > 0 ? settle - 1 : 0;
		hold = hold || settle > 0;
	}
	if (hold != world_sdf.hold)
	{
		pthread_mutex_lock(&world_sdf_lock);
		world_sdf.hold = hold;
		pthread_cond_signal(&world_sdf_bake_wake);
		pthread_mutex_unlock(&world_sdf_lock);
	}
	sdf_frame_timed();
	sdf_startup_total();
	/* ("world" chosen with a map loaded) */
	if (!world_sdf.map_known && game_in_progress() && sdf_key_loaded(&key))
		sdf_current_set(&key);
	sdf_results();
	/* (its static scenery left out of the window until it is drawn) */
	if (world_sdf.map_known && world_sdf.state == _world_sdf_baking && !world_sdf.awaited_made)
		sdf_awaited_make();
	if (world_sdf.copy_wanted && --world_sdf.copy_frames <= 0)
	{
		world_sdf.copy_wanted = 0;
		if (!sdf_bake_begin(&world_sdf.current, 0))
		{
			world_sdf.state = _world_sdf_failed;
			platform_log("world sdf: %s bsp %d has no geometry to bake: the proxies alone", world_sdf.current.map,
				(int)world_sdf.current.bsp);
		}
	}
	/* (uploaded a few slices a frame, then faded in) */
	if (world_sdf.field.cells)
	{
		if (!sdf_upload_done(&world_sdf.field, world_sdf.uploaded_pages, world_sdf.uploaded_objects,
			world_sdf.uploaded_cells, world_sdf.uploaded_layers))
		{
			if (!world_sdf.layer)
				world_sdf.layer = malloc(WORLD_SDF_ATLAS_LAYER_BYTES);
			if (!world_sdf.objects_slice)
				world_sdf.objects_slice = malloc(sdf_objects_slice_bytes(&world_sdf.field.objects));
			if (world_sdf.layer && world_sdf.objects_slice && sdf_upload(&world_sdf.field, &world_sdf.uploaded_pages,
				&world_sdf.uploaded_objects, &world_sdf.uploaded_cells, &world_sdf.uploaded_layers, world_sdf.layer,
				world_sdf.objects_slice, WORLD_SDF_UPLOAD_BYTES))
			{
				world_sdf.faded_from = SDL_GetTicksNS();
			}
			world_sdf.field_sent = 1;
		}
		else
		{
			unsigned long long now = SDL_GetTicksNS();

			world_sdf.fade = now - world_sdf.faded_from >= WORLD_SDF_FADE_NANOSECONDS ? 1.0f :
				(float)((double)(now - world_sdf.faded_from) / (double)WORLD_SDF_FADE_NANOSECONDS);
		}
	}
}

/* ---------- the start-up pass (menu_pictures_render.c) */

static int sdf_tried(struct sdf_key const *key)
{
	long index;

	for (index = 0; index < world_sdf.tried_count; index++)
	{
		if (sdf_key_same(&world_sdf.tried[index], key))
			return 1;
	}
	return 0;
}

void world_sdf_startup_map_loaded(void)
{
	struct sdf_key key;
	char path[1100];

	if (sdf_off() || !sdf_key_loaded(&key) || sdf_tried(&key))
		return;
	if (world_sdf.tried_count < WORLD_SDF_MAXIMUM_TRIED)
		world_sdf.tried[world_sdf.tried_count++] = key;
	sdf_path(&key, path, sizeof(path));
	if (sdf_cache_present(path, key.checksum, (unsigned long)key.bsp))
		return;
	if (!sdf_bake_begin(&key, 1))
		platform_log("world sdf: %s bsp %d has no geometry to bake", key.map, (int)key.bsp);
	else
		world_sdf.startup_bakes++;
}

/* whether a stock map (a10) is missing its first BSP's field: the pass
loads it for its fields (menu_pictures_render.c's) */
int world_sdf_startup_map_wanted(char const *map)
{
	struct sdf_key key;
	char path[1100];

	if (sdf_off())
		return 0;
	memset(&key, 0, sizeof(key));
	snprintf(key.map, sizeof(key.map), "%s", map);
	key.checksum = menu_picture_map_checksum(map);
	if (!key.checksum || sdf_tried(&key))
		return 0;
	sdf_path(&key, path, sizeof(path));
	return !sdf_cache_present(path, key.checksum, 0);
}

int world_sdf_startup_wanted(void)
{
	unsigned int pose;

	if (world_sdf.startup_looked)
		return world_sdf.startup_wanted;
	world_sdf.startup_looked = 1;
	if (sdf_off() || menu_pictures_quiet_run())
		return 0;
	for (pose = 0; pose < menu_picture_pose_count && !world_sdf.startup_wanted; pose++)
	{
		unsigned int before;

		/* (each map once) */
		for (before = 0; before < pose && strcmp(menu_picture_poses[before].map, menu_picture_poses[pose].map);
			before++)
		{
		}
		if (before == pose)
			world_sdf.startup_wanted = world_sdf_startup_map_wanted(menu_picture_poses[pose].map);
	}
	return world_sdf.startup_wanted;
}

/* ---------- the field drawn */

int world_sdf_state(void)
{
	return world_sdf.map_known ? world_sdf.state : _world_sdf_none;
}

/* the field drawn with: whole on the GPU (its pages too) */
static int sdf_field_drawn(void)
{
	return world_sdf.field.cells && sdf_upload_done(&world_sdf.field, world_sdf.uploaded_pages,
		world_sdf.uploaded_objects, world_sdf.uploaded_cells, world_sdf.uploaded_layers);
}

struct world_sdf_object const *world_sdf_object_field(long definition, unsigned long atlas[3])
{
	struct world_sdf_objects const *objects = &world_sdf.field.objects;
	unsigned long index;

	if (definition == NONE || !objects->count || !sdf_field_drawn())
		return NULL;
	for (index = 0; index < objects->count; index++)
	{
		struct world_sdf_object const *object = &objects->objects[index];

		if (object->definition == definition && object->atlas[0] != ~0UL)
		{
			atlas[0] = objects->atlas_width;
			atlas[1] = objects->atlas_height;
			atlas[2] = objects->atlas_depth;
			return object;
		}
	}
	return NULL;
}

/* whether these placements (in sdf_placement_order) hold one of this
definition standing here */
static int sdf_placement_found(struct world_sdf_placement const *placements, unsigned long count, long definition,
	float const position[3])
{
	unsigned long low = 0, high = count, index;

	/* (the first at or past its x) */
	while (low < high)
	{
		unsigned long middle = (low + high) / 2;

		if (placements[middle].position[0] < position[0])
			low = middle + 1;
		else
			high = middle;
	}
	for (index = low; index < count && placements[index].position[0] == position[0]; index++)
	{
		struct world_sdf_placement const *placement = &placements[index];

		if (placement->definition == definition && placement->position[1] == position[1] &&
			placement->position[2] == position[2])
		{
			return 1;
		}
	}
	return 0;
}

int world_sdf_scenery_baked(long definition, float const position[3])
{
	return sdf_field_drawn() &&
		sdf_placement_found(world_sdf.field.scenery.placements, world_sdf.field.scenery.placement_count, definition,
			position);
}

int world_sdf_object_awaited(long definition)
{
	long index;

	if (definition == NONE || world_sdf.state == _world_sdf_failed || sdf_field_drawn())
		return 0;
	for (index = 0; index < world_sdf.awaited_object_count; index++)
	{
		if (world_sdf.awaited_objects[index] == definition)
			return 1;
	}
	return 0;
}

int world_sdf_scenery_awaited(long definition, float const position[3])
{
	return world_sdf.awaited_count && world_sdf.state != _world_sdf_failed && !sdf_field_drawn() &&
		sdf_placement_found(world_sdf.awaited, world_sdf.awaited_count, definition, position);
}

int world_sdf_scenery_layer(void const *lightmap_bitmap)
{
	struct world_sdf_scenery const *scenery = &world_sdf.field.scenery;
	struct structure_bsp const *structure;
	struct bitmap_group const *group;
	long page;

	if (!lightmap_bitmap || !sdf_field_drawn() || !scenery->page_count ||
		!(structure = global_structure_bsp_get()) || structure->lightmap_group.index == NONE ||
		!(group = bitmap_group_get(structure->lightmap_group.index)) || !group->bitmaps.address)
	{
		return WORLD_SDF_SCENERY_NO_LAYER;
	}
	page = (long)((struct bitmap_data const *)lightmap_bitmap - (struct bitmap_data const *)group->bitmaps.address);
	if (page < 0 || page >= group->bitmaps.count || (unsigned long)page >= scenery->page_count)
		return WORLD_SDF_SCENERY_NO_LAYER;
	return scenery->layers[page];
}

float world_sdf_fade(void)
{
	return world_sdf.field.cells ? world_sdf.fade : 0.0f;
}

struct world_sdf_header const *world_sdf_current(void)
{
	/* (the volume's first member: (struct world_sdf_volume const *) this
	is the field, its voxels too, for world_sdf_sample. The game's thread's
	alone, and good until the next world_sdf_frame, BSP switch or unload:
	the threads never touch a field once it is handed over) */
	return world_sdf.field.cells ? &world_sdf.field.header : NULL;
}
