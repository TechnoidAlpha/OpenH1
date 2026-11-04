# The renderer's manifest

`manifest.txt` lists every shader and pipeline the game draws with on the
stock maps, so that a backend can build them when the game is built instead
of the first time a draw needs one (a stall of tens of milliseconds each).

It belongs to the game, not to a backend: it is what the shared front end
(`port/linux/src/d3d8_device.c`) hands any backend. Its format is in
`port/linux/src/xgpu_manifest.h`; `xgpu_manifest_read` reads it.

## What it holds

- **Vertex programs:** the NV2A microcode of the game's vertex shaders
  (`source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_data.inc`), each
  named by its hash.
- **Pixel shader keys:** the combiner registers and render states the game's
  code sets for a draw (`nv2a_pixel_shader_key`), each named by its hash. The
  game builds them at each draw from its code and from the shader tags'
  fields (the transparent shaders' stages, their blend and fog).
- **Draws:** each pipeline key the draws use (the vertex program, the blend,
  depth, stencil, raster and target state, and the pixel key), with the
  vertex layout and the texture stages' sampling (sampler type, addressing,
  border colour) that a backend's shaders and pipelines can depend on.

Nothing else from the maps is in it: no textures, geometry, sounds, strings
or tag names, and no coordinates. Every value is a register, a format or a
state number of the Xbox's GPU. The build does not need the maps.

## How it is made

The front end records each new draw configuration into a file with
`debug.render_manifest` (`HALO_RENDER_MANIFEST=<file>`). The recordings are
merged:

```sh
python tools/render_manifest.py merge port/assets/render/manifest.txt <recording>...
```

`merge --replace` makes the manifest from the recordings alone.

The committed one is each stock map's first frame drawn through every shader
its tags hold (`display.prepare_shaders`: `port/linux/game/render_enumerate.c`),
recorded between the recording's `# enumerating` and `# prepared` notes, one
short run a map, all 24 merged.

The vertex shaders are not recorded: they are the game's code's, 67 programs
each with its declaration. `python tools/render_manifest.py vertex <file>`
reads them from the decompilation (`rasterizer_xbox_vertex_shaders.c` and
`_initialize.c`) as `v` and `s` records; a build makes every vertex function
from them, whatever a map or play meets.

## How it is checked

A recording made with a build whose shaders come from the manifest has no
draw the manifest does not:

```sh
python tools/render_manifest.py stats port/assets/render/manifest.txt <recording>...
```

## A map's enumeration

The manifest is the stock maps'. A map the manifest does not know (a Custom
Edition map) has its shaders and pipelines made as it loads instead: with
`display.prepare_shaders` (on by default), the map's first frame is drawn
first through every shader its tags hold, in each way the game's code draws
them (`port/linux/game/render_enumerate.c`): every material of the
structure (one of each shader, lightmap and flags), in each fog, with every
light tag's light and every model's shadow; then the materials of its other
structures, read as the map loaded, each drawn by a material of the first
standing in for it, so that a load zone's switch has nothing to make (a
structure with materials none could stand in for draws those at its own
first frame); every
model's every geometry, in each lighting, function and colour the models'
code tells apart, with its objects' effects (shields, camouflage) and in
first person; every effect, decal, lens flare, sky, detail object, HUD
element, font, screen flash and screen effect; a mirror's window where the
structure has one. The draws are the map's enumeration
(`xgpu_manifest_enumeration`), which a backend that makes its pipelines
apart from the draws (macOS's Metal) makes during the loading screen.

The front end makes a draw's keys canonical first, so that what an earlier
draw left in the device's state is not part of them where the draw does not
read it: the combiner stages past the combiner count and the portions whose
results nothing reads, a texture stage's fields its mode does not read, the
sampler of a stage that samples nothing (`d3d8_device.c`), a 2D texture's
third address mode (`xgpu_manifest.c`).

### How it is checked

A recording (`debug.render_manifest`) holds the enumeration (the draws
between `# enumerating` and `# prepared`) and marks each later draw the
enumeration does not have with a `# missed:` line of the functions it was
drawn in. Draws are compared as a backend compiles them (the depth, stencil,
culling, fill and depth bias left out: the Metal backend sets them for each
draw):

```sh
python tools/render_manifest.py check --image build/linux_x86_64/halo_guest.elf <recording>...
```

reports each recording's enumeration and misses, grouped by where they were
drawn (`--verbose` names the fields in which the nearest enumerated draw
differs, `--exact` compares whole draws), and exits 1 on a miss.
`tools/render_enumerate_sweep.py` plays each map of a list (every stock
map's, the menu's included, by default) offscreen with the scripted player,
a game for a few cores, and checks the recordings:

```sh
python tools/render_enumerate_sweep.py --data <the folder of maps/> --seconds 90 --jobs 8
```

Every stock map's play, the menu's included, has none. The loading screen's draws
(`render_frame_pregame`), made before the next map is loaded, are no map's:
with multisampling its progress bar is the manifest's alone.

## Another backend

A backend reads the manifest with `xgpu_manifest_read` and builds each draw
as it would at the draw (the Mac's, `port/macos/README.md`, builds the vertex
shaders' functions and those of the pixel programs its combiner's function
cannot draw into the app's library, and checks the rest against it):

1. It decodes the vertex program's microcode (`nv2a_vertex_program_decode`)
   and the pixel key (`xgpu_stream_pixel_key_restore`, then
   `nv2a_pixel_program_decode`).
2. It writes their shaders: SPIR-V with `xgpu_spirv.c`, or MSL with
   `xgpu_msl.c`.
3. It makes the pipeline from the key, the layout and the stages.

A Vulkan backend can fill its pipeline cache.
