/*
DEMO.C

Demos (docs/demos.md): every game this machine plays, recorded as it plays,
and played again exactly, tick for tick, as the game state was.

The game's simulation is a function of its game state (one block of
memory: game_state.c), the two random seeds the ticks take (random_math.c)
and the players' input of each tick ("Determinism", port/linux/README.md).
A demo is then:

- the state it starts from: a snapshot of the game state and the seeds at
  its first tick (the map's first, a game's first), and one every
  demo.snapshot_seconds after it, for seeking;
- each tick's players' input, as the tick takes it (players.c), every
  player's: this machine's, the bots', and the other machines' that the
  netcode gave it; and each later sub-step's controls of the players' units
  (player_control.c's slot of the fast phase: game.sim_rate's 60 and 120);
- the frames' input that the ticks read: the player control globals (the
  scripts test the players' actions), whole at each tick's start they
  changed by;
- what reached the game state from outside the ticks: at each tick's start,
  and each sub-step's of a tick left open across frames, what differs from
  the state the tick (or sub-step) before left (a player joined or left, a
  host's decision, a console command, an effect a particle made) as the
  bytes that changed; and when the game state was rewritten as a whole (a
  checkpoint reverted to, a saved game loaded, the map started over) or the
  game's code made or deleted an object there, the whole of it. The frames'
  own allocations (the particles, the decals, the lights and the rest
  determinism_compare.py names FRAME_ALLOCATIONS) are not compared: the
  ticks do not read them;
- inside a tick, what a host's netcode put in from its clients (their
  predictions of their units) and the choices it made by which machines are
  there (whose hits are their reports');
- a hash of the game state three times a second, and of each allocation, which the
  playback compares with its own, and says where the two first part.

Playing a demo loads its map (a multiplayer game's as a game of this
machine, with its variant), takes its first snapshot, and runs the ticks
with the recorded input in place of the players' (this machine's input,
the bots and the other machines play no part). At each tick's start what
its own frames wrote into the allocations the ticks keep is put back as the
tick before left them, and the recording's differences are written in. Its
sub-steps' controls are put on the units as the recording's were. Nothing
of the machine's own saved games is read or written meanwhile.

The file (demo_files.c writes it on a thread of its own): a header
(DEMO_HEADER_SIZE bytes: the build, the map, the connection, the rate),
then blocks, each packed (zlib): a second of records ('R'), a snapshot that
the playback takes ('K': the start, a reload) or may skip ('k': a seek's).
A record is its type (a byte), its length (4 bytes) and its bytes:

    T <tick> <rate>                    a tick begins (its number as the clock had
                                       it, and the rate it runs at)
    K <tick> <flags> <seeds> {<offset> <size> <bytes>}...
                                       the game state (each allocation's part in
                                       use) and the seeds
    P {<offset> <size> <bytes>}...     what changed in the game state since the
                                       tick before ended
    M <sub-step> {<offset> <size> <bytes>}...
                                       ... before a sub-step of an open tick
    C <offset> <size> <bytes>          the player control globals
    S <global seed> <local seed>       the seeds as they then are
    A {<player> <action>}...           the tick's players' input (players.h's
                                       struct player_action, each in use)
    F {<offset> <size> <bytes>}...     what the netcode put in, inside the tick
    D <choice>                         a choice the netcode made
    U <sub-step> <count> {<player> <unit> <controls>}...
                                       a later sub-step's units' controls
    H <tick> <hash> <hash>...          the game state's hash as the tick ended,
                                       and each allocation's
    V <variant> <options>              the game's variant (a multiplayer game's)
    B <tick> <milliseconds>            a bookmark (the clip key)
    W <tick> <line>                    a line of the host's to its bots, as the
                                       tick applied it (bots_command.c: before
                                       the tick's A; the playback logs it, its
                                       bots' input being the A's)
    E <tick>                           the end

Everything a demo holds is checked before the playback takes it, as the
netcode checks what another machine sends: a demo is a file anyone may
send. Its snapshots and differences go only into the game state's
allocations, and the game state is checked as a saved game is
(game_state.c) after each; an input is a controller's.

A demo of another build is refused, with the reason: the simulation is
the code, and another's may part from it at the first tick.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "main/main.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "memory/memory_pool.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "camera/observer.h"
#include "structures/structure_bsp_definitions.h"

#include "demo.h"
#include "network_coop.h"
#include "halo_keyboard.h"
#include "sim_rate.h"
#include "../src/demo_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* port/linux/src/port_config.c */
char const *config_string(char const *name);
long config_integer(char const *name);
int config_boolean(char const *name);
double config_real(char const *name);
int config_command_line_value(char const *name, char *value, unsigned long size);
void platform_log(char const *format, ...);
/* source/math/random_math.c */
void random_math_port_seeds(unsigned long *global_seed, unsigned long *local_seed);
void random_math_port_set_seeds(unsigned long global_seed, unsigned long local_seed);
unsigned long random_math_port_frame_seed(void);
void random_math_port_set_frame_seed(unsigned long seed);
/* source/saved games/game_state.c */
void *game_state_port_base(long *size, unsigned long *checksum);
boolean game_state_port_demo_load(void (*write)(void *context), void *context, boolean procedures);
/* source/main/main.c */
void main_disallow_persistent_storage(void);
/* port/linux/src/xbox_kernel.c */
unsigned long long platform_shared_clock_microseconds(void);
/* source/structures/cluster_partitions.c, port/linux/game/object_bounds_cache.c */
void cluster_partitions_port_forget(void);
void object_bounds_cache_invalidate(void);
/* source/game/player_control.c */
unsigned long player_control_substep_pressed_flags(long unit_index);
struct player_control *player_control_get(short local_player_index);
/* source/game/game_time.c */
boolean game_time_menu_scene(void);
/* source/main/console.c */
void console_printf(boolean clear, const char *format, ...);

void network_damage_deal_recorded(void const *bytes, unsigned long size);
void damage_kill_object_for_player(long object_index, long player_index);
void *game_engine_port_demo_state(short part, long *size);
void *game_sound_port_demo_state(short part, long *size);
void *wind_port_demo_state(long *size);
void *network_standard_port_demo_state(long *size);
void *main_port_demo_state(short part, long *size);
unsigned long network_standard_demo_rules(void);
extern struct game_variant game_variant_global;
void main_build_identity(long *number, char *flavor, size_t flavor_size, char *repository, size_t repository_size,
	char *version, size_t version_size);

/* ---------- constants */

#define DEMO_MAGIC "OH1DEMO"

/* the build's platform: a snapshot holds the guest's code's addresses (the
caches' procedures, the objects' types' functions), each build's own */
#if defined(HALO_ANDROID)
#define DEMO_PLATFORM "android"
#elif defined(HALO_IOS)
#define DEMO_PLATFORM "ios"
#elif defined(HALO_MACOS)
#define DEMO_PLATFORM "macos"
#elif defined(HALO_WINDOWS)
#define DEMO_PLATFORM "windows"
#elif defined(__aarch64__)
#define DEMO_PLATFORM "linux-arm64"
#else
#define DEMO_PLATFORM "linux-x86_64"
#endif
#ifdef HALO_VR
#define DEMO_PLATFORM_VARIANT "-vr"
#else
#define DEMO_PLATFORM_VARIANT ""
#endif

enum
{
	/* 3: every sub-step's U and every tick's Z, which bound the records
	the tick's code takes as it runs (D, F, I, L); the wind in G; the
	game's rules in the header; 4: G the runs of bytes that changed, with
	the network game and the co-op host's parts (network_coop.c) */
	DEMO_FORMAT_VERSION = 4,
	DEMO_HEADER_SIZE = 512,
	DEMO_BUILD_SIZE = 64,
	DEMO_MAP_NAME_SIZE = 128,

	MAXIMUM_DEMO_ALLOCATIONS = 512,
	/* the ticks of a block of records */
	DEMO_BLOCK_TICKS = TICKS_PER_SECOND,
	/* the hash's ticks: a third of a second, the playback's differences
	written no more once it has parted (demo_playback_patch) */
	DEMO_HASH_TICKS = TICKS_PER_SECOND / 3,
	/* the objects' fields left out of the comparison and the hash (the
	frames' as much as the ticks'): a magic number, the attachments'
	indices, a render state's */
	MAXIMUM_DEMO_SKIPS = 65536,
	/* two differences this near are one */
	DEMO_PATCH_JOIN_BYTES = 32,
	/* the largest record */
	DEMO_RECORD_MAXIMUM_SIZE = DEMO_BLOCK_MAXIMUM_SIZE,
	/* the most of the game engine's state outside the game state (G) */
	DEMO_ENGINE_STATE_SIZE = 1024 * 1024,
	/* the whole game states taken one after another for what the game's
	code made or deleted outside the ticks, and the ticks that give one
	more (a snapshot's), past which the recording warns that they come too
	often (demo_structural_whole) */
	DEMO_STRUCTURAL_BURST = 4,
	/* ... at most this often */
	DEMO_STRUCTURAL_WARNING_TICKS = 60 * TICKS_PER_SECOND,
};

/* a block's kind */
enum
{
	_demo_block_records = 'R',
	_demo_block_snapshot = 'K',
	_demo_block_seek_snapshot = 'k',
};

/* a snapshot's flags */
enum
{
	_demo_snapshot_start_bit,
	_demo_snapshot_loaded_bit,
	_demo_snapshot_reset_bit,
	_demo_snapshot_seek_bit,
};

/* ---------- structures */

struct demo_allocation
{
	char name[32];
	unsigned long offset;
	unsigned long size;
	/* whether the frames move it (determinism_compare.py's
	FRAME_ALLOCATIONS): not compared, nor hashed */
	boolean frames;
	/* whether it is the frames' input that the ticks read (the player
	control globals: the scripts' tests of the players' actions), recorded
	whole at each tick's start that it changed by */
	boolean input;
};

struct demo_buffer
{
	unsigned char *data;
	unsigned long size;
	unsigned long capacity;
	boolean failed;
};

/* what a unit was given in a sub-step */
struct demo_unit_controls
{
	unsigned long control_flags;
	real_vector3d throttle;
	real primary_trigger;
	real_vector3d desired_facing_vector;
	real_vector3d desired_aiming_vector;
	real_vector3d desired_looking_vector;
	unsigned long pressed_flags;
};

/* a block of the demo being played, as its index has it */
struct demo_block_entry
{
	unsigned long offset;
	unsigned long kind;
	unsigned long first_tick;
	unsigned long size;
	unsigned long packed_size;
	/* the ticks the demo ran before it (its records' T, counted at the
	playback's start), where a seek to a snapshot puts the playback */
	unsigned long position;
};

/* ---------- globals */

static struct
{
	boolean read;
	boolean record;
	long keep_count;
	long keep_megabytes;
	long snapshot_ticks;
	real clip_seconds;
} demo_settings;

static struct
{
	unsigned char *base;
	long size;
	unsigned long checksum;
	long count;
	struct demo_allocation list[MAXIMUM_DEMO_ALLOCATIONS];
} demo_allocations;

/* the network game a host's ticks read (G records: demo_outside_part) */
static struct network_game demo_network_game;

/* the objects' fields left out (absolute addresses, in order) */
static unsigned long demo_skips[MAXIMUM_DEMO_SKIPS];
static long demo_skip_count;

static struct
{
	boolean recording;
	struct demo_writer *writer;
	char name[DEMO_FILE_NAME_SIZE];
	unsigned char *shadow;
	boolean shadow_valid;
	/* the machine's players' cameras as they were last recorded (O records) */
	struct observer_result cameras[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	boolean cameras_valid;
	/* the frames' input as it was last recorded (C records) */
	unsigned char *input;
	boolean input_valid;
	struct demo_buffer records;
	struct demo_buffer scratch;
	long block_first_tick;
	long block_ticks;
	long ticks;
	long last_snapshot_tick;
	boolean loaded;
	boolean stopped;
	unsigned long end_seeds[2];
	unsigned long bots_seeds[2];
	/* the seeds as the open tick was left (demo_tick_pause) */
	unsigned long pause_seeds[2];
	/* the game engine's state as it was last recorded (G records) */
	unsigned char *engine_state;
	unsigned long engine_state_size;
	/* the sub-step's units' controls as it began, by player */
	struct demo_unit_controls substep_before[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long substep_units[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long patches;
	unsigned long patch_bytes;
	long last_tick;
	boolean in_tick;
	boolean foreign;
	boolean paused;
	long structural_snapshots;
	/* the whole game states that may yet be taken for what the game's code
	made or deleted outside the ticks before the recording warns, as of a
	tick; those taken past them, and when it last warned
	(demo_structural_whole) */
	long structural_allowance;
	long structural_allowance_tick;
	long structural_too_often;
	long structural_warning_tick;
	/* (the host) the netcode's work after a tick is being recorded
	(demo_netcode_begin) */
	boolean netcode;
	/* the bytes the demo took, once it ended */
	unsigned long long written;
	/* the microseconds the recording took of the game's thread */
	unsigned long long work_microseconds;
} demo_recorder;

static struct
{
	boolean requested;
	boolean playing;
	boolean started;
	boolean ended;
	boolean quit_at_end;
	struct demo_reader *file;
	unsigned long file_size;
	char path[512];
	unsigned char header[DEMO_HEADER_SIZE];
	char map_name[DEMO_MAP_NAME_SIZE];
	long connection;
	long difficulty;
	long rate;
	/* the recorded game's rules (_demo_rule_*: the header's) */
	unsigned long rules;
	/* the records of a tick's code (D, F, I, L) the playback's code did not
	take, left at a sub-step's or the tick's end: its code ran otherwise
	than the recording's (it has parted, or is about to) */
	long records_left;
	boolean records_left_logged;
	/* the machine's players' cameras as the recording's last O records had
	them, put back wherever the tick's code may read them after the frames
	moved them (a sub-step of an open tick, an event between ticks) */
	struct observer_result cameras[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	boolean cameras_valid[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	boolean variant_read;
	struct game_variant variant;
	struct game_variant_options variant_options;
	struct demo_block_entry *blocks;
	long block_count;
	long next_block;
	unsigned char *block;
	unsigned long block_size;
	unsigned long cursor;
	unsigned long pressed_units[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	unsigned long pressed_flags[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long pressed_count;
	long ticks;
	long last_tick;
	boolean diverged;
	long hashes_matched;
	/* the ticks the demo runs, its seek asked for (a block's index, NONE),
	and the playback's controls held last */
	unsigned long length_ticks;
	long seek_block;
	unsigned long controls_held;
	real speed;
	boolean seek_tested;
} demo_player;

/* ---------- the settings */

static void demo_settings_read(
	void)
{
	if (demo_settings.read)
		return;
	demo_settings.read = TRUE;
	demo_settings.record = config_boolean("demo.record") != 0;
	demo_settings.keep_count = config_integer("demo.keep_count");
	demo_settings.keep_megabytes = config_integer("demo.keep_megabytes");
	demo_settings.snapshot_ticks = (long)(config_real("demo.snapshot_seconds") * TICKS_PER_SECOND);
	if (demo_settings.snapshot_ticks < TICKS_PER_SECOND)
		demo_settings.snapshot_ticks = TICKS_PER_SECOND;
	demo_settings.clip_seconds = config_real("demo.clip_seconds");
}

/* ---------- the buffers */

static boolean demo_buffer_reserve(
	struct demo_buffer *buffer,
	unsigned long size)
{
	unsigned long capacity;
	unsigned char *data;

	if (buffer->failed)
		return FALSE;
	if (buffer->size + size <= buffer->capacity)
		return TRUE;
	capacity = buffer->capacity ? buffer->capacity : 65536;
	while (capacity < buffer->size + size)
		capacity *= 2;
	data = demo_files_realloc(buffer->data, capacity);
	if (!data)
	{
		buffer->failed = TRUE;
		return FALSE;
	}
	buffer->data = data;
	buffer->capacity = capacity;
	return TRUE;
}

static void demo_buffer_bytes(
	struct demo_buffer *buffer,
	void const *bytes,
	unsigned long size)
{
	if (!demo_buffer_reserve(buffer, size))
		return;
	memcpy(buffer->data + buffer->size, bytes, size);
	buffer->size += size;
}

static void demo_buffer_u8(
	struct demo_buffer *buffer,
	unsigned long value)
{
	unsigned char byte_value = (unsigned char)value;

	demo_buffer_bytes(buffer, &byte_value, 1);
}

static void demo_buffer_u32(
	struct demo_buffer *buffer,
	unsigned long value)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
	demo_buffer_bytes(buffer, bytes, 4);
}

/* a record begun: its type, and room for its length (demo_record_end) */
static unsigned long demo_record_begin(
	struct demo_buffer *buffer,
	char type)
{
	unsigned long start;

	demo_buffer_u8(buffer, (unsigned char)type);
	start = buffer->size;
	demo_buffer_u32(buffer, 0);
	return start;
}

static void demo_record_end(
	struct demo_buffer *buffer,
	unsigned long start)
{
	unsigned long length;

	if (buffer->failed || start + 4 > buffer->size)
		return;
	length = buffer->size - start - 4;
	buffer->data[start] = (unsigned char)length;
	buffer->data[start + 1] = (unsigned char)(length >> 8);
	buffer->data[start + 2] = (unsigned char)(length >> 16);
	buffer->data[start + 3] = (unsigned char)(length >> 24);
}

static unsigned long demo_read_u32(
	unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

/* ---------- the game state's allocations */

void demo_game_state_allocation(
	char const *name,
	void const *address,
	long size)
{
	static char const *const frames[] = {
		"game time globals", "lights", "decals", "decal globals", "decal vertices",
		"decal vertex cache", "particle systems", "particle system particles", "particle", "contrail",
		"contrail point", "game sound globals", "cached object render states", "motion sensor (radar)",
		"object looping sounds", "scenario globals", "player control globals", "first person weapons",
		/* (the lights' cluster references, and their globals, go with the
		lights: a difference written into them would not be the lights'
		here) */
		"lights globals", "light", "cluster light reference", "light cluster reference",
	};
	struct demo_allocation *allocation;
	long index;

	if (!demo_allocations.base)
		demo_allocations.base = game_state_port_base(&demo_allocations.size, &demo_allocations.checksum);
	if (demo_allocations.count >= MAXIMUM_DEMO_ALLOCATIONS || !demo_allocations.base ||
		(unsigned char const *)address < demo_allocations.base ||
		(unsigned char const *)address + size > demo_allocations.base + demo_allocations.size)
	{
		return;
	}
	allocation = &demo_allocations.list[demo_allocations.count++];
	snprintf(allocation->name, sizeof(allocation->name), "%s", name ? name : "?");
	allocation->offset = (unsigned long)((unsigned char const *)address - demo_allocations.base);
	allocation->size = (unsigned long)size;
	allocation->frames = FALSE;
	for (index = 0; index < (long)NUMBEROF(frames); index++)
	{
		if (!strcmp(allocation->name, frames[index]))
			allocation->frames = TRUE;
	}
	allocation->input = !strcmp(allocation->name, "player control globals");
}

/* the allocations' checksum as the game state has it now (game_state.c's:
each allocation's size, summed as it is made) */
static unsigned long demo_allocation_checksum(
	void)
{
	long size;

	game_state_port_base(&size, &demo_allocations.checksum);
	return demo_allocations.checksum;
}

/* the part of an allocation that is in use, from its start: a data
array's header and its elements up to its count, a memory pool's header
and its blocks up to the end of its last; anything else whole */
static unsigned long demo_allocation_used(
	struct demo_allocation const *allocation)
{
	unsigned char const *address = demo_allocations.base + allocation->offset;
	unsigned long used = allocation->size;

	if (allocation->size >= sizeof(struct data_array) &&
		((struct data_array const *)address)->signature == 'd@t@' &&
		((struct data_array const *)address)->data == (struct data_array const *)address + 1)
	{
		struct data_array const *data = (struct data_array const *)address;

		if (data->count >= 0 && data->size > 0)
			used = sizeof(*data) + (unsigned long)data->count * (unsigned long)data->size;
	}
	else if (allocation->size >= sizeof(struct memory_pool) &&
		((struct memory_pool const *)address)->signature == 'pool' &&
		((struct memory_pool const *)address)->base_address == (struct memory_pool const *)address + 1)
	{
		struct memory_pool const *pool = (struct memory_pool const *)address;
		unsigned char const *last = (unsigned char const *)pool->last_block;

		used = sizeof(*pool);
		if (last && last >= address + sizeof(*pool) && last + sizeof(struct memory_pool_block) <= address + allocation->size)
		{
			long block_size = ((struct memory_pool_block const *)last)->size;

			if (block_size > 0)
				used = (unsigned long)(last - address) + (unsigned long)block_size;
		}
	}
	return used < allocation->size ? used : allocation->size;
}

/* whether an object's attachment of this type is a datum of the frames'
allocations (determinism_log.c's determinism_frames_attachment) */
static boolean demo_frames_attachment(
	char type)
{
	return type == _object_attachment_type_light ||
		type == _object_attachment_type_looping_sound ||
		type == _object_attachment_type_contrail ||
		type == _object_attachment_type_particle_system;
}

static void demo_skip_add(
	void const *address)
{
	if (demo_skip_count < MAXIMUM_DEMO_SKIPS)
		demo_skips[demo_skip_count++] = (unsigned long)address;
}

/* the objects' fields the frames write as well as the ticks (each
object's magic number, the collision tests' mark; its attachments' indices
that are the frames' datums; its render state's index: determinism_log.c's
determinism_hash_objects), in order */
static void demo_skips_find(
	void)
{
	long index;

	demo_skip_count = 0;
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];
		unsigned char const *start = demo_allocations.base + allocation->offset;
		unsigned char const *end = start + allocation->size;
		struct memory_pool const *pool = (struct memory_pool const *)start;
		struct memory_pool_block const *block;
		long blocks = 0;

		if (strcmp(allocation->name, "objects") || allocation->size < sizeof(*pool) || pool->signature != 'pool')
			continue;
		for (block = pool->first_block;
			block && (unsigned char const *)block >= start && (unsigned char const *)(block + 1) <= end &&
				blocks++ < (long)allocation->size;
			block = block->next_block)
		{
			struct object_datum const *object = (struct object_datum const *)(block + 1);
			unsigned char const *block_end = (unsigned char const *)block + block->size;
			short attachment;

			if (block->size < (long)sizeof(*block) || block_end > end ||
				(unsigned char const *)(&object->object.cached_render_state_index + 1) > block_end)
			{
				break;
			}
			demo_skip_add(&object->object.magic_number);
			if ((unsigned char const *)&object->object.attachment_indices[MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT] <=
				block_end && object_definition_get(object->definition_index))
			{
				short attachment_count = (short)MIN(
					object_definition_get(object->definition_index)->object.attachments.count,
					MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT);

				for (attachment = 0; attachment < attachment_count; attachment++)
				{
					if (demo_frames_attachment(object->object.attachment_types[attachment]))
						demo_skip_add(&object->object.attachment_indices[attachment]);
				}
			}
			demo_skip_add(&object->object.cached_render_state_index);
		}
	}
	/* (in order: the blocks are, and each one's fields; but sorted to be
	sure, as the comparison walks them) */
	for (index = 1; index < demo_skip_count; index++)
	{
		unsigned long value = demo_skips[index];
		long other = index - 1;

		while (other >= 0 && demo_skips[other] > value)
		{
			demo_skips[other + 1] = demo_skips[other];
			other--;
		}
		demo_skips[other + 1] = value;
	}
}

/* the hash of the game state the ticks keep: each allocation's part in
use but the frames' allocations, a data array's free datums (what a datum
the frames made and deleted again left there, which no tick reads:
determinism_log.c's u line) and the objects' fields left out (FNV-1a over
words) */
static unsigned long demo_state_hash(
	unsigned long *allocation_hashes)
{
	unsigned long hash = 2166136261UL;
	long skip = 0;
	long index;

	demo_skips_find();
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];
		unsigned long const *word = (unsigned long const *)(demo_allocations.base + allocation->offset);
		unsigned long words = demo_allocation_used(allocation) / 4;
		unsigned long word_index;
		unsigned long allocation_hash = 2166136261UL;
		struct data_array const *data = (struct data_array const *)word;
		boolean data_array = words * 4 >= sizeof(*data) && data->signature == 'd@t@' && data->data == data + 1 &&
			data->size >= (short)sizeof(long) && data->size % 4 == 0;

		if (allocation->frames)
		{
			if (allocation_hashes)
				allocation_hashes[index] = 0;
			continue;
		}
		for (word_index = 0; word_index < words; word_index++, word++)
		{
			/* (a free datum's words: its identifier none) */
			if (data_array && word_index * 4 >= sizeof(*data))
			{
				unsigned long datum_offset = (word_index * 4 - sizeof(*data)) % (unsigned long)data->size;

				if (!((struct datum_header const *)((unsigned char const *)word - datum_offset))->identifier)
					continue;
			}
			while (skip < demo_skip_count && demo_skips[skip] < (unsigned long)word)
				skip++;
			if (skip < demo_skip_count && demo_skips[skip] == (unsigned long)word)
				continue;
			allocation_hash = (allocation_hash ^ *word) * 16777619UL;
		}
		if (allocation_hashes)
			allocation_hashes[index] = allocation_hash;
		hash = (hash ^ allocation_hash) * 16777619UL;
	}
	return hash;
}

/* ---------- recording */

/* the machine's players (players_globals' count is the last tick's: none
before a network game's first, whose players were added outside it) */
static unsigned long demo_local_player_count(
	void)
{
	unsigned long count = 0;
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		if (players_globals->local_players[local_player_index] != NONE)
			count++;
	}
	return count;
}

static void demo_header_write(
	unsigned char *header)
{
	long number;
	char flavor[32], repository[128], version[128];

	memset(header, 0, DEMO_HEADER_SIZE);
	memcpy(header, DEMO_MAGIC, sizeof(DEMO_MAGIC));
	header[8] = DEMO_FORMAT_VERSION;
	header[12] = (unsigned char)(DEMO_HEADER_SIZE & 0xFF);
	header[13] = (unsigned char)(DEMO_HEADER_SIZE >> 8);
	main_build_identity(&number, flavor, sizeof(flavor), repository, sizeof(repository), version, sizeof(version));
	snprintf((char *)header + 16, DEMO_BUILD_SIZE, "%ld/%s/%s%s", number, flavor, DEMO_PLATFORM, DEMO_PLATFORM_VARIANT);
	{
		unsigned long values[] = {
			HALO_PORT_NETWORK_VERSION,
			demo_allocation_checksum(),
			(unsigned long)demo_allocations.size,
			(unsigned long)game_connection(),
			(unsigned long)sim_rate_next(),
			(unsigned long)game_difficulty_level_get(),
			(unsigned long)game_time_get(),
			0,
			demo_local_player_count(),
			(unsigned long)halo_server_mode(),
			/* (the game's rules this machine's settings would give a game
			of the playback's machine otherwise: network_standard.c) */
			network_standard_demo_rules(),
		};
		long index;

		for (index = 0; index < (long)NUMBEROF(values); index++)
		{
			unsigned char *out = header + 80 + index * 4;

			out[0] = (unsigned char)values[index];
			out[1] = (unsigned char)(values[index] >> 8);
			out[2] = (unsigned char)(values[index] >> 16);
			out[3] = (unsigned char)(values[index] >> 24);
		}
	}
	snprintf((char *)header + 128, DEMO_MAP_NAME_SIZE, "%s", tag_get_name(global_scenario_index));
}

/* the game state's allocations in use (all, or but the frames'), as K and P
records hold them */
static void demo_snapshot_write(
	struct demo_buffer *buffer,
	long tick,
	unsigned long flags)
{
	unsigned long seeds[2];
	unsigned long start = demo_record_begin(buffer, 'K');
	long index;

	random_math_port_seeds(&seeds[0], &seeds[1]);
	demo_buffer_u32(buffer, (unsigned long)tick);
	demo_buffer_u32(buffer, flags);
	demo_buffer_u32(buffer, seeds[0]);
	demo_buffer_u32(buffer, seeds[1]);
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];
		unsigned long used = demo_allocation_used(allocation);

		demo_buffer_u32(buffer, allocation->offset);
		demo_buffer_u32(buffer, used);
		demo_buffer_bytes(buffer, demo_allocations.base + allocation->offset, used);
	}
	demo_record_end(buffer, start);
}

/* the records of the ticks so far handed to the writer */
static void demo_records_flush(
	void)
{
	if (!demo_recorder.writer || !demo_recorder.records.size)
		return;
	demo_writer_block(demo_recorder.writer, _demo_block_records, (unsigned long)demo_recorder.block_first_tick,
		demo_recorder.records.data, demo_recorder.records.size, FALSE);
	demo_recorder.records.size = 0;
	demo_recorder.block_ticks = 0;
}

static void demo_snapshot_block(
	long tick,
	unsigned long flags)
{
	boolean seek = TEST_FLAG(flags, _demo_snapshot_seek_bit);

	demo_records_flush();
	demo_recorder.scratch.size = 0;
	demo_snapshot_write(&demo_recorder.scratch, tick, flags);
	if (!demo_recorder.scratch.failed)
	{
		demo_writer_block(demo_recorder.writer, seek ? _demo_block_seek_snapshot : _demo_block_snapshot,
			(unsigned long)tick, demo_recorder.scratch.data, demo_recorder.scratch.size, seek);
	}
	demo_recorder.last_snapshot_tick = tick;
}

/* the allocations the ticks keep copied as the tick left them, to find
what changes before the next */
static unsigned long demo_shadow_used[MAXIMUM_DEMO_ALLOCATIONS];

static void demo_shadow_take(
	void)
{
	long index;

	if (!demo_recorder.shadow)
		return;
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];

		if (!allocation->frames)
		{
			demo_shadow_used[index] = demo_allocation_used(allocation);
			memcpy(demo_recorder.shadow + allocation->offset, demo_allocations.base + allocation->offset,
				demo_shadow_used[index]);
		}
	}
	demo_recorder.shadow_valid = TRUE;
}

/* (the playback's) the allocations the ticks keep put back as the tick
before left them, but the objects' fields the frames write: what the
playback's own frames wrote there since is not the recording's frames'
(an object's function a frame evaluates), whose own come in its records */
static void demo_shadow_restore(
	void)
{
	long skip = 0;
	long index;

	if (!demo_recorder.shadow || !demo_recorder.shadow_valid)
		return;
	demo_skips_find();
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];
		unsigned char *live = demo_allocations.base + allocation->offset;
		unsigned char const *shadow = demo_recorder.shadow + allocation->offset;
		unsigned long used = demo_shadow_used[index];
		unsigned long position = 0;
		struct data_array *data = (struct data_array *)live;
		short live_count = NONE;

		if (allocation->frames || !used || !memcmp(live, shadow, used))
			continue;
		if (used >= sizeof(*data) && data->signature == 'd@t@' && data->data == data + 1 && data->size >= (short)sizeof(short) &&
			data->count >= 0 && data->count <= data->maximum_count &&
			sizeof(*data) + (unsigned long)data->maximum_count * (unsigned long)data->size <= allocation->size)
		{
			live_count = data->count;
		}
		while (skip < demo_skip_count && demo_skips[skip] < (unsigned long)live)
			skip++;
		while (position < used)
		{
			unsigned long end = used;

			if (skip < demo_skip_count && demo_skips[skip] < (unsigned long)(live + used))
				end = demo_skips[skip] - (unsigned long)live;
			if (end > position)
				memcpy(live + position, shadow + position, end - position);
			if (end >= used)
				break;
			position = end + 4;
			skip++;
		}
		/* (a data array's datums the frames made past the count it is put
		back to: free again, their identifiers none, as datum_delete leaves
		them, which datum_new looks for) */
		if (live_count != NONE && data->count >= 0 && data->count < live_count)
		{
			short datum_index;

			for (datum_index = data->count; datum_index < live_count; datum_index++)
				((struct datum_header *)((unsigned char *)data->data + (long)data->size * datum_index))->identifier = 0;
		}
	}
}

/* what changed in the game state's allocations since the tick before
ended, as a P record (none if nothing did): word by word where a run of
them differs, the objects' fields the frames write left out. Whether an
object or an element was made or deleted (a P record then taken back: the
whole game state is recorded instead) */
static boolean demo_patch_write(
	char type)
{
	unsigned long start = 0;
	boolean begun = FALSE;
	boolean structural = FALSE;
	long skip = 0;
	long index;

	if (!demo_recorder.shadow_valid)
		return FALSE;
	demo_skips_find();
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];
		unsigned char const *live = demo_allocations.base + allocation->offset;
		unsigned char const *shadow = demo_recorder.shadow + allocation->offset;
		unsigned long used = demo_allocation_used(allocation);
		unsigned long position = 0;

		if (allocation->frames)
			continue;
		/* (and as far as the shadow's was: a datum deleted from the end, its
		identifier none past the count it left) */
		if (demo_shadow_used[index] > used)
			used = demo_shadow_used[index];
		while (position < used)
		{
			unsigned long chunk = used - position < 256 ? used - position : 256;
			unsigned long run_start, run_end, word;

			if (!memcmp(live + position, shadow + position, chunk))
			{
				position += chunk;
				continue;
			}
			/* (a run of differing words, joined across short gaps) */
			run_start = run_end = 0;
			for (word = position; word + 4 <= used; word += 4)
			{
				unsigned long address = (unsigned long)(live + word);
				boolean differs;

				while (skip < demo_skip_count && demo_skips[skip] < address)
					skip++;
				differs = memcmp(live + word, shadow + word, 4) != 0 &&
					!(skip < demo_skip_count && demo_skips[skip] == address);
				if (differs)
				{
					if (run_end == 0)
						run_start = word;
					run_end = word + 4;
				}
				else if (run_end && word >= run_end + DEMO_PATCH_JOIN_BYTES)
					break;
				else if (!run_end && word >= position + chunk)
					break;
			}
			if (!run_end)
			{
				position = word > position + chunk ? word : position + chunk;
				continue;
			}
			if (!begun)
			{
				start = demo_record_begin(&demo_recorder.records, type);
				begun = TRUE;
			}
			/* (an object, or an element of any array, made or deleted, or
			the objects changed: by the game's code, whose work outside the
			game state these bytes are not; demo_record_tick_begin takes the
			whole game state instead) */
			/* (but the effects, which the frames make too, a particle striking:
			only data, which the ticks then update) */
			if (!strcmp(allocation->name, "objects") || !strcmp(allocation->name, "object") ||
				(run_start < sizeof(struct data_array) && used >= sizeof(struct data_array) &&
					((struct data_array const *)live)->signature == 'd@t@' && strcmp(allocation->name, "effect") &&
					strcmp(allocation->name, "effect location")))
			{
				if (!structural && config_boolean("debug.demo_log"))
					platform_log("demos: %s changed outside the ticks (+%lu, %lu bytes)", allocation->name, run_start,
						run_end - run_start);
				structural = TRUE;
			}
			demo_buffer_u32(&demo_recorder.records, allocation->offset + run_start);
			demo_buffer_u32(&demo_recorder.records, run_end - run_start);
			demo_buffer_bytes(&demo_recorder.records, live + run_start, run_end - run_start);
			demo_recorder.patch_bytes += run_end - run_start;
			position = run_end;
		}
	}
	if (!begun && type == 'F')
	{
		/* (an F record each time, though nothing changed: the playback's
		windows match the recording's one for one) */
		start = demo_record_begin(&demo_recorder.records, type);
		begun = TRUE;
	}
	if (begun && type == 'P' && structural)
	{
		/* (the record taken back) */
		demo_recorder.records.size = start - 1;
		return TRUE;
	}
	if (begun)
	{
		demo_record_end(&demo_recorder.records, start);
		if (type == 'P')
			demo_recorder.patches++;
	}
	return structural;
}

/* the whole game states taken for what the game's code made or deleted
outside the ticks, as of the tick, against a rate: a few one after
another, and one more each snapshot's ticks (demo.snapshot_seconds). Each
is taken, whatever the rate (the playback stays exact), but past it the
recording warns, once a minute at most: a game whose code does that every
tick takes a whole game state a tick, 1 to 2 GB a minute (a host's OpenCE
players' moves did, before demo_netcode_begin) */
static void demo_structural_refill(
	long tick)
{
	long elapsed = tick - demo_recorder.structural_allowance_tick;

	if (elapsed < 0)
	{
		demo_recorder.structural_allowance = DEMO_STRUCTURAL_BURST;
		demo_recorder.structural_allowance_tick = tick;
	}
	else if (elapsed >= demo_settings.snapshot_ticks)
	{
		long more = elapsed / demo_settings.snapshot_ticks;

		demo_recorder.structural_allowance = MIN(DEMO_STRUCTURAL_BURST, demo_recorder.structural_allowance + more);
		demo_recorder.structural_allowance_tick += more * demo_settings.snapshot_ticks;
	}
}

/* ... one counted (always TRUE: it is taken) */
static boolean demo_structural_whole(
	long tick)
{
	demo_structural_refill(tick);
	if (demo_recorder.structural_allowance > 0)
	{
		demo_recorder.structural_allowance--;
		return TRUE;
	}
	demo_recorder.structural_too_often++;
	if (demo_recorder.structural_warning_tick == NONE ||
		tick - demo_recorder.structural_warning_tick >= DEMO_STRUCTURAL_WARNING_TICKS ||
		tick < demo_recorder.structural_warning_tick)
	{
		demo_recorder.structural_warning_tick = tick;
		platform_log("warning: demos: %s: a whole game state at tick %ld for what the game's code made or deleted "
			"outside the ticks, %ld more often than %d and one each %ld ticks so far (%.2f MB written): the "
			"demo grows by about 1 MB each; demo.c's records miss what made them (debug.demo_log names it)",
			demo_recorder.name, tick, demo_recorder.structural_too_often, DEMO_STRUCTURAL_BURST,
			demo_settings.snapshot_ticks, (double)demo_writer_bytes(demo_recorder.writer) / 1e6);
	}
	return TRUE;
}

/* the frames' input the ticks read, whole, where it changed since it was
last recorded (a C record) */
static void demo_input_write(
	void)
{
	long index;

	if (!demo_recorder.input)
		demo_recorder.input = demo_files_alloc((unsigned long)demo_allocations.size);
	if (!demo_recorder.input)
		return;
	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];
		unsigned char const *live = demo_allocations.base + allocation->offset;
		unsigned long start;

		if (!allocation->input ||
			(demo_recorder.input_valid && !memcmp(demo_recorder.input + allocation->offset, live, allocation->size)))
		{
			continue;
		}
		start = demo_record_begin(&demo_recorder.records, 'C');
		demo_buffer_u32(&demo_recorder.records, allocation->offset);
		demo_buffer_u32(&demo_recorder.records, allocation->size);
		demo_buffer_bytes(&demo_recorder.records, live, allocation->size);
		demo_record_end(&demo_recorder.records, start);
		memcpy(demo_recorder.input + allocation->offset, live, allocation->size);
	}
	demo_recorder.input_valid = TRUE;
}

/* the machine's players' cameras as the tick begins, where they changed
since they were last recorded (an O record): the frames move them, and the
ticks read them (material_effects.c's effects by the camera's distance,
players.c's view of the level without a unit) */
static void demo_cameras_write(
	void)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		struct observer_result const *camera;
		unsigned long start;

		if (local_player_get_player_index(local_player_index) == NONE)
			continue;
		camera = observer_get_camera(local_player_index);
		if (demo_recorder.cameras_valid &&
			!memcmp(&demo_recorder.cameras[local_player_index], camera, sizeof(*camera)))
		{
			continue;
		}
		demo_recorder.cameras[local_player_index] = *camera;
		start = demo_record_begin(&demo_recorder.records, 'O');
		demo_buffer_u8(&demo_recorder.records, (unsigned long)local_player_index);
		demo_buffer_bytes(&demo_recorder.records, camera, sizeof(*camera));
		demo_record_end(&demo_recorder.records, start);
	}
	demo_recorder.cameras_valid = TRUE;
}

static void demo_seeds_write(
	struct demo_buffer *buffer,
	unsigned long global_seed,
	unsigned long local_seed)
{
	unsigned long start = demo_record_begin(buffer, 'S');

	demo_buffer_u32(buffer, global_seed);
	demo_buffer_u32(buffer, local_seed);
	demo_record_end(buffer, start);
}

/* a part of what the game keeps outside the game state and the ticks read:
the game engine's (game_engine_port_demo_state: its flags, by the players
the game began with; its goals, the vehicles' homes, a game type's scores),
then the speakers' mouths (game_sound_port_demo_state), then the wind
(wind.c: its random walk, which the ticks step and an antenna's or a flag's
points are blown by); NULL past the last */
static void *demo_outside_part(
	short part,
	long *size)
{
	short count = 0;
	long part_size;
	void *address;

	address = game_engine_port_demo_state(part, size);
	if (address)
		return address;
	while (game_engine_port_demo_state(count, &part_size))
		count++;
	part = (short)(part - count);
	address = game_sound_port_demo_state(part, size);
	if (address)
		return address;
	count = 0;
	while (game_sound_port_demo_state(count, &part_size))
		count++;
	if (part == count)
		return wind_port_demo_state(size);
	/* (and which players are OpenCE machines', whose units a mixed game
	moves once a tick: network_standard.c) */
	if (part == count + 1)
		return network_standard_port_demo_state(size);
	/* (and the network game a host's ticks read: its players, its co-op
	switches; a playback's is the recording's) */
	if (part == count + 2)
	{
		if (!demo_player.playing)
		{
			struct network_game *game = game_connection() == _game_connection_network_server ?
				network_game_get_game() : NULL;

			if (game)
				memcpy(&demo_network_game, game, sizeof(demo_network_game));
			else
				memset(&demo_network_game, 0, sizeof(demo_network_game));
		}
		*size = sizeof(demo_network_game);
		return &demo_network_game;
	}
	/* (and what the ticks asked of the main loop, with its timers: main.c;
	then what the co-op host's ticks read from outside the game state:
	network_coop.c) */
	part = (short)(part - count - 3);
	address = main_port_demo_state(part, size);
	if (address)
		return address;
	count = 0;
	while (main_port_demo_state(count, &part_size))
		count++;
	if (part >= count)
		return network_coop_port_demo_state((short)(part - count), size);
	return NULL;
}

/* ... its parts one after another into state: its size, 0 if it cannot be
had */
static unsigned long demo_engine_state_get(
	unsigned char *state)
{
	unsigned long size = 0;
	short part;
	long part_size;
	void *address;

	for (part = 0; (address = demo_outside_part(part, &part_size)) != NULL; part++)
	{
		if (part_size < 0 || (unsigned long)part_size > DEMO_ENGINE_STATE_SIZE - size)
			return 0;
		memcpy(state + size, address, (size_t)part_size);
		size += (unsigned long)part_size;
	}
	return size;
}

/* ... where it changed since it was last recorded (a G record: its size,
then the runs of bytes that changed, each its offset, its size and its
bytes), or always (a snapshot's, whole: one run of it all): the game
engine's code run outside the ticks (a kill dealt there) changes it, the
game's start set it by its players, and a co-op host's netcode after each
tick changes its parts (network_coop.c) */
static void demo_engine_state_write(
	boolean whole)
{
	static unsigned char *state;
	unsigned long size;
	unsigned long start;

	if (!state)
		state = demo_files_alloc(DEMO_ENGINE_STATE_SIZE);
	if (!demo_recorder.engine_state)
		demo_recorder.engine_state = demo_files_alloc(DEMO_ENGINE_STATE_SIZE);
	if (!state || !demo_recorder.engine_state)
		return;
	size = demo_engine_state_get(state);
	if (!size || (!whole && size == demo_recorder.engine_state_size &&
		!memcmp(state, demo_recorder.engine_state, size)))
	{
		return;
	}
	start = demo_record_begin(&demo_recorder.records, 'G');
	demo_buffer_u32(&demo_recorder.records, size);
	if (whole || size != demo_recorder.engine_state_size)
	{
		demo_buffer_u32(&demo_recorder.records, 0);
		demo_buffer_u32(&demo_recorder.records, size);
		demo_buffer_bytes(&demo_recorder.records, state, size);
	}
	else
	{
		unsigned long position = 0;

		while (position < size)
		{
			unsigned long run_end;

			if (state[position] == demo_recorder.engine_state[position])
			{
				position++;
				continue;
			}
			/* (a run, two this near joined) */
			for (run_end = position + 1; run_end < size; run_end++)
			{
				unsigned long next;

				if (state[run_end] != demo_recorder.engine_state[run_end])
					continue;
				for (next = run_end; next < size && next < run_end + DEMO_PATCH_JOIN_BYTES &&
					state[next] == demo_recorder.engine_state[next]; next++)
				{
				}
				if (next == size || next == run_end + DEMO_PATCH_JOIN_BYTES)
					break;
				run_end = next;
			}
			demo_buffer_u32(&demo_recorder.records, position);
			demo_buffer_u32(&demo_recorder.records, run_end - position);
			demo_buffer_bytes(&demo_recorder.records, state + position, run_end - position);
			position = run_end;
		}
	}
	demo_record_end(&demo_recorder.records, start);
	memcpy(demo_recorder.engine_state, state, size);
	demo_recorder.engine_state_size = size;
}

/* (the recording's) the game engine's state as the tick left it */
static void demo_engine_state_take(
	void)
{
	if (!demo_recorder.engine_state)
		demo_recorder.engine_state = demo_files_alloc(DEMO_ENGINE_STATE_SIZE);
	if (demo_recorder.engine_state)
		demo_recorder.engine_state_size = demo_engine_state_get(demo_recorder.engine_state);
}

/* (the playback's) a G record's runs written over the state as the
playback has it, which is then put back, part by part, if its size is this
build's and each run is in it */
static void demo_engine_state_set(
	unsigned char const *bytes,
	unsigned long length)
{
	static unsigned char *state;
	unsigned long position = 4;
	unsigned long size;
	short part;
	long part_size;
	void *address;

	if (!state)
		state = demo_files_alloc(DEMO_ENGINE_STATE_SIZE);
	if (!state || length < 4)
		return;
	size = demo_engine_state_get(state);
	if (!size || demo_read_u32(bytes) != size)
		return;
	while (position < length)
	{
		unsigned long offset, run;

		if (length - position < 8)
			return;
		offset = demo_read_u32(bytes + position);
		run = demo_read_u32(bytes + position + 4);
		position += 8;
		if (run > length - position || offset > size || run > size - offset)
			return;
		memcpy(state + offset, bytes + position, run);
		position += run;
	}
	position = 0;
	for (part = 0; (address = demo_outside_part(part, &part_size)) != NULL; part++)
	{
		memcpy(address, state + position, (size_t)part_size);
		position += (unsigned long)part_size;
	}
}

static void demo_record_stop(
	char const *reason)
{
	if (!demo_recorder.recording)
		return;
	if (demo_recorder.writer)
	{
		unsigned long start = demo_record_begin(&demo_recorder.records, 'E');

		demo_buffer_u32(&demo_recorder.records, (unsigned long)demo_recorder.last_tick);
		demo_record_end(&demo_recorder.records, start);
		demo_records_flush();
		demo_recorder.written = demo_writer_close(demo_recorder.writer);
		demo_recorder.writer = NULL;
	}
	platform_log("demos: %s ended (%s): %ld ticks, %ld differences from outside the ticks (%lu bytes), %ld "
		"whole game states for what the game's code made or deleted there (%ld more often than allowed); %.2f MB; the "
		"recording took %.3f ms a tick of the game's thread", demo_recorder.name, reason, demo_recorder.ticks,
		demo_recorder.patches, demo_recorder.patch_bytes, demo_recorder.structural_snapshots,
		demo_recorder.structural_too_often, (double)demo_recorder.written / 1e6,
		demo_recorder.ticks ? (double)demo_recorder.work_microseconds / 1000.0 / (double)demo_recorder.ticks : 0.0);
	demo_recorder.recording = FALSE;
	demo_recorder.shadow_valid = FALSE;
	demo_settings.read = FALSE;
}

/* whether this tick's game is one to record: a game of a map (not the main
menu's scene, nor the menus' pictures' pass), this machine's own or its
host's, not a demo playing */
static boolean demo_record_wanted(
	void)
{
	demo_settings_read();
	return demo_settings.record && !demo_player.playing && !demo_player.requested && !game_time_menu_scene() &&
		!menu_pictures_preparing() && game_connection() != _game_connection_network_client &&
		global_scenario_index != NONE && !demo_recorder.stopped;
}

/* the demo being recorded ended as the game quits: its last blocks written */
static void demo_record_at_exit(
	void)
{
	demo_record_stop("the game quit");
}

static void demo_record_start(
	long tick)
{
	static boolean at_exit;

	unsigned char header[DEMO_HEADER_SIZE];
	char folder[512], path[768];
	char const *map = tag_get_name(global_scenario_index);
	char const *short_name = strrchr(map, '\\');
	unsigned long stamp = (unsigned long)time(NULL);

	short_name = short_name ? short_name + 1 : map;
	if (!demo_recorder.shadow)
		demo_recorder.shadow = demo_files_alloc((size_t)demo_allocations.size);
	if (!demo_recorder.shadow || !demo_files_folder(folder, sizeof(folder), FALSE))
	{
		demo_recorder.stopped = TRUE;
		return;
	}
	{
		time_t now = (time_t)stamp;
		struct tm const *moment = localtime(&now);

		if (moment)
		{
			snprintf(demo_recorder.name, sizeof(demo_recorder.name), "%04d%02d%02d-%02d%02d%02d-%.40s.demo",
				moment->tm_year + 1900, moment->tm_mon + 1, moment->tm_mday, moment->tm_hour, moment->tm_min,
				moment->tm_sec, short_name);
		}
		else
			snprintf(demo_recorder.name, sizeof(demo_recorder.name), "%lu-%.40s.demo", stamp, short_name);
	}
	snprintf(path, sizeof(path), "%s/%s", folder, demo_recorder.name);
	demo_files_retain(demo_settings.keep_count > 0 ? demo_settings.keep_count - 1 : 0, demo_settings.keep_megabytes,
		NULL);
	demo_header_write(header);
	header[80 + 7 * 4] = (unsigned char)stamp;
	header[80 + 7 * 4 + 1] = (unsigned char)(stamp >> 8);
	header[80 + 7 * 4 + 2] = (unsigned char)(stamp >> 16);
	header[80 + 7 * 4 + 3] = (unsigned char)(stamp >> 24);
	demo_recorder.writer = demo_writer_open(path, header, sizeof(header));
	if (!demo_recorder.writer)
	{
		demo_recorder.stopped = TRUE;
		return;
	}
	if (!at_exit)
		at_exit = atexit(demo_record_at_exit) == 0;
	demo_recorder.recording = TRUE;
	demo_recorder.records.size = 0;
	demo_recorder.records.failed = FALSE;
	demo_recorder.scratch.failed = FALSE;
	demo_recorder.block_first_tick = tick;
	demo_recorder.block_ticks = 0;
	demo_recorder.ticks = 0;
	demo_recorder.patches = 0;
	demo_recorder.patch_bytes = 0;
	demo_recorder.structural_snapshots = 0;
	demo_recorder.structural_allowance = DEMO_STRUCTURAL_BURST;
	demo_recorder.structural_allowance_tick = tick;
	demo_recorder.structural_too_often = 0;
	demo_recorder.structural_warning_tick = NONE;
	demo_recorder.netcode = FALSE;
	demo_recorder.written = 0;
	demo_recorder.work_microseconds = 0;
	demo_recorder.loaded = FALSE;
	demo_recorder.input_valid = FALSE;
	demo_recorder.cameras_valid = FALSE;
	/* (a multiplayer game's variant, for its playback's game) */
	{
		unsigned long start = demo_record_begin(&demo_recorder.records, 'V');

		demo_buffer_u32(&demo_recorder.records, sizeof(game_variant_global));
		demo_buffer_bytes(&demo_recorder.records, &game_variant_global, sizeof(game_variant_global));
		demo_buffer_u32(&demo_recorder.records, (unsigned long)sizeof(struct game_variant_options));
		demo_buffer_bytes(&demo_recorder.records, game_variant_options_get(), (unsigned long)sizeof(struct game_variant_options));
		demo_record_end(&demo_recorder.records, start);
	}
	platform_log("demos: recording %s", demo_recorder.name);
}

static void demo_record_tick_begin(
	long tick)
{
	unsigned long seeds[2];
	unsigned long start;
	boolean structural;

	if (!demo_recorder.recording)
	{
		if (!demo_record_wanted())
			return;
		demo_record_start(tick);
		if (!demo_recorder.recording)
			return;
		start = demo_record_begin(&demo_recorder.records, 'T');
		demo_buffer_u32(&demo_recorder.records, (unsigned long)tick);
		demo_buffer_u32(&demo_recorder.records, (unsigned long)sim_rate_next());
		demo_record_end(&demo_recorder.records, start);
		demo_snapshot_block(tick, FLAG(_demo_snapshot_start_bit));
	}
	else
	{
		start = demo_record_begin(&demo_recorder.records, 'T');
		demo_buffer_u32(&demo_recorder.records, (unsigned long)tick);
		demo_buffer_u32(&demo_recorder.records, (unsigned long)sim_rate_next());
		demo_record_end(&demo_recorder.records, start);
		structural = FALSE;
		if (!demo_recorder.loaded)
		{
			if (demo_patch_write('P') && demo_structural_whole(tick))
			{
				demo_recorder.loaded = TRUE;
				structural = TRUE;
				demo_recorder.structural_snapshots++;
			}
		}
		/* (a whole game state for a reload too, a death's revert to the last
		checkpoint as much as the pause menu's: the checkpoint the playback's
		own revert takes was saved as the frames had left the game state, and
		the frames are not the recording's) */
		if (demo_recorder.loaded)
		{
			if (config_boolean("debug.demo_log"))
				platform_log("demos: the whole game state at tick %ld (%s)", tick,
					structural ? "made or deleted outside the ticks" : "a saved game or checkpoint loaded");
			demo_snapshot_block(tick, FLAG(_demo_snapshot_loaded_bit));
		}
		else
		{
			demo_input_write();
			random_math_port_seeds(&seeds[0], &seeds[1]);
			if (seeds[0] != demo_recorder.end_seeds[0] || seeds[1] != demo_recorder.end_seeds[1])
				demo_seeds_write(&demo_recorder.records, seeds[0], seeds[1]);
			if (tick - demo_recorder.last_snapshot_tick >= demo_settings.snapshot_ticks ||
				tick < demo_recorder.last_snapshot_tick)
			{
				demo_snapshot_block(tick, FLAG(_demo_snapshot_seek_bit));
			}
		}
	}
	demo_cameras_write();
	demo_engine_state_write(demo_recorder.last_snapshot_tick == tick);
	demo_recorder.loaded = FALSE;
	demo_recorder.in_tick = TRUE;
	/* (a K's input is the input as it then was) */
	if (demo_recorder.input && demo_recorder.last_snapshot_tick == tick)
	{
		long index;

		for (index = 0; index < demo_allocations.count; index++)
		{
			if (demo_allocations.list[index].input)
			{
				memcpy(demo_recorder.input + demo_allocations.list[index].offset,
					demo_allocations.base + demo_allocations.list[index].offset, demo_allocations.list[index].size);
			}
		}
	}
	if (!demo_recorder.block_ticks)
		demo_recorder.block_first_tick = tick;
}

static void demo_record_tick_end(
	long tick)
{
	unsigned long start;

	demo_recorder.in_tick = FALSE;
	if (!demo_recorder.recording)
		return;
	/* (the tick's end, after every record its code made: its T's number,
	the clock's before it counts the tick) */
	start = demo_record_begin(&demo_recorder.records, 'Z');
	demo_buffer_u32(&demo_recorder.records, (unsigned long)(tick - 1));
	demo_record_end(&demo_recorder.records, start);
	if (tick % DEMO_HASH_TICKS == 0)
	{
		static unsigned long allocation_hashes[MAXIMUM_DEMO_ALLOCATIONS];
		unsigned long hash = demo_state_hash(allocation_hashes);
		long index;

		/* (the whole's, then each allocation's, to name where a playback
		parts from it) */
		start = demo_record_begin(&demo_recorder.records, 'H');
		demo_buffer_u32(&demo_recorder.records, (unsigned long)tick);
		demo_buffer_u32(&demo_recorder.records, hash);
		for (index = 0; index < demo_allocations.count; index++)
			demo_buffer_u32(&demo_recorder.records, allocation_hashes[index]);
		demo_record_end(&demo_recorder.records, start);
	}
	demo_shadow_take();
	demo_engine_state_take();
	random_math_port_seeds(&demo_recorder.end_seeds[0], &demo_recorder.end_seeds[1]);
	demo_recorder.ticks++;
	demo_recorder.last_tick = tick;
	/* (each 5 minutes, how the recording goes) */
	if (demo_recorder.ticks % (5 * 60 * TICKS_PER_SECOND) == 0)
	{
		platform_log("demos: %s: %ld minutes, %.2f MB written, %.3f ms a tick of the game's thread", demo_recorder.name,
			demo_recorder.ticks / (60 * TICKS_PER_SECOND), (double)demo_writer_bytes(demo_recorder.writer) / 1e6,
			(double)demo_recorder.work_microseconds / 1000.0 / (double)demo_recorder.ticks);
	}
	if (++demo_recorder.block_ticks >= DEMO_BLOCK_TICKS)
		demo_records_flush();
	if (demo_recorder.records.failed || demo_recorder.scratch.failed)
	{
		demo_record_stop("out of memory");
		demo_recorder.stopped = TRUE;
	}
}

void demo_record_tick_actions_begin(
	void)
{
	random_math_port_seeds(&demo_recorder.bots_seeds[0], &demo_recorder.bots_seeds[1]);
}

void demo_record_tick_actions(
	struct player_action const *actions)
{
	struct data_iterator iterator;
	unsigned long seeds[2];
	unsigned long start;

	if (!demo_recorder.recording)
		return;
	start = demo_record_begin(&demo_recorder.records, 'A');
	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator))
	{
		short index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		demo_buffer_u8(&demo_recorder.records, (unsigned long)index);
		demo_buffer_bytes(&demo_recorder.records, &actions[index], sizeof(actions[index]));
	}
	demo_record_end(&demo_recorder.records, start);
	/* (the seeds the bots took: their playback has no bots) */
	random_math_port_seeds(&seeds[0], &seeds[1]);
	if (seeds[0] != demo_recorder.bots_seeds[0] || seeds[1] != demo_recorder.bots_seeds[1])
		demo_seeds_write(&demo_recorder.records, seeds[0], seeds[1]);
}

static void demo_unit_controls_get(
	long unit_index,
	struct demo_unit_controls *controls)
{
	struct unit_datum *unit = unit_get(unit_index);

	controls->control_flags = unit->unit.control_flags;
	controls->throttle = unit->unit.throttle;
	controls->primary_trigger = unit->unit.primary_trigger;
	controls->desired_facing_vector = unit->unit.desired_facing_vector;
	controls->desired_aiming_vector = unit->unit.desired_aiming_vector;
	controls->desired_looking_vector = unit->unit.desired_looking_vector;
	controls->pressed_flags = 0;
}

void demo_substep_begin(
	short substep_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (substep_index == 0)
		return;
	if (demo_player.playing)
	{
		demo_player.pressed_count = 0;
		return;
	}
	if (!demo_recorder.recording)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		short index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		demo_recorder.substep_units[index] = player->unit_index;
		if (player->unit_index != NONE && unit_try_and_get(player->unit_index))
			demo_unit_controls_get(player->unit_index, &demo_recorder.substep_before[index]);
		else
			demo_recorder.substep_units[index] = NONE;
	}
}

static void demo_record_substep_end(
	short substep_index,
	short substep_count)
{
	struct data_iterator iterator;
	struct player_datum *player;
	unsigned long start;

	start = demo_record_begin(&demo_recorder.records, 'U');
	demo_buffer_u32(&demo_recorder.records, (unsigned long)game_time_get());
	demo_buffer_u8(&demo_recorder.records, (unsigned long)substep_index);
	demo_buffer_u8(&demo_recorder.records, (unsigned long)substep_count);
	data_iterator_new(&iterator, player_data);
	/* (the first sub-step's controls are the tick's own, its A) */
	while (substep_index > 0 && (player = data_iterator_next(&iterator)) != NULL)
	{
		short index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct demo_unit_controls after;

		if (player->unit_index == NONE || player->unit_index != demo_recorder.substep_units[index] ||
			!unit_try_and_get(player->unit_index))
		{
			continue;
		}
		demo_unit_controls_get(player->unit_index, &after);
		after.pressed_flags = player_control_substep_pressed_flags(player->unit_index);
		if (!after.pressed_flags && !memcmp(&after, &demo_recorder.substep_before[index], sizeof(after)))
			continue;
		demo_buffer_u8(&demo_recorder.records, (unsigned long)index);
		demo_buffer_u32(&demo_recorder.records, (unsigned long)player->unit_index);
		demo_buffer_bytes(&demo_recorder.records, &after, sizeof(after));
	}
	demo_record_end(&demo_recorder.records, start);
}

void demo_note_state_loaded(
	void)
{
	demo_recorder.loaded = TRUE;
}

void demo_note_new_map(
	void)
{
	demo_record_stop("a new map");
	demo_recorder.stopped = FALSE;
	/* (a demo's map as it loads: its game's variant, for its game engine;
	a multiplayer game's plays as a game of this machine's own) */
	if (demo_player.playing && !demo_player.started && demo_player.variant_read)
	{
		game_set_game_variant(&demo_player.variant);
		game_set_game_variant_options(&demo_player.variant_options);
	}
	if (demo_player.playing && demo_player.started)
		demo_player.ended = TRUE;
}

/* the clip key pressed (xinput_sdl.c, as it reads the keys): marked at the
next frame's start, on the game's thread */
static volatile long demo_clip_requests;

void demo_clip_request(
	void)
{
	__sync_fetch_and_add(&demo_clip_requests, 1);
}

void demo_bookmark(
	void)
{
	unsigned long start;

	if (!demo_recorder.recording)
		return;
	demo_settings_read();
	start = demo_record_begin(&demo_recorder.records, 'B');
	demo_buffer_u32(&demo_recorder.records, (unsigned long)game_time_get());
	demo_buffer_u32(&demo_recorder.records, (unsigned long)(demo_settings.clip_seconds * 1000.f));
	demo_record_end(&demo_recorder.records, start);
	platform_log("demos: the last %.0f s of %s marked as a clip", demo_settings.clip_seconds, demo_recorder.name);
}

void demo_record_bot_command(
	char const *line)
{
	unsigned long start;

	if (!demo_recorder.recording)
		return;
	start = demo_record_begin(&demo_recorder.records, 'W');
	demo_buffer_u32(&demo_recorder.records, (unsigned long)game_time_get());
	demo_buffer_bytes(&demo_recorder.records, line, (unsigned long)strlen(line));
	demo_record_end(&demo_recorder.records, start);
}

/* ---------- what the frames put in while a tick is open */

/* what changed since the shadow was taken, as an M record of the sub-step
it comes before (none if nothing did), written as the bytes are, whatever
made them: the open tick cannot take a whole game state. (Of sub-step 0:
what came between two ticks before a netcode event, demo_event_begin.)
Whether an object or an element was made or deleted */
static boolean demo_open_patch_write(
	short substep_index)
{
	unsigned long start = demo_record_begin(&demo_recorder.records, 'M');
	unsigned long size_before;
	boolean structural;

	demo_buffer_u8(&demo_recorder.records, (unsigned long)substep_index);
	size_before = demo_recorder.records.size;
	structural = demo_patch_write('F');
	/* (the F record's own type and length dropped, its ranges kept) */
	if (demo_recorder.records.size >= size_before + 5)
	{
		memmove(demo_recorder.records.data + size_before, demo_recorder.records.data + size_before + 5,
			demo_recorder.records.size - size_before - 5);
		demo_recorder.records.size -= 5;
	}
	if (demo_recorder.records.size == size_before)
		demo_recorder.records.size = start - 1;
	else
		demo_record_end(&demo_recorder.records, start);
	return structural;
}

void demo_tick_pause(
	void)
{
	demo_recorder.in_tick = FALSE;
	demo_recorder.paused = FALSE;
	/* (the playback's: as the sub-step left it, for the next's start) */
	if (demo_player.playing && demo_player.started && demo_recorder.shadow)
	{
		demo_shadow_take();
		demo_recorder.paused = TRUE;
		return;
	}
	if (demo_player.playing || !demo_recorder.recording)
		return;
	demo_shadow_take();
	random_math_port_seeds(&demo_recorder.pause_seeds[0], &demo_recorder.pause_seeds[1]);
	demo_recorder.paused = TRUE;
}

static void demo_playback_resume(short substep_index);
static void demo_playback_open_records(short substep_index);

void demo_substep_resume(
	short substep_index)
{
	if (demo_player.playing)
	{
		if (demo_player.started && demo_recorder.paused)
			demo_shadow_restore();
		demo_recorder.paused = FALSE;
		if (demo_player.started)
			demo_playback_resume(substep_index);
		demo_recorder.in_tick = TRUE;
		return;
	}
	if (!demo_recorder.recording)
		return;
	demo_recorder.in_tick = TRUE;
	if (!demo_recorder.paused)
		return;
	demo_recorder.paused = FALSE;
	demo_open_patch_write(substep_index);
	/* (and the frames' input, which they moved meanwhile and the sub-step
	reads: the HUD's crosshair by the aim assist's level; the cameras, by
	which a hit's damage indicator points; and the seeds, an event's code
	took) */
	demo_cameras_write();
	demo_input_write();
	{
		unsigned long seeds[2];

		random_math_port_seeds(&seeds[0], &seeds[1]);
		if (seeds[0] != demo_recorder.pause_seeds[0] || seeds[1] != demo_recorder.pause_seeds[1])
			demo_seeds_write(&demo_recorder.records, seeds[0], seeds[1]);
	}
	demo_engine_state_write(FALSE);
}

/* ---------- what the netcode puts in, inside a tick */

void demo_foreign_begin(
	void)
{
	demo_recorder.foreign = FALSE;
	if (!demo_recorder.recording || !demo_recorder.in_tick || game_connection() != _game_connection_network_server)
		return;
	/* (the shadow, the tick's start's baseline already taken, is free until
	the tick's end) */
	demo_shadow_take();
	demo_recorder.foreign = TRUE;
}

static void demo_playback_foreign(void);

void demo_foreign_end(
	void)
{
	if (demo_player.playing && demo_player.started)
	{
		if (demo_recorder.in_tick)
			demo_playback_foreign();
		return;
	}
	if (!demo_recorder.foreign)
		return;
	demo_recorder.foreign = FALSE;
	demo_patch_write('F');
}

static boolean demo_playback_decide(boolean decision);

boolean demo_decide(
	boolean decision)
{
	unsigned long start;

	if (demo_player.playing && demo_player.started)
		return demo_recorder.in_tick ? demo_playback_decide(decision) : decision;
	if (!demo_recorder.recording || !demo_recorder.in_tick || game_connection() != _game_connection_network_server)
		return decision;
	start = demo_record_begin(&demo_recorder.records, 'D');
	demo_buffer_u8(&demo_recorder.records, decision ? 1 : 0);
	demo_record_end(&demo_recorder.records, start);
	return decision;
}

static char demo_playback_record(unsigned char const **bytes, unsigned long *length, boolean take);

boolean demo_decide_frames(
	boolean decision)
{
	unsigned char const *bytes;
	unsigned long length;
	unsigned long start;

	if (demo_player.playing && demo_player.started)
	{
		if (!demo_recorder.in_tick || demo_playback_record(&bytes, &length, FALSE) != 'L' || length < 1)
			return decision;
		demo_playback_record(&bytes, &length, TRUE);
		return bytes[0] != 0;
	}
	if (!demo_recorder.recording || !demo_recorder.in_tick)
		return decision;
	start = demo_record_begin(&demo_recorder.records, 'L');
	demo_buffer_u8(&demo_recorder.records, decision ? 1 : 0);
	demo_record_end(&demo_recorder.records, start);
	return decision;
}

void demo_decide_reals(
	long object_index,
	real *values,
	short count)
{
	unsigned char const *bytes;
	unsigned long length;
	unsigned long start;
	short index;

	if (count <= 0 || count > 8)
		return;
	if (demo_player.playing && demo_player.started)
	{
		real played[8];

		if (!demo_recorder.in_tick || demo_playback_record(&bytes, &length, FALSE) != 'I' ||
			length != 4 + count * sizeof(real) || demo_read_u32(bytes) != (unsigned long)object_index)
		{
			static long reported;

			if (config_boolean("debug.demo_verify") && reported++ < 10)
			{
				char type = demo_playback_record(&bytes, &length, FALSE);

				platform_log("demos: verify: tick %ld, object %lx's numbers not the next record (a %c, of %lx)",
					demo_player.last_tick, (unsigned long)object_index, type ? type : '0',
					type == 'I' && length >= 4 ? demo_read_u32(bytes) : 0);
			}
			return;
		}
		demo_playback_record(&bytes, &length, TRUE);
		memcpy(played, bytes + 4, length - 4);
		for (index = 0; index < count; index++)
		{
			if (!valid_real(played[index]))
				return;
		}
		memcpy(values, played, length - 4);
		return;
	}
	if (!demo_recorder.recording || !demo_recorder.in_tick)
		return;
	start = demo_record_begin(&demo_recorder.records, 'I');
	demo_buffer_u32(&demo_recorder.records, (unsigned long)object_index);
	demo_buffer_bytes(&demo_recorder.records, values, (unsigned long)count * sizeof(real));
	demo_record_end(&demo_recorder.records, start);
}

/* ---------- what the netcode does outside the ticks */

boolean demo_event_begin(
	char type,
	void const *bytes,
	unsigned long size)
{
	unsigned long seeds[2];
	unsigned long start;

	if (demo_player.playing || !demo_recorder.recording || demo_recorder.in_tick || !demo_recorder.shadow_valid ||
		game_connection() != _game_connection_network_server)
	{
		return FALSE;
	}
	/* (what reached the game state before it, as the difference it comes
	after: of the open tick's next sub-step, else before the next tick; one
	of the game's code there is the whole game state at the next tick's
	start) */
	if (demo_recorder.paused)
		demo_open_patch_write(sim_substep_next());
	else if (demo_open_patch_write(0) && demo_structural_whole(game_time_get()))
		demo_recorder.loaded = TRUE;
	/* (the cameras as the frames left them, which the event's code reads:
	a hit's damage indicator points from its player's camera) */
	demo_cameras_write();
	/* (the seeds as the event's code takes them: the game's global seed,
	the frames' local seed, which its effects take) */
	random_math_port_seeds(&seeds[0], &seeds[1]);
	start = demo_record_begin(&demo_recorder.records, type);
	demo_buffer_u32(&demo_recorder.records, seeds[0]);
	demo_buffer_u32(&demo_recorder.records, random_math_port_frame_seed());
	demo_buffer_bytes(&demo_recorder.records, bytes, size);
	demo_record_end(&demo_recorder.records, start);
	return TRUE;
}

void demo_event_end(
	void)
{
	/* (what the event's code did is the playback's code's to do again: the
	differences after it are from where it left the game state) */
	if (!demo_player.playing && demo_recorder.recording && !demo_recorder.in_tick)
		demo_shadow_take();
}

/* ---------- what the netcode puts in after a tick */

void demo_netcode_begin(
	void)
{
	unsigned long size;

	demo_recorder.netcode = FALSE;
	if (demo_player.playing || !demo_recorder.recording || demo_recorder.in_tick || demo_recorder.paused ||
		!demo_recorder.shadow_valid || game_connection() != _game_connection_network_server)
	{
		return;
	}
	/* (what reached the game state before it, as a difference of its own:
	one of the game's code there is the whole game state at the next
	tick's start, as before an event) */
	size = demo_recorder.records.size;
	if (demo_open_patch_write(0) && demo_structural_whole(game_time_get()))
		demo_recorder.loaded = TRUE;
	if (demo_recorder.records.size != size)
		demo_shadow_take();
	demo_recorder.netcode = TRUE;
}

void demo_netcode_end(
	void)
{
	unsigned long size;

	if (!demo_recorder.netcode)
		return;
	demo_recorder.netcode = FALSE;
	if (!demo_recorder.recording || demo_recorder.in_tick)
		return;
	/* (what it changed, as the bytes are: the units it moved, with their
	cluster references, as the clients' predictions taken at a tick's
	start are, F) */
	size = demo_recorder.records.size;
	demo_open_patch_write(0);
	if (demo_recorder.records.size != size)
		demo_shadow_take();
}

/* ---------- playback */

boolean demo_playing(
	void)
{
	return demo_player.playing;
}

long demo_playback_sim_rate(
	void)
{
	return demo_player.playing && demo_player.started ? demo_player.rate : 0;
}

boolean demo_playback_rules(
	unsigned long *rules)
{
	if (!demo_player.playing)
		return FALSE;
	*rules = demo_player.rules;
	return TRUE;
}

/* (only inside the ticks: the frames, the menus and the netcode stay
those of a game of this machine's own. A multiplayer game's ticks run as
this machine's, as they always did: what the host decided by its machines
in them is recorded, D and F) */
static boolean demo_playback_as_host(
	void)
{
	return demo_player.playing && demo_recorder.in_tick && demo_player.connection == _game_connection_network_server &&
		global_scenario && global_scenario->type == _scenario_type_solo && !game_engine_running();
}

boolean demo_playback_connection(
	short *connection)
{
	if (!demo_playback_as_host())
		return FALSE;
	*connection = (short)demo_player.connection;
	return TRUE;
}

struct network_game *demo_playback_network_game(
	void)
{
	return demo_playback_as_host() ? &demo_network_game : NULL;
}

struct network_game *demo_playback_recorded_game(
	void)
{
	return demo_player.playing && demo_player.connection == _game_connection_network_server ? &demo_network_game :
		NULL;
}

/* a playback over: the game quits (debug.demo_quit, or a demo that cannot be
played), or goes back to the main menu */
static void demo_playback_end(
	boolean quit)
{
	if (quit)
	{
		platform_log("exiting after the demo (debug.demo_quit)");
		exit(0);
	}
	main_goto_main_menu();
}

static void demo_playback_stop(
	char const *reason,
	boolean failed)
{
	if (!demo_player.playing)
		return;
	platform_log("demos: the playback of %s ended at tick %ld (%s): %ld ticks played, %ld hashes as recorded%s; "
		"%ld of the recording's records of the ticks' code left untaken", demo_player.path, demo_player.last_tick,
		reason, demo_player.ticks, demo_player.hashes_matched,
		demo_player.diverged ? ", but it parted from the recording" : "", demo_player.records_left);
	demo_reader_close(demo_player.file);
	demo_player.file = NULL;
	demo_files_free(demo_player.blocks);
	demo_player.blocks = NULL;
	demo_files_free(demo_player.block);
	demo_player.block = NULL;
	demo_player.playing = FALSE;
	demo_player.started = FALSE;
	demo_playback_end(demo_player.quit_at_end || failed);
}

/* the next block the playback takes (a seek's snapshot skipped) read and
unpacked: whether there was one */
/* a block read and unpacked into the playback's block: whether it was */
static boolean demo_playback_read_block(
	struct demo_block_entry const *entry)
{
	unsigned char *packed;
	boolean unpacked;

	{
		packed = demo_files_alloc(entry->packed_size ? entry->packed_size : 1);
		demo_files_free(demo_player.block);
		demo_player.block = demo_files_alloc(entry->size ? entry->size : 1);
		demo_player.block_size = 0;
		demo_player.cursor = 0;
		if (!packed || !demo_player.block)
		{
			demo_files_free(packed);
			return FALSE;
		}
		unpacked = demo_reader_read(demo_player.file, entry->offset + DEMO_BLOCK_HEADER_SIZE, packed,
				entry->packed_size) &&
			demo_files_unpack(packed, entry->packed_size, demo_player.block, entry->size);
		demo_files_free(packed);
		if (!unpacked)
			return FALSE;
		demo_player.block_size = entry->size;
		return TRUE;
	}
}

static struct demo_allocation const *demo_allocation_holding(unsigned long offset, unsigned long size);

/* (debug.demo_verify) a seek's snapshot, as the playback reaches its tick,
compared with the playback's game state: the first bytes apart in each
allocation the ticks keep logged (the objects' by object), to find where a
playback parts */
static void demo_playback_verify(
	struct demo_block_entry const *entry)
{
	unsigned char *packed = demo_files_alloc(entry->packed_size);
	unsigned char *data = demo_files_alloc(entry->size);
	unsigned long position = 21;
	long logged = 0;

	if (packed && data && demo_reader_read(demo_player.file, entry->offset + DEMO_BLOCK_HEADER_SIZE, packed,
		entry->packed_size) && demo_files_unpack(packed, entry->packed_size, data, entry->size) && entry->size > 21)
	{
		demo_skips_find();
		while (position + 8 <= entry->size && logged < 40)
		{
			unsigned long offset = demo_read_u32(data + position);
			unsigned long size = demo_read_u32(data + position + 4);
			struct demo_allocation const *allocation;
			unsigned long index;

			position += 8;
			if (size > entry->size - position)
				break;
			allocation = demo_allocation_holding(offset, size);
			if (allocation && !allocation->frames)
			{
				for (index = 0; index + 4 <= size && logged < 40; index += 4)
				{
					unsigned long address = (unsigned long)(demo_allocations.base + offset + index);
					long skip;
					boolean skipped = FALSE;

					if (!memcmp(data + position + index, demo_allocations.base + offset + index, 4))
						continue;
					for (skip = 0; skip < demo_skip_count; skip++)
						skipped |= demo_skips[skip] == address;
					if (skipped)
						continue;
					{
						char where[160];

						where[0] = 0;
						/* (in the objects: which object, and where in it) */
						if (!strcmp(allocation->name, "objects"))
						{
							struct memory_pool const *pool = (struct memory_pool const *)(demo_allocations.base + offset);
							struct memory_pool_block const *block;
							long blocks = 0;

							for (block = pool->first_block; block && blocks++ < 100000; block = block->next_block)
							{
								unsigned char const *start = (unsigned char const *)block;

								if ((unsigned long)address >= (unsigned long)start &&
									(unsigned long)address < (unsigned long)start + (unsigned long)block->size)
								{
									struct object_datum const *object = (struct object_datum const *)(block + 1);

									snprintf(where, sizeof(where), " (object %s, type %d, +0x%lx, parent %lx)",
										tag_get_name(object->definition_index), object->object.type,
										(unsigned long)address - (unsigned long)object,
										(unsigned long)object->object.parent_object_index);
									break;
								}
							}
						}
						platform_log("demos: verify tick %lu: %s +%lu%s: recorded %08lx, played %08lx", entry->first_tick,
							allocation->name, index, where, demo_read_u32(data + position + index),
							demo_read_u32(demo_allocations.base + offset + index));
					}
					logged++;
				}
			}
			position += size;
		}
		if (!logged)
			platform_log("demos: verify tick %lu: as recorded", entry->first_tick);
	}
	demo_files_free(packed);
	demo_files_free(data);
}

static boolean demo_playback_next_block(
	void)
{
	while (demo_player.next_block < demo_player.block_count)
	{
		struct demo_block_entry const *entry = &demo_player.blocks[demo_player.next_block++];

		if (entry->kind == _demo_block_seek_snapshot)
		{
			if (config_boolean("debug.demo_verify"))
				demo_playback_verify(entry);
			continue;
		}
		return demo_playback_read_block(entry);
	}
	return FALSE;
}

/* the next record (peeked, or taken): its type, its bytes and its length;
0 at the demo's end or a damaged record */
static char demo_playback_record(
	unsigned char const **bytes,
	unsigned long *length,
	boolean take)
{
	unsigned long size;

	while (demo_player.cursor >= demo_player.block_size)
	{
		if (!demo_playback_next_block())
			return 0;
	}
	if (demo_player.block_size - demo_player.cursor < 5)
		return 0;
	size = demo_read_u32(demo_player.block + demo_player.cursor + 1);
	if (size > demo_player.block_size - demo_player.cursor - 5)
		return 0;
	*bytes = demo_player.block + demo_player.cursor + 5;
	*length = size;
	{
		char type = (char)demo_player.block[demo_player.cursor];

		if (take)
			demo_player.cursor += 5 + size;
		return type;
	}
}

/* the allocation that holds [offset, offset+size) wholly, or NULL */
static struct demo_allocation const *demo_allocation_holding(
	unsigned long offset,
	unsigned long size)
{
	long index;

	for (index = 0; index < demo_allocations.count; index++)
	{
		struct demo_allocation const *allocation = &demo_allocations.list[index];

		if (offset >= allocation->offset && size <= allocation->size &&
			offset - allocation->offset <= allocation->size - size)
		{
			return allocation;
		}
	}
	return NULL;
}

/* a P record's or a K record's ranges checked (each wholly in one
allocation; a K's each an allocation from its start), then written into
the game state if write */
static boolean demo_ranges_apply(
	unsigned char const *bytes,
	unsigned long length,
	boolean whole_allocations,
	boolean write)
{
	unsigned long position = 0;

	while (position < length)
	{
		struct demo_allocation const *allocation;
		unsigned long offset, size;

		if (length - position < 8)
			return FALSE;
		offset = demo_read_u32(bytes + position);
		size = demo_read_u32(bytes + position + 4);
		position += 8;
		if (size > length - position)
			return FALSE;
		allocation = demo_allocation_holding(offset, size);
		if (!allocation || (whole_allocations && allocation->offset != offset))
			return FALSE;
		if (write)
		{
			memcpy(demo_allocations.base + offset, bytes + position, size);
			/* (a K's data array past its count as the game leaves one: its free
			elements' identifiers none, which datum_new looks for; what else
			they hold is never read) */
			if (whole_allocations && size >= sizeof(struct data_array))
			{
				struct data_array *data = (struct data_array *)(demo_allocations.base + offset);

				if (data->signature == 'd@t@' && data->data == data + 1 && data->size >= (short)sizeof(short) &&
					data->count >= 0 && data->count <= data->maximum_count &&
					sizeof(*data) + (unsigned long)data->maximum_count * (unsigned long)data->size <= allocation->size)
				{
					short index;

					for (index = data->count; index < data->maximum_count; index++)
						((struct datum_header *)((unsigned char *)data->data + (long)data->size * index))->identifier = 0;
				}
			}
		}
		position += size;
	}
	return TRUE;
}

/* a K record taken: the game state as the snapshot has it, through the
saved games' loading (the caches outside the game state put right, the
state checked), the procedures' changes to it then written over */
/* (game_state_port_demo_load's writer) a record's ranges written */
struct demo_ranges
{
	unsigned char const *bytes;
	unsigned long length;
	boolean whole_allocations;
};

static void demo_ranges_write(
	void *context)
{
	struct demo_ranges const *ranges = context;

	demo_ranges_apply(ranges->bytes, ranges->length, ranges->whole_allocations, TRUE);
}

static boolean demo_playback_snapshot(
	unsigned char const *bytes,
	unsigned long length,
	boolean seek)
{
	struct demo_ranges ranges;
	unsigned long flags;

	if (length < 16 || !demo_ranges_apply(bytes + 16, length - 16, TRUE, FALSE))
		return FALSE;
	flags = demo_read_u32(bytes + 4);
	ranges.bytes = bytes + 16;
	ranges.length = length - 16;
	ranges.whole_allocations = TRUE;
	/* (the start's: the map just loaded here as it was there, the game
	state written over as it is, as nothing outside it differs; else as a
	saved game, the loading's procedures' changes then written over) */
	if (TEST_FLAG(flags, _demo_snapshot_start_bit) && demo_player.connection == _game_connection_local && !seek)
	{
		if (!game_state_port_demo_load(demo_ranges_write, &ranges, FALSE))
			return FALSE;
	}
	else
	{
		if (!game_state_port_demo_load(demo_ranges_write, &ranges, TRUE))
			return FALSE;
		demo_ranges_write(&ranges);
	}
	random_math_port_set_seeds(demo_read_u32(bytes + 8), demo_read_u32(bytes + 12));
	return TRUE;
}

static boolean demo_playback_patch(
	unsigned char const *bytes,
	unsigned long length)
{
	struct demo_ranges ranges;

	if (!demo_ranges_apply(bytes, length, FALSE, FALSE))
		return FALSE;
	/* (once the playback has parted from the recording, the bytes of the
	recording's game state no longer fit its own: an object's place with
	another's cluster references. It plays on as its own game, with the
	recorded input and whole game states) */
	if (demo_player.diverged && !config_boolean("debug.demo_verify"))
		return TRUE;
	ranges.bytes = bytes;
	ranges.length = length;
	ranges.whole_allocations = FALSE;
	/* (what was made outside the code here, an object's place and its
	cluster references: the objects' caches outside the game state
	forgotten, as a loaded game state's are) */
	if (length)
	{
		cluster_partitions_port_forget();
		object_bounds_cache_invalidate();
	}
	return game_state_port_demo_load(demo_ranges_write, &ranges, FALSE);
}

/* an O record taken: the machine's player's camera as the recording's tick
began, for this tick to read (the frames' next sets it again for the
picture), its place checked to be in the level */
static void demo_playback_camera(
	short local_player_index,
	unsigned char const *bytes)
{
	struct observer_result camera;
	struct structure_bsp *structure_bsp = global_structure_bsp_get();

	memcpy(&camera, bytes, sizeof(camera));
	if (local_player_index < 0 || local_player_index >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS ||
		local_player_get_player_index(local_player_index) == NONE || !structure_bsp ||
		!valid_real_point3d(&camera.position) || !valid_real_vector3d(&camera.velocity) ||
		!valid_real_vector3d(&camera.forward) || !valid_real_vector3d(&camera.up) || !valid_real(camera.field_of_view) ||
		(camera.location.cluster_index != NONE &&
			(camera.location.cluster_index < 0 || camera.location.cluster_index >= structure_bsp->clusters.count)) ||
		(camera.location.leaf_index != NONE &&
			(camera.location.leaf_index < 0 || camera.location.leaf_index >= structure_bsp->leaves.count)))
	{
		return;
	}
	*(struct observer_result *)observer_get_camera(local_player_index) = camera;
	demo_player.cameras[local_player_index] = camera;
	demo_player.cameras_valid[local_player_index] = TRUE;
}

/* the cameras as the recording last had them, put back (the playback's own
frames moved them since) */
static void demo_playback_cameras_again(
	void)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		if (demo_player.cameras_valid[local_player_index] && local_player_get_player_index(local_player_index) != NONE)
			*(struct observer_result *)observer_get_camera(local_player_index) = demo_player.cameras[local_player_index];
	}
}

/* the records before a tick's own (its T, then what came from outside it) */
static void demo_playback_tick_begin(
	long tick)
{
	unsigned char const *bytes;
	unsigned long length;
	char type;

	if (demo_player.ended)
	{
		demo_playback_stop("the map changed", FALSE);
		return;
	}
	/* (the start: the demo's map loaded, and nothing taken yet) */
	if (!demo_player.started)
	{
		if (global_scenario_index == NONE || csstrcasecmp(tag_get_name(global_scenario_index), demo_player.map_name))
			return;
		demo_player.started = TRUE;
		/* (the variant, for the record) */
		if (demo_playback_record(&bytes, &length, FALSE) == 'V')
			demo_playback_record(&bytes, &length, TRUE);
	}
	/* (a seek: the snapshot's tick begins here, its T before it taken
	already; then what its tick took from outside it after the snapshot, as
	any tick's: the whole of what the game keeps outside the game state (G),
	the input, the seeds, the cameras) */
	if (demo_player.seek_block != NONE)
	{
		struct demo_block_entry const *entry = &demo_player.blocks[demo_player.seek_block];

		demo_player.seek_block = NONE;
		if (!demo_playback_read_block(entry) ||
			demo_playback_record(&bytes, &length, TRUE) != 'K' || !demo_playback_snapshot(bytes, length, TRUE))
		{
			demo_playback_stop("a damaged snapshot (the seek's)", TRUE);
			return;
		}
		demo_player.next_block = (long)(entry - demo_player.blocks) + 1;
		demo_player.cursor = demo_player.block_size;
		demo_player.ticks = (long)entry->position;
		demo_player.last_tick = (long)entry->first_tick;
	}
	else
	{
		demo_shadow_restore();
		/* (what the netcode did between the ticks) */
		demo_playback_open_records(0);
		if (!demo_player.playing)
			return;
		type = demo_playback_record(&bytes, &length, TRUE);
		if (type == 'E' || type == 0)
		{
			demo_playback_stop(type == 'E' ? "its end" : "the file ends there", FALSE);
			return;
		}
		if (type != 'T' || length < 8)
		{
			demo_playback_stop("a damaged record", TRUE);
			return;
		}
		demo_player.rate = (long)demo_read_u32(bytes + 4);
		if (!sim_rate_supported(demo_player.rate))
		{
			demo_playback_stop("a damaged record (its rate)", TRUE);
			return;
		}
		demo_player.last_tick = (long)demo_read_u32(bytes);
	}
	for (;;)
	{
		type = demo_playback_record(&bytes, &length, FALSE);
		if (type == 'K')
		{
			demo_playback_record(&bytes, &length, TRUE);
			if (!demo_playback_snapshot(bytes, length, FALSE))
			{
				demo_playback_stop("a damaged snapshot", TRUE);
				return;
			}
		}
		else if (type == 'P')
		{
			demo_playback_record(&bytes, &length, TRUE);
			if (!demo_playback_patch(bytes, length))
			{
				demo_playback_stop("a damaged difference", TRUE);
				return;
			}
		}
		else if (type == 'C')
		{
			demo_playback_record(&bytes, &length, TRUE);
			if (!demo_ranges_apply(bytes, length, FALSE, FALSE))
			{
				demo_playback_stop("a damaged record (of the input)", TRUE);
				return;
			}
			demo_ranges_apply(bytes, length, FALSE, TRUE);
		}
		else if (type == 'S' && length >= 8)
		{
			demo_playback_record(&bytes, &length, TRUE);
			random_math_port_set_seeds(demo_read_u32(bytes), demo_read_u32(bytes + 4));
		}
		else if (type == 'O' && length == 1 + sizeof(struct observer_result))
		{
			demo_playback_record(&bytes, &length, TRUE);
			demo_playback_camera(bytes[0], bytes + 1);
		}
		else if (type == 'G')
		{
			demo_playback_record(&bytes, &length, TRUE);
			demo_engine_state_set(bytes, length);
		}
		else if (type == 'B' || type == 'V' || type == 'W')
		{
			demo_playback_record(&bytes, &length, TRUE);
			/* (the host's line to its bots, which the A's input plays) */
			if (type == 'W' && length >= 4)
			{
				platform_log("demos: tick %lu, the host's bots: %.*s", demo_read_u32(bytes), (int)MIN(length - 4, 200UL),
					(char const *)bytes + 4);
			}
		}
		else
			break;
	}
	if (game_time_get() != demo_player.last_tick && !demo_player.diverged)
	{
		platform_log("demos: the playback's tick is %ld where the recording's was %ld", game_time_get(),
			demo_player.last_tick);
	}
}

boolean demo_playback_tick_actions(
	struct player_action *actions)
{
	unsigned char const *bytes;
	unsigned long length;
	unsigned long position = 0;
	char type;

	if (!demo_player.playing || !demo_player.started)
		return FALSE;
	/* (a later sub-step's presses are its own: none are the tick's) */
	demo_player.pressed_count = 0;
	/* (a seek's snapshot's tick: its cameras and the game engine's state
	come after the snapshot) */
	for (;;)
	{
		type = demo_playback_record(&bytes, &length, FALSE);
		if (type == 'O' && length == 1 + sizeof(struct observer_result))
		{
			demo_playback_record(&bytes, &length, TRUE);
			demo_playback_camera(bytes[0], bytes + 1);
		}
		else if (type == 'G')
		{
			demo_playback_record(&bytes, &length, TRUE);
			demo_engine_state_set(bytes, length);
		}
		/* (the host's line to its bots, which the A's input plays) */
		else if (type == 'W' && length >= 4)
		{
			demo_playback_record(&bytes, &length, TRUE);
			platform_log("demos: tick %lu, the host's bots: %.*s", demo_read_u32(bytes), (int)MIN(length - 4, 200UL),
				(char const *)bytes + 4);
		}
		else
			break;
	}
	type = demo_playback_record(&bytes, &length, TRUE);
	if (type != 'A')
	{
		/* (a recording cut short in its last tick ends there) */
		demo_playback_stop(type ? "a damaged record (no input where a tick's was)" : "the file ends there", type != 0);
		return FALSE;
	}
	while (position + 1 + sizeof(struct player_action) <= length)
	{
		short index = bytes[position];
		struct player_action action;

		memcpy(&action, bytes + position + 1, sizeof(action));
		position += 1 + sizeof(action);
		if (index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			continue;
		/* (a controller's: as the netcode checks another machine's) */
		if (!valid_real(action.desired_facing.yaw) || !valid_real(action.desired_facing.pitch) ||
			!valid_real(action.throttle.i) || !valid_real(action.throttle.j) || !valid_real(action.primary_trigger) ||
			(action.desired_weapon_index != NONE &&
				(action.desired_weapon_index < 0 || action.desired_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)) ||
			(action.desired_grenade_index != NONE &&
				(action.desired_grenade_index < 0 || action.desired_grenade_index >= NUMBER_OF_UNIT_GRENADE_TYPES)) ||
			(action.desired_zoom_level != NONE && action.desired_zoom_level < 0))
		{
			continue;
		}
		actions[index] = action;
	}
	/* (the seeds the bots left) */
	if (demo_playback_record(&bytes, &length, FALSE) == 'S' && length >= 8)
	{
		demo_playback_record(&bytes, &length, TRUE);
		random_math_port_set_seeds(demo_read_u32(bytes), demo_read_u32(bytes + 4));
	}
	return TRUE;
}

/* the records of the tick's code that the playback's code left untaken
(a choice of the netcode's, D; what it put in, F; a unit's illumination, I;
a particle held for a later frame, L), up to the record that bounds them (a
sub-step's U, the tick's Z): the recording's code made them, and the
playback's ran otherwise (it has parted from the recording, or is about
to; its hash says where). Each sub-step's and each tick's records are
bounded so, so that what one left never stands where the next looks */
static void demo_playback_records_left(
	char const *where)
{
	unsigned char const *bytes;
	unsigned long length;
	long left = 0;
	char first = 0;
	char type;

	while ((type = demo_playback_record(&bytes, &length, FALSE)) == 'D' || type == 'F' || type == 'I' || type == 'L')
	{
		if (!left)
			first = type;
		demo_playback_record(&bytes, &length, TRUE);
		left++;
	}
	if (!left)
		return;
	demo_player.records_left += left;
	if (!demo_player.records_left_logged)
	{
		demo_player.records_left_logged = TRUE;
		platform_log("demos: the playback's tick %ld left %ld of the recording's records of its code untaken at %s "
			"(the first a %c): its code ran otherwise than the recording's", demo_player.last_tick, left, where,
			first);
	}
}

/* a record where a bound was looked for that is not it: the demo's end (an
E: a recorder stopped in the middle of a tick; or the file's), or else a
damaged demo */
static void demo_playback_bound_missing(
	char type,
	char const *what)
{
	if (type == 'E' || type == 0)
	{
		demo_playback_stop(type == 'E' ? "its end" : "the file ends there", FALSE);
		return;
	}
	platform_log("demos: a %c record where %s was looked for", type, what);
	demo_playback_stop("a damaged record (no sub-step or tick's end where the recording's was)", TRUE);
}

static void demo_playback_substep_end(
	short substep_index,
	short substep_count)
{
	unsigned char const *bytes;
	unsigned long length;
	unsigned long position = 6;
	struct demo_unit_controls controls;
	unsigned long const entry_size = 1 + 4 + sizeof(controls);
	char type;

	demo_playback_records_left("a sub-step's start");
	type = demo_playback_record(&bytes, &length, FALSE);
	if (type != 'U' || length < 6 || demo_read_u32(bytes) != (unsigned long)demo_player.last_tick ||
		bytes[4] != substep_index || bytes[5] != substep_count)
	{
		char what[64];

		snprintf(what, sizeof(what), "the sub-step %d of %d of tick %ld", substep_index, substep_count,
			demo_player.last_tick);
		demo_playback_bound_missing(type, what);
		return;
	}
	demo_playback_record(&bytes, &length, TRUE);
	demo_player.pressed_count = 0;
	while (position + entry_size <= length)
	{
		short index = bytes[position];
		long unit_index = (long)demo_read_u32(bytes + position + 1);
		struct player_datum *player;
		struct unit_datum *unit;

		memcpy(&controls, bytes + position + 5, sizeof(controls));
		position += entry_size;
		if (index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			continue;
		player = player_try_and_get(index);
		if (!player || player->unit_index != unit_index || !unit_try_and_get(unit_index) ||
			!valid_real(controls.primary_trigger) || !valid_real_vector3d(&controls.throttle) ||
			!valid_real_vector3d(&controls.desired_facing_vector) ||
			!valid_real_vector3d(&controls.desired_aiming_vector) ||
			!valid_real_vector3d(&controls.desired_looking_vector))
		{
			continue;
		}
		unit = unit_get(unit_index);
		unit->unit.control_flags = controls.control_flags;
		unit->unit.throttle = controls.throttle;
		unit->unit.primary_trigger = controls.primary_trigger;
		unit->unit.desired_facing_vector = controls.desired_facing_vector;
		unit->unit.desired_aiming_vector = controls.desired_aiming_vector;
		unit->unit.desired_looking_vector = controls.desired_looking_vector;
		if (controls.pressed_flags && demo_player.pressed_count < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		{
			demo_player.pressed_units[demo_player.pressed_count] = (unsigned long)unit_index;
			demo_player.pressed_flags[demo_player.pressed_count] = controls.pressed_flags;
			demo_player.pressed_count++;
		}
	}
}

unsigned long demo_playback_pressed_flags(
	long unit_index)
{
	long index;

	for (index = 0; index < demo_player.pressed_count; index++)
	{
		if (demo_player.pressed_units[index] == (unsigned long)unit_index)
			return demo_player.pressed_flags[index];
	}
	return 0;
}

static void demo_playback_foreign(
	void)
{
	unsigned char const *bytes;
	unsigned long length;

	if (demo_playback_record(&bytes, &length, FALSE) != 'F')
		return;
	demo_playback_record(&bytes, &length, TRUE);
	if (!demo_playback_patch(bytes, length))
		demo_playback_stop("a damaged record (of what another machine put in)", TRUE);
}

/* the records of what came before a sub-step of an open tick (or, of
sub-step 0, before a tick), in their order: the differences (M), and the
netcode's events between them, done again by their code (X: a hit the
host dealt, a client's reported; Y: a kill it made for a player, the
network test's), with the frames' input and the seeds the sub-step takes
(C, S) */
static void demo_playback_open_records(
	short substep_index)
{
	unsigned char const *bytes;
	unsigned long length;
	char type;

	demo_playback_cameras_again();
	while (demo_player.playing)
	{
		type = demo_playback_record(&bytes, &length, FALSE);
		if (type == 'O' && length == 1 + sizeof(struct observer_result))
		{
			demo_playback_record(&bytes, &length, TRUE);
			demo_playback_camera(bytes[0], bytes + 1);
		}
		else if (type == 'M' && length >= 1 && bytes[0] == substep_index)
		{
			demo_playback_record(&bytes, &length, TRUE);
			if (!demo_playback_patch(bytes + 1, length - 1))
				demo_playback_stop("a damaged record (of what came while a tick was open)", TRUE);
		}
		else if ((type == 'X' || type == 'Y') && length >= 8)
		{
			unsigned long seeds[2];

			demo_playback_record(&bytes, &length, TRUE);
			random_math_port_seeds(&seeds[0], &seeds[1]);
			random_math_port_set_seeds(demo_read_u32(bytes), seeds[1]);
			random_math_port_set_frame_seed(demo_read_u32(bytes + 4));
			if (type == 'X')
				network_damage_deal_recorded(bytes + 8, length - 8);
			/* (a kill the host made for a player: the network test's) */
			else if (length == 16 && object_try_and_get((long)demo_read_u32(bytes + 8)) &&
				player_try_and_get((long)demo_read_u32(bytes + 12)))
			{
				damage_kill_object_for_player((long)demo_read_u32(bytes + 8), (long)demo_read_u32(bytes + 12));
			}
		}
		else if (type == 'S' && length >= 8 && substep_index > 0)
		{
			demo_playback_record(&bytes, &length, TRUE);
			random_math_port_set_seeds(demo_read_u32(bytes), demo_read_u32(bytes + 4));
		}
		else if (type == 'G' && substep_index > 0)
		{
			demo_playback_record(&bytes, &length, TRUE);
			demo_engine_state_set(bytes, length);
		}
		else if (type == 'C' && substep_index > 0)
		{
			demo_playback_record(&bytes, &length, TRUE);
			if (!demo_ranges_apply(bytes, length, FALSE, FALSE))
			{
				demo_playback_stop("a damaged record (of the input)", TRUE);
				return;
			}
			demo_ranges_apply(bytes, length, FALSE, TRUE);
		}
		else
			break;
	}
}

static void demo_playback_resume(
	short substep_index)
{
	/* (what the sub-step before left, ahead of what the frames put in
	since) */
	demo_playback_records_left("a sub-step's start");
	demo_playback_open_records(substep_index);
}

static boolean demo_playback_decide(
	boolean decision)
{
	unsigned char const *bytes;
	unsigned long length;

	if (demo_playback_record(&bytes, &length, FALSE) != 'D' || length < 1)
		return decision;
	demo_playback_record(&bytes, &length, TRUE);
	return bytes[0] != 0;
}

static void demo_playback_tick_end(
	long tick)
{
	unsigned char const *bytes;
	unsigned long length;

	static unsigned long allocation_hashes[MAXIMUM_DEMO_ALLOCATIONS];
	unsigned long hash;
	long index;

	demo_player.ticks++;
	/* (as the tick left it, for its next's start) */
	if (!demo_recorder.shadow)
		demo_recorder.shadow = demo_files_alloc((unsigned long)demo_allocations.size);
	demo_shadow_take();
	/* (the tick's end: what its code left of the recording's records, then
	the bound) */
	demo_playback_records_left("the tick's end");
	{
		char type = demo_playback_record(&bytes, &length, FALSE);

		if (type != 'Z' || length < 4 || demo_read_u32(bytes) != (unsigned long)demo_player.last_tick)
		{
			char what[48];

			snprintf(what, sizeof(what), "the end of tick %ld", demo_player.last_tick);
			demo_playback_bound_missing(type, what);
			return;
		}
		demo_playback_record(&bytes, &length, TRUE);
	}
	if (demo_playback_record(&bytes, &length, FALSE) != 'H' || length < 8)
		return;
	demo_playback_record(&bytes, &length, TRUE);
	hash = demo_state_hash(allocation_hashes);
	if (demo_read_u32(bytes + 4) == hash)
	{
		demo_player.hashes_matched++;
		return;
	}
	if (!demo_player.diverged)
	{
		char names[512];
		unsigned long written = 0;

		names[0] = 0;
		for (index = 0; index < demo_allocations.count && 8 + (unsigned long)index * 4 + 4 <= length; index++)
		{
			if (demo_read_u32(bytes + 8 + index * 4) != allocation_hashes[index] && written + 40 < sizeof(names))
				written += snprintf(names + written, sizeof(names) - written, "%s%s", written ? ", " : "",
					demo_allocations.list[index].name);
		}
		demo_player.diverged = TRUE;
		platform_log("demos: the playback of %s parted from the recording by tick %ld (its hash %08lx, the "
			"recording's %08lx: %s); it plays on as its own game", demo_player.path, tick, hash,
			demo_read_u32(bytes + 4), names[0] ? names : "?");
	}
}

/* a demo opened and checked, its blocks indexed: whether it can be played
(its reason logged otherwise) */
static boolean demo_playback_open(
	char const *path)
{
	unsigned char *header = demo_player.header;
	long number;
	char flavor[32], repository[128], version[128], build[DEMO_BUILD_SIZE];
	long capacity = 0;
	unsigned long offset = DEMO_HEADER_SIZE;

	snprintf(demo_player.path, sizeof(demo_player.path), "%s", path);
	demo_player.file = demo_reader_open(path, &demo_player.file_size);
	if (!demo_player.file)
	{
		platform_log("demos: cannot open %s", path);
		return FALSE;
	}
	if (!demo_reader_read(demo_player.file, 0, header, DEMO_HEADER_SIZE) ||
		memcmp(header, DEMO_MAGIC, sizeof(DEMO_MAGIC)))
	{
		platform_log("demos: %s is not a demo", path);
		return FALSE;
	}
	if (header[8] != DEMO_FORMAT_VERSION)
	{
		platform_log("demos: %s is a demo of format %d, which this build does not play (its format is %d)%s", path,
			header[8], DEMO_FORMAT_VERSION,
			header[8] < DEMO_FORMAT_VERSION ? ": it was recorded by an earlier build, whose demos played back otherwise" : "");
		return FALSE;
	}
	main_build_identity(&number, flavor, sizeof(flavor), repository, sizeof(repository), version, sizeof(version));
	snprintf(build, sizeof(build), "%ld/%s/%s%s", number, flavor, DEMO_PLATFORM, DEMO_PLATFORM_VARIANT);
	header[16 + DEMO_BUILD_SIZE - 1] = 0;
	if (strcmp((char const *)header + 16, build) || demo_read_u32(header + 80) != HALO_PORT_NETWORK_VERSION ||
		demo_read_u32(header + 84) != demo_allocation_checksum() ||
		demo_read_u32(header + 88) != (unsigned long)demo_allocations.size)
	{
		platform_log("demos: %s was recorded by another build of OpenH1 (%s, network version %lu), not this one "
			"(%s, network version %d): its game would not play the same here", path, (char const *)header + 16,
			demo_read_u32(header + 80), build, HALO_PORT_NETWORK_VERSION);
		return FALSE;
	}
	demo_player.connection = (long)demo_read_u32(header + 92);
	demo_player.rate = (long)demo_read_u32(header + 96);
	demo_player.difficulty = (long)demo_read_u32(header + 100);
	demo_player.rules = demo_read_u32(header + 120);
	header[128 + DEMO_MAP_NAME_SIZE - 1] = 0;
	snprintf(demo_player.map_name, sizeof(demo_player.map_name), "%s", (char const *)header + 128);
	if (demo_player.connection != _game_connection_local && demo_player.connection != _game_connection_network_server)
	{
		platform_log("demos: %s is of a game another machine hosted, which this build does not play back yet", path);
		return FALSE;
	}
	if (!demo_player.map_name[0] || demo_player.difficulty < 0 ||
		demo_player.difficulty >= NUMBER_OF_GAME_DIFFICULTY_LEVELS)
	{
		platform_log("demos: %s has a damaged header", path);
		return FALSE;
	}
	/* (the blocks' index: a block cut short, a crash's, ends it) */
	for (;;)
	{
		unsigned char block[DEMO_BLOCK_HEADER_SIZE];
		struct demo_block_entry entry;

		if (!demo_reader_read(demo_player.file, offset, block, sizeof(block)) ||
			demo_read_u32(block) != DEMO_BLOCK_MAGIC)
		{
			break;
		}
		entry.offset = offset;
		entry.kind = demo_read_u32(block + 4);
		entry.first_tick = demo_read_u32(block + 8);
		entry.size = demo_read_u32(block + 12);
		entry.packed_size = demo_read_u32(block + 16);
		if (entry.size > DEMO_BLOCK_MAXIMUM_SIZE || entry.packed_size > DEMO_BLOCK_MAXIMUM_SIZE ||
			(entry.kind != _demo_block_records && entry.kind != _demo_block_snapshot &&
				entry.kind != _demo_block_seek_snapshot))
		{
			break;
		}
		if (entry.packed_size > demo_player.file_size - offset - DEMO_BLOCK_HEADER_SIZE)
			break;
		if (demo_player.block_count >= capacity)
		{
			struct demo_block_entry *blocks;

			capacity = capacity ? capacity * 2 : 256;
			blocks = demo_files_realloc(demo_player.blocks, (size_t)capacity * sizeof(*blocks));
			if (!blocks)
				break;
			demo_player.blocks = blocks;
		}
		demo_player.blocks[demo_player.block_count++] = entry;
		offset += DEMO_BLOCK_HEADER_SIZE + entry.packed_size;
	}
	if (demo_player.block_count < 2)
	{
		platform_log("demos: %s holds no game", path);
		return FALSE;
	}
	/* (each block's place in the demo's ticks, for seeking: its records'
	ticks counted) */
	{
		long index;
		unsigned long position = 0;

		for (index = 0; index < demo_player.block_count; index++)
		{
			struct demo_block_entry *entry = &demo_player.blocks[index];

			entry->position = position;
			if (entry->kind == _demo_block_records && demo_playback_read_block(entry))
			{
				unsigned long cursor = 0;

				while (cursor + 5 <= demo_player.block_size)
				{
					unsigned long size = demo_read_u32(demo_player.block + cursor + 1);

					if (size > demo_player.block_size - cursor - 5)
						break;
					if (demo_player.block[cursor] == 'T')
						position++;
					cursor += 5 + size;
				}
			}
			/* (a snapshot comes after its tick's T, in the block before it:
			the tick it begins is the one before the count) */
			if (entry->kind != _demo_block_records && entry->position)
				entry->position--;
		}
		demo_player.length_ticks = position;
		demo_player.block_size = 0;
		demo_player.cursor = 0;
	}
	/* (the game's variant, the first records', for the map's game engine as
	it loads: demo_note_new_map) */
	{
		unsigned char const *bytes;
		unsigned long length;

		if (demo_playback_record(&bytes, &length, FALSE) == 'V' && length >= 8 &&
			demo_read_u32(bytes) == sizeof(demo_player.variant) &&
			length == 8 + sizeof(demo_player.variant) + sizeof(demo_player.variant_options) &&
			demo_read_u32(bytes + 4 + sizeof(demo_player.variant)) == sizeof(demo_player.variant_options))
		{
			memcpy(&demo_player.variant, bytes + 4, sizeof(demo_player.variant));
			memcpy(&demo_player.variant_options, bytes + 8 + sizeof(demo_player.variant),
				sizeof(demo_player.variant_options));
			demo_player.variant_read = TRUE;
		}
		if (demo_player.connection != _game_connection_local && !demo_player.variant_read)
		{
			platform_log("demos: %s has no game variant", path);
			return FALSE;
		}
	}
	platform_log("demos: playing %s (%s, %ld blocks)", path, demo_player.map_name, demo_player.block_count);
	return TRUE;
}

/* a demo asked for played: its map loaded at its difficulty, the playback
taking it from its first tick */
static void demo_playback_begin(
	char const *path)
{
	demo_settings_read();
	demo_record_stop("a demo plays");
	memset(&demo_player, 0, sizeof(demo_player));
	demo_player.quit_at_end = config_boolean("debug.demo_quit") != 0;
	if (!demo_playback_open(path))
	{
		demo_reader_close(demo_player.file);
		demo_files_free(demo_player.blocks);
		memset(&demo_player, 0, sizeof(demo_player));
		if (config_boolean("debug.demo_quit"))
			demo_playback_end(TRUE);
		return;
	}
	demo_player.playing = TRUE;
	demo_player.seek_block = NONE;
	demo_player.speed = 1.f;
	demo_recorder.shadow_valid = FALSE;
	/* (as map_name loads one, at the demo's difficulty, nothing of the
	player's saved game read or written) */
	main_set_map_name(demo_player.map_name);
	main_disallow_persistent_storage();
	main_set_difficulty((short)demo_player.difficulty);
}

/* a seek of seconds from where the playback is: to the snapshot nearest
before that point (the demo's start's, its reloads', one each
demo.snapshot_seconds), at the next tick's start */
static void demo_playback_seek(
	long seconds)
{
	long target = demo_player.ticks + seconds * TICKS_PER_SECOND;
	long found = NONE;
	long index;

	if (target < 0)
		target = 0;
	for (index = 0; index < demo_player.block_count; index++)
	{
		struct demo_block_entry const *entry = &demo_player.blocks[index];

		if (entry->kind != _demo_block_records && (long)entry->position <= target)
			found = index;
	}
	/* (forward, only to one past where it is) */
	if (found == NONE || (seconds > 0 && (long)demo_player.blocks[found].position <= demo_player.ticks))
		return;
	demo_player.seek_block = found;
}

static void demo_playback_show(
	void)
{
	long now = demo_player.ticks / TICKS_PER_SECOND;
	long length = (long)demo_player.length_ticks / TICKS_PER_SECOND;

	console_printf(FALSE, "demo %ld:%02ld / %ld:%02ld%s, speed %gx (Q, E: 10 s back, on; X, C: faster, slower; "
		"space: pause; Backspace: the camera)", now / 60, now % 60, length / 60, length % 60,
		game_time_get_paused() ? ", paused" : "", (double)demo_player.speed);
}

/* the playback's controls, on the keyboard's own (the machine's players
play no part in it), clear of the flying camera's keys (W, A, S, D, R, F,
T, G, Tab and the right mouse button: director.c), which Backspace's
camera switch gives the playback as it gives a game: the flashlight's
seeks 10 seconds back and the action's 10 on, switching the grenade plays
faster and crouching slower, jumping pauses */
static void demo_playback_controls(
	void)
{
	unsigned long held = halo_keyboard_actions(0);
	unsigned long pressed = held & ~demo_player.controls_held;

	demo_player.controls_held = held;
	if (!pressed || !demo_player.started)
		return;
	if (TEST_FLAG(pressed, HALO_KEYBOARD_FLASHLIGHT))
		demo_playback_seek(-10);
	if (TEST_FLAG(pressed, HALO_KEYBOARD_ACTION))
		demo_playback_seek(10);
	if (TEST_FLAG(pressed, HALO_KEYBOARD_SWITCH_GRENADE) && demo_player.speed < 8.f)
		demo_player.speed *= 2.f;
	if (TEST_FLAG(pressed, HALO_KEYBOARD_CROUCH) && demo_player.speed > 0.25f)
		demo_player.speed *= 0.5f;
	if (TEST_FLAG(pressed, HALO_KEYBOARD_JUMP))
		game_time_set_paused(!game_time_get_paused());
	game_time_set_speed(demo_player.speed);
	demo_playback_show();
}

void demo_frame(
	boolean main_menu_loaded,
	real seconds)
{
	static boolean asked;

	(void)seconds;
	if (!asked)
	{
		char path[512];

		asked = TRUE;
		if (!config_command_line_value("--demo", path, sizeof(path)))
			snprintf(path, sizeof(path), "%s", config_string("demo.play"));
		if (path[0])
			demo_playback_begin(path);
	}
	if (demo_player.playing && demo_player.started)
	{
		demo_playback_controls();
		/* (debug.demo_seek "<at>,<by>": a seek of by seconds once the playback
		reaches at seconds, for tests) */
		if (!demo_player.seek_tested && config_string("debug.demo_seek")[0])
		{
			long at = 0, by = 0;

			if (sscanf(config_string("debug.demo_seek"), "%ld,%ld", &at, &by) == 2 &&
				demo_player.ticks >= at * TICKS_PER_SECOND)
			{
				demo_player.seek_tested = TRUE;
				platform_log("demos: seeking %ld seconds from %ld (debug.demo_seek)", by, demo_player.ticks / TICKS_PER_SECOND);
				demo_playback_seek(by);
			}
		}
	}
	if (demo_clip_requests)
	{
		__sync_lock_test_and_set(&demo_clip_requests, 0);
		demo_bookmark();
	}
	/* (the main menu reached: a recording's game over, a playback's too) */
	if (main_menu_loaded)
	{
		if (demo_recorder.recording)
			demo_record_stop("the main menu");
		if (demo_player.playing && demo_player.started)
			demo_playback_stop("the main menu", FALSE);
	}
}

/* ---------- the clock's ticks */

void demo_tick_begin(
	long tick)
{
	if (demo_player.playing)
	{
		demo_playback_tick_begin(tick);
		demo_recorder.in_tick = TRUE;
	}
	else
	{
		unsigned long long start = platform_shared_clock_microseconds();

		demo_record_tick_begin(tick);
		demo_recorder.work_microseconds += platform_shared_clock_microseconds() - start;
	}
}

void demo_tick_end(
	long tick)
{
	demo_recorder.in_tick = FALSE;
	if (demo_player.playing && demo_player.started)
		demo_playback_tick_end(tick);
	else if (!demo_player.playing)
	{
		unsigned long long start = platform_shared_clock_microseconds();

		demo_record_tick_end(tick);
		demo_recorder.work_microseconds += platform_shared_clock_microseconds() - start;
	}
}

void demo_substep_end(
	short substep_index,
	short substep_count)
{
	if (demo_player.playing && demo_player.started)
		demo_playback_substep_end(substep_index, substep_count);
	else if (demo_recorder.recording)
		demo_record_substep_end(substep_index, substep_count);
}
