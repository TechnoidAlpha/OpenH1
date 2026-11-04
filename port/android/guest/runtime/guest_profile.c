/*
GUEST_PROFILE.C

The profile runtime of an instrumented guest (configure.py --pgo=generate),
which records the profile that profile-guided optimisation uses
(pgo/halo_linux.profdata; README.md, "Optimization profiles"). Only that
build links it.

clang's -fprofile-generate counts each function's blocks in the sections
__llvm_prf_cnts, describes the functions in __llvm_prf_data and names them
in __llvm_prf_names (tools/android_asm_convert.py gives a Mach-O compile's
the same ELF sections). compiler-rt's profile runtime, which would write
them out, is not built for the guests, so this writes them itself, at exit,
as its raw profile (version 10, LLVM's InstrProfData.inc), the file
HALO_PGO_FILE names (%p, the process id), which llvm-profdata merges. (The
hosts give the guest only the HALO_ part of their environment, so not
LLVM_PROFILE_FILE, which compiler-rt's runtime reads.)
Value profiling is off (-mllvm -disable-vp), so there are no value sites to
write, and no binary ids.
*/

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* (the compiler's references to the runtime, which this is) */
int __llvm_profile_runtime;

/* the profile's version and kind (IR), which every instrumented unit
defines */
extern const uint64_t __llvm_profile_raw_version;

/* the sections' bounds, which the link gives */
extern char __start___llvm_prf_data[] __attribute__((weak));
extern char __stop___llvm_prf_data[] __attribute__((weak));
extern char __start___llvm_prf_cnts[] __attribute__((weak));
extern char __stop___llvm_prf_cnts[] __attribute__((weak));
extern char __start___llvm_prf_names[] __attribute__((weak));
extern char __stop___llvm_prf_names[] __attribute__((weak));

/* a function's record, as the compiler lays it out for 32-bit pointers
(INSTR_PROF_DATA): only its size matters here */
struct guest_profile_data
{
	uint64_t name_ref;
	uint64_t function_hash;
	uint32_t counter_offset;
	uint32_t bitmap_offset;
	uint32_t function;
	uint32_t values;
	uint32_t counter_count;
	uint16_t value_site_counts[3];
	uint32_t bitmap_bytes;
};

/* the raw profile's header (INSTR_PROF_RAW_HEADER) */
struct guest_profile_header
{
	uint64_t magic;
	uint64_t version;
	uint64_t binary_ids_size;
	uint64_t data_count;
	uint64_t padding_before_counters;
	uint64_t counter_count;
	uint64_t padding_after_counters;
	uint64_t bitmap_bytes;
	uint64_t padding_after_bitmap;
	uint64_t names_size;
	uint64_t counters_delta;
	uint64_t bitmap_delta;
	uint64_t names_delta;
	uint64_t vtable_count;
	uint64_t vtable_names_size;
	uint64_t value_kind_last;
};

_Static_assert(sizeof(struct guest_profile_data) == 48, "a 32-bit profile record is 48 bytes");

/* INSTR_PROF_RAW_MAGIC_32: "\xfflprofR" */
#define GUEST_PROFILE_MAGIC_32 (((uint64_t)255 << 56) | ((uint64_t)'l' << 48) | ((uint64_t)'p' << 40) | \
	((uint64_t)'r' << 32) | ((uint64_t)'o' << 24) | ((uint64_t)'f' << 16) | ((uint64_t)'R' << 8) | (uint64_t)129)
#define GUEST_PROFILE_VALUE_KIND_LAST 2

static int guest_profile_write_all(int file, const void *data, size_t size)
{
	const char *bytes = data;

	while (size)
	{
		ssize_t written = write(file, bytes, size);

		if (written <= 0)
			return 0;
		bytes += written;
		size -= (size_t)written;
	}
	return 1;
}

static size_t guest_profile_padding(size_t size)
{
	return (8 - size % 8) % 8;
}

/* HALO_PGO_FILE's name, its %p the process id (default.profraw if it is
not set) */
static void guest_profile_file_name(char *name, size_t size)
{
	const char *pattern = getenv("HALO_PGO_FILE");
	size_t length = 0;

	if (!pattern || !*pattern)
		pattern = "default.profraw";
	for (; *pattern && length + 16 < size; pattern++)
	{
		if (pattern[0] == '%' && pattern[1] == 'p')
		{
			length += (size_t)snprintf(name + length, size - length, "%d", (int)getpid());
			pattern++;
		}
		else
		{
			name[length++] = *pattern;
		}
	}
	name[length] = '\0';
}

static void guest_profile_write(void)
{
	static const char zeros[8];
	const char *data = __start___llvm_prf_data;
	const char *counters = __start___llvm_prf_cnts;
	const char *names = __start___llvm_prf_names;
	size_t data_size = (size_t)(__stop___llvm_prf_data - __start___llvm_prf_data);
	size_t counters_size = (size_t)(__stop___llvm_prf_cnts - __start___llvm_prf_cnts);
	size_t names_size = (size_t)(__stop___llvm_prf_names - __start___llvm_prf_names);
	struct guest_profile_header header;
	char name[1024];
	int file;
	int written;

	if (!data || !data_size)
		return;
	memset(&header, 0, sizeof(header));
	header.magic = GUEST_PROFILE_MAGIC_32;
	header.version = __llvm_profile_raw_version;
	header.data_count = data_size / sizeof(struct guest_profile_data);
	header.counter_count = counters_size / sizeof(uint64_t);
	header.padding_after_counters = guest_profile_padding(counters_size);
	header.names_size = names_size;
	/* (as compiler-rt computes them, with the guest's 32-bit addresses) */
	header.counters_delta = (uint64_t)((uintptr_t)counters - (uintptr_t)data);
	header.bitmap_delta = (uint64_t)(0 - (uintptr_t)data);
	header.names_delta = (uint64_t)(uintptr_t)names;
	header.value_kind_last = GUEST_PROFILE_VALUE_KIND_LAST;

	guest_profile_file_name(name, sizeof(name));
	file = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (file < 0)
	{
		fprintf(stderr, "profile: cannot write %s\n", name);
		return;
	}
	written = guest_profile_write_all(file, &header, sizeof(header)) &&
		guest_profile_write_all(file, data, data_size) &&
		guest_profile_write_all(file, counters, counters_size) &&
		guest_profile_write_all(file, zeros, header.padding_after_counters) &&
		guest_profile_write_all(file, names, names_size) &&
		guest_profile_write_all(file, zeros, guest_profile_padding(names_size));
	close(file);
	fprintf(stderr, written ? "profile: wrote %s\n" : "profile: could not finish %s\n", name);
}

__attribute__((constructor)) static void guest_profile_initialize(void)
{
	atexit(guest_profile_write);
}
