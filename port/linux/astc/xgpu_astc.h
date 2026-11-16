/*
XGPU_ASTC.H

The Xbox's DXT textures as ASTC 4x4 blocks, for the hosts' renderers on
GPUs that sample ASTC but not BC (iPhones, most Android phones), which
decoded them to 32-bit texels: four times the memory and bandwidth of the
ASTC blocks, eight times DXT1's.

A DXT block is transcoded to the ASTC block that decodes to the same texels
where one exists (xgpu_astc.c, xgpu_astc_transcode): every DXT1 block but
those mixing transparent texels with two or more colors, and DXT3 and DXT5
blocks whose alpha is the same across the block. The rest are encoded from
their decoded texels by ARM's astc-encoder (port/third_party/astc-encoder,
through xgpu_astc_encode.cpp), on worker threads, and the encoded blocks
kept on disk by the hash of the texture's DXT blocks (xgpu_astc_cache.c).
Every block of a texture is independent, so a texture is a list of blocks
here: its levels and faces one after another, as the backend lays them.
*/

#ifndef __HALO_LINUX_XGPU_ASTC_H
#define __HALO_LINUX_XGPU_ASTC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the DXT formats (DXT2 is DXT3's blocks, DXT4 DXT5's) */
enum
{
	XGPU_ASTC_DXT1,
	XGPU_ASTC_DXT3,
	XGPU_ASTC_DXT5,
};

#define XGPU_ASTC_BLOCK_BYTES 16

/* a DXT format's block's bytes (8 or 16) */
unsigned xgpu_astc_dxt_block_bytes(int kind);

/* whether the DXT textures are drawn as ASTC on a GPU, by the setting
display.astc_textures (port_config.c): "auto" (the default but on
Android, and anything else) where it samples ASTC and not BC, "always" where it samples ASTC
(beside BC: to try it on a Mac), "off" never */
int xgpu_astc_chosen(const char *setting, int block_compression, int astc);

/* ---------- the transcoding (xgpu_astc.c) */

/* count DXT blocks of the kind as ASTC 4x4 blocks (astc: 16 bytes each),
returning how many could not be transcoded exactly: those are marked in
encode (a byte each, 1; may be NULL) and written as a block of their mean
color, to be encoded */
uint32_t xgpu_astc_transcode(int kind, const uint8_t *dxt, uint32_t count, uint8_t *astc, uint8_t *encode);

/* a DXT block's 16 texels, rows from the top, as RGBA bytes */
void xgpu_astc_decode_dxt(int kind, const uint8_t *block, uint8_t rgba[64]);

/* ---------- the encoder (xgpu_astc_encode.cpp: astc-encoder's) */

/* astc-encoder's effort for the blocks transcoding leaves: its "fast"
preset (tools/test_astc.py measures the others) */
#define XGPU_ASTC_ENCODER_QUALITY 10.0f

struct xgpu_astc_encoder;

/* an encoder, for one thread at a time; NULL if it cannot be made */
struct xgpu_astc_encoder *xgpu_astc_encoder_create(float quality);
void xgpu_astc_encoder_destroy(struct xgpu_astc_encoder *encoder);
/* count blocks' texels (rgba: 64 bytes a block, as xgpu_astc_decode_dxt
writes them) encoded as ASTC 4x4 blocks; zero if they could not be */
int xgpu_astc_encode(struct xgpu_astc_encoder *encoder, const uint8_t *rgba, uint32_t count, uint8_t *astc);
/* count ASTC 4x4 blocks decoded to their texels (as xgpu_astc_encode takes
them); zero if they could not be */
int xgpu_astc_decode(struct xgpu_astc_encoder *encoder, const uint8_t *astc, uint32_t count, uint8_t *rgba);

/* count DXT blocks as ASTC blocks, transcoded and the rest encoded (encode:
which, as xgpu_astc_transcode marks them; may be NULL); the blocks encoded,
or -1 if they could not be (no encoder, or it failed: they are left their
mean color) */
long xgpu_astc_convert(struct xgpu_astc_encoder *encoder, int kind, const uint8_t *dxt, uint32_t count,
	uint8_t *astc, uint8_t *encode);

/* ---------- the workers and the disk cache (xgpu_astc_cache.c)

Called on one thread, the renderer's, but for the statistics. */

/* the cache's folder (made if it is not there; NULL: none, the encoded
blocks are not kept) and the most bytes it keeps, its least recently used
files deleted past it; the workers start with the first job */
void xgpu_astc_cache_initialize(const char *folder, uint64_t most_bytes);
/* the bytes of the cache's files in folder (another than the cache's own
too, before it is initialized), each deleted with clear: the caches the
player clears (host_caches) */
uint64_t xgpu_astc_cache_files(const char *folder, int clear);

struct xgpu_astc_job;

/* the blocks of a texture whose transcoding leaves some to encode, copied,
to be converted on a worker (owner: the caller's, given back); NULL if the
job cannot be made */
struct xgpu_astc_job *xgpu_astc_submit(int kind, const uint8_t *dxt, uint32_t count, void *owner);
/* the encoded blocks of a texture whose transcoding left left blocks to
encode (astc: its blocks transcoded, encode: their flags), where the
workers have kept them in memory (it was read before, at another address):
1 found, its encoded blocks put in astc; -1 found to be better decoded; 0
not found. The texture's blocks are hashed: a few GB a second. */
int xgpu_astc_remembered(int kind, const uint8_t *dxt, uint32_t count, uint8_t *astc, const uint8_t *encode,
	uint32_t left);
/* a job not wanted any longer: it is freed once it is finished, never
returned by xgpu_astc_finished */
void xgpu_astc_cancel(struct xgpu_astc_job *job);
/* a job finished since the last call (each once; NULL: none), to be freed
by xgpu_astc_job_free */
struct xgpu_astc_job *xgpu_astc_finished(void);
void *xgpu_astc_job_owner(const struct xgpu_astc_job *job);
/* its ASTC blocks, as many as its DXT ones; NULL if it failed */
const uint8_t *xgpu_astc_job_blocks(const struct xgpu_astc_job *job);
void xgpu_astc_job_free(struct xgpu_astc_job *job);
/* waits for every job submitted to finish (the tests', and a backend's at
its end) */
void xgpu_astc_cache_wait(void);
/* the workers stopped and the cache's state let go (the tests') */
void xgpu_astc_cache_shutdown(void);

struct xgpu_astc_statistics
{
	/* jobs finished, those read from the disk cache, textures found in
	memory (xgpu_astc_remembered), blocks encoded, the encoding's seconds,
	bytes written to the cache, files deleted from it */
	uint32_t jobs, cache_hits, memory_hits;
	uint64_t blocks_encoded;
	double encode_seconds;
	uint64_t bytes_written;
	uint32_t evicted;
	/* the cache's bytes on disk now */
	uint64_t cache_bytes;
};
/* the statistics since the start (any thread) */
void xgpu_astc_statistics(struct xgpu_astc_statistics *statistics);

#ifdef __cplusplus
}
#endif

#endif
