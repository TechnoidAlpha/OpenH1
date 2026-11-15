"""Gyro aiming and the flick stick (port/linux/src/gyro_aim.c), fed a controller's samples: the calibration takes a
constant bias off, player space turns a tilted hold's yaw, the smoothing never lags a real turn, the flick stick turns
to where it points and with the rim; and the player's turn (source/game/player_control.c) leaves the gyroscope's and
the flick stick's out of the aim assist's magnetism, which slows and drags the stick's alone."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

AIM_CONTROLS = [
    # no automatic calibration: the bias drifts the view
    ("\t\taim->still_seconds += seconds;", "\t\taim->still_seconds = 0.0f;", "bias"),
    # the calibration asked for turns the view meanwhile, or measures nothing
    ("\t\taim->smoothed[0] = aim->smoothed[1] = 0.0f;\n\t\treturn;\n\t}\n\tcalibration_update",
     "\t\taim->smoothed[0] = aim->smoothed[1] = 0.0f;\n\t}\n\tcalibration_update", "calibrate"),
    # local space's yaw in player space's place: a tilted hold turns less
    ("\tif (settings->player_space && aim->up_known)", "\tif (0)", "player_space"),
    # the up direction from the gyroscope's turn the wrong way
    ("\tturned[2] = aim->up[2] - (rate[0]", "\tturned[2] = aim->up[2] + (rate[0]", "tilt_followed"),
    # a shake's reading taken for gravity's
    ("\tif (fabsf(length - GRAVITY) < ACCELEROMETER_TRUSTED)", "\tif (1)", "tilt_followed"),
    # every motion smoothed, a real turn's too: it lags
    ("#define SMOOTH_FASTEST 0.087f", "#define SMOOTH_FASTEST 100.0f", "smoothing"),
    # the smoothed part lost
    ("\t\trate[index] = rate[index] * direct + smoothed[index];", "\t\trate[index] = rate[index] * direct;",
     "smoothing"),
    # the flick the wrong way, or round the rim the wrong way
    ("\t\t\tflick->flick_size = -angle;", "\t\t\tflick->flick_size = angle;", "flick"),
    ("\t\t\t\tturn -= rate * seconds;", "\t\t\t\tturn += rate * seconds;", "flick"),
    # no way back off the rim but its own threshold: a wavering stick flicks again
    ("#define FLICK_RIM_LEFT 0.7f", "#define FLICK_RIM_LEFT 0.9f", "flick"),
    # thumb on stick: turning while the controller cannot tell, or with the gyro button instead of the touch
    ("\t\treturn touched > 0;", "\t\treturn touched != 0;", "modes"),
    ("\t\treturn touched > 0;", "\t\treturn held != 0;", "modes"),
    # hold to pause turning while held
    ("\t\treturn !held;", "\t\treturn 1;", "modes"),
    # the Steam Controller's touch not known, or Steam Input's virtual controller's taken for one
    ("(product >= 0x1302 && product <= 0x1305)", "(product == 0x1302)", "modes"),
    ("return vendor == VALVE && (product", "return vendor == VALVE && (product == 0x11ff || product", "modes"),
]


def aim_generated(fault=None):
    code = read("port/linux/src/gyro_aim.c")
    code = re.sub(r'^#include "gyro_aim.h"\n', "", code, flags=re.M)
    if fault:
        code = mutated(code, *fault)
    return (("gyro_aim.h", read("port/linux/src/gyro_aim.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", ["bias", "calibrate", "player_space", "tilt_followed", "smoothing", "flick", "modes"])
def test_gyro_aim(case):
    status, output = run(build("gyro_aim", aim_generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", AIM_CONTROLS)
def test_gyro_aim_negative_control(before, after, case):
    status, output = run(build("gyro_aim", aim_generated((before, after))), case)
    assert status == CHECK_FAILED, output


def camera_block(source):
    """get_local_player_input_blob's block of the camera's turn, the stick's look to the devices' direct turns"""
    start = source.index("if (player_control_camera_control_is_active())",
                         source.index("static void get_local_player_input_blob("))
    start = source.index("{", start)
    depth = 0
    for index in range(start, len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise LookupError("the camera's block has no end")


ASSIST_CONTROLS = [
    # the gyroscope slowed by the magnetism's friction, as the stick is
    ("\t\t\t\t\t\t\tinput->facing_delta.yaw += gyro_yaw;",
     "\t\t\t\t\t\t\tinput->facing_delta.yaw += gyro_yaw * (1.f - control->magnetism_level * "
     "PIN(constants->magnetism_friction, 0.f, 1.f));", "magnetism"),
    # the flick stick's turn without the flick stick
    ("if (halo_linux_flick_look(gamepad_index, &flick_yaw) &&\n\t\t\t\t\t\t\tinput_abstraction_port_flick_stick("
     "gamepad_index))", "if (halo_linux_flick_look(gamepad_index, &flick_yaw))", "zoom_and_flick"),
    # the gyroscope not zoomed as the mouse is
    ("\t\t\t\t\t\t\t\tgyro_yaw *= zoom_scale;", "", "zoom_and_flick"),
]


def assist_generated(fault=None):
    source = read("source/game/player_control.c")
    block = camera_block(source)
    if fault:
        block = mutated(block, *fault)
    text = (function(source, "player_control_look_acceleration") + "\n" + "static void camera_turn(struct game_globals_player_control *constants, struct game_input_state "
            "*input_state, struct player_control *control, struct player_datum *player, struct input_blob *input, "
            "real clamped_yaw, real clamped_pitch, real look_yaw_rate, real look_pitch_rate, real time_delta_sec, "
            "short gamepad_index, short local_player_index)\n" + block + "\n")
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", ["magnetism", "zoom_and_flick"])
def test_gyro_aim_assist(case):
    status, output = run(build("gyro_aim_assist", assist_generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", ASSIST_CONTROLS)
def test_gyro_aim_assist_negative_control(before, after, case):
    status, output = run(build("gyro_aim_assist", assist_generated((before, after))), case)
    assert status == CHECK_FAILED, output
