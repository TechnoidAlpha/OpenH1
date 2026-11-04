/*
MODEL_DETAIL.H

The geometry each of a model's permutations draws at the level of detail
display.model_detail chose for the model (model_detail.c).
*/

#ifndef MODEL_DETAIL_H
#define MODEL_DETAIL_H

#include "math/real_math.h"

struct model;
struct model_region_permutation;

/* how a model's permutations take its level (model_detail_rules) */
enum
{
	/* each the model's level, as the Xbox did ("stock", or rasterizer_debug_model_lod) */
	_model_detail_rules_stock,
	/* each its most detailed level at or below the model's that has geometry,
	not a close-up's */
	_model_detail_rules_ordinary,
	/* the same, a close-up's included: the model is as large in the Xbox's
	480 lines as its first close-up level asks */
	_model_detail_rules_close_up,
};

/* the rules for a model drawn this frame, its size in the Xbox's 480 lines
(render_model's level_of_detail_pixels) */
short model_detail_rules(struct model const *model, real stock_pixels);
/* whether that size reaches the model's first close-up level (a cutoff over
1000 pixels) as the Xbox chose levels: FALSE for a model without one, and
for a size that is not a number */
boolean model_detail_stock_close_up(struct model const *model, real stock_pixels);
/* the geometry a permutation draws for the model's level, NONE for none */
short model_detail_geometry_index(struct model const *model,
	struct model_region_permutation const *permutation, short level, short rules);

#endif
