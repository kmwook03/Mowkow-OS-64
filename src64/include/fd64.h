#ifndef MOWKOW64_FD64_H
#define MOWKOW64_FD64_H

#include <stddef.h>
#include <stdint.h>

#define FD64_NAME_LEN 11

struct FDINFO64 {
	uint8_t name[8];
	uint8_t ext[3];
	uint8_t type;
	uint8_t reserved[10];
	uint16_t time;
	uint16_t date;
	uint16_t clustno;
	uint32_t size;
} __attribute__((packed));

struct FDHANDLE64 {
	struct FDINFO64 *finfo;
	uint32_t pos;
	uint16_t cluster;
};

int fd64_init(void);
uint32_t fd64_file_count(void);
const struct FDINFO64 *fd64_file_at(uint32_t index);
int fd64_open(struct FDHANDLE64 *fh, const char *name);
size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t request_size);
int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence);
uint16_t fd64_next_cluster(uint16_t cluster);
int fd64_create(struct FDHANDLE64 *fh, const char *name);
size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size);
int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size);
/* writes back dirty sectors only; returns sectors written, -1 on I/O error */
int fd64_sync(void);

#endif
