#include "address_space_fixture64.h"
/* Use real checked arithmetic; allocation faults come from the shared fixture. */
#define memman64 fixture_unused_memman64
#define memman64_alloc_4k fixture_unused_alloc64
#define memman64_free_4k fixture_unused_free64
#include "../src64/kernel/memory64.c"
#undef memman64_free_4k
#undef memman64_alloc_4k
#undef memman64
#define copy_bytes elf_copy_bytes64
#include "../src64/kernel/elf64_loader.c"
#undef copy_bytes
#include "../src64/kernel/process64.c"

static uint8_t elf_bytes64[0x4000];
static int read_error64, open_error64;
static int reserve_during_read64;
static struct PROCESS64 *reserved64;

int fd64_open_checked(struct FDHANDLE64 *file, const char *name, unsigned int flags)
{
	assert(strcmp(name, "fixture") == 0 && flags == 0 && !irq_off64);
	if (open_error64) { return -2; }
	file->info.size = sizeof(elf_bytes64);
	return 0;
}

int64_t fd64_read_checked(struct FDHANDLE64 *file, void *buffer, size_t size)
{
	assert(file->info.size == size && !irq_off64);
	memcpy(buffer, elf_bytes64, size);
	if (reserve_during_read64) {
		/* A preempting task reserves a different slot while the owner loads. */
		reserved64 = process_alloc();
		assert(reserved64 != NULL && reserved64->state == PROCESS_LOADING64);
	}
	return read_error64 ? -5 : (int64_t) size;
}

static void fixture64(void)
{
	struct ELF64_EHDR *header = (void *) elf_bytes64;
	struct ELF64_PHDR *segments = (void *) (elf_bytes64 + 64);

	memset(elf_bytes64, 0, sizeof(elf_bytes64));
	memcpy(header->e_ident, "\177ELF\002\001\001", 7);
	header->e_type = 2;
#ifdef TEST_AS_AARCH64
	header->e_machine = 183;
#else
	header->e_machine = 62;
#endif
	header->e_version = 1;
	header->e_entry = 0x400000;
	header->e_ehsize = 64;
	header->e_phoff = 64;
	header->e_phentsize = 56;
	header->e_phnum = 3;
	segments[0] = (struct ELF64_PHDR) {1, 5, 0x1000, 0x400000, 0, 31, 0x800, 0x1000};
	segments[1] = (struct ELF64_PHDR) {1, 4, 0x2000, 0x402000, 0, 19, 32, 0x1000};
	segments[2] = (struct ELF64_PHDR) {1, 6, 0x3000, 0x403000, 0, 17, 0x1017, 0x1000};
	memset(elf_bytes64 + 0x1000, 0x31, 31);
	memset(elf_bytes64 + 0x2000, 0x72, 19);
	memset(elf_bytes64 + 0x3000, 0xb4, 17);
	read_error64 = open_error64 = reserve_during_read64 = 0;
	event_count64 = clean_count64 = 0;
}

static void check_page64(struct PROCESS64 *process, uintptr_t va, uintptr_t expected_pa,
	unsigned int expected_flags)
{
	uintptr_t pa;
	unsigned int flags;

	assert(walk64(process->space, va, &pa, &flags) == 0);
	assert(pa == expected_pa && flags == expected_flags);
}

static void check_contents64(struct PROCESS64 *process)
{
	const uint8_t *image = (const void *) process->image_storage.kernel_address;
	const uint8_t *heap = (const void *) process->heap_storage.kernel_address;
	const uint8_t *stack = (const void *) process->stack_storage.kernel_address;
	uintptr_t argv_offset = process->initial_argv - 0x100200000ULL;
	const uintptr_t *argv = (const void *) (stack + argv_offset);
	uintptr_t pa;
	unsigned int flags;

	assert(process->state == PROCESS_READY64 && process->entry == 0x400000);
	assert(process->image.base == 0x400000 && process->image.size == 0x5000);
	assert(process->stack.base == 0x100200000ULL && process->stack.size == 0x10000);
	assert(process->heap.base == 0x100000000ULL && process->heap.size == 0x100000);
	assert(process->heap_next == 0x100000000ULL && process->initial_argc == 3);
	assert(process->initial_rsp == process->initial_argv && !(process->initial_rsp & 15));
	assert(argv[3] == 0 && strcmp((const char *) stack + argv[0] - 0x100200000ULL,
		"fixture") == 0);
	assert(strcmp((const char *) stack + argv[1] - 0x100200000ULL, "한글") == 0);
	assert(strcmp((const char *) stack + argv[2] - 0x100200000ULL, "abc") == 0);
	for (size_t i = 0; i < argv_offset; i++) { assert(stack[i] == 0); }
	for (size_t i = 0; i < 0x100000; i++) { assert(heap[i] == 0); }
	for (size_t i = 0; i < 0x5000; i++) {
		unsigned char expected = i < 31 ? 0x31 :
			(i >= 0x2000 && i < 0x2013) ? 0x72 :
			(i >= 0x3000 && i < 0x3011) ? 0xb4 : 0;

		assert(image[i] == expected);
	}
	check_page64(process, 0x400000, process->image_storage.physical, 5);
	check_page64(process, 0x402000, process->image_storage.physical + 0x2000, 1);
	check_page64(process, 0x403000, process->image_storage.physical + 0x3000, 3);
	check_page64(process, 0x404fff, process->image_storage.physical + 0x4fff, 3);
	check_page64(process, 0x100000000ULL, process->heap_storage.physical, 3);
	check_page64(process, 0x100200000ULL, process->stack_storage.physical, 3);
	assert(walk64(process->space, 0x401000, &pa, &flags) != 0);
	assert(walk64(process->space, 0x405000, &pa, &flags) != 0);
	assert(walk64(process->space, 0x1001ff000ULL, &pa, &flags) != 0);
	assert(walk64(process->space, 0x100210000ULL, &pa, &flags) != 0);
	assert(process->elf_snapshot.size == 0 && image_owner == NULL);
}

static int prepare64(struct PROCESS64 **process)
{
	event_count64 = clean_count64 = 0;
	return process64_prepare_file("fixture", "fixture 한글 abc", NULL, process);
}

static void isolation64(void)
{
	struct PROCESS64 *processes[4] = {0}, *extra = NULL;
	size_t base = live_pages64;

	fixture64(); next_pid = UINT32_MAX;
	for (unsigned int i = 0; i < 4; i++) {
		if (i == 2) { next_pid = UINT32_MAX; }
		assert(prepare64(&processes[i]) == 0);
		assert(processes[i]->pid == (i == 0 ? UINT32_MAX : i));
		check_contents64(processes[i]);
		for (unsigned int j = 0; j < i; j++) {
			assert(processes[i]->pid != processes[j]->pid);
			assert(processes[i]->image_storage.physical != processes[j]->image_storage.physical);
		}
		*(uint8_t *) processes[i]->heap_storage.kernel_address = (uint8_t) (i + 1);
	}
	unsigned int calls = alloc_calls64;

	assert(prepare64(&extra) == -12 && extra == NULL && alloc_calls64 == calls);
	for (unsigned int i = 0; i < 4; i++) {
		assert(*(uint8_t *) processes[i]->heap_storage.kernel_address == i + 1);
		assert(process64_discard_prepared(&processes[i]) == 0 && !processes[i]);
	}
	assert(live_pages64 == base);
	assert(prepare64(&extra) == 0);
	check_contents64(extra);
	assert(process64_discard_prepared(&extra) == 0 && live_pages64 == base);
	reserve_during_read64 = 1;
	assert(prepare64(&extra) == 0 && extra != reserved64 && extra->pid != reserved64->pid);
	assert(process64_discard_prepared(&reserved64) == 0);
	assert(process64_discard_prepared(&extra) == 0 && live_pages64 == base);
}

static void failures_process64(void)
{
	struct PROCESS64 *owner = NULL, *other = NULL;
	static uint64_t before[8][512];
	unsigned int count = as_arch_table_count64();
	size_t base;

	fixture64(); assert(prepare64(&owner) == 0);
	snapshot64(owner->space, before);
	base = live_pages64;
	irq_off64 = 1; assert(address_space_activate64(owner->space) == 0); irq_off64 = 0;
	uintptr_t owner_root = installed64;

	/* Tables, file snapshot, image, stack and heap: every allocation failure. */
	for (unsigned int i = 0; i < count + 4; i++) {
		alloc_fail64 = (int) (alloc_calls64 + i);
		assert(prepare64(&other) == -12 && other == NULL);
		alloc_fail64 = -1;
		assert(live_pages64 == base && !irq_off64 && installed64 == owner_root);
		unchanged64(owner->space, before); check_contents64(owner);
	}
	/* Every cleanup free retains the slot and handle until a later retry. */
	for (unsigned int i = 0; i < count + 3; i++) {
		assert(prepare64(&other) == 0);
		free_fail64 = (int) (free_calls64 + i);
		assert(process64_discard_prepared(&other) == -12 && other != NULL);
		assert(other->state == PROCESS_REAP64 && other->pid != 0);
		free_fail64 = -1;
		assert(process64_discard_prepared(&other) == 0 && other == NULL);
		assert(live_pages64 == base);
		unchanged64(owner->space, before); check_contents64(owner);
	}
	/* Snapshot free failure after mapping cannot publish a READY process. */
	free_fail64 = (int) free_calls64;
	assert(prepare64(&other) == -12 && other == NULL);
	free_fail64 = -1;
	assert(live_pages64 == base);
	/* Persistent unwind failures preserve handles at every allocation stage. */
	for (unsigned int i = 1; i < count + 4; i++) {
		alloc_fail64 = (int) alloc_calls64 + (int) i;
		/* Stack/heap failure occurs after the successful snapshot free. */
		free_fail64 = (int) free_calls64 + (i >= count + 2 ? 1 : 0);
		free_persistent64 = 1;
		assert(prepare64(&other) == -12 && other != NULL);
		assert(other->state == PROCESS_REAP64 && other->pid != 0);
		struct PROCESS64 *reservation = process_alloc();

		assert(reservation != NULL && reservation != other && reservation != owner);
		assert(process64_discard_prepared(&reservation) == 0);
		free_persistent64 = 0; alloc_fail64 = free_fail64 = -1;
		assert(process64_discard_prepared(&other) == 0 && live_pages64 == base);
		unchanged64(owner->space, before); check_contents64(owner);
	}
	/* Persistent snapshot-release failure keeps its pages until all tables retire. */
	free_persistent64 = 1;
	assert(prepare64(&other) == -12 && other != NULL && other->elf_snapshot.size != 0);
	free_persistent64 = 0;
	assert(process64_discard_prepared(&other) == 0 && live_pages64 == base);
	read_error64 = 1;
	assert(prepare64(&other) == -3 && other == NULL && live_pages64 == base);
	read_error64 = 0; open_error64 = 1;
	assert(prepare64(&other) == -2 && other == NULL && live_pages64 == base);
	open_error64 = 0;
	unchanged64(owner->space, before); check_contents64(owner);
	assert(installed64 == owner_root);
	irq_off64 = 1; assert(address_space_activate64(NULL) == 0); irq_off64 = 0;
	assert(process64_discard_prepared(&owner) == 0);
}

static void permissions_process64(void)
{
	struct PROCESS64 *process = NULL;
	struct ELF64_PHDR *segments = (void *) (elf_bytes64 + 64);
	size_t base = live_pages64;
	uintptr_t pa;
	unsigned int flags;

	fixture64(); segments[2].p_flags = 7;
	assert(prepare64(&process) == -5 && !process && live_pages64 == base);
	fixture64();
	segments[1].p_vaddr = 0x400800; segments[1].p_offset = 0x2800;
	segments[1].p_flags = 6;
	assert(prepare64(&process) == -5 && !process && live_pages64 == base);
	segments[1].p_flags = 4;
	assert(prepare64(&process) == 0);
	check_page64(process, 0x400800, process->image_storage.physical + 0x800, 5);
	assert(walk64(process->space, 0x402000, &pa, &flags) != 0);
	assert(process64_discard_prepared(&process) == 0 && live_pages64 == base);
	/* An active process cannot be discarded or lose backing ownership. */
	fixture64(); assert(prepare64(&process) == 0);
	irq_off64 = 1; assert(address_space_activate64(process->space) == 0); irq_off64 = 0;
	unsigned int frees = free_calls64;

	assert(process64_discard_prepared(&process) == -16 && process->state == PROCESS_READY64);
	assert(free_calls64 == frees);
	irq_off64 = 1; assert(address_space_activate64(NULL) == 0); irq_off64 = 0;
	assert(process64_discard_prepared(&process) == 0 && live_pages64 == base);
}

int main(void)
{
	(void) cleaned64;
	reset64(); assert(address_space_init64() == 0);
	isolation64(); failures_process64(); permissions_process64();
	puts("process memory: private ELF, argv, guards, W^X, slots and rollback PASS");
	return 0;
}
