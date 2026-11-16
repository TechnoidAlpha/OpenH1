/*
HOST_RENDER_RECORD.H

The renderer's command stream recorded to a file, and the frames' pictures
taken, as the render thread plays them (host_render_record.c): the fast
loop of the renderer's work, which a tool replays without the game
(port/macos/tools/halo_replay.m).

HALO_RENDER_RECORD=<file> records every record the decoder plays, in its
order, with the guest memory each one reads through guest addresses: the
inline data too large for the ring (in a buffer of its own), and what the
backend reads in place, which it names (host_render_record_note: the Xbox
textures' headers, palettes and texels, the high-res images' pixels) as it
reads it. Memory is stored by its content's hash, each content once.

HALO_RENDER_SHOTS=<directory> and HALO_RENDER_SHOT_FRAMES=<frames> (a list:
"300,600", "100-900/100") write the back buffer of those frames as the
frame presents it, read from the backend (host_render_record_set_shooter),
to <directory>/frame_<n>.png; a frame is counted from the stream's start
(the first is 0). The replay writes the same frames' the same way.

The file's format is this file's and the replay's alone (versioned:
HOST_RENDER_RECORD_VERSION); the stream's own is unchanged.
*/

#ifndef __HALO_HOST_RENDER_RECORD_H
#define __HALO_HOST_RENDER_RECORD_H

#include <stdint.h>

#include "xgpu_stream.h"

#define HOST_RENDER_RECORD_MAGIC 0x4c505248u /* "HRPL" */
/* 2: the stream's version 22, whose MARK records (debug.render_marks) the
replay's inspector reads; a recording of 1 (the stream's 21) plays as it
did, without marks. 3: the stream's 23, whose dynamic lights' cells come
as their changes (DYNAMIC_LIGHTS_CHANGE); a recording of 2 plays its
whole cells (DYNAMIC_LIGHTS_UPLOAD) as it did */
#define HOST_RENDER_RECORD_VERSION 3

/* the file: this header, then blocks, each a block header and its bytes:
together, the events, each an event header and its bytes (an event may
span blocks) */
#define HOST_RENDER_RECORD_BLOCK (8u << 20)

struct host_render_record_file
{
	uint32_t magic, version, header_size;
	uint32_t page_size;
	/* the most bytes a block holds */
	uint32_t block_size;
	/* the ring's header (guest address) and what it held when the stream
	began */
	uint32_t ring_address;
	struct xgpu_stream_ring ring;
};

/* a block's bytes (size of them) are stored as they are (stored = size),
or compressed (LZ4, Apple's libcompression's COMPRESSION_LZ4) into stored */
struct host_render_record_block
{
	uint32_t size, stored;
};

enum
{
	/* a content, the first time it is named: its hash (8 bytes), then
	the bytes */
	HOST_RENDER_EVENT_CONTENT = 1,
	/* guest memory the next record reads: host_render_record_chunk */
	HOST_RENDER_EVENT_CHUNK,
	/* a record as played: its position (4 bytes), then its bytes (a
	SKIP's header alone) */
	HOST_RENDER_EVENT_RECORD,
};

struct host_render_record_event
{
	uint32_t type, size;
};

struct host_render_record_chunk
{
	uint32_t address, size;
	uint32_t hash[2];
};

/* the decoder's (host_render_decode): each record, before and after the
backend plays it */
extern int host_render_record_active;
void host_render_record_before(const struct xgpu_stream_ring *ring, uint32_t position, const unsigned char *record,
	uint32_t type, uint32_t size);
void host_render_record_after(void);

/* the backend read bytes of guest memory at address (recorded with the
record played) */
void host_render_record_note(uint32_t address, uint32_t size);

/* the stream began, and ended (host_render_stream_open and _close): the
settings read, the file begun */
void host_render_record_start(const struct xgpu_stream_ring *ring);
void host_render_record_stop(void);

/* the backend's picture of a target, written to path as a PNG; nonzero if
written (the render thread, between records) */
typedef int (*host_render_shooter)(uint32_t target, const char *path);
void host_render_record_set_shooter(host_render_shooter shooter);
/* the frames to take (as HALO_RENDER_SHOT_FRAMES) into directory (NULL:
none) */
void host_render_record_shots(const char *directory, const char *frames);
/* nonzero if the frame's picture is taken */
int host_render_record_shot_wanted(uint32_t frame);
/* the frames the decoder has ended since the stream began (the replay
counts them again from 0 each time it plays the recording) */
uint32_t host_render_record_frame(void);
void host_render_record_set_frame(uint32_t frame);

/* the replay's: called with each record before it is played (its
position, type and bytes in the ring), to restore the memory it reads */
typedef void (*host_render_restorer)(uint32_t position, uint32_t type, const unsigned char *record);
void host_render_record_set_restorer(host_render_restorer restorer);
/* (the replay's too) called after each record is played, on the render
thread: with the restorer, what a record took to play is the time from one
to the other; NULL none */
void host_render_record_set_played(void (*played)(void));

/* the content's hash, as the file names it */
uint64_t host_render_record_hash(const void *data, uint32_t size);

#endif
