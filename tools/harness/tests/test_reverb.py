"""The reverb of dsound_sdl.c against the I3DL2 listener properties of each sound environment of the maps (reverb.c):
its decay time at low frequencies and at the HF reference, the levels of its early reflections and late
reverberation, when each comes, and how it follows the game from one environment to another.

    python tools/harness/tests/test_reverb.py metrics   the figures of each environment
(MIXER_SOURCE=path: of another dsound_sdl.c, to compare)
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, run  # noqa: E402
from harness.tests.test_mixer import generated  # noqa: E402

CASES = ["decay", "levels", "timing", "transition"]

# a fault in the reverb, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the decay HF ratio held to 1 (a low pass alone), as it was
    "hf-ratio-held": (("clamp_real(properties->flDecayHFRatio, 0.1f, 2.0f)",
                       "clamp_real(properties->flDecayHFRatio, 0.1f, 1.0f)"), "decay"),
    # the late reverberation at the energy of each output channel, 3 dB too wet, as it was
    "late-per-channel": (("sqrtf((1.0f - mean_feedback * mean_feedback) / OUTPUT_CHANNELS)",
                          "sqrtf(1.0f - mean_feedback * mean_feedback)"), "levels"),
    # the reflections at a fixed 15 ms after the first, whatever the reverb delay, as they were
    "reflections-fixed": (("reverb_tap_times[index] * reflections_span",
                           "reverb_tap_times[index] * 0.0149f"), "timing"),
    # the late reverberation heard only out of the delay lines, a line's length late, as it was
    "late-from-line-ends": (("heard = reverb.lines[index][(reverb.line_position - reverb.line_output_delays[index]) & "
                             "(REVERB_LINE_SIZE - 1)];", "heard = reverb.lowpass[index];"), "timing"),
    # the diffusers' delay added to the reverb delay (30 ms late at no diffusion), as it was
    "diffusers-delay-uncounted": (("diffuser_delay * (1.0f - parameters->diffusion / 0.7f)", "diffuser_delay * 0.0f"),
                                  "timing"),
    # a step of the game's mid-fade restarting the fade from the environment before
    "fade-restarted": (("if (reverb.serial != environment_serial && !reverb.fade)",
                        "if (reverb.serial != environment_serial)"), "transition"),
}


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("reverb", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("reverb", generated(fault=fault)), case)
    assert status == CHECK_FAILED, f"the reverb with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    import os
    import subprocess

    other = os.environ.get("MIXER_SOURCE")
    executable = build("reverb", generated(Path(other).read_text(encoding="latin-1") if other else None))
    sys.exit(subprocess.run([str(executable), *(sys.argv[1:] or ["metrics"])]).returncode)
