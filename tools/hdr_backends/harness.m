/*
HARNESS.M

display.hdr's two backends on the Mac's GPU (tools/test_hdr_backends.py):
the Vulkan backend's shaders, made Metal's by SPIRV-Cross, run beside the
Metal backend's own and the reference (port/linux/src/xgpu_hdr.h).

  harness post <dir>
    The Vulkan backend's tone map and encoding (port/linux/vulkan/
    xgpu_vulkan_post_shaders.c's modules: <dir>/post_vert.metal,
    hdr_tone_map.metal, hdr_encode.metal), over the sixteen golden colours
    Metal's test holds its own to (port/macos/tests/xgpu_metal_test.m,
    test_stream_hdr): the tone map in place, reading each pixel where it is
    drawn, at peaks 1, 4 and 16 in both modes; the encoding for scRGB, for
    HDR10's PQ and for SDR; each against the reference to half floats'
    precision.
  harness draws <dir>
    Each draw of tools/hdr_backends/shaders.c's index, both backends'
    fragment functions (<name>.vk.metal from the SPIR-V, <name>.metal the
    MSL emitter's), each with the vertex function the test wrote for its
    inputs (<name>.vk.vertex.metal, <name>.vertex.metal), over the same
    pixels drawn over and the same colours drawn: the two must agree, and
    no channel may pass what the tag allows (its ceiling, alpha's 1); a
    self-illuminated surface's pass leaves the reference's values
    (xgpu_hdr_self_illuminate), past 1 only by its self-illumination, and
    the same pass untagged the SDR frame's.

Prints a line for each failure and exits nonzero if any.
*/

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XGPU_HDR_REFERENCE
#include "xgpu_hdr.h"

static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static int failures;

#define FAIL(...) do { failures++; if (failures < 40) { printf(__VA_ARGS__); printf("\n"); } } while (0)

static id<MTLFunction> function_load(NSString *path, NSString *name)
{
	NSError *error = nil;
	NSString *source = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
	MTLCompileOptions *options = [MTLCompileOptions new];
	id<MTLLibrary> library;

	if (!source)
	{
		FAIL("%s: cannot be read", path.UTF8String);
		return nil;
	}
	library = [device newLibraryWithSource:source options:options error:&error];
	if (!library)
	{
		FAIL("%s: %s", path.UTF8String, error.localizedDescription.UTF8String);
		return nil;
	}
	return [library newFunctionWithName:name];
}

static id<MTLTexture> texture_make(MTLPixelFormat format, NSUInteger width, MTLTextureUsage usage)
{
	MTLTextureDescriptor *description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width
		height:1 mipmapped:NO];

	description.usage = usage;
	description.storageMode = MTLStorageModeShared;
	return [device newTextureWithDescriptor:description];
}

/* rgba floats (count pixels) into a half-float texture */
static void halves_write(id<MTLTexture> texture, const float *rgba, NSUInteger count)
{
	__fp16 *halves = malloc(count * 8);
	NSUInteger index;

	for (index = 0; index < count * 4; index++)
		halves[index] = (__fp16)rgba[index];
	[texture replaceRegion:MTLRegionMake2D(0, 0, count, 1) mipmapLevel:0 withBytes:halves bytesPerRow:count * 8];
	free(halves);
}

static void halves_read(id<MTLTexture> texture, float *rgba, NSUInteger count)
{
	__fp16 *halves = malloc(count * 8);
	NSUInteger index;

	[texture getBytes:halves bytesPerRow:count * 8 fromRegion:MTLRegionMake2D(0, 0, count, 1) mipmapLevel:0];
	for (index = 0; index < count * 4; index++)
		rgba[index] = (float)halves[index];
	free(halves);
}

static float half_of(float value)
{
	return (float)(__fp16)value;
}

/* within half floats' precision of the reference (Metal's test's) */
static int close_to(float got, float expected)
{
	return fabsf(got - expected) <= 0.004f + 0.004f * fabsf(expected);
}

/* ---------- the tone map and the encoding */

#define VECTORS 16

static const float vectors[VECTORS][3] =
{
	{ 0.0f, 0.0f, 0.0f }, { 0.02f, 0.04f, 0.03f }, { 0.25f, 0.5f, 0.75f }, { 1.0f, 1.0f, 1.0f },
	{ 0.2f, 0.6f, 1.0f }, { 1.0f, 0.5f, 0.1f }, { 1.05f, 1.0f, 0.9f }, { 0.55f, 1.1f, 1.2f },
	{ 1.3f, 1.3f, 1.3f }, { 0.4f, 0.9f, 1.6f }, { 2.0f, 1.2f, 0.4f }, { 1.8f, 1.8f, 1.8f },
	{ 3.0f, 2.5f, 1.5f }, { 0.6f, 1.5f, 3.0f }, { 4.0f, 4.0f, 4.0f }, { 6.0f, 0.2f, 0.2f },
};

/* the post passes' uniforms (xgpu_vulkan_post_shaders.h) */
struct post_uniforms
{
	float metrics[4];
	float bounds[4];
	float hdr[4];
};

static void post_draw(id<MTLRenderPipelineState> pipeline, id<MTLTexture> target, id<MTLTexture> source,
	const struct post_uniforms *uniforms)
{
	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLCommandBuffer> commands = [queue commandBuffer];
	id<MTLRenderCommandEncoder> encoder;

	pass.colorAttachments[0].texture = target;
	pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
	pass.colorAttachments[0].storeAction = MTLStoreActionStore;
	encoder = [commands renderCommandEncoderWithDescriptor:pass];
	[encoder setRenderPipelineState:pipeline];
	[encoder setFragmentBytes:uniforms length:sizeof(*uniforms) atIndex:1];
	if (source)
	{
		MTLSamplerDescriptor *sampler = [MTLSamplerDescriptor new];

		sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterNearest;
		[encoder setFragmentTexture:source atIndex:0];
		[encoder setFragmentSamplerState:[device newSamplerStateWithDescriptor:sampler] atIndex:0];
	}
	[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[encoder endEncoding];
	[commands commit];
	[commands waitUntilCompleted];
}

static id<MTLRenderPipelineState> post_pipeline(id<MTLFunction> vertex, id<MTLFunction> fragment, MTLPixelFormat format,
	MTLColorWriteMask mask)
{
	MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
	NSError *error = nil;
	id<MTLRenderPipelineState> pipeline;

	description.vertexFunction = vertex;
	description.fragmentFunction = fragment;
	description.colorAttachments[0].pixelFormat = format;
	description.colorAttachments[0].writeMask = mask;
	pipeline = [device newRenderPipelineStateWithDescriptor:description error:&error];
	if (!pipeline)
		FAIL("a pipeline: %s", error.localizedDescription.UTF8String);
	return pipeline;
}

static void post_test(NSString *dir)
{
	static const float peaks[3] = { 1.0f, 4.0f, 16.0f };
	/* (the last two with the UI's white past and under paper white's) */
	static const struct xgpu_hdr_frame encodings[6] =
	{
		{ XGPU_HDR_MODE_FAITHFUL, XGPU_HDR_OUTPUT_SCRGB, 2.5f, 4.0f },
		{ XGPU_HDR_MODE_FAITHFUL, XGPU_HDR_OUTPUT_PQ, 0.02f, 4.0f },
		{ XGPU_HDR_MODE_FAITHFUL, XGPU_HDR_OUTPUT_PQ, 0.0203f, 10.0f },
		{ XGPU_HDR_MODE_FAITHFUL, XGPU_HDR_OUTPUT_SDR, 1.0f, 1.0f },
		{ XGPU_HDR_MODE_FAITHFUL, XGPU_HDR_OUTPUT_SCRGB, 2.5f, 4.0f, 1.8f },
		{ XGPU_HDR_MODE_FAITHFUL, XGPU_HDR_OUTPUT_PQ, 0.0203f, 10.0f, 0.6f },
	};
	/* the tone map's UI white: none given (paper white's), and the UI's
	white under and past paper white's */
	static const float ui_whites[3] = { 0.0f, 0.6f, 2.0f };
	id<MTLFunction> vertex = function_load([dir stringByAppendingPathComponent:@"post_vert.metal"], @"xgpu_vk_post_vertex");
	id<MTLFunction> tone_map = function_load([dir stringByAppendingPathComponent:@"hdr_tone_map.metal"], @"xgpu_vk_tone_map");
	id<MTLFunction> encode = function_load([dir stringByAppendingPathComponent:@"hdr_encode.metal"], @"xgpu_vk_encode");
	id<MTLRenderPipelineState> tone_pipeline, encode_pipeline;
	float rgba[VECTORS * 4], got[VECTORS * 4];
	int mode, peak, ui, vector, channel, which, checked = 0;

	if (!vertex || !tone_map || !encode)
		return;
	/* (the colour alone written, as the Vulkan pipeline's mask has it) */
	tone_pipeline = post_pipeline(vertex, tone_map, MTLPixelFormatRGBA16Float,
		MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue);
	encode_pipeline = post_pipeline(vertex, encode, MTLPixelFormatRGBA32Float, MTLColorWriteMaskAll);
	if (!tone_pipeline || !encode_pipeline)
		return;
	for (vector = 0; vector < VECTORS; vector++)
	{
		memcpy(rgba + vector * 4, vectors[vector], sizeof(vectors[vector]));
		rgba[vector * 4 + 3] = 0.25f;
	}
	for (mode = XGPU_HDR_MODE_FAITHFUL; mode <= XGPU_HDR_MODE_PUNCHY; mode++)
	{
		for (ui = 0; ui < 3; ui++)
		{
			for (peak = 0; peak < 3; peak++)
			{
				id<MTLTexture> target = texture_make(MTLPixelFormatRGBA16Float, VECTORS,
					MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
				struct post_uniforms uniforms;

				halves_write(target, rgba, VECTORS);
				memset(&uniforms, 0, sizeof(uniforms));
				uniforms.metrics[0] = 1.0f / VECTORS;
				uniforms.metrics[1] = 1.0f;
				uniforms.metrics[2] = VECTORS;
				uniforms.metrics[3] = 1.0f;
				uniforms.hdr[0] = (float)mode;
				uniforms.hdr[1] = peaks[peak];
				uniforms.hdr[2] = ui_whites[ui];
				post_draw(tone_pipeline, target, nil, &uniforms);
				halves_read(target, got, VECTORS);
				for (vector = 0; vector < VECTORS; vector++)
				{
					float expected[3];

					for (channel = 0; channel < 3; channel++)
						expected[channel] = half_of(vectors[vector][channel]);
					xgpu_hdr_tone_map(expected, mode, peaks[peak]);
					xgpu_hdr_to_ui(expected, ui_whites[ui]);
					for (channel = 0; channel < 3; channel++)
					{
						if (!close_to(got[vector * 4 + channel], expected[channel]))
							FAIL("tone map, %s at peak %.0f: vector %d channel %d: %.5f, the reference %.5f",
								mode ? "punchy" : "faithful", (double)peaks[peak], vector, channel,
								(double)got[vector * 4 + channel], (double)expected[channel]);
						checked++;
					}
					if (got[vector * 4 + 3] != 0.25f)
						FAIL("tone map: vector %d's alpha %.5f, not the game's", vector, (double)got[vector * 4 + 3]);
					/* (faithful: a colour under white exactly as it was) */
					if (mode == XGPU_HDR_MODE_FAITHFUL && vector == 2 && !ui)
					{
						for (channel = 0; channel < 3; channel++)
						{
							if (got[vector * 4 + channel] != half_of(vectors[vector][channel]))
								FAIL("tone map: faithful changed a colour under white: %.6f", (double)got[vector * 4 + channel]);
						}
					}
				}
			}
		}
	}
	/* (a pixel not a number, or infinite, either way: tone mapped as 0, and
	as the half floats' most and least, finite) */
	{
		id<MTLTexture> target = texture_make(MTLPixelFormatRGBA16Float, VECTORS,
			MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
		float special[VECTORS * 4], expected[3][3] = { { 0.0f, 0.0f, 0.0f }, { 65504.0f, 65504.0f, 65504.0f },
			{ -65504.0f, -65504.0f, -65504.0f } };
		struct post_uniforms uniforms;
		int finite = 0;

		memset(special, 0, sizeof(special));
		for (channel = 0; channel < 3; channel++)
		{
			special[channel] = NAN;
			special[4 + channel] = INFINITY;
			special[8 + channel] = -INFINITY;
		}
		halves_write(target, special, VECTORS);
		memset(&uniforms, 0, sizeof(uniforms));
		uniforms.metrics[0] = 1.0f / VECTORS;
		uniforms.metrics[1] = 1.0f;
		uniforms.metrics[2] = VECTORS;
		uniforms.metrics[3] = 1.0f;
		uniforms.hdr[0] = (float)XGPU_HDR_MODE_FAITHFUL;
		uniforms.hdr[1] = 4.0f;
		post_draw(tone_pipeline, target, nil, &uniforms);
		halves_read(target, got, VECTORS);
		for (vector = 0; vector < 3; vector++)
		{
			xgpu_hdr_tone_map(expected[vector], XGPU_HDR_MODE_FAITHFUL, 4.0f);
			for (channel = 0; channel < 3; channel++)
			{
				if (!isfinite(got[vector * 4 + channel]) || !close_to(got[vector * 4 + channel], expected[vector][channel]))
					FAIL("tone map: special value %d channel %d: %.5f, the reference %.5f", vector, channel,
						(double)got[vector * 4 + channel], (double)expected[vector][channel]);
				else
					finite++;
			}
		}
		printf("post: %d values not a number or infinite tone mapped finite\n", finite);
	}
	for (which = 0; which < 6; which++)
	{
		id<MTLTexture> source = texture_make(MTLPixelFormatRGBA16Float, VECTORS, MTLTextureUsageShaderRead);
		id<MTLTexture> target = texture_make(MTLPixelFormatRGBA32Float, VECTORS, MTLTextureUsageRenderTarget);
		struct post_uniforms uniforms;

		halves_write(source, rgba, VECTORS);
		memset(&uniforms, 0, sizeof(uniforms));
		uniforms.metrics[2] = 1.0f / VECTORS;
		uniforms.metrics[3] = 1.0f;
		uniforms.hdr[0] = encodings[which].paper_white;
		uniforms.hdr[1] = encodings[which].peak;
		uniforms.hdr[2] = (float)encodings[which].output;
		uniforms.hdr[3] = encodings[which].ui;
		post_draw(encode_pipeline, target, source, &uniforms);
		[target getBytes:got bytesPerRow:VECTORS * 16 fromRegion:MTLRegionMake2D(0, 0, VECTORS, 1) mipmapLevel:0];
		for (vector = 0; vector < VECTORS; vector++)
		{
			float expected[3];

			for (channel = 0; channel < 3; channel++)
				expected[channel] = half_of(vectors[vector][channel]);
			xgpu_hdr_encode(expected, &encodings[which]);
			for (channel = 0; channel < 3; channel++)
			{
				/* (PQ's code values to a thousandth: a tenth of a 10-bit step) */
				float error = fabsf(got[vector * 4 + channel] - expected[channel]);
				int good = encodings[which].output == XGPU_HDR_OUTPUT_PQ ? error <= 0.001f :
					error <= 1e-4f + 1e-4f * fabsf(expected[channel]);

				if (!good)
					FAIL("encoding %d (output %d): vector %d channel %d: %.6f, the reference %.6f", which,
						encodings[which].output, vector, channel, (double)got[vector * 4 + channel], (double)expected[channel]);
				checked++;
			}
		}
	}
	printf("post: %d values checked against the reference\n", checked);
}

/* ---------- the draws */

#define PIXELS 512

/* a draw's fragment function, with its vertex function, drawn as a point
over each pixel (its colour) over the pixels drawn over (had): what it
leaves */
/* (the self-illuminated pass's colour: stage 0's constant) */
static const float self_illumination_color[4] = { 0.9f, 0.7f, 0.4f, 0.0f };

/* (interlocked, the Vulkan backend's draws that read and write their
target as a storage image, texture 1, under a raster order group, the
pipeline writing no colour: drawn over each pixel twice, the second with the
second half of the colours; mask, the pipeline's colour mask) */
static int draw_run(NSString *dir, NSString *stem, NSString *name, NSUInteger uniforms_index, unsigned offset,
	unsigned constants_offset, const float *colors, const float *had, float *left, int interlocked, int twice,
	MTLColorWriteMask mask)
{
	id<MTLFunction> vertex = function_load([dir stringByAppendingPathComponent:[stem stringByAppendingString:@".vertex.metal"]],
		@"hdr_test_vertex");
	id<MTLFunction> fragment = function_load([dir stringByAppendingPathComponent:[stem stringByAppendingString:@".metal"]],
		name);
	MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLRenderPipelineState> pipeline;
	id<MTLCommandBuffer> commands;
	id<MTLRenderCommandEncoder> encoder;
	id<MTLTexture> target, attachment;
	NSError *error = nil;
	float uniforms[1024], width = PIXELS;

	if (!vertex || !fragment)
		return 0;
	description.vertexFunction = vertex;
	description.fragmentFunction = fragment;
	description.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
	description.colorAttachments[0].writeMask = interlocked ? MTLColorWriteMaskNone : mask;
	description.inputPrimitiveTopology = MTLPrimitiveTopologyClassPoint;
	pipeline = [device newRenderPipelineStateWithDescriptor:description error:&error];
	if (!pipeline)
	{
		FAIL("%s: %s", stem.UTF8String, error.localizedDescription.UTF8String);
		return 0;
	}
	target = texture_make(MTLPixelFormatRGBA16Float, PIXELS, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
		MTLTextureUsageShaderWrite);
	halves_write(target, had, PIXELS);
	attachment = interlocked ? texture_make(MTLPixelFormatRGBA16Float, PIXELS, MTLTextureUsageRenderTarget) : target;
	/* (the blend colour, which the constant factors take) */
	memset(uniforms, 0, sizeof(uniforms));
	uniforms[offset / 4] = 0.8f;
	uniforms[offset / 4 + 1] = 0.6f;
	uniforms[offset / 4 + 2] = 0.3f;
	uniforms[offset / 4 + 3] = 0.7f;
	memcpy(&uniforms[constants_offset / 4], self_illumination_color, sizeof(self_illumination_color));
	pass.colorAttachments[0].texture = attachment;
	pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
	pass.colorAttachments[0].storeAction = MTLStoreActionStore;
	commands = [queue commandBuffer];
	encoder = [commands renderCommandEncoderWithDescriptor:pass];
	[encoder setRenderPipelineState:pipeline];
	[encoder setVertexBytes:colors length:(twice ? 2 : 1) * PIXELS * 16 atIndex:0];
	[encoder setVertexBytes:&width length:sizeof(width) atIndex:1];
	[encoder setFragmentBytes:uniforms length:sizeof(uniforms) atIndex:uniforms_index];
	if (interlocked)
		[encoder setFragmentTexture:target atIndex:1];
	[encoder drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:(twice ? 2 : 1) * PIXELS];
	[encoder endEncoding];
	[commands commit];
	[commands waitUntilCompleted];
	halves_read(target, left, PIXELS);
	return 1;
}

static void draws_test(NSString *dir)
{
	NSString *index = [NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:@"index.txt"]
		encoding:NSUTF8StringEncoding error:nil];
	NSArray<NSString *> *lines = [index componentsSeparatedByString:@"\n"];
	static float colors[2 * PIXELS * 4], had[PIXELS * 4], vulkan[PIXELS * 4], metal[PIXELS * 4];
	unsigned spirv_offset = 0, msl_offset = 0, spirv_constants = 0, msl_constants = 0, seed = 12345;
	int pixel, channel, draws = 0, compared = 0, unclamped = 0, self_illuminated = 0, interlocked_draws = 0;
	NSUInteger line;

	if (!index || lines.count < 2 || sscanf(lines[0].UTF8String, "blend_constant %u %u ps_c0 %u %u", &spirv_offset,
		&msl_offset, &spirv_constants, &msl_constants) != 4)
	{
		FAIL("draws: no index");
		return;
	}
	/* the colours drawn (0 to 1, the pixel shaders' clamp; a second set,
	the interlocked draws' second time over each pixel) and the pixels drawn
	over (0 to 3: past white too; alpha 0 to 1), the same for every draw */
	for (pixel = 0; pixel < PIXELS; pixel++)
	{
		for (channel = 0; channel < 4; channel++)
		{
			seed = seed * 1103515245u + 12345u;
			colors[(PIXELS + pixel) * 4 + channel] = half_of((float)((seed >> 8) & 0xffff) / 65535.0f);
		}
	}
	for (pixel = 0; pixel < PIXELS; pixel++)
	{
		for (channel = 0; channel < 4; channel++)
		{
			seed = seed * 1103515245u + 12345u;
			colors[pixel * 4 + channel] = half_of((float)((seed >> 8) & 0xffff) / 65535.0f);
			seed = seed * 1103515245u + 12345u;
			had[pixel * 4 + channel] = half_of((float)((seed >> 8) & 0xffff) / 65535.0f * (channel == 3 ? 1.0f : 3.0f));
		}
	}
	for (line = 1; line < lines.count; line++)
	{
		char name[64];
		unsigned tag;
		int ceiling, interlocked;
		float ceiling_value;
		MTLColorWriteMask mask;

		if (sscanf(lines[line].UTF8String, "%63s %u", name, &tag) != 2)
			continue;
		ceiling = XGPU_HDR_CEILING_OF(tag);
		ceiling_value = xgpu_hdr_ceiling_value(ceiling);
		/* (interlocked: Vulkan's storage image against Metal's programmable
		blending, each pixel drawn twice in one draw; the rest of the
		checks are of one draw over the pixels drawn over) */
		interlocked = strstr(name, "-interlock") != NULL;
		mask = strstr(name, "-interlock-rgb") ? MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue :
			MTLColorWriteMaskAll;
		if (!draw_run(dir, [NSString stringWithFormat:@"%s.vk", name], @"xgpu_vk_fragment", 1, spirv_offset, spirv_constants,
			colors, had, vulkan, interlocked, interlocked, mask) || !draw_run(dir, [NSString stringWithUTF8String:name],
			@"xgpu_fragment", 17, msl_offset, msl_constants, colors, had, metal, 0, interlocked, mask))
			continue;
		draws++;
		if (interlocked)
		{
			interlocked_draws++;
			for (pixel = 0; pixel < PIXELS * 4; pixel++)
			{
				compared++;
				if (fabsf(vulkan[pixel] - metal[pixel]) > 0.002f + 0.002f * fabsf(metal[pixel]))
					FAIL("%s (tag %04x) pixel %d channel %d: Vulkan's %.5f, Metal's %.5f (drawn twice over %.4f)", name,
						tag, pixel / 4, pixel % 4, (double)vulkan[pixel], (double)metal[pixel], (double)had[pixel]);
				if (mask != MTLColorWriteMaskAll && pixel % 4 == 3 && vulkan[pixel] != had[pixel])
					FAIL("%s: pixel %d's alpha written through the mask: %.5f over %.5f", name, pixel / 4,
						(double)vulkan[pixel], (double)had[pixel]);
			}
			continue;
		}
		for (pixel = 0; pixel < PIXELS; pixel++)
		{
			for (channel = 0; channel < 4; channel++)
			{
				float v = vulkan[pixel * 4 + channel], m = metal[pixel * 4 + channel];

				compared++;
				if (fabsf(v - m) > 0.002f + 0.002f * fabsf(m))
					FAIL("%s (tag %04x) pixel %d channel %d: Vulkan's %.5f, Metal's %.5f (drawn %.4f over %.4f)", name,
						tag, pixel, channel, (double)v, (double)m, (double)colors[pixel * 4 + channel],
						(double)had[pixel * 4 + channel]);
				if (v < 0.0f)
					FAIL("%s: pixel %d channel %d below 0: %.5f", name, pixel, channel, (double)v);
				/* (a ceiling: no channel past what the pixel had or the
				ceiling; the alpha limit: alpha not past 1) */
				if (channel < 3 && ceiling && v > fmaxf(had[pixel * 4 + channel], ceiling_value) * 1.002f + 0.002f &&
					!XGPU_HDR_GLOW(tag))
					FAIL("%s: pixel %d channel %d past its ceiling: %.5f", name, pixel, channel, (double)v);
				if (channel == 3 && (tag & XGPU_HDR_ALPHA_LIMIT) && v > 1.0f)
					FAIL("%s: pixel %d's alpha past 1: %.5f", name, pixel, (double)v);
				/* (unclamped: the colour doubled, past 1 too, as drawn: no
				blend) */
				if (XGPU_HDR_IS_UNCLAMPED(tag))
				{
					float expected = channel < 3 ? 2.0f * colors[pixel * 4 + channel] : colors[pixel * 4 + 3];

					if (fabsf(v - expected) > 0.002f + 0.002f * expected)
						FAIL("%s: pixel %d channel %d clamped: %.5f, not %.5f", name, pixel, channel, (double)v,
							(double)expected);
					unclamped += channel < 3 && v > 1.0f;
				}
			}
			if (strncmp(name, "draw-self-illumination", 22) == 0)
			{
				/* (a self-illuminated pass: the diffuse colour, its light, and
				the constant times its alpha, its self-illumination; tagged,
				the reference's glow, past 1 by its self-illumination alone;
				untagged, the SDR frame's) */
				float base[3], lit[3];

				for (channel = 0; channel < 3; channel++)
				{
					base[channel] = colors[pixel * 4 + channel];
					lit[channel] = fminf(base[channel] + self_illumination_color[channel] * colors[pixel * 4 + 3], 1.0f);
				}
				if (tag)
					xgpu_hdr_self_illuminate(lit, base);
				for (channel = 0; channel < 3; channel++)
				{
					float v = vulkan[pixel * 4 + channel];

					if (fabsf(v - lit[channel]) > 0.003f + 0.002f * lit[channel])
						FAIL("%s: pixel %d channel %d: %.5f, the reference's %.5f", name, pixel, channel, (double)v,
							(double)lit[channel]);
					if (v > (tag ? XGPU_HDR_SELF_ILLUMINATION_VALUE * 1.002f : 1.0f))
						FAIL("%s: pixel %d channel %d past its ceiling: %.5f", name, pixel, channel, (double)v);
					self_illuminated += tag && v > 1.0f;
				}
			}
			if (XGPU_HDR_GLOW(tag))
			{
				/* (a glow's ceiling is its brightest channel's) */
				float most = fmaxf(vulkan[pixel * 4], fmaxf(vulkan[pixel * 4 + 1], vulkan[pixel * 4 + 2]));
				float limit = fmaxf(fmaxf(had[pixel * 4], fmaxf(had[pixel * 4 + 1], had[pixel * 4 + 2])), ceiling_value);

				if (most > limit * 1.002f + 0.002f)
					FAIL("%s: pixel %d's glow past its ceiling: %.5f over %.5f", name, pixel, (double)most, (double)limit);
			}
		}
	}
	printf("draws: %d draws (%d interlocked), %d values compared between the backends, %d past 1 unclamped, "
		"%d past 1 self-illuminated\n", draws, interlocked_draws, compared, unclamped, self_illuminated);
	if (draws != 141 || interlocked_draws != 72)
		FAIL("draws: %d drawn (%d interlocked), not 141 (72)", draws, interlocked_draws);
	if (unclamped < PIXELS)
		FAIL("draws: %d values past 1 unclamped", unclamped);
	if (self_illuminated < PIXELS / 4)
		FAIL("draws: %d values past 1 self-illuminated", self_illuminated);
}

int main(int argc, char **argv)
{
	@autoreleasepool
	{
		NSString *dir;

		if (argc != 3)
		{
			fprintf(stderr, "usage: harness post|draws <dir>\n");
			return 2;
		}
		device = MTLCreateSystemDefaultDevice();
		if (!device)
		{
			printf("no Metal device\n");
			return 77;
		}
		queue = [device newCommandQueue];
		dir = [NSString stringWithUTF8String:argv[2]];
		if (!strcmp(argv[1], "post"))
			post_test(dir);
		else
			draws_test(dir);
	}
	if (failures)
		printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
