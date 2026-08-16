/* Writeback sector cache. Direct-mapped, 4 KiB blocks, 4 MiB total.
   ponytail: direct-mapped; go 2-way set-associative only if profiling shows
   thrash. FAT blocks sit at low LBAs and file data at high ones, so aliasing
   pressure is low. */
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
};

static struct CACHE64_BLOCK blocks[CACHE64_BLOCKS];
static uint8_t *cache_data;

static uint8_t *block_data(uint32_t index)
{
	return cache_data + (size_t) index * BLOCK_BYTES;
}

/* returns sectors written, 0 if nothing to do, -1 on I/O error */
static int write_back(uint32_t index)
{
	if (blocks[index].valid == 0 || blocks[index].dirty == 0) {
		return 0;
	}
	if (block64_write((uint64_t) blocks[index].tag * CACHE64_BLOCK_SECTORS,
			CACHE64_BLOCK_SECTORS, block_data(index)) != 0) {
		return -1;
	}
	blocks[index].dirty = 0;
	return CACHE64_BLOCK_SECTORS;
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
	}
	return 0;
}

uint8_t *cache64_get(uint32_t lba, int mode)
{
	uint32_t tag;
	uint32_t index;

	if (cache_data == NULL) {
		return NULL;
	}
	tag = lba / CACHE64_BLOCK_SECTORS;
	index = tag % CACHE64_BLOCKS;
	if (blocks[index].valid == 0 || blocks[index].tag != tag) {
		if (write_back(index) < 0) {
			return NULL;
		}
		blocks[index].valid = 0;
		if (block64_read((uint64_t) tag * CACHE64_BLOCK_SECTORS,
				CACHE64_BLOCK_SECTORS, block_data(index)) != 0) {
			return NULL;
		}
		blocks[index].tag = tag;
		blocks[index].valid = 1;
		blocks[index].dirty = 0;
		blocks[index].meta = 0;
	}
	if (mode != CACHE64_READ) {
		blocks[index].dirty = 1;
		/* sticky: a block holding any metadata flushes in the metadata
		   phase, even if it also holds file data */
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
		blocks[i].dirty = 0;
		blocks[i].meta = 0;
	}
}
