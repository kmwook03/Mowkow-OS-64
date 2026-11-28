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

/* Process syscall numbers; unimplemented operations return -ENOSYS. */
#define UNIX64_SYS_EXIT 0x100       /* status */
#define UNIX64_SYS_READ 0x101       /* fd, buffer, count -> signed byte count */
#define UNIX64_SYS_WRITE 0x102      /* fd, buffer, count -> signed byte count */
#define UNIX64_SYS_FORK 0x103       /* -> child PID in parent, 0 in child */
#define UNIX64_SYS_EXECVE 0x104     /* path, argv, envp */
#define UNIX64_SYS_WAITPID 0x105    /* pid, int32_t *status, options -> PID */
#define UNIX64_SYS_GETPID 0x106
#define UNIX64_SYS_GETPPID 0x107
#define UNIX64_SYS_TCB_SET 0x108     /* aligned writable user thread pointer */
#define UNIX64_SYS_ANON_ALLOC 0x109  /* page-multiple size -> zeroed RW address */
#define UNIX64_SYS_ANON_FREE 0x10a   /* exact address, size; internal libc arena */
#define UNIX64_SYS_OPEN 0x10b        /* root filename, UNIX64_OPEN_* flags -> fd */
#define UNIX64_SYS_CLOSE 0x10c       /* fd */
#define UNIX64_SYS_SEEK 0x10d        /* fd, signed offset, whence -> offset */
#define UNIX64_SYS_ISATTY 0x10e      /* fd -> 0 or negative errno */
#define UNIX64_SYS_FUTEX_WAIT 0x10f  /* private int32 address, value, relative timespec */
#define UNIX64_SYS_FUTEX_WAKE 0x110  /* private int32 address -> number woken */
#define UNIX64_SYS_CLOCK_GET 0x111   /* monotonic timespec output, 10-ms resolution */
/* A1 internal signal 32. Not POSIX sigaction/kill/job control.
 * SET(handler, restorer), zero/zero disables; RAISE() calls handler(32).
 * One active delivery; nested RAISE returns EBUSY. RETURN() is restorer-only.
 */
#define UNIX64_SYS_SIGNAL_SET 0x112
#define UNIX64_SYS_SIGNAL_RAISE 0x113
#define UNIX64_SYS_SIGNAL_RETURN 0x114
#define UNIX64_SYS_SIGNAL_SEND 0x115 /* positive PID; coalesced, deferred while active */
#define UNIX64_SYS_FCNTL 0x116       /* fd, wire command, argument */
/* oldfd, newfd -> newfd over every slot, stdio included. Invalid source/range
 * is EBADF. Replacement clears CLOEXEC; self-dup keeps it. Failure preserves
 * the target.
 */
#define UNIX64_SYS_DUP2 0x117
/* Wire commands are independent of libc's F_* values. GETFD ignores argument;
 * SETFD accepts only FD_CLOEXEC bits. Unsupported commands return EINVAL.
 */
#define UNIX64_F_GETFD 1
#define UNIX64_F_SETFD 2
/* Any open source; out-of-range minimum is EINVAL.
 * Returns the lowest free FD >= minimum, or EMFILE when no slot remains.
 * DUPFD clears CLOEXEC; DUPFD_CLOEXEC sets it. Both share the source offset.
 */
#define UNIX64_F_DUPFD 3
#define UNIX64_F_DUPFD_CLOEXEC 4
/* Per-OFD status word, shared by dup/fork aliases. GETFL ignores argument and
 * returns an FL access mode plus APPEND/NONBLOCK. SETFL ignores access-mode
 * bits, replaces APPEND/NONBLOCK and rejects other bits with EINVAL.
 * NONBLOCK on a stream (console) is ENOTSUP. FL values are not open flags.
 */
#define UNIX64_F_GETFL 5
#define UNIX64_F_SETFL 6
#define UNIX64_FL_RDONLY 0
#define UNIX64_FL_WRONLY 1
#define UNIX64_FL_RDWR 2
#define UNIX64_FL_ACCMODE 3
#define UNIX64_FL_APPEND 4
#define UNIX64_FL_NONBLOCK 8
#define UNIX64_FD_CLOEXEC 1
/* OPEN flags: no access bit is read-only; WRONLY and RDWR are exclusive.
 * CREAT/TRUNC need write access and act on the bounded FAT root only;
 * CREAT never fails on an existing file (no EXCL). APPEND sets UNIX64_FL_APPEND.
 * Other bits are EINVAL.
 */
#define UNIX64_OPEN_CLOEXEC 1
#define UNIX64_OPEN_WRONLY 2
#define UNIX64_OPEN_RDWR 4
#define UNIX64_OPEN_CREAT 8
#define UNIX64_OPEN_TRUNC 16
#define UNIX64_OPEN_APPEND 32
#define UNIX64_WAIT_NOHANG 1

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
