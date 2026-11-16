/*
FIRST_START.C

The Linux builds' and the macOS app's first start without game data
(platform_offer_game_data, called by xbox_files.c): a screen in a window of
the game's own that finds the player's disc image or maps folder
(game_data_scan.c) and copies its maps into the game's folder, operated with
a controller, the keyboard, the mouse or touch.

It asks nothing of the desktop: on a Steam Deck in Game Mode (gamescope) and
in the Steam Frame's headset no system dialog can be shown (one crashes the
game under gamescope, updater.c), and a desktop without the desktop portal or
zenity has no file dialog. So the game looks for the image itself, in
Downloads, on the Desktop, in the home folder, on SD cards and USB drives and
next to the game, and when it finds none it says where to put it and looks
again when asked. The system's file dialog is only an extra ("Browse..."), on
a desktop, and a failure to show one is said in the window too.

On macOS the screen is the game's window from the start, so the player never
sees a dialog of the system's alone, hidden behind other windows or in
another Space, while the game shows nothing. Browse... opens the Mac's open
panel as a sheet of this window (port/macos/host_app.m), which takes the disc
image or the maps folder; the window stays fullscreen meanwhile. A maps
folder is copied too, not used where it is: the game's data is the game's
own, whatever becomes of the folder or wherever the app is.

Custom Edition's installer (halocesetup_en_1.00.exe), found as the disc
images are or picked with Browse..., is offered too, as an extra: its
multiplayer maps (Halo PC's, with bitmaps.map, sounds.map and loc.map) are
written into the custom_maps folder (map_import.c), and the screen comes
back, since the game still needs the Xbox maps to start. A Halo PC CD
image picked is explained, not copied: OpenH1 does not play Halo PC's own
maps, and the installer has the same ones.

(Windows' first start is sdl_platform.c's.)
*/

#include "platform.h"

#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID) && !defined(HALO_IOS)

#include "port_config.h"
#include "xiso.h"
#include "map_unpack.h"
#include "ce_installer.h"
#include "game_data_scan.h"
#include "map_import.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the debug font's glyphs (SDL_RenderDebugText) are 8 pixels square */
#define GLYPH 8
#define ITEMS_MAXIMUM (GAME_DATA_CANDIDATES + 4)

enum
{
	_screen_list,
	_screen_copy,
	_screen_error,
};

enum
{
	_action_candidate,
	_action_look_again,
	_action_browse,
	_action_back,
	_action_quit,
};

struct chooser_item
{
	int action;
	int candidate;
	/* where it was last drawn, in pixels (for the mouse) */
	SDL_FRect rectangle;
};

struct chooser_copy
{
	pthread_mutex_t lock;
	struct game_data_candidate source;
	/* Custom Edition's installer, whose maps go to folder (custom_maps) */
	BOOL installer;
	char folder[GAME_DATA_PATH_SIZE];
	unsigned long long written;
	char destination[GAME_DATA_PATH_SIZE];
	char file[256];
	unsigned long long done;
	unsigned long long total;
	BOOL finished;
	BOOL succeeded;
	char error[512];
};

struct chooser
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	/* the window's size in pixels, and pixels per window coordinate */
	int width, height;
	float pixels_x, pixels_y;
	/* the text's scale: 2 at the Steam Deck's 1280x800 */
	int unit;
	int screen;
	struct game_data_places places;
	char places_text[GAME_DATA_PATH_SIZE + 128];
	struct game_data_candidate candidates[GAME_DATA_CANDIDATES];
	/* whether each candidate image is Custom Edition's installer */
	BOOL installers[GAME_DATA_CANDIDATES];
	int candidate_count;
	/* the times the player has looked again and found nothing new */
	int looked_again;
	struct chooser_item items[ITEMS_MAXIMUM];
	int item_count;
	int selected;
	float scroll;
	/* whether the system's file dialog may be offered */
	BOOL browse;
	/* paths.data's folder, which has no maps ("" when it has not been set) */
	char missing[GAME_DATA_PATH_SIZE];
	char error_title[128];
	char error[1200];
	/* the error screen says something done, not wrong */
	BOOL notice;
	/* the left stick's last direction (-1, 0 or 1), moved once a push */
	int stick;
	struct chooser_copy copy;
};

static struct chooser chooser;

/* ---------- colours (OpenH1's: the site's dark background, #9dff3c) */

static const SDL_Color colour_background = { 13, 16, 20, 255 };
static const SDL_Color colour_panel = { 24, 29, 35, 255 };
static const SDL_Color colour_panel_selected = { 31, 45, 19, 255 };
static const SDL_Color colour_accent = { 157, 255, 60, 255 };
static const SDL_Color colour_text = { 232, 236, 240, 255 };
static const SDL_Color colour_dim = { 142, 152, 163, 255 };
static const SDL_Color colour_bar = { 44, 52, 60, 255 };
static const SDL_Color colour_error = { 255, 122, 96, 255 };

/* ---------- where the game runs */

/* whether the game runs under gamescope (the Steam Deck's Game Mode, the
Steam Frame), where no system dialog can be shown (as updater.c's
updater_gamescope) */
static BOOL first_start_gamescope(void)
{
	const char *wayland_display = getenv("GAMESCOPE_WAYLAND_DISPLAY");
	const char *desktop = getenv("XDG_CURRENT_DESKTOP");
	const char *steam_deck = getenv("SteamDeck");
	const char *gamepad_ui = getenv("SteamGamepadUI");

	return (wayland_display && wayland_display[0]) || (desktop && strstr(desktop, "gamescope")) ||
		(steam_deck && !strcmp(steam_deck, "1") && gamepad_ui && !strcmp(gamepad_ui, "1"));
}

/* the window as the game's would open: fullscreen (borderless) unless
display.mode or display.fullscreen says a window; under gamescope a plain
window, which gamescope shows fullscreen */
static SDL_Window *first_start_window(BOOL gamescope)
{
	const char *mode = config_string("display.mode");
	BOOL fullscreen = !strcmp(mode, "fullscreen") || !strcmp(mode, "borderless") ||
		(!mode[0] && config_boolean("display.fullscreen"));
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

	if (gamescope)
	{
		/* (its display's size: the Steam Deck's 1280x800, the Steam
		Frame's 1280x720) */
		SDL_DisplayID display = SDL_GetPrimaryDisplay();
		const SDL_DisplayMode *desktop = display ? SDL_GetDesktopDisplayMode(display) : NULL;
		int width = desktop && desktop->w >= 640 ? desktop->w : 1280;
		int height = desktop && desktop->h >= 400 ? desktop->h : 800;

		return SDL_CreateWindow("GOAT H1", width, height, SDL_WINDOW_HIGH_PIXEL_DENSITY);
	}
	if (fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN;
	return SDL_CreateWindow("GOAT H1", 1024, 640, flags);
}

/* ---------- text */

static void set_colour(SDL_Color colour)
{
	SDL_SetRenderDrawColor(chooser.renderer, colour.r, colour.g, colour.b, colour.a);
}

static void fill(float x, float y, float width, float height, SDL_Color colour)
{
	SDL_FRect rectangle;

	rectangle.x = x;
	rectangle.y = y;
	rectangle.w = width;
	rectangle.h = height;
	set_colour(colour);
	SDL_RenderFillRect(chooser.renderer, &rectangle);
}

static float text_width(const char *text, int scale)
{
	return (float)(strlen(text) * GLYPH * scale);
}

static void text(float x, float y, int scale, SDL_Color colour, const char *string)
{
	set_colour(colour);
	SDL_SetRenderScale(chooser.renderer, (float)scale, (float)scale);
	SDL_RenderDebugText(chooser.renderer, x / scale, y / scale, string);
	SDL_SetRenderScale(chooser.renderer, 1.0f, 1.0f);
}

/* text cut to fit in characters, its middle replaced by "..." (a path's
start and its file name are what tell it apart) */
static void fit(const char *string, int characters, char *result, size_t size)
{
	size_t length = strlen(string);

	if (characters < 8)
		characters = 8;
	if ((int)length <= characters)
	{
		snprintf(result, size, "%s", string);
		return;
	}
	{
		int head = (characters - 3) / 3;
		int tail = characters - 3 - head;

		snprintf(result, size, "%.*s...%s", head, string, string + length - tail);
	}
}

/* text wrapped at spaces within width; returns the y below it */
static float paragraph(float x, float y, float width, int scale, SDL_Color colour, const char *string)
{
	int columns = (int)(width / (GLYPH * scale));
	float line_height = GLYPH * scale * 1.6f;
	const char *start = string;

	if (columns < 8)
		columns = 8;
	while (*start)
	{
		const char *end = start;
		const char *last_space = NULL;
		char line[512];
		int length;

		while (*end && *end != '\n' && end - start < columns)
		{
			if (*end == ' ')
				last_space = end;
			end++;
		}
		if (*end && *end != '\n' && *end != ' ' && last_space)
			end = last_space;
		length = (int)(end - start) < (int)sizeof(line) - 1 ? (int)(end - start) : (int)sizeof(line) - 1;
		memcpy(line, start, (size_t)length);
		line[length] = 0;
		text(x, y, scale, colour, line);
		y += line_height;
		start = end;
		if (*start == ' ' || *start == '\n')
			start++;
	}
	return y;
}

static void size_text(unsigned long long bytes, char *result, size_t size)
{
	if (bytes >= 1000000000ULL)
		snprintf(result, size, "%.1f GB", (double)bytes / 1e9);
	else
		snprintf(result, size, "%llu MB", (bytes + 500000) / 1000000);
}

/* ---------- the items */

static void items_clear(void)
{
	chooser.item_count = 0;
}

static void items_add(int action, int candidate)
{
	if (chooser.item_count < ITEMS_MAXIMUM)
	{
		struct chooser_item *item = &chooser.items[chooser.item_count++];

		memset(item, 0, sizeof(*item));
		item->action = action;
		item->candidate = candidate;
	}
}

/* the list screen's items: what was found, then looking again, the
system's dialog where there is one, and quitting */
static void items_list(void)
{
	int index;

	items_clear();
	for (index = 0; index < chooser.candidate_count; index++)
		items_add(_action_candidate, index);
	items_add(_action_look_again, -1);
	if (chooser.browse)
		items_add(_action_browse, -1);
	items_add(_action_quit, -1);
	chooser.selected = 0;
	chooser.scroll = 0.0f;
}

static void items_error(void)
{
	items_clear();
	items_add(_action_back, -1);
	items_add(_action_quit, -1);
	chooser.selected = 0;
	chooser.scroll = 0.0f;
}

static const char *item_label(const struct chooser_item *item)
{
	switch (item->action)
	{
	case _action_look_again:
		return "Look again";
	case _action_browse:
#ifdef HALO_MACOS
		return "Browse for the disc image or folder...";
#else
		return "Browse for the disc image...";
#endif
	case _action_back:
		return chooser.notice ? "Continue" : "Back";
	case _action_quit:
		return "Quit";
	}
	return "";
}

/* ---------- drawing */

static void measure(void)
{
	int window_width = 0, window_height = 0;
	int unit;

	SDL_GetWindowSizeInPixels(chooser.window, &chooser.width, &chooser.height);
	SDL_GetWindowSize(chooser.window, &window_width, &window_height);
	if (chooser.width <= 0 || chooser.height <= 0)
	{
		chooser.width = 1280;
		chooser.height = 800;
	}
	chooser.pixels_x = window_width > 0 ? (float)chooser.width / (float)window_width : 1.0f;
	chooser.pixels_y = window_height > 0 ? (float)chooser.height / (float)window_height : 1.0f;
	/* (the body's text 16 pixels high at 640 to 899 pixels, larger above;
	and 60 of its characters across) */
	unit = (chooser.height + 180) / 360;
	while (unit > 1 && chooser.width < unit * GLYPH * 60)
		unit--;
	chooser.unit = unit < 1 ? 1 : unit;
}

/* the header, the headline and the explanation; returns the y below them */
static float draw_heading(float left, float width, const char *headline, SDL_Color headline_colour)
{
	int unit = chooser.unit;
	float y = (float)chooser.height * 0.07f;

	text(left, y, unit * 2, colour_accent, "GOAT H1");
	y += GLYPH * unit * 2 + unit * 6;
	fill(left, y, width, (float)unit, colour_bar);
	y += unit * 12;
	text(left, y, unit + 1, headline_colour, headline);
	return y + GLYPH * (unit + 1) + unit * 10;
}

static void draw_footer(float left, float width, const char *keys)
{
	int unit = chooser.unit;
	float y = (float)chooser.height - GLYPH * unit - (float)chooser.height * 0.05f;

	fill(left, y - unit * 10, width, (float)unit, colour_bar);
	text(left, y, unit, colour_dim, keys);
}

/* the items from y down to bottom, scrolled to show the selected one */
static void draw_items(float left, float width, float top, float bottom)
{
	int unit = chooser.unit;
	float padding = (float)(unit * 6);
	float gap = (float)(unit * 4);
	float heights[ITEMS_MAXIMUM];
	float total = 0.0f, selected_top = 0.0f, y;
	int index;

	for (index = 0; index < chooser.item_count; index++)
	{
		heights[index] = padding * 2 + GLYPH * (unit + 1) +
			(chooser.items[index].action == _action_candidate ? unit * 6 + GLYPH * unit : 0);
		if (index == chooser.selected)
			selected_top = total;
		total += heights[index] + gap;
	}
	/* (scrolled no more than needed for the selected item to show) */
	if (selected_top - chooser.scroll < 0.0f)
		chooser.scroll = selected_top;
	if (chooser.item_count && selected_top + heights[chooser.selected] - chooser.scroll > bottom - top)
		chooser.scroll = selected_top + heights[chooser.selected] - (bottom - top);
	if (total - gap <= bottom - top)
		chooser.scroll = 0.0f;
	y = top - chooser.scroll;
	for (index = 0; index < chooser.item_count; index++)
	{
		struct chooser_item *item = &chooser.items[index];
		BOOL selected = index == chooser.selected;
		float text_left = left + padding + unit * 4;
		int columns = (int)((width - padding * 2 - unit * 4) / (GLYPH * (unit + 1)));
		char line[600];

		item->rectangle.x = left;
		item->rectangle.y = y;
		item->rectangle.w = width;
		item->rectangle.h = heights[index];
		if (y + heights[index] > top - 1.0f && y < bottom + 1.0f)
		{
			fill(left, y, width, heights[index], selected ? colour_panel_selected : colour_panel);
			if (selected)
				fill(left, y, (float)(unit * 3), heights[index], colour_accent);
			if (item->action == _action_candidate)
			{
				const struct game_data_candidate *candidate = &chooser.candidates[item->candidate];
				char size[32], details[GAME_DATA_PATH_SIZE + 96];
				int detail_columns = (int)((width - padding * 2 - unit * 4) / (GLYPH * unit));

				size_text(candidate->size, size, sizeof(size));
				if (chooser.installers[item->candidate])
				{
					fit(candidate->name, columns, line, sizeof(line));
					snprintf(details, sizeof(details), "%s  -  Custom Edition installer (optional)", candidate->place);
				}
				else if (candidate->kind == _game_data_image)
				{
					fit(candidate->name, columns, line, sizeof(line));
					snprintf(details, sizeof(details), "%s  -  disc image, %s", candidate->place, size);
				}
				else
				{
					snprintf(line, sizeof(line), "Maps folder");
					snprintf(details, sizeof(details), "%s/%s  -  %s of maps", candidate->place, candidate->name,
						size);
				}
				text(text_left, y + padding, unit + 1, selected ? colour_accent : colour_text, line);
				fit(details, detail_columns, line, sizeof(line));
				text(text_left, y + padding + GLYPH * (unit + 1) + unit * 6, unit, colour_dim, line);
			}
			else
			{
				text(text_left, y + padding, unit + 1, selected ? colour_accent : colour_text, item_label(item));
			}
		}
		y += heights[index] + gap;
	}
}

/* how many of the candidates are Custom Edition's installer, and the first
that is not */
static int installer_count(void)
{
	int index, count = 0;

	for (index = 0; index < chooser.candidate_count; index++)
		count += chooser.installers[index] != FALSE;
	return count;
}

static int first_xbox_candidate(void)
{
	int index;

	for (index = 0; index < chooser.candidate_count; index++)
	{
		if (!chooser.installers[index])
			return index;
	}
	return 0;
}

static void draw_list(float left, float width)
{
	int unit = chooser.unit;
	char destination[GAME_DATA_PATH_SIZE + 64];
	char missing[GAME_DATA_PATH_SIZE + 160] = "";
	char explanation[GAME_DATA_PATH_SIZE * 3];
	float y;

	game_data_short_path(&chooser.places, chooser.places.destination, destination, (int)sizeof(destination) - 8);
	strcat(destination, "/maps");
	if (chooser.missing[0])
	{
		snprintf(missing, sizeof(missing), "The maps are no longer in %s, the folder paths.data names (moved, or "
			"on a drive not connected?). ", chooser.missing);
	}
	if (chooser.candidate_count > installer_count())
	{
		BOOL one = chooser.candidate_count - installer_count() == 1;
		const struct game_data_candidate *first = &chooser.candidates[first_xbox_candidate()];

		y = draw_heading(left, width, !one ? "Choose your Halo disc image" :
			first->kind == _game_data_image ? "Your Halo disc image was found" :
			"Your Halo maps folder was found", colour_text);
		snprintf(explanation, sizeof(explanation),
			"%sGOAT H1 plays the maps of your own Halo: Combat Evolved for the Xbox. Choosing %s copies "
			"the maps (about 2 GB) to %s.%s", missing, one ? "it" : "one", destination,
			installer_count() ? " Custom Edition's installer adds its multiplayer maps too (optional)." : "");
	}
	else
	{
		y = draw_heading(left, width, "Halo's Xbox maps were not found", colour_text);
		snprintf(explanation, sizeof(explanation),
#ifdef HALO_MACOS
			"%sGOAT H1 plays the maps of your own Halo: Combat Evolved for the Xbox. Choose Browse to pick its "
			"disc image (.iso or .xiso) or its maps folder, or put either in Downloads and choose Look again. "
			"The maps (about 2 GB) are copied to %s.\n\n"
			"%sLooked in %s.",
			missing, destination,
#else
			"%sGOAT H1 plays the maps of your own Halo: Combat Evolved for the Xbox. Put your disc image (.iso) "
			"in Downloads or on your SD card, then press A to look again.\n\n"
			"%sLooked in %s.",
			missing,
#endif
			chooser.looked_again ? "Still nothing found. " : "", chooser.places_text);
	}
	y = paragraph(left, y, width, unit, colour_dim, explanation);
	draw_items(left, width, y + unit * 8, (float)chooser.height - GLYPH * unit - (float)chooser.height * 0.05f -
		unit * 20);
	draw_footer(left, width, "A / Enter: choose     B / Esc: quit     D-pad / arrows: move");
}

static void draw_copy(float left, float width)
{
	int unit = chooser.unit;
	char explanation[GAME_DATA_PATH_SIZE * 3];
	char destination[GAME_DATA_PATH_SIZE];
	char file[256], line[400], fitted[400];
	unsigned long long done, total;
	float y, fraction;

	pthread_mutex_lock(&chooser.copy.lock);
	snprintf(file, sizeof(file), "%s", chooser.copy.file);
	done = chooser.copy.done;
	total = chooser.copy.total;
	pthread_mutex_unlock(&chooser.copy.lock);
	if (chooser.copy.installer)
	{
		y = draw_heading(left, width, "Adding Custom Edition's maps...", colour_text);
		game_data_short_path(&chooser.places, chooser.copy.folder, destination, (int)sizeof(destination));
		snprintf(explanation, sizeof(explanation), "From %s (%s) to %s. Only the maps the folder lacks are "
			"written; the installer itself stays as it is.", chooser.copy.source.name, chooser.copy.source.place,
			destination);
	}
	else
	{
		y = draw_heading(left, width, "Copying the maps...", colour_text);
		game_data_short_path(&chooser.places, chooser.copy.destination, destination, (int)sizeof(destination));
		snprintf(explanation, sizeof(explanation),
			"From %s (%s) to %s/maps. About 2 GB: this takes a few minutes.%s",
			chooser.copy.source.kind == _game_data_image ? chooser.copy.source.name : "the maps folder",
			chooser.copy.source.place, destination,
			chooser.copy.source.kind == _game_data_image ? "" : " The folder itself stays as it is.");
	}
	y = paragraph(left, y, width, unit, colour_dim, explanation) + unit * 16;
	fraction = total ? (float)((double)done / (double)total) : 0.0f;
	fill(left, y, width, (float)(unit * 10), colour_bar);
	fill(left, y, width * fraction, (float)(unit * 10), colour_accent);
	y += unit * 22;
	if (file[0])
		snprintf(line, sizeof(line), "%s   %llu of %llu MB", file, done / 1000000, total / 1000000);
	else
		snprintf(line, sizeof(line), "Starting...");
	fit(line, (int)(width / (GLYPH * unit)), fitted, sizeof(fitted));
	text(left, y, unit, colour_text, fitted);
	draw_footer(left, width, "Closing the window stops the copy.");
}

static void draw_error(float left, float width)
{
	int unit = chooser.unit;
	float y = draw_heading(left, width, chooser.error_title, chooser.notice ? colour_accent : colour_error);

	y = paragraph(left, y, width, unit, colour_dim, chooser.error);
	draw_items(left, width, y + unit * 8, (float)chooser.height - GLYPH * unit - (float)chooser.height * 0.05f -
		unit * 20);
	draw_footer(left, width, "A / Enter: choose     B / Esc: back");
}

static void draw(void)
{
	float margin, width, left;

	measure();
	margin = (float)chooser.width * 0.06f;
	if (margin < 16.0f)
		margin = 16.0f;
	width = (float)chooser.width - margin * 2;
	if (width > (float)(chooser.unit * 560))
		width = (float)(chooser.unit * 560);
	left = ((float)chooser.width - width) * 0.5f;
	set_colour(colour_background);
	SDL_RenderClear(chooser.renderer);
	if (chooser.screen == _screen_copy)
		draw_copy(left, width);
	else if (chooser.screen == _screen_error)
		draw_error(left, width);
	else
		draw_list(left, width);
	SDL_RenderPresent(chooser.renderer);
}

/* a screen saying what is happening, while it happens (the scan) */
static void draw_message(const char *headline, const char *message)
{
	float margin, width, left, y;
	int frame;

	/* (twice: some drivers show the first frame late) */
	for (frame = 0; frame < 2; frame++)
	{
		measure();
		margin = (float)chooser.width * 0.06f;
		width = (float)chooser.width - margin * 2;
		if (width > (float)(chooser.unit * 560))
			width = (float)(chooser.unit * 560);
		left = ((float)chooser.width - width) * 0.5f;
		set_colour(colour_background);
		SDL_RenderClear(chooser.renderer);
		y = draw_heading(left, width, headline, colour_text);
		paragraph(left, y, width, chooser.unit, colour_dim, message);
		SDL_RenderPresent(chooser.renderer);
	}
}

/* ---------- what the player does */

/* an Xbox disc image of Halo, or Custom Edition's installer (an .exe the
scan finds by its name, game_data_scan.c) */
static int image_check(const char *path, void *context)
{
	(void)context;
	return xiso_is_halo_image(path) || ce_installer_is_installer(path);
}

static void scan(void)
{
	int index;

	draw_message("Looking for your Halo disc image...", chooser.places_text);
	chooser.candidate_count = game_data_scan(&chooser.places, image_check, NULL, chooser.candidates,
		GAME_DATA_CANDIDATES);
	platform_log("first start: %d disc images and maps folders found", chooser.candidate_count);
	for (index = 0; index < chooser.candidate_count; index++)
	{
		/* (the scan's check has found it one or the other) */
		chooser.installers[index] = chooser.candidates[index].kind == _game_data_image &&
			!xiso_is_halo_image(chooser.candidates[index].path);
		platform_log("first start: %s %s (%s, %llu bytes)", chooser.installers[index] ? "Custom Edition installer" :
			chooser.candidates[index].kind == _game_data_image ? "disc image" : "maps folder",
			chooser.candidates[index].path, chooser.candidates[index].place, chooser.candidates[index].size);
	}
	chooser.screen = _screen_list;
	items_list();
}

static void show_error(const char *title, const char *message)
{
	snprintf(chooser.error_title, sizeof(chooser.error_title), "%s", title);
	snprintf(chooser.error, sizeof(chooser.error), "%s", message);
	chooser.notice = FALSE;
	chooser.screen = _screen_error;
	items_error();
}

/* the error screen, saying something done (Custom Edition's maps added) */
static void show_notice(const char *title, const char *message)
{
	show_error(title, message);
	chooser.notice = TRUE;
}

static void copy_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	struct chooser_copy *copy = context;

	pthread_mutex_lock(&copy->lock);
	snprintf(copy->file, sizeof(copy->file), "%s", file);
	copy->done = done;
	copy->total = total;
	pthread_mutex_unlock(&copy->lock);
}

static void *copy_thread(void *context)
{
	struct chooser_copy *copy = context;
	char error[512] = "";
	unsigned long long written = 0;
	BOOL succeeded;

	if (copy->installer)
	{
		succeeded = map_import_custom_edition(copy->source.path, copy->folder, copy_progress, copy, &written, error,
			sizeof(error)) != 0;
	}
	else if (copy->source.kind == _game_data_image)
	{
		succeeded = xiso_extract_maps(copy->source.path, copy->destination, copy_progress, copy, error,
			sizeof(error)) != 0;
		/* (and the stock maps unpacked into the cache's slots, on the
		same bar: a failure leaves only slots copied again later) */
		if (succeeded)
			map_unpack_after_extraction(copy->destination, copy_progress, copy);
	}
	else
	{
		succeeded = game_data_copy_maps(copy->source.path, copy->destination, copy_progress, copy, error,
			sizeof(error)) != 0;
	}
	pthread_mutex_lock(&copy->lock);
	snprintf(copy->error, sizeof(copy->error), "%s", error);
	copy->written = written;
	copy->succeeded = succeeded;
	copy->finished = TRUE;
	pthread_mutex_unlock(&copy->lock);
	return NULL;
}

/* copies the maps of source (with installer, Custom Edition's installer's
into custom_maps) */
static void start_copy(const struct game_data_candidate *source, BOOL installer)
{
	pthread_t thread;

	pthread_mutex_lock(&chooser.copy.lock);
	chooser.copy.source = *source;
	chooser.copy.installer = installer;
	chooser.copy.written = 0;
	map_import_custom_maps_folder(chooser.places.destination, chooser.copy.folder, sizeof(chooser.copy.folder));
	snprintf(chooser.copy.destination, sizeof(chooser.copy.destination), "%s", chooser.places.destination);
	chooser.copy.file[0] = 0;
	chooser.copy.done = 0;
	chooser.copy.total = 0;
	chooser.copy.finished = FALSE;
	chooser.copy.succeeded = FALSE;
	chooser.copy.error[0] = 0;
	pthread_mutex_unlock(&chooser.copy.lock);
	platform_log("first start: copying the maps from %s to %s", source->path,
		installer ? chooser.copy.folder : chooser.places.destination);
	if (pthread_create(&thread, NULL, copy_thread, &chooser.copy) != 0)
	{
		show_error("Could not copy the maps", "The copy could not be started.");
		return;
	}
	/* (waited for through copy.finished) */
	pthread_detach(thread);
	chooser.screen = _screen_copy;
	items_clear();
}

struct browse_answer
{
	SDL_AtomicInt done;
	BOOL failed;
	char path[GAME_DATA_PATH_SIZE];
};

static void SDLCALL browse_chosen(void *userdata, const char * const *files, int filter)
{
	struct browse_answer *answer = userdata;

	(void)filter;
	if (!files)
		answer->failed = TRUE;
	else if (files[0])
		snprintf(answer->path, sizeof(answer->path), "%s", files[0]);
	SDL_SetAtomicInt(&answer->done, 1);
}

/* the system's file dialog (the desktop portal's, or zenity's): an extra,
whose failure is said here and not offered again */
static void browse(void)
{
	static const SDL_DialogFileFilter filters[] =
	{
		{ "Xbox disc images and Custom Edition's installer", "iso;xiso;exe" },
		{ "All files", "*" },
	};
	static struct browse_answer answer;
#ifdef HALO_MACOS
	/* (the open panel is a sheet of the window, which shows in its
	fullscreen Space too) */
	BOOL fullscreen = FALSE;
#else
	BOOL fullscreen = (SDL_GetWindowFlags(chooser.window) & SDL_WINDOW_FULLSCREEN) != 0;
#endif
	struct game_data_candidate picked;
	char error[600];
	int folder;

	memset(&answer, 0, sizeof(answer));
	/* (a dialog cannot show above a fullscreen window) */
	if (fullscreen)
		SDL_SetWindowFullscreen(chooser.window, false);
	SDL_ShowOpenFileDialog(browse_chosen, &answer, chooser.window, filters, 2, chooser.places.downloads, false);
	while (!SDL_GetAtomicInt(&answer.done))
	{
		SDL_PumpEvents();
		SDL_Delay(50);
	}
	if (fullscreen)
		SDL_SetWindowFullscreen(chooser.window, true);
	if (answer.failed)
	{
		char message[800];

		platform_log("first start: no file dialog could be shown (%s)", SDL_GetError());
		chooser.browse = FALSE;
		snprintf(message, sizeof(message),
			"This system has no file picker (it needs the desktop portal or zenity). That is fine: put your "
			"disc image (.iso) in Downloads or on your SD card and choose Look again, and GOAT H1 finds it.");
		show_error("No file picker here", message);
		return;
	}
	if (!answer.path[0])
		return;
	platform_log("first start: picked %s", answer.path);
	/* (the Mac's panel takes a maps folder, or the folder holding one, as
	well as an image) */
	folder = game_data_picked_folder(answer.path, &picked, error, sizeof(error));
	if (folder < 0)
	{
		show_error("That folder holds no Xbox maps", error);
		return;
	}
	if (folder > 0)
	{
		start_copy(&picked, FALSE);
		return;
	}
	{
		const char *slash = strrchr(answer.path, '/');
		int kind = map_import_kind(answer.path);

		if (kind == _map_import_cd_image)
		{
			show_error("That is not an Xbox disc image", map_import_cd_image_text);
			return;
		}
		memset(&picked, 0, sizeof(picked));
		picked.kind = _game_data_image;
		snprintf(picked.path, sizeof(picked.path), "%s", answer.path);
		snprintf(picked.name, sizeof(picked.name), "%s", slash ? slash + 1 : answer.path);
		snprintf(picked.place, sizeof(picked.place), "%.*s", slash ? (int)(slash - answer.path) : 0, answer.path);
		/* (anything else is tried as a disc image, which says what is
		wrong with it) */
		start_copy(&picked, kind == _map_import_custom_edition_installer);
	}
}

static void activate(int index)
{
	const struct chooser_item *item;

	if (index < 0 || index >= chooser.item_count)
		return;
	item = &chooser.items[index];
	switch (item->action)
	{
	case _action_candidate:
		start_copy(&chooser.candidates[item->candidate], chooser.installers[item->candidate]);
		break;
	case _action_look_again:
		{
			int before = chooser.candidate_count;

			scan();
			chooser.looked_again = chooser.candidate_count <= before ? chooser.looked_again + 1 : 0;
		}
		break;
	case _action_browse:
		browse();
		break;
	case _action_back:
		scan();
		break;
	case _action_quit:
		platform_log("first start: no game data: quitting");
		exit(EXIT_SUCCESS);
	}
}

/* B, Escape: back from an error, else quit */
static void cancel(void)
{
	if (chooser.screen == _screen_error)
	{
		scan();
	}
	else if (chooser.screen == _screen_list)
	{
		platform_log("first start: no game data: quitting");
		exit(EXIT_SUCCESS);
	}
}

static void move(int direction)
{
	if (chooser.screen == _screen_copy || !chooser.item_count)
		return;
	chooser.selected += direction;
	if (chooser.selected < 0)
		chooser.selected = 0;
	if (chooser.selected >= chooser.item_count)
		chooser.selected = chooser.item_count - 1;
}

/* the item under a point of the window, or -1 */
static int item_at(float x, float y)
{
	int index;

	x *= chooser.pixels_x;
	y *= chooser.pixels_y;
	for (index = 0; index < chooser.item_count; index++)
	{
		const SDL_FRect *rectangle = &chooser.items[index].rectangle;

		if (x >= rectangle->x && x < rectangle->x + rectangle->w && y >= rectangle->y &&
			y < rectangle->y + rectangle->h)
		{
			return index;
		}
	}
	return -1;
}

static void handle(const SDL_Event *event)
{
	int index;

	switch (event->type)
	{
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		platform_log("first start: window closed: quitting");
		exit(EXIT_SUCCESS);
	case SDL_EVENT_GAMEPAD_ADDED:
		SDL_OpenGamepad(event->gdevice.which);
		break;
	case SDL_EVENT_KEY_DOWN:
		switch (event->key.key)
		{
		case SDLK_UP:
		case SDLK_W:
			move(-1);
			break;
		case SDLK_DOWN:
		case SDLK_S:
		case SDLK_TAB:
			move(1);
			break;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
		case SDLK_SPACE:
			if (chooser.screen != _screen_copy && !event->key.repeat)
				activate(chooser.selected);
			break;
		case SDLK_ESCAPE:
		case SDLK_BACKSPACE:
			if (!event->key.repeat)
				cancel();
			break;
		}
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		switch (event->gbutton.button)
		{
		case SDL_GAMEPAD_BUTTON_DPAD_UP:
			move(-1);
			break;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
			move(1);
			break;
		case SDL_GAMEPAD_BUTTON_SOUTH:
		case SDL_GAMEPAD_BUTTON_START:
			if (chooser.screen != _screen_copy)
				activate(chooser.selected);
			break;
		case SDL_GAMEPAD_BUTTON_EAST:
		case SDL_GAMEPAD_BUTTON_BACK:
			cancel();
			break;
		}
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
		{
			int direction = event->gaxis.value > 20000 ? 1 : event->gaxis.value < -20000 ? -1 : 0;

			/* (once a push; back near the middle before the next) */
			if (direction && direction != chooser.stick)
				move(direction);
			if (direction || (event->gaxis.value > -8000 && event->gaxis.value < 8000))
				chooser.stick = direction;
		}
		break;
	case SDL_EVENT_MOUSE_MOTION:
		index = item_at(event->motion.x, event->motion.y);
		if (index >= 0)
			chooser.selected = index;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button == SDL_BUTTON_LEFT && chooser.screen != _screen_copy)
		{
			index = item_at(event->button.x, event->button.y);
			if (index >= 0)
			{
				chooser.selected = index;
				activate(index);
			}
		}
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		move(event->wheel.y > 0.0f ? -1 : event->wheel.y < 0.0f ? 1 : 0);
		break;
	}
}

/* debug.first_start_input: an automated test's next button, one each half
second while the screen waits for the player */
static void test_input(void)
{
	static const char *next;
	static Uint64 at;
	char button[16];
	size_t length;

	if (!next)
		next = config_string("debug.first_start_input");
	if (!*next || chooser.screen == _screen_copy || SDL_GetTicks() < at)
		return;
	at = SDL_GetTicks() + 500;
	length = strcspn(next, ",");
	snprintf(button, sizeof(button), "%.*s", (int)(length < sizeof(button) ? length : sizeof(button) - 1), next);
	next += length + (next[length] == ',');
	platform_log("first start: pressing %s (debug.first_start_input)", button);
	if (!strcmp(button, "up"))
		move(-1);
	else if (!strcmp(button, "down"))
		move(1);
	else if (!strcmp(button, "a"))
		activate(chooser.selected);
	else if (!strcmp(button, "b"))
		cancel();
}

/* ---------- public code */

BOOL platform_offer_game_data(const char *destination, const char *missing)
{
	BOOL gamescope;
	SDL_JoystickID *gamepads;
	int count = 0, index;

	/* not for runs nobody is watching */
	if (config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
	{
		return FALSE;
	}
	memset(&chooser, 0, sizeof(chooser));
	pthread_mutex_init(&chooser.copy.lock, NULL);
	gamescope = first_start_gamescope();
	/* (the system's dialog only on a desktop: none under gamescope, which
	it crashes, nor in the headset) */
#ifdef HALO_VR
	chooser.browse = FALSE;
#else
	chooser.browse = !gamescope;
#endif
	game_data_default_places(&chooser.places, destination);
	game_data_places_text(&chooser.places, chooser.places_text, sizeof(chooser.places_text));
	snprintf(chooser.missing, sizeof(chooser.missing), "%s", missing ? missing : "");
	platform_log("first start: no game data; looking for a disc image (%s)%s", chooser.places_text,
		gamescope ? " under gamescope" : "");
	chooser.window = first_start_window(gamescope);
	chooser.renderer = chooser.window ? SDL_CreateRenderer(chooser.window, NULL) : NULL;
	if (!chooser.renderer)
	{
		platform_log("first start: no window could be opened (%s)", SDL_GetError());
		if (chooser.window)
			SDL_DestroyWindow(chooser.window);
		return FALSE;
	}
	SDL_SetRenderVSync(chooser.renderer, 1);
	/* (the controllers already connected; those connected later arrive as
	events) */
	gamepads = SDL_GetGamepads(&count);
	for (index = 0; gamepads && index < count; index++)
		SDL_OpenGamepad(gamepads[index]);
	SDL_free(gamepads);
	scan();
	for (;;)
	{
		SDL_Event event;

		while (SDL_PollEvent(&event))
			handle(&event);
		test_input();
		if (chooser.screen == _screen_copy)
		{
			BOOL finished, succeeded, installer;
			unsigned long long written;
			char error[512];

			pthread_mutex_lock(&chooser.copy.lock);
			finished = chooser.copy.finished;
			succeeded = chooser.copy.succeeded;
			installer = chooser.copy.installer;
			written = chooser.copy.written;
			snprintf(error, sizeof(error), "%s", chooser.copy.error);
			pthread_mutex_unlock(&chooser.copy.lock);
			if (finished && succeeded && installer)
			{
				char folder[GAME_DATA_PATH_SIZE];
				char message[GAME_DATA_PATH_SIZE + 400];

				/* (the Xbox maps are still needed: back to the list) */
				game_data_short_path(&chooser.places, chooser.copy.folder, folder, (int)sizeof(folder));
				snprintf(message, sizeof(message), "%s %s. They show in the map lists' CUSTOM MULTIPLAYER.\n\n"
					"GOAT H1 still needs the maps of your Xbox disc to start: choose Continue, then its disc image.",
					written ? "Custom Edition's maps are in" : "Custom Edition's maps were already in", folder);
				platform_log("first start: added Custom Edition's maps");
				show_notice(written ? "Custom Edition's maps were added" : "Nothing to add", message);
			}
			else if (finished && succeeded)
			{
				platform_log("first start: copied the maps");
				break;
			}
			else if (finished)
			{
				char message[900];

				platform_log("first start: the copy failed: %s", error);
				snprintf(message, sizeof(message), "%s\n\nChoose Back to look again, or choose another file.",
					error);
				show_error("Could not copy the maps", message);
			}
		}
		draw();
		/* (where vsync is not honoured) */
		SDL_Delay(10);
	}
	SDL_DestroyRenderer(chooser.renderer);
	SDL_DestroyWindow(chooser.window);
	/* (the window gone from the screen, its closing animation over, before
	the game loads its first map, which it does without reading events) */
	for (count = 0; count < 15; count++)
	{
		SDL_PumpEvents();
		SDL_Delay(20);
	}
	return TRUE;
}

#endif
