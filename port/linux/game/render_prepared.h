/*
RENDER_PREPARED.H

A map's preparation recorded and played back (render_prepared.c).
*/

#ifndef __HALO_PORT_RENDER_PREPARED_H
#define __HALO_PORT_RENDER_PREPARED_H

#include "cseries.h"

/* the map's preparation made from its record, in a frame about to be
prepared (mirrors: whether it draws a mirror's window): TRUE, or FALSE
where there is none to play (its walk to be made, and recorded) */
boolean render_prepared_replay(boolean mirrors);
/* a shipped record (prepared_shipped.h) of a map (its cache file's
checksum) and key that plays: not passed over, whole and of this form */
boolean render_prepared_shipped(unsigned long checksum, unsigned long long key);
/* a map not loaded yet (its checksum and scenario, levels\a10\a10) has a
shipped record of the settings now: its first load needs no walk */
boolean render_prepared_shipped_map(unsigned long checksum, char const *scenario);
/* the stock maps' preparations recorded by the first start's pass in this
form (menu_pictures_render.c), and noted so at its end */
boolean render_prepared_stock_recorded(void);
void render_prepared_stock_done(void);
/* the walk recorded, from its preparation's beginning to its end */
void render_prepared_record_begin(boolean mirrors);
void render_prepared_record_end(void);
/* of the draws a record has (their hashes, in order), those the draws made
(in order) do not have */
unsigned long render_prepared_draws_missing(unsigned long long const *recorded, unsigned long recorded_count,
	unsigned long long const *made, unsigned long made_count);
/* a record played back is good: well formed (formed), its objects all found
(unknown 0), and, under the settings, backend and GPU it was made with, its
draws all made (missing 0) */
boolean render_prepared_played(int formed, unsigned long unknown, unsigned long missing, boolean same_derivation);
/* what the settings, backend and GPU now make of a map's draws (a hash of
the settings that change them, render_prepared.c's prepared_settings): a
map prepared under another is prepared again (render_enumerate.c) */
unsigned long long render_prepared_derivation(void);

#endif
