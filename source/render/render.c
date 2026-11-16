/*
RENDER.C

symbols in this file:
001743B0 0020:
	_render_effects (0000)
001743D0 0010:
	_render_initialize (0000)
001743E0 0010:
	_render_initialize_for_new_map (0000)
001743F0 0010:
	_render_dispose_from_old_map (0000)
00174400 0010:
	_render_dispose (0000)
00174410 0110:
	_render_nonplayer_frame (0000)
00174520 00f0:
	_render_frame_pregame (0000)
00174610 0020:
	_render_frame_present (0000)
00174630 0070:
	_render_location_visible (0000)
001746A0 0050:
	_rendered_cluster_get (0000)
001746F0 03f0:
	_render_window (0000)
00174AE0 03f0:
	_render_player_frame (0000)
00174ED0 00f0:
	_render_frame (0000)
0029F44C 001f:
	??_C@_0BP@JNDFGHKA@c?3?2halo?2SOURCE?2render?2render?4c?$AA@ (0000)
0029F470 0061:
	??_C@_0GB@NHJDHKGO@location?9?$DOcluster_index?$DO?$DN0?5?$CG?$CG?5lo@ (0000)
0029F4D8 0052:
	??_C@_0FC@DLFICDJP@rendered_cluster_index?$DO?$DN0?5?$CG?$CG?5ren@ (0000)
0029F530 0052:
	??_C@_0FC@NBMIPGBN@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F588 0051:
	??_C@_0FB@IDPOBHAJ@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F5DC 002c:
	??_C@_0CM@LKHLGDKF@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F608 002c:
	??_C@_0CM@HBCHLAAA@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F638 006d:
	??_C@_0GN@PKELCNNH@?$CBmemcmp?$CI?$CGwindow?9?$DOrender_camera?4w@ (0000)
0029F6A8 0071:
	??_C@_0HB@BCLIBPGM@?$CBmemcmp?$CI?$CGwindow?9?$DOrender_camera?4v@ (0000)
0029F720 0048:
	??_C@_0EI@NGOLLMDP@?$CD?$CD?$CD?5ERROR?5something?5is?5wrong?5wit@ (0000)
0030D4D2 0004:
	_render_contrails_enabled (0000)
	_render_particles_enabled (0001)
	_render_particle_systems_enabled (0002)
	_render_weather_particle_systems_enabled (0003)
004B8B22 0001:
	_render_invalid_fog_warning_displayed (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "objects.h"
#include "render_sprite.h"
#include "scenario.h"
#include "structure_bsp_definitions.h"
#include "effects/player_effects.h"
#include "structures/structure_visibility.h"
#include "render_enumerate.h" /* port: port/linux/game/render_enumerate.c */
#include "rasterizer.h"
#include "rasterizer/rasterizer_lights.h"
#include "profile.h"
#include "progress_bar.h"
#include "ui_widget.h"
#include "bink_playback.h"
#include "game.h"
#include "game_engine.h"
#include "interface/first_person_weapons.h"
#include "interface/interface.h"
#include "editor_stubs.h"
#include "render_debug.h"
#include "objects/object_lights_rendering.h"
#include "effects/particle_systems.h"
#include "effects/weather_particle_systems.h"
#include "main/main.h"
#include "structures/structures.h"
/* port: static transparent part links for the newly loaded model tags. */
#include "models/models.h"
#include "structure_culling.h" /* port: port/linux/game/structure_culling.c */
#include "panini.h" /* port: port/linux/game/panini.c */
#include "near_clip.h" /* port: port/linux/game/near_clip.c */
#include "world_ao.h" /* port: port/linux/game/world_ao.c */

#ifdef HALO_VR
#include "../../port/linux/src/vr.h"
#endif

/* ---------- constants */

enum
{
	_render_target_primary = 0,
	_render_target_secondary,
};

enum
{
	_decal_layer_primary = 0,
	_decal_layer_secondary,
	_decal_layer_light,
	_decal_layer_alpha_tested,
	_decal_layer_water,
};

enum
{
	_render_planar_fog_mode_fully_fogged = 2,
};

/* ---------- macros */

#define RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH halo_screen_width()
#define RASTERIZER_TARGET_RENDER_PRIMARY_HEIGHT 480

/* ---------- structures */

/* ---------- prototypes */

static void render_nonplayer_frame(
	const struct render_window *window,
	long window_type);
static void render_window(
	short local_player_index,
	const struct render_camera *camera,
	const struct render_frustum *frustum,
	const struct render_camera *rasterizer_camera,
	const struct render_frustum *rasterizer_frustum,
	short rasterizer_target,
	boolean has_mirror);
static void render_player_frame(
	struct render_window *window,
	const point2d *screenshot_combined_index);

void render_sky(
	void);

/* ---------- globals */

struct render_globals render;

static boolean render_invalid_fog_warning_displayed;
static boolean render_preparing_frame; /* port: enumerate a view without drawing its real picture */

extern short global_screenshot_count;

/* port: display.ambient_occlusion "world"'s pre-pass is being drawn (render_window) */
boolean render_world_ao_prepass = FALSE;
boolean render_contrails_enabled = TRUE;
boolean render_particles_enabled = TRUE;
boolean render_particle_systems_enabled = TRUE;
boolean render_weather_particle_systems_enabled = TRUE;

#ifdef HALO_VR
/* the VR mode (port/linux/game/vr_render.c): the 3D view drawn into both
eyes, without the HUD and menus, which are drawn into their own layer */
boolean vr_render_camera(struct render_camera *camera, struct render_camera *rasterizer_camera, boolean preparing); /* port: defer headwall until preparation ends */
void vr_render_camera_refresh(struct render_camera *camera, struct render_camera *rasterizer_camera); /* port: fresh culling after preparation */
void vr_render_frustum_bounds(real_rectangle2d *bounds);
void vr_render_end(void);
boolean vr_render_hud_camera(struct render_camera *camera);
void vr_render_hud_end(void);
void vr_render_prepare_hud(boolean begin); /* port: preserve eye render state around HUD preparation */
boolean vr_render_weapon_camera(struct render_camera *camera);
void vr_render_crosshairs(void);
void vr_render_world_marks(void);
void vr_seat_blink_draw(void);
void vr_death_draw(void);
void vr_headwall_draw(void);
boolean vr_headwall_faded(void);
void vr_loading_fade_draw(void);
/* the view in a raised scope, or the smart-link's on the visor while zoomed
(port/linux/game/vr_scope.c), drawn before the eyes into an image of its
own: the eye pass's state (the clusters seen, the lights, the scene) is then
the eyes' */
boolean vr_scope_camera(struct render_camera *camera, struct render_camera *rasterizer_camera);
/* port: its mono pipelines prepared at map load too (vr_scope.c) */
void vr_scope_enumerate_camera(struct render_camera *camera, struct render_camera *rasterizer_camera);
/* the smart-link window's zoomed view drawn over the scope's (vr_scope.c) */
void vr_scope_draw_hud(void);
void vr_render_scope_begin(void);
void vr_render_scope_end(void);
/* port/linux/game/vr_cabin.c: a feed cockpit's display, told to vr.c */
void vr_cabin_display_frame(void);
/* the trigger drawn on the assault rifle's first-person model
(port/linux/game/vr_trigger.c), made as a map is loaded */
void vr_trigger_initialize_for_new_map(void);
void vr_trigger_dispose_from_old_map(void);
/* the cabins drawn about a vehicle's seat (port/linux/game/vr_cabin.c),
made as a map is loaded */
void vr_cabin_initialize_for_new_map(void);
void vr_cabin_dispose_from_old_map(void);

static boolean render_vr_eye_pass;
static boolean render_vr_scope_pass;
#endif

/* port: the HUD's Spartan link (port/linux/game/spartan_link.c): a
teammate's feed drawn before a window's view, into an image of its own,
from their eyes (its camera and window bounds filled), without a HUD; the
window's HUD draws the feeds' images */
boolean spartan_link_feed_begin(short local_player_index, struct render_camera *camera);
void spartan_link_feed_end(void);
void spartan_link_draw(short local_player_index);
static boolean render_link_pass;

/* ---------- public code */

void render_effects(
	boolean enable)
{
	render_weather_particle_systems_enabled = enable;
	render_particle_systems_enabled = enable;
	render_particles_enabled = enable;
	render_contrails_enabled = enable;
}

void render_initialize(
	void)
{
	render_objects_initialize();
	/* port: the structure culling tests display.structure_culling asks for */
	structure_culling_initialize();
}

void render_initialize_for_new_map(
	void)
{
	/* port: the map's frames counted from its start, as the game state's
	caches stamp them (the objects' render states, the particles): the
	frames the menus and the loading drew before, as many as the loading
	took, made two runs of one game differ */
	render.frame_index = 0;
	/* port: and no leaf of the map before's: a camera in no leaf is drawn
	from the leaf it was last drawn in (structure_visibility_find_camera),
	which in another map is any leaf, or none */
	render.leaf_index = NONE;
	/* port: preserve retail's energy-then-two-sided-glass compositing. */
	models_fix_transparent_part_links();
	render_objects_initialize_for_new_map();
	/* port: the new map's shaders and pipelines made at its first frame
	(port/linux/game/render_enumerate.c) */
	render_enumerate_new_map();
#ifdef HALO_VR
	/* port: the VR mode's trigger on the assault rifle's first-person model
	(port/linux/game/vr_trigger.c) */
	vr_trigger_initialize_for_new_map();
	/* port: the VR mode's cabins about a vehicle's seat
	(port/linux/game/vr_cabin.c) */
	vr_cabin_initialize_for_new_map();
#endif
}

void render_dispose_from_old_map(
	void)
{
#ifdef HALO_VR
	vr_trigger_dispose_from_old_map();
	vr_cabin_dispose_from_old_map();
#endif
	render_objects_dispose_from_old_map();
}

void render_dispose(
	void)
{
	render_objects_dispose();
}

static void render_nonplayer_frame(
	const struct render_window *window,
	long window_type)
{
	struct rasterizer_window_begin_parameters parameters;

	profile_render_window_start(FALSE);
	memset(&parameters, 0, sizeof(parameters));

	render.camera = window->render_camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);

	parameters.camera = window->rasterizer_camera;
	render_camera_build_frustum(&parameters.camera, NULL, &parameters.frustum, TRUE);

	parameters.rasterizer_target = _render_target_primary;
	parameters.suppress_clear = window_type == 0;
	parameters.window_index = NONE;
	rasterizer_window_begin(&parameters);

	switch (window_type)
	{
	case 0:
		/* letterbox bars and split screen dividers: laid out from the
		viewport, so not centered like the menus on a wide screen */
#ifdef HALO_VR
		/* (no letterbox before the eyes) */
		if (!halo_vr_frame_active())
#endif
		interface_draw_fullscreen_overlays();
		/* port: a split screen player's Settings (what their pause menu
		opens) over every view, full screen (port/linux/game/menu_view.c) */
		halo_screen_ui_offset(TRUE);
		render_ui_widgets_over_views(&window->rasterizer_camera.viewport_bounds);
		halo_screen_ui_offset(FALSE);
		rasterizer_debug_draw();
		break;

	case 1:
		halo_screen_ui_offset(TRUE);
		game_engine_nonplayer_post_rasterize();
		halo_screen_ui_offset(FALSE);
		break;

	default:
		match_assert("c:\\halo\\SOURCE\\render\\render.c", 287, !"unreachable");
		break;
	}

	rasterizer_window_end();
	profile_render_window_end();

	return;
}

void render_frame_pregame(
	struct render_window const *window,
	struct bitmap_data *bitmap)
{
	struct rasterizer_frame_begin_parameters parameters;
	struct rasterizer_window_begin_parameters rasterizer_parameters;

	render.frame_index++;

	rasterizer_frame_begin(&parameters);
	rasterizer_windows_begin();
	render_marks_frame_begin(); /* port: debug.render_marks */
	profile_render_window_start(FALSE);

	memset(&rasterizer_parameters, 0, sizeof(rasterizer_parameters));

	render.camera = window->render_camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);

	rasterizer_parameters.camera = window->rasterizer_camera;
	render_camera_build_frustum(&rasterizer_parameters.camera, NULL, &rasterizer_parameters.frustum, TRUE);

	rasterizer_parameters.rasterizer_target = 0;
	rasterizer_window_begin(&rasterizer_parameters);

	halo_screen_ui_offset(TRUE);
	render_ui_widgets(0, &window->rasterizer_camera.viewport_bounds);
	halo_screen_ui_offset(FALSE);
	bink_playback_render();

	{
		real progress;
		if (game_map_loading_in_progress(&progress))
		{
			progress_bar_display(progress);
		}
	}

	rasterizer_window_end();
	profile_render_window_end();
	rasterizer_windows_end();
	rasterizer_frame_end();

	return;
}

void render_frame_present(
	const point2d *screenshot_index,
	struct bitmap_data *bitmap)
{
	rasterizer_present(bitmap, screenshot_index);

	return;
}

boolean render_location_visible(
	struct location *location)
{
	match_assert("c:\\halo\\SOURCE\\render\\render.c", 584, location->cluster_index>=0 && location->cluster_index<global_structure_bsp_get()->clusters.count);
	return TEST_FLAG(render.visible_cluster_flags[location->cluster_index>>5], location->cluster_index&31);
}

struct rendered_cluster *rendered_cluster_get(
	short rendered_cluster_index)
{
	match_assert("c:\\halo\\SOURCE\\render\\render.c", 592, rendered_cluster_index>=0 && rendered_cluster_index<render.rendered_cluster_count);
	return &render.rendered_clusters[rendered_cluster_index];
}

static void render_window(
	short local_player_index,
	const struct render_camera *camera,
	const struct render_frustum *frustum,
	const struct render_camera *rasterizer_camera,
	const struct render_frustum *rasterizer_frustum,
	short rasterizer_target,
	boolean has_mirror)
{
	struct rasterizer_window_begin_parameters parameters;
	short rendered_cluster_index;
	boolean world_ao_prepassed; /* port: (below) */

	profile_render_window_start(TRUE);
	render.scene_index++;
	memset(&parameters, 0, sizeof(parameters));

	render.local_player_index = local_player_index;
	render.camera = *camera;
	render.frustum = *frustum;
	parameters.camera = *rasterizer_camera;
	parameters.frustum = *rasterizer_frustum;
	parameters.rasterizer_target = rasterizer_target;
	parameters.has_mirror = has_mirror;
	parameters.window_index = render.window_index;
	parameters.fog = render.fog;

	structure_visibility_compute();
	/* port: a map's first frame drawn through every material's surfaces
	(port/linux/game/render_enumerate.c) */
	render_enumerate_structure();
	player_effect_get_screen_flash(local_player_index, &parameters.screen_flash);
	/* port: display.panini, the view drawn through it with this projection
	(port/linux/game/panini.c) */
	if (!render_link_pass) /* port: (spartan_link.c) */
		panini_window_begin(local_player_index, rasterizer_target, rasterizer_camera, &parameters.frustum);
	rasterizer_window_begin(&parameters);
	/* port: debug.render_marks: the window's camera, its viewport the
	rasterizer's (port/linux/game/render_marks.c) */
	render_marks_view(render.window_index, rasterizer_camera);

	if (!bink_playback_in_progress())
	{
		build_sprite_prepare_for_window();
		render_sky();
#ifdef HALO_VR
		if (render_vr_eye_pass)
		{
			/* the weapon in the right controller (vr.aim) */
			struct render_camera head = render.camera;

			vr_render_weapon_camera(&render.camera);
			first_person_weapon_render_update();
			render.camera = head;
		}
		else if (render_vr_scope_pass)
		{
			/* (the scope looks out of the first-person weapon, not drawn:
			first_person_weapons.c) */
		}
		else
#endif
		first_person_weapon_render_update();
		lights_preprocess_scene();
		/* port: display.ambient_occlusion "world": the window's proxies and
		the field's place handed to the shaders' ambient slot before the
		opaque surfaces shade (port/linux/game/world_ao.c; the objects' draws
		say which they are: world_ao_object_begin), taken away after the
		lightmap pass, which the later passes never read */
		if (!render_link_pass) /* port: (spartan_link.c) */
			world_ao_window_begin(local_player_index, rasterizer_target, &render.camera.position.x);
		/* port: ... found once a pixel: the opaque surfaces' inputs to it
		drawn first, alone (the pre-pass: the same draws as below, their
		transparent parts, widgets and fog left for below:
		render_world_ao_prepass), the occlusion found from them at each
		pixel, and the surfaces then drawn reading theirs (not in the VR
		mode's eye or scope passes, nor a mirror's view) */
		world_ao_prepassed = FALSE;
		if (rasterizer_target == _render_target_primary && !rasterizer_camera->mirrored && !render_link_pass &&
#ifdef HALO_VR
			!render_vr_eye_pass && !render_vr_scope_pass &&
#endif
			parameters.frustum.projection_valid)
		{
			float pass_camera[4][4];
			real_point2d screen_point;
			real_vector3d view_vector, middle, step;
			short axis;

			pass_camera[0][0] = parameters.camera.position.x;
			pass_camera[0][1] = parameters.camera.position.y;
			pass_camera[0][2] = parameters.camera.position.z;
			pass_camera[0][3] = parameters.frustum.z_near;
			/* the ray at the screen's middle, one unit along the view's axis,
			and its change a unit of the screen's x and y */
			screen_point.x = 0.0f;
			screen_point.y = 0.0f;
			render_camera_screen_to_view(&parameters.camera, &parameters.frustum, &screen_point, &view_vector);
			matrix4x3_transform_vector(&parameters.frustum.view_to_world, &view_vector, &middle);
			pass_camera[1][0] = middle.i;
			pass_camera[1][1] = middle.j;
			pass_camera[1][2] = middle.k;
			pass_camera[1][3] = parameters.frustum.z_far;
			for (axis = 0; axis < 2; axis++)
			{
				screen_point.x = axis == 0 ? 1.0f : 0.0f;
				screen_point.y = axis == 1 ? 1.0f : 0.0f;
				render_camera_screen_to_view(&parameters.camera, &parameters.frustum, &screen_point, &view_vector);
				matrix4x3_transform_vector(&parameters.frustum.view_to_world, &view_vector, &step);
				pass_camera[2 + axis][0] = step.i - middle.i;
				pass_camera[2 + axis][1] = step.j - middle.j;
				pass_camera[2 + axis][2] = step.k - middle.k;
				pass_camera[2 + axis][3] = 0.0f;
			}
			world_ao_prepassed = world_ao_prepass_begin();
			if (world_ao_prepassed)
			{
				render_world_ao_prepass = TRUE;
				render_objects();
				render_enumerate_models();
				structure_render_preprocess();
				structure_render_lightmaps();
				render_world_ao_prepass = FALSE;
				world_ao_prepass_end(
					parameters.camera.viewport_bounds.x0,
					parameters.camera.viewport_bounds.y0,
					parameters.camera.viewport_bounds.x1,
					parameters.camera.viewport_bounds.y1,
					(float const (*)[4])pass_camera);
			}
		}
		render_objects();
		/* port: and through every model's parts */
		render_enumerate_models();
		if (!world_ao_prepassed)
		{
			structure_render_preprocess();
		}
		structure_render_lightmaps();
		if (!render_link_pass) /* port: (spartan_link.c) */
			world_ao_window_end();
		/* port: nor in a feed of the Spartan link's (spartan_link.c), as the
		VR mode's scope's below */
		if (!render_link_pass)
		{
#ifdef HALO_VR
		/* the scope's view: no lens flares (their visibility tests are the
		eyes', counted per frame), nor the objects' shadows, which a few
		hundred pixels need not pay for */
		if (!render_vr_scope_pass)
		{
#endif
		rasterizer_lens_flares_submit_occlusion_tests();
		render_object_shadows();
		/* port: and every model's (port/linux/game/render_enumerate.c) */
		render_enumerate_shadows();
#ifdef HALO_VR
		}
#endif
		}
		lights_render_diffuse();
		/* port: and every light's */
		render_enumerate_lights(FALSE);

		rasterizer_decals_begin(_decal_layer_light);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		rasterizer_decals_begin(_decal_layer_alpha_tested);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		structure_render_diffuse_texture();

		rasterizer_decals_begin(_decal_layer_primary);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		rasterizer_decals_begin(_decal_layer_secondary);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();
		/* port: and every decal's (port/linux/game/render_enumerate.c) */
		render_enumerate_decals();

		lights_render_specular();
		render_enumerate_lights(TRUE);	/* port: (render_enumerate.c) */
		structure_render_specular_lightmaps();
		structure_render_reflection_lightmap_masks();
		structure_render_reflection_mirrors();
		structure_render_reflections();
		structure_render_transparent_geometry();
		structure_render_fog();
		game_engine_post_rasterize_objects();
		/* port: (the weather's particles about the window's camera, not a
		feed's: spartan_link.c) */
		if (!render_link_pass)
#ifdef HALO_VR
		/* (the weather's particles about the camera: the eyes') */
		if (!render_vr_scope_pass)
#endif
		weather_particle_systems_render();
		render_particles();
		particle_systems_render();
		render_contrails_normal();
		/* port: and every effect's (port/linux/game/render_enumerate.c) */
		render_enumerate_effects();
		rasterizer_transparent_geometry_draw(TRUE);

		rasterizer_decals_begin(_decal_layer_water);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		structure_render_detail_objects();
		rasterizer_transparent_geometry_draw(FALSE);
		rasterizer_transparent_geometry_stop();
		/* port: the plasma bolts' lights' glows (display.dynamic_lights,
		port/linux/src/d3d8_device.c) */
		halo_dynamic_lights_halos_draw();
		structure_render_fog_screen();
		/* port: a feed of the Spartan link's: its view tone mapped
		(display.hdr), with no HUD (spartan_link.c) */
		if (render_link_pass)
		{
			halo_screen_tone_map(
				rasterizer_camera->viewport_bounds.x0,
				rasterizer_camera->viewport_bounds.y0,
				rasterizer_camera->viewport_bounds.x1,
				rasterizer_camera->viewport_bounds.y1);
		}
		else
#ifdef HALO_VR
		if (render_vr_scope_pass)
		{
			/* the scope's own reticle is drawn over its image (host_vulkan_vr.c),
			or the smart-link's window the weapon's zoomed view, as the
			flat screen shows it (port/linux/game/vr_scope.c) */
			vr_scope_draw_hud();
		}
		else if (render_vr_eye_pass)
		{
			/* port: (port/linux/game/render_enumerate.c) */
			render_enumerate_lens_flares();
			rasterizer_lens_flares_draw();
			render_enumerate_screen_flashes();
			rasterizer_screen_flash();
			/* a blink through a jump of the eye (port/linux/game/vr_seat.c) */
			vr_seat_blink_draw();
			/* the world dimmed as the player dies (port/linux/game/vr_death.c) */
			vr_death_draw();
			/* the view faded with the head in a wall
			(port/linux/game/vr_headwall.c) */
			vr_headwall_draw();
			/* the level coming up out of its loading screen's dark
			(port/linux/game/loading_scene.c) */
			vr_loading_fade_draw();
			/* the crosshairs where the aim meets the world, in the eyes */
			vr_render_crosshairs();
			/* the teammates' triangles, the players' names and the
			waypoints over what they mark, in the eyes */
			vr_render_world_marks();
		}
		else
#endif
		{
		/* port: (port/linux/game/render_enumerate.c) */
		render_enumerate_lens_flares();
		rasterizer_lens_flares_draw();
		/* port: the 3D view tone mapped (display.hdr) and antialiased
		(display.anti_aliasing), before the HUD and menus are drawn over it
		(not the VR mode's eye pass: its targets have a layer for each eye) */
		if (rasterizer_target == _render_target_primary)
		{
			halo_screen_tone_map(
				rasterizer_camera->viewport_bounds.x0,
				rasterizer_camera->viewport_bounds.y0,
				rasterizer_camera->viewport_bounds.x1,
				rasterizer_camera->viewport_bounds.y1);
			halo_screen_anti_alias(
				rasterizer_camera->viewport_bounds.x0,
				rasterizer_camera->viewport_bounds.y0,
				rasterizer_camera->viewport_bounds.x1,
				rasterizer_camera->viewport_bounds.y1);
			/* port: and drawn through display.panini's projection, the HUD
			not (port/linux/game/panini.c) */
			panini_window_draw();
		}
		interface_draw_screen();
		/* port: the HUD's Spartan link: the teammates' feeds in its top line
		(port/linux/game/spartan_link.c) */
		if (rasterizer_target == _render_target_primary)
			spartan_link_draw(local_player_index);
		/* port: and every HUD's (port/linux/game/render_enumerate.c) */
		render_enumerate_interface();
		render_enumerate_screen_flashes();
		rasterizer_screen_flash();
		halo_screen_ui_offset(TRUE);
		render_ui_widgets(local_player_index, &rasterizer_camera->viewport_bounds);
		halo_screen_ui_offset(FALSE);
		}
	}

	bink_playback_render();
#ifdef HALO_VR
	/* (the debug draws, the eyes') */
	if (!render_vr_scope_pass)
#endif
	{
	render_camera_debug_frustum(&render.camera, &render.frustum);
	render_debug();
	editor_render();
	/* port: prepare debug geometry too (port/linux/game/render_enumerate.c) */
	render_enumerate_debug();
	rasterizer_debug_draw();
	}
	/* port: the loading transition too (port/linux/game/render_enumerate.c) */
	render_enumerate_loading_screen();
	if (!render_link_pass) /* port: (spartan_link.c) */
		panini_window_end(); /* port */
	rasterizer_window_end();
	profile_render_window_end();

	return;
}

static void render_player_frame(
	struct render_window *window,
	const point2d *screenshot_combined_index)
{
	struct render_camera *camera;
	boolean has_mirror;
	real_rectangle2d frustum_bounds;
	struct render_frustum frustum;
	struct render_frustum rasterizer_frustum;
	struct render_mirror mirror;

	camera = &window->render_camera;
	has_mirror = FALSE;

	structure_visibility_find_camera(camera);
	render.fog.runtime_flags = 0;
	scenario_get_atmospheric_fog(
		window->local_player_index,
		(word)render.visible_sky_index,
		&camera->position,
		&render.fog);
	structure_get_planar_fog((short)render.cluster_index, &render.fog);

	if (render.fog.atmospheric_maximum_distance != 0.0f &&
		render.visible_sky_index == NONE &&
		render.fog.planar_maximum_distance > render.fog.atmospheric_maximum_distance)
	{
		render.fog.planar_maximum_distance = render.fog.atmospheric_maximum_distance;
	}

	if (render.fog.atmospheric_maximum_density == 1.0f &&
		render.fog.atmospheric_maximum_distance != 0.0f)
	{
		window->render_camera.z_far = MIN(
			window->render_camera.z_far,
			render.fog.atmospheric_maximum_distance);
	}

	if (render.fog.planar_mode == _render_planar_fog_mode_fully_fogged &&
		render.fog.planar_maximum_distance != 0.0f)
	{
		window->render_camera.z_far = MIN(
			window->render_camera.z_far,
			render.fog.planar_maximum_distance);
	}

	if (window->render_camera.z_far <= window->render_camera.z_near)
	{
		if (!render_invalid_fog_warning_displayed)
		{
			error(2, "### ERROR something is wrong with the fog in the sky tag or the fog tag");
			render_invalid_fog_warning_displayed = TRUE;
		}

		window->render_camera.z_far = window->render_camera.z_near + 0.01f;
	}

	match_assert(
		"c:\\halo\\SOURCE\\render\\render.c",
		187,
		!memcmp(&window->render_camera.viewport_bounds,
			&window->rasterizer_camera.viewport_bounds,
			sizeof(rectangle2d)));
	match_assert(
		"c:\\halo\\SOURCE\\render\\render.c",
		188,
		!memcmp(&window->render_camera.window_bounds,
			&window->rasterizer_camera.window_bounds,
			sizeof(rectangle2d)));

	render_camera_build_frustum_bounds(camera, &frustum_bounds);
	/* port: a wide field of view's near plane brought nearer, so that its
	corners do not cut a wall beside the player (port/linux/game/near_
	clip.c), from the whole view's bounds (not a screenshot's tile); the
	VR mode's eyes keep their own */
#ifdef HALO_VR
	if (!render_vr_eye_pass && !render_vr_scope_pass)
#endif
	{
		camera->z_near = render_near_clip_distance(camera, &frustum_bounds);
		window->rasterizer_camera.z_near =
			render_near_clip_distance(&window->rasterizer_camera, &frustum_bounds);
	}

	if (screenshot_combined_index != NULL)
	{
		long tile_count;

		tile_count = global_screenshot_count * global_screenshot_size;
		if (tile_count > 0)
		{
			real tile_width;
			real tile_height;
			real_rectangle2d adjusted_bounds;

			tile_width = (frustum_bounds.x1 - frustum_bounds.x0) / (real)tile_count;
			tile_height = (frustum_bounds.y1 - frustum_bounds.y0) / (real)tile_count;
			adjusted_bounds.x0 =
				(real)screenshot_combined_index->x * tile_width + frustum_bounds.x0;
			adjusted_bounds.y0 =
				(real)(tile_count - screenshot_combined_index->y - 1) * tile_height + frustum_bounds.y0;
			adjusted_bounds.x1 = adjusted_bounds.x0 + tile_width;
			adjusted_bounds.y1 = adjusted_bounds.y0 + tile_height;
			frustum_bounds = adjusted_bounds;
		}
	}

#ifdef HALO_VR
	/* the union of the eyes' fields of view */
	if (render_vr_eye_pass)
		vr_render_frustum_bounds(&frustum_bounds);
#endif
	render_camera_build_frustum(camera, &frustum_bounds, &frustum, TRUE);
	render_camera_build_frustum(
		&window->rasterizer_camera,
		&frustum_bounds,
		&rasterizer_frustum,
		TRUE);

	/* port: a map's (or a structure's) shaders and pipelines made as it
	starts: its first frame drawn first, unseen, through every shader its
	tags hold, in each fog (port/linux/game/render_enumerate.c), before any
	window of it (a mirror's) is drawn; through a mirror's window where the
	frame draws one, below (not the VR mode's eyes'). The scope's mono
	pipelines have an enumeration of their own. */
	if (render_enumerate_begin(main_get_window_count() == 1 && !render_link_pass /* port: (spartan_link.c) */
#ifdef HALO_VR
			&& !render_vr_eye_pass && !render_vr_scope_pass, render_vr_scope_pass
#else
			, FALSE
#endif
			))
	{
		do
		{
			/* (a mirror's window is no player's, as below) */
			render_window(
				render_enumerate_target() == _render_target_secondary ? NONE : window->local_player_index,
				camera,
				&frustum,
				&window->rasterizer_camera,
				&rasterizer_frustum,
				render_enumerate_target(),
				render_enumerate_has_mirror());
		}
		while (render_enumerate_next());
	}
	if (render_preparing_frame) /* port: all views prepare before presentation changes owners */
		return;
	/* port: preparation is part of this render call, before its real picture */
#ifdef HALO_VR
	if (!render_vr_eye_pass && !render_vr_scope_pass)
#endif
		halo_loading_ready();

	if (main_get_window_count() == 1 && !render_link_pass /* port: (spartan_link.c) */
#ifdef HALO_VR
		/* (a mirror's view would need the eyes' mirrored too; the scope's
		has none) */
		&& !render_vr_eye_pass && !render_vr_scope_pass
#endif
		)
	{
		if (structure_visibility_find_mirror(camera, &frustum, &mirror))
		{
			short saved_cluster_index;
			struct render_camera mirror_camera;
			struct render_frustum mirror_frustum;

			saved_cluster_index = (short)render.cluster_index;
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 225, window->render_camera.viewport_bounds.x0==0);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 226, window->render_camera.viewport_bounds.y0==0);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 227, window->render_camera.viewport_bounds.x1==RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 228, window->render_camera.viewport_bounds.y1==RASTERIZER_TARGET_RENDER_PRIMARY_HEIGHT);

			render_camera_mirror(camera, &mirror, &mirror_camera);
			render_camera_build_frustum(
				&mirror_camera,
				&frustum_bounds,
				&mirror_frustum,
				TRUE);

			rasterizer_profile_enable(FALSE);
			render.cluster_index = mirror.cluster_index;
			render_window(
				NONE,
				&mirror_camera,
				&mirror_frustum,
				&mirror_camera,
				&mirror_frustum,
				_render_target_secondary,
				FALSE);
			render.cluster_index = saved_cluster_index;
			rasterizer_profile_enable(TRUE);
			has_mirror = TRUE;
		}
	}

	render_window(
		window->local_player_index,
		camera,
		&frustum,
		&window->rasterizer_camera,
		&rasterizer_frustum,
		_render_target_primary,
		has_mirror);

	return;
}

#ifdef HALO_VR
/* the HUD and the menus of a player's window, into the HUD's layer, from
the camera the layer matches */
static void render_vr_hud(
	const struct render_window *window,
	boolean prepare_only) /* port: prepare the HUD before the first real eye image */
{
	struct rasterizer_window_begin_parameters parameters;
	struct render_camera camera = window->rasterizer_camera;

	profile_render_window_start(FALSE);
	memset(&parameters, 0, sizeof(parameters));
	vr_render_hud_camera(&camera);
	render.local_player_index = window->local_player_index;
	render.camera = camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);
	parameters.camera = camera;
	render_camera_build_frustum(&parameters.camera, NULL, &parameters.frustum, TRUE);
	parameters.rasterizer_target = _render_target_primary;
	/* (the HUD pass began over transparent black: d3d8_device.c) */
	parameters.suppress_clear = TRUE;
	parameters.window_index = render.window_index;
	rasterizer_window_begin(&parameters);
	if (!bink_playback_in_progress())
	{
		if (!prepare_only) /* port: preparation visits HUD variants without a live interface picture */
			interface_draw_screen();
		/* port: and every HUD's, as the map's first frame was drawn
		(port/linux/game/render_enumerate.c) */
		render_enumerate_hud();
		if (!prepare_only) /* port: real widgets remain in their original draw order */
		{
			halo_screen_ui_offset(TRUE);
			render_ui_widgets(window->local_player_index, &camera.viewport_bounds);
			halo_screen_ui_offset(FALSE);
		}
	}
	rasterizer_window_end();
	vr_render_hud_end();
	profile_render_window_end();

	return;
}

/* a player's window in VR: the 3D view once, into both eyes, then its HUD */
static void render_vr_player_frame(
	struct render_window *window)
{
	struct render_window eye_window = *window;
	boolean preparing = render_enumerate_pending(FALSE) ||
		(halo_vr_scope_size() > 0 && render_enumerate_pending(TRUE)); /* port: prepare all view formats together */

	if (vr_render_camera(&eye_window.render_camera, &eye_window.rasterizer_camera, preparing))
	{
		struct render_window scope_window = *window;

		vr_cabin_display_frame();
		render_preparing_frame = preparing; /* port: enumeration must not acquire a real gameplay frame */
		/* port: prepare the scope before play, even if the first weapon
		has none. The loading transition also uses this mono format. */
		if (halo_vr_scope_size() > 0 && render_enumerate_pending(TRUE))
		{
			struct render_window prepare_window = eye_window;

			vr_scope_enumerate_camera(&prepare_window.render_camera, &prepare_window.rasterizer_camera);
			vr_render_scope_begin();
			render_vr_scope_pass = TRUE;
			halo_vr_pass(HALO_VR_PASS_SCOPE);
			render_player_frame(&prepare_window, NULL);
			render_vr_scope_pass = FALSE;
			vr_render_scope_end();
		}
		/* port: prepare stereo and HUD pipelines while loading still owns
		presentation, then restore the eye state for the actual picture. */
		if (preparing)
		{
			render_vr_eye_pass = TRUE;
			halo_vr_pass(HALO_VR_PASS_EYES);
			render_player_frame(&eye_window, NULL);
			render_vr_eye_pass = FALSE;
			halo_vr_pass(HALO_VR_PASS_HUD);
			vr_render_prepare_hud(TRUE);
			render_vr_hud(window, TRUE);
			vr_render_prepare_hud(FALSE);
		}
		render_preparing_frame = FALSE;
		/* port: preparation may span head motion beyond the late-latch culling margin. */
		if (preparing)
			vr_render_camera_refresh(&eye_window.render_camera, &eye_window.rasterizer_camera);
		halo_loading_ready(); /* port: all formats are ready before any real scope or eye draw */
		/* (none with the head in a wall: the scope, at the eye or the hand,
		would see through it as the faded eyes do not:
		port/linux/game/vr_headwall.c) */
		if (vr_scope_camera(&scope_window.render_camera, &scope_window.rasterizer_camera) && !vr_headwall_faded())
		{
			vr_render_scope_begin();
			render_vr_scope_pass = TRUE;
			halo_vr_pass(HALO_VR_PASS_SCOPE);
			render_player_frame(&scope_window, NULL);
			render_vr_scope_pass = FALSE;
			vr_render_scope_end();
		}
		render_vr_eye_pass = TRUE;
		halo_vr_pass(HALO_VR_PASS_EYES);
		render_player_frame(&eye_window, NULL);
		halo_vr_pass(HALO_VR_PASS_HUD);
		render_vr_eye_pass = FALSE;
		vr_render_end();
		/* the HUD's camera starts from the head's */
		window->rasterizer_camera.position = eye_window.rasterizer_camera.position;
	}
	halo_loading_ready(); /* port: HUD-only frames also finish a loading transition */
	render_vr_hud(window, FALSE);

	return;
}
#endif

void render_frame(
	struct render_window *windoze,
	short window_count,
	const point2d *screenshot_page_index,
	const point2d *screenshot_index,
	struct bitmap_data *screenshot_bitmap,
	real time_delta_since_tick_sec)
{
	struct rasterizer_frame_begin_parameters parameters;
	short window_index;
	struct render_window *window;
	point2d screenshot_combined_index;

	render.frame_index++;
	render.time_delta_since_tick_sec = time_delta_since_tick_sec;
	memset(&parameters, 0, sizeof(parameters));
	/* continuous between ticks (render_interpolation.c) */
	parameters.game_time_sec = render_interpolation_game_time_sec(game_time_get());
	rasterizer_frame_begin(&parameters);
	rasterizer_windows_begin();
	/* port: debug.render_marks: the frame's map and tick
	(port/linux/game/render_marks.c) */
	render_marks_frame_begin();
	/* port: display.ambient_occlusion "world": the scene's objects as drawn
	this frame, the field's upload continued (port/linux/game/world_ao.c;
	nothing unless it is on) */
	world_ao_frame_begin();

	for (window_index = 0; window_index < window_count; window_index++)
	{
		long window_type;

		window = &windoze[window_index];
		render.window_index = window_index;
		if (window->console_window)
		{
			window_type = 0;
		}
		else if (window->local_player_index != NONE)
		{
			if (screenshot_index != NULL && screenshot_page_index != NULL)
			{
				screenshot_combined_index.x =
					screenshot_page_index->x * global_screenshot_size + screenshot_index->x;
				screenshot_combined_index.y =
					screenshot_page_index->y * global_screenshot_size + screenshot_index->y;
			}

#ifdef HALO_VR
			if (halo_vr_frame_active())
			{
				render_vr_player_frame(window);
				continue;
			}
#endif
			/* port: the HUD's Spartan link: a teammate's feed, at most one a
			frame, into its own image before the window's view
			(port/linux/game/spartan_link.c) */
			if (screenshot_index == NULL)
			{
				struct render_window feed_window = *window;

				if (spartan_link_feed_begin(window->local_player_index, &feed_window.render_camera))
				{
					feed_window.rasterizer_camera = feed_window.render_camera;
					feed_window.local_player_index = NONE;
					render_link_pass = TRUE;
					render_player_frame(&feed_window, NULL);
					render_link_pass = FALSE;
					spartan_link_feed_end();
				}
			}
			render_player_frame(
				window,
				screenshot_index != NULL ? &screenshot_combined_index : NULL);
			continue;
		}
		else
		{
			window_type = 1;
		}

		render_nonplayer_frame(window, window_type);
	}

	/* port: debug.render_marks: what the frame draws over its windows
	(port/linux/game/render_marks.c) */
	render_marks_kind(RENDER_MARK_OTHER, "frame overlays");
	halo_screen_ui_offset(TRUE);
	progress_bar_eachframe();
	halo_screen_ui_offset(FALSE);
	rasterizer_windows_end();
	rasterizer_frame_end();

	return;
}

/* ---------- private code */
