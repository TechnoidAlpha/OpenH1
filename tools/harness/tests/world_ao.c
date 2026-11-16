/* display.ambient_occlusion "world"'s game side: the setting read, the
frame's and the window's hooks doing nothing unless it is "world"; a
window's block made from the field and the proxies; each object named with
its owner slot and reference, the first-person weapon left unlit; the
made-up window of debug.world_ao_test (port/linux/game/world_ao.c;
test_world_ao.py). The parts' own tests are test_world_scene.py,
test_world_sdf.py and test_world_proxies.py; the shading's arithmetic,
tools/test_world_ao_light.py. */

#include "harness.h"
#include "world_ao.h"
#include "world_cascades.h"
#include "world_proxies.h"
#include "world_rt.h"
#include "world_scene.h"
#include "world_sdf.h"

static const char *setting = "off";
static const char *test_setting = "";
static unsigned long changes = 1;
static int scene_frames, sdf_frames, windows_handed, fields_handed;
static struct
{
	float field[2][4], parameters[2][4], proxies[8][3][4], grid[2][4];
	unsigned int cells;
	int count;
} handed;
static int object_owner = -100;
unsigned char render_enumerating;
static float object_reference = -100.0f;
/* ("ray_traced": the backend's ray queries, the object's owner among the
ray-traced scene's instances, its frames and the log's lines) */
static int ray_queries, backend_ready;
static unsigned long object_rays_owner = 12345, rt_frames;
static int rt_unloads, logged;
/* (the window's span as the front end was last told it) */
static int span = -100;

const char *config_string(const char *name)
{
	if (!strcmp(name, "debug.world_ao_test"))
		return test_setting;
	return setting;
}

/* (the settings as the front end took them up: d3d8_device.c's) */
unsigned long halo_gpu_settings_taken(void)
{
	return changes;
}

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

/* (found once a pixel: the front end's pre-pass, none here) */
static int prepass_found;

int halo_world_ao_prepass_found(void)
{
	return prepass_found;
}

int halo_world_ao_prepass_begin(void)
{
	return 0;
}

void halo_world_ao_prepass_end(short x0, short y0, short x1, short y1, float const camera[4][4],
	float const cascades[4][4], unsigned long frame, int history)
{
	(void)x0; (void)y0; (void)x1; (void)y1; (void)camera; (void)cascades; (void)frame; (void)history;
}

/* the parts (their stubs' behaviour: the real ones are tested apart) */
void world_scene_map_loaded(void) {}
void world_scene_map_unloaded(void) {}
void world_scene_bsp_switched(short bsp_index) { (void)bsp_index; }
void world_scene_frame_begin(void) { scene_frames++; }
void world_sdf_map_loaded(void) {}
void world_sdf_map_unloaded(void) {}
void world_sdf_bsp_switched(short bsp_index) { (void)bsp_index; }
void world_sdf_frame(void) { sdf_frames++; }
void world_sdf_frame_prepared(void) {}
void world_proxies_map_unloaded(void) {}

/* the field: a volume whose header world_sdf_current hands on, half faded in */
static struct world_sdf_volume volume;
static int field_ready;

struct world_sdf_header const *world_sdf_current(void)
{
	return field_ready ? &volume.header : NULL;
}

float world_sdf_fade(void)
{
	return 0.5f;
}

/* (static scenery: object 400, with collision geometry, and 500, a shrub
without) */
int world_proxies_source_baked(long id)
{
	return id == 400 || id == 500;
}

int world_proxies_source_solid(long id)
{
	return id == 400;
}

/* (no static scenery's pages) */
int world_sdf_scenery_layer(void const *lightmap_bitmap)
{
	(void)lightmap_bitmap;
	return WORLD_SDF_SCENERY_NO_LAYER;
}

signed char world_sdf_encode(float distance, float range)
{
	(void)range;
	return distance > 0.0f ? 1 : -1;
}

/* three shapes: two of object 100 (slot 0), one of object 200 (slot 1) */
long world_proxies_window(float const camera_position[3], struct world_proxy *proxies, long *owners, long maximum,
	long *dynamic_count)
{
	static const long ids[3] = { 100, 100, 200 };
	long index;

	(void)camera_position;
	*dynamic_count = maximum < 3 ? maximum : 3;
	for (index = 0; index < 3 && index < maximum; index++)
	{
		memset(&proxies[index], 0, sizeof(proxies[index]));
		proxies[index].v[0][0] = (float)index;
		proxies[index].v[0][3] = _world_proxy_sphere;
		proxies[index].v[1][3] = ids[index] == 100 ? 0.0f : 1.0f;
		proxies[index].v[2][0] = 0.5f;
		owners[index] = ids[index];
	}
	return index;
}

/* object 200's lighting point; the others not in the scene */
struct world_scene_instance const *world_scene_instance_find(long id)
{
	static struct world_scene_instance instance;

	if (id != 200 && id != 500)
		return NULL;
	memset(&instance, 0, sizeof(instance));
	instance.id = id;
	instance.lighting_point[2] = id == 200 ? 7.0f : 6.0f;
	return &instance;
}

/* (world_ao_shading.c's, tested apart: here, which point; asked without a
field, a value that shows it was) */
float world_ao_reference(struct world_sdf_volume const *reference_volume, float const lighting_point[3])
{
	return reference_volume ? lighting_point[2] / 10.0f : 0.3f;
}

/* (world_ao_shading.c's grid, tested apart: here, about which camera, of
how many proxies) */
void world_ao_grid(struct world_proxy const *proxies, long count, float const camera[3], float grid[2][4],
	unsigned int cells[WORLD_AO_GRID_CELLS / 2][4])
{
	(void)proxies;
	memset(grid, 0, sizeof(float) * 8);
	memcpy(grid[0], camera, sizeof(float) * 3);
	cells[0][0] = (unsigned int)count;
}

void halo_world_ao_window(float const field[2][4], float const parameters[2][4], float const (*proxies)[3][4],
	int count, float const grid[2][4], unsigned int const (*cells)[4])
{
	windows_handed++;
	handed.count = count;
	if (count < 0)
		return;
	memcpy(handed.field, field, sizeof(handed.field));
	memcpy(handed.parameters, parameters, sizeof(handed.parameters));
	memcpy(handed.proxies, proxies, (size_t)(count < 8 ? count : 8) * sizeof(handed.proxies[0]));
	memcpy(handed.grid, grid, sizeof(handed.grid));
	handed.cells = cells ? cells[0][0] : 0xffffffffu;
}

void halo_world_ao_span(int value)
{
	span = value;
}

void halo_world_ao_object(int owner, float reference, unsigned long rays_owner)
{
	object_owner = owner;
	object_reference = reference;
	object_rays_owner = rays_owner;
}

static int object_cards = -1;

void halo_world_ao_cards(int cards)
{
	object_cards = cards;
}

int halo_world_ao_ray_queries(void)
{
	return ray_queries;
}

char const *halo_world_ao_ray_queries_missing(void)
{
	return backend_ready && !ray_queries ? "none here" : NULL;
}

void platform_log(const char *format, ...)
{
	(void)format;
	logged++;
}

/* (world_rt.c's, tested apart: test_world_rt.py) */
void world_rt_frame(unsigned long frame)
{
	(void)frame;
	rt_frames++;
}

void world_rt_map_unloaded(void)
{
	rt_unloads++;
}

unsigned long world_rt_owner(long object_index)
{
	return object_index == -1 ? 0 : (unsigned long)(object_index & 0xFFFF) + 1;
}

/* (the made-up field made sparse and handed on: world_sdf_bake.c's and
world_sdf.c's, tested apart) */
int world_sdf_pack(struct world_sdf_volume *packed, signed char const *voxels)
{
	(void)voxels;
	packed->cells = NULL;
	packed->bricks = NULL;
	return 1;
}

void world_sdf_upload(struct world_sdf_volume const *uploaded)
{
	(void)uploaded;
	fields_handed++;
}

/* (the distance-field scene: world_cascades.c's, tested apart; here, how
many frames it was updated in) */
static int scene_updates;
float world_sdf_sample(struct world_sdf_volume const *sampled, float const point[3])
{
	(void)sampled;
	return point[2];
}
unsigned long long SDL_GetTicksNS(void)
{
	return 0;
}
long world_proxies_scene(float const camera_position[3], float reach, struct world_proxy *proxies, long *owners,
	long maximum)
{
	(void)camera_position;
	(void)reach;
	(void)proxies;
	(void)owners;
	(void)maximum;
	return 0;
}
int world_cascades_create(struct world_cascades *cascades)
{
	memset(cascades, 0, sizeof(*cascades));
	return 1;
}
void world_cascades_free(struct world_cascades *cascades)
{
	(void)cascades;
}
void world_cascades_update(struct world_cascades *cascades, float const camera[3], int structure_changed,
	world_cascades_structure structure, void *context, struct world_cascades_object const *objects, long object_count,
	struct world_proxy const *shapes)
{
	(void)cascades;
	(void)camera;
	(void)structure_changed;
	(void)structure;
	(void)context;
	(void)objects;
	(void)object_count;
	(void)shapes;
	scene_updates++;
}
void world_cascades_block(struct world_cascades const *cascades, float block[WORLD_CASCADES][4])
{
	(void)cascades;
	memset(block, 0, sizeof(float) * 4 * WORLD_CASCADES);
}
void halo_world_ao_cascades(unsigned long x, unsigned long y, unsigned long z, unsigned long width, unsigned long height,
	unsigned long depth, void const *data)
{
	(void)x;
	(void)y;
	(void)z;
	(void)width;
	(void)height;
	(void)depth;
	(void)data;
}

#include "under_test.inc"

static void off_case(void)
{
	float const camera[3] = { 0.0f, 0.0f, 0.0f };
	/* (the older "subtle" and "strong" are read as "world" by the settings
	themselves, before this reads them: port_config.c's config_renamed_words) */
	static const char *const others[] = { "off", "" };
	int index;

	for (index = 0; index < 2; index++)
	{
		setting = others[index];
		changes++;
		CHECK(world_ao_mode() == 0, "%s", setting);
		world_ao_frame_begin();
		world_ao_window_begin(0, 0, camera);
		/* (its span told whatever the setting: the manifest's) */
		CHECK(span == WORLD_AO_SPAN_OPEN, "%s: the span opened: %d", setting, span);
		world_ao_object_begin(200);
		world_ao_first_person(1);
		CHECK(span == WORLD_AO_SPAN_FIRST_PERSON, "%s: the first-person weapon's span: %d", setting, span);
		world_ao_first_person(0);
		CHECK(span == WORLD_AO_SPAN_OPEN, "%s: after it: %d", setting, span);
		world_ao_window_end();
		CHECK(span == WORLD_AO_SPAN_NONE, "%s: the span closed: %d", setting, span);
		/* (a window the pre-pass would find once a pixel: its span told so
		whatever the setting, the first-person weapon's in it, and the
		window's again after it: a map's preparation makes their pipelines) */
		prepass_found = 1;
		world_ao_window_begin(0, 0, camera);
		CHECK(!world_ao_prepass_begin(), "%s: a pre-pass begun while off", setting);
		CHECK(span == WORLD_AO_SPAN_FOUND, "%s: the found span: %d", setting, span);
		world_ao_first_person(1);
		CHECK(span == WORLD_AO_SPAN_FOUND_FIRST_PERSON, "%s: its first-person weapon's span: %d", setting, span);
		world_ao_first_person(0);
		CHECK(span == WORLD_AO_SPAN_FOUND, "%s: after it, found: %d", setting, span);
		world_ao_window_end();
		CHECK(span == WORLD_AO_SPAN_NONE, "%s: the found span closed: %d", setting, span);
		prepass_found = 0;
	}
	CHECK(scene_frames == 0 && sdf_frames == 0 && windows_handed == 0, "%d %d %d", scene_frames, sdf_frames,
		windows_handed);
	CHECK(object_owner == -100, "an object named while off: %d", object_owner);
}

static void world_case(void)
{
	setting = "world";
	changes++;
	CHECK(world_ao_mode() == (WORLD_AO_PROXIES | WORLD_AO_FIELD), "%d", world_ao_mode());
	world_ao_frame_begin();
	CHECK(scene_frames == 1 && sdf_frames == 1, "%d %d", scene_frames, sdf_frames);
	/* (and back off once the setting changes) */
	setting = "off";
	changes++;
	CHECK(world_ao_mode() == 0, "off again");
}

static void window_case(void)
{
	float const camera[3] = { 1.0f, 2.0f, 3.0f };

	setting = "world";
	changes++;
	volume.header.width = 8;
	volume.header.height = 4;
	volume.header.depth = 2;
	volume.header.voxel = 0.5f;
	volume.header.range = 6.0f;
	volume.header.origin[0] = -1.0f;
	field_ready = 1;
	/* (none while a map's pipelines are prepared) */
	render_enumerating = 1;
	world_ao_window_begin(0, 0, camera);
	CHECK(windows_handed == 0, "while enumerating: %d", windows_handed);
	CHECK(span == WORLD_AO_SPAN_OPEN, "the span opened while enumerating: %d", span);
	render_enumerating = 0;
	world_ao_window_begin(0, 0, camera);
	CHECK(windows_handed == 1 && handed.count == 3, "%d %d", windows_handed, handed.count);
	CHECK(handed.field[0][0] == -1.0f && handed.field[0][3] == 2.0f && handed.field[1][0] == 8.0f &&
		handed.field[1][2] == 2.0f && handed.field[1][3] == 1.0f, "the field's place (its atlas's layer)");
	CHECK(handed.parameters[0][0] == WORLD_AO_STRENGTH_OBJECTS && handed.parameters[0][1] == WORLD_AO_STRENGTH_LEVEL &&
		handed.parameters[0][2] == 0.5f && handed.parameters[0][3] == 3.0f &&
		handed.parameters[1][0] == WORLD_AO_REFERENCE_WEIGHT, "the parameters");
	CHECK(handed.proxies[2][1][3] == 1.0f, "the proxies' slots handed on as they were");
	CHECK(handed.grid[0][0] == 1.0f && handed.grid[0][2] == 3.0f && handed.cells == 3,
		"the proxies' grid about the camera");
	/* each object's slot (its first shape's) and reference */
	world_ao_object_begin(200);
	CHECK(object_owner == 1 && object_reference == 0.7f, "%d %g", object_owner, object_reference);
	world_ao_object_begin(100);
	CHECK(object_owner == 0 && object_reference == 1.0f, "%d %g", object_owner, object_reference);
	world_ao_object_begin(300);
	CHECK(object_owner == WORLD_AO_OWNER_NONE && object_reference == 1.0f, "%d %g", object_owner, object_reference);
	/* a static placement the field holds: none (from its own depth); one
	the field does not hold (a shrub): its reference, as a moving object's */
	world_ao_object_begin(400);
	CHECK(object_reference == WORLD_AO_REFERENCE_NO_FIELD, "a baked rock: %g", object_reference);
	world_ao_object_begin(500);
	CHECK(object_reference == 0.6f, "a baked shrub: %g", object_reference);
	/* a two-sided shader's cards, and after them */
	world_ao_cards(1);
	CHECK(object_cards == 1, "cards: %d", object_cards);
	world_ao_cards(0);
	CHECK(object_cards == 0, "cards drawn: %d", object_cards);
	world_ao_first_person(1);
	CHECK(object_owner == WORLD_AO_OWNER_UNLIT, "the first-person weapon: %d", object_owner);
	world_ao_first_person(0);
	CHECK(object_owner == WORLD_AO_OWNER_NONE, "after it: %d", object_owner);
	world_ao_object_begin(-1);
	CHECK(object_owner == WORLD_AO_OWNER_NONE && object_reference == 1.0f, "the structure");
	world_ao_window_end();
	CHECK(windows_handed == 2 && handed.count == -1, "taken away: %d %d", windows_handed, handed.count);
	object_owner = -100;
	world_ao_object_begin(200);
	CHECK(object_owner == -100, "an object named with no window");
	object_cards = -1;
	world_ao_cards(1);
	CHECK(object_cards == -1, "cards with no window: %d", object_cards);
	/* without a field: none faded in, every reference 1 */
	field_ready = 0;
	world_ao_window_begin(0, 0, camera);
	CHECK(handed.parameters[0][2] == 0.0f && handed.field[1][0] == 1.0f, "no field");
	world_ao_object_begin(200);
	CHECK(object_owner == 1 && object_reference == 1.0f, "%d %g", object_owner, object_reference);
	world_ao_window_end();
	/* (the distance-field scene: updated once a frame, whatever its windows,
	never while a map's pipelines are prepared) */
	CHECK(scene_updates == 1, "%d updates in a frame of three windows", scene_updates);
	world_ao_frame_begin();
	world_ao_window_begin(0, 0, camera);
	world_ao_window_end();
	CHECK(scene_updates == 2, "%d updates in two frames", scene_updates);
}

/* "ray_traced": "world" and the rays where the backend has ray queries
(said once where it has not, once it is ready); the ray-traced scene's
frame once a frame, each object's owner handed on */
static void rays_case(void)
{
	float const camera[3] = { 1.0f, 2.0f, 3.0f };

	setting = "ray_traced";
	changes++;
	CHECK(world_ao_mode() == (WORLD_AO_PROXIES | WORLD_AO_FIELD), "before the backend is ready: %d", world_ao_mode());
	CHECK(logged == 0, "said before the backend was ready");
	backend_ready = 1;
	CHECK(world_ao_mode() == (WORLD_AO_PROXIES | WORLD_AO_FIELD), "without ray queries: %d", world_ao_mode());
	CHECK(world_ao_mode() == (WORLD_AO_PROXIES | WORLD_AO_FIELD) && logged == 1, "said once: %d", logged);
	world_ao_window_begin(0, 0, camera);
	CHECK(rt_frames == 0, "the ray-traced scene's frame without ray queries");
	world_ao_object_begin(200);
	CHECK(object_rays_owner == 0, "an owner without ray queries: %lu", object_rays_owner);
	world_ao_window_end();
	ray_queries = 1;
	CHECK(world_ao_mode() == (WORLD_AO_PROXIES | WORLD_AO_FIELD | WORLD_AO_RAYS), "with them: %d", world_ao_mode());
	world_ao_frame_begin();
	world_ao_window_begin(0, 0, camera);
	CHECK(rt_frames == 1, "the scene's frame: %lu", rt_frames);
	world_ao_object_begin(200);
	CHECK(object_owner == 1 && object_rays_owner == 201, "%d %lu", object_owner, object_rays_owner);
	world_ao_object_begin(-1);
	CHECK(object_rays_owner == 0, "the structure's owner: %lu", object_rays_owner);
	world_ao_first_person(1);
	CHECK(object_rays_owner == 0, "the first-person weapon's owner: %lu", object_rays_owner);
	world_ao_first_person(0);
	world_ao_window_end();
	/* ("world" again: none) */
	setting = "world";
	changes++;
	world_ao_frame_begin();
	world_ao_window_begin(0, 0, camera);
	CHECK(rt_frames == 1, "the scene's frame with \"world\": %lu", rt_frames);
	world_ao_object_begin(200);
	CHECK(object_rays_owner == 0, "an owner with \"world\": %lu", object_rays_owner);
	world_ao_window_end();
	world_ao_map_unloaded();
	CHECK(rt_unloads == 1, "the scene let go of at the map's end: %d", rt_unloads);
}

static void test_case(void)
{
	float const camera[3] = { 0.0f, 0.0f, 0.0f };

	setting = "world";
	test_setting = "plane 0 0 1 0; sphere 1 2 3 0.5; box 0 0 1 2 3 4; nonsense";
	changes++;
	world_ao_window_begin(0, 0, camera);
	CHECK(handed.count == 2 && fields_handed == 1, "%d %d", handed.count, fields_handed);
	CHECK(handed.proxies[0][0][3] == _world_proxy_sphere && handed.proxies[0][2][0] == 0.5f &&
		handed.proxies[0][1][3] == 200.0f, "the sphere");
	CHECK(handed.proxies[1][0][3] == _world_proxy_box && handed.proxies[1][1][0] == 2.0f &&
		handed.proxies[1][2][1] == 3.0f && handed.proxies[1][2][3] == 4.0f && handed.proxies[1][1][3] == 201.0f,
		"the box");
	CHECK(handed.parameters[0][2] == 1.0f && handed.field[0][3] == 4.0f, "its field, faded in");
	world_ao_window_end();
	world_ao_window_begin(0, 0, camera);
	CHECK(fields_handed == 1, "its field uploaded once: %d", fields_handed);
	world_ao_window_end();
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "off"))
		off_case();
	else if (!strcmp(name, "world"))
		world_case();
	else if (!strcmp(name, "window"))
		window_case();
	else if (!strcmp(name, "test"))
		test_case();
	else if (!strcmp(name, "rays"))
		rays_case();
	else
		return 2;
	return 0;
}
