#include <stddef.h>
#include <stdint.h>

#include "../src64/kernel/fd64_fat.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

#define TEST_MAX_CLUSTER 9U

uint16_t fd64_bytes_per_sector;
uint8_t fd64_sectors_per_cluster;
uint32_t fd64_reserved_sectors;
uint8_t fd64_fat_count;
uint32_t fd64_sectors_per_fat;
uint32_t fd64_root_cluster;
uint32_t fd64_data_lba;
uint32_t fd64_total_sectors;
uint32_t fd64_max_cluster;
uint32_t fd64_alloc_hint;
int fd64_initialized;
int fd64_read_only;

void fd64_mark_read_only(void)
{
	fd64_read_only = 1;
}

static uint8_t fat_sectors[2][FD64_SECTOR_SIZE];
static int fail_reads;
static uint32_t write_calls;
static uint32_t fail_write_call;
static int fail_write_persistent;

static void write32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
	p[2] = (uint8_t) (value >> 16);
	p[3] = (uint8_t) (value >> 24);
}

static uint32_t fat_get(uint32_t cluster)
{
	return fd64_read32(fat_sectors[0] + cluster * sizeof(uint32_t)) &
		FD64_FAT_MASK;
}

static void fat_set(uint32_t cluster, uint32_t value)
{
	write32(fat_sectors[0] + cluster * sizeof(uint32_t), value);
}

static uint32_t fat_copy_get(uint32_t copy, uint32_t cluster)
{
	return fd64_read32(fat_sectors[copy] + cluster * sizeof(uint32_t));
}

static void fat_copy_set(uint32_t copy, uint32_t cluster, uint32_t value)
{
	write32(fat_sectors[copy] + cluster * sizeof(uint32_t), value);
}

uint8_t *cache64_get(uint32_t lba, int mode)
{
	uint32_t copy;

	if (fail_reads != 0 || lba < fd64_reserved_sectors ||
			lba >= fd64_reserved_sectors + fd64_fat_count ||
			(mode != CACHE64_READ && mode != CACHE64_WRITE)) {
		return NULL;
	}
	if (mode == CACHE64_WRITE) {
		write_calls++;
		if (fail_write_call != 0 && write_calls >= fail_write_call &&
				(fail_write_persistent != 0 ||
				write_calls == fail_write_call)) {
			return NULL;
		}
	}
	copy = lba - fd64_reserved_sectors;
	return fat_sectors[copy];
}

static void reset_fat(void)
{
	uint32_t i;

	for (i = 0; i < sizeof(fat_sectors); i++) {
		((uint8_t *) fat_sectors)[i] = 0;
	}
	fd64_bytes_per_sector = FD64_SECTOR_SIZE;
	fd64_sectors_per_cluster = 1;
	fd64_reserved_sectors = 1;
	fd64_fat_count = 1;
	fd64_sectors_per_fat = 1;
	fd64_max_cluster = TEST_MAX_CLUSTER;
	fd64_alloc_hint = TEST_MAX_CLUSTER;
	fd64_read_only = 0;
	fail_reads = 0;
	write_calls = 0;
	fail_write_call = 0;
	fail_write_persistent = 0;
}

static int test_valid_and_short_chains(void)
{
	reset_fat();
	fat_set(2, 3);
	fat_set(3, 4);
	fat_set(4, FD64_FAT_LAST);
	CHECK64(fd64_chain_validate(2, 3) == 0);
	CHECK64(fd64_chain_validate(2, 4) == -1);
	CHECK64(fd64_chain_validate(0, 0) == 0);
	CHECK64(fd64_chain_validate(0, 1) == -1);
	CHECK64(fd64_chain_validate(TEST_MAX_CLUSTER + 1, 1) == -1);
	return 0;
}

static int test_corrupt_entries(void)
{
	reset_fat();
	fat_set(2, 0);
	CHECK64(fd64_chain_validate(2, 1) == -1);
	fat_set(2, 1);
	CHECK64(fd64_chain_validate(2, 1) == -1);
	fat_set(2, FD64_FAT_RESERVED);
	CHECK64(fd64_chain_validate(2, 1) == -1);
	fat_set(2, TEST_MAX_CLUSTER + 1);
	CHECK64(fd64_chain_validate(2, 1) == -1);
	fail_reads = 1;
	CHECK64(fd64_chain_validate(2, 1) == -1);
	return 0;
}

static int test_cycles(void)
{
	reset_fat();
	fat_set(2, 2);
	CHECK64(fd64_chain_validate(2, 1) == -1);

	reset_fat();
	fat_set(2, 3);
	fat_set(3, 4);
	fat_set(4, 3);
	CHECK64(fd64_chain_validate(2, 1) == -1);
	return 0;
}

static int test_free_chain(void)
{
	reset_fat();
	fat_set(2, 3);
	fat_set(3, 2);
	CHECK64(fd64_free_chain(2) == -1);
	CHECK64(fat_get(2) == 3);
	CHECK64(fat_get(3) == 2);

	reset_fat();
	fat_set(2, 3);
	fat_set(3, 4);
	fat_set(4, FD64_FAT_LAST);
	CHECK64(fd64_free_chain(2) == 0);
	CHECK64(fat_get(2) == 0);
	CHECK64(fat_get(3) == 0);
	CHECK64(fat_get(4) == 0);
	CHECK64(fd64_alloc_hint == 2);
	return 0;
}

static int test_fat_copy_rollback(void)
{
	reset_fat();
	fd64_fat_count = 2;
	fat_copy_set(0, 2, 0xa0000000);
	fat_copy_set(1, 2, 0xb0000000);
	fail_write_call = 2;
	CHECK64(fd64_fat_set(2, FD64_FAT_LAST) == -1);
	CHECK64(fat_copy_get(0, 2) == 0xa0000000);
	CHECK64(fat_copy_get(1, 2) == 0xb0000000);
	CHECK64(fd64_read_only == 0);

	reset_fat();
	fd64_fat_count = 2;
	fail_write_call = 2;
	fail_write_persistent = 1;
	CHECK64(fd64_fat_set(2, FD64_FAT_LAST) == -1);
	CHECK64(fd64_read_only == 1);
	return 0;
}

int main(void)
{
	int status;

	status = test_valid_and_short_chains();
	if (status != 0) {
		return status;
	}
	status = test_corrupt_entries();
	if (status != 0) {
		return status;
	}
	status = test_cycles();
	if (status != 0) {
		return status;
	}
	status = test_free_chain();
	if (status != 0) {
		return status;
	}
	return test_fat_copy_rollback();
}
