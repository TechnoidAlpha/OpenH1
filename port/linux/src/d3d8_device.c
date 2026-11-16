/*
D3D8_DEVICE.C

The Xbox Direct3D 8 device: the renderer's front end.

The game drives the device through the XDK's inline functions, which keep
the "simple" render states in D3D__RenderState and call into this file for
everything else. At each draw the full state is read back from there and
described to the backend (xgpu_backend.h) in no API's terms: the vertex
program decoded once per shader (nv2a_vsh.c), the pixel shader - texture
stages and register combiners, 57 render states - a key the backend
decodes and builds a shader from (nv2a_psh.c), the rest a pipeline key and
dynamic state, the resources the draw reads and its range. What the game's
states mean is known here and nowhere else; how a GPU draws it is the
backend's (Vulkan's or Metal's, in the host, through the command stream:
xgpu_stream_encode.c).

Conventions carried over from the Xbox:
- Clip space is D3D's (depth 0..1, y down in window space): viewports,
  scissors and texture rows are given from the top-left, as D3D has them.
- Render targets and textures are identified by the physical address in
  their Data field. A texture whose data is a render target samples the
  render target directly (render-to-texture).
- Vertex data is read from guest memory at draw time.
*/

#include "xgpu.h"
#include "xgpu_shader.h"
#include "xgpu_backend.h"
#include "xgpu_hdr.h"
#include "xgpu_manifest.h"
#include "xgpu_stream.h"
#include "frame_pacing.h"
#include "sdl_platform.h"
#include "halo_ui_pointer.h"
#include "halo_dynamic_lights.h"
#include "port_config.h"
#include "battery_saver.h"
#include "menu_pictures.h"
#include "halo_menus.h"
#include "report.h"
#include "bink_import.h"
#include "halo_linux_source_fixups.h"
#include "panini_projection.h"
#include "world_ao.h"
#include "world_cascades.h"
#include "world_proxies.h"
#include "world_rt.h"
#include "world_sdf.h"
#include "main/console.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void d3d8_surface_resize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);


/* ---------- the screen's width

The Xbox screen is 640x480. The ports keep its 480 logical lines and widen
the layout to the drawable's shape. On the desktop, display.resolution can
choose another resolution, and display.resolution_scaling "original" keeps
640 columns. Android uses the actual drawable's pixels.
The game's camera derives its horizontal field of view from the viewport, so
the 3D view simply widens. The menus and full-screen overlays are laid out
for 640 columns; while they draw (halo_screen_ui_offset), everything shifts
right to center them.

The renderer draws at that resolution (platform_screen_mode): render
targets the size of the screen get that many pixels (screen_scale), and
viewports, clears and visibility counts are scaled to match, so the game
still works in its 480 lines; "original" draws 640x480, scaled up at
presentation. The width and the scale change only between frames, after one
is presented (halo_screen_commit). */

#define SCREEN_HEIGHT 480
#define SCREEN_MAXIMUM_WIDTH 1920

/* the width the game draws, 0 until first asked, and how many pixels a
render target the size of the screen has per unit of it */
static long screen_width;
static float screen_scale[2] = { 1.0f, 1.0f };
/* how many times the display's pixels each way supersampling draws (with
the scale screen_mode_choose last chose), and how many it was asked for
(more where the GPU's largest target has too few pixels) */
static float screen_supersampling = 1.0f, screen_supersampling_asked = 1.0f;
static long ui_offset;
#define UI_OFFSET ((long)ui_offset)
/* ---------- anti-aliasing

display.anti_aliasing, off unless it is set: the Xbox drew without any.
"fxaa" and "smaa" are passes over each window's 3D view before the HUD and
menus are drawn over it (halo_screen_anti_alias, xgpu_vulkan_post.c and
xgpu_metal_post.c); "msaa2x" to "msaa8x" draw the back buffer and its depth
buffer with that many samples a pixel (render_target_multisample). Either
combines with display.supersampling (screen_mode_choose), which draws the
screen's targets at more pixels than the display has: the pass and the
samples' resolve are the targets' own, at that resolution, and the display
blit scales the frame down after the HUD. The setting is read again between
frames (halo_screen_commit), so that a change applies from the next one. */

enum
{
	_anti_aliasing_off,
	_anti_aliasing_fxaa,
	_anti_aliasing_smaa,
	_anti_aliasing_msaa,
};

/* (supersampling where the build draws it: not Android's, nor Metal's on
iOS, whose phones' GPUs have no time to spare for it; display.render_scale
draws them below the screen's resolution instead) */
#if !defined(HALO_ANDROID) && !defined(HALO_IOS)
#define SUPERSAMPLING 1
#endif

static const struct
{
	const char *name;
	int mode;
	int samples;
	/* the supersampling it asks for, each way (1: none) */
	float supersampling;
} anti_aliasing_values[] =
{
	{ "off", _anti_aliasing_off, 0, 1.0f },
	{ "fxaa", _anti_aliasing_fxaa, 0, 1.0f },
	/* (Vulkan draws it on a phone too, as on the desktop, and Metal on a
	Mac: passes of fixed cost whatever the scene; "fxaa" is the cheaper
	pass) */
	{ "smaa", _anti_aliasing_smaa, 0, 1.0f },
	/* (an older version's: twice the pixels each way and no pass, which
	display.supersampling = 2.0 is now; config.toml's is written so as it
	is read, port_config.c's config_migrate_supersampling. It drew nothing
	on the Mac, which had no supersampling then) */
#if defined(SUPERSAMPLING) && !defined(HALO_MACOS)
	{ "ssaa2x", _anti_aliasing_off, 0, 2.0f },
#else
	{ "ssaa2x", _anti_aliasing_off, 0, 1.0f },
#endif
	{ "msaa2x", _anti_aliasing_msaa, 2, 1.0f },
	{ "msaa4x", _anti_aliasing_msaa, 4, 1.0f },
	{ "msaa8x", _anti_aliasing_msaa, 8, 1.0f },
};

#define NUMBER_OF_ANTI_ALIASING_VALUES ((int)(sizeof(anti_aliasing_values) / sizeof(anti_aliasing_values[0])))

/* the value in effect, -1 until the setting is first read; multisampling's
samples a pixel, at most the GPU's (anti_aliasing_prepare) */
static int anti_aliasing_value = -1;
static int anti_aliasing_samples;

static void anti_aliasing_prepare(void);

/* display.anti_aliasing's value (none of them: the first, off) */
static void anti_aliasing_read(void)
{
	/* (read again as the settings change: config_changes) */
	static unsigned long read_at = (unsigned long)-1;
	unsigned long changes = config_changes();
	const char *setting;
	int value;

	if (anti_aliasing_value >= 0 && read_at == changes)
		return;
	read_at = changes;
	setting = config_string("display.anti_aliasing");
	for (value = NUMBER_OF_ANTI_ALIASING_VALUES - 1;
		value > 0 && strcmp(setting, anti_aliasing_values[value].name);
		value--)
	{
	}
	if (value == anti_aliasing_value)
		return;
	anti_aliasing_value = value;
	if (strcmp(setting, anti_aliasing_values[value].name))
		platform_log("anti-aliasing: \"%s\" is unknown, so off", setting);
	else
		platform_log("anti-aliasing: %s", setting);
	anti_aliasing_prepare();
}

static int anti_aliasing(void)
{
	if (anti_aliasing_value < 0)
		anti_aliasing_read();
	return anti_aliasing_values[anti_aliasing_value].mode;
}

/* ---------- display.supersampling

The screen's targets drawn at this many times the display's pixels each
way, 1 to 2, which the display blit scales down to the display's
(screen_mode_choose): the 3D view, the HUD and the menus, with
display.anti_aliasing's pass or samples at the targets' resolution. 1 (or
unset, or not a number) draws the display's pixels. The supersampling
drawn is the most of this, "ssaa2x"'s and display.panini's, at most the
GPU's largest target; not with display.resolution_scaling "original", which
draws the Xbox's 640x480. Read between frames (halo_screen_commit). */

#define SUPERSAMPLING_LEAST 1.0f
#define SUPERSAMPLING_MOST 2.0f

static float supersampling_wanted(void)
{
#ifdef SUPERSAMPLING
	static unsigned long read_at = (unsigned long)-1;
	/* (the setting as last read, and what it gives) */
	static float setting = 1.0f, factor = 1.0f;
	float legacy;

	if (read_at != config_changes())
	{
		float value = (float)config_real("display.supersampling");

		read_at = config_changes();
		if (value != setting)
		{
			setting = value;
			factor = value > SUPERSAMPLING_MOST ? SUPERSAMPLING_MOST :
				value >= SUPERSAMPLING_LEAST ? value : SUPERSAMPLING_LEAST;
			if (factor != value)
				platform_log("supersampling: %g is outside %g to %g, so %gx", value, SUPERSAMPLING_LEAST,
					SUPERSAMPLING_MOST, factor);
			else
				platform_log("supersampling: %gx", factor);
		}
	}
	if (anti_aliasing_value < 0)
		anti_aliasing_read();
	legacy = anti_aliasing_values[anti_aliasing_value].supersampling;
	return legacy > factor ? legacy : factor;
#else
	return 1.0f;
#endif
}

/* ---------- display.hdr (xgpu_hdr.h)

The frame is drawn in HDR where the backend can (Vulkan and Metal: not the
VR mode's eyes) and display.hdr asks for it: "on",
or "auto" while the backend's display shows HDR. The back buffer is then
RGBA16F, each draw into it tagged (xgpu_hdr_classify: what the game's code
says of it, halo_hdr_category and halo_hdr_draw_kind, its blend, and
whether the 3D view is drawn or the HUD over it), and each window's 3D
view tone mapped before the HUD (halo_screen_tone_map). Read between
frames (halo_screen_commit): the back buffer is made again when it changes. */

static struct
{
	/* the frame is drawn in HDR; display.hdr_mode,
	display.hdr_paper_white_nits, display.hdr_paper_white and
	display.hdr_ui_nits as last sent to the backend (sent: once at least) */
	BOOL active, sent;
	int mode, paper_white_nits, paper_white, ui_nits;
	/* the game's renderer's category, and what its code says of the draws
	(halo_hdr_category, halo_hdr_draw_kind) */
	int category, kind;
	/* a window's 3D view is drawn (its categories), until it is tone
	mapped: the draws after are the HUD's and the menus' */
	BOOL view;
	/* the draws' target is the HDR back buffer (bind_targets) */
	BOOL target;
	/* the display's state, last logged; its peak and SDR white, last
	logged, and the frames since (a change logged once it has held a
	moment: a Mac's headroom follows its brightness as it is slid) */
	int display_logged;
	char display_text[160];
	int display_text_frames;
} hdr = { FALSE, FALSE, 0, 0, -1, 0, 0, FALSE, FALSE, -1 };

static int hdr_back_buffer_format(void)
{
	return hdr.active ? XGPU_FORMAT_RGBA16F : XGPU_FORMAT_BGRA8;
}

/* display.hdr's value (XGPU_HDR_*): "auto", "on" ("true"), "off" ("false") */
static int hdr_setting(void)
{
	const char *setting = config_string("display.hdr");

	if (!setting || !strcmp(setting, "auto"))
		return XGPU_HDR_AUTO;
	if (!strcmp(setting, "on") || !strcmp(setting, "true"))
		return XGPU_HDR_ON;
	return XGPU_HDR_OFF;
}

static void category_send(int category);

void halo_hdr_category(int category)
{
	/* (and to the backend, which times the GPU's work by it) */
	category_send(category);
	hdr.category = category;
	/* (the 3D view's categories: past the clear, before the HUD's) */
	if (category > 0 && category < 26)
		hdr.view = TRUE;
}

void halo_hdr_draw_kind(int kind)
{
	hdr.kind = kind;
}

/* the game's renderer's category, for the backend's timing
(xgpu_backend_category) */
static void category_send(int category)
{
	xgpu_backend_category(category);
}

/* ---------- display.ambient_occlusion "world" (port/linux/game/world_ao.c,
docs/world-ao.md)

The window's block (the field's place, its proxies) handed to the backend
for the opaque surfaces that draw until world_ao_window_end takes it away;
the field's slices uploaded as its bake gives them. While a window's block
is given, the opaque surfaces' pixel keys have the world's light
(world_ao_key: xgpu.h's XGPU_AMBIENT_OCCLUSION_LIGHT_* with
XGPU_AMBIENT_OCCLUSION_WORLD): a model's lit for each pixel or each vertex
(where its lighting has its world position), and the structure's lightmap
pass (lit as XGPU_LIGHTING_ENVIRONMENT for its position and normal, the
enhanced dynamic lights or not); never a self-illuminated pass, a blended
one, or the first-person weapon (WORLD_AO_OWNER_UNLIT). Each draw's
uniforms carry its object (halo_world_ao_object: its owner slot and
reference). With the setting off nothing here is called, no key has the
world's bit and the uniforms' world_ao stay as they are: the shaders and
the frames are as without it. */

_Static_assert(XGPU_WORLD_SDF_BRICK == WORLD_SDF_BRICK && XGPU_WORLD_SDF_BRICK_SPAN == WORLD_SDF_BRICK_SPAN &&
	XGPU_WORLD_SDF_ATLAS == WORLD_SDF_ATLAS && XGPU_WORLD_SDF_RANGE_VOXELS == WORLD_SDF_RANGE_VOXELS &&
	XGPU_WORLD_SDF_CELL_OUTSIDE == WORLD_SDF_CELL_OUTSIDE && XGPU_WORLD_SDF_CELL_INSIDE == WORLD_SDF_CELL_INSIDE &&
	XGPU_WORLD_SDF_CELL_BRICK == WORLD_SDF_CELL_BRICK, "the world's field as the emitters read it (xgpu.h)");
_Static_assert(XGPU_WORLD_AO_SHAPES == XGPU_WORLD_AO_PROXIES_MAXIMUM &&
	XGPU_WORLD_AO_SPHERE == XGPU_WORLD_AO_PROXY_SPHERE && XGPU_WORLD_AO_CAPSULE == XGPU_WORLD_AO_PROXY_CAPSULE &&
	XGPU_WORLD_AO_BOX == XGPU_WORLD_AO_PROXY_BOX && XGPU_WORLD_AO_FIELD == XGPU_WORLD_AO_PROXY_FIELD,
	"the world's proxies as the emitters take them (xgpu.h)");
_Static_assert(WORLD_PROXY_MAXIMUM_PER_WINDOW == XGPU_WORLD_AO_PROXIES_MAXIMUM &&
	_world_proxy_sphere == XGPU_WORLD_AO_PROXY_SPHERE && _world_proxy_capsule == XGPU_WORLD_AO_PROXY_CAPSULE &&
	_world_proxy_box == XGPU_WORLD_AO_PROXY_BOX && _world_proxy_field == XGPU_WORLD_AO_PROXY_FIELD,
	"a window's proxies as the game gives them (world_proxies.h)");
_Static_assert(offsetof(struct xgpu_world_ao_block, field) == XGPU_WORLD_AO_BLOCK_FIELD &&
	offsetof(struct xgpu_world_ao_block, parameters) == XGPU_WORLD_AO_BLOCK_PARAMETERS &&
	offsetof(struct xgpu_world_ao_block, proxies) == XGPU_WORLD_AO_BLOCK_PROXIES &&
	offsetof(struct xgpu_world_ao_block, grid) == XGPU_WORLD_AO_BLOCK_GRID &&
	offsetof(struct xgpu_world_ao_block, cells) == XGPU_WORLD_AO_BLOCK_CELLS,
	"the world's block as the emitters declare it (xgpu.h)");
_Static_assert(XGPU_WORLD_AO_GRID_X == WORLD_AO_GRID_X && XGPU_WORLD_AO_GRID_Y == WORLD_AO_GRID_Y &&
	XGPU_WORLD_AO_GRID_Z == WORLD_AO_GRID_Z && XGPU_WORLD_AO_GRID_CELLS == XGPU_WORLD_AO_GRID_CELLS_MAXIMUM,
	"the world's proxy grid as the game makes it and the emitters read it (world_ao.h, xgpu.h)");
_Static_assert(XGPU_WORLD_AO_INPUT_LIGHTMAP == XGPU_WORLD_AO_INPUT_LIGHTMAP_BIT &&
	XGPU_WORLD_AO_INPUT_ALL_SHAPES == XGPU_WORLD_AO_INPUT_ALL_SHAPES_BIT &&
	XGPU_WORLD_AO_INPUT_PRESENT == XGPU_WORLD_AO_INPUT_PRESENT_BIT &&
	XGPU_WORLD_AO_INPUT_CARDS == XGPU_WORLD_AO_INPUT_CARDS_BIT,
	"the pre-pass's inputs as the emitters write them and the CPU reads them (xgpu.h, world_ao.h)");
_Static_assert(WORLD_AO_FIELD_TABLE == XGPU_STREAM_WORLD_AO_TABLE && WORLD_AO_FIELD_ATLAS == XGPU_STREAM_WORLD_AO_ATLAS &&
	WORLD_AO_FIELD_SCENERY == XGPU_STREAM_WORLD_AO_SCENERY && WORLD_AO_FIELD_OBJECTS == XGPU_STREAM_WORLD_AO_OBJECTS,
	"the world's field's parts as the stream carries them");

static struct
{
	/* a window's block is given (its opaque surfaces draw with it); the
	draw's object (its owner slot, WORLD_AO_OWNER_*, and its reference);
	debug.world_ao_view, as read when the block was given; its shaders
	could not be made (world_ao_fail) */
	BOOL active, view;
	int owner;
	float reference;
	BOOL failed;
	/* found once a pixel: the window's pre-pass is drawing (its draws with
	the world's light drawn as their twins, the others skipped: the
	backend's), and the surfaces read their pixels' occlusion from its pass
	(halo_world_ao_prepass_begin, until the window's block is taken away) */
	BOOL prepass, sampled;
	/* (the pixels a side of the blocks it is found once for: 2, a quarter
	of the pixels; 1 with debug.world_ao_full_resolution) */
	float scale;
	/* the window's span as the game draws it (WORLD_AO_SPAN_*:
	halo_world_ao_span), whatever the setting and while a map's pipelines
	are prepared; a record's draw's as it notes it, played back
	(journal_restore) */
	int span;
	/* a map's pipelines are prepared (halo_gpu_prepare until
	halo_gpu_prepared); the backend told to make the draws' twins too
	(world_ao_twins) */
	BOOL preparing, twins;
	/* "ray_traced": the draw's object's owner among the ray-traced scene's
	instances (0 none: halo_world_ao_object) */
	unsigned long rays_owner;
	/* the draws are a two-sided model shader's cards (halo_world_ao_cards) */
	BOOL cards;
} world_ao = { FALSE, FALSE, WORLD_AO_OWNER_NONE, 1.0f, FALSE, FALSE, FALSE, 2.0f, WORLD_AO_SPAN_NONE, FALSE, FALSE, 0,
	FALSE };

/* the draw takes the world's light where its window is given: one of the
window's opaque surfaces, not the first-person weapon, not lighting itself
(the window's span alone, so that the renderer's manifest notes it with
the setting off and in a map's preparation: manifest_note_basis) */
static BOOL world_ao_applies(void)
{
	return (world_ao.span == WORLD_AO_SPAN_OPEN || world_ao.span == WORLD_AO_SPAN_FOUND) &&
		!(hdr.kind & HALO_HDR_KIND_SELF_ILLUMINATION);
}

/* while a map's pipelines are prepared, the world's light as the window's
draws will have it in play where the setting is on (no window is given
then: world_ao.c's world_ao_window_begin): the world's for every one, read
from the pre-pass where it would find the window's once a pixel (its span
found: the primary view into the back buffer, world_ao_prepass_begin) */
static BOOL world_ao_preparing(void)
{
	return world_ao.preparing && !world_ao.failed && world_ao_mode() != 0;
}

/* the window's span is one the pre-pass finds once a pixel (its first-person
weapon's too), whatever the setting */
static BOOL world_ao_span_found(void)
{
	return world_ao.span == WORLD_AO_SPAN_FOUND || world_ao.span == WORLD_AO_SPAN_FOUND_FIRST_PERSON;
}

static BOOL world_ao_found(void)
{
	return world_ao_span_found() && !config_boolean("debug.world_ao_inline");
}

/* a draw's pixel key's occlusion, for the light it multiplies, found in the
world (0: none) */
static unsigned char world_ao_key(int light)
{
	BOOL view, sampled;

	if (world_ao.failed || !world_ao_applies())
		return 0;
	if (world_ao.active)
	{
		view = world_ao.view;
		sampled = world_ao.sampled;
	}
	else if (world_ao_preparing())
	{
		view = config_boolean("debug.world_ao_view");
		sampled = world_ao_found();
	}
	else
	{
		return 0;
	}
	return (unsigned char)(light | XGPU_AMBIENT_OCCLUSION_WORLD | (view ? XGPU_AMBIENT_OCCLUSION_WORLD_VIEW : 0) |
		(sampled ? XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED : 0));
}

/* ... and the pre-pass's twins of the window's draws made with them (each
draw's: xgpu_stream_world_ao_twin, those with the world's light read from
the pre-pass and the occluders, the first-person weapon among them), the
backend told as the draws' need changes (XGPU_WORLD_AO_PREPASS_TWINS) */
static void world_ao_twins(void)
{
	BOOL twins = world_ao_preparing() && world_ao_found();

	if (twins == world_ao.twins)
		return;
	world_ao.twins = twins;
	xgpu_backend_world_ao_prepass(twins ? XGPU_WORLD_AO_PREPASS_TWINS : XGPU_WORLD_AO_PREPASS_NONE, 0, NULL);
}

/* a pipeline with the world's light could not be made: none has it from
here on (said once) */
static void world_ao_fail(void)
{
	if (!world_ao.failed)
		platform_log("GPU: display.ambient_occlusion \"world\"'s shaders cannot be made here (refer to the shader log "
			"above): drawn without it");
	world_ao.failed = TRUE;
}

void halo_world_ao_span(int span)
{
	world_ao.span = span;
}

void halo_world_ao_window(float const field[2][4], float const parameters[2][4], float const (*proxies)[3][4],
	int count, float const grid[2][4], unsigned int const (*cells)[4])
{
	/* (19 KB with the grid: on the heap, made once, not on the stack or in
	the image) */
	static struct xgpu_world_ao_block *made;

	if (count >= 0 && !made)
		made = malloc(sizeof(*made));
	if (count >= 0 && !made)
		count = -1;
	world_ao.active = count >= 0;
	world_ao.owner = WORLD_AO_OWNER_NONE;
	world_ao.reference = 1.0f;
	world_ao.rays_owner = 0;
	world_ao.cards = FALSE;
	/* (found once a pixel: no more) */
	if (world_ao.sampled || world_ao.prepass)
		xgpu_backend_world_ao_prepass(XGPU_WORLD_AO_PREPASS_NONE, 0, NULL);
	world_ao.sampled = world_ao.prepass = FALSE;
	if (count < 0)
	{
		xgpu_backend_world_ao_window(NULL);
		return;
	}
	world_ao.view = config_boolean("debug.world_ao_view");
	if (count > XGPU_WORLD_AO_PROXIES_MAXIMUM)
		count = XGPU_WORLD_AO_PROXIES_MAXIMUM;
	memset(made, 0, sizeof(*made));
	memcpy(made->field, field, sizeof(made->field));
	memcpy(made->parameters, parameters, sizeof(made->parameters));
	if (count)
		memcpy(made->proxies, proxies, (size_t)count * sizeof(made->proxies[0]));
	made->parameters[0][3] = (float)count;
	/* (found once a pixel: at a quarter of the pixels, or each) */
	world_ao.scale = config_boolean("debug.world_ao_full_resolution") ? 1.0f : 2.0f;
	made->parameters[1][3] = world_ao.scale;
	memcpy(made->grid, grid, sizeof(made->grid));
	memcpy(made->cells, cells, sizeof(made->cells));
	xgpu_backend_world_ao_window(made);
}

void halo_world_ao_object(int owner, float reference, unsigned long rays_owner)
{
	world_ao.owner = owner;
	world_ao.reference = reference;
	world_ao.rays_owner = rays_owner < XGPU_WORLD_AO_INSTANCE_OWNERS ? rays_owner : 0;
}

void halo_world_ao_cards(int cards)
{
	world_ao.cards = cards != 0;
}

/* ---------- display.ambient_occlusion "ray_traced" (port/linux/game/world_rt.c)

The ray-traced scene's meshes and instances handed to the backend, which
traces the dynamic objects' occlusion by the GPU's ray queries where it has
them (xgpu_backend_capabilities' ray_queries: Vulkan's, where the device has
VK_KHR_ray_query); the pass is told to (its history[3] 2) and each draw's
uniforms carry its object's owner among the instances (world_ao[2]), which
the pre-pass's twins write into their inputs' last word. */

_Static_assert(sizeof(struct world_ao_instance) == sizeof(struct xgpu_world_ao_instance) &&
	offsetof(struct world_ao_instance, mesh) == offsetof(struct xgpu_world_ao_instance, mesh) &&
	offsetof(struct world_ao_instance, owner) == offsetof(struct xgpu_world_ao_instance, owner) &&
	offsetof(struct world_ao_instance, kind) == offsetof(struct xgpu_world_ao_instance, kind) &&
	WORLD_RT_STRUCTURE == XGPU_WORLD_AO_INSTANCE_STRUCTURE && WORLD_RT_OBJECT == XGPU_WORLD_AO_INSTANCE_OBJECT &&
	WORLD_RT_MESHES == XGPU_STREAM_WORLD_AO_MESHES - 1 &&
	WORLD_RT_INSTANCES == XGPU_STREAM_WORLD_AO_INSTANCES_MAXIMUM &&
	WORLD_RT_MESH_TRIANGLES == XGPU_STREAM_WORLD_AO_MESH_TRIANGLES,
	"the ray-traced scene as the stream carries it (world_rt.h, xgpu_stream.h)");

int halo_world_ao_ray_queries(void)
{
	return xgpu_backend_capabilities.ray_queries != 0;
}

char const *halo_world_ao_ray_queries_missing(void)
{
	/* (said once the backend is ready: its capabilities are zeros before) */
	if (xgpu_backend_capabilities.ray_queries || !xgpu_backend_capabilities.largest_target)
		return NULL;
#ifdef HALO_METAL
	return "the Metal renderer has no ray queries yet";
#else
	return "the GPU or its driver has no Vulkan ray queries (VK_KHR_ray_query, VK_KHR_acceleration_structure)";
#endif
}

void halo_world_ao_mesh(unsigned long id, unsigned long count, float const *triangles)
{
	if (xgpu_backend_capabilities.ray_queries)
		xgpu_backend_world_ao_mesh(id, count, triangles);
}

void halo_world_ao_instances(struct world_ao_instance const *instances, unsigned long count)
{
	if (xgpu_backend_capabilities.ray_queries)
		xgpu_backend_world_ao_instances((struct xgpu_world_ao_instance const *)instances, count);
}

void halo_world_ao_field(int part, unsigned long width, unsigned long height, unsigned long depth,
	unsigned long z_first, unsigned long z_count, void const *data)
{
	xgpu_backend_world_ao_field(part, width, height, depth, z_first, z_count, data);
}

_Static_assert(XGPU_WORLD_CASCADES == WORLD_CASCADES && XGPU_WORLD_CASCADE_SIDE == WORLD_CASCADE_SIDE &&
	XGPU_WORLD_CASCADE_SPAN == WORLD_CASCADE_SPAN && XGPU_WORLD_CASCADE_RANGE_VOXELS == WORLD_CASCADE_RANGE_VOXELS &&
	XGPU_WORLD_TRACE_STEPS == WORLD_TRACE_STEPS && XGPU_WORLD_TRACE_STRUCTURE == WORLD_TRACE_STRUCTURE &&
	XGPU_WORLD_TRACE_OBJECTS == WORLD_TRACE_OBJECTS && XGPU_STREAM_WORLD_AO_CASCADES_SIDE == WORLD_CASCADE_SPAN &&
	XGPU_STREAM_WORLD_AO_CASCADES_DEPTH == WORLD_CASCADE_SPAN * WORLD_CASCADES &&
	WORLD_CASCADE_TEXEL == 2, "the distance-field scene as the emitters and the stream take it (xgpu.h)");

void halo_world_ao_cascades(unsigned long x, unsigned long y, unsigned long z, unsigned long width, unsigned long height,
	unsigned long depth, void const *data)
{
	xgpu_backend_world_ao_cascades(x, y, z, width, height, depth, data);
}

/* a draw's tag (nv2a_pixel_shader_key.hdr): 0 but into the HDR back
buffer */
static unsigned short hdr_tag(const struct xgpu_pipeline_key *key)
{
	struct xgpu_hdr_draw draw;

	if (!hdr.target)
		return 0;
	memset(&draw, 0, sizeof(draw));
	draw.blend = key->blend;
	draw.blend_source = key->blend_source;
	draw.blend_destination = key->blend_destination;
	draw.blend_operation = key->blend_operation;
	draw.color_mask = key->color_mask;
	draw.category = hdr.category;
	draw.interface = !hdr.view;
	draw.kind = hdr.kind;
	draw.mode = hdr.mode;
	return xgpu_hdr_classify(&draw);
}

#ifdef HALO_VR
static BOOL vr_screen_mode(long *width, float scale[2]);
#endif

/* ---------- display.panini (port/linux/game/panini.c)

The 3D view drawn through the Panini projection is drawn larger at its
middle (port/linux/include/panini_projection.h): by its magnification there,
1.5 at 120 degrees across 16:9 at full strength, 2.4 at 150. So that the
middle keeps its sharpness, the screen is supersampled by as much, as
display.supersampling supersamples it (the display blit scales it down), at
most twice the pixels each way: the magnification at the widest of
display.fov's and display.vehicle_fov's views on the display's shape (of
any local player's, each their profile's), or display.supersampling's where
that is more. Not on Android, Metal (the Mac
and iOS) or VR, which draw only the supersampling the player chooses (the
Mac's: display.supersampling) or none; there the middle is drawn from the
pixels the view has. */

#define PANINI_SUPERSAMPLING_MOST 2.0f
/* (split screen's players, each of whose settings may be their own) */
#define PANINI_LOCAL_PLAYERS 4

/* a field of view in degrees across 16:9 as the tangent of half the
vertical (view_fov.c's fov_tangent), or the stock view's (the cyborg's 70
degrees across 4:3, as the game widens it: render_camera_get_adjusted_field_
of_view_tangent's 0.85) outside 20 to 160 (panini_field_of_view, the
projection on) */
static float panini_vertical_tangent(double degrees)
{
	degrees = panini_field_of_view(degrees, 1.0f);
	return degrees ? (float)(tan(degrees * M_PI / 360.0) * (9.0 / 16.0)) :
		(float)(0.75 * 0.85 * tan(35.0 * M_PI / 180.0));
}

static float panini_supersampling(long display_width, long display_height)
{
#if defined(HALO_ANDROID) || defined(HALO_MACOS) || defined(HALO_VR)
	(void)display_width;
	(void)display_height;
	return 1.0f;
#else
	/* (each local player's own settings, their profile's: the most any of
	them needs, the screen being one target for every view of a split
	screen) */
	static long read_at = -1;
	static float strengths[PANINI_LOCAL_PLAYERS], tangents[PANINI_LOCAL_PLAYERS];
	float most = 1.0f;
	int player;

	if (read_at != (long)config_changes())
	{
		read_at = (long)config_changes();
		for (player = 0; player < PANINI_LOCAL_PLAYERS; player++)
		{
			strengths[player] = (float)config_player_real(player, "display.panini");
			if (strengths[player] > 0.0f)
			{
				const char *vehicle = config_player_string(player, "display.vehicle_fov");
				double foot = config_player_real(player, "display.fov");
				char *end;
				double degrees = strtod(vehicle, &end);
				float seated;

				tangents[player] = panini_vertical_tangent(foot);
				/* ("default": the stock view; "match": display.fov's) */
				seated = !strcmp(vehicle, "match") ? tangents[player] :
					panini_vertical_tangent(end != vehicle ? degrees : 0.0);
				if (seated > tangents[player])
					tangents[player] = seated;
			}
		}
	}
	if (display_height <= 0)
		return 1.0f;
	for (player = 0; player < PANINI_LOCAL_PLAYERS; player++)
	{
		float factor;

		if (!(strengths[player] > 0.0f))
			continue;
		factor = panini_projection_scale(strengths[player], tangents[player] * (float)display_width /
			(float)display_height);
		if (factor > most)
			most = factor;
	}
	return most < PANINI_SUPERSAMPLING_MOST ? most : PANINI_SUPERSAMPLING_MOST;
#endif
}

/* the narrowest picture, in columns of the 480 lines: a display narrower
than it letterboxes it (display.picture_shape on Android: 4:3 or 16:9, the
even width a 16:9 display already has, so it adds no bars there) */
static long screen_narrowest_width(void)
{
#ifdef HALO_ANDROID
	if (!strcmp(config_string("display.picture_shape"), "16:9"))
		return 852;
#endif
	return 640;
}

static void screen_mode_choose(long *width, float scale[2])
{
	long display_width, display_height, narrowest;

#ifdef HALO_VR
	/* the menus' 640x480, at the scale of the HUD's image (each VR pass sets
	its own: halo_vr_pass) */
	if (vr_screen_mode(width, scale))
		return;
#endif
	*width = 640;
	scale[0] = scale[1] = 1.0f;
	screen_supersampling = screen_supersampling_asked = 1.0f;
	if (platform_screen_mode(&display_width, &display_height) && display_width > 0 && display_height > 0)
	{
		long wanted = (SCREEN_HEIGHT * display_width + display_height / 2) / display_height;

		narrowest = screen_narrowest_width();
		*width = wanted < narrowest ? narrowest : wanted > SCREEN_MAXIMUM_WIDTH ? SCREEN_MAXIMUM_WIDTH : wanted & ~1L;
		scale[0] = (float)display_width / (float)*width;
		scale[1] = (float)display_height / (float)SCREEN_HEIGHT;
		/* a display narrower or wider than the game can be: the picture
		keeps its shape and the display blit letterboxes it */
		if (*width != wanted && *width != (wanted & ~1L))
			scale[0] = scale[1] = scale[0] < scale[1] ? scale[0] : scale[1];
		/* supersampling: display.supersampling's (or "ssaa2x"'s), or
		display.panini's, its middle's magnification (above), whichever is
		more, at most as many pixels as the GPU's largest target has (once
		it is known), which the display blit scales down; not with
		"original" resolution scaling, which draws the Xbox's 640x480 (and
		gives no display_width). Each target's pixels are its units times
		the scale, rounded (render_target_get): 1.5 at 2560x1440 draws
		3840x2160 */
		if (xgpu_backend_capabilities.largest_target > 0)
		{
			float factor = supersampling_wanted();
			float panini = panini_supersampling(display_width, display_height);

			if (factor < panini)
				factor = panini;
			screen_supersampling_asked = factor;
			if (*width * scale[0] * factor > (float)xgpu_backend_capabilities.largest_target)
				factor = (float)xgpu_backend_capabilities.largest_target / (*width * scale[0]);
			if (SCREEN_HEIGHT * scale[1] * factor > (float)xgpu_backend_capabilities.largest_target)
				factor = (float)xgpu_backend_capabilities.largest_target / (SCREEN_HEIGHT * scale[1]);
			if (factor > 1.0f)
			{
				scale[0] *= factor;
				scale[1] *= factor;
				screen_supersampling = factor;
			}
		}
	}
}

/* the screen's size and its targets' pixels, in the log, with the
supersampling they have and any the GPU could not draw */
static void screen_mode_log(long width, const float scale[2])
{
	if (screen_supersampling_asked > screen_supersampling)
		platform_log("screen: %ldx%d drawn at %.0fx%.0f, supersampled %.3gx (%.3gx asked: the GPU's targets have "
			"%ld pixels at most each way)", width, SCREEN_HEIGHT, width * scale[0], SCREEN_HEIGHT * scale[1],
			screen_supersampling, screen_supersampling_asked, xgpu_backend_capabilities.largest_target);
	else if (screen_supersampling > 1.0f)
		platform_log("screen: %ldx%d drawn at %.0fx%.0f, supersampled %.3gx", width, SCREEN_HEIGHT, width * scale[0],
			SCREEN_HEIGHT * scale[1], screen_supersampling);
	else
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", width, SCREEN_HEIGHT, width * scale[0],
			SCREEN_HEIGHT * scale[1]);
}

long halo_screen_width(void)
{
	if (!screen_width)
	{
		screen_mode_choose(&screen_width, screen_scale);
		screen_mode_log(screen_width, screen_scale);
	}
	return screen_width;
}

#ifdef HALO_VR
static float vr_text_pixel_scale(void);
static int vr_drawing_eyes(void);
#endif

/* the back buffer's pixels for each of the screen's units, across and down
(debug.render_marks' viewports: port/linux/game/render_marks.c) */
void halo_screen_scale_axes(float scale[2])
{
	halo_screen_width();
	scale[0] = screen_scale[0];
	scale[1] = screen_scale[1];
}

/* the display's pixels for each of the 480 lines (text_hires.c) */
float halo_screen_pixel_scale(void)
{
	halo_screen_width();
#ifdef HALO_VR
	if (vr_text_pixel_scale() > 0.0f)
		return vr_text_pixel_scale();
#endif
	return screen_scale[1];
}

/* display.model_detail: the size a model is measured at to choose its
level of detail (source/models/models.c).

The game measures a model's bounding sphere in the lines of its 480-line
screen (render_frustum_sphere_diameter_in_pixels), whatever the display has,
and compares that with the pixel sizes each level is meant for: on a display
of 1440 lines a model three times the size the levels were chosen for is
drawn as the Xbox drew it at 480. "stock" keeps that; "native" measures in
the display's lines (not supersampling's, which the display scales down;
"original" resolution scaling draws 480); "highest" measures as "native"
which models are too small to draw, and draws the others at their most
detailed level; "auto" is "native" in the headset's eyes, which have far more
lines than a display and are seen close, and "stock" elsewhere. Other than
"stock", each permutation then draws the level that
port/linux/game/model_detail.c chooses from the model's. level: the scale for
choosing the level, otherwise for whether to draw at all. */
enum
{
	_model_detail_stock,
	_model_detail_native,
	_model_detail_highest,
	_model_detail_auto,
};

static int model_detail = -1;
static unsigned long model_detail_read_at;

/* the setting as it applies to the draws now ("auto" chosen by them), and
whether they are the eyes' */
static int model_detail_setting(int *eyes)
{
	*eyes = FALSE;
	if (model_detail < 0 || model_detail_read_at != config_changes())
	{
		const char *setting = config_string("display.model_detail");
		int value = !strcmp(setting, "native") ? _model_detail_native :
			!strcmp(setting, "highest") ? _model_detail_highest :
			!strcmp(setting, "auto") ? _model_detail_auto : _model_detail_stock;

		model_detail_read_at = config_changes();
		if (value != model_detail)
		{
			if (value == _model_detail_stock && strcmp(setting, "stock"))
				platform_log("model detail: \"%s\" is unknown, so stock", setting);
			else
				platform_log("model detail: %s", setting);
		}
		model_detail = value;
	}
#ifdef HALO_VR
	*eyes = vr_drawing_eyes();
#endif
	return model_detail == _model_detail_auto ? (*eyes ? _model_detail_native : _model_detail_stock) : model_detail;
}

/* whether the draws now measure models as "stock" does: each permutation then
draws the model's level, as the Xbox did; otherwise the level
port/linux/game/model_detail.c chooses from the model's */
int halo_model_detail_stock(void)
{
	int eyes;

	return model_detail_setting(&eyes) == _model_detail_stock;
}

float halo_model_detail_scale(int level)
{
	float display_scale;
	int detail, eyes;

	detail = model_detail_setting(&eyes);
	if (detail == _model_detail_stock)
		return 1.0f;
	if (detail == _model_detail_highest && level)
		return 1.0e6f;
	halo_screen_width();
	/* (the eyes' scale is their images' own, which nothing supersamples) */
	display_scale = eyes ? screen_scale[1] : screen_scale[1] / screen_supersampling;
	return display_scale > 1.0f ? display_scale : 1.0f;
}

/* how many pixels the screen's targets draw to the Xbox's one, the larger
of the two ways (the screen effects' convolutions: rasterizer_xbox_screen_effect.c) */
float halo_screen_scale(void)
{
	halo_screen_width();
	return screen_scale[0] > screen_scale[1] ? screen_scale[0] : screen_scale[1];
}

/* display.shadow_resolution: the size the shadow maps are drawn at.

Each object's shadow is drawn from above into a 128x128 map, blurred into
another and projected onto the level under it (rasterizer_xbox_shadows.c).
On a large screen, a shadow's 128 texels show as steps along its edge, which
crawl as the object moves. The two maps, the game's only R5G6B5 render
targets (rasterizer_xbox.c), can be drawn larger as the screen's targets are
(render_target_get): the game's viewports, clears and quads, in its 128 units,
scale up with them. The scale is a power of two up to 8 (1024x1024), which
the blur needs to cover the same part of the map as the Xbox's
(rasterizer_shadow_convolve); 1, the default, draws them as the Xbox did. It
changes only between frames (halo_screen_commit). */
#define SHADOW_MAP_SIZE 128
#define SHADOW_MAP_MAXIMUM_SCALE 8

/* 0 until first asked */
static long shadow_scale;
static unsigned long shadow_scale_read_at;

static long shadow_scale_choose(void)
{
	long resolution = config_integer("display.shadow_resolution");
	long scale = 1;

	while (scale < SHADOW_MAP_MAXIMUM_SCALE && SHADOW_MAP_SIZE * scale * 2 <= resolution)
		scale *= 2;
	return scale;
}

/* the shadow maps' pixels for each of their 128 texels each way */
long halo_shadow_map_scale(void)
{
	if (!shadow_scale)
	{
		shadow_scale_read_at = config_changes();
		shadow_scale = shadow_scale_choose();
		platform_log("shadow maps: %ldx%ld", SHADOW_MAP_SIZE * shadow_scale, SHADOW_MAP_SIZE * shadow_scale);
	}
	return shadow_scale;
}

void halo_screen_ui_offset(unsigned char centered)
{
	ui_offset = centered ? (halo_screen_width() - 640) / 2 : 0;
}

/* the menus' centring offset while a split screen's view draws a screen in
it (port/linux/game/menu_view.c: its canvas's left, at its scale) */
void halo_screen_ui_offset_to(long offset)
{
	ui_offset = offset;
}

/* where the first player's menus were drawn last, for the pointer
(ui_point_from_window_on): in a split screen's view (set), the view's
origin, the scale the menus were drawn at and the canvas's left of the
menus' 0 (port/linux/game/menu_view.c); else centred on the whole screen */
static struct
{
	int set;
	float x0, y0, scale, left;
} ui_pointer_view;

void halo_screen_ui_pointer_view(int set, float x0, float y0, float scale, float left)
{
	ui_pointer_view.set = set && scale > 0.0f;
	ui_pointer_view.x0 = x0;
	ui_pointer_view.y0 = y0;
	ui_pointer_view.scale = scale;
	ui_pointer_view.left = left;
}
/* ---------- state the XDK header's inline functions read and write */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
WORD *D3D__IndexData;
BYTE D3D__StateBlockDirty[1024];

/* ---------- vertex shaders */

#define VERTEX_SHADER_SIGNATURE 0x76736864UL /* 'vshd' */
#define VERTEX_PROGRAM_SLOTS 136

struct vertex_shader_object
{
	unsigned long signature;
	unsigned long id;
	/* the program (xgpu_shader.h), decoded as the shader is made; NULL
	for a shader without one */
	struct xgpu_vertex_program *program;
	unsigned long instruction_count;
	/* the declaration's elements (and each one's D3DVSDT_* type, for the
	trace) */
	struct xgpu_vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned char types[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long element_count;
	unsigned long packed_mask;
	/* one of the game's model lighting programs (halo_vertex_shader_lighting),
	whose draws can be lit for each pixel (display.per_pixel_lighting): where
	its lighting's normal and position are (lighting.lights is 0 for the
	others) */
	struct nv2a_vertex_lighting lighting;
	/* a shader lit for each pixel failed to compile or link: lit as the
	vertex shader lights it from then on */
	BOOL lighting_failed;
	/* lit for each vertex with its ambient light apart (display.ambient_
	occlusion, XGPU_LIGHTING_MODEL_SPLIT) failed: lit without the occlusion */
	BOOL split_failed;
	/* what a map's preparation's journal knows it by: its program's and its
	declaration's hash (journal_vertex_shader), the same in every build and
	on every platform, as its id (the order of its making) is not */
	uint32_t journal_identity;
};

/* ---------- render targets */

/* what a render target's scale follows (render_target_get): an entry whose
scale no longer fits is freed (render_targets_evict) */
enum render_target_kind
{
	_render_target_plain,
	/* the screen's targets and the secondary target: screen_scale's */
	_render_target_screen,
	/* a shadow map: halo_shadow_map_scale's */
	_render_target_shadow_map,
};

/* a render target: the Xbox surface it is, and its description; its
storage is the backend's, by its number */
struct xgpu_render_target
{
	xgpu_target number;
	unsigned long data;  /* physical address */
	unsigned long width, height;
	BOOL depth;
	/* pixels per unit of width and height: more than 1 for the screen's
	targets when the game draws at the display's resolution, and its size
	in pixels */
	float scale[2];
	unsigned long pixel_width, pixel_height;
	/* a colour target's format: XGPU_FORMAT_BGRA8, or XGPU_FORMAT_RGBA16F
	the back buffer drawn in HDR (display.hdr) */
	int format;
	/* 2: a texture array, a layer for each eye, drawn into with multiview
	(the VR mode's eye pass); 4 foveated (xgpu_foveated); else 1 */
	int layers;
	/* with multisampling, its samples a pixel (0 when it has none:
	render_target_multisample) */
	int samples;
	/* changes whenever the target is drawn into or cleared (bind_targets) */
	unsigned long written;
};

struct render_target_entry
{
	struct render_target_entry *next;
	/* the next with the same address bucket (render_target_bucket) */
	struct render_target_entry *next_in_bucket;
	struct xgpu_render_target target;
	unsigned long last_rendered;
	/* the back buffer or its depth buffer, which the 3D view is drawn into:
	multisampled with multisampling (render_target_multisample) */
	BOOL screen_buffer;
	enum render_target_kind kind;
	/* a screen's target's view: 0 the screen's own, else an extra view's
	(halo_view_begin), whose targets are its own */
	int view;
};

/* every draw looks up its targets and whether its textures are render
targets, of which there are dozens */
#define RENDER_TARGET_BUCKET_COUNT 256

static struct render_target_entry *render_target_buckets[RENDER_TARGET_BUCKET_COUNT];

static struct render_target_entry **render_target_bucket(unsigned long data)
{
	return &render_target_buckets[((data >> 12) ^ (data >> 20)) % RENDER_TARGET_BUCKET_COUNT];
}

static struct render_target_entry *render_targets;
/* the number the next target gets */
static xgpu_target next_target_number = 1;

static void render_targets_evict(void);

/* ---------- extra views

A view drawn into images of its own rather than the screen: a teammate's
feed in the HUD's Spartan link (port/linux/game/spartan_link.c). Between
halo_view_begin and halo_view_end the screen's targets (the back buffer,
its depth buffer, the secondary target) are the view's: entries of their
own, at the screen's scale, the back buffer and its depth buffer only the
view's size (its units from the screen's top left corner, where its window
is drawn). They are drawn without multisampling, the anti-aliasing pass or
the world's ambient occlusion, and kept from frame to frame, so that the
view need not be drawn every frame; the HUD draws the view's image
(halo_view_sample). Nothing on the backends is new: a view's images are
render targets as any other. */
#define HALO_VIEW_COUNT 17

static struct
{
	/* its size in the screen's units (0: none) */
	unsigned long width, height;
} views[HALO_VIEW_COUNT];
/* the view the screen's targets are now (0: the screen's own), and the
view whose image stage 0 samples (0: none) */
static int screen_view;
static int sampled_view;
/* views' images no longer drawn, freed between frames (halo_screen_commit) */
static BOOL views_released;

/* the target that holds the Xbox surface at this physical address (the
latest drawn into, of the pass drawing now), or NULL */
struct xgpu_render_target *xgpu_render_target_find(unsigned long data);

/* ---------- the device */

struct d3d8_device
{
	D3DPRESENT_PARAMETERS presentation;
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DVIEWPORT8 viewport;
	D3DMATRIX transforms[D3DTS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	/* the region of its texture each stage samples, as a texture of its own
	with its border beyond it (d3d8_texture_region: its size and origin, in
	the texture's coordinates; size 0: the whole texture) */
	float texture_regions[D3DTSS_MAXSTAGES][4];
	D3DSHADERCONSTANTMODE shader_constant_mode;

	struct vertex_shader_object *vertex_shader;
	struct vertex_shader_object *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	float constants[XGPU_VERTEX_CONSTANT_COUNT][4];
	float viewport_scale[4];
	float viewport_offset[4];

	struct
	{
		DWORD data;
		UINT stride;
	} streams[16];
	/* SetIndices' base vertex: added to every index of an indexed draw (the
	dynamic vertex buffers keep each buffer's vertices at an offset into one
	vertex buffer, and their triangles count from 0: contrails, lightning) */
	UINT base_vertex_index;

	/* the current value of each input register (SetVertexData) */
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	/* the input registers written since Begin, a bit each */
	unsigned long immediate_written;
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long immediate_capacity;

	/* inside a visibility test (D3DDevice_BeginVisibilityTest) */
	BOOL visibility_test_active;
	/* the number of the last test ended (0: none; never 0 after) */
	unsigned long visibility_test_number;

	unsigned long frame;
	unsigned long next_vertex_shader_id;
	/* the backend is ready to draw (xgpu_backend_initialize) */
	BOOL gl_ready;
	BOOL created;
};

static struct d3d8_device device;

/* a map's preparation's journal (below): recording, and what it notes */
enum
{
	JOURNAL_DRAW = 1,
	JOURNAL_DRAW_INDEXED,
	JOURNAL_DRAW_IMMEDIATE,
	JOURNAL_CLEAR,
	JOURNAL_ANTI_ALIAS,
	JOURNAL_PANINI,
	JOURNAL_TONE_MAP,
	JOURNAL_HALOS,
	JOURNAL_STATES,
	/* (not of the device's state: a texture the cache loaded, the
	mirror's pages) */
	JOURNAL_TEXTURE = 0x40,
	JOURNAL_PAGES,
};
static BOOL journal_recording;
static void journal_draw(unsigned long op, unsigned long primitive, unsigned long start, unsigned long count,
	unsigned long base_vertex, unsigned long minimum);
static void journal_operation(unsigned long op, const void *arguments, unsigned long bytes);
static void journal_page(unsigned long page);
static void mirror_page_size_set(unsigned long size);
static void journal_vertex_shader(struct vertex_shader_object *object);
static void journal_settings_blind(unsigned long words[3]);
static void journal_vertex_shader_identify(struct vertex_shader_object *object);

/* A preparation draw is needed once for each pipeline, vertex layout and
texture revision. Geometry mirrors are still refreshed for every range:
their contents, unlike transient draw uploads, are needed during play. The
pipeline is its key's number (xgpu_backend_pipeline_number), not the key's
bytes: a map's preparation looks up a key for each of its million draws. */
#define PREPARATION_BUCKETS 4096
struct preparation_key
{
	unsigned long pipeline;
	struct xgpu_dynamic_state dynamic;
	struct xgpu_texture_source textures[4];
	struct xgpu_vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long strides[16], present, written, primitive, sky;
	unsigned long color_data, color_format, color_size, depth_data, depth_format, depth_size;
	unsigned long texture_data[4], texture_format[4], texture_size[4];
	unsigned long texture_generation[4], palette_generation[4];
	/* (recording a map's preparation's journal: the front end's state a
	draw's pipeline is derived from under some settings, though not under
	these, so that a draw the settings now make a repeat of another is
	journaled all the same: journal_settings_blind) */
	unsigned long journaled[3];
};
static struct
{
	BOOL active, duplicate, trackable;
	struct preparation_key key;
	struct preparation_key *entries[PREPARATION_BUCKETS];
	uint32_t hashes[PREPARATION_BUCKETS];
	unsigned long draws, duplicates;
	unsigned long sky;
	unsigned long loaded_bytes, budget;
	unsigned long preserved;
	BOOL trace, sample;
	unsigned long attempts, sampled_draws;
	double sampled_seconds[4];
	void (*checkpoint)(void *const *textures);
} preparation;

static double preparation_time(void)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
	struct timespec now;

	if (!preparation.sample || clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
		return -1.0;
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#else
	return -1.0;
#endif
}

static void preparation_time_add(int stage, double started)
{
	double now;

	if (started < 0.0)
		return;
	now = preparation_time();
	if (now >= started)
		preparation.sampled_seconds[stage] += now - started;
}

void halo_gpu_preparation_checkpoint_set(unsigned long bytes, void (*checkpoint)(void *const *textures))
{
	preparation.loaded_bytes = 0;
	preparation.budget = bytes;
	preparation.checkpoint = checkpoint;
}

void halo_gpu_preparation_loaded(unsigned long bytes)
{
	if (preparation.active && preparation.loaded_bytes < preparation.budget)
		preparation.loaded_bytes += bytes;
}

static void preparation_draw_complete(void)
{
	void *textures[4];
	int stage;

	if (!preparation.active || preparation.preserved || !preparation.checkpoint || !preparation.budget ||
		preparation.loaded_bytes < preparation.budget)
		return;
	/* A stage can remain bound across many draws. Keep those cache blocks
	touched while releasing older draws' unbound resources. */
	xgpu_backend_consume();
	for (stage = 0; stage < 4; stage++)
		textures[stage] = device.textures[stage];
	preparation.loaded_bytes = 0;
	preparation.checkpoint(textures);
}

void halo_gpu_preparation_sky(int sky)
{
	preparation.sky = sky != 0;
}

static void preparation_clear(void)
{
	unsigned long index;

	for (index = 0; index < PREPARATION_BUCKETS; index++)
	{
		free(preparation.entries[index]);
		preparation.entries[index] = NULL;
	}
	preparation.draws = preparation.duplicates = 0;
}

/* Independent word chains avoid a serial multiply for every key word.
The caller still compares the complete key to establish equality. */
static uint32_t preparation_hash(const unsigned char *bytes, unsigned long size)
{
	uint32_t a = 2166136261U, b = 2166136261U + 1U;
	uint32_t c = 2166136261U + 2U, d = 2166136261U + 3U;
	uint32_t word, hash;
	unsigned long index;

	for (index = 0; size - index >= 16; index += 16)
	{
		memcpy(&word, bytes + index, 4);
		a = (a ^ word) * 16777619U;
		memcpy(&word, bytes + index + 4, 4);
		b = (b ^ word) * 16777619U;
		memcpy(&word, bytes + index + 8, 4);
		c = (c ^ word) * 16777619U;
		memcpy(&word, bytes + index + 12, 4);
		d = (d ^ word) * 16777619U;
	}
	hash = a ^ ((b << 7) | (b >> 25)) ^ ((c << 13) | (c >> 19)) ^ ((d << 19) | (d >> 13));
	for (; size - index >= 4; index += 4)
	{
		memcpy(&word, bytes + index, 4);
		hash = (hash ^ word) * 16777619U;
	}
	for (; index < size; index++)
		hash = (hash ^ bytes[index]) * 16777619U;
	return hash;
}

static BOOL preparation_seen(unsigned long pipeline, const struct xgpu_dynamic_state *dynamic, BOOL immediate,
	unsigned long primitive)
{
	struct preparation_key *key = &preparation.key;
	const unsigned char *bytes = (const unsigned char *)key;
	unsigned long index, bucket;
	uint32_t hash;

	if (!preparation.active || !preparation.trackable)
		return FALSE;
	key->pipeline = pipeline;
	key->dynamic = *dynamic;
	key->primitive = primitive;
	key->sky = preparation.sky;
	if (device.render_target)
	{
		key->color_data = device.render_target->Data;
		key->color_format = device.render_target->Format;
		key->color_size = device.render_target->Size;
	}
	if (device.depth_stencil)
	{
		key->depth_data = device.depth_stencil->Data;
		key->depth_format = device.depth_stencil->Format;
		key->depth_size = device.depth_stencil->Size;
	}
	key->written = immediate ? device.immediate_written : 0;
	journal_settings_blind(key->journaled);
	key->present = 0;
	memset(key->elements, 0, sizeof(key->elements));
	memset(key->strides, 0, sizeof(key->strides));
	if (!immediate)
	{
		memcpy(key->elements, device.vertex_shader->elements, sizeof(key->elements));
		for (index = 0; index < 16; index++)
		{
			key->strides[index] = device.streams[index].stride;
			if (device.streams[index].data)
				key->present |= 1UL << index;
		}
	}
	/* The final mix folds high address and state bits into the bucket. */
	hash = preparation_hash(bytes, sizeof(*key));
	hash ^= hash >> 16;
	hash *= 0x85ebca6bUL;
	hash ^= hash >> 13;
	bucket = hash % PREPARATION_BUCKETS;
	preparation.draws++;
	if (preparation.entries[bucket] && preparation.hashes[bucket] == hash &&
		!memcmp(preparation.entries[bucket], key, sizeof(*key)))
	{
		preparation.duplicates++;
		return TRUE;
	}
	/* A collision replaces the entry: it only repeats preparation. */
	if (!preparation.entries[bucket])
		preparation.entries[bucket] = malloc(sizeof(*key));
	if (preparation.entries[bucket])
	{
		*preparation.entries[bucket] = *key;
		preparation.hashes[bucket] = hash;
	}
	return FALSE;
}

/* the visibility tests are counted by the pixel shaders (the backend's
way: xgpu_backend_capabilities) */
#define SAMPLE_COUNTING xgpu_backend_capabilities.sample_counting

/* ---------- the VR mode (HALO_VR; vr.h, vr.c)

A VR frame is drawn in two passes. The eye pass draws the 3D view once, from
the centre of the head with the union of the eyes' fields of view, into both
eyes at once: the screen's render targets are 2-layer texture arrays drawn
into with multiview, the back buffer being the OpenXR swapchain's image
itself, and each vertex the game projected through clip space moves to its
eye by the transform halo_vr_set_eye_transforms gives for the current
projection. The HUD pass draws the HUD, the menus and everything else into
the quad layer's image, over transparent black. The game draws both in its
640x480 screen, each at its image's scale. The passes, and which views each
draw goes to, are kept here; the eyes' images, the eye corrections and the
present's composition are the backend's (port/linux/vulkan/host_vulkan_vr.c). */

/* (xgpu.h; the VR mode's initialization sets it) */
int xgpu_foveated;

#ifdef HALO_VR
#include "vr.h"
#include "vr_host.h"
#include "vr_visor.h"

static struct
{
	BOOL ready;
	int pass;
	int eye_width, eye_height;
	int hud_width, hud_height;
	/* the back buffer in the eye pass: the swapchain's image, or foveated,
	the four layers made into it at the present (the backend's: by the
	target's number) */
	struct render_target_entry eye_target;
	BOOL foveated;
	int fovea_width, fovea_height;
	BOOL eyes_drawn, hud_drawn;
	/* the scope pass (vr.scopes): the 640x480 screen at the scale that
	makes 480 lines its square image's, its view in the middle 480x480
	(vr_scope.c), copied into the scope's image at the present; whether it
	was drawn this frame */
	int scope_size;
	float scope_scale;
	BOOL scope_drawn;
	/* the HUD's groups (vr.h's HALO_VR_HUD_GROUP_*, halo_vr_hud_group): the
	one drawn into, and whether the anchors choose it; while the HUD has
	panels (vr_visor.c, vr.hud_depth), each group but the centre's is drawn
	into a target of its own (the backend's), cleared as it is first
	drawn in a frame, and copied into its panel's image at the present */
	int hud_group, hud_group_mode;
	BOOL hud_panels;
	/* the bound targets draw both eyes (bind_targets), or one eye's layer
	alone: in the eye pass, a visibility test is drawn into the left
	eye's (XGPU_MULTIVIEW_LEFT_EYE), and what halo_vr_draw_eye asks for into
	that eye's (the crosshairs: each eye's screen correction applied in a
	draw of its own; in a multiview draw, on spark's NVIDIA driver, the
	right eye's came out moved by the left eye's correction and then its
	own) */
	BOOL multiview;
	BOOL left_eye;
	BOOL right_eye;
	BOOL visibility_test;
	int eye_alone;
	/* the model drawn is mirrored (halo_vr_mirrored_draws): its triangles'
	winding is the other way */
	BOOL mirrored;
	/* the effect drawn is soft against the scene's depth (1), and thick (2)
	(halo_vr_effect_depth); 0 none */
	int effect_depth;
	/* debug.gpu_stats */
	BOOL statistics;
} vr_gl;
#endif

/* debug.gpu_stats prints these once a second */
static struct
{
	unsigned long draws, immediate_draws, clears, presents;
	unsigned long skipped_no_program, skipped_no_target, skipped_link;
	unsigned long target_changes;
	/* vertex and index bytes drawn from the mirror, and streamed */
	unsigned long mirrored_bytes, streamed_bytes;
} stats;

static D3DDevice *device_pointer(void)
{
	return (D3DDevice *)&device;
}

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = ((color >> 16) & 0xff) / 255.0f;
	out[1] = ((color >> 8) & 0xff) / 255.0f;
	out[2] = (color & 0xff) / 255.0f;
	out[3] = ((color >> 24) & 0xff) / 255.0f;
}

/* ---------- debugging settings, read once (the device's creation) */

static struct
{
	/* debug.gpu_skip_vertex_shaders "<id>,<id>..." drops draws by vertex
	shader, for finding which pass produces something (port_config.c) */
	const char *skip_vertex_shaders;
	BOOL statistics;
} debug_settings;

/* the source blend factor the pixel shader applies (nv2a_pixel_shader_key's
source_blend), when the draw's blend mixes a constant color factor with a
constant alpha one, which the backends' shaders apply instead; 0
otherwise */
static unsigned char source_blend_in_shader(void)
{
	DWORD source = D3D__RenderState[D3DRS_SRCBLEND], destination = D3D__RenderState[D3DRS_DESTBLEND];
	int source_kind = source == D3DBLEND_CONSTANTCOLOR || source == D3DBLEND_INVCONSTANTCOLOR ? 1 :
		source == D3DBLEND_CONSTANTALPHA || source == D3DBLEND_INVCONSTANTALPHA ? 2 : 0;
	int destination_kind = destination == D3DBLEND_CONSTANTCOLOR || destination == D3DBLEND_INVCONSTANTCOLOR ? 1 :
		destination == D3DBLEND_CONSTANTALPHA || destination == D3DBLEND_INVCONSTANTALPHA ? 2 : 0;

	if (!xgpu_backend_capabilities.mixed_constant_blend && D3D__RenderState[D3DRS_ALPHABLENDENABLE] && source_kind &&
		destination_kind && source_kind != destination_kind)
	{
		return source == D3DBLEND_CONSTANTCOLOR ? 1 : source == D3DBLEND_INVCONSTANTCOLOR ? 2 :
			source == D3DBLEND_CONSTANTALPHA ? 3 : 4;
	}
	return 0;
}

/* ---------- vertical blank emulation */

#define VERTICAL_BLANK_NANOSECONDS (1000000000L / 60)

static pthread_mutex_t vertical_blank_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vertical_blank_condition = PTHREAD_COND_INITIALIZER;
static D3DCALLBACK vertical_blank_callback;
static unsigned long vertical_blank_count;
static volatile unsigned int flip_count;
static unsigned long pending_flips;
static BOOL vertical_blank_thread_started = FALSE;

static void *vertical_blank_thread(void *unused)
{
	struct timespec next;

	(void)unused;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		D3DCALLBACK callback;

		next.tv_nsec += VERTICAL_BLANK_NANOSECONDS;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

		pthread_mutex_lock(&vertical_blank_lock);
		vertical_blank_count++;
		/* a presented frame becomes visible at the next vertical blank */
		if (pending_flips)
		{
			pending_flips--;
			flip_count++;
		}
		callback = vertical_blank_callback;
		pthread_cond_broadcast(&vertical_blank_condition);
		pthread_mutex_unlock(&vertical_blank_lock);

		if (callback)
			callback(0);
	}
	return NULL;
}

static void vertical_blank_start(void)
{
	pthread_mutex_lock(&vertical_blank_lock);
	if (!vertical_blank_thread_started)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, vertical_blank_thread, NULL) == 0)
		{
			pthread_detach(thread);
			vertical_blank_thread_started = TRUE;
		}
		else
		{
			platform_log("cannot start the vertical blank thread");
		}
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

/* replaces main/d3d_intimacy.cpp, which reads the counter out of the Xbox
Direct3D runtime's private device structure */
volatile unsigned int *d3d_find_flipcount(void)
{
	return &flip_count;
}

void WINAPI D3DDevice_SetVerticalBlankCallback(D3DCALLBACK callback)
{
	pthread_mutex_lock(&vertical_blank_lock);
	vertical_blank_callback = callback;
	pthread_mutex_unlock(&vertical_blank_lock);
	vertical_blank_start();
}

void WINAPI D3DDevice_BlockUntilVerticalBlank(void)
{
	unsigned long count;

	vertical_blank_start();
	pthread_mutex_lock(&vertical_blank_lock);
	count = vertical_blank_count;
	while (vertical_blank_count == count)
		pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
	pthread_mutex_unlock(&vertical_blank_lock);
}

/* ---------- render targets */

static void surface_dimensions(const D3DSurface *surface, unsigned long *width, unsigned long *height, BOOL *depth)
{
	struct xgpu_texture_description description;
	DWORD format;

	xgpu_texture_describe(surface->Format, surface->Size, &description);
	*width = description.width;
	*height = description.height;
	format = description.format;
	*depth = format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
}

/* the shadow maps: the game's only R5G6B5 render targets, 128x128
(rasterizer_xbox.c), and the port's shadow atlases, whose regions are
maps of 128 (rasterizer_xbox_shadows.c): all drawn at
display.shadow_resolution's scale */
static BOOL surface_is_shadow_map(const D3DSurface *surface)
{
	struct xgpu_texture_description description;

	xgpu_texture_describe(surface->Format, surface->Size, &description);
	return description.format == D3DFMT_R5G6B5 && description.width >= SHADOW_MAP_SIZE &&
		description.height >= SHADOW_MAP_SIZE;
}

/* the secondary render target, the size of the Xbox's (rasterizer_xbox.c) */
#define SECONDARY_TARGET_WIDTH 320
#define SECONDARY_TARGET_HEIGHT 240

/* the targets render_target_get found last, by what it found them from:
each draw asks again for the same two (bind_targets) */
#define RECENT_RENDER_TARGET_COUNT 4

static struct
{
	DWORD data, format, size;
	long screen_width;
	float scale[2];
	int view;
	struct render_target_entry *entry;
} recent_render_targets[RECENT_RENDER_TARGET_COUNT];
static unsigned long recent_render_target_next;

static struct render_target_entry *render_target_remember(const D3DSurface *surface, long screen,
	struct render_target_entry *entry)
{
	unsigned long slot = recent_render_target_next++ % RECENT_RENDER_TARGET_COUNT;

	recent_render_targets[slot].data = surface->Data;
	recent_render_targets[slot].format = surface->Format;
	recent_render_targets[slot].size = surface->Size;
	recent_render_targets[slot].screen_width = screen;
	recent_render_targets[slot].scale[0] = screen_scale[0];
	recent_render_targets[slot].scale[1] = screen_scale[1];
	recent_render_targets[slot].view = screen_view;
	recent_render_targets[slot].entry = entry;
	return entry;
}

/* a colour target of the screen's, drawn in HDR with the back buffer
(display.hdr): the back buffer, and the screen effects' copy of it, which
their passes copy the view through and back (a scope's warp and mask: the
depth buffer's memory, as the Xbox's, rasterizer_xbox_screen_effect.c) */
static BOOL render_target_screen_colour(const struct render_target_entry *entry)
{
	return !entry->target.depth && entry->kind == _render_target_screen && entry->target.layers == 1 &&
		(entry->screen_buffer || entry->target.data == device.depth_buffer.Data);
}

static struct render_target_entry *render_target_get(const D3DSurface *surface)
{
	struct render_target_entry *entry;
	struct xgpu_target_description description;
	unsigned long width, height, slot;
	long screen;
	BOOL depth;

	int layers = 1;

	if (!surface || !surface->Data)
		return NULL;
	float scale[2] = { 1.0f, 1.0f };
	enum render_target_kind kind = _render_target_plain;

	/* (an entry lives until its scale no longer fits: render_targets_evict) */
	screen = halo_screen_width();
	for (slot = 0; slot < RECENT_RENDER_TARGET_COUNT; slot++)
	{
		if (recent_render_targets[slot].entry && recent_render_targets[slot].data == surface->Data &&
			recent_render_targets[slot].format == surface->Format && recent_render_targets[slot].size == surface->Size &&
			recent_render_targets[slot].screen_width == screen && recent_render_targets[slot].scale[0] == screen_scale[0] &&
			recent_render_targets[slot].scale[1] == screen_scale[1] && recent_render_targets[slot].view == screen_view)
		{
			return recent_render_targets[slot].entry;
		}
	}
	surface_dimensions(surface, &width, &height, &depth);
	/* the screen's targets are drawn at the screen's scale, and so is the
	secondary target (the reflections' and active camouflage's: its size is
	rasterizer_xbox.c's), which keeps the game's 320x240 units, that its
	shaders' constants are in, and gets the screen's pixels a unit; the
	shadow maps at display.shadow_resolution's (halo_shadow_map_scale) */
	if ((width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT) ||
		(!depth && width == SECONDARY_TARGET_WIDTH && height == SECONDARY_TARGET_HEIGHT))
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
		kind = _render_target_screen;
		/* (an extra view's: the back buffer and its depth buffer its size) */
		if (screen_view && height == SCREEN_HEIGHT)
		{
			width = views[screen_view].width;
			height = views[screen_view].height;
		}
#ifdef HALO_VR
		/* in the eye pass the back buffer is the eyes' swapchain image, and
		the screen's other targets have a layer for each eye (foveated, two:
		xgpu.h) */
		if (vr_gl.pass == HALO_VR_PASS_EYES)
		{
			if (!depth && surface->Data == device.back_buffer.Data)
				return &vr_gl.eye_target;
			layers = xgpu_foveated ? 4 : 2;
		}
#endif
	}
	else if (!depth && surface_is_shadow_map(surface))
	{
		scale[0] = (float)halo_shadow_map_scale();
		scale[1] = scale[0];
		kind = _render_target_shadow_map;
	}
	for (entry = *render_target_bucket(surface->Data); entry; entry = entry->next_in_bucket)
	{
		if (entry->target.data == surface->Data && entry->target.width == width &&
			entry->target.height == height && entry->target.depth == depth &&
			entry->target.scale[0] == scale[0] && entry->target.scale[1] == scale[1] &&
			entry->target.layers == layers && entry->view == (kind == _render_target_screen ? screen_view : 0))
		{
			return render_target_remember(surface, screen, entry);
		}
	}
	entry = calloc(1, sizeof(*entry));
	entry->target.number = next_target_number++;
	entry->target.data = surface->Data;
	entry->target.width = width;
	entry->target.height = height;
	entry->target.depth = depth;
	entry->target.scale[0] = scale[0];
	entry->target.scale[1] = scale[1];
	entry->kind = kind;
	entry->view = kind == _render_target_screen ? screen_view : 0;
	entry->target.pixel_width = (unsigned long)(width * scale[0] + 0.5f);
	entry->target.pixel_height = (unsigned long)(height * scale[1] + 0.5f);
	entry->target.layers = layers;
	entry->screen_buffer = surface->Data == (depth ? device.depth_buffer.Data : device.back_buffer.Data);
	entry->target.format = depth ? XGPU_FORMAT_D24S8 : render_target_screen_colour(entry) ? hdr_back_buffer_format() :
		XGPU_FORMAT_BGRA8;
	description.width = entry->target.pixel_width;
	description.height = entry->target.pixel_height;
	description.depth = depth;
	description.layers = layers;
	description.format = entry->target.format;
	xgpu_backend_target_create(entry->target.number, &description);
	entry->next = render_targets;
	render_targets = entry;
	entry->next_in_bucket = *render_target_bucket(entry->target.data);
	*render_target_bucket(entry->target.data) = entry;
	return entry;
}

struct xgpu_render_target *xgpu_render_target_find(unsigned long data)
{
	struct render_target_entry *entry, *best = NULL;

#ifdef HALO_VR
	if (vr_gl.pass == HALO_VR_PASS_EYES && data == device.back_buffer.Data)
		return &vr_gl.eye_target.target;
#endif
	for (entry = *render_target_bucket(data); entry; entry = entry->next_in_bucket)
	{
		/* (an extra view's screen targets in its own pass alone, and the
		screen's outside them: halo_view_begin) */
		if (entry->kind == _render_target_screen && entry->view != screen_view)
			continue;
		/* (in a VR pass, the screen's targets of its own pass: the eyes' have
		a layer for each eye, the HUD's and the scope's one, each at its
		pass's scale) */
		if (entry->target.data == data && !entry->target.depth && (!best || entry->last_rendered > best->last_rendered)
#ifdef HALO_VR
			&& (vr_gl.pass == HALO_VR_PASS_NONE || entry->kind != _render_target_screen ||
				((entry->target.layers > 1) == (vr_gl.pass == HALO_VR_PASS_EYES) &&
					entry->target.scale[0] == screen_scale[0] && entry->target.scale[1] == screen_scale[1]))
#endif
			)
			best = entry;
	}
	return best ? &best->target : NULL;
}

/* ---------- multisampling

With display.anti_aliasing's multisampling, the back buffer and its depth
buffer are drawn into multisampled storage, and so is any target drawn
together with one of them (the mirror's view goes to the secondary target
with the back buffer's depth buffer): a draw's targets are all multisampled
or none is. The backend resolves a target's multisampled pixels into it
before anything reads it, and puts its pixels into its multisampled storage
when it is made, or made again for other samples. */

/* the samples a pixel of the bound targets (or 1), and whether they have a
color target (bind_targets) */
static int target_samples = 1;
static BOOL target_has_color;
/* ...and, whatever the settings, whether they are multisampled with
multisampling, and the colour target is a screen buffer's (in half floats
with display.hdr): a draw's context (xgpu_manifest_context) */
static BOOL target_multisampled, target_screen_colour;

/* the multisampled pixels of a target drawn into since into its texture */
static void render_target_resolve(struct xgpu_render_target *target)
{
	xgpu_backend_target_resolve(target->number);
}

/* a target drawn into with these samples a pixel from now on (0: none) */
static void render_target_multisample(struct xgpu_render_target *target, int samples)
{
	if (target->samples == samples)
		return;
	target->samples = samples;
	xgpu_backend_target_multisample(target->number, samples);
}

/* counts draws and clears into render targets (xgpu_render_target.written) */
static unsigned long render_target_write_serial;

/* the pixels per unit of the bound targets (render_target_get) */
static float target_scale[2] = { 1.0f, 1.0f };

/* the pixel edge of a coordinate in a target's units, at its scale:
floorf's, without its call (on 32-bit x86 it saves and restores the FPU's
rounding, several times a draw) */
static long scaled_pixel(float coordinate, float scale)
{
	float value = coordinate * scale + 0.5f;
	long pixel = (long)value;

	/* (the conversion is toward zero: a negative value with a fraction
	rounds down one more) */
	if ((float)pixel > value)
		pixel--;
	return pixel;
}

/* ... in the bound targets' units */
static long target_pixel(float coordinate, int axis)
{
	return scaled_pixel(coordinate, target_scale[axis]);
}

#ifdef HALO_VR
static void vr_frame_resume(void);
#endif

/* binds the current targets; returns FALSE if there is nothing to draw
into */
static BOOL bind_targets(BOOL *has_depth)
{
	struct render_target_entry *color, *depth;
	int samples, views = 1, base = 0, hud_group = -1;

#ifdef HALO_VR
	vr_frame_resume();
#endif
	color = render_target_get(device.render_target);
	depth = render_target_get(device.depth_stencil);
	if (depth && !depth->target.depth)
		depth = NULL;
	if (!color && !depth)
		return FALSE;
	if (color)
	{
		color->last_rendered = device.frame + 1;
		color->target.written = ++render_target_write_serial;
	}
	/* viewports and clears are in the targets' units (render_target_get) */
	target_scale[0] = color ? color->target.scale[0] : depth->target.scale[0];
	target_scale[1] = color ? color->target.scale[1] : depth->target.scale[1];
#ifdef HALO_VR
	views = color ? color->target.layers : depth->target.layers;
	if (color && depth && color->target.layers != depth->target.layers)
		depth = NULL;
	vr_gl.left_eye = views > 1 && (vr_gl.visibility_test || vr_gl.eye_alone == 0);
	vr_gl.right_eye = views > 1 && !vr_gl.left_eye && vr_gl.eye_alone == 1;
	vr_gl.multiview = views > 1 && !vr_gl.left_eye && !vr_gl.right_eye;
	if (views == 4 && (vr_gl.left_eye || vr_gl.right_eye))
	{
		/* foveated: that eye's two layers (xgpu.h) */
		base = vr_gl.right_eye ? 2 : 0;
		views = 2;
	}
	else if (vr_gl.left_eye)
	{
		views = -1;
	}
	else if (vr_gl.right_eye)
	{
		views = -2;
	}
	if (vr_gl.multiview || vr_gl.left_eye || vr_gl.right_eye)
		vr_gl.eyes_drawn = TRUE;
	if (vr_gl.pass == HALO_VR_PASS_HUD && color && color->target.data == device.back_buffer.Data)
	{
		vr_gl.hud_drawn = TRUE;
		/* a group of the HUD's into its own target, for its panel */
		if (vr_gl.hud_panels && vr_gl.hud_group > HALO_VR_HUD_GROUP_CENTRE)
			hud_group = vr_gl.hud_group;
	}
#endif
	/* with multisampling, multisampled where either is a screen buffer or
	is multisampled already */
	target_multisampled = (color && (color->screen_buffer || color->target.samples)) ||
		(depth && (depth->screen_buffer || depth->target.samples));
	samples = anti_aliasing() == _anti_aliasing_msaa ? anti_aliasing_samples : 0;
	if (samples && !target_multisampled)
		samples = 0;
	/* (not an extra view's: a few hundred pixels, drawn now and then) */
	if (screen_view)
	{
		samples = 0;
		target_multisampled = FALSE;
	}
#ifdef HALO_VR
	/* (not in the VR mode's passes: the eyes' targets have a layer for each
	eye, and the HUD and its groups are layers of their own) */
	if (vr_gl.pass != HALO_VR_PASS_NONE)
	{
		samples = 0;
		target_multisampled = FALSE;
	}
#endif
	if (color)
		render_target_multisample(&color->target, samples);
	if (depth)
		render_target_multisample(&depth->target, samples);
	xgpu_backend_set_targets(color ? color->target.number : 0, hud_group, depth ? depth->target.number : 0, views, base,
		samples);
	target_samples = samples ? samples : 1;
	*has_depth = depth != NULL;
	target_has_color = color != NULL;
	hdr.target = color && color->target.format == XGPU_FORMAT_RGBA16F;
	target_screen_colour = color && render_target_screen_colour(color);
	return TRUE;
}

/* ---------- device creation */

#ifdef HALO_VR
static void vr_initialize(void);
#endif

static void hdr_read(void);

/* the window's context exists: the backend made ready, and what the
device's settings ask of it */
static void device_backend_ready(void)
{
	int index;

	/* (the input registers' w is 1 until the game sets them) */
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		device.attributes[index][3] = 1.0f;
	xgpu_backend_initialize();
	memory_watch_initialize();
	mirror_page_size_set(memory_watch_page_size());
	debug_settings.skip_vertex_shaders = config_string("debug.gpu_skip_vertex_shaders");
	debug_settings.statistics = config_boolean("debug.gpu_stats");
#ifdef HALO_VR
	vr_initialize();
#endif
	xgpu_backend_invalidate();
	device.gl_ready = TRUE;
	if (anti_aliasing_value < 0)
		anti_aliasing_read();
	else
		anti_aliasing_prepare();
	/* (and display.hdr, which halo_screen_commit reads between frames, now:
	a frame drawn before the next, a map loaded as the game starts,
	prepares its pipelines as they are, its targets none yet) */
	hdr_read();
	/* (the PC menus' files read meanwhile, on a thread of their own, for
	the first map's load: menu_files.c. Not before: their rows for the
	renderer that draws display.hdr, and for the VR mode, are kept or left
	out as the files are read, by the capabilities and the VR mode set
	above) */
	if (!halo_server_mode() && !strcasecmp(config_string("display.menus"), "pc"))
		halo_menus_read_ahead();
}

/* what display.anti_aliasing's value needs of the backend, once it is
ready: multisampling's samples, at most the GPU's, and the passes'
programs, built as the value is chosen rather than in the middle of a frame
(SMAA's are large) */
static void anti_aliasing_prepare(void)
{
	int mode;

	if (!device.gl_ready || anti_aliasing_value < 0)
		return;
	mode = anti_aliasing_values[anti_aliasing_value].mode;
	anti_aliasing_samples = anti_aliasing_values[anti_aliasing_value].samples;
	if (anti_aliasing_samples > xgpu_backend_capabilities.most_samples)
	{
		platform_log("anti-aliasing: the GPU has at most %d samples a pixel", xgpu_backend_capabilities.most_samples);
		anti_aliasing_samples = xgpu_backend_capabilities.most_samples < 2 ? 0 : xgpu_backend_capabilities.most_samples;
	}
	if (mode == _anti_aliasing_fxaa || mode == _anti_aliasing_smaa)
		xgpu_backend_anti_aliasing_prepare(mode == _anti_aliasing_smaa);
	xgpu_backend_invalidate();
}

Direct3D *WINAPI Direct3DCreate8(UINT sdk_version)
{
	(void)sdk_version;
	return (Direct3D *)1;
}

void WINAPI Direct3D_SetPushBufferSize(DWORD push_buffer_size, DWORD segment_count)
{
	(void)push_buffer_size;
	(void)segment_count;
}

/* the vertex constants: the device's copy, read by the dynamic lights and
the trace, and the backend's, which uploads to each program what changed
since it last had them */
static void constants_store(unsigned long first, const void *data, unsigned long count)
{
	memcpy(device.constants[first], data, count * sizeof(device.constants[0]));
	xgpu_backend_set_constants(first, (const float (*)[4])data, count);
}

static void viewport_update_constants(void)
{
	/* Direct3D's reserved constants c[-38] and c[-37] map clip space to
	the screen; zscale is the depth buffer's range */
	float zscale = 16777215.0f;
	unsigned long width, height;
	BOOL depth;

	if (device.depth_stencil)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.depth_stencil->Format, device.depth_stencil->Size, &description);
		if (description.format == D3DFMT_D16 || description.format == D3DFMT_LIN_D16 ||
			description.format == D3DFMT_F16 || description.format == D3DFMT_LIN_F16)
		{
			zscale = 65535.0f;
		}
	}
	(void)width; (void)height; (void)depth;
	device.viewport_scale[0] = device.viewport.Width * 0.5f;
	device.viewport_scale[1] = -(float)device.viewport.Height * 0.5f;
	device.viewport_scale[2] = zscale * (device.viewport.MaxZ - device.viewport.MinZ);
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = device.viewport.X + device.viewport.Width * 0.5f;
	device.viewport_offset[1] = device.viewport.Y + device.viewport.Height * 0.5f;
	device.viewport_offset[2] = zscale * device.viewport.MinZ;
	device.viewport_offset[3] = 0.0f;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 38, device.viewport_scale, 1);
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 37, device.viewport_offset, 1);
	}
}

HRESULT WINAPI Direct3D_CreateDevice(UINT adapter, D3DDEVTYPE device_type, void *unused, DWORD behavior_flags,
	D3DPRESENT_PARAMETERS *presentation_parameters, D3DDevice **returned_device)
{
	unsigned long width, height;
	int index;

	(void)adapter;
	(void)device_type;
	(void)unused;
	(void)behavior_flags;
	if (!device.created)
	{
		memset(&device, 0, sizeof(device));
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		width = device.presentation.BackBufferWidth ? device.presentation.BackBufferWidth : 640;
		height = device.presentation.BackBufferHeight ? device.presentation.BackBufferHeight : 480;
#ifdef HALO_ANDROID
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
#else
		/* room for the widest screen, which F11 can switch to (the screen's
		width, above) */
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, SCREEN_MAXIMUM_WIDTH, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, SCREEN_MAXIMUM_WIDTH, height);
		d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
#endif
		device.render_target = &device.back_buffer;
		device.depth_stencil = &device.depth_buffer;
		for (index = 0; index < D3DTS_MAX; index++)
		{
			device.transforms[index]._11 = 1.0f;
			device.transforms[index]._22 = 1.0f;
			device.transforms[index]._33 = 1.0f;
			device.transforms[index]._44 = 1.0f;
		}
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MaxZ = 1.0f;
		device.next_vertex_shader_id = 1;
		D3D__RenderState[D3DRS_ZENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZWRITEENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		D3D__RenderState[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
		D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
		D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
		D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
		D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
		D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
		D3D__RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILWRITEMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		for (index = 0; index < D3DTSS_MAXSTAGES; index++)
		{
			D3D__TextureState[index][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MINFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MAXANISOTROPY] = 1;
		}
		viewport_update_constants();

		if (!config_boolean("debug.null_renderer") && platform_video_initialize(width, height))
			device_backend_ready();
		else
			platform_log("Direct3D: running without a window (nothing is displayed)");
		device.created = TRUE;
	}
	*returned_device = device_pointer();
	return S_OK;
}

/* whether the frames the game draws are shown nowhere: the device made
with no backend behind it (debug.null_renderer, a dedicated server, or no
display). main.c then draws none (port/linux/game/unseen_frames.c) */
int halo_frames_unseen(void)
{
	return device.created && !device.gl_ready;
}

/* ---------- the menus' pointer */

/* display.picture_position (Android): a picture of another shape than the
window letterboxed at its top or its bottom, not centered (the backend's
presentation, picture_position_send, and the pointer's inverse of it below):
-1 centered, 0 the top, 1 the bottom */
static int picture_place(void)
{
#ifdef HALO_ANDROID
	const char *value = config_string("display.picture_position");

	if (!strcmp(value, "top"))
		return 0;
	if (!strcmp(value, "bottom"))
		return 1;
#endif
	return -1;
}

#ifdef HALO_ANDROID
/* port/android/guest/runtime/guest_host.h */
int host_touch_picture_top(void);

/* the picture's place last sent to the backend (-2: none yet) */
static int picture_top_sent = -2;
#endif

/* where the picture's top is in a window of these pixels, in its pixels
(-1: centered): at the top or the bottom as the setting has it (the bottom
as the window's height, which the present and the pointer bring up to the
picture's lowest place), and always near the top in a
portrait-shaped one (Android), whose space below is the touch controls':
where the overlay wants it, under its top strip (host_touch_picture_top),
or at the top until it says; the game decides it, the backend's present
places the picture so (picture_position_send) */
static int picture_top_in(int pixel_width, int pixel_height)
{
#ifdef HALO_ANDROID
	if (pixel_height > pixel_width)
	{
		int top = host_touch_picture_top();

		return top > 0 ? top : 0;
	}
#endif
	(void)pixel_width;
	switch (picture_place())
	{
	case 0:
		return 0;
	case 1:
		return pixel_height;
	default:
		return -1;
	}
}

/* (between frames: halo_screen_commit) where the picture goes, to the
backend's present, when it changes (the window's shape may have): Android's,
elsewhere it is centered */
static void picture_position_send(void)
{
#ifdef HALO_ANDROID
	int pixel_width = 0, pixel_height = 0, top;

	if (!device.gl_ready)
		return;
	platform_video_drawable_size(&pixel_width, &pixel_height);
	top = picture_top_in(pixel_width, pixel_height);
	if (top != picture_top_sent)
	{
		xgpu_backend_picture_top(top);
		picture_top_sent = top;
	}
#endif
}

/* display.gamma's range (tools/port_settings.py's GAMMA) */
#define GAMMA_LEAST 0.5f
#define GAMMA_MOST 2.0f

/* (between frames: halo_screen_commit) display.gamma, to the backend's
present, when it changes: 0.5 to 2, 1 none. Not the VR build's: its
headset shows the eyes' images, and its window is no SDR picture to curve. */
static void gamma_send(void)
{
#ifndef HALO_VR
	static float sent;
	/* (read again as the settings change: config_changes) */
	static unsigned long read_at = (unsigned long)-1;
	float gamma;

	if (!device.gl_ready || read_at == config_changes())
		return;
	read_at = config_changes();
	gamma = (float)config_real("display.gamma");
	gamma = !(gamma > 0.0f) ? 1.0f : gamma < GAMMA_LEAST ? GAMMA_LEAST : gamma > GAMMA_MOST ? GAMMA_MOST : gamma;
	if (gamma != sent)
	{
		if (sent || gamma != 1.0f)
			platform_log("gamma: %.2f", gamma);
		xgpu_backend_gamma(gamma);
		sent = gamma;
	}
#endif
}

/* a point in the window, as SDL reports it, in the menus' coordinates: the
inverse of the letterboxed display blit at presentation, the screen's
width and the menus' centering (halo_screen_ui_offset); or, not centered,
the screen's (the game's drawing: the scoreboard's); x and y are -1 if there
is no back buffer or window yet */
static void ui_point_from_window_on(float window_x, float window_y, int centered, short *x, short *y)
{
	struct render_target_entry *back_buffer = render_target_get(&device.back_buffer);
	int window_width, window_height, pixel_width, pixel_height, width, height, left, top;
	float screen_x, screen_y;

	*x = *y = -1;
	if (!back_buffer)
		return;
	platform_video_window_size(&window_width, &window_height);
	platform_video_drawable_size(&pixel_width, &pixel_height);
	if (window_width <= 0 || window_height <= 0)
		return;
	width = pixel_width;
	height = (int)((long)pixel_width * back_buffer->target.pixel_height / back_buffer->target.pixel_width);
	if (height > pixel_height)
	{
		height = pixel_height;
		width = (int)((long)pixel_height * back_buffer->target.pixel_width / back_buffer->target.pixel_height);
	}
	left = (pixel_width - width) / 2;
	/* (as the present places it: no lower than the window's bottom) */
	top = picture_top_in(pixel_width, pixel_height);
	top = top < 0 ? (pixel_height - height) / 2 : top < pixel_height - height ? top : pixel_height - height;
	screen_x = (window_x * pixel_width / window_width - left) * (float)back_buffer->target.width / (float)width;
	screen_y = (window_y * pixel_height / window_height - top) * (float)back_buffer->target.height / (float)height;
	if (centered && ui_pointer_view.set)
	{
		/* (the first player's view in split screen, its menus scaled into
		it: port/linux/game/menu_view.c) */
		*x = (short)floorf((screen_x - ui_pointer_view.x0) / ui_pointer_view.scale - ui_pointer_view.left);
		*y = (short)floorf((screen_y - ui_pointer_view.y0) / ui_pointer_view.scale);
		return;
	}
	*x = (short)floorf(screen_x - (centered ? (float)(halo_screen_width() - 640) / 2.0f : 0.0f));
	*y = (short)floorf(screen_y);
}

static void ui_point_from_window(float window_x, float window_y, short *x, short *y)
{
	ui_point_from_window_on(window_x, window_y, TRUE, x, y);
}

#ifdef HALO_ANDROID
int halo_scoreboard_pointer_update(int offered, struct halo_ui_pointer *pointer)
{
	(void)offered;
	(void)pointer;
	return -1;
}
#else
int halo_scoreboard_pointer_update(int offered, struct halo_ui_pointer *pointer)
{
	struct platform_ui_pointer state;

	memset(pointer, 0, sizeof(*pointer));
	if (!platform_scoreboard_pointer(offered != 0, &state) || !device.gl_ready)
		return 0;
	ui_point_from_window_on(state.x, state.y, FALSE, &pointer->x, &pointer->y);
	ui_point_from_window_on(state.click_x, state.click_y, FALSE, &pointer->click_x, &pointer->click_y);
	pointer->moved = state.moved != FALSE;
	pointer->left_clicks = (unsigned char)(state.left_clicks < 255 ? state.left_clicks : 255);
	return 1;
}
#endif

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	struct platform_ui_pointer state;

	platform_menus_set_active(menus_active != 0);
	platform_ui_pointer_set_active(menus_active != 0);
#ifdef HALO_VR
	halo_vr_menus(menus_active != 0);
#endif
	if (!menus_active || !device.gl_ready || !platform_ui_pointer_read(&state))
		return 0;
	memset(pointer, 0, sizeof(*pointer));
	ui_point_from_window(state.x, state.y, &pointer->x, &pointer->y);
	ui_point_from_window(state.click_x, state.click_y, &pointer->click_x, &pointer->click_y);
	pointer->moved = state.moved != FALSE;
	pointer->left_clicks = (unsigned char)(state.left_clicks < 255 ? state.left_clicks : 255);
	pointer->right_clicks = (unsigned char)(state.right_clicks < 255 ? state.right_clicks : 255);
	pointer->wheel_steps = (signed char)(state.wheel_steps < -8 ? -8 : state.wheel_steps > 8 ? 8 : state.wheel_steps);
	pointer->touch = state.touch != FALSE;
	pointer->left_held = state.touch == FALSE ? state.left_held != FALSE : state.finger_held != FALSE;
	pointer->downs = (unsigned char)(state.downs < 255 ? state.downs : 255);
	ui_point_from_window(state.down_x, state.down_y, &pointer->down_x, &pointer->down_y);
	return 1;
}

/* the display's peak and SDR white as the backend last said them, as text
of a kind (XGPU_HDR_DISPLAY_*: Video Setup's DISPLAY PEAK row, its panel,
the log); FALSE where no renderer draws HDR */
static BOOL hdr_display_text(int kind, char *text, unsigned int size)
{
	struct xgpu_hdr_display display;

	if (size)
		text[0] = 0;
	if (!device.gl_ready || !xgpu_backend_capabilities.hdr)
		return FALSE;
	memset(&display, 0, sizeof(display));
	display.hdr = xgpu_backend_display_hdr(&display.headroom) != FALSE;
	display.white_reported = xgpu_backend_display_white(&display.white_nits) != FALSE;
	xgpu_hdr_display_text(&display, kind, text, size);
	return TRUE;
}

/* (between frames: halo_screen_commit) the settings and the display as
they are now; the back buffer made again if the frame is drawn otherwise */
static void hdr_read(void)
{
	/* (the settings, read again as they change: config_changes; the
	display is asked every frame) */
	static unsigned long read_at = (unsigned long)-1;
	static int setting_read, paper_white_read, paper_white_nits_read, ui_nits_read, mode_read;
	float headroom = 1.0f;
	BOOL display = FALSE, active = FALSE;
	int setting, paper_white, paper_white_nits, ui_nits, mode;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		setting_read = hdr_setting();
		paper_white_read = (int)config_integer("display.hdr_paper_white");
		paper_white_nits_read = (int)config_integer("display.hdr_paper_white_nits");
		ui_nits_read = (int)config_integer("display.hdr_ui_nits");
		mode_read = config_string("display.hdr_mode") && !strcmp(config_string("display.hdr_mode"), "punchy") ?
			XGPU_HDR_MODE_PUNCHY : XGPU_HDR_MODE_FAITHFUL;
	}
	setting = setting_read;
	paper_white = paper_white_read;
	paper_white_nits = paper_white_nits_read;
	ui_nits = ui_nits_read;
	mode = mode_read;
	if (!device.gl_ready)
		return;
	if (xgpu_backend_capabilities.hdr)
	{
		display = xgpu_backend_display_hdr(&headroom);
		active = setting == XGPU_HDR_ON || (setting == XGPU_HDR_AUTO && display);
	}
#ifdef HALO_VR
	if (halo_vr_running())
		active = FALSE;
#endif
	/* (display.hdr_paper_white, the percent of the system's white, only
	while paper white is the system's) */
	if (paper_white_nits > 0)
	{
		paper_white_nits = paper_white_nits < XGPU_HDR_PAPER_WHITE_NITS_LEAST ? XGPU_HDR_PAPER_WHITE_NITS_LEAST :
			paper_white_nits > XGPU_HDR_PAPER_WHITE_NITS_MOST ? XGPU_HDR_PAPER_WHITE_NITS_MOST : paper_white_nits;
		paper_white = 100;
	}
	else
		paper_white_nits = 0;
	if (paper_white <= 0)
		paper_white = 100;
	paper_white = paper_white < XGPU_HDR_PAPER_WHITE_LEAST ? XGPU_HDR_PAPER_WHITE_LEAST :
		paper_white > XGPU_HDR_PAPER_WHITE_MOST ? XGPU_HDR_PAPER_WHITE_MOST : paper_white;
	/* (display.hdr_ui_nits: 0, paper white's) */
	ui_nits = ui_nits <= 0 ? 0 : ui_nits < XGPU_HDR_UI_NITS_LEAST ? XGPU_HDR_UI_NITS_LEAST :
		ui_nits > XGPU_HDR_UI_NITS_MOST ? XGPU_HDR_UI_NITS_MOST : ui_nits;
	if (hdr.display_logged != (display ? 1 : 0) && xgpu_backend_capabilities.hdr)
	{
		platform_log("display: %s (headroom %.2f)", display ? "HDR" : "SDR", headroom);
		hdr.display_logged = display ? 1 : 0;
	}
	if (xgpu_backend_capabilities.hdr)
	{
		/* (the peak and SDR white Video Setup shows, logged at the start
		and as they change) */
		char text[sizeof(hdr.display_text)];

		hdr_display_text(XGPU_HDR_DISPLAY_LOG, text, sizeof(text));
		if (hdr.display_text_frames < 1000)
			hdr.display_text_frames++;
		if (strcmp(text, hdr.display_text) && (!hdr.display_text[0] || hdr.display_text_frames >= 30))
		{
			platform_log("%s", text);
			snprintf(hdr.display_text, sizeof(hdr.display_text), "%s", text);
			hdr.display_text_frames = 0;
		}
	}
	if (active != hdr.active)
	{
		platform_log("HDR: %s (display.hdr \"%s\", %s)", active ? "on" : "off", config_string("display.hdr"),
			mode == XGPU_HDR_MODE_PUNCHY ? "punchy" : "faithful");
		hdr.active = active;
		render_targets_evict();
	}
	if (!hdr.sent || mode != hdr.mode || paper_white_nits != hdr.paper_white_nits || paper_white != hdr.paper_white ||
		ui_nits != hdr.ui_nits || active != hdr.active)
	{
		xgpu_backend_hdr(active, mode, paper_white_nits, paper_white, ui_nits);
		hdr.sent = TRUE;
		hdr.mode = mode;
		hdr.paper_white_nits = paper_white_nits;
		hdr.paper_white = paper_white;
		hdr.ui_nits = ui_nits;
	}
}

int halo_hdr_display_text(int words, char *text, unsigned int size)
{
	return hdr_display_text(words ? XGPU_HDR_DISPLAY_WORDS : XGPU_HDR_DISPLAY_VALUE, text, size);
}

/* config_changes() as halo_screen_commit last took the settings up */
static unsigned long screen_settings_taken = (unsigned long)-1;

unsigned long halo_gpu_settings_taken(void)
{
	return screen_settings_taken;
}

/* takes up the display's shape and resolution, or the window's, if they
have changed; between frames, since the game's layout and the targets must
agree for a whole frame. Returns the width the game draws. */
long halo_screen_commit(void)
{
	long width;
	float scale[2];

	/* (display.battery_saver first: on, it reads the settings below at
	cheaper values) */
	battery_saver_update();
	/* (display.movies: the movies copied from the disc image when the data
	root has none) */
	bink_import_update();
	/* (display.anti_aliasing, as Settings or config.toml has it now; it and
	display.supersampling change the scale below) */
	anti_aliasing_read();
	/* (display.hdr, and the display: the back buffer's format) */
	hdr_read();
	/* (the settings taken up: a map's preparation made again under them,
	render_enumerate.c) */
	screen_settings_taken = config_changes();
	/* (display.picture_position) */
	picture_position_send();
	/* (display.gamma) */
	gamma_send();
	/* display.shadow_resolution, if it has changed: the maps' entries at
	the old scale are freed (render_targets_evict), new ones made at the new */
	if (shadow_scale && shadow_scale_read_at != config_changes())
	{
		long shadow = shadow_scale_choose();

		shadow_scale_read_at = config_changes();
		if (shadow != shadow_scale)
		{
			platform_log("shadow maps: %ldx%ld", SHADOW_MAP_SIZE * shadow, SHADOW_MAP_SIZE * shadow);
			shadow_scale = shadow;
			render_targets_evict();
		}
	}
	/* (the extra views' images no longer drawn: halo_view_release) */
	if (views_released)
	{
		views_released = FALSE;
		render_targets_evict();
	}
	if (!screen_width)
		return halo_screen_width();
	screen_mode_choose(&width, scale);
	if (width != screen_width || scale[0] != screen_scale[0] || scale[1] != screen_scale[1])
	{
		screen_mode_log(width, scale);
		screen_width = width;
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
		if (device.created)
		{
			device.presentation.BackBufferWidth = (UINT)width;
			d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, (unsigned long)width, SCREEN_HEIGHT);
			d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, (unsigned long)width, SCREEN_HEIGHT);
		}
		/* (the entries of the old size and scale, which nothing finds now) */
		render_targets_evict();
	}
	return screen_width;
}

ULONG WINAPI D3DDevice_Release(void)
{
	return 1;
}

void WINAPI D3DDevice_GetDeviceCaps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxVolumeExtent = 512;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xfffff;
	caps->MaxVertexIndex = 0xffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 192;
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxPixelShaderValue = 1.0f;
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	(void)back_buffer;
	(void)type;
	/* like Direct3D, the caller gets a reference it must release */
	device.back_buffer.Common++;
	*result = &device.back_buffer;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **result)
{
	*result = device.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

static BOOL trace_frame(void);

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (trace_frame())
		platform_log("set render target %08lx depth %08lx", render_target ? (unsigned long)render_target->Data : 0,
			depth_stencil ? (unsigned long)depth_stencil->Data : 0);
	stats.target_changes++;
	if (render_target)
		device.render_target = render_target;
	device.depth_stencil = depth_stencil;
	/* like Direct3D, reset the viewport to the whole new target */
	if (device.render_target)
	{
		unsigned long width, height;
		BOOL depth;

		surface_dimensions(device.render_target, &width, &height, &depth);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	device.viewport = *viewport;
	viewport_update_constants();
}

void d3d8_viewport_depth_range(float minimum, float maximum, float previous[2])
{
	if (previous)
	{
		previous[0] = device.viewport.MinZ;
		previous[1] = device.viewport.MaxZ;
	}
	device.viewport.MinZ = minimum;
	device.viewport.MaxZ = maximum;
	viewport_update_constants();
}

void d3d8_texture_region(unsigned long stage, const float *region)
{
	if (stage >= D3DTSS_MAXSTAGES)
		return;
	if (region && region[0] > 0.0f && region[1] > 0.0f)
		memcpy(device.texture_regions[stage], region, sizeof(device.texture_regions[stage]));
	else
		memset(device.texture_regions[stage], 0, sizeof(device.texture_regions[stage]));
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		device.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = device.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	device.shader_constant_mode = mode;
	viewport_update_constants();
}

/* ---------- GPU synchronisation: GL keeps its own ordering */

BOOL WINAPI D3DDevice_IsBusy(void)
{
	return FALSE;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
	if (device.gl_ready)
		xgpu_backend_flush();
}

void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE type, D3DCALLBACK callback, DWORD context)
{
	(void)type;
	/* the "GPU" reaches the callback immediately */
	if (callback)
		callback(context);
}
/* ---------- visibility (occlusion) tests

The game's slots; the backend counts (xgpu_backend.h), in the game's
pixels: it is given the target's pixels for each of the game's, the views
the test is drawn into and the samples a pixel. */

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	if (!device.gl_ready || device.visibility_test_active)
		return;
	device.visibility_test_active = TRUE;
#ifdef HALO_VR
	vr_gl.visibility_test = vr_gl.pass == HALO_VR_PASS_EYES && !SAMPLE_COUNTING;
	if (vr_gl.visibility_test)
	{
		BOOL has_depth;

		/* the query begins with the left eye's layer bound, outside any
		multiview pass (xgpu.h, XGPU_MULTIVIEW_LEFT_EYE) */
		bind_targets(&has_depth);
	}
#endif
	xgpu_backend_visibility_begin();
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	int views = 1;

	if (!device.gl_ready || !device.visibility_test_active)
		return S_OK;
	device.visibility_test_active = FALSE;
#ifdef HALO_VR
	vr_gl.visibility_test = FALSE;
	/* (drawn into both eyes, the test counts each pixel twice) */
	if (vr_gl.multiview)
		views = 2;
#endif
	/* (numbered: the count reaches the game frames later, and the slot may
	have been tested for something else since: the number says which
	test the count is of, D3DDevice_GetVisibilityTestResult) */
	device.visibility_test_number = (device.visibility_test_number + 1) & 0xffffffffUL;
	if (!device.visibility_test_number)
		device.visibility_test_number = 1;
	xgpu_backend_visibility_end(index % XGPU_VISIBILITY_TEST_SLOTS, target_scale[0] * target_scale[1], views,
		target_samples, device.visibility_test_number);
	return S_OK;
}

/* The latest count known for the slot, never pending (the Xbox's waited
for the test, D3DERR_TESTINCOMPLETE; here the counts are the GPU's of a
frame or more before). time_stamp: the number of the test the count is of
(d3d8_visibility_test_number's; 0: none yet), so that a caller whose slot
is tested for one thing frame after frame can tell a count of its own test
from an older one of the slot */
HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	UINT count = 0;
	unsigned long test = 0;

	if (device.gl_ready)
		count = xgpu_backend_visibility_result(index % XGPU_VISIBILITY_TEST_SLOTS, &test);
	if (time_stamp)
		*time_stamp = test;
	if (result)
		*result = count;
	return S_OK;
}

unsigned long d3d8_visibility_test_number(void)
{
	return device.visibility_test_number;
}

/* ---------- render and texture stage state */

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* callers also store the value in D3D__RenderState themselves */
	(void)method;
	(void)value;
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value);

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

/* As the Xbox's D3D8 does it: a z bias is a polygon offset of -bias depth
units plus -bias/4 times the polygon's depth slope, enabled for every fill
mode. Without the slope term, decals (biased by 8) fight with the surface
under them wherever it is seen at an angle. */
void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value)
{
	float offset = -(float)value;
	float slope = offset * 0.25f;
	DWORD enable = value != 0;

	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, sizeof(slope));
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &offset, sizeof(offset));
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_ZBIAS] = value;
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { D3D__RenderState[state] = value; }

COMPLEX_RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES)
COMPLEX_RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
COMPLEX_RENDER_STATE(FogColor, D3DRS_FOGCOLOR)
COMPLEX_RENDER_STATE(FillMode, D3DRS_FILLMODE)
COMPLEX_RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE)
COMPLEX_RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
COMPLEX_RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
COMPLEX_RENDER_STATE(ZEnable, D3DRS_ZENABLE)
COMPLEX_RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE)
COMPLEX_RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
COMPLEX_RENDER_STATE(FrontFace, D3DRS_FRONTFACE)
COMPLEX_RENDER_STATE(CullMode, D3DRS_CULLMODE)
COMPLEX_RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR)
COMPLEX_RENDER_STATE(LogicOp, D3DRS_LOGICOP)
COMPLEX_RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
COMPLEX_RENDER_STATE(MultiSampleType, D3DRS_MULTISAMPLETYPE)
COMPLEX_RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
COMPLEX_RENDER_STATE(LineWidth, D3DRS_LINEWIDTH)
COMPLEX_RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
COMPLEX_RENDER_STATE(YuvEnable, D3DRS_YUVENABLE)
COMPLEX_RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
COMPLEX_RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE)
COMPLEX_RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
COMPLEX_RENDER_STATE(RopZRead, D3DRS_ROPZREAD)
COMPLEX_RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)

void D3DFASTCALL D3DDevice_SetTextureState_Deferred(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_TEXCOORDINDEX] = value;
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_BORDERCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_COLORKEYCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.textures[stage] = texture;
}

/* (xgpu.h) an MCC texture's header about to be freed: no stage keeps it */
void xgpu_mcc_texture_unbind(const DWORD *resource)
{
	unsigned long stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		if ((const DWORD *)device.textures[stage] == resource)
			device.textures[stage] = NULL;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.palettes[stage] = palette;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	/* the definition's members are the pixel shader render states */
	if (!definition)
		return;
	memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs));
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = definition->PSFinalCombinerInputsABCD;
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = definition->PSFinalCombinerInputsEFG;
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0));
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1));
	memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs));
	memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs));
	D3D__RenderState[D3DRS_PSCOMPAREMODE] = definition->PSCompareMode;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = definition->PSFinalCombinerConstant0;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = definition->PSFinalCombinerConstant1;
	memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs));
	D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = definition->PSCombinerCount;
	D3D__RenderState[D3DRS_PSTEXTUREMODES] = definition->PSTextureModes;
	D3D__RenderState[D3DRS_PSDOTMAPPING] = definition->PSDotMapping;
	D3D__RenderState[D3DRS_PSINPUTTEXTURE] = definition->PSInputTexture;
}

/* ---------- vertex shaders */

static unsigned long vertex_type_bytes(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return 4;
	case D3DVSDT_FLOAT2: return 8;
	case D3DVSDT_FLOAT3: return 12;
	case D3DVSDT_FLOAT4: return 16;
	case D3DVSDT_D3DCOLOR: return 4;
	case D3DVSDT_SHORT1: return 2;
	case D3DVSDT_SHORT2: return 4;
	case D3DVSDT_SHORT3: return 6;
	case D3DVSDT_SHORT4: return 8;
	case D3DVSDT_NORMSHORT1: return 2;
	case D3DVSDT_NORMSHORT2: return 4;
	case D3DVSDT_NORMSHORT3: return 6;
	case D3DVSDT_NORMSHORT4: return 8;
	case D3DVSDT_NORMPACKED3: return 4;
	case D3DVSDT_PBYTE1: return 1;
	case D3DVSDT_PBYTE2: return 2;
	case D3DVSDT_PBYTE3: return 3;
	case D3DVSDT_PBYTE4: return 4;
	case D3DVSDT_FLOAT2H: return 12;
	default: return 0;
	}
}

/* a declaration's element type in the backend's terms */
static unsigned char vertex_format(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return XGPU_VERTEX_FLOAT1;
	case D3DVSDT_FLOAT2: return XGPU_VERTEX_FLOAT2;
	case D3DVSDT_FLOAT3: return XGPU_VERTEX_FLOAT3;
	case D3DVSDT_FLOAT4: return XGPU_VERTEX_FLOAT4;
	case D3DVSDT_D3DCOLOR: return XGPU_VERTEX_COLOR;
	case D3DVSDT_SHORT1: return XGPU_VERTEX_SHORT1;
	case D3DVSDT_SHORT2: return XGPU_VERTEX_SHORT2;
	case D3DVSDT_SHORT3: return XGPU_VERTEX_SHORT3;
	case D3DVSDT_SHORT4: return XGPU_VERTEX_SHORT4;
	case D3DVSDT_NORMSHORT1: return XGPU_VERTEX_NORMSHORT1;
	case D3DVSDT_NORMSHORT2: return XGPU_VERTEX_NORMSHORT2;
	case D3DVSDT_NORMSHORT3: return XGPU_VERTEX_NORMSHORT3;
	case D3DVSDT_NORMSHORT4: return XGPU_VERTEX_NORMSHORT4;
	case D3DVSDT_NORMPACKED3: return XGPU_VERTEX_PACKED3;
	case D3DVSDT_PBYTE1: return XGPU_VERTEX_BYTE1;
	case D3DVSDT_PBYTE2: return XGPU_VERTEX_BYTE2;
	case D3DVSDT_PBYTE3: return XGPU_VERTEX_BYTE3;
	case D3DVSDT_PBYTE4: return XGPU_VERTEX_BYTE4;
	case D3DVSDT_FLOAT2H: return XGPU_VERTEX_FLOAT2H;
	case D3DVSDT_NONE: return XGPU_VERTEX_NONE_FORMAT;
	/* (as four floats, as before) */
	default: return XGPU_VERTEX_FLOAT4;
	}
}

static void parse_declaration(struct vertex_shader_object *object, const DWORD *declaration)
{
	unsigned long stream = 0;
	unsigned long offsets[16] = { 0 };

	for (; declaration && *declaration != D3DVSD_END(); declaration++)
	{
		DWORD token = *declaration;
		unsigned long token_type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;

		switch (token_type)
		{
		case D3DVSD_TOKEN_STREAM:
			stream = token & D3DVSD_STREAMNUMBERMASK;
			break;
		case D3DVSD_TOKEN_STREAMDATA:
			if (token & D3DVSD_DATALOADTYPEMASK)
			{
				/* skip: the count is in dwords, or in bytes with bit 27 */
				unsigned long count = (token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT;

				offsets[stream] += (token & 0x08000000) ? count : count * 4;
			}
			else if (object->element_count < XGPU_VERTEX_ATTRIBUTE_COUNT)
			{
				struct xgpu_vertex_element *element = &object->elements[object->element_count];
				unsigned long type = (token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT;

				object->types[object->element_count++] = (unsigned char)type;
				element->reg = (unsigned char)(token & D3DVSD_VERTEXREGMASK);
				element->stream = (unsigned char)stream;
				element->format = vertex_format(type);
				element->bytes = (unsigned char)vertex_type_bytes(type);
				element->offset = (unsigned short)offsets[stream];
				offsets[stream] += element->bytes;
				if (type == D3DVSDT_NORMPACKED3)
					object->packed_mask |= 1UL << element->reg;
			}
			break;
		case D3DVSD_TOKEN_CONSTMEM:
			declaration += ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT) * 4;
			break;
		case D3DVSD_TOKEN_EXT:
			declaration += (token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
			break;
		default:
			break;
		}
	}
}

HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *declaration, CONST DWORD *function, DWORD *handle, DWORD usage)
{
	struct vertex_shader_object *object = calloc(1, sizeof(*object));

	(void)usage;
	if (!object)
		return E_OUTOFMEMORY;
	object->signature = VERTEX_SHADER_SIGNATURE;
	object->id = device.next_vertex_shader_id++;
	if (function)
	{
		/* header: program type in the low word, instruction count in the high */
		object->instruction_count = function[0] >> 16;
		object->program = malloc(sizeof(*object->program));
		if (!object->program)
		{
			free(object);
			return E_OUTOFMEMORY;
		}
		nv2a_vertex_program_decode(function + 1, object->instruction_count, object->program);
		xgpu_backend_vertex_program(object->id, object->program);
		xgpu_manifest_note_vertex_program(object->program->hash, (const uint32_t *)(function + 1),
			object->instruction_count);
	}
	parse_declaration(object, declaration);
	journal_vertex_shader(object);
	/* odd values are FVF codes; programmable shader handles are even */
	*handle = (DWORD)object;
	return S_OK;
}

static struct vertex_shader_object *vertex_shader_from_handle(DWORD handle)
{
	struct vertex_shader_object *object = (struct vertex_shader_object *)handle;

	if (!handle || (handle & 1) || object->signature != VERTEX_SHADER_SIGNATURE)
		return NULL;
	return object;
}

/* the game names its model lighting programs as it creates them
(rasterizer_xbox_vertex_shaders_initialize.c), so that their draws can be lit
for each pixel (display.per_pixel_lighting); one whose lighting is not as
the pixel shader computes it stays lit for each vertex */
void halo_vertex_shader_lighting(unsigned long handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle((DWORD)handle);

	if (!object || !object->program)
		return;
	if (!nv2a_vertex_shader_lighting(object->program, &object->lighting))
	{
		memset(&object->lighting, 0, sizeof(object->lighting));
		platform_log("GPU: vertex shader %lu is not lit as the pixel shader would light it: lit for each vertex",
			object->id);
		return;
	}
	xgpu_backend_vertex_lighting(object->program->hash, &object->lighting);
	/* (a shader the game lights so is another than its program's twin) */
	journal_vertex_shader_identify(object);
}

void WINAPI D3DDevice_DeleteVertexShader(DWORD handle)
{
	/* programs stay cached; the object is small */
	(void)handle;
}

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
	{
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
	}
	else if (!handle)
	{
		/* (0, the fixed-function pipeline, which these renderers do not
		have: OpenCE's loading screen draws with it. Its draws are left out
		(prepare_draw) until a program is set, rather than drawn with the
		program before) */
		device.vertex_shader = NULL;
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_slots[address] = vertex_shader_from_handle(handle);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
		device.vertex_shader = object;
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_address = address;
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	*size = object ? object->instruction_count : 0;
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + XGPU_VERTEX_CONSTANT_BIAS;

	if (first < 0 || first >= XGPU_VERTEX_CONSTANT_COUNT)
		return;
	if (first + (long)constant_count > XGPU_VERTEX_CONSTANT_COUNT)
		constant_count = XGPU_VERTEX_CONSTANT_COUNT - first;
	constants_store((unsigned long)first, constant_data, constant_count);
}

/* the program that runs: the one loaded at the selected address, else the
current shader's own */
static struct vertex_shader_object *current_program(void)
{
	struct vertex_shader_object *program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}
/* ---------- per-draw state */

static unsigned long stage_texture_mode(int stage)
{
	return (D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

/* ---------- samplers

A stage's sampler from its texture stage state: the backend makes a
sampler object of each one the game uses, once (the game uses a few
dozen). */

static unsigned char sampler_filter(DWORD filter)
{
	switch (filter)
	{
	case D3DTEXF_NONE: return XGPU_FILTER_NONE;
	case D3DTEXF_POINT: return XGPU_FILTER_POINT;
	case D3DTEXF_ANISOTROPIC: return XGPU_FILTER_ANISOTROPIC;
	default: return XGPU_FILTER_LINEAR;
	}
}

static unsigned char sampler_address(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return XGPU_ADDRESS_MIRROR;
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_CLAMPTOEDGE: return XGPU_ADDRESS_CLAMP;
	case D3DTADDRESS_BORDER: return XGPU_ADDRESS_BORDER;
	default: return XGPU_ADDRESS_WRAP;
	}
}

/* display.anisotropic_filtering: the most texels along a pixel's footprint
that a texture with mip levels is sampled with where the game filters it
linearly, 0 or 1 for the game's own filtering. The Xbox game asks for none
(it never sets D3DTEXF_ANISOTROPIC or D3DTSS_MAXANISOTROPY), so the floors,
walls and terrain seen at a grazing angle blur toward their smaller mip
levels. At most the GPU's most (16 on the desktop's), and none on GLES
without GL_EXT_texture_filter_anisotropic. */
static long anisotropic_level = -1;
static unsigned long anisotropic_level_read_at;

static DWORD anisotropic_filtering(void)
{
	if (anisotropic_level < 0 || anisotropic_level_read_at != config_changes())
	{
		long level = config_integer("display.anisotropic_filtering");
		long maximum = xgpu_backend_capabilities.most_anisotropy;

		anisotropic_level_read_at = config_changes();
		if (level > maximum)
			level = maximum;
		if (level < 1)
			level = 1;
		if (level != anisotropic_level)
			platform_log("anisotropic filtering: %ldx (the GPU's most %dx)", level, (int)maximum);
		anisotropic_level = level;
	}
	return (DWORD)anisotropic_level;
}

/* hires: a high-res HUD texture (hud_hires.h), drawn smaller than it is, so
filtered and from its mip levels whatever the game asks: the HUD's meters are
point sampled for one player, to keep the Xbox bitmaps' texels sharp.
rendered: a render target the game draws into */
static void sampler_describe(int stage, BOOL mipmapped, BOOL hires, BOOL rendered, struct xgpu_sampler *sampler)
{
	DWORD *state = D3D__TextureState[stage];
	DWORD minify = hires ? D3DTEXF_LINEAR : state[D3DTSS_MINFILTER];
	DWORD mip = hires ? D3DTEXF_LINEAR : mipmapped ? state[D3DTSS_MIPFILTER] : D3DTEXF_NONE;
	DWORD anisotropy = state[D3DTSS_MAXANISOTROPY];

	/* (display.anisotropic_filtering, over the game's linear filtering of
	the maps' textures with mip levels; not of what the game renders, the
	water's ripples, whose finer levels seen far off are noise) */
	if (!hires && mipmapped && !rendered && mip != D3DTEXF_NONE &&
		(minify == D3DTEXF_LINEAR || minify == D3DTEXF_ANISOTROPIC) && anisotropic_filtering() > 1)
	{
		minify = D3DTEXF_ANISOTROPIC;
		if (anisotropy < anisotropic_filtering())
			anisotropy = anisotropic_filtering();
	}
	memset(sampler, 0, sizeof(*sampler));
	sampler->minify = sampler_filter(minify);
	sampler->mip = sampler_filter(mip);
	sampler->magnify = sampler_filter(hires ? D3DTEXF_LINEAR : state[D3DTSS_MAGFILTER]);
	sampler->address[0] = sampler_address(state[D3DTSS_ADDRESSU]);
	sampler->address[1] = sampler_address(state[D3DTSS_ADDRESSV]);
	if (device.texture_regions[stage][0] > 0.0f)
		sampler->address[0] = sampler->address[1] = XGPU_ADDRESS_REGION;
	sampler->address[2] = sampler_address(state[D3DTSS_ADDRESSW]);
	sampler->lod_bias = hires ? 0.0f : dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	sampler->first_level = hires ? 0 : state[D3DTSS_MAXMIPLEVEL];
	sampler->anisotropy = anisotropy;
	sampler->border_color = state[D3DTSS_BORDERCOLOR];
}

/* ---------- render targets' lifetimes */

/* an entry render_target_get no longer finds: the screen's targets at
another scale or screen width than now, the shadow maps at another
display.shadow_resolution */
static BOOL render_target_stale(const struct render_target_entry *entry)
{
	switch (entry->kind)
	{
	case _render_target_screen:
		/* (an extra view's: gone, or of another size) */
		if (entry->view && entry->screen_buffer && (entry->target.width != views[entry->view].width ||
				entry->target.height != views[entry->view].height))
			return TRUE;
		if (entry->view && !views[entry->view].width)
			return TRUE;
		return entry->target.scale[0] != screen_scale[0] || entry->target.scale[1] != screen_scale[1] ||
			(!entry->view && entry->target.height == SCREEN_HEIGHT &&
				entry->target.width != (unsigned long)halo_screen_width()) ||
			(render_target_screen_colour(entry) && entry->target.format != hdr_back_buffer_format());
	case _render_target_shadow_map:
		return entry->target.scale[0] != (float)halo_shadow_map_scale();
	default:
		return FALSE;
	}
}

/* frees an entry taken out of render_targets: its storage, and what
remembered it */
static void render_target_delete(struct render_target_entry *entry)
{
	struct render_target_entry **link;

	for (link = render_target_bucket(entry->target.data); *link != entry; link = &(*link)->next_in_bucket)
		;
	*link = entry->next_in_bucket;
	xgpu_backend_target_delete(entry->target.number);
	free(entry);
}

/* (between frames, when the screen's or the shadow maps' scale has
changed: halo_screen_commit) frees the entries render_target_get no longer
finds, which a frame at the old scale left */
static void render_targets_evict(void)
{
	struct render_target_entry *entry, **link = &render_targets;
	BOOL any = FALSE;

	while ((entry = *link) != NULL)
	{
		if (render_target_stale(entry))
		{
			*link = entry->next;
			render_target_delete(entry);
			any = TRUE;
		}
		else
		{
			link = &entry->next;
		}
	}
	if (any)
	{
		memset(recent_render_targets, 0, sizeof(recent_render_targets));
		xgpu_backend_invalidate();
	}
}

/* ---------- textures

What each stage samples: nothing, an Xbox texture (the backend's cache
makes it from guest memory), or a render target, with its mip levels where
the game renders each level as a surface of its own (the water's ripple
map: the backend copies the levels' targets into a mipmapped texture,
again only once a level's target has been drawn into since). */

static void bind_textures(struct nv2a_pixel_shader_key *key, float texture_scale[4][4])
{
	struct xgpu_texture_source sources[D3DTSS_MAXSTAGES];
	int stage;

	memset(sources, 0, sizeof(sources));
	if (preparation.active)
	{
		memset(&preparation.key, 0, sizeof(preparation.key));
		preparation.trackable = TRUE;
	}
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct xgpu_texture_source *source = &sources[stage];
		struct xgpu_render_target *target;
		struct xgpu_texture_description description;
		int kind = XGPU_TEXTURE_KIND_2D;

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		/* (a stage that does not sample: a texture an earlier draw left there
		is not the draw's; DOT_ZW, 0x0a, computes the depth alone) */
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x0a || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			source->source = XGPU_SOURCE_NONE;
			source->sampler_type = key->sampler_type[stage];
			source->sampled = key->sampler_type[stage] != _xgpu_sampler_none;
			continue;
		}
		target = xgpu_render_target_find(texture->Data);
		/* (an extra view's image in stage 0's place: halo_view_sample) */
		if (stage == 0 && sampled_view)
		{
			struct render_target_entry *view;

			target = NULL;
			for (view = render_targets; view; view = view->next)
			{
				if (view->view == sampled_view && view->screen_buffer && !view->target.depth)
					target = &view->target;
			}
		}
		if (target)
		{
			xgpu_texture_describe(texture->Format, texture->Size, &description);
			source->source = XGPU_SOURCE_TARGET;
			source->target = target->number;
			source->written = target->written;
			/* (the target drawn into, where the backend cannot sample it) */
			if (!xgpu_backend_capabilities.sample_drawn_target)
			{
				struct render_target_entry *drawn = render_target_get(device.render_target);

				source->drawn = drawn ? drawn->target.number : 0;
			}
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)target->width;
				texture_scale[stage][1] = 1.0f / (float)target->height;
			}
			if (!description.linear && !description.cube_map && description.levels > 1 &&
				target->width == description.width && target->height == description.height &&
				target->layers == 1)
			{
				/* the levels the game has rendered, each a target of its own
				the level's size */
				unsigned long level;

				source->data = texture->Data;
				source->width = description.width;
				source->height = description.height;
				source->levels = description.levels;
				for (level = 0; level < description.levels && level < XGPU_MIP_LEVELS; level++)
				{
					unsigned long width = description.width >> level ? description.width >> level : 1;
					unsigned long height = description.height >> level ? description.height >> level : 1;
					struct xgpu_render_target *level_target =
						xgpu_render_target_find(texture->Data + xgpu_texture_level_offset(&description, level));

					if (!level_target || level_target->width != width || level_target->height != height ||
						level_target->pixel_width != width || level_target->pixel_height != height)
						break;
					source->level_targets[level] = level_target->number;
					source->level_written[level] = level_target->written;
					source->rendered_levels++;
				}
			}
			else
			{
				description.levels = 1;
			}
#ifdef HALO_VR
			if (target->layers > 1)
				kind = XGPU_TEXTURE_KIND_2D_ARRAY;
#endif
		}
		else
		{
			const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
				(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;

			source->source = XGPU_SOURCE_TEXTURE;
			source->resource = (DWORD)texture;
			source->palette = (DWORD)palette;
			xgpu_backend_texture_describe(source->resource, source->palette, &description, &kind);
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
		}
		sampler_describe(stage, description.levels > 1, description.hires, target != NULL, &source->sampler);
		if (device.texture_regions[stage][0] > 0.0f)
			memcpy(texture_scale[stage], device.texture_regions[stage], sizeof(texture_scale[stage]));
		/* (the high-res text's distance fields: text_hires.c) */
		if (description.hires_msdf)
		{
			source->sampler.address[0] = source->sampler.address[1] = XGPU_ADDRESS_CLAMP;
			source->sampler.address[2] = XGPU_ADDRESS_MSDF;
		}
		if (stage == 0)
		{
			key->coverage_alpha = description.hires_coverage != FALSE;
			key->point_threshold = description.hires_point_threshold != FALSE;
		}
		key->sampler_type[stage] = kind == XGPU_TEXTURE_KIND_CUBE ? _xgpu_sampler_cube :
			kind == XGPU_TEXTURE_KIND_3D ? _xgpu_sampler_3d :
			kind == XGPU_TEXTURE_KIND_2D_ARRAY ? _xgpu_sampler_2d_layered : _xgpu_sampler_2d;
		source->sampler_type = key->sampler_type[stage];
		source->sampled = TRUE;
	}
	xgpu_backend_set_textures(sources);
	xgpu_manifest_note_textures(sources);
	if (preparation.active)
	{
		memcpy(preparation.key.textures, sources, sizeof(sources));
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			D3DBaseTexture *texture = device.textures[stage];
			struct xgpu_texture_description description;
			unsigned long address, bytes;

			if (sources[stage].source != XGPU_SOURCE_TEXTURE)
				continue;
			xgpu_texture_describe(texture->Format, texture->Size, &description);
			address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(texture->Data);
			bytes = xgpu_texture_face_size(&description) * (description.cube_map ? 6 : 1);
			preparation.key.texture_data[stage] = texture->Data;
			preparation.key.texture_format[stage] = texture->Format;
			preparation.key.texture_size[stage] = texture->Size;
			if (!bytes || !platform_is_contiguous((void *)address) ||
				bytes > PLATFORM_CONTIGUOUS_SIZE - (address - PLATFORM_CONTIGUOUS_BASE))
			{
				preparation.trackable = FALSE;
				continue;
			}
			memory_watch_protect(address, bytes);
			preparation.key.texture_generation[stage] = memory_watch_generation(address, bytes);
			if (sources[stage].palette)
			{
				address = sources[stage].palette;
				if (!platform_is_contiguous((void *)address) ||
					256 * sizeof(D3DCOLOR) > PLATFORM_CONTIGUOUS_SIZE - (address - PLATFORM_CONTIGUOUS_BASE))
					preparation.trackable = FALSE;
				else
				{
					memory_watch_protect(address, 256 * sizeof(D3DCOLOR));
					preparation.key.palette_generation[stage] =
						memory_watch_generation(address, 256 * sizeof(D3DCOLOR));
				}
			}
		}
	}
}

/* ---------- the pipeline key and dynamic state (xgpu_backend.h)

The front end's description of a draw's fixed-function state, from the
game's render states: the key holds what a pipeline object bakes in, the
dynamic state the rest. The Xbox's values are GL's enumerants, but the
description is neither's. */

static unsigned char compare_function(DWORD function)
{
	/* (0, as a render state left at zero, never passes, as GL_NEVER) */
	if (function >= D3DCMP_NEVER && function <= D3DCMP_ALWAYS)
		return (unsigned char)(XGPU_COMPARE_NEVER + (function - D3DCMP_NEVER));
	return function ? XGPU_COMPARE_ALWAYS : XGPU_COMPARE_NEVER;
}

static unsigned char blend_factor(DWORD factor)
{
	switch (factor)
	{
	case D3DBLEND_ZERO: return XGPU_BLEND_ZERO;
	case D3DBLEND_SRCCOLOR: return XGPU_BLEND_SOURCE_COLOR;
	case D3DBLEND_INVSRCCOLOR: return XGPU_BLEND_ONE_MINUS_SOURCE_COLOR;
	case D3DBLEND_SRCALPHA: return XGPU_BLEND_SOURCE_ALPHA;
	case D3DBLEND_INVSRCALPHA: return XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA;
	case D3DBLEND_DESTALPHA: return XGPU_BLEND_DESTINATION_ALPHA;
	case D3DBLEND_INVDESTALPHA: return XGPU_BLEND_ONE_MINUS_DESTINATION_ALPHA;
	case D3DBLEND_DESTCOLOR: return XGPU_BLEND_DESTINATION_COLOR;
	case D3DBLEND_INVDESTCOLOR: return XGPU_BLEND_ONE_MINUS_DESTINATION_COLOR;
	case D3DBLEND_SRCALPHASAT: return XGPU_BLEND_SOURCE_ALPHA_SATURATE;
	case D3DBLEND_CONSTANTCOLOR: return XGPU_BLEND_CONSTANT_COLOR;
	case D3DBLEND_INVCONSTANTCOLOR: return XGPU_BLEND_ONE_MINUS_CONSTANT_COLOR;
	case D3DBLEND_CONSTANTALPHA: return XGPU_BLEND_CONSTANT_ALPHA;
	case D3DBLEND_INVCONSTANTALPHA: return XGPU_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	default: return XGPU_BLEND_ONE;
	}
}

static unsigned char blend_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return XGPU_BLEND_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return XGPU_BLEND_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return XGPU_BLEND_MIN;
	case D3DBLENDOP_MAX: return XGPU_BLEND_MAX;
	/* (and the signed add, which the NV2A has and GL has not) */
	default: return XGPU_BLEND_ADD;
	}
}

static unsigned char stencil_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DSTENCILOP_ZERO: return XGPU_STENCIL_ZERO;
	case D3DSTENCILOP_REPLACE: return XGPU_STENCIL_REPLACE;
	case D3DSTENCILOP_INCRSAT: return XGPU_STENCIL_INCREMENT_CLAMP;
	case D3DSTENCILOP_DECRSAT: return XGPU_STENCIL_DECREMENT_CLAMP;
	case D3DSTENCILOP_INVERT: return XGPU_STENCIL_INVERT;
	case D3DSTENCILOP_INCR: return XGPU_STENCIL_INCREMENT_WRAP;
	case D3DSTENCILOP_DECR: return XGPU_STENCIL_DECREMENT_WRAP;
	default: return XGPU_STENCIL_KEEP;
	}
}

/* the draw's fixed-function state into its pipeline key (the shaders' and
the targets' parts are the caller's) and its dynamic state; has_depth: the
targets have a depth buffer */
static void pipeline_state_describe(BOOL has_depth, struct xgpu_pipeline_key *key, struct xgpu_dynamic_state *dynamic)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];

	dynamic->viewport[0] = target_pixel((float)device.viewport.X, 0);
	dynamic->viewport[1] = target_pixel((float)device.viewport.Y, 1);
	dynamic->viewport[2] = target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - dynamic->viewport[0];
	dynamic->viewport[3] = target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - dynamic->viewport[1];
	/* the game never issues a scissor rectangle, and the NV2A scissor register
	defaults to the viewport, so fragment clipping follows the viewport: this is
	what keeps a split-screen window's geometry from bleeding across the divider */
	memcpy(dynamic->scissor, dynamic->viewport, sizeof(dynamic->scissor));
	dynamic->depth_range[0] = device.viewport.MinZ;
	dynamic->depth_range[1] = device.viewport.MaxZ;

	key->depth_test = has_depth && rs[D3DRS_ZENABLE];
	if (key->depth_test)
		key->depth_function = compare_function(rs[D3DRS_ZFUNC]);
	key->depth_write = key->depth_test && rs[D3DRS_ZWRITEENABLE];

	key->stencil_test = has_depth && rs[D3DRS_STENCILENABLE];
	if (key->stencil_test)
	{
		key->stencil_function = compare_function(rs[D3DRS_STENCILFUNC]);
		key->stencil_read_mask = rs[D3DRS_STENCILMASK];
		key->stencil_fail = stencil_operation(rs[D3DRS_STENCILFAIL]);
		key->stencil_depth_fail = stencil_operation(rs[D3DRS_STENCILZFAIL]);
		key->stencil_pass = stencil_operation(rs[D3DRS_STENCILPASS]);
		key->stencil_write_mask = rs[D3DRS_STENCILWRITEMASK];
		dynamic->stencil_reference = rs[D3DRS_STENCILREF];
	}
	key->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	if (key->blend)
	{
		/* (a constant factor the pixel shader applies: source_blend_in_shader) */
		key->blend_source = key->pixel.source_blend ? XGPU_BLEND_ONE : blend_factor(rs[D3DRS_SRCBLEND]);
		key->blend_destination = blend_factor(rs[D3DRS_DESTBLEND]);
		key->blend_operation = blend_operation(rs[D3DRS_BLENDOP]);
		color_to_vec4(rs[D3DRS_BLENDCOLOR], dynamic->blend_color);
	}
	key->color_mask = (unsigned char)(((write & D3DCOLORWRITEENABLE_RED) ? XGPU_WRITE_RED : 0) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? XGPU_WRITE_GREEN : 0) |
		((write & D3DCOLORWRITEENABLE_BLUE) ? XGPU_WRITE_BLUE : 0) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? XGPU_WRITE_ALPHA : 0));

	/* the cull mode names the winding to discard; FRONTFACE names the
	front winding */
	key->cull = rs[D3DRS_CULLMODE] != D3DCULL_NONE;
	if (key->cull)
	{
		key->front_counter_clockwise = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW;
#ifdef HALO_VR
		/* (the other way about for a mirrored model: halo_vr_mirrored_draws) */
		if (vr_gl.mirrored)
			key->front_counter_clockwise = !key->front_counter_clockwise;
#endif
		key->cull_front = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE];
	}
	key->fill = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? XGPU_FILL_LINES :
		rs[D3DRS_FILLMODE] == D3DFILL_POINT ? XGPU_FILL_POINTS : XGPU_FILL_SOLID;

	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias) */
	key->depth_bias = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	if (key->depth_bias)
	{
		dynamic->depth_bias[0] = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		dynamic->depth_bias[1] = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}
}
/* the uniforms of the latest draws, converted from these inputs; the serial
counts the conversions */
#define DRAW_UNIFORM_INPUT_COUNT (4 + 4 + 16 + 1 + 16 + 2 + 4 + 1 + 1 + 7 * D3DTSS_MAXSTAGES + 4)

static DWORD draw_uniform_inputs[DRAW_UNIFORM_INPUT_COUNT];
static struct xgpu_draw_uniforms draw_uniforms;
static unsigned long draw_uniforms_serial;

/* size is a multiple of 4. Every draw compares its uniforms' inputs (about
300 bytes) with the last's: the loop has no early exit, so that the
compiler compares several words at once, where the C library's memcmp (the
Windows build's) compares them one at a time */
static BOOL words_equal(const void *a, const void *b, unsigned long size)
{
	const DWORD *words_a = a;
	const DWORD *words_b = b;
	DWORD difference = 0;
	unsigned long index;

	for (index = 0; index < size / 4; index++)
		difference |= words_a[index] ^ words_b[index];
	return difference == 0;
}

/* the draw's other uniforms, from the state they come from: most draws
share it with the draw before them, and so share its uniforms */
static void draw_uniforms_update(const float texture_scale[4][4])
{
	DWORD inputs[DRAW_UNIFORM_INPUT_COUNT];
	unsigned long count = 0;
	float world_object[4];
	int stage;

	memcpy(&inputs[count], device.viewport_scale, sizeof(device.viewport_scale));
	count += 4;
	memcpy(&inputs[count], device.viewport_offset, sizeof(device.viewport_offset));
	count += 4;
	memcpy(&inputs[count], texture_scale, 16 * sizeof(float));
	count += 16;
	inputs[count++] = D3D__RenderState[D3DRS_POINTSIZE];
	for (stage = 0; stage < 8; stage++)
	{
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage];
		inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage];
	}
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0];
	inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1];
	inputs[count++] = D3D__RenderState[D3DRS_FOGCOLOR];
	inputs[count++] = D3D__RenderState[D3DRS_FOGSTART];
	inputs[count++] = D3D__RenderState[D3DRS_FOGEND];
	inputs[count++] = D3D__RenderState[D3DRS_FOGDENSITY];
	inputs[count++] = D3D__RenderState[D3DRS_ALPHAREF];
	inputs[count++] = (DWORD)UI_OFFSET;
	/* (display.ambient_occlusion "world": the draw's object, its owner
	slot and reference, its owner among the rays' instances, and whether it
	is two-sided cards; -1, 1, 0 and 0 without the window's block) */
	world_object[0] = world_ao.active ? (float)world_ao.owner : (float)WORLD_AO_OWNER_NONE;
	world_object[1] = world_ao.active ? world_ao.reference : 1.0f;
	world_object[2] = world_ao.active ? (float)world_ao.rays_owner : 0.0f;
	world_object[3] = world_ao.active && world_ao.cards ? 1.0f : 0.0f;
	memcpy(&inputs[count], world_object, sizeof(world_object));
	count += 4;
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		inputs[count++] = state[D3DTSS_BUMPENVMAT00];
		inputs[count++] = state[D3DTSS_BUMPENVMAT01];
		inputs[count++] = state[D3DTSS_BUMPENVMAT10];
		inputs[count++] = state[D3DTSS_BUMPENVMAT11];
		inputs[count++] = state[D3DTSS_BUMPENVLSCALE];
		inputs[count++] = state[D3DTSS_BUMPENVLOFFSET];
		inputs[count++] = state[D3DTSS_MIPMAPLODBIAS];
	}
	if (!draw_uniforms_serial || !words_equal(inputs, draw_uniform_inputs, sizeof(inputs)))
	{
		struct xgpu_draw_uniforms *converted = &draw_uniforms;

		memcpy(draw_uniform_inputs, inputs, sizeof(inputs));
		draw_uniforms_serial++;
		memcpy(converted->viewport_scale, device.viewport_scale, sizeof(converted->viewport_scale));
		memcpy(converted->viewport_offset, device.viewport_offset, sizeof(converted->viewport_offset));
		memcpy(converted->texture_scale, texture_scale, sizeof(converted->texture_scale));
		converted->point_size = D3D__RenderState[D3DRS_POINTSIZE] ?
			dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
		for (stage = 0; stage < 8; stage++)
		{
			color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], converted->ps_c0[stage]);
			color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], converted->ps_c1[stage]);
		}
		color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], converted->ps_final_c0);
		color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], converted->ps_final_c1);
		color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], converted->fog_color);
		converted->fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
		converted->fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
		converted->fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
		converted->fog_parameters[3] = 0.0f;
		converted->alpha_reference = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			DWORD *state = D3D__TextureState[stage];

			converted->bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
			converted->bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
			converted->bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
			converted->bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
			converted->bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
			converted->bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
			converted->bump_luminance[stage][2] = converted->bump_luminance[stage][3] = 0.0f;
			converted->texture_lod_bias[stage] = dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
		}
		converted->screen_offset = (float)UI_OFFSET;
		converted->world_ao[0] = world_object[0];
		converted->world_ao[1] = world_object[1];
		converted->world_ao[2] = world_object[2];
		converted->world_ao[3] = world_object[3];
	}
}

/* ---------- enhanced dynamic lights

display.dynamic_lights "enhanced" (xgpu.h; the game's side is
port/linux/game/dynamic_lights.c): each frame the game hands over its
dynamic lights (halo_dynamic_lights_set), placed where the drawn objects are
(between ticks), and the structure's lightmap pass and the models lit for
each pixel add them for each pixel (xgpu_spirv.c, xgpu_msl.c), in place of the
game's own passes over the structure for each light and its two lights for
each object's vertices. A grid about the camera says which lights reach each of
its cells (xgpu.h), so a pixel weighs only the lights near it: a frame's
lights cost what lights each pixel, not their number. A plasma bolt's light
(a projectile with no model) also has a glow drawn about it
(halo_dynamic_lights_halos_draw).
"classic", the default: none of this, the game's own lighting. Where the
backend has them (xgpu_backend_capabilities: Vulkan, Metal). */
static struct
{
	/* the settings read, as halo_gpu_settings_taken() was then (the flag
	says whether they have been read) */
	BOOL read;
	unsigned long read_at;
	BOOL enhanced;
	/* whether the game played lets them be enhanced, as then (a Pro 2v2
	game's do not: port/linux/game/network_standard.c's
	network_standard_enhanced_lights_allowed) */
	BOOL allowed;
	/* the look: [0] the lights' brightness, the core (1 / (1 + core x^2)),
	the spill past white, the models' wrap; [1] the glow's size (of the
	light's radius), brightness, softness against what it meets (world
	units), its least size (debug.dynamic_lights_look; xgpu_spirv.c) */
	float look[2][4];
	/* the lights this frame (0: none: drawn as classic) */
	int count;
	/* the game's lightmap pass is being drawn (halo_dynamic_lights_environment) */
	BOOL environment;
	BOOL failed;
	struct xgpu_dynamic_lights_block data;
	/* the cells' masks, and past them a slice of the clusters' (xgpu.h) */
	unsigned int mask[XGPU_DYNAMIC_LIGHTS_GRID_Z + 1][XGPU_DYNAMIC_LIGHTS_GRID_Y][XGPU_DYNAMIC_LIGHTS_GRID_X][4];
	/* the world (halo_dynamic_lights_world, malloc's), and whether the
	backend has it */
	unsigned int (*world)[4];
	int world_count;
	BOOL world_sent;
	/* the glows: each one's middle and size, its color (the display's) */
	struct xgpu_dynamic_lights_halos halos;
} dynamic_lights;

/* port/linux/game/network_standard.c's: whether the game played lets the
dynamic lights be enhanced (a Pro 2v2 game's: classic for every player,
whatever their display.dynamic_lights) */
int network_standard_enhanced_lights_allowed(void);

static void dynamic_lights_read_settings(void)
{
	static const float defaults[2][4] = { { 1.7f, 12.0f, 0.18f, 0.25f }, { 0.22f, 1.2f, 0.12f, 0.035f } };
	const char *look;
	BOOL allowed = network_standard_enhanced_lights_allowed() != 0;

	/* (as the front end took the settings up between frames, not as the
	menus write them: the draws' pipelines change at the frame that
	prepares the map again under them, render_enumerate.c's
	enumerated_derivation_check) */
	if (dynamic_lights.read && dynamic_lights.read_at == halo_gpu_settings_taken() && dynamic_lights.allowed == allowed)
		return;
	dynamic_lights.read = TRUE;
	dynamic_lights.read_at = halo_gpu_settings_taken();
	dynamic_lights.allowed = allowed;
	dynamic_lights.enhanced = !strcmp(config_string("display.dynamic_lights"), "enhanced") && allowed;
	memcpy(dynamic_lights.look, defaults, sizeof(defaults));
	look = config_string("debug.dynamic_lights_look");
	if (look && *look)
	{
		float *values = &dynamic_lights.look[0][0];
		int index;

		for (index = 0; index < 8 && *look; index++)
		{
			char *end;
			float value = (float)strtod(look, &end);

			if (end == look)
				break;
			values[index] = value;
			look = end;
		}
	}
}


/* whether the game is to light with the enhanced dynamic lights (it then
hands them over, and leaves the structure to them) */
int halo_dynamic_lights_enhanced(void)
{
	dynamic_lights_read_settings();
	return dynamic_lights.enhanced && !dynamic_lights.failed && device.gl_ready &&
		xgpu_backend_capabilities.dynamic_lights;
}

/* whether a map's preparation draws its passes lit by the enhanced lights
(port/linux/game/render_enumerate.c) and the lights are given to the front
end (halo_dynamic_lights_set): with them enhanced, or classic and the
preparation recorded (render_prepared.c), so that its record holds those
passes' draws for a load with them enhanced. Their draws' pipelines are
derived as the setting says (dynamic_lights_active: classic, the game's
lighting). */
int halo_dynamic_lights_walked(void)
{
	dynamic_lights_read_settings();
	return (dynamic_lights.enhanced || journal_recording) && !dynamic_lights.failed && device.gl_ready &&
		xgpu_backend_capabilities.dynamic_lights;
}

/* a grid cell's index along an axis for an offset from the middle (xgpu.h) */
static int dynamic_lights_cell(float offset, float scale, int count)
{
	float cell = log2f(1.0f + fabsf(offset) * (1.0f / XGPU_DYNAMIC_LIGHTS_CELL)) * scale;
	int index = (int)floorf(offset < 0.0f ? (float)(count / 2) - cell : (float)(count / 2) + cell);

	return index < 0 ? 0 : index >= count ? count - 1 : index;
}

typedef char dynamic_lights_cluster_slice[XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y >=
	XGPU_DYNAMIC_LIGHTS_CLUSTERS ? 1 : -1];
typedef char dynamic_lights_cells_bytes[sizeof(dynamic_lights.mask) == XGPU_DYNAMIC_LIGHTS_CELLS_BYTES ? 1 : -1];
typedef char dynamic_lights_game_sizes[HALO_DYNAMIC_LIGHTS_MAXIMUM == XGPU_DYNAMIC_LIGHTS_MAXIMUM &&
	HALO_DYNAMIC_LIGHTS_CLUSTERS == XGPU_DYNAMIC_LIGHTS_CLUSTERS &&
	HALO_DYNAMIC_LIGHTS_WORLD_TEXELS == XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS ? 1 : -1];

/* the structure's 3D BSP (the game's dynamic_lights.c; xgpu.h's world),
which the backend is given with the frame's lights next */
void halo_dynamic_lights_world(const unsigned int (*texels)[4], int count)
{
	if (count < 1 || count > XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS)
		return;
	if (!dynamic_lights.world)
		dynamic_lights.world = malloc(XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS * sizeof(*dynamic_lights.world));
	if (!dynamic_lights.world)
		return;
	memcpy(dynamic_lights.world, texels, (size_t)count * sizeof(*dynamic_lights.world));
	dynamic_lights.world_count = count;
	dynamic_lights.world_sent = FALSE;
}

/* the frame's lights (the game's dynamic_lights.c): where they are, as the
objects are drawn; the camera's position and axes (the glows face it); the
clusters each light reaches (each cluster's mask of them) */
void halo_dynamic_lights_set(const struct halo_dynamic_light *lights, int count, const float camera[3],
	const float right[3], const float up[3], const unsigned int (*cluster_masks)[4])
{
	static const int sizes[3] = { XGPU_DYNAMIC_LIGHTS_GRID_X, XGPU_DYNAMIC_LIGHTS_GRID_Y, XGPU_DYNAMIC_LIGHTS_GRID_Z };
	struct xgpu_dynamic_lights_halos *halos = &dynamic_lights.halos;
	float scales[3];
	int index, axis;

	dynamic_lights.count = 0;
	halos->count = 0;
	if (!halo_dynamic_lights_walked())
		return;
	if (count > XGPU_DYNAMIC_LIGHTS_MAXIMUM)
		count = XGPU_DYNAMIC_LIGHTS_MAXIMUM;
	if (!xgpu_backend_dynamic_lights_prepare())
	{
		platform_log("GPU: the enhanced dynamic lights' buffers could not be made: classic lights");
		dynamic_lights.failed = TRUE;
		return;
	}
	for (axis = 0; axis < 3; axis++)
	{
		scales[axis] = (float)(sizes[axis] / 2) / log2f(1.0f + XGPU_DYNAMIC_LIGHTS_REACH / XGPU_DYNAMIC_LIGHTS_CELL);
		dynamic_lights.data.grid[0][axis] = camera[axis];
		dynamic_lights.data.grid[1][axis] = scales[axis];
		halos->camera_right[axis] = right[axis];
		halos->camera_up[axis] = up[axis];
	}
	dynamic_lights.data.grid[0][3] = (float)count;
	dynamic_lights.data.grid[1][3] = 1.0f / XGPU_DYNAMIC_LIGHTS_CELL;
	memcpy(dynamic_lights.data.grid[2], dynamic_lights.look[0], sizeof(dynamic_lights.look[0]));
	memset(dynamic_lights.mask, 0, sizeof(dynamic_lights.mask));
	if (cluster_masks)
		memcpy(dynamic_lights.mask[XGPU_DYNAMIC_LIGHTS_GRID_Z], cluster_masks, XGPU_DYNAMIC_LIGHTS_CLUSTERS * 16);
	if (dynamic_lights.world && !dynamic_lights.world_sent)
	{
		xgpu_backend_dynamic_lights_world((const void *)dynamic_lights.world, dynamic_lights.world_count);
		dynamic_lights.world_sent = TRUE;
	}
	for (index = 0; index < count; index++)
	{
		const struct halo_dynamic_light *light = &lights[index];
		int first[3], last[3], x, y, z;
		float brightness = dynamic_lights.look[0][0];

		if (!(light->radius > 0.0f))
			continue;
		dynamic_lights.data.position[index][0] = light->position[0];
		dynamic_lights.data.position[index][1] = light->position[1];
		dynamic_lights.data.position[index][2] = light->position[2];
		dynamic_lights.data.position[index][3] = (light->flags & HALO_DYNAMIC_LIGHT_MODELS_ONLY ? -1.0f : 1.0f) /
			light->radius;
		for (axis = 0; axis < 3; axis++)
		{
			/* (linear: the display's gamma out) */
			dynamic_lights.data.color[index][axis] = powf(light->color[axis], 2.2f) * brightness;
			dynamic_lights.data.cone[index][axis] = light->axis[axis];
			first[axis] = dynamic_lights_cell(light->position[axis] - light->radius - camera[axis], scales[axis],
				sizes[axis]);
			last[axis] = dynamic_lights_cell(light->position[axis] + light->radius - camera[axis], scales[axis],
				sizes[axis]);
		}
		dynamic_lights.data.color[index][3] = light->cone_offset;
		dynamic_lights.data.cone[index][3] = light->cone_scale;
		for (z = first[2]; z <= last[2]; z++)
			for (y = first[1]; y <= last[1]; y++)
				for (x = first[0]; x <= last[0]; x++)
					dynamic_lights.mask[z][y][x][index >> 5] |= 1u << (index & 31);
		if ((light->flags & HALO_DYNAMIC_LIGHT_HALO) && halos->count < XGPU_DYNAMIC_LIGHTS_MAXIMUM)
		{
			float *halo = halos->halos[halos->count * 2];
			float size = light->radius * dynamic_lights.look[1][0];
			float offset[3], distance, fade;

			if (size < dynamic_lights.look[1][3])
				size = dynamic_lights.look[1][3];
			/* (faded out near the camera: a bolt's first frame between ticks
			is drawn where it was made, at the eye, where its glow would
			fill the view) */
			for (axis = 0; axis < 3; axis++)
				offset[axis] = light->position[axis] - camera[axis];
			distance = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
			fade = (distance - size) / (3.0f * size);
			fade = fade < 0.0f ? 0.0f : fade > 1.0f ? 1.0f : fade * fade * (3.0f - 2.0f * fade);
			halo[0] = light->position[0];
			halo[1] = light->position[1];
			halo[2] = light->position[2];
			halo[3] = size;
			halo[4] = light->color[0] * dynamic_lights.look[1][1] * fade;
			halo[5] = light->color[1] * dynamic_lights.look[1][1] * fade;
			halo[6] = light->color[2] * dynamic_lights.look[1][1] * fade;
			halo[7] = 0.0f;
			halos->count++;
		}
	}
	if (!count)
		return;
	xgpu_backend_dynamic_lights_upload(&dynamic_lights.data, dynamic_lights.mask);
	dynamic_lights.count = count;
}

/* the game draws the structure's lightmap pass (rasterizer_xbox_environment.c);
as it begins, the world's projection is the 3D view's (the glows' then) */
void halo_dynamic_lights_environment(int drawing)
{
	struct xgpu_dynamic_lights_halos *halos = &dynamic_lights.halos;

	dynamic_lights.environment = drawing != 0;
	if (drawing)
	{
		memcpy(halos->projection[0], device.constants[0], 4 * sizeof(halos->projection[0]));
		memcpy(halos->projection[4], device.constants[XGPU_VERTEX_CONSTANT_BIAS - 38], sizeof(halos->projection[0]));
		memcpy(halos->projection[5], device.constants[XGPU_VERTEX_CONSTANT_BIAS - 37], sizeof(halos->projection[0]));
		memcpy(halos->viewport_scale, device.viewport_scale, sizeof(halos->viewport_scale));
		memcpy(halos->viewport_offset, device.viewport_offset, sizeof(halos->viewport_offset));
	}
}

/* (the register the game gives a model's own lights in is the one the
backends read them from) */
typedef char dynamic_lights_own_lights_register[XGPU_MODEL_LIGHT_REGISTER(XGPU_MODEL_LIGHT_OWN_LIGHTS) ==
	XGPU_VERTEX_CONSTANT_BIAS + HALO_DYNAMIC_LIGHTS_OWN_LIGHTS_REGISTER ? 1 : -1];

/* the frame's draws are lit by the enhanced dynamic lights */
static BOOL dynamic_lights_active(void)
{
	/* (the setting as it is now: a map's preparation played back from its
	record before anything else read it) */
	dynamic_lights_read_settings();
	return dynamic_lights.count > 0 && dynamic_lights.enhanced;
}

/* the glows of the frame's bolts' lights, into the 3D view, after its
transparent geometry (source/render/render.c): added to what is there, by
the backend */
void halo_dynamic_lights_halos_draw(void)
{
	struct render_target_entry *color, *depth;
	struct xgpu_dynamic_lights_halos *halos = &dynamic_lights.halos;
	int axis;

	if (journal_recording)
		journal_operation(JOURNAL_HALOS, NULL, 0);
	if (!dynamic_lights_active() || !halos->count || !device.gl_ready)
		return;
#ifdef HALO_VR
	vr_frame_resume();
	/* (the eyes' pass, or the flat screen's; not the HUD's, nor a scope's) */
	if (vr_gl.pass != HALO_VR_PASS_NONE && vr_gl.pass != HALO_VR_PASS_EYES)
		return;
#endif
	color = render_target_get(device.render_target);
	depth = render_target_get(device.depth_stencil);
	if (!color || !depth || !depth->target.depth || color->target.layers != depth->target.layers)
		return;
	for (axis = 0; axis < 3; axis++)
		halos->camera[axis] = dynamic_lights.data.grid[0][axis];
	halos->softness = dynamic_lights.look[1][2];
	halos->screen_offset = (float)UI_OFFSET;
	xgpu_backend_dynamic_lights_halos(color->target.number, depth->target.number, color->target.layers, halos);
}

/* display.per_pixel_lighting: the model lighting programs' draws are lit for
each pixel (xgpu_spirv.c's model lighting), from shaders of their own; read
again as the front end takes a change up between frames (as the dynamic
lights are: dynamic_lights_read_settings). The enhanced dynamic lights light
models for each pixel too, whatever it says. */
static BOOL per_pixel_lighting(void)
{
	static BOOL read;
	static unsigned long read_at;
	static BOOL enabled;

	if (!read || read_at != halo_gpu_settings_taken())
	{
		read = TRUE;
		read_at = halo_gpu_settings_taken();
		enabled = config_boolean("display.per_pixel_lighting") != 0;
	}
	return enabled || halo_dynamic_lights_enhanced();
}

/* the combiner portions whose results nothing after reads cleared (what
an earlier draw left in a portion the draw's code did not set, which no
later stage nor the final combiner reads): one program, one key. A
register's colour and alpha are live apart (bit 0 and bit 1); a portion
reads its inputs' (an alpha portion reads the blue of an input not marked
alpha), and writes its destinations' colour (and alpha too, a blue copied
to alpha) or alpha; a mux reads r0's alpha */
#define LIVE_RGB 1
#define LIVE_ALPHA 2

static void live_input(unsigned char *live, unsigned long input, BOOL alpha_portion)
{
	unsigned long reg = input & 0x0f;

	live[reg] |= (input & 0x10) ? LIVE_ALPHA : alpha_portion ? LIVE_RGB : LIVE_RGB;
}

static void dead_portions_clear(DWORD *state)
{
	unsigned char live[16];
	int count = (int)(state[D3DRS_PSCOMBINERCOUNT] & 0xff), stage, input;
	DWORD final_abcd = state[D3DRS_PSFINALCOMBINERINPUTSABCD], final_efg = state[D3DRS_PSFINALCOMBINERINPUTSEFG];

	memset(live, 0, sizeof(live));
	if (count > 8)
		count = 8;
	if (!final_abcd && !final_efg)
	{
		live[XGPU_REGISTER_R0] = LIVE_RGB | LIVE_ALPHA;
	}
	else
	{
		for (input = 0; input < 7; input++)
		{
			unsigned long byte = input < 4 ? (final_abcd >> (24 - 8 * input)) & 0xff :
				(final_efg >> (24 - 8 * (input - 4))) & 0xff;

			if (input < 4 && (byte & 0x0f) == XGPU_REGISTER_V1R0_SUM)
			{
				live[XGPU_REGISTER_V1] |= LIVE_RGB | LIVE_ALPHA;
				live[XGPU_REGISTER_R0] |= LIVE_RGB | LIVE_ALPHA;
			}
			else if (input < 4 && (byte & 0x0f) == XGPU_REGISTER_EF_PRODUCT)
			{
				/* (E and F are read below) */
			}
			else
			{
				live_input(live, byte, input == 6);
			}
		}
	}
	for (stage = count - 1; stage >= 0; stage--)
	{
		unsigned char before[16];
		int portion;

		memcpy(before, live, sizeof(before));
		/* (what the stage writes is not live before it, unless it reads it) */
		for (portion = 0; portion < 2; portion++)
		{
			BOOL alpha = portion == 1;
			DWORD *inputs = &state[(alpha ? D3DRS_PSALPHAINPUTS0 : D3DRS_PSRGBINPUTS0) + stage];
			DWORD *outputs = &state[(alpha ? D3DRS_PSALPHAOUTPUTS0 : D3DRS_PSRGBOUTPUTS0) + stage];
			unsigned long flags = *outputs >> 12;
			unsigned long ab = (*outputs >> 4) & 0x0f, cd = *outputs & 0x0f, sum = (*outputs >> 8) & 0x0f;
			unsigned char channel = alpha ? LIVE_ALPHA : LIVE_RGB;
			BOOL ab_alpha = !alpha && (flags & 0x80), cd_alpha = !alpha && (flags & 0x40);
			BOOL used = (ab && ((live[ab] & channel) || (ab_alpha && (live[ab] & LIVE_ALPHA)))) ||
				(cd && ((live[cd] & channel) || (cd_alpha && (live[cd] & LIVE_ALPHA)))) ||
				(sum && (live[sum] & channel));

			if (!used)
			{
				*inputs = 0;
				*outputs = 0;
				continue;
			}
			if (ab)
				before[ab] &= (unsigned char)~(channel | (ab_alpha ? LIVE_ALPHA : 0));
			if (cd)
				before[cd] &= (unsigned char)~(channel | (cd_alpha ? LIVE_ALPHA : 0));
			if (sum)
				before[sum] &= (unsigned char)~channel;
		}
		for (portion = 0; portion < 2; portion++)
		{
			BOOL alpha = portion == 1;
			DWORD inputs = state[(alpha ? D3DRS_PSALPHAINPUTS0 : D3DRS_PSRGBINPUTS0) + stage];
			DWORD outputs = state[(alpha ? D3DRS_PSALPHAOUTPUTS0 : D3DRS_PSRGBOUTPUTS0) + stage];

			if (!outputs)
				continue;
			for (input = 0; input < 4; input++)
				live_input(before, (inputs >> (24 - 8 * input)) & 0xff, alpha);
			if ((outputs >> 12) & 0x04)
				before[XGPU_REGISTER_R0] |= LIVE_ALPHA;
		}
		before[XGPU_REGISTER_ZERO] = 0;
		memcpy(live, before, sizeof(live));
	}
}

/* a texture stage's fields only its mode reads (what earlier draws left in
the others): the stage it depends on (PSINPUTTEXTURE: the dependent and
dot product modes), its dot product's mapping (PSDOTMAPPING: the dot
product modes), its clip plane's comparisons (PSCOMPAREMODE) */
static BOOL stage_mode_reads_input(unsigned long mode)
{
	return mode >= 0x08 && mode <= 0x12;
}

static void unread_stage_fields_clear(DWORD *state, DWORD texture_modes)
{
	DWORD input = state[D3DRS_PSINPUTTEXTURE], mapping = state[D3DRS_PSDOTMAPPING];
	DWORD compare = state[D3DRS_PSCOMPAREMODE];
	unsigned long modes[4];
	int stage;

	for (stage = 0; stage < 4; stage++)
		modes[stage] = (texture_modes >> (5 * stage)) & 0x1f;
	if (!stage_mode_reads_input(modes[2]))
		input &= ~(1UL << 16);
	/* (a diffuse reflection at stage 2 reads stage 3's input too) */
	if (!stage_mode_reads_input(modes[3]) && modes[2] != 0x0b)
		input &= ~(3UL << 20);
	for (stage = 1; stage < 4; stage++)
	{
		if (!stage_mode_reads_input(modes[stage]))
			mapping &= ~(7UL << ((stage - 1) * 4));
	}
	for (stage = 0; stage < 4; stage++)
	{
		if (modes[stage] != 0x05)
			compare &= ~(0xfUL << (4 * stage));
	}
	state[D3DRS_PSINPUTTEXTURE] = input;
	state[D3DRS_PSDOTMAPPING] = mapping;
	state[D3DRS_PSCOMPAREMODE] = compare;
}

/* the combiners' part of a draw's pixel shader key, from the render states
(less the constants, which are uniforms) and the texture modes: a frame's
draws use a few dozen pixel shaders, interleaved, so the parts already made
are kept, each under its inputs (the same inputs, the same part) */
#define COMBINER_PARTS 128

static struct combiner_part
{
	BOOL made;
	DWORD texture_modes;
	DWORD inputs[D3DRS_PS_MAX];
	DWORD state[D3DRS_PS_MAX];
} combiner_parts[COMBINER_PARTS];

static BOOL combiner_part_found(const struct combiner_part *part, const DWORD *inputs, DWORD texture_modes)
{
	return part->made && part->texture_modes == texture_modes && words_equal(part->inputs, inputs, sizeof(part->inputs));
}

static void combiner_state_describe(DWORD *state, DWORD texture_modes)
{
	static struct combiner_part *last;
	struct combiner_part *part;
	DWORD hash = texture_modes;
	int stage, index;

	memcpy(state, D3D__RenderState, D3DRS_PS_MAX * sizeof(DWORD));
	/* constants are uniforms, not part of the program */
	memset(&state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	/* (most often the draw before's; else found by a sum of the inputs,
	each word weighted apart, which the compiler sums several at once) */
	if (last && combiner_part_found(last, state, texture_modes))
	{
		memcpy(state, last->state, sizeof(last->state));
		return;
	}
	for (index = 0; index < D3DRS_PS_MAX; index++)
		hash += state[index] * (2u * (DWORD)index + 0x9e3779b1u);
	part = &combiner_parts[(hash ^ (hash >> 15) ^ (hash >> 23)) % COMBINER_PARTS];
	last = part;
	if (combiner_part_found(part, state, texture_modes))
	{
		memcpy(state, part->state, sizeof(part->state));
		return;
	}
	part->made = TRUE;
	part->texture_modes = texture_modes;
	memcpy(part->inputs, state, sizeof(part->inputs));
	/* nor are the stages past the combiner count (what earlier draws left
	in them): one program, one key */
	for (stage = (int)(D3D__RenderState[D3DRS_PSCOMBINERCOUNT] & 0xff); stage < 8; stage++)
	{
		state[D3DRS_PSALPHAINPUTS0 + stage] = 0;
		state[D3DRS_PSALPHAOUTPUTS0 + stage] = 0;
		state[D3DRS_PSRGBINPUTS0 + stage] = 0;
		state[D3DRS_PSRGBOUTPUTS0 + stage] = 0;
	}
	dead_portions_clear(state);
	unread_stage_fields_clear(state, texture_modes);
	memcpy(part->state, state, sizeof(part->state));
}

/* what the settings make of a draw's pipeline (xgpu_draw_derive), in it */
static void pipeline_derived(const struct xgpu_draw_derived *derived, struct xgpu_pipeline_key *key)
{
	key->color_format = derived->color_format;
	key->samples = derived->samples;
	key->lighting = derived->lighting;
	key->pixel.alpha_test_samples = derived->alpha_test_samples;
	key->pixel.hdr = derived->hdr;
	key->pixel.per_pixel_lighting = derived->per_pixel_lighting;
	key->pixel.dynamic_lights = derived->dynamic_lights;
	key->pixel.ambient_occlusion = derived->ambient_occlusion;
}

/* a draw as no setting changes it (key: before xgpu_draw_derive) to the
manifest's recording, with what the settings derive it from, whatever they
are now (xgpu_manifest_context) */
static void manifest_note_basis(const struct vertex_shader_object *program, const struct xgpu_pipeline_key *key)
{
	struct xgpu_manifest_context context;

	memset(&context, 0, sizeof(context));
	context.lights = (uint8_t)program->lighting.lights;
	context.world_found = program->lighting.world_found ? 1 : 0;
	context.multisampled = target_multisampled ? 1 : 0;
	context.screen_colour = target_screen_colour ? 1 : 0;
	/* (the lights given to the front end whatever the setting where the
	map's preparation is recorded: halo_dynamic_lights_walked) */
	context.lights_present = dynamic_lights.count > 0;
	context.environment = dynamic_lights.environment ? 1 : 0;
	context.hdr_category = (uint8_t)hdr.category;
	context.hdr_view = hdr.view ? 1 : 0;
	context.hdr_kind = (uint16_t)hdr.kind;
	/* (display.ambient_occlusion "world"'s, as with it on: its window's
	span as the game draws it, whatever the setting, as the journal notes
	it) */
	context.world = world_ao_applies() ? 1 : 0;
	/* (...and where the pre-pass draws its twin: world_ao_twins) */
	context.found = world_ao_span_found() ? 1 : 0;
	xgpu_manifest_note_basis(key, &context);
}

/* the draw's pipeline as the facts derive it (xgpu_draw_derive), bound;
FALSE if none can be drawn with. Where the lit one cannot be made: without
the world's light, where it had it (world_ao_fail: from here on), else
lit by none of the shaders' lighting, its program's lighting or its split
noted as failed */
static BOOL pipeline_bind(struct vertex_shader_object *program, struct xgpu_draw_facts *facts,
	struct xgpu_pipeline_key *pipeline_key)
{
	struct xgpu_draw_derived derived;
	BOOL chosen = FALSE, lit;

	lit = xgpu_draw_derive(facts, FALSE, &derived);
	if (lit)
	{
		pipeline_derived(&derived, pipeline_key);
		chosen = xgpu_backend_pipeline(pipeline_key);
	}
	if (lit && !chosen && (derived.ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD))
	{
		world_ao_fail();
		facts->world = 0;
		lit = xgpu_draw_derive(facts, FALSE, &derived);
		if (lit)
		{
			pipeline_derived(&derived, pipeline_key);
			chosen = xgpu_backend_pipeline(pipeline_key);
		}
	}
	/* (not made: drawn unlit by these shaders instead, below; not at all,
	were it to fail too) */
	if (lit && !chosen && derived.lighting == XGPU_LIGHTING_MODEL_SPLIT)
	{
		platform_log("GPU: vertex shader %lu cannot hand on its ambient light apart (refer to the shader log "
			"above): drawn without the ambient occlusion", program->id);
		program->split_failed = TRUE;
	}
	else if (lit && !chosen)
	{
		platform_log(derived.lighting == XGPU_LIGHTING_ENVIRONMENT ? "GPU: vertex shader %lu cannot be lit by the "
			"dynamic lights (refer to the shader log above): lit by the game's own passes" : "GPU: vertex shader %lu "
			"cannot be lit for each pixel here (refer to the shader log above): lit for each vertex", program->id);
		program->lighting_failed = TRUE;
	}
	if (!chosen)
	{
		xgpu_draw_derive(facts, TRUE, &derived);
		pipeline_derived(&derived, pipeline_key);
		chosen = xgpu_backend_pipeline(pipeline_key);
	}
	return chosen;
}

/* display.ambient_occlusion "world": each pipeline with the world's light
(by its number: xgpu_backend_pipeline_number) told its stand-in, the
pipeline the draw would have without the world, which the backend draws
with until the world's is made (a map's preparation makes the world's:
world_ao_key; the stand-in is drawn where it missed them, or they fail). Found the first time the number is
drawn with: the draw derived again from its facts without the world's
light (basis: its pipeline before the derivation), then the world's bound
again. */
#define WORLD_AO_STAND_INS 0x4000
static struct
{
	unsigned long number[WORLD_AO_STAND_INS];
} world_ao_stand_ins;

static void world_ao_stand_in(struct vertex_shader_object *program, const struct xgpu_draw_facts *facts,
	const struct xgpu_pipeline_key *basis, const struct xgpu_pipeline_key *pipeline_key)
{
	unsigned long number = xgpu_backend_pipeline_number(), slot = number % WORLD_AO_STAND_INS;
	struct xgpu_draw_facts plain_facts = *facts;
	struct xgpu_pipeline_key plain_key = *basis;
	unsigned long plain = 0;

	if (world_ao_stand_ins.number[slot] == number + 1)
		return;
	world_ao_stand_ins.number[slot] = number + 1;
	plain_facts.world = 0;
	if (pipeline_bind(program, &plain_facts, &plain_key))
		plain = xgpu_backend_pipeline_number();
	/* (the world's bound again, and told its stand-in) */
	if (!xgpu_backend_pipeline(pipeline_key) || xgpu_backend_pipeline_number() != number)
		return;
	xgpu_backend_pipeline_stand_in(plain && plain != number ? plain : 0);
}

/* the draw's description to the backend: its textures, its targets, its
pipeline (shaders and fixed-function state), its dynamic state and its
uniforms; FALSE if it is not drawn */
static BOOL prepare_draw(BOOL immediate, unsigned long primitive)
{
	struct vertex_shader_object *program = current_program();
	struct nv2a_pixel_shader_key key;
	struct xgpu_pipeline_key pipeline_key, basis;
	struct xgpu_dynamic_state dynamic;
	struct xgpu_draw_facts facts;
	float texture_scale[4][4];
	BOOL has_depth = FALSE, ready;
	int stage;
	double traced;

	preparation.duplicate = FALSE;
	preparation.sample = preparation.active && preparation.trace &&
		((preparation.attempts ^ (preparation.attempts >> 6)) & 63) == 0;
	if (preparation.active && preparation.trace)
		preparation.attempts++;
	if (preparation.sample)
		preparation.sampled_draws++;
	traced = preparation_time();
	if (!device.gl_ready || !program || !device.vertex_shader || !program->program)
	{
		stats.skipped_no_program++;
		return FALSE;
	}
	{
		const char *skip = debug_settings.skip_vertex_shaders;

		while (skip && *skip)
		{
			if ((unsigned long)atol(skip) == program->id)
				return FALSE;
			skip = strchr(skip, ',');
			if (skip)
				skip++;
		}
	}
	memset(&key, 0, sizeof(key));
	key.texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	combiner_state_describe(key.combiner_state, key.texture_modes);
	/* the textures before the targets: a render target the draw samples
	has its multisampled pixels resolved by the backend, and the back buffer
	can be both sampled and drawn into */
	preparation_time_add(2, traced);
	traced = preparation_time();
	bind_textures(&key, texture_scale);
	preparation_time_add(0, traced);
	traced = preparation_time();
	if (!bind_targets(&has_depth))
	{
		stats.skipped_no_target++;
		return FALSE;
	}
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key.alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key.color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	key.coverage_alpha = key.coverage_alpha && D3D__RenderState[D3DRS_ALPHABLENDENABLE] &&
		D3D__RenderState[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		D3D__RenderState[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA;
	key.point_threshold = key.point_threshold && key.coverage_alpha;
	key.alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	key.fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key.source_blend = source_blend_in_shader();
	key.fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];
	key.count_samples = device.visibility_test_active && SAMPLE_COUNTING;
#ifdef HALO_VR
	key.multiview = vr_gl.multiview ? XGPU_MULTIVIEW_EYES : vr_gl.left_eye ? XGPU_MULTIVIEW_LEFT_EYE :
		vr_gl.right_eye ? XGPU_MULTIVIEW_RIGHT_EYE : XGPU_MULTIVIEW_NONE;
	/* (the VR mode's effects, drawn into both eyes: halo_vr_effect_depth) */
	key.vr_effect = key.multiview == XGPU_MULTIVIEW_EYES ? (unsigned char)vr_gl.effect_depth : 0;
#endif
	/* the pipeline: the shaders, the fixed-function state, the targets */
	memset(&pipeline_key, 0, sizeof(pipeline_key));
	memset(&dynamic, 0, sizeof(dynamic));
	pipeline_key.pixel = key;
	pipeline_state_describe(has_depth, &pipeline_key, &dynamic);
	pipeline_key.vertex_program = program->program->hash;
	pipeline_key.packed_attributes = immediate ? 0 : device.vertex_shader->packed_mask;
	pipeline_key.immediate = immediate != 0;
	pipeline_key.views = key.multiview;
	pipeline_key.color_format = target_has_color ? XGPU_FORMAT_BGRA8 : XGPU_FORMAT_NONE;
	pipeline_key.depth_format = has_depth ? XGPU_FORMAT_D24S8 : XGPU_FORMAT_NONE;
	pipeline_key.samples = 1;
	/* the draw as no setting changes it, recorded with what the settings
	make of it from (debug.render_manifest) */
	if (xgpu_manifest_recording())
		manifest_note_basis(program, &pipeline_key);
	/* ...and what the settings make of it as they are now: its targets'
	samples and format, its alpha test (a sample mask, where the backend
	has one), its tag (display.hdr: from its blend), and its lighting
	(xgpu_draw_derive): a model lighting program's lit for each pixel
	(display.per_pixel_lighting), the enhanced dynamic lights in its two
	point lights' place; over a model lit for each vertex, the ambient
	occlusion its ambient light handed on apart, by the occlusion at each
	pixel (the program's own sum clamps each vertex's light: xgpu.h); a
	structure's lightmap pass lit by the dynamic lights (its normal,
	binormal and tangent packed: rasterizer_xbox_vertex_shaders_initialize.c's
	declaration), their light past white with display.hdr, the lightmap's
	light by the occlusion and theirs added after; else, with
	display.ambient_occlusion "world", a lightmap pass's light, drawn
	opaque, by the occlusion */
	memset(&facts, 0, sizeof(facts));
	facts.blend = pipeline_key.blend;
	facts.color = target_has_color;
	facts.immediate = immediate != 0;
	facts.packed_attributes = (uint32_t)pipeline_key.packed_attributes;
	facts.alpha_test = (uint32_t)key.alpha_test_function;
	facts.lights = program->lighting.lights;
	facts.world_found = program->lighting.world_found;
	facts.lighting_failed = program->lighting_failed;
	facts.split_failed = program->split_failed;
	facts.samples = target_samples;
	facts.hdr_target = hdr.target;
	facts.hdr = hdr_tag(&pipeline_key);
	facts.per_pixel = per_pixel_lighting();
	facts.lights_active = dynamic_lights_active();
	facts.environment = dynamic_lights.environment;
	facts.sample_mask = xgpu_backend_capabilities.sample_mask;
	/* (display.ambient_occlusion "world": its light, where its window is
	given and the draw takes it: world_ao_key) */
	facts.world = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
	if (device.gl_ready)
		world_ao_twins();
	basis = pipeline_key;
	ready = pipeline_bind(program, &facts, &pipeline_key);
	if (ready && (pipeline_key.pixel.ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD))
		world_ao_stand_in(program, &facts, &basis, &pipeline_key);
#ifdef HALO_VR
	if (vr_gl.multiview && vr_gl.statistics && !program->program->projects)
	{
		/* debug.gpu_stats: the eye pass's draws in screen space, the same in
		both eyes (each program once, and the game's function that drew) */
		static unsigned long reported[64];
		static int reported_count;
		int index;

		for (index = 0; index < reported_count && reported[index] != program->id; index++)
			;
		if (index == reported_count && reported_count < 64)
		{
			reported[reported_count++] = program->id;
			platform_log("vr: screen-space draw in the eye pass: vertex shader %lu, drawn from %p", program->id,
				__builtin_return_address(1));
		}
	}
#endif
	if (!ready)
	{
		stats.skipped_link++;
		return FALSE;
	}
	if (immediate)
		stats.immediate_draws++;
	else
		stats.draws++;
	xgpu_manifest_note_pipeline(&pipeline_key);
	preparation_time_add(2, traced);
	traced = preparation_time();
	/* (the number of the key of xgpu_backend_pipeline's last call, above:
	pipeline_key's) */
	preparation.duplicate = preparation_seen(xgpu_backend_pipeline_number(), &dynamic, immediate, primitive);
	preparation_time_add(1, traced);
	if (preparation.duplicate)
		return TRUE;
	traced = preparation_time();
	draw_uniforms_update((const float (*)[4])texture_scale);
	xgpu_backend_draw_state(&dynamic, &draw_uniforms, draw_uniforms_serial);
	preparation_time_add(2, traced);
	return TRUE;
}

/* Screen-only enumeration changes draw state, not the targets, viewport,
constants or transforms. Keep the game's current state, including inputs
whose values immediate draws inherit, around those preparation draws. */
void halo_gpu_draw_preserved(void (*draw)(void *), void *data)
{
	DWORD render_states[D3DRS_MAX], texture_states[D3DTSS_MAXSTAGES][D3DTSS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	struct vertex_shader_object *vertex_shader = device.vertex_shader;
	struct vertex_shader_object *slot_zero = device.program_slots[0];
	unsigned long address = device.program_address;
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active = device.immediate_active;
	D3DPRIMITIVETYPE immediate_type = device.immediate_type;
	unsigned long immediate_written = device.immediate_written, immediate_count = device.immediate_count;

	if (!draw || immediate_active)
		return;
	memcpy(render_states, D3D__RenderState, sizeof(render_states));
	memcpy(texture_states, D3D__TextureState, sizeof(texture_states));
	memcpy(textures, device.textures, sizeof(textures));
	memcpy(attributes, device.attributes, sizeof(attributes));
	preparation.preserved++;
	draw(data);
	memcpy(D3D__RenderState, render_states, sizeof(render_states));
	memcpy(D3D__TextureState, texture_states, sizeof(texture_states));
	memcpy(device.textures, textures, sizeof(textures));
	memcpy(device.attributes, attributes, sizeof(attributes));
	device.vertex_shader = vertex_shader;
	device.program_slots[0] = slot_zero;
	device.program_address = address;
	device.immediate_active = immediate_active;
	device.immediate_type = immediate_type;
	device.immediate_written = immediate_written;
	device.immediate_count = immediate_count;
	/* Rebind the restored state even if a backend cached the last draw. */
	xgpu_backend_invalidate();
	preparation.preserved--;
	preparation_draw_complete();
}

/* the shaders and pipelines of the draws so far made (a map's first
frame's: port/linux/game/render_enumerate.c); the seconds waited */
void halo_gpu_prepare(int new_map)
{
	const char *detail = getenv("HALO_LOAD_TRACE_DETAIL");

	preparation_clear();
	preparation.active = TRUE;
	preparation.trace = config_boolean("debug.load_trace") && detail &&
		detail[0] && strcmp(detail, "0") != 0;
#ifndef CLOCK_THREAD_CPUTIME_ID
	preparation.trace = FALSE;
#endif
	preparation.sample = FALSE;
	preparation.attempts = preparation.sampled_draws = 0;
	memset(preparation.sampled_seconds, 0, sizeof(preparation.sampled_seconds));
	xgpu_manifest_enumerating(1, new_map);
	/* (display.ambient_occlusion "world": its pipelines and twins made too,
	the backend's flag cleared as it begins: world_ao_key) */
	world_ao.preparing = TRUE;
	world_ao.twins = FALSE;
	if (device.gl_ready)
		xgpu_backend_prepare();
}

double halo_thread_cpu_seconds(void)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
	struct timespec now;

	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
		return -1.0;
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#else
	return -1.0;
#endif
}

double halo_gpu_prepared(void)
{
	if (preparation.trace)
	{
		char detail[384];

		snprintf(detail, sizeof(detail), "draws=%lu sampled=%lu textures_cpu=%.6f key_cpu=%.6f "
			"pipeline_cpu=%.6f geometry_cpu=%.6f", preparation.attempts, preparation.sampled_draws,
			preparation.sampled_seconds[0], preparation.sampled_seconds[1], preparation.sampled_seconds[2],
			preparation.sampled_seconds[3]);
		halo_load_trace("prepare.frontend.sampled", detail);
	}
	preparation.active = FALSE;
	preparation.sample = FALSE;
	preparation.checkpoint = NULL;
	/* (the backend's twins' flag cleared as it ends) */
	world_ao.preparing = world_ao.twins = FALSE;
	preparation.duplicate = FALSE;
	platform_log("GPU: preparation reused %lu of %lu tracked draws", preparation.duplicates, preparation.draws);
	preparation_clear();
	xgpu_manifest_enumerating(0, 0);
	platform_log("GPU: the map's enumeration: %lu draws", (unsigned long)xgpu_manifest_enumeration()->draw_count);
	return device.gl_ready ? xgpu_backend_prepared() : 0.0;
}

/* ---------- tracing (debug.gpu_trace_frame) */

static BOOL trace_frame(void)
{
	static long frame = -2;

	if (frame == -2)
		frame = config_integer("debug.gpu_trace_frame");
	return frame >= 0 && device.frame == (unsigned long)frame;
}

static void trace_draw(const char *kind, D3DPRIMITIVETYPE type, unsigned long count, const float *first_vertex)
{
	struct vertex_shader_object *program = current_program();
	DWORD *rs = D3D__RenderState;

	if (!trace_frame())
		return;
	platform_log("%s type %d count %lu vs %lu (decl %lu) vp %lu,%lu %lux%lu z%.2f-%.2f zen %lu zw %lu zf %lx blend %lu %lx/%lx cull %lx cw %08lx tm %05lx cc %lx fin %08lx/%08lx at %lu/%lx",
		kind, type, count, program ? program->id : 0, device.vertex_shader ? device.vertex_shader->id : 0,
		device.viewport.X, device.viewport.Y, device.viewport.Width, device.viewport.Height,
		device.viewport.MinZ, device.viewport.MaxZ, rs[D3DRS_ZENABLE], rs[D3DRS_ZWRITEENABLE], rs[D3DRS_ZFUNC],
		rs[D3DRS_ALPHABLENDENABLE], rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND], rs[D3DRS_CULLMODE],
		rs[D3DRS_COLORWRITEENABLE], rs[D3DRS_PSTEXTUREMODES], rs[D3DRS_PSCOMBINERCOUNT],
		rs[D3DRS_PSFINALCOMBINERINPUTSABCD], rs[D3DRS_PSFINALCOMBINERINPUTSEFG],
		rs[D3DRS_ALPHATESTENABLE], rs[D3DRS_ALPHAFUNC]);
	{
		int stage;

		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			D3DBaseTexture *texture = device.textures[stage];
			struct xgpu_texture_description description;

			if (!texture || !((D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f))
				continue;
			xgpu_texture_describe(texture->Format, texture->Size, &description);
			platform_log("    t%d: data %08lx format %08lx size %08lx -> fmt %02lx %lux%lux%lu levels %lu linear %d cube %d rt %d min %lu mip %lu bias %g maxmip %lu",
				stage, texture->Data, texture->Format, texture->Size, description.format, description.width,
				description.height, description.depth, description.levels, description.linear, description.cube_map,
				xgpu_render_target_find(texture->Data) != NULL, D3D__TextureState[stage][D3DTSS_MINFILTER],
				D3D__TextureState[stage][D3DTSS_MIPFILTER], dword_to_float(D3D__TextureState[stage][D3DTSS_MIPMAPLODBIAS]),
				D3D__TextureState[stage][D3DTSS_MAXMIPLEVEL]);
		}
	}
	platform_log("    offset enable %lu slope %g offset %g zbias %ld stencil %lu func %lx ref %lx mask %lx write %lx ops %lx/%lx/%lx",
		rs[D3DRS_SOLIDOFFSETENABLE], dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]),
		dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]), (long)rs[D3DRS_ZBIAS], rs[D3DRS_STENCILENABLE],
		rs[D3DRS_STENCILFUNC], rs[D3DRS_STENCILREF], rs[D3DRS_STENCILMASK], rs[D3DRS_STENCILWRITEMASK],
		rs[D3DRS_STENCILFAIL], rs[D3DRS_STENCILZFAIL], rs[D3DRS_STENCILPASS]);
	if (config_boolean("debug.gpu_trace_constants"))
	{
		int constant;

		for (constant = 0; constant < XGPU_VERTEX_CONSTANT_COUNT; constant++)
		{
			const float *value = device.constants[constant];

			if (value[0] || value[1] || value[2] || value[3])
				platform_log("    c[%d] = %g %g %g %g", constant, value[0], value[1], value[2], value[3]);
		}
	}
	if (device.vertex_shader)
	{
		unsigned long index;

		for (index = 0; index < device.vertex_shader->element_count; index++)
		{
			const struct xgpu_vertex_element *element = &device.vertex_shader->elements[index];

			platform_log("    decl v%lu: stream %lu offset %lu type %02lx", (unsigned long)element->reg,
				(unsigned long)element->stream, (unsigned long)element->offset, (unsigned long)device.vertex_shader->types[index]);
		}
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			const float *value = device.attributes[index];

			if (value[0] || value[1] || value[2] || value[3] != 1.0f)
				platform_log("    current v%lu = %g %g %g %g", index, value[0], value[1], value[2], value[3]);
		}
	}
	if (first_vertex)
	{
		int reg;

		/* (the registers written since Begin: immediate_emit keeps those alone) */
		for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
		{
			const float *v = first_vertex + reg * 4;

			if ((device.immediate_written & (1UL << reg)) && (v[0] || v[1] || v[2] || v[3] != 1.0f))
				platform_log("    v%d = %g %g %g %g", reg, v[0], v[1], v[2], v[3]);
		}
	}
}

/* ---------- the contiguous window in the backend's buffers

Vertex and index buffers live in the Xbox's contiguous memory, where most
never change once loaded. The mirror keeps a copy of that memory in the
backend's buffers (one per segment, made when first needed) and uploads a page
only when it is first drawn from or after the game has written it: pages
are write-protected once uploaded, as cached textures are (memory_watch.c).
Pages the game rewrites frame after frame (dynamic vertices) would fault on
every write; after a few such rewrites a page counts as volatile for a
while, and draws that use it stream their data as before. */

#define MIRROR_SEGMENT_SIZE XGPU_MIRROR_SEGMENT_SIZE
#define MIRROR_SEGMENT_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_SEGMENT_SIZE)
/* The mirror's page is the write tracking's (memory_watch_page_size): the
host's page, 4 KB, or 16 KB on Apple's hardware (the Mac's and the
iPhone's) and on the Android and ARM Linux kernels that use them, 64 KB on
some ARM Linux ones. A smaller page would share its protection with others,
whose game-written vertices then faulted every frame: each upload protected
the whole host page again. The arrays have room for the smallest page, the
window's 4 KB (MIRROR_PAGE_UNIT). */
#define MIRROR_PAGE_UNIT 0x1000UL
#define MIRROR_PAGE_SLOTS (PLATFORM_CONTIGUOUS_SIZE / MIRROR_PAGE_UNIT)
static unsigned long mirror_page_size = MIRROR_PAGE_UNIT;
#define MIRROR_PAGE_SIZE mirror_page_size
#define MIRROR_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_PAGE_SIZE)
/* rewrites no more than this many frames apart ... */
#define MIRROR_REWRITE_FRAMES 2
/* ... this many times in a row make a page volatile ... */
#define MIRROR_VOLATILE_REWRITES 4
/* ... for this many frames, doubled each time the page turns volatile again
(to 32 times as many): a page that keeps proving dynamic is not watched
again, and faulted on, every few seconds */
#define MIRROR_VOLATILE_FRAMES 600

enum
{
	_mirror_page_absent,
	_mirror_page_present,
	_mirror_page_volatile
};

static struct
{
	unsigned char state[MIRROR_PAGE_SLOTS];
	unsigned char rewrites[MIRROR_PAGE_SLOTS];
	/* the page's memory_watch generation when it was uploaded */
	unsigned long generation[MIRROR_PAGE_SLOTS];
	unsigned long rewritten_frame[MIRROR_PAGE_SLOTS];
	/* the times the page has turned volatile (its period's doublings) */
	unsigned char volatile_times[MIRROR_PAGE_SLOTS];
} mirror;

/* the mirror's page, the write tracking's (as the device's backend is made
ready, before the mirror holds a page), once: a power of two from the unit
to a segment, else the unit */
static void mirror_page_size_set(unsigned long size)
{
	static BOOL set;

	if (set)
		return;
	set = TRUE;
	if (size >= MIRROR_PAGE_UNIT && size <= MIRROR_SEGMENT_SIZE && !(size & (size - 1)))
		mirror_page_size = size;
}

/* uploads the pages of [first, last) that are absent or stale; FALSE if one
of them turns out to be volatile */
static BOOL mirror_refresh(unsigned long first, unsigned long last)
{
	unsigned long page, run;
	BOOL volatile_page = FALSE;
	unsigned char stale[256];
	unsigned long count = last - first;

	if (count > sizeof(stale))
	{
		/* a range this long is refreshed in pieces */
		for (page = first; page < last; page += sizeof(stale))
		{
			if (!mirror_refresh(page, page + sizeof(stale) < last ? page + sizeof(stale) : last))
				return FALSE;
		}
		return TRUE;
	}
	for (page = first; page < last; page++)
	{
		BOOL written = mirror.state[page] == _mirror_page_present &&
			memory_watch_generation(PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE, MIRROR_PAGE_SIZE) >
			mirror.generation[page];

		stale[page - first] = mirror.state[page] != _mirror_page_present || written;
		if (written)
		{
			if (device.frame - mirror.rewritten_frame[page] <= MIRROR_REWRITE_FRAMES)
				mirror.rewrites[page]++;
			else
				mirror.rewrites[page] = 1;
			mirror.rewritten_frame[page] = device.frame;
			if (mirror.rewrites[page] >= MIRROR_VOLATILE_REWRITES)
			{
				mirror.state[page] = _mirror_page_volatile;
				if (mirror.volatile_times[page] < 6)
					mirror.volatile_times[page]++;
				volatile_page = TRUE;
			}
		}
	}
	if (volatile_page)
		return FALSE;
	for (page = first; page < last; page = run)
	{
		unsigned long segment = page * MIRROR_PAGE_SIZE / MIRROR_SEGMENT_SIZE;
		unsigned long address, size;
		/* no queued draw can read pages uploaded for the first time */
		BOOL unused = TRUE;

		if (!stale[page - first])
		{
			run = page + 1;
			continue;
		}
		for (run = page; run < last && stale[run - first]; run++)
		{
			if (mirror.state[run] != _mirror_page_present)
			{
				mirror.rewrites[run] = 0;
				mirror.rewritten_frame[run] = device.frame;
			}
			else
			{
				unused = FALSE;
			}
		}
		address = PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE;
		size = (run - page) * MIRROR_PAGE_SIZE;
		/* protect first, so a write racing with the upload is noticed */
		memory_watch_protect(address, size);
		for (; page < run; page++)
		{
			mirror.generation[page] = memory_watch_generation(PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE,
				MIRROR_PAGE_SIZE);
			mirror.state[page] = _mirror_page_present;
			if (journal_recording)
				journal_page(page);
		}
		xgpu_backend_mirror_write(segment, address - PLATFORM_CONTIGUOUS_BASE - segment * MIRROR_SEGMENT_SIZE, size,
			(const void *)address, unused);
	}
	return TRUE;
}

/* the pages of [address, address + size) the mirror holds and the game has
written since, uploaded now, before the frame's draws read their segments
(d3d8_mirror_rewritten): a load zone's switch writes a whole structure over
the one before, and uploaded as the frame's draws find them, each run of
those pages was a copy of its whole segment made in the frame (the backend's
buffer renamed, as a segment a draw of the frame has read must be), 27 of
them on a10's switch into its seventh structure, 8 ms of the GPU's time. Not
counted as a rewrite (the pages are not volatile for it), nor uploaded where
no draw has read them yet: those are uploaded as they are first drawn, as
ever. */
void d3d8_mirror_rewritten(unsigned long address, unsigned long size)
{
	unsigned long start, end, page, run;

	if (address < PLATFORM_CONTIGUOUS_BASE || !size)
		return;
	start = (address - PLATFORM_CONTIGUOUS_BASE) / MIRROR_PAGE_SIZE;
	end = (address - PLATFORM_CONTIGUOUS_BASE + size + MIRROR_PAGE_SIZE - 1) / MIRROR_PAGE_SIZE;
	if (end > MIRROR_PAGE_COUNT)
		end = MIRROR_PAGE_COUNT;
	for (page = start; page < end; page = run)
	{
		unsigned long segment = page * MIRROR_PAGE_SIZE / MIRROR_SEGMENT_SIZE;
		unsigned long segment_end = (segment + 1) * (MIRROR_SEGMENT_SIZE / MIRROR_PAGE_SIZE);
		unsigned long first;

		for (run = page; run < end && run < segment_end && mirror.state[run] == _mirror_page_present &&
			memory_watch_generation(PLATFORM_CONTIGUOUS_BASE + run * MIRROR_PAGE_SIZE, MIRROR_PAGE_SIZE) >
				mirror.generation[run];
			run++)
		{
		}
		if (run == page)
		{
			run = page + 1;
			continue;
		}
		/* protect first, so a write racing with the upload is noticed */
		memory_watch_protect(PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE, (run - page) * MIRROR_PAGE_SIZE);
		for (first = page; first < run; first++)
		{
			mirror.generation[first] = memory_watch_generation(PLATFORM_CONTIGUOUS_BASE + first * MIRROR_PAGE_SIZE,
				MIRROR_PAGE_SIZE);
		}
		xgpu_backend_mirror_write(segment, page * MIRROR_PAGE_SIZE - segment * MIRROR_SEGMENT_SIZE,
			(run - page) * MIRROR_PAGE_SIZE, (const void *)(PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE), FALSE);
	}
}

/* makes [address, address + size) current in the mirror, giving the segment
that holds it, the range's offset in that segment and the newest upload
generation of its pages (which changes whenever its contents do); FALSE if
the range is outside the window, spans two segments or is volatile */
static BOOL mirror_range(unsigned long address, unsigned long size, long *segment_found, unsigned long *offset,
	unsigned long *generation)
{
	unsigned long start = address - PLATFORM_CONTIGUOUS_BASE;
	unsigned long segment, first, last, page, oldest = ~0UL, newest = 0;
	BOOL present = TRUE;

	if (!size || address < PLATFORM_CONTIGUOUS_BASE || start + size > PLATFORM_CONTIGUOUS_SIZE)
		return FALSE;
	segment = start / MIRROR_SEGMENT_SIZE;
	if ((start + size - 1) / MIRROR_SEGMENT_SIZE != segment)
		return FALSE;
	first = start / MIRROR_PAGE_SIZE;
	last = (start + size - 1) / MIRROR_PAGE_SIZE + 1;
	for (page = first; page < last; page++)
	{
		if (mirror.state[page] == _mirror_page_volatile)
		{
			if (device.frame - mirror.rewritten_frame[page] <
				(MIRROR_VOLATILE_FRAMES << (mirror.volatile_times[page] ? mirror.volatile_times[page] - 1 : 0)))
				return FALSE;
			mirror.state[page] = _mirror_page_absent;
		}
		if (mirror.state[page] != _mirror_page_present)
		{
			present = FALSE;
		}
		else
		{
			if (mirror.generation[page] < oldest)
				oldest = mirror.generation[page];
			if (mirror.generation[page] > newest)
				newest = mirror.generation[page];
		}
	}
	if (!present || memory_watch_generation(address, size) > oldest)
	{
		if (!mirror_refresh(first, last))
			return FALSE;
		for (newest = 0, page = first; page < last; page++)
		{
			if (mirror.generation[page] > newest)
				newest = mirror.generation[page];
		}
	}
	*segment_found = (long)segment;
	*offset = start - segment * MIRROR_SEGMENT_SIZE;
	if (generation)
		*generation = newest;
	stats.mirrored_bytes += size;
	return TRUE;
}

/* the smallest and largest index of an index range the mirror holds: the
same ranges are drawn frame after frame */
#define INDEX_RANGE_SLOTS 4096

static struct
{
	unsigned long address;
	unsigned long count;
	unsigned long generation;
	WORD minimum;
	WORD maximum;
} index_ranges[INDEX_RANGE_SLOTS];

static void index_extent(const WORD *indices, unsigned long count, unsigned long generation, BOOL cached,
	unsigned long *minimum, unsigned long *maximum)
{
	unsigned long slot = (((unsigned long)indices >> 1) ^ (count * 2654435761UL)) % INDEX_RANGE_SLOTS;
	unsigned long index, low = 0xffff, high = 0;

	if (cached && index_ranges[slot].address == (unsigned long)indices && index_ranges[slot].count == count &&
		index_ranges[slot].generation == generation)
	{
		*minimum = index_ranges[slot].minimum;
		*maximum = index_ranges[slot].maximum;
		return;
	}
	for (index = 0; index < count; index++)
	{
		if (indices[index] < low)
			low = indices[index];
		if (indices[index] > high)
			high = indices[index];
	}
	if (cached)
	{
		index_ranges[slot].address = (unsigned long)indices;
		index_ranges[slot].count = count;
		index_ranges[slot].generation = generation;
		index_ranges[slot].minimum = (WORD)low;
		index_ranges[slot].maximum = (WORD)high;
	}
	*minimum = low;
	*maximum = high;
}

/* ---------- vertex data */

/* the backend reads D3DCOLOR elements as BGRA where it can
(xgpu_backend_capabilities.bgra_vertex_colors), or streams them swapped: a
stream with colors cannot come from the mirror then */
static BOOL stream_has_colors(const struct vertex_shader_object *declaration, unsigned long stream)
{
	unsigned long index;

	for (index = 0; index < declaration->element_count; index++)
	{
		if (declaration->elements[index].stream == stream && declaration->elements[index].format == XGPU_VERTEX_COLOR)
			return TRUE;
	}
	return FALSE;
}

/* the vertices [first, first + count) of every stream the declaration uses,
from the mirror where it holds them, else streamed by the backend; the
attributes' data then starts at vertex 0 of the range */
static void setup_streams(unsigned long first, unsigned long count)
{
	struct vertex_shader_object *declaration = device.vertex_shader;
	struct xgpu_vertex_input input;
	unsigned long index;
	double traced = preparation_time();

	memset(&input, 0, sizeof(input));
	input.declaration = declaration->id;
	input.element_count = declaration->element_count;
	memcpy(input.elements, declaration->elements, sizeof(input.elements));
	input.packed_mask = declaration->packed_mask;
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct xgpu_vertex_element *element = &declaration->elements[index];
		unsigned long stream = element->stream;
		unsigned long stride = device.streams[stream].stride;
		unsigned long bytes = stride ? stride * count : 64;
		unsigned long base;

		if (!device.streams[stream].data || element->format == XGPU_VERTEX_NONE_FORMAT || input.streams[stream].present)
			continue;
		base = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) + first * stride;
		input.streams[stream].present = TRUE;
		input.streams[stream].data = base;
		input.streams[stream].bytes = bytes;
		input.streams[stream].stride = stride;
		input.streams[stream].segment = -1;
		if ((xgpu_backend_capabilities.bgra_vertex_colors || !stream_has_colors(declaration, stream)) &&
			mirror_range(base, bytes, &input.streams[stream].segment, &input.streams[stream].offset, NULL))
		{
			continue;
		}
		input.streams[stream].segment = -1;
		stats.streamed_bytes += bytes;
	}
	memcpy(input.attributes, device.attributes, sizeof(input.attributes));
	if (!preparation.duplicate)
		xgpu_backend_set_vertices(&input);
	xgpu_manifest_note_vertices(&input);
	preparation_time_add(3, traced);
}

static int primitive_kind(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return XGPU_POINTS;
	case D3DPT_LINELIST: return XGPU_LINES;
	case D3DPT_LINELOOP: return XGPU_LINE_LOOP;
	case D3DPT_LINESTRIP: return XGPU_LINE_STRIP;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return XGPU_TRIANGLE_STRIP;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return XGPU_TRIANGLE_FAN;
	default: return XGPU_TRIANGLES;
	}
}

/* quads become two triangles each, in a buffer kept for the next draw's
(NULL if there is no memory for them) */
static WORD *quad_indices(const WORD *indices, unsigned long count, unsigned long *out_count)
{
	static WORD *buffer;
	static unsigned long capacity;
	unsigned long quads = count / 4;
	WORD *result;
	unsigned long quad;

	*out_count = 0;
	if (quads * 6 + 1 > capacity)
	{
		WORD *grown = realloc(buffer, (quads * 6 + 1) * sizeof(WORD));

		if (!grown)
			return NULL;
		buffer = grown;
		capacity = quads * 6 + 1;
	}
	result = buffer;

	for (quad = 0; quad < quads; quad++)
	{
		WORD v0 = indices ? indices[quad * 4] : (WORD)(quad * 4);
		WORD v1 = indices ? indices[quad * 4 + 1] : (WORD)(quad * 4 + 1);
		WORD v2 = indices ? indices[quad * 4 + 2] : (WORD)(quad * 4 + 2);
		WORD v3 = indices ? indices[quad * 4 + 3] : (WORD)(quad * 4 + 3);

		result[quad * 6 + 0] = v0;
		result[quad * 6 + 1] = v1;
		result[quad * 6 + 2] = v2;
		result[quad * 6 + 3] = v0;
		result[quad * 6 + 4] = v2;
		result[quad * 6 + 5] = v3;
	}
	*out_count = quads * 6;
	return result;
}

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	D3D__IndexData = index_data ? (WORD *)index_data->Data : NULL;
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	if (!vertex_count || !prepare_draw(FALSE, primitive_type))
		return;
	trace_draw("draw", primitive_type, vertex_count, NULL);
	setup_streams(start_vertex, vertex_count);
	if (preparation.duplicate)
	{
		preparation_draw_complete();
		return;
	}
	if (journal_recording)
		journal_draw(JOURNAL_DRAW, primitive_type, start_vertex, vertex_count, 0, 0);
	xgpu_manifest_record_draw(0, 0);
	if (primitive_type == D3DPT_QUADLIST)
	{
		unsigned long count;
		WORD *indices = quad_indices(NULL, vertex_count, &count);

		xgpu_backend_draw_indices(XGPU_TRIANGLES, indices, count);
	}
	else
	{
		xgpu_backend_draw(primitive_kind(primitive_type), vertex_count);
	}
	preparation_draw_complete();
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	unsigned long minimum, maximum, count, generation = 0, index_offset = 0;
	WORD *indices = NULL;
	const WORD *source = index_data;
	long segment = -1;
	BOOL mirrored;
	double traced;

	if (!vertex_count || !index_data || !prepare_draw(FALSE, primitive_type))
		return;
	traced = preparation_time();
	/* quads are drawn as triangles, from indices made for the draw */
	mirrored = primitive_type != D3DPT_QUADLIST && xgpu_backend_capabilities.base_vertex &&
		mirror_range((unsigned long)index_data, vertex_count * sizeof(WORD), &segment, &index_offset, &generation);
	index_extent(index_data, vertex_count, generation, mirrored, &minimum, &maximum);
	preparation_time_add(3, traced);
	trace_draw("indexed", primitive_type, vertex_count, NULL);
	/* (the streams from the base vertex on: index i is vertex base + i) */
	setup_streams(device.base_vertex_index + minimum, maximum - minimum + 1);
	if (preparation.duplicate)
	{
		preparation_draw_complete();
		return;
	}
	if (journal_recording)
		journal_draw(JOURNAL_DRAW_INDEXED, primitive_type, 0, vertex_count, device.base_vertex_index, minimum);
	xgpu_manifest_record_draw(0, 0);
	if (mirrored)
	{
		/* the attributes start at vertex minimum */
		xgpu_backend_draw_indexed(primitive_kind(primitive_type), index_data, vertex_count, minimum, segment,
			index_offset, FALSE);
		preparation_draw_complete();
		return;
	}
	stats.streamed_bytes += vertex_count * sizeof(WORD);
	count = vertex_count;
	if (primitive_type == D3DPT_QUADLIST)
	{
		indices = quad_indices(index_data, vertex_count, &count);
		source = indices;
	}
	xgpu_backend_draw_indexed(primitive_kind(primitive_type), source, count, minimum, -1, 0, indices != NULL);
	preparation_draw_complete();
}

/* ---------- immediate mode */

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type)
{
	device.immediate_active = TRUE;
	device.immediate_type = primitive_type;
	device.immediate_count = 0;
	device.immediate_written = 0;
}

/* the vertex the registers make: those written since Begin alone, the only
ones the backend takes (xgpu_backend_draw_immediate) */
static void immediate_emit(void)
{
	unsigned long floats = XGPU_VERTEX_ATTRIBUTE_COUNT * 4;
	unsigned long written = device.immediate_written;
	float *vertex;

	if (device.immediate_count == device.immediate_capacity)
	{
		unsigned long capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
		float *vertices = realloc(device.immediate_vertices, capacity * floats * sizeof(float));

		/* (out of memory: the vertex is dropped) */
		if (!vertices)
			return;
		device.immediate_vertices = vertices;
		device.immediate_capacity = capacity;
	}
	vertex = device.immediate_vertices + device.immediate_count * floats;
	while (written)
	{
		int reg = __builtin_ctzl(written);

		memcpy(vertex + reg * 4, device.attributes[reg], 4 * sizeof(float));
		written &= written - 1;
	}
	device.immediate_count++;
}

void WINAPI D3DDevice_End(void)
{
	unsigned long count = device.immediate_count, index_count = 0;
	D3DPRIMITIVETYPE type = device.immediate_type;
	WORD *indices = NULL;

	device.immediate_active = FALSE;
	if (!count || !prepare_draw(TRUE, type))
		return;
	trace_draw("immediate", type, count, device.immediate_vertices);
	if (preparation.duplicate)
	{
		preparation_draw_complete();
		return;
	}
	if (journal_recording)
		journal_draw(JOURNAL_DRAW_IMMEDIATE, type, 0, count, 0, 0);
	xgpu_manifest_record_draw(1, device.immediate_written);
	if (type == D3DPT_QUADLIST)
		indices = quad_indices(NULL, count, &index_count);
	xgpu_backend_draw_immediate(type == D3DPT_QUADLIST ? XGPU_TRIANGLES : primitive_kind(type), device.immediate_vertices,
		count, device.immediate_written, (const float (*)[4])device.attributes, indices, index_count);
	preparation_draw_complete();
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= XGPU_VERTEX_ATTRIBUTE_COUNT)
		return;
	if (device.immediate_active && !(device.immediate_written & (1UL << reg)))
	{
		/* a register first written after some of the vertices: theirs is
		the value it had (immediate_emit keeps only the written ones) */
		unsigned long vertex;

		for (vertex = 0; vertex < device.immediate_count; vertex++)
		{
			memcpy(device.immediate_vertices + (vertex * XGPU_VERTEX_ATTRIBUTE_COUNT + reg) * 4,
				device.attributes[reg], 4 * sizeof(float));
		}
		device.immediate_written |= 1UL << reg;
	}
	device.attributes[reg][0] = a;
	device.attributes[reg][1] = b;
	device.attributes[reg][2] = c;
	device.attributes[reg][3] = d;
	/* like the hardware, writing register 0 completes a vertex */
	if (device.immediate_active && (emit || reg == 0))
		immediate_emit();
}

void WINAPI D3DDevice_SetVertexData2f(INT reg, FLOAT a, FLOAT b)
{
	set_attribute(reg, a, b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4f(INT reg, FLOAT a, FLOAT b, FLOAT c, FLOAT d)
{
	set_attribute(reg, a, b, c, d);
}

void WINAPI D3DDevice_SetVertexData2s(INT reg, SHORT a, SHORT b)
{
	set_attribute(reg, (float)a, (float)b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4ub(INT reg, BYTE a, BYTE b, BYTE c, BYTE d)
{
	set_attribute(reg, a / 255.0f, b / 255.0f, c / 255.0f, d / 255.0f);
}

void WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	float value[4];

	color_to_vec4(color, value);
	set_attribute(reg, value[0], value[1], value[2], value[3]);
}

/* ---------- clearing */

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	float rgba[4];
	unsigned long clear = 0;
	BOOL has_depth = FALSE;
	DWORD index;

	if (journal_recording)
	{
		unsigned int arguments[4] = { flags, color, 0, stencil };

		memcpy(&arguments[2], &z, sizeof(z));
		journal_operation(JOURNAL_CLEAR, arguments, sizeof(arguments));
	}
	if (!device.gl_ready || !bind_targets(&has_depth))
		return;
	if (trace_frame())
		platform_log("clear flags %lx color %08lx z %g count %lu target %08lx depth %08lx", (unsigned long)flags,
			(unsigned long)color, z, (unsigned long)count,
			device.render_target ? (unsigned long)device.render_target->Data : 0,
			device.depth_stencil ? (unsigned long)device.depth_stencil->Data : 0);
	stats.clears++;
	color_to_vec4(color, rgba);
	if (flags & D3DCLEAR_TARGET)
	{
		/* the Xbox clears the channels named (D3DCLEAR_TARGET_R, _G, _B, _A):
		the fog screen clears only alpha, leaving the picture under the fog */
		clear |= XGPU_CLEAR_COLOR | ((flags & D3DCLEAR_TARGET_R) ? XGPU_CLEAR_RED : 0) |
			((flags & D3DCLEAR_TARGET_G) ? XGPU_CLEAR_GREEN : 0) | ((flags & D3DCLEAR_TARGET_B) ? XGPU_CLEAR_BLUE : 0) |
			((flags & D3DCLEAR_TARGET_A) ? XGPU_CLEAR_ALPHA : 0);
	}
	if (has_depth && (flags & D3DCLEAR_ZBUFFER))
		clear |= XGPU_CLEAR_DEPTH;
	if (has_depth && (flags & D3DCLEAR_STENCIL))
		clear |= XGPU_CLEAR_STENCIL;
	if (!(clear & (XGPU_CLEAR_COLOR | XGPU_CLEAR_DEPTH | XGPU_CLEAR_STENCIL)))
		return;
	if (!count || !rectangles)
	{
		/* the NV2A clips a viewport-less clear to the viewport, which is what
		keeps a split-screen window's clear from wiping the other window */
		long rectangle[1][4];

		rectangle[0][0] = target_pixel((float)device.viewport.X, 0);
		rectangle[0][1] = target_pixel((float)device.viewport.Y, 1);
		rectangle[0][2] = target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - rectangle[0][0];
		rectangle[0][3] = target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - rectangle[0][1];
		xgpu_backend_clear(clear, rgba, z, stencil, (const long (*)[4])rectangle, 1);
		return;
	}
	{
		long (*cleared)[4] = malloc(count * sizeof(*cleared));
		unsigned long cleared_count = 0;

		if (!cleared)
			return;
		for (index = 0; index < count; index++)
		{
			INT left = rectangles[index].x1 > device.viewport.X ? rectangles[index].x1 : device.viewport.X;
			INT top = rectangles[index].y1 > device.viewport.Y ? rectangles[index].y1 : device.viewport.Y;
			INT right = rectangles[index].x2 < device.viewport.X + device.viewport.Width ?
				rectangles[index].x2 : device.viewport.X + device.viewport.Width;
			INT bottom = rectangles[index].y2 < device.viewport.Y + device.viewport.Height ?
				rectangles[index].y2 : device.viewport.Y + device.viewport.Height;
			long *rectangle = cleared[cleared_count];

			if (left >= right || top >= bottom)
				continue;
			rectangle[0] = target_pixel((float)(left + UI_OFFSET), 0);
			rectangle[1] = target_pixel((float)top, 1);
			rectangle[2] = target_pixel((float)(right + UI_OFFSET), 0) - rectangle[0];
			rectangle[3] = target_pixel((float)bottom, 1) - rectangle[1];
			cleared_count++;
		}
		xgpu_backend_clear(clear, rgba, z, stencil, (const long (*)[4])cleared, cleared_count);
		free(cleared);
	}
}

/* ---------- the anti-aliasing passes */

/* display.anti_aliasing's pass over a window's 3D view, before the HUD and
menus are drawn over it (source/render/render.c); the window's bounds in the
game's units of the screen */
void halo_screen_anti_alias(short x0, short y0, short x1, short y1)
{
	struct render_target_entry *target;
	long corners[4];
	int mode = anti_aliasing();

	if (journal_recording)
	{
		short arguments[4] = { x0, y0, x1, y1 };

		journal_operation(JOURNAL_ANTI_ALIAS, arguments, sizeof(arguments));
	}
	if (!device.gl_ready || (mode != _anti_aliasing_fxaa && mode != _anti_aliasing_smaa) || screen_view)
		return;
	/* (the primary target's view: the back buffer's) */
	target = render_target_get(&device.back_buffer);
	if (!target)
		return;
	/* (no longer multisampled, if it was before the setting changed) */
	render_target_multisample(&target->target, 0);
	corners[0] = scaled_pixel(x0, target->target.scale[0]);
	corners[1] = scaled_pixel(y0, target->target.scale[1]);
	corners[2] = scaled_pixel(x1, target->target.scale[0]);
	corners[3] = scaled_pixel(y1, target->target.scale[1]);
	xgpu_backend_anti_alias(target->target.number, mode == _anti_aliasing_smaa, corners);
}

/* the corners of a window's 3D view in the back buffer's pixels */
static void view_corners(struct render_target_entry *target, short x0, short y0, short x1, short y1, long corners[4])
{
	corners[0] = scaled_pixel(x0, target->target.scale[0]);
	corners[1] = scaled_pixel(y0, target->target.scale[1]);
	corners[2] = scaled_pixel(x1, target->target.scale[0]);
	corners[3] = scaled_pixel(y1, target->target.scale[1]);
}

/* found once a pixel (xgpu_stream.h's WORLD_AO_PREPASS): the window's
pre-pass, into the backend's targets of the back buffer's size, where the
window draws into the back buffer with its depth buffer */
int halo_world_ao_prepass_found(void)
{
	return device.render_target == &device.back_buffer && device.depth_stencil == &device.depth_buffer;
}

int halo_world_ao_prepass_begin(void)
{
	struct render_target_entry *target;

	if (!device.gl_ready || !world_ao.active || world_ao.failed || screen_view || device.render_target != &device.back_buffer ||
		device.depth_stencil != &device.depth_buffer || !(target = render_target_get(&device.back_buffer)))
		return FALSE;
	world_ao.prepass = world_ao.sampled = TRUE;
	xgpu_backend_world_ao_prepass(XGPU_WORLD_AO_PREPASS_BEGIN, target->target.number, NULL);
	return TRUE;
}

/* ... done: the occlusion found from it over the window's view (its bounds
in the game's units of the screen), as the camera saw it */
void halo_world_ao_prepass_end(short x0, short y0, short x1, short y1, float const camera[4][4],
	float const cascades[4][4], unsigned long frame, int history)
{
	/* (the previous pass, its camera and corners, which the history is
	reprojected by) */
	static struct xgpu_world_ao_pass previous;
	static int previous_made;
	struct render_target_entry *target = render_target_get(&device.back_buffer);
	struct xgpu_world_ao_pass pass;
	long corners[4];
	int index;

	world_ao.prepass = FALSE;
	if (!target || !world_ao.sampled)
	{
		previous_made = 0;
		return;
	}
	memset(&pass, 0, sizeof(pass));
	view_corners(target, x0, y0, x1, y1, corners);
	memcpy(pass.camera, camera, sizeof(pass.camera));
	pass.camera[3][3] = world_ao.scale;
	for (index = 0; index < 4; index++)
		pass.corners[index] = (int32_t)corners[index];
	memcpy(pass.cascades, cascades, sizeof(pass.cascades));
	if (previous_made)
	{
		memcpy(pass.previous, previous.camera, sizeof(pass.previous));
		memcpy(pass.previous_corners, previous.corners, sizeof(pass.previous_corners));
	}
	/* (the history only where the last frame's was this window's, alike in
	place and scale) */
	pass.history[0] = history && previous_made && !memcmp(previous.corners, pass.corners, sizeof(pass.corners)) &&
		previous.camera[3][3] == pass.camera[3][3] ? 1.0f : 0.0f;
	pass.history[1] = (float)(frame & 0xFFFFu);
	pass.history[2] = XGPU_WORLD_AO_HISTORY_NEW;
	{
		/* (HALO_WORLD_AO_NO_TRACE, measuring: the shapes walked, as before
		the distance-field scene, no history) */
		static int traced = -1;

		if (traced < 0)
			traced = getenv("HALO_WORLD_AO_NO_TRACE") == NULL;
		pass.history[3] = !traced ? 0.0f : world_ao_mode() & WORLD_AO_RAYS ? 2.0f : 1.0f;
		if (!traced)
			pass.history[0] = 0.0f;
	}
	previous = pass;
	previous_made = 1;
	xgpu_backend_world_ao_prepass(XGPU_WORLD_AO_PREPASS_END, target->target.number, &pass);
}

/* display.panini's pass over a window's 3D view (port/linux/game/panini.c),
once it is whole, tone mapped and antialiased, before the HUD: the window's
bounds in the game's units of the screen, and the projection (its d, s and
magnification, then the rectilinear view's tangents at the window's left,
right, top and bottom) */
void halo_screen_panini(short x0, short y0, short x1, short y1, const float projection[8])
{
	struct render_target_entry *target;
	long corners[4];

	if (journal_recording)
	{
		unsigned int arguments[10];
		short bounds[4] = { x0, y0, x1, y1 };

		memset(arguments, 0, sizeof(arguments));
		memcpy(arguments, bounds, sizeof(bounds));
		if (projection)
			memcpy(arguments + 2, projection, 8 * sizeof(float));
		journal_operation(JOURNAL_PANINI, arguments, sizeof(arguments));
	}
	/* (no projection: the setting off, the pass only journaled, for a load
	with it on: port/linux/game/panini.c) */
	if (!device.gl_ready || !projection || screen_view)
		return;
	target = render_target_get(&device.back_buffer);
	if (!target)
		return;
	/* (still multisampled, if it is: the backend reads the view's pixels
	resolved and draws the pass into its samples, which the HUD is drawn
	into after. Its samples change only between frames: made again here,
	its attachment would come back for the HUD without the view, and the
	frame's resolve would put that over the pass) */
	view_corners(target, x0, y0, x1, y1, corners);
	xgpu_backend_panini(target->target.number, corners, projection);
}

/* display.hdr: the draws from now on the HUD's and the menus' again, after
a view of the world's drawn among them (the menus' Spartan's own view:
port/linux/game/menu_spartan.c), whose models' draws were the 3D view's */
void halo_hdr_interface(void)
{
	hdr.view = FALSE;
}

/* display.hdr's tone map of a window's 3D view (source/render/render.c),
before its anti-aliasing and the HUD: the draws from now on are the HUD's
and the menus' (xgpu_hdr.h) */
void halo_screen_tone_map(short x0, short y0, short x1, short y1)
{
	struct render_target_entry *target;
	long corners[4];

	if (journal_recording)
	{
		short arguments[4] = { x0, y0, x1, y1 };

		journal_operation(JOURNAL_TONE_MAP, arguments, sizeof(arguments));
	}
	hdr.view = FALSE;
	if (!device.gl_ready || !hdr.active)
		return;
	target = render_target_get(&device.back_buffer);
	if (!target || target->target.format != XGPU_FORMAT_RGBA16F)
		return;
	corners[0] = scaled_pixel(x0, target->target.scale[0]);
	corners[1] = scaled_pixel(y0, target->target.scale[1]);
	corners[2] = scaled_pixel(x1, target->target.scale[0]);
	corners[3] = scaled_pixel(y1, target->target.scale[1]);
	xgpu_backend_tone_map(target->target.number, corners);
}

/* ---------- extra views (above, "extra views") */

/* the screen's targets are the view's from now on (1 to HALO_VIEW_COUNT - 1),
its back buffer and depth buffer width by height of the screen's units */
void halo_view_begin(int view, unsigned long width, unsigned long height)
{
	if (view <= 0 || view >= HALO_VIEW_COUNT || !width || !height)
		return;
	if (width > (unsigned long)halo_screen_width())
		width = (unsigned long)halo_screen_width();
	if (height > SCREEN_HEIGHT)
		height = SCREEN_HEIGHT;
	/* (its images of another size freed between frames) */
	if (views[view].width && (views[view].width != width || views[view].height != height))
		views_released = TRUE;
	views[view].width = width;
	views[view].height = height;
	screen_view = view;
}

/* ... and the screen's own again: the view's image made opaque (its alpha,
which its draws leave as they please, 1), as the HUD draws it */
void halo_view_end(void)
{
	struct render_target_entry *target;

	if (!screen_view)
		return;
	target = device.gl_ready ? render_target_get(&device.back_buffer) : NULL;
	if (target)
	{
		static const float opaque[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		long rectangle[1][4];

		rectangle[0][0] = 0;
		rectangle[0][1] = 0;
		rectangle[0][2] = (long)target->target.pixel_width;
		rectangle[0][3] = (long)target->target.pixel_height;
		xgpu_backend_set_targets(target->target.number, -1, 0, 1, 0, 0);
		xgpu_backend_clear(XGPU_CLEAR_COLOR | XGPU_CLEAR_ALPHA, opaque, 1.0f, 0, (const long (*)[4])rectangle, 1);
		target->target.written = ++render_target_write_serial;
		target->last_rendered = device.frame + 1;
	}
	screen_view = 0;
}

/* whether the view's image is there to be drawn: drawn since its targets
were made (a change of the screen's resolution or format makes them again) */
int halo_view_drawn(int view)
{
	struct render_target_entry *entry;

	if (view <= 0 || view >= HALO_VIEW_COUNT || !views[view].width)
		return FALSE;
	for (entry = render_targets; entry; entry = entry->next)
	{
		if (entry->view == view && entry->screen_buffer && !entry->target.depth && entry->last_rendered &&
			!render_target_stale(entry))
			return TRUE;
	}
	return FALSE;
}

/* the draws from now on sample the view's image in stage 0 in place of its
texture (0: their textures again): its pixels across the texture's
coordinates 0 to 1, the view's width and height */
void halo_view_sample(int view)
{
	sampled_view = view > 0 && view < HALO_VIEW_COUNT ? view : 0;
}

void halo_view_release(int view)
{
	if (view <= 0 || view >= HALO_VIEW_COUNT || !views[view].width)
		return;
	views[view].width = views[view].height = 0;
	views_released = TRUE;
}

/* display.overshield_effect's rise (rasterizer_xbox_plasma_energy.c,
port/linux/game/overshield_effect.c), OpenCE's: a fade above a height on
the model, in OpenCE's OpenGL vertex shaders. Neither of OpenH1's emitters
(SPIR-V, MSL) has it yet, so the overshield is drawn whole as it
rises; Video Setup has no row for it (tools/port_settings.py) */
void d3d8_gl_set_height_fade(float inverse_softness, float height, float offset, float scale)
{
	(void)inverse_softness;
	(void)height;
	(void)offset;
	(void)scale;
}

/* ---------- presentation */

/* the screenshot key's picture (controls.screenshot): the frame as it is
presented, written by the host as a PNG in the data folder's screenshots
(xgpu_backend_screenshot: named for the local date and time), and a note on
screen */
static void write_key_screenshot(struct render_target_entry *target)
{
	char directory[512], name[64];

	if (snprintf(directory, sizeof(directory), "%s/screenshots", platform_data_root()) >= (int)sizeof(directory))
	{
		console_printf(FALSE, "Screenshot failed: the folder's path is too long");
		return;
	}
	if (xgpu_backend_screenshot(target->target.number, directory, name, sizeof(name)))
	{
		console_printf(FALSE, "Screenshot saved: %s", name);
		platform_log("screenshot: saved %s/%s", directory, name);
	}
	else
	{
		console_printf(FALSE, "Screenshot failed: %s", name[0] ? name : "no renderer");
		platform_log("screenshot: failed (%s)", name[0] ? name : "no renderer");
	}
}

/* writes BGRA pixels as a BMP named <name><frame>.bmp in the screenshot
folder: rows from the top (as the game's targets hold them), or from the
bottom (as the VR mode's eye images do) */
void xgpu_write_bmp(const char *name, const unsigned char *pixels, unsigned long width, unsigned long height,
	BOOL bottom_up)
{
	const char *directory = config_string("debug.screenshot_directory");
	unsigned char header[54] = { 'B', 'M' };
	unsigned long image_size = width * height * 4;
	unsigned long row;
	char path[512];
	FILE *file;

	/* (a name of its own, the menu tour's, as it is: halo_screenshot_named) */
	if (name[0] == '=')
		snprintf(path, sizeof(path), "%s/%s.bmp", directory, name + 1);
	else
		snprintf(path, sizeof(path), "%s/%s%05lu.bmp", directory, name, xgpu_screenshot_number());
	file = fopen(path, "wb");
	if (!file)
		return;
	*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
	*(unsigned int *)(header + 10) = 54;
	*(unsigned int *)(header + 14) = 40;
	*(int *)(header + 18) = (int)width;
	*(int *)(header + 22) = bottom_up ? (int)height : -(int)height;
	*(unsigned short *)(header + 26) = 1;
	*(unsigned short *)(header + 28) = 32;
	*(unsigned int *)(header + 34) = (unsigned int)image_size;
	fwrite(header, 1, sizeof(header), file);
	for (row = 0; row < height; row++)
		fwrite(pixels + row * width * 4, 1, width * 4, file);
	fclose(file);
}

/* the determinism check's hash of each frame (port/linux/game/determinism_log.c) */
int determinism_log_frames(void);
void determinism_log_frame(unsigned char const *pixels, long width, long height);
unsigned long xgpu_screenshot_number(void)
{
	return device.frame;
}

/* port/linux/src/dsound_sdl.c: the sound recorded (debug.audio_record; with
the frames' time fixed, mixed in step with them: frames_clock.c) */
unsigned long dsound_sdl_recorded_frames(void);

/* debug.screenshot_stream: each frame's pixels (BGRA, top row first, no
header) appended to that file, opened once: a pipe an encoder reads, so
that a long run's frames never reach the disk */
static void write_screenshot_stream(const unsigned char *pixels, unsigned long width, unsigned long height)
{
	static FILE *stream;
	static BOOL failed;

	if (!stream && !failed)
	{
		stream = fopen(config_string("debug.screenshot_stream"), "wb");
		failed = !stream;
		if (failed)
			platform_log("screenshot stream: cannot write %s", config_string("debug.screenshot_stream"));
	}
	if (!stream)
		return;
	fwrite(pixels, 1, width * height * 4, stream);
	fflush(stream);
}

/* the next frame saved as a PNG in a folder, as the screenshot key saves
one (the menu studio's notes: port/linux/game/menu_studio.c), and the name
it was saved as, once */
static char screenshot_png_directory[512];
static char screenshot_png_name[128];
static BOOL screenshot_png_taken;

void halo_screenshot_png(const char *directory)
{
	snprintf(screenshot_png_directory, sizeof(screenshot_png_directory), "%s", directory);
	screenshot_png_taken = FALSE;
}

BOOL halo_screenshot_png_taken(char *name, unsigned long size)
{
	if (!screenshot_png_taken)
		return FALSE;
	screenshot_png_taken = FALSE;
	snprintf(name, size, "%s", screenshot_png_name);
	return TRUE;
}

/* the next frame saved as debug.screenshot_directory's <name>.bmp (the
menu tour's: port/linux/game/menu_tour.c) */
static char screenshot_name[128];

void halo_screenshot_named(const char *name)
{
	snprintf(screenshot_name, sizeof(screenshot_name), "=%s", name ? name : "");
	if (!name || !name[0])
		screenshot_name[0] = 0;
}

static void write_screenshot(struct render_target_entry *target)
{
	unsigned long width = target->target.pixel_width, height = target->target.pixel_height;
	unsigned char *pixels;
	unsigned long index;

	if (!*config_string("debug.screenshot_directory") && !*config_string("debug.screenshot_stream"))
	{
		screenshot_name[0] = 0;
		return;
	}
	pixels = xgpu_backend_read_target(target->target.number, -1, FALSE);
	if (!pixels)
		return;
	/* the display ignores destination alpha, which the game uses as scratch;
	image viewers would show it as transparency */
	for (index = 0; index < width * height; index++)
		pixels[index * 4 + 3] = 0xff;
	/* (where in debug.audio_record's sound the first frame saved falls, to
	lay the sound under the frames) */
	{
		static BOOL first_logged;

		if (!first_logged)
		{
			first_logged = TRUE;
			platform_log("screenshot: the first frame saved at the recorded sound's frame %lu",
				dsound_sdl_recorded_frames());
		}
	}
	if (*config_string("debug.screenshot_stream"))
		write_screenshot_stream(pixels, width, height);
	else
		xgpu_write_bmp(screenshot_name[0] ? screenshot_name : "frame", pixels, width, height, FALSE);
	screenshot_name[0] = 0;
	free(pixels);
}

#ifdef HALO_VR
/* ---------- the VR mode's passes */

/* the eye target as the eye pass draws it: the eyes' images, or foveated,
the four layers of the inset and the periphery (xgpu.h) */
static void vr_eye_target(BOOL foveated)
{
	struct xgpu_render_target *target = &vr_gl.eye_target.target;
	int width = foveated ? vr_gl.fovea_width : vr_gl.eye_width;
	int height = foveated ? vr_gl.fovea_height : vr_gl.eye_height;

	target->scale[0] = (float)width / 640.0f;
	target->scale[1] = (float)height / (float)SCREEN_HEIGHT;
	target->pixel_width = (unsigned long)width;
	target->pixel_height = (unsigned long)height;
	target->layers = foveated ? 4 : 2;
}

static void vr_initialize(void)
{
	struct xgpu_vr_info info;

	if (!halo_vr_enabled())
		return;
	/* (no session: the window is shown, to play flat in it,
	platform_video_vr_unavailable) */
	if (!xgpu_backend_vr_supported())
	{
		platform_log("VR unavailable: the renderer cannot draw for the headset; playing flat");
		platform_video_vr_unavailable();
		return;
	}
	if (!halo_vr_initialize())
	{
		platform_log("VR unavailable: no OpenXR session (the log above says why); playing flat");
		platform_video_vr_unavailable();
		return;
	}
	halo_vr_eye_size(&vr_gl.eye_width, &vr_gl.eye_height);
	halo_vr_hud_size(&vr_gl.hud_width, &vr_gl.hud_height);
	vr_gl.scope_size = halo_vr_scope_size();
	vr_gl.scope_scale = (float)vr_gl.scope_size / (float)SCREEN_HEIGHT;
	vr_gl.statistics = config_boolean("debug.gpu_stats");
	memset(&info, 0, sizeof(info));
	info.eye_width = vr_gl.eye_width;
	info.eye_height = vr_gl.eye_height;
	info.hud_width = vr_gl.hud_width;
	info.hud_height = vr_gl.hud_height;
	info.scope_size = vr_gl.scope_size;
	info.statistics = vr_gl.statistics;
	info.gpu_time = vr_gl.statistics && config_boolean("debug.vr_gpu_time");
	xgpu_backend_vr_initialize(&info);
	vr_gl.eye_target.target.number = next_target_number++;
	vr_gl.eye_target.target.width = 640;
	vr_gl.eye_target.target.height = SCREEN_HEIGHT;
	vr_eye_target(FALSE);
	vr_gl.eye_alone = -1;
	vr_gl.foveated = info.foveated;
	vr_gl.fovea_width = info.fovea_width;
	vr_gl.fovea_height = info.fovea_height;
	xgpu_foveated = info.foveated ? 1 : 0;
	vr_gl.ready = TRUE;
	/* the screen becomes the menus' 640x480 at the HUD's scale (the device
	is still being made: halo_screen_commit would leave its surfaces) */
	halo_screen_commit();
	device.presentation.BackBufferWidth = 640;
	d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, 640, SCREEN_HEIGHT);
	d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, 640, SCREEN_HEIGHT);
	if (halo_vr_frame_active())
		halo_vr_pass(HALO_VR_PASS_HUD);
}

static BOOL vr_screen_mode(long *width, float scale[2])
{
	if (!vr_gl.ready)
		return FALSE;
	*width = 640;
	scale[0] = (float)vr_gl.hud_width / 640.0f;
	scale[1] = (float)vr_gl.hud_height / (float)SCREEN_HEIGHT;
	return TRUE;
}

void halo_vr_pass(int pass)
{
	if (!vr_gl.ready)
		return;
	if (pass != HALO_VR_PASS_NONE && !halo_vr_frame_active())
		pass = HALO_VR_PASS_NONE;
	if (pass == HALO_VR_PASS_EYES)
	{
		unsigned int image = halo_vr_image(VR_SWAPCHAIN_EYES);

		if (image)
		{
			vr_gl.eye_target.target.data = device.back_buffer.Data;
			/* (foveated, its four layers, made into the image at the
			present) */
			vr_eye_target(vr_gl.foveated);
			xgpu_backend_vr_eyes(vr_gl.eye_target.target.number, image);
			vr_gl.eye_target.last_rendered = device.frame + 1;
			screen_scale[0] = vr_gl.eye_target.target.scale[0];
			screen_scale[1] = vr_gl.eye_target.target.scale[1];
		}
		else
		{
			pass = HALO_VR_PASS_HUD;
		}
	}
	if (pass == HALO_VR_PASS_SCOPE && vr_gl.scope_size <= 0)
		pass = HALO_VR_PASS_HUD;
	if (pass == HALO_VR_PASS_SCOPE)
	{
		/* (its targets, at their own scale, are the game's ordinary 1-layer
		ones, apart from the HUD's: xgpu_render_target_find) */
		screen_scale[0] = screen_scale[1] = vr_gl.scope_scale;
		vr_gl.scope_drawn = TRUE;
	}
	else if (pass != HALO_VR_PASS_EYES)
	{
		screen_scale[0] = (float)vr_gl.hud_width / 640.0f;
		screen_scale[1] = (float)vr_gl.hud_height / (float)SCREEN_HEIGHT;
	}
	if (pass == HALO_VR_PASS_EYES)
		xgpu_backend_vr_timing(1);
	else if (pass == HALO_VR_PASS_HUD && vr_gl.pass == HALO_VR_PASS_EYES)
		xgpu_backend_vr_timing(2);
	vr_gl.pass = pass;
	vr_gl.multiview = FALSE;
	vr_gl.left_eye = FALSE;
	vr_gl.right_eye = FALSE;
	vr_gl.eye_alone = -1;
	xgpu_backend_vr_pass(pass);
	if (pass == HALO_VR_PASS_HUD && !vr_gl.hud_drawn)
	{
		/* the frame's HUD starts transparent */
		static const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		struct render_target_entry *hud = render_target_get(&device.back_buffer);

		if (hud)
		{
			xgpu_backend_clear_target(hud->target.number, transparent);
			hud->last_rendered = device.frame + 1;
			vr_gl.hud_drawn = TRUE;
		}
		/* the HUD's groups at the frame's start: the centre's, and the
		other groups' targets drawn into as they are first drawn
		(xgpu_backend_set_targets) while the HUD has panels */
		{
			struct vr_visor_panel panels[VR_PANEL_COUNT];

			vr_gl.hud_panels = vr_visor_panels(panels) != 0;
			xgpu_backend_vr_groups_reset();
			halo_vr_hud_group(HALO_VR_HUD_GROUP_CENTRE);
		}
	}
	xgpu_backend_invalidate();
}

/* the text the eye pass draws (the players' names, the waypoints' distances:
vr_render.c's marks over the world) is rasterized for the HUD's pixels, as large as it is
drawn there (text_hires.c): its glyphs are for one scale at a time, and the
eyes' would start them over twice a frame; 0 in the other passes */
static float vr_text_pixel_scale(void)
{
	return vr_gl.pass == HALO_VR_PASS_EYES && vr_gl.hud_height > 0 ? (float)vr_gl.hud_height / (float)SCREEN_HEIGHT : 0.0f;
}

/* whether the draws now are the eyes' 3D view (display.model_detail "auto") */
static int vr_drawing_eyes(void)
{
	return vr_gl.pass == HALO_VR_PASS_EYES;
}

/* the draws from now on into that eye's layer alone (0 left, 1 right), or
both (-1): the crosshairs, each eye's in a draw of its own */
void halo_vr_draw_eye(int eye)
{
	vr_gl.eye_alone = eye;
}

/* the model whose nodes' matrices are set now (rasterizer_set_model_skinning)
is mirrored, its axes left-handed (a left-hander's first-person weapon and
hands, vr.dominant_hand): what was its front is its back, until the next
model's, or the model's end */
void halo_vr_mirrored_draws(int mirrored)
{
	vr_gl.mirrored = mirrored != 0;
}

int halo_vr_mirrored(void)
{
	return vr_gl.mirrored;
}

/* the effect drawn from now on (an effect shader's group, its particles,
contrails or weather) is soft against the scene's depth (1), and thick
(2), in the eyes' pass (the backend's VR effects: port/linux/vulkan/README.md
"VR"); 0 for the draws after it */
void halo_vr_effect_depth(int mode)
{
	vr_gl.effect_depth = mode >= 0 && mode <= 2 ? mode : 0;
}

/* ---------- the HUD's groups (vr.h, vr_visor.c's panels) */

void halo_vr_hud_group(int group)
{
	vr_gl.hud_group_mode = group;
	if (group >= 0 && group < HALO_VR_HUD_GROUPS)
		vr_gl.hud_group = group;
	else
		vr_gl.hud_group = HALO_VR_HUD_GROUP_CENTRE;
}

void halo_vr_hud_anchor(int corner)
{
	/* (hud_definitions.h's hud_anchor: top left, top right, bottom left,
	bottom right, centre) */
	static const int groups[5] =
	{
		HALO_VR_HUD_GROUP_WEAPON, HALO_VR_HUD_GROUP_STATUS, HALO_VR_HUD_GROUP_TRACKER, HALO_VR_HUD_GROUP_CENTRE,
		HALO_VR_HUD_GROUP_CENTRE,
	};

	if (vr_gl.hud_group_mode == HALO_VR_HUD_BY_ANCHOR && corner >= 0 && corner < 5)
		vr_gl.hud_group = groups[corner];
}

/* a test's screenshot of this frame (as debug.screenshot_every's, into
debug.screenshot_directory: port/linux/game/vr_death.c's) */
static BOOL vr_screenshot_wanted;

void halo_vr_test_screenshot(void)
{
	vr_screenshot_wanted = TRUE;
}

/* presents a VR frame: the targets each pass drew, to the backend, which
composes the frame and ends it with OpenXR */
static void vr_present(BOOL screenshot)
{
	struct render_target_entry *hud = NULL, *eye_depth = NULL, *scope = NULL;
	struct xgpu_vr_frame frame;

	xgpu_backend_vr_timing(3);
	/* the eyes' depth as drawn (foveated, the four layers'), for the
	smart-link's window */
	if (vr_gl.eyes_drawn)
	{
		int pass = vr_gl.pass;
		float scale[2];

		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
		vr_gl.pass = HALO_VR_PASS_EYES;
		screen_scale[0] = vr_gl.eye_target.target.scale[0];
		screen_scale[1] = vr_gl.eye_target.target.scale[1];
		eye_depth = render_target_get(&device.depth_buffer);
		vr_gl.pass = pass;
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
		if (eye_depth && eye_depth->target.layers != (vr_gl.foveated ? 4 : 2))
			eye_depth = NULL;
	}
	/* the scope's view: the smart-link's window drawn into the eyes, or
	the lens's image for its layer */
	if (vr_gl.scope_drawn && vr_gl.eyes_drawn)
	{
		int pass = vr_gl.pass;
		float scale[2];

		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
		vr_gl.pass = HALO_VR_PASS_SCOPE;
		screen_scale[0] = screen_scale[1] = vr_gl.scope_scale;
		scope = render_target_get(&device.back_buffer);
		vr_gl.pass = pass;
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
	}
	if (vr_gl.hud_drawn)
	{
		int pass = vr_gl.pass;

		vr_gl.pass = HALO_VR_PASS_HUD;
		screen_scale[0] = (float)vr_gl.hud_width / 640.0f;
		screen_scale[1] = (float)vr_gl.hud_height / (float)SCREEN_HEIGHT;
		hud = render_target_get(&device.back_buffer);
		vr_gl.pass = pass;
	}
	memset(&frame, 0, sizeof(frame));
	frame.eyes_drawn = vr_gl.eyes_drawn;
	frame.hud_drawn = vr_gl.hud_drawn;
	frame.scope_drawn = vr_gl.scope_drawn;
	frame.hud_panels = vr_gl.hud_panels;
	frame.eye_target = vr_gl.eye_target.target.number;
	frame.eye_depth = eye_depth ? eye_depth->target.number : 0;
	frame.eye_depth_layers = eye_depth ? eye_depth->target.layers : 0;
	frame.scope = scope ? scope->target.number : 0;
	frame.hud = hud ? hud->target.number : 0;
	xgpu_backend_vr_compose(&frame);
	/* (the eye pass's back buffer is the eyes' images again, for the rest
	of the present) */
	vr_eye_target(FALSE);
	vr_gl.pass = HALO_VR_PASS_NONE;
	vr_gl.multiview = FALSE;
	vr_gl.left_eye = FALSE;
	vr_gl.right_eye = FALSE;
	vr_gl.eye_alone = -1;
	xgpu_backend_vr_submit(&frame, screenshot);
	vr_gl.eyes_drawn = FALSE;
	vr_gl.hud_drawn = FALSE;
	vr_gl.scope_drawn = FALSE;
	xgpu_backend_vr_groups_reset();
	halo_vr_hud_group(HALO_VR_HUD_GROUP_CENTRE);
	xgpu_backend_vr_timing(0);
}

/* the next frame begins when the game first draws (vr.c waits for it
then): until the 3D view, everything goes to the HUD */
static void vr_frame_resume(void)
{
	if (vr_gl.ready && vr_gl.pass == HALO_VR_PASS_NONE && halo_vr_frame_active())
		halo_vr_pass(HALO_VR_PASS_HUD);
}
#endif

/* when the game read the input of the frame it draws (frame_pacing.h) */
static uint64_t frame_input_time;

uint64_t d3d_frame_input_time(void)
{
	return frame_input_time;
}

/* now, in that clock (debug.latency_log's, game/latency_log.c) */
uint64_t d3d_frame_clock_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

/* port/linux/game/sim_trace.c: the simulation's clock, seconds from the
scenario's start (negative before it starts) */
extern double sim_trace_scenario_seconds(void);

/* debug.screenshot_every's frame, while the simulation's clock is between
debug.screenshot_from and debug.screenshot_until (seconds; 0, no bound), so
that a part of a run played again the same way is saved */
static BOOL screenshot_this_frame(void)
{
	static long every = -1;
	static double from, until;
	double now;

	if (every < 0)
	{
		every = config_integer("debug.screenshot_every");
		from = config_real("debug.screenshot_from");
		until = config_real("debug.screenshot_until");
	}
	if (every <= 0 || device.frame % (unsigned long)every != 0)
		return FALSE;
	if (from <= 0.0 && until <= 0.0)
		return TRUE;
	now = sim_trace_scenario_seconds();
	return now >= from && (until <= 0.0 || now <= until);
}

/* the frame being drawn's pixels so far (BGRA, rows from the top; malloc's,
the caller's to free), and its size; NULL if there is none: a menu
picture's frame (port/linux/game/menu_pictures_render.c) */
unsigned char *d3d8_back_buffer_read(unsigned long *width, unsigned long *height)
{
	struct render_target_entry *back_buffer;
	unsigned char *pixels;

	if (!device.gl_ready || !(back_buffer = render_target_get(&device.back_buffer)) || !back_buffer->target.number)
		return NULL;
	render_target_resolve(&back_buffer->target);
	pixels = xgpu_backend_read_target(back_buffer->target.number, -1, TRUE);
	if (pixels)
	{
		*width = back_buffer->target.pixel_width;
		*height = back_buffer->target.pixel_height;
	}
	return pixels;
}

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;

#ifdef HALO_VR
	if (device.gl_ready && vr_gl.ready)
	{
		/* (the window stays hidden: the headset paces the frames) */
		vr_present(vr_screenshot_wanted || screenshot_this_frame());
		vr_screenshot_wanted = FALSE;
		xgpu_backend_frame_end();
	}
	else
#endif
	if (device.gl_ready)
	{
		struct render_target_entry *back_buffer = render_target_get(&device.back_buffer);

		if (trace_frame())
			platform_log("present back buffer %08lx target %u", (unsigned long)device.back_buffer.Data,
				back_buffer->target.number);
		render_target_resolve(&back_buffer->target);
		xgpu_backend_visibility_frame();
		if (screenshot_this_frame() || screenshot_name[0])
			write_screenshot(back_buffer);
		if (determinism_log_frames())
		{
			unsigned char *pixels = xgpu_backend_read_target(back_buffer->target.number, -1, FALSE);

			if (pixels)
			{
				determinism_log_frame(pixels, (long)back_buffer->target.pixel_width,
					(long)back_buffer->target.pixel_height);
				free(pixels);
			}
		}
		if (screenshot_png_directory[0])
		{
			screenshot_png_name[0] = 0;
			if (!xgpu_backend_screenshot(back_buffer->target.number, screenshot_png_directory, screenshot_png_name,
				sizeof(screenshot_png_name)))
			{
				screenshot_png_name[0] = 0;
			}
			screenshot_png_directory[0] = 0;
			screenshot_png_taken = TRUE;
		}
		if (platform_screenshot_take_request())
			write_key_screenshot(back_buffer);
		/* (Save Report's zip, this frame its screenshot: report.c) */
		if (report_take_request())
		{
			char message[160];

			report_write(back_buffer->target.number, TRUE, message, sizeof(message));
			if (message[0])
				console_printf(FALSE, "%s", message);
		}
		/* (a menu picture's frame: kept, and not shown, the preparing
		screen staying: port/linux/game/menu_pictures_render.c) */
		if (!menu_pictures_frame_kept())
		xgpu_backend_present(back_buffer->target.number);
		memory_watch_begin_frame();
		xgpu_backend_frame_end();
	}
	/* (a report asked for with no frame shown: the VR mode's, no renderer's;
	no screenshot) */
	if (report_take_request())
	{
		char message[160];

		report_write(0, TRUE, message, sizeof(message));
		if (message[0])
			console_printf(FALSE, "%s", message);
	}
	device.frame++;
	stats.presents++;
	report_frame_presented();
	/* (display.hdr: a frame begins with no 3D view drawn) */
	hdr.view = FALSE;
	hdr.category = -1;
	hdr.kind = 0;
	if (debug_settings.statistics && device.frame % 60 == 0)
	{
		platform_log("frame %lu: %lu draws, %lu immediate, %lu clears, %lu target changes; skipped %lu no program, %lu no target, %lu link; "
			"%lu KB mirrored, %lu KB streamed",
			device.frame, stats.draws / stats.presents, stats.immediate_draws / stats.presents, stats.clears / stats.presents,
			stats.target_changes / stats.presents, stats.skipped_no_program, stats.skipped_no_target, stats.skipped_link,
			stats.mirrored_bytes / stats.presents / 1024, stats.streamed_bytes / stats.presents / 1024);
		memset(&stats, 0, sizeof(stats));
	}
	platform_pump_events();
	/* (the next frame's input is what was just pumped: the main loop reads
	it at once, frame_pacing.h) */
	{
		struct timespec now;

		clock_gettime(CLOCK_MONOTONIC, &now);
		frame_input_time = (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
	}

	/* the loading screens present before main_initialize_time sets the
	vertical blank callback, and without the thread nothing retires a
	queued frame */
	if (!halo_interpolation_enabled())
		vertical_blank_start();
	pthread_mutex_lock(&vertical_blank_lock);
	/* the Xbox keeps at most two frames queued behind its 60 Hz display;
	with interpolation, frames come at the real display's rate instead,
	paced by vsync (platform_video_swap) */
	if (halo_interpolation_enabled())
	{
		flip_count++;
	}
	else
	{
		while (pending_flips >= 2)
			pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
		pending_flips++;
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}

/* ---------- a map's preparation's journal (port/linux/game/render_prepared.c)

What a map's preparation (render_enumerate.c: its first frame drawn through
every shader) gives the backend, kept so that a later load of the map gives
it the same without the walk: each draw that was not a repeat
(preparation_seen), as the state it was drawn in (the render and texture
states, the textures, targets, viewport, vertex shader and streams, and the
front end's own that prepare_draw reads); each operation of a window that a
backend prepares something for (a clear, the anti-aliasing, Panini, the
tone map, the glows); each texture the cache loaded
for them, in order; and the mirror's pages uploaded. Played back, each draw
is made again in its state from a vertex or a few (a backend prepares its
pipeline and binds its textures; it draws nothing), each operation again,
each texture loaded and each page uploaded, so that a backend, Vulkan's or
Metal's, is given what the walk gave it; the front end derives the draws'
pipelines again, from the settings as they are now. Objects are kept as what
they are, not where: a cached bitmap by its tag and index (the texture
cache's), a texture, surface or palette the device made by the number of
its making (checked by its type and format), the device's own targets by
name, a vertex shader by its id.

The words: an entry's first, its type in the low 8 bits and in the high 24
the count of its state's words that differ from the entry's before, a pair
(the word's index, its value) each after it; a texture's, JOURNAL_TEXTURE,
then its tag's index and the bitmap's; the pages', JOURNAL_PAGES and the
count of runs, then each run's first unit and the unit past it. A unit is
JOURNAL_PAGE_UNIT's bytes of the contiguous window, whatever the platform's
mirror page (MIRROR_PAGE_SIZE: the host's, 4 KB or larger),
so that a record made on one plays back on every other: played back, a run
uploads the pages that hold it, whole (journal_read). */

typedef char journal_words_are_dwords[sizeof(DWORD) == sizeof(uint32_t) ? 1 : -1];
/* (the window's 4 KB pages, the contiguous allocator's on every platform:
xbox_memory.c) */
#define JOURNAL_PAGE_UNIT 0x1000
/* (the mirror's page is a power of two times its unit: mirror_page_size_set) */
typedef char journal_unit_divides_pages[MIRROR_PAGE_UNIT % JOURNAL_PAGE_UNIT == 0 ? 1 : -1];

enum
{
	_journal_object_none,
	/* (value: the tag index; check: the bitmap's index) */
	_journal_object_bitmap,
	/* (value: its number, d3d8_resource_created) */
	_journal_object_created,
	/* (value: 0 the back buffer, 1 its depth buffer) */
	_journal_object_device,
	/* (value: its address, in the contiguous memory) */
	_journal_object_memory,
	/* (value: the device's target, as _device's, whose memory it is: a
	texture or surface the game made over it, in memory of its own; its
	words, format and size) */
	_journal_object_view,
};

struct journal_object
{
	uint32_t kind, value, check;
	/* a texture's or surface's format and size words (a stand-in's, where
	it is not found: journal_stand_in) */
	uint32_t format, size;
};

struct journal_state
{
	uint32_t primitive, start, count, base_vertex, minimum, immediate_written;
	uint32_t arguments[12];
	struct journal_object textures[D3DTSS_MAXSTAGES], palettes[D3DTSS_MAXSTAGES], render_target, depth_stencil;
	uint32_t viewport[6];
	uint32_t vertex_shader, program, program_address, shader_constant_mode;
	uint32_t streams[16][2];
	uint32_t hdr_category, hdr_kind, hdr_view;
	uint32_t lights_environment, lights_count, halos_count, sky, visibility, world_span;
	uint32_t render_state[D3DRS_MAX];
	uint32_t texture_state[D3DTSS_MAXSTAGES][D3DTSS_MAX];
};

#define JOURNAL_STATE_WORDS (sizeof(struct journal_state) / sizeof(uint32_t))
typedef char journal_state_is_words[sizeof(struct journal_state) % sizeof(uint32_t) == 0 ? 1 : -1];
/* the entries told apart (a hash each), at most: past them, an entry is
kept even when it repeats one */
#define JOURNAL_SEEN_SLOTS 16384

static struct
{
	uint32_t *words;
	unsigned long count, capacity;
	BOOL failed;
	/* the state of the entry before (recorded, or played) */
	struct journal_state last;
	uint64_t seen[JOURNAL_SEEN_SLOTS];
	unsigned long seen_count;
	/* objects that are none of the kinds above (recorded), or not found
	(played) */
	unsigned long unknown;
	const struct halo_gpu_journal_bitmaps *bitmaps;
	unsigned char pages[MIRROR_PAGE_SLOTS / 8];
} journal;

/* the textures, surfaces and palettes made, in order, and the vertex shaders
by id */
static void **resources;
static unsigned long resource_count, resource_capacity;
static struct vertex_shader_object **vertex_shaders;
static unsigned long vertex_shader_capacity;

void d3d8_resource_created(void *resource)
{
	if (resource_count == resource_capacity)
	{
		unsigned long capacity = resource_capacity ? resource_capacity * 2 : 256;
		void **larger = realloc(resources, capacity * sizeof(*larger));

		/* (out of memory: its number is unknown, and a journal that draws with
		it is not kept) */
		if (!larger)
			return;
		resources = larger;
		resource_capacity = capacity;
	}
	resources[resource_count++] = resource;
}

void d3d8_resource_freed(void *resource)
{
	unsigned long index;

	for (index = 0; index < resource_count; index++)
	{
		if (resources[index] == resource)
			resources[index] = NULL;
	}
}

/* a vertex shader's identity: its program's hash, its instructions' count,
its declaration's elements, packed ones and count, and the lighting the
game gives it (halo_vertex_shader_lighting); never 0. Two shaders of one
identity draw alike. */
static void journal_vertex_shader_identify(struct vertex_shader_object *object)
{
	uint64_t hash = XGPU_MANIFEST_HASH_START;
	uint32_t words[4];

	words[0] = (uint32_t)object->instruction_count;
	words[1] = (uint32_t)object->element_count;
	words[2] = (uint32_t)object->packed_mask;
	words[3] = (uint32_t)object->lighting.lights;
	if (object->program)
		hash = xgpu_manifest_hash(&object->program->hash, sizeof(object->program->hash), hash);
	hash = xgpu_manifest_hash(words, sizeof(words), hash);
	hash = xgpu_manifest_hash(object->elements, sizeof(object->elements), hash);
	object->journal_identity = (uint32_t)(hash ^ hash >> 32) | 1;
}

static void journal_vertex_shader(struct vertex_shader_object *object)
{
	journal_vertex_shader_identify(object);
	if (object->id >= vertex_shader_capacity)
	{
		unsigned long capacity = object->id + 256;
		struct vertex_shader_object **larger = realloc(vertex_shaders, capacity * sizeof(*larger));

		if (!larger)
			return;
		memset(larger + vertex_shader_capacity, 0, (capacity - vertex_shader_capacity) * sizeof(*larger));
		vertex_shaders = larger;
		vertex_shader_capacity = capacity;
	}
	vertex_shaders[object->id] = object;
}

/* the vertex shader of an identity (journal_vertex_shader), or NULL */
static struct vertex_shader_object *journal_vertex_shader_get(uint32_t identity)
{
	static struct vertex_shader_object *last;
	unsigned long index;

	if (!identity)
		return NULL;
	if (last && last->journal_identity == identity)
		return last;
	for (index = 0; index < vertex_shader_capacity; index++)
	{
		if (vertex_shaders[index] && vertex_shaders[index]->journal_identity == identity)
			return last = vertex_shaders[index];
	}
	return NULL;
}

/* what an object is checked by: its type, and a texture's or surface's
format (not its size, which follows the screen's) */
static uint32_t journal_object_check(const void *object)
{
	const DWORD *fields = object;
	uint32_t type = fields[0] & D3DCOMMON_TYPE_MASK;

	if (type == D3DCOMMON_TYPE_TEXTURE || type == D3DCOMMON_TYPE_SURFACE)
		type |= fields[3] & (D3DFORMAT_FORMAT_MASK | D3DFORMAT_DIMENSION_MASK | D3DFORMAT_CUBEMAP);
	return type;
}

/* the state a replay changes, given back after it */
struct journal_saved
{
	DWORD render_states[D3DRS_MAX], texture_states[D3DTSS_MAXSTAGES][D3DTSS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	D3DSurface *render_target, *depth_stencil;
	D3DVIEWPORT8 viewport;
	struct vertex_shader_object *vertex_shader, *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	D3DSHADERCONSTANTMODE shader_constant_mode;
	DWORD stream_data[16];
	UINT stream_stride[16], base_vertex_index;
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	int hdr_category, hdr_kind;
	BOOL hdr_view;
	BOOL lights_environment, visibility;
	int lights_count, halos_count, world_span;
	unsigned long sky;
};

static struct journal_saved saved;

/* a texture's or surface's format and size words */
static void journal_object_shape(const void *object, struct journal_object *to)
{
	const DWORD *fields = object;
	uint32_t type = fields[0] & D3DCOMMON_TYPE_MASK;

	if (type == D3DCOMMON_TYPE_TEXTURE || type == D3DCOMMON_TYPE_SURFACE)
	{
		to->format = fields[3];
		to->size = fields[4];
	}
}

/* a texture or surface of an object's type, format and size, where the
object itself is not found (one the game made as the walk drew, which a
played back preparation, drawing no walk, has not made yet): what a draw's
pipeline knows of it. Made once, and kept for the next. */
#define JOURNAL_STAND_INS 32
static struct
{
	uint32_t check, format, size;
	void *object;
	/* the texture it is (a surface: its level 0's), made for the first of
	its format and size: a texture and a surface alike are one memory, as a
	target drawn into and then sampled is */
	D3DTexture *texture;
	unsigned long width, height;
	D3DFORMAT d3d_format;
	BOOL cube_map;
} journal_stand_ins[JOURNAL_STAND_INS];

static void *journal_stand_in(const struct journal_object *object)
{
	struct xgpu_texture_description description;
	uint32_t type = object->check & D3DCOMMON_TYPE_MASK;
	D3DFORMAT format = (D3DFORMAT)((object->format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT);
	D3DTexture *texture = NULL;
	void *made = NULL;
	int index, other;

	if ((type != D3DCOMMON_TYPE_TEXTURE && type != D3DCOMMON_TYPE_SURFACE) || !object->format)
		return NULL;
	for (index = 0; index < JOURNAL_STAND_INS && journal_stand_ins[index].object; index++)
	{
		if (journal_stand_ins[index].check == object->check && journal_stand_ins[index].format == object->format &&
			journal_stand_ins[index].size == object->size)
			return journal_stand_ins[index].object;
	}
	if (index == JOURNAL_STAND_INS)
		return NULL;
	xgpu_texture_describe(object->format, object->size, &description);
	if (!description.width || !description.height || description.depth > 1 ||
		(description.cube_map && type == D3DCOMMON_TYPE_SURFACE))
	{
		platform_log("GPU: journal: no stand-in for format %08lx size %08lx (%lux%lux%lu)",
			(unsigned long)object->format, (unsigned long)object->size, description.width, description.height,
			description.depth);
		return NULL;
	}
	for (other = 0; other < index && !texture; other++)
	{
		if (journal_stand_ins[other].width == description.width && journal_stand_ins[other].height ==
			description.height && journal_stand_ins[other].d3d_format == format &&
			journal_stand_ins[other].cube_map == description.cube_map)
			texture = journal_stand_ins[other].texture;
	}
	if (!texture && description.cube_map &&
		D3DDevice_CreateCubeTexture(description.width, description.levels, 0, format, D3DPOOL_DEFAULT,
			(D3DCubeTexture **)&texture) != S_OK)
		return NULL;
	if (!texture && D3DDevice_CreateTexture(description.width, description.height, description.levels, 0, format,
		D3DPOOL_DEFAULT, &texture) != S_OK)
		return NULL;
	if (type == D3DCOMMON_TYPE_SURFACE)
	{
		if (D3DTexture_GetSurfaceLevel(texture, 0, (D3DSurface **)&made) != S_OK)
			return NULL;
		((DWORD *)made)[3] = object->format;
		((DWORD *)made)[4] = object->size;
	}
	else
	{
		made = texture;
		((DWORD *)made)[3] = object->format;
		((DWORD *)made)[4] = object->size;
	}
	journal_stand_ins[index].check = object->check;
	journal_stand_ins[index].format = object->format;
	journal_stand_ins[index].size = object->size;
	journal_stand_ins[index].object = made;
	journal_stand_ins[index].texture = texture;
	journal_stand_ins[index].width = description.width;
	journal_stand_ins[index].height = description.height;
	journal_stand_ins[index].d3d_format = format;
	journal_stand_ins[index].cube_map = description.cube_map;
	return made;
}

/* a texture or surface over one of the device's targets, as the game made
one (its words), its memory the target's now */
static struct
{
	struct journal_object object;
	D3DSurface header;
} journal_views[JOURNAL_STAND_INS];

static void *journal_view(const struct journal_object *object)
{
	const D3DSurface *target = object->value == 0 ? &device.back_buffer : object->value == 1 ? &device.depth_buffer :
		NULL;
	int index;

	if (!target || !target->Data)
		return NULL;
	for (index = 0; index < JOURNAL_STAND_INS && journal_views[index].object.kind; index++)
	{
		if (!memcmp(&journal_views[index].object, object, sizeof(*object)))
			break;
	}
	if (index == JOURNAL_STAND_INS)
		return NULL;
	journal_views[index].object = *object;
	memset(&journal_views[index].header, 0, sizeof(journal_views[index].header));
	journal_views[index].header.Common = (object->check & D3DCOMMON_TYPE_MASK) | 1;
	journal_views[index].header.Data = target->Data;
	journal_views[index].header.Format = object->format;
	journal_views[index].header.Size = object->size;
	return &journal_views[index].header;
}

static void journal_identify(const void *object, struct journal_object *to)
{
	unsigned long tag_index, bitmap_index, index;

	memset(to, 0, sizeof(*to));
	if (!object)
		return;
	if (object == &device.back_buffer || object == &device.depth_buffer)
	{
		to->kind = _journal_object_device;
		to->value = object == &device.back_buffer ? 0 : 1;
		return;
	}
	if (journal.bitmaps && journal.bitmaps->identify(object, &tag_index, &bitmap_index))
	{
		to->kind = _journal_object_bitmap;
		to->value = (uint32_t)tag_index;
		to->check = (uint32_t)bitmap_index;
		return;
	}
	for (index = 0; index < resource_count; index++)
	{
		if (resources[index] == object)
		{
			to->kind = _journal_object_created;
			to->value = (uint32_t)index;
			to->check = journal_object_check(object);
			journal_object_shape(object, to);
			return;
		}
	}
	if (platform_is_contiguous(object))
	{
		to->kind = _journal_object_memory;
		to->value = (uint32_t)(unsigned long)object;
		to->check = journal_object_check(object);
		journal_object_shape(object, to);
		return;
	}
	{
		const DWORD *fields = object;
		uint32_t type = fields[0] & D3DCOMMON_TYPE_MASK;

		if ((type == D3DCOMMON_TYPE_TEXTURE || type == D3DCOMMON_TYPE_SURFACE) && fields[1] &&
			(fields[1] == device.back_buffer.Data || fields[1] == device.depth_buffer.Data))
		{
			to->kind = _journal_object_view;
			to->value = fields[1] == device.back_buffer.Data ? 0 : 1;
			to->check = journal_object_check(object);
			journal_object_shape(object, to);
			return;
		}
	}
	if (journal.unknown < 8)
		platform_log("GPU: journal: unknown object %p (common %08lx format %08lx; device %p)", object,
			(unsigned long)((const DWORD *)object)[0], (unsigned long)((const DWORD *)object)[3], (void *)&device);
	journal.unknown++;
}

static void *journal_resolve(const struct journal_object *object)
{
	unsigned long index;
	void *found = NULL;

	switch (object->kind)
	{
	case _journal_object_none:
		return NULL;
	case _journal_object_device:
		found = object->value == 0 ? (void *)&device.back_buffer : object->value == 1 ? (void *)&device.depth_buffer :
			NULL;
		break;
	case _journal_object_bitmap:
		found = journal.bitmaps ? journal.bitmaps->load(object->value, object->check) : NULL;
		break;
	case _journal_object_created:
		/* (its number's, or failing that the first of its type and format:
		what a draw's pipeline knows of it) */
		/* (its number's, or failing that one like it) */
		if (object->value < resource_count && resources[object->value] &&
			journal_object_check(resources[object->value]) == object->check)
		{
			found = resources[object->value];
			break;
		}
		found = journal_stand_in(object);
		break;
	case _journal_object_view:
		found = journal_view(object);
		break;
	case _journal_object_memory:
		if (platform_is_contiguous((const void *)(unsigned long)object->value) &&
			journal_object_check((const void *)(unsigned long)object->value) == object->check)
			found = (void *)(unsigned long)object->value;
		if (!found)
			found = journal_stand_in(object);
		break;
	}
	if (!found)
	{
		if (journal.unknown < 12)
			platform_log("GPU: journal: object not found: kind %lu value %08lx check %08lx",
				(unsigned long)object->kind, (unsigned long)object->value, (unsigned long)object->check);
		journal.unknown++;
	}
	return found;
}

/* a stage that samples its texture (bind_textures) */
static BOOL journal_stage_samples(int stage)
{
	unsigned long mode = stage_texture_mode(stage);
	D3DBaseTexture *texture = device.textures[stage];

	return texture && texture->Data && mode != 0 && mode != 0x04 && mode != 0x05 && mode != 0x0a && mode != 0x11;
}

static void journal_capture(struct journal_state *state)
{
	struct vertex_shader_object *program = current_program();
	int stage, index;

	memset(state, 0, sizeof(*state));
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		if (!journal_stage_samples(stage))
			continue;
		journal_identify(device.textures[stage], &state->textures[stage]);
		journal_identify(device.palettes[stage], &state->palettes[stage]);
	}
	journal_identify(device.render_target, &state->render_target);
	journal_identify(device.depth_stencil, &state->depth_stencil);
	memcpy(state->viewport, &device.viewport, sizeof(device.viewport) < sizeof(state->viewport) ?
		sizeof(device.viewport) : sizeof(state->viewport));
	state->vertex_shader = device.vertex_shader ? device.vertex_shader->journal_identity : 0;
	state->program = program ? program->journal_identity : 0;
	state->program_address = (uint32_t)device.program_address;
	state->shader_constant_mode = (uint32_t)device.shader_constant_mode;
	for (index = 0; index < 16; index++)
	{
		state->streams[index][0] = (uint32_t)device.streams[index].data;
		state->streams[index][1] = (uint32_t)device.streams[index].stride;
	}
	state->hdr_category = (uint32_t)hdr.category;
	state->hdr_kind = (uint32_t)hdr.kind;
	state->hdr_view = (uint32_t)hdr.view;
	state->lights_environment = (uint32_t)dynamic_lights.environment;
	state->lights_count = (uint32_t)dynamic_lights.count;
	state->halos_count = (uint32_t)dynamic_lights.halos.count;
	state->sky = (uint32_t)preparation.sky;
	state->visibility = (uint32_t)device.visibility_test_active;
	/* (display.ambient_occlusion "world"'s window, whatever the setting: a
	draw's context in a recording of the manifest, prepare_draw) */
	state->world_span = (uint32_t)world_ao.span;
	memcpy(state->render_state, D3D__RenderState, sizeof(state->render_state));
	memcpy(state->texture_state, D3D__TextureState, sizeof(state->texture_state));
}

static BOOL journal_reserve(unsigned long words)
{
	if (journal.failed)
		return FALSE;
	if (journal.count + words > journal.capacity)
	{
		unsigned long capacity = journal.capacity ? journal.capacity * 2 : 65536;
		uint32_t *larger;

		while (capacity < journal.count + words)
			capacity *= 2;
		larger = realloc(journal.words, capacity * sizeof(*larger));
		if (!larger)
		{
			journal.failed = TRUE;
			return FALSE;
		}
		journal.words = larger;
		journal.capacity = capacity;
	}
	return TRUE;
}

/* an entry, unless one the same was kept before */
static void journal_append(unsigned long op, const struct journal_state *state)
{
	const uint32_t *words = (const uint32_t *)state, *last = (const uint32_t *)&journal.last;
	uint64_t hash = xgpu_manifest_hash(&op, sizeof(op), XGPU_MANIFEST_HASH_START);
	unsigned long index, changed = 0, slot;

	hash = xgpu_manifest_hash(state, sizeof(*state), hash);
	hash |= 1;
	if (journal.seen_count < JOURNAL_SEEN_SLOTS * 3 / 4)
	{
		for (slot = (unsigned long)(hash >> 20) % JOURNAL_SEEN_SLOTS; journal.seen[slot];
			slot = (slot + 1) % JOURNAL_SEEN_SLOTS)
		{
			if (journal.seen[slot] == hash)
				return;
		}
		journal.seen[slot] = hash;
		journal.seen_count++;
	}
	for (index = 0; index < JOURNAL_STATE_WORDS; index++)
		changed += words[index] != last[index];
	if (!journal_reserve(1 + changed * 2))
		return;
	journal.words[journal.count++] = (uint32_t)op | (uint32_t)changed << 8;
	for (index = 0; index < JOURNAL_STATE_WORDS; index++)
	{
		if (words[index] == last[index])
			continue;
		journal.words[journal.count++] = (uint32_t)index;
		journal.words[journal.count++] = words[index];
	}
	journal.last = *state;
}

static void journal_draw(unsigned long op, unsigned long primitive, unsigned long start, unsigned long count,
	unsigned long base_vertex, unsigned long minimum)
{
	static struct journal_state state;

	journal_capture(&state);
	state.primitive = (uint32_t)primitive;
	state.start = (uint32_t)start;
	state.count = (uint32_t)count;
	state.base_vertex = (uint32_t)base_vertex;
	state.minimum = (uint32_t)minimum;
	state.immediate_written = op == JOURNAL_DRAW_IMMEDIATE ? (uint32_t)device.immediate_written : 0;
	journal_append(op, &state);
}

static void journal_operation(unsigned long op, const void *arguments, unsigned long bytes)
{
	static struct journal_state state;

	journal_capture(&state);
	if (arguments)
		memcpy(state.arguments, arguments, bytes < sizeof(state.arguments) ? bytes : sizeof(state.arguments));
	journal_append(op, &state);
}

/* the front end's state that a draw's pipeline is derived from under some
settings and not under others (the HDR's tags, with display.hdr; the
enhanced lights', with display.dynamic_lights "enhanced"; the window's
span, with display.ambient_occlusion "world": world_ao_key), as three words: recording a map's preparation, a draw is
journaled where it is new in these or in its pipeline (preparation_seen),
so that its record holds every draw a load under other settings tells
apart; 0 otherwise */
static void journal_settings_blind_of(unsigned long words[3], int hdr_category, int hdr_kind, BOOL hdr_view,
	int lights, BOOL environment, int world_span)
{
	words[0] = (unsigned long)hdr_category;
	words[1] = (unsigned long)hdr_kind;
	words[2] = (unsigned long)(hdr_view != 0) | (unsigned long)(lights > 0) << 1 |
		(unsigned long)(environment != 0) << 2 | (unsigned long)world_span << 3;
}

static void journal_settings_blind(unsigned long words[3])
{
	words[0] = words[1] = words[2] = 0;
	if (journal_recording)
		journal_settings_blind_of(words, hdr.category, hdr.kind, hdr.view, dynamic_lights.count,
			dynamic_lights.environment, world_ao.span);
}

static void journal_page(unsigned long page)
{
	if (page < MIRROR_PAGE_COUNT)
		journal.pages[page >> 3] |= (unsigned char)(1u << (page & 7));
}

void halo_gpu_journal_record(const struct halo_gpu_journal_bitmaps *bitmaps)
{
	free(journal.words);
	memset(&journal, 0, sizeof(journal));
	journal.bitmaps = bitmaps;
	journal_recording = TRUE;
}

void halo_gpu_journal_texture(unsigned int tag_index, unsigned int bitmap_index)
{
	if (!journal_recording || !journal_reserve(3))
		return;
	journal.words[journal.count++] = JOURNAL_TEXTURE;
	journal.words[journal.count++] = tag_index;
	journal.words[journal.count++] = bitmap_index;
}

/* the runs of the pages set in pages (page_count of page_size bytes each),
appended in JOURNAL_PAGE_UNIT's units: JOURNAL_PAGES and their count, then
each's first unit and the unit past it */
static void journal_runs(const unsigned char *pages, unsigned long page_count, unsigned long page_size)
{
	unsigned long page, runs = 0, header;

	if (!journal_reserve(1))
		return;
	header = journal.count++;
	for (page = 0; page < page_count; page++)
	{
		unsigned long end;

		if (!(pages[page >> 3] & (1u << (page & 7))))
			continue;
		for (end = page; end < page_count && (pages[end >> 3] & (1u << (end & 7))); end++)
			;
		if (!journal_reserve(2))
			return;
		journal.words[journal.count++] = (uint32_t)(page * (page_size / JOURNAL_PAGE_UNIT));
		journal.words[journal.count++] = (uint32_t)(end * (page_size / JOURNAL_PAGE_UNIT));
		runs++;
		page = end;
	}
	journal.words[header] = JOURNAL_PAGES | (uint32_t)runs << 8;
}

unsigned int *halo_gpu_journal_take(unsigned int *count, unsigned int *unknown)
{
	uint32_t *words;

	*count = 0;
	*unknown = 0;
	if (!journal_recording)
		return NULL;
	journal_recording = FALSE;
	/* (the pages' runs, last) */
	journal_runs(journal.pages, MIRROR_PAGE_COUNT, MIRROR_PAGE_SIZE);
	*unknown = (unsigned int)journal.unknown;
	if (journal.failed)
	{
		free(journal.words);
		journal.words = NULL;
		return NULL;
	}
	words = journal.words;
	*count = (unsigned int)journal.count;
	journal.words = NULL;
	journal.count = journal.capacity = 0;
	return (unsigned int *)words;
}

/* the journal's form: a change to what an entry holds or how it is written
makes it 2, 3 and so on; its state's size is in it as well (5: the pages'
runs in JOURNAL_PAGE_UNIT's units, not the platform's pages; 6: the world's
window's span, world_span; 7: without the screen's ambient occlusion, its
pre-pass, its flags and its operations; 8: world_span's
WORLD_AO_SPAN_FOUND, the window found once a pixel) */
#define JOURNAL_FORM 8

unsigned int halo_gpu_journal_form(void)
{
	return JOURNAL_FORM << 16 | (unsigned int)JOURNAL_STATE_WORDS;
}

/* the backend's and the GPU's part of what a record's draws are derived
under (render_prepared.c: its enumeration checked under its own alone) */
unsigned long long halo_gpu_journal_derivation(unsigned long long hash)
{
	return xgpu_manifest_hash(&xgpu_backend_capabilities, sizeof(xgpu_backend_capabilities), hash);
}

void halo_gpu_journal_free(unsigned int *words)
{
	free(words);
}

/* the vertices a replayed draw is made from: the fewest its primitive has */
static unsigned long journal_vertices(unsigned long primitive, unsigned long count)
{
	unsigned long least = primitive == D3DPT_POINTLIST ? 1 : primitive == D3DPT_LINELIST ||
		primitive == D3DPT_LINESTRIP || primitive == D3DPT_LINELOOP ? 2 :
		primitive == D3DPT_QUADLIST || primitive == D3DPT_QUADSTRIP ? 4 : 3;

	return count < least ? count : least;
}

static void journal_restore(const struct journal_state *state)
{
	struct vertex_shader_object *program = journal_vertex_shader_get(state->program);
	int stage, index;

	memcpy(D3D__RenderState, state->render_state, sizeof(state->render_state));
	memcpy(D3D__TextureState, state->texture_state, sizeof(state->texture_state));
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		device.textures[stage] = journal_resolve(&state->textures[stage]);
		device.palettes[stage] = journal_resolve(&state->palettes[stage]);
	}
	device.render_target = journal_resolve(&state->render_target);
	device.depth_stencil = journal_resolve(&state->depth_stencil);
	memcpy(&device.viewport, state->viewport, sizeof(device.viewport) < sizeof(state->viewport) ?
		sizeof(device.viewport) : sizeof(state->viewport));
	viewport_update_constants();
	device.vertex_shader = journal_vertex_shader_get(state->vertex_shader);
	if ((state->vertex_shader && !device.vertex_shader) || (state->program && !program))
		journal.unknown++;
	device.program_address = state->program_address < VERTEX_PROGRAM_SLOTS ? state->program_address : 0;
	device.program_slots[device.program_address] = program;
	device.shader_constant_mode = (D3DSHADERCONSTANTMODE)state->shader_constant_mode;
	for (index = 0; index < 16; index++)
	{
		device.streams[index].data = state->streams[index][0];
		device.streams[index].stride = state->streams[index][1];
	}
	hdr.category = (int)state->hdr_category;
	hdr.kind = (int)state->hdr_kind;
	hdr.view = state->hdr_view != 0;
	dynamic_lights.environment = state->lights_environment != 0;
	dynamic_lights.count = (int)state->lights_count;
	dynamic_lights.halos.count = (int)state->halos_count;
	preparation.sky = state->sky;
	device.visibility_test_active = state->visibility != 0;
	world_ao.span = (int)state->world_span;
}

static void journal_play(unsigned long op, const struct journal_state *state)
{
	static WORD indices[4];
	const short *bounds = (const short *)state->arguments;
	float z;

	journal_restore(state);
	switch (op)
	{
	case JOURNAL_DRAW:
		D3DDevice_DrawVertices((D3DPRIMITIVETYPE)state->primitive, state->start,
			journal_vertices(state->primitive, state->count));
		break;
	case JOURNAL_DRAW_INDEXED:
		device.base_vertex_index = state->base_vertex + state->minimum;
		D3DDevice_DrawIndexedVertices((D3DPRIMITIVETYPE)state->primitive,
			journal_vertices(state->primitive, state->count), indices);
		break;
	case JOURNAL_DRAW_IMMEDIATE:
	{
		unsigned long count = journal_vertices(state->primitive, state->count);
		unsigned long floats = XGPU_VERTEX_ATTRIBUTE_COUNT * 4;

		if (count > device.immediate_capacity)
		{
			float *vertices = realloc(device.immediate_vertices, 256 * floats * sizeof(float));

			if (!vertices)
				break;
			device.immediate_vertices = vertices;
			device.immediate_capacity = 256;
		}
		memset(device.immediate_vertices, 0, count * floats * sizeof(float));
		device.immediate_active = TRUE;
		device.immediate_type = (D3DPRIMITIVETYPE)state->primitive;
		device.immediate_count = count;
		device.immediate_written = state->immediate_written;
		D3DDevice_End();
		break;
	}
	case JOURNAL_CLEAR:
		memcpy(&z, &state->arguments[2], sizeof(z));
		D3DDevice_Clear(0, NULL, state->arguments[0], state->arguments[1], z, state->arguments[3]);
		break;
	case JOURNAL_ANTI_ALIAS:
		halo_screen_anti_alias(bounds[0], bounds[1], bounds[2], bounds[3]);
		break;
	case JOURNAL_PANINI:
	{
		/* (where the setting is on now: the projection it gives, if it was
		off as the record was made) */
		float projection[8];

		memcpy(projection, &state->arguments[2], sizeof(projection));
		if (journal.bitmaps && journal.bitmaps->panini && journal.bitmaps->panini(projection))
			halo_screen_panini(bounds[0], bounds[1], bounds[2], bounds[3], projection);
		break;
	}
	case JOURNAL_TONE_MAP:
		halo_screen_tone_map(bounds[0], bounds[1], bounds[2], bounds[3]);
		break;
	case JOURNAL_HALOS:
		halo_dynamic_lights_halos_draw();
		break;
	}
}

/* the journal's words read, a call for each entry (its type and its state,
made from the entry's before in state), texture and run of pages; 0 if
they are malformed (the calls up to there made). A run's units, of a window
of window_size bytes, are given as the pages of page_size bytes that hold
them, whole, less those the run before gave (two runs may share a page
larger than a unit) */
struct journal_reader
{
	void (*entry)(unsigned long op, const struct journal_state *state);
	void (*texture)(uint32_t tag_index, uint32_t bitmap_index);
	void (*pages)(unsigned long first, unsigned long end);
};

static int journal_read(const uint32_t *words, unsigned long count, struct journal_state *state,
	unsigned long window_size, unsigned long page_size, const struct journal_reader *reader)
{
	uint32_t *fields = (uint32_t *)state;
	unsigned long position = 0, index, given = 0;

	while (position < count)
	{
		uint32_t header = words[position++];
		uint32_t type = header & 0xff, size = header >> 8;

		if (type == JOURNAL_TEXTURE)
		{
			if (count - position < 2)
				return 0;
			reader->texture(words[position], words[position + 1]);
			position += 2;
		}
		else if (type == JOURNAL_PAGES)
		{
			if ((count - position) / 2 < size)
				return 0;
			for (index = 0; index < size; index++, position += 2)
			{
				unsigned long first, end;

				if (words[position] >= words[position + 1] || words[position + 1] > window_size / JOURNAL_PAGE_UNIT)
					return 0;
				first = words[position] * JOURNAL_PAGE_UNIT / page_size;
				end = (words[position + 1] * JOURNAL_PAGE_UNIT + page_size - 1) / page_size;
				if (first < given)
					first = given;
				if (first < end)
					reader->pages(first, end);
				if (end > given)
					given = end;
			}
		}
		else if (type >= JOURNAL_DRAW && type < JOURNAL_STATES)
		{
			if ((count - position) / 2 < size)
				return 0;
			for (index = 0; index < size; index++, position += 2)
			{
				if (words[position] >= JOURNAL_STATE_WORDS)
					return 0;
				fields[words[position]] = words[position + 1];
			}
			reader->entry(type, state);
		}
		else
		{
			return 0;
		}
	}
	return 1;
}

static void journal_replay_texture(uint32_t tag_index, uint32_t bitmap_index)
{
	if (journal.bitmaps && !journal.bitmaps->load(tag_index, bitmap_index))
		journal.unknown++;
}

/* a run a segment at a time, as mirror_range uploads them */
static void journal_replay_pages(unsigned long first, unsigned long end)
{
	while (first < end)
	{
		unsigned long segment_end = (first * MIRROR_PAGE_SIZE / MIRROR_SEGMENT_SIZE + 1) *
			(MIRROR_SEGMENT_SIZE / MIRROR_PAGE_SIZE);
		unsigned long last = end < segment_end ? end : segment_end;

		(void)mirror_refresh(first, last);
		first = last;
	}
}

int halo_gpu_journal_replay(const unsigned int *words, unsigned int count, const struct halo_gpu_journal_bitmaps *bitmaps,
	unsigned int *unknown)
{
	static const struct journal_reader reader = { journal_play, journal_replay_texture, journal_replay_pages };
	unsigned long index;
	int well_formed;

	*unknown = 0;
	if (!device.gl_ready || device.immediate_active)
		return 0;
	memcpy(saved.render_states, D3D__RenderState, sizeof(saved.render_states));
	memcpy(saved.texture_states, D3D__TextureState, sizeof(saved.texture_states));
	memcpy(saved.textures, device.textures, sizeof(saved.textures));
	memcpy(saved.palettes, device.palettes, sizeof(saved.palettes));
	saved.render_target = device.render_target;
	saved.depth_stencil = device.depth_stencil;
	saved.viewport = device.viewport;
	saved.vertex_shader = device.vertex_shader;
	memcpy(saved.program_slots, device.program_slots, sizeof(saved.program_slots));
	saved.program_address = device.program_address;
	saved.shader_constant_mode = device.shader_constant_mode;
	for (index = 0; index < 16; index++)
	{
		saved.stream_data[index] = device.streams[index].data;
		saved.stream_stride[index] = device.streams[index].stride;
	}
	saved.base_vertex_index = device.base_vertex_index;
	memcpy(saved.attributes, device.attributes, sizeof(saved.attributes));
	saved.hdr_category = hdr.category;
	saved.hdr_kind = hdr.kind;
	saved.hdr_view = hdr.view;
	saved.lights_environment = dynamic_lights.environment;
	saved.lights_count = dynamic_lights.count;
	saved.halos_count = dynamic_lights.halos.count;
	saved.sky = preparation.sky;
	saved.visibility = device.visibility_test_active;
	saved.world_span = world_ao.span;

	journal_recording = FALSE;
	journal.bitmaps = bitmaps;
	journal.unknown = 0;
	memset(&journal.last, 0, sizeof(journal.last));
	well_formed = journal_read((const uint32_t *)words, count, &journal.last, PLATFORM_CONTIGUOUS_SIZE,
		MIRROR_PAGE_SIZE, &reader);

	memcpy(D3D__RenderState, saved.render_states, sizeof(saved.render_states));
	memcpy(D3D__TextureState, saved.texture_states, sizeof(saved.texture_states));
	memcpy(device.textures, saved.textures, sizeof(saved.textures));
	memcpy(device.palettes, saved.palettes, sizeof(saved.palettes));
	device.render_target = saved.render_target;
	device.depth_stencil = saved.depth_stencil;
	device.viewport = saved.viewport;
	viewport_update_constants();
	device.vertex_shader = saved.vertex_shader;
	memcpy(device.program_slots, saved.program_slots, sizeof(saved.program_slots));
	device.program_address = saved.program_address;
	device.shader_constant_mode = saved.shader_constant_mode;
	for (index = 0; index < 16; index++)
	{
		device.streams[index].data = saved.stream_data[index];
		device.streams[index].stride = saved.stream_stride[index];
	}
	device.base_vertex_index = saved.base_vertex_index;
	memcpy(device.attributes, saved.attributes, sizeof(saved.attributes));
	device.immediate_active = FALSE;
	device.immediate_count = 0;
	device.immediate_written = 0;
	hdr.category = saved.hdr_category;
	hdr.kind = saved.hdr_kind;
	hdr.view = saved.hdr_view;
	dynamic_lights.environment = saved.lights_environment;
	dynamic_lights.count = saved.lights_count;
	dynamic_lights.halos.count = saved.halos_count;
	preparation.sky = saved.sky;
	device.visibility_test_active = saved.visibility;
	world_ao.span = saved.world_span;
	journal.bitmaps = NULL;
	xgpu_backend_invalidate();
	*unknown = (unsigned int)journal.unknown;
	return well_formed;
}
