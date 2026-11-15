/*
CACHE_FILES_WINDOWS.C

symbols in this file:
001ABD40 0050:
	_cache_files_dispose (0000)
001ABD90 01a0:
	_cached_map_issue_async_request (0000)
001ABF30 0030:
	_cached_map_block_on_async_request (0000)
001ABF60 0040:
	_cache_request_get (0000)
001ABFA0 0060:
	_cache_request_next_free_index (0000)
001AC000 0080:
	_cache_requests_flush (0000)
001AC080 0010:
	_cache_files_precache_set_priority (0000)
001AC090 0010:
	_cache_files_precache_in_progress (0000)
001AC0A0 0030:
	_cache_files_precache_is_copying_map (0000)
001AC0D0 0030:
	_cache_files_precache_map_queue_end (0000)
001AC100 0040:
	_cached_map_file_get (0000)
001AC140 0040:
	_cached_map_file_invalidate (0000)
001AC180 0040:
	_cached_map_file_get_handle (0000)
001AC1C0 0050:
	_cached_map_file_get_size (0000)
001AC210 0020:
	_cached_map_file_get_path (0000)
001AC230 0010:
	_cache_file_windows_thread_wake (0000)
001AC240 0090:
	_cache_file_read_io_completion_routine@12 (0000)
001AC2D0 0070:
	_cache_file_blocking_io_completion_routine@12 (0000)
001AC340 0020:
	_cache_file_get_map_path (0000)
001AC360 0010:
	_IDirect3DVertexBuffer8_IsBusy@4 (0000)
001AC370 0010:
	_IDirect3DVertexBuffer8_BlockUntilNotBusy@4 (0000)
001AC380 0010:
	_IDirect3DVertexBuffer8_Register@8 (0000)
001AC390 0010:
	_IDirect3DIndexBuffer8_IsBusy@4 (0000)
001AC3A0 0010:
	_IDirect3DIndexBuffer8_BlockUntilNotBusy@4 (0000)
001AC3B0 0020:
	_cache_file_close (0000)
001AC3D0 0150:
	_cache_file_read (0000)
001AC520 0050:
	_cache_file_promote_read (0000)
001AC570 0090:
	_cache_file_read_header_from_dvd (0000)
001AC600 0020:
	_cached_map_issue_async_read (0000)
001AC620 0020:
	_cached_map_issue_async_write (0000)
001AC640 0060:
	_cache_file_block_until_not_busy (0000)
001AC6A0 0060:
	_tags_header_register_vertex_and_index_buffers (0000)
001AC700 00b0:
	_tags_header_deregister_vertex_and_index_buffers (0000)
001AC7B0 0070:
	_structure_bsp_header_register_vertex_buffers (0000)
001AC820 0070:
	_structure_bsp_header_deregister_vertex_buffers (0000)
001AC890 0060:
	_cached_map_files_delete (0000)
001AC8F0 00f0:
	_cache_files_verify_language (0000)
001AC9E0 00b0:
	_cache_files_precache_map_status (0000)
001ACA90 0070:
	_cached_map_file_set_modification_date (0000)
001ACB00 0190:
	_cached_map_file_read_header (0000)
001ACC90 0060:
	_cached_map_files_find_map (0000)
001ACCF0 0190:
	_cached_map_files_find_free_map (0000)
001ACE80 0130:
	_cache_file_windows_thread_proc (0000)
001ACFB0 0120:
	_cache_file_open (0000)
001AD0D0 02f0:
	_cache_files_open_cache_files (0000)
001AD3C0 0030:
	_cache_files_precache_map_loaded (0000)
001AD3F0 0130:
	_cache_files_precache_map_begin (0000)
001AD520 0060:
	_cache_files_precache_map_end (0000)
001AD580 0080:
	_cache_file_windows_thread_create (0000)
001AD600 0060:
	_cache_files_initialize (0000)
002A71F8 002d:
	??_C@_0CN@LGGFIMDL@cache_file_globals?4open_map_file@ (0000)
002A7228 002b:
	??_C@_0CL@CIGLGOJJ@c?3?2halo?2SOURCE?2cache?2cache_files@ (0000)
002A7254 0020:
	??_C@_0CA@OGKEMPOH@Read?1WriteFileEx?$CI?$CJ?5returned?5?$CD?$CFd?$AA@ (0000)
002A7274 0013:
	??_C@_0BD@PLJFCFLH@completion_routine?$AA@ (0000)
002A7288 0010:
	??_C@_0BA@LMKACIBF@completion_flag?$AA@ (0000)
002A7298 000b:
	??_C@_0L@GFMJGGB@overlapped?$AA@ (0000)
002A72A4 001b:
	??_C@_0BL@LMFIEMFC@file?$CB?$DNINVALID_HANDLE_VALUE?$AA@ (0000)
002A72C0 0017:
	??_C@_0BH@MFHBKAIA@async_request_function?$AA@ (0000)
002A72D8 0046:
	??_C@_0EG@KCENDOIE@request_index?$DO?$DN0?5?$CG?$CG?5request_inde@ (0000)
002A7320 002d:
	??_C@_0CN@BGBLHKIB@cache_file_globals?4open_map_file@ (0000)
002A7350 0024:
	??_C@_0CE@EPABAAJM@cache_file_globals?4copy_in_progr@ (0000)
002A7374 003f:
	??_C@_0DP@INCJNNMP@map_file_index?$DO?$DN0?5?$CG?$CG?5map_file_in@ (0000)
002A73B4 0011:
	??_C@_0BB@JJFBACKB@z?3?2cache?$CF03d?4map?$AA@ (0000)
002A73C8 0024:
	??_C@_0CE@EPGEPEPL@finished_request?9?$DOoverlapped?4hEv@ (0000)
002A73EC 002a:
	??_C@_0CK@GKGPAPGO@bytes_transferred?$DN?$DNfinished_requ@ (0000)
002A7418 001a:
	??_C@_0BK@GBNCJMGM@error_code?$DN?$DNERROR_SUCCESS?$AA@ (0000)
002A7434 0013:
	??_C@_0BD@BDBACLGP@overlapped?9?$DOhEvent?$AA@ (0000)
002A7448 0009:
	??_C@_08DCOLLFMA@?$CFs?$CFs?4map?$AA@ (0000)
002A7454 000a:
	??_C@_09GPIOEEJE@offset?$DO?$DN0?$AA@ (0000)
002A7460 001a:
	??_C@_0BK@OLOIOEMB@completion_flag_reference?$AA@ (0000)
002A747C 002c:
	??_C@_0CM@KBEGNHBI@?$CBIDirect3DIndexBuffer8_IsBusy?$CIin@ (0000)
002A74A8 002e:
	??_C@_0CO@CGLHMLAM@?$CBIDirect3DVertexBuffer8_IsBusy?$CIv@ (0000)
002A74D8 0015:
	??_C@_0BF@COEFPJKL@z?3?2last_language?4dat?$AA@ (0000)
002A74F0 002b:
	??_C@_0CL@NILOOFC@couldn?8t?5read?5header?5from?5cache?5@ (0000)
002A751C 003c:
	??_C@_0DM@LINBCMFH@cache_file_globals?4open_map_file@ (0000)
002A7558 0017:
	??_C@_0BH@BPIEKPLM@?$CBbest_request?9?$DOrunning?$AA@ (0000)
002A7570 0015:
	??_C@_0BF@DANPCANK@map_file_index?$CB?$DNNONE?$AA@ (0000)
002A7588 000e:
	??_C@_0O@OLMPCHGI@scenario_name?$AA@ (0000)
002A7598 002d:
	??_C@_0CN@LAKIIMED@couldn?8t?5open?5or?5create?5new?5cach@ (0000)
002A75C8 0026:
	??_C@_0CG@BLEGAMFF@setup?5for?5new?5cache?5file?5failed?5@ (0000)
002A75F0 0014:
	??_C@_0BE@KLDNFNMA@full?5path?5name?5?8?$CFs?8?$AA@ (0000)
002A7604 0022:
	??_C@_0CC@PCGPBKMC@couldn?8t?5find?5map?5?8?$CFs?8?5on?5the?5DV@ (0000)
002A7628 0020:
	??_C@_0CA@GMOAPCML@starting?5precaching?5of?5map?5?8?$CFs?8?$AA@ (0000)
002A7648 001a:
	??_C@_0BK@MIGLNFOL@cache_file_globals?4thread?$AA@ (0000)
002A7664 001f:
	??_C@_0BP@DJCANAAM@cache_file_globals?4sleep_event?$AA@ (0000)
002A7684 001c:
	??_C@_0BM@POLGJDBF@cache_file_globals?4requests?$AA@ (0000)
004CDFF8 307c:
	_cache_file_globals (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "cache/cache_files_decompress_windows.h"
#include "cache/texture_cache.h"
#include "interface/ui_widget.h"
#include "tag_files/files.h"
#include "tag_files/tag_files.h"
#include "scenario/scenario_definitions.h"
#include "rasterizer/rasterizer.h"
#include "custom_edition_cache.h" /* port: port/linux/game/custom_edition_cache.c */
#include "mcc_cache.h" /* port: port/linux/game/mcc_cache.c */
#include "mcc_share.h" /* port: port/linux/game/mcc_share.c */

#include <xtl.h>

/* ---------- constants */

enum
{
	MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS = 512,
	/* port: 32, any map in any of them, each its map's size, within a
	budget of the disk's space (halo_port_capacity.h) */
	NUMBER_OF_CACHED_MAP_FILES = HALO_PORT_CACHED_MAP_FILES, /* (6) */
	CACHE_FILE_SECTOR_SIZE = 512,
	MAXIMUM_CACHE_FILES = 64, /* port: (20) */
	CACHE_FILE_THREAD_STACK_SIZE = 0x4000,

	SOLO_CACHE_FILE_MAXIMUM_SIZE = 0x11600000,
	MAIN_MENU_CACHE_FILE_MAXIMUM_SIZE = 0x02300000,
	MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE = 0x02F00000,
};

/* ---------- macros */

#define CACHE_FILE_BUILD_STRING "01.01.14.2342"

/* ---------- structures */

struct cache_file_tag_instance;

struct cache_file_tag_header
{
	struct cache_file_tag_instance *tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	D3DVertexBuffer *vertex_buffers;
	long index_buffer_count;
	D3DIndexBuffer *index_buffers;
	unsigned long signature;
};

struct cache_file_structure_bsp_header
{
	void *base_address;
	long vertex_buffer_count;
	D3DVertexBuffer *vertex_buffers;
	long lightmap_vertex_buffer_count;
	D3DVertexBuffer *lightmap_vertex_buffers;
	unsigned long signature;
};

struct cache_file_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
	byte reserved18[8];
	char name[0x20];
	char build[0x20];
	short scenario_type;
	short pad62;
	unsigned long checksum;
	byte reserved68[0x794];
	unsigned long footer_signature;
};

struct cached_map_file
{
	HANDLE file;
	FILETIME last_modification_date;
	struct cache_file_header header;
};

struct cache_file_request
{
	OVERLAPPED overlapped;
	long size;
	void *buffer;
	boolean blocking;
	boolean pending;
	boolean running;
	byte pad1F;
};

typedef BOOL (WINAPI *cached_map_async_request_proc)(
	HANDLE file,
	void *buffer,
	unsigned long size,
	OVERLAPPED *overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine);

struct cache_file_runtime_globals
{
	struct cached_map_file cached_map_files[NUMBER_OF_CACHED_MAP_FILES];
	boolean copy_in_progress;
	byte reserved3049;
	short copying_to_map_file_index;
	char copying_to_map_file_name[32];
	short open_map_file_index;
	short blocking_request_index;
	HANDLE sleep_event;
	HANDLE thread;
	struct cache_file_request *requests;
};

typedef char verify_cache_file_header_size[
	sizeof(struct cache_file_header) == 0x800 ? 1 : -1];
typedef char verify_cached_map_file_size[
	sizeof(struct cached_map_file) == 0x80C ? 1 : -1];
typedef char verify_cached_map_file_name_offset[
	offsetof(
		struct cached_map_file,
		header.name) == 0x2C ? 1 : -1];
typedef char verify_cache_file_request_size[
	sizeof(struct cache_file_request) == 0x20 ? 1 : -1];
typedef char verify_cache_file_request_blocking_offset[
	offsetof(
		struct cache_file_request,
		blocking) == 0x1C ? 1 : -1];
typedef char verify_cache_file_request_pending_offset[
	offsetof(
		struct cache_file_request,
		pending) == 0x1D ? 1 : -1];
/* port: (the original's at its six cache files: 0x3048 and on) */
typedef char verify_cache_file_copy_in_progress_offset[
	offsetof(
		struct cache_file_runtime_globals,
		copy_in_progress) == NUMBER_OF_CACHED_MAP_FILES * sizeof(struct cached_map_file) + 0x0 ? 1 : -1];
typedef char verify_cache_file_copying_map_index_offset[
	offsetof(
		struct cache_file_runtime_globals,
		copying_to_map_file_index) == NUMBER_OF_CACHED_MAP_FILES * sizeof(struct cached_map_file) + 0x2 ? 1 : -1];
typedef char verify_cache_file_copying_map_name_offset[
	offsetof(
		struct cache_file_runtime_globals,
		copying_to_map_file_name) == NUMBER_OF_CACHED_MAP_FILES * sizeof(struct cached_map_file) + 0x4 ? 1 : -1];
typedef char verify_cache_file_open_map_index_offset[
	offsetof(
		struct cache_file_runtime_globals,
		open_map_file_index) == NUMBER_OF_CACHED_MAP_FILES * sizeof(struct cached_map_file) + 0x24 ? 1 : -1];
typedef char verify_cache_file_requests_offset[
	offsetof(
		struct cache_file_runtime_globals,
		requests) == NUMBER_OF_CACHED_MAP_FILES * sizeof(struct cached_map_file) + 0x30 ? 1 : -1];

/* ---------- prototypes */

static void cache_file_get_map_path(
	const char *map_name,
	char *path);
static boolean cache_file_read_header_from_dvd(
	const char *map_name,
	struct cache_file_header *header);
static boolean cache_file_read_header_from_dvd_checked( /* port */
	const char *map_name,
	struct cache_file_header *header,
	boolean fatal);
static short cached_map_files_find_free_map(
	long file_length,
	short scenario_type);
static long cached_map_file_get_size(
	short map_file_index);
static HANDLE cached_map_file_get_handle(
	short map_file_index);

static void CALLBACK cache_file_blocking_io_completion_routine(
	unsigned long error_code,
	unsigned long bytes_transferred,
	OVERLAPPED *overlapped);
static void cached_map_issue_async_request(
	cached_map_async_request_proc async_request_function,
	HANDLE file,
	OVERLAPPED *overlapped,
	void *buffer,
	long size,
	long offset,
	volatile boolean *completion_flag,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine);
static void cached_map_issue_async_read(
	HANDLE file,
	OVERLAPPED *overlapped,
	void *buffer,
	long size,
	long offset,
	volatile boolean *completion_flag,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine);
static void cached_map_issue_async_write(
	HANDLE file,
	OVERLAPPED *overlapped,
	void *buffer,
	long size,
	long offset,
	volatile boolean *completion_flag,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine);
static boolean cached_map_block_on_async_request(
	volatile boolean const *completion_flag);
static void cached_map_file_get_path(
	short map_file_index,
	char *path);
static void cached_map_file_set_modification_date(
	short map_file_index);
static void cached_map_file_read_header(
	short map_file_index);
static void cached_map_files_delete(
	short map_file_index);
static void cache_files_verify_language(
	void);
static void cache_files_open_cache_files(
	void);
static void CALLBACK cache_file_read_io_completion_routine(
	unsigned long error_code,
	unsigned long bytes_transferred,
	OVERLAPPED *overlapped);
static void cache_file_windows_thread_proc(
	void);
static void cache_file_windows_thread_create(
	void);
static struct cache_file_request *cache_request_get(
	short request_index);
static short cache_request_next_free_index(
	void);
static void cache_file_windows_thread_wake(
	void);
static void cache_requests_flush(
	void);
static struct cached_map_file *cached_map_file_get(
	short map_file_index);
static void cached_map_file_invalidate(
	short map_file_index);
static short cached_map_files_find_map(
	const char *map_name);
/* port: the cache files' budget (port/linux/src/map_cache.c) */
static long cached_map_type_maximum_size(
	short scenario_type);
static unsigned long long cached_map_files_bytes(
	short except_map_file_index);
static boolean cached_map_file_resize(
	short map_file_index,
	long size);
static short cached_map_files_least_useful(
	short except_map_file_index);
static void cached_map_files_make_room(
	short map_file_index,
	long size);
static unsigned long long cached_map_files_fit_budget(
	void);

/* ---------- globals */

static struct cache_file_runtime_globals cache_file_globals;
/* port: (cache_files_open_cache_files, cache_files_unpack_update) */
static boolean cached_map_file_adopt(
	short map_file_index);
/* port: the most the cache files hold together (map_cache_budget), and
whether the copy in progress took the texture cache's memory for its
buffers (a copy has buffers of its own: map_cache_copy_buffer) */
static unsigned long long cached_map_files_budget;
/* port: (the least the budget is: what the Xbox's six cache files held) */
#define CACHED_MAP_FILES_LEAST_BUDGET ((unsigned long long)SOLO_CACHE_FILE_MAXIMUM_SIZE * 2 + \
	MAIN_MENU_CACHE_FILE_MAXIMUM_SIZE + (unsigned long long)MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE * 3)
static boolean cache_copy_buffer_stolen = TRUE;
/* port: the slots a worker is unpacking a map into (port/linux/src/map_unpack.c),
the game's thread's: no map until the worker is done and the slot passes
the check a slot gets as the game starts (cached_map_file_adopt) */
static boolean cached_map_file_unpacking[NUMBER_OF_CACHED_MAP_FILES];
static char cached_map_file_unpacking_name[NUMBER_OF_CACHED_MAP_FILES][32];

/* ---------- public code */

void tags_header_register_vertex_and_index_buffers(
	struct cache_file_tag_header *header)
{
	/* port: a long, as the buffers' counts are (a short wrapped on a count
	past 0x7FFF, and the loop never ended) */
	long index;

	for (index = 0; index < header->vertex_buffer_count; index++)
	{
		D3DVertexBuffer *vertex_buffer = &header->vertex_buffers[index];

		vertex_buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
		IDirect3DVertexBuffer8_Register(vertex_buffer, NULL);
	}

	for (index = 0; index < header->index_buffer_count; index++)
	{
		D3DIndexBuffer *index_buffer = &header->index_buffers[index];

		index_buffer->Common = D3DCOMMON_TYPE_INDEXBUFFER | 1;
	}

	return;
}

void tags_header_deregister_vertex_and_index_buffers(
	struct cache_file_tag_header *header)
{
	/* port: a long, as the buffers' counts are (a short wrapped on a count
	past 0x7FFF, and the loop never ended) */
	long index;

	for (index = 0; index < header->vertex_buffer_count; index++)
	{
		D3DVertexBuffer *vertex_buffer = &header->vertex_buffers[index];

		IDirect3DVertexBuffer8_BlockUntilNotBusy(vertex_buffer);
		match_assert(
			"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
			523,
			!IDirect3DVertexBuffer8_IsBusy(vertex_buffer));
	}

	for (index = 0; index < header->index_buffer_count; index++)
	{
		D3DIndexBuffer *index_buffer = &header->index_buffers[index];

		IDirect3DIndexBuffer8_BlockUntilNotBusy(index_buffer);
		match_assert(
			"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
			536,
			!IDirect3DIndexBuffer8_IsBusy(index_buffer));
	}

	return;
}

void structure_bsp_header_register_vertex_buffers(
	struct cache_file_structure_bsp_header *header)
{
	/* port: a long, as the buffers' counts are (a short wrapped on a count
	past 0x7FFF, and the loop never ended) */
	long index;

	for (index = 0; index < header->vertex_buffer_count; index++)
	{
		D3DVertexBuffer *vertex_buffer = &header->vertex_buffers[index];

		vertex_buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
		IDirect3DVertexBuffer8_Register(vertex_buffer, NULL);
	}

	for (index = 0; index < header->lightmap_vertex_buffer_count; index++)
	{
		D3DVertexBuffer *vertex_buffer = &header->lightmap_vertex_buffers[index];

		vertex_buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
		IDirect3DVertexBuffer8_Register(vertex_buffer, NULL);
	}

	return;
}

void structure_bsp_header_deregister_vertex_buffers(
	struct cache_file_structure_bsp_header *header)
{
	/* port: a long, as the buffers' counts are (a short wrapped on a count
	past 0x7FFF, and the loop never ended) */
	long index;

	rasterizer_globals.current_lock_operation = _rasterizer_lock_bsp_switch;

	for (index = 0; index < header->vertex_buffer_count; index++)
	{
		D3DVertexBuffer *vertex_buffer = &header->vertex_buffers[index];

		IDirect3DVertexBuffer8_BlockUntilNotBusy(vertex_buffer);
	}

	for (index = 0; index < header->lightmap_vertex_buffer_count; index++)
	{
		D3DVertexBuffer *vertex_buffer = &header->lightmap_vertex_buffers[index];

		IDirect3DVertexBuffer8_BlockUntilNotBusy(vertex_buffer);
	}

	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;

	return;
}

void cache_files_dispose(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		207,
		cache_file_globals.open_map_file_index==NONE);
	match_free(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		209,
		cache_file_globals.requests);

	return;
}

void cache_files_precache_set_priority(
	boolean blocking)
{
	/* port: (the copy is a worker's, port/linux/src/map_unpack.c, which
	runs a load's copy before any other) */
	(void)blocking;

	return;
}

boolean cache_files_precache_in_progress(
	void)
{
	return cache_file_globals.copy_in_progress;
}

boolean cache_files_precache_is_copying_map(
	const char *map_name)
{
	if (cache_file_globals.copying_to_map_file_index != NONE &&
		strcmp(
			cache_file_globals.copying_to_map_file_name,
			tag_name_strip_path(map_name)) == 0)
	{
		return TRUE;
	}

	return FALSE;
}

boolean cache_files_precache_map_loaded(
	const char *map_name)
{
	/* port: an MCC map (mcc_maps\<name>) is read in place (mcc_cache.c) */
	if (mcc_level_name(map_name))
		return mcc_cache_available(map_name);
	/* port: a Halo Custom Edition map (custom_maps\<name>) is read in place
	and never copied to the cache partition; it is never the game's own map
	of that file name (port/linux/game/custom_edition_cache.c) */
	short map_file_index;
	char path[256];

	if (custom_edition_level_name(map_name))
		return custom_edition_cache_playable(map_name);
	map_file_index = cached_map_files_find_map(tag_name_strip_path(map_name));
	/* port: a slot whose map file has been replaced since it was copied
	(cache_slot_source_matches) is freed, and the map copied again; never
	the open map's slot, or one being copied to */
	if (map_file_index != NONE &&
		map_file_index != cache_file_globals.open_map_file_index &&
		!(cache_file_globals.copy_in_progress && map_file_index == cache_file_globals.copying_to_map_file_index))
	{
		cache_file_get_map_path(tag_name_strip_path(map_name), path);
		if (!cache_slot_source_matches(map_file_index, path, cached_map_file_get(map_file_index)->header.name,
				(unsigned long)cached_map_file_get(map_file_index)->header.checksum,
				(unsigned long)cached_map_file_get(map_file_index)->header.file_length))
		{
			error(_error_silent, "map '%s' has changed since it was cached: copied again", map_name);
			cache_slot_source_forget(map_file_index);
			memset(&cached_map_file_get(map_file_index)->header, 0, sizeof(struct cache_file_header));
			memset(&cached_map_file_get(map_file_index)->last_modification_date, 0, sizeof(FILETIME));
			map_file_index = NONE;
		}
	}
	return map_file_index != NONE;
}

/* (the port's, port/linux/src/sdl_platform.c) */
void platform_log(char const *format, ...);

/* port: the checksum of a map of the maps folder (a10), as the precache
would copy it: its header read where and as cache_files_precache_map_begin
reads it (the map directory of the game's language, through the data root
paths.data names, any case), a cache file header naming the map's own file,
with a cache file to hold it; 0 if the precache would refuse it. The menus'
pictures ask before they load a map (port/linux/game/menu_pictures_render.c) */
unsigned long cache_files_map_copyable_checksum(
	const char *map_name)
{
	const char *cache_map_name = tag_name_strip_path(map_name);
	struct cache_file_header header;

	if (custom_edition_level_name(map_name) || !cache_file_read_header_from_dvd(cache_map_name, &header) ||
		_stricmp(header.name, cache_map_name) != 0 ||
		cached_map_files_find_free_map(header.file_length, header.scenario_type) == NONE)
	{
		return 0;
	}
	return (unsigned long)header.checksum;
}

/* port: whether a map can be loaded: in the cache already, a Custom Edition
map this machine plays, or one the precache would copy
(cache_files_map_copyable_checksum). A map named at the console (map_name)
or in a file is asked of before the game it would end is left: the Xbox's
precache took a map it could not find for a damaged disc and stopped the
game for good (display_error_damaged_media). The reason a map is refused is
logged. */
boolean cache_files_map_loadable(
	const char *map_name)
{
	const char *cache_map_name;
	struct cache_file_header header;

	if (!map_name || !map_name[0])
	{
		error(_error_silent, "no map named");
		return FALSE;
	}
	if (custom_edition_level_name(map_name))
	{
		if (custom_edition_cache_playable(map_name))
			return TRUE;
		error(_error_silent, "couldn't find the Custom Edition map '%s' in custom_maps", map_name);
		return FALSE;
	}
	if (cache_files_precache_map_loaded(map_name))
		return TRUE;
	cache_map_name = tag_name_strip_path(map_name);
	if (!cache_map_name[0] || !cache_file_read_header_from_dvd_checked(cache_map_name, &header, FALSE))
	{
		error(_error_silent, "couldn't find map '%s' (%s), or it is no map this build runs", cache_map_name, map_name);
		return FALSE;
	}
	if (_stricmp(header.name, cache_map_name) != 0)
	{
		error(_error_silent, "map '%s' names itself '%s' in its header: refused", cache_map_name, header.name);
		return FALSE;
	}
	if (cached_map_files_find_free_map(header.file_length, header.scenario_type) == NONE)
	{
		error(_error_silent, "no cache file can hold map '%s' (%08x bytes, type %d)",
			cache_map_name, header.file_length, header.scenario_type);
		return FALSE;
	}

	return TRUE;
}

boolean cache_files_precache_map_begin(
	const char *map_name,
	boolean copy_map)
{
	const char *cache_map_name = tag_name_strip_path(map_name);

	if (mcc_level_name(map_name))
	{
		/* port: a package being fetched from the host (port/linux/game/mcc_share.c)
		is validated once it is there */
		if (mcc_share_game_downloading(map_name))
			return TRUE;
		return mcc_cache_require(map_name, 0);
	}

	/* port: a Halo Custom Edition map (custom_maps\<name>) this machine has
	not is missing, as a map not on the DVD is: never the game's own map of
	its file name (port/linux/game/custom_edition_cache.c) */
	if (custom_edition_level_name(map_name) && !custom_edition_cache_playable(map_name))
	{
		error(_error_silent, "couldn't find the Custom Edition map '%s' in custom_maps", map_name);
		if (copy_map)
		{
			display_error_damaged_media();
		}

		return FALSE;
	}
	/* port: a map a worker is unpacking is waited for, never copied a
	second time into another slot (port/linux/src/map_unpack.c) */
	cache_files_unpack_wait(map_name);
	if (!cache_files_precache_map_loaded(map_name))
	{
		struct cache_file_header header;
		char path[256];

		if (cache_file_read_header_from_dvd(cache_map_name, &header))
		{
			short map_file_index = cached_map_files_find_free_map(
				header.file_length,
				header.scenario_type);
			struct cached_map_file *map_file;
			char slot_path[256];
			/* port: the cache file's size, its map's (sectors' reads past
			its end within it) */
			long copy_size = (header.file_length + 0xFFFF) & ~0xFFFF;

			/* port: the cache file slots are found by the name in their
			header (cached_map_files_find_map): a map file whose header names
			another map would be copied again each time it was asked for, for
			ever */
			if (_stricmp(header.name, cache_map_name) != 0)
			{
				error(_error_silent, "map '%s' names itself '%s' in its header: refused", cache_map_name, header.name);
				platform_log("map %s.map names itself '%s' in its header; a map's name must be its file's",
					cache_map_name, header.name);
				if (copy_map)
				{
					display_error_damaged_media();
				}

				return FALSE;
			}

			/* port: a map no cache file holds (of no type the cache files are
			for, or too big for its type's) is not precached; the texture
			cache's memory is taken only once one does */
			if (map_file_index == NONE)
			{
				error(_error_silent, "no cache file can hold map '%s' (%08x bytes, type %d)",
					cache_map_name, header.file_length, header.scenario_type);
				if (copy_map)
				{
					display_error_damaged_media();
				}

				return FALSE;
			}
			/* port: the copy is the unpacking workers' (port/linux/src/map_unpack.c),
			with buffers of their own: the texture cache keeps its memory */
			cache_copy_buffer_stolen = FALSE;
			map_file = cached_map_file_get(map_file_index);

			memset(
				&map_file->header,
				0,
				sizeof(struct cache_file_header));
			cache_slot_source_forget(map_file_index); /* port: (cache_slot_source_record) */
			cache_file_globals.copy_in_progress = TRUE;
			cache_file_globals.copying_to_map_file_index = map_file_index;
			strncpy(
				cache_file_globals.copying_to_map_file_name,
				cache_map_name,
				sizeof(cache_file_globals.copying_to_map_file_name) - 1);
			cache_file_globals.copying_to_map_file_name[
				sizeof(cache_file_globals.copying_to_map_file_name) - 1] = 0;
			cache_file_get_map_path(cache_map_name, path);
			error(_error_silent, "starting precaching of map '%s'", cache_map_name);
			/* port: the maps given up for its room (the least useful first),
			and the file emptied and made its map's size */
			cached_map_files_make_room(map_file_index, copy_size);
			if (!cached_map_file_resize(map_file_index, 0) || !cached_map_file_resize(map_file_index, copy_size))
			{
				error(_error_silent, "couldn't size cache file %d for map '%s'", map_file_index, cache_map_name);
			}
			/* port: the map unpacked into the slot by a worker, whole as the
			Xbox's decompressor (cache_files_decompress_windows.c) wrote it, and
			its source marked last; ended in cache_files_precache_map_end */
			cached_map_file_get_path(map_file_index, slot_path);
			if (!path[0] || !map_unpack_queue(map_file_index, path, slot_path, copy_size, TRUE))
			{
				error(_error_silent, "couldn't begin the copy of map '%s'", cache_map_name);
				cache_file_globals.copy_in_progress = FALSE;
				cache_file_globals.copying_to_map_file_index = NONE;
				cached_map_file_resize(map_file_index, 0);
				if (copy_map)
				{
					display_error_damaged_media();
				}

				return FALSE;
			}
		}
		else
		{
			error(_error_silent, "couldn't find map '%s' on the DVD", cache_map_name);
			error(_error_silent, "full path name '%s'", map_name);
			if (copy_map)
			{
				display_error_damaged_media();
			}

			return FALSE;
		}
	}

	return TRUE;
}

void cache_files_initialize(
	void)
{
	cache_file_globals.open_map_file_index = NONE;
	cache_file_globals.requests = match_malloc(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		187,
		MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS * sizeof(struct cache_file_request));
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		188,
		cache_file_globals.requests);
	/* port: cache_file_open clears the requests before a map is read; a Halo
	Custom Edition map is read without it, so they start out free
	(port/linux/game/custom_edition_cache.c) */
	memset(cache_file_globals.requests, 0, MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS * sizeof(struct cache_file_request));
	cache_file_windows_thread_create();
	cache_files_verify_language();
	cache_files_open_cache_files();
	/* port: the cache files' budget, of the disk's space (at least what the
	Xbox's six held), port/linux/src/map_cache.c */
	cached_map_files_budget = CACHED_MAP_FILES_LEAST_BUDGET;
	/* (a copy of the game running beside the one that keeps the cache:
	those six's room alone, cache_slot_folder) */
	if (cache_slot_folder_keeps_cache())
	{
		cached_map_files_budget = map_cache_budget(cached_map_files_bytes(NONE), cached_map_files_budget);
		/* (a cache past it, kept at a larger budget before: given up to fit
		now, as the setting's change does while the game runs) */
		cached_map_files_fit_budget();
	}
	/* port: (the copies are port/linux/src/map_unpack.c's: the Xbox's
	decompressor's thread is not started) */

	return;
}

/* port: a copy's slot taken as a whole map: only a copy the worker ended
whole, the slot on the disk and its source marked last
(port/linux/src/map_unpack.c), that passes the check of the game's start
(cached_map_file_adopt), its header the map's it was copying; any other (failed, ended, its
header or pages not written) emptied, so that it is copied again, never
played (port/linux/src/xbox_files.c's cache slots' sources) */
static void cache_files_precache_map_mark(
	short map_file_index)
{
	struct cached_map_file *map_file = cached_map_file_get(map_file_index);

	if (map_unpack_take(map_file_index) &&
		cached_map_file_adopt(map_file_index) &&
		map_file->header.name[0] &&
		_stricmp(map_file->header.name, cache_file_globals.copying_to_map_file_name) == 0 &&
		cached_map_file_get_size(map_file_index) >= map_file->header.file_length)
	{
		return;
	}
	if (map_file->header.name[0])
		error(_error_silent, "the copy of map '%s' did not end whole: copied again", map_file->header.name);
	cache_slot_source_forget(map_file_index);
	memset(&map_file->header, 0, sizeof(struct cache_file_header));
	memset(&map_file->last_modification_date, 0, sizeof(FILETIME));
	cached_map_file_resize(map_file_index, 0);
}

void cache_files_precache_map_end(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1031,
		cache_file_globals.copy_in_progress);
	/* port: (the worker's copy waited for: cache_files_precache_map_mark) */
	if (cache_copy_buffer_stolen) /* port: (cache_files_precache_map_begin) */
		texture_cache_return_memory();
	cache_copy_buffer_stolen = TRUE;
	cached_map_file_set_modification_date(cache_file_globals.copying_to_map_file_index);
	cache_files_precache_map_mark(cache_file_globals.copying_to_map_file_index);
	cache_file_globals.copy_in_progress = FALSE;
	cache_file_globals.copying_to_map_file_index = NONE;

	return;
}

void cache_files_precache_map_queue_end(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1022,
		cache_file_globals.copy_in_progress);
	map_unpack_stop(cache_file_globals.copying_to_map_file_index); /* port: (map_unpack.c) */

	return;
}

void cache_file_promote_read(
	short request_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		614,
		request_index>=0 && request_index<MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS);
	cache_file_globals.requests[request_index].blocking = TRUE;

	return;
}

void cache_file_block_until_not_busy(
	void)
{
	boolean busy;
	short request_index;

	do
	{
		SleepEx(0, TRUE);
		busy = FALSE;
		for (request_index = 0;
			request_index < MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS;
			request_index++)
		{
			match_assert(
				"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
				614,
				request_index>=0 && request_index<MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS);
			if (cache_file_globals.requests[request_index].pending)
			{
				busy = TRUE;
			}
		}
	}
	while (busy);

	return;
}

void cache_file_close(
	void)
{
	if (cache_file_globals.open_map_file_index != NONE)
	{
		cache_requests_flush();
		cache_file_globals.open_map_file_index = NONE;
	}

	return;
}

boolean cache_file_open(
	const char *scenario_name,
	struct cache_file_header *header)
{
	short map_file_index = cached_map_files_find_map(scenario_name);

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		220,
		scenario_name);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		221,
		header);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		223,
		cache_file_globals.open_map_file_index==NONE);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		224,
		map_file_index!=NONE);
	/* port: a map in no cache file is not opened (its index would be
	NONE's, before the cache files) */
	if (map_file_index == NONE)
	{
		error(_error_silent, "the map '%s' is in no cache file", scenario_name);

		return FALSE;
	}
	memset(
		cache_file_globals.requests,
		0,
		MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS * sizeof(struct cache_file_request));
	cache_file_globals.open_map_file_index = map_file_index;
	memcpy(
		header,
		&cached_map_file_get(map_file_index)->header,
		sizeof(struct cache_file_header));
	/* port: played (not a map of the menus' pictures' pass): the maps
	played least recently, then those never played, are given up first for
	room (cached_map_files_least_useful) */
	if (!menu_pictures_preparing())
		cache_slot_source_played(map_file_index);

	return TRUE;
}

short cache_file_read(
	long tag_index,
	long offset,
	long size,
	void *buffer,
	boolean *completion_flag_reference,
	boolean blocking)
{
	short request_index = cache_request_next_free_index();
	struct cache_file_request *request = cache_request_get(request_index);

	/* port: the reads of an MCC map are served in place, at once
	(port/linux/game/mcc_cache.c) */
	if (mcc_cache_tags_loaded())
	{
		*completion_flag_reference = mcc_cache_read(tag_index, offset, size, buffer) ?
			TRUE : _cache_file_read_failed;
		return request_index;
	}
	/* port: the reads of a Halo Custom Edition map are served in place, at
	once; the request stays free (port/linux/game/custom_edition_cache.c) */
	if (custom_edition_cache_tags_loaded())
	{
		custom_edition_cache_read(tag_index, offset, size, buffer);
		*completion_flag_reference = TRUE;

		return request_index;
	}
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		269,
		cache_file_globals.open_map_file_index!=NONE);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		272,
		buffer);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		273,
		completion_flag_reference);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		276,
		offset>=0);
	/* port: the offset and size are the map's (a texture's or sound's,
	which nothing checks): a negative one is no read, failed at once (a
	negative size read the file to its end over what follows the buffer) */
	if (offset < 0 || size < 0)
	{
		error(_error_silent, "cache file read of %08x bytes at %08x refused", size, offset);
		*completion_flag_reference = _cache_file_read_failed;

		return NONE;
	}
	if (size & (CACHE_FILE_SECTOR_SIZE - 1))
	{
		size = (size | (CACHE_FILE_SECTOR_SIZE - 1)) + 1;
	}
	*completion_flag_reference = FALSE;
	memset(
		&request->overlapped,
		0,
		sizeof(OVERLAPPED));
	request->overlapped.hEvent = (HANDLE)completion_flag_reference;
	request->size = size;
	request->overlapped.OffsetHigh = 0;
	request->overlapped.Offset = offset;
	request->buffer = buffer;
	request->pending = TRUE;
	request->blocking = blocking;
	request->running = FALSE;
	cache_file_windows_thread_wake();

	return request_index;
}

short cache_files_precache_map_status(
	real *progress)
{
	short status = _cached_map_file_failed;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		984,
		cache_file_globals.copy_in_progress);
	/* port: the worker's copy (port/linux/src/map_unpack.c) */
	switch (map_unpack_status(cache_file_globals.copying_to_map_file_index, progress))
	{
		case 0:
			status = _cached_map_file_in_progress;
			break;

		case 1:
			status = _cached_map_file_success;
			break;

		default:
			status = _cached_map_file_failed;
			break;
	}

	return status;
}

/* ---------- private code */

static void cached_map_files_delete(
	short map_file_index)
{
	char path[256];

	while (++map_file_index < MAXIMUM_CACHE_FILES)
	{
		cached_map_file_get_path(map_file_index, path);
		DeleteFileA(path);
	}

	SetLastError(ERROR_SUCCESS);

	return;
}

static void cache_files_verify_language(
	void)
{
	struct file_reference reference;
	long file_language;
	long language = XGetLanguage();
	long last_language = NONE;

	if (file_reference_create_from_path(&reference, "z:\\last_language.dat", FALSE))
	{
		if (file_open(&reference, FLAG(_permission_read_bit)))
		{
			if (file_read(&reference, sizeof(long), &file_language))
			{
				last_language = file_language;
			}
			file_close(&reference);
		}
	}

	if (last_language != language)
	{
		short map_file_index;

		for (map_file_index = NONE;
			map_file_index < NUMBER_OF_CACHED_MAP_FILES;
			map_file_index++)
		{
			cached_map_files_delete(map_file_index);
		}
	}

	if (file_reference_create_from_path(&reference, "z:\\last_language.dat", FALSE))
	{
		if (file_create(&reference))
		{
			if (file_open(&reference, FLAG(_permission_write_bit)))
			{
				file_write(&reference, sizeof(long), &language);
				file_close(&reference);
			}
		}
	}

	return;
}

static void cache_files_open_cache_files(
	void)
{
	char path[256];
	short map_file_index;

	cached_map_files_delete(NUMBER_OF_CACHED_MAP_FILES);
	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		struct cached_map_file *map_file = cached_map_file_get(map_file_index);
		HANDLE file;

		/* port: a cache file is its map's size (cache_files_precache_map_begin),
		not its slot's; one not a whole map of the maps folder is emptied
		(cached_map_file_adopt) */
		cached_map_file_get_path(map_file_index, path);
		file = CreateFileA(
			path,
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_ALWAYS,
			FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
			NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
				823,
				FALSE,
				csprintf(temporary, "couldn't open or create new cache file (#%d)", GetLastError()));
		}

		map_file->file = file;
		memset(&map_file->header, 0, sizeof(struct cache_file_header));
		if (file == INVALID_HANDLE_VALUE)
		{
			continue;
		}
		cached_map_file_adopt(map_file_index);
	}

	return;
}

/* port: a slot file taken as its map only if it is a whole map of the maps
folder: its header read, the map's in the maps folder (its checksum), and
the map file the slot was copied from still the one in the maps folder,
not another of the same name and header (a modded map, which keeps the
checksum: the Xbox's disc never changed), with its source's mark of a
whole copy (cache_slot_source_matches). Else it is emptied, which gives
its space back (the six of fixed sizes of the builds before keep their
maps). As the game starts, and as a slot a worker unpacked is taken back
(cache_files_unpack_update) */
static boolean cached_map_file_adopt(
	short map_file_index)
{
	struct cached_map_file *map_file = cached_map_file_get(map_file_index);
	struct cache_file_header dvd_header;
	char map_path[256];
	boolean valid = FALSE;

	memset(&map_file->header, 0, sizeof(struct cache_file_header));
	if (map_file->file == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	if (cached_map_file_get_size(map_file_index) >= (long)sizeof(struct cache_file_header))
	{
		char *cache_map_name = map_file->header.name;

		cached_map_file_read_header(map_file_index);
		cache_slot_source_load(map_file_index);
		cache_file_get_map_path(cache_map_name, map_path);
		valid = map_file->file != INVALID_HANDLE_VALUE &&
			cache_map_name[0] &&
			cached_map_file_get_size(map_file_index) >= map_file->header.file_length &&
			cache_file_read_header_from_dvd(cache_map_name, &dvd_header) &&
			map_file->header.checksum == dvd_header.checksum &&
			cache_slot_source_matches(map_file_index, map_path, cache_map_name,
				(unsigned long)map_file->header.checksum, (unsigned long)map_file->header.file_length);
	}
	if (valid)
	{
		return TRUE;
	}

	cache_slot_source_forget(map_file_index);
	memset(
		&map_file->header,
		0,
		sizeof(struct cache_file_header));
	if (map_file->file != INVALID_HANDLE_VALUE)
	{
		cached_map_file_resize(map_file_index, 0);
	}

	return FALSE;
}

static void CALLBACK cache_file_read_io_completion_routine(
	unsigned long error_code,
	unsigned long bytes_transferred,
	OVERLAPPED *overlapped)
{
	struct cache_file_request *finished_request = (struct cache_file_request *)overlapped;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1387,
		error_code==ERROR_SUCCESS);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1388,
		bytes_transferred==finished_request->size);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1389,
		finished_request->overlapped.hEvent);
	/* port: a read that failed or came up short completes as failed: what
	waits on it stops waiting, and a caller that checks (the map's tags and
	bsps) sees it, instead of taking what is in its buffer for the map */
	*(volatile boolean *)finished_request->overlapped.hEvent =
		error_code == ERROR_SUCCESS && bytes_transferred == (unsigned long)finished_request->size ?
			TRUE :
			_cache_file_read_failed;
	finished_request->pending = FALSE;
	finished_request->running = FALSE;

	return;
}

static void cache_file_windows_thread_proc(
	void)
{
	while (TRUE)
	{
		while (WaitForSingleObjectEx(
			cache_file_globals.sleep_event,
			INFINITE,
			TRUE) == WAIT_IO_COMPLETION)
			;

		while (TRUE)
		{
			struct cache_file_request *best_request = NULL;
			short request_index;
			HANDLE file;

			for (request_index = 0;
				request_index < MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS;
				request_index++)
			{
				struct cache_file_request *request = cache_request_get(request_index);

				if (request->pending &&
					!request->running &&
					(!best_request ||
						(best_request->blocking > request->blocking &&
						best_request->overlapped.Offset > request->overlapped.Offset)))
				{
					best_request = request;
				}
			}

			if (!best_request)
			{
				break;
			}

			file = cached_map_file_get_handle(cache_file_globals.open_map_file_index);
			match_assert(
				"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
				1327,
				!best_request->running);
			best_request->running = TRUE;
			cached_map_issue_async_read(
				file,
				&best_request->overlapped,
				best_request->buffer,
				best_request->size,
				best_request->overlapped.Offset,
				(volatile boolean *)best_request->overlapped.hEvent,
				cache_file_read_io_completion_routine);
		}
	}

	return;
}

static void cache_file_windows_thread_create(
	void)
{
	cache_file_globals.sleep_event = CreateEventA(NULL, FALSE, FALSE, NULL);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1253,
		cache_file_globals.sleep_event);
	cache_file_globals.thread = CreateThread(
		NULL,
		CACHE_FILE_THREAD_STACK_SIZE,
		(LPTHREAD_START_ROUTINE)cache_file_windows_thread_proc,
		NULL,
		0,
		NULL);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1257,
		cache_file_globals.thread);

	return;
}

static void cache_file_get_map_path(
	const char *map_name,
	char *path)
{
	/* port: no more than the callers' paths hold (256; the name can be a
	host's, over the network). One that doesn't fit is no path (no file is
	found), not a cut one (another file could be). */
	enum
	{
		MAXIMUM_MAP_PATH_LENGTH = 256,
	};
	int length = snprintf(path, MAXIMUM_MAP_PATH_LENGTH, "%s%s.map", cache_files_map_directory(), map_name);

	if (length < 0 || length >= MAXIMUM_MAP_PATH_LENGTH)
	{
		error(_error_silent, "map path for '%.64s' is too long", map_name);
		path[0] = 0;
	}

	return;
}

/* port: (with fatal FALSE, a file that is no cache file is refused without
its assertion: cache_files_map_loadable) */
static boolean cache_file_read_header_from_dvd_checked(
	const char *map_name,
	struct cache_file_header *header,
	boolean fatal)
{
	boolean result = FALSE;
	char path[256];
	HANDLE file;

	cache_file_get_map_path(map_name, path);
	file = CreateFileA(
		path,
		GENERIC_READ,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		unsigned long bytes_read;

		if (ReadFile(
			file,
			header,
			sizeof(struct cache_file_header),
			&bytes_read,
			NULL) &&
			bytes_read == sizeof(struct cache_file_header) &&
			cache_file_header_verify(header, path, fatal))
		{
			result = TRUE;
		}
		CloseHandle(file);
	}

	return result;
}

static boolean cache_file_read_header_from_dvd(
	const char *map_name,
	struct cache_file_header *header)
{
	return cache_file_read_header_from_dvd_checked(map_name, header, TRUE);
}

static short cached_map_files_find_free_map(
	long file_length,
	short scenario_type)
{
	/* port: any cache file holds a map of any type, up to the most its type
	had on the Xbox: an empty one while the budget has room for the map,
	else the least useful map's (cached_map_files_least_useful), else an
	empty one all the same; never the open map's, nor the one a copy is
	going to */
	short best_map_file_index = NONE;
	short empty_map_file_index = NONE;
	short map_file_index;

	if (file_length >= cached_map_type_maximum_size(scenario_type))
	{
		return NONE;
	}

	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		struct cached_map_file *map_file = cached_map_file_get(map_file_index);

		if (cache_file_globals.open_map_file_index != map_file_index &&
			!(cache_file_globals.copy_in_progress && cache_file_globals.copying_to_map_file_index == map_file_index) &&
			!cached_map_file_unpacking[map_file_index] &&
			map_file->file != INVALID_HANDLE_VALUE &&
			!map_file->header.name[0])
		{
			empty_map_file_index = map_file_index;
			break;
		}
	}
	if (empty_map_file_index != NONE &&
		cached_map_files_bytes(empty_map_file_index) + (unsigned long long)file_length <= cached_map_files_budget)
	{
		best_map_file_index = empty_map_file_index;
	}
	else
	{
		best_map_file_index = cached_map_files_least_useful(NONE);
		if (best_map_file_index == NONE)
		{
			best_map_file_index = empty_map_file_index;
		}
	}

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1200,
		cache_file_globals.open_map_file_index!=best_map_file_index);

	return best_map_file_index;
}

static long cached_map_file_get_size(
	short map_file_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1232,
		map_file_index>=0 && map_file_index<NUMBER_OF_CACHED_MAP_FILES);
	/* port: the cache file's own (its map's: cache_files_precache_map_begin),
	0 if it did not open */
	if (cached_map_file_get(map_file_index)->file == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	return (long)GetFileSize(cached_map_file_get(map_file_index)->file, NULL);
}

static HANDLE cached_map_file_get_handle(
	short map_file_index)
{
	return cached_map_file_get(map_file_index)->file;
}

static void CALLBACK cache_file_blocking_io_completion_routine(
	unsigned long error_code,
	unsigned long bytes_transferred,
	OVERLAPPED *overlapped)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1405,
		error_code==ERROR_SUCCESS);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1406,
		overlapped->hEvent);
	*(volatile boolean *)overlapped->hEvent = TRUE;

	return;
}

static void cached_map_issue_async_request(
	cached_map_async_request_proc async_request_function,
	HANDLE file,
	OVERLAPPED *overlapped,
	void *buffer,
	long size,
	long offset,
	volatile boolean *completion_flag,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		390,
		async_request_function);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		391,
		file!=INVALID_HANDLE_VALUE);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		392,
		overlapped);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		393,
		buffer);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		394,
		completion_flag);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		395,
		completion_routine);
	memset(
		overlapped,
		0,
		sizeof(OVERLAPPED));
	overlapped->Offset = offset;
	overlapped->OffsetHigh = 0;
	overlapped->hEvent = (HANDLE)completion_flag;
	while (TRUE)
	{
		unsigned long error_code;

		SleepEx(0, TRUE);
		SetLastError(ERROR_SUCCESS);
		if (async_request_function(
			file,
			buffer,
			size,
			overlapped,
			completion_routine))
		{
			break;
		}

		error_code = GetLastError();
		match_vassert(
			"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
			422,
			error_code==ERROR_NOT_ENOUGH_MEMORY || error_code==ERROR_NO_SYSTEM_RESOURCES || error_code==ERROR_INVALID_USER_BUFFER,
			csprintf(temporary, "Read/WriteFileEx() returned #%d", GetLastError()));
	}

	return;
}

static void cached_map_issue_async_read(
	HANDLE file,
	OVERLAPPED *overlapped,
	void *buffer,
	long size,
	long offset,
	volatile boolean *completion_flag,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	cached_map_issue_async_request(
		ReadFileEx,
		file,
		overlapped,
		buffer,
		size,
		offset,
		completion_flag,
		completion_routine);

	return;
}

static void cached_map_issue_async_write(
	HANDLE file,
	OVERLAPPED *overlapped,
	void *buffer,
	long size,
	long offset,
	volatile boolean *completion_flag,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	cached_map_issue_async_request(
		(cached_map_async_request_proc)WriteFileEx,
		file,
		overlapped,
		buffer,
		size,
		offset,
		completion_flag,
		completion_routine);

	return;
}

static boolean cached_map_block_on_async_request(
	volatile boolean const *completion_flag)
{
	boolean completed;

	while (!*completion_flag)
	{
		if (SleepEx(5000, TRUE) != WAIT_IO_COMPLETION)
		{
			break;
		}
	}
	completed = *completion_flag;

	return completed;
}

static void cached_map_file_get_path(
	short map_file_index,
	char *path)
{
	/* port: this copy of the game's folder (cache_slot_folder) */
	sprintf(path, "%scache%03d.map", cache_slot_folder(), map_file_index);

	return;
}

static void cached_map_file_set_modification_date(
	short map_file_index)
{
	struct cached_map_file *map_file = cached_map_file_get(map_file_index);
	SYSTEMTIME system_time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &map_file->last_modification_date);
	SetFileTime(map_file->file, &map_file->last_modification_date, NULL, NULL);

	return;
}

static void cached_map_file_read_header(
	short map_file_index)
{
	struct cached_map_file *map_file = cached_map_file_get(map_file_index);
	char path[256];
	OVERLAPPED overlapped;
	volatile boolean completion_flag;

	cached_map_file_get_path(map_file_index, path);
	GetFileTime(map_file->file, &map_file->last_modification_date, NULL, NULL);
	completion_flag = FALSE;
	cached_map_issue_async_read(
		map_file->file,
		&overlapped,
		&map_file->header,
		sizeof(struct cache_file_header),
		0,
		&completion_flag,
		cache_file_blocking_io_completion_routine);
	cached_map_block_on_async_request(&completion_flag);
	if (completion_flag)
	{
		if (!cache_file_header_verify(&map_file->header, path, FALSE))
		{
			memset(&map_file->header, 0, sizeof(struct cache_file_header));
			memset(&map_file->last_modification_date, 0, sizeof(FILETIME));
		}
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
			1121,
			FALSE,
			csprintf(temporary, "couldn't read header from cache file (#%d)", GetLastError()));
		cached_map_file_invalidate(map_file_index);
	}

	return;
}

static struct cache_file_request *cache_request_get(
	short request_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		614,
		request_index>=0 && request_index<MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS);

	return cache_file_globals.requests + request_index;
}

static short cache_request_next_free_index(
	void)
{
	boolean out_of_requests = FALSE;

	while (TRUE)
	{
		short request_index;

		for (request_index = 0;
			request_index < MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS;
			request_index++)
		{
			if (!cache_request_get(request_index)->pending)
			{
				return request_index;
			}
		}

		/* every request slot is busy: remember that we blocked and scan again
		until the elevator thread retires one of them */
		if (!out_of_requests)
		{
			out_of_requests = TRUE;
		}
	}

	return NONE;
}

static void cache_file_windows_thread_wake(
	void)
{
	SetEvent(cache_file_globals.sleep_event);

	return;
}

static void cache_requests_flush(
	void)
{
	short request_index;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		651,
		cache_file_globals.open_map_file_index!=NONE);
	for (request_index = 0;
		request_index < MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS;
		request_index++)
	{
		volatile struct cache_file_request *request = cache_request_get(request_index);

		while (request->pending)
			;
	}

	return;
}

static struct cached_map_file *cached_map_file_get(
	short map_file_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files_windows.c",
		1208,
		map_file_index>=0 && map_file_index<NUMBER_OF_CACHED_MAP_FILES);

	return cache_file_globals.cached_map_files + map_file_index;
}

static void cached_map_file_invalidate(
	short map_file_index)
{
	struct cached_map_file *map_file = cached_map_file_get(map_file_index);

	if (map_file->file != INVALID_HANDLE_VALUE)
	{
		CloseHandle(map_file->file);
	}
	map_file->file = INVALID_HANDLE_VALUE;

	return;
}

static short cached_map_files_find_map(
	const char *map_name)
{
	short map_file_index;

	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		if (_stricmp(
			map_name,
			cached_map_file_get(map_file_index)->header.name) == 0)
		{
			return map_file_index;
		}
	}

	return NONE;
}

/* port: the most a map of a type may be (the Xbox's cache files' sizes) */
static long cached_map_type_maximum_size(
	short scenario_type)
{
	switch (scenario_type)
	{
		case _scenario_type_solo:
			return SOLO_CACHE_FILE_MAXIMUM_SIZE;

		case _scenario_type_multiplayer:
			return MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE;

		case _scenario_type_main_menu:
			return MAIN_MENU_CACHE_FILE_MAXIMUM_SIZE;

		/* (the type is the map's header's, and a release build's assertion
		goes on: a type no cache file is for finds none) */
		default:
			match_vassert("c:\\halo\\SOURCE\\cache\\cache_files_windows.c", 1172, FALSE, NULL);
			return 0;
	}
}

/* port: the bytes the cache files take, but one's */
static unsigned long long cached_map_files_bytes(
	short except_map_file_index)
{
	unsigned long long bytes = 0;
	short map_file_index;

	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		if (map_file_index != except_map_file_index)
		{
			bytes += (unsigned long)cached_map_file_get_size(map_file_index);
		}
	}

	return bytes;
}

/* port: a cache file made size bytes long (0 gives its space back) */
static boolean cached_map_file_resize(
	short map_file_index,
	long size)
{
	HANDLE file = cached_map_file_get_handle(map_file_index);

	return file != INVALID_HANDLE_VALUE &&
		SetFilePointer(file, size, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		SetEndOfFile(file);
}

/* port: how useful a map is kept, by its name, when it has not been played:
the main menu's, the campaign's levels in their order, then the multiplayer
maps, the most played first; another map least (larger is more useful) */
static long cached_map_usefulness(
	char const *name)
{
	static char const *const useful_maps[] =
	{
		"ui", "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
		"bloodgulch", "hangemhigh", "chillout", "prisoner", "damnation", "sidewinder", "ratrace",
		"beavercreek", "longest", "wizard", "carousel", "boardingaction", "putput",
	};
	long index;

	for (index = 0; index < NUMBEROF(useful_maps); index++)
	{
		if (_stricmp(name, useful_maps[index]) == 0)
		{
			return NUMBEROF(useful_maps) - index;
		}
	}

	return 0;
}

/* port: the map given up first for room: of those a cache file holds (not
the open map's, nor the one a copy is going to, nor except's), the one
played least recently, those never played (copied by the menus' pictures'
pass, or ahead: port/linux/game/map_cache_prefill.c) before any played and
the least useful of them first; NONE if there is none */
static short cached_map_files_least_useful(
	short except_map_file_index)
{
	short best_map_file_index = NONE;
	unsigned long best_played = 0;
	long best_usefulness = 0;
	short map_file_index;

	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		struct cached_map_file *map_file = cached_map_file_get(map_file_index);
		unsigned long played;
		long usefulness;

		if (map_file_index == except_map_file_index ||
			map_file_index == cache_file_globals.open_map_file_index ||
			(cache_file_globals.copy_in_progress && map_file_index == cache_file_globals.copying_to_map_file_index) ||
			cached_map_file_unpacking[map_file_index] ||
			map_file->file == INVALID_HANDLE_VALUE ||
			!map_file->header.name[0])
		{
			continue;
		}
		played = cache_slot_source_played_time(map_file_index);
		usefulness = cached_map_usefulness(map_file->header.name);
		if (best_map_file_index == NONE ||
			played < best_played ||
			(played == best_played && usefulness < best_usefulness))
		{
			best_map_file_index = map_file_index;
			best_played = played;
			best_usefulness = usefulness;
		}
	}

	return best_map_file_index;
}

/* port: the maps given up, the least useful first, until the cache files
have room within the budget for size bytes in map_file_index's */
static void cached_map_files_make_room(
	short map_file_index,
	long size)
{
	while (cached_map_files_bytes(map_file_index) + (unsigned long long)size > cached_map_files_budget)
	{
		short victim_map_file_index = cached_map_files_least_useful(map_file_index);
		struct cached_map_file *victim;

		if (victim_map_file_index == NONE)
		{
			break;
		}
		victim = cached_map_file_get(victim_map_file_index);
		error(_error_silent, "map '%s' given up from the cache for room", victim->header.name);
		cache_slot_source_forget(victim_map_file_index);
		memset(&victim->header, 0, sizeof(struct cache_file_header));
		memset(&victim->last_modification_date, 0, sizeof(FILETIME));
		cached_map_file_resize(victim_map_file_index, 0);
	}

	return;
}

/* port: the maps given up, the least useful first, until the cache files
fit their budget (a smaller one: Settings' DISK SPACE SAVER, game.map_cache_megabytes);
never the open map's, nor the one a copy is going to: the bytes given up */
static unsigned long long cached_map_files_fit_budget(
	void)
{
	unsigned long long before = cached_map_files_bytes(NONE);

	cached_map_files_make_room(NONE, 0);

	return before - cached_map_files_bytes(NONE);
}

/* port: the budget read again (game.map_cache_megabytes changed:
port/linux/game/map_cache_prefill.c), and the cache made to fit it now: the
workers' unpacks ended first (their slots, unfinished, emptied), as a copy
begun for the larger budget may not fit the smaller; the bytes given up */
unsigned long long cache_files_budget_update(
	void)
{
	short map_file_index;

	if (!cache_slot_folder_keeps_cache())
	{
		return 0;
	}
	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		if (cached_map_file_unpacking[map_file_index])
		{
			map_unpack_stop(map_file_index);
			map_unpack_wait(map_file_index);
		}
	}
	cache_files_unpack_update();
	cached_map_files_budget = map_cache_budget(cached_map_files_bytes(NONE), CACHED_MAP_FILES_LEAST_BUDGET);

	return cached_map_files_fit_budget();
}

/* port: the bytes a copy of a map (a10) into the cache would take, without
giving up another map's (port/linux/game/map_cache_prefill.c); 0 if a cache
file holds it, the precache would refuse it, or the budget has no room */
long cache_files_map_prefill_size(
	char const *map_name)
{
	char const *cache_map_name = tag_name_strip_path(map_name);
	struct cache_file_header header;
	long copy_size;
	short map_file_index;

	if (custom_edition_level_name(map_name) ||
		cache_files_precache_map_loaded(map_name) ||
		!cache_file_read_header_from_dvd(cache_map_name, &header) ||
		_stricmp(header.name, cache_map_name) != 0 ||
		header.file_length >= cached_map_type_maximum_size(header.scenario_type))
	{
		return 0;
	}
	copy_size = (header.file_length + 0xFFFF) & ~0xFFFF;
	map_file_index = cached_map_files_find_free_map(header.file_length, header.scenario_type);
	if (map_file_index == NONE ||
		cached_map_file_get(map_file_index)->header.name[0] ||
		cached_map_files_bytes(map_file_index) + (unsigned long long)copy_size > cached_map_files_budget)
	{
		return 0;
	}

	return copy_size;
}

/* port: the bytes the cache files take, their budget, and the maps they
hold (debug.load_trace) */
void cache_files_cache_usage(
	unsigned long long *bytes,
	unsigned long long *budget,
	long *maps)
{
	short map_file_index;

	*bytes = cached_map_files_bytes(NONE);
	*budget = cached_map_files_budget;
	*maps = 0;
	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		*maps += cached_map_file_get(map_file_index)->header.name[0] != 0;
	}

	return;
}

/* port: the slot a worker is unpacking a map (a10) into, NONE if none */
static short cached_map_files_find_unpacking(
	char const *map_name)
{
	short map_file_index;

	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		if (cached_map_file_unpacking[map_file_index] &&
			_stricmp(cached_map_file_unpacking_name[map_file_index], tag_name_strip_path(map_name)) == 0)
		{
			return map_file_index;
		}
	}

	return NONE;
}

/* port: a stock map (a10) unpacked into a slot by a worker
(port/linux/src/map_unpack.c), if the precache would copy it and the budget
has room for it without giving up another map (cache_files_map_prefill_size's
rules): its slot chosen here and held, emptied and made the map's size,
its source forgotten, until the worker is done (cache_files_unpack_update).
FALSE if it was not begun */
boolean cache_files_unpack_begin(
	char const *map_name)
{
	char const *cache_map_name = tag_name_strip_path(map_name);
	struct cache_file_header header;
	char map_path[256], slot_path[256];
	short map_file_index;
	long copy_size;

	if (!cache_slot_folder_keeps_cache() ||
		cached_map_files_find_unpacking(map_name) != NONE ||
		cache_files_precache_is_copying_map(map_name) ||
		(copy_size = cache_files_map_prefill_size(map_name)) <= 0 ||
		!cache_file_read_header_from_dvd(cache_map_name, &header))
	{
		return FALSE;
	}
	map_file_index = cached_map_files_find_free_map(header.file_length, header.scenario_type);
	if (map_file_index == NONE || cached_map_file_get(map_file_index)->header.name[0])
	{
		return FALSE;
	}
	cache_slot_source_forget(map_file_index);
	if (!cached_map_file_resize(map_file_index, 0) || !cached_map_file_resize(map_file_index, copy_size))
	{
		cached_map_file_resize(map_file_index, 0);
		return FALSE;
	}
	cache_file_get_map_path(cache_map_name, map_path);
	cached_map_file_get_path(map_file_index, slot_path);
	if (!map_path[0] || !map_unpack_queue(map_file_index, map_path, slot_path, copy_size, FALSE))
	{
		cached_map_file_resize(map_file_index, 0);
		return FALSE;
	}
	cached_map_file_unpacking[map_file_index] = TRUE;
	strncpy(cached_map_file_unpacking_name[map_file_index], cache_map_name,
		sizeof(cached_map_file_unpacking_name[map_file_index]) - 1);
	cached_map_file_unpacking_name[map_file_index][sizeof(cached_map_file_unpacking_name[map_file_index]) - 1] = 0;

	return TRUE;
}

/* port: the slots the workers are done with taken back, each only once it
passes the check of the game's start (cached_map_file_adopt) */
void cache_files_unpack_update(
	void)
{
	short map_file_index;
	int succeeded;

	while (map_unpack_take_finished(&map_file_index, &succeeded))
	{
		if (map_file_index < 0 || map_file_index >= NUMBER_OF_CACHED_MAP_FILES ||
			!cached_map_file_unpacking[map_file_index])
		{
			continue;
		}
		cached_map_file_unpacking[map_file_index] = FALSE;
		if (succeeded && cached_map_file_adopt(map_file_index))
		{
			cached_map_file_set_modification_date(map_file_index);
		}
		else
		{
			error(_error_silent, "the unpack of map '%s' did not end whole: copied again",
				cached_map_file_unpacking_name[map_file_index]);
			cache_slot_source_forget(map_file_index);
			memset(&cached_map_file_get(map_file_index)->header, 0, sizeof(struct cache_file_header));
			cached_map_file_resize(map_file_index, 0);
		}
		cached_map_file_unpacking_name[map_file_index][0] = 0;
	}

	return;
}

/* port: whether a worker is unpacking a map (a10) */
boolean cache_files_map_unpacking(
	char const *map_name)
{
	cache_files_unpack_update();

	return cached_map_files_find_unpacking(map_name) != NONE;
}

/* port: how many maps the workers are unpacking */
long cache_files_unpacking_count(
	void)
{
	long count = 0;
	short map_file_index;

	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		count += cached_map_file_unpacking[map_file_index] != 0;
	}

	return count;
}

/* port: a map (a10) a worker is unpacking waited for, and taken back */
void cache_files_unpack_wait(
	char const *map_name)
{
	short map_file_index = cached_map_files_find_unpacking(map_name);

	if (map_file_index == NONE)
	{
		return;
	}
	map_unpack_wait(map_file_index);
	cache_files_unpack_update();

	return;
}
