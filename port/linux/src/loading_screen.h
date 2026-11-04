/*
LOADING_SCREEN.H

The loading screen (the Xbox's: a light sweeping over the main menu's ring,
or the bundled picture, its trail glowing, the monks' chant under it) for a
wait of the port's own, such as the menus' pictures being prepared
(port/linux/src/sdl_platform.c). The presenter draws it on its own clock
while the game's thread works, as it does while a map loads.

	halo_loading_screen_begin(TRUE);
	for (each step of the work)
		halo_loading_screen_progress(done / total, "Preparing menu pictures");
	halo_loading_screen_progress(1.0f, NULL);   (it fades out over half a second)
	halo_loading_screen_end();

Nothing is drawn without a window, or while a map load has the screen.
*/

#ifndef LOADING_SCREEN_H
#define LOADING_SCREEN_H

/* Shows the loading screen. follow_progress: the sweep crosses once as the
progress goes from 0 to 1, as the Xbox's did (its position the progress
smoothed, with its slow wobble, its trail fading over the last tenth), and
the chant swells with it; else the sweep loops every four seconds, as a map
load's does. The main menu's ring is drawn if the main menu is up, else
the bundled picture. */
void halo_loading_screen_begin(int follow_progress);

/* The wait's progress, 0 to 1 (the screen fades out once it reaches 1),
and a line under the words (NULL or "": none; it stays until changed). */
void halo_loading_screen_progress(float progress, char const *caption);

/* Gives the screen back: the next picture the game draws is shown. */
void halo_loading_screen_end(void);

#endif
