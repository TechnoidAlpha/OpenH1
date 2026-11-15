/*
HALO_KEYBOARD.H

The keyboard and mouse's own controls (port/linux/src/xinput_sdl.c): in the
game they do not drive the first controller, as they do in the menus, but
the player's actions themselves, by the bindings in config.toml's
[controls] (Settings > Controls Setup changes them). The game takes them
with the first controller's (source/input/input_abstraction.c), so the
profile's button layout is the controller's alone, and reloading has a key
of its own (source/game/player_control.c).
*/

#ifndef HALO_KEYBOARD_H
#define HALO_KEYBOARD_H

enum halo_keyboard_action
{
	HALO_KEYBOARD_MOVE_FORWARD,
	HALO_KEYBOARD_MOVE_BACKWARD,
	HALO_KEYBOARD_STRAFE_LEFT,
	HALO_KEYBOARD_STRAFE_RIGHT,
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_RELOAD,
	HALO_KEYBOARD_ZOOM,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_FLASHLIGHT,
	/* (these two press the controller's Back and Start, which the menus
	read from it) */
	HALO_KEYBOARD_SCOREBOARD,
	HALO_KEYBOARD_PAUSE,
	HALO_KEYBOARD_SCREENSHOT,
	/* (Save Report's, which the game itself does not read: port/linux/src/
	report.c) */
	HALO_KEYBOARD_REPORT,
	/* (voice chat's, which the game itself does not read:
	halo_push_to_talk_held) */
	HALO_KEYBOARD_PUSH_TO_TALK,
	/* (the text chat's, which the game itself does not read: a line to
	everyone, or to the team: port/linux/game/network_chat.c) */
	HALO_KEYBOARD_CHAT,
	HALO_KEYBOARD_TEAM_CHAT,
	/* (a party invite's, which the game itself does not read: accepting
	the newest one a player of the game sent, port/linux/game/network_party.c) */
	HALO_KEYBOARD_PARTY_ACCEPT,
	/* (the players' votes', yes and no in the vote running: port/linux/game/
	network_votekick.c, read with the chat's keys) */
	HALO_KEYBOARD_VOTE_YES,
	HALO_KEYBOARD_VOTE_NO,
	/* (the clip key's, which the game itself does not read: the demo being
	recorded marked, port/linux/game/demo.c) */
	HALO_KEYBOARD_CLIP,
	/* (a ping's, which the game itself does not read: what the player aims
	at marked for their allies, port/linux/game/network_markers.c) */
	HALO_KEYBOARD_PING,
	/* (walking's: while it is held (or toggled on, input.walk), the movement
	keys push the player a part of the way, as a stick pushed a little does,
	slowly enough by default to stay off the enemies' motion trackers:
	source/input/input_abstraction.c, port/linux/game/keyboard_walk.c) */
	HALO_KEYBOARD_WALK,
	/* (the HUD's Spartan link's, which the game itself does not read: the
	teammates' feeds shown or hidden, port/linux/game/spartan_link.c) */
	HALO_KEYBOARD_SPARTAN_LINK,
	NUMBER_OF_HALO_KEYBOARD_ACTIONS
};

/* the actions the keyboard and mouse hold for the player on the controller
(a bit for each halo_keyboard_action): theirs is the first's; none while a
menu or the console is up, or a chat line is typed */
unsigned long halo_keyboard_actions(short controller_index);
/* Current binding label, slot 0/1; zero and empty for unbound or invalid inputs. */
int halo_keyboard_binding_label(int action, int slot, char *shown, unsigned int size);
/* whether voice chat's push to talk is held (controls.push_to_talk), in the
game and in the menus alike */
int halo_push_to_talk_held(void);
/* whether the HUD's button prompts name the keyboard and mouse's bindings
in place of the controller's buttons (input.button_prompts: "auto" follows
the device the first player played with last); the first binding of an
action as a prompt shows it ("E", "Left Click", "Mouse 4"), 0 if it has none.
The name is Latin-1. */
int halo_keyboard_prompts(void);
int halo_keyboard_action_name(int action, char *name, unsigned long size);
/* the server browsers' keys in the menus (port/linux/game/menu_functions.c
says, each frame one is shown, that one is): Tab is X (their sort) and E is
RB alone (their next tab), where elsewhere Tab is Y (the Profiles screen's
new profile) and E is X too (Load Game's, Profiles' and the gametypes'
delete) as well as RB. Whether they are, for the keys and their prompts
(source/interface/ui_widget.c) */
void halo_keyboard_browser_keys(void);
int halo_keyboard_browser_keys_active(void);
/* a Settings screen's keys, the menu engine's (menu_functions.c says, each
frame one is shown, that one is; search: the search's own screen): W, A, S,
D, F, R and L type into its search (settings_search_keys), not the left
stick, Y and the triggers, and on the search's screen Backspace takes a
letter off, not Esc. Whether they are (for the keys) */
void halo_keyboard_settings_keys(int search);
int halo_keyboard_settings_keys_active(void);

/* the first controller's buttons (with the touch controls'), as the text
chat's on-screen keyboard reads them: a bit for each (port/linux/game/
network_chat.c). The game gets none of the controller while that keyboard is
up (network_chat_keyboard_open), nor the touch controls' stick and look */
enum halo_chat_pad_button
{
	HALO_CHAT_PAD_UP,
	HALO_CHAT_PAD_DOWN,
	HALO_CHAT_PAD_LEFT,
	HALO_CHAT_PAD_RIGHT,
	HALO_CHAT_PAD_A,
	HALO_CHAT_PAD_B,
	HALO_CHAT_PAD_X,
	HALO_CHAT_PAD_Y,
	HALO_CHAT_PAD_START,
	/* the left stick, pushed most of the way */
	HALO_CHAT_PAD_STICK_UP,
	HALO_CHAT_PAD_STICK_DOWN,
	HALO_CHAT_PAD_STICK_LEFT,
	HALO_CHAT_PAD_STICK_RIGHT,
	NUMBER_OF_HALO_CHAT_PAD_BUTTONS
};

unsigned long halo_chat_pad_buttons(void);
/* the stock layouts' ping, D-pad Right, pressed on the port's controller
since it was last asked (a custom layout's is its PING:
port/linux/game/network_markers.c); while pings can be sent the game gets
none of that direction (network_markers_pad_open) */
int halo_ping_pad_pressed(short port);
/* Raw physical snapshot: suppress the button which opened controller chat. */
unsigned long halo_chat_pad_buttons_for_port(short port);

/* the scoreboard's player menu by controller and keyboard (source/game/
game_engine.c): each frame a local player's scoreboard shows its player menu
(a network game's), the game says how it holds that player's controller
(halo_scoreboard_pad), and has a bit for each of these pressed since it
last asked. While it is offered (the scoreboard held open with Back or the
scoreboard key) the scoreboard takes the D-pad and X of the controller, and
the arrow keys and Enter of the keyboard, from the game; while it holds the
controls (the scoreboard pinned open to pick a player, or the player menu
open) it takes everything but the screenshot and push to talk, and what is
held as it lets go stays the scoreboard's till let go of (no melee for the B
that closed it, no pause menu for the Escape). Only for the game: never
while a menu is up, nor once the game has not asked for a few frames (its
game over), when the presses are dropped and the next ask is told so
(HALO_SCOREBOARD_PAD_LAPSED). */
enum halo_scoreboard_pad_button
{
	/* the D-pad, the arrow keys */
	HALO_SCOREBOARD_PAD_UP,
	HALO_SCOREBOARD_PAD_DOWN,
	HALO_SCOREBOARD_PAD_LEFT,
	HALO_SCOREBOARD_PAD_RIGHT,
	/* the left stick pushed most of the way (moving the player but while
	the scoreboard holds the controls) */
	HALO_SCOREBOARD_PAD_STICK_UP,
	HALO_SCOREBOARD_PAD_STICK_DOWN,
	HALO_SCOREBOARD_PAD_STICK_LEFT,
	HALO_SCOREBOARD_PAD_STICK_RIGHT,
	/* X, Enter: the player menu opened */
	HALO_SCOREBOARD_PAD_OPEN,
	/* A, Enter: chosen */
	HALO_SCOREBOARD_PAD_ACCEPT,
	/* B, Escape, Backspace: back */
	HALO_SCOREBOARD_PAD_CANCEL,
	/* Back, the scoreboard's key (controls.scoreboard): closed */
	HALO_SCOREBOARD_PAD_CLOSE,
	/* (no button: the presses were dropped since the last ask) */
	HALO_SCOREBOARD_PAD_LAPSED,
	NUMBER_OF_HALO_SCOREBOARD_PAD_BITS
};

/* how the scoreboard holds a controller */
enum
{
	HALO_SCOREBOARD_PAD_FREE,
	HALO_SCOREBOARD_PAD_OFFERED,
	HALO_SCOREBOARD_PAD_HOLDS
};

unsigned long halo_scoreboard_pad(short controller_index, int hold);
/* whether the first player last used a controller (a button pressed, a
stick moved) rather than the keyboard and mouse: the vote's box shows the
D-pad's keys then (port/linux/game/network_votekick.c) */
int halo_gamepad_last_used(void);
/* the taps on the touchscreen outside the menus since the last call while
that keyboard is up, and where the latest was (0 to 1 across and down the
window) */
int halo_chat_taps(float *x, float *y);
/* whether the touch controls hold the screen's bottom left, where the chat
then goes at the top left: on Android and iOS */
int halo_chat_touch_layout(void);
/* the mouse wheel's notches turned while a chat line is typed, since the
last call (up positive): they scroll the lines, not the weapons */
int halo_chat_wheel_steps(void);

#endif
