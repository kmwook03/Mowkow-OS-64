#include <block64.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_SECTORS 1000
#define PARTITION_LBA  100

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static uint64_t fake_sectors = DEVICE_SECTORS;
static uint64_t last_lba;
static uint32_t last_count;
static uint32_t read_calls;
static uint32_t write_calls;
static int invalid_probe;

static void write32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
	p[2] = (uint8_t) (value >> 16);
	p[3] = (uint8_t) (value >> 24);
}

static void clear_sector(uint8_t *sector)
{
	uint32_t i;

	for (i = 0; i < BLOCK64_SECTOR_SIZE; i++) {
		sector[i] = 0;
	}
}

static int fake_read(uint64_t lba, uint32_t count, void *dst)
{
	uint8_t *sector = (uint8_t *) dst;

	read_calls++;
	last_lba = lba;
	last_count = count;
	if (fake_sectors != 0 &&
			(lba >= fake_sectors || count > fake_sectors - lba)) {
		invalid_probe = 1;
		return -1;
	}
	if (lba == 0 && count == 1) {
		clear_sector(sector);
		sector[510] = 0x55;
		sector[511] = 0xaa;
		sector[446 + 4] = 0x0c;
		write32(sector + 446 + 8, DEVICE_SECTORS);
		sector[462 + 4] = 0x0c;
		write32(sector + 462 + 8, PARTITION_LBA);
	} else if (lba == PARTITION_LBA && count == 1) {
		clear_sector(sector);
		sector[82] = 'F';
		sector[83] = 'A';
		sector[84] = 'T';
	}
	return 0;
}

static int fake_write(uint64_t lba, uint32_t count, const void *src)
{
	(void) src;
	write_calls++;
	last_lba = lba;
	last_count = count;
	return 0;
}

static uint64_t fake_sector_count(void)
{
	return fake_sectors;
}

const struct BLOCK64_OPS ahci64_ops = {
	"fake-ahci",
	fake_read,
	fake_write,
	fake_sector_count
};

const struct BLOCK64_OPS ata64_ops = {
	"fake-ata",
	fake_read,
	fake_write,
	fake_sector_count
};

const struct BLOCK64_OPS sdhci64_ops = {
	"fake-sdhci",
	fake_read,
	fake_write,
	fake_sector_count
};

int ahci64_probe(void)
{
	return 0;
}

int sdhci64_probe(void)
{
	return 0;
}

static int test_partition_and_capacity(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	uint32_t calls;

	CHECK64(block64_init() == 0);
	CHECK64(invalid_probe == 0);
	CHECK64(read_calls == 2);
	CHECK64(block64_part_base() == PARTITION_LBA);
	CHECK64(block64_sector_count() == DEVICE_SECTORS - PARTITION_LBA);

	CHECK64(block64_read(0, 1, sector) == 0);
	CHECK64(last_lba == PARTITION_LBA && last_count == 1);
	CHECK64(block64_read(DEVICE_SECTORS - PARTITION_LBA - 1,
		1, sector) == 0);
	CHECK64(last_lba == DEVICE_SECTORS - 1 && last_count == 1);

	calls = read_calls;
	CHECK64(block64_read(DEVICE_SECTORS - PARTITION_LBA,
		1, sector) == -1);
	CHECK64(block64_read(DEVICE_SECTORS - PARTITION_LBA - 1,
		2, sector) == -1);
	CHECK64(block64_read(UINT64_MAX - 1, 2, sector) == -1);
	CHECK64(block64_read(UINT64_MAX - PARTITION_LBA + 1,
		1, sector) == -1);
	CHECK64(block64_read(0, 1, NULL) == -1);
	CHECK64(read_calls == calls);
	return 0;
}

static int test_zero_count_and_write(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	uint32_t reads = read_calls;
	uint32_t writes = write_calls;

	CHECK64(block64_read(UINT64_MAX, 0, NULL) == 0);
	CHECK64(block64_write(UINT64_MAX, 0, NULL) == 0);
	CHECK64(read_calls == reads && write_calls == writes);

	CHECK64(block64_write(4, 1, sector) == 0);
	CHECK64(last_lba == PARTITION_LBA + 4 && last_count == 1);
	writes = write_calls;
	CHECK64(block64_write(DEVICE_SECTORS - PARTITION_LBA,
		1, sector) == -1);
	CHECK64(block64_write(0, 1, NULL) == -1);
	CHECK64(write_calls == writes);
	return 0;
}

static int test_unknown_capacity(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];

	fake_sectors = 0;
	CHECK64(block64_sector_count() == 0);
	CHECK64(block64_read(DEVICE_SECTORS, 1, sector) == 0);
	CHECK64(last_lba == DEVICE_SECTORS + PARTITION_LBA);
	CHECK64(block64_read(UINT64_MAX - PARTITION_LBA + 1,
		1, sector) == -1);
	return 0;
}

int main(void)
{
	int status;

	status = test_partition_and_capacity();
	if (status != 0) {
		return status;
	}
	status = test_zero_count_and_write();
	if (status != 0) {
		return status;
	}
	return test_unknown_capacity();
}
