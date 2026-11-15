/*
MENU_BUMPER_GLYPHS.C (test)

The menus' LB and RB (the tabs' keys, Host Controls', chat's) are drawn as
the pad's shoulder buttons, never the original Xbox's white and black discs
(controller_glyphs.c's controller_glyph_asset_index, while
controller_glyph_menu_bumper_draw draws): a classic Xbox style takes the
modern bumpers, and the other families their own (L1 and R1, L and R). The
HUD's are as display.hud_bumper_style says. test_menu_bumper_glyphs.py takes the game's code.
*/

#include "harness.h"

boolean controller_glyph_menu_bumpers; /* (not static: a control reads it not at all) */
static char const *bumper_style = "original";

static char const *config_string(char const *name)
{
	return !strcmp(name, "display.hud_bumper_style") ? bumper_style : "";
}

#include "under_test.inc"

/* the styles (controller_buttons.c's): classic, gems, menu_gems, xbox_modern,
ps_classic ..., switch_classic ...; the slots: A, B, X, Y, then RB (black)
4 and LB (white) 5; the art: Xbox's RB and LB, then each family's ten */
#define CLASSIC 0
#define PS_CLASSIC 4
#define SWITCH_CLASSIC 8
#define MODERN_RB 32

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "menus"))
	{
		controller_glyph_menu_bumpers = TRUE;
		CHECK(controller_glyph_asset_index(CLASSIC, 4) == MODERN_RB, "the menus' RB: the modern bumper");
		CHECK(controller_glyph_asset_index(CLASSIC, 5) == MODERN_RB + 1, "the menus' LB: the modern bumper");
		CHECK(controller_glyph_asset_index(1, 5) == MODERN_RB + 1, "gems' LB too");
		CHECK(controller_glyph_asset_index(PS_CLASSIC, 4) == MODERN_RB + 10, "a PlayStation pad's R1");
		CHECK(controller_glyph_asset_index(SWITCH_CLASSIC, 5) == MODERN_RB + 21, "a Switch pad's L");
		CHECK(controller_glyph_asset_index(CLASSIC, 0) == NONE, "A stays the classic picture");
		return 0;
	}
	if (!strcmp(name, "view"))
	{
		controller_glyph_menu_bumpers = FALSE;
		CHECK(controller_glyph_asset_index(CLASSIC, 4) == NONE, "the HUD's black button, as the setting says");
		bumper_style = "modern";
		CHECK(controller_glyph_asset_index(CLASSIC, 4) == MODERN_RB, "the HUD's modern bumper, as the setting says");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
