# Loading artwork

`LOADING.jpg` is g0atbutt's supplied remake, retained verbatim. Its SHA-256
is `5062ff5c83664c6b2f692657ae864e9ad74d71b0af8af1e4305bd9a26dc3fa0f`.

`loading.rgba.zlib` contains 640×480 RGBA bytes, compressed with zlib. It is
made by decoding the JPEG, converting to RGBA and resizing with Pillow's
LANCZOS filter. No recoloring, thresholding or generated artwork is applied.

To regenerate, install Pillow and run `python tools/loading_assets.py`.
Normal builds need neither Pillow nor JPEG decoding: `tools/embed_assets.py`
embeds the committed compressed payload, which is decoded once at startup.

The committed conversion used Pillow 13.0.0 and zlib 1.3.2. The input is
1600×1200; the output has 1,228,800 uncompressed bytes and opaque alpha.
The compressed payload's SHA-256 is
`f66b23eb3960213d8f0c821cf8feed577fad5116ec8762bab21fdd199ad51359`.

## The main menu's ring

Once the main menu has been shown, the loading screen draws the menu's own
ring in the picture's place, against the dark as the picture's is (of the
menu's sky its planet alone, dim behind the ring, turned so that its dark
side lies low at the right as the picture's does; its stars and galaxy
left out), read from the player's `ui.map` as
the menu drew it (`port/linux/game/loading_scene.c`) when the map change
leaves the menu. Nothing of the map's is kept in the repository or the
releases; without a scene (the first load, before the menu) the picture
above is drawn as before.

The presenter draws the ring on the processor, into the same 640x480 image
the picture is (`port/linux/src/loading_ring.h`), so every renderer shows it
without pipelines of its own, and the picture's sweep runs over it as it did
over the picture (`loading_art.h`):

- still, as the picture is: the sweep's light and its trail are all that
  moves. The stock ring is framed as the picture shows it (the camera found
  by matching the picture's ring to the drawn one,
  `loading_ring_picture_framing`); another ui map's from the menu's camera;
- its far side soft and its near edge sharp, as the picture's are (a blur by
  its depth, `loading_ring_focus`);
- in the picture's colours: a narrow band of hues about its blue, cyan to
  violet, by the ring's own colour, with the picture's grain and a few
  specks of cyan, lavender and near white in its detail; its brightness
  matched to the picture's, its detail etched; the picture's words over it;
- the sweep lights it: the trail is fed the ring etched, with the light of
  the sweep's band (a light behind the ring at the band's place) on its rim
  and in its detail, so that its edges flare as the band passes;
- dimly in view between the sweeps (`LOADING_ART_SCENE_BASE`);
- in VR, a stereo pair on a window in the world, 8 m before the head as the
  load began, as wide as the picture's camera saw: it starts flat, both
  eyes' pictures the same, and as the first sweep crosses the eyes come
  apart and follow the head, the ring found far behind the glass at eight
  times the game's scale; the sweep's light stays on the window. The
  level's first pictures then come up out of the dark
  (`vr_loading_fade_draw`).

## Sharp at the screen's resolution

On a screen of more than 540 rows the scene and the sweep's light are drawn
at the screen's own rows (at most 2160, and 1920x1080's pixels in all,
`LOADING_ART_HD_BUDGET`), not scaled up from 480 (`loading_art_hd` in
`loading_art.h`):

- the ring is drawn once a load at that size, coloured and etched as the
  480 rows' is, the picture's words over it sampled from the picture with a
  cubic filter (without a scene, the picture itself so scaled);
- each frame, the 480 rows' trail (blurry by nature) is scaled up, and the
  sweep's light added at the sharp pixels, between the steps where the band
  is, the ring lit by the sweep's glint as the 480 rows' feed is;
- the rows are drawn by four threads, the presenter's one of them, and the
  still ring's glint alone is drawn again each step (`loading_ring_glint`):
  a 1080p frame takes about 9 ms on a Ryzen 9 5900X, the 480 rows' frame
  alone about 9 (it took 15).

VR's window stays at 640x480, bit for bit as before.

## The chant

The Xbox's loading screen played a monk's chant (progress_bar.c's
`compressed_monk_tone`, from the decompiled game) as four voices at four
pitches, each swelling with the load's progress. The port plays it the same
way (`platform_loading_chant` in `dsound_sdl.c`), under `audio.volume` and
`audio.effects_volume`, silent with the sound off; it fades out as the
level comes up.

## For the port's own waits

`loading_screen.h` shows the loading screen for a wait of the port's own,
with a caption under the words, the sweep following the wait's progress as
the Xbox's followed the load's (once across, its position the progress
smoothed with a slow wobble) or looping as a map load's does.

The caption is lettered as the words are: in capitals, in OpenCE (the
stand-in for the menus' titles' typeface, which the picture's "LOADING..."
is set in), its capitals 15 of the 480 lines to the words' 22, in the
words' blue as the picture has it down their capitals. The sweep lights it
as it lights the words: it is in what the sweep feeds the trail (in the 480
rows, so the trail's glow spreads from it), and at the screen's own pixels
the presenter draws its letters as the sharp frame draws the words, the
trail there, the sweep's band at its brightness, the letters dimly between
the sweeps (`loading_art_caption_shown`); VR's window has it in its frame.

Diagnostics: `HALO_LOADING_SCENE_DUMP=<file>` writes the scene as the
presenter receives it, and `HALO_LOADING_CAPTURE_DIR=<folder>` writes the
loading frames (`HALO_LOADING_CAPTURE_EVERY=<frames>`: every that many)
and, on macOS, the screen as shown, caption and all (`screen-*.ppm`,
`HALO_LOADING_SCREEN_EVERY=<frames>`, 60 unless set).
