/*
BINK_MOVIE.C

The Bink video SDK's entry points that bink_playback.c calls, over FFmpeg's
Bink decoders (bink_ffmpeg.c), when display.movies is on: the Xbox's movies
from the game data's bink folder (d:\bink\intro.bik, attract1.bik to
attract3.bik, credits.bik; attract_mode.c), which OpenH1 does not ship. With
the setting off (the default), or without the file, BinkOpen reports that the
movie cannot be opened and the game skips it, as it does a missing movie.

bink_playback.c draws each frame from a linear X8R8G8B8 texture over memory
it takes from the texture cache, BinkCopyToBuffer's destination, and paces
the frames by BinkWait. The SDK's memory (RADSetMemory) is not used: the
decoder has its own. The movie's audio plays as a 2D DirectSound stream of
16-bit PCM (dsound_sdl.c), a packet for each frame's audio as the frame is
decoded.

The prototypes match the declarations in bink_playback.c; the RAD SDK's
RADEXPLINK is __stdcall.
*/

#include "platform.h"
#include "port_config.h"
#include "sdl_platform.h"
#include "bink_ffmpeg.h"

#include <string.h>
#include <time.h>

typedef void *(__stdcall *rad_memory_allocate_proc)(unsigned long size);
typedef void (__stdcall *rad_memory_free_proc)(void *memory);
typedef void *(__stdcall *bink_sound_system_open_proc)(unsigned long param);

/* BinkOpen's flag for the larger I/O buffer, which the intro asks for with
the larger memory pool (bink_playback.c) */
#define BINKIOSIZE 0x01000000
/* the memory bink_playback.c takes for the frame (its
BINK_MEMORY_POOL_SIZE and BINK_MEMORY_POOL_SIZE_GODDAMN_BEAVER), less a
page and the frame's alignment */
#define BINK_POOL_SIZE (4 * 1024 * 1024)
#define BINK_LARGE_POOL_SIZE (16 * 1024 * 1024)
#define BINK_POOL_SLACK 8192
/* the texture's widths are whole multiples of 16 pixels (the pitch, in 64
bytes); its largest side */
#define BINK_WIDTH_MULTIPLE 16
#define BINK_MAXIMUM_SIDE 2048
/* how long BinkWait sleeps at most before it answers that the next frame is
not due (the game asks again at once: bink_playback_update__internal) */
#define BINK_WAIT_SLEEP_NS 2000000L

typedef struct BINK
{
	/* the SDK's fields bink_playback.c reads */
	unsigned long Width;
	unsigned long Height;
	unsigned long Frames;
	unsigned long FrameNum;
	unsigned long LastFrameNum;

	struct bink_ffmpeg *decoder;
	unsigned long rate_numerator;
	unsigned long rate_denominator;
	/* the frames decoded; when the first was due (CLOCK_MONOTONIC's ns), 0
	until it is */
	unsigned long decoded_frames;
	/* the file ended before Frames */
	BOOL ended;
	long long start_ns;
	LPDIRECTSOUNDSTREAM stream;
	WAVEFORMATEX wave_format;
} *HBINK;

/* the DirectSound the game hands BinkSetSoundSystem */
static LPDIRECTSOUND bink_direct_sound;

void __stdcall RADSetMemory(rad_memory_allocate_proc allocate, rad_memory_free_proc release)
{
	(void)allocate;
	(void)release;
}

void *__stdcall BinkOpenDirectSound(unsigned long param)
{
	return (void *)param;
}

long __stdcall BinkSetSoundSystem(bink_sound_system_open_proc open, unsigned long param)
{
	bink_direct_sound = open ? (LPDIRECTSOUND)open(param) : NULL;
	return bink_direct_sound != NULL;
}

void __stdcall BinkSetIOSize(unsigned long io_size)
{
	(void)io_size;
}

static long long monotonic_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}

static void bink_audio_open(HBINK bink, const struct bink_ffmpeg_information *information)
{
	DSSTREAMDESC description;

	if (!bink_direct_sound || !information->audio_rate)
		return;
	memset(&bink->wave_format, 0, sizeof(bink->wave_format));
	bink->wave_format.wFormatTag = WAVE_FORMAT_PCM;
	bink->wave_format.nChannels = (unsigned short)information->audio_channels;
	bink->wave_format.nSamplesPerSec = information->audio_rate;
	bink->wave_format.wBitsPerSample = 16;
	bink->wave_format.nBlockAlign = (unsigned short)(2 * information->audio_channels);
	bink->wave_format.nAvgBytesPerSec = information->audio_rate * bink->wave_format.nBlockAlign;
	memset(&description, 0, sizeof(description));
	description.dwMaxAttachedPackets = 64;
	description.lpwfxFormat = &bink->wave_format;
	if (IDirectSound_CreateSoundStream(bink_direct_sound, &description, &bink->stream, NULL) < 0)
		bink->stream = NULL;
}

/* the frame's audio, a packet of its own (dsound_sdl.c copies it) */
static void bink_audio_submit(HBINK bink)
{
	unsigned long frames;
	const short *samples = bink_ffmpeg_take_audio(bink->decoder, &frames);
	XMEDIAPACKET packet;

	if (!bink->stream || !samples)
		return;
	memset(&packet, 0, sizeof(packet));
	packet.pvBuffer = (void *)samples;
	packet.dwMaxSize = frames * bink->wave_format.nBlockAlign;
	IDirectSoundStream_Process(bink->stream, &packet, NULL);
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	char path[1024];
	struct bink_ffmpeg_information information;
	unsigned long pool = (flags & BINKIOSIZE) ? BINK_LARGE_POOL_SIZE : BINK_POOL_SIZE;
	HBINK bink;

	if (!name || !*name)
		return NULL;
	if (!config_boolean("display.movies"))
	{
		platform_log("Bink movies are off (display.movies); skipping \"%s\"", name);
		return NULL;
	}
	platform_translate_path(name, path, sizeof(path));
	bink = calloc(1, sizeof(*bink));
	if (!bink)
		return NULL;
	bink->decoder = bink_ffmpeg_open(path, (pool - BINK_POOL_SLACK) / 4, &information);
	if (!bink->decoder)
	{
		platform_log("Bink movie \"%s\" not played: no such file, or not one FFmpeg's Bink decoders read", path);
		free(bink);
		return NULL;
	}
	if (information.width < BINK_WIDTH_MULTIPLE || information.width > BINK_MAXIMUM_SIDE ||
		information.height > BINK_MAXIMUM_SIDE || information.frames < 2)
	{
		platform_log("Bink movie \"%s\" not played: %lux%lu", path, information.width, information.height);
		bink_ffmpeg_close(bink->decoder);
		free(bink);
		return NULL;
	}
	/* (the columns past the last whole 16 are not drawn) */
	bink->Width = information.width / BINK_WIDTH_MULTIPLE * BINK_WIDTH_MULTIPLE;
	bink->Height = information.height;
	bink->Frames = information.frames;
	bink->FrameNum = 1;
	bink->rate_numerator = information.rate_numerator;
	bink->rate_denominator = information.rate_denominator;
	bink_audio_open(bink, &information);
	/* (the presses before it skip nothing) */
	platform_movie_skip_pressed();
	platform_log("Bink movie \"%s\": %lux%lu, %lu frames at %lu/%lu a second, %s", path, information.width,
		information.height, information.frames, information.rate_numerator, information.rate_denominator,
		bink->stream ? "with sound" : "silent");
	return bink;
}

void __stdcall BinkClose(HBINK bink)
{
	if (!bink)
		return;
	platform_log("Bink movie closed at frame %lu of %lu", bink->decoded_frames, bink->Frames);
	if (bink->stream)
	{
		IDirectSoundStream_Flush(bink->stream);
		IDirectSoundStream_Release(bink->stream);
	}
	bink_ffmpeg_close(bink->decoder);
	free(bink);
}

/* decodes frame FrameNum (once) */
long __stdcall BinkDoFrame(HBINK bink)
{
	while (bink->decoded_frames < bink->FrameNum)
	{
		if (!bink_ffmpeg_decode_frame(bink->decoder))
		{
			bink->ended = TRUE;
			return 0;
		}
		bink->decoded_frames++;
		bink_audio_submit(bink);
	}
	return 0;
}

void __stdcall BinkNextFrame(HBINK bink)
{
	bink->LastFrameNum = bink->FrameNum;
	/* (the game stops at frame Frames - 1: bink_playback_update__internal;
	a file that ended early, there at once) */
	if (bink->ended)
		bink->FrameNum = bink->Frames - 1;
	else if (bink->FrameNum < bink->Frames)
		bink->FrameNum++;
}

/* nonzero while it is too early to show frame FrameNum */
long __stdcall BinkWait(HBINK bink)
{
	long long now = monotonic_ns(), due;

	if (!bink->start_ns)
		bink->start_ns = now;
	due = bink->start_ns + (long long)(bink->FrameNum - 1) * 1000000000LL *
		(long long)bink->rate_denominator / (long long)bink->rate_numerator;
	if (now >= due)
		return 0;
	{
		struct timespec sleep;

		sleep.tv_sec = 0;
		sleep.tv_nsec = due - now < BINK_WAIT_SLEEP_NS ? (long)(due - now) : BINK_WAIT_SLEEP_NS;
		nanosleep(&sleep, NULL);
	}
	return 1;
}

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	(void)flags;
	if (!destination || destination_x || destination_y)
		return 0;
	bink_ffmpeg_copy_frame(bink->decoder, destination, destination_pitch, bink->Width,
		destination_height < bink->Height ? destination_height : bink->Height);
	return 0;
}

/* (debug_bink's statistics: none kept) */
void __stdcall BinkGetSummary(HBINK bink, void *summary)
{
	(void)bink;
	memset(summary, 0, 0x7c);
}

void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count)
{
	unsigned long *fields = (unsigned long *)realtime;

	(void)frame_count;
	memset(realtime, 0, 0x38);
	fields[0] = bink->FrameNum;
	fields[1] = bink->rate_numerator;
	fields[2] = bink->rate_denominator;
	/* (Frames: the game divides by it) */
	fields[3] = 1;
}
