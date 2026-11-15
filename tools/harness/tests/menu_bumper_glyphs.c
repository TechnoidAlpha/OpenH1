/*
MENU_BUMPER_GLYPHS.C (test)

The menus' LB and RB (the tabs' keys, Host Controls', chat's) are drawn as
the pad's shoulder buttons, never the original Xbox's white and black discs
(controller_glyphs.c's controller_glyph_asset_index): outside a player's
view, a classic Xbox style takes the modern bumpers, and the other families
their own (L1 and R1, L and R). In a view (the HUD) display.hud_bumper_style
still decides. test_menu_bumper_glyphs.py takes the game's code.
*/

#include "harness.h"

static struct { short local_player_index; } render;
static char const *bumper_style = "original";

static char const *config_string(char const *name)
{
	return !strcmp(name, "display.hud_bumper_style") ? bumper_style : "";
}

#include "under_test.inc"

/* the styles (controller_buttons.c's): classic, gems, menu_gems, xbox_modern,
ps_classic ..., switch_classic ...; the slots: A, B, X, Y, then LB (white)
4 and RB (black) 5 */
#define CLASSIC 0
#define PS_CLASSIC 4
#define SWITCH_CLASSIC 8
#define MODERN_LB 32

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "menus"))
	{
		render.local_player_index = NONE;
		CHECK(controller_glyph_asset_index(CLASSIC, 4) == MODERN_LB, "the menus' LB: the modern bumper");
		CHECK(controller_glyph_asset_index(CLASSIC, 5) == MODERN_LB + 1, "the menus' RB: the modern bumper");
		CHECK(controller_glyph_asset_index(1, 4) == MODERN_LB, "gems' LB too");
		CHECK(controller_glyph_asset_index(PS_CLASSIC, 4) == MODERN_LB + 10, "a PlayStation pad's L1");
		CHECK(controller_glyph_asset_index(SWITCH_CLASSIC, 5) == MODERN_LB + 21, "a Switch pad's R");
		CHECK(controller_glyph_asset_index(CLASSIC, 0) == NONE, "A stays the classic picture");
		return 0;
	}
	if (!strcmp(name, "view"))
	{
		render.local_player_index = 0;
		CHECK(controller_glyph_asset_index(CLASSIC, 4) == NONE, "the HUD's white button, as the setting says");
		bumper_style = "modern";
		CHECK(controller_glyph_asset_index(CLASSIC, 4) == MODERN_LB, "the HUD's modern bumper, as the setting says");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
