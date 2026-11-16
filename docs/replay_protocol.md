# The replay service's protocol

`halo_replay <recording> --serve` replays a render recording
(`HALO_RENDER_RECORD`: `port/android/host/host_render_record.h`) and answers
requests about its frames: their pictures, which draw made each pixel, what
each draw was and what each draw costs the GPU and the CPU. A client (a review tool, a script, an agent) starts it as
a child process and talks to it in JSON lines. This document is what a client
may rely on; the code is `port/tools/replay/host_render_replay.c`, shared by
Metal's replay (`port/macos/tools/halo_replay.m`, `ninja macos_replay`) and
Vulkan's (`port/linux/vulkan/tools/halo_replay.c`, `ninja linux_replay`;
`ninja windows_replay` builds it for Windows, `build/windows/halo_replay.exe`,
with the Windows host's renderer, and CI keeps it as the `halo-windows-replay`
artifact).

Recordings and the pictures the service draws hold the game's art: keep them
on your own machines.

## Transport

- One request a line on standard input, one reply a line on standard output,
  in order. The log is on standard error. A client sends one request at a
  time and waits for its reply; it may run more than one service on the same
  recording (one for the frames, one for thumbnails, say).
- The service runs locally or over ssh (`ssh <host> halo_replay <recording>
  --serve`, the recording copied there first). Over ssh, ask for inline
  replies (`"inline": true`) and compressed ones (`"compress": true`); ssh's
  own compression helps little on top.
- A failed request is `{"ok": false, "error": "<words>"}`; the service carries
  on. If the replayed stream itself stops (`"the stream stopped"`), the
  service exits after replying.
- `{"cmd": "quit"}` ends it (as does closing its standard input).

## Versions

`info`'s `"protocol"` is the protocol's version, `HOST_RENDER_REPLAY_PROTOCOL`
in `host_render_replay.h`: **2**. `"protocols"` lists every version the
service serves: a version 2 service serves version 1's clients unchanged
(version 2 added `cost` and `info`'s `protocols`, `capabilities` and
`cost_methods`; nothing of version 1 changed). `cost`'s `"cpu"` and `info`'s
`call_kinds` were added to version 2 later: a service has them if its
`capabilities` has `"cpu_cost"`. The version is raised when a
request or a reply changes so that a client of the old version would misread
it; added fields do not raise it, and a client ignores fields it does not
know. A service without `"protocol"` speaks version 1, and one without
`"protocols"` serves only its `"protocol"`. A client takes a service when
one of the versions it speaks is in `"protocols"` (else `"protocol"`), and
otherwise refuses it and says so.

## Requests

### info

```json
{"cmd": "info"}
{"ok": true, "protocol": 2, "protocols": [1, 2], "frames": 1676, "width": 2560, "height": 1440,
 "backend": "metal", "build": "85ccc010a", "marks": true, "records": 4196312,
 "capabilities": ["frame", "draws", "thumbs", "ids", "pick", "cost", "cpu_cost"], "cost_methods": ["subtract"]}
```

`frames`: the recording's frames, numbered from 0. `width`, `height`: the
last frame's back buffer (a frame's own size is in its `frame` reply).
`backend`: `"metal"` or `"vulkan"`. `build`: the replay's git commit, which a
client can key a cache of pictures on (with `backend`). `marks`: the
recording has MARK records (`debug.render_marks`, below). `records`: the
stream's records. `capabilities`: what this service does (`ids` and `pick`
need the backend's ID buffer, `cost` its GPU timing; `cpu_cost`: `cost`
takes `"cpu"`). `cost_methods`: the methods `cost` can measure draws with
(below), the default last. `call_kinds` (where the backend counts its calls
of the driver, Vulkan's): the kinds `cost`'s `"cpu"` counts them by.

### frame

```json
{"cmd": "frame", "n": 120, "png": "/abs/120.png", "ids": "/abs/120.ids"}
{"ok": true, "n": 120, "ms": 54.2, "width": 2560, "height": 1440}
```

Frame `n` is played exactly (the frames before it first; seeking back plays
from the start again, the frames more than eight before the one asked for
without their GPU work) and its picture is the back buffer it presents.

- `"png": <path>`: the picture written there as a PNG (stored, not
  deflated: fast to write).
- `"image": "qoi"`: the picture as a [QOI](https://qoiformat.org) file
  (lossless RGB, smaller than PNG on the game's frames and a few times faster
  to make), written to `"qoi"` (or `"png"`) when given.
- `"ids": <path>` writes the ID buffer there; `"ids": true` asks for it
  inline. Without `ids` the frame is played once, for its picture alone,
  which is faster.
- `"inline": true`: no files; the reply carries `"png_base64"` (or
  `"qoi_base64"`) and, with `ids`, `"ids_base64"`.
- `"compress": true` (with `inline`): the PNG is deflated, and the ID buffer
  comes as `"ids_zlib_base64"`, a zlib stream, in place of `"ids_base64"`. A
  client accepts either. A 1440p frame's reply with its ID buffer is about
  11 MB instead of 34.

`ms` is the time the play took. `width` and `height` are the frame's picture.

### The ID buffer

`width` x `height` little-endian `uint32`, row-major from the top-left: each
pixel's topmost draw that changed it, as the draw's number in its frame plus
one (0: no draw). A draw is a `DRAW`, `DRAW_INDICES`, `DRAW_INDEXED` or
`DRAW_IMMEDIATE` record of the frame, counted from 0 in the order they were
recorded: the `"i"` of `draws` and `pick`. A draw counts where it changed the
pixel's value: an alpha-tested leaf's pixels, not its quad's cut-out corners.
It covers the draws into the target the frame presents. On a backend without
one, `frame` with `ids` and `pick` answer an error.

### draws

```json
{"cmd": "draws", "n": 120}
{"ok": true, "n": 120, "frame": {"n": 120, "present": 3, ...the view, below}, "draws": [<draw>, ...]}
```

Every draw of the frame, in order. `frame.present` is the frame's present
record's number.

### pick

```json
{"cmd": "pick", "n": 120, "x": 640, "y": 360}
{"ok": true, "n": 120, "x": 640, "y": 360, "stack": [<draw>, ...]}
```

The draws that changed the pixel, topmost first, up to 8
(`HOST_RENDER_REPLAY_ID_DEPTH`). It plays the frame with its ID buffer if the
frame last played was another; a pick on the frame last asked for with
`ids` is immediate.

### thumbs

```json
{"cmd": "thumbs", "every": 30, "width": 160, "dir": "/abs/thumbs"}
{"ok": true, "thumbs": [{"n": 0, "png": "/abs/thumbs/thumb_0.png"}, {"n": 30, "png": "..."}, ...]}
```

Small pictures for a timeline: every `every`th frame (default 30), or the
frames of `"frames": [n, ...]`, scaled to `width` pixels across (default 160).
Written to `dir` as `thumb_<n>.png` (`thumb_<n>.qoi` and `"qoi"` with `"image":
"qoi"`); with `"inline": true` or no `dir`, each is `{"n", "png_base64"}` (or
`"qoi_base64"`), deflated with `"compress": true`. A frame that cannot be
played is left out.

### cost

```json
{"cmd": "cost", "n": 2760, "repeats": 7, "all": true}
{"ok": true, "n": 2760, "method": "subtract", "repeats": 7, "plays": 2310, "frame_ms": 3.285,
 "frame_spread": 0.012, "repeat_exact": true,
 "passes": [{"i": 0, "kind": "sky", "width": 2560, "height": 1440, "draws": 65, "ms": 1.207, "spread": 0.004,
             "draws_ms": 0.468, "draws_spread": 0.022, "draws_sum_ms": 1.221}, ...],
 "draws": [{"i": 62, "pass": 0, "ms": 1.092, "spread": 0.016}, ...],
 "draws_ms": 2.33, "frame_draws": 278, "note": "...", "seconds": 10.9}
```

The GPU's time on frame `n`: the frame's, each pass's and each draw's. The
frame is played exactly once (unless it was the frame last played), then
played again where it is, after itself, as many times as the measure needs
(`plays`). `repeat_exact` says whether the first play again drew exactly the
frame's picture (it should; `false` means state leaks from one play to the
next). The frame asked for after a `cost` is played from the start again,
so its picture is exact.

- `repeats` (default 5, at most 64): the plays each figure is the median of.
- `"all": true`: every draw measured; `"draws": [i, ...]`: those; neither:
  the frame and its passes alone (fast: `repeats` plays). Every draw takes
  about `repeats` x 1.13 x (draws + passes) plays: 278 draws and 7 repeats,
  2310 plays in 11 s on an M-series Mac. A client measures every draw once, cheaply, then the
  few it cares about with more repeats.
- `"method"`: one of `info`'s `cost_methods`; by default the backend's own
  if it has one (Vulkan's `timestamps`), else `subtract`.

`frame_ms`: the time the GPU was busy with the frame (its passes' times
added). Each pass, in the order it was made: `kind` (what its first draw
was, or the backend's own pass: `aside` for a small target's pass put
aside, `anti-aliasing`, `copies`...), its target's size, its `draws`, its
`ms` (its part of `frame_ms`: from its start, or the end of the passes
before it, to its end) and, with `all`, `draws_ms`, what the frame takes
less without all its draws at once, and `draws_sum_ms`, its draws' `ms`
added. Each draw: its `pass` (null: none timed it) and its `ms`. Every
figure has its `spread`: the median of the repeats' distances from their
median, x 1.4826 (a normal distribution's standard deviation). A
difference smaller than about twice the two spreads together is noise.
`draws_ms`: the draws' `ms` added; `note`: the backend's words on what its
timing cannot see.

#### The methods

**subtract** (every backend). A draw's `ms` is what the frame takes less
without it: the frame played with the draw left out (not handed to the
backend; the state it was drawn with kept), between plays of the frame
whole, 8 draws between two, the whole's time the mean of the two about it
(so that the GPU's clocks, and other programs' work on the GPU, which change
over seconds, cancel), counted from the draw's pass on (the passes made
before it that ran before it cannot change: their time would be noise). It
is what removing or fixing the draw saves: its own vertices and pixels, and
what it costs later passes (a surface that gives the anti-aliasing edges to
work on, a target sampled later). It is the method for a tiler, where a
draw's pixels are shaded when its pass ends, not when it is drawn: Apple's
GPUs sample timestamps only where a pass's stages begin and end
(`MTLCounterSamplingPointAtStageBoundary`), and a pass ended at each draw
to time it would no longer be the pass it is (each draw's loads and stores,
its hidden surfaces no longer removed). A draw's `ms` can be negative:
within its spread, noise; beyond it, the draw made later work cheaper (an
opaque surface hiding what is behind it). Draws' costs do not add up: two
draws that hide each other each cost little, together much (`draws_ms`
against `draws_sum_ms`).

**timestamps** (Vulkan). A timestamp at the bottom of the pipeline before
and after each draw: the draw's own time. The timestamp before a draw waits
for the work before it, so draws an immediate GPU would overlap run one
after another; it does not see what a draw costs later passes; the first
draw of a pass carries the pass's load or clear; times come in steps of the
device's timestamp period (`note` gives `timestampPeriod` and
`timestampValidBits`). On a tiler it says little: draws are shaded at the
pass's end.

#### The CPU's time: "cpu"

```json
{"cmd": "cost", "n": 2760, "repeats": 7, "cpu": true}
{"ok": true, ..., "cpu": {"repeats": 7, "clock_ns": 7.9, "ticks_per_ms": 3700000.0,
  "frame_ms": 2.412, "frame_spread": 0.031, "wall_ms": 2.47, "after_ms": 0.21,
  "driver_ms": 1.13, "driver_spread": 0.02, "calls": 4312, "after_driver_ms": 0.09, "watched_ms": 2.66,
  "kinds": [{"kind": "draw", "calls": 640, "ms": 0.16, "spread": 0.004}, ...],
  "records": [{"type": "set_textures", "count": 512, "ms": 0.31, "spread": 0.01, "driver_ms": 0.02}, ...],
  "passes": [{"i": 0, "draws": 65, "ms": 0.52, "spread": 0.01, "driver_ms": 0.25, "calls": 610}, ...],
  "draws": [{"i": 62, "pass": 0, "ms": 0.0081, "spread": 0.0004, "own_ms": 0.0042,
             "driver_ms": 0.0031, "driver_spread": 0.0002,
             "calls": {"bind_pipeline": 1, "bind_descriptors": 2, "push_constants": 1, "draw": 1}}, ...],
  "draws_ms": 2.19, "draws_calls": 3920}}
```

With `"cpu": true` the reply has `cpu` too: the render thread's time on the
frame, which is the backend's code and the driver's under it (where the
game's render thread spends its time: decoding the stream, recording the
command buffers, submitting them). After the GPU's plays, the frame is
played again where it is `repeats` times without the GPU's timestamps, each
record timed on the render thread (the processor's counter read after the
replay has put back the memory it reads, and again once it is played: the
replay's own work is not counted), and, where the backend counts its calls
of the driver (`call_kinds`), `repeats` times more in turn with each of those
calls timed and counted. The memory the frame's play in order found changed
counts as written again in each play, so that the backend makes again what
it made then (textures decoded, vertices copied), as the game does each
frame.

- `frame_ms`: the frame's records' time added (the median of the plays).
  `wall_ms`: from the frame's first record to its end, waits included.
  `after_ms`: the records after its last draw (the anti-aliasing, the tone
  map, the present, the frame's end and its submission).
- `driver_ms`: the time inside the driver's calls (the clock's own cost,
  `clock_ns` a read, taken off each), `calls` their number; `kinds` by kind.
  `watched_ms` is `frame_ms` with the calls watched: what the watch itself
  costs, which `frame_ms` and each draw's `ms` do not carry.
- `records`: each record type's `count` in the frame, its time and the
  driver's within it.
- `passes`: each GPU pass's (as `passes` above numbers them; `null`: the
  draws no pass timed) draws' times added, the driver's and the calls.
- `draws`: every draw: `ms`, the records after the draw before it to it (its
  state: textures, constants, pipeline, vertices) and the draw itself;
  `own_ms`, the draw record's alone; `driver_ms` and `calls`, the driver's
  among them, by kind (only the kinds it made). `draws_ms` is their sum.

The kinds (Vulkan's, `xgpu_vulkan.h`): `bind_pipeline`, `bind_descriptors`,
`update_descriptors`, `allocate_descriptors` (and the pools' resets),
`push_constants`, `bind_buffers` (vertex and index), `dynamic_state`
(viewport, scissor, depth bias, blend constants, stencil reference,
topology), `draw` (and dispatches), `clear`, `begin_commands` and
`end_commands` (a command buffer begun and ended: the secondary command
buffers' and the submissions'), `execute_commands`, `rendering` (a dynamic
rendering begun or ended), `barrier`, `copy`, `query`, `submit`, `wait`
(semaphores waited for and read), `create` (samplers, views, images,
buffers, memory, command buffers made).

The clock is the processor's counter (x86's time stamp counter, AArch64's
virtual counter: an Apple GPU Mac's ticks are 41.7 ns), converted to
milliseconds against the monotonic clock over the plays (`ticks_per_ms`).
`clock_ns` is also a gauge of the processor's clock: a frame of the render
thread's is short, and a processor that idles between plays (a laptop's
powersave governor) may not raise its clock for it. On bosgame's i5-1135G7 the
Linux replay's figures were 2.2 times longer with the processor otherwise idle
(`clock_ns` 20.7) than with another process keeping it at its full clock
(7.0): compare machines at like `clock_ns`, or scale by it, and by the
records that make no driver calls (`constants`, `set_textures`,
`vertices`), the backend's own code on each.

The CPU's figures have the GPU's caveats: measure with nothing else running,
compare builds or machines measured alike, and a difference counts only past
the spreads. Two replays of one recording on two machines, or two drivers
on one GPU, compare draw by draw (by `i`): the same records, the same calls,
the driver's time and the backend's (`ms` less `driver_ms`) apart.

#### Accuracy

Measured on an M-series Mac (Metal), Blood Gulch at 2560x1440:

- **The frame and its passes**, against `HALO_METAL_PASS_TIMES` (each
  pass's time as the game takes it, on a normal replay of the same frames):
  frames 2751-2770 measured one by one with `cost` (5 repeats) have a median
  of 3.37 and 3.51 ms in two runs, the normal replay's busy time 3.57 and
  3.58 ms: within 6%. The passes by kind: 1.233 against 1.254 ms (the first
  pass), 0.042 against 0.041 (the HUD), 0.323 against 0.308
  (anti-aliasing), 0.576 against 0.618 (the aside passes), 0.090 against
  0.090 (copies). On a frame of 133 draws the passes agreed within 2%.
- **A draw's `ms`** (subtract, 9 repeats, two runs of one frame): on a quiet
  GPU the runs differ by a median of 0.005 ms, 0.013 ms at the 90th
  percentile, and the costliest draws by 0-12% (0.334 and 0.334 ms; 0.290
  and 0.293; 0.170 and 0.191). With other programs drawing on the GPU (a
  game, another replay) the differences grow five times (0.027 and 0.072
  ms): measure on a quiet GPU, and look at `frame_spread`.
- **Sums**: where draws hide each other, the draws' `ms` add up neither to
  the frame's nor to their pass's `draws_ms` (above: a first pass of 65
  draws, `draws_ms` 0.468, `draws_sum_ms` 1.221). Where they do not, they
  agree to their spreads (the shadow maps' aside passes: 0.072 against
  0.060 ms, 0.064 against 0.075).

Vulkan (an RTX 4090, `timestampPeriod` 1 ns, 64 valid bits): the passes'
shares of the frame match `debug.pass_times`' over the same frames within a
few points (the main pass 56% and 56%, anti-aliasing 22% and 22%). In ms,
the frame played again in place runs 25-50% longer than in an unpaced
replay (the GPU idles between the plays and its clocks drop): compare
shares, or builds measured alike.

**A build against another**: the same recording replayed by each build's
replay, each measured with `cost` in turn (not at once: they would share
the GPU), the draws matched by `i` and ranked by the difference of their
`ms`, a difference counted only past both spreads.

## A draw

Every draw has:

| Field | |
| --- | --- |
| `i` | its number in the frame (the ID buffer holds `i + 1`) |
| `kind` | what it belongs to (marks); `"other"` without marks |
| `label` | its owner's label (marks), else its record and primitive (`"draw_indexed triangles"`) |
| `id` | the owner's id (an object's handle, a widget's instance, a lightmap's `index << 16 \| material`), or `null` |
| `tags` | the tag paths known of the owner: `object`, `model`, `shader`, `bitmap` (only those known; `{}` without marks) |
| `source` | a menu widget's generator source, `{"file", "text"}`, or `null` |
| `category` | the game renderer's profile category (`rasterizer.h`'s), or `null` |
| `record` | `draw`, `draw_indices`, `draw_indexed` or `draw_immediate` |
| `primitive`, `count`, `prims` | the primitive, its vertex or index count, the primitives it makes |
| `pipeline` | the pipeline's number, as a string |
| `target`, `depth_target` | the colour and depth targets drawn into |
| `textures` | each bound stage: `{"stage", "source": "texture" \| "target" \| "image", "resource", "target", "format"}` (`resource` and `format` in hexadecimal) |

A menu widget's draw also has `widget`, its tag's path. Its `source.text` is
the widget's whole source attribute (`tools/menu_sources.py`: its `locate`
finds the generator function's line), and `source.file` its first word, the
generator's file.

`kind` is one of `other`, `world_object`, `first_person`, `bsp`, `sky`,
`decal`, `effect`, `particle`, `contrail`, `lightning`, `weather`, `water`,
`hud`, `text`, `menu_widget`, `cinematic_letterbox`, `debug`,
`detail_object`, `lens_flare`, `fog`, `screen_effect`, `light_volume`,
`shadow`, `clear`. A client shows a kind it does not know as `other`.

## Marks

A recording made with `debug.render_marks` (`HALO_RENDER_MARKS=1` beside
`HALO_RENDER_RECORD`, and `HALO_METAL_UNPACED=1` on a Mac) carries the
stream's MARK records (`port/linux/game/render_marks.c`,
`port/linux/src/xgpu_stream.h`): what each run of draws belongs to, and each
view's camera. `info`'s `marks` says whether it has them. With them, the
draws' `kind`, `label`, `id`, `tags`, `widget` and `source` are filled, and
`draws`' `frame` gains the frame's view:

| Field | |
| --- | --- |
| `map` | the map's name, or `null` |
| `tick` | the game tick |
| `window` | the view's window (split screen's), `null` for the frame's start |
| `camera` | `{"position": [x, y, z], "forward": [x, y, z], "up": [x, y, z], "fov": <horizontal, radians>}` |
| `viewport` | `[x, y, width, height]` in the back buffer's pixels |
| `views` | every view of the frame, each with the fields above: the frame's start's first (`window` `null`), then one per window |

The frame's own fields are its first window's view (or the frame's start's,
if it has no window). A recording without marks plays the same; its draws are
unnamed and its frame has `n` and `present` alone.

## Notes: build/notes.jsonl

A client may leave review notes on what it shows, in the checkout's
`build/notes.jsonl` (out of git), one JSON object a line; `tools/notes.py`
lists, batches and briefs them for whoever fixes them, and marks them taken,
answered and ready for review (`python tools/notes.py --help`). They are the
menu notes' format (`tools/menu_notes.py`, `build/menu_notes.jsonl`) with an
anchor. A client rewrites the file whole (to a temporary file, then renamed
over it) and reads it again when it changes.

| Field | |
| --- | --- |
| `id` | unique, short |
| `time` | when it was written, ISO 8601 UTC (`2026-11-08T00:00:00Z`) |
| `author` | who wrote it (`reviewer`) |
| `origin` | what wrote it (the client's name) |
| `kind` | `fix`, or `question` (no code changed until the reviewer answers on its thread) |
| `scope` | `variant`: this moment; `screen`: this thing (the object or tag wherever it draws); `everywhere`: every draw like it (its kind, its shader) |
| `status` | `open`, `in_progress`, `fixed` (ready for review: `done` is its older name), `closed` (approved) |
| `note` | the words, at most 2000 characters |
| `thread` | replies, `[{"author", "text", "time"}, ...]` |
| `anchor` | the moment, below |
| `picture` | the frame with the draw outlined, a PNG, as a path relative to the checkout (`build/inspector/notes/<id>.png`) |
| `after` | once fixed: the frame drawn again (`tools/notes.py fixed` renders it into `build/inspector/after/`) |
| `commit`, `changed` | once fixed: the fix's commit, when it was marked |

The anchor:

```json
{"recording": "build/r.hrpl", "frame": 120, "pixel": [640, 360],
 "draw": {"i": 3, "kind": "world_object", "label": "warthog #2", "id": 65558,
          "tags": {"object": "vehicles\\warthog\\warthog", "shader": "..."}, "source": null,
          "textures": [...], "pipeline": "12"},
 "camera": {...}, "map": "bloodgulch", "tick": 4200, "backend": "metal", "build": "85ccc010a",
 "source_name": "Metal"}
```

`recording` is relative to the checkout when it is in it. `draw` is the
picked draw as the service gave it (any of its fields), `null` for a note on
the frame as a whole; `camera`, `map` and `tick` are the frame's view;
`backend` and `build` are `info`'s; `source_name` is the client's name for the
service it drew the frame with.

A note on a menu widget (its draw's `kind` `menu_widget`) also carries the
menu notes' `screen`, `widget` and `source`, and is a menu note: its brief and
`fixed` are `tools/menu_notes.py`'s.
