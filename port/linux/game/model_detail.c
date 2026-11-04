/*
MODEL_DETAIL.C

The geometry each of a model's permutations draws when display.model_detail
is "native" or "highest" (halo_model_detail_scale, port/linux/src/d3d8_gl.c),
which choose the model's level of detail at sizes the Xbox's 480 lines
rarely or never reached (render_model, source/models/models.c). "stock"
draws each permutation at the model's level, as the Xbox did.

Two kinds of a map's levels are not made for those sizes:
- a permutation may have no geometry at a level, and draw nothing there:
  "highest" would leave the part out at every distance, so it draws its
  most detailed level below that has some;
- a level whose cutoff is over 1000 pixels is a close-up's: a10's
  cryotube's top level, at 3000, is the tube with the Master Chief lying in
  it, which the Xbox reached only with the camera inside the tube (the
  player in it at the level's start) or for the tube a cinematic made its
  movie star (object_get_level_of_detail_pixels, render_objects.c:
  REAL_MAX). It is drawn only when the model's size in the Xbox's 480 lines
  reaches the level, as the Xbox chose it, never because the display's
  lines or "highest" do; the level below otherwise, so that the empty tubes
  stay empty however close the camera comes.
*/

#include "cseries.h"
#include "model_detail.h"
#include "models/model_definitions.h"
#include "rasterizer/rasterizer_console_vars.h"

/* ---------- constants */

enum
{
	/* the cutoff over which a level is a close-up's */
	MODEL_DETAIL_CLOSE_UP_CUTOFF_PIXELS = 1000,
};

/* ---------- prototypes */

#ifdef HALO_VR
/* port/linux/game/vr_render.c */
real vr_render_level_of_detail_scale(void);
#endif

/* ---------- public code */

short model_detail_rules(struct model const *model, real stock_pixels)
{
	if (halo_model_detail_stock() || rasterizer_debug_options.debug_model_lod!=NONE)
	{
		return _model_detail_rules_stock;
	}
#ifdef HALO_VR
	{
		/* the size in the centre view's 480 lines, not the eye's
		(object_get_level_of_detail_pixels) */
		real scale = vr_render_level_of_detail_scale();

		if (scale>1.f)
		{
			stock_pixels /= scale;
		}
	}
#endif
	return model_detail_stock_close_up(model, stock_pixels) ?
		_model_detail_rules_close_up :
		_model_detail_rules_ordinary;
}

boolean model_detail_stock_close_up(struct model const *model, real stock_pixels)
{
	short level;

	for (level = 0; level<(short)NUMBEROF(model->detail_cutoff_pixels); level++)
	{
		if (model->detail_cutoff_pixels[level]>MODEL_DETAIL_CLOSE_UP_CUTOFF_PIXELS)
		{
			/* (render_model's walk keeps a level whose cutoff the size
			reaches; a size that is not a number reaches none) */
			return stock_pixels>=model->detail_cutoff_pixels[level];
		}
	}
	return FALSE;
}

short model_detail_geometry_index(struct model const *model,
	struct model_region_permutation const *permutation, short level, short rules)
{
	if (rules==_model_detail_rules_stock)
	{
		return permutation->geometry_indices[level];
	}
	for (; level>=0; level--)
	{
		if (permutation->geometry_indices[level]!=NONE &&
			(rules==_model_detail_rules_close_up ||
				model->detail_cutoff_pixels[level]<=MODEL_DETAIL_CLOSE_UP_CUTOFF_PIXELS))
		{
			return permutation->geometry_indices[level];
		}
	}
	return NONE;
}
