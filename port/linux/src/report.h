/*
REPORT.H

Save Report (report.c): a zip of what helps find a problem, saved on this
machine for the player to send.
*/

#ifndef REPORT_H
#define REPORT_H

#include <stddef.h>
#include <stdint.h>

/* a frame presented (d3d8_device.c's present): its time kept, the last
REPORT_FRAMES of them */
void report_frame_presented(void);
/* the frame times of the last seconds logged, as a report's summary gives
them (debug.exit_after's runs with HALO_FRAME_SUMMARY=<seconds>, for
measuring) */
void report_frames_log(double seconds);
/* a line of the log (xbox_kernel.c's platform_log), kept: the last
REPORT_LOG_SIZE bytes */
void report_log_line(const char *line);
/* a report asked for (controls.report, keyboard 1: its note asked for in a
game; Settings' REPORT A PROBLEM, 0: none), saved and sent at the next frame
presented (report_write); and taken (1 if asked) */
void report_request(int keyboard);
int report_take_request(void);
/* the report saved: the target presented is its screenshot (0: none); send,
a GitHub issue opened in the browser, filled in with report.md, and the
reports folder, for its zip to be dragged in. The zip is written on a
thread of its own: the player is told (where it is, or why it failed) once
it is, at a frame presented (report_frame_presented), so message is left
empty; 1. In a game the summary and the frame are taken now, and the zip
written once the player's note is typed (report_note_done). */
int report_write(unsigned int target, int send, char *message, size_t message_size);
/* the note typed for a report asked for in a game (network_chat.c's line),
NULL if it was let go: the report finished, as report_write does */
void report_note_done(const char *note);

/* this session begun (sdl_platform.c, as the game starts; not a dedicated
server's): the last run's crash found, if it did not exit, and this one's
file written, and the host told where to write a crash */
void report_session_start(void);
/* the game halted on a fatal error (main.c's halt_and_catch_fire, its
reason error_get's): reported at the next start as a crash is, though the
player then closes the game */
void report_session_halted(const char *reason);
/* the updater's (updater.c), the new build about to start (1): this session
ended as an exit ends it, before the new one looks for it (which starts
while this one is still quitting, or in this process); or (0) it could not
be started, and this session goes on: its file written again */
void report_session_handover(int leaving);
/* the crash question's answer (menu_functions.c's "port crash report", 1,
and "port crash dismiss", 0): its report saved at the next frame, its issue
opened, or the crash let go; neither asked of again */
void report_crash_answer(int report);

/* ---------- report.c's parts (tools/harness/tests/test_report.py) */

/* the frame times' summary, in report.md's words: micros, each frame's time
(microseconds), at_ms, when it ended (milliseconds from the start), oldest
first; now_ms, the report's time */
void report_frame_summary(const uint32_t *micros, const uint32_t *at_ms, unsigned long count, uint32_t now_ms,
	char *text, size_t size);
/* a frame's work (microseconds each): the GPU's (0xFFFFFFFF: not
measured), the renderer's thread's, the game's waits for the renderer and
the display, and of those the display's pacing and display.max_fps's */
struct report_frame_work
{
	uint32_t gpu, render, waited, paced;
};

/* what held the frames back, in report.md's words: the GPU's time a frame
(its average and 99th percentile), the GPU's, the game's and the
renderer's thread's shares of the time, the pacing's, and the verdict
(GPU-bound, CPU-bound, or limited by vsync or the frame cap): micros, each
frame's time, work, the work in it */
void report_gpu_summary(const uint32_t *micros, const struct report_frame_work *work, unsigned long count,
	char *text, size_t size);
/* a new GitHub issue's address on OpenH1's repository, its title and body
filled in (percent-encoded; the body cut at a line to keep the address
within most bytes); 1 if it fits */
int report_issue_url(const char *title, const char *body, char *url, size_t most);
/* a text with what could say who the player is taken out, its issue being
public: home (the account's folder) as ~, invite and party links, IPv4
addresses (but 127.x and 0.0.0.0) and IPv6 ones (but ::1), chat lines and
the host's notices, and what follows a few markers (a Discord name, a link's
code, network.public_address); malloc()'s, its size into scrubbed_size */
char *report_scrub(const char *text, size_t size, const char *home, size_t *scrubbed_size);
/* report.md's heading of the last run's crash: session, its session's file
("build: ...", "started: ...", "reference address: ..." lines); record, the
host's (the crash's lines, then the log after host_common.c's heading), or
NULL if it wrote none */
void report_crash_summary(const char *session, const char *record, char *text, size_t size);
/* where a log's end taken from its middle (cut: 1) starts: after its first
line's end, the partial line left out */
size_t report_whole_lines(const char *text, size_t size, int cut);
/* the zip's CRC-32 (crc 0 to begin, then each part's) */
uint32_t report_crc32(uint32_t crc, const unsigned char *data, size_t size);

/* whether a file of a maps folder (its name) is a map counted in report.md:
a .map, not bitmaps.map, sounds.map nor loc.map */
int report_map_counted(const char *name);

/* where the problem happened, as report_game_where takes it the moment a
report is asked for: numbers and this machine's tag names only, never a
player's name or an address */
enum
{
	REPORT_WHERE_BUILDS = 8,
	/* (a build that is OpenCE's, not OpenH1's) */
	REPORT_WHERE_OPENCE = -1,
};
struct report_where
{
	/* 0: the menus (nothing more is said) */
	int playing;
	/* the map and the game, report_game_describe's; the level's name, as
	map_name takes it (the scenario's tag, or custom_maps\<name>); a
	campaign (no game engine) and its difficulty (0 easy to 3 impossible);
	the structure BSP loaded */
	char game[256];
	char map[128];
	int campaign, difficulty, bsp;
	/* the game's time, in ticks of ticks_per_second */
	long ticks, ticks_per_second;
	/* the first local player: whether there is one, alive; its place (its
	vehicle's, in one), its aim in degrees (yaw from the x axis, pitch up);
	its vehicle's tag (empty: on foot) */
	int player, alive;
	float position[3], yaw, pitch;
	char vehicle[128];
	/* its camera: position, yaw, pitch, roll and field of view in degrees
	(as debug_camera_set takes them, camera_pose.c) */
	int camera;
	float camera_position[3], camera_yaw, camera_pitch, camera_roll, camera_field_of_view;
	/* a network game: 0 none, 1 hosting, 2 joined; its OpenH1 game's rate
	(0: Legacy); the round trip in milliseconds to the host (a client's) or
	the slowest client's (the host's), -1 not known; the host silent for
	two seconds (a client); the players in the game; (the host) the other
	machines' builds as they told them, a count of each (a release's
	number, 0 a local build, REPORT_WHERE_OPENCE), and of those that told
	none */
	int network;
	long rate, ping;
	int connection_problem, players;
	long builds[REPORT_WHERE_BUILDS], build_counts[REPORT_WHERE_BUILDS];
	int build_kinds, builds_unknown;
};
/* report.md's "## Where" section of it (empty for the menus) */
void report_where_text(const struct report_where *where, char *text, size_t size);

/* port/linux/game/report_game.c: report.md's lines of the mods on, in the
load order, by their mod.toml's names and versions, and of the map loaded:
stock or Custom Edition, its file's name and size and its header's CRC; and
whether game.custom_edition is on */
void report_game_mods_and_maps(char *text, size_t size);
/* ... the map and the game, in a few words; whether a game is being played
(not the menus, nor a loading screen: the note's line closes); and whether
the main menu is up (the ui map: a report's issue waits for it) */
void report_game_describe(char *text, size_t size);
int report_game_playing(void);
int report_game_menus(void);
/* ... and where the game is, this moment */
void report_game_where(struct report_where *where);
/* the line for a report's note opened (network_chat.c: 1 if it is; it calls
report_note_done), or closed unfinished; and the player told something */
int report_game_note_begin(void);
void report_game_note_cancel(void);
void report_game_say(const char *text);

#endif
