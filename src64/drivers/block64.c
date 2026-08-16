/* Transport selection and partition offset. Everything above this file works
   in filesystem-relative LBAs and never learns which controller answered. */
#include <block64.h>
#include <stddef.h>
#include <stdint.h>

static const struct BLOCK64_OPS *ops;
static uint64_t part_base;

static uint32_t read32(const uint8_t *p)
{
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
		((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static int looks_like_fat(const uint8_t *sector)
{
	/* filesystem type strings: FAT12/16 at 0x36, FAT32 at 0x52 */
	return (sector[54] == 'F' && sector[55] == 'A' && sector[56] == 'T') ||
		(sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T');
}

/* Sets part_base from the MBR when sector 0 carries one. The shipped image is
   a superfloppy (filesystem at LBA 0), so the common answer is 0. */
static void find_partition(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	const uint8_t *entry;
	uint32_t start;
	uint32_t i;

	part_base = 0;
	if (ops->read(0, 1, sector) != 0) {
		return;
	}
	if (sector[510] != 0x55 || sector[511] != 0xaa) {
		return;
	}
	for (i = 0; i < 4; i++) {
		entry = sector + 446 + i * 16;
		start = read32(entry + 8);
		if (entry[4] == 0x00 || start == 0) {
			continue;
		}
		/* trust the entry only if a filesystem really starts there: our own
		   boot sector also ends in 0x55aa and must not be read as an MBR */
		if (ops->read(start, 1, sector) != 0 || looks_like_fat(sector) == 0) {
			continue;
		}
		part_base = start;
		return;
	}
}

int block64_init(void)
{
	if (ops != NULL) {
		return 0;
	}
	ops = ahci64_probe() == 0 ? &ahci64_ops : &ata64_ops;
	find_partition();
	return 0;
}

const char *block64_transport(void)
{
	return ops == NULL ? "none" : ops->name;
}

uint64_t block64_part_base(void)
{
	return part_base;
}

uint64_t block64_sector_count(void)
{
	uint64_t total;

	if (ops == NULL) {
		return 0;
	}
	total = ops->sector_count();
	if (total <= part_base) {
		return 0;
	}
	return total - part_base;
}

int block64_read(uint64_t lba, uint32_t count, void *dst)
{
	if (ops == NULL || dst == NULL) {
		return -1;
	}
	return ops->read(part_base + lba, count, dst);
}

int block64_write(uint64_t lba, uint32_t count, const void *src)
{
	if (ops == NULL || src == NULL) {
		return -1;
	}
	return ops->write(part_base + lba, count, src);
}
