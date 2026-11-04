/*
MENU_PICTURES.C

The menus' pictures of the missions and the multiplayer maps (ui.map's
sp_levels and mp_map_grafix: 140x114 in the top left of 256x128 frames)
drawn again, sharp, from the player's own maps (menu_pictures.h).

Each picture's pose (port/assets/pictures: a camera's place, numbers only)
is rendered once by the game itself, in its own window, as its main menu
first shows: "Preparing menu pictures" (port/linux/game/menu_pictures_render.c),
a map at a time, loaded the normal way, the scene set, one frame drawn and
kept. This file is its platform half: which pictures are rendered and in
what order (menu_pictures_wanted), each frame kept cut to the picture's
shape and scaled to 140x114 times the scale this screen asks for
(menu_picture_scale_for_height: what the largest card needs, up to 4K's),
its colours taken toward the original's where the pose says (rr_final.py's
curve: a smooth rising tone curve per channel), and written, laid out as
the original frame is, as a PNG and a note of what it is in the data
folder's menu_pictures; then each picture decoded on a thread of its own
and drawn in place of the original frame wherever the menus draw that frame
(xbox_textures.c, menu_picture_at), whatever size or crop they draw it at.

The original is drawn instead whenever the picture cannot be: the map is
missing, or not the one the pose was made for (its cache file checksum is
not one of the pose's: a modded map), or its render failed twice (a
failure, or the game gone in the middle of one: each render is noted as a
failure before it starts, and as rendered when it ends); and until it is
ready. A pose or map changed, a larger
screen (at the next start), or this file's version renders it again.
Nothing is rendered in runs nobody watches (tests: debug.hidden_window,
debug.exit_after...), in VR, or with display.menu_pictures off.
*/

#include "menu_pictures.h"
#include "hud_hires.h"
#include "platform.h"
#include "port_config.h"
#include "xgpu.h"
#include "xgpu_images.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's: the game's <sys/stat.h> is MSVC's, as install_layout.c says) */
int mkdir(const char *path, unsigned int mode);

/* ---------- constants */

/* the cache's notes say which version of the rendering made them: a change
of the rendering renders every picture again */
#define MENU_PICTURES_VERSION 2
/* renders of a pose (for a map) that did not finish before the original is
kept */
#define MENU_PICTURE_FAILURES 2
/* the largest a menu draws a frame: the campaign's mission card, 322 of
the 640x480 screen's units for the frame's 256 texels (main_menu.campaign.xml) */
#define MENU_PICTURE_LARGEST_UNITS 322
/* (nanoseconds) the most a picture's render may take, from its map's file
read to its frame kept, and the whole pass: past either, the pass ends and
the main menu shows, the pictures not made keeping their originals (a slow
disc's ten seconds a map is a third of a picture's) */
#define MENU_PICTURE_TIME_BUDGET 30000000000ULL
#define MENU_PICTURES_TIME_BUDGET 120000000000ULL

/* ---------- prototypes */

/* the platform layer's */
int halo_server_mode(void);
float halo_screen_pixel_scale(void);
#ifdef HALO_VR
int halo_vr_running(void);
#endif
/* d3d8_device.c's: the frame being drawn's pixels so far (BGRA, rows from
the top; malloc's), and its size */
unsigned char *d3d8_back_buffer_read(unsigned long *width, unsigned long *height);
/* the game's zlib (source/memory/zlib, data_compress.c's) */
int compress2(unsigned char *destination, unsigned long *destination_size, const unsigned char *source,
	unsigned long source_size, int level);

/* ---------- the pure parts (tools/harness/tests/menu_pictures.c) */

/* how many times larger than 140x114 a picture is drawn for a screen of
this many lines: as large as the largest card shows it, from 2 to 6 (4K) */
long menu_picture_scale_for_height(
	long screen_height)
{
	long units = 480L * MENU_PICTURE_FRAME_WIDTH;
	long scale = (screen_height * MENU_PICTURE_LARGEST_UNITS + units - 1) / units;

	if (scale < MENU_PICTURE_SMALLEST_SCALE)
		scale = MENU_PICTURE_SMALLEST_SCALE;
	if (scale > MENU_PICTURE_LARGEST_SCALE)
		scale = MENU_PICTURE_LARGEST_SCALE;
	return scale;
}

/* what to do with a pose's picture (_menu_picture_*_bit), given the
checksum of the player's map (0: there is none) and the cache's note of it
(NULL: none) */
unsigned long menu_picture_decide(
	struct menu_picture_pose const *pose,
	unsigned long map_checksum,
	struct menu_picture_note const *note,
	long wanted_scale)
{
	long index;
	int made_for = 0;
	unsigned long retry;

	/* (no map, 0, or not the map the pose was made for: the original) */
	for (index = 0; index < pose->checksum_count && index < MAXIMUM_MENU_PICTURE_CHECKSUMS; index++)
		made_for |= pose->checksums[index] == map_checksum;
	if (!made_for)
		return 0;
	/* (a note of another pose or map: rendered again) */
	if (!note || note->pose_hash != pose->hash || note->map_checksum != map_checksum)
		return 1UL << _menu_picture_render_bit;
	/* (renders since that did not finish: tried again, up to a point) */
	retry = note->failures < MENU_PICTURE_FAILURES ? 1UL << _menu_picture_render_bit : 0;
	switch (note->result)
	{
	case _menu_picture_rendered:
		/* (too small for this screen: drawn, and a larger one rendered) */
		return (1UL << _menu_picture_use_bit) | (note->scale < wanted_scale ? retry : 0);
	case _menu_picture_modded:
		return 0;
	default:
		return retry;
	}
}

/* the order the poses to render (render in decisions) are rendered in,
into order: those of a map together, in the order of their maps' first
pose, and in their own order there (each map's file decompressed into the
cache once, the next's while the one before is drawn); how many */
long menu_picture_order(
	struct menu_picture_pose const *poses,
	long count,
	unsigned long const *decisions,
	long *order,
	long maximum)
{
	long ordered = 0, first, pose;

	for (first = 0; first < count; first++)
	{
		long before;
		int seen = 0;

		if (!(decisions[first] & (1UL << _menu_picture_render_bit)))
			continue;
		for (before = 0; before < first && !seen; before++)
		{
			seen = (decisions[before] & (1UL << _menu_picture_render_bit)) &&
				!strcmp(poses[before].map, poses[first].map);
		}
		if (seen)
			continue;
		for (pose = first; pose < count && ordered < maximum; pose++)
		{
			if ((decisions[pose] & (1UL << _menu_picture_render_bit)) && !strcmp(poses[pose].map, poses[first].map))
				order[ordered++] = pose;
		}
	}
	return ordered;
}

/* a pose's note as its render begins: counted as failed until it ends
(what was rendered before, for this pose and map, kept meanwhile) */
void menu_picture_note_begin(
	struct menu_picture_note *note,
	struct menu_picture_pose const *pose,
	unsigned long map_checksum,
	long scale)
{
	if (note->pose_hash != pose->hash || note->map_checksum != map_checksum)
	{
		memset(note, 0, sizeof(*note));
		note->result = _menu_picture_none;
		note->pose_hash = pose->hash;
		note->map_checksum = map_checksum;
		note->scale = scale;
	}
	note->failures++;
}

/* ...and as it ends: rendered at this scale (its failures forgotten), or
not (failed: counted; modded: the map not the pose's) */
void menu_picture_note_end(
	struct menu_picture_note *note,
	enum menu_picture_result result,
	long scale)
{
	switch (result)
	{
	case _menu_picture_rendered:
		note->result = result;
		note->scale = scale;
		note->failures = 0;
		break;
	case _menu_picture_modded:
		note->result = result;
		break;
	default:
		/* (a picture rendered before is still drawn) */
		if (note->result != _menu_picture_rendered)
			note->result = _menu_picture_failed;
		break;
	}
}

/* ...or given up on at once: a render that stopped the game (its map's file
not read, the error a damaged disc gives): never tried again for this pose
and map (a picture rendered before is still drawn) */
void menu_picture_note_give_up(
	struct menu_picture_note *note)
{
	if (note->result != _menu_picture_rendered)
		note->result = _menu_picture_failed;
	if (note->failures < MENU_PICTURE_FAILURES)
		note->failures = MENU_PICTURE_FAILURES;
}

/* whether the pass has taken too long (now, when it began and when the
picture being made began, in nanoseconds of one clock): the picture's own
budget or the pass's spent */
int menu_picture_overdue(
	unsigned long long now,
	unsigned long long pass_began,
	unsigned long long picture_began)
{
	/* (a clock gone back: not overdue, the budget counted from now) */
	if (now < pass_began || now < picture_began)
		return 0;
	return now - picture_began > MENU_PICTURE_TIME_BUDGET || now - pass_began > MENU_PICTURES_TIME_BUDGET;
}

static char const *const menu_picture_result_names[] = { "none", "rendered", "failed", "modded" };

/* a note's text read: FALSE if it is not one of this version's */
int menu_picture_note_parse(
	char const *text,
	struct menu_picture_note *note)
{
	long version = 0;
	int have = 0;

	memset(note, 0, sizeof(*note));
	while (text && *text)
	{
		char word[16];
		char value[32];
		char const *end = strchr(text, '\n');

		if (sscanf(text, "%15s %31s", word, value) == 2)
		{
			if (!strcmp(word, "version"))
				version = strtol(value, NULL, 10);
			else if (!strcmp(word, "pose"))
				note->pose_hash = strtoul(value, NULL, 16), have |= 1;
			else if (!strcmp(word, "map"))
				note->map_checksum = strtoul(value, NULL, 16), have |= 2;
			else if (!strcmp(word, "scale"))
				note->scale = strtol(value, NULL, 10);
			else if (!strcmp(word, "failures"))
				note->failures = strtol(value, NULL, 10);
			else if (!strcmp(word, "result"))
			{
				int result;

				for (result = 0; result < (int)(sizeof(menu_picture_result_names) / sizeof(menu_picture_result_names[0]));
					result++)
				{
					if (!strcmp(value, menu_picture_result_names[result]))
					{
						note->result = (enum menu_picture_result)result;
						have |= 4;
					}
				}
			}
		}
		text = end ? end + 1 : NULL;
	}
	if (version != MENU_PICTURES_VERSION || have != 7)
	{
		memset(note, 0, sizeof(*note));
		return 0;
	}
	return 1;
}

/* a note's text: FALSE if it does not fit */
int menu_picture_note_format(
	struct menu_picture_note const *note,
	char *text,
	long size)
{
	int length = snprintf(text, (size_t)size, "version %d\nresult %s\npose %08lx\nmap %08lx\nscale %ld\nfailures %ld\n",
		MENU_PICTURES_VERSION, menu_picture_result_names[(unsigned)note->result < 4 ? note->result : 0],
		note->pose_hash, note->map_checksum, note->scale, note->failures);

	return length > 0 && length < size;
}

/* the value at a place in the order of the values a histogram counts */
static double picture_sorted_at(
	unsigned long const *histogram,
	unsigned long place)
{
	unsigned long seen = 0;
	int value;

	for (value = 0; value < 256; value++)
	{
		seen += histogram[value];
		if (seen > place)
			return value;
	}
	return 255.0;
}

/* a quantile, as numpy's (linear between the two values it falls between) */
static double picture_quantile(
	unsigned long const *histogram,
	unsigned long count,
	double quantile)
{
	double position = quantile * (double)(count - 1);
	unsigned long below = (unsigned long)position;
	double low = picture_sorted_at(histogram, below);
	double high = below + 1 < count ? picture_sorted_at(histogram, below + 1) : low;

	return low + (position - (double)below) * (high - low);
}

/* the colours of count RGBA pixels taken toward those of reference_count
RGBA pixels (the original picture): for each channel, a tone curve mapping
the pixels' quantiles to the reference's, smoothed and kept rising
(tools/preview_rerender/final.py's curve_match, which the poses were judged
with) */
void menu_picture_colour_curve(
	unsigned char *pixels,
	unsigned long count,
	unsigned char const *reference,
	unsigned long reference_count)
{
	enum { QUANTILES = 257, SMOOTH = 9 };
	int channel;

	if (!count || !reference_count)
		return;
	for (channel = 0; channel < 3; channel++)
	{
		unsigned long source_histogram[256], reference_histogram[256], index;
		double source_quantiles[QUANTILES], reference_quantiles[QUANTILES], smoothed[QUANTILES];
		unsigned char curve[256];
		long unique = 0, at;

		memset(source_histogram, 0, sizeof(source_histogram));
		memset(reference_histogram, 0, sizeof(reference_histogram));
		for (index = 0; index < count; index++)
			source_histogram[pixels[index * 4 + channel]]++;
		for (index = 0; index < reference_count; index++)
			reference_histogram[reference[index * 4 + channel]]++;
		/* (each distinct quantile of the pixels once, with the reference's
		at the first place it is reached) */
		for (at = 0; at < QUANTILES; at++)
		{
			double quantile = (double)at / (QUANTILES - 1);
			double source = picture_quantile(source_histogram, count, quantile);

			if (unique && source == source_quantiles[unique - 1])
				continue;
			source_quantiles[unique] = source;
			reference_quantiles[unique] = picture_quantile(reference_histogram, reference_count, quantile);
			unique++;
		}
		/* (a moving average of the reference's, its ends repeated; then
		never falling) */
		for (at = 0; at < unique; at++)
		{
			double sum = 0.0;
			long offset;

			for (offset = -SMOOTH / 2; offset <= SMOOTH / 2; offset++)
			{
				long from = at + offset;

				sum += reference_quantiles[from < 0 ? 0 : from >= unique ? unique - 1 : from];
			}
			smoothed[at] = sum / SMOOTH;
			if (at && smoothed[at] < smoothed[at - 1])
				smoothed[at] = smoothed[at - 1];
		}
		for (index = 0; index < 256; index++)
		{
			double value;

			if (unique == 1 || index <= source_quantiles[0])
			{
				value = smoothed[0];
			}
			else if (index >= source_quantiles[unique - 1])
			{
				value = smoothed[unique - 1];
			}
			else
			{
				for (at = 1; source_quantiles[at] < index; at++)
					;
				value = smoothed[at - 1] + (smoothed[at] - smoothed[at - 1]) *
					((double)index - source_quantiles[at - 1]) / (source_quantiles[at] - source_quantiles[at - 1]);
			}
			curve[index] = (unsigned char)(value < 0.0 ? 0 : value > 255.0 ? 255 : value);
		}
		for (index = 0; index < count; index++)
			pixels[index * 4 + channel] = curve[pixels[index * 4 + channel]];
	}
}

/* the middle of a BGRA frame (rows from the top) cut to the shape aspect
(width over height), averaged down (or up) to width x height RGBA pixels;
FALSE when out of memory */
int menu_picture_cut(
	unsigned char const *bgra,
	unsigned long frame_width,
	unsigned long frame_height,
	float aspect,
	unsigned char *rgba,
	unsigned long width,
	unsigned long height)
{
	unsigned long left = 0, top = 0, cut_width = frame_width, cut_height = frame_height;
	unsigned long x, y;
	float *rows;

	if ((double)frame_width / frame_height > aspect)
	{
		cut_width = (unsigned long)((double)frame_height * aspect + 0.5);
		left = (frame_width - cut_width) / 2;
	}
	else
	{
		cut_height = (unsigned long)((double)frame_width / aspect + 0.5);
		top = (frame_height - cut_height) / 2;
	}
	if (!cut_width || !cut_height || !width || !height)
		return 0;
	/* (across, each output column a box of the cut's columns, partly
	covered at its ends; then down the same way) */
	rows = malloc(cut_height * width * 3 * sizeof(float));
	if (!rows)
		return 0;
	for (y = 0; y < cut_height; y++)
	{
		unsigned char const *row = bgra + ((top + y) * frame_width + left) * 4;

		for (x = 0; x < width; x++)
		{
			double start = (double)x * cut_width / width, end = (double)(x + 1) * cut_width / width;
			double sum[3] = { 0.0, 0.0, 0.0 }, weight = 0.0;
			unsigned long column;

			for (column = (unsigned long)start; column < cut_width && column < end; column++)
			{
				double from = column < start ? start : column, to = column + 1 > end ? end : column + 1;
				double part = to - from;

				sum[0] += row[column * 4 + 2] * part;
				sum[1] += row[column * 4 + 1] * part;
				sum[2] += row[column * 4 + 0] * part;
				weight += part;
			}
			rows[(y * width + x) * 3 + 0] = (float)(sum[0] / weight);
			rows[(y * width + x) * 3 + 1] = (float)(sum[1] / weight);
			rows[(y * width + x) * 3 + 2] = (float)(sum[2] / weight);
		}
	}
	for (y = 0; y < height; y++)
	{
		double start = (double)y * cut_height / height, end = (double)(y + 1) * cut_height / height;

		for (x = 0; x < width; x++)
		{
			double sum[3] = { 0.0, 0.0, 0.0 }, weight = 0.0;
			unsigned long line;
			int channel;

			for (line = (unsigned long)start; line < cut_height && line < end; line++)
			{
				double from = line < start ? start : line, to = line + 1 > end ? end : line + 1;
				double part = to - from;

				for (channel = 0; channel < 3; channel++)
					sum[channel] += rows[(line * width + x) * 3 + channel] * part;
				weight += part;
			}
			for (channel = 0; channel < 3; channel++)
				rgba[(y * width + x) * 4 + channel] = (unsigned char)(sum[channel] / weight + 0.5);
			rgba[(y * width + x) * 4 + 3] = 255;
		}
	}
	free(rows);
	return 1;
}

/* a picture (width x height RGBA) laid out as the original's frame is, at
its scale: in the top left, its last column and row repeated to the
frame's edges (what a filter reads past the picture is the picture's) */
void menu_picture_frame(
	unsigned char const *picture,
	unsigned long width,
	unsigned long height,
	unsigned char *frame,
	unsigned long frame_width,
	unsigned long frame_height)
{
	unsigned long x, y;

	for (y = 0; y < frame_height; y++)
	{
		unsigned char const *row = picture + (y < height ? y : height - 1) * width * 4;

		for (x = 0; x < frame_width; x++)
			memcpy(frame + (y * frame_width + x) * 4, row + (x < width ? x : width - 1) * 4, 4);
	}
}

/* ---------- files */

static struct
{
	pthread_mutex_t lock;
	/* the main menu first shown: the poses looked at (once a run) */
	int started;
	char folder[1024];
	/* each pose's picture, as the main menu found it */
	struct menu_picture_state
	{
		int checked;
		unsigned long map_checksum;
		unsigned long decision;
		struct menu_picture_note note;
		int noted;
		/* a render of it begun, and its note before it */
		int rendering;
		struct menu_picture_note note_before;
		int noted_before;
		/* the original picture's pixels (RGBA, 140x114), for its colours */
		unsigned char *original;
		/* its pixels decoded on the loader thread, or kept from its render
		(the lock's), and the image made from them */
		int loading;
		unsigned char *pixels;
		unsigned long width, height;
		long pixels_scale;
		unsigned int image;
		unsigned long levels;
		long image_scale;
	} pictures[MAXIMUM_MENU_PICTURES];
	long wanted_scale;
} menu_pictures = { PTHREAD_MUTEX_INITIALIZER };

/* the cache's folder: the data folder's menu_pictures */
static char const *picture_folder(void)
{
	if (!menu_pictures.folder[0])
		snprintf(menu_pictures.folder, sizeof(menu_pictures.folder), "%s/menu_pictures", platform_data_root());
	return menu_pictures.folder;
}

static void picture_path(char *path, size_t size, long pose, char const *extension)
{
	snprintf(path, size, "%s/%s.%s", picture_folder(), menu_picture_poses[pose].name, extension);
}

static long picture_count(void)
{
	return menu_picture_pose_count < MAXIMUM_MENU_PICTURES ? (long)menu_picture_pose_count : MAXIMUM_MENU_PICTURES;
}

/* a picture's pixels (malloc's, now the picture's; NULL: none) for the
game to make its image of */
static void picture_pixels_set(long pose, unsigned char *pixels, unsigned long width, unsigned long height,
	long scale)
{
	struct menu_picture_state *state = &menu_pictures.pictures[pose];

	pthread_mutex_lock(&menu_pictures.lock);
	/* (a smaller one decoded after a larger was rendered: let go) */
	if (pixels && (scale < state->pixels_scale || scale < state->image_scale))
	{
		state->loading = 0;
		pthread_mutex_unlock(&menu_pictures.lock);
		free(pixels);
		return;
	}
	free(state->pixels);
	state->pixels = pixels;
	state->width = width;
	state->height = height;
	state->pixels_scale = scale;
	state->loading = 0;
	pthread_mutex_unlock(&menu_pictures.lock);
}

/* a whole small file, NUL-ended (malloc's), or NULL */
static char *picture_read_file(char const *path, unsigned long maximum, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || (unsigned long)length > maximum ||
		fseek(file, 0, SEEK_SET) != 0 || !(data = malloc((size_t)length + 1)))
	{
		fclose(file);
		return NULL;
	}
	if (fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		fclose(file);
		free(data);
		return NULL;
	}
	fclose(file);
	data[length] = 0;
	if (size)
		*size = (unsigned long)length;
	return data;
}

/* a file written whole or not at all (a .new beside it, renamed over it) */
static int picture_write_file(char const *path, void const *data, unsigned long size)
{
	char temporary[1100];
	FILE *file;
	int written;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	written = fwrite(data, 1, size, file) == size;
	written = fclose(file) == 0 && written;
	/* (Windows renames over nothing) */
	if (written)
	{
		remove(path);
		written = rename(temporary, path) == 0;
	}
	if (!written)
		remove(temporary);
	return written;
}

static int picture_note_read(long pose, struct menu_picture_note *note)
{
	char path[1100];
	char *text;
	int read;

	picture_path(path, sizeof(path), pose, "txt");
	text = picture_read_file(path, 4096, NULL);
	read = text && menu_picture_note_parse(text, note);
	free(text);
	return read;
}

static int picture_note_write(long pose, struct menu_picture_note const *note)
{
	char path[1100], text[256];

	picture_path(path, sizeof(path), pose, "txt");
	return menu_picture_note_format(note, text, sizeof(text)) && picture_write_file(path, text, strlen(text));
}

/* the checksum of the player's map (its cache file header's, as saved
games check it), 0 if there is none: the map the game itself would load,
found where and as it finds it (menu_picture_map_checksum) */
static unsigned long picture_map_checksum(long pose)
{
	return menu_picture_map_checksum(menu_picture_poses[pose].map);
}

/* ---------- PNGs (8-bit RGBA, which hud_hires.c reads) */

static unsigned long picture_crc(unsigned long crc, unsigned char const *bytes, unsigned long size)
{
	static unsigned long table[256];
	unsigned long index;

	if (!table[1])
	{
		for (index = 0; index < 256; index++)
		{
			unsigned long value = index;
			int bit;

			for (bit = 0; bit < 8; bit++)
				value = value & 1 ? 0xEDB88320UL ^ (value >> 1) : value >> 1;
			table[index] = value;
		}
	}
	crc ^= 0xFFFFFFFFUL;
	for (index = 0; index < size; index++)
		crc = table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFUL;
}

static unsigned char *picture_chunk(unsigned char *out, char const *type, unsigned char const *data, unsigned long size)
{
	unsigned long crc;

	out[0] = (unsigned char)(size >> 24);
	out[1] = (unsigned char)(size >> 16);
	out[2] = (unsigned char)(size >> 8);
	out[3] = (unsigned char)size;
	memcpy(out + 4, type, 4);
	if (size)
		memmove(out + 8, data, size);
	crc = picture_crc(0, out + 4, size + 4);
	out[8 + size] = (unsigned char)(crc >> 24);
	out[9 + size] = (unsigned char)(crc >> 16);
	out[10 + size] = (unsigned char)(crc >> 8);
	out[11 + size] = (unsigned char)crc;
	return out + 12 + size;
}

static int picture_png_write(char const *path, unsigned char const *rgba, unsigned long width, unsigned long height)
{
	unsigned long stride = width * 4, raw_size = height * (stride + 1);
	unsigned long packed_size = raw_size + raw_size / 1000 + 64;
	unsigned char *raw = malloc(raw_size), *png = malloc(packed_size + 64), *out;
	unsigned char header[13];
	unsigned long row;
	int written = 0;

	if (raw && png)
	{
		for (row = 0; row < height; row++)
		{
			raw[row * (stride + 1)] = 0;
			memcpy(raw + row * (stride + 1) + 1, rgba + row * stride, stride);
		}
		if (compress2(png + 8 + 25 + 8, &packed_size, raw, raw_size, 6) == 0)
		{
			memcpy(png, "\x89PNG\r\n\x1a\n", 8);
			header[0] = (unsigned char)(width >> 24);
			header[1] = (unsigned char)(width >> 16);
			header[2] = (unsigned char)(width >> 8);
			header[3] = (unsigned char)width;
			header[4] = (unsigned char)(height >> 24);
			header[5] = (unsigned char)(height >> 16);
			header[6] = (unsigned char)(height >> 8);
			header[7] = (unsigned char)height;
			header[8] = 8; /* (bits) */
			header[9] = 6; /* (RGBA) */
			header[10] = header[11] = header[12] = 0;
			out = picture_chunk(png + 8, "IHDR", header, sizeof(header));
			/* (the compressed data is where its chunk's data goes) */
			out = picture_chunk(out, "IDAT", out + 8, packed_size);
			out = picture_chunk(out, "IEND", NULL, 0);
			written = picture_write_file(path, png, (unsigned long)(out - png));
		}
	}
	free(raw);
	free(png);
	return written;
}

/* ---------- the pictures found, rendered, loaded and drawn */

static int picture_enabled(void)
{
	return menu_picture_pose_count > 0 && config_boolean("display.menu_pictures") &&
		!config_boolean("debug.null_renderer") && !halo_server_mode();
}

/* runs nobody watches (tests), and those whose first screen is not the
main menu's: nothing rendered */
static int picture_quiet_run(void)
{
	if (config_boolean("debug.menu_pictures"))
		return 0;
	return config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		config_string("debug.network_test")[0] || config_string("debug.test_input")[0] ||
		config_boolean("debug.fixed_step") || config_string("debug.menu_open")[0]
#ifdef HALO_VR
		|| halo_vr_running()
#endif
		;
}

/* the loader thread: a picture's PNG decoded, and kept for the game to make
its image of */
static void *picture_load_thread(void *context)
{
	long pose = (long)(size_t)context;
	struct menu_picture_state *state = &menu_pictures.pictures[pose];
	char path[1100];
	unsigned long size = 0, width = 0, height = 0;
	char *png;
	unsigned char *pixels = NULL;
	long scale;

	pthread_mutex_lock(&menu_pictures.lock);
	scale = state->note.scale;
	pthread_mutex_unlock(&menu_pictures.lock);
	picture_path(path, sizeof(path), pose, "png");
	png = picture_read_file(path, 64UL << 20, &size);
	if (png)
		pixels = hud_hires_png_pixels(png, size, &width, &height);
	free(png);
	if (pixels && (width != (unsigned long)(MENU_PICTURE_FRAME_WIDTH * scale) ||
		height != (unsigned long)(MENU_PICTURE_FRAME_HEIGHT * scale)))
	{
		free(pixels);
		pixels = NULL;
	}
	if (!pixels)
		platform_log("menu pictures: cannot read %s: the original is drawn", path);
	picture_pixels_set(pose, pixels, width, height, scale);
	return NULL;
}

static void picture_load(long pose)
{
	struct menu_picture_state *state = &menu_pictures.pictures[pose];
	pthread_t thread;
	pthread_attr_t attributes;

	pthread_mutex_lock(&menu_pictures.lock);
	if (state->loading || state->image_scale == state->note.scale || state->pixels_scale == state->note.scale)
	{
		pthread_mutex_unlock(&menu_pictures.lock);
		return;
	}
	state->loading = 1;
	pthread_mutex_unlock(&menu_pictures.lock);
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&thread, &attributes, picture_load_thread, (void *)(size_t)pose) != 0)
	{
		pthread_mutex_lock(&menu_pictures.lock);
		state->loading = 0;
		pthread_mutex_unlock(&menu_pictures.lock);
	}
	pthread_attr_destroy(&attributes);
}

/* a pose's picture found: the map's checksum, the note, and so whether it
is drawn, and rendered */
static void picture_check(long pose)
{
	struct menu_picture_state *state = &menu_pictures.pictures[pose];

	state->checked = 1;
	state->map_checksum = picture_map_checksum(pose);
	pthread_mutex_lock(&menu_pictures.lock);
	state->noted = picture_note_read(pose, &state->note);
	pthread_mutex_unlock(&menu_pictures.lock);
	state->decision = menu_picture_decide(&menu_picture_poses[pose], state->map_checksum,
		state->noted ? &state->note : NULL, menu_pictures.wanted_scale);
	if (state->decision & (1UL << _menu_picture_use_bit))
		picture_load(pose);
}

long menu_pictures_wanted(
	long *poses,
	long maximum)
{
	unsigned long decisions[MAXIMUM_MENU_PICTURES];
	long pose;

	if (menu_pictures.started)
		return 0;
	menu_pictures.started = 1;
	if (!picture_enabled())
		return 0;
	mkdir(picture_folder(), 0755);
	menu_pictures.wanted_scale = menu_picture_scale_for_height((long)(480.0f * halo_screen_pixel_scale() + 0.5f));
	for (pose = 0; pose < picture_count(); pose++)
	{
		picture_check(pose);
		decisions[pose] = menu_pictures.pictures[pose].decision;
	}
	if (picture_quiet_run())
		return 0;
	return menu_picture_order(menu_picture_poses, picture_count(), decisions, poses, maximum);
}

long menu_pictures_scale(void)
{
	return menu_pictures.wanted_scale ? menu_pictures.wanted_scale : MENU_PICTURE_SMALLEST_SCALE;
}

void menu_pictures_keep_original(
	long pose,
	void const *texture)
{
	DWORD const *header = texture;
	struct xgpu_texture_description description;
	DWORD *pixels;
	unsigned char *original;
	unsigned long x, y;

	if (pose < 0 || pose >= picture_count() || !header || !header[1])
		return;
	xgpu_texture_describe(header[3], header[4], &description);
	if (description.width != MENU_PICTURE_FRAME_WIDTH || description.height != MENU_PICTURE_FRAME_HEIGHT ||
		description.cube_map || description.depth > 1)
	{
		platform_log("menu pictures: the original frame is %lux%lu, not %dx%d: its colours are not matched",
			description.width, description.height, MENU_PICTURE_FRAME_WIDTH, MENU_PICTURE_FRAME_HEIGHT);
		return;
	}
	pixels = malloc(description.width * description.height * 4);
	original = malloc(MENU_PICTURE_WIDTH * MENU_PICTURE_HEIGHT * 4);
	if (!pixels || !original || (!description.compressed &&
		!xgpu_texture_decode_level(&description, 0, PLATFORM_PHYSICAL_TO_VIRTUAL(header[1]), NULL, pixels)))
	{
		free(pixels);
		free(original);
		return;
	}
	if (description.compressed)
		xgpu_texture_decode_dxt(&description, 0, PLATFORM_PHYSICAL_TO_VIRTUAL(header[1]), pixels);
	/* (ARGB words: BGRA bytes) */
	for (y = 0; y < MENU_PICTURE_HEIGHT; y++)
	{
		for (x = 0; x < MENU_PICTURE_WIDTH; x++)
		{
			DWORD texel = pixels[y * description.width + x];
			unsigned char *out = original + (y * MENU_PICTURE_WIDTH + x) * 4;

			out[0] = (unsigned char)(texel >> 16);
			out[1] = (unsigned char)(texel >> 8);
			out[2] = (unsigned char)texel;
			out[3] = 255;
		}
	}
	free(pixels);
	free(menu_pictures.pictures[pose].original);
	menu_pictures.pictures[pose].original = original;
}

void menu_pictures_render_begin(
	long pose)
{
	struct menu_picture_state *state = &menu_pictures.pictures[pose];
	struct menu_picture_note note;

	/* (the note as it was, given back if the render is given up) */
	pthread_mutex_lock(&menu_pictures.lock);
	state->note_before = state->note;
	state->noted_before = state->noted;
	note = state->noted ? state->note : (struct menu_picture_note){ 0 };
	menu_picture_note_begin(&note, &menu_picture_poses[pose], state->map_checksum, menu_pictures.wanted_scale);
	pthread_mutex_unlock(&menu_pictures.lock);
	if (!picture_note_write(pose, &note))
		platform_log("menu pictures: cannot write the note of %s", menu_picture_poses[pose].name);
	state->rendering = 1;
}

void menu_pictures_render_failed(
	long pose,
	char const *why)
{
	struct menu_picture_state *state = &menu_pictures.pictures[pose];
	struct menu_picture_note note;

	if (!state->rendering)
		return;
	state->rendering = 0;
	note = state->noted_before ? state->note_before : (struct menu_picture_note){ 0 };
	menu_picture_note_begin(&note, &menu_picture_poses[pose], state->map_checksum, menu_pictures.wanted_scale);
	menu_picture_note_end(&note, _menu_picture_failed, menu_pictures.wanted_scale);
	picture_note_write(pose, &note);
	platform_log("menu pictures: %s %s (%ld time%s): the original is drawn", menu_picture_poses[pose].name, why,
		note.failures, note.failures == 1 ? "" : "s");
}

void menu_pictures_render_given_up(
	long pose,
	char const *why)
{
	struct menu_picture_state *state = &menu_pictures.pictures[pose];
	struct menu_picture_note note;

	if (pose < 0 || pose >= picture_count() || !state->rendering)
		return;
	state->rendering = 0;
	note = state->noted_before ? state->note_before : (struct menu_picture_note){ 0 };
	menu_picture_note_begin(&note, &menu_picture_poses[pose], state->map_checksum, menu_pictures.wanted_scale);
	menu_picture_note_end(&note, _menu_picture_failed, menu_pictures.wanted_scale);
	menu_picture_note_give_up(&note);
	picture_note_write(pose, &note);
	platform_log("menu pictures: %s %s: given up, the original is drawn", menu_picture_poses[pose].name, why);
}

int menu_pictures_render_kept(
	long pose)
{
	unsigned long width = 0, height = 0;
	unsigned char *bgra = d3d8_back_buffer_read(&width, &height);
	struct menu_picture_state *state = &menu_pictures.pictures[pose];
	struct menu_picture_pose const *picture = &menu_picture_poses[pose];
	long scale = menu_pictures.wanted_scale;
	unsigned long picture_width = (unsigned long)(MENU_PICTURE_WIDTH * scale);
	unsigned long picture_height = (unsigned long)(MENU_PICTURE_HEIGHT * scale);
	unsigned long frame_width = (unsigned long)(MENU_PICTURE_FRAME_WIDTH * scale);
	unsigned long frame_height = (unsigned long)(MENU_PICTURE_FRAME_HEIGHT * scale);
	unsigned char *cut = malloc(picture_width * picture_height * 4);
	unsigned char *frame = malloc(frame_width * frame_height * 4);
	struct menu_picture_note note;
	char path[1100];
	int written = 0;

	if (bgra && cut && frame && menu_picture_cut(bgra, width, height, picture->aspect, cut, picture_width, picture_height))
	{
		if (picture->colour_curve && state->original)
			menu_picture_colour_curve(cut, picture_width * picture_height, state->original,
				MENU_PICTURE_WIDTH * MENU_PICTURE_HEIGHT);
		menu_picture_frame(cut, picture_width, picture_height, frame, frame_width, frame_height);
		picture_path(path, sizeof(path), pose, "png");
		written = picture_png_write(path, frame, frame_width, frame_height);
		platform_log("menu pictures: %s, %lux%lu from a %lux%lu frame%s: %s", picture->name, picture_width,
			picture_height, width, height, picture->colour_curve ? (state->original ? ", colours matched" :
			", colours not matched (no original)") : "", written ? path : "not written");
	}
	free(cut);
	free(bgra);
	if (!written)
	{
		free(frame);
		menu_pictures_render_failed(pose, "was not written");
		return 0;
	}
	note = state->noted_before ? state->note_before : (struct menu_picture_note){ 0 };
	menu_picture_note_begin(&note, picture, state->map_checksum, scale);
	menu_picture_note_end(&note, _menu_picture_rendered, scale);
	if (!picture_note_write(pose, &note))
	{
		free(frame);
		platform_log("menu pictures: cannot write the note of %s", picture->name);
		return 0;
	}
	state->rendering = 0;
	/* (drawn at once: the preparing screen's, then the menus') */
	pthread_mutex_lock(&menu_pictures.lock);
	state->note = note;
	state->noted = 1;
	state->decision = 1UL << _menu_picture_use_bit;
	pthread_mutex_unlock(&menu_pictures.lock);
	picture_pixels_set(pose, frame, frame_width, frame_height, scale);
	return 1;
}

unsigned int menu_pictures_texture(
	long pose,
	unsigned long *levels)
{
	struct menu_picture_state *state;
	unsigned int image = 0;

	if (pose < 0 || pose >= picture_count())
		return 0;
	state = &menu_pictures.pictures[pose];
	pthread_mutex_lock(&menu_pictures.lock);
	if (state->pixels)
	{
		unsigned long largest;

		/* (a larger one, rendered since: the smaller let go) */
		if (state->image)
			xgpu_image_delete(state->image);
		state->levels = 1;
		for (largest = state->width > state->height ? state->width : state->height; largest > 1; largest >>= 1)
			state->levels++;
		state->image = xgpu_image_create(state->pixels, state->width, state->height, state->levels,
			XGPU_IMAGE_RGBA, TRUE);
		if (!state->image)
			free(state->pixels);
		state->image_scale = state->image ? state->pixels_scale : 0;
		state->pixels = NULL;
	}
	if (state->image)
	{
		image = state->image;
		*levels = state->levels;
	}
	pthread_mutex_unlock(&menu_pictures.lock);
	return image;
}
