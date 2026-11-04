/* Another client's player corrected against this machine's own history of it
(distributed_correct_remote_unit, #287 stage 2c): the host's word names the
end of their client's tick, compared with where this machine had its copy
after running that tick's input. Positions along x; the copy runs ahead on
fresher input. The apply stub puts the unit where it is told (no blend). A
word of one of the host's sub-steps (#287 stage 2d) is compared with where
this machine had the copy as the same sub-step of that tick ended. */
#include "harness.h"
typedef unsigned short word;
enum { _distributed_unit_predicted_bit = 7, MAXIMUM_TRACKED_PLAYERS = 4 };
#define DISTRIBUTED_VELOCITY_SCALE 1024.0f
#define DISTRIBUTED_UNIT_SCALE 32767.0f
struct distributed_vector { short i, j, k; };
struct distributed_unit_state {
    byte flags; short predicted_time; real_point3d position; struct distributed_vector velocity, forward, up;
    real_vector3d fine_velocity; boolean fine;
};
struct fake_object { struct { real_point3d position; real_vector3d translational_velocity, forward, up; } object; };
#define object_datum fake_object
static struct fake_object objects[4];
static struct fake_object *object_get(long i) { return &objects[i]; }
static boolean substeps_active = TRUE;
static long forwarded_key = 100;
static boolean network_substeps_active(void) { return substeps_active; }
static long network_substeps_forwarded_key(short player_index) { (void)player_index; return forwarded_key; }
static void distributed_vector_unpack(struct distributed_vector const *v, real scale, real_vector3d *r) {
    r->i = v->i / scale; r->j = v->j / scale; r->k = v->k / scale;
}
static void distributed_vector_pack(real_vector3d const *v, real scale, struct distributed_vector *r) {
    r->i = (short)(v->i * scale); r->j = (short)(v->j * scale); r->k = (short)(v->k * scale);
}
static real distance_squared3d(real_point3d const *a, real_point3d const *b) {
    return (a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y) + (a->z - b->z) * (a->z - b->z);
}
/* (the tests' reports) */
static struct { long remote_corrections, remote_history_corrections, remote_history_kept; } distributed_statistics;
static int applied;
static real_vector3d applied_velocity;
static boolean distributed_apply_state(long i, struct distributed_unit_state const *s, real_point3d const *p,
    real tolerance, real blend, real speed, real fall) {
    (void)tolerance; (void)blend; (void)speed; (void)fall;
    objects[i].object.position = *p;
    applied_velocity = s->fine_velocity;
    applied++;
    return TRUE;
}
#include "config.inc"
#include "under_test.inc"
int main(int argc, char **argv) {
    char const *name = argc > 1 ? argv[1] : "";
    struct distributed_unit_state state = {0};
    boolean handled;
    short tick;
    short substep = NONE;
    short k;
    /* this machine ran the input of the other client's ticks 96 to 100,
    0.1 a tick: after 98's it had the copy at 9.8, now at 10.0 */
    for (tick = 96; tick <= 100; tick++) {
        struct distributed_remote_position *noted = &distributed_remote_positions[1][tick & (REMOTE_POSITION_TICKS - 1)];
        noted->key = tick; noted->unit_index = 1; noted->position.x = tick / 10.0f;
        noted->velocity.i = 0.1f;
        /* (and as each of the tick's first three sub-steps of four ended) */
        for (k = 0; k < MAXIMUM_SIM_SUBSTEPS - 1; k++) {
            noted = &distributed_remote_substep_positions[1][tick & (REMOTE_POSITION_TICKS - 1)][k];
            noted->key = tick; noted->unit_index = 1; noted->position.x = (tick - 1) / 10.0f + 0.025f * (k + 1);
            noted->velocity.i = 0.1f;
        }
    }
    objects[1].object.position.x = 10.0f;
    objects[1].object.translational_velocity.i = 0.1f;
    /* the host's word: the end of their tick 98, where it had them at 9.8,
    a round trip behind this machine's copy */
    state.flags = FLAG(_distributed_unit_predicted_bit);
    state.predicted_time = 99;
    state.position.x = 9.8f;
    state.velocity.i = (short)(0.1f * DISTRIBUTED_VELOCITY_SCALE);
    if (!strcmp(name, "wrapped")) {
        /* (the tick in 16 bits, past 65535) */
        for (tick = 0; tick < REMOTE_POSITION_TICKS; tick++)
            distributed_remote_positions[1][tick].key = NONE;
        distributed_remote_positions[1][65634 & (REMOTE_POSITION_TICKS - 1)].key = 65634;
        distributed_remote_positions[1][65634 & (REMOTE_POSITION_TICKS - 1)].unit_index = 1;
        distributed_remote_positions[1][65634 & (REMOTE_POSITION_TICKS - 1)].position.x = 9.8f;
        forwarded_key = 65636;
        state.predicted_time = (short)(word)65635;
    }
    if (!strcmp(name, "apart")) state.position.x = 9.5f;
    if (!strcmp(name, "no-history")) state.predicted_time = 40;
    if (!strcmp(name, "unpredicted")) state.flags = 0;
    if (!strcmp(name, "relayed")) forwarded_key = NONE;
    if (!strcmp(name, "inactive")) substeps_active = FALSE;
    /* the host's word as its sub-step 0 of the tick that ran their 98 ended
    (9.725), and as its sub-step 1 did, 0.3 behind (9.75) */
    if (!strcmp(name, "substep-ahead")) { substep = 0; state.position.x = 9.725f; }
    if (!strcmp(name, "substep-apart")) { substep = 1; state.position.x = 9.45f; }
    if (!strcmp(name, "substep-no-history")) {
        substep = 2;
        distributed_remote_substep_positions[1][98 & (REMOTE_POSITION_TICKS - 1)][2].key = NONE;
    }
    handled = distributed_correct_remote_unit(1, 1, &state, substep);
    if (!strcmp(name, "ahead") || !strcmp(name, "wrapped")) {
        /* (where it had them then: not pulled back from where fresher input
        has it now) */
        CHECK(handled, "%s: the copy's own tick was not found", name);
        CHECK(!applied && objects[1].object.position.x == 10.0f, "%s: a copy running ahead was pulled back to %f",
            name, objects[1].object.position.x);
        CHECK(distributed_statistics.remote_history_kept == 1, "%s: the pull-back avoided not counted", name);
    } else if (!strcmp(name, "apart")) {
        /* (0.3 behind where it had them: moved by that, keeping what it did
        since; the ticks since moved with it) */
        CHECK(handled && applied == 1, "a disagreement was not corrected");
        CHECK(objects[1].object.position.x > 9.69f && objects[1].object.position.x < 9.71f,
            "corrected to %f, not 9.7", objects[1].object.position.x);
        CHECK(distributed_remote_positions[1][100 & (REMOTE_POSITION_TICKS - 1)].position.x < 9.71f,
            "the ticks since were not moved with it");
        CHECK(distributed_remote_positions[1][97 & (REMOTE_POSITION_TICKS - 1)].position.x > 9.69f,
            "a tick before the named one was moved");
        CHECK(applied_velocity.i > 0.099f && applied_velocity.i < 0.101f, "its speed taken as %f", applied_velocity.i);
    } else if (!strcmp(name, "substep-ahead")) {
        /* (where it had them as that sub-step ended, not as the tick did) */
        CHECK(handled && !applied && objects[1].object.position.x == 10.0f,
            "a sub-step's word pulled the copy back to %f", objects[1].object.position.x);
    } else if (!strcmp(name, "substep-apart")) {
        struct distributed_remote_position (*sub)[MAXIMUM_SIM_SUBSTEPS - 1] = distributed_remote_substep_positions[1];
        CHECK(handled && applied == 1, "a sub-step's disagreement was not corrected");
        CHECK(objects[1].object.position.x > 9.69f && objects[1].object.position.x < 9.71f,
            "corrected to %f, not 9.7", objects[1].object.position.x);
        /* (from the sub-step named on: the tick's end, its later sub-steps and
        the ticks after; not its sub-steps before, nor the ticks before) */
        CHECK(sub[98 & (REMOTE_POSITION_TICKS - 1)][1].position.x < 9.46f &&
            sub[98 & (REMOTE_POSITION_TICKS - 1)][2].position.x < 9.48f &&
            sub[99 & (REMOTE_POSITION_TICKS - 1)][0].position.x < 9.53f &&
            distributed_remote_positions[1][98 & (REMOTE_POSITION_TICKS - 1)].position.x < 9.51f,
            "the sub-steps and ticks since were not moved with it");
        CHECK(sub[98 & (REMOTE_POSITION_TICKS - 1)][0].position.x > 9.72f &&
            sub[97 & (REMOTE_POSITION_TICKS - 1)][2].position.x > 9.67f &&
            distributed_remote_positions[1][97 & (REMOTE_POSITION_TICKS - 1)].position.x > 9.69f,
            "a sub-step before the named one was moved");
    } else if (!strcmp(name, "no-history") || !strcmp(name, "unpredicted") || !strcmp(name, "relayed") ||
        !strcmp(name, "inactive") || !strcmp(name, "substep-no-history")) {
        /* (put where the host has it, as before: the caller's) */
        CHECK(!handled && !applied, "%s: taken without a tick of its own to compare", name);
    } else {
        fprintf(stderr, "no case %s\n", name);
        return 2;
    }
    return 0;
}
