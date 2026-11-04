/* A player's controls as the host's sub-steps ran them, to a client its
plan does not send the player's every control to
(network_substeps_live_far_due): a press (the held controls changed, a stick
pushed or let go, the trigger pulled) goes as it comes, a stick's drift or a
turn of the aim does not. */
#include <math.h>
#include "harness.h"
enum { MAXIMUM_TRACKED_PLAYERS = 4, PLAYER = 1 };
typedef unsigned short word;
typedef struct { real i, j; } real_vector2d;
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define ABS(x) ((x>=0) ? (x) : -(x))
#include "config.inc"
static struct
{
	struct live_control ran_live[MAXIMUM_TRACKED_PLAYERS];
} network_substeps_globals;
#include "under_test.inc"

/* the player's latest press as the host ran it: held flags, sticks, trigger */
static void latest(unsigned long held, real throttle_i, real throttle_j, real trigger)
{
	struct live_control *live = &network_substeps_globals.ran_live[PLAYER];

	memset(live, 0, sizeof(*live));
	live->valid = TRUE;
	live->pending = TRUE;
	live->host_update = TRUE;
	live->key = 100;
	live->count = 4;
	live->held_flags = held;
	live->throttle.i = throttle_i;
	live->throttle.j = throttle_j;
	live->primary_trigger = trigger;
	live->aim.i = 1.0f;
}

static void expect(char const *name, struct live_control_far *last, boolean due)
{
	boolean was = network_substeps_live_far_due(PLAYER, last);

	CHECK(was == due, "%s: %s, not %s", name, was ? "due" : "not due", due ? "due" : "not due");
}

int main(int argc, char **argv) {
	char const *name = argc > 1 ? argv[1] : "";
	struct live_control_far last;

	memset(&last, 0, sizeof(last));
	/* (the first: due, and noted) */
	latest(0, 0.0f, 0.0f, 0.0f);
	expect(name, &last, TRUE);
	if (!strcmp(name, "first"))
		CHECK(last.valid, "%s: not noted", name);
	else if (!strcmp(name, "same"))
	{
		latest(0, 0.0f, 0.0f, 0.0f);
		expect(name, &last, FALSE);
	}
	else if (!strcmp(name, "turned"))
	{
		/* (the aim alone: the relayed input brings it) */
		latest(0, 0.0f, 0.0f, 0.0f);
		network_substeps_globals.ran_live[PLAYER].aim.i = 0.0f;
		network_substeps_globals.ran_live[PLAYER].aim.j = 1.0f;
		expect(name, &last, FALSE);
	}
	else if (!strcmp(name, "drift"))
	{
		latest(0, 0.1f, -0.1f, 0.1f);
		expect(name, &last, FALSE);
	}
	else if (!strcmp(name, "push"))
	{
		latest(0, 1.0f, 0.0f, 0.0f);
		expect(name, &last, TRUE);
		/* (held: nothing new) */
		latest(0, 0.95f, 0.0f, 0.0f);
		expect(name, &last, FALSE);
		/* (let go) */
		latest(0, 0.0f, 0.0f, 0.0f);
		expect(name, &last, TRUE);
	}
	else if (!strcmp(name, "strafe"))
	{
		latest(0, 0.0f, -1.0f, 0.0f);
		expect(name, &last, TRUE);
	}
	else if (!strcmp(name, "pull"))
	{
		latest(0, 0.0f, 0.0f, 1.0f);
		expect(name, &last, TRUE);
	}
	else if (!strcmp(name, "held"))
	{
		latest(FLAG(3), 0.0f, 0.0f, 0.0f);
		expect(name, &last, TRUE);
		latest(0, 0.0f, 0.0f, 0.0f);
		expect(name, &last, TRUE);
	}
	else if (!strcmp(name, "sent"))
	{
		/* (sent on already) */
		latest(FLAG(3), 0.0f, 0.0f, 1.0f);
		network_substeps_globals.ran_live[PLAYER].pending = FALSE;
		expect(name, &last, FALSE);
	}
	else if (!strcmp(name, "player"))
	{
		CHECK(!network_substeps_live_far_due(MAXIMUM_TRACKED_PLAYERS, &last), "%s: a player past the last", name);
		CHECK(!network_substeps_live_far_due(-1, &last), "%s: a player before the first", name);
	}
	else { fprintf(stderr, "no case %s\n", name); return 2; }
	return 0;
}
