/*
RENDER_ENUMERATE.C

A map's shaders and pipelines made as it starts, not as its draws first
need them: the first frame of a map (and of each of its structures) is
drawn first through every shader its tags hold, in each way the game's
code draws them, and not shown (the frame's own window, drawn after, clears
the target). The draws are the game's own (its structure, model, light,
shadow, effect and interface code), with stand-in geometry: every material
of the structure, every model's every part, every object's effects, every
light, and so on, in each of the states the code tells apart (the fogs,
the models' lighting, functions and colours, first person). The renderer
makes each draw's shaders and pipeline as it would at the draw; the frame
then waits until they are made (xgpu_backend_prepared: the loading screen's
end), so that no draw of the map has a shader or a pipeline to make.

The draws made so are the map's enumeration (xgpu_manifest_enumeration,
port/linux/src/xgpu_manifest.h): what a backend makes for the map. With
debug.render_manifest, the recording marks each later draw the enumeration
does not have ("# missed:", with the stack that made it), which
tools/render_manifest.py check reports.

The window is drawn once for each pass: a fog, a part of what is drawn in
it (the world's, the objects' effects', first person's, a mirror's), and
with the enhanced dynamic lights, whether the renderer's are lit. A new
structure of the map is drawn through its world's passes alone. Some of
the game's code a pass reaches through port functions that draw as it does
(the decals', the lens flares', the weapon HUDs', the screen effect's, the
detail objects', the weather's: each in its own file). render_window
(source/render/render.c) calls in at each part: render_enumerating says
whether the window being drawn is one of these. The texture cache waits for
each texture while enumerating (xbox_texture_cache.c), so that no draw is
skipped or made without its sampler.
*/

#include "cseries.h"
#include "structure_bsp_cache.h"
#include "cache/cache_files.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/light_definitions.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "objects/widgets/light_volumes.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_cinematics.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_environment.h"
#include "rasterizer/rasterizer_lights.h"
#include "rasterizer/rasterizer_model_types.h"
#include "rasterizer/rasterizer_models.h"
#include "rasterizer/rasterizer_widgets.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/sky_definitions.h"
#include "shaders/shaders.h"
#include "structures/structure_bsp_definitions.h"
#include "structures/structure_render.h"
#include "structures/structure_visibility.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "cseries/cseries_windows.h"
#include "cache/texture_cache.h"
#include "menu_pictures.h"
#include "game/game_globals.h"
#include "items/weapon_definitions.h"
#include "units/unit_definitions.h"
#include "cutscene/cinematics.h"
#include "interface/hud_draw.h"
#include "interface/hud_weapon.h"
#include "interface/interface.h"
#include "interface/progress_bar.h"
#include "interface/ui_widget.h"
#include "interface/unit_hud_interface_definition.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/text_group.h"
#include "effects/contrail_definitions.h"
#include "effects/particle_system_definitions.h"
#include "effects/particles.h"
#include "effects/weather_particle_systems.h"
#include "render/render_sprite.h"

#include "halo_dynamic_lights.h"
#include "render_enumerate.h"
#ifdef HALO_VR
#include "../src/vr.h"
boolean vr_render_eye_pass(void);
void vr_render_sky(boolean sky);
/* (render_sprite.c's) */
void render_sprite_vr_thick_force(boolean forced);
#endif

#include <stdlib.h>
#include <string.h>

/* (port/linux/game/dynamic_lights.c) */
void dynamic_lights_note(long rasterizer_light_index, long flags, real cone_scale, real cone_offset,
	real_point3d const *tick_position);
void dynamic_lights_submit(void);

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
int config_boolean(char const *name);
void platform_log(char const *format, ...);
/* cache_files.c's */
boolean tag_index_is_group(long tag_index, long group_tag);

/* (rasterizer_xbox.c's) */
void rasterizer_set_stencil_mode(long mode);
enum
{
	_planar_fog_off = 0,
	_planar_fog_normal,
	_planar_fog_fully_fogged,
};

/* (models.c's) */
enum
{
	NUMBER_OF_DETAIL_LEVELS_PER_MODEL = 5,
};

enum
{
	_render_model_immediate_bit = 0,
	_render_model_shadow_bit,
	_render_model_no_planar_fog_bit,
	_render_model_first_person_bit,
};

/* (render.c's) */
enum
{
	_render_target_primary = 0,
	_render_target_secondary,
};

/* (render_objects.c's) */
enum
{
	_render_model_effect_type_none = 0,
	_render_model_effect_type_active_camouflage,
	_render_model_effect_type_modifier,
};

/* the fogs the passes are drawn in: the map's own as the camera sees it,
none, the atmosphere's alone, the planar fog's (seen through and from
inside it), both */
enum
{
	_fog_own,
	_fog_none,
	_fog_atmospheric,
	_fog_planar,
	_fog_planar_full,
	_fog_both,
	NUMBER_OF_FOGS,
};

/* what each fog's window is drawn with: the world (the structure, its
lights and shadows, every model in each of its lightings, functions and
colours, every effect), the objects' own effects on their models
(modifier shaders, camouflage), first person (the models and the
effects drawn as the first-person weapon's: apart, as their transparent
geometry is sorted after the world's), and the world as a mirror sees it */
enum
{
	_part_world,
	_part_object_effects,
	_part_first_person,
	/* (the world again as a mirror's window draws it, into its target, for a
	structure with a mirror; the world's then draws the mirrors) */
	_part_mirror,
	NUMBER_OF_PARTS,
};

/* each fog's parts are drawn once, and with display.dynamic_lights
"enhanced" again with the renderer's lights (the stand-in ones: models and
the structure drawn lit for each pixel by them) */
#define MAXIMUM_LIGHTINGS 2

/* The first-person model leaves the stencil rejecting its pixels for the
world after it. It may not be visible in the loading frame yet. Prepare
both contexts, including when the weapon is hidden or the player is dead. */
#define NUMBER_OF_STENCIL_CONTEXTS 2

/* a model's every geometry, as many as are told apart */
#define MAXIMUM_ENUMERATED_GEOMETRIES 1024

/* the transparent geometry's groups a model can queue, at most: the
window's queue is drawn and emptied before a model or an effect is drawn
when it has no more room (it holds no more than a frame's) */
#define TRANSPARENT_GROUPS_ROOM 256

/* the maps enumerated in this run of the game remembered (their pipelines
are made: a map loaded again, or its game started again, needs none) */
#define MAXIMUM_ENUMERATED_MAPS 64

/* the structures' materials told apart (material_key) whose pipelines a
map's enumerations have made, at most */
#define MAXIMUM_ENUMERATED_MATERIALS 1024
/* (a material's key as a mirror's window draws it) */
#define MATERIAL_KEY_MIRROR 0x80000000UL
/* (its class: whether its lightmap has a picture, and its flags) */
#define MATERIAL_KEY_CLASS(key) ((key) >> 16)
/* the rounds of the map's other structures' materials drawn as its first
frame's (stand_ins_next), at most */
#define MAXIMUM_STAND_IN_ROUNDS 4

/* (an object index no object has, but not 0: camouflage's source) */
#define STAND_IN_OBJECT_INDEX 0x7ffe0000

boolean render_enumerating = FALSE;

static struct
{
	/* a map loaded since (its tags' indices are no map's own), and the
	structures enumerated since, the one being enumerated and whether it has
	a mirror */
	boolean new_map;
	unsigned long *structure_bsps_enumerated[2];
	/* and their materials' keys (material_key), in each view */
	struct enumerated_materials *materials_enumerated[2];
	short structure_bsp_index;
	boolean mirrors;
	boolean scope;
	/* this frame's: whether the models and effects are drawn too (a new
	map), the pass being drawn, the map's own fog */
	boolean everything, map_drawn;
	short pass;
	/* the interface drawn in a pass (not where the VR mode's eyes are
	drawn), or to draw in the frame's HUD pass (render_enumerate_hud) */
	boolean interface_drawn, interface_pending;
	struct render_fog own_fog;
	unsigned long started;
	/* (the CPU time the game's thread has spent: halo_thread_cpu_seconds) */
	double started_cpu;
	/* the frame's lens flares before the enumeration's (none of these
	left for the frame's own window) */
	long lens_flare_count;
	/* the pass's lights (rasterizer_light_submit's) */
	short light_count;
	long light_indices[MAXIMUM_LIGHTS_PER_WINDOW];
} enumerate = { TRUE, { NULL, NULL }, { NULL, NULL }, NONE };

/* the keys (material_key) of the materials of a map's structures
enumerated: a structure whose every material's key is here has its
pipelines already, and is not enumerated (a load zone's switch to it draws
nothing more); one with new keys draws those materials' surfaces alone. The
keys a map has more of than this are as if new (enumerated each time). */
struct enumerated_materials
{
	short count;
	unsigned long keys[MAXIMUM_ENUMERATED_MATERIALS];
};

/* the maps enumerated (a map is its cache file's checksum and its
scenario's name), and each one's structures and their materials */
static struct
{
	unsigned long checksum;
	char name[64];
	unsigned long structure_bsps[2];
	struct enumerated_materials *materials[2];
} enumerated_maps[MAXIMUM_ENUMERATED_MAPS];
static short enumerated_map_count = 0;

/* the map's structures enumerated: a map not seen in this run of the game
has none */
static unsigned long *map_structures_enumerated(short view, struct enumerated_materials **materials)
{
	static unsigned long none[2] = { 0, 0 };
	unsigned long checksum = cache_files_get_checksum();
	char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	short index;

	for (index = 0; index < enumerated_map_count; index++)
	{
		if (enumerated_maps[index].checksum == checksum && !strncmp(enumerated_maps[index].name, name,
			sizeof(enumerated_maps[index].name) - 1))
		{
			*materials = enumerated_maps[index].materials[view];
			return &enumerated_maps[index].structure_bsps[view];
		}
	}
	*materials = NULL;
	if (enumerated_map_count == MAXIMUM_ENUMERATED_MAPS)
	{
		none[view] = 0;
		return &none[view];
	}
	index = enumerated_map_count++;
	enumerated_maps[index].checksum = checksum;
	strncpy(enumerated_maps[index].name, name, sizeof(enumerated_maps[index].name) - 1);
	enumerated_maps[index].structure_bsps[0] = 0;
	enumerated_maps[index].structure_bsps[1] = 0;
	/* (none if there is no memory for them: every structure enumerated) */
	enumerated_maps[index].materials[0] = calloc(1, sizeof(struct enumerated_materials));
	enumerated_maps[index].materials[1] = calloc(1, sizeof(struct enumerated_materials));
	*materials = enumerated_maps[index].materials[view];
	return &enumerated_maps[index].structure_bsps[view];
}

/* ---------- the structure's materials */

/* what tells a structure's material's draws apart: its shader, whether
its lightmap has a picture, and its flags */
static unsigned long material_key(struct structure_lightmap const *lightmap, struct structure_material const *material)
{
	return (unsigned long)DATUM_INDEX_TO_ABSOLUTE_INDEX(material->shader.index) & 0xffffUL |
		(lightmap->bitmap_index != NONE ? 0x10000UL : 0) | ((unsigned long)material->flags & 0x3fffUL) << 17;
}

static boolean materials_have(struct enumerated_materials const *materials, unsigned long key)
{
	short index;

	if (!materials)
		return FALSE;
	for (index = 0; index < materials->count; index++)
	{
		if (materials->keys[index] == key)
			return TRUE;
	}
	return FALSE;
}

static void materials_add(struct enumerated_materials *materials, unsigned long key)
{
	if (materials && materials->count < MAXIMUM_ENUMERATED_MATERIALS && !materials_have(materials, key))
		materials->keys[materials->count++] = key;
}

/* each of the structure's materials' keys, as its world draws them and (a
structure with a mirror) as its mirror's window does: whether one is new
(not in materials), or with add, they are added */
static boolean structure_materials(struct structure_bsp *structure_bsp, struct enumerated_materials *materials,
	boolean add)
{
	boolean mirrors = structure_visibility_port_has_mirrors();
	short lightmap_index, material_index;

	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
			struct structure_lightmap);

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			unsigned long key = material_key(lightmap, TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material));

			if (add)
			{
				materials_add(materials, key);
				if (mirrors)
					materials_add(materials, key | MATERIAL_KEY_MIRROR);
			}
			else if (!materials_have(materials, key) || (mirrors && !materials_have(materials, key | MATERIAL_KEY_MIRROR)))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* ---------- the map's other structures' materials

As a map starts, the materials of its other structures (each kept as it
was read: structure_bsp_cache.c) whose keys the first structure has not are
drawn too, so that no switch to another structure (a load zone) has
anything to make: after the frame's passes, rounds of its world's passes in
which materials of the first structure of the same class (MATERIAL_KEY_CLASS)
each stand in for one of them, drawn with its shader (and its permutation
0), and given their own back after the round. A key no material of the
first structure can stand in for is left to its structure's own
enumeration, at the switch to it. */

static struct
{
	/* the keys still to draw, and their shaders */
	struct enumerated_materials pending;
	long pending_shader_indices[MAXIMUM_ENUMERATED_MATERIALS];
	/* the round being drawn (0 the frame's own passes), its materials
	standing in, their own shaders and permutations, the keys they stand
	for */
	short round;
	short count;
	struct structure_material *materials[MAXIMUM_ENUMERATED_MATERIALS];
	long shader_indices[MAXIMUM_ENUMERATED_MATERIALS];
	short permutation_indices[MAXIMUM_ENUMERATED_MATERIALS];
	struct enumerated_materials keys;
	/* a lightmap of the first structure drawn without its picture, its
	materials standing in for those of lightmaps without one, and its
	picture */
	struct structure_lightmap *unlit;
	short unlit_bitmap_index;
} stand_ins;

static void stand_ins_visit(void *context, struct structure_lightmap const *lightmap,
	struct structure_material const *material)
{
	struct enumerated_materials const *enumerated = context;
	unsigned long key = material_key(lightmap, material);

	if (material->shader.index != NONE && tag_index_is_group(material->shader.index, SHADER_DEFINITION_TAG) &&
		!materials_have(enumerated, key) && !materials_have(&stand_ins.pending, key) &&
		stand_ins.pending.count < MAXIMUM_ENUMERATED_MATERIALS)
	{
		stand_ins.pending_shader_indices[stand_ins.pending.count] = material->shader.index;
		stand_ins.pending.keys[stand_ins.pending.count++] = key;
	}
}

/* the other structures' keys the map's enumerations have not drawn, and
the first structure's will not */
static void stand_ins_collect(struct structure_bsp *structure_bsp, struct enumerated_materials *enumerated)
{
	static struct enumerated_materials known;

	stand_ins.pending.count = 0;
	stand_ins.round = 0;
	stand_ins.count = 0;
	stand_ins.keys.count = 0;
	if (!enumerated)
		return;
	known = *enumerated;
	structure_materials(structure_bsp, &known, TRUE);
	structure_bsp_cache_materials(stand_ins_visit, &known);
}

/* the last round's materials given their own shaders back, and its keys
counted as drawn */
static void stand_ins_restore(struct enumerated_materials *enumerated)
{
	short index;

	for (index = 0; index < stand_ins.count; index++)
	{
		stand_ins.materials[index]->shader.index = stand_ins.shader_indices[index];
		stand_ins.materials[index]->permutation_index = stand_ins.permutation_indices[index];
	}
	if (stand_ins.unlit)
		stand_ins.unlit->bitmap_index = stand_ins.unlit_bitmap_index;
	stand_ins.unlit = NULL;
	for (index = 0; index < stand_ins.keys.count; index++)
		materials_add(enumerated, stand_ins.keys.keys[index]);
	stand_ins.count = 0;
	stand_ins.keys.count = 0;
}

/* the next round's materials standing in for the keys still to draw: TRUE,
or FALSE with none left (or none that can stand in for them) */
static boolean stand_ins_next(struct structure_bsp *structure_bsp, struct enumerated_materials *enumerated)
{
	short lightmap_index, material_index, index;

	stand_ins_restore(enumerated);
	if (!structure_bsp || !enumerated || stand_ins.round >= MAXIMUM_STAND_IN_ROUNDS || !stand_ins.pending.count)
		return FALSE;
	stand_ins.round++;
	/* (a lightmap drawn without its picture, if a key still to draw has
	none) */
	for (index = 0; index < stand_ins.pending.count && (stand_ins.pending.keys[index] & 0x10000UL); index++)
		;
	if (index < stand_ins.pending.count)
	{
		for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count && !stand_ins.unlit; lightmap_index++)
		{
			struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
				struct structure_lightmap);

			if (lightmap->bitmap_index != NONE && lightmap->materials.count)
				stand_ins.unlit = lightmap;
		}
		if (stand_ins.unlit)
		{
			stand_ins.unlit_bitmap_index = stand_ins.unlit->bitmap_index;
			stand_ins.unlit->bitmap_index = NONE;
		}
	}
	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
			struct structure_lightmap);

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			unsigned long key = material_key(lightmap, material);

			if (material->surface_count <= 0 || material->first_surface_index < 0 ||
				material->first_surface_index >= structure_bsp->surfaces.count)
			{
				continue;
			}
			for (index = 0; index < stand_ins.pending.count; index++)
			{
				unsigned long pending = stand_ins.pending.keys[index];

				if (MATERIAL_KEY_CLASS(pending) != MATERIAL_KEY_CLASS(key))
					continue;
				stand_ins.materials[stand_ins.count] = material;
				stand_ins.shader_indices[stand_ins.count] = material->shader.index;
				stand_ins.permutation_indices[stand_ins.count] = material->permutation_index;
				stand_ins.count++;
				material->shader.index = stand_ins.pending_shader_indices[index];
				material->permutation_index = 0;
				materials_add(&stand_ins.keys, pending);
				stand_ins.pending.count--;
				stand_ins.pending.keys[index] = stand_ins.pending.keys[stand_ins.pending.count];
				stand_ins.pending_shader_indices[index] = stand_ins.pending_shader_indices[stand_ins.pending.count];
				break;
			}
		}
	}
	return stand_ins.count != 0;
}

static short pass_fog(void)
{
	return (short)(enumerate.pass / NUMBER_OF_PARTS % NUMBER_OF_FOGS);
}

/* the renderer's dynamic lights the stand-in lights (display.dynamic_lights
"enhanced") */
static boolean pass_dynamic_lights(void)
{
	return enumerate.pass >= NUMBER_OF_FOGS * NUMBER_OF_PARTS * NUMBER_OF_STENCIL_CONTEXTS;
}

static short passes(void)
{
	return (short)((halo_dynamic_lights_enhanced() ? MAXIMUM_LIGHTINGS : 1) * NUMBER_OF_FOGS * NUMBER_OF_PARTS *
		NUMBER_OF_STENCIL_CONTEXTS);
}

static boolean pass_stencil_reject(void)
{
	return enumerate.pass / (NUMBER_OF_FOGS * NUMBER_OF_PARTS) % NUMBER_OF_STENCIL_CONTEXTS != 0;
}

static short pass_part(void)
{
	return (short)(enumerate.pass % NUMBER_OF_PARTS);
}

/* ---------- the fogs */

static void fog_set(short fog)
{
	struct render_fog *to = &render.fog;
	real_point3d origin = { 0.0f, 0.0f, 0.0f };
	real_vector3d up = { 0.0f, 0.0f, 1.0f };

	*to = enumerate.own_fog;
	if (fog == _fog_own)
		return;
	to->screen = NULL;
	to->atmospheric_maximum_distance = 0.0f;
	to->planar_mode = _planar_fog_off;
	if (fog == _fog_atmospheric || fog == _fog_both)
	{
		to->atmospheric_minimum_distance = 1.0f;
		to->atmospheric_maximum_distance = 100.0f;
		/* (the decals' fog drawn as opaque at 1: rasterizer_xbox_decals.c) */
		to->atmospheric_maximum_density = fog == _fog_both ? 1.0f : 0.5f;
	}
	if (fog == _fog_planar || fog == _fog_planar_full || fog == _fog_both)
	{
		to->planar_mode = fog == _fog_planar_full ? _planar_fog_fully_fogged : _planar_fog_normal;
		plane3d_from_point_and_normal(&to->plane, &origin, &up);
		to->planar_maximum_density = 0.5f;
		to->planar_maximum_distance = 50.0f;
		to->planar_maximum_depth = 10.0f;
	}
}

/* ---------- the frame */

void render_enumerate_new_map(void)
{
	enumerate.new_map = TRUE;
}

/* the world's part, or the mirror's */
static boolean pass_world(void)
{
	return pass_part() == _part_world || pass_part() == _part_mirror;
}

/* the passes a new structure of the map is drawn in: the world's (its
surfaces), not its objects'; a mirror's for a structure with one */
static boolean pass_drawn(void)
{
	if (pass_part() == _part_mirror)
		return enumerate.mirrors;
	return enumerate.everything || pass_part() == _part_world;
}

short render_enumerate_target(void)
{
	return pass_part() == _part_mirror ? _render_target_secondary : _render_target_primary;
}

boolean render_enumerate_has_mirror(void)
{
	return enumerate.mirrors && pass_part() != _part_mirror;
}

boolean render_enumerate_pending(boolean scope)
{
	static int setting = -1;
	short structure_bsp_index = global_structure_bsp_index_get();
	short view = scope ? 1 : 0;

	if (setting < 0)
		setting = config_boolean("display.prepare_shaders") || *config_string("debug.render_manifest");
	/* (nor a map of the menus' pictures: its one frame makes only the
	pipelines it draws with, menu_pictures_render.c) */
	if (!setting || structure_bsp_index == NONE || structure_bsp_index >= LONG_BITS || menu_pictures_preparing())
		return FALSE;
	if (enumerate.new_map)
	{
		enumerate.structure_bsps_enumerated[0] = NULL;
		enumerate.structure_bsps_enumerated[1] = NULL;
		enumerate.new_map = FALSE;
	}
	if (!enumerate.structure_bsps_enumerated[view])
	{
		enumerate.structure_bsps_enumerated[view] =
			map_structures_enumerated(view, &enumerate.materials_enumerated[view]);
	}
	if (TEST_FLAG(*enumerate.structure_bsps_enumerated[view], structure_bsp_index))
		return FALSE;
	/* A later structure whose materials an earlier one's enumeration drew
	has its pipelines: nothing is drawn as the game switches to it (a load
	zone), and its textures load as its surfaces are seen, as the Xbox's. */
	if (*enumerate.structure_bsps_enumerated[view] && global_structure_bsp_get() &&
		!structure_materials(global_structure_bsp_get(), enumerate.materials_enumerated[view], FALSE))
	{
		SET_FLAG(*enumerate.structure_bsps_enumerated[view], structure_bsp_index, TRUE);
		platform_log("render: structure %d%s: its materials' pipelines made before", structure_bsp_index,
			scope ? " (scope)" : "");
		return FALSE;
	}
	return TRUE;
}

boolean render_enumerate_begin(boolean mirrors, boolean scope)
{
	short structure_bsp_index = global_structure_bsp_index_get();
	short view = scope ? 1 : 0;

	if (!render_enumerate_pending(scope))
		return FALSE;
	/* A map's models and effects once, each structure once, in each view. */
	enumerate.everything = *enumerate.structure_bsps_enumerated[view] == 0;
	enumerate.map_drawn = enumerate.everything;
	/* (and with them, its other structures' materials) */
	if (enumerate.everything)
		stand_ins_collect(global_structure_bsp_get(), enumerate.materials_enumerated[view]);
	else
		stand_ins.pending.count = stand_ins.round = 0;
	enumerate.scope = scope;
	enumerate.interface_drawn = FALSE;
	enumerate.structure_bsp_index = structure_bsp_index;
	SET_FLAG(*enumerate.structure_bsps_enumerated[view], structure_bsp_index, TRUE);
	enumerate.mirrors = mirrors && structure_visibility_port_has_mirrors();
	enumerate.own_fog = render.fog;
	halo_load_trace("enumerate.begin", NULL);
	enumerate.started = system_milliseconds();
	enumerate.started_cpu = halo_thread_cpu_seconds();
	enumerate.pass = 0;
	enumerate.lens_flare_count = rasterizer_lens_flares_port_count();
	while (!pass_drawn())
		enumerate.pass++;
	fog_set(pass_fog());
	render_enumerating = TRUE;
	halo_gpu_prepare(enumerate.everything);
	texture_cache_preparation_begin();
	return TRUE;
}

boolean render_enumerate_next(void)
{
	double waited, cpu;

	do
		enumerate.pass++;
	while (enumerate.pass < passes() && !pass_drawn());
	if (enumerate.pass < passes())
	{
		fog_set(pass_fog());
		return TRUE;
	}
	if (!stand_ins.round)
	{
		if (!enumerate.scope)
			enumerate.interface_pending = enumerate.everything && !enumerate.interface_drawn;
		/* (its materials' pipelines made: a later structure with these alone
		draws nothing) */
		if (global_structure_bsp_get())
			structure_materials(global_structure_bsp_get(), enumerate.materials_enumerated[enumerate.scope ? 1 : 0], TRUE);
	}
	/* the map's other structures' materials, in the world's passes alone */
	if (stand_ins_next(global_structure_bsp_get(), enumerate.materials_enumerated[enumerate.scope ? 1 : 0]))
	{
		enumerate.everything = FALSE;
		enumerate.mirrors = FALSE;
		enumerate.pass = 0;
		while (!pass_drawn())
			enumerate.pass++;
		fog_set(pass_fog());
		return TRUE;
	}
	render.fog = enumerate.own_fog;
	rasterizer_lens_flares_port_truncate(enumerate.lens_flare_count);
	render_enumerating = FALSE;
	halo_load_trace("enumerate.draws.end", NULL);
	cpu = halo_thread_cpu_seconds();
	cpu = cpu < 0.0 || enumerate.started_cpu < 0.0 ? -1.0 : cpu - enumerate.started_cpu;
	/* (the loading screen's end: the shaders and pipelines made) */
	waited = halo_gpu_prepared();
	halo_load_trace("enumerate.prepared.end", NULL);
	/* These draws have reached the host. Advance the texture cache as a
	completed frame does, so the next view and HUD can load their real
	bitmaps instead of defaults when this enumeration filled the cache.
	Never unlock between a draw's texture stages. */
	texture_cache_idle();
	platform_log("render: %s structure %d%s: its shaders and pipelines made (drawn in %d passes and %d rounds of the "
		"other structures' materials, %d left, in %lu ms, %.0f ms of the CPU's, %.0f ms waited for the GPU's)",
		enumerate.map_drawn ? "the map's and its" : "its", enumerate.structure_bsp_index,
		enumerate.scope ? " (scope)" : "", passes(), stand_ins.round, stand_ins.pending.count,
		system_milliseconds() - enumerate.started, cpu < 0.0 ? -1.0 : cpu * 1000.0,
		waited * 1000.0);
	{
		short index;

		for (index = 0; index < stand_ins.pending.count; index++)
		{
			platform_log("render: no material of structure %d stands in for %s (lightmap %d, flags %lx)",
				enumerate.structure_bsp_index, tag_get_name(stand_ins.pending_shader_indices[index]),
				(int)(stand_ins.pending.keys[index] >> 16 & 1), stand_ins.pending.keys[index] >> 17);
		}
	}
	return FALSE;
}

/* ---------- the structure: a surface of every material of every
lightmap, which every pass then draws as it would the visible ones; one of
each key (material_key), and not those whose pipelines an earlier
structure's enumeration made */

void render_enumerate_structure(void)
{
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	struct enumerated_materials *enumerated = enumerate.materials_enumerated[enumerate.scope ? 1 : 0];
	unsigned long mirror = pass_part() == _part_mirror ? MATERIAL_KEY_MIRROR : 0;
	static struct enumerated_materials drawn;
	short lightmap_index, material_index;

	if (!render_enumerating || !structure_bsp || !pass_world())
		return;
	drawn.count = 0;
	memset(render.environment_surface_flags, 0, sizeof(render.environment_surface_flags));
	render.environment_surface_count = 0;
	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
			struct structure_lightmap);

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			long surface = material->first_surface_index;
			unsigned long key = material_key(lightmap, material) | mirror;

			if (material->surface_count <= 0 || surface < 0 || surface >= structure_bsp->surfaces.count ||
				surface >= MAXIMUM_SURFACES_PER_STRUCTURE * LONG_BITS)
			{
				continue;
			}
			/* (the keys drawn, at most as many as are kept: past them, every
			material is drawn) */
			if (materials_have(enumerated, key) || materials_have(&drawn, key))
				continue;
			materials_add(&drawn, key);
			if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, surface))
			{
				BIT_VECTOR_SET_FLAG(render.environment_surface_flags, surface, TRUE);
				render.environment_surface_count++;
			}
		}
	}
}

/* ---------- the lights: every light tag's, before the camera, lighting
the models and the structure's surfaces */

static void lights_submit(void)
{
	struct tag_iterator iterator;
	long tag_index;
	short index;

	enumerate.light_count = 0;
	tag_iterator_new(&iterator, LIGHT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE && rasterizer_lights.light_count < MAXIMUM_LIGHTS_PER_WINDOW)
	{
		struct rasterizer_light_submit_parameters light;
		long light_index;

		memset(&light, 0, sizeof(light));
		light.definition = tag_get(LIGHT_DEFINITION_TAG, tag_index);
		light.position.x = render.camera.position.x + render.camera.forward.i * 2.0f;
		light.position.y = render.camera.position.y + render.camera.forward.j * 2.0f;
		light.position.z = render.camera.position.z + render.camera.forward.k * 2.0f;
		light.forward = render.camera.forward;
		light.up = render.camera.up;
		light.color.red = light.color.green = light.color.blue = 1.0f;
		light.radius = 10.0f;
		light_index = rasterizer_light_submit(&light);
		if (light_index != NONE)
			enumerate.light_indices[enumerate.light_count++] = light_index;
	}
	/* (the renderer's: these and the window's, or none) */
	if (halo_dynamic_lights_enhanced())
	{
		long count = rasterizer_lights.light_count;

		/* (the first a projectile's, drawn with a glow) */
		for (index = 0; index < enumerate.light_count; index++)
		{
			dynamic_lights_note(enumerate.light_indices[index], index == 0 ?
				HALO_DYNAMIC_LIGHT_PROJECTILE | HALO_DYNAMIC_LIGHT_HALO : 0, 0.0f, 1.0f,
				&rasterizer_lights.lights[enumerate.light_indices[index]].position);
		}
		if (!pass_dynamic_lights())
			rasterizer_lights.light_count = 0;
		dynamic_lights_submit();
		rasterizer_lights.light_count = count;
	}
}

void render_enumerate_lights(boolean specular)
{
	short index;

	if (!render_enumerating || !pass_world())
		return;
	if (specular)
		rasterizer_environment_specular_lights_begin();
	else
		rasterizer_environment_diffuse_lights_begin();
	for (index = 0; index < enumerate.light_count; index++)
	{
		if (specular)
			structure_render_specular_light(enumerate.light_indices[index], &render.camera.position, 10.0f, 0, NULL);
		else
			structure_render_diffuse_light(enumerate.light_indices[index], &render.camera.position, 10.0f, 0, NULL);
	}
	if (specular)
		rasterizer_environment_specular_lights_end();
	else
		rasterizer_environment_diffuse_lights_end();
}

/* ---------- the models: every geometry of every model (each of its
levels of detail and its regions' permutations: no two of them the same),
in each lighting, function and colour the code tells apart; its shader
permutation the part's own (forced_shader_permutation_index 0) or one
forced */

/* a model's lighting, functions and colours */
struct model_state
{
	/* a point light (the first light's), the functions at 1 (not 0), the
	change colours white or grey (not black), far from the camera (its
	reflections not cut off) */
	boolean lit;
	boolean functions;
	short colours;
	boolean far;
};

enum
{
	_colours_black,
	_colours_white,
	_colours_grey,
};

/* the states the models are drawn in: the code tells apart a point light
or none, functions (self-illumination) or none, change colours white or
not, reflections cut off or not (rasterizer_xbox_models.c's vertex shader
permutations) */
static struct model_state const model_states[] =
{
	{ FALSE, FALSE, _colours_black, FALSE },
	{ FALSE, FALSE, _colours_white, FALSE },
	{ FALSE, TRUE, _colours_grey, TRUE },
	{ TRUE, FALSE, _colours_white, FALSE },
	{ TRUE, TRUE, _colours_grey, TRUE },
};

/* a state's functions, colours and lighting, as the window's transparent
geometry keeps them until it is drawn */
static struct
{
	real function_values[MAXIMUM_FUNCTION_VALUES_PER_MODEL];
	real_rgb_color change_colors[MAXIMUM_CHANGE_COLORS_PER_MODEL];
	struct render_lighting lighting;
} model_state_data[NUMBEROF(model_states)];

static void model_draw(long model_index, short state_index, struct render_model_effect const *effect,
	short forced_shader_permutation_index, unsigned long flags)
{
	struct model *model = model_definition_get(model_index);
	struct model_state const *state = &model_states[state_index];
	unsigned long seen[BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_ENUMERATED_GEOMETRIES)];
	real *function_values = model_state_data[state_index].function_values;
	real_rgb_color *change_colors = model_state_data[state_index].change_colors;
	struct render_lighting *lighting = &model_state_data[state_index].lighting;
	real_point3d centroid;
	short most_permutations = 1, permutation, region_index, level, index;
	short region_count = (short)MIN(model->regions.count, MAXIMUM_REGIONS_PER_MODEL);

	memset(lighting, 0, sizeof(*lighting));
	lighting->ambient_color.red = lighting->ambient_color.green = lighting->ambient_color.blue = 0.5f;
	lighting->shadow_vector.k = -1.0f;
	if (state->lit && enumerate.light_count > 0)
	{
		lighting->point_light_count = 1;
		lighting->point_light_indices[0] = enumerate.light_indices[0];
	}
	for (index = 0; index < MAXIMUM_FUNCTION_VALUES_PER_MODEL; index++)
		function_values[index] = state->functions ? 1.0f : 0.0f;
	for (index = 0; index < MAXIMUM_CHANGE_COLORS_PER_MODEL; index++)
	{
		change_colors[index].red = change_colors[index].green = change_colors[index].blue =
			state->colours == _colours_white ? 1.0f : state->colours == _colours_grey ? 0.5f : 0.0f;
	}
	centroid = render.camera.position;
	if (state->far)
	{
		centroid.x += render.camera.forward.i * 1000.0f;
		centroid.y += render.camera.forward.j * 1000.0f;
		centroid.z += render.camera.forward.k * 1000.0f;
	}

	for (region_index = 0; region_index < region_count; region_index++)
	{
		struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);

		if (region->permutations.count > most_permutations)
			most_permutations = (short)region->permutations.count;
	}
	memset(seen, 0, sizeof(seen));
	for (level = 0; level < NUMBER_OF_DETAIL_LEVELS_PER_MODEL; level++)
	{
		for (permutation = 0; permutation < most_permutations; permutation++)
		{
			char permutations[MAXIMUM_REGIONS_PER_MODEL];
			boolean new_geometry = FALSE;

			memset(permutations, 0, sizeof(permutations));
			for (region_index = 0; region_index < region_count; region_index++)
			{
				struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index,
					struct model_region);
				struct model_region_permutation *chosen;
				short geometry_index;

				permutations[region_index] = (char)(permutation < region->permutations.count ? permutation : 0);
				if (!region->permutations.count)
					continue;
				chosen = TAG_BLOCK_GET_ELEMENT(&region->permutations, permutations[region_index],
					struct model_region_permutation);
				geometry_index = chosen->geometry_indices[level];
				if (geometry_index >= 0 && geometry_index < MAXIMUM_ENUMERATED_GEOMETRIES &&
					!BIT_VECTOR_TEST_FLAG(seen, geometry_index))
				{
					BIT_VECTOR_SET_FLAG(seen, geometry_index, TRUE);
					new_geometry = TRUE;
				}
			}
			if (!new_geometry)
				continue;
			rasterizer_debug_options.debug_model_lod = level;
			rasterizer_transparent_geometry_flush(TRANSPARENT_GROUPS_ROOM);
			/* (each model's shader made whole: a model's draw sets the part of
			its pixel shader the models' share once a run of models,
			rasterizer_xbox_models.c) */
			if (!TEST_FLAG(flags, _render_model_shadow_bit))
				rasterizer_models_begin(TEST_FLAG(flags, _render_model_immediate_bit));
			render_model(model_index, 100000.0f, NULL, permutations, change_colors, function_values, lighting,
				&centroid, 1.0f, effect, effect && effect->type != _render_model_effect_type_none ?
					effect->source_object_index : NONE, forced_shader_permutation_index, flags);
			if (!TEST_FLAG(flags, _render_model_shadow_bit))
				rasterizer_models_end();
		}
	}
}

static void models_draw(void)
{
	struct tag_iterator iterator;
	long model_index;
	short state;

	tag_iterator_new(&iterator, MODELS_GROUP_TAG);
	while ((model_index = tag_iterator_next(&iterator)) != NONE)
	{
		for (state = 0; state < (short)NUMBEROF(model_states); state++)
			model_draw(model_index, state, NULL, 0, 0);
	}
}

/* every object's model with its own effects: its modifier shader (a
shield's), its camouflage (a unit's), its transparency drawn as it
occludes itself, its forced shader permutation */
static void object_effects_draw(void)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, OBJECT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct object_definition *definition = object_definition_get(tag_index);
		long model_index = definition->object.model.index;
		struct render_model_effect effect;
		short state;

		if (model_index == NONE)
			continue;
		for (state = 0; state < (short)NUMBEROF(model_states); state++)
		{
			memset(&effect, 0, sizeof(effect));
			effect.type = _render_model_effect_type_none;
			effect.source_object_index = STAND_IN_OBJECT_INDEX;
			effect.source_object_centroid = render.camera.position;
			if (definition->object.modifier_shader.index != NONE)
			{
				struct shader *modifier = shader_definition_get(definition->object.modifier_shader.index);

				if (shader_type_is_valid_for_modifier(modifier->base.type))
				{
					effect.modifier_shader = modifier;
					model_draw(model_index, state, &effect, 0, 0);
					effect.modifier_shader = NULL;
				}
			}
			if (TEST_FLAG(_object_mask_unit, FLAG(definition->object.type)))
			{
				effect.type = _render_model_effect_type_active_camouflage;
				effect.intensity = 0.5f;
				effect.parameter = 0.0f;
				model_draw(model_index, state, &effect, 0, 0);
				effect.parameter = 1.0f;
				model_draw(model_index, state, &effect, 0, 0);
				effect.type = _render_model_effect_type_none;
				effect.intensity = effect.parameter = 0.0f;
			}
			if (TEST_FLAG(definition->object.flags, _object_transparency_self_occludes_bit))
			{
				effect.type = _render_model_effect_type_modifier;
				model_draw(model_index, state, &effect, 0, 0);
				effect.type = _render_model_effect_type_none;
			}
			if (definition->object.forced_shader_permutation_index > 0)
				model_draw(model_index, state, NULL, definition->object.forced_shader_permutation_index, 0);
		}
	}
}

/* first person: the hands' model and every weapon's first-person model,
as the first-person weapon draws them, plain, camouflaged and with every
unit's modifier shader (its shield's flare on the arms) */
static void first_person_model_draw(long model_index)
{
	unsigned long flags = FLAG(_render_model_first_person_bit);
	struct render_model_effect effect;
	struct tag_iterator iterator;
	long tag_index;
	short state;

	if (model_index == NONE)
		return;
	for (state = 0; state < (short)NUMBEROF(model_states); state++)
	{
		model_draw(model_index, state, NULL, 0, flags);
		memset(&effect, 0, sizeof(effect));
		effect.type = _render_model_effect_type_active_camouflage;
		effect.source_object_index = STAND_IN_OBJECT_INDEX;
		effect.source_object_centroid = render.camera.position;
		effect.intensity = 0.5f;
		model_draw(model_index, state, &effect, 0, flags);
		effect.parameter = 1.0f;
		model_draw(model_index, state, &effect, 0, flags);
		memset(&effect, 0, sizeof(effect));
		effect.source_object_index = STAND_IN_OBJECT_INDEX;
		tag_iterator_new(&iterator, UNIT_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct object_definition *definition = object_definition_get(tag_index);

			if (definition->object.modifier_shader.index != NONE)
			{
				effect.modifier_shader = shader_definition_get(definition->object.modifier_shader.index);
				if (shader_type_is_valid_for_modifier(effect.modifier_shader->base.type))
					model_draw(model_index, state, &effect, 0, flags);
			}
		}
	}
}

static void first_person_models_draw(void)
{
	struct game_globals *globals = scenario_get_game_globals();
	struct tag_iterator iterator;
	long tag_index;
	short index;

	for (index = 0; globals && index < (short)globals->first_person_interface.count; index++)
	{
		struct game_globals_first_person_interface *interface = TAG_BLOCK_GET_ELEMENT(
			&globals->first_person_interface, index, struct game_globals_first_person_interface);

		first_person_model_draw(interface->hands.index);
	}
	tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		first_person_model_draw(weapon_definition_get(tag_index)->weapon.interface_definition.first_person_model.index);
}

/* every sky's model, drawn as render_sky draws it: at once (not queued:
render_model's immediate flag) as the sky, its regions' scales its
functions */
static void skies_draw(void)
{
	struct tag_iterator iterator;
	long tag_index;
	short state;

#ifdef HALO_VR
	/* Match render_sky's infinite-distance transforms and depth clamp.
	The current camera may not see any sky during preparation. */
	vr_render_sky(TRUE);
#endif
	tag_iterator_new(&iterator, SKY_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		/* (a sky's model is its tag's first field: render_sky.c) */
		long model_index = ((struct tag_reference *)tag_get(SKY_DEFINITION_TAG, tag_index))->index;

		if (model_index == NONE)
			continue;
		for (state = 0; state < (short)NUMBEROF(model_states); state++)
			model_draw(model_index, state, NULL, 0, FLAG(_render_model_immediate_bit));
	}
#ifdef HALO_VR
	vr_render_sky(FALSE);
#endif
}

void render_enumerate_models(void)
{
	short saved_lod = rasterizer_debug_options.debug_model_lod;

	if (!render_enumerating)
		return;
	/* rasterizer_xbox.c: NONE (0) or REJECT (2), as model_end leaves it.
	This also applies to the structure's draws after these models. */
	rasterizer_set_stencil_mode(pass_stencil_reject() ? 2 : 0);
	lights_submit();
	if (!enumerate.everything)
		return;
	/* (every lens flare tag's, tested and drawn by every pass after) */
	if (enumerate.pass == 0)
		rasterizer_lens_flares_port_enumerate(TRUE);
	switch (pass_part())
	{
	case _part_world:
	case _part_mirror:
		models_draw();
		skies_draw();
		break;
	case _part_object_effects:
		object_effects_draw();
		break;
	case _part_first_person:
		first_person_models_draw();
		break;
	}
	rasterizer_debug_options.debug_model_lod = saved_lod;
}

/* ---------- the shadows: every model's, cast on the structure's every
surface (structure_render_shadow draws them while enumerating); a new
structure of the map, one model's */

void render_enumerate_shadows(void)
{
	struct tag_iterator iterator;
	long model_index;
	short saved_lod = rasterizer_debug_options.debug_model_lod;
	real_rgb_color shadow_color = { 0.5f, 0.5f, 0.5f };
	real_plane3d planes[6];
	real_rectangle3d bounds;
	real_matrix4x3 shadow_matrix;
	real_vector3d right;

	if (!render_enumerating || !pass_world())
		return;
	memset(planes, 0, sizeof(planes));
	memset(&bounds, 0, sizeof(bounds));
	right = render.camera.forward;
	matrix4x3_from_point_and_vectors(&shadow_matrix, &render.camera.position, &right, global_down3d);
	rasterizer_environment_shadows_begin();
	tag_iterator_new(&iterator, MODELS_GROUP_TAG);
	while ((model_index = tag_iterator_next(&iterator)) != NONE)
	{
		real radius = 1.0f;

		if (!rasterizer_environment_shadow_begin(STAND_IN_OBJECT_INDEX, &shadow_matrix, &shadow_color, 1.0f, &radius))
			continue;
		model_draw(model_index, 0, NULL, 0, FLAG(_render_model_shadow_bit));
		structure_render_shadow(&render.camera.position, radius, &bounds, 0, planes);
		rasterizer_environment_shadow_end();
		/* (a new structure of the map: a shadow cast on it, the models' made) */
		if (!enumerate.everything)
			break;
	}
	rasterizer_environment_shadows_end();
	/* Shadows leave stencil rejection enabled. The world can also follow
	a frame with no shadow or first-person weapon, so retain the selected
	preparation context for the lights and transparent geometry after it. */
	rasterizer_set_stencil_mode(pass_stencil_reject() ? 2 : 0);
	rasterizer_debug_options.debug_model_lod = saved_lod;
}

/* ---------- the lens flares: every lens flare tag's (submitted with the
first pass's models), made visible before they are drawn */

void render_enumerate_lens_flares(void)
{
	if (render_enumerating && enumerate.everything)
		rasterizer_lens_flares_port_enumerate(FALSE);
}

/* ---------- the decals: every decal's, in every layer
(rasterizer_xbox_decals.c), and every detail object collection's, in each
fog */

void render_enumerate_decals(void)
{
	if (!render_enumerating || !enumerate.everything || pass_part() != _part_world)
		return;
	rasterizer_decals_port_enumerate(pass_stencil_reject() ? 2 : 0);
	/* (and every detail object collection's: rasterizer_xbox_detail_objects.c) */
	rasterizer_detail_objects_port_enumerate();
}

/* ---------- the effects: every particle's, contrail's and particle
system state's sprite, a sprite before the camera, which the transparent geometry's pass
then draws as it would theirs */

static void effect_sprite(long bitmap_group_index, struct shader_effect_definition const *shader)
{
	/* (not on the screen: its sprites' vertices are the dynamic screen
	type's, which has no buffer in a map: render_sprite.c) */
	unsigned long flags = pass_part() == _part_first_person ? FLAG(_build_sprites_first_person_bit) : 0;
	real_argb_color color = { 1.0f, 1.0f, 1.0f, 1.0f };
	struct build_sprite_data data;
	real_point3d origin;

	struct bitmap_group *bitmap_group;
	short sequence_index;

	if (bitmap_group_index == NONE)
		return;
	/* (the first sequence that has a sprite: build_sprite draws none of
	another) */
	bitmap_group = bitmap_group_get(bitmap_group_index);
	for (sequence_index = 0; sequence_index < bitmap_group->sequences.count; sequence_index++)
	{
		struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(&bitmap_group->sequences, sequence_index,
			struct bitmap_group_sequence);

		if (sequence->first_bitmap_index != NONE && sequence->sprites.count > 0)
			break;
	}
	if (sequence_index == bitmap_group->sequences.count)
		return;
	origin.x = render.camera.position.x + render.camera.forward.i * 2.0f;
	origin.y = render.camera.position.y + render.camera.forward.j * 2.0f;
	origin.z = render.camera.position.z + render.camera.forward.k * 2.0f;
	rasterizer_transparent_geometry_flush(TRANSPARENT_GROUPS_ROOM);
	build_sprites_begin(&data, 1, bitmap_group_index, shader, flags);
	/* (small: no more than ten sprites over half the screen are drawn,
	render_sprite.c) */
	build_sprite(&data, _build_sprite_normal, sequence_index, 0, &origin, &render.camera.forward, 0.0f, 0.001f, &color,
		1.0f, 0);
	build_sprites_end(&data);
#ifdef HALO_VR
	/* the VR mode's eyes draw large sprites thick (render_sprite.c): their
	pipeline too, and each of the bitmaps' thickness, which the renderer
	bakes as it first draws one (each sprite of a bitmap not yet drawn) */
	if (vr_render_eye_pass() && !TEST_FLAG(flags, _build_sprites_first_person_bit))
	{
		short bitmap_count = (short)MIN(bitmap_group->bitmaps.count, 64);
		unsigned long drawn[2] = { 0, 0 };

		render_sprite_vr_thick_force(TRUE);
		for (sequence_index = 0; sequence_index < bitmap_group->sequences.count; sequence_index++)
		{
			struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(&bitmap_group->sequences, sequence_index,
				struct bitmap_group_sequence);
			short sprite_index;

			if (sequence->first_bitmap_index == NONE)
				continue;
			for (sprite_index = 0; sprite_index < sequence->sprites.count; sprite_index++)
			{
				short bitmap_index = TAG_BLOCK_GET_ELEMENT(&sequence->sprites, sprite_index,
					struct bitmap_group_sprite)->bitmap_index;

				if (bitmap_index < 0 || bitmap_index >= bitmap_count || (drawn[bitmap_index >> 5] & (1ul << (bitmap_index & 31))))
					continue;
				drawn[bitmap_index >> 5] |= 1ul << (bitmap_index & 31);
				rasterizer_transparent_geometry_flush(TRANSPARENT_GROUPS_ROOM);
				build_sprites_begin(&data, 1, bitmap_group_index, shader, flags);
				build_sprite(&data, _build_sprite_normal, sequence_index, sprite_index, &origin, &render.camera.forward,
					0.0f, 0.001f, &color, 1.0f, 0);
				build_sprites_end(&data);
			}
		}
		render_sprite_vr_thick_force(FALSE);
	}
#endif
}

/* Light volumes use the widget sprite shader with depth testing, unlike
lens flares. Their animated objects need not be visible in the first frame. */
static void light_volumes_draw(void)
{
	struct tag_iterator iterator;
	long tag_index;
	real_point3d origin;

	point_from_line3d(&render.camera.position, &render.camera.forward, 2.0f, &origin);
	rasterizer_set_stencil_mode(pass_stencil_reject() ? 2 : 0);
	tag_iterator_new(&iterator, 'mgs2');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct light_volume_definition *definition = light_volume_definition_get(tag_index);

		rasterizer_widget_begin(_widget_type_internal_sprite, FLAG(_rasterizer_widget_z_enable_bit));
		/* The nonblocking texture API returns true while still loading. */
		if (!rasterizer_widget_set_texture(0, definition->map.index, definition->sequence_index))
		{
			rasterizer_widget_batch_sprites(TRUE);
			rasterizer_widget_draw_sprite3d(&origin, 0.01f, NULL, 0.0f, 0x80ffffff);
			rasterizer_widget_batch_sprites(FALSE);
		}
		rasterizer_widget_end();
	}
}

void render_enumerate_effects(void)
{
	struct tag_iterator iterator;
	long tag_index;

	if (!render_enumerating || !enumerate.everything || pass_part() == _part_object_effects)
		return;
	light_volumes_draw();
	tag_iterator_new(&iterator, PARTICLE_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct particle_definition *definition = particle_definition_get(tag_index);

		effect_sprite(definition->bitmap.index, &definition->shader);
	}
	weather_particle_systems_port_enumerate(effect_sprite);
	/* (a contrail's shader is drawn as an effect's: rasterizer_xbox_transparent_geometry.c) */
	tag_iterator_new(&iterator, CONTRAIL_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct contrail_definition *definition = contrail_definition_get(tag_index);

		effect_sprite(definition->bitmap.index, (struct shader_effect_definition const *)&definition->shader);
	}
	tag_iterator_new(&iterator, PARTICLE_SYSTEM_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct particle_system_definition *definition = particle_system_definition_get(tag_index);
		long type_index, state_index;

		for (type_index = 0; type_index < definition->types.count; type_index++)
		{
			struct particle_system_type *type = TAG_BLOCK_GET_ELEMENT(&definition->types, type_index,
				struct particle_system_type);

			for (state_index = 0; state_index < type->particle_states.count; state_index++)
			{
				struct particle_system_type_particle_state *state = TAG_BLOCK_GET_ELEMENT(&type->particle_states,
					state_index, struct particle_system_type_particle_state);

				effect_sprite(state->bitmaps.index, &state->shader);
			}
		}
	}
}

/* ---------- the interface: every HUD's every element (each unit's,
weapon's and grenade's), the motion sensor's blips, every screen flash, the
screen effect with every kind of parameters (a weapon HUD's or a script's:
rasterizer_cinematics.c), every font in each style, a cinematic's bars */

/* (render_cameras.h's, player_effects.c's) */
enum
{
	_render_screen_flash_type_none = 0,
	NUMBER_OF_RENDER_SCREEN_FLASH_TYPES = 7,
};

static void unit_hud_static_draw(struct unit_hud_interface_definition const *definition,
	struct static_hud_element_definition const *element)
{
	if (element->interface_bitmap.index != NONE)
		hud_draw_static_element(render.local_player_index, &definition->absolute_placement, element, 0, 0);
}

static void unit_hud_panel_draw(struct unit_hud_interface_definition const *definition,
	struct metered_panel_definition const *panel)
{
	unit_hud_static_draw(definition, &panel->background);
	if (panel->meter.meter_bitmap.index != NONE)
	{
		hud_draw_meter(render.local_player_index, &definition->absolute_placement, &panel->meter, 128, 128, 0,
			0.0f, 0.0f);
		hud_draw_meter(render.local_player_index, &definition->absolute_placement, &panel->meter, 128, 128,
			FLAG(_hud_draw_flashing_bit), 0.0f, 0.0f);
	}
}

static void unit_huds_draw(void)
{
	struct tag_iterator iterator;
	long hud_index;
	long index;

	tag_iterator_new(&iterator, UNIT_HUD_INTERFACE_DEFINITION_TAG);
	while ((hud_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct unit_hud_interface_definition *definition = unit_hud_interface_definition_get(hud_index);

		unit_hud_static_draw(definition, &definition->background);
		unit_hud_panel_draw(definition, &definition->shield_meter);
		unit_hud_panel_draw(definition, &definition->health_meter);
		unit_hud_static_draw(definition, &definition->motion_sensor.background);
		unit_hud_static_draw(definition, &definition->motion_sensor.foreground);
		for (index = 0; index < definition->auxilary_panel.auxilary_overlays.count; index++)
		{
			struct auxilary_overlay_definition *overlay = TAG_BLOCK_GET_ELEMENT(
				&definition->auxilary_panel.auxilary_overlays, index, struct auxilary_overlay_definition);

			unit_hud_static_draw(definition, &overlay->static_element);
		}
		for (index = 0; index < definition->auxilary_meters.count; index++)
		{
			struct auxilary_meter_definition *meter = TAG_BLOCK_GET_ELEMENT(&definition->auxilary_meters, index,
				struct auxilary_meter_definition);

			unit_hud_panel_draw(definition, &meter->panel);
		}
	}
}

static void motion_sensor_blips_draw(void)
{
	real_point2d center = { 0.0f, 0.0f };
	real_rgb_color color = { 1.0f, 0.5f, 0.0f };

	rasterizer_hud_motion_sensor_blip_begin();
	rasterizer_hud_motion_sensor_blip_draw(&center, 1.0f, 1.0f, &color, FALSE);
	rasterizer_hud_motion_sensor_blip_draw(&center, 1.0f, 1.0f, &color, TRUE);
	rasterizer_hud_motion_sensor_blip_end(&center, 0.0f);
}

static void screen_flashes_draw(void)
{
	struct render_screen_flash saved = global_window_parameters.screen_flash;
	short type;

	for (type = _render_screen_flash_type_none + 1; type < NUMBER_OF_RENDER_SCREEN_FLASH_TYPES; type++)
	{
		global_window_parameters.screen_flash.type = type;
		global_window_parameters.screen_flash.intensity = 0.5f;
		global_window_parameters.screen_flash.color.alpha = global_window_parameters.screen_flash.color.red =
			global_window_parameters.screen_flash.color.green = global_window_parameters.screen_flash.color.blue =
			0.5f;
		rasterizer_screen_flash();
	}
	global_window_parameters.screen_flash = saved;
}

/* a bitmap for a mask or a map: the default 2D texture's first (as a
cinematic's bars draw with it: cinematics.c) */
static struct bitmap_data *stand_in_bitmap(void)
{
	struct game_globals *globals = scenario_get_game_globals();
	struct game_globals_rasterizer_data *data;

	if (!globals || !globals->rasterizer_data.count)
		return NULL;
	data = TAG_BLOCK_GET_ELEMENT(&globals->rasterizer_data, 0, struct game_globals_rasterizer_data);
	if (data->default_textures[0].index == NONE)
		return NULL;
	return bitmap_group_try_and_get_bitmap(data->default_textures[0].index, 0);
}

/* (hud_messaging.c's) */
enum
{
	_text_justification_left = 0,
};

static void fonts_draw(void)
{
	static wchar_t const unicode_string[] = { 'A', 'g', '0', 0 };
	real_argb_color color = { 1.0f, 1.0f, 1.0f, 1.0f };
	rectangle2d bounds;
	struct tag_iterator iterator;
	long font_index;
	short style;

	bounds.x0 = bounds.y0 = 0;
	bounds.x1 = 200;
	bounds.y1 = 50;
	tag_iterator_new(&iterator, FONT_GROUP_TAG);
	while ((font_index = tag_iterator_next(&iterator)) != NONE)
	{
		for (style = _text_style_plain; style < NUMBER_OF_TEXT_STYLES; style++)
		{
			draw_string_set_draw_mode(font_index, style, _text_justification_left, 0, &color);
			rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, unicode_string);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, "Ag0");
		}
	}
}

/* a menu's picture, as its widgets draw them (ui_widget.c: every one over
the plasma, blended by its minimum); the menus are drawn in the frame's
window, but in the VR mode's HUD pass, after its first frame */
static void widget_bitmap_draw(void)
{
	struct bitmap_data *bitmap = stand_in_bitmap();
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	rectangle2d bounds = { 0, 0, 16, 16 };

	if (!bitmap || interface_get_tag_index(_interface_bitmap_iface_map3) == NONE)
		return;
	csmemset(&parameters, 0, sizeof(parameters));
	draw_bitmap_in_rect(bitmap, &bounds, &bounds, NULL, 0xffffffff, &parameters, FALSE);
}

void render_enumerate_interface(void)
{
	if (!render_enumerating || !enumerate.everything || enumerate.pass != 0 || render.local_player_index == NONE)
		return;
	enumerate.interface_drawn = TRUE;
	rasterizer_hud_begin();
	unit_huds_draw();
	hud_weapon_port_enumerate(render.local_player_index);
	motion_sensor_blips_draw();
	rasterizer_hud_end();
	rasterizer_screen_effect_port_enumerate(stand_in_bitmap());
	fonts_draw();
	widget_bitmap_draw();
	/* (a cinematic's letterbox bar: cinematics.c) */
	{
		rectangle2d bar = { 0, 0, 16, 16 };

		draw_quad(&bar, 0xff000000);
	}
}

void render_enumerate_screen_flashes(void)
{
	if (render_enumerating && enumerate.everything && enumerate.pass == 0)
	{
		screen_flashes_draw();
#ifdef HALO_VR
		/* The crosshairs use each eye's view mask, unlike the HUD layer. */
		if (vr_render_eye_pass())
		{
			short eye;

			for (eye = 0; eye < 2; eye++)
			{
				halo_vr_draw_eye(eye);
				rasterizer_hud_begin();
				hud_weapon_port_enumerate_crosshairs();
				rasterizer_hud_end();
			}
			halo_vr_draw_eye(-1);
		}
#endif
	}
}

void render_enumerate_debug(void)
{
	if (render_enumerating && enumerate.everything && enumerate.pass == 0)
		rasterizer_debug_port_enumerate();
}

void render_enumerate_loading_screen(void)
{
	if (!render_enumerating || !enumerate.everything || enumerate.pass != 0)
		return;
#ifdef HALO_VR
	/* Loading uses the ordinary mono target with depth, as the scope does. */
	if (vr_render_eye_pass())
		return;
#endif
	progress_bar_port_enumerate(stand_in_bitmap());
}

/* the VR mode's HUD pass, after the frame's eyes: the interface, where its
draws are (in the HUD's layer), when the eyes' enumeration had none */
void render_enumerate_hud(void)
{
	if (!enumerate.interface_pending)
		return;
	enumerate.interface_pending = FALSE;
	halo_gpu_prepare(FALSE);
	texture_cache_preparation_begin();
	render_enumerating = TRUE;
	enumerate.pass = 0;
	render_enumerate_interface();
	render_enumerating = FALSE;
	halo_gpu_prepared();
	texture_cache_idle();
}
