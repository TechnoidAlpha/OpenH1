"""Held guest frames retire GPU work without presenting over loading art."""
from pathlib import Path
import subprocess

from test_load_trace import build


ROOT = Path(__file__).resolve().parents[1]


def test_held_frames_retire_without_acquiring_or_presenting(tmp_path):
    core = (ROOT / "port/linux/vulkan/xgpu_vulkan.c").read_text()
    core = core[core.index("void xgpu_vulkan_frame_end("):
                core.index("void xgpu_vulkan_statistics_peek(")]
    stream = (ROOT / "port/linux/vulkan/xgpu_vulkan_stream.c").read_text()
    # The rest of frame_end only prints aggregate statistics every 600 frames.
    stream = stream[stream.index("static void stream_present("):
                    stream.index('\tif (stream.statistics)')] + "}\n"
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define VK_NULL_HANDLE 0
#define VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT 1
#define VK_ACCESS_SHADER_WRITE_BIT 2
#define VK_PIPELINE_STAGE_HOST_BIT 4
#define VK_ACCESS_HOST_READ_BIT 8
#define VK_PRESENT_MODE_FIFO_KHR 2
struct xgpu_vulkan_image { int value; } image;
struct query { unsigned counter_count; uint64_t serial; } query;
static struct {
    int gpu_time_armed, frame_timing, frame_ending, deferred, gpu_time_failed;
    struct query *queries;
    uint64_t frame, gpu_nanoseconds;
    double frame_ended;
    struct { unsigned frames, gpu_frames, frames_timed; double gpu_milliseconds, frame_seconds; } statistics;
    struct { int gpu_timestamps; } capabilities;
    int interval, present_mode;
} vk;
/* (debug.frame_timing's GPU times and the draws' times: off here) */
static struct { int pending; uint64_t serial; } draw_times;
static void draw_times_frame_end(void) {}
static void draw_times_collect(void) {}
static struct { int pending; uint64_t serial; } pass_times;
static void pass_times_frame_end(void) {}
static void pass_times_collect(void) {}
/* (the replay's cost timing: off here) */
static void timing_frame_end(void) {}
static void xgpu_vulkan_gpu_time_end(void) { assert(0); }
static void xgpu_vulkan_gpu_time_begin(void) { assert(0); }
static int xgpu_vulkan_gpu_time_read(double *milliseconds) { (void)milliseconds; assert(0); return 0; }
static double seconds_now(void) { return 0.0; }
static unsigned acquisitions, presentations, blank, submissions, recordings;
static unsigned barriers, completions, garbage, texture_frames;
static uint64_t next_serial, published_frame;
static int held;
static int main_commands(void) { return 7; }
static void barrier(int command, int a, int b, int c, int d)
{ assert(command == 7 && a == 1 && b == 2 && c == 4 && d == 8); barriers++; }
static uint64_t present(struct xgpu_vulkan_image *shown, int frame)
{
    acquisitions++; presentations++;
    if (!shown) blank++;
    else assert(shown == &image);
    assert(frame == 1);
    return ++next_serial;
}
static unsigned paced_updates, unshown;
static void frame_pacing_update(void) { paced_updates++; }
static void frames_ahead_update(int present_window) { (void)present_window; }
static void xgpu_vulkan_pacing_unshown(void) { unshown++; }
static void recording_begin(void) { recordings++; }
static uint64_t submit(int acquired, int shown)
{ assert(!acquired && !shown); submissions++; return ++next_serial; }
static void queries_publish(uint64_t frame) { published_frame = frame; }
static void completed_serial(void) { completions++; }
static void garbage_collect(void) { garbage++; }
''' + core + r'''
struct stream_target { struct xgpu_vulkan_image *image; } target = { &image };
struct xgpu_vulkan_texture_statistics { unsigned made, remade, misses; uint64_t bytes; };
static struct {
    uint32_t back_buffer;
    int trace_frames, after_frames;
    struct xgpu_vulkan_texture_statistics after_textures;
    unsigned pipeline_misses;
} stream;
static struct { int twins_made; } prepass;
static void host_load_trace(const char *name, const char *format, ...) { (void)name; (void)format; assert(0); }
static void xgpu_vulkan_textures_statistics_peek(struct xgpu_vulkan_texture_statistics *value) { (void)value; }
static void host_render_stream_gpu_time(uint64_t nanoseconds) { (void)nanoseconds; assert(0); }
static int host_render_stream_loading_holds(void) { return held; }
static struct stream_target *target_get(uint32_t number)
{ assert(number <= 1); return number ? &target : NULL; }
static void target_resolve(struct stream_target *value) { (void)value; }
static void xgpu_vulkan_textures_frame_end(void) { texture_frames++; }
static void world_make_some(void) {}
''' + stream + r'''
int main(void)
{
    vk.frame = 2;
    vk.gpu_time_armed = 1;
    query.counter_count = 1;
    vk.queries = &query;
    held = 1;
    stream_present(NULL, 1);
    stream_frame_end(NULL);
    assert(!acquisitions && !presentations && !blank);
    assert(recordings == 1 && submissions == 1 && barriers == 1);
    assert(query.serial == 1 && !vk.queries && published_frame == 0);
    assert(vk.frame == 3 && vk.statistics.frames == 1 && vk.gpu_time_armed);
    assert(completions == 1 && garbage == 1 && texture_frames == 1);
    /* A normal NULL image still presents black (startup/VR semantics). */
    held = 0;
    stream_present(NULL, 0);
    stream_frame_end(NULL);
    assert(acquisitions == 1 && presentations == 1 && blank == 1);
    assert(submissions == 1 && vk.frame == 4 && vk.gpu_time_armed);
    stream_present(NULL, 1);
    stream_frame_end(NULL);
    assert(acquisitions == 2 && presentations == 2 && blank == 1);
    assert(submissions == 1 && vk.frame == 5 && vk.statistics.frames == 3);
    assert(completions == 3 && garbage == 3 && texture_frames == 3);
    /* (display.low_latency: each frame's pacing looked at, and the held
    frame, which shows nothing, taken at once) */
    assert(paced_updates == 3 && unshown == 1);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=10)
