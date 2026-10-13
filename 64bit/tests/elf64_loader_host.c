#include <stdio.h>
#include <string.h>

#include "../src64/kernel/elf64_loader.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "ELF check failed at line %d\n", __LINE__); \
		return __LINE__; \
	} \
} while (0)

static uint8_t fixture[0x12000];
static size_t fixture_size;
static size_t fixture_read_size;
static int fixture_open_failure;
static uint64_t irq_depth;
static uint32_t irq_errors;
static uint32_t irq_saves;
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t allocation_pool[0x20000];

uint64_t platform_irq_save64(void)
{
	irq_saves++;
	return irq_depth++;
}

void platform_irq_restore64(uint64_t state)
{
	if (irq_depth != state + 1) {
		irq_errors++;
	}
	irq_depth = state;
}

#ifdef __aarch64__
void arch64_sync_user_code(uintptr_t physical, size_t size)
{
	(void) physical;
	(void) size;
}
#endif

int fd64_open(struct FDHANDLE64 *fh, const char *name)
{
	(void) name;
	if (fixture_open_failure != 0) {
		return 0;
	}
	fh->info.size = (uint32_t) fixture_size;
	return 1;
}

size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t size)
{
	(void) fh;
	if (size > fixture_read_size) {
		size = fixture_read_size;
	}
	memcpy(dst, fixture, size);
	return size;
}

static struct ELF64_EHDR *header64(void)
{
	return (struct ELF64_EHDR *) fixture;
}

static struct ELF64_PHDR *segments64(void)
{
	return (struct ELF64_PHDR *) (fixture + sizeof(struct ELF64_EHDR));
}

static void reset_fixture64(void)
{
	struct ELF64_EHDR *ehdr;
	struct ELF64_PHDR *phdr;

	memset(fixture, 0, sizeof fixture);
	fixture_size = sizeof fixture;
	fixture_read_size = fixture_size;
	fixture_open_failure = 0;
	ehdr = header64();
	ehdr->e_ident[0] = 0x7f;
	ehdr->e_ident[1] = 'E';
	ehdr->e_ident[2] = 'L';
	ehdr->e_ident[3] = 'F';
	ehdr->e_ident[4] = 2;
	ehdr->e_ident[5] = 1;
	ehdr->e_ident[6] = 1;
	ehdr->e_type = ET_EXEC;
#ifdef __aarch64__
	ehdr->e_machine = EM_AARCH64;
#else
	ehdr->e_machine = EM_X86_64;
#endif
	ehdr->e_version = 1;
	ehdr->e_entry = USER_IMAGE_MIN;
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_phoff = sizeof(*ehdr);
	ehdr->e_phentsize = sizeof(struct ELF64_PHDR);
	ehdr->e_phnum = 2;
	phdr = segments64();
	phdr[0].p_type = PT_LOAD;
	phdr[0].p_flags = PF_R64 | PF_X64;
	phdr[0].p_offset = 0x1000;
	phdr[0].p_vaddr = USER_IMAGE_MIN;
	phdr[0].p_filesz = 16;
	phdr[0].p_memsz = 0x1000;
	phdr[0].p_align = MEMMAN64_PAGE_SIZE;
	phdr[1] = phdr[0];
	phdr[1].p_flags = PF_R64 | PF_W64;
	phdr[1].p_offset = 0x2000;
	phdr[1].p_vaddr += 0x1000;
}

static int reject_fixture64(int expected)
{
	struct PROCESS64 process = {0};
	size_t available = memman64_total(&memman64);

	CHECK64(elf64_load_process("fixture", &process) == expected);
	CHECK64(memman64_total(&memman64) == available);
	CHECK64(image_owner == NULL);
	CHECK64(process.entry == 0 && process.image.size == 0);
	CHECK64(irq_depth == 0 && irq_errors == 0);
	return 0;
}

static int test_ownership_cleanup64(void)
{
	struct PROCESS64 owner = {0};
	struct PROCESS64 other = {0};
	size_t available = memman64_total(&memman64);
	uint32_t saves;

	reset_fixture64();
	fixture_open_failure = 1;
	CHECK64(reject_fixture64(-1) == 0);
	reset_fixture64();
	fixture_size = sizeof allocation_pool + MEMMAN64_PAGE_SIZE;
	CHECK64(reject_fixture64(-2) == 0);
	reset_fixture64();
	fixture_size = 0;
	CHECK64(reject_fixture64(-2) == 0);
	reset_fixture64();
	image_owner = &owner;
	CHECK64(elf64_load_process("fixture", &other) == -8);
	CHECK64(image_owner == &owner);
	CHECK64(memman64_total(&memman64) == available);
	saves = irq_saves;
	elf64_release_process(&other);
	CHECK64(image_owner == &owner && irq_saves == saves + 1);
	elf64_release_process(&owner);
	CHECK64(image_owner == NULL);
	elf64_release_process(&owner);
	CHECK64(image_owner == NULL);
	CHECK64(irq_depth == 0 && irq_errors == 0);
	return 0;
}

static int test_headers64(void)
{
	reset_fixture64();
	CHECK64(valid_header(header64(), fixture_size));
	fixture_size = sizeof(struct ELF64_EHDR) - 1;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_ehsize--;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_phentsize--;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_phoff = UINT64_MAX - sizeof(struct ELF64_PHDR);
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_phoff = sizeof(struct ELF64_EHDR) - 1;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_phoff = fixture_size;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_phnum = UINT16_MAX;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	fixture_size = sizeof(struct ELF64_EHDR) + 2 * sizeof(struct ELF64_PHDR);
	CHECK64(valid_header(header64(), fixture_size));
	fixture_size--;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	fixture_read_size--;
	CHECK64(reject_fixture64(-3) == 0);
	return 0;
}

static int test_segment_ranges64(void)
{
	uintptr_t low;
	uintptr_t high;

	reset_fixture64();
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	CHECK64(low == USER_IMAGE_MIN && high == USER_IMAGE_MIN + 0x2000);
	segments64()[1].p_filesz = 0;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	/* Empty segments must not extend the image or overlap a live segment. */
	segments64()[1] = segments64()[0];
	segments64()[1].p_filesz = 0;
	segments64()[1].p_memsz = 0;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	CHECK64(high == USER_IMAGE_MIN + 0x1000);
	reset_fixture64();
	segments64()[0].p_offset = UINT64_MAX - 0xfff;
	segments64()[0].p_filesz = 0x2000;
	segments64()[0].p_memsz = 0x2000;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_offset = fixture_size;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_filesz = segments64()[0].p_memsz + 1;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_memsz = UINT64_MAX;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_vaddr = UINT64_MAX - 0xfff;
	segments64()[0].p_memsz = 0x2000;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_vaddr = USER_IMAGE_MIN - 0x1000;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_memsz = USER_IMAGE_MAX - USER_IMAGE_MIN + 1;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	header64()->e_phnum = 1;
	segments64()[0].p_memsz = USER_IMAGE_MAX - USER_IMAGE_MIN;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	CHECK64(high == USER_IMAGE_MAX);
	return 0;
}

static int test_alignment_and_overlap64(void)
{
	uintptr_t low;
	uintptr_t high;
	uint64_t alignments[] = {0, 1, 0x800, 0x1800, UINT64_MAX};
	size_t i;

	for (i = 0; i < sizeof alignments / sizeof alignments[0]; i++) {
		reset_fixture64();
		segments64()[0].p_align = alignments[i];
		CHECK64(reject_fixture64(-5) == 0);
	}
	reset_fixture64();
	segments64()[0].p_offset++;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_align = 0x10000;
	CHECK64(reject_fixture64(-5) == 0);
	segments64()[0].p_offset = 0x10000;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	reset_fixture64();
	segments64()[1].p_vaddr = segments64()[0].p_vaddr;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_memsz++;
	CHECK64(reject_fixture64(-5) == 0);
	/* Check unsorted headers and BSS overlap, as well as file-backed bytes. */
	reset_fixture64();
	segments64()[1].p_filesz = 0;
	segments64()[1].p_vaddr = segments64()[0].p_vaddr;
	CHECK64(reject_fixture64(-5) == 0);
	reset_fixture64();
	segments64()[0].p_vaddr += 0x1000;
	segments64()[1].p_vaddr -= 0x1000;
	header64()->e_entry = segments64()[0].p_vaddr;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	segments64()[1].p_memsz++;
	CHECK64(reject_fixture64(-5) == 0);
	/* Adjacent byte ranges may share a page; page padding is not a segment. */
	reset_fixture64();
	segments64()[0].p_memsz = 16;
	segments64()[1].p_vaddr = USER_IMAGE_MIN + 16;
	segments64()[1].p_offset += 16;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	return 0;
}

static int test_header_identity64(void)
{
	reset_fixture64();
	header64()->e_ident[0] = 0;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_ident[4] = 1;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_ident[5] = 2;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_ident[6] = 0;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_version = 2;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_type = 3;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_machine = 0;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
#ifdef __aarch64__
	header64()->e_machine = EM_X86_64;
#else
	header64()->e_machine = EM_AARCH64;
#endif
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_flags = 1;
	CHECK64(reject_fixture64(-4) == 0);
	reset_fixture64();
	header64()->e_flags = UINT32_MAX;
	CHECK64(reject_fixture64(-4) == 0);
	return 0;
}

static int test_entry_and_load64(void)
{
	uintptr_t low;
	uintptr_t high;

	reset_fixture64();
	header64()->e_phnum = 0;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	segments64()[0].p_type = 0;
	segments64()[1].p_type = 0;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	segments64()[0].p_memsz = 0;
	segments64()[0].p_filesz = 0;
	segments64()[1].p_memsz = 0;
	segments64()[1].p_filesz = 0;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	segments64()[0].p_flags = PF_R64;
	CHECK64(reject_fixture64(-6) == 0);
	/* A zero-sized executable segment cannot authorize entry in data. */
	segments64()[0].p_memsz = 0;
	segments64()[0].p_filesz = 0;
	segments64()[0].p_flags = PF_R64 | PF_X64;
	segments64()[0].p_vaddr = segments64()[1].p_vaddr;
	header64()->e_entry = segments64()[1].p_vaddr;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	header64()->e_entry = USER_IMAGE_MIN - 1;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	header64()->e_entry = USER_IMAGE_MIN + 0x2000;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	header64()->e_entry = segments64()[1].p_vaddr;
	CHECK64(reject_fixture64(-6) == 0);
	reset_fixture64();
	segments64()[1].p_vaddr += 0x1000;
	header64()->e_entry = USER_IMAGE_MIN + 0x1000;
	CHECK64(reject_fixture64(-6) == 0);
	/* Entry may be in any executable segment, irrespective of header order. */
	reset_fixture64();
	segments64()[0].p_flags = PF_R64;
	segments64()[1].p_flags = PF_R64 | PF_X64;
	header64()->e_entry = segments64()[1].p_vaddr;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	header64()->e_entry += segments64()[1].p_memsz - 4;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	return 0;
}

static int test_segment_flags64(void)
{
	uintptr_t low;
	uintptr_t high;
	uint32_t flags;
	size_t i;
	uint32_t invalid_flags[] = {0x8U, 0x10000000U, 0x80000000U, UINT32_MAX};
	uint32_t unsupported_types[] = {PT_DYNAMIC64, PT_INTERP64, PT_TLS64};

	for (i = 0; i < sizeof invalid_flags / sizeof invalid_flags[0]; i++) {
		reset_fixture64();
		segments64()[0].p_flags |= invalid_flags[i];
		CHECK64(reject_fixture64(-5) == 0);
		reset_fixture64();
		segments64()[1].p_flags |= invalid_flags[i];
		CHECK64(reject_fixture64(-5) == 0);
	}
	/* Validate the mask without claiming that hardware enforces W^X/NX. */
	for (flags = 0; flags <= PF_MASK64; flags++) {
		reset_fixture64();
		segments64()[1].p_flags = flags;
		CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	}
	for (i = 0; i < sizeof unsupported_types / sizeof unsupported_types[0]; i++) {
		reset_fixture64();
		segments64()[1].p_type = unsupported_types[i];
		CHECK64(reject_fixture64(-5) == 0);
	}
	/* Linker metadata such as GNU_STACK is not loaded into the image. */
	reset_fixture64();
	segments64()[1].p_type = 0x6474e551U;
	CHECK64(image_range64(fixture, fixture_size, &low, &high) == 0);
	return 0;
}

int main(void)
{
	memman64_init(&memman64);
	CHECK64(memman64_add_pool(&memman64, (uintptr_t) allocation_pool,
		sizeof allocation_pool) == 0);
	CHECK64(test_ownership_cleanup64() == 0);
	CHECK64(test_headers64() == 0);
	CHECK64(test_segment_ranges64() == 0);
	CHECK64(test_alignment_and_overlap64() == 0);
	CHECK64(test_header_identity64() == 0);
	CHECK64(test_entry_and_load64() == 0);
	CHECK64(test_segment_flags64() == 0);
	return 0;
}
