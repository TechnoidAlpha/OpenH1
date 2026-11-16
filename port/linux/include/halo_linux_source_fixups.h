/*
HALO_LINUX_SOURCE_FIXUPS.H

The port's functions that the game's sources call, declared for them.
*/

#ifndef __HALO_LINUX_SOURCE_FIXUPS_H
#define __HALO_LINUX_SOURCE_FIXUPS_H

/* map-independent loading presentation (port/linux/src/sdl_platform.c) */
void halo_loading_begin(void);
void halo_loading_art(void);
int halo_loading_owned(void);
void halo_loading_progress(float progress);
void halo_loading_retire(void);
void halo_loading_end(void);
void halo_loading_ready(void);

/* opt-in map loading timestamps (port/linux/src/sdl_platform.c) */
int halo_load_trace_enabled(void);
void halo_load_trace(char const *phase, char const *detail);
/* each pass of the main loop ended: its long frames traced */
void halo_load_trace_frame(void);
/* Preparation batches release texture cache pages only after a complete
draw and after the render owner has consumed their bytes. */
void halo_gpu_preparation_checkpoint_set(unsigned long bytes, void (*checkpoint)(void *const *textures));
void halo_gpu_preparation_loaded(unsigned long bytes);
/* a map's preparation's journal (port/linux/src/d3d8_device.c), recorded and
played back (port/linux/game/render_prepared.c): the texture cache's
bitmaps, as the journal keeps them */
struct halo_gpu_journal_bitmaps
{
	/* a texture the cache holds: nonzero, and its bitmap's tag and index */
	int (*identify)(void const *texture, unsigned long *tag_index, unsigned long *bitmap_index);
	/* the bitmap's texture, loaded (waited for); NULL if there is none */
	void *(*load)(unsigned long tag_index, unsigned long bitmap_index);
	/* display.panini's pass played back: nonzero where it is on now, the
	projection (as halo_screen_panini's) given where the record's is none
	(made with it off) */
	int (*panini)(float projection[8]);
};
/* recorded from now on (the preparation begun) */
void halo_gpu_journal_record(struct halo_gpu_journal_bitmaps const *bitmaps);
/* (the texture cache's: a bitmap it began loading) */
void halo_gpu_journal_texture(unsigned int tag_index, unsigned int bitmap_index);
/* no longer recorded: the words (malloc's), their count and the objects
it could not keep (none for a journal to keep), or NULL; freed with
halo_gpu_journal_free */
unsigned int *halo_gpu_journal_take(unsigned int *count, unsigned int *unknown);
void halo_gpu_journal_free(unsigned int *words);
/* the journal's form (what a record of it is kept for) */
unsigned int halo_gpu_journal_form(void);
/* played back, in a preparation begun: 1, or 0 if the words are malformed;
the objects not found */
int halo_gpu_journal_replay(unsigned int const *words, unsigned int count,
	struct halo_gpu_journal_bitmaps const *bitmaps, unsigned int *unknown);
/* Discard measured listener motion at camera cuts and lifecycle boundaries
(source/sound/sound_manager.c). NONE resets every local listener. */
void sound_listener_motion_reset(short local_player_index);

/* MCC weapon-deletion notification (port/linux/game/mcc_first_person.inl). */
void mcc_first_person_weapon_deleted(long weapon_index);

/* frames between the 30 Hz ticks (port/linux/game/render_interpolation.c);
the platform layer reads the display.interpolation setting */
struct observer_result;
struct render_camera;
struct real_matrix4x3;
union real_point3d;
union real_vector3d;
union real_rgb_color;
int halo_interpolation_enabled(void);
float game_time_get_tick_fraction(void);
float game_time_get_step_age(void);
void render_interpolation_tick(void);
void render_interpolation_reset(void);
void render_interpolation_frame_begin(void);
void render_interpolation_frame_end(void);
float render_interpolation_fraction(void);
struct real_matrix4x3 *render_interpolation_object_node_matrices(long object_index);
void render_interpolation_object_point(long object_index, union real_point3d *point);
/* an object's origin and a unit's camera where a frame draws them, not
mixed of the tick's state and the frame's (render_interpolation.c) */
void render_interpolation_object_origin(long object_index, union real_point3d *origin);
void render_interpolation_unit_camera_position(long unit_index, union real_point3d *position);
/* where the last frame drew a local player's view from, if it drew one */
unsigned char render_interpolation_drawn_view_position(short local_player_index, union real_point3d *position);
/* debug.network_test_elevator's frames (port/linux/game/network_test.c) */
void network_test_elevator_frame(void);
/* a widget's shape (an antenna's chain, a cloth flag's grid), stepped in the
tick, drawn between the ticks hung from its marker as drawn */
void render_interpolation_shape_tick(unsigned long owner, long index, struct real_matrix4x3 const *anchor,
	short count, union real_point3d const *points);
void render_interpolation_shape_forget(unsigned long owner, long index);
unsigned char render_interpolation_shape(unsigned long owner, long index, struct real_matrix4x3 const *anchor,
	short count, union real_point3d *drawn);
float const *render_interpolation_object_function_values(long object_index);
union real_rgb_color const *render_interpolation_object_change_colors(long object_index);
float render_interpolation_unit_camouflage(long unit_index, float *super_amount);
struct observer_result const *render_interpolation_camera(short local_player_index,
	struct observer_result const *observer);
/* the local player's own view (and first-person weapon) was drawn last from
the newest step, not a tick behind (display.direct_camera) */
unsigned char render_interpolation_view_newest(short local_player_index);
/* the local player's own flashlight in the first person on foot, when no
first-person weapon carries it: along the view drawn, from where the body
has it (render_interpolation.c) */
unsigned char render_interpolation_view_flashlight(long unit_index, union real_point3d *position,
	union real_vector3d *forward, union real_vector3d *up);
/* the first-person weapons' animations advanced (first_person_weapons.c,
in the tick), and how far the frame is past that, in ticks: -1 for a view
drawn between the ticks (render_interpolation.c) */
void render_interpolation_first_person_updated(void);
float render_interpolation_first_person_phase(short local_player_index);
/* ... and how far the step running now ends past it, the same way (0 to 1),
for a state changed in it (first_person_weapon_start_interpolation) */
float render_interpolation_first_person_step_phase(void);
/* ... and a Spartan link feed's weapon (first_person_weapons.c's feeds'
slots), sampled at the frame's time as the player's own view drawn from
the newest step is (render_interpolation_first_person_phase): -1 with the
frames' interpolation off */
float render_interpolation_first_person_feed_phase(void);
void render_interpolation_first_person(short local_player_index, struct real_matrix4x3 *node_matrices,
	short node_count, struct render_camera const *camera);
float render_interpolation_game_time_sec(long ticks);
float render_interpolation_ticks_behind(void);
float render_interpolation_ticks_left(long ticks_left);
struct new_particle_data;
unsigned char render_interpolation_particle_later(struct new_particle_data const *data, long object_index,
	short node_index, float fraction);
void render_interpolation_particles_due(void);
void render_interpolation_object_deleted(long object_index);
void render_interpolation_ghosts_draw(void);

/* whether the map being loaded, by the name it was loaded by, is a
downloaded one, whose scripts may call and set less: one map sharing
downloaded, or one game.downloaded_maps names (port/linux/game/map_share.c) */
int map_share_map_downloaded(char const *map_name);

/* an IP address as the log may show it: a public one as a tag of the run's,
never whole (port/linux/src/log_address.c, log_address.h) */
const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size);

/* the port's menus' screen for one of ui.map's, by name, else the name
(port/linux/game/menu_tags.c) */
char const *pc_menus_screen(char const *name);
/* every failed assertion and warning counted by its place, with how many
times it has failed; then, once it is logged, under debug.fatal_asserts a
failed assertion ends the run (port/linux/game/assert_report.c; cseries.c) */
unsigned long assert_report_failed(char const *information, char const *file, long line, unsigned char fatal);
void assert_report_logged(char const *information, char const *file, long line, unsigned char fatal);
void assert_report_counts(unsigned long *failed, unsigned long *warned);
/* debug.fixed_step: a game of this machine alone runs a tick a frame
(port/linux/src/port_config.c; game_time.c, main.c) */
int halo_fixed_step(void);
/* debug.fresh_memory_fill: the byte (0x00, 0xAA) the allocators fill what
they give out uncleared with, or -1 for the game's own fill
(port/linux/src/port_config.c; debug_memory.c, xbox_memory.c, xbox_kernel.c) */
int halo_fresh_memory_fill(void);
/* the frames' clock, with each frame's time fixed (debug.frame_time,
debug.fixed_step): whether, and the frames' time since the start in
milliseconds; and each frame's time, which main.c gives it
(port/linux/game/frames_clock.c) */
int frames_clock(unsigned long *milliseconds);
void frames_clock_frame(double seconds);
/* debug.trace, the scorecard's per-step log (port/linux/game/sim_trace.c):
each simulation step (the tick it ran, the sub-step's index in the tick and
the sub-steps a tick, 0 and 1 at 30 Hz: game_time.c after each tick, for
its last sub-step; the fast phase's trace slot for the others, sim_rate.h),
and what the steps do, each call made only while the trace is on
(SIM_TRACE), so that it costs nothing when it is off */
extern int sim_trace_on;
#define SIM_TRACE(call) do { if (sim_trace_on) { call; } } while (0)
void sim_trace_step(long tick, long micro_step, long steps_per_tick);
/* the sub-steps of this tick (port/linux/game/sim_rate.h) */
short sim_substep_count(void);
void sim_trace_projectile_new(long projectile_index);
void sim_trace_projectile_collision(long projectile_index, void const *point, void const *normal,
	float time_remaining, float fraction);
void sim_trace_projectile_detonate(long projectile_index);
void sim_trace_weapon_fire(long weapon_index, long owner_object_index, short trigger_index);
/* each frame drawn: the first local player's camera, and its first-person
weapon's nodes (first_person_weapons.c) */
void sim_trace_frame(short local_player_index, void const *camera_position, void const *camera_forward,
	void const *camera_up, void const *node_matrices, short node_count);
/* ... and the first local player's view as drawn, and what they ride as
drawn (main.c, as the frame takes the view: render_interpolation_camera) */
void sim_trace_view(short local_player_index, void const *observer_result);
/* debug.latency_log (port/linux/game/latency_log.c): the first local
player's input to the frame that shows it, each call made only while the
log is on (LATENCY_LOG): a weapon fired (weapons.c), each simulation step
(game_time.c), each frame drawn (main.c), a menu's taking a press
(ui_widget.c) and a rumble impulse to a local player (player_rumble.c) */
extern int latency_log_on;
#define LATENCY_LOG(call) do { if (latency_log_on) { call; } } while (0)
void latency_log_fire(long owner_object_index);
void latency_log_substep_begin(void);
void latency_log_substep_end(void);
void latency_log_frame_begin(void);
void latency_log_frame_rendered(void);
void latency_log_frame_presented(void);
void latency_log_menu_acted(void);
void latency_log_rumble_caused(short local_player_index, float duration_seconds);
/* the Custom Edition tag cache window, or NULL unless the
game.custom_edition setting reserved it (port/linux/src/xbox_memory.c) */
void *halo_custom_edition_tag_cache(void);
/* the computer's memory in bytes, or 0 when the system does not say
(port/linux/src/xbox_memory.c) */
unsigned long long platform_memory_bytes(void);
/* gives the memory of the pages of a part of an XPhysicalAlloc block back to
the system, which reads as zeros (port/linux/src/xbox_memory.c) */
void platform_contiguous_discard(void *address, unsigned long size);
/* where Halo PC keeps the channels of the pixels a Custom Edition bitmap
just arrived at (an enum custom_edition_channel_order,
port/linux/game/cache_file_formats.h), which the renderer then samples in
this build's order; forgotten together when the map goes
(port/linux/src/xbox_textures.c) */
void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order);
void halo_custom_edition_texels_forget(void);
void halo_custom_edition_hud_meter(int active);
/* whether a Halo Custom Edition map's multiplayer vehicles are chosen by
their placements' spawn flags, as in retail Halo, and whether a vehicle
placement is placed in the running game
(port/linux/game/custom_edition_objects.c) */
struct scenario_object_datum;
unsigned char custom_edition_vehicles_by_placement(void);
unsigned char custom_edition_vehicle_placement_allowed(struct scenario_object_datum const *placement);

/* the width of the screen the game draws, 480 lines tall: the device's or
the display's shape, or 640 (port/linux/src/d3d8_device.c) */
long halo_screen_width(void);
/* the menus' widgets anchored to the screen's edges on a touchscreen's wide
screen (their port flags anchor_left, anchor_right and stretch): the
horizontal offset of one, how much wider a stretched one is drawn, and how
far left of the menus' 0 the screen's edge is; 0 elsewhere
(port/linux/game/menu_tags.c) */
short ui_widget_port_anchor_offset(long flags);
short ui_widget_port_stretch(long flags);
short ui_widget_port_anchor_extra(void);
/* whether the menus are laid out for a touchscreen (their "touch" platform:
the Android and iOS apps, or debug.touch_menus): their buttons then name no
controller's key (port/linux/game/menu_tags.c) */
int ui_widget_port_touch_menus(void);
/* the tall layout's footer on a touchscreen, raised above a phone's safe
area at the screen's foot: how far (0 elsewhere), and a widget's move up by
it (its port flag foot) as it is drawn (port/linux/game/menu_tags.c) */
short ui_widget_port_foot_lift(void);
short ui_widget_port_foot_offset(long flags);
/* the wide layout of the port's own screens (their widgets' wide attribute:
port/linux/game/menu_tags.c): the extra width past the menus' 640 a view
this wide lays them out in (to 16:9's), and a value's part of it; the
layout for a view; the extra width it is for; a widget's move and its
growth past its picture in it */
short ui_widget_port_wide_extra_of(long width);
short ui_widget_port_wide_scale(short value, short extra);
void ui_widget_port_wide_layout(long width);
short ui_widget_port_wide_extra(void);
short ui_widget_port_wide_offset(long definition_index);
short ui_widget_port_wide_stretch(long definition_index);
/* the width past 16:9's the menu engine's widgets laid out past it (a look's
past: port/linux/game/menu_tags.c) are laid out for now, half of it each
side, and a past attribute's value's part of a width past 16:9 */
short ui_widget_port_past_extra(void);
short ui_widget_port_past_scale(short value, short past);
/* whether a widget's picture keeps its ends at the widget's width (its port
flag keep_ends) */
unsigned char ui_widget_port_keep_ends(long flags);
/* whether the desktop's mouse wheel steps a widget, a row its own left and
right step through, sideways (its port flag wheel_sideways) */
unsigned char ui_widget_port_wheel_sideways(long flags);
/* the screen's pixels to the Xbox's one (port/linux/src/d3d8_device.c) */
float halo_screen_scale(void);
/* the number of the last visibility test ended (D3DDevice_EndVisibilityTest;
0: none), which D3DDevice_GetVisibilityTestResult gives as the time stamp
of the test a slot's count is of (port/linux/src/d3d8_device.c) */
unsigned long d3d8_visibility_test_number(void);
/* the viewport's depth range (D3DVIEWPORT8's MinZ and MaxZ), the rest of
it kept, and the range it had in previous (NULL: not wanted): a range in
the last step before 1 draws behind everything drawn, still clipped at the
far plane (port/linux/src/d3d8_device.c) */
void d3d8_viewport_depth_range(float minimum, float maximum, float previous[2]);
/* the stage samples a region of its texture as a texture of the region's
alone, addressed by its border beyond it (D3DTADDRESS_BORDER, whatever the
stage's address modes): region its width and height, then its left and top,
in the texture's coordinates (0 to 1); NULL: the whole texture, as the
stage's address modes have it (port/linux/src/d3d8_device.c) */
void d3d8_texture_region(unsigned long stage, const float *region);
/* takes up a new width between frames (F11); returns the width */
long halo_screen_commit(void);
/* the shadow maps' pixels for each of their 128 texels each way, a power of
two (display.shadow_resolution) */
long halo_shadow_map_scale(void);
/* how many times a model's size in the 480-line screen's pixels to measure
it at (display.model_detail, source/models/models.c): level nonzero, to
choose its level of detail, otherwise whether it is drawn */
float halo_model_detail_scale(int level);
/* whether display.model_detail is "stock" (port/linux/src/d3d8_gl.c), for
each permutation's level (port/linux/game/model_detail.c) */
int halo_model_detail_stock(void);
/* a meter's value as drawn on the object it is drawn on: a weapon's meter
that shows its charge shows its heat when that is more, the plasma pistol's
gauge (port/linux/game/weapon_gauge.c) */
float weapon_gauge_meter_value(long object_index, short value_source, float meter_value);
/* while TRUE, drawing shifts right to center 640-column layouts */
void halo_screen_ui_offset(unsigned char centered);
long halo_screen_ui_offset_current(void);
/* names a model lighting vertex program, whose draws can be lit for each
pixel (port/linux/src/d3d8_device.c, display.per_pixel_lighting) */
void halo_vertex_shader_lighting(unsigned long handle);
/* Preserve draw state around loading-screen pipeline preparation. */
void halo_gpu_draw_preserved(void (*draw)(void *), void *data);
/* display.anti_aliasing's pass over a window's 3D view, before the HUD and
menus (source/render/render.c): the window's bounds on the screen */
void halo_screen_anti_alias(short x0, short y0, short x1, short y1);
/* display.panini's pass over a window's 3D view, after its anti-aliasing and
before the HUD (port/linux/game/panini.c): the window's bounds on the
screen, and the projection: d, s, the middle's magnification, 0, and the
rectilinear view's tangents at its left, right, top and bottom */
/* (projection NULL: the setting off, the pass journaled alone for a map's
preparation's record) */
void halo_screen_panini(short x0, short y0, short x1, short y1, const float projection[8]);
/* the enhanced dynamic lights (display.dynamic_lights) */
#include "halo_dynamic_lights.h"
/* display.hdr's tone map of a window's 3D view, before its anti-aliasing
(source/render/render.c), and what the game's renderer tells it of its
draws (port/linux/src/d3d8_device.c, xgpu_hdr.h): the category drawing now
(rasterizer.h's _rasterizer_profile_*, -1 none: rasterizer_profile_begin and
_end), and what the code drawing knows of them (HALO_HDR_KIND_*, 0 none) */
void halo_screen_tone_map(short x0, short y0, short x1, short y1);
void halo_hdr_category(int category);
void halo_hdr_draw_kind(int kind);
/* ... and the draws from now on the HUD's and menus' again, after a view of
the world's drawn among them (port/linux/game/menu_spartan.c) */
void halo_hdr_interface(void);
/* debug.render_marks: what the draws that follow belong to, for the
inspector's replay (port/linux/game/render_marks.c) */
#include "render_marks.h"
/* the display's peak and SDR white, as the backend says them, as text
(Video Setup's DISPLAY PEAK row: its value, "800 NITS"; words, its panel's
words); 0 where no renderer draws HDR (port/linux/src/d3d8_device.c) */
int halo_hdr_display_text(int words, char *text, unsigned int size);
#ifndef HALO_HDR_KIND_NONE
#define HALO_HDR_KIND_NONE 0
#define HALO_HDR_KIND_FIRST_PERSON 1
#define HALO_HDR_KIND_NUMERIC 2
#define HALO_HDR_KIND_METER 4
#define HALO_HDR_KIND_SPECULAR 8
#define HALO_HDR_KIND_HUD_GLOW 16
#define HALO_HDR_KIND_LIGHT 32
#define HALO_HDR_KIND_SELF_ILLUMINATION 64
#define HALO_HDR_KIND_SELF_ILLUMINATION_STAGES(stages) ((stages) << 8)
#endif
/* a run whose frames nobody sees (debug.null_renderer, a dedicated server,
no display: port/linux/src/d3d8_device.c): main.c draws none, and paces its
frames to the game's clock (port/linux/game/unseen_frames.c) */
int halo_frames_unseen(void);
void unseen_frame_wait(void);
/* network.client_timeout_seconds, in milliseconds (port/linux/src/port_config.c) */
unsigned long network_client_timeout_milliseconds(void);
/* a co-op level won: the level the host asked for after it (Host Controls),
at its difficulty, else campaign_next, at the game's (difficulty NONE)
(port/linux/game/network_host_game.c) */
char const *network_host_game_cooperative_won(char const *campaign_next, short *difficulty);
/* the game halted on a fatal error, its report offered at the next start
as a crash's (port/linux/src/report.c: main.c) */
void report_session_halted(char const *reason);
/* a dedicated server (halo --server: port/linux/src/server_mode.c,
port/linux/game/server_director.c; docs/dedicated_server.md): whether this
run is one; its start, before the game's (shell_xbox.c); its frames' pace
(main.c); the game's fatal error ended with (main.c); and the console's
output, which it logs and captures for its commands' replies (terminal.c) */
int halo_server_mode(void);
void server_mode_start(void);
void server_mode_pace_frame(void);
void server_mode_pace_frame_receiving(int (*receive)(long milliseconds));
void server_fatal(char const *text);
void server_console_output(const char *text);
/* ... whether debug.txt's lines go to the standard error too
(server.log_debug_echo: errors.c) */
int halo_server_debug_echo(void);
/* ... the level its rotation plays next, NULL if none (game_engine.c) */
char const *server_director_first_level(void);
/* the caches cleared at the start, where Settings' CLEAR CACHES asked for
it (port/linux/src/game_caches.c: shell_xbox.c) */
void game_caches_start(void);
/* the cache slots' sources (port/linux/src/xbox_files.c): the size and
modification time of the map file a slot was copied from, and the name,
checksum and length of the map its whole copy holds, kept beside it once
the copy is on the disk, so that a slot whose map file has been replaced,
or that is not a whole copy of the map its header names, is not used
(cache_files_windows.c) */
void cache_slot_source_load(short slot);
void cache_slot_source_record(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length);
void cache_slot_source_forget(short slot);
int cache_slot_source_matches(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length);
/* ... the folder of this copy of the game's slots, z:\ or, while another
copy holds that, a z:\cache-N\ of its own (with its backslash) */
const char *cache_slot_folder(void);
/* ... whether it is z:\, the cache kept (else a few slots of its own, and
no copies ahead) */
int cache_slot_folder_keeps_cache(void);
/* what was written to a file is on the disk (xbox_files.c) */
int __stdcall FlushFileBuffers(void *file);
/* ... and when a slot's map was last played (seconds; 0: never, copied
by the menus' pictures' pass or ahead), kept with it: the cache's least
useful map is given up first for room (cache_files_windows.c) */
void cache_slot_source_played(short slot);
unsigned long cache_slot_source_played_time(short slot);
/* the cache files' budget (port/linux/src/map_cache.c): what they may take
of the disk, from the bytes they take now and the least they need; and a
copy's decompression buffers of its own (NULL: none) */
unsigned long long map_cache_budget(unsigned long long cache_bytes, unsigned long long least);
void *map_cache_copy_buffer(long size);
/* stock maps unpacked into cache slots off the game's thread, on a few
workers (port/linux/src/map_unpack.c): one map file into one slot file,
whole as the precache writes it, its source marked last (1 whole, 0 not);
a slot's unpack queued (the paths Xbox paths), a finished one taken (each
once), one waited for, and the workers' number */
int map_unpack_slot(const char *map_path, const char *slot_path, long copy_size, char *name, unsigned long *checksum,
	unsigned long *file_length, volatile int *stop, volatile float *progress);
int map_unpack_queue(short slot, const char *map_xbox_path, const char *slot_xbox_path, long copy_size, int precache);
/* ... the game's own copy (precache): its status (0 going, 1 whole, -1
failed) and progress, asked to stop, and waited for and taken (1 whole) */
int map_unpack_status(short slot, float *progress);
void map_unpack_stop(short slot);
int map_unpack_take(short slot);
int map_unpack_take_finished(short *slot, int *succeeded);
void map_unpack_wait(short slot);
int map_unpack_worker_count(void);
/* the maps copied into the cache ahead (port/linux/game/map_cache_prefill.c):
once a frame (main_loop) */
void map_cache_prefill_update(void);
/* (port/linux/game/menu_pictures_render.c) the menus' pictures' pass runs */
int menu_pictures_preparing(void);
/* the deflection of a gamepad's stick (0 the left, 1 the right) read as
none, of 32767: its player's input.left_stick_dead_zone and
input.right_stick_dead_zone, else the game's own (port/linux/src/xinput_sdl.c) */
short halo_stick_dead_range(short gamepad, short stick);
/* whether a gamepad's look turns the other way side to side: its player's
input.invert_look_horizontal (port/linux/src/xinput_sdl.c) */
int halo_look_horizontal_inverted(short gamepad);
/* whether a local player's controller aims as Modern does (input.aim_style,
the player's own), and Modern's acceleration angle, as its tangent, and the
push that accelerates, or -1 for the game's own (source/game/player_control.c) */
int player_control_aim_modern(short local_player_index, float *acceleration_tangent, float *acceleration_threshold);
/* ... and whether a controller's right stick is the flick stick
(input.flick_stick) */
int halo_linux_flick_stick(void);
/* whether a key, a mouse button or a finger went down since it was last
asked, which skips a Bink movie as a controller's button does
(port/linux/src/sdl_platform.c; source/bink/bink_playback.c) */
int platform_movie_skip_pressed(void);
/* the player's move and look as the game read them, for the input trace
(port/linux/src/input_trace.c): a frame's, or a sub-step's */
void input_trace_player(int controller, int substep, float forward, float strafe, float yaw, float pitch);
/* ... and the look stick's acceleration a tick (source/game/player_control.c) */
void input_trace_aim(int controller, int modern, int accelerating, float acceleration_seconds);
/* the mouse in the menus (source/interface/ui_widget.c) */
#include "halo_ui_pointer.h"
/* the debug camera's mouse and keys (source/camera/director.c): whether the
middle button, R and F are held, and the wheel's notches since the last call
(port/linux/src/xinput_sdl.c) */
int halo_linux_debug_camera_input(int *middle, int *up, int *down, float *wheel);

/* replaces the file at to with the one at from, at once (a rename: on POSIX
rename(2), on Windows MoveFileEx's replace), both Xbox paths; FALSE if it
could not (port/linux/src/xbox_files.c) */
int platform_replace_file(const char *from, const char *to);

#endif
