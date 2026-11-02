/*
LOAD_ZONE.H

A load zone's switch's frame started early enough for its refresh
(load_zone.c).
*/

#ifndef LOAD_ZONE_H
#define LOAD_ZONE_H

/* a switch asked for (main_switch_structure_bsp), and made */
void load_zone_switch_requested(void);
void load_zone_switch_begin(void);
void load_zone_switch_end(void);
/* any switch made (scenario_switch_structure_bsp): the renderer's copies of
the old structure's pages its draws read made the new one's at once */
void load_zone_structure_bsp_switched(void);

#endif
