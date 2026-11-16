/*
HOST_LOADER_TEST.C

Tests of the guest image's checks (port/android/host/host_loader.c:
host_image_validate and host_image_validate_imports) on ELF files and
images made here, and of where iOS maps the image's read-only part from the
executable (host_image_read_only_range), of both classes whatever the build machine is. Built and
run by tools/test_host_units.py; exits nonzero on a failure.
*/

#include "host.h"

#ifdef __APPLE__
/* (macOS has no elf.h: the little of it the files made here need) */
typedef uint16_t Elf32_Half, Elf64_Half;
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off, Elf64_Word;
typedef uint64_t Elf64_Addr, Elf64_Off, Elf64_Xword;

#define EI_NIDENT 16
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define EV_CURRENT 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_386 3
#define EM_X86_64 62
#define EM_AARCH64 183
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_GNU_STACK 0x6474e551
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct
{
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half e_type, e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff, e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct
{
	Elf32_Word p_type;
	Elf32_Off p_offset;
	Elf32_Addr p_vaddr, p_paddr;
	Elf32_Word p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

typedef struct
{
	unsigned char e_ident[EI_NIDENT];
	Elf64_Half e_type, e_machine;
	Elf64_Word e_version;
	Elf64_Addr e_entry;
	Elf64_Off e_phoff, e_shoff;
	Elf64_Word e_flags;
	Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct
{
	Elf64_Word p_type, p_flags;
	Elf64_Off p_offset;
	Elf64_Addr p_vaddr, p_paddr;
	Elf64_Xword p_filesz, p_memsz, p_align;
} Elf64_Phdr;
#else
#include <elf.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- what host_loader.c calls besides */

static char last_error[512];

void host_logf(int priority, const char *format, ...)
{
	va_list arguments;

	(void)priority;
	va_start(arguments, format);
	vsnprintf(last_error, sizeof(last_error), format, arguments);
	va_end(arguments);
}

void host_fatal(const char *format, ...)
{
	(void)format;
	abort();
}

/* (the loader's pages: Linux's 4 KB, as the images' segments are aligned) */
size_t host_page_size(void)
{
	return 4096;
}

int host_memory_initialize(uint32_t base, uint32_t size)
{
	(void)base;
	(void)size;
	return -1;
}

void *host_resolve_import(const char *name)
{
	(void)name;
	return NULL;
}

/* ---------- checking */

static int failures;

static void expect(int condition, const char *what, int line)
{
	if (!condition)
	{
		printf("line %d: %s (last log: %s)\n", line, what, last_error);
		failures++;
	}
}

#define EXPECT(condition) expect((condition), #condition, __LINE__)

/* ---------- ELF files */

#define FILE_SIZE 0x3000

struct segment_spec
{
	uint32_t type, flags;
	uint64_t offset, address, file_size, memory_size;
};

/* an executable of the class with the segments, headers right after the
file header */
static size_t make_elf(unsigned char *file, int elf_class, int machine, const struct segment_spec *segments, int count)
{
	int index;

	memset(file, 0, FILE_SIZE);
	if (elf_class == ELFCLASS64)
	{
		Elf64_Ehdr header;

		memset(&header, 0, sizeof(header));
		memcpy(header.e_ident, ELFMAG, SELFMAG);
		header.e_ident[EI_CLASS] = ELFCLASS64;
		header.e_ident[EI_DATA] = ELFDATA2LSB;
		header.e_ident[EI_VERSION] = EV_CURRENT;
		header.e_type = ET_EXEC;
		header.e_machine = (Elf64_Half)machine;
		header.e_version = EV_CURRENT;
		header.e_phoff = sizeof(header);
		header.e_ehsize = sizeof(header);
		header.e_phentsize = sizeof(Elf64_Phdr);
		header.e_phnum = (Elf64_Half)count;
		memcpy(file, &header, sizeof(header));
		for (index = 0; index < count; index++)
		{
			Elf64_Phdr program;

			memset(&program, 0, sizeof(program));
			program.p_type = segments[index].type;
			program.p_flags = segments[index].flags;
			program.p_offset = segments[index].offset;
			program.p_vaddr = segments[index].address;
			program.p_filesz = segments[index].file_size;
			program.p_memsz = segments[index].memory_size;
			memcpy(file + sizeof(header) + index * sizeof(program), &program, sizeof(program));
		}
	}
	else
	{
		Elf32_Ehdr header;

		memset(&header, 0, sizeof(header));
		memcpy(header.e_ident, ELFMAG, SELFMAG);
		header.e_ident[EI_CLASS] = ELFCLASS32;
		header.e_ident[EI_DATA] = ELFDATA2LSB;
		header.e_ident[EI_VERSION] = EV_CURRENT;
		header.e_type = ET_EXEC;
		header.e_machine = (Elf32_Half)machine;
		header.e_version = EV_CURRENT;
		header.e_phoff = sizeof(header);
		header.e_ehsize = sizeof(header);
		header.e_phentsize = sizeof(Elf32_Phdr);
		header.e_phnum = (Elf32_Half)count;
		memcpy(file, &header, sizeof(header));
		for (index = 0; index < count; index++)
		{
			Elf32_Phdr program;

			memset(&program, 0, sizeof(program));
			program.p_type = segments[index].type;
			program.p_flags = segments[index].flags;
			program.p_offset = (Elf32_Off)segments[index].offset;
			program.p_vaddr = (Elf32_Addr)segments[index].address;
			program.p_filesz = (Elf32_Word)segments[index].file_size;
			program.p_memsz = (Elf32_Word)segments[index].memory_size;
			memcpy(file + sizeof(header) + index * sizeof(program), &program, sizeof(program));
		}
	}
	return FILE_SIZE;
}

#define BASE HALO_GUEST_IMAGE_BASE

/* code at the base, data a page above it */
static const struct segment_spec good_segments[] =
{
	{ PT_LOAD, PF_R | PF_X, 0x1000, BASE, 0x1000, 0x1000 },
	{ PT_LOAD, PF_R | PF_W, 0x2000, BASE + 0x1000, 0x800, 0x4000 },
	{ PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0 },
};

static int validate(int elf_class, int machine, const struct segment_spec *segments, int count,
	struct host_image_layout *layout)
{
	static unsigned char file[FILE_SIZE];
	size_t size = make_elf(file, elf_class, machine, segments, count);

	last_error[0] = 0;
	return host_image_validate(file, size, elf_class, machine, layout);
}

static void test_classes(void)
{
	static unsigned char file[FILE_SIZE];
	struct host_image_layout layout;
	size_t size;

	EXPECT(validate(ELFCLASS64, EM_AARCH64, good_segments, 3, &layout) == 0);
	EXPECT(layout.low == BASE && layout.high == BASE + 0x5000 && layout.segment_count == 2);
	EXPECT(validate(ELFCLASS32, EM_X86_64, good_segments, 3, &layout) == 0);
	EXPECT(layout.low == BASE && layout.high == BASE + 0x5000 && layout.segment_count == 2);
	EXPECT(layout.segments[1].memory_size == 0x4000 && layout.segments[1].offset == 0x2000);

	/* the wrong class or machine for the host */
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 3);
	EXPECT(host_image_validate(file, size, ELFCLASS32, EM_X86_64, &layout) != 0);
	size = make_elf(file, ELFCLASS32, EM_X86_64, good_segments, 3);
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
	size = make_elf(file, ELFCLASS32, EM_386, good_segments, 3);
	EXPECT(host_image_validate(file, size, ELFCLASS32, EM_X86_64, &layout) != 0);
	/* not ELF, too short, not an executable, big-endian */
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 3);
	EXPECT(host_image_validate(file, 10, ELFCLASS64, EM_AARCH64, &layout) != 0);
	EXPECT(host_image_validate(file, sizeof(Elf64_Ehdr) - 1, ELFCLASS64, EM_AARCH64, &layout) != 0);
	EXPECT(host_image_validate(NULL, 0, ELFCLASS64, EM_AARCH64, &layout) != 0);
	file[0] = 0;
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 3);
	((Elf64_Ehdr *)file)->e_type = ET_DYN;
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 3);
	file[EI_DATA] = ELFDATA2MSB;
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
}

static void test_headers_in_file(void)
{
	static unsigned char file[FILE_SIZE];
	struct host_image_layout layout;
	size_t size;

	/* program headers past the end of the file */
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 3);
	((Elf64_Ehdr *)file)->e_phoff = FILE_SIZE - sizeof(Elf64_Phdr);
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
	((Elf64_Ehdr *)file)->e_phoff = ~0ULL - 8;
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
	/* program headers of another size */
	size = make_elf(file, ELFCLASS32, EM_X86_64, good_segments, 3);
	((Elf32_Ehdr *)file)->e_phentsize = sizeof(Elf64_Phdr);
	EXPECT(host_image_validate(file, size, ELFCLASS32, EM_X86_64, &layout) != 0);
	/* none, or a great many */
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 0);
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
	size = make_elf(file, ELFCLASS64, EM_AARCH64, good_segments, 3);
	((Elf64_Ehdr *)file)->e_phnum = 0xffff;
	EXPECT(host_image_validate(file, size, ELFCLASS64, EM_AARCH64, &layout) != 0);
}

static void test_segments(void)
{
	struct segment_spec segments[3];
	struct host_image_layout layout;
	int elf_class;

	for (elf_class = ELFCLASS32; elf_class <= ELFCLASS64; elf_class++)
	{
		int machine = elf_class == ELFCLASS64 ? EM_AARCH64 : EM_X86_64;

		/* not static */
		memcpy(segments, good_segments, sizeof(segments));
		segments[2].type = PT_DYNAMIC;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		EXPECT(strstr(last_error, "PT_DYNAMIC") != NULL);
		segments[2].type = PT_INTERP;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		/* no loadable segment */
		EXPECT(validate(elf_class, machine, &good_segments[2], 1, &layout) != 0);
		/* the file's part past the end of the file, or overflowing */
		memcpy(segments, good_segments, sizeof(segments));
		segments[1].file_size = FILE_SIZE;
		segments[1].memory_size = FILE_SIZE;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		memcpy(segments, good_segments, sizeof(segments));
		segments[1].offset = FILE_SIZE + 1;
		segments[1].file_size = 0;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		memcpy(segments, good_segments, sizeof(segments));
		segments[1].offset = elf_class == ELFCLASS64 ? ~0ULL - 0x10 : 0xfffffff0u;
		segments[1].file_size = 0x20;
		segments[1].memory_size = 0x20;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		/* larger in the file than in memory */
		memcpy(segments, good_segments, sizeof(segments));
		segments[1].memory_size = 0x400;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		/* below the image's range, past its reserve, wrapping around */
		memcpy(segments, good_segments, sizeof(segments));
		segments[0].address = BASE - 0x1000;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		memcpy(segments, good_segments, sizeof(segments));
		segments[1].memory_size = HALO_GUEST_IMAGE_RESERVE;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		memcpy(segments, good_segments, sizeof(segments));
		segments[1].address = BASE + HALO_GUEST_IMAGE_RESERVE - 0x1000;
		segments[1].memory_size = 0x1000;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) == 0);
		segments[1].memory_size = 0x1001;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		if (elf_class == ELFCLASS64)
		{
			memcpy(segments, good_segments, sizeof(segments));
			segments[1].address = ~0ULL - 0x100;
			segments[1].memory_size = 0x1000;
			EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
		}
		/* not starting at the base */
		memcpy(segments, good_segments, sizeof(segments));
		segments[0].address = BASE + 0x1000;
		segments[1].address = BASE + 0x2000;
		EXPECT(validate(elf_class, machine, segments, 3, &layout) != 0);
	}
}

/* ---------- the header and the imports */

#define IMAGE_SIZE 0x5000

static uint8_t image[IMAGE_SIZE];

/* an image whose code (executable) is its first page, holding its header;
the import count, names and table in the data above */
static void make_image(struct host_image_layout *layout, uint32_t count)
{
	struct halo_guest_header header;
	uint32_t names = BASE + 0x1100;
	uint32_t index;

	EXPECT(validate(ELFCLASS64, EM_AARCH64, good_segments, 3, layout) == 0);
	memset(image, 0, sizeof(image));
	memset(&header, 0, sizeof(header));
	header.magic = HALO_GUEST_MAGIC;
	header.abi_version = HALO_GUEST_ABI_VERSION;
	header.image_end = BASE + IMAGE_SIZE;
	header.import_table = BASE + 0x2000;
	header.import_names = names;
	header.import_count = BASE + 0x1000;
	header.start = BASE + 0x100;
	header.thread_start = BASE + 0x200;
	header.thread_attach = BASE + 0x300;
	header.thread_detach = BASE + 0x400;
	header.init_array_start = BASE + 0x1800;
	header.init_array_end = BASE + 0x1808;
	memcpy(image, &header, sizeof(header));
	memcpy(image + 0x1000, &count, sizeof(count));
	for (index = 0; index < count; index++)
	{
		char name[32];

		snprintf(name, sizeof(name), "host_import_%u", index);
		memcpy(image + (names - BASE), name, strlen(name) + 1);
		names += (uint32_t)strlen(name) + 1;
	}
}

static struct halo_guest_header *image_header(void)
{
	return (struct halo_guest_header *)image;
}

static int validate_imports(const struct host_image_layout *layout, uint32_t *count)
{
	last_error[0] = 0;
	return host_image_validate_imports(image, BASE, BASE + IMAGE_SIZE, layout, count);
}

static void test_imports(void)
{
	struct host_image_layout layout;
	uint32_t count = 0;

	make_image(&layout, 3);
	EXPECT(validate_imports(&layout, &count) == 0);
	EXPECT(count == 3);
	make_image(&layout, 0);
	EXPECT(validate_imports(&layout, &count) == 0 && count == 0);

	/* the header */
	make_image(&layout, 3);
	image_header()->magic++;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->abi_version++;
	EXPECT(validate_imports(&layout, &count) != 0);
	EXPECT(host_image_validate_imports(image, BASE, BASE + 8, &layout, &count) != 0);
	/* an entry point outside the code (data, or outside the image) */
	make_image(&layout, 3);
	image_header()->start = BASE + 0x1800;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->thread_detach = 0x1000;
	EXPECT(validate_imports(&layout, &count) != 0);
	/* the initializers */
	make_image(&layout, 3);
	image_header()->init_array_end = BASE + IMAGE_SIZE + 4;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->init_array_end = BASE;
	EXPECT(validate_imports(&layout, &count) != 0);

	/* the count */
	make_image(&layout, 3);
	image_header()->import_count = BASE + IMAGE_SIZE - 2;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->import_count = 0x10;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->import_count = BASE + 0x1001;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	count = 0x7fffffff;
	memcpy(image + 0x1000, &count, sizeof(count));
	EXPECT(validate_imports(&layout, &count) != 0);

	/* the table: outside the image, too long for it, misaligned */
	make_image(&layout, 3);
	image_header()->import_table = 0x20000000;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->import_table = BASE + IMAGE_SIZE - 16;
	EXPECT(validate_imports(&layout, &count) != 0);
	image_header()->import_table = BASE + IMAGE_SIZE - 24;
	EXPECT(validate_imports(&layout, &count) == 0);
	image_header()->import_table = BASE + 0x2004;
	EXPECT(validate_imports(&layout, &count) != 0);

	/* the names: outside the image, unterminated, empty, fewer than the count */
	make_image(&layout, 3);
	image_header()->import_names = BASE + IMAGE_SIZE;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->import_names = BASE + IMAGE_SIZE - 4;
	memset(image + IMAGE_SIZE - 4, 'x', 4);
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	image_header()->import_names = BASE + 0x1500;
	EXPECT(validate_imports(&layout, &count) != 0);
	make_image(&layout, 3);
	count = 5000;
	memcpy(image + 0x1000, &count, sizeof(count));
	EXPECT(validate_imports(&layout, &count) != 0);
}

/* ---------- the read-only part mapped from a file (iOS) */

static void layout_segment(struct host_image_layout *layout, uint32_t flags, uint64_t offset, uint64_t address,
	uint64_t size, uint64_t memory_size)
{
	struct host_image_segment *segment = &layout->segments[layout->segment_count++];

	segment->flags = flags;
	segment->offset = offset;
	segment->address = address;
	segment->file_size = size;
	segment->memory_size = memory_size;
}

/* the guest image's layout as ld.lld links it (64 KB alignment): the
header, the code and the constants in one run of the file, then the data
on a boundary of its own */
static void guest_layout(struct host_image_layout *layout)
{
	memset(layout, 0, sizeof(*layout));
	layout->low = 0x88000000;
	layout_segment(layout, PF_R, 0x10000, 0x88000000, 0x30, 0x30);
	layout_segment(layout, PF_R | PF_X, 0x10030, 0x88000030, 0x5283b4, 0x5283b4);
	layout_segment(layout, PF_R, 0x5383f0, 0x885283f0, 0xbb4092, 0xbb4092);
	layout_segment(layout, PF_R | PF_W, 0x10f0000, 0x890e0000, 0x32da58, 0x1a55bc0);
	layout->high = 0x8ab36000;
}

static void test_read_only_range(void)
{
	struct host_image_layout layout;
	uint64_t start = 0, end = 0, position = 0;

	/* the image at 0x8000 in the executable, 16 KB pages: the run from the
	header to the constants' last page, from the image's 0x10000 */
	guest_layout(&layout);
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8000, &start, &end, &position) == 0);
	EXPECT(start == 0x88000000 && end == 0x890e0000 && position == 0x18000);
	/* (the image not on a page boundary of the file) */
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8100, &start, &end, &position) != 0);
	/* (64 KB pages: the data's boundary is a page's) */
	EXPECT(host_image_read_only_range(&layout, 0x10000, 0x20000, &start, &end, &position) == 0);
	EXPECT(end == 0x890e0000 && position == 0x30000);
	/* the data in the constants' last page */
	guest_layout(&layout);
	layout.segments[3].address = 0x890dd000;
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8000, &start, &end, &position) != 0);
	/* the constants elsewhere in the file than the code's distance puts them */
	guest_layout(&layout);
	layout.segments[2].offset += 0x1000;
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8000, &start, &end, &position) != 0);
	/* a read-only segment with zero-filled memory (not in the file) */
	guest_layout(&layout);
	layout.segments[2].memory_size += 0x100;
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8000, &start, &end, &position) != 0);
	/* no writable segment at all: the whole image */
	guest_layout(&layout);
	layout.segment_count = 3;
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8000, &start, &end, &position) == 0);
	EXPECT(end == 0x890e0000);
	/* only writable segments */
	memset(&layout, 0, sizeof(layout));
	layout.low = 0x88000000;
	layout_segment(&layout, PF_R | PF_W, 0x10000, 0x88000000, 0x1000, 0x1000);
	EXPECT(host_image_read_only_range(&layout, 0x4000, 0x8000, &start, &end, &position) != 0);
}

int main(void)
{
	test_classes();
	test_headers_in_file();
	test_segments();
	test_imports();
	test_read_only_range();
	if (failures)
		printf("%d failures\n", failures);
	else
		printf("host_loader: all passed\n");
	return failures != 0;
}
