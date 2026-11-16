/*
HOST_LOADER.C

Loads the guest image: a statically linked ELF executable of ILP32 code
(see tools/android_build.py) whose segments are copied to the guest
addresses it was linked at (in the low 4 GB, or at the guest's base:
host.h). On AArch64 hosts it is a 64-bit AArch64 ELF file
(arm64_32 code); on x86-64 hosts a 32-bit x86-64 one (x32). The image starts
with a struct halo_guest_header naming its import table, which is filled
with the host functions of the same names.

The file is checked before anything is mapped (host_image_validate), and
its header, import table and names before they are used
(host_image_validate_imports): every offset, size and address must lie in
the file and in the range the host reserves for the image.

On macOS the code is mapped for just-in-time compilation (MAP_JIT): under
the hardened runtime of a signed app, memory that was written cannot be made
executable with mprotect (the process is killed when it runs it), unless the
app has the entitlement for unsigned executable memory, which allows it for
all memory. MAP_JIT memory needs only com.apple.security.cs.allow-jit
(port/macos/halo.entitlements). It cannot be mapped at a fixed address, so
the code's pages are unmapped from the guest's reservation first and mapped
again at the same address.

iOS gives apps no memory that was writable and is executable, MAP_JIT's
included (outside a debugger). There the image is part of the app's signed
executable (port/ios/guest_image.S puts it in a section of the executable's
__TEXT, on a page boundary), and its read-only part (the header, the code and
the constants, below the first writable segment) is mapped from the
executable's file at the guest's addresses: the pages the kernel checks
against the executable's signature, nothing written or made executable by
the host. Only the writable segments are copied (with the import table, in
the image's .data). The guest's base is a multiple of 4 GB wherever its
memory lies (host_memory_base.c), so the code's PC-relative references to
its data find them at base plus their addresses, as on macOS.
*/

#include "host.h"

#if defined(__APPLE__) || defined(_WIN32)
/* (macOS and Windows have no elf.h: the little of it the loader reads) */
#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define EI_CLASS 4
#define EI_DATA 5
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PF_X 1
#define PF_W 2

typedef struct
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct
{
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint32_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct
{
	uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;
#else
#include <elf.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <pthread.h>
#include <stdlib.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#define HOST_IMAGE_FROM_FILE 1
#endif

/* more than the guest has (its own, the POSIX and the GL imports) */
#define IMPORTS_MAXIMUM 16384

struct host_guest_image host_image;

static int fail(const char *format, ...) __attribute__((format(printf, 1, 2)));

static int fail(const char *format, ...)
{
	char text[256];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	host_logf(HOST_LOG_ERROR, "the guest image %s", text);
	return -1;
}

static void missing_import(void)
{
	host_fatal("the guest called a host function that is not available");
}

#if defined(__APPLE__) && !defined(HOST_IMAGE_FROM_FILE)
/* the code at address made executable through MAP_JIT memory; 0 if it
cannot be, and the pages are left as they were */
static int code_map_jit(void *address, size_t size)
{
	void *copy = malloc(size);
	void *mapped;

	if (!copy)
		return 0;
	memcpy(copy, address, size);
	munmap(address, size);
	mapped = mmap(address, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (mapped != address)
	{
		if (mapped != MAP_FAILED)
			munmap(mapped, size);
		/* (the reservation's hole filled again) */
		mapped = mmap(address, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
		if (mapped == address)
			memcpy(address, copy, size);
		free(copy);
		return 0;
	}
	/* written with this thread's writes allowed; every thread executes it
	(the default of each thread) */
	pthread_jit_write_protect_np(0);
	memcpy(address, copy, size);
	pthread_jit_write_protect_np(1);
	free(copy);
	return 1;
}
#endif

/* ---------- the ELF file */

/* the fields of the file header and of a program header the loader uses,
in either class */
struct file_header
{
	uint16_t type, machine;
	uint32_t version;
	uint64_t program_offset;
	uint16_t program_entry_size, program_count;
};

struct program_header
{
	uint32_t type, flags;
	uint64_t offset, address, file_size, memory_size;
};

static int read_file_header(const unsigned char *file, size_t size, int elf_class, struct file_header *header)
{
	if (elf_class == ELFCLASS64)
	{
		Elf64_Ehdr elf;

		if (size < sizeof(elf))
			return fail("is too short for an ELF header (%zu bytes)", size);
		memcpy(&elf, file, sizeof(elf));
		header->type = elf.e_type;
		header->machine = elf.e_machine;
		header->version = elf.e_version;
		header->program_offset = elf.e_phoff;
		header->program_entry_size = elf.e_phentsize;
		header->program_count = elf.e_phnum;
		return elf.e_phentsize == sizeof(Elf64_Phdr) ? 0 : fail("has program headers of %u bytes", elf.e_phentsize);
	}
	else
	{
		Elf32_Ehdr elf;

		if (size < sizeof(elf))
			return fail("is too short for an ELF header (%zu bytes)", size);
		memcpy(&elf, file, sizeof(elf));
		header->type = elf.e_type;
		header->machine = elf.e_machine;
		header->version = elf.e_version;
		header->program_offset = elf.e_phoff;
		header->program_entry_size = elf.e_phentsize;
		header->program_count = elf.e_phnum;
		return elf.e_phentsize == sizeof(Elf32_Phdr) ? 0 : fail("has program headers of %u bytes", elf.e_phentsize);
	}
}

static void read_program_header(const unsigned char *entry, int elf_class, struct program_header *program)
{
	if (elf_class == ELFCLASS64)
	{
		Elf64_Phdr segment;

		memcpy(&segment, entry, sizeof(segment));
		program->type = segment.p_type;
		program->flags = segment.p_flags;
		program->offset = segment.p_offset;
		program->address = segment.p_vaddr;
		program->file_size = segment.p_filesz;
		program->memory_size = segment.p_memsz;
	}
	else
	{
		Elf32_Phdr segment;

		memcpy(&segment, entry, sizeof(segment));
		program->type = segment.p_type;
		program->flags = segment.p_flags;
		program->offset = segment.p_offset;
		program->address = segment.p_vaddr;
		program->file_size = segment.p_filesz;
		program->memory_size = segment.p_memsz;
	}
}

int host_image_validate(const void *file, size_t size, int elf_class, int machine, struct host_image_layout *layout)
{
	const unsigned char *bytes = file;
	const uint64_t reserved_end = (uint64_t)HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE;
	struct file_header header;
	uint64_t low = ~0ULL, high = 0;
	uint32_t index;

	memset(layout, 0, sizeof(*layout));
	if (!file || size < EI_NIDENT || memcmp(bytes, ELFMAG, SELFMAG))
		return fail("is not an ELF file");
	if (bytes[EI_CLASS] != elf_class)
		return fail("is a %s ELF file; this host needs a %s one", bytes[EI_CLASS] == ELFCLASS64 ? "64-bit" : "32-bit",
			elf_class == ELFCLASS64 ? "64-bit" : "32-bit");
	if (bytes[EI_DATA] != ELFDATA2LSB)
		return fail("is not little-endian");
	if (read_file_header(bytes, size, elf_class, &header) != 0)
		return -1;
	if (header.machine != machine)
		return fail("is for machine %u; this host needs machine %d", header.machine, machine);
	if (header.type != ET_EXEC)
		return fail("is not an executable (type %u)", header.type);
	if (header.version != EV_CURRENT)
		return fail("has ELF version %u", header.version);
	if (!header.program_count || header.program_count > 64)
		return fail("has %u program headers", header.program_count);
	if (header.program_offset > size ||
		(uint64_t)header.program_count * header.program_entry_size > size - header.program_offset)
	{
		return fail("has its program headers outside the file");
	}

	for (index = 0; index < header.program_count; index++)
	{
		struct program_header program;
		struct host_image_segment *segment;

		read_program_header(bytes + header.program_offset + (uint64_t)index * header.program_entry_size, elf_class,
			&program);
		if (program.type == PT_DYNAMIC || program.type == PT_INTERP)
			return fail("is not static: it has a %s segment", program.type == PT_DYNAMIC ? "PT_DYNAMIC" : "PT_INTERP");
		if (program.type != PT_LOAD)
			continue;
		if (program.file_size > program.memory_size)
			return fail("segment %u is larger in the file than in memory", index);
		if (program.offset > size || program.file_size > size - program.offset)
			return fail("segment %u lies outside the file", index);
		if (program.address < HALO_GUEST_IMAGE_BASE || program.address > reserved_end ||
			program.memory_size > reserved_end - program.address)
		{
			return fail("segment %u (%llx, %llx bytes) is outside its range %08x-%08llx", index,
				(unsigned long long)program.address, (unsigned long long)program.memory_size,
				HALO_GUEST_IMAGE_BASE, (unsigned long long)reserved_end);
		}
		if (layout->segment_count == HOST_IMAGE_SEGMENTS_MAXIMUM)
			return fail("has more than %d loadable segments", HOST_IMAGE_SEGMENTS_MAXIMUM);
		segment = &layout->segments[layout->segment_count++];
		segment->offset = program.offset;
		segment->address = program.address;
		segment->file_size = program.file_size;
		segment->memory_size = program.memory_size;
		segment->flags = program.flags;
		if (program.address < low)
			low = program.address;
		if (program.address + program.memory_size > high)
			high = program.address + program.memory_size;
	}
	if (!layout->segment_count)
		return fail("has no loadable segment");
	low &= ~0xfffULL;
	high = (high + 0xfff) & ~0xfffULL;
	if (low != HALO_GUEST_IMAGE_BASE || high - low < sizeof(struct halo_guest_header))
		return fail("spans %llx-%llx", (unsigned long long)low, (unsigned long long)high);
	layout->low = low;
	layout->high = high;
	return 0;
}

/* ---------- the header and the import table */

static int inside(uint32_t base, uint32_t end, uint32_t address, uint64_t size)
{
	return address >= base && address <= end && size <= (uint64_t)(end - address);
}

static int executable(const struct host_image_layout *layout, uint32_t address)
{
	uint32_t index;

	for (index = 0; index < layout->segment_count; index++)
	{
		const struct host_image_segment *segment = &layout->segments[index];

		if ((segment->flags & PF_X) && address >= segment->address &&
			address - segment->address < segment->memory_size)
		{
			return 1;
		}
	}
	return 0;
}

int host_image_validate_imports(const uint8_t *memory, uint32_t base, uint32_t end,
	const struct host_image_layout *layout, uint32_t *count)
{
	struct halo_guest_header header;
	uint32_t names, index;

	if (end < base || end - base < sizeof(header))
		return fail("is too small for its header");
	memcpy(&header, memory, sizeof(header));
	if (header.magic != HALO_GUEST_MAGIC || header.abi_version != HALO_GUEST_ABI_VERSION)
		return fail("header does not match this host (ABI version %u, the host's %d)", header.abi_version,
			HALO_GUEST_ABI_VERSION);
	if (!executable(layout, header.start) || !executable(layout, header.thread_start) ||
		!executable(layout, header.thread_attach) || !executable(layout, header.thread_detach))
	{
		return fail("header names an entry point outside its code");
	}
	if (header.init_array_start != header.init_array_end && (header.init_array_start > header.init_array_end ||
		(header.init_array_start & 3) ||
		!inside(base, end, header.init_array_start, header.init_array_end - header.init_array_start)))
	{
		return fail("header's initializers (%08x-%08x) are outside the image", header.init_array_start,
			header.init_array_end);
	}
	if ((header.import_count & 3) || !inside(base, end, header.import_count, sizeof(uint32_t)))
		return fail("header's import count (at %08x) is outside the image", header.import_count);
	memcpy(count, memory + (header.import_count - base), sizeof(*count));
	if (*count > IMPORTS_MAXIMUM)
		return fail("has %u imports (at most %d)", *count, IMPORTS_MAXIMUM);
	if ((header.import_table & 7) || !inside(base, end, header.import_table, (uint64_t)*count * 8))
		return fail("import table (%u entries at %08x) is outside the image", *count, header.import_table);
	names = header.import_names;
	for (index = 0; index < *count; index++)
	{
		const uint8_t *terminator;

		if (!inside(base, end, names, 1))
			return fail("import name %u is outside the image", index);
		terminator = memchr(memory + (names - base), 0, end - names);
		if (!terminator)
			return fail("import name %u is not terminated", index);
		if (terminator == memory + (names - base))
			return fail("import name %u is empty", index);
		names += (uint32_t)(terminator - (memory + (names - base))) + 1;
	}
	return 0;
}

/* ---------- the read-only part, mapped from a file */

int host_image_read_only_range(const struct host_image_layout *layout, uint64_t page, uint64_t file_offset,
	uint64_t *start, uint64_t *end, uint64_t *position)
{
	uint64_t writable = ~0ULL, read_only_end = 0, distance = 0;
	uint32_t index;
	int found = 0;

	/* (the lowest writable segment: everything below it is the read-only
	part) */
	for (index = 0; index < layout->segment_count; index++)
	{
		const struct host_image_segment *segment = &layout->segments[index];

		if ((segment->flags & PF_W) && segment->address < writable)
			writable = segment->address;
	}
	for (index = 0; index < layout->segment_count; index++)
	{
		const struct host_image_segment *segment = &layout->segments[index];
		/* (the file's place of the segment's guest address 0, as an
		unsigned difference) */
		uint64_t at = file_offset + segment->offset - segment->address;

		if (segment->address >= writable)
			continue;
		if (segment->flags & PF_W)
			return fail("segment %u is writable below the first writable one", index);
		if (segment->file_size != segment->memory_size)
			return fail("read-only segment %u is larger in memory than in the file", index);
		if (segment->address + segment->memory_size > writable)
			return fail("read-only segment %u overlaps a writable one", index);
		if (found && at != distance)
			return fail("read-only segment %u is not where the others are in the file", index);
		distance = at;
		found = 1;
		if (segment->address + segment->memory_size > read_only_end)
			read_only_end = segment->address + segment->memory_size;
	}
	if (!found)
		return fail("has no read-only segment");
	*start = layout->low;
	*end = (read_only_end + page - 1) & ~(page - 1);
	*position = distance + *start;
	if ((*start & (page - 1)) || (*position & (page - 1)))
		return fail("read-only part (%llx in the file) is not on a page boundary of %llu bytes",
			(unsigned long long)*position, (unsigned long long)page);
	if (writable != ~0ULL && *end > writable)
		return fail("first writable segment (%llx) shares a page with the read-only part (to %llx)",
			(unsigned long long)writable, (unsigned long long)read_only_end);
	return 0;
}

#ifdef HOST_IMAGE_FROM_FILE
/* the image's read-only part mapped from the executable that holds the
image (file, in its __TEXT, whose bytes are the file's from the Mach-O
header on), over the copy at the guest's addresses */
static int code_map_file(const void *file, size_t size, const struct host_image_layout *layout, uint64_t page)
{
	Dl_info information;
	uint64_t offset, start, end, position;
	unsigned char check[4096];
	void *mapped;
	int descriptor;

	if (!dladdr(file, &information) || !information.dli_fname || !information.dli_fbase)
		return fail("is not in a file of the app");
	offset = (uint64_t)((const char *)file - (const char *)information.dli_fbase);
	if (host_image_read_only_range(layout, page, offset, &start, &end, &position) != 0)
		return -1;
	descriptor = open(information.dli_fname, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return fail("file %s cannot be opened", information.dli_fname);
	/* (the file's bytes at the offset are the image's: the executable is not
	a universal one, whose architectures are at offsets of their own) */
	if (pread(descriptor, check, sizeof(check), (off_t)offset) != (ssize_t)sizeof(check) ||
		memcmp(check, file, size < sizeof(check) ? size : sizeof(check)))
	{
		close(descriptor);
		return fail("is not at %llx in %s", (unsigned long long)offset, information.dli_fname);
	}
	mapped = mmap((void *)host_address((uint32_t)start), end - start, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_FIXED,
		descriptor, (off_t)position);
	close(descriptor);
	if (mapped != (void *)host_address((uint32_t)start))
		return fail("code cannot be mapped from %s (%s)", information.dli_fname, strerror(errno));
	host_logf(HOST_LOG_INFO, "guest code %08llx-%08llx mapped from the executable at %llx",
		(unsigned long long)start, (unsigned long long)end, (unsigned long long)position);
	return 0;
}
#endif

/* ---------- loading */

int host_load_image(const void *file, size_t size)
{
	struct host_image_layout layout;
	const struct halo_guest_header *header;
	uint64_t low, high;
	/* (the host's pages, 16 KB on macOS, which mapping and protection
	work in; the image's are 4 KB) */
	uint64_t page = host_page_size();
	uint64_t *table;
	const char *name;
	uint32_t count, index;
	int missing = 0;

	if (host_image_validate(file, size, HOST_IMAGE_CLASS, HOST_IMAGE_MACHINE, &layout) != 0)
		return -1;
	low = layout.low & ~(page - 1);
	high = (layout.high + page - 1) & ~(page - 1);
	if (low != layout.low || high > 0x100000000ULL)
		return fail("spans %llx-%llx, not whole pages of this host's", (unsigned long long)layout.low,
			(unsigned long long)layout.high);
	if (host_memory_initialize((uint32_t)low, (uint32_t)(high - low)) != 0)
		return -1;
	if (mmap((void *)host_address((uint32_t)low), high - low, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != (void *)host_address((uint32_t)low))
	{
		return fail("range %llx-%llx cannot be mapped", (unsigned long long)low, (unsigned long long)high);
	}
	for (index = 0; index < layout.segment_count; index++)
	{
		const struct host_image_segment *segment = &layout.segments[index];

		memcpy((void *)host_address((uint32_t)segment->address), (const char *)file + segment->offset,
			segment->file_size);
	}

	if (host_image_validate_imports((const uint8_t *)host_address((uint32_t)low), (uint32_t)low, (uint32_t)high, &layout,
		&count) != 0)
	{
		return -1;
	}
	header = host_pointer((uint32_t)low);
	host_image.header = header;
	host_image.base = (uint32_t)low;
	host_image.end = (uint32_t)high;

	table = host_pointer(header->import_table);
	name = host_pointer(header->import_names);
	for (index = 0; index < count; index++)
	{
		void *function = host_resolve_import(name);

		if (!function)
		{
			host_logf(HOST_LOG_WARN, "guest import %s is not available", name);
			function = (void *)missing_import;
			missing++;
		}
		table[index] = (uint64_t)(uintptr_t)function;
		name += strlen(name) + 1;
	}
	host_logf(HOST_LOG_INFO, "guest image %08llx-%08llx, %u imports (%d unavailable)",
		(unsigned long long)low, (unsigned long long)high, count, missing);

#ifdef HOST_IMAGE_FROM_FILE
	/* (the copy's read-only part replaced by the executable's pages: no
	other way to make code executable on iOS) */
	return code_map_file(file, size, &layout, page);
#else
	/* code becomes read-only and executable, in whole host pages: never one
a writable segment shares (guest.ld puts the data on a 64 KB boundary, the
largest host page) */
	for (index = 0; index < layout.segment_count; index++)
	{
		const struct host_image_segment *segment = &layout.segments[index];
		uint64_t start = segment->address & ~(page - 1);
		uint64_t end = (segment->address + segment->memory_size + page - 1) & ~(page - 1);
		uint32_t other;

		if (!(segment->flags & PF_X))
			continue;
		for (other = 0; other < layout.segment_count; other++)
		{
			const struct host_image_segment *data = &layout.segments[other];

			if ((data->flags & PF_W) && data->address < end && data->address + data->memory_size > start)
				return fail("code segment %u shares a page of %llu bytes with writable segment %u", index,
					(unsigned long long)page, other);
		}
#ifdef __APPLE__
		if (code_map_jit((void *)host_address((uint32_t)start), end - start))
			continue;
		host_logf(HOST_LOG_WARN, "cannot map the code for JIT; making it executable with mprotect");
#endif
		if (mprotect((void *)host_address((uint32_t)start), end - start, PROT_READ | PROT_EXEC) != 0)
			return fail("code cannot be made executable");
	}
	__builtin___clear_cache((char *)host_address((uint32_t)low), (char *)host_address((uint32_t)low) + (high - low));
	return 0;
#endif
}
