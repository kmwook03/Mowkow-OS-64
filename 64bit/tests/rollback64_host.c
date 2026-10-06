#include <asmfunc64.h>
#include <block64.h>
#include <console64.h>
#include <elf64_loader.h>
#include <memory64.h>
#include <mtask64.h>
#include <pci64.h>
#include <process64.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK64(condition) do { if (!(condition)) return __LINE__; } while (0)

#define TEST_HBA_GHC 0x04U
#define TEST_HBA_PI  0x0cU

enum ALLOC_MODE64 {
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

_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t process_stack[64U * 1024U];
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t process_heap[1024U * 1024U];
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t dma_page[MEMMAN64_PAGE_SIZE];
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t fake_abar[0x200U];

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	alloc_count++;
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
	if (free_count < sizeof freed / sizeof freed[0]) {
		freed[free_count] = addr;
	}
	free_count++;
	return 0;
}

int elf64_load_process(const char *path, struct PROCESS64 *process)
{
	(void) path;
	process->entry = 0x401000U;
	process->image.base = 0x400000U;
	process->image.size = 0x2000U;
	return 0;
}

void elf64_release_process(struct PROCESS64 *process)
{
	(void) process;
	image_releases++;
}

struct TASK64 *task_now64(void)
{
	return current_task;
}

int enter_user_mode64(uintptr_t entry, uintptr_t stack, uint64_t argc,
	uintptr_t argv, uint16_t code_selector, uint16_t data_selector,
	uintptr_t *saved_kernel_rsp)
{
	(void) entry;
	(void) stack;
	(void) argc;
	(void) argv;
	(void) code_selector;
	(void) data_selector;
	(void) saved_kernel_rsp;
	return -1;
}

void console64_set_raw(int on)
{
	(void) on;
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
	for (i = 0; i < sizeof freed / sizeof freed[0]; i++) {
		freed[i] = 0;
	}
}

static int test_process_heap_failure64(void)
{
	reset_alloc_state64(ALLOC_MODE_PROCESS_HEAP_FAIL);
	CHECK64(process64_exec_file("app", "app", NULL) == -3);
	CHECK64(alloc_count == 2U);
	CHECK64(free_count == 1U);
	CHECK64(freed[0] == (uintptr_t) process_stack);
	CHECK64(image_releases == 1U);
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

	status = test_process_heap_failure64();
	if (status != 0) {
		return status;
	}
	status = test_process_reverse_cleanup64();
	if (status != 0) {
		return status;
	}
	return test_ahci_dma_rollback64();
}
