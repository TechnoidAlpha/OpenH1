/*
SCRIPTED_PAD.C (test)

A controller the test script plays (debug.test_input's script:, p<n> for a
split screen player's) is read as a connected one is
(port/linux/src/xinput_sdl.c): its presses are the controller's own
(chat_physical_pad), so the stock layouts' D-pad Down shows and hides the
Spartan link (spartan_link_pad_read) and D-pad Right pings (ping_pad_read),
and the game gets none of that direction while they are open. Before, a
scripted D-pad Down reached neither, and a scripted split screen port was
read past them. test_scripted_pad.py takes the code.
*/

#include "harness.h"

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef short SHORT;
typedef struct
{
	WORD wButtons;
	BYTE bAnalogButtons[8];
	SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_GAMEPAD;

#include "config.inc"

#define PORT_COUNT 4

struct platform_input_state
{
	BOOL menus;
};

/* the world the reading sees: the scenario's clock, the link and pings open
or not, a custom layout or not */
static double scenario_seconds;
static boolean link_open = TRUE, pings_open = TRUE, custom_layout;
static int scoreboard_reads, chat_reads;

static double sim_trace_scenario_seconds(void) { return scenario_seconds; }
static void sim_trace_aim(float yaw, float pitch) { (void)yaw; (void)pitch; }
static int bots_hunt(float *forward, float *strafe) { *forward = *strafe = 0.0f; return 0; }
static void platform_log(char const *format, ...) { (void)format; }
static void platform_input_read(struct platform_input_state *input, BOOL consume) { input->menus = FALSE; (void)consume; }
static int scoreboard_pad_port_hold(int port, BOOL menus) { (void)port; (void)menus; return 0; }
static void scoreboard_pad_read(int port, XINPUT_GAMEPAD *pad, unsigned long keys, int hold)
{
	(void)port; (void)pad; (void)keys; (void)hold;
	scoreboard_reads++;
}
static void chat_pad_hold_over(short port, XINPUT_GAMEPAD *pad) { (void)port; (void)pad; chat_reads++; }
static int controller_remap_read(short port, unsigned char *layout) { (void)port; (void)layout; return custom_layout; }
static unsigned char spartan_link_pad_open(short port) { (void)port; return (unsigned char)link_open; }
static boolean network_markers_pad_open(short port) { (void)port; return pings_open; }

#include "under_test.inc"

/* one reading of a split screen player's port the script plays (no
controller connected), as XInputGetState makes it */
static XINPUT_GAMEPAD scripted_reading(int port, double seconds)
{
	XINPUT_GAMEPAD pad;

	memset(&pad, 0, sizeof(pad));
	scenario_seconds = seconds;
	split_screen_pad_read(port, &pad, NULL, 0);
	return pad;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a split screen player's scripted D-pad Down: the link's, once, and
	none of it the game's */
	CASE("link")
	{
		XINPUT_GAMEPAD pad;

		test_script_parse("1-1.5 p2 down; 3 p2 down");
		pad = scripted_reading(1, 0.5);
		CHECK(!halo_spartan_link_pad_pressed(1), "a press before the script's");
		pad = scripted_reading(1, 1.0);
		CHECK(!(pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN), "the game got the link's D-pad Down");
		CHECK(halo_spartan_link_pad_pressed(1), "the scripted D-pad Down not the link's");
		CHECK(!halo_spartan_link_pad_pressed(1), "one press taken twice");
		pad = scripted_reading(1, 1.2);
		CHECK(!halo_spartan_link_pad_pressed(1), "held, pressed again");
		CHECK(!halo_spartan_link_pad_pressed(0) && !halo_spartan_link_pad_pressed(2), "another port's press");
		pad = scripted_reading(1, 2.0);
		pad = scripted_reading(1, 3.0);
		CHECK(halo_spartan_link_pad_pressed(1), "the second press missed");
		CHECK(scoreboard_reads == 5 && chat_reads == 5, "the scoreboard and the chat read %d and %d times",
			scoreboard_reads, chat_reads);
		return 0;
	}
	/* D-pad Right: the ping's */
	CASE("ping")
	{
		XINPUT_GAMEPAD pad;

		test_script_parse("1 p3 right");
		pad = scripted_reading(2, 1.0);
		CHECK(!(pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT), "the game got the ping's D-pad Right");
		CHECK(halo_ping_pad_pressed(2), "the scripted D-pad Right not the ping's");
		CHECK(!halo_spartan_link_pad_pressed(2), "a ping the link's");
		return 0;
	}
	/* the link closed to this player (a free-for-all alone, a menu up), or
	a custom layout: the D-pad the game's, as a connected controller's */
	CASE("closed")
	{
		XINPUT_GAMEPAD pad;

		test_script_parse("1 p2 down; 2 p2 down");
		link_open = FALSE;
		pad = scripted_reading(1, 1.0);
		CHECK(pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN, "the D-pad Down taken while the link is closed");
		CHECK(!halo_spartan_link_pad_pressed(1), "a press while the link is closed");
		link_open = TRUE;
		custom_layout = TRUE;
		pad = scripted_reading(1, 2.0);
		CHECK(pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN, "a custom layout's D-pad Down taken");
		CHECK(!halo_spartan_link_pad_pressed(1), "a custom layout's D-pad Down the link's");
		return 0;
	}
	/* the script's other buttons still played, and the script's presses
	cleared between readings */
	CASE("buttons")
	{
		XINPUT_GAMEPAD pad;

		test_script_parse("1 p2 jump; 1 p2 move=0,1");
		pad = scripted_reading(1, 1.0);
		CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_A] == 255 && pad.sThumbLY > 30000, "the jump or the move lost");
		CHECK(test_script_pressed[1].bAnalogButtons[XINPUT_GAMEPAD_A] == 255, "the jump not the controller's own");
		pad = scripted_reading(1, 2.0);
		CHECK(!pad.bAnalogButtons[XINPUT_GAMEPAD_A] && !test_script_pressed[1].bAnalogButtons[XINPUT_GAMEPAD_A],
			"the jump held past its time");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
