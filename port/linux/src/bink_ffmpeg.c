/*
BINK_FFMPEG.C

The Xbox's Bink movies decoded by FFmpeg (port/third_party/ffmpeg: its Bink
demuxer and its bink, binkaudio_rdft and binkaudio_dct decoders, nothing
else), for bink_movie.c, which plays them as the Bink SDK did.

A movie is the player's own file, untrusted: FFmpeg reads it through its
checked bitstream reader (CONFIG_SAFE_BITSTREAM_READER), from a file opened
here, with no protocol of its own. The demuxer refuses headers past its
limits; this file refuses a picture larger than the caller can hold, and keeps
at most a few seconds of audio.

The frames are YUV 4:2:0 (an alpha plane after them is not drawn), turned to
X8R8G8B8 with BT.601's coefficients, in the range the decoder reports (Bink
2's full, the older versions' limited). The audio is the first track, made
16-bit and at most stereo.

This file is compiled with FFmpeg's headers, not the platform layer's
(tools/android_build.py): its interface is plain C (bink_ffmpeg.h).
*/

#include "bink_ffmpeg.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/mem.h"

#include <stdio.h>
#include <string.h>

/* (port/linux/src/sdl_platform.c) */
void platform_log(const char *format, ...);

/* the file's reads, in FFmpeg's buffer */
#define BINK_FFMPEG_IO_SIZE 65536
/* the audio kept until it is taken: two seconds of 48 kHz stereo */
#define BINK_FFMPEG_AUDIO_SAMPLES (2 * 48000 * 2)

struct bink_ffmpeg
{
	FILE *file;
	AVFormatContext *format;
	AVIOContext *io;
	AVCodecContext *video;
	AVCodecContext *audio;
	int video_index;
	int audio_index;
	AVPacket *packet;
	AVFrame *frame;
	/* the decoders were told the file had ended */
	int flushed;
	/* the last frame, X8R8G8B8 */
	unsigned long width;
	unsigned long height;
	unsigned int *pixels;
	/* the audio not yet taken */
	short *audio_samples;
	unsigned long audio_count;
	unsigned long audio_channels;
};

static int file_read(void *opaque, unsigned char *buffer, int size)
{
	size_t count = fread(buffer, 1, (size_t)size, (FILE *)opaque);

	return count ? (int)count : AVERROR_EOF;
}

static int64_t file_seek(void *opaque, int64_t offset, int whence)
{
	FILE *file = (FILE *)opaque;

	if (whence & AVSEEK_SIZE)
	{
		long here = ftell(file), size;

		if (here < 0 || fseek(file, 0, SEEK_END))
			return -1;
		size = ftell(file);
		fseek(file, here, SEEK_SET);
		return size;
	}
	whence &= ~AVSEEK_FORCE;
	if (offset != (long)offset || fseek(file, (long)offset, whence))
		return -1;
	return ftell(file);
}

static void log_callback(void *context, int level, const char *format, va_list arguments)
{
	char line[256];
	size_t length;

	(void)context;
	if (level > AV_LOG_ERROR)
		return;
	vsnprintf(line, sizeof(line), format, arguments);
	length = strlen(line);
	if (length && line[length - 1] == '\n')
		line[length - 1] = '\0';
	platform_log("bink: %s", line);
}

static AVCodecContext *decoder_open(const AVStream *stream)
{
	const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
	AVCodecContext *context;

	if (!codec)
		return NULL;
	context = avcodec_alloc_context3(codec);
	if (!context)
		return NULL;
	if (avcodec_parameters_to_context(context, stream->codecpar) < 0 || avcodec_open2(context, codec, NULL) < 0)
		avcodec_free_context(&context);
	return context;
}

struct bink_ffmpeg *bink_ffmpeg_open(const char *path, unsigned long maximum_pixels,
	struct bink_ffmpeg_information *information)
{
	struct bink_ffmpeg *movie;
	const AVInputFormat *bink = av_find_input_format("bink");
	unsigned char *buffer;
	unsigned int index;

	av_log_set_callback(log_callback);
	movie = av_mallocz(sizeof(*movie));
	if (!movie)
		return NULL;
	movie->video_index = -1;
	movie->audio_index = -1;
	movie->file = fopen(path, "rb");
	buffer = av_malloc(BINK_FFMPEG_IO_SIZE);
	if (movie->file && buffer)
		movie->io = avio_alloc_context(buffer, BINK_FFMPEG_IO_SIZE, 0, movie->file, file_read, NULL, file_seek);
	if (!movie->io)
	{
		av_free(buffer);
		bink_ffmpeg_close(movie);
		return NULL;
	}
	movie->format = avformat_alloc_context();
	if (!movie->format)
	{
		bink_ffmpeg_close(movie);
		return NULL;
	}
	movie->format->pb = movie->io;
	movie->format->flags |= AVFMT_FLAG_CUSTOM_IO;
	if (avformat_open_input(&movie->format, NULL, bink, NULL) < 0)
	{
		/* (avformat_open_input freed the context) */
		movie->format = NULL;
		bink_ffmpeg_close(movie);
		return NULL;
	}
	for (index = 0; index < movie->format->nb_streams; index++)
	{
		const AVStream *stream = movie->format->streams[index];

		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && movie->video_index < 0)
			movie->video_index = (int)index;
		else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && movie->audio_index < 0)
			movie->audio_index = (int)index;
	}
	if (movie->video_index < 0)
	{
		bink_ffmpeg_close(movie);
		return NULL;
	}
	{
		const AVStream *stream = movie->format->streams[movie->video_index];
		const AVCodecParameters *parameters = stream->codecpar;

		if (parameters->width <= 0 || parameters->height <= 0 ||
			(unsigned long)parameters->width > maximum_pixels / (unsigned long)parameters->height ||
			stream->duration <= 0 || stream->time_base.num <= 0 || stream->time_base.den <= 0)
		{
			platform_log("bink: \"%s\" is %dx%d, %lld frames: not played", path, parameters->width,
				parameters->height, (long long)stream->duration);
			bink_ffmpeg_close(movie);
			return NULL;
		}
		movie->width = (unsigned long)parameters->width;
		movie->height = (unsigned long)parameters->height;
		information->width = movie->width;
		information->height = movie->height;
		information->frames = (unsigned long)stream->duration;
		/* (the stream's time base is a frame's time) */
		information->rate_numerator = (unsigned long)stream->time_base.den;
		information->rate_denominator = (unsigned long)stream->time_base.num;
	}
	movie->video = decoder_open(movie->format->streams[movie->video_index]);
	movie->pixels = av_calloc(movie->width * movie->height, sizeof(*movie->pixels));
	movie->packet = av_packet_alloc();
	movie->frame = av_frame_alloc();
	if (!movie->video || !movie->pixels || !movie->packet || !movie->frame)
	{
		bink_ffmpeg_close(movie);
		return NULL;
	}
	information->audio_rate = 0;
	information->audio_channels = 0;
	if (movie->audio_index >= 0)
	{
		movie->audio = decoder_open(movie->format->streams[movie->audio_index]);
		movie->audio_samples = av_malloc(BINK_FFMPEG_AUDIO_SAMPLES * sizeof(*movie->audio_samples));
		if (movie->audio && movie->audio_samples && movie->audio->sample_rate > 0 &&
			movie->audio->ch_layout.nb_channels > 0 &&
			(movie->audio->sample_fmt == AV_SAMPLE_FMT_FLT || movie->audio->sample_fmt == AV_SAMPLE_FMT_FLTP))
		{
			movie->audio_channels = movie->audio->ch_layout.nb_channels > 1 ? 2 : 1;
			information->audio_rate = (unsigned long)movie->audio->sample_rate;
			information->audio_channels = movie->audio_channels;
		}
		else
		{
			avcodec_free_context(&movie->audio);
			movie->audio_index = -1;
		}
	}
	return movie;
}

void bink_ffmpeg_close(struct bink_ffmpeg *movie)
{
	if (!movie)
		return;
	avcodec_free_context(&movie->video);
	avcodec_free_context(&movie->audio);
	av_packet_free(&movie->packet);
	av_frame_free(&movie->frame);
	if (movie->format)
		avformat_close_input(&movie->format);
	if (movie->io)
	{
		av_freep(&movie->io->buffer);
		avio_context_free(&movie->io);
	}
	if (movie->file)
		fclose(movie->file);
	av_free(movie->pixels);
	av_free(movie->audio_samples);
	av_free(movie);
}

static unsigned char clamp_byte(int value)
{
	return value < 0 ? 0 : value > 255 ? 255 : (unsigned char)value;
}

static void frame_convert(struct bink_ffmpeg *movie, const AVFrame *frame)
{
	unsigned long width = (unsigned long)frame->width < movie->width ? (unsigned long)frame->width : movie->width;
	unsigned long height = (unsigned long)frame->height < movie->height ? (unsigned long)frame->height : movie->height;
	int full_range = frame->color_range == AVCOL_RANGE_JPEG;
	unsigned long row, column;

	if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVA420P)
		return;
	for (row = 0; row < height; row++)
	{
		const unsigned char *y = frame->data[0] + row * (size_t)frame->linesize[0];
		const unsigned char *u = frame->data[1] + (row / 2) * (size_t)frame->linesize[1];
		const unsigned char *v = frame->data[2] + (row / 2) * (size_t)frame->linesize[2];
		unsigned int *pixel = movie->pixels + row * movie->width;

		for (column = 0; column < width; column++)
		{
			int d = u[column / 2] - 128, e = v[column / 2] - 128;
			int luma, red, green, blue;

			if (full_range)
			{
				luma = 256 * y[column];
				red = (luma + 359 * e + 128) >> 8;
				green = (luma - 88 * d - 183 * e + 128) >> 8;
				blue = (luma + 454 * d + 128) >> 8;
			}
			else
			{
				luma = 298 * (y[column] - 16);
				red = (luma + 409 * e + 128) >> 8;
				green = (luma - 100 * d - 208 * e + 128) >> 8;
				blue = (luma + 516 * d + 128) >> 8;
			}
			pixel[column] = 0xff000000u | ((unsigned int)clamp_byte(red) << 16) |
				((unsigned int)clamp_byte(green) << 8) | clamp_byte(blue);
		}
	}
}

static float sample_at(const AVFrame *frame, int planar, int channels, int index, int channel)
{
	return planar ? ((const float *)frame->extended_data[channel])[index] :
		((const float *)frame->extended_data[0])[index * channels + channel];
}

static void audio_append(struct bink_ffmpeg *movie, const AVFrame *frame)
{
	int planar = frame->format == AV_SAMPLE_FMT_FLTP;
	int channels = frame->ch_layout.nb_channels;
	int index;
	unsigned long channel;

	if (channels <= 0 || (frame->format != AV_SAMPLE_FMT_FLT && !planar))
		return;
	for (index = 0; index < frame->nb_samples; index++)
	{
		if (movie->audio_count + movie->audio_channels > BINK_FFMPEG_AUDIO_SAMPLES)
			return;
		for (channel = 0; channel < movie->audio_channels; channel++)
		{
			float value = sample_at(frame, planar, channels, index, (int)channel < channels ? (int)channel : 0);
			int scaled = (int)(value * 32767.0f);

			movie->audio_samples[movie->audio_count++] =
				(short)(scaled < -32768 ? -32768 : scaled > 32767 ? 32767 : scaled);
		}
	}
}

static void audio_decode(struct bink_ffmpeg *movie, const AVPacket *packet)
{
	if (avcodec_send_packet(movie->audio, packet) < 0)
		return;
	while (avcodec_receive_frame(movie->audio, movie->frame) == 0)
	{
		audio_append(movie, movie->frame);
		av_frame_unref(movie->frame);
	}
}

int bink_ffmpeg_decode_frame(struct bink_ffmpeg *movie)
{
	for (;;)
	{
		int result = avcodec_receive_frame(movie->video, movie->frame);

		if (result == 0)
		{
			frame_convert(movie, movie->frame);
			av_frame_unref(movie->frame);
			return 1;
		}
		if (result != AVERROR(EAGAIN) || movie->flushed)
			return 0;
		if (av_read_frame(movie->format, movie->packet) < 0)
		{
			movie->flushed = 1;
			avcodec_send_packet(movie->video, NULL);
			continue;
		}
		if (movie->packet->stream_index == movie->video_index)
		{
			/* (a frame that does not decode is skipped) */
			avcodec_send_packet(movie->video, movie->packet);
		}
		else if (movie->packet->stream_index == movie->audio_index && movie->audio)
		{
			audio_decode(movie, movie->packet);
		}
		av_packet_unref(movie->packet);
	}
}

void bink_ffmpeg_copy_frame(struct bink_ffmpeg *movie, void *destination, long pitch,
	unsigned long width, unsigned long height)
{
	unsigned long row;

	if (width > movie->width)
		width = movie->width;
	if (height > movie->height)
		height = movie->height;
	for (row = 0; row < height; row++)
	{
		memcpy((unsigned char *)destination + (long)row * pitch, movie->pixels + row * movie->width,
			width * sizeof(*movie->pixels));
	}
}

const short *bink_ffmpeg_take_audio(struct bink_ffmpeg *movie, unsigned long *frames)
{
	if (!movie->audio_channels || !movie->audio_count)
	{
		*frames = 0;
		return NULL;
	}
	*frames = movie->audio_count / movie->audio_channels;
	movie->audio_count = 0;
	return movie->audio_samples;
}
