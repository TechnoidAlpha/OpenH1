/*
STRUCTURE_BSP_CACHE.H

The map's structure bsps, each kept as it was read and checked
(structure_bsp_cache.c).
*/

#ifndef STRUCTURE_BSP_CACHE_H
#define STRUCTURE_BSP_CACHE_H

#include "cseries.h"

struct scenario_structure_bsp_reference;
struct structure_lightmap;
struct structure_material;

/* the bsp's copy put back where it loads, as it was read and checked, and
the rest of the region bsps load in (region_size bytes at region; none, a
Custom Edition map's) as a read leaves it (0xCD): TRUE; or FALSE with none
kept, for the caller to fill the region and read the bsp */
boolean structure_bsp_cache_restore(struct scenario_structure_bsp_reference const *reference, void *region,
	unsigned long region_size);
/* the bsp where it loads, just read and checked, kept (if there is room) */
void structure_bsp_cache_keep(struct scenario_structure_bsp_reference const *reference);
/* whether another bsp may be kept */
boolean structure_bsp_cache_room(void);
/* each kept bsp's lightmaps' materials, read from its copy (where they are
in it: their pointers lead into the copy, not where the bsp loads) */
void structure_bsp_cache_materials(
	void (*visit)(void *context, struct structure_lightmap const *lightmap, struct structure_material const *material),
	void *context);
/* every copy let go of (the map's scenario unloaded) */
void structure_bsp_cache_dispose(void);

#endif
