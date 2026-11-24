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
/* Internal FAT FD operations, limited to slots 3..MAX_FILES-1. Task context
 * only, under the single-execution-flow ownership above; no user ABI yet.
 * Both return 0 or negative errno. dup chooses the lowest free slot >= minimum
 * and writes a kernel-owned newfd only on success. dup2 atomically replaces its
 * target and clears CLOEXEC; self-dup preserves it. Failure preserves target.
 */
int process_file_dup64(struct PROCESS64 *process, int oldfd, int minimum,
	int cloexec, int *newfd);
int process_file_dup264(struct PROCESS64 *process, int oldfd, int newfd);
void process_file_close64(struct PROCESS64_FILE *file);
void process_files_close64(struct PROCESS64 *process, int cloexec_only);
int process_file_lock64(struct PROCESS64_FILE *file, struct FDHANDLE64 **handle);
void process_file_unlock64(struct PROCESS64_FILE *file);

#endif
