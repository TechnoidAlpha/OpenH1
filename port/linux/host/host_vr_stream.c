/*
HOST_VR_STREAM.C

The VR records of the renderer's command stream (HALO_VR; host_vr_stream.h),
for the host's renderer (Vulkan's, port/linux/vulkan/host_vulkan_vr.c):
the records' entries (xgpu_stream.h, "the VR mode"),
and what the renderer's composition asks of the game's VR mode (vr.h's and
vr_visor.h's functions, the guest's vr.c and vr_visor.c), answered as the
records said, and of OpenXR, which it calls here on the render thread
(host_vr_render.h).

A frame of the runtime's is waited for on the game's thread
(host_vr_frame_wait) and begun here before its first image is acquired (or
at its end, if none is), then ended with the layers the game would submit
(VR_SUBMIT's, computed by vr.c as if every image were acquired), less those
whose image the composition did not acquire or draw: as vr.c's
halo_vr_present does with the guest's own backend.

The late latch (xgpu_stream.h; vr.late_latch): as the eye pass begins
(VR_EYES) the head and the controllers are located again, at the frame's
display time, and each eye's transforms are moved by how far its eye has
moved since the game located it (and the weapon's controller, for the
first-person weapon and the hands), as its view would have been made from
there: clip_eye' = K M inverse(K) clip_eye, K the eye's clip transform from
its own space for the projection, M the move in that space. The layers are
submitted with the eyes moved.
*/

#include "host.h"
#include "host_render_stream.h"
#include "host_vr_stream.h"
#include "host_vr_render.h"
#include "xgpu_backend.h"
#include "vr.h"
#include "vr_visor.h"
#include "port_config.h"
#include "frame_pacing.h"

#include <math.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(sizeof(struct vr_host_layers) == XGPU_STREAM_VR_LAYERS_WORDS * 4, "VR layers");
_Static_assert(sizeof(struct vr_host_pose) == XGPU_STREAM_VR_POSE_WORDS * 4, "VR pose");
_Static_assert(sizeof(struct vr_host_latch) == XGPU_STREAM_VR_LATCH_WORDS * 4, "VR latch");
_Static_assert(sizeof(struct halo_vr_scope_window) == XGPU_STREAM_VR_SCOPE_WINDOW_WORDS * 4, "VR scope window");
_Static_assert(sizeof(struct halo_vr_cockpit_window) == XGPU_STREAM_VR_COCKPIT_WINDOW_WORDS * 4, "VR cockpit window");
_Static_assert(sizeof(struct halo_vr_cockpit_display) == XGPU_STREAM_VR_COCKPIT_DISPLAY_WORDS * 4,
	"VR cockpit display");
_Static_assert(sizeof(struct vr_visor_hud_image) == XGPU_STREAM_VR_HUD_IMAGE_WORDS * 4, "VR HUD image");
_Static_assert(sizeof(struct vr_visor_panel) == XGPU_STREAM_VR_PANEL_WORDS * 4, "VR panel");
_Static_assert(offsetof(struct vr_visor_image, noise_texture) >= XGPU_STREAM_VR_VISOR_IMAGE_WORDS * 4 &&
	offsetof(struct vr_visor_image, noise_texture) < XGPU_STREAM_VR_VISOR_IMAGE_WORDS * 4 + 8, "VR visor image");

_Static_assert(sizeof(struct vr_visor_rim) == XGPU_STREAM_VR_RIM_WORDS * 4, "VR rim");

static struct
{
	/* vr.foveation's inset and its name, as the session was made */
	float foveation_inset;
	char foveation_name[64];
	/* this frame's: the gaze (VR_EYES), the state (VR_STATE), whether the
	game waited for a frame of the runtime's for it (either says) */
	int foveation;
	float centres[2][2];
	struct xgpu_stream_vr_state state;
	BOOL frame;
	/* the images acquired this frame (bits of VR_SWAPCHAIN_*), the eyes'
	depth written */
	unsigned int acquired;
	BOOL depth_written;
	/* the frame being submitted (VR_SUBMIT), and the layers ended with */
	const struct xgpu_stream_vr_submit *submit;
	struct vr_host_layers layers;
	struct vr_host_pose head;
	BOOL submitted;
	/* debug.gpu_stats (VR_INITIALIZE's) */
	BOOL statistics;
} vr_frame;

static const struct host_vr_renderer *renderer;

/* ---------- the settings (port_config.h, the stand-in beside the
renderer's shared files: port/linux/vulkan/xgpu_vulkan_port_config.h)

The VR mode's debugging settings, from their environment variables alone
(port_config.c's names and defaults), as config.toml is the guest's. */

static const struct
{
	const char *name, *variable, *value;
} settings[] =
{
	{ "debug.screenshot_directory", "HALO_SCREENSHOT_DIR", "" },
	{ "debug.vr_gpu_time", "HALO_VR_GPU_TIME", "false" },
	{ "debug.vr_foveation_show", "HALO_VR_FOVEATION_SHOW", "false" },
};

/* each setting's value, its variable read once (the screenshots' is asked
each frame; Windows's getenv locks and scans the environment) */
static const char *setting_values[sizeof(settings) / sizeof(settings[0])];

const char *config_string(const char *name)
{
	size_t index;

	for (index = 0; index < sizeof(settings) / sizeof(settings[0]); index++)
	{
		const char *value;

		if (strcmp(settings[index].name, name))
			continue;
		value = __atomic_load_n(&setting_values[index], __ATOMIC_ACQUIRE);
		if (!value)
		{
			value = getenv(settings[index].variable);
			value = value ? value : settings[index].value;
			__atomic_store_n(&setting_values[index], value, __ATOMIC_RELEASE);
		}
		return value;
	}
	host_logf(HOST_LOG_WARN, "vr: no setting %s in the host", name);
	return "";
}

int config_boolean(const char *name)
{
	const char *value = config_string(name);

	return !strcmp(value, "true") || !strcmp(value, "1") || !strcmp(value, "yes") || !strcmp(value, "on");
}

long config_integer(const char *name)
{
	return strtol(config_string(name), NULL, 10);
}

/* ---------- the late latch */

enum
{
	_latch_world,
	_latch_sky,
	_latch_held,
	_latch_moves,
};

/* a rigid move: a rotation (a unit quaternion: x, y, z, w) then a
translation */
struct move
{
	double q[4];
	double t[3];
};

static struct
{
	/* this frame's (VR_EYES): the poses the game located and drew from,
	when it located them; the head's and the weapon's controller's moves
	taken (each eye's move in the play space, the controller's) */
	BOOL sampled;
	struct vr_host_latch latch;
	uint64_t located, latched;
	BOOL head, held;
	struct move eye_move[2], hand_move;
	/* each eye's view as the game made it (the eye's place, the head's
	turn: vr_render.c's eye cameras), and each eye's moves in its own space
	for the world, the sky and the first-person weapon */
	double view_move[_latch_moves][2][4][4];
	BOOL held_drawn;
	/* the eyes' transforms set last, as moved, and whether they were (for
	the screen corrections) */
	float transforms[2][16];
	BOOL moved;
	/* the time from the poses' locating to the frame's submission
	(debug.gpu_stats): as the game located them, and as latched */
	struct frame_pacing_latency located_age, latched_age;
	unsigned int frames;
} latch;

static uint64_t monotonic_nanoseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void quaternion_multiply(const double a[4], const double b[4], double out[4])
{
	double r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy(out, r, sizeof(r));
}

static void quaternion_rotate(const double q[4], const double v[3], double out[3])
{
	double c[3], cc[3];
	int axis;

	/* (v + 2 w (u x v) + 2 u x (u x v), u the vector part) */
	c[0] = q[1] * v[2] - q[2] * v[1];
	c[1] = q[2] * v[0] - q[0] * v[2];
	c[2] = q[0] * v[1] - q[1] * v[0];
	cc[0] = q[1] * c[2] - q[2] * c[1];
	cc[1] = q[2] * c[0] - q[0] * c[2];
	cc[2] = q[0] * c[1] - q[1] * c[0];
	for (axis = 0; axis < 3; axis++)
		out[axis] = v[axis] + 2.0 * (q[3] * c[axis] + cc[axis]);
}

/* a pose's move from its own space to the play space */
static void move_of_pose(const float position[3], const float orientation[4], struct move *move)
{
	double length = 0.0;
	int axis;

	for (axis = 0; axis < 4; axis++)
		length += (double)orientation[axis] * orientation[axis];
	length = length > 0.0 ? 1.0 / sqrt(length) : 0.0;
	for (axis = 0; axis < 4; axis++)
		move->q[axis] = orientation[axis] * length;
	if (length == 0.0)
		move->q[3] = 1.0;
	for (axis = 0; axis < 3; axis++)
		move->t[axis] = position[axis];
}

/* out: b, then a */
static void move_then(const struct move *a, const struct move *b, struct move *out)
{
	struct move r;
	int axis;

	quaternion_multiply(a->q, b->q, r.q);
	quaternion_rotate(a->q, b->t, r.t);
	for (axis = 0; axis < 3; axis++)
		r.t[axis] += a->t[axis];
	*out = r;
}

static void move_inverse(const struct move *a, struct move *out)
{
	struct move r;
	int axis;

	r.q[0] = -a->q[0];
	r.q[1] = -a->q[1];
	r.q[2] = -a->q[2];
	r.q[3] = a->q[3];
	quaternion_rotate(r.q, a->t, r.t);
	for (axis = 0; axis < 3; axis++)
		r.t[axis] = -r.t[axis];
	*out = r;
}

static void move_identity(struct move *move)
{
	memset(move, 0, sizeof(*move));
	move->q[3] = 1.0;
}

/* row-major, of column vectors */
static void move_matrix(const struct move *move, double m[4][4])
{
	const double x = move->q[0], y = move->q[1], z = move->q[2], w = move->q[3];

	m[0][0] = 1.0 - 2.0 * (y * y + z * z);
	m[0][1] = 2.0 * (x * y - z * w);
	m[0][2] = 2.0 * (x * z + y * w);
	m[1][0] = 2.0 * (x * y + z * w);
	m[1][1] = 1.0 - 2.0 * (x * x + z * z);
	m[1][2] = 2.0 * (y * z - x * w);
	m[2][0] = 2.0 * (x * z - y * w);
	m[2][1] = 2.0 * (y * z + x * w);
	m[2][2] = 1.0 - 2.0 * (x * x + y * y);
	m[0][3] = move->t[0];
	m[1][3] = move->t[1];
	m[2][3] = move->t[2];
	m[3][0] = m[3][1] = m[3][2] = 0.0;
	m[3][3] = 1.0;
}

/* a pose moved (in the play space) */
static void pose_moved(const struct move *move, struct vr_host_pose *pose)
{
	struct move moved;
	int axis;

	move_of_pose(pose->position, pose->orientation, &moved);
	move_then(move, &moved, &moved);
	for (axis = 0; axis < 3; axis++)
		pose->position[axis] = (float)moved.t[axis];
	for (axis = 0; axis < 4; axis++)
		pose->orientation[axis] = (float)moved.q[axis];
}

static BOOL matrix_inverse(const double m[4][4], double out[4][4])
{
	double a[4][8];
	int i, j, k;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			a[i][j] = m[i][j];
			a[i][j + 4] = i == j ? 1.0 : 0.0;
		}
	}
	for (i = 0; i < 4; i++)
	{
		int pivot = i;
		double scale;

		for (k = i + 1; k < 4; k++)
		{
			if (fabs(a[k][i]) > fabs(a[pivot][i]))
				pivot = k;
		}
		if (fabs(a[pivot][i]) < 1e-12)
			return FALSE;
		for (j = 0; j < 8 && pivot != i; j++)
		{
			double t = a[i][j];

			a[i][j] = a[pivot][j];
			a[pivot][j] = t;
		}
		scale = 1.0 / a[i][i];
		for (j = 0; j < 8; j++)
			a[i][j] *= scale;
		for (k = 0; k < 4; k++)
		{
			double factor = a[k][i];

			if (k == i || factor == 0.0)
				continue;
			for (j = 0; j < 8; j++)
				a[k][j] -= factor * a[i][j];
		}
	}
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			out[i][j] = a[i][j + 4];
	}
	return TRUE;
}

static void matrix_multiply(const double a[4][4], const double b[4][4], double out[4][4])
{
	double r[4][4];
	int i, j, k;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			r[i][j] = 0.0;
			for (k = 0; k < 4; k++)
				r[i][j] += a[i][k] * b[k][j];
		}
	}
	memcpy(out, r, sizeof(r));
}

/* VR_EYES: the poses located again, and the moves made of them */
static void latch_take(const struct xgpu_stream_vr_eyes *record)
{
	const struct vr_host_latch *sample = &latch.latch;
	struct vr_host_views views;
	struct move moves[2], hand, before;
	int eye, which;

	latch.sampled = record->latched != 0;
	latch.head = latch.held = latch.held_drawn = latch.moved = FALSE;
	latch.latched = 0;
	if (!latch.sampled)
		return;
	memcpy(&latch.latch, record->latch, sizeof(latch.latch));
	latch.located = (uint64_t)sample->located_high << 32 | sample->located_low;
	if ((!sample->head && sample->hand < 0) || !host_vr_locate_late(&views))
		return;
	latch.latched = monotonic_nanoseconds();
	/* each eye's move since located, in the play space */
	for (eye = 0; eye < 2; eye++)
	{
		move_identity(&moves[eye]);
		if (sample->head && sample->located_eye[0].valid && sample->located_eye[1].valid && views.eye[0].valid &&
			views.eye[1].valid)
		{
			move_of_pose(sample->located_eye[eye].position, sample->located_eye[eye].orientation, &before);
			move_inverse(&before, &before);
			move_of_pose(views.eye[eye].position, views.eye[eye].orientation, &moves[eye]);
			move_then(&moves[eye], &before, &moves[eye]);
			latch.head = TRUE;
		}
	}
	/* the weapon's controller's */
	move_identity(&hand);
	if (sample->hand >= 0 && sample->hand < 2 && sample->located_grip[sample->hand].valid &&
		views.grip[sample->hand].valid)
	{
		move_of_pose(sample->located_grip[sample->hand].position, sample->located_grip[sample->hand].orientation,
			&before);
		move_inverse(&before, &before);
		move_of_pose(views.grip[sample->hand].position, views.grip[sample->hand].orientation, &hand);
		move_then(&hand, &before, &hand);
		latch.held = TRUE;
	}
	memcpy(latch.eye_move, moves, sizeof(moves));
	latch.hand_move = hand;
	/* in each eye's view (its place, the head's turn, as vr_render.c makes
	the eyes' cameras): what was at x in it is now at view^-1 eye^-1 view x,
	and the weapon's at view^-1 eye^-1 hand view x */
	for (eye = 0; eye < 2; eye++)
	{
		struct move view, view_inverse, eye_inverse, m[_latch_moves];

		move_of_pose(sample->eye[eye].position, sample->eye[0].orientation, &view);
		move_inverse(&view, &view_inverse);
		move_inverse(&moves[eye], &eye_inverse);
		move_then(&eye_inverse, &view, &m[_latch_world]);
		move_then(&view_inverse, &m[_latch_world], &m[_latch_world]);
		m[_latch_sky] = m[_latch_world];
		m[_latch_sky].t[0] = m[_latch_sky].t[1] = m[_latch_sky].t[2] = 0.0;
		move_then(&hand, &view, &m[_latch_held]);
		move_then(&eye_inverse, &m[_latch_held], &m[_latch_held]);
		move_then(&view_inverse, &m[_latch_held], &m[_latch_held]);
		for (which = 0; which < _latch_moves; which++)
			move_matrix(&m[which], latch.view_move[which][eye]);
	}
}

/* an eye's transform moved: clip_from_eye move inverse(clip_from_eye)
transform; FALSE if it cannot be */
static BOOL eye_transform_move(const float clip_from_eye[16], const double move[4][4], const float transform[16],
	float out[16])
{
	double k[4][4], inverse[4][4], t[4][4];
	int i, j;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			k[i][j] = clip_from_eye[i * 4 + j];
			t[i][j] = transform[i * 4 + j];
		}
	}
	if (!matrix_inverse(k, inverse))
		return FALSE;
	matrix_multiply(move, inverse, inverse);
	matrix_multiply(k, inverse, k);
	matrix_multiply(k, t, t);
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			out[i * 4 + j] = (float)t[i][j];
	}
	return TRUE;
}

/* the screen's corrections for a point the game projected (its clip
position in the centre's view, in a viewport this wide and high), from the
eyes' transforms as moved: vr_render.c's vr_render_screen_point's */
static void screen_corrections_make(const float clip[4], const float viewport[2], float corrections[2][4])
{
	float made[2][4];
	int eye, axis, i;

	for (eye = 0; eye < 2; eye++)
	{
		const float *t = latch.transforms[eye];
		float moved[4], scale[2], centre_screen[2], eye_screen[2];

		for (i = 0; i < 4; i++)
			moved[i] = t[i * 4] * clip[0] + t[i * 4 + 1] * clip[1] + t[i * 4 + 2] * clip[2] + t[i * 4 + 3] * clip[3];
		if (moved[3] <= 0.0f)
			return;
		for (axis = 0; axis < 2; axis++)
		{
			const float step = 0.01f;
			float nudged = (moved[axis] + t[axis * 4 + axis] * step * clip[3]) / (moved[3] + t[12 + axis] * step * clip[3]);
			float centre_ndc = clip[axis] / clip[3], eye_ndc = moved[axis] / moved[3];

			scale[axis] = (nudged - eye_ndc) / step;
			if (axis == 0)
			{
				centre_screen[0] = ((centre_ndc + 1.0f) * viewport[0] - 1.0f) * 0.5f;
				eye_screen[0] = ((eye_ndc + 1.0f) * viewport[0] - 1.0f) * 0.5f;
			}
			else
			{
				centre_screen[1] = ((1.0f - centre_ndc) * viewport[1] - 1.0f) * 0.5f;
				eye_screen[1] = ((1.0f - eye_ndc) * viewport[1] - 1.0f) * 0.5f;
			}
		}
		made[eye][0] = scale[0];
		made[eye][1] = scale[1];
		made[eye][2] = eye_screen[0] - scale[0] * centre_screen[0];
		made[eye][3] = eye_screen[1] - scale[1] * centre_screen[1];
	}
	memcpy(corrections, made, sizeof(made));
}

/* the frame is submitted (the layers moved: halo_vr_present): the time
from the poses' locating to now */
static void latch_submitted(void)
{
	uint64_t now = monotonic_nanoseconds();

	if (latch.sampled)
		frame_pacing_latency_add(&latch.located_age, latch.located, now);
	if (latch.latched)
		frame_pacing_latency_add(&latch.latched_age, latch.latched, now);
	if (vr_frame.statistics && ++latch.frames >= 300)
	{
		float median, high, latched_median, latched_high;
		uint32_t count = frame_pacing_latency_take(&latch.located_age, &median, &high);
		uint32_t latched = frame_pacing_latency_take(&latch.latched_age, &latched_median, &latched_high);

		host_logf(HOST_LOG_INFO, "vr: pose age at submission (median, 95th percentile): as located %.2f, %.2f ms "
			"(%u frames); latched %.2f, %.2f ms (%u frames)", median, high, count, latched_median, latched_high,
			latched);
		latch.frames = 0;
	}
}

/* ---------- vr.h's, as the records said */

float halo_vr_foveation_inset(void)
{
	return vr_frame.foveation_inset;
}

const char *halo_vr_foveation_name(void)
{
	return vr_frame.foveation_name;
}

int halo_vr_foveation(float centres[2][2])
{
	if (!vr_frame.foveation)
		return 0;
	memcpy(centres, vr_frame.centres, sizeof(vr_frame.centres));
	return vr_frame.foveation;
}

float halo_vr_scope_zoom(float *height)
{
	*height = vr_frame.state.scope_zoom_height;
	return vr_frame.state.scope_zoom;
}

int halo_vr_scope_smartlink(float reticle[2])
{
	reticle[0] = vr_frame.state.scope_reticle[0];
	reticle[1] = vr_frame.state.scope_reticle[1];
	return (vr_frame.state.answered & XGPU_STREAM_VR_STATE_SMARTLINK) != 0;
}

int halo_vr_scope_window(struct halo_vr_scope_window *window)
{
	if (!(vr_frame.state.answered & XGPU_STREAM_VR_STATE_SCOPE_WINDOW))
		return 0;
	memcpy(window, vr_frame.state.scope_window, sizeof(*window));
	return 1;
}

void halo_vr_scope_readout(float *range, int *rounds_loaded, int *rounds_carried)
{
	*range = vr_frame.state.scope_range;
	*rounds_loaded = vr_frame.state.scope_rounds_loaded;
	*rounds_carried = vr_frame.state.scope_rounds_carried;
}

void halo_vr_scope_animation(float *opened_seconds, float *closing, float *switched_seconds)
{
	*opened_seconds = vr_frame.state.scope_animation[0];
	*closing = vr_frame.state.scope_animation[1];
	*switched_seconds = vr_frame.state.scope_animation[2];
}

void halo_vr_scope_shape(float *corner, float *dim, float *feather)
{
	*corner = vr_frame.state.scope_shape[0];
	*dim = vr_frame.state.scope_shape[1];
	*feather = vr_frame.state.scope_shape[2];
}

int halo_vr_scope_gun(void)
{
	return vr_frame.state.scope_gun;
}

int halo_vr_cockpit_window(struct halo_vr_cockpit_window *window, struct halo_vr_cockpit_display *display)
{
	if (!(vr_frame.state.answered & XGPU_STREAM_VR_STATE_COCKPIT))
		return 0;
	memcpy(window, vr_frame.state.cockpit_window, sizeof(*window));
	memcpy(display, vr_frame.state.cockpit_display, sizeof(*display));
	return 1;
}

float halo_vr_vignette(float fov[2][4])
{
	memcpy(fov, vr_frame.state.vignette_fov, sizeof(vr_frame.state.vignette_fov));
	return vr_frame.state.vignette;
}

int halo_vr_depth_wanted(void)
{
	return (vr_frame.state.answered & XGPU_STREAM_VR_STATE_DEPTH) != 0;
}

void halo_vr_depth_written(void)
{
	vr_frame.depth_written = TRUE;
}

void halo_vr_panel_size(int panel, int *width, int *height)
{
	const struct vr_host_info *info = host_vr_info();

	*width = info && panel >= 0 && panel < VR_PANEL_COUNT ? info->panel_width[panel] : 0;
	*height = info && panel >= 0 && panel < VR_PANEL_COUNT ? info->panel_height[panel] : 0;
}

/* the swapchain's image for this frame, acquired (the frame begun first) */
unsigned int halo_vr_image(int swapchain)
{
	const struct vr_host_info *info = host_vr_info();
	int index;

	if (!vr_frame.frame || !info || swapchain < 0 || swapchain >= VR_SWAPCHAIN_COUNT || !host_vr_frame_begin())
		return 0;
	index = host_vr_acquire(swapchain);
	if (index < 0 || index >= info->image_count[swapchain])
		return 0;
	vr_frame.acquired |= 1u << swapchain;
	return renderer->image(swapchain, index);
}

/* the frame ended with the game's layers, less those not acquired or not
drawn (vr.c's halo_vr_present's tests, here) */
void halo_vr_present(int eyes_drawn, int hud_drawn, int scope_drawn)
{
	const struct xgpu_stream_vr_submit *submit = vr_frame.submit;
	struct vr_host_layers *layers = &vr_frame.layers;
	int panel;

	if (!submit || !submit->waited)
		return;
	memcpy(layers, submit->layers, sizeof(*layers));
	memcpy(&vr_frame.head, submit->head, sizeof(vr_frame.head));
	layers->projection = layers->projection && eyes_drawn && (vr_frame.acquired & (1u << VR_SWAPCHAIN_EYES));
	layers->hud = layers->hud && hud_drawn && (vr_frame.acquired & (1u << VR_SWAPCHAIN_HUD));
	layers->depth = layers->depth && layers->projection && vr_frame.depth_written &&
		(vr_frame.acquired & (1u << VR_SWAPCHAIN_DEPTH));
	layers->scope = layers->scope && layers->projection && scope_drawn &&
		(vr_frame.acquired & (1u << VR_SWAPCHAIN_SCOPE));
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		layers->panels[panel].shown = layers->panels[panel].shown &&
			(vr_frame.acquired & (1u << (VR_SWAPCHAIN_PANEL + panel)));
	/* (the late latch's: the eyes drawn from where they are now, the scope
	at the eyepiece with the weapon) */
	if (latch.head)
	{
		int eye;

		for (eye = 0; eye < 2; eye++)
			pose_moved(&latch.eye_move[eye], &layers->eye[eye]);
	}
	if (latch.held && latch.held_drawn && layers->scope && layers->scope_eye >= 0)
		pose_moved(&latch.hand_move, &layers->scope_pose);
	/* (a frame none of whose images was acquired is begun to be ended) */
	latch_submitted();
	if (host_vr_frame_begin())
		host_vr_frame_end(layers);
	vr_frame.submitted = TRUE;
}

int halo_vr_submitted(struct vr_host_layers *layers, struct vr_host_pose *head)
{
	if (!vr_frame.submitted)
		return 0;
	*layers = vr_frame.layers;
	*head = vr_frame.head;
	return 1;
}

/* ---------- vr_visor.h's, as the records said */

void vr_visor_hud_image(struct vr_visor_hud_image *image)
{
	memcpy(image, vr_frame.state.hud_image, sizeof(*image));
}

int vr_visor_panels(struct vr_visor_panel panels[VR_PANEL_COUNT])
{
	if (!(vr_frame.state.answered & XGPU_STREAM_VR_STATE_PANELS))
		return 0;
	memcpy(panels, vr_frame.state.panels, VR_PANEL_COUNT * sizeof(panels[0]));
	return 1;
}

void vr_visor_image(struct vr_visor_image *image)
{
	memset(image, 0, sizeof(*image));
	memcpy(image, vr_frame.state.visor_image, sizeof(vr_frame.state.visor_image));
	/* (guest addresses: xgpu_host_texture_header) */
	image->noise_texture[0] = (void *)(uintptr_t)vr_frame.state.visor_noise[0];
	image->noise_texture[1] = (void *)(uintptr_t)vr_frame.state.visor_noise[1];
	image->time = vr_frame.state.visor_time;
}

int vr_visor_rim(struct vr_visor_rim *rim)
{
	if (!(vr_frame.state.answered & XGPU_STREAM_VR_STATE_RIM))
	{
		memset(rim, 0, sizeof(*rim));
		return 0;
	}
	memcpy(rim, vr_frame.state.rim, sizeof(*rim));
	return 1;
}

/* ---------- the screenshots (xgpu.h; d3d8_device.c's, in the guest) */

static unsigned long screenshot_number;

void xgpu_write_bmp(const char *name, const unsigned char *pixels, unsigned long width, unsigned long height,
	BOOL bottom_up)
{
	const char *directory = config_string("debug.screenshot_directory");
	unsigned char header[54] = { 'B', 'M' };
	uint32_t image_size = (uint32_t)(width * height * 4);
	unsigned long row;
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s%05lu.bmp", directory, name, screenshot_number);
	file = fopen(path, "wb");
	if (!file)
		return;
	*(uint32_t *)(header + 2) = 54 + image_size;
	*(uint32_t *)(header + 10) = 54;
	*(uint32_t *)(header + 14) = 40;
	*(int32_t *)(header + 18) = (int32_t)width;
	*(int32_t *)(header + 22) = bottom_up ? (int32_t)height : -(int32_t)height;
	*(uint16_t *)(header + 26) = 1;
	*(uint16_t *)(header + 28) = 32;
	*(uint32_t *)(header + 34) = image_size;
	fwrite(header, 1, sizeof(header), file);
	for (row = 0; row < height; row++)
		fwrite(pixels + row * width * 4, 1, width * 4, file);
	fclose(file);
}

/* ---------- the records */

static int stream_vr_supported(void *context)
{
	(void)context;
	return renderer->supported() != FALSE;
}

static void stream_vr_initialize(void *context, struct xgpu_stream_vr_info *wire)
{
	struct xgpu_vr_info info;

	(void)context;
	vr_frame.foveation_inset = wire->foveation_inset;
	memcpy(vr_frame.foveation_name, wire->foveation_name, sizeof(vr_frame.foveation_name));
	vr_frame.foveation_name[sizeof(vr_frame.foveation_name) - 1] = 0;
	memset(&info, 0, sizeof(info));
	info.eye_width = wire->eye_width;
	info.eye_height = wire->eye_height;
	info.hud_width = wire->hud_width;
	info.hud_height = wire->hud_height;
	info.scope_size = wire->scope_size;
	info.statistics = (wire->statistics & XGPU_STREAM_VR_STATISTICS) != 0;
	info.gpu_time = (wire->statistics & XGPU_STREAM_VR_GPU_TIME) != 0;
	vr_frame.statistics = info.statistics;
	renderer->initialize(&info);
	wire->foveated = info.foveated != 0;
	wire->fovea_width = info.fovea_width;
	wire->fovea_height = info.fovea_height;
	wire->sample_counting = renderer->sample_counting() != FALSE;
}

static void stream_vr_eyes(void *context, const struct xgpu_stream_vr_eyes *record)
{
	(void)context;
	vr_frame.frame = TRUE;
	vr_frame.foveation = record->foveation;
	memcpy(vr_frame.centres, record->centres, sizeof(vr_frame.centres));
	latch_take(record);
	/* (the game's image is the host's to acquire) */
	renderer->eyes(record->eye_target, halo_vr_image(VR_SWAPCHAIN_EYES));
}

static void stream_vr_pass(void *context, int32_t pass)
{
	(void)context;
	renderer->pass(pass);
}

static void stream_vr_groups_reset(void *context)
{
	(void)context;
	renderer->groups_reset();
}

static void stream_vr_timing(void *context, int32_t mark)
{
	(void)context;
	renderer->timing(mark);
}

static void frame_convert(const struct xgpu_stream_vr_frame *wire, struct xgpu_vr_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	frame->eyes_drawn = wire->eyes_drawn != 0;
	frame->hud_drawn = wire->hud_drawn != 0;
	frame->scope_drawn = wire->scope_drawn != 0;
	frame->hud_panels = wire->hud_panels != 0;
	frame->eye_target = wire->eye_target;
	frame->eye_depth = wire->eye_depth;
	frame->scope = wire->scope;
	frame->hud = wire->hud;
	frame->eye_depth_layers = wire->eye_depth_layers;
}

static void stream_vr_state(void *context, const struct xgpu_stream_vr_state *record)
{
	(void)context;
	vr_frame.state = *record;
	if (record->answered & XGPU_STREAM_VR_STATE_FRAME)
		vr_frame.frame = TRUE;
}

static void stream_vr_compose(void *context, const struct xgpu_stream_vr_frame *wire)
{
	struct xgpu_vr_frame frame;

	(void)context;
	frame_convert(wire, &frame);
	renderer->compose(&frame);
}

static void stream_vr_submit(void *context, const struct xgpu_stream_vr_submit *record)
{
	struct xgpu_vr_frame frame;

	(void)context;
	frame_convert(&record->record.frame, &frame);
	vr_frame.submit = record;
	vr_frame.submitted = FALSE;
	screenshot_number = record->shot;
	/* (halo_vr_present, above, ends the frame) */
	renderer->submit(&frame, record->record.screenshot != 0);
	vr_frame.submit = NULL;
	vr_frame.submitted = FALSE;
	vr_frame.frame = FALSE;
	vr_frame.foveation = 0;
	vr_frame.acquired = 0;
	vr_frame.depth_written = FALSE;
	latch.sampled = latch.head = latch.held = latch.held_drawn = latch.moved = FALSE;
	latch.latched = 0;
}

static void stream_vr_eye_transforms(void *context, const struct xgpu_stream_vr_eye_transforms *record)
{
	int which = record->latch == XGPU_STREAM_VR_LATCH_WORLD ? _latch_world :
		record->latch == XGPU_STREAM_VR_LATCH_SKY ? _latch_sky :
		record->latch == XGPU_STREAM_VR_LATCH_HELD ? _latch_held : -1;
	int eye;

	(void)context;
	memcpy(latch.transforms, record->transforms, sizeof(latch.transforms));
	latch.moved = which >= 0 && (latch.head || (which == _latch_held && latch.held));
	for (eye = 0; eye < 2 && latch.moved; eye++)
	{
		if (!eye_transform_move(record->clip_from_eye[eye], latch.view_move[which][eye], record->transforms[eye],
			latch.transforms[eye]))
			memcpy(latch.transforms[eye], record->transforms[eye], sizeof(latch.transforms[eye]));
	}
	if (latch.moved && which == _latch_held)
		latch.held_drawn = TRUE;
	renderer->eye_transforms((const float (*)[16])latch.transforms);
}

static void stream_vr_screen_corrections(void *context, const struct xgpu_stream_vr_screen_corrections *record)
{
	float corrections[2][4];

	(void)context;
	memcpy(corrections, record->corrections, sizeof(corrections));
	if (latch.moved && record->point[3] > 0.0f)
		screen_corrections_make(record->point, record->viewport, corrections);
	renderer->screen_corrections((const float (*)[4])corrections);
}

static void stream_vr_sky(void *context, int32_t sky)
{
	(void)context;
	renderer->sky(sky);
}

static void stream_vr_eye_feed(void *context, const float feed[7])
{
	(void)context;
	renderer->eye_feed(feed[0], feed[1], feed[2], feed[3], feed[4], feed[5], feed[6]);
}

static void stream_vr_eye_tint(void *context, const float tint[3])
{
	(void)context;
	renderer->eye_tint(tint[0], tint[1], tint[2]);
}

void host_vr_stream_backend(struct host_render_backend *backend, const struct host_vr_renderer *operations)
{
	renderer = operations;
	backend->vr_supported = stream_vr_supported;
	backend->vr_initialize = stream_vr_initialize;
	backend->vr_eyes = stream_vr_eyes;
	backend->vr_pass = stream_vr_pass;
	backend->vr_groups_reset = stream_vr_groups_reset;
	backend->vr_timing = stream_vr_timing;
	backend->vr_compose = stream_vr_compose;
	backend->vr_submit = stream_vr_submit;
	backend->vr_state = stream_vr_state;
	backend->vr_eye_transforms = stream_vr_eye_transforms;
	backend->vr_screen_corrections = stream_vr_screen_corrections;
	backend->vr_sky = stream_vr_sky;
	backend->vr_eye_feed = stream_vr_eye_feed;
	backend->vr_eye_tint = stream_vr_eye_tint;
	/* (OpenXR's calls that use the context from now on on the render
	thread: host_vr_render.h) */
	host_vr_render_thread();
}
