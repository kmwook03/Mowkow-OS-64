#include <stddef.h>
#include <stdint.h>

#include "../src64/drivers/ahci64.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static uint64_t dma_words[AHCI_PRDT_MAX_BYTES / sizeof(uint64_t)];

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	(void) size;
	return 0;
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	(void) man;
	(void) addr;
	(void) size;
	return 0;
}

struct MEMMAN64 memman64;

uint32_t pci64_find_class(uint8_t base_class, uint8_t subclass)
{
	(void) base_class;
	(void) subclass;
	return PCI64_NONE;
}

uint32_t pci64_read32(uint32_t bdf, uint8_t offset)
{
	(void) bdf;
	(void) offset;
	return 0;
}

void pci64_write32(uint32_t bdf, uint8_t offset, uint32_t value)
{
	(void) bdf;
	(void) offset;
	(void) value;
}

static int test_dma_ranges(void)
{
	dma_64bit = 1;
	CHECK64(dma_range_valid(dma_words, BLOCK64_SECTOR_SIZE) != 0);
	CHECK64(dma_range_valid(NULL, BLOCK64_SECTOR_SIZE) == 0);
	CHECK64(dma_range_valid(dma_words, 0) == 0);
	CHECK64(dma_range_valid((uint8_t *) dma_words + 1,
		BLOCK64_SECTOR_SIZE) == 0);
	CHECK64(dma_range_valid((const void *) (UINTPTR_MAX - 255U), 512) == 0);
#if UINTPTR_MAX > UINT32_MAX
	dma_64bit = 0;
	CHECK64(dma_range_valid((const void *) ((uintptr_t) UINT32_MAX + 1U),
		BLOCK64_SECTOR_SIZE) == 0);
#endif
	dma_64bit = 1;
	return 0;
}

static int test_data_request_boundaries(void)
{
	ready = 1;
	sector_total = 1000;
	dma_64bit = 1;
	CHECK64(data_request_valid(999, 1, dma_words) != 0);
	CHECK64(data_request_valid(1000, 1, dma_words) == 0);
	CHECK64(data_request_valid(999, 2, dma_words) == 0);
	CHECK64(data_request_valid(AHCI_LBA48_SECTORS - 1, 2,
		dma_words) == 0);
	CHECK64(data_request_valid(0, 0, dma_words) == 0);
	CHECK64(data_request_valid(0, 1, NULL) == 0);
	return 0;
}

static int test_command_fields(void)
{
	uint32_t prdt_sectors = (uint32_t) (AHCI_PRDT_MAX_BYTES /
		BLOCK64_SECTOR_SIZE);

	ready = 1;
	sector_total = AHCI_LBA48_SECTORS;
	dma_64bit = 1;
	CHECK64(command_valid(ATA_CMD_READ_DMA_EX, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE, 0) != 0);
	CHECK64(command_valid(ATA_CMD_WRITE_DMA_EX, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE, 1) != 0);
	CHECK64(command_valid(ATA_CMD_READ_DMA_EX, 0, 0, dma_words, 0, 0) == 0);
	CHECK64(command_valid(ATA_CMD_READ_DMA_EX, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE - 1, 0) == 0);
	CHECK64(command_valid(ATA_CMD_READ_DMA_EX, 0, prdt_sectors + 1,
		dma_words, (prdt_sectors + 1) * BLOCK64_SECTOR_SIZE, 0) == 0);
	CHECK64(command_valid(ATA_CMD_READ_DMA_EX, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE, 1) == 0);
	CHECK64(command_valid(ATA_CMD_WRITE_DMA_EX, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE, 0) == 0);
	CHECK64(command_valid(0xff, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE, 0) == 0);

	ready = 0;
	CHECK64(command_valid(ATA_CMD_IDENTIFY, 0, 0, dma_words,
		BLOCK64_SECTOR_SIZE, 0) != 0);
	CHECK64(command_valid(ATA_CMD_IDENTIFY, 0, 1, dma_words,
		BLOCK64_SECTOR_SIZE, 0) == 0);
	CHECK64(command_valid(ATA_CMD_IDENTIFY, 0, 0, dma_words,
		BLOCK64_SECTOR_SIZE - 1, 0) == 0);
	return 0;
}

int main(void)
{
	int status;

	status = test_dma_ranges();
	if (status != 0) {
		return status;
	}
	status = test_data_request_boundaries();
	if (status != 0) {
		return status;
	}
	return test_command_fields();
}
