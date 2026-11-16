/*
XGPU_METAL_PIPELINE.M

What a draw's render pipeline state is made of, and its Metal descriptor
(xgpu_metal_pipeline.h). No device: the tool that builds the app's
pipelines ahead of time runs this too.
*/

#import <Metal/Metal.h>

#include "xgpu_metal_pipeline.h"
#include "xgpu_hdr.h"
#include "xgpu_stream.h"

#include <string.h>

/* ---------- formats */

#define SWIZZLE(r, g, b, a) { MTLTextureSwizzle##r, MTLTextureSwizzle##g, MTLTextureSwizzle##b, MTLTextureSwizzle##a }

static const struct xgpu_metal_format_information formats[XGPU_METAL_FORMAT_COUNT] = {
	[XGPU_METAL_FORMAT_NONE] = { MTLPixelFormatInvalid, 0, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_BGRA8] = { MTLPixelFormatBGRA8Unorm, 4, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_BGRX8] = { MTLPixelFormatBGRA8Unorm, 4, 0, 0, 0, SWIZZLE(Red, Green, Blue, One), 1 },
	/* (Metal names packed formats from the low bits up: B5G6R5 has red in
	the top 5 bits, as D3D's R5G6B5) */
	[XGPU_METAL_FORMAT_R5G6B5] = { MTLPixelFormatB5G6R5Unorm, 2, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_A1R5G5B5] = { MTLPixelFormatBGR5A1Unorm, 2, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_X1R5G5B5] = { MTLPixelFormatBGR5A1Unorm, 2, 0, 0, 0, SWIZZLE(Red, Green, Blue, One), 1 },
	/* A4R4G4B4 (alpha in the top 4 bits) is read through ABGR4, whose red
	is the top 4 bits: D3D's alpha, red, green, blue are its red, green,
	blue, alpha */
	[XGPU_METAL_FORMAT_A4R4G4B4] = { MTLPixelFormatABGR4Unorm, 2, 0, 0, 0, SWIZZLE(Green, Blue, Alpha, Red), 1 },
	[XGPU_METAL_FORMAT_L8] = { MTLPixelFormatR8Unorm, 1, 0, 0, 0, SWIZZLE(Red, Red, Red, One), 1 },
	[XGPU_METAL_FORMAT_A8] = { MTLPixelFormatA8Unorm, 1, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	/* A8L8: luminance in the low byte (red), alpha in the high (green) */
	[XGPU_METAL_FORMAT_A8L8] = { MTLPixelFormatRG8Unorm, 2, 0, 0, 0, SWIZZLE(Red, Red, Red, Green), 1 },
	[XGPU_METAL_FORMAT_DXT1] = { MTLPixelFormatBC1_RGBA, 8, 1, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_DXT3] = { MTLPixelFormatBC2_RGBA, 16, 1, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_DXT5] = { MTLPixelFormatBC3_RGBA, 16, 1, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_D24S8] = { MTLPixelFormatDepth32Float_Stencil8, 8, 0, 1, 1, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_D16] = { MTLPixelFormatDepth16Unorm, 2, 0, 1, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_RGBA8] = { MTLPixelFormatRGBA8Unorm, 4, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_COVERAGE] = { MTLPixelFormatR8Unorm, 1, 0, 0, 0, SWIZZLE(One, One, One, Red), 1 },
	[XGPU_METAL_FORMAT_RGBA16F] = { MTLPixelFormatRGBA16Float, 8, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_ASTC] = { MTLPixelFormatASTC_4x4_LDR, 16, 1, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_R8_SNORM] = { MTLPixelFormatR8Snorm, 1, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_R8] = { MTLPixelFormatR8Unorm, 1, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_RG8] = { MTLPixelFormatRG8Unorm, 2, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_RGBA32UI] = { MTLPixelFormatRGBA32Uint, 16, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_RGBA32F] = { MTLPixelFormatRGBA32Float, 16, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
	[XGPU_METAL_FORMAT_RG8_SNORM] = { MTLPixelFormatRG8Snorm, 2, 0, 0, 0, SWIZZLE(Red, Green, Blue, Alpha), 0 },
};

const struct xgpu_metal_format_information *xgpu_metal_format_information(uint32_t format)
{
	return format < XGPU_METAL_FORMAT_COUNT ? &formats[format] : &formats[XGPU_METAL_FORMAT_NONE];
}

/* ---------- the interface's values in Metal's */

static const MTLCompareFunction compare_functions[8] = {
	MTLCompareFunctionNever, MTLCompareFunctionLess, MTLCompareFunctionEqual, MTLCompareFunctionLessEqual,
	MTLCompareFunctionGreater, MTLCompareFunctionNotEqual, MTLCompareFunctionGreaterEqual, MTLCompareFunctionAlways,
};

static const MTLBlendFactor blend_factors[15] = {
	MTLBlendFactorZero, MTLBlendFactorOne, MTLBlendFactorSourceColor, MTLBlendFactorOneMinusSourceColor,
	MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorDestinationAlpha,
	MTLBlendFactorOneMinusDestinationAlpha, MTLBlendFactorDestinationColor, MTLBlendFactorOneMinusDestinationColor,
	MTLBlendFactorSourceAlphaSaturated, MTLBlendFactorBlendColor, MTLBlendFactorOneMinusBlendColor,
	MTLBlendFactorBlendAlpha, MTLBlendFactorOneMinusBlendAlpha,
};

static const MTLBlendOperation blend_operations[5] = {
	MTLBlendOperationAdd, MTLBlendOperationSubtract, MTLBlendOperationReverseSubtract, MTLBlendOperationMin,
	MTLBlendOperationMax,
};

static const MTLStencilOperation stencil_operations[8] = {
	MTLStencilOperationKeep, MTLStencilOperationZero, MTLStencilOperationReplace,
	MTLStencilOperationIncrementClamp, MTLStencilOperationDecrementClamp, MTLStencilOperationInvert,
	MTLStencilOperationIncrementWrap, MTLStencilOperationDecrementWrap,
};

MTLCompareFunction xgpu_metal_compare_function(uint32_t value)
{
	return value < 8 ? compare_functions[value] : MTLCompareFunctionAlways;
}

MTLBlendFactor xgpu_metal_blend_factor(uint32_t value)
{
	return value < 15 ? blend_factors[value] : MTLBlendFactorOne;
}

MTLBlendOperation xgpu_metal_blend_operation(uint32_t value)
{
	return value < 5 ? blend_operations[value] : MTLBlendOperationAdd;
}

MTLStencilOperation xgpu_metal_stencil_operation(uint32_t value)
{
	return value < 8 ? stencil_operations[value] : MTLStencilOperationKeep;
}

MTLVertexFormat xgpu_metal_vertex_format(uint32_t format)
{
	static const MTLVertexFormat vertex_formats[] = {
		[XGPU_METAL_ATTRIBUTE_FLOAT1] = MTLVertexFormatFloat,
		[XGPU_METAL_ATTRIBUTE_FLOAT2] = MTLVertexFormatFloat2,
		[XGPU_METAL_ATTRIBUTE_FLOAT3] = MTLVertexFormatFloat3,
		[XGPU_METAL_ATTRIBUTE_FLOAT4] = MTLVertexFormatFloat4,
		[XGPU_METAL_ATTRIBUTE_D3DCOLOR] = MTLVertexFormatUChar4Normalized_BGRA,
		[XGPU_METAL_ATTRIBUTE_SHORT1] = MTLVertexFormatShort,
		[XGPU_METAL_ATTRIBUTE_SHORT2] = MTLVertexFormatShort2,
		[XGPU_METAL_ATTRIBUTE_SHORT3] = MTLVertexFormatShort3,
		[XGPU_METAL_ATTRIBUTE_SHORT4] = MTLVertexFormatShort4,
		[XGPU_METAL_ATTRIBUTE_NORMSHORT1] = MTLVertexFormatShortNormalized,
		[XGPU_METAL_ATTRIBUTE_NORMSHORT2] = MTLVertexFormatShort2Normalized,
		[XGPU_METAL_ATTRIBUTE_NORMSHORT3] = MTLVertexFormatShort3Normalized,
		[XGPU_METAL_ATTRIBUTE_NORMSHORT4] = MTLVertexFormatShort4Normalized,
		[XGPU_METAL_ATTRIBUTE_PBYTE1] = MTLVertexFormatUCharNormalized,
		[XGPU_METAL_ATTRIBUTE_PBYTE2] = MTLVertexFormatUChar2Normalized,
		[XGPU_METAL_ATTRIBUTE_PBYTE3] = MTLVertexFormatUChar3Normalized,
		[XGPU_METAL_ATTRIBUTE_PBYTE4] = MTLVertexFormatUChar4Normalized,
		[XGPU_METAL_ATTRIBUTE_FLOAT2H] = MTLVertexFormatFloat3,
		[XGPU_METAL_ATTRIBUTE_PACKED] = MTLVertexFormatUInt,
	};

	return format < sizeof(vertex_formats) / sizeof(vertex_formats[0]) ? vertex_formats[format] : MTLVertexFormatInvalid;
}

/* ---------- render pipeline states */

void xgpu_metal_pipeline_state_key_make(const struct xgpu_metal_pipeline *pipeline,
	struct xgpu_metal_pipeline_state_key *key)
{
	int index;

	memset(key, 0, sizeof(*key));
	key->vertex_shader = pipeline->vertex_shader;
	key->pixel_shader = pipeline->pixel_shader;
	memcpy(key->attributes, pipeline->attributes, sizeof(key->attributes));
	for (index = 0; index < 16; index++)
	{
		if (key->attributes[index].format != XGPU_METAL_ATTRIBUTE_NONE &&
			key->attributes[index].stream < XGPU_METAL_STREAM_COUNT)
			key->strides[key->attributes[index].stream] = pipeline->strides[key->attributes[index].stream];
	}
	key->blend = pipeline->blend;
	if (key->blend)
	{
		key->blend_source = pipeline->blend_source;
		key->blend_destination = pipeline->blend_destination;
		key->blend_operation = pipeline->blend_operation;
	}
	key->color_mask = pipeline->color_mask;
	key->color_format = pipeline->color_format;
	key->depth_format = pipeline->depth_format;
	key->samples = pipeline->samples > 1 ? pipeline->samples : 1;
}

MTLRenderPipelineDescriptor *xgpu_metal_pipeline_descriptor(const struct xgpu_metal_pipeline_state_key *key)
{
	MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
	MTLVertexDescriptor *vertex = [MTLVertexDescriptor vertexDescriptor];
	int index;

	for (index = 0; index < 16; index++)
	{
		const struct xgpu_metal_vertex_attribute *attribute = &key->attributes[index];

		if (attribute->format == XGPU_METAL_ATTRIBUTE_NONE)
			continue;
		if (attribute->stream == XGPU_METAL_STREAM_CONSTANT)
		{
			/* (its register's value, the same for every vertex) */
			vertex.attributes[index].format = MTLVertexFormatFloat4;
			vertex.attributes[index].offset = (NSUInteger)index * 16;
			vertex.attributes[index].bufferIndex = XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES;
			vertex.layouts[XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES].stride = 16 * 16;
			vertex.layouts[XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES].stepFunction = MTLVertexStepFunctionConstant;
			vertex.layouts[XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES].stepRate = 0;
			continue;
		}
		if (attribute->stream >= XGPU_METAL_STREAM_COUNT)
			continue;
		vertex.attributes[index].format = xgpu_metal_vertex_format(attribute->format);
		vertex.attributes[index].offset = attribute->offset;
		vertex.attributes[index].bufferIndex = XGPU_METAL_BUFFER_STREAM(attribute->stream);
		vertex.layouts[XGPU_METAL_BUFFER_STREAM(attribute->stream)].stride =
			key->strides[attribute->stream] ? key->strides[attribute->stream] : 4;
	}
	descriptor.vertexDescriptor = vertex;
	descriptor.rasterSampleCount = key->samples;
	if (key->color_format)
	{
		MTLRenderPipelineColorAttachmentDescriptor *attachment = descriptor.colorAttachments[0];

		attachment.pixelFormat = xgpu_metal_format_information(key->color_format)->pixel_format;
		attachment.writeMask = ((key->color_mask & XGPU_WRITE_RED) ? MTLColorWriteMaskRed : 0) |
			((key->color_mask & XGPU_WRITE_GREEN) ? MTLColorWriteMaskGreen : 0) |
			((key->color_mask & XGPU_WRITE_BLUE) ? MTLColorWriteMaskBlue : 0) |
			((key->color_mask & XGPU_WRITE_ALPHA) ? MTLColorWriteMaskAlpha : 0);
		attachment.blendingEnabled = key->blend != 0;
		attachment.sourceRGBBlendFactor = attachment.sourceAlphaBlendFactor = xgpu_metal_blend_factor(key->blend_source);
		attachment.destinationRGBBlendFactor = attachment.destinationAlphaBlendFactor =
			xgpu_metal_blend_factor(key->blend_destination);
		attachment.rgbBlendOperation = attachment.alphaBlendOperation = xgpu_metal_blend_operation(key->blend_operation);
	}
	if (key->depth_format)
	{
		descriptor.depthAttachmentPixelFormat = xgpu_metal_format_information(key->depth_format)->pixel_format;
		if (xgpu_metal_format_information(key->depth_format)->stencil)
			descriptor.stencilAttachmentPixelFormat = descriptor.depthAttachmentPixelFormat;
	}
	return descriptor;
}

int xgpu_metal_pipeline_archivable(const struct xgpu_metal_pipeline_state_key *key)
{
	return key->color_format && key->color_mask;
}

MTLRenderPipelineDescriptor *xgpu_metal_clear_descriptor(const struct xgpu_metal_clear_key *key)
{
	MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];

	descriptor.rasterSampleCount = key->samples;
	if (key->color_format)
	{
		descriptor.colorAttachments[0].pixelFormat = xgpu_metal_format_information(key->color_format)->pixel_format;
		descriptor.colorAttachments[0].writeMask = ((key->parts & XGPU_METAL_CLEAR_RED) ? MTLColorWriteMaskRed : 0) |
			((key->parts & XGPU_METAL_CLEAR_GREEN) ? MTLColorWriteMaskGreen : 0) |
			((key->parts & XGPU_METAL_CLEAR_BLUE) ? MTLColorWriteMaskBlue : 0) |
			((key->parts & XGPU_METAL_CLEAR_ALPHA) ? MTLColorWriteMaskAlpha : 0);
	}
	if (key->depth_format)
	{
		descriptor.depthAttachmentPixelFormat = xgpu_metal_format_information(key->depth_format)->pixel_format;
		if (xgpu_metal_format_information(key->depth_format)->stencil)
			descriptor.stencilAttachmentPixelFormat = descriptor.depthAttachmentPixelFormat;
	}
	return descriptor;
}

MTLRenderPipelineDescriptor *xgpu_metal_present_descriptor(void)
{
	MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];

	descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	return descriptor;
}

/* (the clears: a rectangle in clip space, a colour, a depth; the
presentation: the frame's picture over the drawable, opaque) */
NSString *const xgpu_metal_fixed_source =
	@"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct clear_parameters { float4 rectangle; float4 color; float depth; };\n"
	"struct clear_vertex { float4 position [[position]]; };\n"
	"vertex clear_vertex xgpu_clear_vertex(uint id [[vertex_id]], constant clear_parameters &p [[buffer(0)]])\n"
	"{\n"
	"\tclear_vertex out;\n"
	"\tfloat2 corner = float2((id & 1) ? p.rectangle.z : p.rectangle.x, (id & 2) ? p.rectangle.w : p.rectangle.y);\n"
	"\tout.position = float4(corner, p.depth, 1.0);\n"
	"\treturn out;\n"
	"}\n"
	"fragment float4 xgpu_clear_fragment(constant clear_parameters &p [[buffer(0)]])\n"
	"{\n"
	"\treturn p.color;\n"
	"}\n"
	"struct present_vertex { float4 position [[position]]; float2 coordinates; };\n"
	"vertex present_vertex halo_present_vertex(uint id [[vertex_id]])\n"
	"{\n"
	"\tpresent_vertex out;\n"
	"\tfloat2 corner = float2(id & 1, id >> 1);\n"
	"\tout.position = float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);\n"
	"\tout.coordinates = corner;\n"
	"\treturn out;\n"
	"}\n"
	"fragment float4 halo_present_fragment(present_vertex in [[stage_in]], texture2d<float> picture [[texture(0)]],\n"
	"\tsampler linear [[sampler(0)]])\n"
	"{\n"
	"\treturn float4(picture.sample(linear, in.coordinates).rgb, 1.0);\n"
	"}\n";

/* ---------- the command stream's pipelines */

/* the stream's target formats (XGPU_FORMAT_*: BGRA8 1, D24S8 2, RGBA16F 3) */
static uint8_t target_format(uint8_t format)
{
	return format == 1 ? XGPU_METAL_FORMAT_BGRA8 : format == 2 ? XGPU_METAL_FORMAT_D24S8 :
		format == 3 ? XGPU_METAL_FORMAT_RGBA16F :
		format == XGPU_STREAM_FORMAT_WORLD_AO_INPUTS ? XGPU_METAL_FORMAT_RGBA32UI : XGPU_METAL_FORMAT_NONE;
}

void xgpu_metal_pipeline_from_key(const struct xgpu_stream_pipeline_key *key, struct xgpu_metal_pipeline *pipeline)
{
	memset(pipeline, 0, sizeof(*pipeline));
	/* (display.hdr: a draw whose pixel shader blends it itself, reading the
	pixel it draws over, has no blending of Metal's: xgpu_hdr.h) */
	pipeline->blend = XGPU_HDR_SHADER_BLENDS(key->pixel.hdr) ? 0 : key->blend;
	pipeline->blend_source = key->blend_source;
	pipeline->blend_destination = key->blend_destination;
	pipeline->blend_operation = key->blend_operation;
	pipeline->color_mask = key->color_mask;
	pipeline->depth_test = key->depth_test;
	pipeline->depth_function = key->depth_function;
	pipeline->depth_write = key->depth_write;
	pipeline->stencil_test = key->stencil_test;
	pipeline->stencil_function = key->stencil_function;
	pipeline->stencil_fail = key->stencil_fail;
	pipeline->stencil_depth_fail = key->stencil_depth_fail;
	pipeline->stencil_pass = key->stencil_pass;
	pipeline->stencil_read_mask = (uint8_t)key->stencil_read_mask;
	pipeline->stencil_write_mask = (uint8_t)key->stencil_write_mask;
	pipeline->cull = key->cull;
	pipeline->front_counter_clockwise = key->front_counter_clockwise;
	pipeline->cull_front = key->cull_front;
	pipeline->fill = key->fill;
	pipeline->depth_bias = key->depth_bias;
	pipeline->color_format = target_format(key->color_format);
	pipeline->depth_format = target_format(key->depth_format);
	pipeline->samples = key->samples > 1 ? key->samples : 1;
}

/* XGPU_VERTEX_*: XGPU_METAL_ATTRIBUTE_* are the same */
static uint8_t attribute_format(uint8_t format)
{
	return format < XGPU_METAL_ATTRIBUTE_COUNT ? format : XGPU_METAL_ATTRIBUTE_NONE;
}

uint16_t xgpu_metal_layout_attributes(const struct xgpu_stream_vertex_element *elements, uint32_t count,
	struct xgpu_metal_vertex_attribute attributes[16])
{
	uint32_t fed = 0, integers = 0, index;

	memset(attributes, 0, 16 * sizeof(attributes[0]));
	for (index = 0; index < count; index++)
	{
		const struct xgpu_stream_vertex_element *element = &elements[index];

		if (element->reg >= 16 || element->stream >= XGPU_METAL_STREAM_COUNT)
			continue;
		attributes[element->reg].format = attribute_format(element->format);
		attributes[element->reg].stream = element->stream;
		attributes[element->reg].offset = element->offset;
		if (attributes[element->reg].format != XGPU_METAL_ATTRIBUTE_NONE)
			fed |= 1u << element->reg;
		/* (unnormalized shorts are read as integers, which the shader
		converts) */
		if (element->format >= XGPU_METAL_ATTRIBUTE_SHORT1 && element->format <= XGPU_METAL_ATTRIBUTE_SHORT4)
			integers |= 1u << element->reg;
	}
	/* (every other input, from its value) */
	for (index = 0; index < 16; index++)
	{
		if (!(fed & (1u << index)))
		{
			attributes[index].format = XGPU_METAL_ATTRIBUTE_FLOAT4;
			attributes[index].stream = XGPU_METAL_STREAM_CONSTANT;
		}
	}
	return (uint16_t)integers;
}

uint32_t xgpu_metal_layout_immediate(uint32_t written, struct xgpu_metal_vertex_attribute attributes[16],
	uint16_t strides[XGPU_METAL_STREAM_COUNT])
{
	uint32_t index, count = 0;

	memset(attributes, 0, 16 * sizeof(attributes[0]));
	memset(strides, 0, XGPU_METAL_STREAM_COUNT * sizeof(strides[0]));
	for (index = 0; index < 16; index++)
	{
		attributes[index].format = XGPU_METAL_ATTRIBUTE_FLOAT4;
		if (written & (1u << index))
		{
			attributes[index].stream = 0;
			attributes[index].offset = (uint16_t)(count++ * 16);
		}
		else
		{
			attributes[index].stream = XGPU_METAL_STREAM_CONSTANT;
		}
	}
	strides[0] = (uint16_t)(count * 16);
	return count;
}

/* XGPU_SOURCE_NONE; _xgpu_sampler_3d, _cube, _2d_layered (xgpu.h) */
enum
{
	_source_none = 0,
	_sampler_3d = 2,
	_sampler_cube = 3,
	_sampler_2d_layered = 4,
};

_Static_assert(XGPU_METAL_ADDRESS_REGION == 5, "XGPU_ADDRESS_REGION's number");

uint8_t xgpu_metal_border_axes(uint8_t source, uint8_t sampler_type, const struct xgpu_stream_sampler *sampler)
{
	uint8_t axes = 0;
	int axis;

	/* (the shader draws the border of a 2D or 3D texture's sampler, whatever
	its colour, the uniforms': the pipeline is the same for every colour. A
	draw's border colour is the texture stage's as the draw before left it
	where the game's code sets none, so that a pipeline told apart by it,
	Metal's own three colours drawn by the sampler and the others by the
	shader, was made as the map was prepared for one and missed in play for
	the other: a10's screen effects, prepared with 0x05050505 and drawn
	with 0) */
	if (source == _source_none || sampler_type == _sampler_cube || sampler_type == _sampler_2d_layered)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		if (sampler->address[axis] == XGPU_METAL_ADDRESS_BORDER)
			axes |= (uint8_t)(1u << axis);
	}
	/* (a region of a 2D texture: its border on u and v, the shader's) */
	if (sampler->address[0] == XGPU_METAL_ADDRESS_REGION && sampler_type != _sampler_3d)
		axes = 3 | XGPU_METAL_BORDER_REGION;
	/* (the high-res text's distance fields, a 2D texture's: the shader's) */
	if (sampler->address[2] == XGPU_METAL_ADDRESS_MSDF && sampler_type != _sampler_3d)
		axes |= XGPU_METAL_BORDER_MSDF;
	return axes;
}
