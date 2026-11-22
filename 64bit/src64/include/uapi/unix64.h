#ifndef MOWKOW64_UAPI_UNIX64_H
#define MOWKOW64_UAPI_UNIX64_H

/* Shared kernel/SDK wire ABI. Do not include kernel or libc-private types. */
#define UNIX64_ABI_LEGACY 0
#define UNIX64_ABI_VERSION 1
#define UNIX64_NOTE_NAME "MOWKOW"
#define UNIX64_NOTE_NAMESZ 7
#define UNIX64_NOTE_TYPE 1
#define UNIX64_NOTE_DESCSZ 4
#define UNIX64_SYS_FIRST 0x100
#define UNIX64_SYS_LAST 0x1ff

/* Reserved process calls, not implemented by the A1.1 dispatcher. */
#define UNIX64_SYS_EXIT 0x100       /* status */
#define UNIX64_SYS_READ 0x101       /* fd, buffer, count -> signed byte count */
#define UNIX64_SYS_WRITE 0x102      /* fd, buffer, count -> signed byte count */
#define UNIX64_SYS_FORK 0x103       /* -> child PID in parent, 0 in child */
#define UNIX64_SYS_EXECVE 0x104     /* path, argv, envp */
#define UNIX64_SYS_WAITPID 0x105    /* pid, int32_t *status, options -> PID */
#define UNIX64_SYS_GETPID 0x106
#define UNIX64_SYS_GETPPID 0x107

#define UNIX64_ENOEXEC 8
#define UNIX64_ENOSYS 38
#define UNIX64_ERRNO_MAX 4095
#define UNIX64_PAGE_SIZE 4096

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

/* Pointers on the wire are uint64_t user addresses, never kernel pointers.
 * Sysdeps convert these records field by field to libc structures. Callers
 * zero reserved fields; the kernel never exports its padding or private data.
 */
struct UNIX_TIMESPEC64 {
	int64_t seconds;
	int64_t nanoseconds;
};

struct UNIX_STAT64 {
	uint64_t device;
	uint64_t inode;
	uint64_t links;
	uint64_t special_device;
	int64_t size;
	int64_t blocks;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint32_t block_size;
	struct UNIX_TIMESPEC64 access_time;
	struct UNIX_TIMESPEC64 modify_time;
	struct UNIX_TIMESPEC64 change_time;
	uint64_t reserved[2];
};

#ifdef __cplusplus
#define UNIX64_ASSERT(condition, message) static_assert(condition, message)
#else
#define UNIX64_ASSERT(condition, message) _Static_assert(condition, message)
#endif
UNIX64_ASSERT(sizeof(void *) == 8 && sizeof(long) == 8, "UNIX v1 requires LP64");
UNIX64_ASSERT(sizeof(struct UNIX_TIMESPEC64) == 16, "timespec wire size");
UNIX64_ASSERT(offsetof(struct UNIX_TIMESPEC64, nanoseconds) == 8, "timespec ns");
UNIX64_ASSERT(sizeof(struct UNIX_STAT64) == 128, "stat wire size");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, device) == 0, "stat device");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, inode) == 8, "stat inode");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, links) == 16, "stat links");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, special_device) == 24, "stat rdev");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, size) == 32, "stat size");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, blocks) == 40, "stat blocks");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, mode) == 48, "stat mode");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, uid) == 52, "stat uid");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, gid) == 56, "stat gid");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, block_size) == 60, "stat block size");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, access_time) == 64, "stat atime");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, modify_time) == 80, "stat mtime");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, change_time) == 96, "stat ctime");
UNIX64_ASSERT(offsetof(struct UNIX_STAT64, reserved) == 112, "stat reserved");
#undef UNIX64_ASSERT

/* mlibc sysdeps return positive errno and put successful data in out params. */
static inline int unix64_result_errno(int64_t result)
{
	return result < 0 && result >= -UNIX64_ERRNO_MAX ? (int) -result : 0;
}
#endif

#endif
