/* A client's correction of another player standing on an elevator here: the
host's word has them on the host's floor, a phase of the platform away, and
the height is left to this machine's floor (distributed_remote_rider_height_kept);
anything else (off the floor, in the air, past the phase or across) is the
host's word as it is. */
#include "harness.h"
#include <math.h>
enum { _object_mask_biped = 1, _object_mask_machine = 2, _biped_airborne_bit = 0 };
struct fake_object {
    long mask;
    struct { unsigned flags; long support_surface_index, elevator_object_index; char elevator_ticks; } biped;
};
#define biped_datum fake_object
#define device_datum fake_object
static struct fake_object objects[3];
static void *object_try_and_get_and_verify_type(long i, long mask) {
    return i >= 0 && i < 3 && (objects[i].mask & mask) ? &objects[i] : NULL;
}
#include "config.inc"
#include "under_test.inc"
int main(int argc, char **argv) {
    char const *case_name = argc > 1 ? argv[1] : "";
    real_vector3d error = {0.02f, 0.01f, -0.3f};
    boolean kept;
    boolean expect_kept = !strcmp(case_name, "riding") || !strcmp(case_name, "riding-up");
    objects[1].mask = _object_mask_biped; objects[2].mask = _object_mask_machine;
    /* (on a machine's floor its physics has no structure surface) */
    objects[1].biped.support_surface_index = NONE;
    objects[1].biped.elevator_object_index = 2; objects[1].biped.elevator_ticks = 60;
    if (!strcmp(case_name, "riding-up")) error.k = 0.3f;
    if (!strcmp(case_name, "jump")) objects[1].biped.flags = FLAG(_biped_airborne_bit);
    if (!strcmp(case_name, "walked-off")) objects[1].biped.elevator_ticks = 59;
    if (!strcmp(case_name, "on-foot")) objects[1].biped.elevator_object_index = NONE;
    if (!strcmp(case_name, "far")) error.k = -6.0f;
    if (!strcmp(case_name, "across")) error.i = 2.0f;
    kept = distributed_remote_rider_height_kept(1, &error);
    if (expect_kept) {
        CHECK(kept && error.k == 0.0f, "%s: the host's floor's height taken (%f), not this machine's", case_name, error.k);
        CHECK(error.i == 0.02f && error.j == 0.01f, "%s: the error across changed", case_name);
    } else
        CHECK(!kept && error.k != 0.0f, "%s: the host's word on the height wrongly set aside", case_name);
    return 0;
}
