/*
FAR_CLIP.H

display.draw_distance (far_clip.c): how far the local players' views draw
the world.
*/

#ifndef FAR_CLIP_H
#define FAR_CLIP_H

/* the far clip distance the local players' views are drawn to (world
units): the game's own (rasterizer_globals.far_clip_distance, 1024 unless a
script or the console changed it), or display.draw_distance's when that is
farther and the game's is not one a script brought nearer */
real render_far_clip_distance(void);

#endif
