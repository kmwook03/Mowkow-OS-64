#ifndef MOWKOW64_PROCESS_FILE64_H
#define MOWKOW64_PROCESS_FILE64_H

#include <process64.h>
#include <mutex64.h>

struct UNIX_FILE64;

/* Immutable for the OFD lifetime. Called in task context; callbacks may sleep
 * but must not reacquire the OFD mutex or access user memory. Seekable OFDs
 * hold that mutex around every callback. Streams (no seek) have no shared
 * cursor and synchronize internally, so a blocked reader never stalls a writer.
 * Missing read/write is EBADF (wrong direction); missing seek is ESPIPE.
 * Read/write/seek return signed count/offset or errno; stat returns 0 or
 * errno. Failed seek must preserve backend state. Stat writes only its snapshot.
 */
struct PROCESS_FILE_OPS64 {
	int64_t (*read)(struct UNIX_FILE64 *file, void *buffer, size_t size);
	int64_t (*write)(struct UNIX_FILE64 *file, const void *buffer, size_t size);
	int64_t (*seek)(struct UNIX_FILE64 *file, int64_t offset, int whence);
	int (*stat)(struct UNIX_FILE64 *file, struct FILE_INFO64 *info);
};

#define PROCESS_FILE_READ64 1U
#define PROCESS_FILE_WRITE64 2U
#define PROCESS_FILE_APPEND64 4U
/* Accepted only where reads cannot block (seekable files) until A3's TTY. */
#define PROCESS_FILE_NONBLOCK64 8U
#define PROCESS_FILE_STATUS64 (PROCESS_FILE_APPEND64 | PROCESS_FILE_NONBLOCK64)

/* OFD: task context, lock before the FAT/cache mutex.
 * An installed descriptor retains a reference through sleeping I/O. One
 * execution flow per process; close/exec cannot race that process's syscall.
 */
struct UNIX_FILE64 {
	unsigned int references;
	/* PROCESS_FILE_* bits. Access bits are fixed at open; APPEND/NONBLOCK
	 * change under irq-save and each operation snapshots them once.
	 */
	unsigned int flags;
	struct MUTEX64 mutex;
	const struct PROCESS_FILE_OPS64 *ops;
	struct FDHANDLE64 handle;
	/* Console streams only; outlived by every process that can share the OFD. */
	struct CONSOLE64 *console;
};

int process_file_inherit64(struct PROCESS64 *parent, struct PROCESS64 *child);
/* Install console stdio in an unpublished process with an empty table:
 * fd 0 is a read-only input OFD and fds 1/2 share one write-only output OFD.
 * Returns 0 or -ENFILE, leaving the table empty on failure.
 */
int process_file_stdio64(struct PROCESS64 *process, struct CONSOLE64 *console);
/* Nonzero when an open descriptor names a console stream. irq-safe snapshot. */
int process_file_tty64(const struct PROCESS64_FILE *file);
/* Validate an open descriptor's access mode for one direction without I/O:
 * 0 or -EBADF.
 */
int process_file_check64(const struct PROCESS64_FILE *file, int writing);
/* name is a kernel-owned snapshot; flags use fd64_open_checked's contract.
 * FILE_WRITING64 opens read/write, otherwise read-only.
 * Reserve an OFD before backend I/O, publish the FD/output only on success.
 * Task context, one execution flow per table; takes the lowest free slot.
 */
int process_file_open64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int cloexec, int *newfd);
/* Legacy backend sequencing; flags are FILE_CREATE64/FILE_TRUNCATE64.
 * Always read/write.
 * Never returns a stdio slot. Returns errno-style failures; the syscall
 * boundary maps legacy errors.
 */
int process_file_open_legacy64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int *newfd);
/* Descriptor duplication over every slot, including stdio. Task context only,
 * under the single-execution-flow ownership above. Both return 0 or errno.
 * dup chooses the lowest free slot >= minimum and writes a kernel-owned newfd
 * only on success. dup2 atomically replaces its target and clears CLOEXEC;
 * self-dup preserves it. Failure preserves target.
 */
int process_file_dup64(struct PROCESS64 *process, int oldfd, int minimum,
	int cloexec, int *newfd);
int process_file_dup264(struct PROCESS64 *process, int oldfd, int newfd);
/* Same task/table ownership as dup. CLOEXEC is 0 or 1 per descriptor.
 * Get writes its kernel-owned output only on success; set preserves aliases.
 */
int process_file_get_cloexec64(struct PROCESS64 *process, int fd, int *cloexec);
int process_file_set_cloexec64(struct PROCESS64 *process, int fd, int cloexec);
/* Same task/table ownership as dup. Status is per OFD, shared by every alias.
 * Get writes PROCESS_FILE_* bits only on success. Set replaces APPEND/NONBLOCK
 * and keeps access bits; other bits are EINVAL, NONBLOCK on a stream ENOTSUP.
 */
int process_file_get_status64(struct PROCESS64 *process, int fd, unsigned int *flags);
int process_file_set_status64(struct PROCESS64 *process, int fd, unsigned int flags);
/* Close an open stdio/file descriptor under the same task/table ownership.
 * Returns 0, -EBADF for closed/out-of-range FD, or -EINVAL for NULL process.
 * Clears descriptor flags; aliases retain their OFD and offset.
 */
int process_fd_close64(struct PROCESS64 *process, int fd);
void process_file_close64(struct PROCESS64_FILE *file);
void process_files_close64(struct PROCESS64 *process, int cloexec_only);
/* Read once into a kernel-owned buffer, holding a seekable OFD's cursor mutex.
 * Task context; caller retains the descriptor through sleeping I/O and owns
 * buffer capacity. Returns signed byte count/errno. Errors may advance offset;
 * caller must discard buffer contents on error. No user-memory access here.
 */
int64_t process_file_read64(struct PROCESS64_FILE *file, void *buffer, size_t size);
/* Same task/descriptor ownership as read, from a kernel-owned buffer. Returns a
 * partial count or errno. APPEND moves a seekable cursor to EOF first under the
 * same mutex. FAT reports every failed nonempty write as EIO.
 */
int64_t process_file_write64(struct PROCESS64_FILE *file, const void *buffer,
	size_t size);
/* Same task/descriptor ownership as read. whence: 0=start, 1=current, 2=end.
 * Returns new offset/errno; errors preserve the entire shared cursor state.
 * Negative positions are EINVAL; positions beyond EOF remain ENOTSUP.
 */
int64_t process_file_seek64(struct PROCESS64_FILE *file, int64_t offset, int whence);
/* Checked metadata snapshot, same task/descriptor ownership as read.
 * Kernel-owned output is published only on success; returns 0 or errno.
 */
int process_file_stat64(struct PROCESS64_FILE *file, struct FILE_INFO64 *info);

#endif
