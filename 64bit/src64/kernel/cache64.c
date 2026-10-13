/*
 * cache64.c -- 섹터 캐시 (되쓰기 방식)
 *
 * 4KiB 블록, 전체 4MiB, 직접 사상. 블록마다 metadata 표시가 있어서
 * fd64_sync가 데이터와 메타데이터를 나눠 내보낼 수 있다.
 *
 * 주의: 직접 사상이다. 충돌이 잦다고 측정되면 그때 2-way로 바꾼다.
 * FAT 블록은 낮은 LBA, 파일 데이터는 높은 LBA에 있어 겹칠 일이 적다.
 */
#include <block64.h>
#include <cache64.h>
#include <memory64.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCK_BYTES (CACHE64_BLOCK_SECTORS * BLOCK64_SECTOR_SIZE)

struct CACHE64_BLOCK {
	uint32_t tag;
	uint8_t valid;
	uint8_t dirty;
	uint8_t meta;
	uint8_t sectors;
};

static struct CACHE64_BLOCK blocks[CACHE64_BLOCKS];
static uint8_t *cache_data;

static uint8_t *block_data(uint32_t index)
{
	return cache_data + (size_t) index * BLOCK_BYTES;
}

static int mode_valid(int mode)
{
	return mode == CACHE64_READ || mode == CACHE64_WRITE ||
		mode == CACHE64_WRITE_META;
}

static uint32_t block_sectors(uint32_t tag)
{
	uint64_t first_lba;
	uint64_t remaining;
	uint64_t total;

	first_lba = (uint64_t) tag * CACHE64_BLOCK_SECTORS;
	total = block64_sector_count();
	if (total == 0) {
		return CACHE64_BLOCK_SECTORS;
	}
	if (first_lba >= total) {
		return 0;
	}
	remaining = total - first_lba;
	return remaining < CACHE64_BLOCK_SECTORS ? (uint32_t) remaining :
		CACHE64_BLOCK_SECTORS;
}

static void clear_bytes(uint8_t *buffer, size_t bytes)
{
	size_t i;

	for (i = 0; i < bytes; i++) {
		buffer[i] = 0;
	}
}

/* 내보낸 섹터 수를 돌려준다. 할 일이 없으면 0, 입출력 오류면 -1. */
static int write_back(uint32_t index)
{
	if (blocks[index].valid == 0 || blocks[index].dirty == 0) {
		return 0;
	}
	if (blocks[index].sectors == 0 ||
			blocks[index].sectors > CACHE64_BLOCK_SECTORS) {
		return -1;
	}
	if (block64_write((uint64_t) blocks[index].tag * CACHE64_BLOCK_SECTORS,
			blocks[index].sectors, block_data(index)) != 0) {
		return -1;
	}
	blocks[index].dirty = 0;
	return blocks[index].sectors;
}

int cache64_init(void)
{
	uint32_t i;

	if (cache_data != NULL) {
		return 0;
	}
	cache_data = (uint8_t *) memman64_alloc_4k(&memman64,
		(size_t) CACHE64_BLOCKS * BLOCK_BYTES);
	if (cache_data == NULL) {
		return -1;
	}
	for (i = 0; i < CACHE64_BLOCKS; i++) {
		blocks[i].valid = 0;
		blocks[i].dirty = 0;
		blocks[i].meta = 0;
		blocks[i].sectors = 0;
	}
	return 0;
}

uint8_t *cache64_get(uint32_t lba, int mode)
{
	uint32_t tag;
	uint32_t index;
	uint32_t sectors;
	size_t valid_bytes;

	if (cache_data == NULL || mode_valid(mode) == 0) {
		return NULL;
	}
	tag = lba / CACHE64_BLOCK_SECTORS;
	index = tag % CACHE64_BLOCKS;
	if (blocks[index].valid == 0 || blocks[index].tag != tag) {
		sectors = block_sectors(tag);
		if (sectors == 0 || lba % CACHE64_BLOCK_SECTORS >= sectors) {
			return NULL;
		}
		if (write_back(index) < 0) {
			return NULL;
		}
		blocks[index].valid = 0;
		blocks[index].dirty = 0;
		blocks[index].meta = 0;
		blocks[index].sectors = 0;
		if (block64_read((uint64_t) tag * CACHE64_BLOCK_SECTORS,
				sectors, block_data(index)) != 0) {
			return NULL;
		}
		valid_bytes = (size_t) sectors * BLOCK64_SECTOR_SIZE;
		clear_bytes(block_data(index) + valid_bytes,
			BLOCK_BYTES - valid_bytes);
		blocks[index].tag = tag;
		blocks[index].valid = 1;
		blocks[index].dirty = 0;
		blocks[index].meta = 0;
		blocks[index].sectors = (uint8_t) sectors;
	} else if (lba % CACHE64_BLOCK_SECTORS >= blocks[index].sectors) {
		return NULL;
	}
	if (mode != CACHE64_READ) {
		blocks[index].dirty = 1;
		/* 한 번 metadata로 표시된 블록은 파일 데이터를 같이 담고 있어도
		   메타데이터 단계에서 내보낸다. */
		if (mode == CACHE64_WRITE_META) {
			blocks[index].meta = 1;
		}
	}
	return block_data(index) + (lba % CACHE64_BLOCK_SECTORS) * BLOCK64_SECTOR_SIZE;
}

int cache64_flush(uint32_t start, uint32_t end, int meta)
{
	uint32_t i;
	uint32_t first_lba;
	int written;
	int n;

	if (cache_data == NULL || start > end || (meta != 0 && meta != 1)) {
		return -1;
	}
	written = 0;
	for (i = 0; i < CACHE64_BLOCKS; i++) {
		if (blocks[i].valid == 0 || blocks[i].dirty == 0) {
			continue;
		}
		if ((blocks[i].meta != 0) != (meta != 0)) {
			continue;
		}
		first_lba = blocks[i].tag * CACHE64_BLOCK_SECTORS;
		if (first_lba < start || first_lba >= end) {
			continue;
		}
		n = write_back(i);
		if (n < 0) {
			return -1;
		}
		written += n;
	}
	return written;
}

void cache64_discard_dirty(void)
{
	uint32_t i;

	for (i = 0; i < CACHE64_BLOCKS; i++) {
		if (blocks[i].dirty != 0) {
			blocks[i].valid = 0;
			blocks[i].dirty = 0;
			blocks[i].meta = 0;
			blocks[i].sectors = 0;
		}
	}
}
