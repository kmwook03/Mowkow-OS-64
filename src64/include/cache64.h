#ifndef MOWKOW64_CACHE64_H
#define MOWKOW64_CACHE64_H

#include <stdint.h>

#define CACHE64_BLOCK_SECTORS 8
#define CACHE64_BLOCKS 1024

/* cache64_get modes */
#define CACHE64_READ 0
#define CACHE64_WRITE 1
/* Metadata (directory entries): flushed after file data and the FAT, so a
   failed flush can only ever leave orphan clusters, never a directory entry
   pointing at an unwritten chain. */
#define CACHE64_WRITE_META 2

int cache64_init(void);
/* Pointer to the cached copy of one sector, NULL on I/O error. A write mode
   marks the containing block dirty. The pointer is only valid until the next
   cache64_get: a miss can evict any block, this one included. */
uint8_t *cache64_get(uint32_t lba, int mode);
/* Writes back dirty blocks whose first sector is in [start, end) and whose
   metadata flag matches `meta`. Returns sectors written, -1 on I/O error. */
int cache64_flush(uint32_t start, uint32_t end, int meta);
/* Marks every dirty block clean without writing it. */
void cache64_discard_dirty(void);

#endif
