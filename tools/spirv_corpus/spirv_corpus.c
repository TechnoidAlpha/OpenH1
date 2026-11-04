/*
SPIRV_CORPUS.C

The SPIR-V emitter's corpus (port/linux/src/xgpu_spirv.c): every captured
vertex program in every variant the Vulkan backend draws, and every captured
pixel shader key, written as SPIR-V modules, one .spv file each, for
spirv-val to check (check.sh). Identical modules are written once
(index.txt names each variant's file). The variants include the flat and VR views, with and without foveation.
The made-up programs are msl_corpus's (tools/msl_corpus/synthetic.c), plus
layered texture lookups in each stage; the enhanced dynamic lights'
glows, in each of the layers they are drawn into; display.hdr's tags
(xgpu_hdr.h: every ceiling, expanded or not, the alpha limit and the
dynamic lights past white, over each blend, into a target of one sample and
of four); and anti-aliasing's and display.hdr's passes
(port/linux/vulkan/xgpu_vulkan_post_shaders.c, generated from
tools/vulkan_post_shaders.py's GLSL and xgpu_hdr.c's).

  spirv_corpus <capture dir> <out dir>

The capture directory holds what a run writes with the renderer's test
capture (HALO_TEST_CAPTURE): vsNNN.bin, a vertex program (its NORMPACKED3
inputs' mask, 4 bytes, then its microcode, 16 bytes an instruction), and
ps_XXXXXXXX.key, a pixel shader key as the 32-bit game lays it out (264
bytes).

Built by build.sh against a source tree with the renderer's shader
intermediate form (xgpu_shader.h).
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_hdr.h"
#include "xgpu_shader.h"
#include "xgpu_spirv.h"
#include "xgpu_vulkan_post_shaders.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

void platform_log(const char *format, ...) { (void)format; }

#include "synthetic.c"

static const char *out_dir;
static FILE *index_file;
static unsigned long written, same;

/* the modules written so far, by their words' hash, to write each once */
struct written_module
{
	unsigned long long hash;
	char name[96];
};
static struct written_module *modules;
static unsigned long module_count, module_capacity;

static int names_compare(const void *a, const void *b) { return strcmp(*(char **)a, *(char **)b); }

static int list(const char *dir, const char *prefix, char ***names)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int count = 0, capacity = 0;

	*names = NULL;
	while (d && (e = readdir(d)))
	{
		if (strncmp(e->d_name, prefix, strlen(prefix)))
			continue;
		if (count == capacity)
			*names = realloc(*names, (capacity = capacity ? capacity * 2 : 256) * sizeof(char *));
		(*names)[count++] = strdup(e->d_name);
	}
	if (d)
		closedir(d);
	qsort(*names, count, sizeof(char *), names_compare);
	return count;
}

static void *read_file(const char *dir, const char *name, long *size)
{
	char path[1024];
	FILE *f;
	void *data;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (!(f = fopen(path, "rb")))
	{
		perror(path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc(*size);
	if (fread(data, 1, *size, f) != (size_t)*size)
	{
		perror(path);
		exit(1);
	}
	fclose(f);
	return data;
}

static unsigned long long words_hash(const struct xgpu_spirv_module *module)
{
	unsigned long long hash = 14695981039346656037ULL;
	const unsigned char *bytes = (const unsigned char *)module->words;
	unsigned long index;

	for (index = 0; index < module->count * 4; index++)
		hash = (hash ^ bytes[index]) * 1099511628211ULL;
	return hash;
}

/* writes a variant's module as <name>.spv, unless the same words have been */
static void emit(const char *name, const char *what, BOOL made, struct xgpu_spirv_module *module)
{
	unsigned long long hash;
	unsigned long index;
	char path[1024];
	FILE *file;

	if (!made)
	{
		fprintf(stderr, "%s: no module\n", name);
		exit(1);
	}
	hash = words_hash(module);
	for (index = 0; index < module_count; index++)
	{
		if (modules[index].hash == hash)
		{
			fprintf(index_file, "%s\t%s\t%s\n", name, modules[index].name, what);
			same++;
			free(module->words);
			return;
		}
	}
	if (module_count == module_capacity)
		modules = realloc(modules, (module_capacity = module_capacity ? module_capacity * 2 : 1024) * sizeof(*modules));
	modules[module_count].hash = hash;
	snprintf(modules[module_count].name, sizeof(modules[module_count].name), "%s", name);
	module_count++;
	snprintf(path, sizeof(path), "%s/%s.spv", out_dir, name);
	if (!(file = fopen(path, "wb")) || fwrite(module->words, 4, module->count, file) != module->count || fclose(file))
	{
		perror(path);
		exit(1);
	}
	fprintf(index_file, "%s\t%s\t%s\n", name, name, what);
	written++;
	free(module->words);
}

/* every view mode, with and without the per-eye insets */
static void vertex_program(const char *base, const char *what, const struct xgpu_vertex_program *program,
	unsigned long packed, unsigned long shorts, const struct nv2a_vertex_lighting *lighting)
{
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	char name[128], variant[192];
	int mode, foveated;
	BOOL made;

	memset(&options, 0, sizeof(options));
	for (mode = XGPU_MULTIVIEW_NONE; mode <= XGPU_MULTIVIEW_RIGHT_EYE; mode++)
	{
		for (foveated = 0; foveated <= 1; foveated++)
		{
			options.multiview = mode;
			options.foveated = foveated;
			if (!mode && !foveated)
				snprintf(name, sizeof(name), "%s", base);
			else
				snprintf(name, sizeof(name), "%s-view%d-fovea%d", base, mode, foveated);
			snprintf(variant, sizeof(variant), "%s view %d foveated %d", what, mode, foveated);
			made = xgpu_spirv_vertex_shader(program, packed, shorts, lighting, &options, &module);
			emit(name, variant, made, &module);
		}
	}
}

/* ---------- vertex programs: immediate mode (all floats) and streams
(their NORMPACKED3 inputs), unlit, the structures' lighting for the
enhanced dynamic lights, the model lighting for each pixel; and streams with
the other inputs unnormalized shorts, which no capture records (the vertex
layout does) */

static void vertex(const char *file, const DWORD *words, unsigned long count, unsigned long packed)
{
	struct xgpu_vertex_program program;
	struct nv2a_vertex_lighting lighting, environment;
	char base[64], name[96], what[160];
	BOOL lit;
	int immediate, light;

	snprintf(base, sizeof(base), "%.*s", (int)(strchr(file, '.') ? strchr(file, '.') - file : (long)strlen(file)), file);
	nv2a_vertex_program_decode(words, count, &program);
	memset(&lighting, 0, sizeof(lighting));
	lit = nv2a_vertex_shader_lighting(&program, &lighting);
	memset(&environment, 0, sizeof(environment));
	environment.lights = XGPU_ENVIRONMENT_LIGHTING;
	for (immediate = 0; immediate < 2; immediate++)
	{
		for (light = 0; light < 3; light++)
		{
			const struct nv2a_vertex_lighting *l = light == 0 ? NULL : light == 1 ? &environment : &lighting;

			if (light == 2 && !lit)
				continue;
			snprintf(name, sizeof(name), "%s-%s-light%d", base, immediate ? "immediate" : "streams", light);
			snprintf(what, sizeof(what), "%s packed %#lx lighting %d", file, immediate ? 0 : packed, l ? l->lights : 0);
			vertex_program(name, what, &program, immediate ? 0 : packed, 0, l);
		}
	}
	snprintf(name, sizeof(name), "%s-shorts", base);
	snprintf(what, sizeof(what), "%s packed %#lx shorts %#lx", file, packed, 0xffffUL & ~packed);
	vertex_program(name, what, &program, packed, 0xffffUL & ~packed, NULL);
}

/* ---------- pixel shader keys */

/* a key as the guest (ILP32) lays it out */
static void key_from_guest(const unsigned char *bytes, struct nv2a_pixel_shader_key *key)
{
	unsigned long offset = 0;
	unsigned int value;

	memset(key, 0, sizeof(*key));
	memcpy(key->combiner_state, bytes, sizeof(key->combiner_state));
	offset += sizeof(key->combiner_state);
	memcpy(&key->texture_modes, bytes + offset, 4);
	offset += 4;
	memcpy(key->sampler_type, bytes + offset, 4);
	offset += 4;
	memcpy(key->alpha_kill, bytes + offset, 4);
	offset += 4;
	memcpy(key->color_sign, bytes + offset, 4);
	offset += 4;
	memcpy(&value, bytes + offset, 4);
	key->alpha_test_function = value;
	offset += 4;
	key->fog_enable = bytes[offset++];
	key->fog_table_mode = bytes[offset++];
	key->count_samples = bytes[offset++];
	key->coverage_alpha = bytes[offset++];
	key->point_threshold = bytes[offset++];
	key->per_pixel_lighting = bytes[offset++];
	key->alpha_test_samples = bytes[offset++];
	key->multiview = bytes[offset++];
	key->source_blend = bytes[offset++];
	key->dynamic_lights = bytes[offset++];
	key->vr_effect = bytes[offset++];
}

/* a pixel program as the Vulkan backend draws it: its visibility tests
counted by occlusion queries, and by the shader; the border drawn by the
sampler, and by the shader on each stage that samples a 2D or 3D texture */
static void pixel_variant(const char *base, const char *what, const struct xgpu_pixel_program *program, BOOL foveated)
{
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	char name[128], variant[192];
	int stage, border = 0;
	BOOL made;

	memset(&options, 0, sizeof(options));
	options.foveated = foveated;
	made = xgpu_spirv_pixel_shader(program, &options, &module);
	emit(base, what, made, &module);
	if (program->count_samples)
	{
		options.count_samples = TRUE;
		snprintf(name, sizeof(name), "%s-count", base);
		snprintf(variant, sizeof(variant), "%s counted in the shader", what);
		made = xgpu_spirv_pixel_shader(program, &options, &module);
		emit(name, variant, made, &module);
		options.count_samples = FALSE;
	}
	for (stage = 0; stage < 4; stage++)
	{
		if (program->stages[stage].mode != XGPU_TEXTURE_NONE && (program->stages[stage].sampler == _xgpu_sampler_2d ||
			program->stages[stage].sampler == _xgpu_sampler_3d))
		{
			options.border[stage] = 7;
			border = 1;
		}
	}
	if (border)
	{
		snprintf(name, sizeof(name), "%s-border", base);
		snprintf(variant, sizeof(variant), "%s border drawn by the shader", what);
		made = xgpu_spirv_pixel_shader(program, &options, &module);
		emit(name, variant, made, &module);
	}
}

/* every key in all the views the Vulkan backend can draw */
static void pixel_program(const char *base, const char *what, const struct xgpu_pixel_program *program)
{
	struct xgpu_pixel_program variant = *program;
	char name[96], description[160];
	int mode, foveated;

	for (mode = XGPU_MULTIVIEW_NONE; mode <= XGPU_MULTIVIEW_RIGHT_EYE; mode++)
	{
		variant.multiview = (unsigned char)mode;
		for (foveated = 0; foveated <= 1; foveated++)
		{
			if (!mode && !foveated)
				snprintf(name, sizeof(name), "%s", base);
			else
				snprintf(name, sizeof(name), "%s-view%d-fovea%d", base, mode, foveated);
			snprintf(description, sizeof(description), "%s view %d foveated %d", what, mode, foveated);
			pixel_variant(name, description, &variant, foveated);
		}
	}
}

/* the effects which read an eye target: every texture stage, projected,
bump-offset and dependent coordinates, with visibility tests as well */
static void layered_pixel_programs(void)
{
	static const unsigned char modes[] = { XGPU_TEXTURE_PROJECT2D, XGPU_TEXTURE_BUMPENVMAP, XGPU_TEXTURE_DEPENDENT_AR };
	struct xgpu_pixel_program program;
	char name[64];
	int stage, mode;

	for (stage = 0; stage < 4; stage++)
	{
		for (mode = 0; mode < (int)sizeof(modes); mode++)
		{
			synthetic_base(&program);
			synthetic_stage(&program, stage, modes[mode], _xgpu_sampler_2d_layered);
			program.count_samples = 1;
			snprintf(name, sizeof(name), "synthetic-layered-stage%d-mode%d", stage, modes[mode]);
			pixel_program(name, name, &program);
		}
	}
}

/* a captured key, checked in every view mode */
static void pixel(const char *file, const unsigned char *bytes)
{
	struct nv2a_pixel_shader_key key;
	struct xgpu_pixel_program program;
	char base[64];

	snprintf(base, sizeof(base), "%.*s", (int)(strchr(file, '.') - file), file);
	key_from_guest(bytes, &key);
	nv2a_pixel_program_decode(&key, &program);
	pixel_program(base, file, &program);
}

static void synthetic_pixel(const char *name, const struct xgpu_pixel_program *program)
{
	pixel_program(name, name, program);
}

static void synthetic_vertex(const char *name, const struct xgpu_vertex_program *program)
{
	char variant[96];

	vertex_program(name, name, program, 0, 0, NULL);
	snprintf(variant, sizeof(variant), "%s-streams", name);
	vertex_program(variant, "packed 0x5, shorts 0x60", program, 0x5, 0x60, NULL);
}

/* the enhanced dynamic lights' glows, as the Vulkan backend draws them into
1, 2 and 4 layers: the screen's target, the VR mode's eyes, its eyes
foveated */
static void halo_programs(void)
{
	static const char *const names[3] = { "halo", "halo-view1-fovea0", "halo-view1-fovea1" };
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	char name[64];
	int layers;
	BOOL made;

	for (layers = 0; layers < 3; layers++)
	{
		memset(&options, 0, sizeof(options));
		options.multiview = layers ? XGPU_MULTIVIEW_EYES : XGPU_MULTIVIEW_NONE;
		options.foveated = layers == 2;
		snprintf(name, sizeof(name), "%s-vertex", names[layers]);
		made = xgpu_spirv_halo_vertex_shader(&options, &module);
		emit(name, "the dynamic lights' glows, vertex", made, &module);
		snprintf(name, sizeof(name), "%s-pixel", names[layers]);
		made = xgpu_spirv_halo_pixel_shader(&options, &module);
		emit(name, "the dynamic lights' glows, pixel", made, &module);
	}
}

/* the VR mode's effects (xgpu_spirv.h): an effect's program (its bitmap in
texture 0, a second map in texture 1) soft, and soft and thick, with each
fade, for both eyes and foveated; and the glows drawn as the light their
bolts scatter */
static void effect_programs(void)
{
	static const char *const effects[3] = { "", "soft", "thick" };
	struct xgpu_pixel_program program;
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	char name[96];
	int effect, fade, foveated, second;
	BOOL made;

	for (second = 0; second < 2; second++)
	{
		synthetic_base(&program);
		synthetic_stage(&program, 0, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
		if (second)
			synthetic_stage(&program, 1, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
		program.multiview = XGPU_MULTIVIEW_EYES;
		for (effect = XGPU_SPIRV_EFFECT_SOFT; effect <= XGPU_SPIRV_EFFECT_THICK; effect++)
		{
			for (fade = XGPU_SPIRV_FADE_NONE; fade <= XGPU_SPIRV_FADE_TO_GREY; fade++)
			{
				for (foveated = 0; foveated < 2; foveated++)
				{
					memset(&options, 0, sizeof(options));
					options.multiview = XGPU_MULTIVIEW_EYES;
					options.foveated = foveated;
					options.effect = effect;
					options.effect_fade = fade;
					snprintf(name, sizeof(name), "effect-%s-maps%d-fade%d-fovea%d", effects[effect], second + 1, fade,
						foveated);
					made = xgpu_spirv_pixel_shader(&program, &options, &module);
					emit(name, "the VR mode's effects", made, &module);
				}
			}
		}
	}
	/* a z-sprite (xgpu_shader.h, xgpu_pixel_program_depth_stage: the game's
	effect program with its second map's puff, stages 2 and 3 its dot
	products of it): its depth written, flat and in both eyes, or as an
	effect, soft (and thick, which a z-sprite draws as soft) with each fade */
	synthetic_base(&program);
	synthetic_stage(&program, 0, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
	synthetic_stage(&program, 1, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
	synthetic_stage(&program, 2, XGPU_TEXTURE_DOT_PRODUCT, _xgpu_sampler_none);
	synthetic_stage(&program, 3, XGPU_TEXTURE_DOT_ZW, _xgpu_sampler_none);
	program.stages[2].input = program.stages[3].input = 1;
	for (second = 0; second < 2; second++)
	{
		program.multiview = second ? XGPU_MULTIVIEW_EYES : XGPU_MULTIVIEW_NONE;
		memset(&options, 0, sizeof(options));
		options.multiview = program.multiview;
		snprintf(name, sizeof(name), "zsprite-depth-view%d", second);
		made = xgpu_spirv_pixel_shader(&program, &options, &module);
		emit(name, "a z-sprite's depth", made, &module);
	}
	for (effect = XGPU_SPIRV_EFFECT_SOFT; effect <= XGPU_SPIRV_EFFECT_THICK; effect++)
	{
		for (fade = XGPU_SPIRV_FADE_NONE; fade <= XGPU_SPIRV_FADE_TO_GREY; fade++)
		{
			for (foveated = 0; foveated < 2; foveated++)
			{
				memset(&options, 0, sizeof(options));
				options.multiview = XGPU_MULTIVIEW_EYES;
				options.foveated = foveated;
				options.effect = effect;
				options.effect_fade = fade;
				snprintf(name, sizeof(name), "zsprite-effect-%s-fade%d-fovea%d", effects[effect], fade, foveated);
				made = xgpu_spirv_pixel_shader(&program, &options, &module);
				emit(name, "a z-sprite as the VR mode's effects", made, &module);
			}
		}
	}
	for (foveated = 0; foveated < 2; foveated++)
	{
		memset(&options, 0, sizeof(options));
		options.multiview = XGPU_MULTIVIEW_EYES;
		options.foveated = foveated;
		options.effect = XGPU_SPIRV_EFFECT_SOFT;
		snprintf(name, sizeof(name), "glow-view1-fovea%d-vertex", foveated);
		made = xgpu_spirv_halo_vertex_shader(&options, &module);
		emit(name, "the glows as their bolts' scattered light, vertex", made, &module);
		snprintf(name, sizeof(name), "glow-view1-fovea%d-pixel", foveated);
		made = xgpu_spirv_halo_pixel_shader(&options, &module);
		emit(name, "the glows as their bolts' scattered light, pixel", made, &module);
	}
}

/* anti-aliasing's passes: their vertex shader, FXAA and SMAA's three (as
tools/vulkan_post_shaders.py checked them, made again here with the rest) */
/* display.hdr's tags (xgpu_hdr.h), as the front end gives them
(xgpu_hdr_classify): each blend the game's draws have, each ceiling with and
without a glow's expansion, the alpha limit, and the enhanced dynamic lights
past white; each blending in the shader with the pixel it draws over, of a
target of one sample and of four */
static void hdr_programs(void)
{
	static const unsigned char blends[][3] =
	{
		{ XGPU_BLEND_ONE, XGPU_BLEND_ONE, XGPU_BLEND_ADD },
		{ XGPU_BLEND_SOURCE_ALPHA, XGPU_BLEND_ONE, XGPU_BLEND_ADD },
		{ XGPU_BLEND_DESTINATION_COLOR, XGPU_BLEND_SOURCE_COLOR, XGPU_BLEND_ADD },
		{ XGPU_BLEND_SOURCE_ALPHA, XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA, XGPU_BLEND_ADD },
		{ XGPU_BLEND_ONE_MINUS_DESTINATION_COLOR, XGPU_BLEND_ONE, XGPU_BLEND_ADD },
		{ XGPU_BLEND_CONSTANT_COLOR, XGPU_BLEND_ONE_MINUS_CONSTANT_ALPHA, XGPU_BLEND_ADD },
		{ XGPU_BLEND_SOURCE_ALPHA_SATURATE, XGPU_BLEND_DESTINATION_ALPHA, XGPU_BLEND_ADD },
		{ XGPU_BLEND_ONE, XGPU_BLEND_ONE, XGPU_BLEND_REVERSE_SUBTRACT },
		{ XGPU_BLEND_ONE, XGPU_BLEND_ONE, XGPU_BLEND_SUBTRACT },
		{ XGPU_BLEND_ONE, XGPU_BLEND_ONE, XGPU_BLEND_MIN },
		{ XGPU_BLEND_ONE, XGPU_BLEND_ONE, XGPU_BLEND_MAX },
	};
	static const unsigned short tags[] =
	{
		XGPU_HDR_CEILING(XGPU_HDR_CEILING_PAPER_WHITE),
		XGPU_HDR_CEILING(XGPU_HDR_CEILING_PAPER_WHITE) | XGPU_HDR_ALPHA_LIMIT,
		XGPU_HDR_ALPHA_LIMIT,
		XGPU_HDR_CEILING(XGPU_HDR_CEILING_WEAPON),
		XGPU_HDR_CEILING(XGPU_HDR_CEILING_HUD) | XGPU_HDR_ALPHA_LIMIT,
		XGPU_HDR_CEILING(XGPU_HDR_CEILING_LIGHT),
	};
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	struct xgpu_pixel_program program;
	char name[64], what[96];
	unsigned int blend, tag;
	int samples;

	for (samples = 1; samples <= 4; samples += 3)
	{
		memset(&options, 0, sizeof(options));
		options.input_samples = samples;
		for (blend = 0; blend < sizeof(blends) / sizeof(blends[0]); blend++)
		{
			for (tag = 0; tag < sizeof(tags) / sizeof(tags[0]); tag++)
			{
				synthetic_base(&program);
				synthetic_stage(&program, 0, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
				program.hdr = (unsigned short)(tags[tag] | XGPU_HDR_BLEND(blends[blend][0], blends[blend][1],
					blends[blend][2]));
				snprintf(name, sizeof(name), "hdr-blend%u-tag%u-samples%d", blend, tag, samples);
				snprintf(what, sizeof(what), "display.hdr's tag %04x, %d samples", program.hdr, samples);
				emit(name, what, xgpu_spirv_pixel_shader(&program, &options, &module), &module);
			}
		}
	}
	/* (the structure's lightmap pass: the dynamic lights past white, and
	that with a ceiling, which the front end never gives it, still valid) */
	for (tag = 0; tag < 2; tag++)
	{
		synthetic_base(&program);
		synthetic_stage(&program, 0, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
		program.final.inputs[1].reg = XGPU_REGISTER_V0;
		program.dynamic_lights = XGPU_DYNAMIC_LIGHTS_ENVIRONMENT;
		program.hdr = (unsigned short)(XGPU_HDR_LIGHTS | (tag ? XGPU_HDR_CEILING(XGPU_HDR_CEILING_PAPER_WHITE) |
			XGPU_HDR_BLEND(XGPU_BLEND_ONE, XGPU_BLEND_ONE, XGPU_BLEND_ADD) : 0));
		memset(&options, 0, sizeof(options));
		options.input_samples = 1;
		snprintf(name, sizeof(name), "hdr-lights%u", tag);
		emit(name, "display.hdr's dynamic lights past white", xgpu_spirv_pixel_shader(&program, &options, &module),
			&module);
	}
	/* (a self-illuminated surface's pass: its combiners twice, stage 0's
	constants 0 the second time; a structure's lightmap pass with the
	dynamic lights past white too) */
	for (tag = 0; tag < 2; tag++)
	{
		synthetic_base(&program);
		synthetic_stage(&program, 0, XGPU_TEXTURE_PROJECT2D, _xgpu_sampler_2d);
		program.combiners[0].portions[0].inputs[2].reg = XGPU_REGISTER_C0;
		program.combiners[0].portions[0].inputs[3].reg = XGPU_REGISTER_C1;
		program.combiners[0].portions[0].sum = XGPU_REGISTER_V0;
		program.final.inputs[1].reg = XGPU_REGISTER_V0;
		program.hdr = XGPU_HDR_SELF_ILLUMINATION(1);
		if (tag)
		{
			program.dynamic_lights = XGPU_DYNAMIC_LIGHTS_ENVIRONMENT;
			program.hdr |= XGPU_HDR_LIGHTS;
		}
		memset(&options, 0, sizeof(options));
		options.input_samples = 1;
		snprintf(name, sizeof(name), "hdr-self-illumination%u", tag);
		emit(name, "display.hdr's self-illumination past white", xgpu_spirv_pixel_shader(&program, &options, &module),
			&module);
	}
}

static void post_programs(void)
{
	static const char *const names[XGPU_VULKAN_POST_PROGRAM_COUNT] =
	{
		"post-fxaa", "post-smaa-edges", "post-smaa-weights", "post-smaa-blend", "post-rotate", "post-hdr-tone-map",
		"post-hdr-tone-map-ms", "post-hdr-encode",
	};
	struct xgpu_spirv_module module;
	int program;

	for (program = -1; program < XGPU_VULKAN_POST_PROGRAM_COUNT; program++)
	{
		const struct xgpu_vulkan_post_module *from = program < 0 ? &xgpu_vulkan_post_vertex :
			&xgpu_vulkan_post_programs[program];

		module.count = (unsigned long)from->count;
		module.words = malloc(from->count * 4);
		if (module.words)
			memcpy(module.words, from->words, from->count * 4);
		emit(program < 0 ? "post-vertex" : names[program], "anti-aliasing's passes", module.words != NULL,
			&module);
	}
}

int main(int argc, char **argv)
{
	const char *dir;
	char **names, path[1024];
	int count, index;

	if (argc != 3)
	{
		fprintf(stderr, "usage: spirv_corpus <capture dir> <out dir>\n");
		return 2;
	}
	dir = argv[1];
	out_dir = argv[2];
	if (mkdir(out_dir, 0777) && errno != EEXIST)
	{
		perror(out_dir);
		return 1;
	}
	snprintf(path, sizeof(path), "%s/index.txt", out_dir);
	if (!(index_file = fopen(path, "w")))
	{
		perror(path);
		return 1;
	}
	fprintf(index_file, "# variant\tfile (.spv)\twhat\n");
	count = list(dir, "vs", &names);
	for (index = 0; index < count; index++)
	{
		long size;
		unsigned char *data = read_file(dir, names[index], &size);
		unsigned int packed;

		memcpy(&packed, data, 4);
		vertex(names[index], (const DWORD *)(data + 4), (unsigned long)(size - 4) / 16, packed);
		free(data);
	}
	count = list(dir, "ps_", &names);
	for (index = 0; index < count; index++)
	{
		long size;
		unsigned char *data = read_file(dir, names[index], &size);

		if (size != 264)
		{
			fprintf(stderr, "%s: %ld bytes\n", names[index], size);
			return 1;
		}
		pixel(names[index], data);
		free(data);
	}
	synthetic_pixel_programs(synthetic_pixel);
	layered_pixel_programs();
	synthetic_vertex_programs(synthetic_vertex);
	halo_programs();
	effect_programs();
	hdr_programs();
	post_programs();
	fclose(index_file);
	printf("%lu modules written to %s, %lu variants the same as one of them\n", written, out_dir, same);
	return 0;
}
