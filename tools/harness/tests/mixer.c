/*
MIXER.C (test)

The real mixer of port/linux/src/dsound_sdl.c (its resampler, voices,
reverb and limiter; test_mixer.py takes them as under_test.inc) on known
signals:
	- the resampler against the same Kaiser-windowed sinc worked out in
	  double precision, tap by tap: noise, sines and an impulse at the
	  game's rates (22 and 44 kHz voices, others pitched up and down, a 48
	  kHz one), so a faster resampler is still the same low pass;
	- sines through it measured as the ear would hear them: the gain across
	  the band, and what else comes out (images and aliases) under the tone;
	- a voice out of earshot, which is not resampled, moves on as one heard
	  does, and is heard the same once it comes into earshot;
	- mono voices heard as stereo ones with both channels the same are;
	- audio.resampler = "linear": each frame between the two around it;
	- Xbox ADPCM decoded as a decoder written apart decodes it (two blocks'
	  frames from it), and as the format reads, a frame at a time, for
	  blocks of every header;
	- the Doppler shift of a 3D voice coming, going, or with the listener
	  moving, and its limits;
	- the limiter: a mix up to full scale as it is, a sum over it held to it;
	- audio.channels' choice from the device's channels, and the 5.1 pan
	  (VBAP): each speaker's angle on its speaker alone, every angle where
	  the tangent law hears it, never the LFE, through the mixer too, and
	  2D voices by the game's AC-3 mix bins;
	- 3D audio: audio.spatial's choice of the HRTF or the pan; the HRTF's
	  cues (the time and the level between the ears, the notch moving with
	  the sound up and down, a sound behind duller) and the pan's (none of
	  above, below or behind); distance, occlusion, the room send and the
	  Doppler shift through the HRTF as panned; the crossfade as the choice
	  changes (no click); 2D voices as they were.
"render" writes voices at each rate and a scene's whole mix as raw floats
(test_mixer.py compares the resampler adding four taps at a time with it
adding tap by tap), "metrics" prints the figures, "bench" times a game's
worth of voices (through the HRTF too) and "circle" writes a sound going
round the listener to a WAV file, to listen to.
*/

#include "harness.h"

#include <math.h>
#include <pthread.h>
#include <stddef.h>

typedef int BOOL;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef void *LPVOID;
typedef void (*LPFNXMEDIAOBJECTCALLBACK)(void *, void *, DWORD);
typedef struct { void *lpVtbl; } IDirectSoundStream;
typedef struct { void *pvBuffer; DWORD dwMaxSize; } XMEDIAPACKET;
typedef struct
{
	LONG lRoom, lRoomHF;
	float flRoomRolloffFactor, flDecayTime, flDecayHFRatio;
	LONG lReflections;
	float flReflectionsDelay;
	LONG lReverb;
	float flReverbDelay, flDiffusion, flDensity, flHFReference;
} DSI3DL2LISTENER;
#define DSBVOLUME_MIN (-10000)
#define DS3DMODE_NORMAL 0x00000000
#define DS3DMODE_HEADRELATIVE 0x00000001
#define DS3DMODE_DISABLE 0x00000002

/* the settings and the log (debug.audio_record: none), and SDL's clock
(the mix's time, debug.audio_log) */
typedef unsigned long long Uint64;
#define platform_log(...) ((void)0)
#define SDL_GetTicksNS() 0ull

/* the settings (port_config.c): audio.resampler, which a case sets */
static const char *resampler_setting;

static const char *config_string(const char *name)
{
	return !strcmp(name, "audio.resampler") ? resampler_setting : "";
}

/* the players' voices (voice_audio.c): none */
static void voice_audio_mix(float *output, unsigned long frames)
{
	(void)output;
	(void)frames;
}

#include "under_test.inc"

#define PI 3.14159265358979323846
#define CHUNK 512 /* frames a mix, as SDL asks on Linux */

/* ---------- voices */

/* a voice's one packet, as the game submitted it and the mixer took it over
(dsound_sdl.c's hand-off: mix takes the game's side over first) */
static void voice_packet_taken(struct sdl_stream *stream)
{
	stream->packets[0] = stream->mixing[0];
	stream->packet_count = 1;
	stream->submitted = 1;
	stream->mixing_count = 1;
	stream->mixing_next = 1;
}

/* a voice of frames frames of channels channels at rate, played at frequency
(0: its rate), a 2D voice heard at full volume in both speakers */
static struct sdl_stream *voice_new(const short *samples, unsigned long frames, unsigned long channels,
	unsigned long rate, unsigned long frequency)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));

	stream->channels = channels;
	stream->sample_rate = rate;
	stream->frequency = frequency;
	stream->volume = 1.0f;
	stream->mix_left = stream->mix_right = 1.0f;
	stream->room = stream->room_hf = DSBVOLUME_MIN;
	stream->mixing[0].samples = malloc(frames * channels * sizeof(short));
	memcpy(stream->mixing[0].samples, samples, frames * channels * sizeof(short));
	stream->mixing[0].frames = frames;
	voice_packet_taken(stream);
	resampler_reset(stream);
	return stream;
}

/* a game thread holding the voices' lock for 300 ms, set aside by the
scheduler (hand-off-never-waits) */
static struct
{
	pthread_mutex_t lock;
	pthread_cond_t condition;
	BOOL holding;
} hand_off_test = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, FALSE };

static void *hand_off_holder(void *parameter)
{
	struct timespec pause = { 0, 300000000L };

	(void)parameter;
	pthread_mutex_lock(&voices_lock);
	pthread_mutex_lock(&hand_off_test.lock);
	hand_off_test.holding = TRUE;
	pthread_cond_signal(&hand_off_test.condition);
	pthread_mutex_unlock(&hand_off_test.lock);
	nanosleep(&pause, NULL);
	pthread_mutex_unlock(&voices_lock);
	return NULL;
}

static void voice_delete(struct sdl_stream *stream)
{
	free(stream->mixing[0].samples);
	free(stream);
}

/* frames of the voice mixed alone, CHUNK at a time: the left channel, or
both (stereo: interleaved) */
static void render(struct sdl_stream *stream, float *output, unsigned long frames, BOOL stereo)
{
	static float buffer[CHUNK * 2], send[CHUNK];
	unsigned long done, frame;

	for (done = 0; done < frames; done += CHUNK)
	{
		unsigned long count = frames - done < CHUNK ? frames - done : CHUNK;

		memset(buffer, 0, sizeof(buffer));
		memset(send, 0, sizeof(send));
		mix_voice(stream, buffer, send, count);
		for (frame = 0; frame < count; frame++)
		{
			if (stereo)
			{
				output[(done + frame) * 2] = buffer[frame * 2];
				output[(done + frame) * 2 + 1] = buffer[frame * 2 + 1];
			}
			else
			{
				output[done + frame] = buffer[frame * 2];
			}
		}
	}
}

/* ---------- the reference: the same low pass in double precision */

static double reference_kernel(double distance)
{
	double edge, angle, sinc;

	distance = fabs(distance);
	if (distance >= RESAMPLER_ZERO_CROSSINGS)
		return 0.0;
	edge = distance / RESAMPLER_ZERO_CROSSINGS;
	angle = PI * RESAMPLER_CUTOFF * distance;
	sinc = distance > 0.0 ? sin(angle) / angle : 1.0;
	return RESAMPLER_CUTOFF * sinc * bessel_i0(RESAMPLER_KAISER_BETA * sqrt(1.0 - edge * edge)) /
		bessel_i0(RESAMPLER_KAISER_BETA);
}

/* output frame n of source (frames, mono, full scale 1) resampled at step:
the frames around source time n step, weighted by the low pass (narrowed for a
step over 1, as the mixer's) */
static double reference_frame(const double *source, long frames, double step, unsigned long n)
{
	double scale = step > 1.0 ? 1.0 / (step < RESAMPLER_MAXIMUM_STRETCH ? step : RESAMPLER_MAXIMUM_STRETCH) : 1.0;
	double time = (double)n * step, sum = 0.0;
	long center = (long)floor(time), width = (long)ceil(RESAMPLER_ZERO_CROSSINGS / scale), k;

	for (k = center + 1 - width; k <= center + width; k++)
	{
		if (k >= 0 && k < frames)
			sum += source[k] * scale * reference_kernel((time - (double)k) * scale);
	}
	return sum;
}

/* ---------- signals */

static unsigned long random_state = 1;

static double random_uniform(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (double)((random_state >> 8) & 0xffffff) / 16777216.0 * 2.0 - 1.0;
}

/* frames of a signal as 16-bit samples (and as the doubles they are) */
static void quantize(const double *signal, short *samples, double *exact, unsigned long frames)
{
	unsigned long frame;

	for (frame = 0; frame < frames; frame++)
	{
		long value = lround(signal[frame] * 32767.0);

		samples[frame] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
		exact[frame] = samples[frame] / 32768.0;
	}
}

/* ---------- figures */

/* the amplitude of a sine of frequency cycles a frame in signal[first, last),
and the power of the rest relative to the sine's, in dB */
static void tone_fit(const float *signal, unsigned long first, unsigned long last, double frequency,
	double *amplitude, double *rest_db)
{
	double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, a, b, determinant, rest = 0, tone = 0;
	unsigned long n;

	for (n = first; n < last; n++)
	{
		double s = sin(2.0 * PI * frequency * n), c = cos(2.0 * PI * frequency * n);

		ss += s * s;
		sc += s * c;
		cc += c * c;
		ys += signal[n] * s;
		yc += signal[n] * c;
	}
	determinant = ss * cc - sc * sc;
	a = (ys * cc - yc * sc) / determinant;
	b = (yc * ss - ys * sc) / determinant;
	for (n = first; n < last; n++)
	{
		double fit = a * sin(2.0 * PI * frequency * n) + b * cos(2.0 * PI * frequency * n);

		rest += (signal[n] - fit) * (signal[n] - fit);
		tone += fit * fit;
	}
	*amplitude = sqrt(a * a + b * b);
	*rest_db = 10.0 * log10(rest / tone + 1e-30);
}

static double decibels(double ratio)
{
	return 20.0 * log10(ratio + 1e-30);
}

/* the mixer's output against the reference's for a signal at rate played at
frequency: the error's power relative to the reference's, in dB, and its
largest relative to the largest sample */
static void against_reference(const double *signal, unsigned long frames, unsigned long rate, unsigned long frequency,
	double *error_db, double *peak_error_db)
{
	short *samples = malloc(frames * sizeof(short));
	double *exact = malloc(frames * sizeof(double));
	double step = (double)(frequency ? frequency : rate) / OUTPUT_RATE, error = 0, power = 0, peak = 0, worst = 0;
	unsigned long outputs = (unsigned long)(frames / step), n;
	float *output = calloc(outputs, sizeof(float));
	struct sdl_stream *stream;

	quantize(signal, samples, exact, frames);
	stream = voice_new(samples, frames, 1, rate, frequency);
	render(stream, output, outputs, FALSE);
	for (n = 0; n < outputs; n++)
	{
		double expected = reference_frame(exact, (long)frames, step, n);
		double difference = output[n] - expected;

		error += difference * difference;
		power += expected * expected;
		peak = fabs(expected) > peak ? fabs(expected) : peak;
		worst = fabs(difference) > worst ? fabs(difference) : worst;
	}
	*error_db = 10.0 * log10(error / power + 1e-30);
	*peak_error_db = decibels(worst / peak);
	voice_delete(stream);
	free(output);
	free(exact);
	free(samples);
}

/* a sine of hertz at rate, played at frequency: its gain through the mixer
(dB) and the power of everything else that comes out, relative to it (dB); a
sine the voice plays above the output's band is all "else", relative to the
sine put in */
static void sine_figures(double hertz, unsigned long rate, unsigned long frequency, double *gain_db, double *rest_db)
{
	unsigned long frames = rate, outputs, n;
	double step = (double)(frequency ? frequency : rate) / OUTPUT_RATE, played = hertz * step * OUTPUT_RATE / rate;
	double *signal = malloc(frames * sizeof(double)), *exact = malloc(frames * sizeof(double)), amplitude;
	short *samples = malloc(frames * sizeof(short));
	float *output;
	struct sdl_stream *stream;

	for (n = 0; n < frames; n++)
		signal[n] = 0.5 * sin(2.0 * PI * hertz * n / rate);
	quantize(signal, samples, exact, frames);
	outputs = (unsigned long)(frames / step);
	output = calloc(outputs, sizeof(float));
	stream = voice_new(samples, frames, 1, rate, frequency);
	render(stream, output, outputs, FALSE);
	if (played < 0.5 * OUTPUT_RATE)
	{
		tone_fit(output, outputs / 4, outputs * 3 / 4, played / OUTPUT_RATE, &amplitude, rest_db);
		*gain_db = decibels(amplitude / 0.5);
	}
	else
	{
		double power = 0;

		for (n = outputs / 4; n < outputs * 3 / 4; n++)
			power += (double)output[n] * output[n];
		*gain_db = -999.0;
		*rest_db = 10.0 * log10(power / (outputs / 2) / (0.5 * 0.5 / 2) + 1e-30);
	}
	voice_delete(stream);
	free(output);
	free(samples);
	free(exact);
	free(signal);
}

/* a logarithmic sweep from 20 Hz to top at rate over the voice: what comes out
against the sweep itself at the output's times (an ideal resampler), in dB */
static double sweep_figure(unsigned long rate, double top)
{
	unsigned long frames = 2 * rate, outputs = (unsigned long)((double)frames * OUTPUT_RATE / rate), n;
	double *signal = malloc(frames * sizeof(double)), *exact = malloc(frames * sizeof(double));
	double duration = (double)frames / rate, k = log(top / 20.0), error = 0, power = 0;
	short *samples = malloc(frames * sizeof(short));
	float *output = calloc(outputs, sizeof(float));
	struct sdl_stream *stream;

	for (n = 0; n < frames; n++)
	{
		double t = (double)n / rate;

		signal[n] = 0.5 * sin(2.0 * PI * 20.0 * duration / k * (exp(t / duration * k) - 1.0));
	}
	quantize(signal, samples, exact, frames);
	stream = voice_new(samples, frames, 1, rate, 0);
	render(stream, output, outputs, FALSE);
	for (n = OUTPUT_RATE / 10; n + OUTPUT_RATE / 10 < outputs; n++)
	{
		double t = (double)n / OUTPUT_RATE;
		double ideal = 0.5 * sin(2.0 * PI * 20.0 * duration / k * (exp(t / duration * k) - 1.0));

		error += (output[n] - ideal) * (output[n] - ideal);
		power += ideal * ideal;
	}
	voice_delete(stream);
	free(output);
	free(samples);
	free(exact);
	free(signal);
	return 10.0 * log10(error / power + 1e-30);
}

static double *noise(unsigned long frames)
{
	double *signal = malloc(frames * sizeof(double));
	unsigned long n;

	random_state = 1;
	for (n = 0; n < frames; n++)
		signal[n] = 0.5 * random_uniform();
	return signal;
}

/* ---------- a scene: a game's worth of voices, for "render" and "bench" */

/* voices: mono 22 kHz sounds in the world (indoors sending to the reverb,
half muffled, silent ones out of earshot), stereo 44 kHz music, a few
pitched; each plays for seconds */
static void scene(unsigned long voices, unsigned long silent, BOOL indoor, double seconds)
{
	unsigned long frames = 22050 * 4, v, n;
	short *mono = malloc(frames * sizeof(short)), *stereo = malloc(frames * 2 * sizeof(short));

	random_state = 7;
	for (n = 0; n < frames; n++)
	{
		mono[n] = (short)(8000 * random_uniform());
		stereo[n * 2] = (short)(8000 * random_uniform());
		stereo[n * 2 + 1] = (short)(8000 * random_uniform());
	}
	if (indoor)
	{
		DSI3DL2LISTENER room = { -1000, -100, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f };

		environment = room;
		environment_serial++;
	}
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();
	for (v = 0; v < voices + silent; v++)
	{
		struct sdl_stream *stream;

		if (v == 0)
			stream = voice_new(stereo, frames, 2, 44100, 0);
		else
			stream = voice_new(mono, frames, 1, 22050, v % 5 == 0 ? 22050 + 2000 * (v % 3) : 0);
		if (v)
		{
			stream->has_3d = TRUE;
			stream->mode = DS3DMODE_NORMAL;
			stream->position[0] = (float)(v % 7) - 3.0f;
			stream->position[2] = v < voices ? 2.0f : 1000.0f;
			stream->minimum_distance = 1.0f;
			stream->maximum_distance = v < voices ? 50.0f : 100.0f;
			stream->direct = v < voices ? 0 : DSBVOLUME_MIN;
			stream->direct_hf = v % 2 ? -500 : 0;
			stream->room = v < voices ? 0 : DSBVOLUME_MIN;
			stream->room_hf = 0;
		}
		/* a packet as long as the scene */
		free(stream->mixing[0].samples);
		stream->mixing[0].frames = (unsigned long)(seconds * 50000);
		stream->mixing[0].samples = malloc(stream->mixing[0].frames * stream->channels * sizeof(short));
		for (n = 0; n < stream->mixing[0].frames * stream->channels; n++)
			stream->mixing[0].samples[n] = (v == 0 ? stereo : mono)[n % frames];
		voice_packet_taken(stream);
		stream->next = streams;
		streams = stream;
	}
	free(mono);
	free(stereo);
}

/* the scene's voices out of earshot come into it */
static void scene_approach(void)
{
	struct sdl_stream *stream;

	for (stream = streams; stream; stream = stream->next)
	{
		if (stream->direct == DSBVOLUME_MIN)
		{
			stream->position[2] = 3.0f;
			stream->direct = stream->room = 0;
		}
	}
}

static void bench(unsigned long voices, unsigned long silent, BOOL indoor, double seconds)
{
	unsigned long chunks = (unsigned long)(seconds * OUTPUT_RATE / CHUNK), chunk;
	static float output[CHUNK * 2];
	struct timespec start, end;
	double elapsed;

	scene(voices, silent, indoor, seconds);
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (chunk = 0; chunk < chunks; chunk++)
		mix(output, CHUNK, TRUE);
	clock_gettime(CLOCK_MONOTONIC, &end);
	elapsed = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
	printf("%lu voices (%lu out of earshot)%s: %.2f%% of a core, %.1f ns a frame\n", voices, silent,
		indoor ? " indoors" : "", 100.0 * elapsed / seconds, elapsed * 1e9 / ((double)chunks * CHUNK));
}

/* ---------- Xbox ADPCM */

/* two blocks (bytes (73 i + 41) mod 256 under their headers) and their frames
as a decoder written apart from the port's (tools: an IMA ADPCM decoder in
Python from the format, which ffmpeg's adpcm_ima_wav matches in the framing,
nibble order and interleave) decodes them: a stereo block (headers 1000 at
step 30, -20000 at step 60), and a mono one (32000 at step 80) that clips */
static const unsigned char adpcm_stereo_block[72] = {
	0xe8, 0x03, 0x1e, 0x00, 0xe0, 0xb1, 0x3c, 0x00, 0x71, 0xba, 0x03, 0x4c, 0x95, 0xde, 0x27, 0x70, 0xb9, 0x02, 0x4b,
	0x94, 0xdd, 0x26, 0x6f, 0xb8, 0x01, 0x4a, 0x93, 0xdc, 0x25, 0x6e, 0xb7, 0x00, 0x49, 0x92, 0xdb, 0x24, 0x6d, 0xb6,
	0xff, 0x48, 0x91, 0xda, 0x23, 0x6c, 0xb5, 0xfe, 0x47, 0x90, 0xd9, 0x22, 0x6b, 0xb4, 0xfd, 0x46, 0x8f, 0xd8, 0x21,
	0x6a, 0xb3, 0xfc, 0x45, 0x8e, 0xd7, 0x20, 0x69, 0xb2, 0xfb, 0x44, 0x8d, 0xd6, 0x1f, 0x68,
};
static const short adpcm_stereo_frames[2][64] = {
	{ 1000, 1048, 1268, 1111, 911, 1093, 1116, 922, 1157, 1063, 863, 993, 1016, 866, 1042, 1255, 1170, 1248, 1271,
	  1164, 1340, 1505, 1441, 1265, 1005, 902, 1186, 1377, 1274, 1054, 739, 1118, 1373, 1511, 1385, 1194, 812, 1169,
	  1400, 1021, 1684, 1413, 509, 1110, 1657, 961, 2137, 3579, 2221, 2749, 3550, 2822, 4544, 6186, 4694, 2948, -572,
	  -2081, 3866, 7918, 2762, -1925, -11056, 691 },
	{ -20000, -16876, -18122, -23036, -30402, -15694, -5183, -3272, 22787, -18179, -32768, 20477, 32767, -23096,
	  30149, 26054, -15, 32767, 32767, -15648, 32767, 32767, 4098, 7822, 11207, -22648, 30597, 32767, 4098, -32768,
	  -32768, -32768, 750, 32767, 4098, -32768, -32768, 28668, 32767, 32767, 21595, -15647, -32768, 20477, 32767,
	  -28669, -32764, -32768, -32768, 12285, 32767, -20478, -24573, 31290, -13763, -9668, 8953, -28289, -32384, 16031,
	  -29022, -32768, -20482, -24206 },
};
static const short adpcm_mono_frames[64] = {
	32000, 10978, 32767, 32767, 20481, -32768, -32768, -32768, -14147, -3991, 32767, 12289, -13780, 9919, 12996,
	-12187, 18284, 32767, 20481, -27934, -32768, 28668, 32767, 32767, 32767, 20481, -5588, 11340, 14417, -5169, 17724,
	32767, 21595, -15647, -32768, 20477, 32767, -23096, 30149, 26054, -15, 10141, 13218, -772, 22121, 32767, 24373,
	1480, -32375, 12678, 32767, -15648, 32767, 32767, 4098, 7822, 11207, 1975, 27158, 32767, 23535, 3949, -24031, 9487,
};

/* a block decoded a frame at a time from the format, apart from the port's
loops: frame 0 of a channel is its header's sample (the header's third byte
its step index, held to 88); frame n its nibble n - 1, the nibbles in 4-byte
words that take turns between the channels, eight a word, low nibble first
(the 64th pads the block); IMA ADPCM's step and its index's moves */
static void adpcm_expected(const unsigned char *block, unsigned long channels, short *frames)
{
	static const int steps[89] = {
		7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
		107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
		876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
		4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
		24623, 27086, 29794, 32767,
	};
	unsigned long channel, n;

	for (channel = 0; channel < channels; channel++)
	{
		long predictor = (short)(block[channel * 4] | (block[channel * 4 + 1] << 8));
		int index = block[channel * 4 + 2] > 88 ? 88 : block[channel * 4 + 2];

		frames[channel] = (short)predictor;
		for (n = 1; n < 64; n++)
		{
			unsigned long nibble_index = n - 1, word = nibble_index / 8, within = nibble_index % 8;
			unsigned char byte = block[4 * channels + (word * channels + channel) * 4 + within / 2];
			int nibble = within & 1 ? byte >> 4 : byte & 15, magnitude = nibble & 7, bit;
			long difference = steps[index] >> 3;

			for (bit = 2; bit >= 0; bit--)
			{
				if (magnitude & (1 << bit))
					difference += steps[index] >> (2 - bit);
			}
			predictor += nibble & 8 ? -difference : difference;
			predictor = predictor > 32767 ? 32767 : predictor < -32768 ? -32768 : predictor;
			index += magnitude < 4 ? -1 : 2 * (magnitude - 3);
			index = index < 0 ? 0 : index > 88 ? 88 : index;
			frames[n * channels + channel] = (short)predictor;
		}
	}
}

/* ---------- Doppler */

/* a 1 kHz sine at 22 kHz from a 3D voice ten units ahead of the listener (at
the origin, facing +z, a unit 3.048 metres as the game sets it), moving along z
at velocity units a second (towards the listener: negative), the listener at
listener_velocity, in mode; the shift the mixer works out, and the power of
what it plays besides a sine at expected times 1 kHz, against the sine (dB) */
static void doppler_figures(float velocity, float listener_velocity, DWORD mode, double expected, float *ratio,
	double *rest_db)
{
	unsigned long frames = 22050, outputs = 30000, played = (unsigned long)(OUTPUT_RATE / expected), n;
	short *samples = malloc(frames * sizeof(short));
	double *signal = malloc(frames * sizeof(double)), *exact = malloc(frames * sizeof(double)), amplitude;
	float *output = calloc(outputs, sizeof(float));
	struct sdl_stream *stream;

	for (n = 0; n < frames; n++)
		signal[n] = 0.5 * sin(2.0 * PI * 1000.0 * n / 22050.0);
	quantize(signal, samples, exact, frames);
	stream = voice_new(samples, frames, 1, 22050, 0);
	stream->has_3d = TRUE;
	stream->mode = mode;
	stream->position[2] = 10.0f;
	stream->velocity[2] = velocity;
	stream->minimum_distance = 1.0f;
	stream->maximum_distance = 1000.0f;
	listener.distance_factor = 3.048f;
	listener.velocity[2] = listener_velocity;
	*ratio = doppler_ratio(stream);
	render(stream, output, outputs, FALSE);
	/* (the frames it plays for, short of where it starts and ends) */
	tone_fit(output, 2000, (played < outputs ? played : outputs) - 2000, 1000.0 * expected / OUTPUT_RATE, &amplitude,
		rest_db);
	listener.velocity[2] = 0.0f;
	voice_delete(stream);
	free(output);
	free(exact);
	free(signal);
	free(samples);
}

/* ---------- 3D audio: the HRTF and the pan */

/* a mono 3D voice of frames frames at rate, right, up and ahead of the
listener (at the origin, facing +z, +y up: +x is its right), its minimum
distance a unit, not sending to the reverb */
static struct sdl_stream *voice_placed(const short *samples, unsigned long frames, unsigned long rate, float right,
	float up, float ahead)
{
	struct sdl_stream *stream = voice_new(samples, frames, 1, rate, 0);

	stream->has_3d = TRUE;
	stream->mode = DS3DMODE_NORMAL;
	stream->position[0] = right;
	stream->position[1] = up;
	stream->position[2] = ahead;
	stream->minimum_distance = 1.0f;
	stream->maximum_distance = 100.0f;
	return stream;
}

/* the listener as voice_placed has it */
static void listener_reset(void)
{
	memset(&listener, 0, sizeof(listener));
	listener.front[2] = 1.0f;
	listener.top[1] = 1.0f;
	listener.rolloff_factor = 1.0f;
	listener.distance_factor = 1.0f;
}

/* a second of noise at 48 kHz, as 16-bit samples */
static short *noise_samples(unsigned long frames)
{
	double *signal = noise(frames), *exact = malloc(frames * sizeof(double));
	short *samples = malloc(frames * sizeof(short));

	quantize(signal, samples, exact, frames);
	free(signal);
	free(exact);
	return samples;
}

/* frames of a mono 3D voice of noise at (right, up, ahead), through the
HRTF or panned: both ears, interleaved, after the first mix (its ramps) */
static float *placed_noise(BOOL through_hrtf, float right, float up, float ahead, unsigned long frames)
{
	short *samples = noise_samples(frames + 4096);
	struct sdl_stream *stream = voice_placed(samples, frames + 4096, 48000, right, up, ahead);
	float *output = malloc((frames + CHUNK) * 2 * sizeof(float));

	hrtf_enabled = through_hrtf;
	render(stream, output, frames + CHUNK, TRUE);
	memmove(output, output + CHUNK * 2, frames * 2 * sizeof(float));
	hrtf_enabled = FALSE;
	voice_delete(stream);
	free(samples);
	return output;
}

/* the level between the ears above a few kHz (the first difference's
power, left over right, dB) */
static double ears_level(const float *output, unsigned long frames)
{
	double left = 0.0, right = 0.0;
	unsigned long n;

	for (n = 1; n < frames; n++)
	{
		double l = output[n * 2] - output[(n - 1) * 2], r = output[n * 2 + 1] - output[(n - 1) * 2 + 1];

		left += l * l;
		right += r * r;
	}
	return 10.0 * log10((left + 1e-30) / (right + 1e-30));
}

/* how much later the right ear hears what the left does (frames, -48 to
48: the lag of the ears' greatest cross-correlation) */
static long ears_delay(const float *output, unsigned long frames)
{
	long lag, best = 0;
	double best_sum = -1e30;

	for (lag = -48; lag <= 48; lag++)
	{
		double sum = 0.0;
		unsigned long n;

		for (n = 64; n + 64 < frames; n++)
			sum += (double)output[n * 2] * output[(n + lag) * 2 + 1];
		if (sum > best_sum)
		{
			best_sum = sum;
			best = lag;
		}
	}
	return best;
}

/* an ear's (0 left, 1 right) power around hertz: a 1024-frame Hann
window's spectrum at the five bins nearest it, over every window, in dB */
static double ear_band(const float *output, unsigned long frames, int ear, double hertz)
{
	unsigned long first, n;
	long bin, center = lround(hertz * 1024.0 / OUTPUT_RATE);
	double power = 0.0;

	for (first = 0; first + 1024 <= frames; first += 512)
	{
		for (bin = center - 2; bin <= center + 2; bin++)
		{
			double re = 0.0, im = 0.0;

			for (n = 0; n < 1024; n++)
			{
				double window = 0.5 - 0.5 * cos(2.0 * PI * n / 1024.0);
				double x = output[(first + n) * 2 + ear] * window;

				re += x * cos(2.0 * PI * bin * n / 1024.0);
				im -= x * sin(2.0 * PI * bin * n / 1024.0);
			}
			power += re * re + im * im;
		}
	}
	return 10.0 * log10(power + 1e-30);
}

/* both ears' power, dB */
static double ears_power(const float *output, unsigned long frames)
{
	double power = 0.0;
	unsigned long n;

	for (n = 0; n < frames * 2; n++)
		power += (double)output[n] * output[n];
	return 10.0 * log10(power + 1e-30);
}

/* "circle": a sound going round the listener, ear high, 8 seconds from
ahead to the right, behind, the left and ahead again, then 4 over the
head, ahead to above, behind and below, through the HRTF or panned, to a
16-bit WAV file: bursts of noise, four a second, which ears place best */
static int circle(const char *path, BOOL through_hrtf)
{
	unsigned long frames = OUTPUT_RATE * 12, n, done;
	short *samples = malloc((frames + OUTPUT_RATE) * sizeof(short));
	float *output = calloc(frames * 2, sizeof(float));
	struct sdl_stream *stream;
	FILE *file;
	unsigned char header[44];
	unsigned long bytes = frames * 4;

	random_state = 3;
	for (n = 0; n < frames + OUTPUT_RATE; n++)
	{
		double t = (double)(n % (OUTPUT_RATE / 4)) / OUTPUT_RATE;
		double envelope = t < 0.002 ? t / 0.002 : t < 0.12 ? exp(-(t - 0.002) * 18.0) : 0.0;

		samples[n] = (short)(26000.0 * envelope * random_uniform());
	}
	hrtf_initialize();
	listener_reset();
	stream = voice_placed(samples, frames + OUTPUT_RATE, OUTPUT_RATE, 0.0f, 0.0f, 1.5f);
	hrtf_enabled = through_hrtf;
	for (done = 0; done < frames; done += CHUNK)
	{
		static float buffer[CHUNK * 2], send[CHUNK];
		double t = (double)done / OUTPUT_RATE, angle;
		unsigned long count = frames - done < CHUNK ? frames - done : CHUNK, frame;

		if (t < 8.0)
		{
			angle = 2.0 * PI * t / 8.0;
			stream->position[0] = (float)(1.5 * sin(angle));
			stream->position[1] = 0.0f;
			stream->position[2] = (float)(1.5 * cos(angle));
		}
		else
		{
			angle = 2.0 * PI * (t - 8.0) / 4.0;
			stream->position[0] = 0.0f;
			stream->position[1] = (float)(1.5 * sin(angle));
			stream->position[2] = (float)(1.5 * cos(angle));
		}
		memset(buffer, 0, sizeof(buffer));
		memset(send, 0, sizeof(send));
		mix_voice(stream, buffer, send, count);
		for (frame = 0; frame < count * 2; frame++)
			output[done * 2 + frame] = buffer[frame];
	}
	hrtf_enabled = FALSE;
	file = fopen(path, "wb");
	if (!file)
		return 2;
	memcpy(header, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data\0\0\0\0", 44);
	for (n = 0; n < 4; n++)
	{
		header[4 + n] = (unsigned char)((bytes + 36) >> (8 * n));
		header[40 + n] = (unsigned char)(bytes >> (8 * n));
	}
	fwrite(header, 1, 44, file);
	for (n = 0; n < frames * 2; n++)
	{
		long value = lround(output[n] * 32767.0);
		unsigned char pair[2];

		value = value > 32767 ? 32767 : value < -32768 ? -32768 : value;
		pair[0] = (unsigned char)(value & 0xff);
		pair[1] = (unsigned char)((value >> 8) & 0xff);
		fwrite(pair, 1, 2, file);
	}
	fclose(file);
	voice_delete(stream);
	free(samples);
	free(output);
	return 0;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static const unsigned long rates[][2] = {
		{ 22050, 0 }, { 44100, 0 }, { 48000, 0 }, { 22050, 17000 }, { 22050, 30000 }, { 44100, 60000 },
		{ 44100, 96000 }, { 32000, 0 },
	};
	unsigned long index;

	resampler_initialize();
	resampler_phases_initialize();

	/* the resampler is the low pass worked out exactly: noise at every rate,
	the error well under 16-bit's */
	CASE("reference")
	{
		double *signal = noise(30000);

		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			double error_db, peak_db;

			against_reference(signal, 30000, rates[index][0], rates[index][1], &error_db, &peak_db);
			CHECK(error_db < -100.0 && peak_db < -90.0, "%lu Hz played at %lu: the error %.1f dB, its peak %.1f dB",
				rates[index][0], rates[index][1], error_db, peak_db);
		}
		free(signal);
		return 0;
	}
	/* an impulse: the low pass itself */
	CASE("impulse")
	{
		double *signal = calloc(4000, sizeof(double));

		signal[2000] = 1.0;
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			double error_db, peak_db;

			against_reference(signal, 4000, rates[index][0], rates[index][1], &error_db, &peak_db);
			CHECK(peak_db < -90.0, "%lu Hz played at %lu: the impulse's error peaks at %.1f dB", rates[index][0],
				rates[index][1], peak_db);
		}
		free(signal);
		return 0;
	}
	/* sines as heard: a 22 kHz voice flat within half a dB to 10 kHz, a 44
	kHz one to 20 kHz, the rest 70 dB down in the band, more past it */
	CASE("sines")
	{
		static const struct { unsigned long rate, frequency; double hertz, gain_db, rest_db; } sines[] = {
			{ 22050, 0, 100.0, 0.05, -85.0 }, { 22050, 0, 1000.0, 0.05, -85.0 }, { 22050, 0, 5000.0, 0.05, -80.0 },
			{ 22050, 0, 8000.0, 0.1, -75.0 }, { 22050, 0, 10000.0, 0.5, -70.0 },
			{ 44100, 0, 1000.0, 0.05, -85.0 }, { 44100, 0, 10000.0, 0.05, -80.0 },
			{ 44100, 0, 16000.0, 0.1, -75.0 }, { 44100, 0, 20000.0, 0.5, -70.0 },
			{ 48000, 0, 1000.0, 0.05, -85.0 }, { 48000, 0, 20000.0, 0.5, -70.0 },
			{ 22050, 30000, 5000.0, 0.1, -75.0 },
		};

		for (index = 0; index < sizeof(sines) / sizeof(sines[0]); index++)
		{
			double gain_db, rest_db;

			sine_figures(sines[index].hertz, sines[index].rate, sines[index].frequency, &gain_db, &rest_db);
			CHECK(fabs(gain_db) < sines[index].gain_db && rest_db < sines[index].rest_db,
				"%.0f Hz at %lu Hz, played at %lu: gain %.2f dB, the rest %.1f dB", sines[index].hertz,
				sines[index].rate, sines[index].frequency, gain_db, rest_db);
		}
		return 0;
	}
	/* a tone a voice played faster than the output rate takes over the
	output's band does not come out as an alias */
	CASE("aliases")
	{
		double gain_db, rest_db;

		/* 18 kHz at 44.1 kHz, played at 96 kHz: 39 kHz */
		sine_figures(18000.0, 44100, 96000, &gain_db, &rest_db);
		CHECK(rest_db < -60.0, "an alias of 39 kHz %.1f dB", rest_db);
		/* 15 kHz at 44.1 kHz, played at 88.2 kHz: 30 kHz */
		sine_figures(15000.0, 44100, 88200, &gain_db, &rest_db);
		CHECK(rest_db < -60.0, "an alias of 30 kHz %.1f dB", rest_db);
		return 0;
	}
	/* a voice out of earshot moves on as one heard does: once it is audible
	again, both play the same */
	CASE("silent-voice")
	{
		double *signal = noise(60000);
		short *samples = malloc(60000 * sizeof(short));
		double *exact = malloc(60000 * sizeof(double));
		float *heard = calloc(40000 * 2, sizeof(float)), *skipped = calloc(40000 * 2, sizeof(float));
		struct sdl_stream *loud, *quiet;
		unsigned long n, sizes[] = { 1, 7, 512, 333, 2000, 1024 };

		quantize(signal, samples, exact, 60000);
		for (index = 0; index < 3; index++)
		{
			unsigned long frequency = index == 0 ? 0 : index == 1 ? 23456 : 70000, done = 0, size = 0;

			loud = voice_new(samples, 60000, 1, 22050, frequency);
			quiet = voice_new(samples, 60000, 1, 22050, frequency);
			/* the quiet one turned down for a while, at various mix sizes,
			then up */
			quiet->volume = 0.0f;
			while (done < 20000)
			{
				unsigned long count = sizes[size++ % 6];
				static float scratch[2000 * 2], send[2000];

				memset(scratch, 0, sizeof(scratch));
				memset(send, 0, sizeof(send));
				mix_voice(quiet, scratch, send, count);
				for (n = 0; n < count * 2; n++)
					CHECK(scratch[n] == 0.0f, "a voice turned down was heard");
				memset(scratch, 0, sizeof(scratch));
				mix_voice(loud, scratch, send, count);
				done += count;
			}
			CHECK(quiet->center == loud->center && quiet->phase == loud->phase &&
				quiet->history_count == loud->history_count && quiet->cursor == loud->cursor,
				"played at %lu: turned down, the voice is at %lu + %f, heard at %lu + %f", frequency,
				quiet->center, quiet->phase, loud->center, loud->phase);
			/* (it fades in over a mix from 0; the loud one does the same) */
			quiet->volume = 1.0f;
			loud->current_left = loud->current_right = 0.0f;
			render(quiet, skipped, 40000, TRUE);
			render(loud, heard, 40000, TRUE);
			for (n = 0; n < 40000 * 2; n++)
				CHECK(fabsf(heard[n] - skipped[n]) < 1.0e-6f, "played at %lu: frame %lu differs (%f, %f)",
					frequency, n / 2, heard[n], skipped[n]);
			voice_delete(loud);
			voice_delete(quiet);
		}
		/* a voice that runs out of packets while it is turned down ends as
		one heard does */
		loud = voice_new(samples, 3000, 1, 22050, 0);
		quiet = voice_new(samples, 3000, 1, 22050, 0);
		quiet->volume = 0.0f;
		render(loud, heard, 20000, TRUE);
		render(quiet, skipped, 20000, TRUE);
		CHECK(quiet->center == loud->center && quiet->silence == loud->silence &&
			quiet->mixing[0].finished == loud->mixing[0].finished, "turned down, the voice ends at %lu (%lu), "
			"heard at %lu (%lu)", quiet->center, quiet->silence, loud->center, loud->silence);
		voice_delete(loud);
		voice_delete(quiet);
		free(heard);
		free(skipped);
		free(exact);
		free(samples);
		free(signal);
		return 0;
	}
	/* a mono voice is heard as a stereo one with the same two channels */
	CASE("mono")
	{
		double *signal = noise(30000);
		short *samples = malloc(30000 * sizeof(short)), *pairs = malloc(30000 * 2 * sizeof(short));
		double *exact = malloc(30000 * sizeof(double));
		float *mono = calloc(50000 * 2, sizeof(float)), *stereo = calloc(50000 * 2, sizeof(float));
		unsigned long n;

		quantize(signal, samples, exact, 30000);
		for (n = 0; n < 30000; n++)
			pairs[n * 2] = pairs[n * 2 + 1] = samples[n];
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			struct sdl_stream *one = voice_new(samples, 30000, 1, rates[index][0], rates[index][1]);
			struct sdl_stream *two = voice_new(pairs, 30000, 2, rates[index][0], rates[index][1]);

			render(one, mono, 50000, TRUE);
			render(two, stereo, 50000, TRUE);
			for (n = 0; n < 50000 * 2; n++)
				CHECK(fabsf(mono[n] - stereo[n]) < 1.0e-6f, "%lu Hz played at %lu: frame %lu differs (%f, %f)",
					rates[index][0], rates[index][1], n / 2, mono[n], stereo[n]);
			voice_delete(one);
			voice_delete(two);
		}
		free(mono);
		free(stereo);
		free(exact);
		free(pairs);
		free(samples);
		free(signal);
		return 0;
	}
	/* audio.resampler = "linear": each frame the two source frames around
	it, weighted by how near it is to each */
	CASE("linear")
	{
		double *signal = noise(30000);
		short *samples = malloc(30000 * sizeof(short));
		double *exact = malloc(30000 * sizeof(double));
		float *output = calloc(50000, sizeof(float));
		unsigned long n;

		resampler_setting = "linear";
		resampler_mode = resampler_mode_read();
		CHECK(resampler_mode == RESAMPLER_LINEAR, "audio.resampler = \"linear\" is not read as linear");
		quantize(signal, samples, exact, 30000);
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			double step = (double)(rates[index][1] ? rates[index][1] : rates[index][0]) / OUTPUT_RATE;
			unsigned long outputs = (unsigned long)((30000 - 1) / step);
			struct sdl_stream *stream = voice_new(samples, 30000, 1, rates[index][0], rates[index][1]);

			if (outputs > 50000)
				outputs = 50000;
			render(stream, output, outputs, FALSE);
			for (n = 0; n < outputs; n++)
			{
				double time = (double)n * step, fraction = time - floor(time);
				long first = (long)floor(time);
				double expected = exact[first] + (first + 1 < 30000 ? exact[first + 1] - exact[first] : -exact[first]) *
					fraction;

				CHECK(fabs(output[n] - expected) < 1.0e-5, "%lu Hz played at %lu: frame %lu is %f, not %f",
					rates[index][0], rates[index][1], n, output[n], expected);
			}
			voice_delete(stream);
		}
		resampler_setting = "sinc";
		CHECK(resampler_mode_read() == RESAMPLER_SINC, "audio.resampler = \"sinc\" is not read as sinc");
		resampler_setting = NULL;
		CHECK(resampler_mode_read() == RESAMPLER_SINC, "audio.resampler unset is not read as sinc");
		free(output);
		free(exact);
		free(samples);
		free(signal);
		return 0;
	}
	/* Xbox ADPCM decoded as the reference decoder decodes the two blocks, and
	as the format reads a frame at a time for blocks of every header (step
	indices past 88, predictors at full scale, nibbles that clip) */
	CASE("adpcm")
	{
		static unsigned char blocks[400 * 72];
		short expected[64 * 2];
		unsigned long frames, channels, block, n;
		short *decoded = decode_adpcm(adpcm_stereo_block, 72, 2, &frames);
		unsigned char mono[36];

		CHECK(decoded && frames == 64, "a stereo block is %lu frames, not 64", frames);
		for (n = 0; n < 64 * 2; n++)
			CHECK(decoded[n] == adpcm_stereo_frames[n % 2][n / 2], "stereo frame %lu, channel %lu: %d, not %d", n / 2,
				n % 2, decoded[n], adpcm_stereo_frames[n % 2][n / 2]);
		free(decoded);
		mono[0] = 0x00;
		mono[1] = 0x7d;
		mono[2] = 80;
		mono[3] = 0;
		for (n = 4; n < 36; n++)
			mono[n] = (unsigned char)((n * 73 + 41) & 0xff);
		decoded = decode_adpcm(mono, 36, 1, &frames);
		CHECK(decoded && frames == 64, "a mono block is %lu frames, not 64", frames);
		for (n = 0; n < 64; n++)
			CHECK(decoded[n] == adpcm_mono_frames[n], "mono frame %lu: %d, not %d", n, decoded[n], adpcm_mono_frames[n]);
		free(decoded);
		random_state = 3;
		for (n = 0; n < sizeof(blocks); n++)
			blocks[n] = (unsigned char)((random_uniform() + 1.0) * 128.0);
		for (channels = 1; channels <= 2; channels++)
		{
			unsigned long count = sizeof(blocks) / (36 * channels);

			/* (and a part block at the end, which is not played) */
			decoded = decode_adpcm(blocks, sizeof(blocks) - 7, channels, &frames);
			CHECK(frames == (count - 1) * 64, "%lu channels: %lu frames of %lu blocks and a part", channels, frames,
				count);
			for (block = 0; block + 1 < count; block++)
			{
				adpcm_expected(blocks + block * 36 * channels, channels, expected);
				for (n = 0; n < 64 * channels; n++)
					CHECK(decoded[block * 64 * channels + n] == expected[n], "%lu channels, block %lu (step index %d), "
						"frame %lu: %d, not %d", channels, block, blocks[block * 36 * channels + 4 * (n % channels) + 2],
						n / channels, decoded[block * 64 * channels + n], expected[n]);
			}
			free(decoded);
		}
		return 0;
	}
	/* the Doppler shift: a sound coming at 10 units a second (30.5 m/s) is
	heard 343 / (343 - 30.5) times as high, going away 343 / (343 + 30.5); the
	listener coming at it (343 + 30.5) / 343; head-relative, the voice's own
	velocity alone; to an octave at most; none with audio.doppler false, nor
	for a 2D voice */
	CASE("doppler")
	{
		static const struct { float velocity, listener_velocity; DWORD mode; double expected; } cases[] = {
			{ 0.0f, 0.0f, DS3DMODE_NORMAL, 1.0 },
			{ -10.0f, 0.0f, DS3DMODE_NORMAL, 343.0 / (343.0 - 30.48) },
			{ 10.0f, 0.0f, DS3DMODE_NORMAL, 343.0 / (343.0 + 30.48) },
			{ 0.0f, 10.0f, DS3DMODE_NORMAL, (343.0 + 30.48) / 343.0 },
			{ -10.0f, 10.0f, DS3DMODE_HEADRELATIVE, 343.0 / (343.0 - 30.48) },
			{ -1000.0f, 0.0f, DS3DMODE_NORMAL, 2.0 },
			{ -10.0f, 0.0f, DS3DMODE_DISABLE, 1.0 },
		};
		float ratio;
		double rest_db;

		resampler_mode = RESAMPLER_SINC;
		for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
		{
			doppler_figures(cases[index].velocity, cases[index].listener_velocity, cases[index].mode,
				cases[index].expected, &ratio, &rest_db);
			CHECK(fabs(ratio - cases[index].expected) < 1.0e-5 && rest_db < -60.0, "at %.0f units a second, the "
				"listener at %.0f (mode %lu): shifted %.5f, not %.5f; the rest %.1f dB", cases[index].velocity,
				cases[index].listener_velocity, cases[index].mode, ratio, cases[index].expected, rest_db);
		}
		doppler_enabled = FALSE;
		doppler_figures(-10.0f, 0.0f, DS3DMODE_NORMAL, 1.0, &ratio, &rest_db);
		CHECK(ratio == 1.0f && rest_db < -60.0, "audio.doppler false: shifted %.5f, the rest %.1f dB", ratio, rest_db);
		doppler_enabled = TRUE;
		return 0;
	}
	/* the Doppler shift glides: a voice whose velocity jumps from mix to mix
	between 5 and 15 units a second towards the listener (as the camera's
	velocity did between the frames, sound_manager.c refresh_listener: its
	shift from mix to mix 1.046 or 1.153, 170 cents apart) is heard steady,
	at the shift in between, its pitch moving under 25 cents */
	CASE("doppler-steady")
	{
		static short samples[22050];
		static float output[CHUNK * 2], send[CHUNK];
		struct sdl_stream *stream;
		float lowest = 10.0f, highest = 0.0f, middle = 343.0f / (343.0f - 10.0f * 3.048f);
		unsigned long mix;

		resampler_mode = RESAMPLER_SINC;
		listener.distance_factor = 3.048f;
		stream = voice_new(samples, 22050, 1, 22050, 0);
		stream->has_3d = TRUE;
		stream->mode = DS3DMODE_NORMAL;
		stream->position[2] = 10.0f;
		stream->minimum_distance = 1.0f;
		stream->maximum_distance = 1000.0f;
		for (mix = 0; mix < 40; mix++)
		{
			stream->velocity[2] = mix & 1 ? -15.0f : -5.0f;
			mix_voice(stream, output, send, CHUNK);
			/* (after a quarter of a second) */
			if (mix * CHUNK >= OUTPUT_RATE / 4)
			{
				lowest = stream->current_doppler < lowest ? stream->current_doppler : lowest;
				highest = stream->current_doppler > highest ? stream->current_doppler : highest;
			}
		}
		CHECK(1200.0 * log2((double)highest / lowest) < 25.0 && lowest < middle && highest > middle,
			"heard from x%.4f to x%.4f (%.1f cents), around x%.4f", lowest, highest,
			1200.0 * log2((double)highest / lowest), middle);
		voice_delete(stream);
		return 0;
	}
	/* the limiter leaves a mix up to full scale as it is (the menus' music
	reaches it), and holds two such voices' sum to it */
	CASE("limiter")
	{
		unsigned long frames = 48000 * 2, n, chunk;
		short *samples = malloc(frames * 2 * sizeof(short));
		double *signal = malloc(frames * sizeof(double)), *exact = malloc(frames * sizeof(double));
		static float mixed[CHUNK * 2];
		struct sdl_stream *first, *second;
		float alone = 0.0f, both = 0.0f;

		for (n = 0; n < frames; n++)
			signal[n] = 0.999 * sin(2.0 * PI * 997.0 * n / 48000.0);
		for (n = 0; n < frames; n++)
			samples[n * 2] = samples[n * 2 + 1] = (short)lround(signal[n] * 32768.0);
		reverb_initialize();
		first = voice_new(samples, frames, 2, 48000, 0);
		streams = first;
		for (chunk = 0; chunk < OUTPUT_RATE / 2 / CHUNK; chunk++)
		{
			mix(mixed, CHUNK, TRUE);
			for (n = 0; chunk && n < CHUNK * 2; n++)
				alone = fabsf(mixed[n]) > alone ? fabsf(mixed[n]) : alone;
		}
		CHECK(alone > 0.998f && alone < 1.0f, "a voice at -0.01 dBFS comes out at %.2f dBFS", decibels(alone));
		second = voice_new(samples, frames, 2, 48000, 0);
		second->next = first;
		streams = second;
		for (chunk = 0; chunk < OUTPUT_RATE / 2 / CHUNK; chunk++)
		{
			mix(mixed, CHUNK, TRUE);
			for (n = 0; chunk && n < CHUNK * 2; n++)
				both = fabsf(mixed[n]) > both ? fabsf(mixed[n]) : both;
		}
		CHECK(both <= 1.0f + 1.0e-6f && both > 0.99f, "two voices at -0.01 dBFS come out at %.2f dBFS",
			decibels(both));
		streams = NULL;
		voice_delete(first);
		voice_delete(second);
		free(exact);
		free(signal);
		free(samples);
		return 0;
	}
	/* audio.channels and the device's channels: "auto" is 5.1 on six or
	more, stereo on fewer and when the device cannot say; "5.1" and "stereo"
	whatever the device; anything else stereo */
	CASE("channels")
	{
		static const struct { const char *setting; int device_channels; unsigned long expected; } cases[] = {
			{ "auto", 6, 6 }, { "auto", 8, 6 }, { "auto", 2, 2 }, { "auto", 1, 2 }, { "auto", 4, 2 },
			{ "auto", 5, 2 }, { "auto", 0, 2 }, { "stereo", 6, 2 }, { "stereo", 8, 2 }, { "5.1", 2, 6 },
			{ "5.1", 0, 6 }, { "7.1", 8, 2 }, { "", 6, 2 },
		};

		for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
		{
			unsigned long chosen = output_channels_choose(cases[index].setting, cases[index].device_channels);

			CHECK(chosen == cases[index].expected, "audio.channels \"%s\" on a device of %d channels: %lu, not %lu",
				cases[index].setting, cases[index].device_channels, chosen, cases[index].expected);
		}
		return 0;
	}
	/* the 5.1 pan (VBAP): a sound at each speaker's angle plays from that
	speaker alone; one at any angle from the two either side of it, their
	power summing to 1, their gains' vector sum pointing its way (where the
	tangent law puts the image); never from the LFE. A 3D voice through the
	mixer on 5.1: from its speaker's channel, nearly all of it. 2D voices by
	the mix bins the game gives them on AC-3 */
	CASE("surround")
	{
		/* each speaker's angle (degrees, clockwise from ahead) and its channel
		in SDL's 5.1 order (FL, FR, FC, LFE, BL, BR) */
		static const struct { double degrees; unsigned long channel; } speakers[] = {
			{ 0.0, 2 }, { 30.0, 1 }, { 110.0, 5 }, { 250.0, 4 }, { 330.0, 0 },
		};
		static const double channel_degrees[MAXIMUM_OUTPUT_CHANNELS] = { 330.0, 30.0, 0.0, -1.0, 250.0, 110.0 };
		float gains[MAXIMUM_OUTPUT_CHANNELS];
		unsigned long speaker, degree;

		for (index = 0; index < sizeof(speakers) / sizeof(speakers[0]); index++)
		{
			double radians = speakers[index].degrees * PI / 180.0;

			surround_pan((float)sin(radians), (float)cos(radians), 0.0f, gains);
			for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			{
				CHECK(fabsf(gains[speaker] - (speaker == speakers[index].channel ? 1.0f : 0.0f)) < 1.0e-4f,
					"a sound at %.0f degrees: channel %lu at %f (its speaker is channel %lu)", speakers[index].degrees,
					speaker, gains[speaker], speakers[index].channel);
			}
		}
		for (degree = 0; degree < 3600; degree++)
		{
			double radians = degree * PI / 1800.0, power = 0.0, x = 0.0, y = 0.0, heard;
			unsigned long sounding = 0;

			surround_pan((float)sin(radians), (float)cos(radians), 0.0f, gains);
			for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			{
				power += (double)gains[speaker] * gains[speaker];
				if (gains[speaker] > 1.0e-4f)
				{
					sounding++;
					x += gains[speaker] * sin(channel_degrees[speaker] * PI / 180.0);
					y += gains[speaker] * cos(channel_degrees[speaker] * PI / 180.0);
				}
			}
			heard = atan2(x, y) * 180.0 / PI;
			heard = fmod(heard - degree / 10.0 + 540.0, 360.0) - 180.0;
			CHECK(gains[3] == 0.0f, "a sound at %.1f degrees: the LFE at %f", degree / 10.0, gains[3]);
			CHECK(fabs(power - 1.0) < 1.0e-4 && sounding <= 2 && fabs(heard) < 0.05,
				"a sound at %.1f degrees: power %f, %lu speakers, heard %.2f degrees off", degree / 10.0, power,
				sounding, heard);
		}
		/* through the mixer: a 1 kHz mono 3D voice 50 units away at each
		speaker's angle, 5.1 out */
		output_channels = 6;
		for (index = 0; index < sizeof(speakers) / sizeof(speakers[0]); index++)
		{
			unsigned long frames = 22050, count = 8192, n;
			short *samples = malloc(frames * sizeof(short));
			float *output = calloc(count * MAXIMUM_OUTPUT_CHANNELS, sizeof(float));
			static float send[8192];
			double radians = speakers[index].degrees * PI / 180.0, energy[MAXIMUM_OUTPUT_CHANNELS] = { 0 }, total = 0.0;
			struct sdl_stream *stream;

			for (n = 0; n < frames; n++)
				samples[n] = (short)lround(16000.0 * sin(2.0 * PI * 1000.0 * n / 22050.0));
			stream = voice_new(samples, frames, 1, 22050, 0);
			stream->has_3d = TRUE;
			stream->mode = DS3DMODE_NORMAL;
			stream->position[0] = (float)(50.0 * sin(radians));
			stream->position[2] = (float)(50.0 * cos(radians));
			stream->minimum_distance = 1.0f;
			stream->maximum_distance = 1000.0f;
			mix_voice(stream, output, send, count);
			for (n = 0; n < count; n++)
			{
				for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
				{
					energy[speaker] += (double)output[n * MAXIMUM_OUTPUT_CHANNELS + speaker] *
						output[n * MAXIMUM_OUTPUT_CHANNELS + speaker];
				}
			}
			for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
				total += energy[speaker];
			CHECK(total > 0.0 && energy[speakers[index].channel] > 0.9 * total && energy[3] == 0.0,
				"a 3D voice at %.0f degrees on 5.1: %.1f%% from channel %lu (its speaker), the LFE %g",
				speakers[index].degrees, total > 0.0 ? 100.0 * energy[speakers[index].channel] / total : 0.0,
				speakers[index].channel, energy[3]);
			voice_delete(stream);
			free(output);
			free(samples);
		}
		/* a mono 2D voice at half to the front three; a stereo one's left
		whole to the front left and at half to the back left, its right
		likewise; none to the center or the LFE */
		{
			unsigned long frames = 4800, count = 4096, n;
			short *samples = malloc(frames * 2 * sizeof(short));
			float *output = calloc(count * MAXIMUM_OUTPUT_CHANNELS, sizeof(float));
			static float send[4096];
			float surround[2][MAXIMUM_OUTPUT_CHANNELS];
			struct sdl_stream *stream;

			for (n = 0; n < frames * 2; n++)
				samples[n] = (short)(n & 1 ? -8000 : 8000);
			stream = voice_new(samples, frames, 1, 48000, 0);
			memset(stream->surround_bins, 0, sizeof(stream->surround_bins));
			stream->surround_bins[0][0] = stream->surround_bins[0][1] = stream->surround_bins[0][2] = 0.5f;
			voice_surround_gains(stream, surround);
			CHECK(surround[0][0] == 0.5f && surround[0][1] == 0.5f && surround[0][2] == 0.5f && surround[0][3] == 0.0f &&
				surround[0][4] == 0.0f && surround[0][5] == 0.0f, "a mono 2D voice: %g %g %g %g %g %g", surround[0][0],
				surround[0][1], surround[0][2], surround[0][3], surround[0][4], surround[0][5]);
			voice_delete(stream);
			stream = voice_new(samples, frames, 2, 48000, 0);
			memset(stream->surround_bins, 0, sizeof(stream->surround_bins));
			stream->surround_bins[0][0] = 1.0f;
			stream->surround_bins[1][1] = 1.0f;
			stream->surround_bins[0][4] = 0.5f;
			stream->surround_bins[1][5] = 0.5f;
			mix_voice(stream, output, send, count);
			/* (the left +8000, the right -8000: each speaker its side's sign) */
			n = 2048 * MAXIMUM_OUTPUT_CHANNELS;
			CHECK(output[n] > 0.2f && output[n + 1] < -0.2f && fabsf(output[n + 4] - 0.5f * output[n]) < 1.0e-3f &&
				fabsf(output[n + 5] - 0.5f * output[n + 1]) < 1.0e-3f && output[n + 2] == 0.0f && output[n + 3] == 0.0f,
				"a stereo 2D voice on 5.1: %f %f %f %f %f %f", output[n], output[n + 1], output[n + 2], output[n + 3],
				output[n + 4], output[n + 5]);
			voice_delete(stream);
			free(output);
			free(samples);
		}
		output_channels = 2;
		return 0;
	}
	/* audio.spatial: "auto" through the HRTF on headphones only (speakers
	and not known: the pan), "headphones" and "speakers" whatever the host
	says, another word as "auto"; the VR mode by vr.hrtf alone */
	CASE("spatial-choice")
	{
		static const struct { const char *setting; int headphones; BOOL vr, vr_hrtf, expected; } cases[] = {
			{ "auto", 1, FALSE, FALSE, TRUE }, { "auto", 0, FALSE, FALSE, FALSE },
			{ "auto", -1, FALSE, FALSE, FALSE }, { "headphones", 0, FALSE, FALSE, TRUE },
			{ "headphones", -1, FALSE, FALSE, TRUE }, { "headphones", 1, FALSE, FALSE, TRUE },
			{ "speakers", 1, FALSE, FALSE, FALSE }, { "speakers", -1, FALSE, FALSE, FALSE },
			{ "", 1, FALSE, FALSE, TRUE }, { "", -1, FALSE, FALSE, FALSE },
			{ "auto", 1, TRUE, FALSE, FALSE }, { "speakers", 0, TRUE, TRUE, TRUE },
			{ "headphones", 1, TRUE, FALSE, FALSE },
		};

		for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
		{
			CHECK(spatial_hrtf(cases[index].setting, cases[index].headphones, cases[index].vr, cases[index].vr_hrtf) ==
				cases[index].expected, "audio.spatial \"%s\", the host saying %d%s: %s, not %s", cases[index].setting,
				cases[index].headphones, cases[index].vr ? (cases[index].vr_hrtf ? ", VR with vr.hrtf" : ", VR") : "",
				cases[index].expected ? "panned" : "through the HRTF", cases[index].expected ? "through the HRTF" : "panned");
		}
		return 0;
	}
	/* a 3D voice of noise through the HRTF: to the left (90 degrees), the
	right ear 0.66 ms later (Woodworth's 31.5 frames) and its high
	frequencies well down; ahead and behind, the ears alike, and behind
	duller above a few kHz; above, the pinna's notch moved up from ahead's
	7.7 kHz to 11 kHz, below down to 6 kHz. Panned: to the left, louder in
	the left ear but at the same time; above, below and behind exactly as
	ahead (no cue of them) */
	CASE("hrtf-cues")
	{
		unsigned long frames = 48000;
		float *left_hrtf, *ahead_hrtf, *behind_hrtf, *above_hrtf, *below_hrtf, *left_pan, *ahead_pan, *above_pan,
			*below_pan, *behind_pan;
		double notch_ahead, notch_above, notch_below, rear_db, low_db;
		long delay;
		int ear;

		hrtf_initialize();
		listener_reset();
		left_hrtf = placed_noise(TRUE, -3.0f, 0.0f, 0.0f, frames);
		ahead_hrtf = placed_noise(TRUE, 0.0f, 0.0f, 3.0f, frames);
		behind_hrtf = placed_noise(TRUE, 0.0f, 0.0f, -3.0f, frames);
		above_hrtf = placed_noise(TRUE, 0.0f, 3.0f, 0.0f, frames);
		below_hrtf = placed_noise(TRUE, 0.0f, -3.0f, 0.0f, frames);
		left_pan = placed_noise(FALSE, -3.0f, 0.0f, 0.0f, frames);
		ahead_pan = placed_noise(FALSE, 0.0f, 0.0f, 3.0f, frames);
		behind_pan = placed_noise(FALSE, 0.0f, 0.0f, -3.0f, frames);
		above_pan = placed_noise(FALSE, 0.0f, 3.0f, 0.0f, frames);
		below_pan = placed_noise(FALSE, 0.0f, -3.0f, 0.0f, frames);

		delay = ears_delay(left_hrtf, frames);
		CHECK(delay >= 29 && delay <= 34, "to the left through the HRTF, the right ear %ld frames later, not 31.5", delay);
		CHECK(ears_level(left_hrtf, frames) > 10.0, "to the left through the HRTF, the left ear's high frequencies "
			"%+.1f dB over the right's", ears_level(left_hrtf, frames));
		delay = ears_delay(left_pan, frames);
		CHECK(delay == 0, "to the left panned, the right ear %ld frames later, not at once", delay);
		CHECK(ears_level(left_pan, frames) > 6.0, "to the left panned, the left ear %+.1f dB over the right",
			ears_level(left_pan, frames));
		CHECK(labs(ears_delay(ahead_hrtf, frames)) <= 1 && fabs(ears_level(ahead_hrtf, frames)) < 0.5 &&
			labs(ears_delay(behind_hrtf, frames)) <= 1 && fabs(ears_level(behind_hrtf, frames)) < 0.5,
			"ahead and behind through the HRTF, the ears differ: %ld and %ld frames, %+.2f and %+.2f dB",
			ears_delay(ahead_hrtf, frames), ears_delay(behind_hrtf, frames), ears_level(ahead_hrtf, frames),
			ears_level(behind_hrtf, frames));
		for (ear = 0; ear < 2; ear++)
		{
			rear_db = ear_band(behind_hrtf, frames, ear, 15000.0) - ear_band(ahead_hrtf, frames, ear, 15000.0);
			low_db = ear_band(behind_hrtf, frames, ear, 300.0) - ear_band(ahead_hrtf, frames, ear, 300.0);
			CHECK(rear_db < -4.0 && fabs(low_db) < 1.0, "behind through the HRTF, 15 kHz %+.1f dB and 300 Hz %+.1f dB "
				"of ahead's (ear %d)", rear_db, low_db, ear);
			/* (each notch: its band against the other's) */
			notch_ahead = ear_band(ahead_hrtf, frames, ear, 7667.0) - ear_band(ahead_hrtf, frames, ear, 11000.0);
			notch_above = ear_band(above_hrtf, frames, ear, 7667.0) - ear_band(above_hrtf, frames, ear, 11000.0);
			notch_below = ear_band(below_hrtf, frames, ear, 6000.0) - ear_band(below_hrtf, frames, ear, 7667.0);
			CHECK(notch_above - notch_ahead > 8.0 && notch_below < -4.0, "the notch: ahead 7.7 kHz %+.1f dB of 11 kHz, "
				"above %+.1f dB; below 6 kHz %+.1f dB of 7.7 kHz (ear %d)", notch_ahead, notch_above, notch_below, ear);
		}
		for (index = 0; index < frames * 2; index++)
		{
			CHECK(above_pan[index] == ahead_pan[index] && below_pan[index] == ahead_pan[index] &&
				behind_pan[index] == ahead_pan[index], "panned, above, below or behind differs from ahead at %lu",
				index);
		}
		free(left_hrtf);
		free(ahead_hrtf);
		free(behind_hrtf);
		free(above_hrtf);
		free(below_hrtf);
		free(left_pan);
		free(ahead_pan);
		free(behind_pan);
		free(above_pan);
		free(below_pan);
		return 0;
	}
	/* through the HRTF, a 3D voice keeps the pan's distance, the direct
	path's occlusion (its low pass), the room send and the Doppler shift:
	8 units away 12 dB under 2 (its minimum distance 1), as panned; the
	high frequencies of an occluded voice as far down as panned; the same
	send to the reverb; a 1 kHz sine coming at 10 units a second heard
	343 / (343 - 30.5) times as high */
	CASE("hrtf-paths")
	{
		unsigned long frames = 24000;
		short *samples = noise_samples(frames + 4096);
		float *output = malloc((frames + CHUNK) * 2 * sizeof(float));
		double near_db[2], far_db[2], open_db[2], muffled_db[2], send_power[2];
		int through;

		hrtf_initialize();
		listener_reset();
		for (through = 0; through < 2; through++)
		{
			struct sdl_stream *stream;
			static float buffer[CHUNK * 2], send[CHUNK];
			unsigned long chunk, n;

			hrtf_enabled = through;
			stream = voice_placed(samples, frames + 4096, 48000, -2.0f, 0.0f, 0.0f);
			render(stream, output, frames, TRUE);
			near_db[through] = ears_power(output + CHUNK * 2, frames - CHUNK);
			voice_delete(stream);
			stream = voice_placed(samples, frames + 4096, 48000, -8.0f, 0.0f, 0.0f);
			render(stream, output, frames, TRUE);
			far_db[through] = ears_power(output + CHUNK * 2, frames - CHUNK);
			open_db[through] = ear_band(output + CHUNK * 2, frames - CHUNK, 0, 12000.0) -
				ear_band(output + CHUNK * 2, frames - CHUNK, 0, 500.0);
			voice_delete(stream);
			stream = voice_placed(samples, frames + 4096, 48000, -8.0f, 0.0f, 0.0f);
			stream->direct_hf = -2000;
			render(stream, output, frames, TRUE);
			muffled_db[through] = ear_band(output + CHUNK * 2, frames - CHUNK, 0, 12000.0) -
				ear_band(output + CHUNK * 2, frames - CHUNK, 0, 500.0);
			voice_delete(stream);
			/* (the send, indoors) */
			environment.lRoom = -1000;
			stream = voice_placed(samples, frames + 4096, 48000, -3.0f, 0.0f, 1.0f);
			stream->room = 0;
			stream->room_hf = -300;
			send_power[through] = 0.0;
			for (chunk = 0; chunk < 20; chunk++)
			{
				memset(buffer, 0, sizeof(buffer));
				memset(send, 0, sizeof(send));
				mix_voice(stream, buffer, send, CHUNK);
				for (n = 0; n < CHUNK; n++)
					send_power[through] += (double)send[n] * send[n];
			}
			environment.lRoom = DSBVOLUME_MIN;
			voice_delete(stream);
		}
		hrtf_enabled = FALSE;
		for (through = 0; through < 2; through++)
		{
			CHECK(fabs(far_db[through] - near_db[through] + 12.04) < 0.3, "%s, 8 units away %+.2f dB of 2", through ?
				"through the HRTF" : "panned", far_db[through] - near_db[through]);
		}
		CHECK(fabs((muffled_db[1] - open_db[1]) - (muffled_db[0] - open_db[0])) < 1.0 && muffled_db[1] - open_db[1] < -6.0,
			"occluded, the high frequencies %+.1f dB through the HRTF, %+.1f dB panned", muffled_db[1] - open_db[1],
			muffled_db[0] - open_db[0]);
		CHECK(send_power[0] > 0.0 && send_power[1] == send_power[0], "the room send through the HRTF %g, panned %g",
			send_power[1], send_power[0]);
		free(output);
		free(samples);
		{
			float ratio;
			double rest_db;

			resampler_mode = RESAMPLER_SINC;
			hrtf_enabled = TRUE;
			doppler_figures(-10.0f, 0.0f, DS3DMODE_NORMAL, 343.0 / (343.0 - 30.48), &ratio, &rest_db);
			hrtf_enabled = FALSE;
			listener_reset();
			CHECK(fabs(ratio - 343.0 / (343.0 - 30.48)) < 1.0e-5 && rest_db < -40.0, "through the HRTF, coming at 10 "
				"units a second: shifted %.5f, the rest %.1f dB", ratio, rest_db);
		}
		return 0;
	}
	/* the choice changing as a 3D voice plays (headphones plugged in, then
	out): a 100 Hz sine to the left crossfades from the pan to the HRTF and
	back, with no step: no frame moves further from the last than the
	sine does at the louder of the two (no click) */
	CASE("hrtf-switch")
	{
		unsigned long frames = 48000 * 3, n, chunk;
		short *samples = malloc((frames + 4096) * sizeof(short));
		/* (whole mixes of CHUNK: the last runs past frames) */
		float *output = malloc((frames + CHUNK) * 2 * sizeof(float));
		struct sdl_stream *stream;
		double steady[2] = { 0.0, 0.0 }, switching[2] = { 0.0, 0.0 };
		int ear;

		for (n = 0; n < frames + 4096; n++)
			samples[n] = (short)lround(16000.0 * sin(2.0 * PI * 100.0 * n / 48000.0));
		hrtf_initialize();
		listener_reset();
		stream = voice_placed(samples, frames + 4096, 48000, -3.0f, 0.0f, 0.5f);
		/* (each switch a quarter of a cycle past a zero crossing) */
		for (chunk = 0; chunk * CHUNK < frames; chunk++)
		{
			static float buffer[CHUNK * 2], send[CHUNK];

			hrtf_enabled = chunk >= 94 && chunk < 188;
			memset(buffer, 0, sizeof(buffer));
			memset(send, 0, sizeof(send));
			mix_voice(stream, buffer, send, CHUNK);
			memcpy(output + chunk * CHUNK * 2, buffer, sizeof(buffer));
		}
		hrtf_enabled = FALSE;
		for (n = CHUNK + 1; n < frames; n++)
		{
			/* (the switches and their crossfades, and where each is still) */
			BOOL near_switch = (n >= 94 * CHUNK - 16 && n < 94 * CHUNK + HRTF_FADE_FRAMES + 2 * CHUNK) ||
				(n >= 188 * CHUNK - 16 && n < 188 * CHUNK + HRTF_FADE_FRAMES + 2 * CHUNK);

			for (ear = 0; ear < 2; ear++)
			{
				double step = fabs(output[n * 2 + ear] - output[(n - 1) * 2 + ear]);

				if (near_switch)
					switching[ear] = step > switching[ear] ? step : switching[ear];
				else
					steady[ear] = step > steady[ear] ? step : steady[ear];
			}
		}
		for (ear = 0; ear < 2; ear++)
		{
			CHECK(steady[ear] > 0.0 && switching[ear] < 1.05 * steady[ear], "the %s ear: a step of %.5f while the choice "
				"changes, %.5f at most otherwise (a click)", ear ? "right" : "left", switching[ear], steady[ear]);
		}
		voice_delete(stream);
		free(output);
		free(samples);
		return 0;
	}
	/* 2D voices (the menus, the music, the HUD), stereo voices placed in
	the world and 3D voices with 3D off are as they were through the HRTF:
	the same to the bit; a mono 3D voice is not (the HRTF is on) */
	CASE("dry-2d")
	{
		unsigned long frames = 24000, kind;
		short *samples = noise_samples(frames * 2 + 4096);
		float *panned = malloc(frames * 2 * sizeof(float)), *through = malloc(frames * 2 * sizeof(float));

		hrtf_initialize();
		listener_reset();
		for (kind = 0; kind < 5; kind++)
		{
			int pass;
			BOOL same = TRUE;

			for (pass = 0; pass < 2; pass++)
			{
				struct sdl_stream *stream;

				if (kind == 0)
				{
					/* a 2D stereo voice (music) */
					stream = voice_new(samples, frames + 2048, 2, 44100, 0);
				}
				else if (kind == 1)
				{
					/* a 2D mono voice (the HUD's, a menu's) */
					stream = voice_new(samples, frames + 2048, 1, 22050, 0);
					stream->mix_left = 0.8f;
					stream->mix_right = 0.5f;
				}
				else if (kind == 2)
				{
					/* a stereo voice placed in the world */
					stream = voice_new(samples, frames + 2048, 2, 22050, 0);
					stream->stereo_positioned = TRUE;
					stream->stereo_pan = -0.6f;
				}
				else if (kind == 3)
				{
					/* a 3D voice with 3D off */
					stream = voice_placed(samples, frames + 2048, 22050, -3.0f, 1.0f, 0.0f);
					stream->mode = DS3DMODE_DISABLE;
				}
				else
				{
					/* a mono 3D voice: the HRTF's */
					stream = voice_placed(samples, frames + 2048, 22050, -3.0f, 1.0f, 0.0f);
				}
				hrtf_enabled = pass;
				render(stream, pass ? through : panned, frames, TRUE);
				hrtf_enabled = FALSE;
				voice_delete(stream);
			}
			for (index = 0; index < frames * 2; index++)
				same = same && through[index] == panned[index];
			CHECK(same == (kind != 4), "voice %lu (0 2D stereo, 1 2D mono, 2 stereo placed, 3 3D off, 4 mono 3D) "
				"%s through the HRTF", kind, same ? "the same" : "changed");
		}
		free(samples);
		free(panned);
		free(through);
		return 0;
	}
	/* audio.volume and a voice's own volume (SetVolume) scale every voice
	alike, the 3D voices through the HRTF as well as the 2D ones: each
	voice at a fifth of both is 2 x 13.98 dB down */
	/* Settings > Audio's SOUND (audio.enabled) as the game plays: off fades
	the output to silence over SOUND_FADE_FRAMES and keeps it silent, on
	fades it back and then leaves it as mixed; a device is opened for it
	once a change of the settings turns it on with none open */
	CASE("sound-off")
	{
		static float output[CHUNK * 2];
		unsigned long frame;
		float gain = 1.0f;

		for (frame = 0; frame < CHUNK * 2; frame++)
			output[frame] = 0.5f;
		gain = sound_fade(output, CHUNK, 2, gain, FALSE);
		CHECK(output[0] < 0.5f && output[0] > 0.49f, "the fade's first frame: %f", output[0]);
		CHECK(output[2 * (SOUND_FADE_FRAMES / 2)] > 0.2f && output[2 * (SOUND_FADE_FRAMES / 2)] < 0.3f,
			"half way: %f", output[2 * (SOUND_FADE_FRAMES / 2)]);
		for (frame = SOUND_FADE_FRAMES; frame < CHUNK; frame++)
			CHECK(output[frame * 2] == 0.0f && output[frame * 2 + 1] == 0.0f, "frame %lu after the fade", frame);
		CHECK(gain == 0.0f, "the gain off: %f", gain);
		for (frame = 0; frame < CHUNK * 2; frame++)
			output[frame] = 0.5f;
		gain = sound_fade(output, CHUNK, 2, gain, FALSE);
		for (frame = 0; frame < CHUNK * 2; frame++)
			CHECK(output[frame] == 0.0f, "off, sample %lu: %f", frame, output[frame]);
		for (frame = 0; frame < CHUNK * 2; frame++)
			output[frame] = 0.5f;
		gain = sound_fade(output, CHUNK, 2, gain, TRUE);
		CHECK(output[0] > 0.0f && output[0] < 0.01f, "fading in: %f", output[0]);
		CHECK(output[2 * (CHUNK - 1)] == 0.5f && gain == 1.0f, "faded in: %f, gain %f", output[2 * (CHUNK - 1)], gain);
		for (frame = 0; frame < CHUNK * 2; frame++)
			output[frame] = 0.5f;
		gain = sound_fade(output, CHUNK, 2, gain, TRUE);
		for (frame = 0; frame < CHUNK * 2; frame++)
			CHECK(output[frame] == 0.5f, "on, sample %lu: %f", frame, output[frame]);
		/* (the device: turned on with none open, once a change; not when
		paced by the frames, before the start, already open, off) */
		CHECK(sound_device_wanted(TRUE, FALSE, TRUE, FALSE, 4, 5), "turned on: no device opened");
		CHECK(!sound_device_wanted(TRUE, FALSE, TRUE, FALSE, 5, 5), "opened again with no change");
		CHECK(!sound_device_wanted(TRUE, TRUE, TRUE, FALSE, 4, 5), "a paced mix's device");
		CHECK(!sound_device_wanted(FALSE, FALSE, TRUE, FALSE, 4, 5), "before the start");
		CHECK(!sound_device_wanted(TRUE, FALSE, TRUE, TRUE, 4, 5), "one open already");
		CHECK(!sound_device_wanted(TRUE, FALSE, FALSE, FALSE, 4, 5), "a device for no sound");
		return 0;
	}
	CASE("master-volume")
	{
		unsigned long frames = 24000, kind;
		short *samples = noise_samples(frames * 2 + 4096);
		float *output = malloc(frames * 2 * sizeof(float));

		hrtf_initialize();
		listener_reset();
		for (kind = 0; kind < 5; kind++)
		{
			double power_db[2];
			int pass;

			for (pass = 0; pass < 2; pass++)
			{
				struct sdl_stream *stream;

				if (kind == 0)
					stream = voice_new(samples, frames + 2048, 2, 44100, 0);
				else if (kind == 1)
					stream = voice_new(samples, frames + 2048, 1, 22050, 0);
				else if (kind == 2)
				{
					stream = voice_new(samples, frames + 2048, 2, 22050, 0);
					stream->stereo_positioned = TRUE;
					stream->stereo_pan = -0.6f;
				}
				else
					stream = voice_placed(samples, frames + 2048, 22050, -3.0f, 1.0f, 0.0f);
				hrtf_enabled = kind == 4;
				master_volume = pass ? 0.2f : 1.0f;
				stream->volume = pass ? 0.2f : 1.0f;
				render(stream, output, frames, TRUE);
				power_db[pass] = ears_power(output + CHUNK * 2, frames - CHUNK);
				voice_delete(stream);
			}
			hrtf_enabled = FALSE;
			master_volume = 1.0f;
			CHECK(fabs(power_db[1] - power_db[0] + 27.96) < 0.05, "voice %lu (0 2D stereo, 1 2D mono, 2 stereo placed, "
				"3 3D panned, 4 3D through the HRTF) at a fifth of the master volume and its own: %+.2f dB", kind,
				power_db[1] - power_db[0]);
		}
		free(samples);
		free(output);
		return 0;
	}
	/* "circle <file.wav> hrtf|pan": a sound going round the listener, to
	listen to (circle) */
	CASE("circle")
	{
		if (argc < 4)
		{
			fprintf(stderr, "circle <file.wav> hrtf|pan\n");
			return 2;
		}
		return circle(argv[2], !strcmp(argv[3], "hrtf"));
	}
	CASE("metrics")
	{
		static const double tones[] = { 100, 1000, 5000, 8000, 10000, 10500, 15000, 18000, 20000, 21000 };
		double *signal = noise(30000), *impulse = calloc(4000, sizeof(double));

		impulse[2000] = 1.0;
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			unsigned long rate = rates[index][0], frequency = rates[index][1], tone;
			double error_db, peak_db, impulse_db, impulse_peak_db;

			against_reference(signal, 30000, rate, frequency, &error_db, &peak_db);
			against_reference(impulse, 4000, rate, frequency, &impulse_db, &impulse_peak_db);
			printf("%lu Hz played at %lu: against the exact low pass, noise %.1f dB (peak %.1f), impulse peak %.1f dB\n",
				rate, frequency ? frequency : rate, error_db, peak_db, impulse_peak_db);
			for (tone = 0; tone < sizeof(tones) / sizeof(tones[0]); tone++)
			{
				double gain_db, rest_db;

				if (tones[tone] >= 0.5 * rate)
					continue;
				sine_figures(tones[tone], rate, frequency, &gain_db, &rest_db);
				printf("  %6.0f Hz: gain %+.3f dB, the rest %.1f dB\n", tones[tone], gain_db, rest_db);
			}
			if (!frequency)
				printf("  sweep to 0.9 of its band: %.1f dB from ideal\n", sweep_figure(rate, 0.45 * rate));
		}
		free(signal);
		free(impulse);
		return 0;
	}
	/* noise, mono and stereo, at each rate through the mixer: the output as
	raw floats on stdout, to compare one mixer with another */
	CASE("render")
	{
		static float mixed[CHUNK * 2];
		double *signal = noise(60000);
		short *samples = malloc(30000 * 2 * sizeof(short));
		double *exact = malloc(60000 * sizeof(double));
		float *output = calloc(40000 * 2, sizeof(float));

		quantize(signal, samples, exact, 60000);
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			unsigned long channels;

			for (channels = 1; channels <= 2; channels++)
			{
				struct sdl_stream *stream = voice_new(samples, 30000, channels, rates[index][0], rates[index][1]);

				render(stream, output, 40000, TRUE);
				fwrite(output, sizeof(float), 40000 * 2, stdout);
				voice_delete(stream);
			}
		}
		/* and a scene's whole mix, reverb and limiter, its voices out of
		earshot coming into it halfway */
		scene(24, 8, TRUE, 3.0);
		for (index = 0; index < 2 * OUTPUT_RATE / CHUNK; index++)
		{
			if (index == OUTPUT_RATE / CHUNK)
				scene_approach();
			mix(mixed, CHUNK, TRUE);
			fwrite(mixed, sizeof(float), CHUNK * 2, stdout);
		}
		return 0;
	}
	/* the hand-off: while a game thread holds the voices' lock (set aside by
	the scheduler), the device's mix does not wait for it, mixes what it took
	last, and takes the game's change at the next mix */
	CASE("hand-off-never-waits")
	{
		static float mixed[CHUNK * 2];
		static short samples[48000];
		struct sdl_stream *stream = voice_new(samples, 48000, 1, 48000, 0);
		pthread_t holder;
		struct timespec start, end;
		double waited;

		streams = stream;
		mix(mixed, CHUNK, FALSE);
		pthread_mutex_lock(&hand_off_test.lock);
		pthread_create(&holder, NULL, hand_off_holder, NULL);
		while (!hand_off_test.holding)
			pthread_cond_wait(&hand_off_test.condition, &hand_off_test.lock);
		pthread_mutex_unlock(&hand_off_test.lock);
		/* (the game sets the voice's volume as it holds the lock) */
		stream->pending = (struct voice_settings){ 0 };
		stream->pending.frequency = 48000;
		stream->pending.volume = 0.25f;
		stream->pending_changed = TRUE;
		clock_gettime(CLOCK_MONOTONIC, &start);
		mix(mixed, CHUNK, FALSE);
		clock_gettime(CLOCK_MONOTONIC, &end);
		waited = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
		CHECK(waited < 0.1, "the mix waited %.0f ms for the game's thread", waited * 1000.0);
		CHECK(hand_offs_skipped == 1 && stream->volume == 1.0f, "the mix took the game's side while it was held");
		pthread_join(holder, NULL);
		mix(mixed, CHUNK, FALSE);
		CHECK(stream->volume == 0.25f && !stream->pending_changed, "the next mix did not take the game's change");
		streams = NULL;
		voice_delete(stream);
		return 0;
	}
	/* the hand-off takes every setting the game gives a voice to the mixer's
	side, and nothing else: the game's side all one byte, the mixer's that
	byte in as many bytes */
	CASE("hand-off-settings")
	{
		struct sdl_stream *stream = calloc(1, sizeof(*stream));
		const unsigned char *bytes = (const unsigned char *)stream;
		size_t offset, taken = 0;

		memset(&stream->pending, 0x5a, sizeof(stream->pending));
		stream->pending_changed = TRUE;
		streams = stream;
		voices_hand_off(TRUE);
		for (offset = 0; offset < sizeof(*stream); offset++)
		{
			if (offset >= offsetof(struct sdl_stream, pending) &&
				offset < offsetof(struct sdl_stream, pending) + sizeof(stream->pending))
			{
				continue;
			}
			taken += bytes[offset] == 0x5a;
		}
		CHECK(taken == sizeof(struct voice_settings), "%zu bytes of the settings taken, of %zu", taken,
			sizeof(struct voice_settings));
		streams = NULL;
		free(stream);
		return 0;
	}
	/* "bench [voices] [out of earshot] [indoor|outdoor] [hrtf]": through the
	HRTF, the 3D voices' */
	CASE("bench")
	{
		hrtf_initialize();
		hrtf_enabled = argc > 5 && !strcmp(argv[5], "hrtf");
		bench(argc > 2 ? strtoul(argv[2], NULL, 0) : 24, argc > 3 ? strtoul(argv[3], NULL, 0) : 16,
			argc > 4 && !strcmp(argv[4], "indoor"), 10.0);
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
