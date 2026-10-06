#include <stddef.h>
#include <stdint.h>

#define SDHCI64_HOST_TEST 1
#define SDHCI_TIMEOUT 8U
#include "../src64/arch/aarch64/sdhci64.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static uint32_t interrupt_values[4];
static uint32_t interrupt_count;
static uint32_t interrupt_index;
static uint32_t buffer_value;
static uint32_t buffer_reads;
static uint32_t buffer_writes;
static uint32_t first_buffer_write;
static uint32_t last_buffer_write;
static uint8_t reset_value;
static uint32_t reset_writes;
static int reset_stuck;

static void set_interrupts(const uint32_t *values, uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		interrupt_values[i] = values[i];
	}
	interrupt_count = count;
	interrupt_index = 0;
	buffer_reads = 0;
	buffer_writes = 0;
	first_buffer_write = 0;
	last_buffer_write = 0;
	reset_value = 0;
	reset_writes = 0;
	reset_stuck = 0;
}

uint8_t sdhci64_test_read8(uint32_t offset)
{
	if (offset == SDHCI_SOFTWARE_RESET) {
		return reset_stuck != 0 ? reset_value : 0;
	}
	return 0;
}

uint16_t sdhci64_test_read16(uint32_t offset)
{
	(void) offset;
	return 0;
}

uint32_t sdhci64_test_read32(uint32_t offset)
{
	uint32_t value;

	if (offset == SDHCI_PRESENT_STATE) {
		return 0;
	}
	if (offset == SDHCI_BUFFER) {
		buffer_reads++;
		return buffer_value;
	}
	if (offset != SDHCI_INT_STATUS || interrupt_count == 0) {
		return 0;
	}
	value = interrupt_values[interrupt_index];
	if (interrupt_index + 1U < interrupt_count) {
		interrupt_index++;
	}
	return value;
}

void sdhci64_test_write8(uint32_t offset, uint8_t value)
{
	if (offset == SDHCI_SOFTWARE_RESET) {
		reset_value = value;
		reset_writes++;
	}
}

void sdhci64_test_write16(uint32_t offset, uint16_t value)
{
	(void) offset;
	(void) value;
}

void sdhci64_test_write32(uint32_t offset, uint32_t value)
{
	if (offset != SDHCI_BUFFER) {
		return;
	}
	if (buffer_writes == 0) {
		first_buffer_write = value;
	}
	last_buffer_write = value;
	buffer_writes++;
}

static void set_response_bits(uint32_t response[4], uint32_t start,
	uint32_t size, uint32_t value)
{
	uint32_t bit;
	uint32_t offset;
	uint32_t position;

	for (bit = 0; bit < size; bit++) {
		position = start + bit;
		offset = 3U - position / 32U;
		if ((value & (1U << bit)) != 0) {
			response[offset] |= 1U << (position & 31U);
		}
	}
}

static int test_capacity_decode(void)
{
	uint32_t response[4] = { 0, 0, 0, 0 };
	uint64_t sectors;

	set_response_bits(response, 126, 2, 1);
	CHECK64(decode_card_capacity(response, &sectors) == 0);
	CHECK64(sectors == 1024);
	set_response_bits(response, 48, 22, 0x3fffffU);
	CHECK64(decode_card_capacity(response, &sectors) == 0);
	CHECK64(sectors == UINT32_MAX + 1ULL);

	response[0] = 0;
	response[1] = 0;
	response[2] = 0;
	response[3] = 0;
	set_response_bits(response, 80, 4, 9);
	set_response_bits(response, 62, 12, 1023);
	set_response_bits(response, 47, 3, 7);
	CHECK64(decode_card_capacity(response, &sectors) == 0);
	CHECK64(sectors == 524288);

	set_response_bits(response, 126, 2, 2);
	CHECK64(decode_card_capacity(response, &sectors) == -1);
	CHECK64(decode_card_capacity(NULL, &sectors) == -1);
	CHECK64(decode_card_capacity(response, NULL) == -1);
	return 0;
}

static int test_request_boundaries(void)
{
	uint8_t storage[BLOCK64_SECTOR_SIZE + 1] = { 0 };
	uint32_t argument;

	card_ready = 1;
	card_high_capacity = 1;
	card_sectors = 100;
	CHECK64(request_valid(99, 1, storage + 1) != 0);
	CHECK64(request_valid(100, 1, storage) == 0);
	CHECK64(request_valid(99, 2, storage) == 0);
	CHECK64(request_valid(UINT64_MAX, 1, storage) == 0);
	CHECK64(request_valid(0, 0, storage) == 0);
	CHECK64(request_valid(0, 1, NULL) == 0);
	CHECK64(request_valid(0, 1,
		(const void *) (UINTPTR_MAX - 255U)) == 0);
	CHECK64(card_argument(UINT32_MAX, &argument) == 0);
	CHECK64(argument == UINT32_MAX);
	CHECK64(card_argument(UINT32_MAX + 1ULL, &argument) == -1);

	card_high_capacity = 0;
	card_sectors = (UINT32_MAX / BLOCK64_SECTOR_SIZE) + 1ULL;
	CHECK64(request_valid(card_sectors - 1ULL, 1, storage) != 0);
	CHECK64(card_argument(card_sectors - 1ULL, &argument) == 0);
	CHECK64(argument == 0xfffffe00U);
	CHECK64(card_argument(card_sectors, &argument) == -1);
	CHECK64(card_argument(0, NULL) == -1);

	card_ready = 0;
	CHECK64(sdhci64_ops.read(UINT64_MAX, 0, NULL) == 0);
	CHECK64(sdhci64_ops.write(UINT64_MAX, 0, NULL) == 0);
	return 0;
}

static int test_unaligned_pio(void)
{
	uint8_t storage[BLOCK64_SECTOR_SIZE + 1];
	const uint32_t read_interrupts[] = {
		SDHCI_INT_CMD_COMPLETE,
		SDHCI_INT_BUF_READ_READY,
		SDHCI_INT_XFER_COMPLETE
	};
	const uint32_t write_interrupts[] = {
		SDHCI_INT_CMD_COMPLETE,
		SDHCI_INT_BUF_WRITE_READY,
		SDHCI_INT_XFER_COMPLETE
	};

	buffer_value = 0x44332211U;
	set_interrupts(read_interrupts, 3);
	CHECK64(read_data_command(17, 0, storage + 1,
		BLOCK64_SECTOR_SIZE) == 0);
	CHECK64(buffer_reads == BLOCK64_SECTOR_SIZE / sizeof(uint32_t));
	CHECK64(storage[1] == 0x11 && storage[2] == 0x22);
	CHECK64(storage[3] == 0x33 && storage[4] == 0x44);

	storage[1] = 0x78;
	storage[2] = 0x56;
	storage[3] = 0x34;
	storage[4] = 0x12;
	storage[BLOCK64_SECTOR_SIZE - 3] = 0xef;
	storage[BLOCK64_SECTOR_SIZE - 2] = 0xcd;
	storage[BLOCK64_SECTOR_SIZE - 1] = 0xab;
	storage[BLOCK64_SECTOR_SIZE] = 0x90;
	set_interrupts(write_interrupts, 3);
	CHECK64(write_data_command(24, 0, storage + 1,
		BLOCK64_SECTOR_SIZE) == 0);
	CHECK64(buffer_writes == BLOCK64_SECTOR_SIZE / sizeof(uint32_t));
	CHECK64(first_buffer_write == 0x12345678U);
	CHECK64(last_buffer_write == 0x90abcdefU);
	CHECK64(read_data_command(17, 0, storage, 3) == -1);
	CHECK64(write_data_command(24, 0, storage, 0) == -1);
	return 0;
}

static int test_command_error_recovery(void)
{
	const uint32_t error[] = { SDHCI_INT_ERROR | (1U << 16) };

	card_ready = 1;
	set_interrupts(error, 1);
	CHECK64(send_command(13, 0, SDHCI_CMD_RESP_SHORT, NULL) == -1);
	CHECK64(reset_writes == 1);
	CHECK64(reset_value == SDHCI_RESET_CMD);
	CHECK64(card_ready == 1);

	set_interrupts(error, 1);
	CHECK64(send_command(17, 0,
		SDHCI_CMD_RESP_SHORT | SDHCI_CMD_DATA, NULL) == -1);
	CHECK64(reset_writes == 1);
	CHECK64(reset_value == (SDHCI_RESET_CMD | SDHCI_RESET_DATA));
	CHECK64(card_ready == 1);

	set_interrupts(error, 1);
	reset_stuck = 1;
	CHECK64(send_command(17, 0,
		SDHCI_CMD_RESP_SHORT | SDHCI_CMD_DATA, NULL) == -1);
	CHECK64(reset_writes == 2);
	CHECK64(reset_value == SDHCI_RESET_ALL);
	CHECK64(card_ready == 0);
	CHECK64(recovery_failed == 1);
	return 0;
}

int main(void)
{
	int status;

	status = test_capacity_decode();
	if (status != 0) {
		return status;
	}
	status = test_request_boundaries();
	if (status != 0) {
		return status;
	}
	status = test_unaligned_pio();
	if (status != 0) {
		return status;
	}
	return test_command_error_recovery();
}
