/*
FRAMES_CLOCK.C

The frames' clock. With each frame's time fixed (debug.frame_time, and
debug.fixed_step's step a frame: main.c), what the game times by the
computer's clock outside the ticks runs on the frames' time instead, so
that two runs of the same input play the same frames and leave the same
game state (the determinism check, port/linux/README.md):

- the sound manager's clock (sound_manager.c), which times the sounds:
  their delays, fades and ends, and the sound classes' fades. Its choices
  reach the game state: the HUD's sounds' handles, the looping sounds'
  states and the slots they free, which an object's next looping sound
  takes, and the sound classes' gains;
- the mix, paced by the frames with no device (dsound_sdl.c), so that the
  sounds end on the same frame; and the sound cache's reads, waited for
  (xbox_sound_cache.c), so that a sound starts on the frame that asks for it;
- the network game's countdowns (network_server_manager.c), so that a
  network test's lobby (debug.network_test) runs as many ticks each time;
- the menus' clock (ui_widget.c), which their animations follow, so that
  two runs draw the same screen (tools/menu_gallery.py).

Without them the computer's clock times these, as it always has.
*/

/* port_config.c */
double config_real(const char *name);
int halo_fixed_step(void);
/* dsound_sdl.c */
void dsound_sdl_frame(double seconds);

static double frames_clock_seconds;

/* whether the frames' time is fixed, and the frames' time since the start,
in *milliseconds */
int frames_clock(
	unsigned long *milliseconds)
{
	static int checked, on;

	if (!checked)
	{
		checked = 1;
		on = config_real("debug.frame_time") > 0.0 || halo_fixed_step();
	}
	if (on && milliseconds)
		*milliseconds = (unsigned long)(frames_clock_seconds * 1000.0);
	return on;
}

/* (the main thread, each frame: main.c) the frame's time, and its sound
mixed */
void frames_clock_frame(
	double seconds)
{
	if (!frames_clock(NULL))
		return;
	frames_clock_seconds += seconds;
	dsound_sdl_frame(seconds);
}
