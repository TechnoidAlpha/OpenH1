/* The loading picture never makes the game wait (port/android/host/
host_render_stream.c): its first is drawn as every later one is, once the
guest waiting for the loading's record no longer waits, or at most
LOADING_DEFERRED_MOST_NS later. */
#include "harness.h"
#include <stdint.h>

#include "enums.inc"

struct loading_presentation_frame
{
	unsigned long long epoch, serial, started_ns, elapsed_ns;
	int active, ready;
};
struct host_render_backend
{
	void *context;
	uint64_t (*loading)(void *context, struct loading_presentation_frame *frame);
};
struct xgpu_stream_ring { uint32_t committed, decoded; };
static struct { int guest_waiting; struct xgpu_stream_ring *ring; } render;
static struct xgpu_stream_ring ring;
static uint64_t now_ns = 1000000000ull;
static int pictures;

static uint64_t monotonic_nanoseconds(void) { return now_ns; }
#define host_load_trace(...) ((void)0)
#define host_stall_loading(active) ((void)(active))
#define loading_presentation_build(frame) ((void)0)
static uint64_t picture(void *context, struct loading_presentation_frame *frame)
{
	(void)context;
	(void)frame;
	pictures++;
	return 16666667ull;
}
static const struct host_render_backend backend = { NULL, picture };

#include "under_test.inc"

/* the guest sends the loading's record and waits for it, as
xgpu_backend_loading's synchronous submission does */
static void loading_requested(void)
{
	render.ring = &ring;
	render.guest_waiting = 1;
	loading_set(&backend, 1);
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";

	if (!strcmp(test, "waits"))
	{
		loading_requested();
		CHECK(pictures == 0, "the first picture was drawn while the guest waited for it (%d)", pictures);
		now_ns += 1000000ull;
		host_render_stream_loading_service();
		CHECK(pictures == 0, "drawn while the guest still waits (%d)", pictures);
		/* the record played and answered: the render thread's loop draws it */
		render.guest_waiting = 0;
		host_render_stream_loading_service();
		CHECK(pictures == 1, "the first picture was not drawn once the guest went on (%d)", pictures);
		return 0;
	}
	if (!strcmp(test, "deadline"))
	{
		/* a guest that keeps waiting still sees the picture within the deferral */
		loading_requested();
		now_ns += LOADING_DEFERRED_MOST_NS - 1;
		host_render_stream_loading_service();
		CHECK(pictures == 0, "drawn before the deferral ran out (%d)", pictures);
		now_ns += 2;
		host_render_stream_loading_service();
		CHECK(pictures == 1, "not drawn once the deferral ran out (%d)", pictures);
		return 0;
	}
	if (!strcmp(test, "idle"))
	{
		/* nothing waits: drawn at once, as before */
		render.ring = &ring;
		loading_set(&backend, 1);
		CHECK(pictures == 1, "not drawn at once with nothing waiting (%d)", pictures);
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
