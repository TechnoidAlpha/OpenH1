"""Crouch in source/input/input_abstraction.c, compiled alone: a crouched
player's move stays below the 0.98 that stands a grounded biped up
(source/game/player_control.c), a full diagonal of a stick too
(crouch_walk), and input.crouch "toggle" keeps a tapped crouch on
(crouch_controls_update)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

PRELUDE = r"""
#include <assert.h>
#include <math.h>
#include <string.h>
typedef float real;
typedef unsigned char byte;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define MAXIMUM_GAMEPADS 4
enum { _game_control_jump, _game_control_crouch, NUMBER_OF_GAME_CONTROLS };
struct game_input_state
{
    byte buttons[NUMBER_OF_GAME_CONTROLS];
    real forward_movement;
    real strafe;
};
static real square_root(real x)
{
    return sqrtf(x);
}
static long now_ms;
static unsigned long system_milliseconds(void)
{
    return (unsigned long)now_ms;
}
static const char *crouch_setting = "toggle";
static unsigned long setting_changes = 1;
const char *config_string(const char *name)
{
    assert(!strcmp(name, "input.crouch"));
    return crouch_setting;
}
unsigned long config_changes(void)
{
    return setting_changes;
}
"""


def compile_and_run(tmp_path, name, code):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    text = (ROOT / "source/input/input_abstraction.c").read_text()
    start = text.index("static void crouch_walk(")
    end = text.index("\n}\n", text.index("static void crouch_controls_update(")) + 3
    source = tmp_path / f"{name}.c"
    source.write_text(PRELUDE + text[start:end] + code)
    binary = tmp_path / name
    build = subprocess.run([cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-O1",
                            str(source), "-lm", "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_crouch_walk(tmp_path):
    compile_and_run(tmp_path, "crouch_walk", r"""
static real walked(real forward, real strafe)
{
    crouch_walk(&forward, &strafe);
    return square_root(forward * forward + strafe * strafe);
}

int main(void)
{
    real forward = 0.5f, strafe = -0.25f;

    /* a full push in any direction: crouched (below 0.98), at 0.97 */
    assert(fabsf(walked(1.f, 0.f) - 0.97f) < 1e-5f);
    assert(fabsf(walked(0.f, -1.f) - 0.97f) < 1e-5f);
    /* a stick's full diagonal, (1, 1) and 1.41 long, stood the player up
    when each axis alone was brought to 0.97 */
    assert(walked(1.f, 1.f) < 0.98f && fabsf(walked(1.f, 1.f) - 0.97f) < 1e-5f);
    assert(walked(-1.f, 1.f) < 0.98f);
    /* its direction kept */
    {
        real f = 1.f, s = 1.f;

        crouch_walk(&f, &s);
        assert(fabsf(f - s) < 1e-6f && f > 0.f);
    }
    /* a smaller push unchanged */
    crouch_walk(&forward, &strafe);
    assert(forward == 0.5f && strafe == -0.25f);
    return 0;
}
""")


def test_crouch_toggle(tmp_path):
    compile_and_run(tmp_path, "crouch_toggle", r"""
/* one frame of controller 0 with crouch and jump held or not; whether
the player crouches, and the move as it leaves */
static int frame(int crouch, int jump, long ms, real *forward)
{
    struct game_input_state state;

    memset(&state, 0, sizeof(state));
    state.buttons[_game_control_crouch] = (byte)crouch;
    state.buttons[_game_control_jump] = (byte)jump;
    state.forward_movement = 1.f;
    state.strafe = 1.f;
    now_ms = ms;
    crouch_controls_update(0, &state);
    if (forward)
        *forward = state.forward_movement;
    return state.buttons[_game_control_crouch] != 0;
}

int main(void)
{
    real forward;

    /* a tap: crouched while down and after, at a full diagonal kept
    below the stand-up push */
    assert(frame(1, 0, 0, NULL) && frame(1, 0, 100, NULL));
    assert(frame(0, 0, 150, &forward) && forward < 0.98f * 0.7072f);
    assert(frame(0, 0, 5000, NULL));
    /* the next press stands up at once, and its let-go keeps standing */
    assert(!frame(1, 0, 5100, NULL) && !frame(0, 0, 5150, NULL) && !frame(0, 0, 6000, NULL));
    /* a hold: crouched while held, standing when let go */
    assert(frame(1, 0, 7000, NULL) && frame(1, 0, 8000, NULL) && !frame(0, 0, 8050, NULL));
    /* Jump stands a tapped crouch up */
    assert(frame(1, 0, 9000, NULL) && frame(0, 0, 9100, NULL));
    assert(!frame(0, 1, 9200, NULL) && !frame(0, 0, 9300, NULL));
    /* death or a seat (player_control.c) stands it up */
    assert(frame(1, 0, 10000, NULL) && frame(0, 0, 10100, NULL));
    input_abstraction_crouch_release(0);
    assert(!frame(0, 0, 10200, NULL));
    /* the touch controls' tapped crouch holds the button while it is on:
    a long hold, so standing when it lets go */
    assert(frame(1, 0, 11000, NULL) && frame(1, 0, 15000, NULL) && !frame(0, 0, 15100, NULL));
    /* "hold": crouched only while held, and still kept below the push */
    crouch_setting = "hold";
    setting_changes++;
    assert(frame(1, 0, 16000, &forward) && forward < 0.98f * 0.7072f);
    assert(!frame(0, 0, 16100, NULL));
    return 0;
}
""")
