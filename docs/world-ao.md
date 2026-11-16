# World-space ambient occlusion (#578)

`display.ambient_occlusion = "world"`: ambient occlusion found in world space, built for how Halo is lit.
It has two sources:

1. A **signed distance field (SDF) of the structure BSP**. It is baked on the player's machine from the player's own
   map, once, and cached.
2. **Analytic proxies** (spheres, capsules, boxes) for the dynamic objects.

Both apply to the **ambient term alone**, through the per-surface ambient slot #574 added. Nothing is read from the
screen, so there's no depth pre-pass, no stencil marks (#545), no noise and no denoiser. Off-screen occluders count,
and the result doesn't shimmer at the screen's edges. The same code serves the desktop, the Steam Deck, mobile, VR's
eyes (it doesn't depend on the eye) and Metal with multisampling.

This branch (`world-ao`) is the skeleton: the interfaces, stubs, hooks and tests. Four workstreams fill it in
parallel ([Workstreams](#workstreams)). With the setting at anything but `"world"` nothing new runs. `"world"`
shades with what of the field and the proxies is ready (D's shading).

## Principles

- **Render-only and deterministic.** Nothing here is read by the simulation, sent between machines, saved, or able to
  change a tick. The bake reads tag data, which is read-only once loaded, and works from a copy of it. The result
  depends only on the map and the frame's drawn pose: no history, no randomness.
- **The level doesn't darken itself.** Bungie's radiosity lightmaps already hold the level's own occlusion. The SDF
  never shades the level's lightmap pass. The level is darkened only by objects' proxies, and objects are darkened by
  the level's SDF and by other objects' proxies.
- **Only the ambient light.** The flashlight, the dynamic lights (classic and enhanced), specular, self-illumination,
  decals, transparents and the first-person weapon stay as they are, exactly as with #574's GTAO.
- **Never block or lengthen a load.** The owner's long-term goal is no load screens at all. A map with no field yet
  loads exactly as fast as it does today ([The bake's schedule](#the-bakes-schedule)). With the setting on, a map's
  preparation makes the world's pipelines and the pre-pass's twins with the rest, so none is made in play (#672;
  [Pipelines](#pipelines-in-the-maps-preparation)).
- **Off is pixel-identical.** Off emits no stream records and no new shader code: the emitters write byte-identical
  shaders. The game tells the front end only where a window's opaque surfaces are drawn (`halo_world_ao_span`),
  which the renderer's manifest notes ([Pipelines](#pipelines-in-the-maps-preparation)). The Classic preset sets `display.ambient_occlusion = "off"`, so Classic always bypasses all of this.

## The frame

```
scenario_load ─► world_ao_map_loaded ─► world_scene_map_loaded (meshes, ids)
                                     └► world_sdf_map_loaded  (cache hit: load; miss: background bake)
render_frame ──► world_ao_frame_begin ─► world_scene_frame_begin (objects as drawn: render_interpolation pose)
                                      └► world_sdf_frame (upload a finished field, a few slices a frame)
render_window ─► world_ao_window_begin ─► world_proxies_window (≤64 shapes) + field place
                                       └► halo_world_ao_window ─► XGPU_STREAM_WORLD_AO_WINDOW ─► backend block
                 world_ao_prepass_begin ─► the pre-pass: render_objects + structure_render_lightmaps again,
                                           each world draw as its twin, writing its pixel's inputs
                 world_ao_prepass_end ─► the occlusion's pass (a quarter of the pixels) ─► the AO texture
                 render_objects (world_ao_object_begin per object: owner slot, reference)
                 structure_render_lightmaps (each surface reads its pixel's occlusion, upsampled)
               ─► world_ao_window_end (block withdrawn; later passes never see it)
```

The hooks are in `source/render/render.c` (`render_frame`, `render_window`) and `source/scenario/scenario.c`
(`scenario_load`, `scenario_switch_structure_bsp`, `scenario_unload`), each marked `port:`.

## Found once a pixel

The occlusion was found in the surfaces' shaders as each draws, so a pixel paid for it once per opaque layer
drawn over it (a30's start on a Radeon 8060S: 2.0 to 2.4 evaluations a visible pixel; an Apple GPU's hidden
surface removal leaves the opaque layers' overdraw out, not the alpha-tested foliage's). It is now found once:

- **The pre-pass.** `render_window` walks the window's opaque surfaces twice (`render_world_ao_prepass`: their
  transparent parts, widgets and fog screen left for the second walk, the objects found once). In the first, the
  backend draws each draw with the world's light as its **twin** (`xgpu_stream_world_ao_twin`: the same pipeline, its
  pixel key `XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS`, into an RGBA32Uint target and a depth target of its own) and
  draws every other opaque depth-writing draw as an **occluder** (its test made strictly nearer, writing no inputs:
  the first-person weapon, a surface lighting itself), so no hidden surface's occlusion is found. Blended draws and
  clears are skipped. A twin writes its pixel's inputs: the normal (octahedral), the owner slot, the reference or the
  static scenery's page's texel, the lightmap pass's flags, and its distance from the eye.
- **The pass** (`xgpu_msl_world_ao_pass`, `xgpu_spirv_world_ao_pass`): one triangle over the window. By default it
  runs at a quarter of the pixels: each 2x2 block's nearest drawn pixel, its position found again along the camera's
  ray (`world_ao_pass_position`), its occlusion one call (`world_ao_evaluate`: the static term, the field relative
  to its reference or the page's texel, times the dynamic shapes' product). It writes the occlusion, the distance
  and the normal.
- **The surfaces** (`XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED`) read it in place of finding it: the four texels about
  the pixel, by their bilinear weights and their closeness (distance, relative and scaled by how squarely the
  surface faces the eye, and normal); where none is close, the closest of the 4x4 about it (`world_ao_upsample`).
  A thin silhouette keeps its own value, and the wall behind keeps the wall's.

- **The dynamic objects by rays** (`world_ao_traced`). The pass no longer walks the dynamic objects' spheres,
  capsules and boxes: it casts `XGPU_WORLD_AO_RAYS` (4) short rays a pixel through the distance-field scene's objects
  ([The distance-field scene](#the-distance-field-scene-world_cascadesh)), cosine-weighted about the normal (Duff's
  frame; stratified, turned for each pixel and frame by interleaved gradient noise), from 0.1 wu off the surface to
  2 wu. A hit at t counts 1 − (t / 2)². The share is the cosine-weighted share of the hemisphere the objects block,
  the same quantity the shapes' exact forms found. A pixel no object is within 2 wu of casts none: one fetch of the
  objects' distance in cascade 1, whose range is 2 wu, says so first. Still walked: the occlusion fields (vehicles,
  scenery: they hold occlusion, not distance), and the static scenery's shapes where a lightmap page has no layer.
- **Temporal accumulation.** Each frame's occlusion is blended into the last frame's at the same point (a fifth of
  the new). The point is reprojected by the window's previous camera, and the history is taken only where it is the
  same surface: its distance from that frame's eye within 5%, its normal within about 37°. Otherwise this frame's
  stands alone. The backend keeps two occlusion textures in turn. The front end gives the history only where one
  window draws a frame, this one and the last, alike in place (split screen's windows share the texture), so a
  camera cut, a resize or a second window starts afresh. `HALO_WORLD_AO_NO_TRACE=1` (for measuring) walks the shapes
  as before, without history.

On the M2 (Metal, 3420x2146, quarter resolution; the pass and the GPU's busy time a frame, medians; the same build
with `HALO_WORLD_AO_NO_TRACE=1` the shapes' walk):

| Scene | Walked: pass / busy | Traced: pass / busy |
| --- | --- | --- |
| a30's start | 4.70 / 11.8 ms | **1.66 / 8.3 ms** |
| a10's start | 37.6 / 48.0 ms | **10.2 / 20.1 ms** |
| a30 outdoors, the walk toward the first bridge | 4.59 / 16.0 ms (most 11.0 / 23.4) | **3.80 / 14.7 ms** (most 4.8 / 18.5) |

`debug.world_ao_full_resolution` finds it at every pixel; `debug.world_ao_inline` finds it in the surfaces as
before. VR's eye passes, a mirror's view and any window not drawn into the back buffer with its depth buffer find it
inline. The first-person weapon, transparents, decals, water and the sky take no world light, as before. With
multisampling the pre-pass and the occlusion are a pixel's (at its centre, as the surfaces found it before).

Pipelines: the pass's is made with a map's (`prepass_prepare`), and so are the twins
([Pipelines](#pipelines-in-the-maps-preparation)). One missed is made on the compilers, never waited for: its
draw reads 1 until it is. The
Mac's library holds each form a map's preparation makes, derived from the manifest
(`xgpu_manifest_world_ao_forms`): the inline and sampled draws, their twins, and the occluders' twins.

## The scene (`world_scene.h`)

The world as a world-space renderer sees it, kept beside the game's state.

- **Static meshes:** each structure BSP's collision geometry (`collision_bsp` surfaces, edges and vertices, fanned
  into triangles), in world space. It has id `WORLD_SCENE_ID_STATIC_BIT | bsp_index` and the collision material
  index of each triangle. Collision geometry rather than render geometry, because it's watertight enough to sign the
  field and it's what objects stand on. `world_scene_static_triangles` hands the bake an owned copy, so a background
  thread never reads tags that a map unload frees.
- **Instances:** every drawn object, with id = its datum handle (index and salt), which is stable for the object's
  life and never reused while it could be confused. Each holds its definition (tag index, the proxies' key), its
  root transform this frame and last frame (as drawn, i.e. `render_interpolation.c`'s blended pose, so motion
  vectors come for free later), its bounding sphere as drawn, and its **lighting point** (where its ambient was
  sampled: `render_lighting`'s point below it).
- **The world light list:** the frame's dynamic lights as `dynamic_lights.c` ranks them, plus the structure's light
  fixtures. Each has a stable id, position, radius, cone and linear color. Nothing reads it yet; it's groundwork for
  RT shadows and for the "light list in world space" item.

The scene is built only while `"world"` is on, refreshed once a frame inside `render_interpolation_frame_begin/_end`
(the objects then stand where they are drawn), and freed at unload.

## The distance field (`world_sdf.h`)

### Geometry, bounds and resolution

There's one field per structure BSP. Its bounds are the collision geometry's, cut to **where objects can be**: the
box of the scenario's placements inside the BSP (players' starting points, netgame flags and equipment, bipeds,
vehicles, weapons, equipment, machines, controls, the AI's firing positions and starting locations; not scenery,
which can be a far backdrop) grown by `WORLD_SDF_PLACEMENTS_REACH` (32 wu) each way, then padded by
`WORLD_SDF_PADDING_VOXELS` (2). That drops the sky's box and sea floors out to the horizon: b30's collision BSP is
2000×2000×381 wu (95 triangles of sky and sea floor make 9.6 million of its 9.7 million square wu), its island and
everything placed on it about 300 wu across.

The field is **sparse**: a grid of cells of `WORLD_SDF_BRICK` (8) voxels' steps a side. A cell that a triangle may
reach within range of one of its voxels is a **brick** of 9×9×9 voxels (its far faces are its neighbours' near
faces, so a trilinear filter inside one brick is the whole grid's, exactly); every other cell is a single kind, all
outside (+range) or all inside (-range). Memory follows the surfaces' area, not the bounds' volume. The voxel is
**0.25 world units** (about 0.76 m; a Spartan is about 0.7 wu tall), grown in steps of ×1.125 only until the
table of cells and the bricks fit `WORLD_SDF_MAXIMUM_BYTES` (8 MB) of GPU memory (`world_sdf_plan`). At 0.25 wu a
brick holds 2 wu of surface each side: about 730 bytes per square wu of surface with the 3 wu range, so 0.25 wu
everywhere fits small and indoor BSPs and not the big outdoor ones (Blood Gulch's ~150,000 square wu would need
~110 MB). Those get the finest voxel their surface allows, which with the cut bounds is 2 to 6 times finer than a
whole grid's of the same memory (b30 0.91 wu, was 6.0; Blood Gulch 0.45, was 0.72). Flat ground is exact at any
voxel (trilinear holds a plane's distance); only corners soften.

### Encoding

Two textures, both clamped to the edge:
- the **atlas** of bricks, **R8 signed normalized** (`VK_FORMAT_R8_SNORM`, `MTLPixelFormatR8Snorm`; linear
  filtering of it is required on both APIs), 16×16 bricks a layer (144×144×9 voxels), as many layers as needed;
- the **table** of cells, **RGBA8**, read by texel: an atlas brick's x, y, z and the cell's kind
  (`WORLD_SDF_CELL_OUTSIDE`, `_INSIDE`, `_BRICK`).

A voxel × range is the signed distance at its centre: positive is outside (air), negative is inside solid.
range = `WORLD_SDF_RANGE_VOXELS` (12) × voxel, which is 3 wu at the finest voxel and covers the longest occlusion
step (2.4 wu, below). Precision is range/127, about 0.024 wu. A sample (`world_sdf_sample`, the shaders' too) reads
the point's cell in the table: ±range for a cell of one kind, else the brick's texel in the atlas, filtered. Cells of
one kind are most of the volume, so most taps cost one read of a small texture.

### The bake

Pure (`world_sdf_bake.c`), on N worker threads, each pass split by z slices or by cells:

1. **Plan** (`world_sdf_plan`): the finest voxel whose table and **marked** cells fit the budget. A cell is marked
   where a triangle is within range plus half the cell's diagonal of its middle (`world_sdf_mark`). When a voxel
   doesn't fit, the next tried is a step short of what the bricks' memory (falling at least as the voxel's square)
   says.
2. **Bin** (`world_sdf_bin`): each marked cell's triangles, by the same test.
3. **Bricks, narrow band, exact** (`world_sdf_bake_cell`): for each of a marked cell's triangles, visit its voxels
   within `range` of the triangle's plane and bounding sphere, and keep the least exact point-triangle distance.
   Voxels farther than `range` from every triangle stay at ±range: the encoding clamps there anyway.
4. **Sign:** whether the voxel's centre is inside solid, by the collision BSP's own point test (`bsp3d` leaf
   classification, the physics' answer), on a copy of the bsp3d nodes and planes. It's not done by normal
   orientation: Halo's BSPs aren't perfectly closed. A marked cell whose voxels all come out ±127 is a cell of that
   kind; an unmarked cell's kind is its middle's side (`world_sdf_cell_kind`).
5. **Encode** (`world_sdf_encode`), and the bricks gathered in the cells' order.

The bake runs on fewer threads than processors, at the lowest priority, at a pace the frames set in play
([The bake's schedule](#the-bakes-schedule)), and stops as soon as asked: at quit, and at a map unload if the map
can't be re-read.

### The bake's schedule

- **First launch:** in #575's fixed start (intro → "Preparing menu pictures" → main menu). That pass already loads
  every stock map, so copying each map's geometry while it's loaded (`world_sdf_startup_map_loaded`, called from
  `menu_pictures_render.c`'s `_prepare_loading` state) costs no extra loads. The bakes themselves wait until the pass
  ends and the main menu shows (`menu_pictures_preparing`), so neither comes later than without them; they
  then run from the copies at the lowest priority behind the main menu. If the pictures are already cached but a field is
  missing (a new bake version), `world_sdf_startup_wanted` makes the pass run for the fields alone. Only the loaded
  BSP's tags are in memory, so the pass bakes the BSP each map starts in and each picture's, and switches to no
  other (repeated switches once leaked the device's groups, #506). A campaign map's other BSPs are baked in the
  background the first time the game switches to them (proxies alone until then), never in the switch. A map that
  is played while its field still waits behind the main menu's others has its bake put first.
- **Only the field drawn in a game** (`sdf_schedule_said`, `sdf_bake_next_locked`): while a game is played, no other
  field is baked: the start-up pass's others (and a BSP left) wait, their copies kept, for the main menu, where the
  game is idle. A bake of another field that is running when a game begins, or when the field drawn is queued behind
  it, stops where it is (`sdf_yield`: its threads take no more items) and begins again after it, from its copies.
  So a game's frames never share the processors with the fields of maps nobody is playing: on the M1 Max whose
  playtest found this (build 1845), the start-up pass's 24 fields took 13 minutes behind the main menu and
  in the first games. A bake stops only before the static scenery's pages (they rewrite the copies' shapes), which
  take at most a few seconds. With `"world"` off no bake runs, and one running stops: turning it off in the menu
  while the start-up pass's fields were baking once let them run on for 11 minutes for nothing.
- **Threads:** the processors less 3 (`world_sdf_bake_threads`; at least 1, at most 32; `HALO_WORLD_SDF_THREADS`
  overrides, for measuring), and at most 2 for a pass begun while a game is played (`WORLD_SDF_PLAY_THREADS`). The 3 are the game's thread, the renderer's (the command stream's) and the sound's with
  the system's own (its compositor and drivers): 5 on an M2 or a Steam Deck, 29 on a 5900X. Each thread is lowered
  with `setpriority(PRIO_PROCESS, 0, 19)`, which each host applies to the calling thread: Linux and Android as
  nice 19 and `SCHED_IDLE` (only a processor nothing else wants), macOS and iOS as `QOS_CLASS_BACKGROUND` (the
  efficiency cores), Windows as `THREAD_MODE_BACKGROUND_BEGIN` (the processor's, the disk's and memory's lowest).
  The threads take their work with atomic counters, never the lock the game's thread takes, and the game's thread
  only tries that lock (`sdf_results`): no frame waits for a bake's thread. A slice of a vehicle's field
  allocates nothing (its directions' tables are made once a bake): guest memory mapped and unmapped meanwhile
  makes the renderer drop the buffers and textures over those pages.
- **Its pace in play** (`world_sdf_pace_frame`): while a map is played (not the menus, a loading screen or the
  start's pass, where it runs freely), the bake first rests 30 frames to learn the frames' normal time. It then
  runs until the last 8 frames' mean is more than a tenth and 1 ms over that, or one frame twice it and 8 ms more
  (a hitch). It then rests (its threads nap between their items: a vehicle's row, a page, 16 cells) and learns the
  normal again: 1 s, doubled up to 32 s while resting helps, the least again when the frames were as slow while it
  rested (a heavier scene, not the bake), and halved after 600 calm frames. A game that draws no frames for
  0.25 s (minimized, a load, "world" off) lets it run. On a machine with processors to spare it never rests.
- **Any other time:** after a map loads with no field (a custom map, a new bake version), `world_sdf_map_loaded`
  only looks in the cache. On a miss it copies the geometry (a few ms) and starts the bake on a low-priority
  background thread, after the load is done. **Never during a load:** `scenario_load` does nothing more than a cache
  lookup (test: `test_the_bake_is_never_in_a_load`).
- **While a field isn't ready,** `"world"` quietly runs with **proxies only** (the block's fade is 0), and only
  those that stay when the field is drawn: the static scenery placements the field will hold
  (`world_sdf_scenery_awaited`) and the boxes of the vehicles it will hold occlusion fields of
  (`world_sdf_object_awaited`; their wheels' spheres stay) are left out until it is drawn, then fade in with it. So
  a frame waiting for a field never costs more than one with it. Shading them meanwhile as proxies made b30's
  frames on an M2 six times as long (85 ms against 14; the vehicles' boxes most of it), whether a bake ran or not:
  that, not the bake's threads, was #651's stutter. When the bake finishes, the field uploads a few slices a frame (at most 512 KB a frame) and then **fades in over 1 s**
  (`world_sdf_fade`), with no hitch and no pop. We chose proxies-only over falling back to GTAO: GTAO's pre-pass
  would cost more than the whole new feature on the Deck, and switching would pop.
- **Measured and stated:** a field found in the cache logs `world sdf: <map> bsp <n>: its field cached (baked in
  <ms> ms on <t> threads)`; a bake that stopped for the field drawn, `its bake stopped for the field drawn`. An object's field taken from those kept by their geometry logs `<name>'s occlusion field: <n>
  triangles, <w>x<h>x<d> voxels, the same geometry's baked before (world_sdf/objects)`, and the bake's line counts them
  (`<n> objects' occlusion fields (<n> of them baked before, ...)`). Each bake logs `world sdf: <map> bsp <n>: ... within <bounds>, <w>x<h>x<d> cells, <n>
  bricks, voxel <v>; static scenery <n> placements, <n> shapes, <n> of <n> lightmap pages in <n> layers of <w>x<h>
  (<ms> ms); in <ms> ms on <t> threads, <bytes> in memory, <bytes> cached`, and the header records the times and
  threads. Once the start-up pass's last bake is done it logs `world sdf: the first start's <n> of <n> fields baked
  in <s> s behind the main menu: the pass and its fields <s> s` (the pass's own total from
  `menu_pictures_pass_seconds`). `HALO_WORLD_SDF_FRAMES=1` logs every 600 frames their mean and worst while a
  bake runs (and in how many it rested) and while none does; `HALO_WORLD_SDF_NO_BAKE=1` bakes nothing, as if a field
  were on its way for good (the frames waiting for one, without a bake's threads). Workstream B's PR states the bake time and
  cache size for every stock map on the 5900X, the Steam Deck and one phone. Targets: under 2 s per BSP on 8
  desktop threads, under 10 s in the background on a phone, and a cache under 3 MB per BSP after deflate.

### Cache

The cache follows the menu pictures' conventions (#506/#575): the data folder's `world_sdf/`, made on this machine,
never shipped, and never trusted without checks.

- **Name:** `<data>/world_sdf/<map name>_<bsp index>.wsdf`.
- **Key:** the map's cache file header checksum (`menu_picture_map_checksum`'s source) plus `WORLD_SDF_VERSION`. A
  different map under the same name, or a new version, is baked again. A good file is reused forever.
- **Format** (little-endian, written field by field, not as a struct):
  - an 80-byte `struct world_sdf_header`: magic `WSDF`, version (6: a box without collision geometry of what it
    draws, in the pages; 5: the scenery's occlusion fields), map checksum,
    bsp index, the cells along x/y/z, origin, voxel, range, deflated size, bake ms, bake threads, brick count, the
    static scenery's part's bytes and its pages' bake ms, the vehicles' part's bytes and their bake ms;
  - then the cells (4 bytes each, x fastest, then y, then z), the bricks (729 bytes each), the static scenery's
    part ([Static scenery](#static-scenery)) and the objects' part ([The objects' occlusion fields](#the-objects-occlusion-fields)),
    deflated as one zlib stream (the game's own zlib, `source/memory/zlib`);
  - then a CRC-32 of the header and the deflated bytes (the menu pictures' CRC).
- **Writing:** to `<name>.tmp`, then renamed into place, so a crash leaves no half file.
- **Reading:** a file with the wrong magic, version, checksum, size or CRC, a cell of no kind, a brick's place
  past the bricks, a scenery part whose counts, sizes or pages' layers don't hold, or a vehicles' part whose counts,
  sizes, numbers or halves don't hold is deleted and baked again.
- **Bumping `WORLD_SDF_VERSION`:** whenever the bake's arithmetic, the source geometry or the layout changes.

## Static scenery

The scenery and devices that never move are baked with the field, once per BSP, and leave the per-pixel proxy
loop to the dynamic objects (units, vehicles, items, moving devices).

**What is static.** A placement of the scenery, machines, controls or light fixtures blocks that:
- no script can name (`name_index` none);
- is made at the map's start on every difficulty (no placement flags);
- is in this BSP, by the game's own test (`object_types.c`: its position or its bounding centre in a leaf);
- can't move or be destroyed: its definition has no animations (which is how a device moves) and no body
  vitality.

Anything else stays a dynamic proxy, as before. At run time an object is baked when it's scenery or a device
on no parent, standing exactly where a baked placement of its definition stands (`world_sdf_scenery_baked`).

**The level: lightmap-space pages.** The level never samples the field (it must not darken itself), and the
field's cone reaches 2.4 wu, where scenery boxes reach 19 to 98. So the static shapes' occlusion of the level is
baked in the BSP's own lightmap space:
- One page of bytes per lightmap page that a static shape's reach touches. Each texel is the exact product the
  proxy loop computes, Π (1 − occlusion × fade) over the static shapes (`world_ao_scenery_page`), at the texel
  centre's place and interpolated normal on the surfaces drawn with that lightmap. A placement whose definition has
  an occlusion field ([below](#the-objects-occlusion-fields)) is that field on its node 0 as placed, not its box:
  the objects' fields are baked first, and the pages' reference reads their atlas (`sdf_scenery_fields`).
- The pages' side is the largest of 512, 256... whose layers fit 4 MB. Texels the surfaces don't cover take
  their covered neighbours' mean, two steps out, for the filter at chart edges.
- A page none of whose texels is shaded shares one layer of none.
- At draw time the lightmap pass reads its page at the lightmap's own coordinates, exactly as stage 2 samples
  them (`xT2.xy / w` by the stage's scale). It multiplies the dynamic proxies' product by the page, then the
  floor and the level's strength apply as before.
- **Fallback (never black):** a lightmap with no bitmap, a material whose lightmap vertices aren't its vertices,
  or a page where more than 1% of the covered texels are covered twice at different places (two surfaces sharing
  them) has no layer. Its draws walk every shape of the window, the static scenery's too, as before the bake.
  Per draw, the front end gives the layer in the draw's reference (`world_ao_lightmap_begin`, hooked in
  `rasterizer_environment_lightmap_begin`), −1 for none.

**The objects: the field.** Each static placement's collision geometry is folded into the field. Its triangles
(each node's first BSP, at rest) join the structure's, and its solids join the sign's point test. Objects then see
every static placement, not just the 64 nearest shapes. A static placement's own draws take the field from the
pixel's own depth inside it (each tap's distance less `min(d(p), 0)`), relative to no reference. That keeps a
rock's ground contact while its surface, which sits on its collision, isn't darkened by itself. Their reference
is sent as −1 (`WORLD_AO_REFERENCE_NO_FIELD`). A placement whose definition has no collision geometry (a shrub, a
fern: `world_proxies_source_solid`) isn't in the field, so this doesn't apply: its draws take their reference as a
moving object's do. Taken relative to none, a shrub by a cliff was darkened twice, by the field and by the lightmap
its ambient light is sampled from, which already holds the cliff (#781).

**The window.** The dynamic objects' shapes come first, nearest first; the static scenery's that the drawn field
holds come after them (`world_proxies_window`'s dynamic count, `parameters[1].y`). A draw walks only the shapes
below its limit: a model, and a lightmap draw with a layer, the dynamic ones; a lightmap draw without a layer,
every one. The limit cuts each word of the cell's mask before the subgroup's OR.

**Its part of the file** (after the bricks, in the same deflated stream; `struct world_sdf_scenery`):
- page count, layer count, width, height and placement count, 4 bytes each;
- each page's layer (2 bytes, signed);
- each placement's definition and position (16 bytes);
- the layers' texels.

A part whose counts, sizes or layers don't hold is a damaged file (deleted, baked again). The GPU holds the
pages as a 2D array in the coverage's format, read as (1, 1, 1, the byte): Vulkan set 3 binding 4
(`XGPU_SPIRV_BINDING_WORLD_AO_SCENERY`), Metal texture 7. The command stream's `WORLD_AO_FIELD` carries them as
part 2, before the table, a layer at a time. A 1x1 page of none stands in until they've all come, and the game
names a layer to the draws only once the whole field is on the GPU.

**Measuring:** `HALO_WORLD_AO_COUNTS=1` logs, every 600 windows, a window's shapes, how many are dynamic (by kind),
and its lightmap pages drawn with a layer and without.

## The objects' occlusion fields

A vehicle's shape doesn't change, only where it is. So its occlusion of what's about it is baked once, in its own
space, and read with one filtered fetch where its exact boxes were found shape by shape. (Before this, at b30's start
two Pelicans' eight boxes were about 1.0 ms of the Radeon's 1.31.)

The scenery's too (#666). A piece of scenery stood for by one box about its collision geometry, or a sphere of its
bound without any, is solid where a tree is mostly air, and a point inside a box or sphere is wholly occluded. A
tree's box reaches a little under the ground, so every texel of the ground inside it was occluded whole: a dark
square, with hard edges, about most of a30's trees (they sway, so they're dynamic objects: their boxes were walked by
every pixel), and in the static scenery's pages about b30's (the pages hold the same product). A shrub's sphere made a
disc. Its field is baked from what is drawn: its model's triangles, each blocking the share of a direction that
crosses it which its shader draws. That is all of it for an opaque surface; for an alpha-tested one (a leaf's card)
the share of its base map's texels that pass the alpha test (alpha over 0x7F, the models' `D3DRS_ALPHAREF`), so a
canopy is as see-through as it looks, and the ground under a tree is darkened softly, most where the most cards and
the trunk are overhead.

**What a field holds.** At each voxel of a grid about the object, the share of the cosine-weighted hemisphere its
geometry blocks, for every normal n, as `sat(t + n · v)`: four numbers, RGBA16F. That is the occlusion
itself, not a distance field: a cone of taps along the normal over a distance field misses what's beside the
normal, and measured against the exact boxes a single cone was off by 17/255 on average (a tenth of the points by
40), and five cones of five taps by 7 to 12.

**The bake** (`world_sdf_bake.c`'s `world_sdf_object_*`, pure; each voxel its own, so the same on any threads):
- **Which** (`sdf_object_definitions`): each vehicle definition of the scenario's vehicle palette with collision
  geometry; then each definition of the scenery, machines, controls and light fixtures palettes placed in this BSP
  whose proxies are one box or sphere and that can't move (scenery, or a device with no animations: a door's field at
  rest would be wrong once it opens), the most placed first. A tree that sways moves its upper nodes; its field rides
  node 0, as its box did.
- **Geometry:** a vehicle's, each node's first BSP's triangles at rest, in node 0's space (the field rides on node 0
  as drawn). A wheel's node (named "tire" or "wheel") is left out: wheels turn and spring, so they keep their
  spheres. A piece of scenery's, the triangles it's drawn with (its regions' first permutations, at its most detailed
  level within 2,048 triangles, else its least; read from the vertex and index buffers it's drawn from, as
  `object_mesh.c` reads them), each with its shader's opacity: none for a transparent shader (glass, effects), 1 for
  an opaque one, and an alpha-tested model shader's base map's coverage (`xgpu_texture_alpha_coverage`, at its first
  level no larger than 512). Its collision nodes' solids, if any, are its point test.
- **Grid** (`world_sdf_object_layout`): the triangles' bounds grown by half the longest side (1.5 to 4 wu), at
  0.125 wu, the voxel grown ×1.125 a step until it's at most 96 a side and 65,536 in all (a piece of scenery's 16,384:
  its occlusion is about it on the ground, softer than a vehicle's about its wheels). A Warthog is 41×33×33 at 0.125, a
  Pelican 46×40×29 at 0.41, a30's pine 20×20×27 at 0.46.
- **Each voxel:** 256 fixed directions (a Fibonacci sphere's), each let through by the triangles it crosses, each
  passing on 1 − its opacity of what's left (an opaque one: none). Rays are bucketed on a cube's faces, and a triangle
  tests only the buckets its cone meets: every bucket's cone at once, then each bucket's rays as one run, without
  branches (the rays stored by axis in the buckets' order), so that both are tested a few at a time; the same
  numbers as one by one, bit for bit, in half the time (a 228-triangle field 2.7 s to 1.4 s on one M1 Max
  performance core). The occlusion at 96 normals is their directions' cosines' sum, each by its
  share blocked. t and v are its clamped least squares fit, weighted 0.02 plus each normal's
  occlusion: a surface under a vehicle faces it, and its contact darkening is what shows.
- **Solid:** a voxel inside, by the nodes' own BSP point tests, takes its outside neighbours' mean, step by step.
  The inside's darkness is then never filtered onto a surface just outside facing away.
- **Past the grid:** its edge's value by `(L / (L + e))²`, with L the edge point's distance from the grid's middle
  and e the point's past the edge (the 1/r² of a far occluder). Its reach: where that bound falls under 1/510.

**Cache and schedule:** in each BSP's field file, keyed on the map's checksum and the bake version, one entry per
object tag (its definition, grid, voxel, reach, bounds and texels). It's baked with the field, in the first start's
pass and on a first load, written whole, checked on read, and redone if missing, stale or damaged. Each one baked is
kept apart too, by its geometry (`sdf_object_cache_load`): `<data>/world_sdf/objects/<hash>.wsdo`, named by a 64-bit
FNV-1a hash of `WORLD_SDF_VERSION`, its grid, bounds, triangles, opacities and solids, and holding the magic `HSFO`,
the version, the hash, the part's size, the objects' part of the one object and a CRC-32. Another map's bake takes
it from there instead of baking it again (its definition its own map's); one that fails a check is baked again. A
Warthog, a flag base or a storage crate is in many maps: the 217 objects' fields the playtest that found this baked
(build 1845, an M1 Max) were 119 objects', the flag base's 11 times. A piece of
scenery's alpha-tested textures are loaded for it as a draw loads them, when its geometry is copied (a few, once a
BSP). On the GPU the fields share one 3D atlas within 8 MB (`world_sdf_objects_place`): in columns along z, rows of
them along x and the rows along y, of the depth and width that place the most, in order, in the least bytes (one
row of columns as deep as the atlas took 1.8 times the fields' own bytes; b30's 7 vehicles and 20 pieces of scenery
didn't fit). One past its room keeps its shapes. Its texels
come as part 3 of `WORLD_AO_FIELD`, before the table, and fields are named only once the whole field is on the GPU.

**The window** (`world_proxies_window`): an object whose field is drawn takes it in place of the shapes it stands in
for (`world_proxy_set_fielded`: a vehicle's boxes, a piece of scenery's one box or sphere), as three shapes together
(`_world_proxy_field`, then two of no kind; never split by the window's end). A vehicle's wheels' spheres stay. A
baked static placement takes its field too (a page without a layer walks it). The three hold:
- the world to its voxels (node 0's frame as drawn, inverted);
- its last voxel, its place in the atlas and the atlas's size;
- its geometry's box's half extents, about the grid's middle (the layout centres the grid over it).

Without a field (none baked, not yet uploaded, past the atlas's room), its shapes shade, as before.

**Its shading** (`world_ao_field_proxy_occlusion`, both emitters line for line):
- p and n go to its voxels: one fetch at p's clamped place, `sat(s · (t + n · v))`.
- It's faded over the last quarter of its reach.
- It's also faded by its geometry's box's bound (`world_proxy_box_fade`'s U, in its voxels). Its geometry is
  inside that box, so U bounds its occlusion too. Like a box, it's never fetched where U is under 1/510; on the
  ground beside a Warthog that's a few wu, not its 20 wu reach.
- Its reach is the field's or the box's, where less.
- The grid's cells take its box grown as a box's.

## The distance-field scene (`world_cascades.h`)

The ground ray tracing needs (#602), and what World AO's rays step through: a global signed distance field about the
camera, in four nested cascades (a clipmap), as Lumen's global distance field and AMD's Brixelizer have it.

| Cascade | Voxel | Side (64 voxels) | Range each way |
| --- | --- | --- | --- |
| 0 | 0.25 wu | 16 wu | 1 wu |
| 1 | 0.5 wu | 32 wu | 2 wu |
| 2 | 1 wu | 64 wu | 4 wu |
| 3 | 2 wu | 128 wu | 8 wu |

- **Two distances a voxel** (RG8 snorm, each over its cascade's range, `WORLD_CASCADE_RANGE_VOXELS` = 4 voxels):
  - the structure, the level's own field sampled (`world_sdf_sample`, which holds the static scenery too);
  - the objects, the shapes of every drawn object within reach (`world_proxies_scene`: bipeds' capsules, vehicles'
    wheels, items' boxes; not the static scenery, which is the structure's, and not the shapes an occlusion field
    stands in for, since a field holds occlusion, not distance).
  A ray's mask chooses which it may hit, so the level's pixels trace the objects alone (the lightmaps hold the level's
  own occlusion) and a model's both.
- **Still things go in once.** The structure is sampled where a cascade is filled and in the slabs the camera's
  movement brings in. An object is stamped again only when one of its shapes changed by more than 0.005 wu (the motion
  decides, not its kind): the region its old and new shapes reach is found again, each shape stamping the voxels
  within range of it. A shape thinner than a quarter of a cascade's voxel isn't in it (a biped's limbs are in the
  finest two).
- **Toroidal places.** A cascade moves with the camera in steps of 4 voxels; a voxel's place is its world index
  modulo 64, so a step finds and uploads only the slab it brings in. Found whole (the start, a new field, a jump past
  its side), a cascade is filled 16 slices a frame, the coarsest first, and named ready when done; until then the
  coarser serve.
- **On the GPU:** one 3D texture, 65 x 65 x 260 (each cascade with a guard texel past its last along each axis, a copy
  of its first, so the linear filter is right across the toroidal seam), **2.2 MB** (the CPU keeps the same). The
  game sends the boxes written (`WORLD_AO_CASCADES` records). A cascade's block vec4 is its first voxel's centre and
  its voxel (0: not ready).
- **Cost** (the game's thread, `HALO_WORLD_AO_COUNTS=1`; Ryzen AI Max+ 395): a30's start with the bot walking 0.10 ms a
  frame (4,400 voxels, 30 KB uploaded), standing 0.03 ms; a10's start 0.03 ms. A cascade filled whole is about 12 ms
  of work, spread over its 4 frames.

### The ray step

`world_trace_ray(origin, direction, t_max, mask)` (`world_cascades.c`; `world_trace` in both emitters, checked against
it by `tools/test_world_trace.py`): sphere tracing, at most 24 steps. Each step takes the finest ready cascade whose
window holds the point, reads its two distances (one filtered fetch), and takes those the mask allows. A hit is a
distance under a quarter voxel; a step is at least half a voxel. A distance counts only once the ray is outside it: a
ray starting inside solid (a model's surface within its own capsules) leaves it without hitting it, as front faces
alone would. It returns whether it hit, how far, and what (structure or objects).

### Hardware ray queries (#602)

`display.ambient_occlusion = "ray_traced"`: `"world"`, with the dynamic objects' rays (the pass's `world_ao_traced`)
cast by the GPU's ray queries through the objects' own collision meshes, in place of the step through the
distance-field scene. Everything else is `"world"`'s: the level's field and its reference on the models, the static
scenery's pages and shapes, the pre-pass, the quarter-resolution pass, the upsampling and the history. Vulkan only
(the Linux builds, Windows, Android), where the device has `VK_KHR_ray_query`, `VK_KHR_acceleration_structure` and
buffer device addresses, and is not a processor's (llvmpipe); elsewhere, Metal included, it draws as `"world"` and
says why once in the log. Off by default.

- **The scene** (`world_rt.h`, `port/linux/game/world_rt.c`; read through `world_rt_game.c` and the proxies'
  instances). The meshes, each made once a map as first needed and let go of at its end:
  - the loaded structure BSP's collision geometry (as `world_scene.c` fans it: 41,354 triangles at a10, 27,914 at
    a30, 5,468 at Blood Gulch), made again for another BSP;
  - each definition's nodes' collision geometry, a mesh a node (the first permutation's BSP of the collision node of
    the node's index, as the collision tests take them), turned to face out by the sign of the volume it holds;
  - an item with none (a grenade) a box about its bounding sphere, `WORLD_RT_BOX_SHARE` (0.6) of its radius, up to
    1 wu across. Anything else with none has no mesh: an emitter's, a wire's or the sky's ring's sphere is not its
    shape.

  The instances, each frame: the structure, and every dynamic object's meshes on its nodes as drawn (the
  `render_interpolation.c` pose the proxies take). The static scenery the field and its pages hold, held weapons and
  the first-person weapon are left out, as the proxies leave them out. a10's start: 154 meshes (96,388 triangles),
  931 instances a frame; Blood Gulch: 45 meshes, 89 instances.

  Collision meshes rather than the proxies' shapes: they are the objects' own shape, rigid on each node (so each is
  a bottom-level structure built once, and only the instances move: no refit or skinning), what the proxies are
  fitted to already, and an order of magnitude fewer triangles than the render models. The shapes are what a
  distance field can hold, not what a ray tracer needs.
- **The command stream** (version 21): `WORLD_AO_MESH` (a mesh's triangles; id 0 every mesh let go of),
  `WORLD_AO_INSTANCES` (the frame's: a 3x4 transform, the mesh, the owner and the kind), the backend's
  `ray_queries` in `INITIALIZE`'s answer, each draw's owner among the instances (`world_ao[2]` of the uniforms, its
  datum index plus one), and the pass's `history[3]` 2. The pre-pass's twins write the owner into the inputs' last
  word.
- **The backend** (`port/linux/vulkan/xgpu_vulkan_rt.c`): each mesh a bottom-level structure (fast to trace), the
  frame's new ones built in one call into 16 MB buffers of their own; the instances a top-level structure built
  again each frame (fast to build) before the first pass that reads it, three in turn. The builds go in the early
  commands, before the submission's draws, so no pass is ended for them. The occlusion set names the latest scene
  (or an empty one) at binding 8. The pass's pipeline with ray queries is made with the map's; until it and the
  scene are, the pass steps through the distance-field scene as `"world"`'s does.
- **The rays** (`world_ao_traced_rays`, `xgpu_spirv.c`): the same 4 cosine-weighted rays a pixel from the same point,
  0.1 to 2 wu, the same history blended in. Each takes the nearest front face it meets (`gl_RayFlagsCullBackFacing`):
  an object's (its instance not opaque, so that each candidate but the pixel's own owner is committed) counts
  1 − (t / 2)², the structure's (opaque, either way facing) stops the ray and counts nothing, since the lightmaps and
  the field hold the level's own occlusion. So an object behind a wall or floor no longer shades through it, as the
  distance-field scene's objects channel did. Every pixel casts them: the scene holds every dynamic object drawn, not
  the cascades' reach. The dynamic objects' occlusion fields (the vehicles') are not walked: their meshes are traced.
  The facing was checked on the GPU: a box wound counterclockwise seen from outside, placed about the camera, shades
  nothing inside it unflipped and the floor inside it flipped (`HALO_VULKAN_RT_FLIP=1`).
- **Measured** (RTX 4090, release, a10's start held still: fixed-step frames, offscreen, unpaced, windowed; the
  medians of 600-frame windows of `HALO_GPU_STATS`' GPU time a frame, and of `HALO_VULKAN_DRAW_TIMES=10`'s pass,
  `aofe`, in runs of their own):

  | | 1920x1080 frame / pass | 3840x2160 frame / pass |
  | --- | --- | --- |
  | `"off"` | 0.368 ms / none | 0.640 ms / none |
  | `"world"` | 0.517 ms / 0.066 ms | 1.297 ms / 0.245 ms |
  | `"ray_traced"` | 0.708 ms / 0.115 ms | 1.547 ms / 0.369 ms |

  The pass is 0.05 ms (1080p) and 0.12 ms (4K) dearer than the distance field's step; the rest of the frame's
  difference (0.14 ms at 1080p, 0.13 at 4K) is the top-level structure's build of 931 instances each frame and its
  barrier. The Spark's GB10 builds and traces it (the pass with ray queries, no error); its timings
  are not taken yet (the machine went off the network mid-run).
- **Compared** (a10's and b30's starts, the same frames; the occlusion as the shaded frame over `"off"`'s): on the
  level, contact shade where an object's real shape meets it (a door in its frame, a body on the grass) and none
  through floors; on the models, the same field and reference, so alike. The mean over a10's frame is 0.947 with
  `"world"`, 0.945 with `"ray_traced"`; `"world"`'s soft halo on the wall behind a10's first crewman and consoles is
  mostly gone (4 rays from a wall a metre from a crewman see little of him). At b30's start `"world"` darkens the
  whole beach by about a fifth (a mean of 0.79 against 0.94; most likely the vehicles' occlusion fields, whose reach is 20 to
  90 wu, are walked over it); `"ray_traced"`'s 2 wu rays do not: only what is near shades.
- **Validated**: Vulkan's validation layers on the 4090 (a10, a30, Blood Gulch, the map changes between them): no
  messages.
- **Left**: the models' structure term by rays (needs the reference by rays too: a small pass at each object's
  lighting point), which would drop the field's bake for `"ray_traced"`; a refit of the top-level structure where
  only transforms change; the vehicles' long-reach occlusion, which the fields have and 2 wu rays have not; Metal's
  `intersector` (M3, A17 Pro); the Deck (RADV), the Frame and phones, each measured first (#432's rule). llvmpipe's
  (Mesa 25 on the Spark) crashed in its own code as the scene was built again each frame, not root-caused; it is left
  without ray queries.

## Proxies (`world_proxies.h`)

Each object definition gets up to 16 shapes, made the first time it's drawn and kept until the map unloads:

- **Bipeds:** a shape for each node with collision geometry (the collision model's node of the same index, its
  first permutation's BSP vertices): a capsule along the bone to its child node (or, with no child, along the
  vertices' longest spread), its radius 0.9 × the vertices' mean distance from that axis, its ends the vertices'
  extent along it less the radius; a sphere where they're shorter than they're wide (the head). The 10 largest are
  kept: upper and lower arms, thighs, calves, the torso and the head on a Spartan. With no collision geometry at
  all, a capsule from each node to its child, radius 0.12 × the bounding radius. Each shape rides on its node.
- **Vehicles:** a box for each node's collision geometry (its bounds in the node's space), the smallest merged into
  its nearest ancestor's box (or the largest) until at most 4 are left, plus a sphere for each node named as a wheel
  ("tire", "wheel"): its own geometry's bounds, else its height above the body's lowest point (0.15 to 1 wu). Where
  its occlusion field is drawn, the field stands in for the boxes ([The objects' occlusion fields](#the-objects-occlusion-fields)).
- **Scenery, machines, controls, light fixtures, items (weapons and equipment on the ground, garbage):** one box,
  every node's collision geometry in node 0's space (where a piece of scenery's occlusion field is drawn, the field
  instead). With no collision geometry, the box of what its model draws opaque (its most detailed geometry, its
  regions' first permutations, `object_mesh_get_model`), and no shape when it draws nothing opaque (glass, a glowing
  floor marker, an effect). It was a sphere of the bounding radius at its bounding centre, a bound for culling and
  pickups: a frag grenade's 0.2 wu about its origin, twice the grenade and half under the floor, and Blood Gulch's and
  Hang 'Em High's floor arrows' 0.3 wu, which draw nothing opaque. The floor inside the sphere was wholly occluded: a
  hard-edged dark patch several times a grenade's size (a disc walked, a polygon from the distance-field scene's
  voxels traced), and a round blotch about each arrow in the static scenery's pages (#773).
- **Anything else** with a model: its bounding sphere.
- **None:** projectiles, sound scenery, a weapon or equipment held (its holder covers it, and it would shade the
  hands holding it), invisible objects, and any shape under 0.05 wu across.

**Each frame and window** (`world_proxies_window`): the shapes are placed by their node matrices as drawn, i.e. the
`render_interpolation.c` pose that `render_objects` draws with, so proxies never lag their objects. They're culled
by their reach sphere against the camera's frustum (its four sides), ordered by the distance from the camera to the
reach's centre, and the first 64 are kept. Each proxy records its owner, so an object's own proxies don't shade it
(models have no baked self-occlusion to double, but the crude shapes would look wrong on their own surface). The
**owner slot** (`[1].w`) is the order of the owner's first shape in the window: 0 for the nearest shape's object, then
1, 2... `world_proxies_window`'s `owners[i]` is the object's id (datum handle) for each shape, so `world_ao.c` gives
a draw its object's slot by finding the object there (-1 when it has no shape in the window, which matches none).

The game is read through `world_proxies_source.h` alone (`world_proxies_game.c`): the definitions' tags, the
frame's objects (A's `world_scene_instances`), their
node matrices as drawn and the window's frustum. `debug.world_proxies_view` draws the window's shapes as wireframes
(spheres yellow, capsules green, boxes cyan).

**GPU layout:** three vec4s per proxy (`struct world_proxy`, `xgpu_stream.h`):
- `[0]`: a.xyz, kind (1 sphere, 2 capsule, 3 box);
- `[1]`: b.xyz (a box's half axis x), owner slot;
- `[2]`: radius (a box's half axis y in xyz and half extent z in w).

## Shading

### Where it applies

#574's `XGPU_AMBIENT_OCCLUSION_LIGHT_*` variants gain a source: GTAO's texel, or the world term computed in the
shader. They're the same three places:

| Draw | Ambient multiplied by |
| --- | --- |
| Model lit per pixel (`_LIGHT_MODEL`) | SDF(p, n) relative to the reference × proxies of other objects |
| Model lit per vertex (`_LIGHT_VERTEX`, split vertex shader) | the same, per pixel, on the split ambient |
| Structure lightmap pass (`_LIGHT_LIGHTMAP`) | proxies alone (never the SDF) |

Self-illuminated draws and the first-person weapon stay at 1. #574 kept them with stencil marks; here the front end
simply doesn't give those pixel keys the world variant.

### SDF occlusion (objects)

At a point p with normal n (world space), d(x) is the field's trilinear sample × range:

```
h_i   = h_1 · 2^(i-1),  i = 1..5,   h_1 = max(0.15 wu, voxel)          (0.15 0.3 0.6 1.2 2.4)
occ   = Σ_i 2^-i · clamp((h_i − d(p + n·h_i)) / h_i, 0, 1)             (cone along n; Σ 2^-i < 1)
A_sdf = 1 − clamp(k · occ, 0, 1),   k = 1.6 (tuned by D against captures)
```

The 5 taps are linear 3D texture reads. On mobile/Deck tiers, D may use 4 taps (`h_1 = 0.3`).

**The reference: no double darkening.** An object's ambient light is the lightmap sampled at its lighting point, and
that radiosity already holds the enclosure there: a corridor's floor is dim because the corridor encloses it. So the
SDF term is taken relative to the occlusion the sample already holds:

```
ref   = A_sdf(lighting_point + up·h_1, up)                 (CPU, per object, world_sdf_sample; 1 if no field)
A_obj = clamp(A_sdf(p, n) / max(ref, 0.25), 0, 1)
A_obj = mix(A_sdf(p, n), A_obj, reference_weight)          (block parameters[1][0]; default 1)
```

A Spartan in a corridor isn't darker overall than the floor's light says. But its side facing a wall, its underside
and a Warthog's belly over the ground are darker, which is what the eye misses today.

**Foliage: a two-sided shader's cards** (#781). A plant's leaves are cards drawn with a two-sided model shader
(`_shader_model_two_sided_bit`: its back faces drawn again, the normal reversed). A card is thin, drawn alike from
both sides and lit through either face, and its normal is no surface's: the cone along a horizontal card's normal
finds the ground under it, and along a card facing a cliff the cliff, though the card is open to the sky. A shrub on
Blood Gulch went near black, card by card. So a card's field term is that of its most open direction of three: the
sky's (up, as the reference finds it) and its two faces' (`world_ao_cards_occlusion`: the first of them where alike),
and its shapes and rays are found about that direction. The game says which draws are cards
(`world_ao_cards`, hooked in `_rasterizer_model_draw`; the draw's uniforms' `world_ao[3]`), the pre-pass's twin puts
`XGPU_WORLD_AO_INPUT_CARDS` in its inputs, and the pass (or an inline draw) finds the other two cones for those pixels
alone. A canopy is then darkened where it is enclosed (under an overhang and against a wall at once, or by a vehicle
beside it), not where a card happens to face. Measured (Vulkan, the GB10; mean luminance of the changed pixels, AO
off / before / after): the shrub of #781's report 0.258 / 0.120 / 0.249, a pine by Sidewinder's base 0.096 / 0.038 /
0.073, b30's trees from its start 0.396 / 0.291 / 0.297; nothing but the cards changed in those frames (and a30's
lifepod's ceiling grilles, also a two-sided shader's).

### Proxies' occlusion

Each is the cosine-weighted share of the hemisphere the shape covers, computed analytically. The reference is
`world_proxy_occlusion` in `world_proxies.c` (`sphere_occlusion`, `capsule_occlusion`, `box_occlusion`); D's shaders
follow it line for line. p is the point, n its unit normal, all in world units.

- **Sphere** S(c, r) (centre c, radius r), exact, with the horizon's clipping (Lagarde and de Rousiers' form, as
  Quilez gives it):

  ```
  d = c − p;  if |d|² ≤ r²: 1
  l = |d|,  nl = n·d / l,  h2 = l² / r²,  k2 = 1 − h2·nl²
  k2 > 0.001 (it crosses the tangent plane):
      x   = clamp(−nl·sqrt((h2 − 1) / (1 − nl²)), −1, 1)
      occ = ((nl·acos(x) − sqrt(k2·(h2 − 1))) / h2 + atan(sqrt(k2 / (h2 − 1)))) / π,  clamped 0..1
  else: occ = max(nl, 0) / h2
  ```

  Within 0.01 of the numerical integral everywhere.

- **Capsule** (segment a–b, radius r): the tube as a line 2r wide, the part of it above the tangent plane integrated
  in closed form, plus the two caps' spheres weighted by how much of each shows past the tube, and never less than
  the sphere at the segment's closest point:

  ```
  L = |b − a|;  L < 1e-4: S(a, r)
  u = (b − a) / L,  q = a − p,  s0 = −q·u,  w = q + s0·u,  h = |w|
  nw = n·w,  nu = n·u,  t0 = −s0,  t1 = L − s0
  |nu| < 1e-6: (nw ≤ 0: t1 = t0)        else: th = −nw / nu;  nu > 0: t0 = max(t0, th)  else: t1 = min(t1, th)
  F(t) = nw·(t / (2h²(h² + t²)) + atan(t / h) / (2h³)) − nu / (2(h² + t²))
  line = (t1 > t0 and h > 1e-4) ? 2r·h / π · (F(t1) − F(t0)) : 0
  wa = ½(1 − u·normalize(p − a)),  wb = ½(1 + u·normalize(p − b))
  occ = line + wa·S(a, r) + wb·S(b, r)
  occ = clamp(max(occ, S(a + clamp(s0, 0, L)·u, r)), 0, 1)
  ```

  The line term is (2r h / π) ∫ n·d / |d|⁴ along the segment's part above the horizon; for a long tube facing p it's
  exactly r / h. Within 0.05 of the integral at a radius or more from the surface (0.04 measured); nearer, within
  0.15 (a line 2r wide is a tube only from afar), mean error about 0.015. (The draft's "sphere at the closest point
  times (1 + L·|sin θ| / 2l)" was off by up to 0.6.)

- **Box** (centre c, half axis vectors X and Y, half extent ez along Z = normalize(X) × normalize(Y)), exact: in box
  space (p and n projected on the unit axes), 1 inside; else each face with p outside its plane (at most 3), its 4
  corners taken relative to p and clipped by the plane n·v ≥ 0 (Sutherland–Hodgman taken edge by edge: each edge's
  part above the plane, and the chord from where the face leaves it to where it enters), and the faces' form factor
  (Lambert):

  ```
  edge = atan2(|vi × vj|, vi·vj) · n·(vi × vj) / |vi × vj|      (j = i + 1; the same for any lengths of vi, vj)
  occ  = clamp(|Σ_faces side · Σ_edges edge| / 2π, 0, 1)
  ```

  Each face is taken the same way round as seen from outside the box (its corners' order times its side), so an
  edge two front faces share is taken once each way and its terms cancel: it is left out (`box_occlusion`), and only
  the outline's edges (at most 6) and the chords are found, about half the edges of the faces taken one by one.
  Within 0.02 of the integral everywhere (the residue is the integral's own sampling).

  **Far** (6 half diagonals or more from its centre, `WORLD_PROXIES_FAR`): each front face as a patch at its centre,
  `occ = Σ_faces 4 e_u e_v · o_a · max(n·r, 0) / (π |r|⁴)` (r from p to the face's centre, o_a p's distance outside
  the face's plane): no edges, no atan2, about a tenth of the cost. Measured against the exact faces at 200,000
  random boxes, points and normals: within 0.15/255 at 6 half diagonals (0.5/255 at 4, 1.5/255 at 3); the harness's
  `far` case holds the step where the form changes under 0.2/255.

- **Reach** (`world_proxy_reach`), measured from the sphere's and box's centre and the capsule's midpoint: past it
  the occlusion is under 1/255 in every direction (a box's under 1/510).
  - sphere: 16r;
  - capsule: L/2 + 16·sqrt(r² + 2rL/π), a sphere of its silhouette's area side on;
  - box: its half diagonal sqrt(|X|² + |Y|² + ez²) + sqrt(510 · 4|A| / π), A = (e_y e_z, e_x e_z, e_x e_y) (below).

  The shader skips a shape after that one distance test. A sphere's and a capsule's term also fades to 0 over the
  last quarter of the reach, `fade = clamp((reach − |p − centre|) / (0.25·reach), 0, 1)`, on the level and on objects
  alike, so culling never shows an edge.

- **A box's bound** (`world_proxy_box_fade`). In box space, with o the point's distance outside each pair of faces
  (o_i = max(|p_i| − e_i, 0)), D = |o| and H the box's height above the point's tangent plane (its support along n
  less n·p), the box's occlusion is never more than

  ```
  U = (4 / π) (e_y e_z o_x + e_x e_z o_y + e_x e_y o_z) · min(D, H) / D⁴
  ```

  (its solid angle is its front faces', each at most its area 4 e_j e_k times o_i / D³, since every point of it is D
  or more away; and the cosine at p of any direction to the box is at most H / D). A box's fade is
  `clamp(510 U − 1, 0, 1)`: computed whole where U ≥ 1/255, not at all (skipped) where U ≤ 1/510. U falls as 1/D³ on a
  floor beside a box (H/D is small there) and past a flat or long box's edges, where 17 half diagonals reached far: on
  the ground about a Warthog the box is computed within about 6 wu, not 30. `tools/harness/tests/world_proxies.c`
  (`bound`) checks U is never under the occlusion at 20,000 random points.

- **The window's grid** (`world_ao_grid`, `world_ao_shading.c`). As the enhanced dynamic lights' grid: 16 × 16 × 8
  cells about the camera, boxes in the world (VR's eyes share it), their size log2(1 + |offset| / 1 wu) times each
  axis's scale, the outermost reaching 128 wu and holding whatever reaches past it. Each cell has a 64-bit mask of the
  proxies that can touch it: a sphere's and a capsule's reach, a box's own bounds grown by where its U falls under
  1/510 (if less). Built on the CPU for each window (64 shapes: microseconds), uploaded with the block (16 KB). A pixel
  finds its cell and runs only the proxies of its mask, lowest first. That leaves out only proxies whose factor is
  exactly 1 there, in the same order, so the product is the brute-force loop's bit for bit
  (`world_ao_proxies_occlusion_grid` against `world_ao_proxies_occlusion`: 0 of 160,000 points differ, in random
  windows of 64 shapes, in `tools/test_world_ao_light.py`).

**Combining:**

```
A_prox = Π_shapes (1 − occ_i · fade_i)                 (shapes of other owners in the pixel's cell; ≤64)
A      = max(mix(1, A_obj, fade) · A_prox, 0.2)   (models: fade the field's)
A      = max(A_prox, 0.2)                          (lightmap pass)
A      = mix(1, A, strength_{objects|level})
ambient' = ambient · A,  then the point lights / dynamic lights are added as #574 does
```

The CPU reference of all of it is `world_ao_occlusion` (`port/linux/game/world_ao_shading.c`), and both emitters'
shaders are checked against it at a made-up scene's points (`tools/test_world_ao_light.py`: the Metal functions'
text run as C++, the SPIR-V modules run by a small interpreter; within 1e-6 of it). The draw's owner slot is its
object's first shape's `[1].w` (`world_proxies_window`'s owners are the objects' datum handles); a draw with no
shapes of its own has −1, and the first-person weapon gets no world variant at all.

The strengths are in the block (`parameters[0]`): **subtle** objects 0.6 / level 0.5, **strong** 1.0 / 0.8. Those
two levels may come back as `"world_subtle"` and `"world_strong"` if D's captures call for it; v1 ships `"world"` as
the strong preset.

## GPU binding layout

| | Vulkan (SPIR-V) | Metal (MSL) |
| --- | --- | --- |
| Field's atlas: 3D, R8 snorm, linear, clamp | set `XGPU_SPIRV_SET_OCCLUSION` (3), binding `XGPU_SPIRV_BINDING_WORLD_SDF` (1), combined image sampler | `[[texture(XGPU_MSL_TEXTURE_WORLD_SDF)]]` (5), `constexpr sampler` |
| Field's table: 3D, RGBA8, read by texel | set 3, binding `XGPU_SPIRV_BINDING_WORLD_SDF_TABLE` (3), combined image sampler, `OpImageFetch` | `[[texture(XGPU_MSL_TEXTURE_WORLD_SDF_TABLE)]]` (6), `read` |
| Static scenery's pages: 2D array, R8 (the coverage's format: read as 1, 1, 1, the byte), linear, clamp | set 3, binding `XGPU_SPIRV_BINDING_WORLD_AO_SCENERY` (4), combined image sampler | `[[texture(XGPU_MSL_TEXTURE_WORLD_AO_SCENERY)]]` (7), `constexpr sampler` |
| Vehicles' occlusion fields' atlas: 3D, RGBA16F (t, v.xyz), linear, clamp; a texel of none stands in | set 3, binding `XGPU_SPIRV_BINDING_WORLD_AO_OBJECTS` (5), combined image sampler | `[[texture(XGPU_MSL_TEXTURE_WORLD_AO_OBJECTS)]]` (20: past the combiner's 8 to 19), `constexpr sampler` |
| Block `struct xgpu_world_ao_block` (std140, 19,552 B) | set 3, binding `XGPU_SPIRV_BINDING_WORLD_AO` (2), uniform buffer | `[[buffer(XGPU_MSL_BUFFER_WORLD_AO)]]` (23) |
| Per draw: owner slot, reference (a lightmap draw's: its page's layer, −1 none; a static placement's own: −1) | the draw uniforms (`xgpu_spirv_uniforms`, 2 floats) | `xgpu_msl_uniforms`, the same |

Set 3 is #574's occlusion set, so a draw still binds at most 4 sets, the Vulkan minimum. With no field yet, the
backend binds 1×1×1 stand-ins: a table cell far outside and an atlas texel of +127.

The block: `field[0]` origin.xyz (the first voxel's centre) and 1/voxel; `field[1]` the cells along x, y, z and the
atlas's layers (range is `WORLD_SDF_RANGE_VOXELS` / `field[0].w`); `parameters[0]` strength on
objects, strength on the level, fade, proxy count; `parameters[1]` reference weight, the dynamic objects' shapes' count (the static scenery's after them), whether the walk of the shapes is a SIMD-group's (Metal's, set by the backend; Vulkan's is the emitter's) and the pixels a side of the blocks the occlusion is found once for (1 or 2: [Found once a pixel](#found-once-a-pixel)); then 64 × 3 vec4
proxies; then the grid: `grid[0]` the camera, `grid[1]` each axis's scale and 1 / cell; then 1,024 uvec4 cells, two
cells' 64-bit masks each (cell i's word w at `cells[i / 2][2 (i % 2) + w]`).

**Command stream (version 16; 21 with "ray_traced"'s records, [Hardware ray queries](#hardware-ray-queries-602)):**
- `XGPU_STREAM_WORLD_AO_WINDOW` carries the block (`active` 0: withdrawn);
- `XGPU_STREAM_WORLD_AO_FIELD` carries a slab of slices of a part, the table (4 bytes a texel), the atlas (1), the
  static scenery's pages (1) or the vehicles' fields' atlas (8: four halves) (`width` 0: release every part). The table comes first; the field is drawn with once both have their last slices. The
  decoder checks the slab's part, bounds and size before any backend reads it.
- `XGPU_STREAM_PIPELINE_STAND_IN` names the pipeline bound's stand-in ([Pipelines](#pipelines-in-the-maps-preparation)).

### Pipelines: in the map's preparation

A map's pipelines are made as it starts (`render_enumerate.c`, or played back from its record), and the load waits
for them. No world window is given then (`world_ao_window_begin` while `render_enumerating`: nothing is seen), so
the front end gives the draws the keys they will have in play from their window's span alone (`d3d8_device.c`'s
`world_ao_key` while `world_ao.preparing`, #672):
- **The span:** `WORLD_AO_SPAN_OPEN` for a window's opaque surfaces, `WORLD_AO_SPAN_FOUND` where the pre-pass finds
  them once a pixel (`world_ao_prepass_begin`: the primary view into the back buffer with its depth buffer), and each
  one's first-person form, whatever the setting. The journal keeps it (`world_span`, form 8) and tells draws apart
  by it (`journal_settings_blind_of`), so a record made with the setting off makes every pipeline with it on.
- **The keys:** with `"world"` on, the world's light; read from the pre-pass in a found window (not with
  `debug.world_ao_inline`). A mirror's window has the world's light alone.
- **The twins:** in a found window the backend is told to make each draw's twin too
  (`XGPU_WORLD_AO_PREPASS_TWINS`), with no limit a frame while preparing.
- **The setting switched on in play:** `world_ao_mode` follows the settings as the front end takes them up between
  frames (`halo_gpu_settings_taken`), so the map is prepared again at the frame the world's draws begin.
- **A stand-in for each world pipeline** (`world_ao_stand_in`) is still told to the backend; Vulkan prepares and
  draws it only where the world's shaders cannot be made.
- **Misses are logged** on both backends: a world pipeline, its shaders or a twin made after the preparation is a
  `pipeline missed map preparation` line. a30's lifepod walk: Vulkan 60 before (none of them logged then), 0 after;
  a10's start 24 to 0; switched on in play 44 to 0. Metal on the walk: 16 to 0.
- **The cost:** with `"world"` on, a30's record makes 659 pipelines, not 553: 400 ms, not 275, with the GPU's
  pipeline cache cold (the Radeon 8060S); with the setting off nothing changes.
- **The settings' one derivation:** the world's light is a fact of the draw (`xgpu_draw_facts.world`, from
  `world_ao_key`), and `xgpu_draw_derive` (`xgpu_manifest.c`) gives it the same places as above. The renderer's
  manifest notes it as it would be with `"world"` on (context `cw`), from the window's span as the game draws it,
  whatever the setting and while a map is prepared; the journal keeps the span (`world_span`, form 6). The Mac's
  and iOS's library is built under `"world"` too (`XGPU_MANIFEST_OCCLUSION_WORLD`), so the world's shaders are in
  it. The manifest also notes a draw's window found once a pixel (context `cf`, the first-person weapon's
  too), so the library has the sampled forms, their twins and the occluders' twins (the weapon's, a
  self-illuminated surface's) that the preparation makes. Before, the occluders' were missing: 5 of a30's were
  compiled as it prepared on the Mac. `debug.world_ao_view`'s aren't in it (a debug view, compiled at its
  first draw).

The backends' `world_ao_window` and `world_ao_field` callbacks are still unset, so the records are decoded and
ignored.

## Settings

- `display.ambient_occlusion`: `"off"` (default) | `"world"` | `"ray_traced"` ([Hardware ray
  queries](#hardware-ray-queries-602)). GTAO's `"subtle"` and `"strong"` (#574) are retired,
  with their pre-pass and passes: their stripes on flat surfaces and at door and screen edges were not worth keeping.
  A config.toml or `HALO_AMBIENT_OCCLUSION` with either is read as `"world"`, and the file is written with it
  (port_config.c's `config_renamed_words`).
- `"world"` works in VR's eye passes and with Metal's multisampling.
- The menu (`tools/port_settings.py`, AMBIENT OCCLUSION) has OFF, WORLD and RAY TRACED.
- Classic's preset writes `"off"`, which bypasses everything.
- `debug.world_ao_view` (D): the surfaces it shades drawn as the term alone, in grey. `debug.world_ao_test` (D): a
  made-up field (a plane about the camera) and shapes in place of B's and C's, to check the shading alone. `debug.world_proxies_view`
  (C): the shapes as wireframes.
- A dedicated server or `debug.null_renderer` never builds the scene, bakes or uploads (`world_ao_mode` returns 0
  without a renderer).

## Performance budgets

These are added GPU ms per frame with `"world"` versus `"off"` (`debug.gpu_stats`), measured at c10's swamp and a
Blood Gulch firefight (16 bipeds, 4 vehicles):

| Device | Resolution | Budget | (GTAO strong, #574) |
| --- | --- | --- | --- |
| RTX 4090 | 1440p / 4K | ≤ 0.10 / ≤ 0.25 | 0.52 / 1.17 |
| Radeon 8060S | 1440p | ≤ 0.35 | 1.31 |
| Steam Deck | 1280×800 | ≤ 0.35 | (too costly to enable) |
| Phone (Adreno 7xx / Apple A15) | native | ≤ 0.5 | n/a |
| VR (both eyes, multiview, 90 Hz) | per headset | ≤ 0.4 for both eyes | not drawn in the eyes |

There's no pre-pass, so the CPU doesn't walk the scene twice either. CPU per frame: scene ≤ 0.2 ms, proxies ≤
0.1 ms, upload ≤ 512 KB a frame and only while a field arrives. Memory: field ≤ 8 MB of GPU memory (its table and
atlas: `WORLD_SDF_MAXIMUM_BYTES`) and one block
(19 KB with the proxies' grid) per window.

### The proxies' cost (workstream E)

World minus off, GPU ms a frame (`debug.gpu_stats`, the mean of the last five 600-frame windows), offscreen Vulkan,
fixed step, release builds with B's field; "before" is `world-ao` as D left it, "after" this grid, the box's bound,
its outline and its far patches:

| Case | Radeon 8060S 1440p | Radeon 4K | RTX 4090 1440p | RTX 4090 4K |
| --- | --- | --- | --- | --- |
| Made up: a Warthog's box and wheels, a crate (Blood Gulch's start) | 0.49 → **0.22** | 1.07 → 0.73 | 0.32 → **0.17** | 0.69 → 0.35 |
| Blood Gulch's start, its own objects | 1.53 → **0.41** | | 1.19 → **0.25** | |
| b30's start, 64 scenery shapes | 6.46 → **3.05** | 12.81 → 6.48 | 2.51 → **1.81** | 4.91 → 2.04 |

The proxies themselves now cost about nothing in the real scenes: with the window's proxies taken away (a
measurement-only switch) b30 costs the same (4.75 against 4.76 ms) and Blood Gulch too (1.53 against 1.49 ms), and
with the made-up field (no bake, no upload) and no shapes b30 is at off (1.79 ms). What is left there is the field's
(B's sampling, being rebuilt by F). (G found otherwise with the GPU held at one clock: the proxies are nearly all of
it; [Where the time goes](#where-the-time-goes-workstream-g).) The made-up case meets the Radeon's budget; the 4090's frame is 0.39 ms, far from
its boost clock, so its deltas run high (D's note).

### Where the time goes (workstream G)

E (proxies away: no change) and F (the field's taps away: no change) seemed to disagree. Neither measurement held
the GPU still:

- **The clock.** A fixed-step run is paced (8.3 ms a frame) and the GPU is busy 1 to 2 ms of it, so the Radeon picks
  its clock from run to run. Draws "world" never touches ran up to twice as fast in one run as in another. Measure
  with `HALO_MAX_FPS=-1`: the frames back to back, the GPU at its top clock, the same frames' time steady to 0.01 ms
  between runs.
- **The size.** Offscreen, the default (borderless) draws at SDL's offscreen desktop, 1024x768, whatever
  `HALO_WINDOW_SIZE` says. Add `HALO_DISPLAY_MODE=windowed`.
- **The frames.** Compare the same 600-frame windows by their number (frames 4,800 to 7,800 below): the scene
  moves, so "the last windows" of two runs are not the same frames.

Held still (`HALO_VULKAN_DRAW_TIMES`: a timestamp after each draw; two fixed-step runs matched draw for draw), on
b30's start at 1024x768: no pre-pass, no extra draw, no stall; the world's shaders with nothing to shade cost
+0.05 ms, the field +0.02, the proxies the rest (+0.81): the loop and its shapes' loads 0.08, each shape's reach,
bound and box frame 0.26 (every kind's reach computed for every shape), the exact boxes 0.41, spheres and capsules
0.06.

**The proxies' walk.** With the subgroups' bitwise operations in pixel shaders (Vulkan 1.1, the device's
`subgroupSupportedOperations`), a subgroup walks the shapes of all of its pixels' cells together, lowest first, and
each pixel takes only its own cell's: the same product of the same shapes in the same order, but each shape's index,
place and kind are the subgroup's, so its loads are scalar and only the kinds present run. Each kind finds its reach,
bound and fade in its own branch; a box finds its frame once for its bound and its faces, branches between its far
patches and its exact faces, and takes one face of each pair (the side the pixel is on). Without subgroups (or
`HALO_VULKAN_NO_SUBGROUPS`) each pixel walks its own cell, as before. The Metal emitter walks the same way where the GPU has the SIMD-groups' reductions (`simd_or`: the Apple7 family and Apple silicon Macs; the backend sets the block's `parameters[1].z`), each kind's reach, bound and fade in its own function; an A12 or A13 walks each pixel's own cell.

World minus off, GPU ms a frame, offscreen Vulkan, fixed step, unpaced, windowed, release; before is `world-ao`
with F, after this:

| Scene | Radeon 8060S 1440p | Radeon 4K | RTX 4090 1440p | RTX 4090 4K |
| --- | --- | --- | --- | --- |
| b30's start | 3.44 → **2.22** | 7.07 → 4.63 | 0.77 → **0.58** | 2.19 → 1.70 |
| Blood Gulch's start | 0.47 → **0.24** | 1.00 → 0.50 | 0.13 → **0.11** | 0.30 → 0.24 |

Blood Gulch is within the Radeon's budget. **b30's start is not**: its 64 shapes (23 scenery boxes of 19 to 98 wu
reach, 13 spheres, 28 capsules) are each found exactly over much of the screen. Without the two largest boxes it
is +1.89; without every shape reaching 30 wu or more, still +0.93. No reordering of the same arithmetic reaches 0.35
there. What would: the static scenery's occlusion of the level found once (a lightmap-space bake per BSP, E's note),
leaving the pixels the dynamic objects' shapes alone; or the boxes' far patches from nearer than 6 half diagonals.
Both change the result slightly, so both are a decision, not done here.

### The static scenery baked

The static scenery's occlusion of the level found once per BSP ([Static scenery](#static-scenery)). World minus
off, GPU ms a frame, offscreen Vulkan, fixed step, unpaced, windowed, release, no input. Ten 600-frame windows,
counted from each run's first window of the game's frames (frames run unpaced through the load too, so a window's
number from the start isn't the same frames from run to run). Before is `world-ao` with master merged
(`world-ao-sync`):

| Scene | Radeon 8060S 1440p | RTX 4090 1440p |
| --- | --- | --- |
| b30's start | 1.67 → **1.31** | 0.50 → **0.39** |
| Blood Gulch's start | 0.25 → **0.14** | 0.11 → **0.05** |

**b30's start is still over the Radeon's budget, and the static scenery isn't what's left.** With the static
scenery out of the loop, the window still fills to its 64 shapes, now all dynamic. About 52 of them are the
marines' capsules, 2 to 6 are spheres, and 8 to 12 are boxes, nearly all a vehicle's (the Pelican over the beach)
(`HALO_WORLD_AO_COUNTS`). Taking the window's shapes out by kind, a measurement-only switch, on the same windows:

| Window | Radeon 1440p | World minus off |
| --- | --- | --- |
| every shape | 2.66 | +1.31 |
| no boxes | 1.64 | +0.31 |
| no capsules | 2.50 | |
| no shapes | 1.47 | +0.14 |

So about 1.0 ms of what's left is the vehicle's exact boxes, about 0.17 the marines' capsules, and 0.14 the field
and the shaders. The vehicle moves, so its boxes can't be baked. What would bring it under 0.35 is G's other way:
a box's far patches from nearer than 6 half diagonals (1.5/255 at 3), or a vehicle's boxes merged into fewer.
Both change the result slightly, so both are a decision.

**The bake** (the first start's pass with `"world"`, every stock map's start BSP, the Radeon box's 32 threads at the
lowest priority, behind the main menu): 27 fields in 7.40 s all told, after a 12.19 s pass, so the first start and
its fields take 19.59 s. The largest are a50 1.05 s (588 placements, its pages 0.60 s), d20 bsp 1 0.86 s, b30
0.71 s and c10 0.50 s; the multiplayer maps take 36 to 156 ms. The cache is 33 MB for all 28 files (b30 1.58 MB,
Blood Gulch 3.11 MB, the largest b40 2.83 MB). Seven pages on five BSPs (damnation, c10, c40 bsp 2, d20 bsp 1 and
d40) have texels two surfaces share, and walk the static shapes themselves there.

**The look.** Blood Gulch's frames differ from before by 0.10/255 on average (0.24% of pixels by more than 4/255).
On b30 the rocks, the ground and the trees look the same in the normal view. In `debug.world_ao_view`, two trees
whose leaves lie within their own collision come out a little greyer, since the field now holds them.

### The vehicles' occlusion fields

The vehicles' boxes replaced by their baked occlusion ([The objects' occlusion fields](#the-objects-occlusion-fields)).
World minus off, GPU ms a frame, measured as for the static scenery: unpaced, windowed 1440p, no input, ten 600-frame
windows. Before is `world-ao` with the static scenery baked:

| Scene | Radeon 8060S 1440p | RTX 4090 1440p |
| --- | --- | --- |
| b30's start | 1.32 → **0.40** | 0.34 → **0.16** |
| Blood Gulch's start | 0.13 → **0.12** | 0.05 → **0.05** |

**b30's start is still over the Radeon's 0.35, and the vehicles aren't what's left.** The two Pelicans now take two
fields of the window's 64 shapes (three slots each), not eight boxes. Taking shapes out of the window by kind
(a measurement-only switch), on the same windows:

| Window | Radeon 1440p | World minus off |
| --- | --- | --- |
| every shape | 1.74 | +0.40 |
| no fields | 1.75 | +0.40 |
| no capsules | 1.58 | +0.23 |
| no shapes | 1.48 | +0.13 |

So the vehicles' fields cost nothing measurable. What's left:
- 0.17 is the marines' capsules (about 52 a window);
- about 0.10 is the other small shapes (spheres, weapons on the ground);
- 0.13 is the field and the shaders with no shapes.

Without the box's bound, a field was fetched over its whole reach, and Blood Gulch's Warthogs cost the 4090 +0.09.

**The look.**
- **b30:** the frames differ from the exact boxes' by 13 to 19/255 on average (the same frames of two "before" runs
  differ by 0.4 to 1.7). That's the boxes' error, not the field's. On the ground about the Pelican, against the
  occlusion of its own 288 collision triangles (4,096 rays a point, 1,500 points within 12 wu), the four boxes are
  off by 23.7/255 on average (they give 40.5 where it's 16.8; at most 201), the field by 1.7 (at most 18). The boxes
  bound nodes that the hull doesn't fill.
- **Blood Gulch:** 0.004/255 on average in the normal view and 0.022 in `debug.world_ao_view` (0.04% of pixels by more
  than 16, at most 121, about the Warthogs). Two "before" runs: 0. Against the Warthog body's own triangles the
  boxes are off by 0.6/255 on average (at most 59), the field by 0.2 (at most 16).
- **The fit's own error** (`tools/harness/tests/world_sdf.c`, a box against the integral): ground 2.2/255 on average
  (at most 18); points about it at any normal, 0.05 wu or more from it, 2.7 (at most 22).

**The bake** (first load, the Radeon box's 32 threads):
- b30's seven fields, 589 ms in all: the Warthog 41×33×33 at 0.125 (1.7 s of the threads' time, 357 KB); the
  Pelican 46×40×29 at 0.41 (2.9 s, 427 KB); the lifepod 6.0 s (766 triangles); the Covenant dropship 3.6 s.
- Blood Gulch's three, 175 ms (Warthog, Scorpion, Ghost).
- The atlas: 3.5 MB on b30, 1.2 MB on Blood Gulch. The cache files: b30's 1.58 → 2.89 MB, Blood Gulch's 3.11 →
  3.63 MB.

**Next, for b30's last 0.05:** the marines' capsules are the largest part (0.17).
- A biped farther off as one capsule (or a sphere) rather than ten. At b30's start the capsules are 8.3 wu from
  the camera on average, 40% of them past 10 wu.
- Or a capsule's reach from a bound like the box's, which falls fast on the ground beside it.

### The scenery's occlusion fields (#666)

The scenery's boxes and spheres replaced by fields of what's drawn ([The objects' occlusion
fields](#the-objects-occlusion-fields)). GPU ms a frame on the Radeon 8060S, unpaced, windowed 1440p, twelve
600-frame windows; a30 a scripted walk to a pine by the lifepod, b30 its start with no input:

| Scene | World before | World after | Off |
| --- | --- | --- | --- |
| a30, by a pine | 6.76 (+6.24) | **2.09 (+1.58)** | 0.52 |
| b30's start | 1.47 (+0.37) | **1.49 (+0.38)** | 1.10 |

a30's trees sway, so every one in view was an exact box walked by every pixel. A field is one fetch.

**The bake** (first load, the Radeon box's 29 threads): a30's 13 fields 0.85 s (the scenery's seven 4.9 s of the
threads' time), b30's 27 fields 1.30 s (the scenery's twenty 12.3 s, 8.6 of it the crashed Pelican's 4,223
triangles, its least detailed level). The atlas: 4.4 MB on a30, 6.8 MB on b30.

## Determinism

The feature only ever writes:
- its own globals;
- the cache folder;
- the command stream.

The bake reads a copy of the tags. The proxies and the scene read the drawn (interpolated) state, which the
simulation never reads back. The checks before merging into master:
- `determinism_compare.py --builds --simulation` on a10 with the bot and the null renderer: branch versus master,
  setting off and `"world"`, every tick alike;
- off pixel-identical to master (0 pixels differ) in the offscreen Vulkan captures #574 used.

## Groundwork toward ray tracing

- **Scene with stable ids** (A): the static meshes become BLASes, the instances a TLAS. Ids are the same across
  frames, so a TLAS refit is enough.
- **Separated lighting terms:** ambient is done (#574). Direct dynamic lights, specular and emissive each get their
  own slot in the emitters, and RT shadows, RTAO, GI or reflections can then replace one of them. This is follow-up
  E, below.
- **Normals, material ids, motion vectors:** the instances' previous transforms (A) and the collision material ids
  are in the scene now; the G-buffer outputs are follow-up E.
- **World light list** (A).
- **Sub-pixel camera jitter** (E, with TAA).

## Workstreams

The integration branch is `world-ao`. Each stream branches from it and opens its PRs into it, never into master. The
draft PR `world-ao` → master stays a draft until every stream lands and the checks above pass.

**The shared files.** Each stream edits only its own files and the marked points named in its brief:
- `render.c`, `scenario.c` and `d3d8_device.c`;
- `xgpu_stream.h`, `xgpu_stream.h`, `host_render_stream.c` and `host_render_stream.h`;
- the backends' stream tables.

The interfaces in `port/linux/include/world_*.h` and `port/linux/src/xgpu_stream.h` are fixed. A stream that needs
one changed says so in its PR and in the other owners' threads first.

| Stream | Owns | Implements | Consumes |
| --- | --- | --- | --- |
| A Scene | `world_scene.c`, `world_scene.h`, `tests/world_scene.c`, `test_world_scene.py` | `world_scene.h` | game state, `render_interpolation.c` |
| B Field | `world_sdf.c`, `world_sdf_bake.c`, `world_sdf.h`, the backends' world AO resources (new `xgpu_vulkan_world_ao.c`, `xgpu_metal_world_ao.c`), the `world_ao_field` and `world_ao_window` callbacks, the start-up pass hook in `menu_pictures_render.c`, `tests/world_sdf.c`, `test_world_sdf.py` | `world_sdf.h`, the field and block bindings | `world_scene_static_triangles` |
| C Proxies | `world_proxies.c`, `world_proxies.h`, `tests/world_proxies.c`, `test_world_proxies.py` | `world_proxies.h` | `world_scene_instances`, model and collision tags |
| D Shading | `world_ao.c`, `world_ao.h`, the emitters (`xgpu_spirv.c`, `xgpu_msl.c`), the pixel key and draw uniforms, `d3d8_device.c`'s world section, the `world_ao_object_begin` hook in `render_objects`, the menu entry, `tests/world_ao.c`, `test_world_ao.py` | `world_ao.h`, the shading | all three, through the block |

### A. World scene representation

Fill in `port/linux/game/world_scene.c` behind the fixed interface `port/linux/include/world_scene.h`. Read
docs/world-ao.md's "The scene" first.

- **Static meshes:** at map load (`world_scene_map_loaded`, already called from `scenario_load` only while
  `display.ambient_occlusion = "world"`), give each structure BSP the id `WORLD_SCENE_ID_STATIC_BIT | index`.
  `world_scene_static_triangles` returns an owned world-space copy of a BSP's collision geometry
  (`collision_bsp` surfaces fanned by their edge rings, `source/physics/collision_bsp_definitions.h`) with per-triangle
  material ids and bounds. The bake runs it on another thread after the tags may be gone, so it must copy.
- **Instances:** each frame (`world_scene_frame_begin`, called from `render_frame` while the objects stand at their
  `render_interpolation.c` pose), list every drawn object. Fields:
  - id = its datum handle;
  - kind and object type;
  - definition tag index;
  - root node transform as drawn this frame and last frame;
  - bounding sphere as drawn;
  - lighting point (where `render_lighting` sampled its ambient: `render_objects_port_last_lighting` is the way in).
- **The world light list:** from the frame's ranked dynamic lights (`port/linux/game/dynamic_lights.c`) and the
  structure's light fixtures, with stable ids.

You own `world_scene.c`, `world_scene.h`, `tools/harness/tests/world_scene.c` and `test_world_scene.py`. Touch no
other file except to add a read-only accessor in `render_interpolation.c` or `dynamic_lights.c`, and say so in the
PR. It must stay render-only: never write game state, never allocate from game memory, nothing when the setting is
off.

Test it in isolation with the harness:
- ids stable over frames and never reused for a live object;
- an instance following its drawn node between ticks;
- the triangle copy matching a hand-built collision BSP;
- the empty case when off.

Then in game with a `debug.world_scene_log` count line and a `debug.world_scene_dump` OBJ of a BSP's triangles,
checked by eye in any mesh viewer on the 5900X. B and C consume your output, so land the triangle copy first, then
the instances.

### B. SDF bake, cache and upload

Implement the structure's distance field: `port/linux/game/world_sdf.c` (schedule, cache, upload) and
`port/linux/game/world_sdf_bake.c` (pure arithmetic), behind the fixed `port/linux/include/world_sdf.h`. Follow
docs/world-ao.md's "The distance field" exactly:
- voxel 0.25 wu, grown to stay within 8 MB (sparse since workstream F: [Geometry, bounds and
  resolution](#geometry-bounds-and-resolution));
- R8 snorm with range 12 voxels;
- exact narrow-band point-triangle distances, signed by the collision BSP's `bsp3d` point test run on a copy;
- slabs of z slices baked on low-priority worker threads.

The owner's hard rule: **never block or lengthen a load.**
- Bake in the first-launch "Preparing menu pictures" pass (#575), which already loads every stock map: call
  `world_sdf_startup_map_loaded` from `menu_pictures_render.c`'s `_prepare_loading` state, and make the pass run when
  only fields are missing via `world_sdf_startup_wanted`.
- At any other time, `world_sdf_map_loaded` only looks in the cache. On a miss it copies the triangles
  (`world_scene_static_triangles`) and bakes in the background after the load.
- Until the field is ready the feature runs with proxies alone. Then the field uploads a few slices a frame (at most
  512 KB) and fades in over 1 s.

Cache:
- path: `<data>/world_sdf/<map>_<bsp>.wsdf`;
- key: the map's cache-file checksum plus `WORLD_SDF_VERSION`;
- contents: the 64-byte header written field by field, the zlib-deflated voxels, then a CRC-32;
- writes go to a temp file renamed into place; a bad file is deleted and baked again;
- follow menu_pictures.c's conventions.

You also own the backends' resources for the feature:
- new `port/linux/vulkan/xgpu_vulkan_world_ao.c` and `port/macos/xgpu_metal_world_ao.c`;
- the 3D texture (with a 1×1×1 far-outside stand-in) and the block's uniform buffer at the bindings fixed in
  `xgpu_spirv.h`, `xgpu_msl.h` and `xgpu_metal_bindings.h` (set 3 bindings 1–2; texture 5, buffer 23);
- the `world_ao_field` and `world_ao_window` callbacks at the marked points in `xgpu_vulkan_stream.c` and
  `xgpu_metal_stream.m`.

Test in isolation:
- harness: plane, box room, sphere and a closed BSP-like box against exact distances (±voxel/2), plus encode/sample
  round trips;
- the cache's round trip and rejection cases;
- the real stock maps baked on the 5900X with a timing log.

Your PR must state the bake time and cache size for every stock map (5900X, Steam Deck, one phone) and show that
load times are unchanged (`halo_load_trace`) with an empty cache.

### C. Object proxies

Implement `port/linux/game/world_proxies.c` behind the fixed `port/linux/include/world_proxies.h`, following
docs/world-ao.md's "Proxies" and "Proxies' occlusion".

Per object definition, at most 16 shapes, made on first use from its tags and cached until `world_proxies_map_unloaded`:
- bipeds: capsules on limb bones (node to child node), a torso capsule and a head sphere, with radii from the
  collision model's pills and spheres where present;
- vehicles: up to 4 boxes from the collision model's region BSP bounds, plus wheel spheres;
- scenery, items and machines: one box;
- otherwise: a bounding sphere;
- nothing for projectiles and effects.

Each frame, `world_proxies_window` places the shapes on the node matrices as drawn. Those are the
`render_interpolation.c` pose; take them via A's `world_scene_instances` and the object's node array. It culls by
reach against the frustum, keeps the nearest 64, and fills the 3-vec4 GPU layout with owner slots.

Also own the reference arithmetic in `world_proxy_occlusion` and `world_proxy_reach`: the sphere is done. Add the
capsule (closest-point sphere with its length factor) and the box (clipped-face Lambert form factor). D's shaders
must match it line for line, so agree the text with D.

You own `world_proxies.c`, `world_proxies.h`, `tools/harness/tests/world_proxies.c` and `test_world_proxies.py`.
Don't touch the backends or emitters: your output reaches the GPU through `halo_world_ao_window`, which
`world_ao.c` (D) calls with your array.

Test in isolation:
- harness: each kind against a numerical hemisphere integral (within 0.05);
- reach and culling;
- the owner slot;
- placement on a fake skeleton between ticks;
- in game, `debug.world_proxies_view` draws the shapes as wireframes over the models, so fit is checked by eye on
  bipeds and every vehicle.

### D. Shading

Make the shaders evaluate the world term in #574's ambient slot, on both backends. Read docs/world-ao.md's
"Shading", "GPU binding layout" and "Settings", and #574's design (`xgpu.h`'s `XGPU_AMBIENT_OCCLUSION_LIGHT_*`).

Emitters (`port/linux/src/xgpu_spirv.c` and `xgpu_msl.c`):
- add a world source to the pixel key beside GTAO's texel;
- for `_LIGHT_MODEL` and `_LIGHT_VERTEX`: the SDF cone term relative to the object's reference, times other owners'
  proxies;
- for `_LIGHT_LIGHTMAP`: proxies alone;
- then the strength and fade from the block;
- read the field at set 3 binding 1 / texture 5 and the block at set 3 binding 2 / buffer 23;
- add the per-draw owner slot and reference to the draw uniforms.

The proxies' math must match `world_proxy_occlusion` (C) line for line, and the SDF sample must match
`world_sdf_sample` (B).

Front end and game side:
- own `port/linux/game/world_ao.c` and `world_ao.h`: the setting, building the window's block from B's
  `world_sdf_current` and fade and C's `world_proxies_window`, and `halo_world_ao_window`;
- own `d3d8_device.c`'s "world" section: pixel keys, the `halo_world_ao_object` uniforms, and keeping
  self-illuminated draws and the first-person weapon at 1;
- add the `world_ao_object_begin` hook in `render_objects`;
- add the menu's WORLD choice in `tools/port_settings.py`;
- add `debug.world_ao_view`.

Off must stay byte-identical shaders and pixel-identical frames, and Classic's `"off"` bypasses everything.

Test in isolation, without A, B or C, by feeding a synthetic block (a plane field and three hand-placed proxies)
through `halo_world_ao_window`:
- harness emitter tests in the style of `tools/test_ambient_occlusion_light.py`: no world code when off; the term
  multiplies only the ambient before any light is added, for each of the three draws; `spirv-val` passes;
- offscreen Vulkan captures on the 5900X;
- Metal builds in CI.

Report `debug.gpu_stats` against the budgets table.

### E. Follow-up (not in this round): G-buffer and lighting terms

Separate the direct dynamic lights, specular and emissive into their own shader slots, as #574 did for the ambient.
Then add per-pixel normal, material id and motion vector outputs to an extended pre-pass (motion from A's previous
transforms), and sub-pixel camera jitter for TAA and upscalers. It starts once D lands, because it changes the same
emitters.
