#include <stddef.h>
#include <stdint.h>

#include "../src64/kernel/cache64.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static uint8_t fake_cache[CACHE64_BLOCKS * BLOCK_BYTES];
static uint64_t device_sectors;
static uint64_t last_read_lba;
static uint64_t last_write_lba;
static uint64_t fail_read_lba;
static uint64_t fail_write_lba;
static uint32_t last_read_count;
static uint32_t last_write_count;
static uint32_t read_calls;
static uint32_t write_calls;
static size_t allocation_size;
static int allocation_fails;

struct MEMMAN64 memman64;

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	allocation_size = size;
	return allocation_fails != 0 ? 0 : (uintptr_t) fake_cache;
}

uint64_t block64_sector_count(void)
{
	return device_sectors;
}

int block64_read(uint64_t lba, uint32_t count, void *dst)
{
	uint8_t *out = (uint8_t *) dst;
	uint32_t byte;
	uint32_t sector;

	read_calls++;
	last_read_lba = lba;
	last_read_count = count;
	if (lba == fail_read_lba || dst == NULL ||
			(device_sectors != 0 &&
			(lba >= device_sectors || count > device_sectors - lba))) {
		return -1;
	}
	for (sector = 0; sector < count; sector++) {
		for (byte = 0; byte < BLOCK64_SECTOR_SIZE; byte++) {
			out[(size_t) sector * BLOCK64_SECTOR_SIZE + byte] =
				(uint8_t) (lba + sector);
		}
	}
	return 0;
}

int block64_write(uint64_t lba, uint32_t count, const void *src)
{
	write_calls++;
	last_write_lba = lba;
	last_write_count = count;
	if (lba == fail_write_lba || src == NULL ||
			(device_sectors != 0 &&
			(lba >= device_sectors || count > device_sectors - lba))) {
		return -1;
	}
	return 0;
}

static void reset_cache(uint64_t sectors)
{
	uint32_t i;

	for (i = 0; i < CACHE64_BLOCKS; i++) {
		blocks[i].tag = 0;
		blocks[i].valid = 0;
		blocks[i].dirty = 0;
		blocks[i].meta = 0;
		blocks[i].sectors = 0;
	}
	for (i = 0; i < sizeof(fake_cache); i++) {
		fake_cache[i] = 0xcc;
	}
	cache_data = fake_cache;
	device_sectors = sectors;
	last_read_lba = UINT64_MAX;
	last_write_lba = UINT64_MAX;
	fail_read_lba = UINT64_MAX;
	fail_write_lba = UINT64_MAX;
	last_read_count = 0;
	last_write_count = 0;
	read_calls = 0;
	write_calls = 0;
}

static int test_init_and_modes(void)
{
	uint8_t *p;

	cache_data = NULL;
	allocation_fails = 1;
	CHECK64(cache64_init() == -1);
	allocation_fails = 0;
	CHECK64(cache64_init() == 0);
	CHECK64(allocation_size == sizeof(fake_cache));

	reset_cache(18);
	CHECK64(cache64_get(0, -1) == NULL);
	CHECK64(cache64_get(0, CACHE64_WRITE_META + 1) == NULL);
	CHECK64(read_calls == 0 && write_calls == 0);
	p = cache64_get(0, CACHE64_READ);
	CHECK64(p != NULL && blocks[0].dirty == 0);
	p = cache64_get(0, CACHE64_WRITE);
	CHECK64(p != NULL && blocks[0].dirty != 0 && blocks[0].meta == 0);
	p = cache64_get(0, CACHE64_WRITE_META);
	CHECK64(p != NULL && blocks[0].dirty != 0 && blocks[0].meta != 0);
	return 0;
}

static int test_partial_last_block(void)
{
	uint8_t *block;
	uint8_t *p;
	uint32_t reads;

	reset_cache(18);
	p = cache64_get(17, CACHE64_READ);
	CHECK64(p != NULL && *p == 17);
	CHECK64(last_read_lba == 16 && last_read_count == 2);
	CHECK64(blocks[2].sectors == 2);
	block = block_data(2);
	CHECK64(block[2 * BLOCK64_SECTOR_SIZE] == 0);
	CHECK64(block[BLOCK_BYTES - 1] == 0);
	reads = read_calls;
	CHECK64(cache64_get(18, CACHE64_READ) == NULL);
	CHECK64(read_calls == reads);

	p = cache64_get(17, CACHE64_WRITE);
	CHECK64(p != NULL);
	*p = 0x5a;
	CHECK64(cache64_flush(16, 18, 0) == 2);
	CHECK64(last_write_lba == 16 && last_write_count == 2);
	CHECK64(blocks[2].dirty == 0);
	return 0;
}

static int test_eviction_transitions(void)
{
	const uint32_t colliding_lba = CACHE64_BLOCKS * CACHE64_BLOCK_SECTORS;
	uint8_t *p;
	uint32_t reads;

	reset_cache((uint64_t) colliding_lba + CACHE64_BLOCK_SECTORS);
	p = cache64_get(0, CACHE64_WRITE);
	CHECK64(p != NULL);
	*p = 0x5a;
	fail_write_lba = 0;
	reads = read_calls;
	CHECK64(cache64_get(colliding_lba, CACHE64_READ) == NULL);
	CHECK64(read_calls == reads && write_calls == 1);
	CHECK64(blocks[0].valid != 0 && blocks[0].tag == 0);
	CHECK64(blocks[0].dirty != 0 && *block_data(0) == 0x5a);

	fail_write_lba = UINT64_MAX;
	CHECK64(cache64_get(colliding_lba, CACHE64_READ) != NULL);
	CHECK64(blocks[0].valid != 0);
	CHECK64(blocks[0].tag == CACHE64_BLOCKS && blocks[0].dirty == 0);
	CHECK64(last_read_lba == colliding_lba);

	reset_cache((uint64_t) colliding_lba + CACHE64_BLOCK_SECTORS);
	CHECK64(cache64_get(0, CACHE64_READ) != NULL);
	fail_read_lba = colliding_lba;
	CHECK64(cache64_get(colliding_lba, CACHE64_READ) == NULL);
	CHECK64(blocks[0].valid == 0 && blocks[0].dirty == 0);
	return 0;
}

static int test_flush_failure_and_retry(void)
{
	uint32_t writes;

	reset_cache(32);
	CHECK64(cache64_get(0, CACHE64_WRITE) != NULL);
	CHECK64(cache64_get(8, CACHE64_WRITE_META) != NULL);
	fail_write_lba = 0;
	CHECK64(cache64_flush(0, 16, 0) == -1);
	CHECK64(blocks[0].dirty != 0 && blocks[1].dirty != 0);
	writes = write_calls;
	CHECK64(cache64_flush(0, 16, 2) == -1);
	CHECK64(cache64_flush(16, 8, 0) == -1);
	CHECK64(write_calls == writes);

	fail_write_lba = UINT64_MAX;
	CHECK64(cache64_flush(0, 16, 0) == CACHE64_BLOCK_SECTORS);
	CHECK64(blocks[0].dirty == 0 && blocks[1].dirty != 0);
	CHECK64(cache64_flush(0, 16, 1) == CACHE64_BLOCK_SECTORS);
	CHECK64(blocks[1].dirty == 0);
	return 0;
}

static int test_discard_invalidates_dirty(void)
{
	uint8_t *p;
	uint32_t reads;

	reset_cache(32);
	p = cache64_get(0, CACHE64_WRITE_META);
	CHECK64(p != NULL);
	*p = 0x5a;
	CHECK64(cache64_get(8, CACHE64_READ) != NULL);
	reads = read_calls;
	cache64_discard_dirty();
	CHECK64(blocks[0].valid == 0 && blocks[0].dirty == 0);
	CHECK64(blocks[0].meta == 0 && blocks[0].sectors == 0);
	CHECK64(blocks[1].valid != 0 && blocks[1].dirty == 0);
	CHECK64(write_calls == 0);

	p = cache64_get(0, CACHE64_READ);
	CHECK64(p != NULL && *p == 0);
	CHECK64(read_calls == reads + 1);
	return 0;
}

int main(void)
{
	int status;

	status = test_init_and_modes();
	if (status != 0) {
		return status;
	}
	status = test_partial_last_block();
	if (status != 0) {
		return status;
	}
	status = test_eviction_transitions();
	if (status != 0) {
		return status;
	}
	status = test_flush_failure_and_retry();
	if (status != 0) {
		return status;
	}
	return test_discard_invalidates_dirty();
}
