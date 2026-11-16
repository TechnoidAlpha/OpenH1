/*
WORLD_RT_GAME.C

What display.ambient_occlusion "ray_traced"'s scene (world_rt.c) reads of
the game (world_rt.h's world_rt_source_*): the loaded structure BSP's
collision geometry, as world_scene.c fans it, and a definition's nodes'
collision geometry (the first permutation's BSP of each collision node, the
model's node of the same index, as the collision tests take them:
collision_models.c) and its bounding sphere. Read only; render-only.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"

#include "world_rt.h"
#include "world_scene.h"

#include <string.h>

/* ---------- private code */

/* a definition's collision model (NULL none) */
static struct collision_model const *definition_collision_model(long definition)
{
	struct object_definition const *object_definition;

	if (definition == NONE)
		return NULL;
	object_definition = object_definition_get(definition);
	if (!object_definition || object_definition->object.collision_model.index == NONE)
		return NULL;
	return collision_model_definition_get(object_definition->object.collision_model.index);
}

/* a collision model's node's first BSP (NULL none) */
static struct collision_bsp const *node_collision_bsp(struct collision_model const *collision_model, short node_index)
{
	struct collision_node const *collision_node;

	if (!collision_model || !collision_model->nodes.address || node_index < 0 ||
		node_index >= collision_model->nodes.count)
	{
		return NULL;
	}
	collision_node = TAG_BLOCK_GET_ELEMENT(&collision_model->nodes, node_index, struct collision_node);
	if (!collision_node->bsps.address || collision_node->bsps.count <= 0)
		return NULL;
	return TAG_BLOCK_GET_ELEMENT(&collision_node->bsps, 0, struct collision_bsp);
}

/* ---------- public code */

short world_rt_source_bsp(void)
{
	return global_scenario_try_and_get() && global_collision_bsp ? global_structure_bsp_index : NONE;
}

long world_rt_source_structure(short bsp_index, float *triangles, long maximum)
{
	struct world_scene_triangles found;
	long count;

	if (!world_scene_static_triangles(bsp_index, &found))
		return 0;
	count = found.count;
	if (triangles)
	{
		count = MIN(count, maximum);
		if (count > 0)
			memcpy(triangles, found.positions, (size_t)count * 9 * sizeof(float));
	}
	world_scene_triangles_free(&found);
	return count;
}

short world_rt_source_definition(long definition, float sphere[4], int *item)
{
	struct object_definition const *object_definition;
	struct model const *model;

	sphere[0] = sphere[1] = sphere[2] = sphere[3] = 0.0f;
	*item = FALSE;
	if (definition == NONE)
		return 0;
	object_definition = object_definition_get(definition);
	if (!object_definition || object_definition->object.model.index == NONE)
		return 0;
	model = model_definition_get(object_definition->object.model.index);
	if (!model || !model->nodes.address || model->nodes.count <= 0)
		return 0;
	sphere[0] = object_definition->object.bounding_offset.x;
	sphere[1] = object_definition->object.bounding_offset.y;
	sphere[2] = object_definition->object.bounding_offset.z;
	sphere[3] = object_definition->object.bounding_radius;
	*item = object_definition->object.type == _object_type_weapon || object_definition->object.type == _object_type_equipment ||
		object_definition->object.type == _object_type_garbage;
	return (short)MIN(model->nodes.count, WORLD_RT_NODES);
}

long world_rt_source_node(long definition, short node_index, float *triangles, long maximum)
{
	struct collision_bsp const *bsp = node_collision_bsp(definition_collision_model(definition), node_index);
	long count = bsp ? world_scene_collision_fan(bsp, NULL) : 0;

	if (!triangles || count <= 0)
		return count;
	/* (fanned whole, then: the caller's room is the count it was told) */
	if (count > maximum)
		return 0;
	return world_scene_collision_fan(bsp, triangles);
}

char const *world_rt_source_name(long definition)
{
	return definition == NONE ? "none" : tag_get_name(definition);
}
