# Demos

A demo is a recording of a game, tick by tick, that plays back exactly: the
same game state after every tick as the game that was recorded, as a
Counter-Strike demo is. With `demo.record` on (it is off by default), every
game this computer plays or hosts is recorded as one: the campaign, split
screen, games with bots, a network game it hosts, a dedicated server's. They are the base for clips,
bug reports' reproductions, kill cams and the render inspector.

The code is `port/linux/game/demo.c` (what a demo holds, recording and
playback) and `port/linux/src/demo_files.c` (the files, their writer and
their retention; `demo_unpack.c` reads their blocks). `tools/demo_info.py`
prints what a demo holds.

## Where they are

Demos are kept in the data root's `demos/` folder (the folder with `maps/`),
one file a map: `20261108-014642-a10.demo`, the time it began and the map.
As a new demo begins, the oldest are deleted until no more than
`demo.keep_count` (20) are kept and they take no more than
`demo.keep_megabytes` (2048 MB). A demo moved into `demos/kept/` is pinned:
it is never deleted, nor counted.

`halo --demo <file>` (or `demo.play`) plays one: its map loads at its
difficulty and its game plays as it was recorded. Nothing of the player's
own saved games is read or written meanwhile (their checkpoint, their last
level). With `debug.demo_quit` the game quits when the demo ends, for tests.

## What a demo holds, and why

The game's simulation, its ticks, is a function of the game state (one block
of memory: `source/saved games/game_state.c`), the two random seeds the ticks
take (`source/math/random_math.c`) and the players' input of each tick
("Determinism" in `port/linux/README.md`). So a demo holds:

- **The start.** A snapshot of the game state (each of its allocations' part
  in use: a data array to its count, a memory pool to its last block) and
  the seeds, at the map's first tick (a network game's first: its lobby is
  not recorded).
- **Each tick's input.** Every player's action as the tick takes it
  (`players.c`'s `players_update_before_game`): this machine's players', the
  bots' and the other machines' players' that the netcode gave it, and the
  seeds where the bots took them. At `game.sim_rate` 60 and 120, each later
  sub-step's controls of the players' units (`player_control.c`'s slot of the
  fast phase): their buttons, sticks, trigger and aim, and the presses seen
  in it.
- **The frames' input the ticks read.** The player control globals, which
  the frames write from the controllers and which the scripts test (Pillar of
  Autumn's tutorial asks the player to look up and down), whole at each
  tick's start that they changed by, and before each later sub-step of a
  tick left open across frames (the HUD's crosshair reads the aim assist's
  level the frames set meanwhile), with the seeds where the frames took the
  game's.
- **What reached the game state from outside the ticks.** At each tick's
  start, and each sub-step's when the tick was open across frames, the
  allocations the ticks keep are compared with what the tick before left,
  and the bytes that changed are recorded: a player who joined or left, a
  host's decision, a console command, an effect a particle made as it
  struck. When the game state was rewritten as a whole (a checkpoint
  reverted to, a saved game or core loaded, the map started over), or the
  game's code made or deleted an object outside the ticks, the whole game
  state is recorded instead, as at the start. Each is taken, however often
  (the playback stays exact), but past four one after another and one more
  each `demo.snapshot_seconds` the recording warns in the log, once a
  minute at most: a game whose code does that every tick takes a whole game
  state a tick, about 1 MB packed each (below, "Sizes and cost").
- **What the ticks read of the frames.** Each of this machine's players'
  camera (`observer_get_camera`) as the tick begins, which the material
  effects test by distance and a player without a unit sees the level from,
  and again where the frames moved it before a later sub-step of an open
  tick or an event of the netcode's (a hit's damage indicator points from
  its player's camera); and a unit's illumination, which the AI's sight reads and which is the
  lights' (the frames') at its place.
- **What the netcode put in inside a tick.** A host takes its clients'
  predictions of their own units at the tick's start
  (`network_distributed.c`): what that changed is recorded, and the
  choices it makes by which machines are there (a client's hits are its
  report's to deal: `network_damage.c`).
- **What the netcode put in after a tick.** The predictions of the clients
  whose units a host moves once the tick has ended (an OpenCE machine's in
  a mixed game, every client's in a Legacy game: `distributed_apply_predictions`
  in `network_distributed_tick`) are recorded as the bytes they changed
  (`demo_netcode_begin`, `demo_netcode_end`: an `M` before the next tick),
  as the predictions taken at a tick's start are, and the playback writes
  them in there.
- **What the netcode does outside the ticks, as events.** A client's hit
  that the host deals as its report comes (`network_damage.c`, between two
  ticks or two sub-steps of an open tick) is recorded as the hit dealt, with
  the seeds its code takes (the game's, and the frames' local seed its
  effects take), at its place among the frames' differences: what changed
  before it, the hit, then what changed after it. The playback deals it
  again there, through the same code (`network_damage_deal_recorded`,
  which checks it as a report is), so that what the hit's code does
  outside the game state is done too (the game type's score, which the
  game keeps outside it; an object's cluster references; its effects); its
  bytes are not written in. So are the network test's own (a host's player
  shooting, its kill of the last player: `debug.network_test_shoot`,
  `debug.network_test_kill`).
- **What the game keeps outside the game state** and the ticks read: the
  game engine's (`game_engine_port_demo_state`: its flags, by the players
  the game began with, five or more taking fewer grenades from a map's
  places; the goals, the vehicles' homes, the friendly fire penalties, each
  game type's state, its scores), the speakers' mouths
  (`game_sound_port_demo_state`), the wind (`wind_port_demo_state`: its
  random walk, which the ticks step and an antenna's and a flag's points are
  blown by) and, in a mixed game, which players are OpenCE machines'
  (`network_standard_port_demo_state`: their units, shots and grenades move
  once a tick, `object_get_lane`), what the ticks asked of the main loop
  with its timers (`main_port_demo_state`: a respawn, a lost level's
  revert), and, a co-op host's, the network game
  (its players, its co-op switches) and what the host's co-op code keeps
  (`network_coop_port_demo_state`: its choices of EXTRA ENEMIES, pickups
  and vehicles as the game began, the extra enemies waiting and the
  vehicles' homes, the players' respawn and loading-zone timers and their
  places at the last checkpoint, the scripts' vitality for every player,
  each player's friendly fire and whether they are away, each client's
  structure BSP, a dedicated server's loading-zone rules), whole with each
  snapshot and, as the bytes that changed, wherever it changed outside the
  ticks; the playback puts it back as it was.
- **The frames' choices a tick takes**: whether a particle waits for a
  later frame (`render_interpolation.c`), which decides whether the tick's
  local seed makes it.
- **A hash of the game state three times a second**, and of each allocation, which
  the playback compares with its own (a data array's free datums left out:
  what the frames made and deleted again there, which no tick reads).
- **A snapshot every `demo.snapshot_seconds`** (10), for seeking.
- **Bookmarks** (the clip key) and the game's variant.

The frames' own allocations are neither compared nor hashed: the particles,
decals, contrails, lights (with their cluster references), the motion
sensor, the first-person weapon posed at the camera, the sound's state, the
game time globals (`tools/determinism_compare.py`'s `FRAME_ALLOCATIONS`).
Nor are the objects' fields that the frames write as well as the ticks: an
object's magic number (the collision tests' mark), its render state's
index, its attachments that are the frames' datums.

## Playback

A demo's playback loads its map as a game of this machine (a multiplayer
game's with its variant), and at its first tick takes the start's snapshot.
Then each tick runs with the recorded input in place of the players': this
machine's controllers, the bots and the other machines play no part. At each
tick's start, what the playback's own frames wrote into the allocations the
ticks keep is put back as the tick before left it, and what the recording
had from outside the ticks is written in; a whole game state recorded is
taken as a saved game is (`game_state.c`: the loading's procedures before
and after, the game state checked between them, then written again over
what the procedures changed). The playback reverts to no checkpoint of its
own (`game_state_revert`): what the recording's revert gave comes whole
with the next tick, and a playback that seeks has taken none.

A co-op game this machine hosted plays its ticks as the host's
(`demo_playback_connection`, `demo_playback_network_game`): inside them
the connection is the host's and the network game is the recording's, so
the host's co-op code there (its players spawning beside a teammate, the
extra enemies by the network game's players, the respawns, the loading
zones) runs as it ran; outside them (the frames, the menus) the playback is
a game of this machine's own. Before format 4 the ticks of such a playback
ran as the campaign's, a game of one machine: its other players spawned at
once at the level's start, and the playback parted from the recording at
its first ticks, then stopped at a record that no longer fit its game
state. A multiplayer game's ticks run as this machine's, as they always
did: what its host decided by its machines there is recorded (`D`, `F`).

While a demo plays, the keyboard's own controls drive the playback, not a
player: the flashlight's key and the action's (Q, E) seek 10 seconds back
and on, to the snapshot nearest before that point; switching the grenade
and crouching (X, C) play twice as fast or half as fast (from a quarter to
eight times); jumping (space) pauses. The console's line shows where the
playback is. Backspace switches the camera as it does in a game: the
player's eyes, a flying camera (hold Tab or the right mouse button and fly
with W, A, S, D, R, F), and one following the player.

The playback checks its hash against the recording's three times a second. If they
differ, it says where in the log ("parted from the recording by tick 1680:
effect"), names the allocations that differ, and plays on as its own game:
on the recorded input and whole game states, the recorded differences no
longer written (the recording's bytes would not fit its game state: an
object's place with another's cluster references).

## The file

A header of 512 bytes (`OH1DEMO`, the format's version (4), the build, the
network version, the game state's allocations' checksum, the connection,
the rate, the difficulty, the game's rules, the map), then blocks, each packed with zlib on a
thread of its own as the game plays (`demo_writer_*`): a second of records
(`R`), a snapshot the playback takes (`K`: the start, a reload) or one only
a seek takes (`k`). A record is its type (a byte), its length (4 bytes) and
its bytes:

| Record | Holds |
| --- | --- |
| `T` | a tick begins: its number, its rate |
| `K` | the game state: each allocation's part in use, and the seeds |
| `P` | what changed in the game state since the tick before ended |
| `M` | the same, before a sub-step of a tick left open across frames (of sub-step 0: before a tick, ahead of an `X`, or what a host's netcode put in after the tick before) |
| `C` | the player control globals, whole |
| `S` | the seeds |
| `A` | the tick's players' actions (`struct player_action`) |
| `U` | a sub-step's work begins (every sub-step's, at every rate): the tick's number, the sub-step's and their count, then (past the first) its units' controls |
| `F` | what the netcode put in inside the tick |
| `D` | a choice the netcode made |
| `X` | a hit the host dealt outside the ticks (a client's reported, the network test's): the seeds, then the hit as dealt |
| `Y` | a kill the host made for a player outside the ticks (the network test's): the seeds, the unit, the player |
| `G` | what the game keeps outside the game state (the game engine's, the speakers' mouths, the wind, a mixed game's OpenCE players, a co-op host's network game and co-op state), where it changed outside the ticks: its size, then each run of bytes that changed (its offset, its size, its bytes; one run of it all with a snapshot) |
| `L` | whether a tick's particle waited for a later frame |
| `O` | a local player's camera as the tick begins (and before a later sub-step or an event, where the frames moved it) |
| `I` | a unit's illumination as its update took it |
| `Z` | the tick ends: its number |
| `H` | the game state's hash, and each allocation's |
| `V` | the game's variant and its options |
| `B` | a bookmark: its tick and how far back it reaches |
| `W` | a line of the host's to its bots (`docs/bot_control.md`) as the tick applied it, before the tick's `A`: its tick, then the line; the playback logs it, and the bots' recorded input (`A`) plays it |
| `E` | the end |

Each tick has one shape, whatever came when: its `T`; what came from
outside it; its input (`A`); each sub-step's `U`, in order, with the
records its code made as it ran between them (`D`, `F`, `I`, `L`, read
back by the same code as it runs again); and its `Z`. Between two ticks
there is only what came from outside them. So a playback whose code ran
otherwise than the recording's (it parted from it) leaves the records its
code did not take at the next `U` or `Z`, and goes on: a record never stands
where another sub-step's or tick's is looked for. `tools/demo_info.py
--check` checks a demo's shape.

The game's rules that this machine's settings would otherwise give a game
of its own are in the header, and the playback plays by them, not by the
settings of the machine it plays on: the players' animation enhancements
(`game.enhanced_animations`, which `display.graphics` chooses), their
turning with the aim, and their way of picking up a second weapon
(`game.auto_pickup`), whether it played OpenH1's fixes of the
original's simulation (a Legacy game plays the original's), and whether its
players hijacked vehicles (`game.vehicle_hijack`), as the recorded
game had them (`network_standard.c`'s `network_standard_demo_rules`). A
host's recording plays the hijack rule as its record had it each tick (the
G records' network game), so that Host Controls' change of it during the
game plays back too. Nothing a machine's drawing settings
choose (the graphics preset's drawing, the first-person legs, the field of
view) reaches the game state.

A demo of an earlier format is refused with its format's number ("a demo
of format 2, which this build does not play"). A block cut short (the game
crashed or was killed) ends the demo there; the blocks before it play. A demo is a file anyone may send: its playback checks
every range it writes into the game state (each wholly in one allocation; a
snapshot's each an allocation from its start), the game state after each
as a saved game's is checked, every input as the netcode checks another
machine's, and unpacks with the port's zlib (`port/third_party/zlib`).

A demo of another build is refused with the reason: its build (the release's
number and commit; a local build's `local`), its platform (Linux x86-64 or
arm64, Windows, macOS, iOS, Android, VR or not), its network version and its
game state's layout must be this build's. The simulation is the code, and a
snapshot holds the guest's code's addresses (the caches' procedures, as a
saved game does), each build's own.

## Clips

The clip key (`controls.clip`, F9; CLIP in Controls Setup > Actions) marks
the demo being recorded where it is pressed: a bookmark of its last
`demo.clip_seconds` (30), which `tools/demo_info.py` lists. (Not yet: a
controller's button for it, and the export of a bookmark to video.)

## Sizes and cost

Measured on the Linux x86-64 build (`tools/demo_info.py`):

| Game | MB a minute |
| --- | --- |
| Pillar of Autumn (a10), the campaign's start, 30, 60 or 120 Hz | 1.1 |
| The Silent Cartographer (b30), fighting, a death every 20 s, 60 Hz | 2.3 |
| Blood Gulch Slayer, a player and 3 bots, 60 Hz | 0.7 |
| Blood Gulch Slayer, a player and 3 bots, 120 Hz | 0.8 |
| Blood Gulch Slayer, a player and 7 bots, 120 Hz | 1.1 |
| Blood Gulch Slayer hosted, a client, 120 Hz | 1.6 |
| Blood Gulch Slayer hosted, an OpenH1 and an OpenCE client (`debug.join_legacy`), 3 bots, 120 Hz | 2.5 |
| The Truth and Reconciliation (b40) co-op hosted, 2 OpenH1 and 4 OpenCE clients, `network.coop_enemies` 100, 120 Hz | 6.7 |

Most of it is the snapshots for seeking (about 150 KB packed each in the
campaign) and the whole game states of reloads (a revert's). Recording
takes 0.05 to 0.1 ms of the game's thread a tick (the comparison of the
allocations the ticks keep, about 1 MB, with what the tick before left),
and packing and writing are on a thread of their own; the co-op game above
took 0.28 ms a tick.

Before the host's predictions after a tick were recorded as their bytes, a
mixed game took a whole game state for each tick an OpenCE player moved in:
that co-op game made 220 to 240 MB a minute (0.48 ms a tick), and the
maintainer's, with four OpenCE players over 30 minutes, 27 GB.

## What is checked

`debug.determinism_log` hashes the game state after every tick;
`tools/determinism_compare.py` compares a recording's log with its
playback's (`--simulation`: not the frames' allocations, the lights'
among them, as the demo has them, and a data
array's datums in use only, the log's `u` line, not what its free slots
hold; `--builds`: not the seeds' takers' sites; `--game`: from a network
game's first tick, after its lobby):

```sh
HALO_DEMO_RECORD=1 HALO_DETERMINISM_LOG=rec.txt HALO_TEST_INPUT=bot:3 HALO_SKIP_CINEMATICS=1 ./halo   # init.txt: map_name levels\a10\a10
HALO_DETERMINISM_LOG=play.txt HALO_DEMO_PLAY=demos/<demo> HALO_DEMO_QUIT=1 HALO_DEMO_RECORD=0 ./halo
python tools/determinism_compare.py --simulation --builds \
    --ignore "object list header" --ignore "list object reference" rec.txt play.txt
```

Measured on the last build (omarchy, Linux x86-64, the null renderer, in
real time but where it says `debug.frame_time`), each playback against its
recording, every tick and every one of the demo's own hashes:

| Run | Ticks alike | Hashes as recorded |
| --- | --- | --- |
| a10 at 60 Hz, `debug.frame_time` | 7381 | 738 of 738 |
| a10 at 30 Hz, `debug.frame_time` | 7892 | 789 of 789 |
| a10 at 120 Hz, `debug.frame_time` | 7292 | 729 of 729 |
| a10 in real time | 1771 | 177 of 177 |
| b30 fighting, deaths and reverts, `debug.frame_time` | 6432 | 641 of 641 |
| Slayer, a player and 3 bots, 30 Hz | 2460 | 246 of 246 |
| Slayer, a player and 3 bots, 60 Hz (twice) | 2460 each | 246 of 246 each |
| Slayer, a player and 3 bots, 120 Hz (twice) | 2460 each | 246 of 246 each |
| Slayer, a player and 7 bots, 120 Hz | 2460 | 246 of 246 |
| Slayer, a player and 7 bots, 120 Hz, 2.7 minutes (twice) | 4800 each | 480 of 480 each |
| Slayer, a player and 7 bots, 60 Hz, 2.7 minutes (twice) | 4800 each | 480 of 480 each |
| Blood Gulch hosted, a client, 120 Hz, the network test's shots and kills, 2.75 minutes (four) | 4950 each | 495 of 495 each |
| Blood Gulch hosted with 20 bots at 120 Hz, the renderer on and a player's own settings (enhanced lights, HDR, legs, 100° FOV), a client joined over the internet from another computer (15 ms more each way, 1% of datagrams lost), 4.9 minutes; played on that other computer with the default settings | 8747 | 874 of 874 |

(A tick is the game's 30th of a second at every rate; at 60 and 120 it
runs in 2 or 4 sub-steps. 4800 ticks are 160 seconds.)

At 60 and 120 a particle takes the frames' seed now (below), so a game
differs from a build before it in its local seed; nothing another machine
has reads that seed (each machine makes its own particles and effects, and
the local seed is never sent). `tools/net_compare.py` with this build and
master's, hosted at 120 Hz with three bots, each build hosting in turn
(`--swap`), 94 seconds compared each: both in step (drift 4 and 5 ticks of
2600 apart, largest 0.003 world units, as two builds alike have).

A recorder killed as it plays (the timeout's SIGINT in these runs, which
stops the game at once; a crash) leaves a demo whose whole blocks play:
the playback's index stops at a block cut short, and a recording that ends
inside a tick ends its playback there ("the file ends there", not a damaged
record; every run above ended so). `tools/test_demo_files.py` kills a
writer as it writes and reads what it left, cut anywhere.

Before these fixes the same runs parted: Slayer with three bots after 17
to 38 seconds in each of four runs (a speaker's mouth), with five or seven
at its first tick (the game engine's flags), hosted games after 3 to 77
seconds (the network test's kills and the game type's score, a datum
deleted from an array's end, a particle held for a later frame).

Where they parted, and what changed:

- **A speaker's mouth.** The mouths the speech sounds move (`game_sound.c`)
  are kept outside the game state, with the speakers' seed: a playback
  began with none moving, and its units' mouths, which their nodes follow,
  were not the recording's. They are recorded with the game engine's state
  now (`G`, below). (`debug.demo_verify` named the field as a unit's
  illumination; it is the mouth's aperture, the field after it.)
- **The game engine's state outside the game state.** A game begun with
  five players or more draws its grenades from a map's places by the
  game engine's flags, which its start set by its players: the playback's
  game began as a game of this machine's, its flags not the recording's,
  and its first tick placed other grenades (Slayer with five or seven bots
  parted at once). They are recorded now (`G`), with the rest of what the
  engine keeps outside the game state.
- **The netcode's and the network test's kills and hits in the frames.**
  Their bytes were recorded, not what their code does outside the game
  state: the kill's score (the game types keep theirs outside the game
  state), and the hit message that shows it ("You killed Player 2 (3)").
  They are events now, done again in the playback by their code.
- **A datum deleted from an array's end** outside the ticks: the
  difference written stopped at the count the array was left with, and the
  playback kept the datum's identifier past it, which `datum_new` takes for
  a datum in use (once, a crash deleting an object whose cluster references
  were apart). A difference now runs as far as the array was in use before;
  and the datums the playback's own frames made past a count put back are
  freed again.
- **The particles' seeds.** Whether a tick makes a particle (seen from the
  camera, room among the particles, held for a later frame:
  `render_interpolation_particle_later`) and how many a new particle system
  makes at once (room among its particles) are the frames', and a particle
  made in a tick took the game's local seed: the game's seed moved with the
  frames. At `game.sim_rate` 60 and 120 a particle and a particle system's
  first particles take the frames' local seed now, as the contrails and the
  decals do. At 30 they take the tick's, as the original's (the scorecard's
  30 Hz gate keeps a build bit for bit as master); there the playback has
  the recording's choice of a particle held for a later frame (`L`), but
  not the room the frames left, so a 30 Hz demo of a fight with the
  particles full can still part in the local seed.
- **What the frames made and deleted again** in a data array (an effect, a
  cluster reference) leaves bytes in its free slots, which no tick reads:
  the demo's hash and `--simulation` leave them out.

A real match (the maintainer's first, on build 1968: a host at 120 Hz with
the renderer on, its own settings, a client at internet latency) parted at
tick 10, and the playback then stopped at tick 40 to 49. Three causes:

- **A setting of the machine changed the game state.** `display.graphics`
  "classic" sets `game.enhanced_animations` false, and the enhancements
  (a seat's reload borrowed, a rider's hands off the grips and the pose in
  a throw) were each machine's own setting in the ticks: a playback with
  other settings posed the players' bodies otherwise ("objects", tick 100
  in a recording made so). They are a rule of the game now, read as its
  map begins: in an OpenH1 network game the host's on every machine (a
  client and its host posed a player otherwise before, and so where its
  shots went and what hit it: OpenH1 protocol version 21), and a demo's
  header holds the recorded game's (`network_standard.c`'s
  `network_standard_enhanced_animations`, with the turning with the aim
  and the way of picking up a second weapon).
- **The antennas.** An antenna's chain is stepped in the ticks, blown by
  the wind (`wind.c`), which the game keeps outside the game state and the
  ticks step with the local seed: a playback began with its own. The wind
  is recorded with what the game keeps outside the game state now (`G`).
  (The chain steps only while the frames draw it, `antenna_render`: those
  are the frames' writes into its datum, which the differences record.)
- **The records of a tick's code** (`D`, the netcode's choice of a client's
  hits; `I`, `L`) were read in order by the code that made them, and a
  playback that had parted made fewer of those calls: a choice left over
  stood where the next sub-step's controls were looked for, and the
  playback stopped ("a D record where the sub-step 3 of 4 was looked
  for"). Every sub-step and every tick is bounded now (`U`, `Z`), and what
  a playback's code left untaken is passed over at the bound (logged once).
  A playback whose game had parted also wrote the recording's HUD over its
  own, whose looping sounds (the frames') it never made, and stopped one
  that was not there: a looping sound gone is left alone now.

Played back in the same conditions (a host recording at 120 Hz with the
renderer on and the maintainer's settings, a client on another machine)
the playbacks parted in three more places, each the frames' state reaching
the tick:

- **The first-person weapons** drew an idle weapon's next pose from the
  tick's local seed, by where the frames had left it (their allocation is
  the frames'), and the wind's walk, which takes the same seed, moved with
  them. At 60 and 120 they take the frames' local seed, as the particles do.
- **An effect's particles** were placed with the tick's local seed, as many
  as its locations had (a first-person weapon's are where the frames posed
  it). At 60 and 120 they take the frames' local seed too.
- **A hit dealt between two sub-steps** (a client's report) points its
  player's damage indicator from that player's camera, which the frames had
  moved since the tick began. The cameras are recorded again before such
  an event and before a later sub-step of an open tick (`O`), where the
  frames moved them, and the playback puts back the recording's there.

`tools/test_demo_files.py` checks the files without the game: a demo
written and read back, a damaged block refused, the retention, and the
ticks' one shape (`demo_info.py --check`) whatever came in which sub-step.
`tools/harness/tests/test_game_rules.py` checks that the enhancements are
the game's rule (the host's on a client, the recording's in a playback),
not the machine's setting.

## Not yet

- **A game joined on another machine** is not recorded: its client decides
  nothing, and much of what it has comes from the host inside its ticks.
- **What else the game's code does outside the ticks** is recorded as the
  bytes it changed, not as an event its code does again: a console
  command, the Host Controls (a kick, a team changed). Where that code also
  changes what the game keeps outside its game state (the game type's
  score), a playback parts. The playback's hash says where; it plays on.
- **Effects the frames make** (a particle striking) are recorded as data,
  and the ticks update them: their particles in the playback are the
  playback's own.
- **A demo recorded with `debug.frame_time`** plays back exactly with it
  (any frame time), not in real time: the sound manager's clock reaches the
  game state ("Determinism" in `port/linux/README.md`), and with the frames'
  time fixed it is the frames' time. A demo recorded in real time plays back
  in real time.
- **The problem report** does not attach the last 30 seconds yet: a demo
  holds the players' names (the game state's), which a public issue must not.
- **A seek** can part from the recording where the game keeps something
  outside its game state that the demo does not record: that is where the
  playback was, not where it goes. What `G` holds is the recording's from
  the seek's tick (before format 4 a seek left it as the playback had it:
  a co-op host's demo parted within ticks of one, by the speakers' mouths,
  and a seek back could respawn the host's player early and crash the
  camera on the unit it then lost). Checked in hosted co-op games: every
  hash after seeks on and back.
- **OpenCE's game types** (Infection, Gun Game: `infection.c`,
  `gun_game.c`) keep their players' roles and levels outside the game state
  too, and the demo does not record them yet: a playback of one parts where
  their code ran outside the ticks.
- The other players' views (the camera follows this machine's player), the
  export to video and the Demos screen come next.
