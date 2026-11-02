/*
BINK_FFMPEG.H

A Bink movie decoded by FFmpeg's Bink demuxer and decoders
(port/third_party/ffmpeg): its frames as X8R8G8B8 pixels and its first audio
track as 16-bit PCM. bink_movie.c plays it through the Bink SDK's calls.
*/

#ifndef BINK_FFMPEG_H
#define BINK_FFMPEG_H

struct bink_ffmpeg;

struct bink_ffmpeg_information
{
	unsigned long width;
	unsigned long height;
	unsigned long frames;
	/* the frames a second, as a fraction */
	unsigned long rate_numerator;
	unsigned long rate_denominator;
	/* the audio's samples a second and channels (1 or 2), 0 for none */
	unsigned long audio_rate;
	unsigned long audio_channels;
};

/* opens a movie file (a path of the system's), NULL if it cannot be read or
is not a Bink movie of at most maximum_pixels (width times height) */
struct bink_ffmpeg *bink_ffmpeg_open(const char *path, unsigned long maximum_pixels,
	struct bink_ffmpeg_information *information);
void bink_ffmpeg_close(struct bink_ffmpeg *movie);
/* decodes the next frame and the audio before it: FALSE at the end, or
when the file can be read no further */
int bink_ffmpeg_decode_frame(struct bink_ffmpeg *movie);
/* the last frame decoded, its first width columns and height rows, to
destination (pitch bytes a row) */
void bink_ffmpeg_copy_frame(struct bink_ffmpeg *movie, void *destination, long pitch,
	unsigned long width, unsigned long height);
/* the audio decoded since it was last taken: interleaved 16-bit samples,
and how many frames of them; NULL for none */
const short *bink_ffmpeg_take_audio(struct bink_ffmpeg *movie, unsigned long *frames);

#endif
