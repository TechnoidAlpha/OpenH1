# The scorecard

The automatic check for 60 and 120 Hz simulation: it plays each scenario at each rate, headless, measures what
happened in real units (seconds, world units), and compares the rates two by two (30:60, 30:120, 60:120), each
measure within its tolerance. Every change to the simulation rate must pass it, and at 30 Hz a branch must play
exactly as master does (the gate).

| File | What |
| --- | --- |
| `scorecard.py` | The runner: scenarios × rates → measures → the table; `--tags`, `--pairs`, `--mutation-checks`, `--holdouts` |
| `scenarios.py` | The scenarios (data), their tags and the stubs that need retail maps |
| `metrics.py` | The trace read, what each kind of scenario measures, and the tolerances |
| `identity.py` | The 30 Hz gate: bit-identical to master (the determinism log, `tools/determinism_compare.py`); `--repeat`, two runs of this build alike |
| `campaign_identity.py`, `campaign_b30.txt` | The campaign's 30 Hz gate: b30's AI in Classic, its global seed each tick as the original code's (retail maps: `HALO_RETAIL_MAPS`) |
| `ci.py`, `enforced.txt` | CI's step (the linux-x86_64 job), and the tags it enforces at 60 and 120 |

It needs a build with a guest: `ninja linux_x86_64`, `ninja linux_arm64` or `ninja macos` (the one for this
computer; `HALO_STAGEKIT_BUILD` picks another, `HALO_GAME` another executable). StageKit's maps are written by its
generator built as a guest image of that build's host, so all of it runs natively on Linux (x86-64, arm64) and on
the Mac.

## A unit's loop

    ninja macos                                            # or linux_x86_64: incremental, 10-20 s
    python tools/scorecard/scorecard.py --tags U4          # the unit's scenarios at 30, 60 and 120
    python tools/scorecard/identity.py --baseline-game <master build>/halo   # the gate, against master's build
    python tools/scorecard/scorecard.py --mutation-checks  # (the coordinator's) the known bugs are still caught

A run of the scorecard takes a few seconds (StageKit runs ~70,000 ticks a second, side by side on half the cores:
`--jobs`, `SCORECARD_JOBS`): about 5 s for every scenario at one rate, about 10 s for 30, 60 and 120. With the build,
an iteration is under a minute. On a shared computer, run it gently: `nice -n 15 python3 tools/scorecard/scorecard.py
--jobs 3`.
Exit status 0 means every comparison passed; skips (a rate the build does not have, a scenario that needs retail
maps) do not fail it. `--json` writes every measure. `--list` shows the scenarios and their tags (the plan's units,
`U1`-`U20`, and `jump`, `projectile`, `weapon`, `grenade`, `first_shot`, `impact`, `cadence`, `elevator`, `ai`).

Until a unit's work moves into the fast phase (`port/linux/game/sim_rate.h`), what it moves is logged at a
sub-step before the tick's last as the tick left it: today the jumps fail at 60 and 120 (the biped's steps of
a tick, a staircase against a curve) and every other measure agrees, as the game plays as at 30.

The game's settings it uses: `game.sim_rate` (`HALO_SIM_RATE`, `port/linux/game/sim_rate.h`), `debug.trace` (the
per-step log, `port/linux/game/sim_trace.c`), `debug.trace_seconds`, `debug.trace_mutation`, `debug.test_input`
`"script:"` (input on the simulation's clock, in seconds: `"0.5 jump; 1-2 move=0,1; 2 aim=90,10"`),
`debug.fixed_step` and `debug.network_test` `"log"`.

## What is measured, and how

Times are the simulation's seconds from the scenario's start; a shot's time is the start of the step it was fired
in; a strike's is in its update (a sub-step's, or the tick's) by the fraction the projectile code found. Positions are compared at times every rate
has a step at (the 30 Hz grid); a jump's height is fitted (a parabola) rather than sampled. Scripts put their events
on the 30 Hz grid, so the same script means the same at every rate (but the off-grid jumps', below).

| Scenarios | Measures | Tolerance |
| --- | --- | --- |
| `jump_*` | apex height; apex, air and takeoff times; distance | 0.003 wu; 1 ms; 0.5% |
| `jump_off_grid_*` | a jump pressed between the 30 Hz grid's times: its arc from takeoff (apex height, apex and air times, distance), and its takeoff against where its own rate puts it (the end of the step that first reads the press; the tick's end for a press a tick's first step reads), which legitimately differs between the rates | 0.003 wu; 1 ms; 0.5% |
| `first_shot_*` | the shot's distance a 30th, 2, 3, 5 30ths of a second after it is fired (1: its instant range) | 0.1% |
| `impact_*` | where and when each shot strikes | 0.005 wu; a step + 1 ms |
| `cadence_*` | shots in a held trigger (rates of fire 1, 7, 15, 30), and their times | exact; 1 ms |
| `grenade_arcs` | each grenade's first landing and its time, where it lies when it goes off, its fuse | 0.01 wu; 1 ms; 0.25 wu; a step + 1 ms |
| `grenade_thrown` | grenades thrown by hand (StageKit's throw animation lets go at its key frame): as `grenade_arcs`, timed from each grenade's making in the hand | 0.01 wu; 1 ms; 0.25 wu; a step + 1 ms |
| `ledge_fall` | walking off a ledge 2 high: when it goes over the edge and how long it falls (where a fitted parabola tops out and reaches the floor), how far it drops and goes across, its height every 30th of a second | 1 ms; 0.003 wu; 0.5%; 0.01 wu RMS |
| `shield_recharge` | a shield of 75 recharging in 2 seconds, dropped to nothing and to half by the console: the shield every 30th of a second (debug.trace's player lines carry body and shield), when it was lowest and full again | 0.0001 RMS; 1 ms |
| `crouch_*` | a crouch held a second and a tap, standing and in a jump: how far it is crouched and its eye's height every 30th of a second (debug.trace's player lines carry both), when each change starts and ends, and how far the eye above the feet is between the grid's times from the ticks' lines (the view stands where the newest step left it) | 0.0001 wu RMS; 1 ms; 0.001 wu |
| `strafe_*` | strafing one way and the other (A and D in turn, a fifth and a tenth of a second each): the place and the velocity every 30th of a second, how far it went, the most its velocity changes in a step a second (a tick's change made at one sub-step is 2 or 4 times as much), and how far its velocity is between the grid's times from the ticks' lines | 0.001 wu RMS; 0.001 wu/s RMS; 0.1%; 1%; 0.001 wu/s |
| `elevator_ride` | the player's height every 30th of a second on a lift | 0.01 wu RMS |
| `ai_patrol` | an AI's biped walking command lists: its place every 30th of a second, how far it walked, and how far its steps between the grid's times are from the ticks' lines | 0.01 wu RMS; 0.5%; 0.002 wu |
| `elevator_ai_rider*` | an AI's biped walking across a lift as it rises and back as it comes down, updated before the lift or after it (`_late`), riding its floor or carried by it (`_carried`: a biped with a collision model): as `ai_patrol` | 0.01 wu RMS; 0.5%; 0.002 wu |
| `vehicle_*` | StageKit's car, driven: its path every 30th of a second; to its top speed (half and 90% of it, and the top speed itself), a full-lock circle (its radius, its turn), a ramp's jump and a platform's drop (the apex, the suspension's travel after landing), a roll-over (when), a crash into a parked car (when, where that is a second later, the speed after) | 0.1 wu RMS; 2%, 1%; 2%; 0.03 wu; 5/30 s; two ticks, 0.1 wu, 1% |

Needs retail maps (skipped, stubs in `scenarios.py`): every weapon's fire rate, the retail first shots, frag and
plasma grenades, the needler's and rocket's flights, the retail vehicles, melee, AI reaction, script timing, cinematics. Needs
more than StageKit: the VR hands and a network game.

## The gate

    python tools/scorecard/identity.py --baseline-game <master build>/halo

StageKit's scenarios with the scripted bot (walking, jumping, firing), at 30 Hz, logged a line a tick by both
builds and compared: each tick's game state allocation by allocation, the random seeds, the seeds' takers by count.
Script threads are left out (they keep a guest image address, which moves with any code). `--record <folder>` keeps
a build's logs and `--baseline <folder>` compares with them: CI records master's on each master build and compares
every branch with the latest. `--trace` checks that tracing changes nothing; `--mutation jump` must fail.

    python tools/scorecard/identity.py --repeat

The determinism check: the same scenarios played twice by this build, compared whole (every allocation, the seeds,
their takers by call site): two runs of the same input must be the same game, tick for tick. It needs no baseline,
so CI runs it on every branch, with its negative control, `--repeat --mutation unseeded` (a draw of the game's seed
by the host's clock), which must fail.

### The campaign gate

    HALO_RETAIL_MAPS=<maps folder> python tools/scorecard/campaign_identity.py

StageKit has no AI that reacts to a player, so the gate above cannot see a change to what the campaign's AI does.
This one plays b30's beach battle from the level's start at 30 Hz in fast mode (seed 12345, the player standing
still, Classic), and compares its global random seed, which the AI's choices take, with the original code's
(`campaign_b30.txt`, recorded from 40aba0cf9, the build before the sub-steps) for 2,400 ticks: about 10 seconds.
#312's turn in the air (`game.enhanced_animations`, which Classic turns off) parts at tick 1553; `--mutation` plays
with it on, and must fail. CI's step (`ci.py`) runs it where `HALO_RETAIL_MAPS` names the maps, the scorecard
computer, and notes it skipped on GitHub's runners, which have none.

## Anti-gaming

- **Hidden holdouts.** `--holdouts <folder>` (or `SCORECARD_HOLDOUTS`) adds every `*.json` there: a list of
  scenarios with `scenarios.py`'s fields. A parameter given as `[low, high]` (StageKit's: `bullet_speed`,
  `bullet_gravity`, `rifle_rate`, `wall_near`/`_mid`/`_far`, `grenade_speed`, `_gravity`, `_fuse`, `_bounce`,
  `_friction`, `launcher_rate`) or under `"draw"` is drawn with `--seed` (`SCORECARD_SEED`), and the script can name
  it as `{name}`:

      [{"map": "shoot", "metric": "shots", "tags": ["U4"], "params": {"bullet_speed": [20, 200]},
        "draw": {"pitch": [-5, 30]}, "script": "0.4 aim=250,{pitch}; 0.5 fire", "seconds": 1.5,
        "arguments": {"ranges": [1, 2, 4]}}]

  The coordinator keeps the folder outside the repository and the agents' worktrees, and picks the seed. Their
  results print as `holdout #n: ok/FAIL`, never their measures or parameters, unless `--show-holdouts`. In CI, the
  repository secret `SCORECARD_HOLDOUTS_JSON` (one file's contents) adds them; forks' runs do not get it.
- **Known bugs must fail.** `--mutation-checks` plays two 30 Hz runs, the second with a bug put in
  (`debug.trace_mutation`): `head_start` (a projectile a whole tick further as it is made, the Halo 2 kind), `jump`
  (a jump 1.3% faster, 2.6% higher, as an uncorrected 60 Hz jump), `speed` (a projectile 1% slow). Each must make
  its scenarios fail; `--mutation <name>` puts one into any comparison's second run.
- **The consistency triangle.** 60:120 is compared too, which catches tuning to one rate.
- **Ownership.** `tools/scorecard/`, the tolerances, `enforced.txt` and the holdouts are the coordinator's: a unit's
  pull request that touches them is rejected.

## On a Mac without the macOS build: Linux in a container

The scorecard runs natively on the Mac (`ninja macos`). To run CI's Linux build instead, in a Linux container of
the Mac's own processor (arm64, so no emulation), from the repository's root:

    docker run --rm -it -v "$PWD":/src:ro ubuntu:22.04 bash -c '
      apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates \
        git python3 python3-pytest ninja-build cmake make pkg-config clang libx11-dev libxext-dev libxrandr-dev \
        libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev libwayland-dev \
        wayland-protocols libdrm-dev libgbm-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev &&
      mkdir /work && tar -C /src --exclude=./build --exclude=./build.ninja -cf - . | tar -C /work -xf - &&
      cd /work && python3 configure.py && ninja linux_arm64 && python3 tools/scorecard/scorecard.py'

(Docker Desktop, OrbStack or Colima. The container works on a copy, so the Mac's own `build.ninja` and `build/`
are left alone; keep the container, rather than `--rm`, to rebuild incrementally.)
