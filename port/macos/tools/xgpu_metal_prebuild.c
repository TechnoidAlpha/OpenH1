/*
XGPU_METAL_PREBUILD.C

The app's Metal shaders and pipelines, made when it is built from the
renderer's manifest (port/assets/render/manifest.txt) by
tools/macos_shaders.py, which runs this:

	xgpu_metal_prebuild <manifest> <sources folder> <pipelines script> <library path> [<vertex shaders>]

The vertex shaders are the game's own, from its code (tools/render_manifest.py
vertex): each one's functions are made in every form a draw can need, so
that no vertex function depends on what play or a map's enumeration met.
For each of the manifest's draws it makes what the backend would make at
the draw, with the backend's own code: the stream's records the guest
would send (the pipeline's definition with its pixel program decoded, the
vertex program, its lighting), the draw's variant (the inputs read as
integers, the borders the shader draws: xgpu_metal_pipeline.m), the MSL
emitter's functions (xgpu_metal_shaders.c, through xgpu_metal_shader_create,
which here keeps each function's source), and the render pipeline state
(xgpu_metal_pipeline.m). It writes each function's source as <name>.metal
in the sources folder (named after its source's hash, as the backend looks
it up), with the clears' and the presentation's functions
(xgpu_metal_fixed.metal), the enhanced dynamic lights' glows' and
anti-aliasing's passes' (xgpu_metal_post_shaders.c), and the pipelines as a Metal pipelines script
(xgpu_metal_prebuild_script.m) whose functions are the library's at the
library path. Plain C: the shared headers' BOOL is not Objective-C's.
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_shader.h"
#include "xgpu_stream.h"
#include "xgpu_stream_describe.h"
#include "xgpu_manifest.h"
#include "xgpu_metal.h"
#include "xgpu_metal_pipeline.h"
#include "xgpu_metal_stream.h"
#include "xgpu_metal_prebuild.h"
#include "xgpu_metal_combiner.h"
#include "xgpu_metal_post_shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------- the functions the emitter makes (the backend's
xgpu_metal_shader_create, which compiles them) */

#define FUNCTION_NAME_SIZE 64

static char (*function_names)[FUNCTION_NAME_SIZE];
/* whether each is in the library (one the combiner's function draws is
not: the app specialises that for it, the same machine code) */
static unsigned char *function_written;
static uint32_t function_count, function_capacity;
static const char *sources_folder;

static void fail(const char *what)
{
	fprintf(stderr, "xgpu_metal_prebuild: %s\n", what);
	exit(1);
}

uint32_t xgpu_metal_pixel_shader_create(const char *source, size_t size, const char *entry,
	const uint32_t *constants)
{
	char path[1024];
	uint32_t index;

	for (index = 0; index < function_count; index++)
	{
		if (!strcmp(function_names[index], entry))
			return index + 1;
	}
	if (strlen(entry) >= FUNCTION_NAME_SIZE)
		fail("a function's name is too long");
	if (function_count == function_capacity)
	{
		function_capacity = function_capacity ? function_capacity * 2 : 1024;
		function_names = realloc(function_names, function_capacity * sizeof(*function_names));
		function_written = realloc(function_written, function_capacity);
		if (!function_names || !function_written)
			fail("out of memory");
	}
	function_written[function_count] = !constants;
	snprintf(path, sizeof(path), "%s/%s.metal", sources_folder, entry);
	if (!constants && !prebuild_write_text(path, source, size))
		fail("cannot write a function's source");
	snprintf(function_names[function_count], FUNCTION_NAME_SIZE, "%s", entry);
	return ++function_count;
}

static uint32_t functions_written(void)
{
	uint32_t index, count = 0;

	for (index = 0; index < function_count; index++)
		count += function_written[index];
	return count;
}

static const char *function_name(uint32_t handle)
{
	return handle && handle <= function_count ? function_names[handle - 1] : NULL;
}

/* ---------- the stream's records */

/* the vertex program's record, as the guest's encoder sends it
(xgpu_stream_encode.c); malloc'd */
static struct xgpu_stream_vertex_program *vertex_program_record(const struct xgpu_manifest_vertex_program *microcode,
	struct xgpu_vertex_program *program)
{
	struct xgpu_stream_vertex_program *record;
	DWORD words[XGPU_MANIFEST_MICROCODE_WORDS];
	uint32_t count, index;

	for (index = 0; index < microcode->instruction_count * 4; index++)
		words[index] = microcode->words[index];
	nv2a_vertex_program_decode(words, microcode->instruction_count, program);
	count = (uint32_t)program->instruction_count;
	if (count > XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM)
		count = XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM;
	record = calloc(1, sizeof(*record) + count * XGPU_STREAM_VERTEX_INSTRUCTION_SIZE);
	record->hash[0] = (uint32_t)program->hash;
	record->hash[1] = (uint32_t)(program->hash >> 32);
	record->instruction_count = count;
	record->projects = program->projects;
	memcpy(record->instructions, program->instructions, count * XGPU_STREAM_VERTEX_INSTRUCTION_SIZE);
	return record;
}

/* a model lighting program's record (d3d8_device.c's
halo_vertex_shader_lighting, xgpu_stream_encode.c's); 0 if it cannot be lit
for each pixel */
static int vertex_lighting_record(const struct xgpu_vertex_program *program, struct xgpu_stream_vertex_lighting *record)
{
	struct nv2a_vertex_lighting lighting;

	memset(record, 0, sizeof(*record));
	memset(&lighting, 0, sizeof(lighting));
	if (!nv2a_vertex_shader_lighting(program, &lighting))
		return 0;
	record->hash[0] = (uint32_t)program->hash;
	record->hash[1] = (uint32_t)(program->hash >> 32);
	record->lights = lighting.lights;
	record->normal_instruction = (uint32_t)lighting.normal_instruction;
	record->normal_register = (uint32_t)lighting.normal_register;
	record->position_instruction = (uint32_t)lighting.position_instruction;
	record->position_register = (uint32_t)lighting.position_register;
	record->world_found = lighting.world_found ? 1 : 0;
	record->world_instruction = (uint32_t)lighting.world_instruction;
	record->world_register = (uint32_t)lighting.world_register;
	return 1;
}

/* ---------- the draws */

/* the pipelines made, so that each is written once */
static struct xgpu_metal_pipeline_state_key *made;
static uint32_t made_count, made_capacity;

static int pipeline_new(const struct xgpu_metal_pipeline_state_key *key)
{
	uint32_t index;

	for (index = 0; index < made_count; index++)
	{
		if (!memcmp(&made[index], key, sizeof(*key)))
			return 0;
	}
	if (made_count == made_capacity)
	{
		made_capacity = made_capacity ? made_capacity * 2 : 1024;
		made = realloc(made, made_capacity * sizeof(*made));
		if (!made)
			fail("out of memory");
	}
	made[made_count++] = *key;
	return 1;
}

/* the derived draws made (their hashes: an open-addressed set), so that
each is made once */
static uint64_t *derived_slots;
static uint32_t derived_used, derived_capacity;

static int derived_new(const struct xgpu_manifest_draw *draw)
{
	uint64_t hash = xgpu_manifest_hash(draw, sizeof(*draw), XGPU_MANIFEST_HASH_START);
	uint32_t slot;

	if (!hash)
		hash = 1;
	if ((derived_used + 1) * 2 > derived_capacity)
	{
		uint64_t *old = derived_slots;
		uint32_t old_capacity = derived_capacity, at;

		derived_capacity = derived_capacity ? derived_capacity * 2 : 4096;
		derived_slots = calloc(derived_capacity, sizeof(*derived_slots));
		if (!derived_slots)
			fail("out of memory");
		derived_used = 0;
		for (at = 0; at < old_capacity; at++)
		{
			if (!old[at])
				continue;
			for (slot = (uint32_t)old[at] & (derived_capacity - 1); derived_slots[slot];
				slot = (slot + 1) & (derived_capacity - 1))
				;
			derived_slots[slot] = old[at];
			derived_used++;
		}
		free(old);
	}
	for (slot = (uint32_t)hash & (derived_capacity - 1); derived_slots[slot]; slot = (slot + 1) & (derived_capacity - 1))
	{
		if (derived_slots[slot] == hash)
			return 0;
	}
	derived_slots[slot] = hash;
	derived_used++;
	return 1;
}

/* a draw's pipeline: its shaders made and its state's key; 0 if its
shaders failed */
static int draw_pipeline(const struct xgpu_manifest *manifest, const struct xgpu_manifest_draw *draw,
	struct xgpu_metal_pipeline_state_key *state)
{
	const struct xgpu_metal_stream_shaders *shaders = xgpu_metal_shaders();
	const struct xgpu_manifest_vertex_program *microcode = xgpu_manifest_vertex_program(manifest,
		draw->key.vertex_program[0] | ((uint64_t)draw->key.vertex_program[1] << 32));
	struct xgpu_stream_pipeline_define definition;
	struct xgpu_stream_vertex_program *record;
	struct xgpu_stream_vertex_lighting lighting;
	struct xgpu_vertex_program *program;
	struct nv2a_pixel_shader_key pixel_key;
	struct xgpu_pixel_program pixel;
	struct xgpu_metal_stream_variant variant;
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_stream_vertex_element elements[XGPU_MANIFEST_ATTRIBUTES];
	uint32_t vertex_shader = 0, pixel_shader = 0, element, stage;
	uint16_t integers = 0;
	int lit, made_shaders;

	if (!microcode)
		fail("a draw's vertex program is not in the manifest");
	memset(&definition, 0, sizeof(definition));
	definition.key = draw->key;
	xgpu_stream_pixel_key_restore(&draw->key.pixel, &pixel_key);
	nv2a_pixel_program_decode(&pixel_key, &pixel);
	xgpu_stream_pixel_program_describe(&pixel, &definition.pixel_program);
	program = malloc(sizeof(*program));
	if (!program)
		fail("out of memory");
	record = vertex_program_record(microcode, program);
	/* (its lighting's record for every lit form, as the backend has it
	from the guest for the program whatever the draw: the split's vertex
	function reads its normal, position and lights too) */
	lit = draw->key.lighting != XGPU_LIGHTING_NONE && vertex_lighting_record(program, &lighting);
	free(program);
	/* the layout's attributes (immediate mode's: the registers written),
	and the draw's variant */
	xgpu_metal_pipeline_from_key(&draw->key, &pipeline);
	memset(elements, 0, sizeof(elements));
	for (element = 0; element < draw->element_count; element++)
	{
		elements[element].reg = draw->elements[element].reg;
		elements[element].stream = draw->elements[element].stream;
		elements[element].format = draw->elements[element].format;
		elements[element].bytes = draw->elements[element].bytes;
		elements[element].offset = draw->elements[element].offset;
	}
	if (draw->key.immediate)
	{
		xgpu_metal_layout_immediate(draw->immediate_written, pipeline.attributes, pipeline.strides);
	}
	else
	{
		integers = xgpu_metal_layout_attributes(elements, draw->element_count, pipeline.attributes);
		for (element = 0; element < XGPU_METAL_STREAM_COUNT && element < XGPU_MANIFEST_STREAMS; element++)
			pipeline.strides[element] = draw->strides[element];
	}
	memset(&variant, 0, sizeof(variant));
	variant.integer_attributes = draw->key.immediate ? 0 : integers;
	for (stage = 0; stage < 4; stage++)
	{
		struct xgpu_stream_sampler sampler;

		memset(&sampler, 0, sizeof(sampler));
		memcpy(sampler.address, draw->stages[stage].address, sizeof(sampler.address));
		sampler.border_color = draw->stages[stage].border_color;
		variant.border[stage] = xgpu_metal_border_axes(draw->stages[stage].source, draw->stages[stage].sampler_type,
			&sampler);
	}
	made_shaders = shaders->make(shaders->context, &definition, record, lit ? &lighting : NULL, &variant,
		&vertex_shader, &pixel_shader) && vertex_shader;
	free(record);
	if (!made_shaders)
		return 0;
	pipeline.vertex_shader = vertex_shader;
	pipeline.pixel_shader = pixel_shader;
	xgpu_metal_pipeline_state_key_make(&pipeline, state);
	return 1;
}

/* a vertex shader's functions (an s record: the game's code's), in every
form a draw can need: drawn from its layout or in immediate mode, unlit or
lit as the settings can light it (each pixel's lighting for a model's
lighting program, or its ambient light handed on apart for the ambient
occlusion; the dynamic lights for a structure's lightmap pass, whose
normal, binormal and tangent are packed: d3d8_device.c). The pixel
key is none: these draws are for their vertex functions alone */
static uint32_t vertex_shader_functions(const struct xgpu_manifest *manifest, const struct xgpu_manifest_draw *shader)
{
	const struct xgpu_manifest_vertex_program *microcode = xgpu_manifest_vertex_program(manifest,
		shader->key.vertex_program[0] | ((uint64_t)shader->key.vertex_program[1] << 32));
	struct xgpu_vertex_program *program;
	struct nv2a_vertex_lighting lighting;
	struct xgpu_stream_vertex_program *record;
	struct xgpu_metal_pipeline_state_key state;
	struct xgpu_manifest_draw draw;
	int lights = 0, form, failed = 0;

	if (!microcode)
		fail("a vertex shader's program is not in the manifest");
	program = malloc(sizeof(*program));
	if (!program)
		fail("out of memory");
	record = vertex_program_record(microcode, program);
	memset(&lighting, 0, sizeof(lighting));
	if (nv2a_vertex_shader_lighting(program, &lighting))
		lights = lighting.lights;
	free(program);
	free(record);
	for (form = 0; form < 6; form++)
	{
		draw = *shader;
		draw.key.color_format = XGPU_FORMAT_BGRA8;
		draw.key.samples = 1;
		switch (form)
		{
		case 0:
			draw.key.lighting = XGPU_LIGHTING_NONE;
			break;
		case 1:
			draw.key.immediate = 1;
			draw.key.packed_attributes = 0;
			draw.element_count = 0;
			draw.key.lighting = XGPU_LIGHTING_NONE;
			break;
		case 2:
		case 3:
			if (!lights || (lights == 2) != (form == 3))
				continue;
			draw.key.lighting = form == 3 ? XGPU_LIGHTING_MODEL_POINT : XGPU_LIGHTING_MODEL;
			break;
		case 4:
			/* (display.ambient_occlusion over a model lit for each vertex:
			its ambient light handed on apart) */
			if (!lights)
				continue;
			draw.key.lighting = XGPU_LIGHTING_MODEL_SPLIT;
			break;
		default:
			if ((shader->key.packed_attributes & 0xe) != 0xe)
				continue;
			draw.key.lighting = XGPU_LIGHTING_ENVIRONMENT;
			break;
		}
		if (!draw_pipeline(manifest, &draw, &state))
			failed++;
	}
	return failed;
}

int main(int argc, char **argv)
{
	struct xgpu_manifest manifest, vertex;
	char error[256], path[1024];
	uint32_t index, failed = 0, skipped = 0, derived_count = 0;
	unsigned long pipelines;
	int samples, parts, depth;

	if (argc != 5 && argc != 6)
	{
		fprintf(stderr, "usage: xgpu_metal_prebuild <manifest> <sources folder> <pipelines script> <library path> "
			"[<vertex shaders manifest>]\n");
		return 2;
	}
	/* (no manifest yet: the fixed functions and the combiner's alone) */
	memset(&manifest, 0, sizeof(manifest));
	if (access(argv[1], R_OK) == 0 && !xgpu_manifest_read(argv[1], &manifest, error, sizeof(error)))
	{
		fprintf(stderr, "xgpu_metal_prebuild: %s: %s\n", argv[1], error);
		return 1;
	}
	memset(&vertex, 0, sizeof(vertex));
	if (argc == 6 && !xgpu_manifest_read(argv[5], &vertex, error, sizeof(error)))
	{
		fprintf(stderr, "xgpu_metal_prebuild: %s: %s\n", argv[5], error);
		return 1;
	}
	sources_folder = argv[2];
	mkdir(sources_folder, 0777);
	snprintf(path, sizeof(path), "%s/xgpu_metal_fixed.metal", sources_folder);
	prebuild_write_fixed(path);
	snprintf(path, sizeof(path), "%s/xgpu_metal_combiner.metal", sources_folder);
	if (!prebuild_write_text(path, xgpu_metal_combiner_source, strlen(xgpu_metal_combiner_source)))
		fail("cannot write the combiner's function's source");
	/* each draw as every setting derives it (xgpu_manifest_derive: lit, and
	unlit where the backend could not light it), each derived draw once */
	for (index = 0; index < manifest.draw_count; index++)
	{
		struct xgpu_manifest_settings settings;
		int combination, unlit, lit;

		for (combination = 0; xgpu_manifest_settings(combination, &settings); combination++)
		{
			/* (unlit only where lit: else the same draw) */
			for (unlit = 0, lit = 1; unlit < 2 && lit; unlit++)
			{
				struct xgpu_manifest_draw derived;
				struct xgpu_manifest_draw forms[XGPU_MANIFEST_WORLD_AO_FORMS];
				int form, form_count;

				lit = xgpu_manifest_derive(&manifest.draws[index], &manifest.contexts[index], &settings, unlit, &derived);
				/* (display.ambient_occlusion "world"'s forms of it: finding its
				occlusion itself, reading it found once a pixel, and the
				pre-pass's twin, or another's as an occluder: xgpu_manifest.h) */
				form_count = xgpu_manifest_world_ao_forms(&derived, &manifest.contexts[index], &settings, forms);
				for (form = 0; form < form_count; form++)
				{
				struct xgpu_metal_pipeline_state_key state;
				struct xgpu_manifest_draw draw = forms[form];

				if (!derived_new(&draw))
					continue;
				derived_count++;
				if (!draw_pipeline(&manifest, &draw, &state))
				{
					failed++;
					continue;
				}
				if (!xgpu_metal_pipeline_archivable(&state))
				{
					skipped++;
					continue;
				}
				if (!function_written[state.pixel_shader - 1])
					continue;
				if (pipeline_new(&state))
					prebuild_script_pipeline(&state, function_name(state.vertex_shader),
						function_name(state.pixel_shader));
				}
			}
		}
	}
	/* every vertex shader the game creates, in each form (its pipelines are
	the draws'; these make no more) */
	for (index = 0; index < vertex.vertex_shader_count; index++)
		failed += vertex_shader_functions(&vertex, &vertex.vertex_shaders[index]);
	/* the clears drawn as quads (the game's targets: BGRA8, with D24S8 or
	not, in each of the multisampling's sample counts on Apple's GPUs, at most
	4; the channels the game's colour write masks leave: all, rgb, alpha; one
	that writes no colour has no fragment stage) and the presentation's */
	for (samples = 1; samples <= 4; samples *= 2)
	{
		for (depth = 0; depth < 2; depth++)
		{
			static const int masks[] = { XGPU_METAL_CLEAR_CHANNELS, XGPU_METAL_CLEAR_RED | XGPU_METAL_CLEAR_GREEN |
				XGPU_METAL_CLEAR_BLUE, XGPU_METAL_CLEAR_ALPHA };

			for (parts = 0; parts < 3; parts++)
			{
				struct xgpu_metal_clear_key key;

				memset(&key, 0, sizeof(key));
				key.color_format = XGPU_METAL_FORMAT_BGRA8;
				key.depth_format = depth ? XGPU_METAL_FORMAT_D24S8 : 0;
				key.samples = (uint8_t)samples;
				key.parts = (uint8_t)masks[parts];
				prebuild_script_clear(&key);
			}
		}
	}
	prebuild_script_present();
	/* the fixed functions of the stream's own passes: the glows' and
	anti-aliasing's (their pipelines are made as the setting is chosen) */
	{
		uint32_t halo_vertex, halo_pixel;

		if (!xgpu_metal_shaders()->halo(NULL, &halo_vertex, &halo_pixel))
			fail("cannot write the glows' functions");
		/* (display.ambient_occlusion "world"'s pass's) */
		if (!xgpu_metal_shaders()->world_ao_pass(NULL, &halo_vertex, &halo_pixel))
			fail("cannot write the world occlusion's pass's functions");
		for (index = 0; index < XGPU_METAL_POST_FUNCTION_COUNT; index++)
			xgpu_metal_pixel_shader_create(xgpu_metal_post_functions[index].source, xgpu_metal_post_functions[index].size,
				xgpu_metal_post_functions[index].entry, NULL);
	}
	pipelines = prebuild_script_write(argv[3], argv[4]);
	printf("%u draws (%u under every setting), %u vertex shaders: %u functions (%u in the library), %lu pipelines (%u draws' "
		"writing no colour, not archived; %u whose shaders failed)\n", manifest.draw_count, derived_count,
		vertex.vertex_shader_count, function_count, functions_written(), pipelines, skipped, failed);
	xgpu_manifest_free(&manifest);
	xgpu_manifest_free(&vertex);
	return failed ? 1 : 0;
}
