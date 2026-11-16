/*
HOST_RENDER_REPLAY.H

The renderer's command stream replayed from a recording (HALO_RENDER_RECORD:
port/android/host/host_render_record.h), without the game: no guest, no
maps. What the replays share (host_render_replay.c): the recording read (its
blocks LZ4's as Apple's libcompression writes them, or stored), the guest's
memory put back before each record, the guest's side of the ring, the
frames measured, the frames' pictures compared, and the service
(--serve) a client drives (docs/replay_protocol.md). Each replay is a backend's: Metal's
(port/macos/tools/halo_replay.m), Vulkan's (port/linux/vulkan/tools/
halo_replay.c), which hands host_render_replay_main its platform.

halo_replay <file> [--frames a-b] [--window] [--unpaced] [--shots dir]
	[--shot-frames list] [--loop N] [--hitch ms] [--quiet] [--verbose]
halo_replay <file> --serve
halo_replay --compare <shots> <shots>

--frames a-b: the frames measured (those before are played unmeasured, the
stream's state being theirs; none after); --window: in a window (Metal's),
paced by the display unless --unpaced (offscreen, the frames are as fast as
the GPU); --shots dir: the back buffers of the frames of --shot-frames (as
HALO_RENDER_SHOT_FRAMES; all measured frames if not given) as PNGs, in
dir/loop<n> with --loop; --loop N: the whole replayed N times in one
process; --hitch ms: a hitch is an interval over this (default 12.5: 1.5
refreshes at 120 Hz) and twice the median. --compare a b: the PNGs of two
shot folders compared, pixel by pixel.

Prints each measured frame: its interval (the render thread's, from frame
end to frame end), the render thread's CPU time, the GPU time of the command
buffers completed in it (a frame or two late), its draws and the pipelines
made (compiles) in it; then a summary, hitches named.

--serve: requests as JSON lines on standard input, one JSON line answering
each on standard output (the log on standard error):

	{"cmd":"info"}  ("protocol": HOST_RENDER_REPLAY_PROTOCOL)
	{"cmd":"frame","n":N,"png":"<path>","ids":"<path>"}  (or "inline":true)
	{"cmd":"draws","n":N}
	{"cmd":"pick","n":N,"x":X,"y":Y}
	{"cmd":"thumbs","every":E,"width":W,"dir":"<folder>"}  (or "frames":[..],
		"inline":true)
	{"cmd":"cost","n":N,"repeats":R,"draws":[..]}  (or "all":true, "top":K;
		"cpu":true: the CPU's time too)

cost: frame N's GPU time, the frame's and its passes' (the backend's
timestamps at their boundaries) and its draws' (the frame played again with
each left out: what the frame takes without it, subtracted from what it
takes whole; or the backend's timestamps around each draw, where it has
them), each the median of R plays with its spread. The frame is played
again where it is, after itself (not from the recording's start): the
frame played after a cost is played from the start again, exactly.
With "cpu", the render thread's time on each record of the frame (the
records of a draw: those after the draw before it, to it), the backend's
calls of its driver among it (their time and count, by kind), each draw's,
pass's and record type's, the median of R plays without the GPU's
timestamps (and R more with the driver's calls watched).

A frame is played exactly: the frames before it are played first (from the
recording's start again for one before the frame last played; those more
than eight before it without the GPU's work of their draws, clears,
anti-aliasing, tone map and present, which nothing reads), and its picture
is the back buffer it presents, read before it is presented. Its
ID buffer (where the backend has one) gives each pixel the draws that
changed it, topmost first: a draw's number in its frame plus one (0: none),
a draw being a DRAW, DRAW_INDICES, DRAW_INDEXED or DRAW_IMMEDIATE record of
the frame, counted from its first. A draw counts where it changed the
pixel's value (an alpha-tested leaf's quad, not its cut-out corners).

A recording made with debug.render_marks (the stream's MARK records) says
what each draw belongs to: draws and pick give each draw's owner (its
"kind", "label", "id", "tags": object, model, shader, bitmap; a menu
widget's "widget" tag and "source", the generator's file and the source
attribute's text) and the game renderer's "category" it was drawn in; the
frame's view (its "map", "tick", "window", "camera": position, forward,
up, fov in radians; "viewport" in the back buffer's pixels; every view of
the frame in "views"). Without them a draw's kind is "other" and its label
its record's kind and primitive.
*/

#ifndef __HALO_HOST_RENDER_REPLAY_H
#define __HALO_HOST_RENDER_REPLAY_H

#include <stddef.h>
#include <stdint.h>

/* the service's protocol, in info's "protocol": raised when a request or a
reply changes so that a client reading the old one would misread it (an
added field does not raise it); docs/replay_protocol.md */
#define HOST_RENDER_REPLAY_PROTOCOL 2

/* the draws an ID buffer keeps for each pixel (pick's stack) */
#define HOST_RENDER_REPLAY_ID_DEPTH 8

/* a frame's measures, from the backend's statistics */
struct host_render_replay_statistics
{
	double cpu_ms, gpu_ms, compile_ms, shader_ms;
	uint32_t draws, compiles, shaders;
};

/* a frame's GPU time, as the backend measured it (cost_end) */
struct host_render_replay_pass_time
{
	/* what the pass drew (the backend's names: "world", "hud", ...), and
	its target's size */
	const char *kind;
	uint32_t width, height;
	/* its draws, and its time (ms): its part of the frame's, so that the
	passes' times sum to the frame's */
	uint32_t draws;
	double ms;
	/* when it started on the GPU (ms, from the frame's first timestamp) */
	double start_ms;
	/* its stages' own times (ms; they overlap other passes': the vertex
	stages of a tiler's passes run ahead), 0 if not known */
	double vertex_ms, fragment_ms;
};

struct host_render_replay_cost
{
	/* the time the GPU was busy with the frame (ms) */
	double frame_ms;
	const struct host_render_replay_pass_time *passes;
	uint32_t pass_count;
	/* each draw's pass (UINT32_MAX: none), by the draw's number in the
	frame */
	const uint32_t *draw_pass;
	uint32_t draw_count;
	/* each draw's own time (ms) where the backend measures it, by its
	method's name ("timestamps"); NULL none */
	const double *draw_ms;
	const char *draw_method;
	/* the backend's words on the measure's limits (or NULL) */
	const char *note;
};

struct host_render_replay_platform
{
	/* "metal", "vulkan" */
	const char *backend;
	/* the device made and the stream's backend set
	(host_render_stream_set_backend); window: in a window of width by
	height (only where window is supported); nonzero if made */
	int window_supported;
	int (*initialize)(int window, int width, int height);
	/* the window's pacing (only with a window) */
	void (*set_unpaced)(int unpaced);
	/* (the main thread, while the replay plays: the window's events) */
	void (*pump)(void);
	void (*shutdown)(void);
	/* (the render thread, as a frame ends) its measures since the last */
	void (*frame_statistics)(struct host_render_replay_statistics *statistics);
	/* (after a loop's report) the backend's own lines; NULL none */
	void (*report)(void);
	/* (the render thread) a colour target's pixels as an SDR display
	shows them: BGRA rows from the top, malloc'd; NULL if it has none */
	unsigned char *(*read_target)(uint32_t target, uint32_t *width, uint32_t *height);
	/* The ID buffer (NULL: none). ids_begin: the frame about to be played
	(the render thread, before its first record) keeps one for the target it
	presents, of depth draws a pixel; ids_draw: what the backend is
	handed from now on is the frame's index'th draw (UINT32_MAX: no draw,
	that one handed on); ids_read: (at the present, before
	it) the target's buffer, depth numbers a pixel, topmost first, each a
	draw's index plus one (0 none), rows from the top, malloc'd; ids_end:
	the frame's let go. */
	int (*ids_begin)(uint32_t target, uint32_t depth);
	void (*ids_draw)(uint32_t index);
	uint32_t *(*ids_read)(uint32_t target, uint32_t *width, uint32_t *height);
	void (*ids_end)(void);
	/* The GPU's time (NULL: none). cost_begin: (the render thread, before
	the frame's first record) the frame about to be played is timed;
	nonzero if it will be; cost_draw: as ids_draw; cost_end: (the service's
	thread, the frame played) waits for the frame's GPU work and gives its
	times, which are the backend's until the next cost_begin; nonzero if
	it has them. */
	int (*cost_begin)(void);
	/* (cost_end's draw_method when it has one, for info: NULL none) */
	const char *cost_draw_method;
	void (*cost_draw)(uint32_t index);
	int (*cost_end)(struct host_render_replay_cost *cost);
	/* The CPU's time (cost's "cpu"): what the render thread takes to play
	each record is the replay's own measure (host_render_replay_ticks before
	and after it); the backend's calls of its driver within it, where the
	backend counts them (call_kinds 0: none). calls_watch: (the render
	thread) from now each call it makes adds its time (in
	host_render_replay_ticks' ticks) and one to its kind's entry of ticks
	and counts (call_kinds + 1 entries each, the last the total; NULL: no
	more); call_kind_names: each kind's name ("draw", "bind_pipeline"...). */
	uint32_t call_kinds;
	const char *const *call_kind_names;
	void (*calls_watch)(uint64_t *ticks, uint64_t *counts);
};

int host_render_replay_main(int argc, char *argv[], const struct host_render_replay_platform *platform);

/* the replay's clock for the CPU's time: the processor's counter (x86's
time stamp counter, AArch64's virtual counter), whose ticks the cost
converts to milliseconds against the monotonic clock */
uint64_t host_render_replay_ticks(void);

/* --verbose: the host's information logged too */
extern int host_render_replay_verbose;

/* Apple's COMPRESSION_LZ4 (its blocks of LZ4's format, each with a header
of its own: "bv41" compressed, "bv4-" stored, "bv4$" the end) decoded into
to (at most to_size bytes); returns the bytes decoded, 0 if it is not that
or does not fit */
size_t host_render_replay_lz4_decode(unsigned char *to, size_t to_size, const unsigned char *from, size_t from_size);

/* base64 of size bytes into a malloc'd string */
char *host_render_replay_base64(const unsigned char *data, size_t size);

#endif
