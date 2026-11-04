# HDR (`display.hdr`)

How the renderer draws high dynamic range: one contract, decided once on
the shared side (`port/linux/src/xgpu_hdr.h`, `xgpu_hdr.c`, the front end
`d3d8_device.c`) and carried out by each backend: Metal (macOS, EDR) and
Vulkan (the Linux desktop: HDR10 or scRGB swapchains,
[port/linux/vulkan/README.md](../port/linux/vulkan/README.md)). Android and
the VR mode do not: the Frame's OpenXR swapchains are 8-bit sRGB on SDR
panels, and the extra bandwidth would cost the headset frame time for
highlights it cannot show.

## What the player gets

- **Faithful** (the default): the picture as the game draws it, with only
  its highlights brighter. Surfaces, terrain, skin, the HUD and the menus
  sit at *paper white*, the display's SDR white, exactly where the SDR
  picture has them. The light the game adds on top goes past it: plasma,
  explosions, muzzle flashes, the dynamic lights and their glows, lens
  flares, the sun's glare and a sky's additive layers. Colours stay in
  sRGB's gamut.
- **Punchy**: the whole scene lifted, its bright parts more, each colour
  keeping its hue, and the highlights pushed further toward the peak.
- **Glows**: the first-person weapon's displays (the assault rifle's
  ammunition counter), meters and additive glows reach up to 1.75 times
  paper white; the HUD's deliberate glows (a meter's flash, the motion
  tracker) up to 1.5 times. The rest of the HUD and the menus stay at
  paper white.
- **The light of the world**: the surfaces that are light, which the game
  can only draw up to white, glow past it up to 3 times paper white:
  the teleporters' fields, the levels' light fixtures and Covenant lights,
  energy shields, and the plasma bolts' and needles' glowing volumes.
- **Self-illumination**: the surfaces that light themselves glow past paper
  white up to 2 times it, short of the lights: door control panels,
  computer screens and displays (the Pillar of Autumn's bridge and
  corridors), Forerunner glyphs and panel lights, Covenant panels. Only
  their self-illumination does: the lit surface around it stays where the
  SDR picture has it.
- **Settings**: `display.hdr` (`auto`, `on`, `off`), `display.hdr_mode`
  (`faithful`, `punchy`), `display.hdr_paper_white_nits` (80 to 500 nits,
  or 0, the default: the system's SDR white; the older
  `display.hdr_paper_white`, in percent of the system's SDR white, is still
  read while it is 0), `display.hdr_ui_nits` (the UI's brightness, below).
  On a renderer that draws HDR (the menus' `platform="hdr"`), Video
  Setup's Brightness page has a row for each under its HDR section, and a
  DISPLAY PEAK row that shows the display's peak and SDR white as the
  system reports them. Its Gamma row (`display.gamma`) is SDR's brightness:
  an HDR swapchain's frame is never curved by it.
- **UI brightness** (`display.hdr_ui_nits`): what is drawn over the tone
  mapped view (the HUD's bitmaps and meters, the menus, the scope's reticle
  and frame, the screen flashes) at its own white, 80 to 500 nits, or 0, the
  default: paper white's, the picture as before. The HUD's glows keep their
  ratio above it; the first-person weapon's displays and the
  self-illumination are the 3D view's and stay with paper white. It is
  never brighter than the display's peak.
- **Fallback**: `auto` turns HDR on only on an HDR display; off, and on an
  SDR display, the frame is the 8-bit one, byte for byte. Highlights roll
  off under a soft shoulder to the display's peak (its EDR headroom, which
  follows its brightness), never clipping.

## The frame

The back buffer is RGBA16F. It holds the game's own values, blended by the
game's own maths: 1.0 is paper white, the encoding is sRGB's curve (extended
past 1 as sRGB's extended colour spaces extend it), the primaries sRGB's.
The pipeline is not made linear: the game's blends (its fog, its decals, its
transparent layers) were authored for gamma-space blending, and making them
linear would change every one of them. Nothing clips at 1 instead.

A value past 1 can only come of blending: every pixel shader's result is
clamped to [0, 1], as the NV2A's is. So a frame whose draws add no light past
white is the SDR frame, value for value (to half floats' precision: an 8-bit
target rounds each blend to 1/255, a half float keeps more).

## Which draws go past white

The front end tags each draw into the HDR back buffer
(`nv2a_pixel_shader_key.hdr`, `xgpu_hdr_classify`) from:

- the draw's blend: only an add whose factors are neither zero nor one and
  its complement can go past 1 from values in [0, 1] (an add, an alpha
  multiply add, a double multiply, a meter's constant blend), and only a
  subtraction or a factor of one less the destination's colour can go below
  0 over a pixel past white;
- the game's renderer's category, from `rasterizer_profile_begin` and
  `_end` (`halo_hdr_category`);
- what the drawing code knows (`halo_hdr_draw_kind`): a transparent group of
  the first-person weapon's own shaders (generic, chicago, meter; numeric
  ones are the ammunition counters), glass and water (a surface's specular
  light), a HUD meter while it flashes, the motion tracker; in the world
  (not the sky), a surface that is light: a generic or chicago shader
  the map gives radiosity power (the power it lights the level's
  lightmaps with), a plasma shader, a light's volume; a surface's pass that
  adds its self-illumination to its light (a model's, a structure's
  lightmap pass), and which of its combiner stages' constants are the
  self-illumination's colours;
- whether the 3D view is being drawn or the HUD over it (after the tone
  map).

| Draws | Examples | Tag |
| --- | --- | --- |
| Opaque, alpha blends, multiplies, min, max | the structure, models, fog, smoke, text | none: they cannot leave [0, 1] |
| Additive light in the 3D view | effects, particles, contrails, plasma shaders, light volumes, lens flares, the sun, the game's dynamic lights (diffuse and specular passes), the port's glows, a sky's additive layers | none: their light keeps going past white |
| A surface's look | double multiply decals and transparents; in Faithful the structure's specular lightmaps, reflections and mirrors, water's and glass's reflections; the fog screens | ceiling at paper white |
| Over the tone mapped view | the HUD's bitmaps and meters, screen flashes, screen effects that add light, menus | ceiling at paper white |
| The screen effects' passes | a scope's warp and mask, night vision: the view copied through the effect's target and back | unclamped: the view's light past white kept |
| The first-person weapon's glows | the ammunition counter (whatever its blend), meters, additive display parts | expanded, ceiling 1.75 times paper white |
| The HUD's glows | a meter flashing, the motion tracker | expanded, ceiling 1.5 times paper white |
| The light of the world (additive) | teleporters' fields, light fixtures, Covenant lights, energy shields, plasma bolts' and needles' volumes | expanded from lower, ceiling 3 times paper white |
| Self-illumination (a model's pass, a structure's lightmap pass) | door control panels, computer screens and displays, the Pillar of Autumn's bridge screens, Forerunner glyphs and panel lights | its self-illumination expanded from lower, ceiling 2 times paper white; the rest of the pass as the SDR frame has it |
| Destination alpha | any draw writing alpha whose blend could take it past 1 (a double multiply's) | alpha limited to 1 (the game reads it as a blend factor) |
| The enhanced dynamic lights | a structure's lightmap pass | their shoulder goes on past white (below white, unchanged) |

A **ceiling** means: the draw may take no channel past *max(what the pixel
had, the ceiling)*, nor below 0. A surface's draw over a pixel that was
already past white (a decal over a wall a plasma bolt lights) therefore
does not dim that light, and over a pixel under white it stops where the 8-bit
target stopped. A float target cannot clamp a blend, so the backend blends
such a draw in its pixel shader, reading the pixel it draws over (Metal's
programmable blending; Vulkan's local reads of the colour attachment, in
rasterization order where the device has it, else after a framebuffer-local
barrier before the draw), with the key's blend (`XGPU_HDR_BLEND_*`) and its
fixed-function blending off. One less the destination's colour is never
below 0 there.

**Expansion** brightens a glow where it is bright (its brightest channel
from 0.6 up; the light of the world's from 0.4, which the game draws
dimmer) by more of the light the draw added (its own colour: what was
under it is not brightened), up to its ceiling, which keeps its hue
(`xgpu_hdr_expand`). The counter's digits, cyan, reach about 1.5 times paper
white in their blue (MaxRGB). A teleporter's field keeps its dim body as
the SDR picture has it and its bright swirls pass white; a light fixture's
panel, near white in SDR, reaches the ceiling. The ceilings are one field
of the tag (`XGPU_HDR_CEILING_OF`); a glow is a ceiling past paper white's
(`XGPU_HDR_GLOW`).

**Self-illumination** is not a pass of its own. A model's shader adds it to
the light in its one pass, before the textures multiply that light
(`rasterizer_xbox_models.c`: combiner stage 1 adds the multipurpose map's
self-illumination mask times its colour, stage 1's constant, to the
vertices' diffuse light; the base map, the change colour, the reflection
and the fog follow); a structure's lightmap pass adds it to the lightmap's
light (`rasterizer_xbox_environment.c`: stages 0 to 5 make it from the
self-illumination map's primary, secondary and plasma masks, their
animated colours stages 3's and 4's constants, and the final combiner adds
it), and the textures' pass after multiplies the target by the diffuse
texture, which a float target lets past white. The pass is tagged
`XGPU_HDR_SELF_ILLUMINATION` with those stages (`HALO_HDR_KIND_SELF_ILLUMINATION`),
and the backend's shader runs its combiners twice: as the game has them,
and with those stages' constants 0, the pixel without its
self-illumination. What the first has past the second is the
self-illumination, brightened as a glow is, from the light of the world's
knee, toward 2 times paper white (`xgpu_hdr_self_illuminate`); in a
structure it is then multiplied by the surface's texture, as the game's
light is. The rest of the pass, the lit surface, stays as the SDR frame has
it, and a pixel without self-illumination is the SDR frame's. The
animation (a panel's flicker, a screen's plasma) is the game's: its
colours are the constants.

**The screen effects** (a weapon's zoom: the sniper rifle's warp and mask;
night vision; a script's) copy the tone mapped view into their own target
and draw it back through the NV2A's pixel stage, whose every result is
clamped to 1. Their target is RGBA16F with the back buffer, and their
passes are tagged `XGPU_HDR_UNCLAMPED`: the combiners' results, the final
combiner's inputs and the pixel's colour are not clamped above, so that the
view through a scope keeps its light past white; under white they are what
they were. The scope's reticle and frame are the HUD's, at paper white.

## The tone map and the encoding

The tone map runs over each window's 3D view after its lens flares and
before its anti-aliasing and the HUD (`halo_screen_tone_map`,
`xgpu_backend_tone_map`). Given the mode and the peak *P* (the display's
headroom over paper white; 1 on an SDR display):

- linear light *L* = sRGB's curve inverted (extended past 1); the SDR
  frame's colour *S* = the same of the value clamped to 1;
- **Faithful**: a pixel no channel of which is past 1 is left as it is.
  Past it, two colours are mixed by *t* = smoothstep(1, 2, the brightest
  channel's value): its own hue, *L* under the shoulder by its brightest
  channel, and white-hot, *S* (a channel at 1) brightened by the light that
  was clipped (luminance(*L*) / luminance(*S*)) under the shoulder. A glow a
  little past white keeps its colour; the core of an explosion or a plasma
  bolt is white-hot, as the SDR picture has it, only brighter.
- **Punchy**: the same, with *L* lifted by 1.2 and by up to 1.6 more as the
  SDR luminance goes from 0.5 to 1, and the shoulder starting at
  min(1, 0.75 *P*): the whole picture brighter, saturated colours kept.
- **The shoulder**: identity up to its knee, then Reinhard's curve meeting
  the line at its slope, toward *P* and never reaching it.

The encoding, as the frame is presented (after the HUD), makes it linear,
at most *P*, times paper white, in the display's encoding:

| Output | Values | Paper white |
| --- | --- | --- |
| SDR | the frame clamped to [0, 1] (the window's 8 bits) | the window's white |
| EDR (macOS) | extended linear sRGB, 1.0 the system's SDR white | the setting's nits over 100 (macOS gives no nits: 1.0 taken as its reference modes' 100), at most the headroom |
| scRGB (Vulkan) | extended linear sRGB, 1.0 80 nits | Windows: the setting's nits, at most the peak. Linux (Wayland): 1.0 times the setting over the system's SDR white, at most the headroom |
| HDR10 (Vulkan) | BT.2020 primaries (the same colours: no saturation added), PQ | Windows: the setting's nits, at most the peak. Linux (Wayland): 203 nits times the setting over the system's SDR white, at most the headroom |

Windows' compositor shows both encodings in absolute nits. A Wayland
compositor (`wp_color_manager_v1`) maps the surface's reference white onto
its own, its SDR brightness, and the Linux drivers give the surface no
reference of their own, so the protocol's is the encoding's: 203 nits for
PQ, 80 (scRGB's 1.0) for linear sRGB (KWin: `ColorDescription::toOther`).
With KDE's SDR brightness at 500 nits and a 800-nit peak, paper white at
the system's white is PQ's 203 nits, shown at 500, and the peak PQ's 325,
shown at 800. The system's SDR white in nits is the compositor's reference
luminance, which SDL 3.4 does not keep: the SDL the Linux builds bring is
patched to give it as the window's property
`openh1.wayland.reference_luminance`
(`port/android/patches/sdl-wayland-reference-luminance.patch`), so that a
paper white set in nits is those nits on the panel.

**The UI's white.** The frame's 1.0 is the UI's white
(`xgpu_hdr_frame.ui`: the UI's nits over paper white's, 1 by default and
for SDR): the tone map leaves the 3D view's light divided by it
(`xgpu_hdr_to_ui`), and the encoding takes the frame's light times it
before its peak and paper white. The view is shown as it was, at paper
white, and every draw after the tone map, which the frame's values put
under 1 (or a glow's ceiling), at the UI's nits, with nothing in the draws'
tags or their shaders changed. Both are the encoding's units, so the UI
follows paper white's encoding: on Linux its nits over the system's SDR
white, relative to the compositor's reference. The draws over a frame with
no 3D view (the menus, the loading screen) are the UI's. A screenshot's
picture (`xgpu_hdr_sdr_pixel`) is of the frame's values: with a UI white
past paper white's, its view a little darker than the screen's.

**The display, shown.** The backend writes the display's SDR white in
nits beside its headroom (the ring's `display_white` and
`display_white_reported`: Vulkan's from SDL, BT.2408's 203 where it gives
none; Metal's EDR 1.0, taken as 100 nits, never reported; Metal's headroom
there its potential one, the display's peak, which the headroom now, the
tone map's, rises toward as EDR content is shown), and the front end shows
its peak (the headroom times that white) on Video Setup's
DISPLAY PEAK row (`halo_hdr_display_text`, `xgpu_hdr_display_text`) and
logs it at the start and as it changes ("hdr: display peak 800 nits, SDR
white 203 nits, from the system").

The setting's nits (`xgpu_hdr_paper_white_nits`) are
`display.hdr_paper_white_nits`, or, while it is 0, the system's SDR white
(SDL's on Vulkan, or BT.2408's 203 nits where it gives none; EDR's 1.0 on
macOS) times `display.hdr_paper_white`'s percent (100 by default): the
picture of a config written before the nits, whatever its percent, is as
it was. The peak, *P*, is the display's headroom over the system's SDR
white divided by paper white over it.

The curves are one GLSL source (`xgpu_hdr_functions_source`, the tone map's
and the encoding's main functions in `xgpu_hdr.c`), compiled to SPIR-V by
`tools/vulkan_post_shaders.py` and from it to MSL by
`tools/metal_post_shaders.py` (SPIRV-Cross; the in-place tone map with
`--msl-framebuffer-fetch`), as FXAA and SMAA are. The C in `xgpu_hdr.h`
(`XGPU_HDR_REFERENCE`) is the same maths: the reference the tests hold the
shaders to, and the host's conversion of shots.

## What the backends do

The front end sends: the state (`xgpu_backend_hdr`: drawn in HDR, the mode,
paper white), the back buffer's format (`XGPU_FORMAT_RGBA16F` in its target
description and the screen's pipeline keys), each draw's tag in its pixel
key, and the tone map (`xgpu_backend_tone_map`). It reads the display from
the backend (`xgpu_backend_display_hdr`: whether it shows HDR, its headroom;
the stream's ring carries it, written by the host). A backend declares
`xgpu_backend_capabilities.hdr` and:

1. makes the back buffer RGBA16F, and its copies (the feedback copy of a
   target drawn into, anti-aliasing's) of its format;
2. carries out each tag: a ceiling or alpha limit blends in the shader with
   the pixel drawn over and the key's blend; expansion; the lights' raised
   shoulder;
3. tone maps each 3D view with the shared shader, a draw in the pass that
   drew the view reading its pixels where they are drawn (Metal: in tile
   memory; Vulkan: the colour attachment as an input attachment,
   `VK_KHR_dynamic_rendering_local_read`);
4. encodes the frame for its display and fills in the frame's paper white,
   peak and output (`struct xgpu_hdr_frame`) from what the display says
   (Metal: NSScreen's EDR headroom, polled and read when the window changes
   screens; Vulkan: SDL's HDR state, SDR white and headroom, and the
   surface's HDR colour spaces, the swapchain made again as they change;
   the encoding's limit at the peak is what holds the HUD's glows under it
   on a display of less headroom than their ceiling);
5. converts a half-float target to 8 bits where the front end reads one back
   (screenshots, the determinism log).

## Tests

- `tools/test_hdr.py`: the UI's white (its default paper white's, exactly;
  its ceiling the display's peak; the view shown as it was under it; the
  HUD's glows its ratio above it); the display's readout and log line;
  the classification, table by table; the reference
  (Faithful leaves under white exactly as it is, never passes the peak,
  brightens with the light; Punchy lifts and keeps hues; a core is white-hot;
  the encodings' paper white and peak; PQ's 203 nits); the GLSL's constants
  against the reference's; the game's hooks.
- `port/macos/tests/xgpu_metal_test.m` (`test_stream_hdr`, through the
  command stream's table on the GPU): sixteen golden colours (under white,
  at it, a little and far past it, saturated and grey), tone mapped at peaks
  1, 4 and 16 in both modes and encoded for EDR, each against the reference
  to half floats' precision; the game's alpha kept; under white exactly as
  it was.
- `tools/test_hdr_backends.py`: the Vulkan backend's shaders, which need a
  Vulkan device, made Metal's by SPIRV-Cross and run on a Mac's GPU: its
  tone map and encoding as shipped (`xgpu_vulkan_post_shaders.c`) over the
  same golden colours against the reference (scRGB's and HDR10's encodings
  too, and both with the UI's white past and under paper white's); and each tagged draw (every ceiling, the three glows' expansion,
  the alpha limit, over each of eleven blends) as the SPIR-V emitter writes it and as
  the MSL emitter writes it, over the same 512 pixels: the two backends
  leave the same values, none past its tag's limit; a screen effect's
  pass, unclamped, its colour past 1 in both; and a self-illuminated
  pass, the reference's values in both, past 1 only by its
  self-illumination, and the same pass untagged the SDR frame's. Everywhere: the two
  backends' tone map and encoding are one GLSL source but for the
  uniforms' binding.
- `port/linux/vulkan/tests/passes_test.c` (over a fake Vulkan): the local
  reads' barriers and input sets, the pipelines' blending, the swapchain's
  output for the frames and the display, the encoding, the tone map's
  draw; `tools/spirv_corpus`: every tag's SPIR-V valid for Vulkan 1.1.
- `tools/test_metal_post_shaders.py`: the generated MSL is what the tool
  writes from the GLSL.
- SDR identity: with `display.hdr` off, and `auto` on an SDR display, every
  frame's hash in `debug.determinism_log` matches master's.

## Choices, and where they depart from the plan

- **The light of the world is what the map says is light.** A transparent
  shader with radiosity power lights the level (the tool's lightmaps take
  its light), so the map's own author called it a light: the
  teleporters' fields (power 10) and cones, the levels' fixtures and
  Covenant lights. That is a property of the shader, not its name, so a
  Custom Edition map's teleporters and lights glow too. Of those, only the
  additive draws glow (an alpha blend, the monitor's glass, cannot leave
  [0, 1]; nor can the holograms' maximum). The plasma shaders (energy
  shields) and the lights' volumes (the plasma bolts', the needles', a
  Ghost's bolts) are light by their kind. Additive shaders without power
  (powerups, the warthog's dials, the KOTH hill) stay as the game blends
  them; the effects' particles, contrails and lens flares too, whose
  layers already pile up past white (explosions, muzzle flashes): a
  ceiling would stop that. The first-person weapon's own glows keep their
  ceiling.
- **Self-illumination is found by drawing the pass twice in its shader.**
  A door's panel is one pass of an opaque model or structure, its
  self-illumination one term of its combiners; the game draws no separate
  pass of it to tag. Its combiners run again with the self-illumination's
  colours 0 (only the stages after them are worked twice), so that the
  difference is that term, whatever the combiners do after it (a model's
  textures, reflection and fog); no combiner program is read for its
  meaning. Its ceiling, 2 times paper white, is under the lights' 3: a
  screen lights no room, and glows short of the fixture above it. It is
  the game's own self-illumination, in Faithful and Punchy alike, as the
  light of the world is; the sky's models are left as they are.
- **Ceilings blend in the shader, not by clamping after.** A clamp after a
  draw (a second draw taking the minimum) would dim light that was already
  past white under it: a specular pass over a wall a plasma bolt lights
  would wipe that light out. Only reading the pixel can stop a draw at
  max(what was there, the ceiling).
- **Faithful's colour past white mixes hue and the SDR colour.** Keeping hue
  alone would make plasma cores deep blue where the game shows them
  white-hot; the SDR colour alone would wash out a glow that is barely past
  white (the counter). The mix keeps both looks.
- **sRGB's curve, not a 2.2 power.** Untagged 8-bit content on a Mac reaches
  the panel through its sRGB-curve transfer, so sRGB's curve keeps the
  shadows where the SDR picture has them.
- **sRGB's gamut.** The EDR layer is tagged extended linear sRGB, as the
  owner asked: no colour is pushed into P3. The SDR layer stays untagged (it
  is unchanged), which on a P3 panel shows the game's colours slightly more
  saturated than sRGB's; switching HDR on a Mac with an XDR display shows the
  true sRGB colours.
- **HDR on an SDR display** (`on` where `auto` would be off) uses the same
  tone map at a peak of 1: surfaces as they are, highlights compressed into
  white with their colour rolled toward white-hot.
- **Vulkan reads the attachment where it is drawn, or draws SDR.** The
  ceilings need the pixel drawn over, so the Vulkan renderer draws HDR only
  where the device has `VK_KHR_dynamic_rendering_local_read` (recent
  desktop drivers); elsewhere it reports no HDR and
  the frame stays the 8-bit one. A copy of the target before each tagged
  draw would cost a full-screen copy per HUD bitmap; a separate pass would
  break the game's order of draws. Where the device also has
  `VK_EXT_rasterization_order_attachment_access`, the reads are ordered as
  Metal's are; without it, each tagged draw is ordered after those before
  it by a framebuffer-local barrier, and only two triangles of the same
  tagged draw over the same pixel are unordered.
- **HDR10 on Linux, scRGB where asked or on Windows.** Linux compositors'
  HDR (KDE's, GNOME's, gamescope's) is PQ's; Windows' compositor is scRGB's.
  Both encodings are the shared one; the swapchain takes the one the
  surface has, preferring the platform's.
- **The tone map is no pass of its own on either backend.** It is a draw in
  the view's pass, reading its pixels where they are drawn; the owner asked
  for no new full-screen pass where avoidable. The encoding, which the
  presentation must do anyway, is the one full-screen draw HDR adds on
  Vulkan (into an image of the swapchain's format, which the present's blit
  letterboxes, as Metal's presentation draws into its EDR drawable).
