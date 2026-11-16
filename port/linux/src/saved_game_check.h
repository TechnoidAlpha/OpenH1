/*
SAVED_GAME_CHECK.H

The check of the campaign's saved game made on a thread of its own, ahead
of the menus' screens that ask for it (saved_game_check.c).
*/

#ifndef SAVED_GAME_CHECK_H
#define SAVED_GAME_CHECK_H

/* the saved game at xbox_path: its header (header_size bytes, its checksum
at checksum_offset) and whether the sum of its first buffer_size bytes is
that checksum (1), as game_state_xbox.c checks it; corrupted if not and the
header has a checksum. The check of the file as it is now (its size and
time of change), made ahead, or waited for */
int saved_game_check(const char *xbox_path, void *header, long header_size, long checksum_offset,
	long buffer_size, int *corrupted);
/* (the main menu's, each frame) the check of the file begun, if it has
changed since the last: looked at once a second */
void saved_game_check_ahead(const char *xbox_path, long header_size, long checksum_offset, long buffer_size);

#endif
