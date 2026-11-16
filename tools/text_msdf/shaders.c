/*
SHADERS.C

The high-res text's distance fields (XGPU_BORDER_MSDF) as both backends'
emitters sample them (tools/test_text_msdf.py): a pixel program whose
colour and alpha are stage 0's texel of a 2D texture, its border axes the
distance fields', written as the Vulkan backend's SPIR-V (xgpu_spirv.c)
and the Metal backend's MSL (xgpu_msl.c). The test makes the SPIR-V
Metal's with SPIRV-Cross and draws both on the Mac's GPU, over a field at
several sizes (harness.m): they must agree.

  shaders <out dir>

writes msdf.spv, msdf.metal and index.txt: the uniforms' texture_scale
offsets (the SPIR-V's block, the MSL's).

Built as tools/hdr_backends/shaders.c is.
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_hdr.h"
#include "xgpu_shader.h"
#include "xgpu_spirv.h"
#include "xgpu_msl.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

void platform_log(const char *format, ...) { (void)format; }

static int write_file(const char *dir, const char *name, const void *data, size_t size)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (!(file = fopen(path, "wb")))
	{
		perror(path);
		return 0;
	}
	fwrite(data, 1, size, file);
	fclose(file);
	return 1;
}

int main(int argc, char **argv)
{
	struct xgpu_pixel_program program;
	struct xgpu_spirv_options spirv;
	struct xgpu_msl_options msl;
	struct xgpu_spirv_module module;
	char path[1024];
	FILE *index;
	char *source;

	if (argc != 2 || (mkdir(argv[1], 0777) && errno != EEXIST))
		return 2;
	snprintf(path, sizeof(path), "%s/index.txt", argv[1]);
	if (!(index = fopen(path, "w")))
		return 1;
	fprintf(index, "texture_scale %u %u\n", (unsigned)offsetof(struct xgpu_spirv_uniforms, texture_scale),
		(unsigned)offsetof(struct xgpu_msl_uniforms, texture_scale));
	fclose(index);
	/* (stage 0 a 2D texture; the final combiner's D t0, its G t0's alpha) */
	memset(&program, 0, sizeof(program));
	program.stages[0].declared_mode = program.stages[0].mode = XGPU_TEXTURE_PROJECT2D;
	program.stages[0].sampler = _xgpu_sampler_2d;
	program.stages[0].previous = -1;
	program.final.present = 1;
	program.final.inputs[3].reg = XGPU_REGISTER_T0;
	program.final.inputs[6].reg = XGPU_REGISTER_T0;
	program.final.inputs[6].alpha = 1;
	memset(&spirv, 0, sizeof(spirv));
	spirv.input_samples = 1;
	spirv.border[0] = XGPU_BORDER_MSDF;
	memset(&msl, 0, sizeof(msl));
	msl.border[0] = XGPU_BORDER_MSDF;
	if (!xgpu_spirv_pixel_shader(&program, &spirv, &module) ||
		!write_file(argv[1], "msdf.spv", module.words, module.count * 4))
		return 1;
	source = xgpu_msl_pixel_shader(&program, &msl);
	if (!source || !strstr(source, "msdf_coverage(") || !write_file(argv[1], "msdf.metal", source, strlen(source)))
		return 1;
	return 0;
}
