/*
WORLD_PROXIES.H

display.ambient_occlusion "world" (docs/world-ao.md, #578): the dynamic
things' occluders, a few analytic shapes for each object (spheres, capsules,
boxes), made once per object definition from its skeleton and collision
model (a biped's limbs from its nodes, a vehicle's body from its collision
geometry's pieces), placed each frame on the object's nodes as drawn
(render_interpolation.c) and handed to the renderer for a window. Their
occlusion is analytic: soft, exact, no noise, nothing on screen needed.

Workstream C owns this interface (port/linux/game/world_proxies.c); D's
shaders read the same shapes on the GPU (xgpu_stream.h), and
world_proxy_occlusion is their CPU reference.
*/

#ifndef WORLD_PROXIES_H
#define WORLD_PROXIES_H

/* ---------- constants */

enum
{
	/* the most shapes a definition has */
	WORLD_PROXY_MAXIMUM_PER_DEFINITION = 16,
	/* the most a window hands the renderer (XGPU_WORLD_AO_PROXIES_MAXIMUM) */
	WORLD_PROXY_MAXIMUM_PER_WINDOW = 64,
	/* the window's shapes a vehicle's occlusion field takes */
	WORLD_PROXY_FIELD_SLOTS = 3,
};

enum world_proxy_kind
{
	_world_proxy_none,
	_world_proxy_sphere,
	_world_proxy_capsule,
	_world_proxy_box,
	/* a vehicle's occlusion field (world_sdf.h's struct world_sdf_object):
	this and the next two of the window's shapes (WORLD_PROXY_FIELD_SLOTS),
	which are of no kind */
	_world_proxy_field,
	NUMBER_OF_WORLD_PROXY_KINDS
};

/* ---------- structures */

/* a shape in its node's space (a definition's) */
struct world_proxy_shape
{
	short kind; /* world_proxy_kind */
	short node_index; /* the node it rides on (-1: the object's root) */
	/* sphere: a (centre), radius; capsule: a, b (its segment's ends),
	radius; box: a (centre), axes[0..2] (its half extents along each, in
	the node's space), radius unused */
	float a[3], b[3];
	float radius;
	float axes[3][3];
};

/* a definition's shapes (made once, kept by definition: its tag index) */
struct world_proxy_set
{
	long definition;
	short count;
	struct world_proxy_shape shapes[WORLD_PROXY_MAXIMUM_PER_DEFINITION];
};

/* a shape in the world, as the renderer takes it (the GPU's layout:
xgpu_stream.h's three vec4s): [0] a.xyz, kind; [1] b.xyz (a box's x
half axis), owner (the window's object slot: an object's own shapes do not
shade it); [2] radius (a box's y half axis xyz and z half extent in w) */
struct world_proxy
{
	float v[3][4];
};

/* ---------- prototypes (port/linux/game/world_proxies.c) */

/* a definition's shapes (made the first time it is asked for: from the
model's nodes and the collision model's pieces, a box of what it draws
opaque without them, or a sphere of its bound radius); NULL none (an
object that occludes nothing: a projectile, a light-weight effect object,
a box without collision geometry that draws nothing opaque) */
struct world_proxy_set const *world_proxies_definition(long definition);
/* the map unloaded: every definition's shapes forgotten */
void world_proxies_map_unloaded(void);
/* a window's shapes: those of the frame's instances (world_scene.h) that
can shade what this camera sees (their spheres of influence in its
frustum, nearest first), at most maximum; returns how many. owners[i] is
the instance's id whose shape proxies[i] is. The dynamic objects' come
first, *dynamic_count of them; the static scenery's that the drawn field
and its pages hold after (world_sdf.h's world_sdf_scenery_baked) */
long world_proxies_window(float const camera_position[3], struct world_proxy *proxies, long *owners,
	long maximum, long *dynamic_count);
/* the scene's shapes about the camera (the distance-field scene's objects,
world_cascades.h): every drawn instance's within reach of it, in the world,
whatever the window sees, but for the static scenery the field holds and
the shapes an occlusion field stands in for (drawn or on its way: the field
is not a distance); at most maximum, each instance's together; owners[i] is
the instance's id. Returns how many */
long world_proxies_scene(float const camera_position[3], float reach, struct world_proxy *proxies, long *owners,
	long maximum);
/* whether an object is a static scenery placement that the drawn field
and its pages hold (world_sdf.h's world_sdf_scenery_baked): its draws take
none of the field, which holds its own geometry
(port/linux/game/world_proxies_game.c, which reads the object) */
int world_proxies_source_baked(long id);
/* whether a static scenery placement's own geometry is in the field: its
definition has collision geometry, which the bake folds in (world_sdf.c's
sdf_scenery_copy). A shrub's or a fern's has none: the field does not hold
it (port/linux/game/world_proxies_game.c) */
int world_proxies_source_solid(long id);
struct world_proxies_frame;
/* a definition's shapes placed on these nodes (world space; at most
maximum): how many. The static scenery's, baked where it stands */
long world_proxies_place(long definition, struct world_proxies_frame const *frames, short node_count,
	struct world_proxy *proxies, long maximum);
struct world_proxies_source_field;
/* an object's occlusion field placed on its node 0 (frame), in the GPU's
layout (xgpu_stream.h's XGPU_WORLD_AO_PROXY_FIELD): the world to its
voxels' place, g = (frame^-1 (p) - origin) / voxel, each row an axis of the
frame over its scale squared and the voxel (a frame of one scale). The
window's, and the static scenery's pages' where a placement has one */
void world_proxies_field_place(struct world_proxies_source_field const *field, struct world_proxies_frame const *frame,
	struct world_proxy proxies[WORLD_PROXY_FIELD_SLOTS]);
/* whether a definition's occlusion field stands in for this shape of its
set where the field is drawn: a vehicle's boxes (its wheels' spheres
stay), and the one shape of anything else (its box: of its collision
geometry, else of what it draws) */
int world_proxy_set_fielded(struct world_proxy_set const *set, short shape_index);

/* ---------- the arithmetic (shared with D's shaders: their reference;
tools/harness/tests/world_proxies.c) */

/* the share of a point's cosine-weighted hemisphere (normal) one shape
blocks, 0 to 1 */
float world_proxy_occlusion(struct world_proxy const *proxy, float const point[3], float const normal[3]);
/* how far from a shape its occlusion is worth computing (the shaders'
cull and the window's grid's; past it, under 1/255), from a sphere's or box's centre (a) or a
capsule's midpoint ((a + b) / 2) */
float world_proxy_reach(struct world_proxy const *proxy);
/* a box's share of its occlusion kept at a point (normal): 0 where a bound
on it is under 1/510 (computing it is not needed), 1 where the bound is
1/255 or more (and inside), between the two a ramp, so the box never
shows an edge where it stops being computed (the shaders' too) */
float world_proxy_box_fade(struct world_proxy const *proxy, float const point[3], float const normal[3]);

#endif
