/*
XGPU_COVERAGE.H

The share of a texture's texels an alpha test draws, for code that cannot
include xgpu.h (the game's: world_sdf.c, whose objects' occlusion fields
take an alpha-tested leaf's card as blocking that share of what crosses it).
Plain types only. Defined in xbox_texture_decode.c.
*/

#ifndef XGPU_COVERAGE_H
#define XGPU_COVERAGE_H

/* the share of a 2D texture's texels (its D3D Format and Size words, its
texels at texels, levels after one another as they lie) whose alpha is over
reference, at its first level no larger than largest each way; 1 for one
that cannot be read (a cube map, a volume, a format not known, no memory):
taken as drawn whole */
float xgpu_texture_alpha_coverage(unsigned int format_word, unsigned int size_word, const unsigned char *texels,
	unsigned int reference, unsigned long largest);

#endif
