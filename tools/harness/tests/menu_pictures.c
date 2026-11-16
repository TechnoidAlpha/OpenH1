/*
MENU_PICTURES.C (test)

The menus' pictures drawn again from the player's maps
(port/linux/src/menu_pictures.c): the scale a screen asks for, the cache's
decision (the cached picture drawn, rendered again, or the original kept:
a missing or modded map, a note of another pose or map, a picture too
small, failed renders), the order the preparing pass renders them in and
the pictures it counts (none whose map is missing or modded; with none,
no pass), a render noted before it begins (retried after a failure or a
crash, then given up), a pass resumed at the next start, its notes written
and read back, the colour curve,
the picture cut from the middle of a frame and laid out as the original
frame is, and the game's start's phases (the intro, the pass, the main menu). test_menu_pictures.py takes the header (menu_pictures_h.inc), the
constants (config.inc) and the code (under_test.inc).
*/

#include "harness.h"
#include "menu_pictures_h.inc"
#include "config.inc"

#include "under_test.inc"

static struct menu_picture_pose pose_made_for(unsigned long checksum)
{
	struct menu_picture_pose pose;

	memset(&pose, 0, sizeof(pose));
	pose.name = "sp_levels_00";
	pose.checksums[0] = 0x11111111UL;
	pose.checksums[1] = checksum;
	pose.checksum_count = 2;
	pose.hash = 0xABCDEF01UL;
	return pose;
}

static struct menu_picture_note note_of(struct menu_picture_pose const *pose, unsigned long checksum,
	enum menu_picture_result result, long scale, long failures)
{
	struct menu_picture_note note;

	memset(&note, 0, sizeof(note));
	note.result = result;
	note.pose_hash = pose->hash;
	note.map_checksum = checksum;
	note.scale = scale;
	note.failures = failures;
	return note;
}

#define USE (1UL << _menu_picture_use_bit)
#define RENDER (1UL << _menu_picture_render_bit)

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct menu_picture_pose pose = pose_made_for(0x22222222UL);

	/* as large as the largest card draws the frame (322 units of 480 lines
	for its 256 texels), whole, from 2 to 6 */
	CASE("scale")
	{
		CHECK(menu_picture_scale_for_height(480) == 2, "480 lines: %ld", menu_picture_scale_for_height(480));
		CHECK(menu_picture_scale_for_height(720) == 2, "720 lines: %ld", menu_picture_scale_for_height(720));
		CHECK(menu_picture_scale_for_height(800) == 3, "the Steam Deck's 800: %ld", menu_picture_scale_for_height(800));
		CHECK(menu_picture_scale_for_height(1080) == 3, "1080: %ld", menu_picture_scale_for_height(1080));
		CHECK(menu_picture_scale_for_height(1440) == 4, "1440: %ld", menu_picture_scale_for_height(1440));
		CHECK(menu_picture_scale_for_height(2160) == 6, "4K: %ld", menu_picture_scale_for_height(2160));
		CHECK(menu_picture_scale_for_height(4320) == 6, "past 4K: %ld", menu_picture_scale_for_height(4320));
		CHECK(menu_picture_scale_for_height(0) == 2, "no screen yet: %ld", menu_picture_scale_for_height(0));
	}

	/* no map, or a map the pose was not made for (modded): the original,
	never rendered */
	CASE("fallback")
	{
		struct menu_picture_note note = note_of(&pose, 0x33333333UL, _menu_picture_rendered, 6, 0);

		CHECK(menu_picture_decide(&pose, 0, NULL, 3) == 0, "a missing map was rendered");
		CHECK(menu_picture_decide(&pose, 0x33333333UL, NULL, 3) == 0, "a modded map was rendered");
		CHECK(menu_picture_decide(&pose, 0x33333333UL, &note, 3) == 0,
			"a modded map's picture was drawn, though a note says it was rendered");
		CHECK(menu_picture_decide(&pose, 0x11111111UL, NULL, 3) == RENDER, "the pose's first checksum was not taken");
		CHECK(menu_picture_decide(&pose, 0x22222222UL, NULL, 3) == RENDER, "the pose's second checksum was not taken");
	}

	/* a note of this pose and map: its picture drawn; too small for the
	screen, drawn and rendered larger; of another pose or map, rendered */
	CASE("cached")
	{
		struct menu_picture_note note = note_of(&pose, 0x22222222UL, _menu_picture_rendered, 3, 0);

		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == USE, "a cached picture was not drawn alone");
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 2) == USE, "a larger cached picture was rendered again");
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 6) == (USE | RENDER),
			"a picture too small for the screen was not drawn and rendered larger");
		note.pose_hash ^= 1;
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == RENDER, "a changed pose's picture was drawn");
		note.pose_hash ^= 1;
		CHECK(menu_picture_decide(&pose, 0x11111111UL, &note, 3) == RENDER, "another map's picture was drawn");
	}

	/* failed renders tried again until the second; a modded map noted: the
	original */
	CASE("failures")
	{
		struct menu_picture_note note = note_of(&pose, 0x22222222UL, _menu_picture_failed, 3, 1);

		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == RENDER, "a first failure was not tried again");
		note.failures = 2;
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == 0, "a second failure was tried again");
		note = note_of(&pose, 0x22222222UL, _menu_picture_modded, 3, 0);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == 0, "a map noted modded was rendered");
		note = note_of(&pose, 0x22222222UL, _menu_picture_none, 3, 0);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == RENDER, "a note of nothing was not rendered");
	}

	/* the poses to render in the order they are rendered: a map's together,
	in the order of its first pose, none that is not to be rendered */
	CASE("order")
	{
		struct menu_picture_pose poses[5];
		unsigned long decisions[5] = { RENDER, USE | RENDER, RENDER, USE, RENDER };
		static char const *const maps[5] = { "b30", "a10", "b30", "a10", "c10" };
		long order[5], count, index;

		memset(poses, 0, sizeof(poses));
		for (index = 0; index < 5; index++)
			poses[index].map = maps[index];
		count = menu_picture_order(poses, 5, decisions, order, 5);
		CHECK(count == 4, "%ld poses ordered, not 4", count);
		CHECK(order[0] == 0 && order[1] == 2 && order[2] == 1 && order[3] == 4,
			"the order is %ld %ld %ld %ld, not 0 2 1 4", order[0], order[1], order[2], order[3]);
		CHECK(menu_picture_order(poses, 5, decisions, order, 2) == 2, "more poses ordered than there is room for");
		/* (a map's first pose drawn alone does not bring its others forward) */
		poses[0].map = "c10";
		decisions[0] = USE;
		count = menu_picture_order(poses, 5, decisions, order, 5);
		CHECK(count == 3 && order[0] == 1 && order[1] == 2 && order[2] == 4,
			"the order is %ld %ld %ld, not 1 2 4", order[0], order[1], order[2]);
		decisions[0] = decisions[1] = decisions[2] = decisions[4] = USE;
		CHECK(menu_picture_order(poses, 5, decisions, order, 5) == 0, "a pass with nothing to render");
	}

	/* a render noted as failed before it begins: the game gone in the middle
	of one (a crash) tried again once, then the original kept; one that ends
	rendered forgets its failures; a larger render that fails keeps the
	smaller picture drawn */
	CASE("retry")
	{
		struct menu_picture_note note;

		memset(&note, 0, sizeof(note));
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 3);
		CHECK(note.failures == 1 && note.pose_hash == pose.hash && note.map_checksum == 0x22222222UL,
			"a render begun was not noted: %ld failures", note.failures);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == RENDER, "a render gone once was not tried again");
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 3);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == 0, "a render gone twice was tried again");
		memset(&note, 0, sizeof(note));
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 3);
		menu_picture_note_end(&note, _menu_picture_failed, 3);
		CHECK(note.result == _menu_picture_failed && note.failures == 1, "a failure was noted %d, %ld",
			note.result, note.failures);
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 3);
		menu_picture_note_end(&note, _menu_picture_rendered, 3);
		CHECK(note.result == _menu_picture_rendered && note.failures == 0 && note.scale == 3,
			"a render after a failure was noted %d, %ld failures, scale %ld", note.result, note.failures, note.scale);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == USE, "a rendered picture was not drawn");
		/* (a larger screen: rendered again; gone twice, the smaller drawn) */
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 6);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 6) == (USE | RENDER),
			"a larger render gone once was not tried again, the smaller drawn");
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 6);
		menu_picture_note_end(&note, _menu_picture_failed, 6);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 6) == USE,
			"a larger render that failed twice did not leave the smaller picture drawn alone");
		/* (a note of another map begun anew) */
		note = note_of(&pose, 0x11111111UL, _menu_picture_rendered, 3, 1);
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 3);
		CHECK(note.result == _menu_picture_none && note.failures == 1 && note.map_checksum == 0x22222222UL,
			"another map's note was carried into a render of this one");
	}

	/* the pictures a start renders, and counts: none whose map is missing
	or modded, nor one that failed twice; with none at all (no maps) there
	is no pass, so no screen */
	CASE("makeable")
	{
		struct menu_picture_pose poses[4];
		struct menu_picture_note failed_twice = note_of(&pose, 0x22222222UL, _menu_picture_failed, 3, 2);
		unsigned long decisions[4];
		long order[4], index, count;

		for (index = 0; index < 4; index++)
		{
			poses[index] = pose;
			poses[index].map = index == 0 ? "a10" : index == 1 ? "a30" : index == 2 ? "a50" : "b30";
		}
		decisions[0] = menu_picture_decide(&poses[0], 0, NULL, 3); /* (missing) */
		decisions[1] = menu_picture_decide(&poses[1], 0x33333333UL, NULL, 3); /* (modded) */
		decisions[2] = menu_picture_decide(&poses[2], 0x22222222UL, &failed_twice, 3);
		decisions[3] = menu_picture_decide(&poses[3], 0x22222222UL, NULL, 3);
		count = menu_picture_order(poses, 4, decisions, order, 4);
		CHECK(count == 1 && order[0] == 3, "%ld pictures counted (the first %ld), not the one that can be made",
			count, order[0]);
		for (index = 0; index < 4; index++)
			decisions[index] = menu_picture_decide(&poses[index], 0, NULL, 3);
		CHECK(menu_picture_order(poses, 4, decisions, order, 4) == 0, "with no maps, a pass (and its screen)");
	}

	/* a pass part way when a picture fails, or the game is gone in the
	middle of one (a crash): what was made is drawn; the failed one and
	the one gone are tried once more at the next start, with those not
	reached; twice gone, the original is kept and the rest go on */
	CASE("resume")
	{
		struct menu_picture_pose poses[4];
		struct menu_picture_note notes[4];
		unsigned long decisions[4];
		long order[4], index, count;

		for (index = 0; index < 4; index++)
		{
			poses[index] = pose;
			poses[index].map = index == 0 ? "a10" : index == 1 ? "a30" : index == 2 ? "a50" : "b30";
			memset(&notes[index], 0, sizeof(notes[index]));
		}
		/* (the first made, the second failed, the third gone mid-way, the
		fourth not reached) */
		menu_picture_note_begin(&notes[0], &poses[0], 0x22222222UL, 3);
		menu_picture_note_end(&notes[0], _menu_picture_rendered, 3);
		menu_picture_note_begin(&notes[1], &poses[1], 0x22222222UL, 3);
		menu_picture_note_end(&notes[1], _menu_picture_failed, 3);
		menu_picture_note_begin(&notes[2], &poses[2], 0x22222222UL, 3);
		for (index = 0; index < 3; index++)
			decisions[index] = menu_picture_decide(&poses[index], 0x22222222UL, &notes[index], 3);
		decisions[3] = menu_picture_decide(&poses[3], 0x22222222UL, NULL, 3);
		CHECK(decisions[0] == USE, "a picture made before the crash was not drawn alone");
		CHECK(decisions[1] == RENDER && decisions[2] == RENDER && decisions[3] == RENDER,
			"the failed, the crashed and the unreached pictures were not all tried at the next start");
		count = menu_picture_order(poses, 4, decisions, order, 4);
		CHECK(count == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3, "the next start renders %ld, from %ld",
			count, order[0]);
		/* (gone again: kept the original, the others still made) */
		menu_picture_note_begin(&notes[2], &poses[2], 0x22222222UL, 3);
		decisions[2] = menu_picture_decide(&poses[2], 0x22222222UL, &notes[2], 3);
		CHECK(decisions[2] == 0, "a picture gone twice was tried a third time");
		count = menu_picture_order(poses, 4, decisions, order, 4);
		CHECK(count == 2 && order[0] == 1 && order[1] == 3, "the start after renders %ld, from %ld", count, order[0]);
	}

	/* a note read back as written; another version's, or one missing a
	line, is none */
	CASE("notes")
	{
		struct menu_picture_note note = note_of(&pose, 0xFEDCBA98UL, _menu_picture_failed, 5, 1), read;
		char text[256];

		CHECK(menu_picture_note_format(&note, text, sizeof(text)), "the note did not fit");
		CHECK(menu_picture_note_parse(text, &read), "the note was not read: %s", text);
		CHECK(read.result == note.result && read.pose_hash == note.pose_hash &&
			read.map_checksum == note.map_checksum && read.scale == note.scale && read.failures == note.failures,
			"the note read is not the note written: %s", text);
		CHECK(!menu_picture_note_format(&note, text, 20), "a note that does not fit was written");
		CHECK(!menu_picture_note_parse("version 999\nresult rendered\npose 1\nmap 2\nscale 3\n", &read),
			"another version's note was read");
		CHECK(!menu_picture_note_parse("version 2\nresult rendered\npose 1\nscale 3\n", &read),
			"a note without its map was read");
		CHECK(!menu_picture_note_parse("version 2\nresult maybe\npose 1\nmap 2\nscale 3\n", &read),
			"a note of an unknown result was read");
		CHECK(!menu_picture_note_parse("", &read) && read.result == _menu_picture_none, "nothing was read as a note");
	}

	/* the curve takes the pixels' colours toward the reference's, each
	channel on its own, never reversing their order */
	CASE("colour")
	{
		static unsigned char pixels[256 * 4], reference[200 * 4];
		long index, previous = -1;
		double sum = 0.0, reference_sum = 0.0;

		for (index = 0; index < 256; index++)
		{
			pixels[index * 4 + 0] = (unsigned char)(index / 4); /* (dark: 0 to 63) */
			pixels[index * 4 + 1] = (unsigned char)index;
			pixels[index * 4 + 2] = 77;
			pixels[index * 4 + 3] = 255;
		}
		for (index = 0; index < 200; index++)
		{
			reference[index * 4 + 0] = (unsigned char)(150 + index / 4); /* (bright: 150 to 199) */
			reference[index * 4 + 1] = (unsigned char)(index * 255 / 199);
			reference[index * 4 + 2] = 200;
			reference[index * 4 + 3] = 255;
			reference_sum += reference[index * 4 + 0];
		}
		menu_picture_colour_curve(pixels, 256, reference, 200);
		for (index = 0; index < 256; index++)
		{
			CHECK(pixels[index * 4 + 0] >= previous, "red fell at %ld: %d after %ld", index, pixels[index * 4 + 0],
				previous);
			previous = pixels[index * 4 + 0];
			sum += pixels[index * 4 + 0];
			CHECK(pixels[index * 4 + 2] >= 195 && pixels[index * 4 + 2] <= 200, "a flat blue went to %d, not 200's",
				pixels[index * 4 + 2]);
			CHECK(pixels[index * 4 + 3] == 255, "alpha changed");
		}
		CHECK(sum / 256 > reference_sum / 200 - 6 && sum / 256 < reference_sum / 200 + 6,
			"red's mean is %.1f, not the reference's %.1f", sum / 256, reference_sum / 200);
		/* (the same spread: green, 0 to 255 in both, kept within a few levels) */
		CHECK(abs(pixels[128 * 4 + 1] - 128) <= 6, "green's middle went to %d", pixels[128 * 4 + 1]);
	}

	/* the frame's middle cut to the picture's shape, its pixels averaged to
	the picture's size, BGRA to RGBA */
	CASE("cut")
	{
		static unsigned char frame[160 * 90 * 4], tall[90 * 160 * 4], picture[45 * 45 * 4];
		long x, y;

		/* (a 16:9 frame: blue in its middle 90 columns, red to either side) */
		for (y = 0; y < 90; y++)
		{
			for (x = 0; x < 160; x++)
			{
				unsigned char *texel = frame + (y * 160 + x) * 4;
				int middle = x >= 35 && x < 125;

				texel[0] = middle ? 255 : 0; /* (blue) */
				texel[1] = 0;
				texel[2] = middle ? 0 : 255; /* (red) */
				texel[3] = 0; /* (the game's scratch) */
			}
		}
		CHECK(menu_picture_cut(frame, 160, 90, 1.0f, picture, 9, 9), "the cut failed");
		for (x = 0; x < 9 * 9; x++)
		{
			CHECK(picture[x * 4 + 0] == 0 && picture[x * 4 + 2] == 255 && picture[x * 4 + 3] == 255,
				"pixel %ld is %d %d %d %d, not the middle's blue", x, picture[x * 4], picture[x * 4 + 1],
				picture[x * 4 + 2], picture[x * 4 + 3]);
		}
		/* (a tall frame: its middle rows; the top and bottom 35 green) */
		for (y = 0; y < 160; y++)
		{
			for (x = 0; x < 90; x++)
			{
				unsigned char *texel = tall + (y * 90 + x) * 4;
				int middle = y >= 35 && y < 125;

				texel[0] = middle ? 255 : 0;
				texel[1] = middle ? 0 : 255;
				texel[2] = 0;
				texel[3] = 255;
			}
		}
		CHECK(menu_picture_cut(tall, 90, 160, 1.0f, picture, 9, 9), "the tall cut failed");
		for (x = 0; x < 9 * 9; x++)
			CHECK(picture[x * 4 + 1] == 0 && picture[x * 4 + 2] == 255, "tall pixel %ld is not the middle's blue", x);
		/* (averaged: two columns of 0 and 255 to one of 128) */
		for (y = 0; y < 90; y++)
		{
			for (x = 0; x < 160; x++)
				frame[(y * 160 + x) * 4 + 1] = (unsigned char)(x % 2 ? 255 : 0);
		}
		CHECK(menu_picture_cut(frame, 160, 90, 1.0f, picture, 45, 45), "the halved cut failed");
		CHECK(picture[(20 * 45 + 20) * 4 + 1] == 128, "a pair of 0 and 255 averaged to %d",
			picture[(20 * 45 + 20) * 4 + 1]);
	}

	/* the pass never waits for ever: a picture past its budget, or the
	whole pass past its own, ends it (the main menu shown); within both, it
	goes on (seconds, in nanoseconds) */
	CASE("budget")
	{
		unsigned long long const second = 1000000000ULL;
		unsigned long long const pass = 1000 * second;

		CHECK(!menu_picture_overdue(pass, pass, pass), "a pass just begun is overdue");
		CHECK(!menu_picture_overdue(pass + 29 * second, pass, pass), "a picture of 29 s is overdue");
		CHECK(menu_picture_overdue(pass + 31 * second, pass, pass), "a picture stalled for 31 s goes on");
		CHECK(menu_picture_overdue(pass + 3600 * second, pass, pass), "a picture stalled for an hour goes on");
		/* (each picture well within its own budget, the pass past its) */
		CHECK(!menu_picture_overdue(pass + 119 * second, pass, pass + 110 * second), "a pass of 119 s is overdue");
		CHECK(menu_picture_overdue(pass + 121 * second, pass, pass + 110 * second),
			"a pass of 121 s of quick pictures goes on");
		CHECK(!menu_picture_overdue(pass - second, pass, pass), "a clock gone back is overdue");
	}

	/* a render that stopped the game is never tried again (for this pose
	and map), and a picture rendered before it is still drawn */
	CASE("give_up")
	{
		struct menu_picture_note note;

		memset(&note, 0, sizeof(note));
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 3);
		menu_picture_note_give_up(&note);
		CHECK(note.result == _menu_picture_failed, "a render given up is noted %d, not failed", note.result);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == 0, "a render given up once is tried again");
		note = note_of(&pose, 0x22222222UL, _menu_picture_rendered, 2, 0);
		menu_picture_note_begin(&note, &pose, 0x22222222UL, 6);
		menu_picture_note_give_up(&note);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 6) == USE,
			"a larger render given up hides or retries the picture rendered before (%lx)",
			menu_picture_decide(&pose, 0x22222222UL, &note, 6));
		/* (a new map, or pose, is tried again) */
		CHECK(menu_picture_decide(&pose, 0x11111111UL, &note, 3) == RENDER, "a changed map is not tried");
	}

	/* the game's start, in order: the intro movie played to its end or
	skipped, then the pictures' pass, then the main menu's menus and music;
	none begins before the one before it has ended, and the pass has the game
	to itself from its start to its end's main menu (issue 550: the pass ran
	under the intro, which held the texture cache's spare memory the maps'
	files are copied through and drew over the frames kept) */
	CASE("startup")
	{
		enum { INTRO = _menu_pictures_startup_intro, PICTURES = _menu_pictures_startup_pictures,
			DONE = _menu_pictures_startup_done };

		/* (the intro: until the main menu's map is loaded and no movie is
		open; the pass's state is not its business) */
		CHECK(menu_pictures_startup_next(INTRO, FALSE, FALSE, FALSE) == INTRO, "the start goes on before the main menu");
		CHECK(menu_pictures_startup_next(INTRO, TRUE, TRUE, FALSE) == INTRO, "the pass begins while the intro plays");
		CHECK(menu_pictures_startup_next(INTRO, FALSE, TRUE, FALSE) == INTRO, "the start goes on in a movie");
		CHECK(menu_pictures_startup_next(INTRO, TRUE, FALSE, FALSE) == PICTURES,
			"the pass does not begin once the intro has ended");
		/* (the pass: while it has the game, whatever else is so) */
		CHECK(menu_pictures_startup_next(PICTURES, TRUE, FALSE, TRUE) == PICTURES, "the main menu comes during the pass");
		CHECK(menu_pictures_startup_next(PICTURES, FALSE, FALSE, TRUE) == PICTURES,
			"the main menu comes as the pass loads a map");
		CHECK(menu_pictures_startup_next(PICTURES, TRUE, FALSE, FALSE) == DONE, "the start never ends after the pass");
		CHECK(menu_pictures_startup_next(DONE, TRUE, TRUE, TRUE) == DONE, "the start begins again");
		/* (the main menu's menus and music wait through the intro and the
		pass, not for the pass's own main menu at its end) */
		CHECK(menu_pictures_startup_holds_main_menu(INTRO, FALSE), "the main menu shows before the intro has ended");
		CHECK(menu_pictures_startup_holds_main_menu(PICTURES, TRUE), "the main menu shows during the pass");
		CHECK(!menu_pictures_startup_holds_main_menu(PICTURES, FALSE), "the pass's end's main menu is held");
		CHECK(!menu_pictures_startup_holds_main_menu(DONE, FALSE), "the main menu is held after the start");
		CHECK(!menu_pictures_startup_holds_main_menu(DONE, TRUE), "the main menu is held after the start");
		/* (the pass has the game from its start to its end's main menu) */
		CHECK(menu_pictures_phase_exclusive(TRUE, FALSE), "the running pass shares the game");
		CHECK(!menu_pictures_phase_exclusive(TRUE, TRUE), "the ended pass keeps the game: no intro after it");
		CHECK(!menu_pictures_phase_exclusive(FALSE, FALSE), "no pass has the game");
	}

	/* a cached picture noted as rendered is drawn only if its file is a
	whole PNG of its scale's size: one cut short, of another size, or not a
	PNG is rendered again */
	CASE("whole")
	{
		static unsigned char const ihdr[24] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D',
			'R', 0, 0, 3, 0, 0, 0, 1, 128 };
		static unsigned char const iend[12] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
		unsigned char head[24], tail[12];

		CHECK(menu_picture_png_whole(ihdr, 24, iend, 12, 768, 384), "a whole 768x384 PNG is not whole");
		CHECK(!menu_picture_png_whole(ihdr, 24, iend, 12, 512, 256), "a 768x384 PNG is taken for a 512x256 one");
		CHECK(!menu_picture_png_whole(ihdr, 24, iend, 12, 768, 256), "a 768x384 PNG is taken for a 768x256 one");
		/* (cut short: its last bytes the picture's data, not its end) */
		memcpy(tail, iend, 12);
		tail[11] = 0;
		CHECK(!menu_picture_png_whole(ihdr, 24, tail, 12, 768, 384), "a PNG cut short is whole");
		CHECK(!menu_picture_png_whole(ihdr, 24, iend, 4, 768, 384), "a file of 28 bytes is whole");
		memcpy(head, ihdr, 24);
		head[1] = 'J';
		CHECK(!menu_picture_png_whole(head, 24, iend, 12, 768, 384), "a file that is not a PNG is whole");
		CHECK(!menu_picture_png_whole(ihdr, 16, iend, 12, 768, 384), "a file of no size is whole");
	}

	/* laid out in the frame's top left, its last column and row repeated */
	CASE("frame")
	{
		unsigned char picture[2 * 2 * 4] = { 1, 1, 1, 255, 2, 2, 2, 255, 3, 3, 3, 255, 4, 4, 4, 255 };
		unsigned char frame[5 * 3 * 4];
		static const unsigned char expected[3][5] = { { 1, 2, 2, 2, 2 }, { 3, 4, 4, 4, 4 }, { 3, 4, 4, 4, 4 } };
		long x, y;

		menu_picture_frame(picture, 2, 2, frame, 5, 3);
		for (y = 0; y < 3; y++)
		{
			for (x = 0; x < 5; x++)
				CHECK(frame[(y * 5 + x) * 4] == expected[y][x], "texel %ld,%ld is %d, not %d", x, y,
					frame[(y * 5 + x) * 4], expected[y][x]);
		}
	}
	return 0;
}
