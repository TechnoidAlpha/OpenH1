/*
WORLD_SDF.H

display.ambient_occlusion "world" (docs/world-ao.md, #578): a signed
distance field of each structure BSP's collision geometry, baked on the
player's own machine from the player's own map, once, and kept in the data
folder's world_sdf (as menu_pictures keeps its pictures: never shipped).
Baked off the critical path: in the start's "Preparing menu pictures" pass,
which loads every stock map anyway, or on a low-priority thread after a map
loads without one; never during a load, and a map with no field yet loads
exactly as fast as it would without the feature (the proxies alone shade
until the field is ready, then it fades in).

The field is sparse: a grid of cells, each either a brick of voxels (where
a surface is within range of it) or a single kind (far outside every
surface, or far inside solid), so that its memory follows the surfaces'
area and not the bounds' volume; and it covers the BSP where objects can
be (its placements' reach), not the sky's box or a sea floor out to the
horizon.

Workstream B owns this interface (port/linux/game/world_sdf.c, the bake's
pure arithmetic in port/linux/game/world_sdf_bake.c).
*/

#ifndef WORLD_SDF_H
#define WORLD_SDF_H

/* ---------- constants */

/* the cache's version: bumped whenever the bake or the format changes, so
that every field is baked again (2: sparse bricks, within the placements'
reach; 3: the static scenery in the field, and its occlusion of the level
in lightmap pages; 4: the vehicles' occlusion fields; 5: the scenery's
occlusion fields, of its drawn triangles, its leaves by their coverage,
which its static placements' pages are baked with too; 6: a box without
collision geometry of what it draws, nothing for one that draws nothing
opaque, in the pages, #773) */
#define WORLD_SDF_VERSION 6
/* the file's magic, "WSDF" */
#define WORLD_SDF_MAGIC 0x46445357UL

enum
{
	/* the finest voxel, in thousandths of a world unit (0.25) */
	WORLD_SDF_FINEST_VOXEL_MILLI = 250,
	/* a brick: a cell of WORLD_SDF_BRICK voxels' steps along each axis,
	its WORLD_SDF_BRICK_SPAN voxels a side at the steps' ends (its far
	faces' voxels are the next cell's near faces' too, so that a linear
	filter within one brick never reads another's) */
	WORLD_SDF_BRICK = 8,
	WORLD_SDF_BRICK_SPAN = WORLD_SDF_BRICK + 1,
	WORLD_SDF_BRICK_VOXELS = WORLD_SDF_BRICK_SPAN * WORLD_SDF_BRICK_SPAN * WORLD_SDF_BRICK_SPAN,
	/* the GPU's atlas of bricks: WORLD_SDF_ATLAS by WORLD_SDF_ATLAS bricks a
	layer, as many layers as the bricks fill */
	WORLD_SDF_ATLAS = 16,
	WORLD_SDF_ATLAS_LAYER = WORLD_SDF_ATLAS * WORLD_SDF_ATLAS,
	WORLD_SDF_ATLAS_LAYER_BYTES = WORLD_SDF_ATLAS_LAYER * WORLD_SDF_BRICK_VOXELS,
	/* a cell of the field's table: the atlas's brick x, y, z, and its kind
	(WORLD_SDF_CELL_*) */
	WORLD_SDF_CELL_BYTES = 4,
	/* the most GPU memory a field takes, its table and its atlas: the voxel
	grows past the finest (x1.125 a step) until it fits */
	WORLD_SDF_MAXIMUM_BYTES = 8 * 1024 * 1024,
	/* the most cells along an axis */
	WORLD_SDF_MAXIMUM_DIMENSION = 256,
	/* the distance the encoding holds each way, in voxels: past it, clamped */
	WORLD_SDF_RANGE_VOXELS = 12,
	/* the bounds padded each way, in voxels */
	WORLD_SDF_PADDING_VOXELS = 2,
	/* the field's bounds: the BSP's, cut to its placements' (the players',
	vehicles', units', items', devices' and the AI's) grown by this many
	world units each way: where objects can be */
	WORLD_SDF_PLACEMENTS_REACH = 32,
	/* the static scenery's occlusion of the level (struct
	world_sdf_scenery): the most bytes its pages take, the most and the
	least texels along a page's side, the most lightmap pages a BSP has,
	and the most placements baked */
	WORLD_SDF_SCENERY_MAXIMUM_BYTES = 4 * 1024 * 1024,
	WORLD_SDF_SCENERY_MOST_SIDE = 512,
	WORLD_SDF_SCENERY_LEAST_SIDE = 16,
	WORLD_SDF_SCENERY_MAXIMUM_PAGES = 1024,
	WORLD_SDF_SCENERY_MAXIMUM_PLACEMENTS = 4096,
	/* a page's layer where it has none: the lightmap pass walks the
	static scenery's shapes itself there (no page: a lightmap missing, a
	page whose texels two surfaces share) */
	WORLD_SDF_SCENERY_NO_LAYER = -1,
	/* its part of the file: its counts (page_count, layer_count, width,
	height, placement_count) and a placement's bytes */
	WORLD_SDF_SCENERY_COUNTS = 5 * 4,
	WORLD_SDF_SCENERY_PLACEMENT = 4 * 4,
	/* the objects' occlusion fields (struct world_sdf_object): the most a
	field holds, the most triangles of a piece of scenery's drawn geometry
	(its most detailed level within it, else its least), the most voxels
	(a vehicle's, a piece of scenery's) and voxels along a side one has, the GPU atlas's most texels along a
	side and bytes (4 halves a texel), the directions a voxel looks along
	and the normals its fit is made at, and an object's bytes in the file
	before its texels */
	WORLD_SDF_OBJECT_MAXIMUM = 64,
	WORLD_SDF_OBJECT_MOST_TRIANGLES = 2048,
	WORLD_SDF_OBJECT_MOST_VOXELS = 65536,
	/* (a piece of scenery's: a map has many, their occlusion is about
	them on the ground, softer than a vehicle's about its wheels, and their
	bake's time and the atlas's room go as their voxels) */
	WORLD_SDF_OBJECT_SCENERY_VOXELS = 16384,
	WORLD_SDF_OBJECT_MOST_SIDE = 96,
	WORLD_SDF_OBJECT_ATLAS_SIDE = 256,
	WORLD_SDF_OBJECT_ATLAS_BYTES = 8 * 1024 * 1024,
	WORLD_SDF_OBJECT_TEXEL = 8,
	WORLD_SDF_OBJECT_RAYS = 256,
	WORLD_SDF_OBJECT_NORMALS = 96,
	WORLD_SDF_OBJECT_ENTRY = 4 * 15,
	/* the bake's threads: the processors less those the game's own threads
	run on while it plays (world_sdf_bake_threads), at least one and at
	most WORLD_SDF_MAXIMUM_THREADS */
	WORLD_SDF_RESERVED_PROCESSORS = 3,
	WORLD_SDF_MAXIMUM_THREADS = 32,
	/* the bake's pace in play (struct world_sdf_pace): the frames it rests
	to learn their normal time, the frames whose median is compared with it,
	the least and the most frames a rest lasts (doubled each time frames
	run late again, halved after so many calm ones) */
	WORLD_SDF_PACE_LEARN = 30,
	WORLD_SDF_PACE_WINDOW = 8,
	WORLD_SDF_PACE_LEAST_REST = 60,
	WORLD_SDF_PACE_MOST_REST = 1920,
	WORLD_SDF_PACE_CALM = 600,
};

/* frames late while the bake runs in play: the last window's median more
than the normal's this many times and so many ms (the ms, so that a
machine at hundreds of frames a second does not rest for a millisecond) */
#define WORLD_SDF_PACE_LATE 1.10f
#define WORLD_SDF_PACE_SLACK_MS 1.0f

/* an object field's least voxel, its margin about the object (a share of
its longest side, between the least and the most, world units), and the
voxel's growth a step until it fits */
#define WORLD_SDF_OBJECT_LEAST_VOXEL 0.125f
#define WORLD_SDF_OBJECT_MARGIN 0.5f
#define WORLD_SDF_OBJECT_LEAST_MARGIN 1.5f
#define WORLD_SDF_OBJECT_MOST_MARGIN 4.0f
#define WORLD_SDF_OBJECT_GROWTH 1.125f

/* a cell's kind (struct world_sdf_volume's cells[i][3]): far outside every
surface (+range all through it), far inside solid (-range), or a brick of
the atlas (cells[i][0..2] its place there) */
enum
{
	WORLD_SDF_CELL_OUTSIDE = 0,
	WORLD_SDF_CELL_INSIDE = 1,
	WORLD_SDF_CELL_BRICK = 2,
};

/* a field's state (world_sdf_state) */
enum world_sdf_state
{
	_world_sdf_none, /* no map, or the feature off */
	_world_sdf_baking, /* none cached: baking in the background */
	_world_sdf_ready, /* loaded, and uploaded (or uploading: world_sdf_fade) */
	_world_sdf_failed, /* the bake failed: the proxies alone */
};

/* ---------- structures */

/* a field: width x height x depth cells of WORLD_SDF_BRICK voxels' steps a
side. Its voxels' centres are at origin + voxel * (x y z), x from 0 to
WORLD_SDF_BRICK * width (and y, z so); each holds the distance at its
centre over range (127 = +range: outside; -127 = -range: inside solid).

The cache file's header (little-endian, 72 bytes), followed by the cells
(4 bytes each, x fastest, then y, then z), the bricks and the static
scenery's part (struct world_sdf_scenery, scenery_bytes of it), deflated
(zlib) as one stream; a CRC-32 of the header and the deflated bytes ends
the file. Named <data>/world_sdf/<map name>_<bsp index>.wsdf */
struct world_sdf_header
{
	unsigned long magic; /* WORLD_SDF_MAGIC */
	unsigned long version; /* WORLD_SDF_VERSION */
	unsigned long map_checksum; /* the cache file header's checksum */
	unsigned long bsp_index;
	/* the cells along x, y, z */
	unsigned long width, height, depth;
	/* the first voxel's centre (x0 y0 z0) and the voxel's size, world units */
	float origin[3];
	float voxel;
	/* the distance +-127 stands for, world units */
	float range;
	unsigned long deflated_size;
	/* milliseconds the bake took, and on how many threads (logged, measured) */
	unsigned long bake_milliseconds;
	unsigned long bake_threads;
	/* the bricks */
	unsigned long brick_count;
	/* the static scenery's part's bytes, and the milliseconds its pages
	took of the bake */
	unsigned long scenery_bytes;
	unsigned long scenery_milliseconds;
	/* the objects' occlusion fields' part's bytes, and the milliseconds
	they took of the bake */
	unsigned long objects_bytes;
	unsigned long objects_milliseconds;
};

/* a static scenery placement baked (its shapes in the level's pages, its
collision geometry in the field): its definition (a tag index) and where it
stands, which the window finds its object by (world_sdf_scenery_baked) */
struct world_sdf_placement
{
	long definition;
	float position[3];
};

/* the static scenery's occlusion of the level, in the BSP's lightmaps'
space (docs/world-ao.md, "Static scenery"): for each of its lightmap pages,
the layer of an array of width x height pages of bytes (255 none: the
product of the static shapes' 1 - occlusion x fade at each texel's place
and normal) that the lightmap pass reads at the lightmap's own coordinates,
or WORLD_SDF_SCENERY_NO_LAYER; and the placements baked. Its part of the
file: page_count, layer_count, width, height, placement_count (4 bytes
each), each page's layer (2 bytes, signed), each placement (its definition,
its position: 4 bytes each), then the layers' texels */
struct world_sdf_scenery
{
	unsigned long page_count, layer_count, width, height, placement_count;
	short *layers;
	struct world_sdf_placement *placements;
	unsigned char *texels;
};

/* an object's occlusion field (docs/world-ao.md, "The objects' occlusion
fields"): the share of a point's cosine-weighted hemisphere its geometry
blocks there (a vehicle's collision, a piece of scenery's drawn triangles,
each by its opacity), as sat(t + n . v) of the point's unit normal n in the
object's space (node 0's, at rest), at each of width x height x depth
voxels' centres from origin, voxel apart, in that space: t and v.xyz, 4 half floats a voxel, x
fastest. Between voxels, trilinear; past the grid, its edge's value by
(L / (L + e))^2, L its edge's distance from the grid's middle, e the
point's past the edge (voxels). reach: past it, from the grid's middle
(voxels), the occlusion is under 1/510. bounds: its geometry's (x0 y0 z0
x1 y1 z1, its space), whose box's occlusion bounds its own (world_proxy_box_fade's
U: it is not computed where that is under 1/510). Its definition's tag
index names it; atlas its place in the GPU's atlas (not in the file) */
struct world_sdf_object
{
	long definition;
	unsigned long width, height, depth;
	float origin[3];
	float voxel;
	float reach;
	float bounds[6];
	unsigned short *texels;
	unsigned long atlas[3];
};

/* a field's objects' occlusion fields, and the GPU's atlas they are put
in (width x height x depth texels, each object at its atlas place; made
when the field is taken, not in the file). Its part of the file: the count
(4 bytes); each object's definition, width, height, depth, origin, voxel,
reach and bounds (4 bytes each); then their texels, object by object */
struct world_sdf_objects
{
	unsigned long count;
	struct world_sdf_object *objects;
	unsigned long atlas_width, atlas_height, atlas_depth;
};

/* a field in memory: its cells (width * height * depth, x fastest: a
brick's place in the atlas and the kind, WORLD_SDF_CELL_*) and its bricks
(brick_count * WORLD_SDF_BRICK_VOXELS, each x fastest; brick n is at the
atlas's n % WORLD_SDF_ATLAS, n / WORLD_SDF_ATLAS % WORLD_SDF_ATLAS,
n / WORLD_SDF_ATLAS_LAYER) */
struct world_sdf_volume
{
	struct world_sdf_header header;
	unsigned char (*cells)[WORLD_SDF_CELL_BYTES];
	signed char *bricks;
	struct world_sdf_scenery scenery;
	struct world_sdf_objects objects;
};

/* the bake's pace while a map is played (world_sdf_pace_frame): its
threads rest while the frames run late, so that a machine whose game
threads share its processors (a phone's few cores, the Steam Deck's
hyperthreads) never plays slower for it. Behind the menus it runs freely */
struct world_sdf_pace
{
	/* the frames' normal time (ms; 0: not known): a rest's last frames'
	median */
	float normal;
	/* the last frames' times (a ring, the newest at count - 1 mod window),
	and how many since the last rest ended */
	float frames[WORLD_SDF_PACE_WINDOW];
	long count;
	/* the rest: frames left of it, the late median it began for (0: one to
	learn), and the next rest's frames (doubled while resting helps; the
	least again when frames were as slow while it rested, late for
	something else) */
	long rest;
	float late;
	long next_rest;
	/* the frames baked since the last rest */
	long calm;
};

/* ---------- prototypes (port/linux/game/world_sdf.c) */

/* the game's hooks: a map loaded (finds its fields in the cache; starts
the background bake of those missing, at low priority, from a copy of the
geometry: world_scene_static_triangles), unloaded (a bake still running is
abandoned, its work kept for the next load of that map where it can be),
a BSP switched to (its field uploaded); each frame (a finished bake's field
uploaded, a few slices a frame so that nothing hitches) */
void world_sdf_map_loaded(void);
void world_sdf_map_unloaded(void);
void world_sdf_bsp_switched(short bsp_index);
void world_sdf_frame(void);
/* the "Preparing menu pictures" pass has the map loaded (menu_pictures_render.c):
its fields baked now if they are missing, where no one is waiting to play */
void world_sdf_startup_map_loaded(void);
/* any stock map's field missing (the pass runs for them too) */
int world_sdf_startup_wanted(void);
/* the frame drawn now prepares a map's pipelines (world_ao.c: its time is
the load's, not the frames' that HALO_WORLD_SDF_FRAMES measures) */
void world_sdf_frame_prepared(void);
/* a field handed to the backend whole, its table and its atlas, at once
(debug.world_ao_test's made-up one) */
void world_sdf_upload(struct world_sdf_volume const *volume);

/* the current BSP's field: its state, and how far it has faded in (0 to 1:
the occlusion's strength takes it, so that it never pops) */
int world_sdf_state(void);
float world_sdf_fade(void);
/* its header (NULL: none) */
struct world_sdf_header const *world_sdf_current(void);
/* whether an object of this definition standing here is a static scenery
placement that the drawn field and its pages hold (the window's proxies
put it apart: the lightmap pass reads its pages, the objects the field) */
int world_sdf_scenery_baked(long definition, float const position[3]);
/* whether it is one that the current BSP's field will hold while the
field is on its way (being looked for, baked or uploaded): the window
leaves it out until the field is drawn, which it then fades in with. As
proxies meanwhile, every pixel of the level walked the static scenery's
shapes (on an Apple M2 b30's frames took six times as long) */
int world_sdf_scenery_awaited(long definition, float const position[3]);
/* whether objects of this definition get an occlusion field with the
current BSP's field while that is on its way: their boxes are left out
until it is drawn (boxes cost the most of the proxies: on an Apple M2 the
vehicles' were most of the rest of that sixfold frame) */
int world_sdf_object_awaited(long definition);
/* the drawn field's occlusion field of an object of this definition
(NULL none: its proxies' boxes shade, as before), with the GPU's atlas's
size */
struct world_sdf_object const *world_sdf_object_field(long definition, unsigned long atlas[3]);
/* the layer in the drawn field's pages of a page of the loaded BSP's
lightmaps (its bitmap data, as the lightmap pass binds it), or
WORLD_SDF_SCENERY_NO_LAYER (none drawn, not one of its pages, or none baked
for it) */
int world_sdf_scenery_layer(void const *lightmap_bitmap);

/* ---------- the bake's pure arithmetic (port/linux/game/world_sdf_bake.c;
tools/harness/tests/world_sdf.c) */

/* the bake's threads on a machine of this many processors: forced if more
than 0 (HALO_WORLD_SDF_THREADS, for measuring), else all but
WORLD_SDF_RESERVED_PROCESSORS, which are the game's thread, the renderer's
(the command stream's: the GPU's encoding) and the sound's with the
system's own (its compositor and drivers); at least 1, at most
WORLD_SDF_MAXIMUM_THREADS */
long world_sdf_bake_threads(long processors, long forced);
/* a pace begun: free to run, the normal not known */
void world_sdf_pace_begin(struct world_sdf_pace *pace);
/* a frame's time (ms), and whether a map is played (not a menu, a loading
screen or the start's pass): TRUE if the bake may run until the next frame.
In play it first rests WORLD_SDF_PACE_LEARN frames to learn the normal;
then runs until the frames run late (their median: a bake's threads slow
every frame, and one slow frame now and then is something else's), rests
(learning the normal again), and runs again */
int world_sdf_pace_frame(struct world_sdf_pace *pace, float milliseconds, int playing);

/* the field's cells and origin for these bounds (x0 y0 z0 x1 y1 z1), padded,
at this voxel (world units) */
void world_sdf_layout(float const bounds[6], float voxel, struct world_sdf_header *header);
/* the cells of a layout that may hold a voxel within range of a triangle
(a triangle within range and half the cell's diagonal of its middle):
marks[cell] 1, the others 0; their count */
long world_sdf_mark(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char *marks);
/* the same for slices z_first to z_end - 1 alone (marks of the others
untouched: threads each mark some), their count */
long world_sdf_mark_slices(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char *marks, long z_first, long z_end);
/* the triangles that may be within range of each marked cell (the same
test): indices into positions' triangles, cell by cell, those of cell c
from firsts[c] to firsts[c + 1] - 1 (both malloc'd, firsts width * height *
depth + 1 long); NULL if no memory */
long *world_sdf_bin(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char const *marks, long **firsts);
/* its parts, for threads each taking some slices: the cells' triangles of
slices z_first to z_end - 1 counted (indices NULL: firsts[cell + 1]++, firsts
zeroed before) or put (indices[firsts[cell]++]); between the two passes the
counts made places (world_sdf_bin_place: the indices malloc'd), after the
second the places put back (world_sdf_bin_placed) */
void world_sdf_bin_slices(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char const *marks, long *firsts, long *indices, long z_first, long z_end);
long *world_sdf_bin_place(struct world_sdf_header const *header, long *firsts);
void world_sdf_bin_placed(struct world_sdf_header const *header, long *firsts);
/* the finest layout (from WORLD_SDF_FINEST_VOXEL_MILLI, the voxel x1.125
a step) of these triangles within bounds whose table and marked cells'
bricks fit WORLD_SDF_MAXIMUM_BYTES; its marks (malloc'd), NULL if no
memory. Each layout tried is marked by mark (NULL: world_sdf_mark; threads
may share it), which returns the count */
typedef long (*world_sdf_marker)(void *context, struct world_sdf_header const *header, float const *positions,
	long triangle_count, unsigned char *marks);
unsigned char *world_sdf_plan(float const bounds[6], float const *positions, long triangle_count,
	struct world_sdf_header *header, world_sdf_marker mark, void *context);
/* the GPU memory a layout's table and this many bricks take */
unsigned long world_sdf_bytes(struct world_sdf_header const *header, unsigned long bricks);
/* a distance (world units) encoded, and decoded */
signed char world_sdf_encode(float distance, float range);
float world_sdf_decode(signed char value, float range);
/* a cell's voxels baked into brick (WORLD_SDF_BRICK_VOXELS, x fastest):
each the least distance to these triangles (indices into positions'),
signed by inside(point), the collision BSP's point test (whether a point
is in solid); its kind: WORLD_SDF_CELL_BRICK if a voxel is within range,
else the side all of them are on */
int world_sdf_bake_cell(struct world_sdf_header const *header, unsigned long cell, float const *positions,
	long const *triangles, long triangle_count, int (*inside)(void *context, float const point[3]),
	void *context, signed char *brick);
/* the kind of a cell no surface is within range of: its middle's side */
int world_sdf_cell_kind(struct world_sdf_header const *header, unsigned long cell,
	int (*inside)(void *context, float const point[3]), void *context);
/* a field (header's cells, origin, voxel and range set) of a whole grid of
voxels, (WORLD_SDF_BRICK * width + 1) x ... x (WORLD_SDF_BRICK * depth + 1),
x fastest: its cells and bricks malloc'd; FALSE if no memory */
int world_sdf_pack(struct world_sdf_volume *volume, signed char const *voxels);
/* the field's distance at a point, trilinear, as the shaders read it */
float world_sdf_sample(struct world_sdf_volume const *volume, float const point[3]);
/* the GPU's atlas of the bricks: its layers, and a layer's voxels
(WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN square, WORLD_SDF_BRICK_SPAN deep,
x fastest: WORLD_SDF_ATLAS_LAYER_BYTES; the places past the last brick 0) */
unsigned long world_sdf_atlas_layers(struct world_sdf_header const *header);
void world_sdf_atlas_layer(struct world_sdf_volume const *volume, unsigned long layer, signed char *voxels);

/* ---------- the objects' occlusion fields (struct world_sdf_object) */

/* an object field's grid about its geometry's bounds (x0 y0 z0 x1 y1 z1,
its space): grown by its margin, at the least voxel that fits most_voxels
(WORLD_SDF_OBJECT_MOST_VOXELS a vehicle's, WORLD_SDF_OBJECT_SCENERY_VOXELS
a piece of scenery's) and WORLD_SDF_OBJECT_MOST_SIDE (its texels not
made) */
void world_sdf_object_layout(float const bounds[6], unsigned long most_voxels, struct world_sdf_object *object);
/* the directions and weights every slice reads, made once a bake and
shared by its threads (a slice allocates nothing: the guest's memory
mapped and unmapped meanwhile makes the renderer drop and remake its
buffers and textures over those pages, docs/world-ao.md); NULL if no
memory */
struct world_sdf_object_tables;
struct world_sdf_object_tables *world_sdf_object_tables_make(void);
void world_sdf_object_tables_free(struct world_sdf_object_tables *tables);
/* slice z of an object's voxels, its rows first_row to end_row (values
and solid its whole slice's: a row's are its own, so the rows may be
baked apart, a few at a time): each voxel's t, v.xyz (4 floats, x
fastest) fitted to the occlusion of these triangles (9 floats each) at
WORLD_SDF_OBJECT_NORMALS normals, from WORLD_SDF_OBJECT_RAYS directions
looked along. opacities: each triangle's, 0 to 1, the share of a
direction crossing it that it blocks (an alpha-tested leaf's texture's
coverage); NULL, every one opaque. solid[x] 1 where inside(point) (its
collision's point test: a voxel in solid, whose value is its neighbours'
after) */
void world_sdf_object_slice(struct world_sdf_object_tables const *tables, struct world_sdf_object const *object,
	float const *positions, float const *opacities, long triangle_count, int (*inside)(void *context,
	float const point[3]), void *context, unsigned long z, unsigned long first_row, unsigned long end_row, float *values,
	unsigned char *solid);
/* an object's texels made from its voxels' values (each in solid its
neighbours' outside, step by step), and its reach; FALSE if no memory */
int world_sdf_object_finish(struct world_sdf_object *object, float const *values, unsigned char const *solid);
/* a float as a half, and back (round to nearest even) */
unsigned short world_sdf_half(float value);
float world_sdf_half_value(unsigned short half);
/* an object field's occlusion at a point with this unit normal, both in
the object's space, as the shaders find it (0 to 1) */
float world_sdf_object_occlusion(struct world_sdf_object const *object, float const point[3], float const normal[3]);
/* the objects put in an atlas (each atlas place set, and the atlas's
size): those that fit WORLD_SDF_OBJECT_ATLAS_SIDE and _BYTES, in order, in
columns along z, rows of them along x and the rows along y, of the depth
and width that place the most in the least bytes; how many did (the rest:
none, atlas[0] ~0) */
unsigned long world_sdf_objects_place(struct world_sdf_objects *objects);

#endif
