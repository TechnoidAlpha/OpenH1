/*
MENU_PICTURES.H

The menus' small pictures of the missions and the multiplayer maps (ui.map's
ui\shell\bitmaps\sp_levels and mp_map_grafix, 140x114 in 256x128 frames)
drawn again, sharp, from the player's own maps: a camera's place for each
(port/assets/pictures, embedded by tools/embed_assets.py: never a picture),
rendered once by the game itself, in its own window, under the loading
screen, as a phase of its start between the intro movie and the main menu
("Preparing menu pictures":
port/linux/game/menu_pictures_render.c),
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

/* the phases of the game's start (menu_pictures_startup_next) */
enum
{
	_menu_pictures_startup_intro,
	_menu_pictures_startup_pictures,
	_menu_pictures_startup_done,
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
/* after it, a pose of each stock map the player has (a map made for it, not
modded), whose preparation the pass records where it has none: none in runs
nobody watches, as with the pictures */
long menu_pictures_maps(long *poses, long maximum);
/* runs nobody watches (tests: timed, scripted, driven through the script
console, recorded or measured), and those whose first screen is not the
main menu's: no pass, for the pictures, the maps' preparations or their
distance fields (world_sdf.c) */
int menu_pictures_quiet_run(void);
/* a picture's high-res image, once it is ready, else 0 (the original is
drawn) */
unsigned int menu_pictures_texture(long pose, unsigned long *levels);
/* the original frame's texels (a D3D texture of ui.map's), kept for the
colour curve */
void menu_pictures_keep_original(long pose, void const *texture);
/* a pose's render begun (noted first: a game gone in the middle of it
counts it as failed), failed, or its frame kept (the frame being drawn's,
read back; 0 if that failed; then cut, its colours matched, written and
noted on the pictures' own thread, or failed there) */
void menu_pictures_render_begin(long pose);
void menu_pictures_render_failed(long pose, char const *why);
int menu_pictures_render_kept(long pose);
/* ...or given up on for good (its pose and map's), as one that stopped the
game: not tried again */
void menu_pictures_render_given_up(long pose, char const *why);
/* the kept frames' pictures all made (their files written, drawn): they are
made on a thread of their own as the pass goes on (menu_pictures.c) */
void menu_pictures_render_finished(void);
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
void menu_picture_note_give_up(struct menu_picture_note *note);
int menu_picture_overdue(unsigned long long now, unsigned long long pass_began, unsigned long long picture_began);
long menu_pictures_startup_next(long phase, int main_menu_loaded, int movie_open, int pass_exclusive);
int menu_pictures_startup_holds_main_menu(long phase, int pass_exclusive);
int menu_pictures_phase_exclusive(int running, int ended);
int menu_picture_png_whole(unsigned char const *head, unsigned long head_size, unsigned char const *tail,
	unsigned long tail_size, unsigned long width, unsigned long height);
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
/* the checksum of the map (a10) the game would load, found as it finds its
maps (paths.data, the language's maps folder, any case), and only if its
loader would take it; else 0 (cache_files_map_copyable_checksum) */
unsigned long menu_picture_map_checksum(char const *map);
/* the pass stopped where it is, as the game stops (the error a damaged
disc gives): the picture being made given up, the loading screen given back */
void menu_pictures_abandon(char const *why);
/* the pass begun, as the game's start reaches its phase (main.c's
main_startup_update): TRUE if there are pictures to make, the first begun */
int menu_pictures_begin(void);
/* from then until it asks for the main menu at its end: the pass has the
game to itself (menu_pictures_phase_exclusive) */
int menu_pictures_exclusive(void);
/* each frame of the game's main loop, before it is drawn, while the pass
runs: each picture rendered in turn (its map loaded, the scene set, a frame
drawn and kept), the preparing screen shown meanwhile, then the main menu */
void menu_pictures_frame(int main_menu_loaded);
/* while the pictures are rendered (from the pass begun to the main menu
shown, under the loading screen): the main loop draws only the frames kept
(menu_pictures_frame_drawn) */
int menu_pictures_preparing(void);
/* the start-up pass's seconds this run, to its main menu shown (0: none) */
double menu_pictures_pass_seconds(void);
int menu_pictures_frame_drawn(void);
/* the maps loaded now are the pass's, drawn as they shipped: no mod is
played on them (mods.c), so a picture never shows a mod, and turning mods
on or off leaves the cached pictures right */
int menu_pictures_stock_maps(void);
/* how far a map's preparation recorded in the pass has come (render_enumerate.c) */
void menu_pictures_recording(float fraction);
/* a frame to keep, as it is presented (d3d8_device.c's present: kept, and
not shown); TRUE if it was one */
int menu_pictures_frame_kept(void);
/* a frame kept waits for each texture it draws with (xbox_texture_cache.c) */
int menu_pictures_textures_wait(void);

#endif
