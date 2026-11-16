/*
TEXTURE_CACHE.H

header included in hcex build.
*/

#ifndef __TEXTURE_CACHE_H
#define __TEXTURE_CACHE_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct bitmap_data;

/* ---------- prototypes/TEXTURE_CACHE.C */

void *_texture_cache_bitmap_get_hardware_format(
	struct bitmap_data *bitmap,
	boolean block,
	boolean load);
void *texture_cache_bitmap_load(
	struct bitmap_data *bitmap);
void texture_cache_bitmap_delete(
	struct bitmap_data *bitmap);
void texture_cache_new(
	void);
void texture_cache_close(
	void);
void texture_cache_delete(
	void);
void texture_cache_open(
	void);
void texture_cache_idle(
	void);
/* port: bounded preparation batches keep currently bound textures live. */
void texture_cache_preparation_begin(void);
/* port: a cached bitmap's texture as a map's preparation's journal keeps it
(its tag and index) and its texture again, loaded */
int texture_cache_port_identify(void const *texture, unsigned long *tag_index, unsigned long *bitmap_index);
void *texture_cache_port_load(unsigned long tag_index, unsigned long bitmap_index);
void texture_cache_return_memory(
	void);
void texture_cache_flush(
	void);
void *texture_cache_steal_memory(
	long size);
void texture_cache_debug_render(
	void);
/* port: the pages a map's textures may take, and the map's set as it
loads and as mods' bitmaps change (halo_port_capacity.h) */
long texture_cache_map_page_count(
	boolean custom_edition,
	boolean mod_bitmaps,
	unsigned long long memory_bytes);
void texture_cache_size_for_map(
	void);
unsigned long texture_cache_mod_bitmap_size_limit(
	void);

/* ---------- globals */

/* ---------- public code */

#endif // __TEXTURE_CACHE_H
