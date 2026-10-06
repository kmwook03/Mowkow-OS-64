#include <block64.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_STATUS_PORT 0x1f7
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DF  0x20
#define ATA_STATUS_BSY 0x80
#define ATA_LBA28_MAX  0x0fffffffULL

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static uint8_t statuses[8];
static uint32_t status_count;
static uint32_t status_index;
static uint32_t input_calls;
static uint32_t output_calls;
static uint32_t deadline_calls;
static uint32_t expiry_checks;
static uint32_t expire_after;

static void set_statuses(const uint8_t *values, uint32_t count,
	uint32_t expiry)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		statuses[i] = values[i];
	}
	status_count = count;
	status_index = 0;
	input_calls = 0;
	output_calls = 0;
	deadline_calls = 0;
	expiry_checks = 0;
	expire_after = expiry;
}

uint8_t io_in8(uint16_t port)
{
	uint8_t status;

	input_calls++;
	if (port != ATA_STATUS_PORT || status_count == 0) {
		return 0;
	}
	status = statuses[status_index];
	if (status_index + 1 < status_count) {
		status_index++;
	}
	return status;
}

void io_out8(uint16_t port, uint8_t value)
{
	(void) port;
	(void) value;
	output_calls++;
}

uint64_t poll_deadline64(uint32_t milliseconds)
{
	CHECK64(milliseconds == 5000U);
	deadline_calls++;
	return 1;
}

int poll_deadline_expired64(uint64_t deadline)
{
	CHECK64(deadline == 1);
	expiry_checks++;
	return expiry_checks >= expire_after;
}

static int test_lba28_boundaries(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	const uint8_t error[] = { ATA_STATUS_ERR };

	set_statuses(NULL, 0, 1);
	CHECK64(ata64_ops.read(UINT64_MAX, 0, NULL) == 0);
	CHECK64(ata64_ops.write(UINT64_MAX, 0, NULL) == 0);
	CHECK64(ata64_ops.read(ATA_LBA28_MAX + 1, 1, sector) == -1);
	CHECK64(ata64_ops.read(ATA_LBA28_MAX, 2, sector) == -1);
	CHECK64(ata64_ops.write(ATA_LBA28_MAX + 1, 1, sector) == -1);
	CHECK64(ata64_ops.write(ATA_LBA28_MAX, 2, sector) == -1);
	CHECK64(ata64_ops.read(0, 1, NULL) == -1);
	CHECK64(ata64_ops.write(0, 1, NULL) == -1);
	CHECK64(input_calls == 0 && output_calls == 0 && deadline_calls == 0);

	set_statuses(error, 1, 10);
	CHECK64(ata64_ops.read(ATA_LBA28_MAX, 1, sector) == -1);
	CHECK64(input_calls == 1 && output_calls == 0 && deadline_calls == 1);
	return 0;
}

static int test_status_errors(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	const uint8_t error[] = { ATA_STATUS_ERR };
	const uint8_t fault[] = { ATA_STATUS_DF };
	const uint8_t command_error[] = { 0, ATA_STATUS_ERR };

	set_statuses(error, 1, 10);
	CHECK64(ata64_ops.read(0, 1, sector) == -1);
	CHECK64(input_calls == 1 && output_calls == 0 && deadline_calls == 1);

	set_statuses(fault, 1, 10);
	CHECK64(ata64_ops.write(0, 1, sector) == -1);
	CHECK64(input_calls == 1 && output_calls == 0 && deadline_calls == 1);

	set_statuses(command_error, 2, 10);
	CHECK64(ata64_ops.read(0, 1, sector) == -1);
	CHECK64(input_calls == 2 && output_calls == 6 && deadline_calls == 2);
	return 0;
}

static int test_time_based_timeout(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	const uint8_t busy[] = { ATA_STATUS_BSY };

	set_statuses(busy, 1, 3);
	CHECK64(ata64_ops.read(0, 1, sector) == -1);
	CHECK64(input_calls == 3);
	CHECK64(expiry_checks == 3 && deadline_calls == 1);
	CHECK64(output_calls == 0);
	return 0;
}

int main(void)
{
	int status;

	status = test_lba28_boundaries();
	if (status != 0) {
		return status;
	}
	status = test_status_errors();
	if (status != 0) {
		return status;
	}
	return test_time_based_timeout();
}
