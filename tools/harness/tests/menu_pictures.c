/*
MENU_PICTURES.C (test)

The menus' pictures drawn again from the player's maps
(port/linux/src/menu_pictures.c): the scale a screen asks for, the cache's
decision (the cached picture drawn, rendered again, or the original kept:
a missing or modded map, a note of another pose or map, a picture too
small, failed renders), its notes written and read back, the colour curve,
the picture cut from the middle of a frame and laid out as the original
frame is. test_menu_pictures.py takes the header (menu_pictures_h.inc), the
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

	/* failed renders tried again until the second; a modded map noted by
	the copy: the original */
	CASE("failures")
	{
		struct menu_picture_note note = note_of(&pose, 0x22222222UL, _menu_picture_failed, 3, 1);

		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == RENDER, "a first failure was not tried again");
		note.failures = 2;
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == 0, "a second failure was tried again");
		note = note_of(&pose, 0x22222222UL, _menu_picture_modded, 3, 0);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == 0, "a map the copy found modded was rendered");
		note = note_of(&pose, 0x22222222UL, _menu_picture_none, 3, 0);
		CHECK(menu_picture_decide(&pose, 0x22222222UL, &note, 3) == RENDER, "a note of nothing was not rendered");
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
		CHECK(!menu_picture_note_parse("version 1\nresult rendered\npose 1\nscale 3\n", &read),
			"a note without its map was read");
		CHECK(!menu_picture_note_parse("version 1\nresult maybe\npose 1\nmap 2\nscale 3\n", &read),
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
