/* The host's optional campaign vehicle copies (coop_vehicles.c). */
#ifndef __COOP_VEHICLES_H
#define __COOP_VEHICLES_H

struct scenario_object_datum;
void coop_vehicles_new_game(void);
void coop_vehicles_register(long object_index, struct scenario_object_datum const *source);
void coop_vehicles_update(void);
void coop_vehicles_before_save(void);
void coop_vehicles_after_load(void);
/* the host's vehicles and choices, by part, for the demos (demo.c's G
records: network_coop_port_demo_state); NULL past the last */
void *coop_vehicles_port_demo_state(short part, long *size);

#endif
