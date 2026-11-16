/*
NETWORK_CHAT.H

The text chat of network games (network_chat.c): what the main loop, the
HUD (hud.c, hud_unit.c), the keyboard (xinput_sdl.c) and the netcode ask of
it.
*/

#ifndef __NETWORK_CHAT_H
#define __NETWORK_CHAT_H
#pragma once

/* ---------- constants */

/* who a line is to */
enum
{
	_chat_channel_everyone,
	_chat_channel_team,
	/* those riding in the sender's vehicle */
	_chat_channel_vehicle,
	NUMBER_OF_CHAT_CHANNELS
};

/* ---------- prototypes/NETWORK_CHAT.C */

/* every frame (main.c): the keys that start typing a line, and those typed */
void network_chat_update(void);
/* (a dedicated server) whether a player's name has words its word filter
keeps out at the door (server.chat_filter "block") */
boolean network_chat_name_blocked(wchar_t const *name, short length);
/* the first local player says a line to a channel, as if typed and sent;
the automated tests' (network_test.c) */
void network_chat_say(short channel, char const *text);
/* a notice of the host's (network_distributed.c's _distributed_message_notice:
the MOTD, the votes', a kick's) shown to this machine's players: among the
chat's lines, in its notice colour (in more than one when it is longer than
one holds), or with the chat not shown, as a red line of the console's,
whatever game.console_log says */
void network_chat_show_host_notice(char const *text);
/* ... sent past this machine's own checks of the host's settings, as a
changed build could send it, for the host to refuse (network_test.c) */
void network_chat_say_unchecked(short channel, char const *text);
/* whether a line is being typed: the keyboard types into it, and drives no
player (xinput_sdl.c), as with the console */
boolean network_chat_typing(void);
/* the line for a report's note (F8: port/linux/src/report.c), in any game:
opened (FALSE if there is no player to type it), and closed unfinished.
Enter or Escape gives it to report_note_done. */
boolean network_chat_report_note_begin(void);
void network_chat_report_note_cancel(void);
/* Physical controller owning the on-screen keyboard, or NONE. */
short network_chat_controller(void);
/* (hud_unit.c) where the local player's motion sensor was drawn: its left
and the top of its circle, in the window's units */
void network_chat_note_motion_sensor(short local_player_index, short left, short top);
/* (hud.c, in the HUD of each local player's view) the lines that view's
player hears, above the motion sensor (with the touch controls, under the
weapon's panel at the top left), and in the first local player's the one
being typed */
void network_chat_draw(short local_player_index);
/* (the host) the minutes a player (absolute index) is muted for everyone;
0 for none */
short network_chat_host_mute_minutes(short player_index);
/* (the host: the scoreboard) a player (absolute index) muted for everyone,
their lines and their voice, for /mute's minutes when it gives none (every
machine told), or unmuted */
void network_chat_host_mute(short player_index, boolean mute);
/* (the host: a dedicated server's mute and unmute commands,
server_commands.c) a player's name (ASCII, 11 characters) muted for everyone
for minutes (0: /mute's), every machine told, or unmuted: FALSE if it could
not be */
boolean network_chat_host_mute_named(char const *name, short minutes, boolean mute);
/* ---------- the lobby's chat (the hand-written lobby's chat area,
menu_screen.c and lobby_menu.inc) */

enum
{
	/* the lobby's lines kept */
	NETWORK_CHAT_LOBBY_LINES = 16,
	/* a line's colour: its sender's team's, or the host's notice's */
	NETWORK_CHAT_LOBBY_RED = 0,
	NETWORK_CHAT_LOBBY_BLUE = 1,
	NETWORK_CHAT_LOBBY_NOTICE = 2,
};

/* a lobby's line: its sender's name (none for a notice), its text, its
colour, its sender's machine (NONE for a notice), when it came
(system_milliseconds) */
struct network_chat_lobby_line
{
	wchar_t name[12];
	char text[64];
	short color;
	long machine;
	unsigned long time;
};

/* (network_distributed.c) a lobby's message: a line of the lobby's, or the
host's settings while in the lobby, which a game's chat never takes; and
it handled (machine_index the sender's on the host, NONE on a client) */
boolean network_chat_handles_lobby_message(word const *message, word size);
void network_chat_handle_lobby_message(long machine_index, word const *message, word size, boolean from_stream);
/* each frame the lobby's chat area is shown: its keys (T, Y) start a line */
void network_chat_lobby_frame(void);
/* whether the lobby has its chat now (the host's on, display.text_chat) */
boolean network_chat_lobby_on(void);
/* the lobby's lines, the oldest first, their count */
short network_chat_lobby_lines(struct network_chat_lobby_line *lines, short maximum);
/* the lobby's line being typed (NULL: none), and whether its on-screen
keyboard is up */
char const *network_chat_lobby_typed(boolean *keyboard);
/* a lobby's line begun by a controller (NONE: the keyboard's), with the
on-screen keyboard or not: FALSE if it cannot be */
boolean network_chat_lobby_begin(short controller, boolean keyboard);
/* the lobby's on-screen keyboard drawn in its area (the menus' screen's
units, screen_width by screen_height), its keys' place noted for taps */
void network_chat_lobby_draw_keyboard(long font_index, short x0, short y0, short x1, short y1, short screen_width,
	short screen_height);

/* (the host: network_voice.c) whether a player's name (the network game's,
in the lobby too; at most 12 characters, ended or not) is muted for everyone:
their machine's voice goes no further */
boolean network_chat_host_name_muted(wchar_t const *name);

#endif // __NETWORK_CHAT_H
