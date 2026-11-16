/*
RASTERIZER_XBOX_SHADOWS.C

symbols in this file:
00161950 01b0:
	_D3DDevice_SetRenderState (0000)
00161B00 0050:
	_D3DDevice_SetTextureStageState (0000)
00161B50 0220:
	_IDirect3DDevice8_SetRenderState@12 (0000)
00161D70 0010:
	__rasterizer_environment_shadows_begin (0000)
00161D80 0060:
	_IDirect3DDevice8_SetTextureStageState@16 (0000)
00161DE0 0010:
	_IDirect3DDevice8_SetVertexShaderConstant@16 (0000)
00161DF0 00a0:
	__rasterizer_environment_shadow_model_begin (0000)
00161E90 0010:
	__rasterizer_environment_shadow_model_end (0000)
00161EA0 0020:
	_IDirect3DDevice8_SetVertexData2f@16 (0000)
00161EC0 0010:
	_IDirect3DDevice8_SetVertexData2s@16 (0000)
00161ED0 0010:
	_IDirect3DDevice8_Begin@8 (0000)
00161EE0 0010:
	_IDirect3DDevice8_End@4 (0000)
00161EF0 0080:
	__rasterizer_environment_shadow_end (0000)
00161F70 0010:
	__rasterizer_environment_shadows_end (0000)
00161F80 0300:
	_rasterizer_shadow_convolve (0000)
00162280 03b0:
	__rasterizer_environment_shadow_begin (0000)
00162630 02b0:
	__rasterizer_environment_shadow_model_draw (0000)
001628E0 05c0:
	__rasterizer_environment_shadow_draw (0000)
002929E0 0039:
	??_C@_0DJ@KIPLBCDL@c?3?2halo?2SOURCE?2rasterizer?2xbox?2r@ (0000)
00292A1C 0027:
	??_C@_0CH@OGPNPLFF@?$CD?$CD?$CD?5WARNING?5empty?5shadow?5has?5bee@ (0000)
00292A44 001c:
	??_C@_0BM@OJNHCFOK@object_bounding_radius?$DO0?40f?$AA@ (0000)
00292A60 0037:
	??_C@_0DH@LMLPNHOO@shadow_color?9?$DOblue?5?$DO?$DN0?40f?5?$CG?$CG?5sha@ (0000)
00292A98 0037:
	??_C@_0DH@OODBCLKP@shadow_color?9?$DOgreen?$DO?$DN0?40f?5?$CG?$CG?5sha@ (0000)
00292AD0 0035:
	??_C@_0DF@OLKILINN@shadow_color?9?$DOred?5?$DO?$DN0?40f?5?$CG?$CG?5shad@ (0000)
00292B08 000d:
	??_C@_0N@NKBPFDCL@shadow_color?$AA@ (0000)
00292B18 000e:
	??_C@_0O@MDCFBAFA@shadow_matrix?$AA@ (0000)
0030CF84 0001:
	_shadow_restored (0000)
0046628C 004a:
	_local_shadow_color (0000)
	_local_object_bounding_radius (000c)
	_local_shadow_matrix (0010)
	_local_parameters (0044)
	_shadow_setup (0048)
	_shadow_used (0049)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "game/game_globals.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "interface/hud_draw.h"
#include "rasterizer/common/rasterizer_common.h"
#include "rasterizer/rasterizer_geometry.h"
#include "render/render.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"

/* January retains the stock XDK D3DINLINE out-of-line wrappers emitted by
 * the real device calls below. Keep the stock inline definitions and do not
 * replace these wrappers with handwritten bodies. */
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_model_types.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"
#include "rasterizer/xbox/rasterizer_xbox_draw_primitives.h"

/* ---------- constants */

enum
{
	_rasterizer_target_render_primary = 0,
};

enum
{
	_rasterizer_statistics_mode_enabled = 2,
};

enum
{
	_shader_type_model = 4,
};

enum
{
	_shader_model_detail_after_reflection_bit = 0,
	_shader_model_two_sided_bit,
	_shader_model_not_alpha_tested_bit,
	_shader_model_alpha_blended_decal_bit,
	_shader_model_true_atmospheric_fog_bit,
	_shader_model_nocull_two_sided_bit,
};

enum
{
	RASTERIZER_STENCIL_MODE_NONE = 0,
	RASTERIZER_STENCIL_MODE_REJECT = 2,
};

enum
{
	_shadow_convolution_vertex_shader_index = 0x26,
	_shadow_vertex_shader_index = 0x1d,
	_shadow_model_vertex_shader_index = 0x27,
};

/* ---------- macros */

/* ---------- structures */

struct shader_model_definition
{
	struct shader shader;
	word flags;
	byte reserved2A[0x72];
	real_vector2d map_scale;
	struct tag_reference base_map;
	byte reservedB4[0x24];
	real detail_map_scale;
	struct tag_reference detail_map;
	real detail_map_v_scale;
	byte reservedF0[0xC];
	struct shader_texture_animation animation;
};

typedef char verify_rasterizer_shadows_model_shadow_count_offset[
	offsetof(
		struct rasterizer_frame_statistics_globals,
		model_shadow_count) == 0xF4 ? 1 : -1];
typedef char verify_rasterizer_shadows_model_parameters_size[
	sizeof(struct rasterizer_model_begin_parameters) == 0xCC ? 1 : -1];
typedef char verify_rasterizer_shadows_model_base_map_scale_offset[
	offsetof(struct rasterizer_model_begin_parameters, base_map_scale) == 0xC4
		? 1 : -1];
typedef char verify_rasterizer_shadows_model_animation_offset[
	offsetof(struct rasterizer_model_begin_parameters, animation) == 0x84
		? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_flags_offset[
	offsetof(struct shader_model_definition, flags) == 0x28 ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_map_scale_offset[
	offsetof(struct shader_model_definition, map_scale) == 0x9C ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_base_map_offset[
	offsetof(struct shader_model_definition, base_map) == 0xA4 ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_detail_scale_offset[
	offsetof(struct shader_model_definition, detail_map_scale) == 0xD8 ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_animation_offset[
	offsetof(struct shader_model_definition, animation) == 0xFC ? 1 : -1];

/* ---------- prototypes */

static void rasterizer_shadow_convolve(
	void);
static void rasterizer_shadow_convolve_pass(
	short source_target,
	short destination_target,
	real offset);
/* port: the shadow atlas (rasterizer_shadow_atlas_begin) */
static short shadow_atlas_capacity(
	short atlas_class);
static long shadow_atlas_x(
	short region);
static long shadow_atlas_y(
	short region);
static void shadow_atlas_caster_target(
	short region);

/* ---------- port: the shadow atlas

Each object's shadow was drawn into the 128x128 map, blurred by the chain
of passes (rasterizer_shadow_convolve) into the other, and drawn onto the
level, before the next object's: six passes an object with the port's
512x512 maps, each with a GPU's fixed cost of a pass. With the atlas, the
frame's casters are drawn first, each into a region of 128 of an atlas
(SHADOW_ATLAS_SPACING apart), and the chain's passes are run once over all
of them, back and forth between two atlases: one quad a region, so that the
atlases' guard bands, black as each pass empties the atlas it draws into
(its quads cover their regions, as they cover a map), stay black, and each
region's taps beyond its edge read black as the map's border does. Each
object's receivers are drawn sampling its region of the atlas the chain
ends in as a map of the region's own (d3d8_texture_region: clamped to the
region's edge texels, its border beyond): the level's surfaces are whole,
their texture coordinates far beyond the map's, which only a map's own
border keeps black (sampled as the atlas, they would read the other
regions). A frame's N casters cost 1 + passes passes, all before the
receivers, instead of (1 + passes) * N between them; the shadows are the
same. A pass stores the whole of its target, and a GPU's time for a pass
grows with its target's area as well as with what it draws, so an atlas
holds about the regions a frame needs: the atlases are a class's pair, of
one row of regions, three or seven (shadow_atlas_heights), the class chosen
for the casters the frame before had. Casters past the class's regions, the
larger maps of 1024 (whose atlases would be 128 MB each) and the debugging
options take the per-object path. */

enum
{
	/* the atlases' width, in the game's units (each display.shadow_resolution's
	scale of pixels: 4096 at 512), the classes' heights in
	shadow_atlas_heights */
	SHADOW_ATLAS_WIDTH = 1024,
	SHADOW_ATLAS_CLASSES = 3,
	SHADOW_ATLAS_MAP_SIZE = 128,
	/* the guard band between two regions, at least 2 * scale - 1 pixels at
	any scale (a pass's taps reach 2 * scale - 1 pixels at most beyond its
	region, rasterizer_shadow_convolve_pass), and before the first, half of
	it (beyond the atlas, the border's black) */
	SHADOW_ATLAS_GUARD = 4,
	SHADOW_ATLAS_SPACING = SHADOW_ATLAS_MAP_SIZE + SHADOW_ATLAS_GUARD,
	SHADOW_ATLAS_MARGIN = SHADOW_ATLAS_GUARD / 2,
	SHADOW_ATLAS_COLUMNS = SHADOW_ATLAS_WIDTH / SHADOW_ATLAS_SPACING,
	SHADOW_ATLAS_REGIONS = RASTERIZER_SHADOW_ATLAS_REGIONS,
	/* the casters more than the frame before's that a class is chosen for */
	SHADOW_ATLAS_SLACK = 2,
	SHADOW_ATLAS_MOST_SCALE = 4,
};

/* each class's height (a power of two, as a texture's), the rows of
regions it holds those it has room for */
static short const shadow_atlas_heights[SHADOW_ATLAS_CLASSES] = { 256, 512, 1024 };

/* each class's atlases: the casters' (0), and the chain's other */
static D3DTexture *shadow_atlas_textures[SHADOW_ATLAS_CLASSES][2];
static D3DSurface *shadow_atlas_surfaces[SHADOW_ATLAS_CLASSES][2];
static boolean shadow_atlas_class_made[SHADOW_ATLAS_CLASSES];
/* the frame's class, and the casters that began while the atlas was
active, the frame before's when it begins */
static short shadow_atlas_class = 0;
static short shadow_atlas_casters = 0;
static boolean shadow_atlas_failed = FALSE;
/* the casters go to the atlas, and how many it has */
static boolean shadow_atlas_active = FALSE;
static short shadow_atlas_count = 0;
/* the region of the shadow begun last (NONE: its map's), and of the shadow
whose receivers are drawn (NONE: not the atlas's) */
static short shadow_atlas_region = NONE;
static short shadow_atlas_receiving = NONE;
static struct
{
	real_matrix4x3 matrix;
	real_rgb_color color;
	real radius;
	boolean used;
} shadow_atlas_shadows[SHADOW_ATLAS_REGIONS];

/* ---------- globals */

extern D3DDevice *global_d3d_device;
extern struct pixel_shader_definition pixel_shader;

static boolean shadow_restored = TRUE;

static real_rgb_color local_shadow_color = { 0 };
static real local_object_bounding_radius = 0.0f;
static real_matrix4x3 local_shadow_matrix = { 0 };
static struct rasterizer_model_begin_parameters const *local_parameters = NULL;
static boolean shadow_setup = FALSE;
static boolean shadow_used = FALSE;

/* ---------- public code */

void _rasterizer_environment_shadows_begin(
	void)
{
	rasterizer_profile_begin(_rasterizer_profile_environment_shadows);

	return;
}

boolean _rasterizer_environment_shadow_begin(
	long object_index,
	real_matrix4x3 const *shadow_matrix,
	real_rgb_color const *shadow_color,
	real object_bounding_radius,
	real *shadow_volume_bounding_radius)
{
	boolean success = TRUE;
	real inverse_radius;
	real vsh_constants__screenproj[20];

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		147,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			153,
			shadow_matrix);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			154,
			shadow_color);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			155,
			shadow_color->red >=0.0f && shadow_color->red <=1.0f);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			156,
			shadow_color->green>=0.0f && shadow_color->green<=1.0f);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			157,
			shadow_color->blue >=0.0f && shadow_color->blue <=1.0f);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			158,
			object_bounding_radius>0.0f);

		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED |
			D3DCOLORWRITEENABLE_GREEN |
			D3DCOLORWRITEENABLE_BLUE);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, FALSE);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, TRUE);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHAREF, 0x7F);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, FALSE);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);

		csmemset(&pixel_shader, 0, sizeof(pixel_shader));
		pixel_shader.texture_modes = 1;
		pixel_shader.combiner_count = 1;
		pixel_shader.final_combiner_inputs_abcd = 0x00000020;
		pixel_shader.final_combiner_inputs_efg = 0x00001800;
		rasterizer_set_pixel_shader(&pixel_shader);

		inverse_radius = 1.0f / object_bounding_radius;

		vsh_constants__screenproj[0] = inverse_radius * shadow_matrix->forward.i;
		vsh_constants__screenproj[1] = inverse_radius * shadow_matrix->forward.j;
		vsh_constants__screenproj[2] = inverse_radius * shadow_matrix->forward.k;
		vsh_constants__screenproj[3] = -inverse_radius * dot_product3d(
			&shadow_matrix->forward,
			(real_vector3d const *)&shadow_matrix->position);
		vsh_constants__screenproj[4] = inverse_radius * shadow_matrix->left.i;
		vsh_constants__screenproj[5] = inverse_radius * shadow_matrix->left.j;
		vsh_constants__screenproj[6] = inverse_radius * shadow_matrix->left.k;
		vsh_constants__screenproj[7] = -inverse_radius * dot_product3d(
			&shadow_matrix->left,
			(real_vector3d const *)&shadow_matrix->position);
		vsh_constants__screenproj[8] = 0.0f;
		vsh_constants__screenproj[9] = 0.0f;
		vsh_constants__screenproj[10] = 0.0f;
		vsh_constants__screenproj[11] = 0.5f;
		vsh_constants__screenproj[12] = 0.0f;
		vsh_constants__screenproj[13] = 0.0f;
		vsh_constants__screenproj[14] = 0.0f;
		vsh_constants__screenproj[15] = 1.0f;
		vsh_constants__screenproj[16] = 0.0f;
		vsh_constants__screenproj[17] = 0.0f;
		vsh_constants__screenproj[18] = 0.0f;
		vsh_constants__screenproj[19] = 0.0f;
		IDirect3DDevice8_SetVertexShaderConstant(
			global_d3d_device,
			-68,
			vsh_constants__screenproj,
			5);

		/* port: into a region of the shadow atlas while it has one free
		(rasterizer_shadow_atlas_begin), else into the map as before */
		if (shadow_atlas_active)
		{
			shadow_atlas_casters++;
		}
		shadow_atlas_region = shadow_atlas_active && shadow_atlas_count < shadow_atlas_capacity(shadow_atlas_class) ?
			shadow_atlas_count++ :
			NONE;
		if (shadow_atlas_region != NONE)
		{
			shadow_atlas_caster_target(shadow_atlas_region);
		}
		else
		{
			rasterizer_set_target(
				2,
				0,
				rasterizer_debug_options.shadow_debug_enabled ? 0x88888888 : 0,
				TRUE,
				FALSE);
		}
		rasterizer_set_stencil_mode(0);

		local_shadow_matrix = *shadow_matrix;
		local_shadow_color = *shadow_color;
		local_object_bounding_radius = object_bounding_radius;
		if (shadow_volume_bounding_radius)
		{
			*shadow_volume_bounding_radius = object_bounding_radius;
		}
		/* port: what its receivers are drawn with, after every caster's
		(rasterizer_shadow_atlas_receive) */
		if (shadow_atlas_region != NONE)
		{
			shadow_atlas_shadows[shadow_atlas_region].matrix = *shadow_matrix;
			shadow_atlas_shadows[shadow_atlas_region].color = *shadow_color;
			shadow_atlas_shadows[shadow_atlas_region].radius = object_bounding_radius;
		}

		local_parameters = 0;
		shadow_setup = FALSE;
		shadow_used = FALSE;
		shadow_restored = FALSE;

		if (rasterizer_debug_options.statistics_mode ==
			_rasterizer_statistics_mode_enabled)
		{
			rasterizer_frame_statistics.shadow_count++;
		}
	}

	return success;
}

void _rasterizer_environment_shadow_model_begin(
	struct rasterizer_model_begin_parameters const *parameters)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		239,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			245,
			parameters);

		rasterizer_set_model_skinning(&parameters->skinning);

		local_parameters = parameters;
		shadow_used = TRUE;

		if (rasterizer_debug_options.statistics_mode ==
			_rasterizer_statistics_mode_enabled)
		{
			rasterizer_frame_statistics.model_shadow_count++;
		}
	}

	return;
}

void _rasterizer_environment_shadow_model_end(
	void)
{
	local_parameters = 0;

	return;
}

void _rasterizer_environment_shadow_model_draw(
	struct shader const *shader,
	short bitmap_index,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer)
{
	struct shader_model_definition const *model;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		274,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			280,
			shader);

		if (shader->base.type == _shader_type_model)
		{
			model = (struct shader_model_definition *)shader_get_and_verify_type(
				(struct shader *)shader,
				_shader_type_model);

			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
				286,
				vertex_buffer);
			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
				287,
				triangle_buffer);
			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
				288,
				local_parameters);

			if (TEST_FLAG(model->flags, _shader_model_two_sided_bit))
			{
				IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
			}
			else
			{
				IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
			}
			rasterizer_set_vertex_shader_permutation(
				_shadow_model_vertex_shader_index,
				vertex_buffer->type,
				0);

			if (TEST_FLAG(model->flags, _shader_model_not_alpha_tested_bit))
			{
				IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_PSTEXTUREMODES, 0);
			}
			else
			{
				IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_PSTEXTUREMODES, 1);
				rasterizer_set_texture(
					0,
					0,
					1,
					model->base_map.index,
					bitmap_index);
				IDirect3DDevice8_SetTextureStageState(
					global_d3d_device,
					0,
					D3DTSS_ADDRESSU,
					D3DTADDRESS_WRAP);
				IDirect3DDevice8_SetTextureStageState(
					global_d3d_device,
					0,
					D3DTSS_ADDRESSV,
					D3DTADDRESS_WRAP);
				IDirect3DDevice8_SetTextureStageState(
					global_d3d_device,
					0,
					D3DTSS_MAGFILTER,
					D3DTEXF_LINEAR);
				IDirect3DDevice8_SetTextureStageState(
					global_d3d_device,
					0,
					D3DTSS_MINFILTER,
					D3DTEXF_LINEAR);
				IDirect3DDevice8_SetTextureStageState(
					global_d3d_device,
					0,
					D3DTSS_MIPFILTER,
					D3DTEXF_LINEAR);
			}

			{
				real_vector4d vertex_constants[3] =
				{
					{
						model->detail_map_scale,
						model->detail_map_scale * model->detail_map_v_scale,
						1.0f,
						1.0f
					},
					{1.0f, 0.0f, 0.0f, 0.0f},
					{0.0f, 1.0f, 0.0f, 0.0f}
				};

				shader_texture_animation_evaluate(
					&model->animation,
					&local_parameters->animation,
					local_parameters->base_map_scale.i * model->map_scale.i,
					local_parameters->base_map_scale.j * model->map_scale.j,
					0.0f,
					0.0f,
					0.0f,
					global_frame_parameters.game_time_sec,
					&vertex_constants[1],
					&vertex_constants[2]);
				IDirect3DDevice8_SetVertexShaderConstant(
					global_d3d_device,
					-84,
					vertex_constants,
					NUMBEROF(vertex_constants));
			}

			rasterizer_draw_static_triangles_static_vertices(
				triangle_buffer,
				0,
				triangle_buffer->count,
				vertex_buffer);

			if (rasterizer_debug_options.statistics_mode ==
				_rasterizer_statistics_mode_enabled)
			{
				rasterizer_frame_statistics.model_shadow_draw_count++;
				rasterizer_frame_statistics.model_shadow_triangle_count +=
					triangle_buffer->count;
				rasterizer_frame_statistics.model_shadow_vertex_count +=
					rasterizer_frame_statistics_count_static_vertices(
						triangle_buffer,
						vertex_buffer);
			}
		}
	}

	return;
}

void _rasterizer_environment_shadow_end(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		563,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		if (!shadow_used)
		{
			error(
				_error_silent,
				"### WARNING empty shadow has been cast");
		}

		if (!shadow_restored)
		{
			rasterizer_set_target(
				global_window_parameters.rasterizer_target,
				FALSE,
				FALSE,
				FALSE,
				TRUE);
			shadow_restored = TRUE;
		}
	}

	return;
}

void _rasterizer_environment_shadow_draw(
	struct shader const *shader,
	short bitmap_index,
	long dynamic_triangle_buffer_index,
	long first_triangle_index,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer)
{
	real vertex_constants[5][4];
	real inverse_radius;
	real inverse_radius_quarter;
	real inverse_radius_double;
	real position_dot_up;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		404,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		if (!shadow_setup)
		{
			/* port: a shadow of the atlas is blurred already, into its region
			of the chain's last atlas, sampled as its own map
			(rasterizer_shadow_atlas_convolve) */
			if (shadow_atlas_receiving != NONE)
			{
				real const width = (real)SHADOW_ATLAS_WIDTH;
				real const height = (real)shadow_atlas_heights[shadow_atlas_class];
				float region[4];

				region[0] = (real)SHADOW_ATLAS_MAP_SIZE / width;
				region[1] = (real)SHADOW_ATLAS_MAP_SIZE / height;
				region[2] = (real)shadow_atlas_x(shadow_atlas_receiving) / width;
				region[3] = (real)shadow_atlas_y(shadow_atlas_receiving) / height;
				IDirect3DDevice8_SetTexture(
					global_d3d_device,
					0,
					(IDirect3DBaseTexture8 *)shadow_atlas_textures[shadow_atlas_class][1]);
				d3d8_texture_region(0, region);
			}
			else
			{
				d3d8_texture_region(0, NULL);
				if (rasterizer_debug_options.shadow_convolution_enabled)
				{
					rasterizer_shadow_convolve();
				}

				rasterizer_set_target_as_texture(
					0,
					(rasterizer_debug_options.shadow_convolution_enabled != FALSE) + 2,
					FALSE);
			}
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_MIPFILTER,
				D3DTEXF_LINEAR);

			rasterizer_set_texture_direct(
				1,
				global_rasterizer_data->linear_corner_fade.index,
				0);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_MIPFILTER,
				D3DTEXF_LINEAR);

			rasterizer_set_texture_direct(
				2,
				global_rasterizer_data->vector_normalization.index,
				0);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_ADDRESSW,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_MIPFILTER,
				D3DTEXF_LINEAR);

			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_CULLMODE,
				D3DCULL_CCW);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_COLORWRITEENABLE,
				D3DCOLORWRITEENABLE_ALL);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHABLENDENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_SRCBLEND,
				D3DBLEND_ZERO);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_DESTBLEND,
				D3DBLEND_INVSRCCOLOR);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_BLENDOP,
				D3DBLENDOP_ADD);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHATESTENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHAREF,
				0);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZFUNC,
				D3DCMP_EQUAL);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZWRITEENABLE,
				FALSE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZBIAS,
				0);

			csmemset(&pixel_shader, 0, sizeof(pixel_shader));
			pixel_shader.texture_modes = 0x00000021;
			pixel_shader.combiner_count = 4;
			pixel_shader.constant_0[0] = real_rgb_color_to_pixel32(&local_shadow_color);
			pixel_shader.constant_1[0] = 0x00FFFFFF;
			pixel_shader.rgb_inputs[0] = 0x14200000;
			pixel_shader.rgb_outputs[0] = 0x000000C0;
			pixel_shader.rgb_inputs[1] = 0x290C0821;
			pixel_shader.rgb_outputs[1] = 0x000000CD;
			pixel_shader.rgb_inputs[2] = 0x2C200C2D;
			pixel_shader.rgb_outputs[2] = 0x00000C00;
			pixel_shader.rgb_inputs[3] = 0x2C020000;
			pixel_shader.rgb_outputs[3] = 0x000020D0;
			pixel_shader.final_combiner_inputs_abcd = 0x0000002C;
			pixel_shader.final_combiner_inputs_efg = 0x00000D00;
			if (rasterizer_debug_options.shadow_debug_enabled)
			{
				pixel_shader.final_combiner_inputs_abcd = 0x0000000C;
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_ALPHATESTENABLE,
					FALSE);
			}
			rasterizer_set_pixel_shader(&pixel_shader);

			rasterizer_set_vertex_shader_permutation(
				_shadow_vertex_shader_index,
				vertex_buffer->type,
				shader_get_vertex_shader_permutation(shader));

			inverse_radius = 1.0f / local_object_bounding_radius;
			inverse_radius_quarter = 1.0f / (local_object_bounding_radius * 4.0f);
			inverse_radius_double = 1.0f / (local_object_bounding_radius * 0.5f);

			vertex_constants[0][0] = local_shadow_matrix.forward.i * inverse_radius * 0.5f;
			vertex_constants[0][1] = local_shadow_matrix.forward.j * inverse_radius * 0.5f;
			vertex_constants[0][2] = local_shadow_matrix.forward.k * inverse_radius * 0.5f;
			vertex_constants[0][3] =
				(1.0f -
				dot_product3d(
					(real_vector3d const *)&local_shadow_matrix.position,
					&local_shadow_matrix.forward) *
				inverse_radius) * 0.5f;
			vertex_constants[1][0] = local_shadow_matrix.left.i * inverse_radius * -0.5f;
			vertex_constants[1][1] = local_shadow_matrix.left.j * inverse_radius * -0.5f;
			vertex_constants[1][2] = local_shadow_matrix.left.k * inverse_radius * -0.5f;
			vertex_constants[1][3] =
				(dot_product3d(
					(real_vector3d const *)&local_shadow_matrix.position,
					&local_shadow_matrix.left) *
				inverse_radius + 1.0f) * 0.5f;
			vertex_constants[2][0] = local_shadow_matrix.up.i * inverse_radius_quarter;
			vertex_constants[2][1] = local_shadow_matrix.up.j * inverse_radius_quarter;
			vertex_constants[2][2] = local_shadow_matrix.up.k * inverse_radius_quarter;
			position_dot_up = dot_product3d(
				(real_vector3d const *)&local_shadow_matrix.position,
				&local_shadow_matrix.up);
			vertex_constants[2][3] =
				-(position_dot_up * inverse_radius_quarter);
			vertex_constants[3][0] = -(local_shadow_matrix.up.i * inverse_radius_double);
			vertex_constants[3][1] = -(local_shadow_matrix.up.j * inverse_radius_double);
			vertex_constants[3][2] = -(local_shadow_matrix.up.k * inverse_radius_double);
			vertex_constants[3][3] = position_dot_up * inverse_radius_double;
			vertex_constants[4][0] = local_shadow_matrix.up.i;
			vertex_constants[4][1] = local_shadow_matrix.up.j;
			vertex_constants[4][2] = local_shadow_matrix.up.k;
			vertex_constants[4][3] = 0.0f;
			IDirect3DDevice8_SetVertexShaderConstant(
				global_d3d_device,
				-81,
				vertex_constants,
				5);

			if (!shadow_restored)
			{
				rasterizer_set_target(
					global_window_parameters.rasterizer_target,
					0,
					0,
					FALSE,
					TRUE);
				shadow_restored = TRUE;
			}

			shadow_setup = TRUE;
		}

		rasterizer_set_stencil_mode(RASTERIZER_STENCIL_MODE_REJECT);
		rasterizer_draw_dynamic_triangles_static_vertices(
			dynamic_triangle_buffer_index,
			first_triangle_index,
			triangle_count,
			vertex_buffer);

		if (rasterizer_debug_options.statistics_mode ==
			_rasterizer_statistics_mode_enabled)
		{
			rasterizer_frame_statistics.shadow_draw_count++;
			rasterizer_frame_statistics.shadow_triangle_count += triangle_count;
			rasterizer_frame_statistics.shadow_vertex_count +=
				rasterizer_frame_statistics_count_dynamic_vertices(
					dynamic_triangle_buffer_index,
					first_triangle_index,
					triangle_count);
		}
	}

	return;
}

void _rasterizer_environment_shadows_end(
	void)
{
	rasterizer_profile_end(_rasterizer_profile_environment_shadows);

	return;
}

/* ---------- private code */

static void rasterizer_shadow_convolve(
	void)
{
	short stage;
	real vertex_constants[8][4];
	/* port: the shadow maps' texels for each of the Xbox's 128 */
	long scale = halo_shadow_map_scale();
	long distance;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		31,
		global_d3d_device);

	if (rasterizer_debug_options.draw_environment_shadows &&
		rasterizer_debug_options.shadow_convolution_enabled)
	{
		for (stage = 0; stage < 4; stage++)
		{
			rasterizer_set_target_as_texture(stage, 2, FALSE);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_MIPFILTER,
				D3DTEXF_POINT);
		}

		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_CULLMODE,
			D3DCULL_CCW);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED |
			D3DCOLORWRITEENABLE_GREEN |
			D3DCOLORWRITEENABLE_BLUE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHABLENDENABLE,
			FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHATESTENABLE,
			FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ZENABLE,
			FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ZBIAS,
			0);

		rasterizer_set_vertex_shader_permutation(
			_shadow_convolution_vertex_shader_index,
			8,
			0);

		vertex_constants[0][0] = 1.0f;
		vertex_constants[0][1] = 0.0f;
		vertex_constants[0][2] = 0.0f;
		vertex_constants[0][3] = -0.00390625f;
		vertex_constants[1][0] = 0.0f;
		vertex_constants[1][1] = 1.0f;
		vertex_constants[1][2] = 0.0f;
		vertex_constants[1][3] = -0.00390625f;
		vertex_constants[2][0] = 1.0f;
		vertex_constants[2][1] = 0.0f;
		vertex_constants[2][2] = 0.0f;
		vertex_constants[2][3] = 0.00390625f;
		vertex_constants[3][0] = 0.0f;
		vertex_constants[3][1] = 1.0f;
		vertex_constants[3][2] = 0.0f;
		vertex_constants[3][3] = 0.00390625f;
		vertex_constants[4][0] = 1.0f;
		vertex_constants[4][1] = 0.0f;
		vertex_constants[4][2] = 0.0f;
		vertex_constants[4][3] = -0.00390625f;
		vertex_constants[5][0] = 0.0f;
		vertex_constants[5][1] = 1.0f;
		vertex_constants[5][2] = 0.0f;
		vertex_constants[5][3] = 0.00390625f;
		vertex_constants[6][0] = 1.0f;
		vertex_constants[6][1] = 0.0f;
		vertex_constants[6][2] = 0.0f;
		vertex_constants[6][3] = 0.00390625f;
		vertex_constants[7][0] = 0.0f;
		vertex_constants[7][1] = 1.0f;
		vertex_constants[7][2] = 0.0f;
		vertex_constants[7][3] = -0.00390625f;
		/* port: half a texel of a larger map (rasterizer_shadow_convolve_pass) */
		if (scale > 1)
		{
			for (distance = 0; distance < 8; distance++)
			{
				vertex_constants[distance][3] /= (real)scale;
			}
		}
		IDirect3DDevice8_SetVertexShaderConstant(
			global_d3d_device,
			-81,
			vertex_constants,
			8);

		csmemset(&pixel_shader, 0, sizeof(pixel_shader));
		pixel_shader.texture_modes = 0x00008421;
		pixel_shader.combiner_count = 1;
		pixel_shader.alpha_inputs[0] = 0x08A009A0;
		pixel_shader.alpha_outputs[0] = 0x00030C00;
		pixel_shader.rgb_inputs[0] = 0x0AA00BA0;
		pixel_shader.rgb_outputs[0] = 0x00030C00;
		pixel_shader.final_combiner_inputs_abcd = 0x0C20001C;
		rasterizer_set_pixel_shader(&pixel_shader);

		rasterizer_set_target(3, 0, 0, FALSE, FALSE);

		IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			-1.0078125f,
			1.0078125f);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 0);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			0.9921875f,
			1.0078125f);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 1);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			0.9921875f,
			-0.9921875f);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 1);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			-1.0078125f,
			-0.9921875f);
		IDirect3DDevice8_End(global_d3d_device);

		/* port: the passes that widen the blur of a larger map, back and
		forth between the maps, ending in the secondary */
		for (distance = 1; distance < scale; distance *= 2)
		{
			rasterizer_shadow_convolve_pass(3, 2, (real)distance / (real)(128 * scale));
			rasterizer_shadow_convolve_pass(2, 3, (real)distance / (real)(128 * scale));
		}
	}

	return;
}

/* port: the shadow maps can be drawn larger than the Xbox's 128x128
(display.shadow_resolution, port/linux/src/d3d8_device.c): scale times as many
texels each way, a power of two. The Xbox's blur takes four taps diagonally
half a texel off each texel, on the corners it shares with its neighbours.
Each tap averages the four texels there, and together they weigh the texel
and its neighbours 1, 2, 1 each way: a tent over three texels, which is a box
two texels wide taken twice. For the shadows to stay as soft, a larger map's
blur must cover as much of the map: a tent scale times as wide, a box of
2 * scale texels taken twice, which weighs the texels 2 * scale - |d| for |d|
up to 2 * scale - 1 texels away. The Xbox's pass, with its taps half a texel
of the larger map off, is the box of two texels taken twice. A further pass
with its taps diagonally distance texels off, on texels' centres, averages
texels 2 * distance apart each way, which doubles a box of 2 * distance
texels; two passes each at distance 1, 2, ... scale / 2 make both boxes
2 * scale texels wide. The quad's offsets of half a texel stay as they are:
they are half of the game's units, which the port undoes in the same units
at any size (port/linux/src/nv2a_vsh.c), so the quad covers the map. */
static void rasterizer_shadow_convolve_pass(
	short source_target,
	short destination_target,
	real offset)
{
	short stage;
	real vertex_constants[8][4];

	csmemset(vertex_constants, 0, sizeof(vertex_constants));
	for (stage = 0; stage < 4; stage++)
	{
		rasterizer_set_target_as_texture(stage, source_target, FALSE);
		vertex_constants[stage * 2][0] = 1.0f;
		vertex_constants[stage * 2 + 1][1] = 1.0f;
	}
	/* the Xbox pass's corners, in its order */
	vertex_constants[0][3] = -offset;
	vertex_constants[1][3] = -offset;
	vertex_constants[2][3] = offset;
	vertex_constants[3][3] = offset;
	vertex_constants[4][3] = -offset;
	vertex_constants[5][3] = offset;
	vertex_constants[6][3] = offset;
	vertex_constants[7][3] = -offset;
	IDirect3DDevice8_SetVertexShaderConstant(
		global_d3d_device,
		-81,
		vertex_constants,
		8);

	rasterizer_set_target(destination_target, 0, 0, FALSE, FALSE);

	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, -1.0078125f, 1.0078125f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, 0.9921875f, 1.0078125f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 1);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, 0.9921875f, -0.9921875f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 1);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, -1.0078125f, -0.9921875f);
	IDirect3DDevice8_End(global_d3d_device);

	return;
}

/* ---------- port: the shadow atlas (above, "the shadow atlas") */

static long shadow_atlas_x(
	short region)
{
	return SHADOW_ATLAS_MARGIN + (region % SHADOW_ATLAS_COLUMNS) * SHADOW_ATLAS_SPACING;
}

static long shadow_atlas_y(
	short region)
{
	return SHADOW_ATLAS_MARGIN + (region / SHADOW_ATLAS_COLUMNS) * SHADOW_ATLAS_SPACING;
}

/* the regions a class's atlases hold */
static short shadow_atlas_capacity(
	short atlas_class)
{
	return (short)((shadow_atlas_heights[atlas_class] / SHADOW_ATLAS_SPACING) * SHADOW_ATLAS_COLUMNS);
}

/* a render target of the maps' format */
static boolean shadow_atlas_target_make(
	long width,
	long height,
	D3DTexture **texture,
	D3DSurface **surface)
{
	return IDirect3DDevice8_CreateTexture(
			global_d3d_device,
			width,
			height,
			1,
			D3DUSAGE_RENDERTARGET,
			D3DFMT_R5G6B5,
			D3DPOOL_DEFAULT,
			texture) >= 0 &&
		*texture &&
		IDirect3DTexture8_GetSurfaceLevel(*texture, 0, surface) >= 0 &&
		*surface;
}

/* a class's atlases, made the first time they are needed */
static boolean shadow_atlas_make(
	short atlas_class)
{
	short index;

	if (shadow_atlas_failed)
	{
		return FALSE;
	}
	for (index = 0; !shadow_atlas_failed && !shadow_atlas_class_made[atlas_class] && index < 2; index++)
	{
		if (!shadow_atlas_target_make(
			SHADOW_ATLAS_WIDTH,
			shadow_atlas_heights[atlas_class],
			&shadow_atlas_textures[atlas_class][index],
			&shadow_atlas_surfaces[atlas_class][index]))
		{
			shadow_atlas_failed = TRUE;
		}
	}
	if (shadow_atlas_failed)
	{
		error(_error_silent, "### ERROR the shadow atlas cannot be made: shadows are drawn one at a time");
		return FALSE;
	}
	shadow_atlas_class_made[atlas_class] = TRUE;

	return TRUE;
}

/* an atlas of the frame's class drawn into, all of it emptied first if
clear, the viewport a region's (NONE: all of it) */
static void shadow_atlas_set_target(
	short atlas,
	short region,
	boolean clear)
{
	D3DVIEWPORT8 viewport;

	IDirect3DDevice8_SetRenderTarget(global_d3d_device, shadow_atlas_surfaces[shadow_atlas_class][atlas], NULL);
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = SHADOW_ATLAS_WIDTH;
	viewport.Height = shadow_atlas_heights[shadow_atlas_class];
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	if (clear)
	{
		IDirect3DDevice8_SetViewport(global_d3d_device, &viewport);
		IDirect3DDevice8_Clear(global_d3d_device, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
	}
	if (region != NONE)
	{
		viewport.X = shadow_atlas_x(region);
		viewport.Y = shadow_atlas_y(region);
		viewport.Width = SHADOW_ATLAS_MAP_SIZE;
		viewport.Height = SHADOW_ATLAS_MAP_SIZE;
	}
	IDirect3DDevice8_SetViewport(global_d3d_device, &viewport);

	return;
}

/* a caster drawn into its region of the casters' atlas, which the frame's
first empties */
static void shadow_atlas_caster_target(
	short region)
{
	shadow_atlas_set_target(0, region, region == 0);

	return;
}

/* the chain's passes for the scale: the Xbox's, and each widening
distance's two (rasterizer_shadow_convolve) */
static short shadow_atlas_pass_count(
	long scale)
{
	short count = 1;
	long distance;

	for (distance = 1; distance < scale; distance *= 2)
	{
		count += 2;
	}

	return count;
}

/* the offset of a pass's taps, in a map's texture coordinates: half a
texel for the Xbox's, its distance in texels for a widening pass's */
static real shadow_atlas_pass_offset(
	short pass,
	long scale)
{
	return pass == 0 ?
		0.00390625f / (real)scale :
		(real)(1L << ((pass - 1) / 2)) / (real)(128 * scale);
}

/* the chain's state, as rasterizer_shadow_convolve sets it, sampling an
atlas */
static void shadow_atlas_states(
	short atlas)
{
	short stage;

	for (stage = 0; stage < 4; stage++)
	{
		IDirect3DDevice8_SetTexture(
			global_d3d_device,
			stage,
			(IDirect3DBaseTexture8 *)shadow_atlas_textures[shadow_atlas_class][atlas]);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_ADDRESSU, D3DTADDRESS_BORDER);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_ADDRESSV, D3DTADDRESS_BORDER);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);
	rasterizer_set_vertex_shader_permutation(_shadow_convolution_vertex_shader_index, 8, 0);

	csmemset(&pixel_shader, 0, sizeof(pixel_shader));
	pixel_shader.texture_modes = 0x00008421;
	pixel_shader.combiner_count = 1;
	pixel_shader.alpha_inputs[0] = 0x08A009A0;
	pixel_shader.alpha_outputs[0] = 0x00030C00;
	pixel_shader.rgb_inputs[0] = 0x0AA00BA0;
	pixel_shader.rgb_outputs[0] = 0x00030C00;
	pixel_shader.final_combiner_inputs_abcd = 0x0C20001C;
	rasterizer_set_pixel_shader(&pixel_shader);

	return;
}

/* a pass's quad over the viewport, its taps the pass's corners
(rasterizer_shadow_convolve_pass) about the texels of a region of the
atlas sampled */
static void shadow_atlas_quad(
	short region,
	real offset)
{
	/* the Xbox pass's corners, in its order */
	static real const signs[8] = { -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f };
	real const width = (real)SHADOW_ATLAS_WIDTH;
	real const height = (real)shadow_atlas_heights[shadow_atlas_class];
	real vertex_constants[8][4];
	short stage;

	csmemset(vertex_constants, 0, sizeof(vertex_constants));
	for (stage = 0; stage < 4; stage++)
	{
		/* (the map's coordinates, 0 to 1, made the region's) */
		vertex_constants[stage * 2][0] = (real)SHADOW_ATLAS_MAP_SIZE / width;
		vertex_constants[stage * 2][3] =
			((real)shadow_atlas_x(region) + signs[stage * 2] * offset * (real)SHADOW_ATLAS_MAP_SIZE) / width;
		vertex_constants[stage * 2 + 1][1] = (real)SHADOW_ATLAS_MAP_SIZE / height;
		vertex_constants[stage * 2 + 1][3] =
			((real)shadow_atlas_y(region) + signs[stage * 2 + 1] * offset * (real)SHADOW_ATLAS_MAP_SIZE) / height;
	}
	IDirect3DDevice8_SetVertexShaderConstant(
		global_d3d_device,
		-81,
		vertex_constants,
		8);

	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, -1.0078125f, 1.0078125f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, 0.9921875f, 1.0078125f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 1);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, 0.9921875f, -0.9921875f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 1);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, -1.0078125f, -0.9921875f);
	IDirect3DDevice8_End(global_d3d_device);

	return;
}

boolean rasterizer_shadow_atlas_begin(
	void)
{
	/* the class whose atlases hold the frame before's casters and a few */
	shadow_atlas_class = 0;
	while (shadow_atlas_class < SHADOW_ATLAS_CLASSES - 1 &&
		shadow_atlas_casters + SHADOW_ATLAS_SLACK > shadow_atlas_capacity(shadow_atlas_class))
	{
		shadow_atlas_class++;
	}
	shadow_atlas_casters = 0;
	shadow_atlas_active = FALSE;
	shadow_atlas_count = 0;
	shadow_atlas_region = NONE;
	shadow_atlas_receiving = NONE;
	if (global_window_parameters.rasterizer_target == _rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows &&
		rasterizer_debug_options.shadow_convolution_enabled &&
		!rasterizer_debug_options.shadow_debug_enabled &&
		halo_shadow_map_scale() <= SHADOW_ATLAS_MOST_SCALE &&
		shadow_atlas_make(shadow_atlas_class))
	{
		shadow_atlas_active = TRUE;
	}

	return shadow_atlas_active;
}

short rasterizer_shadow_atlas_caster_end(
	void)
{
	short region = shadow_atlas_region;

	if (region != NONE)
	{
		shadow_atlas_shadows[region].used = shadow_used;
		shadow_atlas_region = NONE;
	}

	return region;
}

void rasterizer_shadow_atlas_convolve(
	void)
{
	long scale = halo_shadow_map_scale();
	short pass_count = shadow_atlas_pass_count(scale);
	short pass, region;

	if (!shadow_atlas_active)
	{
		return;
	}
	shadow_atlas_active = FALSE;
	if (!shadow_atlas_count)
	{
		return;
	}
	/* every pass, back and forth from the casters' atlas, ending in the
	other (the count is odd), whose regions the receivers sample; each
	atlas's guard bands black, as each pass empties the atlas it draws into
	(its quads cover their regions) */
	for (pass = 0; pass < pass_count; pass++)
	{
		shadow_atlas_states(pass & 1);
		for (region = 0; region < shadow_atlas_count; region++)
		{
			shadow_atlas_set_target(!(pass & 1), region, region == 0);
			shadow_atlas_quad(region, shadow_atlas_pass_offset(pass, scale));
		}
	}
	rasterizer_set_target(
		global_window_parameters.rasterizer_target,
		0,
		0,
		FALSE,
		TRUE);
	shadow_restored = TRUE;

	return;
}

void rasterizer_shadow_atlas_receive(
	short region)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		0,
		region >= 0 && region < shadow_atlas_count);

	local_shadow_matrix = shadow_atlas_shadows[region].matrix;
	local_shadow_color = shadow_atlas_shadows[region].color;
	local_object_bounding_radius = shadow_atlas_shadows[region].radius;
	local_parameters = NULL;
	shadow_setup = FALSE;
	shadow_used = shadow_atlas_shadows[region].used;
	shadow_restored = FALSE;
	shadow_atlas_receiving = region;

	return;
}

void rasterizer_shadow_atlas_end(
	void)
{
	d3d8_texture_region(0, NULL);
	shadow_atlas_active = FALSE;
	shadow_atlas_count = 0;
	shadow_atlas_region = NONE;
	shadow_atlas_receiving = NONE;

	return;
}
