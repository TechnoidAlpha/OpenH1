/*
HOST_RENDER_STREAM.H

The host's half of the renderer's command stream (port/linux/src/xgpu_stream.h):
a render thread that plays the guest's records into a backend on the host
(Vulkan's, port/linux/vulkan; Metal's on macOS and iOS), and the guest's
imports that hand it the frames
(host_render_stream.c).

A backend is a table of functions, one for each kind of record, called on
the render thread in the records' order. Most take the record itself, in
place in the ring (4-byte aligned, valid during the call); guest addresses
in it are the host's at host_address() / host_pointer() (host.h). The
render thread keeps what the records change piece by piece (the textures'
stages, the vertex input, the draw state, the pipelines' keys) and hands
the backend the whole of it. An entry left NULL is skipped (a synchronous
record's answer is then zeros).

Inline data (in the ring, or in a buffer of its own when too large for it)
stays as it is until the backend's fence after it has been waited for: the
backend can draw from it in place, without a copy (the ring is one range
of host memory: ring and ring_size, given at open). That holds for the
vertex input's streamed data too, which the draws after set_vertices read:
the guest sends it again before a draw that a fence could come between.
What a backend keeps for later draws and frames (constants, uniforms, the
lights, mirror writes into its segments) it copies during the call. Data
named in place (textures, palettes, a synchronous record's) is the game's
and can change at any time, as on the Xbox.
*/

#ifndef __HALO_HOST_RENDER_STREAM_H
#define __HALO_HOST_RENDER_STREAM_H

#include <stdint.h>

#include "xgpu_stream.h"
#include "loading_presentation.h"

/* the vertex input a draw reads: its layout (as LAYOUT_DEFINE made it),
the streams present (bits of present) and the values of the inputs no
stream feeds (all 16, the last values sent) */
struct host_render_vertex_input
{
	const struct xgpu_stream_layout_define *layout;
	uint32_t present;
	struct xgpu_stream_vertex_stream streams[16];
	float attributes[16][4];
};

/* a stage's texture source and its levels */
struct host_render_texture_stage
{
	struct xgpu_stream_texture_stage stage;
	struct xgpu_stream_texture_level levels[XGPU_STREAM_MIP_LEVELS];
};

struct host_render_backend
{
	void *context;

	/* the stream begins: the ring's host address and size (to wrap in a
	GPU buffer: inline data's host address less ring is its offset), and
	where the visibility tests' counts go (the backend writes slot s's
	latest count to visibility[s] whenever it learns it, from any thread) */
	void (*open)(void *context, void *ring, uint32_t ring_size, volatile uint32_t *visibility);

	/* the GPU's progress: fence marks the work recorded so far and sends
	it to the GPU (Metal: commits the command buffer; GL: glFenceSync and
	a flush), returning a number that grows; wait returns once the GPU has
	done the work of that fence and those before. Called on the render
	thread at each frame's end, and whenever the guest waits for room. */
	uint64_t (*fence)(void *context);
	void (*wait)(void *context, uint64_t fence);
	/* Optional, nonblocking completion query. A backend providing it must
	wake the render thread with host_render_stream_gpu_completed after
	publishing completion, without holding locks this query acquires. */
	int (*fence_ready)(void *context, uint64_t fence);

	void (*initialize)(void *context, struct xgpu_stream_capabilities *capabilities);
	void (*invalidate)(void *context);
	void (*flush)(void *context);
	void (*anti_aliasing_prepare)(void *context, uint32_t smaa);
	void (*anti_alias)(void *context, const struct xgpu_stream_anti_alias *record);
	/* display.hdr (xgpu_hdr.h): its state, and a 3D view's tone map */
	void (*hdr)(void *context, const struct xgpu_stream_hdr *record);
	void (*tone_map)(void *context, const struct xgpu_stream_tone_map *record);
	/* display.ambient_occlusion over a 3D view (none: not drawn) */
	void (*ambient_occlusion)(void *context, const struct xgpu_stream_ambient_occlusion *record);
	/* display.picture_position: the picture n pixels from the top of a
	taller window (n + 1) or centered (0) */
	void (*picture_top)(void *context, uint32_t top);
	/* display.gamma: the setting (a float's bits), from the next present on */
	void (*gamma)(void *context, uint32_t gamma);

	void (*target_create)(void *context, const struct xgpu_stream_target_create *record);
	void (*target_delete)(void *context, uint32_t target);
	void (*target_multisample)(void *context, const struct xgpu_stream_target_multisample *record);
	void (*target_resolve)(void *context, uint32_t target);
	void (*set_targets)(void *context, const struct xgpu_stream_set_targets *record);
	void (*clear)(void *context, const struct xgpu_stream_clear *record);
	void (*clear_target)(void *context, const struct xgpu_stream_clear_target *record);
	/* pixels: host_pointer(record->pixels), record->size bytes; returns
	nonzero if written */
	int (*read_target)(void *context, const struct xgpu_stream_read_target *record);

	void (*set_textures)(void *context, const struct host_render_texture_stage stages[4]);

	void (*set_constants)(void *context, const struct xgpu_stream_constants *record);
	void (*vertex_program)(void *context, const struct xgpu_stream_vertex_program *record);
	void (*vertex_lighting)(void *context, const struct xgpu_stream_vertex_lighting *record);
	/* a pipeline id's key, as it is defined (the backend may make it
	now, or at its first use) */
	void (*pipeline_define)(void *context, const struct xgpu_stream_pipeline_define *record);
	/* the pipeline the draws use from now on; nonzero if it is made (its
	state goes back to the guest: XGPU_STREAM_PIPELINE_*) */
	int (*pipeline)(void *context, uint32_t id, const struct xgpu_stream_pipeline_define *definition);
	/* the latest of each, whichever changed */
	void (*draw_state)(void *context, const struct xgpu_stream_dynamic_state *dynamic,
		const struct xgpu_stream_draw_uniforms *uniforms, uint32_t uniforms_serial);

	void (*mirror_write)(void *context, const struct xgpu_stream_mirror_write *record);
	void (*set_vertices)(void *context, const struct host_render_vertex_input *input);
	void (*draw)(void *context, const struct xgpu_stream_draw *record);
	void (*draw_indices)(void *context, const struct xgpu_stream_draw_indices *record);
	void (*draw_indexed)(void *context, const struct xgpu_stream_draw_indexed *record);
	/* attributes: the 16 registers' values outside the vertices (the last
	immediate draws sent: set_vertices' are its own) */
	void (*draw_immediate)(void *context, const struct xgpu_stream_draw_immediate *record,
		const float (*attributes)[4]);

	void (*visibility_begin)(void *context);
	void (*visibility_end)(void *context, const struct xgpu_stream_visibility_end *record);
	void (*visibility_frame)(void *context);

	void (*present)(void *context, uint32_t back_buffer);
	void (*frame_end)(void *context);
	/* Independent loading content; returns its refresh period in ns. */
	uint64_t (*loading)(void *context, const struct loading_presentation_frame *frame);
	/* Current ownership, independent of the callback's retry interval. */
	int (*loading_owned)(void *context);
	/* Copy 640x480 RGBA before returning; NULL clears the owned picture. */
	void (*loading_art)(void *context, const unsigned char *rgba);
	/* The menu's scene (loading_scene.h), unchecked bytes the backend
	validates; copied before returning, none clears it. */
	void (*loading_scene)(void *context, const void *data, uint32_t bytes);
	/* the load's progress, 0 to 1 (unchecked: the backend clamps it) */
	void (*loading_progress)(void *context, float progress);

	/* nonzero if the block and cells are made */
	int (*dynamic_lights_prepare)(void *context);
	void (*dynamic_lights_upload)(void *context, const struct xgpu_stream_dynamic_lights_upload *record);
	void (*dynamic_lights_halos)(void *context, const struct xgpu_stream_dynamic_lights_halos *record);

	int (*vr_supported)(void *context);
	/* PREPARED: waits until the shaders and pipelines of the draws so far are
	made (where the backend makes them apart from its draws), and returns
	the milliseconds waited (NULL: 0) */
	uint32_t (*prepared)(void *context);
	/* the process is ending (host_exit, on the thread that ends it; NULL:
	nothing to do): what the backend keeps across runs written */
	void (*exiting)(void *context);
	/* PREPARE: the draws until PREPARED are for their shaders and pipelines
	alone (not seen) */
	void (*prepare)(void *context);
	void (*vr_initialize)(void *context, struct xgpu_stream_vr_info *info);
	void (*vr_eyes)(void *context, const struct xgpu_stream_vr_eyes *record);
	void (*vr_pass)(void *context, int32_t pass);
	void (*vr_groups_reset)(void *context);
	void (*vr_timing)(void *context, int32_t mark);
	void (*vr_compose)(void *context, const struct xgpu_stream_vr_frame *frame);
	/* the frame to OpenXR: the place to take the newest head pose */
	void (*vr_submit)(void *context, const struct xgpu_stream_vr_submit *record);
	/* the game's VR state for the next composition (xgpu_stream.h) */
	void (*vr_state)(void *context, const struct xgpu_stream_vr_state *record);
	/* what vr.c sets in the backend as it draws (vr.h): the eyes'
	transforms, the screen's corrections (with what the late latch moves them
	by: xgpu_stream.h), the sky's depth clamping, the feed (7 values) and a
	tint (3) */
	void (*vr_eye_transforms)(void *context, const struct xgpu_stream_vr_eye_transforms *record);
	void (*vr_screen_corrections)(void *context, const struct xgpu_stream_vr_screen_corrections *record);
	void (*vr_sky)(void *context, int32_t sky);
	void (*vr_eye_feed)(void *context, const float feed[7]);
	void (*vr_eye_tint)(void *context, const float tint[3]);
};

/* the backend the streams play into: set by the platform's host before
the guest starts (macOS: the Metal backend). Without one,
host_render_stream_open fails and the guest keeps its own renderer. */
void host_render_stream_set_backend(const struct host_render_backend *backend);

/* the screenshot key's file (XGPU_STREAM_SCREENSHOT): writes the pixels
(BGRA rows from the top) as a PNG in directory (made if it is missing), and
the file's name into name (name_size bytes); nonzero if written, else the
reason in name. Set by the platform's host with its backend
(host_screenshot_write); without one the record's answer is a failure. */
typedef int (*host_render_screenshot_writer)(unsigned char *bgra, uint32_t width, uint32_t height, const char *directory,
	char *name, uint32_t name_size);
void host_render_stream_set_screenshot_writer(host_render_screenshot_writer writer);
/* host_screenshot.c's, with SDL: YYYY-MM-DD_HH.MM.SS.png in local time
(host_screenshot_name: the how manyth of its second, collision, from 0,
adds _2, _3 and on) */
int host_screenshot_write(unsigned char *bgra, uint32_t width, uint32_t height, const char *directory, char *name,
	uint32_t name_size);
void host_screenshot_name(char *name, uint32_t name_size, int year, int month, int day, int hour, int minute,
	int second, unsigned collision);

/* GPU completion may release ring space even when no more records arrive.
Called from any thread, after publishing the backend's completed fence. */
void host_render_stream_gpu_completed(void);

/* ---------- the guest's imports (guest_host.h) */

/* the stream at the guest's ring header (xgpu_stream_ring): checks it,
starts the render thread; 0, or -1 (no backend, a header of another
version, a ring the wrong size or out of place) */
int host_render_stream_open(uint32_t ring);
/* the records up to the header's committed are written: XGPU_STREAM_SUBMIT_* */
void host_render_stream_submit(uint32_t flags);
/* returns once the host has released up to position */
void host_render_stream_wait(uint32_t position);
/* ends the render thread after the records submitted (at exit) */
void host_render_stream_close(void);
/* tells the backend the process is ending (host_exit) */
void host_render_stream_exiting(void);
/* Render thread only, also while asynchronous preparation is pending. */
void host_render_stream_loading_service(void);
int host_render_stream_loading_holds(void);

/* runs function(argument) on the render thread, after the records
submitted so far, and returns once it has (the backend's context is current
there: OpenXR's session is made so); on the calling thread while no stream
is open */
void host_render_stream_call(void (*function)(void *argument), void *argument);

/* the guest's ring header while a stream is open, else NULL: its
statistics, for the host's logs (read as they are, racing with their
writers) */
const struct xgpu_stream_ring *host_render_stream_ring(void);

/* ---------- pacing by the display (frame_pacing.h)

A backend that shows frames on a display it knows the refreshes of paces
the game by it: host_render_stream_display_paced(1), and then, for each
frame it ends, once, host_render_stream_frame_taken when its display takes
it (shows it, or drops it; at once for a frame it has nothing to show of).
The game then starts a frame only while at most FRAME_PACING_QUEUED of its
frames wait for the display. host_render_stream_frame_shown gives the time
the frame reached the screen with its input time
(host_render_stream_frame_input, while the backend ends it), for
host_render_stream_latency: nanoseconds of CLOCK_MONOTONIC, the game's. */
void host_render_stream_display_paced(int paced);
/* display.hdr: the display shows HDR (xgpu_hdr.h's XGPU_HDR_DISPLAY_HEADROOM),
its headroom (1 for none: now, or the display's potential, its peak), and
its SDR white in nits (the system's,
white_reported, or the one the encoding takes where it gives none), for the
guest to read between frames (the ring's display_hdr, display_headroom,
display_white and display_white_reported: any thread, at any time, before
or while a stream is open) */
void host_render_stream_display_hdr(int hdr, float headroom, float white_nits, int white_reported);
uint64_t host_render_stream_frame_input(void);
void host_render_stream_frame_taken(void);
/* the display has latched a refresh's frame (taken one or not) at latch;
its refreshes are period apart: the game then starts each frame its
budget before the latch it is for (frame_pacing_start_time) */
void host_render_stream_display_latched(uint64_t latch, uint64_t period);
/* sleeps until a time of CLOCK_MONOTONIC, woken within a millisecond or so
(on Apple's systems a time constraint thread while it sleeps: an ordinary
timed wait there can end tens of milliseconds late) */
void host_render_stream_sleep_until(uint64_t when);
void host_render_stream_frame_shown(uint64_t input, uint64_t shown);
/* A backend that knows when each frame reached the screen (Vulkan's,
xgpu_vulkan_pacing.c) says so in place of host_render_stream_frame_taken,
host_render_stream_display_latched and host_render_stream_frame_shown:
once for each frame it ends, in order, its input time, when its work was
done (0: not known), when the display latched it (took it for the refresh
it is shown at, which a compositor does some milliseconds before: 0, not
known, then shown's), when it was shown (0: not shown, or not known) and
the refreshes' period (0: not known). The stream then learns the margin it
starts the frames with (frame_pacing.h's frame_pacing_margin_missed), by
the refresh each was started for. Before
it paces the game (host_render_stream_display_paced(1)) it counts the
frames played so far as taken (host_render_stream_frames_taken_reset), and
the next frame it ends is its to say. */
void host_render_stream_frames_taken_reset(void);
void host_render_stream_frame_displayed(uint64_t input, uint64_t finished, uint64_t latched, uint64_t shown,
	uint64_t period);
/* (any thread) a frame's work done, its GPU's included, at finished: its
cost, from its input read (frame_pacing_cost_add), with the render
thread's own */
void host_render_stream_frame_finished(uint64_t input, uint64_t finished);
/* the pacing's counts since the last call: frames shown at the refresh
they were started for (kept) or after it (missed; overran: of those, the
frames whose work ran past the margin), frames that started after their
time (late_starts: the game or the frame before ran long); and the margin
and budget now, in nanoseconds */
struct host_render_pacing_statistics
{
	uint32_t kept, missed, overran, late_starts;
	uint64_t margin, budget;
};
void host_render_stream_pacing_statistics(struct host_render_pacing_statistics *statistics);
/* (the render thread, as it plays a frame's records) the frame is ready
for the display now: its cost (frame_pacing_cost_add) ends here, not as its
records end (a backend that then waits for the display to take it) */
void host_render_stream_frame_ready(void);
/* the input to screen times since the last call: median and 95th
percentile in milliseconds; returns how many */
uint32_t host_render_stream_latency(float *median, float *high);
/* the render thread is playing the stream (1), or waits for more (0) */
int host_render_stream_playing(void);

/* ---------- for tests: the decoder alone, on the calling thread

Plays the records of [from, to) of a ring (the header's buffer and size)
into the backend; returns the position reached (to, or where a malformed
record stopped it, with ring->error set). */
struct host_render_decoder;
struct host_render_decoder *host_render_decoder_new(const struct host_render_backend *backend,
	struct xgpu_stream_ring *ring);
uint32_t host_render_decode(struct host_render_decoder *decoder, uint32_t from, uint32_t to);
void host_render_decoder_free(struct host_render_decoder *decoder);

#endif
