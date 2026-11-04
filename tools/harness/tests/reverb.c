/*
REVERB.C (test)

The real reverb of port/linux/src/dsound_sdl.c (test_reverb.py takes the
mixer as under_test.inc, as test_mixer.py does) measured against the I3DL2
listener properties it is given, for each sound environment of the maps
(their tags' values, turned into I3DL2's as sound_dsound_xbox.c
dsound_set_listener_properties turns them):
	- "decay": the late reverberation's decay time (RT60, from Schroeder's
	  backward integration of its impulse response) is the decay time at
	  low frequencies, and the decay time times the HF ratio at the HF
	  reference, the ratio over 1 too (the cave's 1.3);
	- "levels": the energy of the early reflections and of the late
	  reverberation, over the output channels, is the Reflections and the
	  Reverb level (relative to the Room level the send carries), as the
	  direct path of a 3D voice has an energy of 1 over them; the send's
	  low pass is the Room HF level at the HF reference;
	- "timing": the first reflection comes the reflections delay after the
	  sound, the late reverberation the reverb delay after it, and the
	  reflections span that time between (the I3DL2 model's three parts:
	  the direct path, the reflections, the late reverberation);
	- "transition": the game moves the environment a step at each tick
	  (scenario.c scenario_get_sound_environment) on the way from one to
	  another, and the reverb follows: each fade goes on from where the last
	  got to, never back towards the environment left, and it reaches the
	  new one soon after the game does.
"metrics" prints the figures of each environment.
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

typedef unsigned long long Uint64;
#define platform_log(...) ((void)0)
#define SDL_GetTicksNS() 0ull

static const char *config_string(const char *name)
{
	(void)name;
	return "";
}

static void voice_audio_mix(float *output, unsigned long frames)
{
	(void)output;
	(void)frames;
}

#include "under_test.inc"

#define PI 3.14159265358979323846

/* ---------- the maps' sound environments

Their tags' values (sound_environment_definitions.h: gains, seconds, 0 to 1
for the diffusion and density, hertz), every one the maps have: each is one
of I3DL2's presets of its name */

struct environment_tag
{
	const char *name;
	float room, room_hf, room_rolloff, decay_time, decay_hf_ratio, reflections, reflections_delay,
		reverb, reverb_delay, diffusion, density, hf_reference;
};

static const struct environment_tag environments[] =
{
	{ "alley", 0.316f, 0.72f, 0, 1.49f, 0.86f, 0.079f, 0.007f, 0.0995f, 0.011f, 1, 1, 5000 },
	{ "arena", 0.316f, 0.447f, 0, 7.24f, 0.33f, 0.0825f, 0.02f, 0.102f, 0.03f, 1, 1, 5000 },
	{ "cave", 0.316f, 1.0f, 0, 2.91f, 1.3f, 0.158f, 0.015f, 0.0707f, 0.022f, 1, 1, 5000 },
	{ "concert_hall", 0.316f, 0.5623f, 0, 3.92f, 0.7f, 0.0767f, 0.02f, 0.0997f, 0.029f, 1, 1, 5000 },
	{ "forest", 0.316f, 0.022f, 0, 1.49f, 0.54f, 0.0165f, 0.162f, 0.0492f, 0.088f, 0.79f, 1, 5000 },
	{ "hallway", 0.316f, 0.71f, 0, 1.49f, 0.59f, 0.077f, 0.007f, 0.166f, 0.011f, 1, 1, 5000 },
	{ "hangar", 0.316f, 0.316f, 0, 10.05f, 0.23f, 0.158f, 0.02f, 0.125f, 0.03f, 1, 1, 5000 },
	{ "mountains", 0.316f, 0.0562f, 0, 1.49f, 0.21f, 0.0128f, 0.3f, 0.009f, 0.1f, 0.27f, 1, 5000 },
	{ "quarry", 0.316f, 0.316f, 0, 1.49f, 0.83f, 0.0f, 0.061f, 0.1777f, 0.025f, 1, 1, 5000 },
	{ "stone_room", 0.316f, 0.71f, 0, 2.31f, 0.64f, 0.139f, 0.012f, 0.11f, 0.017f, 1, 1, 5000 },
	{ "underwater", 0.316f, 0.01f, 0, 1.49f, 0.1f, 0.188f, 0.007f, 0.71f, 0.011f, 1, 1, 5000 },
};
#define ENVIRONMENTS (sizeof(environments) / sizeof(environments[0]))

/* sound_dsound_xbox.c dsound_volume_from_gain */
static LONG volume_from_gain(float gain, LONG maximum)
{
	LONG volume;

	if (gain == 0.0f)
		return DSBVOLUME_MIN;
	volume = (LONG)(2000.0 * log10(gain) + maximum);
	return volume < DSBVOLUME_MIN ? DSBVOLUME_MIN : volume > maximum ? maximum : volume;
}

/* sound_dsound_xbox.c dsound_set_listener_properties */
static DSI3DL2LISTENER listener_from_tag(const struct environment_tag *tag)
{
	DSI3DL2LISTENER properties;

	properties.lRoom = volume_from_gain(tag->room, 0);
	properties.lRoomHF = volume_from_gain(tag->room_hf, 0);
	properties.flRoomRolloffFactor = tag->room_rolloff;
	properties.flDecayTime = tag->decay_time;
	properties.flDecayHFRatio = tag->decay_hf_ratio;
	properties.lReflections = volume_from_gain(tag->reflections, 1000);
	properties.flReflectionsDelay = tag->reflections_delay;
	properties.lReverb = volume_from_gain(tag->reverb, 2000);
	properties.flReverbDelay = tag->reverb_delay;
	properties.flDiffusion = tag->diffusion * 100.0f;
	properties.flDensity = tag->density * 100.0f;
	properties.flHFReference = tag->hf_reference;
	return properties;
}

/* ---------- impulse responses */

static unsigned long serial;

/* the reverb's stereo response to a unit impulse sent to it, frames long,
in the environment of properties (a new environment, the reverb silent) */
static float *response(const DSI3DL2LISTENER *properties, unsigned long frames)
{
	float *output = calloc(frames * 2, sizeof(float));
	static float send[MIX_CHUNK_FRAMES];
	unsigned long frame;

	reverb_clear();
	reverb.level = 1.0f;
	reverb_update(properties, ++serial);
	for (frame = 0; frame < frames; frame += MIX_CHUNK_FRAMES)
	{
		unsigned long count = frames - frame < MIX_CHUNK_FRAMES ? frames - frame : MIX_CHUNK_FRAMES;

		memset(send, 0, sizeof(send));
		if (!frame)
			send[0] = 1.0f;
		reverb_process(send, output + frame * 2, count, 1.0f);
	}
	return output;
}

/* the response with only the reflections (late), or only the late
reverberation (!late) */
static float *part_response(DSI3DL2LISTENER properties, BOOL late, unsigned long frames)
{
	if (late)
		properties.lReflections = DSBVOLUME_MIN;
	else
		properties.lReverb = DSBVOLUME_MIN;
	return response(&properties, frames);
}

/* both channels through a band pass about hertz octaves wide (RBJ's, 0 dB
at its center, twice over: the low frequencies of a dull room are 40 dB over
its high ones, and once lets enough of their longer decay through to be
measured as the band's), summed in energy: the band's energy at each frame */
static double *band_energy(const float *output, unsigned long frames, double hertz, double octaves)
{
	double w = 2.0 * PI * hertz / OUTPUT_RATE;
	double alpha = sin(w) * sinh(log(2.0) / 2.0 * octaves * w / sin(w));
	double b0 = alpha / (1.0 + alpha), b2 = -alpha / (1.0 + alpha);
	double a1 = -2.0 * cos(w) / (1.0 + alpha), a2 = (1.0 - alpha) / (1.0 + alpha);
	double *energy = calloc(frames, sizeof(double));
	int channel;

	for (channel = 0; channel < 2; channel++)
	{
		double x1[2] = { 0, 0 }, x2[2] = { 0, 0 }, y1[2] = { 0, 0 }, y2[2] = { 0, 0 };
		unsigned long frame;

		for (frame = 0; frame < frames; frame++)
		{
			double x = output[frame * 2 + channel];
			int stage;

			for (stage = 0; stage < 2; stage++)
			{
				double y = b0 * x + b2 * x2[stage] - a1 * y1[stage] - a2 * y2[stage];

				x2[stage] = x1[stage];
				x1[stage] = x;
				y2[stage] = y1[stage];
				y1[stage] = y;
				x = y;
			}
			energy[frame] += x * x;
		}
	}
	return energy;
}

/* the decay time (seconds to fall 60 dB) of the band's energy: Schroeder's
backward integral, a line fitted to it from 5 to 25 dB down */
static double decay_time(const double *energy, unsigned long frames)
{
	double *integral = malloc(frames * sizeof(double));
	double sum = 0.0, sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
	unsigned long frame;

	for (frame = frames; frame-- > 0;)
	{
		sum += energy[frame];
		integral[frame] = sum;
	}
	for (frame = 0; frame < frames; frame++)
	{
		double level = 10.0 * log10(integral[frame] / integral[0] + 1e-300);

		if (level <= -5.0 && level >= -25.0)
		{
			double t = (double)frame / OUTPUT_RATE;

			sx += t;
			sy += level;
			sxx += t * t;
			sxy += t * level;
			n++;
		}
	}
	free(integral);
	if (n < 2)
		return 0.0;
	return -60.0 / ((n * sxy - sx * sy) / (n * sxx - sx * sx));
}

/* the energy over both channels, in decibels */
static double energy_db(const float *output, unsigned long first, unsigned long last)
{
	double sum = 0.0;
	unsigned long frame;

	for (frame = first; frame < last; frame++)
		sum += (double)output[frame * 2] * output[frame * 2] + (double)output[frame * 2 + 1] * output[frame * 2 + 1];
	return 10.0 * log10(sum + 1e-300);
}

/* where the decay time is measured: at low frequencies, a
third of an octave about 125 Hz, which the lines' filters leave alone; at
the HF reference, a twelfth of an octave about it, so narrow that the decay
measured is the decay there, not that of the band's lower edge, where the
lines' low pass loses less */
#define LOW_BAND 125.0
#define LOW_OCTAVES (1.0 / 3.0)
#define HIGH_OCTAVES (1.0 / 12.0)

/* the first and the last frame over threshold (in amplitude) */
static void extent(const float *output, unsigned long frames, float threshold, long *first, long *last)
{
	unsigned long frame;

	*first = *last = -1;
	for (frame = 0; frame < frames; frame++)
	{
		if (fabsf(output[frame * 2]) > threshold || fabsf(output[frame * 2 + 1]) > threshold)
		{
			if (*first < 0)
				*first = (long)frame;
			*last = (long)frame;
		}
	}
}

/* the time the energy over both channels first reaches fraction of its
total, in frames */
static long energy_onset(const float *output, unsigned long frames, double fraction)
{
	double total = 0.0, sum = 0.0;
	unsigned long frame;

	for (frame = 0; frame < frames; frame++)
		total += (double)output[frame * 2] * output[frame * 2] + (double)output[frame * 2 + 1] * output[frame * 2 + 1];
	for (frame = 0; frame < frames; frame++)
	{
		sum += (double)output[frame * 2] * output[frame * 2] + (double)output[frame * 2 + 1] * output[frame * 2 + 1];
		if (sum >= fraction * total)
			return (long)frame;
	}
	return -1;
}

/* the measured figures of one environment */
struct figures
{
	double decay_low, decay_high, reflections_db, reverb_db, room_hf_db;
	double first_reflection, last_reflection, late_onset; /* seconds */
};


static unsigned long response_frames(const DSI3DL2LISTENER *properties)
{
	return (unsigned long)((properties->flDecayTime * 1.5f + 0.5f) * OUTPUT_RATE);
}

static struct figures measure(const DSI3DL2LISTENER *properties)
{
	struct figures figures;
	unsigned long frames = response_frames(properties);
	float *late = part_response(*properties, TRUE, frames);
	float *early = part_response(*properties, FALSE, OUTPUT_RATE);
	double *energy;
	long first, last;

	energy = band_energy(late, frames, LOW_BAND, LOW_OCTAVES);
	figures.decay_low = decay_time(energy, frames);
	free(energy);
	energy = band_energy(late, frames, properties->flHFReference, HIGH_OCTAVES);
	figures.decay_high = decay_time(energy, frames);
	free(energy);
	/* the levels (of a unit impulse sent: the room level is the send's),
	the late reverberation's with its high frequencies decaying as its low
	ones do (a ratio of 1), which the level is of, over all frequencies */
	{
		DSI3DL2LISTENER flat = *properties;
		float *flat_late;

		flat.flDecayHFRatio = 1.0f;
		flat_late = part_response(flat, TRUE, frames);
		figures.reverb_db = energy_db(flat_late, 0, frames);
		free(flat_late);
	}
	figures.reflections_db = properties->lReflections <= DSBVOLUME_MIN ? -100.0 :
		energy_db(early, 0, OUTPUT_RATE);
	extent(early, OUTPUT_RATE, 1.0e-6f, &first, &last);
	figures.first_reflection = (double)first / OUTPUT_RATE;
	figures.last_reflection = (double)last / OUTPUT_RATE;
	/* the late reverberation's onset: when the first percent of its energy
	is in */
	figures.late_onset = (double)energy_onset(late, frames, 0.01) / OUTPUT_RATE;
	/* the send's low pass at the HF reference: the room HF level */
	{
		float a = lowpass_coefficient(gain_from_millibels(properties->lRoomHF), frequency_cosine(properties->flHFReference));
		double w = 2.0 * PI * properties->flHFReference / OUTPUT_RATE;
		double magnitude = (1.0 - a) / sqrt(1.0 - 2.0 * a * cos(w) + (double)a * a);

		figures.room_hf_db = 20.0 * log10(magnitude);
	}
	free(late);
	free(early);
	return figures;
}

/* ---------- transitions */

/* the late gain the reverb plays at, between its fade's ends */
static float late_gain_now(void)
{
	float fade = 1.0f - (float)reverb.fade / REVERB_FADE_FRAMES;

	return reverb_blend(reverb.from.late_gain, reverb.to.late_gain, fade);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	unsigned long index;

	reverb_initialize();
	CASE("metrics")
	{
		printf("%-13s %6s %6s %6s %6s %7s %7s %7s %7s %6s %6s %6s %6s %6s %6s\n", "environment", "RT60", "meas",
			"RT@HF", "meas", "refl dB", "meas", "rev dB", "meas", "1st ms", "meas", "last", "late", "meas", "rmHF");
		for (index = 0; index < ENVIRONMENTS; index++)
		{
			DSI3DL2LISTENER properties = listener_from_tag(&environments[index]);
			struct figures figures = measure(&properties);
			double ratio = properties.flDecayHFRatio;

			printf("%-13s %6.2f %6.2f %6.2f %6.2f %7.1f %7.1f %7.1f %7.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f/%.1f\n",
				environments[index].name, properties.flDecayTime, figures.decay_low,
				properties.flDecayTime * ratio, figures.decay_high,
				properties.lReflections * 0.01, figures.reflections_db, properties.lReverb * 0.01, figures.reverb_db,
				properties.flReflectionsDelay * 1000.0, figures.first_reflection * 1000.0,
				figures.last_reflection * 1000.0,
				(properties.flReflectionsDelay + properties.flReverbDelay) * 1000.0, figures.late_onset * 1000.0,
				properties.lRoomHF * 0.01, figures.room_hf_db);
		}
		return 0;
	}

	/* the response of environment argv[2] (its index), late (argv[3] "late")
	or early, as raw stereo floats */
	CASE("dump")
	{
		DSI3DL2LISTENER properties = listener_from_tag(&environments[atoi(argv[2])]);
		unsigned long frames = response_frames(&properties);
		float *output = part_response(properties, argc > 3 && !strcmp(argv[3], "late"), frames);

		fwrite(output, sizeof(float), frames * 2, stdout);
		free(output);
		return 0;
	}

	CASE("decay")
	{
		for (index = 0; index < ENVIRONMENTS; index++)
		{
			DSI3DL2LISTENER properties = listener_from_tag(&environments[index]);
			struct figures figures = measure(&properties);
			double high = properties.flDecayTime * properties.flDecayHFRatio;
			struct reverb_parameters parameters;
			int line;

			/* each line's gain a pass: 60 dB in the decay time at low
			frequencies, and in the decay time times the HF ratio at the HF
			reference (its filter's gain there, exactly) */
			reverb_parameters_set(&parameters, &properties);
			for (line = 0; line < REVERB_LINES; line++)
			{
				double seconds = (double)parameters.lengths[line] / OUTPUT_RATE;
				double w = 2.0 * PI * properties.flHFReference / OUTPUT_RATE;
				double b0 = parameters.damping_b0[line], b1 = parameters.damping_b1[line];
				double a1 = parameters.damping_a1[line];
				double low = parameters.feedback[line] * (b0 + b1) / (1.0 + a1);
				double at_reference = parameters.feedback[line] *
					sqrt((b0 * b0 + b1 * b1 + 2.0 * b0 * b1 * cos(w)) / (1.0 + a1 * a1 + 2.0 * a1 * cos(w)));

				CHECK(fabs(20.0 * log10(low) / seconds + 60.0 / properties.flDecayTime) < 0.01,
					"%s: line %d loses %.3f dB a second at low frequencies, not %.3f", environments[index].name, line,
					-20.0 * log10(low) / seconds, 60.0 / properties.flDecayTime);
				CHECK(fabs(20.0 * log10(at_reference) / seconds + 60.0 / high) < 0.05 * 60.0 / high,
					"%s: line %d loses %.3f dB a second at %.0f Hz, not %.3f", environments[index].name, line,
					-20.0 * log10(at_reference) / seconds, properties.flHFReference, 60.0 / high);
			}

			CHECK(fabs(figures.decay_low / properties.flDecayTime - 1.0) < 0.1,
				"%s: the decay time %.2f s, measured %.2f s", environments[index].name, properties.flDecayTime,
				figures.decay_low);
			/* (measured where it is long enough to measure: a decay of 0.3
			s falls the 20 dB fitted in 0.1 s, the time the late
			reverberation takes to build up, which the fit then reads as a
			slower decay; the lines' gains above hold for every one) */
			if (high >= 0.5)
				CHECK(fabs(figures.decay_high / high - 1.0) < 0.15,
					"%s: the decay time at %.0f Hz %.2f s, measured %.2f s", environments[index].name,
					properties.flHFReference, high, figures.decay_high);
		}
		return 0;
	}

	CASE("levels")
	{
		for (index = 0; index < ENVIRONMENTS; index++)
		{
			DSI3DL2LISTENER properties = listener_from_tag(&environments[index]);
			struct figures figures = measure(&properties);

			if (properties.lReflections > DSBVOLUME_MIN)
				CHECK(fabs(figures.reflections_db - properties.lReflections * 0.01) < 1.0,
					"%s: the reflections %.1f dB over the room, measured %.1f dB", environments[index].name,
					properties.lReflections * 0.01, figures.reflections_db);
			CHECK(fabs(figures.reverb_db - properties.lReverb * 0.01) < 1.0,
				"%s: the reverb %.1f dB over the room, measured %.1f dB", environments[index].name,
				properties.lReverb * 0.01, figures.reverb_db);
			CHECK(fabs(figures.room_hf_db - properties.lRoomHF * 0.01) < 0.1,
				"%s: the room HF %.1f dB, measured %.1f dB", environments[index].name, properties.lRoomHF * 0.01,
				figures.room_hf_db);
		}
		/* the reference: a 3D voice's direct path, centred, has an energy of
		1 over the two channels (the equal power pan) */
		{
			struct sdl_stream stream;
			float left, right, room, direct_lowpass, room_lowpass;
			BOOL hrtf_on;
			struct hrtf_parameters hrtf;

			memset(&stream, 0, sizeof(stream));
			stream.has_3d = TRUE;
			stream.channels = 1;
			stream.volume = 1.0f;
			stream.position[2] = 1.0f;
			stream.minimum_distance = 1.0f;
			stream.maximum_distance = 100.0f;
			stream.room = stream.room_hf = DSBVOLUME_MIN;
			listener.position[0] = listener.position[1] = listener.position[2] = 0.0f;
			voice_gains(&stream, &left, &right, &room, &direct_lowpass, &room_lowpass, &hrtf_on, &hrtf);
			CHECK(fabs(left * left + right * right - 1.0) < 1e-4, "the direct path's energy %.3f", left * left + right * right);
		}
		return 0;
	}

	CASE("timing")
	{
		for (index = 0; index < ENVIRONMENTS; index++)
		{
			DSI3DL2LISTENER properties = listener_from_tag(&environments[index]);
			struct figures figures;
			double onset = properties.flReflectionsDelay + properties.flReverbDelay;

			if (properties.lReflections <= DSBVOLUME_MIN)
				properties.lReflections = 0;
			figures = measure(&properties);
			CHECK(fabs(figures.first_reflection - properties.flReflectionsDelay) <= 1.0 / OUTPUT_RATE,
				"%s: the first reflection at %.1f ms, measured %.1f ms", environments[index].name,
				properties.flReflectionsDelay * 1000.0, figures.first_reflection * 1000.0);
			CHECK(figures.last_reflection <= onset + 1.0 / OUTPUT_RATE &&
				figures.last_reflection - properties.flReflectionsDelay >= 0.8 * properties.flReverbDelay,
				"%s: the reflections from %.1f ms to the late reverberation's %.1f ms, measured to %.1f ms",
				environments[index].name, properties.flReflectionsDelay * 1000.0, onset * 1000.0,
				figures.last_reflection * 1000.0);
			/* (its first percent of energy: the diffusers smear it a few
			milliseconds either way, and it builds up as the lines are heard,
			over 30 ms) */
			CHECK(figures.late_onset >= onset - 0.003 && figures.late_onset <= onset + 0.015,
				"%s: the late reverberation at %.1f ms, measured %.1f ms", environments[index].name, onset * 1000.0,
				figures.late_onset * 1000.0);
		}
		return 0;
	}

	/* from the hallway to the mountains as the game moves it, a step a tick
	(33 ms, 1600 frames), through the mix (SetI3DL2Listener's hand-off), a 3D
	voice of noise sending to the reverb all the while */
	CASE("transition")
	{
		struct environment_tag current = environments[5];
		const struct environment_tag *target = &environments[7];
		DSI3DL2LISTENER final = listener_from_tag(target);
		struct sdl_stream *stream = calloc(1, sizeof(*stream));
		float output[64 * 2], start, end, last;
		unsigned long tick, frame = 0, settled = 0, n;

		resampler_initialize();
		resampler_phases_initialize();
		stream->channels = 1;
		stream->sample_rate = OUTPUT_RATE;
		stream->volume = 1.0f;
		stream->has_3d = TRUE;
		stream->mode = DS3DMODE_NORMAL;
		stream->position[2] = 2.0f;
		stream->minimum_distance = 1.0f;
		stream->maximum_distance = 50.0f;
		stream->mixing[0].frames = 3 * OUTPUT_RATE;
		stream->mixing[0].samples = malloc(stream->mixing[0].frames * sizeof(short));
		for (n = 0; n < stream->mixing[0].frames; n++)
			stream->mixing[0].samples[n] = (short)(((n * 2654435761u) >> 16) & 0x1fff) - 0x1000;
		stream->packets[0] = stream->mixing[0];
		stream->packet_count = stream->submitted = stream->mixing_count = stream->mixing_next = 1;
		resampler_reset(stream);
		streams = stream;
		{
			struct reverb_parameters parameters;

			reverb_parameters_set(&parameters, &final);
			end = parameters.late_gain;
		}
		environment_set = listener_from_tag(&current);
		environment_changed = TRUE;
		mix(output, 64, FALSE);
		start = last = late_gain_now();
		for (tick = 0; tick < 60; tick++)
		{
			unsigned long chunk;

			/* scenario.c scenario_get_sound_environment's steps a tick */
#define STEP(field, step) current.field += fmaxf(-(step), fminf((step), target->field - current.field))
			STEP(room, 0.03f);
			STEP(room_hf, 0.03f);
			STEP(room_rolloff, 0.3f);
			STEP(decay_time, 0.1f);
			STEP(decay_hf_ratio, 0.03f);
			STEP(reflections, 0.03f);
			STEP(reflections_delay, 0.09f);
			STEP(reverb, 0.03f);
			STEP(reverb_delay, 0.003f);
			STEP(diffusion, 0.03f);
			STEP(density, 0.03f);
			STEP(hf_reference, 600.0f);
#undef STEP
			environment_set = listener_from_tag(&current);
			environment_changed = TRUE;
			/* 1600 frames a tick, in 64-frame mixes, the late gain watched */
			for (chunk = 0; chunk < 25; chunk++)
			{
				float now;

				mix(output, 64, FALSE);
				frame += 64;
				now = late_gain_now();
				CHECK((now - last) * (end - start) >= -1e-6f,
					"at %.0f ms the late gain went back towards the hallway's: %.4f after %.4f (%.4f to %.4f)",
					frame * 1000.0 / OUTPUT_RATE, now, last, start, end);
				last = now;
				if (!settled && !reverb.fade && !memcmp(&reverb.properties, &final, sizeof(final)))
					settled = frame;
			}
		}
		/* the game is there after 30 ticks (the reverb delay's, 1 s), the
		reverb within two fades after */
		CHECK(!reverb.silent && settled && settled < 30 * 1600 + 2 * REVERB_FADE_FRAMES + 128,
			"the reverb reached the mountains' at %.0f ms", settled * 1000.0 / OUTPUT_RATE);
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
