#include <stddef.h>
#include <stdint.h>

#include "../src64/kernel/fd64.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

#define TEST_TOTAL_SECTORS 131072U
#define TEST_RESERVED_SECTORS 1024U
#define TEST_FAT_COUNT 2U
#define TEST_SECTORS_PER_FAT 1001U

static uint8_t boot_sector[FD64_SECTOR_SIZE];
static uint64_t device_sectors;
static int root_chain_valid;
static int flush_failures;
static uint32_t mutex_depth;
static uint32_t mutex_locks;
static uint32_t mutex_unlocks;

int mutex64_lock(struct MUTEX64 *mutex)
{
	if (mutex == NULL || mutex_depth != 0) {
		return -1;
	}
	mutex_depth = 1;
	mutex_locks++;
	return 0;
}

int mutex64_unlock(struct MUTEX64 *mutex)
{
	if (mutex == NULL || mutex_depth != 1) {
		return -1;
	}
	mutex_depth = 0;
	mutex_unlocks++;
	return 0;
}

static void write16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
}

static void write32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
	p[2] = (uint8_t) (value >> 16);
	p[3] = (uint8_t) (value >> 24);
}

uint16_t fd64_read16(const uint8_t *p)
{
	return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

uint32_t fd64_read32(const uint8_t *p)
{
	return (uint32_t) fd64_read16(p) |
		((uint32_t) fd64_read16(p + 2) << 16);
}

int cache64_init(void)
{
	return 0;
}

uint8_t *cache64_get(uint32_t lba, int mode)
{
	if (lba != 0 || mode != CACHE64_READ) {
		return NULL;
	}
	return boot_sector;
}

int cache64_flush(uint32_t start, uint32_t end, int meta)
{
	(void) start;
	(void) end;
	(void) meta;
	if (flush_failures < 0) {
		return -1;
	}
	if (flush_failures > 0) {
		flush_failures--;
		return -1;
	}
	return 0;
}

uint64_t block64_sector_count(void)
{
	return device_sectors;
}

int fd64_chain_validate(uint32_t first, uint32_t required_clusters)
{
	return root_chain_valid != 0 && first == 2 && required_clusters == 1 ?
		0 : -1;
}

static void make_valid_bpb(void)
{
	uint32_t i;

	for (i = 0; i < sizeof(boot_sector); i++) {
		boot_sector[i] = 0;
	}
	write16(boot_sector + 11, FD64_SECTOR_SIZE);
	boot_sector[13] = 1;
	write16(boot_sector + 14, TEST_RESERVED_SECTORS);
	boot_sector[16] = TEST_FAT_COUNT;
	write32(boot_sector + 32, TEST_TOTAL_SECTORS);
	write32(boot_sector + 36, TEST_SECTORS_PER_FAT);
	write32(boot_sector + 44, 2);
	boot_sector[510] = 0x55;
	boot_sector[511] = 0xaa;
	device_sectors = TEST_TOTAL_SECTORS;
	root_chain_valid = 1;
	flush_failures = 0;
	fd64_initialized = 0;
}

static int mount_rejected(void)
{
	CHECK64(fd64_init() == -1);
	CHECK64(fd64_initialized == 0);
	return 0;
}

static int test_valid_geometry(void)
{
	uint32_t expected_data_lba;
	uint32_t expected_clusters;

	make_valid_bpb();
	CHECK64(fd64_init() == 0);
	expected_data_lba = TEST_RESERVED_SECTORS +
		TEST_FAT_COUNT * TEST_SECTORS_PER_FAT;
	expected_clusters = (TEST_TOTAL_SECTORS - expected_data_lba) + 1;
	CHECK64(fd64_data_lba == expected_data_lba);
	CHECK64(fd64_max_cluster == expected_clusters);
	CHECK64(fd64_root_cluster == 2);
	return 0;
}

static int test_bpb_fields(void)
{
	make_valid_bpb();
	boot_sector[511] = 0;
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	boot_sector[13] = 3;
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	boot_sector[13] = 255;
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	write16(boot_sector + 14, 0);
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	write16(boot_sector + 19, 1);
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	write16(boot_sector + 42, 1);
	CHECK64(mount_rejected() == 0);
	return 0;
}

static int test_geometry_ranges(void)
{
	uint32_t data_lba;
	uint32_t max_cluster;

	make_valid_bpb();
	boot_sector[16] = UINT8_MAX;
	write32(boot_sector + 36, UINT32_MAX);
	write32(boot_sector + 32, UINT32_MAX);
	device_sectors = 0;
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	write32(boot_sector + 36, 1);
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	write32(boot_sector + 32, 65525);
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	data_lba = TEST_RESERVED_SECTORS +
		TEST_FAT_COUNT * TEST_SECTORS_PER_FAT;
	max_cluster = TEST_TOTAL_SECTORS - data_lba + 1;
	write32(boot_sector + 44, max_cluster + 1);
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	device_sectors = TEST_TOTAL_SECTORS - 1;
	CHECK64(mount_rejected() == 0);

	make_valid_bpb();
	root_chain_valid = 0;
	CHECK64(mount_rejected() == 0);
	CHECK64(fd64_max_cluster == 0);
	CHECK64(fd64_root_cluster == 0);
	return 0;
}

static int test_sync_read_only(void)
{
	make_valid_bpb();
	CHECK64(fd64_init() == 0);
	flush_failures = 1;
	CHECK64(fd64_sync() == 0);
	CHECK64(fd64_read_only == 0);
	flush_failures = -1;
	CHECK64(fd64_sync() == -1);
	CHECK64(fd64_read_only == 1);
	return 0;
}

int main(void)
{
	int status;

	status = test_valid_geometry();
	if (status != 0) {
		return status;
	}
	status = test_bpb_fields();
	if (status != 0) {
		return status;
	}
	status = test_geometry_ranges();
	if (status != 0) {
		return status;
	}
	status = test_sync_read_only();
	if (status != 0) {
		return status;
	}
	CHECK64(mutex_depth == 0 && mutex_locks == mutex_unlocks);
	return mutex_locks != 0 ? 0 : __LINE__;
}
