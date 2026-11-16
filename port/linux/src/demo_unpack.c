/*
DEMO_UNPACK.C

A demo's block unpacked (demo_files.h) with the port's zlib, whose inflate
reads other people's data (port/third_party/zlib): a demo is a file anyone
may send. Apart from demo_files.c, whose writer packs with the game's own
deflate, so that the two zlibs' headers never meet.
*/

#include "../../third_party/zlib/zlib_prefixed.h"
#include "demo_files.h"

int demo_files_unpack(void const *packed, unsigned long packed_size, void *data, unsigned long size)
{
	uLongf written = (uLongf)size;

	if (!packed || !data || size > DEMO_BLOCK_MAXIMUM_SIZE)
		return 0;
	if (uncompress((Bytef *)data, &written, (Bytef const *)packed, (uLong)packed_size) != Z_OK)
		return 0;
	return written == (uLongf)size;
}
