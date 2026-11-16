/* The Mac's sound output: the game's mix played through CoreAudio's own
output unit (AUHAL) rather than SDL's CoreAudio driver.

SDL 3.4's CoreAudio driver plays through an AudioQueue, which keeps three
buffers queued, or as many pairs as make 15 ms (SDL_coreaudio.m,
PrepareAudioQueue: MINIMUM_AUDIO_BUFFER_TIME_MS, for Bluetooth headsets that
record and play at once): at 256 frames 26.7 ms queued ahead of the buffer it
asks for, and smaller buffers queue more. The AudioQueue then plays through
the device's I/O cycle at its own 512 frames (10.7 ms at 48 kHz). The output
unit has no queue of its own: the device asks for each I/O buffer as it is
due (the render callback, on the device's I/O thread), and the I/O buffer is
audio.buffer_frames (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, which dsound_sdl.c
sets; at the device's rate, within its range). What the device then adds
after the I/O buffer, its own latency, safety offset and stream latency
(the built-in speakers' processing), is the system's and the same either way.

The stream (host_sdl.c's handle) is an SDL_AudioStream bound to no device:
its callback runs the guest's mix as SDL's device would call it, and the
stream converts the mix to the device's channels and rate (SDL's own
converters: 5.1 to a pair of speakers, 48 kHz to 44.1); the unit takes the
device's format, so it converts nothing. The I/O buffer is a setting of
this process's own (the HAL's buffer frame size is per process): other
programs playing on the device keep theirs.

The unit follows the system's default output (or audio.output_device's,
by SDL's name for it) as it changes: a new default, the device gone (to the
default), its rate or channels changed (another program, Bluetooth
headphones switching to their headset mode for a microphone). Each of those
stops the unit, sets it up for the device as it is now and starts it again,
on one serial queue with opening and closing it. While the computer
sleeps the device stops, and the unit plays on when it wakes. A change of
the device's latency alone (Bluetooth's) is read again and logged, the unit
playing on.

Bluetooth headphones' latency (their radio's buffering) is the device's,
which the log gives in ms as it opens. Only the system lowers it: Game
Mode (the app fullscreen) doubles Bluetooth's rate for headphones and
controllers, by Apple's account.

HALO_AUDIO_QUEUE=1 (or no output unit to be had) plays through SDL's
driver instead. */

#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/thread_info.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "host.h"

struct audio_output
{
	AudioUnit unit;
	SDL_AudioStream *stream;
	/* the format the guest mixes in, and the device's (the stream's
	output, the unit's input) */
	SDL_AudioSpec source;
	SDL_AudioSpec format;
	/* the device it plays on; whether it follows the system's default;
	SDL's name for the one asked for (follow 0) */
	AudioDeviceID device;
	int follow_default;
	char name[256];
	/* the I/O buffer asked for (frames at 48 kHz) and set (at the device's
	rate); the device's latency, safety offset and stream latency (frames)
	and the unit's (seconds) */
	UInt32 frames_asked;
	/* the times it was set up for a device (host_audio_output_timing, on
	the game's thread: counted once the rest is set) */
	_Atomic int setups;
	UInt32 frames;
	UInt32 device_latency, safety_offset, stream_latency;
	Float64 unit_latency;
	/* how the device is connected (kAudioDevicePropertyTransportType:
	Bluetooth's adds its radio's buffering to its latency) */
	UInt32 transport;
	/* whether it was started (resumed), and is set up for its device */
	int started;
	int configured;
	/* its number, which the device's notifications carry (one closed
	meanwhile is not found: outputs) */
	uintptr_t serial;
	struct audio_output *next;
	/* (the I/O thread) the time from a render callback to its buffer's
	start at the device (mach ticks), least and most since it started (or
	was set up again), and the callbacks since; the device's overloads (its
	I/O thread late) */
	_Atomic uint64_t lead_least, lead_most;
	_Atomic uint64_t callbacks;
	_Atomic unsigned overloads;
};

/* the outputs open (changed on the queue, outputs_lock held), and the queue
opening, changing and closing them */
static struct audio_output *outputs;
static pthread_mutex_t outputs_lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t output_serial;
static dispatch_queue_t output_queue;
static mach_timebase_info_data_t timebase;

static double ticks_ms(uint64_t ticks)
{
	return (double)ticks * timebase.numer / timebase.denom / 1.0e6;
}

static UInt32 device_u32(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope,
	UInt32 fallback)
{
	AudioObjectPropertyAddress address = { selector, scope, kAudioObjectPropertyElementMain };
	UInt32 value = 0, size = sizeof(value);

	return AudioObjectGetPropertyData(object, &address, 0, NULL, &size, &value) == noErr ? value : fallback;
}

static AudioDeviceID default_output(void)
{
	return device_u32(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeGlobal, kAudioObjectUnknown);
}

/* a device's output channels (0: none) */
static int device_channels(AudioDeviceID device)
{
	AudioObjectPropertyAddress address = { kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput,
		kAudioObjectPropertyElementMain };
	AudioBufferList *buffers;
	UInt32 size = 0, index;
	int channels = 0;

	if (AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size) != noErr || !size)
		return 0;
	buffers = malloc(size);
	if (!buffers)
		return 0;
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, buffers) == noErr)
	{
		for (index = 0; index < buffers->mNumberBuffers; index++)
			channels += (int)buffers->mBuffers[index].mNumberChannels;
	}
	free(buffers);
	return channels;
}

/* a device's name as SDL gives it (SDL_coreaudio.m: its output name, the
spaces at its end trimmed) into name; 1 on success */
static int device_name(AudioDeviceID device, char *name, size_t size)
{
	AudioObjectPropertyAddress address = { kAudioObjectPropertyName, kAudioDevicePropertyScopeOutput,
		kAudioObjectPropertyElementMain };
	CFStringRef text = NULL;
	UInt32 text_size = sizeof(text);
	size_t length;
	int ok;

	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &text_size, &text) != noErr || !text)
		return 0;
	ok = CFStringGetCString(text, name, (CFIndex)size, kCFStringEncodingUTF8);
	CFRelease(text);
	if (!ok)
		return 0;
	length = strlen(name);
	while (length && name[length - 1] == ' ')
		name[--length] = 0;
	return length > 0;
}

/* a device's connection as a word for the log (Bluetooth, built-in, USB...) */
static const char *transport_name(UInt32 transport, char *code, size_t size)
{
	switch (transport)
	{
	case kAudioDeviceTransportTypeBuiltIn: return "built-in";
	case kAudioDeviceTransportTypeBluetooth: return "Bluetooth";
	case kAudioDeviceTransportTypeBluetoothLE: return "Bluetooth LE";
	case kAudioDeviceTransportTypeUSB: return "USB";
	case kAudioDeviceTransportTypeHDMI: return "HDMI";
	case kAudioDeviceTransportTypeDisplayPort: return "DisplayPort";
	case kAudioDeviceTransportTypeThunderbolt: return "Thunderbolt";
	case kAudioDeviceTransportTypeAirPlay: return "AirPlay";
	case kAudioDeviceTransportTypeAggregate: return "aggregate";
	case kAudioDeviceTransportTypeVirtual: return "virtual";
	case kAudioDeviceTransportTypeUnknown: return "unknown";
	default:
		snprintf(code, size, "'%c%c%c%c'", (char)(transport >> 24), (char)(transport >> 16), (char)(transport >> 8),
			(char)transport);
		return code;
	}
}

static int transport_bluetooth(UInt32 transport)
{
	return transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE;
}

/* the output device SDL names name; kAudioObjectUnknown when none */
static AudioDeviceID device_named(const char *name)
{
	AudioObjectPropertyAddress address = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain };
	AudioDeviceID devices[128], found = kAudioObjectUnknown;
	UInt32 size = sizeof(devices), index;
	char text[256];

	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, devices) != noErr)
		return kAudioObjectUnknown;
	for (index = 0; index < size / sizeof(devices[0]) && found == kAudioObjectUnknown; index++)
	{
		if (device_channels(devices[index]) > 0 && device_name(devices[index], text, sizeof(text)) &&
			!strcmp(text, name))
			found = devices[index];
	}
	return found;
}

/* the device the output is to play on now: the default, or the one named
while it is there (else the default, followed from then on) */
static AudioDeviceID output_device_choose(struct audio_output *output)
{
	AudioDeviceID device;

	if (!output->follow_default)
	{
		device = device_named(output->name);
		if (device != kAudioObjectUnknown &&
			device_u32(device, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, 0))
			return device;
		host_logf(HOST_LOG_INFO, "sound: no output device \"%s\" now; the system's default", output->name);
		output->follow_default = 1;
	}
	return default_output();
}

/* ---------- the device's notifications */

/* (its latency and safety offset too, which a Bluetooth device may change
as it plays: re-read without setting the unit up again) */
static const AudioObjectPropertySelector device_watched[] = {
	kAudioDevicePropertyDeviceIsAlive, kAudioDevicePropertyNominalSampleRate,
	kAudioDevicePropertyStreamConfiguration, kAudioDeviceProcessorOverload,
	kAudioDevicePropertyLatency, kAudioDevicePropertySafetyOffset,
};

static void output_change(void *context);
static void output_overload(void *context);
static void output_lead_log(void *context);

static struct audio_output *output_find(uintptr_t serial)
{
	struct audio_output *output;

	for (output = outputs; output; output = output->next)
	{
		if (output->serial == serial)
			return output;
	}
	return NULL;
}

/* (the HAL's notification thread) an overload is logged and anything else
sets the output up again, on the queue (where one closed is not found) */
static OSStatus device_changed(AudioObjectID object, UInt32 count, const AudioObjectPropertyAddress *addresses,
	void *context)
{
	UInt32 index;
	int change = 0;

	(void)object;
	for (index = 0; index < count; index++)
	{
		if (addresses[index].mSelector == kAudioDeviceProcessorOverload)
		{
			/* (read on the queue: the output may be closing) */
			dispatch_async_f(output_queue, context, output_overload);
		}
		else
		{
			change = 1;
		}
	}
	if (change)
		dispatch_async_f(output_queue, context, output_change);
	return noErr;
}

static void device_watch(struct audio_output *output, AudioDeviceID device, int watch)
{
	size_t index;

	for (index = 0; index < sizeof(device_watched) / sizeof(device_watched[0]); index++)
	{
		AudioObjectPropertyAddress address = { device_watched[index], kAudioObjectPropertyScopeWildcard,
			kAudioObjectPropertyElementWildcard };

		if (watch)
			AudioObjectAddPropertyListener(device, &address, device_changed, (void *)output->serial);
		else
			AudioObjectRemovePropertyListener(device, &address, device_changed, (void *)output->serial);
	}
}

static const AudioObjectPropertyAddress default_address = { kAudioHardwarePropertyDefaultOutputDevice,
	kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };

/* ---------- the render callback (the device's I/O thread) */

static OSStatus render(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
	UInt32 frame_count, AudioBufferList *data)
{
	struct audio_output *output = context;
	uint64_t now = mach_absolute_time();
	UInt32 index;

	(void)flags;
	(void)bus;
	(void)frame_count;
	for (index = 0; index < data->mNumberBuffers; index++)
	{
		AudioBuffer *buffer = &data->mBuffers[index];
		int got = index ? 0 : SDL_GetAudioStreamData(output->stream, buffer->mData, (int)buffer->mDataByteSize);

		if (got < 0)
			got = 0;
		if ((UInt32)got < buffer->mDataByteSize)
			memset((unsigned char *)buffer->mData + got, 0, buffer->mDataByteSize - (UInt32)got);
	}
	/* (how far ahead of its buffer the device asks: its I/O buffer and
	safety offset, as the system's latency counts them) */
	if (time && (time->mFlags & kAudioTimeStampHostTimeValid) && time->mHostTime > now)
	{
		uint64_t lead = time->mHostTime - now;
		uint64_t least = atomic_load_explicit(&output->lead_least, memory_order_relaxed);

		if (!least || lead < least)
			atomic_store_explicit(&output->lead_least, lead, memory_order_relaxed);
		if (lead > atomic_load_explicit(&output->lead_most, memory_order_relaxed))
			atomic_store_explicit(&output->lead_most, lead, memory_order_relaxed);
	}
	atomic_fetch_add_explicit(&output->callbacks, 1, memory_order_relaxed);
	return noErr;
}

/* ---------- setting the unit up (the queue) */

/* (the queue) how far ahead of its buffer the device asked in the first
LEAD_LOG_SECONDS it played (which its I/O buffer and safety offset should
account for), logged */
#define LEAD_LOG_SECONDS 5
static void output_lead_log(void *context)
{
	struct audio_output *output = output_find((uintptr_t)context);
	double rate;

	if (!output || !output->format.freq || !atomic_load(&output->lead_least))
		return;
	rate = output->format.freq;
	host_logf(HOST_LOG_INFO, "sound: the device asked for each buffer %.2f to %.2f ms before it played it (%llu "
		"buffers; its I/O buffer and safety offset: %.2f ms); %u overloads",
		ticks_ms(atomic_load(&output->lead_least)), ticks_ms(atomic_load(&output->lead_most)),
		(unsigned long long)atomic_load(&output->callbacks),
		(double)(output->frames + output->safety_offset) * 1000.0 / rate, atomic_load(&output->overloads));
}

static void output_lead_watch(struct audio_output *output)
{
	atomic_store(&output->lead_least, 0);
	atomic_store(&output->lead_most, 0);
	atomic_store(&output->callbacks, 0);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, LEAD_LOG_SECONDS * NSEC_PER_SEC), output_queue,
		(void *)output->serial, output_lead_log);
}

/* the channel layout SDL gives an AudioQueue of channels (SDL_coreaudio.m),
so that 5.1 and 7.1 reach the same speakers; 0 for mono and stereo */
static AudioChannelLayoutTag layout_tag(int channels)
{
	switch (channels)
	{
	case 3: return kAudioChannelLayoutTag_DVD_4;
	case 4: return kAudioChannelLayoutTag_Quadraphonic;
	case 5: return kAudioChannelLayoutTag_DVD_6;
	case 6: return kAudioChannelLayoutTag_DVD_12;
	case 7: return kAudioChannelLayoutTag_WAVE_6_1;
	case 8: return kAudioChannelLayoutTag_WAVE_7_1;
	default: return 0;
	}
}

/* the device's latency, safety offset and stream latency (frames) read
into the output; 1 when any changed */
static int output_latency_read(struct audio_output *output, AudioDeviceID device)
{
	AudioObjectPropertyAddress address = { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMain };
	AudioStreamID streams[16];
	UInt32 size = sizeof(streams);
	UInt32 device_latency = device_u32(device, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, 0);
	UInt32 safety_offset = device_u32(device, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, 0);
	UInt32 stream_latency = 0;
	int changed;

	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, streams) == noErr && size >= sizeof(streams[0]))
		stream_latency = device_u32(streams[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, 0);
	changed = device_latency != output->device_latency || safety_offset != output->safety_offset ||
		stream_latency != output->stream_latency;
	output->device_latency = device_latency;
	output->safety_offset = safety_offset;
	output->stream_latency = stream_latency;
	return changed;
}

/* the time the device adds after the I/O buffer (ms): its latency, safety
offset and stream latency, and the unit's */
static double output_after_ms(const struct audio_output *output, double rate)
{
	return (double)(output->device_latency + output->safety_offset + output->stream_latency) * 1000.0 / rate +
		output->unit_latency * 1000.0;
}

/* the unit set up for device as it is now and started (if it was): 1 on
success */
static int output_configure(struct audio_output *output, AudioDeviceID device)
{
	AudioObjectPropertyAddress address = { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMain };
	AudioStreamBasicDescription description;
	AudioValueRange range = { 0.0, 0.0 };
	Float64 rate = 0.0;
	UInt32 size, frames;
	int channels = device_channels(device);
	OSStatus result;
	char name[256] = "?", code[8];

	size = sizeof(rate);
	if (device == kAudioObjectUnknown || channels <= 0 ||
		AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &rate) != noErr || rate < 8000.0)
	{
		host_logf(HOST_LOG_WARN, "sound: no output device to play on (CoreAudio's device %u)", (unsigned)device);
		return 0;
	}
	/* (at most SDL's 7.1; a device of more plays it on its first channels) */
	if (channels > 8)
		channels = 8;
	result = AudioUnitSetProperty(output->unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
		&device, sizeof(device));
	if (result != noErr)
	{
		host_logf(HOST_LOG_WARN, "sound: the output unit cannot play on device %u (%d)", (unsigned)device, (int)result);
		return 0;
	}

	/* the I/O buffer: the frames asked for at 48 kHz, at the device's rate,
	within its range */
	frames = (UInt32)((double)output->frames_asked * rate / 48000.0 + 0.5);
	address.mSelector = kAudioDevicePropertyBufferFrameSizeRange;
	size = sizeof(range);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &range) == noErr && range.mMaximum > 0.0)
	{
		if (frames < (UInt32)range.mMinimum)
			frames = (UInt32)range.mMinimum;
		if (frames > (UInt32)range.mMaximum)
			frames = (UInt32)range.mMaximum;
	}
	/* (set through the unit, which passes it to the device in step with its
	own start: set on the device itself, a unit started just after now and
	then never ran, its device's I/O never coming) */
	if (AudioUnitSetProperty(output->unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames,
			sizeof(frames)) != noErr)
		host_logf(HOST_LOG_WARN, "sound: the device keeps its I/O buffer (%u frames asked for)", (unsigned)frames);
	size = sizeof(output->frames);
	if (AudioUnitGetProperty(output->unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0,
			&output->frames, &size) != noErr)
		output->frames = frames;
	output_latency_read(output, device);
	output->transport = device_u32(device, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal,
		kAudioDeviceTransportTypeUnknown);

	/* the mix converted to the device's channels and rate by SDL, which the
	unit takes as it is */
	output->format.format = SDL_AUDIO_F32;
	output->format.channels = channels;
	output->format.freq = (int)rate;
	if (!SDL_SetAudioStreamFormat(output->stream, &output->source, &output->format))
	{
		host_logf(HOST_LOG_WARN, "sound: %s", SDL_GetError());
		return 0;
	}
	memset(&description, 0, sizeof(description));
	description.mSampleRate = rate;
	description.mFormatID = kAudioFormatLinearPCM;
	description.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	description.mChannelsPerFrame = (UInt32)channels;
	description.mBitsPerChannel = 32;
	description.mFramesPerPacket = 1;
	description.mBytesPerFrame = 4 * (UInt32)channels;
	description.mBytesPerPacket = description.mBytesPerFrame;
	result = AudioUnitSetProperty(output->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &description,
		sizeof(description));
	if (result != noErr)
	{
		host_logf(HOST_LOG_WARN, "sound: the output unit takes no %d channels at %.0f Hz (%d)", channels, rate,
			(int)result);
		return 0;
	}
	if (layout_tag(channels))
	{
		AudioChannelLayout layout;

		memset(&layout, 0, sizeof(layout));
		layout.mChannelLayoutTag = layout_tag(channels);
		/* (without it, the channels go to the device's in order) */
		AudioUnitSetProperty(output->unit, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, &layout,
			sizeof(layout));
	}
	result = AudioUnitInitialize(output->unit);
	if (result != noErr)
	{
		host_logf(HOST_LOG_WARN, "sound: the output unit does not start (%d)", (int)result);
		return 0;
	}
	output->unit_latency = 0.0;
	size = sizeof(output->unit_latency);
	AudioUnitGetProperty(output->unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &output->unit_latency,
		&size);
	atomic_fetch_add(&output->setups, 1);
	if (output->device != device)
	{
		if (output->device != kAudioObjectUnknown)
			device_watch(output, output->device, 0);
		device_watch(output, device, 1);
		output->device = device;
	}
	device_name(device, name, sizeof(name));
	host_logf(HOST_LOG_INFO, "sound: CoreAudio's output unit on %s (%s): %d channels at %.0f Hz, an I/O buffer of %u "
		"frames (%u asked for, %.0f to %.0f); the device's latency %u, safety offset %u and stream latency %u frames "
		"(%.1f ms after the buffer)", name, transport_name(output->transport, code, sizeof(code)), channels, rate,
		(unsigned)output->frames, (unsigned)frames, range.mMinimum, range.mMaximum, (unsigned)output->device_latency,
		(unsigned)output->safety_offset, (unsigned)output->stream_latency, output_after_ms(output, rate));
	/* (Bluetooth headphones play at 44.1 or 48 kHz; at 24 kHz or less they
	are in their headset (hands-free) mode, which a microphone open on
	them, ours or another program's, keeps them in) */
	if (transport_bluetooth(output->transport) && rate <= 24000.0)
		host_logf(HOST_LOG_INFO, "sound: %s plays at %.0f Hz, probably its headset mode while a microphone is open on "
			"it (voice chat, or another program); its usual quality comes back once that closes", name, rate);
	if (output->started)
	{
		result = AudioOutputUnitStart(output->unit);
		if (result != noErr)
			host_logf(HOST_LOG_WARN, "sound: the output unit does not play (%d)", (int)result);
		output_lead_watch(output);
	}
	return 1;
}

static void output_stop(struct audio_output *output)
{
	AudioOutputUnitStop(output->unit);
	AudioUnitUninitialize(output->unit);
}

/* (the queue) the device changed, gone, or another made the default */
static void output_change(void *context)
{
	struct audio_output *output = output_find((uintptr_t)context);
	AudioDeviceID device;
	Float64 rate = 0.0;
	AudioObjectPropertyAddress address = { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMain };
	UInt32 size = sizeof(rate);

	if (!output)
		return;
	device = output_device_choose(output);
	AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &rate);
	/* (several notifications come for one change: once it is as set up,
	nothing to do but read its latency again, which the unit plays on
	through; dsound_sdl.c logs the new figure) */
	if (output->configured && device == output->device && (int)rate == output->format.freq &&
		SDL_min(device_channels(device), 8) == output->format.channels &&
		device_u32(device, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, 0))
	{
		UInt32 latency = output->device_latency, safety = output->safety_offset, stream = output->stream_latency;

		if (output_latency_read(output, device))
			host_logf(HOST_LOG_INFO, "sound: the device's latency %u, safety offset %u and stream latency %u frames "
				"(were %u, %u and %u): %.1f ms after the buffer", (unsigned)output->device_latency,
				(unsigned)output->safety_offset, (unsigned)output->stream_latency, (unsigned)latency, (unsigned)safety,
				(unsigned)stream, output_after_ms(output, rate));
		return;
	}
	output_stop(output);
	output->configured = output_configure(output, device);
	if (!output->configured)
		host_logf(HOST_LOG_WARN, "sound: silent until an output device can be played on");
}

/* (the queue) the device's I/O thread was late: logged (a dropout) */
static void output_overload(void *context)
{
	struct audio_output *output = output_find((uintptr_t)context);

	if (!output)
		return;
	host_logf(HOST_LOG_WARN, "sound: CoreAudio reports the device's I/O cycle overloaded (%u since it opened)",
		atomic_fetch_add(&output->overloads, 1) + 1);
}

/* ---------- the host's (host_sdl.c) */

struct open_request
{
	SDL_AudioDeviceID device;
	const SDL_AudioSpec *spec;
	SDL_AudioStreamCallback callback;
	void *userdata;
	SDL_AudioStream *stream;
};

static void output_open(void *context)
{
	struct open_request *request = context;
	AudioComponentDescription description = { kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
		kAudioUnitManufacturer_Apple, 0, 0 };
	AudioComponent component = AudioComponentFindNext(NULL, &description);
	AURenderCallbackStruct callback;
	struct audio_output *output;
	const char *hint = SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
	long frames = hint ? strtol(hint, NULL, 10) : 0;

	if (!component)
		return;
	output = calloc(1, sizeof(*output));
	if (!output)
		return;
	output->source = *request->spec;
	output->frames_asked = (UInt32)(frames > 0 ? frames : 512);
	output->serial = ++output_serial;
	if (request->device == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
	{
		output->follow_default = 1;
	}
	else
	{
		const char *name = SDL_GetAudioDeviceName(request->device);

		snprintf(output->name, sizeof(output->name), "%s", name ? name : "");
		output->follow_default = !output->name[0];
	}
	output->stream = SDL_CreateAudioStream(request->spec, request->spec);
	if (!output->stream || AudioComponentInstanceNew(component, &output->unit) != noErr)
		goto failed;
	callback.inputProc = render;
	callback.inputProcRefCon = output;
	if (AudioUnitSetProperty(output->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
			sizeof(callback)) != noErr ||
		!SDL_SetAudioStreamGetCallback(output->stream, request->callback, request->userdata) ||
		!output_configure(output, output_device_choose(output)))
		goto failed;
	output->configured = 1;
	AudioObjectAddPropertyListener(kAudioObjectSystemObject, &default_address, device_changed, (void *)output->serial);
	pthread_mutex_lock(&outputs_lock);
	output->next = outputs;
	outputs = output;
	pthread_mutex_unlock(&outputs_lock);
	request->stream = output->stream;
	return;

failed:
	if (output->device != kAudioObjectUnknown)
		device_watch(output, output->device, 0);
	if (output->unit)
		AudioComponentInstanceDispose(output->unit);
	if (output->stream)
		SDL_DestroyAudioStream(output->stream);
	free(output);
}

static void output_queue_create(void *context)
{
	(void)context;
	mach_timebase_info(&timebase);
	output_queue = dispatch_queue_create("openh1.audio_output", DISPATCH_QUEUE_SERIAL);
}

/* a stream whose callback the device calls for the guest's mix, playing on
device (SDL's: its default, or one SDL names), paused, as
SDL_OpenAudioDeviceStream's; NULL when SDL's driver is to play it */
SDL_AudioStream *host_audio_output_open(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
	SDL_AudioStreamCallback callback, void *userdata)
{
	static dispatch_once_t once;
	struct open_request request = { device, spec, callback, userdata, NULL };
	const char *driver = SDL_GetCurrentAudioDriver();
	const char *queue = getenv("HALO_AUDIO_QUEUE");

	if (!driver || strcmp(driver, "coreaudio") || (queue && *queue == '1') || !spec || !callback)
		return NULL;
	dispatch_once_f(&once, NULL, output_queue_create);
	dispatch_sync_f(output_queue, &request, output_open);
	if (!request.stream)
		host_logf(HOST_LOG_WARN, "sound: no CoreAudio output unit; SDL's driver plays it");
	return request.stream;
}

/* the output of a stream, if it is one (any thread: not by the stream's
SDL properties, whose lock the render callback holds while it waits for
the mix) */
static struct audio_output *output_of(SDL_AudioStream *stream)
{
	struct audio_output *output;

	pthread_mutex_lock(&outputs_lock);
	for (output = outputs; output && output->stream != stream; output = output->next)
		;
	pthread_mutex_unlock(&outputs_lock);
	return stream ? output : NULL;
}

static void output_resume(void *context)
{
	struct audio_output *output = context;

	if (output->started)
		return;
	output->started = 1;
	if (AudioOutputUnitStart(output->unit) != noErr)
		host_logf(HOST_LOG_WARN, "sound: the output unit does not play");
	output_lead_watch(output);
}

/* the output's stream played: 1 on success, -1 when the stream is not one */
int host_audio_output_resume(SDL_AudioStream *stream)
{
	struct audio_output *output = output_of(stream);

	if (!output)
		return -1;
	dispatch_sync_f(output_queue, output, output_resume);
	return 1;
}

static void output_close(void *context)
{
	struct audio_output *output = context;
	struct audio_output **link;

	/* (returns once a render callback running has finished) */
	output_stop(output);
	AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &default_address, device_changed,
		(void *)output->serial);
	if (output->device != kAudioObjectUnknown)
		device_watch(output, output->device, 0);
	AudioComponentInstanceDispose(output->unit);
	pthread_mutex_lock(&outputs_lock);
	for (link = &outputs; *link; link = &(*link)->next)
	{
		if (*link == output)
		{
			*link = output->next;
			break;
		}
	}
	pthread_mutex_unlock(&outputs_lock);
}

/* the output's unit stopped and closed, and its stream destroyed: 1, or 0
when the stream is not one */
int host_audio_output_close(SDL_AudioStream *stream)
{
	struct audio_output *output = output_of(stream);

	if (!output)
		return 0;
	dispatch_sync_f(output_queue, output, output_close);
	SDL_DestroyAudioStream(stream);
	free(output);
	return 1;
}

/* ---------- the thread that runs the mix

The render callback hands each request to a thread of host_sdl.c's that
can run the guest (its audio_thread), and waits for the mix. That thread
is made a real-time one, its period the I/O cycle's, as the device's I/O
thread is: SDL's TIME_CRITICAL alone (SCHED_RR) ran dry more often with
the processor busy. (Joining the device's I/O workgroup too, as Apple has a
thread that does an I/O thread's work, ran dry more under load, not less.) */

/* (the thread running the mix, before each request: host_sdl.c) the thread
made real-time for the output stream plays on, as it is set up now; *state
and *found are the thread's own, 0 and NULL at first: the output, found
once, so that each request takes no lock the game's threads take (the
output lives as long as the stream). Nothing for a stream of SDL's driver */
void host_audio_output_thread(SDL_AudioStream *stream, int *state, void **found)
{
	struct audio_output *output = *found;
	thread_time_constraint_policy_data_t policy;
	uint64_t period;
	int setups, result;

	if (!output)
		*found = output = output_of(stream);
	if (!output || !output->format.freq)
		return;
	setups = atomic_load(&output->setups);
	if (*state == setups)
		return;
	*state = setups;
	/* (the I/O cycle in mach ticks; at most half of it computing, all of it
	the deadline) */
	period = (uint64_t)((double)output->frames / output->format.freq * 1.0e9 * timebase.denom / timebase.numer);
	policy.period = (uint32_t)period;
	policy.computation = (uint32_t)(period / 2);
	policy.constraint = (uint32_t)period;
	policy.preemptible = 1;
	result = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
		(thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	if (result != KERN_SUCCESS)
		host_logf(HOST_LOG_WARN, "sound: the mix's thread is not made real-time (%d)", result);
}

/* (host_sdl.c's HALO_AUDIO_MIX_TIMES, on the thread running the mix) the
thread's processor time (ns), the process's page faults, pages read in and
context switches, and the thread's priority now */
void host_audio_output_usage(uint64_t *values)
{
	task_events_info_data_t events;
	mach_msg_type_number_t count = TASK_EVENTS_INFO_COUNT;
	thread_extended_info_data_t thread;
	mach_msg_type_number_t thread_count = THREAD_EXTENDED_INFO_COUNT;
	mach_port_t self = pthread_mach_thread_np(pthread_self());
	/* (mach_task_self's own: <mach/mach.h> declares a host_page_size that
	is not host.h's) */
	extern mach_port_t mach_task_self_;

	memset(values, 0, 5 * sizeof(*values));
	values[0] = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
	if (task_info(mach_task_self_, TASK_EVENTS_INFO, (task_info_t)&events, &count) == KERN_SUCCESS)
	{
		values[1] = (uint64_t)(uint32_t)events.faults;
		values[2] = (uint64_t)(uint32_t)events.pageins;
		values[3] = (uint64_t)(uint32_t)events.csw;
	}
	if (thread_info(self, THREAD_EXTENDED_INFO, (thread_info_t)&thread, &thread_count) == KERN_SUCCESS)
		values[4] = (uint64_t)thread.pth_curpri;
}

/* (host_sdl_audio_stream_timing) the output's I/O buffer (frames) and rate,
nothing queued ahead of it, the latency after (microseconds: the I/O
buffer, the device's latency, safety offset and stream latency, and the
unit's), and the times it was set up for a device (which a device's change
counts). 1, or 0 when the stream is not one */
int host_audio_output_timing(SDL_AudioStream *stream, int *timing)
{
	struct audio_output *output = output_of(stream);
	double rate;

	if (!output || !output->format.freq)
		return 0;
	rate = output->format.freq;
	timing[0] = (int)output->frames;
	timing[1] = output->format.freq;
	timing[2] = 0;
	timing[3] = (int)(((double)(output->frames + output->device_latency + output->safety_offset +
		output->stream_latency) / rate + output->unit_latency) * 1.0e6);
	timing[4] = atomic_load(&output->setups);
	return 1;
}

#endif
