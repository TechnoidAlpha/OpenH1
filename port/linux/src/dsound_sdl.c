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
(reverb), through the Xbox's own resampler (send_phases), at twice their
room level and distance rolloff. Each gain to a mix bin is played as the
Xbox played it, silent under -57 dB (voice_level; a 3D voice's by its volume
and rolloff together, and its direct path's with its direct level).
Cones are not modelled (no sound of the maps has one). The mix plays at the
Xbox's output level, half the voices' sum, and a look-ahead
limiter keeps it under full scale (limit). A voice the game cuts off while
it plays fades out over 5 ms rather than stopping dead, which clicked
(voice_cut). On stereo and 5.1 (audio.spatial:
"auto", as the Xbox; spatial_hrtf), and in the VR mode with
vr.hrtf, a mono 3D voice goes to each ear through a model of a head instead
of the pan (hrtf), as the Xbox's DirectSound put its 3D voices through its
HRTF; 2D voices (the menus, the music, the HUD) are as they were.

The output is stereo or 5.1 (audio.channels; "auto": 5.1 on a device of
six channels or more, as the host's SDL reports it, output_channels_choose).
For 5.1 the speaker config reports AC-3, as an Xbox set to Dolby Digital
does: the game then sends its 2D voices to the center and back mix bins too
(dsound_initialize_channel: a mono one at half to the front three, a stereo
one whole to the front pair and at half to the back pair), 3D voices play
from the front and back pairs as the Xbox's did (surround_split), stereo
voices positioned in the world are panned around the five full range
speakers by VBAP (surround_pan), and the reverb plays from the front and
back pairs. The mix bins 0-5 are in SDL's 5.1 order (FL, FR, FC, LFE, BL,
BR), so bin n is output channel n. The game sends nothing to the LFE bin;
what is sent to it plays below 120 Hz, as the Xbox's AC-3 carried it (lfe).
Stereo output mixes exactly as before.

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
audio.resampler chooses the Xbox's low pass ("xbox"), the mixer's sinc one
("sinc") or between neighbouring frames ("linear"); audio.enabled = false skips opening a device;
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
#include <stdarg.h>
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
/* the frames a voice cut off fades out over (voice_cut): 5 ms */
#define CUT_FADE_FRAMES 240

/* the frames a voice's volume, pan, pitch, low passes and way through the
HRTF take to move to new ones: 64 (1.3 ms), however many frames the mixes
have, a ramp going on from one mix to the next (mix_voice). An Xbox's take 1
to 2 ms (recorded: a step of each settles from 10% to 90% within them, a
step of the position through its HRTF too) */
#define PARAMETER_RAMP_FRAMES 64

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64

/* the resampler (resampling) */
#define RESAMPLER_ZERO_CROSSINGS 24
#define RESAMPLER_TABLE_STEPS 256
#define RESAMPLER_CUTOFF 0.96
#define RESAMPLER_KAISER_BETA 6.5
#define RESAMPLER_MAXIMUM_STRETCH 2
#define RESAMPLER_HISTORY 128
/* the Xbox's resampler, which a voice's reverb send goes through
(send_phases): a low pass of SEND_ZERO_CROSSINGS source frames each side, in
SEND_TAPS taps (whole vectors: those past it are 0) */
#define SEND_ZERO_CROSSINGS 3
#define SEND_TAPS 8
/* the Xbox's resampler on a voice's direct path (audio.resampler "xbox":
direct_phases): its low pass as recorded (direct_kernel), DIRECT_ZERO_CROSSINGS
source frames each side, in DIRECT_TAPS taps, a polynomial of DIRECT_KERNEL_ORDER
in each frame */
#define DIRECT_ZERO_CROSSINGS 4
#define DIRECT_KERNEL_ORDER 5
#define DIRECT_TAPS 8

/* the Xbox's quietest levels (voice_level): below VOICE_LEVEL_KNEE (a gain,
-43.84 dB) a level is VOICE_LEVEL_STEP of full scale a decibel above
VOICE_LEVEL_SILENT (-57 dB), where it is silent */
#define VOICE_LEVEL_KNEE 0.0064242f
#define VOICE_LEVEL_STEP (1.0f / 2048.0f)
#define VOICE_LEVEL_SILENT 57.0f
/* a 3D voice's level under voice_level: its volume and distance rolloff at
VOICE_LEVEL_3D times (its direct path's with its direct level), the rest of
its gains after it (voice_gains); its reverb send is VOICE_LEVEL_3D times its
room level and distance rolloff */
#define VOICE_LEVEL_3D 2.0f

/* the Doppler shift (doppler): the speed of sound (metres a second), the
Doppler factor (DirectSound's default, which the game keeps), and the most a
voice is shifted either way (an octave) */
#define DOPPLER_SPEED_OF_SOUND 343.0f
#define DOPPLER_FACTOR 1.0f
#define DOPPLER_MAXIMUM_RATIO 2.0f

/* the HRTF (hrtf): the frames each ear keeps for its delay, the distance
within which a sound's direction fades out (world units), the bands each
ear's level is set in (HRTF_BANDS, split at hrtf_crossovers), and the
directions the table (hrtf_table) holds: every 15 degrees round from ahead
to behind, at 40, 20 and 0 degrees below and above. Measured on an Xbox
(see the HRTF's comment below) */
#define HRTF_FRAMES 64
#define HRTF_NEAR 0.25f
#define HRTF_BANDS 9
#define HRTF_AZIMUTHS 13
#define HRTF_ELEVATIONS 7
/* the crossfade between the pan and the HRTF when the choice changes
(audio.spatial): 100 ms */
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
	/* each ear's delay (frames; left, right), and its weights of the
	bands' low passes and of the sound itself (hrtf_process: each band at
	its level, HRTF_BANDS of them told apart by HRTF_BANDS - 1 low passes) */
	float delay[2];
	float weights[2][HRTF_BANDS];
	/* (each ear's level in each band, dB: for the log, not ramped) */
	float decibels[2][HRTF_BANDS];
	/* each ear's gain: the pan's in the middle (-3 dB), with the voice's
	distance and direct path, the head's filters doing the rest */
	float gain;
	/* the gains of the front pair of speakers and the back pair (on
	stereo 1 and 0; on 5.1 DirectSound's split, surround_split) */
	float pair[2];
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
	/* its number in debug.dsound_log (dsound_log) */
	unsigned long log_number;
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
	/* the gains and low pass coefficients where the last mix left them, and
	the step through the frames (the frequency, Doppler shifted, over the
	output rate): the mixer ramps from them, to avoid clicks, to the ones
	it ramps to (target_), over the frames of the ramp left (ramp_frames,
	mix_voice) */
	float current_left, current_right, current_room;
	double current_step;
	/* the Doppler shift of the last mix (doppler) */
	float current_doppler;
	float current_direct_lowpass, current_room_lowpass;
	float current_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	float target_left, target_right, target_room;
	double target_step;
	float target_direct_lowpass, target_room_lowpass;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	unsigned long ramp_frames;
	BOOL gains_valid;
	/* the direct path's low pass, for each channel, and the room send's */
	float direct_lowpass[2];
	float room_lowpass;
	/* through the HRTF (hrtf): whether its filters ran in the last mix (their
	states are the voice's), how much of the voice they play (0 the pan's to
	1 theirs, crossfaded over HRTF_FADE_FRAMES as the choice changes), the
	parameters where the last mix left them and those they ramp to (with
	the gains), each ear's last HRTF_FRAMES frames before its delay (the
	oldest first), and the bands' low passes' states */
	BOOL hrtf_on;
	float hrtf_mix;
	struct hrtf_parameters hrtf_current, hrtf_target;
	float hrtf_frames[2][HRTF_FRAMES];
	float hrtf_bands[HRTF_BANDS - 1][2];
	/* what the voice was playing when the game cut it off, faded out
	(voice_cut): frames of output_channels floats and their room send, and
	how many of them have played */
	float cut[CUT_FADE_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
	float cut_send[CUT_FADE_FRAMES];
	unsigned long cut_played;
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
/* audio.enabled, read again as the settings change (DirectSoundDoWork):
false silences what is played, the mix going on as before so that the
voices play out in time, as the game expects; the mixer's gain fades to it
(sound_fade) */
static volatile BOOL sound_on = TRUE;
static float sound_gain = 1.0f;
/* audio.doppler */
static BOOL doppler_enabled = TRUE;
/* whether mono 3D voices go through the HRTF: on stereo, audio.spatial;
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

/* a voice's gain to a mix bin (a speaker, an ear, the reverb) as the Xbox
played it. Recorded on an Xbox (OpenH1 issue 714, its sixth run: a 2D voice
stepped down in 2 dB steps from -40 dB), its levels are exact down to about
-44 dB, then lie on a straight line: a 2048th of full scale for each decibel
above -57 dB (-46 dB plays at -45.4, -54 at -56.7, -56 at -66.2), and from
-57 dB they are silent: the mixer skips a voice that is silent everywhere
(mix_voice). Above the knee, where the line crosses the true level, a gain
is as it is; the sign is kept (a stereo voice's difference on 5.1) */
static float voice_level(float gain)
{
	float size = fabsf(gain), level;

	if (size >= VOICE_LEVEL_KNEE)
		return gain;
	if (size <= 0.0f)
		return 0.0f;
	level = (VOICE_LEVEL_SILENT + 20.0f * log10f(size)) * VOICE_LEVEL_STEP;
	if (level <= 0.0f)
		return 0.0f;
	return gain < 0.0f ? -level : level;
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
plasma bolt rising as it comes and falling as it goes. An Xbox's shift is
the first order one: the frequency times 1 + (D v_listener - D v_source) / c,
c the speed of sound, D the Doppler factor, the velocities' parts along the
line from the listener to the source in metres a second (the distance
factor's metres a unit), the source's away from the listener, the
listener's towards the source. Recorded on an Xbox, a source passing at 20
units a second was shifted 1.178 coming and 0.822 going, and at 40, 1.356
and 0.644: the same either way, and twice as far at twice the speed, where
(c + D v_listener) / (c + D v_source) gives 1.216 and 0.849, then 1.551 and
0.738. Each part is held within the speed of sound, and the shift to an
octave either way, so that a projectile passing close by, or a velocity
that jumps (a teleport), takes no voice up out of hearing. A voice's shift
is the one its position and velocities give at each mix, moved to over
PARAMETER_RAMP_FRAMES as a change of its pitch is (doppler_heard): an
Xbox's moved with each of the game's updates and settled within 2 ms.
Recorded on an Xbox, a source passing at 40 units a second, its position set
every 34 ms, stepped its pitch at each set; a glide over 80 ms (as the mixer
had) was 166 cents off it, and 37 cents at 20 units a second. */

static float doppler_ratio(const struct sdl_stream *stream)
{
	float offset[3], distance, source_speed, listener_speed = 0.0f, ratio, limit = DOPPLER_SPEED_OF_SOUND;
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
	ratio = 1.0f + (listener_speed - source_speed) / DOPPLER_SPEED_OF_SOUND;
	if (ratio > DOPPLER_MAXIMUM_RATIO)
		ratio = DOPPLER_MAXIMUM_RATIO;
	if (ratio < 1.0f / DOPPLER_MAXIMUM_RATIO)
		ratio = 1.0f / DOPPLER_MAXIMUM_RATIO;
	return ratio;
}

/* the shift a voice plays in a mix: its doppler_ratio, kept for the
voices' report (the step ramps to it, mix_voice) */
static float doppler_heard(struct sdl_stream *stream)
{
	stream->current_doppler = doppler_ratio(stream);
	return stream->current_doppler;
}

/* ---------- the HRTF

On stereo (audio.spatial, spatial_hrtf), and in the VR mode (vr.hrtf), a
mono 3D voice reaches each ear as it would a head's, from its direction (the
listener's: the camera's, or in VR the head as it is tracked,
sound_manager.c refresh_listener), rather than panned between them: a
sound behind or above is told from one ahead or below. The Xbox's
DirectSound put its 3D voices through its own HRTF (DirectSoundUseFullHRTF)
and left the 2D ones (the menus, the music, the HUD) dry; so here.

Each ear hears the voice at its own level in each of HRTF_BANDS bands (to
500 Hz, 1, 2, 3, 4.5, 6.7, 10 and 14 kHz, and above) and after its own
delay, from a table of the directions an Xbox was recorded from (its final
output, a sweep 2 m away through Halo's own DirectSound calls, every 15
degrees round at 40, 20 and 0 degrees below and above, the sweep through
OpenH1's mixer with no HRTF beside it): for each, each ear's level in each
band (dB, least squares over its third octaves and octaves, the bands'
filters as they are here) and the left ear's delay after the right for a
sound to the right (frames: the Xbox's time between the ears, the lag of
their greatest cross-correlation below 1.5 kHz, less what the bands add to
it). A sound to the left is the mirror of one to the right. Between the
directions recorded, the table is interpolated (the levels in dB), round
and up and down; above and below 60 degrees, towards the poles, each
recorded straight up or down, the ears alike.
What the Xbox's recordings showed, and the model this replaces (Brown and
Duda's) did not:
	- the time between the ears hardly falls with the elevation: to the side
	  0.72 ms level, 0.70 at 20 degrees up or down, 0.61 at 40 (a sphere's
	  falls with its cosine, 0.53 at 40);
	- ahead, the ears' high frequencies a few dB up and down by the band
	  (5 kHz down 3 dB, 6.3 kHz down 5, 12.5 kHz level, 16 kHz down 4);
	- behind, a loss that grows with the frequency, 2 dB at 2.5 kHz, 11 at
	  8 kHz, 30 at 16 kHz, not a low pass's from 2 kHz;
	- to the side, the near ear 4 to 5 dB up to 1.5 kHz, level about 3 kHz
	  and 3 to 5 dB up again from 5 to 12 kHz; the far ear 9 to 23 dB down
	  above 3 kHz;
	- below and above, each its own peaks and dips (40 degrees below, 3 dB up
	  at 1.2 to 1.6 kHz; above, 3 to 4 dB down from 5 kHz and 8 dB at 16
	  kHz).
The bands: HRTF_BANDS - 1 low passes (2nd order, Butterworth) of the
voice, each band one's output less the one below's, the top band the voice
less the highest low pass's; so the bands add up to the voice itself, and a
direction whose bands are all at one level is that level and nothing else.
Each ear sums the low passes' outputs and the voice, weighted (the band's
gain less the next's), into its own line, and reads it its delay later
(Catmull-Rom), whole frames for a still sound, as the Xbox's.

A sound close to the head (within HRTF_NEAR) fades to the middle, the ears
alike and at once, as the pan centres one within its minimum distance. The
parameters ramp over PARAMETER_RAMP_FRAMES, as the gains do: a turn of the
head moves the sound smoothly from ear to ear, with no step. Recorded on an
Xbox, a step of the position went from 10% to 90% within 1 ms; ramped over
the whole mix, as they were, 8 ms at 480 frames a mix. Distance, the direct
path's obstruction and occlusion (its low pass), the room send and the
Doppler shift are the pan's, before the head's. When the choice changes
(audio.spatial set mid-game), each voice crossfades between the pan and the
HRTF over HRTF_FADE_FRAMES, both running meanwhile: no click. "speakers"
keeps the pan. On 5.1 the same, on the front and back pairs, a sound behind
heard as the direction mirrored ahead (surround_split), above and below a
little brighter on the far ear, as the Xbox's back pair (hrtf_behind). */

/* a direction of the table: each ear's level in each band (dB) and the left
ear's delay (frames) after the right, for a sound to the right */
struct hrtf_direction
{
	float left[HRTF_BANDS], right[HRTF_BANDS];
	float delay;
};

static const float hrtf_crossovers[HRTF_BANDS - 1] = { 500, 1000, 2000, 3000, 4500, 6700, 10000, 14000 };

/* round from ahead (0 degrees) to behind (180) to the right, every 15
degrees; 60, 40, 20 and 0 degrees below, and 20, 40 and 60 above. The 60
degree rows and the poles were recorded on an Xbox in the test bed's tenth
run: before, the table stopped at 40 and the poles were the mean of the 40
degree rows, which left a sound straight below (a Warthog's engine and
wheels, 70 to 80 degrees under its driver) 20 to 30 dB too bright above
4.5 kHz, and one 60 below up to 14 dB in a band */
static const struct hrtf_direction hrtf_table[HRTF_ELEVATIONS][HRTF_AZIMUTHS] =
{
	/* 60 degrees below (the test bed's tenth run) */
	{
		{ { -0.8f, -2.1f, -3.9f, 4.8f, -9.9f, -23.3f, -0.4f, -11.1f, -12.5f },
		  { -0.8f, -2.1f, -3.9f, 4.8f, -9.9f, -23.3f, -0.4f, -11.1f, -12.5f }, 0.00f }, /* 0 */
		{ { -1.3f, -3.1f, -5.5f, 5.1f, -15.8f, -25.9f, -0.2f, -22.8f, -14.6f },
		  { -0.1f, -1.3f, -2.5f, 3.7f, -5.4f, -20.0f, -1.2f, -7.4f, -11.5f }, 2.06f }, /* 15 */
		{ { -1.1f, -3.8f, -8.2f, 4.6f, -18.5f, -25.1f, -2.3f, -24.6f, -17.2f },
		  { 0.0f, -0.5f, -0.8f, 2.1f, -2.6f, -21.1f, -1.1f, -7.3f, -12.4f }, 3.62f }, /* 30 */
		{ { -0.9f, -3.6f, -19.3f, 0.0f, -26.6f, -10.9f, -11.3f, -38.1f, -24.0f },
		  { 0.3f, 0.7f, 2.5f, -1.8f, -0.7f, -21.8f, -0.3f, -11.3f, -17.0f }, 7.56f }, /* 45 */
		{ { -1.1f, -2.4f, -21.5f, -8.5f, -12.7f, -19.1f, -18.0f, -39.1f, -55.5f },
		  { 0.3f, 0.8f, 3.5f, -1.5f, -2.4f, -21.5f, -0.3f, -12.3f, -18.0f }, 8.88f }, /* 60 */
		{ { -1.4f, -1.4f, -13.1f, -21.4f, -19.6f, -16.5f, -32.0f, -46.3f, -66.7f },
		  { 0.4f, 0.9f, 4.0f, 0.6f, -5.2f, -22.4f, 0.0f, -14.4f, -17.4f }, 10.63f }, /* 75 */
		{ { -1.5f, -1.2f, -6.8f, -30.9f, -19.4f, -38.2f, -15.5f, -21.8f, -31.3f },
		  { 0.7f, 0.8f, 4.1f, 1.7f, -4.8f, -25.5f, -2.8f, -5.2f, -13.1f }, 12.81f }, /* 90 */
		{ { -1.5f, -1.3f, -13.4f, -35.2f, -15.7f, -18.2f, -23.4f, -24.2f, -30.9f },
		  { 0.7f, 0.8f, 3.8f, 0.7f, -6.7f, -28.2f, -4.4f, -6.4f, -14.1f }, 10.31f }, /* 105 */
		{ { -1.3f, -2.4f, -19.8f, -7.7f, -18.0f, -32.6f, -15.3f, -22.8f, -39.8f },
		  { 0.5f, 0.6f, 3.6f, -2.0f, -5.3f, -28.6f, -7.3f, -5.4f, -13.3f }, 8.81f }, /* 120 */
		{ { -1.3f, -4.7f, -8.0f, -5.1f, -28.0f, -27.7f, -10.7f, -22.5f, -23.7f },
		  { 0.4f, 0.1f, 3.3f, -5.0f, -3.8f, -26.5f, -7.0f, -5.8f, -12.2f }, 8.12f }, /* 135 */
		{ { -1.9f, -3.8f, -1.9f, -3.7f, -10.6f, -24.6f, -8.9f, -13.0f, -21.5f },
		  { -0.3f, -1.0f, 2.1f, -8.2f, -2.1f, -22.2f, -4.1f, -10.8f, -11.0f }, 4.00f }, /* 150 */
		{ { -1.9f, -2.6f, -0.8f, -3.9f, -7.8f, -25.7f, -5.8f, -14.4f, -21.2f },
		  { -0.7f, -1.4f, 1.4f, -6.9f, -2.8f, -22.9f, -4.2f, -9.5f, -13.0f }, 2.13f }, /* 165 */
		{ { -1.4f, -1.9f, 0.4f, -5.3f, -5.2f, -24.1f, -4.6f, -11.5f, -17.3f },
		  { -1.4f, -1.9f, 0.4f, -5.3f, -5.2f, -24.1f, -4.6f, -11.5f, -17.3f }, 0.00f }, /* 180 */
	},
	/* -40 degrees */
	{
		{ { -0.3f, -1.2f, 4.2f, 0.1f, 0.8f, -12.2f, -4.4f, 3.6f, -2.8f },
		  { -0.3f, -1.2f, 4.2f, 0.1f, 0.8f, -12.2f, -4.4f, 3.6f, -2.8f }, 0.00f }, /* 0 */
		{ { -0.8f, -1.2f, 2.6f, -2.0f, 0.4f, -18.3f, -3.3f, 0.7f, -7.7f },
		  { 0.8f, -0.6f, 5.0f, 1.5f, 1.6f, -7.6f, -3.9f, 4.9f, -0.5f }, 4.00f }, /* 15 */
		{ { -1.1f, -1.9f, 0.6f, -2.2f, -5.1f, -22.9f, -0.8f, -5.6f, -11.2f },
		  { 1.3f, 0.6f, 5.2f, 2.3f, 3.2f, -11.4f, -0.3f, 5.6f, -2.0f }, 7.50f }, /* 30 */
		{ { -1.6f, -2.3f, -5.7f, -1.5f, -9.6f, -26.9f, -7.0f, -12.3f, -16.9f },
		  { 2.0f, 2.2f, 4.9f, 3.9f, 2.7f, -16.1f, 7.5f, -13.1f, -1.8f }, 16.50f }, /* 45 */
		{ { -1.3f, -1.7f, -5.9f, -7.0f, -40.8f, -45.5f, -9.4f, -26.9f, -23.6f },
		  { 2.6f, 2.8f, 5.8f, 1.5f, 3.5f, -14.4f, 7.0f, -5.0f, -2.0f }, 21.50f }, /* 60 */
		{ { -1.2f, -0.4f, -6.4f, -40.6f, -33.9f, -14.1f, -30.1f, -17.1f, -33.5f },
		  { 2.7f, 2.9f, 6.4f, -1.9f, 3.9f, -6.3f, 0.2f, 6.2f, -14.0f }, 22.56f }, /* 75 */
		{ { -1.2f, -1.1f, -2.9f, -29.0f, -14.7f, -16.7f, -16.5f, -18.1f, -22.7f },
		  { 2.9f, 2.9f, 6.7f, -1.0f, 0.8f, 0.5f, -11.9f, 7.1f, -0.2f }, 25.00f }, /* 90 */
		{ { -1.4f, -1.0f, -5.5f, -36.2f, -19.6f, -13.6f, -28.4f, -16.1f, -23.1f },
		  { 2.6f, 2.3f, 6.3f, 0.5f, -3.3f, 0.1f, -12.5f, 6.5f, 0.0f }, 23.00f }, /* 105 */
		{ { -1.4f, -1.7f, -9.1f, -5.6f, -34.7f, -39.8f, -11.9f, -20.8f, -38.9f },
		  { 2.5f, 2.0f, 6.0f, 1.4f, -5.7f, -2.6f, -8.9f, 6.3f, -2.2f }, 21.19f }, /* 120 */
		{ { -1.6f, -3.4f, -4.9f, -3.0f, -12.7f, -20.7f, -6.8f, -19.2f, -22.6f },
		  { 1.3f, 1.1f, 5.7f, 0.5f, -6.7f, -8.3f, -6.7f, 2.1f, -10.6f }, 14.06f }, /* 135 */
		{ { -1.7f, -2.5f, -1.0f, -3.0f, -6.2f, -15.9f, -6.3f, -16.7f, -22.0f },
		  { 0.7f, -0.2f, 5.0f, 0.6f, -6.8f, -15.3f, -4.2f, -4.3f, -16.2f }, 8.12f }, /* 150 */
		{ { -1.0f, -2.0f, 0.8f, -0.1f, -5.4f, -17.6f, -6.1f, -14.5f, -23.6f },
		  { -0.1f, -1.0f, 4.5f, -0.9f, -5.9f, -16.7f, -4.4f, -9.8f, -22.1f }, 4.12f }, /* 165 */
		{ { -0.7f, -1.5f, 2.9f, 0.1f, -6.8f, -16.9f, -5.1f, -13.3f, -23.6f },
		  { -0.7f, -1.5f, 2.9f, 0.1f, -6.8f, -16.9f, -5.1f, -13.3f, -23.6f }, 0.00f }, /* 180 */
	},
	/* -20 degrees */
	{
		{ { 0.1f, 0.3f, 3.3f, -13.8f, 5.8f, -9.8f, -14.3f, 4.8f, -3.8f },
		  { 0.1f, 0.3f, 3.3f, -13.8f, 5.8f, -9.8f, -14.3f, 4.8f, -3.8f }, 0.00f }, /* 0 */
		{ { -0.4f, -0.9f, 1.2f, -14.8f, 4.5f, -15.7f, -17.4f, 2.2f, -8.4f },
		  { 0.4f, 1.0f, 4.7f, -16.6f, 6.5f, -3.5f, -13.5f, 5.8f, -0.5f }, 4.19f }, /* 15 */
		{ { -0.7f, -1.6f, -1.3f, -10.5f, -1.6f, -18.0f, -14.7f, -3.9f, -13.1f },
		  { 1.4f, 1.9f, 6.0f, -14.9f, 7.4f, -1.2f, -4.7f, 5.9f, -1.9f }, 9.94f }, /* 30 */
		{ { -0.8f, -2.0f, -4.7f, -4.4f, -6.3f, -12.3f, -16.5f, -9.1f, -18.5f },
		  { 2.0f, 2.6f, 6.2f, -11.8f, 7.6f, -3.9f, 4.3f, -1.6f, -1.1f }, 18.31f }, /* 45 */
		{ { -0.9f, -1.1f, -4.2f, -13.6f, -14.3f, -12.5f, -15.9f, -11.2f, -21.2f },
		  { 3.1f, 3.1f, 6.6f, -4.9f, 4.4f, 2.1f, 2.3f, 2.5f, -0.4f }, 24.38f }, /* 60 */
		{ { -1.1f, 1.0f, -6.5f, -8.1f, -13.6f, -36.4f, -13.9f, -18.4f, -21.0f },
		  { 3.2f, 3.3f, 6.6f, -0.2f, -3.7f, 5.8f, -3.4f, 5.0f, 1.9f }, 27.19f }, /* 75 */
		{ { -1.2f, 0.6f, -3.6f, -23.7f, -6.0f, -25.1f, -15.5f, -16.2f, -19.9f },
		  { 3.2f, 3.1f, 6.7f, 0.3f, -10.1f, 4.9f, -2.8f, 4.6f, 2.7f }, 28.81f }, /* 90 */
		{ { -1.3f, 1.2f, -8.3f, -31.9f, -9.2f, -37.9f, -17.7f, -15.5f, -21.4f },
		  { 2.8f, 2.7f, 6.7f, 0.1f, -13.2f, 1.8f, 0.1f, 3.5f, 2.5f }, 26.13f }, /* 105 */
		{ { -1.1f, -0.7f, -7.7f, -10.1f, -8.3f, -26.6f, -10.9f, -22.0f, -21.4f },
		  { 2.5f, 2.6f, 6.6f, -0.1f, -12.5f, -1.6f, 0.6f, 2.9f, 0.0f }, 24.56f }, /* 120 */
		{ { -1.2f, -2.3f, -8.2f, -1.7f, -10.8f, -33.4f, -14.4f, -13.5f, -25.0f },
		  { 1.6f, 1.7f, 6.7f, -8.5f, -3.1f, -9.5f, -1.0f, 0.6f, -8.9f }, 18.56f }, /* 135 */
		{ { -1.4f, -2.2f, -2.6f, -4.9f, -5.6f, -26.2f, -12.9f, -18.7f, -27.5f },
		  { 0.6f, 1.0f, 5.6f, -8.4f, -0.5f, -18.5f, -1.8f, -7.1f, -15.1f }, 10.50f }, /* 150 */
		{ { -1.2f, -1.3f, 0.4f, -3.1f, -2.5f, -25.8f, -9.6f, -15.5f, -24.0f },
		  { -0.1f, -0.2f, 4.4f, -9.0f, -1.0f, -16.6f, -6.3f, -17.3f, -20.4f }, 4.81f }, /* 165 */
		{ { -0.7f, -0.8f, 2.5f, -3.4f, -3.4f, -24.6f, -6.6f, -23.7f, -26.0f },
		  { -0.7f, -0.8f, 2.5f, -3.4f, -3.4f, -24.6f, -6.6f, -23.7f, -26.0f }, 0.00f }, /* 180 */
	},
	/* level */
	{
		{ { 1.1f, 0.6f, 0.4f, 0.6f, 2.1f, -1.4f, -13.0f, 2.2f, -4.4f },
		  { 1.1f, 0.6f, 0.4f, 0.6f, 2.1f, -1.4f, -13.0f, 2.2f, -4.4f }, 0.00f }, /* 0 */
		{ { 0.6f, -0.3f, -1.1f, -2.8f, 1.7f, -5.4f, -16.4f, 0.7f, -13.2f },
		  { 1.8f, 1.5f, 2.3f, -1.1f, 5.1f, -0.1f, -7.5f, 2.2f, 0.0f }, 4.69f }, /* 15 */
		{ { 0.4f, -1.7f, -4.4f, -1.8f, -7.8f, -10.9f, -14.3f, -5.8f, -18.1f },
		  { 2.1f, 2.7f, 3.6f, -1.2f, 6.7f, -0.2f, 0.1f, 1.3f, -0.7f }, 10.19f }, /* 30 */
		{ { 0.2f, -0.6f, -10.6f, 0.3f, -10.2f, -24.0f, -14.0f, -10.7f, -20.1f },
		  { 3.0f, 3.6f, 4.7f, -1.3f, 7.8f, -1.6f, 5.0f, -2.9f, -1.2f }, 19.56f }, /* 45 */
		{ { -0.5f, 1.4f, -7.8f, -24.8f, -3.8f, -29.2f, -18.5f, -14.4f, -27.6f },
		  { 3.4f, 4.3f, 5.4f, 0.5f, 5.2f, 1.1f, 5.6f, -0.6f, -0.4f }, 24.81f }, /* 60 */
		{ { -0.9f, 1.4f, -2.6f, -18.8f, -16.7f, -24.6f, -12.0f, -18.3f, -20.9f },
		  { 3.4f, 4.4f, 5.3f, 2.7f, -0.1f, 1.4f, 5.2f, 3.4f, 1.3f }, 29.06f }, /* 75 */
		{ { -0.9f, 0.6f, 0.0f, -11.1f, -9.1f, -23.4f, -11.4f, -15.0f, -20.9f },
		  { 3.6f, 4.5f, 5.5f, 3.6f, -3.7f, -0.3f, 5.0f, 5.3f, 1.3f }, 31.50f }, /* 90 */
		{ { -1.0f, 1.3f, -2.6f, -28.9f, -16.7f, -11.6f, -19.4f, -14.2f, -22.4f },
		  { 3.2f, 4.3f, 5.2f, 3.5f, -5.7f, -5.4f, 5.0f, 5.5f, 0.1f }, 28.44f }, /* 105 */
		{ { -0.7f, 1.3f, -10.4f, -12.8f, -6.2f, -37.7f, -16.6f, -15.7f, -28.3f },
		  { 2.9f, 4.3f, 4.3f, 5.0f, -12.5f, -17.6f, 6.5f, 2.3f, -3.9f }, 25.12f }, /* 120 */
		{ { -0.3f, -1.7f, -8.7f, -1.4f, -12.3f, -25.1f, -11.9f, -21.4f, -25.7f },
		  { 1.9f, 3.7f, 3.6f, 4.2f, -16.1f, -21.8f, 5.6f, -8.7f, -10.6f }, 19.94f }, /* 135 */
		{ { -0.6f, -2.1f, -3.3f, -4.8f, -5.7f, -30.2f, -10.2f, -31.3f, -28.3f },
		  { 1.1f, 2.6f, 2.7f, 1.9f, -6.3f, -20.6f, 0.1f, -20.9f, -18.2f }, 11.00f }, /* 150 */
		{ { -0.5f, -0.9f, -0.6f, -3.4f, -1.8f, -24.1f, -8.4f, -39.3f, -26.6f },
		  { 0.8f, 1.1f, 2.6f, -1.8f, -6.0f, -9.5f, -19.0f, -24.8f, -54.2f }, 5.44f }, /* 165 */
		{ { 0.2f, -0.1f, 1.5f, -2.2f, -3.1f, -13.3f, -9.8f, -31.9f, -26.2f },
		  { 0.2f, -0.1f, 1.5f, -2.2f, -3.1f, -13.3f, -9.8f, -31.9f, -26.2f }, 0.00f }, /* 180 */
	},
	/* +20 degrees */
	{
		{ { 1.5f, 0.2f, -0.6f, 2.8f, 0.3f, -0.4f, -6.3f, -1.3f, -6.8f },
		  { 1.5f, 0.2f, -0.6f, 2.8f, 0.3f, -0.4f, -6.3f, -1.3f, -6.8f }, 0.00f }, /* 0 */
		{ { 1.0f, -0.9f, -1.8f, -0.9f, 1.2f, -3.9f, -9.7f, -5.1f, -11.9f },
		  { 2.2f, 0.9f, 0.7f, 3.7f, 1.4f, 2.3f, -4.7f, 1.1f, -2.6f }, 4.25f }, /* 15 */
		{ { 0.2f, -2.0f, -4.2f, -2.2f, -8.6f, -6.2f, -18.0f, -9.2f, -16.4f },
		  { 3.1f, 1.9f, 1.6f, 4.8f, 1.6f, 4.2f, -0.1f, 1.5f, -2.6f }, 9.94f }, /* 30 */
		{ { 0.6f, -1.5f, -9.4f, 0.0f, -10.5f, -17.9f, -19.5f, -15.1f, -24.7f },
		  { 3.7f, 3.2f, 2.5f, 5.3f, 2.6f, 2.5f, 6.3f, -6.8f, -1.1f }, 18.38f }, /* 45 */
		{ { 0.2f, 0.4f, -5.5f, -17.3f, -6.8f, -18.8f, -19.8f, -16.7f, -26.2f },
		  { 4.1f, 3.9f, 2.9f, 6.3f, -0.3f, 3.4f, 6.2f, -0.4f, -2.3f }, 24.31f }, /* 60 */
		{ { -0.2f, 1.6f, -4.7f, -6.0f, -27.0f, -30.3f, -10.4f, -30.8f, -25.6f },
		  { 4.1f, 3.7f, 2.8f, 7.1f, -6.4f, 2.4f, 5.3f, 4.2f, -10.0f }, 27.94f }, /* 75 */
		{ { -0.2f, 0.4f, -0.2f, -15.0f, -3.0f, -12.7f, -8.3f, -22.4f, -24.8f },
		  { 4.3f, 3.6f, 3.2f, 7.3f, -9.9f, -0.7f, 5.5f, 5.5f, -8.0f }, 30.56f }, /* 90 */
		{ { -0.5f, 1.0f, -2.5f, -15.6f, -10.3f, -17.2f, -15.8f, -16.4f, -25.5f },
		  { 4.4f, 3.4f, 3.7f, 7.4f, -11.8f, -4.9f, 5.6f, 5.6f, -7.2f }, 28.50f }, /* 105 */
		{ { -0.2f, 0.2f, -5.6f, -13.0f, -8.2f, -32.9f, -13.6f, -21.8f, -27.9f },
		  { 4.0f, 2.9f, 3.8f, 6.7f, -14.6f, -15.4f, 7.0f, 1.3f, -10.2f }, 24.88f }, /* 120 */
		{ { 0.0f, -1.7f, -7.2f, -2.2f, -12.0f, -28.0f, -11.3f, -21.0f, -30.0f },
		  { 3.2f, 2.3f, 3.0f, 5.4f, -8.9f, -18.5f, 5.0f, -9.3f, -17.3f }, 19.06f }, /* 135 */
		{ { -0.4f, -2.1f, -3.6f, -4.5f, -10.4f, -8.7f, -16.6f, -22.8f, -29.5f },
		  { 2.6f, 1.2f, 2.3f, 3.7f, -8.3f, -8.3f, -1.8f, -21.7f, -19.2f }, 11.00f }, /* 150 */
		{ { 0.2f, -1.0f, -1.8f, -0.7f, -5.7f, -7.7f, -12.3f, -27.3f, -27.3f },
		  { 1.7f, -0.1f, 1.7f, 1.6f, -12.5f, -1.8f, -19.1f, -25.4f, -25.7f }, 5.19f }, /* 165 */
		{ { 1.0f, -0.6f, 0.2f, 1.5f, -14.0f, -2.2f, -16.3f, -24.9f, -27.5f },
		  { 1.0f, -0.6f, 0.2f, 1.5f, -14.0f, -2.2f, -16.3f, -24.9f, -27.5f }, 0.00f }, /* 180 */
	},
	/* +40 degrees */
	{
		{ { 1.1f, -0.2f, -0.1f, 2.3f, -1.5f, -0.9f, -7.0f, -0.1f, -17.9f },
		  { 1.1f, -0.2f, -0.1f, 2.3f, -1.5f, -0.9f, -7.0f, -0.1f, -17.9f }, 0.00f }, /* 0 */
		{ { 0.6f, -0.8f, -1.2f, -2.2f, 2.5f, -10.9f, -5.4f, -5.6f, -21.5f },
		  { 2.0f, 0.1f, 1.4f, 3.3f, -1.8f, 3.4f, -13.2f, 4.4f, -15.2f }, 3.88f }, /* 15 */
		{ { 0.4f, -1.6f, -2.9f, -4.0f, -0.7f, -12.4f, -4.2f, -12.6f, -20.9f },
		  { 2.4f, 0.7f, 2.2f, 3.6f, -1.8f, 5.6f, -11.7f, 5.4f, -2.3f }, 7.62f }, /* 30 */
		{ { 0.6f, -1.5f, -6.8f, -2.8f, -4.8f, -6.6f, -29.7f, -15.2f, -17.5f },
		  { 2.7f, 2.3f, 3.0f, 2.3f, 2.4f, 2.3f, 4.8f, 0.5f, 0.0f }, 16.81f }, /* 45 */
		{ { 0.4f, -0.7f, -4.4f, -4.7f, -14.4f, -5.3f, -18.9f, -12.0f, -25.6f },
		  { 2.9f, 3.0f, 3.2f, 3.2f, 2.4f, 0.0f, 6.3f, 0.4f, -0.4f }, 22.31f }, /* 60 */
		{ { 0.3f, -0.8f, -1.5f, -15.2f, -8.7f, -9.7f, -9.6f, -17.9f, -21.9f },
		  { 3.5f, 2.9f, 3.1f, 4.4f, 0.4f, 0.4f, 4.2f, 4.3f, -1.9f }, 24.44f }, /* 75 */
		{ { 0.2f, -0.7f, -1.1f, -17.8f, -5.2f, -20.1f, -10.4f, -16.5f, -28.5f },
		  { 3.6f, 2.9f, 3.2f, 4.6f, -0.2f, -1.4f, 3.3f, 5.9f, -5.1f }, 25.31f }, /* 90 */
		{ { 0.2f, -1.1f, -1.3f, -14.6f, -8.5f, -20.2f, -6.5f, -22.8f, -26.0f },
		  { 3.1f, 3.0f, 3.5f, 4.2f, 0.0f, -8.0f, 4.9f, 4.4f, -10.1f }, 24.69f }, /* 105 */
		{ { 0.1f, -1.2f, -2.9f, -8.5f, -12.1f, -14.5f, -6.4f, -36.7f, -26.2f },
		  { 3.0f, 2.4f, 3.2f, 4.1f, -1.5f, -11.6f, 5.0f, 1.7f, -12.3f }, 23.00f }, /* 120 */
		{ { 0.2f, -2.0f, -5.5f, -2.8f, -20.5f, -3.9f, -25.1f, -28.1f, -28.3f },
		  { 2.1f, 1.6f, 3.4f, 2.3f, -3.5f, -7.9f, 2.5f, -16.1f, -21.5f }, 15.44f }, /* 135 */
		{ { 0.1f, -2.0f, -3.5f, -1.7f, -17.3f, -3.5f, -12.8f, -35.4f, -27.1f },
		  { 1.7f, 0.3f, 2.9f, 1.3f, -15.4f, 2.1f, -9.8f, -17.9f, -24.1f }, 8.44f }, /* 150 */
		{ { 0.5f, -1.3f, -2.0f, 0.5f, -11.5f, -3.8f, -10.3f, -31.6f, -26.4f },
		  { 1.4f, -0.6f, 2.0f, 0.7f, -18.4f, 1.6f, -11.5f, -19.2f, -24.0f }, 4.25f }, /* 165 */
		{ { 1.0f, -1.0f, 0.4f, 1.2f, -19.2f, -0.1f, -9.6f, -25.8f, -24.9f },
		  { 1.0f, -1.0f, 0.4f, 1.2f, -19.2f, -0.1f, -9.6f, -25.8f, -24.9f }, 0.00f }, /* 180 */
	},
	/* 60 degrees above (the test bed's tenth run) */
	{
		{ { 0.0f, -1.1f, -1.1f, 1.5f, -6.9f, -1.8f, -5.1f, -0.7f, -9.8f },
		  { 0.0f, -1.1f, -1.1f, 1.5f, -6.9f, -1.8f, -5.1f, -0.7f, -9.8f }, 0.00f }, /* 0 */
		{ { 0.0f, -1.3f, -1.9f, -0.1f, -3.2f, -6.8f, -2.4f, -4.6f, -13.5f },
		  { 0.4f, -0.7f, -0.1f, 1.9f, -7.0f, 0.4f, -6.3f, 1.0f, -5.4f }, 2.12f }, /* 15 */
		{ { -0.1f, -1.5f, -2.5f, -1.7f, -3.1f, -6.2f, -5.4f, -4.8f, -15.4f },
		  { 0.5f, -0.6f, 0.7f, 1.5f, -5.0f, 0.6f, -5.1f, 2.5f, -3.9f }, 4.19f }, /* 30 */
		{ { -0.3f, -1.7f, -3.7f, -4.1f, -6.0f, -14.2f, -2.2f, -12.8f, -21.8f },
		  { 0.8f, -0.3f, 1.6f, 1.0f, -2.5f, 0.2f, -3.6f, 4.5f, -2.7f }, 7.94f }, /* 45 */
		{ { -0.3f, -2.0f, -4.0f, -5.2f, -8.0f, -11.9f, -7.3f, -7.8f, -20.4f },
		  { 0.7f, 0.3f, 1.9f, 0.8f, -1.8f, 0.0f, -2.8f, 5.2f, -3.0f }, 9.31f }, /* 60 */
		{ { -0.3f, -2.1f, -3.9f, -6.6f, -8.7f, -14.3f, -7.6f, -13.4f, -21.8f },
		  { 0.8f, 0.5f, 2.0f, 1.1f, -1.7f, -0.8f, -1.3f, 5.3f, -3.6f }, 10.19f }, /* 75 */
		{ { -0.6f, -2.2f, -3.8f, -8.9f, -9.0f, -20.5f, -6.1f, -20.6f, -28.9f },
		  { 1.0f, 0.5f, 2.1f, 1.1f, -1.4f, -3.8f, 1.5f, 4.0f, -6.1f }, 10.75f }, /* 90 */
		{ { -0.3f, -2.1f, -3.4f, -7.8f, -8.2f, -16.9f, -6.2f, -10.0f, -20.7f },
		  { 0.8f, 0.6f, 2.1f, 0.6f, -1.2f, -6.5f, 2.7f, 2.5f, -7.8f }, 10.12f }, /* 105 */
		{ { -0.3f, -1.9f, -3.3f, -6.6f, -7.7f, -18.6f, -3.7f, -8.1f, -21.4f },
		  { 0.7f, 0.4f, 2.0f, 0.3f, -1.4f, -8.5f, 3.2f, 1.1f, -9.2f }, 9.13f }, /* 120 */
		{ { -0.5f, -1.7f, -3.2f, -5.2f, -7.7f, -17.0f, -0.9f, -13.7f, -20.2f },
		  { 0.6f, -0.1f, 1.6f, 0.0f, -2.2f, -9.0f, 2.7f, 0.2f, -10.6f }, 7.75f }, /* 135 */
		{ { -0.2f, -1.3f, -2.0f, -2.7f, -4.2f, -16.1f, -0.3f, -9.1f, -18.9f },
		  { 0.2f, -0.6f, 0.9f, -0.2f, -4.7f, -9.3f, 1.8f, -1.8f, -12.2f }, 3.94f }, /* 150 */
		{ { -0.2f, -1.2f, -1.6f, -1.1f, -5.2f, -16.2f, 1.0f, -8.7f, -17.4f },
		  { -0.1f, -0.9f, 0.1f, -0.1f, -6.5f, -9.7f, 0.8f, -3.1f, -13.9f }, 2.00f }, /* 165 */
		{ { -0.1f, -0.9f, -0.6f, -0.2f, -6.0f, -13.4f, 1.4f, -5.5f, -15.8f },
		  { -0.1f, -0.9f, -0.6f, -0.2f, -6.0f, -13.4f, 1.4f, -5.5f, -15.8f }, 0.00f }, /* 180 */
	},
};
static const struct hrtf_direction hrtf_poles[2] =
{
	/* straight below and straight above (recorded, the test bed's tenth run) */
	{ { -0.3f, -0.3f, 2.0f, -32.3f, -40.1f, -22.4f, -19.1f, -43.1f, -31.9f },
	  { -0.3f, -0.3f, 2.0f, -32.3f, -40.1f, -22.4f, -19.1f, -43.1f, -31.9f }, 0.0f },
	{ { 0.1f, -0.9f, -0.5f, -0.9f, -4.4f, -8.4f, -1.4f, -0.5f, -13.4f },
	  { 0.1f, -0.9f, -0.5f, -0.9f, -4.4f, -8.4f, -1.4f, -0.5f, -13.4f }, 0.0f },
};

/* On 5.1 the Xbox's back pair played a sound behind as the direction
mirrored ahead (surround_split), but not quite: recorded there (its AC-3
decoded), at 135 degrees, 40 below and 40 above, the far ear 3 to 4 dB
brighter at 8 and 16 kHz than the 45 degrees it mirrors, and the near ear
within a dB of it, where at 120 and 150 degrees, level, the mirror is the
Xbox's to 0.6 dB. What the back pair adds to each ear's levels in each band
(dB, for a sound to the right; least squares as the table's, held near 0)
at 135 degrees, 40 below and 40 above; it fades out towards 90 and 180
degrees round (sin 2 azimuth), level and the poles (hrtf_place) */
static const struct
{
	float left[HRTF_BANDS], right[HRTF_BANDS];
} hrtf_behind[2] =
{
	{ { -0.1f, 0.1f, 1.6f, 0.2f, -1.6f, -1.6f, 1.4f, 4.2f, -1.5f },
	  { 0.2f, 0.1f, 0.3f, 0.1f, -0.1f, -1.2f, 0.0f, 4.2f, -2.2f } },
	{ { -0.1f, -0.4f, 0.5f, 0.5f, -4.2f, 4.3f, 3.3f, 4.9f, -5.4f },
	  { 0.1f, -0.2f, 0.1f, 0.3f, 0.1f, 0.3f, -0.8f, 0.6f, 1.1f } },
};

static struct
{
	/* each low pass's b0 (and b2), b1, a1 and a2 (a0 1) */
	float lowpass[HRTF_BANDS - 1][4];
} hrtf;

static void hrtf_initialize(void)
{
	int band;

	/* (RBJ's low pass, Q 1 / sqrt(2)) */
	for (band = 0; band < HRTF_BANDS - 1; band++)
	{
		float w = 2.0f * 3.14159265f * hrtf_crossovers[band] / OUTPUT_RATE;
		float alpha = sinf(w) / (2.0f * 0.70710678f), cosine = cosf(w), a0 = 1.0f + alpha;

		hrtf.lowpass[band][0] = 0.5f * (1.0f - cosine) / a0;
		hrtf.lowpass[band][1] = (1.0f - cosine) / a0;
		hrtf.lowpass[band][2] = -2.0f * cosine / a0;
		hrtf.lowpass[band][3] = (1.0f - alpha) / a0;
	}
}

/* DirectSound's azimuth (degrees, -180 to 180, the right positive) and
elevation (-90 to 90) of a direction right, up and ahead: in each octant
45 degrees times the lesser side over the greater, and 90 less the
greater's turn past 45 (surround_split's) */
static void hrtf_angles(float right, float up, float ahead, float *azimuth, float *elevation)
{
	float horizontal = sqrtf(right * right + ahead * ahead), height = fabsf(up);
	float front = fabsf(ahead), side = fabsf(right);

	if (height < horizontal)
		*elevation = height / horizontal * 45.0f;
	else
		*elevation = height > 0.0f ? 90.0f - horizontal / height * 45.0f : 0.0f;
	if (up < 0.0f)
		*elevation = -*elevation;
	if (front > side)
		*azimuth = side / front * 45.0f;
	else
		*azimuth = side > 0.0f ? 90.0f - front / side * 45.0f : 0.0f;
	if (ahead < 0.0f)
		*azimuth = 180.0f - *azimuth;
	if (right < 0.0f)
		*azimuth = -*azimuth;
}

/* out blended from a to b by t (0 to 1) */
static void hrtf_blend(const struct hrtf_direction *a, const struct hrtf_direction *b, float t,
	struct hrtf_direction *out)
{
	int band;

	for (band = 0; band < HRTF_BANDS; band++)
	{
		out->left[band] = a->left[band] + (b->left[band] - a->left[band]) * t;
		out->right[band] = a->right[band] + (b->right[band] - a->right[band]) * t;
	}
	out->delay = a->delay + (b->delay - a->delay) * t;
}

/* the table at azimuth (degrees, 0 ahead to 180 behind, to the right) and
elevation (-90 to 90), interpolated round, and up and down between its rows
and the poles */
static void hrtf_lookup(float azimuth, float elevation, struct hrtf_direction *out)
{
	static const float rows[HRTF_ELEVATIONS + 2] = { -90.0f, -60.0f, -40.0f, -20.0f, 0.0f, 20.0f, 40.0f, 60.0f, 90.0f };
	float column = azimuth / 15.0f, round;
	struct hrtf_direction at[2];
	int first, row, side;

	column = column < 0.0f ? 0.0f : column > HRTF_AZIMUTHS - 1 ? (float)(HRTF_AZIMUTHS - 1) : column;
	first = (int)column < HRTF_AZIMUTHS - 1 ? (int)column : HRTF_AZIMUTHS - 2;
	round = column - (float)first;
	elevation = elevation < -90.0f ? -90.0f : elevation > 90.0f ? 90.0f : elevation;
	for (row = 0; row < HRTF_ELEVATIONS && elevation > rows[row + 1]; row++)
		;
	for (side = 0; side < 2; side++)
	{
		int at_row = row + side;

		if (at_row == 0)
			at[side] = hrtf_poles[0];
		else if (at_row == HRTF_ELEVATIONS + 1)
			at[side] = hrtf_poles[1];
		else
			hrtf_blend(&hrtf_table[at_row - 1][first], &hrtf_table[at_row - 1][first + 1], round, &at[side]);
	}
	hrtf_blend(&at[0], &at[1], (elevation - rows[row]) / (rows[row + 1] - rows[row]), out);
}

/* the HRTF's parameters for a sound right, up and ahead of the head, as
far away (world units); behind: on 5.1, a sound behind mirrored ahead, as
the back pair plays it (hrtf_behind) */
static void hrtf_place(float right, float up, float ahead, float distance, BOOL behind,
	struct hrtf_parameters *parameters)
{
	float strength = distance < HRTF_NEAR ? distance / HRTF_NEAR : 1.0f, azimuth = 0.0f, elevation = 0.0f;
	struct hrtf_direction direction;
	int mirrored = 0, ear, band;

	if (distance > 1.0e-4f)
	{
		azimuth = atan2f(fabsf(right), ahead) * (180.0f / 3.14159265f);
		elevation = atan2f(up, sqrtf(right * right + ahead * ahead)) * (180.0f / 3.14159265f);
		mirrored = right < 0.0f;
	}
	hrtf_lookup(azimuth, elevation, &direction);
	if (behind && elevation != 0.0f)
	{
		/* (all of it at 45 degrees mirrored, 40 up or down; none ahead or to
		the side, level or at the poles) */
		float height = fabsf(elevation);
		float share = sinf(2.0f * azimuth * (3.14159265f / 180.0f)) *
			(height < 40.0f ? height / 40.0f : (90.0f - height) / 50.0f);

		for (band = 0; band < HRTF_BANDS; band++)
		{
			direction.left[band] += share * hrtf_behind[elevation > 0.0f].left[band];
			direction.right[band] += share * hrtf_behind[elevation > 0.0f].right[band];
		}
	}
	for (ear = 0; ear < 2; ear++)
	{
		/* (a sound to the left: the right's ears swapped) */
		const float *decibels = ear ^ mirrored ? direction.right : direction.left;
		float gain[HRTF_BANDS];

		for (band = 0; band < HRTF_BANDS; band++)
		{
			parameters->decibels[ear][band] = strength * decibels[band];
			gain[band] = powf(10.0f, parameters->decibels[ear][band] / 20.0f);
		}
		/* (each band a low pass less the one below: each low pass's weight
		its band's gain less the next's) */
		for (band = 0; band < HRTF_BANDS - 1; band++)
			parameters->weights[ear][band] = gain[band] - gain[band + 1];
		parameters->weights[ear][HRTF_BANDS - 1] = gain[HRTF_BANDS - 1];
	}
	/* (the nearer ear at no delay; whole frames, as the Xbox's) */
	parameters->delay[mirrored] = floorf(strength * direction.delay + 0.5f);
	parameters->delay[!mirrored] = 0.0f;
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
	int ear, band;

	for (ear = 0; ear < 2; ear++)
	{
		parameters->delay[ear] += step->delay[ear];
		for (band = 0; band < HRTF_BANDS; band++)
			parameters->weights[ear][band] += step->weights[ear][band];
	}
	parameters->gain += step->gain;
	parameters->pair[0] += step->pair[0];
	parameters->pair[1] += step->pair[1];
}

/* the crossfade between the pan and the HRTF a frame on: mix moved by
ramp_mix (a frame's share of HRTF_FADE_FRAMES), no further than end */
static float hrtf_mix_step(float mix, float ramp_mix, float end)
{
	mix += ramp_mix;
	return (ramp_mix > 0.0f && mix > end) || (ramp_mix < 0.0f && mix < end) ? end : mix;
}

/* frames of a mono voice's direct path (direct) through the HRTF to each
ear, added to output (frames of output_channels: on 5.1 to the front pair
and the back pair, at their gains) at mix of the voice (the crossfade with
the pan, which the voice's mix added: hrtf_mix_step to mix_end), from the
voice's parameters (stream->hrtf_current) to target, ramped over the
ramp_frames left (more than frames: on in the next mix); a pass of its own
over the mix, each ear's line one run of frames (the voice's last and
these), so that it keeps to registers */
static void hrtf_process(struct sdl_stream *stream, const float *direct, float *output, unsigned long frames,
	unsigned long ramp_frames, const struct hrtf_parameters *target, float mix, float ramp_mix, float mix_end)
{
	float line[2][HRTF_FRAMES + MIX_CHUNK_FRAMES];
	struct hrtf_parameters now = stream->hrtf_current, ramp;
	float state[HRTF_BANDS - 1][2];
	float share = ramp_frames ? 1.0f / (float)ramp_frames : 0.0f;
	unsigned long frame;
	int ear, band;

	for (ear = 0; ear < 2; ear++)
	{
		ramp.delay[ear] = (target->delay[ear] - now.delay[ear]) * share;
		for (band = 0; band < HRTF_BANDS; band++)
			ramp.weights[ear][band] = (target->weights[ear][band] - now.weights[ear][band]) * share;
		memcpy(line[ear], stream->hrtf_frames[ear], sizeof(stream->hrtf_frames[ear]));
	}
	ramp.gain = (target->gain - now.gain) * share;
	ramp.pair[0] = (target->pair[0] - now.pair[0]) * share;
	ramp.pair[1] = (target->pair[1] - now.pair[1]) * share;
	memcpy(state, stream->hrtf_bands, sizeof(state));
	for (frame = 0; frame < frames; frame++)
	{
		/* the low passes (transposed direct form II), then each ear its
		bands' levels and its delay */
		float x = direct[frame], low[HRTF_BANDS - 1];

		for (band = 0; band < HRTF_BANDS - 1; band++)
		{
			const float *c = hrtf.lowpass[band];
			float y = c[0] * x + state[band][0];

			state[band][0] = flush_denormal(c[1] * x - c[2] * y + state[band][1]);
			state[band][1] = flush_denormal(c[0] * x - c[3] * y);
			low[band] = y;
		}
		for (ear = 0; ear < 2; ear++)
		{
			const float *weights = now.weights[ear];
			float heard = weights[HRTF_BANDS - 1] * x;

			for (band = 0; band < HRTF_BANDS - 1; band++)
				heard += weights[band] * low[band];
			line[ear][HRTF_FRAMES + frame] = heard;
			heard = hrtf_read(line[ear] + HRTF_FRAMES + frame, 1.0f + now.delay[ear]) * now.gain * mix;
			if (output_channels == 6)
			{
				output[frame * 6 + ear] += heard * now.pair[0];
				output[frame * 6 + 4 + ear] += heard * now.pair[1];
			}
			else
				output[frame * 2 + ear] += heard;
		}
		/* (at the ramp's end, exactly the new ones, held) */
		if (frame + 1 == ramp_frames)
		{
			memcpy(now.delay, target->delay, sizeof(now.delay));
			memcpy(now.weights, target->weights, sizeof(now.weights));
			now.gain = target->gain;
			now.pair[0] = target->pair[0];
			now.pair[1] = target->pair[1];
			memset(&ramp, 0, sizeof(ramp));
		}
		else
			hrtf_step(&now, &ramp);
		mix = hrtf_mix_step(mix, ramp_mix, mix_end);
	}
	for (ear = 0; ear < 2; ear++)
		memcpy(stream->hrtf_frames[ear], line[ear] + frames, sizeof(stream->hrtf_frames[ear]));
	memcpy(now.decibels, target->decibels, sizeof(now.decibels));
	stream->hrtf_current = now;
	memcpy(stream->hrtf_bands, state, sizeof(state));
}

/* audio.spatial ("auto", "headphones" or "speakers"; another is "auto"):
whether 3D voices go through the HRTF. "auto" is the Xbox's own way, the
HRTF on any stereo output: the game asks DirectSound for its full HRTF
(DirectSoundUseFullHRTF, sound_dsound_xbox.c) whatever the dashboard's
speaker setting, and an Xbox set to stereo played its 3D voices through it
to the television's speakers too, with no crosstalk cancellation after it
(the effects image's crosstalk effect, a canceller of 11-frame delay and a
one-pole low pass, would lift the sides' difference below 600 Hz by up to 12
dB; an Xbox capture's is 7 to 11 dB under the sum, as the HRTF's alone, so
it passes stereo through). A capture of an Xbox on stereo has the HRTF's
decorrelated high frequencies (side over middle -1 dB at 4 to 8 kHz, the
ears' coherence 0.15), where the pan has almost none (-15 dB, 0.94).
"speakers" pans instead. In the VR mode, vr.hrtf alone, as before (the
headset's speakers are at the ears) */
static BOOL spatial_hrtf(const char *setting, BOOL vr_running, BOOL vr_hrtf)
{
	if (vr_running)
		return vr_hrtf;
	return strcmp(setting, "speakers") != 0;
}

/* On 5.1 (AC-3) the Xbox played a 3D voice from the front pair of speakers
and the back pair, never the center or the LFE: the same pair of ears' sound
on both (the HRTF's, or the pan's), a sound behind heard as the direction
mirrored ahead (azimuth 180 - azimuth), and its power split between the
pairs by its direction, DirectSound's azimuth and elevation (hrtf_angles):
the back pair's share 0.5 + (1 - |elevation| / 90)(|azimuth| / 90 - 1),
held to 0 to 1. Ahead it is all in the front pair, behind all in the back,
to the side half in each, above and below towards half. Recorded on an Xbox
(its AC-3 decoded), a sweep at each 30 degrees around and at 45 and 135
degrees 40 above and below: each pair's two speakers alike in time and level
ahead and behind, both pairs the same time and level apart between their
speakers to the side (660 us, the HRTF's), the back pair's share 0, 0, 0.21,
0.50, 0.79, 1, 1 from ahead round to behind, 0.21 and 0.79 at 45 and 135
degrees, up or down; the center and the LFE silent. So here; on stereo the
front pair alone, all round (spatial_hrtf). pair: the front pair's gain and
the back pair's */
static void surround_split(float right, float up, float ahead, float *pair)
{
	float azimuth = 0.0f, elevation = 0.0f, share;

	if (output_channels != 6)
	{
		pair[0] = 1.0f;
		pair[1] = 0.0f;
		return;
	}
	if (right * right + up * up + ahead * ahead > 1.0e-8f)
		hrtf_angles(right, up, ahead, &azimuth, &elevation);
	share = 0.5f + (1.0f - fabsf(elevation) / 90.0f) * (fabsf(azimuth) / 90.0f - 1.0f);
	share = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
	pair[0] = sqrtf(1.0f - share);
	pair[1] = sqrtf(share);
}

/* the gains of a 3D voice to the left and right (the pan's), the gains of
the front and back pairs of speakers (surround_split) and the distance
rolloff's attenuation (distance_gain); for a mono voice through the HRTF
(hrtf_on: chosen, or crossfading from it), its gain to each ear and its way
there (hrtf_out) */
static void spatialize(const struct sdl_stream *stream, float *left, float *right, float *pair,
	float *distance_gain, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
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
	*distance_gain = attenuation;

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
	surround_split(side, up, ahead, pair);
	*hrtf_on = stream->channels == 1 && (hrtf_enabled || stream->hrtf_mix > 0.0f);
	if (*hrtf_on)
	{
		/* (on 5.1 a sound behind as the direction mirrored ahead) */
		hrtf_place(side, up, output_channels == 6 ? fabsf(ahead) : ahead, distance,
			output_channels == 6 && ahead < 0.0f, hrtf_out);
		hrtf_out->gain = 0.70710678f * attenuation * gain_from_millibels(stream->direct);
		hrtf_out->pair[0] = pair[0];
		hrtf_out->pair[1] = pair[1];
	}
	*left *= attenuation * gain_from_millibels(stream->direct);
	*right *= attenuation * gain_from_millibels(stream->direct);
}

/* the voice's gains to the left and right, and to the reverb (only 3D voices
send to it, as their I3DL2 mix bin did on the Xbox), with the coefficients of
the low passes of its direct path (3D voices: obstruction and occlusion
muffle it) and of the send; a 3D voice's gains of the front and back pairs
of speakers (pair: surround_split; others 1 and 0); a 3D voice through the
HRTF (hrtf_on) has its way to each ear too (hrtf_out) */
static void voice_gains(const struct sdl_stream *stream, float *left, float *right, float *pair, float *room,
	float *direct_lowpass, float *room_lowpass, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	float level_3d = -1.0f, level_direct = -1.0f;

	pair[0] = 1.0f;
	pair[1] = 0.0f;
	*room = 0.0f;
	*direct_lowpass = 0.0f;
	*room_lowpass = 0.0f;
	*hrtf_on = FALSE;
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float distance_gain, cosine = frequency_cosine(environment.flHFReference);

		spatialize(stream, left, right, pair, &distance_gain, hrtf_on, hrtf_out);
		level_3d = VOICE_LEVEL_3D * distance_gain * stream->volume;
		level_direct = level_3d * gain_from_millibels(stream->direct);
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			/* VOICE_LEVEL_3D times the room level and the direct path's
			distance rolloff, whatever the room rolloff: recorded on an
			Xbox (issue 792's eighth run), the cave's reverb of a noise
			burst 0.5 to 20 m away (minimum distance 1 m) is 6.0 dB over
			its room level and rolloff to within 0.1 dB, at room levels of
			-20, -10 and 0 dB and the source's own -10 alike, and a room
			rolloff factor of 1 leaves it as it was. (#790's cap at the
			rolloff, fitted at 2 and 10 m, was 4 dB loud from 3 m and 6
			dB quiet within 1 m) */
			*room = VOICE_LEVEL_3D * gain_from_millibels(level) * distance_gain;
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
	/* (the Xbox's quietest levels before audio.volume, which it did not
	have: voice_level) */
	if (level_3d < 0.0f)
	{
		*left = voice_level(*left * stream->volume) * master_volume;
		*right = voice_level(*right * stream->volume) * master_volume;
		*room = voice_level(*room * stream->volume) * master_volume;
	}
	else
	{
		/* a 3D voice's are those of its volume and distance rolloff
		together, at VOICE_LEVEL_3D times, its other gains after them:
		recorded on an Xbox (issue 714's seventh run), a voice 10 m away
		(-20 dB of rolloff, -23 with the pan's) stepped down from -20 dB
		played as a 2D voice 14 dB quieter would, its direct path and its
		reverb alike: exact to -30 dB, -40 at -42.8, -42 at -52.1, silent
		from -44 (its whole level's would be silent from -34). Its direct
		path's are of its direct level too (obstruction and occlusion
		with it), its reverb's not: recorded again (issue 792's eighth
		run), voices 0.5 to 40 m away, ahead, to the right and at 45
		degrees, went silent where this says to within a dB, each ear at
		the same step (the pan after it), and a voice 10 m away with a
		direct level of -10 dB, or obstructed by -10 dB, 10 dB sooner,
		its reverb where the voice's without it went. The HRTF's gain
		too: without it every mono 3D voice on headphones played at full
		volume, whatever audio.volume, audio.effects_volume and the
		game's own gain said, and only the 2D voices followed them */
		float scale = level_direct > 0.0f ?
			voice_level(level_direct) / level_direct * stream->volume * master_volume : 0.0f;
		float room_scale = level_3d > 0.0f ? voice_level(level_3d) / level_3d * stream->volume * master_volume : 0.0f;

		*left *= scale;
		*right *= scale;
		*room *= room_scale;
		if (*hrtf_on)
			hrtf_out->gain *= scale;
	}
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
	- a 3D voice's left and right gains (voice_gains: the pan's, with its
	  volume) on the front pair and the back pair, at their gains (pair,
	  surround_split), a mono voice's both from its one channel, a stereo
	  voice's from its left and right; through the HRTF, its way to each
	  ear (hrtf_process) instead, crossfaded with these as the choice
	  changes;
	- a stereo voice positioned in the world has its channels' middle (the
	  first row) panned towards it in the front half, and their difference
	  (the second) kept on the front pair as wide as the stereo mix keeps it;
	- a 2D voice goes by its mix bins. */
static void voice_surround_gains(const struct sdl_stream *stream, float left, float right, const float *pair,
	float gains[2][MAXIMUM_OUTPUT_CHANNELS])
{
	unsigned long speaker;

	memset(gains, 0, 2 * sizeof(gains[0]));
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		unsigned long second = stream->channels == 2 ? 1 : 0;

		gains[0][0] = left * pair[0];
		gains[0][4] = left * pair[1];
		gains[second][1] = right * pair[0];
		gains[second][5] = right * pair[1];
		return;
	}
	if (stream->channels == 2 && stream->stereo_positioned)
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
	/* (voice_level, as voice_gains) */
	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
	{
		gains[0][speaker] = voice_level(gains[0][speaker] * stream->volume) * master_volume;
		gains[1][speaker] = voice_level(gains[1][speaker] * stream->volume) * master_volume;
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
/* the Xbox's resampler, as a voice's reverb send hears it: a sinc low pass at
the source's own Nyquist rate, Hann windowed to SEND_ZERO_CROSSINGS frames
each side (six taps), and never narrowed for a voice played faster than the
output rate, so that it lets through about half of the source's top few
kilohertz's images (-6 dB at the source's Nyquist rate) and aliases what a
voice pitched up has above the output's. The reverb takes every other frame
of the send unfiltered (reverb_process), which folds the send's top octave
onto its bottom one: so a 44.1 kHz voice's images at 22 to 24 kHz are the
reverb's low end. Recorded on an Xbox (OpenH1 issue 714, its sixth run:
clicks and noise in the cave at 22.05, 44.1 and 48 kHz, at pitches 0.75 to
1.5), a 44.1 kHz click's reverberation is 3.6 dB louder below 1 kHz at pitch
1 than through the mixer's resampler, 6 dB pitched up 1.5 times, and a 48
kHz one's 1 dB at 0.75; this low pass gives each within 0.5 dB (a width of
three frames each side fits best, of two to eight, and a cutoff at the
source's Nyquist rate, of 0.9 to 1.1 of it). At a phase of 0 it is the
frames themselves, so a voice at the output's rate sends its frames as they
are, as the Xbox's did. The direct path has a longer one (direct_phases). A row
a phase, and its step to the next, as resampler_phases and resampler_deltas */
static float send_phases[RESAMPLER_TABLE_STEPS + 1][SEND_TAPS] RESAMPLER_ALIGNED;
static float send_deltas[RESAMPLER_TABLE_STEPS][SEND_TAPS] RESAMPLER_ALIGNED;

/* the Xbox's resampler on a voice's direct path, audio.resampler's "xbox"
(the default): a low pass at the source's Nyquist rate, never narrowed, over
four source frames each side, as recorded on an Xbox (OpenH1 issue 792, its
eighth run: two trains of sixteen one-sample clicks at 22.05 kHz, pitched 1
and 0.9, each click on another phase, the clicks' frames laid on one time
line by the rate the Xbox played them at). In each source frame from the
center it is a polynomial of the distance into the frame, as here: these
play the recordings back to within -70 dB, with corners at the whole frames,
where the best smooth low pass's is -35 dB. With it the Xbox's 22.05 kHz
voices are played as the Xbox played them: the passband (-2.2 dB at 0.4 of
the source's rate, -6.9 at 0.5; the Kaiser windowed sinc fitted before, -1.9
and -6.0), the images a 4 kHz tone leaves at 0.82, 1.18 and 1.82 of the
source's rate (-63, -71 and -79 dB: the sinc's -70, -99, -101) and the low
pass's lobe from 1.3 to 1.6 of it, at -33 dB, which comes down to the
output's 13 to 19 kHz (issue 714's seventh run: a 22.05 kHz click's images
above 16 kHz, 9 to 40 dB over any smooth low pass's). It is the
coefficients, not their phases: tables of 16 to 64 phases, unblended, fit
the clicks worse than one blended. The Xbox's step a frame is rounded too
(22050 Hz played at 22042.97, 17640 at 17636.72, 24000 and 12000 exactly),
by under 0.6 cents, which is not copied. A row a frame from the center, its
coefficients from the constant up */
static const double direct_kernel[DIRECT_ZERO_CROSSINGS][DIRECT_KERNEL_ORDER + 1] =
{
	{ 1.0007795, -0.2495059, -1.3652166, 0.3404684, 0.4084059, -0.1349805 },
	{ -0.0001219, -0.6004896, 0.7474932, 0.0843083, -0.3094547, 0.0781494 },
	{ -0.0001317, 0.1000168, -0.0728184, -0.1081077, 0.1009553, -0.0199766 },
	{ -0.0001926, -0.0076839, -0.0064829, 0.0419929, -0.0412200, 0.0134312 },
};
/* direct_kernel as a table of phases: a row a phase, and its step to the
next, as resampler_phases */
static float direct_phases[RESAMPLER_TABLE_STEPS + 1][DIRECT_TAPS] RESAMPLER_ALIGNED;
static float direct_deltas[RESAMPLER_TABLE_STEPS][DIRECT_TAPS] RESAMPLER_ALIGNED;

/* how a voice's frames are converted to the output's rate: the Xbox's low
pass (send_phases), the mixer's windowed sinc low pass, or between the two
frames either side (linear: it lets more of the images above the source's
rate through, which some hear as brighter) */
enum resampler_mode
{
	RESAMPLER_XBOX,
	RESAMPLER_SINC,
	RESAMPLER_LINEAR
};
/* audio.resampler */
static enum resampler_mode resampler_mode = RESAMPLER_XBOX;

/* audio.resampler's mode: the Xbox's for any value but "sinc" and "linear" */
static enum resampler_mode resampler_mode_read(void)
{
	const char *setting = config_string("audio.resampler");

	if (setting && !strcmp(setting, "sinc"))
		return RESAMPLER_SINC;
	return setting && !strcmp(setting, "linear") ? RESAMPLER_LINEAR : RESAMPLER_XBOX;
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
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS + 1; phase++)
	{
		for (tap = 0; tap < SEND_TAPS; tap++)
		{
			double distance = (double)((long)tap + 1 - SEND_ZERO_CROSSINGS) - (double)phase / RESAMPLER_TABLE_STEPS;
			double angle = 3.14159265358979 * distance;

			/* (on a frame, exactly it, and 0 at the others) */
			if (phase % RESAMPLER_TABLE_STEPS == 0)
				send_phases[phase][tap] = distance == 0.0 ? 1.0f : 0.0f;
			else if (fabs(distance) >= SEND_ZERO_CROSSINGS)
				send_phases[phase][tap] = 0.0f;
			else
			{
				send_phases[phase][tap] = (float)(sin(angle) / angle *
					(0.5 + 0.5 * cos(angle / SEND_ZERO_CROSSINGS)));
			}
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < SEND_TAPS; tap++)
			send_deltas[phase][tap] = send_phases[phase + 1][tap] - send_phases[phase][tap];
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS + 1; phase++)
	{
		for (tap = 0; tap < DIRECT_TAPS; tap++)
		{
			double distance = fabs((double)((long)tap + 1 - DIRECT_ZERO_CROSSINGS) -
				(double)phase / RESAMPLER_TABLE_STEPS);
			unsigned long frame = (unsigned long)distance;

			/* (on a frame, exactly it, and 0 at the others) */
			if (phase % RESAMPLER_TABLE_STEPS == 0)
				direct_phases[phase][tap] = distance == 0.0 ? 1.0f : 0.0f;
			else if (frame >= DIRECT_ZERO_CROSSINGS)
				direct_phases[phase][tap] = 0.0f;
			else
			{
				double into = distance - (double)frame, weight = 0.0;
				int power;

				for (power = DIRECT_KERNEL_ORDER; power >= 0; power--)
					weight = weight * into + direct_kernel[frame][power];
				direct_phases[phase][tap] = (float)weight;
			}
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < DIRECT_TAPS; tap++)
			direct_deltas[phase][tap] = direct_phases[phase + 1][tap] - direct_phases[phase][tap];
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

/* frames[0, DIRECT_TAPS) weighted by a row of direct_phases, as resampler_dot */
static float direct_dot(const float *frames, const float *weights, const float *deltas, float blend)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < DIRECT_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		sum += *(const resampler_unaligned_vector *)(frames + tap) * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < DIRECT_TAPS; tap++)
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

/* the voice's frame at its phase, its channels' middle, through the Xbox's
resampler (send_phases), for the reverb's send: as resampler_dot, over
SEND_TAPS */
static float send_frame(const struct sdl_stream *stream)
{
	double position = stream->phase * RESAMPLER_TABLE_STEPS;
	unsigned long row = (unsigned long)position;
	float blend = (float)(position - (double)row);
	unsigned long first = (stream->center + 1 - SEND_ZERO_CROSSINGS) % RESAMPLER_HISTORY;
	const float *left = stream->history[0] + first, *right = stream->history[1] + first;
	const float *weights = send_phases[row], *deltas = send_deltas[row];
	int tap;
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };

	for (tap = 0; tap < SEND_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;
		resampler_vector frames = *(const resampler_unaligned_vector *)(left + tap);

		if (stream->channels == 2)
			frames = (frames + *(const resampler_unaligned_vector *)(right + tap)) * 0.5f;
		sum += frames * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;

	for (tap = 0; tap < SEND_TAPS; tap++)
	{
		float frame = stream->channels == 2 ? 0.5f * (left[tap] + right[tap]) : left[tap];

		sum += frame * (weights[tap] + deltas[tap] * blend);
	}
	return sum;
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

/* ---------- a slow mix's parts

HALO_AUDIO_MIX_TIMES=<seconds> (host_sdl.c's mix times) also times the
parts of each mix here, and keeps those of any mix over
HALO_AUDIO_MIX_SLOW_MS (2 by default) for the game's thread to log
(DirectSoundDoWork): the voices' count and what of them was mixed, each part's
time, and the number of the device's request it answered (as host_sdl.c
counts them, which logs the mix's processor time and page faults by the same
number). Off, it costs a test of mix_profile_on a mix and one a voice.
HALO_AUDIO_MUTED=1 mixes as ever and plays silence (for measuring with the
device's own timing and nobody hearing it). */

enum
{
	_mix_part_hand_off,
	_mix_part_voices,
	_mix_part_hrtf,
	_mix_part_publish,
	_mix_part_buffers,
	_mix_part_reverb,
	_mix_part_chat,
	_mix_part_rest,
	_mix_part_put,
	_mix_part_callback,
	NUMBER_OF_MIX_PARTS
};

static const char *const mix_part_names[NUMBER_OF_MIX_PARTS] =
	{ "hand-off", "voices", "HRTF", "publish", "buffers", "reverb", "chat", "rest", "put", "callback's own" };

struct mix_profile
{
	unsigned long request;
	unsigned long frames;
	/* the voices listed, those mixed (not paused, with packets), through the
	HRTF, started over (their resampler reset), and turned all the way down */
	unsigned long voices, mixed, hrtf, started, silent;
	Uint64 total_ns;
	Uint64 parts_ns[NUMBER_OF_MIX_PARTS];
};

#define MIX_PROFILES_KEPT 64
static BOOL mix_profile_on;
static BOOL mix_muted;
static Uint64 mix_slow_ns;
/* (the mixer's thread) the mix being timed; the device's requests */
static struct mix_profile mix_profile;
static unsigned long mix_requests;
/* the slow ones, written by the mixer's thread and read by the game's */
static struct mix_profile mix_profiles[MIX_PROFILES_KEPT];
static volatile unsigned long mix_profiles_written, mix_profiles_read;
static unsigned long mix_profiles_lost;

static void mix_profile_initialize(void)
{
	const char *times = getenv("HALO_AUDIO_MIX_TIMES");
	const char *slow = getenv("HALO_AUDIO_MIX_SLOW_MS");
	const char *muted = getenv("HALO_AUDIO_MUTED");

	mix_profile_on = times && atof(times) > 0.0;
	mix_slow_ns = (Uint64)((slow && atof(slow) > 0.0 ? atof(slow) : 2.0) * 1.0e6);
	mix_muted = muted && muted[0] == '1';
	if (mix_profile_on)
		platform_log("sound: the parts of each mix over %.2f ms are logged", (double)mix_slow_ns / 1.0e6);
	if (mix_muted)
		platform_log("sound: muted (HALO_AUDIO_MUTED): mixed as ever, played as silence");
}

/* (the mixer's thread) the time since *at added to the part, and *at now */
static void mix_part_end(int part, Uint64 *at)
{
	Uint64 now = SDL_GetTicksNS();

	mix_profile.parts_ns[part] += now - *at;
	*at = now;
}

/* (the mixer's thread) a request begun: its mixes' parts from 0 */
static void mix_profile_begin(void)
{
	memset(&mix_profile, 0, sizeof(mix_profile));
	mix_profile.request = mix_requests;
}

/* (the mixer's thread) the request answered, begun at began: kept if it
was slow, what its mixes and putting the mix in did not take counted the
callback's own */
static void mix_profile_end(Uint64 began)
{
	unsigned long written = mix_profiles_written;
	Uint64 parts = 0;
	int part;

	mix_profile.total_ns = SDL_GetTicksNS() - began;
	if (mix_profile.total_ns < mix_slow_ns)
		return;
	for (part = 0; part < _mix_part_callback; part++)
		parts += mix_profile.parts_ns[part];
	mix_profile.parts_ns[_mix_part_callback] = mix_profile.total_ns > parts ? mix_profile.total_ns - parts : 0;
	if (written - __atomic_load_n(&mix_profiles_read, __ATOMIC_ACQUIRE) >= MIX_PROFILES_KEPT)
	{
		mix_profiles_lost++;
		return;
	}
	mix_profiles[written % MIX_PROFILES_KEPT] = mix_profile;
	__atomic_store_n(&mix_profiles_written, written + 1, __ATOMIC_RELEASE);
}

/* (the game's thread) the slow mixes kept, logged */
static void mix_profiles_log(void)
{
	unsigned long read = mix_profiles_read;

	while (read != __atomic_load_n(&mix_profiles_written, __ATOMIC_ACQUIRE))
	{
		const struct mix_profile *profile = &mix_profiles[read % MIX_PROFILES_KEPT];
		char parts[256];
		int length = 0, part;

		for (part = 0; part < NUMBER_OF_MIX_PARTS; part++)
		{
			length += snprintf(parts + length, sizeof(parts) - (size_t)length, "%s%s %.3f", part ? ", " : "",
				mix_part_names[part], (double)profile->parts_ns[part] / 1.0e6);
			if (length >= (int)sizeof(parts))
				break;
		}
		platform_log("sound: slow mix (request %lu): %.3f ms for %lu frames; %lu voices, %lu mixed (%lu through the "
			"HRTF, %lu started, %lu silent); %s ms%s", profile->request, (double)profile->total_ns / 1.0e6,
			profile->frames, profile->voices, profile->mixed, profile->hrtf, profile->started, profile->silent, parts,
			mix_profiles_lost ? " (some lost)" : "");
		read++;
		__atomic_store_n(&mix_profiles_read, read, __ATOMIC_RELEASE);
	}
}

/* ---------- mixing */

/* mixes one voice into output (frames of output_channels floats), and into
the reverb's send (frames of mono) */
static void mix_voice(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	double step, target_step, ramp_step;
	float target_left, target_right, target_room, left, right, room, ramp_left, ramp_right, ramp_room, scale;
	float target_direct_lowpass, target_room_lowpass, direct_lowpass, room_lowpass;
	float ramp_direct_lowpass, ramp_room_lowpass, share;
	float direct_state[2], room_state;
	long width;
	unsigned long frame, ramp_frames;
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
	BOOL surround = output_channels == 6, surround_heard = FALSE;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS], surround_gains[2][MAXIMUM_OUTPUT_CHANNELS];
	float ramp_surround[2][MAXIMUM_OUTPUT_CHANNELS], pair[2];
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
		doppler_heard(stream);
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
		if (mix_profile_on)
			mix_profile.started++;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
		step = target_step;
		scale = resampler_scale(step);
		width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	}
	voice_gains(stream, &target_left, &target_right, pair, &target_room, &target_direct_lowpass,
		&target_room_lowpass, &hrtf_on, &target_hrtf);
	if (mix_profile_on)
	{
		mix_profile.mixed++;
		mix_profile.started += !stream->gains_valid;
		mix_profile.silent += !target_left && !target_right && !target_room;
	}
	/* how much of the voice the HRTF plays: towards all of it while it is
	chosen (hrtf_enabled), none otherwise, a frame's share of the crossfade
	each frame (hrtf_mix_step), however many frames the mixes have; a voice
	starting is at once where it is going */
	hrtf_mix = hrtf_on ? stream->hrtf_mix : 0.0f;
	hrtf_mix_end = hrtf_on && hrtf_enabled ? 1.0f : 0.0f;
	if (!stream->gains_valid)
		hrtf_mix = hrtf_mix_end;
	ramp_hrtf_mix = hrtf_mix_end > hrtf_mix ? 1.0f / HRTF_FADE_FRAMES :
		hrtf_mix_end < hrtf_mix ? -1.0f / HRTF_FADE_FRAMES : 0.0f;
	hrtf_running = hrtf_on && (hrtf_mix > 0.0f || hrtf_mix_end > 0.0f);
	/* (its filters start from silence, as the crossfade starts from none
	of them) */
	if (hrtf_running && (!stream->gains_valid || !stream->hrtf_on))
	{
		stream->hrtf_current = target_hrtf;
		memset(stream->hrtf_frames, 0, sizeof(stream->hrtf_frames));
		memset(stream->hrtf_bands, 0, sizeof(stream->hrtf_bands));
	}
	stream->hrtf_on = hrtf_running;
	if (surround)
		voice_surround_gains(stream, target_left, target_right, pair, target_surround);
	/* a voice starting is at once at its gains, step and way through the
	HRTF; a change to any of them, from the last mix's, ramps from where
	they are to the new ones over PARAMETER_RAMP_FRAMES, then holds them: a
	ramp the mix's frames end goes on in the next mix, and one changed
	before its end ramps on from where it was, so that a change takes the
	same frames however many the mixes have */
	if (!stream->gains_valid)
	{
		if (surround)
			memcpy(stream->current_surround, target_surround, sizeof(target_surround));
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->current_room = target_room;
		stream->current_step = target_step;
		stream->current_direct_lowpass = target_direct_lowpass;
		stream->current_room_lowpass = target_room_lowpass;
		stream->direct_lowpass[0] = stream->direct_lowpass[1] = 0.0f;
		stream->room_lowpass = 0.0f;
		stream->ramp_frames = 0;
		stream->gains_valid = TRUE;
	}
	else if (target_left != stream->target_left || target_right != stream->target_right ||
		target_room != stream->target_room || target_step != stream->target_step ||
		target_direct_lowpass != stream->target_direct_lowpass || target_room_lowpass != stream->target_room_lowpass ||
		(surround && memcmp(target_surround, stream->target_surround, sizeof(target_surround))) ||
		(hrtf_running && memcmp(&target_hrtf, &stream->hrtf_target, sizeof(target_hrtf))))
	{
		stream->ramp_frames = PARAMETER_RAMP_FRAMES;
	}
	stream->target_left = target_left;
	stream->target_right = target_right;
	stream->target_room = target_room;
	stream->target_step = target_step;
	stream->target_direct_lowpass = target_direct_lowpass;
	stream->target_room_lowpass = target_room_lowpass;
	if (surround)
		memcpy(stream->target_surround, target_surround, sizeof(target_surround));
	if (hrtf_running)
		stream->hrtf_target = target_hrtf;
	ramp_frames = stream->ramp_frames;
	share = ramp_frames ? 1.0f / (float)ramp_frames : 0.0f;
	step = stream->current_step;
	ramp_step = ramp_frames ? (target_step - step) / (double)ramp_frames : 0.0;
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
	ramp_left = (target_left - left) * share;
	ramp_right = (target_right - right) * share;
	ramp_room = (target_room - room) * share;
	ramp_direct_lowpass = (target_direct_lowpass - direct_lowpass) * share;
	ramp_room_lowpass = (target_room_lowpass - room_lowpass) * share;
	hrtf_mix_start = hrtf_mix;
	if (surround)
	{
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			surround_gains[0][speaker] = stream->current_surround[0][speaker];
			surround_gains[1][speaker] = stream->current_surround[1][speaker];
			ramp_surround[0][speaker] = (target_surround[0][speaker] - surround_gains[0][speaker]) * share;
			ramp_surround[1][speaker] = (target_surround[1][speaker] - surround_gains[1][speaker]) * share;
			/* (a 2D voice may be heard from the center, the backs or the
			LFE alone, its left and right gains 0) */
			if (surround_gains[0][speaker] || surround_gains[1][speaker] || target_surround[0][speaker] ||
				target_surround[1][speaker])
			{
				surround_heard = TRUE;
			}
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

		if (!left && !right && !room && !ramp_left && !ramp_right && !ramp_room && !surround_heard)
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
		else if (resampler_mode == RESAMPLER_XBOX)
		{
			/* the Xbox's low pass (direct_phases), never narrowed */
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			unsigned long first = (stream->center + 1 - DIRECT_ZERO_CROSSINGS) % RESAMPLER_HISTORY;

			sample_left = direct_dot(stream->history[0] + first, direct_phases[row], direct_deltas[row], blend);
			sample_right = stream->channels == 1 ? sample_left :
				direct_dot(stream->history[1] + first, direct_phases[row], direct_deltas[row], blend);
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
		/* the room send, with its own low pass, through the Xbox's resampler
		(send_phases), not the mixer's: the reverb folds the send's top
		octave onto its bottom one, and the Xbox's let through images and
		aliases there that the mixer's takes out (recorded, the cave's
		reverberation of a click was 4 dB louder below 500 Hz at 48 kHz, 3.6
		dB at 44.1 kHz) */
		if (room || ramp_room)
		{
			float mono = send_frame(stream);

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
		if (surround)
		{
			/* the first row's gains take the left (a mono voice's only)
			channel, or a positioned voice's middle; the second the right, or
			its difference; through the HRTF (hrtf_process, after), the pan's
			share while the choice changes */
			float *speakers = output + frame * MAXIMUM_OUTPUT_CHANNELS;
			float panned = hrtf_running ? 1.0f - hrtf_mix : 1.0f;
			float first = (positioned ? 0.5f * (sample_left + sample_right) : sample_left) * panned;
			float second = (positioned ? 0.5f * (sample_left - sample_right) :
				stream->channels == 2 ? sample_right : 0.0f) * panned;

			if (hrtf_running)
			{
				hrtf_direct[frame] = sample_left;
				hrtf_mix = hrtf_mix_step(hrtf_mix, ramp_hrtf_mix, hrtf_mix_end);
			}
			for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			{
				speakers[speaker] += first * surround_gains[0][speaker] + second * surround_gains[1][speaker];
				surround_gains[0][speaker] += ramp_surround[0][speaker];
				surround_gains[1][speaker] += ramp_surround[1][speaker];
			}
		}
		else if (hrtf_running)
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
			hrtf_mix = hrtf_mix_step(hrtf_mix, ramp_hrtf_mix, hrtf_mix_end);
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
		/* (at the ramp's end, exactly the new ones, held) */
		if (frame + 1 == ramp_frames)
		{
			left = target_left;
			right = target_right;
			room = target_room;
			direct_lowpass = target_direct_lowpass;
			room_lowpass = target_room_lowpass;
			step = target_step;
			ramp_left = ramp_right = ramp_room = ramp_direct_lowpass = ramp_room_lowpass = 0.0f;
			ramp_step = 0.0;
			if (surround)
			{
				memcpy(surround_gains, target_surround, sizeof(surround_gains));
				memset(ramp_surround, 0, sizeof(ramp_surround));
			}
		}
	}
	stream->direct_lowpass[0] = direct_state[0];
	stream->direct_lowpass[1] = direct_state[1];
	stream->room_lowpass = room_state;
	/* (the frames mixed: all of them, or those before the voice ran dry) */
	if (hrtf_running)
	{
		Uint64 hrtf_began = mix_profile_on ? SDL_GetTicksNS() : 0;

		hrtf_process(stream, hrtf_direct, output, frame, ramp_frames, &target_hrtf, hrtf_mix_start, ramp_hrtf_mix,
			hrtf_mix_end);
		if (mix_profile_on)
		{
			mix_profile.parts_ns[_mix_part_hrtf] += SDL_GetTicksNS() - hrtf_began;
			mix_profile.hrtf++;
		}
	}
	stream->hrtf_mix = hrtf_running ? hrtf_mix : hrtf_mix_end;
	/* where the ramp got to, and the frames of it left */
	stream->current_left = left;
	stream->current_right = right;
	stream->current_room = room;
	stream->current_step = step;
	stream->current_direct_lowpass = direct_lowpass;
	stream->current_room_lowpass = room_lowpass;
	if (surround)
		memcpy(stream->current_surround, surround_gains, sizeof(surround_gains));
	stream->ramp_frames = ramp_frames > frame ? ramp_frames - frame : 0;
}

/* ---------- reverb

The Xbox ran an I3DL2 reverb on its audio DSP (the effects image the game
downloads), fed by every 3D voice's I3DL2 mix bin. The game sets the listener
properties from the sound environment the camera is in (sound_dsound_xbox.c
dsound_set_listener_properties), and each 3D voice's room send from its
reverb attenuation and occlusion (dsound_channel_set_I3DL2_properties). This
is that DSP program's reverb, as decoded from the image and from the XDK
DirectSound's conversion of the listener properties into its parameters (in
every retail default.xbe), redone in floating point:
	- the send (here the mono bus, the room level and the Room HF low pass
	  already on it) is halved (and turned over at a Room HF of 0, as the
	  DSP's filter word did), and the reverb runs at half the output rate,
	  24 kHz: it takes every other frame and plays each of its frames twice,
	  as the DSP did (no filter either way: the reverberation stops at 12
	  kHz, with the images of the repeat above, and the send's top octave
	  is folded onto its bottom one, which is why a voice at the output's
	  rate sends its frames unfiltered, mix_voice);
	- every delay is a whole number of the DSP's 4-sample words, and the
	  input's delay line is REVERB_INPUT_SIZE long, a longer delay wrapping
	  round it as the DSP's did;
	- the reflections: on each of four channels (front left and right, back
	  left and right), the input the reflections delay after the sound, and
	  again a channel's share of the reverb delay after that, through a
	  feedback comb of a few milliseconds (gains -0.4, 0.4, -0.4, -0.4), at
	  the reflections level times a channel's weight (0.57, 0.52, 0.54,
	  0.6);
	- the late reverberation: a feedback delay network of four lines of
	  fixed lengths (58 to 103 ms) mixed by a Householder reflection, each
	  line decaying by 60 dB in the decay time (Jot: a gain of
	  10^(-3 length / (rate decay)), whole millibels), its high frequencies
	  in the decay time times the HF ratio at the HF reference (a one-pole
	  low pass; a ratio over 1 is held to 1, as the DSP's conversion does).
	  Into it go taps of the input the reflections and reverb delays after
	  the sound (and 2.4, 5.6 and 9.7 ms later), and the taps of a chain of
	  four delays (up to a second long, as the decay time and the density
	  make it, each decayed as the lines are), at 0.2 times the diffusion;
	  the chain's taps also go, at 1 minus the diffusion, round two loops as
	  long as the chain. Each channel hears a line, at the reverb level, and
	  a loop at half of it;
	- the four channels play from the front and back pairs on 5.1, and on
	  stereo each side's back channel at -3 dB with its front, as the
	  Xbox's stereo downmix (its encode processor's: left = front left +
	  0.707 center + 0.707 back left) played the back speakers' mix bins.
A change of environment crossfades each delay's read from its old length to
its new one, and each gain and filter, over REVERB_FADE_FRAMES: what is
reverberating carries on, without the click of a jump or the pitch shift of
a sliding delay (the DSP faded each tap out and in again, one by one). The
fade is short, 2 ms: recorded on an Xbox, a click played as the
environment was set, 2 ms into its sound, echoed as the new room alone, in
every environment (over 85 ms, as it was, its echo here was the room before's
for the most part, and 1 to 4 dB under the Xbox's, and the cave's, its
reverberation turned over, faded through nothing). The game moves the
environment a step each tick on the way to another (scenario.c), and each
step waits for the fade to the last to end (mix). audio.reverb = false fades
the reverb out over REVERB_LEVEL_FADE_FRAMES and stops it. Once
nothing has been sent for REVERB_QUIET_FRAMES and the reverberation is 120 dB
down, the reverb stops until something is sent again (outdoors, in the
menus). */

#define REVERB_RATE (OUTPUT_RATE / 2)
#define REVERB_INPUT_SIZE 6000 /* the input's and the chain's delays, a quarter of a second */
#define REVERB_LOOP_SIZE 24000 /* the loops' */
#define REVERB_CHANNELS 4
#define REVERB_LINES 4
#define REVERB_CHAIN 4
#define REVERB_COMB_SIZE 720
#define REVERB_FADE_FRAMES 96 /* 2 ms */
/* a new environment's delays wait 100 ms (REVERB_DELAY_HOLD_FRAMES): recorded
on an Xbox (the test bed's ninth run), a click played 2 or 25 ms after the
hallway gave way to the cave echoed at the cave's levels but at the
hallway's delays (its reflections from 7 ms, its late reverberation from
18), and one played 100 ms after it as in the cave alone (fitted: a hold of
4800 frames, the click trains' echoes within 4 to 7 dB rms of the
recordings', 15 before; 4200 to 5400 fit alike) */
#ifndef REVERB_DELAY_HOLD_FRAMES
#define REVERB_DELAY_HOLD_FRAMES 4800
#endif
#define REVERB_LEVEL_FADE_FRAMES 4096 /* 85 ms */
/* the input's delay, the chain and a loop, played out */
#define REVERB_QUIET_FRAMES (2 * (REVERB_INPUT_SIZE * REVERB_CHAIN + REVERB_LOOP_SIZE))
#define REVERB_QUIET_PEAK 1.0e-6f

/* each channel's reflections: its weight, its tap's part of the reverb
delay, and its comb's delay (a part of the reverb delay and seconds), gain
and line's length (its longest delay) */
static const float reverb_reflection_weights[REVERB_CHANNELS] = { 0.57f, 0.52f, 0.54f, 0.6f };
static const float reverb_reflection_taps[REVERB_CHANNELS] = { 0.1052f, 0.0673f, 0.1737f, 0.2404f };
static const float reverb_comb_parts[REVERB_CHANNELS] = { 0.2469f, 0.2209f, 0.2311f, 0.2608f };
static const float reverb_comb_seconds[REVERB_CHANNELS] = { 0.003651f, 0.003515f, 0.003673f, 0.003946f };
static const float reverb_comb_gains[REVERB_CHANNELS] = { -0.4f, 0.4f, -0.4f, -0.4f };
static const long reverb_comb_sizes[REVERB_CHANNELS] = { 680, 612, 640, 720 };
/* the lines' lengths (58, 70.3, 85.2 and 103.3 ms) */
static const long reverb_line_lengths[REVERB_LINES] = { 1392, 1684, 2044, 2476 };
/* the taps of the input into the lines, after the reverb delay; which pair
of them goes into each line, with what signs (times the diffusion) */
static const long reverb_onset_taps[REVERB_LINES] = { 0, 58, 134, 232 };
static const float reverb_onset_signs[2 * REVERB_LINES] = { 1, -1, -1, 1, 1, 1, -1, -1 };
/* the chain's two taps on each of its delays, in parts of its length (the
second goes on into the next delay) */
static const float reverb_chain_taps[REVERB_CHAIN][2] =
{
	{ 0.040023f, 0.021224f }, { 0.288186f, 0.37542f }, { 0.603764f, 0.623583f }, { 0.851905f, 0.759229f }
};

/* what an environment sets: delays in the reverb's samples (multiples of
4), gains, and the one-pole filters' coefficients (y = a y' + b x) */
struct reverb_filter
{
	float a, b;
};

struct reverb_parameters
{
	long reflections_delay, reflection_taps[REVERB_CHANNELS], comb_delays[REVERB_CHANNELS];
	long late_delay, chain[REVERB_CHAIN][2], loop_length;
	float reflections_gains[REVERB_CHANNELS], late_gain;
	float input_gain, onset_gains[2 * REVERB_LINES], loop_input, network_input;
	struct reverb_filter lines[REVERB_LINES], chain_filter, loop_filter;
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
	/* the delays a change leaves in place (delays_from) for delay_hold
	frames before they cross to the new environment's over
	REVERB_FADE_FRAMES (delay_fade): the gains and filters are the new
	environment's at once (fade), its delays only after the hold */
	struct reverb_parameters delays_from;
	unsigned long delay_hold, delay_fade;
	/* the output's level, 1 while audio.reverb is on, moving to 0 when it
	is turned off; whether the buffers are silent; and the frames since the
	send or the output was last over REVERB_QUIET_PEAK */
	float level;
	BOOL silent;
	unsigned long quiet;

	/* whether the next output frame is the second of a reverb frame, and
	that frame's channels */
	BOOL odd;
	float held[REVERB_CHANNELS];

	/* the delays, each written at position (of its size) */
	float input[REVERB_INPUT_SIZE];
	float chain[REVERB_CHAIN - 1][REVERB_INPUT_SIZE];
	float combs[REVERB_CHANNELS][REVERB_COMB_SIZE];
	float lines[REVERB_LINES][2476];
	float loops[2][REVERB_LOOP_SIZE];
	unsigned long position;
	/* the filters' last outputs */
	float line_state[REVERB_LINES], chain_state[REVERB_CHAIN - 1], loop_state[2];
} reverb;

static float clamp_real(float value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

/* the DSP's: a float to a whole number, towards 0; a delay up to a whole
word, and down */
static long reverb_whole(double value)
{
	return (long)value;
}

static long reverb_word_up(long samples)
{
	return (samples + 1) & ~3L;
}

static long reverb_word_down(long samples)
{
	return samples & ~3L;
}

/* 10^(millibels / 2000), as the conversion works it out (in single
precision) */
static float reverb_gain(long millibels)
{
	return (float)pow(10.0, (double)((float)millibels * 0.0005f));
}

/* the one-pole filter whose gain is 10^(level / 2000) at low frequencies
and 10^((level + high) / 2000) at hertz, at the reverb's rate; levels at or
under -10000 silence it */
static struct reverb_filter reverb_filter_set(long level, long high, float hertz)
{
	struct reverb_filter filter = { 0.0f, 0.0f };
	double cosine, power, root;

	if (level <= DSBVOLUME_MIN)
		return filter;
	if (!high)
	{
		filter.b = level ? reverb_gain(level) : 1.0f;
		return filter;
	}
	cosine = cos(2.0 * 3.14159274 * hertz / REVERB_RATE);
	power = reverb_gain(2 * high);
	root = sqrt(2.0 * power * (1.0 - cosine) - power * power * (1.0 - cosine * cosine));
	filter.a = (float)((1.0 - power * cosine - root) / (1.0 - power));
	filter.b = (1.0f - filter.a) * reverb_gain(level);
	/* under the DSP's least step, the filter passes nothing (whatever its a,
	which can come out at 1) */
	if (filter.b < 1.0f / 8388608.0f)
		filter.a = filter.b = 0.0f;
	return filter;
}

/* the filter of a delay of samples in the environment of properties: what
decays it 60 dB in the decay time, and its high frequencies in the decay
time times the HF ratio (held to 1) */
static struct reverb_filter reverb_decay_filter(long samples, const DSI3DL2LISTENER *properties)
{
	double loss = (double)samples * 4.16666662e-05f / properties->flDecayTime * -6000.0;
	long level = reverb_whole(loss);

	if (properties->flDecayHFRatio < 1.0f)
		loss /= properties->flDecayHFRatio;
	return reverb_filter_set(level, reverb_whole(loss) - level, properties->flHFReference);
}

/* the DSP's parameters of an environment, as DirectSound worked them out */
static void reverb_parameters_set(struct reverb_parameters *parameters, const DSI3DL2LISTENER *properties)
{
	float diffusion = properties->flDiffusion * 0.01f;
	/* how long the chain is: a second, shorter in a room of short decay
	(and more so as the density is less) */
	float length = (float)((2.0 - (double)0.01f * properties->flDensity) * 5.0 * (properties->flDecayTime - 1.0));
	double scale;
	long reverb_delay, previous = 0, chain_length;
	int index;

	length = length < 1.0f ? 1.0f : length > 60.0f ? 60.0f : length;
	scale = length < 2.5f ? ((double)length - (double)0.8f) * (double)0.588235259f : 1.0;

	parameters->reflections_delay = reverb_word_down(reverb_whole(properties->flReflectionsDelay * 24000.0));
	reverb_delay = reverb_word_down(reverb_whole(properties->flReverbDelay * 24000.0));
	parameters->late_delay = reverb_delay + parameters->reflections_delay;
	for (index = 0; index < REVERB_CHANNELS; index++)
	{
		long comb = reverb_word_up(reverb_whole(((double)properties->flReverbDelay * reverb_comb_parts[index] +
			reverb_comb_seconds[index]) * 24000.0));

		parameters->reflection_taps[index] = parameters->reflections_delay +
			reverb_word_up(reverb_whole((double)reverb_delay * reverb_reflection_taps[index]));
		parameters->comb_delays[index] = comb > reverb_comb_sizes[index] ? reverb_comb_sizes[index] : comb;
		parameters->reflections_gains[index] = properties->lReflections <= DSBVOLUME_MIN ? 0.0f :
			reverb_gain(properties->lReflections) * reverb_reflection_weights[index];
	}

	/* the chain: its taps at their parts of its length, each delay's
	measured from the last's second tap and squeezed into the delay */
	chain_length = reverb_word_down(reverb_whole(scale * 24000.0));
	for (index = 0; index < REVERB_CHAIN; index++)
	{
		long first = reverb_word_down(reverb_whole(scale * reverb_chain_taps[index][0] * 24000.0));
		long second = reverb_word_down(reverb_whole(scale * reverb_chain_taps[index][1] * 24000.0));
		long span = (first > second ? first : second) - previous;

		if (span > REVERB_INPUT_SIZE - 1)
		{
			double squeeze = (double)(REVERB_INPUT_SIZE - 1) / span;

			parameters->chain[index][0] = reverb_word_up(reverb_whole((first - previous) * squeeze));
			parameters->chain[index][1] = reverb_word_up(reverb_whole((second - previous) * squeeze));
		}
		else
		{
			parameters->chain[index][0] = first - previous;
			parameters->chain[index][1] = second - previous;
		}
		previous = second;
	}
	parameters->loop_length = chain_length > REVERB_LOOP_SIZE ? REVERB_LOOP_SIZE : chain_length;
	parameters->chain_filter = reverb_decay_filter(chain_length >> 2, properties);
	parameters->loop_filter = reverb_decay_filter(chain_length, properties);
	for (index = 0; index < REVERB_LINES; index++)
		parameters->lines[index] = reverb_decay_filter(reverb_line_lengths[index], properties);

	for (index = 0; index < 2 * REVERB_LINES; index++)
		parameters->onset_gains[index] = clamp_real(reverb_onset_signs[index] * diffusion, -1.0f, 1.0f);
	parameters->loop_input = 1.0f - diffusion;
	parameters->network_input = 0.2f * diffusion;
	parameters->late_gain = properties->lReverb <= DSBVOLUME_MIN ? 0.0f : reverb_gain(properties->lReverb);
	/* the send halved; the DSP's Room HF filter, which the send's low pass
	is, passes it whole at a Room HF of 0 with a gain of 1, which its 24-bit
	word holds as -1: the reverberation upside down (the cave's) */
	parameters->input_gain = properties->lRoomHF ? 0.5f : -0.5f;
}

/* a new environment, faded to unless the reverb is silent. The game sets the
same one again several times a second in some places (Derelict), which
changes nothing */
static void reverb_update(const DSI3DL2LISTENER *properties, unsigned long serial)
{
	BOOL reflections_off, held;

	reverb.serial = serial;
	if (!reverb.silent && !memcmp(properties, &reverb.properties, sizeof(*properties)))
		return;
	reflections_off = reverb.properties.lReflections <= DSBVOLUME_MIN;
	reverb.properties = *properties;
	/* the delays in use held, unless a hold is running already: then they
	cross to whichever environment is the last when it ends (the game moves
	from one to another a step each tick, scenario_get_sound_environment) */
	held = reverb.delay_hold || reverb.delay_fade;
	if (!held)
	{
		reverb.delays_from = reverb.to;
		reverb.delay_hold = REVERB_DELAY_HOLD_FRAMES;
		reverb.delay_fade = REVERB_FADE_FRAMES;
	}
	reverb_parameters_set(&reverb.to, properties);
	/* (from a room with no reflections, Halo's outdoors, the reflections'
	delays are the new room's at once: recorded, the cave's first
	reflections came at its 15 ms while its late reverberation waited) */
	if (!held && reflections_off)
	{
		int index;

		reverb.delays_from.reflections_delay = reverb.to.reflections_delay;
		for (index = 0; index < REVERB_CHANNELS; index++)
		{
			reverb.delays_from.reflection_taps[index] = reverb.to.reflection_taps[index];
			reverb.delays_from.comb_delays[index] = reverb.to.comb_delays[index];
		}
	}
	if (reverb.silent)
		reverb.from = reverb.to;
	reverb.fade = reverb.silent ? 0 : REVERB_FADE_FRAMES;
}

static void reverb_clear(void)
{
	memset(reverb.input, 0, sizeof(reverb.input));
	memset(reverb.chain, 0, sizeof(reverb.chain));
	memset(reverb.combs, 0, sizeof(reverb.combs));
	memset(reverb.lines, 0, sizeof(reverb.lines));
	memset(reverb.loops, 0, sizeof(reverb.loops));
	memset(reverb.line_state, 0, sizeof(reverb.line_state));
	memset(reverb.chain_state, 0, sizeof(reverb.chain_state));
	memset(reverb.loop_state, 0, sizeof(reverb.loop_state));
	memset(reverb.held, 0, sizeof(reverb.held));
	reverb.odd = FALSE;
	reverb.from = reverb.to;
	reverb.fade = 0;
	reverb.silent = TRUE;
	reverb.quiet = 0;
}

static void reverb_initialize(void)
{
	reverb.silent = TRUE;
	reverb.level = reverb_enabled ? 1.0f : 0.0f;
	reverb_update(&environment, environment_serial);
	reverb.delays_from = reverb.to;
	reverb.delay_hold = reverb.delay_fade = 0;
}

/* the delays' hold and crossfade moved on frames (output frames) */
static void reverb_delays_step(unsigned long frames)
{
	unsigned long step;

	step = frames < reverb.delay_hold ? frames : reverb.delay_hold;
	reverb.delay_hold -= step;
	frames -= step;
	if (!reverb.delay_hold && reverb.delay_fade)
	{
		reverb.delay_fade = frames < reverb.delay_fade ? reverb.delay_fade - frames : 0;
		if (frames && !reverb.delay_fade)
			reverb.delays_from = reverb.to;
	}
}

static float reverb_blend(float from, float to, float fade)
{
	return from + (to - from) * fade;
}

/* the sample of a delay line size long that went in delay samples ago: as
the DSP's, a delay of a block (16 samples) or more, and one longer than the
line wrapping round it */
static float reverb_tap(const float *line, long size, long delay)
{
	long position = (long)(reverb.position % (unsigned long)size);

	delay = delay < 16 ? 16 : (delay - 16) % size + 16;
	return line[(position - delay + size) % size];
}

/* the tap crossfaded from the from delay to the to delay by fade (0 to 1) */
static float reverb_read(const float *line, long size, long from, long to, float fade)
{
	float sample = reverb_tap(line, size, from);

	return from == to ? sample : sample + (reverb_tap(line, size, to) - sample) * fade;
}

static void reverb_write(float *line, long size, float value)
{
	line[reverb.position % (unsigned long)size] = flush_denormal(value);
}

static float reverb_filter(struct reverb_filter from, struct reverb_filter to, float fade, float *state, float x)
{
	*state = flush_denormal(reverb_blend(from.a, to.a, fade) * *state + reverb_blend(from.b, to.b, fade) * x);
	return *state;
}

/* a frame of the reverb (at REVERB_RATE) of input, its four channels into
channels */
static void reverb_frame(float input, float fade, float *channels)
{
	const struct reverb_parameters *from = &reverb.from, *to = &reverb.to, *dfrom = &reverb.delays_from;
	/* (the delays' own crossfade: none while they are held) */
	float dfade = reverb.delay_hold ? 0.0f : 1.0f - (float)reverb.delay_fade / REVERB_FADE_FRAMES;
	float onset[2 * REVERB_LINES], taps[REVERB_LINES], chain[REVERB_CHAIN][2], sums[2], loops[2];
	float reflections, network_input, loop_input, mean = 0.0f, late_gain;
	int index;

	/* the reflections: each channel's comb */
	reflections = reverb_read(reverb.input, REVERB_INPUT_SIZE, dfrom->reflections_delay, to->reflections_delay, dfade);
	for (index = 0; index < REVERB_CHANNELS; index++)
	{
		float value = reflections + reverb_read(reverb.input, REVERB_INPUT_SIZE,
			dfrom->reflection_taps[index], to->reflection_taps[index], dfade) +
			reverb_comb_gains[index] * reverb_read(reverb.combs[index], reverb_comb_sizes[index],
			dfrom->comb_delays[index], to->comb_delays[index], dfade);

		reverb_write(reverb.combs[index], reverb_comb_sizes[index], value);
		channels[index] = value * reverb_blend(from->reflections_gains[index], to->reflections_gains[index], fade);
	}

	/* the lines' ends, with the input's onset taps in pairs */
	for (index = 0; index < 2 * REVERB_LINES; index++)
	{
		long tap = reverb_onset_taps[index % REVERB_LINES];

		onset[index] = reverb_read(reverb.input, REVERB_INPUT_SIZE, dfrom->late_delay + tap, to->late_delay + tap, dfade);
	}
	for (index = 0; index < REVERB_LINES; index++)
	{
		taps[index] = reverb_tap(reverb.lines[index], reverb_line_lengths[index], reverb_line_lengths[index]) +
			reverb_blend(from->onset_gains[2 * index], to->onset_gains[2 * index], fade) * onset[2 * index] +
			reverb_blend(from->onset_gains[2 * index + 1], to->onset_gains[2 * index + 1], fade) * onset[2 * index + 1];
		mean += 0.5f * taps[index];
	}

	/* the chain's taps, summed in two, round the loops and into the lines */
	for (index = 0; index < REVERB_CHAIN; index++)
	{
		const float *line = index ? reverb.chain[index - 1] : reverb.input;

		chain[index][0] = reverb_read(line, REVERB_INPUT_SIZE, dfrom->chain[index][0], to->chain[index][0], dfade);
		chain[index][1] = reverb_read(line, REVERB_INPUT_SIZE, dfrom->chain[index][1], to->chain[index][1], dfade);
	}
	sums[0] = chain[0][1] + chain[1][0] + chain[2][0] + chain[3][1];
	sums[1] = chain[0][0] + chain[1][1] + chain[2][1] + chain[3][0];
	network_input = reverb_blend(from->network_input, to->network_input, fade) * (sums[0] + sums[1]);
	loop_input = reverb_blend(from->loop_input, to->loop_input, fade);
	for (index = 0; index < 2; index++)
	{
		loops[index] = loop_input * sums[index] +
			reverb_read(reverb.loops[index], REVERB_LOOP_SIZE, dfrom->loop_length, to->loop_length, dfade);
		reverb_write(reverb.loops[index], REVERB_LOOP_SIZE,
			reverb_filter(from->loop_filter, to->loop_filter, fade, &reverb.loop_state[index], loops[index]));
	}
	for (index = 0; index < REVERB_CHAIN - 1; index++)
	{
		reverb_write(reverb.chain[index], REVERB_INPUT_SIZE,
			reverb_filter(from->chain_filter, to->chain_filter, fade, &reverb.chain_state[index], chain[index][1]));
	}

	/* the network: a Householder reflection of the lines' ends, decayed */
	for (index = 0; index < REVERB_LINES; index++)
	{
		reverb_write(reverb.lines[index], reverb_line_lengths[index],
			reverb_filter(from->lines[index], to->lines[index], fade, &reverb.line_state[index],
			taps[index] - mean + network_input));
	}
	reverb_write(reverb.input, REVERB_INPUT_SIZE, input);
	reverb.position++;

	late_gain = reverb_blend(from->late_gain, to->late_gain, fade);
	for (index = 0; index < REVERB_CHANNELS; index++)
		channels[index] += late_gain * (taps[index] + 0.5f * loops[index / 2]);
}

/* adds the reverberation of send to output, its level moving to
target_level */
static void reverb_process(const float *send, float *output, unsigned long frames, float target_level)
{
	float peak = 0.0f;
	unsigned long frame;

	reverb.silent = FALSE;
	for (frame = 0; frame < frames; frame++)
	{
		const float *held = reverb.held;
		float level = reverb.level;

		if (!reverb.odd)
		{
			float fade = 1.0f - (float)reverb.fade / REVERB_FADE_FRAMES;

			reverb_frame(reverb_blend(reverb.from.input_gain, reverb.to.input_gain, fade) * send[frame], fade, reverb.held);
		}
		reverb.odd = !reverb.odd;
		if (output_channels == 6)
		{
			output[frame * 6] += held[0] * level;
			output[frame * 6 + 1] += held[1] * level;
			output[frame * 6 + 4] += held[2] * level;
			output[frame * 6 + 5] += held[3] * level;
		}
		else
		{
			output[frame * 2] += (held[0] + 0.70710678f * held[2]) * level;
			output[frame * 2 + 1] += (held[1] + 0.70710678f * held[3]) * level;
		}
		peak = fabsf(held[0]) > peak ? fabsf(held[0]) : peak;
		peak = fabsf(held[1]) > peak ? fabsf(held[1]) : peak;
		peak = fabsf(held[2]) > peak ? fabsf(held[2]) : peak;
		peak = fabsf(held[3]) > peak ? fabsf(held[3]) : peak;
		peak = fabsf(send[frame]) > peak ? fabsf(send[frame]) : peak;
		if (reverb.fade && !--reverb.fade)
			reverb.from = reverb.to;
		reverb_delays_step(1);
		reverb.level = clamp_real(target_level, reverb.level - 1.0f / REVERB_LEVEL_FADE_FRAMES,
			reverb.level + 1.0f / REVERB_LEVEL_FADE_FRAMES);
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

/* ---------- the LFE

An Xbox set to Dolby Digital plays its LFE mix bin from the AC-3's LFE
channel, which carries only the lowest frequencies: recorded (its AC-3
decoded), a 1 kHz tone sent to the LFE alone came out at nothing over the
capture's last bit, more than 84 dB down, where it played here whole. So on
5.1 the LFE channel goes through a low pass, the Xbox's own: a sweep sent to
the LFE alone, recorded, fell 3 dB by 86 Hz and then as a 4th order filter
does (a fit of 4.7), not as the Dolby Digital LFE's 120 Hz 8th order would.
So a 4th order Butterworth at 86 Hz (24 dB an octave, 85 dB down at 1 kHz,
as the tone was), as two biquads (RBJ's, in double
precision: their poles are close to 1). The
game itself sends nothing to the LFE bin; a sound given it (a mod's) is
heard there as an Xbox played it. */

#define LFE_CUTOFF 86.0
#define LFE_SECTIONS 2

static struct
{
	/* each biquad's b0 (and b2), b1, a1 and a2 (a0 1), and its state
	(transposed direct form II) */
	double coefficients[LFE_SECTIONS][4];
	double state[LFE_SECTIONS][2];
	BOOL ready;
} lfe;

static void lfe_initialize(void)
{
	int section;

	for (section = 0; section < LFE_SECTIONS; section++)
	{
		/* (the Butterworth's Q of each pair of poles) */
		double q = 1.0 / (2.0 * cos((2 * section + 1) * 3.14159265358979 / (4 * LFE_SECTIONS)));
		double w = 2.0 * 3.14159265358979 * LFE_CUTOFF / OUTPUT_RATE;
		double alpha = sin(w) / (2.0 * q), cosine = cos(w), a0 = 1.0 + alpha;

		lfe.coefficients[section][0] = 0.5 * (1.0 - cosine) / a0;
		lfe.coefficients[section][1] = (1.0 - cosine) / a0;
		lfe.coefficients[section][2] = -2.0 * cosine / a0;
		lfe.coefficients[section][3] = (1.0 - alpha) / a0;
	}
	memset(lfe.state, 0, sizeof(lfe.state));
	lfe.ready = TRUE;
}

/* the LFE channel of frames of output (frames of 6) low passed; on stereo
nothing, the filters emptied */
static void lfe_lowpass(float *output, unsigned long frames)
{
	unsigned long frame;
	int section;

	if (!lfe.ready)
		lfe_initialize();
	if (output_channels != 6)
	{
		memset(lfe.state, 0, sizeof(lfe.state));
		return;
	}
	/* (nothing sent and nothing ringing, as nearly always: nothing to do) */
	for (section = 0; section < LFE_SECTIONS; section++)
	{
		if (lfe.state[section][0] || lfe.state[section][1])
			break;
	}
	if (section == LFE_SECTIONS)
	{
		for (frame = 0; frame < frames && !output[frame * 6 + 3]; frame++)
			;
		if (frame == frames)
			return;
	}
	for (frame = 0; frame < frames; frame++)
	{
		double x = output[frame * 6 + 3];

		for (section = 0; section < LFE_SECTIONS; section++)
		{
			const double *c = lfe.coefficients[section];
			double *state = lfe.state[section];
			double y = c[0] * x + state[0];

			state[0] = c[1] * x - c[2] * y + state[1];
			state[1] = c[0] * x - c[3] * y;
			x = y;
		}
		output[frame * 6 + 3] = flush_denormal((float)x);
	}
	/* (fed silence, the state decays into the denormals within a second or
	two: flush_denormal's) */
	for (section = 0; section < LFE_SECTIONS; section++)
	{
		if (fabs(lfe.state[section][0]) < 1.0e-20 && fabs(lfe.state[section][1]) < 1.0e-20)
			lfe.state[section][0] = lfe.state[section][1] = 0.0;
	}
}

/* ---------- subsonic filter

The output loses what is below hearing before the limiter: a fourth-order
Butterworth high pass at 20 Hz, two second-order sections (-3 dB at 20 Hz,
-0.17 at 30, -0.02 at 40; -10 dB at 15 Hz, -24 at 10, -48 at 5). The maps'
sounds carry some: the title music's second section is mostly 0-20 Hz under
its notes, louder than all it has above 30 Hz (issue 879), and a30's
opening a steady offset; nobody hears it, but it pumps speakers, makes the
limiter turn the mix down for nothing and sets off headphones' loudness
warnings. Fourth order, not second: that music's energy is at 10-20 Hz, by
the corner, where a second order took 4.5 dB off and already 0.8 dB at
30 Hz. The state is double: with poles this near 1, a float's rounding is
itself a low rumble. Coefficients for OUTPUT_RATE (48 kHz), the bilinear
transform's, Q 0.5412 and 1.3066. */

static const double subsonic_sections[2][5] =
{
	/* b0, b1, b2, a1, a2 */
	{ 0.9975854184988755, -1.995170836997751, 0.9975854184988755, -1.9951674183225134, 0.9951742556729882 },
	{ 0.9989974292265931, -1.9979948584531861, 0.9989974292265931, -1.9979914349390586, 0.9979982819673134 },
};

typedef char subsonic_rate[OUTPUT_RATE == 48000 ? 1 : -1];

/* each channel's sections' two states (transposed direct form II) */
static double subsonic_state[MAXIMUM_OUTPUT_CHANNELS][2][2];

static void subsonic(float *output, unsigned long frames)
{
	unsigned long frame, channel, section;

	for (channel = 0; channel < output_channels; channel++)
	{
		for (section = 0; section < 2; section++)
		{
			const double *k = subsonic_sections[section];
			double s1 = subsonic_state[channel][section][0], s2 = subsonic_state[channel][section][1];

			for (frame = 0; frame < frames; frame++)
			{
				double in = output[frame * output_channels + channel];
				double out = k[0] * in + s1;

				s1 = k[1] * in - k[3] * out + s2;
				s2 = k[2] * in - k[4] * out;
				output[frame * output_channels + channel] = (float)out;
			}
			/* (denormals flushed: silence decays the state toward them) */
			subsonic_state[channel][section][0] = fabs(s1) < 1e-30 ? 0.0 : s1;
			subsonic_state[channel][section][1] = fabs(s2) < 1e-30 ? 0.0 : s2;
		}
	}
}

/* ---------- limiter

The game sets its mix bins' headroom to 0 (sound_dsound_xbox.c), so the voices
sum at their full level, as on the Xbox, and the Xbox plays that sum at half
(OUTPUT_GAIN, -6.02 dB), after its mix bins and before both of its outputs:
recorded on an Xbox, every sound comes out at exactly half of what the same
calls mix to here, 2D and 3D, its reverb, on stereo and in AC-3 alike, and a
loud sum passes full scale in the mix bins and still comes out whole (an
Xbox recording's peak at 0.70 of full scale is a sum of 1.40). So the mix is
halved there, and only a sum of more than twice full scale goes over. Clipping
each sample, or bending it near full scale, distorts the sound: dialogue over
gunfire crackled. Instead the whole mix is turned down for as long as it
would go over, both channels alike. The output is delayed LIMITER_LOOKAHEAD -
1 frames (1.3 ms), so that the gain comes down smoothly before each peak: the
smallest gain the frames ahead need, averaged over the last LIMITER_LOOKAHEAD
frames, is never more than a peak needs when it plays. The gain comes back up
over LIMITER_RELEASE_SECONDS. The ceiling is full scale: a ceiling under it
(it was -1 dBFS) turned loud passages down where the Xbox played them as they
are. Only a sum over full scale is turned down. */

#define OUTPUT_GAIN 0.5f
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

/* the sound turned off or on (audio.enabled, Settings > Audio's SOUND) as
it plays: the output's gain faded to 0 or 1 over SOUND_FADE_FRAMES, with
no click, and held there; the gain it reached */
#define SOUND_FADE_FRAMES 480
static float sound_fade(float *output, unsigned long frames, unsigned long channels, float gain, BOOL on)
{
	float target = on ? 1.0f : 0.0f;
	unsigned long frame, channel;

	if (gain == target)
	{
		if (!on)
			memset(output, 0, frames * channels * sizeof(float));
		return gain;
	}
	for (frame = 0; frame < frames; frame++)
	{
		if (gain < target)
			gain = gain + 1.0f / SOUND_FADE_FRAMES < target ? gain + 1.0f / SOUND_FADE_FRAMES : target;
		else if (gain > target)
			gain = gain - 1.0f / SOUND_FADE_FRAMES > target ? gain - 1.0f / SOUND_FADE_FRAMES : target;
		for (channel = 0; channel < channels; channel++)
			output[frame * channels + channel] *= gain;
	}
	return gain;
}

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

/* ---------- debug.dsound_log

Every DirectSound call the game makes, a line each in a text file with the
time it was made (microseconds of the mix's own clock, the frames mixed by
then: without a device the mix keeps to the game's frames, which a hidden
game may run slower than the wall clock, and a packet sent by the wall
clock's time would come late), and the sound data of the
packets it submits appended to the same name with .data added (each packet
a line "P <time> <stream> <offset> <bytes>" of that file): what the audio
test bed replays on an Xbox and through this mixer (its twin), to compare
the two playing the same calls. The lines (stream numbers from 1, as they
are created; mB, Hz, DirectSound's coordinates):
  C t n <format tag> <channels> <rate> <3D>   a stream made
  P t n <offset> <bytes>    a packet          F t n   flushed (stopped)
  V t n <mB>  Q t n <Hz>  M t n <3D mode>  Z t n <pause>
  X t n x y z (position)  W t n x y z (velocity)  N t n <min>  K t n <max>
  S t n <direct> <direct HF> <room> <room HF> <room rolloff> <obstruction>
        <its LF ratio> <occlusion> <its LF ratio>   its I3DL2 source
  B t n <mix bins>  U t n <mix bins> <mB each>
  LP t x y z  LV t x y z  LO t <front x y z> <top x y z>  LD t <distance factor>
  LR t <rolloff factor>  LE t <the 12 I3DL2 listener values>
(CommitDeferredSettings is not logged: the game makes it after each
channel's changes, thousands of times a second, and this mixer takes
every setting at once.)
Buffered (a megabyte at a time); nothing at all is done while it is off. */

static FILE *dsound_log_text, *dsound_log_data;
/* the frames mixed so far: the log's clock (mix) */
static unsigned long dsound_log_frames;
static BOOL dsound_log_checked;
static unsigned long dsound_log_streams;
static unsigned long long dsound_log_data_size;
static pthread_mutex_t dsound_log_lock = PTHREAD_MUTEX_INITIALIZER;

static BOOL dsound_log_on(void)
{
	if (!dsound_log_checked)
	{
		const char *path = config_string("debug.dsound_log");

		dsound_log_checked = TRUE;
		if (path && path[0])
		{
			char data_path[1024];

			snprintf(data_path, sizeof(data_path), "%s.data", path);
			dsound_log_text = fopen(path, "w");
			dsound_log_data = dsound_log_text ? fopen(data_path, "wb") : NULL;
			if (!dsound_log_data)
			{
				if (dsound_log_text)
					fclose(dsound_log_text);
				dsound_log_text = NULL;
				platform_log("sound: cannot write the DirectSound log %s", path);
			}
			else
			{
				setvbuf(dsound_log_text, NULL, _IOFBF, 1 << 20);
				setvbuf(dsound_log_data, NULL, _IOFBF, 1 << 20);
				platform_log("sound: logging the DirectSound calls to %s", path);
			}
		}
	}
	return dsound_log_text != NULL;
}

/* the log's time: microseconds of mixed frames */
static unsigned long long dsound_log_time(void)
{
	return (unsigned long long)__atomic_load_n(&dsound_log_frames, __ATOMIC_RELAXED) * 1000000ull / OUTPUT_RATE;
}

/* a line of the log: its code, the time, then the rest as format says */
static void dsound_log(const char *code, const char *format, ...)
{
	va_list arguments;

	if (!dsound_log_on())
		return;
	pthread_mutex_lock(&dsound_log_lock);
	fprintf(dsound_log_text, "%s %llu ", code, dsound_log_time());
	va_start(arguments, format);
	vfprintf(dsound_log_text, format, arguments);
	va_end(arguments);
	fputc('\n', dsound_log_text);
	pthread_mutex_unlock(&dsound_log_lock);
}

static void dsound_log_packet(unsigned long number, const void *data, unsigned long size)
{
	if (!dsound_log_on())
		return;
	pthread_mutex_lock(&dsound_log_lock);
	fprintf(dsound_log_text, "P %llu %lu %llu %lu\n", dsound_log_time(), number, dsound_log_data_size, size);
	fwrite(data, 1, size, dsound_log_data);
	dsound_log_data_size += size;
	pthread_mutex_unlock(&dsound_log_lock);
}

/* (the log written out, from DirectSoundDoWork once a second: a run stopped leaves it whole but its last second) */
static void dsound_log_flush(void)
{
	pthread_mutex_lock(&dsound_log_lock);
	if (dsound_log_text)
	{
		fflush(dsound_log_text);
		fflush(dsound_log_data);
	}
	pthread_mutex_unlock(&dsound_log_lock);
}

#define DSLOG_ON (dsound_log_checked ? dsound_log_text != NULL : dsound_log_on())
#define DSLOG_STREAM(code, format, ...) \
	do { if (DSLOG_ON) dsound_log(code, "%lu " format, stream_from_interface(stream)->log_number, __VA_ARGS__); } while (0)

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

/* (the mixer's thread, voices_lock) the game cuts a voice off (Flush) to
play the next sound on its channel: each shot of a weapon fired faster than
its sound lasts takes the last's channel (sound classes allow one
weapon_fire sound an object). Stopped dead, a waveform drops from wherever
it was to nothing, a click, and a rifle's shots crackled. So at the hand-off
that takes the flush, what the voice would play next is mixed first,
CUT_FADE_FRAMES of it at its gains, faded out along half a cosine, and
played over the next mixes beside the new sound (mix_cut); what is left of a
cut still playing is kept under it. Its packets' samples are still there:
the game's side retires a flushed packet's, freed only once a hand-off has
been taken since (retire). The HRTF's frames and bands, which hold what the
voice played, are cleared after it, so that the new sound does not take
them up unfaded: the cut has them. */
static void voice_cut(struct sdl_stream *stream)
{
	static float output[CUT_FADE_FRAMES * MAXIMUM_OUTPUT_CHANNELS], send[CUT_FADE_FRAMES];
	unsigned long frame, channel, left = CUT_FADE_FRAMES - stream->cut_played;

	if (stream->paused || !stream->mixing_count || !stream->gains_valid || !stream->sample_rate)
		return;
	memset(output, 0, sizeof(output));
	memset(send, 0, sizeof(send));
	mix_voice(stream, output, send, CUT_FADE_FRAMES);
	for (frame = 0; frame < CUT_FADE_FRAMES; frame++)
	{
		float fade = 0.5f + 0.5f * (float)cos(3.14159265358979 * (double)(frame + 1) / CUT_FADE_FRAMES);

		/* (what is left of the earlier cut is read only where it is: past
		it is whatever it was, a NaN perhaps, which no factor of 0 hides) */
		for (channel = 0; channel < output_channels; channel++)
		{
			stream->cut[frame * output_channels + channel] = output[frame * output_channels + channel] * fade +
				(frame < left ? stream->cut[(stream->cut_played + frame) * output_channels + channel] : 0.0f);
		}
		stream->cut_send[frame] = send[frame] * fade +
			(frame < left ? stream->cut_send[stream->cut_played + frame] : 0.0f);
	}
	stream->cut_played = 0;
	memset(stream->hrtf_frames, 0, sizeof(stream->hrtf_frames));
	memset(stream->hrtf_bands, 0, sizeof(stream->hrtf_bands));
}

/* (the mixer's thread) a cut voice's fade out (voice_cut), into the mix */
static void mix_cut(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	unsigned long frame, channel;

	for (frame = 0; frame < frames && stream->cut_played < CUT_FADE_FRAMES; frame++, stream->cut_played++)
	{
		for (channel = 0; channel < output_channels; channel++)
			output[frame * output_channels + channel] += stream->cut[stream->cut_played * output_channels + channel];
		send[frame] += stream->cut_send[stream->cut_played];
	}
}

/* (the mixer's thread, voices_lock) the game's side of a voice taken over:
a flush, the packets it has completed let go of, those it has submitted
since, and its settings */
static void voice_hand_off(struct sdl_stream *stream)
{
	unsigned long first = stream->submitted - stream->packet_count;
	unsigned long sequence;

	/* flushed: what it was playing faded out (voice_cut), the packets it
	held let go of, and the voice starts over */
	if (stream->flush)
	{
		voice_cut(stream);
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
		float offset[3], right_axis[3], room, direct_lowpass, room_lowpass, pair[2];
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
		voice_gains(stream, &report->left_gain, &report->right_gain, pair, &room, &direct_lowpass, &room_lowpass,
			&report->hrtf_on, &report->parameters);
		/* (through the HRTF, each ear's gain is its own, the pan's not
		heard) */
		if (report->hrtf_on && hrtf_enabled)
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

/* ---------- buffers (the mixer's side)

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
	DWORD format, channels;
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

/* a mix of frames into output: the game's side taken over first, waiting
for it only without a device (wait) */
static void mix(float *output, unsigned long frames, BOOL wait)
{
	struct sdl_stream *stream;
	BOOL enabled = reverb_enabled;
	Uint64 started = SDL_GetTicksNS(), part_at = 0, hrtf_ns = 0;

	if (dsound_log_text)
		__atomic_fetch_add(&dsound_log_frames, frames, __ATOMIC_RELAXED);

	if (mix_profile_on)
	{
		mix_profile.frames += frames;
		part_at = started;
	}
	memset(output, 0, frames * output_channels * sizeof(float));
	memset(reverb.send, 0, frames * sizeof(float));
	voices_hand_off(wait);
	if (mix_profile_on)
	{
		mix_part_end(_mix_part_hand_off, &part_at);
		hrtf_ns = mix_profile.parts_ns[_mix_part_hrtf];
	}
	for (stream = mixing_streams; stream; stream = stream->mixing_link)
	{
		mix_voice(stream, output, reverb.send, frames);
		mix_cut(stream, output, reverb.send, frames);
		mix_profile.voices += mix_profile_on;
	}
	if (mix_profile_on)
	{
		/* (the HRTF's time is counted in the voices' too) */
		mix_part_end(_mix_part_voices, &part_at);
		mix_profile.parts_ns[_mix_part_voices] -= mix_profile.parts_ns[_mix_part_hrtf] - hrtf_ns;
	}
	voices_publish(wait);
	if (mix_profile_on)
		mix_part_end(_mix_part_publish, &part_at);
	/* (the game's buffers: the loading screen's chant) */
	mix_buffers(output, frames);
	if (mix_profile_on)
		mix_part_end(_mix_part_buffers, &part_at);
	/* a new environment, once the fade to the last is over */
	if (reverb.serial != environment_serial && !reverb.fade)
		reverb_update(&environment, environment_serial);
	/* the reverb runs while something reverberates, and stops once it is
	turned off and faded out, or quiet */
	/* (a hold goes on while the reverb is silent) */
	if (reverb.silent && !reverb_sent(reverb.send, frames))
		reverb_delays_step(frames);
	if (!reverb.silent || (enabled && reverb_sent(reverb.send, frames)))
	{
		reverb_process(reverb.send, output, frames, enabled ? 1.0f : 0.0f);
		if ((!enabled && reverb.level <= 0.0f) || reverb.quiet > REVERB_QUIET_FRAMES)
			reverb_clear();
	}
	if (mix_profile_on)
		mix_part_end(_mix_part_reverb, &part_at);
	/* (on 5.1, the LFE's band) */
	lfe_lowpass(output, frames);
	/* the game's mix at the Xbox's output level (OUTPUT_GAIN) */
	{
		unsigned long sample, samples = frames * output_channels;

		for (sample = 0; sample < samples; sample++)
			output[sample] *= OUTPUT_GAIN;
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
	if (mix_profile_on)
		mix_part_end(_mix_part_chat, &part_at);
	subsonic(output, frames);
	limit(output, frames);
	sound_gain = sound_fade(output, frames, output_channels, sound_gain, sound_on);
	record(output, frames);
	if (mix_profile_on)
		mix_part_end(_mix_part_rest, &part_at);
	mix_nanoseconds += SDL_GetTicksNS() - started;
	mix_frames += frames;
}

/* ---------- the output's channels

audio.channels (setting: "stereo", "5.1" or "auto"; anything else is
stereo) and the channels of the device the sound plays on (device_channels;
0 when it cannot say): 6 for 5.1, 2 for stereo. "auto" is 5.1 on a device
of six channels or more (a 5.1 or 7.1 receiver, a surround headset: SDL
lays the 5.1 out on a 7.1's speakers), stereo on fewer (speakers, a pair
of headphones) and when it cannot say. On 5.1 the world's sounds play from
the front and back pairs, as the Xbox's (surround_split). */
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
	Uint64 began = mix_profile_on ? SDL_GetTicksNS() : 0;

	(void)userdata;
	(void)total_amount;
	/* (numbered as host_sdl.c numbers the calls: HALO_AUDIO_MIX_TIMES) */
	mix_requests++;
	if (additional_amount <= 0)
		return;
	if (mix_profile_on)
		mix_profile_begin();
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
		if (mix_muted)
			memset(buffer, 0, frames * output_channels * sizeof(float));
		if (mix_profile_on)
		{
			Uint64 put = SDL_GetTicksNS();

			SDL_PutAudioStreamData(stream, buffer, (int)(frames * output_channels * sizeof(float)));
			mix_profile.parts_ns[_mix_part_put] += SDL_GetTicksNS() - put;
		}
		else
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
	if (mix_profile_on)
		mix_profile_end(began);
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

/* the silent clocks running, and whether they are asked to stop (a device
opened in their place: silent_clock_stop) */
static pthread_mutex_t silent_clock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t silent_clock_condition = PTHREAD_COND_INITIALIZER;
static int silent_clocks;
static BOOL silent_clock_stopping;

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
		pthread_mutex_lock(&silent_clock_lock);
		if (silent_clock_stopping)
		{
			silent_clocks--;
			pthread_cond_broadcast(&silent_clock_condition);
			pthread_mutex_unlock(&silent_clock_lock);
			return NULL;
		}
		pthread_mutex_unlock(&silent_clock_lock);
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

/* a silent clock started: the voices drained in real time, without a device */
static void silent_clock_start(void)
{
	pthread_t thread;

	pthread_mutex_lock(&silent_clock_lock);
	silent_clocks++;
	pthread_mutex_unlock(&silent_clock_lock);
	if (pthread_create(&thread, NULL, silent_clock_thread, NULL))
	{
		pthread_mutex_lock(&silent_clock_lock);
		silent_clocks--;
		pthread_mutex_unlock(&silent_clock_lock);
		return;
	}
	pthread_detach(thread);
}

/* the silent clocks stopped, waited for, so that a device's mix is the only
one (a paced mix's never stops: it has no device to open) */
static void silent_clock_stop(void)
{
	pthread_mutex_lock(&silent_clock_lock);
	silent_clock_stopping = TRUE;
	while (silent_clocks > 0)
		pthread_cond_wait(&silent_clock_condition, &silent_clock_lock);
	silent_clock_stopping = FALSE;
	pthread_mutex_unlock(&silent_clock_lock);
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
			platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
			silent_clock_start();
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
	sound_on = config_boolean("audio.enabled");
	sound_gain = sound_on ? 1.0f : 0.0f;
	mix_profile_initialize();
	resampler_mode = resampler_mode_read();
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();
	hrtf_initialize();
	record_open();
	paced = frames_clock(NULL);
	if (paced)
		platform_log("sound: mixed in step with the frames (debug.frame_time, debug.fixed_step), no device");

	if (!paced && sound_on && platform_sdl_initialize())
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
	silent_clock_start();
}

/* whether to open a device now: the sound turned on (audio.enabled) with
none open (the game started with it off, or none could open), once for each
change of the settings, never for a mix paced by the frames */
static BOOL sound_device_wanted(BOOL started, BOOL paced_mix, BOOL on, BOOL open, unsigned long read_at,
	unsigned long changes)
{
	return started && !paced_mix && on && !open && read_at != changes;
}

/* the sound turned on with no device: one opened, its mix in the silent
clock's place (which stops first, so that only one mixes) */
static void sound_device_open(void)
{
	audio_device_read_at = config_changes();
	if (!platform_sdl_initialize())
		return;
	silent_clock_stop();
	snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
	buffer_frames_choose();
	audio_stream = audio_open();
	if (audio_stream)
	{
		platform_log("audio: the sound is on: playing on %s (%s)", audio_device_name,
			output_channels == 6 ? "5.1 surround" : "stereo");
		return;
	}
	platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
	silent_clock_start();
}

/* (the event thread, each frame: sdl_platform.c) audio.output_device
changed (Settings > Audio): the sound goes on on the new device, else the
system's default; audio.enabled turned on with no device open: one opened
(sound_device_open) */
void dsound_sdl_output_device_check(void)
{
	if (sound_device_wanted(audio_started, paced, sound_on, audio_stream != NULL, audio_device_read_at,
		config_changes()))
	{
		sound_device_open();
	}
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
		/* (none at all: the voices drained in real time, as at the start) */
		platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
		silent_clock_start();
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
	if (DSLOG_ON && input->pvBuffer)
		dsound_log_packet(stream->log_number, input->pvBuffer, input->dwMaxSize);
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

	if (DSLOG_ON)
		dsound_log("F", "%lu", stream->log_number);

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
			snprintf(heard, sizeof(heard), "; HRTF: the right ear %+.0f us after the left; left ear 250 Hz %+.1f "
				"dB, 2 kHz %+.1f, 8 kHz %+.1f; right ear %+.1f, %+.1f, %+.1f",
				(parameters->delay[1] - parameters->delay[0]) * 1.0e6f / OUTPUT_RATE, parameters->decibels[0][0],
				parameters->decibels[0][2], parameters->decibels[0][6], parameters->decibels[1][0],
				parameters->decibels[1][2], parameters->decibels[1][6]);
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
	static unsigned long long log_flushed_ns;

	/* (debug.dsound_log written out about once a second) */
	if (dsound_log_text && SDL_GetTicksNS() - log_flushed_ns > 1000000000ull)
	{
		log_flushed_ns = SDL_GetTicksNS();
		dsound_log_flush();
	}
	/* (audio.volume, audio.reverb, audio.doppler, audio.enabled and
	audio.resampler read again when the settings change: on the game's thread, not the mixer's,
	whose lock the config's file I/O would hold) */
	if (volume_read_at != config_changes())
	{
		volume_read_at = config_changes();
		master_volume = (float)config_real("audio.volume");
		reverb_enabled = config_boolean("audio.reverb");
		doppler_enabled = config_boolean("audio.doppler");
		sound_on = config_boolean("audio.enabled");
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
	/* the HRTF, or the pan (audio.spatial), each frame: the mixer
	crossfades each voice from one to the other as it changes; what the
	host says the sound plays on is logged */
	headphones = host_audio_headphones();
	/* (on 5.1 too, to the front and back pairs, as the Xbox's AC-3:
	surround_split) */
	hrtf_enabled = spatial_hrtf(spatial_setting, vr_running, vr_hrtf);
	if (headphones != logged_headphones || hrtf_enabled != logged_hrtf)
	{
		logged_headphones = headphones;
		logged_hrtf = hrtf_enabled;
		platform_log("audio: the sound plays on %s; the world's sounds %s (audio.spatial \"%s\"%s)",
			headphones > 0 ? "headphones" : headphones == 0 ? "speakers" : "a device not known (speakers)",
			hrtf_enabled ? (output_channels == 6 ? "through the HRTF, on the 5.1's front and back pairs" :
			"through the HRTF") : "panned",
			spatial_setting, vr_running ? ", the VR mode: vr.hrtf" : "");
	}
	if (log_voices)
		voices_log();
	if (mix_profile_on)
		mix_profiles_log();
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
	if (DSLOG_ON)
		dsound_log("LE", "%ld %ld %.4f %.4f %.4f %ld %.4f %ld %.4f %.4f %.4f %.4f", (long)listener_properties->lRoom,
			(long)listener_properties->lRoomHF, listener_properties->flRoomRolloffFactor, listener_properties->flDecayTime,
			listener_properties->flDecayHFRatio, (long)listener_properties->lReflections,
			listener_properties->flReflectionsDelay, (long)listener_properties->lReverb, listener_properties->flReverbDelay,
			listener_properties->flDiffusion, listener_properties->flDensity, listener_properties->flHFReference);
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
	if (DSLOG_ON)
		dsound_log("LD", "%.4f", factor);
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
	if (DSLOG_ON)
		dsound_log("LR", "%.4f", factor);
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
	if (DSLOG_ON)
		dsound_log("LP", "%.4f %.4f %.4f", x, y, z);
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
	if (DSLOG_ON)
		dsound_log("LV", "%.4f %.4f %.4f", x, y, z);
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
	if (DSLOG_ON)
		dsound_log("LO", "%.4f %.4f %.4f %.4f %.4f %.4f", x_front, y_front, z_front, x_top, y_top, z_top);
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
	if (DSLOG_ON)
	{
		pthread_mutex_lock(&dsound_log_lock);
		stream->log_number = ++dsound_log_streams;
		pthread_mutex_unlock(&dsound_log_lock);
		dsound_log("C", "%lu %u %u %lu %d", stream->log_number, format ? (unsigned)format->wFormatTag : 0u,
			(unsigned)stream->channels, (unsigned long)stream->sample_rate, stream->has_3d ? 1 : 0);
	}
	stream->pending.mode = DS3DMODE_NORMAL;
	stream->pending.minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->pending.maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	/* (the mixer's side the same, before the mixer can see it) */
	voice_settings_take(stream);
	resampler_reset(stream);
	/* (no cut playing: voice_cut) */
	stream->cut_played = CUT_FADE_FRAMES;
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
	DSLOG_STREAM("Q", "%lu", (unsigned long)frequency);
	STREAM_SETTER(record->frequency = frequency ? frequency : stream_from_interface(stream)->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	DSLOG_STREAM("V", "%ld", (long)volume);
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
	DSLOG_STREAM("B", "%lu", (unsigned long)mix_bin_mask);
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

	if (DSLOG_ON)
	{
		char text[400];
		size_t used = 0;
		unsigned long count = 0;

		for (bit = 0; bit < 32; bit++)
		{
			if ((mix_bin_mask & (1ul << bit)) && used < sizeof(text) - 16)
				used += (size_t)snprintf(text + used, sizeof(text) - used, " %ld", (long)volumes[count++]);
		}
		text[used] = 0;
		DSLOG_STREAM("U", "%lu%s", (unsigned long)mix_bin_mask, text);
	}
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
	DSLOG_STREAM("M", "%lu", (unsigned long)mode);
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	DSLOG_STREAM("X", "%.4f %.4f %.4f", x, y, z);
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	DSLOG_STREAM("N", "%.4f", distance);
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	DSLOG_STREAM("K", "%.9g", distance);
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct, direct_hf, room, room_hf;

	(void)apply;
	DSLOG_STREAM("S", "%ld %ld %ld %ld %.4f %ld %.4f %ld %.4f", (long)source->lDirect, (long)source->lDirectHF,
		(long)source->lRoom, (long)source->lRoomHF, source->flRoomRolloffFactor, (long)source->Obstruction.lHFLevel,
		source->Obstruction.flLFRatio, (long)source->Occlusion.lHFLevel, source->Occlusion.flLFRatio);
	/* the direct path: at low frequencies obstruction and occlusion take
	their LF ratio of their high frequency levels, which (with the source's
	own) muffle it above the HF reference */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	direct_hf = source->lDirect + source->lDirectHF + source->Obstruction.lHFLevel + source->Occlusion.lHFLevel;
	/* the room send: occlusion lowers and muffles it as it does the direct
	path; obstruction does not lower it, but its muffling (its high
	frequency level over its LF ratio's part) reaches it too. The Xbox
	muffles a voice once, before its mix bins, the reverb's among them: an
	Xbox recording of a voice obstructed by -1200 (LF ratio 0) in a hall is
	the recording of it occluded by -1200, sample for sample (the
	difference 38 dB down, the noise's), the reverb's tail as muffled as
	the direct sound */
	room = source->lRoom + (LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	/* the source's own Room HF does nothing: recorded on an Xbox (the test
	bed's ninth run), the cave's reverb of a voice at a Room HF of -1204
	was as loud at 10 kHz as at 250 Hz, and one at a Room and Room HF of
	-1204 (the game's for most of its 3D sounds) or -2000 was 12 or 20 dB
	down at every frequency, to 0.1 dB; the listener's Room HF (each
	environment's) and occlusion's muffling reach the send as before. Taken
	as the I3DL2 level it is, it muffled the reverberation of nearly every
	3D sound by 12 dB more at the HF reference */
	room_hf = source->lRoom + source->Occlusion.lHFLevel +
		(source->Obstruction.lHFLevel - (LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio));
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
	DSLOG_STREAM("Z", "%lu", (unsigned long)pause);
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	DSLOG_STREAM("W", "%.4f %.4f %.4f", x, y, z);
	STREAM_SETTER(record->velocity[0] = x; record->velocity[1] = y; record->velocity[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

/* ---------- buffers (the game's side; the mixer's is mix_buffers) */

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
	record->channels = channels;
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
	/* (from silence, rising to its gain over a twentieth of a second; but a
	sound played from its start that begins in silence itself, at its gain:
	there is no click to soften, and a short one, the hit sound, keeps its
	attack) */
	if (!record->playing)
	{
		record->current = record->samples && record->position == 0.0 && !record->samples[0] &&
			(record->channels != 2 || !record->samples[1]) ? record->gain * master_volume : 0.0f;
	}
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
