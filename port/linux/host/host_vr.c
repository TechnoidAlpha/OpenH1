/*
HOST_VR.C

OpenXR for the VR mode (HALO_VR; port/linux/src/vr_host.h, port/linux/src/
vr.c). The renderer is Vulkan's, in the host (port/linux/vulkan): the
runtime creates the Vulkan instance and device through
XR_KHR_vulkan_enable2 and owns the swapchains' VkImages. These handles stay
on the host (host_vr_vulkan.h), outside the guest's ABI.

The runtime is loaded without the OpenXR loader, which few distributions
package: as the loader does, from the active runtime's manifest
($XDG_CONFIG_HOME/openxr/1/active_runtime.json, then /etc/xdg), through
xrNegotiateLoaderRuntimeInterface. A libopenxr_loader.so.1 next to the
executable or on the system is used instead where there is one, for its API
layers. The Vulkan binding has been tested with Monado on Linux.
*/

#define XR_USE_GRAPHICS_API_VULKAN
#include "host_vr_vulkan.h"
#define XR_NO_PROTOTYPES

#include "host.h"
#include "host_load_trace.h"
#include "host_render_stream.h"
#include "host_vr_render.h"
#include "vr_host.h"

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

/* ---------- the runtime's functions */

#define XR_FUNCTIONS(X) \
	X(xrEnumerateInstanceExtensionProperties) \
	X(xrCreateInstance) \
	X(xrDestroyInstance) \
	X(xrGetInstanceProperties) \
	X(xrGetSystem) \
	X(xrGetSystemProperties) \
	X(xrEnumerateViewConfigurationViews) \
	X(xrCreateSession) \
	X(xrDestroySession) \
	X(xrBeginSession) \
	X(xrEndSession) \
	X(xrPollEvent) \
	X(xrResultToString) \
	X(xrCreateReferenceSpace) \
	X(xrLocateSpace) \
	X(xrLocateViews) \
	X(xrEnumerateSwapchainFormats) \
	X(xrCreateSwapchain) \
	X(xrDestroySwapchain) \
	X(xrEnumerateSwapchainImages) \
	X(xrAcquireSwapchainImage) \
	X(xrWaitSwapchainImage) \
	X(xrReleaseSwapchainImage) \
	X(xrWaitFrame) \
	X(xrBeginFrame) \
	X(xrEndFrame) \
	X(xrStringToPath) \
	X(xrCreateActionSet) \
	X(xrCreateAction) \
	X(xrSuggestInteractionProfileBindings) \
	X(xrAttachSessionActionSets) \
	X(xrCreateActionSpace) \
	X(xrSyncActions) \
	X(xrGetActionStateBoolean) \
	X(xrGetActionStateFloat) \
	X(xrGetActionStateVector2f) \
	X(xrGetActionStatePose) \
	X(xrApplyHapticFeedback) \
	X(xrEnumerateBoundSourcesForAction) \
	X(xrGetInputSourceLocalizedName)

#define XR_DECLARE(name) static PFN_##name name;
XR_FUNCTIONS(XR_DECLARE)
#undef XR_DECLARE
static PFN_xrGetInstanceProcAddr get_instance_proc_address;
static PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR;
static PFN_xrCreateVulkanInstanceKHR xrCreateVulkanInstanceKHR;
static PFN_xrGetVulkanGraphicsDevice2KHR xrGetVulkanGraphicsDevice2KHR;
static PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR;

static struct
{
	XrGraphicsRequirementsVulkan2KHR requirements;
	XrGraphicsBindingVulkan2KHR binding;
	XrSwapchainImageVulkan2KHR *images[VR_SWAPCHAIN_COUNT];
	VkFormat formats[VR_SWAPCHAIN_COUNT];
	VkFormat depth_format;
	int prepared;
} vulkan;

/* the actions (action_names, bindings) */
enum
{
	_action_a, _action_b, _action_x, _action_y, _action_menu, _action_view,
	_action_dpad_up, _action_dpad_down, _action_dpad_left, _action_dpad_right,
	_action_left_stick_click, _action_right_stick_click, _action_left_bumper, _action_right_bumper,
	_action_right_trigger_touch, _action_left_trigger_touch,
	NUMBER_OF_BUTTON_ACTIONS,
	_action_left_stick = NUMBER_OF_BUTTON_ACTIONS, _action_right_stick,
	_action_left_trigger, _action_right_trigger, _action_left_grip, _action_right_grip,
	_action_aim_pose, _action_grip_pose, _action_haptic,
	NUMBER_OF_ACTIONS
};

static struct
{
	void *library;
	XrInstance instance;
	XrSystemId system;
	XrSession session;
	XrSessionState state;
	int running;
	int frame_begun;
	XrFrameState frame;
	XrTime last_display_time;
	XrSpace play_space;
	XrSpace view_space;
	XrSwapchain swapchains[VR_SWAPCHAIN_COUNT];
	int swapchain_width[VR_SWAPCHAIN_COUNT];
	int swapchain_height[VR_SWAPCHAIN_COUNT];
	int acquired[VR_SWAPCHAIN_COUNT];
	/* An acquired image is not usable until xrWaitSwapchainImage succeeds. */
	int waiting[VR_SWAPCHAIN_COUNT];
	XrActionSet action_set;
	XrAction actions[NUMBER_OF_ACTIONS];
	XrPath hands[2];
	XrSpace aim_spaces[2];
	XrSpace grip_spaces[2];
	/* host_vr_probe's: done, and the binding it chose (VR_BINDING_*) */
	int probed;
	int binding;
	int has_frame_controller;
	int has_refresh_rate;
	int want_depth, has_depth;
	int has_cylinder;
	/* the eyes' gaze (XR_EXT_eye_gaze_interaction), for the foveation
	(vr.foveation = "eye"): asked for, the runtime has the extension, its
	system tracks the eyes; its action and space, and whether it was ever
	tracked (logged once) */
	int want_eye_gaze, has_eye_gaze;
	XrAction gaze_action;
	XrSpace gaze_space;
	int gaze_tracked;
	/* the session's images and sizes (host_vr_info) */
	struct vr_host_info info;
	int info_made;
} vr;

/* OpenXR's calls on the render thread
(host_vr_render.h): a frame has been waited for (vr.frame is one's: the
views are located for it: located); the frames waited for on the game's thread and
not yet begun on the render thread (their states, oldest first), and the
one begun */
#define WAITED_FRAMES 4

static struct
{
	int on;
	int located;
	pthread_mutex_t lock;
	XrFrameState waited[WAITED_FRAMES];
	int first, count;
	XrFrameState begun;
} render_thread = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* The render owner begins and ends loading frames. A producer waits for
the next runtime ticket without blocking preparation on the render owner.
The game receives a snapshot; its simulation clock never advances here. */
static struct
{
	int active, suspended, presenting, anchored, ready;
	pthread_t owner;
	pthread_t waiter;
	pthread_cond_t changed;
	int waiter_started, waiter_stop, waiter_request, waiter_busy, waiter_failed;
	struct vr_host_views views;
	struct vr_host_pose anchor;
	/* the stereo pair's window: before the head as the load began, level,
	facing it (loading_presentation.h), and whether this frame is the pair,
	the tangent of half the angle it is wide from its distance */
	struct vr_host_pose window;
	int stereo;
	float stereo_tangent;
} loading = { .changed = PTHREAD_COND_INITIALIZER };

static void loading_wait_request(void);
static void loading_wait_stop(void);

static int loading_guest_views(struct vr_host_views *views)
{
	int copied = 0;

	pthread_mutex_lock(&render_thread.lock);
	if (loading.active && !pthread_equal(pthread_self(), loading.owner))
	{
		*views = loading.views;
		views->display_elapsed = 0.0f;
		copied = 1;
	}
	pthread_mutex_unlock(&render_thread.lock);
	return copied;
}

static const char *result_name(XrResult result, char text[XR_MAX_RESULT_STRING_SIZE])
{
	if (vr.instance && xrResultToString && xrResultToString(vr.instance, result, text) == XR_SUCCESS)
		return text;
	snprintf(text, XR_MAX_RESULT_STRING_SIZE, "XrResult %d", (int)result);
	return text;
}

static int check(XrResult result, const char *what)
{
	char text[XR_MAX_RESULT_STRING_SIZE];

	if (XR_SUCCEEDED(result))
		return 1;
	host_logf(HOST_LOG_ERROR, "vr: %s: %s", what, result_name(result, text));
	return 0;
}

/* ---------- loading the runtime */

/* the manifest's library_path, made absolute from its folder */
static int manifest_library(const char *manifest, char *library, size_t size)
{
	char real[PATH_MAX], text[8192], *key, *value, *end, *slash;
	FILE *file;
	size_t length;

	if (!realpath(manifest, real) || !(file = fopen(real, "rb")))
		return 0;
	length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = 0;
	if (!(key = strstr(text, "\"library_path\"")) || !(value = strchr(key + 14, ':')) ||
		!(value = strchr(value, '"')) || !(end = strchr(++value, '"')))
	{
		return 0;
	}
	*end = 0;
	if (value[0] == '/')
	{
		snprintf(library, size, "%s", value);
	}
	else
	{
		slash = strrchr(real, '/');
		if (slash)
			*slash = 0;
		snprintf(library, size, "%s/%s", real, value);
	}
	return 1;
}

static int runtime_load(void)
{
	char manifest[PATH_MAX], library[PATH_MAX];
	const char *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME"), *override = getenv("XR_RUNTIME_JSON");
	PFN_xrNegotiateLoaderRuntimeInterface negotiate;
	XrNegotiateLoaderInfo loader = { 0 };
	XrNegotiateRuntimeRequest request = { 0 };

	if (get_instance_proc_address)
		return 1;
	/* the loader, where there is one */
	vr.library = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
	if (vr.library)
	{
		get_instance_proc_address = (PFN_xrGetInstanceProcAddr)dlsym(vr.library, "xrGetInstanceProcAddr");
		if (get_instance_proc_address)
		{
			host_logf(HOST_LOG_INFO, "vr: the OpenXR loader libopenxr_loader.so.1");
			return 1;
		}
		dlclose(vr.library);
	}
	/* else the active runtime itself */
	if (override && *override)
		snprintf(manifest, sizeof(manifest), "%s", override);
	else if (config && *config)
		snprintf(manifest, sizeof(manifest), "%s/openxr/1/active_runtime.json", config);
	else
		snprintf(manifest, sizeof(manifest), "%s/.config/openxr/1/active_runtime.json", home ? home : "");
	if (!manifest_library(manifest, library, sizeof(library)))
	{
		snprintf(manifest, sizeof(manifest), "/etc/xdg/openxr/1/active_runtime.json");
		if (!manifest_library(manifest, library, sizeof(library)))
		{
			host_logf(HOST_LOG_WARN, "vr: no active OpenXR runtime");
			return 0;
		}
	}
	vr.library = dlopen(library, RTLD_NOW | RTLD_LOCAL);
	if (!vr.library)
	{
		host_logf(HOST_LOG_WARN, "vr: cannot load the OpenXR runtime %s: %s", library, dlerror());
		return 0;
	}
	negotiate = (PFN_xrNegotiateLoaderRuntimeInterface)dlsym(vr.library, "xrNegotiateLoaderRuntimeInterface");
	if (!negotiate)
		return 0;
	loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
	loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
	loader.structSize = sizeof(loader);
	loader.minInterfaceVersion = 1;
	loader.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
	loader.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
	loader.maxApiVersion = XR_MAKE_VERSION(1, 1, 0xffffffff);
	request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
	request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
	request.structSize = sizeof(request);
	if (negotiate(&loader, &request) != XR_SUCCESS || !request.getInstanceProcAddr)
	{
		host_logf(HOST_LOG_WARN, "vr: the OpenXR runtime %s refused the negotiation", library);
		return 0;
	}
	get_instance_proc_address = request.getInstanceProcAddr;
	host_logf(HOST_LOG_INFO, "vr: the OpenXR runtime %s (from %s)", library, manifest);
	return 1;
}

static int functions_load(XrInstance instance)
{
	int success = 1;

#define XR_LOAD(name) \
	if (get_instance_proc_address(instance, #name, (PFN_xrVoidFunction *)&name) != XR_SUCCESS || !name) \
	{ \
		if (instance) \
		{ \
			host_logf(HOST_LOG_ERROR, "vr: no %s", #name); \
			success = 0; \
		} \
	}
	XR_FUNCTIONS(XR_LOAD)
#undef XR_LOAD
	return success;
}

/* the functions there are before an instance (the others the loader
reports as errors when asked for without one) */
static void global_functions_load(void)
{
	get_instance_proc_address(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
		(PFN_xrVoidFunction *)&xrEnumerateInstanceExtensionProperties);
	get_instance_proc_address(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&xrCreateInstance);
}

/* ---------- the instance and session */

static int extension_listed(const XrExtensionProperties *extensions, uint32_t count, const char *name)
{
	uint32_t index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(extensions[index].extensionName, name))
			return 1;
	}
	return 0;
}

/* the binding the session will take (host_vr_probe) */
static int binding_choose(void)
{
	XrExtensionProperties *extensions;
	uint32_t count = 0, index;
	int enabled;

	if (!runtime_load())
		return VR_BINDING_NONE;
	global_functions_load();
	if (!xrEnumerateInstanceExtensionProperties ||
		xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL) != XR_SUCCESS ||
		!(extensions = calloc(count ? count : 1, sizeof(*extensions))))
	{
		host_logf(HOST_LOG_ERROR, "vr: the OpenXR runtime cannot list its extensions");
		return VR_BINDING_NONE;
	}
	for (index = 0; index < count; index++)
		extensions[index].type = XR_TYPE_EXTENSION_PROPERTIES;
	if (xrEnumerateInstanceExtensionProperties(NULL, count, &count, extensions) != XR_SUCCESS)
		count = 0;
	enabled = extension_listed(extensions, count, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
	free(extensions);
	if (!enabled)
	{
		host_logf(HOST_LOG_ERROR, "vr: the OpenXR runtime has no Vulkan binding (XR_KHR_vulkan_enable2)");
		return VR_BINDING_NONE;
	}
	host_logf(HOST_LOG_INFO, "vr: the runtime takes Vulkan (XR_KHR_vulkan_enable2)");
	return VR_BINDING_VULKAN;
}

int host_vr_probe(void)
{
	if (!vr.probed)
	{
		vr.binding = binding_choose();
		vr.probed = 1;
	}
	return vr.binding;
}

static int instance_create(void)
{
	XrExtensionProperties *extensions;
	uint32_t count = 0, index;
	const char *enabled[8];
	uint32_t enabled_count = 0;
	XrInstanceCreateInfo create = { XR_TYPE_INSTANCE_CREATE_INFO };
	XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };

	if (vr.instance)
		return 1;
	global_functions_load();
	if (!xrEnumerateInstanceExtensionProperties || !xrCreateInstance ||
		xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL) != XR_SUCCESS)
	{
		return 0;
	}
	extensions = calloc(count ? count : 1, sizeof(*extensions));
	if (!extensions)
		return 0;
	for (index = 0; index < count; index++)
		extensions[index].type = XR_TYPE_EXTENSION_PROPERTIES;
	if (!check(xrEnumerateInstanceExtensionProperties(NULL, count, &count, extensions),
		"xrEnumerateInstanceExtensionProperties"))
	{
		free(extensions);
		return 0;
	}
	/* every extension the runtime offers, with its version, on one line:
	what a headset's runtime has (foveation, eye tracking, layers) is read
	from its log, where the runtime's own documentation says less */
	{
		size_t size = 64, at = 0;
		char *text;

		for (index = 0; index < count; index++)
			size += strlen(extensions[index].extensionName) + 16;
		if ((text = malloc(size)) != NULL)
		{
			text[0] = 0;
			for (index = 0; index < count; index++)
				at += (size_t)snprintf(text + at, size - at, "%s%s %u", index ? ", " : "",
					extensions[index].extensionName, (unsigned)extensions[index].extensionVersion);
			host_logf(HOST_LOG_INFO, "vr: the runtime's extensions (%u): %s", (unsigned)count, text);
			free(text);
		}
	}
	if (!extension_listed(extensions, count, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
	{
		free(extensions);
		return 0;
	}
	enabled[enabled_count++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
	if (extension_listed(extensions, count, "XR_VALVE_frame_controller_interaction"))
	{
		enabled[enabled_count++] = "XR_VALVE_frame_controller_interaction";
		vr.has_frame_controller = 1;
	}
	if (extension_listed(extensions, count, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		enabled[enabled_count++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
		vr.has_refresh_rate = 1;
	}
	if (vr.want_depth && extension_listed(extensions, count, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME))
	{
		enabled[enabled_count++] = XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME;
		vr.has_depth = 1;
	}
	/* the HUD on a cylinder about the head, where the runtime has them
	(Monado; not SteamVR 2.18 on the Steam Frame, whose HUD is then a quad
	with the curve in its image: vr_visor.c). HALO_VR_NO_CYLINDER=1 does
	without, to test that on a runtime that has them. */
	if (extension_listed(extensions, count, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) &&
		!getenv("HALO_VR_NO_CYLINDER"))
	{
		enabled[enabled_count++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
		vr.has_cylinder = 1;
	}
	if (vr.want_eye_gaze && extension_listed(extensions, count, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME))
	{
		enabled[enabled_count++] = XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME;
		vr.has_eye_gaze = 1;
	}
	free(extensions);

	strcpy(create.applicationInfo.applicationName, "Halo: Combat Evolved");
	create.applicationInfo.applicationVersion = 1;
	strcpy(create.applicationInfo.engineName, "halo-ce-universal");
	create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	create.enabledExtensionCount = enabled_count;
	create.enabledExtensionNames = enabled;
	if (!check(xrCreateInstance(&create, &vr.instance), "xrCreateInstance"))
		return 0;
	if (!functions_load(vr.instance))
		return 0;
#define XR_VULKAN_LOAD(name) \
	if (get_instance_proc_address(vr.instance, #name, (PFN_xrVoidFunction *)&name) != XR_SUCCESS || !name) \
	{ \
		host_logf(HOST_LOG_ERROR, "vr: no %s", #name); \
		return 0; \
	}
	XR_VULKAN_LOAD(xrGetVulkanGraphicsRequirements2KHR)
	XR_VULKAN_LOAD(xrCreateVulkanInstanceKHR)
	XR_VULKAN_LOAD(xrGetVulkanGraphicsDevice2KHR)
	XR_VULKAN_LOAD(xrCreateVulkanDeviceKHR)
#undef XR_VULKAN_LOAD
	if (xrGetInstanceProperties(vr.instance, &properties) == XR_SUCCESS)
	{
		host_logf(HOST_LOG_INFO, "vr: %s %u.%u.%u", properties.runtimeName,
			(unsigned)XR_VERSION_MAJOR(properties.runtimeVersion), (unsigned)XR_VERSION_MINOR(properties.runtimeVersion),
			(unsigned)XR_VERSION_PATCH(properties.runtimeVersion));
	}
	return 1;
}

/* Vulkan's device exists before the guest asks to create the session. */
static int session_create(void)
{
	XrSessionCreateInfo create = { XR_TYPE_SESSION_CREATE_INFO };

	if (!vulkan.prepared || !vulkan.binding.device)
	{
		host_logf(HOST_LOG_ERROR, "vr: the Vulkan device was not bound before the session");
		return 0;
	}
	create.next = &vulkan.binding;
	create.systemId = vr.system;
	return check(xrCreateSession(vr.instance, &create, &vr.session), "xrCreateSession (Vulkan)");
}

/* the session's swapchain formats, as many as the runtime has (the two-call
idiom), allocated; NULL if there are none or they cannot be read (logged),
their count in count */
static int64_t *swapchain_formats(uint32_t *count)
{
	int64_t *formats;
	uint32_t capacity = 0;

	*count = 0;
	if (!check(xrEnumerateSwapchainFormats(vr.session, 0, &capacity, NULL), "xrEnumerateSwapchainFormats (count)") ||
		!capacity || capacity > 4096)
		return NULL;
	formats = calloc(capacity, sizeof(*formats));
	if (!formats)
		return NULL;
	if (!check(xrEnumerateSwapchainFormats(vr.session, capacity, count, formats), "xrEnumerateSwapchainFormats") ||
		*count > capacity)
	{
		free(formats);
		*count = 0;
		return NULL;
	}
	return formats;
}

static int swapchain_create(int which, int width, int height, int layers, struct vr_host_info *info)
{
	int64_t *formats;
	uint32_t format_count = 0, format_index;
	XrSwapchainCreateInfo create = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	uint32_t count = 0, index;

	/* Request both attachment and transfer usage: the backend can draw
	directly or copy its resolved targets into the runtime's images. */
	{
		/* Encoded game values are copied unchanged. An UNORM runtime image
		would treat those values as linear light, so require sRGB. */
		static const int64_t color_formats[] = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB };
		int preference;

		create.usageFlags = which == VR_SWAPCHAIN_DEPTH ? XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
			XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
		create.usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		formats = swapchain_formats(&format_count);
		if (!formats)
			return 0;
		for (preference = 1; preference >= 0; preference--)
		{
			for (format_index = 0; format_index < format_count; format_index++)
			{
				if (formats[format_index] == color_formats[preference])
					create.format = formats[format_index];
			}
		}
		if (which == VR_SWAPCHAIN_DEPTH)
		{
			create.format = 0;
			for (format_index = 0; format_index < format_count; format_index++)
			{
				if (formats[format_index] == vulkan.depth_format)
					create.format = formats[format_index];
			}
		}
		free(formats);
		if (!create.format)
		{
			host_logf(HOST_LOG_ERROR, "vr: no supported Vulkan %s swapchain format",
				which == VR_SWAPCHAIN_DEPTH ? "depth" : "color");
			return 0;
		}
	}
	create.sampleCount = 1;
	create.width = (uint32_t)width;
	create.height = (uint32_t)height;
	create.faceCount = 1;
	create.arraySize = (uint32_t)layers;
	create.mipCount = 1;
	if (!check(xrCreateSwapchain(vr.session, &create, &vr.swapchains[which]), "xrCreateSwapchain"))
		return 0;
	if (!check(xrEnumerateSwapchainImages(vr.swapchains[which], 0, &count, NULL),
		"xrEnumerateSwapchainImages (count)") || !count || count > INT_MAX)
		goto failed;
	vulkan.images[which] = calloc(count, sizeof(*vulkan.images[which]));
	if (!vulkan.images[which])
		goto failed;
	for (index = 0; index < count; index++)
		vulkan.images[which][index].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
	if (!check(xrEnumerateSwapchainImages(vr.swapchains[which], count, &count,
		(XrSwapchainImageBaseHeader *)vulkan.images[which]), "xrEnumerateSwapchainImages (Vulkan)"))
		goto failed;
	vulkan.formats[which] = (VkFormat)create.format;
	info->image_count[which] = (int)count;
	/* The guest sends swapchain indices, never Vulkan handles. */
	memset(info->images[which], 0, sizeof(info->images[which]));
	vr.swapchain_width[which] = width;
	vr.swapchain_height[which] = height;
	vr.acquired[which] = -1;
	return 1;
failed:
	free(vulkan.images[which]);
	vulkan.images[which] = NULL;
	xrDestroySwapchain(vr.swapchains[which]);
	vr.swapchains[which] = XR_NULL_HANDLE;
	return 0;
}

/* ---------- the controllers */

static const char *const action_names[NUMBER_OF_ACTIONS] =
{
	"a", "b", "x", "y", "menu", "view", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
	"left_stick_click", "right_stick_click", "left_bumper", "right_bumper", "right_trigger_touch",
	"left_trigger_touch", "left_stick", "right_stick", "left_trigger", "right_trigger", "left_grip", "right_grip",
	"aim_pose", "grip_pose", "haptic",
};

struct binding
{
	int action;
	const char *path;
};

/* the Steam Frame's controllers: a gamepad in two halves (the d-pad and
View on the left, A, B, X, Y and Menu on the right); their triggers sense a
finger on them (touch), as the Touch's do */
static const struct binding frame_controller_bindings[] =
{
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/right/input/x/click" },
	{ _action_y, "/user/hand/right/input/y/click" },
	{ _action_menu, "/user/hand/right/input/menu/click" },
	{ _action_view, "/user/hand/left/input/view/click" },
	{ _action_dpad_up, "/user/hand/left/input/dpad_up/click" },
	{ _action_dpad_down, "/user/hand/left/input/dpad_down/click" },
	{ _action_dpad_left, "/user/hand/left/input/dpad_left/click" },
	{ _action_dpad_right, "/user/hand/left/input/dpad_right/click" },
	{ _action_left_stick_click, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_stick_click, "/user/hand/right/input/thumbstick/click" },
	{ _action_left_bumper, "/user/hand/left/input/bumper/click" },
	{ _action_right_bumper, "/user/hand/right/input/bumper/click" },
	{ _action_left_stick, "/user/hand/left/input/thumbstick" },
	{ _action_right_stick, "/user/hand/right/input/thumbstick" },
	{ _action_left_trigger, "/user/hand/left/input/trigger/value" },
	{ _action_right_trigger, "/user/hand/right/input/trigger/value" },
	{ _action_right_trigger_touch, "/user/hand/right/input/trigger/touch" },
	{ _action_left_trigger_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_left_grip, "/user/hand/left/input/squeeze/value" },
	{ _action_right_grip, "/user/hand/right/input/squeeze/value" },
	{ _action_aim_pose, "/user/hand/left/input/aim/pose" },
	{ _action_aim_pose, "/user/hand/right/input/aim/pose" },
	{ _action_grip_pose, "/user/hand/left/input/grip/pose" },
	{ _action_grip_pose, "/user/hand/right/input/grip/pose" },
	{ _action_haptic, "/user/hand/left/output/haptic" },
	{ _action_haptic, "/user/hand/right/output/haptic" },
};

/* Touch-style controllers (what SteamVR maps other controllers onto): X
and Y on the left, the grips as the bumpers, no d-pad or View */
static const struct binding touch_bindings[] =
{
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/left/input/x/click" },
	{ _action_y, "/user/hand/left/input/y/click" },
	{ _action_menu, "/user/hand/left/input/menu/click" },
	{ _action_left_stick_click, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_stick_click, "/user/hand/right/input/thumbstick/click" },
	{ _action_left_bumper, "/user/hand/left/input/squeeze/value" },
	{ _action_right_bumper, "/user/hand/right/input/squeeze/value" },
	{ _action_left_stick, "/user/hand/left/input/thumbstick" },
	{ _action_right_stick, "/user/hand/right/input/thumbstick" },
	{ _action_left_trigger, "/user/hand/left/input/trigger/value" },
	{ _action_right_trigger, "/user/hand/right/input/trigger/value" },
	{ _action_right_trigger_touch, "/user/hand/right/input/trigger/touch" },
	{ _action_left_trigger_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_aim_pose, "/user/hand/left/input/aim/pose" },
	{ _action_aim_pose, "/user/hand/right/input/aim/pose" },
	{ _action_grip_pose, "/user/hand/left/input/grip/pose" },
	{ _action_grip_pose, "/user/hand/right/input/grip/pose" },
	{ _action_haptic, "/user/hand/left/output/haptic" },
	{ _action_haptic, "/user/hand/right/output/haptic" },
};

static void bindings_suggest(const char *profile, const struct binding *bindings, int count)
{
	XrActionSuggestedBinding suggested[32];
	XrInteractionProfileSuggestedBinding suggestion = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	XrPath profile_path;
	XrResult result;
	int index;

	if (xrStringToPath(vr.instance, profile, &profile_path) != XR_SUCCESS)
		return;
	for (index = 0; index < count && index < 32; index++)
	{
		suggested[index].action = vr.actions[bindings[index].action];
		xrStringToPath(vr.instance, bindings[index].path, &suggested[index].binding);
	}
	suggestion.interactionProfile = profile_path;
	suggestion.countSuggestedBindings = (uint32_t)index;
	suggestion.suggestedBindings = suggested;
	result = xrSuggestInteractionProfileBindings(vr.instance, &suggestion);
	if (result == XR_ERROR_PATH_UNSUPPORTED)
	{
		/* a path the runtime's profile lacks spoils all of them: find it
		(each alone) and suggest the rest; a profile the runtime has none
		of is said once */
		int kept = 0, lacking[32], lacked = 0;

		count = index;
		for (index = 0; index < count; index++)
		{
			suggestion.countSuggestedBindings = 1;
			suggestion.suggestedBindings = &suggested[index];
			if (xrSuggestInteractionProfileBindings(vr.instance, &suggestion) == XR_SUCCESS)
				suggested[kept++] = suggested[index];
			else
				lacking[lacked++] = index;
		}
		if (!kept)
		{
			host_logf(HOST_LOG_INFO, "vr: the runtime has no %s", profile);
			return;
		}
		for (index = 0; index < lacked; index++)
			host_logf(HOST_LOG_WARN, "vr: %s has no %s", profile, bindings[lacking[index]].path);
		suggestion.countSuggestedBindings = (uint32_t)kept;
		suggestion.suggestedBindings = suggested;
		result = xrSuggestInteractionProfileBindings(vr.instance, &suggestion);
	}
	if (result != XR_SUCCESS)
	{
		char text[XR_MAX_RESULT_STRING_SIZE];

		host_logf(HOST_LOG_WARN, "vr: bindings for %s: %s", profile, result_name(result, text));
	}
}

static int actions_create(void)
{
	XrActionSetCreateInfo set = { XR_TYPE_ACTION_SET_CREATE_INFO };
	XrSessionActionSetsAttachInfo attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	int index, hand;

	strcpy(set.actionSetName, "gameplay");
	strcpy(set.localizedActionSetName, "Gameplay");
	if (!check(xrCreateActionSet(vr.instance, &set, &vr.action_set), "xrCreateActionSet"))
		return 0;
	xrStringToPath(vr.instance, "/user/hand/left", &vr.hands[0]);
	xrStringToPath(vr.instance, "/user/hand/right", &vr.hands[1]);
	for (index = 0; index < NUMBER_OF_ACTIONS; index++)
	{
		XrActionCreateInfo action = { XR_TYPE_ACTION_CREATE_INFO };

		action.actionType = index < NUMBER_OF_BUTTON_ACTIONS ? XR_ACTION_TYPE_BOOLEAN_INPUT :
			index <= _action_right_stick ? XR_ACTION_TYPE_VECTOR2F_INPUT :
			index <= _action_right_grip ? XR_ACTION_TYPE_FLOAT_INPUT :
			index == _action_haptic ? XR_ACTION_TYPE_VIBRATION_OUTPUT : XR_ACTION_TYPE_POSE_INPUT;
		if (index >= _action_aim_pose)
		{
			action.countSubactionPaths = 2;
			action.subactionPaths = vr.hands;
		}
		strcpy(action.actionName, action_names[index]);
		strcpy(action.localizedActionName, action_names[index]);
		if (!check(xrCreateAction(vr.action_set, &action, &vr.actions[index]), "xrCreateAction"))
			return 0;
	}
	/* (booleans bound to the Touch squeeze's value, a float, are pressed
	past the runtime's threshold. The Frame's profile as Valve documents it,
	frame_controller_valve, and as this build first named it, which a
	runtime without it refuses) */
	if (vr.has_frame_controller)
	{
		bindings_suggest("/interaction_profiles/valve/frame_controller_valve", frame_controller_bindings,
			(int)(sizeof(frame_controller_bindings) / sizeof(frame_controller_bindings[0])));
		bindings_suggest("/interaction_profiles/valve/frame_controller", frame_controller_bindings,
			(int)(sizeof(frame_controller_bindings) / sizeof(frame_controller_bindings[0])));
	}
	bindings_suggest("/interaction_profiles/oculus/touch_controller", touch_bindings,
		(int)(sizeof(touch_bindings) / sizeof(touch_bindings[0])));
	/* the eyes' gaze, a pose of its own profile's */
	if (vr.has_eye_gaze)
	{
		XrActionCreateInfo action = { XR_TYPE_ACTION_CREATE_INFO };
		XrActionSuggestedBinding suggested = { 0 };
		XrInteractionProfileSuggestedBinding suggestion = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };

		action.actionType = XR_ACTION_TYPE_POSE_INPUT;
		strcpy(action.actionName, "eye_gaze");
		strcpy(action.localizedActionName, "Eye gaze");
		if (xrCreateAction(vr.action_set, &action, &vr.gaze_action) == XR_SUCCESS &&
			xrStringToPath(vr.instance, "/interaction_profiles/ext/eye_gaze_interaction",
				&suggestion.interactionProfile) == XR_SUCCESS &&
			xrStringToPath(vr.instance, "/user/eyes_ext/input/gaze_ext/pose", &suggested.binding) == XR_SUCCESS)
		{
			suggested.action = vr.gaze_action;
			suggestion.countSuggestedBindings = 1;
			suggestion.suggestedBindings = &suggested;
			if (!check(xrSuggestInteractionProfileBindings(vr.instance, &suggestion), "the eyes' gaze's binding"))
				vr.has_eye_gaze = 0;
		}
		else
		{
			vr.has_eye_gaze = 0;
		}
	}
	attach.countActionSets = 1;
	attach.actionSets = &vr.action_set;
	if (!check(xrAttachSessionActionSets(vr.session, &attach), "xrAttachSessionActionSets"))
		return 0;
	for (hand = 0; hand < 2; hand++)
	{
		XrActionSpaceCreateInfo space = { XR_TYPE_ACTION_SPACE_CREATE_INFO };

		space.subactionPath = vr.hands[hand];
		space.poseInActionSpace.orientation.w = 1.0f;
		space.action = vr.actions[_action_aim_pose];
		xrCreateActionSpace(vr.session, &space, &vr.aim_spaces[hand]);
		space.action = vr.actions[_action_grip_pose];
		xrCreateActionSpace(vr.session, &space, &vr.grip_spaces[hand]);
	}
	if (vr.has_eye_gaze)
	{
		XrActionSpaceCreateInfo space = { XR_TYPE_ACTION_SPACE_CREATE_INFO };

		space.action = vr.gaze_action;
		space.poseInActionSpace.orientation.w = 1.0f;
		if (!check(xrCreateActionSpace(vr.session, &space, &vr.gaze_space), "the eyes' gaze's space"))
			vr.has_eye_gaze = 0;
	}
	return 1;
}

/* ---------- timing (HALO_VR_TIMING: what the runtime's calls cost) */

static struct
{
	int enabled;
	double acquire, release, end, frames;
} timing = { -1 };

static double seconds_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static void timing_frame(void)
{
	if (++timing.frames >= 300)
	{
		host_logf(HOST_LOG_INFO, "vr: runtime calls a frame: acquire %.2f ms, release %.2f ms, end %.2f ms",
			timing.acquire * 1000.0 / timing.frames, timing.release * 1000.0 / timing.frames,
			timing.end * 1000.0 / timing.frames);
		timing.acquire = timing.release = timing.end = timing.frames = 0.0;
	}
}

/* ---------- the public functions */

static int initialize(struct vr_host_info *info)
{
	XrViewConfigurationView views[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
	XrSystemProperties properties = { XR_TYPE_SYSTEM_PROPERTIES };
	XrSystemEyeGazeInteractionPropertiesEXT eye_gaze = { XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
	XrReferenceSpaceCreateInfo space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	uint32_t count = 0;
	int width, height;

	if (vr.info_made)
	{
		*info = vr.info;
		return 1;
	}
	if (vr.session)
		return 0;
	vr.want_depth = info->depth;
	vr.want_eye_gaze = info->eye_gaze;
	if (!host_vr_probe() || !instance_create() || !session_create())
		return 0;
	if (vr.has_eye_gaze)
		properties.next = &eye_gaze;
	if (xrGetSystemProperties(vr.instance, vr.system, &properties) == XR_SUCCESS)
		snprintf(info->system_name, sizeof(info->system_name), "%s", properties.systemName);
	if (vr.has_eye_gaze)
	{
		/* (a runtime may have the extension for a headset without eye
		tracking: its system says) */
		host_logf(HOST_LOG_INFO, "vr: the eyes' gaze (XR_EXT_eye_gaze_interaction): %s",
			eye_gaze.supportsEyeGazeInteraction ? "the system tracks it" : "the system does not track it");
		vr.has_eye_gaze = vr.want_eye_gaze && eye_gaze.supportsEyeGazeInteraction != XR_FALSE;
	}
	else if (vr.want_eye_gaze)
	{
		host_logf(HOST_LOG_INFO, "vr: the runtime has no eyes' gaze (XR_EXT_eye_gaze_interaction)");
	}
	if (!check(xrEnumerateViewConfigurationViews(vr.instance, vr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		2, &count, views), "xrEnumerateViewConfigurationViews"))
	{
		return 0;
	}
	info->recommended_width = (int)views[0].recommendedImageRectWidth;
	info->recommended_height = (int)views[0].recommendedImageRectHeight;
	width = info->eye_width > 0 ? info->eye_width :
		(int)(views[0].recommendedImageRectWidth * (info->scale > 0.0f ? info->scale : 1.0f) + 0.5f);
	height = info->eye_height > 0 ? info->eye_height :
		(int)(views[0].recommendedImageRectHeight * (info->scale > 0.0f ? info->scale : 1.0f) + 0.5f);
	if (width > (int)views[0].maxImageRectWidth)
		width = (int)views[0].maxImageRectWidth;
	if (height > (int)views[0].maxImageRectHeight)
		height = (int)views[0].maxImageRectHeight;
	info->eye_width = width & ~1;
	info->eye_height = height & ~1;
	if (!swapchain_create(VR_SWAPCHAIN_EYES, info->eye_width, info->eye_height, 2, info) ||
		!swapchain_create(VR_SWAPCHAIN_HUD, info->hud_width, info->hud_height, 1, info))
	{
		return 0;
	}
	info->depth = vr.want_depth && vr.has_depth && swapchain_create(VR_SWAPCHAIN_DEPTH, info->eye_width, info->eye_height, 2, info);
	if (info->visor_width > 0 && info->visor_height > 0 &&
		!swapchain_create(VR_SWAPCHAIN_VISOR, info->visor_width, info->visor_height, 1, info))
	{
		info->visor_width = info->visor_height = 0;
	}
	{
		int panel;

		for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		{
			if (info->panel_width[panel] > 0 && info->panel_height[panel] > 0 &&
				!swapchain_create(VR_SWAPCHAIN_PANEL + panel, info->panel_width[panel], info->panel_height[panel], 1,
					info))
			{
				info->panel_width[panel] = info->panel_height[panel] = 0;
			}
		}
	}
	if (info->scope_size > 0 && !swapchain_create(VR_SWAPCHAIN_SCOPE, info->scope_size, info->scope_size, 1, info))
		info->scope_size = 0;
	info->cylinder = vr.has_cylinder;

	space.poseInReferenceSpace.orientation.w = 1.0f;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	check(xrCreateReferenceSpace(vr.session, &space, &vr.view_space), "xrCreateReferenceSpace (view)");
	space.referenceSpaceType = info->standing ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (xrCreateReferenceSpace(vr.session, &space, &vr.play_space) != XR_SUCCESS)
	{
		space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		if (!check(xrCreateReferenceSpace(vr.session, &space, &vr.play_space), "xrCreateReferenceSpace (play)"))
			return 0;
	}
	if (!actions_create())
		return 0;
	info->eye_gaze = vr.want_eye_gaze && vr.has_eye_gaze;
	info->refresh_rate = 0.0f;
	if (vr.has_refresh_rate)
	{
		PFN_xrRequestDisplayRefreshRateFB request = NULL;
		PFN_xrGetDisplayRefreshRateFB current = NULL;
		PFN_xrEnumerateDisplayRefreshRatesFB enumerate = NULL;
		int refused = 0;

		get_instance_proc_address(vr.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&request);
		get_instance_proc_address(vr.instance, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction *)&current);
		get_instance_proc_address(vr.instance, "xrEnumerateDisplayRefreshRatesFB",
			(PFN_xrVoidFunction *)&enumerate);
		/* the rate asked for (vr.refresh_rate), which the runtime may round
		to a mode of the display or refuse */
		if (request && info->refresh_rate_wanted > 0.0f)
		{
			XrResult result = request(vr.session, info->refresh_rate_wanted);

			if (result != XR_SUCCESS)
			{
				char text[XR_MAX_RESULT_STRING_SIZE];

				host_logf(HOST_LOG_WARN, "vr: %.0f Hz refused: %s", info->refresh_rate_wanted, result_name(result, text));
				refused = 1;
			}
		}
		if (current)
			current(vr.session, &info->refresh_rate);
		/* (refused, or another given: the rates the display has, to choose
		from) */
		if (enumerate && info->refresh_rate_wanted > 0.0f &&
			(refused || fabsf(info->refresh_rate - info->refresh_rate_wanted) > 0.5f))
		{
			float *rates = NULL;
			char text[256] = "";
			uint32_t count = 0, index;
			int at = 0;

			if (enumerate(vr.session, 0, &count, NULL) == XR_SUCCESS && count > 0 &&
				(rates = calloc(count, sizeof(*rates))) != NULL &&
				enumerate(vr.session, count, &count, rates) == XR_SUCCESS)
			{
				for (index = 0; index < count && at < (int)sizeof(text) - 1; index++)
					at += snprintf(text + at, sizeof(text) - (size_t)at, "%s%g", index ? ", " : "", rates[index]);
				host_logf(HOST_LOG_INFO, "vr: the display's refresh rates: %s Hz (vr.refresh_rate %g; %g now)",
					text, info->refresh_rate_wanted, info->refresh_rate);
			}
			free(rates);
		}
	}
	host_logf(HOST_LOG_INFO, "vr: %s, eyes %dx%d (recommended %dx%d), HUD %dx%d, visor %dx%d, %d HUD panels, %s space%s",
		info->system_name, info->eye_width, info->eye_height, info->recommended_width, info->recommended_height,
		info->hud_width, info->hud_height, info->visor_width, info->visor_height,
		(info->panel_width[0] > 0) + (info->panel_width[1] > 0) + (info->panel_width[2] > 0) + (info->panel_width[3] > 0),
		space.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "stage" : "local",
		vr.has_cylinder ? ", cylinder layers" : "");
	vr.info = *info;
	vr.info_made = 1;
	return 1;
}

struct initialize_call
{
	struct vr_host_info *info;
	int result;
};

static void initialize_called(void *argument)
{
	struct initialize_call *call = argument;

	call->result = initialize(call->info);
}

int host_vr_initialize(struct vr_host_info *info)
{
	struct initialize_call call;

	if (!render_thread.on)
		return initialize(info);
	/* (on the render thread, which has the device) */
	call.info = info;
	call.result = 0;
	host_render_stream_call(initialize_called, &call);
	return call.result;
}

void host_vr_render_thread(void)
{
	render_thread.on = 1;
}

const struct vr_host_info *host_vr_info(void)
{
	return vr.info_made ? &vr.info : NULL;
}

int host_vr_frame_begin(void)
{
	XrFrameBeginInfo begin = { XR_TYPE_FRAME_BEGIN_INFO };

	if (vr.frame_begun)
		return 1;
	if (loading.active && !loading.presenting)
	{
		if (!loading.ready)
			return 0;
		/* Transfer the producer's next prediction to the first real
		picture. Waiting again here would skip a refresh at handoff. */
		loading.suspended = 1;
		pthread_mutex_lock(&render_thread.lock);
		while (!render_thread.count && (loading.waiter_busy || loading.waiter_request) && !loading.waiter_failed)
			pthread_cond_wait(&loading.changed, &render_thread.lock);
		pthread_mutex_unlock(&render_thread.lock);
	}
	pthread_mutex_lock(&render_thread.lock);
	if (!render_thread.count)
	{
		pthread_mutex_unlock(&render_thread.lock);
		return 0;
	}
	render_thread.begun = render_thread.waited[render_thread.first];
	render_thread.first = (render_thread.first + 1) % WAITED_FRAMES;
	render_thread.count--;
	pthread_mutex_unlock(&render_thread.lock);
	if (!check(xrBeginFrame(vr.session, &begin), "xrBeginFrame"))
		return 0;
	vr.frame_begun = 1;
	return 1;
}

/* The guest input thread can send rumble while the host exits. Publish
only a running session, and drain its last haptic call before ending or
destroying it. The action and paths have the same session lifetime. */
static struct
{
	pthread_mutex_t lock;
	XrSession session;
	XrAction action;
	XrPath hands[2];
} haptics = { .lock = PTHREAD_MUTEX_INITIALIZER };

static void haptics_session_set(int running)
{
	pthread_mutex_lock(&haptics.lock);
	haptics.session = running ? vr.session : XR_NULL_HANDLE;
	if (running)
	{
		haptics.action = vr.actions[_action_haptic];
		memcpy(haptics.hands, vr.hands, sizeof(haptics.hands));
	}
	pthread_mutex_unlock(&haptics.lock);
}

static void session_begin(void)
{
	XrSessionBeginInfo begin = { XR_TYPE_SESSION_BEGIN_INFO };

	begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vr.running = check(xrBeginSession(vr.session, &begin), "xrBeginSession");
	haptics_session_set(vr.running);
}

static void session_end(void)
{
	haptics_session_set(0);
	xrEndSession(vr.session);
	vr.running = 0;
}

static void events_poll(void)
{
	XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };

	while (xrPollEvent(vr.instance, &event) == XR_SUCCESS)
	{
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		{
			const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event;

			vr.state = changed->state;
			host_logf(HOST_LOG_INFO, "vr: session state %d", (int)vr.state);
			if (vr.state == XR_SESSION_STATE_READY)
			{
				session_begin();
			}
			else if (vr.state == XR_SESSION_STATE_STOPPING)
			{
				if (loading.active)
				{
					loading_wait_stop();
					loading.presenting = 1;
					if (host_vr_frame_begin())
						host_vr_frame_end(NULL);
					loading.presenting = 0;
				}
				session_end();
			}
			else if (vr.state == XR_SESSION_STATE_EXITING || vr.state == XR_SESSION_STATE_LOSS_PENDING)
			{
				/* the runtime closes the application (quit from its dashboard,
				or it is going away): so does the game, as other VR games do */
				host_logf(HOST_LOG_INFO, "vr: the runtime ends the session; quitting");
				host_exit(0);
			}
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)
		{
			int hand;

			for (hand = 0; hand < 2; hand++)
			{
				XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };
				PFN_xrGetCurrentInteractionProfile get = NULL;
				PFN_xrPathToString to_string = NULL;
				char name[XR_MAX_PATH_LENGTH] = "";
				uint32_t length = 0;

				get_instance_proc_address(vr.instance, "xrGetCurrentInteractionProfile", (PFN_xrVoidFunction *)&get);
				get_instance_proc_address(vr.instance, "xrPathToString", (PFN_xrVoidFunction *)&to_string);
				if (get && to_string && get(vr.session, vr.hands[hand], &state) == XR_SUCCESS && state.interactionProfile)
					to_string(vr.instance, state.interactionProfile, sizeof(name), &length, name);
				host_logf(HOST_LOG_INFO, "vr: %s hand: %s", hand ? "right" : "left", name[0] ? name : "none");
			}
		}
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
	}
}

int host_vr_frame_wait(struct vr_host_views *views)
{
	XrFrameWaitInfo wait = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin = { XR_TYPE_FRAME_BEGIN_INFO };

	if (loading_guest_views(views))
		return 1;
	views->should_render = 0;
	if (!vr.session)
		return 0;
	events_poll();
	if (!vr.running)
	{
		/* (until the session is ready: the game goes on without frames) */
		struct timespec pause = { 0, 5000000 };

		nanosleep(&pause, NULL);
		return 0;
	}
	if (vr.frame_begun && !render_thread.on)
		return 1;
	vr.frame.type = XR_TYPE_FRAME_STATE;
	vr.frame.next = NULL;
	host_load_trace("xr.wait.begin", "runtime");
	if (!check(xrWaitFrame(vr.session, &wait, &vr.frame), "xrWaitFrame"))
		return 0;
	host_load_trace("xr.wait.end", "predicted=%lld period=%lld render=%d",
		(long long)vr.frame.predictedDisplayTime, (long long)vr.frame.predictedDisplayPeriod, vr.frame.shouldRender);
	if (render_thread.on)
	{
		/* (begun by the render thread as it draws the frame:
		host_vr_frame_begin) */
		pthread_mutex_lock(&render_thread.lock);
		if (render_thread.count == WAITED_FRAMES)
		{
			host_logf(HOST_LOG_WARN, "vr: a frame waited for was never begun");
			render_thread.first = (render_thread.first + 1) % WAITED_FRAMES;
			render_thread.count--;
		}
		render_thread.waited[(render_thread.first + render_thread.count) % WAITED_FRAMES] = vr.frame;
		render_thread.count++;
		pthread_mutex_unlock(&render_thread.lock);
		render_thread.located = 1;
	}
	else
	{
		if (!check(xrBeginFrame(vr.session, &begin), "xrBeginFrame"))
			return 0;
		vr.frame_begun = 1;
	}
	views->should_render = vr.frame.shouldRender ? 1 : 0;
	views->focused = vr.state == XR_SESSION_STATE_FOCUSED;
	views->display_period = (float)(vr.frame.predictedDisplayPeriod * 1e-9);
	views->display_elapsed = vr.last_display_time ?
		(float)((vr.frame.predictedDisplayTime - vr.last_display_time) * 1e-9) : 0.0f;
	vr.last_display_time = vr.frame.predictedDisplayTime;
	return 1;
}

static void pose_from_xr(const XrPosef *xr, int valid, struct vr_host_pose *pose)
{
	pose->position[0] = xr->position.x;
	pose->position[1] = xr->position.y;
	pose->position[2] = xr->position.z;
	pose->orientation[0] = xr->orientation.x;
	pose->orientation[1] = xr->orientation.y;
	pose->orientation[2] = xr->orientation.z;
	pose->orientation[3] = xr->orientation.w;
	pose->valid = valid;
}

/* the views, head and controllers at the display time, located now */
static int locate_at(XrTime time, struct vr_host_views *views)
{
	XrViewLocateInfo locate = { XR_TYPE_VIEW_LOCATE_INFO };
	XrViewState state = { XR_TYPE_VIEW_STATE };
	XrView located[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	uint32_t count = 0;
	int eye, hand, valid;

	locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate.displayTime = time;
	locate.space = vr.play_space;
	if (xrLocateViews(vr.session, &locate, &state, 2, &count, located) != XR_SUCCESS || count != 2)
		return 0;
	valid = (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
	for (eye = 0; eye < 2; eye++)
	{
		pose_from_xr(&located[eye].pose, valid, &views->eye[eye]);
		views->fov[eye][0] = tanf(located[eye].fov.angleLeft);
		views->fov[eye][1] = tanf(located[eye].fov.angleRight);
		views->fov[eye][2] = tanf(located[eye].fov.angleUp);
		views->fov[eye][3] = tanf(located[eye].fov.angleDown);
	}
	if (xrLocateSpace(vr.view_space, vr.play_space, time, &location) == XR_SUCCESS)
		pose_from_xr(&location.pose, (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0, &views->head);
	for (hand = 0; hand < 2; hand++)
	{
		location.type = XR_TYPE_SPACE_LOCATION;
		location.next = NULL;
		if (xrLocateSpace(vr.aim_spaces[hand], vr.play_space, time, &location) == XR_SUCCESS)
			pose_from_xr(&location.pose, (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0,
				&views->aim[hand]);
		location.type = XR_TYPE_SPACE_LOCATION;
		if (xrLocateSpace(vr.grip_spaces[hand], vr.play_space, time, &location) == XR_SUCCESS)
			pose_from_xr(&location.pose, (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0,
				&views->grip[hand]);
	}
	/* the eyes' gaze, in the head's space: valid only while the eyes are
	tracked (a blink, the eyes lost, the user's permission not given) */
	views->gaze.valid = 0;
	if (vr.has_eye_gaze)
	{
		XrSpaceLocation gaze = { XR_TYPE_SPACE_LOCATION };

		if (xrLocateSpace(vr.gaze_space, vr.view_space, time, &gaze) == XR_SUCCESS)
		{
			pose_from_xr(&gaze.pose, (gaze.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
				(gaze.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT), &views->gaze);
			if (views->gaze.valid && !vr.gaze_tracked)
			{
				host_logf(HOST_LOG_INFO, "vr: the eyes' gaze is tracked");
				vr.gaze_tracked = 1;
			}
		}
	}
	return 1;
}

int host_vr_locate(struct vr_host_views *views)
{
	if (loading_guest_views(views))
		return views->head.valid;
	if (!(render_thread.on ? render_thread.located : vr.frame_begun))
		return 0;
	return locate_at(vr.frame.predictedDisplayTime, views);
}

/* (the render thread: the frame it draws is the one it began) */
int host_vr_locate_late(struct vr_host_views *views)
{
	int valid;

	if (loading.active && !loading.presenting && !loading.ready)
	{
		*views = loading.views;
		return views->head.valid;
	}
	if (!render_thread.on || !host_vr_frame_begin())
		return 0;
	valid = locate_at(render_thread.begun.predictedDisplayTime, views);
	host_load_trace("xr.pose.late", "valid=%d predicted=%lld orientation=%.6f,%.6f,%.6f,%.6f", valid,
		(long long)render_thread.begun.predictedDisplayTime, valid ? views->eye[0].orientation[0] : 0.0f,
		valid ? views->eye[0].orientation[1] : 0.0f, valid ? views->eye[0].orientation[2] : 0.0f,
		valid ? views->eye[0].orientation[3] : 0.0f);
	return valid;
}

static void *loading_wait_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&render_thread.lock);
	for (;;)
	{
		XrFrameWaitInfo wait = { XR_TYPE_FRAME_WAIT_INFO };
		XrFrameState state = { XR_TYPE_FRAME_STATE };
		int success;

		while (!loading.waiter_request && !loading.waiter_stop)
			pthread_cond_wait(&loading.changed, &render_thread.lock);
		if (!loading.waiter_request)
			break;
		loading.waiter_request = 0;
		loading.waiter_busy = 1;
		pthread_mutex_unlock(&render_thread.lock);
		host_load_trace("xr.wait.begin", "loading producer");
		success = check(xrWaitFrame(vr.session, &wait, &state), "xrWaitFrame (loading)");
		host_load_trace("xr.wait.end", "predicted=%lld period=%lld render=%d loading=1",
			(long long)state.predictedDisplayTime, (long long)state.predictedDisplayPeriod, state.shouldRender);
		pthread_mutex_lock(&render_thread.lock);
		loading.waiter_busy = 0;
		loading.waiter_failed = !success;
		if (success)
		{
			render_thread.waited[(render_thread.first + render_thread.count) % WAITED_FRAMES] = state;
			render_thread.count++;
		}
		pthread_cond_broadcast(&loading.changed);
		if (loading.waiter_stop || !success)
			break;
	}
	pthread_mutex_unlock(&render_thread.lock);
	return NULL;
}

static void loading_wait_request(void)
{
	if (!loading.active || loading.ready || !vr.running || !vr.session)
		return;
	pthread_mutex_lock(&render_thread.lock);
	if (!loading.waiter_started)
	{
		loading.waiter_stop = loading.waiter_failed = 0;
		if (pthread_create(&loading.waiter, NULL, loading_wait_thread, NULL) == 0)
			loading.waiter_started = 1;
		else
			loading.waiter_failed = 1;
	}
	if (loading.waiter_started && !loading.waiter_busy && !loading.waiter_failed && !render_thread.count)
	{
		loading.waiter_request = 1;
		pthread_cond_broadcast(&loading.changed);
	}
	pthread_mutex_unlock(&render_thread.lock);
}

static void loading_wait_stop(void)
{
	pthread_mutex_lock(&render_thread.lock);
	loading.waiter_stop = 1;
	pthread_cond_broadcast(&loading.changed);
	pthread_mutex_unlock(&render_thread.lock);
	if (loading.waiter_started)
		pthread_join(loading.waiter, NULL);
	loading.waiter_started = 0;
}

void host_vr_loading_set(int active)
{
	if (!render_thread.on)
		return;
	if (active == 2)
	{
		/* Ensure a final ticket is in flight before asking the producer
		to stop. The real frame consumes it through frame_begin. */
		loading_wait_request();
		loading.ready = 1;
		pthread_mutex_lock(&render_thread.lock);
		loading.waiter_stop = 1;
		pthread_cond_broadcast(&loading.changed);
		pthread_mutex_unlock(&render_thread.lock);
		return;
	}
	if (!active)
	{
		loading_wait_stop();
		loading.presenting = 1;
		if (vr.frame_begun || render_thread.count)
		{
			if (host_vr_frame_begin())
				host_vr_frame_end(NULL);
		}
		loading.presenting = 0;
	}
	if (active)
	{
		memset(&loading.anchor, 0, sizeof(loading.anchor));
		loading.anchor.orientation[3] = 1.0f;
		loading.anchor.position[2] = -2.0f;
		memset(&loading.window, 0, sizeof(loading.window));
		loading.window.orientation[3] = 1.0f;
		loading.window.position[2] = -LOADING_PRESENTATION_VR_DISTANCE;
		/* A game frame may already have been waited for when its map
		change begins. End that ticket before the loading frame loop. */
		loading.presenting = 1;
		if (vr.frame_begun || render_thread.count)
		{
			if (host_vr_frame_begin())
				host_vr_frame_end(NULL);
		}
		loading.presenting = 0;
	}
	pthread_mutex_lock(&render_thread.lock);
	loading.owner = pthread_self();
	loading.active = active;
	loading.suspended = loading.anchored = loading.ready = 0;
	if (!active)
		vr.last_display_time = 0;
	pthread_mutex_unlock(&render_thread.lock);
	if (active)
		loading_wait_request();
}

int host_vr_loading_owned(void)
{
	int owned;

	pthread_mutex_lock(&render_thread.lock);
	owned = render_thread.on && loading.active && !loading.ready && !loading.suspended &&
		vr.running && vr.session && vr.swapchains[VR_SWAPCHAIN_HUD] &&
		loading.waiter_started && !loading.waiter_failed && !loading.waiter_stop;
	pthread_mutex_unlock(&render_thread.lock);
	return owned;
}

int host_vr_loading_begin(const struct loading_presentation_frame *frame, struct vr_host_views *views)
{
	int index;

	memset(views, 0, sizeof(*views));
	if (!loading.active || loading.suspended || !vr.session)
		return -1;
	events_poll();
	loading_wait_request();
	pthread_mutex_lock(&render_thread.lock);
	index = render_thread.count != 0;
	pthread_mutex_unlock(&render_thread.lock);
	if (!index)
		return -1;
	loading.presenting = 1;
	if (!host_vr_frame_begin())
	{
		loading.presenting = 0;
		return -1;
	}
	views->should_render = render_thread.begun.shouldRender != 0;
	views->focused = vr.state == XR_SESSION_STATE_FOCUSED;
	views->display_period = (float)(render_thread.begun.predictedDisplayPeriod * 1e-9);
	locate_at(render_thread.begun.predictedDisplayTime, views);
	pthread_mutex_lock(&render_thread.lock);
	loading.views = *views;
	pthread_mutex_unlock(&render_thread.lock);
	if (!loading.anchored && views->head.valid)
	{
		const float *q = views->head.orientation;

		loading.anchor = views->head;
		loading.anchor.position[0] -= 4.0f * (q[0] * q[2] + q[3] * q[1]);
		loading.anchor.position[1] -= 4.0f * (q[1] * q[2] - q[3] * q[0]);
		loading.anchor.position[2] -= 2.0f * (1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
		/* (the window: the head's facing about the vertical alone, so that
		it stands upright however the head was tilted; at eye height) */
		{
			float forward_x = -2.0f * (q[0] * q[2] + q[3] * q[1]);
			float forward_z = -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
			float length = sqrtf(forward_x * forward_x + forward_z * forward_z), yaw;

			if (length < 1e-4f)
			{
				forward_x = 0.0f;
				forward_z = -1.0f;
				length = 1.0f;
			}
			forward_x /= length;
			forward_z /= length;
			yaw = atan2f(-forward_x, -forward_z);
			loading.window = views->head;
			loading.window.position[0] += forward_x * LOADING_PRESENTATION_VR_DISTANCE;
			loading.window.position[2] += forward_z * LOADING_PRESENTATION_VR_DISTANCE;
			loading.window.orientation[0] = 0.0f;
			loading.window.orientation[1] = sinf(yaw * 0.5f);
			loading.window.orientation[2] = 0.0f;
			loading.window.orientation[3] = cosf(yaw * 0.5f);
		}
		loading.anchored = 1;
	}
	host_load_trace("loading.pose", "epoch=%llu serial=%llu predicted=%lld valid=%d orientation=%.6f,%.6f,%.6f,%.6f",
		(unsigned long long)frame->epoch, (unsigned long long)frame->serial,
		(long long)render_thread.begun.predictedDisplayTime, views->head.valid,
		views->head.orientation[0], views->head.orientation[1], views->head.orientation[2], views->head.orientation[3]);
	index = views->should_render ? host_vr_acquire(VR_SWAPCHAIN_HUD) : -1;
	if (index < 0)
	{
		host_vr_frame_end(NULL);
		loading.presenting = 0;
		loading_wait_request();
	}
	return index;
}

void host_vr_loading_stereo(int stereo, float tangent)
{
	loading.stereo = stereo && tangent > 0.05f && tangent < 4.0f;
	loading.stereo_tangent = tangent;
}

int host_vr_loading_eyes(float left[3], float right[3])
{
	const struct vr_host_pose *window = &loading.window;
	float angle = 2.0f * atan2f(window->orientation[1], window->orientation[3]);
	float cosine = cosf(angle), sine = sinf(angle);
	int eye;

	if (!loading.anchored || !loading.views.eye[0].valid || !loading.views.eye[1].valid)
		return 0;
	for (eye = 0; eye < 2; eye++)
	{
		const float *position = loading.views.eye[eye].position;
		float *out = eye ? right : left;
		float x = position[0] - window->position[0], z = position[2] - window->position[2];

		/* (into the window's space: its yaw undone) */
		out[0] = x * cosine - z * sine;
		out[1] = position[1] - window->position[1];
		out[2] = x * sine + z * cosine;
	}
	return 1;
}

void host_vr_loading_end(const struct loading_presentation_frame *frame)
{
	struct vr_host_layers layers;

	memset(&layers, 0, sizeof(layers));
	layers.hud = 1;
	layers.hud_pose = loading.anchor;
	layers.hud_size[0] = 1.6f;
	layers.hud_size[1] = 0.8f;
	if (loading.stereo)
	{
		/* the menu's scene through a window in the world, as wide as the
		pair was drawn for from where the head was */
		layers.hud_stereo = 1;
		layers.hud_stereo_width = LOADING_PRESENTATION_VR_EYE_WIDTH;
		layers.hud_stereo_height = LOADING_PRESENTATION_VR_EYE_HEIGHT;
		layers.hud_pose = loading.window;
		layers.hud_size[0] = 2.0f * LOADING_PRESENTATION_VR_DISTANCE * loading.stereo_tangent;
		layers.hud_size[1] = layers.hud_size[0] * (float)LOADING_PRESENTATION_VR_EYE_HEIGHT /
			(float)LOADING_PRESENTATION_VR_EYE_WIDTH;
	}
	loading.stereo = 0;
	host_vr_frame_end(&layers);
	loading.presenting = 0;
	loading_wait_request();
	host_load_trace("loading.submitted", "epoch=%llu serial=%llu elapsed_ns=%llu world_space=%d "
		"anchor_position=%.9g,%.9g,%.9g anchor_orientation=%.9g,%.9g,%.9g,%.9g",
		(unsigned long long)frame->epoch, (unsigned long long)frame->serial,
		(unsigned long long)frame->elapsed_ns, !layers.hud_head_locked,
		layers.hud_pose.position[0], layers.hud_pose.position[1], layers.hud_pose.position[2],
		layers.hud_pose.orientation[0], layers.hud_pose.orientation[1],
		layers.hud_pose.orientation[2], layers.hud_pose.orientation[3]);
}

static int button(int action)
{
	XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };

	get.action = vr.actions[action];
	return xrGetActionStateBoolean(vr.session, &get, &state) == XR_SUCCESS && state.isActive && state.currentState;
}

static float axis(int action)
{
	XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };

	get.action = vr.actions[action];
	return xrGetActionStateFloat(vr.session, &get, &state) == XR_SUCCESS && state.isActive ? state.currentState : 0.0f;
}

static void stick(int action, float *value)
{
	XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };

	get.action = vr.actions[action];
	value[0] = value[1] = 0.0f;
	if (xrGetActionStateVector2f(vr.session, &get, &state) == XR_SUCCESS && state.isActive)
	{
		value[0] = state.currentState.x;
		value[1] = state.currentState.y;
	}
}

int host_vr_input(struct vr_host_input *input)
{
	XrActiveActionSet active = { 0 };
	XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
	int index, hand;

	memset(input, 0, sizeof(*input));
	input->right_trigger_touch = -1;
	input->left_trigger_touch = -1;
	if (!vr.running || vr.state != XR_SESSION_STATE_FOCUSED)
		return 0;
	active.actionSet = vr.action_set;
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	if (xrSyncActions(vr.session, &sync) != XR_SUCCESS)
		return 0;
	for (index = 0; index < NUMBER_OF_BUTTON_ACTIONS; index++)
	{
		if (index != _action_right_trigger_touch && index != _action_left_trigger_touch && button(index))
			input->buttons |= 1u << index;
	}
	{
		/* (-1: the controller has no touch on its trigger, or the runtime
		binds none) */
		XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
		XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };

		get.action = vr.actions[_action_right_trigger_touch];
		input->right_trigger_touch = xrGetActionStateBoolean(vr.session, &get, &state) == XR_SUCCESS && state.isActive ?
			state.currentState != 0 : -1;
		get.action = vr.actions[_action_left_trigger_touch];
		input->left_trigger_touch = xrGetActionStateBoolean(vr.session, &get, &state) == XR_SUCCESS && state.isActive ?
			state.currentState != 0 : -1;
	}
	stick(_action_left_stick, input->left_stick);
	stick(_action_right_stick, input->right_stick);
	input->left_trigger = axis(_action_left_trigger);
	input->right_trigger = axis(_action_right_trigger);
	input->left_grip = axis(_action_left_grip);
	input->right_grip = axis(_action_right_grip);
	for (hand = 0; hand < 2; hand++)
	{
		XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
		XrActionStatePose state = { XR_TYPE_ACTION_STATE_POSE };

		get.action = vr.actions[_action_aim_pose];
		get.subactionPath = vr.hands[hand];
		input->active[hand] = xrGetActionStatePose(vr.session, &get, &state) == XR_SUCCESS && state.isActive;
	}
	return 1;
}

/* the name the runtime gives the source bound to one of the controllers'
inputs (a VR_BUTTON_ bit, VR_INPUT_LEFT_TRIGGER or VR_INPUT_RIGHT_TRIGGER),
for the HUD's prompts: its component ("A Button", "Trigger"), with its hand
where the other hand has one too; 0 (and an empty name) without a session,
or with nothing bound to it */
int host_vr_input_name(int input, char *name, int size)
{
	XrBoundSourcesForActionEnumerateInfo enumerate = { XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO };
	XrInputSourceLocalizedNameGetInfo get = { XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO };
	XrPath sources[16];
	uint32_t count = 0, length = 0;
	char text[XR_MAX_LOCALIZED_ACTION_NAME_SIZE * 2];
	int action = -1, index, paired;

	if (!name || size <= 0)
		return 0;
	name[0] = 0;
	if (!vr.running || !vr.session)
		return 0;
	if (input == VR_INPUT_LEFT_TRIGGER)
		action = _action_left_trigger;
	else if (input == VR_INPUT_RIGHT_TRIGGER)
		action = _action_right_trigger;
	for (index = 0; action < 0 && index < NUMBER_OF_BUTTON_ACTIONS; index++)
	{
		/* (a button's bit is its action's: host_vr_input) */
		if (input == (int)(1u << index))
			action = index;
	}
	if (action < 0)
		return 0;
	enumerate.action = vr.actions[action];
	if (xrEnumerateBoundSourcesForAction(vr.session, &enumerate, 0, &count, NULL) != XR_SUCCESS || !count ||
		count > sizeof(sources) / sizeof(sources[0]) ||
		xrEnumerateBoundSourcesForAction(vr.session, &enumerate, count, &count, sources) != XR_SUCCESS || !count)
		return 0;
	paired = action == _action_left_trigger || action == _action_right_trigger ||
		action == _action_left_bumper || action == _action_right_bumper ||
		action == _action_left_stick_click || action == _action_right_stick_click;
	get.sourcePath = sources[0];
	get.whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT |
		(paired ? XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT : 0);
	if (xrGetInputSourceLocalizedName(vr.session, &get, (uint32_t)sizeof(text), &length, text) != XR_SUCCESS ||
		length <= 1)
		return 0;
	text[sizeof(text) - 1] = 0;
	snprintf(name, (size_t)size, "%s", text);
	return 1;
}

int host_vr_acquire(int swapchain)
{
	XrSwapchainImageAcquireInfo acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	XrResult result;
	uint32_t index;
	double started = 0.0;

	if (swapchain < 0 || swapchain >= VR_SWAPCHAIN_COUNT || !vr.swapchains[swapchain])
		return -1;
	if (vr.acquired[swapchain] >= 0 && !vr.waiting[swapchain])
		return vr.acquired[swapchain];
	if (timing.enabled < 0)
		timing.enabled = getenv("HALO_VR_TIMING") != NULL;
	if (timing.enabled)
		started = seconds_now();
	if (vr.acquired[swapchain] < 0)
	{
		if (!check(xrAcquireSwapchainImage(vr.swapchains[swapchain], &acquire, &index), "xrAcquireSwapchainImage"))
			goto failed;
		vr.acquired[swapchain] = (int)index;
		vr.waiting[swapchain] = 1;
	}
	wait.timeout = 1000000000; /* (a second: a lost runtime must not hang the game) */
	result = xrWaitSwapchainImage(vr.swapchains[swapchain], &wait);
	/* A timeout is a positive result, but the image is still the runtime's.
	Keep this acquisition and wait for it again before drawing or releasing. */
	if (result == XR_TIMEOUT_EXPIRED)
		goto failed;
	if (!check(result, "xrWaitSwapchainImage"))
		goto failed;
	vr.waiting[swapchain] = 0;
	if (timing.enabled)
		timing.acquire += seconds_now() - started;
	return vr.acquired[swapchain];
failed:
	if (timing.enabled)
		timing.acquire += seconds_now() - started;
	return -1;
}

void host_vr_release(int swapchain)
{
	XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	if (swapchain < 0 || swapchain >= VR_SWAPCHAIN_COUNT || !vr.swapchains[swapchain] ||
		vr.acquired[swapchain] < 0 || vr.waiting[swapchain])
		return;
	xrReleaseSwapchainImage(vr.swapchains[swapchain], &release);
	vr.acquired[swapchain] = -1;
}

static void pose_to_xr(const struct vr_host_pose *pose, XrPosef *xr)
{
	xr->position.x = pose->position[0];
	xr->position.y = pose->position[1];
	xr->position.z = pose->position[2];
	xr->orientation.x = pose->orientation[0];
	xr->orientation.y = pose->orientation[1];
	xr->orientation.z = pose->orientation[2];
	xr->orientation.w = pose->orientation[3];
}

/* a HUD panel's quad, if it is shown and over (or under) the HUD's layer */
static void panel_layer(const struct vr_host_layers *layers, int panel, int over_hud, XrCompositionLayerQuad *quad,
	const XrCompositionLayerBaseHeader **submitted, uint32_t *count)
{
	int swapchain = VR_SWAPCHAIN_PANEL + panel;

	if (!layers || !layers->hud || !layers->panels[panel].shown || !layers->panels[panel].over_hud != !over_hud ||
		!vr.swapchains[swapchain])
	{
		return;
	}
	memset(quad, 0, sizeof(*quad));
	quad->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
	quad->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	quad->space = layers->hud_head_locked ? vr.view_space : vr.play_space;
	quad->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad->subImage.swapchain = vr.swapchains[swapchain];
	quad->subImage.imageRect.extent.width = vr.swapchain_width[swapchain];
	quad->subImage.imageRect.extent.height = vr.swapchain_height[swapchain];
	pose_to_xr(&layers->panels[panel].pose, &quad->pose);
	quad->size.width = layers->panels[panel].size[0];
	quad->size.height = layers->panels[panel].size[1];
	submitted[(*count)++] = (const XrCompositionLayerBaseHeader *)quad;
}

void host_vr_frame_end(const struct vr_host_layers *layers)
{
	XrFrameEndInfo end = { XR_TYPE_FRAME_END_INFO };
	XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerProjectionView views[2];
	XrCompositionLayerDepthInfoKHR depths[2];
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad stereo[2] = { { XR_TYPE_COMPOSITION_LAYER_QUAD }, { XR_TYPE_COMPOSITION_LAYER_QUAD } };
	XrCompositionLayerQuad visor = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerCylinderKHR cylinder = { XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR };
	XrCompositionLayerQuad panels[VR_PANEL_COUNT];
	XrCompositionLayerQuad scope = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *submitted[6 + VR_PANEL_COUNT];
	uint32_t count = 0;
	int eye, panel;

	if (!vr.frame_begun)
		return;
	if (timing.enabled > 0)
		timing.release -= seconds_now();
	host_vr_release(VR_SWAPCHAIN_EYES);
	host_vr_release(VR_SWAPCHAIN_HUD);
	host_vr_release(VR_SWAPCHAIN_DEPTH);
	host_vr_release(VR_SWAPCHAIN_VISOR);
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		host_vr_release(VR_SWAPCHAIN_PANEL + panel);
	host_vr_release(VR_SWAPCHAIN_SCOPE);
	if (timing.enabled > 0)
		timing.release += seconds_now();
	if (layers && layers->projection)
	{
		for (eye = 0; eye < 2; eye++)
		{
			memset(&views[eye], 0, sizeof(views[eye]));
			views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			pose_to_xr(&layers->eye[eye], &views[eye].pose);
			views[eye].fov.angleLeft = atanf(layers->fov[eye][0]);
			views[eye].fov.angleRight = atanf(layers->fov[eye][1]);
			views[eye].fov.angleUp = atanf(layers->fov[eye][2]);
			views[eye].fov.angleDown = atanf(layers->fov[eye][3]);
			views[eye].subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_EYES];
			views[eye].subImage.imageRect.extent.width = vr.swapchain_width[VR_SWAPCHAIN_EYES];
			views[eye].subImage.imageRect.extent.height = vr.swapchain_height[VR_SWAPCHAIN_EYES];
			views[eye].subImage.imageArrayIndex = (uint32_t)eye;
			if (layers->depth && vr.swapchains[VR_SWAPCHAIN_DEPTH])
			{
				/* the eyes' depth, for the compositor's reprojection */
				memset(&depths[eye], 0, sizeof(depths[eye]));
				depths[eye].type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
				depths[eye].subImage = views[eye].subImage;
				depths[eye].subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_DEPTH];
				/* (kept reversed, xgpu_vulkan.h: 1 at the near plane, 0 at
				the far, which OpenXR has as a nearZ beyond the farZ) */
				depths[eye].minDepth = 0.0f;
				depths[eye].maxDepth = 1.0f;
				depths[eye].nearZ = layers->far_z;
				depths[eye].farZ = layers->near_z;
				views[eye].next = &depths[eye];
			}
		}
		projection.space = vr.play_space;
		projection.viewCount = 2;
		projection.views = views;
		submitted[count++] = (const XrCompositionLayerBaseHeader *)&projection;
	}
	if (layers && layers->projection && layers->scope && vr.swapchains[VR_SWAPCHAIN_SCOPE])
	{
		/* the scope's magnified view at its eyepiece, over the world (the
		weapon about it included), for the eye that looks through it alone:
		the other sees the scope's body, as it would; or the smart-link's on
		the visor, for both. Premultiplied, its lens round in the square
		image */
		scope.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		scope.space = vr.play_space;
		scope.eyeVisibility = layers->scope_eye < 0 ? XR_EYE_VISIBILITY_BOTH :
			layers->scope_eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
		scope.subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_SCOPE];
		scope.subImage.imageRect.extent.width = vr.swapchain_width[VR_SWAPCHAIN_SCOPE];
		scope.subImage.imageRect.extent.height = vr.swapchain_height[VR_SWAPCHAIN_SCOPE];
		pose_to_xr(&layers->scope_pose, &scope.pose);
		scope.size.width = layers->scope_size;
		scope.size.height = layers->scope_size;
		submitted[count++] = (const XrCompositionLayerBaseHeader *)&scope;
	}
	/* the HUD's panels behind its layer (farther), then the layer, then
	those before it, then the visor's glass over all */
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		panel_layer(layers, panel, 0, &panels[panel], submitted, &count);
	if (layers && layers->hud && layers->hud_stereo && layers->hud_stereo_width > 0 &&
		layers->hud_stereo_height > 0 && 2 * layers->hud_stereo_width <= (int)vr.swapchain_width[VR_SWAPCHAIN_HUD] &&
		layers->hud_stereo_height <= (int)vr.swapchain_height[VR_SWAPCHAIN_HUD])
	{
		/* a stereo pair: each eye its half of the image's top, on the same
		quad, held in the world by the compositor at every frame */
		for (eye = 0; eye < 2; eye++)
		{
			stereo[eye].layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			stereo[eye].space = layers->hud_head_locked ? vr.view_space : vr.play_space;
			stereo[eye].eyeVisibility = eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
			stereo[eye].subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_HUD];
			stereo[eye].subImage.imageRect.offset.x = eye * layers->hud_stereo_width;
			stereo[eye].subImage.imageRect.extent.width = layers->hud_stereo_width;
			stereo[eye].subImage.imageRect.extent.height = layers->hud_stereo_height;
			pose_to_xr(&layers->hud_pose, &stereo[eye].pose);
			stereo[eye].size.width = layers->hud_size[0];
			stereo[eye].size.height = layers->hud_size[1];
			submitted[count++] = (const XrCompositionLayerBaseHeader *)&stereo[eye];
		}
	}
	else if (layers && layers->hud && layers->hud_cylinder && vr.has_cylinder)
	{
		/* (as the quad below) */
		cylinder.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		cylinder.space = layers->hud_head_locked ? vr.view_space : vr.play_space;
		cylinder.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		cylinder.subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_HUD];
		cylinder.subImage.imageRect.extent.width = vr.swapchain_width[VR_SWAPCHAIN_HUD];
		cylinder.subImage.imageRect.extent.height = vr.swapchain_height[VR_SWAPCHAIN_HUD];
		pose_to_xr(&layers->hud_pose, &cylinder.pose);
		cylinder.radius = layers->hud_radius;
		cylinder.centralAngle = layers->hud_angle;
		/* (the arc's length over its height) */
		cylinder.aspectRatio = layers->hud_radius * layers->hud_angle / layers->hud_size[1];
		submitted[count++] = (const XrCompositionLayerBaseHeader *)&cylinder;
	}
	else if (layers && layers->hud)
	{
		/* the game's HUD is drawn over the cleared image with its colour
		already multiplied by its coverage, as the compositor's default
		(premultiplied) blending expects */
		quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		quad.space = layers->hud_head_locked ? vr.view_space : vr.play_space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_HUD];
		quad.subImage.imageRect.extent.width = vr.swapchain_width[VR_SWAPCHAIN_HUD];
		quad.subImage.imageRect.extent.height = vr.swapchain_height[VR_SWAPCHAIN_HUD];
		pose_to_xr(&layers->hud_pose, &quad.pose);
		quad.size.width = layers->hud_size[0];
		quad.size.height = layers->hud_size[1];
		submitted[count++] = (const XrCompositionLayerBaseHeader *)&quad;
	}
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		panel_layer(layers, panel, 1, &panels[panel], submitted, &count);
	if (layers && layers->visor && vr.swapchains[VR_SWAPCHAIN_VISOR])
	{
		/* the visor's glass, over the HUD: premultiplied, as the HUD is */
		visor.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		visor.space = vr.view_space;
		visor.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		visor.subImage.swapchain = vr.swapchains[VR_SWAPCHAIN_VISOR];
		visor.subImage.imageRect.extent.width = vr.swapchain_width[VR_SWAPCHAIN_VISOR];
		visor.subImage.imageRect.extent.height = vr.swapchain_height[VR_SWAPCHAIN_VISOR];
		pose_to_xr(&layers->visor_pose, &visor.pose);
		visor.size.width = layers->visor_size[0];
		visor.size.height = layers->visor_size[1];
		submitted[count++] = (const XrCompositionLayerBaseHeader *)&visor;
	}
	end.displayTime = render_thread.on ? render_thread.begun.predictedDisplayTime : vr.frame.predictedDisplayTime;
	end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end.layerCount = count;
	end.layers = submitted;
	if (timing.enabled > 0)
		timing.end -= seconds_now();
	host_load_trace("xr.submit.begin", "predicted=%lld layers=%u", (long long)end.displayTime, count);
	check(xrEndFrame(vr.session, &end), "xrEndFrame");
	host_load_trace("xr.submit.end", "predicted=%lld layers=%u", (long long)end.displayTime, count);
	if (timing.enabled > 0)
	{
		timing.end += seconds_now();
		timing_frame();
	}
	vr.frame_begun = 0;
}

void host_vr_haptic(int hand, float amplitude, float seconds)
{
	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };

	if (hand < 0 || hand > 1)
		return;
	pthread_mutex_lock(&haptics.lock);
	if (haptics.session)
	{
		info.action = haptics.action;
		info.subactionPath = haptics.hands[hand];
		vibration.amplitude = amplitude;
		vibration.duration = (XrDuration)(seconds * 1e9f);
		vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
		xrApplyHapticFeedback(haptics.session, &info, (const XrHapticBaseHeader *)&vibration);
	}
	pthread_mutex_unlock(&haptics.lock);
}

/* ---------- the Vulkan device and runtime images (host only) */

int host_vr_vulkan_requested(void)
{
	return vr.probed && vr.binding == VR_BINDING_VULKAN;
}

int host_vr_vulkan_prepare(void)
{
	XrSystemGetInfo system = { XR_TYPE_SYSTEM_GET_INFO };

	if (vulkan.prepared)
		return 1;
	/* The device is made before the guest supplies its settings. Enable
	the optional extensions here; initialize uses only the ones requested. */
	vr.want_depth = vr.want_eye_gaze = 1;
	if (host_vr_probe() != VR_BINDING_VULKAN || !instance_create())
		goto failed;
	system.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!check(xrGetSystem(vr.instance, &system, &vr.system), "xrGetSystem (is the headset on?)"))
		goto failed;
	vulkan.requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
	if (!check(xrGetVulkanGraphicsRequirements2KHR(vr.instance, vr.system, &vulkan.requirements),
		"xrGetVulkanGraphicsRequirements2KHR"))
		goto failed;
	host_logf(HOST_LOG_INFO, "vr: Vulkan minimum %u.%u, highest tested %u.%u",
		(unsigned)XR_VERSION_MAJOR(vulkan.requirements.minApiVersionSupported),
		(unsigned)XR_VERSION_MINOR(vulkan.requirements.minApiVersionSupported),
		(unsigned)XR_VERSION_MAJOR(vulkan.requirements.maxApiVersionSupported),
		(unsigned)XR_VERSION_MINOR(vulkan.requirements.maxApiVersionSupported));
	vulkan.prepared = 1;
	return 1;
failed:
	host_vr_vulkan_shutdown();
	return 0;
}

VkResult host_vr_vulkan_create_instance(PFN_vkGetInstanceProcAddr get_proc,
	const VkInstanceCreateInfo *create, const VkAllocationCallbacks *allocator, VkInstance *instance)
{
	XrVulkanInstanceCreateInfoKHR xr = { XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
	VkResult result = VK_ERROR_INITIALIZATION_FAILED;
	uint32_t version = create->pApplicationInfo ? create->pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
	XrVersion requested = XR_MAKE_VERSION(VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version),
		VK_VERSION_PATCH(version));

	*instance = VK_NULL_HANDLE;
	if (!vulkan.prepared)
		return result;
	if (requested < vulkan.requirements.minApiVersionSupported)
	{
		host_logf(HOST_LOG_ERROR, "vr: the runtime requires a newer Vulkan version than the backend requests");
		return VK_ERROR_INCOMPATIBLE_DRIVER;
	}
	xr.systemId = vr.system;
	xr.pfnGetInstanceProcAddr = get_proc;
	xr.vulkanCreateInfo = create;
	xr.vulkanAllocator = allocator;
	if (!check(xrCreateVulkanInstanceKHR(vr.instance, &xr, instance, &result), "xrCreateVulkanInstanceKHR"))
		return VK_ERROR_INITIALIZATION_FAILED;
	return result;
}

VkPhysicalDevice host_vr_vulkan_physical_device(VkInstance instance)
{
	XrVulkanGraphicsDeviceGetInfoKHR get = { XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
	VkPhysicalDevice physical = VK_NULL_HANDLE;

	if (!vulkan.prepared)
		return VK_NULL_HANDLE;
	get.systemId = vr.system;
	get.vulkanInstance = instance;
	if (!check(xrGetVulkanGraphicsDevice2KHR(vr.instance, &get, &physical), "xrGetVulkanGraphicsDevice2KHR"))
		return VK_NULL_HANDLE;
	return physical;
}

VkResult host_vr_vulkan_create_device(PFN_vkGetInstanceProcAddr get_proc, VkPhysicalDevice physical,
	const VkDeviceCreateInfo *create, const VkAllocationCallbacks *allocator, VkDevice *device)
{
	XrVulkanDeviceCreateInfoKHR xr = { XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
	VkResult result = VK_ERROR_INITIALIZATION_FAILED;

	*device = VK_NULL_HANDLE;
	if (!vulkan.prepared)
		return result;
	xr.systemId = vr.system;
	xr.pfnGetInstanceProcAddr = get_proc;
	xr.vulkanPhysicalDevice = physical;
	xr.vulkanCreateInfo = create;
	xr.vulkanAllocator = allocator;
	if (!check(xrCreateVulkanDeviceKHR(vr.instance, &xr, device, &result), "xrCreateVulkanDeviceKHR"))
		return VK_ERROR_INITIALIZATION_FAILED;
	return result;
}

void host_vr_vulkan_bind(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
	uint32_t queue_family, uint32_t queue_index)
{
	vulkan.binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
	vulkan.binding.instance = instance;
	vulkan.binding.physicalDevice = physical;
	vulkan.binding.device = device;
	vulkan.binding.queueFamilyIndex = queue_family;
	vulkan.binding.queueIndex = queue_index;
}

VkImage host_vr_vulkan_image(int swapchain, int index)
{
	if (swapchain < 0 || swapchain >= VR_SWAPCHAIN_COUNT || index < 0 ||
		index >= vr.info.image_count[swapchain] || !vulkan.images[swapchain])
		return VK_NULL_HANDLE;
	return vulkan.images[swapchain][index].image;
}

VkFormat host_vr_vulkan_format(int swapchain)
{
	if (swapchain < 0 || swapchain >= VR_SWAPCHAIN_COUNT || !vulkan.images[swapchain])
		return VK_FORMAT_UNDEFINED;
	return vulkan.formats[swapchain];
}

void host_vr_vulkan_set_depth_format(VkFormat format)
{
	vulkan.depth_format = format;
}

void host_vr_vulkan_shutdown(void)
{
	int index;

	haptics_session_set(0);
	if (loading.active)
		host_vr_loading_set(0);
	if (vr.session && xrDestroySession)
		xrDestroySession(vr.session);
	if (vr.instance && xrDestroyInstance)
		xrDestroyInstance(vr.instance);
	for (index = 0; index < VR_SWAPCHAIN_COUNT; index++)
	{
		free(vulkan.images[index]);
		vr.swapchains[index] = XR_NULL_HANDLE;
		vr.acquired[index] = -1;
		vr.waiting[index] = 0;
	}
	memset(&vulkan, 0, sizeof(vulkan));
	vr.session = XR_NULL_HANDLE;
	vr.instance = XR_NULL_HANDLE;
	vr.system = XR_NULL_SYSTEM_ID;
	vr.info_made = vr.running = vr.frame_begun = 0;
}
