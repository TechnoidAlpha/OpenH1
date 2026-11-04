/*
LOAD_ZONE.C

A load zone's switch of structure bsp (main_switch_structure_bsp, from the
level's scripts) is made at the start of the frame after the one it was
asked for in (main_switch_to_structure_bsp_private, main.c). Its work
(the bsp copied back from structure_bsp_cache.c, the objects reconnected
to it) is a few milliseconds the frames around it do not have, and the
host starts each frame just in time for its refresh, by what the frames
before took (frame_pacing.h): the frame with the switch would miss its
refresh. So from the frame it is asked for in until it is made, the host
is told that the next frame takes LOAD_ZONE_ESTIMATE_FACTOR times the
longest switch so far (LOAD_ZONE_LEAST_MICROSECONDS at least) more than
the frames do (xgpu_backend_frame_extra), and starts it that much earlier.
Only the frames' times change: the switch is made where it was.
*/

#include "cseries.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "load_zone.h"

/* the platform layer's */
void xgpu_backend_frame_extra(unsigned long microseconds);
void d3d8_mirror_rewritten(unsigned long address, unsigned long size);
unsigned long long SDL_GetTicksNS(void);
void halo_load_trace(char const *phase, char const *detail);

#define LOAD_ZONE_LEAST_MICROSECONDS 3000UL
#define LOAD_ZONE_ESTIMATE_FACTOR 2UL

static struct
{
	unsigned long longest_microseconds;
	unsigned long long started;
} load_zone;

static unsigned long load_zone_estimate(
	void)
{
	return MAX(LOAD_ZONE_LEAST_MICROSECONDS, LOAD_ZONE_ESTIMATE_FACTOR * load_zone.longest_microseconds);
}

void load_zone_switch_requested(
	void)
{
	xgpu_backend_frame_extra(load_zone_estimate());
}

void load_zone_switch_begin(
	void)
{
	halo_load_trace("bsp.switch.request", NULL);
	load_zone.started = SDL_GetTicksNS();
}

void load_zone_switch_end(
	void)
{
	unsigned long microseconds = (unsigned long)((SDL_GetTicksNS() - load_zone.started) / 1000ULL);

	if (microseconds > load_zone.longest_microseconds)
		load_zone.longest_microseconds = microseconds;
	xgpu_backend_frame_extra(0);
	halo_load_trace("bsp.switch.made", NULL);
}

void load_zone_structure_bsp_switched(
	void)
{
	struct scenario *scenario = global_scenario_try_and_get();
	short index = global_structure_bsp_index_get();

	if (scenario && index >= 0 && index < scenario->structure_bsp_references.count)
	{
		struct scenario_structure_bsp_reference *reference = TAG_BLOCK_GET_ELEMENT(
			&scenario->structure_bsp_references, index, struct scenario_structure_bsp_reference);

		if (reference->file_size > 0)
			d3d8_mirror_rewritten((unsigned long)reference->base_address, (unsigned long)reference->file_size);
		halo_load_trace("bsp.mirror.end", NULL);
	}
}
