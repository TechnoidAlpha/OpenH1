/*
SHIELD_COLOR.C

Your energy shield's color, as this machine draws it (Video Setup > FOV and
Viewmodels > Shield Color): the flare in one of the multiplayer armor colors
instead of the shield's own, whatever color the armor is; each local
player's own (their profile's: player_settings.c), on their own unit. The shield's own
shader is drawn, only its two colors are replaced, keeping their brightness:
no new assets. Others see your shield as their own settings draw it.

Only the drawing is changed; the shield itself is the game's.
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "units/units.h"
#include "game/players.h"
#include "saved games/player_profile.h"

#include "player_settings.h"

#include <string.h>

/* the multiplayer armor colors, in the profile's order (player_profile.c) */
static char const *const shield_color_names[] =
{
	"white", "black", "red", "blue", "gray", "yellow", "green", "pink", "purple",
	"cyan", "cobalt", "orange", "teal", "sage", "brown", "tan", "maroon", "salmon",
};

static unsigned long shield_color_changes = (unsigned long)-1;
/* each local player's color (NONE: the shield's own) */
static short shield_color_indices[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

static void shield_color_update(
	void)
{
	short local_player_index;

	if (shield_color_changes == config_changes())
		return;
	shield_color_changes = config_changes();
	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		char const *value = config_player_string(local_player_index, "display.shield_color");
		short index;

		shield_color_indices[local_player_index] = NONE;
		for (index = 0; value && index < (short)NUMBEROF(shield_color_names); index++)
		{
			if (!strcmp(value, shield_color_names[index]))
				shield_color_indices[local_player_index] = index;
		}
	}
}

/* the colors a unit's shield is drawn with, given the unit's own: when the
unit is a local player's and a color is chosen, a copy of them that
shield_color_override knows again; otherwise the unit's own */
static real_rgb_color shield_color_marker[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS][4];

real_rgb_color const *shield_color_colors(
	long unit_index,
	real_rgb_color const *colors)
{
	short local_player_index;

	shield_color_update();
	if (!colors)
		return colors;
	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);

		if (shield_color_indices[local_player_index] != NONE && player_index != NONE &&
			player_get(player_index)->unit_index == unit_index)
		{
			csmemcpy(shield_color_marker[local_player_index], colors, sizeof(shield_color_marker[local_player_index]));
			return shield_color_marker[local_player_index];
		}
	}
	return colors;
}

/* the chosen color, when a plasma shader is drawn with colors from
shield_color_colors (FALSE: drawn as it is) */
boolean shield_color_override(
	real_rgb_color const *colors,
	real_rgb_color *chosen)
{
	short local_player_index;

	if (!colors)
		return FALSE;
	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		if (colors == shield_color_marker[local_player_index] && shield_color_indices[local_player_index] != NONE)
		{
			*chosen = player_profile_get_rgb_color(shield_color_indices[local_player_index]);
			return TRUE;
		}
	}
	return FALSE;
}

/* a shield color in place of one of the shader's own: the chosen hue at the
original's brightness (its brightest channel) */
void shield_color_apply(
	real_rgb_color const *chosen,
	real_rgb_color const *original,
	real_rgb_color *result)
{
	real brightness = MAX(original->red, MAX(original->green, original->blue));
	real peak = MAX(chosen->red, MAX(chosen->green, chosen->blue));

	if (peak <= 0.f)
	{
		/* (black: the flare is added to what is behind it, so black would
		not show at all; a dark smoke-grey instead) */
		result->red = result->green = result->blue = brightness * 0.12f;
		return;
	}
	result->red = chosen->red / peak * brightness;
	result->green = chosen->green / peak * brightness;
	result->blue = chosen->blue / peak * brightness;
}
