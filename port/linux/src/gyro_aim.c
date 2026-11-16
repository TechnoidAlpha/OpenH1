/*
GYRO_AIM.C

Aiming with a controller's gyroscope, and the flick stick (gyro_aim.h).
*/

#include "gyro_aim.h"

#include <math.h>
#include <string.h>

/* standard gravity, meters a second squared (SDL_STANDARD_GRAVITY) */
#define GRAVITY 9.80665f

/* ---------- the up direction (player space)

The gyroscope turns the up direction as the controller turns, and the
accelerometer, which reads gravity's reaction at rest, draws it back to the
true up over UP_CORRECTION_SECONDS: a shake (whose acceleration is not
gravity's) cannot throw it, nor the gyroscope's error drift it. A reading
far from gravity's strength is a shake, and corrects nothing. */
#define UP_CORRECTION_SECONDS 0.25f
#define ACCELEROMETER_TRUSTED (0.25f * GRAVITY)

/* ---------- the automatic calibration

The gyroscope reads a little turn at rest (its bias), which would drift
the view. While the controller is still (its readings steady, within
STILL_GYRO and STILL_ACCELEROMETER of their recent means, and the turn
read small), for STILL_SETTLE_SECONDS, the gyroscope's mean is its bias,
followed over BIAS_SECONDS. A turn as slow and as steady as a bias is
taken for one only while it stays under STILL_RATE_MOST. */
#define STILL_MEAN_SECONDS 0.25f
#define STILL_GYRO 0.035f
#define STILL_ACCELEROMETER 0.5f
#define STILL_RATE_MOST 0.1f
/* (before any bias is known, a larger one is allowed for) */
#define STILL_RATE_MOST_FIRST 0.3f
#define STILL_SETTLE_SECONDS 1.0f
#define BIAS_SECONDS 1.0f

/* ---------- the smoothing (soft tiered smoothing)

A turn slower than SMOOTH_SLOWEST (radians a second) is all smoothed, a
turn faster than SMOOTH_FASTEST not at all, one between in part: the
hand's tremor steadied, never a real motion's lag. The smoothed part is
averaged over SMOOTH_SECONDS, and none of it is lost: it turns the view
as far, a little later. */
#define SMOOTH_SLOWEST 0.044f
#define SMOOTH_FASTEST 0.087f
#define SMOOTH_SECONDS 0.06f

/* a sample this long after the last is after a gap (samples lost, the
sensor turned off and on): it turns nothing */
#define LONGEST_SAMPLE 0.1f

/* player space's relaxation: a turn about an axis tilted from the vertical
counts for more than its vertical part, up to the whole of it (the hand's
turn is never quite level) */
#define YAW_RELAX 1.41f

static float clamp01(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

/* how far toward a target an average over time constant `seconds` goes in
`step` seconds */
static float follow(float step, float seconds)
{
	return step / (seconds + step);
}

/* the soft tiered smoothing of a rate (n components, of which the
magnitude decides): what is direct, plus the smoothed part's average */
static void smooth_tiered(float *rate, float *smoothed, int count, float slowest, float fastest, float average_seconds,
	float step)
{
	float magnitude = 0.0f, direct, k = follow(step, average_seconds);
	int index;

	for (index = 0; index < count; index++)
		magnitude += rate[index] * rate[index];
	magnitude = sqrtf(magnitude);
	direct = clamp01((magnitude - slowest) / (fastest - slowest));
	for (index = 0; index < count; index++)
	{
		smoothed[index] += (rate[index] * (1.0f - direct) - smoothed[index]) * k;
		rate[index] = rate[index] * direct + smoothed[index];
	}
}

void gyro_aim_reset(struct gyro_aim *aim)
{
	memset(aim, 0, sizeof(*aim));
}

void gyro_aim_accelerometer(struct gyro_aim *aim, const float accelerometer[3])
{
	memcpy(aim->accelerometer, accelerometer, sizeof(aim->accelerometer));
	aim->accelerometer_known = 1;
}

void gyro_aim_calibrate(struct gyro_aim *aim)
{
	aim->calibrating = 1;
	aim->calibration_sum[0] = aim->calibration_sum[1] = aim->calibration_sum[2] = 0.0f;
	aim->calibration_seconds = 0.0f;
}

/* the up direction turned by the sample's rate and drawn toward the
accelerometer's */
static void up_update(struct gyro_aim *aim, const float rate[3], float seconds)
{
	float length, measured[3], turned[3];
	int index;

	length = sqrtf(aim->accelerometer[0] * aim->accelerometer[0] + aim->accelerometer[1] * aim->accelerometer[1] +
		aim->accelerometer[2] * aim->accelerometer[2]);
	if (!aim->accelerometer_known || !(length > 0.0f))
		return;
	for (index = 0; index < 3; index++)
		measured[index] = aim->accelerometer[index] / length;
	if (!aim->up_known)
	{
		memcpy(aim->up, measured, sizeof(aim->up));
		aim->up_known = 1;
		return;
	}
	/* (a direction fixed in the world, seen from the controller, turns
	against it: d/dt up = -rate x up) */
	turned[0] = aim->up[0] - (rate[1] * aim->up[2] - rate[2] * aim->up[1]) * seconds;
	turned[1] = aim->up[1] - (rate[2] * aim->up[0] - rate[0] * aim->up[2]) * seconds;
	turned[2] = aim->up[2] - (rate[0] * aim->up[1] - rate[1] * aim->up[0]) * seconds;
	if (fabsf(length - GRAVITY) < ACCELEROMETER_TRUSTED)
	{
		float k = follow(seconds, UP_CORRECTION_SECONDS);

		for (index = 0; index < 3; index++)
			turned[index] += (measured[index] - turned[index]) * k;
	}
	length = sqrtf(turned[0] * turned[0] + turned[1] * turned[1] + turned[2] * turned[2]);
	if (length > 0.0f)
	{
		for (index = 0; index < 3; index++)
			aim->up[index] = turned[index] / length;
	}
}

/* the automatic calibration's sample */
static void calibration_update(struct gyro_aim *aim, const float gyro[3], float seconds)
{
	float k = follow(seconds, STILL_MEAN_SECONDS);
	float rate_most = aim->bias_known ? STILL_RATE_MOST : STILL_RATE_MOST_FIRST;
	float gyro_off = 0.0f, accelerometer_off = 0.0f, rate = 0.0f;
	int index;

	if (!aim->means_known)
	{
		memcpy(aim->mean_gyro, gyro, sizeof(aim->mean_gyro));
		memcpy(aim->mean_accelerometer, aim->accelerometer, sizeof(aim->mean_accelerometer));
		aim->means_known = 1;
	}
	for (index = 0; index < 3; index++)
	{
		float bias = aim->bias_known ? aim->bias[index] : 0.0f;

		aim->mean_gyro[index] += (gyro[index] - aim->mean_gyro[index]) * k;
		aim->mean_accelerometer[index] += (aim->accelerometer[index] - aim->mean_accelerometer[index]) * k;
		gyro_off = fmaxf(gyro_off, fabsf(gyro[index] - aim->mean_gyro[index]));
		accelerometer_off = fmaxf(accelerometer_off, fabsf(aim->accelerometer[index] - aim->mean_accelerometer[index]));
		rate = fmaxf(rate, fabsf(gyro[index] - bias));
	}
	if (gyro_off < STILL_GYRO && accelerometer_off < STILL_ACCELEROMETER && rate < rate_most)
		aim->still_seconds += seconds;
	else
		aim->still_seconds = 0.0f;
	if (aim->still_seconds < STILL_SETTLE_SECONDS)
		return;
	if (!aim->bias_known)
	{
		memcpy(aim->bias, aim->mean_gyro, sizeof(aim->bias));
		aim->bias_known = 1;
		return;
	}
	k = follow(seconds, BIAS_SECONDS);
	for (index = 0; index < 3; index++)
		aim->bias[index] += (aim->mean_gyro[index] - aim->bias[index]) * k;
}

void gyro_aim_sample(struct gyro_aim *aim, const float gyro[3], float seconds,
	const struct gyro_aim_settings *settings, float turn[2])
{
	float rate[3], look[2];
	int index;

	turn[0] = turn[1] = 0.0f;
	if (!(seconds > 0.0f) || seconds > LONGEST_SAMPLE ||
		!isfinite(gyro[0]) || !isfinite(gyro[1]) || !isfinite(gyro[2]))
	{
		return;
	}
	for (index = 0; index < 3; index++)
		rate[index] = gyro[index] - (aim->bias_known ? aim->bias[index] : 0.0f);
	up_update(aim, rate, seconds);
	if (aim->calibrating)
	{
		for (index = 0; index < 3; index++)
			aim->calibration_sum[index] += gyro[index] * seconds;
		aim->calibration_seconds += seconds;
		if (aim->calibration_seconds >= GYRO_AIM_CALIBRATION_SECONDS)
		{
			for (index = 0; index < 3; index++)
				aim->bias[index] = aim->calibration_sum[index] / aim->calibration_seconds;
			aim->bias_known = 1;
			aim->calibrating = 0;
			aim->still_seconds = 0.0f;
		}
		aim->smoothed[0] = aim->smoothed[1] = 0.0f;
		return;
	}
	calibration_update(aim, gyro, seconds);

	/* the turn's axes: in player space, the yaw about the up direction,
	however the controller is held (but for its sideways tilt, which is
	the pitch's axis); in local space the controller's own */
	look[1] = rate[0];
	if (settings->player_space && aim->up_known)
	{
		float world = aim->up[1] * rate[1] + aim->up[2] * rate[2];
		float whole = sqrtf(rate[1] * rate[1] + rate[2] * rate[2]);
		float relaxed = fabsf(world) * YAW_RELAX;

		look[0] = (world < 0.0f ? -1.0f : 1.0f) * (relaxed < whole ? relaxed : whole);
	}
	else
	{
		look[0] = rate[1];
	}
	if (settings->smoothing)
		smooth_tiered(look, aim->smoothed, 2, SMOOTH_SLOWEST, SMOOTH_FASTEST, SMOOTH_SECONDS, seconds);
	turn[0] = look[0] * seconds * settings->sensitivity;
	turn[1] = look[1] * seconds * settings->sensitivity * settings->vertical_sensitivity;
}

/* ---------- the flick stick */

/* the stick on the rim, and back off it (lower, so that a stick held at
the rim does not flick again as it wavers) */
#define FLICK_RIM 0.9f
#define FLICK_RIM_LEFT 0.7f
/* the rim's turn smoothed as the gyroscope's is (radians a second) */
#define RIM_SMOOTH_SLOWEST 0.5f
#define RIM_SMOOTH_FASTEST 1.0f
#define RIM_SMOOTH_SECONDS 0.05f

#define PI_F 3.14159265358979f

/* the flick's share done at a share of its time: fast, then easing in */
static float flick_ease(float share)
{
	share = clamp01(share);
	return 1.0f - (1.0f - share) * (1.0f - share);
}

/* ---------- when the gyroscope's turn counts */

int gyro_aim_active(int mode, int held, int touched)
{
	switch (mode)
	{
	case GYRO_AIM_ON:
		return 1;
	case GYRO_AIM_HOLD:
		return held != 0;
	case GYRO_AIM_PAUSE:
		return !held;
	case GYRO_AIM_TOUCH:
		return touched > 0;
	default:
		return 0;
	}
}

#define VALVE 0x28de

int gyro_stick_touch_known(unsigned short vendor, unsigned short product)
{
	return vendor == VALVE && (product == 0x1205 || (product >= 0x1302 && product <= 0x1305));
}

int gyro_steam_deck(unsigned short vendor, unsigned short product)
{
	return vendor == VALVE && product == 0x1205;
}

void flick_stick_reset(struct flick_stick *flick)
{
	memset(flick, 0, sizeof(*flick));
	flick->flick_seconds = FLICK_STICK_SECONDS;
}

float flick_stick_update(struct flick_stick *flick, float x, float y, float seconds)
{
	float magnitude = sqrtf(x * x + y * y);
	float turn = 0.0f;

	if (!(seconds >= 0.0f) || !isfinite(x) || !isfinite(y))
		return 0.0f;
	if (magnitude >= (flick->on_rim ? FLICK_RIM_LEFT : FLICK_RIM))
	{
		float angle = atan2f(x, y);

		if (!flick->on_rim)
		{
			/* (a flick not yet done finishes at once) */
			turn += flick->flick_size * (1.0f - flick_ease(flick->flick_seconds / FLICK_STICK_SECONDS));
			/* a turn to where the stick points: to the right is a yaw to
			the right, negative */
			flick->flick_size = -angle;
			flick->flick_seconds = 0.0f;
			flick->smoothed = 0.0f;
			flick->on_rim = 1;
		}
		else
		{
			float change = angle - flick->angle;

			while (change > PI_F)
				change -= 2.0f * PI_F;
			while (change < -PI_F)
				change += 2.0f * PI_F;
			if (seconds > 0.0f)
			{
				float rate = change / seconds;

				smooth_tiered(&rate, &flick->smoothed, 1, RIM_SMOOTH_SLOWEST, RIM_SMOOTH_FASTEST,
					RIM_SMOOTH_SECONDS, seconds);
				turn -= rate * seconds;
			}
			else
			{
				turn -= change;
			}
		}
		flick->angle = angle;
	}
	else
	{
		flick->on_rim = 0;
		flick->smoothed = 0.0f;
	}
	if (flick->flick_seconds < FLICK_STICK_SECONDS)
	{
		float before = flick_ease(flick->flick_seconds / FLICK_STICK_SECONDS);

		flick->flick_seconds = fminf(flick->flick_seconds + seconds, FLICK_STICK_SECONDS);
		turn += (flick_ease(flick->flick_seconds / FLICK_STICK_SECONDS) - before) * flick->flick_size;
	}
	return turn;
}
