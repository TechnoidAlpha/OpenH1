/* display.ambient_occlusion "world"'s object proxies (port/linux/game/world_proxies.c,
test_world_proxies.py): each shape's analytic occlusion (the shaders' reference) against a numerical integral of the
cosine-weighted hemisphere, its reach, a window's culling, order and owner slots, the shapes made from a fake
skeleton and placed on its nodes as they move, and a vehicle's occlusion field in place of its boxes (its three
slots together, its wheels' spheres kept, its place, its reach, its boxes where it has none). */

#include "harness.h"

#include <math.h>

#include "under_test.inc"

/* ---------- the fake world (world_proxies_source.h) */

#define FAKE_INSTANCES 256

/* (world_proxies_window's count of the dynamic objects' shapes) */
static long dynamic_count;
static struct
{
	struct world_proxies_source sources[8]; /* by definition */
	int source_known[8];
	struct world_proxies_source_instance instances[FAKE_INSTANCES];
	long instance_count;
	/* each instance's nodes (by its index in instances) */
	struct world_proxies_frame frames[FAKE_INSTANCES][4];
	short frame_counts[FAKE_INSTANCES];
	int frustum;
	float planes[4][4];
	long lines;
	long definitions_read;
} fake;

int world_proxies_source_definition(long definition, struct world_proxies_source *source)
{
	long index = definition & 0xFFFF; /* (a tag index's absolute index; its salt above) */

	fake.definitions_read++;
	if (definition == -1 || index >= 8 || !fake.source_known[index])
		return 0;
	*source = fake.sources[index];
	return 1;
}

long world_proxies_source_instances(struct world_proxies_source_instance *instances, long maximum)
{
	long count = fake.instance_count < maximum ? fake.instance_count : maximum;

	memcpy(instances, fake.instances, sizeof(*instances) * (size_t)count);
	return count;
}

short world_proxies_source_nodes(long id, struct world_proxies_frame *frames, short maximum)
{
	long index;

	for (index = 0; index < fake.instance_count; index++)
	{
		if (fake.instances[index].id == id)
		{
			short count = fake.frame_counts[index] < maximum ? fake.frame_counts[index] : maximum;

			memcpy(frames, fake.frames[index], sizeof(*frames) * (size_t)count);
			return count;
		}
	}
	return 0;
}

int world_proxies_source_frustum(float planes[4][4])
{
	memcpy(planes, fake.planes, sizeof(fake.planes));
	return fake.frustum;
}

/* (an occlusion field: a vehicle's, of definition 3, and a piece of
scenery's, of definition 5, where field_known) */
static int field_known;
static struct world_proxies_source_field fake_field;

int world_proxies_source_field(long definition, struct world_proxies_source_field *field)
{
	if (!field_known || ((definition & 0xFFFF) != 3 && (definition & 0xFFFF) != 5))
		return 0;
	*field = fake_field;
	return 1;
}

int world_proxies_source_view(void)
{
	return 1;
}

void world_proxies_source_line(float const a[3], float const b[3], unsigned long color)
{
	(void)a;
	(void)b;
	(void)color;
	fake.lines++;
}

static void identity(struct world_proxies_frame *frame, float x, float y, float z)
{
	memset(frame, 0, sizeof(*frame));
	frame->axes[0][0] = frame->axes[1][1] = frame->axes[2][2] = 1.0f;
	frame->position[0] = x;
	frame->position[1] = y;
	frame->position[2] = z;
}

/* ---------- the numerical integral */

static unsigned long random_state = 12345;

static double uniform(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (double)((random_state >> 8) & 0xFFFFFF) / (double)0x1000000;
}

static void random_direction(float out[3])
{
	double x, y, z, length;

	do
	{
		x = uniform() * 2.0 - 1.0;
		y = uniform() * 2.0 - 1.0;
		z = uniform() * 2.0 - 1.0;
		length = x * x + y * y + z * z;
	} while (length > 1.0 || length < 0.01);
	length = sqrt(length);
	out[0] = (float)(x / length);
	out[1] = (float)(y / length);
	out[2] = (float)(z / length);
}

static double ddot(double const a[3], double const b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* whether a ray from o along d (unit) meets the shape at t > 0 */
static int ray_sphere(double const o[3], double const d[3], double const c[3], double r)
{
	double oc[3] = { o[0] - c[0], o[1] - c[1], o[2] - c[2] };
	double b = ddot(oc, d), cc = ddot(oc, oc) - r * r, h = b * b - cc;

	return h >= 0.0 && -b + sqrt(h) > 0.0 && (cc < 0.0 || -b - sqrt(h) > 0.0);
}

static int ray_capsule(double const o[3], double const d[3], double const a[3], double const b[3], double r)
{
	double ba[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, oa[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
	double baba = ddot(ba, ba), bard = ddot(ba, d), baoa = ddot(ba, oa), rdoa = ddot(d, oa), oaoa = ddot(oa, oa);
	double qa = baba - bard * bard, qb = baba * rdoa - baoa * bard, qc = baba * oaoa - baoa * baoa - r * r * baba;
	double h = qb * qb - qa * qc;

	if (qa > 1.0e-12 && h >= 0.0)
	{
		double t = (-qb - sqrt(h)) / qa, y = baoa + t * bard;

		if (t > 0.0 && y > 0.0 && y < baba)
			return 1;
	}
	return ray_sphere(o, d, a, r) || ray_sphere(o, d, b, r);
}

static int ray_box(double const o[3], double const d[3], double const centre[3], double const axes[3][3],
	double const extents[3])
{
	double near = -1.0e30, far = 1.0e30;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		double offset[3] = { o[0] - centre[0], o[1] - centre[1], o[2] - centre[2] };
		double origin = ddot(offset, axes[axis]), direction = ddot(d, axes[axis]);

		if (fabs(direction) < 1.0e-12)
		{
			if (fabs(origin) > extents[axis])
				return 0;
			continue;
		}
		{
			double t0 = (-extents[axis] - origin) / direction, t1 = (extents[axis] - origin) / direction;

			if (t0 > t1)
			{
				double swap = t0;

				t0 = t1;
				t1 = swap;
			}
			near = t0 > near ? t0 : near;
			far = t1 < far ? t1 : far;
		}
	}
	return near <= far && far > 0.0;
}

/* the share of p's cosine-weighted hemisphere the proxy blocks: N x N
stratified directions (Malley's: a disc's uniform points lifted) */
static double integral(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	enum { N = 256 };
	double o[3] = { point[0], point[1], point[2] }, n[3] = { normal[0], normal[1], normal[2] }, t1[3], t2[3];
	double a[3], b[3], axes[3][3], extents[3];
	long hits = 0;
	int i, j, k, kind = (int)proxy->v[0][3];

	for (k = 0; k < 3; k++)
	{
		a[k] = proxy->v[0][k];
		b[k] = proxy->v[1][k];
	}
	if (kind == _world_proxy_box)
	{
		extents[0] = sqrt(ddot(b, b));
		for (k = 0; k < 3; k++)
		{
			axes[0][k] = b[k] / extents[0];
			axes[1][k] = proxy->v[2][k];
		}
		extents[1] = sqrt(ddot(axes[1], axes[1]));
		for (k = 0; k < 3; k++)
			axes[1][k] /= extents[1];
		axes[2][0] = axes[0][1] * axes[1][2] - axes[0][2] * axes[1][1];
		axes[2][1] = axes[0][2] * axes[1][0] - axes[0][0] * axes[1][2];
		axes[2][2] = axes[0][0] * axes[1][1] - axes[0][1] * axes[1][0];
		extents[2] = proxy->v[2][3];
	}
	if (fabs(n[0]) < 0.9)
	{
		t1[0] = 0.0, t1[1] = n[2], t1[2] = -n[1];
	}
	else
	{
		t1[0] = -n[2], t1[1] = 0.0, t1[2] = n[0];
	}
	{
		double length = sqrt(ddot(t1, t1));

		for (k = 0; k < 3; k++)
			t1[k] /= length;
	}
	t2[0] = n[1] * t1[2] - n[2] * t1[1];
	t2[1] = n[2] * t1[0] - n[0] * t1[2];
	t2[2] = n[0] * t1[1] - n[1] * t1[0];
	for (i = 0; i < N; i++)
	{
		for (j = 0; j < N; j++)
		{
			double u1 = (i + 0.5) / N, u2 = (j + 0.5) / N, radius = sqrt(u1), angle = 2.0 * M_PI * u2;
			double x = radius * cos(angle), y = radius * sin(angle), z = sqrt(1.0 - u1), d[3];
			int hit;

			for (k = 0; k < 3; k++)
				d[k] = x * t1[k] + y * t2[k] + z * n[k];
			if (kind == _world_proxy_sphere)
				hit = ray_sphere(o, d, a, proxy->v[2][0]);
			else if (kind == _world_proxy_capsule)
				hit = ray_capsule(o, d, a, b, proxy->v[2][0]);
			else
				hit = ray_box(o, d, a, (double const (*)[3])axes, extents);
			hits += hit;
		}
	}
	return (double)hits / (double)(N * N);
}

/* ---------- the proxies */

static struct world_proxy sphere(float x, float y, float z, float radius)
{
	struct world_proxy proxy;

	memset(&proxy, 0, sizeof(proxy));
	proxy.v[0][0] = proxy.v[1][0] = x;
	proxy.v[0][1] = proxy.v[1][1] = y;
	proxy.v[0][2] = proxy.v[1][2] = z;
	proxy.v[0][3] = (float)_world_proxy_sphere;
	proxy.v[2][0] = radius;
	return proxy;
}

static struct world_proxy capsule(float const a[3], float const b[3], float radius)
{
	struct world_proxy proxy;

	memset(&proxy, 0, sizeof(proxy));
	memcpy(proxy.v[0], a, sizeof(float) * 3);
	memcpy(proxy.v[1], b, sizeof(float) * 3);
	proxy.v[0][3] = (float)_world_proxy_capsule;
	proxy.v[2][0] = radius;
	return proxy;
}

/* a box of random turn and size about the origin */
static struct world_proxy random_box(void)
{
	struct world_proxy proxy;
	float x[3], y[3], length, along;
	int k;

	memset(&proxy, 0, sizeof(proxy));
	random_direction(x);
	random_direction(y);
	along = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
	for (k = 0; k < 3; k++)
		y[k] -= along * x[k];
	length = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	for (k = 0; k < 3; k++)
		y[k] /= length;
	proxy.v[0][3] = (float)_world_proxy_box;
	{
		float ex = 0.2f + 1.8f * (float)uniform(), ey = 0.2f + 1.8f * (float)uniform();

		for (k = 0; k < 3; k++)
		{
			proxy.v[1][k] = ex * x[k];
			proxy.v[2][k] = ey * y[k];
		}
	}
	proxy.v[2][3] = 0.2f + 1.8f * (float)uniform();
	return proxy;
}

/* a point's distance from a shape's surface (exact: a capsule's from its
segment less the radius, a box's from its faces; 0 inside) */
static double surface_distance(struct world_proxy const *proxy, float const point[3])
{
	double d[3], distance;
	int k;

	for (k = 0; k < 3; k++)
		d[k] = point[k] - proxy->v[0][k];
	if ((int)proxy->v[0][3] == _world_proxy_box)
	{
		double x[3], y[3], z[3], ex, ey, q[3], outside = 0.0;

		for (k = 0; k < 3; k++)
		{
			x[k] = proxy->v[1][k];
			y[k] = proxy->v[2][k];
		}
		ex = sqrt(ddot(x, x));
		ey = sqrt(ddot(y, y));
		for (k = 0; k < 3; k++)
		{
			x[k] /= ex;
			y[k] /= ey;
		}
		z[0] = x[1] * y[2] - x[2] * y[1];
		z[1] = x[2] * y[0] - x[0] * y[2];
		z[2] = x[0] * y[1] - x[1] * y[0];
		q[0] = fabs(ddot(d, x)) - ex;
		q[1] = fabs(ddot(d, y)) - ey;
		q[2] = fabs(ddot(d, z)) - proxy->v[2][3];
		for (k = 0; k < 3; k++)
			outside += q[k] > 0.0 ? q[k] * q[k] : 0.0;
		return sqrt(outside);
	}
	{
		double ab[3], t;

		for (k = 0; k < 3; k++)
			ab[k] = proxy->v[1][k] - proxy->v[0][k];
		t = ddot(ab, ab) > 0.0 ? ddot(d, ab) / ddot(ab, ab) : 0.0;
		t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
		for (k = 0; k < 3; k++)
			d[k] -= t * ab[k];
		distance = sqrt(ddot(d, d)) - proxy->v[2][0];
		return distance > 0.0 ? distance : 0.0;
	}
}

/* a point gap from the shape's surface, out from its centre along a random
direction */
static void point_off(struct world_proxy const *proxy, float gap, float point[3])
{
	float direction[3], centre[3], low = 0.0f, high = 1.0f;
	int k, step;

	world_proxy_reach_centre(proxy, centre);
	random_direction(direction);
	for (;;)
	{
		for (k = 0; k < 3; k++)
			point[k] = centre[k] + high * direction[k];
		if (surface_distance(proxy, point) >= gap)
			break;
		high *= 2.0f;
	}
	for (step = 0; step < 40; step++)
	{
		float middle = 0.5f * (low + high);

		for (k = 0; k < 3; k++)
			point[k] = centre[k] + middle * direction[k];
		if (surface_distance(proxy, point) >= gap)
			high = middle;
		else
			low = middle;
	}
	for (k = 0; k < 3; k++)
		point[k] = centre[k] + high * direction[k];
}

/* the worst error of a kind's occlusion against the integral, over random
points gap_low to gap_high (in the shape's size) off it, random normals */
static double worst_error(struct world_proxy (*make)(void), float gap_low, float gap_high, int count)
{
	double worst = 0.0;
	int index;

	for (index = 0; index < count; index++)
	{
		struct world_proxy proxy = make();
		float point[3], normal[3], gap = gap_low + (gap_high - gap_low) * (float)(uniform() * uniform());
		double reference, error;
		float occlusion;

		point_off(&proxy, gap, point);
		random_direction(normal);
		reference = integral(&proxy, point, normal);
		occlusion = world_proxy_occlusion(&proxy, point, normal);
		CHECK(occlusion >= 0.0f && occlusion <= 1.0f, "%g", occlusion);
		error = fabs(occlusion - reference);
		if (error > worst)
			worst = error;
	}
	return worst;
}

static struct world_proxy unit_sphere(void)
{
	return sphere(0.0f, 0.0f, 0.0f, 1.0f);
}

static struct world_proxy random_capsule(void)
{
	float a[3] = { 0.0f, 0.0f, 0.0f }, b[3];
	float length = 0.05f + 6.0f * (float)uniform();
	int k;

	random_direction(b);
	for (k = 0; k < 3; k++)
		b[k] *= length;
	return capsule(a, b, 1.0f);
}

static void sphere_case(void)
{
	float const origin[3] = { 0.0f, 0.0f, 0.0f };
	float const up[3] = { 0.0f, 0.0f, 1.0f };
	float const down[3] = { 0.0f, 0.0f, -1.0f };
	struct world_proxy above = sphere(0.0f, 0.0f, 2.0f, 1.0f);
	struct world_proxy beside = sphere(2.0f, 0.0f, 0.0f, 1.0f);
	float near_occlusion = world_proxy_occlusion(&above, origin, up);
	double worst;

	/* straight above at twice its radius: (r / l)^2 = 1/4 */
	CHECK(fabsf(near_occlusion - 0.25f) < 1.0e-5f, "%g", near_occlusion);
	/* behind the surface: none */
	CHECK(world_proxy_occlusion(&above, origin, down) == 0.0f, "behind");
	/* on the horizon: half of it above it counts */
	CHECK(fabs(world_proxy_occlusion(&beside, origin, up) - integral(&beside, origin, up)) < 0.01, "horizon %g %g",
		world_proxy_occlusion(&beside, origin, up), integral(&beside, origin, up));
	/* everywhere: within 0.01 of the integral (it is exact) */
	worst = worst_error(unit_sphere, 0.02f, 8.0f, 60);
	CHECK(worst < 0.01, "sphere's worst error %g", worst);
}

static void capsule_case(void)
{
	double worst_far, worst_near;
	float const origin[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 0.0f, 1.0f };
	float const a[3] = { -50.0f, 0.0f, 2.0f }, b[3] = { 50.0f, 0.0f, 2.0f };
	struct world_proxy rail = capsule(a, b, 1.0f);
	float occlusion = world_proxy_occlusion(&rail, origin, up);

	/* a long tube straight above, facing it: r / h exactly */
	CHECK(fabsf(occlusion - 0.5f) < 0.01f, "the tube %g", occlusion);
	/* a radius or more off its surface: within 0.05 of the integral */
	worst_far = worst_error(random_capsule, 1.0f, 8.0f, 60);
	CHECK(worst_far < 0.05, "capsule's worst error past a radius %g", worst_far);
	/* nearer, within 0.15 (a line 2r wide is a tube only from afar) */
	worst_near = worst_error(random_capsule, 0.05f, 1.0f, 60);
	CHECK(worst_near < 0.15, "capsule's worst error near %g", worst_near);
	printf("capsule: worst %.3f past a radius, %.3f nearer\n", worst_far, worst_near);
}

static void box_case(void)
{
	float const origin[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 0.0f, 1.0f };
	struct world_proxy ceiling;
	double worst;
	float occlusion;

	/* a wide slab straight above, its near face 1 above: nearly all of it */
	memset(&ceiling, 0, sizeof(ceiling));
	ceiling.v[0][2] = 1.5f;
	ceiling.v[0][3] = (float)_world_proxy_box;
	ceiling.v[1][0] = 1000.0f;
	ceiling.v[2][1] = 1000.0f;
	ceiling.v[2][3] = 0.5f;
	occlusion = world_proxy_occlusion(&ceiling, origin, up);
	CHECK(occlusion > 0.99f, "the ceiling %g", occlusion);
	/* inside: all */
	CHECK(world_proxy_occlusion(&ceiling, ceiling.v[0], up) == 1.0f, "inside");
	/* everywhere: within 0.02 of the integral (the faces' form factor is
	exact) */
	worst = worst_error(random_box, 0.02f, 8.0f, 80);
	CHECK(worst < 0.02, "box's worst error %g", worst);
}

/* past its reach, every kind's occlusion is under 1/255 in every
direction, its normal facing the shape */
static void reach_case(void)
{
	int index;

	for (index = 0; index < 300; index++)
	{
		struct world_proxy proxy = index % 3 == 0 ? unit_sphere() : index % 3 == 1 ? random_capsule() : random_box();
		float centre[3], direction[3], point[3], normal[3], reach = world_proxy_reach(&proxy), occlusion;
		int k;

		world_proxy_reach_centre(&proxy, centre);
		random_direction(direction);
		for (k = 0; k < 3; k++)
		{
			point[k] = centre[k] + reach * 1.001f * direction[k];
			normal[k] = -direction[k];
		}
		occlusion = world_proxy_occlusion(&proxy, point, normal);
		CHECK(occlusion < 1.0f / 255.0f, "kind %d at its reach %g: %g", (int)proxy.v[0][3], reach, occlusion);
	}
	/* (and a sphere's reach is 16 radii) */
	{
		struct world_proxy far = sphere(0.0f, 0.0f, 20.0f, 1.0f);

		CHECK(world_proxy_reach(&far) == 16.0f, "%g", world_proxy_reach(&far));
	}
}

/* a box's fade is its bound on its occlusion U made a ramp (saturate(510 U
- 1)): never under the occlusion. Where the fade is 0 the occlusion is
under 1/510, and where it is under 1 the occlusion is at most
(1 + fade) / 510; at random points about random boxes, near and far,
facing anywhere */
static void bound_case(void)
{
	int index, faded = 0, kept = 0;
	float worst = 0.0f;

	for (index = 0; index < 20000; index++)
	{
		struct world_proxy proxy = random_box();
		float point[3], normal[3], occlusion, fade, gap = 0.01f + 40.0f * (float)(uniform() * uniform());

		point_off(&proxy, gap, point);
		random_direction(normal);
		occlusion = world_proxy_occlusion(&proxy, point, normal);
		fade = world_proxy_box_fade(&proxy, point, normal);
		if (fade < 1.0f)
		{
			float over = occlusion * 510.0f - (1.0f + fade);

			worst = over > worst ? over : worst;
			faded++;
		}
		else
		{
			kept++;
		}
	}
	CHECK(worst <= 1.0e-3f, "the box's occlusion past its bound by %g / 510", worst);
	CHECK(faded > 1000 && kept > 1000, "both sides seen: %d faded, %d kept", faded, kept);
	/* inside: 1 */
	{
		struct world_proxy proxy = random_box();
		float up[3] = { 0.0f, 0.0f, 1.0f };

		CHECK(world_proxy_box_fade(&proxy, proxy.v[0], up) == 1.0f, "inside");
	}
}

/* past 6 half diagonals a box is its faces' patches: where the form
changes (just inside: the exact faces; just outside: the patches) the two
are within 0.2/255, facing anywhere */
static void far_case(void)
{
	int index;
	float worst = 0.0f;

	for (index = 0; index < 20000; index++)
	{
		struct world_proxy proxy = random_box();
		float direction[3], normal[3], near_point[3], far_point[3], difference;
		float half = sqrtf(proxy.v[1][0] * proxy.v[1][0] + proxy.v[1][1] * proxy.v[1][1] + proxy.v[1][2] * proxy.v[1][2] +
			proxy.v[2][0] * proxy.v[2][0] + proxy.v[2][1] * proxy.v[2][1] + proxy.v[2][2] * proxy.v[2][2] +
			proxy.v[2][3] * proxy.v[2][3]);
		int k;

		random_direction(direction);
		random_direction(normal);
		for (k = 0; k < 3; k++)
		{
			near_point[k] = proxy.v[0][k] + direction[k] * half * 5.999f;
			far_point[k] = proxy.v[0][k] + direction[k] * half * 6.001f;
		}
		difference = fabsf(world_proxy_occlusion(&proxy, near_point, normal) -
			world_proxy_occlusion(&proxy, far_point, normal));
		worst = difference > worst ? difference : worst;
	}
	CHECK(worst * 255.0f < 0.2f, "the box's patches past 6 half diagonals %g / 255 from its faces", worst * 255.0f);
}

/* ---------- windows */

/* definition 0: one sphere of radius 0.5 on node 0 (a sphere class) */
static void one_sphere_definition(void)
{
	memset(&fake.sources[0], 0, sizeof(fake.sources[0]));
	fake.sources[0].shape_class = _world_proxies_source_sphere;
	fake.sources[0].node_count = 1;
	fake.sources[0].bounding_radius = 0.5f;
	fake.sources[0].nodes[0].parent = -1;
	identity(&fake.sources[0].nodes[0].default_frame, 0.0f, 0.0f, 0.0f);
	fake.source_known[0] = 1;
}

static void add_instance(long id, long definition, float x, float y, float z)
{
	long index = fake.instance_count++;

	fake.instances[index].id = id;
	fake.instances[index].definition = definition;
	fake.instances[index].baked = 0;
	fake.instances[index].field_awaited = 0;
	identity(&fake.frames[index][0], x, y, z);
	fake.frame_counts[index] = 1;
}

/* a frustum looking along +x from the origin, 90 degrees wide and high */
static void frustum_along_x(void)
{
	float const s = 0.70710678f;
	float const planes[4][4] = {
		{ -s, s, 0.0f, 0.0f }, /* left (+y side) */
		{ -s, -s, 0.0f, 0.0f }, /* right */
		{ -s, 0.0f, -s, 0.0f }, /* bottom */
		{ -s, 0.0f, s, 0.0f }, /* top */
	};

	memcpy(fake.planes, planes, sizeof(planes));
	fake.frustum = 1;
}

static void culling_case(void)
{
	float const camera[3] = { 0.0f, 0.0f, 0.0f };
	struct world_proxy proxies[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long owners[WORLD_PROXY_MAXIMUM_PER_WINDOW], count, index;

	one_sphere_definition();
	frustum_along_x();
	/* ahead: in; behind, farther than its reach (8): out; behind the camera
	but within reach of what it sees: in */
	add_instance(100, 0, 10.0f, 0.0f, 0.0f);
	add_instance(101, 0, -20.0f, 0.0f, 0.0f);
	add_instance(102, 0, -3.0f, 0.0f, 0.0f);
	add_instance(103, 0, 10.0f, 30.0f, 0.0f);
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 2, "%ld", count);
	/* nearest first */
	CHECK(owners[0] == 102 && owners[1] == 100, "%ld %ld", owners[0], owners[1]);
	CHECK(proxies[1].v[0][0] == 10.0f && proxies[1].v[2][0] == 0.5f, "placed %g", proxies[1].v[0][0]);
	CHECK(fake.lines > 0, "the debug view drew nothing");
	/* more than a window holds: the nearest 64, in order */
	fake.instance_count = 0;
	for (index = 0; index < 200; index++)
		add_instance(1000 + index, 0, 5.0f + (float)((index * 37) % 200), 0.0f, 0.0f);
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == WORLD_PROXY_MAXIMUM_PER_WINDOW, "%ld", count);
	for (index = 0; index < count; index++)
		CHECK(proxies[index].v[0][0] == 5.0f + (float)index, "%ld: %g", index, proxies[index].v[0][0]);
	/* a smaller maximum, and none */
	CHECK(world_proxies_window(camera, proxies, owners, 3, &dynamic_count) == 3, "three");
	CHECK(world_proxies_window(camera, NULL, NULL, 0, &dynamic_count) == 0, "none");
	/* a definition is made once */
	{
		long read = fake.definitions_read;

		world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
		CHECK(fake.definitions_read == read, "made again");
		world_proxies_map_unloaded();
		world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
		CHECK(fake.definitions_read == read + 1, "kept past the map");
	}
}

/* ---------- a fake biped: a pelvis, a thigh and a calf, a cylinder of
collision vertices on each */

#define CYLINDER_VERTICES 16

static float vertices[3][CYLINDER_VERTICES][4];

/* a cylinder's ring of vertices from 0 to length along x, radius r */
static void cylinder(float out[CYLINDER_VERTICES][4], float length, float radius)
{
	int index;

	for (index = 0; index < CYLINDER_VERTICES; index++)
	{
		float angle = 2.0f * 3.14159265f * (float)(index % 8) / 8.0f;

		out[index][0] = index < 8 ? 0.0f : length;
		out[index][1] = radius * cosf(angle);
		out[index][2] = radius * sinf(angle);
		out[index][3] = 0.0f;
	}
}

static void biped_definition(void)
{
	struct world_proxies_source *source = &fake.sources[1];
	short node;

	memset(source, 0, sizeof(*source));
	source->shape_class = _world_proxies_source_biped;
	source->node_count = 3;
	source->bounding_radius = 1.0f;
	/* the pelvis at 1 up, the thigh's node at it, the calf's 0.5 along the
	thigh's x (pointing down in the default pose: x is down) */
	identity(&source->nodes[0].default_frame, 0.0f, 0.0f, 1.0f);
	identity(&source->nodes[1].default_frame, 0.0f, 0.0f, 1.0f);
	identity(&source->nodes[2].default_frame, 0.0f, 0.0f, 0.5f);
	for (node = 1; node < 3; node++)
	{
		struct world_proxies_frame *frame = &source->nodes[node].default_frame;

		/* (x down, y left, z forward) */
		memset(frame->axes, 0, sizeof(frame->axes));
		frame->axes[0][2] = -1.0f;
		frame->axes[1][1] = 1.0f;
		frame->axes[2][0] = 1.0f;
	}
	source->nodes[0].parent = -1;
	source->nodes[1].parent = 0;
	source->nodes[2].parent = 1;
	cylinder(vertices[1], 0.5f, 0.1f);
	cylinder(vertices[2], 0.5f, 0.08f);
	for (node = 1; node < 3; node++)
	{
		source->nodes[node].vertex_count = CYLINDER_VERTICES;
		source->nodes[node].vertex_stride = sizeof(vertices[node][0]);
		source->nodes[node].vertices = vertices[node][0];
	}
	fake.source_known[1] = 1;
}

static void skeleton_case(void)
{
	struct world_proxy_set const *set;
	float const camera[3] = { -5.0f, 0.0f, 1.0f };
	struct world_proxy proxies[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long owners[WORLD_PROXY_MAXIMUM_PER_WINDOW], count, index;
	short node;

	biped_definition();
	set = world_proxies_definition(1);
	CHECK(set && set->count == 2, "%d", set ? set->count : -1);
	/* the thigh's capsule: along its node's x, inside its cylinder (the
	pelvis has no geometry, so no shape) */
	for (index = 0; index < set->count; index++)
	{
		struct world_proxy_shape const *shape = &set->shapes[index];

		CHECK(shape->kind == _world_proxy_capsule, "kind %d", shape->kind);
		CHECK(shape->node_index == 1 || shape->node_index == 2, "node %d", shape->node_index);
		CHECK(shape->radius > 0.06f && shape->radius <= (shape->node_index == 1 ? 0.1f : 0.08f), "radius %g", shape->radius);
		CHECK(fabsf(shape->a[1]) < 1.0e-5f && fabsf(shape->a[2]) < 1.0e-5f && shape->b[0] > shape->a[0], "along x");
	}
	/* placed on the nodes as drawn: the leg straight down, then (a tick
	later) the knee bent, the calf pointing forward */
	fake.instance_count = 0;
	fake.frustum = 0;
	add_instance(7, 1, 0.0f, 0.0f, 0.0f);
	fake.frame_counts[0] = 3;
	for (node = 0; node < 3; node++)
	{
		fake.frames[0][node] = fake.sources[1].nodes[node].default_frame;
		fake.frames[0][node].position[0] += 3.0f;
	}
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 2, "%ld", count);
	for (index = 0; index < count; index++)
	{
		/* (straight down at x = 3: a above b) */
		CHECK(fabsf(proxies[index].v[0][0] - 3.0f) < 1.0e-5f && fabsf(proxies[index].v[1][0] - 3.0f) < 1.0e-5f, "x");
		CHECK(proxies[index].v[0][2] > proxies[index].v[1][2], "down");
	}
	/* the calf turned to point forward (+x) from the knee */
	memset(fake.frames[0][2].axes, 0, sizeof(fake.frames[0][2].axes));
	fake.frames[0][2].axes[0][0] = 1.0f;
	fake.frames[0][2].axes[1][1] = 1.0f;
	fake.frames[0][2].axes[2][2] = 1.0f;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 2, "%ld", count);
	for (index = 0; index < count; index++)
	{
		struct world_proxy const *proxy = &proxies[index];

		if (proxy->v[0][2] > 0.6f)
			continue; /* (the thigh) */
		CHECK(fabsf(proxy->v[0][2] - 0.5f) < 1.0e-5f && fabsf(proxy->v[1][2] - 0.5f) < 1.0e-5f, "level %g %g",
			proxy->v[0][2], proxy->v[1][2]);
		CHECK(proxy->v[1][0] > proxy->v[0][0] && proxy->v[0][0] >= 3.0f, "forward %g %g", proxy->v[0][0], proxy->v[1][0]);
	}
	/* a scaled object's shapes scale with it */
	for (node = 0; node < 3; node++)
	{
		int axis, k;

		for (axis = 0; axis < 3; axis++)
			for (k = 0; k < 3; k++)
				fake.frames[0][node].axes[axis][k] *= 2.0f;
	}
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	for (index = 0; index < count; index++)
		CHECK(proxies[index].v[2][0] > 0.12f, "scaled radius %g", proxies[index].v[2][0]);
}

static void owner_case(void)
{
	float const camera[3] = { -5.0f, 0.0f, 1.0f };
	struct world_proxy proxies[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long owners[WORLD_PROXY_MAXIMUM_PER_WINDOW], count, index;
	short node;

	biped_definition();
	one_sphere_definition();
	fake.frustum = 0;
	/* a biped (2 shapes) between two spheres */
	add_instance(20, 0, -1.0f, 0.0f, 1.0f);
	add_instance(21, 1, 0.0f, 0.0f, 0.0f);
	fake.frame_counts[1] = 3;
	for (node = 0; node < 3; node++)
		fake.frames[1][node] = fake.sources[1].nodes[node].default_frame;
	add_instance(22, 0, 2.0f, 0.0f, 1.0f);
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 4, "%ld", count);
	/* each owner one slot, in order of its first shape; a slot's shapes all
	its owner's */
	for (index = 0; index < count; index++)
	{
		long other;

		CHECK(owners[index] >= 20 && owners[index] <= 22, "owner %ld", owners[index]);
		for (other = 0; other < count; other++)
		{
			CHECK((owners[index] == owners[other]) == (proxies[index].v[1][3] == proxies[other].v[1][3]),
				"%ld and %ld: owners %ld %ld slots %g %g", index, other, owners[index], owners[other],
				proxies[index].v[1][3], proxies[other].v[1][3]);
		}
	}
	CHECK(owners[0] == 20 && proxies[0].v[1][3] == 0.0f, "the nearest owner's slot 0");
	CHECK(proxies[count - 1].v[1][3] == 2.0f && owners[count - 1] == 22, "the last owner's slot 2");
}

/* the static scenery the field and its pages hold: its shapes after every
dynamic object's, nearer or not, and the dynamic objects' counted */
static void baked_case(void)
{
	float const camera[3] = { -5.0f, 0.0f, 1.0f };
	struct world_proxy proxies[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long owners[WORLD_PROXY_MAXIMUM_PER_WINDOW], count;

	one_sphere_definition();
	fake.frustum = 0;
	/* (a baked sphere nearest, a dynamic one, a baked one, a dynamic one
	farthest) */
	add_instance(30, 0, -4.0f, 0.0f, 1.0f);
	fake.instances[0].baked = 1;
	add_instance(31, 0, -2.0f, 0.0f, 1.0f);
	add_instance(32, 0, 0.0f, 0.0f, 1.0f);
	fake.instances[2].baked = 1;
	add_instance(33, 0, 3.0f, 0.0f, 1.0f);
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 4 && dynamic_count == 2, "%ld shapes, %ld dynamic", count, dynamic_count);
	CHECK(owners[0] == 31 && owners[1] == 33 && owners[2] == 30 && owners[3] == 32, "the order %ld %ld %ld %ld",
		owners[0], owners[1], owners[2], owners[3]);
	/* (a window too small for all: the dynamic objects' kept first) */
	count = world_proxies_window(camera, proxies, owners, 3, &dynamic_count);
	CHECK(count == 3 && dynamic_count == 2 && owners[2] == 30, "three: %ld, %ld dynamic, the last %ld", count,
		dynamic_count, owners[2]);
}

/* a vehicle: a body box (node 0, 2 x 1 x 0.8) and a wheel (node 1) */
static float field_body[8][3], field_wheel[8][3];

static void field_definition(void)
{
	struct world_proxies_source *source = &fake.sources[3];
	int corner;

	memset(source, 0, sizeof(*source));
	source->shape_class = _world_proxies_source_vehicle;
	source->node_count = 2;
	source->bounding_radius = 1.5f;
	source->nodes[0].parent = -1;
	identity(&source->nodes[0].default_frame, 0.0f, 0.0f, 0.0f);
	source->nodes[1].parent = 0;
	source->nodes[1].wheel = 1;
	identity(&source->nodes[1].default_frame, 0.8f, 0.6f, 0.2f);
	for (corner = 0; corner < 8; corner++)
	{
		field_body[corner][0] = corner & 1 ? 1.0f : -1.0f;
		field_body[corner][1] = corner & 2 ? 0.5f : -0.5f;
		field_body[corner][2] = corner & 4 ? 0.8f : 0.0f;
		field_wheel[corner][0] = corner & 1 ? 0.2f : -0.2f;
		field_wheel[corner][1] = corner & 2 ? 0.2f : -0.2f;
		field_wheel[corner][2] = corner & 4 ? 0.2f : -0.2f;
	}
	source->nodes[0].vertex_count = source->nodes[1].vertex_count = 8;
	source->nodes[0].vertex_stride = source->nodes[1].vertex_stride = sizeof(float) * 3;
	source->nodes[0].vertices = &field_body[0][0];
	source->nodes[1].vertices = &field_wheel[0][0];
	fake.source_known[3] = 1;
	/* (its field: 0.25 voxels over the body and 1.5 about it, its place in
	an atlas of 16 x 16 x 32) */
	memset(&fake_field, 0, sizeof(fake_field));
	fake_field.origin[0] = -2.5f;
	fake_field.origin[1] = -2.0f;
	fake_field.origin[2] = -1.475f;
	fake_field.voxel = 0.25f;
	fake_field.size[0] = 21;
	fake_field.size[1] = 17;
	fake_field.size[2] = 16;
	fake_field.reach = 30.0f;
	fake_field.bounds[0] = -1.0f;
	fake_field.bounds[1] = -0.5f;
	fake_field.bounds[2] = 0.0f;
	fake_field.bounds[3] = 1.0f;
	fake_field.bounds[4] = 0.5f;
	fake_field.bounds[5] = 0.8f;
	fake_field.place[2] = 5;
	fake_field.atlas[0] = 32;
	fake_field.atlas[1] = 32;
	fake_field.atlas[2] = 64;
}

/* a field's place of a world point (its first slot's and the next's rows) */
static void field_voxel(struct world_proxy const *proxy, float const point[3], float g[3])
{
	float const *rows[3] = { proxy[0].v[1], proxy[1].v[0], proxy[1].v[1] };
	int axis;

	for (axis = 0; axis < 3; axis++)
		g[axis] = rows[axis][0] * point[0] + rows[axis][1] * point[1] + rows[axis][2] * point[2] + proxy[0].v[2][axis];
}

static void field_case(void)
{
	float const camera[3] = { -6.0f, 0.0f, 1.0f };
	struct world_proxy proxies[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long owners[WORLD_PROXY_MAXIMUM_PER_WINDOW], count, index;
	int axis, kinds[NUMBER_OF_WORLD_PROXY_KINDS];

	field_definition();
	one_sphere_definition();
	fake.frustum = 0;
	add_instance(40, 3, 2.0f, 1.0f, 0.0f);
	fake.frame_counts[0] = 2;
	fake.frames[0][1] = fake.sources[3].nodes[1].default_frame;
	fake.frames[0][1].position[0] += 2.0f;
	fake.frames[0][1].position[1] += 1.0f;
	/* none drawn: its box and its wheel's sphere */
	field_known = 0;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count == 2 && kinds[_world_proxy_box] == 1 && kinds[_world_proxy_sphere] == 1, "without: %ld", count);
	/* none drawn but one on its way (with the BSP's field): neither its box
	nor a field until it comes, its wheel's sphere kept */
	fake.instances[0].field_awaited = 1;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count == 1 && kinds[_world_proxy_sphere] == 1, "awaited: %ld", count);
	fake.instances[0].field_awaited = 0;
	/* drawn: its field in three slots together, in place of its box, and
	its wheel's sphere */
	field_known = 1;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count == 4 && dynamic_count == 4 && kinds[_world_proxy_field] == 1 && kinds[_world_proxy_none] == 2 &&
		kinds[_world_proxy_sphere] == 1 && kinds[_world_proxy_box] == 0, "with: %ld, %ld dynamic", count, dynamic_count);
	for (index = 0; index < count; index++)
	{
		CHECK(owners[index] == 40 && proxies[index].v[1][3] == 0.0f, "slot %ld's owner", index);
		if ((int)proxies[index].v[0][3] == _world_proxy_field)
		{
			CHECK(index + 2 < count && (int)proxies[index + 1].v[0][3] == _world_proxy_none &&
				(int)proxies[index + 2].v[0][3] == _world_proxy_none, "its slots apart");
			break;
		}
	}
	{
		struct world_proxy const *field = &proxies[index];
		/* (its first voxel where the field's origin stands, on node 0 at 2, 1, 0) */
		float origin[3] = { -0.5f, -1.0f, -1.475f }, g[3], middle[3];

		field_voxel(field, origin, g);
		CHECK(fabsf(g[0]) < 1.0e-4f && fabsf(g[1]) < 1.0e-4f && fabsf(g[2]) < 1.0e-4f, "its origin %g %g %g", g[0], g[1],
			g[2]);
		field_voxel(field, field[0].v[0], middle);
		CHECK(fabsf(middle[0] - 10.0f) < 1.0e-3f && fabsf(middle[1] - 8.0f) < 1.0e-3f && fabsf(middle[2] - 7.5f) < 1.0e-3f,
			"its middle %g %g %g", middle[0], middle[1], middle[2]);
		CHECK(field[1].v[2][0] == 20.0f && field[1].v[2][1] == 16.0f && field[1].v[2][2] == 15.0f, "its last voxel");
		CHECK(field[2].v[0][2] == 5.5f && field[2].v[1][0] == 1.0f / 32.0f && field[2].v[1][2] == 1.0f / 64.0f, "its atlas");
		/* (its geometry's box in its voxels: half 4, 2, 1.6) */
		CHECK(fabsf(field[2].v[2][0] - 4.0f) < 1.0e-4f && fabsf(field[2].v[2][1] - 2.0f) < 1.0e-4f &&
			fabsf(field[2].v[2][2] - 1.6f) < 1.0e-4f && field[2].v[2][3] == 0.0f && field[1].v[2][3] == 0.0f, "its box");
		/* (its reach: the field's, 7.5) */
		CHECK(fabsf(field[0].v[2][3] - 7.5f) < 1.0e-4f, "reach %g", field[0].v[2][3]);
		CHECK(world_proxy_reach(field) == field[0].v[2][3], "world_proxy_reach");
	}
	/* (a field reaching past its box's bound: the box's reach, from the
	grid's middle) */
	fake_field.reach = 1000.0f;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	for (index = 0; index < count && (int)proxies[index].v[0][3] != _world_proxy_field; index++)
		;
	{
		struct world_proxy box;
		float reach;

		memset(&box, 0, sizeof(box));
		box.v[0][3] = (float)_world_proxy_box;
		box.v[1][0] = 1.0f;
		box.v[2][1] = 0.5f;
		box.v[2][3] = 0.4f;
		reach = world_proxy_reach(&box);
		CHECK(index < count && fabsf(proxies[index].v[2][3] - reach) < 1.0e-3f, "capped reach %g, the box's %g",
			index < count ? proxies[index].v[2][3] : -1.0f, reach);
	}
	fake_field.reach = 30.0f;
	/* scaled twice: its voxels' place the same, its reach twice */
	for (axis = 0; axis < 3; axis++)
	{
		int k;

		for (k = 0; k < 3; k++)
			fake.frames[0][0].axes[axis][k] *= 2.0f;
	}
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	for (index = 0; index < count && (int)proxies[index].v[0][3] != _world_proxy_field; index++)
		;
	{
		/* (its origin at 2 + 2 (-2.5), 1 + 2 (-2), 2 (-1.475)) */
		float origin[3] = { -3.0f, -3.0f, -2.95f }, g[3];

		CHECK(index < count, "scaled: no field");
		field_voxel(&proxies[index], origin, g);
		CHECK(fabsf(g[0]) < 1.0e-3f && fabsf(g[1]) < 1.0e-3f && fabsf(g[2]) < 1.0e-3f, "scaled origin %g %g %g", g[0], g[1],
			g[2]);
		CHECK(fabsf(proxies[index].v[2][3] - 15.0f) < 1.0e-3f, "scaled reach %g", proxies[index].v[2][3]);
	}
	for (axis = 0; axis < 3; axis++)
	{
		int k;

		for (k = 0; k < 3; k++)
			fake.frames[0][0].axes[axis][k] *= 0.5f;
	}
	/* a window too small for its field: none of it, never a part; with a
	nearer sphere, the sphere and the wheel */
	add_instance(41, 0, -5.0f, 0.0f, 1.0f);
	count = world_proxies_window(camera, proxies, owners, 3, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count <= 3 && kinds[_world_proxy_field] == 0 && kinds[_world_proxy_none] == 0, "three: %ld, %d fields, %d parts",
		count, kinds[_world_proxy_field], kinds[_world_proxy_none]);
	count = world_proxies_window(camera, proxies, owners, 4, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count == 4 && kinds[_world_proxy_field] == 1 && kinds[_world_proxy_none] == 2 && owners[0] == 41,
		"four: %ld, %d fields", count, kinds[_world_proxy_field]);
	/* (a baked placement takes its field too, after the dynamic objects':
its pages were baked with it, and a page without a layer walks it) */
	fake.instances[0].baked = 1;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count == 5 && kinds[_world_proxy_field] == 1 && kinds[_world_proxy_box] == 0 && dynamic_count == 1 &&
		owners[0] == 41, "baked: %ld, %ld dynamic", count, dynamic_count);
	fake.instances[0].baked = 0;
	/* a piece of scenery with no collision geometry (a shrub: once one
	sphere of its bound, which held the ground inside it wholly occluded,
	#666; now the box of what it draws, #773): its field in place of its
	box; on its way, neither */
	memset(&fake.sources[5], 0, sizeof(fake.sources[5]));
	fake.sources[5].shape_class = _world_proxies_source_box;
	fake.sources[5].node_count = 1;
	fake.sources[5].bounding_radius = 1.5f;
	fake.sources[5].drawn = 1;
	fake.sources[5].drawn_bounds[0][0] = -0.8f;
	fake.sources[5].drawn_bounds[0][1] = -0.8f;
	fake.sources[5].drawn_bounds[1][0] = 0.8f;
	fake.sources[5].drawn_bounds[1][1] = 0.8f;
	fake.sources[5].drawn_bounds[1][2] = 1.2f;
	fake.sources[5].nodes[0].parent = -1;
	identity(&fake.sources[5].nodes[0].default_frame, 0.0f, 0.0f, 0.0f);
	fake.source_known[5] = 1;
	fake.instance_count = 0;
	add_instance(42, 5, 2.0f, 0.0f, 0.0f);
	field_known = 0;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 1 && (int)proxies[0].v[0][3] == _world_proxy_box, "the shrub without: %ld", count);
	fake.instances[0].field_awaited = 1;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	CHECK(count == 0, "the shrub's on its way: %ld", count);
	fake.instances[0].field_awaited = 0;
	field_known = 1;
	count = world_proxies_window(camera, proxies, owners, WORLD_PROXY_MAXIMUM_PER_WINDOW, &dynamic_count);
	memset(kinds, 0, sizeof(kinds));
	for (index = 0; index < count; index++)
		kinds[(int)proxies[index].v[0][3]]++;
	CHECK(count == 3 && kinds[_world_proxy_field] == 1 && kinds[_world_proxy_box] == 0, "the shrub with: %ld", count);
	/* (a definition of one shape of any kind: its field stands in for it) */
	memset(&fake.sources[4], 0, sizeof(fake.sources[4]));
	fake.sources[4].shape_class = _world_proxies_source_sphere;
	fake.sources[4].node_count = 1;
	fake.sources[4].bounding_radius = 1.0f;
	fake.sources[4].nodes[0].parent = -1;
	identity(&fake.sources[4].nodes[0].default_frame, 0.0f, 0.0f, 0.0f);
	fake.source_known[4] = 1;
	CHECK(world_proxies_definition(4) && world_proxy_set_fielded(world_proxies_definition(4), 0), "one sphere's field");
}

/* items and scenery without collision geometry (#773): a grenade on the
floor, whose bounding sphere (a bound for culling and pickups, about its
origin, twice its size) held the floor about it wholly occluded, a dark
disc several times the grenade; and a floor marker that draws nothing
opaque (a glowing triangle on the floor), whose sphere half under the
floor darkened a disc about it */
static void drawn_case(void)
{
	float const up[3] = { 0.0f, 0.0f, 1.0f };
	struct world_proxy_set const *set;
	struct world_proxy proxy;
	float floor[3], occlusion;
	int k;

	world_proxies_map_unloaded();
	memset(&fake.sources[6], 0, sizeof(fake.sources[6]));
	fake.sources[6].shape_class = _world_proxies_source_box;
	fake.sources[6].node_count = 1;
	fake.sources[6].bounding_radius = 0.2f;
	fake.sources[6].drawn = 1;
	fake.sources[6].drawn_bounds[0][0] = -0.05f;
	fake.sources[6].drawn_bounds[0][1] = -0.035f;
	fake.sources[6].drawn_bounds[0][2] = -0.03f;
	fake.sources[6].drawn_bounds[1][0] = 0.05f;
	fake.sources[6].drawn_bounds[1][1] = 0.035f;
	fake.sources[6].drawn_bounds[1][2] = 0.03f;
	fake.sources[6].nodes[0].parent = -1;
	identity(&fake.sources[6].nodes[0].default_frame, 0.0f, 0.0f, 0.0f);
	fake.source_known[6] = 1;
	set = world_proxies_definition(6);
	CHECK(set && set->count == 1 && set->shapes[0].kind == _world_proxy_box, "the grenade: %d shapes",
		set ? set->count : -1);
	for (k = 0; k < 3; k++)
	{
		float half = 0.5f * (fake.sources[6].drawn_bounds[1][k] - fake.sources[6].drawn_bounds[0][k]);

		CHECK(fabsf(set->shapes[0].axes[k][k] - half) < 1.0e-5f && fabsf(set->shapes[0].a[k]) < 1.0e-5f,
			"the grenade's box %d: %g, not %g", k, set->shapes[0].axes[k][k], half);
	}
	/* the floor 0.15 from its middle (inside its bounding sphere): hardly
	shaded */
	identity(&fake.frames[0][0], 0.0f, 0.0f, 0.0f);
	CHECK(world_proxies_place(6, fake.frames[0], 1, &proxy, 1) == 1, "the grenade placed");
	floor[0] = 0.15f;
	floor[1] = 0.0f;
	floor[2] = -0.03f;
	occlusion = world_proxy_occlusion(&proxy, floor, up);
	CHECK(occlusion < 0.1f, "the floor beside the grenade: %g", occlusion);
	/* the marker: no shapes at all */
	memset(&fake.sources[7], 0, sizeof(fake.sources[7]));
	fake.sources[7].shape_class = _world_proxies_source_box;
	fake.sources[7].node_count = 1;
	fake.sources[7].bounding_radius = 0.3f;
	fake.sources[7].nodes[0].parent = -1;
	identity(&fake.sources[7].nodes[0].default_frame, 0.0f, 0.0f, 0.0f);
	fake.source_known[7] = 1;
	CHECK(world_proxies_definition(7) == NULL, "the floor marker has shapes");
}

static void none_case(void)
{
	float const origin[3] = { 0.0f, 0.0f, 0.0f };
	float const up[3] = { 0.0f, 0.0f, 1.0f };
	struct world_proxy proxy;

	memset(&proxy, 0, sizeof(proxy));
	CHECK(world_proxy_occlusion(&proxy, origin, up) == 0.0f, "none");
	CHECK(world_proxies_window(origin, NULL, NULL, 0, &dynamic_count) == 0, "an empty window");
	/* an unknown definition and a projectile's: no shapes */
	CHECK(world_proxies_definition(3) == NULL, "unknown");
	CHECK(world_proxies_definition(-1) == NULL, "none");
	/* a tag index is a datum handle, its salt in the sign bit */
	one_sphere_definition();
	CHECK(world_proxies_definition((long)0xE3D70000UL) != NULL, "a salted tag index");
	memset(&fake.sources[2], 0, sizeof(fake.sources[2]));
	fake.sources[2].shape_class = _world_proxies_source_none;
	fake.sources[2].node_count = 1;
	fake.sources[2].bounding_radius = 1.0f;
	fake.source_known[2] = 1;
	CHECK(world_proxies_definition(2) == NULL, "none's class");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "sphere"))
		sphere_case();
	else if (!strcmp(name, "capsule"))
		capsule_case();
	else if (!strcmp(name, "box"))
		box_case();
	else if (!strcmp(name, "reach"))
		reach_case();
	else if (!strcmp(name, "bound"))
		bound_case();
	else if (!strcmp(name, "far"))
		far_case();
	else if (!strcmp(name, "culling"))
		culling_case();
	else if (!strcmp(name, "skeleton"))
		skeleton_case();
	else if (!strcmp(name, "owner"))
		owner_case();
	else if (!strcmp(name, "baked"))
		baked_case();
	else if (!strcmp(name, "field"))
		field_case();
	else if (!strcmp(name, "none"))
		none_case();
	else if (!strcmp(name, "drawn"))
		drawn_case();
	else
		return 2;
	return 0;
}
