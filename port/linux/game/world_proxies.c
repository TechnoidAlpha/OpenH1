/*
WORLD_PROXIES.C

display.ambient_occlusion "world"'s object proxies (world_proxies.h,
docs/world-ao.md): each object definition's spheres, capsules and boxes,
placed on its nodes as drawn, and their analytic occlusion (the shaders'
reference). Workstream C.

Pure arithmetic on plain numbers: what it reads of the game (a definition's
skeleton and collision vertices, the window's objects and their nodes, the
frustum) comes through world_proxies_source.h (world_proxies_game.c in the
game, a fake world in tools/harness/tests/world_proxies.c).
*/

#include "world_proxies.h"
#include "world_proxies_source.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define WORLD_PROXIES_PI 3.14159265358979f
/* a box is computed whole where a bound on its occlusion is 2 / this (one
step of 8 bits) or more, not at all under 1 / this (half a step), faded
between (world_proxy_box_fade) */
#define WORLD_PROXIES_LEAST 510.0f
/* a box is found as its faces' patches past this many half diagonals from
its centre (box_occlusion) */
#define WORLD_PROXIES_FAR 6.0f
/* a shape smaller than this across occludes nothing worth a slot */
#define WORLD_PROXIES_SMALLEST 0.05f
/* a capsule's radius against its collision vertices' mean distance from
its axis (a little inside the mesh: a proxy larger than its model leaves a
dark halo) */
#define WORLD_PROXIES_FIT 0.9f

enum
{
	/* the most shapes a biped keeps (its largest): the limbs (upper and
	lower arm, thigh, calf), the torso and the head */
	WORLD_PROXIES_BIPED_SHAPES = 10,
	/* the most boxes a vehicle keeps, its wheels' spheres beside them */
	WORLD_PROXIES_VEHICLE_BOXES = 4,
	/* definitions remembered (a map draws a few hundred at most; past this a
	definition's shapes are made again each time they are asked for) */
	WORLD_PROXIES_CACHE_SIZE = 512,
	/* the most objects a window looks at */
	WORLD_PROXIES_MAXIMUM_INSTANCES = 2048,
};

/* the debug view's colours */
#define WORLD_PROXIES_SPHERE_COLOR 0xffe040UL
#define WORLD_PROXIES_CAPSULE_COLOR 0x40ff60UL
#define WORLD_PROXIES_BOX_COLOR 0x40e0ffUL

/* ---------- structures */

struct world_proxies_cache_entry
{
	int used;
	struct world_proxy_set set;
};

struct world_proxies_candidate
{
	/* (a vehicle's occlusion field's three, else one) */
	struct world_proxy proxies[WORLD_PROXY_FIELD_SLOTS];
	short slots;
	long owner;
	float distance;
	/* (a static scenery placement the field and its pages hold) */
	int baked;
};

/* ---------- globals */

static struct
{
	/* (allocated on first use, freed at a map's unload: kept out of the
	image, whose guest address window is tight) */
	struct world_proxies_cache_entry *entries;
	long entry_count;
	/* a definition made when the cache is full */
	struct world_proxy_set spare;
	struct world_proxies_source source;
	struct world_proxies_source_instance instances[WORLD_PROXIES_MAXIMUM_INSTANCES];
	struct world_proxies_frame frames[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
	struct world_proxies_candidate candidates[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long candidate_count, candidate_slots;
} world_proxies;

/* ---------- vectors */

static float dot3(float const a[3], float const b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void sub3(float const a[3], float const b[3], float out[3])
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

static void cross3(float const a[3], float const b[3], float out[3])
{
	float x = a[1] * b[2] - a[2] * b[1];
	float y = a[2] * b[0] - a[0] * b[2];
	float z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static float length3(float const a[3])
{
	return sqrtf(dot3(a, a));
}

static float clampf(float value, float low, float high)
{
	return value < low ? low : value > high ? high : value;
}

/* a point of a frame's space in its parent's */
static void frame_point(struct world_proxies_frame const *frame, float const point[3], float out[3])
{
	float result[3];
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		result[axis] = frame->position[axis] + frame->axes[0][axis] * point[0] + frame->axes[1][axis] * point[1] +
			frame->axes[2][axis] * point[2];
	}
	memcpy(out, result, sizeof(result));
}

static void frame_vector(struct world_proxies_frame const *frame, float const vector[3], float out[3])
{
	float result[3];
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		result[axis] = frame->axes[0][axis] * vector[0] + frame->axes[1][axis] * vector[1] +
			frame->axes[2][axis] * vector[2];
	}
	memcpy(out, result, sizeof(result));
}

/* a point of the parent's space in a frame's (its axes square to each
other, each as long as its scale) */
static void frame_point_inverse(struct world_proxies_frame const *frame, float const point[3], float out[3])
{
	float offset[3], result[3];
	int axis;

	sub3(point, frame->position, offset);
	for (axis = 0; axis < 3; axis++)
	{
		float length_squared = dot3(frame->axes[axis], frame->axes[axis]);

		result[axis] = length_squared > 0.0f ? dot3(offset, frame->axes[axis]) / length_squared : 0.0f;
	}
	memcpy(out, result, sizeof(result));
}

static float frame_scale(struct world_proxies_frame const *frame)
{
	return length3(frame->axes[0]);
}

/* ---------- the arithmetic (D's shaders follow it line for line:
docs/world-ao.md, "Proxies' occlusion") */

/* a sphere's share of the cosine-weighted hemisphere at p (normal n), with
the horizon's clipping: exact (Lagarde and de Rousiers' form, as Quilez
gives it) */
static float sphere_occlusion(float const centre[3], float radius, float const point[3], float const normal[3])
{
	float d[3], length_squared, length, nl, h2, k2;

	sub3(centre, point, d);
	length_squared = dot3(d, d);
	if (length_squared <= radius * radius)
		return 1.0f;
	length = sqrtf(length_squared);
	nl = dot3(normal, d) / length;
	h2 = length_squared / (radius * radius);
	k2 = 1.0f - h2 * nl * nl;
	if (k2 > 0.001f)
	{
		/* (the sphere crosses p's tangent plane) */
		float cosine = clampf(-nl * sqrtf((h2 - 1.0f) / (1.0f - nl * nl)), -1.0f, 1.0f);
		float occlusion = (nl * acosf(cosine) - sqrtf(k2 * (h2 - 1.0f))) / h2 + atanf(sqrtf(k2 / (h2 - 1.0f)));

		return clampf(occlusion / WORLD_PROXIES_PI, 0.0f, 1.0f);
	}
	return nl > 0.0f ? nl / h2 : 0.0f;
}

/* a capsule (segment a to b, radius r): its tube as a line 2r wide, the
part above p's tangent plane integrated in closed form, plus its two caps'
spheres, each by how much of it shows past the tube (half of each seen from
the side, the near one whole seen end on), and never less than the sphere
at the segment's point closest to p (near the tube, where a line 2r wide
says too little) */
static float capsule_occlusion(float const a[3], float const b[3], float radius, float const point[3],
	float const normal[3])
{
	float axis[3], q[3], w[3], to_a[3], to_b[3], closest[3];
	float length, s0, h, h2, nw, nu, t0, t1, line, weight_a, weight_b, occlusion, closest_occlusion;
	float to_a_length, to_b_length;
	int k;

	sub3(b, a, axis);
	length = length3(axis);
	if (length < 1.0e-4f)
		return sphere_occlusion(a, radius, point, normal);
	for (k = 0; k < 3; k++)
		axis[k] /= length;
	/* the line: (2 r h / pi) times the integral of n.d / |d|^4 along it,
	d = w + t axis from p, w the foot of p's perpendicular, h = |w| */
	sub3(a, point, q);
	s0 = -dot3(q, axis);
	for (k = 0; k < 3; k++)
		w[k] = q[k] + s0 * axis[k];
	h = length3(w);
	nw = dot3(normal, w);
	nu = dot3(normal, axis);
	t0 = -s0;
	t1 = length - s0;
	line = 0.0f;
	if (fabsf(nu) < 1.0e-6f)
	{
		if (nw <= 0.0f)
			t1 = t0;
	}
	else
	{
		/* (above the tangent plane only: n.d > 0) */
		float t_horizon = -nw / nu;

		if (nu > 0.0f)
			t0 = t0 > t_horizon ? t0 : t_horizon;
		else
			t1 = t1 < t_horizon ? t1 : t_horizon;
	}
	if (t1 > t0 && h > 1.0e-4f)
	{
		float f0, f1;

		h2 = h * h;
		f0 = nw * (t0 / (2.0f * h2 * (h2 + t0 * t0)) + atanf(t0 / h) / (2.0f * h2 * h)) - nu / (2.0f * (h2 + t0 * t0));
		f1 = nw * (t1 / (2.0f * h2 * (h2 + t1 * t1)) + atanf(t1 / h) / (2.0f * h2 * h)) - nu / (2.0f * (h2 + t1 * t1));
		line = 2.0f * radius * h / WORLD_PROXIES_PI * (f1 - f0);
	}
	/* the caps */
	sub3(point, a, to_a);
	sub3(point, b, to_b);
	to_a_length = length3(to_a);
	to_b_length = length3(to_b);
	weight_a = to_a_length > 0.0f ? 0.5f * (1.0f - dot3(axis, to_a) / to_a_length) : 0.5f;
	weight_b = to_b_length > 0.0f ? 0.5f * (1.0f + dot3(axis, to_b) / to_b_length) : 0.5f;
	occlusion = line + weight_a * sphere_occlusion(a, radius, point, normal) +
		weight_b * sphere_occlusion(b, radius, point, normal);
	/* the sphere at the closest point */
	s0 = clampf(s0, 0.0f, length);
	for (k = 0; k < 3; k++)
		closest[k] = a[k] + s0 * axis[k];
	closest_occlusion = sphere_occlusion(closest, radius, point, normal);
	occlusion = occlusion > closest_occlusion ? occlusion : closest_occlusion;
	return clampf(occlusion, 0.0f, 1.0f);
}

/* an edge's term of a polygon's form factor at p (normal n): the angle the
edge (a to b, from p) subtends times n . its plane's unit normal. Both are
the same for any lengths of a and b: atan2(|a x b|, a . b) and
n . (a x b) / |a x b|, so neither is normalized (none where the angle's sine
is under 1e-7) */
static float box_edge(float const normal[3], float const a[3], float const b[3])
{
	float edge_normal[3], sine_squared, sine;

	cross3(a, b, edge_normal);
	sine_squared = dot3(edge_normal, edge_normal);
	if (!(sine_squared > 1.0e-14f * dot3(a, a) * dot3(b, b)))
		return 0.0f;
	sine = sqrtf(sine_squared);
	return atan2f(sine, dot3(a, b)) * dot3(normal, edge_normal) / sine;
}

/* a box (centre, half axes x and y, half extent along z = x cross y): the
faces turned towards p (at most 3), each clipped by p's tangent plane, and
their form factor (Lambert): (1 / 2 pi) the sum over the edges of the angle
each subtends times n . (the edge's plane's normal). The faces are taken
the same way round as seen from outside the box (their corners' order times
the side), so an edge two of them share is taken once each way and its two
terms cancel: such an edge is left out, and only the outline's edges (at
most 6, and the tangent plane's chords) are found. A face's clipped edges
are its edges' parts above the plane (Sutherland and Hodgman's taken edge
by edge) and the chord from where it leaves the plane to where it enters */
static float box_occlusion(float const centre[3], float const half_x[3], float const half_y[3], float extent_z,
	float const point[3], float const normal[3])
{
	float axes[3][3], extents[3], local[3], local_normal[3], offset[3];
	float occlusion = 0.0f;
	int axis, k;

	extents[0] = length3(half_x);
	extents[1] = length3(half_y);
	extents[2] = extent_z;
	if (extents[0] <= 0.0f || extents[1] <= 0.0f || extents[2] <= 0.0f)
		return 0.0f;
	for (k = 0; k < 3; k++)
	{
		axes[0][k] = half_x[k] / extents[0];
		axes[1][k] = half_y[k] / extents[1];
	}
	cross3(axes[0], axes[1], axes[2]);
	sub3(point, centre, offset);
	for (axis = 0; axis < 3; axis++)
	{
		local[axis] = dot3(offset, axes[axis]);
		local_normal[axis] = dot3(normal, axes[axis]);
	}
	if (fabsf(local[0]) <= extents[0] && fabsf(local[1]) <= extents[1] && fabsf(local[2]) <= extents[2])
		return 1.0f;
	/* far from it (WORLD_PROXIES_FAR half diagonals or more): each front face
	as a patch at its centre, area 4 e_u e_v, its cosine (o / r) and p's
	(n . r / r, 0 below the plane) over pi r^2, r from p to the face's centre:
	within 0.15/255 of the exact form there (0.6/255 at 4 half diagonals,
	1.5/255 at 3), at a tenth of its cost */
	if (dot3(offset, offset) >= WORLD_PROXIES_FAR * WORLD_PROXIES_FAR * (extents[0] * extents[0] +
		extents[1] * extents[1] + extents[2] * extents[2]))
	{
		for (axis = 0; axis < 3; axis++)
		{
			int side, u = (axis + 1) % 3, v = (axis + 2) % 3;

			for (side = -1; side <= 1; side += 2)
			{
				float out = (float)side * local[axis] - extents[axis], r[3], r_squared, facing;

				if (out <= 0.0f)
					continue;
				r[axis] = (float)side * extents[axis] - local[axis];
				r[u] = -local[u];
				r[v] = -local[v];
				r_squared = dot3(r, r);
				facing = dot3(local_normal, r);
				if (facing > 0.0f)
					occlusion += 4.0f * extents[u] * extents[v] * out * facing / (WORLD_PROXIES_PI * r_squared * r_squared);
			}
		}
		return clampf(occlusion, 0.0f, 1.0f);
	}
	for (axis = 0; axis < 3; axis++)
	{
		int side;

		for (side = -1; side <= 1; side += 2)
		{
			/* the face's corners from p (about +axis: (-u -v), (+u -v), (+u +v),
			(-u +v)) and their heights above the tangent plane */
			float corners[4][3], heights[4], exit_point[3], entry_point[3], face = 0.0f;
			int u = (axis + 1) % 3, v = (axis + 2) % 3, corner, crossed = 0;

			if ((float)side * local[axis] <= extents[axis])
				continue;
			for (corner = 0; corner < 4; corner++)
			{
				float su = (corner == 1 || corner == 2) ? 1.0f : -1.0f;
				float sv = corner >= 2 ? 1.0f : -1.0f;

				corners[corner][axis] = (float)side * extents[axis] - local[axis];
				corners[corner][u] = su * extents[u] - local[u];
				corners[corner][v] = sv * extents[v] - local[v];
				heights[corner] = dot3(local_normal, corners[corner]);
			}
			for (corner = 0; corner < 4; corner++)
			{
				int next = (corner + 1) % 4;
				/* (the face across this edge: edges 0 and 2 lie on v's faces, 1
				and 3 on u's, at -, +, +, -) */
				int across = corner == 0 || corner == 2 ? v : u;
				float across_side = corner == 0 || corner == 3 ? -1.0f : 1.0f;
				int above = heights[corner] >= 0.0f, next_above = heights[next] >= 0.0f;
				float crossing[3], t = heights[corner] / (heights[corner] - heights[next]);

				for (k = 0; k < 3; k++)
					crossing[k] = corners[corner][k] + t * (corners[next][k] - corners[corner][k]);
				if (above && !next_above)
				{
					memcpy(exit_point, crossing, sizeof(exit_point));
					crossed = 1;
				}
				if (!above && next_above)
					memcpy(entry_point, crossing, sizeof(entry_point));
				if ((above || next_above) && across_side * local[across] <= extents[across])
					face += box_edge(local_normal, above ? corners[corner] : crossing, next_above ? corners[next] : crossing);
			}
			if (crossed)
				face += box_edge(local_normal, exit_point, entry_point);
			occlusion += (float)side * face;
		}
	}
	return clampf(fabsf(occlusion) / (2.0f * WORLD_PROXIES_PI), 0.0f, 1.0f);
}

/* a box's fade (world_proxy_box_fade): a bound on box_occlusion that is
never under it, U, made a fade that is 0 where U is under 1/510 and 1 where
it is 1/255 or more, saturate(510 U - 1): what it leaves out of the
occlusion is under 1/255 (and the old reach's fade, 17 half diagonals,
left out about as much face on). In box space, with o the point's
distance outside each pair of faces (o_i = max(|p_i| - e_i, 0)) and D = |o|
its distance from the box:
- the box's solid angle is its front faces' (at most 3), and a face's is
  the integral over it of o_i / r^3 with r >= D: at most its area 4 e_j e_k
  times o_i / D^3;
- the cosine at p of any direction to the box is at most H / D, H the
  box's height above p's tangent plane (its support along n less n . p);
so U = (1 / pi) (4 (e_y e_z o_x + e_x e_z o_y + e_x e_y o_z) / D^3) min(1, H / D).
It falls as 1 / D^3 on a floor beside a box (H / D small), and past a thin
box's edges it is its edge's area's, where 17 half diagonals reached far:
the box is computed only where it can show. 0 wholly below the tangent
plane; 1 inside */
float world_proxy_box_fade(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	float extents[3], axes[3][3], offset[3], outside[3], distance_squared, height = 0.0f, faces, bound;
	int axis, k;

	extents[0] = length3(proxy->v[1]);
	extents[1] = length3(proxy->v[2]);
	extents[2] = proxy->v[2][3];
	if (extents[0] <= 0.0f || extents[1] <= 0.0f || extents[2] <= 0.0f)
		return 0.0f;
	for (k = 0; k < 3; k++)
	{
		axes[0][k] = proxy->v[1][k] / extents[0];
		axes[1][k] = proxy->v[2][k] / extents[1];
	}
	cross3(axes[0], axes[1], axes[2]);
	sub3(point, proxy->v[0], offset);
	distance_squared = 0.0f;
	for (axis = 0; axis < 3; axis++)
	{
		float local = dot3(offset, axes[axis]), local_normal = dot3(normal, axes[axis]);

		outside[axis] = fabsf(local) - extents[axis];
		outside[axis] = outside[axis] > 0.0f ? outside[axis] : 0.0f;
		distance_squared += outside[axis] * outside[axis];
		height += extents[axis] * fabsf(local_normal) - local_normal * local;
	}
	if (distance_squared <= 0.0f)
		return 1.0f;
	if (height <= 0.0f)
		return 0.0f;
	faces = extents[1] * extents[2] * outside[0] + extents[0] * extents[2] * outside[1] +
		extents[0] * extents[1] * outside[2];
	{
		float distance = sqrtf(distance_squared);

		bound = WORLD_PROXIES_LEAST * 4.0f / WORLD_PROXIES_PI * faces * (height < distance ? height : distance) /
			(distance_squared * distance_squared);
	}
	return clampf(bound - 1.0f, 0.0f, 1.0f);
}

float world_proxy_occlusion(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	switch ((int)proxy->v[0][3])
	{
	case _world_proxy_sphere:
		return sphere_occlusion(proxy->v[0], proxy->v[2][0], point, normal);
	case _world_proxy_capsule:
		return capsule_occlusion(proxy->v[0], proxy->v[1], proxy->v[2][0], point, normal);
	case _world_proxy_box:
		return box_occlusion(proxy->v[0], proxy->v[1], proxy->v[2], proxy->v[2][3], point, normal);
	default:
		return 0.0f;
	}
}

/* the centre world_proxy_reach is measured from: a sphere's and a box's
centre, a capsule's midpoint */
static void world_proxy_reach_centre(struct world_proxy const *proxy, float centre[3])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		centre[k] = (int)proxy->v[0][3] == _world_proxy_capsule ?
			0.5f * (proxy->v[0][k] + proxy->v[1][k]) : proxy->v[0][k];
	}
}

float world_proxy_reach(struct world_proxy const *proxy)
{
	switch ((int)proxy->v[0][3])
	{
	case _world_proxy_capsule:
	{
		/* as a sphere of its silhouette's area seen side on (2 r L + pi r^2),
		from the segment's nearest point */
		float axis[3], length, radius = proxy->v[2][0];

		sub3(proxy->v[1], proxy->v[0], axis);
		length = length3(axis);
		return 0.5f * length + 16.0f * sqrtf(radius * radius + 2.0f * radius * length / WORLD_PROXIES_PI);
	}
	case _world_proxy_field:
		/* (its own: past it, under 1/510) */
		return proxy->v[2][3];
	case _world_proxy_none:
		return 0.0f;
	case _world_proxy_box:
	{
		/* world_proxy_box_fade's U is at most 4 |A| / (pi D^2), A its faces'
		areas over 4 (e_y e_z, e_x e_z, e_x e_y): under 1/510 (its fade 0)
		past D^2 = 510 * 4 |A| / pi from the box, so past that and a half
		diagonal from its centre. (A sphere about the box, for the window's
		frustum and order: the grid takes the box's own bounds grown by D,
		and each pixel the bound itself, which is far less on a floor beside
		it or past a flat or long box's edges) */
		float x = length3(proxy->v[1]), y = length3(proxy->v[2]), z = proxy->v[2][3];
		float areas = sqrtf((y * z) * (y * z) + (x * z) * (x * z) + (x * y) * (x * y));

		return sqrtf(x * x + y * y + z * z) + sqrtf(WORLD_PROXIES_LEAST * 4.0f / WORLD_PROXIES_PI * areas);
	}
	default:
		/* (r / l)^2 < 1/255 past 16 radii */
		return 16.0f * proxy->v[2][0];
	}
}

/* ---------- the shapes of a definition */

static float shape_volume(struct world_proxy_shape const *shape)
{
	float axis[3];

	switch (shape->kind)
	{
	case _world_proxy_capsule:
		sub3(shape->b, shape->a, axis);
		return WORLD_PROXIES_PI * shape->radius * shape->radius * (length3(axis) + 4.0f / 3.0f * shape->radius);
	case _world_proxy_box:
		return 8.0f * length3(shape->axes[0]) * length3(shape->axes[1]) * length3(shape->axes[2]);
	default:
		return 4.0f / 3.0f * WORLD_PROXIES_PI * shape->radius * shape->radius * shape->radius;
	}
}

static float shape_size(struct world_proxy_shape const *shape)
{
	float axis[3];

	switch (shape->kind)
	{
	case _world_proxy_capsule:
		sub3(shape->b, shape->a, axis);
		return length3(axis) + 2.0f * shape->radius;
	case _world_proxy_box:
	{
		float x = length3(shape->axes[0]), y = length3(shape->axes[1]), z = length3(shape->axes[2]);

		return 2.0f * (x > y ? (x > z ? x : z) : (y > z ? y : z));
	}
	default:
		return 2.0f * shape->radius;
	}
}

static void set_add(struct world_proxy_set *set, struct world_proxy_shape const *shape)
{
	if (set->count < WORLD_PROXY_MAXIMUM_PER_DEFINITION && shape_size(shape) >= WORLD_PROXIES_SMALLEST)
		set->shapes[set->count++] = *shape;
}

/* the set cut to its largest shapes (bipeds: WORLD_PROXIES_BIPED_SHAPES) */
static void set_keep_largest(struct world_proxy_set *set, short keep)
{
	short i, j;

	for (i = 1; i < set->count; i++)
	{
		struct world_proxy_shape shape = set->shapes[i];
		float volume = shape_volume(&shape);

		for (j = i; j > 0 && shape_volume(&set->shapes[j - 1]) < volume; j--)
			set->shapes[j] = set->shapes[j - 1];
		set->shapes[j] = shape;
	}
	if (set->count > keep)
		set->count = keep;
}

static float const *node_vertex(struct world_proxies_source_node const *node, long index)
{
	return (float const *)((char const *)node->vertices + index * node->vertex_stride);
}

/* a node's child farthest from it, in its space; FALSE: none */
static int node_child_offset(struct world_proxies_source const *source, short node_index, float offset[3])
{
	float best = 0.0f;
	short child;

	for (child = 0; child < source->node_count; child++)
	{
		float local[3], length;

		if (source->nodes[child].parent != node_index)
			continue;
		frame_point_inverse(&source->nodes[node_index].default_frame,
			source->nodes[child].default_frame.position, local);
		length = length3(local);
		if (length > best)
		{
			best = length;
			memcpy(offset, local, sizeof(local));
		}
	}
	return best > WORLD_PROXIES_SMALLEST;
}

/* the direction the vertices spread along most (their covariance's first
eigenvector, by power iteration); FALSE: none */
static int principal_axis(struct world_proxies_source_node const *node, float const centre[3], float axis[3])
{
	float covariance[3][3];
	long vertex;
	int i, j, step;

	memset(covariance, 0, sizeof(covariance));
	for (vertex = 0; vertex < node->vertex_count; vertex++)
	{
		float away[3];

		sub3(node_vertex(node, vertex), centre, away);
		for (i = 0; i < 3; i++)
		{
			for (j = 0; j < 3; j++)
				covariance[i][j] += away[i] * away[j];
		}
	}
	axis[0] = 0.57735f;
	axis[1] = 0.57735f;
	axis[2] = 0.57735f;
	for (step = 0; step < 32; step++)
	{
		float next[3], length;

		for (i = 0; i < 3; i++)
			next[i] = dot3(covariance[i], axis);
		length = length3(next);
		if (length <= 0.0f)
			return 0;
		for (i = 0; i < 3; i++)
			axis[i] = next[i] / length;
	}
	return 1;
}

/* a node's collision vertices fitted: a capsule along the bone to its
child (or along their longest spread), or a sphere where they are short */
static void biped_node_shape(struct world_proxies_source const *source, short node_index,
	struct world_proxy_shape *shape)
{
	struct world_proxies_source_node const *node = &source->nodes[node_index];
	float centre[3] = { 0.0f, 0.0f, 0.0f }, direction[3];
	long vertex;
	int k;

	memset(shape, 0, sizeof(*shape));
	shape->node_index = node_index;
	for (vertex = 0; vertex < node->vertex_count; vertex++)
	{
		for (k = 0; k < 3; k++)
			centre[k] += node_vertex(node, vertex)[k] / (float)node->vertex_count;
	}
	if (node_child_offset(source, node_index, direction) || principal_axis(node, centre, direction))
	{
		float length = length3(direction), perpendicular_centre[3] = { 0.0f, 0.0f, 0.0f };
		float lowest = 1.0e30f, highest = -1.0e30f, distance = 0.0f, radius;

		for (k = 0; k < 3; k++)
			direction[k] /= length;
		for (vertex = 0; vertex < node->vertex_count; vertex++)
		{
			float const *point = node_vertex(node, vertex);
			float along = dot3(point, direction);

			lowest = along < lowest ? along : lowest;
			highest = along > highest ? along : highest;
			for (k = 0; k < 3; k++)
				perpendicular_centre[k] += (point[k] - along * direction[k]) / (float)node->vertex_count;
		}
		for (vertex = 0; vertex < node->vertex_count; vertex++)
		{
			float const *point = node_vertex(node, vertex);
			float along = dot3(point, direction), away[3];

			for (k = 0; k < 3; k++)
				away[k] = point[k] - along * direction[k] - perpendicular_centre[k];
			distance += length3(away) / (float)node->vertex_count;
		}
		radius = WORLD_PROXIES_FIT * distance;
		if (highest - lowest > 2.0f * radius)
		{
			shape->kind = _world_proxy_capsule;
			shape->radius = radius;
			for (k = 0; k < 3; k++)
			{
				shape->a[k] = perpendicular_centre[k] + (lowest + radius) * direction[k];
				shape->b[k] = perpendicular_centre[k] + (highest - radius) * direction[k];
			}
			return;
		}
	}
	{
		/* a sphere of the vertices' mean distance from their centre */
		float distance = 0.0f;

		for (vertex = 0; vertex < node->vertex_count; vertex++)
		{
			float away[3];

			sub3(node_vertex(node, vertex), centre, away);
			distance += length3(away) / (float)node->vertex_count;
		}
		shape->kind = _world_proxy_sphere;
		shape->radius = WORLD_PROXIES_FIT * distance;
		memcpy(shape->a, centre, sizeof(centre));
		memcpy(shape->b, centre, sizeof(centre));
	}
}

static void biped_shapes(struct world_proxies_source const *source, struct world_proxy_set *set)
{
	/* (every node with collision geometry: up to 64, then the largest kept) */
	struct world_proxy_shape shapes[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
	struct world_proxy_set all;
	short node_index, count = 0, i;

	for (node_index = 0; node_index < source->node_count; node_index++)
	{
		if (source->nodes[node_index].vertex_count >= 4)
			biped_node_shape(source, node_index, &shapes[count++]);
	}
	if (count == 0)
	{
		/* no collision geometry: each bone to its child, its radius a share of
		the bounding radius */
		for (node_index = 0; node_index < source->node_count; node_index++)
		{
			float offset[3];

			if (!node_child_offset(source, node_index, offset))
				continue;
			memset(&shapes[count], 0, sizeof(shapes[count]));
			shapes[count].kind = _world_proxy_capsule;
			shapes[count].node_index = node_index;
			shapes[count].radius = 0.12f * source->bounding_radius;
			memcpy(shapes[count].b, offset, sizeof(offset));
			count++;
		}
	}
	/* (the largest, whatever their order) */
	memset(&all, 0, sizeof(all));
	for (i = 0; i < count; i++)
	{
		if (shape_size(&shapes[i]) < WORLD_PROXIES_SMALLEST)
			continue;
		if (all.count < WORLD_PROXY_MAXIMUM_PER_DEFINITION)
		{
			all.shapes[all.count++] = shapes[i];
		}
		else
		{
			/* (full: the smallest kept so far gives way) */
			short smallest = 0, j;

			for (j = 1; j < all.count; j++)
			{
				if (shape_volume(&all.shapes[j]) < shape_volume(&all.shapes[smallest]))
					smallest = j;
			}
			if (shape_volume(&shapes[i]) > shape_volume(&all.shapes[smallest]))
				all.shapes[smallest] = shapes[i];
		}
	}
	set_keep_largest(&all, WORLD_PROXIES_BIPED_SHAPES);
	for (i = 0; i < all.count; i++)
		set_add(set, &all.shapes[i]);
}

/* a node's collision vertices' bounds in another node's space (the
default pose); FALSE: none */
static int node_bounds_in(struct world_proxies_source const *source, short node_index, short space_index,
	float bounds[2][3])
{
	struct world_proxies_source_node const *node = &source->nodes[node_index];
	long vertex;
	int k;

	if (node->vertex_count <= 0)
		return 0;
	for (vertex = 0; vertex < node->vertex_count; vertex++)
	{
		float point[3];

		frame_point(&node->default_frame, node_vertex(node, vertex), point);
		frame_point_inverse(&source->nodes[space_index].default_frame, point, point);
		for (k = 0; k < 3; k++)
		{
			if (vertex == 0 || point[k] < bounds[0][k])
				bounds[0][k] = point[k];
			if (vertex == 0 || point[k] > bounds[1][k])
				bounds[1][k] = point[k];
		}
	}
	return 1;
}

static void box_shape(short node_index, float const bounds[2][3], struct world_proxy_shape *shape)
{
	int k;

	memset(shape, 0, sizeof(*shape));
	shape->kind = _world_proxy_box;
	shape->node_index = node_index;
	for (k = 0; k < 3; k++)
	{
		shape->a[k] = 0.5f * (bounds[0][k] + bounds[1][k]);
		shape->axes[k][k] = 0.5f * (bounds[1][k] - bounds[0][k]);
	}
	memcpy(shape->b, shape->a, sizeof(shape->a));
}

/* bounds grown by a box's corners carried from one node's space to
another's */
static void bounds_add_box(struct world_proxies_source const *source, short from, float const box[2][3], short to,
	float bounds[2][3])
{
	int corner, k;

	for (corner = 0; corner < 8; corner++)
	{
		float point[3];

		for (k = 0; k < 3; k++)
			point[k] = box[(corner >> k) & 1][k];
		frame_point(&source->nodes[from].default_frame, point, point);
		frame_point_inverse(&source->nodes[to].default_frame, point, point);
		for (k = 0; k < 3; k++)
		{
			bounds[0][k] = point[k] < bounds[0][k] ? point[k] : bounds[0][k];
			bounds[1][k] = point[k] > bounds[1][k] ? point[k] : bounds[1][k];
		}
	}
}

static float bounds_volume(float const bounds[2][3])
{
	return (bounds[1][0] - bounds[0][0]) * (bounds[1][1] - bounds[0][1]) * (bounds[1][2] - bounds[0][2]);
}

static void vehicle_shapes(struct world_proxies_source const *source, struct world_proxy_set *set)
{
	float boxes[WORLD_PROXIES_SOURCE_MAXIMUM_NODES][2][3];
	short box_nodes[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
	short node_index, count = 0, i;
	float lowest = 1.0e30f;

	/* a box for each node's collision geometry (but the wheels') */
	for (node_index = 0; node_index < source->node_count; node_index++)
	{
		if (source->nodes[node_index].wheel)
			continue;
		if (node_bounds_in(source, node_index, node_index, boxes[count]))
			box_nodes[count++] = node_index;
	}
	/* at most WORLD_PROXIES_VEHICLE_BOXES: the smallest merged into its
	nearest ancestor's box, or the largest's */
	while (count > WORLD_PROXIES_VEHICLE_BOXES)
	{
		short smallest = 0, largest = 0, into = -1, ancestor;

		for (i = 1; i < count; i++)
		{
			if (bounds_volume(boxes[i]) < bounds_volume(boxes[smallest]))
				smallest = i;
			if (bounds_volume(boxes[i]) > bounds_volume(boxes[largest]))
				largest = i;
		}
		for (ancestor = source->nodes[box_nodes[smallest]].parent; ancestor >= 0 && into < 0;
			ancestor = source->nodes[ancestor].parent)
		{
			for (i = 0; i < count; i++)
			{
				if (i != smallest && box_nodes[i] == ancestor)
					into = i;
			}
		}
		if (into < 0)
			into = largest != smallest ? largest : (smallest == 0 ? 1 : 0);
		bounds_add_box(source, box_nodes[smallest], (float const (*)[3])boxes[smallest], box_nodes[into], boxes[into]);
		for (i = smallest; i < count - 1; i++)
		{
			memcpy(boxes[i], boxes[i + 1], sizeof(boxes[i]));
			box_nodes[i] = box_nodes[i + 1];
		}
		count--;
	}
	for (i = 0; i < count; i++)
	{
		struct world_proxy_shape shape;
		int corner, k;

		box_shape(box_nodes[i], (float const (*)[3])boxes[i], &shape);
		set_add(set, &shape);
		/* (the body's lowest point, in the model's space: the wheels' ground) */
		for (corner = 0; corner < 8; corner++)
		{
			float point[3];

			for (k = 0; k < 3; k++)
				point[k] = boxes[i][(corner >> k) & 1][k];
			frame_point(&source->nodes[box_nodes[i]].default_frame, point, point);
			lowest = point[2] < lowest ? point[2] : lowest;
		}
	}
	/* the wheels: a sphere each, of its own collision geometry, else of its
	height above the body's lowest point (clamped) */
	for (node_index = 0; node_index < source->node_count; node_index++)
	{
		struct world_proxies_source_node const *node = &source->nodes[node_index];
		struct world_proxy_shape shape;
		float bounds[2][3];
		int k;

		if (!node->wheel)
			continue;
		memset(&shape, 0, sizeof(shape));
		shape.kind = _world_proxy_sphere;
		shape.node_index = node_index;
		if (node_bounds_in(source, node_index, node_index, bounds))
		{
			for (k = 0; k < 3; k++)
			{
				float half = 0.5f * (bounds[1][k] - bounds[0][k]);

				shape.a[k] = 0.5f * (bounds[0][k] + bounds[1][k]);
				shape.radius = half > shape.radius ? half : shape.radius;
			}
		}
		else
		{
			float height = node->default_frame.position[2] - lowest;

			shape.radius = lowest < 1.0e29f && height > 0.0f ? clampf(height, 0.15f, 1.0f) : 0.3f;
		}
		memcpy(shape.b, shape.a, sizeof(shape.a));
		set_add(set, &shape);
	}
}

/* the bounding sphere, in node 0's space */
static void sphere_shape(struct world_proxies_source const *source, struct world_proxy_shape *shape)
{
	memset(shape, 0, sizeof(*shape));
	shape->kind = _world_proxy_sphere;
	shape->node_index = 0;
	shape->radius = source->bounding_radius;
	if (source->node_count > 0)
		frame_point_inverse(&source->nodes[0].default_frame, source->bounding_center, shape->a);
	else
		memcpy(shape->a, source->bounding_center, sizeof(shape->a));
	memcpy(shape->b, shape->a, sizeof(shape->a));
}

static void definition_shapes(struct world_proxies_source const *source, struct world_proxy_set *set)
{
	struct world_proxy_shape shape;

	if (source->node_count <= 0 || source->bounding_radius < 0.5f * WORLD_PROXIES_SMALLEST)
		return;
	switch (source->shape_class)
	{
	case _world_proxies_source_biped:
		biped_shapes(source, set);
		break;
	case _world_proxies_source_vehicle:
		vehicle_shapes(source, set);
		if (set->count == 0)
		{
			sphere_shape(source, &shape);
			set_add(set, &shape);
		}
		break;
	case _world_proxies_source_box:
	{
		/* every node's collision geometry in node 0's space, one box; with
		none, the box of what it draws opaque, and nothing when it draws
		nothing opaque (not its bounding sphere: a bound for culling, about
		its origin on the floor, which held the floor inside it wholly
		occluded about a grenade or a floor marker, #773) */
		float bounds[2][3], box[2][3];
		short node_index;
		int any = 0;

		for (node_index = 0; node_index < source->node_count; node_index++)
		{
			if (!node_bounds_in(source, node_index, 0, box))
				continue;
			if (!any)
				memcpy(bounds, box, sizeof(box));
			bounds_add_box(source, 0, (float const (*)[3])box, 0, bounds);
			any = 1;
		}
		if (!any && source->drawn)
		{
			memcpy(bounds, source->drawn_bounds, sizeof(bounds));
			any = 1;
		}
		if (any)
		{
			box_shape(0, (float const (*)[3])bounds, &shape);
			set_add(set, &shape);
		}
		break;
	}
	case _world_proxies_source_sphere:
		sphere_shape(source, &shape);
		set_add(set, &shape);
		break;
	default:
		break;
	}
}

struct world_proxy_set const *world_proxies_definition(long definition)
{
	struct world_proxy_set *set;
	long slot, probe;

	/* (a tag index is a datum handle: its salt sets the sign bit) */
	if (definition == -1)
		return NULL;
	/* (open addressing on the tag index) */
	slot = -1;
	if (!world_proxies.entries)
		world_proxies.entries = calloc(WORLD_PROXIES_CACHE_SIZE, sizeof(*world_proxies.entries));
	for (probe = 0; world_proxies.entries && probe < WORLD_PROXIES_CACHE_SIZE; probe++)
	{
		struct world_proxies_cache_entry *entry =
			&world_proxies.entries[((unsigned long)definition * 2654435761UL + (unsigned long)probe) %
				WORLD_PROXIES_CACHE_SIZE];

		if (!entry->used)
		{
			slot = entry - world_proxies.entries;
			break;
		}
		if (entry->set.definition == definition)
			return entry->set.count > 0 ? &entry->set : NULL;
	}
	set = slot >= 0 ? &world_proxies.entries[slot].set : &world_proxies.spare;
	memset(set, 0, sizeof(*set));
	set->definition = definition;
	memset(&world_proxies.source, 0, sizeof(world_proxies.source));
	if (world_proxies_source_definition(definition, &world_proxies.source))
		definition_shapes(&world_proxies.source, set);
	if (slot >= 0)
	{
		world_proxies.entries[slot].used = 1;
		world_proxies.entry_count++;
	}
	return set->count > 0 ? set : NULL;
}

void world_proxies_map_unloaded(void)
{
	free(world_proxies.entries);
	world_proxies.entries = NULL;
	world_proxies.entry_count = 0;
}

/* ---------- a window's shapes */

/* a definition's shape placed on its node as drawn, in the GPU's layout */
static void shape_place(struct world_proxy_shape const *shape, struct world_proxies_frame const *frame,
	struct world_proxy *proxy)
{
	float scale = frame_scale(frame);

	memset(proxy, 0, sizeof(*proxy));
	proxy->v[0][3] = (float)shape->kind;
	frame_point(frame, shape->a, proxy->v[0]);
	switch (shape->kind)
	{
	case _world_proxy_capsule:
		frame_point(frame, shape->b, proxy->v[1]);
		proxy->v[2][0] = shape->radius * scale;
		break;
	case _world_proxy_box:
	{
		/* x and y half axes as vectors, z as an extent along x cross y */
		float z[3], x_cross_y[3];

		frame_vector(frame, shape->axes[0], proxy->v[1]);
		frame_vector(frame, shape->axes[1], proxy->v[2]);
		frame_vector(frame, shape->axes[2], z);
		cross3(proxy->v[1], proxy->v[2], x_cross_y);
		proxy->v[2][3] = length3(z);
		if (dot3(x_cross_y, z) < 0.0f)
		{
			/* (a mirrored frame: x turned round keeps the box) */
			proxy->v[1][0] = -proxy->v[1][0];
			proxy->v[1][1] = -proxy->v[1][1];
			proxy->v[1][2] = -proxy->v[1][2];
		}
		break;
	}
	default:
		memcpy(proxy->v[1], proxy->v[0], sizeof(float) * 3);
		proxy->v[2][0] = shape->radius * scale;
		break;
	}
}

/* the debug view: a circle about a centre in the plane of two directions */
static void view_circle(float const centre[3], float const x[3], float const y[3], float radius, int from, int to,
	unsigned long color)
{
	float previous[3];
	int step, k;

	for (step = from; step <= to; step++)
	{
		float angle = 2.0f * WORLD_PROXIES_PI * (float)step / 16.0f, point[3];

		for (k = 0; k < 3; k++)
			point[k] = centre[k] + radius * (cosf(angle) * x[k] + sinf(angle) * y[k]);
		if (step > from)
			world_proxies_source_line(previous, point, color);
		memcpy(previous, point, sizeof(point));
	}
}

static void view_basis(float const axis[3], float x[3], float y[3])
{
	float const up[3] = { 0.0f, 0.0f, 1.0f }, side[3] = { 1.0f, 0.0f, 0.0f };
	float length;
	int k;

	cross3(axis, fabsf(axis[2]) < 0.9f ? up : side, x);
	length = length3(x);
	for (k = 0; k < 3; k++)
		x[k] /= length;
	cross3(axis, x, y);
}

static void view_proxy(struct world_proxy const *proxy)
{
	/* (a vehicle's occlusion field, and its slots after the first: none drawn) */
	if ((int)proxy->v[0][3] < _world_proxy_sphere || (int)proxy->v[0][3] > _world_proxy_box)
		return;
	float const x_axis[3] = { 1.0f, 0.0f, 0.0f }, y_axis[3] = { 0.0f, 1.0f, 0.0f }, z_axis[3] = { 0.0f, 0.0f, 1.0f };
	int k;

	switch ((int)proxy->v[0][3])
	{
	case _world_proxy_sphere:
		view_circle(proxy->v[0], x_axis, y_axis, proxy->v[2][0], 0, 16, WORLD_PROXIES_SPHERE_COLOR);
		view_circle(proxy->v[0], x_axis, z_axis, proxy->v[2][0], 0, 16, WORLD_PROXIES_SPHERE_COLOR);
		view_circle(proxy->v[0], y_axis, z_axis, proxy->v[2][0], 0, 16, WORLD_PROXIES_SPHERE_COLOR);
		break;
	case _world_proxy_capsule:
	{
		float axis[3], x[3], y[3], length, radius = proxy->v[2][0];
		int side;

		sub3(proxy->v[1], proxy->v[0], axis);
		length = length3(axis);
		if (length <= 0.0f)
			break;
		for (k = 0; k < 3; k++)
			axis[k] /= length;
		view_basis(axis, x, y);
		view_circle(proxy->v[0], x, y, radius, 0, 16, WORLD_PROXIES_CAPSULE_COLOR);
		view_circle(proxy->v[1], x, y, radius, 0, 16, WORLD_PROXIES_CAPSULE_COLOR);
		/* (the caps' half circles, the side lines) */
		view_circle(proxy->v[1], x, axis, radius, 0, 8, WORLD_PROXIES_CAPSULE_COLOR);
		view_circle(proxy->v[1], y, axis, radius, 0, 8, WORLD_PROXIES_CAPSULE_COLOR);
		view_circle(proxy->v[0], x, axis, radius, 8, 16, WORLD_PROXIES_CAPSULE_COLOR);
		view_circle(proxy->v[0], y, axis, radius, 8, 16, WORLD_PROXIES_CAPSULE_COLOR);
		for (side = 0; side < 4; side++)
		{
			float a[3], b[3], sx = side == 0 ? 1.0f : side == 1 ? -1.0f : 0.0f, sy = side == 2 ? 1.0f : side == 3 ? -1.0f : 0.0f;

			for (k = 0; k < 3; k++)
			{
				a[k] = proxy->v[0][k] + radius * (sx * x[k] + sy * y[k]);
				b[k] = proxy->v[1][k] + radius * (sx * x[k] + sy * y[k]);
			}
			world_proxies_source_line(a, b, WORLD_PROXIES_CAPSULE_COLOR);
		}
		break;
	}
	case _world_proxy_box:
	{
		float axes[3][3], corners[8][3], extent_x = length3(proxy->v[1]), extent_y = length3(proxy->v[2]);
		int corner, edge;

		if (extent_x <= 0.0f || extent_y <= 0.0f)
			break;
		cross3(proxy->v[1], proxy->v[2], axes[2]);
		for (k = 0; k < 3; k++)
		{
			axes[0][k] = proxy->v[1][k];
			axes[1][k] = proxy->v[2][k];
			axes[2][k] *= proxy->v[2][3] / (extent_x * extent_y);
		}
		for (corner = 0; corner < 8; corner++)
		{
			for (k = 0; k < 3; k++)
			{
				corners[corner][k] = proxy->v[0][k] + ((corner & 1) ? 1.0f : -1.0f) * axes[0][k] +
					((corner & 2) ? 1.0f : -1.0f) * axes[1][k] + ((corner & 4) ? 1.0f : -1.0f) * axes[2][k];
			}
		}
		for (corner = 0; corner < 8; corner++)
		{
			for (edge = 1; edge < 8; edge <<= 1)
			{
				if (!(corner & edge))
					world_proxies_source_line(corners[corner], corners[corner | edge], WORLD_PROXIES_BOX_COLOR);
			}
		}
		break;
	}
	default:
		break;
	}
}

/* whether a shape goes before a candidate kept: the dynamic objects'
first, then the nearer */
static int candidate_before(int baked, float distance, struct world_proxies_candidate const *candidate)
{
	return baked != candidate->baked ? !baked : distance < candidate->distance;
}

/* a candidate kept, nearest first (the dynamic objects' first), the
farthest dropped while the window's slots are short; FALSE if it is not */
static int candidate_add(struct world_proxy const *proxies, short slots, long owner, float distance, int baked,
	long maximum)
{
	long i;

	while (world_proxies.candidate_slots + slots > maximum && world_proxies.candidate_count > 0 &&
		candidate_before(baked, distance, &world_proxies.candidates[world_proxies.candidate_count - 1]))
	{
		world_proxies.candidate_slots -= world_proxies.candidates[--world_proxies.candidate_count].slots;
	}
	if (world_proxies.candidate_slots + slots > maximum || world_proxies.candidate_count >= WORLD_PROXY_MAXIMUM_PER_WINDOW)
		return 0;
	for (i = world_proxies.candidate_count; i > 0 && candidate_before(baked, distance, &world_proxies.candidates[i - 1]); i--)
		world_proxies.candidates[i] = world_proxies.candidates[i - 1];
	memcpy(world_proxies.candidates[i].proxies, proxies, sizeof(*proxies) * (size_t)slots);
	world_proxies.candidates[i].slots = slots;
	world_proxies.candidates[i].owner = owner;
	world_proxies.candidates[i].distance = distance;
	world_proxies.candidates[i].baked = baked;
	world_proxies.candidate_count++;
	world_proxies.candidate_slots += slots;
	return 1;
}

/* (world_proxies.h) */
void world_proxies_field_place(struct world_proxies_source_field const *field, struct world_proxies_frame const *frame,
	struct world_proxy proxies[WORLD_PROXY_FIELD_SLOTS])
{
	float scale = frame_scale(frame), middle[3], over;
	int axis, k;

	memset(proxies, 0, sizeof(*proxies) * WORLD_PROXY_FIELD_SLOTS);
	scale = scale > 0.0f ? scale : 1.0f;
	over = 1.0f / (scale * scale * field->voxel);
	for (axis = 0; axis < 3; axis++)
	{
		float *row = axis == 0 ? proxies[0].v[1] : axis == 1 ? proxies[1].v[0] : proxies[1].v[1];

		for (k = 0; k < 3; k++)
			row[k] = frame->axes[axis][k] * over;
		proxies[0].v[2][axis] = -dot3(frame->axes[axis], frame->position) * over - field->origin[axis] / field->voxel;
		middle[axis] = field->origin[axis] + 0.5f * field->voxel * (float)(field->size[axis] - 1);
		proxies[1].v[2][axis] = (float)(field->size[axis] - 1);
		proxies[2].v[0][axis] = (float)field->place[axis] + 0.5f;
		proxies[2].v[1][axis] = 1.0f / (float)(field->atlas[axis] ? field->atlas[axis] : 1);
	}
	frame_point(frame, middle, proxies[0].v[0]);
	proxies[0].v[0][3] = (float)_world_proxy_field;
	/* (its geometry's box among its voxels, whose bound is its own: its
	half extents, about the grid's middle, which world_sdf_object_layout
	centres over it) */
	{
		float extents[3], areas, box_reach;

		for (axis = 0; axis < 3; axis++)
		{
			extents[axis] = 0.5f * (field->bounds[3 + axis] - field->bounds[axis]) / field->voxel;
			proxies[2].v[2][axis] = extents[axis];
		}
		/* (its reach: the field's, or the box's (world_proxy_reach's), where
		less) */
		for (axis = 0; axis < 3; axis++)
			extents[axis] *= field->voxel * scale;
		areas = sqrtf((extents[1] * extents[2]) * (extents[1] * extents[2]) + (extents[0] * extents[2]) *
			(extents[0] * extents[2]) + (extents[0] * extents[1]) * (extents[0] * extents[1]));
		box_reach = sqrtf(dot3(extents, extents)) + sqrtf(WORLD_PROXIES_LEAST * 4.0f / WORLD_PROXIES_PI * areas);
		proxies[0].v[2][3] = field->reach * field->voxel * scale;
		proxies[0].v[2][3] = box_reach < proxies[0].v[2][3] ? box_reach : proxies[0].v[2][3];
	}
}

long world_proxies_place(long definition, struct world_proxies_frame const *frames, short node_count,
	struct world_proxy *proxies, long maximum)
{
	struct world_proxy_set const *set = world_proxies_definition(definition);
	long count = 0;
	short shape_index;

	for (shape_index = 0; set && node_count > 0 && shape_index < set->count && count < maximum; shape_index++)
	{
		struct world_proxy_shape const *shape = &set->shapes[shape_index];

		shape_place(shape, &frames[shape->node_index >= 0 && shape->node_index < node_count ? shape->node_index : 0],
			&proxies[count++]);
	}
	return count;
}

int world_proxy_set_fielded(struct world_proxy_set const *set, short shape_index)
{
	/* (a vehicle's boxes, not its wheels' spheres; anything else's one shape,
	a box or, with no collision geometry, a sphere of its bound) */
	return set->shapes[shape_index].kind == _world_proxy_box || set->count == 1;
}

long world_proxies_scene(float const camera_position[3], float reach, struct world_proxy *proxies, long *owners,
	long maximum)
{
	long instance_count, instance_index, count = 0;

	if (maximum <= 0 || !proxies || !owners)
		return 0;
	instance_count = world_proxies_source_instances(world_proxies.instances, WORLD_PROXIES_MAXIMUM_INSTANCES);
	for (instance_index = 0; instance_index < instance_count && count < maximum; instance_index++)
	{
		struct world_proxies_source_instance const *instance = &world_proxies.instances[instance_index];
		struct world_proxy_set const *set = world_proxies_definition(instance->definition);
		struct world_proxies_source_field field;
		short node_count, shape_index;
		int fielded = 0;

		/* (a static placement the field holds is the structure's) */
		if (!set || instance->baked)
			continue;
		node_count = world_proxies_source_nodes(instance->id, world_proxies.frames, WORLD_PROXIES_SOURCE_MAXIMUM_NODES);
		if (node_count <= 0)
			continue;
		/* (an occlusion field, drawn or on its way, stands in for the shapes
		it is fielded for: not a distance, so neither is here) */
		for (shape_index = 0; shape_index < set->count; shape_index++)
			fielded = fielded || world_proxy_set_fielded(set, shape_index);
		fielded = fielded && (instance->field_awaited || world_proxies_source_field(instance->definition, &field));
		for (shape_index = 0; shape_index < set->count && count < maximum; shape_index++)
		{
			struct world_proxy_shape const *shape = &set->shapes[shape_index];
			float centre[3], offset[3];

			if (fielded && world_proxy_set_fielded(set, shape_index))
				continue;
			shape_place(shape, &world_proxies.frames[shape->node_index >= 0 && shape->node_index < node_count ?
				shape->node_index : 0], &proxies[count]);
			world_proxy_reach_centre(&proxies[count], centre);
			sub3(centre, camera_position, offset);
			if (length3(offset) > reach)
				continue;
			owners[count++] = instance->id;
		}
	}
	return count;
}

long world_proxies_window(float const camera_position[3], struct world_proxy *proxies, long *owners,
	long maximum, long *dynamic_count)
{
	float planes[4][4];
	int frustum;
	long instance_count, instance_index, count = 0, slots, i, j, c;

	if (maximum > WORLD_PROXY_MAXIMUM_PER_WINDOW)
		maximum = WORLD_PROXY_MAXIMUM_PER_WINDOW;
	*dynamic_count = 0;
	if (maximum <= 0 || !proxies || !owners)
		return 0;
	frustum = world_proxies_source_frustum(planes);
	world_proxies.candidate_count = world_proxies.candidate_slots = 0;
	instance_count = world_proxies_source_instances(world_proxies.instances, WORLD_PROXIES_MAXIMUM_INSTANCES);
	for (instance_index = 0; instance_index < instance_count; instance_index++)
	{
		struct world_proxies_source_instance const *instance = &world_proxies.instances[instance_index];
		struct world_proxy_set const *set = world_proxies_definition(instance->definition);
		struct world_proxies_source_field field;
		short node_count, shape_index;
		/* (an object with an occlusion field drawn: the field in place of the
		shapes it stands in for (world_proxy_set_fielded): a vehicle's boxes,
		whose wheels keep their spheres, which turn and spring; a piece of
		scenery's box or sphere, whether it moves or its placement is baked.
		One whose field is on its way: neither those shapes nor a field until
		it comes, faded in) */
		int fielded = 0, boxless;

		if (!set)
			continue;
		node_count = world_proxies_source_nodes(instance->id, world_proxies.frames, WORLD_PROXIES_SOURCE_MAXIMUM_NODES);
		if (node_count <= 0)
			continue;
		for (shape_index = 0; shape_index < set->count; shape_index++)
			fielded = fielded || world_proxy_set_fielded(set, shape_index);
		boxless = fielded && instance->field_awaited;
		fielded = fielded && !boxless && world_proxies_source_field(instance->definition, &field);
		boxless = boxless || fielded;
		for (shape_index = fielded ? -1 : 0; shape_index < set->count; shape_index++)
		{
			struct world_proxy placed[WORLD_PROXY_FIELD_SLOTS];
			float centre[3], offset[3], reach, distance;
			int plane, seen = 1;
			short slot_count = 1;

			if (shape_index < 0)
			{
				world_proxies_field_place(&field, &world_proxies.frames[0], placed);
				slot_count = WORLD_PROXY_FIELD_SLOTS;
			}
			else
			{
				struct world_proxy_shape const *shape = &set->shapes[shape_index];

				if (boxless && world_proxy_set_fielded(set, shape_index))
					continue;
				shape_place(shape, &world_proxies.frames[shape->node_index >= 0 && shape->node_index < node_count ?
					shape->node_index : 0], &placed[0]);
			}
			/* its reach's sphere against the frustum's sides */
			world_proxy_reach_centre(&placed[0], centre);
			reach = world_proxy_reach(&placed[0]);
			for (plane = 0; frustum && plane < 4 && seen; plane++)
			{
				if (dot3(planes[plane], centre) - planes[plane][3] > reach)
					seen = 0;
			}
			if (!seen)
				continue;
			/* the nearest kept, nearest first: the dynamic objects' first, the
			static scenery's the field holds after them (world_sdf_scenery_baked:
			the lightmap pass reads its pages, the objects the field, and only
			a page without a layer walks them) */
			sub3(centre, camera_position, offset);
			distance = length3(offset);
			candidate_add(placed, slot_count, instance->id, distance, instance->baked, maximum);
		}
	}
	for (c = 0; c < world_proxies.candidate_count; c++)
	{
		struct world_proxies_candidate const *candidate = &world_proxies.candidates[c];

		for (j = 0; j < candidate->slots; j++, count++)
		{
			proxies[count] = candidate->proxies[j];
			owners[count] = candidate->owner;
			*dynamic_count += !candidate->baked;
		}
	}
	/* each owner's slot: the order its first shape has in the window (so
	the shader leaves an object's own shapes out: world_ao.c gives a draw
	its object's slot, or -1) */
	slots = 0;
	for (i = 0; i < count; i++)
	{
		for (j = 0; j < i && owners[j] != owners[i]; j++)
			;
		proxies[i].v[1][3] = j < i ? proxies[j].v[1][3] : (float)slots++;
	}
	if (world_proxies_source_view())
	{
		for (i = 0; i < count; i++)
			view_proxy(&proxies[i]);
	}
	return count;
}
