/*
XGPU_MANIFEST.H

The renderer's manifest: every shader and pipeline the game draws with, as
the shared front end (d3d8_device.c) describes them to a backend, so that a
backend can build them ahead of time instead of at a draw. It is a property of
the game, not of a backend: it holds the shader IR's inputs (the NV2A
vertex programs' microcode, the pixel shader keys) and the pipeline keys
(xgpu_stream.h's fixed-width form of xgpu_backend.h's), with the vertex
layout and the texture stages' sampling that each draw of a key has. No
textures, geometry, names or other map content: a pixel shader key is the
combiner registers and render states the game's code set from its tags.

The front end records it (debug.render_manifest: a file each new draw
configuration is added to); tools/render_manifest.py merges recordings into
the committed one (port/assets/render/manifest.txt), whose README says how
it is made and checked. Any backend reads it with xgpu_manifest_read.

The file is text, a record a line ('#' starts a comment):

	manifest 1
	v <hash> <instructions> <microcode>
	p <hash> <field>=<value> ...
	d <field>=<value> ...
	s vp=<hash> pa=<mask> l=<layout> w=<strides>

v: a vertex program, named by its microcode's hash (xgpu_shader.h's,
sixteen hex digits), its instruction count and its words (eight hex digits
each, four an instruction, without spaces).
p: a pixel shader key (xgpu_stream_pixel_key), named by its hash
(xgpu_manifest_hash of its bytes): cs=<register>:<value>,... the combiner
states that are not 0, then tm (the texture modes), sa (the samplers'
types), ak (alpha kill), sg (colour sign), at (the alpha test's function),
fe, ft, cn, ca, pt, pl, as, mv, sb, dl (fog enabled, fog table mode, count
samples, coverage alpha, point threshold, per-pixel lighting, alpha test
samples, multiview, source blend, dynamic lights).
d: a draw's configuration: its pipeline key's fields (vp the vertex
program, pa packed attributes, im immediate, li lighting, vw views, bl
blend enabled/source/destination/operation, cm colour mask, dt depth
test/function/write, st stencil test/function/fail/depth fail/pass, sm
stencil read/write masks, cu cull/counter-clockwise/front, fi fill, db
depth bias, fm colour format/depth format/samples), px its pixel key's
hash; its vertex layout: l=<reg>.<stream>.<format>.<bytes>.<offset>/...
(xgpu_vertex_element) and w=<stream>:<stride>,... (the streams the
elements read), or i=<registers written> for immediate mode; and its
texture stages: t=<source>.<sampler type>.<address u>.<v>.<w>.<border
colour>/... (four; the border colour only where an axis is clamped to it,
else 0). A recording's draw is as no setting changes it, with its context
(xgpu_manifest_context): cl, ct, co, cd, ch, ck and cw.

s: a vertex shader as the game creates it: its program and its
declaration's layout (fields as a draw's of the same names), whichever
draws use it. These come from the game's code, not from play:
tools/render_manifest.py vertex writes them from the decompilation's
table (rasterizer_xbox_vertex_shaders.c and _initialize.c).
*/

#ifndef __HALO_XGPU_MANIFEST_H
#define __HALO_XGPU_MANIFEST_H

#include "xgpu_stream.h"

#include <stddef.h>
#include <stdint.h>

#define XGPU_MANIFEST_VERSION 2
#define XGPU_MANIFEST_ATTRIBUTES 16
#define XGPU_MANIFEST_STREAMS 16
#define XGPU_MANIFEST_MICROCODE_WORDS (136 * 4)

struct xgpu_manifest_vertex_program
{
	uint64_t hash;
	uint32_t instruction_count;
	uint32_t words[XGPU_MANIFEST_MICROCODE_WORDS];
};

struct xgpu_manifest_element
{
	uint8_t reg, stream, format, bytes;
	uint16_t offset;
};

/* a texture stage's sampling: XGPU_SOURCE_*, _xgpu_sampler_*,
XGPU_ADDRESS_* for u, v and w, the border colour (ARGB) where an axis is
XGPU_ADDRESS_BORDER, else 0 */
struct xgpu_manifest_stage
{
	uint8_t source, sampler_type, address[3];
	uint32_t border_color;
};

struct xgpu_manifest_draw
{
	struct xgpu_stream_pipeline_key key;
	/* immediate mode: the registers written; else the layout */
	uint32_t immediate_written;
	uint32_t element_count;
	struct xgpu_manifest_element elements[XGPU_MANIFEST_ATTRIBUTES];
	uint16_t strides[XGPU_MANIFEST_STREAMS];
	struct xgpu_manifest_stage stages[4];
};

/* what a draw's pipeline is derived from under the settings, that no
setting changes (a d record's cx: xgpu_manifest_derive). Its vertex
program's lighting (nv2a_vertex_lighting's lights: 0 none, 1 or 2; the
world's transform found in it); its targets: multisampled with
multisampling (a screen buffer's, or a target multisampled already), the
colour target in half floats with display.hdr (the back buffer's, the
screen effects' copy); the enhanced dynamic lights'
present in the frame and the structure's lightmap pass drawing; the game's
renderer's category (rasterizer.h's _rasterizer_profile_*, 255 outside one),
the 3D view drawn and what the game's code says of the draw (xgpu_hdr.h's
xgpu_hdr_draw, HALO_HDR_KIND_*); display.ambient_occlusion "world"'s light
for it, as it would be with "world" on (drawn among a window's opaque
surfaces, not the first-person weapon, not lighting itself: d3d8_device.c's
world_ao_applies); and whether its window is one the pre-pass finds the
world's occlusion for once a pixel (its span found, the first-person
weapon's too: world_ao_span_found), where the pre-pass draws its twin. */
struct xgpu_manifest_context
{
	uint8_t lights, world_found, multisampled, screen_colour;
	uint8_t lights_present, environment, hdr_category, hdr_view;
	uint16_t hdr_kind;
	uint8_t world, found;
};

struct xgpu_manifest
{
	struct xgpu_manifest_vertex_program *vertex_programs;
	uint32_t vertex_program_count;
	/* the draws, as no setting changes them (each with its context: what
	the settings derive it from, xgpu_manifest_derive), or as drawn (a map's
	enumeration, whose contexts are none) */
	struct xgpu_manifest_draw *draws;
	struct xgpu_manifest_context *contexts;
	uint32_t draw_count;
	/* the vertex shaders as the game creates them (s records): each a
	draw's vertex program, packed attributes and layout alone */
	struct xgpu_manifest_draw *vertex_shaders;
	uint32_t vertex_shader_count;
};

/* FNV-1a, 64 bits: a pixel key's name, a draw's identity */
uint64_t xgpu_manifest_hash(const void *data, size_t size, uint64_t hash);
#define XGPU_MANIFEST_HASH_START 14695981039346656037ULL

/* reads a manifest; returns 1, or 0 with a message in error (the line) */
int xgpu_manifest_read(const char *path, struct xgpu_manifest *manifest, char *error, size_t error_size);
void xgpu_manifest_free(struct xgpu_manifest *manifest);
/* the vertex program a key names, or NULL */
const struct xgpu_manifest_vertex_program *xgpu_manifest_vertex_program(const struct xgpu_manifest *manifest,
	uint64_t hash);

/* ---------- a draw's pipeline under the settings

The settings change parts of a draw's pipeline: its targets' samples
(display.anti_aliasing's multisampling) and colour format (display.hdr),
its pixel shader's alpha test by sample (with multisampling), its HDR tag,
and how it is lit (display.per_pixel_lighting, display.dynamic_lights,
display.ambient_occlusion). The front end derives them here from the draw's
facts as they are now (d3d8_device.c's prepare_draw); a manifest's draw is
the draw as no setting changes it, with its context, from which a backend's
build derives it here under every setting (xgpu_manifest_settings), so that
it makes every shader any setting draws with. */

struct xgpu_draw_facts
{
	/* the draw's: its blend enabled, a colour target, drawn in immediate
	mode, its attributes packed (NORMPACKED3), its alpha test's function */
	int blend, color, immediate;
	uint32_t packed_attributes, alpha_test;
	/* its vertex program's lighting (xgpu_manifest_context's), and whether
	lighting it for each pixel, or handing its ambient light on apart, has
	failed (the backend could not make it: d3d8_device.c) */
	int lights, world_found, lighting_failed, split_failed;
	/* the targets' samples a pixel; the colour target in half floats
	(display.hdr), and the draw's tag there (xgpu_hdr_classify) */
	int samples, hdr_target;
	uint16_t hdr;
	/* display.ambient_occlusion "world"'s light for the draw (xgpu.h's
	XGPU_AMBIENT_OCCLUSION_WORLD, and _WORLD_VIEW with debug.world_ao_view;
	0 none) */
	int world;
	/* lit for each pixel (display.per_pixel_lighting, or the enhanced
	dynamic lights); the enhanced lights lighting the frame; the structure's
	lightmap pass drawing */
	int per_pixel, lights_active, environment;
	/* the backend has a sample mask (xgpu_backend_capabilities) */
	int sample_mask;
};

/* what the facts make of the pipeline (xgpu_backend.h's XGPU_LIGHTING_*,
XGPU_FORMAT_*; xgpu.h's XGPU_DYNAMIC_LIGHTS_*, XGPU_AMBIENT_OCCLUSION_*) */
struct xgpu_draw_derived
{
	uint8_t lighting, color_format, samples;
	uint8_t alpha_test_samples, per_pixel_lighting, dynamic_lights, ambient_occlusion;
	uint16_t hdr;
};

/* the pipeline the facts give a draw, lit as the settings light it, or
(unlit: where the backend could not make that) lit by none of the shaders'
lighting; returns whether it is lit (a lighting not XGPU_LIGHTING_NONE) */
int xgpu_draw_derive(const struct xgpu_draw_facts *facts, int unlit, struct xgpu_draw_derived *derived);

/* the settings a manifest's draw is derived under: the targets' samples (1,
or display.anti_aliasing's multisampling's), display.hdr on, each pixel lit
(display.per_pixel_lighting), the enhanced dynamic lights, the ambient
occlusion (XGPU_MANIFEST_OCCLUSION_*); with the stream's backends' sample
mask and dynamic lights (Metal's and Vulkan's) */
struct xgpu_manifest_settings
{
	int samples, hdr, per_pixel_lighting, enhanced_lights, ambient_occlusion;
};

/* display.ambient_occlusion: off, or found in the world ("world"; without
debug.world_ao_view's light) */
enum
{
	XGPU_MANIFEST_OCCLUSION_OFF,
	XGPU_MANIFEST_OCCLUSION_WORLD,
	XGPU_MANIFEST_OCCLUSIONS
};

/* the index-th combination of every value of every setting that changes a
draw (multisampling's samples: d3d8_device.c's anti_aliasing_values'); 0
past the last */
int xgpu_manifest_settings(int index, struct xgpu_manifest_settings *settings);
/* a manifest's draw (draw, its context) as it is drawn under the settings,
or unlit (xgpu_draw_derive) into derived; returns whether it is lit. The
HDR tag is classified by xgpu_hdr.c (linked with this) */
int xgpu_manifest_derive(const struct xgpu_manifest_draw *draw, const struct xgpu_manifest_context *context,
	const struct xgpu_manifest_settings *settings, int unlit, struct xgpu_manifest_draw *derived);
/* display.ambient_occlusion "world"'s forms of a derived draw (derived, as
xgpu_manifest_derive gave it under the settings; its context) into forms:
the draw itself; under "world", where its window is found once a pixel
(context's found), the draw reading
its occlusion from the pass (XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED) where it
has the world's light, and the pre-pass's twin (xgpu_stream_world_ao_twin:
that one's, writing its pixel's inputs, or another opaque one's as an
occluder). Returns how many (1 to XGPU_MANIFEST_WORLD_AO_FORMS) */
#define XGPU_MANIFEST_WORLD_AO_FORMS 3
int xgpu_manifest_world_ao_forms(const struct xgpu_manifest_draw *derived, const struct xgpu_manifest_context *context,
	const struct xgpu_manifest_settings *settings, struct xgpu_manifest_draw forms[XGPU_MANIFEST_WORLD_AO_FORMS]);

/* ---------- the recorder (the front end's; debug.render_manifest) */

#ifndef XGPU_MANIFEST_READER_ONLY

struct xgpu_pipeline_key;
struct xgpu_texture_source;
struct xgpu_vertex_input;

/* whether draws are recorded (read once: debug.render_manifest) */
int xgpu_manifest_recording(void);
/* a vertex program's microcode as it is made (the words after its header) */
void xgpu_manifest_note_vertex_program(unsigned long long hash, const uint32_t *words, unsigned long instruction_count);
/* the next draw's textures, pipeline and vertices, then the draw */
/* (the four stages) */
void xgpu_manifest_note_textures(const struct xgpu_texture_source *stages);
void xgpu_manifest_note_pipeline(const struct xgpu_pipeline_key *key);
/* ...and the draw as no setting changes it, with its context: what a
recording writes (the pipeline above is the map's enumeration's) */
void xgpu_manifest_note_basis(const struct xgpu_pipeline_key *key, const struct xgpu_manifest_context *context);
void xgpu_manifest_note_vertices(const struct xgpu_vertex_input *input);
void xgpu_manifest_record_draw(int immediate, unsigned long written);
/* a comment line in the recording */
void xgpu_manifest_note(const char *text);
/* the draws from now until xgpu_manifest_enumerating(0, 0) are a map's
enumeration (port/linux/game/render_enumerate.c): new_map starts the map's
list, else (a new structure of the map) adds to it. Recorded, each draw
after that the list does not have is written again after a comment line,
"# missed:" and the return addresses of the stack that made it. */
void xgpu_manifest_enumerating(int enumerating, int new_map);
/* the map's enumeration: its draws (pipeline keys, pixel keys, layouts and
stages, as the stream describes them), and the vertex programs recorded
(debug.render_manifest; else none: the backend has made them all) */
const struct xgpu_manifest *xgpu_manifest_enumeration(void);

#endif

#endif
