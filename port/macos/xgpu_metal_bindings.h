/*
XGPU_METAL_BINDINGS.H

Where the Metal backend (xgpu_metal.m) binds what a draw's shaders read: the
argument table indices of the MSL emitter's (port/linux/src/xgpu_msl.h)
[[buffer(n)]], [[texture(n)]] and [[sampler(n)]] attributes, and of the
vertex descriptor's buffers. Plain #defines, usable from C, Objective-C and
MSL. xgpu_metal_shaders.c checks that they are the emitter's.
*/

#ifndef __HALO_MACOS_XGPU_METAL_BINDINGS_H
#define __HALO_MACOS_XGPU_METAL_BINDINGS_H

/* the vertex streams: stream n is buffer XGPU_METAL_BUFFER_STREAM(n), and
attribute n of the vertex descriptor is [[attribute(n)]] */
#define XGPU_METAL_STREAM_COUNT 16
#define XGPU_METAL_BUFFER_STREAM(stream) (stream)
/* the vertex constants, 192 float4 (3 KB: setVertexBytes;
XGPU_MSL_BUFFER_CONSTANTS) */
#define XGPU_METAL_BUFFER_VERTEX_CONSTANTS 16
#define XGPU_METAL_VERTEX_CONSTANT_COUNT 192
/* the draw's other uniforms (the emitter's struct xgpu_msl_uniforms), bound
to both stages (setVertexBytes, setFragmentBytes; XGPU_MSL_BUFFER_UNIFORMS) */
#define XGPU_METAL_BUFFER_DRAW_UNIFORMS 17
/* the enhanced dynamic lights' block and their grid's cells, for the pixel
shaders that read them (xgpu_metal_set_dynamic_lights;
XGPU_MSL_BUFFER_DYNAMIC_LIGHTS, _DYNAMIC_LIGHT_CELLS) */
#define XGPU_METAL_BUFFER_DYNAMIC_LIGHTS 18
#define XGPU_METAL_BUFFER_DYNAMIC_LIGHT_CELLS 22
/* ... and their world (xgpu_metal_set_dynamic_light_world;
XGPU_MSL_BUFFER_DYNAMIC_LIGHT_WORLD) */
#define XGPU_METAL_BUFFER_DYNAMIC_LIGHT_WORLD 24
/* (19: the emitter's sample counter, which the macOS app does not use) */
/* the values of the inputs no stream feeds, 16 float4 (setVertexBytes),
read by the vertex descriptor at a constant step: the shaders read every
input as [[attribute(n)]] whether a stream feeds it or not */
#define XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES 20
/* (the VR mode's uniforms, should the macOS app ever draw both eyes) */
#define XGPU_METAL_BUFFER_VIEW_UNIFORMS 21

/* texture stage n samples texture n with sampler n: the game's four */
#define XGPU_METAL_TEXTURE_STAGE(stage) (stage)
#define XGPU_METAL_SAMPLER_STAGE(stage) (stage)
#define XGPU_METAL_TEXTURE_STAGE_COUNT 4
/* display.ambient_occlusion "world" (port/linux/src/xgpu_stream.h): the
field past the stages (XGPU_MSL_TEXTURE_WORLD_SDF) and the window's block
(XGPU_MSL_BUFFER_WORLD_AO), bound by xgpu_metal_set_world_ao, not as a
stage: a 3D texture at stage 5 would also be bound at the combiner's place
for it, XGPU_METAL_COMBINER_TEXTURE_3D + 5, which is a cube stage's */
#define XGPU_METAL_TEXTURE_WORLD_SDF 5
/* (and its table of cells, past it: 6 is no stage's, nor a combiner's place) */
#define XGPU_METAL_TEXTURE_WORLD_SDF_TABLE 6
/* (and the static scenery's pages of the level's occlusion: 7 is no
stage's, nor a combiner's place, whose 3D ones begin at 8) */
#define XGPU_METAL_TEXTURE_WORLD_AO_SCENERY 7
/* (and the vehicles' occlusion fields' atlas, a 3D texture of four
halves: past the combiner's places, 8 to 19) */
#define XGPU_METAL_TEXTURE_WORLD_AO_OBJECTS 20
#define XGPU_METAL_BUFFER_WORLD_AO 23
/* (and the occlusion found once a pixel, which the draws that read it
read: past the objects' atlas) */
#define XGPU_METAL_TEXTURE_WORLD_AO_PIXELS 21
/* (the World AO pass's own: the window's previous occlusion and the
distance-field scene's cascades, XGPU_MSL_WORLD_AO_PASS_*) */
#define XGPU_METAL_WORLD_AO_PASS_HISTORY 1
#define XGPU_METAL_WORLD_AO_PASS_CASCADES 2

#endif
