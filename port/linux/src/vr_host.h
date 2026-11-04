/*
VR_HOST.H

What the VR mode (HALO_VR, vr.c) asks of OpenXR, which the host does
(port/linux/host/host_vr.c): the session, the swapchains, the frame loop,
the poses and the controllers. The game is guest code with 32-bit pointers
that cannot call OpenXR, whose structures hold 64-bit pointers and handles,
so these structures hold only 32-bit integers and floats, laid out alike on
both sides. The swapchains' images (Vulkan's) stay on the host, and their
names here are zero.

Poses are in the play space: OpenXR's (metres; x right, y up, z backwards),
the stage when standing, the local space when seated. Field of view angles
are tangents: left and down negative.
*/

#ifndef __HALO_VR_HOST_H
#define __HALO_VR_HOST_H

enum
{
	VR_SWAPCHAIN_EYES = 0, /* a 2-layer texture array: layer 0 the left eye */
	VR_SWAPCHAIN_HUD,      /* the HUD and the menus, a quad (or cylinder) layer */
	VR_SWAPCHAIN_DEPTH,    /* the eyes' depth (XR_KHR_composition_layer_depth), 2 layers */
	VR_SWAPCHAIN_VISOR,    /* a quad layer for the visor's glass (unused: the game draws it into the eyes' images, vr_visor.c) */
	/* the HUD's panels at their own depths (vr_visor.c), a quad layer each */
	VR_SWAPCHAIN_PANEL,
	/* the magnified view in a weapon's scope, a quad layer at its eyepiece
	(or on the visor, smart-linked) */
	VR_SWAPCHAIN_SCOPE = VR_SWAPCHAIN_PANEL + 4,
	VR_SWAPCHAIN_COUNT,
	VR_PANEL_COUNT = 4,
	VR_SWAPCHAIN_IMAGES = 4,
};

/* the graphics binding the runtime's session takes the renderer through
(host_vr_probe) */
enum
{
	VR_BINDING_NONE = 0,
	VR_BINDING_VULKAN, /* XR_KHR_vulkan_enable2: the host owns its instance, device and images */
};

/* the controllers' buttons (struct vr_host_input) */
enum
{
	VR_BUTTON_A = 1 << 0,
	VR_BUTTON_B = 1 << 1,
	VR_BUTTON_X = 1 << 2,
	VR_BUTTON_Y = 1 << 3,
	VR_BUTTON_MENU = 1 << 4,
	VR_BUTTON_VIEW = 1 << 5,
	VR_BUTTON_DPAD_UP = 1 << 6,
	VR_BUTTON_DPAD_DOWN = 1 << 7,
	VR_BUTTON_DPAD_LEFT = 1 << 8,
	VR_BUTTON_DPAD_RIGHT = 1 << 9,
	VR_BUTTON_LEFT_STICK = 1 << 10,
	VR_BUTTON_RIGHT_STICK = 1 << 11,
	VR_BUTTON_LEFT_BUMPER = 1 << 12,
	VR_BUTTON_RIGHT_BUMPER = 1 << 13,
};

/* the triggers, for host_vr_input_name */
enum
{
	VR_INPUT_LEFT_TRIGGER = 1 << 14,
	VR_INPUT_RIGHT_TRIGGER = 1 << 15,
};

struct vr_host_info
{
	/* in: the eye images' size (0: the runtime's recommendation scaled by
	scale); out: the size made */
	int eye_width, eye_height;
	float scale;
	/* in */
	int hud_width, hud_height;
	/* in: the visor's image (0: none); out: 0 if it could not be made */
	int visor_width, visor_height;
	/* in: the HUD's panels' images (0: none); out: 0 where one could not be
	made */
	int panel_width[VR_PANEL_COUNT], panel_height[VR_PANEL_COUNT];
	/* in: the scope's image, square (0: none); out: 0 if it could not be
	made */
	int scope_size;
	int standing;
	float refresh_rate_wanted; /* 0: the runtime's choice */
	int depth; /* in: submit the eyes' depth; out: the runtime takes it */
	/* in: track the eyes' gaze (XR_EXT_eye_gaze_interaction); out: the
	runtime and its system do */
	int eye_gaze;
	/* out */
	int recommended_width, recommended_height;
	int image_count[VR_SWAPCHAIN_COUNT];
	unsigned int images[VR_SWAPCHAIN_COUNT][VR_SWAPCHAIN_IMAGES];
	float refresh_rate;
	/* the runtime shows cylinder layers (XR_KHR_composition_layer_cylinder) */
	int cylinder;
	char system_name[64];
};

struct vr_host_pose
{
	float position[3];
	float orientation[4]; /* x, y, z, w */
	int valid;
};

struct vr_host_views
{
	/* the frame's (host_vr_frame_wait) */
	int should_render;
	int focused;
	float display_period; /* seconds */
	/* from the last frame's predicted display time to this one's; 0 on
	the first */
	float display_elapsed; /* seconds */
	/* host_vr_locate's, at the frame's predicted display time */
	struct vr_host_pose eye[2];
	float fov[2][4]; /* left, right, up, down */
	struct vr_host_pose head;
	struct vr_host_pose aim[2]; /* the controllers' pointing poses */
	struct vr_host_pose grip[2];
	/* the eyes' gaze (vr_host_info's eye_gaze), in the head's space (its -z
	where the eyes look; OpenXR's view space), valid while they are
	tracked */
	struct vr_host_pose gaze;
};

/* The late latch (vr.late_latch): the poses a frame's eyes are drawn from,
and those they were located as, which the host's render thread, just before
it draws the frame, moves to the newest it locates (host_vr_locate_late;
xgpu_stream.h's VR_EYES) */
struct vr_host_latch
{
	/* the head's newest pose may be taken (not debug.vr_test_head's) */
	int head;
	/* the controller the first-person weapon and the hands move with (0
	the runtime's left, 1 its right), -1 none (debug.vr_test_hands') */
	int hand;
	/* when the poses were located: nanoseconds of CLOCK_MONOTONIC */
	unsigned int located_low, located_high;
	/* as located: the eyes and the controllers' grips */
	struct vr_host_pose located_eye[2];
	struct vr_host_pose located_grip[2];
	/* the eyes the frame is drawn from (and its layers submitted with):
	the located ones, moved by vr.c's tests (debug.vr_test_jitter) */
	struct vr_host_pose eye[2];
};

struct vr_host_input
{
	unsigned int buttons;
	float left_stick[2], right_stick[2];
	float left_trigger, right_trigger;
	float left_grip, right_grip;
	/* a finger on the right trigger: 1, none: 0, the trigger senses none
	(no such input): -1 */
	int right_trigger_touch;
	int active[2];
	/* and on the left trigger, the same (a left-hander's trigger finger) */
	int left_trigger_touch;
};

struct vr_host_layers
{
	/* the eye images, drawn from these views */
	int projection;
	struct vr_host_pose eye[2];
	float fov[2][4];
	/* with the eyes' depth (the game's 0 to 1 from near_z to far_z,
	metres; kept reversed, 1 to 0, in the host's image: host_vr.c) */
	int depth;
	float near_z, far_z;
	/* the HUD image on a quad: in front of the head (head_locked), or at
	pose in the play space; size in metres. On a cylinder instead
	(hud_cylinder, where vr_host_info's cylinder says the runtime has them):
	about pose's y axis, hud_radius from it, hud_angle radians around (its
	middle along pose's -z), hud_size[1] metres high */
	int hud;
	int hud_head_locked;
	struct vr_host_pose hud_pose;
	float hud_size[2];
	int hud_cylinder;
	float hud_radius, hud_angle;
	/* the HUD image's top left holds a stereo pair instead, each eye's
	hud_stereo_width by hud_stereo_height pixels side by side (the left
	eye's first), on a quad for each eye at hud_pose, hud_size metres (the
	loading screen's window: loading_presentation.h) */
	int hud_stereo;
	int hud_stereo_width, hud_stereo_height;
	/* the HUD's panels (VR_SWAPCHAIN_PANEL on), each a quad at its own
	depth, placed as the HUD's layer is (hud_head_locked): those not
	over_hud under the HUD's layer, the others over it */
	struct
	{
		int shown;
		int over_hud;
		struct vr_host_pose pose;
		float size[2];
	} panels[VR_PANEL_COUNT];
	/* the visor's glass on a quad before the head, over everything else */
	int visor;
	struct vr_host_pose visor_pose;
	float visor_size[2];
	/* the scope's image (VR_SWAPCHAIN_SCOPE) on a quad at the scope's
	eyepiece, in the play space, size metres square, seen by one eye alone
	(scope_eye: 0 left, 1 right), or the smart-link's on the visor by both
	(-1): over the eyes' images, under the HUD */
	int scope;
	int scope_eye;
	struct vr_host_pose scope_pose;
	float scope_size;
};

/* after SDL_Init and before the window is made: loads the runtime and
reads its extensions (without starting it, nor asking for a headset), and
says which binding its session will take: VR_BINDING_VULKAN if it offers
XR_KHR_vulkan_enable2, else VR_BINDING_NONE (the reason is logged). */
int host_vr_probe(void);
/* after the host has bound its Vulkan device (on its render thread).
0 if there is no OpenXR runtime, headset or graphics binding. */
int host_vr_initialize(struct vr_host_info *info);
/* waits for the runtime's next frame (which the host's render thread begins
and ends as it draws it: port/linux/host/host_vr_render.h); 0 if the
session is not running (no frame is waited for) */
int host_vr_frame_wait(struct vr_host_views *views);
/* the views, head and controllers at the frame's display time */
int host_vr_locate(struct vr_host_views *views);
int host_vr_input(struct vr_host_input *input);
/* the name the runtime gives the source bound to an input (a VR_BUTTON_ bit,
or VR_INPUT_LEFT_TRIGGER, VR_INPUT_RIGHT_TRIGGER), UTF-8, for the HUD's
prompts; 0 when it gives none */
int host_vr_input_name(int input, char *name, int size);
void host_vr_haptic(int hand, float amplitude, float seconds);

#endif
