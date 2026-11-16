/*
FAR_CLIP.H

display.draw_distance (far_clip.c): how far the local players' views draw
the world.
*/

#ifndef FAR_CLIP_H
#define FAR_CLIP_H

/* the far clip distance a local player's view is drawn to (world units;
their own display.draw_distance, their profile's: player_settings.c; NONE:
config.toml's): the game's own (rasterizer_globals.far_clip_distance, 1024 unless a
script or the console changed it), or display.draw_distance's when that is
farther and the game's is not one a script brought nearer */
real render_far_clip_distance(short local_player_index);

#endif
