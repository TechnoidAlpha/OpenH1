# Linux

The Linux builds are 64-bit: `ninja linux_x86_64` compiles the game for
64-bit x86 Linux, and `ninja linux_arm64` for 64-bit ARM Linux. Each is the
default build of its computer (`ninja` without a target builds it). The
result is a native executable, `build/linux_x86_64/halo` or
`build/linux_arm64/halo`. The game shows its graphics with Vulkan 1.3. It
plays sound through SDL3. It accepts keyboard, mouse and gamepad input.

The game's data (tags, cache files, saved games) contains 32-bit pointers,
as on the Xbox. So each build runs the game as the Android port does: the
game is 32-bit-pointer code of the computer's architecture (x32 on x86-64,
arm64_32 on ARM), a guest image in a 64-bit executable, the host. Refer to
"64-bit x86" and "64-bit ARM". The examples below name
`build/linux_x86_64`; on a 64-bit ARM computer, read `build/linux_arm64`.

## Requirements

You do not need the Xbox SDK. The declarations that the game uses are in
`port/include/xdk`.

To build:

- Python and ninja.
- clang for the host. The guest is compiled, linked and checked
  (`tools/guest_image_check.py`) with LLVM 22.1.8, which `configure.py`
  downloads the first time (approximately 1.5 GB) to `~/.cache/openh1`.
  Refer to "The guests' compiler" in [README.md](../../README.md#the-guests-compiler).
  The option `--linux-x86-64-cc` (or `--linux-arm64-cc`) of `configure.py`
  selects a different clang for the host and the guest, which then needs
  the `x32` (or `arm64_32`) target, `ld.lld`, `llvm-ar` and `llvm-objdump`.
- CMake, and the development files that SDL3 uses for X11, Wayland and
  sound. Refer to the SDL documentation, or to the `linux-x86_64` and
  `linux-arm64` jobs in `.github/workflows/build.yml`. No OpenGL or Vulkan
  development files are needed: the build downloads the Vulkan headers it
  uses.
- A network connection for the first build. `configure.py` downloads musl
  1.2.5 (checked against its SHA-256), SDL 3.4.16 and the Khronos Vulkan
  headers (checked against their SHA-256) to `build/<name>/third_party`
  (`build/linux_x86_64/third_party`, `build/linux_arm64/third_party`), and
  with `--vr` the OpenXR headers ("VR"). Later runs of `configure.py` take
  them from there.

To start the game:

- A GPU and driver with Vulkan 1.3 (dynamic rendering, timeline
  semaphores, host query reset), and the Vulkan loader, `libvulkan.so.1`,
  which the game loads when it starts. Refer to
  [the Vulkan README](vulkan/README.md) for the rest of what the driver
  must have. Without them the game says so in a message box ("The game
  cannot draw with Vulkan on this device") and in its log.
- glibc 2.35 or later, for the builds from GitHub Actions, which are built
  on Ubuntu 22.04. A build that you make needs the glibc of the computer
  that built it, or a later one.
- The X11 or Wayland client libraries, and the PipeWire, PulseAudio or ALSA
  client libraries for sound. The game brings SDL 3 (`libSDL3.so.0`, next
  to the executable in a build, in `lib/openh1` in a release), which looks
  for X11, Wayland, libdecor, PipeWire, PulseAudio and ALSA when it starts,
  so it starts with whichever of them the system has. On GNOME, a Wayland
  window has borders only with libdecor.

SteamOS has all of these. Its system is read-only, and the builds from
GitHub Actions need no package: they bring their own SDL3
(`lib/openh1/libSDL3.so.0`). The executable looks for it in its own folder
and in `../lib/openh1` first (a `DT_RPATH` of
`$ORIGIN:$ORIGIN/../lib/openh1`, which comes before `LD_LIBRARY_PATH`), so
that it uses this SDL even when Steam sets `LD_LIBRARY_PATH`. Its license
is `SDL3-LICENSE.txt`.

To see the glibc version that a build needs, enter
`readelf -V build/linux_x86_64/halo | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`.

## Build the game

1. Go to the root folder of the repository.
2. Enter `python configure.py`.
3. Enter `ninja` (`ninja linux_x86_64` on a 64-bit x86 computer,
   `ninja linux_arm64` on a 64-bit ARM computer).

## Start the game

Enter `build/linux_x86_64/halo`.

The release (`OpenH1-Linux.zip`, or
`OpenH1-Linux-ARM64.zip` for 64-bit ARM) is a folder laid out as a
Unix prefix is (refer to "Packaging"). Unpack it into a folder, and open
`install.sh` in that folder: it installs the game in
`~/Applications/OpenH1` (refer to "Steam Deck", which applies to any
Linux desktop). Or enter `bin/openh1` in the folder where you unpacked it.
The folder of the game is the game's folder: the game keeps `config.toml`
and `maps/` there.

The game data is the folder that contains `maps/`, from an Xbox disc image
of any version of the game. The game looks for this folder in this
sequence:

1. `paths.data` in `config.toml`.
2. The current folder.
3. The game's folder: the folder of the release (above `bin/`), or the
   folder of the executable (`build/linux_x86_64/halo`). For an installed game
   (refer to "Packaging"), `~/.local/share/halo-linux`, unless the game's
   folder is read-only and has `maps/`.
4. `assets/` in the current folder, and `assets/` in the repository that
   contains the executable.

A `paths.data` whose folder has no `maps/` (it was moved, or it is on a
drive that is not connected) is passed over for the others, and the first
start, below, says so and offers to find the maps again.

If the game finds no data (its first start), it looks for your Xbox disc
image itself and shows what it finds on a screen of its own, in its window
(`port/linux/src/first_start.c`). The screen works with a controller (the
D-pad or the left stick, A to choose, B to quit), the keyboard (the arrows,
Enter, Escape), the mouse and touch. It asks nothing of the desktop, so it
works in the Steam Deck's Game Mode and on the Steam Frame, where no system
dialog can be shown, and on a desktop without a file picker.

The game looks in these places, a few folders deep (it skips hidden folders
and Steam's game folders, and stops after 3 seconds):

1. Downloads and the Desktop (the XDG user folders, else `~/Downloads` and
   `~/Desktop`).
2. SD cards and USB drives (below `/run/media` and `/media`). A Steam
   Deck's SD card is shown as "SD card".
3. The home folder.
4. The game's folder, which it copies `maps/` to (the folder of the
   release, above `bin/`, or of the executable). A link to the image
   there is found too.

It offers each disc image of Halo (a name that ends in `.iso` or `.xiso`, in
capitals or not, whose file system the game reads to check that it holds
Halo's `maps/ui.map`), and each maps folder copied from a disc (a folder
with the Xbox game's `ui.map`; Halo PC's maps are not offered), with the
place and the size of each. Choose one to copy its maps (about 2 GB) to the
game's folder. The screen shows the progress, and then the game starts.

If the game finds nothing, the screen says where to put the image: put it
in Downloads or on the SD card (from another computer, or with Steam's file
transfer), then choose "Look again". On a desktop (not under gamescope, and
not in the VR build), the screen also has "Browse for the disc image...",
the desktop's file picker (through the desktop portal or `zenity`). Where
neither is present, the screen says so, and you can still put the image in
Downloads and choose "Look again". The game does not stop. A copy that
fails (an incomplete image, a full disk) is shown on the same screen;
"Back" looks again.

The game writes the copy to `maps.partial`. When the copy is complete, the
game changes the name to `maps`. If the copy stops before it is complete,
the game asks for the disc image again at the next start.

### Steam Deck

Do the set-up in Desktop Mode:

1. Download `OpenH1-Linux.zip` and unpack it (in Dolphin, the file
   manager: Extract > Extract archive here).
2. Open `install.sh` in the unpacked folder (Dolphin asks whether to run
   it: Execute). It opens a Konsole window, which shows what it does.
3. When it asks where your game's maps are, select the disc image (`.iso`
   or `.xiso`) or the maps folder copied from the disc.
4. Start OpenH1 from the library in Game Mode.

Step 3 can be skipped: put the disc image in Downloads or on the SD card
(in Game Mode too, with Steam's file transfer from another computer), and
start OpenH1 from Game Mode. Its first start finds the image and copies the
maps (refer to "Game data", above).

Updates need no Desktop Mode: when a new release is out, the game
downloads it while you play, and asks at the main menu. Select UPDATE with
the controller, and the game updates and starts again (refer to "Updates").

`install.sh` (`tools/linux_install.sh`, at the top of every Linux
release's tree; the Steam Frame's package has the same script, "Steam
Frame"):

- Copies the release to `~/Applications/OpenH1`: a folder of the player's
  own, which the game can write to and so updates itself in (refer to
  "Updates"), and which SteamOS's updates keep. Each file that differs from
  the installed one is copied as `<name>.new` and compared with the
  release's; when all are, each takes its name with a rename. It refuses
  while the game runs. Open the `install.sh` of a newer release to update
  in the same way (the game also updates itself). It never changes
  `config.toml`, `maps/`, `debug.txt`, the files of the self-updater, or the
  saves (`~/.local/share/halo-linux`), and does not delete the files that
  an older release had and this one has not, as the self-updater does not.
  After the install, the unpacked folder can be deleted.
- A release that already has your files (`config.toml` or `maps/` next to
  `bin/`: a release unpacked before `install.sh` came with it) stays where
  it is: `install.sh` copies nothing, and does the rest for that folder.
- If the game has no maps yet (and `paths.data` is not set), asks with
  `kdialog` for a disc image or a maps folder. A maps folder (the folder
  selected, or `maps/` in it) has its `.map` files and `loading.tga`
  copied to `maps.partial`, then given the name `maps`, as the game does.
  A disc image is the game's to copy (its first start, above):
  `install.sh` puts a link to it in the game's folder, where the game finds
  it, and asks to start the game once to copy the maps (about 2 GB).
  Choose the image when the game shows it. Starting the game from Game Mode
  does the same.
- Writes the menu entry `~/.local/share/applications/openh1.desktop`: the
  release's `share/applications/openh1.desktop`, with the installed
  `bin/openh1`, its folder and its icon
  (`share/icons/hicolor/256x256/apps/openh1.png`).
- Adds that entry to Steam (SteamOS's `steamos-add-to-steam`), named
  OpenH1, when Steam has no shortcut to the installed `bin/openh1` (in
  `~/.local/share/Steam/userdata/*/config/shortcuts.vdf`, which it only
  reads: Steam rewrites that file while it runs). Without
  `steamos-add-to-steam`, it says how to add it: in Steam, Games > Add a
  Non-Steam Game to My Library, OpenH1, Add Selected Programs.
- Gives the shortcut its library artwork, so that Game Mode shows the
  game's tile, banner and logo rather than a grey tile: the release's
  `share/openh1/steam/*.png` (`port/assets/steam`, drawn by
  `tools/steam_art.py`), copied into the grid folder of each Steam user
  whose `shortcuts.vdf` has the shortcut
  (`~/.local/share/Steam/userdata/<user>/config/grid/`): `<id>p.png` (the
  capsule, 600x900), `<id>.png` (the wide capsule, 920x430),
  `<id>_hero.png` (1920x620), `<id>_logo.png` and `<id>_icon.png`. `<id>`
  is the shortcut's app ID, its `appid` in `shortcuts.vdf` (a binary VDF,
  which `install.sh` reads with `python3` and never writes); for an entry
  without one, the ID Steam gives it (the CRC-32 of its `Exe` and
  `AppName`, with the top bit set). A picture there that the player put
  is kept: `install.sh` replaces only its own (listed with their SHA-256
  in `.steam-art` in the game's folder). A running Steam may not show new
  artwork until it restarts (on a Steam Deck, restart Steam or the Deck).
  Run `install.sh` again after adding the shortcut by hand to give it its
  artwork.
- On another Linux desktop, without `konsole`, `kdialog` or
  `steamos-add-to-steam`, it does the same and says what is left to do:
  the game asks for its disc image at its first start.
- The VR build's release (`OpenH1-Linux-VR.zip`, whose tree has
  `share/openh1/vr`) goes beside the other, so that a computer keeps both:
  in `~/Applications/OpenH1-VR`, with the menu entry
  `~/.local/share/applications/openh1-vr.desktop`, named OpenH1 VR (as the
  Steam Frame's), and a Steam shortcut of its own (its check looks for its
  own `bin/openh1`). Neither install changes the other's folder, menu entry
  or shortcut, and each refuses a folder that holds the other build.

- Build: the 64-bit x86 one, `OpenH1-Linux.zip`, which draws
  with Vulkan (SteamOS has it).
- Screen: the game fills the screen without borders (`display.mode` empty,
  thus borderless). The 3D view and the HUD have the shape of the screen
  (16:10) and are drawn at its resolution; the menus are at the center.
- Controls: Steam Input gives the game a virtual controller, which the game
  operates as the controller of the Xbox. With the template that Steam selects
  ("Gamepad With Joystick Trackpad"), the buttons, sticks and triggers have
  the functions of the same controls on an Xbox controller, the right
  trackpad operates as the right stick, and the back buttons (L4, L5, R4,
  R5) do nothing until you assign them in the controller settings of the
  game in Steam. With a template that makes a trackpad a mouse, the mouse
  aims in the game and moves the pointer in the menus.
- Text: names of profiles and gametypes use the keyboard of the game, which
  the controller operates. The text fields of the menus (the name and the
  password in Server Setup, and the password of a game in the server
  browser) open the keyboard of Steam. Type the text, then select Enter on
  that keyboard.
- Frame rate: with `display.vsync` off (the default), the game draws up to
  twice the display's refresh rate (`display.max_fps`), for the least
  latency; set `display.vsync = true` to show one frame for each refresh,
  following the refresh rate and the frame limit of Quick Access >
  Performance (40 to 60 Hz on the LCD model, up to 90 Hz on the OLED model),
  which spends less of the battery; with it, `display.low_latency` starts
  each frame just in time for its refresh, so that a press is on screen
  about 15 ms later at 60 Hz, not about 90 (refer to "Low latency with
  vsync"). The world is calculated at 30 Hz at all rates. Refer to "Frame rate". For a longer battery, set Video Setup >
  Battery Saver to Auto (on while the Deck is not charging) or On: 30
  frames each second at 60 or 90 Hz, 40 with the screen at 40 or 80 Hz, and
  the Classic look (refer to "Battery saver").
- Sleep: the clocks of the game do not count the time that the Deck sleeps,
  so the game continues from where it stopped. A network game does not
  wait: the host drops a machine that it has not heard from for 15 seconds,
  and while a Deck that hosts sleeps, the other players have no host.

## Files and folders

| Xbox drive | Folder |
| --- | --- |
| `d:\` | The data root: the folder that contains `maps/`. |
| `z:\` | `z/` in the save root. This folder contains the cache (approximately 800 MB of map data) and the saved games. |
| `u:\` | `u/` in the save root. This folder contains the user data. |

The save root is `paths.saves` in `config.toml`. If that setting is empty,
the save root is `$XDG_DATA_HOME/halo-linux` (usually
`~/.local/share/halo-linux`).

The game makes the folders when it needs them. Names of files and folders
are not case-sensitive, as on the Xbox.

These files are in the data root:

| File | Contents |
| --- | --- |
| `debug.txt` | The log of the game. At start-up, the game shows the data root in the terminal. A crash writes its report (the faulting address and the calls that led to it) here as well; the `reference address` line at the top of each session places those addresses in the build. |
| `init.txt` | Console commands that the game does at start-up. For example, `map_name levels\a10\a10` starts the first campaign level. |
| `tags/` | Sound tag files that replace the sounds of the maps (`audio.loose_sounds`). |
| `mods/` | Mods: folders of Halo PC tag files whose sounds, bitmaps, text and models play over the maps' on this machine, in the order of the main menu's Mods (`load_order.toml`). Refer to [docs/mods.md](../../docs/mods.md). |

The settings are in `config.toml` in the game's folder (the release's
folder, or the folder of `build/linux_x86_64/halo`), or in
`~/.config/halo-linux` for an installed game. Refer to "Settings" and
"Packaging". Internet play's MQTT brokers are in `brokers.txt`
(`network.brokers_file`): the one next to `config.toml`, if there is one,
else the release's `share/openh1/brokers.txt`.

If the game stops because of a fatal signal, the host writes the address,
the registers and the frame chain to the standard error. Where the address
is in the game's code, the report says so with the command that finds its
function in the guest image: `addr2line -e build/linux_x86_64/halo_guest.elf
<address>`.

## Controls

The keyboard and the mouse are a control scheme of their own for the player
of controller 1: each action has up to two keys or mouse buttons, which
Settings > Controls Setup (or `[controls]` in `config.toml`) changes. The
game adds the input of the first gamepad to controller 1. The other
gamepads operate controllers 2 to 4. With two or more players on this
computer (co-op, or split screen in a network game) and only one gamepad,
that gamepad is controller 2 (player 2) and the keyboard and mouse stay
controller 1. The gamepad changes controller only when none of its buttons
is held. The profile's button layout (Settings > Gamepads) is the
gamepads' only. Besides the Xbox's five, it has the Master Chief
Collection's Bumper Jumper: jump on the left bumper, melee on the right,
switching grenades on A, action and reload on B and the flashlight on X.
Fishsticks has zoom on the left trigger and melee on the right stick's
button, the grenade thrown with the right bumper, crouch on B and grenades
switched with the pad's left direction (which then does not strafe or turn).

Recon is the standard layout with action and reload on the right bumper and
grenades switched with the left bumper (the flashlight moves to X).

| Action | Keys and buttons (default) |
| --- | --- |
| move | W, A, S, D |
| aim | mouse (direct aim) |
| fire | left mouse button |
| throw a grenade | G |
| jump (and skip a cutscene) | space |
| crouch | left ctrl, C |
| melee | F, mouse button 4 |
| reload | R |
| action (pick up, hold to swap weapons, enter or leave a vehicle; never reloads) | E |
| change the weapon | mouse wheel, 1 |
| change the grenade | X |
| flashlight | Q |
| zoom | right mouse button, Z |
| show the scores (hold) | tab |
| pause menu | escape |
| screenshot | F10 |
| talk in voice chat (hold) | V |
| type a chat line to everyone | T |
| type a chat line to your team | Y |
| accept a party invite from a player of the game | J |
| while typing a chat line: to everyone, the team or the vehicle, in turn | Tab |

With a controller, or the touch controls' D-pad, D-pad Up opens the text
chat's on-screen keyboard (refer to "Text chat"), and D-pad Down accepts a
party invite (refer to "Parties").

Always: \` opens the developer console, F12 releases or captures the mouse,
F11 changes between fullscreen and window. Screenshot (default F10,
rebindable under Controls Setup > Actions, below Pause Menu; not on Android) saves a PNG of the completed
frame to `screenshots/` beside `maps/`, named `YYYY-MM-DD_HH.MM.SS.png` in
local time, and shows "Screenshot saved" with the file's name on screen.
Captures in the same second get a numeric suffix (`_2`, `_3`) so previous
screenshots are preserved. With HDR on, the picture is tone mapped to SDR,
as an SDR display shows the game in HDR. The game asks the host for it: the
host reads the frame from its renderer and writes the file
(`android/host/host_screenshot.c`).

One movement of the mouse wheel changes the weapon one time. A second
movement after a short pause changes it again.

In the menus, the mouse moves a pointer:

- The item below the pointer gets the focus.
- A left click selects the item. On a setting with values, a click on the
  left or right half changes the value. On a button in the key of a screen
  (for example "B = Back"), a click pushes that button.
- On the on-screen keyboard (a profile's name), a left click presses the
  key below the pointer, or pushes the "B =BACK" or "A =ENTER" legend.
- A right click goes back.
- The mouse wheel moves through the items.

The keyboard also operates the menus, with keys of its own: the arrow keys
(and W, A, S, D) move, space or enter selects, escape or backspace goes
back (escape resumes the game from the pause menu), delete deletes. When the game continues, the mouse aims again. A mouse button that you hold from the menu does not fire until
you push it again.

## Menus

The menus are the PC version's (Halo Custom Edition's): its main menu and
every screen it leads to (campaign, profiles, multiplayer with its server
browser, direct IP and server setup, the gametype editor, and the profile's
settings: controls, gamepads, mouse, audio, video, network and colour),
laid out as it has them. Their pictures are the high-res redraws; the few
that are not yet (3D renders, screenshots) are drawn from the Xbox's own
menus where it has the same, else a placeholder, listed in
`port/assets/menus/NON_HANDDRAWN.md`.

The campaign is wired: Campaign, on player 1's profile (the one last used),
continues its saved game, starts a level it has reached at a difficulty (New
Game), or loads or deletes any profile's saved game (Load Game). Many of the
other functions behind the screens are not wired to this game yet
(`port/assets/menus/UNWIRED.md`): the lists they fill (profiles, maps,
servers, key bindings) are empty, and the settings they change do not
change. The screens open, close and move between each other as the PC
version's. `display.menus = "xbox"` gives the Xbox's menus, which start every
kind of game.

The menus' text is in the language of the Custom Edition whose `loc.map` is
in `custom_maps` (French, German, Italian, Spanish and so on), as that
edition shows it: each of the PC version's texts is that `loc.map`'s
(`display.menu_language`). Without one, and for the lines the port adds,
it is English. The pictures with words in them (the screens' titles, the
main menu's items) are drawn in English.

Multiplayer > SPLIT-SCREEN CO-OP is the Xbox's cooperative play, which the PC
version does not have: two players on this computer play the campaign in
split screen. Player 1 is the player who chose it, on the current profile.
Player 2 then chooses a profile with their own controller (a gamepad), and
New Game's levels are those either profile has reached. Either player's
controller chooses the level and the difficulty. A co-op game does
not continue a saved game of one player. Multiplayer > ONLINE CO-OP hosts
the campaign over the Internet instead: it is Create Game > Internet with
the map list on the campaign's levels, and friends join by the invite link.

Network games have split screen too: up to 4 players on each computer. In
the game lobby, another controller presses START to join, and the new
player's profile is chosen on the ADD PLAYER screen that opens (with any
controller). With one gamepad, choose the lobby's ADD PLAYER first (X, or
E on the keyboard): until then that gamepad shares controller 1 with the keyboard. Two players on one profile get different names from the host. A
player's B in the lobby leaves the game alone, and the last player of the
computer leaves it for all of them. When all the players are on the host's
computer, START GAME starts the game without another computer, and a team
game does not need a player on each team.

The lobby, a network game's and a game of this computer alone's (PLAY WITH
BOTS, split screen), is laid out as Multiplayer is (`game/lobby_menu.inc`).
The players are in a column for each team, red's and blue's, or in one list
in a game without teams and in Infection, whose zombies are chosen as it
starts. Each row has the player's team's dot, name and a tag: HOST, BOT,
PARTY (a member of your party), OPENCE or OPENH1 (the host knows each
computer's; a player knows only its own). Your own row is framed. In each
column the host comes first, then your party's members together, then the
others, the bots last; a column with more players than rows says how many
more. Left and right (the arrow keys, the D-pad) put you on red's or blue's
team, as does a click on a column. The panel beside them has the next
game (its gametype, map, score to win and time; OPENH1 and its tick rate,
or LEGACY; OPENCE CAN JOIN, which only the host knows), the bots' rows of a
game of this computer alone (BOTS' COUNT, SKILL and TEAMS, as in Server
Setup), the countdown and how another player joins, and the host's GAME
SETTINGS (Server Setup, whose START GAME comes back to the lobby) and START
GAME (Enter, or A on the players). A player sees "Waiting for the host"
there instead. The footer has CHANGE TEAM, COPY INVITE (Y, or F: your
party's invite, or else the internet game's, copied; refer to "Invites from
anywhere"), ADD PLAYER and LEAVE. On a phone or tablet
the lobby is the list of players and buttons it was. After a game of this
computer alone the host picks the next one as in a network game (the map,
the gametype, Server Setup, then this lobby), not on the Xbox's Select Map
and Select Teams; a local game started by `debug.network_test` opens this
lobby too. In the game, each player's pause menu
opens on their part of the screen, and its LEAVE GAME is theirs: their part
of the screen stays until the game ends. A game under way shows its own
screen before JOIN GAME: players join there the same way, START or ADD
PLAYER then START, and JOIN GAME brings them all into the game.

In a multiplayer game, the pause menu (escape) has SETTINGS, which opens
the profile's settings while the game goes on, and for the host HOST
CONTROLS (below; in a game of this computer alone, END GAME).
There Network Setup has only its pages that work in a game (Map Sharing,
Multiplayer HUD with the text chat, and Streamer Mode), not INTERNET PLAY,
which would drop the game, nor the rows for the main menu.
END GAME ends the game as its time limit does: the players stay, and after
the carnage report the host picks the next map and gametype (PICK GAME).

HOST CONTROLS is the host's of a network game (internet or LAN), laid out as
the server browser is, over the game dimmed and without its HUD, and in the
lobby too (its HOST CONTROLS, in the details panel, before the game starts),
with six tabs (BOTS, the last, not in co-op):

- PLAYERS: everyone in the game, with their team, score, ping and a tag
  (HOST, BOT, OPENH1 or OPENCE; in a Legacy game the host cannot tell OpenH1's
  players from OpenCE's). For the player chosen: KICK, BAN... (then BAN FOR
  AN HOUR, BAN FOR A DAY or BAN FOR GOOD, with ADD A REASON to type why
  first), MOVE TO RED or BLUE, and MUTE... (then for 10 minutes, an hour or
  a day; their text chat and voice, for everyone) or UNMUTE. The host's own
  players are never kicked, banned or muted; KICK on a bot takes it out, one
  bot fewer (`bots.count`), as BOTS's REMOVE BOT does. In the lobby every player can be moved, OpenCE's
  too. In a game a team move is offered in an OpenH1 game of teams with no
  OpenCE player: the player dies (no death counted) and respawns on the
  other team.
- GAME: END GAME NOW and RESTART ROUND (each pressed twice; a restart starts
  the same map and gametype again after a few seconds in the lobby), CHANGE
  MAP... (the MAPS tab; its value the game that plays next), CHANGE
  RULES... (RULES, below), SHUFFLE TEAMS
  (at random, as many on each) and BALANCE TEAMS (as many on each, moving as
  few as it can; in a game, by score too), each pressed twice in a game,
  where those moved die and respawn; ANNOUNCE... (type a message every
  player sees, as a dedicated server's notice; in a game only), SCORE LIMIT
  and TIME LIMIT (left and right: in the lobby they change its game at once;
  during a game they wait for the next one, which the row says), PASSWORD (a
  public internet game's, typed: at once, for new joins), LOCK TEAMS
  (players cannot switch teams in the lobby; the host still moves them),
  ALLOW VOTEKICK (`network.votekick`), LISTING (a public internet game's
  PUBLIC or PRIVATE), MAX PLAYERS (left and right; never fewer than are in
  the game) and ALLOW OPENCE PLAYERS (an OpenH1 game's, for new joins).
- RULES (GAME's CHANGE RULES...; B goes back to GAME): the gametype's rules,
  in the lobby (its game, for every machine) and during a game. VEHICLES (a
  team game's RED VEHICLES and BLUE VEHICLES: default, none, Warthogs,
  Ghosts, Scorpions, Rocket Warthogs, Banshees, Gun Turrets, all the map's,
  and the gametype's own custom counts if it has them) swaps them at once:
  every vehicle nobody rides goes and the new set's are placed at the map's
  vehicle spawns, as the map places them when a game starts (a set the map
  has no spawns for places none); a vehicle somebody rides stays until it is
  empty, then goes, and the new vehicle whose spawn it holds comes once the
  spawn is clear. STARTING WEAPONS (the map's, or CUSTOM's PRIMARY WEAPON and
  SECONDARY WEAPON) and WEAPON SET take effect at each player's next spawn
  (those alive keep theirs) and for the weapons that spawn on the map from
  then. SHIELDS, MOTION TRACKER, INFINITE GRENADES and INVISIBLE PLAYERS
  change at once in an OpenH1 game with no OpenCE player (every OpenH1
  machine is told); in a Legacy game, or one with OpenCE players, they wait
  for the next game, which the row says (NEXT GAME). SAVE AS GAMETYPE...
  (type a name, Enter saves) keeps the rules as your gametype, which MAPS
  and the playlist offer (`saved=<name>`). The game's record keeps the
  changes: a player who joins plays by them, and the next game starts with
  them unless the playlist has another. Not in co-op.
- MAPS: the gametype (left and right step it: the built-in gametypes,
  OpenCE's and those you saved, Create Game's CUSTOM, which entries name
  `saved=<name>`: its letters and digits, lowered, `_` between them, as
  `saved=my_ctf_2` for "My CTF #2"), then every multiplayer map, the Xbox's and Custom Edition's
  (`custom_maps`; map sharing gives them to the players without them). For
  the map chosen: PLAY NOW (in a game, pressed twice: the game ends with no
  scores, and every machine goes through the lobby to the new one), AFTER
  THIS GAME (its scores, then the lobby with it, which starts by itself)
  and ADD TO PLAYLIST. In the lobby PLAY IN THIS LOBBY sets the lobby's game.
- PLAYLIST: the games `network.host_playlist` holds, the next marked, and
  first a change asked for (NEXT, with CANCEL CHANGE). After each game's
  scores the next one is set in the lobby and its countdown starts; a change
  asked for comes first, and the playlist goes on after it. For the game
  chosen: PLAY NOW, AFTER THIS GAME, REMOVE and CLEAR PLAYLIST (pressed
  twice). The lobby shows the game after the next one (THEN), and every
  player is told the next game as the scores show.
- BANS: `bans.txt`'s players, when they were banned and why, and UNBAN.
- BOTS: the bots' settings, each written to `config.toml` as it changes
  (as Server Setup's rows are), which the game takes at once (refer to
  "Bots"), then the bots playing (their team and score), and for the bot
  chosen REMOVE BOT (one bot fewer: `bots.count` one fewer than play).
  BOTS (left and right, none to 15, no further either way): in the lobby
  the bots join and leave at once; in a game a bot joins as a player who
  joins a game in progress, one every few seconds, and leaves as a player
  who quits. SKILL (Easy, Normal, Heroic, Legendary, `bots.skill`) and
  TEAM TACTICS (`bots.tactics`) change every bot's play at once. TEAMS
  (EVEN or AGAINST YOU, `bots.teams`) places the lobby's bots at once; in a
  game, the bots that join from then, and BALANCE TEAMS (a team game's,
  Game's own, pressed twice in a game) moves those playing. MAKE ROOM FOR
  PLAYERS (`bots.make_room`) and, with it on, FILL TO (`bots.fill_to`)
  take effect at once too: bots leave or come back to fill the game to it.
  In a gametype the bots do not play the rows are there, and apply from
  the next Slayer game, which the panel says. A matchmaking game's count,
  room and fill are its playlist's, and stay; its bots are not removed.
  Nothing new goes to the other machines: a bot is a player to them.

The host's console has the same: `host_map <map> [gametype]` plays a game
now (as PLAY NOW; in the lobby, at once), `host_map_next <map> [gametype]`
after this game, `host_playlist [entries]` shows or replaces the playlist,
`host_playlist_add <map> [gametype]`, `host_playlist_remove <number>` and
`host_playlist_clear` change it (entries as `server.rotation`'s:
`bloodgulch:ctf+score=5`, `custom_maps\<name>`), `host_say <words>` (and the
chat's `/say <words>`) tells everyone, `host_shuffle_teams` and
`host_balance_teams` arrange the teams, `host_score_limit <score>` and
`host_time_limit <minutes>` set the limits, `host_bots [<0 to 15>]` shows or sets the bots'
count and `host_bot_skill <easy|normal|heroic|legendary>` their skill (as BOTS's rows),
`host_vehicles <default|none|warthog|ghost|scorpion|rocket_warthog|banshee|turret|pc> [red|blue]`
sets the vehicles and `host_weapons map` or `host_weapons <primary> [<secondary>]` (`none`,
`random`, `assault_rifle`, `pistol`, `shotgun`, `sniper_rifle`, `rocket_launcher`,
`plasma_pistol`, `plasma_rifle`, `needler`) the starting weapons (as RULES's rows), and
`ban <player> [30m|2h|7d|1w|forever]
[reason]` bans for a while and says why (the duration with its unit).
A co-op game has none of the map changes and no playlist: its next game is
the campaign's next level. A list of more than 512 maps leaves the rest out,
and MAPS says how many. Nothing new goes to the other machines: a map change is the lobby's own, a
notice OpenCE knows, so OpenCE players in a Legacy game see it all as any
host's.

The scoreboard's player menu (a right click on a name) offers the host the
same: Mute for everyone, Kick, Ban (picked twice), Move to the other team,
and Host controls..., which opens Host Controls with the player chosen. The
console's `host_controls` opens it too (`host_controls game`, `host_controls
bans`, `host_controls bots`, `host_controls rules`, `host_controls players <name>`), and `host_controls end` and
`host_controls restart` end or restart the round, and `host_controls unban
<name>` unbans. Clients have none of it.

The carnage report after a game is laid out as Settings is: a line with the
gametype, map and length, the result (each team's score with the winner
marked, or the free for all's winner), and one table of the players with
their place, score, kills, assists, deaths, kills per death, suicides and (in
a team game) betrayals, your own row lit and each team's totals at its
foot. These are the statistics the game keeps of each player; it keeps no
accuracy or sprees. Enter (A) picks the next game and Esc (B) leaves, as
before. The in-game scoreboard has the same rules and lit row, no Ping
column in a game of this computer alone, and its prompts in a footer;
players' names over their heads fade out while it shows.

The port's own screens use the screen's width up to 16:9: Settings,
Multiplayer, the Server Browser (and the LAN list and Join by Link) and the
carnage report. Their lists start at the screen's left and their panels end
at its right, each wider by what the screen adds: Settings' rows and help
panel, the browser's Server, Map and Mode columns and its details panel,
Multiplayer's items and panel, the carnage report's columns, and the
scoreboard's names. The tabs spread across the screen, and the key prompts
keep to its edges. A wider screen (21:9 and up) has the 16:9 layout in its
middle; a 16:10 screen (the Steam Deck) a little less than 16:9's; 4:3 the
640 as before. The PC version's own screens stay in the middle 640. The
layout follows the mouse, the keys, the highlights and the scrolling, and a
part of a split screen narrower than the menus (three or four players) has
the 640's (`port/assets/menus/README.md`, `wide`).
LEAVE GAME of the host still ends the game for everyone. The buttons go into
the map's own pause menu, before LEAVE GAME, so the buttons of a custom map
stay. The Xbox's pause box is drawn taller to hold them (a redraw,
`port/assets/menus/port_svg/pause`); a custom map's box keeps its size, and
what is below its list moves down. The few pictures of the settings that
come from the main menu's map are not drawn there.

In a single-player campaign, the pause menu has SETTINGS too, before REVERT
TO SAVED, in the same box: a list with room for it centres its rows, and
one without keeps its size, its rows closer. It opens the same settings,
while the game stays paused, with only Controls, Gamepads, Mouse, Audio and
Video Setup; Gamepads' OK saves the profile at once. In co-op the settings
have Network Setup too, with its in-game pages as in a multiplayer game.

In an online co-op game, the pause menu also has FRIENDLY FIRE ON or
FRIENDLY FIRE OFF, after SETTINGS, in a box drawn taller for it: whether this
computer's players hurt, and are hurt by, the other players
(`network.coop_own_friendly_fire`). A or enter changes it, from that moment.

The menus are XML files in `port/assets/menus` (`tools/ce_menus.py` writes
them from the PC version's tags), which the game contains. To change them,
put files in a `menus` folder next to `config.toml`: a file with the same
path replaces one of the game's, and another `.xml` file is added.
`port/assets/menus/README.md` describes the files. If a file has a problem,
the game uses the Xbox's menus and the log names the file, the line and the
problem.

## Bots

Computer players (bots) can play a local multiplayer game with you:
`bots.count` (1 to 15) of them join the lobby of a Slayer game played on
this computer. In the menus, Multiplayer > PLAY WITH BOTS (under Create
Game) hosts one: the map, the gametype, then Server Setup, which has three
rows for them in the place of the network's rows (GAME, INVITE LINK, MAP
SHARING), BOTS (NONE, 1 to 15), BOT SKILL and BOT TEAMS (EVEN or AGAINST
YOU). Each row writes its setting to `config.toml` as it changes, so the
next game starts with them too. The host of a network game changes them
while it plays too, in Host Controls' BOTS tab (refer to "Host Controls"). For a gametype the bots do not play (CTF,
Oddball, King, Race) the rows are dimmed and passed over, and the line
below them says so. START GAME opens the lobby with the bots in it, and
more players on this computer join it with START, as in a network game. In
the Xbox's menus (`display.menus = "xbox"`), Multiplayer > Split Screen is
the game, with the settings as `config.toml` has them (the Xbox's menus
have no rows for them). They are players as you are: in the scores, the respawns
and the game's rules, on the teams that `bots.teams` asks for, and they
follow the teams' changes in the lobby. They look for enemies, aim, fire,
throw grenades and pick up better weapons, as well as `bots.skill` lets
them, and walk the paths that the campaign's AI finds on the maps' walkable
surfaces (`game/bots.c`). They play no other gametypes (but SWAT, Infection
and Gun Game, which are Slayer's: below).

They aim as people do, not as a machine: a bot's aim turns as a hand moves
it (an easy bot flicks past its target and comes back, a legendary one
settles on it), never settles to a perfect shot (a slow wobble of its
skill's size, and good and bad stretches), flinches when it is hit (more
when its shield pops), is caught out for a moment when its target strafes
the other way or jumps, trails a target that crosses its view fast, and
fires while its aim still closes on the target, so some shots miss. It aims
where its weapon is best: the sniper rifle and the pistol at the body to
bring the shield down and at the head once it is down, the rocket launcher and the fuel rod
gun at the feet, anything else at the body; the lower the skill, the less
often it goes for the head. In a fight they play as people do: a grenade
to open on a whole shield, a grenade then in for a blow, the pistol's
shots to bring the shield down then a blow, a blow at the reach the game's
melee has; they strafe one way while the enemy's aim is off them and turn
about when it comes on them (as quickly as their skill reacts, the easy
ones not always), now and then with a jump, and crouch to fire at range
while no one aims at them.

A bot knows only what a player in its place could (`game/bots_knowledge.c`):
the enemies its own sight saw and, with `bots.share_sightings`, those its
teammates saw, a callout's delay later (`bots.share_delay`); it goes and
looks where they were, but has no motion tracker of others' places and fires
only at what it sees. It knows the map's item spawns and times them by the
game's clock as a player can, and believes an item is there as it last saw
the spawn. So it backs off to cover to let its shield come back when a fight
goes against it, goes for a health pack when hurt, goes for the power
weapons and powerups (and waits at a spawn for one about to come back,
leaving it to a teammate who goes for it), swaps to the weapon better for
the range, keeps near its teammates now and then, and looks towards an
enemy it knows of as it walks. With `bots.tactics` (off by default) a team's
bots also play together (`game/bots_tactics.c`): spacing, crossfire, focus
fire, trading for a fallen teammate, closing in and falling back together,
and sharing out the power items and the escort of a human teammate. Measured
on Team Slayer with bots alone, the team play slowed the fights and did not
win more, so it is a choice, not the default.
halo.log has a line each minute with each weapon family's rounds that hit
someone of those the bots fired, and `debug.bots_weapon` gives every bot
one weapon, to measure it.

Bots play system link and internet games too, OpenH1 and Legacy, public or
private: Create Game's Server Setup has a BOTS row after the gametype's
rows, its value what they are ("10, NORMAL, FILL TO 16", NONE, or NOT IN
THIS GAMETYPE, dimmed, for a gametype they do not play), and A opens their
screen: BOTS, BOT SKILL, BOT TEAMS, MAKE ROOM (ON or OFF) and, with it on,
FILL TO (2 to 16), written to `config.toml` with OK. The host plays them:
each bot is a player of a machine of its own with no connection, whose
input the host's tick gives the others with every other player's, so every
machine sees an ordinary player, OpenCE's in a mixed game too, and an OpenH1
game's bots move at its rate (NETCODE.md, "Bots in network games"). With
MAKE ROOM on (`bots.make_room`, the default) the bots make room for people:
no more of them play than leave the game `bots.fill_to` players and bots
(and its MAXIMUM PLAYERS), so a player who joins a game the bots fill takes
a bot's place at once, in the lobby or in the game (the bot of the team
with more players, the lowest scoring), and a bot comes back a few seconds
after a player leaves. With it off, the bots stay, and MAXIMUM PLAYERS
counts the players alone: a game of 16 with 10 bots takes 16 people. The
server browser and the local network's list count the people
("6/16", the panel "6/16 · 10 BOTS"), the scoreboard's ping column says BOT
for a bot, and the carnage report names it "(Bot)". The bots stay from game
to game, and go with the host's game; the welcome to OpenCE players goes to
real machines alone.

## OpenCE gametypes

Create Game's Select Gametype screen has a bank between STANDARD and
CUSTOM: OPENCE, the port's own gametypes (`game/game_modes.c`).

| Gametype | Rules |
| --- | --- |
| SWAT | Team Slayer with no shields, in which a shot to the head of any weapon kills (a bullet's, a plasma bolt's, a needle's or a shotgun pellet's, not a melee's or an explosion's). Everyone starts with a pistol and an assault rifle; no weapons are on the map, no one has grenades, and there is no motion sensor. |
| Infection | Team Slayer. Humans are red and keep the map's weapons; zombies are blue, carry the Oddball skull and nothing else, move faster, hit harder with the skull, and cannot drive, pick up grenades or powerups. Once two players are alive the round counts down, then the host chooses the first zombies (a human is always left). A human killed by a zombie, by themselves or by a fall becomes a zombie, one killed by a teammate does not; a zombie respawns a zombie, and a player who joins late is one. The zombies win when no human is left, the humans when the round's time runs out (5 minutes). Each player scores a kill of the other side. The rules follow pull request #420's (`game/infection.c`). |
| Gun Game | Free for all Slayer. Everyone starts with the first weapon of a list (the rocket launcher, the shotgun, the sniper rifle, the plasma rifle, the assault rifle, the needler, the plasma pistol, the pistol, of those the map has); each kill gives the killer the next, and a kill with the last wins. No one picks up a weapon, a weapon emptied is refilled, and a melee kill takes the victim back a weapon (`game/gun_game.c`). |
| Hardcore Slayer, Hardcore CTF, Hardcore Oddball, Hardcore King | The team gametypes with Hardcore's rules: no motion sensor (the flag, the ball and the hill keep their nav points), no aim assist (no autoaim nor magnetism, for controllers and the mouse alike), friendly fire on, and a kill restores the killer's health, and their shields unless an overshield has them past full. |

Edit Gametypes edits them as any gametype, and its OPENCE OPTIONS screen
(after TEAMPLAY OPTIONS) gives any gametype these rules: its MODE (NONE,
SWAT, INFECTION or GUN GAME: Slayer only; Infection is played on teams and
Gun Game free for all, which the screen's OK sets), HARDCORE (any game
type), and the mode's own options: Infection's first zombies, zombie speed,
skull damage, zombie respawn time, countdown and zombie shields, and Gun
Game's weapon order (strongest first, weakest first, or random, drawn by
the host for each game) and whether a melee kill demotes. A saved gametype
keeps them with its PC options (`saved games/playlist_profile.c`, version 2
of their block; a gametype saved before them has none), and the network
game carries them to every machine (network version 35, `NETCODE.md`).
Only the host decides each mode's events (Infection's zombies, Gun Game's
weapons, Hardcore's restores); the clients take them from it as they take
the rest of the game. The lobby and Server Setup name the gametype's mode.
`debug.network_test` hosts them by name: `swat`, `infection`, `gun_game`,
`hardcore`, `hardcore_ctf`, `hardcore_oddball` and `hardcore_king`
(`host:bloodgulch:infection`).

Three more options of OpenCE's (its pull request #467, by s3anyboy) are in
the gametype editor's own screens:

- Capture the Flag's KILLS SCORE and KILLS PER CAPTURE: with Kills Score
  on, each kill is a point for the killer's team (a betrayal takes one
  away) and a capture is worth Kills per Capture (5 to 100, 25 by
  default); CAPTURES TO WIN stays in captures, so a game of 3 captures at
  25 is won at 75 points. Off, Capture the Flag scores captures alone. A
  gametype of OpenCE's earlier CTF/Slayer type is Capture the Flag with
  Kills Score on.
- King of the Hill's MULTI-HILL: 2 to 5 hills at once (as many as the map
  has), any of which a player holds scores; they move together with MOVING
  HILL. OFF is the one hill.
- Teamplay Options' FORCE TEAM SPAWNS: a team game other than Capture the
  Flag spawns each player only at their own team's Capture the Flag spawn
  points (not on a map without them for both teams).

They are kept in the gametype (Kills Score and Multi-Hill in its game
type's options, Force Team Spawns in its PC options), so a saved gametype
and Host Controls' `saved=<name>` keep them, and the network game carries
them to every machine in fields the earlier versions left empty: network
version 39 is unchanged, and OpenCE's builds play them with this one. The
host sends Multi-Hill's other hills after King of the Hill's state, as
OpenCE's host does. A dedicated server's gametype files and the rotation
take them as `kills_score`, `points_per_capture` (`[ctf]`), `multi_hill`
(`[king]`) and `force_team_spawns` (`[options]`):
`bloodgulch:ctf+kills_score=true+points_per_capture=10`.

## Settings

The settings are in `config.toml` in the game's folder (the release's
folder, or `build/linux_x86_64/config.toml`; `~/.config/halo-linux/config.toml` for
an installed game: refer to "Packaging"). At the first start, the game writes the file
with a comment for each setting and the setting commented out at its
default (`# vsync = true`): a commented setting follows the default, also a
newer version's. To keep a value of your own, take the `# ` out (or change
it in the Settings menu, which does that). A newer version adds its new
settings the same way. To get the default values again, delete the file.

A file written before this (without the note "A setting commented out ...
is at its default" in its head) keeps its values; the game adds the note,
and a value that was only the older versions' default and has changed
since becomes the new default, once: `vr.hands = "floating"` becomes
`# hands = "arms"`.

The game reads the file at start-up. If a key is not correct, or a value
has the wrong type, the game writes the line to the log and uses the default
value. A key in the wrong section is named with the section it goes under. A
file that cannot be parsed leaves every setting at its default; a word value
needs its quotes (`menus = "pc"`, not `menus = pc`). The Settings menu (Video, Mouse, Audio, Network and Controls Setup)
changes the useful settings, writes them into the file (only their lines
change) and applies them at once, but `audio.enabled`, and `display.menus`
from the next main menu.

Each setting has an environment variable. The environment variable changes
the setting for one start of the game. It has priority over the file.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `display.mode` | `""` | `HALO_DISPLAY_MODE` | `"fullscreen"`: the display, taken at the mode of `display.resolution` (the nearest the display has), or at its desktop mode. `"borderless"`: a window over the whole desktop, whose mode does not change. `"windowed"`: a window of `display.window_size`. Empty: `display.fullscreen` decides (`true`: borderless). F11 changes between the window and the fullscreen mode. Video Setup sets it. |
| `display.fullscreen` | `true` | `HALO_FULLSCREEN` | `true`: borderless, as `display.mode = "borderless"`. `false`: a window, as `display.mode = "windowed"`. Used when `display.mode` is empty. |
| `display.resolution` | `"native"` | `HALO_RESOLUTION` | What fullscreen and borderless draw at: `"native"`, the display's own resolution, or `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. Fullscreen sets the display to it. Borderless draws at it and scales the picture to the display, where the display has room for it. The picture has 480 lines of the game and the width of the resolution's shape. A window draws at its own size instead. Video Setup's Resolution sets it, from the display's modes; it shows with Fullscreen and Borderless. |
| `display.resolution_scaling` | `"native"` | `HALO_RESOLUTION_SCALING` | `"native"`: the game draws at the resolution of the window, or of the display (or `display.resolution`) fullscreen. `"original"`: the game draws the 640x480 picture of the Xbox and scales it up to the window or the display, whatever `display.mode` is. Video Setup sets it. |
| `display.window_size` | `""` | `HALO_WINDOW_SIZE` | The size of the window, as `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. You can change the size of the window; the game's picture takes its shape. Empty: `display.window_scale` decides. Video Setup's Window Size sets it, from sizes of each shape (4:3, 16:10, 16:9 and 21:9) that fit the desktop; it shows with Windowed. |
| `display.window_scale` | `2` | `HALO_WINDOW_SCALE` | Used when `display.window_size` is empty: the size of the window, as a multiple of 640x480. |
| `display.vsync` | `false` (Linux, Windows, macOS); `true` (iOS, Android) | `HALO_NO_VSYNC=1` sets `false` | `true`: each frame waits for the display. `false`: as many frames as `display.max_fps` allows (twice the refresh rate by default). With Vulkan, `false` presents in mailbox mode where the display has it (no tearing), else immediately. On macOS, `false` shows the newest frame at each refresh, with the older one dropped (no tearing). The VR mode's headset paces itself. With Vulkan and `true`, `display.low_latency` starts each frame just in time for its refresh. |
| `display.low_latency` | `"auto"` | `HALO_LOW_LATENCY` | With vsync on (Vulkan: Linux, the Steam Deck, Windows, Android), the game starts each frame as late as it can and still have it on screen at the next refresh, its input read then, and never queues frames ahead of the display. `"on"`, `"off"`, or `"auto"`: on where it was measured, Linux and the Steam Deck (Windows and Android: off until measured there). It needs a driver that says when each frame reaches the screen (`VK_KHR_present_wait`, or `VK_GOOGLE_display_timing`); without one it does nothing. The Mac's frames are paced by its display already. Refer to "Low latency with vsync". |
| `display.max_fps` | `0` | `HALO_MAX_FPS` | With vsync off, the most frames each second. `0`: twice the display's refresh rate. `-1`: no limit, which can hang some Intel graphics (Raptor Lake), resetting the desktop's graphics too. A press while a frame waits for its time (a key, a mouse or controller button, a trigger or stick pushed or let go, the mouse moved after it was still) begins the next frame at once, so that it is read then: one frame more for each press, at most one an interval; the frame after waits for its time as before. Not the Mac's, whose frames wait for the display. |
| `display.battery_saver` | `"off"` | `HALO_BATTERY_SAVER` | Draw less, for a longer battery and a cooler machine. `"on"`: each frame stays on screen for a whole number of the display's refreshes, 30 to 60 frames each second (60 at 60 or 120 Hz, 45 at 90 Hz), and the settings `display.graphics` chooses that are commented out are drawn at their Classic values; those set keep their own. `"auto"`: on while the machine runs on its battery, off on its charger, as SDL says (a machine without a battery, or one that cannot tell, is taken to be on its charger). `"off"`: never. Not in VR. The game plays the same. Video Setup > Battery Saver. Refer to "Battery saver". |
| `display.graphics` | `"enhanced"` | `HALO_GRAPHICS` | The look of the 3D view, as one choice. `"classic"`: as the Xbox drew it. `"enhanced"`: the port's improvements together, each as far as the machine's GPU does it well. It chooses `display.anti_aliasing`, `display.anisotropic_filtering`, `display.shadow_resolution`, `display.per_pixel_lighting`, `display.model_detail` and `display.dynamic_lights` where they are commented out; each of them set wins over it. Video Setup > Graphics > Preset. Refer to "Classic and Enhanced". |
| `display.anti_aliasing` | `display.graphics`'s | `HALO_ANTI_ALIASING` | The smoothing of jagged edges, which the Xbox did not have. `"off"`: none, as on the Xbox. `"fxaa"` or `"smaa"`: a pass over the 3D view after the game draws it. The HUD and the menus stay sharp. SMAA is the sharper and costs more. `"ssaa2x"`: the game draws at two times the resolution in each direction (at most the GPU's largest texture), and the picture is scaled down. The GPU does four times the work. Not with `display.resolution_scaling = "original"`. `"msaa2x"`, `"msaa4x"` or `"msaa8x"`: each pixel of the 3D view has that many samples (at most the GPU's). On Android, macOS and iOS `"ssaa2x"` gives none. On iOS `display.graphics = "enhanced"` gives `"msaa4x"` (port/ios/README.md). The VR mode's eyes have neither pass. A change applies from the next frame. Refer to "Anti-aliasing" in "What operates". Commented out, `display.graphics` chooses it (refer to "Classic and Enhanced"); set, it wins over `display.graphics`. |
| `display.prepare_shaders` | `true` | `HALO_PREPARE_SHADERS` | `true`: as a map loads, its first frame (and the first of each of its structures) is drawn unseen through every shader its tags hold, in each way the game's code draws them, so that the renderer makes their shaders and pipelines during the loading screen, not in play (`port/assets/render/README.md`, "A map's enumeration"). `false`: each is made the first time a draw needs it, which the draw waits for. |
| `display.interpolation` | `true` | `HALO_INTERPOLATION` | `true`: one frame for each refresh of the display. `false`: 30 frames each second, as on the Xbox. Refer to "Frame rate". |
| `display.direct_camera` | `true` | `HALO_DIRECT_CAMERA` | `true`: in first person, on foot, the view points where the player aims in each frame, not where the last tick left it; and (not in the VR mode) the view stands where the newest step left the player, and the gun and hands show the newest step's pose (a shot, a step, a reload seen 33 ms sooner). In a vehicle's seat (first or third person, not in the VR mode) the vehicle, its riders and the view from it are drawn as the vehicle's newest step left it, the view aimed where the player aims. On Android and iOS too, with the touch controls' swipe and gyroscope. Refer to "Frame rate". |
| `display.fov` | `0.0` | `HALO_FOV` | The first-person view's field of view on foot, in degrees across at 16:9, from 20 to 150. `0`: the stock view. Refer to "Field of view". |
| `display.viewmodel_fov` | `0.0` | `HALO_VIEWMODEL_FOV` | The first-person weapon's and hands' field of view, in degrees across at 16:9, from 20 to 150. `0`: the weapon's stock view, also when `display.fov` widens the world. Refer to "Field of view". |
| `display.viewmodel_visible` | `true` | `HALO_VIEWMODEL_VISIBLE` | `true`: the first-person weapon, hands and what is attached to them are drawn. `false`: they are not; firing, animation, sound and lights go on, and other players' models are drawn. |
| `display.passenger_third_person` | `false` | `HALO_PASSENGER_THIRD_PERSON` | Third-person view for the Warthog's passenger and the Scorpion's four passengers. Uses the gunner's camera on the Warthog and the driver's on the Scorpion, within the passenger's firing arc. Smooth entry and exit; weapon zoom is disabled while riding. The reticle follows the shot's centre ray. Local view only. In the campaign and co-op only: not in multiplayer, where seeing around the vehicle would be an advantage. Never in VR, which stays in first person in every seat. |
| `display.viewmodel_shield` | `false` | `HALO_VIEWMODEL_SHIELD` | `true`: the energy shield's flare (its hits, its charging, its fall) is drawn on the first-person arms too, as on the body, with the unit's own shield shader, kept closer to the arms than to the body. The stock game draws it on the body only. |
| `display.first_person_legs` | `false` | `HALO_FIRST_PERSON_LEGS` | `true`: your own body is drawn from the waist down in first person, so you see your legs when you look down, moving as the body's animation moves them. Refer to "Field of view". |
| `display.shield_color` | `default` | `HALO_SHIELD_COLOR` | Your energy shield's flare as this machine draws it: `default` (the shield's own) or a multiplayer armor color (`white`, `black`, `red`, `blue`, `gray`, `yellow`, `green`, `pink`, `purple`, `cyan`, `cobalt`, `orange`, `teal`, `sage`, `brown`, `tan`, `maroon`, `salmon`), whatever the armor's, on the body and, with `display.viewmodel_shield`, on the first-person arms. The shield's own shader is drawn with its two colors replaced at their brightness; the flare is added to what is behind it, so `black` is drawn a faint grey. Only this machine sees it. |
| `display.high_res_hud` | `true` | `HALO_HIGH_RES_HUD` | `true`: the HUD (meters, counters, panels and their outlines, the motion sensor, reticles, waypoints, scopes) is drawn from the high-res assets in `port/assets/hud`, 8x the size of the maps' bitmaps; the message icons' pictures of the weapons and the skull from Aerocatia's and Jessie's art (`port/assets/hud/NOTICE.md`). Selected labels and messaging sheets (their text) are upscaled from the maps' own pixels without changing their layout. `false`: the maps' own bitmaps. |
| `display.hud_button_style` | `classic` | `HALO_HUD_BUTTON_STYLE` | Gameplay controller icons while High-Res HUD is on: `classic` (plain Xbox faces), `gems` (shaded Xbox faces), or `menu_gems` (Dark Gems). |
| `display.high_res_text` | `true` | `HALO_HIGH_RES_TEXT` | `true`: the menus' and HUD's text is drawn with the fonts in `port/assets/fonts` (Overpass, in place of the maps' Interstate) at the resolution the game draws at, laid out as before; the menus' titles are drawn from the high-res pictures in `port/assets/titles`, and the controller button icons from those in `port/assets/buttons`. `false`: the maps' bitmap fonts, titles and button icons. |
| `display.shadow_resolution` | `display.graphics`'s | `HALO_SHADOW_RESOLUTION` | The size of the maps that the shadows of the objects are drawn in, in pixels each way: `128`, `256`, `512` or `1024` (other values go down to one of these). The game draws the shadow of each object into a map of 128x128 pixels, blurs it and projects it onto the ground. On a large screen, the edges of these shadows show steps that move when the object moves. A larger map makes the edges smooth; the blur is made wider to match, so the shadows are as soft as on the Xbox. Each doubling adds two passes of the blur, which up to `512` are drawn once a frame for all the objects (the shadow atlas: 2048x2048 at `512`, two of them, and nine maps, 41 MB of the GPU's memory; 10 MB at `256`). `128`: as on the Xbox. Commented out, `display.graphics` chooses it (refer to "Classic and Enhanced"); set, it wins over `display.graphics`. |
| `display.menus` | `"pc"` | `HALO_MENUS` | `"pc"`: the PC version's menus, from the files in `port/assets/menus` and a `menus` folder next to `config.toml`. Refer to "Menus". `"xbox"`: the Xbox's menus. |
| `display.menu_language` | `"maps"` | `HALO_MENU_LANGUAGE` | The PC menus' language. `"maps"`: the language of the Custom Edition whose `loc.map` is in `custom_maps` (English without one). `"english"`: English. The port's own lines are English. |
| `display.player_names` | `"all"` | `HALO_PLAYER_NAMES` | In multiplayer, whose names are drawn above their heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. An ally's name is drawn above the triangle the game shows over teammates. An enemy's name shows only within the motion sensor's reach, while the enemy is in sight and not camouflaged, so it never shows where an enemy hides. The gametype's motion tracker setting also applies: no names if it shows no players, only allies' if it shows only friends. |
| `display.player_name_scale` | `0.75` | `HALO_PLAYER_NAME_SCALE` | How large the players' names are drawn: `1.0` is three quarters of the size of the HUD's text, from `0.25` to `4`. With high-res text, larger names are rasterized at their size, so they stay sharp. |
| `display.match_timer` | `false` | `HALO_MATCH_TIMER` | `true`: a multiplayer game draws its clock at the bottom right, counting up from the game's first tick, which the items' respawn periods count from too: a weapon is back on the map at every multiple of its period on the clock. Campaign and co-op have none. MATCH TIMER in Settings > Interface, under Multiplayer. |
| `display.text_chat` | `true` | `HALO_TEXT_CHAT` | Show the text chat of network games (Settings > Network Setup > Multiplayer HUD > TEXT CHAT). `false` hides its lines, and its keys and a controller's D-pad Up open nothing. Refer to "Text chat". |
| `display.chat_style` | `"colors"` | `HALO_CHAT_STYLE` | The text chat's colours (Settings > Network Setup > Multiplayer HUD > CHAT TEXT): `"colors"`, the player's name in their team's colour and the text a pale blue; `"white"`, all of it white, as Custom Edition. |
| `display.scoreboard_team_layout` | `"teams"` | `HALO_SCOREBOARD_TEAM_LAYOUT` | How the multiplayer scoreboard (hold BACK, or tab) lists a team game's players. `"teams"`: a column for each team, red on the left and blue on the right. `"score"`: all the players in order of score. With more players than fit, the mouse wheel and Page Up / Page Down scroll the scoreboard. |
| `display.scoreboard_background` | `true` | `HALO_SCOREBOARD_BACKGROUND` | `true`: the multiplayer scoreboard (hold BACK, or tab) has a panel behind its text, for clearer text. |
| `display.scoreboard_background_color` | `"6, 20, 48, 190"` (the menus' dark blue) | `HALO_SCOREBOARD_BACKGROUND_COLOR` | The color of the scoreboard's panel: `"red, green, blue, alpha"`, each from `0` to `255`. Alpha `0` is see-through, `255` is solid. |
| `display.anisotropic_filtering` | `display.graphics`'s | `HALO_ANISOTROPIC_FILTERING` | Textures seen at a grazing angle (floors, walls, terrain) are sampled along the angle from finer mip levels, so they stay sharp; the Xbox game asks for none. `0` or `1`: as on the Xbox. `2`, `4`, `8` or `16`: at most that many samples (at most the GPU's); `4` keeps most of `16`'s sharpness at less cost on slower GPUs. It applies to the textures of the map that have mip levels and are filtered linearly (base, detail, bump and environment maps; lightmaps have no mip levels), not to what the game renders, such as the water's ripples. With Vulkan (Linux, Windows, Android), only where the GPU has `samplerAnisotropy`. Commented out, `display.graphics` chooses it (refer to "Classic and Enhanced"); set, it wins over `display.graphics`. |
| `display.latency_meter` | `false` | `HALO_LATENCY_METER` | `true`: in a network game, your round trip to the host in milliseconds at the top right of the screen, "Ping 45 ms" with four bars: green below 80 ms, yellow below 150, red from 150. The host shows "Max ping", its slowest player's. A player's machine that has heard nothing from the host for two seconds shows "Connection problem". The round trip includes each machine's wait for its next tick, so a LAN reads 15 to 35 ms. Not shown in the menus, after the game, in a co-op cutscene or when the host plays alone. The scoreboard's pings take the same colours, and show the round trips this machine times itself (the host: each player's; a player: their own), so they agree with the meter. Nothing more is sent. Only in config.toml, not in the menus. |
| `display.per_pixel_lighting` | `display.graphics`'s | `HALO_PER_PIXEL_LIGHTING` | `false`: the models (characters, weapons, vehicles, scenery) are lit at each vertex and the light is blended between them, as on the Xbox. The light across a curved surface then shows facets, and a point light that passes close lights only the vertices it reaches. `true`: the models are lit at each pixel by the same lights (the ambient light, two distant lights and two point lights), which changes their look. Commented out, `display.graphics` chooses it (refer to "Classic and Enhanced"); set, it wins over `display.graphics`. |
| `display.structure_culling` | `31` | `HALO_STRUCTURE_CULLING` | Which tests skip the triangles of the level (its walls, floors and terrain) that the camera cannot see before they are drawn, as bits added together: `1`, triangles outside the view; `2`, groups of triangles that all face away from the camera; `4`, single triangles that face away; `8`, smaller groups, so that more of them are skipped whole; `16`, the rooms nearest the camera first. Each test is conservative: the picture is the same, with fewer triangles drawn. `0`: the original walk, which draws every triangle of each visible room. The console's `structure_culling <bits>` changes it until the game quits, and `debug_structure_culling true` shows what was skipped (red: outside the view, blue: facing away). In the VR build the game culls once from the head's centre for both eyes, so the tests allow for them: they hold for every point within the farthest eye's distance from the centre, plus 3 cm the head may move before the late latch draws the frame (about 6 cm, in world units by `vr.world_scale`); the view's planes are the camera's own, moved out by that much, not a portal's, and there is no far plane (`game/vr_render.c`, `structure_culling_set_camera_radius`). |
| `display.model_detail` | `display.graphics`'s | `HALO_MODEL_DETAIL` | Which level of detail (of up to five) each model is drawn at. The game measures a model by its size in the lines of its 480-line screen, at any resolution, so at 1440 lines a model three times larger than the levels were made for is drawn as the Xbox drew it. `"stock"`: as on the Xbox. `"native"`: measured in the lines of the display (not supersampling's), so more detail is kept at a distance at high resolutions, and small far models that the Xbox left out are drawn. `"highest"`: measured as `"native"` for which models are drawn, and those drawn at their most detailed level. `"auto"`: `"native"` in the headset's eyes in VR, which have several times a display's lines and are seen close, and `"stock"` on a display. With either, a part that has no geometry at the level chosen draws its most detailed level below that has some, and a level made for a model's inside (one for a size over 1000 pixels, as the top level of a10's cryotubes) is drawn only while the camera is inside the model. Models whose map gives one level (the trees and rocks of b30) do not change. Android renders at its drawable's native resolution, so `"native"` uses those pixels too. Commented out, `display.graphics` chooses it (refer to "Classic and Enhanced"); set, it wins over `display.graphics`. |
| `display.shield_glow` | `false` | `HALO_SHIELD_GLOW` | `true`: as a unit's energy shield flares (hit, charging, falling), it lights what is around it and itself, as a plasma bolt does, in the shield shader's own color (a Spartan's gold, an Elite's blue), rising and falling with the flare seen on the body. An ordinary dynamic light of the game's, drawn with a light definition the map has (`shield_glow.c`). Up to 8 units near the camera. Video Setup's Extra Effects screen sets it. In a network game, the host's `network.shield_effects` decides, unless it is `players` (each player's own). |
| `display.shield_glow_intensity` | `default` | `HALO_SHIELD_GLOW_INTENSITY` | The shield glow's reach and brightness: `default`, or `light_show` (three times as far, and at full strength from a third of the flare: a light is no brighter than full). The units glowing are those near any local player's camera, so each of a split screen's views has the same. Video Setup's Extra Effects screen sets it. In a network game, the host's `network.shield_effects` decides, unless it is `players` (each player's own). |
| `display.overshield_effect` | `false` | `HALO_OVERSHIELD_EFFECT` | `true`: the overshield's flare rises up the body from the feet as it charges, with a soft edge (2 seconds, the charge's time from a full shield; 3 from a shield that was down; 2 from any other), then stays lit, whole, for as long as the overshield lasts, its energy moving at 30% of its speed, in its own color (the powerup's, 253, 78, 116), whatever shield color its player chose; the shield's own flares are not drawn over it, and its glow (`display.shield_glow`) rises and stays with it. With `display.viewmodel_shield` it comes up on the first-person arms as the rise reaches them. A hit on it plays what the projectile plays on an Elite's shield (its sound, sparks and light), when the map has that effect and it only shows and sounds. `false`: the overshield as the stock game draws it. Only this machine's drawing and sounds change (`overshield_effect.c`; the rise is a fade in the vertex shader, `d3d8_gl_set_height_fade`). Video Setup's Extra Effects screen sets it. In a network game, the host's `network.shield_effects` decides, unless it is `players` (each player's own). |
| `display.dynamic_lights` | `display.graphics`'s | `HALO_DYNAMIC_LIGHTS` | The light of plasma bolts, needles, muzzle flashes, grenades and explosions. `"classic"`: the game's own. `"enhanced"`: the structure and the models are lit for each pixel by the lights near each pixel (a grid of the lights about the camera, not a cap of two lights an object), the structure through its bump maps; in linear light, falling off as the inverse square of the distance with a soft core, to nothing at the light tag's radius, under a soft shoulder that never clips (a light's color and radius are its tag's); placed where the objects are drawn between ticks; lighting the first-person weapon and hands; with a soft glow about each plasma bolt in flight (a projectile with no model). Bolts and needles light too (the game draws only their lens flares). Vulkan and Metal: the Linux builds, Windows and Android (Vulkan) and macOS and iOS (Metal). The structure's spotlights (a flashlight's gel) keep the game's own pass. Refer to "Dynamic lights" under "VR". Commented out, `display.graphics` chooses it (refer to "Classic and Enhanced"); set, it wins over `display.graphics`. |
| `display.hdr` | `"auto"` | `HALO_HDR` | High dynamic range. `"auto"`: on while the window's display shows HDR (an XDR or HDR display: macOS's EDR headroom past 2, which an SDR panel's dimmed white never reaches); `"on"`: always (on an SDR display the highlights are rolled off under its white); `"off"`: never. The frame is drawn in half floats, which keep the light that the game's additive effects (plasma, explosions, muzzle flashes, the dynamic lights, lens flares, the sun) add past white, and an HDR display shows it brighter than paper white, its SDR white. Surfaces, the HUD and the menus stay where they are; the first-person weapon's displays and the HUD's flashes and motion tracker glow a little past paper white. Off, and with `"auto"` on an SDR display, the picture is the 8-bit one, unchanged. Metal (macOS) and Vulkan (the Linux desktop and Windows, where the device reads its colour attachments, `VK_KHR_dynamic_rendering_local_read`, and the compositor gives the swapchain an HDR colour space: HDR10 or scRGB; `"auto"` follows SDL's HDR state); Android and the VR mode draw SDR, and Video Setup shows the HDR rows only where the renderer draws HDR. The design: [docs/hdr.md](../../docs/hdr.md). A change applies from the next frame. |
| `display.hdr_mode` | `"faithful"` | `HALO_HDR_MODE` | With HDR: `"faithful"`: the picture as the game draws it, only its highlights brighter (what was under white stays exactly so); `"punchy"`: the whole scene and its bright parts lifted, each colour keeping its hue, for a brighter, remastered look. |
| `display.hdr_paper_white` | `100` | `HALO_HDR_PAPER_WHITE` | With HDR, how bright paper white (white surfaces, the HUD, the menus) is, in percent of the system's SDR white (macOS's; on Vulkan, SDL's SDR white, or 203 nits where the system gives none), `50` to `250`. The highlights keep the rest of the display's headroom; paper white is never brighter than the display can show. |
| `display.astc_textures` | `"auto"` | `HALO_ASTC_TEXTURES` | The builds with a host only, where the GPU cannot sample the maps' DXT (BC) textures: iPhones (Metal) and most Android phones (Vulkan). Checked on Android on a Pixel 11 Pro (PowerVR, no BC); the Android emulator, whose ASTC is emulated, draws those textures black. `"auto"`: where the GPU samples ASTC and not DXT, the DXT textures are transcoded to ASTC 4x4, a quarter of the memory and bandwidth of the decoded textures drawn before, and the same picture (within a third of an 8-bit step for nearly every block; the rest are encoded, at 39 to 43 dB of PSNR against their DXT texels, on worker threads the first time a texture is drawn, the texture drawn decoded meanwhile, and kept in the app's caches folder, 256 MB at most). `"always"`: wherever the GPU samples ASTC, beside DXT too (an Apple silicon Mac's, to try it). `"off"`: decoded, as before. Linux and Windows draw DXT as it is. Read by the host from the file at the start (`port/linux/astc/xgpu_astc.h`). |
| `display.picture_position` | `"center"` | `HALO_PICTURE_POSITION` | Android only. Where the picture sits on a screen taller than it (a folding phone's inner screen, a tablet, a window in split screen): `"center"`, with black above and below, `"top"`, with all the black below it, where the touch controls are, or `"bottom"`, with all the black above it. Refer to [port/android/README.md](../android/README.md). |
| `display.orientation` | `"landscape"` | `HALO_ORIENTATION` | Android only. Which way up the game is held: `"landscape"`, `"portrait"` (a strip along the top, the picture under it and the touch controls below it), or `"auto"`, whichever way the device is held, as its own rotation lock allows. A change applies at once. Refer to [port/android/README.md](../android/README.md). |
| `display.picture_shape` | `"4:3"` | `HALO_PICTURE_SHAPE` | Android only. The picture's shape on a screen taller than it: `"4:3"`, the Xbox's, or `"16:9"`, which sees more to the sides and leaves more of the screen to the touch controls. |
| `display.frame_style` | `"plain"` | `HALO_FRAME_STYLE` | Android only. The look of the screen around the picture in portrait: `"plain"` black, `"halo"` (navy, with the HUD's cyan lines and rings) or `"xbox"` (ribbed black, with green). It is never drawn over the picture. |
| `audio.enabled` | `true` | `HALO_NO_AUDIO=1` sets `false` | `false`: no audio device. The sound continues without output. |
| `audio.volume` | `1.0` | `HALO_VOLUME` | The master volume. |
| `audio.music_volume` | `1.0` | `HALO_MUSIC_VOLUME` | The music's volume, of the master volume. |
| `audio.effects_volume` | `1.0` | `HALO_EFFECTS_VOLUME` | The volume of the other sounds (effects and speech), of the master volume. |
| `audio.buffer_frames` | `0` | `HALO_AUDIO_BUFFER_FRAMES` | The frames the sound device asks for at a time, at 48 kHz, from `64` to `8192`: smaller is heard sooner, larger rides out stalls that would cut the sound out (256 is 5.3 ms, 1024 is 21 ms). `0`: automatic, `256` to start (`512` on Windows), doubled up to `2048` each time the device runs dry, which the log says. Refer to "Sound delay". Not on Android or iOS (1024). |
| `audio.channels` | `"auto"` | `HALO_AUDIO_CHANNELS` | `"stereo"`, or `"5.1"` for surround sound as with Dolby Digital on the Xbox: the world's sounds panned around the five speakers (VBAP), the game's 2D sounds on the speakers it sends them to, nothing on the LFE (the game sends it none). `"auto"`: 5.1 if the sound device has 6 or more channels (the host's SDL says how many the device plays: a 5.1 or 7.1 receiver or headset), else stereo. Read when the game starts. Not on Android. 5.1 is always panned, never the HRTF (`vr.hrtf` too). |
| `audio.reverb` | `true` | `HALO_REVERB` | `true`: the sounds of the world reverberate as the place the player is in does: the sound environments of the maps (a corridor, a cave, a large hall, outdoors) set the reverberation, as the I3DL2 reverb of the Xbox did. A sound behind a wall or a door is muffled in it too. `false`: no reverberation (sounds behind a wall are still muffled). |
| `audio.doppler` | `true` | `HALO_DOPPLER` | `true`: a sound of the world moving towards the player rises in pitch and one moving away falls (a Banshee or a Ghost passing by, a plasma bolt), from the velocities the game gives each sound, as the Xbox's DirectSound shifted them (the speed of sound 343 m/s, the shift held to an octave either way). `false`: each keeps its pitch. |
| `audio.spatial` | `"auto"` | `HALO_AUDIO_SPATIAL` | How the world's sounds are placed around you (Settings > Audio > 3D AUDIO; "3D audio"): `"headphones"` through a model of a head (HRTF), each from its direction, ahead, behind, above or below, as the Xbox's 3D audio was; `"speakers"` panned left and right; `"auto"` the first on headphones and the second otherwise, as the system's output changes. The menus, music and HUD are not placed. 5.1 is always panned; the VR mode keeps `vr.hrtf`. |
| `audio.resampler` | `"sinc"` | `HALO_AUDIO_RESAMPLER` | How the sounds are converted to the output's rate (48 kHz): `"sinc"` with a band-limited low pass, which keeps out the images above the sound's own rate; `"linear"` between neighbouring samples, which lets some of them through and sounds brighter for it. Audio Setup's SOUND PROCESSING screen sets it, with `audio.reverb`. |
| `audio.voice_chat` | `"push_to_talk"` | `HALO_VOICE_CHAT` | How you talk in voice chat: `"push_to_talk"` (while `controls.push_to_talk` is held; the microphone opens when you first press it), `"open_mic"` (when the microphone hears speech), or `"off"`. You hear the other players in every case. Refer to "Voice chat". |
| `audio.voice_volume` | `1.0` | `HALO_VOICE_VOLUME` | The volume of the voices of the other players, `0` to `2`. |
| `audio.output_device`, `audio.input_device` | `"default"` | `HALO_AUDIO_OUTPUT_DEVICE`, `HALO_AUDIO_INPUT_DEVICE` | The speakers and the microphone, by the name that Settings > Audio shows, or `"default"` for the device of the system. If the device is not found, the game uses the device of the system. Not on Android. |
| `audio.loose_sounds` | `false` | `HALO_LOOSE_SOUNDS` | For those who make sounds. `true`: each sound of a map that has a sound tag file of its name in `tags/` in the data root (for example `tags/sound/sfx/weapons/assault rifle/fire.sound`) plays from that file. The files are Halo PC tag files, as the Halo Editing Kit and Invader write them. At the console, `loose_sounds_reload` reads the files again, and `loose_sounds false` plays the mod packs' or map's sounds again. Loose files take priority over mod packs, independently of `mods_enable`. When a file changes, all sounds stop. |
| `input.mouse_sensitivity` | `5.73` | `HALO_MOUSE_SENSITIVITY` | The mouse's aim on Counter-Strike 2's scale: each count the mouse reports turns the view 0.022 degrees (CS2's `m_yaw` and `m_pitch`) times this, so the number you play CS2 with turns the same here (1.7: 0.0374 degrees a count). It is linear, with no acceleration, whatever the field of view, the resolution or the frame rate: the counts are SDL's relative motion, the mouse's own on Windows (raw input), on Linux (XInput2's raw events, Wayland's unaccelerated motion), and on macOS the GameController framework's GCMouse counts in place of the system's accelerated pointer (`port/macos/host_raw_mouse.m`; a trackpad, which is no GCMouse, keeps the pointer's motion). The default, 5.73, turns as the older versions' default did. A file of an older version, whose scale was 0.0022 radians a count, has a value of its own converted once, times 5.7296, so that it turns as before (and the vertical one made a multiplier). Settings > Controls has a slider of it and a field to type a value in (Enter keeps it). Controllers, the touch controls and VR have their own sensitivity. |
| `input.mouse_vertical_sensitivity` | `1.0` | `HALO_MOUSE_VERTICAL_SENSITIVITY` | The vertical mouse aim, times `input.mouse_sensitivity` (as CS2's `m_pitch` is of `m_yaw`): `1` turns up and down as far as side to side, `0.5` half as far. `0` is `1`. |
| `input.zoom_sensitivity` | `"cs2"` | `HALO_ZOOM_SENSITIVITY` | How the mouse turns zoomed, at every zoom level of every weapon. `"cs2"`: as CS2 turns with `zoom_sensitivity_ratio` 1, the hip turn times the zoomed field of view over the unzoomed one, each the horizontal angle across 4:3 the view is drawn at (`display.fov` widens the unzoomed one, so its zoomed turn is slower in proportion). At the stock view the pistol's and rocket launcher's 2x turn 0.488 times the hip turn, the sniper rifle's 8x 0.121 times. `"halo"`: the game's, one over the magnification (0.5 at 2x, 0.125 at 8x). Controllers, the touch controls and VR keep the game's whatever this is. Settings > Controls: ZOOM AIM SPEED (CS2 or ORIGINAL). Only this machine's: nothing of it reaches the network. |
| `input.crouch` | `"hold"` | `HALO_CROUCH` | A controller's or the keys' Crouch: `"hold"` crouches while it is held, as Halo did; `"toggle"` crouches at a tap until Crouch is pressed again, Jump is pressed, a vehicle's seat is taken or the player dies, and still crouches while held. The touch controls' Crouch is `input.touch_crouch`. |
| `input.crouch_walk` | `false` | `HALO_CROUCH_WALK` | `true`: a crouched player stays crouched at a full push of a controller's stick, walking at a crouch's speed (Settings > Controls: CROUCH WALK). `false`: a full push stands them up, as Halo did (it stands a grounded player up once the move is 0.98 long, which a stick's diagonal reaches at about 70% of its travel). The touch controls' stick always stays crouched, and the keys keep their own crouch walk. |
| `input.button_prompts` | `"auto"` | `HALO_BUTTON_PROMPTS` | The HUD's button prompts ("Hold X to pick up", the campaign's help text; Mouse Setup > BUTTON PROMPTS). `"auto"`: while the keyboard and mouse were played with last (a key or mouse button bound to an action, the captured mouse moved), the prompt names the key or button bound to the action (its first binding, a key or button before the wheel: `E`, `Left Click`, `Mouse 4`, `WASD` for moving), in the prompt's font in a drawn key cap, as Halo PC named the bound key; while a controller (or the touch controls) was, the controller's button, as before. Before either is used: the keys, unless a controller is connected or the touch controls are there. It follows Controls Setup's bindings as they change. `"controller"`: always the controller's buttons, as on the Xbox. `"keyboard"`: always the keys. In VR (but with `"controller"`), a prompt names what presses its button on the controllers in the hands: the input bound to it as the OpenXR runtime names it (`A Button`, `Right Hand Trigger`; the hand where the other hand has one too), or a fixed name when the runtime names none (`A`, `Right Bumper` on the Steam Frame, `Right Grip` on Touch-style controllers), the triggers and sticks' clicks as `vr.dominant_hand` and `vr.swap_sticks` place them, and `Left Hand Throw` for the grenade with `vr.grenade_throw = "gesture"` on foot; moving and looking keep the sticks' pictures. Only the first player's prompts (the keyboard's player); what the keyboard has no action for (the D-pad, the menus' accept and back) keeps the button. |
| `input.playstation_kernel_driver` | `false` | `HALO_PLAYSTATION_KERNEL_DRIVER` | Linux: `true` drives a PlayStation controller (DualSense, DualShock 4) through the kernel's driver of it (`hid-playstation` since Linux 5.12, `hid-sony`): its buttons, sticks, triggers and rumble, with no setup, but not its lightbar. `false`: SDL's own driver, which needs access to the controller's `/dev/hidraw` device, as Steam's udev rules give it (`steam-devices`, or Steam installed). Read at start-up. |
| `input.invert_mouse` | `false` | `HALO_MOUSE_INVERT=1` sets `true` | `true`: the vertical mouse aim is inverted. |
| `input.mouse_aim_assist` | `false` | `HALO_MOUSE_AIM_ASSIST` | `true`: the magnetism of the controller also operates for the mouse. `false`: when the mouse moved after the right stick, the view is not slowed or dragged by a target. The autoaim of the bullets operates in both cases. |
| `controls.<action>` | (the table in "Controls") | `HALO_KEY_<ACTION>` | The keys and mouse buttons of an action, up to two, separated by a comma: `move_forward`, `move_backward`, `strafe_left`, `strafe_right`, `jump`, `crouch`, `fire`, `throw_grenade`, `melee`, `reload`, `zoom`, `switch_weapon`, `switch_grenade`, `action`, `flashlight`, `scoreboard`, `pause`, `screenshot`, `push_to_talk`, `chat`, `team_chat`, `party_accept`. Keys by their names on a US keyboard (`"W"`, `"Space"`, `"Left Ctrl"`, `"F1"`): a key is the one in that place on any keyboard, which Controls Setup shows by its own label (the US name on Android). Buttons: `"Mouse Left"`, `"Mouse Right"`, `"Mouse Middle"`, `"Mouse 4"`, `"Mouse 5"`, `"Wheel"` (either way), `"Wheel Up"`, `"Wheel Down"`. |
| `game.downloaded_maps` | `""` | `HALO_DOWNLOADED_MAPS` | Maps to play as downloaded maps, besides the maps that map sharing downloaded (it names them in `downloaded_maps.txt` in `custom_maps`): their names (`bloodgulch`, `hugeass`), separated by commas, or `"*"` for all the maps. The scripts of a downloaded map cannot change the settings of the player or the games of other players. `debug.txt` names what the game refused. |
| `game.console_log` | `"important"` | `HALO_CONSOLE_LOG` | What the console shows on the screen. `"important"`: bans, players that the host drops for cheating, the reasons that the game refuses a command, and the asserts that stop the game. `"all"`: all the lines. `"none"`: only the asserts that stop the game. The output of a command always shows. `debug.txt` gets all the lines. |
| `game.language` | `""` | `HALO_LANGUAGE` | The language of the menus: `ja`, `de`, `fr`, `es` or `it`. Empty: English. |
| `game.enhanced_animations` | `true` | `HALO_ENHANCED_ANIMATIONS` | `true`: the player bipeds' grenade throws keep their legs moving, blended by speed and direction (crouched throws stay crouched, throws in the air use the jump's legs), Warthog and Scorpion riders stay seated to throw and let go of the grips to throw and reload, and a player turns with the aim while throwing, as while meleeing. `false`: the original animations, which freeze the legs during a throw and stand a rider up. Only in config.toml, not in the menus. |
| `game.sim_rate` | `60` | `HALO_SIM_RATE` | How many steps a second the game takes for what a player feels in their hands: their own movement and aim, projectiles, thrown items, and the VR mode's throws and grabs. `30` (as the game was made), `60` or `120`: 1, 2 or 4 steps inside each of the game's 30 ticks a second. Everything else keeps the 30 ticks: the AI, scripts, cinematics, animations, timers, vehicles, saved games and the network. Any other value is `30` (refer to "Simulation rate" in "How the port operates"). It applies to games of this computer alone: the campaign, split screen and games with bots (a split screen game runs as a network game of one computer that takes no other). Network games with other computers use the rate the game agreed on, hosting or joining: `30` in a Legacy game (OpenCE's netcode steps at 30), and the host's `network.sim_rate` in an OpenH1 game. The playback of a saved film is always `30`. TICK RATE in Video Setup > Graphics > Advanced: ORIGINAL (30), 60 or 120. A change applies from the next tick. Where the game sets the setting aside (a network game with other computers, a film's playback), the row is locked: dimmed, at the game's rate (ORIGINAL (30) in a Legacy game), passed over, and OK leaves the setting as it was. |
| `game.unlock_all_levels` | `false` | `HALO_UNLOCK_ALL_LEVELS` | `true`: a profile created afterward starts with all the campaign levels finished on all the difficulties. `false`: a new profile starts at The Pillar of Autumn, and each level opens when you finish the level before it, as on the Xbox. Existing profiles keep their progress. To start a profile over, type `profile_reset_campaign` in the console. |
| `bots.count` | `0` | `HALO_BOTS` | Computer players (bots) in a Slayer game this computer hosts: `0` to `15` join its lobby with the players, as far as the game has room: a local (split screen) game, or a system link or internet one, where the host plays them and the other machines see them as players (`bots.make_room`). `0`: none, as in the stock game. BOTS in Server Setup (of Multiplayer > PLAY WITH BOTS, or its BOTS screen in a network game) writes it. Refer to "Bots". |
| `bots.skill` | `"normal"` | `HALO_BOT_SKILL` | How well the bots play: `"easy"`, `"normal"`, `"heroic"` or `"legendary"`: how soon they react, how far and how wide they see, how large their aim error is and how fast it settles, how fast they turn. BOT SKILL in Server Setup of Multiplayer > PLAY WITH BOTS writes it. |
| `bots.teams` | `"even"` | `HALO_BOT_TEAMS` | In a team game: `"even"` puts the bots on both teams, evening them with the players; `"against"` puts them all on the team the players are not on. BOT TEAMS (EVEN or AGAINST YOU) in Server Setup of Multiplayer > PLAY WITH BOTS writes it. |
| `bots.make_room` | `true` | `HALO_BOTS_MAKE_ROOM` | In a system link or internet game with bots: a bot leaves as a player joins, so that the game has no more than `bots.fill_to` players and bots, and comes back as a player leaves. `false`: the bots stay, and the game's most players are its players' alone. MAKE ROOM on Server Setup's BOTS screen writes it. Refer to "Bots". |
| `bots.fill_to` | `16` | `HALO_BOTS_FILL_TO` | With `bots.make_room`, the players and bots a system link or internet game is filled to (2 to 128; no more than its most players). FILL TO on Server Setup's BOTS screen writes it. |
| `bots.tactics` | `false` | `HALO_BOTS_TACTICS` | In team games, a team's bots play together: they keep apart (about five metres) and within reach of each other, fire at the enemy a teammate is hurting and take angles apart on one enemy, go after a teammate's killer while that player's shields are down, close in together after a kill and fall back together when the fight goes against them, and share out the power weapons and the escort of a human teammate (the best placed bot takes each). `false`: each bot plays on its own; measured, the team play slows the fights and wins no more (refer to "Bots"). |
| `bots.share_sightings` | `true` | `HALO_BOTS_SHARE_SIGHTINGS` | A team's bots share the enemies they see, each sighting reaching the team's other bots `bots.share_delay` after it, as a callout would; it counts for where they go and look, never for their aim (a bot fires only at what it sees). `false`: a bot knows only what it saw itself. |
| `bots.share_delay` | `1.0` | `HALO_BOTS_SHARE_DELAY` | With `bots.share_sightings`: the seconds from a bot's sighting of an enemy to its teammates' knowing of it (0 to 5). |
| `paths.data` | `""` | `HALO_DATA_ROOT` | The data root. Refer to "Start the game". |
| `paths.saves` | `""` | `HALO_SAVE_ROOT` | The save root. Refer to "Files and folders". |
| `paths.logs` | `""` | `HALO_LOG_ROOT` | The folder of `debug.txt`, `bans.txt` and `cheaters.txt`. Empty: the data root. With it, the data root can be read-only (a dedicated server's maps mounted so). |
| `paths.custom_maps` | `""` | `HALO_CUSTOM_MAPS_ROOT` | The `custom_maps` folder (Custom Edition maps, with their `bitmaps.map`, `sounds.map` and `loc.map`). Empty: the one in the data root. |
| `network.address` | `""` | `HALO_NET_ADDRESS` | The IPv4 address of this machine for system link. Refer to "Play on one computer". |
| `network.broadcast` | `""` | `HALO_NET_BROADCAST` | IPv4 addresses, with commas between them, that get the broadcasts of the game. Empty: 255.255.255.255. |
| `network.port` | `5150` | `HALO_NET_PORT` | The UDP port of system link games (the Xbox's); the clients' is the next. Another port lets copies of the game host side by side on one computer (tests, dedicated setups). A LAN game on another port is found only by machines set to the same port. Refer to "Play on one computer". |
| `network.online` | `true` | `HALO_NET_ONLINE` | `true`: internet play. `false`: system link on the local network only. |
| `network.join_from_clipboard` | `true` | `HALO_NET_JOIN_FROM_CLIPBOARD` | `true`: when the game comes to the front, it joins the game of an invite link on the clipboard. |
| `network.tunnel_port` | `0` | `HALO_NET_TUNNEL_PORT` | The UDP port for internet play. `0`: the game selects a port. Refer to "Internet play". |
| `network.allow_upnp` | `true` | `HALO_NET_ALLOW_UPNP` | `true`: internet play can ask the router to forward its port (UPnP). `false`: the game does not ask. Refer to "Internet play". |
| `network.public_lobby` | `true` | `HALO_NET_PUBLIC_LOBBY` | `true`: the server browser. Public games are listed, and Join Game > Server Browser shows them. `false`: no games are listed or shown. Refer to "Server browser". |
| `friends.share_status` | `true` | `HALO_FRIENDS_SHARE_STATUS` | Show your friends that you are online, and where, and let them join you (Multiplayer > FRIENDS). Only your friends can read it. `false` appears offline: nothing is published, and you still see your friends. Refer to "Friends". |
| `network.host_public` | `true` | `HALO_NET_HOST_PUBLIC` | `true`: a new game of Create Game > Internet starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in Server Setup changes it for each game. Refer to "Server browser". |
| `network.game_mode` | `"openh1"` | `HALO_NET_GAME_MODE` | When you host: `"openh1"`, an OpenH1 game ("OpenH1 60 Tick" and the like) at the tick rate `network.sim_rate` sets, which OpenCE players can join too, at 30 (`network.allow_opence`); `"legacy"`, OpenCE's netcode at 30 ticks a second for everyone. `"standard"` is the same as `"openh1"`. Server Setup's GAME row (OPENH1 30 TICK, OPENH1 60 TICK, OPENH1 120 TICK or LEGACY) writes it and `network.sim_rate`. Read when hosting starts, and again when the game is started from Server Setup if no one has joined yet. Refer to "OpenH1 and Legacy games" in NETCODE.md. |
| `network.sim_rate` | `120` | `HALO_NET_SIM_RATE` | When you host an OpenH1 game: its tick rate, `30`, `60` or `120`. Every machine in the game uses it from the start of the game to its end, and each player's sub-step inputs go to every machine (NETCODE.md, "Sub-step inputs"). The server browser shows it before each game's gametype ("60 SLAYER"; a Legacy game's 30). |
| `network.allow_opence` | `true` | `HALO_NET_ALLOW_OPENCE` | When you host an OpenH1 game: `true` lets OpenCE players join it too (`false`: OpenH1 players only). They play at 30 ticks a second, as they do in a Legacy game, while OpenH1 players play at `network.sim_rate`. OpenCE players see the game as any game of OpenCE's; the host sends them exactly what a Legacy host sends. Read with `network.game_mode`. Refer to "OpenCE players in an OpenH1 game" in NETCODE.md. |
| `network.opence_welcome` | `true` | `HALO_NET_OPENCE_WELCOME` | When you host: tell each OpenCE player, a few seconds after they load into a game, "This server runs OpenH1." and "Get it free at openh1.com for smoother 60 and 120 tick games.", in the text chat as lines from no player. Once a player while you host, in any kind of game. OpenH1 players don't see it, and OpenCE shows no chat in a public game joined from its server browser. `false`: no welcome. Refer to "OpenH1 and Legacy games" in NETCODE.md. |
| `network.coop_friendly_fire` | `"on"` | `HALO_NET_COOP_FRIENDLY_FIRE` | Whether the players of an online co-op game hurt each other: `"off"`, `"on"`, `"shields_only"` or `"explosives_only"`. FRIENDLY FIRE in co-op's Server Setup > Co-op Options writes its choice here. Their AI allies they hurt as `network.coop_ally_friendly_fire` says. |
| `network.coop_ally_friendly_fire` | `true` | `HALO_NET_COOP_ALLY_FRIENDLY_FIRE` | Whether the players of an online co-op game hurt their AI allies (the Marines, Captain Keyes, the Sentinels while they are allies), as in the campaign. `false`: the players' weapons, grenades, melee and vehicles do them no harm, so that no player can kill the allies a level needs. The allies' and enemies' own damage is unchanged. ALLY FRIENDLY FIRE in co-op's Server Setup > Co-op Options writes its choice here; the host sends it with the game's settings. |
| `network.coop_own_friendly_fire` | `true` | `HALO_NET_COOP_OWN_FRIENDLY_FIRE` | Whether this machine's players hurt, and are hurt by, the other players of an online co-op game. Two players hurt each other only if both have it `true`, and then as the host's `network.coop_friendly_fire` says. FRIENDLY FIRE in that game's pause menu writes its choice here. A player away from the controls for over 70 seconds is hurt by every player, whatever either chose. |
| `network.coop_player_collisions` | `true` | `HALO_NET_COOP_PLAYER_COLLISIONS` | Whether the players of an online co-op game bump into each other. `false`: they walk through each other, so that one cannot block a doorway or stand on another; they still bump into the AI's characters. PLAYER COLLISIONS in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.coop_enemies_mode` | `"per_player"` | `HALO_NET_COOP_ENEMIES_MODE` | Online co-op's extra enemies: `"none"`; `"per_player"`, each squad of enemies that a level places grows by `network.coop_enemies` for each player past the first; or `"multiplier"`, each squad is `network.coop_enemies_multiplier` times as large, for any number of players. The extra enemies stand around the squad's places, and those that a dropship has no seats for drop out of it after its passengers. EXTRA ENEMIES in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.coop_enemies` | `50` | `HALO_NET_COOP_ENEMIES` | The extra enemies per player, a percentage from `25` to `200`: for each player past the first, each squad of enemies gets this much of itself more (`100`: as many again, so four players meet four times the squad), up to 8 times the squad however many players there are. PER PLAYER in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.coop_pickups` | `"none"` | `HALO_NET_COOP_PICKUPS` | EXTRA PICKUPS in co-op Server Setup > Co-op Options. Modes: `"per_player"`, `"multiplier"`, `"none"`. Static mode uses the shared MULTIPLIER, capped at 8x. In per-player mode, the host scales scenario-placed campaign weapons, grenades, health kits, overshields and active camouflage by the shared PER PLAYER percentage, capped at 8 times their original count. Fractions are distributed over placements of the same item. Copies retain weapon ammunition and use clear nearby floor in the active BSP. No safe space or capacity means no extra pickup. Enemy drops, inventory and vehicles are excluded. Script-created supplies count surviving copies against their placement budget. Checkpoints retain pending counts and copies together, without multiplying again on restore. |
| `network.coop_vehicles` | `"none"` | `HALO_NET_COOP_VEHICLES` | EXTRA VEHICLES after EXTRA PICKUPS in Co-op Options. Modes: `"per_player"`, `"multiplier"`, `"none"`. Each authored driveable vehicle supplies floor((players - 1) * Per Player * 2 / 300) extra copies in per-player mode, or floor((Multiplier - 1) * 2 / 3) extras in static mode, one at a time after a player drives the previous one clear of the home by the sum of both bounding-sphere radii plus 15%. Vehicle growth has an exact 2/3 modifier before rounding down; the default 50% requires four players for the first copy. Pelican cargo uses its unloaded resting position. Only the host spawns; obstructed pads and exhausted object capacity wait. Copies have no script name or AI passengers. In-memory checkpoint reverts restore the home table and budgets together; loading a disk/core checkpoint without that table forgets its old homes. None by default. |
| `network.coop_enemies_multiplier` | `2` | `HALO_NET_COOP_ENEMIES_MULTIPLIER` | The shared static multiplier, `2` to `32`, for options set to STATIC MULTIPLIER. Enemy squads are this many times as large; pickups are capped at 8x; vehicles supply floor((Multiplier - 1) * 2 / 3) extra copies. MULTIPLIER in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.voice_lobby` | `true` | `HALO_NET_VOICE_LOBBY` | When you host: `true`, all players hear all players in the lobby, before and after a game. |
| `network.voice_mode` | `"team_global_enemy_proximity"` | `HALO_NET_VOICE_MODE` | When you host: who hears whom during a game. `"off"`; `"team_proximity"` (teammates who are near); `"team_enemy_proximity"` (all players who are near); `"team_global"` (all teammates); `"team_global_enemy_proximity"` (all teammates, and enemies who are near). Refer to "Voice chat". |
| `network.voice_kbps` | `24` | `HALO_NET_VOICE_KBPS` | When you host: the voice quality in the lobby and in a game, in kilobits per second, `8` to `64`. |
| `network.voice_proximity` | `15.0` | `HALO_NET_VOICE_PROXIMITY` | When you host: the distance in world units (1 unit is approximately 3 metres) at which players are near, for voice chat. `5` to `100`. |
| `network.text_chat` | `"on"` | `HALO_NET_TEXT_CHAT` | When you host: the text chat. `"on"`, lines with links refused; `"links"`, links allowed; `"off"`, no chat (in either case). Listed in the server browser or not. TEXT CHAT in Server Setup > Teamplay Options (in co-op, Voice and Voting) writes its choice here. Refer to "Text chat". |
| `network.host_playlist` | `""` | `HALO_NET_HOST_PLAYLIST` | When you host: the games played one after another, each after the last one's scores: `map[:gametype[+rule=value...]]`, separated by commas, as a dedicated server's `server.rotation` (`docs/dedicated_server.md`). Host Controls' PLAYLIST and MAPS write it, and the console's `host_playlist`. Empty: you pick each game. Refer to "Host Controls". |
| `network.votekick` | `true` | `HALO_NET_VOTEKICK` | When you host: `true`, the players can vote to kick a player. Refer to "Security". `false`: no votes. VOTE KICK in Server Setup > Teamplay Options (in co-op, Voice and Voting) writes its choice here. |
| `network.votekick_minutes` | `5` | `HALO_NET_VOTEKICK_MINUTES` | When you host: the minutes that a player must play on the server before the player can start a vote to kick (`0` to `60`). To vote, a player must play for 2 minutes, or for this time if it is less. |
| `network.votekick_ban_minutes` | `30` | `HALO_NET_VOTEKICK_BAN_MINUTES` | When you host: the minutes that a player who is kicked by a vote cannot join again (`1` to `1440`). |
| `network.shield_effects` | `stock` | `HALO_NET_SHIELD_EFFECTS` | Host: the shield effects every player of a network game sees, whatever their own settings, so that none sees what another does not: `stock`, `glow` (`display.shield_glow`), `light_show` (its Light Show intensity), `overshield` (`display.overshield_effect`), `glow_overshield` or `light_show_overshield`; or `players`, not synced: each player sees their own settings' (PLAYERS' OWN). Sent to each client when it is ready and when it changes (`network_shield_effects.c`); a client draws stock until it has it. SHIELD EFFECTS in Server Setup > Player Options writes its choice here. |
| `network.fire_origins` | `true` | `HALO_NET_FIRE_ORIGINS` | Host: whether a client's reported hit that the shooter's camera had no line of sight to may still count from where the client says its player's shots leave, when that is not the camera (distributed message 93: a VR build's weapon hand, which may be past a corner the camera is behind). The hand must be within 0.4 world units of the camera, no more than 0.05 behind the eye along the line to the target, 0.2 to its side and 0.1 above it, with the line from the eye to it clear; the host logs how many hits counted only that way. `false`: from the camera and vehicles' guns only (`network_damage.c`). |
| `network.block_mouse_aim_assist` | `false` | `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | Host: Player Options > Aim Assist, Allowed or Block M+KB. Block overrides optional mouse magnetism while mouse/keyboard aims, including on the host, without changing the saved preference. Controllers and touch retain their existing behavior; bullet autoaim is unchanged. |
| `network.map_share` | `true` | `HALO_NET_MAP_SHARE` | When you host: send a player who joins without the game's Custom Edition map the map, and the resource maps that it needs and that the player does not have. SHARE MAPS in Server Setup > Map Sharing writes its choice here. Refer to "Map sharing". |
| `network.map_share_upload` | `1024` | `HALO_NET_MAP_SHARE_UPLOAD` | When you host: the most kilobytes per second that the game sends of maps (`16` to `20480`, 20 MB/s). UPLOAD LIMIT in Server Setup > Map Sharing writes its choice here. |
| `network.map_seed` | `true` | `HALO_NET_MAP_SEED` | When you play in the game of another machine: help send its map to players who join without it. HELP SEND MAPS in Settings > Network Setup > Map Sharing. |
| `network.map_seed_upload` | `256` | `HALO_NET_MAP_SEED_UPLOAD` | When you play in the game of another machine: the most kilobytes per second that the game sends of maps to other players (`16` to `10240`, 10 MB/s). UPLOAD LIMIT in Settings > Network Setup > Map Sharing. |
| `network.map_download` | `true` | `HALO_NET_MAP_DOWNLOAD` | When you join a game on a Custom Edition map that you do not have: download it from the players of the game. `false`: leave the game and show which map is missing. DOWNLOAD MAPS in Settings > Network Setup > Map Sharing. |
| `network.coop_public` | `false` | `HALO_NET_COOP_PUBLIC` | `true`: an online co-op game (Create Game > ONLINE CO-OP, or Internet with a SINGLEPLAYER map) starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in co-op's Server Setup writes its choice here. Refer to "Server browser". |
| `network.brokers_file` | `"brokers.txt"` | `HALO_NET_BROKERS_FILE` | The file of the public MQTT brokers that let the machines of an invite find each other, and that carry the listings of the server browser: next to `config.toml` (else the release's, `share/openh1/brokers.txt`), unless a full path. One `host:port` on each line, up to 4; `#` starts a comment. |
| `network.stun_servers` | Google and Cloudflare | `HALO_NET_STUN` | The public STUN servers (`host:port`, with commas between them) that give the internet address of a machine. |
| `network.client_timeout_seconds` | `15` | `HALO_NET_CLIENT_TIMEOUT_SECONDS` | Hosting: the seconds the host waits for a machine it hears nothing from before it drops it (5 to 300; the stock game waits 15). The same seconds are how long each machine's reliable stream waits for its peer to read before it gives up, and how long a host in a game waits for its own network to come back (an address renewed, a wifi drop) before it ends the game; each machine reads its own setting. A longer wait keeps players whose connection stalls (a browser tab in the background), but a machine that is really gone leaves its players frozen for longer. |
| `network.public_address` | `""` | `HALO_NET_PUBLIC_ADDRESS` | Hosting: the internet address the joiners reach this machine at, `host[:port]` (the port forwarded to `network.tunnel_port`), for a server behind a NAT with one forwarded port. It is offered first, and its name is looked up again every 5 minutes. Empty: what STUN and UPnP find. |
| `network.listings` | `"both"` | `HALO_NET_LISTINGS` | What the server browser lists. `"both"`: the dedicated servers of the master servers first, then the games players host (from the brokers). `"masters"`: the master servers' only. `"brokers"`: the brokers' only. Refer to "Server browser". |
| `network.masters_file` | `"masters.txt"` | `HALO_NET_MASTERS_FILE` | The file of the master servers the server browser asks for dedicated servers, next to `config.toml` unless a full path: on each line an `https://` URL and the master's key (`ed25519:<hex>`), 4 at most. A file that names none (missing, or comments alone, as the release's) leaves the official master, built in (`https://master.opencommunityedition.com`). Refer to "Dedicated server". |
| `server.enabled` | `false` | `HALO_SERVER` | `true`: run as a dedicated server, as `halo --server` does. Refer to "Dedicated server". The `server.*` settings are written to `config.toml` only by a dedicated server. |
| `server.name` | `"OpenH1 Server"` | `HALO_SERVER_NAME` | The game's name in the lobby and the server browser: 15 characters of printable ASCII. |
| `server.max_players` | `16` | `HALO_SERVER_MAX_PLAYERS` | The most players (2 to 127). |
| `server.min_players` | `1` | `HALO_SERVER_MIN_PLAYERS` | The players in the lobby before it counts down to a game (1 to 16). |
| `server.password` | `""` | `HALO_SERVER_PASSWORD` | The password the server browser asks for (an invite link joins without it). Empty: none. It is never logged. `HALO_SERVER_PASSWORD_FILE` names a file whose first line is it instead (a container's secret; pinned, as the variable would be). |
| `server.public` | `true` | `HALO_SERVER_PUBLIC` | `true`: the server is in everyone's server browser (on the master servers, with `network.online`). `false`: players join with its invite link, or on the LAN. |
| `server.rotation` | `"bloodgulch:ctf, sidewinder:team_slayer, prisoner:slayer"` | `HALO_SERVER_ROTATION` | The games, played one after another: `map[:gametype[+rule=value...]]`, separated by commas, semicolons or new lines, 64 at most (an entry 255 characters, the whole 4096); a line that begins with `#` is a comment. A map is a multiplayer map's name (`bloodgulch`), `custom_maps\<name>` or a level's path; a gametype is a built-in gametype's name (`slayer`, `team_slayer`, `ctf`...), an OpenCE gametype's (`swat`...), a gametype file's (`gametypes/<name>.toml`) or `file=<saved gametype>`. An entry that cannot be played is left out with a warning. A change of the rotation (a command, the web page, `sv_reload`) keeps its place: the next game is the entry after the one played last. Refer to `docs/dedicated_server.md`, "The rotation". |
| `server.rotation_fallback` | `false` | `HALO_SERVER_ROTATION_FALLBACK` | `true`: with no entry of `server.rotation` that can be played, play `bloodgulch:slayer`. `false`: stop (exit code 3). |
| `server.lobby_seconds` | `15` | `HALO_SERVER_LOBBY_SECONDS` | The seconds the lobby counts down once `server.min_players` are in (0 to 300; `0`: the game starts at once). |
| `server.postgame_seconds` | `12` | `HALO_SERVER_POSTGAME_SECONDS` | The seconds the scores show after a game (0 to 300). |
| `server.empty_seconds` | `30` | `HALO_SERVER_EMPTY_SECONDS` | The seconds a game with nobody in it goes on before it ends. `0`: it does not end. A game whose last player leaves ends at once, as the game ends it. |
| `server.idle_minutes` | `0` | `HALO_SERVER_IDLE_MINUTES` | The minutes a game goes on with no score before it ends. `0`: it does not end. |
| `server.team_start` | `"one"` | `HALO_SERVER_TEAM_START` | A team game. `"one"`: it starts with the players of one team (the next players to join balance the teams). `"both"`: it waits, saying nothing, for a player on each team. `"skip"`: while one player waits, a team game (not one `sv_map` asked for) gives way to the rotation's next entry without teams, if it has one; checked when the lobby opens and again when its countdown would begin. |
| `server.motd` | `""` | `HALO_SERVER_MOTD` | A notice each machine sees when it joins (127 characters). Empty: none. |
| `server.frame_rate` | `60` | `HALO_SERVER_FRAME_RATE` | The frames a second (30 to 120); the game's ticks stay at 30 a second. A server with no other machine runs 10 frames a second. |
| `server.console`, `server.console_exit_on_eof` | `true`, `false` | `HALO_SERVER_CONSOLE`, `HALO_SERVER_CONSOLE_EXIT_ON_EOF` | `true`: the server takes commands from the standard input; it stops when the standard input ends if `server.console_exit_on_eof` is `true` (a panel can close it), after running the last line, whether or not a line's end ended it. |
| `server.init_file` | `"init.txt"` | `HALO_SERVER_INIT_FILE` | The commands run once at the start, one on each line: in the data root unless a full path. Empty: none. |
| `server.web_port`, `server.web_bind` | `8080`, `"127.0.0.1"` | `HALO_SERVER_WEB_PORT`, `HALO_SERVER_WEB_BIND` | The TCP port and IPv4 address of the web page, its API and the remote console (RCON). `0`: none. With an address beyond loopback, each web password that is set must have 8 characters or more, else the server stops (exit code 3, as for a port it may not bind or an address the machine does not have; a port in use: exit code 4); put a reverse proxy with TLS in front of it. Each server on a machine needs its own port. Read at the start only. |
| `server.web_password` | `""` | `HALO_SERVER_WEB_PASSWORD` | The admin password of the web page (8 characters or more). Empty: the server makes one at the first start, shows it once and writes it here. |
| `server.web_owner_password` | `""` | `HALO_SERVER_WEB_OWNER_PASSWORD` | A password with the owner role, for a supervisor on the server's own machine (a fleet's agent, a panel): over the API and the RCON it does what the standard input does (`quit`, `sv_stop`, `sv_restart`, the owner's settings, the game's console commands, the players' addresses). It signs in from `127.0.0.0/8` alone, never through `server.web_trusted_proxy`; from another address it is refused (`403 forbidden`) and counted as a wrong password. 8 characters or more (else the server exits with code 3). Empty: none. `sv_set` does not change it (`config.toml` and `sv_reload`). |
| `server.web_moderator_password`, `server.web_viewer_password` | `""` | `HALO_SERVER_WEB_MODERATOR_PASSWORD`, `HALO_SERVER_WEB_VIEWER_PASSWORD` | Other passwords: the moderator role (kick, mute, say, the next map, bans of one day at most) and the viewer role (the status, the players and the log; no changes). 8 characters or more when the web port is bound beyond loopback (else the server exits with code 3; on loopback, a warning). Empty: none. `sv_set` changes the moderator password, not the viewer password (`config.toml` and `sv_reload`). Each web password's variable has a `_FILE` form (`HALO_SERVER_WEB_PASSWORD_FILE`, `HALO_SERVER_WEB_OWNER_PASSWORD_FILE`...): when the variable is not set, the file's first line is the password, pinned as the variable would be; read at the start. |
| `server.web_ui` | `true` | `HALO_SERVER_WEB_UI` | `false`: the web port serves the API and RCON, but not the admin page. |
| `server.web_trusted_proxy` | `""` | `HALO_SERVER_WEB_TRUSTED_PROXY` | The address of a reverse proxy in front of the web port: its `X-Forwarded-For` (the last entry, the one the proxy added, an IPv4 or IPv6 address with or without a port), `-Host` and `-Proto` headers are believed, and a sign-in through it with `X-Forwarded-Proto: https` gets a `Secure` cookie. Empty: none. |
| `server.metrics` | `true` | `HALO_SERVER_METRICS` | `true`: the web port serves `/metrics` (Prometheus) without a password. |
| `server.log_format` | `"text"` | `HALO_SERVER_LOG_FORMAT` | The standard output's lines: `"text"` or `"json"`. |
| `server.log_addresses` | `false` | `HALO_SERVER_LOG_ADDRESSES` | `true`: the players' internet addresses and hardware IDs are in the log, the events and the API for every role (the owner's console always has them). |
| `server.log_debug_echo` | `false` | `HALO_SERVER_LOG_DEBUG_ECHO` | `true`: the lines of `debug.txt` are also written to the standard error. |
| `server.status_file` | `""` | `HALO_SERVER_STATUS_FILE` | A file the status (as `/api/status` gives it) is written to every 2 seconds, for a panel without HTTP. Empty: none. |
| `server.auto_update` | `"off"` | `HALO_SERVER_AUTO_UPDATE` | `"check"`: every 6 hours, the server looks for a newer release and logs it. `"install"`: it also installs it once the server has been empty for 10 minutes (on a thread of its own, the server playing on meanwhile), and stops with exit code 5, once it is empty, to be started again; an install that fails is tried again at the next check. `"off"`: neither. |
| `server.upnp` | `false` | `HALO_SERVER_UPNP` | `true`: ask the router (UPnP) to forward the tunnel's port (as `network.allow_upnp`, for a server at home). |
| `server.coop_difficulty` | `"normal"` | `HALO_SERVER_COOP_DIFFICULTY` | The difficulty of co-op entries of the rotation (`<level>:coop`): `"easy"`, `"normal"`, `"hard"` or `"impossible"`. |
| `server.list_on_brokers` | `false` | `HALO_SERVER_LIST_ON_BROKERS` | `true`: the server is also listed with the games players host (through the brokers), for server browsers without master servers. |
| `server.master_publish` | `true` | `HALO_SERVER_MASTER_PUBLISH` | `true`: a public server is published to the master servers, signed with the publisher key. |
| `server.masters_file` | `"masters.txt"` | `HALO_SERVER_MASTERS_FILE` | The master servers the server is published to, next to `config.toml` unless a full path: on each line an `https://` URL and the master's key (`ed25519:<hex>`). None named: the official master, built in. |
| `server.publisher_key_file` | `"publisher.key"` | `HALO_SERVER_PUBLISHER_KEY_FILE` | The operator's Ed25519 key for the master servers (`halo --server --keygen` makes it), next to `config.toml` unless a full path. In a container, `HALO_SERVER_PUBLISHER_KEY` can hold the key itself. |
| `server.instance` | `""` | `HALO_SERVER_INSTANCE` | A short name that tells this server from the operator's other servers on the master servers (letters, digits, `-` and `_`). Empty: made from `server.name`. |
| `server.region` | `""` | `HALO_SERVER_REGION` | A region the server browser shows (`eu`, `na-east`...): 16 characters. Empty: none. |
| `server.chat_filter` | `"off"` | `HALO_SERVER_CHAT_FILTER` | The words the server keeps out of its chat and its players' names (streamer mode's list): `"off"`; `"censor"`, a line's words shown as stars; `"block"`, a line with them refused (its sender told why) and a player whose name has them refused at the door. Links are `network.text_chat`'s. |
| `server.chat_filter_slurs`, `server.chat_filter_swearing`, `server.chat_filter_insults` | `"mild"`, `"off"`, `"off"` | `HALO_SERVER_CHAT_FILTER_SLURS`, `_SWEARING`, `_INSULTS` | With `server.chat_filter`, each group's words kept out: `"off"`, `"severe"` (the worst), `"strong"` or `"mild"` (all). |
| `server.master_publish_address` | `false` | `HALO_SERVER_MASTER_PUBLISH_ADDRESS` | `true`: the master servers can give this server's address to players. `false`: only their probe uses it. |
| `discord.application_id` | OpenH1's application (`1561963518344036434`; refer to "OpenH1's Discord application") | `HALO_DISCORD_APPLICATION` | The Discord application for your status, invites and asks to join. Its name is what Discord shows you playing. Empty: no Discord. |
| `discord.rich_presence` | `true` | `HALO_DISCORD_RICH_PRESENCE` | `true`: Discord shows what you do: in the menus, in a party, the game you host or play. Refer to "Discord". `false`: Discord shows nothing, and no one joins you through Discord. |
| `discord.allow_join_requests` | `true` | `HALO_DISCORD_ALLOW_JOIN_REQUESTS` | `true`: Discord friends can ask to join your party or game (the game asks you), and you can invite them with Discord's Invite to Join. `false`: Discord shows no Join buttons, and declines asks; send your invite instead. |
| `update.auto` | `true` | `HALO_UPDATE_AUTO` | `true`: at start-up, the game looks for a new version. Refer to "Updates". `false`: the game does not look. |
| `streamer.enabled` | `false` | `HALO_STREAMER_MODE` | `true`: streamer mode hides offensive words, as stars, in the names and text of other players, maps and mods, at the levels below. Refer to "Streamer mode". STREAMER MODE in Settings > Network Setup > Streamer Mode. |
| `streamer.slurs` | `"mild"` | `HALO_STREAMER_SLURS` | With `streamer.enabled`, the slurs (race, ethnicity, sexuality, gender, disability) hidden: `"off"`, `"severe"`, `"strong"` (strong and severe) or `"mild"` (all). CENSOR SLURS in Settings > Network Setup > Streamer Mode. |
| `streamer.swearing` | `"strong"` | `HALO_STREAMER_SWEARING` | With `streamer.enabled`, the swearing and sexual words hidden: `"off"`, `"severe"`, `"strong"` or `"mild"`. CENSOR SWEARING in Settings > Network Setup > Streamer Mode. |
| `streamer.insults` | `"off"` | `HALO_STREAMER_INSULTS` | With `streamer.enabled`, the insults that are not slurs hidden: `"off"`, `"severe"`, `"strong"` or `"mild"`. CENSOR INSULTS in Settings > Network Setup > Streamer Mode. |
| `crash_reports.upload` | `"ask"` | `HALO_CRASH_REPORTS` | Windows only. `"yes"`: the game sends a report of each crash to the developers. `"no"`: the game sends no reports. `"ask"`: the game asks at the next crash and writes the answer here. Refer to "Crash reports" in [port/windows/README.md](../windows/README.md#crash-reports). |
| `debug.first_start_input` | `""` | `HALO_FIRST_START_INPUT` | The buttons an automated test presses on the first start's screen (no game data), one each half second: `up`, `down`, `a` and `b`, separated by commas. Empty: the player's. |
| `debug.update_answer` | `""` | `HALO_UPDATE_ANSWER` | The answer to the update question, for automatic tests: `yes`, `no` or `never`. Empty: the game asks. |
| `debug.update_from` | `""` | `HALO_UPDATE_FROM` | A release's zip file to update to, for tests of updates: the game offers it as a new version without asking GitHub, also when the build has no build number, but not in the version that the update starts. Empty: the game asks GitHub. |
| `debug.load_trace` | `false` | `HALO_LOAD_TRACE` | Log map loading checkpoints, and start-up's from the host's `main` (`host.main`, `host.guest_main`), with absolute monotonic wall, thread and process CPU seconds. Subtract timestamps to measure phases; process CPU includes worker threads. Set `HALO_LOAD_TRACE_DETAIL=1` to add sampled preparation CPU timings and detailed renderer traces; measure final loading times without these extra diagnostics. |
| `debug.fixed_step` | `false` | `HALO_FIXED_STEP` | `true`: a game of this computer alone runs one step a frame (a tick at `game.sim_rate` 30, a sub-step at 60 and 120), whatever time has passed, so that it plays the same every time (tests). |
| `debug.exit_after` | `0.0` | `HALO_EXIT_AFTER` | The game stops after this number of seconds (from when its window opens; with `debug.null_renderer`, or no display, from its first frame; a dedicated server stops as `quit` stops it). `0`: never. |
| `debug.screenshot_directory`, `debug.screenshot_every` | `""`, `0` | `HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY` | The game writes each Nth frame to this folder as a BMP file. |
| `debug.screenshot_stream` | `""` | `HALO_SCREENSHOT_STREAM` | With `debug.screenshot_every`, a file each frame's pixels are appended to instead of the folder's BMP files: BGRA, the top row first, no header (a named pipe an encoder reads, `ffmpeg -f rawvideo -pix_fmt bgra -s <width>x<height>`), so that a long run's frames never reach the disk. |
| `debug.screenshot_from`, `debug.screenshot_until` | `0`, `0` | `HALO_SCREENSHOT_FROM`, `HALO_SCREENSHOT_UNTIL` | With `debug.screenshot_every`, only the frames between these seconds of the scenario's simulation (the clock of `debug.trace` and `debug.test_input`'s scripts; `0`: no bound) are written: a part of a long run, run again the same way. |
| `debug.hidden_window`, `debug.null_renderer` | `false` | `HALO_HIDDEN_WINDOW`, `HALO_NULL_RENDERER` | `true`: no visible window, or no graphics. |
| `debug.join_legacy` | `false` | `HALO_DEBUG_JOIN_LEGACY` | `true`: join every game as an OpenCE build joins it, an OpenH1 game that OpenCE players can join (`network.allow_opence`) too: at 30 ticks a second, with the Legacy game's join token. The tests' stand-in for an OpenCE machine (`tools/stagekit/tests/test_mixed.py`). |
| `debug.no_clipboard` | `false` | `HALO_NO_CLIPBOARD` | `true`: the game leaves the clipboard alone, for tests and tools: it puts no invite link on it and joins none found there. A run with `debug.null_renderer`, `debug.test_input`, `debug.fixed_step`, `debug.network_test` or `debug.exit_after` leaves it alone too. |
| `debug.render_manifest` | `""` | `HALO_RENDER_MANIFEST` | A file the draws' shaders and pipelines are recorded into, each new one added (the renderer's manifest: `port/assets/render/README.md`), and a map's enumeration (as `display.prepare_shaders`): each later draw the map's enumeration does not have is marked missed, with the functions it was drawn in (`tools/render_manifest.py check`). Empty: none. |
| `debug.gpu_stats`, `debug.gpu_trace_frame`, `debug.gpu_trace_constants`, `debug.gpu_dump_shaders`, `debug.texture_dump_directory`, `debug.gpu_validation` | off | `HALO_GPU_STATS`, `HALO_GPU_TRACE`, `HALO_GPU_TRACE_CONSTANTS`, `HALO_GPU_DUMP_SHADERS`, `HALO_TEXTURE_DUMP`, `HALO_GPU_VALIDATION` | Tools to find problems in the graphics: counts for each frame (with Vulkan, also its submissions, passes, clears and pipelines every 600 frames, as `HALO_VULKAN_STATS`), every draw of one frame, the shaders' code (Vulkan's SPIR-V, as `.spv` files, as `HALO_VULKAN_SHADERS`), the textures as they are read (TGA files, `tex<number>_fmt<format>_<width>x<height>.tga`), and the graphics API's errors in the log (Vulkan's validation layers, which must be installed, as `HALO_VULKAN_VALIDATION`). |
| `debug.gl_debug` | `false` | `HALO_GL_DEBUG` | Deprecated: `debug.gpu_validation`'s old name, which still turns it on (with Vulkan, the validation layers). |
| `debug.skip_cinematics` | `false` | `HALO_SKIP_CINEMATICS` | Skip each cinematic of a local game: as the skip button does where the level lets it be skipped, else run at sixteen times its speed (`main_skip`), so that a test reaches play in seconds (b30's opening, which cannot be skipped, in about 100 frames rather than 2,400). |
| `debug.menu_open` | `""` | `HALO_MENU_OPEN` | Start on this screen of the menus (`main_menu/settings_select/...`, as `port/assets/menus` names it), a player profile being edited, to look at it. |
| `debug.matchmaking`, `debug.matchmaking_host` | `""` | `HALO_MATCHMAKING`, `HALO_MATCHMAKING_HOST` | Search a matchmaking playlist (its name, `"Team Slayer"`, or its number, 1 up) once the main menu is up, and how well this machine tells its party it can host (`"strict"` or `"open"`; empty: as its network is), for a test; with either, a player without a profile plays. |
| `debug.party` | `""` | `HALO_PARTY` | Join this party link at the start, or lead its party (`lead:` before the link): several copies of the game in one party, for a test. |
| `debug.friends_folder` | `""` | `HALO_FRIENDS_FOLDER` | Keep `friends.txt` in this folder, not the player profile's: a test's copies, which have no profile. |
| `debug.friends` | `""` | `HALO_FRIENDS` | The Friends screen's actions a test takes at a second after the start: `"<second> <action> [<code>]\|..."`, the actions `add`, `accept` and `ignore` (every request), `remove`, `block`, `unblock`, `invite`, `join`, `accept_invite`, `share`, `hide`, `list` (logged) and `show` (a row chosen); a code is its text, or `@` and another copy's `friends.txt`. |
| `debug.discord_socket` | `""` | `HALO_DISCORD_SOCKET` | The Discord client's socket (on Windows, its pipe) to connect to, and no other: a stand-in for Discord in a test, so that a test never reaches the Discord app of the person at the computer. Empty: Discord's own. |
| `debug.gpu_skip_vertex_shaders`, `debug.gpu_debug_expression`, `debug.gpu_debug_flat`, `debug.gpu_debug_texture0` | off | `HALO_GPU_SKIP_VS`, `HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_FLAT`, `HALO_GPU_DEBUG_T0` | Tools to find problems in the graphics: skip the draws of a vertex shader, or replace the output of all pixel shaders with an expression (for example `t0.rgb`). `debug.gpu_debug_expression` is Metal's alone (`HALO_GPU_DEBUG_EXPR`, as MSL): it does nothing with Vulkan, whose shaders are SPIR-V written directly, with no source to put it in. |
| `debug.dynamic_lights_look`, `debug.dynamic_lights_log` | `""`, `false` | `HALO_DYNAMIC_LIGHTS_LOOK`, `HALO_DYNAMIC_LIGHTS_LOG` | Tools for the enhanced dynamic lights: up to eight numbers in place of their look's (the lights' brightness, `1.7`; their core's inverse square in radii, `12`; the spill past white, `0.18`; the wrap on models, `0.25`; the glow's size in the light's radius, `0.22`, its brightness, `1.2`, its softness against what it meets in world units, `0.12`, and its least size, `0.035`), and a log of where the first projectile's light is drawn each frame beside where the game placed it at the tick, and once a second how many lights there are. |
| `debug.network_test`, `debug.network_test_start`, `debug.network_test_kill`, `debug.network_test_score`, `debug.network_test_shoot`, `debug.network_test_vehicle`, `debug.network_test_hitch`, `debug.network_test_pickup`, `debug.network_test_pickup_weapon`, `debug.network_test_chat`, `debug.network_test_name`, `debug.network_test_players`, `debug.network_test_most_players`, `debug.network_test_public`, `debug.test_input` | off | `HALO_NETWORK_TEST`, `HALO_NETWORK_TEST_START`, `HALO_NETWORK_TEST_KILL`, `HALO_NETWORK_TEST_SCORE`, `HALO_NETWORK_TEST_SHOOT`, `HALO_NETWORK_TEST_VEHICLE`, `HALO_NETWORK_TEST_HITCH`, `HALO_NETWORK_TEST_PICKUP`, `HALO_NETWORK_TEST_PICKUP_WEAPON`, `HALO_NETWORK_TEST_CHAT`, `HALO_NETWORK_TEST_NAME`, `HALO_NETWORK_TEST_PLAYERS`, `HALO_NETWORK_TEST_MOST_PLAYERS`, `HALO_NETWORK_TEST_PUBLIC`, `HALO_TEST_INPUT` | Automatic tests of system link, or (`log`) a log of the game one machine plays (`game/network_test.c`); a `local:` game has `debug.network_test_players` players (split screen), and a hosted one takes `debug.network_test_most_players` at most (`0`: the game's own); a test game starts `debug.network_test_start` seconds (`15.0`) after the host opens it. Refer to `NETCODE.md`. `debug.test_input = "menu:a down down a"` presses menu buttons one a second (`a`, `b`, `x`, `y`, `up`, `down`, `left`, `right`, `start`, `back` or `wait`, `screenshot`, the Screenshot key's capture, or `click:<x>:<y>`, a click of the pointer there, `x` and `y` 0 to 1 of the window; `back+x` holds Back through its second and presses X half way through it), for testing the menus; `"script:0.5 jump; 1-2 move=0,1; 2 aim=90,10"` presses, holds and aims on the simulation's clock, in seconds from the scenario's start, the same at any simulation rate (`tools/scorecard`). |
| `debug.network_test_cues`, `debug.network_test_cue_players` | none, `1` | `HALO_NETWORK_TEST_CUES`, `HALO_NETWORK_TEST_CUE_PLAYERS` | Console lines a test runs at their seconds, from the first tick that many players are in (`"<second> <line>\|..."`). |
| `debug.network_test_lobby_cues` | none | `HALO_NETWORK_TEST_LOBBY_CUES` | Console lines a test's host runs in its first lobby, at their seconds of its setup, before `debug.network_test_start` starts the game (`"<second> <line>\|..."`). |
| `debug.bot_trace` | `0` | `HALO_BOT_TRACE` | Every this many seconds, each bot's state is written to the log: where it is and where it goes, its path, its target and its weapon. `0`: never. |
| `debug.bot_player` | `false` | `HALO_BOT_PLAYER` (set) | In a local game with bots (`bots.count`), a bot of its own plays the first player too, and its view is drawn as the player's: footage of a fighter without a person at the controls. |
| `debug.bot_player_weapon` | `""` | `HALO_BOT_PLAYER_WEAPON` | With `debug.bot_player`, a weapon (part of its tag's name: `sniper`, `rocket`) the player's bot goes for wherever on the map it lies, and keeps; with a sniper rifle it looks through the scope at a distance. |
| `debug.bots_weapon` | `""` | `HALO_BOTS_WEAPON` | A weapon (part of its tag's name: `assault rifle`, `\pistol`, `sniper`) every bot spawns with and keeps while it has rounds, to measure the bots' aim with one weapon: halo.log's `bots: ... aim` line, each minute, gives each weapon family's rounds that struck someone of those fired. |
| `debug.bot_player_grenades` | `""` | `HALO_BOT_PLAYER_GRENADES` | With `debug.bot_player`, `plasma`: the player's bot spawns with four plasma grenades and no frags, and throws them at 4 to 18 world units, six times as often as a bot, for sticks. |
| `debug.bot_player_vehicle` | `""` | `HALO_BOT_PLAYER_VEHICLE` | With `debug.bot_player`, a vehicle (part of its tag's name: `warthog`, `ghost`) the player's bot walks to and boards whenever it is on foot, a free one nearest first, and drives: along its paths, and full ahead at the enemies it fights. |
| `debug.touch_menus` | `false` | `HALO_TOUCH_MENUS` | Lays the menus out as the touchscreen's builds (Android, iOS) do: their `touch` screens (Multiplayer's tiles, the server browser, the lobby, Host Controls and the Party screen with every tap target a finger's size, the tabs as chips and the footer's key prompts as buttons), for looking at them on a computer. Read as the menus load. |
| `debug.touch_targets` | `false` | `HALO_TOUCH_TARGETS` | Outlines the tap targets of the menus (item green, value blue, list slot yellow, legend button red, the band beside the slots of a list orange, keys of the on-screen keyboard white), marks where the last finger went down and the last tap landed for 3 seconds, and logs each tap with the target that it hit (for a value, also where it splits into previous and next): to judge the accuracy of touch. |
| `debug.log_addresses` | `false` | `HALO_LOG_ADDRESSES=1` sets `true` | `true`: other machines' internet addresses are written whole in `debug.txt` and the console, for debugging your own network. `false`: a public address is written as a tag (`addr#3f2a9c`) that matches up only within one run; local network addresses are always whole. Do not post a `debug.txt` written with this on. |
| `debug.network_latency`, `debug.network_loss`, `debug.network_corrupt`, `debug.network_corrupt_stream`, `debug.network_corrupt_after` | `0` | `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS`, `HALO_NETWORK_CORRUPT`, `HALO_NETWORK_CORRUPT_STREAM`, `HALO_NETWORK_CORRUPT_AFTER` | The game holds all the data that it receives for this number of milliseconds, ignores this percentage of the datagrams, and damages this percentage of the datagrams it receives, and this percentage of its reads of streams, at random (bytes changed, cut short, stretched or replaced), from this many seconds after the start. Use the first two to test the netcode as on the internet, and the others to test that nothing another machine sends can crash the game (a damaged stream is closed, so a little goes a long way; a host's messages to its own client are damaged too, so start damaging once the game has started). |
| `debug.voice_test` | `false` | `HALO_VOICE_TEST` | Automatic tests of voice chat: a tone replaces the microphone, and each voice that the game hears is written to the log once each second. |
| `debug.latency_log` | `""` | `HALO_LATENCY_LOG` | A file to write a line to for each press of the fire control and each move from standing of the first player, on the controller (`fire`, `move`) and on the mouse (`click`), and each turn of the mouse from still (`turn`), in milliseconds: the press to the reading of the controller or the mouse, the reading to the simulation step that acts on it (the weapon fires, the unit moves; for a turn, the view takes it), that step to the first frame drawn after it, that frame to the first frame that shows it (the frames draw a tick behind: "Frame rate"), and that frame's drawing and handing on, and whether the player sat in a vehicle (a move of the vehicle, a shot of its gun: `game/latency_log.c`). A press in a menu (the D-pad, A or the left stick, `menu`) is acted on when the menu takes it (the selection moved, an item chosen). A rumble from rest gets a `rumble` line: the step that made it to the first write of the motors (the SDL call), its time on, and the time the game gave it. `tools/latency_summary.py` gives each stage's median and 95th percentile, those in a vehicle apart. A shot's sound gets a line of its own, from the step to the sound heard ("Sound delay"). `debug.test_input = "latency:<seed>"` pulls the fire control, pushes the stick, clicks the mouse and moves it in turn on the real clock, so the presses fall anywhere in a tick (`"latency:<seed>:fire,click,turn"` plays those kinds only, and `"latency:<seed>:menu"` the D-pad down and up in a menu; the mouse's come as its events do); with `debug.network_test = "local:bloodgulch:swat"` the player holds SWAT's pistol, which fires as the control is pulled (the plasma pistol a multiplayer game starts with otherwise fires as it is let go). The way to the screen after that is the renderer's (`HALO_METAL_STATS` on the Mac; with Vulkan, `HALO_VULKAN_FRAME_LOG`, which `tools/latency_summary.py --frames` joins to the presses by the two times each line ends with, the press and the showing frame's input read in nanoseconds of `CLOCK_MONOTONIC`, for the press to that frame's present and to its time on screen). |
| `debug.determinism_log` | `""` | `HALO_DETERMINISM_LOG` | A file to write a line a tick to: a hash of the game state as the tick left it, the random seeds, and every taker of a seed since the line before by its call site (in the tick, in the frames, on other threads). Two runs of the same input compared with `tools/determinism_compare.py` first differ where something besides the input reached the game. |
| `debug.determinism_dump` | `0` | `HALO_DETERMINISM_DUMP` | With `debug.determinism_log`, the tick after which to write the game state too, counted as logged from 1 (its allocations in order, to `<log>.<count>.bin`), to find where two runs' game states differ (`tools/determinism_compare.py --dump`), and that tick's first frame as `<log>.<count>.bmp`; `0` none. |
| `debug.trace`, `debug.trace_seconds`, `debug.trace_mutation` | `""`, `0.0`, `""` | `HALO_TRACE`, `HALO_TRACE_SECONDS`, `HALO_TRACE_MUTATION` | A file to write a line a simulation step to (`game/sim_trace.c`): the players' place, velocity and aim, each projectile's, and the step's shots, projectiles made, strikes and detonations, timed in seconds from the scenario's start; quit once the scenario has run `debug.trace_seconds`; `debug.trace_mutation` puts a known bug in on purpose (`head_start`, `jump`, `speed`), which the scorecard must catch (`tools/scorecard`). Off, it costs a test a call. `HALO_GUEST_IMAGE` (the environment only) runs another guest image in the game's place: StageKit's map writer (`tools/stagekit`). |
| `debug.frame_time` | `0.0` | `HALO_FRAME_TIME` | Seconds of game time each frame runs, whatever time it took, so that two runs of the same input run the same frames (the determinism check, with `debug.determinism_log`); `0` the time each frame took. |
| `debug.bots_tactics_team` | `-1` | `HALO_BOTS_TACTICS_TEAM` | With `bots.tactics`, only this team's bots (`0` red, `1` blue) play together, the other team's each on its own, to measure what the team play is worth (the bots' minute line gives each team's kills); `-1` both teams'. |
| `debug.random_seed` | `0` | `HALO_RANDOM_SEED` | The random seed of the network games this machine hosts, so that a test plays the same game again (where players spawn, the weapons' spread, the AI's choices); `0` a new one for each game, from the clock. |
| `debug.audio_log` | `false` | `HALO_AUDIO_LOG` | Write to the log, five times a second, each sound placed in the world that plays: where it is from the listener (metres ahead, right and up), its gain in each ear (through the HRTF, its delays and filters too), the time the mixing takes, how long the sound device waited for its frames of the time it had queued, and its dropouts ("Sound delay"). |
| `debug.audio_record` | `""` | `HALO_AUDIO_RECORD` | Record the game's sound as it plays (with a device or without) to this WAV file, 48 kHz 16-bit stereo, for automatic tests. Empty: none. |
| `debug.sample_seconds` | `0.0` | `HALO_SAMPLE` | The builds with a host only (Linux, Android, macOS). The host writes to the log, this often in seconds, where each game thread is: its program counter and frame chain, to find where the game spends its time or stops. `0`: never. |
| `debug.memory_watch` | `true` | `HALO_MEMORY_WATCH` | The builds with a host only (Linux, Android, macOS). `true`: the host notices the game's writes to cached textures and vertices by page protection. `false`: it compares page contents once a frame instead, which is slower. |
| `debug.telnet_console`, `debug.telnet_console_port` | `false`, `2323` | `HALO_TELNET_CONSOLE`, `HALO_TELNET_CONSOLE_PORT` | The game listens on 127.0.0.1, on this port, for a script console (connect with telnet). The console has no password, so only this computer can reach it. |
| `debug.profile_record`, `debug.profile_record_when`, `debug.profile_memory` | `false`, `"start"`, `256` | `HALO_PROFILE_RECORD`, `HALO_PROFILE_RECORD_WHEN`, `HALO_PROFILE_MEMORY` | Profiling builds only (`configure.py --profile`). Record without a command; `"start"` records from the first frame until the first map change, `"game"` records each game outside the main menu. Memory is 4–1024 MB. Numbered `profile_<stamp>_<role>.part<n>.json` files remain in `profiles/` after recording. Refer to "Profiling builds" in the main README. |


### Classic and Enhanced

`display.graphics` (Video Setup > Graphics > Preset) chooses the settings
of the 3D view's look together. Each of them that is commented out in
`config.toml` takes the preset's value; one that is set (in the file, by its
environment variable, or in the Settings menu) keeps its own value. In the
Settings menu, Preset shows the rows it chooses at its values as it
changes, and a row written at the preset's value is commented out again,
so that choosing Classic or Enhanced there chooses them all.

| Setting | Classic | Enhanced |
| --- | --- | --- |
| `display.anti_aliasing` | `"off"` | `"smaa"` |
| `display.anisotropic_filtering` | `0` | `16` |
| `display.shadow_resolution` | `128` | `512` |
| `display.per_pixel_lighting` | `false` | `true` |
| `display.model_detail` | `"auto"` | `"native"` |
| `display.dynamic_lights` | `"classic"` | `"enhanced"` |

Enhanced is the default, the same on every build: every improvement. SMAA
is one pass of fixed cost whatever the scene, where multisampling multiplies
the work of each of the game's passes; anisotropic filtering costs about
0.08 ms a frame at 4x at 1080p. A phone or a headset that runs hot or short
of its refresh rate with it can be set lower setting by setting, or with
Classic. `"auto"` model detail is `"native"` in the headset's eyes. Classic
keeps `"auto"` for that reason: the headset's eyes have many more lines than
the Xbox's 480.

A renderer without a feature draws without it, as it does when the setting
is chosen by hand. Vulkan (the Linux builds, Windows, Android) and Metal
(macOS, iOS) draw them all, SMAA on a phone too. Enhanced gains them as the
renderers do.
The preset changes only the drawing: nothing the game simulates or sends.
The HUD, the text, the field of view, smooth motion and the shield glow
keep their own settings.

## Updates

The builds from GitHub Actions (refer to the main [README](../../README.md#download))
can update themselves. At start-up, the game asks GitHub for the latest
release. The game does not wait for the answer. If the latest release is not
newer, the game does nothing.

If the latest release is newer, the game asks: "Do you want to update?"

- Select "Yes" to update. The game downloads the release for this platform
  (`OpenH1-Linux.zip`, `OpenH1-Linux-ARM64.zip`, or the
  VR build's `OpenH1-Linux-VR.zip`: "VR"), replaces its files
  and starts the new
  version. The old files get the extension `.old`. The new version deletes
  them.
- Select "No" to continue. The game asks again at the next start.
- Select "Do not ask again", then "Yes", to stop the questions. The game
  writes `auto = false` in the `[update]` section of `config.toml`. To get
  the questions again, set `auto = true`.

Under gamescope (the Game Mode of the Steam Deck), a system dialog stops
the game, so the game asks in its own menus, and you answer with the
controller:

- When the latest release is newer, the game downloads its zip in the
  background while you play, into `update.partial/` beside the game.
- Then, at the main menu, it asks: "A new version is ready. Update now?"
- UPDATE: the game replaces its files as above (the old ones as `.old`),
  and starts the new version in the same process. Steam started that
  process and waits for it, so Steam does not see the game quit or crash,
  and the game stays open in Game Mode. If the new version cannot start
  there, the game quits; start it again from the library.
- NOT NOW (or B): the game goes on. It asks again at the next start, which
  deletes the download first.

To stop the questions, turn off Settings > Network Setup > Check for
Updates (`update.auto`). `update_answer = "yes"` (or `"no"`, `"never"`) in
the `[debug]` section answers without the question. The Windows build under
Proton does not ask under gamescope: update it from the desktop.

The game downloads through HTTPS. It examines the certificate of the server
against the certificate authorities of the system: on Linux, the bundle of
the distribution (`src/posix_update.c`, with Mbed TLS); on Windows, the
certificate store of Windows (WinHTTP). The game's folder must let the
game write to it.

Only a game in a release's tree (`bin/openh1`, refer to "Packaging"), or
the Steam Frame's (below), updates itself. Builds that you make yourself
have no build number, and a build's folder (`build/linux_x86_64`) is not a
tree: they do not look for updates. An installed game (refer to
"Packaging") does not look for updates: its package manager updates it.

### On the Steam Frame

The Steam Frame's VR build (`~/apps/halo-vr`, "Steam Frame") updates itself
from the headset. It looks for a new version when its launcher,
`halo-vr.sh`, starts it (as Steam does), and `update.auto` is true
(Settings > Network Setup > Check for Updates):

- When the latest release is newer, the game downloads that release's
  `OpenH1-Steam-Frame.zip` in the background while you play, and unpacks
  it in `update.partial/` beside the game.
- Then, at the main menu, it asks in its own menus (a system dialog would
  not show in the headset, and stops the game under gamescope, where the
  Frame runs it): "A new version is ready. Update now?"
- UPDATE: the game writes `update-ready` and quits. `halo-vr.sh` then
  installs the package with the package's own `install.sh` (the same as
  installing it by hand: its `SHA256SUMS` checked, each file put in place
  with a rename, `halo` kept as `halo.prev`, the player's files
  untouched), deletes the download, and starts the new version. Its
  output is in `halo-vr.log.prev`. If the install fails, the game starts
  as it was.
- NOT NOW (or B): the game goes on. It asks again at the next start (the
  launcher deletes the download first).

`update_answer = "yes"` (or `"no"`, `"never"`) in the `[debug]` section
answers without the question, and `update_from` names a package's zip to
update to, for tests. A game not started by `halo-vr.sh`, or not in a
folder with the package's `halo-vr.sh` and `SHA256SUMS`, does not look for
updates.

## Packaging

The release (`OpenH1-Linux.zip`, `OpenH1-Linux-ARM64.zip`,
`OpenH1-Linux-VR.zip`, and their `-debug.zip`) is laid out as a
Unix prefix is, so a package can copy its folders to `/usr` (or
`/opt/openh1`) as they are (but not the Steam Frame's
`OpenH1-Steam-Frame.zip`, its own package: "Steam Frame" in "VR"):

| Path | Contents |
| --- | --- |
| `install.sh` | The installer (`tools/linux_install.sh`: "Steam Deck"). Only in the release's zips: `tools/linux_package.py tree`, for a package, leaves it out. |
| `bin/openh1` | The game: a 64-bit executable (x86-64 or AArch64), with the game's guest image inside it. |
| `lib/openh1/libSDL3.so.0` | SDL 3, which the game loads from there (a `DT_RPATH` of `$ORIGIN:$ORIGIN/../lib/openh1`; refer to "Requirements"). Leave it out to use the system's SDL 3. |
| `share/openh1/brokers.txt` | Internet play's MQTT brokers (`network.brokers_file`). |
| `share/openh1/vr` | Only in the VR build's release: by it, `install.sh` installs that build beside the other ("Steam Deck"). |
| `share/openh1/steam/*.png` | Steam's library artwork for the game's shortcut, which `install.sh` puts in Steam's grid folder ("Steam Deck"); OpenH1 VR's in the VR build's release. Only in the release's zips, as `install.sh`. |
| `share/applications/openh1.desktop` | The desktop entry. |
| `share/icons/hicolor/<size>/apps/openh1.png` | Its icon, 16x16 to 512x512. |
| `share/doc/openh1/` | The main README and this one. |
| `share/licenses/openh1/` | The project's license (`LICENSE.md`, CC0) and the licenses of what the build carries, which ask to be carried with it. |

The game finds its files from the place of its executable
(`src/install_layout.c`): when the executable's folder is `bin/` and
`share/openh1/` is next to that folder, the folder above `bin/` is the
game's folder. Otherwise the folder of the executable is the game's folder
(a build: `build/linux_x86_64/halo`).

An installed game keeps the player's files in the player's folders, not in
the game's folder, and does not update itself:

- `config.toml`: `$XDG_CONFIG_HOME/halo-linux/` (`~/.config/halo-linux/`).
- The game data (`maps/`, which the first start copies from a disc image)
  and `debug.txt`: `$XDG_DATA_HOME/halo-linux/`
  (`~/.local/share/halo-linux/`), as are the saves (refer to "Files and
  folders").
- `brokers.txt`: the package's `share/openh1/brokers.txt`, unless the player
  puts one next to `config.toml`.

The game is installed when the player cannot write to the game's folder
(`/usr`, `/opt/openh1`), or when `share/openh1/packaged` exists. A package
adds that file (it can be empty) so that the game is installed also when its
folder can be written to: when root starts it, or in a folder that the
player owns. Otherwise the game is portable: it keeps its files in its
folder and, in a release's tree, updates itself, as an unpacked release
does.

A read-only folder without `share/openh1/packaged` (a portable copy on a
read-only disk, or `/opt/halo` with maps that an administrator put there)
keeps the `config.toml` and `maps/` it has: the game reads them there, and
puts in the player's folders only what the folder does not have. Without a
home folder (no `XDG_CONFIG_HOME`, `XDG_DATA_HOME` or `HOME`), an installed
game keeps its files in its folder, as far as it can be written to, and
still does not update itself.

To make a package from the sources, build as the releases do, and lay out
the build as the release's tree:

```sh
python configure.py --release
ninja
python tools/linux_package.py tree "$pkgdir/usr"
```

`tools/linux_package.py tree` lays out the build of this computer's
architecture (`build/linux_x86_64`, or `build/linux_arm64` on 64-bit ARM;
`--build` names another), with the SDL 3 that the build brings in
`lib/openh1`. A build that you make has no build number, so it does not
look for updates in any place.

The game needs: glibc 2.35 or later (or the glibc of the computer that
built it), the Vulkan loader (`libvulkan.so.1`) and a driver with Vulkan
1.3, the X11 or Wayland client libraries, and the PipeWire, PulseAudio or
ALSA client libraries for sound: refer to "Requirements". The first start
finds the disc image itself; its "Browse for the disc image..." is the
desktop's file dialog, through `xdg-desktop-portal` (or `zenity`), which
the game does without.

The 64-bit ARM build downloads `OpenH1-Linux-ARM64.zip` (or
`-debug.zip`), not the x86-64 one. The 64-bit x86 build downloads
`OpenH1-Linux.zip` (or `-debug.zip`), and its VR build
`OpenH1-Linux-VR.zip` ("VR").

## Frame rate

The game calculates its world at 30 Hz, as on the Xbox. On the Xbox, the
game showed one frame for each calculation (tick). This port shows more:
on Linux, Windows and macOS, without vsync, up to twice the refresh rate (`display.max_fps`); with vsync (iOS, Android, or
`display.vsync = true`), one frame for each
refresh of the display, for example at 60, 120 or 240 Hz.

Each frame shows the world between the last two ticks
(`game/render_interpolation.c`):

- After each tick, the game keeps the camera, the position of each part of
  each object, and the first-person weapon.
- Each frame mixes the last two ticks. The mix agrees with the time since
  the last tick.
- Rotations use quaternions. Positions and scales are linear.
- A teleport, a respawn or a cut of the camera does not mix. It jumps.
- After a long frame (several ticks in one frame), the camera mixes the
  last tick only, as the objects do.
- In cinematics, a camera that moves with an object (the lifepod in a30,
  a Pelican) moves with the object as it is drawn.

Thus the frames are one tick (33 ms) after the calculation. The calculation
does not change. At `game.sim_rate` 60 and 120, the player's movement,
the vehicle a player drives (and its turns, its riders and the view from its
seats), projectiles and thrown items are drawn on the path of their
sub-steps, not on the line between two ticks (refer to "Simulation rate").

Other things the game moves once a tick are drawn between ticks as well:

- The motion tracker's sweep (and a custom blip's pulse) moves in each
  frame. The blips are still taken once a tick, as on the Xbox, and leave
  their trail a tick at a time, but the newest of each (a unit, a flag or
  ball) is drawn where its unit or goal is drawn, from where the player
  is drawn.
- A waypoint (nav point) over an object stays over the object as it is
  drawn, an ally or a vehicle on the move too (in VR as well, where the
  waypoint is drawn in the world). So does a multiplayer game's marker
  over the flag or the ball, over the player who carries it, and over
  slayer's target.
- The screen's flashes (a hit, a teleport) and a level's fades, and the
  camera's shake and knock, follow their curves in each frame. The shake
  is random once a tick, as on the Xbox; between ticks it moves from one
  tick's place to the next's. In VR the view does not shake (refer to
  "How it operates" in "VR").
- A moving object's streams of particles (a looping effect's: a
  vehicle's exhaust, a wreck's smoke and sparks) are made through each
  tick where the object is drawn, rather than in 30 clumps a second at its
  place of the tick. A one-time effect's burst (an impact, an explosion)
  is made at once, as before.
- A contrail's head (a rocket's exhaust, a needle's or a plasma bolt's
  trail) stays on the projectile in the frames between ticks, where it
  stayed behind until the next tick.
- An object made moving (a bolt, a rocket, a grenade thrown) is drawn
  from a tick of its motion back in its first tick, not standing a tick
  at the muzzle. A projectile with a model (a rocket, a needle, a
  grenade) that hits something is drawn a tick longer, on to where it hit,
  as what it made there begins: it went a tick early, short of the wall,
  as the explosion appeared. That last tick is drawn only: nothing of the
  game sees it.
- An object's animated glows and colours (a weapon's charge or heat, a
  vehicle's lights, a shield's flare: the values and change colours its
  shaders read) and a unit's active camouflage change between ticks, as
  its pose does.
- So do the brightness and colour of the lights on an object (a muzzle
  flash, a plasma pistol's charge, a vehicle's lights), and the light of an
  explosion or an impact fades in each frame.

The direction of the view is an exception. The game reads the mouse, the
sticks and the touch controls in each frame. In first person, on foot, each
frame points the view where the player aims at that time
(`display.direct_camera`), with the mouse's motion (and on Android and iOS
the touch controls' swipe and gyroscope) that came while the frame was
calculated: the view reads them again as the frame takes it, after the
simulation, and turns on by that motion, which the next frame's input then
gives the player (only the view drawn takes it early: nothing sent or
simulated changes; the touch controls' not while the aim assist's
magnetism slows them, which would turn the player otherwise). Thus the view
turns in the frame that the mouse moves. The view also stands where the
newest step left the player, carried on by the player's velocity for the
time since that step, and the gun and hands are posed from it with their
animations sampled at the frame's time (each between its frame and the
next, which the game advances a frame a tick, and the sway's springs where
their velocity has them): the player's own movement, shots and
reloads are seen in the first frame after the step that made them, not a
tick (33 ms) later (`debug.latency_log` measures it).

In a vehicle's seat (a driver's, a passenger's or a gunner's, in the first
person or following the vehicle), the vehicle and what it carries (its
riders, the player in the seat, its gun) are drawn as the vehicle's newest
step left it, carried on as that step moved and turned it for the time
since, their pose (the wheels, the turret's aim, a rider's animation)
still between the ticks; and the view stands at the seat's camera point on
the vehicle so drawn, aimed where the player aims at that time, with the
late motion as on foot: in the first person turned and rolled with the
vehicle as the seat's camera turns it; following it, swung about the seat's
point as the frames between the ticks swing it (its pull in where it would
pass through something, which the game finds a frame at a time, stays
smooth), turned with the player's aim (not by the late motion in a
passenger's following view, `display.passenger_third_person`, which keeps
to the firing arc). A
correction of the netcode glides the vehicle and the view together, as it
glided the vehicle before; and as a player gets in or out (or dies, or
respawns in one) the vehicle glides from the one drawing to the other, with
no jump of the tick of motion between them. Where the vehicle's next step
is not where its motion carried on put it (a wheel touching down, a
landing, a bounce, a crash), it moves on from where it was drawn, the
difference blended away over a tick, easing out and in, rather than jumped
(a start from standing is drawn at once). While the player's aim is held
at the seat's pitch limit, which the game eases toward the vehicle's tilt
a step at a time, the view's pitch is the mean of the last step's frames,
so that it moves evenly. A seat whose view is the vehicle's gun's (which
aims once a tick) is drawn as before.

Everything else, other players and their vehicles included, is drawn
between the ticks as before. In cinematics the view mixes as the other
things do, and the VR mode's eyes, on foot and in a seat, stand where they
did.

The menus read the controller, the keyboard and the mouse each frame, and
a press moves the selection or chooses an item in the frame that reads it;
that frame shows it (`debug.latency_log`'s `menu` lines). Holding a
direction repeats it four times a second, and the menus' fades and timers
run on the clock, as on the Xbox; a spinner's arrow lights for half a
second at any frame rate (it was 15 frames). The controller's rumble
starts at the end of the step that made it (a shot, a hit, a grenade) and
a vehicle's in the frame its sound starts, and lasts as long as the game
gives it from then; the game writes the motors as they change and every
33 ms while they run, and a game that stops for 100 ms (a hitch, a map
loading) leaves them stopped (`rumble` lines).

To get 30 frames each second, set `display.interpolation = false`.

### Low latency with vsync

With vsync on, the display shows a frame at each refresh, and a game left
to itself draws ahead of it: the Vulkan swapchain (FIFO) holds two or three
finished frames, each waiting for its refresh, and the game starts the next
as soon as an image is free. Its input is read when it starts, so what is
on screen was read two or three refreshes (33 to 50 ms at 60 Hz) before.

`display.low_latency` (`"auto"`, on for Linux and the Steam Deck) paces the
game by the display instead (`vulkan/xgpu_vulkan_pacing.c`,
`src/frame_pacing.h`, the command stream's `display_wait`):

- Each present has a number, and a thread waits for each in turn
  (`VK_KHR_present_wait`): it comes back when the display (or the
  compositor: gamescope, on the Deck, takes its frames about 4.6 ms before
  the refresh) has taken the frame. Where the driver also says when each
  frame reached the screen and how far apart the refreshes are
  (`VK_GOOGLE_display_timing`: gamescope's, Android's), that time is the
  frame's time on screen; without present wait (Android) the thread asks it
  each millisecond.
- The game starts a frame only while at most one of its frames waits for
  the display, so the swapchain has three images: one on screen, one
  waiting, one drawn into.
- And not before the time it takes ahead of the refresh it is for: the
  most a frame took of the last 128, from its input read to its work done
  on the GPU, and a margin. A frame that started at its time and was done
  that margin early, but still missed its refresh, widens the margin by
  1 ms (a display that takes its frames sooner than the margin gives); 120
  frames in time narrow it by 0.1 ms, down to 0.25 ms over the margin
  that last missed, which itself comes down 0.1 ms a minute. The margin
  starts at 1.5 ms.

The game plays the same; only when each frame starts changes. On a GPU
that cannot draw a frame each refresh, the frames start at once, as before.
`HALO_VULKAN_STATS=1` logs, every 600 frames, the refreshes that showed a
frame again, the frames shown after the refresh they were started for (and
how many of those took longer than any before them), the margin and the
input to screen time; `HALO_VULKAN_FRAME_LOG=<file>` writes each frame's
times (`vulkan/README.md`), which `tools/latency_summary.py --frames` joins
to `debug.latency_log`'s presses.

Measured on Linux (an RTX 4090) in gamescope's headless backend (the Steam
Deck's compositor, without a screen), 1280x720, a local SWAT game on Blood
Gulch with `debug.test_input = "latency:7:fire,move,click,turn"`, 150 s a
run: the press to the first frame that shows it on screen (display
timing's time), median / 95th percentile in ms, off then on:

| display, `game.sim_rate` | fire | move | click | turn |
|---|---|---|---|---|
| 60 Hz, 30 | 91.0 / 115.5 → 18.3 / 36.0 | 90.6 / 113.7 → 23.0 / 37.7 | 95.9 / 119.5 → 23.0 / 36.9 | 90.8 / 116.8 → 15.5 / 22.8 |
| 60 Hz, 60 | 91.4 / 115.8 → 16.6 / 23.3 | 93.6 / 114.5 → 15.0 / 22.6 | 96.2 / 119.4 → 13.4 / 20.6 | 88.9 / 116.0 → 14.9 / 22.6 |
| 60 Hz, 120 | 95.4 / 115.5 → 13.9 / 22.3 | 85.3 / 115.3 → 14.4 / 23.3 | 90.0 / 111.4 → 15.9 / 22.1 | 96.1 / 118.2 → 16.3 / 23.2 |
| 120 Hz, 120 | 51.2 / 61.7 → 10.5 / 14.5 | 49.5 / 61.1 → 11.9 / 19.3 | 50.7 / 60.9 → 11.1 / 14.6 | 52.8 / 60.7 → 10.9 / 14.3 |

Off, a frame waited 54.4 ms from its present to the screen at 60 Hz (three
frames queued, and gamescope's 4.6 ms), 29.4 ms at 120 Hz; on, 5.9 to
6.5 ms. What is left is mostly the wait for the next frame's input read (up
to a refresh) and, at 30 steps a second, for the step. The refreshes that
showed a frame again, from the first press on, in about 7,870 frames a run
at 60 Hz: none off; 0, 1 and 1 on at 30, 60 and 120 steps (one frame that
took 2.5 ms, three times the others, at the same moment of each run), and
2 in 15,740 at 120 Hz.

### Battery saver

`display.battery_saver` (Video Setup > Battery Saver) draws less, for a
handheld's (or a laptop's) battery and heat: `"on"`, `"off"` (the default),
or `"auto"`, on while the machine runs on its battery
(`src/battery_saver.c`). The game looks at the battery every 5 seconds on a
thread of its own, with SDL's `SDL_GetPowerInfo`: the system's power supply
on Linux (the Steam Deck's too), IOKit on macOS, the battery broadcast on
Android, Windows' power status. The guest builds reach it through the host
(`host_sdl_power_info`). A machine without a battery, or one whose battery
SDL cannot read, is taken to be on its charger: Auto stays off.

While it is on:

- Each frame stays on screen for a whole number of the display's refreshes,
  as many as bring the frames each second to 60 or fewer, but not fewer than
  30, so that every tick is drawn (`frame_pacing_divisor` in
  `src/frame_pacing.h`): 60 at 60, 120 or 240 Hz, 45 at 90 Hz, 48 at
  144 Hz. A display at 60 Hz or below keeps its own rate, so there battery
  saver changes only the graphics. VR never has it. With every frame on
  screen as long as the others, the motion stays even, and the frames still
  mix the ticks (`display.interpolation`). The Vulkan host (Linux, Windows, Android)
  presents each frame that many times in its FIFO swapchain, so that the
  game waits for the display as it does at every refresh; the Metal host
  (macOS) runs its display link at that rate (a ProMotion display slows to
  it). Without vsync, the frames are spaced by
  the clock (with `display.max_fps`'s limit, the lower).
- The settings that `display.graphics` chooses (refer to "Classic and
  Enhanced") are drawn at their Classic values where they are commented
  out, whatever the preset: no anti-aliasing, no anisotropic filtering,
  128-pixel shadows, lighting at each vertex, stock model detail, the
  game's own dynamic lights. A setting that is set keeps its own value. The
  Settings menu shows the values of the file, which battery saver does not
  change.

Only the drawing changes: the game calculates its ticks as before, and a
network game is the same on every machine (`HALO_PORT_NETWORK_VERSION` is
unchanged). In VR, battery saver is off: the headset's runtime paces its
frames, and a frame held for two of its refreshes would make the player
sick. The log says when it turns on or off, and why for Auto.

To see the frame rate:

1. Push \` to open the developer console.
2. Enter `display_framerate true`.

In the game of another host, the console runs only the commands that change
nothing of the game (such as `display_framerate`), and the game puts back
cheats, the game speed and the settings of the drawing that show more of
the world (such as `rasterizer_wireframe`). Refer to `NETCODE.md`.

The frame rate shows at the bottom right of the screen. It is the mean over
half a second.

## 3D audio

On headphones the world's sounds reach each ear as they would reach your
head, as the Xbox's DirectSound put its 3D sounds through its HRTF: a sound
to your left is a little later and duller at your right ear, one behind is
duller, and one above or below has a tone of its own (the notch of the
pinna's), so ahead, behind, above and below can be told apart. On speakers
they are panned left and right, as before. The menus, the music, the HUD
and the other 2D sounds are as they were. The model is the VR mode's ("Sound
from your head": Brown and Duda's, a few filters a sound); a sound's
distance, occlusion, reverb and Doppler shift are the pan's.

`audio.spatial` (Settings > Audio > 3D AUDIO) chooses. `"auto"`, the
default, asks the system what it plays on, at the start and each second
after (`port/android/host/host_audio_route.c`, in the host, which the game
asks through `host_audio_headphones`); plugging headphones in or taking them
out mid-game crossfades each sound between the two over 100 ms, with no
click:

- macOS: the default output device: its name (External Headphones, AirPods,
  a headset), the built-in output's data source, Bluetooth (headphones
  unless its name says a speaker).
- iOS: the audio session's route (wired or Bluetooth headphones).
- Windows: the default endpoint's form factor (Headphones or Headset).
- Linux: the sound server's default sink (PulseAudio, or PipeWire's pulse
  server, through libpulse loaded at run time): its form factor, its active
  port, or Bluetooth; else ALSA's headphone or headset jack. The Steam
  Deck's 3.5 mm jack is headphones.
- Android: the devices the game's sound is routed to (Android 13 and
  later), or those connected: wired, Bluetooth and USB headsets and
  headphones, hearing aids.

A device it cannot tell is speakers. `"headphones"` and `"speakers"` choose
for good. 5.1 is panned around its speakers, and the VR mode keeps
`vr.hrtf`. The log says what the sound plays on as it changes ("audio: the
sound plays on headphones; the world's sounds through the HRTF").

The HRTF costs about 15 ns a 3D sound a frame on an Apple M2 core: the
mixer's bench (`tools/harness/tests/test_mixer.py bench 24 16 indoor hrtf`,
24 sounds indoors) takes 3.6% of a core through it and 1.9% panned (the VR
mode's HRTF took 4.7% before it had a pass of its own); a game on Blood
Gulch with bots, 2.0% and 1.4%. `test_mixer.py circle out.wav hrtf` (or
`pan`) writes a sound going round the listener, to listen to.

## Sound delay

A sound is heard some time after the game starts it: the game's sound update
in the next frame; the wait for the sound device's next request for frames,
at most `audio.buffer_frames`; the frames queued on the device ahead of that
request's, which SDL's driver keeps (none on the Mac: below); and the
system's own latency after (its output buffer; a MacBook's built-in speakers
add 27 ms of their own processing). The log says the last two as the device
opens ("sound: the device (coreaudio unit) asks for 256 frames at a time at
48000 Hz ...; 0.0 ms queued ahead of them, the system's latency after 35.0
ms"), and `debug.latency_log` times a shot's sound from the step the weapon
fires in (`game/latency_log.c`).

A smaller buffer is heard sooner, but each request has to be answered
sooner too, or the device runs dry: it plays silence, a click or a crackle.
The mixer runs on a thread of the host's that SDL's audio thread hands each
request to (`port/android/host/host_sdl.c`), at the same priority as SDL's
(but on Android; on the Mac a real-time thread, its period the device's
I/O cycle). The mixer never waits for the game's threads: what the game
sets and submits goes to its own side of each voice, and the mixer takes
it over at the start of each mix only if no game thread holds its lock,
else at the next (`src/dsound_sdl.c`, "the hand-off"; `debug.audio_log`
counts the mixes that left it). A game thread the scheduler sets aside
with the lock held used to make the mix wait up to 50 ms, and the device
run dry. The game times each answer against what the device had
queued (`src/dsound_sdl.c`, "the device's timing"), counts a dropout when it
waited longer, and logs it ("sound: the device ran dry ..."). With
`audio.buffer_frames = 0`, the default, the buffer starts at 256 frames (512
on Windows) and doubles, up to 2048, each time the device runs dry (not
counting the first 5 seconds after it opens, the game starting up); the
device opens again at the new size, a moment of silence. A number keeps
that size.

What the drivers queue (SDL 3.4):

- macOS: the game plays through CoreAudio's own output unit (AUHAL,
  `port/macos/host_audio_output.c`), not SDL's driver, and nothing is
  queued: the device asks for each buffer one I/O buffer
  (`audio.buffer_frames`, this program's own: other programs keep theirs)
  and its safety offset before it plays it, as the log measures ("the
  device asked for each buffer 6.83 to 6.89 ms before it played it ...; its
  I/O buffer and safety offset: 6.88 ms"). It follows the system's default
  output, or `audio.output_device`, as devices come and go or change rate
  (a moment of silence, logged, which does not grow the buffer).
  `HALO_AUDIO_QUEUE=1` plays through SDL's driver instead, an AudioQueue:
  three buffers, or as many pairs as make 15 ms (`SDL_coreaudio.m`, for
  Bluetooth headsets that record and play at once), all but the one asked
  for ahead, and then its own I/O buffer of 512 frames: 26.7 + 10.7 ms at
  256 frames (the least), 29.3 ms queued at 128, 32 ms at 512 and 768,
  42.7 ms at 1024.
- Linux (PipeWire): one buffer ahead, and the graph's cycle (its quantum)
  becomes the buffer, for the other programs on the device too. PulseAudio
  (without PipeWire) targets the buffer in all.
- Windows (WASAPI, shared): the buffer rounded to the steps the system's
  audio engine allows, within the driver's range: most drivers allow only
  10 ms (480 frames), so 512 is the engine's own period on most computers,
  and a driver that allows less gets 512 frames. Not measured here: no
  Windows computer to measure on.

On the Mac, what is left after the I/O buffer is the device's: its
latency, safety offset and stream latency (the MacBook Air M2's speakers:
70 + 74 + 1280 frames, 29.7 ms, most of it the speakers' own processing),
which no program can change. Headphones on the jack, a USB or HDMI device or
AirPods report their own (AirPods over Bluetooth far more).

Bluetooth headphones (AirPods) and controllers, on the Mac and iOS:

- The radio's buffering is the device's latency. The log names the
  connection and gives the time after the buffer as the device opens
  ("sound: CoreAudio's output unit on AirPods Pro (Bluetooth): ... (N ms
  after the buffer)"), and says when it changes ("sound: the system's
  latency is now N ms"). On iOS that is the audio session's output latency
  and I/O buffer.
- Game Mode (macOS 14 on Apple silicon, iOS 18) doubles Bluetooth's rate
  for AirPods and controllers, which Apple says lowers both delays. It turns
  on while a game is fullscreen in the system's own way: the app's
  `Info.plist` has the games category and `LSSupportsGameMode` (and iOS's
  `GCSupportsGameMode`). On the Mac that is the default fullscreen (and
  `display.mode = "borderless"`), which SDL puts in a Space of its own;
  `"fullscreen"` (the display taken) is not, so Game Mode probably stays
  off there (not tested). The menu bar shows a controller while it is on,
  and `log stream --predicate 'process == "gamepolicyd"'` says "Game mode
  status is now on".
- Voice chat's microphone is the only one the game opens, and only in a
  network game whose host has voice on: for all of it with open mic, and
  from the first press with push to talk. On Bluetooth headphones it moves
  them to their headset mode (lower quality, 16 or 24 kHz) until the game
  ends; the log says so as the sound moves with them.
- The dropout counter's slack is the I/O buffer alone, not the device's
  latency, so a Bluetooth device's long latency neither hides a dropout nor
  grows the buffer.

Measured on an Apple M2 MacBook Air (its speakers) and a Ryzen 9 5900X
(PipeWire, a null sink: its device's own latency is not in it), with the
menus' music, Blood Gulch with 15 bots at 120 ticks through the HRTF and in
5.1, and that with the processor kept busy beside it (8 and 24 busy loops):
a shot to its sound (the median of `debug.latency_log`'s), and the
dropouts:

| `audio.buffer_frames` | macOS: queued + system | macOS: shot to sound | Linux: queued | Linux: shot to sound | Dropouts |
|---|---|---|---|---|---|
| 128 | 0 + 32.3 ms | 40 ms | 2.7 ms | 4.4 ms | Linux, busy: 53 (PipeWire counted 160) |
| 256 | 0 + 35.0 ms | 43 ms | 5.3 ms | 8 ms | macOS, busy: once in 3 runs of 4 (it grew to 512) |
| 512 | 0 + 40.3 ms | 52 ms | 10.7 ms | 15 ms | none |
| 768 | | | 16 ms | 26 ms | none |
| 1024 | 0 + 51.0 ms | | 21.3 ms | 31 ms | none |

Through SDL's AudioQueue (`HALO_AUDIO_QUEUE=1`) the Mac had 26.7 + 40.3 ms
at 256 frames and 76 to 79 ms from a shot to its sound; 42.7 + 40.3 ms and
97 ms at 1024, the default before.

256 never ran dry on Linux with the processor busy, nor on the Mac in the
menus, the fights and the map loads; hence the automatic start. With the
Mac's processor kept busy by eight loops, the mixer at times waits 5 to 35
ms for the lock the game's thread shares with it (a thread holding it put
aside for others), which 256 frames (5.3 ms) does not ride out and the
AudioQueue's 26.7 ms mostly did: the buffer grows to 512 once, still 30 ms
sooner than the AudioQueue.

## Field of view

Video Setup's FOV AND VIEWMODELS screen sets the field of view of the
first-person view (`display.fov`) and of the weapon and hands
(`display.viewmodel_fov`), from 80 to 150 degrees in steps of 5, and can
hide the weapon (`display.viewmodel_visible`). DEFAULT, the setting's `0`,
keeps the stock view. `config.toml` takes any angle from 20 to 150.

- An angle is across the screen at 16:9. Another shape keeps the same
  angle up and down, as the stock view does.
- The field of view applies on foot in first person only. Vehicles, death,
  cinematics and scripted cameras keep their own view.
- A scope's zoom levels keep their stock view: the extra width fades out as
  the zoom comes in. A view narrower than the stock one narrows the zoom
  too.
- The reticles scale with the view, so that they stay on the aim. The
  scopes' pictures keep their place.
- By default the weapon keeps its stock view when the world is wider:
  arms and a gun right against the camera stretch at a wide angle.

LEGS (`display.first_person_legs`) draws your own body in first person
from the waist down, so you see your legs when you look down, walking,
running and crouching as everyone else sees them. It is the body others
see, posed as they see it: the upper body is folded away, and the body is
set back behind the eye as far as keeps the open waist below the view
(the way Halo 2 keeps its first-person body's open top out of sight), more
the further down you look. It is drawn with the world's camera, so it keeps
its place and size at any field of view. Hit boxes, markers and everything
the game does with the body are unchanged. In the VR mode the chest is drawn
too, under the real head (VR, "Your body").

Only this machine's view changes. Nothing the machines send each other
changes, so players with different settings play together.

## System link

The Xbox game lets 16 players on 4 machines play a system link game. This
port lets up to 128 players on up to 128 machines play. Each machine can
have up to 4 players (split screen).

- `include/halo_port_limits.h` sets the limits.
- `include/halo_port_capacity.h` sets the memory for the limits. The game
  state is 16 MB at `0x81A00000` (3.3 MB on the Xbox). The pools of objects,
  effects, particles, contrails, lights and sounds are also larger.

Obey these rules:

- All the machines in a game must use a build with the same limits.
- The port uses protocol version 2. It does not see the Xbox game or older
  builds of the port. They do not see the port.

These are the differences from the Xbox:

- The host waits up to 60 seconds (15 seconds on the Xbox) for the other
  machines to load the map.
- If a machine does not read the messages of the host for two seconds, the
  host removes it from the game.
- The saved games contain all the game state. Thus a saved game is 16 MB.
  Saved games from older builds of the port do not operate.
- In campaign and in games of up to 16 players, the game removes garbage
  (bodies, dropped weapons) as on the Xbox. In larger games, it keeps more
  garbage, in proportion to the players.
- The lobby shows the local machine and the first three remote machines.
  The other machines are also in the game.
- In free-for-all games, each player is a team.

Linux, Windows and Android machines can play in the same game. Each machine
simulates the players from the same inputs, and the host does not correct
all of the game. Thus each machine must calculate the same floating-point
results, and all the ports:

- Compile without fused multiply-add (`-ffp-contract=off`).
- Use the math functions of musl (`port/include/halo_math.h`,
  `port/third_party/musl-math`), not the math functions of the system.

### Play on one computer

More than one copy of the game can play on one computer. Each copy must
have a different loopback address. A copy with an address gets no
broadcasts. Thus each copy must send its broadcasts to the other copies.

For a host and two clients, enter these commands in three terminals:

```sh
HALO_NET_ADDRESS=127.0.0.200 HALO_NET_BROADCAST=127.0.0.201,127.0.0.202 build/linux_x86_64/halo
HALO_NET_ADDRESS=127.0.0.201 HALO_NET_BROADCAST=127.0.0.200 build/linux_x86_64/halo
HALO_NET_ADDRESS=127.0.0.202 HALO_NET_BROADCAST=127.0.0.200 build/linux_x86_64/halo
```

Do not give 127.0.0.1 to a copy. Each copy gets to its own address through
127.0.0.1. Linux and Windows send all of 127.0.0.0/8 to the loopback
interface.

Two games side by side (two tests at once, say) need ports of their own:
`network.port` (`HALO_NET_PORT`) moves a game's port from 5150 (the
clients' from 5151) to another and the next. Give the host and its clients
the same port. A Mac's loopback interface has 127.0.0.1 alone, so there the
port keeps two hosts apart. The map sharing port (5153) does not move:
turn `network.map_share` off in such tests.

### Test with many machines

`tools/system_link_bots.py` adds simple machines to a game. Each machine has
one player. The machines obey the system link protocol, but they do not
calculate the game or move their players.

1. Start a game on the host.
2. Enter `python tools/system_link_bots.py --host 127.0.0.200 --machines 127 --start`.

Each machine uses its own loopback address, from 127.0.0.2. The option
`--start` starts the game when all the machines are in the lobby. If the
host has no `network.address`, do not give `--host`. For a host with
another `network.port`, give `--port` the same port.

## Internet play

Machines with an invite link can play system link on the internet. This
project has no server.

When a copy of the game starts to host a system link game, it makes an
invite link: `openh1://join/<64 hexadecimal digits>`. The game writes the link
to the standard error and puts it on the clipboard. The links of older
versions of the game (44 digits) do not operate. The game writes a message
when it gets one.

To join a game, do one of these steps:

- Open the link. The game is the handler of `openh1://` links: at each
  start it registers the scheme (a desktop entry on Linux, the registry on
  Windows). `halo://` is OpenCE's scheme, which older versions of OpenH1
  registered too: the game removes a `halo://` registration that runs its
  own executable, and leaves OpenCE's. The game still takes the `halo://`
  links of older versions, from any of these steps. If the game
  already operates, the new copy gives the link to it and stops. A key in a
  file that only the user can read (`halo-ce-universal.key` in
  `$XDG_RUNTIME_DIR`, else `~/.halo-ce-universal.key`; on Windows in
  `%LOCALAPPDATA%`) encrypts the link, so the programs of other users cannot
  read it. On macOS and iOS the system gives the link to the app, running
  or started by it (`port/macos/host_links.c`).
- Copy the link (or the 64 digits) and go to the game.
- Enter `halo <link>`.
- Accept a Discord invite, or ask to join in Discord. Refer to "Discord".
- Open the join page's address (`https://openh1.com/join/#join=<64
  digits>`): refer to "Invites from anywhere".

The game also takes a web address with an invite
(`https://openh1.com/?join=<64 digits>`, or the join page's
`https://openh1.com/join/#join=<64 digits>`), as a link.

When the machines connect, the game of the host shows in Multiplayer,
System Link. Join the game as on a local network. System link on a local
network does not need an invite.

### Parties

Friends can group up before a game and go from game to game together
(`src/p2p_party.c`; the menus' side is `game/party_menu.inc`):

- Multiplayer > PLAY ONLINE > PARTY opens the Party screen. MAKE PARTY makes a party
  of up to 16 players, you its leader, and puts its link on the clipboard:
  `openh1://party/<32 hexadecimal digits>`. COPY INVITE copies an invite to
  paste in any chat (refer to "Invites from anywhere").
  Friends join it as they join an invite: they open the link, copy it and
  go to the game, use JOIN BY LINK, or PASTE LINK on the Party screen. The
  Party screen lists the members, marks the leader, and says where each
  one is.
- When the leader hosts an internet game, or joins one (by a link or from
  the Server Browser), every member joins it too: the Party screen opens
  over the main menu or Multiplayer, and joins the game. A member who
  cannot join (the game is full, a different version of OpenH1, a Legacy
  game that this version cannot join, a host that cannot be reached, or a
  LAN game) sees why in plain words ("Couldn't join your leader's game: the
  game is full."), and stays in the party; the leader sees it too. JOIN
  LEADER tries again. A member in another game joins when it goes back to
  the menus.
- In the Server Browser, the leader sees whether a game has room for the
  whole party: "Room for your party (4)", or the players dimmed and "No room
  for your party of 4". When the leader joins an OpenH1 game, its host holds
  a slot for each member for 20 seconds while they follow, so that no one
  else takes them and splits the party; in a team game it puts the party on
  one team when that team has room. A Legacy game's host, or an older
  OpenH1's, holds nothing: the browser says "Room for your party (4), but
  places aren't held", and the members join as they can.
- After a game the party is where it was. When the host leaves, the party
  is in the menus again, and the leader takes it to the next game.
- LEAVE PARTY leaves; the leader's party passes to the next member (also
  when the leader quits, or is not heard for 12 seconds). The leader can
  END PARTY for everyone.
- In an OpenH1 network game, right-click on the scores to pick a player:
  **Invite to party** invites them to your party (one is made if you have
  none), and **Join their party**, when they are in one, asks them to let
  you in. The player sees "Keyes invites you to their party" (or "Keyes wants
  to join your party") at the top of the screen for 30 seconds; J
  (`controls.party_accept`) or a controller's D-pad Down accepts, the same
  as opening the party's link, and doing nothing lets it go. The picked
  player's menu has Accept too. OpenCE players, bots, and players of an
  older OpenH1 are not offered these, and nothing is sent to their
  machines. Invites from a player you muted are not shown.

A party's messages go through the signalling brokers, encrypted with a key
from its link and signed by each member, on a topic of their own: OpenCE and
older versions of OpenH1 never see them, and a game without a party is as
before. Voice chat in a party is not done yet (the game's voice chat goes
through the host of a game).

### Matchmaking

Multiplayer > PLAY ONLINE > MATCHMAKING finds a game for you and your party
by playlist (`game/matchmaking.c`; the screen is `game/matchmaking_menu.inc`),
with no server of its own: it uses the server browser's public games and
the party's brokers.

- The playlists: TEAM SLAYER (4 against 4), SLAYER (everyone for
  themselves, 8 at most, parties of 2), CAPTURE THE FLAG (4 against 4), SWAT
  and BIG TEAM BATTLE (8 against 8 on Blood Gulch, Sidewinder and Boarding
  Action). Each has its gametypes, maps, players and the biggest party it
  takes.
- Pick a playlist and press A (or SEARCH). Your party's leader searches for
  everyone; a member sees that its leader searches. The panel shows the time
  searched, the games found and what the search is doing; CANCEL (X) stops
  it. The search goes on if you go back to Multiplayer, whose MATCHMAKING
  says SEARCHING.
- The search takes an OpenH1 game hosted for the playlist with room for the
  whole party, the nearest first (then the fullest), and joins it; the party
  follows, its slots held by the host (refer to "Parties"). A game it cannot
  reach or join is passed over.
- When none fits for 20 to 35 seconds, the party hosts one: the member with
  the best connection (one whose network is not strict, when the leader's
  is) leads and hosts, and the others follow. The game is public, named
  after the playlist, on one of its maps with its gametype. Others searching
  the playlist find it. It starts by itself 3 seconds after it is full, or
  after 90 seconds: with bots filling it in Slayer playlists, else with the
  players it has (2 at least).
- It never joins a Legacy game, nor an OpenH1 game of another version. OpenCE
  players can join a game it hosts, as any OpenH1 game.

### Friends

Multiplayer > PLAY ONLINE > FRIENDS keeps lasting friends for each player
profile (`src/p2p_friends.c`; the screen is `game/friends_menu.inc`), with
no server of its own: their messages go through the same signalling brokers
as invites and parties, sealed and signed, on topics only the players
concerned can work out.

- Each profile has a key of its own, made the first time and kept in
  `friends.txt` in the profile's folder (keep it to yourself: it is the
  profile's identity to its friends). Its **friend code** is a hash of the key,
  12 letters and digits in groups of four, `K7QM-2XPA-9DTR`. Its last character
  is a check: a mistyped character, or two swapped, is caught. Lower case, I or
  L for 1, O for 0 and the dashes are all taken.
- The first row is your code: COPY CODE puts it on the clipboard, and ADD BY
  CODE sends a friend request to a code typed (a code on the clipboard is
  filled in). The other player sees the request (and a toast); ACCEPT makes
  you friends, IGNORE turns it down for good (they can't send it again, until
  you add them). Two players who ask each other are friends at once. A
  request is sent again each start and every 5 minutes until it is answered
  or cancelled, so the other player need not be online.
- The list shows your friends online first (where they are: the menus, a
  party, hosting or playing a game, and which), then the requests, the
  friends offline, the requests you sent, your recent players and the codes
  you blocked. A toast says when a friend comes online, sends a request, or
  accepts yours.
- **JOIN** joins a friend's party, or, out of one, makes a party with them as
  its leader (the same spare party Discord's Ask to Join uses); the party
  then follows them into their games, as any party does. **INVITE** sends a
  friend your party's link (a party is made if you have none): they see "Keyes
  invites you to their party" with J or D-pad Down to join (Esc or D-pad Up:
  not now), as a Discord friend's ask; on a touchscreen they join from the
  Friends screen.
- **REMOVE** and **BLOCK** (each pressed twice) end a friendship on both sides.
  A blocked code's requests and invites are let go; UNBLOCK lets them in
  again.
- **Online status**: each game shares where it is with its friends only, on a
  topic and key that only they hold (a new key is made when you remove or
  block a friend, so they can no longer read it), once a minute and when it
  changes. APPEAR OFFLINE (or `friends.share_status = false`) publishes
  nothing: friends see you offline at once, and you still see them.
- **Recent players**: the last 50 players of your network games, with when and
  in what game. In an OpenH1 game each machine tells the host its player's
  friend code, which the host tells the other OpenH1 players: ADD FRIEND
  works on them here, and the scoreboard's player menu (right-click on the
  scores) has **Add friend**. OpenCE players, players of older builds and
  every player of a Legacy game have no code: they are listed, and can't be
  added.
- Internet play (`network.online`) must be on. With it, the game keeps its
  connection to the brokers while a profile has a code.

### Text chat

Network games have a text chat, as the PC version had. Press T
(`controls.chat`) to type a line to all the players, or Y
(`controls.team_chat`) to type one to your team (in a game without teams,
to all the players). While you type, Tab changes who the line is to, in
turn: all the players, your team, or your vehicle (the players riding in
the vehicle you ride in, whatever the map's vehicles are, Custom Edition
maps' too; on foot, only you see it). Enter sends the line, Escape cancels
it. While you type, your player does not move.

With a controller, D-pad Up opens an on-screen keyboard at the bottom right
(on Android, at the bottom in the middle) for a line to all the players:
the left stick (or D-pad Left and Right) moves between the keys, A types
the key, X deletes, Y switches capitals, D-pad Up and Down change who the
line is to (all the players, your team, your vehicle), Start sends the
line, and B closes the keyboard (the button that closed it does nothing in
the game until you let it go). Its last row has Aa (capitals), #+= (the
symbols), space, DEL and SEND. On a touchscreen (Android, iOS), the touch
controls' D-pad Up opens it, a tap on a key
types it, and a tap anywhere else closes it: the touch controls hide while
it is up. While the keyboard is up the controller and the touch controls
move nothing in the game. The face buttons are the same places on
other controllers (on a PlayStation controller: Cross types, Square
deletes, Triangle switches capitals, Circle closes).

The lines show above the motion sensor, in a column a little over half the
screen wide: a long line wraps onto the next rows, and the line you type
shows its end when it is wider. The player's name is in the colour of
their team (blue in a game without teams), the text in a pale blue, and the
host's notices in a pale gold; WHITE in CHAT TEXT in Settings > Network
Setup > Multiplayer HUD (`display.chat_style`) draws them all white, as
Custom Edition does. A line to the
team says "(team)" after the name, one to the vehicle "(vehicle)". The
lines go after ten seconds; while you type, the last six rows show. Where
the touch controls are (Android, iOS), the lines go at the top left, the newest highest. In split screen,
each player's view shows the lines that player hears, and the first player
types. No line shows, and none can be typed, in a menu, in a cinematic or
while the HUD is hidden; opening a menu ends the line being typed. A new
game starts with no lines. A line has 63 characters at most, letters,
numbers and punctuation but `|` only (ASCII; `|` becomes `?`). `debug.txt`
gets every line.

Streamer mode (refer to "Streamer mode") hides offensive words in the
chat's lines and players' names as it does in all the text the game draws:
only on your screen, never in what is sent, and never in the line you are
typing.

The host sends each line on: a player of an older build does not see the
lines and cannot send them. The host takes 5 lines at once from a machine,
then one each 1.5 seconds; a machine that joins starts afresh.

TEXT CHAT in Settings > Network Setup > Multiplayer HUD (`display.text_chat`)
can HIDE the chat entirely, for purists and streamers: no lines show, and
its keys open nothing.

The host chooses the text chat for its games with TEXT CHAT in Server
Setup > Teamplay Options (in co-op, Voice and Voting; `network.text_chat`):
ON (the default: a line with a link, such as `discord.gg/...`,
`example.com`, `www.`, an address, or `example [dot] com`, is refused),
WITH LINKS, or OFF. A domain that is a word as well (`.me`, `.to`, `.is`)
counts only with a path after it (`youtu.be/...`), so that `go.to` and
`he.is` are not refused. Every game has the text chat its host chooses,
public (listed in the server browser), private or LAN, a player's or a
dedicated server's; it is on by default. An OpenCE player, or one on an
older OpenH1, has none in a public game a player hosts: their game keeps
the chat off there. The host can mute a player (`/mute`, or MUTE FOR ALL in
Host Controls), and `display.text_chat` hides the chat on one machine.
Pressing T where the chat
is off says so, and a refused line tells its sender why.

On the scoreboard (right-click, then click the player), Mute voice and chat
mutes a player for you alone: you no longer see their lines or hear their
voice, and nobody else is told. The host's scoreboard also has Mute for
everyone: no machine sees the player's lines or hears their voice for 20
minutes, or until the host picks Unmute for everyone, and all the players
see "Milenko muted for 20 minutes". The mute holds in the lobby too. A
machine's voice is one: muting one of two players in split screen mutes
the other's voice too.

The host can also mute a player from the chat: type `/mute <player>
<minutes>`, as `/mute Milenko 5` (20 minutes when you give none, 1440 at
most), which mutes their voice too. The player is found as the console's `kick` finds one: the whole
name in either case, else the one player whose name begins with what you
typed. All the players see "Milenko muted for 5 minutes". The muted
player's lines go to no one, and they see how many minutes are left. The
mute ends when its minutes are up, or with `/unmute <player>`. A player
who leaves and joins again under the same name, in either case, is still
muted. Only the
host mutes: a player's `/mute` is not sent.

### Voice chat

Network games have voice chat, on the local network and on the internet.
Hold V (`controls.push_to_talk`, Settings > Controls Setup) to talk, or set
VOICE CHAT in Settings > Audio to OPEN MIC. VOICE VOLUME sets the volume of
the other players. On Android, set VOICE CHAT to OPEN MIC to talk: the game
then asks for the microphone.

OUTPUT DEVICE and INPUT DEVICE in Settings > Audio select the speakers and
the microphone (SYSTEM DEFAULT, the default, follows the system). The list
has the devices that the computer had when the game started.

The host sets voice chat for all players in Server Setup > Teamplay Options
(`network.voice_*` in `config.toml`):

- In the lobby, before and after a game, all players hear all players
  (LOBBY VOICE CHAT).
- During a game (VOICE CHAT): OFF; TEAM NEAR; ANYONE NEAR; TEAM; or TEAM,
  ENEMIES NEAR (the default). Near is VOICE NEAR DISTANCE (45 metres is the
  default), and both players must be alive. A player who is near sounds
  quieter when further, and from their direction. In a game without teams
  all players are enemies; in co-op, all are teammates.
- The quality (VOICE QUALITY), 8 to 64 kilobits per second (24 is the
  default), in the lobby and in the game.

In co-op, Server Setup has no Teamplay Options: the same settings are in
Server Setup > Voice and Voting.

A speaker shows next to the name of a player who talks: in the lobby, on
the scoreboard, in a list at the left of the screen during a game, and
beside the name above their head (when names are shown there:
`display.player_names`). To mute a player, open the scoreboard,
right-click, click the player, and click Mute voice and chat. You no longer
hear them or see their text chat lines, and nobody else is told. The host's
Mute for everyone silences a player's voice for every machine (refer to
"Text chat").

The host sends each voice on to the players who can hear it. The host
accepts a voice only from the machine of the player (with a key that it
gives each machine on its connection), only at the quality it set, at most
50 frames each second from a machine, and from at most 8 players at a time.
The voices are compressed with Opus (`port/third_party/opus`).

### Server browser

Server Setup in Create Game > Internet has a LISTING row:

- PUBLIC (the default): the game also shows in Join Game > Server Browser
  on every machine. Anyone can see and join the game.
- PRIVATE: only players with the invite link can join.

Each new game starts as PUBLIC (`network.host_public = false` makes new
games start as PRIVATE). An online co-op game starts as PRIVATE, and keeps
the last choice of its LISTING (`network.coop_public`). A LAN game is never listed. `network.public_lobby = false` turns the server browser off.

A PUBLIC game can also have a PASSWORD (a row of Server Setup, below
LISTING). The Server Browser shows a lock at the left of a game with a
password. A player who selects that game must type the password, and JOIN
GAME joins only with the correct password. The invite link of the game joins
it without the password. The host keeps the password only while the game
runs.

The Server Browser, the LAN list and Join by Link are laid out as Settings
is: tabs across the top, the games under their columns (Server, Map, Mode,
Players, Ping), the chosen game's details in a panel at the right with JOIN
GAME, and the keys at the bottom (F Favorite, R Refresh, Tab Sort, Esc Back
on the keyboard; a controller's Y, right trigger, X and B). A name, map or
mode too long for its column ends in "..."; the panel shows all of it. A
click selects a game, and a click on the selected game, a double click, A,
Enter or JOIN GAME joins it. The game joins the invite of the game, as for a
link. When it reaches the host, it opens the lobby. If it cannot reach the
host in 10 seconds, it marks the game failed, and the panel says why in
words for anyone, with no technical terms ("Couldn't join. Your network is
blocking it."; with the game's name when another game is chosen); the log
has the cause (`menus: could not reach
...`: whether a signalling broker answered, whether the host did, and
whether this network's NAT or the host's is strict). The connecting box
that a link (PASTE LINK) or a game opens says the same, with what to try:

| The box says | When |
| --- | --- |
| INTERNET PLAY IS OFF | `network.online` is off |
| CAN'T GET ONLINE: check your internet connection | No signalling broker is reached |
| GAME NOT FOUND: the game may have ended, or its host is offline (ask the host for a new invite link; a server browser's game: try another) | The host never answered (an invite works only while the copy of the game that made it runs) |
| NETWORK BLOCKED: your network is blocking the connection | This machine's NAT gives each destination its own port and no router forwards `network.tunnel_port`: it says to turn on OPEN ROUTER PORT (`network.allow_upnp`) if that is off, else to try another network (on a phone, Wi-Fi instead of mobile data) or have someone else host |
| CAN'T REACH THE HOST: the host's network is blocking the connection | The host's addresses show that its NAT is the strict one: the host is asked to turn on OPEN ROUTER PORT |
| CAN'T REACH THE HOST: a network is blocking the connection | The host answered, but no path opened, and neither side is known to be strict |

Port forwarding (a fixed `network.tunnel_port` forwarded on the router)
helps too, on a computer: refer to "Connection".

The Ping column shows the round trip to each host in milliseconds, coloured
by how good it is, with bars before it. The browser times it as a join would
reach the host: it makes the tunnel with the listing's invite, keeps the
shortest of three pings, and closes the tunnel again. It times a few hosts
at once, each again every two minutes and on Refresh. Joining a host whose
tunnel is open uses that tunnel. While it is timed the column shows a faded
"…"; a game with a password, whose invite the listing hides, a host not
reached and a LAN game show "–". Refresh also asks the hosts for their
listings again.

The Mode column names an OpenCE gametype's kind (SWAT, INFECTION, GUN GAME,
or HC and the game engine for Hardcore: HC CTF, HC SLAYER), which the host
sends with the game (network version 37), and otherwise the game engine's
(SLAYER, CTF, KING OF THE HILL). The preview of a game before joining it
names it the same way. An OpenH1 game shows its tick rate in a badge after
its name ("60", "120"); a Legacy game shows none. The panel names the
chosen game's kind (OPENH1 or LEGACY), its tick rate (a Legacy game's 30),
and OPENCE CAN JOIN for one OpenCE players join too (an OpenH1 game at 30,
the default, and every Legacy game: NETCODE.md, "OpenH1 and Legacy games").

Sorting and the tabs, in all three lists, for this run of the game:

- Choosing a column's title sorts the games by it; choosing it again sorts
  the other way. Click or tap the title, or with a controller press up from
  the first game to reach the titles, left and right to pick one, and A.
  The title sorted by is brighter, with an arrow its way. Players sort the
  most first; a ping not known goes last either way. Tab (X) sorts by the
  next column.
- The tabs (Q and E, LB and RB, or a click; not in Join by Link) show every
  game, OpenH1's own only (those OpenCE players can join too among them),
  Legacy ones only, co-op games, or the favorites.
- Favorites stay at the top, sorted the same way among themselves; those
  not found stay at the bottom of All and Favorites, grey.

The browser lists two kinds of games: the dedicated servers that the
master servers list (`masters.txt`, `network.masters_file`; the official
master, `https://master.opencommunityedition.com`, built in, when the file
names none), and the games
that players host, which come through the brokers. `network.listings`
chooses the kinds that the browser asks for: both (the default), only the
master servers, or only the brokers.

- The top-left tab chooses the games that the list shows: DEDICATED (at
  first), P2P GAMES or ALL GAMES (left and right, `network.browser_source`).
  When `network.listings` is `"masters"`, the tab shows DEDICATED only;
  when it is `"brokers"`, P2P GAMES only.
  PLAYERS, the sum of the players in the games shown, is in the middle tab.
- The column titles sort the list (A or Enter on a title, or a click).
  The titles are: server name, map, game, players, the lock (the games
  with a password first) and the dedicated icon (dedicated servers first).
  A second choice of the same title reverses the order, and an arrow on
  the title shows the direction. The default order is OpenH1 games
  first, the dedicated servers first among them and among the Legacy games
  after them, then the games with the most players, then by name. The browser
  keeps the order in `network.browser_sort`.
- FILTERS opens the Filters screen of Custom Edition. It has SHOW EMPTY
  SERVERS, SHOW FULL SERVERS, GAME TYPE (ANY or one game engine) and SHOW
  MISSING MAPS (NO, YES, or IF DOWNLOADABLE, for a Custom Edition map that
  Map Sharing can download when you join). OK writes the
  `network.browser_*` settings. Settings > Network Setup > SERVER BROWSER
  writes the same settings, and also shows LISTS, SORT BY (DEDICATED FIRST,
  PLAYERS, NAME, MAP, GAME TYPE or LOCKED FIRST; an order that a title reversed
  shows as the same choice) and MASTER SERVERS
  (the number of master servers that answered the last request of the
  browser, for example `2 OF 2`). The PING and TEAMPLAY OPTIONS rows of
  Custom Edition are not on the Filters screen.
- A dedicated server's row shows the dedicated icon of Custom Edition.
  Instead of a ping, a dedicated server shows its region (EU, NA-E),
  because the browser does not ping the servers that the master servers
  list. The row shows JOINING while a join runs. A game of another network
  version stays in the list in grey, after the other games, with NEWER or
  OLDER. Its row does not join the game: it shows the message about the
  version. The server count above the list adds the number of dedicated
  servers (`SERVERS: 8 (5 dedicated)`) when the list shows all the games on
  one page. PAGE shows only when the list has more than one page.
- The two lines below the list describe the selected game. The first line
  shows its players, map, game and region. The second line shows where the
  game comes from: "Dedicated server by <publisher>, listed on N master
  servers" (the operator's name comes from the master server, and only this
  line shows it), "Hosted by a player (anonymous listing)", "Password
  required", or, for a game of another version, the update that it needs.
  During a join, the second line shows the steps of the join, and how many
  seconds it has taken. The steps are: the request to the brokers for the
  host, the answer of the host and the connection attempt (UDP hole
  punching), and the connection to the host. If the join fails, the line
  shows the reason:
  - the host did not answer;
  - no route to the host (the line says strict NAT when that is the cause);
  - the host refused the join: the game is full or closed, this machine is
    banned, the password is wrong, the version is different, or the invite
    changed.
  The browser shows a refusal when it opens again after the main menu
  error.
- If no master server answers but the brokers do, the second line shows "No
  master server answered: showing player-hosted games only", and the list
  shows the games from the brokers. An error panel covers the list in two
  cases: internet play is off, or nothing answers (no master server and no
  broker) after 15 seconds. OK, Enter or B closes the panel.
- In the lobby of a dedicated server, the panel at the right shows the
  name of the server and "dedicated" on its first line. It shows "Waiting
  for players" while the server waits for enough players
  (`server.min_players`) to start its countdown.

How it operates (`src/p2p_lobby.c`):

- The host of a public game publishes a listing: the invite, and the name,
  map, gametype and player counts of the game. The listing goes to the
  same MQTT brokers as the invites (`network.brokers_file`), retained,
  to a topic of the host (`hceu/3/lobby/s/<hash of its key>`).
- The key of the host signs the listing (Ed25519). The key is the key of the
  invite, so no other machine can list the invite of the host, change its
  listing, or list a game with false details.
- The host publishes the listing again every 30 seconds, when the game
  changes, and when a browser asks. If the topic of the host is empty or
  holds another listing, the host publishes again in 5 seconds or less. Thus
  a broker that deletes the listing does not remove the game.
- When the host stops (it stops hosting, the game becomes private, or the
  game quits), it publishes a closed listing, and then empties its topic.
  If the host loses its connection, the broker empties its topic (the will
  of the connection). A browser removes a game that it does not hear for 90
  seconds.
- When a public game becomes private, the host makes a new invite. Thus a
  player who saw the listing cannot join with the old invite.
- The listing of a game with a password does not hold the invite in clear
  text. The secret part of the invite (its token) is encrypted with a key
  from the password (Argon2id, salted with the key of the host, then
  XChaCha20-Poly1305). The browser makes the key from the password that the
  player types, and opens the invite only if the password is correct. When
  the host sets or changes the password, it makes a new invite. A player who
  has the listing can try passwords on their own machine without the host,
  so use a long password.

A public game does not publish the address of the host. But any machine
with the invite can ask the host to connect, and the host then sends its
addresses. Thus anyone can learn the address of the host of a public game,
as for any public server.

The brokers are in `brokers.txt` (from `port/assets/network/brokers.txt`:
`share/openh1/brokers.txt` in the Linux release, next to the executable in
a build and on Windows; on Android, the app writes it next to `config.toml`
at each start), one `host:port` on each line. A `brokers.txt` next to
`config.toml` comes first. The game uses
all of them at once (up to 4), so one that works is enough. Without the
file (a game run from somewhere the builds did not put it), the game uses
its own copy of the list. An update
replaces `brokers.txt`: to use brokers of your own, put them in another
file and name it in `network.brokers_file`. All the players must use the
same broker to see each other's games. The game uses
MQTT 5 if the broker has it, else MQTT 3.1.1. A broker that does not keep
retained messages, or does not let clients subscribe with wildcards, carries
only invites, not listings.

### Map sharing

When you join a game on a Custom Edition map that you do not have, the game
downloads the map from the players of the game, if the host shares its maps
(SHARE MAPS in Server Setup > Map Sharing, `network.map_share`, on by
default). This operates on a local network and
on the internet. It also downloads the resource maps that the map needs
(`bitmaps.map`, `sounds.map`, `loc.map`) if you do not have them. A line at
the top of the screen shows the progress. In the lobby, the game starts when
every player has the map. If the game already operates, you join it when
the download is complete. If no player sends any of the map for a minute,
you leave the game, and the main menu tells you why. If the downloaded map
cannot be played, the game deletes the files that it downloaded, and you
leave the game.

All the players help (HELP SEND MAPS in Settings > Network Setup > Map
Sharing, `network.map_seed`), as the peers of a torrent do:
the downloaded map comes in pieces from the host and from each player who
has it or has a part of it. The game checks each piece against its hash, so
a player cannot send a different map. A player who sends a bad piece is not
asked again. To keep the upload free for the game, the host sends at most
`network.map_share_upload` (1024 KB/s: UPLOAD LIMIT in Server Setup > Map
Sharing) and each other player at most `network.map_seed_upload` (256 KB/s:
UPLOAD LIMIT in Settings > Network Setup > Map Sharing). Set these lower on
a slow connection. DOWNLOAD MAPS there (`network.map_download`) turns
downloading off: you then leave a game whose map you do not have.

Maps keep their names. A game is on a map by its name (`hugeass`), and says
which version of it by the start of the map's hash, so a game always loads
the version of the host, whatever your copy is called. A downloaded map
takes its own name (`hugeass.map`); if you have another version of that
name, the download is named with the start of its hash
(`hugeass_0e2b3ee4.map`), and the menus show it as "Hugeass (2)". A map that
an earlier build renamed this way gets its name back, unless you have a map
of that name. A download that stops leaves a `.part` file in `custom_maps`
(only of the last map whose download stopped, and only if it holds part of
the map); the next download of that map continues from it. The host shares
maps in the folder of a Halo Custom Edition install (`paths.custom_edition`)
too; downloads go to `custom_maps`.

Over the internet, the players of a game connect only to the host. To send
each other a map, a player answers the other players of the game (who have
its invite) as the host does, and a player who downloads connects to them in
the same way that it connected to the host. Map sharing uses port 5153 for
its connections. Refer to `NETCODE.md`, "Map sharing".

### Security

Only machines with the invite can find the game:

- Each copy of the game makes an Ed25519 key pair when it starts, and from
  it an X25519 key pair (Monocypher, `port/third_party/monocypher`). Its
  identifier is from the hash of its X25519 public key. The Ed25519 key
  signs the listing of a public game.
- The link contains a 16-byte hash of the public key of the host and a
  random 16-byte token. The identifier of the host is from the first 6
  bytes of the hash.
- The machines exchange their public keys and addresses through public MQTT
  brokers (`network.brokers_file`). The topics are HMACs of the token.
  A key from the token encrypts and authenticates the messages
  (`src/p2p_signal.c`, `src/p2p_crypto.c`). The host authenticates its answer
  with a key that only it and the player can calculate. Its public key must
  agree with the hash in the link. The hash is long, so no other machine can
  find a key with the same hash.
- Then the player shows in the same way that it has the private key of its
  public key. Only then does the host make a session for the player. Thus
  other machines with the invite cannot make sessions in the name of a
  player (such a session would keep the player out).
- Each two machines get the keys of their packets from their key pairs and
  a random number from each. The keys do not go through the brokers. Thus
  other machines with the invite cannot read or change the packets.
- Each packet is encrypted and authenticated, with a different key in each
  direction. A machine ignores a packet that it already received.
- A machine can send only to the ports of the game on the other machine.
- The host makes one session from each request of a player. If a person
  sends a copy of an old request again, the host ignores it. A player that
  must ask again sends a new request.
- The host tries to reach at most 8 new players at the same time. The
  other players ask again.
- The host answers a request that is not proven at most one time each
  second through each broker. It answers at most 20 of these requests each
  second, after a first 32. Each answer goes only through the broker that
  brought the request. Thus a flood of requests does not use much of the
  bandwidth of the host.
- The host does the key work of at most 20 requests each second from keys
  that it does not know, after a first 32. It keeps the key work of the
  last 256 keys. Thus the proof of a player does not need more key work. A
  flood of requests can make players join more slowly. A player asks again
  for 90 seconds.
- The host refuses the predicted movement of a player whose game runs
  faster than time (a speed hack). If the messages on the player's
  connection were also ahead for ten seconds, the host drops and bans the
  player: each player sees who in red on the console, and the host adds a
  line to `cheaters.txt` and `bans.txt` (beside `debug.txt`) with the
  address and hardware id of the player, and the Discord name and id that
  the game of the player told it, marked `(self-reported)` (a player can
  change these). The host refuses a machine whose address or hardware id is
  in `bans.txt`. If only the player's datagrams were ahead, the host does
  not drop the player, because another machine can send datagrams with the
  player's address: it adds an `unverified` line to `cheaters.txt`.
- The host can ban a player with `ban <player name>` in the developer
  console (Tab completes the name), or from HOST CONTROLS (its pause menu,
  above). Unban a player in HOST CONTROLS > BANS, or remove their line from
  `bans.txt`. Refer to `NETCODE.md`. `kick <player name>` drops the player the same
  way, but keeps nothing: no line in `bans.txt`, and the player can join
  again at once. In co-op, `bringto` brings every player to the host.
- Players can vote to kick a player. The host turns this on or off with
  VOTE KICK in Server Setup > Teamplay Options (`network.votekick`; in
  co-op, Server Setup > Voice and Voting). Hold the scoreboard key, right-click to
  show the pointer, and click the name of the player. Then click **Start
  a vote to kick**. With a controller (or the keyboard), hold Back (the
  scoreboard key): a player is lit, and X (Enter) opens their menu. The
  D-pad (the arrow keys) moves the light and pins the scoreboard open, so
  Back can be let go of; while it is pinned the player stands, the left stick
  moves the light too, A (Enter) opens the menu, and B (Esc) or Back
  closes the scoreboard. In the menu, up and down pick an item, A (Enter)
  does it and B (Esc) goes back. In split screen each player picks on
  their own view with their own controller. Other players vote in the same way, and see the vote on
  the scoreboard. `votekick <player name>` in the developer console does
  the same. The host also gets **Kick** and **Ban** in this menu: these do
  the same as the `kick` and `ban` commands (click **Ban** two times). The
  host counts the votes, and these rules prevent abuse:
  - The vote passes when more than half of the players vote for it, and at
    least two players. The player of the vote is counted, but cannot vote.
    Thus, in a game of two equal teams, one team cannot kick a player of
    the other team without help.
  - The host counts one vote for each internet address (for internet
    play, the real address of the player, not the address of the tunnel).
    Two machines at one address, or with one hardware id, have one vote.
  - To start a vote, a player must have played for
    `network.votekick_minutes` (5) on this server. To vote, a player must
    have played for 2 minutes (or less, if that setting is less). The host
    counts the time. When a player joins again, the time starts again.
    Players who cannot vote yet are not counted.
  - The host only accepts a vote that comes on the connection of the
    player, not a datagram, which another machine can send with the
    address of the player.
  - One vote runs at a time, for 45 seconds, with 30 seconds before the
    next vote. If a vote fails, the player who started it cannot start a
    vote for 5 minutes, and nobody can start a vote against the same player
    for 10 minutes.
  - Nobody can vote to kick a player of the host.
  - A player kicked by a vote cannot join again for
    `network.votekick_ban_minutes` (30), by address and hardware id.
  - A player who leaves during a vote against them is kept out the same
    way, as if the vote passed. While the vote runs, the host refuses the
    player if they try to join again. Thus nobody can avoid a vote by
    leaving.
  So that every player can be named, the host trims the spaces around a
  name and removes characters that draw as nothing. A
  letter with a mark is typed as the plain letter (`ban jose` for "José").
  A name with nothing left to type becomes "Player", and a name that another
  player already has gets a number ("Player 2"). The game refuses a profile
  name that is blank, and a multiplayer game refuses a profile whose name was
  made blank before this check.
- An invite operates while the copy of the game that made it operates.

### Connection

Each machine gets its public address from public STUN servers. Then the two
machines send packets to each other until the packets get through (UDP hole
punching). There is no relay.

Some networks give a different port for each destination (for example some
mobile and company networks). Two machines behind such networks cannot
connect. To connect, forward `network.tunnel_port` on the router of one of
the machines.

The game can ask the router to forward the port (UPnP,
`src/posix_upnp.c`, with `port/third_party/miniupnpc`):

- The host asks its router when a player uses its invite.
- A player that joins asks its router when it does not reach the host in
  5 seconds.
- The forwarded port is one more address that the machine gives to the
  other machine.
- The forward has a duration of one hour. The game makes it longer while
  it operates. When the game stops normally, it removes the forward. It
  does not remove the forward after a crash, or if a request to the router
  is still under way 3 seconds after the game starts to stop. Some routers only make forwards without a duration.
- When the game finds the router, it removes the forwards to this machine
  that have the description "Halo internet play" and that no copy of the
  game uses now (forwards that a copy of the game did not remove).
- UPnP does not help behind a second NAT, for example the NAT of a mobile
  network provider. Then the router has a private address, and the game
  does not ask.

To stop all UPnP requests, set `network.allow_upnp` to `false`.

In the game, each machine has an address in 100.64.0.0/10:

- `src/xnet.c` gives the datagrams that the bound UDP sockets of the game
  send to such an address to `src/p2p.c`. Other datagrams (of sockets that
  are not bound yet, or that are connected to the address) and the TCP
  connections of the game go through local sockets on 127.0.0.1 (or
  `network.address`). The traffic from the other machines comes to the
  game from local sockets too.
- `src/p2p.c` sends that traffic through one UDP socket. UDP datagrams go
  as they are. TCP connections go as KCP streams (`port/third_party/kcp`).
- The broadcasts of the game go to all the machines. Thus the game of the
  host shows on the other machines.

### Invites from anywhere

Steam's chat, and most other apps, make only `https://` links clickable,
not `openh1://`. COPY INVITE (on the Party screen, and in the lobby's footer)
copies a short message for any chat (`p2p_invite_message`):

```
Join my OpenH1 party: https://openh1.com/join/#party=<32 hexadecimal digits>
Or paste this into OpenH1's Join by Link: openh1://party/<32 hexadecimal digits>
```

In a party it is the party's invite: a friend joins the party, then follows
its leader into the lobby or the game. Out of a party, in an internet game,
it is the game's (`#join=` and `openh1://join/`, 64 digits).

The address opens the join page (`website/public/join`, on openh1.com). The
code is after `#`, which a browser never sends to the site. The page opens
the `openh1://` link (the game is its handler on Windows, Linux, Android,
macOS and iOS), shows the link to copy into JOIN BY LINK (or the Party
screen's PASTE LINK; the game also joins a link that it finds on the
clipboard when it comes to the front), and shows the downloads for a friend who does not
have OpenH1 yet. The game takes the join page's addresses, as it takes every
other form of the links (`openh1://join/`, `openh1://party/`, older
versions' `halo://join/` and `halo://party/`, a bare code, `?join=`,
`?party=`).

### Discord

If the Discord desktop client operates, Discord shows what you do in
OpenH1 (`src/p2p_discord.c`, through the application of
`discord.application_id`; `discord.rich_presence = false` shows nothing):

- In the menus: "In the menus", and in a party "In a party (3 of 16)".
- When you host: "Hosting OpenH1 120 Tick · Slayer on Blood Gulch", and
  "In the lobby" or "Playing" with the number of players (or the party).
- In the game of another machine: what the game is, the same way.
- In the campaign: "Campaign on" the level.

Friends join you through Discord (`discord.allow_join_requests`):

- A friend clicks **Ask to Join** on your status. The game shows "Bob asks
  to join your party" for 30 seconds, over the menus or at the top of the
  game: J (`controls.party_accept`) or a controller's D-pad Down lets them
  in, Esc or the D-pad's Up declines. The keys are taken for the answer
  only (Esc does not also go back or pause). An ask not answered is
  declined.
- You send **Invite to Join** from Discord.
- In a party, they join your party (its link is the activity's join
  secret), then follow its leader into the lobby or the game. When you play
  an internet game out of a party, they join the game (its invite). In the
  menus out of a party, a party is made for them, you its leader: when you
  let them in, or when a friend that you invited arrives (the game listens
  for them on the party's topic, as long as Discord has its link). In a LAN
  game, or the campaign, no one can join through Discord.

Discord gives the secret only to the friends that you invite or let in.
When you accept an invite in Discord, or a friend lets you in, the game
joins it as it joins a link. If the game does not operate, Discord starts
it. The game connects only to a Discord client of the same user.

#### OpenH1's Discord application

Discord shows the name of the application (`discord.application_id`) as
the game that you play, and its pictures. OpenH1's application is
`1561963518344036434`, made at
<https://discord.com/developers/applications> with the App Icon
`port/assets/icon/openh1-icon.png` and these three images in **Rich
Presence** > **Art Assets**, with these names exactly (the game sends the
names; Discord can take a few minutes to show new art):

| Name | File | Where Discord shows it |
| --- | --- | --- |
| `logo` | `port/assets/discord/logo.png` | The large picture, always |
| `party` | `port/assets/discord/party.png` | The small picture in its corner, in a party |
| `host` | `port/assets/discord/host.png` | The small picture, when you host |

The SVG files beside them are their sources (1024 × 1024 pixels). To try
another application on one computer, put it in `config.toml`:
`application_id = "<id>"` under `[discord]`. The game sends the small
pictures only with an application other than OpenCE's
(`1553978809840050229`), whose art has `logo` alone. Ask to Join, Join and
Invite to Join need no other setting of the application.

## Dedicated server

`halo --server` (or `server.enabled = true`) runs the game as a host with no
window, sound or player of its own, for a VPS, a container or a game panel.
It hosts the games of `server.rotation` one after another: the lobby counts
down when `server.min_players` are in, the game is played, its scores show,
and the lobby opens with the next entry. Players join it as they join any
host: on the LAN, with its invite link, or from the server browser.
`docs/dedicated_server.md` is the reference: the command line, the exit
codes, the commands, the HTTP API, the remote console, the events and the
files.

```sh
./halo --server                                  # the settings of config.toml
HALO_SERVER_ROTATION="bloodgulch:ctf, prisoner:slayer+score=25" ./halo --server
./halo --server --config /srv/opence/config.toml
```

The server needs the game data (`maps/`, as for the game); without
`maps/ui.map` it stops at once with exit code 2. At the first start it
writes the `server.*` settings to `config.toml` (they are written only by a
server) and a web password if there is none. It writes one line for each
event to the standard output (`server.log_format = "json"` for JSON
lines), and the game's own log to `debug.txt` as the game does
(`paths.logs` moves it, so that the data root can be read-only).

- **Commands.** The standard input, `init.txt` (`server.init_file`) and the
  web page take the same commands: `sv_status`, `sv_players`, `say`, `tell`,
  `kick`, `ban <player> 2h <reason>`, `unban`, `mute`, `sv_map`,
  `sv_map_next`, `sv_end_game`, `sv_rotation_set`, `sv_set <setting>
  <value>`, `sv_reload`, `sv_ban_address <address|hardware id>`,
  `sv_stop [now|game|empty] [notice]`, `sv_restart`, `quit`. `help` lists
  them. `sv_stop game` (or `empty`) withdraws the listings, refuses new
  players and stops once the game is over (or nobody plays), without
  cutting a game short; `sv_stop cancel` undoes it. The game's own console
  commands need the owner role: the standard input and `init.txt` take
  them, and the web page and the remote console with
  `server.web_owner_password` from the machine itself; with another
  password they do not (and `kick` and `ban` are always the server's).
- **Web page.** `server.web_port` (8080, on `127.0.0.1`) serves an admin
  page, a JSON API, a remote console over WebSocket, `/healthz` and
  `/metrics`. Sign in with `server.web_password` (the admin); the moderator
  and viewer passwords give fewer rights, and `server.web_owner_password`,
  from the machine itself alone, the owner's. To reach it from another
  computer, use an SSH tunnel, or a reverse proxy with TLS,
  `server.web_bind` and `server.web_trusted_proxy`; bound beyond loopback,
  every web password must have 8 characters or more.
- **Signals.** SIGTERM and SIGINT stop the server cleanly (a game in
  progress shows its scores, every machine is told, the listings are
  withdrawn); SIGHUP reads the settings and the rotation again.
- **Listing.** A public server (`server.public`) with `network.online` is
  published to the master servers of `masters.txt`, signed with the
  operator's key: `halo --server --keygen` makes `publisher.key` and prints
  the public key, which the operator gives to a master server's Discord
  bot: `/apply` in the master's Discord server (the master's `/apply` page
  links to it), with `halo --server --sign <nonce>`'s proof of key. Until a
  master approves it, the server is not listed
  there (its log says so), but players can join it with its invite link
  and on the LAN. A change the lists show (public or private, the
  players, the name, the map) is published within 5 seconds; made
  private, the server makes a new invite and takes itself off the
  masters' lists, as `network.public_lobby = false` does
  (`docs/dedicated_server.md`, "The listing"). `server.list_on_brokers`
  also lists it with the games players host. A player's public game is published to the masters too
  when `publisher.key` is next to `config.toml` (not as a dedicated
  server), and withdrawn when it ends. `docs/master_protocol.md` is the
  protocol.
- **Ports.** The tunnel's UDP port (`network.tunnel_port`, 2302 for a
  server) must be reachable from the internet; `network.public_address`
  names the address and forwarded port behind a NAT. The web port is TCP
  and best kept private.
- **Updates.** `server.auto_update = "install"` installs a new release once
  the server has been empty for 10 minutes (on a thread of its own: the
  server goes on meanwhile), and stops with exit code 5, once it is empty,
  so that what runs it (systemd, Docker, a panel) starts the new build. An
  install that fails is tried again at the next check, 6 hours later.
- **Probe.** `halo --probe <invite>` prints the state of the invite's game
  (a server's name, map, gametype and players) as one line of JSON, for a
  list site, and exits: the game of the invite's host alone. It never
  starts a server, whatever the settings say, and writes nothing to
  `config.toml`.

OpenCE's `deploy/` folder (a systemd unit, a Docker image, a Helm chart, panels'
templates, example gametype files and `init.txt`) is not in OpenH1; its
files work with OpenH1's server too, run as `bin/openh1 --server`.

**Fleets.** For many servers on many machines, beside the panels,
`opence-fleet`
([github.com/OpenCommunityEdition/opence-fleet](https://github.com/OpenCommunityEdition/opence-fleet))
is one control plane over them all: an agent on each machine runs its
servers and drives each through the API as the owner, from the machine
itself (`server.web_owner_password`), drains a server before an update
(`sv_stop game`, `POST /api/stop`) and bans an address on every server at
once (`POST /api/bans`); `docs/dedicated_server.md`, "Panels and fleets".

## Mods

A mod is a folder in `mods/` in the data root with a `mod.toml` and a
`tags` folder of Halo PC tag files, as the Halo Editing Kit and Invader write
them: sounds, bitmaps, string lists and models that play over the maps' tags
of the same names, on this machine only (a high-res audio or texture pack, a
translation, higher-detail models that keep the map's nodes, markers and
regions). Every mod in the folder is on, in alphabetical order moved as the
mods ask (`load_after`, `load_before`); the main menu's Mods lists them as a
resource pack list does, the mod at the top playing over those below it,
turns them off and moves them, and keeps that in `mods/load_order.toml`.
Mods that say they cannot be played together (`incompatible`) are asked
about at startup. The console's `mods_list` lists them, `mods_reload` reads
their files again and `mods_enable false` plays the maps' own tags. The game
checks every file before it plays it, and leaves out (with a line in
`debug.txt`) one it cannot trust or that does not fit the map's tag it
replaces. A mod changes nothing that the machines of a
network game agree on. Refer to [docs/mods.md](../../docs/mods.md).

## Streamer mode

Streamer mode (Settings > Network Setup > Streamer Mode, `streamer.enabled`)
hides offensive words as stars wherever the game draws text that other
people or files wrote: the text chat's lines, players' names over their
heads, in the chat, in the kill feed, on
the scoreboards and in the lobbies, the server browser's games, the host's
notices in the console, map and mod names, and the text of a map's or mod's
own string lists, as well as the message boxes about mods and maps. It
changes only what this machine shows: the names others see, and what the
machines send each other, stay as they are.

The words come from Surge AI's rated list of English profanities, reviewed
for the game (`port/assets/text_filter`), in three groups, each with a level
that hides that severity and worse:

| Group | Setting | Default | What it has |
| --- | --- | --- | --- |
| SLURS | `streamer.slurs` | `"mild"` (all) | Slurs about race, ethnicity, nationality, religion, sexuality, gender and disability |
| SWEARING | `streamer.swearing` | `"strong"` | Swearing and sexual words; the mildest (damn, hell) show at `"strong"` |
| INSULTS | `streamer.insults` | `"off"` | Name-calling that is not a slur |

A word is found however it is dressed: in capitals or with accents, in
look-alike letters (Cyrillic, Greek, full-width, small capitals), in leet
(`0` for o, `@` for a, `ph` for f), with its letters stretched ("fuuuck"),
with punctuation between them (`f.u.c.k`), spaced out (`f u c k`, `fu ck`),
and with characters the game does not draw (zero-width spaces, the menus'
`|` codes) between them. Each word may be found inside a name, at a word's
start, or only as a whole word, as fits it, and some words are never
hidden though a hidden one is inside them (Scunthorpe). `python3
tools/text_filter_terms.py --check "some text"` shows what would be hidden.

What it cannot catch: letters added into a word or its letters scrambled, new
spellings, a word split between two names or two lines, voice chat (mute a
player from the scoreboard, or set `audio.voice_volume` to 0), and pictures.
What you type is shown as you type it.

Your own words: `text_filter.txt` in the data root, with lines of `add
<word>` (hidden whenever streamer mode is on) and `allow <word>` (never
hidden), read when the game starts and when the settings change.

## Map checks

The game reads a map's tags straight into memory and uses them as its own
structures: every pointer, count, index and enum in them is the map's, and
the game writes values into tags as it runs. So before anything reads a
map's tags, the port checks every tag against a schema of its group
(`game/tag_schema_*.c`, read by `game/tag_validate.c`), and each structure
BSP as it loads:

- Every block and every piece of data must lie in the tags (or the BSP) and
  overlap no other. Otherwise the game refuses the map.
- A block with more elements than the game has room for is cut to the
  maximum. A tag reference that is not a tag of the right group becomes
  none. So do an index past its block and an enum past its values (or they
  become 0, where the game cannot take none). A string gets its terminator.
  Values that the game sets as it runs are reset.
- Checks that the schema cannot express run last: the BSPs' and the models'
  graphs, vertex and index buffers, and indices into other tags.

Each correction goes to `debug.txt`. The game's own maps need none.
`build/map_validate/map_validate [--strict] map.map...` runs the same checks
on map files without the game, and `tools/test_linux_port.py` runs it on
the maps in `assets/maps`. `python tools/x86_game_abi.py` builds it: a test
tool compiled in the game's 32-bit x86 ABI, which needs clang and the C
library's 32-bit files (`gcc-multilib` on Debian and Ubuntu, `lib32-glibc`
on Arch Linux) on 64-bit x86 Linux. Nothing 32-bit ships.
`map_validate --fuzz <runs> map.map` changes a few words of the tags at
random in each run. The checks must not crash or hang, and a map that they
let through must need no more corrections.

A map's scripts can call only the script functions that a map needs (the
allowlist in `hs/hs.c`). They cannot call the functions for files, the
saved state of the game, the console, debugging or cheats. A script that
calls one does not run. The developer console can call every function.

Halo Custom Edition maps get the same checks (those that need OpenSauce are
refused). Their own loader (`game/cache_file_formats.c`) reads them into
their tag cache at 0x40440000 and converts what Custom Edition lays out
differently, then the validator checks their tags and each of their BSPs as
it checks this build's maps, before the game converts their models, BSP
geometry and scripts. Put them with `bitmaps.map`, `sounds.map` and
`loc.map` in `custom_maps`, beside `maps`, or set `paths.custom_edition` to
a Custom Edition install; the map lists show them as CUSTOM SINGLEPLAYER and
CUSTOM MULTIPLAYER, played as campaign levels (alone, or as network co-op)
or as multiplayer maps by their scenario type, and `game.custom_edition =
false` refuses them. `map_validate` checks them too, with the resource maps
beside each map or in `--maps <folder>`. See
`docs/custom_edition_caches.md`.

Defensive checks stay in the game code too. An index into a tag block, the
tags or a tag's data that is out of range gets zeros (`tag_empty_data` in
`tag_files/tag_groups.c`), not other memory.

A map's name must be its file's: the cache file slots are found by the name
in the map's header, so a map file whose header names another map (a
renamed one) is refused, not copied again for ever. The `loading.tga` a map
pack may put in the maps folder is read only if it is an uncompressed 24-bit
picture of 320 by 240, the loading screen's texture.

A checkpoint (`savegame.bin`, in the profile's folder) and a core are
images of the game state's memory: with the data arrays' pointers to their
elements, the objects' memory pool's blocks and the references to them, and
the caches' procedures. Before one is taken, each of those is checked
against what the game made at startup (`game_state_image_accept` in
`saved games/game_state.c`): an image that does not match (a damaged or
crafted file) is refused, and the level starts over.

## 64-bit ARM

`ninja linux_arm64` builds the game for 64-bit ARM Linux. It was tested on
a Steam Frame (SteamOS, Adreno 750 with Mesa). Enter it on a 64-bit ARM
computer: on such a computer, `ninja` without a target builds it.

| Result | Item |
| --- | --- |
| `build/linux_arm64/halo` | The game, an AArch64 executable |
| `build/linux_arm64/libSDL3.so.0` | SDL 3.4.16. The game finds it next to the executable (in `lib/openh1` in the release, `OpenH1-Linux-ARM64.zip`: "Packaging"). |
| `build/linux_arm64/SDL3-LICENSE.txt` | The license of SDL |

The game data, the settings, the saved games, the controls and system link
are the same as on 64-bit x86, and so is the renderer: Vulkan 1.3, in the
host. Linux arm64, Linux x86-64, Windows, macOS and Android machines can
play in the same game.

What the Linux builds (each a guest build) do not have yet, as the guest
reaches SDL only through the host's imports (`host/host_imports.list`),
which do not include what they need: the window's icon, and the names of
gamepads in the log.

### Requirements

As in "Requirements" above. The option `--linux-arm64-cc` of `configure.py`
selects a different clang for the host and the guest, which then needs the
`arm64_32` target, `ld.lld`, `llvm-ar` and `llvm-objdump`. `configure.py`
downloads the build's sources to `build/linux_arm64/third_party`. The
release builds of GitHub Actions are built on Ubuntu 22.04 (the
`linux-arm64` job), and use glibc 2.35 or later.

### How it operates

Recent 64-bit ARM processors (the Steam Frame's, for example) cannot
execute 32-bit ARM code, and few distributions have 32-bit ARM libraries
(SDL3, Mesa). Thus this build operates as the Android port does: the game is
ILP32 AArch64 code, a guest image in a 64-bit process. Refer to "How the
port operates" in [port/android/README.md](../android/README.md#how-the-port-operates).

- The guest image is the image of the Android build
  (`generate_guest_image` in `tools/android_build.py`, for the architecture
  `ARM64_32` of `tools/guest_arch.py`), with the desktop code paths of the
  platform layer. `tools/linux_guest_build.py` builds it and the host; each
  of its builds (`LINUX_GUESTS`) is for one architecture. The guest code has
  `HALO_GUEST` (and `HALO_ARM64_GUEST`, `HALO_GUEST_MACHO`) and
  `HALO_VULKAN`, not `HALO_ANDROID`.
- The host is the Android host (`port/android/host`) as a Linux executable.
  `host/host_main.c` replaces its `host_main.c` and `host_touch.c`; what
  both entry points do is in its `host_common.c`. The host's log is the
  standard error stream, as is the report of a crash (the registers and
  the frame chain). The executable contains
  the guest image (`host/guest_image.S`). The guest gets the variables of
  the environment that it reads (the `HALO_` settings, `HOME`, the XDG
  folders), so the `HALO_` settings operate.
- The desktop code paths use more of SDL than the Android app:
  `host/guest_desktop.c` and `host/host_desktop.c` supply it, and
  `host/host_imports.list` lists their imports. The guest does SDL's
  threads, mutexes, atomic values, files and `SDL_GlobDirectory` itself.
  The host does the windows, the displays, the 2D renderer, the dialogs
  and the downloads of the self-updater (`src/posix_update.c`).
- The renderer is Vulkan's, in the host ("The renderer's command stream",
  below, and [the Vulkan README](vulkan/README.md)).
- The host keeps the GPU driver from the kernel's `trace_marker`
  (`src/posix_trace_marker.c`, under "The platform layer" below): on the
  Steam Frame, its markers took the game from the headset's 72 Hz to about
  50 frames a second.
- The guest's memory is the low 4 GB of the process. With
  `configure.py --guest-base`, it is at a base that the host chooses: each
  guest address is an offset from a base register, which every memory
  access of the guest adds (`tools/guest_base_register.py`;
  `android/host/host_memory_base.c`). Then the guest's 4 KB pages also
  operate on a kernel with 16 KB pages.

### The renderer's command stream

The renderer's backend runs on the 64-bit host, not in the guest. The
game's renderer is in two halves (`src/xgpu_backend.h`): a front end that
knows the Xbox's Direct3D, and a backend that drives the GPU. The front
end's calls go to a command stream instead of a backend in the guest
(`src/xgpu_stream.h`). The guest writes each call
as a record into a ring in its own memory (`src/xgpu_stream_encode.c`), and
one host call each frame hands the frame over. A thread of the host plays
the records into a backend of the host: Vulkan on Linux and Android, Metal
on macOS (`android/host/host_render_stream.c`, whose `host_render_backend`
is the table of a backend's functions). It does so while the game goes on
to the next frame, so the GPU driver's work leaves the game's thread, and
no graphics call crosses from the guest to the host.

- The records hold only fixed-width integers and floats, alike in the
  ILP32 guest and the 64-bit host. Guest data in them is a guest address,
  which the host reads in place.
- Data that the game rewrites before the GPU reads it is copied into the
  ring: streamed vertices, indices, immediate mode, constants, uniforms,
  the mirror's writes, the dynamic lights. Data too large for the ring (an
  eighth of it) is copied into a buffer of its own, which is freed when
  the host is done with it. Textures and palettes are read where they are,
  as on the Xbox, whose GPU reads the game's memory a frame behind.
- Pipeline keys and vertex layouts are sent once, then named by a
  number. Draw states, textures and vertex inputs are sent when they
  change. A busy frame is a few MB, most of it streamed vertices.
- The game waits only when the ring (32 MB) is full, or when two frames
  are not done yet: the GPU draws one frame while the host's thread plays
  the next, and the game writes the third.
- The calls whose answer the front end needs now are answered without a
  wait. The capabilities and the VR mode's sizes are answered once, at
  start-up. A pipeline that cannot be made is reported a frame later. The
  visibility tests' counts are written into the guest's memory by the
  host. A screenshot waits for the frame's end; the Screenshot key's is
  written by the host (`XGPU_STREAM_SCREENSHOT`).
- In the VR mode, the host submits the frame to OpenXR as it plays the
  frame's last record: the moment to take the newest head pose.

- Textures are the host's to read and cache (`vulkan/xgpu_vulkan_textures.c`;
  the decoding, `src/xbox_texture_decode.c`, is every backend's). The
  high-res replacements (the HUD's, the menus' art, the high-res text) are
  images: their pixels in the game's memory (`src/xgpu_images.c`), which a
  texture stage names in the Xbox texture's place.

Every build uses it: the Linux builds, Windows and Android with the Vulkan
backend (`vulkan/`, its [README](vulkan/README.md)), and macOS and iOS with
the Metal backend (port/macos/README.md, "Metal"). On Linux:

- The host builds the Vulkan backend (`tools/vulkan_build.py`), with the
  texture decoding.
- The guest makes the window (`src/sdl_platform.c`) and hands it to the
  host (`host_vulkan_window_open`; `vulkan/guest_vulkan.c`,
  `vulkan/host_vulkan.c`): the host makes its Vulkan surface and swapchain,
  and its render thread draws and presents the frames, with the present
  mode that `display.vsync` chooses ("The window" in
  [the Vulkan README](vulkan/README.md)).
- The VR mode (`--vr`): OpenXR's calls that use the Vulkan device (the
  session made, the swapchains' images acquired and released, the frame
  begun and ended) are made on the render thread (`host/host_vr.c`,
  `host/host_vr_stream.c`, `vulkan/host_vulkan_vr.c`); the game's thread
  waits for the runtime's frames and locates the head and hands. What the
  frame's composition reads of `src/vr.c` and `src/vr_visor.c` (the scope,
  a cockpit's display, the vignette, the visor, the rim) comes as a record
  before it, as do the gaze for the foveation, the poses the eyes are
  drawn from (the late latch: "VR", "How it operates") and the layers the
  game submits; what `vr.c` sets as the frame is drawn (the eyes' transforms, the
  sky, the feed, the tint) are records in their order. The host ends each
  frame as it plays its last record. The screenshots of the eyes, the HUD
  and the scope are the host's.
- At exit the guest has the host play the frames submitted (the last one
  shown, the VR mode's ended).
- The visibility tests' counts are what the GPU has written by the end of
  the frame the host plays. The game's next frame reads them while the
  host plays its frames, so they can be a frame older or newer than those
  of a backend in the game, whose own depend on the frame's pacing.

`tools/test_render_stream.py` tests the stream with a backend of its
own.

## 64-bit x86

`ninja linux_x86_64` builds the game as a 64-bit x86 Linux executable. It
is the default build of a 64-bit x86 computer. It runs the game as the
64-bit ARM build does, as a guest image of 32-bit-pointer code in a 64-bit
process: here x32 code, the x86-64 instruction set with 32-bit pointers. It
needs no 32-bit libraries or drivers.

| Result | Item |
| --- | --- |
| `build/linux_x86_64/halo` | The game, an x86-64 executable |
| `build/linux_x86_64/libSDL3.so.0` | SDL 3.4.16. The game finds it next to the executable (in `lib/openh1` in the release, `OpenH1-Linux.zip`: "Packaging"). |
| `build/linux_x86_64/SDL3-LICENSE.txt` | The license of SDL |

The game data, the settings (`config.toml` in the game's folder), the
saved games, the controls and system link are the same as in the 64-bit
ARM build, and so is the renderer: Vulkan 1.3, in the host. The game is
computed with the same code and options as in the other builds (SSE
arithmetic, no fused multiply-add, the vendored musl maths); its
`long double` is a `double`, as MSVC's is.

### Requirements

As in "Requirements" above. The guest is compiled for x32
(`x86_64-linux-gnux32`). The option `--linux-x86-64-cc` of `configure.py`
selects a different clang for the host and the guest, which then needs the
x32 target, `ld.lld`, `llvm-ar` and `llvm-objdump`. The kernel's own x32
support is not needed: the guest never calls the kernel itself.
`configure.py` downloads the build's sources to
`build/linux_x86_64/third_party`. The release builds of GitHub Actions are
built on Ubuntu 22.04 (the `linux-x86_64` job), and use glibc 2.35 or
later.

### How it operates

As "How it operates" under "64-bit ARM", with these differences:

- The guest is compiled by clang for x32 (`X32` in `tools/guest_arch.py`),
  which writes ELF objects itself: no assembly is converted. Its C library
  is musl with `guest/libc/arch/x32` of `port/android`: musl's x86 bits,
  with the guest's own types and system calls, as for `arm64_32`.
- The guest image is linked at `0x70000000`, below 2 GB, not above the
  Xbox memory (`0x80000000` to `0xA0000000`) as on AArch64: x32's static code addresses
  its own symbols with 32-bit values that are sign-extended, which cannot
  reach 2 GB or more. The Xbox memory above it is reached through
  registers, and its addresses compute with 32-bit wraparound, as on the
  Xbox.
- The Xbox memory is 512 MB, as on every build, with the desktop builds'
  256 MB texture cache, all of it for every map, and their 16 MB sound
  cache, which Custom Edition maps need (`halo_port_capacity.h`). The
  64-bit ARM build and Android give an Xbox map 22 MB of the texture cache
  and keep the Xbox's 4 MB sound cache.
- The guest calls the host's functions through import stubs that jump
  through a table of 64-bit addresses. The host's functions read pointer
  arguments as 64-bit registers, and x32 code passes them zero-extended,
  which `tools/test_host_units.py` tests (`x32_boundary_test.c`). The host
  passes the guest its own arguments zero-extended in turn
  (`host_call_guest`). An argument after the sixth is on the stack, in an
  8-byte slot, where clang's x32 code also writes a pointer zero-extended;
  as only the registers are tested, such an argument is kept a 32-bit or
  64-bit integer, never a pointer (`tools/check_host_imports.py`).
- The thread pointer (`%fs`) belongs to the host's glibc. The guest's
  thread-local variables are emulated, and the build stops if the image
  uses `%fs` or `%gs`, or has thread-local storage
  (`tools/guest_image_check.py`).
- The image needs no runtime library (`libgcc` or compiler-rt): x86-64 has
  every operation that its integers and floats need, and the link stops if
  that changes.
- The host is a position-independent executable, which leaves the low 4 GB
  of the address space to the guest.

## Vulkan

Both Linux builds draw with the Vulkan renderer (`vulkan/`), in the host,
as Android does. A GPU and driver with Vulkan 1.3 are needed; the rest of
what the driver must have, and the renderer's own variables, are in
[the Vulkan README](vulkan/README.md). With `--vr` the build has the VR
mode, through OpenXR's `XR_KHR_vulkan_enable2` ("VR").

It draws everything the renderer has: the enhanced dynamic lights, FXAA and
SMAA, and the clears of some of a target's colour channels (a fog screen's,
and a split screen's windows' ends, clear alpha alone). It takes the
graphics' debugging settings: `debug.gpu_stats`, `debug.gpu_dump_shaders`
(SPIR-V), `debug.gpu_validation` (the validation layers; `debug.gl_debug`
is its old name) and `debug.texture_dump_directory`.
`debug.gpu_debug_expression` is Metal's alone, and does nothing with Vulkan.

## VR

`python3 configure.py --vr`, then `ninja` (`ninja linux_x86_64` or
`ninja linux_arm64`), builds the computer's Linux build with a VR mode: the
game plays in an OpenXR headset, in stereo, with the head tracked in 6
degrees of freedom. It was made on a Steam Frame with SteamVR. The VR build
draws with Vulkan, as every Linux build does, and gives the runtime its
Vulkan device through `XR_KHR_vulkan_enable2`, the only binding it has
([the Vulkan README](vulkan/README.md)). Without `--vr` the build has no
VR mode. It needs no OpenXR package, as it
loads the active runtime itself (or a `libopenxr_loader.so.1` where there is
one). Configuring it downloads the OpenXR SDK's four headers (to
`build/<build>/third_party/openxr`, checked against their SHA-256 in
`tools/linux_guest_build.py`).

To play, start SteamVR, then start `halo` (`bin/openh1` in a release; from a terminal of the headset's
desktop session, or as a non-Steam game in Steam). Without a runtime or a
headset the game plays flat, in its window. `vr.enabled = false` (or
`HALO_VR=0`) plays flat too.

On a PC, the releases have the 64-bit x86 VR build,
`OpenH1-Linux-VR.zip`, beside the one without VR
(`tools/ci_build.py linux_x86_64 release --vr`): unpack it to a folder of
its own, with the maps ("Start the game"), and start `bin/openh1`. It
updates itself to the release's VR build, never to the other. The build
without VR has no VR mode, and the VR build starts the
runtime (SteamVR's server) whenever it starts, so it is a download of its
own. For the Steam Frame, the releases have the 64-bit ARM VR build as the
Frame's package, `OpenH1-Steam-Frame.zip`
(`tools/ci_build.py linux_arm64 release --vr`, which runs
`tools/frame_package.py --zip`). On the Frame the VR build updates itself
to the release's package, through its launcher ("On the Steam Frame" in
"Updates"); or install a new one with its `install.sh`, or
`tools/frame_install.sh` (below).

### Steam Frame

On the Frame, the game is in `~/apps/halo-vr/`, and Steam starts it
through a shortcut (a non-Steam game) to the launcher there,
`halo-vr.sh` (`port/linux/frame/halo-vr.sh`, whose comments list the
environment it gives the game). On the Frame itself, in desktop mode:
download the latest release's package, unzip it and open `install.sh` in
it (port/linux/frame/README.md): it installs the game, asks for the maps,
and adds it to Steam with its library artwork. From a computer with SSH to the Frame,
the latest release's package
([OpenH1-Steam-Frame.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Steam-Frame.zip)):

```sh
tools/frame_install.sh --package OpenH1-Steam-Frame.zip <frame>
```

or one built here:

```sh
python3 configure.py --vr && ninja linux_arm64
python3 tools/frame_package.py
tools/frame_install.sh <frame>
```

- `tools/frame_package.py` makes the package, `dist/halo-vr-frame/` and
  `dist/halo-vr-frame.tar.gz`: the game, `libSDL3.so.0`, `halo-vr.sh`,
  `install.sh` (`tools/linux_install.sh`, the Linux release's installer:
  "Steam Deck"), `brokers.txt` (without which internet play finds no
  games), the menu entry (`openh1.desktop`) and its icon (`openh1.png`),
  Steam's library artwork for OpenH1 VR (`steam-<kind>.png`), a README
  (`port/linux/frame/README.md`: install, update, uninstall, the files),
  the libraries' licenses, `VERSION` (the commit) and `SHA256SUMS`. It
  takes the game from `build/linux_arm64` (`--build` names another), and
  stops if the game is not the VR build, or needs a library that
  neither the package nor SteamOS has. `--zip <file>` also writes it as a
  zip (the `halo-vr-frame` folder, with the files' modes), as the release's
  `OpenH1-Steam-Frame.zip`.
- The package's `install.sh`, opened on the Frame, installs the package
  it is in into `~/apps/halo-vr/`, as the Linux release's does its tree
  ("Steam Deck": one script, `tools/linux_install.sh`, with a small part
  of its own for each package). It refuses while the game runs
  (`pgrep -x halo`), checks the package against its `SHA256SUMS`, and
  never starts the game. Each file that differs goes in as `<name>.new`,
  checked, and once all are, each takes its name (a rename, so a stopped
  install leaves the old game whole). The replaced `halo`,
  `libSDL3.so.0` and `halo-vr.sh` are kept as `<name>.prev` (the last
  only): `mv halo.prev halo` goes back. Files an earlier package had and
  this one has not are removed; `brokers.txt` is replaced. It prints what
  changed (and the commits, from `VERSION`). It never touches
  `config.toml`, `halo-vr.env`, the maps, the logs or the saved games.
- `tools/frame_install.sh <host>` (`--package` for another package, a
  folder, the `.tar.gz` or a release's `.zip`) does the same from a
  computer, over SSH: it copies the package and the repository's
  `tools/linux_install.sh` to a folder of the Frame's `/tmp`, runs it
  there with `--batch` (no questions, and no `steamos-add-to-steam`,
  which needs the Frame's own session), then deletes the folder.
  `--local <folder>` installs into `<folder>/apps/halo-vr` instead, the
  folder standing in for the Frame's home (to test the script).
- The Steam shortcut: the install writes the menu entry
  `~/.local/share/applications/openh1-vr.desktop` (the package's
  `openh1.desktop`, named OpenH1 VR, with its `openh1.png`), and, opened
  on the Frame, asks the running Steam to add it (SteamOS's
  `steamos-add-to-steam`) when Steam has no shortcut to the launcher (in
  `~/.local/share/Steam/userdata/*/config/shortcuts.vdf`, which the
  install only reads: Steam rewrites it while it runs, which on the Frame
  is always). Otherwise it prints how to add it on the Frame: in Steam's
  desktop mode, Games > Add a Non-Steam Game to My Library, OpenH1 VR,
  Add Selected Programs.
- The shortcut's library artwork: the package's `steam-<kind>.png`, put
  in Steam's grid folder under the shortcut's app ID, as on a Steam Deck
  ("Steam Deck"), once Steam has the shortcut. A running Steam may not
  show it until it restarts.
- The maps go in `~/apps/halo-vr/maps` (or `paths.data`); the install
  says so when there are none. Opened on the Frame, it first asks for the
  disc's maps folder (`kdialog`) and copies its `.map` files. Or put the
  disc image in the Frame's Downloads: the game's first start, in the
  headset, finds it and copies its maps ("Game data").
- Settings of your own for the launcher (`HALO_*=value` lines) go in
  `~/apps/halo-vr/halo-vr.env`, which updates keep, or in the shortcut's
  launch options in Steam (`HALO_VR=0 %command%`).
- Started from Steam, the game's output (and a crash's backtrace) is in
  `~/apps/halo-vr/halo-vr.log`, the previous run's in `halo-vr.log.prev`;
  its log is `debug.txt` beside the maps.

### SteamVR on a desktop PC

Before its window opens, the game asks the runtime whether it offers
`XR_KHR_vulkan_enable2` (`host_vr_probe`): the runtime then chooses the
Vulkan device, which the host's renderer takes. A runtime without it gets
no session: the game says so in its log (`vr: the OpenXR runtime has no
Vulkan binding`) and plays flat. The window stays hidden while the headset
shows the game; if the session cannot be made after all (no headset, the
runtime's compositor failing), the game says so in its log, shows the
window and plays flat in it.

`tools/vr_test/openxr_probe.c` loads a runtime as the game does and lists
its extensions and whether it has a headset; its comment says how to build
it. `openxr_probe --extensions-only <manifest>` does not start the runtime;
without it, the probe goes on to `xrCreateInstance` and `xrGetSystem`
(which start SteamVR's server).

What it found with SteamVR 2.18.3 on x86-64 Linux (CachyOS, KDE Plasma on
Wayland, an RTX 4090 with NVIDIA 615.78, `steamxr_linux64.json`, October
2026), with no headset connected, when the VR build still drew with OpenGL
(through EGL's binding, `XR_MNDX_egl_enable`), before it drew with Vulkan:

- The runtime offers 41 extensions, `XR_KHR_opengl_enable` (12) and
  `XR_MNDX_egl_enable` (2) among them. It also offers `XR_FB_display_refresh_rate`,
  `XR_KHR_composition_layer_depth`, `XR_EXT_eye_gaze_interaction`,
  `XR_VALVE_frame_controller_interaction` and `XR_MND_headless`, but not
  `XR_KHR_composition_layer_cylinder`: the visor's HUD is a quad, curved in
  its image (`vr_visor.c`). Listing them needs neither SteamVR running nor a
  headset.
- Without a headset `xrCreateInstance` fails (`XR_ERROR_RUNTIME_FAILURE`),
  not `xrGetSystem`: the runtime starts `vrserver`, which refuses the
  connection (`VRInitError_Driver_WirelessHmdNotConnected` in
  `vrserver.txt`, the Frame's `vrlink` driver having no headset).
- SteamVR's null driver gives a headset to test with. It is a setting
  (`steamvr.vrsettings`: `"driver_null": { "enable": true }` and
  `"steamvr": { "forcedDriver": "null" }`), which a configuration folder of
  its own can hold without changing the user's: an `openvrpaths.vrpath`
  in `$XDG_CONFIG_HOME/openvr` naming that folder as `config` (and a `log`
  folder), with `XDG_CONFIG_HOME` set for the game alone (and `HOME` with
  links to `~/.steam`'s `root`, `sdk32`, `sdk64`, `bin32` and `bin64`, which
  SteamVR's launchers look for there: SteamVR still runs from the user's
  Steam folder, and may write its logs and caches there). With it, the
  game's instance is `SteamVR/OpenXR 2.18.3`, its system
  `SteamVR/OpenXR : null`, and its OpenGL context of then (OpenGL 4.5,
  EGL's, under XWayland) met the runtime's requirements (OpenGL 4.3 to
  4.6).
- With the null driver the session does not open: `xrCreateSession` fails
  (`XR_ERROR_RUNTIME_FAILURE`) and the game plays flat. SteamVR's compositor
  on Linux shows a headset only in direct mode, on a display of its own,
  even the null driver's: it looks for one through RandR and Vulkan's
  display extensions (`VK_EXT_direct_mode_display`,
  `VK_EXT_acquire_xlib_display`), or, under Wayland, leases one from the
  desktop (`wp_drm_lease_device_v1`), and with none, stops (`CHmdWindowSDL:
  VR requires direct mode`, `VRInitError_Compositor_CannotDRMLeaseDisplay`
  in `vrcompositor.txt`). It opens no window on the desktop for the null
  headset, and a virtual display (`IVRVirtualDisplay`) does not change that.
- A driver whose headset takes the frames itself, in driver direct mode
  (`IVRDriverDirectModeComponent`, as streamed headsets' drivers do), needs
  no display: `tools/vr_test/steamvr_driver.sh` (`tools/vr_test/README.md`)
  runs SteamVR with such a driver of the tests', with settings and logs in
  a configuration folder of its own (the user's SteamVR settings untouched;
  Steam's own folder may get logs and caches), and stops what it started
  however it ends. With it the game's session opens and reaches FOCUSED. SteamVR
  asks the driver for 2268x2520 swap images (its 150% of the driver's
  1512x1680) and recommends the game 1852x2056 eyes. Each frame comes to the
  driver as two layers: the game's eyes as it drew them, and SteamVR's
  system layer, where SteamVR draws the game's quad layers (the visor's HUD
  panels, which SteamVR's OpenXR runtime makes overlays) over a clear
  image. A headset that holds perfectly still goes into standby after some
  seconds, and the game's session to VISIBLE and SYNCHRONIZED, its frames no
  longer shown: the driver sways it by a tenth of a millimetre.

What was measured on x86-64, an RTX 4090, the head turning, fixed
foveation, a release build, 90 Hz (11.1 ms a frame), with the OpenGL
renderer the VR build had then (not yet measured again with Vulkan):

- With SteamVR and the test driver, at 1852x2056 eyes. The game's CPU time
  includes `xrEndFrame`'s 2.5 ms; SteamVR's "ApplicationTime" is its own
  measure of the game's frame (`vrcompositor.txt`); its compositor, in
  driver direct mode, takes 0.1 ms of CPU and measures no GPU time of its
  own (`GetFrameTimings`); "Total GPU" is the GPU's time for all of a frame.

  | Level | The game's time a frame (CPU) | SteamVR's ApplicationTime (CPU / GPU) | Total GPU | Late frames |
  | --- | --- | --- | --- | --- |
  | a10 | 4.5 ms | 2.4 / 6.0 ms | 5.6 to 7.1 ms | 0 of 300, none dropped or reprojected |
  | b30 | 6.6 to 6.8 ms | 4.6 / 8.1 ms | 8.4 to 9.5 ms | 0 of 300, none dropped or reprojected |

The game kept 90 Hz on an RTX 4090 with room to spare. A headset's own
latency and its display are not in these.

### Controls

The Steam Frame's controllers are the halves of an Xbox controller:

| Controller | Game |
| --- | --- |
| Left stick | Move (click: crouch) |
| Head | Crouch: crouch in the room, and the game crouches (`vr.physical_crouch`) |
| Right stick, left and right | Turn: in steps (`vr.turn = "snap"`) or smoothly (`"smooth"`) |
| Right stick click | Zoom: a window about the reticle (`vr.scope_view = "smartlink"`), or a raised scope's magnification (`"eye"`): below |
| Head | Look |
| Right controller | Aim: it holds the weapon, and the reticle shows where it points (`vr.aim = "controller"`). With `vr.aim = "head"`, the head aims and the reticle stays in the middle of the view. |
| Left controller | The left hand, where the controller is; the left grip closes it (`vr.hands`). The right index finger lies off the right trigger until your finger touches it, then rests on it and pulls with it ("The trigger finger", below). |
| Right trigger, left trigger | Fire, throw a grenade (the left trigger only with `vr.grenade_throw = "both"` or `"button"`: by default the hand throws, below) |
| A, B, X, Y | Jump, melee, action and reload, switch weapon |
| Left grip, the left hand on a long gun's foregrip | Aim it with both hands, along the line from the right hand to the left (`vr.two_handed`) |
| A swing of the right controller: hard, or softer with the right grip held | Melee (`vr.melee_gesture`, below) |
| The right hand over the right shoulder or at the right hip, gripping | Draw the weapon carried there (`vr.holsters`, below) |
| The sniper rifle's or the pistol's scope raised to an eye | Look through it, magnified (`vr.scope_view = "eye"`, below) |
| The left hand gripping at the left hip and pushing what it took into the weapon (the right stick pushed down ejects the magazine first, if you like) | Reload by hand: take another magazine, the one in the weapon ejecting itself, and push it in (`vr.manual_reload`, below) |
| The left grip held, the left hand drawn back and swung, the grip let go | Throw a grenade where the hand threw it, as hard as it did (`vr.grenade_throw`, below) |
| Beside an overturned vehicle, both hands low gripping it, then lifted together | Flip it (`vr.vehicle_flip`, below; X reloads there) |
| The right hand reaching down to a weapon on the ground, gripping | Pick it up, or swap the one in hand for it (`vr.grab_world`, below; X held as ever) |
| Either hand pushed into a control (a door's panel, a bridge's, a lift's) | Press it, as X (`vr.grab_world`, below; X as ever) |
| Left bumper, right bumper | Flashlight, switch grenade |
| D-pad (left controller), View, Menu | The menus' d-pad, Back (scoreboard), Start (pause) |
| Both grips, held | Recentre: the head's place and heading become the player's eye and facing (not while the left hand holds a foregrip, nor while the hands hold a vehicle to lift it, nor on a Ghost's handlebars) |
| Both grips on a Ghost's handles, turned as a bike's | Steer it, its guns aiming where the bars point (`vr.ghost_steering`, below) |
| Both grips on a Warthog's wheel, turned as a car's | Steer it (`vr.warthog_steering`, below) |
| Driving, the hands off the handlebars or the wheel: either stick pushed sideways | Steer (the Ghost, the Warthog); in the Scorpion, the left stick turns you and the aim, which it drives toward. The left stick's forward and back throttle as ever |
| A gamepad's Back (View), held 1 s | Recentre, as both grips do |

Other controllers that SteamVR maps to the Touch layout operate as well
(the grips as the bumpers).

#### Left-handed (`vr.dominant_hand`, `vr.swap_sticks`)

`vr.dominant_hand = "left"` (VR Display's DOMINANT HAND: LEFT) puts the
weapon in your left hand, and all that the right hand does above the left
does, and the other way round: the left trigger fires, its touch lays the
index finger on it, the left controller aims (the reticle, the shot, the
smart-link's view, a scope raised to your left eye) and swings to melee, and
the left grip draws from the holsters, which are on your left (a long gun
over your left shoulder, a pistol at your left hip). The right hand holds a
long gun's foregrip, takes magazines from your right hip (the rocket
launcher's tubes from behind your right shoulder) and pushes them in, and
throws the grenades (the right trigger with `vr.grenade_throw = "both"`). The
haptics are swapped with the hands, the game's rumble too (the weapon's in
the left controller).

- The first-person weapon and both hands are drawn mirrored, a left hand
  holding a left-handed weapon (the game's models are right-handed): the
  weapon in the left hand, the right hand on its foregrip, the arms from
  the shoulders on their own sides, the magazine and its well, the trigger
  and the finger on it mirrored with it. The assault rifle's counter reads
  as it does (its digits and its compass the right way round); the other
  marks on its display are mirrored.
- The sticks swap with the hand (`vr.swap_sticks = "auto"`, VR Display's
  SWAP STICKS): the right stick moves (its click crouches) and the left
  turns, its click zooms and pushed down it ejects the magazine.
  `vr.swap_sticks = "no"` keeps the left stick moving, `"yes"` swaps them
  for a right hand too.
- The buttons stay where they are, as the game's prompts name them: A and
  B on the right, X and Y, the d-pad and View on the left, the bumpers.
- A vehicle's handlebars and wheel are held by both hands as ever, each
  hand on its own side's handle; a turret fires with the left trigger and
  aims where the left controller points.
- Not with `vr.aim = "gamepad"`, where the controllers are a gamepad's
  halves (the gamepad's own STICK LAYOUT swaps its sticks).
- SteamVR's dominant hand for the game (its per-application setting) is not
  read: OpenXR, which this build uses, does not expose it (SteamVR gives it
  to OpenVR applications alone), so the setting says.

#### The trigger finger

The Steam Frame's triggers sense a finger on them (capacitive touch), as
the Touch's do, besides how far they are pulled (analog, 0 to 1). The right
index finger on the weapon follows yours: off the trigger it lies straight
along the right side of the weapon, out of the guard, as you keep a finger
off a trigger; touch the trigger and it comes to rest on it (in about
0.08 s), and it pulls as far as you pull, the weapon's trigger going back
with it (`vr_hands.c`, each weapon's own curls). A trigger pulled at all
holds the finger on it, whatever the touch says. A controller whose
trigger senses no touch (the runtime binds none) leaves the finger resting
on the trigger, as before; the log says which (`vr: the right trigger
senses ...`).

The Frame's controllers are bound through Valve's interaction profile,
`/interaction_profiles/valve/frame_controller_valve` as Valve documents it
(and `/interaction_profiles/valve/frame_controller`, as this build named it
first, for a runtime that has that one), with `trigger/touch` on each
hand; other controllers through the Touch's, which has it too. The log
names the profile the runtime chose for each hand (`vr: right hand: ...`).

#### Meleeing with a swing (`vr.melee_gesture`)

Whack with the weapon in your right hand, as you would with a real one: a
hard swing in any direction (sideways, down, forward, the butt or the body
of the gun leading) melees, as B does (B still melees too). With the right
grip held, a softer swing will do. A melee that lands on anything (a body,
a wall, the ground) knocks the right controller.

- Hard: the hand faster than 2.6 m/s; with the right grip squeezed, faster
  than 1.8 m/s. Either for at least 10 cm, 40 ms and three frames of the
  swing (a twitch, or the tracking jumping when it finds the controller
  again, is none).
- One melee a swing: the hand must slow down (below 1 m/s) before the next
  swing melees, and no sooner than 0.6 s after the last.
- What is not a swing: the hand carried by your body or your head (a quick
  turn to a new target, a step, crouching: its speed from your head, as it
  moves and turns too, is less than half its own), aiming (a sweep to a new
  target is slower than a whack), and, the grip open as it begins, a hand
  going back toward your body (drawing back, reaching to your hip or over
  your shoulder for a holster, to grip there).
- A swing past a holster or through one is a swing, whatever the right
  grip does. A holster claims your hand only when you squeeze the grip in
  it, the hand all but still there (slower than 2 m/s, and not in a swing
  that has melee'd): that is a draw (or a grip on an empty holster), and no
  swing melees while that grip is held, nor half a second after a draw,
  nor the swing that brings the drawn weapon out, however long your hand
  waits there. A grip squeezed by a swing passing through draws nothing,
  nor does a grip held since before (a draw is a squeeze at the holster).
  Held through a fast reach to a holster, the grip makes that reach a
  swing: reach with it open, as a draw is.
- Nor while your left hand throws a grenade (its swing and its release) or
  holds a magazine, or for 0.4 s after (the throw's swing, the reload's
  push). Holding a grenade before the throw does not keep the right hand
  from meleeing.
- The right grip's other use: with the left grip, it recentres (held both
  for 0.75 s; a whack is far shorter). It closes no hand: the right hand
  holds the weapon.
- Only on foot, in play (not in a vehicle, the menus or a cinematic), with
  the controllers aiming (not `vr.aim = "gamepad"`). The swing presses the
  game's B, so it melees as B does in a network game too (the host is told
  of a melee as of a press of B).

`debug.vr_melee_log = true` logs the swings that did not melee too: how
fast and how far each went, and why (too slow, a holster, a throw...); the
thresholds are named constants at the top of `src/vr.c` (`MELEE_*`).

#### Aiming with the controller (`vr.aim = "controller"`)

The reticle is where the right controller's line meets the world (as far
as what it is on, in both eyes), and the shot goes there: the game fires
from its own eye (the Master Chief's, crouched or not, which is not where
your hand is), so it is aimed from its eye at the point the reticle is on.
The reticle turns red, and the bullets' own autoaim helps, as the game
judges them from its eye along that aim. At any range, held low and to
the side, after a step in the room away from the game's eye or in a
vehicle's seat, what the reticle is on is what you hit.

- A reticle nearer than 1.5 m (a wall at arm's length) aims at the point
  1.5 m along your line, as the game's aim would swing about wildly
  otherwise as the line crossed edges near and far: the shot meets so near
  a wall within a few centimetres of the reticle anyway.
- As the line moves from something near to something far, the reticle
  moves to it along the line over a few hundredths of a second (its
  distance smoothed, so that it does not jump), and the shot goes where
  the line is on at once.
- Only the aim's direction changes: the netcode sends it as ever.
- Aim assist (`vr.aim_assist`): near an enemy the game's own aim assist
  would be drawn to (as it is for a gamepad's stick: within the weapon's
  magnetism angle and distance), the reticle is slow to leave it: as your
  hand moves the aim off it, the aim moves less than your hand (half as much
  at the enemy's middle), never more, and never against your hand, nor on
  its own with your hand still (but for the zoomed pull below). Following a moving enemy is not slowed.
  Zoomed, the aim is also drawn gently toward it (1 degree a second at
  most). "It" is the part of the enemy you can see from your weapon,
  nearest where you aim, from its hips to its head: aimed at a head, the
  aim stays on the head. An enemy in a vehicle or on a turret (a Shade's
  gunner, a Ghost's driver) is the target, never the vehicle, and the
  vehicle's body is no part of it: over a Shade's gun you are held on the
  gunner's head, not drawn down onto the turret; a Banshee's pilot, out of
  sight in its cockpit, is no target, nor is an empty vehicle. The reticle may lag your weapon so by 3 degrees at
  most, and away from an enemy catches up with it smoothly (no faster than
  6 degrees a second, more only as your hand moves). The reticle, the
  smart-link's view and the shot follow the assisted aim; the bullets' own
  autoaim is the game's, whatever this is. VR Controls' AIM ASSIST: OFF,
  WEAK, ON or STRONG.

#### Throwing a grenade with the left hand (`vr.grenade_throw = "gesture"`)

Throw it as you would a ball: squeeze the left grip to take a grenade (a
short buzz says you hold one; only with a grenade to throw, on foot;
anywhere but at the magazines' pouch on your left hip, below), draw
the hand back (beside your head, or low for a lob), swing it forward and
open the hand. The grenade leaves from your hand as you let go, along the
way the hand was going (lifted 8 degrees), as hard as you threw it: a
throw of 4 m/s or faster throws as far as the game's throw does, slower
ones shorter (to 30% of it), never farther. Only the hand throws: on foot
the left trigger throws nothing (`vr.grenade_throw = "both"` lets it throw
as well, `"button"` alone; in a vehicle it is the vehicle's, as ever). The
right bumper switches the grenade. From the moment you
take it until it flies, the grenade (frag or plasma, the one selected) is
in your left hand, whatever `vr.hands` is, and it leaves from just where
you held it. It is drawn at 0.6 of the game's size (the game's frag is about
20 cm, larger than the hand, which it hid), the fingers closing about it
rather than through it, and the plasma grenade lit by its own blue light,
as a live one is.

- What counts as a swing: the hand moving forward (the way your head
  faces) faster than 1.6 m/s, with the grip held for 0.1 s at least, from
  no farther than 15 cm ahead of your eyes since you took it. A hand held
  out (on a foregrip, reaching) and pushed forward is no throw; nor is the
  grip let go of before a swing.
- The swing starts the game's throw (as the left trigger would), and the
  game's grenade is then yours: a swing held without letting go throws it
  anyway after 0.75 s.
- The left grip on a long gun's foregrip takes the foregrip instead
  (`vr.two_handed`), and both grips held recentre: neither takes a grenade.
  Nor does the left grip at your left hip, or within 4 cm of it,
  whatever the weapon (with the rocket launcher, nor behind your left
  shoulder): that is where magazines are taken (`vr.manual_reload`,
  below), and a grip there never brings a grenade, even with nothing to
  reload (two weak ticks say so). Grenades have no place of their own on
  the body: the grip takes one wherever else the hand is, as the throw
  starts from wherever the hand is drawn back to.
- Your left hand and arm are the throw in first person: the weapon stays
  in your right hand (with `vr.hands = "game"` the game's arms throw, timed
  to yours). The player's body throws as you do: its throw waits before
  letting go while you hold on, and lets go when you do (below, "How it
  operates", for what the other players of a network game see).

`debug.vr_throw_log = true` logs each step of a throw (the grip, the
swing or why it was not one, the release's speed and direction, the
game's throw timed to it); the thresholds are named constants at the top
of `src/vr.c`.

#### Holsters (`vr.holsters`)

You carry two weapons and hold one; the other is on your body, where you
can reach for it: a long gun over your right shoulder, a pistol or the
needler at your right hip. Reach there with your right hand and squeeze
the right grip to draw it: the game switches to it (as Y does, through the
game's switch, which others in the game see as ever), and the one you held
takes its place in its own holster. The weapon in its holster lights up as
your hand comes near it, and the right controller ticks as your hand gets
there, so you can find it without looking: your shield's energy wraps it,
in pale blue, rising from about 35 cm away and full once your hand is
there, and its metal takes on the blue. The shoulder's holster, out of
sight, ticks too as your hand nears it, from about 30 cm, the ticks
quicker and stronger the nearer, until the tick of getting there (the
hip's does not: your hand passes by it and rests by it).

- The holsters follow your body, which faces where your head does, but
  only once your head turns more than 40 degrees from it, and then slowly:
  looking about does not move them, nor does looking down (they hang from
  your neck, as your body does, not from your eyes, which a nod moves
  forward). Recentring (both grips) sets them again. Your body drawn
  (below) and your arms' shoulders face the same way.
- The shoulder's holster is behind and just above your right shoulder,
  the hip's at your right hip, about 70 cm below your eyes: from about
  20 cm away from either, your hand is there.
- The long gun is slung across the back of your body drawn (below, "Your
  body"), as a sling holds a rifle: on its side, about 2 cm behind the
  farthest back of the armour, its middle at the middle of the back, its
  butt up over the right shoulder and its barrel down across the back to
  the left, 30 degrees from upright; it leans back with your body as you
  look down. The assault rifle's butt is just above the shoulder, a long
  gun's (the sniper rifle's, the rocket launcher's) behind your head, its
  muzzle by your left thigh. Where your hand reaches for it stays over the
  shoulder. The weapon at the hip is where your hand reaches, but beside
  your body's hips: a wide one (the needler) is moved out until it is
  about 2 cm clear of them. (Drawn at the reach's own place, a long gun lay
  through the shoulder and the collar, up to 19 cm into them, and a big one
  rose through the shoulder beside your head.)
- A gripping hand in the other holster (with nothing in it) does nothing.
  A squeeze there draws only with the hand all but still: a swing passing
  through, the grip squeezed or held, draws nothing (and melees: below). A
  swing does not melee while the grip taken at a holster is held, nor for
  half a second after you draw, nor as it brings the drawn weapon out
  (drawing is a fast move forward).
- Only on foot, with `vr.aim = "controller"`.

#### Scopes (`vr.scopes`, `vr.scope_view`)

The flat game zooms by narrowing the whole view, which in a headset would
swing the world with your head. In the VR mode the zoom magnifies a view of
its own instead, and the rest of your view stays as it was, in one of two
ways (`vr.scope_view`; VR Controls' SCOPES: SMART-LINK, AT THE EYE or OFF).

##### Smart-link (`vr.scope_view = "smartlink"`, the default)

As the MJOLNIR armour's smart-link pipes the weapon's sight into the
helmet's HUD (the pistol's scope is a "smart-linked scope"), the zoom shows
on your visor: you never bring a scope to your eye.

- The right stick's click zooms as on the flat screen, wherever you hold
  the weapon: it steps through the weapon's levels and from the last
  unzooms (the sniper rifle 2x, then "10x", which magnifies 8 times as on
  the flat screen, then none; the pistol 2x, then none; the rocket
  launcher's too). What unzooms the game unzooms it: a hit, a reload, a
  grenade, a switch of weapons.
- The sniper rifle's and the pistol's zoom is the flat screen's zoomed
  view, fed into the HUD: the game's own scope, drawn from the same art
  (the sniper rifle's rounded frame with its elevation ladders on both
  edges, the diamond reticle, the crosshair's bars and its "2x" or "10x";
  the pistol's round lens, its reticle and "2x"), with the scope's mask
  darkening and blurring the zoomed view about it as on the flat screen.
  While zoomed it shows in a 4:3 window about the reticle: the sniper
  rifle's frame 60 degrees across (`vr.smartlink_size`), the pistol's lens
  to the same scale (about 38 degrees, as much smaller as on the flat
  screen), the window as wide as the HUD (`vr.hud_size`) or a fifth wider
  than the sniper rifle's frame, its edge fading into the world over about
  1.5 degrees. Outside it your view stays the world, unmagnified and a
  little dimmed (12%).
- Other weapons that zoom (the rocket launcher), and a seat's zoom
  (below), have no scope of their own on the flat screen: theirs is a wide
  16:9 window, the smart-link's own, `vr.smartlink_size` across. In it: a
  faint blue tint and scan lines, its edges darkened, a thin line of the
  HUD's blue about it with heavier corners and a soft glow outside; a fine
  crosshair across it with range ticks and a gap at the aim, a ring and a
  faint red dot where the shot goes; and along its bottom edge the zoom,
  the range to what the aim meets and the rounds ("2X   RANGE 014.1   AMMO
  4/8").
- Either window's middle is on the reticle and at the reticle's depth
  (each eye sees it where it sees the reticle; the scopes' no nearer than
  1.5 m), facing you and level with the horizon. It stays where the weapon
  aims: turning or tilting your head does not move or turn it. It shows
  where the weapon aims, magnified (2 or 8 times the world about it). With
  `vr.smartlink_place = "visor"` it is on your visor instead, where the HUD
  is: it stays before your eyes as the flat screen does, and the weapon
  aims what it shows. It is drawn over the weapon in your hand
  (`vr.scope_gun`: or the weapon faintly through it, or in front of it)
  and under the HUD.
- As you zoom, the scope's window grows from the reticle over 0.1 s while
  the view in it pushes in from the world's size to the zoom's over 0.17 s,
  as the flat screen's zoom does; from 2x to 10x the view eases in over
  0.17 s; unzoomed, it is gone at once, as on the flat screen. The 16:9
  window opens out from the reticle over
  about 0.18 s, a bright line sweeping down it and a little interference
  settling, the sight and the readout last; from 2x to 10x its view eases in
  over 0.2 s with a brief flicker; unzoomed, it shrinks back and switches off
  as a CRT does (about 0.1 s), to a line through the reticle and a point.
  With `debug.screenshot_every = 1` (the game slowed by the screenshots)
  these step a 72 Hz frame each frame, so that each shows.
- While zoomed, the aim is steadied, the more the more it magnifies (and
  more still with your left hand on a long gun's foregrip): your hand's
  small shake and slow sway are damped, while a deliberate swing passes
  through promptly, its reticle a little behind it and caught up within a
  quarter of a second once you stop. The reticle, the magnified view and
  the shot all follow the steadied aim: the shot goes where the reticle
  shows, and the weapon in your hand stays where your hand is.
- In a vehicle's seat whose weapon you aim, seen from the seat
  (`vr.vehicle_view = "first_person"`): the Scorpion's driver (either
  `vr.scorpion_view`), and the same in any seat that makes you the
  vehicle's gunner (a turret's, the Warthog's gun). There you hold no weapon of your own
  and the vehicles' weapons have no zoom, so the game's click does nothing,
  on the flat screen too. In the headset the click zooms the same window
  about the seat's reticle, where the right controller aims (the turret
  turns to it): 2x, then 4x, then none (`vr.vehicle_zoom`), its readouts
  the range and the Scorpion's shells; none for a plasma weapon. It is the
  view alone: the game is not zoomed (nothing changes for the shot, the aim
  assist or the network). The sight looks out of the vehicle: its view does
  not show the vehicle, nor the Scorpion's compartment or hatch, and the
  window is drawn over them. Getting out unzooms it.

##### At the eye (`vr.scope_view = "eye"`)

The sniper rifle's and the pistol's scopes magnify as real ones do: raise
one to your eye and look through it, and the view within its lens is
magnified. The lens shows to the eye looking through it; your other eye
sees the scope from outside, as it would.

- The sniper rifle's is a rifle scope, held at the eye: bring the back of
  its scope (where its display is) within about 12 cm of an eye, looking
  along it. Its lens, about 5 cm across, fills a good part of the view.
- The pistol's (the smart-linked scope on the front of its slide) is a
  pistol scope, of long eye relief: hold the pistol out, up to about 65 cm
  from your eye, and line your eye up with the scope (within about 4 cm of
  its line). Its lens is shown as a window of at least 12 degrees, larger
  than the scope itself: its sight's display.
- Raised, it is as many times larger in the lens as the game's zoom is
  (the sniper rifle 2x, the pistol 2x), and the right stick's click steps
  through the levels (the sniper rifle's 2x and 10x, and back; its "10x"
  magnifies 8 times, as on the flat screen). A scope
  raised again has the level it had last, until you switch weapons.
  Lowered, nothing zooms, and the click does nothing.
- The scope also counts as the game's zoom (sent to the other players as
  ever: the zoom's sounds, and what it does to the weapon), so what
  unzooms the game unzooms the scope: a hit, a reload, a grenade. Its lens
  then shows the world at its own size until you click.
- The lens has a reticle (fine lines through the middle, thick posts from
  its edge, and a faint red dot), which is where the shot goes: the world's
  reticle is not drawn while a scope is raised. The zoom is shown small in
  the lens, below and right of its middle, as the game's HUD labels it
  ("2x", "10x"; about 1.6 degrees tall, larger in the pistol's small
  window), not while unzoomed. The flat game's zoomed view (its "2x" and
  "10x" with their dial, the range ticks) is not drawn while a scope is
  raised, which in the HUD would float beside the lens, nor the display on
  the back of the sniper rifle's scope (its green waveform and dial), all
  about the lens at your eye; lowered, the rifle shows it again. It must point within about
  20 degrees of where your head looks; it is lowered past 25 degrees, or
  once your eye leaves it by a quarter more than it came in.
- Other weapons that zoom (the rocket launcher) zoom as the game does,
  which magnifies nothing in the headset.

Either way, the zoom is the game's (sent to the other players as ever),
the first-person weapon stays in your hand while zoomed, and the controller
aims as fast as ever. Only with `vr.aim = "controller"`, on foot.
`vr.scopes = false` (or `vr.scope_view = "off"`): the game's zoom, which
magnifies nothing in the headset.

#### Reloading by hand (`vr.manual_reload`)

Reload as you would a real gun: reach to your left hip with your left hand
and squeeze the left grip to take a magazine (a tick as your hand gets
there, a firmer one as you take it), whenever the weapon has room for one
and rounds to fill it: the magazine in the weapon ejects itself as you take
the new one (a clack and the magazine sliding out, in the right hand; the
weapon is empty now, and does not fire). Push it into the weapon where it
goes, still gripping (a firm click in the left hand as it seats, and a
lighter one in the right). The right stick pushed down ejects the magazine
before you reach, if you like (the same clack). These are firmer than the
pouch's ticks, and the game's rumble (a weapon's firing goes on a moment
after it stops) and the weapons' and the hits' feel ("The weapons' feel",
below) wait while they play. The rounds are in the weapon the moment
it is pushed home: the HUD counts them, and the weapon fires at once (your
hand took the reload's time: the game's own reload, its time and its
animation, is not run). The ammunition is the game's, as a reload by X
would leave it; others see your count change, not your body reload. Your
weapon stays in your hand, as the flat screen's reload animation would not
leave it.

- Where it goes is where the weapon's own magazine is, in its first-person
  model (on the assault rifle, a bullpup, behind the grip, under your
  forearm). The magazine you hold is the weapon's own, drawn in your hand;
  the needler, whose model has none, has its ammunition's pickup, which goes
  in under its body, ahead of your hand. While your hand holds one, where it goes lights up
  in your shield's energy, as the holsters do, the more the nearer it comes;
  within about 7 cm of it, it goes in. While the magazine is out, the
  weapon is drawn without it. Whenever one is to be taken (the weapon not
  full, with rounds to fill it), the magazine at your hip lights up as your
  hand comes to it.
- The shotgun is fed a shell at a time, with nothing to eject: take a shell
  from your hip whenever it has room, and push it in at the loading port,
  under the receiver ahead of the grip. Each shell is loaded as it goes
  in (a short click in each hand), one round (on the flat screen the game loads to full).
- The rocket launcher: the right stick ejects its tubes, and new ones are
  behind your left shoulder (where your left hand reaches over it, as the
  right one does for a long gun).
- The plasma weapons have no magazine, and are as ever.
- An empty weapon does not reload itself, as it does on the flat screen:
  reload it by hand, or with X, which reloads as ever (with its animation).
- A grip at your left hip is the reload's until you let go, whether it
  took a magazine or not (full, no rounds left, a plasma weapon): it holds
  no foregrip and takes no grenade. So is a grip a little short of it
  (within 4 cm of where it takes a magazine), which takes the magazine as
  your hand gets there. With nothing to take, the pouch does not light up,
  and the grip there gives two weak ticks: full. Letting go before the
  magazine is in drops it (the old one is out: take another).
- In a network game the host runs your weapon: as a client, its copy
  reloads an empty weapon by itself and loads the shotgun to full, and
  yours then takes the host's ammunition, as it always does. Your hand's
  reload is a press of reload there, as X is, so as a client the magazine
  pushed home is loaded after the game's reload time, as before (the
  host is not told a reload was the hand's: the network's messages have no
  room for it without a new network version). As the host, or alone, it is
  loaded at once.
- Only on foot, with `vr.aim = "controller"`.

#### The weapons' feel (`vr.recoil`, `vr.weapon_haptics`)

Each shot kicks the weapon in your hand and is felt in the controllers,
each weapon its own way; a hit on you is felt in the hand on the side it
came from.

- The kick: the weapon in your right hand (your hand with it, and your left
  on its foregrip) turns up about your hand and comes back a little toward
  you, and is where your hand holds it again within about a tenth of a
  second (the heavy weapons a little longer): the pistol's snap (5 degrees
  and 1.5 cm at its height), the assault rifle's chatter (1.6 degrees a
  round, quick), the shotgun's shove (8 degrees, 3.5 cm), the sniper
  rifle's heavy kick (9 degrees, 3.5 cm), the rocket launcher's push (6 cm
  back and 4 degrees, slower; the fuel rod gun's alike), the plasma
  weapons' and the needler's light pulse (1 to 1.5 degrees), the plasma
  pistol's charged shot three times its own. However fast a weapon fires it
  kicks no more than 1.6 times a shot's. It is drawn only: the aim is where
  your controller points, so the reticle, where the shot goes and the
  smart-link's view do not move with it; while a scope is raised to your
  eye (`vr.scope_view = "eye"`) the weapon does not kick at all.
- The game's own first-person animations do not move the weapon out of
  your hand: on the flat screen firing (the plasma pistol's charged shot
  most of all), the plasma weapons' venting, a melee, a reload, drawing a
  weapon and putting it away, standing idle and walking swing the weapon
  and the arms about the view, which in your hand would fling it away from
  where you hold it, the arm after it. In VR the weapon stays in your hand
  and only the kick above moves it; what moves on the weapon itself still
  does (the pistol's slide, the plasma pistol's cover and dial as it
  charges and vents, the plasma rifle's vents, the needler's needles, the
  magazines, the shotgun's pump), and the muzzle flash and the other
  effects come from the weapon where it is. Your left hand on a long
  gun's foregrip follows the animation's hand while it stays near the grip
  (the shotgun's pump), and stays on the gun when the animation would take
  it away. With `vr.aim = "gamepad"` the weapon is posed as on the flat
  screen, its animations with it.
- The feel, in the weapon's hand (and a third as strong in the left on a
  long gun's foregrip): the pistol's sharp tap, the assault rifle's fast
  buzz (a short pulse a round), the shotgun's heavy thump, the sniper
  rifle's crack and its decay, the rocket launcher's long push, the plasma
  weapons' soft pulse, the plasma pistol's charged shot's surge (and,
  while it charges, a hum in your hand, rising until it is charged), the
  needler's ticks, the flamethrower's continuous rumble. It replaces the
  rumble the game gives a gamepad for your own firing.
- A hit with damage: a pulse and its echo in the hand on the side it came
  from (from the one who dealt it, else back along the way it pushed), as
  your head faces: from straight ahead or behind, or from above, in both
  hands alike. The more damage, the harder, from about a third for a graze
  to full for 40 points of damage (the assault rifle's bullet deals 10, the
  plasma pistol's bolt about 17). Your shields breaking are a crackle of
  three sharp pulses and a fading rumble in both hands, stronger on the
  side of the hit that broke them. These replace the gamepad's rumble for
  the hits; the game's other rumble (an explosion nearby that does no
  damage, a level's scripted one) is as ever.
- The reload's patterns and the lift's (above, and "Flipping a vehicle")
  come first: while one plays in a hand, a shot or a hit is not felt in
  that hand.
- `vr.recoil` scales the kick and `vr.weapon_haptics` the feel, from 0
  (none: nor the gamepad's rumble for your firing and the hits) to 1 (not
  in VR Controls).
- In a network game, a client fires its own weapon at once (it spends its
  ammunition as it fires), so its kick and feel are at once too; a hit is
  felt as the host's damage reaches it.
- Only with `vr.aim = "controller"`. With a gamepad the game's rumble is
  as ever.

#### Flipping a vehicle (`vr.vehicle_flip`)

Where the game offers to flip a vehicle lying on its side or its roof (on
foot, beside it, nobody driving it: its prompt), lift it over with both
hands, as you would take a bumper or the frame's edge: reach both hands low
toward it (below your waist: about 50 cm below your eyes as recentred,
standing `vr.player_height`; and out toward the vehicle, their middle at
least 25 cm before you that way), squeeze both grips (a tick in each hand
as it takes hold, then a firmer squeeze in both as both hold), and lift
both together, upward (and a little forward, if you like), fast: 20 cm
within 0.3 s. That flips it, as X does on the flat screen: the game's
action, pressed for you (a heavy thump in both hands). The prompt in the
HUD says "Grip low with both hands and lift to flip Warthog" (naming the
vehicle, as the game's does). The game flips the
vehicle as ever: as a network game's client, the host does, as it would for
X.

- X does not flip it (`vr.vehicle_flip = "lift"`, the default): beside an
  overturned vehicle the flip outranks everything else X would do there
  (enter, pick up), so X reloads there instead, as it does where there is
  nothing else to do. `"both"`: X flips it too, and the prompt is the
  game's; `"button"`: X alone, as before.
- One hand, the grips open, or a slow lift (20 cm taking longer than 0.3 s)
  flips nothing. Let go of either grip and take hold again to try again.
- The hands' grips there are the lift's until you let go: they take no
  grenade, no magazine from the left hip, no weapon from the right hip,
  hold no foregrip, and held together they do not recentre (both grips
  held elsewhere still recentre, a flip offered or not). A swing does not
  melee while they hold, nor for half a second after the lift. One hand
  low at the hip and the other up, as for a magazine, is the pouch's or
  the holster's as ever.
- The Warthog and the Ghost turn upright. The game offers the Scorpion's
  flip too, but its flip does not right the tank, by X as by the lift (on
  its roof it rocks and stays; on its side it rolls onto its roof).
- Not with `vr.aim = "gamepad"`: the gamepad's X flips, as on the flat
  screen.

#### Picking up a weapon by hand (`vr.grab_world`)

Where the game offers a weapon lying on the ground (at your feet: "Hold X
to pick up", or to swap for the one you hold), take it with your hand:
reach down to it with your right hand (crouch, as you would) and squeeze
the right grip. The weapon lights up as your hand comes, as the holsters'
weapons do (your shield's energy over it, in pale blue, rising from about
35 cm away and full once your hand is there), and the right controller
ticks as your hand gets there: within 20 cm of the weapon (of the line
along it, its middle and its ends), a firmer tick as the grip takes it.
The grip is the game's pick-up, as X held: the weapon goes where the game
puts it, in your hand, the one you held dropped for it (with two carried)
or into its holster on your body (with one). As a network game's client,
the host picks it up as it would for X.

- With several weapons at your feet the game offers the one nearest your
  body; the one your hand reaches for (its light) is offered instead, and
  the HUD's prompt names it. As a network game's client, the host decides
  which, by your body as ever: your hand takes the one the prompt names.
- The grip is the holsters' at your right hip and over your right
  shoulder: a weapon on the ground is far from both. Squeezed in a swing
  passing by (the hand faster than 2 m/s), it takes nothing. A swing does
  not melee while the grip taken at a weapon is held, nor for half a second
  after. The left grip takes grenades and magazines, as ever: only the
  right hand takes weapons.
- X held picks up as ever (`vr.grab_world = false`: X alone). Ammunition
  is picked up by walking over it, as ever. Entering a vehicle stays on X:
  no gesture is as plain as the button there (a hand on a door, a seat's
  edge, differs for each seat, and a reach past a vehicle would climb in).
- Only on foot, with `vr.aim = "controller"`.

#### Pressing a control by hand (`vr.grab_world`)

Where the game offers a control to activate ("Press X to activate": a
door's panel, a bridge's, a lift's, the holo panels of the Forerunner
buildings), press it with your hand: push either hand into its face, as
you would a button (the left hand open, or the right hand's fist, the
weapon in it). Your fingers (9 cm before the controller's grip, where it
points) pushed to its face within half its size of its middle (from 6 cm
to 50 cm: the holo panels are 2 m across) press it, a click in that hand:
the game's action, as X. Once your fingers have left it (5 cm back), they
press it again.

- The game offers a control along your aim, which is the right
  controller's, whatever the hand pressing does: as your hand presses, the
  game's aim turns to the control for a few hundredths of a second, until
  the game offers it, then the action. The reticle, your weapon and the
  view stay with your hands and head: only your body, as others see it,
  looks at what you press. As a network game's client, the host decides
  the controls you use by your aim, which it is told, as for X: it presses
  it as it would for X.
- Not the left hand on a long gun's foregrip, holding a grenade or a
  magazine. A swing does not melee for 0.4 s after a press (a brisk push).
- X activates as ever (`vr.grab_world = false`: X alone).
- Only on foot, with `vr.aim = "controller"`.

#### Driving a Ghost by its handlebars (`vr.ghost_steering`)

In a Ghost's driver seat, grip its handles with both hands and turn them as
a bike's handlebars: the Ghost turns, and its guns aim where the bars point.
The left stick throttles and strafes as ever, the right trigger fires, and
jump brakes (the Ghost has no boost).

You need not take hold to drive: with the hands off the handles, push
either stick sideways and the Ghost turns that way, up to 90 degrees a
second at a full push (past a fifth of the stick: a resting thumb steers
nothing); let it come back and the Ghost holds its course. The left stick
then throttles and steers, as a car game's, and does not strafe; it strafes
again once both hands hold the handles. Its guns fire straight ahead.

- Take hold: squeeze each grip with the hand near its handle (within
  30 cm: the handles, on each side of the cockpit, are wide apart for the
  Chief), a tick in that hand; from then on that hand is drawn on its
  handle, closed about it, wherever your hand is, and the other where its
  controller is, until you let go.
- Steer: with both hands on, turn the bars (the line from your left hand to
  your right) and the Ghost turns, 2 degrees a second for each degree of
  the bars, up to 90 degrees a second (from 45 degrees of bars); bars held
  turned keep it turning, straight bars hold it straight (2 degrees either
  way are straight: hands are never quite still). Tilting your hands up or
  down (from how they pointed as you took hold) pitches the guns, 35
  degrees at most. The reticle is where the guns aim.
- Let go of either grip and the Ghost holds its course: the turn eases out
  over a few tenths of a second, never a sudden turn; your head looks
  about freely and steers nothing.
- The view turns with the Ghost: your heading is the Ghost's, smoothed
  each frame (not in the steps of the game's 30 ticks a second), and you
  look about from there; the right stick turns the view nothing (it steers
  the Ghost while your hands are off the handles), and both grips are the
  bars, not a recentring. You take the Ghost's heading as your eye comes
  into the seat, the room turning gently to it (no faster than 45 degrees a
  second; with `vr.seat_motion = "blink"`, in the blink), and keep it as you
  get out. The horizon stays level, and your eye is held in the Ghost as it
  would be level: its pitching and rolling as it hovers (up to 9 degrees,
  your eye 1.45 m behind its middle) reach your head only slowly, as a
  slope's, not as its quick wobble.
- The edges of your view dim a little while it turns fast (from 40 degrees
  a second, most at 120) or the Ghost speeds up or slows hard
  (`vr.comfort_vignette`, on by default).
- The Ghost's handles do not turn on its model (it has no node for them).
  The steering is the driver's facing, the game's own (the Ghost turns
  toward its driver's facing), which is all a network game sends of it: it
  steers so as a client too.
- `"look"`: the Ghost steers and aims where the right controller points, as
  any other vehicle does, and the view turns with the stick, not the Ghost.
- Only with `vr.aim = "controller"`; the Warthog's driver seat is steered
  by its wheel (below); other vehicles are as before (the Banshee is to
  come).

#### Driving a Warthog by its wheel (`vr.warthog_steering`)

In a Warthog's driver seat, take its wheel with both hands and turn it as a
car's: the Warthog's wheels turn, and it turns as they do. The left stick
throttles (and reverses) as ever, jump brakes.

Or drive without taking hold: with the hands off the wheel, push either
stick sideways and the Warthog's wheels turn that way, to their lock at a
full push (past a fifth of the stick), the other way as it reverses, as the
wheel does; let it come back and they straighten.

- Take hold: squeeze each grip with the hand near its side of the wheel
  (within 30 cm of the wheel's grips: about 70 cm before your eyes and 25 cm
  below them, 29 cm apart), a tick in that hand; from then on that hand is
  drawn on the wheel, closed about its rim, and turns with it.
- Steer: with both hands on, turn the wheel (the line from your left hand
  to your right, turned about the wheel's axis from where it was as you took
  hold) and the Warthog's front wheels turn a quarter as far, to their
  lock (30 degrees either way: the wheel turned 120 degrees), the rear ones
  the other way, as the game steers them; how fast it turns is its speed's
  doing, as a car's: at 20 m/s, 10 degrees of its wheels (40 of yours)
  turn it some 80 degrees a second. Reversing, it turns the other way, as a
  car does. 2 degrees of your wheel either way are straight.
- Let go of either grip and the wheel comes back straight over a few tenths
  of a second: the Warthog holds its course.
- The model's wheel turns as the game turns the Warthog's wheels (its
  "frame steering wheel", by the game's steering animation), and your hands
  with it.
- The ride is the Ghost's (the view turns with the Warthog, level; the
  vignette), for the Warthog's suspension: its pitch and roll (past 40
  degrees on b30's beach) reach your eye over half a second. Your eye rises
  and falls with it, which it does with the ground, slowly (under 2 Hz:
  there is no quick bounce to smooth).
- The steering is the driver's facing, the game's own (the game turns the
  wheels toward the facing), which is all a network game sends of it.
- `"look"`: the Warthog steers where the right controller points, as before,
  the view turned by the stick. Only with `vr.aim = "controller"`.

In a Warthog's passenger seat, your hands are as on foot, the weapon in your
right hand (a passenger shoots as ever, CE's passenger's aim reaching 90
degrees either side), and the ride is the driver's: the view turns with the
Warthog, level, your eye held level in it, the vignette. The game turns a
passenger's facing as the Warthog tilts (as the flat screen's camera tilts
with it): the VR mode's, taken in the world, is turned back first, so the
shot goes where the reticle is however the Warthog leans. Whatever
`vr.warthog_steering` is (with `vr.aim = "controller"`).

At a Warthog's chaingun (its gunner's seat), you stand behind the gun as
the Warthog carries you: the right controller aims it as it aims a weapon
(where it points, the reticle where its line meets the world; the turret
turns at most half a turn a second, as the game turns it), and the right
trigger fires. Squeeze the grips with your hands at the gun's handles
(within 30 cm) and your hands are drawn on them, turning and pitching with
the gun; let go and they are where your controllers are. No weapon of your
own is drawn. The ride is the driver's: the view turns with the Warthog,
not the turret (which swings before you), level, the vignette.

#### Flying a Banshee

In a Banshee's pilot seat, the ride is the Warthog's: the view turns with
the Banshee, the horizon level whatever it does (it banks as it turns, past
30 degrees, and pitches as it climbs and dives: the ship moves about you,
not the world), your eye held level in it (its tilt let in over 0.4 s), and
the comfort vignette, which counts its climbs and dives as it counts its
turns (the level view does not follow them, the world streaming past it up
or down). It steers where the right controller points, as before, now from
the Banshee's heading (pointing 30 degrees aside, it turns some 60 degrees a
second; up and down, it climbs and dives); the left stick throttles, the
triggers fire its guns. Its cockpit is the game's model (none of its own
yet). With `vr.aim = "controller"`.


#### Climbing into a vehicle (`vr.vehicle_climb`)

With the controller aiming, the Scorpion, the Warthog, the Ghost, the Banshee
and the Shade can be climbed with your hands, as in a climbing game: the
hands pull and the world follows them, which is felt as your own motion. X
still gets you in hands-free, into any vehicle; one without holds (the
Wraith, a dropship) is got into with X alone.

- Each vehicle's holds are where a Spartan would grab. As a hand comes
  within reach of one, the vehicle lights up (as the holsters' weapons do)
  and the controller ticks.
- Grip to take a hold (a firmer tick). While a hand holds, your view moves
  against the hand's motion, 1:1 in every direction: pull down and you rise,
  pull toward you and you move forward. Either hand, or both; hand over hand
  works. The weapon in your hand is not drawn while a hand holds. Let go of
  both and the view comes back to your body gently, at walking pace (from
  high on a vehicle, across and then down).
- Got in, your view glides on into the seat (no faster than 0.8 m/s), and
  turns to face the vehicle's front as it does (no faster than 45 degrees a
  second), with no blink.
- The Scorpion: each track guard's outer edge (at about 1.6 m), a rail
  along the middle of its top, a rail at the foot of the hull's side, the
  hull's top edge, and the rim of the hatch over the driver's cockpit. Take
  the rim and lower yourself (raise your hands): as you come down to the rim
  your view is drawn toward the middle of the opening, and 30 cm below the
  rim you are in the driver's seat, your view carried on into the cockpit
  and turned to face the display. Out: take the rim overhead from the seat
  and pull yourself up; 30 cm above it you are out.
- The Warthog's driver and passenger: the windscreen's corner post, the
  side's top edge and the seat's back, by each seat. Take one and pull
  yourself in: within 60 cm of the seat your view is drawn to it, and within
  25 cm you are in it. Out: take the post or the side's edge and push
  yourself up and over it; 35 cm from where you sat you are out.
- The Warthog's gunner: the tailgate's top edge and the bed's sides climb
  you up into the bed; both hands on the chaingun's handles, near where the
  gunner stands, and you are on the gun (your hands stay on its handles).
  Out: let go of the handles, take the bed's side and push yourself up and
  over it.
- The Ghost: its handlebars and the cowl's edges above them. Take them and
  pull yourself toward the saddle: within 60 cm your view is drawn into it,
  and within 25 cm you are on it, the handlebars yours to steer by. Out:
  with both hands on the handlebars, push off them (past 10 cm of slack,
  your view moves back as your hands push); 25 cm back you are off, still
  holding them, until you let go.
- The Banshee: its tail booms' tops, the hull's edges beside the cockpit,
  and the cockpit's sills. Come over the cockpit and lower yourself into it,
  as into the Scorpion's hatch; 12 cm above the sills you are in the pilot's
  seat. Out: take the sills and push yourself up; 35 cm above them you are
  out.
- The Shade: two handles at the back of its gun, and its seat's sides. Pull
  yourself up by them; both hands on the handles, near the seat, and you are
  on the gun. Out: take the seat's sides by your hips and push yourself up
  and off.
- Your view's climb is the view's alone, as your head moving in the room is:
  your body stays on the ground beside the vehicle (others see it standing)
  until you are in the seat, and your hands pull your view no farther than
  4 m from it. Only what X would do reaches the game, so it plays in a
  network game.

#### Driving a Scorpion (`vr.scorpion_view`)

The Scorpion's model is a closed hull, made for the game's camera following
it: its driver's head is in the hatch's lid, over an empty slot. So the
eye is put where it belongs, and what the model lacks is drawn about it, with
the game's own textures and lighting:

- `"feed"` (the default): you drive from a closed cockpit inside the hull,
  the world on a display all the way round you, as UNSC vehicles run on camera
  feeds. The cockpit is a frame of reference that stays still about you as
  the tank moves, a defence against motion sickness. Its walls are the
  Pelican's troop bay's gunmetal panels (the Scorpion's own in a map without
  the Pelican), and in it are a dash, the two steering levers, side consoles
  and a seat, your body seated in it (below, "Your body"), its legs under
  the dash in a footwell.
  - The display wraps all the way round you: the cockpit is round, and the
    display is the opening in its wall, from 20 degrees below your eyes to
    45 above, so you can turn your head and look out any way. The world in
    it is at its true size and depth in each eye, as through a window, with
    the feed's look over it: a faint blue tint, scan lines, its edges
    darkened, a frame of the HUD's blue. Ribs every 45 degrees (but ahead
    and behind) mark its panels, the cockpit's still frame with the floor,
    the dash and the levers. The front panel, from 45 degrees left to 45
    right, is the main one: along its bottom the zoom, the range to where
    you aim and the cannon's rounds. The others are the hull cameras, a
    little dimmer, labelled L HULL, R HULL and REAR. The reticle is where
    you aim, as always; the turret turns to it. The right stick's click
    opens the 2x and 4x window within the front panel (`vr.vehicle_zoom`).
  - On a holotable under the display, the tank itself: the Scorpion's own
    model a hand's length long, glowing in the HUD's blue, its turret turned
    as the real one is (at a glance, which way the gun points against the
    hull you sit in), tilted as the hull is from level.
  - Getting in and out is in the first person, with no break: as you climb
    in (the X button), your view rides the body through the game's own
    climb onto the tank and down into the driver's place, and the cockpit is
    there about you as you come down into it. The ride follows the body's
    head smoothed (the climb's bumps gone, no faster than 3 m/s: where the
    game slides the body to the climb's start, the view glides), never its
    head's turn: your own head keeps its pitch and roll. As you climb, the
    play space turns with you, eased and no faster than 45 degrees a
    second, so that you face the display as you sit. The edges of the view
    dim a little while it moves faster than walking (`vr.comfort_vignette`).
    The tank's armour is not drawn about you once you are within 15 cm of the
    cockpit, so you do not see through its plates as you pass, and from inside
    only the world is seen through the display, as a hull camera's. Seated,
    the display wakes: a grate of bars slides down over it, and the feed
    washes over it from the top down (0.4 s). Getting out, the display
    collapses dark as the body starts to climb, and the view rides it up and
    out and down beside the tank, to where you stand.
  - With `vr.seat_motion = "head"`, the view is where the game puts the
    driver's head, as in the other views.
- `"hatch"`: you stand in the turret's hatch, a cupola ring
  about you with its lid open, the eye above the turret on its axis, so that
  it does not swing as the turret turns under it. You see all about, over
  the hull; the turret aims where you aim.
- `"inside"`: you sit in the driver's compartment at the front of the hull,
  under the front plate: walls, the hatch above you, a seat, the steering
  levers either side of your hips, an instrument panel over your knees and
  a footwell under it for your legs, and vision blocks across the front, which
  you see out through.

In each, the view stays yours, level and turned by your head and the stick:
the Scorpion turns toward where you aim as you drive it, as it does under
the game's camera, so the hull turns under you, not the view with it. The
left stick throttles, and pushed sideways turns you smoothly (up to 90
degrees a second at a full push), and the aim with you, so the tank turns
after it, as the right stick does; it does not slide the tank sideways
(which it never did). A Warthog driven with `vr.warthog_steering = "look"`
is the same.


#### Flying a Banshee

The Banshee's canopy closes over your head as you fly, and its faces face
out, so from the seat you saw only its nose below you. Now you sit in a
cockpit: a sill at your shoulders, a console ahead with the Covenant's
glowing panels and two handles, and on the canopy a rail along each side, a
spine overhead, a bow ahead framing the view and a shell behind your head,
opening with the canopy as you get in and out. The view ahead and above
stays open, as through the canopy.

#### Your body (`display.first_person_legs`)

With LEGS on (Video Setup > FOV and Viewmodels, `display.first_person_legs`;
on by default in the VR build),
looking down in the headset shows Master Chief's body under you: the
chest, the legs and the feet, but not his head nor his arms (your arms are
the VR mode's own, `vr.hands`, from shoulders over his). The body is where
yours is, not where the game's eye is:

- Its neck is under your head: at the top of your neck, about 10 cm behind
  and 10 cm below your eyes as your head is turned and tilted, so that it
  stays put as you nod and look down, and moves with you as you lean or
  step about the play space; its own neck 5 cm behind that, as the armour's
  chest is deeper than yours: standing, the front of its chest is about
  10 cm before your eyes (yours is about under them), not 15. It stands on
  the ground as the game has it.
- It faces your body's heading, the holsters' (above): where your head
  faces, once your head turns more than 40 degrees from it, and then
  slowly; not where the gun aims. Your arms' shoulders (`vr.shoulders`)
  face the same way, from the same neck.
- It stands upright and square over its hips, as you do: the game's body
  leans into its aim, its chest turned to the gun while its feet stay
  planted. Its legs walk, run and crouch as the game animates them.
- It is not set back behind the eye as on the flat screen: its chest
  closes its waist. Looking straight down, the Chief's broad chest hides
  his feet below it; they show as they step forward, or looking less
  steeply.
- Its upper body leans back from your eyes, about its hips, as far as
  keeps what your eyes see of it out of the near edge of the view (about
  19 cm before them), which would cut into the armour of the chest and
  collar and show it hollow: looking ahead, or down at your belt, it stands
  upright; looking down over the chest, your eyes nod forward over it, and
  it leans back only as far as needed, about 20 degrees looking 60 degrees
  down and 35 looking 80 down (up to 50 crouched low, or with a pistol,
  whose stance holds the chest higher). Nor do your eyes see the opening of
  the collar (no head is drawn, and the top of the neck is a dark pit),
  which a chin would hide. It leans at once as far as needed, and comes
  upright again over a second. Its hips and legs, and the holsters and the
  pouch about them, stay where they are.

The body is the one everyone else sees, drawn differently for you alone:
hit boxes, markers and what others see are the game's.

In a vehicle's seat (`vr.vehicle_view = "first_person"`) your body is drawn
too, once you are in the seat (not while it climbs in or out; in the
Scorpion's feed cockpit, once the display has woken), as the game seats it:
in the Scorpion's driver seat leant back with its legs out ahead, in the
Warthog's seats, on the Ghost, lying along the Banshee, standing at the
Warthog's chaingun, on the Shade.

- Its neck is under your head as you recentred: 10 cm behind and below
  the seat's eye (or its cockpit's), and its own 5 cm behind that, as on
  foot. Its hips and legs stay in the seat; as your head moves, its upper
  body turns toward it about the hips (as far as 34 degrees, as a person
  seated leans and twists), and leans back clear of your eyes as on foot.
- No head; its arms are yours (`vr.hands`), from its shoulders to your
  controllers. In a seat where the game draws no first-person arms (the
  Scorpion's driver, the Banshee's pilot, the Shade's gunner), they are
  drawn there now, the hands where the controllers are, without a weapon,
  as on a Ghost's handlebars off them. With `vr.aim = "gamepad"` the body
  keeps the arms the game gives it.
- The Scorpion's cockpits were made for no body: its feed cockpit and its
  inside view are made around the Chief seated in them (the seat's pan
  under his hips and its back behind him, the floor under his feet, the dash
  or the panel over his knees, a footwell ahead for his legs, the levers
  either side of his hips). In the hatch (`"hatch"`) he stands, his legs
  hanging straight down into the turret, turned to your body's heading:
  from the eye his chest rises through the cupola's ring.
- Only this headset draws it so: the game's body in the seat, what others
  see (in the Scorpion, hidden in its hull), is as ever.

#### Crouching (`vr.physical_crouch`)

Crouch in the room and you crouch in the game; stand up and you stand.
Your head about 35 cm below your standing eyes (`vr.height = "standing"`:
`vr.player_height` above the floor), or 25 cm below where it was at the
recentring (seated), crouches; back up within 7 cm of that, you stand. The
game crouches as the left stick's click does, held while your head is down
(Halo's crouch is held, not toggled), and through its input and the
netcode as ever: your body is lower, and moves as a crouched player does
(at a full stick too, which would stand a crouch by the stick).

- The view follows your head alone: the game lowers its eye as it crouches
  (0.82 m for the Master Chief, at `vr.world_scale` 1), which is taken back
  out while the crouch is your head's. A crouch by the stick's click keeps
  the game's drop.
- Only on foot, not in a vehicle's seat nor in the menus; any `vr.aim`.

#### Cinematics seen from your own head

Some cinematics are seen from the player's own eyes, not from a movie
camera: the Pelicans' rides in (the openings of a50, b30, b40 and c10),
seated in the troop bay, and a30's lifepod once it has come down, where you
look about but cannot move yet. There your hands are your own
(`vr.hands`), the weapon in your right hand as the game has it in hand, the
left hand open or closing with its grip, as in play. They act on nothing:
the cinematic has the game's aim and none of the player's input, so the
trigger fires nothing, and the holsters, a grenade's throw and the scopes
wait until you have control. A cinematic seen from a movie camera elsewhere
keeps the game's view, with no hands floating in it.

- The hands are the first-person weapon's (its arms are the model): with no
  weapon in hand there are no hands, in a cinematic as in play.
- With `vr.aim = "controller"` (with `"head"` or `"gamepad"`, the game's
  arms as ever).
- The log says when each cinematic begins, whether from the player's head
  or a movie camera, and when it ends (`vr cinematic:`).

#### Dying (`vr.death_view`)

With `vr.death_view = "crt"` (the default), dying is your helmet's feed
switching, in the language of the smart-link's window:

- As you die, two frames of the shields' alarm (static, red), then the
  helmet's view collapses as a CRT switches off: to a bright line of the
  HUD's blue through the middle of your view, the line to a point, dark
  (about a quarter of a second).
- In the dark the view cuts to a death cam: a vantage fixed in the world
  about 3 m from your body, to its side (behind and to the side of where
  you faced, where the body is in sight and the vantage clear of walls:
  the 8 directions about the body are tried, then nearer), your eyes at
  standing height above the ground under the body, facing it. Your head
  looks about from there as ever. Its feed switches on (a point, a line,
  the whole view, a little interference settling) and shows as a feed: a
  faint blue tint, scan lines, its edges darkened.
- Turning (the right stick) moves the vantage 45 degrees about the body,
  in a quick cut (off and on); it is put again so if the body goes out of
  its sight or slides away.
- As the game brings you back (the checkpoint, or a multiplayer game's
  respawn), the feed switches off, the helmet's view switches on, and the
  HUD boots over a second: the shield meter charges from empty, the motion
  sensor comes up, the ammo counts up.

The other values keep the views before it. With `"face"`, when you die,
the view stays where your head was: you look about from there
as ever, and nothing else moves or turns it but a blink. Your body
falls before and under you (look down), drawn as the others see it, and the
world dims and reddens. Just before the game reverts to the last checkpoint
(three seconds after, in the campaign) or respawns you (in a multiplayer
game), it goes dark, and you come up out of the dark in your new place. The
flat game's camera instead pulls away from the body over three seconds,
turning to keep it in view: on b30, 10 to 14 m out and 7 to 12 m up, at up
to half a metre a frame, which in a headset carries the world off and
turns it about your head.

- The death begins with a dark beat: your eyes are within the falling body
  for a moment (its head is not drawn while it is at your eyes, and the
  view dims while any of it is within 25 to 55 cm of them), and the body
  falls away out of it.
- With `"face"` (the default), a body that lands out of sight (more than
  50 degrees from where you look and more than a metre away across the
  ground: a blast before you throws it back) turns the view to it in a
  blink, once, about a second after, when it has landed; your eye stays
  where it is. A body at your feet you see by looking down. `"still"`
  never turns the view.
- The visor goes with the HUD (the game shows the dead none), with a last
  red flare on its glass.
- Watching someone else (network co-op's spectating follows a teammate; a
  multiplayer game's camera turns to another player after 15 seconds), the
  view is put where the game's camera is, looking where it looks, once it
  has settled on them, in a blink, and held there; and put there again in a
  blink once the game's camera has moved 4 m from it.
- `"game"`: the game's camera, as the flat screen's, blinked into and out
  of.

Only the view changes: the game's camera (which the sounds are heard from),
the revert, the respawn and what the netcode sends are the game's.

#### Your head in a wall (`vr.head_in_wall`)

Your head moves in the room apart from your body in the game, which the
game moves and keeps off the walls (its eye stays some 0.6 m from them).
Lean or step into a wall, a rock or a pillar and your eyes would go into it,
and see through it: the level's far side, and what is behind. That is the
default (`"off"`): a view going black in play is worse than the peek. With
`"fade"` the view fades to black as your eyes go in, over the
last few centimetres, and comes back as you lean or step out (over a
quarter of a second). The HUD stays, saying to step back, and a scope shows
nothing meanwhile (it would see through the wall too).

- The game draws nothing nearer the eyes than 19 cm (its near plane): a
  wall closer than that before your eyes is cut away whole and shows what
  is behind it. So the view begins to fade where the wall before your eyes
  is 25 cm away and is black at 20 cm; your eyes in it, or through it (the
  line from the game's eye to yours crossing it), it is black whichever way
  you look.
- Leaning around a corner or out from behind a rock with your head in the
  open, crouching behind cover, and your head near a wall (farther than
  that) do not fade, nor does anything in a vehicle's seat (its own hull
  about you) or as you die.
- What counts is what blocks the game's line of sight: the level, scenery
  and machines (doors); not grates and fences (two-sided surfaces), the
  invisible walls at a level's edges, water, other people, nor vehicles,
  which drive past your head (and whose hulls are not their models).
- `"off"`: you see through it, as before.

Only the view changes: your body stays where the game has it.

#### With a gamepad (`vr.aim = "gamepad"`)

A gamepad (an Xbox controller, by Bluetooth or USB, or the headset's
controllers as its two halves) plays as on the flat screen, with the
profile's button layout, look sensitivity and acceleration, and the aim
assist: the left stick moves, the right stick turns and aims, and the
triggers, bumpers, buttons, d-pad, Start and Back are the game's. The
headset shows the world, and the head looks about from where the stick
points the view (`vr.gamepad_view`):

| | Does |
| --- | --- |
| Right stick | Turns and aims the player, as on the flat screen. With `vr.gamepad_view = "camera"` (the default) it is the camera, as on the Xbox: it turns and pitches the view smoothly, and you aim at the middle of the view. With `"level"` it turns the view smoothly but never pitches it: its pitch moves the weapon and the reticle, not the horizon. With `"snap"` the view's heading turns by `vr.snap_turn_angle` each time the aim is more than that from it (the aim sweeps across the view, which turns in steps), and its pitch moves only the weapon and the reticle. |
| Head | Looks about from the view the stick points, in 6 degrees of freedom, and never moves the aim: with the head straight the reticle is in the middle (with `"camera"`), and a glance looks elsewhere while the reticle stays where the aim is. |
| Back (View), held 1 s | Recentre: where the head looks becomes where the player aims |

`"camera"` is the most like the Xbox game, and the most likely to make you
sick: the stick turns and tilts the whole world while your head is still.
`"level"` keeps the horizon level, and `"snap"` turns it in steps, which
most people find the most comfortable.

The weapon is posed as on the flat screen from the player's eye looking
along the aim, so it stays before the body whichever way the head looks.
The reticle is in the world, where the aim meets it (as far as what it
aims at, in both eyes), as large as in the HUD.
The headset's controllers are an Xbox controller's halves here: their
sticks, buttons and triggers are a gamepad's (the right stick looks, never
snap-turns), where they point aims nothing, and a swing does not melee;
they rumble with the gamepad. Both grips held still recentre. In the headset
every gamepad is the first player's: Steam may list a gamepad of its own
(Steam Input's) besides the one in the hands, and all of them merge into
controller 1 (each stick pushed furthest wins). In a vehicle the view is
as `vr.vehicle_view` says, and the vehicle steers and aims where the stick
does.

#### Sound from your head (`vr.hrtf`)

You hear from where your head is, as you see from there: the world's
sounds are placed about your head as it is tracked, its place and its turn
(left and right, up and down, and its tilt), each frame, not about the
game's camera (the body's facing, which on the flat screen moves only at
the game's 30 ticks a second). Turn your head and a Hunter behind you comes
round to your side; lean and a sound beside you is nearer one ear. A sound
from the world moves smoothly from ear to ear as your head turns: each
sound's place is given to the mixer each frame, a few millimetres being
enough to move it (on the flat screen, 15 cm), and the mixer ramps it across
each 10 ms it mixes, with no step.

- From where: the game's eye at its tick (as on the flat screen: the
  sounds are where their objects are at the tick, and the player's own,
  carried along, stay put about the head), moved as the view's eye is from
  it (your head's crouch, a vehicle's seat, the death's held eye) and by
  your head's place in the room. Turned as your head is this frame (as it
  is located when the frame begins, before it is drawn).
- The sounds placed about the listener rather than in the world (a
  looping sound's details: a bird, a gust, at random about you) are about
  your body, not your head: they stay where they are as your head turns.
- The HRTF (`vr.hrtf`, the default): each sound reaches each ear as it
  would your head's, not panned between them: a little later at the far ear
  (up to 0.66 ms, the way around a head), duller there and brighter at the
  near ear (the head's shadow, above about 1 kHz), a notch of the pinna's
  that rises with the sound from below your ears (6 kHz) to above them (11
  kHz), and a sound behind duller (up to 7 dB above 2.5 kHz). It is a model
  of a head (Brown and Duda's), not one measured: it needs no dataset, and
  costs a few filters a sound. Over all directions a sound is as bright as
  panned. Behind and before, above and below are best told apart by turning
  your head, which the listener now follows. `false`: panned left and right,
  as on the flat screen (from your head all the same).
- Only the first player's, in the headset; on the flat screen, and played
  flat (`vr.enabled = false`), the sound is the flat game's.
- `debug.vr_audio_log` logs, each frame, where the sounds were heard from
  against the head the frame was drawn from; `debug.audio_log`, each sound
  in the world playing (where it is from your head, its gains, and the
  HRTF's delays and filters); `debug.audio_record` records the mix to a
  WAV file; `debug.vr_test_sound` plays a sound held in the world (below).

#### Dynamic lights (`display.dynamic_lights`)

With `display.dynamic_lights = "enhanced"` (Video Setup > Graphics >
Dynamic Lights, on the flat screen too), the lights of plasma bolts,
needles, muzzle flashes, grenades and explosions are drawn for each pixel:

- A bolt's light glides with it. The game places a light attached to an
  object at each tick, and the objects are drawn between ticks; each light
  is now drawn on its object's marker as drawn, in both modes. A plasma
  grenade thrown on a50, frame by frame at 72 Hz: its light moved 0.139
  world units each frame in flight, where the tick's position moved 0.333
  every third frame and stood still between (`debug.dynamic_lights_log`).
- Bolts and needles light what they pass. On the Xbox their lights are their
  lens flares' only (`weapons\plasma rifle\bolt`, `weapons\needler\needle
  flare`: not dynamic); enhanced, each lights by its tag's color and radius.
- The structure is lit for each pixel through its bump map, the models
  through their normals, and the first-person weapon and hands by the
  lights near them where they are, in your hand (the game lit them by your
  body's two nearest lights): your own muzzle flash, which is moved to the
  first-person weapon's muzzle, your grenade, the bolts passing.
- No light is left out: each pixel takes the lights of its cell of the grid,
  as many as the game draws (128 a window).
- A glow is drawn about each plasma bolt in flight (a projectile with no
  model: a needle's or a grenade's own body would hide its middle), sized
  and colored by its light, in both eyes at its depth (both eyes in one
  draw, the foveated insets too), fading as it nears what is behind it. In
  the VR mode's eyes it is the light the bolt scatters toward each eye
  from a haze about it, worked out along each eye's ray ("Effects with
  depth", below).
- Vulkan draws the lights, grid and shoulder, and the glows, from shaders
  written as SPIR-V, prepared with the map's other pipelines as it loads.
- The light is added in linear light under a soft shoulder: a light's core
  comes up bright and fades softly outward, never clipping to a flat
  disc. An explosion (its tag's radius 10 world units) is bright about it,
  not over everything it reaches.

Its cost, on spark (`HALO_GPU_STATS=1 HALO_VR_GPU_TIME=1`, 1728 x 1728 eyes,
foveated, a50 with the plasma rifle firing in bursts): the GPU's time a
frame 0.97 ms enhanced, 0.93 ms classic, over 70 s. A pixel's cost grows
with the lights that reach it, not with the frame's; the Steam Frame's GPU,
several times slower, is to be measured.

#### Effects with depth

In the eyes, the game's effects (smoke, dust, sparks, explosions, muzzle
flashes' sprites, contrails, rain and snow: everything drawn with its
effect shaders) are drawn with depth, always (there is no setting; the
flat screen draws them as the game does):

- They face the head. A sprite that faces the viewer faces the centre of
  the head and stays upright in the world, so it holds still as the head
  turns and rolls, and both eyes see the same card (the game draws the 3D
  view once, from the head's centre, into both eyes). Near straight above
  or below the head, it stays upright toward a fixed axis of the world,
  never the view's.
- They are soft. Each pixel of an effect fades as it nears the wall,
  floor or object behind it (the depth the scene was drawn with), over a
  distance in the world of half its sprite's radius, so an effect meets the
  world without a hard straight line.
- They are thick. Each effect bitmap's thickness is baked from its alpha
  as the map loads (its silhouette's distance to the edge in each sprite,
  made a sphere's profile, smoothed, thinner where the alpha is faint) and
  kept with the texture. A sprite at least 2 degrees wide on the screen is
  drawn with it as a bulge toward the eyes (half its radius high at its
  middle, so puffs look round, not like balloons): each eye's ray meets the
  bulge's front where that eye's texel is drawn, so the two eyes see
  different texels and the effect has depth inside it, and it fades into
  the world from that front. Smaller sprites (sparks, rain, distant puffs)
  stay flat cards.
- A plasma bolt's glow (`display.dynamic_lights = "enhanced"`) is the
  light the bolt scatters toward each eye from a haze about it, its
  density falling off as the card's glow does across it: closed along each
  eye's ray, from the eye to the scene's surface. Unhidden it is the card's
  glow; a wall through it hides the part behind the wall, in each eye.

port/linux/vulkan/README.md ("The VR mode's effects") says how they are
drawn and what they cost.

### Settings

In the headset, Settings' VR Setup (below About; only there while the
game plays in VR) sets most of these with the left d-pad: VR Controls
(the hands, the aim, the gamepad's view, two-handed aiming, the aim assist, the melee
swing, the holsters, the physical crouch, the scopes (smart-link, at the eye or off), reloading by hand, turning, the vehicles' view and the view as you die) and, from its last row, VR Display (the play position,
the eye height, the world's scale, the HUD's and the menus' distance and
size, the resolution and the refresh rate, and first the dominant hand and
the sticks) and, from its last row, its
visor page (the HUD on the visor or flat, its depth, its wrap along the
visor, its lag and glow, its look on the glass, its reaction to the world, the
helmet's rim and its depth, the glass's shade, the shields' effects and
the strength of their energy). OK writes them to
`config.toml`, and they apply at once, but those marked RESTART:
`vr.height`, `vr.resolution_scale` and `vr.refresh_rate`, which the
headset's session is opened with, apply from the next start.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `vr.enabled` | `true` | `HALO_VR` | `false`: play flat. |
| `vr.resolution_scale` | `1.0` | `HALO_VR_RESOLUTION_SCALE` | The eyes' images, as a multiple of the size that the headset recommends (from `0.25` to `2`). |
| `vr.refresh_rate` | `90.0` | `HALO_VR_REFRESH_RATE` | The refresh rate to ask the runtime for. `0`: the runtime's. When the runtime refuses it or gives another, the log lists the rates the display has (`vr: the display's refresh rates: ...`). |
| `vr.aim` | `"controller"` | `HALO_VR_AIM` | `"controller"`: the right controller aims and holds the weapon. `"head"`: the head aims. `"gamepad"`: a gamepad aims and plays, as on the flat screen, and the head looks about freely (above, "With a gamepad"). |
| `vr.gamepad_view` | `"camera"` | `HALO_VR_GAMEPAD_VIEW` | With `vr.aim = "gamepad"`: `"camera"`, the stick turns and pitches the view as the flat screen's camera, the head's turn on top (the most like the Xbox game, and the most likely to make you sick); `"level"`, it turns the view smoothly, and its pitch moves only the reticle; `"snap"`, the view turns a step of `vr.snap_turn_angle` each time the aim is more than that from it, and its pitch moves only the reticle. |
| `vr.dominant_hand` | `"right"` | `HALO_VR_DOMINANT_HAND` | The hand that holds the weapon: `"right"`, or `"left"`: the weapon in the left controller, drawn mirrored with both hands, the left trigger firing, the right hand on the foregrip, at the magazines (at the right hip) and throwing, the holsters on the left, the haptics swapped with the hands (above, "Left-handed"). Not with `vr.aim = "gamepad"`. |
| `vr.swap_sticks` | `"auto"` | `HALO_VR_SWAP_STICKS` | `"yes"`: the right stick moves (its click crouches) and the left turns (its click zooms, pushed down it ejects the magazine); `"no"`: as they are; `"auto"`: swapped with `vr.dominant_hand = "left"`. Not with `vr.aim = "gamepad"`. |
| `vr.weapon_offset` | `"0.15, -0.22, 0.30"` | `HALO_VR_WEAPON_OFFSET` | With `vr.hands = "game"`: where the weapon's grip is from the eye that the game poses it for, in metres: right, up, forward. |
| `vr.depth` | `false` | `HALO_VR_DEPTH` | `true`: give the runtime the eyes' depth too (`XR_KHR_composition_layer_depth`), for its reprojection. SteamVR on the Steam Frame could not make a depth swapchain for the OpenGL session the VR build had before it drew with Vulkan. |
| `vr.foveation` | `"eye"` | `HALO_VR_FOVEATION` | Foveated rendering (below, "How it operates"): each eye's image at its full resolution in an inset where you look, and at less about it, which draws half of the eyes' pixels (at the default `vr.foveation_inset`). `"eye"`: the inset follows the eyes, where the runtime tracks them (`XR_EXT_eye_gaze_interaction`; else as `"fixed"`); `"fixed"`: it stays in the middle of each eye's view; `"off"`: all of each image at full resolution. Not in VR Display; from the next start. |
| `vr.foveation_inset` | `0.5` | `HALO_VR_FOVEATION_INSET` | With `vr.foveation`: how much of each eye's image across and up is the inset (from `0.3` to `0.8`), at the image's resolution; about it, the image is drawn at this much of it (`0.5`: half across and up). Smaller is faster. From the next start. |
| `vr.late_latch` | `true` | `HALO_VR_LATE_LATCH` | The late latch (below, "How it operates"): each frame is drawn from where the head and the weapon's controller are just before it goes to the headset, located again by the renderer's thread, not where they were as the game began the frame; the game's work is the frame's as it was. `false`: as located when the game began it. |
| `vr.melee_gesture` | `true` | `HALO_VR_MELEE_GESTURE` | A swing of the right controller melees, as B does: a hard one in any direction, or a softer one with the right grip held ("Meleeing with a swing"). |
| `vr.holsters` | `true` | `HALO_VR_HOLSTERS` | With `vr.aim = "controller"`: the weapon you carry but do not hold is on your body, and the right hand reaching there and gripping draws it (above, "Holsters"). Y switches weapons as ever. |
| `vr.physical_crouch` | `true` | `HALO_VR_PHYSICAL_CROUCH` | The head crouching crouches the player: about 0.35 m below the standing eyes (`vr.height = "standing"`), or 0.25 m below the recentred ones (seated), and standing within 0.07 m of that stands (above, "Crouching"). The left stick's click crouches as ever. |
| `vr.scopes` | `true` | `HALO_VR_SCOPES` | With `vr.aim = "controller"`: the zoom magnifies a view of its own, as `vr.scope_view` says (above, "Scopes"). `false`: the game's zoom (nothing is magnified in the headset), whatever `vr.scope_view` is. |
| `vr.scope_view` | `"smartlink"` | `HALO_VR_SCOPE_VIEW` | With `vr.scopes`: `"smartlink"`, the right stick's click zooms as the game does, wherever the weapon is, and a wide magnified window opens about the reticle, at its depth (above, "Smart-link"); `"eye"`, the sniper rifle's and the pistol's scopes magnify the view within their lens while raised to an eye, and lowered do not zoom (above, "At the eye"); `"off"`, as `vr.scopes = false`. |
| `vr.smartlink_size` | `60.0` | `HALO_VR_SMARTLINK_SIZE` | With `vr.scope_view = "smartlink"`: how wide the zoom's window is, in degrees of the view (30 to 100); it is 16:9. The sniper rifle's and the pistol's are the flat screen's zoomed view: this is the sniper rifle's scope's frame in it, the pistol's to its scale, the zoomed view about them as wide as the HUD (`vr.hud_size`) or a fifth wider than the sniper rifle's frame. |
| `vr.smartlink_place` | `"reticle"` | `HALO_VR_SMARTLINK_PLACE` | With `vr.scope_view = "smartlink"`: where the zoom's window is: `"reticle"`, about the reticle at its depth, where the weapon aims (it moves with your hand); `"visor"`, on your visor where the HUD is (it stays before your eyes, as the flat screen does, and the weapon aims what it shows). Not in VR Controls. |
| `vr.scope_gun` | `"hidden"` | `HALO_VR_SCOPE_GUN` | With `vr.scope_view = "smartlink"`: how the weapon in your hand shows where it crosses the zoom's window: `"hidden"`, the window over it (no barrel in the view); `"ghost"`, faintly through the window (a fifth); `"visible"`, in front of the window. Not in VR Controls. |
| `vr.vehicle_climb` | `true` | `HALO_VR_VEHICLE_CLIMB` | With `vr.aim = "controller"`: a vehicle with holds (the Scorpion, the Warthog, the Ghost, the Banshee, the Shade) is climbed with the hands: grip a hold and pull, the view moving against the hands; lowered into a hatch by its rim, pulled to a seat by its holds, or with both hands on a gun's handles, you are in the seat, and pulled or pushed out of it by its holds (the Ghost: pushed off its handlebars), out ("Climbing into a vehicle"). X works as ever. |
| `vr.vehicle_zoom` | `"2 4"` | `HALO_VR_VEHICLE_ZOOM` | With `vr.scope_view = "smartlink"`, in a vehicle's seat whose weapon you aim, seen from the seat: the magnifications the right stick's click steps through in the zoom's window, then none (up to 4, each more than 1; `""`: the click does nothing there, as before). A vehicle's weapon with zoom levels of its own (none in the game's maps) has those. Not in VR Controls. |
| `vr.recoil` | `1.0` | `HALO_VR_RECOIL` | With `vr.aim = "controller"`: how far the weapon in your hand kicks as it fires, each its own way, from `0` (none) to `1` (above, "The weapons' feel"). Drawn alone: the aim, the shots and the scopes' views stay where you point. Not in VR Controls. |
| `vr.weapon_haptics` | `1.0` | `HALO_VR_WEAPON_HAPTICS` | With `vr.aim = "controller"`: how strongly the controllers feel each weapon firing, each its own way, and the hits on you, in the hand on the side they came from, in place of the gamepad's rumble for them, from `0` (none) to `1` (above, "The weapons' feel"). Not in VR Controls. |
| `vr.vehicle_flip` | `"lift"` | `HALO_VR_VEHICLE_FLIP` | How an overturned vehicle the game offers to flip is flipped (above, "Flipping a vehicle"): `"lift"`, by both hands gripping it low beside it and lifting together, fast (X reloads there); `"button"`, by X, as ever; `"both"`, by either. With `vr.aim = "gamepad"`, by X. Not in VR Controls. |
| `vr.grab_world` | `true` | `HALO_VR_GRAB_WORLD` | With `vr.aim = "controller"`: the hands take hold of the world (above, "Picking up a weapon by hand", "Pressing a control by hand"): a weapon on the ground the game offers is taken by reaching down to it with the right hand and gripping; a control the game offers to activate is pressed by pushing either hand into it. `false`: X alone, which works either way. Not in VR Controls. |
| `vr.manual_reload` | `true` | `HALO_VR_MANUAL_RELOAD` | With `vr.aim = "controller"`: reload by hand (above, "Reloading by hand"): the left hand takes another magazine from the left hip (the one in the weapon ejecting itself, or the right stick pushed down ejects it) and pushes it in, loaded at once (as a network game's client, after the game's reload time); the shotgun's shells one at a time; an empty weapon does not reload itself. X reloads as ever. |
| `vr.grenade_throw` | `"gesture"` | `HALO_VR_GRENADE_THROW` | With `vr.aim = "controller"`: `"gesture"`, a grenade is thrown by throwing it with the left hand alone (above, "Throwing a grenade with the left hand"; on foot the left trigger throws nothing); `"both"`, with the left hand or the left trigger; `"button"`, with the left trigger alone. |
| `vr.aim_assist` | `1.0` | `HALO_VR_AIM_ASSIST` | With `vr.aim = "controller"`: the aim assist (above, "Aiming with the controller"): near an enemy the game's magnetism would be drawn to, the aim is slow to leave it, and zoomed, is drawn gently toward it; how strongly, from `0` (none) to `2`. Zoomed, it reaches as far as the zoom's and is as wide as unzoomed (the flat game narrows it by the zoom's magnification, as its whole view is magnified; in the headset only a window is). The bullets' own autoaim is the game's whatever this is. With the left hand on a long gun's foregrip, half as strong again. The aim never moves while your hand is still but for that zoomed pull: away from a target it comes back to your hand's line only as your hand turns (not on its own as a reload, a melee or a kill loses the target), and a long frame (a hitch) does not reset it. |
| `vr.two_handed` | `true` | `HALO_VR_TWO_HANDED` | With `vr.aim = "controller"`: a long gun (any but the pistols, the needler, the flag and the ball) is aimed along the line from the right hand to the left while the left grip holds it by the foregrip (pressed within 18 cm of the line ahead of the right hand). The aim turns to that line over 0.15 s, smoothed, and back when the grip is let go. |
| `vr.hands` | `"arms"` | `HALO_VR_HANDS` | With `vr.aim = "controller"`: each first-person hand where its controller is (the right holding the weapon, its index finger on the trigger as far as the trigger is pulled; the left open, closing as the grip is pulled, and on a long gun's foregrip while it holds it). `"arms"`: the arms reach the hands from the shoulders (`vr.shoulders`). `"floating"`: no arms above the wrists. `"game"`: the game's arms, posed from the weapon in the right hand. A hand whose controller is not tracked is not drawn, nor its arm. In a cinematic seen from the player's head the hands are the controllers' too, acting on nothing (above, "Cinematics seen from your own head"). |
| `vr.shoulders` | `"0.19, -0.20, -0.07"` | `HALO_VR_SHOULDERS` | With `vr.hands = "arms"`: where each shoulder is from the eyes, in metres: out to its side, up, forward, turned with the body (the holsters' heading: it faces where the head does, follows it past a 40 degree turn, and slowly). From the eyes as they would be with the head level and turned with the body: the shoulders hang from the neck, so a nod or a turn of the head does not move them. |
| `vr.vehicle_view` | `"first_person"` | `HALO_VR_VEHICLE_VIEW` | `"first_person"`: in a vehicle's seat, the view is from the seated player's head, level with the horizon. `"third_person"`: the game's camera following the vehicle. |
| `vr.warthog_steering` | `"wheel"` | `HALO_VR_WARTHOG_STEERING` | With `vr.aim = "controller"`, in a Warthog's driver seat: `"wheel"`, grip its wheel and turn it to steer, the view turning with the Warthog ("Driving a Warthog by its wheel"); `"look"`, it steers where the right controller points, the view turned by the stick. |
| `vr.ghost_steering` | `"handlebars"` | `HALO_VR_GHOST_STEERING` | With `vr.aim = "controller"`, in a Ghost's driver seat: `"handlebars"`, grip its handles and turn them to steer, the view turning with the Ghost ("Driving a Ghost by its handlebars"); `"look"`, it steers and aims where the right controller points, the view turned by the stick. |
| `vr.scorpion_view` | `"feed"` | `HALO_VR_SCORPION_VIEW` | With `vr.vehicle_view = "first_person"`, in a Scorpion's driver seat: `"feed"`, you ride the body's climb in (and out), in the first person, into a closed cockpit inside the hull and see the world on a display all the way round you (true to size; the zoom within its front), the tank's hologram on the dash; `"hatch"`, you stand in the turret's hatch, the eye above the turret on its axis, seeing all about over the hull; `"inside"`, you sit in the driver's compartment at the front of the hull, seeing out through its vision blocks ("Driving a Scorpion"). |
| `vr.comfort_vignette` | `true` | `HALO_VR_COMFORT_VIGNETTE` | The edges of the view dim a little while the view turns fast with a Ghost steered by its handlebars, or it speeds up or slows hard. |
| `vr.seat_motion` | `"still"` | `HALO_VR_SEAT_MOTION` | With `vr.vehicle_view = "first_person"`: `"still"`, you stay in the first person all the way in and out: the eye follows the body's head as it climbs in and out, smoothed (no faster than 3 m/s), and once seated stays put in the vehicle, riding with it but not with a turret's turning or flinches, with no cut to black; `"blink"`, the eye stays where you stood as the body climbs in, and in the seat as it climbs out, and blinks (darkens and lightens in a quarter second) where it moves between places; `"head"`, it follows the seated body's head as the game animates it. |
| `vr.death_view` | `"crt"` | `HALO_VR_DEATH_VIEW` | As you die: `"crt"`, the helmet's view switches off as a CRT does and cuts to a death cam, a feed from beside your body (the right stick moves it about the body), until the game brings you back and the HUD boots (above, "Dying"); `"face"`, the view stays where your head was, the world dimmed and reddened, your body falling before and under you, and turns to the body in a blink if it lands out of sight, until the game brings you back out of the dark (above, "Dying"); `"still"`, the same, never turned; `"game"`, the game's camera pulling away from the body. |
| `vr.head_in_wall` | `"off"` | `HALO_VR_HEAD_IN_WALL` | Your head leaned or stepped into a wall or a pillar, or through it: `"off"` (the default), you see through it, as a black view is worse; `"fade"`, the view fades to black as your eyes go in (where the wall before them comes within 25 cm, black at 20 cm, just short of the game's near plane; black once they are in it or past it) and back as they come out, the HUD saying to step back, a scope showing nothing; not in a vehicle's seat (above, "Your head in a wall"). |
| `vr.turn` | `"snap"` | `HALO_VR_TURN` | `"snap"` or `"smooth"` (with `vr.aim = "gamepad"`, `vr.gamepad_view` instead). |
| `vr.snap_turn_angle` | `30.0` | `HALO_VR_SNAP_TURN_ANGLE` | Degrees of each snap turn. |
| `vr.smooth_turn_speed` | `150.0` | `HALO_VR_SMOOTH_TURN_SPEED` | Degrees each second of smooth turning. |
| `vr.height` | `"seated"` | `HALO_VR_HEIGHT` | `"seated"`: the head where it is at the recentring is the player's eye. `"standing"`: the head `vr.player_height` above the floor is. |
| `vr.player_height` | `1.65` | `HALO_VR_PLAYER_HEIGHT` | Standing, the height of your eyes, in metres. |
| `vr.world_scale` | `1.0` | `HALO_VR_WORLD_SCALE` | More than `1` makes the world look larger. |
| `vr.hud` | `"visor"` | `HALO_VR_HUD` | `"visor"`: the HUD on the helmet's visor (below, "The HUD on the visor"). `"flat"`: a flat screen in front of the eyes. |
| `vr.hud_distance`, `vr.hud_size` | `2.0`, `60.0` | `HALO_VR_HUD_DISTANCE`, `HALO_VR_HUD_SIZE` | The HUD's distance in front of the eyes (metres) and width (degrees of the view). On the visor, the radius of its curve and the angle that it spans. |
| `vr.hud_lag` | `0.03` | `HALO_VR_HUD_LAG` | With `vr.hud = "visor"`: how far the HUD trails the head's turns, as the time constant of its smoothing in seconds (up to `0.2`), never more than 1.5 degrees behind. `0`: the HUD turns with the head exactly. |
| `vr.hud_depth` | `1.0` | `HALO_VR_HUD_DEPTH` | With `vr.hud = "visor"`: how far apart in depth the HUD's parts are, times their own distances from the HUD's (the four below), from `0` (all at `vr.hud_distance`, one layer) to `2`. Never nearer than 1 m. |
| `vr.hud_depth_tracker`, `vr.hud_depth_status`, `vr.hud_depth_weapon`, `vr.hud_depth_world` | `-0.55`, `-0.35`, `-0.25`, `0.5` | `HALO_VR_HUD_DEPTH_TRACKER`, `HALO_VR_HUD_DEPTH_STATUS`, `HALO_VR_HUD_DEPTH_WEAPON`, `HALO_VR_HUD_DEPTH_WORLD` | The distances from the HUD's (metres, at `vr.hud_depth` 1; nearer less than 0) of the motion tracker; the shields and the health; the weapon, its ammunition and the grenades; and the waypoints off the screen, at its edge (those on it, the teammates' triangles and the players' names are drawn in the eyes, over what they mark: below, "How it operates"). At the defaults: 1.45, 1.65, 1.75 and 2.5 m, the rest of the HUD (the crosshair, the messages) 2 m. |
| `vr.hud_tracker_tilt` | `20.0` | `HALO_VR_HUD_TRACKER_TILT` | How far the motion tracker tilts back, in degrees (-45 to 45). |
| `vr.hud_wrap` | `1.0` | `HALO_VR_HUD_WRAP` | With `vr.hud = "visor"` and `vr.hud_depth` above `0`: the HUD's corners along the visor's curve, from `0` (where the game puts them, as before) to `2`. At `1` the weapon's panel (its ammunition and the grenades), the shields' and the motion tracker's turn out from the HUD's middle by 7 degrees across and 5 up or down, toward the edges of the view where a glance finds them, and bend as the visor's brow and chin do: falling away toward the sides above (rising, below), by a quarter of the square of their angle across (the shields' meter, from its inner end to its outer, about 3 degrees). The waypoints, the names and the HUD's middle stay where they are. |
| `vr.hud_glow` | `0.5` | `HALO_VR_HUD_GLOW` | With `vr.hud = "visor"`: the HUD's glow on the glass, from `0` (none) to `1`. |
| `vr.hud_glass` | `1.0` | `HALO_VR_HUD_GLASS` | With `vr.hud = "visor"`: the HUD as light on the visor's glass, from `0` (none: as before, an opaque screen) to `1`: it covers 40% less of the world behind it than its light (the world shows through it), takes a little of the glass's gold, blooms more widely about its glow in the gold, and the glass's other surface reflects it faintly, blurred, a little below. |
| `vr.hud_react` | `1.0` | `HALO_VR_HUD_REACT` | With `vr.hud = "visor"`: the HUD reacting to the world as a helmet's display would, from `0` (none, as before) to `1`. Before bright light (a sunlit beach behind the motion tracker) the glass darkens behind each element, softly about it, and the element brightens a little; in the dark it dims to 70%; both follow the light behind it over about a third of a second. With `vr.visor_effects`, a hit on the shields (or on the health, without them) disturbs its projection for a moment (bands of it shifted across by up to a quarter of a degree and dimmed, fading in about a tenth of a second), and its bloom pulses red with the low shields' warning. |
| `vr.helmet_rim` | `1.0` | `HALO_VR_HELMET_RIM` | With `vr.hud = "visor"`: the helmet's faceplate about the visor, from `0` (none) to `1`: its lip at the very edges of the view, in depth, the brow and the chin, never a frame. |
| `vr.helmet_rim_depth`, `vr.helmet_rim_reach` | `0.10`, `0.07` | `HALO_VR_HELMET_RIM_DEPTH`, `HALO_VR_HELMET_RIM_REACH` | How far before the eyes the rim's lip is (metres, `0.05` to `0.25`), and how far in from the edges of the view it begins (a fraction of them, `0` to `0.2`): at the defaults, about 2 degrees of each eye's view at its outer edge, its top and its bottom (the log says how much). |
| `vr.visor_frame` | `0.4` | `HALO_VR_VISOR_FRAME` | With `vr.hud = "visor"`: the glass: the helmet's shadow on it from the brow and the chin, faint at the sides, and its faint gold, from `0` (none) to `1`. |
| `vr.visor_effects` | `true` | `HALO_VR_VISOR_EFFECTS` | With `vr.hud = "visor"`: the shields on the glass: their energy across it (the armour's own, below, "The HUD on the visor") as they are hit and as they recharge, rising to their level and flashing as they are full; its rim pulses red while they are low or gone (or while the health is low without them), giving way to their energy as they recharge; the HUD flickers as they break, and comes up as you take control. |
| `vr.visor_energy` | `1.0` | `HALO_VR_VISOR_ENERGY` | With `vr.visor_effects`: how strong the shields' energy on the glass is, from `0` (none) to `2`. |
| `vr.menu_distance`, `vr.menu_width` | `2.5`, `2.6` | `HALO_VR_MENU_DISTANCE`, `HALO_VR_MENU_WIDTH` | The menus' distance and width, in metres. |
| `vr.hrtf` | `true` | `HALO_VR_HRTF` | Hear the world's sounds as your ears would, on headphones or the headset's speakers, from their direction about your head: the time and the shadow of your head between your ears, and the tones your ears give a sound from above, below and behind (a model of a head's HRTF, "Sound from your head"). `false`: panned left and right, as on the flat screen. |
| `debug.vr_force_render` | `false` | `HALO_VR_FORCE_RENDER` | Draw the frames while the runtime says not to (the headset not worn), to measure them. |
| `debug.vr_test_turn` | `0.0` | `HALO_VR_TEST_TURN` | Turn the player this many degrees a second, for automated tests without hands. |
| `debug.vr_test_jitter` | `0.0` | `HALO_VR_TEST_JITTER` | Move the head by up to this many millimetres (and turn it as many hundredths of a degree) at random each frame, as a worn headset moves: for automated tests of what flickers when the view barely moves. |
| `debug.vr_test_head` | `""` | `HALO_VR_TEST_HEAD` | Hold the head still and level where it is first located (a simulated headset's head wobbles), for automated tests: `"0"`, or `"<degrees> <seconds>"` turns it left and right by as many degrees over as many seconds, smoothly (a sine of the frames' display times); `"<degrees> <seconds> <pitch>"` also pitches it up by as many degrees (less than 0: down), as looking down at the hip's holster; `"<degrees> <seconds> <pitch> <roll>"` also rolls it by as many degrees to the right (the right ear down). |
| `debug.vr_test_hands` | `""` | `HALO_VR_TEST_HANDS` | Hold the controllers still, for automated tests without hands: `"lx ly lz lyaw lpitch lroll, rx ry rz ryaw rpitch rroll[, grip[, buttons[, trigger[, head[, frames[, move[, down[, nod[, forward[, right[, stick x[, stick y[, right grip]]]]]]]]]]]]]"`, the left and right hands in metres right, up and forward from the recentred head and degrees of yaw (left), pitch (up) and roll (right) (a hand 10 metres or more out is not tracked), the left grip's pull (0 to 1), the buttons pressed for the pose's first 20 frames (A 1, B 2, X 4, Y 8, the right stick's click (zoom) 2048, the right trigger 65536, the left 131072, the right grip 262144, the right stick pushed down 524288), the right trigger's pull (0 to 1; less than 0, the index finger off the trigger, as when its touch senses none: "The trigger finger"), the head turned (degrees left), the frames the pose lasts (`0`: 288), the frames the hands take to move to it from the pose before, a step a frame (`0`: none; a swing, as fast as its steps), the head lowered (metres, as a crouch; moved with the hands: below), nodded (degrees up, about the neck 8 cm behind and 15 cm below the eyes, as a person's lower neck: looking 60 degrees down, the eyes come 9 cm forward and 14 cm down) and moved in the room (metres forward and right, as a lean or a step from where the head was recentred), each with the hands, and the left stick (right and forward, -1 to 1: driving) and the right grip's pull (0 to 1: a vehicle's handlebars) through the pose. A pose shorter than 20 frames presses its buttons only as long. Up to 512 poses separated by `;` are taken in turn (a pose a frame, of 1 frame moved in 1, poses a path frame by frame: a swing's curve) (the log says from which frame). With the hands posed, a cross marks each controller's grip and a line where it points, and white lines the arms' bones. With `vr.dominant_hand = "left"` the poses are a left-hander's, mirrored: the right hand is the weapon's, held by the left controller where the mirror puts it (right and left, yaw and roll, the head's turn and its move across, all the other way), the left hand the right controller, and the trigger, the grips, the stick and the stick's clicks are those of the controllers and sticks that do what they name (the left trigger, the turn's stick's click on the left stick), so a test of the right hand plays as a left-hander's. Below, a grenade throw scripted. |
| `debug.vr_test_death` | `""` | `HALO_VR_TEST_DEATH` | Kill the first player, for automated tests of the view as they die: `"<seconds> [kill\|grenade\|watch]"`, that many seconds after they first aim (a level's clock does not start at 0), once a run, as the script's `unit_kill` does, by a frag grenade's blast half a metre before them (their shields and nearly all their health gone first: the body thrown back), or killed and then the dead camera turned to the nearest living biped, as network co-op's spectating turns it to a teammate. From a second before, the log says how far the view moved and turned each frame, how tinted, dimmed and dark it was, and where the body was from the eye (how far, across and up, in view or not), and each phase's sums (alive, dead, back) with how much of the motion was seen (between frames not in full darkness), and each blink; with `debug.screenshot_directory`, the eyes six times a second from the death to 1.5 s after the player is back (24 times in its first half second). With `debug.vr_test_head = "0"`, the motion the player did not make. |
| `debug.vr_test_grenades` | `""` | `HALO_VR_TEST_GRENADES` | Give the first VR player these grenades as they first aim, for tests of the grenade held in the hand: `"<frags> <plasma grenades>"`. |
| `debug.vr_test_finger` | `""` | `HALO_VR_TEST_FINGER` | The right index finger on the weapon's trigger instead of the weapon's own (`game/vr_hands.c`, `trigger_fingers`), for tests: `"<rest> <rest> <rest> <pull> <pull> <pull> [<travel> [<off> <off> <off>]]"`, its curl at each joint from the knuckle at rest, from the idle animation's (less than 0: straighter), and how much more at a full pull (radians), how far back the trigger goes (metres), and its curl off the trigger (the trigger's touch sensing no finger; a test pose's trigger less than 0). |
| `debug.vr_test_flip` | `""` | `HALO_VR_TEST_FLIP` | Overturn a vehicle before the first player, once a map, 3 s after the player first aims, the level's AI erased (once a second), for automated tests of the lift (`vr.vehicle_flip`): `"<name> [roof\|side\|upright] [<metres ahead>]"`, a part of the vehicle's tag name (`"warthog"`; b30 has the Warthog, b40 the Ghost and the Scorpion too), on its roof (by default) or its left side, or upright (to enter it), broadside, its middle as far ahead as puts it within reach (by default). The log says where, how it lies each second and whether the player is in it (`vr flip: test vehicle's up`), each offer, and each step of the lift (`vr: flip:`). |
| `debug.vr_test_grab` | `""` | `HALO_VR_TEST_GRAB` | Lay weapons on the ground before the first player, once a map, 3 s after the player first aims, the level's AI erased (once a second), for automated tests of the grab (`vr.grab_world`): `"weapon <name>[ @ <metres ahead> <metres right>][; weapon ...]"`, up to two, a part of each weapon's tag name (`"plasma rifle"`; b30 has the needler and the plasma pistol too), 0.5 m ahead and 0.15 m right by default, across the player's way. Or `"control [<n> [<metres>]]"`: the map's controls listed, and the player stood before the nth nearest they could use in this part of the map (the nearest by default; c10 starts beside a holo panel, a10 and b30's beach have none), facing it, 0.6 m from its face by default; as a network game's host, flat (`debug.network_test`, co-op), the last player is stood there, and their machine, with the same setting, turns them to it. The log says, as they change, where each is from the head, what the game offers, how near the right hand is, a control's state (on, off), and what the player carries (`vr grab:`), and each grip and press (`vr: grab:`). |
| `debug.vr_test_weapons` | `""` | `HALO_VR_TEST_WEAPONS` | Give the first player these weapons in place of their own, the first in hand, once a map, 2 s after the player first aims (a level's script gives its own as the player takes control), for automated tests of the scopes: `"<name>[, <name>]"`, parts of the weapons' tag names (`"sniper rifle, pistol"`; b30 has no sniper rifle, a50 has both). |
| `debug.vr_test_target` | `""` | `HALO_VR_TEST_TARGET` | Place a still biped before the first VR player, for automated tests of the aim: `"<metres ahead> [<metres right> [<name> [free\|head]]][; <vehicle>]"`, a biped whose tag's name has this in it (`"grunt"` if none), 3 s after they first aim on foot standing still (a level's drop moves them), on the ground; after a `;`, in a seat of a vehicle whose tag's name has that in it, placed there (`"c gun turret"`: the Shade, on b40, c10 and d20; `"ghost"`, `"banshee"`), or with the name `-`, the vehicle alone, empty; turned left or right as little as puts it in sight, with no mind of its own (the level's AI erased, so that only the player's shots hit it), and again 2 s after it dies. The right controller held by `debug.vr_test_hands` points at its middle (`head`: its head; `free`: where `debug.vr_test_hands` points it), and each hit on it is logged (`vr aim: test target`). |
| `debug.vr_test_shake` | `""` | `HALO_VR_TEST_SHAKE` | Shake the controllers as a hand does, for automated tests of the aim steadied while zoomed: `"<degrees> [<millimetres>]"`, the right controller turned at random by about this much (RMS: half a slow wander, smoothed noise below 1.5 Hz, half a 9 Hz tremor), both moved by this much. |
| `debug.vr_aim_log` | `false` | `HALO_VR_AIM_LOG` | Log the controller's aim once a tick: where its line meets the world (and what: the structure, or the object's tag), how far the game's aim was turned to meet it there from the game's eye, how far from that point the unit's aim passes, whether the reticle is red, and the aim assist's target's magnetism level and how many of 9 points of its body are in sight from the controller (with `debug.vr_test_target`, how far the target's middle is from both, and its head from the game's aim); with `debug.vr_test_hands`, each frame, the controller's direction and the aim's, steadied (`vr aim steady`). |
| `debug.vr_throw_log` | `false` | `HALO_VR_THROW_LOG` | Log each step of a grenade thrown with the left hand (`vr.grenade_throw`): the grip taken (or why it holds nothing), the swing (or why a fast hand was none), the release's speed, power and direction, and the game's throw: when it began, its animations held or moved on to their release, and the grenade's place and speed. |
| `debug.vr_audio_log` | `false` | `HALO_VR_AUDIO_LOG` | Log, each frame, where the sounds were heard from (the listener: its yaw, pitch, roll and place) against the head the frame was drawn from, and the game's camera ("Sound from your head"). |
| `debug.vr_test_sound` | `""` | `HALO_VR_TEST_SOUND` | Play a sound of the map's at a point held in the world, for automated tests of the listener: `"<text> <seconds>[ <distance> <degrees>]"`, the first sound whose tag name has the text in it, again every so many seconds, so many world units (2) from the player's eye, turned so many degrees left of the view's heading (0: ahead); placed again if the player is carried a tenth of a unit away (a level's ride in). |
| `debug.vr_melee_log` | `false` | `HALO_VR_MELEE_LOG` | Log the right hand's swings that did not melee (`vr.melee_gesture`) too: how fast and how far each went, and why it was none (too slow, a holster, a throw, a reload, the tracking jumping). Each melee by a swing is logged whatever this is. |
| `debug.vr_test_weapon` | `""` | `HALO_VR_TEST_WEAPON` | Put a weapon in the first player's hand, for automated tests of the reload by hand: `"<text> <seconds>"`, that many seconds into the level the player's weapons are taken and a new one is in hand, the first of the map's whose tag name has the text in it (`"shotgun 15"` on d20, `"rocket 15"` on b30). |
| `debug.vr_test_seat` | `""` | `HALO_VR_TEST_SEAT` | Get into a vehicle's seat and out again, for automated tests of the view in a seat: `"<label> <seconds> [<seconds seated>]"`, the first player gets into the seat whose label has this text in it (`"gunner"`, `"W-driver"`, `"GT-gunner"`: the Shade) that many seconds into the level, as the action button does, in a vehicle of the kind made before them three seconds earlier (one found in the level, else of a kind the map has a tag for: `"G-driver"` makes a Ghost on b30, whose Ghosts come later; standing at the seat's entrance from two seconds earlier), and out again after as many seconds seated (10 by default). From a second before, the log says how far the view moved and turned each frame (and its heading and roll, the vehicle's heading, pitch and roll, the comfort vignette and the frame's time: degrees, seconds), how dark a blink made it, and each phase's sums (on foot, entering, seated, exiting) with how much of the motion was seen (not in a blink's full darkness), and each blink: with `debug.vr_test_head = "0"`, the motion the player did not make. |
| `debug.vr_test_head_wall` | `""` | `HALO_VR_TEST_HEAD_WALL` | Put the first player's eye at `"<x> <y> <z>"` (world units) once they have been on foot 2 s, once a run, for automated tests of the head in a wall (`vr.head_in_wall`): `debug.vr_test_hands` then leans the head into the world from a known place, and logs, as the head moves and the fade changes, where the game's eye is and where the view looks, how far the head is from it, how far past the world it is, how near the world is ahead (within the near plane and 6 cm), and how dark the view is to be and is; and once the unit's collision radius and the near plane. |
| `debug.vr_test_fov` | `""` | `HALO_VR_TEST_FOV` | Each eye's field of view instead of the runtime's, for automated tests of what the edges of a wide view show: `"<outward> <inward> <up> <down>[ <cant>]"`, in degrees from the eye's axis, each eye turned outward by `<cant>` degrees (as canted displays are; the game's 3D view assumes the eyes look the same way, so only the visor and the helmet's rim are right with it). |
| `debug.vr_test_gaze` | `""` | `HALO_VR_TEST_GAZE` | The eyes' gaze instead of the runtime's, for automated tests of the foveation (`vr.foveation = "eye"`): `"<right> <up>[ <seconds>]"`, in degrees from the head's forward; with `<seconds>`, it jumps to the opposite and back every so many seconds, as the eyes do. |
| `debug.vr_foveation_show` | `false` | `HALO_VR_FOVEATION_SHOW` | With `vr.foveation`: tint red what is drawn at the periphery's resolution (fully about the inset, partly in its blend), to see the inset. |
| `debug.vr_gpu_time` | `false` | `HALO_VR_GPU_TIME` | With `debug.gpu_stats`, log elapsed render GPU timelines from the first render commands after the runtime's frame wait through composition, read without waiting (every 300 frames, `vr: elapsed render GPU timeline per frame`). This includes GPU stalls and any gaps in command submission, not only GPU busy time (`tools/gpu_busy.py` reads that from the kernel). Unavailable when the graphics queue has no timestamp support. The guest reads it and hands it to the host's renderer with VR's start, so config.toml's works as well as the variable. |

`debug.test_input = "pad"` (`HALO_TEST_INPUT=pad`) puts the gamepad mode
through its paces without a gamepad: it turns, looks up and down, fires,
throws a grenade, holds Back (recentring) and walks, a few seconds each in
turn, and logs each.

A grenade thrown with the left hand, scripted (b30, whose first throw the
game allows about 45 s in at 72 Hz), the right hand holding the weapon
ahead (`R` here stands for `0.2 -0.3 0.4 0 0 0`), the head still
(`HALO_VR_TEST_HEAD=0`), with `HALO_VR_THROW_LOG=true`:

```
HALO_VR_TEST_HANDS="-0.25 -0.45 0.15 0 0 0, R, 0, 0, 0, 0, 3200, 0;
  -0.20 0.05 -0.10 0 30 0, R, 1, 0, 0, 0, 90, 36;
  -0.15 0.0 0.45 0 0 0, R, 1, 0, 0, 0, 8, 8;
  -0.12 -0.1 0.6 0 -20 0, R, 0, 0, 0, 0, 400, 6"
```

(on one line): the left hand low for 3200 frames, then taking a grenade
and drawn back beside the head over half a second, swung 0.55 m forward
in 8 frames (5 m/s) and let go of, which logs the swing, the game's throw
beginning, its animation moved on to the release, and the grenade thrown
from the hand.

A crouch scripted (seated, `R` and `L` the hands at their places, `R4` and
`L4` 0.4 m lower), with the head still:

```
HALO_VR_TEST_HANDS="L, R, 0, 0, 0, 0, 1200, 0, 0;
  L4, R4, 0, 0, 0, 0, 576, 72, 0.40;
  L, R, 0, 0, 0, 0, 576, 72, 0"
```

stands, crouches 0.4 m over 72 frames, holds it, and stands up again: the
log says when the crouch is on and off (`vr: crouch:`) and, once a tick
while the crouch or the head moves, how high the view is above the
player's feet: the game's eye, what of its drop is taken back out, and the
head's own (`vr crouch:`).

A reload by hand, scripted (b30, the assault rifle; `R` as above, the head
pitched down, `HALO_VR_TEST_HEAD="0 1 -35"`):

```
HALO_VR_TEST_HANDS="-0.25 -0.45 0.15 0 0 0, R, 0, 0, 0, 0, 1300, 0;
  -0.25 -0.45 0.15 0 0 0, R, 0, 65536, 1, 0, 60, 0;
  -0.25 -0.45 0.15 0 0 0, R, 0, 524288, 0, 0, 60, 0;
  -0.22 -0.62 0.06 0 0 0, R, 0, 0, 0, 0, 40, 15;
  -0.22 -0.62 0.06 0 0 0, R, 1, 0, 0, 0, 40, 0;
  0.10 -0.60 0.22 0 0 0, R, 1, 0, 0, 0, 100, 20;
  0.16 -0.43 0.27 0 0 0, R, 1, 0, 0, 0, 200, 12;
  -0.25 -0.45 0.15 0 0 0, R, 0, 0, 0, 0, 400, 20"
```

(on one line): the right trigger fires a burst, the right stick ejects,
the left hand goes to the left hip and takes a magazine, comes under the
rifle and pushes it home behind the grip. The log says each step (`vr:
reload:` and `vr reload:`): where the magazine goes, from the recentred
head as the hands are placed here, how far off it was pushed home, the tick
it was pushed home and the tick it was loaded (the same), with the rounds
loaded and carried before and after, and the tick of the first trigger pull
and shot after it. With `HALO_SCREENSHOT_DIR` (and no
`HALO_SCREENSHOT_EVERY`), the frames as it is loaded and every 12 frames for
half a second after are written (the HUD's count).

`debug.gpu_stats` also logs the VR frames' timing, and
`debug.screenshot_every` writes both eyes (`eyes*.bmp`), the HUD
(`hud*.bmp`) and both eyes with the layers over them, as the headset shows
them (`view*.bmp`), and while a scope's view is shown (raised, or smart-linked)
its image (`scope*.bmp`).

The GPU's time is best read from the kernel: the process's
`drm-engine-gpu` time in `/proc/<pid>/fdinfo`, which `tools/gpu_busy.py`
prints each second as the GPU's busy share and, with `--hz`, as milliseconds
a frame (run it on the headset over ssh while the game plays: it needs no
setting, and costs the game nothing). The figures below were
measured with the OpenGL renderer the VR build had before it drew with
Vulkan (on the Steam Frame, Mesa's Zink on Turnip). On the Steam Frame (b30's
first outdoor area, SteamVR holding 72 Hz with the headset idle, the GPU at
its 903 MHz) the game keeps the GPU busy about 8 ms a frame at
`vr.resolution_scale = 1.0` (1728x1728 eyes), about 13 ms at 1.25 (on the
edge of 72 Hz), and at 1.5 the runtime drops to 36 Hz. At 90 Hz (11.1 ms)
the default scale leaves about 3 ms, so there is no anti-aliasing:
supersampling costs as above, and multisampling the eye pass
(`GL_OVR_multiview_multisampled_render_to_texture`, which Zink offers) cost
15 ms a frame at 2 samples, and drew the multi-pass shaders wrongly.
So the eyes' alpha-tested surfaces (b30's foliage, a10's grates and
fences, decals with holes) are tested once a pixel, and crawl as the head
moves: multisampling's coverage of their samples by alpha ("Anti-aliasing"
in "What operates"; alpha to coverage) needs multisampled eyes.
Foveated (`vr.foveation`, the default; below), half of the eyes' pixels
are drawn. On a DGX Spark (b30's first area, the game frozen, the GPU's
time from the eye pass to the present by a timer query, the least of
300-frame spans: the GPU was shared) the eye pass went from 11.6 to 7.8 ms
a frame with 3456x3456 eyes, and from 5.6 to 4.7 ms with 1728x1728 eyes,
where NVIDIA's GPU spends less of its time on pixels than the Steam Frame's
does; making the eye images from the four layers cost 0.2 ms more there.

### How it operates

- The host opens the OpenXR session with the Vulkan device of its
  renderer (`XR_KHR_vulkan_enable2`; `host/host_vr.c`), which the runtime
  chooses; a runtime without that binding gets no session, which the host
  finds before the window opens (`host_vr_probe`; "SteamVR on a desktop
  PC", above). The swapchains' Vulkan images stay on the host; the guest
  gets the views and the controllers through `src/vr_host.h`, whose
  structures hold only 32-bit values.
- The game draws its 3D view once a frame, from the centre of the head,
  with a field of view that covers both eyes (`game/vr_render.c`): the
  culling and the game's work are those of one view. That view has the
  game's 480-line viewport, so the size of an object on the screen, which
  picks its model's detail level, its lighting's refresh and its shadow, is
  scaled to an eye's lines over the eye's field of view (about 3.6 times,
  at 1728 lines an eye). The renderer draws
  each draw into both eyes at once with Vulkan's multiview, into the eyes'
  image, an image of two layers, which is copied into the runtime's
  swapchain image as the frame ends (`vulkan/host_vulkan_vr.c`). Each vertex that the
  game projected through clip space moves to its eye by a 4x4 for each
  eye, which `game/vr_render.c` makes each time the game sets a projection
  (`rasterizer_set_frustum_z`). The eyes are beside the centre, looking
  the same way, so only x and y move: each eye's depth is exactly the
  centre's. (Made through the inverse of the centre's projection, the depth
  lost its precision, differently each frame: some frames all beyond about
  100 world units went past the far plane, the water's edge flickered on
  b30's horizon, and, depth-clamped, a10's Pillar of Autumn showed its far
  side over its near hull.) The screen's other render targets have a
  layer for each eye. Draws in screen space are the same in both eyes.
- The late latch (`vr.late_latch`, the default): the game locates the
  head as it begins to draw a frame, and the renderer's thread on the host
  plays the frame a few milliseconds later. As it begins the eye pass it
  locates the head and the controllers again, at the frame's display time
  (`host_vr_locate_late`), and moves what the frame draws by how far they
  have moved since (`host/host_vr_stream.c`): each eye's 4x4 for each
  projection (the world's with the head; the sky's, drawn about the eye,
  with its turn alone; the first-person weapon's and the hands' with the
  head and the weapon's controller), as the view would have been made from
  there, and the layers' poses submitted with the eyes. The game's work
  (its culling, a little wider for it, its aim, sounds and HUD) is the
  frame's as located; the picture is the newest. The left hand is in the
  same model as the right, so it moves with the weapon's controller too
  until the next frame. Not with `debug.vr_test_head` (its head is not the
  runtime's) nor the right hand with `debug.vr_test_hands`. With
  `debug.gpu_stats` the host logs the time from the poses' locating to the
  frame's submission, as the game located them and as latched.
- Foveated (`vr.foveation`, the default), the eye pass draws four views
  in the same draws (multiview with four views, where the device has them
  and fragment storage-buffer atomics; `vulkan/host_vulkan_vr.c`),
  each `vr.foveation_inset` of an eye's image across and up (864x864 of
  1728x1728 by default): for each eye the whole of it, at that fraction of
  its resolution, and an inset of it, at its full resolution, about where
  the eyes look. Each eye's vertices move to its two views as they moved to
  its one, the inset's magnified about its place, so the game's work and
  its draws are as before; half of the eyes' pixels are drawn (as many as
  one eye's), and the GPU drops what is outside an inset before it shades
  it. The screen's targets have four layers, each layer drawn as its view
  is, and sampled at the point that the layer has (where an inset does not
  reach, as at the edge of a blur, from the whole eye's layer). The present
  makes each eye's image from its two layers (`vr_fovea_composite`): the
  whole eye's magnified (bilinear), and over it the inset, on the image's
  own pixels (its place snaps to whole pixels, so that as it moves, nothing
  in it crawls), blended in over 15% of its side from its edges (none at
  the image's own edges), the blend's corners round and its ends smoothed,
  so that no edge shows where the detail changes; the eyes' tint, the
  helmet's rim and the visor's glass are drawn after, into the images, at
  their full resolution. The inset follows the gaze
  (`XR_EXT_eye_gaze_interaction`, in the head's space, projected into each
  eye's view as its image was drawn: both eyes' insets on the same place in
  the world), smoothed: after a saccade (a jump of 3 degrees or more) over
  12 ms, otherwise over 80 ms, so that the eyes' drift and the tracker's
  noise do not move it; lost (a blink), it stays 0.3 s, then returns to the
  middle over 0.5 s. Without eye tracking it is in the middle of each eye's
  view (where the lens's axis is). The visibility tests (lens flares) count
  the whole eyes' layers alone, as before, so a flare is as bright in the
  inset as out of it; the eyes' depth is not submitted (`vr.depth`) while
  foveated. Neither the GPU's fragment density maps nor OpenXR's own
  foveation (`XR_FB_foveation`, `XR_META_foveation_eye_tracked`, which
  Valve lists for the Frame) is used ([the Vulkan README](vulkan/README.md)):
  the views are what draws the periphery at less. The log says what the runtime offers
  (`vr: the runtime's extensions`), what the driver has (`vr: the driver's
  shading rate and foveation extensions`), the foveation (`vr: foveation
  by ...`), and when the eyes are first tracked (`vr: the eyes' gaze is
  tracked`).
- The HUD and the menus are drawn into a target of their own, which is
  copied each frame into a layer: in front of the head in a game, in
  front of the place of the last recentring in the menus. The HUD's camera
  has the field of view of the layer, so the waypoints are in the correct
  place.
- The HUD on the visor (`vr.hud = "visor"`, `src/vr_visor.c`) is curved
  about the eyes: the game's 640x480 HUD is laid out in angles, across
  and up, so that each element is as large and as square at the edges as
  in the middle, and all of it at one distance. Where the runtime has
  cylinder layers (`XR_KHR_composition_layer_cylinder`, as Monado does) it
  is on a cylinder about the head; else (SteamVR 2.18 on the Steam Frame)
  on a quad whose image the copy draws curved, which from the eyes looks
  the same, its edges only a little farther (the log says which).
  `HALO_VR_NO_CYLINDER=1` does without cylinders, to test that. What the
  HUD projects from the world (the waypoints, the names, the reticle where
  the head aims) goes through the same curve
  (`vr_render_hud_to_screen`). The HUD's orientation trails the head's
  (`vr.hud_lag`): smoothed toward it, without a spring, so that it never
  overshoots and is still while the head is, and held within 1.5 degrees;
  its camera looks where the layer does, so the waypoints stay on what they
  mark, and the reticle where the head aims. The HUD's parts have depths
  of their own (`vr.hud_depth`): its draws are grouped by the anchor the
  game places each element by (`hud_draw.c`; `hud.c` holds the waypoints
  at the HUD's edge, and the messages, to groups of their own), each
  group but the middle's drawn into a target of its own and shown on a
  quad of its own (four more layers, `VR_SWAPCHAIN_PANEL`), its region of
  the HUD curved in its image as the HUD is, at its own distance, placed
  with the HUD's layer: from the head, each element is where it was, only
  nearer or farther, and the waypoints stay on what they mark. The
  corners' panels follow the visor's curve (`vr.hud_wrap`): each is turned
  out from the HUD's middle toward its own corner, and its region bent
  (its angle up less by its bend times the square of its angle across, as
  the brow falls away), the panel's quad placed and sized for the turned
  and bent region and the copy undoing both for each pixel, so that its
  elements keep their sizes and only move and curve. Quads,
  which every runtime has; the stereo depth is the compositor's, and the
  HUD's pixels never enter the eyes' images, whose depth is the world's.
  It stays in the compositor's layers, not drawn on the glass in the eyes'
  images, though it looks to be on the glass (`vr.hud_wrap`, `vr.hud_glass`,
  `vr.hud_react`): a layer is sampled once, at its own resolution (its text
  and the motion tracker sharp), and reprojected on its own, where in the
  eyes' images it would be resampled at theirs, reprojected with the world.
  And its depth is a comfortable one: at the glass's 0.105 m the eyes would
  have to cross to about 34 degrees and could not focus there (the
  headset's lenses focus at about arm's length or more), so it is shown
  as a real helmet's display is collimated, far beyond the glass: from 1.45
  m (the motion tracker) to 2.5 m (the waypoints), where the eyes fuse it
  without effort and it never doubles. Its layers are no larger than they
  were, none of them near (SteamVR drew a line across a near, wide quad:
  below). The look on the glass is the copy's (`vr_copy_hud_image`): its
  alpha less than its light, the glass's gold in it and its bloom, its
  faint reflection (a blurred copy 18 of its pixels below, at a tenth of
  its light). Its reaction to the world reads the eyes' images as drawn:
  a map of the world's light, 64 by 52, in tangents from the middle of the
  head (`vr_light_map`, 16 samples of both eyes for each of its pixels,
  linear), into which each frame's light is blended over 0.35 s; the copy
  looks up the light behind each of its pixels and, where it is bright,
  adds cover (dark) about the elements and brightens them, and where it is
  dark dims them. On a DGX Spark the copy of the HUD and its panels went
  from 0.10 to 0.19 ms of the GPU's a frame with all three on.
  The visor's glass is 0.105 m before the head, on the head exactly
  (the helmet does not lag): the helmet's shadow on it from the brow and
  the chin, the glass a faint gold, and the shields on it, from the
  player's unit as the game has it. It is drawn into the eyes' images, as
  the rim is (`vr_draw_visor`, multiview), each pixel where that eye's
  ray meets the glass, blended linearly as the compositor would, not a
  layer of its own: as a quad layer that
  near and that wide (it reached 80 degrees from the head's forward), the
  Steam Frame showed a straight diagonal line across it, in a different
  place in each eye, which made it hard to look at; SteamVR draws such a
  quad wrongly. Its pattern never ends in an edge: it reaches past every
  corner of both eyes' views as the runtime gives them (`xrLocateViews`:
  their fields of view and their turns, on any headset), and fades to
  nothing beyond them. It is under the HUD's layers now (they are the
  compositor's, over the eyes' images), which it no longer shades. Their
  energy is the armour's own: the unit's modifier shader (the Chief's
  `characters\cyborg\shaders\shield hit`, a plasma), its two noise maps
  (`characters\elite\bitmaps\plasma shield noise`) moving through the
  glass at their scales, directions and periods, combined as its pixel
  shader combines them (`rasterizer_xbox_plasma_energy.c`), coloured amber
  facing and blue grazing as its colours are, and as bright as the game
  makes the armour glow (the unit's function the plasma's intensity comes
  from: the Chief's 'shield glow source', from the shields' recent damage).
  As the shields recharge, it fills the glass from below to their level,
  brightest at its front, and flashes as they are full (the Chief's
  collision model has no recharging effect of its own, and its depleted
  effect is particles: no flash of the screen's to move onto the glass).
  More at the glass's edges than in its middle. The red warning stays
  toward the rim; the HUD's flicker as the shields break is three dips in
  under half a second. The helmet's rim (`vr.helmet_rim`) is drawn into
  the eyes' images (`vr_draw_rim`, multiview), before the head as the
  faceplate is: the visor's opening, wide and low, its lip just in from the
  edges of each eye's own view, its bevel and frame curving back toward the
  face, only its brow and chin to see (the sides barely, the corners not at
  all), fading in smoothly from its lip and out again beyond the view,
  with no highlight along it (no line, no edge); each eye sees its own
  side, both the brow and the chin, in depth. It is sized by the runtime's fields of view,
  so it stays at their edges on any headset. Other effects on the glass
  (water, dirt, cracks, reflections) would be drawn with it
  (`vr_draw_visor`). The copy adds the HUD's glow, a soft ring of its own
  light about each element.
  `debug.screenshot_every` also writes what the headset shows
  (`view*.bmp`): the layers drawn over each eye's image as the compositor
  would place them. With `vr.aim = "controller"` or `"gamepad"` the reticle is not in
  the layer but in the eyes (`vr_render_crosshairs`): drawn at the end of
  the eye pass where the aim meets the world (a line-of-sight test, its
  distance smoothed), in each eye by a scale and offset from the middle of
  the screen where the game draws it, as large as in the layer, each eye
  in a draw of its own (`halo_vr_draw_eye`). In the layer, in front of the
  head, it was placed for the head's pose as the frame was drawn, which
  the compositor showed it with at the pose of the display while it moved
  the eyes' images to that: it swam against the world as the head moved,
  stepped by the layer's pixels, and at the layer's 2 metres the eyes saw
  it doubled against what it aimed at. So are the marks the HUD puts over
  the world (`vr_render_world_marks`): the triangles over the player's
  teammates, the players' names above their heads and the waypoints on the
  screen (the campaign's, a multiplayer game's flags, ball and hill), each
  drawn at the end of the eye pass in each eye over what it marks, at its
  depth, as large as in the layer; the game projects each and draws it as
  ever, and a screen correction for each mark moves it there in each eye
  (`vr_render_world_mark`), in a draw for each eye (drawn in both at once,
  the right eye's triangles lost their tops). In the layer (the world's
  panel, 2.5 m before the head at the defaults) a teammate's triangle
  flickered over them in a network game: at the layer's distance, the eyes
  saw it doubled against a teammate 15 m away (about a degree and a quarter
  apart), it trailed the head's turns with the layer and stepped by the
  HUD's pixels while the teammate moved smoothly, and the glass's
  reflection and the hits' bands (`vr.hud_glass`, `vr.hud_react`) touched
  it. A waypoint off the screen stays at the HUD's edge, in its layer; in a
  frame without the eyes all are in the layer, as before. The eye pass
  draws its text (the names, the waypoints' distances) rasterized for the
  HUD's pixels (`halo_screen_pixel_scale`), as large as it is drawn there,
  so the glyphs are not made again for each pass.
- The runtime paces the game (`xrWaitFrame`). A frame ends when the next
  starts, because SteamVR's `xrEndFrame` waits for the GPU. The views are
  located again just before the frame's draws. The game's ticks are
  interpolated, as on the flat screen. The frame is begun before the game
  reads its clock, and the game's time moves on by the time between the
  frames' predicted displays, so that the frames are as evenly apart in
  the game as the headset shows them (read when the last frame's work
  ended, they were 9 to 19 ms apart at 72 Hz, and what moved fast
  juddered).
- The view does not shake. The game shakes and knocks a player's camera
  (firing, explosions, hits, a level's scripted quake:
  `player_effect_get_camera_effect_matrix`), and the eyes were placed from
  that camera: the view moved by up to a few centimetres that the head did
  not move, which sickens. In VR the effect is still taken (its timers run,
  and the gamepad's rumble with them) but does not move the camera
  (`main.c`, `set_window_camera_values`). The hits are felt in the
  controllers (below), and the screen's flashes are drawn.
- The player's facing follows the right controller (or the head). Thus
  the netcode sees a normal player's aim. The first-person weapon is
  posed from a camera behind the controller, and drawn in both eyes as
  the world is. With both hands on a long gun, the facing and the weapon
  follow the line between the hands instead: still only an aim for the
  netcode.
- The game fires from its own eye (`unit_adjust_projectile_ray`: the
  unit's camera) and judges its red reticle and its bullets' autoaim from
  there along the facing (`aim_assist.c`). Given the controller's
  direction alone, its line was the controller's moved to its eye,
  parallel, 30 to 50 cm away (the weapon held low and to the right; more as
  the head leans or walks in the room away from the eye): with the reticle
  on a grunt's middle the shot went over its head, saved only where the
  bullets' autoaim reached: with the hand 0.9 m from the eye, a grunt 6 m
  away was red 22 ticks of 664 and was killed twice in five bursts of the
  rifle; the sniper rifle, zoomed, never turned red on a grunt 15 m away.
  So the facing is the direction from the game's eye (as it fires from it:
  `director_camera_deterministic`, crouched or not, or a vehicle's camera,
  which aims the seat's weapon) to where the controller's line meets the
  world, the reticle's point (`game/vr_aim.c`): taken as the player's
  control aims, with the poses the aim is taken with, placed in the world
  from the eye the last frame was drawn from (no tick has passed since),
  and the line-of-sight test's distance, not the reticle's smoothed one.
  No nearer than 1.5 m along the line, nor nearer the eye along it than
  three times the eye's distance across it, so that the aim swings by 18
  degrees at most. Measured on b30 (`debug.vr_test_target`,
  `debug.vr_aim_log`), the rifle held 0.53 m from the eye, low and right:
  the unit's aim passed 50 cm from the reticle's point before, 0 to 1 cm
  after, on foot, crouched by the head (the eye 0.37 m away) and in the
  Warthog's gunner seat (its camera 8 m behind); the grunt with the hand
  0.9 m away red 542 ticks of 547 and killed five times, and the sniper
  rifle's reticle red on it while zoomed.
- The game's magnetism moves a gamepad's look (`player_control.c`), which
  the controller does not use: the aim assist is `vr.c`'s
  (`aim_assist_step`), after the aim is steadied. As the player's control
  aims, `vr_aim.c` asks the game for the target its magnetism would be
  drawn to from its eye along the aim (`player_aim_assist_target`,
  `aim_assist.c`: the weapon's magnetism angle and distance at its zoom,
  the same targets) and tells vr.c its magnetism level and its middle's
  direction from the controller. The part of the hand's turn that takes
  the aim away from the target (less as much of it as follows the target's
  own turn, and none of a turn toward it) is slowed by half, times
  the level and `vr.aim_assist` (by 0.8 at most); zoomed, the aim is drawn
  toward it at 1 degree a second times the same. The aim stays
  within 3 degrees of the hand's, and away from a target comes back to it
  over 0.3 s, no faster than 6 degrees a second plus half the hand's turn.
  Not the point the game's autoaim aims at: that point lies on the line
  from the game's eye, and seen from the hand it moves as the aim does,
  which drew the aim off the target. Nor the target's middle, as it was:
  of a Shade's gunner that is behind the turret's gun, and zoomed the aim
  was drawn down off the head onto the turret (the user's "locks on the
  turret instead of the grunt"). Now `vr_aim.c` tests the target's autoaim
  pill (its pelvis to its head) at 9 points for being in sight from the
  controller (nothing between but the target itself: not the vehicle it
  sits in, which the game's own test counts as clear for its gunner), and
  tells vr.c the part in sight; the aim is held to, and drawn to, its
  nearest point (so not slowed along the body, nor drawn along it), and
  the target's middle is only how it moves. None of it in sight, no target.
  The game's own choice of target was right: only ever a biped, a vehicle's
  occupant found within it, never the vehicle; its bullets' autoaim is left
  as it is. Measured with `debug.vr_test_target = "<metres> 0 grunt head;
  c gun turret"` (a grunt in a Shade's seat, the hand pointed at its head):
  on c10 with the pistol at 2x, the aim was drawn from the head to the
  middle (2.6 degrees, the view centred on the turret's gun) before, held
  within 0.2 degree of the head after (5 of the 9 points in sight); on b40
  with the sniper rifle at 8x, 1.95 degrees off the head (40 cm at 12 m)
  before, 0.04 to 0.36 after. An Elite on a Ghost: held on its head, all of
  it in sight; on a Banshee: none in sight, no target; an empty Ghost or
  Shade: no target. Measured on b30
  (`debug.vr_test_target = "8 0 grunt free"`), the hand sweeping across a
  grunt 8 m away at 8 degrees a second: the aim on it 313 frames where the
  hand was on it 216 (`vr.aim_assist = 2`: 550), never moving against the
  hand; on a50 with the sniper rifle zoomed, the hand held 0.6 degrees off a
  grunt's middle 15 m away, the aim drawn to 0.07 degrees of it at 2x and
  8x, and back with the hand as the zoom ended.
- With a gamepad (`vr.aim = "gamepad"`) the facing is the game's own, from
  the stick (`player_control.c`), and the netcode sees it as ever; the
  view before the head's turn and move is the facing's yaw and pitch
  (`vr.gamepad_view = "camera"`), its yaw alone (`"level"`) or its yaw in
  steps (`"snap"`), and the first-person weapon is posed from the player's eye along
  the facing (not the camera, which shakes as the weapon fires), the
  game's arms with it. The reticle is where the facing meets the world, as
  the controller's is where its line does.
- The first-person weapon and the arms are two models posed by one
  animation graph, whose nodes the game builds each frame
  (`first_person_weapon_build_node_matrices`); `game/vr_hands.c` poses
  them again. Each hand's middle (its wrist and knuckles) is at its
  controller's grip pose, turned with the controller's pointing pose as
  the weapon's idle animation has it turned from the camera. For the
  right hand the camera is placed so; once the nodes are built, all of
  them are moved together (a rigid move, nothing changing shape) until the
  weapon's root (`frame gun`, hanging from `frame r wriste`; the needler's
  `frame body`) is where the idle pose has it from that camera
  (`weapon_hold`), so that the game's animations, which move the whole
  weapon and both arms about the camera (firing, the plasma pistol's
  charged shot, the vent, a melee, a reload, drawing, the idle's sway and
  the walk's bob), keep only what they move within the weapon and the
  right hand on it, and the weapon stays in the controller (the effects at
  its markers with it), whatever `vr.hands` is; the left wrist
  (`frame l wriste`) and its fingers are put there in the idle pose's
  shape, the fingers curled about the knuckles' line as the grip is
  pulled, and blended onto the weapon's foregrip as the left hand takes
  it: onto the animation's hand while it stays within 12 cm of the idle
  pose's on the weapon, and onto the idle pose's from 24 cm
  (`support_hold`). The right index finger curls as the trigger is
  pulled, and the weapon's `frame trigger` (the pistol's) moves back. How
  far it curls at rest and pulled is each weapon's (`trigger_fingers`, by
  the first-person animation's tag name), from the idle animation's finger,
  fitted to the weapon's first-person model as the game poses it: the
  finger's mesh against the model's, touching the trigger (the pistol's, the assault rifle's drawn one,
  below) as it goes back, or lying where a trigger would be, never in the
  weapon. The idle animation's finger runs along the pistol's trigger into
  its frame, and wraps into the front of the Covenant weapons' grips; the
  needler's grip still meets the finger a little, whatever its curl. The
  rocket launcher's finger lies flat along the front of its grip, off its
  corner, with no room for a trigger.
- Some first-person models have no trigger (the assault rifle's, the
  sniper rifle's, the shotgun's, the flamethrower's): their animation's index finger hooks
  round an empty gap under the receiver, ahead of the grip. So one is
  drawn there (`game/vr_trigger.c`), modelled for each to sit in the crook
  of the finger (`game/vr_trigger_model.h`, made by
  `tools/vr_trigger_model.py`): the assault rifle's an MA5B's, the sniper
  rifle's slimmer, the shotgun's broad and short, inside its own guard,
  the flamethrower's stout, raked forward out of its handle's underside,
  each a curved blade of rounded section, its face crowned where the
  finger lies, broad where it hangs from the receiver and curling forward
  to its tip below the finger, out of a low housing on the receiver's
  underside (202 vertices and 312 triangles each). Each is found by its
  model's tag name as a map is loaded, made once a map as a model part's
  buffers and drawn just after its weapon's model with the weapon's own
  shader, from its gun node, as the model is drawn (its lighting, tint and
  camouflage), the blade's faces sampling a fitting part of the weapon's
  texture (the trigger the rifle's paints but no face of its model shows;
  the sniper rifle's and the shotgun's receiver's underside; a bar of the
  flamethrower's frame), the
  housing's the receiver's underside. The blade turns back about its top
  as the finger pulls, its face going back as far as the finger's weapon
  has its trigger go (`trigger_fingers`); the flamethrower's, its pin
  below the finger, slides back and up along it instead; the housing
  stays. Only for
  those models (by their tag names): the world's weapons and the third
  person have none.
- The vehicles' models were made for the game's camera following them: the
  Scorpion's is a closed hull, its driver's head in the hatch's lid over
  an empty slot. For a seat of a vehicle's model, and one of its views
  where the vehicle has a setting for it, a cabin (`game/vr_cabin.c`,
  `game/vr_cabin_model.h`, made by `tools/vr_cabin_model.py`) puts the eye
  where it belongs (vr_seat.c takes it, and blinks to another as the view
  changes) and draws about it what the model lacks: meshes made once a map
  as a model part's buffers, each drawn from a node of the model with one
  of its own shaders just after the model is drawn (`render_objects.c`),
  for the vehicle the first player is seated in alone, their texture
  coordinates stored as the model's are (divided by its base map scale).
  The Scorpion's: inside, a driver's compartment ahead of the slot, under
  the front plate, its vision blocks open (the hull's own faces about it
  face out and are not drawn from inside); the hatch, a cupola ring on the
  seat: the game turns a Scorpion toward its driver's facing (vehicles.c),
  so a view turned with the hull would chase its own turn.
- The weapon's kick (`game/vr_feel.c`): the game tells it of each shot of
- The weapons' feel (`game/vr_feel.c`): the game tells it of each shot of
  the first player's weapon in hand as the trigger fires
  (`weapon_trigger_fire`, which a network game's client runs for its own
  player's weapon too), and of the damage each local player takes as it
  shakes their view (`player_effect_start`, on a client as the host's
  damage is replayed). The kick is a critically damped spring each shot
  strikes (its peak the weapon's kick, after a fortieth to a twelfth of a
  second), moved on once a frame; the first-person weapon's camera
  (`vr_render_weapon_camera`) is turned up by it about the controller's
  grip and moved back, so the weapon and the hands posed from it kick
  together. A scope's view is placed from the camera unkicked
  (`vr_render_scope_view`). Each weapon's kick and pattern are by its tag's
  name: the firing's damage effects, which kick the flat screen's view and
  rumble its gamepad, do not say how hard a gun kicks (the camera's impulse
  is none but for the plasma pistol's charged shot, and the sniper rifle's
  rumble is lighter than the pistol's), nor does the projectiles' damage
  (the rocket's is in its blast). The damage effect of the player's own
  firing (that of the weapon in hand, from no one) and a hit with damage
  are not given to the gamepad's rumble (`rumble_player_impulse`); the
  shields breaking is seen as the unit's shields go down, the same unit
  from frame to frame. The patterns are played by `src/vr.c` a pulse at a
  time as each comes due, at the frame (OpenXR's haptics: each pulse
  replaces the one before).
  turret, the eye on the turret's axis. The Banshee's: its canopy closes
  over the pilot in flight and faces out, so only the nose was seen; a
  cockpit about the pilot, on the hull a sill, a console with the
  Covenant's glowing panels and two handles, and on the canopy's node
  (made closed as the game poses it in flight, put open as the model is)
  the rails, a spine, a bow framing the view ahead and a shell behind the
  head, the view ahead and above left open. The view stays the player's in
  its seat: the game turns a Scorpion toward its driver's facing
  (vehicles.c), so a view turned with the hull would chase its own turn.
- The game throws a grenade in an animation: the throw button starts the
  unit's throw animation, its third frame puts the grenade in the unit's
  left hand, and its key frame (the cyborg's with the assault rifle: frame
8, 0.27 s in) lets it go, from
  just before the eyes, along the aim, at the unit's grenade velocity (let
  go of sooner, as when a flinch cuts the throw short, more weakly). The
  first-person weapon plays a throw of its own beside it, which lowers the
  weapon. A grenade thrown with the left hand (`src/vr.c`,
  `game/vr_grenade.c`): the swing presses the left trigger, so that the
  game's own throw begins, through its input and the netcode as any throw
  does; while the hand holds on, the throw animation (and the first-person
  one, which marks no release of its own: the unit's frame) waits a frame
  before its release, and when the hand lets go sooner, goes on to it at
  once (the grenade put in the hand first if it is not there yet); the
  grenade is then moved to the hand and its velocity turned along the
  hand's throw, its speed the game's times the throw's power. With the
  hands the controllers', the first-person throw is not played: the left
  hand is the player's, and the weapon stays in the right. The game's
  grenade, in the hand of the player's body from the throw's third frame,
  is not drawn in first person (nor is the body), so the grenade in the
  left hand is drawn by `game/vr_grenade.c` after the first-person weapon:
  the selected type's projectile model at the controller's grip, from the
  grip taking it until the game throws it (let go of, where the hand let it
  go). The thrown one starts at the point last drawn, and its first tick is
  drawn from there (`render_interpolation_object_from`), not swept from the
  body's hand. The hand's
  velocity is measured over 35 ms of its poses (at their display times),
  and the release's is the fastest of the last 0.1 s, as the hand slows
  as it opens. While the hand throws, the player's facing turns along the
  throw, which the netcode sends: on a client, its own copy of the
  grenade is the one that deals damage (reported to the host), and the
  host's copy, which the other players see, flies the same way, but at
  the game's speed, let go of when the host's animation lets go of it.
- With arms, the shoulders are below and beside the eyes, turned with the
  body: the heading between the head's and the hands', followed past a
  40 degree turn of the head, and slowly (so that looking about does not
  swing them). Each arm is two bones, the model's (0.31 to 0.33 m each):
  the elbow is where they meet (the law of cosines), bent down, out and a
  little back, and each bone is turned from the idle pose's to lie along
  the arm, the forearm twisted with the hand. Out of reach, the shoulder
  comes forward up to 5 cm and the arm stretches the rest. With floating
  hands, the arms are scaled to almost nothing at the wrists.
- The physical crouch (`vr.physical_crouch`, `src/vr.c`): the head's
  height from the recentred eyes (with 7 cm of hysteresis) presses the
  left stick's click, the game's crouch, which the game holds only while it
  is pressed; with it, the crouch is held at a full stick too
  (`player_control.c`, which otherwise stands a player pushing the stick
  all the way). The game's eye drops by its crouch (`biped->biped.crouch`
  times the biped's standing camera height less its crouching one,
  `biped_get_sight_position`) over a few ticks; `game/vr_render.c` adds
  that back to the eye while the head is low (just out of a vehicle the
  player is still crouched from before it, a frame before the crouch is
  pressed again), the crouch blended between the last two ticks as the
  camera is, and keeps doing so until the game has stood the player up
  again (else standing up would drop the view by the crouch still left). A crouch by a hand, with the head up, keeps the
  game's drop. With `vr.aim = "gamepad"` the weapon, posed from the eye,
  comes down with the head's crouch, and the reticle stays where the aim
  from the game's eye meets the world.
- The flip of an overturned vehicle (`vr.vehicle_flip`, `src/vr.c`,
  `game/vr_flip.c`): the game offers it as the player's action result
  (`players.c`, `player_examine_nearby_vehicle`, each tick on every
  machine, so a network game's client knows its own offer), which outranks
  every other. `player_control.c` asks `vr_flip.c` as it reads the
  player's input: with a flip offered, the lift's press sets the action
  bit, as X does, so the game, and a client's host, flips it as for X;
  where only the lift flips, X's action bit becomes the reload bit (the
  reload the action falls back to where it does nothing else). `vr.c`
  tells the hands' lift from the controllers' poses, before the holsters,
  the reload, the foregrip, the throw and the recentring read the grips,
  which leave alone a grip the lift has. The prompt is the HUD's own state
  message, our text in place of the map's (`hud.c`).
- The grab of a weapon (`vr.grab_world`, `src/vr.c`, `game/vr_grab.c`):
  the game offers a weapon on the ground as the player's action result
  (`players.c`, `player_examine_nearby_item`, each tick on every machine),
  the nearest the body of those it would offer
  (`player_set_action_result`); there `vr_grab_offer` keeps the first
  player's offered weapons, and among them offers the one the right hand
  is nearest (within 35 cm of its reach) instead, but on a network game's
  client, whose host decides pick-ups. `player_control.c` asks
  `vr_grab.c` as it reads the player's input: the offered weapons go to
  `vr.c` as lines in the head's axes (`vr_render_world_place`), which
  tells the hand's reach (after the holsters, which keep a grip squeezed in
  them) and its grip; once the game offers the weapon gripped (a tick
  later at most), the swap bit is set for three ticks, as X held sets it,
  so the game, and a client's host, picks it up as for X
  (`player_handle_weapon_swap`). `render_objects.c` draws the weapon the
  hand is nearest lit up as the holsters' are (`vr_holster.c`,
  `vr_holster_highlight_object`).
- The press of a control (`vr.grab_world`, the same files): the game offers
  a device control only where the player's aim meets its bounding sphere
  (`player_examine_nearby_device`, which a network game's host runs for a
  client by the aim the client sends). `vr_grab.c` tells `vr.c` the
  controls the game would offer were the aim on them (in reach of the body,
  able to change, front-facing: the same tests), each as its face (its
  `front` marker, facing the player's side for one used from both sides)
  and a radius; `vr.c` presses one as either hand's fingers reach its face
  from before it. Until the game offers it (or 10 ticks), and for the two
  ticks of its action, `player_control.c` takes the facing from
  `vr_grab_facing`: from the eye to the control's middle, as the grenade's
  throw takes it (`vr_grenade_facing`), so the action bit goes with an aim
  on the control, in the same ticks, to the host as to the game here.
- In a vehicle's seat (`vr.vehicle_view = "first_person"`) the eye is in
  the seat, and the player's body is drawn from it as the game seats it
  (`game/first_person_legs.c`'s `first_person_legs_vr_seated`, from
  `vr_render_body_seat`), moved so that the top of its neck is where the
  real one is with the head as recentred (10 cm behind and below the seat's
  eye, `vr_render.c`'s `base_position`, the eye before the head's offset),
  its upper body turned about its hips toward the real neck (no farther
  than 0.6 radians), leant back clear of the eyes and its head folded away
  as on foot, and its arms folded away where the VR mode draws its own
  (`vr_render_handlebar_hands`: in a seat whose camera follows the vehicle,
  the hands alone with the body drawn); standing in a cabin that says so
  (`vr_cabin_model.h`'s `standing`: the Scorpion's hatch). The Scorpion's
  driver is not hidden by its seat's flags (`scorpion-driver`: driver,
  gunner, third-person camera, not invisible) but by its hull, the seat's
  pose sitting him under the hatch; the VR mode hid the player's own unit
  in every seat (`vr_render_hides_object`), and still does for the scope's
  pass and for what is attached to a feed cockpit's vehicle (the body is
  drawn there through `first_person_legs_wanted`). The game animates the
  seated body, none of it the player's own motion: getting in sweeps the
  head 2.7 to 3.4 m over most of a second, getting out 1.7 to 2.7 m, and a
  turret's seat swings about the turret's pivot as it aims (where the
  controller or head points), the Shade's eye 0.65 m from it, with flinches
  when hit. With `vr.seat_motion = "still"` the eye follows the head's
  path as the body climbs in (and out), smoothed in the vehicle's frame
  (`game/vr_seat.c`'s glide: a critically damped spring over 0.12 s, no
  faster than 3 m/s), then is held in the vehicle (its root node's frame,
  posed between ticks as it is drawn) where the head is once seated, until
  the body climbs out; what is left between where the eye was and where it
  belongs (the seat's eye, a cabin's, the first person's once out) closes
  over 0.12 s, no faster than 3 m/s, with no blink. With `"blink"` it is
  held where it was standing as the body climbs in, and blinks into the
  seat and out of it. It rides with the vehicle, but the gun
  swivels in front of it instead of the eye about the gun. With `"head"`
  the eye is the seated player's head marker. The view turns as on foot
  (the stick and the head, never the vehicle): the vehicle steers and aims
  where the controller (or head) points, and the horizon stays level
  whatever the vehicle does. But in a Ghost's driver seat steered by its
  handlebars (`vr.ghost_steering`, `game/vr_vehicle.c`): once the eye is in
  the seat (the body seated; the blink in at its darkest), the view's heading
  (the player's axes, `body_yaw`; eased to it over half a second or more, no
  faster than 45 degrees a second, but all at once in a blink's dark) is the
  Ghost's root node's, as the frame draws it
  between ticks, eased over 0.05 s (the ticks' steps in its turn rate
  smoothed out); the eye is the seat's held level in the Ghost (its place
  turned by the heading alone), its shift by the Ghost's tilt let in over
  0.3 s; the handles are its "left hand elite" and "right hand elite"
  markers (where the Chief's hands are too as the game animates him
  driving; the Ghost has none for him), given to `src/vr.c` in the
  player's axes, which takes each as the grip closes within 30 cm, and
  turns the bars into the driver's facing: the Ghost's heading at its last
  tick turned by the error at which the game's turn toward the facing
  (`update_alien_scout_physics`: the lesser of sqrt(2 a e) and e / 2 a tick,
  a = 0.2 degrees a tick squared) is the turn the bars ask for, so that the
  error holds as it turns. Its aim line, which the reticle and the aim from
  the game's eye follow (`vr_aim.c`), is from between the handles along
  that facing. The first-person model, which the game does not draw from
  a seat whose camera follows the vehicle (`first_person_weapons.c`), is
  drawn there without its weapon (the arms alone), each hand holding its
  handle placed on it as the controller would be, upright along the Ghost,
  its fingers closed. The comfort vignette (`vulkan/host_vulkan_vr.c`,
  with `halo_vr_vignette`'s strength) multiplies each eye's image by a dimming beyond 50
  degrees from that eye's own middle (30 at its strongest) over 22
  degrees, by 65% at most, eased in over 0.12 s and out over 0.4 s.
  The Warthog's driver seat is the same seat (`vehicle_seats` in
  `vr_vehicle.c`, `struct halo_vr_seat`), steered by its wheel: the handles
  are its "left hand wheel grip" and "right hand wheel grip" markers, on
  its "frame steering wheel" node, which the game's steering animation
  turns; the wheel's axis is the cross of its grips about that node, toward
  the driver; the wheel's turn is the hands' line's about it from where it
  was as both took hold, and a quarter of it, to the tag's lock (30
  degrees), is the facing's error from the Warthog's heading (with the sign
  of its `vehicle.speed`): `vehicle_update` turns the wheels to that error
  itself (`update_human_jeep_physics` steers the front pair by it and the
  rear the other way). Its tilt is let in over 0.5 s. Its passenger's seat
  is ridden so too (`HALO_VR_SEAT_RIDE`: no handles, the hands as on foot).
  The game takes the facing of a seat without the third-person camera bit
  (the passenger's) as turned with the vehicle's tilt
  (`player_aiming_vector_from_facing`): `vr_vehicle_facing` turns the VR
  mode's facing back by that turn's inverse first (`player_control.c`). Its
  gunner's seat is ridden so too (`HALO_VR_SEAT_TURRET`): its handles are
  the "gunner left hand" and "gunner right hand" markers, on the chaingun's
  "frame gun" node, the hands held as they are turned, and it aims as the
  controller does on foot (the game's turret follows the gunner's aim at
  most at its tag's half a turn a second). A Banshee's pilot's seat is ridden as a
  passenger's (no handles: the right controller steers it, as the facing
  the game turns it toward), its tilt let in over 0.4 s; it flies, so the
  vignette takes the turn of its forward, its pitch's too, as its turn.
- Where the eye would jump more than 0.1 m (into the seat once seated, back
  to the player's own eye once out, to the game's camera following the
  vehicle or, with `vr.death_view = "game"`, the dead's, and back), it
  blinks: the view darkens over 0.1 s with the eye held, moves in the dark,
  and lightens over 0.15 s. The darkness multiplies the eyes' images as
  they are presented, all of each, before the helmet's rim
  (`halo_vr_eye_tint`, `vr_draw_tint`; the HUD's layer stays lit): a
  screen flash in the eye pass, as it was, covers the game's viewport
  alone, which the eyes' images reach past at their outer edges (the left
  eye's outer 3% stayed lit on spark's simulated headset). The game's
  director cuts between its first-person camera and the one following a
  vehicle in the VR mode, rather than flying between them for a second
  (`camera_change_pause`), which swept the view some 6 m, from behind the
  player into their head, on getting out.
- As the player dies (`vr.death_view`, `game/vr_death.c`) the game's
  director turns to its dead camera (`dead_camera.c`), which the VR mode
  took as the eye, and its heading as the view's (the dead do not aim):
  from inside the body it pulls out over three seconds to 2 to 6 world
  units from it, in a direction picked at random, 27 to 63 degrees above
  it, looking at it. With `"face"` and `"still"` the eye is held instead
  where it was the frame before (`vr_seat.c`'s, in a vehicle's seat too),
  and the view's heading is the player's, as the stick turns it: the game's
  camera is the director's as ever (what is drawn about it), but not the
  view, nor the sounds' listener, which is the head in the view ("Sound
  from your head"). The dead body is not the player's unit any
  more, so the game draws it whatever the camera; while its head is within
  0.6 m of the eye it is drawn shrunk into its neck (`render_objects.c`,
  from 0.3 m), and while any of its nodes is within 0.55 m the view is
  dimmed (by 85% within 0.25 m), the dead body's armour seen from within it
  and lit by its shields' last flash. The tint (the view times 0.68, 0.34,
  0.30 over 1.2 s), the dimming, the blinks and the dark are one more
  multiple of the eyes' images, as the seat's blink is (`vr_death_draw`).
  The dark comes over the last 15 ticks before the game brings the player
  back: the revert's (`main.c`'s timer, 92 ticks after the players are all dead:
  `main_vr_revert_ticks_left`) or the multiplayer respawn's (the player's
  `respawn_timer`); where neither is known (a client of network co-op), the
  view goes dark as the player is back. Back, it is held dark 0.1 s and
  lifted over 0.3 s, in steps of at most a thirtieth of a second (the frame
  a checkpoint is loaded in is long), while the eye's jump to its new place
  blinks (`vr_seat.c`). A blink of the death's own (0.1 s down, 0.15 s up)
  turns the view to a body landed out of sight (`"face"`: once it is slower
  than 0.5 m/s, from 0.8 s after the death, at the latest 1.6 s), or puts
  the eye at the game's camera when it watches someone else (the dead
  camera's unit is not the player's body: `director_vr_dead_camera`), once
  it moves slower than 1 m/s from 0.5 s after it turned to them (at the
  latest 2.5 s), and again 4 m on, at most every 2 s; never as the dark
  before the player is back comes.
- The head in a wall (`vr.head_in_wall`, `game/vr_headwall.c`): each frame
  on foot (in the first-person view, alive, no cinematic), once the eyes
  are placed (`vr_render.c`), the line from the game's eye (before the
  head moves it: the unit's physics keeps it 0.2 world units, 0.61 m, from
  the walls) to the eyes' middle and 5 cm on (the eyes' sphere) is tested
  with `collision_test_vector`, as the game's line of sight is
  (`_collision_test_for_line_of_sight_flags`: front faces of the
  structure, scenery and machines, not two-sided nor invisible surfaces)
  but without vehicles, ignoring the player's unit; how far the sphere's
  front is past the hit fades the view to black over 5 cm (black with the
  eyes' middle at the surface, past which the tests from it find nothing,
  front faces only). And from the eyes' middle, five lines ahead (along
  the view and 15 degrees off it up, down, left and right) as far as the
  near plane (`rasterizer_near_clip_distance`, 0.0625 world units) and
  6 cm: the nearest depth along the view fades it over 5 cm, black 1 cm
  short of the near plane, where a wall facing the eyes is clipped whole
  (and a little before which the edges of the view, where a rock bulges
  nearer than it is at the middle, see through already). The
  darkness rises to that at most in 0.05 s and falls in 0.25 s; it is one
  more multiple of the eyes' images (`vr_headwall_draw`, after the
  death's), and from half dark the HUD's state message is ours
  (`hud.c`, outranking the game's) and the scope pass is skipped
  (`render.c`), so neither a raised scope nor the smart-link sees through.
  A wall beside the head clipped at the edge of the view (leaning around
  a corner) does not fade it.
- The sky is drawn about the camera at a thousandth of its size, a few
  metres away: its draws move to each eye as from the centre of the head,
  so that it is at infinity. Its draws are depth-clamped rather than
  clipped by the near and far planes (its far side is near the far plane,
  and the union view, moved by the head, clipped parts of it some frames).
  The rest of the eye pass is clipped as the flat screen is.
  What the game projects to the screen itself (lens flares,
  their visibility tests, the light volumes' sprites) is drawn in screen
  space and moved to each eye as its point moves, by a scale and offset.
  The sun's glow (`rasterizer_ray_of_buddha`), a blur of the screen about
  the sun, is not moved.
- A raised scope (`vr.scopes`, `game/vr_scope.c`) is drawn in a pass of
  its own before the eye pass (whose state, the clusters seen, the lights
  and the scene, is then the eyes'): the game's 3D view once more, from the
  scope's eyepiece along the aim, its field of view the angle the lens
  spans from the eye over the zoom's magnification, into the middle 480x480
  of the 640x480 screen at a scale of its own (480 lines are 480 pixels at
  `vr.resolution_scale` 1, more or fewer with it, 256 to 768), in the
  game's ordinary one-layer targets (no multiview, no eye transforms). It
  draws no first-person weapon, no lens flares (their visibility tests are
  the eyes'), no objects' shadows, no weather and no HUD. At the present
  the middle square is copied into the scope's swapchain image, upside
  down for OpenXR, round, darkening to black at its edge, with its reticle,
  and shown on a quad layer at the eyepiece (`VR_SWAPCHAIN_SCOPE`), facing
  back along the scope, over the eyes' images and under the HUD's layers,
  for the eye looking through it alone (`eyeVisibility`): the compositor
  reprojects it as it does the eyes'. Its pass's shaders, without
  multiview, are a driver's to make as they are first drawn with; the pass
  is drawn once, unseen, the first time a scope is in hand in a map, so
  that the first raise does not wait for most of them (on spark the first
  raise took 0.44 s without it, 0.22 s with). Where each scope's eyepiece
  is on its first-person model is a table in `vr_scope.c`. On spark (b30,
  the pistol raised, against the same run with `vr.scopes = false`) the
  pass costs about 0.5 to 1.3 ms of the GPU's time and 1.5 ms of the
  game's a frame; confirm it on a Steam Frame.
- The smart-link's view (`vr.scope_view = "smartlink"`, `vr_scope.c`) is
  that pass too, while the game is zoomed (the player's zoom level, which
  the game's own click and unzooms set): the weapon's sight's, from the
  right controller along the aim's line (`vr_render_scope_aim`), its field
  of view `vr.smartlink_size` over the zoom's magnification, up as the
  world's: its middle is on what the line meets, however far. From between
  the eyes toward that point, as it was, it swung by the eyes' parallax
  each time the line crossed an edge and the point jumped along it (a
  Shade 12 m away against hills 200 m behind: 2.4 degrees over a few
  frames, 19 in the view at 8x); along the line it turns only as the aim
  does (on b40's Shade, swept across at 8x: no more than the aim, in any
  frame).
  For the sniper rifle and the pistol it is the flat screen's zoomed view:
  the pass draws all the 640x480 screen (not its middle 480 alone), and
  over the view the weapon HUD's screen effect (the scope's mask:
  `interface.c`, `interface_draw_vr_scope_screen_effect`) and the weapon's
  zoomed HUD alone (`hud_weapon.c`, `hud_render_weapon_zoomed`: all its
  crosshairs, as the flat screen draws them zoomed, and the elements that
  follow the zoom; not moved to the controller's point as the HUD layer's
  crosshairs are), laid out in the window the game gave the camera (the
  HUD's safe area, as on the flat screen); the copy keeps a 4:3 window of
  its middle square, the HUD's size or more, its edge feathered, and the
  eyes' pass dims the world about it (`halo_vr_scope_shape`). Its
  `scope-view<frame>.bmp` screenshot is the pass's image as drawn.
  Its image (the same copy: the window, its sight and readout, its glow,
  and its animation, the image's middle rows) is not a layer: the host's
  renderer (`vulkan/host_vulkan_vr.c`) draws it into both eyes' images
  after the foveation's composite, where
  each eye's ray meets the window's quad (`halo_vr_scope_window`: through
  the reticle's point, at its smoothed distance, facing back along the line
  from between the eyes to it, up as the world's), so that each eye sees it about the reticle, at its depth, over
  the world and the weapon in hand and under the helmet's rim, the visor
  and the HUD's layer. Where the eyes' depth is nearer than 1.2 m (the
  first-person weapon and the hands; foveated, the periphery's depth),
  `vr.scope_gun` lets the weapon through (a fifth, or all of it). Its image
  is 1024 pixels square at `vr.resolution_scale = 1` (480 for the eye's
  lens). The HUD's zoomed view is not
  drawn in the HUD's layer while it shows (`hud_weapon.c`, `vr_scope_shown`), and its shaders
  are made ready as the eye's are, the first time a weapon that zooms is in
  hand in a map. On spark (a50's start, standing still, the pistol zoomed
  and unzoomed every 8 s in one run) its cost was within the noise of the
  GPU's time, about 0.1 ms in 1 (b30's fight varies far more from run to
  run); the pass is the eye's, as large, whose 0.5 to 1.3 ms on b30 is the
  better guide. Confirm it on a Steam Frame.
- Zoomed, the smart-link's view magnifies the hand's shake as much as the
  world (2 and 8 times). The aim is steadied while it shows (`vr.c`,
  `aim_steady`; the game tells vr.c the magnification, `vr_aim.c`): a
  one-euro filter of the aim's direction, its cutoff 2 Hz over the
  magnification less one (2 Hz at 2x, 0.29 Hz at 8x), a third lower with
  the left hand on the foregrip, plus 0.15 Hz for each degree a second the
  hand turns (its turn smoothed at 1.5 Hz, so that a tremor's back and
  forth cancels and a swing does not), eased in and out with the zoom. The
  reticle, the view and the game's aim follow it (`aim_forward`, the
  aim's line); the first-person weapon follows the hand. Each frame steps
  it once from the last frame's state: the poses located as the game aims
  and those located again for the draws are each steadied from it, the
  later replacing the earlier. Measured with `debug.vr_test_shake = "0.4
  1"` on a50 (the sniper rifle; a shake of 0.55 degrees RMS, half a 1.5 Hz
  wander, half a 9 Hz tremor): the aim's change from frame to frame was
  3.2 times less at 2x and 7.2 times less at 8x (0.05 degrees, 0.4 in the
  view), its sway 0.6 and 0.5 of the hand's; with both hands on the rifle
  (the line between them sways 0.2 degrees) 0.1 to 0.15 degree at 8x,
  about a degree in the view. A 10 degree swing in half a second at 8x
  lagged by 1.2 degrees at most and was within 0.1 degree of the hand
  0.25 s after it stopped; at 2x, 0.7 degrees and 0.06 s. How it feels in
  the hand is the Steam Frame's to say.
- The sounds' listener (`game/vr_render.c`, `vr_render_listener`): the game
  places each sound in its listener's view each frame (`sound_manager.c`,
  `update_channels`), the listener built from the game's camera at the
  tick (`refresh_listener`: `observer_get_camera`, not interpolated), and
  the mixer (`src/dsound_sdl.c`) pans each voice from the place it is
  given, its own listener at the origin. In the VR mode the first player's
  listener is the head: the tick's camera, moved by the view's eye's move
  from the game's eye in the last frame drawn and by the head's place, and
  turned by the head as located when the frame began (`halo_vr_head`,
  sound being placed before the frame is drawn) and by the view's heading
  (the camera's own as it is then, in a cinematic). The places are resent
  to the mixer as they move 6 mm (`sound_dsound_xbox.c`, 15 cm on the flat
  screen), and the mixer ramps each voice's gains over each mix (512
  frames). Measured on b30 with `debug.vr_test_head = "90 16"` and the
  controllers held still: the listener's yaw, pitch and roll were the
  drawn head's within 0.03 degree each frame (3,700 frames), and on foot its
  place too, while the game's camera did not turn; in the skipped ride in,
  whose frames are many ticks apart, the tick's listener led the
  interpolated view by up to 2.7 m, as the flat game's does.
- The HRTF (`vr.hrtf`, `src/dsound_sdl.c`, `hrtf_place`) is Brown and
  Duda's structural model: for each mono 3D voice a high-shelf of the
  pinna's shadow behind, a peaking notch (RBJ's), a 64-frame delay line,
  and for each ear a delay read from it (Catmull-Rom) and a one-pole
  one-zero head shadow, all ramped over each mix. Measured as above, with a
  sound 3 m away level with the head (`debug.vr_test_sound`,
  `debug.audio_log`, `debug.audio_record`): 90 degrees to the left, the
  right ear 0.63 ms later, the high frequencies 5.7 dB up at the left ear
  and 13 to 20 dB down at the right; behind, 5.3 dB less above 2.5 kHz and
  the ears all but alike; 40 degrees above, the notch at 10 kHz (7.7 kHz
  level). In the recorded mix the level between the ears above 3 kHz
  followed the head's turn from -12 to +10 dB, smoothly. Panned
  (`vr.hrtf = false`), the same sound (within its minimum distance, 9 m,
  so drawn to the middle) was at most 7.7 dB louder in one ear, with no
  delay. The mixing took 4.1% of a core of a DGX Spark (2.5% panned, 2.2%
  on the flat screen): the Steam Frame's cores are to be measured.
- The visibility tests (the lens flares') are counted by the pixel shaders'
  atomic counters (in a storage buffer, where the device has fragment
  stores), not by queries: Vulkan counts a query in a multiview pass in as
  many slots as views, which Zink, the OpenGL the VR build drew with on the
  Steam Frame before Vulkan, did not allow for, and Turnip's writes past
  them faulted the GPU (and could lose the device).
- The first-person weapon's own lens flares (the plasma pistol's charge, a
  ball of light at its muzzle; the tips' glow; the plasma rifle's vents; a
  muzzle flash's) are not tested in the eyes: they are drawn wholly visible
  (`rasterizer_lights.c`). The weapon is drawn over the world, which hides
  neither it nor them, and over the weapon itself they are masked where
  they are drawn, as on the flat screen. Their test is of a point a
  centimetre or two across at the marker, which the flat screen's pose of
  the weapon keeps in the clear; held in the hand, the weapon covers it
  from many sides: aimed with the eye behind the pistol, its charge was a
  faint sliver at the muzzle (b30 on spark), and the tips' glow was gone.

## What operates

| Area | Status |
| --- | --- |
| Game code | All 466 C files of the game. The changes are in "Game source changes". |
| Graphics | Direct3D 8 on Vulkan 1.3, in the host, with SDL3's window. A front end (`src/d3d8_device.c`) keeps the Direct3D state and turns each draw into a description that any graphics API can draw (`src/xgpu_backend.h`: the pipeline's key, the textures and targets it binds, the draw's ranges, all plain data); the Vulkan backend (`vulkan/`) draws them in the host, building the pipeline for a key once and keeping it (on macOS and iOS the Metal backend). The port translates the NV2A vertex shaders and register combiners into a description of their operations (`src/xgpu_shader.h`), which `src/xgpu_spirv.c` writes as SPIR-V (and `src/xgpu_msl.c` as MSL, for Metal). It decodes all the Xbox texture formats. The vertex and index buffers come from a copy of the Xbox memory on the GPU. |
| High-res HUD | The HUD is drawn from high-res assets: redraws at 8x the size of the maps' bitmaps (4x for the largest), in `port/assets/hud`. They cover the meters, counters, panels and their outlines, the motion sensor, reticles, waypoints and scopes, but no bitmap with English text. `tools/hud_assets.py` makes them from the SVG redraws (the weapons' reticles from those of the PC HUD sheet that match the Xbox's exactly, the sniper rifle's 2x and 10x set in Overpass), and the build embeds them in the executable. When the game uploads one of those bitmaps, `src/hud_hires.c` gives the high-res texture in its place, if the bitmap's pixels are those of the English maps: another language's maps keep their own. The message icons' pictures of the weapons and the skull (the prompts' "Hold to swap for" a weapon) are Aerocatia's and Jessie's art from Halo PC Restored (`port/assets/hud/NOTICE.md`), cut and placed by `tools/hud_assets.py` (pictures) at the art's 4x: the game draws those sprites alone from it, each checked on its own cell's texels, so that every language's maps draw them, beside the sheets' own text. The game sizes and places the HUD from its tags as before. `display.high_res_hud = false` turns this off. |
| High-res text | The menus' and HUD's text is drawn with Overpass (`port/assets/fonts`, SIL Open Font License) in place of the maps' bitmap fonts, which are Interstate. `src/text_hires.c` rasterizes each glyph with stb_truetype (`port/third_party/stb`) at the resolution the game draws at, into an atlas that a placeholder bitmap of the game stands for. The game lays the text out from its font tags as before: each font is as tall as its tag's capitals, and narrowed where the tag's letters are narrower than the Xbox maps' (the fonts of a Custom Edition `loc.map`), so that its letters do not run into each other. The menus' titles (the screens' headers and the main menu's items) are pictures of text in the maps, so they are drawn as the high-res HUD is: `tools/title_assets.py` sets each one again in OpenCE, Roger White's public-domain Newtown respaced to match the maps' commercial title typeface (`tools/title_font.py`), at 4x the bitmap's size over its own plate or glow, each letter placed where the map's letter is, in `port/assets/titles`. The postgame carnage report's title is set over a hand-made SVG redraw of its panel (`port/assets/titles/svg`) instead. The A, B, X and Y button icons beside the menus' key labels and in their text ("Press A to Join") are drawn from hand-made SVG redraws at 8x the size of the maps' bitmaps (`port/assets/buttons`, made by `tools/button_assets.py`), in the same sizes and places. `display.high_res_text = false` turns it off. |
| Dynamic lights | `display.dynamic_lights = "enhanced"` (`src/d3d8_device.c`, `game/dynamic_lights.c`; Vulkan's `src/xgpu_spirv.c`, `vulkan/xgpu_vulkan_stream.c`): the frame's lights are a uniform block, and a 32 x 32 x 16 grid about the camera, its cells growing with the distance (out to 128 world units each way), a 128-bit mask of the lights that reach each cell (an integer 3D texture). The structure's lightmap pass and the models' per-pixel lighting (`display.per_pixel_lighting`'s, which this turns on) add the lights of their cell for each pixel, in place of the game's pass over the structure for each light and its two lights for each model. The vertex shaders hand on the world position: a model program's skinned position before it is projected, a structure's vertex as it is. The glows are a camera-facing quad each, projected as the game's programs are, faded against the scene's depth, sampled with the target drawn into without it. With Vulkan the block and the cells are its draw set's, written in the stream's order, and the glows' shaders are the SPIR-V emitter's. Metal draws them alike (`src/xgpu_msl.c`, `port/macos/xgpu_metal_stream.m`): the block and the cells (in buffers: the cells' masks in the texture's order) are copied into the frame's upload ring as the stream sends them and bound for the draws after them, and the glows' functions are the MSL emitter's, the SPIR-V emitter's operation for operation. A view that finds more lights than the 128 lit (512 on the desktop builds and the Mac, 256 on the Steam Frame: `include/halo_port_capacity.h`) lights the 128 that show most, each scored by its brightness times its radius over its distance as it was drawn last frame, those wholly behind the camera last (`game/dynamic_lights.c`, `dynamic_lights_rank`), classic lights too. |
| Anti-aliasing | Off unless `display.anti_aliasing` is set (`src/d3d8_device.c`). FXAA (written in the port) and SMAA (`port/third_party/smaa`, MIT licensed, at its HIGH preset, compiled as GLSL) are passes over the 3D view of each window, after the lens flares and before the HUD and the menus (`render/render.c`). The Vulkan backend runs them, their GLSL (`tools/vulkan_post_shaders.py`) compiled to SPIR-V when the port is changed (`vulkan/xgpu_vulkan_post.c`), its pipelines made when the setting is chosen and as a map is prepared. The Metal backend runs them too, the same SPIR-V written as MSL by SPIRV-Cross (`port/macos/xgpu_metal_post.c`, `tools/metal_post_shaders.py`). Supersampling draws the render targets the size of the screen at two times the resolution in each direction, and the display blit scales them down. Multisampling draws the back buffer and its depth buffer into multisampled renderbuffers, and with them any target that is drawn together with one of them (a mirror's view, in the secondary target with the back buffer's depth buffer), so that the attachments of a framebuffer are all multisampled or none is. A target's pixels are resolved into its texture before something reads the texture (as a texture, or at the display blit). Visibility tests count samples, divided by the samples of a pixel. An alpha-tested surface (foliage, grates) covers the samples of a pixel in proportion to its alpha past the reference (the sample mask). |
| Sound | Xbox DirectSound on SDL3 audio (`src/dsound_sdl.c`): PCM and Xbox ADPCM, mixed at 48 kHz in stereo or 5.1, with volume, pitch, mix bins, distance, pan, occlusion and obstruction, and the Doppler shift of moving sounds (`audio.doppler`); on headphones, the world's sounds through an HRTF (`audio.spatial`, "3D audio"). There are no cones (no sound of the maps has one). On 5.1 the game gets the AC-3 speaker setting of the Xbox and sends its 2D sounds to the center and back speakers, 3D sounds pan across the five speakers, and the reverb plays from the front and back pairs. The game sends nothing to the LFE channel. |
| Input | XInput on SDL3 (`src/xinput_sdl.c`): keyboard, mouse, gamepads with rumble, and the debug keyboard for the console. |
| Files | The Win32 file functions and the MSVC file functions on POSIX, with the translation of Xbox paths. |
| Threads | Threads, events, mutexes, critical sections, interlocked operations and alertable waits. |
| Memory | The port reserves the Xbox memory at `0x80000000`. Thus the game gets the fixed addresses that it expects. |
| Saved games | The Xbox `UDATA` layout, with SHA-1 signatures. |
| Networking | Winsock on BSD sockets. System link on a local network and on the internet. |
| Bink video | Not available. The game skips the movies. |

## How the port operates

### The compiler

The game's sources were written for the MSVC compiler. Each build compiles
them with clang and options that give MSVC's ABI and semantics:

- The Linux builds (as Android and macOS) compile the game into the guest
  image (`generate_guest_image` in `tools/android_build.py`): the
  architecture's target (`x86_64-linux-gnux32` or `arm64_32`,
  `tools/guest_arch.py`), with `GUEST_FLAGS` for every unit (`-fshort-wchar`:
  16-bit `wchar_t`; `-ffp-contract=off`: no fused multiply-add;
  `-femulated-tls`; `-nostdinc`, as the C library is the guest's musl) and
  `GUEST_CODE_FLAGS` for the game and the platform layer
  (`-fms-extensions`: the MSVC extensions; `-fcommon`: tentative
  definitions, as in C89; `-fno-strict-aliasing`, `-fwrapv` and
  `-fno-delete-null-pointer-checks`: no optimisations that assume the
  absence of what MSVC tolerates; `-fno-omit-frame-pointer`).
- The Windows build (`tools/windows_build.py`) compiles for
  `i686-pc-windows-msvc`, which gives MSVC's ABI itself: 16-bit `wchar_t`,
  MSVC's structure layout, `__declspec`, calling conventions and COMDAT
  inline functions ([port/windows/README.md](../windows/README.md)).
- The tests that compile parts of the engine natively in the game's 32-bit
  x86 ABI (the harness, `tools/harness`, and the tag validator's
  `map_validate`: `tools/x86_game_abi.py`) compile for `i686-linux-gnu`
  (`-m32`) with `-fshort-wchar`, `-malign-double` (8-byte alignment of
  64-bit members) and `-freg-struct-return` (small structures and unions
  returned in registers, as on Win32). Nothing built so ships.

The game sees only the ISO C of its C library (`__STRICT_ANSI__`). Thus
POSIX names, for example `random`, do not conflict with the names of the
game.

These files supply the MSVC functions that clang does not have:

| File | Contents |
| --- | --- |
| `include/halo_linux_prefix.h` | The first header of each file: the architecture macros of the SDK, MSVC `__inline`, SEH keywords, `__declspec(selectany)`. |
| `include/` | Headers that add MSVC names to the C runtime headers. |
| `port/include/xdk` | The Xbox SDK declarations. The compiler reads this folder after all the other folders. |
| `tools/linux_msvc_semantics.py` | Makes a header that declares each struct tag at file scope, as MSVC does (`build/msvc_semantics`, for every guest). It also makes the header inline functions weak, as the COMDAT functions of MSVC. `game/msvc_comdat.c` gives one external copy of each. |
| `include/halo_linux_winsock_names.h` | Gives new names to the Winsock functions of the SDK. Thus they do not link to the glibc functions with the same names. |
| `include/halo_linux_source_fixups.h` | Declares the port's functions that the game's sources call. |

### The platform layer (`src/`)

- The files `posix_*.c` use glibc, whose structures have the 64-bit
  host's layout: the host builds them (`tools/linux_guest_build.py`), and
  the guest calls them through import stubs that
  `tools/android_posix_stubs.py` makes from `src/posix.h`.
- The other files include the SDK declarations through `platform.h`. Thus
  the compiler examines each definition against the SDK prototype.
- `src/halo_linker_common.c` gives weak storage for some globals of the
  January link, and for `fast_ftol_C` and `main_crash`.
- `main/d3d_intimacy.cpp` reads a private structure of the Xbox Direct3D.
  The port does not build this file. `src/d3d8_device.c` gives
  `d3d_find_flipcount`.
- The GPU driver cannot open the kernel's `trace_marker`
  (`src/posix_trace_marker.c`, which the host links). SteamOS keeps kernel tracing on for its GPU
  performance captures (`gpu-trace.service`), and its Mesa then writes a
  marker for each traced driver function: on the Steam Frame, some 480,000
  writes a second, which took the game from the headset's 72 Hz to about
  50 frames a second. The Steam Deck runs the same service, but its Mesa
  (25.3) has no markers to write: there the refusal
  changes nothing (measured: the same frame times and power either way).
  `HALO_GPU_TRACE_MARKERS=1` lets the driver write them, to capture with
  gpuvis.

### The renderer

The game draws through the Xbox's Direct3D 8. The renderer has two halves,
so that each graphics API (Vulkan, Metal) needs a backend and no copy of
its own of what the Xbox's states mean:

| File | Contents |
| --- | --- |
| `src/d3d8_device.c` | The front end: the `D3DDevice_*` functions and the device's state. It finds the render targets by the Xbox surface's address and numbers them, keeps the vertex shaders and declarations, the visibility tests' slots, the screen's size and scale, the mirror's pages and the VR mode's passes. For each draw it fills a pipeline key, the dynamic state, the textures bound and the draw's range. |
| `src/xgpu_backend.h` | What passes between the halves. It holds no Xbox value and no graphics API's. Each call is plain data: targets are numbers, guest data is a guest address and a size, and the backend keeps no pointer into the front end's memory. A few calls are answered at once (a texture's description, whether a pipeline's shaders build, a visibility test's count, a screenshot's pixels); the header says how each could wait for the frame's end. So the calls of a frame go to the 64-bit host as one stream ("The renderer's command stream", under "64-bit ARM"). |
| `vulkan/` | The Vulkan backend, the Linux builds' and Android's, in the host: the command stream's backend ([its README](vulkan/README.md)). In the VR mode it holds the eyes' images and the HUD's, and composes the frame for OpenXR (`vulkan/host_vulkan_vr.c`). |
| `src/xgpu_shader.h` | The vertex programs and pixel shaders, decoded once: `src/nv2a_vsh.c` decodes the NV2A vertex microcode, `src/nv2a_psh.c` the texture stages and register combiners. |
| `src/xgpu_spirv.c` | Writes SPIR-V from the decoded programs, for Vulkan. |
| `src/xgpu_msl.c` | Writes MSL from the decoded programs, for Metal. |

The host caches the textures (`src/xbox_textures.c` keeps only the
high-res replacements' lookup and the Custom Edition channel orders), and
the high-res replacements (`src/hud_hires.c`, `src/text_hires.c`,
`src/menu_files.c`) are images (`src/xgpu_images.c`).

### Presenting during map loading

Map loading keeps the game thread's activation order. A loading epoch begins
before the blocking work and presents an animated panel whose resources belong
to the platform, rather than to the map being replaced. Its animation uses
monotonic presentation time; it does not advance game ticks or take random
seeds. Queued rendering is retired before tags or a structure's storage are
released.

The loading panel uses Paul Hontz's redraw of the retail loading picture
(`port/assets/loading`). Its compressed pixels are embedded in every build,
decoded once at startup and copied into the presenter's own storage. The
shared compositor preserves the picture's colors and adds the mask and
feedback glow at 640 by 480. Each backend presents those same pixels. The
animation uses elapsed time so it keeps moving during a blocked load; its
sweep is not a percentage of work completed.

The shared renderer prepares distinct pipeline and texture combinations in
bounded batches. Vulkan and Metal prepare their pipelines without drawing the
unseen enumeration or uploading its transient geometry. The host render thread
services loading presentation between records and while waiting for pipeline
compilation. Metal uses its independent presentation callbacks.

Vulkan can queue up to 512 owned native pipeline descriptions ahead of one
compiler worker. The render thread owns the pipeline table and publishes
completed handles; preparation drains every request before reporting ready.
Ordinary drawing always waits for an actual pipeline handle.

OpenXR uses the same loading epoch and panel. It submits fresh predicted poses
in a world-anchored scene while loading and transfers frame ownership back to
gameplay when its first image is ready. Waiting for the runtime's next frame
does not stop the render thread from consuming preparation work. Backend
presentation details do not change the shared map loader or simulation order.

### Measuring map loading

For a separate visual diagnostic run, set `HALO_LOADING_CAPTURE_DIR` to an
existing private directory. The hosts read back their rendered
loading image twice per load: after its first frame and after half a second,
when the load lasts that long. Files are `loading-<epoch>-<serial>.ppm`.
These are GPU image readbacks; they stall rendering and must never be enabled
in timing runs. Leave the variable unset for normal operation. OpenXR captures
show the loading panel's pixels; runtime pose traces and the submitted quad's
space separately establish its placement. Convert retained evidence to PNG
and remove temporary captures after inspection.

`HALO_LOAD_TRACE=1` records the map's loading phases, file requests, renderer
preparation and presentation boundaries. Use the environment variable for a
guest build so both the game and host record their work. Thread CPU times
belong to the recorded owner; `-1` means that clock is unavailable. Nested
phase times overlap and must not be added. Detailed per-draw timing can
materially slow preparation, so compare normal runs too.
Vulkan's `pipeline_seconds` and published `driver_seconds` sum wall time inside
driver creation calls. That work overlaps enumeration; the sum is neither
thread CPU time nor additional loading latency. Worker `driver.begin/end`
records time the calls, while render-owner `request/published` records track
their lifetime in the preparation queue.

Each loading epoch's presentation summary includes `actual_boundaries`: the
gap from the preceding game picture to the first loading picture, the largest
gap between loading pictures, and the handoff to the first game picture.
Unproven boundaries are `null`. The conservative bracketing metric remains
separate: it can include the interval from the first to the second game frame.
Vulkan and OpenXR timestamps describe submission returns;
Metal uses drawable presentation times, including callbacks received after
the loading epoch ends. These timestamps do not all prove physical display.

On Linux, the loading runner starts each map directly, offline, in a private
directory. It retains logs and JSON evidence and removes its run folders.
For example:

```sh
python3 tools/load_benchmark.py --halo build/linux_x86_64/halo \
    --data assets --out /path/on/disk/load-results --maps bloodgulch a10 b30 \
    --os-cache cold warm --game-cache reused --shader-cache reused
```

The output directory must be new and on a disk filesystem, not tmpfs. Cold
runs evict only the requested map, `ui.map` and private decompressed cache
files, then verify that their pages are absent with `mincore`; warm runs
read them and verify residency. This describes the OS file cache separately
from reusable decompressed maps and shader cache files. Each run starts a
new process. Driver-internal caches may need separate auditing. `--all`
selects all 24 stock maps. Any exception, malformed trace or reported missed
pipeline preparation fails the run while keeping its evidence. `--keep-going`
continues later runs after a failed launch and still exits with failure;
cache-state verification failures always stop the measurement.

The default display is offscreen. `--video-driver x11` selects an explicitly
provided private virtual display for windowed checks. Vulkan present returns
and OpenXR submissions measure application progress, not a physical screen's
refreshes. Metal's callback also records the drawable's actual presentation
time. The first game-frame submission is not necessarily the first playable
frame: a skipped cinematic can immediately load another BSP. Presentation
gaps in the JSON cover the whole run; inspect the map's phase boundaries
when attributing them to loading.

### Determinism

The game's simulation, its ticks, is a function of the map, the game's
random seed and the players' input of each tick: the same game played
again with the same input leaves the same game state after every tick.
What the game takes from outside the input should be only presentation:
what is drawn between the ticks, what is heard, what the texture cache has
loaded. (At different frame rates a10 plays the same; Blood Gulch's
contrails, which a tick adds points to, still part by the frame rate.)

- Random numbers. The global seed (`random()`, the game's own) is set from
  the game's seed at a map's start and taken only in the ticks (and once
  at startup, for the periodic functions' tables). The
  local seed (`local_random()`, which picks the decals, the effects and
  the particles) is set from the game's seed at a map's start too, and is
  two: a taker in a tick takes the ticks' local seed, one between the
  ticks (the particles moved each frame, the camera's shake, the
  renderer, the menus) the frames' (`math/random_math.c`), so the ticks'
  moves only in the ticks and in their order, however many frames are
  drawn between them. The sounds have a seed of their own
  (`sound/sound_manager.c`), and the debug allocator's fill another
  (`cseries/debug_memory.c`): the sound manager picks as the mixer plays,
  by the clock. A speaker's sound is the one exception: the game picks its
  permutation at its start, from a seed of the speakers'
  (`sound/game_sound.c`), as its mouth moves to it.
- Time. The ticks read only the game's time. The speakers' mouths move in
  the ticks, from the ticks since the sound began; the wind steps once a
  tick.
- What the ticks keep. Not the sound manager's indices, which are the
  mixer's (a unit's speech, the first-person weapon's sound); a decal is
  made whether or not the texture cache has its bitmap yet.
- A new network game's seed is the clock's (`debug.random_seed` fixes it).

The determinism check is `debug.determinism_log`: a line a tick in a file
of its own, with a hash of the game state as the tick left it (of each of
its allocations too), the two seeds, every taker of a seed by its call
site (in the tick, between the ticks, on another thread), the clock's
readers in the tick, and a hash of each frame drawn. `debug.frame_time` gives each frame the same time, so
that two runs draw the same frames, and the scripted player
(`debug.test_input`, `bot:<seed>`) plays on the game's ticks. Two runs,
compared by `tools/determinism_compare.py`, first differ where something
besides the input reached the game; it names the takers (from the
executable's symbols, or the guest image's: `build/linux_arm64/halo_guest.elf`)
and the allocations, and `debug.determinism_dump` writes the game state at
a tick for the tool to compare byte by byte (by data array element, by
object), and that tick's first frame as a BMP. For example, a10 twice:

```sh
HALO_DETERMINISM_LOG=determinism.txt HALO_FRAME_TIME=0.0166667 HALO_TEST_INPUT=bot:3 \
    HALO_SKIP_CINEMATICS=1 HALO_EXIT_AFTER=120 ./halo     # with init.txt: map_name levels\a10\a10
python tools/determinism_compare.py a/determinism.txt b/determinism.txt \
    --symbols build/linux_arm64/halo_guest.elf
```

`--simulation` compares only what the ticks do, for two runs at different
frame rates: not the frames, the takers between the ticks, nor the
allocations that the frames move (the particles, the decals' vertices,
the lights, the fog and the sound environment, the motion sensor, the
first-person weapon posed at the camera, the input as the frames take
it). With `walk:<seed>`, a scripted player that
never turns (a turn is the stick's rate over each frame's time, so it is
the same each tick only at one frame rate), a10 at 60 and at 90 frames a
second is the same game. Left out of the hash, as they mean nothing
between their uses: the objects' magic numbers (the collision tests'
marks, which the frames' tests leave too) and their cached render states'
indices (the renderer's), and the free space after the objects' last
block.

### Simulation rate

`game.sim_rate` (`game/sim_rate.c`, `game/sim_rate.h`) gives the game two
clocks. The game's time stays at 30 ticks a second, and everything a tick
does runs once a tick, as before: the AI, the scripts, cinematics,
animations, timers, vehicles, the game engine, saved games and the network.
Inside each tick, after the objects' update (`game/game.c`,
`game_tick_substep`), a fast phase runs 1, 2 or 4 sub-steps (30, 60 or 120),
where the player's movement and aim, projectiles, thrown items, and the VR
mode's throws and grabs move in smaller steps.

- The clock (`game/game_time.c`) runs a sub-step each time a sub-step's
  time (1/60 or 1/120 of a second) has passed, not a whole tick at once, so
  a tick's sub-steps are spread over the frames that pass meanwhile. The
  tick's own work runs with its first sub-step, and the work that follows
  the fast phase with its last, so a tick runs in the order it always had.
  At 120 on a display of 120 Hz that is one sub-step a frame, and each
  sub-step takes the input that its frame read (`game/player_control.c`). A
  frame slower than a sub-step runs the sub-steps that are due, in order.
  At 30 a sub-step is the whole tick, as before.
- Between two sub-steps of a tick, the tick is open. The work of a frame
  that changes the game as a whole (a checkpoint saved, a saved game
  loaded or reverted to, a map changed or reset, a structure switched)
  waits until the tick has ended (`main/main.c`). The clock begins no new
  tick until then. If the clock has stopped (paused, the console), the open
  tick is run to its end at once. A game is saved only between two ticks.
- A frame is drawn between the last two ticks, as far into the next as the
  clock has run, including the open tick's sub-steps that have run
  (`game_time_get_tick_fraction`). `sim_substep_blend` gives the sub-step
  that the frame is in.

- The rate is taken at the start of each tick. It is the setting (60 by
  default) in a game of this machine alone (`sim_rate_follows_setting`): a
  game of its own connection (the campaign) or a split screen game (and so
  the bots' games), a network game of one machine that takes no other
  (`network_game_is_splitscreen_local`), whose sub-steps take each frame's
  input as the campaign's do (`game/player_control.c`). In a
  network game with other machines, every machine must step alike, so the
  rate is the one the game agreed on (`sim_rate_network`): 30 in a Legacy
  game (OpenCE's netcode), and the host's `network.sim_rate` in an OpenH1
  game (OpenH1's own, `game/network_standard.c`; NETCODE.md, "OpenH1 and
  Legacy games"). It is 30 in the playback of a saved film. Meanwhile
  (`sim_rate_locked`) Video Setup > Graphics' TICK RATE row is locked at
  that rate, and its OK leaves the setting as it was
  (`game/menu_functions.c`, `sim_rate_row_show`).
- Velocities and timers in the game state stay per tick. A sub-step takes
  its share with `sim_per_substep` (linear), `sim_per_substep_squared`
  (accelerations), `sim_decay_per_substep` (a factor each tick) and
  `sim_chance_per_substep`. At 30 each returns its argument exactly. At 60 and
  120 the linear and quadratic ones scale by a power of two, which is exact.
- Nothing of a sub-step outlives its tick, other than what a tick at 30
  leaves in the game state. A saved game (taken at the end of a tick) is the
  same at every rate and carries no rate: it loads at the rate the next tick
  takes, the player's setting or a network game's 30. A counter of ticks in a byte or a short (`airborne_ticks`,
  `elevator_ticks`, a held button) counts on the last sub-step only
  (`sim_substep_is_last`), so it does not overflow at 120.
- A subsystem registers its work for a slot (`sim_substep_register`). The
  slots run in a fixed order each sub-step. The frames between the ticks
  can draw a sub-stepped object between its sub-steps' poses
  (`sim_substep_blend`). At 30, that is the tick's own fraction.
- The frames draw the micro lane on its sub-steps' path
  (`game/render_interpolation.c`, the last slot). After each sub-step the
  place of each object of the micro lane is kept, for the tick that the
  frames draw and the next tick, whose sub-steps run meanwhile. A frame in
  sub-step j of the drawn tick draws the object between its places after
  sub-step j - 1 (or the end of the tick before) and sub-step j. The pose,
  its rotations and what the object carries mix between the ticks as
  before, moved with the object. The first-person view on foot, and the
  first-person weapon with it, move in the same way. The frames are still
  one tick after the calculation. The tables are the frames' only: the
  simulation never reads them. A projectile's first two ticks (its head
  start and hold) and the VR mode's grenade let go of in a sub-step are
  drawn between the ticks, as before. The grenade stays where the hand let
  it go until the frames reach the tick of its release. At 30 nothing is
  kept.
- A vehicle that a player drives also turns in the sub-steps, so its frame
  (the place and rotation of its root node) is kept after each sub-step too.
  A frame draws it between two of them: the place is linear, the rotation
  a normalised lerp the short way. Its nodes, and those of what it carries
  (riders, the gunner, a turret, the player in the seat), mix between the
  ticks in the vehicle's frame and go where that frame is. The view from a
  seat (first person or following, flat or VR) hangs on the vehicle at the
  unit's camera position, kept in the vehicle's frame at each tick. The
  view's snapshot is moved back to where the tick left that point, and the
  frames move it with that point onto the vehicle's path.
- The profiling build times the fast phase as `game_tick.substeps`.
- The determinism check writes the rate (`rate 60`) before the first tick.
  `tools/determinism_compare.py` compares two logs of different rates
  only with `--across-rates`. Make a run to compare against a build
  before the setting, or against master as the scorecard's gate does, at
  30 (`HALO_SIM_RATE=30`): the default is 60.

### Game source changes

Five files of the game have changes for clang. These changes do not change
the MSVC objects: a comparison of all 612 C objects showed no difference in
code or data.

| File | Change |
| --- | --- |
| `cseries/cseries.c` | The naked function `stristr` uses `[ebp+8]` and `[ebp+12]` for its parameters. |
| `bitmaps/bitmap_drawing.c` | `*((word *)p)++` is now `*(*(word **)&p)++`. |
| `rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.c` | `&(T *)x` is now `(T **)&x`. |
| `hs/hs.c` | Local prototypes that did not agree with `ai_script.h` are removed. |
| `units/vehicles.c` | The local prototype of `unit_update_animation` uses the type of `units.h`. |

`math/real_math.h` had a copy of `plane2d_from_points` that did not agree
with the function in `effects/decals.c`. clang used the copy, and parts of
levels were not visible. The copy now agrees with the function.

Other changes:

| File | Change |
| --- | --- |
| `scenario/scenario.c` | The BSP connection tables have names, not MSVC offsets. |
| `rasterizer/xbox/rasterizer_xbox_environment_fog.c` | A local pointer gets its value from the file-scope array with the same name. |
| `game/player_control.c` | The mouse aims the player on controller 1 directly, its zoomed turn as `input.zoom_sensitivity` chooses (`view_fov.c`). The touch controls' swipe and gyroscope come since the frame's input are read again for the drawn view, as the input would turn the player by them (`player_control_touch_look_late`, `display.direct_camera`). |
| `sound/game_sound.c` | The game calculates the obstruction of each sound one time for each tick, not for each frame. |
| `sound/sound_manager.c` | A track of a looping sound that is gone stops, even in the millisecond it started (`refresh_sound`). The Xbox never drew three frames in one millisecond; the port can (the null renderer, no frame limit), and then read the deleted looping sound and crashed. |
| `cseries/errors.c` | `debug.txt` stays open between lines. A line, or a run of up to four lines, that comes twice in a row is counted, not written again, until another line comes or the game ends (`last line repeated N times`, `last 2 lines repeated N times`). At 64 MB, `debug.txt` becomes `debug.old.txt` and a new `debug.txt` starts. |
| `networking/`, `game/`, `interface/`, `bungie_net/network/` and the pools of objects, effects and sounds | The system link limits and the memory for them. |
| `game/`, `objects/`, `units/`, `networking/` | The distributed netcode. Refer to `NETCODE.md`. |
| `cache/cache_files.c` | When a map's tags load and unload, the port finds the bitmaps that the high-res HUD replaces (`game/hud_hires_tags.c`), adds the tags of the menus to the menus' map (`game/menu_tags.c`), and plays the mods' tags (`game/mods.c`). |
| `cache/cache_files_windows.c` | Each cache slot (`z:\cacheNNN.map`) remembers the size and modification time of the map file it was copied from, in `z:\cacheNNN.source` (`src/xbox_files.c`). A slot whose map file has been replaced since, by a modded map that keeps the header's checksum, another region's or a corrected one, is copied again, at the start or the next time the map is loaded. The Xbox's disc never changed, so the name and checksum in the header were enough. |
| `cache/xbox_sound_cache.c`, `cache/xbox_texture_cache.c` | The samples of a mod's sound are copied from memory, and the pixels of a mod's bitmap read from its file and laid out for the hardware, at once (`game/mod_sounds.c`, `game/mod_bitmaps.c`); a mod's linear bitmap need not have rows of whole steps of the pitch. |
| `hs/hs.c` | The console's `mods_reload`, `mods_enable` and `mods_list`, which a client may run too. |
| `interface/ui_widget.c`, `interface/ui_widget_event_handler_functions.c`, `interface/ui_widget_game_data_input_functions.c` | The main menu is the PC version's from `port/assets/menus` (`display.menus`); the menus' widgets can call the port's functions (`game/menu_functions.c`) and send the PC version's custom activation event; the widgets' memory is 256 KB, not 16 KB; the main menu and Multiplayer clear co-op's controllers, so that a gamepad going back to controller 1 is not a controller unplugged; the lobby's split screen players leave alone, and one who quits in game is not joined to the next game (`interface/player_ui.c`).; Multiplayer's PLAY WITH BOTS hosts a game that takes no other machine, as the Xbox's split screen does (`ui_widget_port_host_local`); the port's own screens are laid out for the view's width up to 16:9 (their widgets' `wide`: `game/menu_tags.c`'s `ui_widget_port_wide_layout`), a picture widened by stretching its middle (`draw_bitmap_widened_in_rect`); after a game of this machine alone the host's map select and the lobby open in place of the Xbox's split screen Select Map and Select Teams (`game/menu_tags.c`'s `pc_menus_screen`). |
| `interface/player_ui.c` | Fast setup opens the pregame screen through the port's menus (the lobby), as `interface/ui_widget.c` does. Player 1's last used profile (`lastprof.txt`) is looked for once, not each time the menus and the party ask (each frame), which logged its absence again each time. |
| `game/game_engine.c`, `game/game_engine.h`, `game/game_engine_slayer.c`, `game/game.c`, `game/aim_assist.c`, `game/players.c`, `objects/damage.c`, `objects/objects.c`, `units/units.c`, `items/weapons.c`, `saved games/playlist_profile.c`, `saved games/saved_game_files.c` | OpenCE's gametypes (`game/game_modes.c`, "OpenCE gametypes"): the gametype's PC options carry a mode and Hardcore with their options; the built-in ones are written after the Xbox's default gametypes; Infection's and Gun Game's game engines are Slayer's with their rules; SWAT's headshots, Hardcore's motion sensor, aim assist and restore of the killer; Infection's zombies' skulls, grenades, powerups, seats and melee. |
| `game/game.c` | Each tick takes its rate (`game.sim_rate`), and runs the fast phase's sub-steps after the objects' update (`game/sim_rate.c`; "Simulation rate"). |
| `game/game_time.c`, `main/main.c` | With `debug.fixed_step`, a game of one machine runs a step a frame (a tick, or a sub-step of `game.sim_rate`), each frame taking a step's time, so that it plays the same every time (`tools/stagekit`). |
| `game/game_time.c`, `items/projectiles.c`, `items/weapons.c`, `main/main.c` | `debug.trace` (`game/sim_trace.c`, `tools/scorecard`): a call after each simulation step, a call each where a projectile is made, strikes and detonates and a weapon fires, and one as each frame takes the first player's view, made only while the trace is on (`SIM_TRACE`). |
| `game/game_time.c`, `items/weapons.c`, `main/main.c`, `interface/ui_widget.c`, `game/player_rumble.c` | `debug.latency_log` (`game/latency_log.c`): a call around each simulation step, where a weapon fires, as each frame begins, is drawn and is handed on, where the menus take a press and where a rumble impulse is made, made only while the log is on (`LATENCY_LOG`); the controller's and the mouse's readings and the motors' writes call it from `src/xinput_sdl.c`. |
| `game/player_rumble.c`, `game/game.c`, `input/input_xbox.c` | The rumble is written at the end of each simulation step and when a vehicle's changes, on the game's thread, as the motors change and every 33 ms while they run, not 30 times a second by the vertical blank's thread a tick after the step; an impulse's time counts from the step that made it. With the profile's vibration off, the motors stop. |
| `interface/ui_widget.c` | A spinner's arrow is lit for 15 ticks of real time, not 15 frames. |
| `input/input_abstraction.c`, `game/player_control.c`, `game/players.c`, `game/player_queues_new.c`, `units/units.h` | The keyboard and mouse's actions (`src/xinput_sdl.c`, `include/halo_keyboard.h`) join controller 1's game controls; their reload key reloads on its own, and their action key only acts (a control flag of the port's, sent with the player's action, stops the reload the controller's X falls back to). |
| `saved games/player_profile.h`, `interface/player_ui.c` | A sixth button layout, the Master Chief Collection's Bumper Jumper (`profile.button_preset` 5), and a seventh, Fishsticks (6: zoom on the left trigger, melee on the right stick's button, grenade throw on the right bumper, crouch on B, grenade switch on the pad's left, which then does not move you), and an eighth, Recon (7: the standard buttons with action and reload on the right bumper, grenade switch on the left bumper, flashlight on X). A layout the build does not know (a later build's) gets the standard buttons, not none, which moved start and back and stopped the game. |
| `sound/sound_manager.c`, `interface/hud.c`, `game/game_engine.c` | The music's and the other sounds' volumes; the HUD's and the scoreboard's settings are read again when Settings changes them. |
| `rasterizer/xbox/rasterizer_xbox_shadows.c` | With larger shadow maps (`display.shadow_resolution`), the blur's taps stay half a texel apart, and more passes widen it to cover the same part of the map as on the Xbox. |
| `rasterizer/xbox/rasterizer_xbox_shadows.c`, `render/render_objects.c`, `rasterizer/rasterizer.h` | The shadow atlas: a frame's shadow casters (up to 9, with maps up to 512) are drawn into regions of an atlas first, the blur's passes run once over all of them (one quad a region, the guard bands between them black, as a map's border reads), the last into a map of each region's own, and then each object's receivers are drawn sampling its map. A frame's N shadows take 6 + N passes at 512 rather than 7N; they look the same. More casters, 1024 maps and the debugging options draw them one at a time as before. |
| `rasterizer/rasterizer.c` | The 128 KB of vertices of the debug display of a model's vertices are static, not on the stack: the compiler inlines the function in `render_model`, and the Windows build probed the stack a page at a time for every model drawn. |
| `render/render.c` | The 3D view of each window is tone mapped (`display.hdr`) and antialiased (`display.anti_aliasing`) before the HUD is drawn. The projectiles' lights' glows are drawn after the transparent geometry (`display.dynamic_lights`). |
| `render/render.c` | `render_initialize` reads the structure culling tests from `display.structure_culling`. |
| `models/models.c` | A model's size, which chooses its level of detail and whether it is drawn, is scaled as `display.model_detail` says (`halo_model_detail_scale`, `src/d3d8_device.c`)., and each permutation draws the level `game/model_detail.c` chooses from the model's (stock: the model's). |
| `interface/hud.c` | In multiplayer, players' names are drawn above their heads (`display.player_names`, `display.player_name_scale`), and fade out while the scoreboard shows. |
| `game/game_engine.c` | The carnage report and the full-screen scoreboard are laid out as Settings is: the report's one table of the players' statistics with the teams' totals, the result and the game's line; the scoreboard's rules, lit row and footer, and no ping column in a game of one computer. Both are as wide as the menus' wide layout: the report's panel and columns across the screen up to 16:9, the scoreboard's names wider. |
| `text/draw_string.c`, `interface/terminal.c`, `interface/virtual_keyboard.c` | Streamer mode's words are hidden in every string drawn or measured (`game/text_filter.c`), but in the console's line and the virtual keyboard's text being typed. |
| `rasterizer/rasterizer_text.c`, `text/draw_string.c` | Text is drawn from an atlas of the fonts' glyphs, rasterized at the resolution the game draws at (`src/text_hires.c`), when the font has every character of the string. Text can be drawn scaled about a point (`rasterizer_text_set_scale`), as the players' names are. Each glyph's advance is centred on the font tag character's, so the layout is the same, and a glyph is cut at a text box only where the font tag's character visibly was. |
| `interface/first_person_weapons.c` | The first-person hands are drawn with the unit's shield modifier shader, as its body is (`display.viewmodel_shield`). |
| `interface/first_person_weapons.c` | With the player's own view drawn from the newest step (`display.direct_camera`), the gun's animations are sampled at the frame's time: the state and moving animations between their frame and the next, a state's blend in by its fraction, the sway's springs where their velocity has them (`game/render_interpolation.c`, `render_interpolation_first_person_phase`). |
| `render/render_objects.c`, `objects/objects.c` | In first person, your own body is drawn from the waist down with the pose `first_person_legs.c` makes (`display.first_person_legs`); the waist it found and the mesh `object_mesh.c` read are forgotten on a new map. |
| `objects/object_lights.c` | Glows the port lights itself (`shield_glow.c`): dynamic lights attached to an object's node, colored and sized each frame by their maker, drawn with a light definition the map has. |
| `render/render_objects.c`, `interface/first_person_weapons.c`, `rasterizer/xbox/rasterizer_xbox_plasma_energy.c`, `items/projectiles.c` | An overshield as `overshield_effect.c` draws it (`display.overshield_effect`): in its own color, its intensity full, its energy slower, faded above the height it has risen to; a hit on it plays the projectile's effect for an Elite's energy shield in place of its own (only the effect: the projectile's response is its own). |
| `objects/object_lights.c`, `rasterizer/xbox/rasterizer_xbox_environment.c` | A light attached to an object is drawn where the object is drawn between ticks (its marker as drawn), not where the tick placed it: it no longer steps behind a bolt at 30 Hz (the light's own position, which the units' illumination reads, is kept). With `display.dynamic_lights = "enhanced"`, the frame's lights go to the renderer (`game/dynamic_lights.c`), a projectile's light that is only its lens flare's lights too, a muzzle flash's is moved to the first-person weapon, the structure's own pass for each light is left to the lightmap pass (but spotlights'), which tells the renderer when it is drawn. |
| `render/render_objects.c`, `interface/first_person_weapons.c`, `rasterizer/xbox/rasterizer_xbox_plasma_energy.c` | A local player's shield plasma is drawn in the chosen armor color (`display.shield_color`, `shield_color.c`), on the body and on the first-person arms; in the VR build, the plasma over the weapon in its holster in its highlight's color, close to the weapon (`vr_holster.c`). |
| `objects/object_lights.c` | A light attached to an object is as bright, and of the colour, that the object's function and change colour are between ticks, and an effect's light (an explosion's, an impact's) fades at the frame's time, not a step a tick (`game/render_interpolation.c`); the light's own colour and radius, which the game reads, are the tick's. |
| `objects/objects.c`, `render/render_objects.c` | A projectile deleted in a tick is drawn through the frames after it, on to where it was deleted (`game/render_interpolation.c`), with the lighting it was last drawn with. |
| `game/game_engine.c`, `game/game_engine_ctf.c`, `game/game_engine_oddball.c`, `game/game_engine_slayer.c` | A goal over an object (a flag, the ball, a carrier, a target) notes the object, and its marker is drawn over the object as drawn between ticks (`game_engine_get_goal_drawn_position`); the goal's own position is the tick's. |
| `interface/motion_sensor.c` | The motion sensor's newest blips are drawn from where their units and goals, and the player, are drawn between ticks (a copy of the drawing's own); the blips' history is the ticks'. |
| `render/render_sprite.c` | In the VR mode's eyes, sprites that face the viewer (smoke, explosions, sparks, glows) face the centre of the head and stay upright in the world, where they were parallel to the view's plane and rolled with the head (`vr_render_eye_pass`); near straight above or below the head, upright toward a fixed axis of the world. A build whose largest sprite is at least 2 degrees wide on the screen is marked to be drawn thick ("Effects with depth"). |
| `rasterizer/xbox/rasterizer_xbox_transparent_geometry.c` | In the VR mode's eyes, an effect shader's group is drawn soft against the scene's depth, and thick where its sprites are large (`halo_vr_effect_depth`), but the first-person weapon's and one drawn without depth. A z-sprite (a bullet impact's puff) is drawn soft, its fade measured from the puff's own depth, which is its depth test (`vulkan/README.md`, "The VR mode's effects"). |
| `rasterizer/xbox/rasterizer_xbox_text.c`, `rasterizer/rasterizer_text.c` | The characters of a string are one draw, not a draw each (and a second for the drop shadow). The batch is drawn early before a character is cached and before the high-res glyph atlas starts over (`src/text_hires.c`). |
| `rasterizer/xbox/rasterizer_xbox_decals.c` | Decals drawn one after another with the same blend function and bitmap are one draw, with each decal's color in its vertices, not a draw each: a long fight leaves hundreds in view. Within a run of decals whose blend functions commute (multiply, add, min, max, reverse subtract), they are drawn by bitmap, so more of them share a draw. |
| `rasterizer/xbox/rasterizer_xbox_widgets.c`, `objects/widgets/light_volumes.c` | A light volume's sprites (a plasma bolt's glow, a couple of hundred) are one draw. |
| `rasterizer/rasterizer.h` | A frame holds 2048 transparent geometry groups, not 384 (`include/halo_port_capacity.h`): a co-op game with many added enemies asked for more, and the Xbox's pool drew the frame without the rest. |
| `structures/structure_render.c` | The structure's visible surfaces are put in order by a radix sort, not `qsort_4byte`: the same order. |
| `cache/cache_files.c`, `networking/network_client_manager.c`, `networking/network_server_manager.c`, `networking/network_server_message_handler.c`, `main/main.c`, `interface/interface.c` | Map sharing (`game/map_share.c`): a client that does not have the game's Custom Edition map asks the host for it instead of leaving, is not given time to precache it until it has it, and adds its players to a game in progress only then; the host gives the game's map its own name and its version (its hash's start), takes the requests from any machine in the game, lobby too, and does not drop a machine that downloads; the transfers run each frame, and their progress is drawn over the screen. |
| `cutscene/cinematics.c` | A scenario's titles (some Custom Edition maps show their name for the whole game) are not drawn over a menu, as the letterbox is not: the screen's overlays are drawn after the players' menus. |
| `tag_files/tag_groups.h` | `TAG_BLOCK_GET_ELEMENT` returns an element in the block without a call, and calls `tag_block_get_element_with_size` for anything else. |
| `game/players.c`, `game/player_queues_new.c`, `networking/network_server_manager.c` | Bots (`game/bots.c`, `bots.count`): the lobby of a Slayer game this machine hosts takes the bot players, each the one player of a machine of its own that no real machine is (joining machines are numbered below them), and every tick their actions come from `game/bots.c` instead of their empty queues, into the update the host relays (`update_server_take_bot_action`). A network game's bots make room for the players who join (`bots.make_room`), told the other machines by the game's own messages; the advertisement and the listing count the players apart from them. |
| `input/input_xbox.c`, `physics/collision_usage.c` | The clock is read only for a button held, and for collision tests only when their times are shown (a read of the clock was slow in a browser, for the web port). |
| `math/random_math.c`, `game/game.c`, `game/game_time.c` | The local seed is set from the game's at a map's start, and is two: the ticks' and the frames' ("Determinism"). The seeds' takers and the game state's allocations go to the determinism check (`game/determinism_log.c`). |
| `cseries/debug_memory.c` | The debug allocator fills a new allocation from a seed of its own, not the game's local seed. |
| `networking/network_game_globals.c` | A new network game's seed takes the clock's too, as the local seed is the last map's; `debug.random_seed` sets it. |
| `render/render.c`, `rasterizer/xbox/rasterizer_xbox_decals.c` | The frames are counted from the map's start, as the game state's caches stamp them: the menus' and the loading's frames, as many as the loading took, made two runs of one game differ. |
| `scenario/scenario.c` | A location's last field is set, not left as the stack had it. The wind steps once a tick, at the tick's end, not by the frames' time. |
| `sound/sound_manager.c`, `sound/sound_definitions.c` | The sounds' random choices take a seed of their own. The speakers' mouths are not moved from the mixer's place in the sound. |
| `sound/game_sound.c`, `units/unit_dialogue.c` | The speakers' mouths move in the ticks, from the permutation the game picks at the speech's start and the ticks since. A unit does not keep the sound manager's index of its speech, and a scripted sound's time ends when it is stopped, whether or not the mixer played it. |
| `main/main.c` | Each frame can run a set time (`debug.frame_time`). |
| `effects/decals.c` | A decal is made whether or not the texture cache has its bitmap yet (the bitmap is asked for; drawing the decal loads it). |
| `interface/first_person_weapons.c` | The first-person weapon's sound's index is kept beside the game state; the weapon's state holds the sound's definition. |
| `cseries/cseries_windows.c` | The clock's readers in a tick go to the determinism check. |
| `main/main.c`, `interface/hud.c`, `interface/hud_unit.c`, `interface/hud_draw.c`, `game/game_engine.c` | The text chat of network games (`game/network_chat.c`): its keys are read each frame, and the debug keys are not while a line is typed; each view's HUD draws its lines, above the motion sensor, whose place the HUD notes (the left of the last picture drawn); the scoreboard's menu has Mute voice and chat, and the host's Mute for everyone. |

The x86 inline assembly of the game is replaced by C. Thus the compiler
can optimize that code for each processor:

| File | Assembly | Replacement |
| --- | --- | --- |
| `cseries/cseries.h` | x87 `fistp` (`fast_ftol`) | `__builtin_rint` |
| `bitmaps/bitmaps_inlines.h` | x87 conversions | C conversions |
| `math/matrix_math.c` | SSE `matrix4x3_multiply` | a C loop |
| `effects/decals.c` | an x87 conversion | a C conversion |
| `cseries/profile.c` | `rdtsc` | `QueryPerformanceCounter` |
| `cseries/cseries.c` | naked `stristr` | a C `stristr` |
| `cseries/stack_walk_windows.c` | a read of EBP | `__builtin_frame_address` |
| `interface/hud_draw.c` | a read of `[ebp+4]` | `__builtin_return_address(1)` |
| `bink/bink_playback.c` | `int 3` | `__builtin_trap` |

The x87 control and status words (`_control87`, `_statusfp`, `_clearfp` in
`src/msvc_crt.c`) use `fenv.h`. On AArch64 (Android, Linux arm64), they use
the FPCR and FPSR; on x86-64, MXCSR and the x87 control word.

Some of the game's declarations have their definitions' types, which the
web port's WebAssembly needed (it checks each call's type); they stay.
