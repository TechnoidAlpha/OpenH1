"""The game state's image (source/saved games/game_state.c): a checkpoint's holds the part of the game state in use, the
CPU's allocations then the GPU's; taken back, from it or from an image of the whole game state (an older build's file,
a core, Save and Quit's), the parts in use are what they were and the rest is left; what is checked is found at the
same place in both; an image of another size is refused (#644)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402
from harness import skip_without_32_bit_abi  # noqa: E402

SOURCE = "source/saved games/game_state.c"
CAPACITY = read("port/linux/include/halo_port_capacity.h")

CASES = ["size", "taken", "taken-back", "whole", "checked", "other-size"]
NAMES = ["game_state_image_pointer", "game_state_image_refuse", "game_state_image_valid", "game_state_image_size",
         "game_state_image_take", "game_state_image_accept"]

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "whole-reservation": (("return game_state_globals.cpu_allocation_size + game_state_globals.gpu_allocation_size;",
                           "return GAME_STATE_SIZE;"), "game_state_image_size", "size"),
    "gpu-taken-from-elsewhere": (("base + GAME_STATE_SIZE - game_state_globals.gpu_allocation_size,",
                                  "base + game_state_globals.cpu_allocation_size,"), "game_state_image_take", "taken"),
    "gpu-read-from-elsewhere": (("\t\tgpu_image = (byte *)image + game_state_globals.cpu_allocation_size;",
                                 "\t\tgpu_image = (byte *)image + GAME_STATE_SIZE - game_state_globals.gpu_allocation_size;"),
                                "game_state_image_accept", "taken-back"),
    "whole-read-as-compact": (("\t\tgpu_image = (byte *)image + GAME_STATE_SIZE - game_state_globals.gpu_allocation_size;",
                               "\t\tgpu_image = (byte *)image + game_state_globals.cpu_allocation_size;"),
                              "game_state_image_accept", "whole"),
    "unused-part-overwritten": (("\tcsmemcpy(game_state_globals.base_address, image, game_state_globals.cpu_allocation_size);",
                                 "\tcsmemcpy(game_state_globals.base_address, image, size);"),
                                "game_state_image_accept", "taken-back"),
    "unchecked": (("\t\tif (!valid)\n", "\t\tif (!valid && 0)\n"), "game_state_image_valid", "checked"),
    "any-size": (("\telse if (size == game_state_image_size())", "\telse if (1)"), "game_state_image_accept",
                 "other-size"),
}


def generated(fault=None, faulty_function=None):
    source = read(SOURCE)
    size = constant(CAPACITY, "HALO_PORT_GAME_STATE_CPU_SIZE") + constant(CAPACITY, "HALO_PORT_GAME_STATE_GPU_SIZE")
    types = "\n\n".join([
        f"#define GAME_STATE_SIZE ({size}L)",
        f"#define GAME_STATE_CPU_SIZE ({constant(CAPACITY, 'HALO_PORT_GAME_STATE_CPU_SIZE')}L)",
        enum_with(source, "_game_state_allocation_data,"),
    ]) + "\n"
    text = ""
    for name in NAMES:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("types.inc", types), ("under_test.inc", text))


# what only the game's 32-bit ABI shows (harness.NATIVE_64_BIT)
ABI_DEPENDENT = {
    "whole": "the game state and its image are laid out with the game's 32-bit pointers and longs",
    "checked": "the game state and its image are laid out with the game's 32-bit pointers and longs",
    "other-size": "the game state and its image are laid out with the game's 32-bit pointers and longs",
    "taken-back": "the game state and its image are laid out with the game's 32-bit pointers and longs",
}


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    skip_without_32_bit_abi(ABI_DEPENDENT, case)
    status, output = run(build("game_state_image", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    skip_without_32_bit_abi(ABI_DEPENDENT, control, case)
    status, output = run(build("game_state_image", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
