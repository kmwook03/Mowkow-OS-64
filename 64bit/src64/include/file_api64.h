#ifndef MOWKOW64_FILE_API64_H
#define MOWKOW64_FILE_API64_H

#include <stdint.h>

/* Additive SYS_FILE ABI. Existing calls 1..10 keep their legacy semantics.
 * Task context only. Results are 0/count/fd or negative errno values.
 * STAT: path, output; OPEN: path, flags; READ: fd, buffer, count;
 * SYNC: fd; FSTAT: fd, output. Unused arguments must be zero.
 * READ reports EIO on an incomplete pre-EOF read, even after copying a prefix
 * and advancing the handle. Callers must discard that prefix on failure.
 * SYNC flushes the shared filesystem in data/FAT/directory order. It does not
 * promise power-loss atomicity or a device write-cache flush.
 */
#define FILE_NAME_CAPACITY64 160
#define FILE_STAT64 0
#define FILE_OPEN64 1
#define FILE_READ64 2
#define FILE_SYNC64 3
#define FILE_FSTAT64 4
#define FILE_CREATE64 1
#define FILE_TRUNCATE64 2
#define FILE_WRITING64 4
#define FILE_READONLY64 1
#define FILE_DIRECTORY64 2
#define FILE_VOLUME_READONLY64 4

/* FAT has no uid/gid or POSIX permissions. Expose actual attributes and FAT
 * modification date/time instead. Identity is the directory entry location.
 */
struct FILE_INFO64 {
	uint64_t identity64;
	uint32_t size64;
	uint32_t flags64;
	uint16_t date64;
	uint16_t time64;
	uint32_t reserved64;
};
_Static_assert(sizeof(struct FILE_INFO64) == 24, "file metadata ABI size");

#endif
