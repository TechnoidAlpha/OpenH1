/*
PREPARED_SHIPPED.H

The stock maps' preparations' records shipped with the game: one file,
prepared.records (port/assets/prepared, made by tools/prepared_records.py on
a Linux machine from the stock maps: render states, texture references by
tag and bitmap index, mirror pages and shader ids, no map data), which the
builds put beside the game as they do brokers.txt (beside config.toml; the
Linux release's share/openh1; the macOS and iOS apps and the APK copy theirs
beside config.toml at each start). It is read once, when a record is first
looked for, and kept; each record is zlib's, inflated as it is played
(port/linux/game/render_prepared.c), never written out.

The file: "OH1S", its form (1) and its records' count, each 32 bits; then a
record's map's cache file checksum, its key (high, low), its offset in the
file, its compressed size and its size inflated, 32 bits each, for each
record; then the records.
*/

#ifndef PREPARED_SHIPPED_H
#define PREPARED_SHIPPED_H

#define PREPARED_SHIPPED_FILE "prepared.records"

/* the size of the shipped record of a map (its checksum) and key, inflated;
0 where none is shipped */
unsigned int prepared_shipped_size(unsigned int checksum, unsigned int key_high, unsigned int key_low);
/* ...inflated into a buffer of that size (the caller's, whose memory is the
game's): whether it inflates whole */
int prepared_shipped_inflate(unsigned int checksum, unsigned int key_high, unsigned int key_low, void *into,
	unsigned int size);

#endif
