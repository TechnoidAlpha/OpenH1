/*
XGPU_METAL_PREBUILD_SCRIPT.M

The pipelines script of xgpu_metal_prebuild.c (metal-tt's JSON,
metal-pipelines-script(5)): each pipeline's descriptor as the backend makes
it (xgpu_metal_pipeline.m), written as the backend's own archives' scripts
are (metal-source -flatbuffers=json harvested.metallib).
*/

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "xgpu_metal_pipeline.h"
#include "xgpu_metal_prebuild.h"

#include <stdio.h>
#include <stdlib.h>

static NSMutableArray *pipelines;

static void fail(const char *what)
{
	fprintf(stderr, "xgpu_metal_prebuild: %s\n", what);
	exit(1);
}

/* ---------- the pipelines script (metal-pipelines-script(5)) */

static const char *vertex_format_name(MTLVertexFormat format)
{
	switch (format)
	{
	case MTLVertexFormatFloat: return "Float";
	case MTLVertexFormatFloat2: return "Float2";
	case MTLVertexFormatFloat3: return "Float3";
	case MTLVertexFormatFloat4: return "Float4";
	case MTLVertexFormatUChar4Normalized_BGRA: return "UChar4Normalized_BGRA";
	case MTLVertexFormatShort: return "Short";
	case MTLVertexFormatShort2: return "Short2";
	case MTLVertexFormatShort3: return "Short3";
	case MTLVertexFormatShort4: return "Short4";
	case MTLVertexFormatShortNormalized: return "ShortNormalized";
	case MTLVertexFormatShort2Normalized: return "Short2Normalized";
	case MTLVertexFormatShort3Normalized: return "Short3Normalized";
	case MTLVertexFormatShort4Normalized: return "Short4Normalized";
	case MTLVertexFormatUCharNormalized: return "UCharNormalized";
	case MTLVertexFormatUChar2Normalized: return "UChar2Normalized";
	case MTLVertexFormatUChar3Normalized: return "UChar3Normalized";
	case MTLVertexFormatUChar4Normalized: return "UChar4Normalized";
	case MTLVertexFormatUInt: return "UInt";
	default: return NULL;
	}
}

static const char *pixel_format_name(MTLPixelFormat format)
{
	switch (format)
	{
	case MTLPixelFormatInvalid: return "Invalid";
	case MTLPixelFormatBGRA8Unorm: return "BGRA8Unorm";
	/* (display.hdr's targets) */
	case MTLPixelFormatRGBA16Float: return "RGBA16Float";
	/* (display.ambient_occlusion "world"'s pre-pass's inputs, and its pass's) */
	case MTLPixelFormatRGBA32Uint: return "RGBA32Uint";
	case MTLPixelFormatRGBA32Float: return "RGBA32Float";
	case MTLPixelFormatDepth32Float_Stencil8: return "Depth32Float_Stencil8";
	case MTLPixelFormatDepth16Unorm: return "Depth16Unorm";
	default: return NULL;
	}
}

static const char *blend_factor_name(MTLBlendFactor factor)
{
	static const char *const names[] = {
		[MTLBlendFactorZero] = "Zero",
		[MTLBlendFactorOne] = "One",
		[MTLBlendFactorSourceColor] = "SourceColor",
		[MTLBlendFactorOneMinusSourceColor] = "OneMinusSourceColor",
		[MTLBlendFactorSourceAlpha] = "SourceAlpha",
		[MTLBlendFactorOneMinusSourceAlpha] = "OneMinusSourceAlpha",
		[MTLBlendFactorDestinationColor] = "DestinationColor",
		[MTLBlendFactorOneMinusDestinationColor] = "OneMinusDestinationColor",
		[MTLBlendFactorDestinationAlpha] = "DestinationAlpha",
		[MTLBlendFactorOneMinusDestinationAlpha] = "OneMinusDestinationAlpha",
		[MTLBlendFactorSourceAlphaSaturated] = "SourceAlphaSaturated",
		[MTLBlendFactorBlendColor] = "BlendColor",
		[MTLBlendFactorOneMinusBlendColor] = "OneMinusBlendColor",
		[MTLBlendFactorBlendAlpha] = "BlendAlpha",
		[MTLBlendFactorOneMinusBlendAlpha] = "OneMinusBlendAlpha",
	};

	return (NSUInteger)factor < sizeof(names) / sizeof(names[0]) ? names[factor] : NULL;
}

static const char *blend_operation_name(MTLBlendOperation operation)
{
	static const char *const names[] = {
		[MTLBlendOperationAdd] = "Add",
		[MTLBlendOperationSubtract] = "Subtract",
		[MTLBlendOperationReverseSubtract] = "ReverseSubtract",
		[MTLBlendOperationMin] = "Min",
		[MTLBlendOperationMax] = "Max",
	};

	return (NSUInteger)operation < sizeof(names) / sizeof(names[0]) ? names[operation] : NULL;
}

/* a pipeline's entry in the script: as the harvested scripts of the
backend's own archives write it (metal-source -flatbuffers=json), the
attributes and the layouts up to the last used in order */
static NSDictionary *pipeline_entry(MTLRenderPipelineDescriptor *descriptor, NSString *vertex, NSString *fragment)
{
	NSMutableDictionary *entry = [NSMutableDictionary dictionary];
	NSMutableArray *attributes = [NSMutableArray array], *layouts = [NSMutableArray array];
	MTLRenderPipelineColorAttachmentDescriptor *color = descriptor.colorAttachments[0];
	NSUInteger index, last_attribute = 0, last_layout = 0, used = 0;

	entry[@"vertex_function"] = [@"alias:library#" stringByAppendingString:vertex];
	if (fragment)
		entry[@"fragment_function"] = [@"alias:library#" stringByAppendingString:fragment];
	for (index = 0; index < 31; index++)
	{
		MTLVertexAttributeDescriptor *attribute = descriptor.vertexDescriptor.attributes[index];

		if (attribute.format != MTLVertexFormatInvalid)
		{
			last_attribute = index + 1;
			used |= 1u << attribute.bufferIndex;
			if (attribute.bufferIndex + 1 > last_layout)
				last_layout = attribute.bufferIndex + 1;
		}
	}
	for (index = 0; index < last_attribute; index++)
	{
		MTLVertexAttributeDescriptor *attribute = descriptor.vertexDescriptor.attributes[index];
		const char *name = attribute.format == MTLVertexFormatInvalid ? "Invalid" : vertex_format_name(attribute.format);

		if (!name)
			fail("a vertex format the script writer does not know");
		[attributes addObject:@{ @"format": @(name), @"offset": @(attribute.offset),
			@"buffer_index": @(attribute.bufferIndex) }];
	}
	for (index = 0; index < last_layout; index++)
	{
		MTLVertexBufferLayoutDescriptor *layout = descriptor.vertexDescriptor.layouts[index];

		[layouts addObject:@{
			@"step_function": layout.stepFunction == MTLVertexStepFunctionConstant ? @"Constant" : @"PerVertex",
			@"step_rate": @(layout.stepRate), @"stride": @(layout.stride) }];
	}
	if (last_attribute)
		entry[@"vertex_descriptor"] = @{ @"attributes": attributes, @"layouts": layouts };
	entry[@"raster_sample_count"] = @(descriptor.rasterSampleCount);
	if (color.pixelFormat != MTLPixelFormatInvalid)
	{
		const char *format = pixel_format_name(color.pixelFormat), *source = blend_factor_name(color.sourceRGBBlendFactor),
			*destination = blend_factor_name(color.destinationRGBBlendFactor),
			*source_alpha = blend_factor_name(color.sourceAlphaBlendFactor),
			*destination_alpha = blend_factor_name(color.destinationAlphaBlendFactor),
			*operation = blend_operation_name(color.rgbBlendOperation),
			*alpha_operation = blend_operation_name(color.alphaBlendOperation);

		if (!format || !source || !destination || !source_alpha || !destination_alpha || !operation || !alpha_operation)
			fail("a colour attachment's value the script writer does not know");
		entry[@"color_attachments"] = @[ @{ @"pixel_format": @(format), @"write_mask": @(color.writeMask),
			@"blending_enabled": @(color.blendingEnabled), @"rgb_blend_operation": @(operation),
			@"alpha_blend_operation": @(alpha_operation), @"source_rgb_blend_factor": @(source),
			@"destination_rgb_blend_factor": @(destination), @"source_alpha_blend_factor": @(source_alpha),
			@"destination_alpha_blend_factor": @(destination_alpha) } ];
	}
	if (descriptor.depthAttachmentPixelFormat != MTLPixelFormatInvalid)
	{
		if (!pixel_format_name(descriptor.depthAttachmentPixelFormat))
			fail("a depth format the script writer does not know");
		entry[@"depth_attachment_pixel_format"] = @(pixel_format_name(descriptor.depthAttachmentPixelFormat));
	}
	if (descriptor.stencilAttachmentPixelFormat != MTLPixelFormatInvalid)
		entry[@"stencil_attachment_pixel_format"] = @(pixel_format_name(descriptor.stencilAttachmentPixelFormat));
	(void)used;
	return entry;
}

static void pipeline_add(MTLRenderPipelineDescriptor *descriptor, const char *vertex, const char *fragment)
{
	if (!pipelines)
		pipelines = [NSMutableArray array];
	[pipelines addObject:pipeline_entry(descriptor, @(vertex), fragment ? @(fragment) : nil)];
}

void prebuild_script_pipeline(const struct xgpu_metal_pipeline_state_key *key, const char *vertex, const char *fragment)
{
	@autoreleasepool
	{
		pipeline_add(xgpu_metal_pipeline_descriptor(key), vertex, fragment);
	}
}

void prebuild_script_clear(const struct xgpu_metal_clear_key *key)
{
	@autoreleasepool
	{
		pipeline_add(xgpu_metal_clear_descriptor(key), "xgpu_clear_vertex", "xgpu_clear_fragment");
	}
}

void prebuild_script_present(void)
{
	@autoreleasepool
	{
		pipeline_add(xgpu_metal_present_descriptor(), "halo_present_vertex", "halo_present_fragment");
	}
}

unsigned long prebuild_script_write(const char *path, const char *library)
{
	@autoreleasepool
	{
		NSError *error = nil;
		NSData *script = [NSJSONSerialization dataWithJSONObject:@{
			@"version": @{ @"major": @0, @"minor": @3, @"sub_minor": @0 },
			@"libraries": @{ @"paths": @[ @{ @"label": @"library", @"path": @(library) } ] },
			@"pipelines": @{ @"render_pipelines": pipelines ? pipelines : @[] },
		} options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];

		if (!script || ![script writeToFile:@(path) atomically:YES])
			fail("cannot write the pipelines script");
		return pipelines.count;
	}
}

int prebuild_write_text(const char *path, const char *text, unsigned long size)
{
	@autoreleasepool
	{
		return [[NSData dataWithBytes:text length:size] writeToFile:@(path) atomically:NO];
	}
}

void prebuild_write_fixed(const char *path)
{
	@autoreleasepool
	{
		if (![[xgpu_metal_fixed_source dataUsingEncoding:NSUTF8StringEncoding] writeToFile:@(path) atomically:NO])
			fail("cannot write the fixed functions' source");
	}
}
