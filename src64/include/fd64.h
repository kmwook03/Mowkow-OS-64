#ifndef MOWKOW64_FD64_H
#define MOWKOW64_FD64_H

#include <stddef.h>
#include <stdint.h>

#define FD64_NAME_LEN 11
/* VFAT allows 255 UTF-16 units; this kernel caps names shorter so the buffers
   that carry them stay stack-sized. Both limits are enforced on every path
   that accepts a name. */
#define FD64_LFN_MAX_UNITS 52
#define FD64_NAME_MAX 160

struct FDINFO64 {
	uint8_t name[8];
	uint8_t ext[3];
	uint8_t type;
	uint8_t reserved[8];
	uint16_t clustno_hi;
	uint16_t time;
	uint16_t date;
	uint16_t clustno;
	uint32_t size;
} __attribute__((packed));

/* Where a directory entry lives. The FAT32 root is a cluster chain, so an
   index into a fixed-size array no longer locates one. cluster == 0 means
   "no entry": the handle is closed. */
struct FDPOS64 {
	uint32_t cluster;
	uint32_t offset;
};

/* Cached blocks move, so a handle may not hold a pointer into one: it keeps a
   copy of the directory entry plus the entry's location on disk. */
struct FDHANDLE64 {
	struct FDINFO64 info;
	struct FDPOS64 dir;
	uint32_t pos;
	uint32_t cluster;
};

int fd64_init(void);
uint32_t fd64_file_count(void);
/* Copies entry `index` into *out and its name (the VFAT long name when the
   entry has one, else the 8.3 name) into `name`. Either output may be NULL.
   Returns 1 if the entry exists, 0 otherwise. */
int fd64_file_at(uint32_t index, struct FDINFO64 *out, char *name, size_t name_size);
int fd64_open(struct FDHANDLE64 *fh, const char *name);
size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t request_size);
int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence);
uint32_t fd64_next_cluster(uint32_t cluster);
int fd64_create(struct FDHANDLE64 *fh, const char *name);
size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size);
int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size);
/* writes back dirty sectors only; returns sectors written, -1 on I/O error */
int fd64_sync(void);

#endif
