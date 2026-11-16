/*
LOWERED_RETICLE.C

display.lowered_reticle (port/linux/game/lowered_reticle.c): the view turned
up from the aim by the angle that puts the aim the setting's fraction of
the view's height below its middle, at the view's drawn field of view; the
HUD's reticle drawn where the aim lands, through the view as drawn (its
shake too) and at the HUD's reticle scale; the first-person weapon posed
at the view turned back to the aim; a scope's zoom bringing the reticle
back to the middle; nothing turned when the setting is off or the view is
not the player's aim. Checked against a projection of its own, the
camera's pinhole on a 1920x1080 screen, its vertical angle across the
window (the safe frame) as the game's frustum takes it, the whole screen or
a smaller one in its middle: the reticle is a fraction of the screen's
height below the middle either way; and drawn through display.panini's
projection (the game's panini_projection.h), as panini.c draws the view and
places the HUD's marks.
*/

#include "harness.h"
#include <math.h>

#include "panini_projection.h"

#define MAXIMUM_LOCAL_PLAYERS 4
#define _pi 3.14159265358979323846f
#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))

typedef struct { real x, y; } real_point2d;
typedef struct { short x, y; } point2d;
typedef struct { short x0, x1, y0, y1; } rectangle2d;

struct render_camera
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	real vertical_field_of_view;
	rectangle2d viewport_bounds;
	rectangle2d window_bounds;
};

struct render_frustum
{
	boolean projection_valid;
};

static struct
{
	short local_player_index;
	struct render_camera camera;
	struct render_frustum frustum;
} render;

struct unit_definition { struct { real camera_field_of_view; } unit; };
struct unit_datum { long definition_index; struct { short current_weapon_index; } unit; };

#define WIDTH 1920
#define HEIGHT 1080
/* the game's units' camera angle (70 degrees across 4:3) */
#define UNZOOMED 1.2217305f

static int fake_on;
static double fake_percent = 10.0;
static unsigned long fake_changes = 1;
static boolean fake_aiming = TRUE;
static long fake_weapon = 7;
static real fake_scope;
static struct unit_datum fake_unit;
/* display.panini's strength (0 off) and vertical compression */
static float fake_panini_d, fake_panini_s;
static struct unit_definition fake_definition;

static unsigned long config_changes(void) { return fake_changes; }
static int config_boolean(const char *name) { return !strcmp(name, "display.lowered_reticle") ? fake_on : 0; }
static double config_real(const char *name)
{
	return !strcmp(name, "display.lowered_reticle_amount") ? fake_percent : -1.0e9;
}
/* each local player's own (their profile's): player 2's switch set apart
(fake_player2_on: -1 config.toml's) */
static int fake_player2_on = -1;
static int config_player_boolean(int local_player, const char *name)
{
	if (local_player == 1 && fake_player2_on >= 0 && !strcmp(name, "display.lowered_reticle"))
		return fake_player2_on;
	return config_boolean(name);
}
static double config_player_real(int local_player, const char *name)
{
	(void)local_player;
	return config_real(name);
}
static struct unit_datum *render_fov_unit(short local_player_index, long *unit_index)
{
	(void)local_player_index;
	*unit_index = 3;
	return fake_aiming ? &fake_unit : NULL;
}
static struct unit_definition *unit_definition_get(long definition_index)
{
	(void)definition_index;
	return &fake_definition;
}
static long unit_inventory_get_weapon(long unit_index, short index)
{
	(void)unit_index;
	(void)index;
	return fake_weapon;
}
/* the weapon's first zoom level's angle: the camera's over its magnification */
static real weapon_get_field_of_view(long weapon_index, real field_of_view, short zoom_level)
{
	(void)weapon_index;
	(void)zoom_level;
	return fake_scope > 0.0f ? 2.0f * atanf(tanf(field_of_view * 0.5f) / fake_scope) : field_of_view;
}

/* panini.c's: the projection of a view of these half tangents at the
viewport's edges, while display.panini is on */
static boolean panini_projection_of_view(short local_player_index, real tangent_x, real tangent_y,
	struct panini_projection *projection)
{
	(void)local_player_index;
	return fake_panini_d > 0.0f && panini_projection_fit(projection, fake_panini_d, fake_panini_s, tangent_x, tangent_y);
}

/* the test's projection: the camera's pinhole, its vertical angle across the
window's height (render_camera_build_frustum_bounds' and _build_frustum's:
the window's middle is the view's), then display.panini's projection when it
is on (panini.c's panini_view_to_screen), pixels on the screen from its top
left */
static boolean project(struct render_camera const *camera, real_point3d const *point, real_point2d *screen)
{
	real_vector3d right, to;
	real depth, half = tanf(camera->vertical_field_of_view * 0.5f);
	real viewport_width = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	real viewport_height = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real tangent_y = half * viewport_height / (real)(camera->window_bounds.y1 - camera->window_bounds.y0);
	real tangent_x = tangent_y * viewport_width / viewport_height;
	real across, up;
	struct panini_projection projection;

	right.i = camera->forward.j * camera->up.k - camera->forward.k * camera->up.j;
	right.j = camera->forward.k * camera->up.i - camera->forward.i * camera->up.k;
	right.k = camera->forward.i * camera->up.j - camera->forward.j * camera->up.i;
	to.i = point->x - camera->position.x;
	to.j = point->y - camera->position.y;
	to.k = point->z - camera->position.z;
	depth = to.i * camera->forward.i + to.j * camera->forward.j + to.k * camera->forward.k;
	if (!(depth > 0.0f))
		return FALSE;
	across = (to.i * right.i + to.j * right.j + to.k * right.k) / depth;
	up = (to.i * camera->up.i + to.j * camera->up.j + to.k * camera->up.k) / depth;
	if (panini_projection_of_view(0, tangent_x, tangent_y, &projection))
		panini_projection_forward(&projection, across, up, &across, &up);
	screen->x = (camera->window_bounds.x0 + camera->window_bounds.x1) * 0.5f + across / tangent_x * viewport_width * 0.5f;
	screen->y = (camera->window_bounds.y0 + camera->window_bounds.y1) * 0.5f - up / tangent_y * viewport_height * 0.5f;
	return TRUE;
}

static boolean render_camera_world_to_screen(struct render_camera const *camera, struct render_frustum const *frustum,
	real_point3d const *point, real_point2d *screen)
{
	(void)frustum;
	return project(camera, point, screen);
}

#include "under_test.inc"

#define NEAR(a, b, e) (fabsf((a) - (b)) <= (e))

static real_vector3d vector(real i, real j, real k)
{
	real_vector3d v;

	v.i = i;
	v.j = j;
	v.k = k;
	return v;
}

static real dot(real_vector3d a, real_vector3d b)
{
	return a.i * b.i + a.j * b.j + a.k * b.k;
}

static void settings(int on, double percent)
{
	fake_on = on;
	fake_percent = percent;
	fake_changes++;
}

/* the window's inset from the screen's edges (the safe frame), each side */
static short inset_x, inset_y;

/* a view aimed along yaw and pitch (radians), up square to it */
static struct render_camera camera_aimed(real yaw, real pitch, real vertical_field_of_view)
{
	struct render_camera camera;

	memset(&camera, 0, sizeof(camera));
	camera.position.x = 12.f;
	camera.position.y = -3.f;
	camera.position.z = 1.5f;
	camera.forward = vector(cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch));
	camera.up = vector(-cosf(yaw) * sinf(pitch), -sinf(yaw) * sinf(pitch), cosf(pitch));
	camera.vertical_field_of_view = vertical_field_of_view;
	camera.viewport_bounds.x1 = WIDTH;
	camera.viewport_bounds.y1 = HEIGHT;
	camera.window_bounds.x0 = inset_x;
	camera.window_bounds.x1 = WIDTH - inset_x;
	camera.window_bounds.y0 = inset_y;
	camera.window_bounds.y1 = HEIGHT - inset_y;
	return camera;
}

/* the view of a frame: the game's field of view (the observer's), the angle
it is drawn at, forward and up turned as main.c's set_window_camera_values
turns them */
static struct render_camera frame(struct render_camera aim, real game_field_of_view)
{
	struct render_camera view = aim;

	lowered_reticle_view(0, game_field_of_view, &view);
	render.local_player_index = 0;
	render.camera = view;
	render.frustum.projection_valid = TRUE;
	return view;
}

/* where the aim (the unturned forward) lands on the view as drawn, from the
window's middle */
static real_point2d aim_on_screen(struct render_camera const *aim, struct render_camera const *view)
{
	real_point3d point;
	real_point2d screen;

	point.x = aim->position.x + aim->forward.i * 30.f;
	point.y = aim->position.y + aim->forward.j * 30.f;
	point.z = aim->position.z + aim->forward.k * 30.f;
	CHECK(project(view, &point, &screen), "the aim is behind the view");
	screen.x -= WIDTH * 0.5f;
	screen.y -= HEIGHT * 0.5f;
	return screen;
}

static void check_square(struct render_camera const *view)
{
	CHECK(NEAR(dot(view->forward, view->forward), 1.f, 1e-5f) && NEAR(dot(view->up, view->up), 1.f, 1e-5f) &&
		NEAR(dot(view->forward, view->up), 0.f, 1e-5f), "the turned view is not square");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	/* the stock view, 16:9 display.fov's 110 and a narrow one */
	static real const angles[] = { 0.9601f, 1.3456f, 0.5f };
	static real const pitches[] = { 0.f, 0.6f, -0.9f, 1.3f };
	struct render_camera aim, view;
	real_point2d offset;
	short a, p;

	fake_definition.unit.camera_field_of_view = UNZOOMED;
	fake_scope = 2.f;
	CASE("off")
	{
		/* the stock view: nothing turned, the middle, the weapon at the view */
		settings(0, 10.0);
		aim = camera_aimed(0.4f, 0.3f, angles[0]);
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "the view turned with the setting off");
		CHECK(lowered_reticle_lift(0) == 0.f, "a lift of %f", lowered_reticle_lift(0));
		CHECK(!lowered_reticle_screen_offset(0, &view, &render.frustum, &offset), "an offset");
		CHECK(lowered_reticle_aim_camera(0, &view) == &view, "another camera for the weapon");
		{
			point2d point = { 960, 540 };

			lowered_reticle_offset_point(&point, 0.8f);
			CHECK(point.x == 960 && point.y == 540, "the reticle moved to %d %d", point.x, point.y);
		}
		/* on, but nothing to lower by */
		settings(1, 0.0);
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "the view turned by 0");
	}
	CASE("lands_on_the_reticle")
	{
		/* at any field of view and any aim, the aim lands the fraction of
		the screen's height below its middle, straight below it, and the
		view looks above the aim; with the window the whole screen and a
		safe frame inside it (the game's, 85% of it) */
		short window;

		settings(1, 12.0);
		for (window = 0; window < 2; window++)
		for (a = 0; a < (short)(sizeof(angles) / sizeof(angles[0])); a++)
		{
			inset_x = window ? 144 : 0;
			inset_y = window ? 81 : 0;
			for (p = 0; p < (short)(sizeof(pitches) / sizeof(pitches[0])); p++)
			{
				aim = camera_aimed(-1.1f + p, pitches[p], angles[a]);
				view = frame(aim, UNZOOMED);
				check_square(&view);
				offset = aim_on_screen(&aim, &view);
				CHECK(NEAR(offset.x, 0.f, 0.05f) && NEAR(offset.y, 0.12f * HEIGHT, 0.05f),
					"angle %f pitch %f: the aim at %f %f, not 0 %f", angles[a], pitches[p], offset.x, offset.y,
					0.12f * HEIGHT);
				CHECK(dot(view.forward, aim.up) > 0.f, "the view looks below the aim");
				CHECK(NEAR(dot(view.forward, aim.forward), cosf(lowered_reticle_lift(0)), 1e-5f), "turned by %f",
					lowered_reticle_lift(0));
				/* the view is turned about its right: the aim's yaw kept */
				CHECK(fabsf(dot(view.forward, vector(-sinf(-1.1f + p), cosf(-1.1f + p), 0.f))) < 1e-5f,
					"the view turned aside");
			}
		}
	}
	CASE("screen_offset")
	{
		/* the HUD's offset is where the aim lands, through the view as
		drawn */
		settings(1, 10.0);
		for (a = 0; a < (short)(sizeof(angles) / sizeof(angles[0])); a++)
		{
			real_point2d landed;

			aim = camera_aimed(2.0f, -0.2f, angles[a]);
			view = frame(aim, UNZOOMED);
			landed = aim_on_screen(&aim, &view);
			CHECK(lowered_reticle_screen_offset(0, &view, &render.frustum, &offset), "no offset");
			CHECK(NEAR(offset.x, landed.x, 0.05f) && NEAR(offset.y, landed.y, 0.05f) &&
				NEAR(offset.y, 108.f, 0.05f), "angle %f: %f %f, the aim at %f %f", angles[a], offset.x, offset.y,
				landed.x, landed.y);
		}
		/* a shaken view (the camera's effect, after the turn): the reticle
		keeps its place on the screen, as the stock one keeps the middle */
		{
			real roll = 0.05f, c = cosf(roll), s = sinf(roll);
			real_vector3d right;

			right.i = view.forward.j * view.up.k - view.forward.k * view.up.j;
			right.j = view.forward.k * view.up.i - view.forward.i * view.up.k;
			right.k = view.forward.i * view.up.j - view.forward.j * view.up.i;
			view.up = vector(view.up.i * c + right.i * s, view.up.j * c + right.j * s, view.up.k * c + right.k * s);
			CHECK(lowered_reticle_screen_offset(0, &view, &render.frustum, &offset), "no offset shaken");
			CHECK(NEAR(offset.x, 0.f, 0.05f) && NEAR(offset.y, 108.f, 0.05f), "shaken: %f %f", offset.x, offset.y);
		}
	}
	CASE("hud_scale")
	{
		/* the reticle's point, moved at the HUD's size and scaled about the
		middle with the field of view (hud_draw.c), lands on the aim */
		static real const scales[] = { 1.f, 0.7f, 1.3f };
		short s;

		settings(1, 15.0);
		aim = camera_aimed(0.f, 0.f, angles[1]);
		view = frame(aim, UNZOOMED);
		offset = aim_on_screen(&aim, &view);
		for (s = 0; s < 3; s++)
		{
			point2d point = { 960, 540 };
			real drawn_x, drawn_y;

			lowered_reticle_offset_point(&point, scales[s]);
			drawn_x = 960.f + (point.x - 960.f) * scales[s];
			drawn_y = 540.f + (point.y - 540.f) * scales[s];
			CHECK(NEAR(drawn_x, 960.f + offset.x, 1.f) && NEAR(drawn_y, 540.f + offset.y, 1.f),
				"scale %f: drawn at %f %f, the aim at %f %f", scales[s], drawn_x, drawn_y, 960.f + offset.x,
				540.f + offset.y);
		}
	}
	CASE("aim_camera")
	{
		/* the weapon's camera is the view turned back to the aim: its
		forward the aim, its up and place the view's frame's */
		struct render_camera const *pose;

		settings(1, 20.0);
		aim = camera_aimed(0.7f, 0.25f, angles[0]);
		view = frame(aim, UNZOOMED);
		pose = lowered_reticle_aim_camera(0, &view);
		CHECK(pose != &view, "the view's own camera");
		CHECK(NEAR(pose->forward.i, aim.forward.i, 1e-5f) && NEAR(pose->forward.j, aim.forward.j, 1e-5f) &&
			NEAR(pose->forward.k, aim.forward.k, 1e-5f), "forward %f %f %f, the aim %f %f %f", pose->forward.i,
			pose->forward.j, pose->forward.k, aim.forward.i, aim.forward.j, aim.forward.k);
		CHECK(NEAR(pose->up.i, aim.up.i, 1e-5f) && NEAR(pose->up.j, aim.up.j, 1e-5f) && NEAR(pose->up.k, aim.up.k, 1e-5f),
			"up");
		CHECK(!memcmp(&pose->position, &view.position, sizeof(view.position)) &&
			pose->vertical_field_of_view == view.vertical_field_of_view, "the place or the angle changed");
		CHECK(!memcmp(&view.window_bounds, &pose->window_bounds, sizeof(view.window_bounds)), "the window");
		/* the weapon's forward on the screen: where the aim is */
		{
			struct render_camera held = *pose;

			offset = aim_on_screen(&held, &view);
			CHECK(NEAR(offset.y, 0.2f * HEIGHT, 0.05f), "the weapon points at %f", offset.y);
		}
	}
	CASE("zoom")
	{
		/* a 2x scope: all of it unzoomed, none at the scope's angle, about
		half way between, none at a further level; with no scope, a view
		narrowed by 2x likewise */
		real scoped = 2.f * atanf(tanf(UNZOOMED * 0.5f) / 2.f);
		real between = 2.f * atanf(tanf(UNZOOMED * 0.5f) * 0.75f);
		real further = 2.f * atanf(tanf(UNZOOMED * 0.5f) / 8.f);
		real full;

		settings(1, 10.0);
		aim = camera_aimed(0.f, 0.f, angles[0]);
		frame(aim, UNZOOMED);
		full = lowered_reticle_lift(0);
		CHECK(full > 0.f, "no lift unzoomed");
		frame(aim, scoped);
		CHECK(lowered_reticle_lift(0) == 0.f, "a lift scoped: %f", lowered_reticle_lift(0));
		frame(aim, further);
		CHECK(lowered_reticle_lift(0) == 0.f, "a lift further in: %f", lowered_reticle_lift(0));
		frame(aim, between);
		CHECK(lowered_reticle_lift(0) > 0.3f * full && lowered_reticle_lift(0) < 0.7f * full, "between: %f of %f",
			lowered_reticle_lift(0), full);
		/* a view wider than the unit's (a vehicle's) keeps all of it */
		frame(aim, 1.5f);
		CHECK(NEAR(lowered_reticle_lift(0), full, 1e-6f), "wider: %f", lowered_reticle_lift(0));
		fake_scope = 0.f;
		frame(aim, scoped);
		CHECK(lowered_reticle_lift(0) == 0.f, "no scope, 2x: %f", lowered_reticle_lift(0));
		fake_weapon = NONE;
		frame(aim, UNZOOMED);
		CHECK(NEAR(lowered_reticle_lift(0), full, 1e-6f), "no weapon: %f", lowered_reticle_lift(0));
	}
	CASE("not_the_aim")
	{
		/* dead, a cinematic, a scripted camera: the view the game's */
		settings(1, 10.0);
		aim = camera_aimed(0.f, 0.2f, angles[0]);
		frame(aim, UNZOOMED);
		CHECK(lowered_reticle_lift(0) > 0.f, "not turned");
		fake_aiming = FALSE;
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "the view turned");
		CHECK(!lowered_reticle_screen_offset(0, &view, &render.frustum, &offset), "an offset");
		/* and back, the next frame turns again */
		fake_aiming = TRUE;
		frame(aim, UNZOOMED);
		CHECK(lowered_reticle_lift(0) > 0.f, "not turned again");
		/* a player who is not local */
		view = aim;
		lowered_reticle_view(-1, UNZOOMED, &view);
		lowered_reticle_view(MAXIMUM_LOCAL_PLAYERS, UNZOOMED, &view);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "another player's view turned");
		CHECK(lowered_reticle_lift(-1) == 0.f && lowered_reticle_lift(MAXIMUM_LOCAL_PLAYERS) == 0.f, "a lift");
	}
	CASE("range")
	{
		/* past 30, 30; below 0 or not a number, none */
		aim = camera_aimed(0.f, 0.f, angles[0]);
		settings(1, 80.0);
		view = frame(aim, UNZOOMED);
		offset = aim_on_screen(&aim, &view);
		CHECK(NEAR(offset.y, 0.3f * HEIGHT, 0.05f), "held at %f", offset.y);
		settings(1, -5.0);
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "turned below 0");
		settings(1, nan(""));
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "turned by not a number");
	}
	CASE("changed")
	{
		/* a change of the settings is read at once */
		aim = camera_aimed(0.f, 0.f, angles[0]);
		settings(1, 5.0);
		frame(aim, UNZOOMED);
		settings(1, 20.0);
		view = frame(aim, UNZOOMED);
		offset = aim_on_screen(&aim, &view);
		CHECK(NEAR(offset.y, 0.2f * HEIGHT, 0.05f), "at %f", offset.y);
		settings(0, 20.0);
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)), "turned once off");
	}
	CASE("panini")
	{
		/* drawn through display.panini, at full strength and with some of
		its vertical compression, and at half: the reticle the same fraction
		of the screen below the middle, the HUD's offset there too, the view
		turned less than drawn as it is (the middle magnified) */
		static real const strengths[][2] = { { 1.f, 0.f }, { 1.f, 0.4f }, { 0.5f, 1.f } };
		short window, k;

		settings(1, 12.0);
		for (window = 0; window < 2; window++)
		for (k = 0; k < 3; k++)
		for (a = 0; a < (short)(sizeof(angles) / sizeof(angles[0])); a++)
		{
			real rectilinear;

			inset_x = window ? 144 : 0;
			inset_y = window ? 81 : 0;
			aim = camera_aimed(0.3f, 0.2f, angles[a]);
			fake_panini_d = 0.f;
			frame(aim, UNZOOMED);
			rectilinear = lowered_reticle_lift(0);
			fake_panini_d = strengths[k][0];
			fake_panini_s = strengths[k][1];
			view = frame(aim, UNZOOMED);
			CHECK(lowered_reticle_lift(0) < rectilinear, "angle %f: turned %f, %f drawn as it is", angles[a],
				lowered_reticle_lift(0), rectilinear);
			offset = aim_on_screen(&aim, &view);
			CHECK(NEAR(offset.x, 0.f, 0.05f) && NEAR(offset.y, 0.12f * HEIGHT, 0.1f),
				"angle %f strength %f: the aim at %f %f", angles[a], strengths[k][0], offset.x, offset.y);
			{
				real_point2d hud;

				CHECK(lowered_reticle_screen_offset(0, &view, &render.frustum, &hud), "no offset");
				CHECK(NEAR(hud.x, offset.x, 0.05f) && NEAR(hud.y, offset.y, 0.05f), "the HUD at %f %f", hud.x, hud.y);
			}
		}
		fake_panini_d = 0.f;
	}
	CASE("players")
	{
		/* split screen's players each their own: player 2's on in their
		profile, player 1's off in config.toml's */
		aim = camera_aimed(0.3f, 0.2f, angles[0]);
		settings(0, 10.0);
		fake_player2_on = 1;
		fake_changes++;
		view = aim;
		lowered_reticle_view(1, UNZOOMED, &view);
		CHECK(lowered_reticle_lift(1) > 0.f, "player 2's not lowered");
		view = frame(aim, UNZOOMED);
		CHECK(!memcmp(&view, &aim, sizeof(view)) && lowered_reticle_lift(0) == 0.f, "player 1's lowered");
		fake_player2_on = 0;
		fake_changes++;
		view = aim;
		lowered_reticle_view(1, UNZOOMED, &view);
		CHECK(lowered_reticle_lift(1) == 0.f, "player 2's lowered once off");
		fake_player2_on = -1;
	}
	return 0;
}
