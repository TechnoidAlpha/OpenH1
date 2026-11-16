/*
HARNESS.M

The high-res text's distance fields drawn by both backends' shaders on the
Mac's GPU (tools/test_text_msdf.py): the MSL emitter's fragment function
(msdf.metal) and the SPIR-V emitter's made Metal's by SPIRV-Cross
(msdf.vk.metal), each with a vertex function of its inputs
(*.vertex.metal: a quad, xT0 its texture's coordinates), over a glyph's
field (field.raw: width and height, 32 bits each, then its RGBA) drawn
at several sizes, from much smaller than the field to much larger. The
two must leave the same coverage, and the glyph must show.

  harness <dir>
*/

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <math.h>
#include <stdio.h>

static id<MTLDevice> device;

static id<MTLRenderPipelineState> pipeline_make(NSString *dir, NSString *stem, NSString *fragment_name)
{
	NSError *error = nil;
	NSString *fragment = [NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:
		[stem stringByAppendingString:@".metal"]] encoding:NSUTF8StringEncoding error:&error];
	NSString *vertex = [NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:
		[stem stringByAppendingString:@".vertex.metal"]] encoding:NSUTF8StringEncoding error:&error];
	id<MTLLibrary> fragments = [device newLibraryWithSource:fragment options:nil error:&error];
	id<MTLLibrary> vertices;
	MTLRenderPipelineDescriptor *description;
	id<MTLRenderPipelineState> state;

	if (!fragments)
	{
		fprintf(stderr, "%s: %s\n", stem.UTF8String, error.localizedDescription.UTF8String);
		exit(1);
	}
	vertices = [device newLibraryWithSource:vertex options:nil error:&error];
	if (!vertices)
	{
		fprintf(stderr, "%s vertex: %s\n", stem.UTF8String, error.localizedDescription.UTF8String);
		exit(1);
	}
	description = [MTLRenderPipelineDescriptor new];
	description.vertexFunction = [vertices newFunctionWithName:@"msdf_test_vertex"];
	description.fragmentFunction = [fragments newFunctionWithName:fragment_name];
	description.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
	state = [device newRenderPipelineStateWithDescriptor:description error:&error];
	if (!state)
	{
		fprintf(stderr, "%s pipeline: %s\n", stem.UTF8String, error.localizedDescription.UTF8String);
		exit(1);
	}
	return state;
}

/* the quad drawn at width by height pixels; its pixels' alpha into out */
static void draw(id<MTLRenderPipelineState> state, id<MTLTexture> field, id<MTLSamplerState> sampler,
	const void *uniforms, NSUInteger uniforms_size, NSUInteger uniforms_index, NSUInteger width, NSUInteger height,
	float *out)
{
	MTLTextureDescriptor *description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
		MTLPixelFormatRGBA32Float width:width height:height mipmapped:NO];
	id<MTLTexture> target;
	id<MTLCommandQueue> queue = [device newCommandQueue];
	id<MTLCommandBuffer> buffer = [queue commandBuffer];
	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLRenderCommandEncoder> encoder;
	float *pixels = malloc(width * height * 16);
	NSUInteger index;

	description.usage = MTLTextureUsageRenderTarget;
	description.storageMode = MTLStorageModeShared;
	target = [device newTextureWithDescriptor:description];
	pass.colorAttachments[0].texture = target;
	pass.colorAttachments[0].loadAction = MTLLoadActionClear;
	pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
	pass.colorAttachments[0].storeAction = MTLStoreActionStore;
	encoder = [buffer renderCommandEncoderWithDescriptor:pass];
	[encoder setRenderPipelineState:state];
	[encoder setFragmentBytes:uniforms length:uniforms_size atIndex:uniforms_index];
	[encoder setFragmentTexture:field atIndex:0];
	[encoder setFragmentSamplerState:sampler atIndex:0];
	[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[encoder endEncoding];
	[buffer commit];
	[buffer waitUntilCompleted];
	[target getBytes:pixels bytesPerRow:width * 16 fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
	for (index = 0; index < width * height; index++)
		out[index] = pixels[index * 4 + 3];
	free(pixels);
}

int main(int argc, char **argv)
{
	@autoreleasepool
	{
		static const NSUInteger heights[] = { 8, 12, 20, 32, 64, 160, 400 };
		NSString *dir;
		NSData *raw;
		const unsigned int *header;
		unsigned int width, height, offsets[2] = { 0, 0 };
		MTLTextureDescriptor *description;
		MTLSamplerDescriptor *sampling;
		id<MTLTexture> field;
		id<MTLSamplerState> sampler;
		id<MTLRenderPipelineState> metal, vulkan;
		unsigned char uniforms[4096];
		FILE *index;
		unsigned int size;
		double worst = 0.0;
		unsigned long compared = 0;

		if (argc != 2)
			return 2;
		device = MTLCreateSystemDefaultDevice();
		dir = [NSString stringWithUTF8String:argv[1]];
		index = fopen([[dir stringByAppendingPathComponent:@"index.txt"] UTF8String], "r");
		if (!index || fscanf(index, "texture_scale %u %u", &offsets[0], &offsets[1]) != 2)
			return 1;
		fclose(index);
		raw = [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"field.raw"]];
		header = raw.bytes;
		width = header[0];
		height = header[1];
		description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width
			height:height mipmapped:NO];
		field = [device newTextureWithDescriptor:description];
		[field replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:header + 2
			bytesPerRow:width * 4];
		sampling = [MTLSamplerDescriptor new];
		sampling.minFilter = sampling.magFilter = MTLSamplerMinMagFilterLinear;
		sampling.sAddressMode = sampling.tAddressMode = MTLSamplerAddressModeClampToEdge;
		sampler = [device newSamplerStateWithDescriptor:sampling];
		metal = pipeline_make(dir, @"msdf", @"xgpu_fragment");
		vulkan = pipeline_make(dir, @"msdf.vk", @"xgpu_vk_fragment");
		for (size = 0; size < sizeof(heights) / sizeof(heights[0]); size++)
		{
			NSUInteger h = heights[size], w = (h * width + height / 2) / height, pixel;
			float *a = malloc(w * h * 4), *b = malloc(w * h * 4);
			double ink = 0.0;
			float one[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

			memset(uniforms, 0, sizeof(uniforms));
			memcpy(uniforms + offsets[1], one, sizeof(one));
			draw(metal, field, sampler, uniforms, sizeof(uniforms), 17, w, h, a);
			memset(uniforms, 0, sizeof(uniforms));
			memcpy(uniforms + offsets[0], one, sizeof(one));
			draw(vulkan, field, sampler, uniforms, sizeof(uniforms), 1, w, h, b);
			for (pixel = 0; pixel < w * h; pixel++)
			{
				double difference = fabs((double)a[pixel] - (double)b[pixel]);

				if (difference > worst)
					worst = difference;
				ink += a[pixel];
				compared++;
			}
			ink /= (double)(w * h);
			printf("%lux%lu: ink %.3f\n", (unsigned long)w, (unsigned long)h, ink);
			if (!(ink > 0.05 && ink < 0.95))
			{
				printf("FAIL: the glyph does not show at %lu pixels\n", (unsigned long)h);
				return 1;
			}
			free(a);
			free(b);
		}
		printf("msdf: %lu pixels compared, the most apart %.6f\n", compared, worst);
		return worst > 1.0 / 512.0;
	}
}
