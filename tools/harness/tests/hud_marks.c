#include "harness.h"
#include <math.h>
#include <wchar.h>

/* A fake view for the HUD's marks over the world: a waypoint and its distance (hud_nav_points.c's
custom_render_nav_point: a scenario's flag, an object, a game engine goal, Basic Training's spots), a teammate's
triangle and a player's name (hud.c's hud_draw_friendly_indicator, hud_draw_player_name), a ping's name over its arrow
(network_markers.c's markers_draw_name) and the passenger's reticle
(passenger_camera.c's passenger_camera_reticle_offset), each drawn through the HUD's screen quads and text
(rasterizer.c's rasterizer_psuedo_dynamic_screen_quad_draw, rasterizer_text.c's character quads). The camera turns
and strafes past a point 40 world units away, a frame at a time at 60, 120 and 144 frames a second, in the game's
852x480 screen: each frame, what is drawn of the mark is where the point projects through that frame's camera,
not the whole unit short of it (several pixels of the display's: the mark stepped as the world under it moved
smoothly), and the distance under a waypoint is the meters to the nearest tenth. */

typedef unsigned short word;
typedef unsigned long pixel32;
typedef union { real n[2]; struct { real x, y; }; struct { real u, v; }; } real_point2d;
typedef struct { real i, j; } real_vector2d;
typedef struct { short x, y; } point2d;
typedef struct { short x0, y0, x1, y1; } rectangle2d;
typedef struct { real x0, x1, y0, y1; } real_rectangle2d;
typedef struct { real red, green, blue; } real_rgb_color;
typedef struct { real alpha, red, green, blue; } real_argb_color;
struct tag_reference { long group_tag; long index; };
struct tag_block { long count; void *address; void *definition; };
#define TAG_BLOCK_GET_ELEMENT(block, element, type) (&((type *)(block)->address)[(element)])
#define NUMBEROF(array) (sizeof(array) / sizeof(array[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define SHORT_MAX 32767
#define csmemset memset
#define STACK_BUFFER_LENGTH 4
#define match_vassert(file, line, condition, message) ((void)0)
#define match_assert_stack_frame(file, line) ((void)0)
static char temporary[256];
static long get_return_eip(void) { return 0; }
static short check_stack_buffer(long *buffer) { (void)buffer; return NONE; }
static char const *csprintf(char *buffer, char const *format, ...) { (void)format; return buffer; }
static real square_root(real x) { return sqrtf(x); }
static real arctangent(real y, real x) { return atan2f(y, x); }
static real sine(real x) { return sinf(x); }
static real cosine(real x) { return cosf(x); }
static real power(real x, real y) { return powf(x, y); }
/* mcc_hud_draw.c's: a stock map's bitmaps are at their own scale */
static real mcc_hud_bitmap_scale(long bitmap_group_index) { (void)bitmap_group_index; return 1.0f; }
static long fast_ftol(real value) { return lrintf(value); }
static long fast_ftol_C(real value) { return lrintf(value); }
static real distance3d(real_point3d const *a, real_point3d const *b)
{
	return sqrtf((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y) + (a->z - b->z) * (a->z - b->z));
}
enum { _hud_anchor_top_left, _hud_anchor_top_right, _hud_anchor_bottom_left, _hud_anchor_bottom_right,
	_hud_anchor_center };
enum { NUMBER_OF_POINTS_PER_RECTANGLE = 4, NUMBER_OF_VERTICES_PER_QUADRILATERAL = 4 };
enum { _shader_framebuffer_blend_function_alpha_multiply_add = 1 };
enum { _interface_bitmap_multiplayer_hud = 7 };

/* the camera: its place and its yaw (level), the frame's view drawn (render.c's render.camera, render.frustum: the
world to the view as a matrix of the camera's own here) */
typedef struct { real_point3d position; real yaw; } real_matrix4x3;
struct render_camera { real_point3d position; rectangle2d viewport_bounds, window_bounds; };
struct render_frustum { real_matrix4x3 world_to_view; real z_near, z_far; };
static struct { struct render_camera camera; struct render_frustum frustum; short local_player_index; } render;
static struct { real far_clip_distance; } rasterizer_globals = { 1024.0f };
/* (the view's tangent across half its height) */
#define TANGENT_Y 0.6f
#define SCREEN_WIDTH 852
#define SCREEN_HEIGHT 480

/* the world point in the view: x right, y up, z back toward the camera */
static void matrix4x3_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *result)
{
	real dx = p->x - m->position.x, dy = p->y - m->position.y, dz = p->z - m->position.z;
	real c = cosf(m->yaw), s = sinf(m->yaw);
	real_point3d r;

	r.x = dx * s - dy * c;
	r.y = dz;
	r.z = -(dx * c + dy * s);
	*result = r;
}

/* render_cameras.c's: the view point on the screen (the viewport's units, from its corner), FALSE off it */
static boolean render_camera_view_to_screen(struct render_camera const *camera, struct render_frustum const *frustum,
	real_point3d const *view, real_point2d *screen)
{
	real width = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	real height = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real x, y;

	(void)frustum;
	if (view->z >= 0.0f)
		return FALSE;
	x = view->x / (-view->z * TANGENT_Y * width / height);
	y = -view->y / (-view->z * TANGENT_Y);
	if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f)
		return FALSE;
	screen->x = width * (x + 1.0f) * 0.5f + camera->viewport_bounds.x0;
	screen->y = height * (y + 1.0f) * 0.5f + camera->viewport_bounds.y0;
	return TRUE;
}
static void panini_view_direction(real_point3d const *view_point, real *x, real *y)
{
	(void)view_point; (void)x; (void)y;
}

/* ---------- what is drawn */

struct bitmap_data { short width, height; point2d registration_point; };
struct dynamic_screen_vertex { real_point2d position; real_point2d texture_coordinates; pixel32 color; };
struct rasterizer_dynamic_screen_geometry_parameters
{
	void *meter_parameters;
	struct bitmap_data *map[3];
	real_vector2d map_scale[3];
	real_vector2d map_texture_scale[3];
	short framebuffer_blend_function;
	boolean point_sampled;
};
/* the quads drawn, as the device is given them */
#define MAXIMUM_QUADS 16
static struct dynamic_screen_vertex quads[MAXIMUM_QUADS][4];
static short quad_count;
static void _rasterizer_psuedo_dynamic_screen_quad_draw(struct rasterizer_dynamic_screen_geometry_parameters *parameters,
	struct dynamic_screen_vertex *vertices)
{
	(void)parameters;
	CHECK(quad_count < MAXIMUM_QUADS, "too many quads");
	memcpy(quads[quad_count++], vertices, sizeof(quads[0]));
}
/* the text's characters, as the device is given them */
static struct dynamic_screen_vertex characters[MAXIMUM_QUADS][4];
static short character_count;
static void rasterizer_text_draw_character(struct dynamic_screen_vertex const *vertices)
{
	CHECK(character_count < MAXIMUM_QUADS, "too many characters");
	memcpy(characters[character_count++], vertices, sizeof(characters[0]));
}
static real text_scale = 1.0f;
static real text_scale_origin_x = 0.0f;
static real text_scale_origin_y = 0.0f;

#include "decls.inc"

static void pixel32_to_real_rgb_color(pixel32 color, real_rgb_color *rgb)
{
	(void)color;
	rgb->red = rgb->green = rgb->blue = 1.0f;
}
static pixel32 real_rgb_color_to_pixel32(real_rgb_color const *rgb) { (void)rgb; return 0x00FFFFFF; }

/* the waypoint's arrow, 16 units square about its middle, and the triangle, 12 wide and 10 high, hung from its
bottom's middle */
static struct bitmap_data arrow_bitmap = { 16, 16, { 8, 8 } };
static struct bitmap_data triangle_bitmap = { 12, 10, { 6, 10 } };
static real_rectangle2d const whole_clip = { 0.0f, 1.0f, 0.0f, 1.0f };
static struct hud_waypoint_arrow arrows[1];
static struct
{
	struct { real top_offset, bottom_offset, left_offset, right_offset; struct tag_reference arrow_bitmap;
		struct tag_block arrows; } waypoint;
} hud_globals_data, *hud_globals = &hud_globals_data;

static void hud_retrieve_bitmap_and_bounding_rect(long bitmap_index, short sequence_index, short frame_index,
	struct bitmap_data const **bitmap, real_rectangle2d const **clip)
{
	(void)bitmap_index; (void)sequence_index; (void)frame_index;
	*bitmap = &arrow_bitmap;
	*clip = &whole_clip;
}
static boolean _texture_cache_bitmap_get_hardware_format(struct bitmap_data *bitmap, boolean a, boolean b)
{
	(void)bitmap; (void)a; (void)b;
	return TRUE;
}
/* hud_draw.c's, about the point (the anchor's middle), its offsets whole */
static void hud_draw_bitmap_direct(struct bitmap_data const *bitmap, short placement, point2d const *point,
	real_rectangle2d const *clip, real scale, real theta, pixel32 color, boolean is_interface_bitmap)
{
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	struct dynamic_screen_vertex vertices[4];
	short index;

	(void)placement; (void)clip; (void)theta; (void)color; (void)is_interface_bitmap;
	memset(&parameters, 0, sizeof(parameters));
	memset(vertices, 0, sizeof(vertices));
	for (index = 0; index < 4; index++)
	{
		real x = ((index + 1) & 2) ? bitmap->width * 0.5f : -bitmap->width * 0.5f;
		real y = index > 1 ? bitmap->height * 0.5f : -bitmap->height * 0.5f;

		vertices[index].position.x = (real)(point->x + fast_ftol(x * scale));
		vertices[index].position.y = (real)(point->y + fast_ftol(y * scale));
	}
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}
/* the distance under it: the number drawn (hud_draw.c's), a quad at its place */
static long shown_value, shown_decimal;
static point2d shown_at;
static void hud_draw_numbers(short local_player_index, struct hud_absolute_placement_definition const *absolute,
	struct number_hud_element_definition const *numbers, short value, short decimal_value, short draw_flags,
	long flash_reference_time, real override_scale)
{
	struct dynamic_screen_vertex vertices[4];
	short index;

	(void)local_player_index; (void)absolute; (void)draw_flags; (void)flash_reference_time; (void)override_scale;
	shown_value = value;
	shown_decimal = decimal_value;
	shown_at = numbers->placement.offset;
	memset(vertices, 0, sizeof(vertices));
	for (index = 0; index < 4; index++)
	{
		vertices[index].position.x = (real)(numbers->placement.offset.x + (((index + 1) & 2) ? 20 : 0));
		vertices[index].position.y = (real)(numbers->placement.offset.y + (index > 1 ? 8 : 0));
	}
	rasterizer_psuedo_dynamic_screen_quad_draw(NULL, vertices);
}

/* the players: the local one (0), a teammate (1), each's unit (its head where the frame draws it) */
struct player_datum { long unit_index; wchar_t name[12]; struct { long machine_index; } network_player_data; };
static struct player_datum players[2] = { { 0x10001, { 0 }, { 0 } }, { 0x20002, { 'K', 'e', 'y', 'e', 's' }, { 1 } } };
static real_point3d head;
struct unit_datum { struct { real active_camouflage; } unit; };
static struct unit_datum unit_data;
static struct player_datum *player_get(long player_index) { return &players[player_index]; }
static long local_player_get_player_index(short local_player_index) { (void)local_player_index; return 0; }
static struct unit_datum *unit_get(long unit_index) { (void)unit_index; return &unit_data; }
static void unit_get_head_position(long unit_index, real_point3d *position) { (void)unit_index; *position = head; }
/* where the waypoint's distance is from: the view drawn (render_interpolation.c's) */
static void render_interpolation_unit_camera_position(long unit_index, real_point3d *position)
{
	(void)unit_index;
	*position = render.camera.position;
}
static struct bitmap_data *bitmap_group_get_bitmap_from_sequence(long group, short sequence, short frame)
{
	(void)group; (void)sequence; (void)frame;
	return &triangle_bitmap;
}
static long interface_get_tag_index(short index) { return index; }

/* a player's name: its font, laid out centred in its bounds, a character quad 6 units wide as tall as the font */
struct font_header { short ascending_height, descending_height; };
static struct font_header font = { 9, 3 };
static long hud_get_font_index(void) { return 1; }
static struct font_header *font_definition_get(long font_index) { (void)font_index; return &font; }
static boolean hud_player_name_in_sight(long unit_index, real_point3d const *point)
{
	(void)unit_index; (void)point;
	return TRUE;
}
static void hud_player_name_color(boolean ally, real_argb_color *color)
{
	(void)ally;
	color->alpha = color->red = color->green = color->blue = 1.0f;
}
static real hud_player_name_scale(void) { return 1.5f; }
static void draw_string_set_draw_mode(long font_index, short a, short b, short c, real_argb_color const *color)
{
	(void)font_index; (void)a; (void)b; (void)c; (void)color;
}
static void rasterizer_text_draw_scaled_character(struct dynamic_screen_vertex const *vertices);
static void rasterizer_draw_unicode_string(rectangle2d const *bounds, void *a, void *b, long c, wchar_t const *string)
{
	struct dynamic_screen_vertex vertices[4];
	short middle = (short)((bounds->x0 + bounds->x1) / 2);
	short index;

	(void)a; (void)b; (void)c; (void)string;
	memset(vertices, 0, sizeof(vertices));
	for (index = 0; index < 4; index++)
	{
		vertices[index].position.x = (real)(middle + (((index + 1) & 2) ? 3 : -3));
		vertices[index].position.y = (real)(index > 1 ? bounds->y1 : bounds->y0);
	}
	rasterizer_text_draw_scaled_character(vertices);
}
static boolean network_voice_machine_speaking(long machine_index) { (void)machine_index; return FALSE; }
static void draw_unicode_string_compute_bounds(rectangle2d const *bounds, wchar_t const *string, rectangle2d *text,
	rectangle2d *cursor)
{
	(void)string;
	*text = *bounds;
	*cursor = *bounds;
}
static short draw_unicode_string_capital_middle(rectangle2d const *bounds, wchar_t const *string)
{
	(void)string;
	return bounds->y0;
}
static void network_voice_draw_icon(rectangle2d const *bounds, boolean muted, real alpha)
{
	(void)bounds; (void)muted; (void)alpha;
}

#include "under_test.inc"

/* ---------- the checks */

#define PIXELS_PER_UNIT (1664.0 / SCREEN_HEIGHT) /* (a 13-inch MacBook Air's display: 1664 lines for the 480) */
#define WORST 0.01 /* units: a thirtieth of the display's pixel */

static void camera_at(real x, real y, real yaw)
{
	render.camera.position.x = x;
	render.camera.position.y = y;
	render.camera.position.z = 0.6f;
	render.frustum.world_to_view.position = render.camera.position;
	render.frustum.world_to_view.yaw = yaw;
	render.frustum.z_near = 0.0625f;
	render.frustum.z_far = 1024.0f;
}

/* where the point is on the screen through the frame's camera, worked out on its own (in double) */
static boolean projected(real_point3d const *point, double *x, double *y)
{
	double dx = point->x - render.camera.position.x, dy = point->y - render.camera.position.y;
	double dz = point->z - render.camera.position.z;
	double c = cos(render.frustum.world_to_view.yaw), s = sin(render.frustum.world_to_view.yaw);
	double along = dx * c + dy * s, right = dx * s - dy * c;
	double aspect = (double)SCREEN_WIDTH / SCREEN_HEIGHT;

	if (along <= 0.0)
		return FALSE;
	*x = SCREEN_WIDTH * (right / (along * TANGENT_Y * aspect) + 1.0) * 0.5;
	*y = SCREEN_HEIGHT * (-dz / (along * TANGENT_Y) + 1.0) * 0.5;
	return fabs(right / (along * TANGENT_Y * aspect)) < 0.9 && fabs(dz / (along * TANGENT_Y)) < 0.9;
}

static void quad_middle(struct dynamic_screen_vertex const *quad, double *x, double *y)
{
	*x = (quad[0].position.x + quad[1].position.x + quad[2].position.x + quad[3].position.x) / 4.0;
	*y = (quad[0].position.y + quad[1].position.y + quad[2].position.y + quad[3].position.y) / 4.0;
}

static void begin_frame(void)
{
	quad_count = 0;
	character_count = 0;
}

/* the frame's camera, a camera turning (40 degrees a second) for a second and then strafing (2 world units a
second) for one, at that many frames a second; the frame's time */
static void scripted_camera(long frame, long rate)
{
	double t = (double)frame / rate;
	double turned = t < 1.0 ? t : 1.0;
	double strafed = t > 1.0 ? t - 1.0 : 0.0;
	real yaw = (real)(-0.35 + turned * 40.0 * M_PI / 180.0);

	camera_at((real)(-strafed * 2.0 * sin(yaw)), (real)(strafed * 2.0 * cos(yaw)), yaw);
}

static real_point3d const mark = { 40.0f, 0.0f, 1.2f };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static long const rates[] = { 60, 120, 144 };

	render.camera.viewport_bounds.x1 = render.camera.window_bounds.x1 = SCREEN_WIDTH;
	render.camera.viewport_bounds.y1 = render.camera.window_bounds.y1 = SCREEN_HEIGHT;
	hud_globals->waypoint.arrows.count = 1;
	hud_globals->waypoint.arrows.address = arrows;
	hud_globals->waypoint.arrow_bitmap.index = 1;
	hud_globals->waypoint.top_offset = hud_globals->waypoint.bottom_offset = 20.0f;
	hud_globals->waypoint.left_offset = hud_globals->waypoint.right_offset = 20.0f;
	arrows[0].opacity = 1.0f;

	/* a waypoint, each frame: its arrow's middle where the point projects, its distance's number moved with it,
	and nothing else drawn moved after it */
	CASE("waypoint")
	{
		short rate_index;

		for (rate_index = 0; rate_index < (short)NUMBEROF(rates); rate_index++)
		{
			long frame, frames = 0;
			double worst = 0.0;

			for (frame = 0; frame < 2 * rates[rate_index]; frame++)
			{
				double x, y, arrow_x, arrow_y, number_x, number_y;
				real_vector2d offset;

				scripted_camera(frame, rates[rate_index]);
				if (!projected(&mark, &x, &y))
					continue;
				begin_frame();
				custom_render_nav_point(0, &mark, 0, _waypoint_on_screen);
				CHECK(quad_count == 2, "%d quads drawn, not the arrow and its number", quad_count);
				quad_middle(quads[0], &arrow_x, &arrow_y);
				worst = MAX(worst, MAX(fabs(arrow_x - x), fabs(arrow_y - y)));
				/* (the number where it is placed from the arrow's whole units, moved on as the arrow is) */
				quad_middle(quads[1], &number_x, &number_y);
				number_x -= 10.0 + shown_at.x + arrow_x;
				number_y -= 4.0 + shown_at.y + arrow_y;
				CHECK(fabs(number_x - floor(number_x + 0.5)) < 1e-4 && fabs(number_y - floor(number_y + 0.5)) < 1e-4,
					"the number not moved with the arrow (%f %f of a unit apart)", number_x, number_y);
				rasterizer_screen_offset_get(&offset);
				CHECK(offset.i == 0.0f && offset.j == 0.0f, "the rest of the HUD moved after it (%f %f)",
					offset.i, offset.j);
				frames++;
			}
			CHECK(frames > rates[rate_index], "the point on the screen in only %ld frames", frames);
			CHECK(worst < WORST, "at %ld frames a second the arrow %.3f units (%.2f of the display's pixels) off its "
				"point", rates[rate_index], worst, worst * PIXELS_PER_UNIT);
			printf("%ld Hz: %ld frames, the arrow at most %.5f units off its point\n", rates[rate_index], frames,
				worst);
		}
		return 0;
	}

	/* the number under a waypoint: the meters (3.048 a world unit) to the nearest tenth, the whole meters and the
	tenth of the same number */
	CASE("distance")
	{
		static real const meters[] = { 108.24f, 108.26f, 108.44f, 108.56f, 108.64f, 108.96f, 109.04f, 12.5f, 99.97f };
		short index;

		camera_at(0.0f, 0.0f, 0.0f);
		for (index = 0; index < (short)NUMBEROF(meters); index++)
		{
			real_point3d point = { meters[index] / 3.048f, 0.0f, 0.6f };
			long tenths = lrint(floor((double)meters[index] * 10.0 + 0.5));

			begin_frame();
			custom_render_nav_point(0, &point, 0, _waypoint_on_screen);
			CHECK(shown_value == tenths / 10 && shown_decimal == tenths % 10 * 1000,
				"%.2f m shown as %ld.%ld, not %ld.%ld", meters[index], shown_value, shown_decimal / 1000,
				tenths / 10, tenths % 10);
		}
		return 0;
	}

	/* a teammate's triangle, each frame: hung from its bottom's middle where the point over the head projects */
	CASE("triangle")
	{
		short rate_index;

		for (rate_index = 0; rate_index < (short)NUMBEROF(rates); rate_index++)
		{
			long frame;
			double worst = 0.0;

			for (frame = 0; frame < 2 * rates[rate_index]; frame++)
			{
				real_point3d above;
				double x, y;
				real_vector2d offset;

				scripted_camera(frame, rates[rate_index]);
				head = mark;
				above = head;
				above.z += 0.30000001f;
				if (!projected(&above, &x, &y))
					continue;
				begin_frame();
				hud_draw_friendly_indicator(1);
				CHECK(quad_count == 1, "%d quads drawn, not the triangle", quad_count);
				/* (its top's middle less the top's place: the clip's top part of the bitmap taken off by the fade) */
				worst = MAX(worst, fabs((quads[0][0].position.x + quads[0][1].position.x) * 0.5 - x));
				worst = MAX(worst, fabs(quads[0][2].position.y - y));
				rasterizer_screen_offset_get(&offset);
				CHECK(offset.i == 0.0f && offset.j == 0.0f, "the rest of the HUD moved after it");
			}
			CHECK(worst < WORST, "at %ld frames a second the triangle %.3f units (%.2f pixels) off its point",
				rates[rate_index], worst, worst * PIXELS_PER_UNIT);
		}
		return 0;
	}

	/* a player's name, each frame: its text's middle where the point over the head projects, its bottom 2 units
	above it */
	CASE("name")
	{
		short rate_index;

		for (rate_index = 0; rate_index < (short)NUMBEROF(rates); rate_index++)
		{
			long frame;
			double worst = 0.0;

			for (frame = 0; frame < 2 * rates[rate_index]; frame++)
			{
				real_point3d above;
				double x, y, text_x, text_y;
				real_vector2d offset;

				scripted_camera(frame, rates[rate_index]);
				head = mark;
				above = head;
				above.z += 0.30000001f;
				if (!projected(&above, &x, &y))
					continue;
				begin_frame();
				hud_draw_player_name(1, TRUE, FALSE, 100.0f, 1.0f);
				CHECK(character_count == 1, "%d characters drawn", character_count);
				quad_middle(characters[0], &text_x, &text_y);
				worst = MAX(worst, fabs(text_x - x));
				worst = MAX(worst, fabs(characters[0][2].position.y - (y - 2.0)));
				rasterizer_screen_offset_get(&offset);
				CHECK(offset.i == 0.0f && offset.j == 0.0f, "the rest of the HUD moved after it");
				CHECK(text_scale == 1.0f, "the text left scaled");
			}
			CHECK(worst < WORST, "at %ld frames a second the name %.3f units (%.2f pixels) off its point",
				rates[rate_index], worst, worst * PIXELS_PER_UNIT);
		}
		return 0;
	}

	/* a ping's name (network_markers.c), each frame: its text's middle over its arrow (drawn where the point
	projects), its bottom 2 units above the arrow's top */
	CASE("ping_name")
	{
		static wchar_t const name[] = { 'K', 'e', 'y', 'e', 's', 0 };
		real_rgb_color const color = { 1.0f, 0.5f, 0.25f };
		short rate_index;

		for (rate_index = 0; rate_index < (short)NUMBEROF(rates); rate_index++)
		{
			long frame, frames = 0;
			double worst = 0.0;

			for (frame = 0; frame < 2 * rates[rate_index]; frame++)
			{
				double x, y, text_x, text_y;
				real_point2d drawn;
				real half_height;
				short drawn_type;
				real_vector2d offset;

				scripted_camera(frame, rates[rate_index]);
				if (!projected(&mark, &x, &y))
					continue;
				begin_frame();
				CHECK(hud_render_nav_point_colored(0, &mark, 0, _waypoint_on_screen, &color, 1.0f, &drawn,
					&half_height, &drawn_type), "the arrow not drawn");
				markers_draw_name(1, name, &color, 1.0f, &drawn, half_height);
				CHECK(character_count == 1, "%d characters drawn", character_count);
				quad_middle(characters[0], &text_x, &text_y);
				worst = MAX(worst, fabs(text_x - x));
				worst = MAX(worst, fabs(characters[0][2].position.y - (y - half_height - 2.0)));
				rasterizer_screen_offset_get(&offset);
				CHECK(offset.i == 0.0f && offset.j == 0.0f, "the rest of the HUD moved after it");
				CHECK(text_scale == 1.0f, "the text left scaled");
				frames++;
			}
			CHECK(frames > rates[rate_index], "the point on the screen in only %ld frames", frames);
			CHECK(worst < WORST, "at %ld frames a second the ping's name %.3f units (%.2f pixels) off its arrow",
				rates[rate_index], worst, worst * PIXELS_PER_UNIT);
		}
		return 0;
	}

	/* the passenger's reticle (hud_draw.c scales it about the middle by the reticle's scale, then moves it by the
	rest): where the shot projects, offsets of every part of a unit */
	CASE("passenger_reticle")
	{
		static real const scales[] = { 1.0f, 0.8f, 1.25f };
		short scale_index;
		long step;

		reticle_active = TRUE;
		for (scale_index = 0; scale_index < (short)NUMBEROF(scales); scale_index++)
		{
			for (step = 0; step < 200; step++)
			{
				real scale = scales[scale_index];
				point2d point = { 426, 240 };
				real_vector2d rest;
				double drawn_x, drawn_y;

				reticle_offset.x = -37.0f + step * 0.371f;
				reticle_offset.y = 11.0f - step * 0.129f;
				passenger_camera_reticle_offset(&point, scale, &rest);
				/* (where hud_draw.c draws its middle: scaled about the screen's middle, moved on by the rest) */
				drawn_x = 426.0 + (point.x - 426.0) * scale + rest.i;
				drawn_y = 240.0 + (point.y - 240.0) * scale + rest.j;
				CHECK(fabs(drawn_x - (426.0 + reticle_offset.x)) < 1e-3 &&
					fabs(drawn_y - (240.0 + reticle_offset.y)) < 1e-3,
					"the reticle at (%f %f), the shot at (%f %f), at a scale of %f", drawn_x, drawn_y,
					426.0 + reticle_offset.x, 240.0 + reticle_offset.y, scale);
			}
		}
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
