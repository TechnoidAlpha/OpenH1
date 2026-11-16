/*
HUD_BUTTON_STYLE.C

display.hud_button_style (port/linux/game/hud_hires_tags.c's
hud_button_style): each local player's view draws its controller buttons
in its own player's style (their profile's), read again as it changes.
*/

#include "harness.h"
#include <string.h>

static char const *fake_styles[4] = { "classic", "classic", "classic", "classic" };
static char const *fake_global = "classic";
static unsigned long fake_changes = 1;

static unsigned long config_changes(void) { return fake_changes; }
static char const *config_player_string(int local_player, char const *name)
{
	(void)name;
	return local_player >= 0 && local_player < 4 ? fake_styles[local_player] : fake_global;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("players")
	{
		fake_styles[0] = "gems";
		fake_styles[1] = "menu_gems";
		fake_changes++;
		CHECK(hud_button_style(0) == 1 && hud_button_style(1) == 2 && hud_button_style(2) == 0,
			"the styles: %d %d %d", hud_button_style(0), hud_button_style(1), hud_button_style(2));
		CHECK(hud_button_style(NONE) == 0, "no player's: %d", hud_button_style(NONE));
		fake_styles[1] = "classic";
		fake_changes++;
		CHECK(hud_button_style(1) == 0 && hud_button_style(0) == 1, "changed: %d %d", hud_button_style(1),
			hud_button_style(0));
		/* (an older file's "dark_gems" is menu_gems') */
		fake_styles[2] = "dark_gems";
		fake_changes++;
		CHECK(hud_button_style(2) == 2, "dark gems: %d", hud_button_style(2));
	}
	return 0;
}
