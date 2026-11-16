# Asset-free regression tests

The engine's real functions, taken from the sources and compiled against a small fake world: no game assets, SDK or
graphics, so a fix can carry a test that CI runs.

    python -m pytest -q tools/harness

They compile with `-m32`, in the game's 32-bit x86 ABI (as `tools/x86_game_abi.py` does; nothing 32-bit ships), so
they need `clang` (or `CC`) and a 32-bit C runtime (`lib32-glibc`, `gcc-multilib`) on 64-bit x86 Linux (on a Mac, see
below). pytest finds every `tests/test_*.py`, so a new test needs no change to CI.

## On a Mac, or 64-bit ARM Linux

macOS has no 32-bit runtime, and 64-bit ARM has no 32-bit x86, so there (`harness.NATIVE_64_BIT`) the tests compile for
the machine's own 64-bit ABI, without fused multiply-add, as the game is built, and need nothing but Xcode's `clang`:

    python3 -m pytest -q -rs tools/harness tools/test_touch_menu.py tools/test_controller_remap.py \
        tools/test_controller_remap_context.py tools/test_host_units.py

Whatever only the game's 32-bit ABI shows is skipped there, each with its reason ("needs the game's 32-bit x86
ABI, ..."): the structures' sizes and offsets the game has with 32-bit pointers and longs, its 32-bit clocks' wraps, its
32-bit sizes' overflows. A test marks these by name in its `ABI_DEPENDENT` (`skip_without_32_bit_abi`), or as a whole
(`needs_32_bit_abi`), and a negative control of such a case is skipped with it. Nothing is skipped on x86-64 Linux,
which CI runs on every push: run those there (with gcc-multilib). An x86-64 Linux container on a Mac is no stand-in: its
emulated 32-bit x86 (Rosetta's, OrbStack's or Docker's) rounds the x87's floating point differently, and is too slow for
some tests' time limits.

A new test whose result depends on the ABI should say so in `ABI_DEPENDENT`, with what it depends on, rather than fail
on a Mac.

## A test

- `tests/<name>.c`: the fake world, only what the code reads, with what it calls stubbed as macros that name only the
  arguments they use; then `#include "under_test.inc"` and a `main` that runs the case named on its command line,
  checked with `CHECK`.
- `tests/test_<name>.py`: takes the code, enums and constants from the sources (`function`, `inline`, `enum_with`,
  `constant`) rather than copying them, so the test checks what the game builds; `build` compiles it (warnings are
  errors) and each case is a pytest test.
- Negative controls: the code with a deliberate fault (`mutated`) must fail a named case's `CHECK`. A test that cannot
  fail proves nothing.

## Limits

Functions are taken by matching braces; one that is not found fails the test. Code with many dependencies is better
compiled as whole files against stub headers. Whatever needs real maps or the whole game is checked in the game
(offline: `debug.null_renderer`).
