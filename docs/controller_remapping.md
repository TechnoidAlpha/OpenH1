# Controller button remapping

Settings > Controls > Controller > **Button Remapping** edits separate P1–P4 layouts. These belong
to input slots on this installation, not device serial numbers or player profiles.
Empty settings keep the profile preset and stock shortcuts.

Choose Movement, Combat, General, Communication or Debug Camera on the SHOW row. Select an action, release
the opening press, then press the desired button on that player's controller.
All four D-pad directions and Back/Select are available, and the back paddles of a
controller that has them (an Elite's P1 to P4, a DualSense Edge's, the Steam Deck's
L4, L5, R4, R5 with Steam Input off): left and right step only through the paddles
the controller has. Start/Options and Escape cancel capture. Button names follow the
detected controller family (Xbox, PlayStation, Switch, Steam Deck). Stick axes, sensitivity and menu navigation keep their settings.

Left/right also cycles assignments, including **UNBOUND**. The menu's X action
(Square on PlayStation, Y on Switch) clears the selected action; keyboard Delete
clears it during capture. Assigning an occupied button names the affected action
in the panel and waits for A or Enter to replace it, or B or Escape to cancel.
Replacing clears the previous action; it never swaps the two. Multiple actions may
be unbound, including every action. Start remains available to open Pause.

The page's OK saves pending edits for all four slots. Cancel discards them.
Defaults restores the selected slot's profile preset. Use / Reload remains combined.
General includes Flashlight, Zoom, Scoreboard (hold) and Screenshot (one press).
Movement buttons use forward/backward/strafe directions independently of stick
presets, with the keyboard controls' diagonal and crouch-walk speeds.

Communication includes Chat, Team Chat, Vehicle Chat, Push to Talk and Ping
(port/linux/README.md, "Pings"). Ping's default is D-pad Right, the stock layouts'
ping, which a new custom layout takes from Strafe Right (left unbound); an older
layout keeps its D-pad Right for its strafe, and Ping starts unbound there.
Chat opens on the channel that player chose last (`network.chat_channels`, as the
keyboard's T does); Team Chat and Vehicle Chat open on their own channel.
Chat opens once per press and belongs to the local player who opened it: their
viewport, name, team and vehicle. The same controller drives the on-screen keyboard.
Only one line can be edited at a time. A departing or disconnected owner cancels it.
Chat remains subject to the game's existing multiplayer and host chat settings.
Voice uses the existing shared microphone and first player's voice routing; any
slot can hold its assigned Push to Talk button. Enable push-to-talk in voice settings.
Keyboard and touch retain their own bindings.

Debug includes Camera Speed Up/Down, Camera / Player Switch, Camera Raise/Lower
and Cycle Camera. These reuse buttons independently of gameplay. Hold Cycle Camera
for one second to advance through the director's views: first person, flying,
following, then first person again during normal play. Release between view changes.
Its default is RB/Black. The existing `director_camera_switch_fast` debug setting
changes it to one cycle per press. The director still refuses cycling for clients
in another machine's game; remapping does not bypass that restriction.

The other debug actions apply only to an already available flying, orbiting or
editor camera. Camera / Player Switch changes whether input controls the camera or
player. While controlling the player with a debug camera available, only the debug
switch takes priority over gameplay. While controlling the camera, the whole debug
layout takes priority. Defaults match stock camera controls: D-pad Up/Down,
Right Stick, RT and LT. All can be changed or cleared. Cycle Camera also works
while controlling the player; its button can still perform its gameplay action,
just as the stock RB shortcut does. Clearing Cycle Camera disables custom camera
cycling, including the old shortcut through the translated gameplay buttons.
Menus and chat continue to block custom camera input in either mode.

Gameplay prompts use the physical binding, or **UNBOUND** for a cleared action:
the HUD's messages, the help screens' text (`ui_widget.c`) and, played with a
gamepad, the VR Tutorial's steps. The keyboard's prompts (`input.button_prompts`)
keep naming its keys, and the menus' own A and B (the help screens' "Press A to
continue") stay the stock buttons, as the menus do.

## In OpenH1

- **What is remapped**: an SDL gamepad's physical buttons, before the keyboard,
  the mouse, the touch controls and the scripted player (`debug.test_input`) are
  merged into the first controller. The game, the netcode, the bots and the
  recorded and scripted input see actions as they always have: the remapped pad
  is read through the profile's preset like any other, and what a player does is
  sent as before. Nothing is sent that was not, so the network version is not
  raised. A sub-step's reading at 60 and 120 Hz is the frame's remapped one.
- **VR**: the controllers in the hands are not remapped. Their buttons are the
  OpenXR runtime's binding, and the VR gestures (a swing's melee, a grip's
  holster) press buttons of the profile's layout. The VR prompts and the VR
  Tutorial name them through the profile's preset; a gamepad played in VR is
  remapped as anywhere else.
- **Overlays**: with a custom layout, what OpenH1's overlays read of the pad
  passes through as it is (`controller_remap_reserve`): a vote's D-pad left and
  right, a Discord ask's D-pad up and down, the scoreboard's D-pad and X while it
  offers its player menu, all of it while it holds the controls. Those buttons
  press no action meanwhile.
- **Sticks**: the profile's STICK LAYOUT swaps the sticks (Southpaw). INVERT LOOK
  (the profile's) inverts up and down; INVERT TURNING
  (`input.invert_look_horizontal`) inverts side to side for every controller.
  STICK DEAD ZONES (`input.left_stick_dead_zone`, `input.right_stick_dead_zone`,
  OpenCE#526) sets each stick's dead zone, at the frame's reading and at every
  sub-step's.

## Reading route and integration

1. [controller_remap.c](../port/linux/src/controller_remap.c) owns tokens,
   parsing, labels, defaults and conflict resolution. Configuration is cached by
   `config_changes`; invalid lists fall back to the profile without partial writes.
2. [controller_remap_menu.inl](../port/linux/game/controller_remap_menu.inl)
   owns pending edits, replacement proposals and capture, on a page of Settings
   (`BUTTON_REMAPPING` in [port_settings.py](../tools/port_settings.py), written
   with `python3 tools/port_settings.py`); `menu_functions.c` calls it from the
   page's update, panel, OK, Cancel and Defaults.
3. [controller_remap_input.c](../port/linux/src/controller_remap_input.c)
   reads physical buttons before keyboard/touch merging. It translates the original
   ten gameplay actions into the profile's buttons, preserving pressure and hold
   timers. Additional commands use semantic action bits, never synthetic key presses.
   Capture, gameplay, menus and camera contexts have independent release guards.
4. [controller_remap_game.c](../port/linux/game/controller_remap_game.c) maps
   physical ports to local players, reads profile and camera context, supplies
   digital movement and adjusts a temporary copy of prompt preferences. Its main-menu
   guard is separate from per-player widgets: the UI map has a live player, while
   the main-menu root belongs to any player (`NONE`). In OpenH1 any screen of
   `NONE`'s gives way too (`ui_widgets_port_active_for_everyone`): a network
   game's postgame and pregame screens, an error. Pause menus retain their
   per-player ownership so other split-screen players can continue playing.
5. The minimal hooks in [xinput_sdl.c](../port/linux/src/xinput_sdl.c) provide
   physical input, per-port chat navigation and push-to-talk. Chat's existing
   [network_chat.c](../port/linux/game/network_chat.c) editor retains its normal
   host validation and packet format while recording its local owner. No network
   version change is needed: existing chat packets already identify their sender,
   and the host already validates that sender against the sending machine.
6. Small `port:` hooks in [director.c](../source/camera/director.c) supply camera
   availability and map cleanup, and pass the custom cycle request to the existing
   camera selector. `controller_remap_camera_cycle` owns its physical-button timing
   in the input module; the director owns camera selection and permissions, which
   cannot be implemented solely in that module. No map-family selector is changed.
   [input_abstraction.c](../source/input/input_abstraction.c)
   applies digital movement; [hud_messaging.c](../source/interface/hud_messaging.c)
   maps prompt identity and renders UNBOUND. Those private engine states cannot be
   accessed solely from a feature module. No map loading path is changed.

## Persistence and lifecycle

`input.controller_p1` through `input.controller_p4` default to empty strings.
New custom layouts use `v4:` followed by 27 comma-separated entries in this order:

```
jump, grenade type, use/reload, weapon, melee, flashlight, grenade, fire, crouch,
zoom, scoreboard, screenshot, forward, backward, left, right, chat, team chat,
vehicle chat, push to talk, camera faster, camera slower, camera/player switch,
camera raise, camera lower, cycle camera, ping
```

Ping is last so that a `v3:` layout (26 entries) is a `v4:` one's start; it is a
gameplay and communication action (its button is unique among them), not the
debug camera's.

Entries are physical tokens: `south,east,west,north,rb,lb,lt,rt,ls,rs,dpad_up,
dpad_down,dpad_left,dpad_right,back,left_paddle,right_paddle,left_paddle2,right_paddle2`,
or `unbound`. The paddles are SDL's left and right paddle 1 and 2 (an Elite's P3, P1,
P4, P2; the Steam Deck's L4, R4, L5, R5): they are not the Xbox pad's, so only a custom
layout's actions take them, and a prompt names an action on one in words ("L4") where
the HUD has no icon for it (`controller_remap_prompt_text`). South means A on Xbox, Cross on
PlayStation and B on Switch. Tokens must be unique within gameplay/communication
and separately within Debug; `unbound` may repeat. Unbound saves remain distinct
from an empty setting (profile defaults).

Old ten-action, 25-action `v2:` and 26-action `v3:` strings remain readable; Ping
takes D-pad Right only where no older action has it. The appended Cycle
Camera defaults to RB unless an old debug action already owns RB, in which case
Cycle Camera starts unbound and can be assigned in the Debug group. Existing
assignments are preserved. Ten-action strings' original actions take priority over
new defaults, including Recon's use of D-pad Left. P1 retains the Up chat shortcut;
other slots default to Up movement. Additional chat channels, voice and screenshot
start unbound. Saving writes the new format. Custom digital movement now has the
explicit directions above; stick presets still govern the analog sticks.

Disconnects, focus loss, menu/chat transitions, layout changes and map cleanup
release transient action states. Buttons held across a transition must be released
before they trigger gameplay again. Capture has a watchdog for an abandoned menu.
Clearing a layout never changes another player's bindings or a saved player profile.

## Verification

[Controller tests](../tools/test_controller_remap.py) execute production C for
parsing, migration, replacement, cleared saves, all stock presets, per-player input,
pressure, capture, disconnects, focus and context changes, semantic commands,
prompt identity and pending menu state. Chat ownership and hold-over checks use
the real production functions.
[Context integration tests](../tools/test_controller_remap_context.py)
combine the real widget-ownership check, remapper and director input for main-menu
navigation, pause, camera cycling, alternate bindings, timing and lifecycle guards.
Tests distinguish those contracts from physical driver behaviour. Runtime
screenshots and Xbox/CE gameplay checks complement the tests;
real-controller playtesting is still needed for device-specific behaviour.
