/*
DSOUND_SDL.C

Xbox DirectSound for the Linux build: a software mixer on an SDL3 audio
stream.

The game plays everything through DirectSound streams: 16-bit stereo PCM
(music and other uncompressed sounds) and Xbox ADPCM, mono or stereo, at 22
or 44 kHz. A packet is decoded to 16-bit PCM when the game submits it, since
the sound cache may reuse its memory once the packet completes. The mixer
runs on SDL's audio thread; for every voice it resamples to the output rate
with a windowed sinc low pass (which is how SetFrequency changes pitch;
resampling) and applies:
	- the stream volume (millibels),
	- the front left and right mix bin volumes of 2D voices,
	- for 3D voices, DirectSound's inverse distance rolloff between the
	  minimum and maximum distance, an equal power pan from the source's
	  direction in listener space, and the I3DL2 direct path, obstruction
	  and occlusion levels, their high frequency levels a low pass at the
	  environment's HF reference: a sound behind a wall is muffled;
	- for 3D voices, DirectSound's Doppler shift from the velocities the
	  game gives the voice and the listener (doppler).
3D voices also send to an I3DL2 reverb of the environment the listener is in
(reverb). Cones are not modelled (no sound of the maps has one). A look-ahead
limiter keeps the sum under full scale (limit). On headphones (audio.spatial:
"auto" asks the host what the sound plays on, spatial_hrtf), and in the VR
mode with vr.hrtf, a mono 3D voice goes to each ear through a model of a head
instead of the pan (hrtf), as the Xbox's DirectSound put its 3D voices through
its HRTF; 2D voices (the menus, the music, the HUD) are as they were.

The output is stereo or 5.1 (audio.channels; "auto": 5.1 on a device of
six channels or more, as the host's SDL reports it, output_channels_choose).
For 5.1 the speaker config reports AC-3, as an Xbox set to Dolby Digital
does: the game then sends its 2D voices to the center and back mix bins too
(dsound_initialize_channel: a mono one at half to the front three, a stereo
one whole to the front pair and at half to the back pair), 3D voices and
stereo voices positioned in the world are panned around the five full range
speakers by VBAP (surround_pan), never the HRTF, and the reverb plays from the
front and back pairs. The mix bins 0-5 are in SDL's 5.1 order (FL, FR, FC,
LFE, BL, BR), so bin n is output channel n. The game sends nothing to the
LFE bin. Stereo output mixes exactly as before.

Packets the mixer has finished are completed from DirectSoundDoWork, which
the game calls every frame, and from Flush, never from the audio thread:
the game's completion callback is not meant to run concurrently with it.

The mixer never waits for the game's threads (the hand-off): what the game
sets and submits goes to its own side of each voice, under voices_lock, and
the mixer takes it over to its side at the start of each mix, if the lock is
free; if a game thread holds it, the mixer mixes what it took last time and
takes the changes at the next mix.

Without an audio device, a clock thread runs the same mixer into a scratch
buffer, so streams still drain at their real rate.

audio.volume sets the master volume (default 1.0); audio.reverb = false
turns the reverb off, and audio.doppler = false the Doppler shift;
audio.spatial chooses the HRTF or the pan ("auto", "headphones", "speakers");
audio.resampler = "linear" resamples between neighbouring frames instead of
with the sinc low pass; audio.enabled = false skips opening a device;
audio.channels chooses stereo or 5.1
(port_config.c). For automated tests, debug.audio_record records the mix to a
WAV file (its front pair, on 5.1) and debug.audio_log logs the 3D voices' places
and gains (record, voices_log).
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "voice_audio.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2 /* the stereo mix's, and the reverb's outputs */
#define MAXIMUM_OUTPUT_CHANNELS 6
#define MAXIMUM_STREAM_PACKETS 64
#define MIX_CHUNK_FRAMES 1024

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64

/* the resampler (resampling) */
#define RESAMPLER_ZERO_CROSSINGS 24
#define RESAMPLER_TABLE_STEPS 256
#define RESAMPLER_CUTOFF 0.96
#define RESAMPLER_KAISER_BETA 6.5
#define RESAMPLER_MAXIMUM_STRETCH 2
#define RESAMPLER_HISTORY 128

/* the Doppler shift (doppler): the speed of sound (metres a second), the
Doppler factor (DirectSound's default, which the game keeps), and the most a
voice is shifted either way (an octave) */
#define DOPPLER_SPEED_OF_SOUND 343.0f
#define DOPPLER_FACTOR 1.0f
#define DOPPLER_MAXIMUM_RATIO 2.0f
/* the time (seconds) a voice's shift takes to move most of the way (1 -
1/e) to a new one: a velocity's change at a tick, a projectile's bounce,
is a glide, not a step of pitch */
#define DOPPLER_SMOOTHING_SECONDS 0.08f

/* the HRTF (hrtf): the frames a voice keeps for its ears' delays, the head's
radius (metres) and the speed of sound (metres a second), the head shadow's
alpha over all directions (hrtf_initialize), the distance within which a
sound's direction fades out (world units), the pinna's notch (its frequency
from below the ears to above them, its depth and its Q) and the shadow of
the pinna behind (its frequency and its depth) */
#define HRTF_FRAMES 64
#define HRTF_HEAD_RADIUS 0.0875f
#define HRTF_SPEED_OF_SOUND 343.0f
#define HRTF_DIFFUSE_ALPHA 1.03f
#define HRTF_NEAR 0.25f
#define HRTF_NOTCH_LOW 6000.0f
#define HRTF_NOTCH_HIGH 11000.0f
#define HRTF_NOTCH_DECIBELS -10.0f
#define HRTF_NOTCH_Q 2.0f
#define HRTF_REAR_FREQUENCY 2500.0f
#define HRTF_REAR_DEPTH 0.55f
/* the crossfade between the pan and the HRTF when the choice changes (the
headphones plugged in or out, audio.spatial): 100 ms */
#define HRTF_FADE_FRAMES 4800
#if defined(__GNUC__) || defined(__clang__)
#define RESAMPLER_ALIGNED __attribute__((aligned(16)))
#else
#define RESAMPLER_ALIGNED
#endif

/* ---------- voices */

struct voice_packet
{
	XMEDIAPACKET packet;
	short *samples;           /* interleaved, source channel count */
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
	BOOL restart;             /* submitted to a stream with none: it starts over (stream_process) */
};

/* a mono 3D voice's way to each ear through the HRTF (hrtf_place), which
the mixer ramps from one mix to the next as it does the gains */
struct hrtf_parameters
{
	/* each ear's delay (frames; left, right), and its head shadow, a one-pole
	one-zero filter's zero (its pole is the same for all: hrtf) */
	float delay[2];
	float shadow_b0[2], shadow_b1[2];
	/* the pinna's notch (a peaking filter's b0, b1, b2, a1, a2) at its
	frequency, and how much of the high frequencies a sound behind loses
	(0 to 1) */
	float notch[5];
	float notch_frequency;
	float rear;
	/* each ear's gain: the pan's in the middle (-3 dB), with the voice's
	distance and direct path, the head's filters doing the rest */
	float gain;
};

/* what the game sets on a voice, on its side (the stream's pending), which
the mixer takes over to its own, the stream's fields of the same names
(voice_settings_take) */
struct voice_settings
{
	BOOL paused;
	DWORD frequency;
	float volume;
	float mix_left, mix_right;
	BOOL stereo_positioned;
	float stereo_pan, stereo_distance, stereo_distance_fade;
	float surround_bins[2][MAXIMUM_OUTPUT_CHANNELS];
	DWORD mode;
	float position[3], velocity[3];
	float minimum_distance, maximum_distance;
	LONG direct, direct_hf, room, room_hf;
	float room_rolloff_factor;
};

/* a voice's flush (sdl_stream) */
#define VOICE_FLUSHING 1
#define VOICE_FLUSHED 2

/* A voice has two sides (the hand-off). The game's, under voices_lock: next,
the reference count, the settings it gives (pending), and its packets
(packets, packet_head and packet_count: submitted and not yet completed,
submitted counting them all, flush set by Flush). The mixer's: the rest, the
settings and the packets as it took them (mixing) at its last hand-off. The
format does not change once the voice is made. */
struct sdl_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct sdl_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* (voices_lock) the game's side */
	struct voice_settings pending;
	BOOL pending_changed;
	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	unsigned long submitted;
	/* VOICE_FLUSHING while Flush completes the packets (the mixer takes none
	meanwhile), VOICE_FLUSHED once it has: the mixer lets go of those it holds
	and starts over */
	int flush;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	/* a stereo voice of a sound in the world: panned towards it, -1 left
	to 1 right, its distance, and the fade of its volume with distance that
	the game made (dsound_sdl_stream_set_stereo_position) */
	BOOL stereo_positioned;
	float stereo_pan;
	float stereo_distance;
	float stereo_distance_fade;
	float headroom;
	/* on 5.1 (output_channels 6): the gains from each source channel to
	each speaker mix bin (SetMixBins, SetMixBinVolumes) */
	float surround_bins[2][MAXIMUM_OUTPUT_CHANNELS];

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	/* (units a second; SetVelocity) */
	float velocity[3];
	float minimum_distance, maximum_distance;
	/* the I3DL2 direct path's level and high frequency level, in millibels
	with obstruction and occlusion (SetI3DL2Source) */
	LONG direct, direct_hf;
	/* the I3DL2 room (reverb) send's level and high frequency level, in
	millibels with occlusion, and its rolloff factor (SetI3DL2Source) */
	LONG room, room_hf;
	float room_rolloff_factor;

	/* the voices the mixer mixes, as it took them (mixing_streams) */
	struct sdl_stream *mixing_link;
	/* the packets it holds: those submitted that the game had not completed
	at its last hand-off, the finished ones too, and the count of the
	packets submitted up to the last it holds */
	struct voice_packet mixing[MAXIMUM_STREAM_PACKETS];
	unsigned long mixing_head;
	unsigned long mixing_count;
	unsigned long mixing_next;
	/* the next frame to take from the first packet not finished */
	unsigned long cursor;
	/* the resampler's (resampler_reset): the last RESAMPLER_HISTORY frames
	taken, each channel's apart and each frame twice, RESAMPLER_HISTORY apart,
	so that the frames a low pass reaches are side by side however the ring
	wraps (resampler_dot); how many were taken, the frame the output is at and
	how far past it, and the frames of silence taken since the packets ran
	out */
	float history[2][2 * RESAMPLER_HISTORY];
	unsigned long history_count;
	unsigned long center;
	double phase;
	unsigned long silence;
	/* gains and low pass coefficients the mixer is ramping from, to avoid
	clicks, and the step through the frames (the frequency, Doppler shifted,
	over the output rate) */
	float current_left, current_right, current_room;
	double current_step;
	/* the Doppler shift heard, moving towards doppler_ratio's (doppler) */
	float current_doppler;
	float current_direct_lowpass, current_room_lowpass;
	float current_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	BOOL gains_valid;
	/* the direct path's low pass, for each channel, and the room send's */
	float direct_lowpass[2];
	float room_lowpass;
	/* through the HRTF (hrtf): whether its filters ran in the last mix (their
	states are the voice's), how much of the voice they play (0 the pan's to
	1 theirs, crossfaded over HRTF_FADE_FRAMES as the choice changes), the
	parameters the mixer is ramping from, the voice's last HRTF_FRAMES frames
	as the ears take them (its direct path's, notched; the oldest first),
	and the filters' states: the rear's low pass, the notch's last two
	inputs and outputs, each ear's shadow's last input and output */
	BOOL hrtf_on;
	float hrtf_mix;
	struct hrtf_parameters hrtf_current;
	float hrtf_frames[HRTF_FRAMES];
	float hrtf_rear;
	float hrtf_notch[4];
	float hrtf_shadow[2][2];
};

/* the game's side of the voices (the hand-off), which the mixer only tries,
and the game's voices, the newest first; the mixer's, as it took them */
static pthread_mutex_t voices_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sdl_stream *streams;
static struct sdl_stream *mixing_streams;

/* the listener, in DirectSound's left-handed +y up space: the mixer's, and
the game's (voices_lock), which the mixer takes when it has changed */
static struct listener_state
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	/* meters a unit (SetDistanceFactor): the game sets 3.048, a world unit
	being 10 feet. Only the Doppler shift uses it (doppler) */
	float distance_factor;
	/* (units a second; the game gives each voice's velocity relative to the
	listener, and the listener none) */
	float velocity[3];
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f, { 0, 0, 0 } },
	listener_set = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f, { 0, 0, 0 } };
static BOOL listener_changed;

static float master_volume = 1.0f;
/* audio.doppler */
static BOOL doppler_enabled = TRUE;
/* whether mono 3D voices go through the HRTF: on headphones, audio.spatial;
in the VR mode, vr.hrtf (spatial_hrtf, DirectSoundDoWork) */
static BOOL hrtf_enabled = FALSE;
/* 2 or 6 (audio.channels), chosen when the device first opens; the game
sets its mix bins by it, so it holds while the game runs */
static unsigned long output_channels = 2;

/* the I3DL2 listener properties (SetI3DL2Listener), which the reverb models:
DirectSound's defaults until the game sets an environment, their room level
leaving it silent; serial counts the changes. The mixer's, and the game's
(voices_lock), which the mixer takes when it has changed */
static DSI3DL2LISTENER environment =
{
	DSBVOLUME_MIN, 0, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f
};
static unsigned long environment_serial;
static DSI3DL2LISTENER environment_set =
{
	DSBVOLUME_MIN, 0, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f
};
static BOOL environment_changed;
/* audio.reverb */
static BOOL reverb_enabled = TRUE;

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* value, or 0 once it is far too small to hear: the state of a filter or of
the reverb fed silence decays into the denormals, which many CPUs work out
slowly, and neither x86 nor ARM flushes them to zero by default */
static float flush_denormal(float value)
{
	return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

/* cos(2 pi f / OUTPUT_RATE) of a filter's frequency f, within what the
output rate holds */
static float frequency_cosine(float frequency)
{
	if (frequency < 20.0f)
		frequency = 20.0f;
	if (frequency > 0.45f * OUTPUT_RATE)
		frequency = 0.45f * OUTPUT_RATE;
	return cosf(2.0f * 3.14159265f * frequency / OUTPUT_RATE);
}

/* the coefficient a of the one-pole low pass y = x + a (y' - x) whose gain is
gain (at most 1) at the frequency of cosine (frequency_cosine) */
static float lowpass_coefficient(float gain, float cosine)
{
	float a, b;

	if (gain >= 1.0f)
		return 0.0f;
	if (gain < 0.001f)
		gain = 0.001f;
	a = 1.0f - gain * gain;
	b = 1.0f - gain * gain * cosine;
	return (b - sqrtf(b * b - a * a)) / a;
}

/* ---------- decoding */

static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

static int ima_expand(int nibble, int *predictor, int *index)
{
	int step = ima_step_table[*index];
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += ima_index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
}

/* Xbox ADPCM: per block, a 4-byte header per channel (first sample, step
index), then 4-byte groups of eight nibbles, low nibble first, alternating
between channels; 64 samples per channel. The header's sample is the block's
first, and the nibbles code the 63 after it: the 64th nibble only pads the
block (the maps' sounds always have 0 there), and decoding it in place of the
header's sample put a wrong sample in every 64, a buzz at 344 Hz in 22 kHz
sounds. */
static short *decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	short *samples = malloc((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * channels * sizeof(short));
	unsigned long block, channel;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		short *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			unsigned long group, byte;

			output[channel] = (short)predictor;
			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					/* nibble n codes sample n + 1 */
					unsigned long sample = group * 8 + byte * 2 + 1;

					output[sample * channels + channel] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
					if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)ima_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
	*frame_count = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
	return samples;
}

static short *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	short *samples = malloc((frames ? frames : 1) * channels * sizeof(short));

	if (samples)
		memcpy(samples, source, frames * channels * sizeof(short));
	*frame_count = samples ? frames : 0;
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* DirectSound's inverse distance law, held beyond the maximum distance. The
game sets a voice's maximum distance before its minimum, so the maximum can
be under the minimum for a moment: it is held at the minimum */
static float distance_attenuation(const struct sdl_stream *stream, float distance, float rolloff_factor)
{
	float minimum = stream->minimum_distance;
	float maximum = stream->maximum_distance > minimum ? stream->maximum_distance : minimum;

	if (minimum <= 0.0f || distance <= minimum)
		return 1.0f;
	if (distance > maximum)
		distance = maximum;
	return minimum / (minimum + rolloff_factor * (distance - minimum));
}

/* ---------- Doppler

The game gives each 3D voice its velocity (units a second, relative to the
listener: sound_manager.c update_channels) and the Xbox's DirectSound shifted
its pitch by them, as a sound passing by is heard: a Banshee, a Ghost or a
plasma bolt rising as it comes and falling as it goes. DirectSound's shift:
the frequency times (c + D v_listener) / (c + D v_source), c the speed of
sound, D the Doppler factor, the velocities' parts along the line from the
listener to the source in metres a second (the distance factor's metres a
unit), the source's away from the listener, the listener's towards the
source. Each part is held within half the speed of sound, and the shift to
an octave either way, so that a projectile passing close by, or a velocity
that jumps (a teleport), takes no voice up out of hearing. The mixer moves
each voice's shift towards the one its velocities give over
DOPPLER_SMOOTHING_SECONDS, so that a velocity that changes in steps (once a
tick, or the frames' share of a tick's movement) glides the pitch rather than
warbling it (doppler_smoothed). */

static float doppler_ratio(const struct sdl_stream *stream)
{
	float offset[3], distance, source_speed, listener_speed = 0.0f, ratio, limit = 0.5f * DOPPLER_SPEED_OF_SOUND;
	int axis;

	if (!doppler_enabled || !stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		offset[axis] = stream->position[axis] -
			(stream->mode == DS3DMODE_HEADRELATIVE ? 0.0f : listener.position[axis]);
	}
	distance = sqrtf(dot3(offset, offset));
	if (distance < 1.0e-4f)
		return 1.0f;
	source_speed = DOPPLER_FACTOR * listener.distance_factor * dot3(stream->velocity, offset) / distance;
	/* (head-relative: the voice's velocity is the listener's already) */
	if (stream->mode != DS3DMODE_HEADRELATIVE)
		listener_speed = DOPPLER_FACTOR * listener.distance_factor * dot3(listener.velocity, offset) / distance;
	source_speed = source_speed < -limit ? -limit : source_speed > limit ? limit : source_speed;
	listener_speed = listener_speed < -limit ? -limit : listener_speed > limit ? limit : listener_speed;
	ratio = (DOPPLER_SPEED_OF_SOUND + listener_speed) / (DOPPLER_SPEED_OF_SOUND + source_speed);
	if (ratio > DOPPLER_MAXIMUM_RATIO)
		ratio = DOPPLER_MAXIMUM_RATIO;
	if (ratio < 1.0f / DOPPLER_MAXIMUM_RATIO)
		ratio = 1.0f / DOPPLER_MAXIMUM_RATIO;
	return ratio;
}

/* the shift a voice plays in a mix of frames: its doppler_ratio approached
as a one-pole low pass, from where it was at the voice's last mix, or at
once for a voice starting (its gains not valid) */
static float doppler_smoothed(struct sdl_stream *stream, unsigned long frames)
{
	float target = doppler_ratio(stream);

	if (!stream->gains_valid)
		stream->current_doppler = target;
	else
		stream->current_doppler += (target - stream->current_doppler) *
			(1.0f - expf(-(float)frames / (DOPPLER_SMOOTHING_SECONDS * OUTPUT_RATE)));
	return stream->current_doppler;
}

/* ---------- the HRTF

On headphones (audio.spatial, spatial_hrtf), and in the VR mode (vr.hrtf), a
mono 3D voice reaches each ear as it would a head's, from its direction (the
listener's: the camera's, or in VR the head as it is tracked,
sound_manager.c refresh_listener), rather than panned between them: a
sound behind or above is told from one ahead or below. The Xbox's
DirectSound put its 3D voices through its own HRTF (DirectSoundUseFullHRTF)
and left the 2D ones (the menus, the music, the HUD) dry; so here. Not a
measured HRTF (which would need a dataset of a head's, and a convolution
for each voice and ear) but a model of a head's
(Brown and Duda, "A structural model for binaural sound synthesis", 1998),
a few filters a voice, its parameters from the direction:
	- each ear its own delay, from the time the sound takes around a sphere
	  of a head's size (Woodworth's), to the far ear up to 0.66 ms later;
	- each ear the head's shadow, a one-pole one-zero shelf above about 1
	  kHz, from 6 dB louder facing the sound to 20 dB quieter in the shadow
	  (Brown and Duda's), the louder and quieter about what a sound from
	  everywhere has (the diffuse field: HRTF_DIFFUSE_ALPHA), so that over
	  all directions the sound is no brighter nor duller than panned;
	- the pinna's notch, its frequency rising with the sound from below the
	  ears (6 kHz) to above them (11 kHz), as a pinna's first notch does
	  (it is what tells up from down);
	- a sound behind loses some of its high frequencies (the pinna's shadow,
	  up to 7 dB above 2.5 kHz).
A sound close to the head (within HRTF_NEAR) fades to the middle, as the pan
centres one within its minimum distance. The parameters are ramped across
each mix, as the gains are: a turn of the head moves the sound smoothly
from ear to ear, with no step. Distance, the direct path's obstruction and
occlusion (its low pass), the room send and the Doppler shift are the
pan's, before the head's filters. When the choice changes (headphones
plugged in or out mid-game), each voice crossfades between the pan and the
HRTF over HRTF_FADE_FRAMES, both running meanwhile: no click. Speakers,
and 5.1, keep the pan (a head's filters are for a pair of ears). */

static struct
{
	/* the head shadow's pole (a1 of y = b0 x + b1 x' - a1 y', the same for
	every direction) and the bilinear transform's constant over the pole;
	the rear's low pass's coefficient */
	float shadow_a1;
	float shadow_k;
	float rear_lowpass;
} hrtf;

static void hrtf_initialize(void)
{
	/* Brown and Duda's pole, 2c/a, lowered by the diffuse field's alpha:
	the shadow over the diffuse field's (H(alpha) / H(alpha_diffuse)) */
	float pole = 2.0f * HRTF_SPEED_OF_SOUND / (HRTF_HEAD_RADIUS * HRTF_DIFFUSE_ALPHA);

	hrtf.shadow_k = 1.0f / tanf(pole / (2.0f * OUTPUT_RATE));
	hrtf.shadow_a1 = (1.0f - hrtf.shadow_k) / (1.0f + hrtf.shadow_k);
	hrtf.rear_lowpass = expf(-2.0f * 3.14159265f * HRTF_REAR_FREQUENCY / OUTPUT_RATE);
}

/* the HRTF's parameters for a sound right, up and ahead of the head, as
far away (world units) */
static void hrtf_place(float right, float up, float ahead, float distance, struct hrtf_parameters *parameters)
{
	float strength = distance < HRTF_NEAR ? distance / HRTF_NEAR : 1.0f;
	float lateral = 0.0f, rise = 0.0f, front = 1.0f, delay[2], elevation, frequency, a, w, alpha, a0;
	int ear;

	if (distance > 1.0e-4f)
	{
		lateral = right / distance;
		rise = up / distance;
		front = ahead / distance;
	}
	for (ear = 0; ear < 2; ear++)
	{
		/* the angle from the ear's axis: Brown and Duda's alpha, 2 facing
		the sound, 0.1 at 150 degrees from it */
		float cosine = ear ? lateral : -lateral;
		float theta = acosf(cosine < -1.0f ? -1.0f : (cosine > 1.0f ? 1.0f : cosine));
		float shadow = 1.0f + strength *
			((1.05f + 0.95f * cosf(theta * (180.0f / 150.0f))) / HRTF_DIFFUSE_ALPHA - 1.0f);

		parameters->shadow_b0[ear] = (1.0f + shadow * hrtf.shadow_k) / (1.0f + hrtf.shadow_k);
		parameters->shadow_b1[ear] = (1.0f - shadow * hrtf.shadow_k) / (1.0f + hrtf.shadow_k);
		/* Woodworth's: the way around the head to the ear */
		delay[ear] = theta < 0.5f * 3.14159265f ? 1.0f - cosf(theta) : theta - 0.5f * 3.14159265f + 1.0f;
	}
	/* (the nearer ear at no delay) */
	for (ear = 0; ear < 2; ear++)
	{
		parameters->delay[ear] = strength * (delay[ear] - (delay[0] < delay[1] ? delay[0] : delay[1])) *
			(HRTF_HEAD_RADIUS / HRTF_SPEED_OF_SOUND) * OUTPUT_RATE;
	}
	/* the notch, from 30 degrees below to 60 above (RBJ's peaking filter) */
	elevation = asinf(rise < -1.0f ? -1.0f : (rise > 1.0f ? 1.0f : rise)) * (180.0f / 3.14159265f);
	elevation = (elevation + 30.0f) / 90.0f;
	frequency = HRTF_NOTCH_LOW + (HRTF_NOTCH_HIGH - HRTF_NOTCH_LOW) *
		(elevation < 0.0f ? 0.0f : (elevation > 1.0f ? 1.0f : elevation));
	a = powf(10.0f, strength * HRTF_NOTCH_DECIBELS / 40.0f);
	w = 2.0f * 3.14159265f * frequency / OUTPUT_RATE;
	alpha = sinf(w) / (2.0f * HRTF_NOTCH_Q);
	a0 = 1.0f + alpha / a;
	parameters->notch[0] = (1.0f + alpha * a) / a0;
	parameters->notch[1] = -2.0f * cosf(w) / a0;
	parameters->notch[2] = (1.0f - alpha * a) / a0;
	parameters->notch[3] = parameters->notch[1];
	parameters->notch[4] = (1.0f - alpha / a) / a0;
	parameters->notch_frequency = frequency;
	parameters->rear = front < 0.0f ? -front * strength * HRTF_REAR_DEPTH : 0.0f;
}

/* the frame delay frames before newest (at least 1: one frame each side of
it is read; at most HRTF_FRAMES - 2), interpolated (Catmull-Rom) */
static float hrtf_read(const float *newest, float delay)
{
	unsigned long whole = (unsigned long)delay;
	const float *at = newest - whole;
	float t = delay - (float)whole;
	float newer = at[1];
	float x0 = at[0];
	float x1 = at[-1];
	float older = at[-2];

	return x0 + 0.5f * t * (x1 - newer + t * (2.0f * newer - 5.0f * x0 + 4.0f * x1 - older +
		t * (3.0f * (x0 - x1) + older - newer)));
}

/* parameters moved on by step (a frame's share of a ramp) */
static void hrtf_step(struct hrtf_parameters *parameters, const struct hrtf_parameters *step)
{
	int index;

	for (index = 0; index < 2; index++)
	{
		parameters->delay[index] += step->delay[index];
		parameters->shadow_b0[index] += step->shadow_b0[index];
		parameters->shadow_b1[index] += step->shadow_b1[index];
	}
	for (index = 0; index < 5; index++)
		parameters->notch[index] += step->notch[index];
	parameters->rear += step->rear;
	parameters->gain += step->gain;
}

/* frames of a mono voice's direct path (direct) through the HRTF to each
ear, added to output (stereo) at mix of the voice (a frame's step of it
ramp_mix: the crossfade with the pan, which the voice's mix added), from
the voice's parameters (stream->hrtf_current) to target, ramped over
ramp_frames; a pass of its own over the mix, its delay line one run of
frames (the voice's last and these), so that it keeps to registers */
static void hrtf_process(struct sdl_stream *stream, const float *direct, float *output, unsigned long frames,
	unsigned long ramp_frames, const struct hrtf_parameters *target, float mix, float ramp_mix)
{
	float line[HRTF_FRAMES + MIX_CHUNK_FRAMES];
	struct hrtf_parameters now = stream->hrtf_current, ramp;
	float rear = stream->hrtf_rear, notch[4], shadow[2][2];
	unsigned long frame;
	int index;

	for (index = 0; index < 2; index++)
	{
		ramp.delay[index] = (target->delay[index] - now.delay[index]) / (float)ramp_frames;
		ramp.shadow_b0[index] = (target->shadow_b0[index] - now.shadow_b0[index]) / (float)ramp_frames;
		ramp.shadow_b1[index] = (target->shadow_b1[index] - now.shadow_b1[index]) / (float)ramp_frames;
	}
	for (index = 0; index < 5; index++)
		ramp.notch[index] = (target->notch[index] - now.notch[index]) / (float)ramp_frames;
	ramp.rear = (target->rear - now.rear) / (float)ramp_frames;
	ramp.gain = (target->gain - now.gain) / (float)ramp_frames;
	memcpy(notch, stream->hrtf_notch, sizeof(notch));
	memcpy(shadow, stream->hrtf_shadow, sizeof(shadow));
	memcpy(line, stream->hrtf_frames, sizeof(stream->hrtf_frames));
	for (frame = 0; frame < frames; frame++)
	{
		/* behind, the high frequencies taken off; the pinna's notch; then
		each ear its own delay and the head's shadow */
		float x = direct[frame], notched;
		int ear;

		rear = flush_denormal(x + hrtf.rear_lowpass * (rear - x));
		x -= now.rear * (x - rear);
		notched = now.notch[0] * x + now.notch[1] * notch[0] + now.notch[2] * notch[1] - now.notch[3] * notch[2] -
			now.notch[4] * notch[3];
		notched = flush_denormal(notched);
		notch[1] = notch[0];
		notch[0] = x;
		notch[3] = notch[2];
		notch[2] = notched;
		line[HRTF_FRAMES + frame] = notched;
		for (ear = 0; ear < 2; ear++)
		{
			float heard = hrtf_read(line + HRTF_FRAMES + frame, 1.0f + now.delay[ear]);
			float shadowed = flush_denormal(now.shadow_b0[ear] * heard + now.shadow_b1[ear] * shadow[ear][0] -
				hrtf.shadow_a1 * shadow[ear][1]);

			shadow[ear][0] = heard;
			shadow[ear][1] = shadowed;
			output[frame * 2 + ear] += shadowed * now.gain * mix;
		}
		hrtf_step(&now, &ramp);
		mix += ramp_mix;
	}
	memcpy(stream->hrtf_frames, line + frames, sizeof(stream->hrtf_frames));
	stream->hrtf_current = *target;
	stream->hrtf_rear = rear;
	memcpy(stream->hrtf_notch, notch, sizeof(notch));
	memcpy(stream->hrtf_shadow, shadow, sizeof(shadow));
}

/* audio.spatial ("auto", "headphones" or "speakers"; another is "auto")
and what the host says the sound plays on (headphones: 1 headphones, 0
speakers, -1 unknown, host_audio_headphones): whether 3D voices go through
the HRTF. Unknown is speakers: the pan is right for both, the HRTF only
for headphones. In the VR mode, vr.hrtf alone, as before (the headset's
speakers are at the ears) */
static BOOL spatial_hrtf(const char *setting, int headphones, BOOL vr_running, BOOL vr_hrtf)
{
	if (vr_running)
		return vr_hrtf;
	if (!strcmp(setting, "headphones"))
		return TRUE;
	if (!strcmp(setting, "speakers"))
		return FALSE;
	return headphones > 0;
}

/* the gains of a 3D voice to the left and right (the pan's), and the room
rolloff's attenuation of its room send; for a mono voice through the HRTF
(hrtf_on: chosen, or crossfading from it), its gain to each ear and its way
there (hrtf_out) */
static void spatialize(const struct sdl_stream *stream, float *left, float *right, float *room,
	BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	float offset[3], right_axis[3], distance, attenuation, pan, side, ahead, up;
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis];
		side = offset[0];
		up = offset[1];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		up = dot3(offset, listener.top);
		ahead = dot3(offset, listener.front);
	}
	/* in the game's units, those of the minimum and maximum distances: the
	distance factor turns units into meters for Doppler, and scaling by it
	here put every 3D sound 3 times as far away, up to 10 dB quieter */
	distance = sqrtf(dot3(offset, offset));
	attenuation = distance_attenuation(stream, distance, listener.rolloff_factor);
	*room = distance_attenuation(stream, distance, environment.flRoomRolloffFactor + stream->room_rolloff_factor);

	/* equal power pan; close sources and sources straight ahead or behind
	stay centred, and neither ear drops below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle) * 1.41421356f * 0.70710678f;
		*right = sinf(angle) * 1.41421356f * 0.70710678f;
	}
	/* and through the HRTF, each ear at the pan's gain in the middle (-3
	dB), the head's filters doing the rest */
	*hrtf_on = stream->channels == 1 && (hrtf_enabled || stream->hrtf_mix > 0.0f);
	if (*hrtf_on)
	{
		hrtf_place(side, up, ahead, distance, hrtf_out);
		hrtf_out->gain = 0.70710678f * attenuation * gain_from_millibels(stream->direct);
	}
	*left *= attenuation * gain_from_millibels(stream->direct);
	*right *= attenuation * gain_from_millibels(stream->direct);
}

/* the voice's gains to the left and right, and to the reverb (only 3D voices
send to it, as their I3DL2 mix bin did on the Xbox), with the coefficients of
the low passes of its direct path (3D voices: obstruction and occlusion
muffle it) and of the send; a 3D voice through the HRTF (hrtf_on) has its
way to each ear too (hrtf_out) */
static void voice_gains(const struct sdl_stream *stream, float *left, float *right, float *room,
	float *direct_lowpass, float *room_lowpass, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	*room = 0.0f;
	*direct_lowpass = 0.0f;
	*room_lowpass = 0.0f;
	*hrtf_on = FALSE;
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float rolloff, cosine = frequency_cosine(environment.flHFReference);

		spatialize(stream, left, right, &rolloff, hrtf_on, hrtf_out);
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			*room = gain_from_millibels(level) * rolloff;
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else if (stream->channels == 2 && stream->stereo_positioned)
	{
		/* the equal power pan of a 3D voice, at the gains of a 2D one when
		centred (the game fades it with distance), its direct path muffled
		and its room send made as a 3D voice's are (SetI3DL2Source) */
		float angle = (stream->stereo_pan + 1.0f) * 0.25f * 3.14159265f;
		float direct = gain_from_millibels(stream->direct);
		float cosine = frequency_cosine(environment.flHFReference);

		*left = cosf(angle) * 1.41421356f * stream->mix_left * direct;
		*right = sinf(angle) * 1.41421356f * stream->mix_right * direct;
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			/* the room's rolloff with distance, a 3D voice's; the volume
			holds the game's fade with it, which a 3D voice's room send does
			not take, so it is taken back out (no further than a twentieth:
			past that the send fades out with the sound) */
			*room = gain_from_millibels(level) *
				distance_attenuation(stream, stream->stereo_distance,
					environment.flRoomRolloffFactor + stream->room_rolloff_factor) /
				(stream->stereo_distance_fade > 0.05f ? stream->stereo_distance_fade : 0.05f);
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else
	{
		*left = stream->mix_left;
		*right = stream->mix_right;
	}
	*left *= stream->volume * master_volume;
	*right *= stream->volume * master_volume;
	*room *= stream->volume * master_volume;
	/* (the HRTF's gain too: without it every mono 3D voice on headphones
	played at full volume, whatever audio.volume, audio.effects_volume and
	the game's own gain said, and only the 2D voices followed them) */
	if (*hrtf_on)
		hrtf_out->gain *= stream->volume * master_volume;
}

/* the 5.1 speakers around the listener, clockwise from the center, and
their directions in radians (ITU-R BS.775: fronts at 30 degrees, backs at
110) */
#define SURROUND_RING_SPEAKERS 5
static const unsigned long surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 };
static const float surround_ring_angle[SURROUND_RING_SPEAKERS + 1] =
{
	0.0f, 0.52359878f, 1.91986218f, 4.36332313f, 5.75958653f, 6.28318531f,
};

/* the speaker gains of a sound from a direction, side (right) and ahead:
vector base amplitude panning (Pulkki's VBAP, in the plane) between the two
speakers either side of it, blended (in power) towards all five equally by
spread, 0 to 1. Their power sums to 1, as the stereo pan's does.

VBAP takes the gains g1 and g2 whose sum g1 l1 + g2 l2 of the speakers'
directions l1 and l2 points the sound's way p, then scales them to a power
of 1: g1 = sin(a2 - a) / sin(a2 - a1) and g2 = sin(a - a1) / sin(a2 - a1),
a the sound's angle, a1 and a2 the speakers', the common divisor gone with
the scaling. So a sound at a speaker plays from it alone, one between two
from both, as the tangent law of stereo panning puts a phantom image (the
listener between them hears it where it is), and no pair is wider than 180
degrees (the backs' 140 is the widest). */
static void surround_pan(float side, float ahead, float spread, float *gains)
{
	float angle = atan2f(side, ahead);
	unsigned long segment, speaker;

	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		gains[speaker] = 0.0f;
	if (angle < 0.0f)
		angle += 6.28318531f;
	for (segment = 0; segment < SURROUND_RING_SPEAKERS - 1; segment++)
	{
		if (angle < surround_ring_angle[segment + 1])
			break;
	}
	{
		float first = sinf(surround_ring_angle[segment + 1] - angle);
		float second = sinf(angle - surround_ring_angle[segment]);
		float length;

		first = first > 0.0f ? first : 0.0f;
		second = second > 0.0f ? second : 0.0f;
		length = sqrtf(first * first + second * second);
		if (length < 1.0e-6f)
		{
			first = 1.0f;
			length = 1.0f;
		}
		gains[surround_ring_channel[segment]] = first / length;
		gains[surround_ring_channel[(segment + 1) % SURROUND_RING_SPEAKERS]] = second / length;
	}
	for (speaker = 0; speaker < SURROUND_RING_SPEAKERS; speaker++)
	{
		float *gain = &gains[surround_ring_channel[speaker]];

		*gain = sqrtf((1.0f - spread) * *gain * *gain + spread / SURROUND_RING_SPEAKERS);
	}
}

/* on 5.1, a voice's gains from each source channel to each speaker (the
direct path's; its room send and low passes are voice_gains'):
	- a 3D voice is panned around the speakers from its direction, a little
	  spread so that it never vanishes between two, spread out towards all
	  five inside its minimum distance and above or below, as the stereo pan
	  centres those;
	- a stereo voice positioned in the world has its channels' middle (the
	  first row) panned towards it in the front half, and their difference
	  (the second) kept on the front pair as wide as the stereo mix keeps it;
	- a 2D voice goes by its mix bins. */
static void voice_surround_gains(const struct sdl_stream *stream, float gains[2][MAXIMUM_OUTPUT_CHANNELS])
{
	float scale = stream->volume * master_volume;
	unsigned long speaker;

	memset(gains, 0, 2 * sizeof(gains[0]));
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float offset[3], right_axis[3], side, ahead, distance, horizontal, attenuation, spread = 0.1f;
		int axis;

		if (stream->mode == DS3DMODE_HEADRELATIVE)
		{
			for (axis = 0; axis < 3; axis++)
				offset[axis] = stream->position[axis];
			side = offset[0];
			ahead = offset[2];
		}
		else
		{
			for (axis = 0; axis < 3; axis++)
				offset[axis] = stream->position[axis] - listener.position[axis];
			right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
			right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
			right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
			side = dot3(offset, right_axis);
			ahead = dot3(offset, listener.front);
		}
		horizontal = sqrtf(side * side + ahead * ahead);
		distance = sqrtf(dot3(offset, offset));
		if (horizontal <= 1.0e-4f)
			spread = 1.0f;
		else if (distance > 1.0e-4f && 0.5f * (1.0f - horizontal / distance) > spread)
			spread = 0.5f * (1.0f - horizontal / distance);
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f &&
			1.0f - distance / stream->minimum_distance > spread)
		{
			spread = 1.0f - distance / stream->minimum_distance;
		}
		surround_pan(side, ahead, spread, gains[0]);
		attenuation = distance_attenuation(stream, distance, listener.rolloff_factor) *
			gain_from_millibels(stream->direct);
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			gains[0][speaker] *= attenuation;
			/* a stereo 3D voice: both channels from where it is */
			if (stream->channels == 2)
				gains[1][speaker] = gains[0][speaker] *= 0.5f;
		}
	}
	else if (stream->channels == 2 && stream->stereo_positioned)
	{
		float pan = stream->stereo_pan < -1.0f ? -1.0f : stream->stereo_pan > 1.0f ? 1.0f : stream->stereo_pan;
		float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		float direct = gain_from_millibels(stream->direct);
		float far_gain = (cosf(angle) < sinf(angle) ? cosf(angle) : sinf(angle)) * 1.41421356f;

		/* the middle at the stereo pan's level (1 on each side when centred,
		a power of 2) */
		surround_pan(sinf(pan * 1.57079633f), cosf(pan * 1.57079633f), 0.1f, gains[0]);
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			gains[0][speaker] *= 1.41421356f * stream->mix_left * direct;
		gains[1][0] = far_gain * stream->mix_left * direct;
		gains[1][1] = -far_gain * stream->mix_right * direct;
	}
	else
	{
		memcpy(gains, stream->surround_bins, 2 * sizeof(gains[0]));
		if (stream->channels == 1)
			memset(gains[1], 0, sizeof(gains[1]));
	}
	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
	{
		gains[0][speaker] *= scale;
		gains[1][speaker] *= scale;
	}
}

/* ---------- resampling

Each voice is resampled to the output rate by band-limited interpolation (J.
O. Smith's): an output sample is the source frames around its moment, each
weighted by a windowed sinc low pass centred there. The low pass keeps
RESAMPLER_CUTOFF of the source's band (a 22 kHz voice is 0.5 dB down at 10
kHz) and takes the images of it out (80 dB down), but for those of its last
few hundred hertz, which lie beside them, about 20 dB down. Linear
interpolation, which the mixer did before, left the images only 8 to 20 dB
down, a gritty haze above 11 kHz over every 22 kHz voice. A voice
played faster than the output rate takes its frames (a step over 1) gets the
low pass narrowed to match, up to RESAMPLER_MAXIMUM_STRETCH times, so it
does not alias. The frames come from the voice's packets in turn, so the low
pass reads straight across a packet's end into the next. */

/* the low pass's one side, RESAMPLER_TABLE_STEPS values a source frame */
static float resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 2];
/* the same low pass as the weights of a voice's taps 1 - RESAMPLER_ZERO_CROSSINGS
to RESAMPLER_ZERO_CROSSINGS, at each of RESAMPLER_TABLE_STEPS phases between two
source frames (and one row more, for the last's blend): a voice at the output
rate or slower blends two rows by its phase, which is what the table gave tap
by tap, a third of the work. resampler_deltas holds each row's step to the
next, so the blend is a multiply and an add a tap */
#define RESAMPLER_TAPS (2 * RESAMPLER_ZERO_CROSSINGS)
static float resampler_phases[RESAMPLER_TABLE_STEPS + 1][RESAMPLER_TAPS] RESAMPLER_ALIGNED;
static float resampler_deltas[RESAMPLER_TABLE_STEPS][RESAMPLER_TAPS] RESAMPLER_ALIGNED;

/* how a voice's frames are converted to the output's rate: the windowed sinc
low pass, or between the two frames either side (linear: it lets more of the
images above the source's rate through, which some hear as brighter) */
enum resampler_mode
{
	RESAMPLER_SINC,
	RESAMPLER_LINEAR
};
/* audio.resampler */
static enum resampler_mode resampler_mode = RESAMPLER_SINC;

/* audio.resampler's mode: sinc for any value but "linear" */
static enum resampler_mode resampler_mode_read(void)
{
	const char *setting = config_string("audio.resampler");

	return setting && !strcmp(setting, "linear") ? RESAMPLER_LINEAR : RESAMPLER_SINC;
}

static double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 64 && term > 1.0e-12 * sum; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

static void resampler_initialize(void)
{
	unsigned long index;

	for (index = 0; index <= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS; index++)
	{
		double distance = (double)index / RESAMPLER_TABLE_STEPS;
		double edge = distance / RESAMPLER_ZERO_CROSSINGS;
		double angle = 3.14159265358979 * RESAMPLER_CUTOFF * distance;
		double sinc = index ? sin(angle) / angle : 1.0;
		double window = bessel_i0(RESAMPLER_KAISER_BETA * sqrt(1.0 - edge * edge)) / bessel_i0(RESAMPLER_KAISER_BETA);

		resampler_table[index] = (float)(RESAMPLER_CUTOFF * sinc * window);
	}
	resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 1] = 0.0f;
}

/* the low pass, distance source frames from its centre times
RESAMPLER_TABLE_STEPS */
static float resampler_weight(float distance)
{
	unsigned long index = (unsigned long)distance;
	float fraction;

	if (index >= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS)
		return 0.0f;
	fraction = distance - (float)index;
	return resampler_table[index] + (resampler_table[index + 1] - resampler_table[index]) * fraction;
}

static void resampler_phases_initialize(void)
{
	unsigned long phase, tap;

	for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < 2 * RESAMPLER_ZERO_CROSSINGS; tap++)
		{
			float distance = fabsf((float)((long)tap + 1 - RESAMPLER_ZERO_CROSSINGS) -
				(float)phase / RESAMPLER_TABLE_STEPS);

			resampler_phases[phase][tap] = resampler_weight(distance * RESAMPLER_TABLE_STEPS);
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < RESAMPLER_TAPS; tap++)
			resampler_deltas[phase][tap] = resampler_phases[phase + 1][tap] - resampler_phases[phase][tap];
	}
}

/* The low pass over a voice's frames, the sum of each frame times its weight:
four taps at a time where the compiler has vectors (SSE, NEON), summed in four
lanes, then the lanes together. Only the order of the additions differs from
tap by tap, a difference of the order of float's rounding (-115 dB or less);
RESAMPLER_SCALAR adds them tap by tap (tools/harness/tests/test_mixer.py). */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(RESAMPLER_SCALAR)
typedef float resampler_vector __attribute__((vector_size(16)));
typedef float resampler_unaligned_vector __attribute__((vector_size(16), aligned(4)));
/* (each row of the phase tables whole vectors, aligned) */
typedef char resampler_taps_in_fours[RESAMPLER_TAPS % 4 ? -1 : 1];
#define RESAMPLER_LANES 4
#else
#define RESAMPLER_LANES 1
#endif

/* frames[0, RESAMPLER_TAPS) weighted by the phase table's row blended blend of
the way to the next */
static float resampler_dot(const float *frames, const float *weights, const float *deltas, float blend)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		sum += *(const resampler_unaligned_vector *)(frames + tap) * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap++)
		sum += frames[tap] * (weights[tap] + deltas[tap] * blend);
	return sum;
#endif
}

/* resampler_dot of two channels' frames, with the same weights */
static void resampler_dot2(const float *left_frames, const float *right_frames, const float *weights,
	const float *deltas, float blend, float *left, float *right)
{
#if RESAMPLER_LANES == 4
	resampler_vector left_sum = { 0.0f, 0.0f, 0.0f, 0.0f }, right_sum = left_sum;
	resampler_vector blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		left_sum += *(const resampler_unaligned_vector *)(left_frames + tap) * weight;
		right_sum += *(const resampler_unaligned_vector *)(right_frames + tap) * weight;
	}
	*left = (left_sum[0] + left_sum[1]) + (left_sum[2] + left_sum[3]);
	*right = (right_sum[0] + right_sum[1]) + (right_sum[2] + right_sum[3]);
#else
	*left = resampler_dot(left_frames, weights, deltas, blend);
	*right = resampler_dot(right_frames, weights, deltas, blend);
#endif
}

/* a voice starting (over): silence before its first frame, which the output
starts at */
static void resampler_reset(struct sdl_stream *stream)
{
	memset(stream->history, 0, sizeof(stream->history));
	stream->history_count = RESAMPLER_ZERO_CROSSINGS * RESAMPLER_MAXIMUM_STRETCH;
	stream->center = stream->history_count;
	stream->phase = 0.0;
	stream->silence = 0;
}

/* how much the low pass is narrowed for a voice taking frames step at a time:
for one faster than the output rate, to keep what it takes above the output's
band out (up to RESAMPLER_MAXIMUM_STRETCH times) */
static float resampler_scale(double step)
{
	return step > 1.0 ? (float)(1.0 / (step < RESAMPLER_MAXIMUM_STRETCH ? step : RESAMPLER_MAXIMUM_STRETCH)) : 1.0f;
}

static float packet_sample(const struct voice_packet *packet, unsigned long frame, unsigned long channel,
	unsigned long channels)
{
	return packet->samples[frame * channels + channel] * (1.0f / 32768.0f);
}

/* the voice's next frame, finishing the packets it passes; FALSE once they
run out */
static BOOL take_frame(struct sdl_stream *stream, float *frame)
{
	for (;;)
	{
		struct voice_packet *packet = NULL;
		unsigned long position;

		for (position = 0; position < stream->mixing_count; position++)
		{
			struct voice_packet *candidate = &stream->mixing[(stream->mixing_head + position) % MAXIMUM_STREAM_PACKETS];

			if (!candidate->finished)
			{
				packet = candidate;
				break;
			}
		}
		if (!packet)
			return FALSE;
		if (stream->cursor < packet->frames)
		{
			frame[0] = packet_sample(packet, stream->cursor, 0, stream->channels);
			frame[1] = packet_sample(packet, stream->cursor, stream->channels - 1, stream->channels);
			stream->cursor++;
			return TRUE;
		}
		stream->cursor = 0;
		packet->finished = TRUE;
	}
}

/* whether the voice has a frame to take; the packets with none left are
marked finished on the way, as take_frame marks them */
static BOOL voice_has_frames(struct sdl_stream *stream)
{
	unsigned long position;

	for (position = 0; position < stream->mixing_count; position++)
	{
		struct voice_packet *packet = &stream->mixing[(stream->mixing_head + position) % MAXIMUM_STREAM_PACKETS];

		if (packet->finished)
			continue;
		if (stream->cursor < packet->frames)
			return TRUE;
		stream->cursor = 0;
		packet->finished = TRUE;
	}
	return FALSE;
}

/* ---------- mixing */

/* mixes one voice into output (frames of output_channels floats), and into
the reverb's send (frames of mono) */
static void mix_voice(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	double step, target_step, ramp_step;
	float target_left, target_right, target_room, left, right, room, ramp_left, ramp_right, ramp_room, scale;
	float target_direct_lowpass, target_room_lowpass, direct_lowpass, room_lowpass;
	float ramp_direct_lowpass, ramp_room_lowpass;
	float direct_state[2], room_state;
	long width;
	unsigned long frame;
	/* (a stereo voice panned towards its sound: voice_gains) */
	BOOL positioned = stream->channels == 2 && stream->stereo_positioned;
	/* (through the HRTF: whether it may take the voice, and whether its
	filters run in this mix; how much of the voice they play, at the start,
	the end and a frame's step; its parameters, and the direct path the
	HRTF takes, hrtf_process) */
	BOOL hrtf_on, hrtf_running;
	float hrtf_mix, hrtf_mix_end, ramp_hrtf_mix, hrtf_mix_start;
	struct hrtf_parameters target_hrtf;
	float hrtf_direct[MIX_CHUNK_FRAMES];
	/* on 5.1, the speakers' gains (voice_surround_gains) */
	BOOL surround = output_channels == 6;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS], surround_gains[2][MAXIMUM_OUTPUT_CHANNELS];
	float ramp_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	unsigned long speaker;

	if (stream->paused || !stream->mixing_count || !stream->sample_rate)
		return;
	/* (at most MIX_CHUNK_FRAMES at a time: the HRTF's direct path) */
	if (frames > MIX_CHUNK_FRAMES)
	{
		mix_voice(stream, output, send, MIX_CHUNK_FRAMES);
		mix_voice(stream, output + MIX_CHUNK_FRAMES * output_channels, send + MIX_CHUNK_FRAMES,
			frames - MIX_CHUNK_FRAMES);
		return;
	}
	target_step = (double)(stream->frequency ? stream->frequency : stream->sample_rate) / OUTPUT_RATE *
		doppler_smoothed(stream, frames);
	/* (the step ramps from the last mix's to it, as the gains do) */
	step = stream->gains_valid ? stream->current_step : target_step;
	scale = resampler_scale(step > target_step ? step : target_step);
	width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	/* a voice that ran dry and stopped (below) starts over once it has
	frames again: stream_process starts over only a stream with no packets,
	and the next can come before the finished ones are completed */
	if (stream->silence > (unsigned long)(2 * width))
	{
		if (!voice_has_frames(stream))
			return;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
		step = target_step;
		scale = resampler_scale(step);
		width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	}
	ramp_step = (target_step - step) / (double)frames;
	voice_gains(stream, &target_left, &target_right, &target_room, &target_direct_lowpass, &target_room_lowpass,
		&hrtf_on, &target_hrtf);
	/* (the HRTF is for a pair of ears: none on 5.1) */
	if (surround)
		hrtf_on = FALSE;
	/* how much of the voice the HRTF plays: towards all of it while it is
	chosen (hrtf_enabled), none otherwise, a frame's share of the crossfade
	at a time; a voice starting is at once where it is going */
	hrtf_mix = hrtf_on ? stream->hrtf_mix : 0.0f;
	hrtf_mix_end = hrtf_on && hrtf_enabled ? 1.0f : 0.0f;
	if (!stream->gains_valid)
		hrtf_mix = hrtf_mix_end;
	else
	{
		float fade = (float)frames / HRTF_FADE_FRAMES;

		if (hrtf_mix_end > hrtf_mix)
			hrtf_mix_end = hrtf_mix + fade < 1.0f ? hrtf_mix + fade : 1.0f;
		else if (hrtf_mix_end < hrtf_mix)
			hrtf_mix_end = hrtf_mix - fade > 0.0f ? hrtf_mix - fade : 0.0f;
	}
	ramp_hrtf_mix = (hrtf_mix_end - hrtf_mix) / (float)frames;
	hrtf_running = hrtf_on && (hrtf_mix > 0.0f || hrtf_mix_end > 0.0f);
	/* (its filters start from silence, as the crossfade starts from none
	of them) */
	if (hrtf_running && (!stream->gains_valid || !stream->hrtf_on))
	{
		stream->hrtf_current = target_hrtf;
		memset(stream->hrtf_frames, 0, sizeof(stream->hrtf_frames));
		stream->hrtf_rear = 0.0f;
		memset(stream->hrtf_notch, 0, sizeof(stream->hrtf_notch));
		memset(stream->hrtf_shadow, 0, sizeof(stream->hrtf_shadow));
	}
	stream->hrtf_on = hrtf_running;
	stream->hrtf_mix = hrtf_mix_end;
	if (surround)
		voice_surround_gains(stream, target_surround);
	if (!stream->gains_valid)
	{
		if (surround)
			memcpy(stream->current_surround, target_surround, sizeof(target_surround));
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->current_room = target_room;
		stream->current_direct_lowpass = target_direct_lowpass;
		stream->current_room_lowpass = target_room_lowpass;
		stream->direct_lowpass[0] = stream->direct_lowpass[1] = 0.0f;
		stream->room_lowpass = 0.0f;
		stream->gains_valid = TRUE;
	}
	left = stream->current_left;
	right = stream->current_right;
	room = stream->current_room;
	direct_lowpass = stream->current_direct_lowpass;
	room_lowpass = stream->current_room_lowpass;
	/* the low passes' states, here while the mix writes the output (which
	the compiler cannot tell from them) */
	direct_state[0] = stream->direct_lowpass[0];
	direct_state[1] = stream->direct_lowpass[1];
	room_state = stream->room_lowpass;
	ramp_left = (target_left - left) / (float)frames;
	ramp_right = (target_right - right) / (float)frames;
	ramp_room = (target_room - room) / (float)frames;
	ramp_direct_lowpass = (target_direct_lowpass - direct_lowpass) / (float)frames;
	ramp_room_lowpass = (target_room_lowpass - room_lowpass) / (float)frames;
	hrtf_mix_start = hrtf_mix;
	if (surround)
	{
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			surround_gains[0][speaker] = stream->current_surround[0][speaker];
			surround_gains[1][speaker] = stream->current_surround[1][speaker];
			ramp_surround[0][speaker] = (target_surround[0][speaker] - surround_gains[0][speaker]) / (float)frames;
			ramp_surround[1][speaker] = (target_surround[1][speaker] - surround_gains[1][speaker]) / (float)frames;
		}
	}

	for (frame = 0; frame < frames; frame++)
	{
		float sample_left = 0.0f, sample_right = 0.0f;
		long tap;

		/* the frames ahead the low pass reaches; after the last packet,
		silence, until what was taken has played out */
		while ((long)(stream->history_count - stream->center) <= width)
		{
			unsigned long slot = stream->history_count % RESAMPLER_HISTORY;
			float taken[2];

			if (take_frame(stream, taken))
			{
				stream->silence = 0;
			}
			else
			{
				taken[0] = taken[1] = 0.0f;
				stream->silence++;
			}
			stream->history[0][slot] = stream->history[0][slot + RESAMPLER_HISTORY] = taken[0];
			stream->history[1][slot] = stream->history[1][slot + RESAMPLER_HISTORY] = taken[1];
			stream->history_count++;
		}
		if (stream->silence > (unsigned long)(2 * width))
			break;

		if (!left && !right && !room && !ramp_left && !ramp_right && !ramp_room)
		{
			/* a voice turned all the way down (out of earshot), and not
			sending to the reverb, only moves on */
		}
		else if (resampler_mode == RESAMPLER_LINEAR)
		{
			/* (the frame the output is at and the next, side by side
			however the ring wraps) */
			const float *left_frames = stream->history[0] + stream->center % RESAMPLER_HISTORY;
			const float *right_frames = stream->history[1] + stream->center % RESAMPLER_HISTORY;
			float fraction = (float)stream->phase;

			sample_left = left_frames[0] + (left_frames[1] - left_frames[0]) * fraction;
			sample_right = right_frames[0] + (right_frames[1] - right_frames[0]) * fraction;
		}
		else if (scale == 1.0f)
		{
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			unsigned long first = (stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY;

			/* (a mono voice's two channels are the same: take_frame) */
			if (stream->channels == 1)
			{
				sample_left = sample_right = resampler_dot(stream->history[0] + first, resampler_phases[row],
					resampler_deltas[row], blend);
			}
			else
			{
				resampler_dot2(stream->history[0] + first, stream->history[1] + first, resampler_phases[row],
					resampler_deltas[row], blend, &sample_left, &sample_right);
			}
		}
		else
		{
			unsigned long first = (stream->center + 1 - (unsigned long)width) % RESAMPLER_HISTORY;
			const float *left_frames = stream->history[0] + first, *right_frames = stream->history[1] + first;

			for (tap = 1 - width; tap <= width; tap++)
			{
				float weight = scale * resampler_weight(fabsf((float)tap - (float)stream->phase) * scale * RESAMPLER_TABLE_STEPS);

				sample_left += left_frames[tap + width - 1] * weight;
				if (stream->channels == 2)
					sample_right += right_frames[tap + width - 1] * weight;
			}
			if (stream->channels == 1)
				sample_right = sample_left;
		}
		/* the room send, with its own low pass */
		if (room || ramp_room)
		{
			float mono = stream->channels == 1 ? sample_left : 0.5f * (sample_left + sample_right);

			room_state = flush_denormal(mono + room_lowpass * (room_state - mono));
			send[frame] += room_state * room;
		}
		/* the direct path, muffled, or as it is; a mono voice's mix bins or pan
		split it across the speakers */
		if (direct_lowpass || ramp_direct_lowpass)
		{
			sample_left = flush_denormal(sample_left + direct_lowpass * (direct_state[0] - sample_left));
			sample_right = flush_denormal(sample_right + direct_lowpass * (direct_state[1] - sample_right));
		}
		direct_state[0] = sample_left;
		direct_state[1] = sample_right;
		if (hrtf_running)
		{
			/* through the HRTF (hrtf_process, after), crossfaded with the
			pan while the choice changes */
			float panned = 1.0f - hrtf_mix;

			hrtf_direct[frame] = sample_left;
			if (panned > 0.0f)
			{
				output[frame * 2] += sample_left * left * panned;
				output[frame * 2 + 1] += sample_left * right * panned;
			}
			hrtf_mix += ramp_hrtf_mix;
		}
		else if (surround)
		{
			/* the first row's gains take the left (a mono voice's only)
			channel, or a positioned voice's middle; the second the right, or
			its difference */
			float *speakers = output + frame * MAXIMUM_OUTPUT_CHANNELS;
			float first = positioned ? 0.5f * (sample_left + sample_right) : sample_left;
			float second = positioned ? 0.5f * (sample_left - sample_right) : stream->channels == 2 ? sample_right : 0.0f;

			for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			{
				speakers[speaker] += first * surround_gains[0][speaker] + second * surround_gains[1][speaker];
				surround_gains[0][speaker] += ramp_surround[0][speaker];
				surround_gains[1][speaker] += ramp_surround[1][speaker];
			}
		}
		else if (stream->channels == 1)
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_left * right;
		}
		else if (positioned)
		{
			/* the channels' middle panned, and their difference kept as
			wide as the far ear's gain: the sound comes from where it is,
			still stereo */
			float middle = 0.5f * (sample_left + sample_right);
			float side = 0.5f * (sample_left - sample_right);
			float far_gain = left < right ? left : right;

			output[frame * 2] += middle * left + side * far_gain;
			output[frame * 2 + 1] += middle * right - side * far_gain;
		}
		else
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_right * right;
		}
		left += ramp_left;
		right += ramp_right;
		room += ramp_room;
		direct_lowpass += ramp_direct_lowpass;
		room_lowpass += ramp_room_lowpass;
		stream->phase += step;
		step += ramp_step;
		while (stream->phase >= 1.0)
		{
			stream->phase -= 1.0;
			stream->center++;
		}
	}
	stream->direct_lowpass[0] = direct_state[0];
	stream->direct_lowpass[1] = direct_state[1];
	stream->room_lowpass = room_state;
	/* (the frames mixed: all of them, or those before the voice ran dry) */
	if (hrtf_running)
		hrtf_process(stream, hrtf_direct, output, frame, frames, &target_hrtf, hrtf_mix_start, ramp_hrtf_mix);
	stream->current_left = target_left;
	stream->current_right = target_right;
	stream->current_room = target_room;
	stream->current_step = target_step;
	stream->current_direct_lowpass = target_direct_lowpass;
	stream->current_room_lowpass = target_room_lowpass;
	if (surround)
		memcpy(stream->current_surround, target_surround, sizeof(target_surround));
}

/* ---------- reverb

The Xbox ran an I3DL2 reverb on its audio DSP (the effects image the game
downloads), fed by every 3D voice's I3DL2 mix bin. The game sets the listener
properties from the sound environment the camera is in (sound_dsound_xbox.c
dsound_set_listener_properties), and each 3D voice's room send from its
reverb attenuation and occlusion (dsound_channel_set_I3DL2_properties). This
is a reverb of the I3DL2 model on those properties, not the DSP's program:
	- each 3D voice sends to a mono bus at the listener's and its own room
	  levels, occlusion included, their high frequency levels a low pass at the
	  HF reference, attenuated with distance by the room rolloff factors
	  (voice_gains);
	- the early reflections are REVERB_TAPS taps of the bus, starting the
	  reflections delay later, at the reflections level;
	- the late reverberation starts the reverb delay after the reflections:
	  all-pass diffusers as strong as the diffusion, then a feedback delay
	  network of REVERB_LINES lines, as long as the density makes them, each
	  losing what decays it by 60 dB in the decay time, and its high
	  frequencies in the decay time times the HF ratio, at the reverb level.
Both are normalized to the energy sent, so their levels are relative to the
room level, as I3DL2 gives them. A change of environment crossfades each
delay's read from its old length to its new one, and each gain, over
REVERB_FADE_FRAMES: what is reverberating carries on, without the click of a
jump or the pitch shift of a sliding delay. audio.reverb = false fades the
reverb out over as long and stops it. Once nothing has been sent for
REVERB_QUIET_FRAMES and the reverberation is 120 dB down, the reverb stops
until something is sent again (outdoors, in the menus). */

#define REVERB_DELAY_SIZE 32768 /* the reflections and reverb delays, 0.3 + 0.1 s at most, and the taps */
#define REVERB_TAPS 6
#define REVERB_DIFFUSERS 4
#define REVERB_DIFFUSER_SIZE 1024
#define REVERB_LINES 8
#define REVERB_LINE_SIZE 4096
#define REVERB_FADE_FRAMES 4096 /* 85 ms */
/* the input delays, then the diffusers, played out */
#define REVERB_QUIET_FRAMES (REVERB_DELAY_SIZE + REVERB_DIFFUSERS * REVERB_DIFFUSER_SIZE)
#define REVERB_QUIET_PEAK 1.0e-6f

/* in milliseconds: the taps after the reflections delay, the diffusers, and
the lines at full density */
static const float reverb_tap_times[REVERB_TAPS] = { 0.0f, 3.1f, 5.3f, 7.9f, 11.2f, 14.9f };
static const float reverb_tap_signs[REVERB_TAPS] = { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f };
static const float reverb_diffuser_times[REVERB_DIFFUSERS] = { 4.7f, 3.6f, 12.7f, 9.3f };
static const float reverb_line_times[REVERB_LINES] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.1f };
/* which lines go in negated, and the two outputs' signs of the lines */
static const float reverb_input_signs[REVERB_LINES] = { 1, -1, -1, 1, 1, -1, -1, 1 };
static const float reverb_left_signs[REVERB_LINES] = { 1, -1, 1, -1, 1, -1, 1, -1 };
static const float reverb_right_signs[REVERB_LINES] = { 1, 1, -1, -1, 1, 1, -1, -1 };

/* what an environment sets: delays in samples, and gains */
struct reverb_parameters
{
	unsigned long taps[REVERB_TAPS];
	unsigned long late_delay;
	unsigned long lengths[REVERB_LINES];
	/* each line's gain a pass, and its low pass's coefficient */
	float feedback[REVERB_LINES];
	float damping[REVERB_LINES];
	float reflections_gain, late_gain;
	float diffusion;
};

static struct
{
	/* the bus of the current mix */
	float send[MIX_CHUNK_FRAMES];
	/* the environment serial and properties of the parameters faded to */
	unsigned long serial;
	DSI3DL2LISTENER properties;
	/* the parameters fading from and to, and the frames of the fade left
	(none: from is to) */
	struct reverb_parameters from, to;
	unsigned long fade;
	/* the output's level, 1 while audio.reverb is on, moving to 0 when it
	is turned off; whether the buffers are silent; and the frames since the
	send or the output was last over REVERB_QUIET_PEAK */
	float level;
	BOOL silent;
	unsigned long quiet;

	/* the bus, delayed for the taps and the late reverberation */
	float delay[REVERB_DELAY_SIZE];
	unsigned long delay_position;

	float diffusers[REVERB_DIFFUSERS][REVERB_DIFFUSER_SIZE];
	unsigned long diffuser_lengths[REVERB_DIFFUSERS];
	unsigned long diffuser_position;

	float lines[REVERB_LINES][REVERB_LINE_SIZE];
	unsigned long line_position;
	/* each line's low pass */
	float lowpass[REVERB_LINES];
} reverb;

static float clamp_real(float value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

/* whole samples */
static unsigned long reverb_samples(float seconds)
{
	return (unsigned long)floorf(seconds * OUTPUT_RATE + 0.5f);
}

/* the sample buffer got delay samples before position, crossfaded from the
from delay to the to delay by fade (0 to 1) */
static float reverb_read(const float *buffer, unsigned long mask, unsigned long position,
	unsigned long from_delay, unsigned long to_delay, float fade)
{
	float from_sample = buffer[(position - from_delay) & mask];

	if (from_delay == to_delay)
		return from_sample;
	return from_sample + (buffer[(position - to_delay) & mask] - from_sample) * fade;
}

static float reverb_blend(float from, float to, float fade)
{
	return from + (to - from) * fade;
}

static void reverb_parameters_set(struct reverb_parameters *parameters, const DSI3DL2LISTENER *properties)
{
	float decay = clamp_real(properties->flDecayTime, 0.1f, 20.0f);
	/* a low pass loses high frequencies; a ratio over 1 would need them to
	last longer */
	float high_decay = decay * clamp_real(properties->flDecayHFRatio, 0.1f, 1.0f);
	float scale = 0.25f + 0.75f * clamp_real(properties->flDensity / 100.0f, 0.0f, 1.0f);
	float cosine = frequency_cosine(properties->flHFReference);
	float mean_length = 0.0f, mean_feedback;
	unsigned long reflections_delay;
	int index;

	for (index = 0; index < REVERB_LINES; index++)
	{
		float length = floorf(reverb_line_times[index] * 0.001f * OUTPUT_RATE * scale + 0.5f);
		float high_feedback;

		parameters->lengths[index] = (unsigned long)length;
		parameters->feedback[index] = powf(10.0f, -3.0f * length / (decay * OUTPUT_RATE));
		high_feedback = powf(10.0f, -3.0f * length / (high_decay * OUTPUT_RATE));
		parameters->damping[index] = lowpass_coefficient(high_feedback / parameters->feedback[index], cosine);
		mean_length += length / REVERB_LINES;
	}
	mean_feedback = powf(10.0f, -3.0f * mean_length / (decay * OUTPUT_RATE));
	reflections_delay = reverb_samples(clamp_real(properties->flReflectionsDelay, 0.0f, 0.3f));
	for (index = 0; index < REVERB_TAPS; index++)
		parameters->taps[index] = reflections_delay + reverb_samples(reverb_tap_times[index] * 0.001f);
	parameters->late_delay = reflections_delay + reverb_samples(clamp_real(properties->flReverbDelay, 0.0f, 0.1f));
	/* the taps' energy, half of them to each side; the network's, whose
	sound circulates until it decays */
	parameters->reflections_gain = gain_from_millibels(properties->lReflections) /
		sqrtf((float)REVERB_TAPS / OUTPUT_CHANNELS);
	parameters->late_gain = gain_from_millibels(properties->lReverb) * sqrtf(1.0f - mean_feedback * mean_feedback);
	parameters->diffusion = 0.7f * clamp_real(properties->flDiffusion / 100.0f, 0.0f, 1.0f);
}

/* a new environment, faded to unless the reverb is silent. The game sets the
same one again several times a second in some places (Derelict), which
changes nothing */
static void reverb_update(const DSI3DL2LISTENER *properties, unsigned long serial)
{
	reverb.serial = serial;
	if (!reverb.silent && !memcmp(properties, &reverb.properties, sizeof(*properties)))
		return;
	reverb.properties = *properties;
	reverb_parameters_set(&reverb.to, properties);
	if (reverb.silent)
		reverb.from = reverb.to;
	reverb.fade = reverb.silent ? 0 : REVERB_FADE_FRAMES;
}

static void reverb_clear(void)
{
	memset(reverb.delay, 0, sizeof(reverb.delay));
	memset(reverb.diffusers, 0, sizeof(reverb.diffusers));
	memset(reverb.lines, 0, sizeof(reverb.lines));
	memset(reverb.lowpass, 0, sizeof(reverb.lowpass));
	reverb.from = reverb.to;
	reverb.fade = 0;
	reverb.silent = TRUE;
	reverb.quiet = 0;
}

static void reverb_initialize(void)
{
	int index;

	for (index = 0; index < REVERB_DIFFUSERS; index++)
		reverb.diffuser_lengths[index] = reverb_samples(reverb_diffuser_times[index] * 0.001f);
	reverb.silent = TRUE;
	reverb.level = reverb_enabled ? 1.0f : 0.0f;
	reverb_update(&environment, environment_serial);
}

/* adds the reverberation of send to output, its level moving to
target_level */
static void reverb_process(const float *send, float *output, unsigned long frames, float target_level)
{
	const struct reverb_parameters *from = &reverb.from, *to = &reverb.to;
	float input_scale = 1.0f / sqrtf((float)REVERB_LINES), peak = 0.0f;
	unsigned long frame;
	int index;

	reverb.silent = FALSE;
	for (frame = 0; frame < frames; frame++)
	{
		/* how far the fade from the old environment to the new is */
		float fade = 1.0f - (float)reverb.fade / REVERB_FADE_FRAMES;
		float early[OUTPUT_CHANNELS] = { 0.0f, 0.0f };
		float outputs[REVERB_LINES], late_left = 0.0f, late_right = 0.0f, feedback_sum = 0.0f, value;
		float diffusion = reverb_blend(from->diffusion, to->diffusion, fade);

		reverb.delay[reverb.delay_position & (REVERB_DELAY_SIZE - 1)] = send[frame];
		for (index = 0; index < REVERB_TAPS; index++)
		{
			early[index % OUTPUT_CHANNELS] += reverb_tap_signs[index] * reverb_read(reverb.delay, REVERB_DELAY_SIZE - 1,
				reverb.delay_position, from->taps[index], to->taps[index], fade);
		}

		/* diffusers: w = x + g w', y = w' - g w */
		value = reverb_read(reverb.delay, REVERB_DELAY_SIZE - 1, reverb.delay_position,
			from->late_delay, to->late_delay, fade);
		reverb.delay_position++;
		for (index = 0; index < REVERB_DIFFUSERS; index++)
		{
			float *buffer = reverb.diffusers[index];
			float delayed = buffer[(reverb.diffuser_position - reverb.diffuser_lengths[index]) & (REVERB_DIFFUSER_SIZE - 1)];
			float written = flush_denormal(value + diffusion * delayed);

			buffer[reverb.diffuser_position & (REVERB_DIFFUSER_SIZE - 1)] = written;
			value = delayed - diffusion * written;
		}
		reverb.diffuser_position++;

		/* the network: each line's output is damped and heard, then decayed
		and mixed back into every line by a Householder reflection */
		for (index = 0; index < REVERB_LINES; index++)
		{
			float line_output = reverb_read(reverb.lines[index], REVERB_LINE_SIZE - 1, reverb.line_position,
				from->lengths[index], to->lengths[index], fade);
			float damping = reverb_blend(from->damping[index], to->damping[index], fade);

			reverb.lowpass[index] = flush_denormal(line_output + damping * (reverb.lowpass[index] - line_output));
			late_left += reverb_left_signs[index] * reverb.lowpass[index];
			late_right += reverb_right_signs[index] * reverb.lowpass[index];
			outputs[index] = reverb.lowpass[index] * reverb_blend(from->feedback[index], to->feedback[index], fade);
			feedback_sum += outputs[index];
		}
		feedback_sum *= 2.0f / REVERB_LINES;
		for (index = 0; index < REVERB_LINES; index++)
		{
			reverb.lines[index][reverb.line_position & (REVERB_LINE_SIZE - 1)] =
				flush_denormal(outputs[index] - feedback_sum + value * reverb_input_signs[index] * input_scale);
		}
		reverb.line_position++;

		{
			float reflections_gain = reverb_blend(from->reflections_gain, to->reflections_gain, fade);
			float late_gain = reverb_blend(from->late_gain, to->late_gain, fade);
			float left = early[0] * reflections_gain + late_left * late_gain;
			float right = early[1] * reflections_gain + late_right * late_gain;

			if (output_channels == 6)
			{
				/* from the front and back pairs, at the same power */
				output[frame * 6] += left * reverb.level * 0.70710678f;
				output[frame * 6 + 1] += right * reverb.level * 0.70710678f;
				output[frame * 6 + 4] += left * reverb.level * 0.70710678f;
				output[frame * 6 + 5] += right * reverb.level * 0.70710678f;
			}
			else
			{
				output[frame * 2] += left * reverb.level;
				output[frame * 2 + 1] += right * reverb.level;
			}
			peak = fabsf(left) > peak ? fabsf(left) : peak;
			peak = fabsf(right) > peak ? fabsf(right) : peak;
			peak = fabsf(send[frame]) > peak ? fabsf(send[frame]) : peak;
		}
		if (reverb.fade && !--reverb.fade)
			reverb.from = reverb.to;
		reverb.level = clamp_real(target_level, reverb.level - 1.0f / REVERB_FADE_FRAMES,
			reverb.level + 1.0f / REVERB_FADE_FRAMES);
	}
	reverb.quiet = peak > REVERB_QUIET_PEAK ? 0 : reverb.quiet + frames;
}

/* whether anything is sent to the reverb in send */
static BOOL reverb_sent(const float *send, unsigned long frames)
{
	unsigned long frame;

	for (frame = 0; frame < frames; frame++)
	{
		if (fabsf(send[frame]) > REVERB_QUIET_PEAK)
			return TRUE;
	}
	return FALSE;
}

/* ---------- limiter

The game sets its mix bins' headroom to 0 (sound_dsound_xbox.c), so the voices
sum at their full level, as on the Xbox, and a pile of loud ones goes over
full scale. Clipping each sample, or bending it near full scale, distorts the
sound: dialogue over gunfire crackled. Instead the whole mix is turned down
for as long as it would go over, both channels alike. The output is delayed
LIMITER_LOOKAHEAD - 1 frames (1.3 ms), so that the gain comes down smoothly
before each peak: the smallest gain the frames ahead need, averaged over the
last LIMITER_LOOKAHEAD frames, is never more than a peak needs when it plays.
The gain comes back up over LIMITER_RELEASE_SECONDS. The ceiling is full
scale: the menus' music is mastered to it (the title theme's samples reach 0
dBFS), and a ceiling under it (it was -1 dBFS) turned the theme down by up to
1.2 dB at each of its loud notes and for the release after, a sixth of its
loud part, where the Xbox played it as it is. Only a sum over full scale is
turned down. */

#define LIMITER_CEILING 1.0f /* 0 dBFS */
#define LIMITER_LOOKAHEAD 64
#define LIMITER_RELEASE_SECONDS 0.1f

static struct
{
	/* the frames the output is delayed by, and the gain each needs */
	float delay[LIMITER_LOOKAHEAD][MAXIMUM_OUTPUT_CHANNELS];
	float needed[LIMITER_LOOKAHEAD];
	/* how many of them are under 1: none while nothing is too loud, when the
	smallest is 1 without looking */
	unsigned long limiting;
	/* the gain held down to what the frames ahead need, coming back up */
	float held;
	/* its last LIMITER_LOOKAHEAD values, and their sum */
	float history[LIMITER_LOOKAHEAD];
	double history_sum;
	unsigned long position;
	BOOL initialized;
} limiter;

static void limit(float *output, unsigned long frames)
{
	float release = 1.0f - expf(-1.0f / (LIMITER_RELEASE_SECONDS * OUTPUT_RATE));
	unsigned long frame, index, channel;

	if (!limiter.initialized)
	{
		for (index = 0; index < LIMITER_LOOKAHEAD; index++)
		{
			limiter.needed[index] = 1.0f;
			limiter.history[index] = 1.0f;
		}
		limiter.held = 1.0f;
		limiter.history_sum = LIMITER_LOOKAHEAD;
		limiter.initialized = TRUE;
	}
	for (frame = 0; frame < frames; frame++)
	{
		float *sample = output + frame * output_channels;
		unsigned long position = limiter.position;
		unsigned long oldest = (position + 1) % LIMITER_LOOKAHEAD;
		float peak = 0.0f, lowest, gain;

		/* the new frame takes the slot of the oldest, which has played */
		for (channel = 0; channel < output_channels; channel++)
		{
			if (fabsf(sample[channel]) > peak)
				peak = fabsf(sample[channel]);
			limiter.delay[position][channel] = sample[channel];
		}
		limiter.limiting -= limiter.needed[position] < 1.0f;
		limiter.needed[position] = peak > LIMITER_CEILING ? LIMITER_CEILING / peak : 1.0f;
		limiter.limiting += limiter.needed[position] < 1.0f;
		lowest = 1.0f;
		if (limiter.limiting)
		{
			lowest = limiter.needed[0];
			for (index = 1; index < LIMITER_LOOKAHEAD; index++)
			{
				if (limiter.needed[index] < lowest)
					lowest = limiter.needed[index];
			}
		}
		if (lowest < limiter.held)
			limiter.held = lowest;
		else
			limiter.held += (lowest - limiter.held) * release;
		limiter.history_sum += limiter.held - limiter.history[position];
		limiter.history[position] = limiter.held;
		gain = (float)(limiter.history_sum / LIMITER_LOOKAHEAD);
		/* the frame LIMITER_LOOKAHEAD - 1 frames old plays */
		for (channel = 0; channel < output_channels; channel++)
			sample[channel] = limiter.delay[oldest][channel] * gain;
		limiter.position = oldest;
	}
}

/* ---------- recording

debug.audio_record: the mix as it plays, to a WAV file of 16-bit stereo at
the output rate, its sizes written again each second, so that a test that
stops the game mid-run still leaves a whole file. On the mixer's thread (the
device's or the clock's); opened before either starts (audio_start). */

static FILE *record_file;
static unsigned long record_frames;

static void record_sizes(void)
{
	unsigned long bytes = record_frames * OUTPUT_CHANNELS * 2;
	unsigned char size[4];
	long end = ftell(record_file);

	size[0] = (unsigned char)(bytes + 36);
	size[1] = (unsigned char)((bytes + 36) >> 8);
	size[2] = (unsigned char)((bytes + 36) >> 16);
	size[3] = (unsigned char)((bytes + 36) >> 24);
	fseek(record_file, 4, SEEK_SET);
	fwrite(size, 1, 4, record_file);
	size[0] = (unsigned char)bytes;
	size[1] = (unsigned char)(bytes >> 8);
	size[2] = (unsigned char)(bytes >> 16);
	size[3] = (unsigned char)(bytes >> 24);
	fseek(record_file, 40, SEEK_SET);
	fwrite(size, 1, 4, record_file);
	fseek(record_file, end, SEEK_SET);
	fflush(record_file);
}

static void record_open(void)
{
	static const unsigned char header[44] =
	{
		'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, OUTPUT_CHANNELS, 0,
		OUTPUT_RATE & 0xff, (OUTPUT_RATE >> 8) & 0xff, (OUTPUT_RATE >> 16) & 0xff, 0,
		(OUTPUT_RATE * OUTPUT_CHANNELS * 2) & 0xff, ((OUTPUT_RATE * OUTPUT_CHANNELS * 2) >> 8) & 0xff,
		((OUTPUT_RATE * OUTPUT_CHANNELS * 2) >> 16) & 0xff, 0,
		OUTPUT_CHANNELS * 2, 0, 16, 0,
		'd', 'a', 't', 'a', 0, 0, 0, 0,
	};
	const char *path = config_string("debug.audio_record");

	if (!path || !path[0])
		return;
	record_file = fopen(path, "wb");
	if (!record_file)
	{
		platform_log("audio: cannot record to %s", path);
		return;
	}
	fwrite(header, 1, sizeof(header), record_file);
	fflush(record_file);
	platform_log("audio: recording to %s", path);
}

static void record(const float *output, unsigned long frames)
{
	short samples[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	unsigned long index, count = frames * OUTPUT_CHANNELS;

	if (!record_file)
		return;
	if (count > MIX_CHUNK_FRAMES * OUTPUT_CHANNELS)
		count = MIX_CHUNK_FRAMES * OUTPUT_CHANNELS;
	for (index = 0; index < count; index++)
	{
		/* (on 5.1, the front pair) rounded to the nearest step, at the
		scale the mixer reads samples at, so that a voice played as it is is
		recorded as it is; cutting toward zero took half a step off each
		sample on average */
		float sample = output[index / OUTPUT_CHANNELS * output_channels + index % OUTPUT_CHANNELS] * 32768.0f;

		samples[index] = (short)lrintf(sample > 32767.0f ? 32767.0f : (sample < -32768.0f ? -32768.0f : sample));
	}
	fwrite(samples, sizeof(short), count, record_file);
	if ((record_frames + frames) / OUTPUT_RATE != record_frames / OUTPUT_RATE)
	{
		record_frames += frames;
		record_sizes();
	}
	else
	{
		record_frames += frames;
	}
}

/* ---------- the hand-off

The mixer runs on the device's thread, which must answer each request
within the buffer (5.3 ms at 256 frames), and the game's threads run at an
ordinary priority: a game thread holding a lock the mixer waits on can be
set aside by the scheduler for tens of milliseconds, and the device runs
dry. So the mixer never waits on the game's threads. What the game sets
(each voice's settings, the listener, the environment) and submits (packets,
flushes, voices made and released) goes to its own side, under voices_lock,
held only to set it. At the start of each mix the mixer tries the lock and,
if it is free, takes the game's side over to its own (voices_hand_off), as
the lock held for the whole mix used to give it; if a game thread holds it,
the mixer mixes what it took last time and takes the changes at the next
mix, a buffer later. After the mix it tries the lock again to say which
packets it finished (voices_publish), for the game to complete. The packets
the game lets go of while the mixer may still hold them (flushed, or a
voice released) are freed once the mixer has taken a hand-off since
(retire). Without a device (silent_clock_thread, and a mix paced by the
frames drawn) the mixer waits for the lock, as it did. */

/* (voices_lock) the hand-offs the mixer has taken; (the mixer's thread)
those it left for the next mix, a game thread holding the lock */
static unsigned long hand_offs;
static unsigned long hand_offs_skipped;

/* (voices_lock) memory the mixer may still read, freed once it has taken a
hand-off since (retired_free) */
static struct retired_memory
{
	void *memory;
	unsigned long hand_off;
} *retired;
static unsigned long retired_count, retired_size;

/* debug.latency_log (the sound's latency, below): (voices_lock) the next
voice started is measured, from the step at armed_ns; the voice measured,
from act_ns, started at start_ns; the device's request whose buffer first
had it (the mixer's thread sets mixed), and when that buffer was handed
back (handed_ns) */
static struct
{
	Uint64 armed_ns;
	struct sdl_stream *stream;
	Uint64 act_ns, start_ns, request_ns, handed_ns;
	BOOL mixed;
} sound_latency;
/* (the mixer's thread) the device's request whose buffer is being mixed,
until its first mix; the voice measured is in that buffer; the buffer it was
in handed back, for the next hand-off to say if the lock was not free */
static Uint64 sound_latency_request_ns;
static BOOL sound_latency_handing;
static Uint64 sound_latency_handed_ns;

/* debug.audio_log: (voices_lock) each 3D voice playing, where it is from the
listener and its gains as the mixer has them, which the mixer makes at a
hand-off once the game's thread wants them (voices_log) */
#define VOICES_REPORTED 128
struct voice_report
{
	const struct sdl_stream *stream;
	float ahead, right, up, minimum_distance;
	float left_gain, right_gain;
	BOOL hrtf_on;
	struct hrtf_parameters parameters;
	float doppler;
};
static struct
{
	BOOL wanted, ready;
	unsigned long count;
	struct voice_report voices[VOICES_REPORTED];
} voices_report;

/* (the mixer's thread, voices_lock) the settings the game gave the voice */
static void voice_settings_take(struct sdl_stream *stream)
{
	const struct voice_settings *settings = &stream->pending;

	stream->paused = settings->paused;
	stream->frequency = settings->frequency;
	stream->volume = settings->volume;
	stream->mix_left = settings->mix_left;
	stream->mix_right = settings->mix_right;
	stream->stereo_positioned = settings->stereo_positioned;
	stream->stereo_pan = settings->stereo_pan;
	stream->stereo_distance = settings->stereo_distance;
	stream->stereo_distance_fade = settings->stereo_distance_fade;
	memcpy(stream->surround_bins, settings->surround_bins, sizeof(stream->surround_bins));
	stream->mode = settings->mode;
	memcpy(stream->position, settings->position, sizeof(stream->position));
	memcpy(stream->velocity, settings->velocity, sizeof(stream->velocity));
	stream->minimum_distance = settings->minimum_distance;
	stream->maximum_distance = settings->maximum_distance;
	stream->direct = settings->direct;
	stream->direct_hf = settings->direct_hf;
	stream->room = settings->room;
	stream->room_hf = settings->room_hf;
	stream->room_rolloff_factor = settings->room_rolloff_factor;
}

/* (the mixer's thread, voices_lock) the packets the mixer has finished,
marked on the game's side for it to complete: they lead the voice's */
static void voice_finished_publish(struct sdl_stream *stream)
{
	unsigned long first = stream->submitted - stream->packet_count;
	unsigned long position;

	for (position = 0; position < stream->mixing_count; position++)
	{
		const struct voice_packet *packet = &stream->mixing[(stream->mixing_head + position) % MAXIMUM_STREAM_PACKETS];
		unsigned long offset = stream->mixing_next - stream->mixing_count + position - first;

		if (!packet->finished)
			break;
		if (offset < stream->packet_count)
			stream->packets[(stream->packet_head + offset) % MAXIMUM_STREAM_PACKETS].finished = TRUE;
	}
}

/* (the mixer's thread, voices_lock) the game's side of a voice taken over:
a flush, the packets it has completed let go of, those it has submitted
since, and its settings */
static void voice_hand_off(struct sdl_stream *stream)
{
	unsigned long first = stream->submitted - stream->packet_count;
	unsigned long sequence;

	/* flushed: the packets it held let go of, and the voice starts over */
	if (stream->flush)
	{
		stream->mixing_count = 0;
		stream->cursor = 0;
		resampler_reset(stream);
		if (stream->flush == VOICE_FLUSHED)
			stream->flush = 0;
	}
	/* (the packets numbered from the first submitted: those before the
	game's first it has completed, which were finished) */
	while (stream->mixing_count && (long)(first - (stream->mixing_next - stream->mixing_count)) > 0)
	{
		stream->mixing_head = (stream->mixing_head + 1) % MAXIMUM_STREAM_PACKETS;
		stream->mixing_count--;
	}
	voice_finished_publish(stream);
	if (stream->flush != VOICE_FLUSHING)
	{
		sequence = (long)(first - stream->mixing_next) > 0 ? first : stream->mixing_next;
		for (; sequence != stream->submitted && stream->mixing_count < MAXIMUM_STREAM_PACKETS; sequence++)
		{
			const struct voice_packet *packet =
				&stream->packets[(stream->packet_head + (sequence - first)) % MAXIMUM_STREAM_PACKETS];

			/* (a stream that ran dry starts over) */
			if (packet->restart)
			{
				stream->cursor = 0;
				resampler_reset(stream);
				stream->gains_valid = FALSE;
			}
			stream->mixing[(stream->mixing_head + stream->mixing_count) % MAXIMUM_STREAM_PACKETS] = *packet;
			stream->mixing[(stream->mixing_head + stream->mixing_count) % MAXIMUM_STREAM_PACKETS].finished = FALSE;
			stream->mixing_count++;
		}
		stream->mixing_next = sequence;
	}
	if (stream->pending_changed)
	{
		voice_settings_take(stream);
		stream->pending_changed = FALSE;
	}
}

/* (the mixer's thread, voices_lock) debug.audio_log's voices, as the mixer
has them now */
static void voices_report_make(void)
{
	struct sdl_stream *stream;

	voices_report.count = 0;
	for (stream = mixing_streams; stream && voices_report.count < VOICES_REPORTED; stream = stream->mixing_link)
	{
		struct voice_report *report = &voices_report.voices[voices_report.count];
		float offset[3], right_axis[3], room, direct_lowpass, room_lowpass;
		int axis;

		if (!stream->has_3d || stream->mode == DS3DMODE_DISABLE || stream->paused || !stream->mixing_count)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			offset[axis] = stream->position[axis] -
				(stream->mode == DS3DMODE_HEADRELATIVE ? 0.0f : listener.position[axis]);
		}
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		report->stream = stream;
		report->ahead = dot3(offset, listener.front) * listener.distance_factor;
		report->right = dot3(offset, right_axis) * listener.distance_factor;
		report->up = dot3(offset, listener.top) * listener.distance_factor;
		report->minimum_distance = stream->minimum_distance * listener.distance_factor;
		voice_gains(stream, &report->left_gain, &report->right_gain, &room, &direct_lowpass, &room_lowpass,
			&report->hrtf_on, &report->parameters);
		/* (through the HRTF, each ear's gain is its own, the pan's not
		heard) */
		if (report->hrtf_on && hrtf_enabled && output_channels == 2)
			report->left_gain = report->right_gain = report->parameters.gain;
		report->doppler = stream->current_doppler;
		voices_report.count++;
	}
	voices_report.ready = TRUE;
	voices_report.wanted = FALSE;
}

/* (the mixer's thread) the game's side taken over, at the start of a mix:
waiting for the lock (wait: without a device), or only if it is free */
static void voices_hand_off(BOOL wait)
{
	struct sdl_stream *stream;

	if (wait)
		pthread_mutex_lock(&voices_lock);
	else if (pthread_mutex_trylock(&voices_lock))
	{
		hand_offs_skipped++;
		return;
	}
	for (stream = streams; stream; stream = stream->next)
	{
		voice_hand_off(stream);
		stream->mixing_link = stream->next;
	}
	mixing_streams = streams;
	if (listener_changed)
	{
		listener = listener_set;
		listener_changed = FALSE;
	}
	if (environment_changed)
	{
		environment = environment_set;
		environment_serial++;
		environment_changed = FALSE;
	}
	/* (debug.latency_log: the voice measured, in this buffer from its start) */
	if (sound_latency_request_ns && sound_latency.stream && !sound_latency.mixed && !sound_latency.stream->paused &&
		sound_latency.stream->sample_rate && voice_has_frames(sound_latency.stream))
	{
		sound_latency.mixed = TRUE;
		sound_latency.request_ns = sound_latency_request_ns;
		sound_latency_handing = TRUE;
	}
	if (sound_latency_handed_ns)
	{
		if (sound_latency.mixed && !sound_latency.handed_ns)
			sound_latency.handed_ns = sound_latency_handed_ns;
		sound_latency_handed_ns = 0;
	}
	if (voices_report.wanted)
		voices_report_make();
	hand_offs++;
	pthread_mutex_unlock(&voices_lock);
}

/* (the mixer's thread) after a mix, the packets it finished said to the
game, if the lock is free (else at the next hand-off) */
static void voices_publish(BOOL wait)
{
	struct sdl_stream *stream;

	if (wait)
		pthread_mutex_lock(&voices_lock);
	else if (pthread_mutex_trylock(&voices_lock))
		return;
	for (stream = mixing_streams; stream; stream = stream->mixing_link)
		voice_finished_publish(stream);
	pthread_mutex_unlock(&voices_lock);
}

/* (the game's thread, voices_lock) memory the mixer may still read freed
once it has taken a hand-off since, which lets go of it (a flushed packet's
samples, a voice released); kept, never freed, if there is no room to keep
it */
static void retire(void *memory)
{
	if (!memory)
		return;
	if (retired_count == retired_size)
	{
		unsigned long size = retired_size ? retired_size * 2 : 64;
		struct retired_memory *grown = realloc(retired, size * sizeof(*retired));

		if (!grown)
			return;
		retired = grown;
		retired_size = size;
	}
	retired[retired_count].memory = memory;
	retired[retired_count].hand_off = hand_offs;
	retired_count++;
}

/* (the game's thread) what the mixer has let go of, freed */
static void retired_free(void)
{
	unsigned long index, kept = 0;

	pthread_mutex_lock(&voices_lock);
	for (index = 0; index < retired_count; index++)
	{
		if (retired[index].hand_off != hand_offs)
			free(retired[index].memory);
		else
			retired[kept++] = retired[index];
	}
	retired_count = kept;
	pthread_mutex_unlock(&voices_lock);
}

/* debug.audio_log: the time the mixes took (nanoseconds) and the frames
they mixed, since the log last said */
static Uint64 mix_nanoseconds;
static unsigned long mix_frames;

static void mix_buffers(float *output, unsigned long frames);

/* a mix of frames into output: the game's side taken over first, waiting
for it only without a device (wait) */
static void mix(float *output, unsigned long frames, BOOL wait)
{
	struct sdl_stream *stream;
	BOOL enabled = reverb_enabled;
	Uint64 started = SDL_GetTicksNS();

	memset(output, 0, frames * output_channels * sizeof(float));
	memset(reverb.send, 0, frames * sizeof(float));
	voices_hand_off(wait);
	for (stream = mixing_streams; stream; stream = stream->mixing_link)
		mix_voice(stream, output, reverb.send, frames);
	voices_publish(wait);
	/* (the game's buffers: the loading screen's chant) */
	mix_buffers(output, frames);
	/* a new environment, once the fade to the last is over */
	if (reverb.serial != environment_serial && !reverb.fade)
		reverb_update(&environment, environment_serial);
	/* the reverb runs while something reverberates, and stops once it is
	turned off and faded out, or quiet */
	if (!reverb.silent || (enabled && reverb_sent(reverb.send, frames)))
	{
		reverb_process(reverb.send, output, frames, enabled ? 1.0f : 0.0f);
		if ((!enabled && reverb.level <= 0.0f) || reverb.quiet > REVERB_QUIET_FRAMES)
			reverb_clear();
	}
	/* the players' voices (voice_audio.c), dry, under the limiter; on 5.1
	from the front pair */
	if (output_channels == 6)
	{
		float voices[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
		unsigned long frame;

		memset(voices, 0, frames * OUTPUT_CHANNELS * sizeof(float));
		voice_audio_mix(voices, frames);
		for (frame = 0; frame < frames; frame++)
		{
			output[frame * 6] += voices[frame * 2];
			output[frame * 6 + 1] += voices[frame * 2 + 1];
		}
	}
	else
	{
		voice_audio_mix(output, frames);
	}
	limit(output, frames);
	record(output, frames);
	mix_nanoseconds += SDL_GetTicksNS() - started;
	mix_frames += frames;
}

/* ---------- the output's channels

audio.channels (setting: "stereo", "5.1" or "auto"; anything else is
stereo) and the channels of the device the sound plays on (device_channels;
0 when it cannot say): 6 for 5.1, 2 for stereo. "auto" is 5.1 on a device
of six channels or more (a 5.1 or 7.1 receiver, a surround headset: SDL
lays the 5.1 out on a 7.1's speakers), stereo on fewer (speakers, a pair
of headphones) and when it cannot say. On 5.1 the speakers are panned
(surround_pan), never the HRTF, which is for a pair of ears. */
static unsigned long output_channels_choose(const char *setting, int device_channels)
{
	if (setting && !strcmp(setting, "5.1"))
		return 6;
	if (setting && !strcmp(setting, "auto") && device_channels >= 6)
		return 6;
	return 2;
}

/* ---------- output */

static SDL_AudioStream *audio_stream;
static BOOL audio_started = FALSE;
/* the device it plays on (audio.output_device), and when it was looked at */
static char audio_device_name[PLATFORM_AUDIO_DEVICE_NAME_SIZE];
static unsigned long audio_device_read_at = (unsigned long)-1;

/* ---------- the device's timing

The device asks for frames as it plays them out (audio_callback): a request
for a buffer comes as it has room for one, the frames queued ahead of it
(device_timing: SDL's driver keeps a buffer or more) still to play. A
request answered after those have played out leaves the device nothing to
play: it plays silence (a dropout, heard as a click or a crackle), and then
plays the frames late (PipeWire, WASAPI) or skips them (CoreAudio's
AudioQueue).

So each request is timed against the device's clock, which the requests
themselves keep: the lag of a request, its time since the first request
less the time of the frames asked for before it, is least when it comes
as soon as it can (TIMING_BLOCK_NS: the least of each block of requests, a
fall followed at once; a rise followed once the next block keeps it too,
which a device that plays the frames late after a dropout does, and the
device's clock running a little slower than the computer's). An answer's
lateness, its request's lag over that least plus the time to answer it, is
what the device has waited of what it had queued: more than that is a
dropout, counted once until an answer is in time again with half to spare.
More than TIMING_PAUSE_NS is the device stopping (the computer asleep, the
sound moved to another device) rather than a dropout. The game's thread
logs the dropouts and, with audio.buffer_frames 0, grows the buffer
(timing_check). */
#define TIMING_BLOCK_NS 250000000LL
#define TIMING_BLOCK_REQUESTS 8
#define TIMING_RISE_NS 1000000LL
#define TIMING_PAUSE_NS 250000000LL

/* the device the sound plays on (device_timing_read): its driver, its
buffer (frames) and rate, the time SDL's driver keeps queued ahead of the
buffer it asks for and the system's latency after (microseconds; -1 not
known), and the times the Mac's output unit was set up for it (0 for SDL's
drivers: device_timing_changed) */
static char device_driver[32];
static int device_timing[5];
static BOOL device_timing_valid;

static struct
{
	/* (the mixer's thread) the time the device has queued ahead of a
	request (nanoseconds); the first request since the device opened, the
	frames asked for since, the request being answered and its lag; the
	least lag followed, this block's and the last's; whether an answer was
	late and none in time since */
	Sint64 slack_ns;
	Sint64 first_ns;
	Uint64 frames;
	Sint64 request_ns, lag_ns;
	Sint64 baseline_ns, least_ns, last_least_ns;
	BOOL baseline_valid;
	Sint64 block_ns;
	unsigned long block_requests;
	BOOL late;
	/* (the mixer's thread) what it has counted since it last added them to
	those below: the mixer never waits for timing_lock (the hand-off) */
	unsigned long counted_dropouts;
	double counted_silence_ms;
	unsigned long counted_pauses;
	Sint64 counted_latest_ns;
	/* (timing_lock) since the device opened: its dropouts and their silence
	(the lateness beyond the slack), its pauses; the latest answer since the
	game's thread last took it */
	unsigned long dropouts;
	double silence_ms;
	unsigned long pauses;
	Sint64 latest_ns;
} timing;
static pthread_mutex_t timing_lock = PTHREAD_MUTEX_INITIALIZER;

/* (no request can be answered meanwhile: the device opening) */
static void timing_reset(void)
{
	pthread_mutex_lock(&timing_lock);
	memset(&timing, 0, sizeof(timing));
	pthread_mutex_unlock(&timing_lock);
}

/* (the device opened, not yet playing) its slack: what it keeps queued, or
with nothing queued (the Mac's CoreAudio output unit, which asks for each
buffer as its I/O cycle comes) the buffer itself, at the device's rate;
none for SDL's dummy and disk drivers (the tests'), which have no device to
run dry */
static void timing_slack(void)
{
	if (device_timing_valid && (!strcmp(device_driver, "dummy") || !strcmp(device_driver, "disk")))
		timing.slack_ns = INT64_MAX;
	else if (device_timing_valid && device_timing[2] > 0)
		timing.slack_ns = (Sint64)device_timing[2] * 1000;
	else
		timing.slack_ns = (Sint64)((double)(device_timing_valid ? device_timing[0] : 1024) *
			(1.0e9 / (device_timing_valid && device_timing[1] > 0 ? device_timing[1] : OUTPUT_RATE)));
}

/* (the mixer's thread) a request, at now */
static void timing_request(Sint64 now)
{
	Sint64 lag;

	timing.request_ns = now;
	if (!timing.first_ns)
	{
		timing.first_ns = now;
		timing.block_ns = now;
		timing.least_ns = INT64_MAX;
	}
	lag = now - timing.first_ns - (Sint64)((double)timing.frames * (1.0e9 / OUTPUT_RATE));
	timing.lag_ns = lag;
	if (!timing.baseline_valid || lag < timing.baseline_ns)
	{
		timing.baseline_valid = TRUE;
		timing.baseline_ns = lag;
	}
	if (lag < timing.least_ns)
		timing.least_ns = lag;
	if (++timing.block_requests < TIMING_BLOCK_REQUESTS || now - timing.block_ns < TIMING_BLOCK_NS)
		return;
	/* (a small rise followed at once, a larger one once two blocks keep it) */
	if (timing.least_ns - timing.baseline_ns < TIMING_RISE_NS)
		timing.baseline_ns = timing.least_ns;
	else if (timing.last_least_ns - timing.baseline_ns >= TIMING_RISE_NS)
		timing.baseline_ns = timing.least_ns < timing.last_least_ns ? timing.least_ns : timing.last_least_ns;
	timing.last_least_ns = timing.least_ns;
	timing.block_ns = now;
	timing.block_requests = 0;
	timing.least_ns = INT64_MAX;
}

/* (the mixer's thread) the request answered: frames handed back at now */
static void timing_answered(unsigned long frames, Sint64 now)
{
	Sint64 lateness = timing.lag_ns - timing.baseline_ns + (now - timing.request_ns);

	timing.frames += frames;
	if (lateness > timing.counted_latest_ns)
		timing.counted_latest_ns = lateness;
	if (lateness > timing.slack_ns && !timing.late)
	{
		timing.late = TRUE;
		if (lateness - timing.slack_ns > TIMING_PAUSE_NS)
		{
			timing.counted_pauses++;
		}
		else
		{
			timing.counted_dropouts++;
			timing.counted_silence_ms += (double)(lateness - timing.slack_ns) / 1.0e6;
		}
	}
	else if (lateness < timing.slack_ns / 2)
	{
		timing.late = FALSE;
	}
	/* (added for the game's thread if the lock is free, else after the
	next answer) */
	if (pthread_mutex_trylock(&timing_lock))
		return;
	if (timing.counted_latest_ns > timing.latest_ns)
		timing.latest_ns = timing.counted_latest_ns;
	timing.dropouts += timing.counted_dropouts;
	timing.silence_ms += timing.counted_silence_ms;
	timing.pauses += timing.counted_pauses;
	pthread_mutex_unlock(&timing_lock);
	timing.counted_latest_ns = 0;
	timing.counted_dropouts = 0;
	timing.counted_silence_ms = 0.0;
	timing.counted_pauses = 0;
}

/* ---------- the sound's latency (debug.latency_log)

From the simulation step in which the first player's weapon fires
(latency_log.c) to the sound of it heard: the next voice the game starts
(stream_process: the game's sound update, in the frame after the step), the
device's next request (a mix takes a newly started voice from the start of
its buffer), and the frames queued on the device ahead of that buffer and
the system's latency after (device_timing: what SDL's driver keeps queued,
and what the system reports). The time to answer the request is not added:
the buffer plays after those queued ahead, however soon it is handed back,
unless so late that the device runs dry (timing). The mixer marks the voice
as it takes it over (voices_hand_off: sound_latency). */

/* (the host: port/android/host/host_sdl.c) */
int host_sdl_audio_stream_timing(unsigned int stream, char *driver, int driver_size, int *timing);

/* (the game's thread) the voice measured, once its first frames are
handed back to the device: [0] the step, [1] its start, [2] the device's
request, [3] the buffer handed back (SDL's clock, nanoseconds); the frames
queued on the device ahead of the buffer and the system's latency after
(milliseconds; system -1 not known), the device's buffer (frames) and its
dropouts since it opened. FALSE until then */
BOOL dsound_sdl_latency_take(unsigned long long *times, double *queued_ms, double *system_ms, long *buffer_frames,
	unsigned long *dropouts)
{
	BOOL taken = FALSE;

	pthread_mutex_lock(&voices_lock);
	if (sound_latency.handed_ns)
	{
		times[0] = sound_latency.act_ns;
		times[1] = sound_latency.start_ns;
		times[2] = sound_latency.request_ns;
		times[3] = sound_latency.handed_ns;
		memset(&sound_latency, 0, sizeof(sound_latency));
		taken = TRUE;
	}
	pthread_mutex_unlock(&voices_lock);
	if (!taken)
		return FALSE;
	*queued_ms = device_timing_valid ? device_timing[2] / 1000.0 : 0.0;
	*system_ms = device_timing_valid && device_timing[3] >= 0 ? device_timing[3] / 1000.0 : -1.0;
	*buffer_frames = device_timing_valid ? device_timing[0] : 0;
	pthread_mutex_lock(&timing_lock);
	*dropouts = timing.dropouts;
	pthread_mutex_unlock(&timing_lock);
	return TRUE;
}

/* (the game's thread) the next voice started is measured, from act_ns (a
step in which the weapon fired), unless one is being measured */
void dsound_sdl_latency_arm(unsigned long long act_ns)
{
	pthread_mutex_lock(&voices_lock);
	if (!sound_latency.stream && !sound_latency.handed_ns)
		sound_latency.armed_ns = act_ns;
	pthread_mutex_unlock(&voices_lock);
}

/* audio.output_device ("default": the system's; none on Android) */
static const char *audio_device_setting(void)
{
	const char *name = config_string("audio.output_device");

	return name && name[0] ? name : "default";
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	float buffer[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
	unsigned long handed = 0;
	Sint64 now;

	(void)userdata;
	(void)total_amount;
	if (additional_amount <= 0)
		return;
	timing_request((Sint64)SDL_GetTicksNS());
	/* (debug.latency_log: the voice measured, in this buffer from its start,
	as the first mix's hand-off takes it) */
	sound_latency_request_ns = (Uint64)timing.request_ns;
	while (additional_amount > 0)
	{
		unsigned long frames = (unsigned long)additional_amount / (output_channels * sizeof(float));

		if (frames > MIX_CHUNK_FRAMES)
			frames = MIX_CHUNK_FRAMES;
		if (!frames)
			frames = 1;
		mix(buffer, frames, FALSE);
		sound_latency_request_ns = 0;
		SDL_PutAudioStreamData(stream, buffer, (int)(frames * output_channels * sizeof(float)));
		additional_amount -= (int)(frames * output_channels * sizeof(float));
		handed += frames;
	}
	now = (Sint64)SDL_GetTicksNS();
	timing_answered(handed, now);
	/* (the time it was handed back; at the next hand-off if a game thread
	holds the lock) */
	if (sound_latency_handing)
	{
		sound_latency_handing = FALSE;
		if (!pthread_mutex_trylock(&voices_lock))
		{
			if (sound_latency.mixed)
				sound_latency.handed_ns = (Uint64)now;
			pthread_mutex_unlock(&voices_lock);
		}
		else
		{
			sound_latency_handed_ns = (Uint64)now;
		}
	}
}

/* without a device, drain voices in real time */
/* the frames' time fixed (debug.frame_time, debug.fixed_step: footage, the
frames drawn at a fixed time each, slower or faster than the clock, and the
tests): the mix paced by the frames instead, with no device, each frame
owing its time's sound, which the frame waits to be mixed (dsound_sdl_frame),
so that a recording keeps step with the pictures and two runs of the same
input hear the same (port/linux/game/frames_clock.c) */
int frames_clock(unsigned long *milliseconds);
static BOOL paced;
static pthread_mutex_t paced_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t paced_condition = PTHREAD_COND_INITIALIZER;
static double paced_owed;

static void *silent_clock_thread(void *parameter)
{
	float buffer[480 * MAXIMUM_OUTPUT_CHANNELS];
	struct timespec next;

	(void)parameter;
	while (paced)
	{
		pthread_mutex_lock(&paced_lock);
		while (paced_owed < 480.0)
			pthread_cond_wait(&paced_condition, &paced_lock);
		pthread_mutex_unlock(&paced_lock);
		mix(buffer, 480, TRUE);
		pthread_mutex_lock(&paced_lock);
		paced_owed -= 480.0;
		pthread_cond_broadcast(&paced_condition);
		pthread_mutex_unlock(&paced_lock);
	}
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		mix(buffer, 480, TRUE);
		next.tv_nsec += 10000000L;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}
	return NULL;
}

/* (the main thread, each frame: frames_clock.c) its time's sound owed to a
paced mix, waited for */
void dsound_sdl_frame(double seconds)
{
	if (!paced)
		return;
	pthread_mutex_lock(&paced_lock);
	paced_owed += seconds * OUTPUT_RATE;
	pthread_cond_broadcast(&paced_condition);
	while (paced_owed >= 480.0)
		pthread_cond_wait(&paced_condition, &paced_lock);
	pthread_mutex_unlock(&paced_lock);
}

/* the frames of sound recorded so far (debug.audio_record) */
unsigned long dsound_sdl_recorded_frames(void)
{
	return record_frames;
}

/* audio.channels on Android: stereo; elsewhere what output_channels_choose
makes of the setting and of the channels the device has, which the host's
SDL says (host_desktop.c: SDL_GetAudioDeviceFormat, the format the device
plays in, not one SDL would convert to) */
static unsigned long choose_output_channels(SDL_AudioDeviceID device)
{
#ifdef HALO_ANDROID
	(void)device;
	return 2;
#else
	const char *setting = config_string("audio.channels");
	SDL_AudioSpec spec;
	int device_channels = 0;

	if (setting && !strcmp(setting, "auto") && SDL_GetAudioDeviceFormat(device, &spec, NULL))
		device_channels = spec.channels;
	if (setting && strcmp(setting, "auto") && strcmp(setting, "stereo") && strcmp(setting, "5.1"))
		platform_log("audio.channels \"%s\" is not \"auto\", \"stereo\" or \"5.1\"; using stereo", setting);
	if (setting && !strcmp(setting, "auto"))
	{
		platform_log("sound: the device has %d channels%s", device_channels,
			device_channels ? "" : " (it cannot say: stereo)");
	}
	return output_channels_choose(setting, device_channels);
#endif
}

/* ---------- the device's buffer

The frames the device asks for at a time (audio.buffer_frames: SDL's
SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, which the host's SDL takes). A request
is handed to a thread of the host's that runs the guest's mix
(host_sdl.c), and the sound plays after the frames SDL's driver keeps
queued ahead of it, a buffer or more (device_timing): a smaller buffer is
heard sooner, but each request must be answered sooner too, or the device
runs dry (timing). 0, the default: BUFFER_FRAMES_AUTO to start, doubled up
to BUFFER_FRAMES_AUTO_MOST each time the device runs dry, which the log
says (timing_check; the sound stops for a moment as the device opens
again). On Android and iOS 1024: there 512 broke the menus' music up (the
thread that runs the guest getting too little time). */
#ifdef HALO_WINDOWS
#define BUFFER_FRAMES_AUTO 512
#else
#define BUFFER_FRAMES_AUTO 256
#endif
#define BUFFER_FRAMES_AUTO_MOST 2048
static long buffer_frames = 1024;
static BOOL buffer_frames_auto;
/* when the device last opened (SDL_GetTicks) */
static Uint64 audio_opened_at;

static void buffer_frames_choose(void)
{
#if defined(HALO_ANDROID) || defined(HALO_IOS)
	buffer_frames = 1024;
#else
	buffer_frames = config_integer("audio.buffer_frames");
	buffer_frames_auto = buffer_frames <= 0;
	if (buffer_frames_auto)
		buffer_frames = BUFFER_FRAMES_AUTO;
	if (buffer_frames < 64)
		buffer_frames = 64;
	if (buffer_frames > 8192)
		buffer_frames = 8192;
#endif
}

/* (the device opened: device_timing) */
static void device_timing_read(SDL_AudioStream *stream)
{
	char system[32] = "not known";

	device_timing_valid = host_sdl_audio_stream_timing((unsigned int)(uintptr_t)stream, device_driver,
		(int)sizeof(device_driver), device_timing) != 0;
	/* (the mixer's thread may be answering: device_timing_changed) */
	pthread_mutex_lock(&timing_lock);
	timing_slack();
	pthread_mutex_unlock(&timing_lock);
	if (!device_timing_valid)
		return;
	if (device_timing[3] >= 0)
		snprintf(system, sizeof(system), "%.1f ms", device_timing[3] / 1000.0);
	platform_log("sound: the device (%s) asks for %d frames at a time at %d Hz (audio.buffer_frames %ld%s); "
		"%.1f ms queued ahead of them, the system's latency after %s", device_driver, device_timing[0],
		device_timing[1], buffer_frames_auto ? 0L : buffer_frames, buffer_frames_auto ? ", automatic" : "",
		device_timing[2] / 1000.0, system);
}

/* the device (audio.output_device, else the system's default) opened at
buffer_frames, playing; NULL when none opens */
static SDL_AudioStream *audio_open(void)
{
	SDL_AudioSpec spec;
	SDL_AudioStream *stream;
	char frames[16];

	spec.format = SDL_AUDIO_F32;
	/* (on a device opened again, the game's mix bins are set for the
	channels the sound started with; SDL turns them into the device's) */
	spec.channels = (int)output_channels;
	spec.freq = OUTPUT_RATE;
	snprintf(frames, sizeof(frames), "%ld", buffer_frames);
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
	timing_reset();
	audio_opened_at = SDL_GetTicks();
	stream = SDL_OpenAudioDeviceStream(platform_audio_device(FALSE, audio_device_name), &spec, audio_callback, NULL);
	if (!stream && strcmp(audio_device_name, "default"))
		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
	if (!stream)
		return NULL;
	device_timing_read(stream);
	SDL_ResumeAudioStreamDevice(stream);
	return stream;
}

/* (the event thread, each frame) the Mac's output unit set up again
(port/macos/host_audio_output.c: another default device, the device's rate
or channels changed): its timing read again, and the dropout of the change
(the unit stops meanwhile) kept from growing the buffer, as at the device's
opening. And the system's latency, where it is known,
read again as it changes (Bluetooth headphones', or iOS's route: AirPods
connected, the speaker after): logged, and used by the sound's latency log
from then on; SDL's drivers asked once a second */
#define DEVICE_LATENCY_CHECK_MS 1000
static void device_timing_changed(void)
{
	static Uint64 checked_at;
	int timing_now[5] = { 0 };
	char driver[sizeof(device_driver)];
	BOOL unit;
	Uint64 now = SDL_GetTicks();

	if (!device_timing_valid || device_timing[3] < 0)
		return;
	unit = !strcmp(device_driver, "coreaudio unit");
	if (!unit && now - checked_at < DEVICE_LATENCY_CHECK_MS)
		return;
	checked_at = now;
	if (!host_sdl_audio_stream_timing((unsigned int)(uintptr_t)audio_stream, driver, (int)sizeof(driver), timing_now))
		return;
	if (unit && timing_now[4] != device_timing[4])
	{
		platform_log("sound: the output device changed");
		device_timing_read(audio_stream);
		audio_opened_at = now;
		return;
	}
	/* (a change of a millisecond or more) */
	if (timing_now[3] < 0 || abs(timing_now[3] - device_timing[3]) < 1000)
		return;
	platform_log("sound: the system's latency is now %.1f ms (was %.1f ms)", timing_now[3] / 1000.0,
		device_timing[3] / 1000.0);
	device_timing[3] = timing_now[3];
}

/* (the event thread, each frame) the device's dropouts since the log last
said, at most every TIMING_LOG_MS; with audio.buffer_frames 0, the buffer
doubled at a dropout, the device opened again, but not for those in the
first TIMING_SETTLE_MS after it opened (the game starting up) */
#define TIMING_LOG_MS 10000
#define TIMING_SETTLE_MS 5000
static void timing_check(void)
{
	static Uint64 logged_at;
	static unsigned long logged_dropouts, settled_dropouts;
	unsigned long dropouts;
	double silence_ms;
	Uint64 now = SDL_GetTicks();

	pthread_mutex_lock(&timing_lock);
	dropouts = timing.dropouts;
	silence_ms = timing.silence_ms;
	pthread_mutex_unlock(&timing_lock);
	if (dropouts < logged_dropouts)
		logged_dropouts = 0;
	if (now - audio_opened_at < TIMING_SETTLE_MS)
		settled_dropouts = dropouts;
	if (dropouts == logged_dropouts)
		return;
	if (buffer_frames_auto && dropouts > settled_dropouts && buffer_frames < BUFFER_FRAMES_AUTO_MOST)
	{
		long from = buffer_frames;

		buffer_frames *= 2;
		if (buffer_frames > BUFFER_FRAMES_AUTO_MOST)
			buffer_frames = BUFFER_FRAMES_AUTO_MOST;
		platform_log("sound: the device ran dry (%.1f ms of silence since it opened): its buffer grows from %ld to "
			"%ld frames (audio.buffer_frames 0, automatic)", silence_ms, from, buffer_frames);
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = audio_open();
		logged_dropouts = 0;
		logged_at = now;
		if (!audio_stream)
		{
			pthread_t thread;

			platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
			pthread_create(&thread, NULL, silent_clock_thread, NULL);
			pthread_detach(thread);
		}
		return;
	}
	if (logged_at && now - logged_at < TIMING_LOG_MS)
		return;
	platform_log("sound: the device ran dry %lu time%s since it opened, %.1f ms of silence in all (a buffer of %ld "
		"frames: audio.buffer_frames)", dropouts, dropouts == 1 ? "" : "s", silence_ms, buffer_frames);
	logged_dropouts = dropouts;
	logged_at = now;
}

static void audio_start(void)
{
	if (audio_started)
		return;
	audio_started = TRUE;
	master_volume = (float)config_real("audio.volume");
	reverb_enabled = config_boolean("audio.reverb");
	doppler_enabled = config_boolean("audio.doppler");
	resampler_mode = resampler_mode_read();
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();
	hrtf_initialize();
	record_open();
	paced = frames_clock(NULL);
	if (paced)
		platform_log("sound: mixed in step with the frames (debug.frame_time, debug.fixed_step), no device");

	if (!paced && config_boolean("audio.enabled") && platform_sdl_initialize())
	{
		snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
		output_channels = choose_output_channels(platform_audio_device(FALSE, audio_device_name));
		platform_log("sound: %s", output_channels == 6 ? "5.1 surround" : "stereo");
		buffer_frames_choose();
		audio_device_read_at = config_changes();
		audio_stream = audio_open();
		if (audio_stream)
			return;
		platform_log("cannot open an audio device (%s); sound is silent", SDL_GetError());
		output_channels = 2;
	}
	{
		pthread_t thread;

		pthread_create(&thread, NULL, silent_clock_thread, NULL);
		pthread_detach(thread);
	}
}

/* (the event thread, each frame: sdl_platform.c) audio.output_device
changed (Settings > Audio): the sound goes on on the new device, else the
system's default */
void dsound_sdl_output_device_check(void)
{
	if (!audio_stream)
		return;
	device_timing_changed();
	timing_check();
	if (!audio_stream || audio_device_read_at == config_changes())
		return;
	audio_device_read_at = config_changes();
	if (!strcmp(audio_device_name, audio_device_setting()))
		return;
	snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
	SDL_DestroyAudioStream(audio_stream);
	audio_stream = audio_open();
	if (audio_stream)
	{
		platform_log("audio: playing on %s", audio_device_name);
	}
	else
	{
		pthread_t thread;

		/* (none at all: the voices drained in real time, as at the start) */
		platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
		pthread_create(&thread, NULL, silent_clock_thread, NULL);
		pthread_detach(thread);
	}
}

/* ---------- completion */

/* (voices_lock) a packet's samples let go of: freed if the mixer finished it
(it reads them no more), else once it has taken a hand-off since (it may
hold the packet until then) */
static void packet_release(struct voice_packet *entry)
{
	if (entry->finished)
		free(entry->samples);
	else
		retire(entry->samples);
	entry->samples = NULL;
}

/* completes the head packet; called with the lock held, which the game's
callback runs without */
static void stream_complete_head(struct sdl_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	packet_release(entry);
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		pthread_mutex_unlock(&voices_lock);
		stream->callback(stream->context, packet.pContext, status);
		pthread_mutex_lock(&voices_lock);
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

static void streams_complete_finished(void)
{
	struct sdl_stream *stream;

	pthread_mutex_lock(&voices_lock);
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	pthread_mutex_unlock(&voices_lock);
}

/* ---------- stream interface */

static struct sdl_stream *stream_from_interface(void *stream)
{
	return (struct sdl_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	ULONG count;

	pthread_mutex_lock(&voices_lock);
	count = ++stream->reference_count;
	pthread_mutex_unlock(&voices_lock);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct sdl_stream **link;
	ULONG count;

	pthread_mutex_lock(&voices_lock);
	count = --stream->reference_count;
	pthread_mutex_unlock(&voices_lock);
	if (count)
		return count;

	stream_flush(object);
	pthread_mutex_lock(&voices_lock);
	if (sound_latency.stream == stream)
		memset(&sound_latency, 0, sizeof(sound_latency));
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			break;
		}
	}
	/* (the mixer may hold it until its next hand-off) */
	retire(stream);
	pthread_mutex_unlock(&voices_lock);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct sdl_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct sdl_stream *stream = stream_from_interface(object);

	pthread_mutex_lock(&voices_lock);
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	pthread_mutex_unlock(&voices_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames = 0;
	short *samples;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* decode outside the lock */
	samples = stream->adpcm ?
		decode_adpcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames) :
		decode_pcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames);
	pthread_mutex_lock(&voices_lock);
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		pthread_mutex_unlock(&voices_lock);
		free(samples);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->finished = FALSE;
	/* a stream that ran dry starts over, as the mixer takes the packet
	(voice_hand_off) */
	entry->restart = !stream->packet_count;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		/* (debug.latency_log: the voice started after the weapon fired) */
		if (sound_latency.armed_ns && !sound_latency.stream)
		{
			sound_latency.stream = stream;
			sound_latency.act_ns = sound_latency.armed_ns;
			sound_latency.start_ns = SDL_GetTicksNS();
			sound_latency.armed_ns = 0;
		}
	}
	stream->packet_count++;
	stream->submitted++;
	pthread_mutex_unlock(&voices_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);

	pthread_mutex_lock(&voices_lock);
	/* (the mixer takes none of the packets while they are completed, the
	lock let go for the game's callback; once they are, it lets go of those
	it holds and starts the voice over: voice_hand_off) */
	stream->flush = VOICE_FLUSHING;
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		/* Cancellation is synchronous. SUCCESS would let the game's
		completion callback refill the stream we are stopping. */
		stream_complete_head(stream, XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->flush = VOICE_FLUSHED;
	/* (the next sound on the channel says where it is) */
	stream->pending.stereo_positioned = FALSE;
	stream->pending_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return S_OK;
}

/* A stereo voice of a sound in the world (`positioned`) is panned towards
it, `pan` from -1 (left) to 1 (right), and muffled and reverberated as a 3D
voice is (its I3DL2 source, which the game sets as a 3D channel's), its room
send rolling off from `minimum_distance` with `distance`. The game fades its
volume with distance itself, by `distance_fade`. Not positioned, it plays as
the Xbox played every stereo sound, unpanned and dry. (sound_manager.c,
update_channels: sound_dsound_xbox.c calls this.) */
void dsound_sdl_stream_set_stereo_position(IDirectSoundStream *object, BOOL positioned, float pan,
	float distance, float minimum_distance, float distance_fade)
{
	struct sdl_stream *stream = stream_from_interface(object);

	pthread_mutex_lock(&voices_lock);
	stream->pending.stereo_positioned = positioned && stream->channels == 2;
	stream->pending.stereo_pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
	stream->pending.stereo_distance = distance > 0.0f ? distance : 0.0f;
	stream->pending.stereo_distance_fade = distance_fade < 0.0f ? 0.0f : (distance_fade > 1.0f ? 1.0f : distance_fade);
	/* (as the game gives a 3D channel: no maximum) */
	stream->pending.minimum_distance = minimum_distance > 0.0f ? minimum_distance : 0.0f;
	stream->pending.maximum_distance = 3.4e38f;
	stream->pending_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct sdl_direct_sound
{
	ULONG reference_count;
};

static struct sdl_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

/* debug.audio_log: each 3D voice playing (a sound placed in the world),
where it is from the listener and its gains, as the mixer had them at its
last hand-off (voices_report_make), logged without the lock */
static void voices_log(void)
{
	static Uint64 logged_at;
	static struct voice_report reported[VOICES_REPORTED];
	unsigned long count = 0, index;
	Uint64 now = SDL_GetTicks();

	if (now - logged_at < 200)
		return;
	logged_at = now;
	/* (what the mixing costs: its time over the time it mixed; and where
	the recording is) */
	if (mix_frames)
	{
		char recorded[48] = "";
		Sint64 latest_ns;
		unsigned long dropouts;
		double silence_ms;

		if (record_file)
			snprintf(recorded, sizeof(recorded), "; %.2f s recorded", (double)record_frames / OUTPUT_RATE);
		pthread_mutex_lock(&timing_lock);
		latest_ns = timing.latest_ns;
		timing.latest_ns = 0;
		dropouts = timing.dropouts;
		silence_ms = timing.silence_ms;
		pthread_mutex_unlock(&timing_lock);
		platform_log("audio: mixing took %.2f%% of the time it mixed (the HRTF %s)%s; the device waited %.2f ms "
			"at most of the %.2f ms it had queued, %lu dropouts (%.1f ms) since it opened; %lu hand-offs left for "
			"the next mix (a game thread held the lock)", 100.0 *
			(double)mix_nanoseconds / ((double)mix_frames * 1.0e9 / OUTPUT_RATE), hrtf_enabled ? "on" : "off",
			recorded, (double)latest_ns / 1.0e6, (double)timing.slack_ns / 1.0e6, dropouts, silence_ms,
			hand_offs_skipped);
		mix_nanoseconds = 0;
		mix_frames = 0;
	}
	pthread_mutex_lock(&voices_lock);
	if (voices_report.ready)
	{
		count = voices_report.count;
		memcpy(reported, voices_report.voices, count * sizeof(reported[0]));
		voices_report.ready = FALSE;
	}
	voices_report.wanted = TRUE;
	pthread_mutex_unlock(&voices_lock);
	for (index = 0; index < count; index++)
	{
		const struct voice_report *report = &reported[index];
		const struct hrtf_parameters *parameters = &report->parameters;
		char heard[160] = "";

		if (report->hrtf_on)
		{
			/* (the shadows' gains at the highest frequencies: (b0 - b1) /
			(1 - a1)) */
			snprintf(heard, sizeof(heard), "; HRTF: the right ear %+.0f us after the left, high frequencies left "
				"%+.1f dB, right %+.1f dB, notch %.0f Hz, behind %+.1f dB",
				(parameters->delay[1] - parameters->delay[0]) * 1.0e6f / OUTPUT_RATE,
				20.0f * log10f((parameters->shadow_b0[0] - parameters->shadow_b1[0]) / (1.0f - hrtf.shadow_a1)),
				20.0f * log10f((parameters->shadow_b0[1] - parameters->shadow_b1[1]) / (1.0f - hrtf.shadow_a1)),
				parameters->notch_frequency, 20.0f * log10f(1.0f - parameters->rear * (1.0f - (1.0f - hrtf.rear_lowpass) /
				(1.0f + hrtf.rear_lowpass))));
		}
		platform_log("audio: voice %p: %.2f m ahead, %.2f right, %.2f up (minimum distance %.2f m); gains left %.1f "
			"dB, right %.1f dB; Doppler x%.3f%s", (const void *)report->stream, report->ahead, report->right,
			report->up, report->minimum_distance,
			report->left_gain > 0.0f ? 20.0f * log10f(report->left_gain) : -999.0f,
			report->right_gain > 0.0f ? 20.0f * log10f(report->right_gain) : -999.0f, report->doppler, heard);
	}
}

#ifdef HALO_VR
int halo_vr_running(void);
#endif

/* (the host: port/android/host/host_audio_route.c) what the system's sound
plays on: 1 headphones, 0 speakers, -1 unknown; kept current by the host as
the output device changes, and cheap to ask */
int host_audio_headphones(void);

VOID WINAPI DirectSoundDoWork(void)
{
	static unsigned long volume_read_at = (unsigned long)-1;
	static BOOL log_voices;
	static char spatial_setting[16] = "auto";
	static int logged_headphones = -2;
	static BOOL logged_hrtf;
	BOOL vr_running = FALSE, vr_hrtf = FALSE;
	int headphones;
#ifdef HALO_VR
	static BOOL hrtf_setting;
#endif

	/* (audio.volume, audio.reverb, audio.doppler and audio.resampler read
	again when the settings change: on the game's thread, not the mixer's,
	whose lock the config's file I/O would hold) */
	if (volume_read_at != config_changes())
	{
		volume_read_at = config_changes();
		master_volume = (float)config_real("audio.volume");
		reverb_enabled = config_boolean("audio.reverb");
		doppler_enabled = config_boolean("audio.doppler");
		log_voices = config_boolean("debug.audio_log");
		snprintf(spatial_setting, sizeof(spatial_setting), "%s", config_string("audio.spatial"));
#ifdef HALO_VR
		hrtf_setting = config_boolean("vr.hrtf");
#endif
		resampler_mode = resampler_mode_read();
	}
#ifdef HALO_VR
	/* (in the VR mode, vr.hrtf, as before) */
	vr_running = halo_vr_running() != 0;
	vr_hrtf = hrtf_setting;
#endif
	/* the HRTF on headphones, the pan on speakers (audio.spatial), each
	frame: the host keeps what the sound plays on, and the mixer crossfades
	each voice from one to the other as it changes */
	headphones = host_audio_headphones();
	/* (5.1 is panned around its speakers whatever audio.spatial says: the
	HRTF is for a pair of ears) */
	hrtf_enabled = output_channels != 6 && spatial_hrtf(spatial_setting, headphones, vr_running, vr_hrtf);
	if (headphones != logged_headphones || hrtf_enabled != logged_hrtf)
	{
		logged_headphones = headphones;
		logged_hrtf = hrtf_enabled;
		platform_log("audio: the sound plays on %s; the world's sounds %s (audio.spatial \"%s\"%s)",
			headphones > 0 ? "headphones" : headphones == 0 ? "speakers" : "a device not known (speakers)",
			output_channels == 6 ? "panned around the 5.1 speakers" : hrtf_enabled ? "through the HRTF" : "panned",
			spatial_setting, vr_running ? ", the VR mode: vr.hrtf" : "");
	}
	if (log_voices)
		voices_log();
	streams_complete_finished();
	retired_free();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = output_channels == 6 ? DSSPEAKER_ENABLE_AC3 : DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&voices_lock);
	environment_set = *listener_properties;
	environment_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&voices_lock);
	listener_set.distance_factor = factor > 0.0f ? factor : 1.0f;
	listener_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&voices_lock);
	listener_set.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	listener_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&voices_lock);
	listener_set.position[0] = x;
	listener_set.position[1] = y;
	listener_set.position[2] = z;
	listener_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&voices_lock);
	listener_set.velocity[0] = x;
	listener_set.velocity[1] = y;
	listener_set.velocity[2] = z;
	listener_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	pthread_mutex_lock(&voices_lock);
	listener_set.front[0] = x_front;
	listener_set.front[1] = y_front;
	listener_set.front[2] = z_front;
	listener_set.top[0] = x_top;
	listener_set.top[1] = y_top;
	listener_set.top[2] = z_top;
	normalize3(listener_set.front);
	normalize3(listener_set.top);
	listener_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->pending.frequency = stream->sample_rate;
	stream->pending.volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	stream->pending.mix_left = 1.0f;
	stream->pending.mix_right = 1.0f;
	stream->pending.surround_bins[0][0] = 1.0f;
	stream->pending.surround_bins[stream->channels - 1][1] = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->pending.mode = DS3DMODE_NORMAL;
	stream->pending.minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->pending.maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	/* (the mixer's side the same, before the mixer can see it) */
	voice_settings_take(stream);
	resampler_reset(stream);
	pthread_mutex_lock(&voices_lock);
	stream->next = streams;
	streams = stream;
	pthread_mutex_unlock(&voices_lock);
	*result = &stream->object;
	return DS_OK;
}

/* January-era DirectSound exports the game declares itself
(sound_dsound_xbox.c); the XDK 3911 headers no longer carry them */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long active;

	pthread_mutex_lock(&voices_lock);
	active = record->packet_count != 0;
	pthread_mutex_unlock(&voices_lock);
	return active;
}

/* (the voice's settings on the game's side, record) */
#define STREAM_SETTER(body) \
	struct voice_settings *record = &stream_from_interface(stream)->pending; \
	pthread_mutex_lock(&voices_lock); \
	body; \
	stream_from_interface(stream)->pending_changed = TRUE; \
	pthread_mutex_unlock(&voices_lock); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : stream_from_interface(stream)->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
}

/* the speaker bins of a mask, for 5.1, in the order of its set bits: a stereo
voice sends its left channel to the first, third, ... and its right to the
second, fourth, ... (the game's stereo voices on AC-3 take front left, front
right, back left, back right). The crosstalk, reverb and effect sends are not
speakers. volumes is NULL for 0 dB. */
static void stream_set_surround_bins(struct voice_settings *record, unsigned long channels, DWORD mix_bin_mask,
	const LONG *volumes, BOOL clear)
{
	unsigned long bit, index = 0;

	if (clear)
		memset(record->surround_bins, 0, sizeof(record->surround_bins));
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) & DSMIXBIN_SPEAKER_MASK)
		{
			record->surround_bins[channels == 2 ? index & 1 : 0][bit] =
				volumes ? gain_from_millibels(volumes[index]) : 1.0f;
		}
		index++;
	}
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f;
		stream_set_surround_bins(record, stream_from_interface(stream)->channels, mix_bin_mask, NULL, TRUE))
}

/* volumes come in the order of the set bits of the mask */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct voice_settings *record = &stream_from_interface(stream)->pending;
	unsigned long bit, index = 0;

	pthread_mutex_lock(&voices_lock);
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	stream_set_surround_bins(record, stream_from_interface(stream)->channels, mix_bin_mask, volumes, FALSE);
	stream_from_interface(stream)->pending_changed = TRUE;
	pthread_mutex_unlock(&voices_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct, direct_hf, room, room_hf;

	(void)apply;
	/* the direct path: at low frequencies obstruction and occlusion take
	their LF ratio of their high frequency levels, which (with the source's
	own) muffle it above the HF reference */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	direct_hf = source->lDirect + source->lDirectHF + source->Obstruction.lHFLevel + source->Occlusion.lHFLevel;
	/* the room send: occlusion muffles it as it does the direct path, and
	obstruction does not reach it */
	room = source->lRoom + (LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	room_hf = source->lRoom + source->lRoomHF + source->Occlusion.lHFLevel;
	{
		STREAM_SETTER(
			record->direct = direct;
			record->direct_hf = direct_hf;
			record->room = room;
			record->room_hf = room_hf;
			record->room_rolloff_factor = source->flRoomRolloffFactor)
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->velocity[0] = x; record->velocity[1] = y; record->velocity[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

/* ---------- buffers

The game's buffers: the loading screen's chant (progress_bar.c: one sound,
looped, as four voices at their pitches, each swelling with the load's
progress), and a silent looping one that keeps the voice processor busy.
A buffer's sound is decoded once as its data is set (Xbox ADPCM or 16-bit
PCM, mono or stereo), and played from the start, looped or once, at its
pitch (the playback rate: 48 kHz times two to the pitch over 4096) and
volume (millibels, under audio.effects_volume and audio.volume), mixed in
the front pair, dry. The game's side and the mixer's share buffers_lock. */

struct sdl_buffer
{
	ULONG reference_count;
	/* its format, and its sound decoded (frames of channels) */
	WORD format, channels;
	short *samples;
	unsigned long frames;
	/* the game's: playing, looped, its rate (frames a second) and gain */
	BOOL playing, looping;
	double rate;
	float gain;
	/* the mixer's: where it plays, in frames, and its gain as it goes to
	the game's (no click as a voice comes in, changes or stops: Stop lets
	it fade out first) */
	double position;
	float current;
	BOOL stopping;
	struct sdl_buffer *link;
};

static pthread_mutex_t buffers_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sdl_buffer *buffers;

/* (the mixer's thread) the buffers that play, added to output */
static void mix_buffers(float *output, unsigned long frames)
{
	struct sdl_buffer *buffer;

	pthread_mutex_lock(&buffers_lock);
	for (buffer = buffers; buffer; buffer = buffer->link)
	{
		double step = buffer->rate / OUTPUT_RATE;
		float gain = buffer->stopping ? 0.0f : buffer->gain * master_volume;
		unsigned long frame;

		if (!buffer->playing || !buffer->samples || !buffer->frames || step <= 0.0)
			continue;
		if (buffer->stopping && buffer->current < 1e-4f)
		{
			buffer->playing = buffer->stopping = FALSE;
			buffer->current = 0.0f;
			continue;
		}
		for (frame = 0; frame < frames; frame++)
		{
			/* (a twentieth of a second to a new gain) */
			buffer->current += (gain - buffer->current) * (1.0f / (0.05f * OUTPUT_RATE / 4.0f));
			unsigned long index = (unsigned long)buffer->position, next = index + 1;
			float fraction = (float)(buffer->position - (double)index), left, right;

			if (next >= buffer->frames)
				next = buffer->looping ? 0 : index;
			if (buffer->channels == 2)
			{
				left = buffer->samples[index * 2] + (buffer->samples[next * 2] - buffer->samples[index * 2]) * fraction;
				right = buffer->samples[index * 2 + 1] +
					(buffer->samples[next * 2 + 1] - buffer->samples[index * 2 + 1]) * fraction;
			}
			else
				left = right = buffer->samples[index] + (buffer->samples[next] - buffer->samples[index]) * fraction;
			output[frame * output_channels] += left * buffer->current * (1.0f / 32768.0f);
			output[frame * output_channels + 1] += right * buffer->current * (1.0f / 32768.0f);
			buffer->position += step;
			if (buffer->position >= (double)buffer->frames)
			{
				if (!buffer->looping)
				{
					buffer->playing = FALSE;
					buffer->position = 0.0;
					break;
				}
				buffer->position -= (double)buffer->frames * (unsigned long)(buffer->position / (double)buffer->frames);
			}
		}
	}
	pthread_mutex_unlock(&buffers_lock);
}

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct sdl_buffer *buffer = calloc(1, sizeof(*buffer));

	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	buffer->rate = 48000.0;
	buffer->gain = (float)config_real("audio.effects_volume");
	if (description && description->lpwfxFormat)
	{
		buffer->format = description->lpwfxFormat->wFormatTag;
		buffer->channels = description->lpwfxFormat->nChannels;
		if (description->lpwfxFormat->nSamplesPerSec)
			buffer->rate = (double)description->lpwfxFormat->nSamplesPerSec;
	}
	pthread_mutex_lock(&buffers_lock);
	buffer->link = buffers;
	buffers = buffer;
	pthread_mutex_unlock(&buffers_lock);
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer, **link;
	ULONG count = --record->reference_count;

	if (!count)
	{
		/* (out of the mixer's list before it is freed) */
		pthread_mutex_lock(&buffers_lock);
		for (link = &buffers; *link; link = &(*link)->link)
		{
			if (*link == record)
			{
				*link = record->link;
				break;
			}
		}
		pthread_mutex_unlock(&buffers_lock);
		free(record->samples);
		free(record);
	}
	return count;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	unsigned long channels = record->channels == 2 ? 2 : 1, frames = 0;
	short *samples = NULL, *old;

	if (data && size && record->format == WAVE_FORMAT_XBOX_ADPCM)
		samples = decode_adpcm(data, size, channels, &frames);
	else if (data && size && record->format == WAVE_FORMAT_PCM)
		samples = decode_pcm(data, size, channels, &frames);
	/* (a silent sound, the voice processor's keep-alive, is not mixed) */
	if (samples)
	{
		unsigned long index;

		for (index = 0; index < frames * channels && !samples[index]; index++)
			;
		if (index == frames * channels)
		{
			free(samples);
			samples = NULL;
		}
	}
	pthread_mutex_lock(&buffers_lock);
	old = record->samples;
	record->samples = samples;
	record->frames = samples ? frames : 0;
	record->channels = (WORD)channels;
	record->position = 0.0;
	pthread_mutex_unlock(&buffers_lock);
	free(old);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;

	(void)reserved1;
	(void)reserved2;
	pthread_mutex_lock(&buffers_lock);
	record->looping = (flags & DSBPLAY_LOOPING) != 0;
	if (!record->playing)
		record->current = 0.0f;
	record->playing = TRUE;
	record->stopping = FALSE;
	pthread_mutex_unlock(&buffers_lock);
	return DS_OK;
}

/* (it fades out over a twentieth of a second, then stops) */
HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	pthread_mutex_lock(&buffers_lock);
	((struct sdl_buffer *)buffer)->stopping = ((struct sdl_buffer *)buffer)->playing;
	pthread_mutex_unlock(&buffers_lock);
	return DS_OK;
}

/* ---------- the loading screen's chant

The Xbox's loading screen played a monk's chant under it
(progress_bar.c's compressed_monk_tone, a second and a half of Xbox ADPCM
at 22 kHz, looped): four voices of it at four pitches, each coming in as
the load reaches its share of it (from the start, 40, 50 and 55 per cent)
and swelling and fading over the rest, as the sine of how far through it
is, to the 0.3. progress_bar.c's own voices are not made in the port
(its draws are the presenter's, its frames do not keep time with them):
the loading screen's begin, progress and end play these. */

#define LOADING_CHANT_VOICES 4

/* (progress_bar.c) */
extern unsigned char compressed_monk_tone[18396];

static LPDIRECTSOUNDBUFFER loading_chant_voices[LOADING_CHANT_VOICES];

void platform_loading_chant(int playing, float progress)
{
	static const float ranges[LOADING_CHANT_VOICES][2] = { { 0.0f, 1.0f }, { 0.4f, 1.0f }, { 0.5f, 1.0f }, { 0.55f, 1.0f } };
	static const float volumes[LOADING_CHANT_VOICES] = { 3500.0f, 4500.0f, 3500.0f, 4500.0f };
	static const LONG pitches[LOADING_CHANT_VOICES] = { -4681, -6386, -3999, -8773 };
	static BOOL started;
	long voice;

	if (!playing)
	{
		if (started)
			for (voice = 0; voice < LOADING_CHANT_VOICES; voice++)
				if (loading_chant_voices[voice])
					IDirectSoundBuffer_Stop(loading_chant_voices[voice]);
		started = FALSE;
		return;
	}
	if (!started)
	{
		WAVEFORMATEX format;
		DSBUFFERDESC description;

		memset(&format, 0, sizeof(format));
		format.wFormatTag = WAVE_FORMAT_XBOX_ADPCM;
		format.nChannels = 1;
		format.nSamplesPerSec = 22050;
		format.wBitsPerSample = 4;
		format.nBlockAlign = 36;
		memset(&description, 0, sizeof(description));
		description.dwSize = sizeof(description);
		description.lpwfxFormat = &format;
		for (voice = 0; voice < LOADING_CHANT_VOICES; voice++)
		{
			if (!loading_chant_voices[voice])
			{
				if (DirectSoundCreateBuffer(&description, &loading_chant_voices[voice]) != DS_OK)
					return;
				IDirectSoundBuffer_SetBufferData(loading_chant_voices[voice], compressed_monk_tone,
					sizeof(compressed_monk_tone));
				IDirectSoundBuffer_SetPitch(loading_chant_voices[voice], pitches[voice]);
			}
			IDirectSoundBuffer_SetCurrentPosition(loading_chant_voices[voice], 0);
			IDirectSoundBuffer_SetVolume(loading_chant_voices[voice], -5000);
			IDirectSoundBuffer_Play(loading_chant_voices[voice], 0, 0, DSBPLAY_LOOPING);
		}
		started = TRUE;
	}
	if (!(progress == progress))
		progress = 0.0f;
	progress = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
	for (voice = 0; voice < LOADING_CHANT_VOICES; voice++)
	{
		float t = progress < ranges[voice][0] || progress > ranges[voice][1] ? 0.0f :
			(progress - ranges[voice][0]) / (ranges[voice][1] - ranges[voice][0]);
		float swell = sinf(t * 3.1415f);

		IDirectSoundBuffer_SetVolume(loading_chant_voices[voice],
			(LONG)((swell > 0.0f ? powf(swell, 0.3f) : 0.0f) * volumes[voice] - 5000.0f));
	}
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;

	/* (bytes, of 16-bit PCM; an ADPCM buffer's only from its start) */
	pthread_mutex_lock(&buffers_lock);
	record->position = record->format == WAVE_FORMAT_PCM && record->frames ?
		(double)((play_cursor / (2u * record->channels)) % record->frames) : 0.0;
	pthread_mutex_unlock(&buffers_lock);
	return DS_OK;
}

/* (the whole buffer loops: the game's loops are its whole sound) */
HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length) { (void)buffer; (void)loop_start; (void)loop_length; return DS_OK; }

HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	double rate = 48000.0 * pow(2.0, (double)pitch / 4096.0);

	pthread_mutex_lock(&buffers_lock);
	record->rate = rate;
	pthread_mutex_unlock(&buffers_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	/* (audio.effects_volume read here, on the game's thread) */
	float gain = gain_from_millibels(volume) * (float)config_real("audio.effects_volume");

	pthread_mutex_lock(&buffers_lock);
	record->gain = gain;
	pthread_mutex_unlock(&buffers_lock);
	return DS_OK;
}
