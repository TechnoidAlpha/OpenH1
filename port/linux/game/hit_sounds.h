/* The hit sounds (audio.hit_sounds, hit_sounds.c): a short beep as this
machine's player damages another player, as the host dealt it. */
#ifndef HIT_SOUNDS_H
#define HIT_SOUNDS_H

/* damage dealt, as the machine that decides it dealt it: the host's (or a
game of this machine's own) as it deals it, a client's as the host's word on
it comes (network_damage.c). The players are datum indices (NONE: none); a
beep if the attacker is one of this machine's players (no bot), the victim
another player, the damage some (or a kill), the setting on and the host
allowing it */
void hit_sounds_note_damage(long attacker_player_index, long victim_player_index, float shield_damage,
	float body_damage, unsigned long being_damaged_flags);

#endif
