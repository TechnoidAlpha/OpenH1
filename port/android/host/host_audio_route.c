/*
HOST_AUDIO_ROUTE.C

What the system's sound plays on, for the sound's log
(port/linux/src/dsound_sdl.c: audio.spatial "auto" is the HRTF on any
stereo output, as the Xbox's, so it no longer depends on it). The guest asks each frame
(host_audio_headphones), which reads what a thread of the host's own last
found: it looks once a second, so that asking costs the game nothing and a
slow answer from the system (a sound server, COM) never holds it. Each
system's own way:
	- macOS: CoreAudio's default output device: its name (External
	  Headphones, AirPods, a headset), its transport (Bluetooth: AirPods and
	  other headphones, unless its name says a speaker) and its data source
	  (the built-in output's headphone jack);
	- iOS: the audio session's route (port/ios/host_app.m);
	- Windows: the default endpoint's form factor (MMDevice: Headphones,
	  Headset; a Bluetooth or USB headset is one of them);
	- Linux: the sound server's default sink (PulseAudio, or PipeWire's
	  pulse server; libpulse loaded at run time, as SDL loads it): its form
	  factor (headphone, headset), its active port (the headphones', a
	  headset's) or a Bluetooth sink's; else, or with no server, ALSA's
	  headphone or headset jack of its card. The Steam Deck's 3.5 mm jack is
	  its card's Headphones port, and ALSA's Headset Jack;
	- Android: AudioManager's output devices (wired headphones and headsets,
	  Bluetooth, USB headsets: HaloActivity.audioOutputHeadphones).
1 headphones, 0 speakers, -1 not known (which the guest takes as
speakers).
*/

#include "host.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
/* port/ios/host_app.m: the audio session's route */
int host_app_audio_headphones(void);
#elif defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#elif defined(_WIN32)
#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#elif defined(__ANDROID__)
#include <jni.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#if __has_include(<sound/asound.h>)
#include <sound/asound.h>
#define ROUTE_ALSA 1
#endif
#if __has_include(<pulse/pulseaudio.h>)
#include <pulse/pulseaudio.h>
#define ROUTE_PULSE 1
#endif
#endif

/* how often the thread looks (milliseconds) */
#define ROUTE_PERIOD 1000

static int route_found = -1;
static int route_started;

/* whether a device's name says headphones (1), a speaker (0), or neither
(-1) */
static int route_name(const char *name)
{
	static const char *const headphones[] = { "headphone", "headset", "airpods", "earbud", "earphone", "buds",
		NULL };
	static const char *const speakers[] = { "speaker", "soundbar", "hdmi", "displayport", NULL };
	char lower[256];
	size_t index;

	if (!name)
		return -1;
	for (index = 0; name[index] && index + 1 < sizeof(lower); index++)
		lower[index] = (char)tolower((unsigned char)name[index]);
	lower[index] = 0;
	for (index = 0; headphones[index]; index++)
	{
		if (strstr(lower, headphones[index]))
			return 1;
	}
	for (index = 0; speakers[index]; index++)
	{
		if (strstr(lower, speakers[index]))
			return 0;
	}
	return -1;
}

/* ---------- macOS */

#if defined(__APPLE__) && !TARGET_OS_IPHONE

#define ROUTE_FOUR(a, b, c, d) (((UInt32)(a) << 24) | ((UInt32)(b) << 16) | ((UInt32)(c) << 8) | (UInt32)(d))

static int route_detect(void)
{
	AudioObjectPropertyAddress address = { kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeGlobal, 0 };
	AudioDeviceID device = kAudioObjectUnknown;
	UInt32 size = sizeof(device), transport = 0, source = 0;
	CFStringRef name_string = NULL;
	char name[256] = "";
	int named;

	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) != noErr ||
		device == kAudioObjectUnknown)
		return -1;
	address.mSelector = kAudioDevicePropertyTransportType;
	size = sizeof(transport);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &transport) != noErr)
		transport = 0;
	address.mSelector = kAudioObjectPropertyName;
	size = sizeof(name_string);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &name_string) == noErr && name_string)
	{
		if (!CFStringGetCString(name_string, name, sizeof(name), kCFStringEncodingUTF8))
			name[0] = 0;
		CFRelease(name_string);
	}
	named = route_name(name);
	/* (the built-in output's jack: its data source, headphones or the
	internal speakers; on Apple silicon the jack is a device of its own,
	External Headphones) */
	address.mSelector = kAudioDevicePropertyDataSource;
	address.mScope = kAudioDevicePropertyScopeOutput;
	size = sizeof(source);
	if (AudioObjectHasProperty(device, &address) &&
		AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &source) == noErr)
	{
		if (source == ROUTE_FOUR('h', 'd', 'p', 'n'))
			return 1;
		if (source == ROUTE_FOUR('i', 's', 'p', 'k'))
			return 0;
	}
	if (named >= 0)
		return named;
	/* (AirPods and the other Bluetooth headphones; a Bluetooth speaker is
	one by name) */
	if (transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE)
		return 1;
	return 0;
}

#endif

/* ---------- iOS */

#if defined(__APPLE__) && TARGET_OS_IPHONE

static int route_detect(void)
{
	return host_app_audio_headphones();
}

#endif

/* ---------- Windows */

#if defined(_WIN32)

/* (MMDevice's: written out, so as to need neither INITGUID nor uuid.lib) */
static const CLSID route_enumerator_class = { 0xbcde0395, 0xe52f, 0x467c,
	{ 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const IID route_enumerator_interface = { 0xa95664d2, 0x9614, 0x4f35,
	{ 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
/* PKEY_AudioEndpoint_FormFactor */
static const PROPERTYKEY route_form_factor = { { 0x1da5d803, 0xd492, 0x4edd,
	{ 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 0 };

static int route_detect(void)
{
	static IMMDeviceEnumerator *enumerator;
	static BOOL initialized;
	IMMDevice *device = NULL;
	IPropertyStore *store = NULL;
	PROPVARIANT value;
	int found = -1;

	if (!initialized)
	{
		/* (this thread's own COM, for good) */
		initialized = TRUE;
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if (FAILED(CoCreateInstance(&route_enumerator_class, NULL, CLSCTX_ALL, &route_enumerator_interface,
			(void **)&enumerator)))
			enumerator = NULL;
	}
	if (!enumerator)
		return -1;
	if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device)))
		return -1;
	if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &store)))
	{
		PropVariantInit(&value);
		if (SUCCEEDED(IPropertyStore_GetValue(store, &route_form_factor, &value)) && value.vt == VT_UI4)
			found = value.ulVal == Headphones || value.ulVal == Headset ? 1 : 0;
		PropVariantClear(&value);
		IPropertyStore_Release(store);
	}
	IMMDevice_Release(device);
	return found;
}

#endif

/* ---------- Android */

#if defined(__ANDROID__)

static int route_detect(void)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	int found = -1;

	if (env && activity)
	{
		/* (on the activity's own class: this thread's class loader is the
		system's, as host_gesture_insets says) */
		jclass activity_class = (*env)->GetObjectClass(env, activity);
		jmethodID method = activity_class ? (*env)->GetMethodID(env, activity_class, "audioOutputHeadphones", "()I")
			: NULL;

		if (method)
			found = (int)(*env)->CallIntMethod(env, activity, method);
		if ((*env)->ExceptionCheck(env))
		{
			(*env)->ExceptionClear(env);
			found = -1;
		}
		if (activity_class)
			(*env)->DeleteLocalRef(env, activity_class);
	}
	if (env && activity)
		(*env)->DeleteLocalRef(env, activity);
	return found;
}

#endif

/* ---------- Linux */

#if defined(__linux__) && !defined(__ANDROID__)

#ifdef ROUTE_ALSA
/* a card's headphone or headset jack (ALSA's controls, as the kernel's
driver names them: "Headphone Jack", "Headset Jack", "Front Headphone
Jack"; not a microphone's, nor a phantom one, which is always there): 1
plugged in, 0 not, -1 none */
static int route_alsa_jack(int card)
{
	struct snd_ctl_elem_list list;
	struct snd_ctl_elem_id *ids;
	char path[32];
	unsigned int index;
	int handle, found = -1;

	snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
	handle = open(path, O_RDONLY | O_CLOEXEC);
	if (handle < 0)
		return -1;
	memset(&list, 0, sizeof(list));
	if (ioctl(handle, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0 || !list.count ||
		!(ids = calloc(list.count, sizeof(*ids))))
	{
		close(handle);
		return -1;
	}
	list.space = list.count;
	list.pids = ids;
	if (ioctl(handle, SNDRV_CTL_IOCTL_ELEM_LIST, &list) >= 0)
	{
		for (index = 0; index < list.used; index++)
		{
			const char *name = (const char *)ids[index].name;
			struct snd_ctl_elem_value value;

			if (ids[index].iface != SNDRV_CTL_ELEM_IFACE_CARD || !strstr(name, "Jack") || strstr(name, "Mic") ||
				strstr(name, "Phantom") || (!strstr(name, "Headphone") && !strstr(name, "Headset")))
				continue;
			memset(&value, 0, sizeof(value));
			value.id.numid = ids[index].numid;
			if (ioctl(handle, SNDRV_CTL_IOCTL_ELEM_READ, &value) < 0)
				continue;
			if (value.value.integer.value[0])
			{
				found = 1;
				break;
			}
			found = 0;
		}
	}
	free(ids);
	close(handle);
	return found;
}
#endif

/* the first card's jack that there is (no sound server to say which card
plays) */
static int route_alsa_any(void)
{
#ifdef ROUTE_ALSA
	int card, found = -1;

	for (card = 0; card < 8 && found < 1; card++)
	{
		int jack = route_alsa_jack(card);

		if (jack >= 0)
			found = jack;
	}
	return found;
#else
	return -1;
#endif
}

#ifdef ROUTE_PULSE

/* libpulse's functions, loaded at run time (the game runs without it) */
static struct
{
	int loaded, tried;
	pa_mainloop *(*mainloop_new)(void);
	void (*mainloop_free)(pa_mainloop *);
	pa_mainloop_api *(*mainloop_get_api)(pa_mainloop *);
	int (*mainloop_iterate)(pa_mainloop *, int, int *);
	pa_context *(*context_new)(pa_mainloop_api *, const char *);
	int (*context_connect)(pa_context *, const char *, pa_context_flags_t, const pa_spawn_api *);
	void (*context_disconnect)(pa_context *);
	void (*context_unref)(pa_context *);
	pa_context_state_t (*context_get_state)(const pa_context *);
	pa_operation *(*context_get_server_info)(pa_context *, pa_server_info_cb_t, void *);
	pa_operation *(*context_get_sink_info_by_name)(pa_context *, const char *, pa_sink_info_cb_t, void *);
	void (*operation_unref)(pa_operation *);
	const char *(*proplist_gets)(const pa_proplist *, const char *);
	pa_mainloop *mainloop;
	pa_context *context;
	/* the next look at the server after it could not be reached (ms) */
	Uint64 retry_at;
} pulse;

/* what a query found: the default sink's name, then what it plays on and
its ALSA card */
struct route_query
{
	int done;
	char sink[256];
	int found;
	int card;
};

static int route_pulse_load(void)
{
	void *library;

	if (pulse.tried)
		return pulse.loaded;
	pulse.tried = 1;
	library = dlopen("libpulse.so.0", RTLD_NOW | RTLD_LOCAL);
	if (!library)
		return 0;
#define ROUTE_SYMBOL(field, name) (*(void **)&pulse.field = dlsym(library, name))
	if (!ROUTE_SYMBOL(mainloop_new, "pa_mainloop_new") || !ROUTE_SYMBOL(mainloop_free, "pa_mainloop_free") ||
		!ROUTE_SYMBOL(mainloop_get_api, "pa_mainloop_get_api") ||
		!ROUTE_SYMBOL(mainloop_iterate, "pa_mainloop_iterate") || !ROUTE_SYMBOL(context_new, "pa_context_new") ||
		!ROUTE_SYMBOL(context_connect, "pa_context_connect") ||
		!ROUTE_SYMBOL(context_disconnect, "pa_context_disconnect") ||
		!ROUTE_SYMBOL(context_unref, "pa_context_unref") ||
		!ROUTE_SYMBOL(context_get_state, "pa_context_get_state") ||
		!ROUTE_SYMBOL(context_get_server_info, "pa_context_get_server_info") ||
		!ROUTE_SYMBOL(context_get_sink_info_by_name, "pa_context_get_sink_info_by_name") ||
		!ROUTE_SYMBOL(operation_unref, "pa_operation_unref") || !ROUTE_SYMBOL(proplist_gets, "pa_proplist_gets"))
	{
		dlclose(library);
		return 0;
	}
#undef ROUTE_SYMBOL
	pulse.loaded = 1;
	return 1;
}

static void route_pulse_close(void)
{
	if (pulse.context)
	{
		pulse.context_disconnect(pulse.context);
		pulse.context_unref(pulse.context);
		pulse.context = NULL;
	}
	if (pulse.mainloop)
	{
		pulse.mainloop_free(pulse.mainloop);
		pulse.mainloop = NULL;
	}
}

/* runs the server's messages until done or the context fails, for at
most two seconds */
static int route_pulse_wait(const int *done)
{
	int round;

	for (round = 0; round < 400; round++)
	{
		pa_context_state_t state;

		pulse.mainloop_iterate(pulse.mainloop, 0, NULL);
		state = pulse.context_get_state(pulse.context);
		if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
			return 0;
		if (done ? *done : state == PA_CONTEXT_READY)
			return 1;
		SDL_Delay(5);
	}
	return 0;
}

static void route_pulse_server(pa_context *context, const pa_server_info *information, void *data)
{
	struct route_query *query = data;

	(void)context;
	if (information && information->default_sink_name)
		snprintf(query->sink, sizeof(query->sink), "%s", information->default_sink_name);
	query->done = 1;
}

static void route_pulse_sink(pa_context *context, const pa_sink_info *information, int last, void *data)
{
	struct route_query *query = data;
	const char *form, *bus, *card;

	(void)context;
	if (last || !information)
	{
		query->done = 1;
		return;
	}
	form = pulse.proplist_gets(information->proplist, PA_PROP_DEVICE_FORM_FACTOR);
	bus = pulse.proplist_gets(information->proplist, PA_PROP_DEVICE_BUS);
	card = pulse.proplist_gets(information->proplist, "alsa.card");
	query->card = card ? atoi(card) : -1;
	if (form && (!strcmp(form, "headphone") || !strcmp(form, "headset")))
	{
		query->found = 1;
		return;
	}
	/* the port it plays from: the headphones', the speakers' */
	if (information->active_port)
	{
		const pa_sink_port_info *port = information->active_port;
		int named = route_name(port->name);

#if defined(PA_CHECK_VERSION) && PA_CHECK_VERSION(14, 0, 0)
		if (port->type == PA_DEVICE_PORT_TYPE_HEADPHONES || port->type == PA_DEVICE_PORT_TYPE_HEADSET)
			named = 1;
		else if (port->type == PA_DEVICE_PORT_TYPE_SPEAKER || port->type == PA_DEVICE_PORT_TYPE_HDMI ||
			port->type == PA_DEVICE_PORT_TYPE_TV || port->type == PA_DEVICE_PORT_TYPE_LINE ||
			port->type == PA_DEVICE_PORT_TYPE_SPDIF || port->type == PA_DEVICE_PORT_TYPE_CAR ||
			port->type == PA_DEVICE_PORT_TYPE_HIFI)
			named = 0;
#endif
		if (named < 0)
			named = route_name(port->description);
		if (named >= 0)
		{
			query->found = named;
			return;
		}
	}
	if (form && (!strcmp(form, "speaker") || !strcmp(form, "tv") || !strcmp(form, "hifi") ||
		!strcmp(form, "computer") || !strcmp(form, "car") || !strcmp(form, "portable") ||
		!strcmp(form, "hands-free")))
	{
		query->found = 0;
		return;
	}
	/* (a Bluetooth sink that says nothing more: headphones, as on macOS) */
	if (bus && !strcmp(bus, "bluetooth"))
		query->found = 1;
}

/* the sound server's default sink: 1, 0, or -1 if the server says nothing
(*card: its ALSA card, or -1); 0 if there is no server */
static int route_pulse(int *found, int *card)
{
	struct route_query query;
	pa_operation *operation;

	*found = -1;
	*card = -1;
	if (!route_pulse_load())
		return 0;
	if (!pulse.context)
	{
		if (SDL_GetTicks() < pulse.retry_at)
			return 0;
		pulse.mainloop = pulse.mainloop_new();
		pulse.context = pulse.mainloop ? pulse.context_new(pulse.mainloop_get_api(pulse.mainloop), "GOAT H1") : NULL;
		if (!pulse.context || pulse.context_connect(pulse.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0 ||
			!route_pulse_wait(NULL))
		{
			/* (no server: ALSA's jacks, and the server looked for again
			in ten seconds) */
			route_pulse_close();
			pulse.retry_at = SDL_GetTicks() + 10000;
			return 0;
		}
	}
	memset(&query, 0, sizeof(query));
	query.found = -1;
	query.card = -1;
	operation = pulse.context_get_server_info(pulse.context, route_pulse_server, &query);
	if (!operation || !route_pulse_wait(&query.done))
	{
		if (operation)
			pulse.operation_unref(operation);
		route_pulse_close();
		return 0;
	}
	pulse.operation_unref(operation);
	if (!query.sink[0])
		return 1;
	query.done = 0;
	operation = pulse.context_get_sink_info_by_name(pulse.context, query.sink, route_pulse_sink, &query);
	if (!operation || !route_pulse_wait(&query.done))
	{
		if (operation)
			pulse.operation_unref(operation);
		route_pulse_close();
		return 0;
	}
	pulse.operation_unref(operation);
	*found = query.found;
	*card = query.card;
	return 1;
}

#endif

static int route_detect(void)
{
#ifdef ROUTE_PULSE
	int found, card;

	if (route_pulse(&found, &card))
	{
		/* (a sink of a card whose port says nothing: its jack, as on the
		Steam Deck) */
#ifdef ROUTE_ALSA
		if (found < 0 && card >= 0)
			found = route_alsa_jack(card);
#endif
		return found;
	}
#endif
	return route_alsa_any();
}

#endif

/* ---------- the thread */

static void *route_thread(void *parameter)
{
	(void)parameter;
	for (;;)
	{
		int found = route_detect();

		if (found != __atomic_load_n(&route_found, __ATOMIC_ACQUIRE))
		{
			host_logf(HOST_LOG_INFO, "audio: the system's sound plays on %s",
				found > 0 ? "headphones" : found == 0 ? "speakers" : "a device not known");
			__atomic_store_n(&route_found, found, __ATOMIC_RELEASE);
		}
		SDL_Delay(ROUTE_PERIOD);
	}
	return NULL;
}

/* (the guest, each frame: port/linux/src/dsound_sdl.c DirectSoundDoWork)
1 headphones, 0 speakers, -1 not known yet or at all; the first call starts
the thread that finds out */
int host_audio_headphones(void)
{
	if (!__atomic_exchange_n(&route_started, 1, __ATOMIC_ACQ_REL))
	{
		pthread_t thread;
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&thread, &attributes, route_thread, NULL))
			host_logf(HOST_LOG_WARN, "audio: cannot start the thread that finds the headphones");
		pthread_attr_destroy(&attributes);
	}
	return __atomic_load_n(&route_found, __ATOMIC_ACQUIRE);
}
