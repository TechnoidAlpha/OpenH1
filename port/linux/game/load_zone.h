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

#endif
