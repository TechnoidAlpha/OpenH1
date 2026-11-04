/* An OpenH1 game's actors to far clients (network_actors.c,
actor_state_pressed; network_objects.c, distributed_aims_at): a unit's
press (its trigger pulled or let go, a start, a stop, a sharp turn) goes to
every client at once, a steady turn or a stick's drift does not until it
adds up to one; a unit aimed at a player, or a player's aim on it, is told
by the line of the aim. */
#include <math.h>
#include "harness.h"
typedef unsigned short word;
#define ABS(x) ((x>=0) ? (x) : -(x))
#define csmemcpy memcpy
struct distributed_vector { short i, j, k; };
#include "config.inc"
#include "under_test.inc"

static struct distributed_actor_state state;
static struct actor_pressed last;

static void expect(char const *name, boolean pressed)
{
	boolean was = actor_state_pressed(&last, &state);

	CHECK(was == pressed, "%s: %s, not %s", name, was ? "a press" : "no press", pressed ? "a press" : "no press");
}

int main(int argc, char **argv) {
	char const *case_name = argc > 1 ? argv[1] : "";
	short step;

	memset(&state, 0, sizeof(state));
	memset(&last, 0, sizeof(last));
	state.aiming[_angle_yaw] = 1000;
	/* (the first is a press, and noted) */
	expect(case_name, TRUE);
	CHECK(last.valid && last.aiming[_angle_yaw] == 1000, "%s: not noted", case_name);
	CASE("same")
		expect(case_name, FALSE);
	CASE("flags")
	{
		state.control_flags = 4;
		expect(case_name, TRUE);
		expect(case_name, FALSE);
	}
	CASE("trigger")
	{
		/* a touch of the trigger from none is a pull; a little more is not */
		state.primary_trigger = 10;
		expect(case_name, TRUE);
		state.primary_trigger = 40;
		expect(case_name, FALSE);
		state.primary_trigger = 0;
		expect(case_name, TRUE);
	}
	CASE("start")
	{
		state.throttle[0] = 127;
		expect(case_name, TRUE);
		state.throttle[0] = 0;
		expect(case_name, TRUE);
	}
	CASE("drift")
	{
		/* a stick's drift adds up to a press, told against the last one */
		for (step = 1; step <= 4; step++)
		{
			state.throttle[1] = (signed char)(step * 10);
			expect(case_name, step * 10 >= 32);
		}
	}
	CASE("turn")
	{
		/* a steady turn, 1/128 of a turn a tick: a press every fourth tick */
		for (step = 1; step <= 8; step++)
		{
			state.aiming[_angle_yaw] = (short)(1000 + step * 512);
			expect(case_name, step % 4 == 0);
		}
	}
	CASE("wrap")
	{
		/* across the packed angles' wrap: the short way round */
		state.aiming[_angle_yaw] = 32000;
		expect(case_name, TRUE);
		state.aiming[_angle_yaw] = -32000;
		expect(case_name, FALSE);
		state.aiming[_angle_pitch] = -2048;
		expect(case_name, TRUE);
	}
	CASE("animation")
	{
		state.animation_state = 2;
		expect(case_name, TRUE);
	}
	CASE("aims")
	{
		real_point3d from = {0.0f, 0.0f, 0.0f};
		real_vector3d aim = {1.0f, 0.0f, 0.0f};
		real_point3d near_line = {100.0f, 1.5f, 0.0f};
		real_point3d off_line = {10.0f, 3.0f, 0.0f};
		real_point3d far_within_angle = {300.0f, 10.0f, 0.0f};
		real_point3d behind = {-10.0f, 0.0f, 0.0f};

		CHECK(distributed_aims_at(&from, &aim, &near_line), "%s: near the line", case_name);
		CHECK(!distributed_aims_at(&from, &aim, &off_line), "%s: off the line", case_name);
		CHECK(distributed_aims_at(&from, &aim, &far_within_angle), "%s: far, within the angle", case_name);
		CHECK(!distributed_aims_at(&from, &aim, &behind), "%s: behind", case_name);
	}
	return 0;
}
