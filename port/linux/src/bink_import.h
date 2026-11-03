/*
BINK_IMPORT.H

The Xbox's movies copied into a data root that has none (bink_import.c).
*/

#ifndef BINK_IMPORT_H
#define BINK_IMPORT_H

/* (between frames: halo_screen_commit) with display.movies on and no movies
in the data root's bink folder, copies them once from the disc image the
maps came from, if it is still where it was, in a thread of its own */
void bink_import_update(void);

#endif
