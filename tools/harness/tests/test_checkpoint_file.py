"""A checkpoint's file (source/saved games/game_state_xbox.c, written by port/linux/src/saved_game_writer.c): the image
taken at the checkpoint and written on the writer's thread while the game goes on, a revert's read and the next
checkpoint waiting for the write, the file always a whole image even if the game stops in the middle of a write, a
failed write leaving the one before it, an older build's file of the whole game state still read (#644)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

CAPACITY = read("port/linux/include/halo_port_capacity.h")

WRITER = "port/linux/src/saved_game_writer.c"
XBOX = "source/saved games/game_state_xbox.c"

CASES = ["background", "revert-waits", "next-waits", "stopped", "failed", "older-file"]
XBOX_NAMES = ["game_state_write_wait", "game_state_file_image_reserve", "game_state_create_or_open_file",
              "game_state_close_file", "game_state_write_to_file", "game_state_read_from_file"]

# a fault in the code (the file it is in, the text and its replacement) and the case that must catch it
NEGATIVE_CONTROLS = {
    "written-on-the-games-thread": (WRITER, ("pthread_create(&thread, NULL, saved_game_writer_thread, NULL) == 0",
                                             "pthread_create(&thread, NULL, saved_game_writer_thread, NULL) == 99"),
                                    "background"),
    "image-not-copied": (XBOX, ("\t\tgame_state_image_take(image);\n", "\t\timage = test_state;\n"), "background"),
    "revert-does-not-wait": (XBOX, ("\t/* port: the last checkpoint written first */\n\tgame_state_write_wait();\n"
                                    "\tmatch_assert(", "\tmatch_assert("), "revert-waits"),
    "wait-does-not-wait": (WRITER, ("\twhile (saved_game_writer.pending)\n\t\tpthread_cond_wait(&saved_game_writer"
                                    ".changed, &saved_game_writer.lock);\n\terror = ",
                                    "\terror = "), "revert-waits"),
    "next-does-not-wait": (XBOX, ("\tgame_state_write_wait();\n\tsize = game_state_image_size();",
                                  "\tsize = game_state_image_size();"), "next-waits"),
    "written-in-place": (WRITER, ('"%s.new", path', '"%s", path'), "stopped"),
    "failure-unreported": (XBOX, ("\treturn !error;\n", "\treturn TRUE;\n"), "failed"),
    "failure-leaves-its-file": (WRITER, ("\tif (error)\n\t\tunlink(temporary);\n", ""), "failed"),
    "older-file-refused": (XBOX, ("\t\telse if (size == GAME_STATE_FILE_SIZE)\n\t\t\timage = malloc(size);\n", ""),
                           "older-file"),
}


def file_size(source):
    """game_state_xbox.c's GAME_STATE_FILE_SIZE, the native builds' game state's (HALO_PORT_GAME_STATE_SIZE)"""
    if not re.search(r"\bGAME_STATE_FILE_SIZE = HALO_PORT_GAME_STATE_SIZE\b", source):
        raise LookupError("GAME_STATE_FILE_SIZE is no longer HALO_PORT_GAME_STATE_SIZE")
    return constant(CAPACITY, "HALO_PORT_GAME_STATE_CPU_SIZE") + constant(CAPACITY, "HALO_PORT_GAME_STATE_GPU_SIZE")


def file_image(source):
    """the checkpoint's image's static structure (game_state_xbox.c's)"""
    match = re.search(r"^static struct\n\{[^}]*\} game_state_file_image = \{ 0 \};", source, re.M)
    if not match:
        raise LookupError("game_state_file_image not found in the sources")
    return match.group(0)


def generated(control=None):
    writer = read(WRITER)
    xbox = read(XBOX)
    if control:
        path, fault, _ = NEGATIVE_CONTROLS[control]
        if path == WRITER:
            writer = mutated(writer, *fault)
        else:
            xbox = mutated(xbox, *fault)
    types = "\n\n".join([
        f"#define GAME_STATE_FILE_SIZE ({file_size(xbox)}L)",
        structure(xbox, "xbox_game_state_globals_prefix"),
        file_image(xbox),
    ]) + "\n"
    # (the writer whole, but for its own headers: the test's are above it)
    text = re.sub(r'^#include ".*"\n', "", writer, flags=re.M) + "\n"
    text += "\n".join(function(xbox, name) for name in XBOX_NAMES) + "\n"
    return (("types.inc", types), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("checkpoint_file", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    case = NEGATIVE_CONTROLS[control][2]
    status, output = run(build("checkpoint_file", generated(control)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
