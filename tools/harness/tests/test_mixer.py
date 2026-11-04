"""The real mixer of dsound_sdl.c on known signals (mixer.c): its resampler against the same low pass worked out in
double precision, sines through it as the ear hears them, voices out of earshot, mono voices, the linear
resampler, the Xbox ADPCM decoder against reference vectors, the Doppler shift, the limiter, audio.channels' choice
and the 5.1 pan, and 3D audio: audio.spatial's choice, the HRTF's cues against the pan's, its distance,
occlusion, reverb and Doppler, the crossfade when the choice changes, and 2D voices left as they are; the master
volume on every kind of voice; and the
hand-off of the game's side of the voices to the mixer, which never waits for a game thread.

    python tools/harness/tests/test_mixer.py metrics   the figures
    python tools/harness/tests/test_mixer.py bench [voices] [out of earshot] [indoor|outdoor] [hrtf]   a game's worth of
        voices, timed (through the HRTF: hrtf)
    python tools/harness/tests/test_mixer.py circle out.wav hrtf|pan   a sound going round the listener, to listen to
    python tools/harness/tests/test_mixer.py render > out.raw   noise through the mixer, to compare with another's
(MIXER_SOURCE=path: of another dsound_sdl.c; MIXER_SCALAR=1: its resampler adding tap by tap)
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

SOURCE = "port/linux/src/dsound_sdl.c"
CASES = ["reference", "impulse", "sines", "aliases", "silent-voice", "mono", "linear", "adpcm", "doppler", "doppler-steady",
         "limiter", "channels", "surround", "spatial-choice", "hrtf-cues", "hrtf-paths", "hrtf-switch", "dry-2d",
         "master-volume", "buffers",
         "hand-off-never-waits", "hand-off-settings"]

# a fault in the mixer, and the case that must catch it
NEGATIVE_CONTROLS = {
    "taps-off-by-one": (("(stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY",
                         "(stream->center + 2 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY"), "reference"),
    "phase-not-blended": (("for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)",
                           "for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase += 2)"), "impulse"),
    "no-stretch": (("#define RESAMPLER_MAXIMUM_STRETCH 2", "#define RESAMPLER_MAXIMUM_STRETCH 1"), "aliases"),
    "linear-nearest": (("left_frames[0] + (left_frames[1] - left_frames[0]) * fraction", "left_frames[0]"), "linear"),
    # Xbox ADPCM: the high nibble first, the header's sample left out (all 64 nibbles decoded), the step index held a
    # step short, the channels' words not interleaved
    "adpcm-high-nibble-first": (("ima_expand(nibbles[byte] & 0xf, &predictor, &index)",
                                 "ima_expand(nibbles[byte] >> 4, &predictor, &index)"), "adpcm"),
    "adpcm-no-header-sample": (("unsigned long sample = group * 8 + byte * 2 + 1;",
                                "unsigned long sample = group * 8 + byte * 2;"), "adpcm"),
    "adpcm-index-87": (("if (*index > 88) *index = 88;", "if (*index > 87) *index = 87;"), "adpcm"),
    "adpcm-not-interleaved": (("(group * channels + channel) * 4", "(channel * 8 + group) * 4"), "adpcm"),
    # the Doppler shift: left out, or the source's part with the wrong sign
    "doppler-off": (("doppler_smoothed(stream, frames);\n\t/* (the step", "1.0;\n\t/* (the step"), "doppler"),
    # the Doppler shift a step at each mix, as the velocities give it
    "doppler-unsmoothed": (("#define DOPPLER_SMOOTHING_SECONDS 0.08f", "#define DOPPLER_SMOOTHING_SECONDS 0.0001f"),
                           "doppler-steady"),
    "doppler-source-sign": (("(DOPPLER_SPEED_OF_SOUND + listener_speed) / (DOPPLER_SPEED_OF_SOUND + source_speed)",
                             "(DOPPLER_SPEED_OF_SOUND + listener_speed) / (DOPPLER_SPEED_OF_SOUND - source_speed)"),
                            "doppler"),
    # the limiter's ceiling under full scale, as it was
    "limiter-ceiling": (("#define LIMITER_CEILING 1.0f", "#define LIMITER_CEILING 0.891f"), "limiter"),
    # audio.channels "auto" stereo whatever the device (as every build was before the host said its channels)
    "auto-always-stereo": (('!strcmp(setting, "auto") && device_channels >= 6', '!strcmp(setting, "auto") && 0'),
                           "channels"),
    "auto-on-four": (("device_channels >= 6", "device_channels >= 4"), "channels"),
    # the 5.1 pan: the backs swapped, a back on the LFE, or the pan linear in the angle (the pairwise pan it was)
    "surround-backs-swapped": (("surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 }",
                                "surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 4, 5, 0 }"), "surround"),
    "surround-back-on-lfe": (("surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 }",
                              "surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 3, 0 }"), "surround"),
    "surround-linear-in-angle": (("float first = sinf(surround_ring_angle[segment + 1] - angle);\n"
                                  "\t\tfloat second = sinf(angle - surround_ring_angle[segment]);",
                                  "float first = cosf(1.57079633f * (angle - surround_ring_angle[segment]) /\n"
                                  "\t\t\t(surround_ring_angle[segment + 1] - surround_ring_angle[segment]));\n"
                                  "\t\tfloat second = sinf(1.57079633f * (angle - surround_ring_angle[segment]) /\n"
                                  "\t\t\t(surround_ring_angle[segment + 1] - surround_ring_angle[segment]));"),
                                 "surround"),
    # audio.spatial: headphones forced panned; a device not known taken as headphones; the VR mode by audio.spatial
    "spatial-headphones-panned": (('if (!strcmp(setting, "headphones"))\n\t\treturn TRUE;',
                                   'if (!strcmp(setting, "headphones"))\n\t\treturn FALSE;'), "spatial-choice"),
    "spatial-unknown-headphones": (("return headphones > 0;", "return headphones != 0;"), "spatial-choice"),
    "spatial-vr-ignored": (("if (vr_running)\n\t\treturn vr_hrtf;", "if (vr_running && 0)\n\t\treturn vr_hrtf;"),
                           "spatial-choice"),
    # the HRTF: no time between the ears, no head shadow, the notch fixed (no elevation), nothing behind
    "hrtf-no-delay": (("parameters->delay[ear] = strength *", "parameters->delay[ear] = 0.0f * strength *"), "hrtf-cues"),
    "hrtf-no-shadow": (("float shadow = 1.0f + strength *", "float shadow = 1.0f + 0.0f * strength *"), "hrtf-cues"),
    "hrtf-no-elevation": (("elevation = (elevation + 30.0f) / 90.0f;", "elevation = 30.0f / 90.0f;"), "hrtf-cues"),
    "hrtf-no-rear": (("#define HRTF_REAR_DEPTH 0.55f", "#define HRTF_REAR_DEPTH 0.0f"), "hrtf-cues"),
    # through the HRTF: the distance left out
    "hrtf-no-distance": (("hrtf_out->gain = 0.70710678f * attenuation *", "hrtf_out->gain = 0.70710678f *"),
                         "hrtf-paths"),
    # the choice switched at once, without the crossfade
    "hrtf-switch-at-once": (("hrtf_mix = hrtf_on ? stream->hrtf_mix : 0.0f;",
                             "hrtf_mix = hrtf_on && hrtf_enabled ? 1.0f : 0.0f;"), "hrtf-switch"),
    # a 2D mono voice through the HRTF
    "hrtf-2d": (("\t*hrtf_on = FALSE;\n\tif (stream->has_3d && stream->mode != DS3DMODE_DISABLE)",
                 "\t*hrtf_on = FALSE;\n\tif (stream->channels == 1 || (stream->has_3d && stream->mode != DS3DMODE_DISABLE))"),
                "dry-2d"),
    # the HRTF's gain without the master volume and the voice's own (every 3D sound at full volume on headphones)
    "hrtf-no-volume": (("\tif (*hrtf_on)\n\t\thrtf_out->gain *= stream->volume * master_volume;\n", ""), "master-volume"),
    # the hand-off: the device's mix waiting for the game's thread to take its side over, or to say what it
    # finished; a setting left on the game's side
    "hand-off-waits": (("else if (pthread_mutex_trylock(&voices_lock))\n\t{\n\t\thand_offs_skipped++;",
                        "else if (pthread_mutex_lock(&voices_lock))\n\t{\n\t\thand_offs_skipped++;"),
                       "hand-off-never-waits"),
    "publish-waits": (("else if (pthread_mutex_trylock(&voices_lock))\n\t\treturn;",
                       "else if (pthread_mutex_lock(&voices_lock))\n\t\treturn;"), "hand-off-never-waits"),
    "setting-not-taken": (("\tstream->room_hf = settings->room_hf;\n", ""), "hand-off-settings"),
}


def generated(source=None, fault=None, scalar=False):
    """the mixer: dsound_sdl.c from its constants to the end of the mixing (before the output), its resampler adding
    tap by tap if scalar"""
    text = source if source is not None else read(SOURCE)
    code = text[text.index("#define OUTPUT_RATE"):text.index("/* ---------- output")]
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", ("#define RESAMPLER_SCALAR\n" if scalar else "") + code),)


def rendered(scalar):
    """mixer.c's render: voices at each rate, then a scene's whole mix, as floats"""
    import array
    import subprocess

    result = subprocess.run([str(build("mixer", generated(scalar=scalar))), "render"], capture_output=True,
                            timeout=120, check=True)
    return array.array("f", result.stdout)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mixer", generated()), case)
    assert status == 0, output


def test_vectors_add_as_taps_do():
    """the resampler four taps at a time is the resampler tap by tap but for the order of its additions: the same
    sound to float's rounding, through the whole mix (reverb and limiter)"""
    vector, scalar = rendered(False), rendered(True)
    assert len(vector) == len(scalar) > 0
    worst = max(abs(a - b) for a, b in zip(vector, scalar))
    assert 0 < worst < 1e-5, f"the vector resampler differs by {worst} (none: no vectors)"


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("mixer", generated(fault=fault)), case)
    assert status == CHECK_FAILED, f"the mixer with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    import os
    import subprocess

    # (MIXER_SOURCE: another dsound_sdl.c, to compare; MIXER_SCALAR=1: its resampler tap by tap)
    other = os.environ.get("MIXER_SOURCE")
    executable = build("mixer", generated(Path(other).read_text(encoding="latin-1") if other else None,
                                          scalar=bool(os.environ.get("MIXER_SCALAR"))))
    sys.exit(subprocess.run([str(executable), *(sys.argv[1:] or ["metrics"])]).returncode)
