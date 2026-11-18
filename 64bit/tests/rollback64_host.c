#include <asmfunc64.h>
#include <block64.h>
#include <pci64.h>
#include <string.h>
#include "../src64/kernel/process64.c"

#define CHECK64(condition) do { if (!(condition)) return __LINE__; } while (0)
#define TEST_HBA_GHC 0x04U
#define TEST_HBA_PI 0x0cU
#define ALLOC_MODE_PROCESS_OK 0
#define ALLOC_MODE_AHCI 1

struct MEMMAN64 memman64;
static uint64_t process_irq64;
static struct TASK64 test_task;
static struct TASK64 *current_task;
static uint32_t alloc_count, free_count, pci_command, pci_command_writes;
static uintptr_t freed[4];
_Alignas(MEMMAN64_PAGE_SIZE) static uint8_t dma_page[MEMMAN64_PAGE_SIZE];
_Alignas(MEMMAN64_PAGE_SIZE) static uint8_t fake_abar[0x200U];

uint64_t platform_irq_save64(void) { return process_irq64++; }
void platform_irq_restore64(uint64_t flags) { process_irq64 = flags; }
struct TASK64 *task_now64(void) { return current_task; }
/* This fixture tests range envelopes; real leaves are covered by process_memory64. */
int address_space_validate64(const struct ADDRESS_SPACE64 *space, uintptr_t address,
	size_t size, unsigned int access)
{
	(void) space; (void) address; (void) size; (void) access;
	return 0;
}
uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	alloc_count++;
	return size == sizeof dma_page ? (uintptr_t) dma_page : 0;
}
int memman64_free_4k(struct MEMMAN64 *man, uintptr_t address, size_t size)
{
	(void) man; (void) size;
	if (free_count < 4) { freed[free_count] = address; }
	free_count++;
	return 0;
}
static void reset_alloc_state64(int mode)
{
	(void) mode;
	alloc_count = free_count = 0;
	current_task = NULL;
	memset(&test_task, 0, sizeof test_task);
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
	CHECK64(test_argument_stack64() == 0);
	CHECK64(test_user_ranges64() == 0);
	return test_ahci_dma_rollback64();
}
