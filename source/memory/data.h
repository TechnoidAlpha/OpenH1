/*
DATA.H

header included in hcex build.
*/

#ifndef __DATA_H
#define __DATA_H
#pragma once

/* ---------- headers */

#include "tag_files.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct data_array
{
	char name[TAG_STRING_LENGTH+1];
	short maximum_count;
	short size;
	boolean valid;
	boolean identifier_zero_invalid;
	unsigned long signature;
	short first_free_absolute_index;
	short count;
	short actual_count;
	short next_identifier;
	void *data;
};

struct datum_header
{
	short identifier;
};

struct data_iterator
{
	struct data_array *data;
	short absolute_index;
	short pad;
	long datum_index;
	unsigned long signature;
};

typedef char data_iterator_size_assert[
	sizeof(struct data_iterator) == 0x10 ? 1 : -1];

/* ---------- prototypes/DATA.C */

long data_allocation_size(short maximum_count, short size);
void data_initialize(struct data_array *data, const char *name, short maximum_count, short size);
void *datum_try_and_get(struct data_array *data, long index);
void data_verify(struct data_array *data);
/* port: datum_get where its fast path (data.h) finds nothing */
void *datum_get_slow(struct data_array *data, long index);
struct data_array *data_new(const char *name, short maximum_count, short size);
void data_dispose(struct data_array *data);
void data_make_invalid(struct data_array *data);
long datum_new_at_index(struct data_array *data, long index);
long datum_new(struct data_array *data);
void datum_delete(struct data_array *data, long index);
void data_delete_all(struct data_array *data);
void data_iterator_new(struct data_iterator *iterator, struct data_array *data);
void *data_iterator_next(struct data_iterator *iterator);
long data_next_index(struct data_array *data, long index);
long data_prev_index(struct data_array *data, long index);
void data_compact(struct data_array *data);
void data_make_valid(struct data_array *data);

/* port: datum_get without a call where all it checks holds (a large share
of the simulation's time was the call), and the function where something
does not, which asserts and reports as datum_get always has (__inline__:
msvc_comdat.c makes __inline weak, which a static function cannot be) */
static __inline__ void *datum_get(
	struct data_array *data,
	long index)
{
	short identifier = index>>16;
	short absolute_index = index;

	if (data->valid &&
		(identifier || !data->identifier_zero_invalid) &&
		absolute_index>=0 && absolute_index<data->count)
	{
		struct datum_header *header = (struct datum_header *)((char *)data->data+data->size*absolute_index);

		if (header->identifier && (!identifier || identifier==header->identifier))
		{
			return header;
		}
	}

	return datum_get_slow(data, index);
}

/* ---------- globals */

/* ---------- public code */

#endif // __DATA_H
