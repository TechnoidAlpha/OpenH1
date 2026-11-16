/*
WORLD_PROXIES_GAME.C

What the object proxies (world_proxies.c) read of the game
(world_proxies_source.h): a definition's skeleton and collision vertices
from its tags, the window's objects and their nodes as drawn, the window's
frustum, and debug.world_proxies_view's lines. Read only; render-only.

The objects are world_scene.h's instances (workstream A), their nodes the
object's node matrices, read inside the frame's render_interpolation pose
(render_window): as drawn.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "render/render.h"
#include "render/render_debug.h"
#include "tag_files/tag_groups.h"

#include "object_mesh.h"
#include "world_proxies.h"
#include "world_proxies_source.h"
#include "world_scene.h"
#include "world_sdf.h"

#include <string.h>

int config_boolean(char const *name);
unsigned long config_changes(void);

/* ---------- private code */

static void frame_from_matrix(real_matrix4x3 const *matrix, struct world_proxies_frame *frame)
{
	frame->axes[0][0] = matrix->scale * matrix->forward.i;
	frame->axes[0][1] = matrix->scale * matrix->forward.j;
	frame->axes[0][2] = matrix->scale * matrix->forward.k;
	frame->axes[1][0] = matrix->scale * matrix->left.i;
	frame->axes[1][1] = matrix->scale * matrix->left.j;
	frame->axes[1][2] = matrix->scale * matrix->left.k;
	frame->axes[2][0] = matrix->scale * matrix->up.i;
	frame->axes[2][1] = matrix->scale * matrix->up.j;
	frame->axes[2][2] = matrix->scale * matrix->up.k;
	frame->position[0] = matrix->position.x;
	frame->position[1] = matrix->position.y;
	frame->position[2] = matrix->position.z;
}

/* a node named as a wheel ("front left tire", "wheel"; not a steering
wheel) */
static short node_is_wheel(char const *name)
{
	char lower[TAG_STRING_LENGTH + 1];
	short i;

	for (i = 0; i < TAG_STRING_LENGTH && name[i]; i++)
		lower[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] - 'A' + 'a') : name[i];
	lower[i] = 0;
	return (strstr(lower, "tire") || strstr(lower, "wheel")) && !strstr(lower, "steer");
}

static short shape_class_of(short object_type)
{
	switch (object_type)
	{
	case _object_type_biped:
		return _world_proxies_source_biped;
	case _object_type_vehicle:
		return _world_proxies_source_vehicle;
	case _object_type_weapon:
	case _object_type_equipment:
	case _object_type_garbage:
	case _object_type_scenery:
	case _object_type_machine:
	case _object_type_control:
	case _object_type_light_fixture:
		return _world_proxies_source_box;
	case _object_type_projectile:
	case _object_type_sound_scenery:
		return _world_proxies_source_none;
	default:
		return _world_proxies_source_sphere;
	}
}

/* an object that occludes: drawn, and not a held weapon or equipment (its
holder's shapes cover it, and it would shade the hands holding it) */
static boolean object_occludes(long object_index)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get(object_index);

	if (!object || TEST_FLAG(object->object.flags, _object_invisible_bit))
		return FALSE;
	if (object->object.parent_object_index != NONE &&
		(object->object.type == _object_type_weapon || object->object.type == _object_type_equipment))
	{
		return FALSE;
	}
	return object->object.type != _object_type_projectile && object->object.type != _object_type_sound_scenery;
}

/* a static scenery placement that the drawn field and its pages hold
(or, awaited, that the field on its way will hold): scenery or a device on
no other object, standing where one baked stands */
static int object_static(long object_index, long definition, int awaited)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get(object_index);
	float position[3];

	if (!object || object->object.parent_object_index != NONE || (object->object.type != _object_type_scenery &&
		object->object.type != _object_type_machine && object->object.type != _object_type_control &&
		object->object.type != _object_type_light_fixture))
	{
		return FALSE;
	}
	position[0] = object->object.position.x;
	position[1] = object->object.position.y;
	position[2] = object->object.position.z;
	return awaited ? world_sdf_scenery_awaited(definition, position) : world_sdf_scenery_baked(definition, position);
}

static int object_baked(long object_index, long definition)
{
	return object_static(object_index, definition, FALSE);
}

/* ---------- public code */

int world_proxies_source_baked(long id)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get(id);

	return object && object_baked(id, object->definition_index);
}

int world_proxies_source_solid(long id)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get(id);
	struct object_definition const *definition;
	struct collision_model const *collision;
	long node_index;

	if (!object)
		return FALSE;
	definition = object_definition_get(object->definition_index);
	collision = definition && definition->object.collision_model.index != NONE ?
		collision_model_definition_get(definition->object.collision_model.index) : NULL;
	/* (any node's first BSP with triangles, as the bake folds them) */
	for (node_index = 0; collision && collision->nodes.address && node_index < collision->nodes.count; node_index++)
	{
		struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&collision->nodes, node_index, struct collision_node);

		if (node->bsps.address && node->bsps.count > 0 &&
			world_scene_collision_fan(TAG_BLOCK_GET_ELEMENT(&node->bsps, 0, struct collision_bsp), NULL) > 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

int world_proxies_source_field(long definition, struct world_proxies_source_field *field)
{
	unsigned long atlas[3];
	struct world_sdf_object const *object = world_sdf_object_field(definition, atlas);
	int axis;

	if (!object)
		return FALSE;
	for (axis = 0; axis < 3; axis++)
	{
		field->origin[axis] = object->origin[axis];
		field->place[axis] = object->atlas[axis];
		field->atlas[axis] = atlas[axis];
	}
	field->voxel = object->voxel;
	field->reach = object->reach;
	memcpy(field->bounds, object->bounds, sizeof(field->bounds));
	field->size[0] = object->width;
	field->size[1] = object->height;
	field->size[2] = object->depth;
	return TRUE;
}

/* the bounds of what a model draws opaque, in node 0's space (its default
pose's: each vertex by node 0's default inverse); FALSE when it draws
nothing opaque */
static int model_drawn_bounds(long model_index, float bounds[2][3])
{
	struct object_mesh const *mesh = object_mesh_get_model(model_index);
	long index;
	int axis;

	if (!mesh || mesh->vertex_count <= 0 || mesh->node_count <= 0)
		return FALSE;
	for (index = 0; index < mesh->vertex_count; index++)
	{
		real_point3d point;
		float at[3];

		matrix4x3_transform_point(&mesh->inverse[0], &mesh->vertices[index].position, &point);
		at[0] = point.x;
		at[1] = point.y;
		at[2] = point.z;
		for (axis = 0; axis < 3; axis++)
		{
			if (!index || at[axis] < bounds[0][axis])
				bounds[0][axis] = at[axis];
			if (!index || at[axis] > bounds[1][axis])
				bounds[1][axis] = at[axis];
		}
	}
	return TRUE;
}

int world_proxies_source_definition(long definition, struct world_proxies_source *source)
{
	int collision_any = FALSE;

	struct object_definition const *object_definition;
	struct model const *model;
	struct collision_model const *collision_model;
	short node_index;

	source->shape_class = _world_proxies_source_none;
	source->node_count = 0;
	source->drawn = 0;
	if (definition == NONE)
		return FALSE;
	object_definition = object_definition_get(definition);
	if (!object_definition || object_definition->object.model.index == NONE)
		return FALSE;
	model = model_definition_get(object_definition->object.model.index);
	if (!model || !model->nodes.address || model->nodes.count <= 0)
		return FALSE;
	collision_model = object_definition->object.collision_model.index != NONE ?
		collision_model_definition_get(object_definition->object.collision_model.index) : NULL;
	source->shape_class = shape_class_of(object_definition->object.type);
	source->bounding_radius = object_definition->object.bounding_radius;
	source->bounding_center[0] = object_definition->object.bounding_offset.x;
	source->bounding_center[1] = object_definition->object.bounding_offset.y;
	source->bounding_center[2] = object_definition->object.bounding_offset.z;
	source->node_count = (short)MIN(model->nodes.count, WORLD_PROXIES_SOURCE_MAXIMUM_NODES);
	for (node_index = 0; node_index < source->node_count; node_index++)
	{
		struct model_node const *model_node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
		struct world_proxies_source_node *node = &source->nodes[node_index];
		real_matrix4x3 default_matrix;

		node->parent = model_node->parent_node_index;
		node->wheel = node_is_wheel(model_node->name);
		/* (the default pose's node in the model's space: its inverse's
		inverse) */
		matrix4x3_inverse(&model_node->runtime_default_inverse_matrix, &default_matrix);
		frame_from_matrix(&default_matrix, &node->default_frame);
		node->vertex_count = 0;
		node->vertex_stride = sizeof(struct collision_vertex);
		node->vertices = NULL;
		/* (a collision node is the model's node of its index, as the collision
		tests take them: collision_models.c) */
		if (collision_model && collision_model->nodes.address && node_index < collision_model->nodes.count)
		{
			struct collision_node const *collision_node =
				TAG_BLOCK_GET_ELEMENT(&collision_model->nodes, node_index, struct collision_node);

			if (collision_node->bsps.address && collision_node->bsps.count > 0)
			{
				struct collision_bsp const *bsp = TAG_BLOCK_GET_ELEMENT(&collision_node->bsps, 0, struct collision_bsp);

				if (bsp->vertices.address && bsp->vertices.count > 0)
				{
					node->vertex_count = bsp->vertices.count;
					node->vertices = &TAG_BLOCK_GET_ELEMENT(&bsp->vertices, 0, struct collision_vertex)->point.x;
					collision_any = TRUE;
				}
			}
		}
	}
	/* (a box without collision geometry is of what it draws: its bounding
	sphere is a bound for culling and pickups, about its origin on the
	floor, and much larger than a grenade or a floor marker, #773) */
	if (source->shape_class == _world_proxies_source_box && !collision_any)
		source->drawn = (short)model_drawn_bounds(object_definition->object.model.index, source->drawn_bounds);
	return TRUE;
}

long world_proxies_source_instances(struct world_proxies_source_instance *instances, long maximum)
{
	/* workstream A's scene: the frame's objects as drawn */
	struct world_scene_instance const *scene = world_scene_instances();
	long index, count = 0;

	for (index = 0; scene && index < world_scene_instance_count() && count < maximum; index++)
	{
		/* (nor the static scenery whose field is on its way: it comes with
		the field, faded in) */
		if (scene[index].kind != _world_scene_instance_object || !object_occludes(scene[index].id) ||
			object_static(scene[index].id, scene[index].definition, TRUE))
		{
			continue;
		}
		instances[count].id = scene[index].id;
		instances[count].definition = scene[index].definition;
		instances[count].baked = object_baked(scene[index].id, scene[index].definition);
		instances[count].field_awaited = world_sdf_object_awaited(scene[index].definition);
		count++;
	}
	return count;
}

short world_proxies_source_nodes(long id, struct world_proxies_frame *frames, short maximum)
{
	struct object_datum *object = (struct object_datum *)object_try_and_get(id);
	real_matrix4x3 const *matrices;
	short count, node_index;

	if (!object)
		return 0;
	matrices = object_get_node_matrices(id);
	count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	if (!matrices || count <= 0)
		return 0;
	count = MIN(count, maximum);
	for (node_index = 0; node_index < count; node_index++)
		frame_from_matrix(&matrices[node_index], &frames[node_index]);
	return count;
}

int world_proxies_source_frustum(float planes[4][4])
{
	short plane;

	for (plane = 0; plane < 4; plane++)
	{
		planes[plane][0] = render.frustum.world_planes[plane].n.i;
		planes[plane][1] = render.frustum.world_planes[plane].n.j;
		planes[plane][2] = render.frustum.world_planes[plane].n.k;
		planes[plane][3] = render.frustum.world_planes[plane].d;
	}
	return TRUE;
}

int world_proxies_source_view(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static int view;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		view = config_boolean("debug.world_proxies_view") != 0;
	}
	return view;
}

void world_proxies_source_line(float const a[3], float const b[3], unsigned long color)
{
	real_point3d point0, point1;
	real_argb_color argb;

	point0.x = a[0];
	point0.y = a[1];
	point0.z = a[2];
	point1.x = b[0];
	point1.y = b[1];
	point1.z = b[2];
	argb.alpha = 1.0f;
	argb.red = (real)((color >> 16) & 0xff) / 255.0f;
	argb.green = (real)((color >> 8) & 0xff) / 255.0f;
	argb.blue = (real)(color & 0xff) / 255.0f;
	render_debug_line(TRUE, &point0, &point1, &argb);
}
