#ifndef MOWKOW64_PROCESS_FILE64_H
#define MOWKOW64_PROCESS_FILE64_H

#include <process64.h>
#include <mutex64.h>

/* FAT OFD: task context, lock before the FAT/cache mutex.
 * An installed descriptor retains a reference through sleeping I/O. One
 * execution flow per process; close/exec cannot race that process's syscall.
 */
struct UNIX_FILE64 {
	unsigned int references;
	struct MUTEX64 mutex;
	struct FDHANDLE64 handle;
};

int process_file_inherit64(struct PROCESS64 *parent, struct PROCESS64 *child);
/* name is a kernel-owned snapshot; flags use fd64_open_checked's contract.
 * Reserve an OFD before backend I/O, publish the FD/output only on success.
 * Task context, one execution flow per table; slots 3..MAX_FILES-1 only.
 */
int process_file_open64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int cloexec, int *newfd);
/* Legacy backend sequencing; flags are FILE_CREATE64/FILE_TRUNCATE64.
 * Returns errno-style failures; the syscall boundary maps legacy errors.
 */
int process_file_open_legacy64(struct PROCESS64 *process, const char *name,
	unsigned int flags, int *newfd);
/* Internal FAT FD operations, limited to slots 3..MAX_FILES-1. Task context
 * only, under the single-execution-flow ownership above; no user ABI yet.
 * Both return 0 or negative errno. dup chooses the lowest free slot >= minimum
 * and writes a kernel-owned newfd only on success. dup2 atomically replaces its
 * target and clears CLOEXEC; self-dup preserves it. Failure preserves target.
 */
int process_file_dup64(struct PROCESS64 *process, int oldfd, int minimum,
	int cloexec, int *newfd);
int process_file_dup264(struct PROCESS64 *process, int oldfd, int newfd);
/* Same task/table ownership as dup; includes open console descriptors 0..2.
 * CLOEXEC is 0 or 1. Stdio stores only this flag in its file-table slot.
 * Get writes its kernel-owned output only on success; set preserves aliases.
 */
int process_file_get_cloexec64(struct PROCESS64 *process, int fd, int *cloexec);
int process_file_set_cloexec64(struct PROCESS64 *process, int fd, int cloexec);
/* Close an open stdio/file descriptor under the same task/table ownership.
 * Returns 0, -EBADF for closed/out-of-range FD, or -EINVAL for NULL process.
 * Clears descriptor flags; aliases retain their OFD and offset.
 */
int process_fd_close64(struct PROCESS64 *process, int fd);
void process_file_close64(struct PROCESS64_FILE *file);
void process_files_close64(struct PROCESS64 *process, int cloexec_only);
int process_file_lock64(struct PROCESS64_FILE *file, struct FDHANDLE64 **handle);
void process_file_unlock64(struct PROCESS64_FILE *file);

#endif
