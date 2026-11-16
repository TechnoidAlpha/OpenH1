"""Asset-free probes of the production preparation cache and compiler worker."""
from pathlib import Path
import re
import subprocess

from test_load_trace import build


ROOT = Path(__file__).resolve().parents[1]


def test_loading_scheduler_keeps_deadlines_and_work_after_slow_callbacks(tmp_path):
    text = (ROOT / "port/android/host/host_render_stream.c").read_text()
    functions = text[text.index("static uint64_t monotonic_nanoseconds(void);"):text.index("struct host_render_decoder\n")]
    source = r'''
#include <assert.h>
#include <string.h>
#include "loading_presentation.h"
struct host_render_backend {
    void *context;
    uint64_t (*loading)(void *, const struct loading_presentation_frame *);
};
static uint64_t clock_now = 100000000, duration, interval = 16000000;
static unsigned calls;
static uint64_t monotonic_nanoseconds(void) { return clock_now; }
static void host_load_trace(const char *phase, const char *format, ...) {
    (void)phase; (void)format;
}
static void host_stall_loading(int active) { (void)active; }
static uint64_t callback(void *context, const struct loading_presentation_frame *frame) {
    (void)context;
    if (frame->active && !frame->ready) { clock_now += duration; calls++; }
    return interval;
}
''' + functions + r'''
/* the stream is played to its end: the picture waits only for its interval */
static int loading_stream_waits(void) { return 0; }
int main(void) {
    struct host_render_backend backend = {0, callback};
    uint64_t start;
    duration = 2000000;
    start = clock_now;
    loading_set(&backend, 1);
    assert(calls == 1 && loading.next == start + interval);
    host_render_stream_loading_service();
    assert(calls == 1);
    clock_now = loading.next;
    duration = 23000000;
    host_render_stream_loading_service();
    assert(loading.next == clock_now + 5000000);
    clock_now = loading.next;
    duration = 26000000;
    host_render_stream_loading_service();
    assert(loading.next == clock_now + 2000000);
    clock_now = loading.next;
    duration = 23000000;
    host_render_stream_loading_service();
    assert(loading.next == clock_now + 2000000);
    clock_now = loading.next;
    duration = 40000000;
    host_render_stream_loading_service();
    assert(loading.next == clock_now + 1000000);
    loading_set(&backend, 0);
    duration = 23000000;
    loading_set(&backend, 1);
    assert(loading.cost_index == 1 && loading.next == clock_now + 5000000);
    loading_set(&backend, 0);
    interval = 1000000;
    duration = 2000000;
    loading_set(&backend, 1);
    assert(loading.next == clock_now + 1000000);
    loading_set(&backend, 2);
    assert(!host_render_stream_loading_holds());
    start = calls;
    clock_now += 100000000;
    host_render_stream_loading_service();
    assert(calls == start);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_loading_retirement_repeats_inside_one_epoch(tmp_path):
    text = (ROOT / "port/linux/src/sdl_platform.c").read_text()
    functions = text[text.index("void halo_loading_begin(void)"):text.index("static struct platform_input_state input_state;")]
    # (the caption's size, as loading_presentation.h sets it)
    scale = re.search(r"#define LOADING_PRESENTATION_CAPTION_SCALE (\d+)",
                      (ROOT / "port/linux/src/loading_presentation.h").read_text()).group(1)
    source = f"#define LOADING_PRESENTATION_CAPTION_SCALE {scale}\n" + r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int platform_window = 1, loading_presentation_active;
static int epochs, retired, borrowed, storage;
static int xgpu_backend_loading_owned(void) { return 0; }
static void halo_load_trace(const char *phase, const char *detail) {
    (void)phase; (void)detail;
}
static int scenes, fades, chanting, follows = -1, captions, preparing;
/* (read before each loading screen takes the screen) */
static void halo_loading_scene(void) { assert(scenes == epochs); scenes++; }
/* (the menus' pictures being prepared: menu_pictures_render.c) */
static int menu_pictures_preparing(void) { return preparing; }
static void halo_loading_fade_begin(void) { fades++; }
static void xgpu_backend_loading_progress(float progress) { (void)progress; }
static void xgpu_backend_loading(int phase) { if (phase == 1) epochs++; }
static void xgpu_backend_loading_mode(int follow) { follows = follow; }
static void xgpu_backend_loading_caption(const unsigned char *coverage, unsigned long width, unsigned long height) {
    assert(!coverage == !width && !width == !height); captions += coverage != NULL;
}
static void platform_loading_chant(int playing, float progress) { (void)progress; chanting = playing; }
static int flushes;
static void xgpu_backend_flush(void) { flushes++; }
static unsigned long long SDL_GetTicksNS(void) { return 0; }
static unsigned char *text_hires_line(const char *tag, const char *text, float cap, int *width, int *height) {
    (void)tag; (void)cap; *width = (int)strlen(text) * 4; *height = 8; return calloc((size_t)*width * 8, 1);
}
static void xgpu_backend_consume(void) {
    assert(!borrowed || storage == borrowed);
    borrowed = 0; retired++;
}
''' + functions + r'''
int main(void) {
    halo_loading_begin();
    /* (a map load's sweep loops; the chant plays under it) */
    assert(follows == 0 && chanting);
    storage = borrowed = 1;
    halo_loading_begin();
    /* the menu's scene read once, before the presenter takes the screen */
    assert(epochs == 1 && borrowed == 1 && scenes == 1);
    halo_loading_retire();
    assert(!borrowed && retired == 1);
    storage = borrowed = 2;
    halo_loading_retire();
    assert(!borrowed && retired == 2 && epochs == 1);
    halo_loading_ready();
    assert(fades == 1 && !chanting);
    /* (the port's own waits are not shown over a map load's) */
    halo_loading_screen_begin(1);
    assert(epochs == 1 && follows == 0);
    halo_loading_end();
    /* a wait of the port's own: the sweep following its progress, the
    chant, a caption set once for each change, and a map load's end does
    not take its screen */
    halo_loading_screen_begin(1);
    assert(epochs == 2 && follows == 1 && chanting && loading_presentation_active == 3);
    halo_loading_screen_progress(0.25f, "Preparing menu pictures");
    halo_loading_screen_progress(0.5f, "Preparing menu pictures");
    /* (sent at once, the game drawing nothing to carry them: a new caption
    always, else at most 30 times a second) */
    assert(captions == 1 && flushes == 1);
    halo_loading_end();
    assert(loading_presentation_active == 3);
    halo_loading_screen_progress(1.0f, NULL);
    halo_loading_screen_end();
    assert(!loading_presentation_active && !chanting && follows == 0);
    platform_window = 0;
    storage = borrowed = 3;
    halo_loading_retire();
    assert(!borrowed && retired == 3);
    /* while the menus' pictures are prepared: their pass's one loading
    screen stays over each map's load, its old draws retired all the same,
    and its progress sent as it changes (no game frame carries it) */
    platform_window = 1;
    halo_loading_screen_begin(1);
    preparing = 1;
    storage = borrowed = 4;
    halo_loading_begin();
    assert(!borrowed && retired == 4 && epochs == 3 && loading_presentation_active == 3);
    {
        int before = flushes;

        halo_loading_screen_progress(0.5f, "Preparing menu pictures 4 of 11");
        assert(flushes == before + 1);
    }
    halo_loading_end();
    assert(loading_presentation_active == 3);
    preparing = 0;
    halo_loading_screen_end();
    assert(!loading_presentation_active);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)
    cache = (ROOT / "source/cache/cache_files.c").read_text()
    tags = cache[cache.index("void scenario_tags_unload("):]
    bsp = cache[cache.index("void scenario_structure_bsp_unload("):]
    assert tags.index("halo_loading_retire();") < tags.index("hud_hires_tags_unloaded();")
    assert bsp.index("halo_loading_retire();") < bsp.index("structure_bsp_header_deregister_vertex_buffers(")


def test_preparation_cpu_clock_can_be_unavailable(tmp_path):
    text = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    sample = text[text.index("static double preparation_time(void)"):text.index("static void preparation_time_add(")]
    total = text[text.index("double halo_thread_cpu_seconds(void)"):text.index("double halo_gpu_prepared(void)")]
    source = '''
#include <assert.h>
#include <time.h>
#undef CLOCK_THREAD_CPUTIME_ID
''' + sample + total + '''
int main(void) {
    assert(preparation_time() == -1.0);
    assert(halo_thread_cpu_seconds() == -1.0);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_sky_preparation_uses_and_restores_sky_context(tmp_path):
    text = (ROOT / "port/linux/game/render_enumerate.c").read_text()
    function = text[text.index("static void skies_draw(void)"):text.index("void render_enumerate_models(void)")]
    source = r'''
#include <assert.h>
#include <stddef.h>
#define HALO_VR 1
#define TRUE 1
#define FALSE 0
#define NONE -1
#define SKY_DEFINITION_TAG 7
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
#define FLAG(a) (1 << (a))
#define _render_model_immediate_bit 3
struct tag_iterator { int next; };
struct tag_reference { long index; };
static struct tag_reference tags[] = {{7}, {NONE}, {8}};
int model_states[2];
static int sky, changes, draws;
static void vr_render_sky(int active) { sky = active; changes++; }
static void tag_iterator_new(struct tag_iterator *i, int tag) {
    assert(tag == SKY_DEFINITION_TAG); i->next = 0;
}
static long tag_iterator_next(struct tag_iterator *i) {
    return i->next < 3 ? i->next++ : NONE;
}
static void *tag_get(int tag, long index) {
    assert(tag == SKY_DEFINITION_TAG); return &tags[index];
}
static void model_draw(long model, short state, void *effect, int permutation, unsigned long flags) {
    assert(sky && (model == 7 || model == 8));
    assert(state >= 0 && state < 2 && !effect && !permutation);
    assert(flags == FLAG(_render_model_immediate_bit)); draws++;
}
''' + function + r'''
int main(void) {
    skies_draw();
    assert(!sky && changes == 2 && draws == 4);
    tags[0].index = tags[2].index = NONE;
    skies_draw();
    assert(!sky && changes == 4 && draws == 4);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_preparation_cache_compares_layout_resources_and_context(tmp_path):
    text = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    code = text[text.index("#define PREPARATION_BUCKETS"):text.index("/* the visibility tests are counted")]
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define XGPU_VERTEX_ATTRIBUTE_COUNT 16
struct xgpu_pipeline_key { unsigned long state[32]; };
struct xgpu_dynamic_state { unsigned long state[16]; };
struct xgpu_texture_source { unsigned long state[32]; };
struct xgpu_vertex_element { unsigned long format, stream, offset; };
struct surface { unsigned long Data, Format, Size; };
struct declaration { struct xgpu_vertex_element elements[16]; };
static struct {
    unsigned long immediate_written;
    struct declaration *vertex_shader;
    struct { unsigned long data, stride; } streams[16];
    struct surface *render_target, *depth_stencil;
    void *textures[4];
} device;
static int consumed, checkpointed;
static void xgpu_backend_consume(void) { consumed++; }
/* (the settings a draw is journaled under alike: none here) */
static void journal_settings_blind(unsigned long words[3]) { words[0] = words[1] = words[2] = 0; }
static void checkpoint(void *const *textures)
{
    assert(consumed == checkpointed + 1);
    assert(textures[0] == device.textures[0]);
    checkpointed++;
}
''' + code + r'''
int main(void)
{
    /* (the pipeline key's number: xgpu_backend_pipeline_number) */
    unsigned long pipeline = 1;
    struct xgpu_dynamic_state dynamic = {{0}};
    struct declaration declaration;
    struct surface target = {1, 2, 3};
    unsigned long index;
    memset(&declaration, 0, sizeof(declaration));
    device.vertex_shader = &declaration;
    device.render_target = &target;
    preparation.active = preparation.trackable = TRUE;
    device.textures[0] = &target;
    halo_gpu_preparation_checkpoint_set(16, checkpoint);
    halo_gpu_preparation_loaded(8);
    preparation_draw_complete();
    assert(!consumed && !checkpointed);
    halo_gpu_preparation_loaded(8);
    assert(!consumed && !checkpointed);
    preparation.preserved++;
    preparation_draw_complete();
    assert(!consumed && !checkpointed);
    preparation.preserved--;
    preparation_draw_complete();
    assert(consumed == 1 && checkpointed == 1);
    preparation_draw_complete();
    assert(consumed == 1 && checkpointed == 1);
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    assert(preparation_seen(pipeline, &dynamic, FALSE, 4));
    /* Force equal stored hash but unequal bytes; never accept a hash alone. */
    for (index = 0; index < PREPARATION_BUCKETS; index++)
        if (preparation.entries[index]) preparation.entries[index]->pipeline += 0x10000;
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    assert(preparation_seen(pipeline, &dynamic, FALSE, 4));
    declaration.elements[0].format++;
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    device.streams[0].data = 16;
    device.streams[0].stride = 32;
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    preparation.key.texture_generation[0]++;
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    preparation.key.palette_generation[0]++;
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    halo_gpu_preparation_sky(1);
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    target.Format++;
    assert(!preparation_seen(pipeline, &dynamic, FALSE, 4));
    assert(preparation_seen(pipeline, &dynamic, FALSE, 4));
    device.immediate_written = 3;
    assert(!preparation_seen(pipeline, &dynamic, TRUE, 4));
    device.immediate_written = 7;
    assert(!preparation_seen(pipeline, &dynamic, TRUE, 4));
    preparation.trackable = FALSE;
    assert(!preparation_seen(pipeline, &dynamic, TRUE, 4));
    preparation.trackable = TRUE;
    /* More unique entries than buckets: collision never means equal. */
    for (index = 1; index < PREPARATION_BUCKETS * 3; index++)
    {
        pipeline = index + 1;
        assert(!preparation_seen(pipeline, &dynamic, TRUE, 4));
        assert(preparation_seen(pipeline, &dynamic, TRUE, 4));
    }
    preparation_clear();
    preparation_time_add(0, preparation_time());
    assert(!preparation_seen(pipeline, &dynamic, TRUE, 4));
    preparation.active = FALSE;
    assert(!preparation_seen(pipeline, &dynamic, TRUE, 4));
    preparation_clear();
    return 0;
}
'''
    binary = build(tmp_path, source)
    subprocess.run([str(binary)], check=True, timeout=15)


def test_pipeline_worker_presents_during_compile_and_joins(tmp_path):
    text = (ROOT / "port/linux/vulkan/xgpu_vulkan.c").read_text()
    # (up to the end of pipeline_compiler_stop: what follows it, before
    # pipeline_build, is the pipelines' own, which the probe does not use)
    code = text[text.index("/* Preparation queues owned native descriptions."):
                text.index("\n}\n", text.index("static void pipeline_compiler_stop(")) + 3]
    entries = text[text.index("struct pipeline_entry\n"):text.index("struct shader_entry\n")]
    accessors = text[text.index("static struct pipeline_entry *pipeline_find_apart("):
                     text.index("uint64_t xgpu_vulkan_pipeline_count(")]
    source = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
typedef uintptr_t VkPipeline;
typedef int VkResult;
#define VK_NULL_HANDLE 0
#define PIPELINE_BUCKETS 8
struct xgpu_vulkan_pipeline_description { uintptr_t value; };
''' + entries + r'''
static struct {
    struct pipeline_entry *pipelines[PIPELINE_BUCKETS];
    uint64_t pipeline_count, pipelines_made;
    struct { unsigned pipelines_made; double pipeline_seconds; } statistics;
} vk;
static pthread_t owner;
static int serviced, calls, failures, published;
static int synchronous;
static void host_load_trace(const char *phase, const char *detail, ...)
{
    (void)detail;
    assert(pthread_equal(pthread_self(), owner));
    if (!strcmp(phase, "vulkan.pipeline.published")) published++;
}
static int check(VkResult result, const char *what)
{
    (void)what;
    assert(pthread_equal(pthread_self(), owner));
    if (result) failures++;
    return result == 0;
}
static uint32_t description_hash(const struct xgpu_vulkan_pipeline_description *description)
{
    return (uint32_t)description->value;
}
static void service(void)
{
    assert(pthread_equal(pthread_self(), owner));
    serviced++;
}
#define HOST_LOG_INFO 0
static void host_logf(int level, const char *format, ...)
{
    (void)level; (void)format;
}
''' + code + r'''
static VkPipeline pipeline_build(const struct xgpu_vulkan_pipeline_description *description,
    VkResult *result, double *elapsed)
{
    struct timespec delay = {0, 2000000};
    uintptr_t value = description->value;
    assert(pthread_equal(pthread_self(), owner) == synchronous);
    nanosleep(&delay, NULL);
    /* The job keeps its own description even if its source is overwritten. */
    assert(description->value == value);
    *result = value == 9999 ? -1 : 0;
    *elapsed = 0.002;
    __atomic_add_fetch(&calls, 1, __ATOMIC_RELEASE);
    return *result ? VK_NULL_HANDLE : value;
}
''' + accessors + r'''
int main(void)
{
    struct xgpu_vulkan_pipeline_description description = {1234};
    unsigned index;
    owner = pthread_self();
    xgpu_vulkan_preparation_service(service);
    for (index = 1; index <= PIPELINE_JOBS * 3; index++)
    {
        description.value = index;
        assert(xgpu_vulkan_pipeline_prepare(&description));
        assert(xgpu_vulkan_pipeline_prepare(&description));
        description.value = 0;
    }
    assert(vk.pipeline_count == PIPELINE_JOBS * 3);
    assert(serviced > 0);
    description.value = PIPELINE_JOBS * 3;
    assert(xgpu_vulkan_pipeline(&description) == description.value);
    assert(published == PIPELINE_JOBS * 3);
    /* A retirement/checkpoint may occur with compilation pending. Jobs keep
       no borrowed input and normal handle lookup still waits for the key. */
    description.value = 5000;
    assert(xgpu_vulkan_pipeline_prepare(&description));
    description.value = 0;
    xgpu_vulkan_preparation_service(NULL);
    description.value = 5000;
    assert(xgpu_vulkan_pipeline(&description) == 5000);
    assert(vk.statistics.pipelines_made == PIPELINE_JOBS * 3 + 1);
    /* Failed creation is published on the owner and wakes all waiters. */
    xgpu_vulkan_preparation_service(service);
    description.value = 9999;
    assert(xgpu_vulkan_pipeline_prepare(&description));
    assert(xgpu_vulkan_pipeline(&description) == VK_NULL_HANDLE);
    assert(failures == 1);
    assert(!xgpu_vulkan_pipeline_prepare(&description));
    description.value = 5001;
    assert(xgpu_vulkan_pipeline_prepare(&description));
    pipeline_compiler_stop();
    assert(!pipeline_compiler.started);
    assert(xgpu_vulkan_pipeline(&description) == 5001);
    description.value = 5002;
    assert(xgpu_vulkan_pipeline_prepare(&description));
    xgpu_vulkan_preparation_service(NULL);
    pipeline_compiler_stop();
    synchronous = 1;
    description.value = 5003;
    assert(xgpu_vulkan_pipeline(&description) == 5003);
    assert(__atomic_load_n(&calls, __ATOMIC_ACQUIRE) == PIPELINE_JOBS * 3 + 5);
    assert(published == calls);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_texture_checkpoint_keeps_all_bound_stages_pinned(tmp_path):
    text = (ROOT / "source/cache/xbox_texture_cache.c").read_text()
    code = text[text.index("/* port: release an enumeration batch only after"):
                text.index("void texture_cache_bitmap_new(")]
    source = r'''
#include <assert.h>
#include <stddef.h>
#define XBOX_TEXTURE_CACHE_PAGE_SIZE 0x4000
/* (an Xbox map's pages on the AArch64 guests, of a cache of 0x4000) */
static long texture_cache_map_pages = 0x580;
struct xbox_texture_cache_texture { int hardware_format; };
struct data_iterator { int next, datum_index; };
static struct xbox_texture_cache_texture entries[6];
static struct { void *textures, *cache; } xbox_texture_cache_globals;
static int touched[6], advanced;
static unsigned long budget;
static void (*callback)(void *const *);
static void texture_cache_idle(void)
{
    int index;
    for (index = 0; index < 6; index++) touched[index] = 0;
    advanced++;
}
static void data_iterator_new(struct data_iterator *iterator, void *data)
{ (void)data; iterator->next = 0; }
static void *data_iterator_next(struct data_iterator *iterator)
{
    if (iterator->next == 6) return NULL;
    iterator->datum_index = iterator->next;
    return &entries[iterator->next++];
}
static void lruv_block_touch(void *cache, int index)
{ (void)cache; assert(advanced); touched[index]++; }
static void halo_gpu_preparation_checkpoint_set(unsigned long bytes, void (*function)(void *const *))
{ budget = bytes; callback = function; }
''' + code + r'''
int main(void)
{
    void *bound[4];
    int index;
    texture_cache_preparation_begin();
    assert(budget == 22 * 1024 * 1024 / 8 && callback);
    /* (the map's pages, not the whole cache's) */
    texture_cache_map_pages = 0x4000;
    texture_cache_preparation_begin();
    assert(budget == 256 * 1024 * 1024 / 8);
    for (index = 0; index < 4; index++) bound[index] = &entries[index + 1].hardware_format;
    callback(bound);
    assert(advanced == 1 && !touched[0] && !touched[5]);
    for (index = 1; index < 5; index++) assert(touched[index] == 1);
    /* Duplicate bindings pin once; non-cache resources are untouched. */
    bound[0] = bound[1];
    bound[2] = &budget;
    bound[3] = NULL;
    callback(bound);
    assert(advanced == 2 && touched[2] == 1);
    assert(!touched[0] && !touched[1] && !touched[3] && !touched[4] && !touched[5]);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_decal_preparation_restores_each_layer_context(tmp_path):
    text = (ROOT / "source/rasterizer/xbox/rasterizer_xbox_decals.c").read_text()
    function = text[text.index("void rasterizer_decals_port_enumerate("):]
    source = r'''
#include <assert.h>
#define NONE -1
#define NUMBER_OF_SHADER_FRAMEBUFFER_BLEND_FUNCTIONS 2
#define NUMBER_OF_DECAL_LAYERS 5
#define DECAL_GROUP_TAG 1
#define _rasterizer_drawing_mode_normal 0
#define _decal_layer_primary 0
struct tag_iterator { long next; };
struct definition { struct { short framebuffer_blend_function; } shader; };
static struct definition definitions[] = {{{0}}, {{1}}};
static struct { int drawing_mode, draw_environment_decals; } rasterizer_debug_options = {0, 1};
static short current_stencil, expected_stencil, current_layer;
static unsigned begins, quads;
static void tag_iterator_new(struct tag_iterator *it, int tag) { assert(tag == DECAL_GROUP_TAG); it->next = 0; }
static long tag_iterator_next(struct tag_iterator *it) { return it->next < 2 ? it->next++ : NONE; }
static struct definition *decal_definition_get(long tag) { return &definitions[tag]; }
static void rasterizer_set_stencil_mode(short mode) { current_stencil = mode; }
static void rasterizer_decals_begin(short layer) {
    assert(current_stencil == expected_stencil);
    current_layer = layer;
    begins++;
}
static void rasterizer_decals_port_quad(long tag) { assert(tag >= 0 && tag < 2); quads++; }
static void rasterizer_decals_end(void) {
    /* The production alpha-tested layer leaves stencil rejection on. */
    if (current_layer == 3) current_stencil = 2;
}
''' + function + r'''
int main(void) {
    expected_stencil = 0;
    current_stencil = 2;
    rasterizer_decals_port_enumerate(expected_stencil);
    assert(begins && quads && current_stencil == 0);
    expected_stencil = 2;
    current_stencil = 0;
    rasterizer_decals_port_enumerate(expected_stencil);
    assert(current_stencil == 2);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_light_volume_preparation_covers_depth_and_stencil(tmp_path):
    text = (ROOT / "port/linux/game/render_enumerate.c").read_text()
    function = text[text.index("static void light_volumes_draw(void)"):text.index("void render_enumerate_effects(void)")]
    source = r'''
#include <assert.h>
#include <stddef.h>
#define NONE -1
#define TRUE 1
#define FALSE 0
#define FLAG(a) (1 << (a))
#define _widget_type_internal_sprite 5
#define _rasterizer_widget_z_enable_bit 0
typedef struct { float x,y,z; } real_point3d;
struct tag_iterator { long next; };
struct light_volume_definition { struct { long index; } map; short sequence_index; };
static struct light_volume_definition definitions[] = {{{NONE}, 0}, {{10}, 1}, {{11}, 2}};
static struct { struct { real_point3d position, forward; } camera; } render;
static int reject, stencil, depth, batch, begins, ends, draws;
static void point_from_line3d(const real_point3d *p, const real_point3d *v, float t, real_point3d *out) {
    out->x=p->x+v->x*t; out->y=p->y+v->y*t; out->z=p->z+v->z*t;
}
static int pass_stencil_reject(void) { return reject; }
static void rasterizer_set_stencil_mode(short mode) { stencil = mode; }
static void tag_iterator_new(struct tag_iterator *it, int tag) { assert(tag == 'mgs2'); it->next = 0; }
static long tag_iterator_next(struct tag_iterator *it) { return it->next < 3 ? it->next++ : NONE; }
static struct light_volume_definition *light_volume_definition_get(long tag) { return &definitions[tag]; }
static void rasterizer_widget_begin(int type, int flags) {
    assert(type == _widget_type_internal_sprite && flags == FLAG(_rasterizer_widget_z_enable_bit));
    assert(stencil == (reject ? 2 : 0)); depth = 1; begins++;
}
static int rasterizer_widget_set_texture(int stage, long map, short sequence) {
    assert(stage == 0 && sequence == (map == NONE ? 0 : map-9)); return map == 11;
}
static void rasterizer_widget_batch_sprites(int enabled) { batch = enabled; }
static int category, kind;
static void rasterizer_widget_draw_sprite3d(const real_point3d *p, float radius, void *scale, float rotation, unsigned color) {
    assert(depth && batch && p->z == 2.0f && radius > 0 && color == 0x80ffffff);
    assert(category == 7 && kind == 3);
    assert(!scale && rotation == 0); draws++;
}
static void rasterizer_widget_end(void) { assert(!batch); ends++; }
/* (each drawn as a light among the queued transparents, display.hdr's kind) */
#define _rasterizer_profile_queued_transparents 7
#define HALO_HDR_KIND_NONE 0
#define HALO_HDR_KIND_LIGHT 3
static void halo_hdr_category(int c) { category = c; }
static void halo_hdr_draw_kind(int k) { kind = k; }
''' + function + r'''
int main(void) {
    render.camera.forward.z = 1;
    light_volumes_draw();
    reject = 1;
    light_volumes_draw();
    assert(begins == 6 && ends == 6 && draws == 4);
    assert(category == NONE && kind == HALO_HDR_KIND_NONE);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_water_preparation_keeps_world_and_sky_bumpmaps(tmp_path):
    text = (ROOT / "source/rasterizer/xbox/rasterizer_xbox_water.c").read_text()
    start = text.index("if (water_needs_update_flag", text.index("void rasterizer_water_draw("))
    end = text.index("\n\t\t\t}", start) + len("\n\t\t\t}")
    gate = text[start:end]
    source = r'''
#include <assert.h>
#define FALSE 0
#define NONE (-1)
static int water_needs_update_flag, render_enumerating, sky, builds[2], gpu_builds;
static short cached_stencil, device_stencil;
struct group { void *shader; };
static short rasterizer_get_stencil_mode(void) { return cached_stencil; }
static void rasterizer_set_stencil_mode(short mode) {
    if (mode != cached_stencil) cached_stencil = device_stencil = mode;
}
static void rasterizer_water_build_bumpmap(void *shader) {
    assert(shader); builds[sky]++;
    /* Vulkan and Metal prepare pipelines without executing these draws. */
    if (!render_enumerating) gpu_builds++;
    rasterizer_set_stencil_mode(0);
    rasterizer_set_stencil_mode(2);
}
static void draw(struct group *group) {
''' + gate + r'''
}
int main(void) {
    struct group group;
    group.shader = &group;
    /* Ordinary rendering retains the once-per-frame gate, including a
       frame that reuses its existing bumpmap. */
    draw(&group);
    assert(builds[0] == 0);
    water_needs_update_flag = 1;
    draw(&group); draw(&group);
    sky = 1; draw(&group);
    assert(builds[0] == 1 && builds[1] == 0 && !water_needs_update_flag);
    assert(cached_stencil == 2 && device_stencil == 2);
    /* Preparation must visit the same material in both contexts even
       after the world's earlier draw has consumed the update flag. */
    render_enumerating = 1;
    assert(render_enumerating);
    rasterizer_set_stencil_mode(0);
    sky = 0; draw(&group);
    assert(cached_stencil == 0 && device_stencil == 0);
    rasterizer_set_stencil_mode(2);
    sky = 1; draw(&group);
    assert(cached_stencil == 2 && device_stencil == 2);
    assert(builds[0] == 2 && builds[1] == 1 && !water_needs_update_flag);
    render_enumerating = 0;
    draw(&group);
    assert(builds[1] == 1);
    water_needs_update_flag = 1;
    draw(&group); draw(&group);
    assert(builds[1] == 2 && !water_needs_update_flag);
    /* frame_begin marks the real frame's transient image dirty before
       scope/eye enumeration. Those passes must not consume its update. */
    water_needs_update_flag = 1;
    render_enumerating = 1;
    rasterizer_set_stencil_mode(0);
    sky = 0; draw(&group);
    sky = 1; draw(&group);
    assert(water_needs_update_flag && gpu_builds == 2);
    assert(cached_stencil == 0 && device_stencil == 0);
    render_enumerating = 0;
    draw(&group);
    assert(!water_needs_update_flag && gpu_builds == 3);
    assert(cached_stencil == 2 && device_stencil == 2);
    draw(&group);
    assert(gpu_builds == 3);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True)
    # The original once-per-frame gate misses the sky's pipeline.
    broken = source.replace(" || render_enumerating", "")
    result = subprocess.run([str(build(tmp_path, broken))], capture_output=True)
    assert result.returncode != 0
    # Extra bumpmap draws used to leave rejection on, hiding the eight
    # measured Damnation native variants with stencil testing disabled.
    broken = source.replace("rasterizer_set_stencil_mode(saved_stencil);", "(void)saved_stencil;")
    result = subprocess.run([str(build(tmp_path, broken))], capture_output=True)
    assert result.returncode != 0
    broken = source.replace("if (!render_enumerating)\n\t\t\t\t\twater_needs_update_flag", "water_needs_update_flag")
    assert broken != source
    result = subprocess.run([str(build(tmp_path, broken))], capture_output=True)
    assert result.returncode != 0


def test_preparation_hash_bounds_and_unaligned_input(tmp_path):
    text = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    helper = text[text.index("static uint32_t preparation_hash("):text.index("static BOOL preparation_seen(")]
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
''' + helper + r'''
int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    unsigned char *memory = mmap(0, page * 2, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char copy[80];
    unsigned long size, i;
    uint32_t expected;
    assert(memory != MAP_FAILED);
    assert(mprotect(memory + page, page, PROT_NONE) == 0);
    for (size = 0; size <= 67; size++)
    {
        unsigned char *bytes = memory + page - size;
        for (i = 0; i < size; i++) bytes[i] = (unsigned char)(i * 31 + 7);
        memcpy(copy + 1, bytes, size);
        expected = preparation_hash(copy + 1, size);
        assert(preparation_hash(bytes, size) == expected);
        if (size)
        {
            copy[size] ^= 1;
            assert(preparation_hash(copy + 1, size) != expected);
        }
    }
    assert(munmap(memory, page * 2) == 0);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_progress_bar_shader_does_not_depend_on_previous_map(tmp_path):
    text = (ROOT / "source/interface/progress_bar.c").read_text()
    state = text[text.index("static void progress_bar_draw_state(void)\n{"):text.index("static void progress_bar_blur_textures(")]
    # Both production paths establish this state before drawing; neither can
    # silently replace its shader with an inherited/fixed-function handle.
    for start, end in (("static void progress_bar_prepare_draw(void *data)",
                        "void progress_bar_port_enumerate("),
                       ("static void progress_bar_render(\n\treal progress)\n{",
                        "static void progress_bar_create_noise_texture(")):
        body = text[text.index(start):text.index(end, text.index(start))]
        assert body.count("progress_bar_draw_state();") == 1
        assert body.index("progress_bar_draw_state();") < body.index("draw_fade_layer(")
        assert "SetVertexShader(" not in body
        if start.startswith("static void progress_bar_render("):
            assert body.index("rasterizer_set_vertex_shader_permutation(4, 8, 0);") < body.index("progress_bar_draw_state();")
        else:
            assert "rasterizer_set_vertex_shader_permutation(" not in body
    ready = text[text.index("static void progress_bar_make_stuff_ready(\n\tvoid)\n{"):text.index("static void progress_bar_load_loading_texture(\n\tIDirect3DTexture8 **texture)\n{")]
    assert ready.index("rasterizer_set_vertex_shader_permutation(4, 8, 0);") < ready.index("progress_bar_draw_state();")
    assert "halo_gpu_draw_preserved(progress_bar_prepare_draw, texture);" in text
    source = r'''
#include <assert.h>
#include <string.h>
enum { D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
       D3DRS_CULLMODE, D3DRS_ZENABLE, D3DRS_LIGHTING, STATE_COUNT };
enum { TRUE = 1, FALSE = 0, D3DBLEND_SRCALPHA = 4,
       D3DBLEND_ONE = 1, D3DCULL_NONE = 1 };
static int global_d3d_device;
static struct { unsigned handle; } vertex_shader_table[66];
static struct { unsigned shader, states[STATE_COUNT]; } pipeline;
static void IDirect3DDevice8_SetVertexShader(int device, unsigned handle) {
    (void)device;
    /* Production handle zero leaves the preceding shader selected. */
    if (handle) pipeline.shader = handle;
}
static void IDirect3DDevice8_SetRenderState(int device, unsigned state, unsigned value) {
    (void)device;
    pipeline.states[state] = value;
}
''' + state + r'''
int main(void) {
    unsigned expected[STATE_COUNT + 1], previous;
    vertex_shader_table[4].handle = 104;
    memset(&pipeline, 0, sizeof(pipeline));
    progress_bar_draw_state();
    assert(pipeline.shader == 104);
    memcpy(expected, &pipeline, sizeof(expected));
    for (previous = 0; previous < 66; previous++) {
        memset(&pipeline, 0xff, sizeof(pipeline));
        pipeline.shader = previous + 100;
        progress_bar_draw_state();
        assert(!memcmp(expected, &pipeline, sizeof(expected)));
    }
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)


def test_progress_bar_shader_cache_and_preparation_restore(tmp_path):
    text = (ROOT / "source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_runtime.c").read_text()
    runtime = text[text.index("struct packed_vertex_shader\n"):text.index("/* ---------- private code */")]
    source = r'''
#include <assert.h>
#include <string.h>
typedef int boolean;
typedef unsigned char byte;
typedef unsigned int UINT;
#define NONE -1
#define TRUE 1
#define FALSE 0
#define NUMBER_OF_VERTEX_SHADERS 67
#define NUMBER_OF_RASTERIZER_VERTEX_TYPES 12
#define VALID_INDEX(i,n) ((i) >= 0 && (i) < (n))
#define _error_silent 0
#define match_assert(file,line,test) assert(test)
#define match_vassert(file,line,test,...) assert(test)
#define error(...) assert(0)
static int global_d3d_device;
static struct { unsigned handle, instruction_count; } vertex_shader_table[67];
static struct { int statistics_mode; } rasterizer_debug_options;
static struct { unsigned vertex_shader_instruction_count; } rasterizer_frame_statistics;
static unsigned selected, calls;
static int IDirect3DDevice8_SetVertexShader(int device, unsigned handle) {
    (void)device;
    if (handle) selected = handle;
    calls++;
    return 0;
}
static int IDirect3DDevice8_GetVertexShaderSize(int device, unsigned handle, UINT *size) {
    (void)device; (void)handle; *size = 1; return 0;
}
static int IDirect3DDevice8_LoadVertexShader(int device, unsigned handle, long offset) {
    (void)device; (void)handle; (void)offset; assert(0); return -1;
}
static int IDirect3DDevice8_SelectVertexShader(int device, unsigned handle, long offset) {
    (void)device; (void)handle; (void)offset; assert(0); return -1;
}
''' + runtime + r'''
void rasterizer_error(long code, const char *format, ...) {
    (void)code; (void)format; assert(0);
}
int main(void) {
    unsigned saved, saved_calls;
    int index;
    for (index = 0; index < 67; index++) vertex_shader_table[index].handle = index + 100;
    rasterizer_set_vertex_shader_permutation(65, 6, 0);
    assert(selected == 165 && vertex_shader_runtime_globals.current_vertex_shader_index == 65);
    /* Preparation's draw-preserved callback changes only front-end state. */
    saved = selected;
    IDirect3DDevice8_SetVertexShader(global_d3d_device, vertex_shader_table[4].handle);
    assert(selected == 104 && vertex_shader_runtime_globals.current_vertex_shader_index == 65);
    selected = saved;
    /* The actual progress path must move the runtime cache with its shader. */
    rasterizer_set_vertex_shader_permutation(4, 8, 0);
    assert(selected == 104 && vertex_shader_runtime_globals.current_vertex_shader_index == 4);
    saved_calls = calls;
    rasterizer_set_vertex_shader_permutation(65, 6, 0);
    assert(selected == 165 && calls == saved_calls + 1);
    assert(vertex_shader_runtime_globals.current_vertex_shader_index == 65);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)
