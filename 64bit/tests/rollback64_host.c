#include <asmfunc64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <block64.h>
#include <console64.h>
#include <elf64_loader.h>
#include <memory64.h>
#include <mtask64.h>
#include <pci64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../src64/kernel/process64.c"

#define CHECK64(condition) do { if (!(condition)) return __LINE__; } while (0)

#define TEST_HBA_GHC 0x04U
#define TEST_HBA_PI  0x0cU

enum ALLOC_MODE64 {
	ALLOC_MODE_PROCESS_STACK_FAIL,
	ALLOC_MODE_PROCESS_HEAP_FAIL,
	ALLOC_MODE_PROCESS_OK,
	ALLOC_MODE_AHCI,
};

struct MEMMAN64 memman64;

static enum ALLOC_MODE64 alloc_mode;
static uint32_t alloc_count;
static uint32_t free_count;
static uintptr_t freed[4];
static uint32_t image_releases;
static struct TASK64 *current_task;
static uint32_t pci_command;
static uint32_t pci_command_writes;
static int elf_status;
static int enter_status;
static int exit_requested;
static int exit_status;
static uint32_t entered;
static uint32_t raw_restores;
static int cleanup_error;
static struct PROCESS64 *test_image_owner;
static struct PROCESS64 *loaded_process;
static struct TASK64 test_task;
static const char *expected_args[PROCESS64_MAX_ARGS];
static size_t expected_argc;
static int check_args;
static char loaded_name[FD64_NAME_MAX];
static int leave_resources_open;
static int console_tokens[2];
static int console_raw[2];
#ifdef __aarch64__
static uint32_t map_count;
static uint32_t unmap_count;
static uint32_t map_failure;
static int mapping_live;
#endif

_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t process_stack[64U * 1024U];
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t process_heap[1024U * 1024U];
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t dma_page[MEMMAN64_PAGE_SIZE];
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t fake_abar[0x200U];

static int files_cleared64(const struct PROCESS64 *process)
{
	static const struct PROCESS64_FILE empty;
	size_t i;

	for (i = 0; i < PROCESS64_MAX_FILES; i++) {
		if (memcmp(&process->files[i], &empty, sizeof empty) != 0) {
			return 0;
		}
	}
	return 1;
}

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	alloc_count++;
	if (alloc_mode == ALLOC_MODE_PROCESS_STACK_FAIL) {
		return 0;
	}
	if (alloc_mode == ALLOC_MODE_PROCESS_HEAP_FAIL) {
		return alloc_count == 1U ? (uintptr_t) process_stack : 0;
	}
	if (alloc_mode == ALLOC_MODE_PROCESS_OK) {
		if (size == sizeof process_stack) {
			return (uintptr_t) process_stack;
		}
		if (size == sizeof process_heap) {
			return (uintptr_t) process_heap;
		}
		return 0;
	}
	if (alloc_mode == ALLOC_MODE_AHCI && size == sizeof dma_page) {
		return (uintptr_t) dma_page;
	}
	return 0;
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	(void) man;
	(void) size;
	if (current_task != NULL && current_task->process != NULL) {
		cleanup_error = 1;
	}
	if (loaded_process != NULL && files_cleared64(loaded_process) == 0) {
		cleanup_error = 1;
	}
#ifdef __aarch64__
	if (mapping_live != 0) {
		cleanup_error = 1;
	}
#endif
	if (free_count < sizeof freed / sizeof freed[0]) {
		freed[free_count] = addr;
	}
	free_count++;
	return 0;
}

int elf64_load_process(const char *path, struct PROCESS64 *process)
{
	memcpy(loaded_name, path, strlen(path) + 1);
	loaded_process = process;
	if (test_image_owner != NULL) {
		return -8;
	}
	if (elf_status != 0) {
		return elf_status;
	}
	test_image_owner = process;
	process->entry = 0x401000U;
	process->image.base = 0x400000U;
	process->image.size = 0x2000U;
	return 0;
}

void elf64_release_process(struct PROCESS64 *process)
{
	if (test_image_owner == process) {
		if (process->heap.size != 0 || process->stack.size != 0 ||
			process->user_mappings != 0 || files_cleared64(process) == 0) {
			cleanup_error = 1;
		}
		image_releases++;
		test_image_owner = NULL;
	}
}

#ifdef __aarch64__
int arch64_user_map_range(uintptr_t base, size_t size, int executable)
{
	(void) base;
	(void) size;
	(void) executable;
	map_count++;
	/* Even a failed map can publish partial state requiring cleanup. */
	mapping_live = 1;
	return map_count == map_failure ? -1 : 0;
}

void arch64_user_unmap_all(void)
{
	if (test_image_owner == NULL || test_image_owner->user_mappings == 0 ||
		free_count != 0 || files_cleared64(test_image_owner) == 0 ||
		(current_task != NULL && current_task->process != NULL)) {
		cleanup_error = 1;
	}
	unmap_count++;
	mapping_live = 0;
}
#endif

struct TASK64 *task_now64(void)
{
	return current_task;
}

int enter_user_mode64(uintptr_t entry, uintptr_t stack, uint64_t argc,
	uintptr_t argv, uint16_t code_selector, uint16_t data_selector,
	uintptr_t *saved_kernel_rsp)
{
	(void) entry;
	(void) code_selector;
	(void) data_selector;
	CHECK64(current_task != NULL && current_task->is_user == 1);
	CHECK64(process64_current() == test_image_owner);
	CHECK64((stack & 15U) == 0 && stack == argv);
	CHECK64(argc <= PROCESS64_MAX_ARGS);
	CHECK64(((uintptr_t *) argv)[argc] == 0);
	if (check_args != 0) {
		uint64_t i;

		CHECK64(argc == expected_argc);
		for (i = 0; i < argc; i++) {
			CHECK64(strcmp((const char *) ((uintptr_t *) argv)[i],
				expected_args[i]) == 0);
		}
	}
	entered++;
	*saved_kernel_rsp = 0x123000U;
	if (leave_resources_open != 0) {
		size_t i;

		for (i = 3; i < PROCESS64_MAX_FILES; i++) {
			memset(&test_image_owner->files[i].fh, 0xa5,
				sizeof(test_image_owner->files[i].fh));
			test_image_owner->files[i].used = 1;
		}
	}
	if (exit_requested != 0) {
		process64_exit_current(exit_status);
		/* Exit/fault marking must leave resources alive on the exception stack. */
		CHECK64(process64_current() == test_image_owner);
		CHECK64(test_image_owner->heap.size != 0 && image_releases == 0);
		CHECK64(free_count == 0 && raw_restores == 0);
		if (leave_resources_open != 0) {
			CHECK64(test_image_owner->files[3].used == 1);
		}
	}
	return enter_status;
}

void console64_set_raw_con(struct CONSOLE64 *con, int on)
{
	if (con == NULL) {
		return;
	}
	if (on != 0 || process64_current() != test_image_owner ||
		current_task == NULL || current_task->is_user == 0 ||
		con != test_image_owner->console) {
		cleanup_error = 1;
	}
	if (con == (struct CONSOLE64 *) (void *) &console_tokens[0]) {
		console_raw[0] = 0;
	} else if (con == (struct CONSOLE64 *) (void *) &console_tokens[1]) {
		console_raw[1] = 0;
	} else {
		cleanup_error = 1;
	}
	raw_restores++;
}

uint32_t pci64_find_class(uint8_t class_code, uint8_t subclass)
{
	CHECK64(class_code == 0x01U);
	CHECK64(subclass == 0x06U);
	return 1U;
}

uint32_t pci64_read32(uint32_t bdf, uint8_t offset)
{
	CHECK64(bdf == 1U);
	if (offset == PCI64_REG_COMMAND) {
		return pci_command;
	}
	if (offset == PCI64_REG_BAR5) {
		return (uint32_t) (uintptr_t) fake_abar;
	}
	return 0;
}

void pci64_write32(uint32_t bdf, uint8_t offset, uint32_t value)
{
	if (bdf == 1U && offset == PCI64_REG_COMMAND) {
		pci_command = value;
		pci_command_writes++;
	}
}

static void reset_alloc_state64(enum ALLOC_MODE64 mode)
{
	uint32_t i;

	alloc_mode = mode;
	alloc_count = 0;
	free_count = 0;
	image_releases = 0;
	current_task = NULL;
	elf_status = 0;
	enter_status = -1;
	exit_requested = 0;
	exit_status = 0;
	entered = 0;
	raw_restores = 0;
	cleanup_error = 0;
	test_image_owner = NULL;
	loaded_process = NULL;
	check_args = 0;
	loaded_name[0] = '\0';
	leave_resources_open = 0;
	console_raw[0] = console_raw[1] = 1;
	memset(&test_task, 0, sizeof test_task);
#ifdef __aarch64__
	map_count = 0;
	unmap_count = 0;
	map_failure = 0;
	mapping_live = 0;
#endif
	for (i = 0; i < sizeof freed / sizeof freed[0]; i++) {
		freed[i] = 0;
	}
}

static int check_process_cleared64(void)
{
	struct PROCESS64 empty = {0};

	CHECK64(loaded_process != NULL);
	CHECK64(memcmp(loaded_process, &empty, sizeof empty) == 0);
	CHECK64(cleanup_error == 0);
	CHECK64(test_task.process == NULL && test_task.is_user == 0);
	CHECK64(test_task.kernel_rsp == 0);
	return 0;
}

static int test_process_early_failure64(void)
{
	struct PROCESS64 other = {0};

	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	elf_status = -4;
	CHECK64(process64_exec_file("app", "app", NULL) == -2);
	CHECK64(alloc_count == 0 && free_count == 0 && image_releases == 0);
	CHECK64(check_process_cleared64() == 0);
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	test_image_owner = &other;
#ifdef __aarch64__
	mapping_live = 1;
#endif
	CHECK64(process64_exec_file("app", "app", NULL) == -8);
	CHECK64(test_image_owner == &other);
	CHECK64(alloc_count == 0 && free_count == 0 && image_releases == 0);
#ifdef __aarch64__
	CHECK64(mapping_live == 1 && unmap_count == 0);
#endif
	CHECK64(check_process_cleared64() == 0);
	reset_alloc_state64(ALLOC_MODE_PROCESS_STACK_FAIL);
	CHECK64(process64_exec_file("app", "app", NULL) == -3);
	CHECK64(alloc_count == 1 && free_count == 0 && image_releases == 1);
#ifdef __aarch64__
	CHECK64(map_count == 0 && unmap_count == 0);
#endif
	CHECK64(check_process_cleared64() == 0);
	return 0;
}

static int test_process_heap_failure64(void)
{
	reset_alloc_state64(ALLOC_MODE_PROCESS_HEAP_FAIL);
	CHECK64(process64_exec_file("app", "app", NULL) == -3);
	CHECK64(alloc_count == 2U);
	CHECK64(free_count == 1U);
	CHECK64(freed[0] == (uintptr_t) process_stack);
	CHECK64(image_releases == 1U);
	CHECK64(check_process_cleared64() == 0);
#ifdef __aarch64__
	CHECK64(map_count == 0 && unmap_count == 0);
#endif
	return 0;
}

static int test_process_reverse_cleanup64(void)
{
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	CHECK64(process64_exec_file("app", "app arg", NULL) == -4);
	CHECK64(alloc_count == 2U);
	CHECK64(free_count == 2U);
	CHECK64(freed[0] == (uintptr_t) process_heap);
	CHECK64(freed[1] == (uintptr_t) process_stack);
	CHECK64(image_releases == 1U);
	CHECK64(check_process_cleared64() == 0);
#ifdef __aarch64__
	CHECK64(map_count == 3 && unmap_count == 1 && mapping_live == 0);
#endif
	return 0;
}

#ifdef __aarch64__
static int test_process_mapping_failure64(void)
{
	uint32_t stage;

	for (stage = 1; stage <= 3; stage++) {
		reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
		map_failure = stage;
		CHECK64(process64_exec_file("app", "app", NULL) == -5);
		CHECK64(map_count == stage && unmap_count == 1 && mapping_live == 0);
		CHECK64(free_count == 2 && image_releases == 1);
		CHECK64(freed[0] == (uintptr_t) process_heap);
		CHECK64(freed[1] == (uintptr_t) process_stack);
		CHECK64(entered == 0 && raw_restores == 0);
		CHECK64(check_process_cleared64() == 0);
	}
	return 0;
}
#endif

static int test_process_return_cleanup64(void)
{
	int statuses[] = {0, 17, PROCESS64_EXIT_FAULT_BASE + 14};
	struct CONSOLE64 *launch_console = (struct CONSOLE64 *) (void *) &console_tokens[0];
	size_t i;

	for (i = 0; i < sizeof statuses / sizeof statuses[0]; i++) {
		reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
		current_task = &test_task;
		exit_requested = 1;
		exit_status = statuses[i];
		leave_resources_open = 1;
		CHECK64(process64_exec_file("app", "app arg", launch_console) == statuses[i]);
		CHECK64(entered == 1 && raw_restores == 1);
		CHECK64(console_raw[0] == 0 && console_raw[1] == 1);
		CHECK64(free_count == 2 && image_releases == 1);
		CHECK64(freed[0] == (uintptr_t) process_heap);
		CHECK64(freed[1] == (uintptr_t) process_stack);
		CHECK64(test_image_owner == NULL);
#ifdef __aarch64__
		CHECK64(map_count == 3 && unmap_count == 1 && mapping_live == 0);
#endif
		CHECK64(check_process_cleared64() == 0);
		/* Repeated cleanup must not touch any terminal or resource again. */
		process_cleanup64(loaded_process, &test_task);
		CHECK64(raw_restores == 1 && free_count == 2 && image_releases == 1);
#ifdef __aarch64__
		CHECK64(unmap_count == 1);
#endif
	}
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	current_task = &test_task;
	leave_resources_open = 1;
	CHECK64(process64_exec_file("app", "app", launch_console) == -1);
	CHECK64(entered == 1 && raw_restores == 1 && image_releases == 1);
	CHECK64(check_process_cleared64() == 0);
	CHECK64(console_raw[0] == 0 && console_raw[1] == 1);
	/* A console-less process must still release files and memory. */
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	current_task = &test_task;
	leave_resources_open = 1;
	CHECK64(process64_exec_file("app", "app", NULL) == -1);
	CHECK64(entered == 1 && raw_restores == 0 && image_releases == 1);
	CHECK64(console_raw[0] == 1 && console_raw[1] == 1);
	CHECK64(free_count == 2 && check_process_cleared64() == 0);
	return 0;
}

static int test_command_limits64(void)
{
	char line[PROCESS64_CMDLINE_MAX + 1];
	char name[FD64_NAME_MAX + 1];
	struct PROCESS64_ARGS args;

	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	CHECK64(process64_exec_file(NULL, NULL, NULL) == PROCESS64_ERR_INVALID);
	CHECK64(process64_exec_file("", NULL, NULL) == PROCESS64_ERR_INVALID);
	CHECK64(process64_exec_file(" app", NULL, NULL) == PROCESS64_ERR_INVALID);
	memset(name, 'n', sizeof name);
	name[sizeof name - 1] = '\0';
	CHECK64(process64_exec_file(name, "app", NULL) == PROCESS64_ERR_ARGS);
	memset(line, 'x', sizeof line);
	line[sizeof line - 1] = '\0';
	CHECK64(process64_exec_file("app", line, NULL) == PROCESS64_ERR_ARGS);
	CHECK64(process64_exec_file("app", "a b c d e f g h i", NULL) ==
		PROCESS64_ERR_ARGS);
	CHECK64(loaded_process == NULL && alloc_count == 0 && entered == 0);
	CHECK64(free_count == 0 && test_image_owner == NULL);
	line[PROCESS64_CMDLINE_MAX - 1] = '\0';
	CHECK64(parse_args64(line, &args) == 0);
	CHECK64(args.argc == 1 && args.lengths[0] == PROCESS64_CMDLINE_MAX - 1);
	CHECK64(parse_args64("  a b  c d e f g h  ", &args) == 0);
	CHECK64(args.argc == PROCESS64_MAX_ARGS);
	CHECK64(parse_args64("", &args) == 0 && args.argc == 0);
	CHECK64(parse_args64("     ", &args) == 0 && args.argc == 0);
	/* The longest accepted filename must reach the loader without truncation. */
	name[FD64_NAME_MAX - 1] = '\0';
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	current_task = &test_task;
	CHECK64(process64_exec_file(name, "app", NULL) == -1);
	CHECK64(strcmp(loaded_name, name) == 0 && entered == 1);
	CHECK64(check_process_cleared64() == 0);
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	current_task = &test_task;
	check_args = 1;
	expected_argc = PROCESS64_MAX_ARGS;
	expected_args[0] = "a";
	expected_args[1] = "b";
	expected_args[2] = "c";
	expected_args[3] = "d";
	expected_args[4] = "e";
	expected_args[5] = "f";
	expected_args[6] = "g";
	expected_args[7] = "h";
	CHECK64(process64_exec_file("app", "  a b  c d e f g h  ", NULL) == -1);
	CHECK64(entered == 1 && check_process_cleared64() == 0);
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	current_task = &test_task;
	check_args = 1;
	expected_argc = 2;
	expected_args[0] = "app";
	expected_args[1] = "한글";
	CHECK64(process64_exec_file("app 한글", NULL, NULL) == -1);
	CHECK64(strcmp(loaded_name, "app") == 0);
	CHECK64(entered == 1 && check_process_cleared64() == 0);
	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	current_task = &test_task;
	check_args = 1;
	expected_argc = 1;
	expected_args[0] = line;
	CHECK64(process64_exec_file("app", line, NULL) == -1);
	CHECK64(entered == 1 && check_process_cleared64() == 0);
	return 0;
}

static int test_argument_stack64(void)
{
	_Alignas(16) uint8_t storage[64];
	uint8_t original[sizeof storage];
	char long_arg[80];
	struct PROCESS64 process = {0};
	struct PROCESS64_ARGS args;
	uintptr_t rsp;
	uintptr_t argv;
	uintptr_t *vector;

	/* Distinct backing/user bases exercise the AArch64 address contract. */
	process.stack.base = 0x10000;
	process.stack.size = sizeof storage;
	process.stack_backing = (uintptr_t) storage;
	CHECK64(parse_args64("x yz", &args) == 0);
	memset(storage, 0xa5, sizeof storage);
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == 0);
	CHECK64(rsp == argv && (rsp & 15U) == 0);
	CHECK64(rsp >= process.stack.base + 16);
	vector = (uintptr_t *) (storage + (argv - process.stack.base));
	CHECK64(vector[0] == process.stack.base + 62);
	CHECK64(vector[1] == process.stack.base + 59 && vector[2] == 0);
	CHECK64(strcmp((const char *) storage + 62, "x") == 0);
	CHECK64(strcmp((const char *) storage + 59, "yz") == 0);
	CHECK64(storage[0] == 0xa5 && storage[58] == 0xa5);
	memset(storage, 0xa5, sizeof storage);
	memcpy(original, storage, sizeof storage);
	rsp = 123;
	argv = 456;
	process.stack.size = 32;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	CHECK64(memcmp(storage, original, sizeof storage) == 0);
	CHECK64(rsp == 123 && argv == 456);
	process.stack.size = sizeof storage;
	memset(long_arg, 'x', sizeof long_arg);
	long_arg[sizeof long_arg - 1] = '\0';
	CHECK64(parse_args64(long_arg, &args) == 0);
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	CHECK64(memcmp(storage, original, sizeof storage) == 0);
	CHECK64(parse_args64("x yz", &args) == 0);
	process.stack.base = UINTPTR_MAX - 15;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	process.stack.base = 0x10001;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	process.stack.base = 0x10000;
	process.stack_backing = UINTPTR_MAX - 15;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	process.stack_backing = (uintptr_t) storage + 1;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	process.stack_backing = (uintptr_t) storage;
	process.stack.size--;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	process.stack.size = sizeof storage;
	args.argc = PROCESS64_MAX_ARGS + 1;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_INVALID);
	CHECK64(parse_args64("x", &args) == 0);
	args.lengths[0] = SIZE_MAX;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_INVALID);
	CHECK64(parse_args64("", &args) == 0);
	process.stack.size = 16;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == PROCESS64_ERR_STACK);
	CHECK64(memcmp(storage, original, sizeof storage) == 0);
	/* Exact fit includes the NULL vector entry and first call's aligned slot. */
	process.stack.size = 32;
	CHECK64(setup_args64(&process, &args, &rsp, &argv) == 0);
	CHECK64(rsp == process.stack.base + 16);
	CHECK64(*(uintptr_t *) (storage + 16) == 0);
	return 0;
}

static int test_user_ranges64(void)
{
	struct PROCESS64 process = {0};
	static const struct {
		uintptr_t ptr;
		size_t size;
		int valid;
	} cases[] = {
		{0, 0, 0}, {0, 1, 0}, {0x0fff, 0, 0}, {0x0fff, 1, 0},
		{0x1000, 0, 1}, {0x1000, 0x100, 1}, {0x10ff, 1, 1},
		{0x10ff, 2, 0}, {0x1100, 0, 1}, {0x1100, 1, 0},
		{0x1500, 0, 0}, {0x1070, 0x1000, 0},
		{0x2000, 0x80, 1}, {0x2000, 0x81, 0}, {0x2000, 0x100, 0},
		{0x207f, 1, 1}, {0x207f, 2, 0}, {0x2080, 0x80, 1},
		{0x20ff, 1, 1}, {0x20ff, 2, 0}, {0x2100, 0, 1}, {0x2101, 0, 0},
		{0x1000, SIZE_MAX, 0}, {UINTPTR_MAX - 15, 32, 0},
		{UINTPTR_MAX, 1, 0}, {UINTPTR_MAX, 0, 0},
	};
	size_t i;

	reset_alloc_state64(ALLOC_MODE_PROCESS_OK);
	process.image.base = 0x1000;
	process.image.size = 0x100;
	process.stack.base = 0x2000;
	process.stack.size = 0x80;
	/* Adjacent stack and heap must still contain a request independently. */
	process.heap.base = 0x2080;
	process.heap.size = 0x80;
	current_task = &test_task;
	test_task.process = &process;
	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		CHECK64(process64_user_range_valid((const void *) cases[i].ptr,
			cases[i].size) == cases[i].valid);
	}
	process.image.base = UINTPTR_MAX - 15;
	process.image.size = 32;
	CHECK64(process64_user_range_valid((const void *) process.image.base, 1) == 0);
	CHECK64(process64_user_range_valid((const void *) process.image.base, 0) == 0);
	CHECK64(process64_user_range_valid((const void *) 0x2000, 1) == 1);
	process.image.base = UINTPTR_MAX - 31;
	process.image.size = 31;
	CHECK64(process64_user_range_valid((const void *) process.image.base, 31) == 1);
	CHECK64(process64_user_range_valid((const void *) (UINTPTR_MAX - 1), 1) == 1);
	CHECK64(process64_user_range_valid((const void *) UINTPTR_MAX, 0) == 1);
	CHECK64(process64_user_range_valid((const void *) UINTPTR_MAX, 1) == 0);
	process.image.size = 0;
	CHECK64(process64_user_range_valid((const void *) process.image.base, 0) == 0);
	process.image.base = 0;
	process.image.size = 128;
	CHECK64(process64_user_range_valid((const void *) 1, 1) == 0);
	CHECK64(process64_user_range_valid((const void *) 0x2000, 1) == 1);
	test_task.process = NULL;
	CHECK64(process64_user_range_valid((const void *) 0x2000, 1) == 0);
	CHECK64(process64_user_range_valid((const void *) 0x2000, 0) == 0);
	current_task = NULL;
	CHECK64(process64_user_range_valid((const void *) 0x2000, 0) == 0);
	return 0;
}

static int test_ahci_dma_rollback64(void)
{
	uint32_t *ghc;
	uint32_t *ports;

	reset_alloc_state64(ALLOC_MODE_AHCI);
	ghc = (uint32_t *) (void *) (fake_abar + TEST_HBA_GHC);
	ports = (uint32_t *) (void *) (fake_abar + TEST_HBA_PI);
	*ghc = 0x02U;
	*ports = 0;
	pci_command = PCI64_COMMAND_IO;
	pci_command_writes = 0;
	CHECK64(ahci64_probe() == -1);
	CHECK64(alloc_count == 1U);
	CHECK64(free_count == 1U);
	CHECK64(freed[0] == (uintptr_t) dma_page);
	CHECK64(*ghc == 0x02U);
	CHECK64(pci_command == PCI64_COMMAND_IO);
	CHECK64(pci_command_writes == 2U);
	return 0;
}

int main(void)
{
	int status;

	CHECK64(test_process_early_failure64() == 0);
	status = test_process_heap_failure64();
	if (status != 0) {
		return status;
	}
	status = test_process_reverse_cleanup64();
	if (status != 0) {
		return status;
	}
#ifdef __aarch64__
	CHECK64(test_process_mapping_failure64() == 0);
#endif
	CHECK64(test_process_return_cleanup64() == 0);
	CHECK64(test_command_limits64() == 0);
	CHECK64(test_argument_stack64() == 0);
	CHECK64(test_user_ranges64() == 0);
	return test_ahci_dma_rollback64();
}
