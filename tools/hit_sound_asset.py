#!/usr/bin/env python3
"""Makes the hit sound (audio.hit_sounds: port/linux/game/hit_sounds.c), a
PLACEHOLDER until a better one is made:

    python tools/hit_sound_asset.py           # writes the WAV and the C data
    python tools/hit_sound_asset.py --check   # fails if either is missing or not as written

The sound is an original beep, made here from nothing but its numbers: a
sine of HIT_FREQUENCY with a quieter octave above it, HIT_MILLISECONDS
long, rising in HIT_ATTACK_MILLISECONDS and dying away, and faded to
silence at its end, at 48 kHz, 16-bit, mono. It copies no other game's
sound (not Halo PC's or Custom Edition's).

It writes port/assets/sounds/hit_sound.wav, to listen to, and
port/linux/game/hit_sound_pcm.inc, its samples as C data, which the game
plays from (no file beside the game). Run it again after changing the
numbers below.
"""

import argparse
import math
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WAV = ROOT / "port/assets/sounds/hit_sound.wav"
INC = ROOT / "port/linux/game/hit_sound_pcm.inc"

RATE = 48000
HIT_FREQUENCY = 1320.0
# (the octave above, of the tone's level: a little brightness)
HIT_OCTAVE_LEVEL = 0.18
HIT_MILLISECONDS = 65
HIT_ATTACK_MILLISECONDS = 2.0
# (the decay's time constant, and the fade to silence over the sound's last part)
HIT_DECAY_MILLISECONDS = 22.0
HIT_FADE_MILLISECONDS = 15.0
# (its peak, of full scale: the game plays it quieter, hit_sounds.c's
# HIT_SOUND_VOLUME)
HIT_PEAK = 0.5


def samples() -> list:
    """the sound's 16-bit samples, from silence to silence"""
    count = RATE * HIT_MILLISECONDS // 1000
    attack = RATE * HIT_ATTACK_MILLISECONDS / 1000.0
    fade = RATE * HIT_FADE_MILLISECONDS / 1000.0
    decay = RATE * HIT_DECAY_MILLISECONDS / 1000.0
    raw = []
    for n in range(count):
        t = n / RATE
        tone = math.sin(2.0 * math.pi * HIT_FREQUENCY * t) + \
            HIT_OCTAVE_LEVEL * math.sin(2.0 * math.pi * 2.0 * HIT_FREQUENCY * t)
        envelope = math.exp(-n / decay)
        if n < attack:
            envelope *= 0.5 - 0.5 * math.cos(math.pi * n / attack)
        left = count - 1 - n
        if left < fade:
            envelope *= 0.5 - 0.5 * math.cos(math.pi * left / fade)
        raw.append(tone * envelope)
    peak = max(abs(value) for value in raw)
    return [int(round(value / peak * HIT_PEAK * 32767.0)) for value in raw]


def wav_bytes(pcm: list) -> bytes:
    data = struct.pack(f"<{len(pcm)}h", *pcm)
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 1, RATE,
                         RATE * 2, 2, 16, b"data", len(data))
    return header + data


def inc_text(pcm: list) -> str:
    lines = [
        "/* hit_sound_pcm.inc: the hit sound, a PLACEHOLDER (made by tools/hit_sound_asset.py,",
        "which says how; port/assets/sounds/hit_sound.wav is the same sound): 16-bit mono PCM",
        f"at {RATE} Hz, {len(pcm)} samples. Do not edit: run the script again. */",
        f"#define HIT_SOUND_RATE {RATE}",
        f"#define HIT_SOUND_SAMPLES {len(pcm)}",
        "static short const hit_sound_pcm[HIT_SOUND_SAMPLES] =",
        "{",
    ]
    for start in range(0, len(pcm), 12):
        lines.append("\t" + ", ".join(str(value) for value in pcm[start:start + 12]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if the files are not as this writes them")
    arguments = parser.parse_args()
    pcm = samples()
    outputs = {WAV: wav_bytes(pcm), INC: inc_text(pcm).encode()}
    if arguments.check:
        stale = [path for path, data in outputs.items() if not path.is_file() or path.read_bytes() != data]
        for path in stale:
            print(f"{path.relative_to(ROOT)} is not as tools/hit_sound_asset.py writes it", file=sys.stderr)
        return 1 if stale else 0
    for path, data in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    # (the WAV read back, so a malformed header shows here)
    with wave.open(str(WAV)) as check:
        assert check.getframerate() == RATE and check.getnframes() == len(pcm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
