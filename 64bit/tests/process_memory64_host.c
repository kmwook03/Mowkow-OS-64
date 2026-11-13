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
#include "../src64/kernel/user_access64.c"

static uint8_t elf_bytes64[0x4000];
static int read_error64, open_error64;
static int reserve_during_read64;
static struct PROCESS64 *reserved64;
static struct TASK64 execution_tasks64[4];
static struct TASK64 *current_task64;
static uintptr_t entry_top64;
static unsigned int execution_depth64, execution_limit64, entry_calls64, raw_calls64;
static int exit_status64, fail_exit_free64 = -1;
static struct PROCESS64 *returned_process64;
static unsigned char console_tokens64[4];
static char opened_name64[FD64_NAME_MAX];

struct TASK64 *task_now64(void) { return current_task64; }

void platform_process_stack64(uintptr_t top)
{
	assert(irq_off64);
	entry_top64 = top;
}

void console64_set_raw_con(struct CONSOLE64 *console, int on)
{
	assert(!irq_off64 && !on && current_task64->process == NULL);
	assert(active_space64 == &kernel_space64 && entry_top64 == 0);
	assert(returned_process64->state == PROCESS_REAP64);
	assert(returned_process64->files[3].used && returned_process64->entry_storage.size);
	if (console == NULL) { return; }
	assert(console == (struct CONSOLE64 *) &console_tokens64[execution_depth64]);
	raw_calls64++;
}

/* Replace the CPU transition only. Nested execs model suspended owners, while
 * real process/ELF/table code must preserve all four roots and detach on return.
 */
int enter_user_mode64(uintptr_t entry, uintptr_t stack, uint64_t argc, uintptr_t argv,
	uint16_t code_selector, uint16_t data_selector, uintptr_t *saved_kernel_rsp)
{
	struct TASK64 *task = current_task64;
	struct PROCESS64 *process = process64_current();
	unsigned int depth = execution_depth64;
	uintptr_t root = installed64;
	unsigned int frees = free_calls64;

	assert(irq_off64 && process && process->state == PROCESS_RUNNING64);
	assert(active_space64 == process->space && task->is_user);
	assert(entry == 0x400000 && stack == argv && argc == 3);
	assert(code_selector == GDT64_USER_CODE && data_selector == GDT64_USER_DATA);
	assert(entry_top64 == process->entry_storage.kernel_address + 65536);
	assert(process64_entry_stack_top() == entry_top64);
	assert(task->entry_stack_base == process->entry_storage.kernel_address);
	assert(task->entry_stack_size == 65536);
	assert(process64_discard_prepared(&process) == -22 && free_calls64 == frees);
	assert(process64_user_range_valid((void *) 0x400000, 31));
	assert(!process64_user_write_valid((void *) 0x400000, 1));
	assert(!process64_user_range_valid((void *) 0x400fff, 2));
	assert(process64_user_write_valid((void *) 0x403000, 0x1017));
	assert(!process64_user_range_valid((void *) process->entry_storage.kernel_address, 1));
	assert(!process64_user_write_valid((void *) UINTPTR_MAX, 2));
	*saved_kernel_rsp = 0x123000 + depth * 0x1000;
	process->files[3].used = 1;
	unsigned char *snapshot = process64_io_buffer(17);

	assert(snapshot != NULL && process->io_ready);
	snapshot[0] = (unsigned char) (depth + 9);
	*(unsigned char *) process->heap_storage.kernel_address = (unsigned char) (depth + 1);
	entry_calls64++;
	if (depth + 1 < execution_limit64) {
		current_task64 = &execution_tasks64[++execution_depth64];
		process64_switch_task64(current_task64);
		platform_irq_restore64(0);
		event_count64 = clean_count64 = 0;
		assert(process64_exec_file("fixture", "fixture 한글 abc",
			(struct CONSOLE64 *) &console_tokens64[execution_depth64]) == exit_status64);
		assert(!irq_off64 && current_task64->process == NULL);
		execution_depth64--;
		current_task64 = task;
		(void) platform_irq_save64();
		process64_switch_task64(task);
		assert(installed64 == root && active_space64 == process->space);
		assert(process64_entry_stack_top() == entry_top64);
		assert(*(unsigned char *) process->heap_storage.kernel_address == depth + 1);
		assert(snapshot[0] == depth + 9 && process64_io_buffer(17) == snapshot);
	}
	frees = free_calls64;
	process64_exit_current(exit_status64);
	assert(process64_current_exit_rsp() == 0x123000 + depth * 0x1000);
	assert(process64_current_exit_status() == exit_status64);
	assert(process64_current() == process && process->files[3].used);
	assert(free_calls64 == frees && active_space64 == process->space);
	if (fail_exit_free64 >= 0) {
		free_fail64 = (int) free_calls64 + fail_exit_free64;
		free_persistent64 = 1;
	}
	returned_process64 = process;
	return -99; /* Explicit process exit status wins over the entry return value. */
}

int fd64_open_checked(struct FDHANDLE64 *file, const char *name, unsigned int flags)
{
	assert(strlen(name) < sizeof(opened_name64) && flags == 0 && !irq_off64);
	memcpy(opened_name64, name, strlen(name) + 1);
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
	assert(process->elf_snapshot.size == 0);
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

	/* Tables, snapshot, image, stack, heap and entry stack: every allocation failure. */
	for (unsigned int i = 0; i < count + 5; i++) {
		alloc_fail64 = (int) (alloc_calls64 + i);
		assert(prepare64(&other) == -12 && other == NULL);
		alloc_fail64 = -1;
		assert(live_pages64 == base && !irq_off64 && installed64 == owner_root);
		unchanged64(owner->space, before); check_contents64(owner);
	}
	/* Every cleanup free retains the slot and handle until a later retry. */
	for (unsigned int i = 0; i < count + 4; i++) {
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
	for (unsigned int i = 1; i < count + 5; i++) {
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

static void user_copy64(void)
{
	struct PROCESS64 *owner = NULL, *peer = NULL;
	struct TASK64 task = {0};
	unsigned char data[32], output[32];
	const uintptr_t bad[] = {0, 0x100000, 0x401000, 0x1001ff000ULL,
		0x100210000ULL, UINTPTR_MAX - 7};
	size_t base = live_pages64;

	fixture64(); assert(prepare64(&owner) == 0 && prepare64(&peer) == 0);
	task.process = owner; current_task64 = &task;
	memset(data, 0x67, sizeof(data));
	assert(copy_from_user64(output, (void *) 0x400000, 31) == 0);
	for (size_t i = 0; i < 31; i++) { assert(output[i] == 0x31); }
	assert(copy_to_user64((void *) 0x400000, data, 1) == -14);
	assert(copy_to_user64((void *) 0x402000, data, 1) == -14);
	assert(copy_to_user64((void *) 0x403ff8, data, sizeof(data)) == 0);
	assert(memcmp((void *) (owner->image_storage.kernel_address + 0x3ff8), data, 32) == 0);
	assert(*(unsigned char *) (peer->image_storage.kernel_address + 0x3ff8) == 0);
	assert(copy_from_user64(output, (void *) 0x403ff8, sizeof(output)) == 0);
	assert(memcmp(output, data, sizeof(data)) == 0);
	memset(output, 0xa5, sizeof(output));
	assert(copy_from_user64(output, (void *) 0x400ff8, sizeof(output)) == -14);
	for (size_t i = 0; i < sizeof(output); i++) { assert(output[i] == 0xa5); }
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		assert(copy_from_user64(output, (void *) bad[i], 16) == -14);
		assert(copy_to_user64((void *) bad[i], data, 16) == -14);
	}
	assert(copy_from_user64(output, (void *) peer->heap_storage.kernel_address, 1) == -14);
	assert(copy_to_user64((void *) peer->heap_storage.physical, data, 1) == -14);
	assert(copy_from_user64(output, (void *) 0x403000, SIZE_MAX) == -14);
	assert(copy_from_user64(output, (void *) 0x100100000ULL, 0) == 0);
	assert(copy_to_user64((void *) 0x100100000ULL, data, 0) == 0);
	assert(copy_from_user64(output, NULL, 0) == -14);
	assert(copy_from_user64(NULL, (void *) 0x400000, 1) == -22);
	assert(copy_from_user64((void *) UINTPTR_MAX, (void *) 0x400000, 1) == -22);
	assert(copy_to_user64((void *) 0x1000ffff8ULL, data, 16) == -14);
	assert(*(unsigned char *) (owner->heap_storage.kernel_address + 0xffff8) == 0);
	assert(copy_to_user64((void *) 0x403ffc, "abcdEF", 7) == 0);
	assert(copy_string_from_user64((char *) output, sizeof(output), (void *) 0x403ffc) == 6);
	assert(memcmp(output, "abcdEF", 7) == 0);
	assert(copy_string_from_user64((char *) output, 4, (void *) 0x403ffc) == -36);
	assert(output[0] == 0);
	assert(copy_to_user64((void *) 0x404fff, "x", 1) == 0);
	assert(copy_string_from_user64((char *) output, 2, (void *) 0x404fff) == -14);
	assert(output[0] == 0);
	assert(process64_io_buffer(0) != NULL && !owner->io_storage.size);
	alloc_fail64 = (int) alloc_calls64;
	assert(process64_io_buffer(1) == NULL && !owner->io_storage.size);
	alloc_fail64 = -1;
	void *buffer = process64_io_buffer(1);

	assert(buffer != NULL && owner->io_storage.size == 4096);
	assert(!process64_user_range_valid(buffer, 1));
	free_fail64 = (int) free_calls64;
	assert(process64_io_buffer(4097) == NULL && owner->io_storage.kernel_address ==
		(uintptr_t) buffer && owner->io_storage.size == 4096);
	free_fail64 = -1;
	assert(process64_io_buffer(4096) == buffer);
	assert(process64_io_buffer(4097) != NULL && owner->io_storage.size == 8192);
	assert(process64_io_buffer(SIZE_MAX) == NULL);
	current_task64 = NULL;
	assert(copy_from_user64(output, (void *) 0x400000, 1) == -14);
	/* Fail the I/O snapshot free after all private page tables retire. */
	free_fail64 = (int) free_calls64 + (int) as_arch_table_count64();
	assert(process64_discard_prepared(&owner) == -12 && owner && owner->io_storage.size);
	free_fail64 = -1;
	assert(process64_discard_prepared(&owner) == 0);
	assert(process64_discard_prepared(&peer) == 0 && live_pages64 == base);
}

static void execution64(void)
{
	size_t base = live_pages64;
	const int statuses[] = {0, 17, 142};

	fixture64();
	assert(process64_exec_file("fixture", NULL, NULL) == -4);
	for (unsigned int i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
		fixture64();
		memset(execution_tasks64, 0, sizeof(execution_tasks64));
		current_task64 = &execution_tasks64[0];
		execution_limit64 = 4;
		exit_status64 = statuses[i];
		entry_calls64 = raw_calls64 = 0;
		assert(process64_exec_file("fixture", "fixture 한글 abc",
			(struct CONSOLE64 *) &console_tokens64[0]) == exit_status64);
		assert(entry_calls64 == 4 && raw_calls64 == 4 && !irq_off64);
		assert(active_space64 == &kernel_space64 && entry_top64 == 0);
		assert(live_pages64 == base && execution_depth64 == 0);
		for (unsigned int j = 0; j < 4; j++) {
			assert(execution_tasks64[j].process == NULL && !execution_tasks64[j].is_user);
			assert(!execution_tasks64[j].entry_stack_base && !execution_tasks64[j].entry_stack_size);
			assert(process_table[j].state == PROCESS_FREE64);
		}
	}
	/* Every failed teardown retains its detached owner for a later bounded reap. */
	execution_limit64 = 1;
	for (unsigned int i = 0; i < as_arch_table_count64() + 5; i++) {
		fixture64();
		fail_exit_free64 = (int) i;
		assert(process64_exec_file("fixture", "fixture 한글 abc",
			(struct CONSOLE64 *) &console_tokens64[0]) == exit_status64);
		assert(returned_process64->state == PROCESS_REAP64 && returned_process64->reap_pending);
		assert(!current_task64->process && live_pages64 > base && !irq_off64);
		fail_exit_free64 = free_fail64 = -1;
		free_persistent64 = 0;
		fixture64();
		assert(process64_exec_file("fixture", "fixture 한글 abc",
			(struct CONSOLE64 *) &console_tokens64[0]) == exit_status64);
		assert(live_pages64 == base && !irq_off64);
	}
	/* Failed preparation with persistent rollback errors is also queued by exec. */
	fixture64();
	alloc_fail64 = (int) alloc_calls64 + 1;
	free_fail64 = (int) free_calls64;
	free_persistent64 = 1;
	assert(process64_exec_file("fixture", "fixture 한글 abc", NULL) == -12);
	assert(live_pages64 > base && !current_task64->process);
	alloc_fail64 = free_fail64 = -1; free_persistent64 = 0;
	fixture64();
	assert(process64_exec_file("fixture", "fixture 한글 abc",
		(struct CONSOLE64 *) &console_tokens64[0]) == exit_status64);
	assert(live_pages64 == base);
	fixture64();
	unsigned int raw_before = raw_calls64;

	assert(process64_exec_file("fixture", "fixture 한글 abc", NULL) == exit_status64);
	assert(live_pages64 == base && raw_calls64 == raw_before && !irq_off64);
	current_task64 = NULL;
}

static void command_limits64(void)
{
	char line[PROCESS64_CMDLINE_MAX + 1], name[FD64_NAME_MAX + 1];
	struct PROCESS64 *process = NULL;
	struct PROCESS64_ARGS args;
	size_t base = live_pages64;

	fixture64();
	assert(process64_prepare_file(NULL, NULL, NULL, &process) == -22);
	assert(process64_prepare_file("", NULL, NULL, &process) == -22);
	assert(process64_prepare_file(" fixture", NULL, NULL, &process) == -22);
	memset(name, 'n', sizeof(name)); name[sizeof(name) - 1] = '\0';
	assert(process64_prepare_file(name, "fixture", NULL, &process) == -7);
	memset(line, 'a', sizeof(line)); line[sizeof(line) - 1] = '\0';
	assert(process64_prepare_file("fixture", line, NULL, &process) == -7);
	assert(process64_prepare_file("fixture", "a b c d e f g h i", NULL, &process) == -7);
	assert(process == NULL && live_pages64 == base);
	assert(parse_args64("  a b c d e f g h  ", &args) == 0 && args.argc == 8);
	assert(parse_args64("   ", &args) == 0 && args.argc == 0);
	line[sizeof(line) - 2] = '\0'; name[sizeof(name) - 2] = '\0';
	assert(process64_prepare_file(name, line, NULL, &process) == 0);
	assert(strcmp(opened_name64, name) == 0 && process->initial_argc == 1);
	const uintptr_t *argv = (const void *) (process->stack_storage.kernel_address +
		process->initial_argv - process->stack.base);
	const char *arg = (const void *) (process->stack_storage.kernel_address +
		argv[0] - process->stack.base);

	assert(argv[1] == 0 && strcmp(arg, line) == 0);
	assert(process64_discard_prepared(&process) == 0 && live_pages64 == base);
}

int main(void)
{
	(void) cleaned64;
	reset64(); assert(address_space_init64() == 0);
	isolation64();
	user_copy64(); failures_process64(); permissions_process64();
	execution64(); command_limits64();
	puts("process memory: private ELF, argv, W^X, slots, execution, exit/reap and rollback PASS");
	return 0;
}
