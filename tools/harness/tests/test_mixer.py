"""The real mixer of dsound_sdl.c on known signals (mixer.c): its resampler against the same low pass worked out in
double precision, sines through it as the ear hears them, voices out of earshot, mono voices, the linear
resampler, the Xbox ADPCM decoder against reference vectors, the Doppler shift, the limiter, audio.channels' choice
and the 5.1 pan, the LFE's low pass, and 3D audio: audio.spatial's choice, the HRTF's cues against the pan's, its
distance, occlusion, reverb and Doppler, the crossfade when the choice changes, and 2D voices left as they are; the master
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
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402
from harness import skip_without_32_bit_abi  # noqa: E402

SOURCE = "port/linux/src/dsound_sdl.c"
CASES = ["reference", "impulse", "sines", "aliases", "silent-voice", "mono", "linear", "cut", "cuts", "adpcm", "doppler", "doppler-step",
         "parameter-ramp", "block-size",
         "limiter", "obstruction", "room-send", "room-send-images", "quiet-levels", "quiet-levels-3d", "resampler-xbox", "resampler-xbox-images", "channels", "surround", "lfe", "spatial-choice", "hrtf-cues", "hrtf-paths", "hrtf-switch",
         "dry-2d",
         "master-volume", "buffers", "sound-off",
         "hand-off-never-waits", "hand-off-settings"]

# a fault in the mixer, and the case that must catch it
NEGATIVE_CONTROLS = {
    "taps-off-by-one": (("(stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY",
                         "(stream->center + 2 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY"), "reference"),
    "phase-not-blended": (("for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)",
                           "for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase += 2)"), "impulse"),
    "no-stretch": (("#define RESAMPLER_MAXIMUM_STRETCH 2", "#define RESAMPLER_MAXIMUM_STRETCH 1"), "aliases"),
    "linear-nearest": (("left_frames[0] + (left_frames[1] - left_frames[0]) * fraction", "left_frames[0]"), "linear"),
    # a voice cut off: stopped dead (as it was), not faded, not cut at the hand-off, or a cut playing over the
    # earlier one read past what is left of it
    "cut-dead": (("mix_voice(stream, output, send, CUT_FADE_FRAMES);", "return;"), "cut"),
    "cut-not-faded": (("float fade = 0.5f + 0.5f * (float)cos(3.14159265358979 * (double)(frame + 1) / CUT_FADE_FRAMES);",
                       "float fade = 1.0f;"), "cut"),
    "cut-not-at-hand-off": (("\t\tvoice_cut(stream);\n\t\tstream->mixing_count = 0;",
                             "\t\tstream->mixing_count = 0;"), "cut"),
    "cut-past-the-earlier": (("(frame < left ? stream->cut_send[stream->cut_played + frame] : 0.0f)",
                              "(frame < left ? 1.0f : 0.0f) * stream->cut_send[(stream->cut_played + frame) % CUT_FADE_FRAMES]"),
                             "cuts"),
    # Xbox ADPCM: the high nibble first, the header's sample left out (all 64 nibbles decoded), the step index held a
    # step short, the channels' words not interleaved
    "adpcm-high-nibble-first": (("ima_expand(nibbles[byte] & 0xf, &predictor, &index)",
                                 "ima_expand(nibbles[byte] >> 4, &predictor, &index)"), "adpcm"),
    "adpcm-no-header-sample": (("unsigned long sample = group * 8 + byte * 2 + 1;",
                                "unsigned long sample = group * 8 + byte * 2;"), "adpcm"),
    "adpcm-index-87": (("if (*index > 88) *index = 88;", "if (*index > 87) *index = 87;"), "adpcm"),
    "adpcm-not-interleaved": (("(group * channels + channel) * 4", "(channel * 8 + group) * 4"), "adpcm"),
    # the Doppler shift: left out, or the source's part with the wrong sign
    "doppler-off": (("doppler_heard(stream);\n\t/* (the step", "1.0;\n\t/* (the step"), "doppler"),
    # the Doppler shift glided towards the velocities' over 80 ms (a 512-frame mix's share), as it was
    "doppler-glide": (("stream->current_doppler = doppler_ratio(stream);",
                       "stream->current_doppler += (doppler_ratio(stream) - stream->current_doppler) *\n"
                       "\t\t(stream->gains_valid ? 0.125f : 1.0f);"), "doppler-step"),
    "doppler-source-sign": (("(listener_speed - source_speed) / DOPPLER_SPEED_OF_SOUND",
                             "(listener_speed + source_speed) / DOPPLER_SPEED_OF_SOUND"), "doppler"),
    # the Doppler shift as (c + v_listener) / (c + v_source), as it was: 1.216 and 0.849 where an Xbox gave 1.178
    # and 0.822
    "doppler-physical": (("ratio = 1.0f + (listener_speed - source_speed) / DOPPLER_SPEED_OF_SOUND;",
                          "ratio = (DOPPLER_SPEED_OF_SOUND + listener_speed) / (DOPPLER_SPEED_OF_SOUND + "
                          "source_speed);"), "doppler"),
    # the limiter's ceiling under full scale, as it was
    "limiter-ceiling": (("#define LIMITER_CEILING 1.0f", "#define LIMITER_CEILING 0.891f"), "limiter"),
    # the mix at full level (twice the Xbox's), as it was; or held to full scale before it is halved
    "output-not-halved": (("#define OUTPUT_GAIN 0.5f", "#define OUTPUT_GAIN 1.0f"), "limiter"),
    "limited-before-halved": (("#define LIMITER_CEILING 1.0f", "#define LIMITER_CEILING 0.5f"), "limiter"),
    # obstruction: the reverb's send left bright (as it was), lowered by its LF ratio, or no muffling at all
    "obstruction-send-bright": (("source->Occlusion.lHFLevel +\n\t\t(source->Obstruction.lHFLevel -", "source->Occlusion.lHFLevel +\n\t\t0 * (source->Obstruction.lHFLevel -"),
                                "obstruction"),
    "obstruction-lowers-send": (("room = source->lRoom + (LONG)", "room = source->lRoom + (LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) + (LONG)"),
                                "obstruction"),
    "obstruction-not-muffling": (("direct_hf = source->lDirect + source->lDirectHF + source->Obstruction.lHFLevel",
                                  "direct_hf = source->lDirect + source->lDirectHF + 0 * source->Obstruction.lHFLevel"),
                                 "obstruction"),
    # the source's Room HF added to its Room, as I3DL2 reads it and as it was (the reverb of most 3D sounds 12 dB
    # darker at 5 kHz than the Xbox's)
    "room-hf-added": (("room_hf = source->lRoom + source->Occlusion.lHFLevel +",
                       "room_hf = source->lRoom + source->lRoomHF + source->Occlusion.lHFLevel +"), "room-send"),
    # the room send of a voice at the output's rate through the resampler's low pass, as it was: the reverb's
    # frames fold its missing 23 to 24 kHz onto the reverb's low end (the cave's 4 dB under the Xbox's below 500 Hz)
    "room-send-low-passed": (("float mono = send_frame(stream);",
                              "float mono = stream->channels == 1 ? sample_left : 0.5f * (sample_left + sample_right);"),
                             "room-send"),
    # the room send of a voice at another rate through the mixer's resampler, as it was (a 44.1 kHz click's reverb
    # 3.6 dB under the Xbox's below 1 kHz, 6 dB pitched up 1.5 times); through the Xbox's four frames each side, not
    # three; its phases not blended
    "room-send-images-mixer": (("float mono = send_frame(stream);",
                                "float mono = stream->channels == 1 ? sample_left : 0.5f * (sample_left + sample_right);"),
                               "room-send-images"),
    "room-send-images-wider": (("#define SEND_ZERO_CROSSINGS 3", "#define SEND_ZERO_CROSSINGS 4"), "room-send-images"),
    "room-send-images-not-blended": (("send_deltas[phase][tap] = send_phases[phase + 1][tap] - send_phases[phase][tap];",
                                      "send_deltas[phase][tap] = 0.0f;"), "room-send-images"),
    # the Xbox's quietest levels: none (every level as it is, as it was); audio.volume in them; the HRTF's or the
    # room send's left out
    "quiet-levels-exact": (("if (size >= VOICE_LEVEL_KNEE)", "if (size >= 0.0f)"), "quiet-levels"),
    "quiet-levels-volume-in": (("*left = voice_level(*left * stream->volume) * master_volume;",
                                "*left = voice_level(*left * stream->volume * master_volume);"), "quiet-levels"),
    "quiet-levels-hrtf": (("hrtf_out->gain *= scale;", "hrtf_out->gain *= stream->volume * master_volume;"),
                          "quiet-levels"),
    "quiet-levels-room": (("*room *= room_scale;", "*room *= stream->volume * master_volume;"), "quiet-levels"),
    # a 3D voice's: each gain's whole level's (as #785 had it: 10 m away silent from -34 dB, not -44), its volume and
    # rolloff's once over, not twice; its direct path's without its direct level, its room send's with it; its room
    # send capped at its rolloff (#790's), or its room level and rolloff once over
    # audio.resampler "xbox": the direct path through the send's six taps (#785's model), through three frames each
    # side, not four; the mixer's low pass by default, as it was
    "direct-six-taps": (("direct_phases[row], direct_deltas[row], blend);\n\t\t\tsample_right",
                         "send_phases[row], send_deltas[row], blend);\n\t\t\tsample_right"), "resampler-xbox"),
    "direct-three-each-side": (("else if (frame >= DIRECT_ZERO_CROSSINGS)", "else if (frame >= 3)"),
                               "resampler-xbox"),
    # the direct path's low pass smooth (a Hann windowed sinc over its four frames each side: none of the Xbox's
    # corners at the frames, its stop band's images and lobe), or its polynomials cut to cubics
    "direct-smooth": (("\t\t\t\tdirect_phases[phase][tap] = (float)weight;",
                       "\t\t\t\tdirect_phases[phase][tap] = (float)(sin(3.14159265358979 * distance) / "
                       "(3.14159265358979 * distance) * (0.5 + 0.5 * cos(3.14159265358979 * distance / 4.0)));"),
                      "resampler-xbox-images"),
    "direct-cubic": (("for (power = DIRECT_KERNEL_ORDER; power >= 0; power--)", "for (power = 3; power >= 0; power--)"),
                     "resampler-xbox-images"),
    "default-sinc": (("return setting && !strcmp(setting, \"linear\") ? RESAMPLER_LINEAR : RESAMPLER_XBOX;",
                      "return setting && !strcmp(setting, \"linear\") ? RESAMPLER_LINEAR : RESAMPLER_SINC;"),
                     "resampler-xbox"),
    "quiet-levels-3d-whole": (("\tif (level_3d < 0.0f)\n", "\tif (1)\n"), "quiet-levels-3d"),
    "quiet-levels-3d-once": (("#define VOICE_LEVEL_3D 2.0f", "#define VOICE_LEVEL_3D 1.0f"), "quiet-levels-3d"),
    "direct-floor-without-direct": (("level_direct = level_3d * gain_from_millibels(stream->direct);",
                                     "level_direct = level_3d;"), "quiet-levels-3d"),
    "room-floor-with-direct": (("*room *= room_scale;", "*room *= scale;"), "quiet-levels-3d"),
    "room-send-capped": (("*room = VOICE_LEVEL_3D * gain_from_millibels(level) * distance_gain;",
                          "*room = gain_from_millibels(level) < distance_gain ? gain_from_millibels(level) : "
                          "distance_gain;"), "quiet-levels-3d"),
    "room-send-once": (("*room = VOICE_LEVEL_3D * gain_from_millibels(level) * distance_gain;",
                        "*room = gain_from_millibels(level) * distance_gain;"), "quiet-levels-3d"),
    # audio.channels "auto" stereo whatever the device (as every build was before the host said its channels)
    "auto-always-stereo": (('!strcmp(setting, "auto") && device_channels >= 6', '!strcmp(setting, "auto") && 0'),
                           "channels"),
    "auto-on-four": (("device_channels >= 6", "device_channels >= 4"), "channels"),
    # the 5.1 pan: the backs swapped, a back on the LFE, or the pan linear in the angle (the pairwise pan it was)
    "surround-backs-swapped": (("surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 }",
                                "surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 4, 5, 0 }"), "surround"),
    "surround-back-on-lfe": (("surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 }",
                              "surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 3, 0 }"), "surround"),
    # 3D voices on 5.1: the split between the pairs another (linear in the angle), the HRTF's or the pan's back pair
    # silent, a sound behind not mirrored ahead (the HRTF's duller behind), a 2D voice to the center alone skipped
    "surround-split-linear": (("share = 0.5f + (1.0f - fabsf(elevation) / 90.0f) * (fabsf(azimuth) / 90.0f - 1.0f);",
                               "share = fabsf(azimuth) / 180.0f;"), "surround"),
    "surround-hrtf-front-only": (("output[frame * 6 + 4 + ear] += heard * now.pair[1];",
                                  "output[frame * 6 + 4 + ear] += 0.0f * now.pair[1];"), "surround"),
    "surround-pan-front-only": (("gains[0][4] = left * pair[1];", "gains[0][4] = 0.0f * pair[1];"), "surround"),
    "surround-not-mirrored": (("output_channels == 6 ? fabsf(ahead) : ahead, distance", "ahead, distance"), "surround"),
    "surround-center-skipped": (("!ramp_room && !surround_heard)", "!ramp_room && (!surround_heard || TRUE))"), "surround"),
    "surround-linear-in-angle": (("float first = sinf(surround_ring_angle[segment + 1] - angle);\n"
                                  "\t\tfloat second = sinf(angle - surround_ring_angle[segment]);",
                                  "float first = cosf(1.57079633f * (angle - surround_ring_angle[segment]) /\n"
                                  "\t\t\t(surround_ring_angle[segment + 1] - surround_ring_angle[segment]));\n"
                                  "\t\tfloat second = sinf(1.57079633f * (angle - surround_ring_angle[segment]) /\n"
                                  "\t\t\t(surround_ring_angle[segment + 1] - surround_ring_angle[segment]));"),
                                 "surround"),
    # the LFE on 5.1 full range, as it was (an Xbox's AC-3 carried only its bass)
    "lfe-full-range": (("\tlfe_lowpass(output, frames);\n", ""), "lfe"),
    # a change ramped over the whole mix, as it was (8 ms at 512 frames, where an Xbox took 1 to 2 ms)
    "parameter-ramp-whole-mix": (("#define PARAMETER_RAMP_FRAMES 64", "#define PARAMETER_RAMP_FRAMES 100000"),
                                 "parameter-ramp"),
    # a change ramped over a mix of fewer than PARAMETER_RAMP_FRAMES, as it was (shorter the fewer the frames); the
    # HRTF's over the whole mix (8 ms at 480 frames); the crossfade with the HRTF a mix's share a mix
    "ramp-in-one-mix": (("stream->ramp_frames = PARAMETER_RAMP_FRAMES;",
                         "stream->ramp_frames = frames < PARAMETER_RAMP_FRAMES ? frames : PARAMETER_RAMP_FRAMES;"),
                        "block-size"),
    "hrtf-ramp-whole-mix": (("hrtf_process(stream, hrtf_direct, output, frame, ramp_frames,",
                             "hrtf_process(stream, hrtf_direct, output, frame, frames,"), "block-size"),
    "hrtf-fade-per-mix": (("ramp_hrtf_mix = hrtf_mix_end > hrtf_mix ? 1.0f / HRTF_FADE_FRAMES :",
                           "ramp_hrtf_mix = hrtf_mix_end > hrtf_mix ? 512.0f / HRTF_FADE_FRAMES / (float)frames :"),
                          "block-size"),
    # the LFE's low pass at the Dolby Digital LFE's 120 Hz, 8th order, as it was
    "lfe-120-hz": (("#define LFE_CUTOFF 86.0\n#define LFE_SECTIONS 2\n", "#define LFE_CUTOFF 120.0\n#define LFE_SECTIONS 4\n"),
                   "lfe"),
    # audio.spatial: "auto" panned on stereo; "speakers" through the HRTF; the VR mode by audio.spatial
    "spatial-auto-panned": (('return strcmp(setting, "speakers") != 0;',
                             'return !strcmp(setting, "headphones");'), "spatial-choice"),
    "spatial-speakers-hrtf": (('return strcmp(setting, "speakers") != 0;', 'return TRUE;'), "spatial-choice"),
    "spatial-vr-ignored": (("if (vr_running)\n\t\treturn vr_hrtf;", "if (vr_running && 0)\n\t\treturn vr_hrtf;"),
                           "spatial-choice"),
    # the HRTF: no time between the ears, no level between them, no elevation, nothing behind
    "hrtf-no-delay": (("parameters->delay[mirrored] = floorf(", "parameters->delay[mirrored] = 0.0f * floorf("),
                      "hrtf-cues"),
    "hrtf-no-shadow": (("const float *decibels = ear ^ mirrored ? direction.right : direction.left;",
                        "const float *decibels = direction.right;"), "hrtf-cues"),
    "hrtf-no-elevation": (("elevation = atan2f(up,", "elevation = 0.0f * atan2f(up,"), "hrtf-cues"),
    "hrtf-no-rear": (("azimuth = atan2f(fabsf(right), ahead)", "azimuth = atan2f(fabsf(right), fabsf(ahead))"),
                     "hrtf-cues"),
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
    "hrtf-no-volume": (("\t\tif (*hrtf_on)\n\t\t\thrtf_out->gain *= scale;\n", ""), "master-volume"),
    # the hand-off: the device's mix waiting for the game's thread to take its side over, or to say what it
    # finished; a setting left on the game's side
    "hand-off-waits": (("else if (pthread_mutex_trylock(&voices_lock))\n\t{\n\t\thand_offs_skipped++;",
                        "else if (pthread_mutex_lock(&voices_lock))\n\t{\n\t\thand_offs_skipped++;"),
                       "hand-off-never-waits"),
    "publish-waits": (("else if (pthread_mutex_trylock(&voices_lock))\n\t\treturn;",
                       "else if (pthread_mutex_lock(&voices_lock))\n\t\treturn;"), "hand-off-never-waits"),
    # Settings > Audio's SOUND: off cut at once (a click), off not held, on never back; a device opened again at
    # every frame, or not for a game started with the sound off (#510)
    "sound-cut": (("\tif (gain == target)\n\t{", "\tgain = target;\n\tif (gain == target)\n\t{"), "sound-off"),
    "sound-off-not-held": (("\t\tif (!on)\n\t\t\tmemset(output, 0, frames * channels * sizeof(float));\n", ""),
                           "sound-off"),
    "sound-never-back": (("\t\tif (gain < target)", "\t\tif (FALSE)"), "sound-off"),
    "device-every-frame": (("&& !open && read_at != changes;", "&& !open;"), "sound-off"),
    "device-never": (("return started && !paced_mix && on && !open && read_at != changes;", "return FALSE;"),
                     "sound-off"),
    "setting-not-taken": (("\tstream->room_hf = settings->room_hf;\n", ""), "hand-off-settings"),
}


def generated(source=None, fault=None, scalar=False):
    """the mixer: dsound_sdl.c from its constants to the end of the mixing (before the output), its resampler adding
    tap by tap if scalar"""
    text = source if source is not None else read(SOURCE)
    code = text[text.index("#define OUTPUT_RATE"):text.index("/* ---------- output")]
    code += "\n" + function(text, "sound_device_wanted") + "\n"
    # (a voice's I3DL2 source, as the game sets it, for mixer.c's obstruction)
    setter = text[text.index("#define STREAM_SETTER(body)"):]
    code += "\n#ifdef UNDER_TEST_I3DL2_SOURCE\n" + function(text, "stream_from_interface") + "\n" + \
        setter[:setter.index("\n\n")] + "\n\n" + function(text, "IDirectSoundStream_SetI3DL2Source") + "\n#endif\n"
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


# what only the game's 32-bit ABI shows (harness.NATIVE_64_BIT)
ABI_DEPENDENT = {
    "hand-off-settings": "the voice settings handed off are the game's structure, sized with its 32-bit longs",
}


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    skip_without_32_bit_abi(ABI_DEPENDENT, case)
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
    skip_without_32_bit_abi(ABI_DEPENDENT, control, case)
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
