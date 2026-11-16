/*
WORLD_AO.H

display.ambient_occlusion "world" (docs/world-ao.md, #578): ambient
occlusion found in world space, from the structure's baked distance field
(world_sdf.h) and the objects' analytic proxies (world_proxies.h), applied
to the ambient term alone in the shaders (xgpu.h's
XGPU_AMBIENT_OCCLUSION_LIGHT_*). Render-only and deterministic: no noise, no
history, nothing from the screen. Off ("off", and Classic's preset, which
sets "off") draws exactly as before.

The renderer's side of a frame (this file's game hooks, called from
source/render/render.c), and the front end's (halo_world_ao_*,
port/linux/src/d3d8_device.c, which hands it to the backend:
xgpu_backend_world_ao_*). Workstream D owns the shading and this file's
setting; A, B and C fill what it hands on.
*/

#ifndef WORLD_AO_H
#define WORLD_AO_H

/* ---------- constants */

/* display.ambient_occlusion's world mode's parts (world_ao_mode) */
enum
{
	WORLD_AO_PROXIES = 1, /* the objects' proxies */
	WORLD_AO_FIELD = 2, /* the structure's distance field (on objects alone) */
	/* "ray_traced": the dynamic objects' occlusion traced by the GPU's ray
	queries, through their collision meshes (world_rt.h), where the backend
	has them; "world" otherwise */
	WORLD_AO_RAYS = 4,
};

/* a draw's owner (halo_world_ao_object), past the window's proxies' owner
slots (0 up): none (the structure, an object with no proxies: every proxy
shades it), and drawn without the occlusion (the first-person weapon) */
enum
{
	WORLD_AO_OWNER_NONE = -1,
	WORLD_AO_OWNER_UNLIT = -2,
};

/* the window's span as the game draws it (halo_world_ao_span): outside
one, its opaque surfaces, the first-person weapon among them; and the same
in a window whose occlusion the pre-pass finds once a pixel where the
setting is on (world_ao_prepass_begin: the primary view, into the back
buffer with its depth buffer, whatever the setting), so that a map's
preparation makes the pipelines its draws have then, and their twins */
enum
{
	WORLD_AO_SPAN_NONE,
	WORLD_AO_SPAN_OPEN,
	WORLD_AO_SPAN_FIRST_PERSON,
	WORLD_AO_SPAN_FOUND,
	WORLD_AO_SPAN_FOUND_FIRST_PERSON,
};

/* the shading's constants (docs/world-ao.md, "Shading"; the emitters' are
xgpu.h's XGPU_WORLD_AO_*, which port/linux/src/d3d8_device.c checks are
these): the cone's gain, its first step's least (world units) and taps; the
least reference divided by; the share of a proxy's reach it fades out
over; the least the occlusion leaves; the strengths on objects and on the
level ("world" is the strong preset); the reference's weight */
#define WORLD_AO_K 1.6f
#define WORLD_AO_STEP 0.15f
#define WORLD_AO_TAPS 5
#define WORLD_AO_REFERENCE_LEAST 0.25f
#define WORLD_AO_REACH_FADE 0.25f
#define WORLD_AO_FLOOR 0.2f
#define WORLD_AO_STRENGTH_OBJECTS 1.0f
#define WORLD_AO_STRENGTH_LEVEL 0.8f
#define WORLD_AO_REFERENCE_WEIGHT 1.0f
/* a model draw's reference for a static scenery placement's own draws:
the field holds its own geometry, so its taps are taken from the pixel's
own depth inside it (world_ao_occlusion), relative to no reference */
#define WORLD_AO_REFERENCE_NO_FIELD -1.0f

/* the window's proxy grid (world_ao_grid; xgpu.h's XGPU_WORLD_AO_GRID_*):
cells about the camera, boxes in the world (eye-independent), each with a
64-bit mask of the proxies whose reach can touch it, so a pixel computes
only those. As the enhanced dynamic lights' grid, a cell's index along an
axis is log2(1 + |offset| / WORLD_AO_GRID_CELL) times that axis's scale,
from the middle: small cells near the camera, the outermost reaching
WORLD_AO_GRID_REACH world units and holding whatever reaches past it */
#define WORLD_AO_GRID_X 16
#define WORLD_AO_GRID_Y 16
#define WORLD_AO_GRID_Z 8
#define WORLD_AO_GRID_CELL 1.0f
#define WORLD_AO_GRID_REACH 128.0f
#define WORLD_AO_GRID_CELLS (WORLD_AO_GRID_X * WORLD_AO_GRID_Y * WORLD_AO_GRID_Z)

struct world_proxy;
struct world_sdf_volume;

/* ---------- prototypes (port/linux/game/world_ao.c) */

/* display.ambient_occlusion is "world" or "ray_traced": WORLD_AO_* parts
on (WORLD_AO_RAYS where "ray_traced" is and the backend has ray queries);
0 off (any other value, the VR mode's eye passes until they are done, the
Classic preset) */
int world_ao_mode(void);

/* the map loaded (scenario_load: its scene found, its fields looked for in
the cache; a missing one baked in the background, never here), a BSP
switched to (scenario_switch_structure_bsp), the map unloaded
(scenario_unload: everything kept of it let go, the bake abandoned). Off,
nothing but the unload, which frees what an earlier "world" kept */
void world_ao_map_loaded(void);
void world_ao_bsp_switched(short bsp_index);
void world_ao_map_unloaded(void);

/* each frame (render_frame): the scene's objects found again
(world_scene_frame_begin), a field's upload continued (world_sdf_frame) */
void world_ao_frame_begin(void);
/* a window's 3D view (render_window): its proxies and the field's place
handed to the renderer before its opaque surfaces draw, and taken away
after the lightmap pass (the later passes never read it) */
void world_ao_window_begin(short local_player_index, short rasterizer_target, float const camera_position[3]);
void world_ao_window_end(void);
/* ... found once a pixel (port/linux/src/xgpu_stream.h's WORLD_AO_PREPASS):
the window's opaque surfaces' pre-pass begun (FALSE: none, the surfaces
find their own as they draw), and ended, the occlusion found from it over
the window's viewport (the game's screen's bounds) as the camera saw it
(xgpu_stream.h's struct xgpu_world_ao_pass's camera) */
int world_ao_prepass_begin(void);
void world_ao_prepass_end(short x0, short y0, short x1, short y1, float const camera[4][4]);
/* which object is drawn now (render_objects: its own proxies do not shade
it; -1 none, the structure); the first-person weapon drawn (TRUE) and done
(FALSE: none drawn now), which the occlusion leaves as it is */
void world_ao_object_begin(long object_index);
void world_ao_first_person(int drawing);
/* the model shader drawn next is a two-sided one's cards (TRUE: its
_shader_model_two_sided_bit, a plant's leaves; FALSE after its draws):
their occlusion is taken about their most open direction, the sky's or
either face's, not each card's one normal (world_ao_cards_occlusion;
rasterizer_xbox_models.c's _rasterizer_model_draw) */
void world_ao_cards(int cards);
/* the structure's lightmap pass draws with this page of its lightmaps
next (rasterizer_environment_lightmap_begin; NULL none): its static
scenery's layer, or the static scenery's shapes walked where it has none */
void world_ao_lightmap_begin(void const *lightmap_bitmap);

/* ---------- the arithmetic (the shaders' reference: xgpu.h's
XGPU_AMBIENT_OCCLUSION_WORLD, the emitters' world_ao_*; tools/harness/
tests/world_ao.c) */

/* A_sdf: the share of the ambient light the field leaves a point with this
normal (1 none: no volume) */
float world_ao_field_occlusion(struct world_sdf_volume const *volume, float const point[3], float const normal[3]);
/* the same with each tap's distance taken from own (the pixel's own depth
inside solid, 0 or less: a static scenery placement's own draw) */
float world_ao_field_occlusion_from(struct world_sdf_volume const *volume, float const point[3], float const normal[3],
	float own);
/* an object's reference: A_sdf where its ambient light was sampled, above
its lighting point, facing up */
float world_ao_reference(struct world_sdf_volume const *volume, float const lighting_point[3]);
/* A_prox: the share the proxies (their owner slots in v[1][3]) of other
owners than owner leave */
float world_ao_proxies_occlusion(struct world_proxy const *proxies, long count, float const point[3],
	float const normal[3], float owner);
/* the atlas of the objects' occlusion fields the reference reads
(world_sdf.h's struct world_sdf_objects, as the GPU has it: width x height
x depth texels of four half floats; NULL none: the tests', and the bake's
while it bakes the static scenery's pages), and an object's field's
occlusion at a point (its three shapes from proxy: xgpu_stream.h's
XGPU_WORLD_AO_PROXY_FIELD), as the shaders find it */
void world_ao_objects_atlas(unsigned short const *texels, unsigned long width, unsigned long height,
	unsigned long depth);
float world_ao_field_proxy_occlusion(struct world_proxy const *proxy, float const point[3], float const normal[3]);
/* its share kept at a point by its geometry's box's bound (world_proxies.h's
world_proxy_box_fade, the box in its voxels): 0 where that is under 1/510 */
float world_ao_field_proxy_fade(struct world_proxy const *proxy, float const point[3], float const normal[3]);
/* a lightmap page of the static scenery's occlusion of the level
(world_sdf.h's struct world_sdf_scenery): the triangles that draw with it
(WORLD_AO_SCENERY_TRIANGLE floats each: three corners' positions, then their
normals, then their lightmap coordinates, 0 to 1) rasterized at width x
height texels' centres, each the product of the shapes' 1 - occlusion x
fade (A_prox, owner none) at its place and normal, as a byte (255: 1); the
texels no triangle covers take their covered neighbours' (two steps out),
else 255. Returns how many texels were covered, and in *shared how many of
them two triangles at different places cover (the page then means two
things there). Pure; texels width * height bytes */
enum
{
	WORLD_AO_SCENERY_TRIANGLE = 24,
};
long world_ao_scenery_page(struct world_proxy const *shapes, long shape_count, float const *triangles,
	long triangle_count, unsigned long width, unsigned long height, unsigned char *texels, long *shared);
/* the window's proxy grid about the camera: grid[0] the camera (w 0);
grid[1] each axis's scale, one over WORLD_AO_GRID_CELL; cells[i / 2][2 (i % 2) + w]
word w of cell i's mask ((z * WORLD_AO_GRID_Y + y) * WORLD_AO_GRID_X + x),
bit b of word w proxy 32 w + b. A proxy is in every cell its reach (a box's:
the box grown by the distance its occlusion's bound falls under 1/510, if
less) touches. Pure; the block's (xgpu_stream.h's struct
xgpu_world_ao_block: grid, cells) */
void world_ao_grid(struct world_proxy const *proxies, long count, float const camera[3], float grid[2][4],
	unsigned int cells[WORLD_AO_GRID_CELLS / 2][4]);
/* the cell a point is in (as the shaders find it) */
int world_ao_grid_cell(float const grid[2][4], float const point[3]);
/* A_prox as the shaders find it: the proxies of the point's cell alone,
in the same order (what world_ao_proxies_occlusion finds, exactly: the
others' reach does not touch the cell) */
float world_ao_proxies_occlusion_grid(struct world_proxy const *proxies, float const grid[2][4],
	unsigned int const cells[WORLD_AO_GRID_CELLS / 2][4], float const point[3], float const normal[3], float owner);
/* the occlusion a draw's pixel multiplies its ambient light by: a model's
(lightmap FALSE: the field relative to its reference, faded in (where the
reference is negative, from the pixel's own depth in it, relative to none),
times the dynamic proxies) or the lightmap pass's (the proxies alone, or, where its
page has a layer (reference, as the lightmap pass's draws give it, 0 or
more), the dynamic proxies times scenery, the page's texel there), the
parameters as the block's (xgpu_stream.h's struct xgpu_world_ao_block:
parameters[1][1] is how many of the proxies are the dynamic objects', the
static scenery's after them); a model's two-sided shader's cards (cards
TRUE) about their most open direction (world_ao_cards_occlusion) */
float world_ao_occlusion(float const parameters[2][4], struct world_sdf_volume const *volume,
	struct world_proxy const *proxies, long count, float const point[3], float const normal[3], float owner,
	float reference, int lightmap, float scenery, int cards);
/* of a window's proxies, how many a draw walks: the dynamic objects'
alone, or every one (the lightmap pass where its page has no layer) */
long world_ao_proxies_walked(float const parameters[2][4], long count, float reference, int lightmap);

/* found once a pixel (port/linux/src/xgpu_stream.h's WORLD_AO_PREPASS:
the pre-pass's inputs, then the occlusion's pass; the
emitters' world_ao_pass_*). A pixel's world position, its distance from
the eye (the block's grid[0], from which the pre-pass measured it) along
the ray through its centre (x, y in its target's pixels) as the camera
(struct xgpu_world_ao_pass's) saw it */
void world_ao_pass_position(float const camera[4][4], long const corners[4], float x, float y, float distance,
	float const eye[3], float point[3]);
/* a pixel's inputs as the pre-pass writes them (xgpu.h's
XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS: the normal octahedral, the owner, the
reference or the page's texel as a half, the lightmap pass's, every shape
walked; its distance from the eye, a float's bits; a word spare), and read
again (0: none drawn there) */
#define XGPU_WORLD_AO_INPUT_LIGHTMAP_BIT 0x100u
#define XGPU_WORLD_AO_INPUT_ALL_SHAPES_BIT 0x200u
#define XGPU_WORLD_AO_INPUT_PRESENT_BIT 0x400u
#define XGPU_WORLD_AO_INPUT_CARDS_BIT 0x800u
/* the field's occlusion at a two-sided model shader's card (a plant's
leaf: world_ao_cards), taps from own as world_ao_field_occlusion_from's:
that of its most open direction of the sky's (up, as the object's
reference finds it) and its two faces', the first of them where alike.
A card is thin and drawn alike from both sides, and lit through either
face: the field's cone along its one normal finds the ground below a
horizontal card, or the wall a card faces, where the card is open to the
sky (#781). The direction found is put in normal, which the shapes and
rays are then found about */
float world_ao_cards_occlusion(struct world_sdf_volume const *volume, float const point[3], float normal[3],
	float own);
void world_ao_inputs_pack(float const normal[3], float owner, float value, int lightmap, int all_shapes, int cards,
	float distance, unsigned int words[4]);
int world_ao_inputs_unpack(unsigned int const words[4], float normal[3], float *owner, float *value, int *lightmap,
	int *all_shapes, int *cards, float *distance);
/* the occlusion at a pixel from its inputs (1 none) and its position, as
the occlusion's pass finds it: world_ao_occlusion's, its proxies those of
the point's cell alone */
float world_ao_pass_occlusion(float const parameters[2][4], struct world_sdf_volume const *volume,
	struct world_proxy const *proxies, long count, float const grid[2][4],
	unsigned int const cells[WORLD_AO_GRID_CELLS / 2][4], float const point[3], unsigned int const words[4]);

/* the occlusion found at a quarter of the pixels (scale 2: each 2x2
block's, at its nearest pixel; 1, each pixel's own), as the pass writes it
(four floats a texel: the occlusion, the pixel's distance from the eye
(0 none), its normal octahedral) and a surface's pixel reads it (at its
centre x, y, in its target's pixels, its distance from the eye, how
squarely it faces the eye (|n . the eye's ray|) and its unit normal): the
four texels about it by their bilinear weights and their closeness, each
their distance's (relative, by how squarely the surface faces the eye, no
less than WORLD_AO_UPSAMPLE_FACING; WORLD_AO_UPSAMPLE_DEPTH of it halving
it) times their normal's (the cosine's eighth power); where their weights'
sum is under WORLD_AO_UPSAMPLE_LEAST, the closest texel of the 4x4 about
it (the emitters' world_ao_upsample) */
struct world_ao_pass_texel
{
	float occlusion, distance, normal[2];
};
#define WORLD_AO_UPSAMPLE_DEPTH 0.02f
#define WORLD_AO_UPSAMPLE_FACING 0.2f
#define WORLD_AO_UPSAMPLE_LEAST 1.0e-4f
float world_ao_upsample(struct world_ao_pass_texel const *texels, long width, long height, float scale, float x,
	float y, float distance, float facing, float const normal[3]);

/* ---------- the front end's (port/linux/src/d3d8_device.c) */

/* the window's world occlusion: the field's place and the parameters
(xgpu_stream.h's struct xgpu_world_ao_block: field, parameters), the
proxies (three vec4s each: world_proxies.h's struct world_proxy) and their
grid (world_ao_grid). count < 0: none (the window draws as without it,
from here on) */
void halo_world_ao_window(float const field[2][4], float const parameters[2][4], float const (*proxies)[3][4],
	int count, float const grid[2][4], unsigned int const (*cells)[4]);
/* the object drawn next: its slot among the window's proxies' owners
(WORLD_AO_OWNER_NONE: none, the structure; WORLD_AO_OWNER_UNLIT: drawn
without the occlusion), its reference, the field's occlusion at its
lighting point, which its ambient sample already holds (1: none), and with
"ray_traced" its owner among the ray-traced scene's instances (world_rt.h's
world_rt_owner: 0 none), whose own the rays leave out */
void halo_world_ao_object(int owner, float reference, unsigned long rays_owner);
/* the draws next are two-sided cards (world_ao_cards), or not */
void halo_world_ao_cards(int cards);
/* "ray_traced": whether the backend traces the occlusion's rays by the
GPU's ray queries (FALSE until it has said: "world" is drawn), and why not
where it has said it does not (NULL: it does, or has not said) */
int halo_world_ao_ray_queries(void);
char const *halo_world_ao_ray_queries_missing(void);
/* ... its scene (port/linux/src/xgpu_stream.h's WORLD_AO_MESH, _INSTANCES):
a mesh, count triangles of nine floats (id 0: every mesh let go of), and
the frame's instances of the meshes (struct world_ao_instance, as
xgpu_stream.h's struct xgpu_world_ao_instance) */
struct world_ao_instance
{
	float transform[3][4];
	unsigned int mesh, owner, kind, pad;
};
void halo_world_ao_mesh(unsigned long id, unsigned long count, float const *triangles);
void halo_world_ao_instances(struct world_ao_instance const *instances, unsigned long count);
/* where the window's opaque surfaces are drawn (WORLD_AO_SPAN_*), whatever
the setting and while a map's pipelines are prepared: the draws that take
the world's light when it is on, which the renderer's manifest notes of
each draw with it off too, so that the Mac's and iOS's shader libraries
have the world's (xgpu_manifest.h's context) */
void halo_world_ao_span(int span);
/* the window's pre-pass begun (FALSE: none can be drawn), and ended, the
occlusion found from it (xgpu_backend_world_ao_prepass); whether one would
be drawn into the window's target as it is now, whatever the setting
(halo_world_ao_prepass_found: its span WORLD_AO_SPAN_FOUND); ended with the
distance-field scene's cascades (world_cascades.h's block), the frame's
number, and whether this window's previous frame's occlusion may be
reprojected (one window a frame, this and the last) */
int halo_world_ao_prepass_found(void);
int halo_world_ao_prepass_begin(void);
void halo_world_ao_prepass_end(short x0, short y0, short x1, short y1, float const camera[4][4],
	float const cascades[4][4], unsigned long frame, int history);
/* a part of the field uploaded (WORLD_AO_FIELD_*: world_sdf.h's table of
cells, 4 bytes each, or its atlas of bricks' voxels, signed bytes), slices
z_first to z_first + z_count - 1 of a width * height * depth volume; width
0 releases the field, both parts. The field is drawn with once both have
had their last slices */
enum
{
	WORLD_AO_FIELD_TABLE = 0,
	WORLD_AO_FIELD_ATLAS = 1,
	/* (the static scenery's pages: width x height bytes, depth layers;
	sent before the table) */
	WORLD_AO_FIELD_SCENERY = 2,
	/* (the objects' occlusion fields' atlas: width x height texels of 4
	half floats, depth slices; sent before the table) */
	WORLD_AO_FIELD_OBJECTS = 3,
};
void halo_world_ao_field(int part, unsigned long width, unsigned long height, unsigned long depth,
	unsigned long z_first, unsigned long z_count, void const *data);
/* a box of the distance-field scene's cascades' texture (world_cascades.h:
its first texel x, y, z, its size, two signed bytes a texel, x fastest);
width 0 releases it */
void halo_world_ao_cascades(unsigned long x, unsigned long y, unsigned long z, unsigned long width, unsigned long height,
	unsigned long depth, void const *data);

#endif
