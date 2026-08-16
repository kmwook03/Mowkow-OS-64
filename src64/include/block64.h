#ifndef MOWKOW64_BLOCK64_H
#define MOWKOW64_BLOCK64_H

#include <stdint.h>

#define BLOCK64_SECTOR_SIZE 512

/* One storage transport. LBAs here are absolute: the partition offset is
   applied by block64_read/block64_write before dispatching. */
struct BLOCK64_OPS {
	const char *name;
	int (*read)(uint64_t lba, uint32_t count, void *dst);
	int (*write)(uint64_t lba, uint32_t count, const void *src);
	uint64_t (*sector_count)(void);
};

/* Picks a transport (AHCI when present, ATA PIO otherwise) and reads the
   partition table. Must run before any other call here. */
int block64_init(void);
const char *block64_transport(void);
/* LBA offset added to every request: 0 for a superfloppy, the partition's
   first sector when the disk carries an MBR. */
uint64_t block64_part_base(void);
/* total sectors on the volume, 0 when the device did not report a size */
uint64_t block64_sector_count(void);
int block64_read(uint64_t lba, uint32_t count, void *dst);
int block64_write(uint64_t lba, uint32_t count, const void *src);

extern const struct BLOCK64_OPS ata64_ops;
extern const struct BLOCK64_OPS ahci64_ops;
/* 0 when an AHCI controller was found and a port was brought up */
int ahci64_probe(void);

#endif
