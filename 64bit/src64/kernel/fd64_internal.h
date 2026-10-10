#ifndef MOWKOW64_FD64_INTERNAL_H
#define MOWKOW64_FD64_INTERNAL_H

#include <block64.h>
#include <fd64.h>
#include <stddef.h>
#include <stdint.h>

#define FD64_SECTOR_SIZE BLOCK64_SECTOR_SIZE
#define FD64_FAT_EOC  0x0ffffff8
#define FD64_FAT_MASK 0x0fffffff
#define FD64_FAT_LAST 0x0fffffff
#define FD64_FAT_RESERVED 0x0ffffff0
#define FD64_MIN_DATA_CLUSTERS 65525
#define FD64_FIXED_DATE (((2026 - 1980) << 9) | (1 << 5) | 1)
#define FD64_FIXED_TIME 0
#define FD64_DIR_ENTRY_SIZE ((uint32_t) sizeof(struct FDINFO64))

struct FDCHAIN64 {
	uint32_t current;
	uint32_t hare;
	uint32_t remaining;
};

extern uint16_t fd64_bytes_per_sector;
extern uint8_t fd64_sectors_per_cluster;
extern uint32_t fd64_reserved_sectors;
extern uint8_t fd64_fat_count;
extern uint32_t fd64_sectors_per_fat;
extern uint32_t fd64_root_cluster;
extern uint32_t fd64_data_lba;
extern uint32_t fd64_total_sectors;
extern uint32_t fd64_max_cluster;
extern uint32_t fd64_alloc_hint;
extern int fd64_initialized;
extern int fd64_read_only;

uint16_t fd64_read16(const uint8_t *p);
uint32_t fd64_read32(const uint8_t *p);
uint32_t fd64_cluster_bytes(void);
uint32_t fd64_info_cluster(const struct FDINFO64 *info);
void fd64_info_set_cluster(struct FDINFO64 *info, uint32_t cluster);
int fd64_cluster_valid(uint32_t cluster);
int fd64_chain_init(struct FDCHAIN64 *chain, uint32_t first);
int fd64_chain_advance(struct FDCHAIN64 *chain, uint32_t *next);
int fd64_chain_validate(uint32_t first, uint32_t required_clusters);
uint32_t fd64_next_cluster_unlocked(uint32_t cluster);
void fd64_mark_read_only(void);
struct FDINFO64 *fd64_dir_at(const struct FDPOS64 *pos, int mode);
uint8_t *fd64_cluster_sector(uint32_t cluster, uint32_t offset, int mode);
int fd64_fat_set(uint32_t cluster, uint32_t value);
uint32_t fd64_alloc_cluster(void);
int fd64_free_chain(uint32_t cluster);
int fd64_zero_cluster(uint32_t cluster);

void fd64_dir_first(struct FDPOS64 *pos);
int fd64_dir_scan_next(struct FDPOS64 *pos, struct FDINFO64 *out,
	struct FDPOS64 *at, char *name, size_t name_size);
int fd64_dir_find(const char *name, struct FDINFO64 *out,
	struct FDPOS64 *at);
int fd64_dir_write(struct FDHANDLE64 *fh);
int fd64_dir_create(struct FDHANDLE64 *fh, const char *name);

#endif
