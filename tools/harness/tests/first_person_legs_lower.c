/*
FIRST_PERSON_LEGS_LOWER.C

The first-person body's lower body's own heading on the flat screen
(port/linux/game/first_person_legs.c's first_person_legs_lower_turn), turned
frame by frame for an aim and a way the player moves:

- planted: standing, the aim turned within the dead zone: the lower body does
  not turn (its feet stay where they stand, #772);
- catch_up: the aim turned past the dead zone: the lower body turns after it,
  starting and ending smoothly, never more than its twist from the aim, and
  stands again once it has caught up;
- rates: one player's input at 30, 60, 120 and 240 frames a second: the lower
  body where the others have it at each time they all draw;
- walks: walking at an angle, the lower body faces the way it walks forward
  or backward (never behind the aim), and sideways it sidesteps: each walk
  goes the way the player moves (#772: the legs faced the aim);
- keep: the way the player moves wavering about halfway between two walks:
  the walk kept, and changed once the way is well past halfway.
*/

#include "harness.h"
#include <math.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define PIN(n, low, high) ((n) < (low) ? (low) : ((n) > (high) ? (high) : (n)))
#define _pi ((real)3.14159265358979323846f)

typedef struct { real scale; real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;

#include "under_test.inc"

#define DEGREES(d) ((real)((d) * _pi / 180.0))

static struct first_person_legs_lower lower;

static void stand(real yaw)
{
	memset(&lower, 0, sizeof(lower));
	lower.walk = NONE;
	lower.yaw = yaw;
}

/* seconds of frames at a rate, the aim and the way the player moves (its
throttle: forward, left; none standing) steady */
static void play(real seconds, int rate, real aim, real forward, real left)
{
	int frame, frames = (int)lroundf(seconds * (real)rate);

	for (frame = 0; frame < frames; frame++)
	{
		first_person_legs_lower_turn(&lower, aim, forward, left, 1.0f / (real)rate);
		CHECK(fabsf(first_person_legs_angle(lower.yaw - aim)) <= FIRST_PERSON_LEGS_TWIST_MOST + 1.0e-5f,
			"twisted %g degrees from the aim", first_person_legs_angle(lower.yaw - aim) * 180.0 / _pi);
	}
}

/* the player's input of the rates case, played at a rate: the lower body's
heading at each half second */
static void rates_played(int rate, real headings[6])
{
	stand(0.f);
	play(0.5f, rate, DEGREES(30), 0.f, 0.f);
	headings[0] = lower.yaw;
	play(0.5f, rate, DEGREES(100), 0.f, 0.f);
	headings[1] = lower.yaw;
	play(0.5f, rate, DEGREES(100), 0.7071f, -0.7071f);
	headings[2] = lower.yaw;
	play(0.5f, rate, DEGREES(140), 0.f, 1.f);
	headings[3] = lower.yaw;
	play(0.5f, rate, DEGREES(140), -0.7071f, 0.7071f);
	headings[4] = lower.yaw;
	play(0.5f, rate, DEGREES(140), 0.f, 0.f);
	headings[5] = lower.yaw;
}

/* walking a way (degrees from the aim) from standing: the walk it walks, and
that it goes that way, its heading within halfway and a little of the aim's
way */
static void walks(real way, short walk, real off)
{
	static real const ways[4] = { 0.f, 180.f, 90.f, -90.f };
	real aim = DEGREES(20), heading;

	stand(aim);
	play(1.0f, 60, aim, cosf(DEGREES(way)), sinf(DEGREES(way)));
	heading = first_person_legs_angle(lower.yaw - aim);
	CHECK(lower.walk == walk, "walking %g degrees: walk %d, not %d", way, lower.walk, walk);
	CHECK(fabsf(heading - DEGREES(off)) < DEGREES(0.5), "walking %g degrees: heading %g, not %g", way,
		heading * 180.0 / _pi, off);
	CHECK(fabsf(first_person_legs_angle(heading + DEGREES(ways[walk]) - DEGREES(way))) < DEGREES(0.5),
		"walking %g degrees: its walk goes %g", way, (heading * 180.0 / _pi) + ways[walk]);
	CHECK(fabsf(heading) <= 0.25f * _pi + FIRST_PERSON_LEGS_WALK_KEEP + 1.0e-5f, "walking %g degrees: heading %g",
		way, heading * 180.0 / _pi);
}

int main(int argc, char **argv)
{
	char const *which = argc > 1 ? argv[1] : "";

	if (!strcmp(which, "planted"))
	{
		/* the aim swept within the dead zone, at 144 frames a second */
		int frame;

		stand(DEGREES(10));
		for (frame = 0; frame < 3 * 144; frame++)
		{
			real aim = DEGREES(10) + 0.95f * FIRST_PERSON_LEGS_DEAD_ZONE * sinf((real)frame / 40.f);

			first_person_legs_lower_turn(&lower, aim, 0.f, 0.f, 1.f / 144.f);
			CHECK(lower.yaw == DEGREES(10), "frame %d, the aim %g degrees from it: turned to %g", frame,
				(aim - DEGREES(10)) * 180.0 / _pi, lower.yaw * 180.0 / _pi);
		}
	}
	else if (!strcmp(which, "catch_up"))
	{
		real before;

		stand(0.f);
		first_person_legs_lower_turn(&lower, DEGREES(80), 0.f, 0.f, 1.f / 144.f);
		CHECK(lower.catching, "not turning after an aim 80 degrees from it");
		/* (starting smoothly: from standing, a frame turns it little) */
		CHECK(fabsf(lower.yaw) < DEGREES(1), "turned %g degrees in its first frame", lower.yaw * 180.0 / _pi);
		play(0.5f, 144, DEGREES(80), 0.f, 0.f);
		CHECK(!lower.catching && fabsf(first_person_legs_angle(lower.yaw - DEGREES(80))) < FIRST_PERSON_LEGS_SETTLED,
			"not caught up in half a second: %g degrees", lower.yaw * 180.0 / _pi);
		before = lower.yaw;
		play(1.0f, 144, DEGREES(80) + 0.5f * FIRST_PERSON_LEGS_DEAD_ZONE, 0.f, 0.f);
		CHECK(lower.yaw == before, "caught up, it turned on");
		/* (the aim whipped round: never twisted past its most) */
		stand(0.f);
		play(0.05f, 144, DEGREES(170), 0.f, 0.f);
	}
	else if (!strcmp(which, "rates"))
	{
		static int const rates[] = { 30, 60, 120, 240 };
		real headings[4][6];
		int rate, at;

		for (rate = 0; rate < 4; rate++)
			rates_played(rates[rate], headings[rate]);
		for (rate = 1; rate < 4; rate++)
		{
			for (at = 0; at < 6; at++)
			{
				CHECK(fabsf(first_person_legs_angle(headings[rate][at] - headings[0][at])) < DEGREES(0.25),
					"%d frames a second: at %g s, %g degrees, at 30 %g", rates[rate], 0.5 * (at + 1),
					headings[rate][at] * 180.0 / _pi, headings[0][at] * 180.0 / _pi);
			}
		}
	}
	else if (!strcmp(which, "walks"))
	{
		/* (the way, the walk: forward, backward, left, right; the heading
		from the aim) */
		walks(0.f, 0, 0.f);
		walks(30.f, 0, 30.f);
		walks(-45.f, 0, -45.f);
		walks(60.f, 2, -30.f);
		walks(90.f, 2, 0.f);
		walks(-90.f, 3, 0.f);
		walks(-120.f, 3, -30.f);
		walks(135.f, 1, -45.f);
		walks(-150.f, 1, 30.f);
		walks(180.f, 1, 0.f);
	}
	else if (!strcmp(which, "keep"))
	{
		int frame;

		/* (wavering about halfway between forward and left) */
		stand(0.f);
		for (frame = 0; frame < 120; frame++)
		{
			real way = DEGREES(45) + DEGREES(8) * sinf((real)frame / 5.f);

			first_person_legs_lower_turn(&lower, 0.f, cosf(way), sinf(way), 1.f / 60.f);
			CHECK(lower.walk == 0, "frame %d, walking %g degrees: walk %d", frame, way * 180.0 / _pi, lower.walk);
		}
		/* (well past halfway: the sideways walk) */
		play(0.5f, 60, 0.f, cosf(DEGREES(70)), sinf(DEGREES(70)));
		CHECK(lower.walk == 2, "walking 70 degrees: walk %d", lower.walk);
	}
	else
	{
		fprintf(stderr, "no case %s\n", which);
		return 2;
	}
	return 0;
}
