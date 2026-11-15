# Sounds

`hit_sound.wav` is the hit sound (`audio.hit_sounds`:
`port/linux/game/hit_sounds.c`), a **placeholder** until a better one is
made. It is an original beep that `tools/hit_sound_asset.py` makes from its
numbers alone (a 1320 Hz sine with a quieter octave, 65 ms, rising in 2 ms,
dying away and faded to silence; 48 kHz, 16-bit, mono). It copies no other
game's sound: not Halo PC's or Custom Edition's, nor a recreation of one.

The game plays it from `port/linux/game/hit_sound_pcm.inc`, the same
samples as C data, which the script writes with the WAV. To change it,
change the script's numbers and run it again
(`python tools/hit_sound_asset.py`; `--check` says whether both files are
as it writes them).
