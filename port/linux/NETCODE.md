# Distributed netcode

The Xbox game plays system link in lockstep: clients send their input to
the host, the host sends every machine every player's input for each 30 Hz
tick, and every machine simulates the whole game from them, waiting for
each tick's update. A client therefore sees its own movement and shots a
full round trip late, and any machine whose simulation differs in the last
bit goes out of sync.

The native builds replace that (they no longer have the lockstep netcode)
with the model of later Halo engines (the "distributed" simulation of the
MonkeyNuts/Ares source)
with ideas from VALORANT's netcode articles, keeping the 30 Hz tick:

- **Every machine ticks on its own clock.** Nobody waits for anybody: a
  client no longer runs only the ticks the host has sent. A client's clock
  starts with the host's first game update and takes the host's time from
  it (at a game's start as at a late join), so every machine reads the
  game's timers alike. In an OpenH1 game it then leads the host's by its
  trip to the host (below, "The client's clock").
- **Own player predicted.** A client drives its own player (and the
  vehicle it drives) from its local input at once. Remote players are
  driven by the inputs the host relays every tick, the latest one held
  until a newer arrives.
- **Actors driven.** Only the host runs the AI. Each tick it sends its
  clients the control its actors gave their units (how they move, where
  they face, aim and look, their trigger and buttons, their animation
  impulses) with the units' state, those near a client's players every
  tick and others less often (in an OpenH1 game, a press of their control
  and an aim at a client's player at once: "Actors to far clients"); a
  client drives each unit with the latest it has, as it drives a remote
  player's, until it hears nothing of it for two seconds
  (`port/linux/game/network_actors.c`).
- **Co-op.** A network game on a campaign level with no game engine
  (Create Game's Map screen, its SINGLEPLAYER maps, over LAN and the
  internet) is co-op. Only the host runs the
  level's scripts and spawns players. `network_coop.c` sends the clients
  everything the scripts do that they would otherwise miss:
  - every tick: the cinematic, camera, screen fade, the HUD settings the
    scripts control (what is shown, the mission timer), the skip vote, and
    which teams are allies and friends (the scripts' allegiances, so the
    marines are the players' allies on every machine);
  - once each, numbered so nothing is applied twice: script sounds,
    chapter titles, help and objective text, "Checkpoint" messages, screen
    shake, nav points, custom animations on units and scenery, and units
    opening and closing (dropships' doors);
  - device groups (doors, elevators, switches; a client sets none itself,
    and its player's use of one is relayed to the host), and each device's
    position and power as it changes: a client puts its device where the
    host's is once that stops, or if they drift apart while it moves;
  - which named objects exist, so scripted creates and deletes match.

  Script actions naming player0 also reach the players beyond player1.
  They use the same reverse datum order as the script's `(players)` list,
  skipping players without units, so a scripted BSP transition does not
  leave the extra players in the unloaded part of the level.

  A script waiting for every player inside a trigger brings the others
  beside a player who arrived, but advances only after their positions
  are inside the volume. Failed placements retry, rather than closing a
  one-way door on players left behind. An empty `(players)` list during
  respawn does not satisfy that wait; empty AI lists keep their original
  meaning.

  Every machine follows the host's structure BSP: a switch is sent at once
  and reliably, and each client's input says which BSP it has loaded.
  Until a client has the host's, the host takes none of its players'
  movement and none of its loading zones, and after any switch no loading
  zone switches again until every machine has the new BSP (ten seconds at
  most). Only the host's crossing of a loading zone switches the BSP, and
  it brings every player to the host, however far behind, so no one
  running ahead or doubling back drags the team through the level (while
  none of the host's players are alive, anyone's crossing does); a client
  standing on a loading zone is told it waits for the host. A player
  outside the loaded BSP and falling for two seconds is brought back beside
  the host, else a teammate. A dead
  player watches a living teammate (`coop_spectate.c`) and comes back
  beside one once it is safe. With everyone dead they come back where they
  were at the last checkpoint, without a revert. A mission the scripts fail
  with players still alive reverts to the last checkpoint on the host, as a
  skipped cutscene does (below); a client never reverts on its own. A level
  won ends the round as in multiplayer, and the next round is the
  campaign's next level.

  Cutscenes are skipped by vote: more than half the machines must press
  skip. The host then reverts as single player does, but keeps its clock
  moving forward (the netcode depends on that) and moves the script
  threads' wake times along with it. The object, device and name syncs
  bring the clients up to date.

  The host's EXTRA ENEMIES (`coop_enemies.c`, `network.coop_enemies_mode`)
  give each squad of enemies a level places more of itself: PER PLAYER, a
  percentage of itself for each player past the first, to 8 times its size
  at most (more filled the clients' objects); STATIC MULTIPLIER,
  that many times itself for any number of players. They stand around its
  starting locations on free ground (the same floor, clear of crates and
  other actors, with room to stand), or where none is left on rings about
  them as before, and never take the actors a level needs for its own (the
  actor pool, `halo_port_capacity.h`, holds 1024). Riders a dropship has no
  seats for are kept, and placed beside its riders once they get out. Only
  the host runs the AI, so the clients see them as the host's other actors.

  Each player chooses whether to play with friendly fire between players
  (`network.coop_own_friendly_fire`, FRIENDLY FIRE in the pause menu): two
  players hurt each other only if both do. A client's input tells the host
  its choice. A player with no input for over 70 seconds (no button pressed
  or let go, no stick or trigger held) is hurt by every player, whatever
  either chose, until their next input.
- **Host authoritative.** The host alone decides damage, deaths, spawns,
  pickups, scores and the game's objects; clients do not decide them but
  apply what the host sends.
- **Corrections.** The host sends each client the authoritative state of
  the players' units and the game's moving objects; a client moves its
  copies toward it, a small error half of the way each tick, a larger one
  at once, drawn gliding from where they were (a player's moved at once
  is on the teleporter it lands on, as a teleporter leaves one it sends,
  so the client's does not send it back). A client's own unit and
  vehicle are only corrected past a tolerance, so prediction does not
  rubber-band: the host tells the client which of the client's ticks it
  has its player at (its prediction come back), and the client compares
  that with where it had the player at that tick, not now, and moves the
  player by the difference, keeping what they did since. The host takes a
  client's player where the client says within a tolerance of its own,
  and no further from an anchor (one it took, a newer once a second) than
  a player moves in the client's ticks since, those no more than its own
  since and a little jitter (so the tolerance and the jitter are not gained
  again each tick); a player on foot moves across and up (and the host
  takes their velocity) no faster than twice as fast as they run and jump,
  or as fast as the host's own ticks sent its copy of them in the last few
  seconds or the flight they began then (an explosion's throw, which the
  client learns of a round trip late), whichever is more; down no faster
  than that and a fall from the highest the host has had them since they
  were on the ground; no more than 2 world units a tick, with a twentieth
  of a world unit a tick more for where they are; and no higher above
  where the host last had them on the ground than a jump takes them (as
  fast up as a jump or a run, the legs drawn up, and a world unit more)
  and a throw its ticks gave them, and in the air no higher than a thing
  thrown up at that speed falls to since (the ticks since the host's copy
  left the ground, less the client's round trip, half a second at most,
  and jitter): past it, the
  host's copy falls as its own ticks have it, so no one hovers, walks on
  air or comes down slowly. What the host's ticks sent its copy is
  its speed less as much as the velocity it took of the client was faster
  than the player goes of their own (and up, what its tick added but a
  jump's), so a client that says it goes faster (a copy said to hover and
  fall, gaining the host's gravity each tick) gains nothing by it. A
  teleporter, which moves the host's own copy too, starts afresh. Riding an
  elevator (moving, or at rest with the player on its floor) the host takes
  only where a client's player is across,
  not how high: the client's elevator follows the host's (co-op's device
  sync), so its player's height reaches the host a round trip behind the
  host's elevator, under its floor going up, where the host's copy would
  fall through it. Each machine's elevator carries its own copy up and
  down, and they meet where it stops. Floor contact keeps that protection
  through a stop or reversal; the remembered attachment alone after leaving
  a stopped platform does not. A client echoing a prediction while still on
  the same elevator keeps its local floor height, rather than applying the
  difference between the platforms' phases to its current height, when that
  difference is within 4 world units (the platform's travel over a round
  trip); a greater one, jumps, leaving the platform and authoritative
  teleports correct normally. Rider carry uses the platform's current
  collision displacement, including phase corrections, rather than its
  previous tick's displacement (network version 30).
- **Shooter's hits.** A client reports what its own players hit; the host
  checks the report (the player's, a weapon they carry, fired from within
  its reach, the target where the host had it when the shooter saw it, no
  faster than weapons fire) and deals the damage. What the shooter saw
  hit, hits. In an OpenH1 game the shooter's machine shows the hit on
  another player at once, as the host will deal it (the shield's flare and
  break, the flinch, the death and the fall), and the host's word confirms
  it a round trip later, announcing the kill, or undoes it (below, "Hits
  shown at once").

A host's game advertisement says that it plays this netcode. A client does
not join a host that does not (one of network version 4 built before the
lockstep netcode was removed, set to play it): it tells the player why.

## Versions

The native builds' network code has a version, an unsigned 16-bit number
(`HALO_PORT_NETWORK_VERSION` in `port/linux/include/halo_port_limits.h`),
raised with any change to what the machines send each other. A host puts it
in its game's advertisement (reserved bytes that hosts built before there
was a version send as zeros, so they are version 0). A client does not join
a host of another version: it shows a message box that says which of the
two is newer, with both versions ("update the game" or "ask the host to
update"), and stays in the list of games. Version 1 was the first of this
netcode; version 2 lets a machine join a game in progress; version 3 puts
each player in its slot of the host's player list on every machine;
version 4 plays the European (PAL) maps as the North American ones
(`port/linux/game/pal_tags.c`); version 5 corrects a client's own player
against where it had them at the tick the host has them at, and checks
what the machines send each other more closely; version 6 sends a unit's
state and a player's input without the parts that are their defaults, fills
each datagram, sends damage and pickups only to the machines they concern,
stamps a hit with the host's tick the client had heard of, and checks hits
and a client's own player's moves more closely; version 7 sends with a
killing blow its killer's score after it, and with a body's state that it
is dead; version 8 is the first whose clients play by the host's rules
(below), so a build without them joins no host of it; version 9 tells
every machine of a player the host dropped for cheating, each client
tells the host its Discord user, and a machine's join request carries its
hardware id; version 10 sends every player's ping for the scoreboard;
version 11 sends with the game's settings its gametype's PC options;
version 12 plays the campaign together (co-op, above), drives the host's
actors on its clients and sends the flinches and deaths the host picked;
version 13 drives up to 1056 of the host's AI units on its clients (co-op's
extra enemies), where 12 drove 288; version 14 sends co-op's device positions
and its units opening and closing; version 15 sends co-op's allegiances with
its presentation, a message of another size; version 16 has a client's input
say which structure BSP it has loaded (co-op); version 17 breaks the host's
glass and destructible scenery on every machine (and takes a client's hits on
scenery), sends the cluster a co-op cutscene keeps active, and leaves a
failed co-op mission's revert to the host; version 18 sends with an object
the bitmap of its shaders it draws with when its actor variant set one (co-op:
the Elite major's and commander's armor); version 19 sends with the game's
settings whether co-op's players collide with each other (Server Setup's PLAYER
COLLISIONS: each machine's players then pass through the others'); version 20
lists a public game with a password with its invite's token sealed with the
password's key (`p2p_lobby.c`), a listing of another layout; version 21
sends each killing blow again reliably and an object come to rest three
times (a client waits for a player's blow before its body dies without one),
and switches co-op's BSP on the host's crossing alone; version 22 names a
Custom Edition map `custom_maps\<name>`, which older clients cannot load or
explain; version 23 keeps the blocks of a Custom Edition map that 22 cut to
the Xbox tools' limits, so both place the same objects; version 24 sends the
gametype's PC vehicle set; version 25 sends a unit's integrated light with
its object state; version 26 has each machine say its WebRTC in internet
play's signalling (a native build's certificate, a browser's ICE credentials
too), so that the web port's browsers played (the web port is removed since,
and a build now says it has none);
version 27
shares Custom Edition maps (below, "Map sharing"): a client asks the host for
a map it lacks, and the game's players reach each other to send it; version
28 holds the players of a machine still downloading the map out of the game
until it has the map, keeps a multiplayer map's first scripted BSP, leaves
dynamic lights and the flashlight on past the Xbox game's player and vehicle
limits, and readies a second weapon picked up while firing; version 29 names
a Custom Edition map by its own name (`custom_maps\hugeass`, which every
screen shows) and says which version it is by the map version of the game
record, its hash's first 32 bits (the header checksum before), which a
client finds its file by (below, "Map sharing"); version 30 carries an
elevator's riders by its movement of the same tick, which the Xbox game did
a tick late, so that every machine moves them alike; version 31 puts the
Xbox game's begin game, graceful exit and pregame keep-alive field lists
under their own messages (its table had each on its neighbour's) and makes
the keep-alive a long, as it was sent: not a byte on the wire moves but the
keep-alive's last two, the sender's stack before and zero now, and the
table's record changed with it; and with it, every machine drops a datagram
of the netcode stamped more than 15 seconds from its sender's latest, the
host takes a client's hits, Discord user and readiness only over its
connection, and the host answers each machine's datagrams where they come
from, so that a second copy of the game on one computer, on a port of its
own, plays too; version 32 sends the host's mouse aim assist policy
(distributed message 90, `network_aim_assist.c`), which a client must not
ignore, and has a client's input say whether its player plays co-op with
friendly fire; version 33 sends with the game's settings whether co-op's
players hurt their AI allies (Server Setup's ALLY FRIENDLY FIRE: the host
then deals the players' hits of them no damage); version 34 takes clients'
hits on, and sends the host's damage to, the map's scenery and devices
whose shield is all damage can break (Rat Race's energy shields, a Custom
Edition map's tripmines), which version 17 left out as having no body to
destroy, and has every machine run those shields' stun and recharge itself;
and the host deals what such an object deals itself (its shield or body
breaking: a tripmine a client's player threw going off under the host's
vehicle), which a client no longer reports and the host took from no one.
One made in the game, a tripmine, is at an index of its own on each machine:
the host takes a client's hit on its copy as one on the host's nearest where
the client had it, and a client the host's damage to one as damage to its own
copy nearest the damage. Then version 35 sends with the game's settings the
options of OpenCE's gametypes (SWAT, Infection, Gun Game and Hardcore:
`port/linux/game/game_modes.c`), which make the gametype's PC options 0x2C
bytes where they were 0x1C, and the game settings record 16 bytes longer; and
in an Infection game the host sends its round in the game type's state
(`struct distributed_infection_state`: each player's role and score, the
round's phase and winner) in place of Slayer's, which each client checks whole
before taking it. Then version 36 sends the host's shield effects (distributed
message 91, `network_shield_effects.c`: the shield glow, its Light Show reach
and the overshield effect, Server Setup's SHIELD EFFECTS), which every client
draws in place of its player's own, so that a build that would draw its own
cannot join. Then version 37 sends with a game's LAN advertisement (the fourth
of its reserved bytes) and internet listing (the first byte of its stamp) the
game's kind: an OpenCE gametype's mode and Hardcore
(`port/linux/game/game_modes.h`'s `game_modes_kind`), which the server
browser names in its gametype column ("SWAT", "HC CTF"); a browser takes a
mode for a Slayer game only, and one it does not know as none. Then version 38
lets the host's shield effects be `players` (SHIELD EFFECTS' PLAYERS' OWN), a
value of distributed message 91 that tells each client to draw its own
settings', not synced. Then version 39 marks a dedicated server's game (a
host with no player of its own, `halo --server`: "Dedicated servers" below) with a flag of
its LAN advertisement (`HALO_PORT_ADVERTISED_DEDICATED_FLAG`, 0x04 of the
third reserved byte) and of its internet listing (32), which the server
browser shows. The listing's flag is its host's word, which any host can
set, so a browser counts a game a dedicated server's on a master server's
word alone, not on that flag.

Version 39 also carries, as OpenCE's does (its pull request #467), three
gametype options in space the earlier builds sent empty, so it was not
raised: Capture the Flag's Kills Score and a capture's points in its
variant's union (`struct ctf_variant`), King of the Hill's Multi-Hill in
its (`struct king_variant`), and Force Team Spawns in the PC options (a
reserved byte of `struct game_variant_options`, still 0x2C bytes). Only the
host spawns players and scores, and a client takes Capture the Flag's team
scores from the game type's state as before. With Multi-Hill on, the host's
King of the Hill state is followed by the other hills
(`struct king_extra_hills_network_state`: their count and their ids, whose
points each machine finds on its map); a game of one hill sends the state
alone, as before. A build without them plays a game of one hill and plain
Capture the Flag with these; a Multi-Hill game's state, longer than it
takes, it does not take.

OpenH1's own games ("OpenH1 60 Tick" and the like, below; Standard games in
the code) have a protocol version of their own,
`HALO_PORT_STANDARD_NETWORK_VERSION` (`halo_port_limits.h`), separate from
the network version, which stays the Legacy games' and OpenCE's. An OpenH1
host advertises it with its top bit set (`HALO_PORT_STANDARD_VERSION_BIT`:
version 1 is 0x8001) where a Legacy host advertises the network version, in
its game's advertisement and its internet listing. A build that knows of
OpenH1 games joins an OpenH1 host of its own protocol version, and tells the
player which is newer otherwise. A build that does not (OpenCE, or OpenH1
before these games) takes the advertised version for a newer one and does
not join. OpenH1 games carry everything Legacy games carry, so the protocol
version is raised with every raise of the network version as well as with
any change of their own: `HALO_PORT_STANDARD_BASE_VERSION` names the network
version it was last raised for, and the build fails while the two differ.
Protocol version 1 (on network version 38) agrees on the game's tick rate
and carries it in the game record, advertisement and listing; version 2
sends each player's sub-steps at 60 and 120 (below, "Sub-step inputs");
version 3 lets OpenCE players into an OpenH1 game (below, "OpenCE players in
an OpenH1 game"); version 4 moves a player to the other team in a game (below,
"The host's controls"); version 5 is the first on network version 39, whose
dedicated server's flag it carries, and changes nothing else of its own;
version 6 sends a client's players' input on to the other OpenH1 clients as
it comes (below, "Forwarded input"), and a client's prediction's velocity at
full precision (below, "Sub-step inputs"); version 7 sends each sub-step's
controls as it runs, and a client's hits at the end of the sub-step that
made them (below, "Sub-steps as they run"); version 8 sends a client's input
as its tick's first sub-step ends and has the host take it, with the
client's prediction of the tick before, as its own tick begins (below,
"Input at its tick"); version 9 sends every OpenH1 client the client's tick
the host has each client's player at (below, "Remote players against their
history"); version 10 sends the variant's flags of a game under way that the
host changed in Host Controls' RULES (distributed message 134,
`_distributed_message_host_rules`, `network_host_rules.c`: shields, the motion
sensor, infinite grenades, invisible players), reliably, to its OpenH1 clients
in a game of no OpenCE machine (below, "The host's controls"); version 11
is the first on network version 41 (below: the join request's Discord pass,
the votes' kinds and no votes, the dedicated server's map vote), and changes
nothing else of its own: an OpenH1 game carries the votes, the map vote and
the AFK kicks as a Legacy game does, its OpenCE machines' too; version 12
has the host tell each OpenH1 client, once a second, how long before its
tick its inputs came (distributed message 135,
`_distributed_message_clock_phase`, `network_clock.c`, reliably), by which
the client leads its clock (below, "The client's clock"); version 13
sends every player's presses as the host's sub-steps run them to the clients
it does not send their every control to (below, "Presses to far clients");
version 14 sends each OpenH1 client the states of the players near its own,
or aimed at, as each of the host's sub-steps but a tick's last ends
(distributed message 136, `_distributed_message_substep_states`,
unreliable; below, "States at the sub-steps"); version 15 gives a dedicated
server's admins Host Controls in the game (distributed messages 137 and 138,
`_distributed_message_admin_down` and `_up`, reliably: below, "Dedicated
servers"); version 16 brings relays for machines whose NATs keep them
apart (the relays' packets on the tunnel's socket, the signalling's relay
offers, a dedicated server's relays told its players: below, "Relays");
version 17 moves the bots' machines to the session's last 64 (113 to 127
before), by which every OpenH1 machine tells a bot's player (below, "Bots
in network games"); version 18 carries the host's way of picking up a
second weapon (`game.auto_pickup`: holding the action button, the Xbox's,
or walking over it, Custom Edition's) in the game record, in bit 3 of
`cooperative_flags` (`_network_game_hold_to_pick_up_bit`), as read in the
lobby with the rate, so that an OpenH1 client's prompts and pickups are the
host's (`network_standard_walk_over_pickup`); an OpenCE machine's copy of
the record leaves it clear, and its players, and a Legacy game's, walk over
weapons as OpenCE's do.
With no new version, each client then tells the
host its build (distributed message 92, `_distributed_message_client_build`,
reliably once a game after its ready, over its connection alone): its
number, configuration, the label it shows ("OpenH1 Linux | build 120
(release)", OpenCE's "OpenCE Linux | build 282 (release)"), the GitHub
repository it is made from (owner/name, which the build scripts take from
CI's `GITHUB_REPOSITORY` or the checkout's origin remote: OpenH1's builds
say `OpenH1/OpenH1`) and the mods it plays, a line each ("folder version",
in the load order); the host keeps it printable and logs it once, and a
dedicated server shows it in `/api/players` (`client`). A host without the
message drops it as a kind it does not know. It is below 120, so an OpenH1
host takes it from OpenCE's clients in a mixed game too.
Distributed message 93 (`network_damage.c`) needs no new version either,
as a host that does not know it drops it: a client whose players' shots
leave from elsewhere than their units' cameras (a VR build, whose weapon
fires from the player's hand) sends the host where from, unreliably when it
changes and every eighth tick: an offset from the camera of no more than 0.4
world units (a player leaning out and an arm). The host keeps it a second,
for that machine's own players only. When none of the shooter's eyes (nor a
vehicle's gun) had a line to a reported hit's target, the host's line of
sight check then also looks from the hand, with the line from the eye to it
clear (a hand through a wall does not hit) and the hand where one firing at
the target can be: no more than 0.05 world units behind the eye along the
line to the target, 0.2 to its side and 0.1 above it (a client cannot claim
a hand far past a corner). Such hits are counted in the host's log, and the
host's `network.fire_origins` (true by default) turns the hand off.
Then version 40 adds to the join request (`message_client_join_game_request`,
0x70 bytes before) a Discord account's pass, 170 bytes, and its proof, 64
(`port/linux/src/player_link.c`; `docs/master_protocol.md`, "Player keys,
links and passes"), all zero for none: a dedicated server refuses a machine
from a VPN's, Tor exit's or datacenter's address with one of five new
rejection codes (`_rejection_code_vpn_link_required` and the four after it,
`port/linux/game/network_link.c`), and a client refused with the first joins
again at once with its pass, bound to the tunnel's session. A build before
them shows the codes as the game being closed.

Then version 41 changes both vote messages' entries (`network_votekick.c`:
the request's first byte, once the player's index, is the vote's kind; the
status's first byte, once the player's index, is the kind, and it carries
the no votes), adds `_rejection_code_address_full` (a host's
`server.max_machines_per_address`), and a dedicated server's map vote
(`network_map_vote.c`), in the lobby too, over the machines' streams as map
sharing's: the host's choices (map and gametype, 8 at most), their votes,
the time left and the winner (`_distributed_message_map_vote`, 94, to each
client reliably every second and as it changes), and a client's choice
(`_distributed_message_map_vote_choice`, 95), taken only from the machine's
own stream, at most four a second, one counted per machine. A co-op
game's flags also tell its clients whether the host is a dedicated server
(`_network_game_cooperative_dedicated_bit`: its loading zones then ask for
most of the living players).

`port/linux/network_format.txt` records the wire format of the current
version (`tools/network_format.py`): `test_linux_port.py` fails when the
sources' messages, kinds, bits, structs sent whole or the sizes of their
arrays differ from it.

A client plays by its host's rules: in another's game (searching for it,
in its lobby, or playing it) the developer console, the telnet console
and the cheat buttons run only commands that change nothing of the game
(what the machine shows and how its controls feel: `hs_compile_and_evaluate`),
and each frame and tick the game's cheats, speed, autoaim, magnetism and
rider ejection are put back to the host's, and what it draws of the world
to what everyone's draws (no wireframe, debug drawing mode, environment
left out, fog, grass or water off), as set before joining too
(`cheats_network_client_enforce`); its camera stays the player's own
(no flying or following camera). The host keeps its own.

The host also finds a client whose game runs faster than time (a speed
hack, which speeds up the machine's own clock, so that nothing on it can
tell): every two seconds it measures how fast the ticks the client's
messages are stamped with go by against its own clock's time, and how far
ahead of its own time they are. A client's clock starts at the host's and
only ever jumps forward to it when behind, so an honest one is never ahead
while going faster (one that caught up, or a host that stalled, is one or
the other, not both). One more than a tenth faster and half a second ahead
has its players' predictions refused at once (the host's copies go as its
own ticks have them). After ten seconds of it, if a message that came over
its connection's stream was that far ahead too, it is dropped, every
machine is told who, in red on its console and in its `debug.txt`, and its
address is kept out of the host's games while the host runs. A datagram is
known to be the machine's only by the address it came from, which another
machine can send one as, so on its datagrams alone it is not dropped: its
players' predictions stay refused while it goes on, and it is logged once,
as unverified (dropped as above if its stream says so later)
(`distributed_note_client_clock`, `network_game_server_kick_machine`,
`_distributed_message_notice`). The host also adds a line to
`cheaters.txt` beside its `debug.txt`: when, the player's address (an
internet play peer's real one), their Discord user and their players'
names, and why (an unverified one says so). The Discord user is marked
`(self-reported)` there and in `bans.txt`. A client tells the host its Discord user as the Discord
client signed in on its machine says (its id and name, none without one:
not running, or internet play off), once it is in the game and again
when it changes; it says what it likes, so the host keeps of it only digits
in the id and letters, digits, "_", "." and "-" in the name, 23 and 39 of
them at most (`p2p_discord_sanitize`), and names it as the player said.
A joining machine tells the host its hardware id: a keyed hash (HMAC-SHA-256,
16 bytes as hex) of what its machine is known by (Windows' SMBIOS UUID, else
its MachineGuid; Linux's `/etc/machine-id`; Android's `ANDROID_ID`, which the
launcher writes to `hardware_id.txt`: `p2p_hardware_id`), kept by the host
as hex only. A player dropped for cheating (on its stream's word, as
above), and one the host bans with the
console's `ban <player name>` (Tab completes the name; the host's alone), is
added to `bans.txt` beside `debug.txt` (a line each, as in `cheaters.txt`,
with `ip=` and `hwid=`): the host refuses a machine joining whose address or
hardware id is in it (a line taken out unbans). Both are as the player's
machine tells them: anyone with administrator or root access can change
them, and players behind one address share it. The console's `kick <player
name>` drops a player as `ban` does (every machine told), but adds no line
and keeps no address out: the player may join again at once. Besides
`bans.txt`, the host keeps out a machine dropped for cheating or banned for
ever while it runs, by its address as it joined (an internet play peer's
stand-in), its real address and its hardware id, any of them; a dedicated
server's ban for a while (`ban <player> 30m`) is left to its `bans.txt`
line, whose `until=` ends it, and its `unban` lets the machine in again by
the line's address or hardware id (`network_game_server_port_forget_kicked_address`).

The players' votes to kick a player and to end the game
(`network_votekick.c`): a client sends `_distributed_message_votekick` (the
vote's kind, the player's absolute index for a kick, and yes or no)
reliably, and the host takes it only from the client's stream. The host
alone counts:
one vote per real address (an internet play peer's endpoint, not its
stand-in), and votes again deduplicated by hardware id, which only ever
removes votes (the players who may vote are counted by address alone, so a
copied hardware id cannot lower the votes needed). A vote needs yes votes
scaled with the players who may vote, the target counted (more than half
up to 8, then 40% and at least 5 up to 32, then 30% and at least 13), or a
fixed percentage of them (`network.votekick_threshold` `static`,
`network.votekick_percent`), at least two, and more yes than no; one the no votes have made lost ends at
once;
starting one needs `network.votekick_minutes` played on this host in its
own ticks, voting two minutes; one vote runs at a time, and a failed vote
cools its starter down and protects its target. A vote that passes kicks as
the `kick` command does, and the host refuses the address and hardware id
for `network.votekick_ban_minutes`. The host sends each client
`_distributed_message_votekick_status` every second while a vote runs (and
as it changes: its kind, yes, no and needed), for the vote's box at the
left of the HUD (F1 votes yes, F2 no) and the scoreboard, whose right click
frees the mouse to pick a player; the host's own scoreboard adds Kick and
Ban. A vote to end the game ("!endgame" in the chat, `network.vote_end_game`)
counts as a kick's with no target, from 30 seconds played and not in a
game's first minute, and ends the game as `sv_end_game` does.

The text chat (`network_chat.c`): a client sends its player's line to the
host (`_distributed_message_chat_up`, reliably), which takes it only from
the client's own stream and from one of that machine's players, at most 5
lines at once and one each 1.5 seconds after (more are dropped; a machine
gone from the game has its count let go, so one new to its slot starts
afresh), its text cut to 63 characters of printable ASCII but `|` (which
the game's text drawing takes for the start of a code; it becomes `?`, on
the host and again on each client). The host alone sends the line on
(`_distributed_message_chat_down`, reliably, with the sender's absolute
index and its channel, the line's flags): to every client, a team's line
only to the machines with a player of the sender's team (to every one in a
game without teams), and a vehicle's line only to those with a player
riding in the sender's vehicle (the outermost object each unit is a child
of, so any map's vehicles), as the host has them; the sender's machine gets
its own line whatever it is. The host shows it itself if one of its own
players hears it or said it. A client shows only
the host's lines, its own too, so every machine shows them in one order,
and takes them (and the chat settings below) only over the host's
connection's stream: a datagram from the host's address could be anyone's.
Each local player's view shows the lines that player hears.
The host's own `/mute <player> [minutes]` and `/unmute <player>` are
commands, not lines: it keeps a mute by the player's name (in ASCII,
matched in either case, as the host keeps names apart) and its minutes by
its own clock, drops the lines of a muted player's machine
when they come up, and sends that machine alone a notice of the minutes
left; it tells every machine of each mute and unmute. A notice is a down
line with `_chat_line_notice_bit` set, its text alone (no sender). A
client's `/mute` is never sent, and the host takes the text of a line that
came up as a line, never a command. The host's scoreboard Mute for everyone
is the same mute (20 minutes), and the host relays none of a muted player's
machine's voice either (`network_voice.c`'s `voice_host_relay`, by the
names of the network game's players, which the lobby has too). A
machine's own scoreboard mute stays its own: it plays none of that
machine's voice and shows none of its players' lines, and tells no one.
The host's chat settings (`network.text_chat`: on, links allowed, or off,
listed in the server browser or not) go to every client in
`_distributed_message_chat_config` (one byte of flags), reliably, as they
change and every 3 seconds. A client without them (from a host without the
chat, or before they come) has the chat off. A public game a player hosts
has the chat as its host's settings say, as a private or LAN game does;
OpenCE, and OpenH1 before it, kept the chat off in any game listed in the
server browser, on the host and on a client that joined it from the browser,
so such a client still shows none (nothing between the machines changed). A
client refuses a line the
settings refuse before sending it, and the host refuses it again whatever
the client knew, telling its machine why: a changed build gets no further.
Links are found in the line's text (`chat_text_has_link`): a scheme, `www.`,
an address of four numbers, or a name and a common top-level domain, its
dots also written `(dot)`, `[dot]`, `{dot}`, `(.)`, `[.]` or ` dot `, with
spaces about them or not, or a dot with spaces both sides; a domain that is
a word as well (`me`, `to`, `is`) counts only with a path, a port or another
domain after it (`chat_link_domain`).
Builds without the chat drop the three kinds as unknown, so the network
version is unchanged.

Voice chat (`network_voice.c`, the sound in `port/linux/src/voice_audio.c`)
travels as distributed messages too, but in the lobby as well as in a game:
`network_voice_handles_message` takes them before the netcode's checks that
a game is running (`network_distributed_handle_message`, and the host's
stream and datagram paths in `network_server_message_handler.c`). A client
sends its 20 ms Opus frames to the host unreliably
(`_distributed_message_voice_up`), each with the key the host gave it over
its stream (`_distributed_message_voice_config`, with the host's settings,
again every ten seconds and when they change), so a datagram sent as from
another machine is dropped; an empty one every two seconds says it hears
voices. The host checks the key, the frame's size against its quality, a
rate of 50 frames a second (a burst of 10), and at most 8 machines talking,
then sends the frame to each machine that may hear it
(`_distributed_message_voice_down`, with that machine's key) by its
`network.voice_mode`, `network.voice_lobby` and `network.voice_proximity`,
or plays it itself. The host never decodes a frame it only relays, and
never sends a machine its own. A listener sets the gain and pan of a voice
heard for being near by its own view of the speaker's unit. Mutes are each
machine's own. Builds without voice chat never say they hear voices, so the
host sends them none, and drop the kinds as unknown.
A speed hack of less than a tenth is let be: the host's bounds on how far
and how fast a client's player moves and fires hold it to the host's time
anyway.

The host's AIM ASSIST option in Player Options defaults to ALLOWED.
BLOCK M+KB overrides optional mouse magnetism on every machine, including
the host. Aiming with the controller or touch retains its existing behavior;
the player's saved preference and the bullets' autoaim are unchanged.
`network_aim_assist.c` sends the policy reliably on client ready and on
change. Clients take it only from the host and default to blocked until it
arrives. This adds distributed message 90; network version 32 requires
matching builds so an older client cannot silently ignore the rule.

## OpenH1 and Legacy games

A host plays one of two kinds of game (`network.game_mode`, read when it
starts hosting; `port/linux/game/network_standard.c`):

- **Legacy**: OpenCE's netcode at network version 41 and 30 ticks a second,
  which OpenCE builds join. A Legacy game is unchanged in every byte: its
  advertisement, listing, join token and game record are as before.
- **OpenH1** (`"openh1"`, the default when hosting; `"standard"`, its first
  name, still works): OpenH1's own, shown to players as "OpenH1 <n> Tick"
  with its tick rate ("OpenH1 60 Tick"). The code calls these Standard
  games. The host picks the tick rate (`network.sim_rate`: 30, 60 or 120,
  the powers of two the sub-steps scale by exactly, `sim_rate_supported`).
  Every machine takes it before the game's first tick, and it holds for the
  whole game.

Server Setup's GAME row picks the kind and the rate: OPENH1 30 TICK, OPENH1
60 TICK (the default), OPENH1 120 TICK or LEGACY. It writes
`network.game_mode` and `network.sim_rate`. The server, made before Server
Setup opens, reads them again when its START GAME is pressed, as long as no
other machine has joined (`network_standard_server_mode_read`). Once a
machine has joined, the kind of game stays as it was.

The server browser and the system link list show each game's tick rate
before its gametype ("60 SLAYER"; a Legacy game's 30), where a long
gametype the column cuts leaves it. The lists sort by it and filter by
kind (KIND: all, OpenH1 only, Legacy only): port/linux/README.md, "Server
browser". The line under the
list names the chosen game's kind: "OpenH1 60 Tick, OpenCE can join" (a
mixed game, below), "OpenH1 60 Tick" or "Legacy".

How OpenH1 machines recognise each other in an OpenH1 game that OpenH1
players alone join (`network.allow_opence` false; by default OpenCE players
join an OpenH1 game too, advertised as below, "OpenCE players in an OpenH1
game"):

- The advertisement's version word carries the OpenH1 protocol version
  (above), and the reserved byte after the game's kind
  (`HALO_PORT_ADVERTISED_RATE_OFFSET`) carries the tick rate. The internet
  listing has the version in its version field and the rate in the second
  byte of its stamp. OpenCE's parsers ignore both bytes, which are zero in
  a Legacy game.
- An OpenCE build sees such a game as a newer version. On the LAN it
  lists the game and refuses to join it ("The host is using a newer version
  of the network code"). Its server browser hides the game, and says
  "games are on a newer version" when nothing else is listed.
  `HALO_PORT_STANDARD_REFUSAL` ("This is an OpenH1 60 Tick game; join a
  Legacy game or get OpenH1", with the game's rate) is the text an OpenCE
  build that knows of OpenH1 games would show for one. No game needs it
  otherwise: everything an OpenH1 game plays, OpenCE players join. A build without that change
  can show only its own messages: join refusals are codes, and host text
  (notices, chat) reaches only a machine already in a game.
- An OpenH1 host accepts a join only with an OpenH1 game's join token
  (`network_standard_join_token`: the token's last two bytes changed by
  "S" and the protocol version). Any other machine is refused with
  `_rejection_code_bad_join_token`, whatever its advertisement check said.
- The host reads `network.sim_rate` while its game is in the lobby. The
  value it last read when the game starts is the game's rate. The game
  record carries it to every machine, including a late joiner, in
  `struct network_game`'s `standard_rate`: a short the Xbox game left
  unused, which a Legacy host never writes. `sim_rate.c`'s
  `sim_rate_network` is the only place where it becomes the rate a tick
  takes: on the host, the rate it took; on a client of an OpenH1 game, the
  rate in the record, if supported (otherwise 30); 30 in a Legacy game.

The host welcomes OpenCE players (`port/linux/game/network_welcome.c`,
`network.opence_welcome`): a few seconds after a client machine has loaded
the game (its client ready), the host sends it two notices of the text
chat, "This server runs OpenH1." and "Get it free at openh1.com for
smoother 60 and 120 tick games.", once a player (their machine's real
address and hardware id) while it hosts. A notice is OpenCE's own
`_distributed_message_chat_down` with `_chat_line_notice_bit`, which
OpenCE's chat shows as a line from no player, so nothing new goes between
the machines. A Legacy game is the same in every byte from OpenCE and
OpenH1 machines, so the host cannot tell them apart: it welcomes every
client machine, in any kind of game, and an OpenH1 machine does not show
the two lines. An OpenCE client in a public game it joined from the server
browser shows no chat at all, so it does not see them either.

### Sub-step inputs

At 60 and 120 a tick runs 2 or 4 sub-steps, and each local player's unit
takes a new reading of the controller in every sub-step after the first: the
held controls (crouch, jump, triggers, grenade), the movement, the trigger's
pull and the facing (`player_control.c`). Every machine replays the other
players from the input it is given, so the sub-steps go between the machines
too (`port/linux/game/network_substeps.c`, protocol version 2):

- A client sends its own players' sub-steps of the tick it has just run
  with its input of that tick, in the same datagram
  (`_distributed_message_player_substeps`, 120, unreliable). The record is
  keyed by the client's tick and carries full precision: what the unit was
  given in each later sub-step (its held controls, throttle and trigger, and
  its facing if its aim turned).
- The host's next tick that takes that input gives the player's unit the
  same controls in the same sub-steps, before the units and bipeds move.
  It takes a record only if the record is of the tick whose input the tick
  took (`update_queues_distributed_input_key`, latched as the tick begins).
  A press in a later sub-step is seen there, as on the client (a jump
  launches at that sub-step's end: `bipeds.c`).
- The host relays, to each client, the sub-steps its tick ran for the
  players whose input it relays that tick
  (`_distributed_message_relayed_substeps`, 121, unreliable, keyed by the
  host's update). The encoding is as compact as the relayed input: angles
  in 16 bits, the sticks of 127, the trigger of 255. Each client replays
  them as the host does.
- A player whose sub-steps changed nothing of what the tick gave the unit
  is not sent. A machine that has no record (none sent, or lost) holds the
  tick's input through the tick's sub-steps, as at 30.

Neither message is sent in a game of one sub-step a tick (a Legacy game, or
an OpenH1 game at 30), and every number in either is checked: the player
(the sender's own, on the host), the count of sub-steps (the game's), and
values finite and within a controller's reach.

Checked with `net_compare` (bloodgulch, the scripted bot, 30 s, each copy
hosting in turn). The host's copy of the client's player moved as the
client moved it but in 0.4% to 4.8% of the ticks at 60, 5.8% to 13% at 120;
with the host's replay turned off (a build made for the test), 33% at 60 and
39% at 120. The ticks apart at 120 were neither lost nor late records (the
host replayed every record a client sent, and ticks without one were as
often apart) nor the relayed encoding (the host's copy is moved by the
client's own full-precision record), but the velocity in the client's
prediction, in 16 bits a part (a 1024th of a world unit a tick): the host
moved the player on from the rounded velocity, which a quarter tick's
sub-step keeps more of than a half tick's. The moves were at most 0.0007
world units apart, which the host's tolerance (it takes a client's player
where it says within 3.5 world units, and a client corrects its own only
past 3) and the others' (a remote player within 0.05 of the host's word is
left as it is) never show. From protocol version 6 an OpenH1 game's client
also sends the velocity at full precision
(`_distributed_unit_part_fine_velocity_bit`, 12 bytes a tick), and the host
moves the player on from it: in the
same games, 0.0% to 0.7% of the ticks apart at 60 and 0.6% to 1.1% at 120
(against 3.5% to 14% and 0% to 12% without, three copies on bloodgulch and
beavercreek). The datagrams' bytes a second (`network test: bandwidth`), with
one player on each machine:

| Rate | Host to client | Client to host |
| --- | --- | --- |
| 30 | 8,246 | 3,330 |
| 60 | 8,403 to 8,415 | 4,706 to 4,858 |
| 120 | 8,535 to 9,294 | 4,706 to 4,858 |

A client's record is 80 bytes whatever the rate. A relayed record is 2
bytes plus 10 for each later sub-step (12 at 60, 32 at 120), sent only for
a player whose sub-steps changed something.

### Forwarded input

In an OpenH1 game the host sends each client's players' input on to its
other OpenH1 clients as it comes (protocol version 6; `network_distributed.c`,
`_distributed_message_forwarded_inputs`, 131, unreliable), not at its next
tick. Before, a client's press waited for the host's next tick (half a tick
on average) and, at 60 and 120, for that tick's last sub-step (most of a
tick more) before the host relayed it, as the tick ran it; the host's own
simulation still takes the input at its tick, as before.

- After reading its datagrams each frame (`network_distributed_frame_received`,
  from `main.c`), the host sends each OpenH1 client, in one datagram, the
  latest input heard that frame of each player its last tick's plan sends
  that client every tick or every second tick (near it, or aimed at:
  "Transport", below), with the sub-steps heard of the same tick, in the
  relayed input's encoding with the client's tick it is of; no sooner than
  8 ms after it last did (`FORWARDED_INPUT_INTERVAL_MILLISECONDS`: a host
  drawing hundreds of frames a second would send each client a datagram
  nearly every frame); a dedicated server reads its datagrams as they come,
  between its frames, and sends them on no sooner than 2 ms after
  ("Dedicated servers"). A player sent
  every second tick is forwarded every second of their client's ticks, and
  at once when their buttons or choices changed in the ticks the input
  carries.
- A client drives that player by the forwarded input (keyed by their
  client's tick, its own record beside the relayed one:
  `player_queues_new.c`'s forwarded actions) while one has come in the last
  4 ticks, and by the host's relayed input otherwise. Each keeps its own
  buttons, taken once; a change of source lets the new one's go (they came
  the other way already).
- The host's tick relays no input it forwarded to that client in the last 2
  ticks of the tick its update ran or later; one of a client gone silent
  (its player stood still at the host's tick) goes as before.
- A client takes no forwarded input as the host's latest tick (the round
  trips are timed by the host's ticks' own messages).
- Nothing changes for an OpenCE machine (it is sent no message of 120 and
  after) or in a Legacy game. An OpenCE player's input is forwarded to the
  OpenH1 clients like any other.
- A player aimed at is in the set every tick of the aim. Until the change
  that made protocol version 14 (a change of the host's alone), the plan
  looked at the aim only on the ticks the player's distance did not send
  them, so a player aimed at from further than 60 world units (sent every
  third or fourth tick by distance) left the set on every third or fourth
  tick of the host's, and was driven by the relayed input on those. The
  plan now takes the aim on every tick for the set; the ticks a player's
  state and relayed input go on are as before (those of the distance and
  those of the aim), so a Legacy game's bytes are unchanged
  (`distributed_aimed_period`; `tools/harness/tests/test_plan_aim.py`).
  There is no hysteresis: a player stays in the set while the aim holds,
  as one near stays while within 60 world units.

Where its unit is is still the host's word, at its ticks: a client's copy of
a player it drives by forwarded input reacts to a change of input sooner,
and is moved toward the host's state of the player (`REMOTE_BLEND_DISTANCE`)
as before. Measured below ("Measuring latency").

### Sub-steps as they run

At 60 and 120 a client's input and sub-steps go to the host as its tick
ends, so a press in a tick's first sub-step waited most of a tick before it
was sent, and another client took the forwarded input only at its own next
tick. From protocol version 7 each machine also sends its own players'
controls as each sub-step runs (`network_substeps.c`'s live controls):

- At the end of each sub-step (`_sim_substep_network`, after the weapons,
  projectiles and items), where a local player's unit's controls changed
  since the sub-step before: the held controls (crouch, jump, the triggers,
  the grenade), the movement, the trigger's pull and where the unit aims,
  keyed by the player's tick (the host's update, for the host's own
  players) and the sub-step. A client sends them to the host at once in a
  datagram of their own (`_distributed_message_player_live_controls`, 132,
  unreliable, 36 bytes a player at full precision), and with them the hits
  the sub-step made (`network_damage_client_tick`, reliably), which went with
  the tick's end before. The last sub-step's go with the tick's own
  messages.
- The host sends them on to its OpenH1 clients as they come, with its own
  players' (`_distributed_message_forwarded_live_controls`, 133,
  unreliable, 19 bytes a player: the sticks of 127, the trigger of 255, the
  aim in 16 bits a part), to those its last tick's plan sends the player
  every tick or every second tick, as the forwarded input (above); every
  player's presses go to the others too (below, "Presses to far clients").
  Its own simulation takes a client's input at its tick, as before.
- A client gives another player's unit the latest it has of them, where it
  is later than the sub-step of the input its tick took (of the same kind
  of key: a client's player's while the forwarded input drives them, the
  host's own player's while the relayed input does): after each tick's
  actions are given (`network_substeps_actions_given`, from `game.c`'s
  macro step, before the objects move) and at each sub-step in place of the
  record's. A press given so is seen at the sub-step it is given in, as a
  replayed one is (`network_substeps_pressed_flags`). Once the input its
  tick takes is later, the input and its record drive the player again.
- A lost datagram costs nothing that does not come again: the next change,
  the tick's input with its buttons of the ticks before, or its record.

None of it is sent at 30, in a Legacy game or to an OpenCE machine.

The prediction's ack and the host's history of where the players were stay a
tick's: the host takes positions once a tick, and a sub-step's pose is
within a tick's movement of a tick's, far inside the hit check's 2 world
units.

### Input at its tick

Until protocol version 8 the host took a client's input and prediction at
its next tick: a client's input of tick t went as its tick ended, with where
its player was after t, and the host's next tick ran that input from its own
copy and put the copy where the client had it as the tick ended. Its copy,
and the states every other client corrects its own copies toward, stood a
tick and a phase's wait behind the client. From version 8, in an OpenH1 game
of several sub-steps a tick (`network_distributed.c`,
`distributed_prediction_at_tick_start`):

- A client sends its input of a tick as the tick's first sub-step ends,
  with the live controls of that sub-step (`network_distributed_client_substep_inputs`),
  not as the tick ends. Its prediction (where its player is after the tick)
  goes as the tick ends, as before, so a prediction of the end of tick t-1
  is followed by the input of tick t a sub-step later.
- The host takes the latest prediction heard as its tick begins, after the
  tick's actions are given and before the objects move
  (`network_distributed_host_actions_given`, from `network_substeps_actions_given`
  in `game.c`'s macro step), and the vehicles' predictions with it
  (`network_objects_apply_vehicle_predictions`). The tick runs the client's
  tick after that prediction (`distributed_host_run_time`): each later
  sub-step gives the player's unit the client's controls of that sub-step
  of it as they came (the latest at or before the sub-step, kept for the
  last 4 ticks: `host_live_apply`), or the tick's record if it came; with
  none of the tick come by the sub-step, the latest of the input taken (the
  tick runs past the input heard where the prediction came before it), else
  it holds the latest. A tick that runs no new client tick (its input late
  or lost) holds the latest controls it has, not the record of the input
  before.
- Where the input of the tick after the latest prediction has not come
  when the host's tick begins (the two machines' ticks less than a sub-step
  apart, once the network's delay is counted, as on a LAN), the controls of
  that tick's sub-steps would each come just after the host's own sub-step
  ran without them. The tick then takes the prediction heard before the
  latest (`distributed_predictions_earlier`), whose input has come, and
  runs that input as the client did, the latest kept for the next tick; in
  that phase the host's copy is as fresh as before version 8 (taking the
  latest, the host's moves of a client's player in bot games on the
  loopback came apart from the client's in 36% to 51% of the ticks).
  That phase is found by the tick's own: where the latest prediction is
  ahead of the input heard, the input heard ran the tick before and none
  before it is kept, the tick takes none (holds, once), so that the next
  prediction finds the latest not yet taken (`distributed_host_take_choice`).
  Without it a game that began in that phase stayed in it: in bloodgulch
  bot games on a steady loopback, the host took the latest ahead of the
  input in 82% of the ticks at 120 and 100% at 60, of which 62% and 68%
  came apart (net_compare failed at 60); holding, it held 2 to 7 ticks a
  game and then took the one before the latest, 0% to 0.6% apart. Where the prediction of the input heard was lost (the one
  kept is older), the tick takes none and runs the input from the host's
  own copy, not from one a tick behind it. The vehicle a player drives
  takes as the player does. `debug.network_test`'s host logs each take's
  ticks and the drift after each (`network test: takes ...`).
- At the end of the tick the host's copy is its own run of the client's
  tick, so its word on the player names the tick after
  (`predicted_time`, one later than the prediction it took). The client
  compares that with where it had its player then, as before; on a network
  quicker than the sub-steps the host can end its run of the tick first, and
  a word on a tick the client has not ended is taken as of its latest
  (`PREDICTION_ECHO_LEAD_TICKS`; the vehicle it drives,
  `OWN_VEHICLE_ECHO_LEAD_TICKS`). The latency meter's round trip is timed
  from when the input went.
- The drift figures compare the host's run of a tick, from the prediction
  it took, with the client's next prediction, as before.
- A client gives another player the latest live control of the input its
  tick took at any later sub-step too, unless that tick's record came: the
  input now comes before the rest of its tick's controls.

The host's checks count host ticks from the take as before (the on-foot
bounds' ticks since the anchor, the ceiling's round trip, the seats'
disagreement, the clock check): each counts from and to the same point, now
the tick's start. The client's elevator phase tolerance (4 world units) is
unchanged: the host's word is a tick fresher, so the phases are nearer.

Nothing changes in a Legacy game, an OpenH1 game at 30, or for an OpenCE
player in a mixed game (the host takes that player's prediction as its tick
ends). Bots have no client input (`update_server_take_bot_action`), and the
host gives them nothing of this. Measured below ("Measuring latency").

### Remote players against their history

A client drives another client's player by its forwarded input and live
controls, ahead of the host's states of that player, and moved its copy
toward the host's state when the two were more than
`REMOTE_CORRECTION_TOLERANCE` (0.05 world units) apart: after a start, a
stop or a jump the copy was pulled back for the ticks the host's state
lagged. From protocol version 9, in an OpenH1 game of several sub-steps a
tick, the host sends every OpenH1 client, in each client player's state,
the client's tick the host has the player at (`predicted_time`, 2 bytes,
which only the player's own client had before). Each client notes where its
copy of each other client's player was as each of its ticks ended, by the
tick of their client whose forwarded input that tick ran (32 ticks:
`distributed_note_remote_positions`), and compares the host's state with
where it had the copy after running the input of the tick named
(`distributed_correct_remote_unit`). Within the tolerance the copy is left
as it is, however far ahead its fresher input took it; further, it is moved
by the difference (its speed by the host's difference of it, its facing its
own), and the ticks noted since with it. A player the relayed input drives,
the host's own players and bots, and a state without the tick are put where
the host has them, as before.

### The client's clock

A client's clock takes the host's time from the host's first game update,
so its ticks begin a trip from the host (and a sub-step) after the host's of
the same number, at a phase against the host's ticks that the network's
delay set for the whole game. The host takes a client's input at its own
tick (as the tick begins: "Input at its tick"), so an input waited on the
host from nothing to a whole tick, by the game: the spread of the 20 ms and
50 ms figures in "Measuring latency". From protocol version 12, in an OpenH1
game at any rate (`port/linux/game/network_clock.c`):

- The host notes when each of its ticks begins, and when each OpenH1
  client's input of each tick comes (its first; a client sends its input of
  a tick as the tick's first sub-step ends, or at 30 as the tick ends: about
  when the tick begins). For each input it times how long before the
  host's tick of the same number began it came (negative: after), and once
  a second (`CLOCK_REPORT_TICKS`) sends the client the second's figure:
  the second earliest of its 30 or so (`CLOCK_REPORT_PERCENTILE`, so that one
  datagram held up does not move the clock), with how many it had
  (`_distributed_message_clock_phase`, 135, 8 bytes, reliably over the
  client's connection, which no one else can send as the host).
- The client moves its clock until the figure is 2 ms
  (`CLOCK_EARLY_TARGET_MICROSECONDS`): its ticks then lead the host's of
  the same number by the trip to the host and that margin, as other
  engines' clients do, and the host's tick of each number takes the input
  of the client's tick of that number, each sub-step's controls coming just
  before the host's sub-step runs. Within 50 ms (`CLOCK_JUMP_MICROSECONDS`)
  it moves by running its ticks up to 5% faster or slower (`CLOCK_SLEW`,
  `game_time.c`'s `game_time_update`), no tick skipped or run twice: under
  1 ms (`CLOCK_EARLY_LOW_MICROSECONDS`: late, or nearly) ahead to the
  target at once, over 5 ms (`CLOCK_EARLY_HIGH_MICROSECONDS`) back by half
  the difference a report (`CLOCK_GAIN`: the next report's second began
  before the move), and between the two not at all. A host reads its
  datagrams once a frame, and cannot tell when in the frame before its read
  an input came: an input read in the frame its tick begins in, just
  before the tick, is timed as the target, however long before the read it
  came; one read in a frame before, from that read. (A dedicated server
  reads them as they come while it waits for its frame, each read counted
  as a frame's: one read so is timed from when it came, and only those
  that came in the last millisecond of the wait are taken for the target.) (Timed from the read
  alone, the clock was pushed a frame early and swung back; on a dedicated
  server of 60 frames a second, so early that the prediction of the
  client's next tick came before the host's tick too, and the host took the
  one before it in about a third of the ticks.) Further behind (a game's start, a join, the round trip grown),
  it skips the nearest whole ticks of the difference at a tick's end
  (`game_time_port_advance`: renumbered, none run, as its clock already
  jumps to the host's when it is a second behind) and runs the rest, under
  half a tick, at once if it is behind, or slowly if ahead. Further ahead
  (the round trip shortened) it slows down; it never goes back, which the
  host would take for old messages (`distributed_add_new_ticks`).
- A dedicated server of an OpenH1 game waits for its frame's time
  (`server_mode_pace_frame`) before it reads its datagrams, not after
  (`main.c`, `network_clock_server_pace_first`): what came while it waited is
  read before the tick its frame runs. Read after, a client's input that
  came in the wait missed the tick, and the clock above, seeing it read
  just after the tick began, held it a frame early or swung across the
  frame (in a server's game on the loopback, the host took the prediction
  before the latest in 1,737 of 2,479 ticks: "Input at its tick"). A Legacy
  game's server keeps its order.
- Its ticks lead the host's latest it has heard by no more than 12
  (`CLOCK_LEAD_LIMIT_TICKS`): the host's clock check drops a client whose
  ticks run fast and more than 15 ahead (`CLIENT_CLOCK_AHEAD_TICKS`). Past
  that (a round trip of about 400 ms) the clock keeps its phase but not its
  numbers: the host takes the latest input heard whatever its number.

A client's game at a given moment is where it was before: the remote
players, the host's units and objects are driven and corrected as they
come, so only the numbers of its ticks move, and its ticks' phase against
the host's. What counts ticks across the machines counts from and to the
same point on each, as before: the host's echo of a client's prediction
names the client's tick, and the client compares it with where it had its
player then; the round trips (the seats' disagreement, the ceiling's lead,
the hit check's) are timed by the host's own ticks or by when messages
went, and are a trip and the margin, not a trip and a phase's wait; the
on-foot bounds' ticks since the anchor take the client's ticks no further
than the host's since and the jitter, so a skip of the client's numbers
gains it nothing; the host's clock check sees a client a tick or so ahead,
not behind; the elevator's phase between the two machines is the trip
there and back and the margin, under the 4 world units' tolerance as
before. With the input there before the host's tick, the phase in which
the host's tick began before a client's input came ("Input at its tick",
`distributed_predictions_earlier`) is rare once the clock has settled (an
input held up past the tick: below, "Measuring latency").

The host reports only to OpenH1 clients of an OpenH1 game, and a client
moves its clock only on the report: a Legacy game, an OpenH1 machine in a
Legacy game and an OpenCE machine in a mixed game keep the clock they had.
`debug.network_test`'s clients log the latest report, the reports, the ticks
skipped and the time moved by (`network test: clock early <us> reports <n>
skipped <n> moved <us> us`). Measured below ("Measuring latency").

### Presses to far clients

Forwarded input and live controls go only to the clients whose players are
near the player, or aim at them ("Forwarded input", above). A client further
off drives the player by the host's relayed input, which goes with the
host's update as its tick ends (at once where the buttons changed, else
every 3, 4 or 6 ticks) and which the client's tick takes as it begins: a
press waited for the rest of the host's tick, the network and the client's
next tick, and a push of the stick (not a button) for the player's period
too. In the latency games (three copies on bloodgulch, the scripted players
standing where they spawned, 20 to 31 ms ±3 each way) the host's player's
press reached a near client a network trip and a sub-step or two after it,
33 to 36 ms at 60, and one more than 60 world units off 2 to 3 ticks later
(48 to 72 ms for the trigger, 53 to 124 for the stick); a client's press
reached another client near it in 63 to 71 ms, and one far off in 104 to
131 ms.

From protocol version 13 the host also sends each player's controls as its
own sub-steps ran them (`ran_live` in `network_substeps.c`, keyed by its
update, the relayed input's own key) to its OpenH1 clients its plan sends
the player less often (or not at all, out of sight), when they make a press
against what it last sent that client (`network_substeps_live_far_due`,
`distributed_far_live`): the held controls changed, a stick moved by a
quarter of its reach, or the trigger by a quarter. A start, a stop, a pull
and a release go as they run; a turn of the aim or a stick's drift does not,
and the relayed input brings it at its period as before. A client gives it
the unit as it gives a near player's live control, until its tick takes a
relayed input as late. A client's player's press goes so as the host's tick
ran it (from their live controls as they came, "Input at its tick"), the
host's own players' and the bots' as they ran; a bot, which has no live
controls of its own, goes so to the near clients too. Each press costs a
client 19 bytes, in the forwarded datagram of that frame or one of its own.
The interval between forwarded datagrams
(`FORWARDED_INPUT_INTERVAL_MILLISECONDS`) counts from the last one sent, not
from a frame that had nothing to send.

The same latency games, three delays at each rate (pooled means in ms, the
worst 95th percentile in brackets), before and after, both before the
client's clock (protocol version 12, which moves the client-to-host path
and so the client-to-client one, not the presses' sends); far is a player the
plan sent the client less often than every second tick:

| Path | 60 | 120 |
| --- | --- | --- |
| Host's press to a client | 56 (166) → 35 (53) | 52 (127) → 32 (46) |
| ... to a far client | 90 (166) → 35 (36) | 81 (127) → 31 (41) |
| Client's press to another client | 83 (164) → 69 (117) | 90 (173) → 73 (104) |
| ... to a far client | 117 (164) → 78 (116) | 107 (161) → 73 (104) |
| Client's press to the host | 36 (51) → 39 (65) | 42 (62) → 41 (63) |

A far client now sees the host's player's press as soon as a near one does.
A client's press reaches a far client a sub-step or so after the host runs
it, where the near ones have it as it comes (two network trips and a frame);
the host's bytes a second to each client were unchanged within the spread
the players' places give (9.8 to 12.1 KB/s in either build).

### States at the sub-steps

A client drives the players near its own by their forwarded input and live
controls, and corrects its copy of each against where it had it at the
tick the host's state names ("Remote players against their history").
Since version 8 the host's copy of a client's player runs that client's
sub-steps as they come ("Input at its tick"), so its state changes at each
sub-step, but went once a tick, as the tick ended. From protocol version 14,
in an OpenH1 game of several sub-steps a tick (`network_distributed.c`,
`network_distributed_host_substep_ended`):

- As each of its sub-steps but a tick's last ends (the last's is the tick's
  own state), the host sends each OpenH1 client the state of each player
  its last tick's plan sends that client every tick or every second tick
  (near it, or aimed at: the players whose forwarded input and live
  controls it is sent), alive and on foot, of an OpenH1 client (whose
  sub-steps the host runs as they come) or the host's own players and bots
  (`distributed_substep_state_due`): `_distributed_message_substep_states`
  (136, unreliable), the sub-step and the count, then each state as the
  tick's are written (35 bytes on foot, 37 with the client's tick), in the
  datagram of the sub-step's live controls where one goes then, else in
  one of its own. A client player's names the client's tick the host's tick
  is running, as the tick's own state will.
- A client notes where its copy of each other client's player was as each
  sub-step of its ticks but the last ended, by the same key as the tick's
  (`distributed_note_remote_positions`), and compares a sub-step's state
  with where it had the copy as the same sub-step of that tick of theirs
  ended (`distributed_correct_remote_unit`): a disagreement is corrected up
  to most of a tick sooner, and the ticks and sub-steps noted since move
  with it. The host's own players and bots, and a player its notes do not
  name, are moved toward the state as a tick's are.
- A client takes only a player on foot, of the unit it has, and not its own;
  a death, a spawn, a seat or the shields wait for the tick's state. It
  takes no sub-step's state older than one it took of the player, or than
  the host's latest tick's states, and a tick's state older than a
  sub-step's state it took does not move the player back (the datagrams
  came out of order).

Each costs a client, at 120, 90 states a second for each player near it (30
at 60), about 3.3 KB/s a player at 120 (1.1 at 60) and 10 bytes a message.
Nothing changes in a Legacy game, at 30, or for an OpenCE machine (sent no
message of 120 and after) or an OpenCE player's state.

### Actors to far clients

Only the host runs the AI. It sends each client its actors' units as it
sends moving objects, by the distance from the client's nearest player
(`network_objects_send_period`): within 25 world units every tick, then
every second, third and fourth tick past 60 and 120, but a unit starting a
one-tick action (a jump, a grenade, a melee, an impulse) to every client at
once. A far enemy's start, stop, burst or turn reached the client up to
three ticks late (100 ms, 50 on average past 120), the distance problem
"Presses to far clients" solved for the players: a sniper's or a Banshee's
shot left from where the client had it aimed some ticks before.

In an OpenH1 game the host also sends an actor's unit, whatever its period,
to:

- every OpenH1 client, the tick its control makes a press against what it
  last sent every client (`actor_state_pressed`): the control flags (the
  trigger's, crouch, ...) or the animation state changed, the trigger or a
  stick moved by a quarter of its reach (the trigger from none, by any),
  or the aim turned by 1/32 of a turn (11 degrees). A steady turn or drift
  adds up to a press and goes then;
- an OpenH1 client every tick while the unit aims at one of its players
  (the line of its aim within 2 world units of them, or 5 degrees), or one
  of them at it (`network_objects_aims_with_machine`): a duel at range is
  seen as the host has it, its aim and its shots.

Nothing changes on the wire: the entries are the ones the host sent, only
sooner, so no protocol version was needed; a client takes them as before.
Nothing changes in a Legacy game or for an OpenCE machine. In a co-op game
on b30 at 120 (the host and one client, 25 ms each way, the scripted player
wandering the beach's fight, 51 seconds), the host sent the client 100.0 KB/s
on average (`network test: bandwidth`), 63.3 KB/s of it actors' states
(`network test: actors to clients`): the presses 1.0 KB/s of those, the aims
1.8 KB/s, so 97.2 KB/s before (2.9% more).

### Remote players drawn from the newest sub-steps

The frames are drawn between the ticks (`render_interpolation.c`): a frame
is the tick's fraction of the way from the end of the tick before the latest
to the end of the latest, a tick (33 ms) behind the clock. At 60 and 120 the
clock runs a tick's sub-steps as their time comes, so the newest simulated
sub-step is at most one sub-step behind the clock, and an object drawn a
tick behind it was 25 to 33 ms behind what the machine's own simulation had
(29 on average) at 120, and 17 to 33 (25) at 60. On a client that is where
its shots meet the other players (its own simulation, against which it
reports its hits: "Shooter's hits"), so a player aimed at what was drawn,
not at where the shot was tested. The player's own view was already drawn
from the newest step (`display.direct_camera`), as is the vehicle they ride.

In a network game (`sim_rate_locked`: not split screen, not a film) at 60
and 120, the objects of the micro lane that are other players' (another
machine's player or a bot, the vehicle a player drives, and the host's
actors) are drawn between their two newest sub-steps instead
(`render_interpolation_lead_wanted`, `render_interpolation_lead_step`): a
sub-step's interpolation, never past a step the simulation ran, 0 to 8 ms
behind it at 120 (4 on average) and 0 to 17 at 60 (8). Only where each is
moves so, and a vehicle's frame (its place and rotation); its pose (its
animation, the aim of its head and its gun) is still blended between the
ticks, as before. Not drawn so: what a local player is or rides (their view
hangs on it), items, anything in a cinematic, and a player standing on a
lift that moves (of the macro lane, drawn a tick behind). Projectiles are
drawn so in every game at 60 and 120 (`render_interpolation_projectile_lead`),
the players' own too, so that another's shot leaves from where that player
is drawn, and meets what it hits as its impact appears: from the frame
after the sub-step that made it, from half a tick of its motion back to
where its head start put it over the tick it holds, then between its two
newest sub-steps; one that strikes in a sub-step is drawn through that
sub-step's frames on to where it struck (`render_interpolation_object_deleted`),
never past it. As an object comes to be drawn so, or no longer (a
lift starts under a player, a local player gets into another's vehicle), it
glides from one to the other over four ticks (`LEAD_GLIDE_TICKS`). Drawing
only: nothing sent or simulated changes, and a Legacy game, an OpenH1 game
at 30, single player and split screen are drawn as before.

### OpenCE players in an OpenH1 game

An OpenH1 game's host takes OpenCE players too (`network.allow_opence`,
on by default: a mixed game; `port/linux/game/network_standard.c`). The
server browser and the LAN list say so ("OpenH1 60 Tick, OpenCE can join"),
and Server Setup's GAME choices. Each player plays
at their own rate: OpenCE players at 30, as in a Legacy game, and OpenH1
players at the game's rate with their sub-step inputs. OpenCE needs no
change:

- The host advertises the network version (38), as a Legacy host does, so
  OpenCE lists and joins the game. The reserved bytes after the game's kind,
  which OpenCE never reads, carry the rate
  (`HALO_PORT_ADVERTISED_RATE_OFFSET`) and the OpenH1 protocol version
  (`HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET`). Its internet listing is so
  too: the network version, which OpenCE's server browser lists, and the
  rate and the protocol version in the second and third bytes of its stamp
  (`p2p_lobby.c`). An OpenH1 build of that protocol joins it as an OpenH1
  game, with the OpenH1 join token, and its lists show "OpenH1 60 Tick,
  OpenCE can join". Any other build (OpenCE, an older or newer OpenH1)
  joins it as a Legacy game, with the Legacy token, plays at 30, and lists
  it as one.
- The host takes either token, and so knows each machine's kind
  (`network_standard_machine_is_opence`, `network_standard_machine_is_openh1`).
  A Legacy host cannot tell them apart: every machine's token is the same.
- An OpenCE machine gets exactly what a Legacy host sends. Its game record has
  no rate (`network_standard_game_for_machine`: `standard_rate` 0), and it
  gets none of OpenH1's own messages (120 and above:
  `network_standard_message_goes_to`, checked where the distributed netcode
  queues a datagram's messages and a reliable one). Everything else in an
  OpenH1 game is network version 41's already.
- Each player is stepped at their own rate on every OpenH1 machine. An
  OpenCE player's biped, and the vehicle they drive, are of the macro lane
  (`objects.h`): they move once a tick, exactly as on the player's own
  machine, so the host's move of the player matches the OpenCE client's
  prediction. Their shots stay on the tick (`weapons_substep` fires ahead
  only for a unit of the micro lane), and their projectiles and grenades
  fly once a tick too, as on their machine. The sub-steps of OpenH1 players
  see them between their ticks' poses, as they see anything of the macro
  lane.
- The host tells its OpenH1 clients which players are OpenCE's
  (`_distributed_message_opence_players`, 122, a bit a player, unreliable,
  when it changes and each second), so that they step them on the tick too.
  Until then, a client steps them as a player whose sub-steps it never
  heard, holding the tick's input, and the host's corrections keep it near.
- The host does not welcome OpenH1 machines it knows by their token
  (`network.opence_welcome`).
- Parties made from the scoreboard (`network_party.c`) use 123 to 125,
  reliably, in OpenH1 games only: a client's word to the host (whether it
  can party and is in one, an invite with the party's token, an ask to join),
  the invite or ask the host sends on to the one machine it is for, and the
  host's list of who can party. Each entry has a format byte of its own. The
  host sends them only to OpenH1 machines that told it they can party, so
  never to an OpenCE machine, and an older OpenH1 drops them as kinds it
  does not know.
- Friends (`port/linux/src/p2p_friends.c`): a client's party status also
  tells its player's friend code (a flag bit, and the code in the status's
  token, which a status left empty before); the host sends the codes on to
  the same machines in `_distributed_message_friend_players` (130, reliably,
  when they change: an entry each, its format byte, the player and the
  code). Older builds send no code, never set the bit, and drop 130 as a
  kind they do not know: nothing they read changed, so the network version
  stays. The friends themselves (requests, presence, invites) go through the
  signalling brokers, never the game's connection.
- The host's controls (`network_host_controls.c`, below) use 126 to move a
  player to the other team, only in an OpenH1 game with no OpenCE machine in
  it.

Shots between players of different rates are resolved as every client's
shots are (above, "Shooter's hits"), and nothing in that reads a rate:

- The shooter's own machine decides what its shot hit, at the shooter's
  rate. A 30 Hz OpenCE machine steps its shot once a tick against its
  targets where its ticks have them: an OpenH1 player as the host's relayed
  input and corrections move them, a tick at a time. A 60 or 120 Hz OpenH1
  machine steps its shot in the sub-steps against its targets where each
  sub-step sees them: another OpenH1 player as their sub-steps move them,
  an OpenCE player (of the macro lane) between where their last two ticks
  left them. The machine reports the hit, with where it saw the target and
  the host's tick it had heard of.
- The host takes the report if the target is within 2 world units, and a
  few ticks of how fast it went (`REPORT_HISTORY_TOLERANCE`,
  `REPORT_HISTORY_LEAD_TICKS`), of where the host had it at one of its ticks
  as far back as the report was made. The host keeps a tick's poses, not a
  sub-step's: a sub-step's pose is less than a tick's movement from a tick's,
  far inside that reach, so a 120 Hz shooter's hit between two ticks and a
  30 Hz shooter's hit at a tick are taken alike, and a target where the host
  never had it is refused whatever the shooter's rate
  (`tools/harness/tests/test_cross_rate_hits.py`). The other checks (the
  weapon's rate of fire, its reach from where the host had the shooter) are
  in ticks too, and a shot fired at a sub-step keeps the 30 Hz cadence
  (`weapon_fire_ahead`).
- The host's own player's shots, at the host's rate, hit what the host's
  sub-steps see, as any OpenH1 machine's do, and the host deals them
  without a report.

So each player's shot hits what that player saw, at their own rate, and the
host holds every shooter to the same reach.

A prediction a client sends of its player is taken as the tick after it
was heard ends, and with it the client's input that the tick took. At 60
and 120 a tick's sub-steps may be spread over several frames
(`sim_rate.h`). A prediction heard between them waits for the next tick, as
an input heard then does (`network_distributed_tick_begun`). Taken at the
open tick's end, it put the host's copy of the player where the client was
a tick after the input the host had moved it with. An OpenCE machine sends
as its 30 Hz tick ends, which is in the middle of a 120 Hz host's tick, so
its player was moved so in most ticks.

`debug.join_legacy` makes an OpenH1 build join every game as OpenCE joins it,
the tests' stand-in for an OpenCE machine: the network test logs each
second, on every machine, the rate it plays at, the players it steps as
OpenCE's, and, on a client that joined as OpenCE does, how many of OpenH1's
messages it heard and the rate its record brought, both 0 from a mixed
host, and, the host, each client's player's drift (below, "Comparing two
builds"): `network test: rate <rate> opence players <players> | legacy
heard openh1 <messages> record <rate> | drift by player <player>:<apart>/<compared>...`.
An OpenCE player's drift is that of a Legacy game (none apart but where
the player touched something), as the host moves them a tick at a time as
their machine does. `tools/net_compare.py --c` plays a third copy, a second
client, and `--opence` names the copies that join as OpenCE: it checks
those lines too, and with `--events` that each client reported hits and the
host rejected none (`debug.network_test_shoot`: the OpenCE player shoots an
OpenH1 player, an OpenH1 player the OpenCE one).

### The host's controls

The host's HOST CONTROLS (its pause menu, the scoreboard's player menu, the
console's `host_controls`: `port/linux/game/network_host_controls.c`) use the
host's own authority and send nothing new to a Legacy game's or an OpenCE
machine:

- Kick and Ban are the console's `kick` and `ban` (the machine dropped, a
  ban's line in `bans.txt`). Mute for everyone is the text chat's host mute.
  End game now ends the game as its time limit does; Restart round ends it,
  presses the scores' A for the host (every machine to the lobby, as
  `network_game_server_reset_to_pregame` sends it) and starts the lobby's
  countdown on the same map and gametype.
- A change of map and gametype, the host's playlist (`network_host_game.c`)
  and the score and time limits are the lobby's own messages (the game's
  record with its map and variant, as Server Setup sends them): a change
  asked for now ends the game as Restart round does and sets the new one in
  the lobby; one after this game, or the playlist's next, waits for the
  scores and the host's A, then is set in the lobby and its countdown
  started. The limits change only in the lobby (no machine of the Xbox's
  game changes them in a game), so during a game they wait for the next.
  The lobby's team changes (Host Controls in the lobby, Shuffle and Balance
  teams there) are the host's game record sent again, which every machine
  takes, OpenCE's too; in a game, Shuffle and Balance are team moves (below)
  and offered only where those are. The host's announcements and the next
  game are the notice (`_distributed_message_notice`) the dedicated server's
  `say` sends, which OpenCE machines show; in the lobby no machine hears a
  notice, so there are none. None of it needed a new message or a new
  network version.
- Lock teams refuses, on the host, a client's change of its players' team in
  the lobby (the player settings it sends): the client sees its team stay. Max
  players, the listing and Allow OpenCE players change only what the host
  takes and advertises from then on (an OpenH1 game closed to OpenCE's joins
  advertises as an OpenH1 game again, and stays mixed for those in it).
- Move to other team has no message in the Xbox's game, which changes teams
  only in the lobby. The host kills the player's living unit without
  statistics (as a player who quits; every machine sees the death as any
  other), sets the team on its player and game records, and tells each
  OpenH1 client (`_distributed_message_host_team`, 126, reliably: the player
  and the team, with a format byte), which takes it only from the host's
  stream in an OpenH1 game of teams. It is offered only in an OpenH1 game with
  no OpenCE machine in it, whose machines all play the host's protocol (their
  join token says so): an OpenCE machine would keep the player on the old
  team. The player respawns on the new team; their score goes with them.
- RULES (`network_host_rules.c`) changes the gametype's rules. In the lobby
  it is the lobby's game record, which every machine takes. In a game it
  changes the host's game and record (a player who joins takes the record).
  The vehicles and starting weapons are the host's alone, in any game,
  Legacy too: for a new vehicle set the host deletes every vehicle nobody
  rides and makes the new set's at the map's vehicle placements
  (`game_engine_vehicle_set_changed`), and every machine, OpenCE's too, sees
  the host's objects come and go as it sees a respawned vehicle's; a ridden
  vehicle stays until it is empty, and a new one whose pad it holds waits
  for the pad. Starting weapons, the loadout and the weapon set take effect
  at each player's next spawn, which only the host makes. The variant's flags
  every machine draws by (shields, the motion sensor, infinite grenades,
  invisible players) go to each OpenH1 client (`_distributed_message_host_rules`,
  134, reliably: a format byte and the flags, which a client takes only from
  the host's stream in an OpenH1 game under way, masked to those four). That
  is OpenH1 protocol version 10. They change in a game only when no OpenCE
  machine is in it, as a team move does; otherwise, and in a Legacy game,
  they wait for the next game's lobby. A Legacy game is sent nothing new.

### Bots in network games

A host plays the bots of its system link and internet games
(`port/linux/game/bots.c`, `bots.count`; README.md, "Bots"), and nothing
new goes between the machines:

- Each bot is the one player of a machine of its own, a machine index from
  `BOTS_FIRST_MACHINE` (64) up that no real machine takes: the host numbers
  the machines that join below it (64 machines, each with up to four
  players: more than the session's 128 players). A game takes up to 64 bots,
  as far as its most players leave room. Every OpenH1 machine tags a bot's
  player by its machine (the lobby's and the scores' BOT), so the range is
  part of the Standard version (17; 113 to 127 before, with 15 bots at most). The machine has no connection: the host
  plays its player, as it plays a remote player whose input comes at once.
- The bots join the lobby as players do, in the game's settings record, and
  a game in progress as a late joiner's players do
  (`_message_server_add_player_ingame`); one leaves as a machine's player
  who quits (`_message_server_remove_player_ingame`). Every machine, an
  OpenCE one too, sees an ordinary player of an ordinary machine.
- The host's tick takes each bot's controls from `bots.c` in place of what
  its queue held, and puts them in the update the tick ran
  (`update_server_take_bot_action`), which is what the host relays of every
  player (`_distributed_message_relayed_actions`). A client replays a bot
  from that input and the host's corrections, as any remote player. A bot
  holds its tick's controls through the tick's sub-steps (an OpenH1 game at
  60 or 120), as a player whose sub-steps changed nothing: the host sends no
  sub-steps of it, and every OpenH1 machine steps it alike. The host deals a
  bot's damage itself, as a player of no client.
- The advertisement's and the listing's players are the people (the game's
  maximum players theirs: with `bots.make_room` off the record's most
  players are theirs and the bots'), and the bots go in a reserved byte
  OpenCE does not read: the advertisement's
  `HALO_PORT_ADVERTISED_BOTS_OFFSET`, the listing's fourth stamp byte. A
  game without bots sends them as zero, as before.

Checked on one Mac (an internet game through a local broker, the host and
a client each a hidden copy, 4 bots, 60 seconds): `net_compare --compare`
passes at 120 and in a Legacy game, the client logging the bots as the
host's players (machines 113 to 116, the bots' range then), their positions 0.03 world units
apart on average.

### Parties in public games

A party's leader joins a game first and its members follow it in
(`port/linux/game/party_menu.inc`), so the host holds room for them meanwhile
(`port/linux/game/network_party.c`):

- The leader, a client of an OpenH1 game, asks its host to hold room for its
  members (`_distributed_message_party_hold`, 128, reliably, over its own
  stream): an entry for each, with a format byte and the hash of its tunnel
  key (`p2p_party_member_key_hashes`: the X25519 form of its party key, the
  key it reaches a host with, which the host reads with `p2p_peer_key_hash`).
  It asks again when its members change. The host holds as many as it has
  room for (slots free of players and of other parties' holds, the bots
  apart: a bot leaves for a member who comes, as for any player, and with
  `bots.make_room` fewer bots join while slots are held) for 20 seconds,
  and answers how many
  (`_distributed_message_party_held`, 129). A host leading a party holds
  room for its own members the same way. A machine is granted holds at most
  3 times a game; each grant replaces the last. A leader that leaves lets its
  holds go.
- A held slot is its member's alone: anyone else's join, and any other
  machine's player, is refused as for a full game until the member's player
  is in or the hold lapses. The advertisement and the server browser's
  listing count held slots as players, so browsers show the room really left.
  A member that finds its leader's OpenH1 game looking full joins anyway,
  and the host decides. In a team game the host puts a party's player on the
  team most of its party is on, if that team has half the game's players or
  fewer; the lobby's auto team balance moves a player of no party first.
- Matchmaking (`port/linux/game/matchmaking.c`) adds nothing to the game's
  messages: a host lists the playlist it hosts for in the listing's stamp
  (its sixth byte, the playlist's number; an OpenH1 game's alone), and a
  party picks its host among its members by a byte a member adds to its
  STATE after its name (how well it can host now: unable, strict NAT, open;
  a leader's STATE has none, and older builds read no further than the
  name) and a HOST message (`'H'`, the member's id and the playlist) that
  the member takes by taking the lead (`p2p_party.c`). An older build
  passes over the type and never sends the byte, so it is never asked.
- The listing's stamp says the host holds room (its fifth byte, 1). A
  Legacy host, and an older OpenH1 one (which drops 128 as a kind it does not
  know), hold nothing: the browser says there is no room guarantee and the
  members join as they can. Nothing goes to an OpenCE machine. The OpenH1
  protocol version is unchanged: the new kinds are only asked for, and an
  older host ignores them.

## Joining a game in progress

A game stays open when it starts (on the Xbox it closed, since every
machine had to simulate it from its first tick), and the game list shows
it. A machine that joins it is accepted as in the
pregame, and its players are added as the game adds a player in game:
every machine in the game spawns them, told by the game's own
`_message_server_add_player_ingame`. Then the host sends that machine alone
the game's settings and its start, with the host's game time (the start
message carries 16 bits of it); the machine loads the game, sets its clock
to that time and the ticks it spent loading, and takes the rest of the time
from the first game update if it is ahead (so that the game's timers read
as the host's). It takes up the host's count of updates where it is. When it has loaded, the distributed netcode
gives it the host's objects (network_objects.c), every player's statistics
and the game type's state, and it plays on as any other client.

The netcode names a player by its datum's index, which must be the same on
every machine, the one that joined too. That machine has not the players
who left (their datums stay until the game ends), nor the order in which
the others added players. So each player's datum is
its slot in the host's player list: every machine makes it there, and the
host gives a player added to the game in progress a slot whose datum is
free (`network_game_manager.c`). A player added to the game in progress
also gets its team and the game type's data, as the players at the start
do (in free for all, a team of its own).

The game is closed to joins while its machines load and after it ends.
A machine must join within 10 seconds of connecting; in a game, the host
drops a machine it has heard nothing from for 15 seconds
(`network.client_timeout_seconds`), and one that
joined the game in progress and has not loaded in two minutes.

Until it has loaded, the machine hears none of the game's messages (which a
machine in the pregame refuses, and which the others no longer need), only
a pregame keep-alive every five seconds from the host
(`network_server_manager.c`, `network_server_message_handler.c`).

## Dedicated servers

A dedicated server (`halo --server`: `docs/dedicated_server.md`) is a host
as any other, with these differences, all on the host's side:

- **No player of its own.** The host machine is in the game as machine 0
  but has no player, so its lobby counts the joined machines' players only
  (`network_server_manager.c`'s `server_ok_to_countdown`, which asks
  `server_director_ok_to_countdown` for `server.min_players` and
  `server.team_start`), and a game with one team's players may start (the
  next players to join balance the teams). The host waits for no player of
  its own to precache, and a client's player that quits the game does not
  end the host's (`network_client_manager.c`: only a local player's quit
  leaves).
- **No ticks of its own player.** The host's own action queue may hold no
  action (`player_queues_new.c`), as a game with no local player has none.
- **The kind of game.** A dedicated server hosts Legacy games unless its
  `server.game_mode` says `openh1_60` or `openh1_120` (`openh1_30` too),
  in place of a player host's `network.game_mode` and `network.sim_rate`
  (`network_standard.c`); `network.allow_opence` is read as a player
  host's. Its OpenH1 game is any host's: the advertisement, the listing,
  the join tokens, the game record's rate, the sub-steps, forwarded input
  and live controls, the predictions taken as its ticks begin, mixed
  games. With no player of its own it sends no sub-steps or live controls
  of its own; everything its ticks run of its clients' players is theirs.
  The director takes the setting again in the lobby while no other
  machine is in it (`network_standard_server_mode_read`), and frames an
  OpenH1 game at least as often as its tick rate. While it waits for its
  frame's time it waits on its game's socket, and reads the datagrams that
  come as they come (as its frame's read does, and nothing else of the
  frame: `network_distributed_server_receive`, from
  `server_mode_pace_frame_receiving`), sending their input on to the other
  clients at once, no sooner than 2 ms after the last
  (`FORWARDED_INPUT_SERVER_INTERVAL_MILLISECONDS`), not at its next frame;
  the last millisecond before the frame's time is a precise sleep, and the
  frame's read takes what came in it. Nothing on the wire is
  new: a dedicated server's OpenH1 game is a player host's, but for the
  dedicated flag every dedicated server sends.

  Checked by `tools/test_dedicated_server.py` (bloodgulch, two headless
  clients with the scripted bot, a minute; the server logs each second with
  `debug.network_test` `log`, which a server keeps, and is judged with
  `net_compare.py`'s checks): at 60 and 120 both OpenH1 clients in step, the
  server's drift of each one's player 0 of 1800 ticks, its takes 2 apart of
  3880 at 60 and of 3893 at 120. In a mixed game at 120 with an OpenCE-mode
  client (`debug.join_legacy`), the OpenCE client at 30 hearing none of
  OpenH1's messages: without scripted hits, each player's drift 0 of 1800
  ticks; with a hit every 8 seconds, every hit both ways taken, and drift of
  9% to 10% and 2% to 3% in two runs (the players brought together to
  shoot, and touching: a player host's game with a hit every 4 seconds had
  4% and 10%). OpenCE's
  own release (build 335) as one client and OpenH1 as the other, without
  scripted hits: both in step with a Legacy server and with an OpenH1 120
  Tick one, 0 of 4800 ticks apart.
- **The director.** `port/linux/game/server_director.c` drives the lobby as
  a player at the pregame screen would: it sets the map and gametype of the
  rotation's entry, starts the countdown once the server's own rule allows
  it (`network_game_server_port_ok_to_countdown`, the rule the server checks
  each tick, then `network_game_server_port_countdown_begin`), and after the postgame
  goes back to the lobby with the next entry. The clients see a lobby, a
  game and a postgame as with any host.
- **Shutdown.** On SIGTERM, a game in progress goes to its postgame, and
  the host tells every machine it goes (`network_game_server_graceful_shutdown`)
  before it exits, so that the clients show the scores rather than a lost
  connection.
- **The flags.** Since version 39 a dedicated server's LAN advertisement
  carries `HALO_PORT_ADVERTISED_DEDICATED_FLAG` and its internet listing the
  dedicated flag (32); a listing published to the master servers is the
  same signed listing (`docs/master_protocol.md`), and the master's probe
  is answered on the tunnel's socket (`p2p.c`, `server_master_probe`).
- **Admins in the game.** From OpenH1 protocol version 15 a dedicated
  server's admins (`port/linux/game/network_admin.c`;
  `docs/dedicated_server.md`, "Admins in the game") have Host Controls. The
  server sends each OpenH1 machine of its game, as it joins, its id (32
  bytes, `admin_id.txt`) and a nonce of 32 random bytes
  (`_distributed_message_admin_down`, 137, its challenge); the machine
  signs `openh1-admin-proof-1`, the nonce and its connection's binding (the
  internet play tunnel's session secret as a Discord pass's proof is bound,
  32 zero bytes on a LAN: `player_link.c`) with a key for that server id
  alone (its Ed25519 seed HMAC-SHA-256 of `openh1-admin-key-1` and the id,
  keyed with the machine's own `admin.key`, not the player key), and
  answers with the key and the signature (`_distributed_message_admin_up`,
  138): two servers see two unrelated keys of one player. One answer is taken a
  nonce; a key is never taken on the machine's word. A machine that proved
  a key listed in `admins.txt` or `server.admins` is an admin: the server
  tells it the game once a second (its flags, the entry played and the
  next, then its players by slot, 32 a message) and the lists it asks for
  (the maps, gametypes, rotation and bans, in pages of 3000 bytes). An
  admin's request is a verb, a slot with the name its list had there, a
  number and a text (156 bytes), taken only from the machine's own stream,
  from a machine that proved an admin's key, 4 a second at most (bursts of
  8); each field is checked (`network_admin_request_line`: the slot must
  hold that name now, numbers in range, an entry one word of printable
  ASCII) and the request is run as the server's own command at the admin
  role (`kick`, `ban`, `mute`, `sv_team`, `sv_map`, `sv_map_after`,
  `sv_rotation_add`...), its answer sent back. Each message carries a
  format byte (1) and a kind; a size that is not its kind's is dropped. A
  player host sends neither message, and an OpenCE machine is sent none.
  Checked by `tools/harness/tests/test_network_admin.py` (the keys listed,
  each request's command, those refused, the rate),
  `test_player_link.py`'s `admin-proof` (the signature taken for its key,
  nonce and binding alone; two servers' ids, two keys), and `tools/test_dedicated_server.py`'s
  `test_an_admin_changes_the_map_from_the_game`.
- **The timeout.** `network.client_timeout_seconds` (15 seconds by
  default) sets how long a host waits for a silent machine, and two more
  waits with it: how long a stream's writes may stall before its connection
  is dropped (`network_connection.c`), on every machine, clients too; and
  how long a host in a game rides out its network going (an address
  renewed, a wifi drop: `network_game_server_network_lost`). So it is not
  the host's alone, and not a dedicated server's only: each machine reads
  its own setting, and nothing on the wire changes (the machines need not
  agree on it).

## Map sharing

A client may join a game on a Custom Edition map it does not have
(`port/linux/game/map_share.c`, `map_share_files.c`). A game's level name is
the map's own (`custom_maps\hugeass`), which every screen and the server
browser show, and the game record's map version is the first 32 bits of the
map's hash (`map_share_hash_version`), which says which version of the map it
is: a client plays its file of that name and version, so two versions never
meet. A file's hash is BLAKE2b's (32 bytes) of a label, its size and the
BLAKE2b hashes of its pieces (256 KiB each); a map is hashed when it is
needed (a host's game is put on it, a client's settings name its name) and
kept for the run. No map is renamed. A map downloaded beside another version
of its name is named for its hash (`hugeass_0e2b3ee4.map`), and the menus
show it as `Hugeass (2)`; a map an earlier build named so, with no map of
its name alone beside it, has its name given back as the map lists find it.

- **Asking.** A client whose settings name a map it lacks, or lacks of
  their version (`cache_files_map_present`), or one whose resource maps it
  lacks, asks the host over its connection
  (`_distributed_message_map_share_request`: the level, and which of the
  map, bitmaps.map, sounds.map and loc.map it lacks) instead of leaving, if
  `network.map_download` lets it. Its players wait to join until the host's settings
  have named the map, and for a game in progress until it is downloaded; in
  the lobby they join at once. A host that starts while a machine is still
  downloading starts the others without it: the machine's players are held
  out of the game, and so is any player it asks for meanwhile (a machine
  whose download began in the lobby does not know the game has started),
  and the machine joins the game in progress once the map is there. A
  client told to start before its download is done (by a host of an earlier
  build) puts the start off until the map is there, rather than loading a
  map it does not have yet (the damaged disc error, which stops the game).
- **The offer.** A host that shares maps (`network.map_share`) hashes the
  map, and the resource maps the map needs (its tag index says which: a tag
  whose entry's word at 0x18 is set is held whole by bitmaps.map for a
  bitmap, sounds.map for a sound, loc.map for a font, string list or HUD
  message text) that the client lacks, on a thread of their own, and
  answers with each file's size and hash (`_offer`, or why it does not:
  sharing off, or not the game's map), and which of its other machines may
  have them (`_sources`: an address on a LAN, internet play's key hash on the
  internet), again every ten seconds. A map that is not of the game's version is
  not offered. The client checks that the map is of the game's version, and
  that no file is larger than the cache loader takes, and refuses a map
  name Windows takes for a device (`CON`, `NUL`, `COM1` and the like,
  before the first dot). It connects to a LAN machine the host names only
  at an address of the host's kind (a private network's if the host's is,
  loopback only if the host's is; never 0.0.0.0, multicast or internet
  play's), so that a host cannot send its players anywhere.
- **The swarm.** The client connects to the host and up to eight of the
  others on port 5153 (of two downloading, the one of the lower machine
  connects). Each end says which files it has, whole or by piece, and tells
  the other of each piece it gets; the client asks for the rarest pieces
  first (from a random place, so that clients ask for different pieces), two
  at a time from each connection, sixteen in all, and the last few again of
  whoever is quicker. The pieces' hashes come from the host (from the
  others it named after ten seconds without them, and from the connections
  other machines made after thirty), all of them within a minute, checked
  against the file's hash; a frame of none is refused. Each piece is
  checked against its own as it arrives, and an end that sends a wrong one
  is not asked again. A machine
  sends of the game's files (its map, the resource maps the map needs, a
  download's pieces) no faster than `network.map_share_upload` (the host,
  1024 KB/s) or `network.map_seed_upload` (a player, 256 KB/s), round all
  its connections a 16 KiB block at a time, so that the game's own traffic
  keeps the rest of the upload; `network.map_seed = false` sends none. A
  player that has downloaded the map sends it on at once.
- **Connections.** A machine takes up to 24 connections, of which 15 others
  made (the rest are kept for its own download's), four from one address,
  eight at once that have not said hello (the oldest makes way); a
  connection says hello within ten seconds, and one that sends no whole
  frame for 30 seconds is closed. The host takes connections only from its
  game's machines' addresses.
- **Internet play.** The players of an internet game reach its host alone.
  A player that shares answers the holders of the invite it joined with as
  its host does (`p2p_set_answering_players`), and a downloading client asks
  another player, by the key hash the host told, for a session as it asked
  the host (`p2p_reach_player`: up to eight at once, alongside the request to
  the host, in one topic of the invite's token). Its map sharing connection
  then goes through their tunnel, as the game's connections go.
- **Writing.** A download is written to `<file>.part` beside its file in
  custom_maps, each piece at its place as it comes (the file is not made its
  size at once), and renamed when every piece is had. A part file found
  again is checked piece by piece on the hashing thread and taken up where
  it was. A download that stops deletes a part file that holds none of its
  file, and keeps one map's part file at most (the one kept before is
  deleted), so that a host cannot fill a disk with offers.
  The client checks the map again once it has it (`custom_edition_cache_present`),
  and deletes the files the download put in custom_maps if it cannot be
  played (or if one of them could not be put there), so that none is taken
  for present; it tells the host its progress every two seconds
  (`_progress`). The host does not drop a machine downloading (and for 20
  seconds after) for adding no players or loading slowly, while what it says
  it has grew by a piece (or to the whole, and no more than the files
  offered) in the last 80 seconds, counted from the first offer of the
  game's map however often, and for whatever level, the machine asks again.
  It takes a machine's requests five seconds apart at most: one that comes
  sooner waits its turn (the last of them kept). A connection forgets which
  pieces the other end has of the download's files when the download ends.
  A download that brings nothing for a minute, or a host that does not
  answer in 30 seconds, leaves the game, saying why.
- The download's progress is drawn at the top of the screen.

## Stages

1. (Done) Decoupled ticks: clients tick on their own clock with local input
   for local players and the latest relayed input for remote ones; the host
   no longer waits for clients; taps are accumulated so a quick button
   press is never lost; out-of-sync checks off. The lockstep netcode is
   gone: the host's per-tick game update carries no actions, and only
   keeps the clients' count of its ticks.
2. (Done) Authority: clients skip damage, deaths, spawns, pickups, item
   spawns, and scoring, and apply the host's state for them
   (`port/linux/game/network_distributed.c`):
   - every tick, every player's unit: which it is, alive or not, the seat
     it rides, shields and health (down, recharging, the damage they show),
     its powerups (camouflage and how long each has left), where it is (a
     client's own player's position is its own, within a tolerance, and the
     host takes it), and when dead who killed it;
   - what a client's players pick up, which the host decides: the client
     shows it (the HUD's message, the sound, a powerup's screen flash);
     only that client is told;
   - every two seconds, each player's ping (unreliable): the round trip
     of its machine's messages to the host and back, as the host smooths
     it, 0 for the host's own players, for the scoreboard;
   - twice a second and with every kill, the players' statistics that
     changed (once a second a few more, round them all, as the message is
     unreliable); when it changes (looked at five times a second, sent at
     most every other look unless the game ended; reliably, so never
     again unchanged), the game type's state (scores, the flags, the balls
     and their carriers, the king's hill, the players' speeds) and whether
     the game is over. A machine that has loaded is sent all of both at
     once. It counts the game type's events (captures, grabs and returns
     of the flags, laps, the balls reset, a ball passed to its carrier's
     killer), and a client announces those it has not had,
     as the host does its own; a client runs the rest of the game type's
     update that only shows the game (waypoints, messages, sounds, a
     carrier's speed).

   The messages are a kind of their own (the game's unused "data" message
   type), unreliable per tick, reliable for what must not be lost.
3. (Done) Object identity (`port/linux/game/network_objects.c`): the
   game's units, vehicles, weapons and equipment are the host's, at the same
   datum index (identifier and all) on every machine, so a message names
   one by its index.
   - The host tells its clients (reliably) of each such object it makes
     (what it is, where, how it looks) and each it deletes; clients make
     and delete theirs to match. A client that has loaded asks for all the
     host's objects and is told when it has them; from then on it deletes
     any such object the host has not told it of, and nothing but the
     host's word deletes the host's. That word has deleted a CTF flag or
     an oddball, which the game itself never deletes (two assertions,
     in `objects.c` and `weapons.c`, stop a build that is not `--release`
     there). Why the host deleted it is not known; the client follows
     the host's word whatever the reason: it takes the flag or ball out
     of its carrier's hands, and the goal its game type shows at it goes
     with it. The two assertions are not made for anything a client
     deletes in `distributed_client_delete`: the host's objects on its
     word, one in the place of an object the host creates, and its own
     the host has not told it of.
   - The objects placed when the map loads are placed alike everywhere:
     the host's word finds a client's already there. Past loading, a
     client's own objects (projectiles, effects: what only it sees) take
     indices from the upper half of the object array, clear of the host's.
   - Ten times a second, what every unit carries (the host's weapons, slot
     for slot, their ammunition, the weapon in hand, the grenades); a
     client moves the same weapon objects in and out of its units, which
     carry no grenades until the host says (not the unit tag's: a player
     spawned with the button held threw one the host's copy had not). A
     change of weapons or grenades goes to every client at once; one of
     ammunition only to the unit's player's machine at once, and to the
     others as often as they are sent that player. A client takes its own
     players' ammunition and grenade counts from the host only once a
     round trip has passed since it last fired, reloaded or threw (what
     the host says before then is from before). How old what a unit
     carries is travels in 16 bits. A client keeps an inventory whose
     unit or weapons have not come yet (the stream can trail the datagram)
     and applies it in full once they have, the newest of a unit's winning;
     one is dropped `EMPTY_INVENTORY_TICKS` after the newest came.
   - Players take the units the host spawns them with, seats are the
     host's (a client's own player's once it has ridden otherwise for
     longer than a round trip), and the CTF flags and oddballs are the same
     objects everywhere.
4. (Done) Corrections: the host sends each client where its moving objects
   are (vehicles, items, bodies) as often as they are near that client's
   nearest player (every tick within 25 world units, every second within
   60, every third within 120, every fourth further off), to every client
   three times over half a second as an object comes to rest (one lost
   would leave a falling body hanging until its turn round all of them),
   and a few of those at rest, round them all. A client takes the host's
   word on whether each is at rest even when its copy is close enough to
   leave where it is. A body's pose goes with it: one lying limp that the
   host moves stops lying limp (the blast that moved the host's reaches
   only the machines that see the body's player), and one the host has at
   rest is on the ground, so it lies limp again where it stops instead of
   keeping the pose of its fall in the air. A client puts its copies there, and the difference is
   drawn fading over a few ticks (`render_interpolation.c`) instead of a
   jump. An item loose in the world moves on a client by the same physics
   as on the host, its copy a tick or two behind or ahead of the host's by
   however late the host's states come, which varies; so a state is taken
   only when it is off both the copy's path of its last ticks and the
   host's last states' (`distributed_client_item_on_path`): compared with
   where the copy is now, a state a tick late was a tick's motion off, and
   the halfway moves back to it made a falling or bouncing item stutter. On
   those paths a copy at rest stays at rest, and one still moving when the
   host says its copy is at rest comes to rest by itself, unless it still
   moves at the host's next word. A vehicle no player drives is carried
   between those updates along the positions the host sent, a recording's lifepod as well as a Pelican.
   Its physics velocity swings about that path, so the carry steps from
   one of those positions to the next, and a jump further than a step of
   the path is left where the host put it. The host's word that it is at
   rest stops that step, and the step is kept only for the last gap
   between positions, so a late word does not drive it on. A client drives its own player's vehicle and sends where it is,
   which the host takes within a tolerance, as it does its own player's
   unit, and no further from an anchor (one it took, a newer once a
   second) than the vehicle moves in the client's ticks since, those no
   more than its own since and a little jitter (twice the tag's top speed,
   or as fast as the host's own ticks sent its copy in the last few
   seconds beyond the velocity it took of the client, whichever is more,
   no more than 3 world units a tick, and a tenth more a tick, with the
   blend distance; its velocity no faster, without the tenth, so that the
   client's word does not raise it, so a vehicle falls no faster than
   that and a tick's gravity: a long fall's last moves are the host's; a
   teleporter falls back to the tolerance); a vehicle that does not fly,
   float or stay (not a Banshee, a boat or a turret), in the air, no
   higher above where it left the ground than a thing thrown up as fast
   as it went up then, and a tenth of a world unit a tick, falls to since
   (less the client's round trip, half a second at most, and jitter), with
   2 world units more (as a player on foot); a copy at rest that the
   client's word moves wakes, so that it falls and counts its time in the
   air. The host sends the client its own
   vehicle every third tick, with the client's tick it took the vehicle at
   (`_distributed_object_predicted_bit` and a 16-bit time in the object's
   state); the client compares that with where it had the vehicle at that
   tick, and past 4 world units moves it by the difference, as it does
   its own player's unit.
5. (Done) Hits (`port/linux/game/network_damage.c`):
   - A client deals no damage. What its own players' shots, grenades,
     melee and vehicles hit, it reports to the host (reliably), with the
     host's latest tick it had heard of when it made the report.
   - The host deals a report once it has checked it: from that machine's
     player; damage one of their weapons (a vehicle's a driver's or
     gunner's; now or in the last ten seconds), a grenade the host's own game
     saw them throw in the last ten seconds that has not gone off (each
     throw's explosion is taken once, its other hits and its other damage
     (a frag grenade's shock wave) that tick with it: holding grenades
     deals nothing) or the vehicle they drove (in the
     last ten seconds: its collisions) can deal (its projectiles' impacts
     and detonations, followed through the tags), no harder than it can be
     (all of it, but an airborne melee blow's half again); of the shape the
     game gives that damage: what hits at a point (a projectile's impact,
     or its detonation on what it sticks to, and an explosion) there, its
     origin its epicenter; a melee blow from the striker's head (its
     origin) and body (its epicenter), both within a few world units of
     where the host had the player; a collision from the vehicle (its
     epicenter) within a few world units and the vehicle's size of where
     the host had the vehicle the player drove, the target (where the
     client had it) no further from it than their sizes and the push the
     vehicle gives it; the origin at the target in each (an explosion's and
     a melee blow's within its reach); what hits at a point within its
     reach of where the host had the player (their unit, or the vehicle it
     rode, and its size) since it could have been fired: the projectile's
     range, or its speed for as long as its timer runs, what it sets off
     going off from there, with 6 world units more (the host keeps thirteen
     seconds of where each player was, every third tick, as a rocket flies
     and a grenade outlives its thrower, and looks no further back than
     the client's round trip and a second before the report came; not checked for what the tags do
     not bound: a projectile that sticks, which what it sticks to carries,
     one whose timer starts once it bounces or rests, a weapon's own
     detonation; no line of sight); the target within a few world units of
     where the host had it at the tick the report was made at (a player's
     unit or vehicle: the host keeps a second of where they were) or of
     where it is (more for a fast one; also for one that is no longer a
     player's, a body or a vehicle left);
     and no more reports than the weapon that deals them fires (its rate of
     fire and projectiles a shot, with a margin; an explosion's hits count
     as one, when its damage has a reach, and no object is hit twice by
     one, the explosions of each player's reports told apart on their
     own). A report made more than three seconds ago (a burst of them held
     back while the network was out) is refused. A report is paid for
     before the host looks through its history, so a flood of them costs
     the sender its hits. The damage is dealt as a client's own hit can
     be: only the flags such a hit has, area damage as the game deals that
     damage (the report's only for damage dealt both ways), an explosion's
     direction from its epicenter to the target's centre (where the client
     had it) and its scale no more than its fall off over that distance
     gives (unless it does not fall off), any other's direction one long,
     and the host's multiplier and team, not the report's, its owner the
     player's unit (or the vehicle it rides, or a unit of theirs); a
     report with a number that is not finite, or a node, region or
     material the target does not have, is refused. Its own copies of a
     client's projectiles deal nothing (the report does), but once that
     client has left the game they deal what they hit, as its own do (a
     hit the client reported just before it left may so be dealt twice).
   - The host sends its clients the damage it dealt to units, and a client
     replays what it does besides the harm (which the units' states
     bring): the player's screen flash and shake, the unit's flinch, pain
     sound, knockback and stun, the scope it knocks the player out of, and
     who the HUD shows hit them. A killing blow it replays whole, so the
     body falls as the shot had it and the kill is announced with the
     host's killer (and the killer's score after it, as the host's game
     type has it: the game type's state, which brings the scores, may come
     after the blow), and a telefrag's message (a client does not decide a
     telefrag itself: it sees who blocks a teleporter a latency late); an actor's (a biped no player's, alive until then)
     too, counted by no one there (the host's statistics come as they
     are). A killing blow goes to every client; other damage to
     the machines of the unit's player, its riders and the damage's owner,
     and of the clients sent that player this tick (who can see them); a
     player's screen effects to that player's machine alone, but for a
     weapon's own shake of the player firing it (no one's damage), which
     that player's machine shows itself at once. The killing blow is sent
     with the tick's other damage and once more reliably (a client replays
     one blow of a unit only), so every body falls as the host's did: a
     body the host says is dead (the objects' states say so of an actor's,
     the units' states of a player's) that is still alive half a second on
     is killed with nothing to show.
   - A client's own projectiles respond to what they hit as the game has
     them: the host's shields and health, which the client has, say
     whether the shield or the body took the hit, and how much is left of
     it (the host's own copies of a client's projectiles likewise).
   - Hits shown at once (OpenH1 games but co-op's, `network.predict_hits`;
     below).

### Hits shown at once

Without it, a client's own projectiles show their impact at once (the
spark on a shield, the blood on a body: the material's response, which the
host's shields and health choose), and its own weapon's shake; everything
else a hit does comes with the host's word on it, a round trip after the
shot (50 to 100 ms on most connections, more over a lossy one): the
target's shield flare and its breaking (the units' states bring the damage
they show), its flinch, pain sound and knockback (the replayed damage), its
death, the body's fall and the dropped weapons (the replayed killing blow),
the kill's message, and the score (the game type's state).

In an OpenH1 game (not co-op's) a client shows its own players' hits on
another player's living unit on foot at once (`network_damage.c`,
`distributed_predict_hit`): it deals the damage to its copy of the unit as
the host will, with the least damage the hit's roll can give (so that a
kill it shows is one the host's roll gives too), and counts nothing of it
(`damage.c`): the flare, the shield's breaking, blood, the flinch, pain
sound and knockback, and, where it kills, the death and the body's fall at
once. It reports the hit as before, and keeps what the hit took of the unit
until the host's word on it comes:

- The host's states of the unit, which do not have the hit yet, are taken
  less what the hits not yet answered took (and their flare and shown
  damage as this machine's hits left them), so a shot's effect is not put
  back and the next shot's is judged against what is left.
- The host's damage of the hit (its replayed damage, which goes to the
  shooter's machine) answers it, and is not replayed again; the unit's next
  state has it.
- A kill shown is confirmed by the host's killing blow (or, its blow lost,
  by the units' states saying the player is dead): the kill is announced
  then, with the host's killer and their score after it, as before. The
  body has been lying there a round trip already.
- A hit whose word has not come a round trip and half a second on
  (`PREDICTED_HIT_SLACK_TICKS`) is refused: its effect is the host's states'
  again, and a unit it killed stands again where it fell (`unit_port_revive`:
  alive, its player's, the shields, health, grenades and weapons it had),
  so a refused kill shows as a fall and getting up, never as a kill.

Not shown at once, as the game has no way back from them or the host
decides them by what the client cannot know: hits on vehicles and their
riders, on actors (co-op), fire (a burning death runs about), a body
destroyed, the host's instant kills, telefrags, and pickups. A pickup is
the host's: the item is the host's object, which only its word deletes or
moves, and two players reaching for the same weapon (the case where a
pickup is felt) is what a client cannot predict, so a weapon shown in hand
at once would often vanish again; the HUD's message and the sound come
with the host's word, a round trip after the walk over.

With `debug.network_test`, each client logs how its shown hits went once a
second (`network test: predicted hits <shown> answered <n> unanswered <n> |
kills <shown> confirmed <n> refused <n>`). In a pair of 30-second bot games
on bloodgulch at 120 Hz over 25 ms ±3 each way (`net_compare.py --events`,
with `HALO_NETWORK_TEST_SHOOT=0.5`), the clients showed 55 hits: 52 answered
by the host's damage, 3 by its killing blows, none unanswered, and one kill
shown, confirmed (none refused).

## Transport

What reaches the other machines, and how, decides how the game feels over
a real network as much as the model does (compared with Quake III, Source,
Unity's Netcode for Entities, lightyear, netfox and the Ares source):

- **Datagrams carry only what is theirs.** A datagram is known to be a
  machine's only by the address it came from, which anyone can send one
  as. So a client takes from a datagram only what the host sends in one: its
  game's advertisement, its answer to a ping, and the distributed netcode's
  messages (checked as the host's below); the game's own messages (a player
  added or removed, the game begun or over, its settings), which the host
  sends over its connection, are ignored in a datagram
  (`network_client_message_handler.c`). The host takes a client's hits,
  Discord user and readiness only over its connection too, and every
  machine drops a datagram of the netcode stamped with a tick more than 15
  seconds from the latest it had from that machine (one stamped far ahead
  would have the machine's newer ones taken for stale); a message over the
  connection is taken whatever its tick (`network_distributed.c`).
- **Nothing held back.** The game's connections (the reliable messages:
  objects made and deleted, the game type's state, hits, pickups) send each
  write at once (`TCP_NODELAY`, in `xnet.c` for the game's sockets and in
  `p2p.c` for internet play's): with Nagle's algorithm a small write waited
  for the other end's delayed acknowledgement, up to 200 ms on Windows.
  Internet play's tunnel carries them with KCP, which sends only as its
  interval passes (10 ms, the least it takes): the tunnel's thread flushes
  each connection's KCP once a pass that something was written to it or
  came for it, so a write and the acknowledgements of what came go at once,
  a burst in as few datagrams as it fills, and the interval only times the
  resending.
- **Input every tick, unreliably, each tick's buttons several times.** A
  client sends the host its players' input after each tick, with the
  buttons of the three ticks before it, and the host sends every client
  every player's input as its tick ran it, the same way. Each tick's
  buttons are taken once, from whichever message brings them first
  (`player_queues_new.c`), so a press is lost only with four datagrams lost
  in a row, and nothing waits for a lost one to be sent again (the game's
  own per-tick update, reliable and so held up by any loss, now carries no
  input). The host's clock still reaches the clients in it, and the game's
  own client update, whose input the host no longer takes, goes ten times
  a second instead of sixty; the host takes its own players' input at
  each of its ticks.
- **As few datagrams a tick as they fill.** The unreliable messages of a
  tick to a machine go together (`_distributed_message_batch`), saving each
  one's headers (internet play's tunnel adds 36 bytes to every datagram: 15
  of its header, 16 of its tag and 5 of its own inside), each message split
  between two where one is full, so none goes out part empty; the machines'
  datagrams go in a turn that starts one further each tick.
- **Stamped with their tick.** Every message carries the sender's tick
  (its header's game time); an unreliable one (the units, the input, the
  objects, the statistics, what units carry, the damage) that arrives after
  a newer of its kind is dropped, so a late datagram never puts anything
  back.
- **Taken at the tick.** The host takes a client's players' and vehicles'
  positions (the latest of each) at its next tick, not as each arrives. (An
  OpenH1 game's host sends a client's input on to the others as it comes:
  "Forwarded input", above.)
- **Fewer bytes.** Vectors travel in 16 bits a part, shields and health in
  16 bits; a unit's state goes without the parts that are their defaults
  (the vehicle and seat of one that rides nothing, the position of one that
  rides, its up when straight up, the damage its shields and body show,
  powerups and camouflage when it has none, the tick its own client
  predicted it at to any other client), 35 bytes for a player on foot
  instead of 64; a player's input goes with its tick's update once a
  message, and the buttons of the three ticks before only where they
  differ from the tick after (14 to 20 bytes instead of 24); what a unit
  carries is sent when it changes (and once a second); the moving objects
  as often as they are near the client's players, the objects at rest
  round all of them, four a tick; the players' statistics when
  they change (with every kill, twice a second), with sixteen more
  players' once a second round them all; the game type's state when it
  changes, at most two and a half times a second.
- **The players each client needs, when it needs them.** The host sends a
  client every player's unit and input every tick when they are within 25
  world units of the client's own players, every second tick within 60,
  every third within 120, every fourth further off, and every sixth when no
  cluster of the client's players' can see theirs (the map's potentially
  visible set, which errs on the side of seeing: Halo's are coarse, and
  most of a map sees most of it) or they are dead. A client whose players
  are dead is sent the others as from where its players last were alive.
  None is ever left out, but a player who has left the game, whose unit
  goes only when it changes, and a dead player's input, which drives
  nothing. Whatever changes what a client sees goes at once:
  - a player's death, spawn or seat, and a player coming into sight;
  - a player's input every tick while their buttons or weapon choices
    change (the ticks it carries), so no jump, grenade or melee of theirs
    is missed, even out of sight;
  - a player a client's player aims at within 35 degrees at least every
    second tick, and within 20 degrees through a scope every tick, so a
    sniper sees a far player move as smoothly as a near one (as Ares
    raises the priority of what a player zooms onto).

  A client's own players' units go to it every tick, their input never (it
  has its own).
- **The round trip.** A client's input messages tell the host the latest
  host tick the client has had, which gives the host each client's round
  trip (smoothed as TCP smooths its own, once a message). The host keeps a
  second (32 ticks) of where players' units and vehicles were, and checks a
  client's hit against where the target was from the host tick the client
  had last heard of when it made the report (three ticks more), instead of
  against where it is now with a wide margin: however long the report
  took to come (the reliable channel's resending holds it up), that is
  when the shooter saw the target (as far back as the host keeps). A
  report made more than three seconds ago is refused.
- **The latency meter** (`latency_meter.c`, `display.latency_meter`) shows
  the same round trips, timed in milliseconds by when each machine sent the
  tick they come back naming (the last 64 ticks' send times kept), with
  nothing more sent: the host each client's, by the host tick its input
  names; a client its own, by its tick the host's state of its player (or,
  driving, its vehicle) says the host has it at. Each is the network both
  ways plus each machine's wait for its next tick, so a LAN reads 15 to 35
  ms. A client that has heard nothing from the host for two seconds shows
  "Connection problem"; the host logs a client silent as long. With the
  meter on, the scoreboard shows these where the machine has them (the
  host every client's, a client its own players'), and the host's measure
  in ticks, which every machine is sent, for the others.

### Relays

Two machines whose NATs keep them apart (hole punching cannot meet them)
connect through a relay: a dedicated server or a relay of `halo --relay`,
never a player's machine (`port/linux/src/p2p_relay.c`; the packets and the
limits are in `docs/dedicated_server.md`, "Relays"). This is OpenH1
protocol version 16. Legacy games are unchanged; the relays are below
the game, in the tunnel, and two OpenH1 machines in a Legacy game use them
alike. OpenCE machines never offer relays, so they connect as before.

- **Fallback only.** A pair tries relays only once hole punching has had 4
  seconds (1.5 when both machines' addresses show strict NATs). Each offers
  the other its relays through signalling (`relays.txt`, and the relays its
  game's dedicated server told it of); both bind to all of them, and only
  once the other has offered (so a machine that never offers holds no
  relay's room). The first relay that carries a ping through makes the
  session's path.
- **Failover.** The other relays that paired stay paired (a keep-alive each
  4 seconds). A path silent for 2.5 seconds is replaced by the first other
  relay a packet comes through (both machines ping through every paired
  relay meanwhile); with none paired, the relays are bound again. The
  session ends only at its own 20 seconds of silence.
- **Direct again, with hysteresis.** While relayed, both machines ping each
  other's addresses every 3 seconds for a minute, then every 30. A direct
  path takes over only once its packets have come for 5 seconds without a
  gap of 2.5 (or at once if the relay is silent); the relays are kept 30
  seconds more, to go back to.
- **What a relay sees.** Both machines' addresses; never the game's
  traffic, which the tunnel seals with keys only the two have, as on a
  direct path. Nothing is less private than a direct connection but that.
- **Bans.** A relayed player's address, for the host's bans and votes, is
  the one the relay saw its binds come from and checked by its cookie: never
  one the player gave of itself. The relay tells only the host's side.

### Fronts

A relay needs both machines to take part, so it cannot help a player whose
build never offers relays (OpenCE, an older OpenH1) reach a host behind a
NAT that cannot be punched. A front can: a public UDP port on a relay of
`halo --relay` (`server.front_ports`) that the host registers with
(`network.front`; `port/linux/src/p2p_front.c`; the packets are in
`docs/dedicated_server.md`, "Fronts"). The host lists the front's port as
one more of its addresses in its signalling answer, and a joiner punches to
it as to any other. Nothing a joiner sends or receives changes: the front
passes its tunnel packets, still sealed end to end, to the host, and the
host's answers back from the same port, so the joiner cannot tell it from
the host. On the host, a joiner reached so is relayed through the front (a
relay attempt of the front's, "front and client number"), with the address
the front saw it at for its bans and votes; a direct path found later takes
over as from a relay. The protocol between host and front is versioned on
its own (version 1); the OpenH1 and Legacy protocols, and
`HALO_PORT_NETWORK_VERSION`, are unchanged.

## Testing

`debug.network_test` (`port/linux/game/network_test.c`) hosts or joins a
game without the menus (in a team game the joining player takes the other
team; `local:<map>` plays a local split screen game instead, which the
bots of `bots.count` join), and `debug.test_input` plays controller 1 with a scripted bot (or,
`look:<seed>`, one that stands still, only turning and looking up and
down) in the game alone, leaving the menus and the lobby to the test; each machine logs every player's position, health and shields,
where they aim and face, their animation state and how hard they move,
weapons, grenades, score, kills and deaths every second, with the objects
made and removed and the hits reported, dealt, rejected and replayed, so
two machines' views of one game can be compared. `debug.network_test_kill`,
`debug.network_test_shoot`, `debug.network_test_vehicle` and
`debug.network_test_pickup` script kills, hits, a vehicle ride and a weapon
swap the bots' wandering does not reach (`debug.network_test_pickup_weapon`
picks the weapon: the first whose tag name has it in it, as "sniper"),
`debug.network_test_hitch` stops the frame for that many seconds as the
vehicle test seats the player (as a stall of the renderer or the VR runtime
does: the host must play on, its own client not timing out its own server),
`debug.network_test_chat` has each machine's player say a chat line to
everyone, one to the team and one to the vehicle (the host seats the last
two players in one first), then the host mutes the last player for a
minute, each says a line, the host unmutes them and each says another,
each says a line with a link (a client's sent past its own check, for the
host to refuse), the host turns the text chat off (and on again) as each
says one more, the host mutes the last player for everyone (as its
scoreboard does) around a line each, and the last player's machine mutes
the host's for itself (as a client's scoreboard does) before another,
which every machine's `debug.txt` records as
it shows them (the notices as `chat notice:`), and `debug.network_test_score`
shortens the game, to test the next (`host:<map>:<variant>,<variant>...`
plays the variants in turn, the next once a game is over, as the host's
button on the scores does). `debug.network_test_public` lists the host's
game in the server browser, and `browse` joins the one listed there with the
most players (not co-op, if it can), as the server browser does.
`debug.network_latency` and
`debug.network_loss` hold back what a machine receives and drop some of its
datagrams, to test as over the internet. `debug.network_corrupt` damages
that share of the datagrams a machine receives at random (bytes changed,
cut short, stretched to a full datagram, or replaced throughout: `xnet.c`),
and `debug.network_corrupt_stream` that share of its reads of connections
(a damaged connection is closed, so a little goes a long way), from
`debug.network_corrupt_after` seconds after the start (what a host sends its
own client over the loopback is damaged too, so the game is set up and
started first), to test that nothing another machine sends can crash the
game: a host and a client with a third of their datagrams damaged must play
on, their logs noting what they refused.

With `log`, `debug.network_test` hosts and joins nothing: it logs the game
one machine plays (a `map_name` in `init.txt` plays one without the menus),
as above. `debug.network_test_cues` has the host, or that machine, run
console lines at their ticks, from the first tick every player is in.

The host logs to `debug.txt` when a player on another machine presses the
action button where the host has nothing for them to pick up, with where it
has them and the nearest item: a client that sees a pickup the host does
not.

### Comparing two builds

`tools/net_compare.py` checks that two builds play a network game in step:
a new port or architecture against the 64-bit x86 Linux build (arm64
against x86-64), or a build with a change to the simulation against one
without. It plays them together on the loopback, build A
hosting and B joining (and, with `--swap`, the other way round), on each
map given, each copy's player a seeded bot (`debug.test_input`
`bot:<seed>`, `--events` adds `debug.network_test_shoot` and
`debug.network_test_kill`), headless (`debug.null_renderer`, with no
display; or with `--window` a hidden window, on the display the tool was
started with, `DISPLAY` or `WAYLAND_DISPLAY`, or `--display`'s: a virtual
one, Xvfb, rather than a desktop), and compares the two machines' logs:

```sh
python tools/net_compare.py --a build/linux_x86_64/halo --b <another build's halo> \
    --data <folder holding maps/> --maps bloodgulch,beavercreek,hangemhigh --swap
```

It needs the retail maps, so it runs on a developer's machine or a
self-hosted runner, not in the public CI; run it before merging a change to
a build's compiler, architecture, maths or the simulation itself. A build
is its executable: a build's (`build/linux_x86_64/halo`), or a release
tree's `bin/openh1` (its `lib/` and `share/openh1` are linked into
the copy, which stays a portable tree). Each copy gets its own folder (an
executable, `config.toml` written in the copy's folder, a data root linking
the maps, and saves) in `build/net_compare/`, removed when every game
passes. It prints PASS, FAIL (with the first divergent player, tick and
field) or ERROR (a copy that did not start, join or log) for each game and
a line of figures; a copy's log and the end of its `debug.txt` are kept for
each game. It exits 0 when every game passes, 1 when any fails, even if
another was an error (a divergence is the finding; an error only a gap in
it), else 2 when any was an error. Each copy runs in a process group of its
own, which the tool ends however it ends: stopped with Ctrl-C, SIGTERM or
SIGHUP, it stops the copies and exits 128 plus the signal's number, and
killed outright, the kernel ends the copies (`PR_SET_PDEATHSIG`). `--compare <host log>
<client log>` judges two logs already written, and
`--a-wrapper`/`--b-wrapper` run a build under an emulator. `--port <n>`
plays the game on port n (`network.port`; the clients' n + 1), so that two
runs, or a run and another game, share the computer (both builds must have
the setting). Both builds
must log the drift (below): one built before it is an error.

The host decides and a client predicts its own player, so the comparison
is not lockstep's bit for bit, but of what the netcode allows:

- **Each second's state.** For every player, what the host logged and what
  the client logged at the same tick (the machines' clocks agree: the
  ticks match). Positions within 1 world unit: a remote player's word
  comes a tick or so late, a client's prediction reaches the host as late,
  and a player runs about 0.08 world units a tick, so identical builds are
  0.04 apart on average and 0.24 at most, and a client puts its copy of a
  remote player where the host has it past `REMOTE_BLEND_DISTANCE` (1
  world unit); out for 2 seconds in a row fails. Aim within 15 degrees,
  body and shield within 0.15, and alive or dead, riding, grenades,
  weapons, score, kills, deaths and team the same: out for 3 seconds in a
  row fails, as the host's word on damage, a death, a pickup or a score is
  a round trip, or a death blow's wait, late, and a player thrown by a
  blast aims as no input says for a second or two. Rounds within 3: out
  for 6 seconds fails, as a client keeps its own player's ammunition
  while it fires and takes the host's (a pickup's) only once it stops
  (`network_objects.c`).
- **Drift.** Those agree between builds that simulate differently too,
  as the netcode corrects every copy toward the host's. What does not is
  how the host moves a client's player: each tick the host moves it on
  from where it took the client's last prediction, with the client's
  input, and then hears the client's prediction of the same tick. Where
  the host took the last a tick ago, it was of the client's tick before,
  and the host has the input of the client's tick, both machines moved
  the player one tick with the same input from much the same place, and
  `distributed_take_prediction` (network_distributed.c) notes how far
  apart the two moves were, and whether further than
  `DIVERGENCE_APART_DISTANCE` (0.0001 world units, far above the last
  bits' rounding). (The moves, not where they end: the host puts its copy
  only halfway to a prediction a little off, so where it ends would be as
  far off for ticks after anything that set them apart.) Identical builds
  move it alike to the last bit but where the player touched something
  the two machines have in slightly different places (the other player,
  a grenade's blast, an item knocked about). A build that moves a player
  otherwise is apart in most of the ticks the player moves. More than 60%
  of the ticks apart fails.

The per-second line each machine logs (`network_test_log_players`) is:

```
network test: tick <tick> player <index>: (<x> <y> <z>) h<body>/<shield>[ riding][ camo] g<grenades>/<grenades> w
  a<aim yaw>/<aim pitch> f<facing yaw> l<look yaw>/<look pitch> as<aiming screen>/<looking screen> st<animation state>
  thr<throttle>[ <weapon tag>:<rounds>...] s<score> k<kills> d<deaths> f<team kills> t<team> m<machine>
  [player <index>: dead s<score> k<kills> d<deaths> f<team kills> t<team> m<machine>] ...
  | items <on the ground> (+<created> -<deleted> !<failed> x<removed>) | <playing|game over> to <score to win>
  | sent <n> received <n> corrected <n> | hits <n> dealt <n> rejected <n> replayed <n>
  | local <player> camera (<x> <y> <z>) respawn <ticks> | drift <ticks> <apart> <mean> <largest>
```

(on one line; a riding player's position is the vehicle's, angles are in
degrees, a weapon's tag is its tag index's low 16 bits in hexadecimal, and
its rounds are those loaded and in reserve). `drift` is the host's: how
many ticks of the second it compared a client's move of its player with
its own (above), how many of them were apart, and the mean and largest
distance apart (world units); a client's is 0. net_compare.py reads every
field but the items, the hits and the camera, and compares the last game a
log has.

Calibration (on one 24-thread computer, with up to four games played at
once, 60 seconds each, both ways round, on bloodgulch, beavercreek,
hangemhigh, chillout, prisoner, ratrace, longest and carousel; with the
32-bit x86 Linux build the port had then, i686):

| Builds | Games | Result | Ticks apart |
| --- | --- | --- | --- |
| i686 against itself | 64 | 63 pass (one copy crashed, an error) | 0-13%, up to 42% with the frames uneven |
| i686 against itself, `--events` | 12 | 12 pass | 7-36% |
| B's players 0.5% faster | 10 | 10 fail, at the first second compared | 77-88% |
| B's players 2% faster | 6 | 6 fail, at the first second compared | 88-93% |
| B's gravity 0.5% or 2% stronger | 14 | 14 pass | 25-38% |

(B a build of the same sources with `movement_desired` or `global_gravity`
scaled in `biped_update_physics`, a change made for the test.) A change to
how players move on the ground is caught in the first second; one that
changes only the ticks in the air, as gravity does, is within what
identical builds show, and not caught. Positions agree to 0.04 world units
on average, 0.24 at most, in every game, perturbed or not: the netcode
corrects them.

### Measuring latency

`tools/net_compare.py --latency` times how long a press takes to reach the
other machines, on the computer's own clock (`CLOCK_MONOTONIC`, which every
copy on one computer shares: `platform_shared_clock_microseconds`). Each
copy's player pulls the trigger for 60 ms and pushes the stick for 200 ms in
turn, 410 to 710 ms apart, on the real clock (`debug.test_input`
`latency:<seed>:fire,move`), so the presses fall anywhere in a tick. With
`debug.network_test` each machine logs, at each sub-step, when each player's
unit first takes a pull or a push (`network test: press fire player 1
local at <microseconds>` on the player's own machine, `remote` on the
others: `network_test.c`'s `_sim_substep_network_test` slot), each hit its
player reports, the host deals and its damage is shown (`network test: hit
made|dealt|shown object <index> at <microseconds>`) and, with
`display.latency_meter` (which `--latency` turns on), the meter's round
trips once a second (`network test: ping own <ms> | players <player>:<ms>...`).
The tool pairs each press with its first sight on each other machine (within
400 ms), each report with the host's dealing of it and the shooter's showing
of its damage, and prints their median, mean and 95th percentile (`--json`
keeps them). `--c` plays a third copy, so that a press goes from one client
to another; `--events` adds the hits. The latency player stands still but
for its pushes, so the drift figures are not judged.

A network's delay sets each machine's phase against the others' ticks (a
client's clock was the host's, from the first update it heard, until
protocol version 12: "The client's clock"), so each delay gives waits of
its own: the figures below pool three delays a third of
a tick apart for each network (20, 25.5 and 31 ms each way for 20 ms; 50,
55.5 and 61 for 50), with `tc netem` on the loopback of a network namespace
of its own (`unshare -rn`, then `tc qdisc add dev lo root netem delay 20ms
3ms`), which delays every datagram and every segment of the connections
each way.

Pooled means in ms (the worst 95th percentile in brackets): before
forwarded input, with it (protocol version 6), and with the sub-steps as
they run too (version 7).

Press to sight from one client to another (`press seen, client to client`):

| Network | 30 | 60 | 120 |
| --- | --- | --- | --- |
| LAN | 33 (64), 17 (32), 17 (29) | 67 (69), 34 (40), 10 (14) | 67 (71), 37 (83), 5 (15) |
| 20 ms ±3 | 73 (105), 67 (71), 71 (100) | 129 (149), 84 (103), 62 (136) | 133 (136), 93 (102), 56 (74) |
| 50 ms ±5 | 139 (167), 129 (161), 131 (166) | 183 (201), 142 (172), 124 (195) | 184 (203), 150 (175), 117 (141) |
| 20 ms ±3, 1% loss | 76 (120), 68 (99), 73 (91) | 128 (140), 90 (103), 62 (117) | 137 (151), 95 (144), 58 (73) |

The host's player's press to a client (`host to client`):

| Network | 30 | 60 | 120 |
| --- | --- | --- | --- |
| LAN | 16, 17, 18 | 47, 37, 13 | 36, 36, 7 |
| 20 ms ±3 | 41, 35, 35 | 65, 68, 37 | 65, 62, 31 |
| 50 ms ±5 | 70, 70, 70 | 92, 95, 67 | 91, 93, 62 |
| 20 ms ±3, 1% loss | 37, 34, 41 | 65, 61, 39 | 67, 65, 32 |

A hit reported to the host's dealing it (`hit made to dealt`):

| Network | 30 | 60 | 120 |
| --- | --- | --- | --- |
| LAN | 33, 33, 33 | 33, 32, 16 | 33, 32, 8 |
| 20 ms ±3 | 57, 57, 55 | 56, 59, 42 | 57, 58, 33 |
| 50 ms ±5 | 88, 89, 84 | 90, 90, 71 | 89, 89, 64 |
| 20 ms ±3, 1% loss | 67, 73, 65 | 60, 68, 42 | 57, 55, 35 |

A client's press to the host (`client to host`) and the meter's round trip
are the same in all three (the host takes a client's input at its tick): a
LAN's round trip reads 17 ms at 30, 18 to 30 at 60 and 30 to 31 at 120, 58
to 62 at 30, 87 to 90 at 60 and 93 to 94 at 120 over 20 ms each way, and
121 to 123, 146 to 149 and 146 to 151 over 50. A LAN's figures at 30 are of
one phase each (their spread is the phase's, not the builds'). With 1% of
the datagrams lost, a hit's report (reliable) waits for its resending now
and then: a worst 95th percentile of 310 to 340 ms. The tables of every
figure are kept by `--json`.

With the input taken at its tick (version 8, "Input at its tick") and the
other players corrected against their history (version 9), over the same
20 ms ±3 network (three copies on one computer, pooled over the three
delays; the means in ms, the worst 95th percentile in brackets), before,
with version 8 and with version 9:

| Path | 60 | 120 |
| --- | --- | --- |
| Client press to the host's simulation | 62 (70), 39 (59), 40 (66) | 66 (76), 39 (71), 37 (43) |
| Client press to another client | 64 (135), 61 (79), 62 (78) | 57 (76), 58 (73), 59 (100) |
| Host's press to a client | 37 (67), 37 (48), 36 (51) | 33 (57), 33 (46), 32 (43) |

A client's press reaches the host's simulation a network trip and about a
sub-step after it, where it took a trip, a tick and a phase's wait. The
other paths were near their floor already (forwarded input and live
controls) and are unchanged. In bot games (three copies, bloodgulch and
beavercreek, on the loopback and over 20 ms ±3, at 30, 60 and 120) every
game passes; the host's moves of a client's player come apart from the
client's in 0.4% to 0.8% of the ticks before and 8% to 12% with version 8
at 60 and 120 (a sub-step's controls that come after the host's sub-step
ran without them), and the clients' corrections and the positions apart
are as before.

With the client's clock led ahead of the host's (version 12, "The client's
clock"), over 20, 25.5 and 31 ms ±3 each way (three copies, one game a
delay; the means in ms of a client's press to the host's simulation, and in
brackets what it takes past the one-way delay):

| Rate | Before | With the clock led |
| --- | --- | --- |
| 30 | 36.3 (16.3), 31.8 (6.3), 36.3 (5.3) | 27.5 (7.5), 32.7 (7.2), 37.7 (6.7) |
| 60 | 38.7 (18.7), 33.4 (7.9), 38.4 (7.4) | 27.5 (7.5), 32.3 (6.8), 37.9 (6.9) |
| 120 | 38.9 (18.9), 37.6 (12.1), 38.4 (7.4) | 26.5 (6.5), 32.1 (6.6), 38.0 (7.0) |

What a press takes past the network's delay was 5 to 19 ms, by the phase
each delay set; it is 6.5 to 7.5 ms at every delay and rate (the input's
datagram, the host's frame, the 2 ms margin and the sub-step it lands in).
Pooled, 38.3, 36.8 and 34.8 ms become 32.2, 32.6 and 32.6 at 120, 60 and 30.
The meter's round trip drops with it (83 to 72 ms pooled). The press from
one client to another and from the host to a client are as before: their
spread is the players' places (near or aimed at, forwarded; else relayed),
which differ from game to game; in games where they were placed alike the
two builds measured alike (56.6 and 56.9 ms, 62.7 and 62.7).

In bot games (three copies, bloodgulch and beavercreek, on the loopback and
over 20 ms ±3, at 30, 60 and 120) every game passes. The host's moves of a
client's player come apart from the client's in 0.0% to 1.2% of the ticks
at every rate (8% to 12% at 60 and 120 before: the sub-steps' controls now
come before the host's sub-steps run), the clients' corrections are 4 to 31
a game, and the positions apart are 0.017 to 0.057 world units on average,
0.24 at most. The host took the prediction before the latest
(`distributed_predictions_earlier`) or held in 0% to 4% of the ticks of the
latency games, against up to 83% before. The host's states at the sub-step rate (version 14) are measured in "States
at the sub-steps".
