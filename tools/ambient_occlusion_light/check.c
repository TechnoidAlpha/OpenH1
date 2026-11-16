/*
CHECK.C

display.ambient_occlusion's light (port/linux/src/xgpu.h's
XGPU_AMBIENT_OCCLUSION_LIGHT_*; tools/test_ambient_occlusion_light.py): the
shaders of a surface lit by the occlusion, as the emitters write them, for
the test to read. Each case's pixel shader (a model lit for each pixel, by
its point lights or the enhanced dynamic lights; a model lit for each
vertex; a structure's lightmap pass, alone or with the enhanced dynamic
lights), with the occlusion and without it, and the vertex shader of a
model lit for each vertex with its ambient light apart:

  check msl [<source>]                the Metal emitter's, each after a line
                                      "==== <case> <occlusion: 0 or 1>"
  check spirv <directory> [<source>]  the SPIR-V emitter's, <case>-<occlusion>.spv
  check spirv_subgroups <directory> [<source>]
                                      the same, with the subgroups' bitwise
                                      operations (xgpu_spirv_options
                                      subgroups)

  check trace [<module>]              the distance-field scene's ray step
                                      alone: the Metal emitter's text, or the
                                      SPIR-V emitter's module written to
                                      <module>

<source>: the occlusion's, "world" (display.ambient_occlusion "world":
XGPU_AMBIENT_OCCLUSION_WORLD, the default), "view" (the world's, shown:
debug.world_ao_view), or "bare" (the light's bits alone, without
XGPU_AMBIENT_OCCLUSION_WORLD: no occlusion at all).

Built by the test with the shader decoders and both emitters, as
tools/msl_corpus is.
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_shader.h"
#include "xgpu_msl.h"
#include "xgpu_spirv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...) { (void)format; }

/* the occlusion's source: the key's bits beside its light (xgpu.h) */
static unsigned char source = XGPU_AMBIENT_OCCLUSION_WORLD;

/* the pixel shader's cases: name, per_pixel_lighting, dynamic_lights, the
light the occlusion multiplies */
static const struct
{
	const char *name;
	unsigned char per_pixel_lighting, dynamic_lights, light;
} cases[] =
{
	{ "model", 2, 0, XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL },
	{ "model_dynamic", 1, XGPU_DYNAMIC_LIGHTS_MODEL, XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL },
	{ "vertex", 0, 0, XGPU_AMBIENT_OCCLUSION_LIGHT_VERTEX },
	{ "lightmap", 0, 0, XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP },
	{ "lightmap_dynamic", 0, XGPU_DYNAMIC_LIGHTS_ENVIRONMENT, XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP },
};

/* a texture times the diffuse colour (the models' and the lightmap pass's
shape: its light, v0, by its texture) */
static void program_make(struct xgpu_pixel_program *program, int index, int occlusion)
{
	memset(program, 0, sizeof(*program));
	program->stages[0].declared_mode = program->stages[0].mode = XGPU_TEXTURE_PROJECT2D;
	program->stages[0].sampler = _xgpu_sampler_2d;
	program->final.present = 1;
	program->final.inputs[0].reg = XGPU_REGISTER_V0;
	program->final.inputs[1].reg = XGPU_REGISTER_T0;
	program->final.inputs[6].reg = XGPU_REGISTER_T0;
	program->final.inputs[6].alpha = 1;
	/* (the lightmap pass's lightmap, in stage 2: the static scenery's pages
	are read at its coordinates) */
	if (cases[index].light == XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
	{
		program->stages[2].declared_mode = program->stages[2].mode = XGPU_TEXTURE_PROJECT2D;
		program->stages[2].sampler = _xgpu_sampler_2d;
	}
	program->per_pixel_lighting = cases[index].per_pixel_lighting;
	program->dynamic_lights = cases[index].dynamic_lights;
	program->ambient_occlusion = occlusion ? (unsigned char)(cases[index].light | source) : 0;
}

/* a model lighting program's stand-in: the normal in r0 and the position
in r1 before its one instruction (oPos = v0) */
static void vertex_make(struct xgpu_vertex_program *program, struct nv2a_vertex_lighting *lighting)
{
	memset(program, 0, sizeof(*program));
	program->instruction_count = 1;
	program->instructions[0].mac = XGPU_MAC_MOV;
	program->instructions[0].a.file = XGPU_VERTEX_INPUT;
	program->instructions[0].a.index = 0;
	program->instructions[0].a.swizzle[0] = 0;
	program->instructions[0].a.swizzle[1] = 1;
	program->instructions[0].a.swizzle[2] = 2;
	program->instructions[0].a.swizzle[3] = 3;
	program->instructions[0].writes_output = 1;
	program->instructions[0].output_is_register = 1;
	program->instructions[0].output = XGPU_OUTPUT_POSITION;
	program->instructions[0].output_mask = 15;
	program->instructions[0].last = 1;
	memset(lighting, 0, sizeof(*lighting));
	lighting->lights = 2;
	lighting->normal_instruction = 0;
	lighting->normal_register = 0;
	lighting->position_instruction = 0;
	lighting->position_register = 1;
	lighting->split = TRUE;
}

static int msl(void)
{
	struct xgpu_pixel_program program;
	struct xgpu_vertex_program vertex;
	struct nv2a_vertex_lighting lighting;
	struct xgpu_msl_options options;
	unsigned int index;
	int occlusion;
	char *source;

	memset(&options, 0, sizeof(options));
	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
	{
		for (occlusion = 0; occlusion < 2; occlusion++)
		{
			program_make(&program, (int)index, occlusion);
			source = xgpu_msl_pixel_shader(&program, &options);
			if (!source)
				return 1;
			printf("==== %s %d\n%s\n", cases[index].name, occlusion, source);
			free(source);
		}
	}
	vertex_make(&vertex, &lighting);
	source = xgpu_msl_vertex_shader(&vertex, 0, 0, &lighting, &options);
	if (!source)
		return 1;
	printf("==== vertex_split 1\n%s\n", source);
	free(source);
	return 0;
}

static int spirv_write(const char *directory, const char *name, int occlusion, const struct xgpu_spirv_module *module)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s-%d.spv", directory, name, occlusion);
	if (!(file = fopen(path, "wb")))
		return 1;
	fwrite(module->words, 4, module->count, file);
	fclose(file);
	return 0;
}

static int spirv(const char *directory, int subgroups)
{
	struct xgpu_pixel_program program;
	struct xgpu_vertex_program vertex;
	struct nv2a_vertex_lighting lighting;
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	unsigned int index;
	int occlusion;

	memset(&options, 0, sizeof(options));
	options.subgroups = subgroups;
	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
	{
		for (occlusion = 0; occlusion < 2; occlusion++)
		{
			program_make(&program, (int)index, occlusion);
			if (!xgpu_spirv_pixel_shader(&program, &options, &module) ||
				spirv_write(directory, cases[index].name, occlusion, &module))
				return 1;
			free(module.words);
		}
	}
	vertex_make(&vertex, &lighting);
	if (!xgpu_spirv_vertex_shader(&vertex, 0, 0, &lighting, &options, &module) ||
		spirv_write(directory, "vertex_split", 1, &module))
		return 1;
	free(module.words);
	return 0;
}

static int source_set(const char *name)
{
	if (!strcmp(name, "bare"))
		source = 0;
	else if (!strcmp(name, "world"))
		source = XGPU_AMBIENT_OCCLUSION_WORLD;
	else if (!strcmp(name, "view"))
		source = XGPU_AMBIENT_OCCLUSION_WORLD | XGPU_AMBIENT_OCCLUSION_WORLD_VIEW;
	else
		return 0;
	return 1;
}

/* the distance-field scene's ray step alone (tools/test_world_trace.py):
the Metal emitter's text, or the SPIR-V emitter's module written to path */
static int trace(const char *path)
{
	struct xgpu_spirv_module module;
	char *text;
	FILE *file;

	if (!path)
	{
		text = xgpu_msl_world_trace_test();
		if (!text)
			return 1;
		fputs(text, stdout);
		free(text);
		return 0;
	}
	if (!xgpu_spirv_world_trace_test(&module) || !(file = fopen(path, "wb")))
		return 1;
	fwrite(module.words, sizeof(*module.words), module.count, file);
	fclose(file);
	free(module.words);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "trace"))
		return trace(argc > 2 ? argv[2] : NULL);
	/* (the occlusion's pass, as the Metal emitter writes it; or the SPIR-V
	emitter's, written to a file, with the subgroups' operations) */
	/* (or with "ray_traced"'s ray queries: pass_rays) */
	if (argc == 3 && (!strcmp(argv[1], "pass") || !strcmp(argv[1], "pass_rays")))
	{
		struct xgpu_spirv_options options;
		struct xgpu_spirv_module module;
		FILE *file;

		memset(&options, 0, sizeof(options));
		options.subgroups = TRUE;
		options.ray_queries = !strcmp(argv[1], "pass_rays");
		if (!xgpu_spirv_world_ao_pass(XGPU_SPIRV_WORLD_AO_OCCLUSION, &options, &module) || !(file = fopen(argv[2], "wb")))
			return 1;
		fwrite(module.words, sizeof(*module.words), module.count, file);
		fclose(file);
		free(module.words);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "pass"))
	{
		char *text = xgpu_msl_world_ao_pass(TRUE);

		if (!text)
			return 1;
		fputs(text, stdout);
		free(text);
		return 0;
	}
	if ((argc == 2 || (argc == 3 && source_set(argv[2]))) && !strcmp(argv[1], "msl"))
		return msl();
	if ((argc == 3 || (argc == 4 && source_set(argv[3]))) &&
		(!strcmp(argv[1], "spirv") || !strcmp(argv[1], "spirv_subgroups")))
		return spirv(argv[2], !strcmp(argv[1], "spirv_subgroups"));
	fprintf(stderr, "check msl [world|view|bare] | check spirv|spirv_subgroups <directory> [world|view|bare] | "
		"check trace [<module>] | check pass|pass_rays <module>\n");
	return 2;
}
