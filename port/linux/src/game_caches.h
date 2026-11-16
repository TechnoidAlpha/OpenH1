/*
GAME_CACHES.H

What the game keeps only to start and to load faster (game_caches.c):
measured, and cleared at the next start at the player's asking (Settings'
CLEAR CACHES).
*/

#ifndef __HALO_LINUX_GAME_CACHES_H
#define __HALO_LINUX_GAME_CACHES_H

/* the caches' bytes, the host's too (the disk's: walked, not for a frame) */
unsigned long long game_caches_bytes(void);
/* whether they are to be cleared at the next start */
int game_caches_requested(void);
/* their clearing at the next start asked for, or no longer */
void game_caches_request(int request);
/* (shell_xbox.c's main, before anything opens them) cleared, if asked */
void game_caches_start(void);

#endif
