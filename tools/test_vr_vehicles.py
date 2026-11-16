"""The VR mode's vehicles: the eye's way into a seat and out of it (port/linux/game/vr_seat.c),
and steering with the sticks while seated (port/linux/src/vr.c), compiled from the sources
against a fake world.

- Getting in and out of a vehicle by the action (X) with vr.seat_motion = "still" (the default)
  stays in the first person: the view never darkens, the eye never moves faster than the ride's
  limit (3 m/s) however the game's climbing animation jumps the body's head, it ends in the seat
  (where the head is once seated, held there in the vehicle as it drives), and ends back at the
  first person's eye once out. With "blink" (the older way) it darkens, as before.
- Back from a death (a respawn, a checkpoint's revert), the eye is the new one's from the first frame: not the
  death's eye (its cam's vantage, or where the player died) held in view while a blink darkens, outside the head
  as the helmet's view switches on (port/linux/game/vr_death.c calls vr_seat_eye_cut).
- With the hands off a Ghost's handlebars or a Warthog's wheel, a stick pushed sideways steers:
  the Ghost turns toward the push at up to 90 degrees a second (the game's own turn toward the
  facing, update_alien_scout_physics), the Warthog's wheels turn toward their lock, the other way
  as it reverses.
- The player's body in a seat (port/linux/game/first_person_legs.c), posed from the Chief as the
  game seats him driving the Scorpion: its neck under the head as recentred, its legs the seat's,
  its upper body turning toward the head about its hips (no farther than 0.6 radians), its head
  and arms folded away for the VR mode's own; standing in the hatch, its legs straight down. And
  the Scorpion's cockpits made around it (tools/vr_cabin_model.py): nothing of them within its
  armour, its seat and floor under it, the header the generator's.
"""
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SEAT = ROOT / "port/linux/game/vr_seat.c"
VR = ROOT / "port/linux/src/vr.c"


def function(source, name):
    match = re.search(r"^(?:static )?[\w *]+\b" + name + r"\(\s*[^;{]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError(f"function not found: {name}")
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def between(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


def defines(source, *names):
    out = []
    for name in names:
        match = re.search(r"^#define " + name + r"\b.*$", source, re.M)
        if not match:
            raise ValueError(f"constant not found: {name}")
        out.append(match.group(0))
    return "\n".join(out) + "\n"


def compiler():
    found = next((shutil.which(name) for name in (os.environ.get("CC", "clang"), "clang", "cc") if shutil.which(name)),
                 None)
    if not found:
        pytest.skip("a C compiler is needed")
    return found


def build(tmp_path, source, name):
    c = tmp_path / f"{name}.c"
    exe = tmp_path / name
    c.write_text(source)
    subprocess.run([compiler(), "-std=gnu99", "-O1", "-w", "-Werror=implicit-function-declaration", str(c), "-o",
                    str(exe), "-lm"], check=True)
    return exe


# ---------- the eye's way into a seat and out (vr_seat.c)

SEAT_WORLD = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
typedef float real;
typedef struct {real x,y,z;} real_point3d;
typedef struct {real i,j,k;} real_vector3d;
typedef struct {real scale; real_vector3d forward,left,up; real_point3d position;} real_matrix4x3;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
enum {_unit_state_seated = 1, _unit_state_entering_seat, _unit_state_exiting_seat};
enum {_director_perspective_first_person = 0, _director_perspective_following = 1};
struct unit_datum {struct {long parent_object_index;} object; struct {short parent_seat_index; struct {short state;} animation;} unit;};

static struct unit_datum the_unit;
static real_matrix4x3 vehicle_node = {1.0f, {1,0,0}, {0,1,0}, {0,0,1}, {0,0,0}};
static real_point3d the_head;
static const char *seat_motion_setting = "still";
static unsigned long changes = 1;
static real vignette_asked;
static int vehicle_gone;

static const char *config_string(const char *name) {(void)name; return seat_motion_setting;}
static unsigned long config_changes(void) {return changes;}
#define platform_log(...) ((void)0)
static struct unit_datum *unit_get(long index) {(void)index; return &the_unit;}
static void *object_try_and_get(long index) {return index == 7 && !vehicle_gone ? &vehicle_node : NULL;}
static real_matrix4x3 const *object_get_node_matrix(long index, short node) {(void)index; (void)node; return &vehicle_node;}
static void unit_get_head_position(long index, real_point3d *head) {(void)index; *head = the_head;}
static short director_get_perspective(short local) {(void)local; return _director_perspective_first_person;}
static short vr_cabin_seat_eye(long index, real_point3d *eye) {(void)index; (void)eye; return NONE;}
static boolean vr_cabin_feed(short index) {return index != NONE;}
static boolean vr_climb_exiting(void) {return FALSE;}
static boolean vr_climb_active(void) {return FALSE;}
static void halo_vr_comfort_vignette(float amount) {vignette_asked = amount;}
static real dot(real_vector3d const *a, real_vector3d const *b) {return a->i*b->i + a->j*b->j + a->k*b->k;}
static real magnitude_squared3d(real_vector3d const *v) {return dot(v, v);}
static real distance3d(real_point3d const *a, real_point3d const *b)
{real x = a->x-b->x, y = a->y-b->y, z = a->z-b->z; return sqrtf(x*x + y*y + z*z);}
static void vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *v)
{v->i = b->x-a->x; v->j = b->y-a->y; v->k = b->z-a->z;}
static void scale_vector3d(real_vector3d const *v, real s, real_vector3d *out) {out->i = v->i*s; out->j = v->j*s; out->k = v->k*s;}
static void point_from_line3d(real_point3d const *p, real_vector3d const *v, real t, real_point3d *out)
{out->x = p->x + v->i*t; out->y = p->y + v->j*t; out->z = p->z + v->k*t;}
static void matrix4x3_transform_vector(real_matrix4x3 const *m, real_vector3d const *v, real_vector3d *out)
{real_vector3d r; r.i = m->forward.i*v->i + m->left.i*v->j + m->up.i*v->k; r.j = m->forward.j*v->i + m->left.j*v->j + m->up.j*v->k;
 r.k = m->forward.k*v->i + m->left.k*v->j + m->up.k*v->k; *out = r;}
static void matrix4x3_inverse_transform_vector(real_matrix4x3 const *m, real_vector3d const *v, real_vector3d *out)
{real_vector3d r; r.i = dot(&m->forward, v); r.j = dot(&m->left, v); r.k = dot(&m->up, v); *out = r;}
static void matrix4x3_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *out)
{real_vector3d v = {p->x, p->y, p->z}; matrix4x3_transform_vector(m, &v, &v);
 out->x = v.i + m->position.x; out->y = v.j + m->position.y; out->z = v.k + m->position.z;}
static void matrix4x3_inverse_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *out)
{real_vector3d v = {p->x - m->position.x, p->y - m->position.y, p->z - m->position.z};
 matrix4x3_inverse_transform_vector(m, &v, &v); out->x = v.i; out->y = v.j; out->z = v.k;}
"""

SEAT_STUBS = r"""
static void feed_ride(long unit_index, long vehicle_index, real_point3d const *was, real_point3d const *head,
    real_point3d const *cockpit_eye, real_point3d *eye, real seconds) {abort();}
static boolean ride_arrived(long vehicle_index, real_point3d const *place) {abort();}
"""

SEAT_MAIN = r"""
#define DT (1.0f / 60.0f)
static real_point3d eye_was;
static int frames, dark_frames;
static real largest_step, darkest;

/* a frame: the game's camera (the first person's eye) and the unit seated (else NONE) */
static real_point3d frame(long seated, real_point3d camera)
{
    real_point3d eye = camera;
    vr_seat_eye(seated, TRUE, DT, &eye);
    if (frames++ > 0 && !vehicle_moving)
        largest_step = MAX(largest_step, distance3d(&eye_was, &eye));
    if (vr_seat_darkness() > 0.0f)
        dark_frames++;
    darkest = MAX(darkest, vr_seat_darkness());
    eye_was = eye;
    return eye;
}

int main(int argc, char **argv)
{
    real_point3d stand = {0.0f, 0.0f, 0.6f}, seat = {1.0f, 0.3f, 0.5f}, out = {2.0f, 0.0f, 0.6f}, eye, start;
    int i, moving = argc > 2 && !strcmp(argv[2], "moving");
    real limit = RIDE_FASTEST / METRES_PER_WORLD_UNIT * DT * 1.001f;

    seat_motion_setting = argv[1];
    the_unit.object.parent_object_index = 7;
    the_unit.unit.parent_seat_index = 0;
    /* on foot */
    for (i = 0; i < 30; i++)
        eye = frame(NONE, stand);
    /* climbing in: the game's animation, the head in half-metre steps every few frames, and a bob */
    the_unit.unit.animation.state = _unit_state_entering_seat;
    for (i = 0; i < 54; i++)
    {
        real t = (real)(i / 6 * 6) / 54.0f;
        the_head.x = stand.x + (seat.x - stand.x) * t;
        the_head.y = stand.y + (seat.y - stand.y) * t;
        the_head.z = stand.z + (seat.z - stand.z) * t + 0.08f * (real)((i / 3) % 2);
        eye = frame(7, stand);
    }
    /* seated: the seat's animation moves the head about; the eye stays where it was seated, in the
    vehicle, as the vehicle drives on */
    the_unit.unit.animation.state = _unit_state_seated;
    the_head = seat;
    eye = frame(7, stand);
    for (i = 0; i < 120; i++)
    {
        the_head.z = seat.z + 0.05f * sinf((real)i);
        if (moving && i >= 60)
        {
            vehicle_moving = TRUE;
            vehicle_node.position.x += 3.0f * DT;
        }
        eye = frame(7, stand);
        if (i == 59)
            printf("seated %f %f %f shown %d\n", eye.x, eye.y, eye.z, vr_seat_seated_shown());
    }
    if (moving)
        printf("moved seated %f %f %f\n", eye.x - vehicle_node.position.x, eye.y - vehicle_node.position.y,
            eye.z - vehicle_node.position.z);
    vehicle_moving = FALSE;
    frames = 0;
    /* climbing out: from the seat to beside the vehicle (wherever it drove to), in steps, the head in
    the world */
    the_unit.unit.animation.state = _unit_state_exiting_seat;
    point_from_line3d(&seat, (real_vector3d *)&vehicle_node.position, 1.0f, &start);
    point_from_line3d(&out, (real_vector3d *)&vehicle_node.position, 1.0f, &out);
    for (i = 0; i < 30; i++)
    {
        real t = (real)(i / 5 * 5) / 30.0f;
        the_head.x = start.x + (out.x - start.x) * t;
        the_head.y = start.y + (out.y - start.y) * t;
        the_head.z = start.z + (out.z - start.z) * t;
        eye = frame(7, out);
    }
    /* out, standing */
    for (i = 0; i < 90; i++)
        eye = frame(NONE, out);
    printf("out %f %f %f\n", eye.x - out.x, eye.y - out.y, eye.z - out.z);
    printf("largest step %f limit %f dark frames %d darkest %f blinks %ld glides %ld\n", largest_step, limit,
        dark_frames, darkest, vr_seat.blinks, vr_seat.glides);
    return 0;
}
"""


def seat_source(text=None):
    text = text or SEAT.read_text()
    source = SEAT_WORLD + "static int vehicle_moving;\n"
    source += between(text, "#define METRES_PER_WORLD_UNIT", "/* ---------- the view */")
    source += SEAT_STUBS
    for name in ("seat_motion", "seat_still", "seat_blinks", "vehicle_to_local", "vehicle_from_local",
                 "shown_position", "ride_spring", "glide_left", "glide_to_frame", "glide_from_frame",
                 "vr_seat_eye", "vr_seat_seated_shown", "vr_seat_darkness"):
        source += function(text, name) + "\n"
    return source + SEAT_MAIN


def run_seat(tmp_path, motion, *extra, text=None, name="seat"):
    exe = build(tmp_path, seat_source(text), name)
    result = subprocess.run([str(exe), motion, *extra], check=True, capture_output=True, text=True).stdout
    values = {}
    for line in result.splitlines():
        words = line.split()
        if words[0] == "seated":
            values["seated"] = [float(w) for w in words[1:4]]
            values["shown"] = int(words[5])
        elif words[0] == "moved":
            values["moved"] = [float(w) for w in words[2:5]]
        elif words[0] == "out":
            values["out"] = [float(w) for w in words[1:4]]
        elif words[0] == "largest":
            values.update(step=float(words[2]), limit=float(words[4]), dark=int(words[7]), darkest=float(words[9]),
                          blinks=int(words[11]), glides=int(words[13]))
    return values


def test_getting_in_and_out_stays_in_the_first_person(tmp_path):
    v = run_seat(tmp_path, "still")
    # never dark, never a blink: the eye glides
    assert v["dark"] == 0 and v["blinks"] == 0
    assert v["glides"] >= 2
    # never faster than the ride's limit, though the animation jumps the head 0.16 world units (half a
    # metre) at a time
    assert v["step"] <= v["limit"] + 1e-6
    # in the seat: where the head was once seated, not bobbing with the seat's animation
    assert v["seated"] == pytest.approx([1.0, 0.3, 0.5], abs=1e-3)
    assert v["shown"] == 1
    # and out: the first person's eye, exactly
    assert v["out"] == pytest.approx([0.0, 0.0, 0.0], abs=1e-4)


def test_the_seated_eye_rides_with_the_vehicle(tmp_path):
    v = run_seat(tmp_path, "still", "moving")
    assert v["moved"] == pytest.approx([1.0, 0.3, 0.5], abs=1e-3)
    assert v["dark"] == 0
    assert v["out"] == pytest.approx([0.0, 0.0, 0.0], abs=1e-4)


def test_blink_setting_still_blinks(tmp_path):
    v = run_seat(tmp_path, "blink")
    assert v["blinks"] >= 2 and v["darkest"] == pytest.approx(1.0)
    assert v["glides"] == 0
    # (held where the head was as the blink ended: the seat's animation bobs it 5 cm about the seat)
    assert v["seated"] == pytest.approx([1.0, 0.3, 0.5], abs=0.051)


def test_negative_control_without_the_settling_glide_it_blinks(tmp_path):
    text = SEAT.read_text()
    mutated = text.replace("vr_seat.glide = _glide_settling;", "vr_seat.glide = _glide_none;")
    assert mutated != text
    v = run_seat(tmp_path, "still", text=mutated, name="seat_mutated")
    assert v["dark"] > 0


def test_negative_control_without_following_the_head_jumps(tmp_path):
    text = SEAT.read_text()
    # (the head's path not followed and smoothed: only the jump into it closed, the climb's steps seen)
    mutated = text.replace("boolean following = (source == _seat_source_getting_in",
                           "boolean following = FALSE && (source == _seat_source_getting_in")
    assert mutated != text
    v = run_seat(tmp_path, "still", text=mutated, name="seat_unfollowed")
    assert v["step"] > v["limit"]


# ---------- back from a death (vr_death.c, vr_seat.c)

RESPAWN_MAIN = r"""
#define DT (1.0f / 90.0f)

int main(int argc, char **argv)
{
    /* the death cam's vantage (3 m from the body) the eye while dead, and the new unit's eye back */
    real_point3d vantage = {4.0f, 1.0f, 1.2f}, head = {10.0f, -3.0f, 0.6f}, eye;
    real farthest = 0.0f, darkest = 0.0f;
    int i, cut = argc > 1 && !strcmp(argv[1], "cut");

    perspective_now = 3;
    for (i = 0; i < 30; i++)
    {
        eye = vantage;
        vr_seat_eye(NONE, TRUE, DT, &eye);
    }
    /* back: the game's camera the new first person's eye, at once */
    perspective_now = _director_perspective_first_person;
    if (cut)
        vr_seat_eye_cut();
    for (i = 0; i < 45; i++)
    {
        eye = head;
        vr_seat_eye(NONE, TRUE, DT, &eye);
        farthest = MAX(farthest, distance3d(&eye, &head));
        darkest = MAX(darkest, vr_seat_darkness());
        if (i == 0)
            printf("first %f\n", distance3d(&eye, &head));
    }
    printf("farthest %f darkest %f\n", farthest, darkest);
    return 0;
}
"""


def run_respawn(tmp_path, *args, text=None, name="respawn"):
    text = text or SEAT.read_text()
    source = "static short perspective_now;\n" + SEAT_WORLD.replace(
        "return _director_perspective_first_person;}", "return perspective_now;}")
    source += "static int vehicle_moving;\n"
    source += between(text, "#define METRES_PER_WORLD_UNIT", "/* ---------- the view */")
    source += SEAT_STUBS
    for name_ in ("seat_motion", "seat_still", "seat_blinks", "vehicle_to_local", "vehicle_from_local",
                  "shown_position", "ride_spring", "glide_left", "glide_to_frame", "glide_from_frame",
                  "vr_seat_eye", "vr_seat_eye_cut", "vr_seat_darkness"):
        source += function(text, name_) + "\n"
    exe = build(tmp_path, source + RESPAWN_MAIN, name)
    out = subprocess.run([str(exe), *args], check=True, capture_output=True, text=True).stdout.split()
    return {"first": float(out[1]), "farthest": float(out[3]), "darkest": float(out[5])}


def test_back_from_a_death_the_eye_is_the_new_ones_at_once(tmp_path):
    v = run_respawn(tmp_path, "cut")
    assert v["first"] == pytest.approx(0.0, abs=1e-5) and v["farthest"] == pytest.approx(0.0, abs=1e-5)
    # (the death's own dark and switching bring it up: no blink of the seat's)
    assert v["darkest"] == 0.0


def test_without_the_cut_the_deaths_eye_is_held_in_view(tmp_path):
    # (the negative control: as it was, the blink holds the death cam's vantage as it darkens)
    v = run_respawn(tmp_path, name="respawn_held")
    assert v["first"] > 1.0 and v["darkest"] > 0.0


def test_negative_control_the_cut_that_keeps_the_eye_fails(tmp_path):
    text = SEAT.read_text()
    body = function(text, "vr_seat_eye_cut")
    mutated = text.replace(body, body.replace("vr_seat.shown_valid = FALSE;", ""))
    assert mutated != text
    v = run_respawn(tmp_path, "cut", text=mutated, name="respawn_mutated")
    assert v["first"] > 1.0


# ---------- steering with the sticks (vr.c)

STEER_WORLD = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
enum {HALO_VR_SEAT_RIDE, HALO_VR_SEAT_BARS, HALO_VR_SEAT_WHEEL, HALO_VR_SEAT_TURRET};
"""

STEER_MAIN = r"""
/* the game's turn of the Ghost toward the facing, each tick (vr.c's HANDLEBAR_* comment:
update_alien_scout_physics): the lesser of sqrt(2 a e) and e / 2 for an error of e */
static float ghost_rate(float error)
{
    float a = HANDLEBAR_GHOST_TURN_ACCELERATION, e = fabsf(error), tick = sqrtf(2.0f * a * e);
    if (tick > e / 2.0f)
        tick = e / 2.0f;
    return tick * HANDLEBAR_TICKS_PER_SECOND;
}

int main(void)
{
    float lock_left = 0.52f, lock_right = 0.5f;
    printf("dead %g %g\n", stick_facing(HALO_VR_SEAT_BARS, 0.15f, -0.19f, lock_left, lock_right, 1.0f),
        stick_facing(HALO_VR_SEAT_WHEEL, 0.0f, 0.0f, lock_left, lock_right, 1.0f));
    printf("barsright %g rate %g\n", stick_facing(HALO_VR_SEAT_BARS, 0.0f, 1.0f, lock_left, lock_right, 1.0f),
        ghost_rate(stick_facing(HALO_VR_SEAT_BARS, 0.0f, 1.0f, lock_left, lock_right, 1.0f)));
    printf("barsleft %g rate %g\n", stick_facing(HALO_VR_SEAT_BARS, -0.6f, 0.0f, lock_left, lock_right, 1.0f),
        ghost_rate(stick_facing(HALO_VR_SEAT_BARS, -0.6f, 0.0f, lock_left, lock_right, 1.0f)));
    printf("wheel left %g right %g reversing %g\n",
        stick_facing(HALO_VR_SEAT_WHEEL, -1.0f, 0.0f, lock_left, lock_right, 1.0f),
        stick_facing(HALO_VR_SEAT_WHEEL, 0.0f, 1.0f, lock_left, lock_right, 1.0f),
        stick_facing(HALO_VR_SEAT_WHEEL, -1.0f, 0.0f, lock_left, lock_right, -1.0f));
    printf("stronger %g ride %g\n", stick_facing(HALO_VR_SEAT_WHEEL, -0.5f, 1.0f, lock_left, lock_right, 1.0f),
        stick_facing(HALO_VR_SEAT_RIDE, 1.0f, 1.0f, lock_left, lock_right, 1.0f));
    printf("barsfacing %g %g\n", bars_facing(0.0f), bars_facing(0.3f));
    return 0;
}
"""


def steer_source():
    text = VR.read_text()
    source = STEER_WORLD
    source += defines(text, "HANDLEBAR_DEADZONE", "HANDLEBAR_TURN_RATE", "HANDLEBAR_TURN_FASTEST",
                      "HANDLEBAR_GHOST_TURN_ACCELERATION", "HANDLEBAR_TICKS_PER_SECOND", "STICK_STEER_DEADZONE")
    for name in ("stick_steer_push", "turn_facing", "bars_facing", "stick_facing"):
        source += function(text, name) + "\n"
    return source + STEER_MAIN


def test_sticks_steer_a_seat_without_the_hands_on_its_controls(tmp_path):
    exe = build(tmp_path, steer_source(), "steer")
    lines = {line.split()[0]: line.split() for line in
             subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.splitlines()}
    assert float(lines["dead"][1]) == 0.0 and float(lines["dead"][2]) == 0.0
    # a full push right turns the Ghost right (the facing to its right: negative) at 90 degrees a second
    right = lines["barsright"]
    assert float(right[1]) < 0.0
    assert float(right[3]) == pytest.approx(1.5708, rel=0.05)
    # half a push left (0.6, past a 0.2 deadzone), left at half that
    left = lines["barsleft"]
    assert float(left[1]) > 0.0
    assert float(left[3]) == pytest.approx(0.7854, rel=0.05)
    # the Warthog's wheels to their lock, the other way as it reverses
    wheel = lines["wheel"]
    assert float(wheel[2]) == pytest.approx(0.52)
    assert float(wheel[4]) == pytest.approx(-0.5)
    assert float(wheel[6]) == pytest.approx(-0.52)
    # the stronger push of the two sticks; a passenger's seat is not steered
    assert float(lines["stronger"][1]) == pytest.approx(-0.5)
    assert float(lines["stronger"][3]) == 0.0
    # the handlebars' own steering is as it was: straight bars, straight; turned, a turn
    assert float(lines["barsfacing"][1]) == 0.0
    assert float(lines["barsfacing"][2]) > 0.0


# ---------- the player's body in a seat (first_person_legs.c, tools/vr_cabin_model.py)

LEGS = ROOT / "port/linux/game/first_person_legs.c"
BODY_H = ROOT / "port/linux/game/vr_body.h"
CABINS = ROOT / "tools/vr_cabin_model.py"

BODY_WORLD = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
typedef float real;
typedef struct {real x,y,z;} real_point3d;
typedef struct {real i,j,k;} real_vector3d;
typedef struct {real scale; real_vector3d forward,left,up; real_point3d position;} real_matrix4x3;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define TAG_STRING_LENGTH 31
#define FIRST_PERSON_LEGS_MAXIMUM_NODES 64
#define NUMBEROF(a) ((long)(sizeof(a) / sizeof((a)[0])))
struct model_node {char name[TAG_STRING_LENGTH + 1]; short parent_node_index;};
struct model {struct {long count; struct model_node *address;} nodes;};
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])
static real_vector3d const zero = {0, 0, 0}, upward = {0, 0, 1};
static real_vector3d const *global_zero_vector3d = &zero, *global_up3d = &upward;
static real dot_product3d(real_vector3d const *a, real_vector3d const *b) {return a->i*b->i + a->j*b->j + a->k*b->k;}
static real magnitude3d(real_vector3d const *v) {return sqrtf(dot_product3d(v, v));}
static real normalize3d(real_vector3d *v)
{real m = magnitude3d(v); if (m > 0) {v->i /= m; v->j /= m; v->k /= m;} return m;}
static void cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *c)
{real_vector3d r = {a->j*b->k - a->k*b->j, a->k*b->i - a->i*b->k, a->i*b->j - a->j*b->i}; *c = r;}
static void vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *v)
{v->i = b->x-a->x; v->j = b->y-a->y; v->k = b->z-a->z;}
"""

BODY_STUBS = r"""
static struct vr_body_seat the_seat;
static real_point3d the_anchor;
static int leant;
static boolean vr_render_body_seat(long object_index, struct vr_body_seat *seat) {*seat = the_seat; return TRUE;}
static boolean vr_render_body_anchor(real_point3d *neck, real *yaw) {*neck = the_anchor; *yaw = 0; return TRUE;}
struct render_frustum {int unused;};
static struct render_frustum the_view;
static boolean vr_render_body_eyes(real_point3d eyes[2], struct render_frustum const *views[2], real *near,
    real *seconds)
{eyes[0] = eyes[1] = the_anchor; views[0] = views[1] = &the_view; *near = 0.0625f; *seconds = 0; return TRUE;}
static void first_person_legs_lean(long object_index, struct model const *model, long count,
    real_point3d const *pivot, real_point3d const *neck_top, real_vector3d const *forward, real_vector3d const *left,
    real_point3d const eyes[2], struct render_frustum const *const views[2], real near, real seconds, real most)
{leant++;}
"""

# the Master Chief as the game seats him driving the Scorpion ("scorpion-driver unarmed idle", logged in the
# game: each node's parent and place in the hull's axes, world units)
SCORPION_DRIVER = [
    ("bip01 pelvis", -1, 0.033, 0.189, 0.422), ("bip01 l thigh", 0, 0.033, 0.231, 0.422),
    ("bip01 r thigh", 0, 0.033, 0.147, 0.422), ("bip01 spine", 0, 0.025, 0.189, 0.459),
    ("bip01 l calf", 1, 0.205, 0.234, 0.427), ("bip01 r calf", 2, 0.204, 0.135, 0.434),
    ("bip01 spine1", 3, -0.007, 0.189, 0.525), ("bip01 l clavicle", 6, -0.021, 0.250, 0.587),
    ("bip01 l foot", 4, 0.328, 0.223, 0.332), ("bip01 neck", 6, -0.022, 0.190, 0.610),
    ("bip01 r clavicle", 6, -0.015, 0.130, 0.589), ("bip01 r foot", 5, 0.329, 0.141, 0.341),
    ("bip01 head", 9, -0.024, 0.190, 0.627), ("bip01 l upperarm", 7, -0.007, 0.277, 0.589),
    ("bip01 r upperarm", 10, 0.003, 0.105, 0.592), ("bip01 l forearm", 13, 0.063, 0.292, 0.508),
    ("bip01 r forearm", 14, 0.062, 0.090, 0.503), ("bip01 l hand", 15, 0.176, 0.289, 0.489),
    ("bip01 r hand", 16, 0.172, 0.100, 0.475),
]
# the feed cockpit's eye (vr_cabin_model.h), and the top of the real neck 10 cm behind and below it, the
# body's own neck 5 cm behind that (first_person_legs.c's FIRST_PERSON_LEGS_VR_BACK)
FEED_EYE = (0.465, 0.2, 0.52)
REST_NECK = (FEED_EYE[0] - 0.0328, FEED_EYE[1], FEED_EYE[2] - 0.0328)

BODY_MAIN = r"""
static struct model_node node_list[] = {%(nodes)s};
static real const places[][3] = {%(places)s};
#define COUNT ((long)(sizeof(node_list) / sizeof(node_list[0])))

int main(int argc, char **argv)
{
    struct model model = {{COUNT, node_list}};
    real_matrix4x3 nodes[64];
    real_matrix4x3 const *posed;
    long i;

    for (i = 0; i < COUNT; i++)
    {
        real_matrix4x3 m = {1.0f, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {places[i][0], places[i][1], places[i][2]}};
        nodes[i] = m;
    }
    /* the top of the real neck as recentred; the head moved by argv[1..3] (world units) from there;
    argv[4]: "stand" (turned to face left), "pad" (the gamepad: no arms of the VR mode's) */
    the_seat.rest_neck.x = %(rx)ff; the_seat.rest_neck.y = %(ry)ff; the_seat.rest_neck.z = %(rz)ff;
    the_seat.forward.i = 1; the_seat.left.j = 1; the_seat.pose_forward.i = 1;
    the_seat.arms = strcmp(argv[4], "pad") != 0;
    the_seat.standing = strcmp(argv[4], "stand") == 0;
    if (the_seat.standing)
    {
        the_seat.forward.i = 0; the_seat.forward.j = 1; the_seat.left.i = -1; the_seat.left.j = 0;
    }
    the_anchor.x = the_seat.rest_neck.x + (real)atof(argv[1]);
    the_anchor.y = the_seat.rest_neck.y + (real)atof(argv[2]);
    the_anchor.z = the_seat.rest_neck.z + (real)atof(argv[3]);
    posed = first_person_legs_vr_seated(7, &model, COUNT, nodes);
    if (!posed)
        return 1;
    for (i = 0; i < COUNT; i++)
        printf("%%s|%%f %%f %%f %%f\n", node_list[i].name, posed[i].position.x, posed[i].position.y,
            posed[i].position.z, posed[i].scale);
    printf("leant|%%d\n", leant);
    return 0;
}
"""


def body_source(text=None):
    text = text or LEGS.read_text()
    source = BODY_WORLD
    source += between(BODY_H.read_text(), "struct vr_body_seat", "#endif")
    source += defines(text, "FIRST_PERSON_LEGS_VR_BACK", "FIRST_PERSON_LEGS_SEAT_TURN_MOST", "FIRST_PERSON_LEGS_FOLDED",
                      "FIRST_PERSON_LEGS_LEAN_MAXIMUM")
    source += "static real_matrix4x3 first_person_legs_matrices[FIRST_PERSON_LEGS_MAXIMUM_NODES];\n"
    source += BODY_STUBS
    for name in ("first_person_legs_name_has", "first_person_legs_node", "first_person_legs_turn",
                 "first_person_legs_rotate", "first_person_legs_vr_turn_toward", "first_person_legs_below",
                 "first_person_legs_vr_seated"):
        source += function(text, name) + "\n"
    nodes = ", ".join('{"%s", %d}' % (name, parent) for name, parent, *_ in SCORPION_DRIVER)
    places = ", ".join("{%ff, %ff, %ff}" % (x, y, z) for _, _, x, y, z in SCORPION_DRIVER)
    return source + BODY_MAIN % {"nodes": nodes, "places": places, "rx": REST_NECK[0], "ry": REST_NECK[1],
                                 "rz": REST_NECK[2]}


def pose_body(tmp_path, head=(0.0, 0.0, 0.0), how="sit", text=None, name="body"):
    exe = build(tmp_path, body_source(text), name)
    out = subprocess.run([str(exe), *(str(h) for h in head), how], check=True, capture_output=True,
                         text=True).stdout
    posed = {}
    for line in out.splitlines():
        key, value = line.split("|")
        posed[key] = [float(v) for v in value.split()]
    return posed


def difference(a, b):
    return [a[k] - b[k] for k in range(3)]


NECK_PLACE = [REST_NECK[0] - 0.016, REST_NECK[1], REST_NECK[2]]


def test_the_seated_body_hangs_its_neck_under_the_recentred_head(tmp_path):
    posed = pose_body(tmp_path)
    # the top of its neck 5 cm behind the real one's place (vr.c's: 10 cm behind and below the eye)
    assert posed["bip01 neck"][:3] == pytest.approx(NECK_PLACE, abs=1e-4)
    # its legs as the game seats it: the knees and feet where they are from the hips
    game = {name: (x, y, z) for name, _, x, y, z in SCORPION_DRIVER}
    for leg in ("bip01 l calf", "bip01 r foot"):
        assert difference(posed[leg], posed["bip01 pelvis"]) == pytest.approx(
            difference(game[leg], game["bip01 pelvis"]), abs=1e-4)
    # its head and arms folded away (the VR mode's arms are drawn), and leant clear of the eyes
    for folded in ("bip01 head", "bip01 l hand", "bip01 r forearm", "bip01 l upperarm"):
        assert posed[folded][3] < 0.01
    assert posed["bip01 l thigh"][3] == 1.0 and posed["bip01 spine1"][3] == 1.0
    assert posed["bip01 l hand"][:3] == pytest.approx(posed["bip01 l upperarm"][:3], abs=1e-5)
    assert posed["leant"][0] == 1


def test_the_seated_body_leans_with_the_head_its_hips_staying(tmp_path):
    rest = pose_body(tmp_path)
    # the head 10 cm forward and 5 cm left: the upper body turns toward it about the hips
    posed = pose_body(tmp_path, head=(0.0328, 0.0164, 0.0), name="body_lean")
    assert posed["bip01 pelvis"][:3] == pytest.approx(rest["bip01 pelvis"][:3], abs=1e-5)
    assert posed["bip01 l foot"][:3] == pytest.approx(rest["bip01 l foot"][:3], abs=1e-5)
    moved = difference(posed["bip01 neck"], rest["bip01 neck"])
    assert moved[0] > 0.02 and moved[1] > 0.01

    def spine(p):
        return math.dist(p["bip01 neck"][:3], p["bip01 pelvis"][:3])

    # (the spine's length kept: it turns, it does not stretch)
    assert spine(posed) == pytest.approx(spine(rest), abs=1e-4)
    # and no farther than a person seated turns, however far the head goes
    far = pose_body(tmp_path, head=(0.5, 0.0, -0.3), name="body_far")
    a, b = difference(rest["bip01 neck"], rest["bip01 pelvis"]), difference(far["bip01 neck"], far["bip01 pelvis"])
    angle = math.acos(sum(x * y for x, y in zip(a, b)) / (math.hypot(*a) * math.hypot(*b)))
    assert angle == pytest.approx(0.6, abs=1e-3)


def test_with_the_gamepad_the_body_keeps_its_arms(tmp_path):
    posed = pose_body(tmp_path, how="pad")
    assert posed["bip01 l hand"][3] == 1.0 and posed["bip01 head"][3] < 0.01


def test_standing_in_the_hatch_the_legs_hang_down_turned_to_the_body(tmp_path):
    posed = pose_body(tmp_path, how="stand")
    for side in ("l", "r"):
        thigh, calf, foot = (posed["bip01 %s %s" % (side, part)] for part in ("thigh", "calf", "foot"))
        assert calf[:2] == pytest.approx(thigh[:2], abs=1e-4) and foot[:2] == pytest.approx(thigh[:2], abs=1e-4)
        assert foot[2] < calf[2] < thigh[2]
    # turned to face left (+y): its left shoulder behind it (-x), its right ahead of it
    assert posed["bip01 l clavicle"][0] < posed["bip01 neck"][0] < posed["bip01 r clavicle"][0]
    assert posed["bip01 neck"][:3] == pytest.approx([REST_NECK[0], REST_NECK[1] - 0.016, REST_NECK[2]], abs=1e-4)


def test_negative_control_without_the_move_the_neck_stays_where_the_game_seats_it(tmp_path):
    text = LEGS.read_text()
    mutated = text.replace("vector_from_points3d(&first_person_legs_matrices[neck].position, &target, &move);",
                           "move = *global_zero_vector3d;")
    assert mutated != text
    posed = pose_body(tmp_path, text=mutated, name="body_unmoved")
    assert posed["bip01 neck"][:3] != pytest.approx(NECK_PLACE, abs=1e-3)


def cabin_generator():
    import importlib.util
    spec = importlib.util.spec_from_file_location("vr_cabin_model", CABINS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_cabin_header_is_the_generators(tmp_path):
    out = tmp_path / "vr_cabin_model.h"
    subprocess.run([sys.executable, str(CABINS), "--out", str(out)], check=True, capture_output=True)
    assert out.read_text() == (ROOT / "port/linux/game/vr_cabin_model.h").read_text()


@pytest.mark.parametrize("name", ["scorpion_inside", "scorpion_feed"])
def test_the_seated_body_fits_the_scorpions_cockpit(name):
    """the body as the game seats it, about the cabin's eye as first_person_legs.c draws it, clear of the
    cabin's walls and fittings: none of their faces' corners within its armour's box (from its seat up to its
    knees' top, from the back of its hips to its feet's front, between its sides), its seat's top under it and
    its floor under its feet"""
    module = cabin_generator()
    spec = next(s for s in module.CABINS if s["name"] == name)
    cabin = spec["make"]()
    assert tuple(cabin["eye"]) == FEED_EYE or name != "scorpion_feed"
    body = module.seated_body(cabin["eye"])
    x0, x1 = body["hips_back"] + 0.005, body["front"] - 0.005
    y0, y1 = body["sides"][0] + 0.005, body["sides"][1] - 0.005
    z0, z1 = body["seat"] + 0.005, body["knees"] - 0.005
    inside = [p for mesh in cabin["meshes"] for p, _, _ in mesh.vertices
              if x0 < p[0] < x1 and y0 < p[1] < y1 and z0 < p[2] < z1]
    assert not inside
    # (the faces under it facing up: its seat's top, the floor's under its feet)
    under = [p[2] for mesh in cabin["meshes"] for p, n, _ in mesh.vertices
             if n[2] > 0.99 and x0 < p[0] < x1 and y0 - 0.01 < p[1] < y1 + 0.01 and p[2] <= body["seat"] + 1e-6]
    assert max(under) == pytest.approx(body["seat"], abs=1e-6)
    assert min(under) <= body["feet"]


def test_negative_control_the_old_feed_cockpit_cuts_into_the_body():
    module = cabin_generator()
    text = CABINS.read_text()
    # (its floor, seat and dash as they were, made for no body)
    old = text.replace("z0, z1 = round(body['feet'] - 0.005, 3), 0.730", "z0, z1 = 0.200, 0.730")
    assert old != text
    namespace = {}
    exec(compile(old, str(CABINS), "exec"), namespace)
    cabin = namespace["scorpion_feed"](namespace["FEED_PELICAN"])
    body = module.seated_body(cabin["eye"])
    under = [p[2] for mesh in cabin["meshes"] for p, n, _ in mesh.vertices
             if n[2] > 0.99 and body["hips_back"] < p[0] < body["front"] and p[2] < body["seat"]]
    assert min(under) > body["feet"]
