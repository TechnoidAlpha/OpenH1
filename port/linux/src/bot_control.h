/*
BOT_CONTROL.H

The host's control connection to its bots, and the command line that uses
it (bot_control.c; docs/bot_control.md): with bots.control on (or the game
started with --bot-control), a game this machine runs listens on
127.0.0.1:bots.control_port for lines of port/linux/game/bots_command.c's
commands, each with the token the game writes next to config.toml
(bot_control.txt). `halo bots <command>` sends one and prints its answer;
`halo bots watch` follows the events. A dedicated server takes the same
commands through its own admin connections (RCON, HTTP: "bots <command>"),
and `halo bots --http <host:port> --password <admin password>` reaches them.
*/

#ifndef __HALO_LINUX_BOT_CONTROL_H
#define __HALO_LINUX_BOT_CONTROL_H

/* each frame of a game (main.c): the connection opened or closed as the
setting says, its requests answered between frames */
void bot_control_frame(void);

/* the command line's "bots ..." (server_mode.c, before anything starts):
answered, and the process ended with its exit code (0: done, 1: the command
failed, 2: no game answered) */
void bot_control_command_line(int first_argument);

#endif
