# Directing the bots

The host of a game with bots can command them from outside the game: by
hand, from a script, or from an AI agent that watches the game and gives
orders. The bots keep their own aim, movement and sight. An order is a goal
("defend the red flag"), and a trait is a way of playing ("aggressive"),
never a puppet's strings. This is good for playing with friends (you command
your bot team), for tests (you set up an exact situation), and for training
bots with a program.

It is the host's alone, and off until the host turns it on.

## Turning it on

- A game on this computer: start it with `halo --bot-control`, or set
  `bots.control = true` (`HALO_BOT_CONTROL=1`). The game listens on
  127.0.0.1 alone, at `bots.control_port` (25190, or the next nine ports if
  that one is taken). It writes the port and a new random token to
  `bot_control.txt` next to `config.toml` each time it opens. Nothing on
  another computer can reach it, and a client of a game cannot use it.
- A dedicated server: the `bots` command is an admin's, like its other
  commands, on its console, the RCON and `POST /api/command`
  (`{"line":"bots order red defend red flag"}`). It uses no second
  connection and needs no second password.

`goath1` is the game's command. It runs the game's executable, and
`halo` and `openh1` still work the same:

- Linux: the release has `bin/goath1` beside `bin/openh1`.
- Windows: the release has `goath1.cmd` beside `halo.exe`. Add that folder
  to your PATH to type `goath1` anywhere (`halo --install-cli` says how).
- macOS, or Linux to put it on the PATH: run the game once with
  `--install-cli`, for example
  `"/Applications/GOAT H1.app/Contents/MacOS/halo" --install-cli`. It writes a
  `goath1` script to `/usr/local/bin` (if it may write there) or to
  `~/.local/bin`, and tells you how to add that folder to the PATH if it is
  not on it yet. `--install-cli <folder>` picks the folder. It never replaces
  a `goath1` it did not write.

`goath1 bots <command>` sends one command and prints the
answer. `--json` prints one JSON object a line. The exit status is 0 when the
command did what it said, 1 when it refused (the reason is printed), and 2
when no game answered.

```sh
goath1 bots status                      # map, mode, score, time
goath1 bots bots                        # each bot: team, place, health, weapon, order, traits, what it does
goath1 bots places                      # the places on this map that orders can name
goath1 bots watch --json                # events, one JSON a line, and the bots every 5 s
goath1 bots help
```

For a dedicated server, add `--http <host:port> --password <admin password>`
(or set `HALO_BOTS_PASSWORD`): `goath1 bots --http 127.0.0.1:8080 status`.

## Orders

`goath1 bots order <who> <order> [target]`

`<who>` is `all`, `red`, `blue` (or `all red`), a bot's name or number
(as `bots` lists them), or several of them joined by commas (`Johnson,Jenkins`).

| Order | What the bots do |
| --- | --- |
| `attack [target]` (`take`, `contest`) | Go to the target, fighting on the way, and hold the ground there. With no target: the enemy base. On the enemy flag: take it, and the game's own play carries it home; if the flag is carried, hunt the enemy carrier (or escort the teammate who has it). On a player: go where their team last saw that player, and prefer that player as a target. If no one on the team has seen that player, the bots search on their own. |
| `push` | Attack the enemy base. |
| `defend [target]` (`guard`) | Keep within about 10 units of the target, fighting from there. With no target: their own flag (in Capture the Flag) or base. |
| `hold [target]` | Stay at the spot, through fights. With no target: where each bot stands. |
| `follow <player>` | Stay with a teammate (`me` is the host's own player). |
| `escort <target>` | Stay with a teammate, or with whoever carries a flag or the ball: by a teammate carrying it, after an enemy carrying it, by it while it lies. `escort flag carrier`, `escort ball carrier`. |
| `return [flag]` | Go to their own flag lying out and touch it to return it; after its carrier while it is carried; done once it is home. |
| `goto <target>` (`go to`, `move`) | Go there, then stay nearby. |
| `retreat` | Fall back to their own base and defend it. |
| `regroup` | Gather at the middle of the team's living players. |
| `clear` (`free`, `play objective`) | Back to their own play, the game's objective with it. |

A target is a place (`red base`, `blue flag`, `middle`, `hill 2`,
`warthog 1`, `rocket launcher`, ...: `bots places`), `our flag`,
`enemy base` and the like (resolved for each bot's team), a player's name,
`me`, or coordinates (`12.5,-80,0.3`). Objectives that move are targets
too: `hill` (King of the Hill's hill in play, wherever it moves: `hold hill`,
`contest hill`), `ball` (Oddball's, lying or carried: `take ball`,
`defend ball carrier`), and the flags where they are now (`red flag`);
`flag carrier` is the enemy carrying the bot's own flag for an attack or a
return, and the teammate carrying the enemy's for an escort or a defense. `flag` and `base` alone mean the
bot's own for `defend`, `hold` and `retreat`, and the enemy's for the others.
The places come from the map itself: its flag stands and starting points
(the bases), its hills, its vehicles and its power item spawns.

An order lasts until another one, a `clear`, or the next map. `order_done`
is sent when the bots get there (they then hold that ground) or when the
player they attacked dies. `order_failed` is sent when the target is gone or
a teammate they followed left.

## Traits

`goath1 bots set <who> <trait> <value>`, and `goath1 bots set <who> reset`.

| Trait | Values |
| --- | --- |
| `skill` (`accuracy`) | `easy`, `normal`, `heroic`, `legendary`, or `default` (`bots.skill`) |
| `aggression` | 0 to 1 (default 0.5). Higher: chases lost enemies longer (3 s at 0 to 13 s at 1) and from 0.8 up never falls back to recharge its shield. Lower: falls back sooner. |
| `teamwork` | 0 to 1 (default 1). How often the bot follows its team's plays (`bots.tactics`: trades, falling back, escorts). |
| `weapon` | A weapon it goes for wherever it lies (`sniper rifle`, `rocket launcher`), or `any`. It is never given one. |
| `vehicles` | `all`, `none`, or some of `warthog,ghost,scorpion,banshee` |
| `style` | `default`, `rusher` (aggression 0.9, teamwork 0.3), `defender` (0.2, 1), `lone` (0.6, 0) |

Traits last until changed, from map to map.

## Bots and teams

`goath1 bots add [n] [red|blue]`, `remove [n]`, `fill <n>` and
`team <who> <red|blue>`. `add` and `remove` change `bots.count` (`fill` sets
`bots.fill_to`) for this run only: `config.toml` is not written. The lobby
takes them at once. A network game takes them a few seconds later, and the
game decides which bots leave (the larger team's first). A local
(split screen) game takes them in its next lobby. `team` moves a bot as a
player's `!switch` does: it dies with no score and spawns on the new team,
following the gametype's team balance rule.

## Fairness and replays

- Each command is checked when it arrives, and its changes (orders, traits,
  team moves) are made at the start of the next game tick, inside the
  simulation, in the order they came. Only the host runs the bots. The
  other machines get the bots' controls as they always do, so a network
  game stays in step, and no message on the network changes.
- A demo records each command where its tick applied it (`W` records:
  `tools/demo_info.py` lists them). Playback uses the recorded controls, so
  the replay matches the game tick for tick.
- A team's coach sees only what that team could know: `--team red` (or the
  red coach's token, below) gives red's bots alone, the enemies red's bots
  saw (where and when they last saw them, never where they are now), the
  other team's orders not at all, and a kill red was not part of without
  where it happened. What the game shows every player stays in: the score,
  the time, the flags' and the ball's markers, the hill, the kill feed.
  Only the bots' sight counts as a team's: a human teammate's view is not
  tracked.
- The bots see only what a player in their place could. An attack on an
  enemy uses what the bot's team has seen of that enemy, and the HUD's
  carrier markers, never the enemy's real position. The host's output (`bots`, `watch`)
  shows everything, because the host already has the whole game state.

## Events

`goath1 bots watch` prints one JSON object a line, for as long as it runs
(`--seconds S` stops it; `--every S` sets how often it prints the bots'
state, 0 for never; `--from-start` prints the events the game still holds
before going on):

```json
{"seq":12,"tick":1840,"event":"kill","killer":"Johnson","killer_team":"blue","victim":"Jenkins","victim_team":"red","victim_bot":true,"weapon":"assault rifle","position":[61.2,-120.4,0.5]}
{"seq":13,"tick":1902,"event":"flag_taken","flag":"red","player":"Mendoza","position":[95.7,-159.4,-0.1]}
{"seq":14,"tick":1910,"event":"command","line":"order red defend red flag","bots":3}
{"seq":15,"tick":1910,"event":"order","bot":"Jenkins","number":2,"order":"defend","target":"red flag"}
{"seq":16,"tick":2200,"event":"order_done","bot":"Polaski","number":6,"order":"attack","target":"blue base","reason":"there"}
{"event":"snapshot","tick":2210,"bots":[{"number":1,"name":"Johnson","team":"blue","alive":true,"position":[40.1,-80.2,0.1],"health":1,"shield":0.6,"weapon":"assault rifle","kills":2,"deaths":1,"order":{"kind":"free","target":"","state":"active"},"traits":{...},"doing":"fighting Jenkins"}]}
```

Events: `kill`, `flag_taken`, `flag_dropped`, `flag_returned`,
`flag_captured`, `ball_taken`, `ball_dropped`, `hill_moved`,
`hill_controlled` (`team`, or `player` in a free-for-all), `hill_contested`,
`hill_free`, `lap`, `command` (a line applied, and to how many bots),
`command_failed`, `order`, `order_done`, `order_failed`, `match_started`,
`match_over`, `match_speed`, and `snapshot`.
`seq` counts up from 0 for the whole run. The game keeps the last 512 events.
`goath1 bots events <after> --json` gets the ones after a number, and the
answer's `next` is the number to ask for next time.

## An agent's loop

Watch, decide, order. A script or an AI agent runs the
same commands a person does:

```sh
halo --bot-control &                     # host a game with bots (Multiplayer > CREATE GAME, bots in the lobby)
goath1 bots places --json                  # the map's places, once per map
goath1 bots watch --json --every 2 | while read -r line; do
  case "$line" in
    *'"event":"flag_taken"'*'"flag":"red"'*)  goath1 bots order red attack red flag ;;   # hunt our flag's carrier
    *'"event":"flag_returned"'*'"flag":"red"'*) goath1 bots order red defend red flag ;;
    *'"event":"snapshot"'*)               : ;;  # decide from where everyone is
  esac
done
```

An agent can also skip the stream and work in turns. Each turn it calls
`goath1 bots status --json` and `goath1 bots bots --json`, chooses orders, sends
them (`goath1 bots order ... --json` replies `{"ok":true,"bots":3,"tick":...}`),
then reads `goath1 bots events <next> --json` to see what happened.

## Team knowledge

`goath1 bots knowledge --json` (one line of JSON) is what each team knows,
for a coach's decisions:

- `objective`: the mode, the score, the score to win and how far each team
  is from it, the time left; and by gametype, Capture the Flag's flags
  (`home`, `carried` or `dropped`, where, the carrier, `return_in` seconds
  for a dropped flag), Oddball's balls (where, the carrier and their team),
  King of the Hill's hill (where, `control`: `red`, `blue`, `contested`,
  `none`, and `move_in` seconds for a moving hill), Race's nav points and
  each player's laps. Slayer has the score and `to_win`.
- `teams`: for each team, its `players` (where, health, shield, each
  weapon's rounds loaded and carried or its battery, frag and plasma
  grenades), its `enemies_seen` (the last sighting of each enemy its bots
  saw: where, `tick`, `seconds_ago`), and its `power_items` (each power
  weapon and power-up spawn: `present` and `back_in` seconds as the team's
  bots last saw it, or `next_turn_in` by the game's clock, which every
  player can time).
- `kills`: the last 16 kills (killer, victim, their teams, the weapon).
  Headshots are not told apart.

`goath1 bots bots --json` gives each bot's `weapons` and `grenades` too.

`--team red` (or `blue`) is a coach's view: `bots`, `places`, `events`,
`watch` and `knowledge` then show what that team knows alone, and orders and
traits go to that team's bots alone (`all` is all of its own). Each time the
game opens bot control it writes `bot_control_red.txt` and
`bot_control_blue.txt` beside `bot_control.txt`, each with a token of its
own: a coach given only its team's file (`goath1 bots --team red`, which reads
it) cannot see more than its team, whatever it asks, and cannot add, remove
or move bots or run the match. `watch --team red` prints `knowledge` as its
snapshot.

## Running a match from the command line

The host runs whole matches, one after another, for a script or an AI agent
that trains its team:

| Command | What it does |
| --- | --- |
| `match start <map> <gametype> [--bots N] [--seed S] [--score N] [--time M]` | Plays that game now: in the lobby at once and started, or the game being played ended and the new one started from its lobby (a dedicated server as `sv_map`, a player's host as Host Controls' change of game). `--bots N` puts N bots on each team; `--seed` is the game's random seed; `--score` and `--time` its score to win and minutes. A game of bots alone plays once `match start` has run. |
| `match restart` | The last `match start` again. |
| `match end` | Ends the match now, with its scores. |
| `match status` | The match's number, `state` (`starting`, `playing`, `over`), map, mode, seed, ticks, score, speed asked and measured. |
| `match speed <x>` | Runs the simulation x times faster than real time (0.1 to 30, `max` for 30): the game's own speed, so each tick plays as it would and only more of them run a frame. Only where no other machine is in the game: a dedicated server, or a host whose game has no one else. It is refused while another machine is in, and drops to 1 (a `match_speed` event) the moment one joins. |
| `match results --json` | The winner, the score, `ticks` and `seconds` of game, `real_seconds` it took, each player's score, kills, deaths, assists, suicides, betrayals, shots and accuracy (`null` when the game counted no shots), and objective statistics (flag grabs, captures and returns; ball and hill seconds; laps), the teams' totals, and `orders`: each line the ticks applied and its tick. Kept until the next match starts. A coach's results list its own orders alone. |
| `match at <tick> <line>` | An `order`, `set` or `team` line applied at that tick of the next match (or this one). |

The same seed and the same lines at the same ticks play the same match:
`goath1 bots match start ... --replay results.json` starts it and sends
each of the results' orders for its tick. `tools/scorecard/bot_match.py
--repeat` plays a Blood Gulch match on a dedicated server, then again from
its results, and checks the two agree (`--mutation seed` is its negative
control). At `match speed 20` a 4-minute match plays in 12 seconds.

A coach's loop, a match at a time:

```sh
halo --server &                                     # or a host's game with --bot-control
goath1 bots --http 127.0.0.1:8080 --password "$PW" match start bloodgulch ctf --bots 4 --seed 7 --score 3
goath1 bots --http 127.0.0.1:8080 --password "$PW" match speed 20
goath1 bots --http 127.0.0.1:8080 --password "$PW" --team red watch --json --every 1 | while read -r line; do
  case "$line" in
    *'"event":"snapshot"'*)                                   # red's knowledge: decide
      goath1 bots --http 127.0.0.1:8080 --password "$PW" --team red order all attack enemy flag ;;
    *'"event":"flag_taken"'*'"flag":"red"'*)
      goath1 bots --http 127.0.0.1:8080 --password "$PW" --team red order all return flag ;;
    *'"event":"match_over"'*) break ;;
  esac
done
goath1 bots --http 127.0.0.1:8080 --password "$PW" match results --json > results.json
```

On a host's own game the same commands run without `--http` and
`--password`, and a coach with `bot_control_red.txt` alone runs
`goath1 bots --team red ...`.
