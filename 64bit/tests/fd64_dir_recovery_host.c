#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src64/kernel/fd64.c"
#include "../src64/kernel/fd64_fat.c"
#include "../src64/kernel/fd64_dir.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "directory check failed at line %d\n", __LINE__); \
		return __LINE__; \
	} \
} while (0)

#define TEST_SECTORS 32U
#define TEST_MAX_CLUSTER 12U

static uint8_t sectors[TEST_SECTORS][FD64_SECTOR_SIZE];
static uint32_t fail_meta_lba;
static uint32_t fail_meta_after;
static uint32_t meta_matches;
static uint32_t fail_data_lba;
static uint32_t flush_calls;
static int truncate_order_seen;
static uint32_t mutex_depth;

int mutex64_lock(struct MUTEX64 *mutex)
{
	if (mutex == NULL || mutex_depth != 0) {
		return -1;
	}
	mutex_depth = 1;
	return 0;
}

int mutex64_unlock(struct MUTEX64 *mutex)
{
	if (mutex == NULL || mutex_depth != 1) {
		return -1;
	}
	mutex_depth = 0;
	return 0;
}

static uint32_t fat_get(uint32_t cluster);

int cache64_init(void)
{
	return 0;
}

uint64_t block64_sector_count(void)
{
	return TEST_SECTORS;
}

uint8_t *cache64_get(uint32_t lba, int mode)
{
	if (lba >= TEST_SECTORS ||
			(mode != CACHE64_READ && mode != CACHE64_WRITE &&
			mode != CACHE64_WRITE_META)) {
		return NULL;
	}
	if (mode == CACHE64_WRITE_META && lba == fail_meta_lba) {
		if (meta_matches++ == fail_meta_after) {
			fail_meta_lba = UINT32_MAX;
			return NULL;
		}
	}
	if (mode == CACHE64_WRITE && lba == fail_data_lba) {
		fail_data_lba = UINT32_MAX;
		return NULL;
	}
	return sectors[lba];
}

int cache64_flush(uint32_t start, uint32_t end, int meta)
{
	const struct FDINFO64 *entry;

	(void) start;
	(void) end;
	(void) meta;
	flush_calls++;
	if (flush_calls == 1) {
		entry = (const struct FDINFO64 *) sectors[fd64_data_lba];
		if (entry->size == 0 && fd64_info_cluster(entry) == 0 &&
				fat_get(3) == 4) {
			truncate_order_seen = 1;
		}
	}
	return 0;
}

static void write32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
	p[2] = (uint8_t) (value >> 16);
	p[3] = (uint8_t) (value >> 24);
}

static uint32_t fat_get(uint32_t cluster)
{
	return fd64_read32(sectors[fd64_reserved_sectors] +
		cluster * sizeof(uint32_t)) & FD64_FAT_MASK;
}

static void fat_set(uint32_t cluster, uint32_t value)
{
	write32(sectors[fd64_reserved_sectors] +
		cluster * sizeof(uint32_t), value);
}

static void reset_volume(void)
{
	struct FDINFO64 *entry;
	uint32_t i;
	uint32_t j;

	for (i = 0; i < sizeof(sectors); i++) {
		((uint8_t *) sectors)[i] = 0;
	}
	fd64_bytes_per_sector = FD64_SECTOR_SIZE;
	fd64_sectors_per_cluster = 1;
	fd64_reserved_sectors = 1;
	fd64_fat_count = 1;
	fd64_sectors_per_fat = 1;
	fd64_root_cluster = 2;
	fd64_data_lba = 4;
	fd64_total_sectors = TEST_SECTORS;
	fd64_max_cluster = TEST_MAX_CLUSTER;
	fd64_alloc_hint = 3;
	fd64_initialized = 1;
	fd64_read_only = 0;
	fail_meta_lba = UINT32_MAX;
	fail_meta_after = 0;
	meta_matches = 0;
	fail_data_lba = UINT32_MAX;
	flush_calls = 0;
	truncate_order_seen = 0;
	fat_set(2, FD64_FAT_LAST);
	for (i = 0; i < FD64_SECTOR_SIZE / sizeof(struct FDINFO64); i++) {
		entry = (struct FDINFO64 *) sectors[fd64_data_lba] + i;
		for (j = 0; j < sizeof(*entry); j++) {
			((uint8_t *) entry)[j] = 0;
		}
		entry->name[0] = 'A';
		entry->type = 0x20;
	}
}

static void reset_empty_root(void)
{
	uint32_t i;

	reset_volume();
	for (i = 0; i < FD64_SECTOR_SIZE; i++) {
		sectors[fd64_data_lba][i] = 0;
	}
}

static int test_name_policy(void)
{
	static const char *const invalid[] = {
		".", "..", "trailing.", "trailing ", "bad\"name", "bad*name",
		"bad/name", "bad:name", "bad<name", "bad>name", "bad?name",
		"bad\\name", "bad|name", "bad\001name"
	};
	char maximum[FD64_LFN_MAX_UNITS + 1];
	char too_many_units[FD64_LFN_MAX_UNITS + 2];
	char too_many_bytes[FD64_NAME_MAX + 1];
	struct FDHANDLE64 fh;
	uint32_t i;

	for (i = 0; i < FD64_LFN_MAX_UNITS; i++) {
		maximum[i] = 'a';
		too_many_units[i] = 'b';
	}
	maximum[FD64_LFN_MAX_UNITS] = '\0';
	too_many_units[FD64_LFN_MAX_UNITS] = 'b';
	too_many_units[FD64_LFN_MAX_UNITS + 1] = '\0';
	for (i = 0; i < FD64_NAME_MAX; i++) {
		too_many_bytes[i] = 'c';
	}
	too_many_bytes[FD64_NAME_MAX] = '\0';

	reset_empty_root();
	CHECK64(fd64_dir_create(&fh, maximum) == 1);
	reset_empty_root();
	CHECK64(fd64_dir_create(&fh, too_many_units) == 0);
	CHECK64(fd64_dir_create(&fh, too_many_bytes) == 0);
	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		reset_empty_root();
		CHECK64(fd64_dir_create(&fh, invalid[i]) == 0);
		CHECK64(sectors[fd64_data_lba][0] == 0);
	}

	reset_empty_root();
	CHECK64(fd64_dir_create(&fh, "Report.txt") == 1);
	CHECK64(fd64_dir_create(&fh, "report.TXT") == 0);
	CHECK64(fd64_dir_create(&fh, "Report.txt") == 0);
	CHECK64(fd64_file_count() == 1);
	return 0;
}

static int test_unicode_names64(void)
{
	const char *split = "aaaaaaaaaaaa\xf0\x9f\x98\x80.txt";
	const char *other = "aaaaaaaaaaaa\xf0\x9f\x98\x81.txt";
	const char *const invalid[] = {
		"\xc2", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80",
	};
	struct FDHANDLE64 fh;
	struct FDHANDLE64 second;
	struct FDINFO64 info;
	char name[FD64_NAME_MAX];
	char maximum[26 * 4 + 1];
	char too_long[27 * 4 + 1];
	char alias[13];
	uint8_t snapshot[sizeof(sectors)];
	uint8_t *raw;
	uint32_t offset;

	reset_empty_root();
	CHECK64(fd64_create(&fh, split) == 1);
	CHECK64(fd64_file_at(0, &info, name, sizeof(name)) == 1);
	CHECK64(strcmp(name, split) == 0);
	CHECK64(fd64_open(&second, split) == 1);
	CHECK64(second.dir.cluster == fh.dir.cluster && second.dir.offset == fh.dir.offset);
	/* Verify a pair split across LFN entries without joining scalar halves. */
	raw = sectors[fd64_data_lba];
	CHECK64(raw[0] == (LFN_LAST | 2));
	CHECK64(fd64_read16(raw + lfn_unit_offset(0)) == 0xde00);
	CHECK64(raw[32] == 1);
	CHECK64(fd64_read16(raw + 32 + lfn_unit_offset(12)) == 0xd83d);
	CHECK64(fd64_create(&second, other) == 1);
	CHECK64(fd64_file_count() == 2);
	CHECK64(fd64_file_at(1, &info, name, sizeof(name)) == 1);
	CHECK64(strcmp(name, other) == 0);
	CHECK64(fd64_open(&second, other) == 1);
	CHECK64(second.dir.offset != fh.dir.offset);
	CHECK64(fd64_write(&fh, "x", 1) == 1);
	memcpy(snapshot, sectors, sizeof(sectors));
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK64(fd64_open(&second, invalid[i]) == 0);
		CHECK64(fd64_create(&second, invalid[i]) == 0);
		CHECK64(memcmp(snapshot, sectors, sizeof(sectors)) == 0);
	}

	/* Corrupt the low half into a high half: list uses the intact 8.3 alias,
	 * and the malformed long name can no longer open the file.
	 */
	short_name_text(&fh.info, alias, sizeof(alias));
	offset = lfn_unit_offset(0);
	raw[offset] = 0x00;
	raw[offset + 1] = 0xd8;
	CHECK64(fd64_file_at(0, &info, name, sizeof(name)) == 1);
	CHECK64(strcmp(name, alias) == 0);
	CHECK64(fd64_open(&second, split) == 0);
	CHECK64(fd64_open(&second, alias) == 1 && second.info.size == 1);
	/* A NUL after the high half also truncates the pair and forces fallback. */
	raw[offset + 1] = 0;
	CHECK64(fd64_file_at(0, &info, name, sizeof(name)) == 1);
	CHECK64(strcmp(name, alias) == 0);

	for (size_t i = 0; i < 27; i++) {
		memcpy(too_long + i * 4, "\xf0\x9f\x98\x80", 4);
	}
	too_long[sizeof(too_long) - 1] = '\0';
	memcpy(maximum, too_long, sizeof(maximum) - 1);
	maximum[sizeof(maximum) - 1] = '\0';
	reset_empty_root();
	CHECK64(fd64_create(&fh, maximum) == 1);
	CHECK64(fd64_file_at(0, &info, name, sizeof(name)) == 1);
	CHECK64(strcmp(name, maximum) == 0);
	CHECK64(fd64_open(&second, maximum) == 1);
	memcpy(snapshot, sectors, sizeof(sectors));
	CHECK64(fd64_open(&second, too_long) == 0);
	CHECK64(fd64_create(&second, too_long) == 0);
	CHECK64(memcmp(snapshot, sectors, sizeof(sectors)) == 0);
	/* Reject a malformed suffix before its first eight bytes can match an
	 * existing short alias and trigger create's truncate behavior.
	 */
	reset_empty_root();
	CHECK64(fd64_create(&fh, "abcdefgh") == 1);
	CHECK64(fd64_write(&fh, "keep", 4) == 4);
	memcpy(snapshot, sectors, sizeof(sectors));
	CHECK64(fd64_create(&second, "abcdefgh\xed\xa0\x80") == 0);
	CHECK64(memcmp(snapshot, sectors, sizeof(sectors)) == 0);
	return 0;
}

static int test_file_size_and_seek_bounds(void)
{
	struct FDHANDLE64 before;
	struct FDHANDLE64 fh;
	const char byte = 'x';
	uint32_t i;

	reset_empty_root();
	for (i = 0; i < sizeof(fh.info); i++) {
		((uint8_t *) &fh.info)[i] = 0;
	}
	fh.dir.cluster = 2;
	fh.dir.offset = 0;
	fh.pos = UINT32_MAX;
	fh.cluster = 0;
	before = fh;
	CHECK64(fd64_write(&fh, &byte, 1) == 0);
	CHECK64(fh.pos == before.pos && fh.info.size == before.info.size);
	if (SIZE_MAX > UINT32_MAX) {
		fh.pos = 0;
		CHECK64(fd64_write(&fh, &byte, (size_t) UINT32_MAX + 1) == 0);
	}
	fh.pos = 0;
	CHECK64(fd64_seek(&fh, INT64_MIN, 1) == -1);
	CHECK64(fh.pos == 0);
	return 0;
}

static int test_zero_failure_rollback(void)
{
	struct FDHANDLE64 fh;

	reset_volume();
	fail_meta_lba = fd64_data_lba + 1;
	fail_meta_after = 0;
	CHECK64(fd64_dir_create(&fh, "new.txt") == 0);
	CHECK64(fat_get(2) == FD64_FAT_LAST);
	CHECK64(fat_get(3) == 0);
	CHECK64(fd64_alloc_hint == 3);
	CHECK64(fd64_read_only == 0);
	return 0;
}

static int test_entry_failure_rollback(void)
{
	struct FDHANDLE64 fh;

	reset_volume();
	fail_meta_lba = fd64_data_lba + 1;
	fail_meta_after = 1;
	CHECK64(fd64_dir_create(&fh, "new.txt") == 0);
	CHECK64(fat_get(2) == FD64_FAT_LAST);
	CHECK64(fat_get(3) == 0);
	CHECK64(sectors[fd64_data_lba + 1][0] == 0);
	CHECK64(fd64_read_only == 0);
	return 0;
}

static int test_success_and_read_only(void)
{
	struct FDHANDLE64 fh;

	reset_volume();
	CHECK64(fd64_dir_create(&fh, "new.txt") == 1);
	CHECK64(fat_get(2) == 3);
	CHECK64(fat_get(3) == FD64_FAT_LAST);
	CHECK64(fh.dir.cluster == 3);
	CHECK64(fh.info.name[0] == 'N');
	fd64_read_only = 1;
	CHECK64(fd64_dir_create(&fh, "blocked.txt") == 0);
	return 0;
}

static int test_write_allocation_rollback(void)
{
	struct FDHANDLE64 before;
	struct FDHANDLE64 fh;
	const char byte = 'x';

	reset_volume();
	fh.info = *(struct FDINFO64 *) sectors[fd64_data_lba];
	fd64_info_set_cluster(&fh.info, 0);
	fh.info.size = 0;
	fh.dir.cluster = 2;
	fh.dir.offset = 0;
	fh.pos = 0;
	fh.cluster = 0;
	before = fh;
	fail_data_lba = fd64_data_lba + 1;
	CHECK64(fd64_write(&fh, &byte, 1) == 0);
	CHECK64(fat_get(3) == 0);
	CHECK64(fd64_info_cluster(&fh.info) ==
		fd64_info_cluster(&before.info));
	CHECK64(fh.pos == before.pos);
	CHECK64(fd64_read_only == 0);
	return 0;
}

static int test_truncate_detaches_before_free(void)
{
	struct FDINFO64 *entry;
	struct FDHANDLE64 fh;

	reset_volume();
	entry = (struct FDINFO64 *) sectors[fd64_data_lba];
	fd64_info_set_cluster(entry, 3);
	entry->size = 2 * FD64_SECTOR_SIZE;
	fat_set(3, 4);
	fat_set(4, FD64_FAT_LAST);
	fh.info = *entry;
	fh.dir.cluster = 2;
	fh.dir.offset = 0;
	fh.pos = 0;
	fh.cluster = 3;
	CHECK64(fd64_truncate(&fh, 0) == 0);
	CHECK64(truncate_order_seen == 1);
	CHECK64(flush_calls == 6);
	CHECK64(fd64_info_cluster(entry) == 0);
	CHECK64(entry->size == 0);
	CHECK64(fat_get(3) == 0);
	CHECK64(fat_get(4) == 0);
	return 0;
}

int main(void)
{
	int status;

	status = test_name_policy();
	if (status != 0) {
		return status;
	}
	status = test_unicode_names64();
	if (status != 0) {
		return status;
	}
	status = test_file_size_and_seek_bounds();
	if (status != 0) {
		return status;
	}

	status = test_zero_failure_rollback();
	if (status != 0) {
		return status;
	}
	status = test_entry_failure_rollback();
	if (status != 0) {
		return status;
	}
	status = test_success_and_read_only();
	if (status != 0) {
		return status;
	}
	status = test_write_allocation_rollback();
	if (status != 0) {
		return status;
	}
	status = test_truncate_detaches_before_free();
	if (status != 0) {
		return status;
	}
	return mutex_depth == 0 ? 0 : __LINE__;
}
