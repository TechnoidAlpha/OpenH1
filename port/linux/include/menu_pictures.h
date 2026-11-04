/*
MENU_PICTURES.H

The menus' small pictures of the missions and the multiplayer maps (ui.map's
ui\shell\bitmaps\sp_levels and mp_map_grafix, 140x114 in 256x128 frames)
drawn again, sharp, from the player's own maps: a camera's place for each
(port/assets/pictures, embedded by tools/embed_assets.py: never a picture),
rendered once by the game itself, in its own window, as the main menu first
shows ("Preparing menu pictures": port/linux/game/menu_pictures_render.c),
kept in the data folder, and drawn in place of the original frame
(port/linux/src/menu_pictures.c). Any picture that cannot be (its map
missing or not the one the pose was made for, a render that failed twice)
stays the original.
*/

#ifndef MENU_PICTURES_H
#define MENU_PICTURES_H

/* ---------- constants */

enum
{
	/* the pictures' frames in ui.map: the picture in the top left of each */
	MENU_PICTURE_FRAME_WIDTH = 256,
	MENU_PICTURE_FRAME_HEIGHT = 128,
	MENU_PICTURE_WIDTH = 140,
	MENU_PICTURE_HEIGHT = 114,
	/* how many times larger than the original a picture is drawn: from the
	least to the most (a 4K screen's largest card) */
	MENU_PICTURE_SMALLEST_SCALE = 2,
	MENU_PICTURE_LARGEST_SCALE = 6,
	MAXIMUM_MENU_PICTURE_CHECKSUMS = 4,
	/* the most poses */
	MAXIMUM_MENU_PICTURES = 64,
};

/* what to do with a picture (menu_picture_decide) */
enum
{
	/* the cached picture is drawn */
	_menu_picture_use_bit = 0,
	/* it is rendered (again) */
	_menu_picture_render_bit,
};

/* a cached picture's state, as its note in the cache says (menu_picture_note) */
enum menu_picture_result
{
	_menu_picture_none = 0,
	_menu_picture_rendered,
	_menu_picture_failed,
	_menu_picture_modded,
};

/* ---------- structures */

/* a picture's pose (port/assets/pictures/<name>.json) */
struct menu_picture_pose
{
	char const *name; /* sp_levels_00: the bitmap's name and the frame's index */
	char const *bitmap; /* ui\shell\bitmaps\sp_levels */
	long frame;
	char const *map; /* a10: maps\a10.map */
	char const *scenario; /* levels\a10\a10 */
	long bsp;
	/* the picture's shape before it was squeezed to 140x114 */
	float aspect;
	/* its colours taken toward the original's (rr_final.py's curve) */
	int colour_curve;
	/* how long the world runs for the objects placed to settle (a vehicle
	comes to rest, a rider takes its seat), in milliseconds; 0: left as
	placed (settle_seconds) */
	long settle_milliseconds;
	/* debug_camera_set's argument */
	char const *camera;
	/* console lines, each ended by a new line: the pose's own, then its
	objects' (debug_object_place) */
	char const *commands;
	/* the map's cache file checksums it was made for */
	unsigned long checksums[MAXIMUM_MENU_PICTURE_CHECKSUMS];
	long checksum_count;
	/* the pose file's CRC-32 */
	unsigned long hash;
};

/* a cached picture's note: what was last rendered for what, and the
renders tried since that did not finish (a failure, or the game gone in
the middle of one: noted before it starts) */
struct menu_picture_note
{
	enum menu_picture_result result;
	unsigned long pose_hash;
	unsigned long map_checksum;
	long scale;
	long failures;
};

/* ---------- globals */

/* (tools/embed_assets.py) */
extern const struct menu_picture_pose menu_picture_poses[];
extern const unsigned int menu_picture_pose_count;

/* ---------- prototypes (port/linux/src/menu_pictures.c) */

/* the poses looked at in the main menu (their maps' checksums, their
notes): those to render this run, in the order they are rendered, into
poses; how many. None if nothing is to be rendered, or in runs nobody
watches, or with display.menu_pictures off */
long menu_pictures_wanted(long *poses, long maximum);
/* a picture's high-res image, once it is ready, else 0 (the original is
drawn) */
unsigned int menu_pictures_texture(long pose, unsigned long *levels);
/* the original frame's texels (a D3D texture of ui.map's), kept for the
colour curve */
void menu_pictures_keep_original(long pose, void const *texture);
/* a pose's render begun (noted first: a game gone in the middle of it
counts it as failed), failed, or its frame kept (the frame being drawn's, so far: cut, its
colours matched, written and noted; 0 if that failed) */
void menu_pictures_render_begin(long pose);
void menu_pictures_render_failed(long pose, char const *why);
int menu_pictures_render_kept(long pose);
/* the scale the pictures are rendered at (menu_picture_scale_for_height) */
long menu_pictures_scale(void);

/* the pure parts (tools/harness/tests/menu_pictures.c) */
long menu_picture_scale_for_height(long screen_height);
unsigned long menu_picture_decide(struct menu_picture_pose const *pose, unsigned long map_checksum,
	struct menu_picture_note const *note, long wanted_scale);
long menu_picture_order(struct menu_picture_pose const *poses, long count, unsigned long const *decisions,
	long *order, long maximum);
void menu_picture_note_begin(struct menu_picture_note *note, struct menu_picture_pose const *pose,
	unsigned long map_checksum, long scale);
void menu_picture_note_end(struct menu_picture_note *note, enum menu_picture_result result, long scale);
int menu_picture_note_parse(char const *text, struct menu_picture_note *note);
int menu_picture_note_format(struct menu_picture_note const *note, char *text, long size);
void menu_picture_colour_curve(unsigned char *pixels, unsigned long count, unsigned char const *reference,
	unsigned long reference_count);
int menu_picture_cut(unsigned char const *bgra, unsigned long frame_width, unsigned long frame_height, float aspect,
	unsigned char *rgba, unsigned long width, unsigned long height);
void menu_picture_frame(unsigned char const *picture, unsigned long width, unsigned long height, unsigned char *frame,
	unsigned long frame_width, unsigned long frame_height);

/* ---------- prototypes (port/linux/game/menu_pictures_render.c) */

/* the pose whose original frame is this texture's, or -1 */
long menu_picture_at(unsigned long address, long width, long height);
/* each frame of the game's main loop, before it is drawn: in the main menu
first shown, the pictures to render looked for, and if there are any, each
rendered in turn (its map loaded, the scene set, a frame drawn and kept),
the preparing screen shown meanwhile, then the main menu again */
void menu_pictures_frame(int main_menu_loaded);
/* while the pictures are rendered (from the main menu first shown to the
main menu again): the main loop draws the preparing screen alone
(menu_pictures_screen_only), the game's own frame when one is kept, or
nothing (menu_pictures_frame_drawn) */
int menu_pictures_preparing(void);
int menu_pictures_frame_drawn(void);
int menu_pictures_screen_only(void);
/* the preparing screen, drawn over whatever the frame has (the 640x480
screen's 2D pass) */
void menu_pictures_screen_draw(void);
/* a frame to keep, as it is presented (d3d8_device.c's present: kept, and
not shown); TRUE if it was one */
int menu_pictures_frame_kept(void);
/* a frame kept waits for each texture it draws with (xbox_texture_cache.c) */
int menu_pictures_textures_wait(void);

#endif
