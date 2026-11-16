#include "harness.h"
#include <math.h>

/* A controller's aim (source/game/player_control.c's player_control_stick_aim, input.aim_style): the look stick's
push made a turn through the globals' look curve, and whether the turn accelerates. Classic must be the original's
to the bit; Modern must turn as fast whichever way the stick points and accelerate only near side to side. And the
look stick's round dead zone (source/input/input_xbox.c's fix_round_dead_zone), Modern's. */

typedef struct { short x, y; } point2d;
#define SHORT_MAX 32767
#define _real_epsilon 0.000001f
#define PIN(x, low, high) ((x) < (low) ? (low) : (x) > (high) ? (high) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static real square_root(real x) { return sqrtf(x); }

#include "under_test.inc"

/* a look curve as the globals' look_function is (rising, slow near the middle), and its pegging threshold */
static const real curve[] = { 0.0f, 0.04f, 0.1f, 0.18f, 0.3f, 0.45f, 0.62f, 0.8f, 1.0f };
#define CURVE_COUNT ((short)(sizeof(curve) / sizeof(curve[0])))
#define PEGGING 0.9f

static const real angles[] = { 0.0f, 30.0f, 45.0f, 60.0f, 90.0f };
static const real pushes[] = { 0.1f, 0.3f, 0.5f, 0.75f, 0.9f, 0.95f, 1.0f };
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const struct player_control_aim classic = { FALSE, 0.0f, -1.0f };

/* Modern's defaults: 20 degrees (input.modern_aim_acceleration_angle), the game's own threshold */
static struct player_control_aim modern(real degrees, real threshold)
{
	struct player_control_aim aim = { TRUE, 0.0f, threshold };

	aim.acceleration_tangent = (real)tan(degrees * M_PI / 180.0);
	return aim;
}

/* the stick pushed push (of its travel) at degrees from side to side, as input_abstraction.c gives it: the circle the
stick moves in stretched out to the square */
static void stick(real push, real degrees, real *yaw, real *pitch)
{
	double radians = degrees * M_PI / 180.0;
	double c = cos(radians), s = sin(radians);
	double scale = 1.0 / MAX(fabs(c), fabs(s));

	*yaw = (real)(push * c * scale);
	*pitch = (real)(push * s * scale);
	if (degrees == 0.0f)
		*pitch = 0.0f;
	if (degrees == 90.0f)
		*yaw = 0.0f;
}

/* the original's, as it was before the aim styles (player_control.c's player_control_clamp_look, its stick look's
curve and its acceleration's test), kept here word for word */
static boolean original(real yaw_in, real pitch_in, real *curved_yaw, real *curved_pitch)
{
	real absolute_pitch = fabs(pitch_in);
	real absolute_yaw = fabs(yaw_in);
	real look_scale = 1.f;
	real clamped_yaw, clamped_pitch;

	if (absolute_pitch > 0.1f && absolute_yaw > 0.1f)
	{
		if (absolute_pitch > absolute_yaw)
		{
			absolute_yaw /= absolute_pitch;
			absolute_pitch = 1.f;
		}
		else
		{
			absolute_pitch /= absolute_yaw;
			absolute_yaw = 1.f;
		}
		look_scale = square_root(
			absolute_pitch * absolute_pitch +
			absolute_yaw * absolute_yaw);
	}
	clamped_yaw = PIN(yaw_in * look_scale, -1.f, 1.f);
	clamped_pitch = PIN(pitch_in * look_scale, -1.f, 1.f);
	*curved_yaw = evaluate_piecewise_linear_function(CURVE_COUNT, curve, clamped_yaw);
	*curved_pitch = evaluate_piecewise_linear_function(CURVE_COUNT, curve, clamped_pitch);
	return fabs(clamped_yaw) >= PEGGING;
}

static boolean aim(struct player_control_aim const *style, real yaw, real pitch, real *curved_yaw, real *curved_pitch)
{
	real clamped_yaw, clamped_pitch;

	return player_control_stick_aim(style, CURVE_COUNT, curve, PEGGING, yaw, pitch, curved_yaw, curved_pitch,
		&clamped_yaw, &clamped_pitch);
}

/* Classic is the original's to the bit, every angle, push and sign; a full diagonal turns faster and accelerates */
static void case_classic(void)
{
	size_t a, p;
	int sign;
	real yaw, pitch, cy, cp, oy, op;

	for (a = 0; a < COUNT(angles); a++)
		for (p = 0; p < COUNT(pushes); p++)
			for (sign = 0; sign < 4; sign++)
			{
				boolean pegged, original_pegged;

				stick(pushes[p], angles[a], &yaw, &pitch);
				yaw = sign & 1 ? -yaw : yaw;
				pitch = sign & 2 ? -pitch : pitch;
				pegged = aim(&classic, yaw, pitch, &cy, &cp);
				original_pegged = original(yaw, pitch, &oy, &op);
				CHECK(!memcmp(&cy, &oy, sizeof(real)) && !memcmp(&cp, &op, sizeof(real)) &&
					pegged == original_pegged,
					"at %g degrees, push %g: classic %.9g, %.9g (%d), the original %.9g, %.9g (%d)", angles[a],
					pushes[p], cy, cp, pegged, oy, op, original_pegged);
			}
	stick(1.0f, 45.0f, &yaw, &pitch);
	CHECK(aim(&classic, yaw, pitch, &cy, &cp) && cy == 1.0f && cp == 1.0f,
		"a full diagonal under Classic: %g, %g, not the curve's top both ways and accelerating", cy, cp);
}

/* Modern turns as fast whichever way the stick points, as fast as Classic straight across */
static void case_modern_speed(void)
{
	struct player_control_aim style = modern(20.0f, -1.0f);
	size_t a, p;
	real yaw, pitch, cy, cp, oy, op;

	for (p = 0; p < COUNT(pushes); p++)
	{
		real expected = evaluate_piecewise_linear_function(CURVE_COUNT, curve, pushes[p]);

		stick(pushes[p], 0.0f, &yaw, &pitch);
		aim(&style, yaw, pitch, &cy, &cp);
		original(yaw, pitch, &oy, &op);
		CHECK(!memcmp(&cy, &oy, sizeof(real)) && cp == 0.0f,
			"straight across, push %g: modern %.9g, classic %.9g", pushes[p], cy, oy);
		for (a = 0; a < COUNT(angles); a++)
		{
			real speed;
			double radians = angles[a] * M_PI / 180.0;

			stick(pushes[p], angles[a], &yaw, &pitch);
			aim(&style, -yaw, pitch, &cy, &cp);
			speed = sqrtf(cy * cy + cp * cp);
			CHECK(fabsf(speed - expected) < 1e-5f,
				"at %g degrees, push %g: speed %.7g, straight across %.7g", angles[a], pushes[p], speed, expected);
			CHECK(fabs(atan2(cp, -cy) - radians) < 1e-4,
				"at %g degrees, push %g: turned at %g degrees", angles[a], pushes[p],
				atan2(cp, -cy) * 180.0 / M_PI);
		}
	}
	CHECK(!aim(&style, 0.0f, 0.0f, &cy, &cp) && cy == 0.0f && cp == 0.0f, "the stick at rest turned %g, %g", cy, cp);
}

/* Modern accelerates only pushed to the threshold within its angle of side to side: never on a diagonal */
static void case_modern_acceleration(void)
{
	struct player_control_aim style = modern(20.0f, -1.0f);
	real yaw, pitch, cy, cp;
	size_t a;

	for (a = 0; a < COUNT(angles); a++)
	{
		boolean accelerates;

		stick(1.0f, angles[a], &yaw, &pitch);
		accelerates = aim(&style, yaw, pitch, &cy, &cp);
		CHECK(accelerates == (angles[a] <= 20.0f), "a full push at %g degrees %s", angles[a],
			accelerates ? "accelerated" : "did not accelerate");
		/* (the left and down too) */
		CHECK(aim(&style, -yaw, -pitch, &cy, &cp) == accelerates, "a full push at %g degrees, mirrored, differed",
			angles[a]);
	}
	stick(1.0f, 19.0f, &yaw, &pitch);
	CHECK(aim(&style, yaw, pitch, &cy, &cp), "a full push at 19 degrees did not accelerate");
	stick(1.0f, 21.0f, &yaw, &pitch);
	CHECK(!aim(&style, yaw, pitch, &cy, &cp), "a full push at 21 degrees accelerated");
	stick(0.85f, 0.0f, &yaw, &pitch);
	CHECK(!aim(&style, yaw, pitch, &cy, &cp), "a push short of the threshold accelerated");
	stick(PEGGING, 0.0f, &yaw, &pitch);
	CHECK(aim(&style, yaw, pitch, &cy, &cp), "a push at the threshold did not accelerate");
	/* (where Classic does, a full diagonal) */
	stick(1.0f, 45.0f, &yaw, &pitch);
	CHECK(aim(&classic, yaw, pitch, &cy, &cp), "Classic did not accelerate on a full diagonal");
}

/* Modern's tunables: its angle and its threshold */
static void case_modern_tunables(void)
{
	struct player_control_aim wide = modern(45.0f, -1.0f);
	struct player_control_aim early = modern(20.0f, 0.5f);
	struct player_control_aim none = modern(0.0f, -1.0f);
	real yaw, pitch, cy, cp;

	stick(1.0f, 44.0f, &yaw, &pitch);
	CHECK(aim(&wide, yaw, pitch, &cy, &cp), "45 degrees wide, a push at 44 did not accelerate");
	stick(0.6f, 10.0f, &yaw, &pitch);
	CHECK(aim(&early, yaw, pitch, &cy, &cp), "from half a push, 0.6 did not accelerate");
	stick(0.4f, 0.0f, &yaw, &pitch);
	CHECK(!aim(&early, yaw, pitch, &cy, &cp), "from half a push, 0.4 accelerated");
	stick(1.0f, 0.0f, &yaw, &pitch);
	CHECK(aim(&none, yaw, pitch, &cy, &cp), "0 degrees wide, straight across did not accelerate");
	stick(1.0f, 2.0f, &yaw, &pitch);
	CHECK(!aim(&none, yaw, pitch, &cy, &cp), "0 degrees wide, 2 degrees off accelerated");
}

/* the round dead zone: none within its range whichever way, a full push at the rim the whole travel whichever way,
the direction kept, where each direction's own reads a full diagonal short */
static void case_round_dead_zone(void)
{
	short range = (short)(0.27 * 32767);
	point2d out;
	double length;

	fix_round_dead_zone(6000, 6000, range, &out);
	CHECK(out.x == 0 && out.y == 0, "inside the dead zone on a diagonal read %d, %d", out.x, out.y);
	fix_round_dead_zone(23170, 23170, range, &out);
	length = sqrt((double)out.x * out.x + (double)out.y * out.y);
	CHECK(fabs(length - 32767) < 3 && abs(out.x - out.y) <= 1, "a full diagonal read %d, %d", out.x, out.y);
	fix_round_dead_zone(32767, 0, range, &out);
	CHECK(out.x == 32767 && out.y == 0, "a full push straight across read %d, %d", out.x, out.y);
	fix_round_dead_zone(-16000, 12000, range, &out);
	CHECK(out.x < 0 && out.y > 0 && abs(out.x * 3 + out.y * 4) < 8, "the direction turned: %d, %d", out.x, out.y);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "classic"))
		case_classic();
	else if (!strcmp(name, "modern_speed"))
		case_modern_speed();
	else if (!strcmp(name, "modern_acceleration"))
		case_modern_acceleration();
	else if (!strcmp(name, "modern_tunables"))
		case_modern_tunables();
	else if (!strcmp(name, "round_dead_zone"))
		case_round_dead_zone();
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
