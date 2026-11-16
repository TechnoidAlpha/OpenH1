/*
RENDER_MARKS.H

debug.render_marks (port/linux/game/render_marks.c): the renderer's command
stream told what the draws that follow belong to (port/linux/src/
xgpu_stream.h's MARK records), for the inspector's replay
(port/macos/tools/halo_replay.m --serve): the camera of each window drawn,
and the owner of each run of draws (an object, the structure's shader, an
effect, the HUD, a menu widget, text). The backends draw nothing for them;
with the setting off nothing is sent and each call returns at once.

The kinds are xgpu_stream.h's XGPU_STREAM_MARK_*, in the same order
(kept so by hand: the stream's encoder takes them as its kinds).
*/

#ifndef RENDER_MARKS_H
#define RENDER_MARKS_H

enum
{
	RENDER_MARK_OTHER,
	RENDER_MARK_WORLD_OBJECT,
	RENDER_MARK_FIRST_PERSON,
	RENDER_MARK_BSP,
	RENDER_MARK_SKY,
	RENDER_MARK_DECAL,
	RENDER_MARK_EFFECT,
	RENDER_MARK_PARTICLE,
	RENDER_MARK_CONTRAIL,
	RENDER_MARK_LIGHTNING,
	RENDER_MARK_WEATHER,
	RENDER_MARK_WATER,
	RENDER_MARK_HUD,
	RENDER_MARK_TEXT,
	RENDER_MARK_MENU_WIDGET,
	RENDER_MARK_CINEMATIC_LETTERBOX,
	RENDER_MARK_DEBUG,
	RENDER_MARK_DETAIL_OBJECT,
	RENDER_MARK_LENS_FLARE,
	RENDER_MARK_FOG,
	RENDER_MARK_SCREEN_EFFECT,
	RENDER_MARK_LIGHT_VOLUME,
	RENDER_MARK_SHADOW,
	RENDER_MARK_CLEAR,
	NUMBER_OF_RENDER_MARK_KINDS
};

/* nonzero while the setting is on (read once a frame) */
extern int render_marks_on;

struct render_camera;

/* the frame begun (the setting read; the map and the tick), and a
window's camera (render.c) */
void render_marks_frame_begin(void);
void render_marks_view(short window_index, struct render_camera const *camera);
/* the category drawing (rasterizer.h's _rasterizer_profile_*, -1 none:
rasterizer_profile_begin and _end): every draw's owner until a finer one */
void render_marks_category(int category);
/* a model drawn (models.c's render_model): the object it is (its index, or
NONE), in first person or as a shadow; and each of its parts' shader */
void render_marks_model(long model_index, long object_index, int first_person, int shadow);
void render_marks_model_shader(long shader_index);
/* a structure material's surfaces (structure_render.c) */
void render_marks_structure(long shader_index, short lightmap_index, short material_index);
/* the kind (RENDER_MARK_PARTICLE, ...) and a tag (its bitmap: NONE none)
of the transparent groups made from now on (NONE: none), each group made
(rasterizer_xbox_dynavobgeom.c), and a group drawn
(rasterizer_xbox_transparent_geometry.c) */
void render_marks_next(int kind, long tag_index);
void render_marks_group_made(void const *group);
void render_marks_transparent(void const *group, unsigned long geometry_flags, long object_index, void const *shader,
	int first_person, int sky, int water);
/* a menu widget drawn (ui_widget.c): its definition, its instance, its
background bitmap */
void render_marks_widget(long definition_index, void const *widget, long bitmap_index);
/* a HUD interface (its tag: NONE none) or a part of the HUD */
void render_marks_hud(long tag_index, char const *label);
/* draws of a kind of their own (the letterbox, the debug drawing) */
void render_marks_kind(int kind, char const *label);
/* text drawn (draw_string.c), and done */
void render_marks_text(char const *string);
void render_marks_unicode_text(unsigned short const *string);
void render_marks_text_end(void);

#endif
